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

/** \file blender/gpu/intern/gpu_legacy.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.26: the old OpenGL on a core context (GPU_legacy.h). This
 * file draws: immediate mode and client arrays become the streaming
 * buffer's vertices, drawn through one vertex array per context with the
 * fixed-function look (gpu_shader_legacy.glsl) or the application's program
 * (its old built-ins fed by name, GPU_old_builtins); and the GLEW functions
 * that need to know (programs, vertex arrays, buffers, shader sources).
 */

#define GPU_LEGACY_NO_REDIRECT

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER)
#  include <intrin.h>  /* _BitScanForward */
#endif

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include "MEM_guardedalloc.h"

#include "GPU_glew.h"
#include "GPU_layer.h"
#include "GPU_translate.h"

#include "gpu_legacy_intern.h"

#if defined(LGL_TEST_EGL)
#  include <EGL/egl.h>
#elif !defined(_WIN32) && !defined(__APPLE__)
#  include <GL/glx.h>
#endif

int GPU_lgl_on = 0;
LglState lgl;

/* ------------------------------------------------------------------------ */
/* notes: each said once */

#define LGL_NOTES 128
static const char *lgl_notes[LGL_NOTES];
static int lgl_nnotes = 0;

void lgl_note_once(const char *what)
{
	for (int i = 0; i < lgl_nnotes; i++) {
		if (lgl_notes[i] == what || strcmp(lgl_notes[i], what) == 0) {
			return;
		}
	}
	if (lgl_nnotes < LGL_NOTES) {
		lgl_notes[lgl_nnotes++] = what;
	}
	printf("BGE-Remix GL emulation: %s\n", what);
	fflush(stdout);
}

void lgl_flush_note(const char *what)
{
	lgl_note_once(what);
}

void lgl_dirty(void)
{
	lgl.dirty = 1;
}

/* ------------------------------------------------------------------------ */
/* per context: the vertex array everything is drawn with, and what it holds */

typedef struct LglAttrCache {
	int enabled;
	GLuint buffer;
	GLintptr offset;
	GLsizei stride;
	GLint size;
	GLenum type;
	int normalized, integer;
	GLuint divisor;
	int valid;
	float value[4];      /* the constant value last given (value_valid) */
	int value_valid;
} LglAttrCache;

typedef struct LglContext {
	void *ctx;
	GLuint vao;
	GLuint ebo;
	LglAttrCache attr[16];
	/* the viewport and point size as last set through the emulation (so
	 * drawing need not ask the driver, which may wait for its own thread) */
	int vp[4], vp_valid;
	float point_size;
	int point_size_valid;
	/* what is bound in the context, to leave out binding it again:
	 * the vertex array, the program, the fixed-function block's range */
	GLuint vao_bound, program_bound;
	int vao_known, program_known;
	GLuint ubo_buf;
	size_t ubo_off;
} LglContext;

#define LGL_CONTEXTS 16
static LglContext lgl_ctx[LGL_CONTEXTS];
static int lgl_nctx = 0;
static LglContext *lgl_cur = NULL;

static void *lgl_platform_context(void)
{
#if defined(LGL_TEST_EGL)
	return (void *)eglGetCurrentContext();
#elif defined(_WIN32)
	return (void *)wglGetCurrentContext();
#elif !defined(__APPLE__)
	return (void *)glXGetCurrentContext();
#else
	return NULL;
#endif
}

static LglContext *lgl_context(void)
{
	void *c = lgl_platform_context();
	if (lgl_cur && lgl_cur->ctx == c) {
		return lgl_cur;
	}
	for (int i = 0; i < lgl_nctx; i++) {
		if (lgl_ctx[i].ctx == c) {
			lgl_cur = &lgl_ctx[i];
			return lgl_cur;
		}
	}
	LglContext *lc = (lgl_nctx < LGL_CONTEXTS) ? &lgl_ctx[lgl_nctx++] : &lgl_ctx[LGL_CONTEXTS - 1];
	memset(lc, 0, sizeof(*lc));
	lc->ctx = c;
	glCreateVertexArrays(1, &lc->vao);
	lgl_cur = lc;
	return lc;
}

/* ------------------------------------------------------------------------ */
/* the fixed-function look: one program per sampler layout */

extern char datatoc_gpu_shader_legacy_glsl[];

typedef struct LglProg {
	int key;
	GLuint program;
	GLint loc_prim, loc_mode, loc_point, loc_vp;
	int prim, mode;
	float point[4], vp[4];
} LglProg;

#define LGL_PROGS 32
static LglProg lgl_progs[LGL_PROGS];
static int lgl_nprogs = 0;

static GLuint lgl_compile(GLenum type, const char **src, int n, const char *what)
{
	GLuint sh = glCreateShader(type);
	glShaderSource(sh, n, src, NULL);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		printf("BGE-Remix GL emulation: the %s does not build:\n%s\n", what, log);
		glDeleteShader(sh);
		return 0;
	}
	return sh;
}

static LglProg *lgl_program(int key)
{
	for (int i = 0; i < lgl_nprogs; i++) {
		if (lgl_progs[i].key == key) {
			return &lgl_progs[i];
		}
	}
	if (lgl_nprogs == LGL_PROGS) {
		return &lgl_progs[0];
	}
	char defs[256];
	snprintf(defs, sizeof(defs), "#define LGL_T0 %d\n#define LGL_T1 %d\n#define LGL_T2 %d\n#define LGL_T3 %d\n",
	         key & 3, (key >> 2) & 3, (key >> 4) & 3, (key >> 6) & 3);
	const char *vs[4] = {"#version 450 core\n", "#define VERTEX\n", defs, datatoc_gpu_shader_legacy_glsl};
	const char *fs[4] = {"#version 450 core\n", "#define FRAGMENT\n", defs, datatoc_gpu_shader_legacy_glsl};
	GLuint v = lgl_compile(GL_VERTEX_SHADER, vs, 4, "fixed-function vertex stage");
	GLuint f = lgl_compile(GL_FRAGMENT_SHADER, fs, 4, "fixed-function fragment stage");
	LglProg *lp = &lgl_progs[lgl_nprogs++];
	memset(lp, 0, sizeof(*lp));
	lp->key = key;
	lp->prim = lp->mode = -1;
	if (!v || !f) {
		return lp;
	}
	GLuint p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetProgramInfoLog(p, sizeof(log), NULL, log);
		printf("BGE-Remix GL emulation: the fixed-function look does not link:\n%s\n", log);
		glDeleteProgram(p);
		return lp;
	}
	lp->program = p;
	lp->loc_prim = glGetUniformLocation(p, "u_prim");
	lp->loc_mode = glGetUniformLocation(p, "u_mode");
	lp->loc_point = glGetUniformLocation(p, "u_point");
	lp->loc_vp = glGetUniformLocation(p, "u_vp");
	return lp;
}

/* the sampler layout the enabled units need (2 bits each: 0 2D, 1 cube, 2 rectangle) */
static int lgl_program_key(void)
{
	int key = 0;
	for (int u = 0; u < LGL_FF_UNITS; u++) {
		const int en = lgl.unit[u].en;
		int t = 0;
		if (en & U_CUBE) {
			t = 1;
		}
		else if (en & U_3D) {
			t = 0;
		}
		else if (en & U_RECT) {
			t = 2;
		}
		key |= t << (u * 2);
	}
	return key;
}

/* ------------------------------------------------------------------------ */
/* the application's programs: where their old built-ins are */

typedef struct LglUserProg {
	GLuint program;
	int valid;
	GLint loc[S_FIXED];     /* bge_pos, bge_col, bge_nor, bge_texco0..3 (-1: none) */
	int nattr;
	GLint attr_loc[16];     /* every active attribute */
	int attr_integer[16];
	GLint loc_clipplane, loc_clipplanes;
} LglUserProg;

#define LGL_USERPROGS 512
static LglUserProg *lgl_userprogs = NULL;

static void lgl_userprog_forget(GLuint program)
{
	if (lgl_userprogs) {
		LglUserProg *up = &lgl_userprogs[program & (LGL_USERPROGS - 1)];
		if (up->program == program) {
			up->valid = 0;
		}
	}
}

static LglUserProg *lgl_userprog(GLuint program)
{
	if (!lgl_userprogs) {
		lgl_userprogs = MEM_callocN(sizeof(LglUserProg) * LGL_USERPROGS, "lgl user programs");
	}
	LglUserProg *up = &lgl_userprogs[program & (LGL_USERPROGS - 1)];
	if (up->valid && up->program == program) {
		return up;
	}
	memset(up, 0, sizeof(*up));
	up->program = program;
	up->valid = 1;
	static const char *names[S_FIXED] = {"bge_pos", "bge_col", "bge_nor", "bge_texco0", "bge_texco1",
	                                     "bge_texco2", "bge_texco3", NULL};
	for (int i = 0; i < S_FIXED; i++) {
		up->loc[i] = names[i] ? glGetAttribLocation(program, names[i]) : -1;
	}
	GLint n = 0;
	glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &n);
	for (GLint i = 0; i < n && up->nattr < 16; i++) {
		char name[128];
		GLint size = 0;
		GLenum type = 0;
		glGetActiveAttrib(program, (GLuint)i, sizeof(name), NULL, &size, &type, name);
		const GLint loc = glGetAttribLocation(program, name);
		if (loc < 0 || loc >= 16) {
			continue;
		}
		up->attr_loc[up->nattr] = loc;
		up->attr_integer[up->nattr] = (type == GL_INT || type == GL_UNSIGNED_INT || type == GL_INT_VEC2 ||
		                               type == GL_INT_VEC3 || type == GL_INT_VEC4 ||
		                               type == GL_UNSIGNED_INT_VEC2 || type == GL_UNSIGNED_INT_VEC3 ||
		                               type == GL_UNSIGNED_INT_VEC4);
		up->nattr++;
	}
	/* an old program (a compatibility context only): gl_Vertex is attribute 0 */
	if (up->loc[S_POS] < 0 && up->nattr == 0) {
		up->loc[S_POS] = 0;
	}
	up->loc_clipplane = glGetUniformLocation(program, "bge_clipplane");
	up->loc_clipplanes = glGetUniformLocation(program, "bge_clipplanes[0]");
	return up;
}

/* ------------------------------------------------------------------------ */
/* drawing */

typedef struct LglSrc {
	int on;              /* an array (else the value) */
	GLuint buffer;
	GLintptr offset;
	GLsizei stride;
	GLint size;
	GLenum type;
	int normalized, integer;
	GLuint divisor;
	float value[4];
} LglSrc;

static int lgl_type_size(GLenum type)
{
	switch (type) {
		case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
		case GL_SHORT: case GL_UNSIGNED_SHORT: case GL_HALF_FLOAT: return 2;
		case GL_DOUBLE: return 8;
		default: return 4;
	}
}

/* an attribute location as "s" has it: an array (its binding's buffer
 * noted in the cache and its bit set in "changed", the bindings given
 * together afterwards) or a constant value */
