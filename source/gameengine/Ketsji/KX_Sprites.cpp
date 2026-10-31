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

/** \file gameengine/Ketsji/KX_Sprites.cpp
 *  \ingroup ketsji
 *
 * BGE-Remix P8 Part C (P8.20): halos and particles (KX_Sprites.h).
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "KX_Sprites.h"
#include "KX_Scene.h"
#include "KX_GameObject.h"
#include "EXP_ListValue.h"
#include "PHY_IPhysicsController.h"

#include "GPU_glew.h"
#include "GPU_texture.h"

extern "C" {
#include "BLI_math.h"
#include "BLI_utildefines.h"
#include "BLI_noise.h"
#include "BKE_cdderivedmesh.h"
#include "BKE_DerivedMesh.h"
#include "BKE_material.h"
#include "BKE_object.h"
#include "DNA_group_types.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_force.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"
}

/* ------------------------------------------------------------------------- */
/* shared with Remix (KX_RemixRenderer::DrawEmitters) */

void KX_ParticleDescription(Scene *bscene, ParticleSystem *psys, float gravity, bool colliders, float bounce,
                            GPUParticleDesc *r)
{
	GPU_particle_description(bscene, psys, gravity, colliders, bounce, r);
}

void KX_HaloStyle(const Material *ma, GPUHaloStyle *s)
{
	memset(s, 0, sizeof(*s));
	if (!ma) {
		s->color[0] = s->color[1] = s->color[2] = 1.0f;
		s->alpha = 1.0f;
		s->hard = 50.0f;
		s->size = 0.5f;
		return;
	}
	s->color[0] = ma->r;
	s->color[1] = ma->g;
	s->color[2] = ma->b;
	s->alpha = ma->alpha;
	s->add = ma->add;
	s->hard = (float)ma->har;
	s->size = ma->hasize;
	s->rings = (ma->mode & MA_HALO_RINGS) ? ma->ringc : 0;
	s->lines = (ma->mode & MA_HALO_LINES) ? ma->linec : 0;
	s->stars = (ma->mode & MA_STAR) ? ma->starc : 0;
	s->seed = (unsigned char)ma->seed1;
	s->xalpha = (ma->mode & MA_HALO_XALPHA) ? 1 : 0;
	s->line_color[0] = ma->specr;
	s->line_color[1] = ma->specg;
	s->line_color[2] = ma->specb;
	s->ring_color[0] = ma->mirr;
	s->ring_color[1] = ma->mirg;
	s->ring_color[2] = ma->mirb;
	s->flares = (ma->mode & MA_HALO_FLARE) ? ma->flarec : 0;
	s->flare_size = ma->flaresize;
	s->flare_boost = ma->flareboost;
	s->sub_size = ma->subsize;
	s->flare_seed = (unsigned char)ma->seed2;
}

/* Blender Internal's shadeHaloFloat (as the sprites' shader does it) */
void KX_HaloPicture(const GPUHaloStyle *s, int size, float *rgba)
{
	const unsigned char *perm;
	const float *hv, *hp;
	BLI_noise_tables(&perm, &hv, &hp);
	const float rad = 0.5f * (float)size, radsq = rad * rad;
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			float *out = &rgba[4 * (y * size + x)];
			out[0] = out[1] = out[2] = out[3] = 0.0f;
			const float xn = (float)x + 0.5f - rad, yn = (float)y + 0.5f - rad;
			float dist = xn * xn + yn * yn;
			if (dist >= radsq)
				continue;
			const float radist = sqrtf(dist);
			float ringf = 0.0f, linef = 0.0f;
			int ofs = s->seed;
			for (int a = s->rings; a > 0; a--, ofs += 2) {
				const float fac = fabsf(hv[(ofs + 1) % 768] * (rad * fabsf(hv[ofs % 768]) - radist));
				if (fac < 1.0f)
					ringf += 1.0f - fac;
			}
			dist /= radsq;
			if (s->hard >= 30.0f) {
				dist = sqrtf(dist);
				if (s->hard >= 40.0f) {
					dist = sinf(dist * (float)M_PI_2);
					if (s->hard >= 50.0f)
						dist = sqrtf(dist);
				}
			}
			else if (s->hard < 20.0f)
				dist *= dist;
			dist = (dist < 1.0f) ? 1.0f - dist : 0.0f;
			if (s->lines > 0) {
				ofs = s->seed;
				for (int a = s->lines; a > 0; a--, ofs += 3) {
					const float fac = fabsf(xn * hv[ofs % 768] + yn * hv[(ofs + 1) % 768]);
					if (fac < 1.0f)
						linef += 1.0f - fac;
				}
				linef *= dist;
			}
			if (s->stars > 0) {
				float angle = atan2f(yn, xn) * (1.0f + 0.25f * (float)s->stars);
				const float co = cosf(angle), si = sinf(angle);
				float ster = fabsf((co * xn + si * yn) * (co * yn - si * xn));
				if (ster > 1.0f) {
					ster = rad / ster;
					if (ster < 1.0f)
						dist *= sqrtf(ster);
				}
			}
			if (dist <= 0.00001f)
				continue;
			dist *= s->alpha;
			ringf *= dist;
			linef *= s->alpha;
			for (int k = 0; k < 3; k++)
				out[k] = dist * s->color[k] + linef * s->line_color[k] + ringf * s->ring_color[k];
			out[3] = s->xalpha ? dist * dist + linef * linef + ringf * ringf : dist + linef + ringf;
			out[3] = std::min(out[3], 1.0f);
		}
	}
}

