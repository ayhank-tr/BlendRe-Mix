/*
 * BGE-Remix test double for the RTX Remix runtime (P1 tests, CI without a GPU).
 *
 * Built as d3d9.dll. It exports remixapi_InitializeLibrary like the real
 * runtime and fills the interface with functions that only record what they
 * were called with, one line per call, in the file named by BGE_REMIX_MOCK_LOG.
 *
 * BGE_REMIX_MOCK_MODE:
 *   ok            (default) everything succeeds
 *   version       InitializeLibrary reports an incompatible API version
 *   startup_fail  Startup fails as with a too old graphics driver
 *   throw         InitializeLibrary throws a C++ exception, as the real
 *                 runtime does for some failures
 *
 * Like the real runtime, Startup hooks the window procedure and Shutdown does
 * not restore it. A message reaching the hook after Shutdown is logged as
 * "WndProc after Shutdown" (where the real runtime crashes).
 *
 * Runtime layout: when a d3d9_x64.dll lies next to this d3d9.dll, this DLL
 * acts like Remix's real shim: it loads d3d9_x64.dll with a plain
 * LoadLibraryW (a C++ exception if that fails) and forwards to it. The
 * d3d9_x64.dll build (MOCK_IMPL) needs remixdep.dll from the same folder,
 * like the real runtime needs NRD, DLSS, CUDA and others.
 *
 * P2: the game modes' D3D9 objects (mock_d3d9.c) and the scene calls. Each
 * object created is logged with what it was created with; draws and lights
 * are counted per frame and logged with each Present ("Present draws=N
 * lights=M"); the camera is logged when it changes.
 *
 * GPL v2 or later, as Blender.
 */

#define NOMINMAX
#define REMIX_LIBRARY_EXPORTS
#define REMIX_WINAPI_NO_LIBRARY_LOADER
#include <remix/remix_c.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "mock_d3d9.h"

#ifdef MOCK_IMPL
extern "C" __declspec(dllimport) int remixdep_ping(void);
#endif

namespace {

char g_mode[32] = "ok";
bool g_shut = false;
WNDPROC g_prev_proc = nullptr;
HWND g_hooked = nullptr;
uintptr_t g_next_handle = 0x1000;

IDirect3D9Ex *g_d3d = nullptr;
IDirect3DDevice9Ex *g_dev = nullptr;
int g_frame_draws = 0;
int g_frame_lights = 0;
int g_copies = 0;
bool g_camera_new = false;
float g_camera[32];
float g_camera_logged[32];

void vlog(const char *fmt, va_list args)
{
	const char *path = getenv("BGE_REMIX_MOCK_LOG");
	if (!path || !path[0]) {
		return;
	}
	FILE *f = fopen(path, "ab");
	if (!f) {
		return;
	}
	vfprintf(f, fmt, args);
	fputs("\n", f);
	fclose(f);
}

void mlog(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vlog(fmt, args);
	va_end(args);
}

template<typename T> T handle()
{
	g_next_handle += 0x10;
	return reinterpret_cast<T>(g_next_handle);
}

LRESULT CALLBACK mock_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (g_shut) {
		mlog("WndProc after Shutdown msg=0x%04x", msg);
	}
	return CallWindowProcW(g_prev_proc, hwnd, msg, wparam, lparam);
}

void hook_window(HWND hwnd)
{
	g_prev_proc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)mock_window_proc);
	g_hooked = hwnd;
	g_shut = false;
}

remixapi_ErrorCode REMIXAPI_CALL m_Startup(const remixapi_StartupInfo *info)
{
	const int valid = (info && info->sType == REMIXAPI_STRUCT_TYPE_STARTUP_INFO && IsWindow(info->hwnd)) ? 1 : 0;
	mlog("Startup hwnd_valid=%d", valid);
	if (valid) {
		/* hook the window, like the real runtime (also before a failed start) */
		hook_window(info->hwnd);
	}
	if (strcmp(g_mode, "startup_fail") == 0) {
		g_shut = true;
		return REMIXAPI_ERROR_CODE_HRESULT_DRIVER_VERSION_BELOW_MINIMUM;
	}
	return valid ? REMIXAPI_ERROR_CODE_SUCCESS : REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}

