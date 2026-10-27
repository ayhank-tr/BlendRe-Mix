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

/** \file BRMX_light.h
 *  \ingroup remix
 *
 * BGE-Remix: Blender's lamps and world in Remix's physical units, shared by
 * the game engine (P2) and the 3D view and F12 (P5), so that they light a
 * scene the same way.
 *
 * The world colour and the lamps, in Blender Internal's units, are turned
 * into Remix's physical units so that a white surface lit by a lamp of
 * energy 1 at distance D comes out as bright as in Blender Internal (far
 * from the lamp); the scene's Light Scale is a global factor on top, tuned by
 * eye against the room's Cycles render (P2 sign-off).
 */

#ifndef __BRMX_LIGHT_H__
#define __BRMX_LIGHT_H__

#ifndef __cplusplus
#  error "BRMX_light.h is C++"
#endif

#include "BRMX_loader.h"

namespace brmx {

/* Chosen by eye against the room's Cycles render (P2 sign-off, RTX 3080):
 * with 1, the lamps outshine everything else under Remix's automatic
 * exposure (the glowing panel disappears) */
static const float DEFAULT_LIGHT_SCALE = 0.125f;
/* Chosen against the material grid's Cycles render (P3 sign-off, RTX 3080):
 * +1 stop matches the backdrop's mid-grey. Same default as the Rasterizer
 * panel's Exposure. */
static const float DEFAULT_EXPOSURE = 1.0f;

/* One lamp, as the game engine and Blender's Lamp both describe it. */
struct LampInput {
	int type;            /* LA_LOCAL, LA_SUN, LA_SPOT, LA_HEMI, LA_AREA */
	float energy;        /* before the light scale */
	float color[3];      /* linear; a negative lamp has been left out already */
	float dist;          /* falloff distance */
	float spotsize;      /* full cone angle, radians */
	float spotblend;
	bool have_lamp;      /* the area fields below come from a Lamp */
	float area_size;     /* point/spot: the radius; area: the size */
	float area_sizey;
	bool area_square;
	float scale[2];      /* the object's world scale on X and Y (area lamps) */
};

/* The lamp at pos, with its world axes (lamps shine along -axis_z), as a
 * Remix light: returns 1 and fills r_desc (all but the hash), or 2 for a
 * hemi lamp (Remix has none: its colour times energy goes into r_dome, to be
 * added to the sky), or 0 for no light (no energy). */
int lamp_light(const LampInput &lamp, const float pos[3], const float axis_x[3], const float axis_y[3],
               const float axis_z[3], float light_scale, brb_light_desc &r_desc, float r_dome[3]);

/* The world's sky as a dome light of this colour (the horizon colour plus
 * the hemi lamps); returns 0 when it is black. Fills all but the hash. */
int dome_light(const float color[3], brb_light_desc &r_desc);

/* Stops on top of Remix's automatic exposure, for both of Remix's tone
 * mappers (rtx.localtonemap.exposure and rtx.tonemap.exposureBias); sent
 * only when it differs from what was sent last. */
void apply_exposure(const BRMX_Bridge *bridge, float stops);

}  /* namespace brmx */

#endif  /* __BRMX_LIGHT_H__ */
