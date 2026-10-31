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

/** \file gameengine/Rasterizer/RAS_OpenGLRasterizer/RAS_StorageBatch.cpp
 *  \ingroup bgerastogl
 */

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

#include "RAS_StorageBatch.h"
#include "RAS_Deformer.h"
#include "RAS_GPUSkin.h"
#include "RAS_GPUCulling.h"
#include "RAS_MaterialBucket.h"
#include "RAS_MeshObject.h"
#include "RAS_TexVert.h"
#include "MT_Matrix4x4.h"

#include "GPU_glew.h"

extern "C" {
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_lights.h"
#include "GPU_shader.h"
}

/* how a layout was made: the shader and what it was fed */
struct RAS_BatchLayout {
	unsigned int program;
	int flags;  /* 1: wireframe (no texture coordinates or attributes), 2: colour array, 4: our own shader,
	             * 8: instanced (attribute 15) */
	int texco_num;
	int texco[RAS_MAX_TEXCO];
	int attrib_num;
	int attrib[RAS_MAX_ATTRIB];
	int layer[RAS_MAX_ATTRIB];
};

#define RAS_BATCH_VAOS 3

struct RAS_BatchArray {
	RAS_StorageBatch *owner;
	RAS_DisplayArray *array;
	GLuint vbo, ibo;
	size_t nverts, nindex;    /* the array's counts when made */
	const void *vdata;        /* its vertex data then (a moved vector is made again) */
	bool quads;               /* made triangles: 6 indices for every 4 */
	unsigned int uploaded;    /* the frame its vertices were last copied */
	GLuint vaos[RAS_BATCH_VAOS];
	RAS_BatchLayout layouts[RAS_BATCH_VAOS];
	int next;
	float center[3], radius;  /* the vertices' bounding sphere (local) */
	unsigned int skinned;     /* the GPU skin frame its buffer holds (0: the CPU's vertices) */
};

/* the offsets inside RAS_TexVert */
static RAS_TexVert s_tv;
#define TV_OFS(ptr) ((const void *)((const char *)(ptr) - (const char *)&s_tv))
#define TV_STRIDE ((GLsizei)sizeof(RAS_TexVert))

static void batch_array_delete_gl(RAS_BatchArray *ba)
{
	if (ba->vbo)
		glDeleteBuffers(1, &ba->vbo);
	if (ba->ibo)
		glDeleteBuffers(1, &ba->ibo);
	for (int i = 0; i < RAS_BATCH_VAOS; i++) {
		if (ba->vaos[i])
			glDeleteVertexArrays(1, &ba->vaos[i]);
		ba->vaos[i] = 0;
	}
	ba->vbo = ba->ibo = 0;
}

/* RAS_DisplayArray's destructor (or a copy assigned over it) */
static void batch_array_free(void *storage)
{
	RAS_BatchArray *ba = (RAS_BatchArray *)storage;
	if (!ba)
		return;
	if (ba->owner) {
		ba->owner->Forget(ba);
		batch_array_delete_gl(ba);
	}
	delete ba;
}

/* indices as drawn: quads made triangles, split as OpenGL splits an indexed
 * GL_QUADS draw (0 1 3, 1 2 3: the last vertex leads both), so a quad that is
 * not flat or not a parallelogram looks as it did in stock 2.79 */
static void batch_indices(const RAS_DisplayArray *array, size_t first, size_t count, bool quads,
                          std::vector<unsigned short>& out)
{
	out.clear();
	const unsigned short *src = &array->m_index[0];
	if (quads) {
		out.reserve(count / 4 * 6);
		for (size_t i = first; i + 3 < first + count; i += 4) {
			out.push_back(src[i]);
			out.push_back(src[i + 1]);
			out.push_back(src[i + 3]);
			out.push_back(src[i + 1]);
			out.push_back(src[i + 2]);
			out.push_back(src[i + 3]);
		}
	}
	else {
		out.assign(src + first, src + first + count);
	}
}

/* data into a buffer on the card through the streaming buffer */
static void batch_copy_in(GLuint dst, size_t dst_offset, const void *data, size_t size)
{
	if (size == 0)
		return;
	GLuint buffer = 0;
	size_t offset = 0;
	void *ptr = GPU_stream_alloc(size, &buffer, &offset);
	if (ptr) {
		memcpy(ptr, data, size);
		GPU_stream_written(buffer);
		glCopyNamedBufferSubData(buffer, dst, (GLintptr)offset, (GLintptr)dst_offset, (GLsizeiptr)size);
	}
	else {
		glNamedBufferSubData(dst, (GLintptr)dst_offset, (GLsizeiptr)size, data);
	}
}

RAS_StorageBatch::RAS_StorageBatch(int *texco_num, RAS_IRasterizer::TexCoGen *texco, int *attrib_num,
                                   RAS_IRasterizer::TexCoGen *attrib, int *attrib_layer, bool *classic,
                                   const GPUClassicState *state)
    :m_drawingmode(RAS_IRasterizer::KX_TEXTURED),
    m_texco_num(texco_num),
    m_attrib_num(attrib_num),
    m_texco(texco),
    m_attrib(attrib),
    m_attrib_layer(attrib_layer),
    m_classic(classic),
    m_state(state),
    m_frame(1),
    m_instbuf(0),
    m_cmdbuf(0),
    m_visbuf(0),
    m_cmdused(0),
    m_visused(0),
    m_culling(NULL),
    m_inst_draws(0),
    m_inst_objects(0),
    m_skinstate(0)
{
	memset(m_skinprog, 0, sizeof(m_skinprog));
	memset(m_skinloc, -1, sizeof(m_skinloc));
	RAS_DisplayArray::s_storage_free = batch_array_free;
}

RAS_StorageBatch::~RAS_StorageBatch()
{
	Exit();
}

bool RAS_StorageBatch::Init()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	return true;
}

void RAS_StorageBatch::Exit()
{
	/* the records stay with their arrays (freed with them), without buffers */
	for (std::set<RAS_BatchArray *>::iterator it = m_arrays.begin(); it != m_arrays.end(); ++it) {
		batch_array_delete_gl(*it);
		(*it)->owner = NULL;
	}
	m_arrays.clear();
	m_programs.clear();
	GLuint bufs[3] = {m_instbuf, m_cmdbuf, m_visbuf};
	for (int i = 0; i < 3; i++) {
		if (bufs[i])
			glDeleteBuffers(1, &bufs[i]);
	}
	m_instbuf = m_cmdbuf = m_visbuf = 0;
	for (int i = 0; i < 4; i++) {
		if (m_skinprog[i])
			glDeleteProgram(m_skinprog[i]);
		m_skinprog[i] = 0;
	}
	m_skinstate = 0;
}

