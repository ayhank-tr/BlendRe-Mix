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

/** \file bridge_startup.cpp
 *  \ingroup remix_bridge
 *
 * The bridge's window, Remix startup with the BGE-Remix configuration, and shutdown.
 */

#include <algorithm>

#include "bridge_intern.h"

namespace brb {

static const wchar_t *WINDOW_CLASS = L"BGERemixBridgeWindow";

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_CLOSE) {
		/* Keep the window (and Remix's surface) alive; just hide it. */
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	}
	if (msg == WM_NCHITTEST && (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)) {
		/* Window mode: the mouse belongs to the game window underneath. */
		return HTTRANSPARENT;
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* Remix hooks the window procedure of the window it renders into and does not
 * restore it at Shutdown; its hook then reads freed memory on the next
 * message (found on the RTX 3080: a crash in dxvk::D3D9WindowProc when the
 * window was destroyed). Put our own procedure back before anything else can
 * reach the window. */
static void unhook_window()
{
	if (g_hwnd && (WNDPROC)GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC) != window_proc) {
		SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)window_proc);
		log("window procedure restored after Remix");
	}
}

void destroy_window()
{
	if (g_hwnd) {
		unhook_window();
		DestroyWindow(g_hwnd);
		g_hwnd = nullptr;
	}
}

HWND create_window(int width, int height, bool visible)
{
	static bool registered = false;
	HINSTANCE inst = GetModuleHandleW(nullptr);
	if (!registered) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.style = CS_OWNDC;
		wc.lpfnWndProc = window_proc;
		wc.hInstance = inst;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.lpszClassName = WINDOW_CLASS;
		if (!RegisterClassExW(&wc)) {
			log("RegisterClassExW failed: %lu", GetLastError());
			return nullptr;
		}
		registered = true;
	}
	DWORD style = WS_OVERLAPPEDWINDOW;
	RECT r = {0, 0, width, height};
	AdjustWindowRect(&r, style, FALSE);
	HWND hwnd = CreateWindowExW(0, WINDOW_CLASS, L"BGE-Remix (RTX Remix)", style,
	                            CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
	                            nullptr, nullptr, inst, nullptr);
	if (!hwnd) {
		log("CreateWindowExW failed: %lu", GetLastError());
		return nullptr;
	}
	if (visible) {
		ShowWindow(hwnd, SW_SHOWNORMAL);
		UpdateWindow(hwnd);
	}
	return hwnd;
}

void pump_messages()
{
	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

double now_ms()
{
	static LARGE_INTEGER freq = {};
	if (freq.QuadPart == 0) {
		QueryPerformanceFrequency(&freq);
	}
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return 1000.0 * (double)t.QuadPart / (double)freq.QuadPart;
}

}  /* namespace brb */

using namespace brb;

extern "C" {

BRB_API int brb_set_config(const char *key, const char *value)
{
	if (!g_dll || !g_remix.SetConfigVariable) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	remixapi_ErrorCode r = g_remix.SetConfigVariable(key, value);
	log("config %s = %s -> %u", key, value, (unsigned)r);
	return (int)r;
}

BRB_API brb_status brb_startup(int width, int height, int visible)
{
	if (g_status == BRB_STATUS_RUNNING) {
		return g_status;
	}
	if (g_status != BRB_STATUS_READY) {
		return g_status;
	}
	g_mode = BRB_MODE_WINDOW;
	if (!g_hwnd) {
		g_hwnd = create_window(std::max(width, 64), std::max(height, 64), visible != 0);
		if (!g_hwnd) {
			set_status(BRB_STATUS_STARTUP_FAILED, "RTX Remix could not start: Windows refused to create its window.");
			return g_status;
		}
	}
	pump_messages();

	remixapi_StartupInfo info = {};
	info.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
	info.pNext = nullptr;
	info.hwnd = g_hwnd;
	info.disableSrgbConversionForOutput = FALSE;
	info.forceNoVkSwapchain = FALSE;
	info.editorModeEnabled = FALSE;
	info.combineGuiInFinalColor = TRUE;

	const double t0 = now_ms();
	remixapi_ErrorCode r;
	try {
		r = g_remix.Startup(&info);
	}
	catch (...) {
		log("the runtime raised an exception in Startup");
		r = REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
	}
	log("Startup -> %u in %.1f ms", (unsigned)r, now_ms() - t0);
	if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
		destroy_window();
		g_mode = BRB_MODE_NONE;
		set_status(BRB_STATUS_STARTUP_FAILED, with_code("RTX Remix could not start", r));
		return g_status;
	}
	g_started = true;

	/* Blender: Z up, right-handed, 1 unit = 1 m; Remix renders in cm. */
	brb_set_config("rtx.zUp", "True");
	brb_set_config("rtx.leftHandedCoordinateSystem", "False");
	brb_set_config("rtx.sceneScale", "0.01");
	brb_set_config("rtx.captureShowMenuOnHotkey", "False");
	/* Highlighting (bge.render.remixHighlight) outlines the objects; by
	 * default Remix also greys out everything else, which suits its editor
	 * but not a game (RTX run 1 of P4). */
	brb_set_config("rtx.postfx.desaturateOthersOnHighlight", "False");

	set_status(BRB_STATUS_RUNNING, "RTX Remix is running.");
	return g_status;
}

BRB_API void brb_shutdown(void)
{
	/* Order matters: our objects on Remix's device go first, Remix shuts down,
	 * our window procedure goes back on, the window goes, and only then are the
	 * runtime's DLLs released. */
	device_release();
	scene_forget();
	if (g_dll && g_started && g_remix.Shutdown) {
		try {
			remixapi_ErrorCode r = g_remix.Shutdown();
			log("Shutdown -> %u", (unsigned)r);
		}
		catch (...) {
			log("the runtime raised an exception while shutting down");
		}
	}
	destroy_window();
	if (g_dll) {
		FreeLibrary(g_dll);
		log("runtime unloaded");
	}
	release_runtime_paths();
	g_remix = remixapi_Interface();
	g_dll = nullptr;
	g_started = false;
	g_mode = BRB_MODE_NONE;
	g_runtime_dir.clear();
	if (g_status != BRB_STATUS_NOT_LOADED) {
		set_status(BRB_STATUS_NOT_LOADED, "RTX Remix was shut down.");
	}
}

}  /* extern "C" */
