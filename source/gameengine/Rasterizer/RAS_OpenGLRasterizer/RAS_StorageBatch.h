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
 * A layout feeds both the generic attributes the game's shaders read (by
 * name: bge_pos_b, bge_nor_b, bge_col_b, bge_texcoN_b, and the built-in
 * shaders' bge_pos, bge_nor, bge_col, bge_texco; the material's own by
 * place) and, while the compatibility context lasts, the old arrays, so a
 * shader still written for gl_Vertex draws the same.
 */

#ifndef __RAS_STORAGEBATCH_H__
#define __RAS_STORAGEBATCH_H__

#include <map>
#include <set>

#include "RAS_IStorage.h"
#include "RAS_IRasterizer.h"
#include "RAS_OpenGLRasterizer.h"

struct RAS_BatchArray;

class RAS_StorageBatch : public RAS_IStorage
{
public:
	RAS_StorageBatch(int *texco_num, RAS_IRasterizer::TexCoGen *texco, int *attrib_num,
	                 RAS_IRasterizer::TexCoGen *attrib, int *attrib_layer, bool *classic);
	virtual ~RAS_StorageBatch();

	virtual bool Init();
	virtual void Exit();

	virtual void IndexPrimitives(RAS_MeshSlot& ms);

	virtual void SetDrawingMode(int drawingmode) { m_drawingmode = drawingmode; }
	virtual void BeginFrame();

	/* the array's record is going (its display array is freed) */
	void Forget(RAS_BatchArray *ba);

	struct ProgramInfo {
		int pos, nor, col, texco[8], cl_texco;
		int batch;  /* the uniform bge_batch */
	};

protected:
	int m_drawingmode;

	int *m_texco_num;
	int *m_attrib_num;
	RAS_IRasterizer::TexCoGen *m_texco;
	RAS_IRasterizer::TexCoGen *m_attrib;
	int *m_attrib_layer;
	bool *m_classic;

	unsigned int m_frame;
	std::set<RAS_BatchArray *> m_arrays;
	std::map<unsigned int, ProgramInfo> m_programs;

	RAS_BatchArray *GetArray(RAS_DisplayArray *array);
	void UploadVertices(RAS_BatchArray *ba);
	void UploadIndices(RAS_BatchArray *ba, size_t first, size_t count);
	const ProgramInfo &GetProgram(unsigned int program);
	unsigned int GetVAO(RAS_BatchArray *ba, unsigned int program, bool wireframe, bool color_array);

#ifdef WITH_CXX_GUARDEDALLOC
public:
	void *operator new(size_t num_bytes) { return MEM_mallocN(num_bytes, "GE:RAS_StorageBatch"); }
	void operator delete( void *mem ) { MEM_freeN(mem); }
#endif
};

#endif  /* __RAS_STORAGEBATCH_H__ */