void RAS_StorageBatch::BeginFrame()
{
	m_frame++;
	/* programs can be made again under the same name: asked anew each frame */
	m_programs.clear();
	GPU_old_builtins_frame();
	m_cmdused = m_visused = 0;
	m_inst_draws = m_inst_objects = 0;
}

void RAS_StorageBatch::Forget(RAS_BatchArray *ba)
{
	m_arrays.erase(ba);
}

RAS_BatchArray *RAS_StorageBatch::GetArray(RAS_DisplayArray *array)
{
	RAS_BatchArray *ba = (RAS_BatchArray *)array->m_storage;
	if (ba && ba->owner == this && ba->nverts == array->m_vertex.size() &&
	    ba->nindex == array->m_index.size() && ba->vdata == (const void *)&array->m_vertex[0])
	{
		return ba;
	}
	if (ba) {
		/* the array changed size (or its record belongs to a storage that
		 * has gone): made again */
		array->m_storage = NULL;
		batch_array_free(ba);
	}

	ba = new RAS_BatchArray;
	memset(ba, 0, sizeof(*ba));
	ba->owner = this;
	ba->array = array;
	ba->nverts = array->m_vertex.size();
	ba->nindex = array->m_index.size();
	ba->vdata = &array->m_vertex[0];
	ba->quads = (array->m_type == RAS_DisplayArray::QUAD);
	ba->uploaded = m_frame;
	{
		float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
		for (size_t v = 0; v < ba->nverts; v++) {
			const float *co = array->m_vertex[v].getXYZ();
			for (int k = 0; k < 3; k++) {
				lo[k] = (co[k] < lo[k]) ? co[k] : lo[k];
				hi[k] = (co[k] > hi[k]) ? co[k] : hi[k];
			}
		}
		float r2 = 0.0f;
		for (int k = 0; k < 3; k++) {
			ba->center[k] = 0.5f * (lo[k] + hi[k]);
		}
		for (size_t v = 0; v < ba->nverts; v++) {
			const float *co = array->m_vertex[v].getXYZ();
			const float dx = co[0] - ba->center[0], dy = co[1] - ba->center[1], dz = co[2] - ba->center[2];
			const float d2 = dx * dx + dy * dy + dz * dz;
			r2 = (d2 > r2) ? d2 : r2;
		}
		ba->radius = sqrtf(r2);
	}

	glCreateBuffers(1, &ba->vbo);
	glNamedBufferStorage(ba->vbo, (GLsizeiptr)(ba->nverts * sizeof(RAS_TexVert)), &array->m_vertex[0],
	                     GL_DYNAMIC_STORAGE_BIT);
	GPU_debug_label(GL_BUFFER, ba->vbo, "BGE-Remix mesh vertices");

	std::vector<unsigned short> indices;
	batch_indices(array, 0, ba->nindex, ba->quads, indices);
	glCreateBuffers(1, &ba->ibo);
	glNamedBufferStorage(ba->ibo, (GLsizeiptr)((indices.empty() ? 1 : indices.size()) * sizeof(unsigned short)),
	                     indices.empty() ? NULL : &indices[0], GL_DYNAMIC_STORAGE_BIT);
	GPU_debug_label(GL_BUFFER, ba->ibo, "BGE-Remix mesh indices");

	array->m_storage = ba;
	m_arrays.insert(ba);
	return ba;
}

void RAS_StorageBatch::UploadVertices(RAS_BatchArray *ba)
{
	batch_copy_in(ba->vbo, 0, &ba->array->m_vertex[0], ba->nverts * sizeof(RAS_TexVert));
	ba->skinned = 0;
}

void RAS_StorageBatch::UploadIndices(RAS_BatchArray *ba, size_t first, size_t count)
{
	std::vector<unsigned short> indices;
	batch_indices(ba->array, first, count, ba->quads, indices);
	const size_t dst = (ba->quads ? first / 4 * 6 : first) * sizeof(unsigned short);
	if (!indices.empty())
		batch_copy_in(ba->ibo, dst, &indices[0], indices.size() * sizeof(unsigned short));
}

const RAS_StorageBatch::ProgramInfo& RAS_StorageBatch::GetProgram(unsigned int program)
{
	std::map<unsigned int, ProgramInfo>::iterator it = m_programs.find(program);
	if (it != m_programs.end())
		return it->second;

	ProgramInfo pi;
	pi.pos = pi.nor = pi.col = pi.cl_texco = -1;
	pi.core = false;
	pi.instanced = -1;
	for (int i = 0; i < 8; i++)
		pi.texco[i] = -1;
	if (program) {
		GLint linked = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &linked);
		if (linked) {
			pi.pos = glGetAttribLocation(program, "bge_pos");
			pi.nor = glGetAttribLocation(program, "bge_nor");
			pi.col = glGetAttribLocation(program, "bge_col");
			char name[32];
			for (int i = 0; i < 8; i++) {
				snprintf(name, sizeof(name), "bge_texco%d", i);
				pi.texco[i] = glGetAttribLocation(program, name);
			}
			pi.cl_texco = glGetAttribLocation(program, "bge_texco");
			pi.core = pi.pos >= 0;
			pi.instanced = glGetUniformLocation(program, "bge_instanced");
		}
	}
	return m_programs[program] = pi;
}

/* a texture coordinate source as an attribute (the old arrays' choices) */
static void batch_texco_attrib(GLint loc, RAS_IRasterizer::TexCoGen gen, int layer)
{
	switch (gen) {
		case RAS_IRasterizer::RAS_TEXCO_ORCO:
		case RAS_IRasterizer::RAS_TEXCO_GLOB:
			glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, TV_STRIDE, TV_OFS(s_tv.getXYZ()));
			break;
		case RAS_IRasterizer::RAS_TEXCO_UV:
			glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, TV_STRIDE, TV_OFS(s_tv.getUV(layer)));
			break;
		case RAS_IRasterizer::RAS_TEXCO_NORM:
			glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, TV_STRIDE, TV_OFS(s_tv.getNormal()));
			break;
		case RAS_IRasterizer::RAS_TEXTANGENT:
			glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, TV_STRIDE, TV_OFS(s_tv.getTangent()));
			break;
		case RAS_IRasterizer::RAS_TEXCO_VCOL:
			glVertexAttribPointer(loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, TV_STRIDE, TV_OFS(s_tv.getRGBA()));
			break;
		default:
			return;
	}
	glEnableVertexAttribArray(loc);
}

