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

/** \file blender/gpu/intern/gpu_legacy_raster.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.26: the old OpenGL's raster operations on a core context
 * (the raster position, bitmaps, glDrawPixels with zoom and pixel transfer:
 * textured quads), display lists (the calls recorded and played back),
 * one-dimensional evaluators, and the selection mode (nothing is recorded:
 * the selection code uses occlusion queries or depth picking).
 */

#define GPU_LEGACY_NO_REDIRECT

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "GPU_glew.h"
#include "GPU_layer.h"

#include "gpu_legacy_intern.h"

/* ------------------------------------------------------------------------ */
/* the raster position */

static void window_to_clip(const int vp[4], float x, float y, float z, float r[4])
{
	r[0] = (x - (float)vp[0]) / (float)vp[2] * 2.0f - 1.0f;
	r[1] = (y - (float)vp[1]) / (float)vp[3] * 2.0f - 1.0f;
	r[2] = z * 2.0f - 1.0f;
	r[3] = 1.0f;
}

void lgl_rasterpos4f(float x, float y, float z, float w)
{
	const float f[4] = {x, y, z, w};
	LGL_RECORD(LOP_RASTERPOS, NULL, 0, f, 4);
	float mvp[16];
	int vp[4];
	lgl_current_mvp(mvp);
	lgl_viewport(vp);
	float c[4];
	for (int k = 0; k < 4; k++) {
		c[k] = mvp[k] * x + mvp[4 + k] * y + mvp[8 + k] * z + mvp[12 + k] * w;
	}
	const float cw = c[3];
	lgl.raster_valid = (cw > 0.0f && c[0] >= -cw && c[0] <= cw && c[1] >= -cw && c[1] <= cw && c[2] >= -cw &&
	                    c[2] <= cw);
	if (cw != 0.0f) {
		lgl.raster[0] = vp[0] + (c[0] / cw * 0.5f + 0.5f) * vp[2];
		lgl.raster[1] = vp[1] + (c[1] / cw * 0.5f + 0.5f) * vp[3];
		lgl.raster[2] = c[2] / cw * 0.5f + 0.5f;
	}
	lgl.raster[3] = cw;
	memcpy(lgl.raster_color, lgl.color, sizeof(float[4]));
	memcpy(lgl.raster_tex, lgl.tex[0], sizeof(float[4]));
}

/* ------------------------------------------------------------------------ */
/* the emulation's pixel textures (grown as needed) */

typedef struct LglPxTex {
	GLuint tex;
	GLenum internal;
	int w, h;
} LglPxTex;

static LglPxTex lgl_px[3] = {{0, GL_RGBA8, 0, 0}, {0, GL_RGBA32F, 0, 0}, {0, GL_R8, 0, 0}};

