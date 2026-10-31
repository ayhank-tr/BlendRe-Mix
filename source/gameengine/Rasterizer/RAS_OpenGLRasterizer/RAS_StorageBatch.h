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

/** \file RAS_StorageBatch.h
 *  \ingroup bgerastogl
 *
 * BGE-Remix P8.4: the game's meshes as batches. Each display array keeps its
 * vertices and indices on the card (immutable buffers, changed only by copies
 * from the streaming buffer); vertex layouts are kept per shader in vertex
 * array objects. Meshes the CPU changes (armatures, shape keys, soft bodies,
 * Python) are written into the streaming buffer and copied on the card, once
 * a frame; depth-sorted ones get their indices the same way.
 *
 * A layout feeds the generic attributes the game's shaders read: by name
 * (bge_pos, bge_nor, bge_col, bge_texcoN, and the classic shader's
 * bge_texco) and the material's own by place. BGE-Remix P8 Part B: the
 * matrices and the other old built-ins are set as uniforms before each
 * draw (GPU_old_builtins) from the layer's matrices and the game's state.
 * Only drawing that is still the old kind (no shader in the wireframe and
 * solid modes, or a custom shader whose translation did not build) gets
 * the old arrays, and the old alpha test, while the compatibility context
 * lasts. A derived mesh (modifiers) is drawn from its slot's own arrays.
 * BGE-Remix P8 Part B (P8.13): a mesh its deformer gives a RAS_GPUSkin is
 * deformed by compute passes, which write its arrays' buffers on the card.
 */

#ifndef __RAS_STORAGEBATCH_H__
#define __RAS_STORAGEBATCH_H__

#include <map>
#include <set>

#include "RAS_IStorage.h"
#include "RAS_IRasterizer.h"
#include "RAS_OpenGLRasterizer.h"

#include "GPU_layer.h"

struct RAS_BatchArray;

class RAS_StorageBatch : public RAS_IStorage
{
public:
	RAS_StorageBatch(int *texco_num, RAS_IRasterizer::TexCoGen *texco, int *attrib_num,
	                 RAS_IRasterizer::TexCoGen *attrib, int *attrib_layer, bool *classic,
	                 const GPUClassicState *state);
	virtual ~RAS_StorageBatch();

	virtual bool Init();
	virtual void Exit();

	virtual void IndexPrimitives(RAS_MeshSlot& ms);
	/* BGE-Remix P8 Part B (P8.11): slots of one mesh and material in one
	 * instanced draw (their model-view matrices from "view" and their own);
	 * false when the bound shader cannot (the caller draws them one by one) */
	bool IndexPrimitivesInstanced(std::vector<RAS_MeshSlot *>& slots, const float view[16]);
	void SetCulling(class RAS_GPUCulling *culling) { m_culling = culling; }
	/* the frame's instanced draws and instances (statistics) */
	int InstancedDraws() const { return m_inst_draws; }
	int InstancedObjects() const { return m_inst_objects; }
	/* the frame's instances the culling pass tested and those it let through
	 * (read back from the card: only with BGE_REMIX_DRAW_STATS=1, else -1) */
	void CullingStats(int *r_tested, int *r_drawn);

	virtual void SetDrawingMode(int drawingmode) { m_drawingmode = drawingmode; }
	virtual void BeginFrame();

	/* the array's record is going (its display array is freed) */
	void Forget(RAS_BatchArray *ba);

	struct ProgramInfo {
		int pos, nor, col, texco[8], cl_texco;
		bool core;  /* reads bge_pos: attributes only (otherwise the old arrays) */
		int instanced;  /* the uniform bge_instanced (-1: the shader cannot instance) */
	};

protected:
	int m_drawingmode;

	int *m_texco_num;
	int *m_attrib_num;
	RAS_IRasterizer::TexCoGen *m_texco;
	RAS_IRasterizer::TexCoGen *m_attrib;
	int *m_attrib_layer;
	bool *m_classic;
	const GPUClassicState *m_state;

	unsigned int m_frame;
	std::set<RAS_BatchArray *> m_arrays;
	std::map<unsigned int, ProgramInfo> m_programs;

	RAS_BatchArray *GetArray(RAS_DisplayArray *array);
	void UploadVertices(RAS_BatchArray *ba);
	void UploadIndices(RAS_BatchArray *ba, size_t first, size_t count);
	const ProgramInfo &GetProgram(unsigned int program);
	unsigned int GetVAO(RAS_BatchArray *ba, unsigned int program, bool own, bool wireframe, bool color_array,
	                    bool instanced = false);

	/* instancing: the instance numbers (attribute 15), the indirect commands
	 * and the visible lists of the culling pass (used from the start each frame) */
	unsigned int m_instbuf, m_cmdbuf, m_visbuf;
	unsigned int m_cmdused, m_visused;
	class RAS_GPUCulling *m_culling;
	int m_inst_draws, m_inst_objects;
	int m_cull_tested;
	void DrawArray(RAS_MeshSlot& ms, RAS_DisplayArray *array, unsigned short *index, size_t totindex,
	               unsigned int program, const ProgramInfo& pi, bool own, bool modified, bool zsort);

	/* GPU skinning (P8.13): the compute passes (0 untried, 1 made, -1 failed),
	 * and the slot's arrays deformed on the card before they are drawn */
	unsigned int m_skinprog[4];
	int m_skinloc[4][17];
	int m_skinstate;
	bool SkinPrograms();
	void SkinArrays(RAS_MeshSlot& ms, struct RAS_GPUSkin *skin, bool modified);

#ifdef WITH_CXX_GUARDEDALLOC
public:
	void *operator new(size_t num_bytes) { return MEM_mallocN(num_bytes, "GE:RAS_StorageBatch"); }
	void operator delete( void *mem ) { MEM_freeN(mem); }
#endif
};

#endif  /* __RAS_STORAGEBATCH_H__ */
