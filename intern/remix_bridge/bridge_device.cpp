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

/** \file bridge_device.cpp
 *  \ingroup remix_bridge
 *
 * The game modes. Remix's own Startup() keeps its D3D9 device to itself, so
 * for games the bridge creates the device the same way Startup() does
 * (dxvk_CreateD3D9 + CreateDeviceEx + dxvk_RegisterD3D9Device) and keeps it:
 *
 * - Copy mode: the device presents into a hidden window. After each Present
 *   the finished frame is read back (GetFrontBufferData) into system memory,
 *   where the game engine picks it up and draws it with OpenGL. Remix's
 *   "external swapchain" gives a Vulkan image without its memory handle, so
 *   OpenGL cannot use the frame directly.
 * - Child mode: the device presents into a child window covering the game's
 *   window (blenderplayer only); nothing is copied.
 */

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "bridge_intern.h"

namespace brb {

brb_mode g_mode = BRB_MODE_NONE;

static IDirect3D9Ex *g_d3d = nullptr;
static IDirect3DDevice9Ex *g_device = nullptr;
static IDirect3DSurface9 *g_readback = nullptr; /* system memory copy of the frame */
static D3DFORMAT g_readback_format = D3DFMT_UNKNOWN;
static bool g_locked = false;
static D3DLOCKED_RECT g_lock = {};
static int g_width = 0, g_height = 0;
static bool g_have_pixels = false;
/* "front": GetFrontBufferData after Present (default); "back": GetRenderTargetData
 * of back buffer 0 (BGE_REMIX_COPY_SOURCE=back), for drivers where the hidden
 * window's swap chain does not rotate its buffers. */
static bool g_copy_from_back = false;

static const wchar_t *CHILD_CLASS = L"BGERemixBridgeChild";

static HWND create_hidden_window(int width, int height)
{
	/* WS_POPUP: the client area is exactly width x height, also when that is
	 * larger than the screen. Never shown. */
	static bool registered = false;
	HINSTANCE inst = GetModuleHandleW(nullptr);
	static const wchar_t *HIDDEN_CLASS = L"BGERemixBridgeHidden";
	if (!registered) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = window_proc;
		wc.hInstance = inst;
		wc.lpszClassName = HIDDEN_CLASS;
		if (!RegisterClassExW(&wc)) {
			log("RegisterClassExW (hidden) failed: %lu", GetLastError());
			return nullptr;
		}
		registered = true;
	}
	HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, HIDDEN_CLASS, L"BGE-Remix (RTX Remix, copy)",
	                            WS_POPUP, 0, 0, width, height, nullptr, nullptr, inst, nullptr);
	if (!hwnd) {
		log("CreateWindowExW (hidden) failed: %lu", GetLastError());
	}
	return hwnd;
}

static HWND create_child_window(HWND parent, int x, int y, int width, int height)
{
	static bool registered = false;
	HINSTANCE inst = GetModuleHandleW(nullptr);
	if (!registered) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = window_proc;
		wc.hInstance = inst;
		wc.lpszClassName = CHILD_CLASS;
		if (!RegisterClassExW(&wc)) {
			log("RegisterClassExW (child) failed: %lu", GetLastError());
			return nullptr;
		}
		registered = true;
	}
	/* The game window must not paint over its child (GHOST's window has no
	 * WS_CLIPCHILDREN). */
	const LONG_PTR style = GetWindowLongPtrW(parent, GWL_STYLE);
	if (!(style & WS_CLIPCHILDREN)) {
		SetWindowLongPtrW(parent, GWL_STYLE, style | WS_CLIPCHILDREN);
	}
	/* Disabled and transparent for the mouse: keyboard and mouse stay with the
	 * game window. */
	HWND hwnd = CreateWindowExW(WS_EX_NOPARENTNOTIFY, CHILD_CLASS, L"BGE-Remix (RTX Remix)",
	                            WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS,
	                            x, y, width, height, parent, nullptr, inst, nullptr);
	if (!hwnd) {
		log("CreateWindowExW (child) failed: %lu", GetLastError());
	}
	return hwnd;
}

