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
 * - Direct mode (P4.8): Remix is started with forceNoVkSwapchain, so it has no
 *   window of its own. After each Present it hands over its picture as a
 *   Vulkan image plus a "done" and a "resume" semaphore
 *   (dxvk_GetExternalSwapchain); the game waits for done, draws the image with
 *   GL_NV_draw_vulkan_image and signals resume. Nothing is copied, and the
 *   game's thread does not wait for Remix's graphics work. Remix keeps its
 *   device to itself in this mode; its present call resizes the picture to
 *   the hidden window (P5: the bridge resizes that window).
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

/* direct mode: the last presented frame, until brb_frame_vk takes it */
static bool g_direct_pending = false;
static bool g_direct_resized = false; /* Remix changed the picture size on that frame */
static uint64_t g_vk_image = 0, g_vk_done = 0, g_vk_resume = 0;
/* direct mode sizes: g_width x g_height is the picture's (Remix's back buffer),
 * the hidden window's client area is g_win_w x g_win_h, and the size asked
 * for is g_target_w x g_target_h */
static int g_win_w = 0, g_win_h = 0;
static int g_target_w = 0, g_target_h = 0;

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
	g_direct_pending = false;
	g_direct_resized = false;
	g_vk_image = g_vk_done = g_vk_resume = 0;
	g_win_w = g_win_h = g_target_w = g_target_h = 0;
}

/* Direct mode: Remix's present call (remixapi_Present, 1.6.1) resets its
 * picture to the window's client size, but only when the width and the
 * height both differ from the picture's. The window goes straight to the size
 * asked for when both sides change, and through an in-between size (both
 * sides different from the old and from the new size) when only one does. */