static void lgl_attr_set(LglContext *lc, int loc, const LglSrc *s, unsigned int *changed)
{
	LglAttrCache *c = &lc->attr[loc];
	if (s && s->on) {
		if (!c->valid || !c->enabled) {
			glEnableVertexArrayAttrib(lc->vao, (GLuint)loc);
			c->enabled = 1;
			/* the current value is not to be relied on after drawing from an array */
			c->value_valid = 0;
		}
		if (!c->valid || c->size != s->size || c->type != s->type || c->normalized != s->normalized ||
		    c->integer != s->integer)
		{
			if (s->integer) {
				glVertexArrayAttribIFormat(lc->vao, (GLuint)loc, s->size, s->type, 0);
			}
			else {
				glVertexArrayAttribFormat(lc->vao, (GLuint)loc, s->size, s->type,
				                          s->normalized ? GL_TRUE : GL_FALSE, 0);
			}
			glVertexArrayAttribBinding(lc->vao, (GLuint)loc, (GLuint)loc);
			c->size = s->size;
			c->type = s->type;
			c->normalized = s->normalized;
			c->integer = s->integer;
		}
		if (!c->valid || c->buffer != s->buffer || c->offset != s->offset || c->stride != s->stride) {
			c->buffer = s->buffer;
			c->offset = s->offset;
			c->stride = s->stride;
			*changed |= (1u << loc);
		}
		if (!c->valid || c->divisor != s->divisor) {
			glVertexArrayBindingDivisor(lc->vao, (GLuint)loc, s->divisor);
			c->divisor = s->divisor;
		}
		c->valid = 1;
	}
	else {
		if (!c->valid || c->enabled) {
			glDisableVertexArrayAttrib(lc->vao, (GLuint)loc);
			c->enabled = 0;
			c->valid = 1;
		}
		if (s && (!c->value_valid || memcmp(c->value, s->value, sizeof(float[4])) != 0)) {
			glVertexAttrib4fv((GLuint)loc, s->value);
			memcpy(c->value, s->value, sizeof(float[4]));
			c->value_valid = 1;
		}
	}
}

/* the buffers of the bindings that changed, a call for each run of
 * neighbouring ones (a binding left as it was may name a buffer deleted since) */
static void lgl_attr_bind(LglContext *lc, unsigned int changed)
{
	int loc = 0;
	while (changed >> loc) {
		if (!((changed >> loc) & 1u)) {
			loc++;
			continue;
		}
		GLuint bufs[16];
		GLintptr offs[16];
		GLsizei strides[16];
		const int first = loc;
		int n = 0;
		for (; loc < 16 && ((changed >> loc) & 1u); loc++, n++) {
			const LglAttrCache *c = &lc->attr[loc];
			bufs[n] = c->buffer;
			offs[n] = c->offset;
			strides[n] = c->stride;
		}
		if (n == 1) {
			glVertexArrayVertexBuffer(lc->vao, (GLuint)first, bufs[0], offs[0], strides[0]);
		}
		else {
			glVertexArrayVertexBuffers(lc->vao, (GLuint)first, n, bufs, offs, strides);
		}
	}
}

/* glVertexAttrib outside glBegin: the cache knows the value */
static void lgl_attr_value_changed(unsigned int index, const float v[4])
{
	LglContext *lc = lgl_context();
	if (index < 16) {
		memcpy(lc->attr[index].value, v, sizeof(float[4]));
		lc->attr[index].value_valid = 1;
	}
}

static void lgl_mat_eq(float a[16], const float b[4][4], int *changed)
{
	if (memcmp(a, b, sizeof(float[16])) != 0) {
		memcpy(a, b, sizeof(float[16]));
		*changed = 1;
	}
}

/* the block of the fixed-function look: written anew to the streaming
 * buffer where it changed (the card may still read the one before, so it is
 * not written over), then bound where it is */
static void lgl_update_block(LglContext *lc)
{
	float mv[4][4], proj[4][4], tm[4][4];
	int changed = lgl.dirty || !lgl.block_valid;
	GPU_matrix_get(GPU_MATRIX_MODELVIEW, mv);
	GPU_matrix_get(GPU_MATRIX_PROJECTION, proj);
	lgl_mat_eq(lgl.last_mv, mv, &changed);
	lgl_mat_eq(lgl.last_proj, proj, &changed);
	for (int u = 0; u < LGL_FF_UNITS; u++) {
		GPU_texture_matrix_get(u, tm);
		lgl_mat_eq(lgl.last_texmat[u], tm, &changed);
	}
	int write = !lgl.block_buf || lgl.block_gen != GPU_stream_generation();
	if (changed) {
		LglBlock b;
		lgl_build_block(&b);
		if (!lgl.block_valid || memcmp(&b, &lgl.block, sizeof(b)) != 0) {
			memcpy(&lgl.block, &b, sizeof(b));
			write = 1;
		}
		lgl.block_valid = 1;
		lgl.dirty = 0;
	}
	if (write) {
		GLuint buf = 0;
		size_t off = 0;
		void *dst = GPU_stream_alloc(sizeof(LglBlock), &buf, &off);
		if (!dst) {
			return;
		}
		memcpy(dst, &lgl.block, sizeof(LglBlock));
		GPU_stream_written(buf);
		lgl.block_buf = buf;
		lgl.block_off = off;
		lgl.block_gen = GPU_stream_generation();
		lgl.stat_blocks += 1.0;
	}
	if (lc->ubo_buf != lgl.block_buf || lc->ubo_off != lgl.block_off) {
		glBindBufferRange(GL_UNIFORM_BUFFER, 15, lgl.block_buf, (GLintptr)lgl.block_off, sizeof(LglBlock));
		lc->ubo_buf = lgl.block_buf;
		lc->ubo_off = lgl.block_off;
	}
}

/* the vertex array and program, bound unless they are */
static void lgl_bind_vao(LglContext *lc, GLuint vao)
{
	if (!lc->vao_known || lc->vao_bound != vao) {
		glBindVertexArray(vao);
		lc->vao_bound = vao;
		lc->vao_known = 1;
	}
}

static void lgl_use_program(LglContext *lc, GLuint program)
{
	if (!lc->program_known || lc->program_bound != program) {
		glUseProgram(program);
		lc->program_bound = program;
		lc->program_known = 1;
	}
}

/* the extra clip planes for the application's programs (translated ones
 * write gl_ClipDistance from them) */
static void lgl_user_clip(LglUserProg *up)
{
	if (up->loc_clipplane < 0 && up->loc_clipplanes < 0) {
		return;
	}
	/* planes that are off keep everything (a driver may clip by a distance
	 * written for a plane that is off) */
	float planes[LGL_CLIPS][4];
	for (int i = 0; i < LGL_CLIPS; i++) {
		const int on = (lgl.clip_on >> i) & 1;
		for (int k = 0; k < 4; k++) {
			planes[i][k] = on ? (float)lgl.clip[i][k] : (k == 3 ? 1.0f : 0.0f);
		}
	}
	if (up->loc_clipplane >= 0 && (lgl.clip_on & 1)) {
		/* (otherwise the mirror's plane GPU_old_builtins gave stays) */
		glProgramUniform4fv(up->program, up->loc_clipplane, 1, planes[0]);
	}
	if (up->loc_clipplanes >= 0) {
		glProgramUniform4fv(up->program, up->loc_clipplanes, LGL_CLIPS, planes[0]);
	}
}

/* one draw: prim is what OpenGL 4.5 draws; with itype the indices are in
 * ibuf at ioffset (basevertex added); prim_class: 0 points, 1 lines,
 * 2 polygons, 3 polygon outlines or corners */
