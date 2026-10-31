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

/** \file KX_Sprites.h
 *  \ingroup ketsji
 *
 * BGE-Remix P8 Part C (P8.20): halo materials and Blender particle systems
 * in the game's own picture (GPU_sprites.h does the drawing).
 *
 * Halo materials: each vertex of the object's mesh is a halo (its faces are
 * not drawn). Particle systems (Emitter type): simulated on the card from
 * the same description Remix's GPU particles get (KX_ParticleDescription),
 * drawn by their Render type: Halo, Billboard, Line; Object and Group
 * particles move copies of the object (or the group's objects), so they
 * are ordinary game objects (lit, shadowed, sent to Remix). Path and None
 * draw nothing.
 */

#ifndef __KX_SPRITES_H__
#define __KX_SPRITES_H__

#include <vector>

#include "MT_Point3.h"
#include "GPU_sprites.h"

class KX_Scene;
class KX_GameObject;
struct Object;
struct Scene;
struct ParticleSystem;
struct Material;

/* a particle system's description, shared by the game's simulation and
 * Remix's GPU particles (P4.6): rate from Amount over the emission frames,
 * lifetime, normal speed and its random spread, gravity, drag, size */
void KX_ParticleDescription(Scene *bscene, ParticleSystem *psys, float gravity, bool colliders, float bounce,
                            GPUParticleDesc *r_desc);

/* a halo material's look */
void KX_HaloStyle(const Material *ma, GPUHaloStyle *r_style);

/* the halo as a picture, size x size, its radius half the size (float
 * RGBA, the colour as Blender Internal adds it, alpha its cover) */
void KX_HaloPicture(const GPUHaloStyle *style, int size, float *r_rgba);

class KX_Sprites
{
public:
	KX_Sprites();
	~KX_Sprites();

	/* finds the scene's halos and particle systems (now and then) and moves
	 * the particles to the game's time */
	void Update(KX_Scene *scene, double time);
	/* draws them over the scene: pass 0 billboards (the transparent
	 * pass's), 1 halos and lines, 2 the halos' flares */
	void Draw(KX_Scene *scene, int pass);
	bool HasFlares(KX_Scene *scene) const;
	bool Empty() const { return m_halos.empty() && m_emitters.empty(); }
	/* remove_copies: the scene is still there (its Object particles go) */
	void Release(bool remove_copies = true);

	/* the frame's numbers (for the tests' logs) */
	int HaloCount() const;
	int EmitterCount() const { return (int)m_emitters.size(); }

private:
	struct HaloEntry {
		KX_GameObject *obj;   /* held */
		unsigned int points;
		int count;
		GPUHaloStyle style;
		bool seen;
	};
	struct EmitterEntry {
		KX_GameObject *obj;   /* held */
		ParticleSystem *psys;
		GPUParticles *gpu;
		int ren_as;
		GPUHaloStyle halo;
		float color[4];
		unsigned int texture;
		int align, split_x, split_y, split_mode;
		float line_back, line_front;
		bool line_by_speed;
		MT_Point3 last_pos;
		bool have_last;
		std::vector<KX_GameObject *> templates;  /* Object / Group: what is copied */
		std::vector<KX_GameObject *> copies;     /* held */
		bool seen;
	};
	std::vector<HaloEntry> m_halos;
	std::vector<EmitterEntry> m_emitters;
	std::vector<GPUParticleCollider> m_colliders;
	KX_Scene *m_scene;
	int m_scan;
	double m_last_time;
	bool m_have_time;

	void Scan(KX_Scene *scene);
	void Colliders(KX_Scene *scene, float *r_bounce);
	void MoveCopies(KX_Scene *scene, EmitterEntry &e);
	void FreeEmitter(KX_Scene *scene, EmitterEntry &e, bool remove_copies = true);
};

#endif  /* __KX_SPRITES_H__ */
