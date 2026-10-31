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

/** \file blender/gpu/intern/gpu_layer.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.2: the drawing layer (see GPU_layer.h). OpenGL 4.5 core
 * calls only: objects are created and edited with direct state access.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_shader.h"

#include "gpu_private.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------------ */
/* buffers */

struct GPUBuf {
	GLuint id;
	size_t size;
	bool dynamic;
};

GPUBuf *GPU_buf_create(size_t size, const void *data, bool dynamic, const char *label)
{
	GPUBuf *buf = MEM_callocN(sizeof(GPUBuf), "GPUBuf");
	buf->size = size;
	buf->dynamic = dynamic;
	glCreateBuffers(1, &buf->id);
	glNamedBufferStorage(buf->id, (GLsizeiptr)(size ? size : 4), data, dynamic ? GL_DYNAMIC_STORAGE_BIT : 0);
	GPU_debug_label(GL_BUFFER, buf->id, label);
	return buf;
}

void GPU_buf_update(GPUBuf *buf, size_t offset, size_t size, const void *data)
{
	if (buf && buf->dynamic && size && offset + size <= buf->size) {
		glNamedBufferSubData(buf->id, (GLintptr)offset, (GLsizeiptr)size, data);
	}
}

void GPU_buf_free(GPUBuf *buf)
{
	if (buf) {
		glDeleteBuffers(1, &buf->id);
		MEM_freeN(buf);
	}
}

unsigned int GPU_buf_id(const GPUBuf *buf)
{
	return buf ? buf->id : 0;
}

size_t GPU_buf_size(const GPUBuf *buf)
{
	return buf ? buf->size : 0;
}

/* ------------------------------------------------------------------------ */
/* streaming: one persistent, coherent buffer in three parts, each fenced
 * when left and waited for before it is written again */

#define STREAM_PARTS 3
#define STREAM_PART_SIZE ((size_t)4 * 1024 * 1024)
#define STREAM_ALIGN 256

static struct {
	GLuint id;
	unsigned char *mapped;
	GLsync fences[STREAM_PARTS];
	int part;
	size_t used;   /* within the current part */
	/* requests larger than a part get their own buffer for the frame */
	GLuint big[8];
	int nbig;
} gpu_stream = {0};

static void stream_init(void)
{
	if (gpu_stream.id) {
		return;
	}
	const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
	glCreateBuffers(1, &gpu_stream.id);
	glNamedBufferStorage(gpu_stream.id, (GLsizeiptr)(STREAM_PART_SIZE * STREAM_PARTS), NULL, flags);
	gpu_stream.mapped = glMapNamedBufferRange(gpu_stream.id, 0, (GLsizeiptr)(STREAM_PART_SIZE * STREAM_PARTS), flags);
	GPU_debug_label(GL_BUFFER, gpu_stream.id, "BGE-Remix stream");
	gpu_stream.part = 0;
	gpu_stream.used = 0;
}

static void stream_next_part(void)
{
	if (gpu_stream.fences[gpu_stream.part]) {
		glDeleteSync(gpu_stream.fences[gpu_stream.part]);
	}
	gpu_stream.fences[gpu_stream.part] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	gpu_stream.part = (gpu_stream.part + 1) % STREAM_PARTS;
	gpu_stream.used = 0;
	GLsync wait = gpu_stream.fences[gpu_stream.part];
	if (wait) {
		/* the card may still read this part: wait for it */
		while (glClientWaitSync(wait, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000) == GL_TIMEOUT_EXPIRED) {
			/* keep waiting */
		}
		glDeleteSync(wait);
		gpu_stream.fences[gpu_stream.part] = NULL;
	}
}