unsigned int RAS_StorageBatch::GetVAO(RAS_BatchArray *ba, unsigned int program, bool own, bool wireframe,
                                      bool color_array, bool instanced)
{
	RAS_BatchLayout layout;
	memset(&layout, 0, sizeof(layout));
	layout.program = program;
	const ProgramInfo& pi = GetProgram(program);
	/* which arrays: a shader that reads bge_pos gets attributes only (its
	 * position is attribute 0); the old kind (no shader, or one still
	 * written for gl_Vertex) the old arrays. On some drivers the old arrays
	 * share places with attributes, so an array the shader does not read is
	 * left off. Our own shaders (classic, depth) take no material attributes. */
	const bool legacy = !pi.core;
	layout.flags = (wireframe ? 1 : 0) | (color_array ? 2 : 0) | (own ? 4 : 0) | (instanced ? 8 : 0);
	if (!wireframe) {
		layout.texco_num = *m_texco_num;
		for (int i = 0; i < layout.texco_num; i++)
			layout.texco[i] = (int)m_texco[i];
	}
	if (!wireframe && !own) {
		layout.attrib_num = *m_attrib_num;
		for (int i = 0; i < layout.attrib_num; i++) {
			layout.attrib[i] = (int)m_attrib[i];
			layout.layer[i] = m_attrib_layer[i];
		}
	}

	for (int i = 0; i < RAS_BATCH_VAOS; i++) {
		if (ba->vaos[i] && memcmp(&ba->layouts[i], &layout, sizeof(layout)) == 0)
			return ba->vaos[i];
	}

	/* a new layout, in place of the oldest */
	const int slot = ba->next;
	ba->next = (ba->next + 1) % RAS_BATCH_VAOS;
	if (ba->vaos[slot])
		glDeleteVertexArrays(1, &ba->vaos[slot]);
	GLuint vao = 0;
	glGenVertexArrays(1, &vao);
	ba->vaos[slot] = vao;
	ba->layouts[slot] = layout;

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, ba->vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ba->ibo);

	if (legacy) {
		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, TV_STRIDE, TV_OFS(s_tv.getXYZ()));
		glEnableClientState(GL_NORMAL_ARRAY);
		glNormalPointer(GL_FLOAT, TV_STRIDE, TV_OFS(s_tv.getNormal()));
		if (color_array) {
			glEnableClientState(GL_COLOR_ARRAY);
			glColorPointer(4, GL_UNSIGNED_BYTE, TV_STRIDE, TV_OFS(s_tv.getRGBA()));
		}
	}
	if (!wireframe && legacy) {
		for (int unit = 0; unit < layout.texco_num; unit++) {
			glClientActiveTexture(GL_TEXTURE0 + unit);
			switch (m_texco[unit]) {
				case RAS_IRasterizer::RAS_TEXCO_ORCO:
				case RAS_IRasterizer::RAS_TEXCO_GLOB:
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(3, GL_FLOAT, TV_STRIDE, TV_OFS(s_tv.getXYZ()));
					break;
				case RAS_IRasterizer::RAS_TEXCO_UV:
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(2, GL_FLOAT, TV_STRIDE, TV_OFS(s_tv.getUV(unit)));
					break;
				case RAS_IRasterizer::RAS_TEXCO_NORM:
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(3, GL_FLOAT, TV_STRIDE, TV_OFS(s_tv.getNormal()));
					break;
				case RAS_IRasterizer::RAS_TEXTANGENT:
					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
					glTexCoordPointer(4, GL_FLOAT, TV_STRIDE, TV_OFS(s_tv.getTangent()));
					break;
				default:
					break;
			}
		}
		glClientActiveTexture(GL_TEXTURE0);
	}
	if (!wireframe && !own) {
		/* the material's own attributes, by place */
		for (int unit = 0; unit < layout.attrib_num; unit++)
			batch_texco_attrib(unit, m_attrib[unit], m_attrib_layer[unit]);
	}

	/* the attributes the game's shaders read by name */
	if (pi.pos >= 0) {
		glVertexAttribPointer(pi.pos, 3, GL_FLOAT, GL_FALSE, TV_STRIDE, TV_OFS(s_tv.getXYZ()));
		glEnableVertexAttribArray(pi.pos);
	}
	if (pi.nor >= 0) {
		glVertexAttribPointer(pi.nor, 3, GL_FLOAT, GL_FALSE, TV_STRIDE, TV_OFS(s_tv.getNormal()));
		glEnableVertexAttribArray(pi.nor);
	}
	if (pi.col >= 0 && color_array) {
		glVertexAttribPointer(pi.col, 4, GL_UNSIGNED_BYTE, GL_TRUE, TV_STRIDE, TV_OFS(s_tv.getRGBA()));
		glEnableVertexAttribArray(pi.col);
	}
	if (!wireframe) {
		for (int i = 0; i < 8; i++) {
			if (pi.texco[i] >= 0 && i < *m_texco_num)
				batch_texco_attrib(pi.texco[i], m_texco[i], i);
		}
		if (pi.cl_texco >= 0) {
			/* the classic look's one texture: unit 0's coordinates (UV 0 when none) */
			const RAS_IRasterizer::TexCoGen gen = (*m_texco_num > 0) ? m_texco[0] : RAS_IRasterizer::RAS_TEXCO_UV;
			if (gen == RAS_IRasterizer::RAS_TEXCO_UV || gen == RAS_IRasterizer::RAS_TEXCO_ORCO ||
			    gen == RAS_IRasterizer::RAS_TEXCO_GLOB || gen == RAS_IRasterizer::RAS_TEXCO_NORM)
			{
				glVertexAttribPointer(pi.cl_texco, 2, GL_FLOAT, GL_FALSE, TV_STRIDE,
				                      gen == RAS_IRasterizer::RAS_TEXCO_UV ? TV_OFS(s_tv.getUV(0)) :
				                      gen == RAS_IRasterizer::RAS_TEXCO_NORM ? TV_OFS(s_tv.getNormal()) :
				                      TV_OFS(s_tv.getXYZ()));
				glEnableVertexAttribArray(pi.cl_texco);
			}
		}
	}

	if (instanced && m_instbuf) {
		/* the instance number: one per instance (the draw's base instance added) */
		glBindBuffer(GL_ARRAY_BUFFER, m_instbuf);
		glVertexAttribIPointer(15, 1, GL_UNSIGNED_INT, 0, 0);
		glVertexAttribDivisor(15, 1);
		glEnableVertexAttribArray(15);
	}

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	return vao;
}

