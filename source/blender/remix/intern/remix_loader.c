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

/** \file remix_loader.c
 *  \ingroup remix
 *
 * Loads bge_remix_bridge.dll from Blender's (or blenderplayer's) own folder
 * on first use and finds the folder of the Remix runtime. No Python here:
 * blenderplayer uses it too.
 */

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BLI_utildefines.h"

#include "BKE_appdir.h"
#include "BKE_blender_version.h"

#include "BRMX_loader.h"

static struct {
	int tried;
	HMODULE dll;
	char error[512];
	wchar_t path[MAX_PATH];
	BRMX_Bridge fn;
} loader = {0};

static void brmx_atexit(void)
{
	BRMX_exit();
}

static int load_bridge(void)
{
	const char *dir = BKE_appdir_program_dir();
	int n;
	void **slot;

	n = MultiByteToWideChar(CP_UTF8, 0, dir, -1, loader.path, MAX_PATH - 32);
	if (n <= 0) {
		strcpy(loader.error, "could not find the program's own folder");
		return 0;
	}
	wcscat(loader.path, L"\\bge_remix_bridge.dll");

	loader.dll = LoadLibraryExW(loader.path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (loader.dll == NULL) {
		_snprintf(loader.error, sizeof(loader.error),
		          "bge_remix_bridge.dll did not load (Windows error %lu)", GetLastError());
		return 0;
	}

	/* The interface version first: a bridge from another build may not have
	 * the other functions, or have them with other arguments. */
	loader.fn.abi_version = (int (*)(void))GetProcAddress(loader.dll, "brb_abi_version");
	if (loader.fn.abi_version == NULL) {
		strcpy(loader.error, "bge_remix_bridge.dll lacks brb_abi_version");
		goto fail;
	}
	if (loader.fn.abi_version() != BRB_ABI_VERSION) {
		_snprintf(loader.error, sizeof(loader.error),
		          "bge_remix_bridge.dll is from another build (interface %d, expected %d)",
		          loader.fn.abi_version(), BRB_ABI_VERSION);
		goto fail;
	}

#define BRIDGE_FN(member, name) \
	slot = (void **)&loader.fn.member; \
	*slot = (void *)GetProcAddress(loader.dll, name); \
	if (*slot == NULL) { \
		_snprintf(loader.error, sizeof(loader.error), "bge_remix_bridge.dll lacks %s", name); \
		goto fail; \
	} (void)0

	BRIDGE_FN(api_version, "brb_api_version");
	BRIDGE_FN(load, "brb_load");
	BRIDGE_FN(get_status, "brb_get_status");
	BRIDGE_FN(status_text, "brb_status_text");
	BRIDGE_FN(log_path, "brb_log_path");
	BRIDGE_FN(startup, "brb_startup");
	BRIDGE_FN(set_config, "brb_set_config");
	BRIDGE_FN(triangle_test, "brb_triangle_test");
	BRIDGE_FN(shutdown, "brb_shutdown");
	BRIDGE_FN(startup_ex, "brb_startup_ex");
	BRIDGE_FN(get_mode, "brb_get_mode");
	BRIDGE_FN(resize, "brb_resize");
	BRIDGE_FN(show, "brb_show");
	BRIDGE_FN(material_create, "brb_material_create");
	BRIDGE_FN(material_destroy, "brb_material_destroy");
	BRIDGE_FN(mesh_create, "brb_mesh_create");
	BRIDGE_FN(mesh_destroy, "brb_mesh_destroy");
	BRIDGE_FN(light_create, "brb_light_create");
	BRIDGE_FN(light_destroy, "brb_light_destroy");
	BRIDGE_FN(scene_clear, "brb_scene_clear");
	BRIDGE_FN(frame_begin, "brb_frame_begin");
	BRIDGE_FN(frame_camera, "brb_frame_camera");
	BRIDGE_FN(frame_draw, "brb_frame_draw");
	BRIDGE_FN(frame_light, "brb_frame_light");
	BRIDGE_FN(frame_end, "brb_frame_end");
	BRIDGE_FN(frame_pixels, "brb_frame_pixels");
	BRIDGE_FN(save_bmp, "brb_save_bmp");
	BRIDGE_FN(get_stats, "brb_get_stats");
	BRIDGE_FN(reset_stats, "brb_reset_stats");
	BRIDGE_FN(texture_write, "brb_texture_write");
	BRIDGE_FN(texture_submit, "brb_texture_submit");
	BRIDGE_FN(texture_poll, "brb_texture_poll");
	BRIDGE_FN(texture_pending, "brb_texture_pending");
#undef BRIDGE_FN

	/* Remix is shut down before the process ends, however it ends. */
	atexit(brmx_atexit);
	return 1;

fail:
	FreeLibrary(loader.dll);
	loader.dll = NULL;
	memset(&loader.fn, 0, sizeof(loader.fn));
	return 0;
}

const BRMX_Bridge *BRMX_bridge(void)
{
	if (!loader.tried) {
		loader.tried = 1;
		load_bridge();
	}
	return loader.dll ? &loader.fn : NULL;
}

const char *BRMX_bridge_error(void)
{
	BRMX_bridge();
	return loader.dll ? "" : loader.error;
}

const wchar_t *BRMX_bridge_path(void)
{
	BRMX_bridge();
	return loader.path;
}

void BRMX_exit(void)
{
	if (loader.dll && loader.fn.shutdown) {
		loader.fn.shutdown();
	}
}

/* ------------------------------------------------------------------------- */
/* Runtime folder */

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* The string value of "key" in a JSON object written by Python's json.dump
 * (UTF-8, non-ASCII as \uXXXX), as UTF-16. Enough for settings.json. */
static int json_string_value(const char *json, const char *key, wchar_t *out, int maxlen)
{
	char pattern[64];
	const char *p;
	int n = 0;

	_snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p) {
		return 0;
	}
	p += strlen(pattern);
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	if (*p++ != ':') return 0;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	if (*p++ != '"') return 0;

	while (*p && *p != '"') {
		unsigned int c;
		if (n >= maxlen - 1) {
			return 0;
		}
		if (*p == '\\') {
			p++;
			switch (*p) {
				case 'n': c = '\n'; p++; break;
				case 't': c = '\t'; p++; break;
				case 'r': c = '\r'; p++; break;
				case 'b': c = '\b'; p++; break;
				case 'f': c = '\f'; p++; break;
				case 'u': {
					int i, d;
					c = 0;
					for (i = 1; i <= 4; i++) {
						d = hex_digit(p[i]);
						if (d < 0) return 0;
						c = c * 16 + (unsigned int)d;
					}
					p += 5;
					break;
				}
				case '\0': return 0;
				default: c = (unsigned char)*p; p++; break; /* \" \\ \/ */
			}
			out[n++] = (wchar_t)c; /* UTF-16 code units, surrogates included */
		}
		else {
			/* raw UTF-8 (json.dump with ensure_ascii=False) */
			const char *start = p;
			int len = 1, w;
			unsigned char b = (unsigned char)*p;
			if (b >= 0xF0) len = 4;
			else if (b >= 0xE0) len = 3;
			else if (b >= 0xC0) len = 2;
			for (w = 1; w < len; w++) {
				if (!p[w]) return 0;
			}
			p += len;
			w = MultiByteToWideChar(CP_UTF8, 0, start, len, out + n, maxlen - 1 - n);
			if (w <= 0) return 0;
			n += w;
		}
	}
	if (*p != '"') {
		return 0;
	}
	out[n] = 0;
	return n > 0;
}

