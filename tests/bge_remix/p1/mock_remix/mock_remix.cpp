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
 * GPL v2 or later, as Blender.
 */

#define REMIX_LIBRARY_EXPORTS
#define REMIX_WINAPI_NO_LIBRARY_LOADER
#include <remix/remix_c.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#ifdef MOCK_IMPL
extern "C" __declspec(dllimport) int remixdep_ping(void);
#endif

namespace {

char g_mode[32] = "ok";
bool g_shut = false;
WNDPROC g_prev_proc = nullptr;
uintptr_t g_next_handle = 0x1000;

void mlog(const char *fmt, ...)
{
	const char *path = getenv("BGE_REMIX_MOCK_LOG");
	if (!path || !path[0]) {
		return;
	}
	FILE *f = fopen(path, "ab");
	if (!f) {
		return;
	}
	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);
	fputs("\n", f);
	fclose(f);
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

remixapi_ErrorCode REMIXAPI_CALL m_Startup(const remixapi_StartupInfo *info)
{
	const int valid = (info && info->sType == REMIXAPI_STRUCT_TYPE_STARTUP_INFO && IsWindow(info->hwnd)) ? 1 : 0;
	mlog("Startup hwnd_valid=%d", valid);
	if (valid) {
		/* hook the window, like the real runtime (also before a failed start) */
		g_prev_proc = (WNDPROC)SetWindowLongPtrW(info->hwnd, GWLP_WNDPROC, (LONG_PTR)mock_window_proc);
		g_shut = false;
	}
	if (strcmp(g_mode, "startup_fail") == 0) {
		g_shut = true;
		return REMIXAPI_ERROR_CODE_HRESULT_DRIVER_VERSION_BELOW_MINIMUM;
	}
	return valid ? REMIXAPI_ERROR_CODE_SUCCESS : REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}

remixapi_ErrorCode REMIXAPI_CALL m_Shutdown(void)
{
	mlog("Shutdown");
	g_shut = true; /* the hook stays, as with the real runtime */
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_CreateMaterial(const remixapi_MaterialInfo *info, remixapi_MaterialHandle *out)
{
	mlog("CreateMaterial hash=%llx", info ? (unsigned long long)info->hash : 0ull);
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
	mlog("CreateMesh surfaces=%u", info ? info->surfaces_count : 0u);
	*out = handle<remixapi_MeshHandle>();
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DestroyMesh(remixapi_MeshHandle)
{
	mlog("DestroyMesh");
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_SetupCamera(const remixapi_CameraInfo *)
{
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DrawInstance(const remixapi_InstanceInfo *)
{
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_CreateLight(const remixapi_LightInfo *info, remixapi_LightHandle *out)
{
	mlog("CreateLight hash=%llx", info ? (unsigned long long)info->hash : 0ull);
	*out = handle<remixapi_LightHandle>();
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DestroyLight(remixapi_LightHandle)
{
	mlog("DestroyLight");
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DrawLightInstance(remixapi_LightHandle)
{
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_SetConfigVariable(const char *key, const char *value)
{
	mlog("Config %s=%s", key ? key : "", value ? value : "");
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_Present(const remixapi_PresentInfo *)
{
	mlog("Present");
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

}  /* namespace */

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
	*out_result = r;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}