static void lgl_submit(GLenum prim, GLint first, GLsizei count, GLenum itype, GLuint ibuf, GLintptr ioffset,
                       GLint basevertex, GLsizei instances, const LglSrc fixed[S_FIXED],
                       const LglSrc gen[LGL_GENERIC], int prim_class, int mode)
{
	if (count <= 0 || instances <= 0) {
		return;
	}
	LglContext *lc = lgl_context();
	lgl_bind_vao(lc, lc->vao);
	lgl.stat_draws += 1.0;

	const LglSrc *at[16];
	memset(at, 0, sizeof(at));
	GLuint program;

	if (lgl.app_program) {
		program = lgl.app_program;
		LglUserProg *up = lgl_userprog(program);
		for (int i = 0; i < S_FIXED; i++) {
			const GLint loc = up->loc[i];
			if (loc >= 0 && loc < 16) {
				at[loc] = &fixed[i];
			}
		}
		for (int g = 0; g < LGL_GENERIC; g++) {
			if (gen[g].on && !at[g]) {
				at[g] = &gen[g];
			}
		}
		/* every other attribute the program reads: its current value */
		static LglSrc gconst[16];
		for (int a = 0; a < up->nattr; a++) {
			const GLint loc = up->attr_loc[a];
			if (!at[loc]) {
				memset(&gconst[loc], 0, sizeof(LglSrc));
				memcpy(gconst[loc].value, lgl.generic[loc], sizeof(float[4]));
				at[loc] = &gconst[loc];
			}
		}
		GPUClassicState st;
		lgl_classic_state(&st);
		GPU_old_builtins(program, &st);
		lgl_user_clip(up);
		lgl_use_program(lc, program);
	}
	else {
		LglProg *lp = lgl_program(lgl_program_key());
		program = lp->program;
		if (!program) {
			return;
		}
		lgl_update_block(lc);
		lgl_use_program(lc, program);
		if (lp->prim != prim_class) {
			glProgramUniform1i(program, lp->loc_prim, prim_class);
			lp->prim = prim_class;
		}
		if (lp->mode != mode) {
			glProgramUniform1i(program, lp->loc_mode, mode);
			lp->mode = mode;
		}
		if (prim_class == 0) {
			/* points: where the old rules put their centres */
			float point[4] = {1.0f, 0.0f, 0.0f, 0.0f}, vp[4];
			int ivp[4];
			point[0] = lgl_point_size();
			point[1] = (lgl.en & E_POINT_SMOOTH) ? 0.0f : 1.0f;
			lgl_viewport(ivp);
			for (int k = 0; k < 4; k++) {
				vp[k] = (float)ivp[k];
			}
			if (memcmp(point, lp->point, sizeof(point)) != 0) {
				glProgramUniform4fv(program, lp->loc_point, 1, point);
				memcpy(lp->point, point, sizeof(point));
			}
			if (memcmp(vp, lp->vp, sizeof(vp)) != 0) {
				glProgramUniform4fv(program, lp->loc_vp, 1, vp);
				memcpy(lp->vp, vp, sizeof(vp));
			}
		}
		for (int i = 0; i < S_FIXED; i++) {
			at[i] = &fixed[i];
		}
	}

	unsigned int changed = 0;
	for (int loc = 0; loc < 16; loc++) {
		lgl_attr_set(lc, loc, at[loc], &changed);
	}
	lgl_attr_bind(lc, changed);

	if (itype) {
		if (lc->ebo != ibuf) {
			glVertexArrayElementBuffer(lc->vao, ibuf);
			lc->ebo = ibuf;
		}
		if (instances > 1 || basevertex) {
			glDrawElementsInstancedBaseVertex(prim, count, itype, (const void *)ioffset, instances, basevertex);
		}
		else {
			glDrawElements(prim, count, itype, (const void *)ioffset);
		}
		if (ibuf != lgl.app_ebo) {
			/* the application's element buffer back in its place */
			glVertexArrayElementBuffer(lc->vao, lgl.app_ebo);
			lc->ebo = lgl.app_ebo;
		}
	}
	else {
		if (instances > 1) {
			glDrawArraysInstanced(prim, first, count, instances);
		}
		else {
			glDrawArrays(prim, first, count);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* primitive conversion: what OpenGL 4.5 no longer draws */

static unsigned int *lgl_idx = NULL;
static int lgl_idx_cap = 0;

static unsigned int *lgl_idx_reserve(int n)
{
	if (n > lgl_idx_cap) {
		lgl_idx_cap = n + n / 2 + 64;
		lgl_idx = MEM_reallocN_id(lgl_idx, sizeof(unsigned int) * (size_t)lgl_idx_cap, "lgl indices");
	}
	return lgl_idx;
}

static int lgl_prim_class(GLenum prim)
{
	switch (prim) {
		case GL_POINTS: return 0;
		case GL_LINES: case GL_LINE_STRIP: case GL_LINE_LOOP: return 1;
		default: return 2;
	}
}

static int lgl_needs_conversion(GLenum prim)
{
	return prim == GL_QUADS || prim == GL_QUAD_STRIP || prim == GL_POLYGON;
}

/* quads, quad strips and polygons as triangles (or outlines, or corners,
 * with the polygon mode); "src" maps the n vertices to their indices (NULL:
 * 0..n-1). Returns the count in lgl_idx and the primitive. */
static int lgl_convert(GLenum prim, const unsigned int *src, int n, GLenum *r_prim, int *r_class)
{
	const GLenum pm = lgl.poly_mode[0];
	const int flat = (lgl.shade_model == GL_FLAT);
#define V(i) (src ? src[i] : (unsigned int)(i))
	int out = 0;
	if (pm == GL_POINT) {
		unsigned int *d = lgl_idx_reserve(n);
		for (int i = 0; i < n; i++) {
			d[out++] = V(i);
		}
		*r_prim = GL_POINTS;
		*r_class = 3;
		return out;
	}
	if (pm == GL_LINE) {
		unsigned int *d = lgl_idx_reserve(n * 2 + 8);
		if (prim == GL_QUADS) {
			for (int q = 0; q + 3 < n; q += 4) {
				for (int k = 0; k < 4; k++) {
					d[out++] = V(q + k);
					d[out++] = V(q + (k + 1) % 4);
				}
			}
		}
		else if (prim == GL_QUAD_STRIP) {
			for (int i = 0; i + 3 < n; i += 2) {
				const unsigned int a = V(i), b = V(i + 1), c = V(i + 3), e = V(i + 2);
				d[out++] = a; d[out++] = b;
				d[out++] = b; d[out++] = c;
				d[out++] = c; d[out++] = e;
				d[out++] = e; d[out++] = a;
			}
		}
		else if (n >= 3) {
			for (int i = 0; i < n; i++) {
				d[out++] = V(i);
				d[out++] = V((i + 1) % n);
			}
		}
		*r_prim = GL_LINES;
		*r_class = 3;
		return out;
	}
	unsigned int *d = lgl_idx_reserve(n * 3 + 8);
	if (prim == GL_QUADS) {
		for (int q = 0; q + 3 < n; q += 4) {
			const unsigned int a = V(q), b = V(q + 1), c = V(q + 2), e = V(q + 3);
			if (flat) {
				/* the quad's last vertex leads both (as the old quads had it) */
				d[out++] = a; d[out++] = b; d[out++] = e;
				d[out++] = b; d[out++] = c; d[out++] = e;
			}
			else {
				d[out++] = a; d[out++] = b; d[out++] = c;
				d[out++] = a; d[out++] = c; d[out++] = e;
			}
		}
	}
	else if (prim == GL_QUAD_STRIP) {
		for (int i = 0; i + 3 < n; i += 2) {
			const unsigned int a = V(i), b = V(i + 1), c = V(i + 3), e = V(i + 2);
			d[out++] = a; d[out++] = b; d[out++] = c;
			d[out++] = e; d[out++] = a; d[out++] = c;
		}
	}
	else {
		/* a polygon: its first vertex leads (last in each triangle) */
		for (int i = 1; i + 1 < n; i++) {
			if (flat) {
				d[out++] = V(i); d[out++] = V(i + 1); d[out++] = V(0);
			}
			else {
				d[out++] = V(0); d[out++] = V(i); d[out++] = V(i + 1);
			}
		}
	}
#undef V
	*r_prim = GL_TRIANGLES;
	*r_class = 2;
	return out;
}

static GLuint lgl_upload(const void *data, size_t size, GLintptr *r_offset)
{
	GLuint buf = 0;
	size_t off = 0;
	void *dst = GPU_stream_alloc(size, &buf, &off);
	if (dst) {
		memcpy(dst, data, size);
	}
	lgl.stat_bytes += (double)size;
	GPU_stream_written(buf);
	*r_offset = (GLintptr)off;
	return buf;
}

/* ------------------------------------------------------------------------ */
/* immediate mode */

/* a large block is drawn in parts of this many vertices as it is given
 * (divisible by 1, 2, 3 and 4): the card starts on it while the rest comes,
 * and the vertices waiting stay in the processor's cache */
#define LGL_FLUSH 65520

static void lgl_draw_immediate(void);
static void lgl_flush_part(void);

/* the lowest bit set */
static int lgl_lowbit(unsigned int m)
{
#if defined(_MSC_VER)
	unsigned long i;
	_BitScanForward(&i, m);
	return (int)i;
#else
	return __builtin_ctz(m);
#endif
}

static void lgl_reserve(int n)
{
	if (n <= lgl.cap) {
		return;
	}
	int cap = lgl.cap ? lgl.cap : 256;
	while (cap < n) {
		cap *= 2;
	}
	lgl.verts = MEM_reallocN_id(lgl.verts, sizeof(float[4]) * (size_t)cap, "lgl vertices");
	for (int s = 0; s < X_SLOTS; s++) {
		if (lgl.xs[s]) {
			lgl.xs[s] = MEM_reallocN_id(lgl.xs[s], sizeof(float[4]) * (size_t)cap, "lgl slot");
		}
	}
	if (lgl.dist) {
		lgl.dist = MEM_reallocN_id(lgl.dist, sizeof(float) * (size_t)cap, "lgl distances");
	}
	lgl.cap = cap;
}

/* the current value a slot takes from */
static void lgl_slot_value(int s, float r[4])
{
	if (s == X_COL) {
		memcpy(r, lgl.color, sizeof(float[4]));
	}
	else if (s == X_NOR) {
		memcpy(r, lgl.normal, sizeof(float[3]));
		r[3] = 0.0f;
	}
	else if (s < X_GEN0) {
		memcpy(r, lgl.tex[s - X_TEX0], sizeof(float[4]));
	}
	else {
		memcpy(r, lgl.generic[s - X_GEN0], sizeof(float[4]));
	}
}

/* a slot's value about to change within the block: from now on the slot is
 * sent per vertex, the vertices before keep the value they had. Before the
 * block's first vertex nothing changes: the new value is the block's. */
static void lgl_slot_use(int slot)
{
	if (!lgl.in_begin || lgl.nverts == 0 || (lgl.xused & (1u << slot))) {
		return;
	}
	if (!lgl.xs[slot]) {
		lgl_reserve(256);
		lgl.xs[slot] = MEM_mallocN(sizeof(float[4]) * (size_t)lgl.cap, "lgl slot");
	}
	float before[4];
	lgl_slot_value(slot, before);
	float *x = lgl.xs[slot];
	for (int i = 0; i < lgl.nverts; i++) {
		memcpy(&x[i * 4], before, sizeof(float[4]));
	}
	if (slot == X_COL) {
		lgl.col_ub = lgl.color_ub;
	}
	lgl.xused |= (1u << slot);
}

void lgl_vertex4f(float x, float y, float z, float w)
{
	const float f[4] = {x, y, z, w};
	LGL_RECORD(LOP_VERTEX, NULL, 0, f, 4);
	if (!lgl.in_begin) {
		return;
	}
	if (lgl.nverts >= lgl.cap) {
		lgl_reserve(lgl.nverts + 1);
	}
	const int i = lgl.nverts;
	float *v = &lgl.verts[i * 4];
	v[0] = x; v[1] = y; v[2] = z; v[3] = w;
	if (w != 1.0f) {
		lgl.pos_w = 1;
	}
	unsigned int m = lgl.xused;
	if (m & (1u << X_COL)) {
		/* the usual one (a colour per vertex) without the loop */
		memcpy(&lgl.xs[X_COL][i * 4], lgl.color, sizeof(float[4]));
		m &= ~(1u << X_COL);
	}
	while (m) {
		const int s = lgl_lowbit(m);
		m &= m - 1;
		lgl_slot_value(s, &lgl.xs[s][i * 4]);
	}
	lgl.nverts++;
	if (lgl.nverts >= LGL_FLUSH && lgl.flush_overlap >= 0) {
		lgl_flush_part();
	}
}

static void lgl_color_set(const float c[4], int ub)
{
	if (lgl.in_begin) {
		lgl_slot_use(X_COL);
		if (lgl.xused & (1u << X_COL)) {
			lgl.col_ub &= ub;
		}
	}
	memcpy(lgl.color, c, sizeof(float[4]));
	lgl.color_ub = ub;
	/* colour material follows the colour */
	lgl_color_changed();
}

void lgl_color4f(float r, float g, float b, float a)
{
	const float f[4] = {r, g, b, a};
	LGL_RECORD(LOP_COLOR, NULL, 0, f, 4);
	lgl_color_set(f, 0);
}

void lgl_color4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	const float f[4] = {(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, (float)a / 255.0f};
	const unsigned int ub = 1;
	LGL_RECORD(LOP_COLOR, &ub, 1, f, 4);
	lgl_color_set(f, 1);
}

void lgl_normal3f(float x, float y, float z)
{
	const float f[3] = {x, y, z};
	LGL_RECORD(LOP_NORMAL, NULL, 0, f, 3);
	lgl_slot_use(X_NOR);
	lgl.normal[0] = x;
	lgl.normal[1] = y;
	lgl.normal[2] = z;
}

void lgl_texcoord4f(int unit, float s, float t, float r, float q)
{
	if (unit < 0 || unit >= LGL_UNITS) {
		return;
	}
	const unsigned int e = (unsigned int)unit;
	const float f[4] = {s, t, r, q};
	LGL_RECORD(LOP_TEXCOORD, &e, 1, f, 4);
	if (unit < LGL_FF_UNITS) {
		lgl_slot_use(X_TEX0 + unit);
	}
	lgl.tex[unit][0] = s;
	lgl.tex[unit][1] = t;
	lgl.tex[unit][2] = r;
	lgl.tex[unit][3] = q;
}

void lgl_attrib4f(unsigned int index, float x, float y, float z, float w)
{
	if (index >= LGL_GENERIC) {
		return;
	}
	if (index == 0 && lgl.in_begin) {
		/* attribute 0 is the position */
		lgl_vertex4f(x, y, z, w);
		return;
	}
	const float f[4] = {x, y, z, w};
	LGL_RECORD(LOP_ATTRIB, &index, 1, f, 4);
	lgl_slot_use(X_GEN0 + (int)index);
	lgl.generic[index][0] = x;
	lgl.generic[index][1] = y;
	lgl.generic[index][2] = z;
	lgl.generic[index][3] = w;
	if (!lgl.in_begin) {
		glVertexAttrib4f(index, x, y, z, w);
		lgl_attr_value_changed(index, f);
	}
}

/* the window position of a vertex, for line stipples */
static void lgl_window_xy(const float mvp[16], const int vp[4], const float *p, float r[2])
{
	float c[4];
	for (int k = 0; k < 4; k++) {
		c[k] = mvp[k] * p[0] + mvp[4 + k] * p[1] + mvp[8 + k] * p[2] + mvp[12 + k] * p[3];
	}
	const float w = (c[3] != 0.0f) ? c[3] : 1.0f;
	r[0] = vp[0] + (c[0] / w * 0.5f + 0.5f) * vp[2];
	r[1] = vp[1] + (c[1] / w * 0.5f + 0.5f) * vp[3];
}

static void lgl_stipple_distances(GLenum prim, int n)
{
	float mvp[16];
	int vp[4];
	lgl_current_mvp(mvp);
	lgl_viewport(vp);
	if (!lgl.dist) {
		lgl_reserve(256);
		lgl.dist = MEM_mallocN(sizeof(float) * (size_t)lgl.cap, "lgl distances");
	}
	/* a strip's later part goes on from where the one before ended (its
	 * first vertex is the one before's last) */
	float prev[2] = {0, 0}, dist = (prim == GL_LINE_STRIP) ? lgl.dist_start : 0.0f;
	for (int i = 0; i < n; i++) {
		float xy[2];
		lgl_window_xy(mvp, vp, &lgl.verts[i * 4], xy);
		if (prim == GL_LINES && (i & 1) == 0) {
			dist = 0.0f;
		}
		else if (i > 0) {
			dist += fmaxf(fabsf(xy[0] - prev[0]), fabsf(xy[1] - prev[1]));
		}
		lgl.dist[i] = dist;
		prev[0] = xy[0];
		prev[1] = xy[1];
	}
	lgl.dist_start = dist;
}

/* the block's vertices replaced by the ones "idx" names (an outline's edges
 * each with its own two vertices) */
static void lgl_expand(const unsigned int *idx, int count)
{
	float *old = MEM_mallocN(sizeof(float[4]) * (size_t)lgl.nverts, "lgl expand");
	memcpy(old, lgl.verts, sizeof(float[4]) * (size_t)lgl.nverts);
	float *oldx[X_SLOTS];
	for (int s = 0; s < X_SLOTS; s++) {
		oldx[s] = NULL;
		if (lgl.xused & (1u << s)) {
			oldx[s] = MEM_mallocN(sizeof(float[4]) * (size_t)lgl.nverts, "lgl expand");
			memcpy(oldx[s], lgl.xs[s], sizeof(float[4]) * (size_t)lgl.nverts);
		}
	}
	lgl_reserve(count);
	for (int i = 0; i < count; i++) {
		memcpy(&lgl.verts[i * 4], &old[idx[i] * 4], sizeof(float[4]));
		for (int s = 0; s < X_SLOTS; s++) {
			if (oldx[s]) {
				memcpy(&lgl.xs[s][i * 4], &oldx[s][idx[i] * 4], sizeof(float[4]));
			}
		}
	}
	lgl.nverts = count;
	MEM_freeN(old);
	for (int s = 0; s < X_SLOTS; s++) {
		if (oldx[s]) {
			MEM_freeN(oldx[s]);
		}
	}
}

/* how a block is sent: the position with 3 or 4 values, the slots that
 * changed within it (colours as bytes when they all came as bytes), the
 * stipple distances */
typedef struct LglPack {
	int pos_size;
	unsigned int slots;
	int col_ub;
	int dist;
	size_t bpv;  /* bytes per vertex */
} LglPack;

static size_t lgl_slot_bytes(const LglPack *pk, int s)
{
	if (s == X_COL && pk->col_ub) {
		return 4;
	}
	return (s == X_NOR) ? sizeof(float[3]) : sizeof(float[4]);
}

/* vertices start..start+n-1 of the block, drawn as "prim" */
/* a quad, quad strip or polygon block up to this many vertices is drawn as
 * the triangles (or outline lines) it makes, gathered vertex by vertex,
 * instead of through an index buffer (saving the index upload and the
 * element buffer's binding and rebinding: the interface draws thousands) */
#define LGL_GATHER 64

static void lgl_emit(GLenum prim, int start, int n, const LglPack *pk, int prim_class)
{
	const unsigned int *gidx = NULL;
	GLenum gprim = prim;
	int gcls = prim_class;
	if (lgl_needs_conversion(prim) && n <= LGL_GATHER) {
		const int c = lgl_convert(prim, NULL, n, &gprim, &gcls);
		if (c <= 0) {
			return;
		}
		gidx = lgl_idx;
		lgl.stat_verts += (double)n;
		n = c;  /* the gathered vertices from here on */
	}
	else {
		lgl.stat_verts += (double)n;
	}
#define SRC(k) (start + (gidx ? (int)gidx[k] : (k)))
	const size_t size = pk->bpv * (size_t)n;
	GLuint buf = 0;
	size_t off = 0;
	unsigned char *dst = GPU_stream_alloc(size, &buf, &off);
	if (!dst) {
		return;
	}
	size_t at = 0;
	size_t offs[X_SLOTS], pos_off = 0, dist_off = 0;
	/* the positions */
	if (pk->pos_size == 3) {
		float *d = (float *)dst;
		for (int i = 0; i < n; i++, d += 3) {
			const float *v = &lgl.verts[SRC(i) * 4];
			d[0] = v[0];
			d[1] = v[1];
			d[2] = v[2];
		}
		at = sizeof(float[3]) * (size_t)n;
	}
	else {
		if (gidx) {
			float *d = (float *)dst;
			for (int i = 0; i < n; i++) {
				memcpy(&d[i * 4], &lgl.verts[SRC(i) * 4], sizeof(float[4]));
			}
		}
		else {
			memcpy(dst, &lgl.verts[start * 4], sizeof(float[4]) * (size_t)n);
		}
		at = sizeof(float[4]) * (size_t)n;
	}
	/* the slots */
	unsigned int m = pk->slots;
	while (m) {
		const int s = lgl_lowbit(m);
		m &= m - 1;
		const float *xs = lgl.xs[s];
		offs[s] = at;
		if (s == X_COL && pk->col_ub) {
			unsigned char *d = dst + at;
			for (int i = 0; i < n; i++, d += 4) {
				const float *x = &xs[SRC(i) * 4];
				for (int k = 0; k < 4; k++) {
					d[k] = (unsigned char)(x[k] * 255.0f + 0.5f);
				}
			}
		}
		else if (s == X_NOR) {
			float *d = (float *)(dst + at);
			for (int i = 0; i < n; i++, d += 3) {
				const float *x = &xs[SRC(i) * 4];
				d[0] = x[0];
				d[1] = x[1];
				d[2] = x[2];
			}
		}
		else if (gidx) {
			float *d = (float *)(dst + at);
			for (int i = 0; i < n; i++) {
				memcpy(&d[i * 4], &xs[SRC(i) * 4], sizeof(float[4]));
			}
		}
		else {
			memcpy(dst + at, &xs[start * 4], sizeof(float[4]) * (size_t)n);
		}
		at += lgl_slot_bytes(pk, s) * (size_t)n;
	}
	/* the distances */
	if (pk->dist) {
		dist_off = at;
		float *d = (float *)(dst + at);
		for (int i = 0; i < n; i++) {
			d[i] = lgl.dist[SRC(i)];
		}
		at += sizeof(float) * (size_t)n;
	}
	GPU_stream_written(buf);
	lgl.stat_bytes += (double)size;

	LglSrc fixed[S_FIXED], gen[LGL_GENERIC];
	memset(fixed, 0, sizeof(fixed));
	memset(gen, 0, sizeof(gen));
#define ARRAY(src, o, sz, tp, norm, str) { \
		(src).on = 1; (src).buffer = buf; (src).offset = (GLintptr)(off + (o)); (src).size = (sz); \
		(src).type = (tp); (src).normalized = (norm); (src).stride = (GLsizei)(str); } (void)0
	ARRAY(fixed[S_POS], pos_off, pk->pos_size, GL_FLOAT, 0, sizeof(float) * (size_t)pk->pos_size);
	if (pk->slots & (1u << X_COL)) {
		if (pk->col_ub) {
			ARRAY(fixed[S_COL], offs[X_COL], 4, GL_UNSIGNED_BYTE, 1, 4);
		}
		else {
			ARRAY(fixed[S_COL], offs[X_COL], 4, GL_FLOAT, 0, sizeof(float[4]));
		}
	}
	else {
		memcpy(fixed[S_COL].value, lgl.color, sizeof(float[4]));
	}
	if (pk->slots & (1u << X_NOR)) {
		ARRAY(fixed[S_NOR], offs[X_NOR], 3, GL_FLOAT, 0, sizeof(float[3]));
	}
	else {
		memcpy(fixed[S_NOR].value, lgl.normal, sizeof(float[3]));
	}
	for (int u = 0; u < LGL_FF_UNITS; u++) {
		const int s = X_TEX0 + u;
		if (pk->slots & (1u << s)) {
			ARRAY(fixed[S_TEX0 + u], offs[s], 4, GL_FLOAT, 0, sizeof(float[4]));
		}
		else {
			memcpy(fixed[S_TEX0 + u].value, lgl.tex[u], sizeof(float[4]));
		}
	}
	if (pk->dist) {
		ARRAY(fixed[S_DIST], dist_off, 1, GL_FLOAT, 0, sizeof(float));
	}
	for (int g = 0; g < LGL_GENERIC; g++) {
		const int s = X_GEN0 + g;
		if (pk->slots & (1u << s)) {
			ARRAY(gen[g], offs[s], 4, GL_FLOAT, 0, sizeof(float[4]));
		}
	}
#undef ARRAY

#undef SRC
	if (gidx) {
		lgl_submit(gprim, 0, n, 0, 0, 0, 0, 1, fixed, gen, gcls, 0);
	}
	else if (lgl_needs_conversion(prim)) {
		GLenum p2;
		int cls;
		const int count = lgl_convert(prim, NULL, n, &p2, &cls);
		if (count > 0) {
			GLintptr ioff;
			const GLuint ib = lgl_upload(lgl_idx, sizeof(unsigned int) * (size_t)count, &ioff);
			lgl_submit(p2, 0, count, GL_UNSIGNED_INT, ib, ioff, 0, 1, fixed, gen, cls, 0);
		}
	}
	else {
		lgl_submit(prim, 0, n, 0, 0, 0, 0, 1, fixed, gen, prim_class, 0);
	}
}

static void lgl_draw_immediate(void)
{
	GLenum prim = lgl.prim;
	if (lgl.nverts <= 0) {
		return;
	}
	if (lgl_needs_conversion(prim) && lgl.poly_mode[0] == GL_LINE && (lgl.en & E_LINE_STIPPLE)) {
		/* a stippled outline: its edges as lines of their own */
		GLenum p2;
		int cls;
		const int count = lgl_convert(prim, NULL, lgl.nverts, &p2, &cls);
		unsigned int *idx = MEM_mallocN(sizeof(unsigned int) * (size_t)(count ? count : 1), "lgl edges");
		memcpy(idx, lgl_idx, sizeof(unsigned int) * (size_t)count);
		lgl_expand(idx, count);
		MEM_freeN(idx);
		prim = GL_LINES;
	}
	int n = lgl.nverts;
	const int prim_class = lgl_prim_class(prim);
	const int stipple = (prim_class == 1) && (lgl.en & E_LINE_STIPPLE);
	if (stipple) {
		if (prim == GL_LINE_LOOP && n >= 2) {
			/* a loop as a strip back to its start (the stipple runs on) */
			lgl_reserve(n + 1);
			memcpy(&lgl.verts[n * 4], &lgl.verts[0], sizeof(float[4]));
			for (int s = 0; s < X_SLOTS; s++) {
				if (lgl.xused & (1u << s)) {
					memcpy(&lgl.xs[s][n * 4], &lgl.xs[s][0], sizeof(float[4]));
				}
			}
			n++;
			prim = GL_LINE_STRIP;
		}
		lgl_stipple_distances(prim, n);
	}

	LglPack pk;
	pk.pos_size = lgl.pos_w ? 4 : 3;
	pk.slots = lgl.xused;
	pk.col_ub = (lgl.xused & (1u << X_COL)) ? lgl.col_ub : 0;
	pk.dist = stipple;
	pk.bpv = sizeof(float) * (size_t)pk.pos_size + (stipple ? sizeof(float) : 0);
	for (int s = 0; s < X_SLOTS; s++) {
		if (pk.slots & (1u << s)) {
			pk.bpv += lgl_slot_bytes(&pk, s);
		}
	}

	/* a block too large for the streaming buffer's parts, drawn in pieces
	 * where the primitive allows (strips overlap by a vertex or two, and
	 * start on an even vertex so their triangles keep their facing) */
	int unit = 0, overlap = 0;
	switch (prim) {
		case GL_POINTS: unit = 1; break;
		case GL_LINES: unit = 2; break;
		case GL_TRIANGLES: unit = 3; break;
		case GL_QUADS: unit = 4; break;
		case GL_LINE_STRIP: unit = 1; overlap = 1; break;
		case GL_TRIANGLE_STRIP: case GL_QUAD_STRIP: unit = 2; overlap = 2; break;
		default: break;
	}
	const size_t part = GPU_stream_part_size();
	int piece = 0;
	if (unit && part && pk.bpv * (size_t)n > part) {
		piece = (int)(part / pk.bpv);
		piece -= piece % unit;
		if (piece <= overlap + unit) {
			piece = 0;
		}
	}
	if (!piece) {
		lgl_emit(prim, 0, n, &pk, prim_class);
		return;
	}
	for (int start = 0; start < n; ) {
		const int c = (n - start < piece) ? n - start : piece;
		lgl_emit(prim, start, c, &pk, prim_class);
		lgl.stat_pieces += 1.0;
		if (start + c >= n) {
			break;
		}
		start += c - overlap;
	}
}

void GPU_lgl_Begin(GLenum mode)
{
	const unsigned int e = mode;
	LGL_RECORD(LOP_BEGIN, &e, 1, NULL, 0);
	if (lgl.in_begin) {
		lgl_note_once("glBegin inside glBegin");
		return;
	}
	lgl.in_begin = 1;
	lgl.prim = mode;
	lgl.nverts = 0;
	lgl.xused = 0;
	lgl.pos_w = 0;
	lgl.col_ub = 0;
	lgl.dist_start = 0.0f;
	/* which blocks can be drawn as they come */
	switch (mode) {
		case GL_POINTS: case GL_LINES: case GL_TRIANGLES: case GL_QUADS: lgl.flush_overlap = 0; break;
		case GL_LINE_STRIP: lgl.flush_overlap = 1; break;
		case GL_TRIANGLE_STRIP: case GL_QUAD_STRIP: lgl.flush_overlap = 2; break;
		default: lgl.flush_overlap = -1; break;  /* loops, fans, polygons */
	}
	if (lgl_needs_conversion(mode) && lgl.poly_mode[0] == GL_LINE && (lgl.en & E_LINE_STIPPLE)) {
		lgl.flush_overlap = -1;  /* a stippled outline is redone as a whole */
	}
}

/* the block so far drawn; a strip keeps its last vertices to go on from */
static void lgl_flush_part(void)
{
	lgl_draw_immediate();
	lgl.stat_pieces += 1.0;
	const int n = lgl.nverts, keep = lgl.flush_overlap;
	if (keep > 0) {
		memmove(lgl.verts, &lgl.verts[(n - keep) * 4], sizeof(float[4]) * (size_t)keep);
		for (int s = 0; s < X_SLOTS; s++) {
			if (lgl.xused & (1u << s)) {
				memmove(lgl.xs[s], &lgl.xs[s][(n - keep) * 4], sizeof(float[4]) * (size_t)keep);
			}
		}
		lgl.nverts = keep;
	}
	else {
		/* the values now current are the next part's (until they change) */
		lgl.nverts = 0;
		lgl.xused = 0;
		lgl.pos_w = 0;
		lgl.col_ub = 0;
	}
}

void GPU_lgl_End(void)
{
	LGL_RECORD(LOP_END, NULL, 0, NULL, 0);
	if (!lgl.in_begin) {
		return;
	}
	lgl.in_begin = 0;
	lgl_draw_immediate();
}

void gpu_lgl_stats(double *r_draws, double *r_vertices, double *r_bytes, double *r_pieces, double *r_blocks,
                   int reset)
{
	*r_draws = lgl.stat_draws;
	*r_vertices = lgl.stat_verts;
	*r_bytes = lgl.stat_bytes;
	*r_pieces = lgl.stat_pieces;
	*r_blocks = lgl.stat_blocks;
	if (reset) {
		lgl.stat_draws = lgl.stat_verts = lgl.stat_bytes = lgl.stat_pieces = lgl.stat_blocks = 0.0;
	}
}

void lgl_rect4f(float x1, float y1, float x2, float y2)
{
	GPU_lgl_Begin(GL_POLYGON);
	lgl_vertex4f(x1, y1, 0.0f, 1.0f);
	lgl_vertex4f(x2, y1, 0.0f, 1.0f);
	lgl_vertex4f(x2, y2, 0.0f, 1.0f);
	lgl_vertex4f(x1, y2, 0.0f, 1.0f);
	GPU_lgl_End();
}

/* ------------------------------------------------------------------------ */
/* client arrays */

static LglArray *lgl_client_array(GLenum array)
{
	switch (array) {
		case GL_VERTEX_ARRAY: return &lgl.arr_pos;
		case GL_COLOR_ARRAY: return &lgl.arr_col;
		case GL_NORMAL_ARRAY: return &lgl.arr_nor;
		case GL_TEXTURE_COORD_ARRAY: return &lgl.arr_tex[lgl.client_unit];
		default: return NULL;
	}
}

void GPU_lgl_EnableClientState(GLenum array)
{
	LglArray *a = lgl_client_array(array);
	if (a) {
		a->enabled = 1;
	}
	else {
		lgl_note_once("glEnableClientState: an array the emulation does not draw");
	}
}

void GPU_lgl_DisableClientState(GLenum array)
{
	LglArray *a = lgl_client_array(array);
	if (a) {
		a->enabled = 0;
	}
}

static void lgl_array_set(LglArray *a, int size, GLenum type, int normalized, int integer, GLsizei stride,
                          const void *pointer)
{
	a->size = size;
	a->type = type;
	a->normalized = normalized;
	a->integer = integer;
	a->stride = stride;
	a->pointer = pointer;
	a->buffer = lgl.app_array_buffer;
}

void GPU_lgl_VertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
	lgl_array_set(&lgl.arr_pos, size, type, 0, 0, stride, pointer);
}

void GPU_lgl_NormalPointer(GLenum type, GLsizei stride, const void *pointer)
{
	lgl_array_set(&lgl.arr_nor, 3, type, type != GL_FLOAT && type != GL_DOUBLE, 0, stride, pointer);
}

void GPU_lgl_ColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
	lgl_array_set(&lgl.arr_col, size, type, type != GL_FLOAT && type != GL_DOUBLE, 0, stride, pointer);
}