static int read_settings_dir(const wchar_t *base, wchar_t *r_dir, int maxlen)
{
	wchar_t path[MAX_PATH + 64];
	FILE *f;
	char buf[8192];
	size_t len;

	_snwprintf(path, MAX_PATH + 63, L"%ls\\settings.json", base);
	path[MAX_PATH + 63] = 0;
	f = _wfopen(path, L"rb");
	if (!f) {
		return 0;
	}
	len = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[len] = 0;
	return json_string_value(buf, "runtime_dir", r_dir, maxlen);
}

int BRMX_runtime_dir(wchar_t *r_dir, int maxlen)
{
	const char *user;
	wchar_t base[MAX_PATH + 1];
	DWORD len;
	int n;

	/* Through the Win32 API: the C runtime's copy of the environment does not
	 * see changes made from Python. */
	len = GetEnvironmentVariableW(L"BGE_REMIX_RUNTIME_DIR", r_dir, (DWORD)maxlen);
	if (len > 0 && len < (DWORD)maxlen) {
		return 1;
	}

	/* bpy.utils.resource_path('USER') */
	user = BKE_appdir_folder_id_version(BLENDER_RESOURCE_PATH_USER, BLENDER_VERSION, false);
	if (!user || !user[0]) {
		return 0;
	}
	n = MultiByteToWideChar(CP_UTF8, 0, user, -1, base, MAX_PATH - 16);
	if (n <= 0) {
		return 0;
	}
	wcscat(base, L"\\remix");

	if (read_settings_dir(base, r_dir, maxlen)) {
		return 1;
	}
	if (_snwprintf(r_dir, maxlen, L"%ls\\%hs", base, BRMX_RUNTIME_VERSION) < 0) {
		return 0;
	}
	r_dir[maxlen - 1] = 0;
	return 1;
}

int BRMX_ensure_loaded(void)
{
	const BRMX_Bridge *b = BRMX_bridge();
	brb_status s;
	wchar_t dir[MAX_PATH * 2];

	if (!b) {
		return -1;
	}
	s = b->get_status();
	if (s == BRB_STATUS_READY || s == BRB_STATUS_RUNNING) {
		return (int)s;
	}
	if (!BRMX_runtime_dir(dir, (int)(sizeof(dir) / sizeof(dir[0])))) {
		return (int)s;
	}
	return (int)b->load(dir);
}
