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
} LglAttrCache;

typedef struct LglContext {
	void *ctx;
	GLuint vao;
	GLuint ebo;
	LglAttrCache attr[16];
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
static GLuint lgl_ubo = 0;

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

static void lgl_attr_set(LglContext *lc, int loc, const LglSrc *s)
{
	LglAttrCache *c = &lc->attr[loc];
	if (s && s->on) {
		if (!c->valid || !c->enabled) {
			glEnableVertexArrayAttrib(lc->vao, (GLuint)loc);
			c->enabled = 1;
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
			glVertexArrayVertexBuffer(lc->vao, (GLuint)loc, s->buffer, s->offset, s->stride);
			c->buffer = s->buffer;
			c->offset = s->offset;
			c->stride = s->stride;
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
		if (s) {
			glVertexAttrib4fv((GLuint)loc, s->value);
		}
	}
}

static void lgl_mat_eq(float a[16], const float b[4][4], int *changed)
{
	if (memcmp(a, b, sizeof(float[16])) != 0) {
		memcpy(a, b, sizeof(float[16]));
		*changed = 1;
	}
}

/* the block of the fixed-function look, given anew where it changed */
static void lgl_update_block(void)
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
	if (!changed) {
		return;
	}
	if (!lgl_ubo) {
		glCreateBuffers(1, &lgl_ubo);
		glNamedBufferData(lgl_ubo, sizeof(LglBlock), NULL, GL_DYNAMIC_DRAW);
	}
	LglBlock b;
	lgl_build_block(&b);
	if (!lgl.block_valid) {
		glNamedBufferSubData(lgl_ubo, 0, sizeof(LglBlock), &b);
	}
	else {
		/* the part that changed */
		const unsigned char *x = (const unsigned char *)&b, *y = (const unsigned char *)&lgl.block;
		size_t lo = 0, hi = sizeof(LglBlock);
		while (lo < hi && x[lo] == y[lo]) {
			lo++;
		}
		while (hi > lo && x[hi - 1] == y[hi - 1]) {
			hi--;
		}
		if (hi > lo) {
			lo &= ~(size_t)15;
			hi = (hi + 15) & ~(size_t)15;
			if (hi > sizeof(LglBlock)) {
				hi = sizeof(LglBlock);
			}
			glNamedBufferSubData(lgl_ubo, (GLintptr)lo, (GLsizeiptr)(hi - lo), x + lo);
		}
	}
	memcpy(&lgl.block, &b, sizeof(b));
	lgl.block_valid = 1;
	lgl.dirty = 0;
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
	glBindVertexArray(lc->vao);

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
		glUseProgram(program);
	}
	else {
		LglProg *lp = lgl_program(lgl_program_key());
		program = lp->program;
		if (!program) {
			return;
		}
		lgl_update_block();
		glBindBufferBase(GL_UNIFORM_BUFFER, 15, lgl_ubo);
		glUseProgram(program);
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
			glGetFloatv(GL_POINT_SIZE, &point[0]);
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

	for (int loc = 0; loc < 16; loc++) {
		lgl_attr_set(lc, loc, at[loc]);
	}

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
	GPU_stream_written(buf);
	*r_offset = (GLintptr)off;
	return buf;
}

/* ------------------------------------------------------------------------ */
/* immediate mode */

#define VF 16  /* floats per vertex */

static void lgl_reserve(int n)
{
	if (n <= lgl.cap) {
		return;
	}
	int cap = lgl.cap ? lgl.cap : 256;
	while (cap < n) {
		cap *= 2;
	}
	lgl.verts = MEM_reallocN_id(lgl.verts, sizeof(float) * VF * (size_t)cap, "lgl vertices");
	for (int s = 0; s < X_SLOTS; s++) {
		if (lgl.xs[s]) {
			lgl.xs[s] = MEM_reallocN_id(lgl.xs[s], sizeof(float[4]) * (size_t)cap, "lgl extra");
		}
	}
	lgl.cap = cap;
}

/* an extra slot set within the block: the vertices before it get the value it had */
static void lgl_slot_use(int slot, const float before[4])
{
	if (lgl.xused & (1u << slot)) {
		return;
	}
	if (!lgl.xs[slot]) {
		lgl.xs[slot] = MEM_mallocN(sizeof(float[4]) * (size_t)(lgl.cap ? lgl.cap : 256), "lgl extra");
		if (!lgl.cap) {
			lgl_reserve(256);
		}
	}
	for (int i = 0; i < lgl.nverts; i++) {
		memcpy(&lgl.xs[slot][i * 4], before, sizeof(float[4]));
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
	lgl_reserve(lgl.nverts + 1);
	float *v = &lgl.verts[lgl.nverts * VF];
	v[0] = x; v[1] = y; v[2] = z; v[3] = w;
	memcpy(v + 4, lgl.color, sizeof(float[4]));
	memcpy(v + 8, lgl.normal, sizeof(float[3]));
	v[11] = 0.0f;
	memcpy(v + 12, lgl.tex[0], sizeof(float[4]));
	if (lgl.xused) {
		for (int s = 0; s < X_SLOTS; s++) {
			if (lgl.xused & (1u << s)) {
				const float *val = (s < X_GEN0) ? lgl.tex[1 + s] : lgl.generic[s - X_GEN0];
				memcpy(&lgl.xs[s][lgl.nverts * 4], val, sizeof(float[4]));
			}
		}
	}
	lgl.nverts++;
}

void lgl_color4f(float r, float g, float b, float a)
{
	const float f[4] = {r, g, b, a};
	LGL_RECORD(LOP_COLOR, NULL, 0, f, 4);
	lgl.color[0] = r;
	lgl.color[1] = g;
	lgl.color[2] = b;
	lgl.color[3] = a;
	/* colour material follows the colour */
	lgl_color_changed();
}

void lgl_normal3f(float x, float y, float z)
{
	const float f[3] = {x, y, z};
	LGL_RECORD(LOP_NORMAL, NULL, 0, f, 3);
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
	if (lgl.in_begin && unit >= 1 && unit < LGL_FF_UNITS) {
		lgl_slot_use(X_TEX1 + unit - 1, lgl.tex[unit]);
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
	if (lgl.in_begin) {
		lgl_slot_use(X_GEN0 + (int)index, lgl.generic[index]);
	}
	lgl.generic[index][0] = x;
	lgl.generic[index][1] = y;
	lgl.generic[index][2] = z;
	lgl.generic[index][3] = w;
	if (!lgl.in_begin) {
		glVertexAttrib4f(index, x, y, z, w);
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
	float prev[2] = {0, 0}, dist = 0.0f;
	for (int i = 0; i < n; i++) {
		float *v = &lgl.verts[i * VF];
		float xy[2];
		lgl_window_xy(mvp, vp, v, xy);
		if (prim == GL_LINES && (i & 1) == 0) {
			dist = 0.0f;
		}
		else if (i > 0) {
			dist += fmaxf(fabsf(xy[0] - prev[0]), fabsf(xy[1] - prev[1]));
		}
		v[11] = dist;
		prev[0] = xy[0];
		prev[1] = xy[1];
	}
}

/* the block's vertices replaced by the ones "idx" names (an outline's edges
 * each with its own two vertices) */
static void lgl_expand(const unsigned int *idx, int count)
{
	float *old = MEM_mallocN(sizeof(float) * VF * (size_t)lgl.nverts, "lgl expand");
	memcpy(old, lgl.verts, sizeof(float) * VF * (size_t)lgl.nverts);
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
		memcpy(&lgl.verts[i * VF], &old[idx[i] * VF], sizeof(float) * VF);
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
	int prim_class = lgl_prim_class(prim);
	const int stipple = (prim_class == 1) && (lgl.en & E_LINE_STIPPLE);
	if (stipple) {
		if (prim == GL_LINE_LOOP && n >= 2) {
			/* a loop as a strip back to its start (the stipple runs on) */
			lgl_reserve(n + 1);
			memcpy(&lgl.verts[n * VF], &lgl.verts[0], sizeof(float) * VF);
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

	/* the vertices, then each extra slot */
	size_t size = sizeof(float) * VF * (size_t)n;
	int nslots = 0;
	for (int s = 0; s < X_SLOTS; s++) {
		if (lgl.xused & (1u << s)) {
			nslots++;
		}
	}
	size += sizeof(float[4]) * (size_t)n * (size_t)nslots;
	GLuint buf = 0;
	size_t off = 0;
	unsigned char *dst = GPU_stream_alloc(size, &buf, &off);
	if (!dst) {
		return;
	}
	memcpy(dst, lgl.verts, sizeof(float) * VF * (size_t)n);
	size_t xoff[X_SLOTS];
	size_t at = sizeof(float) * VF * (size_t)n;
	for (int s = 0; s < X_SLOTS; s++) {
		if (lgl.xused & (1u << s)) {
			memcpy(dst + at, lgl.xs[s], sizeof(float[4]) * (size_t)n);
			xoff[s] = at;
			at += sizeof(float[4]) * (size_t)n;
		}
	}
	GPU_stream_written(buf);

	LglSrc fixed[S_FIXED], gen[LGL_GENERIC];
	memset(fixed, 0, sizeof(fixed));
	memset(gen, 0, sizeof(gen));
	const GLsizei stride = (GLsizei)(sizeof(float) * VF);
	static const int offs[S_FIXED] = {0, 16, 32, 48, 0, 0, 0, 44};
	static const int sizes[S_FIXED] = {4, 4, 3, 4, 4, 4, 4, 1};
	for (int i = 0; i < S_FIXED; i++) {
		if (i >= S_TEX1 && i <= S_TEX3) {
			const int slot = X_TEX1 + (i - S_TEX1);
			if (lgl.xused & (1u << slot)) {
				fixed[i].on = 1;
				fixed[i].buffer = buf;
				fixed[i].offset = (GLintptr)(off + xoff[slot]);
				fixed[i].stride = (GLsizei)sizeof(float[4]);
				fixed[i].size = 4;
				fixed[i].type = GL_FLOAT;
			}
			else {
				memcpy(fixed[i].value, lgl.tex[i - S_TEX0], sizeof(float[4]));
			}
			continue;
		}
		fixed[i].on = 1;
		fixed[i].buffer = buf;
		fixed[i].offset = (GLintptr)(off + (size_t)offs[i]);
		fixed[i].stride = stride;
		fixed[i].size = sizes[i];
		fixed[i].type = GL_FLOAT;
	}
	for (int g = 0; g < LGL_GENERIC; g++) {
		const int slot = X_GEN0 + g;
		if (lgl.xused & (1u << slot)) {
			gen[g].on = 1;
			gen[g].buffer = buf;
			gen[g].offset = (GLintptr)(off + xoff[slot]);
			gen[g].stride = (GLsizei)sizeof(float[4]);
			gen[g].size = 4;
			gen[g].type = GL_FLOAT;
		}
	}

	if (lgl_needs_conversion(prim)) {
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

void GPU_lgl_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
	if (lgl.app_vao) {
		glDrawArrays(mode, first, count);
		return;
	}
	lgl_draw_arrays(mode, first, count, 0, NULL, 1, 0, 0, 0);
}

void GPU_lgl_DrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
	if (lgl.app_vao) {
		glDrawElements(mode, count, type, indices);
		return;
	}
	lgl_draw_arrays(mode, 0, count, type, indices, 1, 0, 0, 0);
}

void GPU_lgl_DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                               const void *indices)
{
	if (!GPU_lgl_on || lgl.app_vao) {
		glDrawRangeElements(mode, start, end, count, type, indices);
		return;
	}
	lgl_draw_arrays(mode, 0, count, type, indices, 1, 1, start, end);
}

void GPU_lgl_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances)
{
	if (!GPU_lgl_on || lgl.app_vao) {
		glDrawArraysInstanced(mode, first, count, instances);
		return;
	}
	lgl_draw_arrays(mode, first, count, 0, NULL, instances, 0, 0, 0);
}

void GPU_lgl_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices,
                                   GLsizei instances)
{
	if (!GPU_lgl_on || lgl.app_vao) {
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
	float verts[4][VF];
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
		fixed[i].stride = (GLsizei)sizeof(float) * VF;
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
	glUseProgram(app_program);
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
	glGetIntegerv(GL_VIEWPORT, r_vp);
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
	glBindVertexArray(vao ? vao : lgl_context()->vao);
}

void GPU_lgl_DeleteVertexArrays(GLsizei n, const GLuint *arrays)
{
	if (GPU_lgl_on) {
		for (GLsizei i = 0; i < n; i++) {
			if (arrays[i] && arrays[i] == lgl.app_vao) {
				lgl.app_vao = 0;
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
			glBindVertexArray(lc->vao);
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
		glBindVertexArray(lgl_context()->vao);
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
	if (lgl_ubo) {
		glDeleteBuffers(1, &lgl_ubo);
		lgl_ubo = 0;
	}
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
}