void GPU_lgl_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
	lgl_array_set(&lgl.arr_tex[lgl.client_unit], size, type, 0, 0, stride, pointer);
}

void GPU_lgl_InterleavedArrays(GLenum format, GLsizei stride, const void *pointer)
{
	(void)format;
	(void)stride;
	(void)pointer;
	lgl_note_once("glInterleavedArrays is not emulated");
}

static GLsizei lgl_stride(const LglArray *a)
{
	return a->stride ? a->stride : (GLsizei)(a->size * lgl_type_size(a->type));
}

/* one element of an array, as floats (normalised as OpenGL would) */
static void lgl_fetch(const LglArray *a, int i, float r[4], const float def[4])
{
	memcpy(r, def, sizeof(float[4]));
	const GLsizei stride = lgl_stride(a);
	const int ts = lgl_type_size(a->type);
	unsigned char tmp[64];
	const unsigned char *p;
	if (a->buffer) {
		const GLintptr off = (GLintptr)a->pointer + (GLintptr)stride * i;
		glGetNamedBufferSubData(a->buffer, off, a->size * ts, tmp);
		p = tmp;
	}
	else {
		p = (const unsigned char *)a->pointer + (size_t)stride * (size_t)i;
	}
	for (int k = 0; k < a->size && k < 4; k++) {
		const unsigned char *q = p + k * ts;
		float v;
		switch (a->type) {
			case GL_BYTE: v = *(const signed char *)q; if (a->normalized) v /= 127.0f; break;
			case GL_UNSIGNED_BYTE: v = *q; if (a->normalized) v /= 255.0f; break;
			case GL_SHORT: v = *(const short *)q; if (a->normalized) v /= 32767.0f; break;
			case GL_UNSIGNED_SHORT: v = *(const unsigned short *)q; if (a->normalized) v /= 65535.0f; break;
			case GL_INT: v = (float)*(const int *)q; if (a->normalized) v /= 2147483647.0f; break;
			case GL_UNSIGNED_INT: v = (float)*(const unsigned int *)q; if (a->normalized) v /= 4294967295.0f; break;
			case GL_DOUBLE: v = (float)*(const double *)q; break;
			default: v = *(const float *)q; break;
		}
		r[k] = v;
	}
}

