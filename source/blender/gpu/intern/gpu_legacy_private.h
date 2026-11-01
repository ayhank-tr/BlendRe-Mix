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

/** \file blender/gpu/intern/gpu_legacy_private.h
 *  \ingroup gpu
 *
 * BGE-Remix P8.26: what the OpenGL emulation's files share (included with
 * GPU_LEGACY_NO_REDIRECT, so the real functions are called).
 */

#ifndef __GPU_LEGACY_PRIVATE_H__
#define __GPU_LEGACY_PRIVATE_H__

#ifdef __cplusplus
extern "C" {
#endif

extern int GPU_lgl_on;

/* the per-vertex calls, every typed form ends in one of these */
void lgl_vertex4f(float x, float y, float z, float w);
void lgl_color4f(float r, float g, float b, float a);
void lgl_color4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void lgl_normal3f(float x, float y, float z);
void lgl_texcoord4f(int unit, float s, float t, float r, float q);
void lgl_attrib4f(unsigned int index, float x, float y, float z, float w);
void lgl_rasterpos4f(float x, float y, float z, float w);
void lgl_rect4f(float x1, float y1, float x2, float y2);
void lgl_evalcoord1f(float u);

const char **gpu_lgl_unsupported_names(void);

/* GPU_caps_init: the emulation on or off for this run */
void gpu_lgl_init(int core_profile);
void gpu_lgl_exit(void);

/* GPU_draw_stats: the emulation's draws, immediate-mode vertices, bytes it
 * streamed and the pieces large blocks were drawn in */
void gpu_lgl_stats(double *r_draws, double *r_vertices, double *r_bytes, double *r_pieces, int reset);

#ifdef __cplusplus
}
#endif

#endif  /* __GPU_LEGACY_PRIVATE_H__ */
