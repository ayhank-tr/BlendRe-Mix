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

/** \file bridge_scene.cpp
 *  \ingroup remix_bridge
 *
 * The scene interface (version 2): materials, meshes, lights, camera, draws.
 * The game engine describes things in its own terms (OpenGL matrices,
 * Blender's uv origin); the conversion to Remix's conventions happens here.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "bridge_intern.h"

namespace brb {

brb_frame_stats g_stats = {};

static std::unordered_map<uint64_t, remixapi_MaterialHandle> g_materials;
static std::unordered_map<uint64_t, remixapi_MeshHandle> g_meshes;
static std::unordered_map<uint64_t, remixapi_LightHandle> g_lights;
static int g_frame_draws = 0;
static int g_frame_lights = 0;
static std::wstring g_dome_texture;

void scene_forget()
{
	g_materials.clear();
	g_meshes.clear();
	g_lights.clear();
	g_stats.meshes = g_stats.materials = g_stats.light_objects = 0;
}

static bool running()
{
	return g_status == BRB_STATUS_RUNNING && g_started && g_dll;
}

static int track(remixapi_ErrorCode r)
{
	if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
		g_stats.errors++;
		g_stats.last_error = (int)r;
	}
	return (int)r;
}

static remixapi_Float3D f3(const float v[3])
{
	remixapi_Float3D r = {v[0], v[1], v[2]};
	return r;
}

/* Remix's dome light needs a texture. A small white one, multiplied by the
 * light's radiance, gives a uniform sky. Written once to the temp folder. */
static const std::wstring &dome_texture()
{
	if (!g_dome_texture.empty()) {
		return g_dome_texture;
	}
	wchar_t tmp[MAX_PATH + 1] = {0};
	DWORD n = GetTempPathW(MAX_PATH, tmp);
	std::wstring dir = (n > 0) ? std::wstring(tmp) : std::wstring(L".\\");
	dir += L"bge-remix";
	CreateDirectoryW(dir.c_str(), nullptr);
	std::wstring path = dir + L"\\dome_white.dds";

	const uint32_t W = 8, H = 4;
	uint32_t header[32] = {0};
	header[0] = 0x20534444;            /* "DDS " */
	header[1] = 124;                   /* dwSize */
	header[2] = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000; /* CAPS HEIGHT WIDTH PITCH PIXELFORMAT */
	header[3] = H;
	header[4] = W;
	header[5] = W * 4;                 /* pitch */
	header[19] = 32;                   /* ddspf.dwSize */
	header[20] = 0x40 | 0x1;           /* DDPF_RGB | DDPF_ALPHAPIXELS */
	header[22] = 32;                   /* bits per pixel */
	header[23] = 0x00FF0000;           /* R */
	header[24] = 0x0000FF00;           /* G */
	header[25] = 0x000000FF;           /* B */
	header[26] = 0xFF000000;           /* A */
	header[27] = 0x1000;               /* DDSCAPS_TEXTURE */
	FILE *f = _wfopen(path.c_str(), L"wb");
	if (f) {
		fwrite(header, sizeof(header), 1, f);
		std::vector<uint32_t> px(W * H, 0xFFFFFFFFu);
		fwrite(px.data(), 4, px.size(), f);
		fclose(f);
		g_dome_texture = path;
	}
	else {
		log("could not write the dome texture %s", utf8(path).c_str());
	}
	return g_dome_texture;
}

}  /* namespace brb */

using namespace brb;

