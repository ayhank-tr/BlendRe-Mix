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

/** \file gpu_private.h
 *  \ingroup gpu
 */

#ifndef __GPU_PRIVATE_H__
#define __GPU_PRIVATE_H__

#include "BLI_sys_types.h"  /* bool */

/* call this before running any of the functions below */
void gpu_extensions_init(void);
void gpu_extensions_exit(void);

/* gpu_debug.c */
void gpu_debug_init(void);
void gpu_debug_exit(void);

/* BGE-Remix P8.3: gpu_shader_cache.c */
unsigned long long gpu_shader_cache_key(const char **parts, int num_parts);
bool gpu_shader_cache_enabled(void);
bool gpu_shader_cache_load(unsigned int program, unsigned long long key);
void gpu_shader_cache_save(unsigned int program, unsigned long long key);

/* BGE-Remix P8.1: gpu_glcheck.c */
void gpu_glcheck_init(void);
void gpu_glcheck_exit(void);

/* BGE-Remix P8.2: gpu_layer.c, gpu_builtin.c */
void gpu_layer_init(void);
void gpu_layer_exit(void);
void gpu_builtin_exit(void);
/* the layer's matrix helpers (column-major float[4][4]) */
void gpu_layer_m4_mul(float r[4][4], const float a[4][4], const float b[4][4]);
bool gpu_layer_m4_invert(float r[4][4], const float m[4][4]);
/* the material library compiles with the core front end (gpu_codegen.c) */
bool GPU_codegen_selftest_core(void);
/* the layer's self-test (gpu_selftest.c): the number of failed checks */
int GPU_layer_selftest(const char *dir);

/* gpu_lights.c, gpu_material.c (BGE-Remix P8 Part B, P8.16) */
void gpu_lights_views_get(int *r_count, const float **r_mats, int *r_layer);
bool gpu_scene_depth_is_on(void);
void gpu_world_mist_get(float r[9]);
struct Scene;
struct World;
bool gpu_world_cube_draw(struct Scene *scene, struct World *wo, bool linear, unsigned int cube, int face);
void gpu_ramp_blend_rgb(int type, float r_col[3], float fac, const float col[3]);

#endif  /* __GPU_PRIVATE_H__ */