void RAS_StorageBatch::DrawArray(RAS_MeshSlot& ms, RAS_DisplayArray *array, unsigned short *index, size_t totindex,
                                 unsigned int program, const ProgramInfo& pi, bool own, bool modified, bool zsort)
{
	const bool wireframe = m_drawingmode <= RAS_IRasterizer::KX_WIREFRAME;
	if (totindex == 0 || array->m_vertex.empty())
		return;

	RAS_BatchArray *ba = GetArray(array);
	if (modified && ba->uploaded != m_frame) {
		/* changed on the CPU (deformed, or by Python): once a frame */
		UploadVertices(ba);
		UploadIndices(ba, 0, ba->nindex);
		ba->uploaded = m_frame;
	}

	const size_t first = (size_t)(index - &array->m_index[0]);
	if (zsort)
		UploadIndices(ba, first, totindex);

	const GLenum mode = (array->m_type == RAS_DisplayArray::LINE) ? GL_LINES : GL_TRIANGLES;

	/* the colour, as the vertex arrays had it */
	bool color_array = false;
	float rgba[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	if (mode != GL_LINES && !wireframe) {
		if (ms.m_bObjectColor) {
			const MT_Vector4& c = ms.m_RGBAcolor;
			rgba[0] = (float)c[0];
			rgba[1] = (float)c[1];
			rgba[2] = (float)c[2];
			rgba[3] = (float)c[3];
		}
		else {
			color_array = true;
		}
	}
	if (!pi.core)
		glColor4fv(rgba);
	else if (pi.col >= 0 && !color_array)
		glVertexAttrib4fv(pi.col, rgba);

	const GLuint vao = GetVAO(ba, program, own, wireframe, color_array);
	const size_t gl_first = ba->quads ? first / 4 * 6 : first;
	const size_t gl_count = ba->quads ? totindex / 4 * 6 : totindex;

	/* BGE-Remix P8 Part B (P8.16): a lamp's shadow views in one pass, each
	 * an instance (a shader without the core front end cannot pick its
	 * layer: left out) */
	const int views = GPU_shadow_views_count();
	if (views > 0 && !pi.core) {
		static bool said = false;
		if (!said) {
			printf("BGE-Remix shadows: a caster whose shader is the old kind is left out of one-pass shadows\n");
			said = true;
		}
		return;
	}

	glBindVertexArray(vao);
	if (views > 0) {
		glDrawElementsInstanced(mode, (GLsizei)gl_count, GL_UNSIGNED_SHORT,
		                        (const void *)(uintptr_t)(gl_first * sizeof(unsigned short)), views);
	}
	else {
		glDrawElements(mode, (GLsizei)gl_count, GL_UNSIGNED_SHORT,
		               (const void *)(uintptr_t)(gl_first * sizeof(unsigned short)));
	}
	glBindVertexArray(0);
}

void RAS_StorageBatch::IndexPrimitives(RAS_MeshSlot& ms)
{
	const bool modified = ms.m_mesh && ms.m_mesh->MeshModified();
	const bool zsort = ms.m_bucket && ms.m_bucket->IsZSort() && m_drawingmode >= RAS_IRasterizer::KX_SOLID;

	/* BGE-Remix P8 Part B (P8.13): a mesh deformed on the graphics card */
	RAS_GPUSkin *skin = ms.m_pDeformer ? ms.m_pDeformer->GetGPUSkin() : NULL;
	if (skin && !ms.m_pDerivedArrays)
		SkinArrays(ms, skin, modified);

	/* the shader: the bound one; with none, the built-in depth shader for
	 * shadow maps, and the classic look for the rest (a material whose
	 * shader could not be built, Multitexture materials, and the solid,
	 * wireframe and bounding-box modes: BGE-Remix P8 Part B, the old
	 * fixed-function lighting is not set any more) */
	GLint bound = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &bound);
	GPUShader *own = NULL;
	if (bound == 0) {
		if (m_drawingmode == RAS_IRasterizer::KX_SHADOW) {
			own = GPU_builtin_game_depth();
		}
		else {
			own = GPU_builtin_shader(GPU_BUILTIN_CLASSIC);
			static bool said = false;
			if (own && !said && m_classic && *m_classic && m_drawingmode == RAS_IRasterizer::KX_TEXTURED) {
				printf("BGE-Remix: a material without its GLSL shader is drawn with the built-in classic shader\n");
				said = true;
			}
		}
		if (own) {
			GPU_shader_bind(own);
			bound = (GLint)GPU_shader_program(own);
		}
	}
	const ProgramInfo& pi = GetProgram((unsigned int)bound);

	/* BGE-Remix P8 Part B: the game's state, the colour drawn with (colour
	 * material follows it: the object colour, or the material's, which the
	 * vertex colours carry when the mesh has none) */
	GPUClassicState state = *m_state;
	if (ms.m_bObjectColor) {
		const MT_Vector4& c = ms.m_RGBAcolor;
		for (int k = 0; k < 4; k++)
			state.current_color[k] = (float)c[k];
	}
	else if (ms.m_bucket) {
		unsigned char rgba[4];
		ms.m_bucket->GetPolyMaterial()->GetMaterialRGBAColor(rgba);
		for (int k = 0; k < 4; k++)
			state.current_color[k] = rgba[k] / 255.0f;
	}

	if (own && own == GPU_builtin_shader(GPU_BUILTIN_CLASSIC)) {
		GPU_classic_texenv_from_gl(&state);
		GPU_matrix_uniforms(own);
		GPU_classic_uniforms(own, &state);
	}
	else if (bound) {
		/* the matrices and the old built-ins the shader reads */
		GPU_old_builtins((unsigned int)bound, &state);
	}

	/* a shader still the old kind (its translation did not build) reads the
	 * old matrices */
	if (!pi.core && !own)
		GPU_matrix_to_gl();

	/* the old alpha test only for the old kind of drawing (shaders do their own) */
	if (!pi.core && !own && state.alpha_test) {
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, state.alpha_ref);
	}
	else {
		glDisable(GL_ALPHA_TEST);
	}

	if (ms.m_pDerivedArrays) {
		/* a derived mesh (modifiers): its arrays for this slot's material */
		RAS_DisplayArrayList& arrays = *ms.m_pDerivedArrays;
		for (size_t a = 0; a < arrays.size(); a++) {
			RAS_DisplayArray *array = arrays[a];
			if (!array->m_index.empty())
				DrawArray(ms, array, &array->m_index[0], array->m_index.size(), (unsigned int)bound, pi,
				          own != NULL, modified, false);
		}
	}
	else {
		RAS_MeshSlot::iterator it;
		for (ms.begin(it); !ms.end(it); ms.next(it))
			DrawArray(ms, it.array, it.index, it.totindex, (unsigned int)bound, pi, own != NULL, modified, zsort);
	}

	if (own)
		GPU_shader_unbind();
}