/* ------------------------------------------------------------------------- */

KX_Sprites::KX_Sprites()
    : m_scene(NULL), m_scan(0), m_last_time(0.0), m_have_time(false)
{
}

KX_Sprites::~KX_Sprites()
{
	Release();
}

static void object_matrix(KX_GameObject *obj, float r_m[4][4])
{
	obj->GetOpenGLMatrix();
	const float *m = obj->GetOpenGLMatrixPtr()->getPointer();
	memcpy(r_m, m, sizeof(float) * 16);
}

static Material *halo_material(Object *ob)
{
	for (int c = 1; c <= ob->totcol; c++) {
		Material *ma = give_current_material(ob, c);
		if (ma && ma->material_type == MA_TYPE_HALO)
			return ma;
	}
	return NULL;
}

static bool particles_drawn(ParticleSystem *psys)
{
	ParticleSettings *part = psys->part;
	if (!part || part->type != PART_EMITTER || (psys->flag & PSYS_DISABLED) || part->totpart <= 0)
		return false;
	return ELEM(part->ren_as, PART_DRAW_HALO, PART_DRAW_BB, PART_DRAW_LINE, PART_DRAW_OB, PART_DRAW_GR);
}

/* the emitter's triangles in object space (9 floats each) */
static void emitter_triangles(Object *ob, std::vector<float> &tris)
{
	tris.clear();
	if (ob->type != OB_MESH || !ob->data)
		return;
	DerivedMesh *dm = CDDM_from_mesh((Mesh *)ob->data);
	DM_ensure_tessface(dm);
	MVert *mv = dm->getVertArray(dm);
	MFace *mf = dm->getTessFaceArray(dm);
	const int n = dm->getNumTessFaces(dm);
	for (int f = 0; f < n; f++) {
		const unsigned int v[4] = {mf[f].v1, mf[f].v2, mf[f].v3, mf[f].v4};
		const int k = mf[f].v4 ? 2 : 1;
		for (int t = 0; t < k; t++) {
			const unsigned int a = v[0], b = v[t + 1], c = v[t + 2];
			tris.insert(tris.end(), mv[a].co, mv[a].co + 3);
			tris.insert(tris.end(), mv[b].co, mv[b].co + 3);
			tris.insert(tris.end(), mv[c].co, mv[c].co + 3);
		}
	}
	/* a mesh without faces: its vertices as tiny triangles */
	if (tris.empty()) {
		const int nv = dm->getNumVerts(dm);
		for (int i = 0; i < nv; i++) {
			for (int k = 0; k < 3; k++)
				tris.insert(tris.end(), mv[i].co, mv[i].co + 3);
		}
	}
	dm->release(dm);
}

static KX_GameObject *find_template(KX_Scene *scene, Object *ob)
{
	if (!ob)
		return NULL;
	CValue *v = scene->GetInactiveList()->FindValue(ob->id.name + 2);
	if (!v)
		v = scene->GetObjectList()->FindValue(ob->id.name + 2);
	return static_cast<KX_GameObject *>(v);
}

