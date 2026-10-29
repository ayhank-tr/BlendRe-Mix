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
 * The scene interface (versions 2 to 4): materials, meshes, lights, camera,
 * draws, skinning, picking, GPU particles.
 * The game engine describes things in its own terms (OpenGL matrices,
 * Blender's uv origin); the conversion to Remix's conventions happens here.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
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
static int g_frame_skinned = 0;
static int g_frame_bones = 0;
static int g_frame_emitters = 0;

/* picking: Remix answers on one of its own threads */
static std::mutex g_pick_mutex;
static int g_pick_state = 0;        /* 0 none, 1 waiting, 2 answered, -1 failed */
static unsigned g_pick_serial = 0;  /* the request an answer belongs to */
static std::vector<uint32_t> g_pick_ids;
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

/* Remix's material shaders take albedo, emissive and transmittance colours
 * as gamma (sRGB) values and convert them to linear (gammaToLinear in
 * opaque_surface_material_interaction.slangh): Blender's linear colours are
 * encoded first, or they come out squared (P3 RTX run: oversaturated). */
static float srgb(float c)
{
	c = std::min(std::max(c, 0.0f), 1.0f);
	return (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

static remixapi_Float3D srgb3(const float v[3])
{
	remixapi_Float3D r = {srgb(v[0]), srgb(v[1]), srgb(v[2])};
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

/* OpenGL 4 x 4 (column major) to Remix's 3 x 4 rows */
static remixapi_Transform to_transform(const float m[16])
{
	remixapi_Transform t = {};
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 4; c++) {
			t.matrix[r][c] = m[c * 4 + r];
		}
	}
	return t;
}

/* a * b, both OpenGL (column major) */
static void mul4(const float a[16], const float b[16], float r[16])
{
	for (int c = 0; c < 4; c++) {
		for (int row = 0; row < 4; row++) {
			float v = 0.0f;
			for (int k = 0; k < 4; k++) {
				v += a[k * 4 + row] * b[c * 4 + k];
			}
			r[c * 4 + row] = v;
		}
	}
}

/* Bones in the object's space (the interface's convention) and the
 * object's matrix as the instance transform; BGE_REMIX_BONES_WORLD=1 sends
 * world-space bones with an identity transform instead (the RTX check
 * T4.1 tells which one Remix 1.6.1 expects). */
static bool bones_world()
{
	static int v = -1;
	if (v < 0) {
		const char *e = getenv("BGE_REMIX_BONES_WORLD");
		v = (e && e[0] == '1') ? 1 : 0;
		if (v) {
			log("bones are sent in world space (BGE_REMIX_BONES_WORLD)");
		}
	}
	return v == 1;
}

static uint32_t to_categories(uint32_t c)
{
	uint32_t r = 0;
	if (c & BRB_CATEGORY_PARTICLE) {
		r |= REMIXAPI_INSTANCE_CATEGORY_BIT_PARTICLE;
	}
	if (c & BRB_CATEGORY_PARTICLE_EMITTER) {
		r |= REMIXAPI_INSTANCE_CATEGORY_BIT_PARTICLE_EMITTER;
	}
	return r;
}

/* Blender units (metres) to Remix's particle units (centimetres, which
 * Remix scales by rtx.sceneScale = 0.01 into our metres) */
static const float CM = 100.0f;

struct ParticleExt {
	remixapi_InstanceInfoParticleSystemEXT info;
	remixapi_Float4D color[2];
	remixapi_Float2D size_min[2], size_max[2];
	float rot_min[2], rot_max[2];
	remixapi_Float3D velocity[2];
};

static void fill_particles(const brb_particle_desc &d, ParticleExt &p)
{
	remixapi_InstanceInfoParticleSystemEXT &i = p.info;
	i = remixapi_InstanceInfoParticleSystemEXT();
	i.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_PARTICLE_SYSTEM_EXT;
	i.maxNumParticles = std::max<uint32_t>(d.max_particles, 1u);
	i.spawnRatePerSecond = std::max(d.spawn_rate, 0.0f);
	i.minTimeToLive = std::max(d.lifetime_min, 0.01f);
	i.maxTimeToLive = std::max(d.lifetime_max, i.minTimeToLive);
	i.initialVelocityFromNormal = d.speed_normal * CM;
	i.initialVelocityConeAngleDegrees = std::min(std::max(d.cone_angle_deg, 0.0f), 180.0f);
	i.initialVelocityFromMotion = d.speed_from_motion;
	i.gravityForce = d.gravity * CM;
	i.dragCoefficient = std::max(d.drag, 0.0f);
	i.enableCollisionDetection = d.collide ? TRUE : FALSE;
	i.collisionRestitution = std::min(std::max(d.restitution, 0.0f), 1.0f);
	i.collisionThickness = 5.0f;
	i.collisionMode = 0; /* bounce */
	i.alignParticlesToVelocity = d.align_to_velocity ? TRUE : FALSE;
	i.hideEmitter = d.hide_emitter ? TRUE : FALSE;
	i.billboardType = (uint8_t)std::min(std::max(d.billboard, 0), 3);
	i.spriteSheetMode = 0;
	i.motionTrailMultiplier = 1.0f;
	i.turbulenceFrequency = 0.05f;
	i.turbulenceForce = 0.0f;
	/* the animated values: start (birth) and end (death) */
	for (int k = 0; k < 2; k++) {
		const float *c = k ? d.color_end : d.color_start;
		p.color[k] = {c[0], c[1], c[2], c[3]};
		const float scale = k ? d.size_end_scale : 1.0f;
		p.size_min[k] = {d.size_min * scale * CM, d.size_min * scale * CM};
		p.size_max[k] = {d.size_max * scale * CM, d.size_max * scale * CM};
		p.rot_min[k] = -std::fabs(d.rotation_speed);
		p.rot_max[k] = std::fabs(d.rotation_speed);
		p.velocity[k] = {-1.0f, -1.0f, -1.0f}; /* no limit */
	}
	i.minColor = {p.color, 2};
	i.maxColor = {p.color, 2};
	i.minSize = {p.size_min, 2};
	i.maxSize = {p.size_max, 2};
	i.minRotationSpeed = {p.rot_min, 2};
	i.maxRotationSpeed = {p.rot_max, 2};
	i.maxVelocity = {p.velocity, 2};
}

static int draw_instance(const brb_draw_desc &d)
{
	if (!running()) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	auto it = g_meshes.find(d.mesh);
	if (it == g_meshes.end()) {
		return track(REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
	}
	const int flags = d.flags;
	const bool skinned = d.bones && d.bone_count > 0;
	const bool world = skinned && bones_world();
	static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	remixapi_InstanceInfo inst = {};
	inst.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO;
	inst.mesh = it->second;
	inst.transform = to_transform(world ? identity : d.model);
	inst.categoryFlags = to_categories(d.categories);
	inst.doubleSided = (flags & BRB_DRAW_DOUBLE_SIDED) ? TRUE : FALSE;
	void **tail = &inst.pNext;

	remixapi_InstanceInfoObjectPickingEXT pick = {};
	if (d.pick_id) {
		pick.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_OBJECT_PICKING_EXT;
		pick.objectPickingValue = d.pick_id;
		*tail = &pick;
		tail = &pick.pNext;
	}

	std::vector<remixapi_Transform> bone_tf;
	remixapi_InstanceInfoBoneTransformsEXT bones = {};
	if (skinned) {
		const uint32_t n = std::min<uint32_t>(d.bone_count, BRB_MAX_BONES);
		bone_tf.resize(n);
		for (uint32_t b = 0; b < n; b++) {
			if (world) {
				float m[16];
				mul4(d.model, d.bones + (size_t)b * 16, m);
				bone_tf[b] = to_transform(m);
			}
			else {
				bone_tf[b] = to_transform(d.bones + (size_t)b * 16);
			}
		}
		bones.sType = REMIXAPI_STRUCT_TYPE_INSTANCE_INFO_BONE_TRANSFORMS_EXT;
		bones.boneTransforms_values = bone_tf.data();
		bones.boneTransforms_count = n;
		*tail = &bones;
		tail = &bones.pNext;
		g_frame_skinned++;
		g_frame_bones += (int)n;
	}

	ParticleExt particles;
	if ((d.categories & BRB_CATEGORY_PARTICLE_EMITTER) && d.particles) {
		fill_particles(*d.particles, particles);
		*tail = &particles.info;
		tail = &particles.info.pNext;
		g_frame_emitters++;
	}
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
		*tail = &blend;
		tail = &blend.pNext;
	}
	g_frame_draws++;
	return track(guarded("DrawInstance", [&] { return g_remix.DrawInstance(&inst); }));
}

}  /* namespace brb */

using namespace brb;

extern "C" {

BRB_API int brb_material_create(const brb_material_desc *d)
{
	if (!running() || !d || d->hash == 0) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	auto clamp01 = [](float v) { return std::min(std::max(v, 0.0f), 1.0f); };
	/* "" and NULL: no texture (Remix wants NULL) */
	auto tex = [](const wchar_t *p) -> const wchar_t * { return (p && p[0]) ? p : nullptr; };

	remixapi_MaterialInfoOpaqueEXT opaque = default_opaque();
	remixapi_MaterialInfoOpaqueSubsurfaceEXT sss = {};
	remixapi_MaterialInfoTranslucentEXT glass = {};
	void *ext = &opaque;

	if (d->type == BRB_MATERIAL_TRANSLUCENT) {
		glass.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_TRANSLUCENT_EXT;
		glass.refractiveIndex = std::min(std::max(d->ior, 1.0f), 3.0f);
		glass.transmittanceColor = srgb3(d->transmittance);
		/* Remix's glass rejects a zero distance; Blender's glass tints per surface */
		glass.transmittanceMeasurementDistance = std::max(d->transmittance_distance, 0.001f);
		glass.thinWallThickness_hasvalue = d->thin_walled ? TRUE : FALSE;
		glass.thinWallThickness_value = 0.001f;
		glass.useDiffuseLayer = FALSE;
		ext = &glass;
	}
	else {
		opaque.albedoConstant = srgb3(d->albedo);
		opaque.opacityConstant = clamp01(d->opacity);
		opaque.roughnessConstant = clamp01(d->roughness);
		opaque.metallicConstant = clamp01(d->metallic);
		opaque.roughnessTexture = tex(d->roughness_texture);
		opaque.metallicTexture = tex(d->metallic_texture);
		if (d->additive) {
			/* P8.8: Blender's Add blending is Remix's emissive blending (one,
			 * one: dxvk-remix BlendType::kEmissive) */
			opaque.useDrawCallAlphaState = FALSE;
			opaque.blendType_hasvalue = TRUE;
			opaque.blendType_value = 8;
		}
		else if (d->alpha_blend) {
			/* blend by opacity (and the albedo texture's alpha), not by the
			 * (missing) draw call state */
			opaque.useDrawCallAlphaState = FALSE;
			opaque.blendType_hasvalue = TRUE;
			opaque.blendType_value = 0; /* alpha */
		}
		else if (d->alpha_test) {
			/* cut out: keep texels whose alpha is above the cutoff */
			opaque.useDrawCallAlphaState = FALSE;
			opaque.blendType_hasvalue = FALSE;
			opaque.alphaTestType = 4; /* AlphaTestType::kGreater */
			opaque.alphaReferenceValue = (uint8_t)std::lround(clamp01(d->alpha_cutoff) * 255.0f);
		}
		if (d->subsurface) {
			/* Remix's diffusion profile, as Blender's Christensen-Burley:
			 * radius per channel in scene units (here Blender units) */
			sss.sType = REMIXAPI_STRUCT_TYPE_MATERIAL_INFO_OPAQUE_SUBSURFACE_EXT;
			sss.subsurfaceTransmittanceColor = srgb3(d->subsurface_color);
			sss.subsurfaceSingleScatteringAlbedo = f3(d->subsurface_color);
			sss.subsurfaceMeasurementDistance = 0.0f;
			sss.subsurfaceVolumetricAnisotropy = 0.0f;
			sss.subsurfaceDiffusionProfile = TRUE;
			sss.subsurfaceRadius = f3(d->subsurface_radius);
			sss.subsurfaceRadiusScale = 1.0f;
			const float rmax = std::max(std::max(d->subsurface_radius[0], d->subsurface_radius[1]), d->subsurface_radius[2]);
			sss.subsurfaceMaxSampleRadius = std::max(rmax * 3.0f, 0.001f);
			opaque.pNext = &sss;
		}
	}
	/* Remix ignores a second material with the same hash: replace it */
	brb_material_destroy(d->hash);
	remixapi_MaterialInfo info = default_material(d->hash, ext);
	info.emissiveIntensity = std::max(d->emissive_intensity, 0.0f);
	info.emissiveColorConstant = srgb3(d->emissive);
	info.albedoTexture = tex(d->albedo_texture);
	info.normalTexture = tex(d->normal_texture);
	info.emissiveTexture = tex(d->emissive_texture);
	info.filterMode = d->filter_nearest ? 0 : 1;   /* MDL filter: nearest, linear */
	info.wrapModeU = info.wrapModeV = d->clamp ? 0 : 1; /* MDL wrap: clamp, repeat */

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

/* BGE_REMIX_SKIN_LAYOUT=documented: the weight layout remix_c.h describes
 * (bones_per_vertex weights per vertex, one after the other), for the RTX
 * check that tells which one Remix 1.6.1 reads (see brb_mesh_create). */
static bool documented_layout()
{
	static int v = -1;
	if (v < 0) {
		const char *e = getenv("BGE_REMIX_SKIN_LAYOUT");
		v = (e && strcmp(e, "documented") == 0) ? 1 : 0;
		if (v) {
			log("bone weights in the documented layout (BGE_REMIX_SKIN_LAYOUT)");
		}
	}
	return v == 1;
}

BRB_API int brb_mesh_create(uint64_t hash, const brb_surface_desc *surfaces, uint32_t surface_count)
{
	if (!running() || hash == 0 || (!surfaces && surface_count)) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	std::vector<std::vector<remixapi_HardcodedVertex>> verts(surface_count);
	std::vector<std::vector<uint32_t>> tris(surface_count);
	std::vector<std::vector<float>> weights(surface_count);
	std::vector<std::vector<uint32_t>> bones(surface_count);
	std::vector<remixapi_MeshInfoSurfaceTriangles> infos(surface_count);
	for (uint32_t s = 0; s < surface_count; s++) {
		const brb_surface_desc &src = surfaces[s];
		const brb_skin_desc *skin = src.skin;
		const uint32_t bpv = skin ? std::min<uint32_t>(std::max<uint32_t>(skin->bones_per_vertex, 1u),
		                                                  BRB_MAX_BONES_PER_VERTEX) : 0;
		if (skin && (!skin->weights || !skin->bones)) {
			return track(REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS);
		}
		/* Remix 1.6.1 reads a vertex's blend weights from float number
		 * (vertex index) of the weight buffer (the buffer's stride is one
		 * float, rtx_remix_api.cpp), bones_per_vertex - 1 of them, the last
		 * weight being the rest to 1 (skinning.h). With more than two bones
		 * per vertex these reads overlap, so each vertex is placed
		 * bones_per_vertex - 1 slots apart and its weights written there; the
		 * slots in between hold copies that no triangle uses. */
		const bool documented = documented_layout();
		const uint32_t spacing = (bpv > 2 && !documented) ? bpv - 1 : 1;
		const uint32_t slots = (src.vertex_count == 0) ? 0 : (src.vertex_count - 1) * spacing + 1;
		std::vector<remixapi_HardcodedVertex> &dst = verts[s];
		dst.resize(slots);
		if (skin) {
			/* the size Remix checks: bones_per_vertex values per vertex */
			weights[s].assign((size_t)slots * bpv, 0.0f);
			bones[s].assign((size_t)slots * bpv, 0u);
		}
		for (uint32_t i = 0; i < src.vertex_count; i++) {
			const brb_vertex &v = src.vertices[i];
			remixapi_HardcodedVertex &o = dst[(size_t)i * spacing];
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
			if (skin) {
				const size_t at = (size_t)i * spacing;
				for (uint32_t k = 0; k < bpv; k++) {
					bones[s][at * bpv + k] = skin->bones[(size_t)i * bpv + k];
				}
				if (documented) {
					for (uint32_t k = 0; k < bpv; k++) {
						weights[s][at * bpv + k] = skin->weights[(size_t)i * bpv + k];
					}
				}
				else {
					/* the first bpv - 1 weights from float number `at` on */
					for (uint32_t k = 0; k + 1 < bpv; k++) {
						weights[s][at + k] = skin->weights[(size_t)i * bpv + k];
					}
				}
			}
			/* the unused slots after it: the same vertex, so that bounds stay */
			for (uint32_t k = 1; k < spacing && (size_t)i * spacing + k < dst.size(); k++) {
				dst[(size_t)i * spacing + k] = o;
			}
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
		if (spacing > 1) {
			for (uint32_t &k : idx) {
				k *= spacing;
			}
		}
		t.indices_values = idx.data();
		t.indices_count = idx.size();
		t.skinning_hasvalue = skin ? TRUE : FALSE;
		if (skin) {
			t.skinning_value.bonesPerVertex = bpv;
			t.skinning_value.blendWeights_values = weights[s].data();
			t.skinning_value.blendWeights_count = (uint32_t)weights[s].size();
			t.skinning_value.blendIndices_values = bones[s].data();
			t.skinning_value.blendIndices_count = (uint32_t)bones[s].size();
		}
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
	g_frame_skinned = 0;
	g_frame_bones = 0;
	g_frame_emitters = 0;
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
	if (!model) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	brb_draw_desc d = {};
	d.mesh = mesh;
	memcpy(d.model, model, sizeof(d.model));
	d.flags = flags;
	return draw_instance(d);
}

BRB_API int brb_frame_draw_ex(const brb_draw_desc *desc)
{
	if (!desc) {
		return REMIXAPI_ERROR_CODE_INVALID_ARGUMENTS;
	}
	return draw_instance(*desc);
}

static void REMIXAPI_PTR pick_callback(const uint32_t *values, uint32_t count, void *user)
{
	std::lock_guard<std::mutex> lock(g_pick_mutex);
	if ((unsigned)(uintptr_t)user != g_pick_serial) {
		return; /* an answer to a request that was replaced */
	}
	g_pick_ids.assign(values, values + count);
	std::sort(g_pick_ids.begin(), g_pick_ids.end());
	g_pick_state = 2;
}

BRB_API int brb_pick_request(int left, int top, int right, int bottom)
{
	if (!running() || !g_remix.pick_RequestObjectPicking) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	remixapi_Rect2D rect = {left, top, std::max(right, left + 1), std::max(bottom, top + 1)};
	void *user;
	{
		std::lock_guard<std::mutex> lock(g_pick_mutex);
		g_pick_serial++;
		g_pick_state = 1;
		g_pick_ids.clear();
		user = (void *)(uintptr_t)g_pick_serial;
	}
	remixapi_ErrorCode r = guarded("pick_RequestObjectPicking",
	                               [&] { return g_remix.pick_RequestObjectPicking(&rect, pick_callback, user); });
	if (r != REMIXAPI_ERROR_CODE_SUCCESS) {
		std::lock_guard<std::mutex> lock(g_pick_mutex);
		g_pick_state = -1;
	}
	return track(r);
}

BRB_API int brb_pick_result(uint32_t *r_ids, int max, int *r_count)
{
	std::lock_guard<std::mutex> lock(g_pick_mutex);
	if (r_count) {
		*r_count = 0;
	}
	if (g_pick_state == -1) {
		g_pick_state = 0;
		return -1;
	}
	if (g_pick_state != 2) {
		return 0;
	}
	const int n = (int)g_pick_ids.size();
	for (int i = 0; i < n && i < max && r_ids; i++) {
		r_ids[i] = g_pick_ids[i];
	}
	if (r_count) {
		*r_count = n;
	}
	g_pick_state = 0;
	g_stats.picks++;
	return 1;
}

BRB_API int brb_highlight(const uint32_t *ids, int count, float r, float g, float b)
{
	if (!running() || !g_remix.pick_HighlightObjects) {
		return REMIXAPI_ERROR_CODE_NOT_INITIALIZED;
	}
	if (!ids || count <= 0) {
		return 0;
	}
	auto byte = [](float v) { return (uint8_t)std::lround(std::min(std::max(v, 0.0f), 1.0f) * 255.0f); };
	return track(guarded("pick_HighlightObjects", [&] {
		return g_remix.pick_HighlightObjects(ids, (uint32_t)count, byte(r), byte(g), byte(b));
	}));
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
	g_stats.skinned_draws = g_frame_skinned;
	g_stats.bones = g_frame_bones;
	g_stats.emitters = g_frame_emitters;
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
