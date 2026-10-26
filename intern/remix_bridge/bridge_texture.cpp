/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file bridge_texture.cpp
 *  \ingroup remix_bridge
 *
 * The texture interface (version 3): Blender's pixels to DDS files as RTX
 * Remix reads them, with DirectXTex.
 *
 *   colour    -> BC7 holding sRGB-encoded values in a plain UNORM format:
 *                Remix's material shaders convert albedo and emission from
 *                gamma to linear themselves (gammaToLinear, "assuming all
 *                textures are not using sRGB formats"), so an _SRGB format
 *                would be decoded twice (P3 RTX run: dark, oversaturated)
 *   normal    -> BC5, Remix's unsigned hemisphere-octahedral encoding (the
 *                opaque material shader decodes every normal map with
 *                unsignedOctahedralToHemisphereDirection), in Direct3D's
 *                tangent convention (green down, as Remix samples with v
 *                flipped)
 *   scalar    -> BC4
 *   HDR       -> BC6H (unsigned)
 *
 * Every file gets a full mip chain. BC6H and BC7 are compressed on the GPU
 * (DirectCompute) when there is one; otherwise, and for BC4/BC5, on the CPU,
 * big images split into strips over several threads.
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "bridge_intern.h"

#include <d3d11.h>
#include <DirectXTex.h>

