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

/** \file RAS_ScreenEffects.h
 *  \ingroup bgerastogl
 *
 * BGE-Remix P8 Part C (P8.23): the Modern pipeline's camera and screen
 * effects, compute passes over the scene's half-float picture and depth
 * before the view transform: ambient occlusion (the World's setting),
 * depth of field (the camera's focus and f-stop), motion blur (the Render
 * tab's shutter; the camera's motion, from the depth and last frame's
 * view), bloom, auto exposure (a running average of the picture's
 * brightness) and vignette (in the last pass).
 */

#ifndef __RAS_SCREENEFFECTS_H__
#define __RAS_SCREENEFFECTS_H__

class RAS_ScreenEffects
{
public:
	struct Settings {
		bool ao;
		float ao_distance, ao_energy;
		bool dof;
		float focus, fstop, focal_mm, sensor_mm;
		bool mblur;
		float shutter;
		bool bloom;
		float bloom_intensity, bloom_threshold, bloom_radius;
		bool autoexp;
		float exposure_speed;
		float vignette;
		bool Any() const { return ao || dof || mblur || bloom || autoexp; }
	};

	RAS_ScreenEffects();
	~RAS_ScreenEffects();
	void Free();

	/* runs the effects on "color" (single-sampled, width x height, the
	 * scene's picture) with "depth"; returns the texture holding the result
	 * (0: none ran, use color). view and proj: this frame's camera. */
	unsigned int Run(unsigned int color, unsigned int depth, int width, int height, const float view[16],
	                 const float proj[16], const Settings &s, double time);
	/* the 1x1 exposure texture (auto exposure), 0 when off */
	unsigned int Exposure() const { return m_exp_on ? m_exposure : 0; }

private:
	unsigned int m_prog[10];
	bool m_failed;
	int m_size[2];
	unsigned int m_tex[2];     /* ping-pong, RGBA16F */
	unsigned int m_ao;         /* R8 */
	unsigned int m_bloom;      /* RGBA16F, half size, mips */
	int m_bloom_levels;
	unsigned int m_lum;        /* R16F, quarter size, mips */
	int m_lum_levels;
	unsigned int m_exposure;   /* R32F 1x1 */
	bool m_exp_on, m_exp_reset;
	unsigned int m_sampler_linear, m_sampler_point;
	float m_prev_vp[16];
	bool m_have_prev;
	double m_last_time;

	bool Programs();
	void Targets(int width, int height);
};

#endif  /* __RAS_SCREENEFFECTS_H__ */