void lgl_array_element(int i)
{
	float v[4];
	static const float def0001[4] = {0, 0, 0, 1};
	if (lgl.arr_col.enabled) {
		static const float defc[4] = {0, 0, 0, 1};
		lgl_fetch(&lgl.arr_col, i, v, defc);
		lgl_color4f(v[0], v[1], v[2], v[3]);
	}
	if (lgl.arr_nor.enabled) {
		lgl_fetch(&lgl.arr_nor, i, v, def0001);
		lgl_normal3f(v[0], v[1], v[2]);
	}
	for (int u = 0; u < LGL_FF_UNITS; u++) {
		if (lgl.arr_tex[u].enabled) {
			lgl_fetch(&lgl.arr_tex[u], i, v, def0001);
			lgl_texcoord4f(u, v[0], v[1], v[2], v[3]);
		}
	}
	for (int g = 1; g < LGL_GENERIC; g++) {
		if (lgl.arr_gen[g].enabled) {
			lgl_fetch(&lgl.arr_gen[g], i, v, def0001);
			lgl_attrib4f((unsigned int)g, v[0], v[1], v[2], v[3]);
		}
	}
	if (lgl.arr_pos.enabled) {
		lgl_fetch(&lgl.arr_pos, i, v, def0001);
		lgl_vertex4f(v[0], v[1], v[2], v[3]);
	}
	else if (lgl.arr_gen[0].enabled) {
		lgl_fetch(&lgl.arr_gen[0], i, v, def0001);
		lgl_vertex4f(v[0], v[1], v[2], v[3]);
	}
}