extern "C" {

BRB_API int brb_material_create(const brb_material_desc *d)
{
	if (!running() || !d || d->hash == 0) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	remixapi_MaterialInfoOpaqueEXT opaque = default_opaque();
	opaque.albedoConstant = f3(d->albedo);
	opaque.opacityConstant = std::min(std::max(d->opacity, 0.0f), 1.0f);
	opaque.roughnessConstant = std::min(std::max(d->roughness, 0.0f), 1.0f);
	opaque.metallicConstant = std::min(std::max(d->metallic, 0.0f), 1.0f);
	if (d->alpha_blend) {
		/* blend by opacityConstant, not by the (missing) draw call state */
		opaque.useDrawCallAlphaState = FALSE;
		opaque.blendType_hasvalue = TRUE;
		opaque.blendType_value = 0; /* alpha */
	}
	/* Remix ignores a second material with the same hash: replace it */
	brb_material_destroy(d->hash);
	remixapi_MaterialInfo info = default_material(d->hash, &opaque);
	info.emissiveIntensity = std::max(d->emissive_intensity, 0.0f);
	info.emissiveColorConstant = f3(d->emissive);

	remixapi_MaterialHandle h = nullptr;
	remixapi_ErrorCode r = guarded("CreateMaterial", [&] { return g_remix.CreateMaterial(&info, &h); });
	if (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		g_materials[d->hash] = h;
		g_stats.materials_created++;
		g_stats.materials = (int)g_materials.size();
	}
	return track(r);
}

BRB_API void brb_material_destroy(uint64_t hash)
{
	auto it = g_materials.find(hash);
	if (it == g_materials.end()) {
		return;
	}
	if (running()) {
		remixapi_MaterialHandle h = it->second;
		track(guarded("DestroyMaterial", [&] { return g_remix.DestroyMaterial(h); }));
	}
	g_materials.erase(it);
	g_stats.materials = (int)g_materials.size();
}

BRB_API int brb_mesh_create(uint64_t hash, const brb_surface_desc *surfaces, uint32_t surface_count)
{
	if (!running() || hash == 0 || (!surfaces && surface_count)) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	std::vector<std::vector<remixapi_HardcodedVertex>> verts(surface_count);
	std::vector<std::vector<uint32_t>> tris(surface_count);
	std::vector<remixapi_MeshInfoSurfaceTriangles> infos(surface_count);
	for (uint32_t s = 0; s < surface_count; s++) {
		const brb_surface_desc &src = surfaces[s];
		std::vector<remixapi_HardcodedVertex> &dst = verts[s];
		dst.resize(src.vertex_count);
		for (uint32_t i = 0; i < src.vertex_count; i++) {
			const brb_vertex &v = src.vertices[i];
			remixapi_HardcodedVertex &o = dst[i];
			o = remixapi_HardcodedVertex();
			o.position[0] = v.position[0];
			o.position[1] = v.position[1];
			o.position[2] = v.position[2];
			o.normal[0] = v.normal[0];
			o.normal[1] = v.normal[1];
			o.normal[2] = v.normal[2];
			/* Remix samples textures with the origin at the top left (D3D) */
			o.texcoord[0] = v.uv[0];
			o.texcoord[1] = 1.0f - v.uv[1];
			o.color = v.color;
		}
		remixapi_MeshInfoSurfaceTriangles &t = infos[s];
		t = remixapi_MeshInfoSurfaceTriangles();
		t.vertices_values = dst.data();
		t.vertices_count = dst.size();
		/* Remix's front faces are clockwise (D3D), OpenGL's counter-clockwise:
		 * single-sided surfaces would show their back (RTX 3080, P2 room) */
		std::vector<uint32_t> &idx = tris[s];
		idx.assign(src.indices, src.indices + src.index_count);
		for (size_t i = 0; i + 2 < idx.size(); i += 3) {
			std::swap(idx[i + 1], idx[i + 2]);
		}
		t.indices_values = idx.data();
		t.indices_count = idx.size();
		t.skinning_hasvalue = FALSE;
		auto m = g_materials.find(src.material);
		t.material = (m != g_materials.end()) ? m->second : nullptr;
	}
	/* Remix ignores a second mesh with the same hash: replace it */
	brb_mesh_destroy(hash);
	remixapi_MeshInfo info = {};
	info.sType = REMIXAPI_STRUCT_TYPE_MESH_INFO;
	info.hash = hash;
	info.surfaces_values = infos.data();
	info.surfaces_count = surface_count;
	remixapi_MeshHandle h = nullptr;
	remixapi_ErrorCode r = guarded("CreateMesh", [&] { return g_remix.CreateMesh(&info, &h); });
	if (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		g_meshes[hash] = h;
		g_stats.meshes_created++;
		g_stats.meshes = (int)g_meshes.size();
	}
	return track(r);
}

BRB_API void brb_mesh_destroy(uint64_t hash)
{
	auto it = g_meshes.find(hash);
	if (it == g_meshes.end()) {
		return;
	}
	if (running()) {
		remixapi_MeshHandle h = it->second;
		track(guarded("DestroyMesh", [&] { return g_remix.DestroyMesh(h); }));
	}
	g_meshes.erase(it);
	g_stats.meshes = (int)g_meshes.size();
}

BRB_API int brb_light_create(const brb_light_desc *d)
{
	if (!running() || !d || d->hash == 0) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	remixapi_LightInfo info = {};
	info.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO;
	info.hash = d->hash;
	info.radiance = f3(d->radiance);

	remixapi_LightInfoLightShaping shaping = {};
	shaping.direction = f3(d->direction);
	shaping.coneAngleDegrees = d->cone_angle_deg;
	shaping.coneSoftness = d->cone_softness;
	shaping.focusExponent = 0.0f;

	remixapi_LightInfoSphereEXT sphere = default_sphere();
	remixapi_LightInfoRectEXT rect = {};
	remixapi_LightInfoDistantEXT distant = {};
	remixapi_LightInfoDomeEXT dome = {};
	switch (d->type) {
		case BRB_LIGHT_SPHERE:
			sphere.position = f3(d->position);
			sphere.radius = std::max(d->radius, 0.001f);
			sphere.shaping_hasvalue = d->shaped ? TRUE : FALSE;
			sphere.shaping_value = shaping;
			info.pNext = &sphere;
			break;
		case BRB_LIGHT_RECT:
			rect.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_RECT_EXT;
			rect.position = f3(d->position);
			rect.xAxis = f3(d->x_axis);
			rect.xSize = std::max(d->size[0], 0.001f);
			rect.yAxis = f3(d->y_axis);
			rect.ySize = std::max(d->size[1], 0.001f);
			rect.direction = f3(d->direction);
			rect.shaping_hasvalue = d->shaped ? TRUE : FALSE;
			rect.shaping_value = shaping;
			rect.volumetricRadianceScale = 1.0f;
			info.pNext = &rect;
			break;
		case BRB_LIGHT_DISTANT:
			distant.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DISTANT_EXT;
			distant.direction = f3(d->direction);
			distant.angularDiameterDegrees = std::max(d->angular_diameter_deg, 0.01f);
			distant.volumetricRadianceScale = 1.0f;
			info.pNext = &distant;
			break;
		case BRB_LIGHT_DOME:
			dome.sType = REMIXAPI_STRUCT_TYPE_LIGHT_INFO_DOME_EXT;
			dome.transform = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}};
			dome.colorTexture = dome_texture().c_str();
			info.pNext = &dome;
			break;
		default:
			return track(REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
	}
	remixapi_LightHandle h = nullptr;
	remixapi_ErrorCode r = guarded("CreateLight", [&] { return g_remix.CreateLight(&info, &h); });
	if (r == REMIXAPI_ERROR_CODE_SUCCESS) {
		g_lights[d->hash] = h;
		g_stats.lights_created++;
		g_stats.light_objects = (int)g_lights.size();
	}
	return track(r);
}

