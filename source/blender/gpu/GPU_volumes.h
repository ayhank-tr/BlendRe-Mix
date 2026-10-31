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

/** \file GPU_volumes.h
 *  \ingroup gpu
 *
 * BGE-Remix P8 Part B (P8.19): volume materials, drawn by stepping through
 * their object's box as Blender Internal's volume integrator does (density,
 * absorption by the transmission colour, scattering of the lamps with the
 * reflection colour and asymmetry, emission), stopped by the scene's depth,
 * over the picture. A baked smoke and fire simulation is a 3D texture of
 * its density (and flame) in the box.
 */

#ifndef __GPU_VOLUMES_H__
#define __GPU_VOLUMES_H__

#include "BLI_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GPUVolume {
	float obmat[4][4];        /* the object's place */
	float bbmin[3], bbmax[3]; /* its box (object space) */
	float density, density_scale, scattering, emission;
	float emission_col[3], transmission_col[3], reflection_col[3];
	float reflection, asymmetry, stepsize, depth_cutoff;
	int shadeless;
	/* a smoke domain's grids (0: none), box and flame colour */
	unsigned int smoke_density, smoke_flame;
	float smoke_min[3], smoke_max[3];
} GPUVolume;

/* draws the volume with the current matrices (the view) over the bound
 * target; the scene's depth must have been drawn (GPU_scene_depth_begin) */
void GPU_volume_draw(const GPUVolume *vol);

/* a 3D texture (R16F) of res[0] x res[1] x res[2] values, made or filled */
void GPU_volume_grid(unsigned int *r_texture, const int res[3], const float *values);
void GPU_volume_grid_free(unsigned int *texture);

void GPU_volumes_exit(void);

#ifdef __cplusplus
}
#endif

#endif  /* __GPU_VOLUMES_H__ */
