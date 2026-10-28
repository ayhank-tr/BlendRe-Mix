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

/** \file remix_light.cc
 *  \ingroup remix
 *
 * Lamps and world (BRMX_light.h). Moved here from the game engine
 * (KX_RemixRenderer::UpdateLights, P2) without changes to the formulas.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include "DNA_lamp_types.h"
}

#include "BRMX_light.h"

namespace brmx {

static const float SUN_ANGULAR_DIAMETER_DEG = 0.53f;
static const float MIN_LIGHT_RADIUS = 0.02f;
/* Remix's own default for rtx.localtonemap.exposure (1.6.1) */
static const double LOCAL_TONEMAP_EXPOSURE = 0.75;
static const double PI = 3.14159265358979323846;

static void copy3(const float a[3], float r[3])
{
	r[0] = a[0];
	r[1] = a[1];
	r[2] = a[2];
}

static void normalize3(float v[3])
{
	const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (len > 1e-8f) {
		v[0] /= len;
		v[1] /= len;
		v[2] /= len;
	}
}

int lamp_light(const LampInput &lamp, const float pos[3], const float axis_x[3], const float axis_y[3],
               const float axis_z[3], float light_scale, brb_light_desc &d, float r_dome[3])
{
	const float energy = lamp.energy * light_scale;
	if (!(energy > 0.0f)) {
		return 0;
	}
	const float color[3] = {std::fabs(lamp.color[0]), std::fabs(lamp.color[1]), std::fabs(lamp.color[2])};
	if (lamp.type == LA_HEMI) {
		/* Hemi: light from the whole sky, added to the dome */
		for (int k = 0; k < 3; k++) {
			r_dome[k] += color[k] * energy;
		}
		return 2;
	}

	const uint64_t hash = d.hash;
	memset(&d, 0, sizeof(d));
	d.hash = hash;
	copy3(pos, d.position);
	/* lamps shine along their local -Z */
	d.direction[0] = -axis_z[0];
	d.direction[1] = -axis_z[1];
	d.direction[2] = -axis_z[2];
	normalize3(d.direction);
	const float dist = std::max(lamp.dist, 0.001f);
	float scale = 1.0f;

	if (lamp.type == LA_SUN) {
		/* Remix's distant light "radiance" is the irradiance it gives (as
		 * Remix converts D3D9 sun lights): pi * E. Dividing by the sun's
		 * solid angle burnt the sunlit floor out (RTX 3080, P2 room). */
		d.type = BRB_LIGHT_DISTANT;
		d.angular_diameter_deg = SUN_ANGULAR_DIAMETER_DEG;
		scale = (float)(PI * energy);
	}
	else if (lamp.type == LA_AREA && lamp.have_lamp) {
		d.type = BRB_LIGHT_RECT;
		copy3(axis_x, d.x_axis);
		copy3(axis_y, d.y_axis);
		normalize3(d.x_axis);
		normalize3(d.y_axis);
		d.size[0] = std::max(lamp.area_size * std::fabs(lamp.scale[0]), 0.001f);
		d.size[1] = std::max((lamp.area_square ? lamp.area_size : lamp.area_sizey) * std::fabs(lamp.scale[1]),
		                     0.001f);
		/* intensity pi * E * D over the lamp's area */
		scale = (float)(PI * energy * dist / (d.size[0] * d.size[1]));
	}
	else {
		/* point and spot: a small sphere; Blender Internal's falloff
		 * E * D / (D + r^2) is E * D / r^2 far away, so the intensity is
		 * pi * E * D, spread over the sphere's disc pi * R^2 */
		d.type = BRB_LIGHT_SPHERE;
		d.radius = std::max(lamp.have_lamp ? lamp.area_size : 0.1f, MIN_LIGHT_RADIUS);
		scale = energy * dist / (d.radius * d.radius);
		if (lamp.type == LA_SPOT) {
			d.shaped = 1;
			const double half = lamp.spotsize * 0.5;
			d.cone_angle_deg = (float)(half * 180.0 / PI);
			/* Remix fades the cone edge over coneSoftness in cosine units
			 * (smoothstep(cos(cone), cos(cone) + softness, cos(angle))), as
			 * Blender does over (1 - cos(cone)) * blend; passing the blend
			 * itself dimmed and blurred the pool (P2 RTX run 5) */
			const float blend = std::min(std::max(lamp.spotblend, 0.0f), 1.0f);
			d.cone_softness = (float)((1.0 - std::cos(half)) * blend);
		}
	}
	d.radiance[0] = color[0] * scale;
	d.radiance[1] = color[1] * scale;
	d.radiance[2] = color[2] * scale;
	return 1;
}

int dome_light(const float color[3], brb_light_desc &d)
{
	if (!(color[0] > 0.0f || color[1] > 0.0f || color[2] > 0.0f)) {
		return 0;
	}
	const uint64_t hash = d.hash;
	memset(&d, 0, sizeof(d));
	d.hash = hash;
	d.type = BRB_LIGHT_DOME;
	d.radiance[0] = color[0];
	d.radiance[1] = color[1];
	d.radiance[2] = color[2];
	return 1;
}

void apply_exposure(const BRMX_Bridge *bridge, float stops)
{
	/* Remix's default tone mapper (rtx.tonemappingMode Local) ignores
	 * rtx.tonemap.exposureBias and scales by rtx.localtonemap.exposure
	 * instead (P2 RTX run 5: four exposures, one picture), so both are set:
	 * the local factor from Remix's default, the global bias for scenes that
	 * switch to the global tone mapper. */
	/* sent when it changes (the game, the 3D view and F12 share Remix) */
	static float sent = 1e9f;
	if (!bridge || stops == sent) {
		return;
	}
	sent = stops;
	char value[32];
	snprintf(value, sizeof(value), "%g", LOCAL_TONEMAP_EXPOSURE * std::pow(2.0, (double)stops));
	bridge->set_config("rtx.localtonemap.exposure", value);
	snprintf(value, sizeof(value), "%g", stops);
	bridge->set_config("rtx.tonemap.exposureBias", value);
}

}  /* namespace brmx */