static D3DPRESENT_PARAMETERS present_params(int width, int height)
{
	/* As Remix's own Startup() */
	D3DPRESENT_PARAMETERS pp = {};
	pp.BackBufferWidth = (UINT)width;
	pp.BackBufferHeight = (UINT)height;
	pp.BackBufferFormat = D3DFMT_UNKNOWN;
	pp.BackBufferCount = 0;
	pp.MultiSampleType = D3DMULTISAMPLE_NONE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.hDeviceWindow = g_hwnd;
	pp.Windowed = TRUE;
	pp.EnableAutoDepthStencil = FALSE;
	pp.PresentationInterval = 0;
	return pp;
}

static void release_readback()
{
	device_frame_unlock();
	if (g_readback) {
		g_readback->Release();
		g_readback = nullptr;
	}
	g_have_pixels = false;
}

static bool create_readback()
{
	release_readback();
	if (g_mode != BRB_MODE_COPY || !g_device) {
		return true;
	}
	/* Same format as the back buffer, so the copy needs no conversion. */
	D3DFORMAT format = D3DFMT_A8R8G8B8;
	IDirect3DSurface9 *bb = nullptr;
	if (SUCCEEDED(g_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
		D3DSURFACE_DESC desc = {};
		if (SUCCEEDED(bb->GetDesc(&desc)) &&
		    (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8))
		{
			format = desc.Format;
		}
		bb->Release();
	}
	HRESULT hr = g_device->CreateOffscreenPlainSurface((UINT)g_width, (UINT)g_height, format,
	                                                  D3DPOOL_SYSTEMMEM, &g_readback, nullptr);
	if (FAILED(hr) || !g_readback) {
		log("CreateOffscreenPlainSurface %dx%d failed: 0x%08lx", g_width, g_height, (unsigned long)hr);
		g_readback = nullptr;
		return false;
	}
	g_readback_format = format;
	return true;
}

void device_frame_unlock()
{
	if (g_locked && g_readback) {
		g_readback->UnlockRect();
	}
	g_locked = false;
	g_lock = D3DLOCKED_RECT();
}

void device_release()
{
	release_readback();
	if (g_device || g_d3d) {
		/* Remix's Shutdown releases the device and the D3D9 object until nothing
		 * holds them; the bridge only forgets its pointers. */
		log("game mode device handed back to Remix for shutdown");
	}
	g_device = nullptr;
	g_d3d = nullptr;
	g_width = g_height = 0;
}

static remixapi_ErrorCode copy_frame()
{
	if (!g_readback) {
		return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
	}
	HRESULT hr;
	if (g_copy_from_back) {
		IDirect3DSurface9 *bb = nullptr;
		hr = g_device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
		if (SUCCEEDED(hr) && bb) {
			hr = g_device->GetRenderTargetData(bb, g_readback);
			bb->Release();
		}
	}
	else {
		hr = g_device->GetFrontBufferData(0, g_readback);
	}
	if (FAILED(hr)) {
		log("copying the frame failed: 0x%08lx", (unsigned long)hr);
		return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
	}
	hr = g_readback->LockRect(&g_lock, nullptr, D3DLOCK_READONLY);
	if (FAILED(hr) || !g_lock.pBits) {
		log("LockRect failed: 0x%08lx", (unsigned long)hr);
		return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
	}
	g_locked = true;
	g_have_pixels = true;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode device_present()
{
	const double t0 = now_ms();
	remixapi_ErrorCode r = guarded("Present", [] { return g_remix.Present(nullptr); });
	const double t1 = now_ms();
	g_stats.present_ms = t1 - t0;
	g_stats.copy_ms = 0.0;
	if (r == REMIXAPI_ERROR_CODE_SUCCESS && g_mode == BRB_MODE_COPY) {
		try {
			r = copy_frame();
		}
		catch (...) {
			log("the runtime raised an exception while copying the frame");
			r = REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
		}
		g_stats.copy_ms = now_ms() - t1;
	}
	return r;
}

static bool env_is(const char *name, const char *value)
{
	char buf[64];
	const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
	return n > 0 && n < sizeof(buf) && _stricmp(buf, value) == 0;
}

}  /* namespace brb */

using namespace brb;

extern "C" {

BRB_API int brb_get_mode(void)
{
	return (int)g_mode;
}

static brb_status startup_ex_impl(int mode, int width, int height, HWND parent, int x, int y)
{
	width = std::max(width, 8);
	height = std::max(height, 8);
	g_mode = (brb_mode)mode;
	g_width = width;
	g_height = height;
	g_copy_from_back = env_is("BGE_REMIX_COPY_SOURCE", "back");

	g_hwnd = (mode == BRB_MODE_CHILD) ? create_child_window(parent, x, y, width, height) :
	                                    create_hidden_window(width, height);
	if (!g_hwnd) {
		g_mode = BRB_MODE_NONE;
		set_status(BRB_STATUS_STARTUP_FAILED, "RTX Remix could not start: Windows refused to create its window.");
		return g_status;
	}

	const double t0 = now_ms();
	remixapi_ErrorCode r = REMIXAPI_ERROR_CODE_SUCCESS;
	if (!g_remix.dxvk_CreateD3D9 || !g_remix.dxvk_RegisterD3D9Device) {
		r = REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE;
	}
	if (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		/* editorModeEnabled = FALSE: game settings, sRGB output, Remix's menu
		 * drawn into the picture */
		r = guarded("dxvk_CreateD3D9", [] { return g_remix.dxvk_CreateD3D9(FALSE, &g_d3d); });
		if (r == REMIXAPI_ERROR_CODE_SUCCESS && !g_d3d) {
			r = REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
		}
	}
	if (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		D3DPRESENT_PARAMETERS pp = present_params(width, height);
		HRESULT hr;
		try {
			hr = g_d3d->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hwnd,
			                           D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr, &g_device);
		}
		catch (...) {
			log("the runtime raised an exception in CreateDeviceEx");
			hr = E_FAIL;
		}
		if (FAILED(hr) || !g_device) {
			log("CreateDeviceEx failed: 0x%08lx", (unsigned long)hr);
			/* Remix passes its own error codes (0x8896xxxx) through HRESULTs */
			r = (((unsigned long)hr & 0xFFFF0000ul) == 0x88960000ul) ? (remixapi_ErrorCode)(unsigned long)hr :
			                                                           REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
			g_device = nullptr;
		}
	}
	if (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		r = guarded("dxvk_RegisterD3D9Device", [] { return g_remix.dxvk_RegisterD3D9Device(g_device); });
	}
	log("game mode %d startup %dx%d -> %u in %.1f ms", mode, width, height, (unsigned)r, now_ms() - t0);
	if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
		if (g_device) {
			g_device->Release();
		}
		if (g_d3d) {
			g_d3d->Release();
		}
		g_device = nullptr;
		g_d3d = nullptr;
		destroy_window();
		g_mode = BRB_MODE_NONE;
		set_status(BRB_STATUS_STARTUP_FAILED, with_code("RTX Remix could not start", r));
		return g_status;
	}
	g_started = true;
	g_stats.width = width;
	g_stats.height = height;

	set_scene_defaults();
	if (env_is("BGE_REMIX_NO_REPLACEMENTS", "1")) {
		/* Captures need replacement assets off before the first frame (C1). */
		brb_set_config("rtx.enableReplacementAssets", "False");
	}

	if (!create_readback()) {
		set_status(BRB_STATUS_RUNNING, "RTX Remix is running, but its picture cannot be copied.");
		return g_status;
	}
	set_status(BRB_STATUS_RUNNING, mode == BRB_MODE_COPY ? "RTX Remix is running (copy mode)." :
	                                                       "RTX Remix is running (window mode).");
	return g_status;
}

