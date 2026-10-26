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

/** \file bridge_load.cpp
 *  \ingroup remix_bridge
 *
 * Loading the RTX Remix runtime, and readable Remix error messages.
 */

#include <cstdio>

#include "bridge_intern.h"

namespace brb {

std::string error_text(remixapi_ErrorCode code)
{
	switch (code) {
		case REMIXAPI_ERROR_CODE_SUCCESS:
			return "success";
		case REMIXAPI_ERROR_CODE_GENERAL_FAILURE:
			return "general failure";
		case REMIXAPI_ERROR_CODE_LOAD_LIBRARY_FAILURE:
			return "Windows could not load the runtime DLL or a DLL it needs";
		case REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS:
			return "invalid arguments";
		case REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE:
			return "the DLL is not an RTX Remix runtime (no remixapi_InitializeLibrary)";
		case REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION:
			return "the runtime was built for another Remix API version";
		case REMIXAPI_ERROR_CODE_NOT_INITIALIZED:
			return "the runtime is not initialized";
		case REMIXAPI_ERROR_CODE_HRESULT_NO_REQUIRED_GPU_FEATURES:
			return "the graphics card lacks features Remix needs (an NVIDIA RTX card is required)";
		case REMIXAPI_ERROR_CODE_HRESULT_DRIVER_VERSION_BELOW_MINIMUM:
			return "the graphics driver is too old for this Remix runtime; please update the NVIDIA driver";
		case REMIXAPI_ERROR_CODE_HRESULT_DXVK_INSTANCE_EXTENSION_FAIL:
			return "a Vulkan instance extension Remix needs is missing";
		case REMIXAPI_ERROR_CODE_HRESULT_VK_CREATE_INSTANCE_FAIL:
			return "Vulkan could not be started";
		case REMIXAPI_ERROR_CODE_HRESULT_VK_CREATE_DEVICE_FAIL:
			return "Vulkan could not open the graphics card";
		case REMIXAPI_ERROR_CODE_HRESULT_GRAPHICS_QUEUE_FAMILY_MISSING:
			return "the graphics card has no suitable Vulkan graphics queue";
		default: {
			char buf[64];
			snprintf(buf, sizeof(buf), "remixapi error 0x%08x", (unsigned)code);
			return buf;
		}
	}
}

std::string with_code(const std::string &what, remixapi_ErrorCode code)
{
	char buf[32];
	if ((unsigned)code > 0xFFFF) {
		snprintf(buf, sizeof(buf), " (code 0x%08X)", (unsigned)code);
	}
	else {
		snprintf(buf, sizeof(buf), " (code %u)", (unsigned)code);
	}
	return what + ": " + error_text(code) + buf + ".";
}

}  /* namespace brb */

using namespace brb;

extern "C" {

BRB_API brb_status brb_load(const wchar_t *runtime_dir)
{
	std::wstring dir = runtime_dir ? runtime_dir : L"";
	while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) {
		dir.pop_back();
	}
	if (g_dll) {
		if (dir == g_runtime_dir) {
			return g_status;
		}
		brb_shutdown();
	}
	log("load runtime from '%s' (bridge API %s)", utf8(dir).c_str(), brb_api_version());

	const std::wstring dll_path = dir + L"\\d3d9.dll";
	if (dir.empty() || !file_exists(dll_path)) {
		set_status(BRB_STATUS_NOT_INSTALLED,
		           "No RTX Remix runtime installed (no d3d9.dll in '" + utf8(dir) + "').");
		return g_status;
	}

	remixapi_Interface iface = {};
	remixapi_HMODULE dll = nullptr;
	remixapi_ErrorCode r = remixapi_lib_loadRemixDllAndInitialize(dll_path.c_str(), &iface, &dll);
	if (r == REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION) {
		set_status(BRB_STATUS_INCOMPATIBLE_VERSION,
		           std::string("This RTX Remix runtime does not match BGE-Remix (Remix API ") +
		           brb_api_version() + "). Install the runtime from Preferences > System > RTX Remix.");
		return g_status;
	}
	if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
		set_status(BRB_STATUS_LOAD_FAILED, with_code("RTX Remix could not be loaded", r));
		return g_status;
	}
	g_remix = iface;
	g_dll = dll;
	g_runtime_dir = dir;
	set_status(BRB_STATUS_READY, "RTX Remix is installed and ready (" + utf8(dir) + ").");
	return g_status;
}

}  /* extern "C" */