/* ------------------------------------------------------------------------ */
/* BGE-Remix P8 Part B (P8.11): instances */

#define INST_MAX 65536            /* instance numbers (and instances a draw) */
#define CMD_MAX 4096              /* indirect commands a frame */
#define VIS_MAX (1024 * 1024)     /* visible instance numbers a frame */

bool RAS_StorageBatch::IndexPrimitivesInstanced(std::vector<RAS_MeshSlot *>& slots, const float view[16])
{
	if (slots.size() < 2 || !GPU_speed_feature("instancing") || !GPU_instancing_supported())
		return false;
	GLint bound = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &bound);
	if (!bound)
		return false;
	const ProgramInfo& pi = GetProgram((unsigned int)bound);
	if (!pi.core || pi.instanced < 0)
		return false;
	if (m_drawingmode != RAS_IRasterizer::KX_TEXTURED)
		return false;

	if (!m_instbuf) {
		std::vector<unsigned int> numbers(INST_MAX);
		for (unsigned int i = 0; i < INST_MAX; i++)
			numbers[i] = i;
		glCreateBuffers(1, &m_instbuf);
		glNamedBufferStorage(m_instbuf, sizeof(unsigned int) * INST_MAX, &numbers[0], 0);
		glCreateBuffers(1, &m_cmdbuf);
		glNamedBufferStorage(m_cmdbuf, sizeof(unsigned int) * 5 * CMD_MAX, NULL, GL_DYNAMIC_STORAGE_BIT);
		glCreateBuffers(1, &m_visbuf);
		glNamedBufferStorage(m_visbuf, sizeof(unsigned int) * VIS_MAX, NULL, 0);
		GPU_debug_label(GL_BUFFER, m_instbuf, "BGE-Remix instance numbers");
		GPU_debug_label(GL_BUFFER, m_cmdbuf, "BGE-Remix indirect draws");
		GPU_debug_label(GL_BUFFER, m_visbuf, "BGE-Remix visible instances");
	}

	RAS_MeshSlot& first = *slots[0];
	const bool modified = first.m_mesh && first.m_mesh->MeshModified();
	const size_t total = slots.size();

	/* the game's state for the old built-ins (the first slot's colour: the
	 * slots have no object colour) */
	GPUClassicState state = *m_state;
	if (first.m_bucket) {
		unsigned char rgba[4];
		first.m_bucket->GetPolyMaterial()->GetMaterialRGBAColor(rgba);
		for (int k = 0; k < 4; k++)
			state.current_color[k] = rgba[k] / 255.0f;
	}
	GPU_old_builtins((unsigned int)bound, &state);
	glDisable(GL_ALPHA_TEST);

	for (size_t start = 0; start < total; start += INST_MAX) {
		const size_t n = (total - start < INST_MAX) ? total - start : INST_MAX;

		/* the instances' model-view matrices: the view times each object's */
		GLuint buffer = 0;
		size_t offset = 0;
		float *mats = (float *)GPU_stream_alloc(n * 16 * sizeof(float), &buffer, &offset);
		if (!mats)
			return start > 0;
		MT_Matrix4x4 v;
		v.setValue(view);
		for (size_t i = 0; i < n; i++) {
			MT_Matrix4x4 o;
			o.setValue(slots[start + i]->m_OpenGLMatrix);
			MT_Matrix4x4 mv = v * o;
			mv.getValue(&mats[i * 16]);
		}
		GPU_stream_written(buffer);
		glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 8, buffer, (GLintptr)offset, (GLsizeiptr)(n * 16 * sizeof(float)));
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, m_visbuf);

		RAS_MeshSlot::iterator it;
		for (first.begin(it); !first.end(it); first.next(it)) {
			if (it.totindex == 0 || it.array->m_vertex.empty())
				continue;
			RAS_BatchArray *ba = GetArray(it.array);
			if (modified && ba->uploaded != m_frame) {
				UploadVertices(ba);
				UploadIndices(ba, 0, ba->nindex);
				ba->uploaded = m_frame;
			}
			const size_t firstidx = (size_t)(it.index - &it.array->m_index[0]);
			const GLenum mode = (it.array->m_type == RAS_DisplayArray::LINE) ? GL_LINES : GL_TRIANGLES;
			const bool color_array = !first.m_bObjectColor && mode != GL_LINES;
			if (pi.col >= 0 && !color_array) {
				const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
				glVertexAttrib4fv(pi.col, black);
			}
			const GLuint vao = GetVAO(ba, (unsigned int)bound, false, false, color_array, true);
			const size_t gl_first = ba->quads ? firstidx / 4 * 6 : firstidx;
			const size_t gl_count = ba->quads ? it.totindex / 4 * 6 : it.totindex;

			/* the culling pass (the main view, with last frame's depth) */
			bool culled = false;
			if (m_culling && m_cmdused < CMD_MAX && m_visused + n <= VIS_MAX) {
				const unsigned int cmd[5] = {(unsigned int)gl_count, 0u, (unsigned int)gl_first, 0u, m_visused};
				const unsigned int cmd_offset = m_cmdused * 5 * sizeof(unsigned int);
				glNamedBufferSubData(m_cmdbuf, cmd_offset, sizeof(cmd), cmd);
				if (m_culling->Cull((int)n, ba->center, ba->radius, view, m_cmdbuf, cmd_offset)) {
					glProgramUniform1i((GLuint)bound, pi.instanced, 2);
					glBindVertexArray(vao);
					glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_cmdbuf);
					glDrawElementsIndirect(mode, GL_UNSIGNED_SHORT, (const void *)(uintptr_t)cmd_offset);
					glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
					glBindVertexArray(0);
					m_cmdused++;
					m_visused += (unsigned int)n;
					culled = true;
				}
			}
			if (!culled) {
				glProgramUniform1i((GLuint)bound, pi.instanced, 1);
				glBindVertexArray(vao);
				glDrawElementsInstancedBaseInstance(mode, (GLsizei)gl_count, GL_UNSIGNED_SHORT,
				                                    (const void *)(uintptr_t)(gl_first * sizeof(unsigned short)),
				                                    (GLsizei)n, 0);
				glBindVertexArray(0);
			}
			m_inst_draws++;
		}
		m_inst_objects += (int)n;
	}
	glProgramUniform1i((GLuint)bound, pi.instanced, 0);
	return true;
}