BRB_API void brb_light_destroy(uint64_t hash)
{
	auto it = g_lights.find(hash);
	if (it == g_lights.end()) {
		return;
	}
	if (running()) {
		remixapi_LightHandle h = it->second;
		track(guarded("DestroyLight", [&] { return g_remix.DestroyLight(h); }));
	}
	g_lights.erase(it);
	g_stats.light_objects = (int)g_lights.size();
}

BRB_API void brb_scene_clear(void)
{
	std::vector<uint64_t> keys;
	for (auto &kv : g_meshes) {
		keys.push_back(kv.first);
	}
	for (uint64_t k : keys) {
		brb_mesh_destroy(k);
	}
	keys.clear();
	for (auto &kv : g_materials) {
		keys.push_back(kv.first);
	}
	for (uint64_t k : keys) {
		brb_material_destroy(k);
	}
	keys.clear();
	for (auto &kv : g_lights) {
		keys.push_back(kv.first);
	}
	for (uint64_t k : keys) {
		brb_light_destroy(k);
	}
	log("scene cleared");
}

BRB_API int brb_frame_begin(void)
{
	device_frame_unlock();
	g_frame_draws = 0;
	g_frame_lights = 0;
	return running() ? 0 : REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
}

BRB_API int brb_frame_camera(const float view[16], const float proj[16])
{
	if (!running() || !view || !proj) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	/* Both OpenGL and Remix store a matrix as four columns of four floats, so
	 * the view matrix goes through as it is. Remix's projection is D3D's:
	 * clip depth 0..1 instead of -1..1, so z' = (z + w) / 2. Remix reads
	 * field of view, aspect, clip range, shift and handedness (right-handed
	 * here) from the matrix itself. */
	remixapi_CameraInfo info = {};
	info.sType = REMIXAPI_STRUCT_TYPE_CAMERA_INFO;
	info.type = REMIXAPI_CAMERA_TYPE_WORLD;
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			info.view[c][r] = view[c * 4 + r];
			info.projection[c][r] = proj[c * 4 + r];
		}
		info.projection[c][2] = 0.5f * proj[c * 4 + 2] + 0.5f * proj[c * 4 + 3];
	}
	return track(guarded("SetupCamera", [&] { return g_remix.SetupCamera(&info); }));
}