void GPU_lgl_ArrayElement(GLint i)
{
	lgl_array_element(i);
}

static unsigned int lgl_index_at(GLenum itype, const void *indices, int i)
{
	switch (itype) {
		case GL_UNSIGNED_BYTE: return ((const unsigned char *)indices)[i];
		case GL_UNSIGNED_SHORT: return ((const unsigned short *)indices)[i];
		default: return ((const unsigned int *)indices)[i];
	}
}

/* through immediate mode (display lists, line stipples) */
static void lgl_draw_by_elements(GLenum mode, GLint first, GLsizei count, GLenum itype, const void *cpu_indices)
{
	GPU_lgl_Begin(mode);
	for (GLsizei i = 0; i < count; i++) {
		lgl_array_element(itype ? (int)lgl_index_at(itype, cpu_indices, i) : first + i);
	}
	GPU_lgl_End();
}

static void lgl_src_from_array(LglSrc *s, const LglArray *a)
{
	memset(s, 0, sizeof(*s));
	s->on = 1;
	s->buffer = a->buffer;
	s->offset = (GLintptr)a->pointer;
	s->stride = lgl_stride(a);
	s->size = a->size;
	s->type = a->type;
	s->normalized = a->normalized;
	s->integer = a->integer;
	s->divisor = a->divisor;
}

/* glDrawArrays (itype 0) and glDrawElements with the client arrays */
static void lgl_draw_arrays(GLenum mode, GLint first, GLsizei count, GLenum itype, const void *indices,
                            GLsizei instances, int range_known, GLuint rmin, GLuint rmax)
{
	if (count <= 0) {
		return;
	}
	/* the indices on the processor, where needed */
	const void *cpu_indices = NULL;
	void *cpu_owned = NULL;
	const int its = itype ? lgl_type_size(itype) : 0;
	if (itype) {
		if (lgl.app_ebo) {
			cpu_owned = MEM_mallocN((size_t)count * (size_t)its, "lgl indices read");
			glGetNamedBufferSubData(lgl.app_ebo, (GLintptr)indices, (GLsizeiptr)count * its, cpu_owned);
			cpu_indices = cpu_owned;
		}
		else {
			cpu_indices = indices;
		}
	}

	if (lgl.list || ((lgl.en & E_LINE_STIPPLE) && lgl_prim_class(mode) == 1)) {
		lgl_draw_by_elements(mode, first, count, itype, cpu_indices);
		if (cpu_owned) {
			MEM_freeN(cpu_owned);
		}
		return;
	}
	if (!lgl.arr_pos.enabled && !lgl.arr_gen[0].enabled) {
		if (cpu_owned) {
			MEM_freeN(cpu_owned);
		}
		return;
	}

	/* the vertices used */
	GLuint vmin, vmax;
	if (!itype) {
		vmin = (GLuint)first;
		vmax = (GLuint)(first + count - 1);
	}
	else if (range_known) {
		vmin = rmin;
		vmax = rmax;
	}
	else {
		vmin = 0xffffffffu;
		vmax = 0;
		for (GLsizei i = 0; i < count; i++) {
			const unsigned int k = lgl_index_at(itype, cpu_indices, i);
			if (k < vmin) vmin = k;
			if (k > vmax) vmax = k;
		}
	}

	LglArray *arrays[S_FIXED];
	memset(arrays, 0, sizeof(arrays));
	arrays[S_POS] = lgl.arr_pos.enabled ? &lgl.arr_pos : &lgl.arr_gen[0];
	arrays[S_COL] = lgl.arr_col.enabled ? &lgl.arr_col : NULL;
	arrays[S_NOR] = lgl.arr_nor.enabled ? &lgl.arr_nor : NULL;
	for (int u = 0; u < LGL_FF_UNITS; u++) {
		arrays[S_TEX0 + u] = lgl.arr_tex[u].enabled ? &lgl.arr_tex[u] : NULL;
	}
	int any_client = 0, any_buffer = 0;
	for (int i = 0; i < S_FIXED; i++) {
		if (arrays[i]) {
			if (arrays[i]->buffer) any_buffer = 1; else any_client = 1;
		}
	}
	for (int g = 1; g < LGL_GENERIC; g++) {
		if (lgl.arr_gen[g].enabled) {
			if (lgl.arr_gen[g].buffer) any_buffer = 1; else any_client = 1;
		}
	}
	/* a small quad, quad strip or polygon from the processor's memory: through
	 * immediate mode, which draws it without an index buffer (the current
	 * values the arrays set on the way are put back) */
	if (lgl_needs_conversion(mode) && count <= LGL_GATHER && !any_buffer && instances == 1) {
		float color[4], normal[3], tex[LGL_UNITS][4], generic[LGL_GENERIC][4];
		const int color_ub = lgl.color_ub;
		memcpy(color, lgl.color, sizeof(color));
		memcpy(normal, lgl.normal, sizeof(normal));
		memcpy(tex, lgl.tex, sizeof(tex));
		memcpy(generic, lgl.generic, sizeof(generic));
		lgl_draw_by_elements(mode, first, count, itype, cpu_indices);
		const int color_back = memcmp(color, lgl.color, sizeof(color)) != 0;
		memcpy(lgl.color, color, sizeof(color));
		lgl.color_ub = color_ub;
		memcpy(lgl.normal, normal, sizeof(normal));
		memcpy(lgl.tex, tex, sizeof(tex));
		memcpy(lgl.generic, generic, sizeof(generic));
		if (color_back) {
			lgl_color_changed();
		}
		if (cpu_owned) {
			MEM_freeN(cpu_owned);
		}
		return;
	}

	/* client memory is copied from "base" (all of it from 0 when buffers are
	 * also read, as they are read where they are) */
	const GLuint base = (any_client && !any_buffer) ? vmin : 0;

	LglSrc fixed[S_FIXED], gen[LGL_GENERIC];
	memset(fixed, 0, sizeof(fixed));
	memset(gen, 0, sizeof(gen));
	LglArray *all[S_FIXED + LGL_GENERIC];
	LglSrc *dst[S_FIXED + LGL_GENERIC];
	int nall = 0;
	for (int i = 0; i < S_FIXED; i++) {
		if (arrays[i]) {
			all[nall] = arrays[i];
			dst[nall++] = &fixed[i];
		}
	}
	for (int g = 1; g < LGL_GENERIC; g++) {
		if (lgl.arr_gen[g].enabled) {
			all[nall] = &lgl.arr_gen[g];
			dst[nall++] = &gen[g];
		}
	}
	for (int k = 0; k < nall; k++) {
		const LglArray *a = all[k];
		lgl_src_from_array(dst[k], a);
		if (!a->buffer) {
			const GLsizei stride = lgl_stride(a);
			const size_t esize = (size_t)a->size * (size_t)lgl_type_size(a->type);
			const size_t bytes = (size_t)stride * (size_t)(vmax - base) + esize;
			GLintptr off;
			dst[k]->buffer = lgl_upload((const unsigned char *)a->pointer + (size_t)stride * base, bytes, &off);
			dst[k]->offset = off;
		}
	}
	/* what is not an array: the current values */
	if (!arrays[S_COL]) memcpy(fixed[S_COL].value, lgl.color, sizeof(float[4]));
	if (!arrays[S_NOR]) {
		memcpy(fixed[S_NOR].value, lgl.normal, sizeof(float[3]));
		fixed[S_NOR].value[3] = 1.0f;
	}
	for (int u = 0; u < LGL_FF_UNITS; u++) {
		if (!arrays[S_TEX0 + u]) memcpy(fixed[S_TEX0 + u].value, lgl.tex[u], sizeof(float[4]));
	}

	const int cls = lgl_prim_class(mode);
	if (lgl_needs_conversion(mode)) {
		unsigned int *src = NULL;
		if (itype) {
			src = MEM_mallocN(sizeof(unsigned int) * (size_t)count, "lgl indices");
			for (GLsizei i = 0; i < count; i++) {
				src[i] = lgl_index_at(itype, cpu_indices, i) - base;
			}
		}
		else if (first - (GLint)base != 0) {
			src = MEM_mallocN(sizeof(unsigned int) * (size_t)count, "lgl indices");
			for (GLsizei i = 0; i < count; i++) {
				src[i] = (unsigned int)(first + i) - base;
			}
		}
		GLenum p2;
		int cls2;
		const int n = lgl_convert(mode, src, count, &p2, &cls2);
		if (src) {
			MEM_freeN(src);
		}
		if (n > 0) {
			GLintptr ioff;
			const GLuint ib = lgl_upload(lgl_idx, sizeof(unsigned int) * (size_t)n, &ioff);
			lgl_submit(p2, 0, n, GL_UNSIGNED_INT, ib, ioff, 0, instances, fixed, gen, cls2, 0);
		}
	}
	else if (itype) {
		GLuint ib;
		GLintptr ioff;
		if (lgl.app_ebo) {
			ib = lgl.app_ebo;
			ioff = (GLintptr)indices;
		}
		else {
			ib = lgl_upload(indices, (size_t)count * (size_t)its, &ioff);
		}
		lgl_submit(mode, 0, count, itype, ib, ioff, -(GLint)base, instances, fixed, gen, cls, 0);
	}
	else {
		lgl_submit(mode, first - (GLint)base, count, 0, 0, 0, 0, instances, fixed, gen, cls, 0);
	}
	if (cpu_owned) {
		MEM_freeN(cpu_owned);
	}
}

/* the application's own vertex array draws: its array and program bound
 * (the emulation's draws in between bound its own) */
static void lgl_app_bindings(void)
{
	if (GPU_lgl_on) {
		LglContext *lc = lgl_context();
		lgl_bind_vao(lc, lgl.app_vao);
		if (lgl.app_program) {
			lgl_use_program(lc, lgl.app_program);
		}
	}
}

void GPU_lgl_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if (lgl.app_vao) {
		lgl_app_bindings();
		glDrawArrays(mode, first, count);
		return;
	}
	lgl_draw_arrays(mode, first, count, 0, NULL, 1, 0, 0, 0);
}

void GPU_lgl_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
	if (lgl.app_vao) {
		lgl_app_bindings();
		glDrawElements(mode, count, type, indices);
		return;
	}
	lgl_draw_arrays(mode, 0, count, type, indices, 1, 0, 0, 0);
}

void GPU_lgl_DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                               const void *indices)
{
	if (!GPU_lgl_on || lgl.app_vao) {
		lgl_app_bindings();
		glDrawRangeElements(mode, start, end, count, type, indices);
		return;
	}
	lgl_draw_arrays(mode, 0, count, type, indices, 1, 1, start, end);
}

void GPU_lgl_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{
	if (!GPU_lgl_on || lgl.app_vao) {
		lgl_app_bindings();
		glDrawArraysInstanced(mode, first, count, instances);
		return;
	}
	lgl_draw_arrays(mode, first, count, 0, NULL, instances, 0, 0, 0);
}

