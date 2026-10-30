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
 * P4: meshes are kept (positions, triangles, bone weights) so that draws
 * can be checked: skinned meshes are decoded the way Remix 1.6.1 reads its
 * weight buffer (one float of stride per vertex, the last weight implied).
 * Present also counts skinned draws, bones, emitters, particle-category
 * draws and highlighted ids. Picking requests are answered at the next
 * Present: the first drawn object whose world bounds the ray meets.
 *   BGE_REMIX_MOCK_DRAWS_FRAME=a,b logs every draw of those frames ("Draw ...")
 *   BGE_REMIX_MOCK_SKIN_FRAMES=a,b logs the skinned positions of skinned
 *                                  draws on those frames ("Skinned ...")
 *
 * GPL v2 or later, as Blender.
 */

#define NOMINMAX
#define REMIX_LIBRARY_EXPORTS
#define REMIX_WINAPI_NO_LIBRARY_LOADER
#include <remix/remix_c.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

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
int g_frame_blended = 0;
int g_frame_lights = 0;
int g_copies = 0;
bool g_camera_new = false;
float g_camera[32];
float g_camera_logged[32];

/* P4 */
struct MockSurface {
	std::vector<float> pos;          /* 3 per vertex slot */
	std::vector<uint32_t> indices;
	uint32_t bpv = 0;
	std::vector<float> weights;
	std::vector<uint32_t> bones;
};
struct MockMesh {
	unsigned long long hash = 0;
	std::vector<MockSurface> surfaces;
};
std::map<uintptr_t, MockMesh> g_meshes;
struct MockDraw {
	uint32_t pick_id;
	float lo[3], hi[3]; /* world bounds */
};
std::vector<MockDraw> g_draws;
int g_present_count = 0;
unsigned long long g_frame_bonehash = 1469598103934665603ull;
int g_frame_skinned = 0, g_frame_bones = 0, g_frame_emitters = 0, g_frame_particles = 0, g_frame_highlighted = 0;
bool g_pick_pending = false;
remixapi_Rect2D g_pick_rect;
PFN_remixapi_pick_RequestObjectPickingUserCallback g_pick_cb = nullptr;
void *g_pick_user = nullptr;
std::map<unsigned long long, std::string> g_emitters_logged;

bool frame_listed(const char *env, int frame)
{
	const char *v = getenv(env);
	if (!v || !v[0]) {
		return false;
	}
	std::string list = std::string(",") + v + ",";
	return list.find("," + std::to_string(frame) + ",") != std::string::npos;
}

/* Remix's rows (3 x 4) times a point */
void xform(const remixapi_Transform &t, const float p[3], float r[3])
{
	for (int i = 0; i < 3; i++) {
		r[i] = t.matrix[i][0] * p[0] + t.matrix[i][1] * p[1] + t.matrix[i][2] * p[2] + t.matrix[i][3];
	}
}

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

static bool g_external = false;
static int g_external_calls = 0;
/* P5: direct mode's picture size, changed by Present like Remix 1.6.1 does
 * (rtx_remix_api.cpp: only when the window's width and height both differ) */
static HWND g_ext_window = nullptr;
static int g_ext_w = 0, g_ext_h = 0;
static int g_ext_resets = 0;