static LglPxTex *px_texture(int which, int w, int h)
{
	LglPxTex *p = &lgl_px[which];
	if (p->tex && p->w >= w && p->h >= h) {
		return p;
	}
	int tw = p->w > 64 ? p->w : 64, th = p->h > 64 ? p->h : 64;
	while (tw < w) tw *= 2;
	while (th < h) th *= 2;
	if (p->tex) {
		glDeleteTextures(1, &p->tex);
	}
	glCreateTextures(GL_TEXTURE_2D, 1, &p->tex);
	glTextureStorage2D(p->tex, 1, p->internal, tw, th);
	glTextureParameteri(p->tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTextureParameteri(p->tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTextureParameteri(p->tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(p->tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	p->w = tw;
	p->h = th;
	return p;
}

static void px_quad(int mode, const LglPxTex *p, float x0, float y0, float x1, float y1, int w, int h,
                    const float color[4])
{
	int vp[4];
	lgl_viewport(vp);
	float corners[4][4];
	window_to_clip(vp, x0, y0, lgl.raster[2], corners[0]);
	window_to_clip(vp, x1, y0, lgl.raster[2], corners[1]);
	window_to_clip(vp, x1, y1, lgl.raster[2], corners[2]);
	window_to_clip(vp, x0, y1, lgl.raster[2], corners[3]);
	const float u = (float)w / (float)p->w, v = (float)h / (float)p->h;
	const float uv[4][2] = {{0.0f, 0.0f}, {u, 0.0f}, {u, v}, {0.0f, v}};
	lgl_draw_quad_pixels(mode, p->tex, (const float (*)[4])corners, uv, color);
}

void GPU_lgl_Bitmap(GLsizei w, GLsizei h, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove,
                    const GLubyte *bitmap)
{
	if (!lgl.raster_valid) {
		return;
	}
	if (lgl.list) {
		lgl_note_once("glBitmap in a display list is drawn at once");
	}
	if (w > 0 && h > 0 && bitmap) {
		GLint align = 4, row_length = 0, skip_rows = 0, skip_pixels = 0, lsb = 0;
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
		glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
		glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
		glGetIntegerv(GL_UNPACK_LSB_FIRST, &lsb);
		const int width = row_length > 0 ? row_length : w;
		int row_bytes = (width + 7) / 8;
		if (align > 1) {
			row_bytes = (row_bytes + align - 1) / align * align;
		}
		unsigned char *img = MEM_mallocN((size_t)w * (size_t)h, "lgl bitmap");
		for (int y = 0; y < h; y++) {
			const unsigned char *row = bitmap + (size_t)(y + skip_rows) * (size_t)row_bytes;
			for (int x = 0; x < w; x++) {
				const int bx = x + skip_pixels;
				const unsigned char byte = row[bx >> 3];
				const int bit = lsb ? (byte >> (bx & 7)) & 1 : (byte >> (7 - (bx & 7))) & 1;
				img[y * w + x] = bit ? 255 : 0;
			}
		}
		LglPxTex *p = px_texture(2, w, h);
		GLint ub = 0;
		glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &ub);
		if (ub) {
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
		glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
		glTextureSubImage2D(p->tex, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, img);
		glPixelStorei(GL_UNPACK_ALIGNMENT, align);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
		glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
		glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
		if (ub) {
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)ub);
		}
		MEM_freeN(img);
		const float x0 = floorf(lgl.raster[0] - xorig), y0 = floorf(lgl.raster[1] - yorig);
		px_quad(2, p, x0, y0, x0 + (float)w, y0 + (float)h, w, h, lgl.raster_color);
	}
	lgl.raster[0] += xmove;
	lgl.raster[1] += ymove;
}

void GPU_lgl_DrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels)
{
	if (!lgl.raster_valid || w <= 0 || h <= 0) {
		return;
	}
	if (lgl.list) {
		lgl_note_once("glDrawPixels in a display list is drawn at once");
	}
	if (format == GL_DEPTH_COMPONENT || format == GL_STENCIL_INDEX || format == GL_COLOR_INDEX ||
	    format == GL_DEPTH_STENCIL)
	{
		lgl_note_once("glDrawPixels of depth, stencil or colour indices is not emulated");
		return;
	}
	const int bytes = (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT_8_8_8_8 ||
	                   type == GL_UNSIGNED_INT_8_8_8_8_REV);
	LglPxTex *p = px_texture(bytes ? 0 : 1, w, h);
	GLenum fmt = format;
	GLint swz[4] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	switch (format) {
		case GL_LUMINANCE: fmt = GL_RED; swz[1] = swz[2] = GL_RED; swz[3] = GL_ONE; break;
		case GL_LUMINANCE_ALPHA: fmt = GL_RG; swz[1] = swz[2] = GL_RED; swz[3] = GL_GREEN; break;
		case GL_ALPHA: fmt = GL_RED; swz[0] = swz[1] = swz[2] = GL_ZERO; swz[3] = GL_RED; break;
		case GL_RED: swz[1] = swz[2] = GL_ZERO; swz[3] = GL_ONE; break;
		case GL_GREEN: fmt = GL_RED; swz[0] = swz[2] = GL_ZERO; swz[1] = GL_RED; swz[3] = GL_ONE; break;
		case GL_BLUE: fmt = GL_RED; swz[0] = swz[1] = GL_ZERO; swz[2] = GL_RED; swz[3] = GL_ONE; break;
		case GL_RGB: case GL_BGR: swz[3] = GL_ONE; break;
		default: break;
	}
	glTextureParameteriv(p->tex, GL_TEXTURE_SWIZZLE_RGBA, swz);
	glTextureSubImage2D(p->tex, 0, 0, 0, w, h, fmt, type, pixels);
	const float x0 = lgl.raster[0], y0 = lgl.raster[1];
	px_quad(1, p, x0, y0, x0 + (float)w * lgl.zoom[0], y0 + (float)h * lgl.zoom[1], w, h, lgl.raster_color);
}

void GPU_lgl_PixelTransferf(GLenum pname, GLfloat v)
{
	switch (pname) {
		case GL_RED_SCALE: lgl.px_scale[0] = v; break;
		case GL_GREEN_SCALE: lgl.px_scale[1] = v; break;
		case GL_BLUE_SCALE: lgl.px_scale[2] = v; break;
		case GL_ALPHA_SCALE: lgl.px_scale[3] = v; break;
		case GL_RED_BIAS: lgl.px_bias[0] = v; break;
		case GL_GREEN_BIAS: lgl.px_bias[1] = v; break;
		case GL_BLUE_BIAS: lgl.px_bias[2] = v; break;
		case GL_ALPHA_BIAS: lgl.px_bias[3] = v; break;
		case GL_DEPTH_SCALE: lgl.depth_scale = v; break;
		case GL_DEPTH_BIAS: lgl.depth_bias = v; break;
		case GL_MAP_COLOR:
			lgl.map_color = (v != 0.0f);
			if (lgl.map_color) {
				lgl_note_once("glPixelTransfer GL_MAP_COLOR is not emulated");
			}
			break;
		default: break;
	}
	lgl_dirty();
}

void GPU_lgl_PixelTransferi(GLenum pname, GLint v)
{
	GPU_lgl_PixelTransferf(pname, (float)v);
}

void GPU_lgl_PixelZoom(GLfloat x, GLfloat y)
{
	lgl.zoom[0] = x;
	lgl.zoom[1] = y;
}

/* ------------------------------------------------------------------------ */
/* display lists */

typedef struct LglCmd {
	unsigned short op;
	unsigned char ne, nf;
	unsigned int e[2];
	int foff;
} LglCmd;

typedef struct LglList {
	LglCmd *cmds;
	int ncmds, capcmds;
	float *fl;
	int nfl, capfl;
	int used;
} LglList;

static LglList *lgl_lists = NULL;
static unsigned int lgl_nlists = 0;  /* names given: 1..lgl_nlists-1 */
static LglList lgl_compiling;

static LglList *list_get(unsigned int name)
{
	return (name && name < lgl_nlists && lgl_lists[name].used) ? &lgl_lists[name] : NULL;
}

static void list_clear(LglList *l)
{
	if (l->cmds) MEM_freeN(l->cmds);
	if (l->fl) MEM_freeN(l->fl);
	memset(l, 0, sizeof(*l));
}

int lgl_list_record_begin(int op, const unsigned int *e, int ne, const float *f, int nf)
{
	LglList *l = lgl.list;
	if (l->ncmds == l->capcmds) {
		l->capcmds = l->capcmds ? l->capcmds * 2 : 64;
		l->cmds = MEM_reallocN_id(l->cmds, sizeof(LglCmd) * (size_t)l->capcmds, "lgl list");
	}
	if (l->nfl + nf > l->capfl) {
		while (l->nfl + nf > l->capfl) {
			l->capfl = l->capfl ? l->capfl * 2 : 256;
		}
		l->fl = MEM_reallocN_id(l->fl, sizeof(float) * (size_t)l->capfl, "lgl list values");
	}
	LglCmd *c = &l->cmds[l->ncmds++];
	c->op = (unsigned short)op;
	c->ne = (unsigned char)ne;
	c->nf = (unsigned char)nf;
	c->e[0] = ne > 0 ? e[0] : 0;
	c->e[1] = ne > 1 ? e[1] : 0;
	c->foff = l->nfl;
	if (nf) {
		memcpy(l->fl + l->nfl, f, sizeof(float) * (size_t)nf);
		l->nfl += nf;
	}
	return lgl.list_mode == GL_COMPILE;
}

GLuint GPU_lgl_GenLists(GLsizei range)
{
	if (range <= 0) {
		return 0;
	}
	if (lgl_nlists == 0) {
		lgl_nlists = 1;
	}
	const unsigned int first = lgl_nlists;
	lgl_nlists += (unsigned int)range;
	lgl_lists = MEM_recallocN_id(lgl_lists, sizeof(LglList) * lgl_nlists, "lgl lists");
	for (unsigned int i = first; i < lgl_nlists; i++) {
		lgl_lists[i].used = 1;
	}
	return first;
}

void GPU_lgl_DeleteLists(GLuint list, GLsizei range)
{
	for (GLsizei i = 0; i < range; i++) {
		LglList *l = list_get(list + (unsigned int)i);
		if (l) {
			list_clear(l);
		}
	}
}

GLboolean GPU_lgl_IsList(GLuint list)
{
	return list_get(list) ? GL_TRUE : GL_FALSE;
}

void GPU_lgl_NewList(GLuint list, GLenum mode)
{
	if (lgl.list) {
		lgl_note_once("glNewList inside glNewList");
		return;
	}
	if (list >= lgl_nlists) {
		/* a name glGenLists did not give: made room for */
		const unsigned int old = lgl_nlists ? lgl_nlists : 1;
		lgl_nlists = list + 1;
		lgl_lists = MEM_recallocN_id(lgl_lists, sizeof(LglList) * lgl_nlists, "lgl lists");
		(void)old;
	}
	memset(&lgl_compiling, 0, sizeof(lgl_compiling));
	lgl.list = (struct LglList *)&lgl_compiling;
	lgl.list_name = list;
	lgl.list_mode = mode;
}

void GPU_lgl_EndList(void)
{
	if (!lgl.list) {
		return;
	}
	LglList *dst = &lgl_lists[lgl.list_name];
	list_clear(dst);
	*dst = lgl_compiling;
	dst->used = 1;
	memset(&lgl_compiling, 0, sizeof(lgl_compiling));
	lgl.list = NULL;
}

static void list_play(const LglList *l)
{
	if (lgl.list_depth >= 64) {
		return;
	}
	lgl.list_depth++;
	/* while playing back nothing is recorded again */
	struct LglList *compiling = lgl.list;
	lgl.list = NULL;
	for (int i = 0; i < l->ncmds; i++) {
		const LglCmd *c = &l->cmds[i];
		const float *f = l->fl + c->foff;
		switch (c->op) {
			case LOP_BEGIN: GPU_lgl_Begin(c->e[0]); break;
			case LOP_END: GPU_lgl_End(); break;
			case LOP_VERTEX: lgl_vertex4f(f[0], f[1], f[2], f[3]); break;
			case LOP_COLOR:
				if (c->ne && c->e[0]) {
					/* recorded from bytes */
					lgl_color4ub((unsigned char)(f[0] * 255.0f + 0.5f), (unsigned char)(f[1] * 255.0f + 0.5f),
					             (unsigned char)(f[2] * 255.0f + 0.5f), (unsigned char)(f[3] * 255.0f + 0.5f));
				}
				else {
					lgl_color4f(f[0], f[1], f[2], f[3]);
				}
				break;
			case LOP_NORMAL: lgl_normal3f(f[0], f[1], f[2]); break;
			case LOP_TEXCOORD: lgl_texcoord4f((int)c->e[0], f[0], f[1], f[2], f[3]); break;
			case LOP_ATTRIB: lgl_attrib4f(c->e[0], f[0], f[1], f[2], f[3]); break;
			case LOP_ENABLE: GPU_lgl_Enable(c->e[0]); break;
			case LOP_DISABLE: GPU_lgl_Disable(c->e[0]); break;
			case LOP_MATRIXMODE: GPU_lgl_MatrixMode(c->e[0]); break;
			case LOP_LOADIDENTITY: GPU_lgl_LoadIdentity(); break;
			case LOP_LOADMATRIX: GPU_lgl_LoadMatrixf(f); break;
			case LOP_MULTMATRIX: GPU_lgl_MultMatrixf(f); break;
			case LOP_PUSHMATRIX: GPU_lgl_PushMatrix(); break;
			case LOP_POPMATRIX: GPU_lgl_PopMatrix(); break;
			case LOP_MATERIAL: GPU_lgl_Materialfv(c->e[0], c->e[1], f); break;
			case LOP_LIGHT: GPU_lgl_Lightfv(c->e[0], c->e[1], f); break;
			case LOP_LIGHTMODEL: GPU_lgl_LightModelfv(c->e[1], f); break;
			case LOP_SHADEMODEL: GPU_lgl_ShadeModel(c->e[0]); break;
			case LOP_CALLLIST: GPU_lgl_CallList(c->e[0]); break;
			case LOP_BINDTEXTURE: GPU_lgl_BindTexture(c->e[0], c->e[1]); break;
			case LOP_COLORMATERIAL: GPU_lgl_ColorMaterial(c->e[0], c->e[1]); break;
			case LOP_LINESTIPPLE: GPU_lgl_LineStipple((GLint)c->e[0], (GLushort)c->e[1]); break;
			case LOP_POLYGONMODE: GPU_lgl_PolygonMode(c->e[0], c->e[1]); break;
			case LOP_TEXENV: GPU_lgl_TexEnvfv(c->e[0], c->e[1], f); break;
			case LOP_RASTERPOS: lgl_rasterpos4f(f[0], f[1], f[2], f[3]); break;
			case LOP_ALPHAFUNC: GPU_lgl_AlphaFunc(c->e[0], f[0]); break;
			case LOP_PUSHATTRIB: GPU_lgl_PushAttrib(c->e[0]); break;
			case LOP_POPATTRIB: GPU_lgl_PopAttrib(); break;
			case LOP_FOG: GPU_lgl_Fogfv(c->e[0], f); break;
			case LOP_TEXGEN: GPU_lgl_TexGenfv(c->e[0], c->e[1], f); break;
			case LOP_POLYGONSTIPPLE: GPU_lgl_PolygonStipple((const GLubyte *)f); break;
			case LOP_CLIPPLANE:
			{
				const double eq[4] = {f[0], f[1], f[2], f[3]};
				GPU_lgl_ClipPlane(c->e[0], eq);
				break;
			}
			default: break;
		}
	}
	lgl.list = compiling;
	lgl.list_depth--;
}

void GPU_lgl_CallList(GLuint list)
{
	const unsigned int e = list;
	LGL_RECORD(LOP_CALLLIST, &e, 1, NULL, 0);
	const LglList *l = list_get(list);
	if (l) {
		list_play(l);
	}
}

void GPU_lgl_CallLists(GLsizei n, GLenum type, const void *lists)
{
	for (GLsizei i = 0; i < n; i++) {
		unsigned int id;
		switch (type) {
			case GL_BYTE: id = (unsigned int)((const signed char *)lists)[i]; break;
			case GL_UNSIGNED_BYTE: id = ((const unsigned char *)lists)[i]; break;
			case GL_SHORT: id = (unsigned int)((const short *)lists)[i]; break;
			case GL_UNSIGNED_SHORT: id = ((const unsigned short *)lists)[i]; break;
			case GL_INT: id = (unsigned int)((const int *)lists)[i]; break;
			case GL_FLOAT: id = (unsigned int)((const float *)lists)[i]; break;
			default: id = ((const unsigned int *)lists)[i]; break;
		}
		GPU_lgl_CallList(lgl.list_base + id);
	}
}

void GPU_lgl_ListBase(GLuint base)
{
	lgl.list_base = base;
}

void lgl_lists_free(void)
{
	for (unsigned int i = 0; i < lgl_nlists; i++) {
		list_clear(&lgl_lists[i]);
	}
	if (lgl_lists) {
		MEM_freeN(lgl_lists);
		lgl_lists = NULL;
	}
	lgl_nlists = 0;
	list_clear(&lgl_compiling);
	lgl.list = NULL;
	for (int i = 0; i < 3; i++) {
		if (lgl_px[i].tex) {
			glDeleteTextures(1, &lgl_px[i].tex);
			lgl_px[i].tex = 0;
			lgl_px[i].w = lgl_px[i].h = 0;
		}
	}
	lgl_state_free();
}

/* ------------------------------------------------------------------------ */
/* one-dimensional evaluators (Bezier curves) */

void GPU_lgl_Map1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points)
{
	if (target != GL_MAP1_VERTEX_3 && target != GL_MAP1_VERTEX_4) {
		lgl_note_once("glMap1 is emulated for vertices only");
		return;
	}
	const int size = (target == GL_MAP1_VERTEX_3) ? 3 : 4;
	if (order < 1 || order > 64) {
		return;
	}
	lgl.map1_u1 = u1;
	lgl.map1_u2 = u2;
	lgl.map1_order = order;
	lgl.map1_size = size;
	for (int i = 0; i < order; i++) {
		for (int k = 0; k < 4; k++) {
			lgl.map1_points[i * 4 + k] = (k < size) ? points[i * stride + k] : 1.0f;
		}
	}
}

