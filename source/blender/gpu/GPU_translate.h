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

/** \file GPU_translate.h
 *  \ingroup gpu
 *
 * BGE-Remix P8.3: users' old GLSL (custom shaders, 2D filters, written for
 * GLSL 1.10/1.20 with built-ins such as gl_ModelViewMatrix, gl_TexCoord or
 * gl_LightSource) translated to GLSL 4.50. The built-ins become bge_ names;
 * a front end maps them back to the old built-ins while the old drawing code
 * remains (compatibility), or declares them for the drawing layer (core).
 */

#ifndef __GPU_TRANSLATE_H__
#define __GPU_TRANSLATE_H__

#include "BLI_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	GPU_TRANSLATE_VERTEX = 0,
	GPU_TRANSLATE_FRAGMENT = 1,
};

/* the stage ready to compile (version, extensions, front end, translated
 * text; MEM_freeN it); r_problems: built-ins it could not translate, each
 * reported with its line under "who" */
char *GPU_glsl_translate(const char *source, int stage, bool core, const char *who, int *r_problems);

/* translates and builds a program from old GLSL (vert may be NULL for a 2D
 * filter's fragment shader), and checks the translation also builds with
 * the core front end; 0 when it did not build (the caller then builds the
 * original, as stock Blender does) */
unsigned int GPU_glsl_translate_program(const char *vert, const char *frag, const char *who);
/* BGE-Remix P8 Part B: "game": the program is a custom shader or 2D filter
 * the game draws, built from the core translation (the compatibility one
 * when that does not build); a core 2D filter reads attribute 0 (bge_pos)
 * and the uniform bge_canvascoord */
unsigned int GPU_glsl_translate_program_ex(const char *vert, const char *frag, const char *who, bool game);

/* a user's name as the translation spells it (names GLSL 4.50 reserves,
 * such as "texture" or "sample", get a suffix) */
const char *GPU_glsl_translated_name(const char *name, char *buf, int buflen);
/* uniform and attribute places by the user's name, in translated or
 * original programs */
int GPU_glsl_uniform_location(unsigned int program, const char *name);
int GPU_glsl_attrib_location(unsigned int program, const char *name);

#ifdef __cplusplus
}
#endif

#endif  /* __GPU_TRANSLATE_H__ */
