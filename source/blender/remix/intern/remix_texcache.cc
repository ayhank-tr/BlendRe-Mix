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

/** \file remix_texcache.cc
 *  \ingroup remix
 *
 * The texture cache (see BRMX_material.h). A cached file is named after the
 * image, what it is used as and a hash of its pixels:
 *
 *   remix_cache/<image name>_<kind>_<hash>.dds
 *
 * so a changed image gets a new file and an unchanged one is reused, also
 * by the next run and by blenderplayer. Encoding happens on the bridge's
 * encoder thread; the game asks again each frame until the file is there.
 */

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "BRMX_bake.h"
#include "BRMX_material.h"

extern "C" {
#include "DNA_ID.h"
#include "DNA_image_types.h"

#include "BKE_appdir.h"
#include "BKE_global.h"
#include "BKE_image.h"
#include "BKE_main.h"

#include "BLI_hash_mm2a.h"

#include "IMB_imbuf_types.h"

#include "GPU_draw.h"
}

namespace brmx {

namespace {

std::wstring widen(const char *utf8)
{
	if (!utf8 || !utf8[0]) {
		return std::wstring();
	}
	const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
	if (n <= 0) {
		return std::wstring();
	}
	std::wstring w((size_t)n - 1, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &w[0], n);
	return w;
}

bool exists(const std::wstring &path)
{
	const DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void make_dirs(const std::wstring &dir)
{
	/* each level in turn; existing ones are fine */
	for (size_t i = 3; i <= dir.size(); i++) {
		if (i == dir.size() || dir[i] == L'\\' || dir[i] == L'/') {
			CreateDirectoryW(dir.substr(0, i).c_str(), NULL);
		}
	}
}

/* a file-name-safe version of an image name */
std::wstring safe_name(const char *name)
{
	std::wstring out;
	for (const char *c = name; *c && out.size() < 40; c++) {
		const char ch = *c;
		const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
		                ch == '-' || ch == '_';
		out += ok ? (wchar_t)ch : L'_';
	}
	return out.empty() ? std::wstring(L"image") : out;
}

const wchar_t *kind_tag(const TextureRequest &r)
{
	switch (r.kind) {
		case BRB_TEXTURE_COLOR: return (r.flags & BRB_TEXTURE_LINEAR) ? L"lin" : L"col";
		case BRB_TEXTURE_NORMAL: return L"nrm";
		case BRB_TEXTURE_SCALAR: {
			static const wchar_t *tags[2][4] = {{L"r", L"g", L"b", L"a"}, {L"ri", L"gi", L"bi", L"ai"}};
			return tags[(r.flags & BRB_TEXTURE_INVERT) ? 1 : 0][r.channel & 3];
		}
		case BRB_TEXTURE_HDR: return L"hdr";
	}
	return L"x";
}

int key_bits(const TextureRequest &r)
{
	return r.kind | ((r.channel & 3) << 4) | (r.flags << 8) | (r.max_size << 16);
}

/* P8.24: halved (2x2 means) until neither side is above max_size */
template<typename T> void shrink(std::vector<T> &px, int &w, int &h, int max_size, float scale)
{
	while (max_size > 0 && (w > max_size || h > max_size) && (w > 1 || h > 1)) {
		const int nw = std::max(w / 2, 1), nh = std::max(h / 2, 1);
		std::vector<T> out((size_t)nw * nh * 4);
		for (int y = 0; y < nh; y++) {
			for (int x = 0; x < nw; x++) {
				for (int c = 0; c < 4; c++) {
					float sum = 0.0f;
					int n = 0;
					for (int dy = 0; dy < 2; dy++) {
						for (int dx = 0; dx < 2; dx++) {
							const int sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
							sum += (float)px[((size_t)sy * w + sx) * 4 + c];
							n++;
						}
					}
					out[((size_t)y * nw + x) * 4 + c] = (T)(sum / n + scale);
				}
			}
		}
		px.swap(out);
		w = nw;
		h = nh;
	}
}

}  /* namespace */

TextureCache::TextureCache() : encoded(0), reused(0), failed(0), encode_seconds(0.0), m_waiting(0)
{
}

std::wstring TextureCache::folder()
{
	wchar_t env[MAX_PATH * 2];
	const DWORD n = GetEnvironmentVariableW(L"BGE_REMIX_CACHE_DIR", env, MAX_PATH * 2);
	if (n > 0 && n < MAX_PATH * 2) {
		return std::wstring(env);
	}
	if (G.main && G.main->name[0]) {
		std::wstring blend = widen(G.main->name);
		const size_t slash = blend.find_last_of(L"\\/");
		if (slash != std::wstring::npos) {
			return blend.substr(0, slash) + L"\\remix_cache";
		}
	}
	std::wstring tmp = widen(BKE_tempdir_base());
	while (!tmp.empty() && (tmp.back() == L'\\' || tmp.back() == L'/')) {
		tmp.pop_back();
	}
	return tmp + L"\\bge-remix\\remix_cache";
}

std::wstring TextureCache::baked_path(const TextureRequest &req)
{
	if (!req.baked) {
		return std::wstring();
	}
	wchar_t hex[24];
	swprintf(hex, 24, L"%016llx", (unsigned long long)req.baked->key);
	/* P8.24: "kb": rows from the bottom (interface 10) */
	return folder() + L"\\" + safe_name(req.baked->name.c_str()) + L"_" + kind_tag(req) + L"_kb" + hex + L".dds";
}

bool TextureCache::cached(const TextureRequest &req)
{
	return req.baked && req.baked->key && exists(baked_path(req));
}

TextureCache::State TextureCache::request(const TextureRequest &req, std::wstring &r_path)
{
	if (!req.wanted()) {
		return MISSING;
	}
	const Key key(req.baked ? (const void *)req.baked.get() : (const void *)req.image, key_bits(req));
	std::map<Key, Entry>::iterator it = m_entries.find(key);
	if (it != m_entries.end()) {
		r_path = it->second.path;
		return it->second.state;
	}
	Entry &e = m_entries[key];
	const BRMX_Bridge *B = BRMX_bridge();
	if (!B) {
		e.state = FAILED;
		failed++;
		return FAILED;
	}

	void *lock = NULL;
	ImBuf *ibuf = NULL;
	int w, h;
	std::vector<float> rgbaf;
	std::vector<unsigned char> rgba8_small; /* P8.24: made smaller (max_size) */
	const unsigned char *rgba8 = NULL;
	std::string name;
	if (req.baked && req.baked->key) {
		/* P8.7: a bake named by its content: found, or encoded under that name */
		e.path = baked_path(req);
		r_path = e.path;
		if (exists(e.path)) {
			e.state = READY;
			reused++;
			return READY;
		}
		if (req.baked->cached) {
			/* the file went away after the bake was skipped */
			e.state = FAILED;
			failed++;
			return FAILED;
		}
	}
	if (req.baked) {
		/* P8.6: a baked procedural texture */
		w = req.baked->width;
		h = req.baked->height;
		if (w <= 0 || h <= 0 || req.baked->rgba.size() < (size_t)w * h * 4) {
			e.state = FAILED;
			failed++;
			return FAILED;
		}
		rgba8 = req.baked->rgba.data();
		name = req.baked->name;
	}
	else {
		ibuf = BKE_image_acquire_ibuf(req.image, NULL, &lock);
		if (!ibuf || (!ibuf->rect && !ibuf->rect_float) || ibuf->x <= 0 || ibuf->y <= 0) {
			BKE_image_release_ibuf(req.image, ibuf, lock);
			printf("BGE-Remix: image \"%s\" has no pixels (missing file?); left out\n", req.image->id.name + 2);
			e.state = FAILED;
			failed++;
			return FAILED;
		}
		w = ibuf->x;
		h = ibuf->y;
		name = req.image->id.name + 2;
		if (ibuf->rect) {
			rgba8 = (const unsigned char *)ibuf->rect;
		}
		else {
			/* float images may have 1, 3 or 4 channels */
			const int ch = ibuf->channels;
			rgbaf.resize((size_t)w * h * 4);
			for (size_t i = 0; i < (size_t)w * h; i++) {
				const float *src = ibuf->rect_float + i * ch;
				float *dst = &rgbaf[i * 4];
				dst[0] = src[0];
				dst[1] = ch >= 3 ? src[1] : src[0];
				dst[2] = ch >= 3 ? src[2] : src[0];
				dst[3] = ch == 4 ? src[3] : 1.0f;
			}
		}
	}
	/* P8.24: the slot's largest size */
	if (req.max_size > 0 && (w > req.max_size || h > req.max_size)) {
		if (rgba8) {
			rgba8_small.assign(rgba8, rgba8 + (size_t)w * h * 4);
			shrink(rgba8_small, w, h, req.max_size, 0.5f);
			rgba8 = rgba8_small.data();
		}
		else {
			shrink(rgbaf, w, h, req.max_size, 0.0f);
		}
	}
	/* P9.2: a DirectX normal map: its green flipped (Remix reads OpenGL's) */
	if (req.flags & BRMX_TEXTURE_FLIP_GREEN) {
		if (rgba8) {
			if (rgba8 != rgba8_small.data()) {
				rgba8_small.assign(rgba8, rgba8 + (size_t)w * h * 4);
			}
			for (size_t i = 0; i < (size_t)w * h; i++) {
				rgba8_small[i * 4 + 1] = (unsigned char)(255 - rgba8_small[i * 4 + 1]);
			}
			rgba8 = rgba8_small.data();
		}
		else {
			for (size_t i = 0; i < (size_t)w * h; i++) {
				rgbaf[i * 4 + 1] = 1.0f - rgbaf[i * 4 + 1];
			}
		}
	}
	BLI_HashMurmur2A mm;
	/* P8.24: "RMX2": rows from the bottom (interface 10), so no file from
	 * before (rows from the top) is taken for one */
	BLI_hash_mm2a_init(&mm, 0x524d5832u /* "RMX2" */);
	BLI_hash_mm2a_add_int(&mm, w);
	BLI_hash_mm2a_add_int(&mm, h);
	if (rgba8) {
		BLI_hash_mm2a_add(&mm, rgba8, (size_t)w * h * 4);
	}
	else {
		BLI_hash_mm2a_add(&mm, (const unsigned char *)rgbaf.data(), rgbaf.size() * sizeof(float));
	}
	const unsigned int hash = BLI_hash_mm2a_end(&mm);

	const std::wstring dir = folder();
	wchar_t hex[16];
	swprintf(hex, 16, L"%08x", hash);
	if (!(req.baked && req.baked->key)) {
		e.path = dir + L"\\" + safe_name(name.c_str()) + L"_" + kind_tag(req) + L"_" + hex + L".dds";
		if (req.max_size > 0) {
			wchar_t size[16];
			swprintf(size, 16, L"_m%d", req.max_size);
			e.path = dir + L"\\" + safe_name(name.c_str()) + L"_" + kind_tag(req) + size + L"_" + hex + L".dds";
		}
	}
	r_path = e.path;
	if (exists(e.path)) {
		if (ibuf) {
			BKE_image_release_ibuf(req.image, ibuf, lock);
		}
		e.state = READY;
		reused++;
		return READY;
	}
	make_dirs(dir);
	brb_texture_desc d;
	memset(&d, 0, sizeof(d));
	d.path = e.path.c_str();
	d.kind = req.kind;
	d.width = w;
	d.height = h;
	d.rgba8 = rgba8;
	d.rgbaf = rgba8 ? NULL : rgbaf.data();
	d.channel = req.channel;
	d.flags = req.flags & ~BRMX_TEXTURE_FLIP_GREEN;  /* (the cache's own) */
	wchar_t no_gpu[8];
	if (GetEnvironmentVariableW(L"BGE_REMIX_TEXTURE_NO_GPU", no_gpu, 8) > 0) {
		d.flags |= BRB_TEXTURE_NO_GPU;
	}
	/* the bridge copies the pixels */
	e.ticket = B->texture_submit(&d);
	if (ibuf) {
		BKE_image_release_ibuf(req.image, ibuf, lock);
	}
	if (e.ticket <= 0) {
		e.state = FAILED;
		failed++;
		return FAILED;
	}
	e.state = WAITING;
	m_waiting++;
	return WAITING;
}

void TextureCache::poll()
{
	if (m_waiting == 0) {
		return;
	}
	const BRMX_Bridge *B = BRMX_bridge();
	if (!B) {
		return;
	}
	for (std::map<Key, Entry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it) {
		Entry &e = it->second;
		if (e.state != WAITING) {
			continue;
		}
		brb_texture_result r;
		const int s = B->texture_poll(e.ticket, &r);
		if (s == 0) {
			continue;
		}
		m_waiting--;
		if (s == 1) {
			e.state = READY;
			encoded++;
			encode_seconds += r.seconds;
		}
		else {
			e.state = FAILED;
			failed++;
			printf("BGE-Remix: texture %ls: %s\n", e.path.c_str(), r.error);
		}
	}
}

int TextureCache::waiting() const
{
	return m_waiting;
}

/* ------------------------------------------------------------------------- */
/* P8.24: the game's OpenGL reads the cache's BC7 files */

namespace {

unsigned pixel_hash(int w, int h, const unsigned char *rgba8, const float *rgbaf, size_t nfloat)
{
	BLI_HashMurmur2A mm;
	/* as TextureCache::request */
	BLI_hash_mm2a_init(&mm, 0x524d5832u /* "RMX2" */);
	BLI_hash_mm2a_add_int(&mm, w);
	BLI_hash_mm2a_add_int(&mm, h);
	if (rgba8) {
		BLI_hash_mm2a_add(&mm, rgba8, (size_t)w * h * 4);
	}
	else {
		BLI_hash_mm2a_add(&mm, (const unsigned char *)rgbaf, nfloat * sizeof(float));
	}
	return BLI_hash_mm2a_end(&mm);
}

bool gl_cache_load(Image *ima, ImBuf *ibuf, GPUCompressedImage *r)
{
	if (!ima || !ibuf || !ibuf->rect || ibuf->x <= 0 || ibuf->y <= 0) {
		return false;
	}
	const int w = ibuf->x, h = ibuf->y;
	wchar_t hex[16];
	swprintf(hex, 16, L"%08x", pixel_hash(w, h, (const unsigned char *)ibuf->rect, NULL, 0));
	const std::wstring path = TextureCache::folder() + L"\\" + safe_name(ima->id.name + 2) + L"_col_" + hex + L".dds";
	FILE *f = _wfopen(path.c_str(), L"rb");
	if (!f) {
		return false;
	}
	std::vector<unsigned char> *bytes = new std::vector<unsigned char>();
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n > 148) {
		bytes->resize((size_t)n);
		if (fread(bytes->data(), 1, (size_t)n, f) != (size_t)n) {
			bytes->clear();
		}
	}
	fclose(f);
	const unsigned char *b = bytes->data();
	auto u32 = [&](size_t at) {
		return (uint32_t)b[at] | ((uint32_t)b[at + 1] << 8) | ((uint32_t)b[at + 2] << 16) | ((uint32_t)b[at + 3] << 24);
	};
	/* "DDS ", a DX10 header, BC7_UNORM (98), the image's size */
	if (bytes->size() <= 148 || memcmp(b, "DDS ", 4) != 0 || memcmp(b + 84, "DX10", 4) != 0 || u32(128) != 98 ||
	    (int)u32(12) != h || (int)u32(16) != w)
	{
		delete bytes;
		return false;
	}
	const int mips = std::max<int>((int)u32(28), 1);
	size_t at = 148;
	int levels = 0;
	for (int lw = w, lh = h; levels < mips && levels < 16; levels++) {
		const size_t size = (size_t)((lw + 3) / 4) * (size_t)((lh + 3) / 4) * 16;
		if (at + size > bytes->size()) {
			break;
		}
		r->data[levels] = b + at;
		r->size[levels] = (int)size;
		at += size;
		lw = std::max(lw / 2, 1);
		lh = std::max(lh / 2, 1);
	}
	if (levels == 0) {
		delete bytes;
		return false;
	}
	r->format = 0x8E8C; /* GL_COMPRESSED_RGBA_BPTC_UNORM: sRGB values, as the game's 8-bit textures */
	r->width = w;
	r->height = h;
	r->levels = levels;
	r->owner = bytes;
	return true;
}

void gl_cache_free(GPUCompressedImage *img)
{
	delete (std::vector<unsigned char> *)img->owner;
	img->owner = NULL;
}

}  /* namespace */

void install_gl_image_cache(bool on)
{
	if (on) {
		GPU_set_image_cache(gl_cache_load, gl_cache_free);
	}
	else {
		GPU_set_image_cache(NULL, NULL);
	}
}

}  /* namespace brmx */
