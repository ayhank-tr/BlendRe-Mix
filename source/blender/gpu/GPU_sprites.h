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

/** \file GPU_sprites.h
 *  \ingroup gpu
 *
 * BGE-Remix P8 Part C (P8.20): halos and particles in the game's picture.
 *
 * Halos: Blender Internal's halo (shadeHaloFloat) in a sprite's shader:
 * hardness, Add, rings, lines and stars, and its lens flares (renderflare),
 * which fade as the scene's depth hides the halo. Points come from a halo
 * material's mesh or from particles.
 *
 * Particles: emitted and moved by a compute pass from the same description
 * the game sends to Remix's GPU particles (rate, lifetime, start speed and
 * spread, gravity, drag, bouncing off collision shapes), then drawn as
 * halos, textured billboards or lines; Object and Group particles read
 * their places back for the game's own objects.
 */

#ifndef __GPU_SPRITES_H__
#define __GPU_SPRITES_H__

#include "BLI_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* a halo material's look (Blender Internal's names in comments) */
typedef struct GPUHaloStyle {
	float color[3];        /* r, g, b */
	float alpha;           /* alpha */
	float add;             /* add (0..1) */
	float hard;            /* har */
	float size;            /* hasize, world units */
	int rings, lines, stars;  /* ringc, linec, starc when their switches are on (else 0) */
	int seed;              /* seed1 */
	int xalpha;            /* MA_HALO_XALPHA */
	float line_color[3];   /* spec colour */
	float ring_color[3];   /* mirror colour */
	int flares;            /* flarec when Flare is on (else 0) */
	float flare_size, flare_boost, sub_size;  /* flaresize, flareboost, subsize */
	int flare_seed;        /* seed2 */
} GPUHaloStyle;

/* points for halos (object space): a buffer to give GPU_halos_draw */
unsigned int GPU_halo_points_create(const float (*co)[3], int count);
void GPU_halo_points_free(unsigned int buffer);

/* draws count halos (or their flares) at the points with the object's
 * matrix and the current view and projection; the flares read the scene's
 * depth (when it was drawn) to fade as the halo is hidden */
void GPU_halos_draw(unsigned int points, int count, const float model[4][4], const GPUHaloStyle *style,
                    bool flares);

/* ---- particles ---- */

/* what the game sends to Remix's GPU particles (bge_remix_bridge.h,
 * brb_particle_desc), with the same meaning */
typedef struct GPUParticleDesc {
	int max_particles;
	float spawn_rate;            /* per second */
	float lifetime_min, lifetime_max;
	float speed_normal;
	float cone_angle_deg;
	float speed_from_motion;
	float gravity;
	float drag;
	float size_min, size_max;
	float size_end_scale;
	float color_start[4], color_end[4];
	float rotation_speed;        /* turns per second */
	int collide;
	float restitution;
} GPUParticleDesc;

/* a box (half sizes), a sphere (radius in half[0]) or a plane (its local
 * XY at Z 0, Z up) the particles bounce off */
typedef struct GPUParticleCollider {
	float obmat[4][4];
	float half[3];
	int shape;                   /* 0 box, 1 sphere, 2 plane */
} GPUParticleCollider;

/* a Blender particle system's description, shared by the game's
 * simulation and Remix's GPU particles (the game's and the editor's):
 * rate from Amount over the emission frames, lifetime, normal speed and
 * its random spread, gravity (the scene's, times the system's weight),
 * drag, size, a fade out at the end of life */
struct Scene;
struct ParticleSystem;
void GPU_particle_description(const struct Scene *scene, const struct ParticleSystem *psys, float gravity,
                              bool colliders, float bounce, GPUParticleDesc *r_desc);

typedef struct GPUParticles GPUParticles;

/* tris: the emitter's triangles in object space (9 floats each) */
GPUParticles *GPU_particles_create(const GPUParticleDesc *desc, const float *tris, int tri_count, unsigned int seed);
void GPU_particles_free(GPUParticles *ps);
int GPU_particles_max(const GPUParticles *ps);

/* moves them to "time" (seconds since the game began; dt since the last
 * step) with the emitter where it is now (model) and its velocity */
void GPU_particles_step(GPUParticles *ps, double time, float dt, const float model[4][4], const float velocity[3],
                        const GPUParticleCollider *colliders, int collider_count);

/* draws them as halos */
void GPU_particles_draw_halos(GPUParticles *ps, const GPUHaloStyle *style, bool flares);

/* as billboards: align 0 facing the view, 1 upright (around Z), 2 along the
 * velocity; texture 0 for none; split: the picture's grid (columns, rows)
 * and how a particle picks its cell (0 the first, 1 by age, 2 at random) */
void GPU_particles_draw_billboards(GPUParticles *ps, const float color[4], unsigned int texture, int align,
                                   int split_x, int split_y, int split_mode);

/* as lines from "back" behind to "front" ahead of each (times its speed
 * when by_speed) */
void GPU_particles_draw_lines(GPUParticles *ps, const float color[4], float back, float front, bool by_speed);

/* the places of the live ones (xyz world, w their size; w <= 0 for the
 * rest): up to max, returns how many were written (a short stall: for
 * Object and Group particles, which are few) */
int GPU_particles_read(GPUParticles *ps, float (*r_places)[4], int max);

void GPU_sprites_exit(void);

#ifdef __cplusplus
}
#endif

#endif  /* __GPU_SPRITES_H__ */
