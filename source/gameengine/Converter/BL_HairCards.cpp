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

/** \file gameengine/Converter/BL_HairCards.cpp
 *  \ingroup bgeconv
 *
 * BGE-Remix P8 Part C (P8.20): hair as ribbons (BL_HairCards.h).
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "BL_HairCards.h"
#include "BL_BlenderDataConversion.h"
#include "KX_BlenderSceneConverter.h"
#include "KX_Scene.h"
#include "RAS_MeshObject.h"

extern "C" {
#include "MEM_guardedalloc.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_string.h"
#include "DNA_ID.h"
#include "BKE_customdata.h"
#include "BKE_DerivedMesh.h"
#include "BKE_global.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_particle.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_subsurf.h"
#include "BKE_texture.h"
#include "DNA_texture_types.h"
#include "RE_shader_ext.h"
#include "IMB_colormanagement.h"
#include "GPU_layer.h"
#include "BKE_scene.h"
#include "DNA_camera_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"
}

extern "C" Material defmaterial;  /* material.c */

/* Blender Internal's strand width along the hair (strand_eval_width): t 0
 * at the root, 1 at the tip */
static float strand_width(const Material *ma, float t)
{
	float fac = t;
	if (ma->strand_ease != 0.0f) {
		fac = (ma->strand_ease < 0.0f) ? powf(t, 1.0f + ma->strand_ease) : powf(t, 1.0f / (1.0f - ma->strand_ease));
	}
	return (1.0f - fac) * ma->strand_sta + fac * ma->strand_end;
}

/* the length one pixel of the F12 picture covers at distance d from the
 * scene camera (or 0 without one) */
static float pixel_length(Scene *scene, float d)
{
	Object *camob = scene ? scene->camera : NULL;
	if (!camob || camob->type != OB_CAMERA || !camob->data)
		return 0.0f;
	const Camera *ca = (const Camera *)camob->data;
	const float width = std::max(1.0f, (float)scene->r.xsch * (float)scene->r.size / 100.0f);
	if (ca->type == CAM_ORTHO)
		return ca->ortho_scale / width;
	const float sensor = (ca->sensor_fit == CAMERA_SENSOR_FIT_VERT) ? ca->sensor_y : ca->sensor_x;
	return 2.0f * d * (0.5f * sensor / std::max(ca->lens, 1e-3f)) / width;
}

