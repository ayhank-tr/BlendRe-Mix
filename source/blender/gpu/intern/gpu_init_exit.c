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
 * The Original Code is Copyright (C) 2013 Blender Foundation.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): Jason Wilkins
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file source/blender/gpu/intern/gpu_init_exit.c
 *  \ingroup gpu
 */

#include "BLI_sys_types.h"
#include "GPU_init_exit.h"  /* interface */

#include <stdlib.h>

#include "BKE_global.h"

#include "GPU_lights.h"
#include "GPU_probes.h"
#include "GPU_volumes.h"
#include "GPU_material.h"
#include "GPU_shader.h"

#include "intern/gpu_codegen.h"
#include "intern/gpu_private.h"

/**
 * although the order of initialization and shutdown should not matter
 * (except for the extensions), I chose alphabetical and reverse alphabetical order
 */

static bool initialized = false;

void GPU_init(void)
{
	/* can't avoid calling this multiple times, see wm_window_ghostwindow_add */
	if (initialized)
		return;

	initialized = true;

	/* BGE-Remix P8.1: OpenGL 4.5 or a clear message; the tiers; driver messages */
	gpu_glcheck_init();

	gpu_extensions_init(); /* must come first */

	gpu_codegen_init();

	if (G.debug & G_DEBUG_GPU)
		gpu_debug_init();

	/* BGE-Remix P8.2, P8.3: the drawing layer and the shader cache */
	gpu_layer_init();
	GPU_shader_cache_init();

	/* BGE-Remix P8.2: the layer's self-test (T8.1) */
	{
		const char *dir = getenv("BGE_REMIX_GL_SELFTEST");
		if (dir && dir[0]) {
			GPU_layer_selftest(dir);
		}
	}
}



void GPU_exit(void)
{
	if (G.debug & G_DEBUG_GPU)
		gpu_debug_exit();

	/* BGE-Remix P8.1-P8.3 */
	GPU_shader_cache_exit();
	gpu_layer_exit();
	GPU_shared_buffers_free();
	GPU_lights_exit();  /* P8 Part B */
	GPU_probes_exit();
	GPU_volumes_exit();
	gpu_glcheck_exit();

	gpu_codegen_exit();

	gpu_extensions_exit(); /* must come last */

	initialized = false;
}