BRB_API int brb_frame_draw(uint64_t mesh, const float model[16], int flags)
{
	if (!running() || !model) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	auto it = g_meshes.find(mesh);
	if (it == g_meshes.end()) {
		return track(REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
	}
	remixapi_InstanceInfo inst = {};
	inst.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
	inst.mesh = it->second;
	/* 3 x 4, rows; OpenGL's matrix is column major */
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 4; c++) {
			inst.transform.matrix[r][c] = model[c * 4 + r];
		}
	}
	inst.doubleSided = (flags & BRB_DRAW_DOUBLE_SIDED) ? TRUE : FALSE;
	/* Remix traces surfaces as fully opaque unless the instance's draw state
	 * blends (the material's own blend type and opacity then apply) */
	remixapi_InstanceInfoBlendEXT blend = {};
	if (flags & BRB_DRAW_BLEND) {
		blend.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BLEND_EXT;
		blend.alphaBlendEnabled = TRUE;
		blend.srcColorBlendFactor = 6;  /* VK_BLEND_FACTOR_SRC_ALPHA */
		blend.dstColorBlendFactor = 7;  /* VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA */
		blend.colorBlendOp = 0;         /* VK_BLEND_OP_ADD */
		blend.srcAlphaBlendFactor = 1;  /* VK_BLEND_FACTOR_ONE */
		blend.dstAlphaBlendFactor = 0;  /* VK_BLEND_FACTOR_ZERO */
		blend.alphaBlendOp = 0;
		blend.writeMask = 0xF;
		blend.tFactor = 0xFFFFFFFFu;
		inst.pNext = &blend;
	}
	g_frame_draws++;
	return track(guarded("DrawInstance", [&] { return g_remix.DrawInstance(&inst); }));
}

BRB_API int brb_frame_light(uint64_t light)
{
	if (!running()) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	auto it = g_lights.find(light);
	if (it == g_lights.end()) {
		return track(REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
	}
	g_frame_lights++;
	remixapi_LightHandle h = it->second;
	return track(guarded("DrawLightInstance", [&] { return g_remix.DrawLightInstance(h); }));
}

BRB_API int brb_frame_end(void)
{
	if (!running()) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	remixapi_ErrorCode r = (g_mode == BRB_MODE_WINDOW) ?
	                           guarded("Present", [] { return g_remix.Present(nullptr); }) :
	                           device_present();
	g_stats.frames++;
	g_stats.draws = g_frame_draws;
	g_stats.lights = g_frame_lights;
	return track(r);
}

BRB_API void brb_get_stats(brb_frame_stats *r_stats)
{
	if (r_stats) {
		*r_stats = g_stats;
	}
}

BRB_API void brb_reset_stats(void)
{
	const int w = g_stats.width, h = g_stats.height;
	const int meshes = g_stats.meshes, materials = g_stats.materials, lights = g_stats.light_objects;
	g_stats = brb_frame_stats();
	g_stats.width = w;
	g_stats.height = h;
	g_stats.meshes = meshes;
	g_stats.materials = materials;
	g_stats.light_objects = lights;
}

}  /* extern "C" */