void *GPU_stream_alloc(size_t size, unsigned int *r_buffer, size_t *r_offset)
{
	stream_init();
	if (!gpu_stream.mapped || size > STREAM_PART_SIZE) {
		/* too large (or no mapping): a buffer of its own, freed at the frame's end */
		if (gpu_stream.nbig == 8) {
			GPU_stream_frame_end();
		}
		GLuint id;
		glCreateBuffers(1, &id);
		glNamedBufferStorage(id, (GLsizeiptr)size, NULL, GL_MAP_WRITE_BIT);
		gpu_stream.big[gpu_stream.nbig++] = id;
		*r_buffer = id;
		*r_offset = 0;
		/* written through a temporary mapping the caller fills: map now,
		 * unmapped at the next draw is not possible, so copy instead */
		void *ptr = glMapNamedBufferRange(id, 0, (GLsizeiptr)size, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
		return ptr;
	}
	size_t offset = (gpu_stream.used + (STREAM_ALIGN - 1)) & ~(size_t)(STREAM_ALIGN - 1);
	if (offset + size > STREAM_PART_SIZE) {
		stream_next_part();
		offset = 0;
	}
	gpu_stream.used = offset + size;
	*r_buffer = gpu_stream.id;
	*r_offset = (size_t)gpu_stream.part * STREAM_PART_SIZE + offset;
	return gpu_stream.mapped + *r_offset;
}

void GPU_stream_written(unsigned int buffer)
{
	if (buffer && buffer != gpu_stream.id) {
		GLint mapped = 0;
		glGetNamedBufferParameteriv(buffer, GL_BUFFER_MAPPED, &mapped);
		if (mapped) {
			glUnmapNamedBuffer(buffer);
		}
	}
}

static void stream_unmap_big(void)
{
	for (int i = 0; i < gpu_stream.nbig; i++) {
		GLint mapped = 0;
		glGetNamedBufferParameteriv(gpu_stream.big[i], GL_BUFFER_MAPPED, &mapped);
		if (mapped) {
			glUnmapNamedBuffer(gpu_stream.big[i]);
		}
	}
}

void GPU_stream_frame_end(void)
{
	if (!gpu_stream.id) {
		return;
	}
	stream_unmap_big();
	if (gpu_stream.nbig) {
		glDeleteBuffers(gpu_stream.nbig, gpu_stream.big);
		gpu_stream.nbig = 0;
	}
	if (gpu_stream.used) {
		stream_next_part();
	}
}

static void stream_exit(void)
{
	if (!gpu_stream.id) {
		return;
	}
	stream_unmap_big();
	if (gpu_stream.nbig) {
		glDeleteBuffers(gpu_stream.nbig, gpu_stream.big);
	}
	for (int i = 0; i < STREAM_PARTS; i++) {
		if (gpu_stream.fences[i]) {
			glDeleteSync(gpu_stream.fences[i]);
		}
	}
	glUnmapNamedBuffer(gpu_stream.id);
	glDeleteBuffers(1, &gpu_stream.id);
	memset(&gpu_stream, 0, sizeof(gpu_stream));
}

/* ------------------------------------------------------------------------ */
/* vertex formats */

static int comp_size(GPUCompType comp)
{
	switch (comp) {
		case GPU_COMP_U8: case GPU_COMP_I8: return 1;
		case GPU_COMP_U16: case GPU_COMP_I16: return 2;
		default: return 4;
	}
}

static GLenum comp_gl(GPUCompType comp)
{
	switch (comp) {
		case GPU_COMP_U8: return GL_UNSIGNED_BYTE;
		case GPU_COMP_I8: return GL_BYTE;
		case GPU_COMP_U16: return GL_UNSIGNED_SHORT;
		case GPU_COMP_I16: return GL_SHORT;
		case GPU_COMP_U32: return GL_UNSIGNED_INT;
		case GPU_COMP_I32: return GL_INT;
		default: return GL_FLOAT;
	}
}

void GPU_vertformat_clear(GPUVertFormat *format)
{
	memset(format, 0, sizeof(*format));
}

static int vertformat_add(GPUVertFormat *format, const char *name, GPUCompType comp, int len, bool normalize,
                          bool integer)
{
	if (format->count >= GPU_VERTFORMAT_MAX || len < 1 || len > 4) {
		return -1;
	}
	GPUVertAttr *a = &format->attrs[format->count];
	strncpy(a->name, name, sizeof(a->name) - 1);
	a->name[sizeof(a->name) - 1] = '\0';
	a->comp = (unsigned char)comp;
	a->len = (unsigned char)len;
	a->normalize = normalize;
	a->integer = integer;
	/* each attribute starts at 4 bytes */
	a->offset = (unsigned short)format->stride;
	format->stride += (comp_size(comp) * len + 3) & ~3;
	return format->count++;
}

int GPU_vertformat_add(GPUVertFormat *format, const char *name, GPUCompType comp, int len, bool normalize)
{
	return vertformat_add(format, name, comp, len, normalize, false);
}

int GPU_vertformat_add_int(GPUVertFormat *format, const char *name, GPUCompType comp, int len)
{
	return vertformat_add(format, name, comp, len, false, true);
}

/* a vertex array for a format, its attributes found by name in a program */
static GLuint vao_for_program(const GPUVertFormat *format, GLuint program, GLuint vbo, size_t offset, GLuint ibo)
{
	GLuint vao;
	glCreateVertexArrays(1, &vao);
	for (int i = 0; i < format->count; i++) {
		const GPUVertAttr *a = &format->attrs[i];
		const GLint loc = glGetAttribLocation(program, a->name);
		if (loc < 0) {
			continue;
		}
		glEnableVertexArrayAttrib(vao, (GLuint)loc);
		if (a->integer) {
			glVertexArrayAttribIFormat(vao, (GLuint)loc, a->len, comp_gl(a->comp), a->offset);
		}
		else {
			glVertexArrayAttribFormat(vao, (GLuint)loc, a->len, comp_gl(a->comp), a->normalize, a->offset);
		}
		glVertexArrayAttribBinding(vao, (GLuint)loc, 0);
	}
	glVertexArrayVertexBuffer(vao, 0, vbo, (GLintptr)offset, format->stride);
	if (ibo) {
		glVertexArrayElementBuffer(vao, ibo);
	}
	return vao;
}

/* ------------------------------------------------------------------------ */
/* batches */

#define BATCH_VAOS 4

struct GPUBatch {
	GLenum prim;
	GPUVertFormat format;
	GPUBuf *vbo, *ibo;
	int vertex_count, index_count;
	/* a vertex array per program it was drawn with (the attribute places differ) */
	GLuint programs[BATCH_VAOS], vaos[BATCH_VAOS];
	int next_vao;
	char label[64];
};

GPUBatch *GPU_batch_create(unsigned int prim, const GPUVertFormat *format,
                           const void *vertices, int vertex_count,
                           const unsigned int *indices, int index_count,
                           bool dynamic, const char *label)
{
	GPUBatch *batch = MEM_callocN(sizeof(GPUBatch), "GPUBatch");
	batch->prim = prim;
	batch->format = *format;
	batch->vertex_count = vertex_count;
	batch->index_count = indices ? index_count : 0;
	batch->vbo = GPU_buf_create((size_t)format->stride * (size_t)vertex_count, vertices, dynamic, label);
	if (indices && index_count > 0) {
		batch->ibo = GPU_buf_create(sizeof(unsigned int) * (size_t)index_count, indices, false, label);
	}
	if (label) {
		strncpy(batch->label, label, sizeof(batch->label) - 1);
	}
	return batch;
}

void GPU_batch_update(GPUBatch *batch, const void *vertices, int vertex_count)
{
	const size_t size = (size_t)batch->format.stride * (size_t)vertex_count;
	if (size <= GPU_buf_size(batch->vbo)) {
		GPU_buf_update(batch->vbo, 0, size, vertices);
		batch->vertex_count = vertex_count;
	}
}

static GLuint batch_vao(GPUBatch *batch, GLuint program)
{
	for (int i = 0; i < BATCH_VAOS; i++) {
		if (batch->vaos[i] && batch->programs[i] == program) {
			return batch->vaos[i];
		}
	}
	const int i = batch->next_vao;
	batch->next_vao = (batch->next_vao + 1) % BATCH_VAOS;
	if (batch->vaos[i]) {
		glDeleteVertexArrays(1, &batch->vaos[i]);
	}
	batch->vaos[i] = vao_for_program(&batch->format, program, GPU_buf_id(batch->vbo), 0, GPU_buf_id(batch->ibo));
	batch->programs[i] = program;
	return batch->vaos[i];
}

void GPU_batch_draw_instanced(GPUBatch *batch, struct GPUShader *shader, int instances)
{
	if (!batch || !shader || batch->vertex_count <= 0 || instances <= 0) {
		return;
	}
	const GLuint program = GPU_shader_program(shader);
	GPU_shader_bind(shader);
	GPU_matrix_uniforms(shader);
	glBindVertexArray(batch_vao(batch, program));
	if (batch->ibo) {
		glDrawElementsInstanced(batch->prim, batch->index_count, GL_UNSIGNED_INT, NULL, instances);
	}
	else {
		glDrawArraysInstanced(batch->prim, 0, batch->vertex_count, instances);
	}
	glBindVertexArray(0);
}

void GPU_batch_draw(GPUBatch *batch, struct GPUShader *shader)
{
	GPU_batch_draw_instanced(batch, shader, 1);
}

void GPU_batch_free(GPUBatch *batch)
{
	if (!batch) {
		return;
	}
	for (int i = 0; i < BATCH_VAOS; i++) {
		if (batch->vaos[i]) {
			glDeleteVertexArrays(1, &batch->vaos[i]);
		}
	}
	GPU_buf_free(batch->vbo);
	GPU_buf_free(batch->ibo);
	MEM_freeN(batch);
}

/* ------------------------------------------------------------------------ */
/* matrices: column-major float[4][4] as Blender keeps them (m[column][row]) */

#define MATRIX_DEPTH 32

static struct {
	float stack[2][MATRIX_DEPTH][4][4];
	int top[2];
	GPUMatrixMode mode;
	bool init;
} gpu_mat = {{{{{0}}}}};

static void m4_identity(float m[4][4])
{
	memset(m, 0, sizeof(float[4][4]));
	m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

static void m4_mul(float r[4][4], const float a[4][4], const float b[4][4])
{
	float t[4][4];
	for (int c = 0; c < 4; c++) {
		for (int row = 0; row < 4; row++) {
			t[c][row] = a[0][row] * b[c][0] + a[1][row] * b[c][1] + a[2][row] * b[c][2] + a[3][row] * b[c][3];
		}
	}
	memcpy(r, t, sizeof(t));
}

/* general inverse (cofactors); false when singular */
static bool m4_invert(float r[4][4], const float m[4][4])
{
	const float *a = &m[0][0];
	float inv[16];
	inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
	inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
	inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
	inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
	inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
	inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
	inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
	inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
	inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
	inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
	inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
	inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
	inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
	inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
	inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
	inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
	float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
	if (det == 0.0f) {
		m4_identity(r);
		return false;
	}
	det = 1.0f / det;
	for (int i = 0; i < 16; i++) {
		(&r[0][0])[i] = inv[i] * det;
	}
	return true;
}

static void mat_init(void)
{
	if (!gpu_mat.init) {
		m4_identity(gpu_mat.stack[0][0]);
		m4_identity(gpu_mat.stack[1][0]);
		gpu_mat.init = true;
	}
}

static float (*mat_current(void))[4]
{
	mat_init();
	return gpu_mat.stack[gpu_mat.mode][gpu_mat.top[gpu_mat.mode]];
}

void GPU_matrix_mode(GPUMatrixMode mode)
{
	gpu_mat.mode = mode;
}

void GPU_matrix_push(void)
{
	mat_init();
	int *top = &gpu_mat.top[gpu_mat.mode];
	if (*top + 1 < MATRIX_DEPTH) {
		memcpy(gpu_mat.stack[gpu_mat.mode][*top + 1], gpu_mat.stack[gpu_mat.mode][*top], sizeof(float[4][4]));
		(*top)++;
	}
	else {
		fprintf(stderr, "BGE-Remix layer: matrix stack full\n");
	}
}

void GPU_matrix_pop(void)
{
	int *top = &gpu_mat.top[gpu_mat.mode];
	if (*top > 0) {
		(*top)--;
	}
	else {
		fprintf(stderr, "BGE-Remix layer: matrix stack empty\n");
	}
}

void GPU_matrix_identity(void)
{
	m4_identity(mat_current());
}

void GPU_matrix_load(const float m[4][4])
{
	memcpy(mat_current(), m, sizeof(float[4][4]));
}

void GPU_matrix_mult(const float m[4][4])
{
	float (*cur)[4] = mat_current();
	m4_mul(cur, cur, m);
}

void GPU_matrix_translate(float x, float y, float z)
{
	float m[4][4];
	m4_identity(m);
	m[3][0] = x;
	m[3][1] = y;
	m[3][2] = z;
	GPU_matrix_mult(m);
}

void GPU_matrix_scale(float x, float y, float z)
{
	float m[4][4];
	m4_identity(m);
	m[0][0] = x;
	m[1][1] = y;
	m[2][2] = z;
	GPU_matrix_mult(m);
}

void GPU_matrix_rotate(float degrees, float x, float y, float z)
{
	const float len = sqrtf(x * x + y * y + z * z);
	if (len == 0.0f) {
		return;
	}
	x /= len;
	y /= len;
	z /= len;
	const float a = degrees * (float)(M_PI / 180.0);
	const float c = cosf(a), s = sinf(a), t = 1.0f - c;
	float m[4][4];
	m4_identity(m);
	/* glRotate's matrix, column-major */
	m[0][0] = x * x * t + c;     m[1][0] = x * y * t - z * s; m[2][0] = x * z * t + y * s;
	m[0][1] = y * x * t + z * s; m[1][1] = y * y * t + c;     m[2][1] = y * z * t - x * s;
	m[0][2] = x * z * t - y * s; m[1][2] = y * z * t + x * s; m[2][2] = z * z * t + c;
	GPU_matrix_mult(m);
}

void GPU_matrix_ortho(float left, float right, float bottom, float top, float znear, float zfar)
{
	float m[4][4];
	m4_identity(m);
	m[0][0] = 2.0f / (right - left);
	m[1][1] = 2.0f / (top - bottom);
	m[2][2] = -2.0f / (zfar - znear);
	m[3][0] = -(right + left) / (right - left);
	m[3][1] = -(top + bottom) / (top - bottom);
	m[3][2] = -(zfar + znear) / (zfar - znear);
	GPU_matrix_mult(m);
}

void GPU_matrix_frustum(float left, float right, float bottom, float top, float znear, float zfar)
{
	float m[4][4];
	memset(m, 0, sizeof(m));
	m[0][0] = 2.0f * znear / (right - left);
	m[1][1] = 2.0f * znear / (top - bottom);
	m[2][0] = (right + left) / (right - left);
	m[2][1] = (top + bottom) / (top - bottom);
	m[2][2] = -(zfar + znear) / (zfar - znear);
	m[2][3] = -1.0f;
	m[3][2] = -2.0f * zfar * znear / (zfar - znear);
	GPU_matrix_mult(m);
}

/* for gpu_builtin.c */
void gpu_layer_m4_mul(float r[4][4], const float a[4][4], const float b[4][4])
{
	m4_mul(r, a, b);
}

bool gpu_layer_m4_invert(float r[4][4], const float m[4][4])
{
	return m4_invert(r, m);
}

/* BGE-Remix P8 Part B: texture matrices, one per unit */
static struct {
	float m[8][4][4];
	bool init;
} gpu_texmat = {{{{0}}}};

static void texmat_init(void)
{
	if (!gpu_texmat.init) {
		for (int i = 0; i < 8; i++) {
			m4_identity(gpu_texmat.m[i]);
		}
		gpu_texmat.init = true;
	}
}

void GPU_texture_matrix_set(int unit, const float m[4][4])
{
	texmat_init();
	if (unit < 0 || unit >= 8) {
		return;
	}
	if (m) {
		memcpy(gpu_texmat.m[unit], m, sizeof(float[4][4]));
	}
	else {
		m4_identity(gpu_texmat.m[unit]);
	}
}

void GPU_texture_matrix_get(int unit, float r_m[4][4])
{
	texmat_init();
	if (unit < 0 || unit >= 8) {
		m4_identity(r_m);
		return;
	}
	memcpy(r_m, gpu_texmat.m[unit], sizeof(float[4][4]));
}

void GPU_matrix_get(GPUMatrixMode mode, float r_m[4][4])
{
	mat_init();
	memcpy(r_m, gpu_mat.stack[mode][gpu_mat.top[mode]], sizeof(float[4][4]));
}

void GPU_matrix_from_gl(void)
{
	mat_init();
	if (GPU_caps()->core_profile) {
		return;
	}
	glGetFloatv(GL_MODELVIEW_MATRIX, &gpu_mat.stack[0][gpu_mat.top[0]][0][0]);
	glGetFloatv(GL_PROJECTION_MATRIX, &gpu_mat.stack[1][gpu_mat.top[1]][0][0]);
}

void GPU_matrix_to_gl(void)
{
	mat_init();
	if (GPU_caps()->core_profile) {
		return;
	}
	GLint mode;
	glGetIntegerv(GL_MATRIX_MODE, &mode);
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(&gpu_mat.stack[0][gpu_mat.top[0]][0][0]);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(&gpu_mat.stack[1][gpu_mat.top[1]][0][0]);
	glMatrixMode((GLenum)mode);
}

void GPU_matrix_uniforms(struct GPUShader *shader)
{
	mat_init();
	const GLuint program = GPU_shader_program(shader);
	const float (*mv)[4] = (const float (*)[4])gpu_mat.stack[0][gpu_mat.top[0]];
	const float (*proj)[4] = (const float (*)[4])gpu_mat.stack[1][gpu_mat.top[1]];
	GLint loc;

	if ((loc = glGetUniformLocation(program, "bge_mv")) >= 0) {
		glProgramUniformMatrix4fv(program, loc, 1, GL_FALSE, &mv[0][0]);
	}
	if ((loc = glGetUniformLocation(program, "bge_proj")) >= 0) {
		glProgramUniformMatrix4fv(program, loc, 1, GL_FALSE, &proj[0][0]);
	}
	if ((loc = glGetUniformLocation(program, "bge_mvp")) >= 0) {
		float mvp[4][4];
		m4_mul(mvp, proj, mv);
		glProgramUniformMatrix4fv(program, loc, 1, GL_FALSE, &mvp[0][0]);
	}
	const GLint mvinv_loc = glGetUniformLocation(program, "bge_mvinv");
	const GLint nor_loc = glGetUniformLocation(program, "bge_normalmat");
	if (mvinv_loc >= 0 || nor_loc >= 0) {
		float inv[4][4];
		m4_invert(inv, mv);
		if (mvinv_loc >= 0) {
			glProgramUniformMatrix4fv(program, mvinv_loc, 1, GL_FALSE, &inv[0][0]);
		}
		if (nor_loc >= 0) {
			/* the transpose of the inverse's upper 3x3 */
			float n[3][3];
			for (int c = 0; c < 3; c++) {
				for (int r = 0; r < 3; r++) {
					n[c][r] = inv[r][c];
				}
			}
			glProgramUniformMatrix3fv(program, nor_loc, 1, GL_FALSE, &n[0][0]);
		}
	}
	if ((loc = glGetUniformLocation(program, "bge_projinv")) >= 0) {
		float inv[4][4];
		m4_invert(inv, proj);
		glProgramUniformMatrix4fv(program, loc, 1, GL_FALSE, &inv[0][0]);
	}
}

/* ------------------------------------------------------------------------ */
/* state */

static void set_enabled_offset(GLenum cap, bool on)
{
	if (on) {
		glEnable(cap);
	}
	else {
		glDisable(cap);
	}
}

void GPU_state_blend(GPULayerBlend blend)
{
	if (blend == GPU_LAYER_BLEND_NONE) {
		glDisable(GL_BLEND);
		return;
	}
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	switch (blend) {
		case GPU_LAYER_BLEND_ALPHA:
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case GPU_LAYER_BLEND_ALPHA_PREMUL:
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case GPU_LAYER_BLEND_ADD:
			glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			break;
		case GPU_LAYER_BLEND_MULTIPLY:
			glBlendFunc(GL_DST_COLOR, GL_ZERO);
			break;
		default:
			break;
	}
}

void GPU_state_depth(bool test, bool write)
{
	if (test) {
		glEnable(GL_DEPTH_TEST);
	}
	else {
		glDisable(GL_DEPTH_TEST);
	}
	glDepthMask(write ? GL_TRUE : GL_FALSE);
}

void GPU_state_cull(bool on, bool back)
{
	if (on) {
		glEnable(GL_CULL_FACE);
		glCullFace(back ? GL_BACK : GL_FRONT);
	}
	else {
		glDisable(GL_CULL_FACE);
	}
}

void GPU_state_viewport(int x, int y, int w, int h)
{
	glViewport(x, y, w, h);
}

void GPU_state_scissor(bool on, int x, int y, int w, int h)
{
	if (on) {
		glEnable(GL_SCISSOR_TEST);
		glScissor(x, y, w, h);
	}
	else {
		glDisable(GL_SCISSOR_TEST);
	}
}

void GPU_state_polygon_offset(float factor, float units, bool lines)
{
	const bool on = (factor != 0.0f || units != 0.0f);
	glPolygonOffset(factor, units);
	set_enabled_offset(GL_POLYGON_OFFSET_FILL, on && !lines);
	set_enabled_offset(GL_POLYGON_OFFSET_LINE, on && lines);
}

enum {
	SS_DRAW_FB, SS_READ_FB, SS_PROGRAM, SS_VAO, SS_ACTIVE_TEX, SS_TEX2D, SS_TEX2D_UNIT0, SS_ARRAY_BUF,
	SS_BLEND, SS_BLEND_SRC_RGB, SS_BLEND_DST_RGB, SS_BLEND_SRC_A, SS_BLEND_DST_A, SS_BLEND_EQ_RGB, SS_BLEND_EQ_A,
	SS_DEPTH_TEST, SS_DEPTH_MASK, SS_DEPTH_FUNC, SS_CULL, SS_CULL_MODE, SS_FRONT_FACE,
	SS_SCISSOR, SS_STENCIL, SS_POLY_OFFSET, SS_POINT_SIZE_PROG, SS_COLOR_MASK, SS_POLYGON_MODE,
	SS_UNPACK_ALIGN, SS_PACK_ALIGN, SS_MATRIX_MODE,
	SS_VIEWPORT,                 /* 4 */
	SS_SCISSOR_BOX = SS_VIEWPORT + 4,  /* 4 */
	SS_COUNT = SS_SCISSOR_BOX + 4,
};

static void set_enabled(GLenum cap, int on)
{
	if (on) {
		glEnable(cap);
	}
	else {
		glDisable(cap);
	}
}

void GPU_state_scope_begin(GPUStateScope *scope)
{
	int *v = scope->ints;
	memset(scope, 0, sizeof(*scope));
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &v[SS_DRAW_FB]);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &v[SS_READ_FB]);
	glGetIntegerv(GL_CURRENT_PROGRAM, &v[SS_PROGRAM]);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &v[SS_VAO]);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &v[SS_ACTIVE_TEX]);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &v[SS_TEX2D]);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &v[SS_TEX2D_UNIT0]);
	glActiveTexture((GLenum)v[SS_ACTIVE_TEX]);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &v[SS_ARRAY_BUF]);
	v[SS_BLEND] = glIsEnabled(GL_BLEND);
	glGetIntegerv(GL_BLEND_SRC_RGB, &v[SS_BLEND_SRC_RGB]);
	glGetIntegerv(GL_BLEND_DST_RGB, &v[SS_BLEND_DST_RGB]);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &v[SS_BLEND_SRC_A]);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &v[SS_BLEND_DST_A]);
	glGetIntegerv(GL_BLEND_EQUATION_RGB, &v[SS_BLEND_EQ_RGB]);
	glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &v[SS_BLEND_EQ_A]);
	v[SS_DEPTH_TEST] = glIsEnabled(GL_DEPTH_TEST);
	GLboolean b = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &b);
	v[SS_DEPTH_MASK] = b;
	glGetIntegerv(GL_DEPTH_FUNC, &v[SS_DEPTH_FUNC]);
	v[SS_CULL] = glIsEnabled(GL_CULL_FACE);
	glGetIntegerv(GL_CULL_FACE_MODE, &v[SS_CULL_MODE]);
	glGetIntegerv(GL_FRONT_FACE, &v[SS_FRONT_FACE]);
	v[SS_SCISSOR] = glIsEnabled(GL_SCISSOR_TEST);
	v[SS_STENCIL] = glIsEnabled(GL_STENCIL_TEST);
	v[SS_POLY_OFFSET] = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	v[SS_POINT_SIZE_PROG] = glIsEnabled(GL_PROGRAM_POINT_SIZE);
	GLboolean cm[4] = {1, 1, 1, 1};
	glGetBooleanv(GL_COLOR_WRITEMASK, cm);
	v[SS_COLOR_MASK] = cm[0] | (cm[1] << 1) | (cm[2] << 2) | (cm[3] << 3);
	GLint pm[2] = {GL_FILL, GL_FILL};
	glGetIntegerv(GL_POLYGON_MODE, pm);
	v[SS_POLYGON_MODE] = pm[0];
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &v[SS_UNPACK_ALIGN]);
	glGetIntegerv(GL_PACK_ALIGNMENT, &v[SS_PACK_ALIGN]);
	glGetIntegerv(GL_VIEWPORT, &v[SS_VIEWPORT]);
	glGetIntegerv(GL_SCISSOR_BOX, &v[SS_SCISSOR_BOX]);
	scope->legacy = !GPU_caps()->core_profile;
	if (scope->legacy) {
		glGetIntegerv(GL_MATRIX_MODE, &v[SS_MATRIX_MODE]);
		glGetFloatv(GL_MODELVIEW_MATRIX, &scope->matrices[0]);
		glGetFloatv(GL_PROJECTION_MATRIX, &scope->matrices[16]);
	}
}

