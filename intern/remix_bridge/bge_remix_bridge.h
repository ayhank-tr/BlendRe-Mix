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

/** \file bge_remix_bridge.h
 *  \ingroup remix_bridge
 *
 * BGE-Remix: the plain C interface of bge_remix_bridge.dll.
 *
 * The bridge is the only code that talks to the RTX Remix runtime. Blender
 * loads the DLL on first use (LoadLibrary + GetProcAddress of the functions
 * below), so nothing here is linked into blender.exe.
 *
 * Strings are UTF-8, paths are UTF-16 (wchar_t). All calls must come from the
 * thread that created the Remix window (Blender's main thread).
 */

#ifndef __BGE_REMIX_BRIDGE_H__
#define __BGE_REMIX_BRIDGE_H__

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BGE_REMIX_BRIDGE_EXPORTS
#  define BRB_API __declspec(dllexport)
#else
#  define BRB_API __declspec(dllimport)
#endif

/* Bumped whenever a function below changes; Blender refuses a bridge DLL
 * with another value instead of calling into it. */
#define BRB_ABI_VERSION 1

typedef enum brb_status {
	BRB_STATUS_NOT_LOADED = 0,           /* nothing tried yet */
	BRB_STATUS_NOT_INSTALLED = 1,        /* no runtime in the given folder */
	BRB_STATUS_INCOMPATIBLE_VERSION = 2, /* runtime built for another API minor version */
	BRB_STATUS_LOAD_FAILED = 3,          /* the runtime DLL or one it needs did not load */
	BRB_STATUS_STARTUP_FAILED = 4,       /* loaded, but Remix refused to start (GPU, driver, Vulkan) */
	BRB_STATUS_READY = 5,                /* loaded, not started */
	BRB_STATUS_RUNNING = 6,              /* started, rendering into its window */
} brb_status;

typedef struct brb_triangle_result {
	int frames;               /* frames presented */
	double seconds;           /* wall time of the loop */
	double frame_ms_mean;
	double frame_ms_median;
	double frame_ms_p95;
	int present_errors;       /* Present calls that did not return success */
	int screenshot_saved;     /* 1 when the screenshot file was written */
	int capture_requested;    /* 1 when the capture hotkey was sent */
	int last_error;           /* last remixapi_ErrorCode seen, 0 if none */
} brb_triangle_result;

/* Always BRB_ABI_VERSION of the DLL. */
BRB_API int brb_abi_version(void);

/* Remix API version the bridge was compiled against, e.g. "0.6.5". */
BRB_API const char *brb_api_version(void);

/* Load the runtime from a folder containing Remix's d3d9.dll.
 * Returns READY or the reason it is not usable. Safe to call again. */
BRB_API brb_status brb_load(const wchar_t *runtime_dir);

BRB_API brb_status brb_get_status(void);

/* Readable explanation of the current status (English, UTF-8). */
BRB_API const char *brb_status_text(void);

/* Path of the bridge's log file (UTF-8). */
BRB_API const char *brb_log_path(void);

/* Create the bridge's window and start Remix in it, then apply the BGE-Remix
 * configuration (Z up, right-handed, metres). Needs READY. */
BRB_API brb_status brb_startup(int width, int height, int visible);

/* Set one Remix option (rtx.conf key). Returns the remixapi_ErrorCode. */
BRB_API int brb_set_config(const char *key, const char *value);

/* Draw the P1 test scene (a lit triangle over a floor) for `seconds`.
 * screenshot_path (BMP) may be NULL. capture != 0 presses Remix's capture
 * hotkey half-way through. Needs RUNNING. Returns 0 on success. */
BRB_API int brb_triangle_test(double seconds, const wchar_t *screenshot_path, int capture,
                              brb_triangle_result *r_result);

/* Shut Remix down and unload it. Safe to call at any time, also twice. */
BRB_API void brb_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif  /* __BGE_REMIX_BRIDGE_H__ */
