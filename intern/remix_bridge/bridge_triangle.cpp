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

/** \file bridge_triangle.cpp
 *  \ingroup remix_bridge
 *
 * The P1 test scene: a lit triangle over a floor, drawn through the Remix API.
 */

#include <algorithm>
#include <cstdio>
#include <vector>

#include "bridge_intern.h"

namespace brb {

bool save_window_bmp(HWND hwnd, const wchar_t *path)
{
	RECT rc;
	GetClientRect(hwnd, &rc);
	const int w = rc.right - rc.left, h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0) {
		return false;
	}
	HDC wdc = GetDC(hwnd);
	HDC mdc = CreateCompatibleDC(wdc);
	HBITMAP bmp = CreateCompatibleBitmap(wdc, w, h);
	HGDIOBJ old = SelectObject(mdc, bmp);
	const UINT PW_CLIENTONLY_ = 0x1, PW_RENDERFULLCONTENT_ = 0x2;
	BOOL ok = PrintWindow(hwnd, mdc, PW_CLIENTONLY_ | PW_RENDERFULLCONTENT_);
	if (!ok) {
		ok = BitBlt(mdc, 0, 0, w, h, wdc, 0, 0, SRCCOPY);
	}

	BITMAPINFOHEADER bi = {};
	bi.biSize = sizeof(bi);
	bi.biWidth = w;
	bi.biHeight = h; /* bottom-up, as BMP files store it */
	bi.biPlanes = 1;
	bi.biBitCount = 24;
	bi.biCompression = BI_RGB;
	const int stride = ((w * 3 + 3) / 4) * 4;
	std::vector<unsigned char> pixels((size_t)stride * h);
	GetDIBits(mdc, bmp, 0, h, pixels.data(), (BITMAPINFO *)&bi, DIB_RGB_COLORS);

	SelectObject(mdc, old);
	DeleteObject(bmp);
	DeleteDC(mdc);
	ReleaseDC(hwnd, wdc);

	FILE *f = _wfopen(path, L"wb");
	if (!f) {
		return false;
	}
	BITMAPFILEHEADER bf = {};
	bf.bfType = 0x4D42;
	bf.bfOffBits = sizeof(bf) + sizeof(bi);
	bf.bfSize = bf.bfOffBits + (DWORD)pixels.size();
	fwrite(&bf, sizeof(bf), 1, f);
	fwrite(&bi, sizeof(bi), 1, f);
	fwrite(pixels.data(), 1, pixels.size(), f);
	fclose(f);
	return ok != FALSE;
}

bool save_bmp(const wchar_t *path, const unsigned char *bgra, int width, int height, int pitch)
{
	if (!bgra || width <= 0 || height <= 0) {
		return false;
	}
	BITMAPINFOHEADER bi = {};
	bi.biSize = sizeof(bi);
	bi.biWidth = width;
	bi.biHeight = height; /* bottom-up */
	bi.biPlanes = 1;
	bi.biBitCount = 24;
	bi.biCompression = BI_RGB;
	const int stride = ((width * 3 + 3) / 4) * 4;
	std::vector<unsigned char> pixels((size_t)stride * height, 0);
	for (int y = 0; y < height; y++) {
		const unsigned char *src = bgra + (size_t)pitch * (height - 1 - y);
		unsigned char *dst = &pixels[(size_t)stride * y];
		for (int x = 0; x < width; x++) {
			dst[x * 3 + 0] = src[x * 4 + 0];
			dst[x * 3 + 1] = src[x * 4 + 1];
			dst[x * 3 + 2] = src[x * 4 + 2];
		}
	}
	FILE *f = _wfopen(path, L"wb");
	if (!f) {
		return false;
	}
	BITMAPFILEHEADER bf = {};
	bf.bfType = 0x4D42;
	bf.bfOffBits = sizeof(bf) + sizeof(bi);
	bf.bfSize = bf.bfOffBits + (DWORD)pixels.size();
	fwrite(&bf, sizeof(bf), 1, f);
	fwrite(&bi, sizeof(bi), 1, f);
	fwrite(pixels.data(), 1, pixels.size(), f);
	fclose(f);
	return true;
}

void press_capture_hotkey()
{
	/* rtx.captureHotKey defaults to Ctrl+Shift+Q; with
	 * rtx.captureShowMenuOnHotkey = False it starts a capture directly. */
	/* Remix refuses to capture while replacement assets are on ("Cannot begin
	 * capture while replacement assets are enabled", RTX 3080 run). */
	brb_set_config("rtx.enableReplacementAssets", "False");
	SetForegroundWindow(g_hwnd);
	INPUT in[6] = {};
	const WORD keys[3] = {VK_CONTROL, VK_SHIFT, 'Q'};
	for (int i = 0; i < 3; i++) {
		in[i].type = INPUT_KEYBOARD;
		in[i].ki.wVk = keys[i];
		in[5 - i].type = INPUT_KEYBOARD;
		in[5 - i].ki.wVk = keys[i];
		in[5 - i].ki.dwFlags = KEYEVENTF_KEYUP;
	}
	SendInput(3, in, sizeof(INPUT));
	Sleep(150);
	SendInput(3, in + 3, sizeof(INPUT));
	log("capture hotkey sent");
}