void GPU_state_scope_end(const GPUStateScope *scope)
{
	const int *v = scope->ints;
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)v[SS_DRAW_FB]);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)v[SS_READ_FB]);
	glUseProgram((GLuint)v[SS_PROGRAM]);
	glBindVertexArray((GLuint)v[SS_VAO]);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, (GLuint)v[SS_TEX2D_UNIT0]);
	glActiveTexture((GLenum)v[SS_ACTIVE_TEX]);
	glBindTexture(GL_TEXTURE_2D, (GLuint)v[SS_TEX2D]);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)v[SS_ARRAY_BUF]);
	set_enabled(GL_BLEND, v[SS_BLEND]);
	glBlendFuncSeparate((GLenum)v[SS_BLEND_SRC_RGB], (GLenum)v[SS_BLEND_DST_RGB],
	                    (GLenum)v[SS_BLEND_SRC_A], (GLenum)v[SS_BLEND_DST_A]);
	glBlendEquationSeparate((GLenum)v[SS_BLEND_EQ_RGB], (GLenum)v[SS_BLEND_EQ_A]);
	set_enabled(GL_DEPTH_TEST, v[SS_DEPTH_TEST]);
	glDepthMask(v[SS_DEPTH_MASK] ? GL_TRUE : GL_FALSE);
	glDepthFunc((GLenum)v[SS_DEPTH_FUNC]);
	set_enabled(GL_CULL_FACE, v[SS_CULL]);
	glCullFace((GLenum)v[SS_CULL_MODE]);
	glFrontFace((GLenum)v[SS_FRONT_FACE]);
	set_enabled(GL_SCISSOR_TEST, v[SS_SCISSOR]);
	set_enabled(GL_STENCIL_TEST, v[SS_STENCIL]);
	set_enabled(GL_POLYGON_OFFSET_FILL, v[SS_POLY_OFFSET]);
	set_enabled(GL_PROGRAM_POINT_SIZE, v[SS_POINT_SIZE_PROG]);
	glColorMask(v[SS_COLOR_MASK] & 1, (v[SS_COLOR_MASK] >> 1) & 1, (v[SS_COLOR_MASK] >> 2) & 1,
	            (v[SS_COLOR_MASK] >> 3) & 1);
	glPolygonMode(GL_FRONT_AND_BACK, (GLenum)v[SS_POLYGON_MODE]);
	glPixelStorei(GL_UNPACK_ALIGNMENT, v[SS_UNPACK_ALIGN]);
	glPixelStorei(GL_PACK_ALIGNMENT, v[SS_PACK_ALIGN]);
	glViewport(v[SS_VIEWPORT], v[SS_VIEWPORT + 1], v[SS_VIEWPORT + 2], v[SS_VIEWPORT + 3]);
	glScissor(v[SS_SCISSOR_BOX], v[SS_SCISSOR_BOX + 1], v[SS_SCISSOR_BOX + 2], v[SS_SCISSOR_BOX + 3]);
	if (scope->legacy) {
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(&scope->matrices[0]);
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf(&scope->matrices[16]);
		glMatrixMode((GLenum)v[SS_MATRIX_MODE]);
	}
}

