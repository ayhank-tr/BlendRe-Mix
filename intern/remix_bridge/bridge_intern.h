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

/** \file bridge_intern.h
 *  \ingroup remix_bridge
 *
 * State and helpers shared by the bridge's source files.
 */

#ifndef __BRIDGE_INTERN_H__
#define __BRIDGE_INTERN_H__

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>

#include <string>

#include <remix/remix_c.h>

#define BGE_REMIX_BRIDGE_EXPORTS
#include "bge_remix_bridge.h"

namespace brb {

/* bridge.cpp */
extern brb_status g_status;
extern std::string g_text;
extern remixapi_Interface g_remix;
extern remixapi_HMODULE g_dll;
extern bool g_started;
extern HWND g_hwnd;
extern std::wstring g_runtime_dir;
/* Remix's implementation DLL (d3d9_x64.dll), preloaded by brb_load */
extern HMODULE g_impl;

/* Undo what brb_load changed in the process: the DLL search folder and the
 * preloaded implementation DLL. */
void release_runtime_paths();

std::string utf8(const std::wstring &w);
const std::string &log_path();
void log(const char *fmt, ...);
/* rtx.* settings every scene needs (bridge_startup.cpp) */
void set_scene_defaults();
void set_status(brb_status status, const std::string &text);
bool file_exists(const std::wstring &path);

/* bridge_load.cpp */
std::string error_text(remixapi_ErrorCode code);
std::string with_code(const std::string &what, remixapi_ErrorCode code);

/* bridge_startup.cpp */
LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
HWND create_window(int width, int height, bool visible);
void destroy_window();
void pump_messages();
double now_ms();
/* Call a runtime function, turning a C++ exception from it into an error code. */
template<typename F> remixapi_ErrorCode guarded(const char *what, F &&fn)
{
	try {
		return fn();
	}
	catch (...) {
		log("the runtime raised an exception in %s", what);
		return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
	}
}

/* bridge_triangle.cpp */
remixapi_MaterialInfoOpaqueEXT default_opaque();
remixapi_MaterialInfo default_material(uint64_t hash, void *ext);
remixapi_LightInfoSphereEXT default_sphere();
bool save_window_bmp(HWND hwnd, const wchar_t *path);
bool save_bmp(const wchar_t *path, const unsigned char *bgra, int width, int height, int pitch);

/* bridge_device.cpp: the game modes (copy, child window) */
extern brb_mode g_mode;
/* Release the device-side objects of the game modes; called before Remix shuts down. */
void device_release();
void device_frame_unlock();
/* Present, and in copy mode copy the picture out. */
remixapi_ErrorCode device_present();

/* bridge_scene.cpp */
extern brb_frame_stats g_stats;
/* Forget all objects without calling the runtime (it is going away). */
void scene_forget();

}  /* namespace brb */

#endif  /* __BRIDGE_INTERN_H__ */