void KX_Sprites::Colliders(KX_Scene *scene, float *r_bounce)
{
	m_colliders.clear();
	*r_bounce = 0.5f;
	CListValue *objects = scene->GetObjectList();
	for (int i = 0; i < objects->GetCount(); i++) {
		KX_GameObject *obj = (KX_GameObject *)objects->GetValue(i);
		Object *ob = obj->GetBlenderObject();
		if (!ob || !ob->pd || !ob->pd->deflect)
			continue;
		BoundBox *bb = BKE_object_boundbox_get(ob);
		float lo[3] = {-1.0f, -1.0f, -1.0f}, hi[3] = {1.0f, 1.0f, 1.0f};
		if (bb) {
			INIT_MINMAX(lo, hi);
			for (int k = 0; k < 8; k++)
				minmax_v3v3_v3(lo, hi, bb->vec[k]);
		}
		GPUParticleCollider c;
		float m[4][4], centre[3], half[3];
		object_matrix(obj, m);
		mid_v3_v3v3(centre, lo, hi);
		sub_v3_v3v3(half, hi, lo);
		mul_v3_fl(half, 0.5f);
		float t[4][4];
		unit_m4(t);
		copy_v3_v3(t[3], centre);
		mul_m4_m4m4(c.obmat, m, t);
		copy_v3_v3(c.half, half);
		c.shape = (half[2] < 0.01f) ? 2 : 0;
		m_colliders.push_back(c);
		*r_bounce = std::min(std::max(1.0f - ob->pd->pdef_damp, 0.0f), 1.0f);
	}
}

void KX_Sprites::FreeEmitter(KX_Scene *scene, EmitterEntry &e, bool remove_copies)
{
	GPU_particles_free(e.gpu);
	e.gpu = NULL;
	for (size_t k = 0; k < e.copies.size(); k++) {
		if (remove_copies && scene && e.copies[k]->GetSGNode())
			scene->DelayedRemoveObject(e.copies[k]);
		e.copies[k]->Release();
	}
	e.copies.clear();
	e.obj->Release();
}

void KX_Sprites::Release(bool remove_copies)
{
	for (size_t i = 0; i < m_halos.size(); i++) {
		GPU_halo_points_free(m_halos[i].points);
		m_halos[i].obj->Release();
	}
	m_halos.clear();
	for (size_t i = 0; i < m_emitters.size(); i++)
		FreeEmitter(m_scene, m_emitters[i], remove_copies);
	m_emitters.clear();
	m_colliders.clear();
	m_scene = NULL;
	m_have_time = false;
}

