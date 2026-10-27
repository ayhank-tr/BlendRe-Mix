/*
 * BGE-Remix test double: the few Direct3D 9 objects the bridge's game modes
 * use (P2). Written in C so the COM tables can be filled by name; every
 * method the bridge does not use answers E_NOTIMPL and is logged.
 *
 * GetFrontBufferData and GetRenderTargetData fill the copy with a fixed
 * colour (red 64, green 128, blue 192), so a test can find Remix's "picture"
 * in the game's OpenGL window.
 *
 * GPL v2 or later, as Blender.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <d3d9.h>

#include "mock_d3d9.h"

/* ------------------------------------------------------------------------- */
/* Shared */

static HRESULT STDMETHODCALLTYPE not_implemented(void *self)
{
	(void)self;
	mock_log("D3D9 method not implemented in the mock was called");
	return E_NOTIMPL;
}

/* Fill every slot of a COM table with not_implemented. */
static void fill_table(void *table, size_t size)
{
	void **slots = (void **)table;
	size_t i;
	for (i = 0; i < size / sizeof(void *); i++) {
		slots[i] = (void *)not_implemented;
	}
}

/* ------------------------------------------------------------------------- */
/* Surface (system memory) */

typedef struct MockSurface {
	IDirect3DSurface9 iface;
	LONG refs;
	UINT width, height;
	D3DFORMAT format;
	unsigned char *pixels;
	int locked;
} MockSurface;

static ULONG STDMETHODCALLTYPE surf_AddRef(IDirect3DSurface9 *s)
{
	return (ULONG)InterlockedIncrement(&((MockSurface *)s)->refs);
}