RAS_MeshObject *BL_ConvertHairCards(Object *ob, KX_Scene *kxscene, KX_BlenderSceneConverter *converter,
                                    bool libloading)
{
	if (!ob || ob->type != OB_MESH || !ob->particlesystem.first)
		return NULL;
	Scene *scene = kxscene->GetBlenderScene();

	bool any = false;
	for (ParticleSystem *psys = (ParticleSystem *)ob->particlesystem.first; psys; psys = psys->next) {
		if (psys->part && psys->part->type == PART_HAIR && psys->part->ren_as == PART_DRAW_PATH &&
		    !(psys->flag & PSYS_DISABLED))
			any = true;
	}
	if (!any)
		return NULL;

	/* the particle systems worked out (their modifier runs with the mesh's) */
	mesh_get_derived_final(scene, ob, CD_MASK_BAREMESH);

	float imat[4][4], campos[3] = {0.0f, 0.0f, 0.0f};
	invert_m4_m4(imat, ob->obmat);
	if (scene->camera)
		mul_v3_m4v3(campos, imat, scene->camera->obmat[3]);

	std::vector<float> co;        /* 3 per vertex */
	std::vector<float> uv;        /* 2 per vertex */
	std::vector<int> quads;       /* 4 vertex numbers each */
	std::vector<short> quad_mat;  /* the material slot of each */
	std::vector<Material *> mats;
	int strands = 0;

	for (ParticleSystem *psys = (ParticleSystem *)ob->particlesystem.first; psys; psys = psys->next) {
		ParticleSettings *part = psys->part;
		if (!part || part->type != PART_HAIR || part->ren_as != PART_DRAW_PATH || (psys->flag & PSYS_DISABLED))
			continue;
		if (!psys->pathcache && !psys->childcache) {
			/* not cached by the viewport (its Display is not Rendered): made here */
			ParticleSimulationData sim = {0};
			sim.scene = scene;
			sim.ob = ob;
			sim.psys = psys;
			sim.psmd = psys_get_modifier(ob, psys);
			if (sim.psmd && sim.psmd->dm_final) {
				const float cfra = BKE_scene_frame_get(scene);
				psys_cache_paths(&sim, cfra, false);
				psys_cache_child_paths(&sim, cfra, false, false);
			}
		}
		Material *ma = give_current_material(ob, part->omat);
		if (!ma)
			ma = &defmaterial;
		short slot = -1;
		for (size_t m = 0; m < mats.size(); m++) {
			if (mats[m] == ma)
				slot = (short)m;
		}
		if (slot < 0) {
			slot = (short)mats.size();
			mats.push_back(ma);
		}
		const bool units = (ma->mode & MA_STR_B_UNITS) != 0;

		/* the paths: parents (unless children hide them) and children */
		std::vector<ParticleCacheKey *> paths;
		const int totchild = psys->childcache ? psys->totchildcache : 0;
		if (psys->pathcache && (totchild == 0 || (part->draw & PART_DRAW_PARENT))) {
			for (int i = 0; i < psys->totcached; i++)
				paths.push_back(psys->pathcache[i]);
		}
		for (int i = 0; i < totchild; i++)
			paths.push_back(psys->childcache[i]);

		for (size_t i = 0; i < paths.size(); i++) {
			ParticleCacheKey *key = paths[i];
			const int segs = key ? key->segments : 0;
			if (segs < 1)
				continue;
			std::vector<float> pts(3 * (size_t)(segs + 1));
			for (int k = 0; k <= segs; k++)
				mul_v3_m4v3(&pts[3 * k], imat, key[k].co);
			const int first = (int)(co.size() / 3);
			/* the root's distance to the camera turns pixels into lengths */
			const float px = units ? 1.0f : pixel_length(scene, len_v3v3(&pts[0], campos) * mat4_to_scale(ob->obmat)) /
			                                    std::max(mat4_to_scale(ob->obmat), 1e-6f);
			for (int k = 0; k <= segs; k++) {
				const float *p = &pts[3 * k];
				float tan[3], view[3], side[3];
				const float *a = &pts[3 * std::max(k - 1, 0)], *b = &pts[3 * std::min(k + 1, segs)];
				sub_v3_v3v3(tan, b, a);
				sub_v3_v3v3(view, p, campos);
				cross_v3_v3v3(side, tan, view);
				if (normalize_v3(side) < 1e-8f) {
					float up[3] = {0.0f, 0.0f, 1.0f};
					cross_v3_v3v3(side, tan, up);
					if (normalize_v3(side) < 1e-8f)
						side[0] = 1.0f;
				}
				const float t = (float)k / (float)segs;
				float w = strand_width(ma, t);
				if (!units)
					w = std::max(w, ma->strand_min);
				w *= px;
				if (w <= 0.0f)
					w = 1e-4f;
				for (int s = 0; s < 2; s++) {
					const float f = (s == 0 ? -0.5f : 0.5f) * w;
					co.push_back(p[0] + side[0] * f);
					co.push_back(p[1] + side[1] * f);
					co.push_back(p[2] + side[2] * f);
					uv.push_back((float)s);
					uv.push_back(t);
				}
			}
			/* the faces turned toward the camera */
			float n[3], toward[3], e1[3], e2[3];
			sub_v3_v3v3(e1, &co[3 * (first + 1)], &co[3 * first]);
			sub_v3_v3v3(e2, &co[3 * (first + 2)], &co[3 * first]);
			cross_v3_v3v3(n, e1, e2);
			sub_v3_v3v3(toward, campos, &co[3 * first]);
			const bool flip = dot_v3v3(n, toward) < 0.0f;
			for (int k = 0; k < segs; k++) {
				const int v0 = first + 2 * k, v1 = v0 + 1, v2 = v0 + 3, v3 = v0 + 2;
				if (flip) {
					quads.push_back(v0); quads.push_back(v3); quads.push_back(v2); quads.push_back(v1);
				}
				else {
					quads.push_back(v0); quads.push_back(v1); quads.push_back(v2); quads.push_back(v3);
				}
				quad_mat.push_back(slot);
			}
			strands++;
		}
	}
	if (quads.empty())
		return NULL;

	/* a mesh for the converter (freed with it at the game's end) */
	const int totvert = (int)(co.size() / 3), totpoly = (int)(quads.size() / 4), totloop = totpoly * 4;
	char name[MAX_ID_NAME];
	BLI_snprintf(name, sizeof(name), "BGE-Remix hair %s", ob->id.name + 2);
	Mesh *me = BKE_mesh_add(G.main, name);
	me->totvert = totvert;
	me->totpoly = totpoly;
	me->totloop = totloop;
	CustomData_add_layer(&me->vdata, CD_MVERT, CD_CALLOC, NULL, totvert);
	CustomData_add_layer(&me->pdata, CD_MPOLY, CD_CALLOC, NULL, totpoly);
	CustomData_add_layer(&me->ldata, CD_MLOOP, CD_CALLOC, NULL, totloop);
	CustomData_add_layer_named(&me->pdata, CD_MTEXPOLY, CD_CALLOC, NULL, totpoly, "UVMap");
	CustomData_add_layer_named(&me->ldata, CD_MLOOPUV, CD_CALLOC, NULL, totloop, "UVMap");
	BKE_mesh_update_customdata_pointers(me, false);
	for (int v = 0; v < totvert; v++)
		copy_v3_v3(me->mvert[v].co, &co[3 * v]);
	for (int f = 0; f < totpoly; f++) {
		me->mpoly[f].loopstart = 4 * f;
		me->mpoly[f].totloop = 4;
		me->mpoly[f].mat_nr = quad_mat[f];
		me->mpoly[f].flag = ME_SMOOTH;
		for (int k = 0; k < 4; k++) {
			const int v = quads[4 * f + k];
			me->mloop[4 * f + k].v = (unsigned int)v;
			me->mloopuv[4 * f + k].uv[0] = uv[2 * v];
			me->mloopuv[4 * f + k].uv[1] = uv[2 * v + 1];
		}
	}
	me->totcol = (short)mats.size();
	me->mat = (Material **)MEM_callocN(sizeof(Material *) * mats.size(), "hair materials");
	for (size_t m = 0; m < mats.size(); m++) {
		me->mat[m] = (mats[m] == &defmaterial) ? NULL : mats[m];
		if (me->mat[m])
			id_us_plus(&me->mat[m]->id);
	}
	BKE_mesh_calc_edges(me, false, false);
	BKE_mesh_calc_normals(me);
	converter->RegisterTemporaryID(&me->id);

	RAS_MeshObject *meshobj = BL_ConvertMesh(me, NULL, kxscene, converter, libloading);
	static int said = 0;
	if (said < 4) {
		said++;
		printf("BGE-Remix hair %s: %d strands as %d ribbon faces\n", ob->id.name + 2, strands, totpoly);
	}
	return meshobj;
}