void GPU_lgl_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                   GLsizei instances)
{
	if (!GPU_lgl_on || lgl.app_vao) {
		lgl_app_bindings();
		glDrawElementsInstanced(mode, count, type, indices, instances);
		return;
	}
	lgl_draw_arrays(mode, 0, count, type, indices, instances, 0, 0, 0);
}

/* ------------------------------------------------------------------------ */
/* pixels and bitmaps: a quad in clip space with the emulation's texture */

void lgl_draw_quad_pixels(int mode, unsigned int texture, const float corners[4][4], const float uv[4][2],
                          const float color[4])
{
	float verts[4][16];  /* pos 4, colour 4, normal 3, distance 1, texture 4 */
	memset(verts, 0, sizeof(verts));
	for (int i = 0; i < 4; i++) {
		memcpy(verts[i], corners[i], sizeof(float[4]));
		memcpy(verts[i] + 4, color, sizeof(float[4]));
		verts[i][12] = uv[i][0];
		verts[i][13] = uv[i][1];
		verts[i][15] = 1.0f;
	}
	GLintptr off;
	const GLuint buf = lgl_upload(verts, sizeof(verts), &off);
	LglSrc fixed[S_FIXED], gen[LGL_GENERIC];
	memset(fixed, 0, sizeof(fixed));
	memset(gen, 0, sizeof(gen));
	static const int offs[S_FIXED] = {0, 16, 32, 48, 0, 0, 0, 44};
	static const int sizes[S_FIXED] = {4, 4, 3, 4, 4, 4, 4, 1};
	for (int i = 0; i < S_FIXED; i++) {
		if (i >= S_TEX1 && i <= S_TEX3) {
			continue;
		}
		fixed[i].on = 1;
		fixed[i].buffer = buf;
		fixed[i].offset = off + offs[i];
		fixed[i].stride = (GLsizei)sizeof(float[16]);
		fixed[i].size = sizes[i];
		fixed[i].type = GL_FLOAT;
	}
	glBindTextureUnit(LGL_PX_UNIT, texture);
	/* the fixed-function look draws them (whatever program the application has) */
	const GLuint app_program = lgl.app_program;
	lgl.app_program = 0;
	/* the fixed-function texturing is off for them */
	lgl_submit(GL_TRIANGLE_FAN, 0, 4, 0, 0, 0, 0, 1, fixed, gen, 2, mode);
	lgl.app_program = app_program;
	lgl_use_program(lgl_context(), app_program);
	glBindTextureUnit(LGL_PX_UNIT, lgl_unit15_texture());
}

/* ------------------------------------------------------------------------ */
/* shared helpers */

void lgl_current_mvp(float r_mvp[16])
{
	float mv[4][4], proj[4][4];
	GPU_matrix_get(GPU_MATRIX_MODELVIEW, mv);
	GPU_matrix_get(GPU_MATRIX_PROJECTION, proj);
	for (int c = 0; c < 4; c++) {
		for (int r = 0; r < 4; r++) {
			r_mvp[c * 4 + r] = proj[0][r] * mv[c][0] + proj[1][r] * mv[c][1] + proj[2][r] * mv[c][2] +
			                   proj[3][r] * mv[c][3];
		}
	}
}

void lgl_viewport(int r_vp[4])
{
	LglContext *lc = lgl_context();
	if (!lc->vp_valid) {
		glGetIntegerv(GL_VIEWPORT, lc->vp);
		lc->vp_valid = 1;
	}
	memcpy(r_vp, lc->vp, sizeof(int[4]));
}

float lgl_point_size(void)
{
	LglContext *lc = lgl_context();
	if (!lc->point_size_valid) {
		glGetFloatv(GL_POINT_SIZE, &lc->point_size);
		lc->point_size_valid = 1;
	}
	return lc->point_size;
}

void GPU_lgl_Viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
	glViewport(x, y, w, h);
	LglContext *lc = lgl_context();
	lc->vp[0] = x;
	lc->vp[1] = y;
	lc->vp[2] = w;
	lc->vp[3] = h;
	lc->vp_valid = 1;
}

void GPU_lgl_PointSize(GLfloat size)
{
	glPointSize(size);
	LglContext *lc = lgl_context();
	lc->point_size = size;
	lc->point_size_valid = 1;
}

/* the base formats of textures the application made with the old formats */
typedef struct LglTexFmt {
	unsigned int texture;
	int base;
} LglTexFmt;

#define LGL_TEXFMTS 4096
static LglTexFmt *lgl_texfmts = NULL;

int lgl_tex_base_format(unsigned int texture)
{
	if (!lgl_texfmts || !texture) {
		return 0;
	}
	unsigned int h = (texture * 2654435761u) & (LGL_TEXFMTS - 1);
	for (int n = 0; n < 16; n++, h = (h + 1) & (LGL_TEXFMTS - 1)) {
		if (lgl_texfmts[h].texture == texture) {
			return lgl_texfmts[h].base;
		}
		if (lgl_texfmts[h].texture == 0) {
			break;
		}
	}
	return 0;
}

void lgl_tex_set_base_format(unsigned int texture, int base)
{
	if (!texture) {
		return;
	}
	if (!lgl_texfmts) {
		if (base == 0) {
			return;
		}
		lgl_texfmts = MEM_callocN(sizeof(LglTexFmt) * LGL_TEXFMTS, "lgl texture formats");
	}
	unsigned int h = (texture * 2654435761u) & (LGL_TEXFMTS - 1);
	LglTexFmt *empty = NULL;
	for (int n = 0; n < 16; n++, h = (h + 1) & (LGL_TEXFMTS - 1)) {
		if (lgl_texfmts[h].texture == texture) {
			lgl_texfmts[h].base = base;
			return;
		}
		if (lgl_texfmts[h].texture == 0 && !empty) {
			empty = &lgl_texfmts[h];
		}
	}
	if (base == 0) {
		return;
	}
	if (!empty) {
		/* the table is crowded here: the slot is taken over */
		empty = &lgl_texfmts[(texture * 2654435761u) & (LGL_TEXFMTS - 1)];
	}
	empty->texture = texture;
	empty->base = base;
}

/* ------------------------------------------------------------------------ */
/* GLEW functions */

void GPU_lgl_UseProgram(GLuint program)
{
	if (GPU_lgl_on) {
		lgl.app_program = program;
		lgl_use_program(lgl_context(), program);
		return;
	}
	glUseProgram(program);
}

void GPU_lgl_BindVertexArray(GLuint vao)
{
	if (!GPU_lgl_on) {
		glBindVertexArray(vao);
		return;
	}
	lgl.app_vao = vao;
	LglContext *lc = lgl_context();
	lgl_bind_vao(lc, vao ? vao : lc->vao);
}

void GPU_lgl_DeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
	if (GPU_lgl_on) {
		LglContext *lc = lgl_context();
		for (GLsizei i = 0; i < n; i++) {
			if (arrays[i] && arrays[i] == lgl.app_vao) {
				lgl.app_vao = 0;
			}
			if (arrays[i] && arrays[i] == lc->vao_bound) {
				/* deleting the bound vertex array binds none */
				lc->vao_known = 0;
			}
		}
	}
	glDeleteVertexArrays(n, arrays);
}

void GPU_lgl_BindBuffer(GLenum target, GLuint buffer)
{
	if (GPU_lgl_on) {
		if (target == GL_ELEMENT_ARRAY_BUFFER && !lgl.app_vao) {
			/* the emulation's vertex array stands for the default one: it
			 * holds the binding (glBufferData and the like use it) */
			LglContext *lc = lgl_context();
			lgl.app_ebo = buffer;
			lgl_bind_vao(lc, lc->vao);
			glBindBuffer(target, buffer);
			lc->ebo = buffer;
			return;
		}
		if (target == GL_ARRAY_BUFFER) {
			lgl.app_array_buffer = buffer;
		}
	}
	glBindBuffer(target, buffer);
}

void GPU_lgl_DeleteBuffers(GLsizei n, const GLuint *buffers)
{
	if (GPU_lgl_on) {
		for (GLsizei i = 0; i < n; i++) {
			if (!buffers[i]) {
				continue;
			}
			if (buffers[i] == lgl.app_ebo) {
				lgl.app_ebo = 0;
			}
			if (buffers[i] == lgl.app_array_buffer) {
				lgl.app_array_buffer = 0;
			}
			/* the context's vertex arrays may still hold it */
			for (int c = 0; c < lgl_nctx; c++) {
				if (lgl_ctx[c].ebo == buffers[i]) {
					lgl_ctx[c].ebo = (GLuint)-1;
				}
				for (int k = 0; k < 16; k++) {
					if (lgl_ctx[c].attr[k].buffer == buffers[i]) {
						lgl_ctx[c].attr[k].valid = 0;
					}
				}
			}
		}
	}
	glDeleteBuffers(n, buffers);
}

void GPU_lgl_VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride,
                                 const void *pointer)
{
	if (!GPU_lgl_on || lgl.app_vao || index >= LGL_GENERIC) {
		glVertexAttribPointer(index, size, type, normalized, stride, pointer);
		return;
	}
	lgl_array_set(&lgl.arr_gen[index], size, type, normalized ? 1 : 0, 0, stride, pointer);
}

void GPU_lgl_VertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer)
{
	if (!GPU_lgl_on || lgl.app_vao || index >= LGL_GENERIC) {
		glVertexAttribIPointer(index, size, type, stride, pointer);
		return;
	}
	lgl_array_set(&lgl.arr_gen[index], size, type, 0, 1, stride, pointer);
}

void GPU_lgl_EnableVertexAttribArray(GLuint index)
{
	if (!GPU_lgl_on || lgl.app_vao || index >= LGL_GENERIC) {
		glEnableVertexAttribArray(index);
		return;
	}
	lgl.arr_gen[index].enabled = 1;
}

void GPU_lgl_DisableVertexAttribArray(GLuint index)
{
	if (!GPU_lgl_on || lgl.app_vao || index >= LGL_GENERIC) {
		glDisableVertexAttribArray(index);
		return;
	}
	lgl.arr_gen[index].enabled = 0;
}

void GPU_lgl_VertexAttribDivisor(GLuint index, GLuint divisor)
{
	if (!GPU_lgl_on || lgl.app_vao || index >= LGL_GENERIC) {
		glVertexAttribDivisor(index, divisor);
		return;
	}
	lgl.arr_gen[index].divisor = divisor;
}

void GPU_lgl_ActiveTexture(GLenum texture)
{
	if (GPU_lgl_on) {
		lgl.active_unit = (int)(texture - GL_TEXTURE0);
	}
	glActiveTexture(texture);
}

void GPU_lgl_ClientActiveTexture(GLenum texture)
{
	if (!GPU_lgl_on) {
		glClientActiveTexture(texture);
		return;
	}
	const int u = (int)(texture - GL_TEXTURE0);
	lgl.client_unit = (u >= 0 && u < LGL_UNITS) ? u : 0;
}

/* ---- shaders: the old GLSL translated, a vertex stage for a fragment one ---- */

typedef struct LglShaderMark {
	GLuint shader;
	int translated;
} LglShaderMark;

#define LGL_SHADERMARKS 1024
static LglShaderMark *lgl_shadermarks = NULL;