static void direct_set_window(int w, int h)
{
	if (w == g_win_w && h == g_win_h) {
		return;
	}
	SetWindowPos(g_hwnd, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
	RECT r = {};
	GetClientRect(g_hwnd, &r);
	g_win_w = (int)(r.right - r.left);
	g_win_h = (int)(r.bottom - r.top);
}

static void direct_size_step()
{
	if (!g_hwnd || g_target_w <= 0 || g_target_h <= 0) {
		return;
	}
	if (g_target_w == g_width && g_target_h == g_height) {
		direct_set_window(g_width, g_height);
	}
	else if (g_target_w != g_width && g_target_h != g_height) {
		direct_set_window(g_target_w, g_target_h);
	}
	else {
		const int iw = (g_target_w + 1 != g_width) ? g_target_w + 1 : g_target_w + 2;
		const int ih = (g_target_h + 1 != g_height) ? g_target_h + 1 : g_target_h + 2;
		direct_set_window(iw, ih);
	}
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
	/* direct mode: this present resizes the picture when the window's width and
	 * height both differ from it (see direct_size_step) */
	const bool resizes = g_mode == BRB_MODE_DIRECT && g_win_w > 0 && g_win_h > 0 && g_win_w != g_width &&
	                     g_win_h != g_height;
	const double t0 = now_ms();
	remixapi_ErrorCode r = guarded("Present", [] { return g_remix.Present(nullptr); });
	const double t1 = now_ms();
	if (r == REMIXAPI_ERROR_CODE_SUCCESS && resizes) {
		log("direct mode: picture %dx%d -> %dx%d", g_width, g_height, g_win_w, g_win_h);
		g_width = g_win_w;
		g_height = g_win_h;
		g_stats.width = g_width;
		g_stats.height = g_height;
		g_stats.resets++;
		/* from an in-between size on to the size asked for */
		direct_size_step();
	}
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
	else if (r == REMIXAPI_ERROR_CODE_SUCCESS && g_mode == BRB_MODE_DIRECT) {
		/* Remix has signalled "done" and waits for "resume": the game must take this frame */
		uint64_t image = 0, done = 0, resume = 0;
		remixapi_ErrorCode e = g_remix.dxvk_GetExternalSwapchain ?
		                           guarded("dxvk_GetExternalSwapchain", [&] {
			                           return g_remix.dxvk_GetExternalSwapchain(&image, &done, &resume);
		                           }) :
		                           REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE;
		if (e == REMIXAPI_ERROR_CODE_SUCCESS && image && done && resume) {
			g_vk_image = image;
			g_vk_done = done;
			g_vk_resume = resume;
			g_direct_pending = true;
			g_direct_resized = resizes;
		}
		else {
			log("direct mode: no external swapchain after Present (%u)", (unsigned)e);
			g_direct_pending = false;
			r = (e != REMIXAPI_ERROR_CODE_SUCCESS) ? e : REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
		}
	}
	return r;
}

/* Overlay layers of game launchers (GOG Galaxy, Epic, Steam) load into every
 * Vulkan program. The OpenGL extension uses Remix's Vulkan handles as they
 * are, and layers may wrap them (NV_draw_vulkan_image, issue 1), so they are
 * kept out of direct mode. Set before Remix creates its Vulkan instance; an
 * existing VK_LOADER_LAYERS_DISABLE, or BGE_REMIX_KEEP_VK_LAYERS=1, wins. */
static void disable_overlay_layers()
{
	char buf[8];
	if (GetEnvironmentVariableA("BGE_REMIX_KEEP_VK_LAYERS", buf, sizeof(buf)) > 0 && buf[0] == '1') {
		return;
	}
	if (GetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE", nullptr, 0) > 0) {
		return;
	}
	const char *layers = "GalaxyOverlayVkLayer*,VK_LAYER_EOS_Overlay*,VK_LAYER_VALVE_steam_overlay*";
	SetEnvironmentVariableA("VK_LOADER_LAYERS_DISABLE", layers);
	_putenv_s("VK_LOADER_LAYERS_DISABLE", layers);
	log("direct mode: Vulkan overlay layers off (%s)", layers);
}

static remixapi_ErrorCode start_direct(int width, int height)
{
	if (!g_remix.Startup || !g_remix.dxvk_GetExternalSwapchain) {
		return REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE;
	}
	disable_overlay_layers();
	remixapi_StartupInfo info = {};
	info.sType = REMIXAPI_STRUCT_TYPE_STARTUP_INFO;
	info.pNext = nullptr;
	info.hwnd = g_hwnd; /* only its size is used: Remix makes its picture that big */
	info.disableSrgbConversionForOutput = FALSE;
	info.forceNoVkSwapchain = TRUE;
	info.editorModeEnabled = FALSE;
	info.combineGuiInFinalColor = TRUE;
	(void)width;
	(void)height;
	remixapi_ErrorCode r;
	try {
		r = g_remix.Startup(&info);
	}
	catch (...) {
		log("the runtime raised an exception in Startup (direct mode)");
		r = REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
	}
	return r;
}

static bool env_is(const char *name, const char *value)
{
	char buf[64];
	const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
	return n > 0 && n < sizeof(buf) && _stricmp(buf, value) == 0;
}

const void *device_pixels(int *r_width, int *r_height, int *r_pitch)
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

bool device_has_vk()
{
	return g_mode == BRB_MODE_DIRECT && g_direct_pending;
}

int device_take_vk(uint64_t *r_image, uint64_t *r_done, uint64_t *r_resume, int *r_width, int *r_height)
{
	if (g_mode != BRB_MODE_DIRECT || !g_direct_pending) {
		return 0;
	}
	g_direct_pending = false;
	if (r_image) {
		*r_image = g_vk_image;
	}
	if (r_done) {
		*r_done = g_vk_done;
	}
	if (r_resume) {
		*r_resume = g_vk_resume;
	}
	if (r_width) {
		*r_width = g_width;
	}
	if (r_height) {
		*r_height = g_height;
	}
	return g_direct_resized ? 2 : 1;
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
	g_direct_pending = false;
	g_direct_resized = false;
	if (g_hwnd) {
		RECT rc = {};
		GetClientRect(g_hwnd, &rc);
		g_win_w = g_target_w = (int)(rc.right - rc.left);
		g_win_h = g_target_h = (int)(rc.bottom - rc.top);
		if (mode == BRB_MODE_DIRECT) {
			/* Remix takes the window's size as its picture's */
			g_width = g_win_w;
			g_height = g_win_h;
		}
	}
	if (!g_hwnd) {
		g_mode = BRB_MODE_NONE;
		set_status(BRB_STATUS_STARTUP_FAILED, "RTX Remix could not start: Windows refused to create its window.");
		return g_status;
	}

	const double t0 = now_ms();
	remixapi_ErrorCode r = REMIXAPI_ERROR_CODE_SUCCESS;
	if (mode == BRB_MODE_DIRECT) {
		r = start_direct(width, height);
	}
	else if (!g_remix.dxvk_CreateD3D9 || !g_remix.dxvk_RegisterD3D9Device) {
		r = REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE;
	}
	if (r == REMIXAPI_ERROR_CODE_SUCCESS && mode != BRB_MODE_DIRECT) {
		/* editorModeEnabled = FALSE: game settings, sRGB output, Remix's menu
		 * drawn into the picture */
		r = guarded("dxvk_CreateD3D9", [] { return g_remix.dxvk_CreateD3D9(FALSE, &g_d3d); });
		if (r == REMIXAPI_ERROR_CODE_SUCCESS && !g_d3d) {
			r = REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
		}
	}
	if (r == REMIXAPI_ERROR_CODE_SUCCESS && mode != BRB_MODE_DIRECT) {
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
	if (r == REMIXAPI_ERROR_CODE_SUCCESS && mode != BRB_MODE_DIRECT) {
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
	g_stats.width = g_width;
	g_stats.height = g_height;

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
	                               mode == BRB_MODE_DIRECT ? "RTX Remix is running (direct mode)." :
	                                                         "RTX Remix is running (window mode).");
	return g_status;
}

BRB_API brb_status brb_startup_ex(int mode, int width, int height, void *parent_hwnd, int x, int y)
{
	if (mode != BRB_MODE_COPY && mode != BRB_MODE_CHILD && mode != BRB_MODE_DIRECT) {
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
	if (g_status == BRB_STATUS_RUNNING && g_mode == BRB_MODE_DIRECT) {
		/* Remix keeps its device to itself here; its present call follows the
		 * hidden window's size (direct_size_step) */
		if (!g_hwnd) {
			return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
		}
		width = std::max(width, 8);
		height = std::max(height, 8);
		if (width != g_target_w || height != g_target_h) {
			log("direct mode: resize to %dx%d asked (picture %dx%d)", width, height, g_width, g_height);
		}
		g_target_w = width;
		g_target_h = height;
		direct_size_step();
		return REMIXAPI_ERROR_CODE_SUCCESS;
	}
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
	return device_pixels(r_width, r_height, r_pitch);
}

BRB_API int brb_frame_vk(uint64_t *r_image, uint64_t *r_done, uint64_t *r_resume, int *r_width, int *r_height)
{
	return device_take_vk(r_image, r_done, r_resume, r_width, r_height);
}

BRB_API int brb_save_bmp(const wchar_t *path)
{
	if (!path || !path[0] || g_mode == BRB_MODE_DIRECT) {
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