/* ------------------------------------------------------------------------ */
/* immediate drawing */

typedef struct ImmVert {
	float pos[3];
	float col[4];
	float texco[2];
	float nor[3];
} ImmVert;

#define IMM_VAOS 8

static struct {
	bool active;
	GLenum prim;
	GPULayerShader shader;
	ImmVert cur;
	ImmVert *verts;
	int len, cap;
	GPUVertFormat format;
	GLuint vao_programs[IMM_VAOS], vaos[IMM_VAOS];
	float color[4];
} gpu_imm = {false};

static void imm_format(void)
{
	if (gpu_imm.format.count) {
		return;
	}
	GPU_vertformat_add(&gpu_imm.format, "bge_pos", GPU_COMP_F32, 3, false);
	GPU_vertformat_add(&gpu_imm.format, "bge_col", GPU_COMP_F32, 4, false);
	GPU_vertformat_add(&gpu_imm.format, "bge_texco", GPU_COMP_F32, 2, false);
	GPU_vertformat_add(&gpu_imm.format, "bge_nor", GPU_COMP_F32, 3, false);
}

void GPU_imm_begin(unsigned int prim, GPULayerShader shader)
{
	imm_format();
	gpu_imm.active = true;
	gpu_imm.prim = prim;
	gpu_imm.shader = shader;
	gpu_imm.len = 0;
	if (gpu_imm.cur.col[3] == 0.0f && gpu_imm.cur.col[0] == 0.0f) {
		gpu_imm.cur.col[0] = gpu_imm.cur.col[1] = gpu_imm.cur.col[2] = gpu_imm.cur.col[3] = 1.0f;
	}
}

