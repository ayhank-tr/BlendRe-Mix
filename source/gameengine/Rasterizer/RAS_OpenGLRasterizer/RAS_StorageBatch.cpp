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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#include "RAS_StorageBatch.h"
#include "RAS_MaterialBucket.h"
#include "RAS_MeshObject.h"
#include "RAS_TexVert.h"

#include "GPU_glew.h"

extern "C" {
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_shader.h"
}

/* how a layout was made: the shader and what it was fed */
struct RAS_BatchLayout {
	unsigned int program;
	int flags;  /* 1: wireframe (no texture coordinates or attributes), 2: colour array, 4: our own shader */
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
    m_frame(1)
{
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
}

void RAS_StorageBatch::BeginFrame()
{
	m_frame++;
	/* programs can be made again under the same name: asked anew each frame */
	m_programs.clear();
	GPU_old_builtins_frame();
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
                                      bool color_array)
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
	layout.flags = (wireframe ? 1 : 0) | (color_array ? 2 : 0) | (own ? 4 : 0);
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

	glBindVertexArray(vao);
	glDrawElements(mode, (GLsizei)gl_count, GL_UNSIGNED_SHORT,
	               (const void *)(uintptr_t)(gl_first * sizeof(unsigned short)));
	glBindVertexArray(0);
}

void RAS_StorageBatch::IndexPrimitives(RAS_MeshSlot& ms)
{
	const bool modified = ms.m_mesh && ms.m_mesh->MeshModified();
	const bool zsort = ms.m_bucket && ms.m_bucket->IsZSort() && m_drawingmode >= RAS_IRasterizer::KX_SOLID;

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