/* Default values as in the SDK's C++ wrapper (remix.h, 1.6.1), kept here in
 * plain C structs so the bridge needs only remix_c.h. */
remixapi_MaterialInfoOpaqueEXT default_opaque()
{
	remixapi_MaterialInfoOpaqueEXT m = {};
	m.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT;
	m.albedoConstant = {0.2f, 0.2f, 0.2f};
	m.opacityConstant = 1.0f;
	m.roughnessConstant = 0.5f;
	m.metallicConstant = 0.0f;
	m.thinFilmThickness_value = 200.0f;
	m.useDrawCallAlphaState = TRUE;
	m.alphaTestType = 7;
	m.enableDlssControlMask = TRUE;
	m.dlssControlMaskIntensity = 1.0f;
	m.dlssControlMaskToneStrength = 0.3f;
	m.dlssControlMaskStructuralStrength = 0.7f;
	return m;
}

remixapi_MaterialInfo default_material(uint64_t hash, void *ext)
{
	remixapi_MaterialInfo m = {};
	m.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO;
	m.pNext = ext;
	m.hash = hash;
	m.emissiveIntensity = 40.0f;
	m.spriteSheetRow = 1;
	m.spriteSheetCol = 1;
	m.filterMode = 1; /* linear */
	m.wrapModeU = 1;  /* repeat */
	m.wrapModeV = 1;
	return m;
}

remixapi_LightInfoSphereEXT default_sphere()
{
	remixapi_LightInfoSphereEXT l = {};
	l.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT;
	l.radius = 0.05f;
	l.shaping_hasvalue = FALSE;
	l.shaping_value.direction = {0.0f, 0.0f, 1.0f};
	l.shaping_value.coneAngleDegrees = 180.0f;
	l.volumetricRadianceScale = 1.0f;
	return l;
}

remixapi_HardcodedVertex vertex(float x, float y, float z, float nx, float ny, float nz)
{
	remixapi_HardcodedVertex v = {};
	v.position[0] = x;
	v.position[1] = y;
	v.position[2] = z;
	v.normal[0] = nx;
	v.normal[1] = ny;
	v.normal[2] = nz;
	v.color = 0xFFFFFFFF;
	return v;
}

}  /* namespace brb */

using namespace brb;