void GPU_imm_color4f(float r, float g, float b, float a)
{
	gpu_imm.cur.col[0] = r;
	gpu_imm.cur.col[1] = g;
	gpu_imm.cur.col[2] = b;
	gpu_imm.cur.col[3] = a;
}

void GPU_imm_color4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	GPU_imm_color4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

void GPU_imm_texco2f(float u, float v)
{
	gpu_imm.cur.texco[0] = u;
	gpu_imm.cur.texco[1] = v;
}

void GPU_imm_normal3f(float x, float y, float z)
{
	gpu_imm.cur.nor[0] = x;
	gpu_imm.cur.nor[1] = y;
	gpu_imm.cur.nor[2] = z;
}

void GPU_imm_vertex3f(float x, float y, float z)
{
	if (!gpu_imm.active) {
		return;
	}
	if (gpu_imm.len == gpu_imm.cap) {
		gpu_imm.cap = gpu_imm.cap ? gpu_imm.cap * 2 : 256;
		gpu_imm.verts = MEM_reallocN_id(gpu_imm.verts, sizeof(ImmVert) * (size_t)gpu_imm.cap, "imm verts");
	}
	ImmVert *v = &gpu_imm.verts[gpu_imm.len++];
	*v = gpu_imm.cur;
	v->pos[0] = x;
	v->pos[1] = y;
	v->pos[2] = z;
}