void GPU_lgl_Map1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points)
{
	float pts[64 * 4];
	const int size = (target == GL_MAP1_VERTEX_3) ? 3 : 4;
	if (order < 1 || order > 64) {
		return;
	}
	for (int i = 0; i < order; i++) {
		for (int k = 0; k < size; k++) {
			pts[i * size + k] = (float)points[i * stride + k];
		}
	}
	GPU_lgl_Map1f(target, (float)u1, (float)u2, size, order, pts);
}

void lgl_evalcoord1f(float u)
{
	if (!(lgl.en & (E_MAP1_VERTEX_3 | E_MAP1_VERTEX_4)) || lgl.map1_order < 1) {
		return;
	}
	const float range = lgl.map1_u2 - lgl.map1_u1;
	const float t = (range != 0.0f) ? (u - lgl.map1_u1) / range : 0.0f;
	float p[64 * 4];
	const int n = lgl.map1_order;
	memcpy(p, lgl.map1_points, sizeof(float) * 4 * (size_t)n);
	for (int r = 1; r < n; r++) {
		for (int i = 0; i < n - r; i++) {
			for (int k = 0; k < 4; k++) {
				p[i * 4 + k] = p[i * 4 + k] * (1.0f - t) + p[(i + 1) * 4 + k] * t;
			}
		}
	}
	if (lgl.map1_size == 3) {
		lgl_vertex4f(p[0], p[1], p[2], 1.0f);
	}
	else {
		lgl_vertex4f(p[0], p[1], p[2], p[3]);
	}
}

