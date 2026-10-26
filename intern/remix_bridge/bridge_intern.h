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

std::string utf8(const std::wstring &w);
const std::string &log_path();
void log(const char *fmt, ...);
void set_status(brb_status status, const std::string &text);
bool file_exists(const std::wstring &path);

/* bridge_load.cpp */
std::string error_text(remixapi_ErrorCode code);
std::string with_code(const std::string &what, remixapi_ErrorCode code);

/* bridge_startup.cpp */
void pump_messages();
double now_ms();

}  /* namespace brb */

#endif  /* __BRIDGE_INTERN_H__ */