void GPU_imm_vertex2f(float x, float y)
{
	GPU_imm_vertex3f(x, y, 0.0f);
}

void GPU_imm_uniform_color(float r, float g, float b, float a)
{
	gpu_imm.color[0] = r;
	gpu_imm.color[1] = g;
	gpu_imm.color[2] = b;
	gpu_imm.color[3] = a;
}

/* quads and polygons as triangles; returns the vertex count to draw */
static int imm_triangulate(GLenum *r_prim, ImmVert **r_verts, bool *r_owned)
{
	const int n = gpu_imm.len;
	*r_owned = false;
	*r_verts = gpu_imm.verts;
	if (gpu_imm.prim == GL_QUADS) {
		const int quads = n / 4;
		ImmVert *t = MEM_mallocN(sizeof(ImmVert) * (size_t)(quads * 6 + 1), "imm quads");
		for (int q = 0; q < quads; q++) {
			const ImmVert *v = &gpu_imm.verts[q * 4];
			t[q * 6 + 0] = v[0]; t[q * 6 + 1] = v[1]; t[q * 6 + 2] = v[2];
			t[q * 6 + 3] = v[0]; t[q * 6 + 4] = v[2]; t[q * 6 + 5] = v[3];
		}
		*r_prim = GL_TRIANGLES;
		*r_verts = t;
		*r_owned = true;
		return quads * 6;
	}
	if (gpu_imm.prim == GL_QUAD_STRIP) {
		*r_prim = GL_TRIANGLE_STRIP;
		return n;
	}
	if (gpu_imm.prim == GL_POLYGON) {
		*r_prim = GL_TRIANGLE_FAN;
		return n;
	}
	*r_prim = gpu_imm.prim;
	return n;
}

