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

namespace {

char g_mode[32] = "ok";
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

remixapi_ErrorCode REMIXAPI_CALL m_Startup(const remixapi_StartupInfo *info)
{
	const int valid = (info && info->sType == REMIXAPI_STRUCT_TYPE_STARTUP_INFO && IsWindow(info->hwnd)) ? 1 : 0;
	mlog("Startup hwnd_valid=%d", valid);
	if (strcmp(g_mode, "startup_fail") == 0) {
		return REMIXAPI_ERROR_CODE_HRESULT_DRIVER_VERSION_BELOW_MINIMUM;
	}
	return valid ? REMIXAPI_ERROR_CODE_SUCCESS : REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
}

remixapi_ErrorCode REMIXAPI_CALL m_Shutdown(void)
{
	mlog("Shutdown");
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

extern "C" REMIXAPI remixapi_ErrorCode REMIXAPI_CALL remixapi_InitializeLibrary(
    const remixapi_InitializeLibraryInfo *info, remixapi_Interface *out_result)
{
	const char *mode = getenv("BGE_REMIX_MOCK_MODE");
	if (mode && mode[0]) {
		strncpy(g_mode, mode, sizeof(g_mode) - 1);
	}
	const uint64_t v = info ? info->version : 0;
	mlog("InitializeLibrary api=%llu.%llu.%llu mode=%s",
	     (unsigned long long)REMIXAPI_VERSION_GET_MAJOR(v), (unsigned long long)REMIXAPI_VERSION_GET_MINOR(v),
	     (unsigned long long)REMIXAPI_VERSION_GET_PATCH(v), g_mode);
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
