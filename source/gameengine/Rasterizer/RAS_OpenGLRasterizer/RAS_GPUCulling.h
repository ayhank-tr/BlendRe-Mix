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

/** \file RAS_GPUCulling.h
 *  \ingroup bgerastogl
 *
 * BGE-Remix P8 Part B (P8.11): occlusion culling on the graphics card. After
 * the main camera's view of the first scene is drawn, its depth becomes a
 * pyramid (each level the farthest depth of four texels of the level below).
 * The next frame, before instances of a mesh are drawn, a compute pass tests
 * each instance's bounding sphere against the pyramid (seen from the
 * previous frame's camera) and writes the visible instances and their count
 * into an indirect draw. The CPU's frustum culling and BGE's own occlusion
 * culling have already run; this only removes what the last frame showed
 * was hidden. It is used for the main camera's view only.
 */

#ifndef __RAS_GPUCULLING_H__
#define __RAS_GPUCULLING_H__

class RAS_GPUCulling
{
public:
	RAS_GPUCulling();
	~RAS_GPUCulling();

	/* the pyramid from a depth texture (multisampled or not; 0: the read
	 * framebuffer's depth is copied, when it is not multisampled) over the
	 * viewport x, y, w, h, with the view and projection it was drawn with */
	void Build(unsigned int depth_texture, bool multisample, const int viewport[4],
	           const float view[16], const float proj[16]);
	void Invalidate() { m_valid = false; }
	bool Valid() const { return m_valid; }

	/* the main view is being drawn (culling may be used) */
	void SetMainView(bool on) { m_mainview = on; }
	bool MainView() const { return m_mainview; }

	/* tests "count" instances whose model-view matrices are in "matrices"
	 * (bound at binding 8) against the pyramid; their visible numbers go to
	 * the buffer bound at binding 9 and their count into the indirect
	 * command at "command" (bound to GL_DRAW_INDIRECT_BUFFER's buffer) */
	bool Cull(int count, const float center[3], float radius, const float cur_view[16],
	          unsigned int command_buffer, unsigned int command_offset);

	/* a frame's culled and drawn instances (read back late: statistics) */
	int Tested() const { return m_tested; }
	void FrameStats();

	void Free();

private:
	bool m_valid, m_mainview;
	unsigned int m_depthcopy, m_pyramid;
	int m_size[2], m_levels, m_copysize[2];
	float m_prevview[16], m_prevproj[16];
	unsigned int m_prog_first, m_prog_firstms, m_prog_down, m_prog_cull;
	bool m_failed;
	int m_tested;

	bool Programs();
};

#endif  /* __RAS_GPUCULLING_H__ */
