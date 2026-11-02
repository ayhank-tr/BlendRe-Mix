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

/** \file GPU_probes.h
 *  \ingroup gpu
 *
 * BGE-Remix P8 Part B (P8.18): reflections.
 *
 * Reflection probes: each a cube in one cube-map array (its six faces drawn
 * from the probe's place, then blurred by roughness into the smaller mip
 * levels). Materials reflect the nearest probe that reaches them (the probe
 * list in a storage buffer, binding 14), or the sky (the world drawn into a
 * small cube), or the horizon and zenith colours.
 *
 * Planar mirrors: a mirror object's view of the scene, drawn mirrored with a
 * clip plane into a picture whose mip levels blur it; materials with the
 * mirror switch read it at the pixel.
 *
 * Screen-space reflections (Modern scenes): materials trace the reflected
 * ray through the scene's depth and take the colour the last frame showed
 * where it hits.
 */

#ifndef __GPU_PROBES_H__
#define __GPU_PROBES_H__

#include "BLI_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Scene;
struct World;

/* ---- probes ---- */

/* a probe's place in the array (-1: none left) and its release */
int GPU_probe_alloc(void);
void GPU_probe_free(int probe);
/* where it is and how far it reaches (world units); drawn: its faces hold a
 * picture (until then materials skip it) */
void GPU_probe_place(int probe, const float center[3], float reach, bool drawn);
/* drawing one face: binds its target (cleared), gives the camera's view
 * and projection (90 degrees, near 0.05, far the reach) */
void GPU_probe_face_begin(int probe, int face, float r_view[4][4], float r_proj[4][4], int r_size[1]);
void GPU_probe_face_end(void);
/* the face pictures blurred by roughness into the mip levels */
void GPU_probe_filter(int probe);
/* a face's size in texels (BGE_REMIX_PROBE_SIZE, 128 by default) */
int GPU_probe_size(void);

/* ---- the sky ---- */

/* the world drawn into the sky cube (the game's world shader); none: the
 * horizon and zenith colours are reflected */
void GPU_sky_cube_update(struct Scene *scene, struct World *wo);
void GPU_sky_cube_clear(void);

/* ---- planar mirrors ---- */

/* a mirror's picture of width x height (made again when the size changes):
 * begin binds it (cleared) and the clip plane (eye space of the mirrored
 * camera: a x + b y + c z + d >= 0 is kept), end makes its mip levels.
 * "mirror" numbers up to GPU_MIRRORS pictures. */
#define GPU_MIRRORS 4
void GPU_mirror_begin(int mirror, int width, int height, const float plane[4]);
void GPU_mirror_end(int mirror);
/* the picture the next draws read (-1: none) */
void GPU_mirror_use(int mirror);
/* the clip plane of the drawing that follows (NULL: none) */
void GPU_clip_plane_set(const float plane[4]);
bool GPU_clip_plane(float r_plane[4]);

/* ---- screen-space reflections ---- */

/* the colour of the frame drawn (scene linear), kept for the next frame's
 * traces; on: Modern scenes with reflections on */
void GPU_ssr_history_store(unsigned int framebuffer, int width, int height);
void GPU_ssr_set(bool on);
/* the materials trace (on, and a frame kept); wanted: on (the frame is kept
 * from the first one) */
bool GPU_ssr(void);
bool GPU_ssr_wanted(void);

/* ---- for the materials ---- */

int GPU_probes_unit_cube(void);
int GPU_probes_unit_sky(void);
int GPU_probes_unit_mirror(void);
int GPU_probes_unit_history(void);
/* binds the probe list, the probes, the sky, the mirror and the history */
void GPU_probes_bind(void);
void GPU_probes_program_samplers(unsigned int program);
/* for the old built-ins: mirror on, its picture's size, sky on, the clip
 * plane, reflections traced */
void GPU_probes_values(int *r_mirror, float r_mirror_size[2], int *r_sky, float r_plane[4], int *r_ssr);

void GPU_probes_exit(void);

#ifdef __cplusplus
}
#endif

#endif  /* __GPU_PROBES_H__ */
