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
 * The Original Code is Copyright (C) 2014 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Jason Wilkins
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file glew-mx.c
 *  \ingroup glew-mx
 */

#include "glew-mx.h"

#include <stdio.h>
#include <stdlib.h>

#define CASE_CODE_RETURN_STR(code) case code: return #code;

static const char *get_glew_error_enum_string(GLenum error)
{
	switch (error) {
		CASE_CODE_RETURN_STR(GLEW_OK) /* also GLEW_NO_ERROR */
		CASE_CODE_RETURN_STR(GLEW_ERROR_NO_GL_VERSION)
		CASE_CODE_RETURN_STR(GLEW_ERROR_GL_VERSION_10_ONLY)
		CASE_CODE_RETURN_STR(GLEW_ERROR_GLX_VERSION_11_ONLY)
#ifdef WITH_GLEW_ES
		CASE_CODE_RETURN_STR(GLEW_ERROR_NOT_GLES_VERSION)
		CASE_CODE_RETURN_STR(GLEW_ERROR_GLES_VERSION)
		CASE_CODE_RETURN_STR(GLEW_ERROR_NO_EGL_VERSION)
		CASE_CODE_RETURN_STR(GLEW_ERROR_EGL_VERSION_10_ONLY)
#endif
		default:
			return NULL;
	}
}


GLenum glew_chk(GLenum error, const char *file, int line, const char *text)
{
	if (error != GLEW_OK) {
		const char *code = get_glew_error_enum_string(error);
		const char *msg  = (const char *)glewGetErrorString(error);

		if (error == GLEW_ERROR_NO_GL_VERSION)
			return GLEW_OK;

#ifndef NDEBUG
		fprintf(stderr,
		        "%s(%d):[%s] -> GLEW Error (0x%04X): %s: %s\n",
		        file, line, text, error,
		        code ? code : "<no symbol>",
		        msg  ? msg  : "<no message>");
#else
		(void) file;
		(void) line;
		(void) text;
		fprintf(stderr,
		        "GLEW Error (0x%04X): %s: %s\n",
		        error,
		        code ? code : "<no symbol>",
		        msg  ? msg  : "<no message>");
#endif
	}

	return error;
}


#ifdef WITH_GLEW_MX
MXContext *_mx_context = NULL;
#endif


/* BGE-Remix P8.29: on a core context GLEW's flags as an old context would
 * have them: every version up to the context's (GLEW counts a version as
 * missing when one of its old functions is), and the extensions OpenGL 4.5
 * has as its own (a core context does not list them) */
static void glew_core_flags(void)
{
	GLint mask = 0, major = 0, minor = 0;
	glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
	if (!(mask & GL_CONTEXT_CORE_PROFILE_BIT)) {
		return;
	}
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);
	const int v = major * 10 + minor;
#ifndef WITH_GLEW_MX
#  define SET(x, on) __GLEW_##x = (on) ? GL_TRUE : GL_FALSE
	SET(VERSION_1_1, 1); SET(VERSION_1_2, 1); SET(VERSION_1_2_1, 1); SET(VERSION_1_3, 1); SET(VERSION_1_4, 1);
	SET(VERSION_1_5, 1); SET(VERSION_2_0, 1); SET(VERSION_2_1, 1); SET(VERSION_3_0, 1);
	SET(VERSION_3_1, v >= 31); SET(VERSION_3_2, v >= 32); SET(VERSION_3_3, v >= 33); SET(VERSION_4_0, v >= 40);
	SET(VERSION_4_1, v >= 41); SET(VERSION_4_2, v >= 42); SET(VERSION_4_3, v >= 43); SET(VERSION_4_4, v >= 44);
	SET(VERSION_4_5, v >= 45);
	if (v >= 45) {
		SET(ARB_shader_objects, 1); SET(ARB_vertex_shader, 1); SET(ARB_fragment_shader, 1);
		SET(ARB_multitexture, 1); SET(ARB_texture_cube_map, 1); SET(ARB_texture_env_combine, 1);
		SET(EXT_framebuffer_object, 1); SET(ARB_framebuffer_object, 1); SET(EXT_framebuffer_blit, 1);
		SET(EXT_framebuffer_multisample, 1); SET(ARB_texture_multisample, 1);
		SET(ARB_texture_float, 1); SET(EXT_texture3D, 1);
		SET(ARB_texture_non_power_of_two, 1); SET(ARB_depth_texture, 1); SET(ARB_shadow, 1);
		SET(ARB_texture_rg, 1); SET(ARB_texture_rectangle, 1);
		SET(ARB_vertex_buffer_object, 1); SET(ARB_vertex_array_object, 1); SET(ARB_sampler_objects, 1);
		SET(ARB_draw_instanced, 1); SET(ARB_draw_elements_base_vertex, 1); SET(ARB_uniform_buffer_object, 1);
		SET(ARB_texture_swizzle, 1); SET(ARB_sync, 1); SET(ARB_half_float_pixel, 1);
		SET(EXT_separate_specular_color, 1); SET(ARB_texture_buffer_object, 1);
		SET(ARB_get_program_binary, 1); SET(ARB_compute_shader, 1); SET(ARB_texture_query_lod, 1);
		SET(ARB_debug_output, 1); SET(KHR_debug, 1);
	}
	SET(ARB_compatibility, 0);
#  undef SET
#else
	(void)v;
#endif
}

MXContext *mxCreateContext(void)
{
	/* BGE-Remix P8.29: a core context lists no extensions in the old
	 * string: their functions are looked up anyway */
	glewExperimental = GL_TRUE;
#ifdef WITH_GLEW_MX
	MXContext* new_ctx = calloc(1, sizeof(MXContext));

	if (new_ctx != NULL) {
		MXContext* cur_ctx = _mx_context;
		_mx_context = new_ctx;
		GLEW_CHK(glewInit());
		glew_core_flags();
		_mx_context = cur_ctx;
	}

	return new_ctx;
#else
	GLEW_CHK(glewInit());
	glew_core_flags();
	return NULL;
#endif
}


MXContext *mxGetCurrentContext(void)
{
#ifdef WITH_GLEW_MX
	return _mx_context;
#else
	return NULL;
#endif
}


void mxMakeCurrentContext(MXContext *ctx)
{
#ifdef WITH_GLEW_MX
	_mx_context = ctx;
#else
	(void)ctx;
#endif
}


void mxDestroyContext(MXContext *ctx)
{
#ifdef WITH_GLEW_MX
	if (_mx_context == ctx)
		_mx_context = NULL;

	free(ctx);
#else
	(void)ctx;
#endif
}
