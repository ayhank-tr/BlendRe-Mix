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

/** \file GPU_lights.h
 *  \ingroup gpu
 *
 * BGE-Remix P8 Part B (P8.15, P8.16): what every material's lamps share.
 *
 * The shadow pool: one depth texture array holding every lamp's shadow
 * views (a spot lamp's one, a cube's six faces, the sun's four cascades),
 * one layer each, all of one size. A lamp's views are drawn in one pass
 * where the card lets the vertex stage choose the layer (each view an
 * instance; GPU_shadow_layered), otherwise one view at a time. Materials
 * read the pool through bge_shadowpool, on a texture unit kept for it.
 *
 * The clustered lamps: every lamp as one record in a storage buffer; a
 * compute pass sorts them into 16 x 9 x 24 clusters of the view each time
 * the view changes, and the material shaders loop over the lamps of the
 * pixel's cluster.
 */

#ifndef __GPU_LIGHTS_H__
#define __GPU_LIGHTS_H__

#include "BLI_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the shadow pool ---- */

/* a lamp's views in one pass (the vertex stage picks the layer; Tier 2, and
 * the "layered_shadows" speed switch on) */
bool GPU_shadow_layered(void);

/* the layers' side in texels (0 before the first lamp) */
int GPU_shadow_pool_size(void);

/* counted on whenever the pool is made again (every map must be drawn again) */
unsigned int GPU_shadow_pool_generation(void);

/* "views" layers for a lamp that wants maps of "size" texels: the first
 * layer, or -1. The pool may be made again (larger) for it. */
int GPU_shadow_pool_alloc(int views, int size);
void GPU_shadow_pool_free(int layer, int views);

/* drawing a lamp's views: begin clears its layers and binds the pool;
 * layered: every layer at once (the views as instances, see
 * GPU_shadow_views_set), otherwise GPU_shadow_pool_view picks each one.
 * depth_clamp: casters before the near plane still cast (the sun) */
void GPU_shadow_pool_begin(int layer, int views, bool layered, bool depth_clamp);
void GPU_shadow_pool_view(int layer);
void GPU_shadow_pool_end(void);

/* a 2D texture of one layer (Python's shadowBindId); made again with the
 * pool. *r_view and *r_gen are the caller's, kept between calls. */
int GPU_shadow_pool_layer_texture(int layer, unsigned int *r_view, unsigned int *r_gen);

/* reads one layer's depths (w = h = GPU_shadow_pool_size) */
void GPU_shadow_pool_read(int layer, float *r_depth);

/* the views drawn in the pass that follows: count 0 (one view, as set by
 * the matrices) or the views of a layered pass, each a matrix from the
 * first view's clip space to its own, into layer "layer" + view */
void GPU_shadow_views_set(int count, const float (*clip_from_first)[4][4], int layer);
int GPU_shadow_views_count(void);

/* a shadow map is being drawn (the materials' lighting is skipped; they
 * only cut out) */
void GPU_shadow_pass_set(bool on);
bool GPU_shadow_pass(void);

/* the units the shared textures sit on (the last two of the fragment stage) */
int GPU_lights_unit_shadowpool(void);
int GPU_lights_unit_scenedepth(void);

/* binds the shared textures: the pool (a blank one while a map is drawn)
 * and the scene's depth (contact shadows) */
void GPU_lights_bind_textures(unsigned int scenedepth_texture);

/* a new program: its shared samplers on their units */
void GPU_lights_program_samplers(unsigned int program);

void GPU_lights_exit(void);

#ifdef __cplusplus
}
#endif

#endif  /* __GPU_LIGHTS_H__ */