void KX_Sprites::Scan(KX_Scene *scene)
{
	Scene *bscene = scene->GetBlenderScene();
	const float gravity = bscene ? std::fabs(bscene->gm.gravity) : 9.81f;
	float bounce = 0.5f;
	Colliders(scene, &bounce);

	for (size_t i = 0; i < m_halos.size(); i++)
		m_halos[i].seen = false;
	for (size_t i = 0; i < m_emitters.size(); i++)
		m_emitters[i].seen = false;

	CListValue *objects = scene->GetObjectList();
	for (int i = 0; i < objects->GetCount(); i++) {
		KX_GameObject *obj = (KX_GameObject *)objects->GetValue(i);
		Object *ob = obj->GetBlenderObject();
		if (!ob || ob->type != OB_MESH)
			continue;
		/* a halo material: its vertices */
		if (Material *ma = (obj->GetMeshCount() > 0) ? halo_material(ob) : NULL) {
			bool known = false;
			for (size_t k = 0; k < m_halos.size() && !known; k++) {
				if (m_halos[k].obj == obj) {
					m_halos[k].seen = known = true;
					KX_HaloStyle(ma, &m_halos[k].style);  /* (a script may change it) */
				}
			}
			Mesh *me = (Mesh *)ob->data;
			if (!known && me && me->totvert > 0) {
				HaloEntry h;
				std::vector<float> co(3 * (size_t)me->totvert);
				for (int v = 0; v < me->totvert; v++)
					copy_v3_v3(&co[3 * v], me->mvert[v].co);
				h.points = GPU_halo_points_create((const float (*)[3])&co[0], me->totvert);
				if (h.points) {
					h.obj = obj;
					obj->AddRef();
					h.count = me->totvert;
					KX_HaloStyle(ma, &h.style);
					h.seen = true;
					m_halos.push_back(h);
				}
			}
		}
		/* particle systems */
		for (ParticleSystem *psys = (ParticleSystem *)ob->particlesystem.first; psys; psys = psys->next) {
			if (!particles_drawn(psys))
				continue;
			bool known = false;
			for (size_t k = 0; k < m_emitters.size() && !known; k++) {
				if (m_emitters[k].obj == obj && m_emitters[k].psys == psys)
					m_emitters[k].seen = known = true;
			}
			if (known)
				continue;
			ParticleSettings *part = psys->part;
			GPUParticleDesc desc;
			KX_ParticleDescription(bscene, psys, gravity, !m_colliders.empty(), bounce, &desc);
			std::vector<float> tris;
			emitter_triangles(ob, tris);
			if (tris.empty())
				continue;
			unsigned int seed = (unsigned int)psys->seed * 2654435761u;
			for (const char *c = psys->name; *c; c++)
				seed = seed * 31u + (unsigned char)*c;
			EmitterEntry e;
			e.gpu = GPU_particles_create(&desc, &tris[0], (int)(tris.size() / 9), seed);
			if (!e.gpu)
				continue;
			e.obj = obj;
			obj->AddRef();
			e.psys = psys;
			e.ren_as = part->ren_as;
			Material *ma = give_current_material(ob, part->omat);
			KX_HaloStyle(ma, &e.halo);
			/* a particle halo: the material's halo size for the particle's size */
			e.halo.size = (ma ? ma->hasize : 0.5f) / std::max(part->size, 1e-4f);
			e.color[0] = ma ? ma->r : 1.0f;
			e.color[1] = ma ? ma->g : 1.0f;
			e.color[2] = ma ? ma->b : 1.0f;
			e.color[3] = ma ? ma->alpha : 1.0f;
			e.texture = 0;
			for (int t = 0; ma && t < MAX_MTEX && !e.texture; t++) {
				MTex *mtex = ma->mtex[t];
				if (mtex && mtex->tex && mtex->tex->type == TEX_IMAGE && mtex->tex->ima) {
					GPUTexture *tex = GPU_texture_from_blender(mtex->tex->ima, &mtex->tex->iuser, GL_TEXTURE_2D, false,
					                                           0.0, 1);
					if (tex)
						e.texture = (unsigned int)GPU_texture_opengl_bindcode(tex);
				}
			}
			e.align = (part->bb_align == PART_BB_Z) ? 1 : (part->bb_align == PART_BB_VEL) ? 2 : 0;
			e.split_x = e.split_y = std::max<int>(part->bb_uv_split, 1);
			e.split_mode = (part->bb_anim == PART_BB_ANIM_AGE) ? 1 :
			               (part->bb_split_offset == PART_BB_OFF_RANDOM) ? 2 : 0;
			e.line_back = part->draw_line[0];
			e.line_front = part->draw_line[1];
			e.line_by_speed = (part->draw & PART_DRAW_VEL_LENGTH) != 0;
			e.have_last = false;
			if (part->ren_as == PART_DRAW_OB) {
				if (KX_GameObject *t = find_template(scene, part->dup_ob))
					e.templates.push_back(t);
			}
			else if (part->ren_as == PART_DRAW_GR && part->dup_group) {
				for (GroupObject *go = (GroupObject *)part->dup_group->gobject.first; go; go = go->next) {
					if (KX_GameObject *t = find_template(scene, go->ob))
						e.templates.push_back(t);
				}
			}
			/* "Emitter" off in the Render panel: the emitter is not drawn */
			if (!(part->draw & PART_DRAW_EMITTER))
				obj->SetVisible(false, false);
			e.seen = true;
			m_emitters.push_back(e);
		}
	}
	/* those whose objects are gone */
	for (size_t k = m_halos.size(); k-- > 0;) {
		if (!m_halos[k].seen) {
			GPU_halo_points_free(m_halos[k].points);
			m_halos[k].obj->Release();
			m_halos.erase(m_halos.begin() + k);
		}
	}
	for (size_t k = m_emitters.size(); k-- > 0;) {
		if (!m_emitters[k].seen) {
			FreeEmitter(scene, m_emitters[k]);
			m_emitters.erase(m_emitters.begin() + k);
		}
	}
	static int said = 0;
	if (said < 2 && (!m_halos.empty() || !m_emitters.empty())) {
		said++;
		printf("BGE-Remix sprites: %d halo objects (%d halos), %d particle systems\n", (int)m_halos.size(),
		       HaloCount(), (int)m_emitters.size());
	}
}

int KX_Sprites::HaloCount() const
{
	int n = 0;
	for (size_t i = 0; i < m_halos.size(); i++)
		n += m_halos[i].count;
	return n;
}