static void lgl_shader_mark(GLuint shader, int translated)
{
	if (!lgl_shadermarks) {
		lgl_shadermarks = MEM_callocN(sizeof(LglShaderMark) * LGL_SHADERMARKS, "lgl shader marks");
	}
	LglShaderMark *m = &lgl_shadermarks[shader & (LGL_SHADERMARKS - 1)];
	m->shader = shader;
	m->translated = translated;
}

static int lgl_shader_translated(GLuint shader)
{
	if (!lgl_shadermarks) {
		return 0;
	}
	const LglShaderMark *m = &lgl_shadermarks[shader & (LGL_SHADERMARKS - 1)];
	return m->shader == shader && m->translated;
}

/* GLSL a core context compiles as it is: "#version 150 core" and later
 * (without "compatibility") */
static int lgl_glsl_is_core(const char *src)
{
	const char *p = src;
	while (*p) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
			p++;
		}
		if (p[0] == '/' && p[1] == '/') {
			while (*p && *p != '\n') p++;
			continue;
		}
		if (p[0] == '/' && p[1] == '*') {
			const char *e = strstr(p + 2, "*/");
			p = e ? e + 2 : p + strlen(p);
			continue;
		}
		break;
	}
	if (*p != '#') {
		return 0;
	}
	p++;
	while (*p == ' ' || *p == '\t') p++;
	if (strncmp(p, "version", 7) != 0) {
		return 0;
	}
	p += 7;
	const int v = atoi(p);
	while (*p && *p != '\n' && *p != 'c') p++;
	const int compat = (strncmp(p, "compatibility", 13) == 0);
	if (compat) {
		return 0;
	}
	if (v == 150) {
		return strncmp(p, "core", 4) == 0;
	}
	return v >= 330;
}

void GPU_lgl_ShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{
	if (!GPU_lgl_on) {
		glShaderSource(shader, count, (const GLchar **)string, length);
		return;
	}
	size_t total = 1;
	for (GLsizei i = 0; i < count; i++) {
		total += (length && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
	}
	char *src = MEM_mallocN(total, "lgl shader source");
	size_t at = 0;
	for (GLsizei i = 0; i < count; i++) {
		const size_t n = (length && length[i] >= 0) ? (size_t)length[i] : strlen(string[i]);
		memcpy(src + at, string[i], n);
		at += n;
	}
	src[at] = '\0';
	if (lgl_glsl_is_core(src)) {
		glShaderSource(shader, 1, (const GLchar **)&src, NULL);
		lgl_shader_mark(shader, 0);
		MEM_freeN(src);
		return;
	}
	GLint type = 0;
	glGetShaderiv(shader, GL_SHADER_TYPE, &type);
	const int stage = (type == GL_FRAGMENT_SHADER) ? GPU_TRANSLATE_FRAGMENT :
	                  (type == GL_GEOMETRY_SHADER) ? GPU_TRANSLATE_GEOMETRY : GPU_TRANSLATE_VERTEX;
	int problems = 0;
	char *tr = GPU_glsl_translate(src, stage, true, "glShaderSource", &problems);
	glShaderSource(shader, 1, (const GLchar **)&tr, NULL);
	lgl_shader_mark(shader, 1);
	MEM_freeN(tr);
	MEM_freeN(src);
}

/* the vertex stage the old fixed function gave a program with only a
 * fragment stage (the translation's names) */
static const char *lgl_default_vert =
        "#version 450 core\n"
        "layout(location = 0) in vec4 bge_pos;\n"
        "in vec4 bge_col;\n"
        "in vec4 bge_texco0;\n"
        "in vec4 bge_texco1;\n"
        "uniform mat4 bge_mvp;\n"
        "uniform mat4 bge_texturematrix[8];\n"
        "out vec4 bge_texcoord[8];\n"
        "out vec4 bge_frontcolor;\n"
        "out vec4 bge_frontcolor2;\n"
        "out float bge_fogcoord;\n"
        "void main() {\n"
        "	gl_Position = bge_mvp * bge_pos;\n"
        "	bge_texcoord[0] = bge_texturematrix[0] * bge_texco0;\n"
        "	bge_texcoord[1] = bge_texturematrix[1] * bge_texco1;\n"
        "	for (int i = 2; i < 8; i++) bge_texcoord[i] = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "	bge_frontcolor = bge_col;\n"
        "	bge_frontcolor2 = vec4(0.0);\n"
        "	bge_fogcoord = abs((bge_mvp * bge_pos).z);\n"
        "}\n";
static GLuint lgl_default_vs = 0;

void GPU_lgl_LinkProgram(GLuint program)
{
	if (!GPU_lgl_on) {
		glLinkProgram(program);
		return;
	}
	GLuint shaders[16];
	GLsizei n = 0;
	glGetAttachedShaders(program, 16, &n, shaders);
	int has_vertex = 0, has_fragment = 0, translated = 0;
	for (GLsizei i = 0; i < n; i++) {
		GLint type = 0;
		glGetShaderiv(shaders[i], GL_SHADER_TYPE, &type);
		has_vertex |= (type == GL_VERTEX_SHADER);
		has_fragment |= (type == GL_FRAGMENT_SHADER);
		translated |= lgl_shader_translated(shaders[i]);
	}
	if (has_fragment && !has_vertex) {
		if (!lgl_default_vs) {
			lgl_default_vs = lgl_compile(GL_VERTEX_SHADER, &lgl_default_vert, 1, "old fixed-function vertex stage");
		}
		if (lgl_default_vs) {
			glAttachShader(program, lgl_default_vs);
		}
		translated = 1;
	}
	if (translated) {
		glBindAttribLocation(program, 0, "bge_pos");
	}
	glLinkProgram(program);
	lgl_userprog_forget(program);
	GPU_old_builtins_forget(program);
}

void GPU_lgl_DeleteProgram(GLuint program)
{
	if (GPU_lgl_on && program) {
		lgl_userprog_forget(program);
		GPU_old_builtins_forget(program);
	}
	glDeleteProgram(program);
}

GLint GPU_lgl_GetUniformLocation(GLuint program, const GLchar *name)
{
	if (!GPU_lgl_on) {
		return glGetUniformLocation(program, name);
	}
	return GPU_glsl_uniform_location(program, name);
}

GLint GPU_lgl_GetAttribLocation(GLuint program, const GLchar *name)
{
	if (!GPU_lgl_on) {
		return glGetAttribLocation(program, name);
	}
	return GPU_glsl_attrib_location(program, name);
}

void GPU_lgl_BindAttribLocation(GLuint program, GLuint index, const GLchar *name)
{
	if (!GPU_lgl_on) {
		glBindAttribLocation(program, index, name);
		return;
	}
	char buf[256];
	glBindAttribLocation(program, index, GPU_glsl_translated_name(name, buf, sizeof(buf)));
}

void GPU_lgl_ProgramParameteriEXT(GLuint program, GLenum pname, GLint value)
{
	if (!GPU_lgl_on) {
		glProgramParameteriEXT(program, pname, value);
		return;
	}
	/* the old geometry shader settings: GLSL 1.50 says them in the shader */
	if (pname == GL_GEOMETRY_INPUT_TYPE_EXT || pname == GL_GEOMETRY_OUTPUT_TYPE_EXT ||
	    pname == GL_GEOMETRY_VERTICES_OUT_EXT)
	{
		return;
	}
	glProgramParameteri(program, pname, value);
}

/* ---- the ARB shader objects ---- */

void GPU_lgl_DeleteObject(GLuint object)
{
	if (glIsShader(object)) {
		glDeleteShader(object);
	}
	else {
		GPU_lgl_DeleteProgram(object);
	}
}

void GPU_lgl_GetObjectParameteriv(GLuint object, GLenum pname, GLint *params)
{
	const int shader = glIsShader(object);
	if (pname == GL_OBJECT_TYPE_ARB) {
		*params = shader ? GL_SHADER_OBJECT_ARB : GL_PROGRAM_OBJECT_ARB;
		return;
	}
	if (pname == GL_OBJECT_SUBTYPE_ARB) {
		pname = GL_SHADER_TYPE;
	}
	if (shader) {
		glGetShaderiv(object, pname, params);
	}
	else {
		glGetProgramiv(object, pname, params);
	}
}

void GPU_lgl_GetInfoLog(GLuint object, GLsizei maxlen, GLsizei *length, GLchar *log)
{
	if (glIsShader(object)) {
		glGetShaderInfoLog(object, maxlen, length, log);
	}
	else {
		glGetProgramInfoLog(object, maxlen, length, log);
	}
}

/* ------------------------------------------------------------------------ */
/* for bgl */

int GPU_lgl_unsupported(const char *name)
{
	const char **names = gpu_lgl_unsupported_names();
	for (int i = 0; names[i]; i++) {
		if (strcmp(names[i], name) == 0) {
			return 1;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* start and end */

void gpu_lgl_init(int core_profile)
{
	const char *env = getenv("BGE_REMIX_GL_EMULATE");
	GPU_lgl_on = core_profile || (env && env[0] == '1');
	if (!lgl.init) {
		memset(&lgl, 0, sizeof(lgl));
		lgl_state_defaults();
		lgl.init = 1;
	}
	if (GPU_lgl_on) {
		printf("BGE-Remix GL: the old OpenGL calls are emulated (%s)\n",
		       core_profile ? "core context" : "BGE_REMIX_GL_EMULATE=1");
		/* the default vertex array as the application sees it */
		LglContext *lc = lgl_context();
		lgl_bind_vao(lc, lc->vao);
	}
}

void gpu_lgl_exit(void)
{
	for (int i = 0; i < lgl_nprogs; i++) {
		if (lgl_progs[i].program) {
			glDeleteProgram(lgl_progs[i].program);
		}
	}
	lgl_nprogs = 0;
	if (lgl_default_vs) {
		glDeleteShader(lgl_default_vs);
		lgl_default_vs = 0;
	}
	for (int c = 0; c < lgl_nctx; c++) {
		if (lgl_ctx[c].ctx == lgl_platform_context() && lgl_ctx[c].vao) {
			glDeleteVertexArrays(1, &lgl_ctx[c].vao);
		}
	}
	lgl_nctx = 0;
	lgl_cur = NULL;
	lgl_lists_free();
	if (lgl.verts) {
		MEM_freeN(lgl.verts);
		lgl.verts = NULL;
	}
	for (int s = 0; s < X_SLOTS; s++) {
		if (lgl.xs[s]) {
			MEM_freeN(lgl.xs[s]);
			lgl.xs[s] = NULL;
		}
	}
	if (lgl.dist) {
		MEM_freeN(lgl.dist);
		lgl.dist = NULL;
	}
	lgl.cap = 0;
	if (lgl_idx) {
		MEM_freeN(lgl_idx);
		lgl_idx = NULL;
		lgl_idx_cap = 0;
	}
	if (lgl_userprogs) {
		MEM_freeN(lgl_userprogs);
		lgl_userprogs = NULL;
	}
	if (lgl_shadermarks) {
		MEM_freeN(lgl_shadermarks);
		lgl_shadermarks = NULL;
	}
	if (lgl_texfmts) {
		MEM_freeN(lgl_texfmts);
		lgl_texfmts = NULL;
	}
	lgl.block_valid = 0;
	lgl.block_buf = 0;
}