static void client_size(HWND hwnd, int &w, int &h)
{
	RECT r = {};
	GetClientRect(hwnd, &r);
	w = (int)(r.right - r.left);
	h = (int)(r.bottom - r.top);
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
	if (info && info->forceNoVkSwapchain) {
		/* P4.8 direct mode: no swapchain; the picture goes out by dxvk_GetExternalSwapchain */
		mlog("NoVkSwapchain requested editor=%d srgb_off=%d", info->editorModeEnabled ? 1 : 0,
		     info->disableSrgbConversionForOutput ? 1 : 0);
		g_external = true;
		g_ext_window = info->hwnd;
		if (IsWindow(info->hwnd)) {
			client_size(info->hwnd, g_ext_w, g_ext_h);
			mlog("External size %dx%d", g_ext_w, g_ext_h);
		}
	}
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

/* P4.8: fake Vulkan handles; the count tells the tests how often the game asked */
remixapi_ErrorCode REMIXAPI_CALL m_dxvk_GetExternalSwapchain(uint64_t *out_image, uint64_t *out_done,
                                                            uint64_t *out_resume)
{
	if (!out_image || !out_done || !out_resume) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	if (!g_external) {
		return REMIXAPI_ERROR_CODE_GENERAL_FAILURE;
	}
	g_external_calls++;
	*out_image = 0x1111;
	*out_done = 0x2222;
	*out_resume = 0x3333;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_Shutdown(void)
{
	if (g_external) {
		mlog("ExternalSwapchain calls=%d resets=%d", g_external_calls, g_ext_resets);
	}
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

/* a texture path for the log: "-" when there is none (no spaces expected) */
static std::string tex_path(const wchar_t *p)
{
	if (!p || !p[0]) {
		return "-";
	}
	std::string out;
	for (const wchar_t *c = p; *c; c++) {
		out += (*c < 128 && *c != L' ') ? (char)*c : '_';
	}
	return out;
}

remixapi_ErrorCode REMIXAPI_CALL m_CreateMaterial(const remixapi_MaterialInfo *info, remixapi_MaterialHandle *out)
{
	if (!info) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	const remixapi_MaterialInfoOpaqueEXT *o = find_ext<remixapi_MaterialInfoOpaqueEXT>(
	    info->pNext, REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_EXT);
	const remixapi_MaterialInfoTranslucentEXT *t = find_ext<remixapi_MaterialInfoTranslucentEXT>(
	    info->pNext, REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT);
	const remixapi_MaterialInfoOpaqueSubsurfaceEXT *ss = find_ext<remixapi_MaterialInfoOpaqueSubsurfaceEXT>(
	    info->pNext, REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT);
	const std::string common =
	    " albedo_tex=" + tex_path(info->albedoTexture) + " normal_tex=" + tex_path(info->normalTexture) +
	    " emissive_tex=" + tex_path(info->emissiveTexture) + " filter=" + std::to_string(info->filterMode) +
	    " wrap=" + std::to_string(info->wrapModeU);
	if (o) {
		mlog("CreateMaterial hash=%llx type=opaque albedo=%.4f,%.4f,%.4f opacity=%.4f roughness=%.4f metallic=%.4f "
		     "emissive=%.4f,%.4f,%.4f intensity=%.4f blend=%d alphatest=%d alpharef=%d subsurface=%d "
		     "sss_radius=%.4f,%.4f,%.4f roughness_tex=%s metallic_tex=%s%s blendtype=%d",
		     (unsigned long long)info->hash, o->albedoConstant.x, o->albedoConstant.y, o->albedoConstant.z,
		     o->opacityConstant, o->roughnessConstant, o->metallicConstant, info->emissiveColorConstant.x,
		     info->emissiveColorConstant.y, info->emissiveColorConstant.z, info->emissiveIntensity,
		     o->blendType_hasvalue ? 1 : 0, (!o->useDrawCallAlphaState && o->alphaTestType != 7) ? 1 : 0,
		     (int)o->alphaReferenceValue, ss ? 1 : 0, ss ? ss->subsurfaceRadius.x : 0.0f,
		     ss ? ss->subsurfaceRadius.y : 0.0f, ss ? ss->subsurfaceRadius.z : 0.0f,
		     tex_path(o->roughnessTexture).c_str(), tex_path(o->metallicTexture).c_str(), common.c_str(),
		     o->blendType_hasvalue ? o->blendType_value : -1);
	}
	else if (t) {
		mlog("CreateMaterial hash=%llx type=translucent ior=%.4f transmittance=%.4f,%.4f,%.4f distance=%.4f "
		     "emissive=%.4f,%.4f,%.4f intensity=%.4f%s",
		     (unsigned long long)info->hash, t->refractiveIndex, t->transmittanceColor.x, t->transmittanceColor.y,
		     t->transmittanceColor.z, t->transmittanceMeasurementDistance, info->emissiveColorConstant.x,
		     info->emissiveColorConstant.y, info->emissiveColorConstant.z, info->emissiveIntensity,
		     common.c_str());
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
	/* P4: keep it; decode bone weights as Remix 1.6.1 reads them */
	MockMesh mesh;
	mesh.hash = (unsigned long long)info->hash;
	uint32_t bpv = 0, max_bone = 0;
	int skinned = 0, bad_weights = 0, referenced = 0;
	float wmax_err = 0.0f;
	for (uint32_t s = 0; s < info->surfaces_count; s++) {
		const remixapi_MeshInfoSurfaceTriangles &t = info->surfaces_values[s];
		MockSurface ms;
		ms.pos.resize((size_t)t.vertices_count * 3);
		for (uint64_t i = 0; i < t.vertices_count; i++) {
			memcpy(&ms.pos[i * 3], t.vertices_values[i].position, sizeof(float) * 3);
		}
		ms.indices.assign(t.indices_values, t.indices_values + t.indices_count);
		if (t.skinning_hasvalue) {
			skinned = 1;
			const remixapi_MeshInfoSkinning &k = t.skinning_value;
			ms.bpv = k.bonesPerVertex;
			bpv = std::max(bpv, k.bonesPerVertex);
			if (k.blendWeights_count != k.bonesPerVertex * t.vertices_count ||
			    k.blendIndices_count != k.bonesPerVertex * t.vertices_count)
			{
				bad_weights++; /* Remix's documented sizes */
			}
			ms.weights.assign(k.blendWeights_values, k.blendWeights_values + k.blendWeights_count);
			ms.bones.assign(k.blendIndices_values, k.blendIndices_values + k.blendIndices_count);
			std::vector<char> used(t.vertices_count, 0);
			for (uint32_t v : ms.indices) {
				if (v < t.vertices_count) {
					used[v] = 1;
				}
			}
			for (uint64_t v = 0; v < t.vertices_count; v++) {
				if (!used[v]) {
					continue;
				}
				referenced++;
				float sum = 0.0f;
				for (uint32_t j = 0; j + 1 < ms.bpv; j++) {
					const float w = (v + j < ms.weights.size()) ? ms.weights[v + j] : 0.0f;
					if (w < -1e-4f || w > 1.0001f) {
						bad_weights++;
					}
					sum += w;
				}
				wmax_err = std::max(wmax_err, std::max(sum - 1.0f, 0.0f));
				for (uint32_t j = 0; j < ms.bpv; j++) {
					max_bone = std::max(max_bone, ms.bones[v * ms.bpv + j]);
				}
			}
		}
		mesh.surfaces.push_back(std::move(ms));
	}
	mlog("CreateMesh hash=%llx surfaces=%u verts=%llu indices=%llu no_material=%d winding=%s "
	     "bbox=%.4f,%.4f,%.4f,%.4f,%.4f,%.4f skinned=%d bpv=%u referenced=%d max_bone=%u bad_weights=%d "
	     "weight_overflow=%.5f",
	     (unsigned long long)info->hash, info->surfaces_count, verts, indices, no_material,
	     (cw && !ccw) ? "cw" : (ccw && !cw) ? "ccw" : "mixed",
	     lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], skinned, bpv, referenced, max_bone, bad_weights, wmax_err);
	*out = handle<remixapi_MeshHandle>();
	g_meshes[(uintptr_t)*out] = std::move(mesh);
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_DestroyMesh(remixapi_MeshHandle h)
{
	g_meshes.erase((uintptr_t)h);
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
	if (const remixapi_InstanceInfoBlendEXT *b = find_ext<remixapi_InstanceInfoBlendEXT>(
	        info->pNext, REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT))
	{
		g_frame_blended += b->alphaBlendEnabled ? 1 : 0;
	}
	/* P4 */
	const auto *pick = find_ext<remixapi_InstanceInfoObjectPickingEXT>(
	    info->pNext, REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT);
	const auto *bones = find_ext<remixapi_InstanceInfoBoneTransformsEXT>(
	    info->pNext, REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT);
	const auto *ps = find_ext<remixapi_InstanceInfoParticleSystemEXT>(
	    info->pNext, REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_PARTICLE_SYSTEM_EXT);
	if (info->categoryFlags & REMIXAPI_INSTANCE_CATEGORY_BIT_PARTICLE) {
		g_frame_particles++;
	}
	if (bones) {
		g_frame_skinned++;
		g_frame_bones += (int)bones->boneTransforms_count;
		const unsigned char *b = (const unsigned char *)bones->boneTransforms_values;
		for (size_t i = 0; i < sizeof(remixapi_Transform) * bones->boneTransforms_count; i++) {
			g_frame_bonehash = (g_frame_bonehash ^ b[i]) * 1099511628211ull;
		}
	}
	auto mit = g_meshes.find((uintptr_t)info->mesh);
	const MockMesh *mesh = (mit != g_meshes.end()) ? &mit->second : nullptr;
	if (ps) {
		g_frame_emitters++;
		char buf[640];
		snprintf(buf, sizeof(buf),
		         "max=%u rate=%.3f life=%.3f,%.3f speed=%.3f cone=%.3f motion=%.3f gravity=%.3f drag=%.3f "
		         "size=%.3f,%.3f end_size=%.3f color=%.3f,%.3f,%.3f,%.3f end_alpha=%.3f rot=%.3f collide=%d "
		         "restitution=%.3f align=%d hide=%d billboard=%d cats=%u",
		         ps->maxNumParticles, ps->spawnRatePerSecond, ps->minTimeToLive, ps->maxTimeToLive,
		         ps->initialVelocityFromNormal, ps->initialVelocityConeAngleDegrees, ps->initialVelocityFromMotion,
		         ps->gravityForce, ps->dragCoefficient,
		         ps->minSize.numberElements ? ps->minSize.pData[0].x : -1.0f,
		         ps->maxSize.numberElements ? ps->maxSize.pData[0].x : -1.0f,
		         ps->minSize.numberElements > 1 ? ps->minSize.pData[1].x : -1.0f,
		         ps->minColor.numberElements ? ps->minColor.pData[0].x : -1.0f,
		         ps->minColor.numberElements ? ps->minColor.pData[0].y : -1.0f,
		         ps->minColor.numberElements ? ps->minColor.pData[0].z : -1.0f,
		         ps->minColor.numberElements ? ps->minColor.pData[0].w : -1.0f,
		         ps->minColor.numberElements > 1 ? ps->minColor.pData[1].w : -1.0f,
		         ps->maxRotationSpeed.numberElements ? ps->maxRotationSpeed.pData[0] : 0.0f,
		         ps->enableCollisionDetection ? 1 : 0, ps->collisionRestitution, ps->alignParticlesToVelocity ? 1 : 0,
		         ps->hideEmitter ? 1 : 0, (int)ps->billboardType, (unsigned)info->categoryFlags);
		const unsigned long long key = mesh ? mesh->hash : 0;
		if (g_emitters_logged[key] != buf) {
			mlog("Emitter mesh=%llx %s", key, buf);
			g_emitters_logged[key] = buf;
		}
	}
	/* world bounds of the drawn triangles (skinned: as Remix skins them) */
	MockDraw d = {pick ? pick->objectPickingValue : 0, {1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
	const bool log_skin = bones && frame_listed("BGE_REMIX_MOCK_SKIN_FRAMES", g_present_count);
	std::string skinned_text;
	int skinned_n = 0;
	if (mesh) {
		for (const MockSurface &ms : mesh->surfaces) {
			std::vector<char> seen(ms.pos.size() / 3, 0);
			for (uint32_t v : ms.indices) {
				if ((size_t)v * 3 + 2 >= ms.pos.size()) {
					continue;
				}
				float p[3] = {ms.pos[v * 3], ms.pos[v * 3 + 1], ms.pos[v * 3 + 2]};
				if (bones && ms.bpv) {
					float acc[3] = {0, 0, 0}, rest = 1.0f;
					for (uint32_t j = 0; j < ms.bpv; j++) {
						float w = rest;
						if (j + 1 < ms.bpv) {
							w = (v + j < ms.weights.size()) ? ms.weights[v + j] : 0.0f;
							rest -= w;
						}
						const uint32_t b = ms.bones[(size_t)v * ms.bpv + j];
						if (w > 0.0f && b < bones->boneTransforms_count) {
							float q[3];
							xform(bones->boneTransforms_values[b], p, q);
							for (int k = 0; k < 3; k++) {
								acc[k] += q[k] * w;
							}
						}
					}
					memcpy(p, acc, sizeof(p));
				}
				float w[3];
				xform(info->transform, p, w);
				for (int k = 0; k < 3; k++) {
					d.lo[k] = std::min(d.lo[k], w[k]);
					d.hi[k] = std::max(d.hi[k], w[k]);
				}
				if (log_skin && !seen[v]) {
					seen[v] = 1;
					char buf[64];
					snprintf(buf, sizeof(buf), "%s%.4f,%.4f,%.4f", skinned_n ? ";" : "", w[0], w[1], w[2]);
					skinned_text += buf;
					skinned_n++;
				}
			}
		}
	}
	g_draws.push_back(d);
	if (log_skin) {
		mlog("Skinned frame=%d mesh=%llx pick=%u n=%d pos=%s", g_present_count, mesh ? mesh->hash : 0ULL,
		     d.pick_id, skinned_n, skinned_text.c_str());
	}
	if (frame_listed("BGE_REMIX_MOCK_DRAWS_FRAME", g_present_count)) {
		const remixapi_Transform &t = info->transform;
		mlog("Draw frame=%d mesh=%llx pick=%u cats=%u bones=%u double=%d m=%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,"
		     "%.5f,%.5f,%.5f,%.5f bounds=%.4f,%.4f,%.4f,%.4f,%.4f,%.4f",
		     g_present_count, mesh ? mesh->hash : 0ULL, d.pick_id, (unsigned)info->categoryFlags,
		     bones ? bones->boneTransforms_count : 0u, info->doubleSided ? 1 : 0, t.matrix[0][0], t.matrix[0][1],
		     t.matrix[0][2], t.matrix[0][3], t.matrix[1][0], t.matrix[1][1], t.matrix[1][2], t.matrix[1][3],
		     t.matrix[2][0], t.matrix[2][1], t.matrix[2][2], t.matrix[2][3], d.lo[0], d.lo[1], d.lo[2], d.hi[0],
		     d.hi[1], d.hi[2]);
	}
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
		     "cone=%.4f softness=%.4f radiance=%.4f,%.4f,%.4f volumetric=%.4f",
		     hash, l->position.x, l->position.y, l->position.z, l->radius, l->shaping_hasvalue ? 1 : 0,
		     l->shaping_value.direction.x, l->shaping_value.direction.y, l->shaping_value.direction.z,
		     l->shaping_value.coneAngleDegrees, l->shaping_value.coneSoftness, rad.x, rad.y, rad.z,
		     l->volumetricRadianceScale);
	}
	else if (auto *l = find_ext<remixapi_LightInfoRectEXT>(info->pNext, REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT)) {
		mlog("CreateLight hash=%llx type=rect pos=%.4f,%.4f,%.4f size=%.4f,%.4f dir=%.4f,%.4f,%.4f "
		     "radiance=%.4f,%.4f,%.4f volumetric=%.4f",
		     hash, l->position.x, l->position.y, l->position.z, l->xSize, l->ySize, l->direction.x,
		     l->direction.y, l->direction.z, rad.x, rad.y, rad.z, l->volumetricRadianceScale);
	}
	else if (auto *l = find_ext<remixapi_LightInfoDiskEXT>(info->pNext, REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISK_EXT)) {
		/* interface 8 */
		mlog("CreateLight hash=%llx type=disk pos=%.4f,%.4f,%.4f radii=%.4f,%.4f shaped=%d dir=%.4f,%.4f,%.4f "
		     "cone=%.4f softness=%.4f radiance=%.4f,%.4f,%.4f volumetric=%.4f",
		     hash, l->position.x, l->position.y, l->position.z, l->xRadius, l->yRadius, l->shaping_hasvalue ? 1 : 0,
		     l->direction.x, l->direction.y, l->direction.z, l->shaping_value.coneAngleDegrees,
		     l->shaping_value.coneSoftness, rad.x, rad.y, rad.z, l->volumetricRadianceScale);
	}
	else if (auto *l = find_ext<remixapi_LightInfoDistantEXT>(info->pNext,
	                                                         REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT))
	{
		mlog("CreateLight hash=%llx type=distant dir=%.4f,%.4f,%.4f angle=%.4f radiance=%.4f,%.4f,%.4f "
		     "volumetric=%.4f",
		     hash, l->direction.x, l->direction.y, l->direction.z, l->angularDiameterDegrees, rad.x, rad.y, rad.z,
		     l->volumetricRadianceScale);
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

/* project a world point with the last camera; false when behind it */
bool project(const float p[3], float &x, float &y, float &depth)
{
	const float *v = g_camera, *pr = g_camera + 16; /* column major, D3D depth */
	float e[4], c[4];
	for (int r = 0; r < 4; r++) {
		e[r] = v[0 * 4 + r] * p[0] + v[1 * 4 + r] * p[1] + v[2 * 4 + r] * p[2] + v[3 * 4 + r];
	}
	for (int r = 0; r < 4; r++) {
		c[r] = pr[0 * 4 + r] * e[0] + pr[1 * 4 + r] * e[1] + pr[2 * 4 + r] * e[2] + pr[3 * 4 + r] * e[3];
	}
	if (c[3] <= 1e-6f) {
		return false;
	}
	int w = 0, h = 0;
	if (!mock_backbuffer_size(&w, &h)) {
		return false;
	}
	x = (c[0] / c[3] * 0.5f + 0.5f) * (float)w;
	y = (1.0f - (c[1] / c[3] * 0.5f + 0.5f)) * (float)h;
	depth = c[3];
	return true;
}

/* The first drawn object whose world bounds the ray through the request's
 * centre meets (bounds, not triangles: no cut-out alpha, no exact shapes). */
void answer_pick()
{
	if (!g_pick_pending) {
		return;
	}
	g_pick_pending = false;
	int w = 0, h = 0;
	uint32_t best = 0;
	const float px = 0.5f * (float)(g_pick_rect.left + g_pick_rect.right);
	const float py = 0.5f * (float)(g_pick_rect.top + g_pick_rect.bottom);
	if (mock_backbuffer_size(&w, &h) && w > 0 && h > 0) {
		const float *v = g_camera, *pr = g_camera + 16; /* column major */
		const float nx = px / (float)w * 2.0f - 1.0f, ny = 1.0f - py / (float)h * 2.0f;
		/* view-space direction (z = -1), with the projection's shift */
		const float dv[3] = {(nx + pr[2 * 4 + 0]) / pr[0 * 4 + 0], (ny + pr[2 * 4 + 1]) / pr[1 * 4 + 1], -1.0f};
		/* the view matrix is rigid: world = R^T (view - t) */
		float o[3], d[3];
		for (int i = 0; i < 3; i++) {
			o[i] = -(v[i * 4 + 0] * v[12] + v[i * 4 + 1] * v[13] + v[i * 4 + 2] * v[14]);
			d[i] = v[i * 4 + 0] * dv[0] + v[i * 4 + 1] * dv[1] + v[i * 4 + 2] * dv[2];
		}
		float best_t = 1e30f;
		for (const MockDraw &dr : g_draws) {
			if (!dr.pick_id || dr.lo[0] > dr.hi[0]) {
				continue;
			}
			float t0 = 0.0f, t1 = 1e30f;
			bool hit = true;
			for (int k = 0; k < 3 && hit; k++) {
				const float lo = dr.lo[k] - 1e-3f, hi = dr.hi[k] + 1e-3f;
				if (std::fabs(d[k]) < 1e-9f) {
					hit = o[k] >= lo && o[k] <= hi;
					continue;
				}
				float a = (lo - o[k]) / d[k], b = (hi - o[k]) / d[k];
				if (a > b) {
					std::swap(a, b);
				}
				t0 = std::max(t0, a);
				t1 = std::min(t1, b);
				hit = t0 <= t1;
			}
			if (hit && t0 < best_t) {
				best_t = t0;
				best = dr.pick_id;
			}
		}
	}
	mlog("Pick x=%d y=%d answer=%u", (int)px, (int)py, best);
	if (g_pick_cb) {
		if (best) {
			g_pick_cb(&best, 1, g_pick_user);
		}
		else {
			g_pick_cb(nullptr, 0, g_pick_user);
		}
	}
}

remixapi_ErrorCode REMIXAPI_CALL m_pick_RequestObjectPicking(const remixapi_Rect2D *region,
                                                             PFN_remixapi_pick_RequestObjectPickingUserCallback cb,
                                                             void *user)
{
	if (!region || !cb) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	g_pick_rect = *region;
	g_pick_cb = cb;
	g_pick_user = user;
	g_pick_pending = true;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}

remixapi_ErrorCode REMIXAPI_CALL m_pick_HighlightObjects(const uint32_t *values, uint32_t count, uint8_t, uint8_t,
                                                         uint8_t)
{
	if (!values) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	g_frame_highlighted += (int)count;
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
	answer_pick();
	mlog("Present draws=%d lights=%d blended=%d skinned=%d bones=%d emitters=%d particles=%d highlighted=%d "
	     "bonehash=%llx",
	     g_frame_draws, g_frame_lights, g_frame_blended, g_frame_skinned, g_frame_bones, g_frame_emitters,
	     g_frame_particles, g_frame_highlighted, g_frame_bonehash);
	g_frame_bonehash = 1469598103934665603ull;
	g_frame_draws = 0;
	g_frame_blended = 0;
	g_frame_lights = 0;
	g_frame_skinned = g_frame_bones = g_frame_emitters = g_frame_particles = g_frame_highlighted = 0;
	g_draws.clear();
	g_present_count++;
	if (g_external && g_ext_window && IsWindow(g_ext_window)) {
		/* as remixapi_Present: after presenting, reset to the window's size when
		 * both sides differ */
		int w = 0, h = 0;
		client_size(g_ext_window, w, h);
		if (w > 0 && h > 0 && w != g_ext_w && h != g_ext_h) {
			g_ext_w = w;
			g_ext_h = h;
			g_ext_resets++;
			mlog("External reset %dx%d", w, h);
		}
	}
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
	r.pick_RequestObjectPicking = m_pick_RequestObjectPicking;
	r.pick_HighlightObjects = m_pick_HighlightObjects;
	r.dxvk_GetExternalSwapchain = m_dxvk_GetExternalSwapchain;
	*out_result = r;
	return REMIXAPI_ERROR_CODE_SUCCESS;
}