BRB_API brb_status brb_startup_ex(int mode, int width, int height, void *parent_hwnd, int x, int y)
{
	if (mode != BRB_MODE_COPY && mode != BRB_MODE_CHILD) {
		return g_status;
	}
	if (g_status == BRB_STATUS_RUNNING) {
		if (g_mode == mode) {
			brb_resize(width, height, x, y);
		}
		return g_status;
	}
	if (g_status != BRB_STATUS_READY) {
		return g_status;
	}
	if (mode == BRB_MODE_CHILD && !IsWindow((HWND)parent_hwnd)) {
		set_status(BRB_STATUS_STARTUP_FAILED, "RTX Remix could not start: the game window was not found.");
		/* the runtime is still loaded and usable in another mode */
		g_status = BRB_STATUS_READY;
		return BRB_STATUS_STARTUP_FAILED;
	}
	try {
		return startup_ex_impl(mode, width, height, (HWND)parent_hwnd, x, y);
	}
	catch (...) {
		log("unexpected exception during game mode startup");
		set_status(BRB_STATUS_STARTUP_FAILED, "RTX Remix could not start (unexpected error).");
		return g_status;
	}
}

BRB_API int brb_resize(int width, int height, int x, int y)
{
	if (g_status != BRB_STATUS_RUNNING || !g_device || !g_hwnd) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	width = std::max(width, 8);
	height = std::max(height, 8);
	if (g_mode == BRB_MODE_CHILD) {
		SetWindowPos(g_hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
	}
	if (width == g_width && height == g_height) {
		return REMIXAPI_ERROR_CODE_SUCCESS;
	}
	release_readback();
	if (g_mode != BRB_MODE_CHILD) {
		SetWindowPos(g_hwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
	}
	D3DPRESENT_PARAMETERS pp = present_params(width, height);
	HRESULT hr;
	try {
		hr = g_device->ResetEx(&pp, nullptr);
	}
	catch (...) {
		hr = E_FAIL;
	}
	log("resize to %dx%d -> 0x%08lx", width, height, (unsigned long)hr);
	if (FAILED(hr)) {
		/* keep the old size working */
		g_stats.errors++;
		if (g_mode != BRB_MODE_CHILD) {
			SetWindowPos(g_hwnd, nullptr, 0, 0, g_width, g_height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
		}
		create_readback();
		return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
	}
	g_width = width;
	g_height = height;
	g_stats.width = width;
	g_stats.height = height;
	return create_readback() ? REMIXAPI_ERROR_CODE_SUCCESS : REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
}

BRB_API void brb_show(int visible)
{
	if (g_mode == BRB_MODE_CHILD && g_hwnd) {
		ShowWindow(g_hwnd, visible ? SW_SHOWNA : SW_HIDE);
	}
}

BRB_API const void *brb_frame_pixels(int *r_width, int *r_height, int *r_pitch)
{
	if (g_mode != BRB_MODE_COPY || !g_locked || !g_have_pixels) {
		return nullptr;
	}
	if (r_width) {
		*r_width = g_width;
	}
	if (r_height) {
		*r_height = g_height;
	}
	if (r_pitch) {
		*r_pitch = (int)g_lock.Pitch;
	}
	return g_lock.pBits;
}

BRB_API int brb_save_bmp(const wchar_t *path)
{
	if (!path || !path[0]) {
		return 0;
	}
	bool ok;
	if (g_mode == BRB_MODE_COPY) {
		int w = 0, h = 0, pitch = 0;
		const void *px = brb_frame_pixels(&w, &h, &pitch);
		ok = save_bmp(path, (const unsigned char *)px, w, h, pitch);
	}
	else {
		ok = g_hwnd && save_window_bmp(g_hwnd, path);
	}
	log("screenshot %s: %s", utf8(path).c_str(), ok ? "saved" : "failed");
	return ok ? 1 : 0;
}

}  /* extern "C" */
