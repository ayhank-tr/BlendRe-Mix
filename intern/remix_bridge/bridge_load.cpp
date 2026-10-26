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

void release_runtime_paths()
{
	if (g_impl) {
		FreeLibrary(g_impl);
		g_impl = nullptr;
	}
	SetDllDirectoryW(nullptr);
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

	/* Remix's d3d9.dll is a small shim that loads d3d9_x64.dll from its own
	 * folder with a plain LoadLibraryW. Windows then looks for that DLL's own
	 * dependencies (NRD, DLSS, CUDA, TBB, USD...) next to blender.exe, not in
	 * the runtime folder, and the load fails. So: add the runtime folder to
	 * the DLL search path for as long as Remix is loaded, and preload the
	 * implementation with its own folder as the search base. */
	SetDllDirectoryW(dir.c_str());
	const std::wstring impl_path = dir + L"\\d3d9_x64.dll";
	if (file_exists(impl_path)) {
		g_impl = LoadLibraryExW(impl_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (g_impl == nullptr) {
			const DWORD err = GetLastError();
			release_runtime_paths();
			char buf[96];
			snprintf(buf, sizeof(buf), " (Windows error %lu).", (unsigned long)err);
			std::string text = "RTX Remix could not be loaded: Windows could not load d3d9_x64.dll or a DLL it needs";
			text += buf;
			if (err == ERROR_MOD_NOT_FOUND) {
				text += " A file is missing: reinstall the runtime, and make sure the Microsoft Visual C++ "
				        "Redistributable (2015-2022, x64) is installed.";
			}
			set_status(BRB_STATUS_LOAD_FAILED, text);
			return g_status;
		}
		log("preloaded %s", utf8(impl_path).c_str());
	}

	remixapi_Interface iface = {};
	remixapi_HMODULE dll = nullptr;
	remixapi_ErrorCode r;
	try {
		r = remixapi_lib_loadRemixDllAndInitialize(dll_path.c_str(), &iface, &dll);
	}
	catch (...) {
		/* The runtime reports some failures as C++ exceptions; never let one
		 * reach Blender. */
		log("the runtime raised an exception while loading");
		r = REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
	}
	if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
		release_runtime_paths();
	}
	if (r == REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION) {
		set_status(BRB_STATUS_INCOMPATIBLE_VERSION,
		           std::string("This RTX Remix runtime does not match BGE-Remix (Remix API ") +
		           brb_api_version() + "). Install the runtime from Preferences > System > RTX Remix.");
		return g_status;
	}
	if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
		set_status(BRB_STATUS_LOAD_FAILED, with_code("RTX Remix could not be loaded", r) +
		           " Details are in Remix's log and in " + log_path() + ".");
		return g_status;
	}
	g_remix = iface;
	g_dll = dll;
	g_runtime_dir = dir;
	set_status(BRB_STATUS_READY, "RTX Remix is installed and ready (" + utf8(dir) + ").");
	return g_status;
}

}  /* extern "C" */
