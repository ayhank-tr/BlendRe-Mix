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

/** \file RAS_Pipeline.h
 *  \ingroup bgerastogl
 *
 * BGE-Remix P8 Part B (P8.12): the two pipelines. A scene draws into its own
 * target when it needs one:
 *
 * - Modern: a half-float colour target (multisampled with anti-aliasing).
 *   At the end one pass applies the scene's exposure, then its view transform,
 *   look and display (OpenColorIO, baked into a 3D lookup texture over a log2
 *   shaper), then its gamma, and writes the display's values to the window.
 *   With anti-aliasing each sample is tone mapped before they are averaged,
 *   so bright edges stay smooth.
 * - Classic: 8-bit colour as Blender 2.79 drew it, straight into the window;
 *   with anti-aliasing into an 8-bit multisampled target averaged by the same
 *   pass.
 *
 * The pass also writes the scene's depth to the window (2D filters read it)
 * and puts a later scene over the earlier ones by its alpha (it starts
 * transparent), so the 2D filters and overlays draw after it, on the window,
 * as before.
 */

#ifndef __RAS_PIPELINE_H__
#define __RAS_PIPELINE_H__

#include <string>

struct Scene;
struct GPUShader;

class RAS_Pipeline
{
public:
	RAS_Pipeline();
	~RAS_Pipeline();

	/* the scene's target bound (true) or the window kept (false: Classic
	 * without anti-aliasing). "area": the canvas in window coordinates
	 * (x, y, width, height); "clear": the part the first scene clears with
	 * "background" (the rest stays the window's: framing bars) */
	bool Begin(const Scene *scene, bool first, const float background[3], int samples,
	           const int area[4], const int clear[4]);
	/* the target put on the framebuffer bound before Begin (the window, or
	 * an offscreen target), which is bound again */
	void End();

	bool Active() const { return m_active; }
	/* the target's depth texture while active (0 otherwise), whether it is
	 * multisampled, and the area drawn */
	void DepthTexture(unsigned int *r_texture, bool *r_multisample, int r_area[4]) const;
	bool Multisampled() const { return m_active && m_samples > 1; }
	bool Modern() const { return m_active && m_modern; }

	void Free();

private:
	bool m_active;
	bool m_modern;
	int m_samples;
	int m_area[4];
	unsigned int m_prevfb;  /* the framebuffer bound before (End draws into it) */

	unsigned int m_fbo, m_color, m_depth;
	int m_size[2], m_fbsamples;
	bool m_fbmodern;

	/* the composite programs: [multisampled] */
	GPUShader *m_shader[2];
	bool m_shader_failed[2];

	/* the view transform's lookup texture and what it was made from */
	unsigned int m_lut;
	std::string m_lutkey;
	float m_exposure, m_gamma;

	bool MakeTarget(int width, int height, int samples, bool modern);
	void UpdateLUT(const Scene *scene);
	GPUShader *Shader(bool multisampled);
};

#endif  /* __RAS_PIPELINE_H__ */