/* ------------------------------------------------------------------------- */
/* displacement (P8.22) */

static MTex *displace_mtex(Mesh *mesh, Object *ob, Material **r_ma)
{
	for (int c = 1; c <= std::max<int>(ob->totcol, mesh->totcol); c++) {
		Material *ma = give_current_material(ob, c);
		for (int t = 0; ma && t < MAX_MTEX; t++) {
			MTex *mtex = ma->mtex[t];
			if (mtex && mtex->tex && (mtex->mapto & MAP_DISPLACE) && (ma->septex & (1 << t)) == 0) {
				*r_ma = ma;
				return mtex;
			}
		}
	}
	return NULL;
}

Mesh *BL_DisplacedMesh(Object *ob, Mesh *mesh, Scene *scene, KX_BlenderSceneConverter *converter)
{
	Material *ma = NULL;
	MTex *mtex = (ob && mesh && mesh->totpoly > 0) ? displace_mtex(mesh, ob, &ma) : NULL;
	if (!mtex || !GPU_speed_feature("tessellation"))
		return NULL;

	/* finer: simple subdivision up to about 64,000 faces */
	int levels = 0;
	for (long faces = mesh->totpoly; levels < 5 && faces * 4 <= 65536; faces *= 4)
		levels++;
	DerivedMesh *dm = CDDM_from_mesh(mesh);
	if (levels > 0) {
		SubsurfModifierData smd;
		memset(&smd, 0, sizeof(smd));
		smd.levels = smd.renderLevels = (short)levels;
		smd.subdivType = ME_SIMPLE_SUBSURF;
		DerivedMesh *sub = subsurf_make_derived_from_derived(dm, &smd, NULL, (SubsurfFlags)0);
		DerivedMesh *cd = CDDM_copy(sub);
		sub->release(sub);
		dm->release(dm);
		dm = cd;
	}
	CDDM_calc_normals(dm);
	const int totvert = dm->getNumVerts(dm);
	MVert *mv = dm->getVertArray(dm);

	/* each vertex's texture coordinate: its first UV, or its place in the
	 * mesh's texture space (Generated) */
	std::vector<float> uvco(2 * (size_t)totvert, 0.0f);
	std::vector<char> has_uv((size_t)totvert, 0);
	const MLoopUV *luv = (const MLoopUV *)CustomData_get_layer(&dm->loopData, CD_MLOOPUV);
	if (luv && mtex->texco == TEXCO_UV) {
		const MLoop *ml = dm->getLoopArray(dm);
		const int totloop = dm->getNumLoops(dm);
		for (int l = 0; l < totloop; l++) {
			const unsigned int v = ml[l].v;
			if (!has_uv[v]) {
				uvco[2 * v] = luv[l].uv[0];
				uvco[2 * v + 1] = luv[l].uv[1];
				has_uv[v] = 1;
			}
		}
	}
	float loc[3], size[3];
	BKE_mesh_texspace_get(mesh, loc, NULL, size);
	for (int v = 0; v < totvert; v++) {
		float co[3];
		if (has_uv[v]) {
			co[0] = uvco[2 * v] * 2.0f - 1.0f;
			co[1] = uvco[2 * v + 1] * 2.0f - 1.0f;
			co[2] = 0.0f;
		}
		else {
			for (int k = 0; k < 3; k++)
				co[k] = (mv[v].co[k] - loc[k]) / (size[k] != 0.0f ? size[k] : 1.0f);
		}
		/* the slot's offset and size */
		for (int k = 0; k < 3; k++)
			co[k] = mtex->size[k] * (co[k] + mtex->ofs[k]);
		TexResult texres;
		memset(&texres, 0, sizeof(texres));
		BKE_texture_get_value(scene, mtex->tex, co, &texres, false);
		const float value = texres.tin;  /* (an RGB texture's: its channels' mean) */
		float no[3];
		normal_short_to_float_v3(no, mv[v].no);
		madd_v3_v3fl(mv[v].co, no, (value - 0.5f) * mtex->dispfac);
	}
	CDDM_calc_normals(dm);

	char name[MAX_ID_NAME];
	BLI_snprintf(name, sizeof(name), "BGE-Remix displaced %s", mesh->id.name + 2);
	Mesh *out = BKE_mesh_add(G.main, name);
	DM_to_mesh(dm, out, ob, CD_MASK_MESH, true);
	out->totcol = mesh->totcol;
	if (mesh->mat) {
		out->mat = (Material **)MEM_dupallocN(mesh->mat);
		for (int c = 0; c < out->totcol; c++) {
			if (out->mat[c])
				id_us_plus(&out->mat[c]->id);
		}
	}
	converter->RegisterTemporaryID(&out->id);
	static int said = 0;
	if (said < 4) {
		said++;
		printf("BGE-Remix displacement %s: %d levels, %d faces\n", ob->id.name + 2, levels, out->totpoly);
	}
	return out;
}