static ULONG STDMETHODCALLTYPE surf_Release(IDirect3DSurface9 *s)
{
	MockSurface *m = (MockSurface *)s;
	LONG n = InterlockedDecrement(&m->refs);
	if (n == 0) {
		free(m->pixels);
		free(m);
	}
	return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE surf_GetDesc(IDirect3DSurface9 *s, D3DSURFACE_DESC *d)
{
	MockSurface *m = (MockSurface *)s;
	memset(d, 0, sizeof(*d));
	d->Format = m->format;
	d->Type = D3DRTYPE_SURFACE;
	d->Pool = D3DPOOL_SYSTEMMEM;
	d->Width = m->width;
	d->Height = m->height;
	return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE surf_LockRect(IDirect3DSurface9 *s, D3DLOCKED_RECT *r, const RECT *rect, DWORD flags)
{
	MockSurface *m = (MockSurface *)s;
	(void)rect;
	(void)flags;
	if (m->locked) {
		return D3DERR_INVALIDCALL;
	}
	m->locked = 1;
	r->Pitch = (INT)(m->width * 4);
	r->pBits = m->pixels;
	return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE surf_UnlockRect(IDirect3DSurface9 *s)
{
	MockSurface *m = (MockSurface *)s;
	if (!m->locked) {
		return D3DERR_INVALIDCALL;
	}
	m->locked = 0;
	return D3D_OK;
}

static IDirect3DSurface9Vtbl g_surface_table;

static MockSurface *surface_new(UINT w, UINT h, D3DFORMAT format)
{
	MockSurface *m = (MockSurface *)calloc(1, sizeof(MockSurface));
	if (!g_surface_table.Release) {
		fill_table(&g_surface_table, sizeof(g_surface_table));
		g_surface_table.AddRef = surf_AddRef;
		g_surface_table.Release = surf_Release;
		g_surface_table.GetDesc = surf_GetDesc;
		g_surface_table.LockRect = surf_LockRect;
		g_surface_table.UnlockRect = surf_UnlockRect;
	}
	m->iface.lpVtbl = &g_surface_table;
	m->refs = 1;
	m->width = w;
	m->height = h;
	m->format = format;
	m->pixels = (unsigned char *)calloc((size_t)w * h, 4);
	return m;
}

/* B, G, R, A in memory */
static void fill_picture(MockSurface *m)
{
	size_t i, n = (size_t)m->width * m->height;
	for (i = 0; i < n; i++) {
		m->pixels[i * 4 + 0] = 192;
		m->pixels[i * 4 + 1] = 128;
		m->pixels[i * 4 + 2] = 64;
		m->pixels[i * 4 + 3] = 255;
	}
}

/* ------------------------------------------------------------------------- */
/* Device */

typedef struct MockDevice {
	IDirect3DDevice9Ex iface;
	LONG refs;
	HWND hwnd;
	UINT width, height;
	MockSurface *backbuffer;
} MockDevice;

static MockDevice *g_device;

/* the back buffer's size (P4: the fake picking answers in its pixels) */
int mock_backbuffer_size(int *w, int *h)
{
	if (!g_device) {
		return 0;
	}
	*w = (int)g_device->width;
	*h = (int)g_device->height;
	return 1;
}

static ULONG STDMETHODCALLTYPE dev_AddRef(IDirect3DDevice9Ex *d)
{
	return (ULONG)InterlockedIncrement(&((MockDevice *)d)->refs);
}

static ULONG STDMETHODCALLTYPE dev_Release(IDirect3DDevice9Ex *d)
{
	MockDevice *m = (MockDevice *)d;
	LONG n = InterlockedDecrement(&m->refs);
	if (n == 0) {
		mock_log("Device released");
		if (m->backbuffer) {
			surf_Release(&m->backbuffer->iface);
		}
		if (g_device == m) {
			g_device = NULL;
		}
		free(m);
	}
	return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE dev_CreateOffscreenPlainSurface(
        IDirect3DDevice9Ex *d, UINT w, UINT h, D3DFORMAT format, D3DPOOL pool,
        IDirect3DSurface9 **out, HANDLE *shared)
{
	(void)d;
	(void)shared;
	mock_log("CreateOffscreenPlainSurface %ux%u pool=%d", w, h, (int)pool);
	*out = &surface_new(w, h, format)->iface;
	return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE dev_GetFrontBufferData(IDirect3DDevice9Ex *d, UINT swapchain, IDirect3DSurface9 *dst)
{
	MockSurface *m = (MockSurface *)dst;
	(void)d;
	(void)swapchain;
	if (!m || m->locked) {
		return D3DERR_INVALIDCALL;
	}
	fill_picture(m);
	mock_count_copy();
	return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE dev_GetBackBuffer(IDirect3DDevice9Ex *d, UINT sc, UINT i, D3DBACKBUFFER_TYPE t,
                                                   IDirect3DSurface9 **out)
{
	MockDevice *m = (MockDevice *)d;
	(void)sc;
	(void)i;
	(void)t;
	surf_AddRef(&m->backbuffer->iface);
	*out = &m->backbuffer->iface;
	return D3D_OK;
}

static HRESULT STDMETHODCALLTYPE dev_GetRenderTargetData(IDirect3DDevice9Ex *d, IDirect3DSurface9 *src,
                                                         IDirect3DSurface9 *dst)
{
	(void)src;
	return dev_GetFrontBufferData(d, 0, dst);
}

static HRESULT STDMETHODCALLTYPE dev_ResetEx(IDirect3DDevice9Ex *d, D3DPRESENT_PARAMETERS *pp, D3DDISPLAYMODEEX *mode)
{
	MockDevice *m = (MockDevice *)d;
	(void)mode;
	m->width = pp->BackBufferWidth;
	m->height = pp->BackBufferHeight;
	surf_Release(&m->backbuffer->iface);
	m->backbuffer = surface_new(m->width, m->height, D3DFMT_X8R8G8B8);
	mock_log("ResetEx %ux%u", m->width, m->height);
	return D3D_OK;
}

static IDirect3DDevice9ExVtbl g_device_table;

/* ------------------------------------------------------------------------- */
/* IDirect3D9Ex */

typedef struct MockD3D {
	IDirect3D9Ex iface;
	LONG refs;
} MockD3D;

static ULONG STDMETHODCALLTYPE d3d_AddRef(IDirect3D9Ex *d)
{
	return (ULONG)InterlockedIncrement(&((MockD3D *)d)->refs);
}

static ULONG STDMETHODCALLTYPE d3d_Release(IDirect3D9Ex *d)
{
	MockD3D *m = (MockD3D *)d;
	LONG n = InterlockedDecrement(&m->refs);
	if (n == 0) {
		mock_log("D3D9 released");
		free(m);
	}
	return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE d3d_CreateDeviceEx(
        IDirect3D9Ex *d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD flags,
        D3DPRESENT_PARAMETERS *pp, D3DDISPLAYMODEEX *mode, IDirect3DDevice9Ex **out)
{
	MockDevice *m;
	const int valid = IsWindow(pp->hDeviceWindow) ? 1 : 0;
	(void)d;
	(void)adapter;
	(void)type;
	(void)focus;
	(void)flags;
	(void)mode;
	mock_log("CreateDeviceEx hwnd_valid=%d child=%d %ux%u", valid,
	         valid && (GetWindowLongPtrW(pp->hDeviceWindow, GWL_STYLE) & WS_CHILD) ? 1 : 0,
	         pp->BackBufferWidth, pp->BackBufferHeight);
	if (!valid) {
		return D3DERR_INVALIDCALL;
	}
	if (mock_mode_is("startup_fail")) {
		/* as a too old driver does */
		return (HRESULT)0x88960002;
	}
	if (!g_device_table.Release) {
		fill_table(&g_device_table, sizeof(g_device_table));
		g_device_table.AddRef = dev_AddRef;
		g_device_table.Release = dev_Release;
		g_device_table.CreateOffscreenPlainSurface = dev_CreateOffscreenPlainSurface;
		g_device_table.GetFrontBufferData = dev_GetFrontBufferData;
		g_device_table.GetBackBuffer = dev_GetBackBuffer;
		g_device_table.GetRenderTargetData = dev_GetRenderTargetData;
		g_device_table.ResetEx = dev_ResetEx;
	}
	m = (MockDevice *)calloc(1, sizeof(MockDevice));
	m->iface.lpVtbl = &g_device_table;
	m->refs = 1;
	m->hwnd = pp->hDeviceWindow;
	m->width = pp->BackBufferWidth;
	m->height = pp->BackBufferHeight;
	m->backbuffer = surface_new(m->width, m->height, D3DFMT_X8R8G8B8);
	/* like the real runtime: the device window is hooked */
	mock_hook_window(m->hwnd);
	g_device = m;
	*out = &m->iface;
	return D3D_OK;
}

static IDirect3D9ExVtbl g_d3d_table;

IDirect3D9Ex *mock_create_d3d9(void)
{
	MockD3D *m = (MockD3D *)calloc(1, sizeof(MockD3D));
	if (!g_d3d_table.Release) {
		fill_table(&g_d3d_table, sizeof(g_d3d_table));
		g_d3d_table.AddRef = d3d_AddRef;
		g_d3d_table.Release = d3d_Release;
		g_d3d_table.CreateDeviceEx = d3d_CreateDeviceEx;
	}
	m->iface.lpVtbl = &g_d3d_table;
	m->refs = 1;
	return &m->iface;
}

int mock_is_device(IDirect3DDevice9Ex *device)
{
	return device != NULL && device->lpVtbl == &g_device_table;
}

/* As Remix's Shutdown: release until nothing holds them. */
void mock_release_all(IDirect3DDevice9Ex *device, IDirect3D9Ex *d3d)
{
	if (device) {
		while (dev_Release(device) > 0) {
		}
	}
	if (d3d) {
		while (d3d_Release(d3d) > 0) {
		}
	}
}