extern "C" {

static int triangle_test_impl(double seconds, const wchar_t *screenshot_path, int capture,
                              brb_triangle_result *r_result)
{
	brb_triangle_result res = {};
	if (g_status != BRB_STATUS_RUNNING) {
		if (r_result) {
			*r_result = res;
		}
		return -1;
	}

	/* Materials */
	remixapi_MaterialInfoOpaqueEXT floor_opaque = default_opaque();
	floor_opaque.albedoConstant = {0.6f, 0.6f, 0.6f};
	floor_opaque.roughnessConstant = 0.7f;
	remixapi_MaterialInfo floor_info = default_material(0xB6E0001, &floor_opaque);

	remixapi_MaterialInfoOpaqueEXT tri_opaque = default_opaque();
	tri_opaque.albedoConstant = {0.9f, 0.35f, 0.1f};
	tri_opaque.roughnessConstant = 0.35f;
	remixapi_MaterialInfo tri_info = default_material(0xB6E0002, &tri_opaque);

	remixapi_MaterialHandle floor_mat = nullptr, tri_mat = nullptr;
	remixapi_ErrorCode r = g_remix.CreateMaterial(&floor_info, &floor_mat);
	if (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		r = g_remix.CreateMaterial(&tri_info, &tri_mat);
	}

	/* Mesh: a 6 x 6 m floor and a 1.6 m triangle standing on it, facing -Y */
	const remixapi_HardcodedVertex floor_v[4] = {
	    vertex(-3, -3, 0, 0, 0, 1), vertex(3, -3, 0, 0, 0, 1),
	    vertex(3, 3, 0, 0, 0, 1), vertex(-3, 3, 0, 0, 0, 1)};
	const uint32_t floor_i[6] = {0, 1, 2, 0, 2, 3};
	const remixapi_HardcodedVertex tri_v[3] = {
	    vertex(-0.8f, 0, 0.2f, 0, -1, 0), vertex(0.8f, 0, 0.2f, 0, -1, 0), vertex(0, 0, 1.6f, 0, -1, 0)};
	const uint32_t tri_i[3] = {0, 1, 2};

	remixapi_MeshInfoSurfaceTriangles surfaces[2] = {};
	surfaces[0].vertices_values = floor_v;
	surfaces[0].vertices_count = 4;
	surfaces[0].indices_values = floor_i;
	surfaces[0].indices_count = 6;
	surfaces[0].material = floor_mat;
	surfaces[1].vertices_values = tri_v;
	surfaces[1].vertices_count = 3;
	surfaces[1].indices_values = tri_i;
	surfaces[1].indices_count = 3;
	surfaces[1].material = tri_mat;

	remixapi_MeshInfo mesh_info = {};
	mesh_info.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
	mesh_info.hash = 0xB6E0010;
	mesh_info.surfaces_values = surfaces;
	mesh_info.surfaces_count = 2;
	remixapi_MeshHandle mesh = nullptr;
	if (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		r = g_remix.CreateMesh(&mesh_info, &mesh);
	}

	/* Light: a 30 cm sphere light above and in front, to the right */
	remixapi_LightInfoSphereEXT sphere = default_sphere();
	sphere.position = {1.5f, -1.5f, 3.0f};
	sphere.radius = 0.3f;
	remixapi_LightInfo light_info = {};
	light_info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
	light_info.pNext = &sphere;
	light_info.hash = 0xB6E0020;
	light_info.radiance = {40.0f, 37.0f, 33.0f};
	remixapi_LightHandle light = nullptr;
	if (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		r = g_remix.CreateLight(&light_info, &light);
	}
	if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
		log("test scene creation failed: %u", (unsigned)r);
		res.last_error = (int)r;
	}

	/* Camera: 4 m in front of the triangle, eye height 1.2 m, looking at it */
	remixapi_CameraInfoParameterizedEXT cam = {};
	cam.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT;
	cam.position = {0.0f, -4.0f, 1.2f};
	cam.forward = {0.0f, 0.9950f, -0.0995f};
	cam.right = {1.0f, 0.0f, 0.0f};
	cam.up = {0.0f, 0.0995f, 0.9950f};
	cam.fovYInDegrees = 50.0f;
	cam.aspect = 16.0f / 9.0f;
	cam.nearPlane = 0.05f;
	cam.farPlane = 500.0f;
	remixapi_CameraInfo cam_info = {};
	cam_info.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
	cam_info.pNext = &cam;
	cam_info.type = REMIXAPI_CAMERA_TYPE_WORLD;

	remixapi_InstanceInfo inst = {};
	inst.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
	inst.mesh = mesh;
	inst.transform = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
	inst.doubleSided = TRUE;

	std::vector<double> deltas;
	const double start = now_ms();
	double last = start;
	bool capture_sent = false;
	while (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		pump_messages();
		RECT rc;
		GetClientRect(g_hwnd, &rc);
		const int w = rc.right - rc.left, h = rc.bottom - rc.top;
		cam.aspect = (w > 0 && h > 0) ? (float)w / (float)h : 16.0f / 9.0f;

		remixapi_ErrorCode pr;
		try {
			g_remix.SetupCamera(&cam_info);
			g_remix.DrawInstance(&inst);
			g_remix.DrawLightInstance(light);
			pr = g_remix.Present(nullptr);
		}
		catch (...) {
			log("the runtime raised an exception while drawing");
			pr = REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
			res.present_errors = 1000; /* stop the loop */
		}
		if (pr != REMIXAPI_ERROR_CODE_SUCCESS) {
			res.present_errors++;
			res.last_error = (int)pr;
		}
		res.frames++;

		const double t = now_ms();
		deltas.push_back(t - last);
		last = t;
		const double elapsed = (t - start) / 1000.0;
		if (capture && !capture_sent && elapsed >= seconds * 0.5) {
			press_capture_hotkey();
			capture_sent = true;
			res.capture_requested = 1;
		}
		if (elapsed >= seconds || res.present_errors > 30) {
			break;
		}
	}
	res.seconds = (last - start) / 1000.0;

	if (screenshot_path && screenshot_path[0] && res.frames > 0) {
		res.screenshot_saved = save_window_bmp(g_hwnd, screenshot_path) ? 1 : 0;
	}

	if (!deltas.empty()) {
		double sum = 0.0;
		for (double d : deltas) {
			sum += d;
		}
		std::sort(deltas.begin(), deltas.end());
		res.frame_ms_mean = sum / deltas.size();
		res.frame_ms_median = deltas[deltas.size() / 2];
		res.frame_ms_p95 = deltas[std::min(deltas.size() - 1, (size_t)(deltas.size() * 0.95))];
	}

	if (light) {
		g_remix.DestroyLight(light);
	}
	if (mesh) {
		g_remix.DestroyMesh(mesh);
	}
	if (tri_mat) {
		g_remix.DestroyMaterial(tri_mat);
	}
	if (floor_mat) {
		g_remix.DestroyMaterial(floor_mat);
	}
	log("triangle test: %d frames in %.2f s, median %.2f ms, present errors %d, screenshot %d, capture %d",
	    res.frames, res.seconds, res.frame_ms_median, res.present_errors, res.screenshot_saved,
	    res.capture_requested);
	if (r_result) {
		*r_result = res;
	}
	return (r == REMIXAPI_ERROR_CODE_SUCCESS && res.present_errors == 0) ? 0 : 1;
}

BRB_API int brb_triangle_test(double seconds, const wchar_t *screenshot_path, int capture,
                              brb_triangle_result *r_result)
{
	try {
		return triangle_test_impl(seconds, screenshot_path, capture, r_result);
	}
	catch (...) {
		log("the runtime raised an exception in the triangle test");
		if (r_result) {
			*r_result = brb_triangle_result();
			r_result->last_error = REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
		}
		return 1;
	}
}

}  /* extern "C" */
