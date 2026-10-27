/*
 * BGE-Remix test double: shared between mock_remix.cpp and mock_d3d9.c.
 * GPL v2 or later, as Blender.
 */

#ifndef __MOCK_D3D9_H__
#define __MOCK_D3D9_H__

#include <windows.h>
#include <d3d9.h>

#ifdef __cplusplus
extern "C" {
#endif

void mock_log(const char *fmt, ...);
int mock_mode_is(const char *mode);
void mock_hook_window(HWND hwnd);
void mock_count_copy(void);
int mock_backbuffer_size(int *w, int *h);

IDirect3D9Ex *mock_create_d3d9(void);
int mock_is_device(IDirect3DDevice9Ex *device);
void mock_release_all(IDirect3DDevice9Ex *device, IDirect3D9Ex *d3d);

#ifdef __cplusplus
}
#endif

#endif