namespace brb {

namespace {

struct Job {
	int ticket = 0;
	std::wstring path;
	int kind = 0, width = 0, height = 0, channel = 0, flags = 0;
	std::vector<unsigned char> rgba8;
	std::vector<float> rgbaf;
};

struct Pixels {
	/* a copy of the caller's pixels or a view of them */
	const unsigned char *rgba8 = nullptr;
	const float *rgbaf = nullptr;
};

double seconds_since(std::chrono::steady_clock::time_point t0)
{
	return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void set_error(brb_texture_result &r, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(r.error, sizeof(r.error), fmt, ap);
	va_end(ap);
	r.state = -1;
}

float srgb_to_linear(float c)
{
	return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float c)
{
	c = std::min(std::max(c, 0.0f), 1.0f);
	return (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

unsigned char to_byte(float v)
{
	return (unsigned char)std::lround(std::min(std::max(v, 0.0f), 1.0f) * 255.0f);
}

/* Value of texel (x, y) counted from the top, channel c, in 0..1 (bytes) or
 * as stored (floats). Blender's rows start at the bottom. */
inline float texel(const Pixels &p, int w, int h, int x, int y, int c)
{
	const size_t i = ((size_t)(h - 1 - y) * (size_t)w + (size_t)x) * 4 + (size_t)c;
	return p.rgba8 ? p.rgba8[i] / 255.0f : p.rgbaf[i];
}

/* ------------------------------------------------------------------------- */
/* Normal maps */

/* Remix's hemisphereDirectionToUnsignedOctahedral (packing.slangh) */
void normal_to_octahedral(float x, float y, float z, unsigned char out[2])
{
	z = std::max(z, 0.0f);
	const float d = std::fabs(x) + std::fabs(y) + z;
	float px = 0.0f, py = 0.0f;
	if (d > 0.0f) {
		px = x / d;
		py = y / d;
	}
	out[0] = to_byte((px + py) * 0.5f + 0.5f);
	out[1] = to_byte((px - py) * 0.5f + 0.5f);
}

void normalize3(float v[3])
{
	const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 1e-8f) {
		v[0] /= l;
		v[1] /= l;
		v[2] /= l;
	}
	else {
		v[0] = v[1] = 0.0f;
		v[2] = 1.0f;
	}
}

/* Mips of the unit vectors (averaged and normalized again), then each level
 * in octahedral form as R8G8: averaging the encoded values would bend the
 * normals of distant surfaces. */
HRESULT build_normal_mips(const Job &job, const Pixels &px, DirectX::ScratchImage &out)
{
	const int w0 = job.width, h0 = job.height;
	std::vector<float> level((size_t)w0 * h0 * 3);
	for (int y = 0; y < h0; y++) {
		for (int x = 0; x < w0; x++) {
			float n[3];
			for (int c = 0; c < 3; c++) {
				/* stored 0..1 in bytes and floats alike (16-bit PNG, EXR) */
				n[c] = texel(px, w0, h0, x, y, c) * 2.0f - 1.0f;
			}
			/* Blender's maps are OpenGL style (green: up the image); Remix
			 * samples with v flipped, so up the image is -v for it */
			n[1] = -n[1];
			normalize3(n);
			memcpy(&level[((size_t)y * w0 + x) * 3], n, sizeof(n));
		}
	}
	size_t levels = 1;
	for (int w = w0, h = h0; w > 1 || h > 1; w = std::max(w / 2, 1), h = std::max(h / 2, 1)) {
		levels++;
	}
	HRESULT hr = out.Initialize2D(DXGI_FORMAT_R8G8_UNORM, (size_t)w0, (size_t)h0, 1, levels);
	if (FAILED(hr)) {
		return hr;
	}
	int w = w0, h = h0;
	for (size_t m = 0; m < levels; m++) {
		const DirectX::Image *img = out.GetImage(m, 0, 0);
		for (int y = 0; y < h; y++) {
			unsigned char *row = img->pixels + (size_t)y * img->rowPitch;
			for (int x = 0; x < w; x++) {
				const float *n = &level[((size_t)y * w + x) * 3];
				normal_to_octahedral(n[0], n[1], n[2], row + x * 2);
			}
		}
		if (m + 1 == levels) {
			break;
		}
		const int nw = std::max(w / 2, 1), nh = std::max(h / 2, 1);
		std::vector<float> next((size_t)nw * nh * 3);
		for (int y = 0; y < nh; y++) {
			for (int x = 0; x < nw; x++) {
				float s[3] = {0.0f, 0.0f, 0.0f};
				for (int dy = 0; dy < 2; dy++) {
					for (int dx = 0; dx < 2; dx++) {
						const int sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
						const float *n = &level[((size_t)sy * w + sx) * 3];
						s[0] += n[0];
						s[1] += n[1];
						s[2] += n[2];
					}
				}
				normalize3(s);
				memcpy(&next[((size_t)y * nw + x) * 3], s, sizeof(s));
			}
		}
		level.swap(next);
		w = nw;
		h = nh;
	}
	return S_OK;
}

/* ------------------------------------------------------------------------- */
/* The top level, before mips */

HRESULT build_base(const Job &job, const Pixels &px, DirectX::ScratchImage &out, DXGI_FORMAT &r_target)
{
	const int w = job.width, h = job.height;
	HRESULT hr = E_INVALIDARG;
	switch (job.kind) {
		case BRB_TEXTURE_COLOR: {
			/* always sRGB-encoded values in the end (see the top); the mips
			 * are made in an _SRGB format so they average in linear light,
			 * and the format is relabelled UNORM before compression */
			const bool linear_bytes = (job.flags & BRB_TEXTURE_LINEAR) != 0;
			hr = out.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, w, h, 1, 1);
			if (FAILED(hr)) {
				return hr;
			}
			const DirectX::Image *img = out.GetImage(0, 0, 0);
			for (int y = 0; y < h; y++) {
				unsigned char *row = img->pixels + (size_t)y * img->rowPitch;
				for (int x = 0; x < w; x++) {
					for (int c = 0; c < 4; c++) {
						float v = texel(px, w, h, x, y, c);
						if (c < 3 && (px.rgbaf || linear_bytes)) {
							v = linear_to_srgb(v);
						}
						row[x * 4 + c] = to_byte(v);
					}
				}
			}
			r_target = DXGI_FORMAT_BC7_UNORM;
			return S_OK;
		}
		case BRB_TEXTURE_SCALAR: {
			hr = out.Initialize2D(DXGI_FORMAT_R8_UNORM, w, h, 1, 1);
			if (FAILED(hr)) {
				return hr;
			}
			const int c = std::min(std::max(job.channel, 0), 3);
			const bool invert = (job.flags & BRB_TEXTURE_INVERT) != 0;
			const DirectX::Image *img = out.GetImage(0, 0, 0);
			for (int y = 0; y < h; y++) {
				unsigned char *row = img->pixels + (size_t)y * img->rowPitch;
				for (int x = 0; x < w; x++) {
					const float v = texel(px, w, h, x, y, c);
					row[x] = to_byte(invert ? 1.0f - v : v);
				}
			}
			r_target = DXGI_FORMAT_BC4_UNORM;
			return S_OK;
		}
		case BRB_TEXTURE_HDR: {
			hr = out.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, w, h, 1, 1);
			if (FAILED(hr)) {
				return hr;
			}
			const DirectX::Image *img = out.GetImage(0, 0, 0);
			for (int y = 0; y < h; y++) {
				float *row = (float *)(img->pixels + (size_t)y * img->rowPitch);
				for (int x = 0; x < w; x++) {
					for (int c = 0; c < 4; c++) {
						float v = texel(px, w, h, x, y, c);
						if (px.rgba8 && c < 3) {
							v = srgb_to_linear(v);
						}
						row[x * 4 + c] = std::max(v, 0.0f);
					}
				}
			}
			r_target = DXGI_FORMAT_BC6H_UF16;
			return S_OK;
		}
		default:
			return E_INVALIDARG;
	}
}

/* ------------------------------------------------------------------------- */
/* Compression */

#ifdef BRB_DIRECTXTEX_GPU
/* The Direct3D 11 device for compression, made once; NULL without a usable
 * GPU (CI, remote desktop). Its immediate context is not thread-safe, so
 * every use holds g_gpu_mutex. */
std::mutex g_gpu_mutex;

ID3D11Device *gpu_device()
{
	static bool tried = false;
	static ID3D11Device *device = nullptr;
	if (!tried) {
		tried = true;
		const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
		D3D_FEATURE_LEVEL got;
		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
		                               D3D11_SDK_VERSION, &device, &got, nullptr);
		if (FAILED(hr)) {
			device = nullptr;
			log("textures: no Direct3D 11 GPU (0x%08lx), compressing on the CPU", (unsigned long)hr);
		}
		else {
			log("textures: compressing BC6H/BC7 on the GPU");
		}
	}
	return device;
}
#endif

/* One mip level on the CPU, in strips of 4-row blocks over several threads
 * (BC blocks are stored row after row, so the strips' blocks join up). */
HRESULT compress_level_cpu(const DirectX::Image &src, DXGI_FORMAT format, DirectX::TEX_COMPRESS_FLAGS flags,
                           const DirectX::Image &dst)
{
	const size_t block_rows = (src.height + 3) / 4;
	const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
	const size_t strips = std::min<size_t>(std::min<size_t>(hw, 16), std::max<size_t>(1, block_rows / 16));
	if (strips <= 1) {
		DirectX::ScratchImage tmp;
		HRESULT hr = DirectX::Compress(src, format, flags, DirectX::TEX_THRESHOLD_DEFAULT, tmp);
		if (SUCCEEDED(hr)) {
			memcpy(dst.pixels, tmp.GetImage(0, 0, 0)->pixels, std::min(dst.slicePitch, tmp.GetImage(0, 0, 0)->slicePitch));
		}
		return hr;
	}
	std::vector<HRESULT> results(strips, S_OK);
	std::vector<std::thread> threads;
	threads.reserve(strips);
	const size_t per = (block_rows + strips - 1) / strips;
	for (size_t s = 0; s < strips; s++) {
		const size_t b0 = s * per, b1 = std::min(block_rows, b0 + per);
		if (b0 >= b1) {
			break;
		}
		auto work = [&, s, b0, b1] {
			try {
				DirectX::Image part = src;
				part.height = std::min(src.height, b1 * 4) - b0 * 4;
				part.pixels = src.pixels + b0 * 4 * src.rowPitch;
				part.slicePitch = part.rowPitch * part.height;
				DirectX::ScratchImage tmp;
				HRESULT hr = DirectX::Compress(part, format, flags, DirectX::TEX_THRESHOLD_DEFAULT, tmp);
				if (SUCCEEDED(hr)) {
					const DirectX::Image *t = tmp.GetImage(0, 0, 0);
					memcpy(dst.pixels + b0 * dst.rowPitch, t->pixels, std::min(t->slicePitch, (b1 - b0) * dst.rowPitch));
				}
				results[s] = hr;
			}
			catch (...) {
				results[s] = E_OUTOFMEMORY;
			}
		};
		try {
			threads.emplace_back(work);
		}
		catch (...) {
			/* no thread left (a destroyed joinable std::thread ends the
			 * process): this strip on this thread */
			work();
		}
	}
	for (std::thread &t : threads) {
		t.join();
	}
	for (HRESULT hr : results) {
		if (FAILED(hr)) {
			return hr;
		}
	}
	return S_OK;
}

HRESULT compress(const DirectX::ScratchImage &mips, DXGI_FORMAT format, int flags, DirectX::ScratchImage &out,
                 int &r_gpu)
{
	r_gpu = 0;
	const DirectX::TexMetadata &meta = mips.GetMetadata();
#ifdef BRB_DIRECTXTEX_GPU
	if ((format == DXGI_FORMAT_BC7_UNORM || format == DXGI_FORMAT_BC6H_UF16) &&
	    !(flags & BRB_TEXTURE_NO_GPU))
	{
		std::lock_guard<std::mutex> lock(g_gpu_mutex);
		if (ID3D11Device *device = gpu_device()) {
			HRESULT hr = DirectX::Compress(device, mips.GetImages(), mips.GetImageCount(), meta, format,
			                               DirectX::TEX_COMPRESS_DEFAULT, 1.0f, out);
			if (SUCCEEDED(hr)) {
				r_gpu = 1;
				return hr;
			}
			log("textures: GPU compression failed (0x%08lx), using the CPU", (unsigned long)hr);
		}
	}
#endif
	DirectX::TexMetadata m = meta;
	m.format = format;
	HRESULT hr = out.Initialize(m);
	if (FAILED(hr)) {
		return hr;
	}
	/* BC7 on the CPU: the quick mode (mode 6 only) is many times faster and
	 * still good for colour maps */
	DirectX::TEX_COMPRESS_FLAGS cf = DirectX::TEX_COMPRESS_DEFAULT;
	if (format == DXGI_FORMAT_BC7_UNORM) {
		cf = DirectX::TEX_COMPRESS_BC7_QUICK;
	}
	for (size_t i = 0; i < meta.mipLevels; i++) {
		hr = compress_level_cpu(*mips.GetImage(i, 0, 0), format, cf, *out.GetImage(i, 0, 0));
		if (FAILED(hr)) {
			return hr;
		}
	}
	return S_OK;
}

/* ------------------------------------------------------------------------- */

bool encode(const Job &job, const Pixels &px, brb_texture_result &r)
{
	const auto t0 = std::chrono::steady_clock::now();
	memset(&r, 0, sizeof(r));
	if (job.width <= 0 || job.height <= 0 || (!px.rgba8 && !px.rgbaf) || job.path.empty()) {
		set_error(r, "no pixels or no path");
		return false;
	}
	try {
		DirectX::ScratchImage mips;
		DXGI_FORMAT target = DXGI_FORMAT_UNKNOWN;
		HRESULT hr;
		if (job.kind == BRB_TEXTURE_NORMAL) {
			hr = build_normal_mips(job, px, mips);
			target = DXGI_FORMAT_BC5_UNORM;
		}
		else {
			DirectX::ScratchImage base;
			hr = build_base(job, px, base, target);
			if (SUCCEEDED(hr)) {
				if (job.width > 1 || job.height > 1) {
					/* the box filter takes only powers of two */
					const bool pow2 = !(job.width & (job.width - 1)) && !(job.height & (job.height - 1));
					hr = DirectX::GenerateMipMaps(*base.GetImage(0, 0, 0),
					                              (pow2 ? DirectX::TEX_FILTER_BOX : DirectX::TEX_FILTER_LINEAR) |
					                                  DirectX::TEX_FILTER_FORCE_NON_WIC,
					                              0, mips);
				}
				else {
					mips = std::move(base);
				}
				if (SUCCEEDED(hr) && mips.GetMetadata().format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
					mips.OverrideFormat(DXGI_FORMAT_R8G8B8A8_UNORM);
				}
			}
		}
		if (FAILED(hr)) {
			set_error(r, "preparing the image failed (0x%08lx)", (unsigned long)hr);
			return false;
		}
		DirectX::ScratchImage bc;
		hr = compress(mips, target, job.flags, bc, r.gpu);
		if (FAILED(hr)) {
			set_error(r, "compression failed (0x%08lx)", (unsigned long)hr);
			return false;
		}
		/* written next to the target and renamed, so a cache never sees half a file */
		const std::wstring tmp = job.path + L".part";
		hr = DirectX::SaveToDDSFile(bc.GetImages(), bc.GetImageCount(), bc.GetMetadata(),
		                            DirectX::DDS_FLAGS_FORCE_DX10_EXT, tmp.c_str());
		if (FAILED(hr)) {
			DeleteFileW(tmp.c_str());
			set_error(r, "writing %s failed (0x%08lx)", utf8(job.path).c_str(), (unsigned long)hr);
			return false;
		}
		if (!MoveFileExW(tmp.c_str(), job.path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
			DeleteFileW(tmp.c_str());
			set_error(r, "renaming to %s failed (%lu)", utf8(job.path).c_str(), (unsigned long)GetLastError());
			return false;
		}
		r.state = 1;
		r.dxgi_format = (int)target;
		r.mips = (int)bc.GetMetadata().mipLevels;
		r.seconds = seconds_since(t0);
		return true;
	}
	catch (...) {
		set_error(r, "out of memory");
		return false;
	}
}

const char *kind_name(int kind)
{
	switch (kind) {
		case BRB_TEXTURE_COLOR: return "colour";
		case BRB_TEXTURE_NORMAL: return "normal";
		case BRB_TEXTURE_SCALAR: return "scalar";
		case BRB_TEXTURE_HDR: return "hdr";
	}
	return "?";
}

void log_result(const Job &job, const brb_texture_result &r)
{
	if (r.state == 1) {
		log("texture %s (%s %dx%d) -> format %d, %d mips, %s, %.3f s", utf8(job.path).c_str(), kind_name(job.kind),
		    job.width, job.height, r.dxgi_format, r.mips, r.gpu ? "GPU" : "CPU", r.seconds);
	}
	else {
		log("texture %s failed: %s", utf8(job.path).c_str(), r.error);
	}
}

/* ------------------------------------------------------------------------- */
/* The encoder thread. Created on first use and never destroyed (the process
 * may end while it works; it is detached and its state is never freed). */

struct Worker {
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<std::unique_ptr<Job>> queue;
	std::map<int, brb_texture_result> done;
	std::set<int> open;  /* submitted, not yet finished */
	int busy = 0;
	int next_ticket = 1;
	bool started = false;

	void run()
	{
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		for (;;) {
			std::unique_ptr<Job> job;
			{
				std::unique_lock<std::mutex> lock(mutex);
				wake.wait(lock, [this] { return !queue.empty(); });
				job = std::move(queue.front());
				queue.pop_front();
				busy = 1;
			}
			Pixels px;
			px.rgba8 = job->rgba8.empty() ? nullptr : job->rgba8.data();
			px.rgbaf = job->rgbaf.empty() ? nullptr : job->rgbaf.data();
			brb_texture_result r;
			encode(*job, px, r);
			log_result(*job, r);
			{
				std::lock_guard<std::mutex> lock(mutex);
				done[job->ticket] = r;
				open.erase(job->ticket);
				busy = 0;
			}
		}
	}
};

Worker *worker()
{
	static Worker *w = new Worker();
	return w;
}

bool valid(const brb_texture_desc *d)
{
	return d && d->path && d->path[0] && d->width > 0 && d->height > 0 && d->width <= 16384 && d->height <= 16384 &&
	       (d->rgba8 || d->rgbaf) && d->kind >= BRB_TEXTURE_COLOR && d->kind <= BRB_TEXTURE_HDR;
}

}  /* namespace */

}  /* namespace brb */