/* ------------------------------------------------------------------------ */
/* BGE-Remix P8 Part B (P8.13): GPU skinning */

/* BGE-Remix P8 Part B (P8.13): GPU skinning's compute passes. Storage
 * buffers: 0 the mesh's static data (uint words), 1 this frame's bones and
 * key values (floats), 2 the scratch (per mesh vertex position and normal,
 * per face normal), 3 the array's map (per array vertex: mesh vertex, flat
 * face), 4 the array's vertex buffer (RAS_TexVert as floats). */
static const char *skin_cs_common =
        "layout(local_size_x = 64) in;\n"
        "layout(std430, binding = 0) readonly buffer Static { uint st[]; };\n"
        "layout(std430, binding = 1) readonly buffer Frame { float fr[]; };\n"
        "layout(std430, binding = 2) buffer Scratch { vec4 sc[]; };\n"
        "layout(std430, binding = 3) readonly buffer Map { uint mp[]; };\n"
        "layout(std430, binding = 4) buffer Verts { float vb[]; };\n"
        "uniform uint nverts, nfaces, ngroups, nkeys;\n"
        "uniform uint o_rest, o_woff, o_w, o_keys, o_faces, o_vfoff, o_vfaces, o_keyvals;\n"
        "uniform int normals;\n"
        "uniform uint count, stride, posofs, norofs;\n"
        "float sf(uint i) { return uintBitsToFloat(st[i]); }\n"
        "vec3 sv3(uint i) { return vec3(sf(i), sf(i + 1u), sf(i + 2u)); }\n"
        "mat4 bone(uint g, uint which)\n"
        "{\n"
        "	uint b = g * 32u + which * 16u;\n"
        "	return mat4(fr[b], fr[b + 1u], fr[b + 2u], fr[b + 3u], fr[b + 4u], fr[b + 5u], fr[b + 6u], fr[b + 7u],\n"
        "	            fr[b + 8u], fr[b + 9u], fr[b + 10u], fr[b + 11u], fr[b + 12u], fr[b + 13u], fr[b + 14u], fr[b + 15u]);\n"
        "}\n";

/* pass 1: per mesh vertex, the keys and the bones */
static const char *skin_cs_deform =
        "void main()\n"
        "{\n"
        "	uint v = gl_GlobalInvocationID.x;\n"
        "	if (v >= nverts) return;\n"
        "	vec3 p = sv3(o_rest + v * 8u);\n"
        "	vec3 n = sv3(o_rest + v * 8u + 4u);\n"
        "	for (uint k = 0u; k < nkeys; k++) {\n"
        "		float w = fr[o_keyvals + k];\n"
        "		if (w != 0.0) p += w * sv3(o_keys + (k * nverts + v) * 4u);\n"
        "	}\n"
        "	if (ngroups > 0u) {\n"
        "		uint a = st[o_woff + v], b = st[o_woff + v + 1u];\n"
        "		vec3 acc = vec3(0.0);\n"
        "		float contrib = 0.0, maxw = -1.0;\n"
        "		uint maxg = 0u;\n"
        "		for (uint j = a; j < b; j++) {\n"
        "			uint g = st[o_w + j * 2u];\n"
        "			float w = sf(o_w + j * 2u + 1u);\n"
        "			if (g >= ngroups || w <= 0.0) continue;\n"
        "			acc += ((bone(g, 0u) * vec4(p, 1.0)).xyz - p) * w;\n"
        "			contrib += w;\n"
        "			if (w > maxw) { maxw = w; maxg = g; }\n"
        "		}\n"
        "		if (contrib > 0.0001) p += acc / contrib;\n"
        "		if (normals == 1 && maxw > 0.0) n = mat3(bone(maxg, 1u)) * n;\n"
        "	}\n"
        "	sc[v] = vec4(p, 1.0);\n"
        "	sc[nverts + v] = vec4(n, 0.0);\n"
        "}\n";

/* pass 2: per face, its normal (as BL_MeshDeformer::RecalcNormals) */
static const char *skin_cs_facenormals =
        "void main()\n"
        "{\n"
        "	uint f = gl_GlobalInvocationID.x;\n"
        "	if (f >= nfaces) return;\n"
        "	uint i0 = st[o_faces + f * 4u], i1 = st[o_faces + f * 4u + 1u];\n"
        "	uint i2 = st[o_faces + f * 4u + 2u], i3 = st[o_faces + f * 4u + 3u];\n"
        "	vec3 c1 = sc[i0].xyz, c2 = sc[i1].xyz, c3 = sc[i2].xyz;\n"
        "	vec3 n1, n2;\n"
        "	if (i3 != 0xFFFFFFFFu) { vec3 c4 = sc[i3].xyz; n1 = c1 - c3; n2 = c2 - c4; }\n"
        "	else { n1 = c1 - c2; n2 = c2 - c3; }\n"
        "	vec3 fn = cross(n1, n2);\n"
        "	float l = length(fn);\n"
        "	sc[2u * nverts + f] = vec4(l > 0.0 ? fn / l : fn, 0.0);\n"
        "}\n";

/* pass 3: per mesh vertex, the sum of its faces' normals */
static const char *skin_cs_smooth =
        "void main()\n"
        "{\n"
        "	uint v = gl_GlobalInvocationID.x;\n"
        "	if (v >= nverts) return;\n"
        "	vec3 n = vec3(0.0);\n"
        "	for (uint j = st[o_vfoff + v]; j < st[o_vfoff + v + 1u]; j++)\n"
        "		n += sc[2u * nverts + st[o_vfaces + j]].xyz;\n"
        "	sc[nverts + v] = vec4(n, 0.0);\n"
        "}\n";

