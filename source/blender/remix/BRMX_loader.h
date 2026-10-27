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

/** \file BRMX_loader.h
 *  \ingroup remix
 *
 * BGE-Remix: the shared loader of bge_remix_bridge.dll, linked into
 * blender.exe and blenderplayer.exe. The DLL is loaded the first time
 * BRMX_bridge() is called, never at startup.
 */

#ifndef __BRMX_LOADER_H__
#define __BRMX_LOADER_H__

#include <wchar.h>

#include "bge_remix_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The runtime version BGE-Remix installs and expects (as in
 * release/scripts/modules/bge_remix/pin.py; a test checks they match). */
#define BRMX_RUNTIME_VERSION "1.6.1"

/* The bridge's functions. */
typedef struct BRMX_Bridge {
	int (*abi_version)(void);
	const char *(*api_version)(void);
	brb_status (*load)(const wchar_t *runtime_dir);
	brb_status (*get_status)(void);
	const char *(*status_text)(void);
	const char *(*log_path)(void);
	brb_status (*startup)(int width, int height, int visible);
	int (*set_config)(const char *key, const char *value);
	int (*triangle_test)(double seconds, const wchar_t *screenshot_path, int capture, brb_triangle_result *r);
	void (*shutdown)(void);

	brb_status (*startup_ex)(int mode, int width, int height, void *parent_hwnd, int x, int y);
	int (*get_mode)(void);
	int (*resize)(int width, int height, int x, int y);
	void (*show)(int visible);
	int (*material_create)(const brb_material_desc *desc);
	void (*material_destroy)(uint64_t hash);
	int (*mesh_create)(uint64_t hash, const brb_surface_desc *surfaces, uint32_t count);
	void (*mesh_destroy)(uint64_t hash);
	int (*light_create)(const brb_light_desc *desc);
	void (*light_destroy)(uint64_t hash);
	void (*scene_clear)(void);
	int (*frame_begin)(void);
	int (*frame_camera)(const float view[16], const float proj[16]);
	int (*frame_draw)(uint64_t mesh, const float model[16], int flags);
	int (*frame_light)(uint64_t light);
	int (*frame_end)(void);
	const void *(*frame_pixels)(int *r_width, int *r_height, int *r_pitch);
	int (*save_bmp)(const wchar_t *path);
	void (*get_stats)(brb_frame_stats *r_stats);
	void (*reset_stats)(void);

	int (*texture_write)(const brb_texture_desc *desc, brb_texture_result *r_result);
	int (*texture_submit)(const brb_texture_desc *desc);
	int (*texture_poll)(int ticket, brb_texture_result *r_result);
	int (*texture_pending)(void);

	int (*frame_draw_ex)(const brb_draw_desc *desc);
	int (*pick_request)(int left, int top, int right, int bottom);
	int (*pick_result)(uint32_t *r_ids, int max, int *r_count);
	int (*highlight)(const uint32_t *ids, int count, float r, float g, float b);
} BRMX_Bridge;

/* The bridge, loaded on first use; NULL when it is missing or from another
 * build (BRMX_bridge_error says why). */
const BRMX_Bridge *BRMX_bridge(void);
const char *BRMX_bridge_error(void);
const wchar_t *BRMX_bridge_path(void);

/* The folder the Remix runtime is used from, in this order:
 *   1. the environment variable BGE_REMIX_RUNTIME_DIR
 *   2. "runtime_dir" in <Blender user folder>/remix/settings.json
 *      (Preferences > System > RTX Remix)
 *   3. <Blender user folder>/remix/<BRMX_RUNTIME_VERSION>
 * Same rules as bge_remix.runtime_dir() in Python. Returns 0 if it does not fit. */
int BRMX_runtime_dir(wchar_t *r_dir, int maxlen);

/* Load the runtime from BRMX_runtime_dir() unless it is loaded already.
 * Returns the bridge's status, or -1 without a bridge. */
int BRMX_ensure_loaded(void);

/* Shut Remix down if the bridge is loaded (also registered with atexit). */
void BRMX_exit(void);

#ifdef __cplusplus
}
#endif

#endif  /* __BRMX_LOADER_H__ */