using namespace brb;

extern "C" {

BRB_API int brb_texture_write(const brb_texture_desc *d, brb_texture_result *r_result)
{
	brb_texture_result r;
	memset(&r, 0, sizeof(r));
	if (!valid(d)) {
		set_error(r, "invalid texture description");
		if (r_result) {
			*r_result = r;
		}
		return -1;
	}
	Job job;
	job.path = d->path;
	job.kind = d->kind;
	job.width = d->width;
	job.height = d->height;
	job.channel = d->channel;
	job.flags = d->flags;
	Pixels px;
	px.rgba8 = d->rgba8;
	px.rgbaf = d->rgba8 ? nullptr : d->rgbaf;
	encode(job, px, r);
	log_result(job, r);
	if (r_result) {
		*r_result = r;
	}
	return r.state == 1 ? 0 : -1;
}

BRB_API int brb_texture_submit(const brb_texture_desc *d)
{
	if (!valid(d)) {
		return 0;
	}
	try {
		std::unique_ptr<Job> job(new Job());
		job->path = d->path;
		job->kind = d->kind;
		job->width = d->width;
		job->height = d->height;
		job->channel = d->channel;
		job->flags = d->flags;
		const size_t n = (size_t)d->width * (size_t)d->height * 4;
		if (d->rgba8) {
			job->rgba8.assign(d->rgba8, d->rgba8 + n);
		}
		else {
			job->rgbaf.assign(d->rgbaf, d->rgbaf + n);
		}
		Worker *w = worker();
		std::lock_guard<std::mutex> lock(w->mutex);
		job->ticket = w->next_ticket++;
		const int ticket = job->ticket;
		w->open.insert(ticket);
		w->queue.push_back(std::move(job));
		if (!w->started) {
			w->started = true;
			std::thread([w] { w->run(); }).detach();
		}
		w->wake.notify_one();
		return ticket;
	}
	catch (...) {
		log("textures: could not queue %s", utf8(d->path).c_str());
		return 0;
	}
}

BRB_API int brb_texture_poll(int ticket, brb_texture_result *r_result)
{
	Worker *w = worker();
	std::lock_guard<std::mutex> lock(w->mutex);
	auto it = w->done.find(ticket);
	if (it != w->done.end()) {
		const brb_texture_result r = it->second;
		w->done.erase(it);
		if (r_result) {
			*r_result = r;
		}
		return r.state;
	}
	if (w->open.count(ticket)) {
		if (r_result) {
			memset(r_result, 0, sizeof(*r_result));
		}
		return 0; /* waiting or being encoded */
	}
	return -2;
}

BRB_API int brb_texture_pending(void)
{
	Worker *w = worker();
	std::lock_guard<std::mutex> lock(w->mutex);
	return (int)w->queue.size() + w->busy;
}

}  /* extern "C" */