/* pass 4: per array vertex, into the vertex buffer */
static const char *skin_cs_write =
        "void main()\n"
        "{\n"
        "	uint i = gl_GlobalInvocationID.x;\n"
        "	if (i >= count) return;\n"
        "	uint v = mp[i * 2u], face = mp[i * 2u + 1u];\n"
        "	if (v >= nverts) return;\n"
        "	vec3 p = sc[v].xyz;\n"
        "	vec3 n = (normals == 2 && face != 0xFFFFFFFFu) ? sc[2u * nverts + face].xyz : sc[nverts + v].xyz;\n"
        "	float l = length(n);\n"
        "	if (l > 0.0) n /= l;\n"
        "	uint b = i * stride;\n"
        "	vb[b + posofs] = p.x; vb[b + posofs + 1u] = p.y; vb[b + posofs + 2u] = p.z;\n"
        "	vb[b + norofs] = n.x; vb[b + norofs + 1u] = n.y; vb[b + norofs + 2u] = n.z;\n"
        "}\n";

static const char *skin_uniform_names[17] = {
	"nverts", "nfaces", "ngroups", "nkeys", "o_rest", "o_woff", "o_w", "o_keys", "o_faces", "o_vfoff",
	"o_vfaces", "o_keyvals", "normals", "count", "stride", "posofs", "norofs",
};
enum {
	SU_NVERTS, SU_NFACES, SU_NGROUPS, SU_NKEYS, SU_O_REST, SU_O_WOFF, SU_O_W, SU_O_KEYS, SU_O_FACES, SU_O_VFOFF,
	SU_O_VFACES, SU_O_KEYVALS, SU_NORMALS, SU_COUNT, SU_STRIDE, SU_POSOFS, SU_NOROFS,
};
enum { SKIN_DEFORM, SKIN_FACENORMALS, SKIN_SMOOTH, SKIN_WRITE };

/* a skin's buffers on the card */
struct RAS_SkinGPU {
	GLuint st;                 /* the static data (binding 0) */
	GLuint sc;                 /* the scratch (binding 2) */
	unsigned int nfaces;
	unsigned int o[SU_O_KEYVALS + 1];  /* the offsets, by SU_* */
	unsigned int frame;        /* the skin frame the scratch holds (0: none) */
	std::map<const RAS_DisplayArray *, GLuint> maps;  /* each array's map (binding 3) */
};

static void skin_gpu_free(void *gpu)
{
	RAS_SkinGPU *g = (RAS_SkinGPU *)gpu;
	if (!g)
		return;
	if (g->st)
		glDeleteBuffers(1, &g->st);
	if (g->sc)
		glDeleteBuffers(1, &g->sc);
	for (std::map<const RAS_DisplayArray *, GLuint>::iterator it = g->maps.begin(); it != g->maps.end(); ++it) {
		if (it->second)
			glDeleteBuffers(1, &it->second);
	}
	delete g;
}

static unsigned int skin_put(std::vector<unsigned int>& words, const void *data, size_t count)
{
	const unsigned int offset = (unsigned int)words.size();
	words.resize(words.size() + count);
	if (count)
		memcpy(&words[offset], data, count * sizeof(unsigned int));
	return offset;
}

/* the skin's static data and scratch on the card */
static RAS_SkinGPU *skin_gpu_make(RAS_GPUSkin *skin)
{
	RAS_SkinGPU *g = new RAS_SkinGPU();
	g->st = g->sc = 0;
	g->frame = 0;
	g->nfaces = (unsigned int)(skin->faces.size() / 4);
	memset(g->o, 0, sizeof(g->o));

	std::vector<unsigned int> words;
	words.reserve(skin->rest.size() + skin->woffset.size() + skin->weights.size() + skin->keys.size() +
	              skin->faces.size() + skin->vfoffset.size() + skin->vfaces.size() + 1);
	g->o[SU_O_REST] = skin_put(words, skin->rest.empty() ? NULL : &skin->rest[0], skin->rest.size());
	g->o[SU_O_WOFF] = skin_put(words, skin->woffset.empty() ? NULL : &skin->woffset[0], skin->woffset.size());
	g->o[SU_O_W] = skin_put(words, skin->weights.empty() ? NULL : &skin->weights[0], skin->weights.size());
	g->o[SU_O_KEYS] = skin_put(words, skin->keys.empty() ? NULL : &skin->keys[0], skin->keys.size());
	g->o[SU_O_FACES] = skin_put(words, skin->faces.empty() ? NULL : &skin->faces[0], skin->faces.size());
	g->o[SU_O_VFOFF] = skin_put(words, skin->vfoffset.empty() ? NULL : &skin->vfoffset[0], skin->vfoffset.size());
	g->o[SU_O_VFACES] = skin_put(words, skin->vfaces.empty() ? NULL : &skin->vfaces[0], skin->vfaces.size());
	g->o[SU_O_KEYVALS] = (unsigned int)skin->bones.size();
	if (words.empty())
		words.push_back(0);

	glCreateBuffers(1, &g->st);
	glNamedBufferStorage(g->st, (GLsizeiptr)(words.size() * sizeof(unsigned int)), &words[0], 0);
	GPU_debug_label(GL_BUFFER, g->st, "BGE-Remix GPU skin data");
	const size_t scratch = (2 * (size_t)skin->nverts + g->nfaces + 1) * 4 * sizeof(float);
	glCreateBuffers(1, &g->sc);
	glNamedBufferStorage(g->sc, (GLsizeiptr)scratch, NULL, 0);
	GPU_debug_label(GL_BUFFER, g->sc, "BGE-Remix GPU skin scratch");
	return g;
}

bool RAS_StorageBatch::SkinPrograms()
{
	if (m_skinstate != 0)
		return m_skinstate == 1;
	const char *passes[4] = {skin_cs_deform, skin_cs_facenormals, skin_cs_smooth, skin_cs_write};
	const char *labels[4] = {"BGE-Remix GPU skinning", "BGE-Remix GPU skinning face normals",
	                         "BGE-Remix GPU skinning normals", "BGE-Remix GPU skinning write"};
	m_skinstate = 1;
	for (int p = 0; p < 4; p++) {
		std::string source = std::string(skin_cs_common) + passes[p];
		m_skinprog[p] = GPU_compute_program(source.c_str(), labels[p]);
		if (!m_skinprog[p]) {
			m_skinstate = -1;
			continue;
		}
		for (int u = 0; u < 17; u++)
			m_skinloc[p][u] = glGetUniformLocation(m_skinprog[p], skin_uniform_names[u]);
	}
	if (m_skinstate != 1) {
		/* the meshes are deformed on the CPU again (from the next frame) */
		printf("BGE-Remix: the GPU skinning shaders did not build; skinning is done on the CPU\n");
		GPU_speed_feature_set("gpu_skinning", false);
	}
	return m_skinstate == 1;
}