void GPU_imm_end(void)
{
	if (!gpu_imm.active) {
		return;
	}
	gpu_imm.active = false;
	if (gpu_imm.len == 0) {
		return;
	}
	struct GPUShader *shader = GPU_builtin_shader(gpu_imm.shader);
	if (!shader) {
		return;
	}
	GLenum prim;
	ImmVert *verts;
	bool owned;
	const int count = imm_triangulate(&prim, &verts, &owned);
	if (count > 0) {
		GLuint buffer;
		size_t offset;
		const size_t size = sizeof(ImmVert) * (size_t)count;
		void *dst = GPU_stream_alloc(size, &buffer, &offset);
		if (dst) {
			memcpy(dst, verts, size);
			if (buffer != gpu_stream.id) {
				glUnmapNamedBuffer(buffer);
			}
			const GLuint program = GPU_shader_program(shader);
			int slot = -1;
			for (int i = 0; i < IMM_VAOS; i++) {
				if (gpu_imm.vaos[i] && gpu_imm.vao_programs[i] == program) {
					slot = i;
					break;
				}
			}
			if (slot < 0) {
				for (slot = 0; slot < IMM_VAOS - 1 && gpu_imm.vaos[slot]; slot++) {
					/* the first free place (the last is reused) */
				}
				if (gpu_imm.vaos[slot]) {
					glDeleteVertexArrays(1, &gpu_imm.vaos[slot]);
				}
				gpu_imm.vaos[slot] = vao_for_program(&gpu_imm.format, program, buffer, offset, 0);
				gpu_imm.vao_programs[slot] = program;
			}
			const GLuint vao = gpu_imm.vaos[slot];
			glVertexArrayVertexBuffer(vao, 0, buffer, (GLintptr)offset, gpu_imm.format.stride);

			/* BGE-Remix P8 Part B: the program bound before is bound again
			 * after (the game's drawing reads which one is bound) */
			GLint prev_program = 0;
			glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
			GPU_shader_bind(shader);
			GPU_matrix_uniforms(shader);
			const GLint color_loc = glGetUniformLocation(program, "bge_color");
			if (color_loc >= 0) {
				glUniform4fv(color_loc, 1, gpu_imm.color);
			}
			/* (a compatibility context gives gl_PointCoord only with point
			 * sprites on: NVIDIA's; Mesa's always does) */
			GLboolean sprite = GL_FALSE;
			if (gpu_imm.shader == GPU_BUILTIN_POINT) {
				glEnable(GL_PROGRAM_POINT_SIZE);
				if (!GPU_caps()->core_profile) {
					sprite = glIsEnabled(GL_POINT_SPRITE);
					glEnable(GL_POINT_SPRITE);
				}
			}
			glBindVertexArray(vao);
			glDrawArrays(prim, 0, count);
			glBindVertexArray(0);
			if (gpu_imm.shader == GPU_BUILTIN_POINT) {
				glDisable(GL_PROGRAM_POINT_SIZE);
				if (!GPU_caps()->core_profile && !sprite) {
					glDisable(GL_POINT_SPRITE);
				}
			}
			glUseProgram((GLuint)prev_program);
		}
	}
	if (owned) {
		MEM_freeN(verts);
	}
}

/* ------------------------------------------------------------------------ */

void gpu_layer_init(void)
{
	mat_init();
	gpu_imm.color[0] = gpu_imm.color[1] = gpu_imm.color[2] = gpu_imm.color[3] = 1.0f;
}

/* BGE-Remix P8 Part B: the speed features' switches */
static const char *gpu_speed_names[] = {"instancing", "gpu_culling", "gpu_skinning", "layered_shadows",
                                        "clustered_lights"};
#define SPEED_COUNT 5
static int gpu_speed_on[SPEED_COUNT] = {-1, -1, -1, -1, -1};

static int speed_index(const char *name)
{
	for (int i = 0; i < SPEED_COUNT; i++) {
		if (name && strcmp(name, gpu_speed_names[i]) == 0) {
			return i;
		}
	}
	return -1;
}

bool GPU_speed_feature(const char *name)
{
	const int i = speed_index(name);
	if (i < 0) {
		return false;
	}
	if (gpu_speed_on[i] < 0) {
		char env[64] = "BGE_REMIX_NO_";
		size_t n = strlen(env);
		for (const char *c = gpu_speed_names[i]; *c && n < sizeof(env) - 1; c++) {
			env[n++] = (char)((*c >= 'a' && *c <= 'z') ? *c - 32 : *c);
		}
		env[n] = '\0';
		const char *v = getenv(env);
		gpu_speed_on[i] = (v && v[0] && v[0] != '0') ? 0 : 1;
	}
	return gpu_speed_on[i] == 1;
}

bool GPU_speed_feature_set(const char *name, bool on)
{
	const int i = speed_index(name);
	if (i < 0) {
		return false;
	}
	gpu_speed_on[i] = on ? 1 : 0;
	return true;
}

/* BGE-Remix P8 Part B: GPU time of the game's passes, from timestamps. Each
 * frame has its own set of queries; a set is read four frames later (when
 * its results are there), summed per pass and averaged. */

#define PT_FRAMES 4
#define PT_QUERIES 64  /* begin and end pairs per frame */

static const char *gpu_pt_names[GPU_PASS_COUNT] = {
	"Shadows", "Opaque", "Transparent", "Sky", "Filters", "Overlays"};

static struct {
	bool init, broken;
	GLuint queries[PT_FRAMES][PT_QUERIES * 2];
	int pass[PT_FRAMES][PT_QUERIES];
	int used[PT_FRAMES];
	int open[GPU_PASS_COUNT];   /* the open pair of each pass (-1: none) */
	int frame;                  /* the set written this frame */
	int frames_done;
	float ms[GPU_PASS_COUNT];   /* averaged; -1: unknown */
} gpu_pt = {false};

static void pt_init(void)
{
	if (gpu_pt.init) {
		return;
	}
	gpu_pt.init = true;
	glGenQueries(PT_FRAMES * PT_QUERIES * 2, &gpu_pt.queries[0][0]);
	for (int p = 0; p < GPU_PASS_COUNT; p++) {
		gpu_pt.open[p] = -1;
		gpu_pt.ms[p] = -1.0f;
	}
}

void GPU_pass_timer_begin(GPUPassTimer pass)
{
	pt_init();
	const int f = gpu_pt.frame;
	if (pass < 0 || pass >= GPU_PASS_COUNT || gpu_pt.open[pass] >= 0 || gpu_pt.used[f] >= PT_QUERIES) {
		return;
	}
	const int i = gpu_pt.used[f]++;
	gpu_pt.pass[f][i] = pass;
	gpu_pt.open[pass] = i;
	glQueryCounter(gpu_pt.queries[f][i * 2], GL_TIMESTAMP);
}

