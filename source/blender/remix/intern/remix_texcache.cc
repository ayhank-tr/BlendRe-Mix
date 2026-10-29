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
	return r.kind | ((r.channel & 3) << 4) | (r.flags << 8);
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
	const unsigned char *rgba8 = NULL;
	std::string name;
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
	BLI_HashMurmur2A mm;
	BLI_hash_mm2a_init(&mm, 0x52454d58u /* "REMX" */);
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
	e.path = dir + L"\\" + safe_name(name.c_str()) + L"_" + kind_tag(req) + L"_" + hex + L".dds";
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
	d.flags = req.flags;
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

}  /* namespace brmx */