void RAS_StorageBatch::SkinArrays(RAS_MeshSlot& ms, RAS_GPUSkin *skin, bool modified)
{
	if (skin->nverts <= 0 || !SkinPrograms())
		return;

	/* what the passes change: the program and storage bindings 0 to 4 */
	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	GLint saved[5];
	GLint64 saved_start[5], saved_size[5];
	for (int i = 0; i < 5; i++) {
		glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &saved[i]);
		glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &saved_start[i]);
		glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &saved_size[i]);
	}

	if (!skin->gpu) {
		skin->gpu = skin_gpu_make(skin);
		skin->gpu_free = skin_gpu_free;
	}
	RAS_SkinGPU *g = (RAS_SkinGPU *)skin->gpu;
	const unsigned int nverts = (unsigned int)skin->nverts;
	const GLuint groups = (nverts + 63) / 64;
	bool wrote = false;

	/* the numbers every pass reads */
	unsigned int values[17];
	memset(values, 0, sizeof(values));
	values[SU_NVERTS] = nverts;
	values[SU_NFACES] = g->nfaces;
	values[SU_NGROUPS] = (unsigned int)skin->ngroups;
	values[SU_NKEYS] = (unsigned int)skin->nkeys;
	for (int u = SU_O_REST; u <= SU_O_KEYVALS; u++)
		values[u] = g->o[u];
	values[SU_STRIDE] = (unsigned int)(TV_STRIDE / 4);
	values[SU_POSOFS] = (unsigned int)((uintptr_t)TV_OFS(s_tv.getXYZ()) / 4);
	values[SU_NOROFS] = (unsigned int)((uintptr_t)TV_OFS(s_tv.getNormal()) / 4);

	if (g->frame != skin->frame) {
		/* this frame's bones and key values */
		const size_t nfloats = skin->bones.size() + skin->keyvalues.size();
		const size_t size = (nfloats ? nfloats : 1) * sizeof(float);
		GLuint buffer = 0;
		size_t offset = 0;
		float *data = (float *)GPU_stream_alloc(size, &buffer, &offset);
		if (!data)
			goto restore;
		if (!skin->bones.empty())
			memcpy(data, &skin->bones[0], skin->bones.size() * sizeof(float));
		if (!skin->keyvalues.empty())
			memcpy(data + skin->bones.size(), &skin->keyvalues[0], skin->keyvalues.size() * sizeof(float));
		GPU_stream_written(buffer);

		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g->st);
		glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, buffer, (GLintptr)offset, (GLsizeiptr)size);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g->sc);

		int passes[3] = {SKIN_DEFORM, -1, -1};
		if (skin->normals == RAS_GPUSkin::NORMALS_FACES && g->nfaces > 0) {
			passes[1] = SKIN_FACENORMALS;
			passes[2] = SKIN_SMOOTH;
		}
		for (int k = 0; k < 3; k++) {
			const int p = passes[k];
			if (p < 0)
				continue;
			glUseProgram(m_skinprog[p]);
			for (int u = 0; u < 17; u++) {
				if (m_skinloc[p][u] < 0)
					continue;
				if (u == SU_NORMALS)
					glUniform1i(m_skinloc[p][u], skin->normals);
				else
					glUniform1ui(m_skinloc[p][u], values[u]);
			}
			glDispatchCompute(p == SKIN_FACENORMALS ? (g->nfaces + 63) / 64 : groups, 1, 1);
			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		}
		g->frame = skin->frame;
	}

	{
		/* each array the slot draws: its buffer, written from the scratch */
		const int p = SKIN_WRITE;
		bool bound_pass = false;
		RAS_MeshSlot::iterator it;
		for (ms.begin(it); !ms.end(it); ms.next(it)) {
			RAS_DisplayArray *array = it.array;
			if (array->m_vertex.empty())
				continue;
			RAS_BatchArray *ba = GetArray(array);
			if (modified && ba->uploaded != m_frame) {
				/* as DrawArray would (the CPU's vertices first, then deformed) */
				UploadVertices(ba);
				UploadIndices(ba, 0, ba->nindex);
				ba->uploaded = m_frame;
			}
			if (ba->skinned == skin->frame)
				continue;

			std::map<const RAS_DisplayArray *, RAS_GPUSkin::Array>::iterator ait = skin->arrays.find(array);
			if (ait == skin->arrays.end() || ait->second.orig.size() != ba->nverts)
				continue;  /* not the deformer's array: drawn as it is */
			GLuint& map = g->maps[array];
			if (!map) {
				std::vector<unsigned int> pairs(ba->nverts * 2);
				for (size_t i = 0; i < ba->nverts; i++) {
					pairs[i * 2] = ait->second.orig[i];
					pairs[i * 2 + 1] = ait->second.face[i];
				}
				glCreateBuffers(1, &map);
				glNamedBufferStorage(map, (GLsizeiptr)(pairs.size() * sizeof(unsigned int)), &pairs[0], 0);
				GPU_debug_label(GL_BUFFER, map, "BGE-Remix GPU skin map");
			}

			if (!bound_pass) {
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, g->st);
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, g->sc);
				glUseProgram(m_skinprog[p]);
				for (int u = 0; u < 17; u++) {
					if (m_skinloc[p][u] < 0 || u == SU_COUNT)
						continue;
					if (u == SU_NORMALS)
						glUniform1i(m_skinloc[p][u], skin->normals);
					else
						glUniform1ui(m_skinloc[p][u], values[u]);
				}
				bound_pass = true;
			}
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, map);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ba->vbo);
			if (m_skinloc[p][SU_COUNT] >= 0)
				glUniform1ui(m_skinloc[p][SU_COUNT], (unsigned int)ba->nverts);
			glDispatchCompute((GLuint)((ba->nverts + 63) / 64), 1, 1);
			ba->skinned = skin->frame;
			wrote = true;
		}
	}

	if (wrote) {
		/* the buffers are read as vertices next (or copied over by an upload) */
		glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT |
		                GL_SHADER_STORAGE_BARRIER_BIT);
	}

restore:
	for (int i = 0; i < 5; i++) {
		if (saved[i] && saved_size[i] > 0)
			glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, (GLuint)saved[i], (GLintptr)saved_start[i],
			                  (GLsizeiptr)saved_size[i]);
		else
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, (GLuint)saved[i]);
	}
	glUseProgram((GLuint)program);
}