void GPU_pass_timer_end(GPUPassTimer pass)
{
	if (!gpu_pt.init || pass < 0 || pass >= GPU_PASS_COUNT || gpu_pt.open[pass] < 0) {
		return;
	}
	const int i = gpu_pt.open[pass];
	gpu_pt.open[pass] = -1;
	glQueryCounter(gpu_pt.queries[gpu_pt.frame][i * 2 + 1], GL_TIMESTAMP);
}

void GPU_pass_timer_frame(void)
{
	if (!gpu_pt.init) {
		return;
	}
	/* a pass left open is closed with the frame */
	for (int p = 0; p < GPU_PASS_COUNT; p++) {
		if (gpu_pt.open[p] >= 0) {
			GPU_pass_timer_end((GPUPassTimer)p);
		}
	}
	gpu_pt.frames_done++;
	gpu_pt.frame = (gpu_pt.frame + 1) % PT_FRAMES;
	/* the set about to be written again is the oldest: read it */
	const int f = gpu_pt.frame;
	if (gpu_pt.frames_done >= PT_FRAMES) {
		double sum[GPU_PASS_COUNT] = {0.0};
		bool seen[GPU_PASS_COUNT] = {false};
		bool ok = true;
		for (int i = 0; i < gpu_pt.used[f] && ok; i++) {
			GLuint avail = 0;
			glGetQueryObjectuiv(gpu_pt.queries[f][i * 2 + 1], GL_QUERY_RESULT_AVAILABLE, &avail);
			if (!avail) {
				ok = false;  /* not there yet: skipped (not waited for) */
				break;
			}
			GLuint64 t0 = 0, t1 = 0;
			glGetQueryObjectui64v(gpu_pt.queries[f][i * 2], GL_QUERY_RESULT, &t0);
			glGetQueryObjectui64v(gpu_pt.queries[f][i * 2 + 1], GL_QUERY_RESULT, &t1);
			const int p = gpu_pt.pass[f][i];
			sum[p] += (t1 > t0) ? (double)(t1 - t0) * 1e-6 : 0.0;
			seen[p] = true;
		}
		if (ok) {
			for (int p = 0; p < GPU_PASS_COUNT; p++) {
				const float v = seen[p] ? (float)sum[p] : 0.0f;
				gpu_pt.ms[p] = (gpu_pt.ms[p] < 0.0f) ? v : gpu_pt.ms[p] * 0.9f + v * 0.1f;
			}
		}
	}
	gpu_pt.used[f] = 0;
}

float GPU_pass_timer_ms(GPUPassTimer pass)
{
	if (pass < 0 || pass >= GPU_PASS_COUNT || !gpu_pt.init) {
		return -1.0f;
	}
	return gpu_pt.ms[pass];
}

const char *GPU_pass_timer_name(GPUPassTimer pass)
{
	return (pass >= 0 && pass < GPU_PASS_COUNT) ? gpu_pt_names[pass] : "";
}

/* BGE-Remix P8 Part B: one triangle over the viewport (2D filters, motion
 * blur, stereo interlace); attribute 0 holds its corners, so a
 * compatibility context draws it too */
static struct {
	GLuint vao, vbo;
} gpu_fs = {0, 0};

void GPU_draw_fullscreen_triangle(void)
{
	if (!gpu_fs.vao) {
		const float corners[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
		glCreateBuffers(1, &gpu_fs.vbo);
		glNamedBufferStorage(gpu_fs.vbo, sizeof(corners), corners, 0);
		glCreateVertexArrays(1, &gpu_fs.vao);
		glVertexArrayVertexBuffer(gpu_fs.vao, 0, gpu_fs.vbo, 0, 2 * sizeof(float));
		glVertexArrayAttribFormat(gpu_fs.vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
		glVertexArrayAttribBinding(gpu_fs.vao, 0, 0);
		glEnableVertexArrayAttrib(gpu_fs.vao, 0);
		GPU_debug_label(GL_VERTEX_ARRAY, gpu_fs.vao, "BGE-Remix full-screen triangle");
	}
	GLint prev = 0;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev);
	glBindVertexArray(gpu_fs.vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray((GLuint)prev);
}

void gpu_layer_exit(void)
{
	if (gpu_pt.init) {
		glDeleteQueries(PT_FRAMES * PT_QUERIES * 2, &gpu_pt.queries[0][0]);
		memset(&gpu_pt, 0, sizeof(gpu_pt));
	}
	if (gpu_fs.vao) {
		glDeleteVertexArrays(1, &gpu_fs.vao);
		glDeleteBuffers(1, &gpu_fs.vbo);
		gpu_fs.vao = gpu_fs.vbo = 0;
	}
	for (int i = 0; i < IMM_VAOS; i++) {
		if (gpu_imm.vaos[i]) {
			glDeleteVertexArrays(1, &gpu_imm.vaos[i]);
			gpu_imm.vaos[i] = 0;
		}
	}
	MEM_SAFE_FREE(gpu_imm.verts);
	gpu_imm.len = gpu_imm.cap = 0;
	stream_exit();
	gpu_builtin_exit();
}

/* ------------------------------------------------------------------------ */
/* compute (BGE-Remix P8.7) */

unsigned int GPU_compute_program(const char *source, const char *label)
{
	const char *src[2] = {"#version 450 core\n", source};
	GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
	glShaderSource(sh, 2, src, NULL);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[2048];
		glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		printf("BGE-Remix compute %s: does not build:\n%s\n", label ? label : "", log);
		glDeleteShader(sh);
		return 0;
	}
	GLuint prog = glCreateProgram();
	glAttachShader(prog, sh);
	glLinkProgram(prog);
	glDeleteShader(sh);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[2048];
		glGetProgramInfoLog(prog, sizeof(log), NULL, log);
		printf("BGE-Remix compute %s: does not link:\n%s\n", label ? label : "", log);
		glDeleteProgram(prog);
		return 0;
	}
	GPU_debug_label(GL_PROGRAM, prog, label);
	return prog;
}

void GPU_compute_dispatch_2d(int width, int height, int group)
{
	if (width <= 0 || height <= 0 || group <= 0) {
		return;
	}
	glDispatchCompute((GLuint)((width + group - 1) / group), (GLuint)((height + group - 1) / group), 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
	                GL_TEXTURE_UPDATE_BARRIER_BIT | GL_PIXEL_BUFFER_BARRIER_BIT);
}