remixapi_ErrorCode REMIXAPI_CALL m_Shutdown(void)
{
	if (g_dev) { /* registered: both are alive */
		mlog("Copies %d", g_copies);
		mock_release_all(g_dev, g_d3d);
		g_dev = nullptr;
		g_d3d = nullptr;
	}
	mlog("Shutdown");
	g_shut = true; /* the hook stays, as with the real runtime */
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

template<typename T> const T *find_ext(const void *pnext, remixapi_StructType type)
{
	struct Head {
		remixapi_StructType sType;
		void *pNext;
	};
	for (const Head *h = (const Head *)pnext; h; h = (const Head *)h->pNext) {
		if (h->sType == type) {
			return (const T *)h;
		}
	}
	return nullptr;
}

remixapi_ErrorCode REMIXAPI_CALL m_CreateMaterial(const remixapi_MaterialInfo *info, remixapi_MaterialHandle *out)
{
	if (!info) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	const remixapi_MaterialInfoOpaqueEXT *o = find_ext<remixapi_MaterialInfoOpaqueEXT>(
	    info->pNext, REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT);
	if (o) {
		mlog("CreateMaterial hash=%llx albedo=%.4f,%.4f,%.4f opacity=%.4f roughness=%.4f metallic=%.4f "
		     "emissive=%.4f,%.4f,%.4f intensity=%.4f blend=%d",
		     (unsigned long long)info->hash, o->albedoConstant.x, o->albedoConstant.y, o->albedoConstant.z,
		     o->opacityConstant, o->roughnessConstant, o->metallicConstant, info->emissiveColorConstant.x,
		     info->emissiveColorConstant.y, info->emissiveColorConstant.z, info->emissiveIntensity,
		     o->blendType_hasvalue ? 1 : 0);
	}
	else {
		mlog("CreateMaterial hash=%llx", (unsigned long long)info->hash);
	}
	*out = handle<remixapi_MaterialHandle>();
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DestroyMaterial(remixapi_MaterialHandle)
{
	mlog("DestroyMaterial");
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_CreateMesh(const remixapi_MeshInfo *info, remixapi_MeshHandle *out)
{
	if (!info) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	unsigned long long verts = 0, indices = 0;
	float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
	int no_material = 0;
	/* winding against the vertex normals: Remix wants clockwise front faces */
	int cw = 0, ccw = 0;
	for (uint32_t s = 0; s < info->surfaces_count; s++) {
		const remixapi_MeshInfoSurfaceTriangles &t = info->surfaces_values[s];
		for (uint64_t i = 0; i + 2 < t.indices_count; i += 3) {
			const uint32_t a = t.indices_values[i], b = t.indices_values[i + 1], c = t.indices_values[i + 2];
			if (a >= t.vertices_count || b >= t.vertices_count || c >= t.vertices_count) {
				continue;
			}
			const float *p0 = t.vertices_values[a].position, *p1 = t.vertices_values[b].position,
			            *p2 = t.vertices_values[c].position;
			const float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
			const float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
			const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
			const float *vn = t.vertices_values[a].normal;
			const float d = n[0] * vn[0] + n[1] * vn[1] + n[2] * vn[2];
			if (d < 0.0f) {
				cw++;
			}
			else if (d > 0.0f) {
				ccw++;
			}
		}
		verts += t.vertices_count;
		indices += t.indices_count;
		no_material += t.material ? 0 : 1;
		for (uint64_t i = 0; i < t.vertices_count; i++) {
			for (int k = 0; k < 3; k++) {
				lo[k] = std::min(lo[k], t.vertices_values[i].position[k]);
				hi[k] = std::max(hi[k], t.vertices_values[i].position[k]);
			}
		}
	}
	mlog("CreateMesh hash=%llx surfaces=%u verts=%llu indices=%llu no_material=%d winding=%s "
	     "bbox=%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
	     (unsigned long long)info->hash, info->surfaces_count, verts, indices, no_material,
	     (cw && !ccw) ? "cw" : (ccw && !cw) ? "ccw" : "mixed",
	     lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
	*out = handle<remixapi_MeshHandle>();
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DestroyMesh(remixapi_MeshHandle)
{
	mlog("DestroyMesh");
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_SetupCamera(const remixapi_CameraInfo *info)
{
	if (!info) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	if (find_ext<remixapi_CameraInfoParameterizedEXT>(info->pNext,
	                                                   REMIXAPI_STRUCT_TYPE_CAMERA_INFO_PARAMETERIZED_EXT))
	{
		return REMIXAPI_ERROR_CODE_SUCCESS; /* P1's triangle test */
	}
	memcpy(g_camera, info->view, sizeof(float) * 16);
	memcpy(g_camera + 16, info->projection, sizeof(float) * 16);
	g_camera_new = true;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DrawInstance(const remixapi_InstanceInfo *info)
{
	if (!info || !info->mesh) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	g_frame_draws++;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_CreateLight(const remixapi_LightInfo *info, remixapi_LightHandle *out)
{
	if (!info) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	const unsigned long long hash = (unsigned long long)info->hash;
	const remixapi_Float3D &rad = info->radiance;
	if (auto *l = find_ext<remixapi_LightInfoSphereEXT>(info->pNext, REMIXAPI_STRUCT_TYPE_LIGHT_INFO_SPHERE_EXT)) {
		mlog("CreateLight hash=%llx type=sphere pos=%.4f,%.4f,%.4f radius=%.4f shaped=%d dir=%.4f,%.4f,%.4f "
		     "cone=%.4f softness=%.4f radiance=%.4f,%.4f,%.4f",
		     hash, l->position.x, l->position.y, l->position.z, l->radius, l->shaping_hasvalue ? 1 : 0,
		     l->shaping_value.direction.x, l->shaping_value.direction.y, l->shaping_value.direction.z,
		     l->shaping_value.coneAngleDegrees, l->shaping_value.coneSoftness, rad.x, rad.y, rad.z);
	}
	else if (auto *l = find_ext<remixapi_LightInfoRectEXT>(info->pNext, REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT)) {
		mlog("CreateLight hash=%llx type=rect pos=%.4f,%.4f,%.4f size=%.4f,%.4f dir=%.4f,%.4f,%.4f "
		     "radiance=%.4f,%.4f,%.4f",
		     hash, l->position.x, l->position.y, l->position.z, l->xSize, l->ySize, l->direction.x,
		     l->direction.y, l->direction.z, rad.x, rad.y, rad.z);
	}
	else if (auto *l = find_ext<remixapi_LightInfoDistantEXT>(info->pNext,
	                                                         REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT))
	{
		mlog("CreateLight hash=%llx type=distant dir=%.4f,%.4f,%.4f angle=%.4f radiance=%.4f,%.4f,%.4f",
		     hash, l->direction.x, l->direction.y, l->direction.z, l->angularDiameterDegrees, rad.x, rad.y, rad.z);
	}
	else if (auto *l = find_ext<remixapi_LightInfoDomeEXT>(info->pNext, REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT)) {
		const bool tex = l->colorTexture && GetFileAttributesW(l->colorTexture) != INVALID_FILE_ATTRIBUTES;
		mlog("CreateLight hash=%llx type=dome texture=%d radiance=%.4f,%.4f,%.4f", hash, tex ? 1 : 0,
		     rad.x, rad.y, rad.z);
	}
	else {
		mlog("CreateLight hash=%llx type=unknown", hash);
	}
	*out = handle<remixapi_LightHandle>();
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DestroyLight(remixapi_LightHandle)
{
	mlog("DestroyLight");
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DrawLightInstance(remixapi_LightHandle light)
{
	if (!light) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	g_frame_lights++;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_SetConfigVariable(const char *key, const char *value)
{
	mlog("Config %s=%s", key ? key : "", value ? value : "");
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_Present(const remixapi_PresentInfo *)
{
	if (g_camera_new && memcmp(g_camera, g_camera_logged, sizeof(g_camera)) != 0) {
		char buf[1024];
		int n = snprintf(buf, sizeof(buf), "Camera view=");
		for (int i = 0; i < 32; i++) {
			n += snprintf(buf + n, sizeof(buf) - n, "%s%.7g", (i == 0 || i == 16) ? "" : ",", g_camera[i]);
			if (i == 15) {
				n += snprintf(buf + n, sizeof(buf) - n, " proj=");
			}
		}
		mlog("%s", buf);
		memcpy(g_camera_logged, g_camera, sizeof(g_camera));
	}
	g_camera_new = false;
	mlog("Present draws=%d lights=%d", g_frame_draws, g_frame_lights);
	g_frame_draws = 0;
	g_frame_lights = 0;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_dxvk_CreateD3D9(remixapi_Bool editor_mode, IDirect3D9Ex **out)
{
	mlog("CreateD3D9 editor=%d", editor_mode ? 1 : 0);
	if (!out) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	g_d3d = mock_create_d3d9();
	*out = g_d3d;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_dxvk_RegisterD3D9Device(IDirect3DDevice9Ex *device)
{
	const int ok = mock_is_device(device);
	mlog("RegisterD3D9Device ok=%d", ok);
	if (!ok) {
		return REMIXAPI_ERROR_CODE_REGISTERING_NON_REMIX_D3D9_DEVICE;
	}
	g_dev = device;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

}  /* namespace */

extern "C" void mock_log(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vlog(fmt, args);
	va_end(args);
}

extern "C" int mock_mode_is(const char *mode)
{
	return strcmp(g_mode, mode) == 0;
}

extern "C" void mock_hook_window(HWND hwnd)
{
	hook_window(hwnd);
}

extern "C" void mock_count_copy(void)
{
	g_copies++;
}

#ifndef MOCK_IMPL
/* The shim part: forward to d3d9_x64.dll when it is there. */
static bool shim_forward(const remixapi_InitializeLibraryInfo *info, remixapi_Interface *out_result,
                         remixapi_ErrorCode *r_code)
{
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                   reinterpret_cast<LPCWSTR>(&shim_forward), &self);
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(self, path, MAX_PATH);
	std::wstring impl(path);
	impl = impl.substr(0, impl.find_last_of(L"\\/") + 1) + L"d3d9_x64.dll";
	if (GetFileAttributesW(impl.c_str()) == INVALID_FILE_ATTRIBUTES) {
		return false;
	}
	mlog("Shim loads d3d9_x64.dll");
	HMODULE h = LoadLibraryW(impl.c_str()); /* plain, as Remix's shim does */
	if (h == nullptr) {
		mlog("Shim failed to load the implementation (error %lu)", GetLastError());
		throw std::runtime_error("d3d9 shim: failed to load implementation DLL");
	}
	typedef remixapi_ErrorCode(REMIXAPI_CALL * init_fn)(const remixapi_InitializeLibraryInfo *, remixapi_Interface *);
	init_fn fn = reinterpret_cast<init_fn>(GetProcAddress(h, "remixapi_InitializeLibrary"));
	*r_code = fn ? fn(info, out_result) : REMIXAPI_ERROR_CODE_GET_PROC_ADDRESS_FAILURE;
	return true;
}
#endif

extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_InitializeLibrary(
    const remixapi_InitializeLibraryInfo *info, remixapi_Interface *out_result)
{
#ifndef MOCK_IMPL
	{
		remixapi_ErrorCode code;
		if (shim_forward(info, out_result, &code)) {
			return code;
		}
	}
#else
	mlog("Implementation loaded, dependency says %d", remixdep_ping());
#endif
	const char *mode = getenv("BGE_REMIX_MOCK_MODE");
	if (mode && mode[0]) {
		strncpy(g_mode, mode, sizeof(g_mode) - 1);
	}
	const uint64_t v = info ? info->version : 0;
	mlog("InitializeLibrary api=%llu.%llu.%llu mode=%s",
	     (unsigned long long)REMIXAPI_VERSION_GET_MAJOR(v), (unsigned long long)REMIXAPI_VERSION_GET_MINOR(v),
	     (unsigned long long)REMIXAPI_VERSION_GET_PATCH(v), g_mode);
	if (strcmp(g_mode, "throw") == 0) {
		throw std::runtime_error("mock runtime: simulated failure");
	}
	if (!info || !out_result || info->sType != REMIXAPI_STRUCT_TYPE_INITIALIZE_LIBRARY_INFO) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	/* Like the real runtime: the major and minor versions must match. */
	if (strcmp(g_mode, "version") == 0 ||
	    REMIXAPI_VERSION_GET_MAJOR(v) != REMIXAPI_VERSION_MAJOR ||
	    REMIXAPI_VERSION_GET_MINOR(v) != REMIXAPI_VERSION_MINOR)
	{
		return REMIXAPI_ERROR_CODE_INCOMPATIBLE_VERSION;
	}
	remixapi_Interface r = {};
	r.Shutdown = m_Shutdown;
	r.CreateMaterial = m_CreateMaterial;
	r.DestroyMaterial = m_DestroyMaterial;
	r.CreateMesh = m_CreateMesh;
	r.DestroyMesh = m_DestroyMesh;
	r.SetupCamera = m_SetupCamera;
	r.DrawInstance = m_DrawInstance;
	r.CreateLight = m_CreateLight;
	r.DestroyLight = m_DestroyLight;
	r.DrawLightInstance = m_DrawLightInstance;
	r.SetConfigVariable = m_SetConfigVariable;
	r.Startup = m_Startup;
	r.Present = m_Present;
	r.dxvk_CreateD3D9 = m_dxvk_CreateD3D9;
	r.dxvk_RegisterD3D9Device = m_dxvk_RegisterD3D9Device;
	*out_result = r;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}