void GPU_lgl_MapGrid1f(GLint n, GLfloat u1, GLfloat u2)
{
	lgl.grid1_n = n;
	lgl.grid1_u1 = u1;
	lgl.grid1_u2 = u2;
}

void GPU_lgl_MapGrid1d(GLint n, GLdouble u1, GLdouble u2)
{
	GPU_lgl_MapGrid1f(n, (float)u1, (float)u2);
}

static float grid1_u(int i)
{
	if (lgl.grid1_n <= 0) {
		return lgl.grid1_u1;
	}
	return lgl.grid1_u1 + (float)i * (lgl.grid1_u2 - lgl.grid1_u1) / (float)lgl.grid1_n;
}

void GPU_lgl_EvalPoint1(GLint i)
{
	lgl_evalcoord1f(grid1_u(i));
}

void GPU_lgl_EvalMesh1(GLenum mode, GLint i1, GLint i2)
{
	GPU_lgl_Begin(mode == GL_POINT ? GL_POINTS : GL_LINE_STRIP);
	for (GLint i = i1; i <= i2; i++) {
		lgl_evalcoord1f(grid1_u(i));
	}
	GPU_lgl_End();
}

/* ------------------------------------------------------------------------ */
/* selection: the old GL_SELECT mode records nothing here */

GLint GPU_lgl_RenderMode(GLenum mode)
{
	if (mode != GL_RENDER) {
		lgl_note_once("glRenderMode(GL_SELECT/GL_FEEDBACK) is not emulated: no hits are given");
	}
	lgl.render_mode = mode;
	return 0;
}

void GPU_lgl_SelectBuffer(GLsizei size, GLuint *buffer)
{
	(void)size;
	(void)buffer;
}

void GPU_lgl_InitNames(void)
{
}

void GPU_lgl_LoadName(GLuint name)
{
	(void)name;
}

void GPU_lgl_PushName(GLuint name)
{
	(void)name;
}

void GPU_lgl_PopName(void)
{
}