void KX_Sprites::MoveCopies(KX_Scene *scene, EmitterEntry &e)
{
	if (e.templates.empty())
		return;
	const int max = std::min(GPU_particles_max(e.gpu), 1024);
	std::vector<float> places(4 * (size_t)max);
	const int n = GPU_particles_read(e.gpu, (float (*)[4])&places[0], max);
	std::vector<int> live;
	for (int i = 0; i < n; i++) {
		if (places[4 * i + 3] > 0.0f)
			live.push_back(i);
	}
	/* as many copies as live particles */
	while (e.copies.size() < live.size()) {
		KX_GameObject *t = e.templates[e.copies.size() % e.templates.size()];
		KX_GameObject *c = static_cast<KX_GameObject *>(scene->AddReplicaObject(t, e.obj, 0));
		if (!c)
			break;
		c->AddRef();
		if (c->GetPhysicsController())
			c->GetPhysicsController()->SuspendDynamics(true);
		e.copies.push_back(c);
	}
	while (e.copies.size() > live.size()) {
		KX_GameObject *c = e.copies.back();
		e.copies.pop_back();
		if (c->GetSGNode())
			scene->DelayedRemoveObject(c);
		c->Release();
	}
	const float base = std::max(e.psys->part->size, 1e-4f);
	for (size_t k = 0; k < e.copies.size(); k++) {
		KX_GameObject *c = e.copies[k];
		if (!c->GetSGNode())
			continue;
		const float *p = &places[4 * live[k]];
		KX_GameObject *t = e.templates[k % e.templates.size()];
		c->NodeSetLocalPosition(MT_Point3(p[0], p[1], p[2]));
		c->NodeSetLocalScale(t->NodeGetWorldScaling() * (p[3] / base));
		c->NodeUpdateGS(0.0);
	}
}

void KX_Sprites::Update(KX_Scene *scene, double time)
{
	if (scene != m_scene) {
		/* (another scene: the old one may be gone) */
		Release(false);
		m_scene = scene;
		m_scan = 0;
	}
	if ((m_scan++ % 60) == 0)
		Scan(scene);
	if (m_emitters.empty())
		return;
	const float dt = m_have_time ? (float)std::max(time - m_last_time, 0.0) : 0.0f;
	m_last_time = time;
	m_have_time = true;
	for (size_t i = 0; i < m_emitters.size(); i++) {
		EmitterEntry &e = m_emitters[i];
		float m[4][4];
		object_matrix(e.obj, m);
		const MT_Point3 pos = e.obj->NodeGetWorldPosition();
		float vel[3] = {0.0f, 0.0f, 0.0f};
		if (e.have_last && dt > 1e-6f) {
			for (int k = 0; k < 3; k++)
				vel[k] = (float)((pos[k] - e.last_pos[k]) / dt);
		}
		e.last_pos = pos;
		e.have_last = true;
		GPU_particles_step(e.gpu, time, dt, m, vel, m_colliders.empty() ? NULL : &m_colliders[0],
		                   (int)m_colliders.size());
		if (e.ren_as == PART_DRAW_OB || e.ren_as == PART_DRAW_GR)
			MoveCopies(scene, e);
	}
}

bool KX_Sprites::HasFlares(KX_Scene *scene) const
{
	if (scene != m_scene)
		return false;
	for (size_t i = 0; i < m_halos.size(); i++) {
		if (m_halos[i].style.flares > 0)
			return true;
	}
	for (size_t i = 0; i < m_emitters.size(); i++) {
		if (m_emitters[i].ren_as == PART_DRAW_HALO && m_emitters[i].halo.flares > 0)
			return true;
	}
	return false;
}

void KX_Sprites::Draw(KX_Scene *scene, int pass)
{
	if (scene != m_scene)
		return;
	const bool flares = (pass == 2);
	for (size_t i = 0; i < m_halos.size() && pass != 0; i++) {
		HaloEntry &h = m_halos[i];
		if (!h.obj->GetVisible())
			continue;
		float m[4][4];
		object_matrix(h.obj, m);
		GPU_halos_draw(h.points, h.count, m, &h.style, flares);
	}
	for (size_t i = 0; i < m_emitters.size(); i++) {
		EmitterEntry &e = m_emitters[i];
		if (e.ren_as == PART_DRAW_HALO) {
			if (pass != 0)
				GPU_particles_draw_halos(e.gpu, &e.halo, flares);
		}
		else if (e.ren_as == PART_DRAW_BB && pass == 0)
			GPU_particles_draw_billboards(e.gpu, e.color, e.texture, e.align, e.split_x, e.split_y, e.split_mode);
		else if (e.ren_as == PART_DRAW_LINE && pass == 1)
			GPU_particles_draw_lines(e.gpu, e.color, e.line_back, e.line_front, e.line_by_speed);
	}
}
