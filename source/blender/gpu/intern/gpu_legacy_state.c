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

/** \file blender/gpu/intern/gpu_legacy_state.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.26: the old OpenGL's state on a core context: enables and
 * queries, the matrix stacks (the drawing layer's), lamps, material, fog,
 * texture environments and coordinate generation, stipples and clip planes,
 * the old texture formats, and the attribute stacks.
 */

#define GPU_LEGACY_NO_REDIRECT

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "GPU_glew.h"
#include "GPU_layer.h"

#include "gpu_private.h"
#include "gpu_legacy_intern.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------------ */
/* small matrix helpers: column-major float[16] */

static void m16_identity(float *m)
{
	memset(m, 0, sizeof(float[16]));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void m16_mul(float *r, const float *a, const float *b)
{
	float t[16];
	for (int c = 0; c < 4; c++) {
		for (int row = 0; row < 4; row++) {
			t[c * 4 + row] = a[row] * b[c * 4] + a[4 + row] * b[c * 4 + 1] + a[8 + row] * b[c * 4 + 2] +
			                 a[12 + row] * b[c * 4 + 3];
		}
	}
	memcpy(r, t, sizeof(t));
}

static int m16_invert(float *r, const float *m)
{
	float inv[4][4];
	const int ok = gpu_layer_m4_invert(inv, (const float (*)[4])m);
	memcpy(r, inv, sizeof(inv));
	return ok;
}

static void m16_from_d(float r[16], const double *m)
{
	for (int i = 0; i < 16; i++) {
		r[i] = (float)m[i];
	}
}

static void m16_transpose(float *r, const float *m)
{
	for (int c = 0; c < 4; c++) {
		for (int row = 0; row < 4; row++) {
			r[c * 4 + row] = m[row * 4 + c];
		}
	}
}

/* a plane in object space to eye space: p' = p * inverse(modelview) */
static void plane_to_eye(const double p[4], double r[4])
{
	float mv[16], inv[16];
	GPU_matrix_get(GPU_MATRIX_MODELVIEW, (float (*)[4])mv);
	m16_invert(inv, mv);
	for (int c = 0; c < 4; c++) {
		r[c] = p[0] * inv[c * 4 + 0] + p[1] * inv[c * 4 + 1] + p[2] * inv[c * 4 + 2] + p[3] * inv[c * 4 + 3];
	}
}

void lgl_matrix_changed(void)
{
	/* the draws compare the matrices themselves */
}

/* ------------------------------------------------------------------------ */
/* defaults */

static void unit_defaults(LglUnit *u)
{
	memset(u, 0, sizeof(*u));
	u->env_mode = GL_MODULATE;
	u->comb_rgb = u->comb_alpha = GL_MODULATE;
	u->src_rgb[0] = u->src_alpha[0] = GL_TEXTURE;
	u->src_rgb[1] = u->src_alpha[1] = GL_PREVIOUS;
	u->src_rgb[2] = u->src_alpha[2] = GL_CONSTANT;
	u->op_rgb[0] = u->op_rgb[1] = GL_SRC_COLOR;
	u->op_rgb[2] = GL_SRC_ALPHA;
	u->op_alpha[0] = u->op_alpha[1] = u->op_alpha[2] = GL_SRC_ALPHA;
	u->rgb_scale = u->alpha_scale = 1.0f;
	for (int k = 0; k < 4; k++) {
		u->gen_mode[k] = GL_EYE_LINEAR;
	}
	u->gen_obj[0][0] = u->gen_eye[0][0] = 1.0f;
	u->gen_obj[1][1] = u->gen_eye[1][1] = 1.0f;
	for (int i = 0; i < LGL_TEXSTACK; i++) {
		m16_identity(&u->texmat[i][0][0]);
	}
}

void lgl_state_defaults(void)
{
	for (int k = 0; k < 4; k++) {
		lgl.color[k] = 1.0f;
	}
	lgl.normal[2] = 1.0f;
	for (int u = 0; u < LGL_UNITS; u++) {
		lgl.tex[u][3] = 1.0f;
		unit_defaults(&lgl.unit[u]);
	}
	for (int g = 0; g < LGL_GENERIC; g++) {
		lgl.generic[g][3] = 1.0f;
	}
	lgl.edge = 1.0f;
	for (int i = 0; i < LGL_LIGHTS; i++) {
		LglLight *l = &lgl.lights[i];
		memset(l, 0, sizeof(*l));
		l->amb[3] = l->dif[3] = l->spec[3] = 1.0f;
		l->pos[2] = 1.0f;
		l->spot_dir[2] = -1.0f;
		l->spot_cut = 180.0f;
		l->att[0] = 1.0f;
		if (i == 0) {
			for (int k = 0; k < 4; k++) {
				l->dif[k] = l->spec[k] = 1.0f;
			}
		}
	}
	for (int f = 0; f < 2; f++) {
		LglMaterial *m = &lgl.mat[f];
		const float amb[4] = {0.2f, 0.2f, 0.2f, 1.0f}, dif[4] = {0.8f, 0.8f, 0.8f, 1.0f};
		const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		memcpy(m->amb, amb, sizeof(amb));
		memcpy(m->dif, dif, sizeof(dif));
		memcpy(m->spec, black, sizeof(black));
		memcpy(m->emi, black, sizeof(black));
		m->shin = 0.0f;
	}
	lgl.scene_amb[0] = lgl.scene_amb[1] = lgl.scene_amb[2] = 0.2f;
	lgl.scene_amb[3] = 1.0f;
	lgl.cm_face = GL_FRONT_AND_BACK;
	lgl.cm_mode = GL_AMBIENT_AND_DIFFUSE;
	lgl.shade_model = GL_SMOOTH;
	lgl.alpha_func = GL_ALWAYS;
	lgl.fog_mode = GL_EXP;
	lgl.fog_density = 1.0f;
	lgl.fog_end = 1.0f;
	lgl.line_factor = 1;
	lgl.line_pattern = 0xffff;
	memset(lgl.poly_stipple, 0xff, sizeof(lgl.poly_stipple));
	lgl.matrix_mode = GL_MODELVIEW;
	lgl.poly_mode[0] = lgl.poly_mode[1] = GL_FILL;
	for (int k = 0; k < 4; k++) {
		lgl.px_scale[k] = 1.0f;
		lgl.raster_color[k] = 1.0f;
	}
	lgl.depth_scale = 1.0f;
	lgl.zoom[0] = lgl.zoom[1] = 1.0f;
	lgl.raster[3] = 1.0f;
	lgl.raster_valid = 1;
	lgl.raster_tex[3] = 1.0f;
	lgl.render_mode = GL_RENDER;
	lgl.dirty = 1;
}

/* ------------------------------------------------------------------------ */
/* the colour material follows the colour */

static void color_material_apply(void)
{
	for (int f = 0; f < 2; f++) {
		if (!((lgl.cm_face == GL_FRONT_AND_BACK) || (lgl.cm_face == GL_FRONT && f == 0) ||
		      (lgl.cm_face == GL_BACK && f == 1)))
		{
			continue;
		}
		LglMaterial *m = &lgl.mat[f];
		switch (lgl.cm_mode) {
			case GL_AMBIENT: memcpy(m->amb, lgl.color, sizeof(float[4])); break;
			case GL_DIFFUSE: memcpy(m->dif, lgl.color, sizeof(float[4])); break;
			case GL_SPECULAR: memcpy(m->spec, lgl.color, sizeof(float[4])); break;
			case GL_EMISSION: memcpy(m->emi, lgl.color, sizeof(float[4])); break;
			default:
				memcpy(m->amb, lgl.color, sizeof(float[4]));
				memcpy(m->dif, lgl.color, sizeof(float[4]));
				break;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* enables */

int lgl_cap_enable(unsigned int cap, int on)
{
	unsigned int bit = 0;
	LglUnit *u = &lgl.unit[(lgl.active_unit >= 0 && lgl.active_unit < LGL_UNITS) ? lgl.active_unit : 0];
	switch (cap) {
		case GL_LIGHTING: bit = E_LIGHTING; break;
		case GL_COLOR_MATERIAL: bit = E_COLOR_MATERIAL; break;
		case GL_NORMALIZE: bit = E_NORMALIZE; break;
		case GL_RESCALE_NORMAL: bit = E_RESCALE_NORMAL; break;
		case GL_FOG: bit = E_FOG; break;
		case GL_ALPHA_TEST: bit = E_ALPHA_TEST; break;
		case GL_LINE_STIPPLE: bit = E_LINE_STIPPLE; break;
		case GL_POLYGON_STIPPLE: bit = E_POLYGON_STIPPLE; break;
		case GL_POINT_SMOOTH: bit = E_POINT_SMOOTH; break;
		case GL_MAP1_VERTEX_3: bit = E_MAP1_VERTEX_3; break;
		case GL_MAP1_VERTEX_4: bit = E_MAP1_VERTEX_4; break;
		case GL_AUTO_NORMAL: bit = E_AUTO_NORMAL; break;
		case GL_COLOR_SUM: bit = E_COLOR_SUM; break;
		case GL_POINT_SPRITE: bit = E_POINT_SPRITE; break;
		case GL_INDEX_LOGIC_OP: bit = E_LOGIC_INDEX; break;
		case GL_MAP1_COLOR_4: case GL_MAP1_INDEX: case GL_MAP1_NORMAL: case GL_MAP1_TEXTURE_COORD_1:
		case GL_MAP1_TEXTURE_COORD_2: case GL_MAP1_TEXTURE_COORD_3: case GL_MAP1_TEXTURE_COORD_4:
		case GL_MAP2_COLOR_4: case GL_MAP2_INDEX: case GL_MAP2_NORMAL: case GL_MAP2_TEXTURE_COORD_1:
		case GL_MAP2_TEXTURE_COORD_2: case GL_MAP2_TEXTURE_COORD_3: case GL_MAP2_TEXTURE_COORD_4:
		case GL_MAP2_VERTEX_3: case GL_MAP2_VERTEX_4:
			return 1;
		case GL_TEXTURE_1D: case GL_TEXTURE_2D: case GL_TEXTURE_3D: case GL_TEXTURE_CUBE_MAP:
		case GL_TEXTURE_RECTANGLE: case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R:
		case GL_TEXTURE_GEN_Q:
		{
			const int ub = (cap == GL_TEXTURE_1D) ? U_1D : (cap == GL_TEXTURE_2D) ? U_2D : (cap == GL_TEXTURE_3D) ? U_3D :
			               (cap == GL_TEXTURE_CUBE_MAP) ? U_CUBE : (cap == GL_TEXTURE_RECTANGLE) ? U_RECT :
			               (cap == GL_TEXTURE_GEN_S) ? U_GEN_S : (cap == GL_TEXTURE_GEN_T) ? U_GEN_T :
			               (cap == GL_TEXTURE_GEN_R) ? U_GEN_R : U_GEN_Q;
			if (on) u->en |= ub; else u->en &= ~ub;
			lgl_dirty();
			return 1;
		}
		default:
			break;
	}
	if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + LGL_LIGHTS) {
		const unsigned int b = 1u << (cap - GL_LIGHT0);
		if (on) lgl.light_on |= b; else lgl.light_on &= ~b;
		lgl_dirty();
		return 1;
	}
	if (cap >= GL_CLIP_PLANE0 && cap < GL_CLIP_PLANE0 + LGL_CLIPS) {
		const unsigned int b = 1u << (cap - GL_CLIP_PLANE0);
		if (on) lgl.clip_on |= b; else lgl.clip_on &= ~b;
		lgl_dirty();
		return 0;  /* OpenGL's clip distances too */
	}
	if (bit) {
		if (on) {
			lgl.en |= bit;
			if (bit == E_COLOR_MATERIAL) {
				color_material_apply();
			}
		}
		else {
			lgl.en &= ~bit;
		}
		lgl_dirty();
		return 1;
	}
	return 0;
}

int lgl_cap_get(unsigned int cap, int *r_on)
{
	const LglUnit *u = &lgl.unit[(lgl.active_unit >= 0 && lgl.active_unit < LGL_UNITS) ? lgl.active_unit : 0];
	unsigned int bit = 0;
	switch (cap) {
		case GL_LIGHTING: bit = E_LIGHTING; break;
		case GL_COLOR_MATERIAL: bit = E_COLOR_MATERIAL; break;
		case GL_NORMALIZE: bit = E_NORMALIZE; break;
		case GL_RESCALE_NORMAL: bit = E_RESCALE_NORMAL; break;
		case GL_FOG: bit = E_FOG; break;
		case GL_ALPHA_TEST: bit = E_ALPHA_TEST; break;
		case GL_LINE_STIPPLE: bit = E_LINE_STIPPLE; break;
		case GL_POLYGON_STIPPLE: bit = E_POLYGON_STIPPLE; break;
		case GL_POINT_SMOOTH: bit = E_POINT_SMOOTH; break;
		case GL_MAP1_VERTEX_3: bit = E_MAP1_VERTEX_3; break;
		case GL_MAP1_VERTEX_4: bit = E_MAP1_VERTEX_4; break;
		case GL_AUTO_NORMAL: bit = E_AUTO_NORMAL; break;
		case GL_COLOR_SUM: bit = E_COLOR_SUM; break;
		case GL_POINT_SPRITE: bit = E_POINT_SPRITE; break;
		case GL_INDEX_LOGIC_OP: bit = E_LOGIC_INDEX; break;
		case GL_TEXTURE_1D: *r_on = (u->en & U_1D) != 0; return 1;
		case GL_TEXTURE_2D: *r_on = (u->en & U_2D) != 0; return 1;
		case GL_TEXTURE_3D: *r_on = (u->en & U_3D) != 0; return 1;
		case GL_TEXTURE_CUBE_MAP: *r_on = (u->en & U_CUBE) != 0; return 1;
		case GL_TEXTURE_RECTANGLE: *r_on = (u->en & U_RECT) != 0; return 1;
		case GL_TEXTURE_GEN_S: *r_on = (u->en & U_GEN_S) != 0; return 1;
		case GL_TEXTURE_GEN_T: *r_on = (u->en & U_GEN_T) != 0; return 1;
		case GL_TEXTURE_GEN_R: *r_on = (u->en & U_GEN_R) != 0; return 1;
		case GL_TEXTURE_GEN_Q: *r_on = (u->en & U_GEN_Q) != 0; return 1;
		case GL_VERTEX_ARRAY: *r_on = lgl.arr_pos.enabled; return 1;
		case GL_NORMAL_ARRAY: *r_on = lgl.arr_nor.enabled; return 1;
		case GL_COLOR_ARRAY: *r_on = lgl.arr_col.enabled; return 1;
		case GL_TEXTURE_COORD_ARRAY: *r_on = lgl.arr_tex[lgl.client_unit].enabled; return 1;
		case GL_INDEX_ARRAY: case GL_EDGE_FLAG_ARRAY: *r_on = 0; return 1;
		default: break;
	}
	if (bit) {
		*r_on = (lgl.en & bit) != 0;
		return 1;
	}
	if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + LGL_LIGHTS) {
		*r_on = (lgl.light_on >> (cap - GL_LIGHT0)) & 1;
		return 1;
	}
	if (cap >= GL_MAP1_COLOR_4 && cap <= GL_MAP2_VERTEX_4) {
		*r_on = 0;
		return 1;
	}
	return 0;
}

void GPU_lgl_Enable(GLenum cap)
{
	const unsigned int e = cap;
	LGL_RECORD(LOP_ENABLE, &e, 1, NULL, 0);
	if (!lgl_cap_enable(cap, 1)) {
		glEnable(cap);
	}
}

void GPU_lgl_Disable(GLenum cap)
{
	const unsigned int e = cap;
	LGL_RECORD(LOP_DISABLE, &e, 1, NULL, 0);
	if (!lgl_cap_enable(cap, 0)) {
		glDisable(cap);
	}
}

GLboolean GPU_lgl_IsEnabled(GLenum cap)
{
	int on;
	if (lgl_cap_get(cap, &on)) {
		return on ? GL_TRUE : GL_FALSE;
	}
	return glIsEnabled(cap);
}

/* ------------------------------------------------------------------------ */
/* queries */

static int fb_bits(GLenum pname)
{
	GLint fb = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
	GLenum att, what;
	switch (pname) {
		case GL_RED_BITS: what = GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE; break;
		case GL_GREEN_BITS: what = GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE; break;
		case GL_BLUE_BITS: what = GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE; break;
		case GL_ALPHA_BITS: what = GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE; break;
		case GL_DEPTH_BITS: what = GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE; break;
		default: what = GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE; break;
	}
	if (pname == GL_DEPTH_BITS) {
		att = fb ? GL_DEPTH_ATTACHMENT : GL_DEPTH;
	}
	else if (pname == GL_STENCIL_BITS) {
		att = fb ? GL_STENCIL_ATTACHMENT : GL_STENCIL;
	}
	else {
		att = fb ? GL_COLOR_ATTACHMENT0 : GL_BACK_LEFT;
	}
	GLint type = GL_NONE, v = 0;
	glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, att, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
	if (type == GL_NONE) {
		return 0;
	}
	glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, att, what, &v);
	return v;
}

static int set4(double *v, const float *f, int n)
{
	for (int i = 0; i < n; i++) {
		v[i] = f[i];
	}
	return n;
}

/* the emulation's answer (r_n values), or 0: OpenGL's */
int lgl_get(unsigned int pname, double *v, int *r_n)
{
	int n = 0, on;
	float m[4][4];
	switch (pname) {
		case GL_MODELVIEW_MATRIX:
			GPU_matrix_get(GPU_MATRIX_MODELVIEW, m);
			n = set4(v, &m[0][0], 16);
			break;
		case GL_PROJECTION_MATRIX:
			GPU_matrix_get(GPU_MATRIX_PROJECTION, m);
			n = set4(v, &m[0][0], 16);
			break;
		case GL_TEXTURE_MATRIX:
			GPU_texture_matrix_get(lgl.active_unit, m);
			n = set4(v, &m[0][0], 16);
			break;
		case GL_TRANSPOSE_MODELVIEW_MATRIX: case GL_TRANSPOSE_PROJECTION_MATRIX: case GL_TRANSPOSE_TEXTURE_MATRIX:
		{
			float t[16];
			if (pname == GL_TRANSPOSE_MODELVIEW_MATRIX) GPU_matrix_get(GPU_MATRIX_MODELVIEW, m);
			else if (pname == GL_TRANSPOSE_PROJECTION_MATRIX) GPU_matrix_get(GPU_MATRIX_PROJECTION, m);
			else GPU_texture_matrix_get(lgl.active_unit, m);
			m16_transpose(t, &m[0][0]);
			n = set4(v, t, 16);
			break;
		}
		case GL_MATRIX_MODE: v[0] = lgl.matrix_mode; n = 1; break;
		case GL_MODELVIEW_STACK_DEPTH: case GL_PROJECTION_STACK_DEPTH:
			v[0] = 1; n = 1; break;
		case GL_TEXTURE_STACK_DEPTH: v[0] = lgl.unit[lgl.active_unit].textop + 1; n = 1; break;
		case GL_MAX_MODELVIEW_STACK_DEPTH: case GL_MAX_PROJECTION_STACK_DEPTH: v[0] = 32; n = 1; break;
		case GL_MAX_TEXTURE_STACK_DEPTH: v[0] = LGL_TEXSTACK; n = 1; break;
		case GL_MAX_ATTRIB_STACK_DEPTH: case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH: v[0] = LGL_ATTRIBSTACK; n = 1; break;
		case GL_MAX_LIGHTS: v[0] = LGL_LIGHTS; n = 1; break;
		case GL_MAX_TEXTURE_UNITS: v[0] = LGL_FF_UNITS; n = 1; break;
		case GL_MAX_TEXTURE_COORDS: v[0] = LGL_UNITS; n = 1; break;
		case GL_MAX_LIST_NESTING: v[0] = 64; n = 1; break;
		case GL_MAX_EVAL_ORDER: v[0] = 64; n = 1; break;
		case GL_MAX_PIXEL_MAP_TABLE: v[0] = 256; n = 1; break;
		case GL_MAX_NAME_STACK_DEPTH: v[0] = 64; n = 1; break;
		case GL_CURRENT_COLOR: n = set4(v, lgl.color, 4); break;
		case GL_CURRENT_NORMAL: n = set4(v, lgl.normal, 3); break;
		case GL_CURRENT_TEXTURE_COORDS: n = set4(v, lgl.tex[lgl.active_unit < LGL_UNITS ? lgl.active_unit : 0], 4); break;
		case GL_CURRENT_RASTER_POSITION: n = set4(v, lgl.raster, 4); break;
		case GL_CURRENT_RASTER_POSITION_VALID: v[0] = lgl.raster_valid; n = 1; break;
		case GL_CURRENT_RASTER_COLOR: n = set4(v, lgl.raster_color, 4); break;
		case GL_CURRENT_RASTER_TEXTURE_COORDS: n = set4(v, lgl.raster_tex, 4); break;
		case GL_CURRENT_RASTER_DISTANCE: v[0] = 0; n = 1; break;
		case GL_CURRENT_INDEX: case GL_CURRENT_RASTER_INDEX: v[0] = 0; n = 1; break;
		case GL_EDGE_FLAG: v[0] = lgl.edge; n = 1; break;
		case GL_SHADE_MODEL: v[0] = lgl.shade_model; n = 1; break;
		case GL_ALPHA_TEST_FUNC: v[0] = lgl.alpha_func; n = 1; break;
		case GL_ALPHA_TEST_REF: v[0] = lgl.alpha_ref; n = 1; break;
		case GL_LINE_STIPPLE_PATTERN: v[0] = lgl.line_pattern; n = 1; break;
		case GL_LINE_STIPPLE_REPEAT: v[0] = lgl.line_factor; n = 1; break;
		case GL_LIST_INDEX: v[0] = lgl.list ? lgl.list_name : 0; n = 1; break;
		case GL_LIST_MODE: v[0] = lgl.list ? lgl.list_mode : 0; n = 1; break;
		case GL_LIST_BASE: v[0] = lgl.list_base; n = 1; break;
		case GL_CLIENT_ACTIVE_TEXTURE: v[0] = GL_TEXTURE0 + lgl.client_unit; n = 1; break;
		case GL_ZOOM_X: v[0] = lgl.zoom[0]; n = 1; break;
		case GL_ZOOM_Y: v[0] = lgl.zoom[1]; n = 1; break;
		case GL_RED_SCALE: v[0] = lgl.px_scale[0]; n = 1; break;
		case GL_GREEN_SCALE: v[0] = lgl.px_scale[1]; n = 1; break;
		case GL_BLUE_SCALE: v[0] = lgl.px_scale[2]; n = 1; break;
		case GL_ALPHA_SCALE: v[0] = lgl.px_scale[3]; n = 1; break;
		case GL_RED_BIAS: v[0] = lgl.px_bias[0]; n = 1; break;
		case GL_GREEN_BIAS: v[0] = lgl.px_bias[1]; n = 1; break;
		case GL_BLUE_BIAS: v[0] = lgl.px_bias[2]; n = 1; break;
		case GL_ALPHA_BIAS: v[0] = lgl.px_bias[3]; n = 1; break;
		case GL_DEPTH_SCALE: v[0] = lgl.depth_scale; n = 1; break;
		case GL_DEPTH_BIAS: v[0] = lgl.depth_bias; n = 1; break;
		case GL_MAP_COLOR: v[0] = lgl.map_color; n = 1; break;
		case GL_COLOR_MATERIAL_FACE: v[0] = lgl.cm_face; n = 1; break;
		case GL_COLOR_MATERIAL_PARAMETER: v[0] = lgl.cm_mode; n = 1; break;
		case GL_LIGHT_MODEL_AMBIENT: n = set4(v, lgl.scene_amb, 4); break;
		case GL_LIGHT_MODEL_TWO_SIDE: v[0] = lgl.two_side; n = 1; break;
		case GL_LIGHT_MODEL_LOCAL_VIEWER: v[0] = lgl.local_viewer; n = 1; break;
		case GL_LIGHT_MODEL_COLOR_CONTROL: v[0] = lgl.separate_spec ? GL_SEPARATE_SPECULAR_COLOR : GL_SINGLE_COLOR; n = 1; break;
		case GL_FOG_MODE: v[0] = lgl.fog_mode; n = 1; break;
		case GL_FOG_DENSITY: v[0] = lgl.fog_density; n = 1; break;
		case GL_FOG_START: v[0] = lgl.fog_start; n = 1; break;
		case GL_FOG_END: v[0] = lgl.fog_end; n = 1; break;
		case GL_FOG_INDEX: v[0] = lgl.fog_index; n = 1; break;
		case GL_FOG_COLOR: n = set4(v, lgl.fog_color, 4); break;
		case GL_POLYGON_MODE: v[0] = lgl.poly_mode[0]; v[1] = lgl.poly_mode[1]; n = 2; break;
		case GL_CURRENT_PROGRAM: v[0] = lgl.app_program; n = 1; break;
		case GL_VERTEX_ARRAY_BINDING: v[0] = lgl.app_vao; n = 1; break;
		case GL_ELEMENT_ARRAY_BUFFER_BINDING:
			if (lgl.app_vao) return 0;
			v[0] = lgl.app_ebo; n = 1; break;
		case GL_VERTEX_ARRAY_SIZE: v[0] = lgl.arr_pos.size; n = 1; break;
		case GL_VERTEX_ARRAY_TYPE: v[0] = lgl.arr_pos.type; n = 1; break;
		case GL_VERTEX_ARRAY_STRIDE: v[0] = lgl.arr_pos.stride; n = 1; break;
		case GL_RENDER_MODE: v[0] = lgl.render_mode; n = 1; break;
		case GL_RGBA_MODE: v[0] = 1; n = 1; break;
		case GL_INDEX_MODE: v[0] = 0; n = 1; break;
		case GL_AUX_BUFFERS: case GL_INDEX_BITS: case GL_ACCUM_RED_BITS: case GL_ACCUM_GREEN_BITS:
		case GL_ACCUM_BLUE_BITS: case GL_ACCUM_ALPHA_BITS: case GL_NAME_STACK_DEPTH:
			v[0] = 0; n = 1; break;
		case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: case GL_DEPTH_BITS:
		case GL_STENCIL_BITS:
			v[0] = fb_bits(pname); n = 1; break;
		case GL_PERSPECTIVE_CORRECTION_HINT: case GL_POINT_SMOOTH_HINT: case GL_FOG_HINT:
		case GL_GENERATE_MIPMAP_HINT:
			v[0] = GL_DONT_CARE; n = 1; break;
		case GL_TEXTURE_BINDING_2D: case GL_TEXTURE_BINDING_RECTANGLE: case GL_TEXTURE_BINDING_CUBE_MAP:
			return 0;
		default:
			if (lgl_cap_get(pname, &on)) {
				v[0] = on;
				n = 1;
			}
			break;
	}
	*r_n = n;
	return n > 0;
}

void GPU_lgl_GetDoublev(GLenum pname, GLdouble *params)
{
	double v[16];
	int n;
	if (lgl_get(pname, v, &n)) {
		memcpy(params, v, sizeof(double) * (size_t)n);
		return;
	}
	glGetDoublev(pname, params);
}

void GPU_lgl_GetFloatv(GLenum pname, GLfloat *params)
{
	double v[16];
	int n;
	if (lgl_get(pname, v, &n)) {
		for (int i = 0; i < n; i++) {
			params[i] = (float)v[i];
		}
		return;
	}
	glGetFloatv(pname, params);
}

void GPU_lgl_GetIntegerv(GLenum pname, GLint *params)
{
	double v[16];
	int n;
	if (lgl_get(pname, v, &n)) {
		const int color = (pname == GL_CURRENT_COLOR || pname == GL_CURRENT_RASTER_COLOR ||
		                   pname == GL_LIGHT_MODEL_AMBIENT || pname == GL_FOG_COLOR);
		for (int i = 0; i < n; i++) {
			params[i] = color ? (GLint)(v[i] * 2147483647.0) : (GLint)floor(v[i] + 0.5);
		}
		return;
	}
	glGetIntegerv(pname, params);
}

void GPU_lgl_GetBooleanv(GLenum pname, GLboolean *params)
{
	double v[16];
	int n;
	if (lgl_get(pname, v, &n)) {
		for (int i = 0; i < n; i++) {
			params[i] = (v[i] != 0.0) ? GL_TRUE : GL_FALSE;
		}
		return;
	}
	glGetBooleanv(pname, params);
}

static char *lgl_extensions = NULL;

const GLubyte *GPU_lgl_GetString(GLenum name)
{
	if (name != GL_EXTENSIONS) {
		return glGetString(name);
	}
	if (!lgl_extensions) {
		GLint n = 0;
		glGetIntegerv(GL_NUM_EXTENSIONS, &n);
		size_t len = 1;
		for (GLint i = 0; i < n; i++) {
			const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
			len += (e ? strlen(e) : 0) + 1;
		}
		lgl_extensions = malloc(len);
		lgl_extensions[0] = '\0';
		size_t at = 0;
		for (GLint i = 0; i < n; i++) {
			const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
			if (e) {
				const size_t l = strlen(e);
				memcpy(lgl_extensions + at, e, l);
				at += l;
				lgl_extensions[at++] = ' ';
			}
		}
		lgl_extensions[at] = '\0';
	}
	return (const GLubyte *)lgl_extensions;
}

/* ------------------------------------------------------------------------ */
/* matrices: the drawing layer's stack (modelview, projection); the texture
 * matrices kept here per unit and given to the layer */

static LglUnit *active_unit(void)
{
	return &lgl.unit[(lgl.active_unit >= 0 && lgl.active_unit < LGL_UNITS) ? lgl.active_unit : 0];
}

static void texmat_sync(void)
{
	const int un = (lgl.active_unit >= 0 && lgl.active_unit < LGL_UNITS) ? lgl.active_unit : 0;
	LglUnit *u = &lgl.unit[un];
	GPU_texture_matrix_set(un, (const float (*)[4])u->texmat[u->textop]);
}

void GPU_lgl_MatrixMode(GLenum mode)
{
	const unsigned int e = mode;
	LGL_RECORD(LOP_MATRIXMODE, &e, 1, NULL, 0);
	lgl.matrix_mode = mode;
	if (mode == GL_MODELVIEW) {
		GPU_matrix_mode(GPU_MATRIX_MODELVIEW);
	}
	else if (mode == GL_PROJECTION) {
		GPU_matrix_mode(GPU_MATRIX_PROJECTION);
	}
}

static void mat_load(const float m[16])
{
	if (lgl.matrix_mode == GL_TEXTURE) {
		LglUnit *u = active_unit();
		memcpy(u->texmat[u->textop], m, sizeof(float[16]));
		texmat_sync();
	}
	else if (lgl.matrix_mode == GL_MODELVIEW || lgl.matrix_mode == GL_PROJECTION) {
		GPU_matrix_load((const float (*)[4])m);
	}
}

static void mat_mult(const float m[16])
{
	if (lgl.matrix_mode == GL_TEXTURE) {
		LglUnit *u = active_unit();
		m16_mul(&u->texmat[u->textop][0][0], &u->texmat[u->textop][0][0], m);
		texmat_sync();
	}
	else if (lgl.matrix_mode == GL_MODELVIEW || lgl.matrix_mode == GL_PROJECTION) {
		GPU_matrix_mult((const float (*)[4])m);
	}
}

void GPU_lgl_LoadIdentity(void)
{
	LGL_RECORD(LOP_LOADIDENTITY, NULL, 0, NULL, 0);
	float m[16];
	m16_identity(m);
	mat_load(m);
}

void GPU_lgl_LoadMatrixf(const GLfloat *m)
{
	LGL_RECORD(LOP_LOADMATRIX, NULL, 0, m, 16);
	mat_load(m);
}

void GPU_lgl_LoadMatrixd(const GLdouble *m)
{
	float f[16];
	m16_from_d(f, m);
	GPU_lgl_LoadMatrixf(f);
}

void GPU_lgl_MultMatrixf(const GLfloat *m)
{
	LGL_RECORD(LOP_MULTMATRIX, NULL, 0, m, 16);
	mat_mult(m);
}

void GPU_lgl_MultMatrixd(const GLdouble *m)
{
	float f[16];
	m16_from_d(f, m);
	GPU_lgl_MultMatrixf(f);
}

void GPU_lgl_LoadTransposeMatrixf(const GLfloat *m)
{
	if (!GPU_lgl_on) {
		glLoadTransposeMatrixf(m);
		return;
	}
	float t[16];
	m16_transpose(t, m);
	GPU_lgl_LoadMatrixf(t);
}

void GPU_lgl_LoadTransposeMatrixd(const GLdouble *m)
{
	if (!GPU_lgl_on) {
		glLoadTransposeMatrixd(m);
		return;
	}
	float f[16], t[16];
	m16_from_d(f, m);
	m16_transpose(t, f);
	GPU_lgl_LoadMatrixf(t);
}

void GPU_lgl_MultTransposeMatrixf(const GLfloat *m)
{
	if (!GPU_lgl_on) {
		glMultTransposeMatrixf(m);
		return;
	}
	float t[16];
	m16_transpose(t, m);
	GPU_lgl_MultMatrixf(t);
}

void GPU_lgl_MultTransposeMatrixd(const GLdouble *m)
{
	if (!GPU_lgl_on) {
		glMultTransposeMatrixd(m);
		return;
	}
	float f[16], t[16];
	m16_from_d(f, m);
	m16_transpose(t, f);
	GPU_lgl_MultMatrixf(t);
}

void GPU_lgl_PushMatrix(void)
{
	LGL_RECORD(LOP_PUSHMATRIX, NULL, 0, NULL, 0);
	if (lgl.matrix_mode == GL_TEXTURE) {
		LglUnit *u = active_unit();
		if (u->textop + 1 < LGL_TEXSTACK) {
			memcpy(u->texmat[u->textop + 1], u->texmat[u->textop], sizeof(float[16]));
			u->textop++;
		}
		else {
			lgl_note_once("the texture matrix stack is full");
		}
	}
	else if (lgl.matrix_mode == GL_MODELVIEW || lgl.matrix_mode == GL_PROJECTION) {
		GPU_matrix_push();
	}
}

void GPU_lgl_PopMatrix(void)
{
	LGL_RECORD(LOP_POPMATRIX, NULL, 0, NULL, 0);
	if (lgl.matrix_mode == GL_TEXTURE) {
		LglUnit *u = active_unit();
		if (u->textop > 0) {
			u->textop--;
			texmat_sync();
		}
	}
	else if (lgl.matrix_mode == GL_MODELVIEW || lgl.matrix_mode == GL_PROJECTION) {
		GPU_matrix_pop();
	}
}

void GPU_lgl_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
	float m[16];
	m16_identity(m);
	m[12] = x;
	m[13] = y;
	m[14] = z;
	GPU_lgl_MultMatrixf(m);
}

void GPU_lgl_Translated(GLdouble x, GLdouble y, GLdouble z)
{
	GPU_lgl_Translatef((float)x, (float)y, (float)z);
}

void GPU_lgl_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
	float m[16];
	m16_identity(m);
	m[0] = x;
	m[5] = y;
	m[10] = z;
	GPU_lgl_MultMatrixf(m);
}

void GPU_lgl_Scaled(GLdouble x, GLdouble y, GLdouble z)
{
	GPU_lgl_Scalef((float)x, (float)y, (float)z);
}

void GPU_lgl_Rotatef(GLfloat degrees, GLfloat x, GLfloat y, GLfloat z)
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
	float m[16];
	m16_identity(m);
	m[0] = x * x * t + c;     m[4] = x * y * t - z * s; m[8] = x * z * t + y * s;
	m[1] = y * x * t + z * s; m[5] = y * y * t + c;     m[9] = y * z * t - x * s;
	m[2] = x * z * t - y * s; m[6] = y * z * t + x * s; m[10] = z * z * t + c;
	GPU_lgl_MultMatrixf(m);
}

void GPU_lgl_Rotated(GLdouble degrees, GLdouble x, GLdouble y, GLdouble z)
{
	GPU_lgl_Rotatef((float)degrees, (float)x, (float)y, (float)z);
}

void GPU_lgl_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	float m[16];
	m16_identity(m);
	m[0] = (float)(2.0 / (r - l));
	m[5] = (float)(2.0 / (t - b));
	m[10] = (float)(-2.0 / (f - n));
	m[12] = (float)(-(r + l) / (r - l));
	m[13] = (float)(-(t + b) / (t - b));
	m[14] = (float)(-(f + n) / (f - n));
	GPU_lgl_MultMatrixf(m);
}

void GPU_lgl_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
	float m[16];
	memset(m, 0, sizeof(m));
	m[0] = (float)(2.0 * n / (r - l));
	m[5] = (float)(2.0 * n / (t - b));
	m[8] = (float)((r + l) / (r - l));
	m[9] = (float)((t + b) / (t - b));
	m[10] = (float)(-(f + n) / (f - n));
	m[11] = -1.0f;
	m[14] = (float)(-2.0 * f * n / (f - n));
	GPU_lgl_MultMatrixf(m);
}

/* ------------------------------------------------------------------------ */
/* lamps, material, light model, fog */

void GPU_lgl_Lightfv(GLenum light, GLenum pname, const GLfloat *p)
{
	const unsigned int e[2] = {light, pname};
	const int nf = (pname == GL_POSITION || pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR) ? 4 :
	               (pname == GL_SPOT_DIRECTION) ? 3 : 1;
	LGL_RECORD(LOP_LIGHT, e, 2, p, nf);
	if (light < GL_LIGHT0 || light >= GL_LIGHT0 + LGL_LIGHTS) {
		return;
	}
	LglLight *l = &lgl.lights[light - GL_LIGHT0];
	float mv[16];
	switch (pname) {
		case GL_AMBIENT: memcpy(l->amb, p, sizeof(float[4])); break;
		case GL_DIFFUSE: memcpy(l->dif, p, sizeof(float[4])); break;
		case GL_SPECULAR: memcpy(l->spec, p, sizeof(float[4])); break;
		case GL_POSITION:
			GPU_matrix_get(GPU_MATRIX_MODELVIEW, (float (*)[4])mv);
			for (int r = 0; r < 4; r++) {
				l->pos[r] = mv[r] * p[0] + mv[4 + r] * p[1] + mv[8 + r] * p[2] + mv[12 + r] * p[3];
			}
			break;
		case GL_SPOT_DIRECTION:
			GPU_matrix_get(GPU_MATRIX_MODELVIEW, (float (*)[4])mv);
			for (int r = 0; r < 3; r++) {
				l->spot_dir[r] = mv[r] * p[0] + mv[4 + r] * p[1] + mv[8 + r] * p[2];
			}
			break;
		case GL_SPOT_EXPONENT: l->spot_exp = p[0]; break;
		case GL_SPOT_CUTOFF: l->spot_cut = p[0]; break;
		case GL_CONSTANT_ATTENUATION: l->att[0] = p[0]; break;
		case GL_LINEAR_ATTENUATION: l->att[1] = p[0]; break;
		case GL_QUADRATIC_ATTENUATION: l->att[2] = p[0]; break;
		default: break;
	}
	lgl_dirty();
}

void GPU_lgl_Lightf(GLenum light, GLenum pname, GLfloat param)
{
	const float p[4] = {param, 0, 0, 0};
	GPU_lgl_Lightfv(light, pname, p);
}

void GPU_lgl_Lighti(GLenum light, GLenum pname, GLint param)
{
	GPU_lgl_Lightf(light, pname, (float)param);
}

void GPU_lgl_Lightiv(GLenum light, GLenum pname, const GLint *params)
{
	float p[4] = {0, 0, 0, 0};
	const int color = (pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR);
	const int n = (pname == GL_POSITION || color) ? 4 : (pname == GL_SPOT_DIRECTION) ? 3 : 1;
	for (int i = 0; i < n; i++) {
		p[i] = color ? (float)params[i] / 2147483647.0f : (float)params[i];
	}
	GPU_lgl_Lightfv(light, pname, p);
}

void GPU_lgl_GetLightfv(GLenum light, GLenum pname, GLfloat *p)
{
	if (light < GL_LIGHT0 || light >= GL_LIGHT0 + LGL_LIGHTS) {
		return;
	}
	const LglLight *l = &lgl.lights[light - GL_LIGHT0];
	switch (pname) {
		case GL_AMBIENT: memcpy(p, l->amb, sizeof(float[4])); break;
		case GL_DIFFUSE: memcpy(p, l->dif, sizeof(float[4])); break;
		case GL_SPECULAR: memcpy(p, l->spec, sizeof(float[4])); break;
		case GL_POSITION: memcpy(p, l->pos, sizeof(float[4])); break;
		case GL_SPOT_DIRECTION: memcpy(p, l->spot_dir, sizeof(float[3])); break;
		case GL_SPOT_EXPONENT: p[0] = l->spot_exp; break;
		case GL_SPOT_CUTOFF: p[0] = l->spot_cut; break;
		case GL_CONSTANT_ATTENUATION: p[0] = l->att[0]; break;
		case GL_LINEAR_ATTENUATION: p[0] = l->att[1]; break;
		case GL_QUADRATIC_ATTENUATION: p[0] = l->att[2]; break;
		default: break;
	}
}

void GPU_lgl_GetLightiv(GLenum light, GLenum pname, GLint *params)
{
	float p[4] = {0, 0, 0, 0};
	GPU_lgl_GetLightfv(light, pname, p);
	const int n = (pname == GL_POSITION || pname == GL_AMBIENT || pname == GL_DIFFUSE || pname == GL_SPECULAR) ? 4 :
	              (pname == GL_SPOT_DIRECTION) ? 3 : 1;
	for (int i = 0; i < n; i++) {
		params[i] = (GLint)p[i];
	}
}

void GPU_lgl_LightModelfv(GLenum pname, const GLfloat *p)
{
	const unsigned int e[2] = {0, pname};
	LGL_RECORD(LOP_LIGHTMODEL, e, 2, p, pname == GL_LIGHT_MODEL_AMBIENT ? 4 : 1);
	switch (pname) {
		case GL_LIGHT_MODEL_AMBIENT: memcpy(lgl.scene_amb, p, sizeof(float[4])); break;
		case GL_LIGHT_MODEL_TWO_SIDE: lgl.two_side = (p[0] != 0.0f); break;
		case GL_LIGHT_MODEL_LOCAL_VIEWER: lgl.local_viewer = (p[0] != 0.0f); break;
		case GL_LIGHT_MODEL_COLOR_CONTROL: lgl.separate_spec = ((GLenum)p[0] == GL_SEPARATE_SPECULAR_COLOR); break;
		default: break;
	}
	lgl_dirty();
}

void GPU_lgl_LightModelf(GLenum pname, GLfloat param)
{
	const float p[4] = {param, 0, 0, 0};
	GPU_lgl_LightModelfv(pname, p);
}

void GPU_lgl_LightModeli(GLenum pname, GLint param)
{
	GPU_lgl_LightModelf(pname, (float)param);
}

void GPU_lgl_LightModeliv(GLenum pname, const GLint *params)
{
	float p[4];
	if (pname == GL_LIGHT_MODEL_AMBIENT) {
		for (int i = 0; i < 4; i++) {
			p[i] = (float)params[i] / 2147483647.0f;
		}
	}
	else {
		p[0] = (float)params[0];
	}
	GPU_lgl_LightModelfv(pname, p);
}

void GPU_lgl_Materialfv(GLenum face, GLenum pname, const GLfloat *p)
{
	const unsigned int e[2] = {face, pname};
	LGL_RECORD(LOP_MATERIAL, e, 2, p, pname == GL_SHININESS ? 1 : 4);
	for (int f = 0; f < 2; f++) {
		if (!(face == GL_FRONT_AND_BACK || (face == GL_FRONT && f == 0) || (face == GL_BACK && f == 1))) {
			continue;
		}
		LglMaterial *m = &lgl.mat[f];
		switch (pname) {
			case GL_AMBIENT: memcpy(m->amb, p, sizeof(float[4])); break;
			case GL_DIFFUSE: memcpy(m->dif, p, sizeof(float[4])); break;
			case GL_SPECULAR: memcpy(m->spec, p, sizeof(float[4])); break;
			case GL_EMISSION: memcpy(m->emi, p, sizeof(float[4])); break;
			case GL_SHININESS: m->shin = p[0]; break;
			case GL_AMBIENT_AND_DIFFUSE:
				memcpy(m->amb, p, sizeof(float[4]));
				memcpy(m->dif, p, sizeof(float[4]));
				break;
			default: break;
		}
	}
	lgl_dirty();
}

void GPU_lgl_Materialf(GLenum face, GLenum pname, GLfloat param)
{
	const float p[4] = {param, 0, 0, 0};
	GPU_lgl_Materialfv(face, pname, p);
}

void GPU_lgl_Materiali(GLenum face, GLenum pname, GLint param)
{
	GPU_lgl_Materialf(face, pname, (float)param);
}

void GPU_lgl_Materialiv(GLenum face, GLenum pname, const GLint *params)
{
	float p[4];
	if (pname == GL_SHININESS) {
		p[0] = (float)params[0];
	}
	else {
		for (int i = 0; i < 4; i++) {
			p[i] = (float)params[i] / 2147483647.0f;
		}
	}
	GPU_lgl_Materialfv(face, pname, p);
}

void GPU_lgl_GetMaterialfv(GLenum face, GLenum pname, GLfloat *p)
{
	const LglMaterial *m = &lgl.mat[face == GL_BACK ? 1 : 0];
	switch (pname) {
		case GL_AMBIENT: memcpy(p, m->amb, sizeof(float[4])); break;
		case GL_DIFFUSE: memcpy(p, m->dif, sizeof(float[4])); break;
		case GL_SPECULAR: memcpy(p, m->spec, sizeof(float[4])); break;
		case GL_EMISSION: memcpy(p, m->emi, sizeof(float[4])); break;
		case GL_SHININESS: p[0] = m->shin; break;
		default: break;
	}
}

void GPU_lgl_GetMaterialiv(GLenum face, GLenum pname, GLint *params)
{
	float p[4] = {0, 0, 0, 0};
	GPU_lgl_GetMaterialfv(face, pname, p);
	const int n = (pname == GL_SHININESS) ? 1 : 4;
	for (int i = 0; i < n; i++) {
		params[i] = (pname == GL_SHININESS) ? (GLint)p[i] : (GLint)(p[i] * 2147483647.0f);
	}
}

void GPU_lgl_ColorMaterial(GLenum face, GLenum mode)
{
	const unsigned int e[2] = {face, mode};
	LGL_RECORD(LOP_COLORMATERIAL, e, 2, NULL, 0);
	lgl.cm_face = face;
	lgl.cm_mode = mode;
	if (lgl.en & E_COLOR_MATERIAL) {
		color_material_apply();
	}
	lgl_dirty();
}

/* lgl_color4f: the colour material follows */
void lgl_color_changed(void)
{
	if (lgl.en & E_COLOR_MATERIAL) {
		color_material_apply();
		lgl_dirty();
	}
}

void GPU_lgl_ShadeModel(GLenum mode)
{
	const unsigned int e = mode;
	LGL_RECORD(LOP_SHADEMODEL, &e, 1, NULL, 0);
	lgl.shade_model = mode;
	lgl_dirty();
}

void GPU_lgl_AlphaFunc(GLenum func, GLfloat ref)
{
	const unsigned int e = func;
	LGL_RECORD(LOP_ALPHAFUNC, &e, 1, &ref, 1);
	lgl.alpha_func = func;
	lgl.alpha_ref = ref < 0.0f ? 0.0f : ref > 1.0f ? 1.0f : ref;
	lgl_dirty();
}

void GPU_lgl_Fogfv(GLenum pname, const GLfloat *p)
{
	const unsigned int e = pname;
	LGL_RECORD(LOP_FOG, &e, 1, p, pname == GL_FOG_COLOR ? 4 : 1);
	switch (pname) {
		case GL_FOG_MODE: lgl.fog_mode = (GLenum)p[0]; break;
		case GL_FOG_DENSITY: lgl.fog_density = p[0]; break;
		case GL_FOG_START: lgl.fog_start = p[0]; break;
		case GL_FOG_END: lgl.fog_end = p[0]; break;
		case GL_FOG_INDEX: lgl.fog_index = p[0]; break;
		case GL_FOG_COLOR: memcpy(lgl.fog_color, p, sizeof(float[4])); break;
		default: break;
	}
	lgl_dirty();
}

void GPU_lgl_Fogf(GLenum pname, GLfloat param)
{
	const float p[4] = {param, 0, 0, 0};
	GPU_lgl_Fogfv(pname, p);
}

void GPU_lgl_Fogi(GLenum pname, GLint param)
{
	GPU_lgl_Fogf(pname, (float)param);
}

void GPU_lgl_Fogiv(GLenum pname, const GLint *params)
{
	float p[4];
	if (pname == GL_FOG_COLOR) {
		for (int i = 0; i < 4; i++) {
			p[i] = (float)params[i] / 2147483647.0f;
		}
	}
	else {
		p[0] = (float)params[0];
	}
	GPU_lgl_Fogfv(pname, p);
}

/* ------------------------------------------------------------------------ */
/* texture environment and coordinate generation */

void GPU_lgl_TexEnvfv(GLenum target, GLenum pname, const GLfloat *p)
{
	const unsigned int e[2] = {target, pname};
	LGL_RECORD(LOP_TEXENV, e, 2, p, pname == GL_TEXTURE_ENV_COLOR ? 4 : 1);
	LglUnit *u = active_unit();
	if (target == GL_TEXTURE_FILTER_CONTROL) {
		if (pname == GL_TEXTURE_LOD_BIAS) {
			/* a texture's own bias now */
			glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, p[0]);
		}
		return;
	}
	if (target == GL_POINT_SPRITE) {
		if (pname == GL_COORD_REPLACE) {
			u->coord_replace = (p[0] != 0.0f);
		}
		return;
	}
	if (target != GL_TEXTURE_ENV) {
		return;
	}
	const GLenum v = (GLenum)p[0];
	switch (pname) {
		case GL_TEXTURE_ENV_MODE: u->env_mode = (int)v; break;
		case GL_TEXTURE_ENV_COLOR: memcpy(u->env_color, p, sizeof(float[4])); break;
		case GL_COMBINE_RGB: u->comb_rgb = (int)v; break;
		case GL_COMBINE_ALPHA: u->comb_alpha = (int)v; break;
		case GL_SRC0_RGB: u->src_rgb[0] = (int)v; break;
		case GL_SRC1_RGB: u->src_rgb[1] = (int)v; break;
		case GL_SRC2_RGB: u->src_rgb[2] = (int)v; break;
		case GL_SRC0_ALPHA: u->src_alpha[0] = (int)v; break;
		case GL_SRC1_ALPHA: u->src_alpha[1] = (int)v; break;
		case GL_SRC2_ALPHA: u->src_alpha[2] = (int)v; break;
		case GL_OPERAND0_RGB: u->op_rgb[0] = (int)v; break;
		case GL_OPERAND1_RGB: u->op_rgb[1] = (int)v; break;
		case GL_OPERAND2_RGB: u->op_rgb[2] = (int)v; break;
		case GL_OPERAND0_ALPHA: u->op_alpha[0] = (int)v; break;
		case GL_OPERAND1_ALPHA: u->op_alpha[1] = (int)v; break;
		case GL_OPERAND2_ALPHA: u->op_alpha[2] = (int)v; break;
		case GL_RGB_SCALE: u->rgb_scale = p[0]; break;
		case GL_ALPHA_SCALE: u->alpha_scale = p[0]; break;
		default: break;
	}
	lgl_dirty();
}

void GPU_lgl_TexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	const float p[4] = {param, 0, 0, 0};
	GPU_lgl_TexEnvfv(target, pname, p);
}

void GPU_lgl_TexEnvi(GLenum target, GLenum pname, GLint param)
{
	GPU_lgl_TexEnvf(target, pname, (float)param);
}

void GPU_lgl_TexEnviv(GLenum target, GLenum pname, const GLint *params)
{
	float p[4];
	if (pname == GL_TEXTURE_ENV_COLOR) {
		for (int i = 0; i < 4; i++) {
			p[i] = (float)params[i] / 2147483647.0f;
		}
	}
	else {
		p[0] = (float)params[0];
	}
	GPU_lgl_TexEnvfv(target, pname, p);
}

void GPU_lgl_GetTexEnvfv(GLenum target, GLenum pname, GLfloat *p)
{
	const LglUnit *u = active_unit();
	(void)target;
	switch (pname) {
		case GL_TEXTURE_ENV_MODE: p[0] = (float)u->env_mode; break;
		case GL_TEXTURE_ENV_COLOR: memcpy(p, u->env_color, sizeof(float[4])); break;
		case GL_COMBINE_RGB: p[0] = (float)u->comb_rgb; break;
		case GL_COMBINE_ALPHA: p[0] = (float)u->comb_alpha; break;
		case GL_RGB_SCALE: p[0] = u->rgb_scale; break;
		case GL_ALPHA_SCALE: p[0] = u->alpha_scale; break;
		default:
			if (pname >= GL_SRC0_RGB && pname <= GL_SRC2_RGB) p[0] = (float)u->src_rgb[pname - GL_SRC0_RGB];
			else if (pname >= GL_SRC0_ALPHA && pname <= GL_SRC2_ALPHA) p[0] = (float)u->src_alpha[pname - GL_SRC0_ALPHA];
			else if (pname >= GL_OPERAND0_RGB && pname <= GL_OPERAND2_RGB) p[0] = (float)u->op_rgb[pname - GL_OPERAND0_RGB];
			else if (pname >= GL_OPERAND0_ALPHA && pname <= GL_OPERAND2_ALPHA) p[0] = (float)u->op_alpha[pname - GL_OPERAND0_ALPHA];
			break;
	}
}

void GPU_lgl_GetTexEnviv(GLenum target, GLenum pname, GLint *params)
{
	float p[4] = {0, 0, 0, 0};
	GPU_lgl_GetTexEnvfv(target, pname, p);
	if (pname == GL_TEXTURE_ENV_COLOR) {
		for (int i = 0; i < 4; i++) {
			params[i] = (GLint)(p[i] * 2147483647.0f);
		}
	}
	else {
		params[0] = (GLint)p[0];
	}
}

static int gen_coord(GLenum coord)
{
	return (coord == GL_S) ? 0 : (coord == GL_T) ? 1 : (coord == GL_R) ? 2 : (coord == GL_Q) ? 3 : -1;
}

void GPU_lgl_TexGendv(GLenum coord, GLenum pname, const GLdouble *p)
{
	const int k = gen_coord(coord);
	if (k < 0) {
		return;
	}
	{
		const unsigned int e[2] = {coord, pname};
		float f[4] = {(float)p[0], 0, 0, 0};
		if (pname != GL_TEXTURE_GEN_MODE) {
			for (int i = 0; i < 4; i++) f[i] = (float)p[i];
		}
		LGL_RECORD(LOP_TEXGEN, e, 2, f, 4);
	}
	LglUnit *u = active_unit();
	if (pname == GL_TEXTURE_GEN_MODE) {
		u->gen_mode[k] = (int)p[0];
	}
	else if (pname == GL_OBJECT_PLANE) {
		for (int i = 0; i < 4; i++) {
			u->gen_obj[k][i] = (float)p[i];
		}
	}
	else if (pname == GL_EYE_PLANE) {
		double r[4];
		plane_to_eye(p, r);
		for (int i = 0; i < 4; i++) {
			u->gen_eye[k][i] = (float)r[i];
		}
	}
	lgl_dirty();
}

void GPU_lgl_TexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
	double p[4] = {params[0], 0, 0, 0};
	if (pname != GL_TEXTURE_GEN_MODE) {
		for (int i = 0; i < 4; i++) p[i] = params[i];
	}
	GPU_lgl_TexGendv(coord, pname, p);
}

void GPU_lgl_TexGeniv(GLenum coord, GLenum pname, const GLint *params)
{
	double p[4] = {params[0], 0, 0, 0};
	if (pname != GL_TEXTURE_GEN_MODE) {
		for (int i = 0; i < 4; i++) p[i] = params[i];
	}
	GPU_lgl_TexGendv(coord, pname, p);
}

void GPU_lgl_TexGend(GLenum coord, GLenum pname, GLdouble param)
{
	const double p[4] = {param, 0, 0, 0};
	GPU_lgl_TexGendv(coord, pname, p);
}

void GPU_lgl_TexGenf(GLenum coord, GLenum pname, GLfloat param)
{
	GPU_lgl_TexGend(coord, pname, param);
}

void GPU_lgl_TexGeni(GLenum coord, GLenum pname, GLint param)
{
	GPU_lgl_TexGend(coord, pname, param);
}

void GPU_lgl_GetTexGendv(GLenum coord, GLenum pname, GLdouble *p)
{
	const int k = gen_coord(coord);
	if (k < 0) {
		return;
	}
	const LglUnit *u = active_unit();
	if (pname == GL_TEXTURE_GEN_MODE) {
		p[0] = u->gen_mode[k];
	}
	else {
		const float *src = (pname == GL_OBJECT_PLANE) ? u->gen_obj[k] : u->gen_eye[k];
		for (int i = 0; i < 4; i++) {
			p[i] = src[i];
		}
	}
}

void GPU_lgl_GetTexGenfv(GLenum coord, GLenum pname, GLfloat *params)
{
	double p[4] = {0, 0, 0, 0};
	GPU_lgl_GetTexGendv(coord, pname, p);
	for (int i = 0; i < (pname == GL_TEXTURE_GEN_MODE ? 1 : 4); i++) {
		params[i] = (float)p[i];
	}
}

void GPU_lgl_GetTexGeniv(GLenum coord, GLenum pname, GLint *params)
{
	double p[4] = {0, 0, 0, 0};
	GPU_lgl_GetTexGendv(coord, pname, p);
	for (int i = 0; i < (pname == GL_TEXTURE_GEN_MODE ? 1 : 4); i++) {
		params[i] = (GLint)p[i];
	}
}

/* ------------------------------------------------------------------------ */
/* stipples, clip planes, polygon mode, hints */

void GPU_lgl_LineStipple(GLint factor, GLushort pattern)
{
	const unsigned int e[2] = {(unsigned int)factor, pattern};
	LGL_RECORD(LOP_LINESTIPPLE, e, 2, NULL, 0);
	lgl.line_factor = factor < 1 ? 1 : factor > 256 ? 256 : factor;
	lgl.line_pattern = pattern;
	lgl_dirty();
}

void GPU_lgl_PolygonStipple(const GLubyte *mask)
{
	float f[32];
	memcpy(f, mask, 128);
	LGL_RECORD(LOP_POLYGONSTIPPLE, NULL, 0, f, 32);
	memcpy(lgl.poly_stipple, mask, 128);
	lgl_dirty();
}

void GPU_lgl_GetPolygonStipple(GLubyte *mask)
{
	memcpy(mask, lgl.poly_stipple, 128);
}

void GPU_lgl_ClipPlane(GLenum plane, const GLdouble *eq)
{
	const unsigned int e = plane;
	const float f[4] = {(float)eq[0], (float)eq[1], (float)eq[2], (float)eq[3]};
	LGL_RECORD(LOP_CLIPPLANE, &e, 1, f, 4);
	if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + LGL_CLIPS) {
		return;
	}
	plane_to_eye(eq, lgl.clip[plane - GL_CLIP_PLANE0]);
	lgl_dirty();
}

void GPU_lgl_GetClipPlane(GLenum plane, GLdouble *eq)
{
	if (plane < GL_CLIP_PLANE0 || plane >= GL_CLIP_PLANE0 + LGL_CLIPS) {
		return;
	}
	memcpy(eq, lgl.clip[plane - GL_CLIP_PLANE0], sizeof(double[4]));
}

void GPU_lgl_PolygonMode(GLenum face, GLenum mode)
{
	const unsigned int e[2] = {face, mode};
	LGL_RECORD(LOP_POLYGONMODE, e, 2, NULL, 0);
	if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
		lgl.poly_mode[0] = mode;
	}
	if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
		lgl.poly_mode[1] = mode;
	}
	if (face != GL_FRONT_AND_BACK && lgl.poly_mode[0] != lgl.poly_mode[1]) {
		lgl_note_once("glPolygonMode with different front and back modes: the front's is used");
	}
	glPolygonMode(GL_FRONT_AND_BACK, lgl.poly_mode[0]);
}

void GPU_lgl_Hint(GLenum target, GLenum mode)
{
	switch (target) {
		case GL_LINE_SMOOTH_HINT: case GL_POLYGON_SMOOTH_HINT: case GL_TEXTURE_COMPRESSION_HINT:
		case GL_FRAGMENT_SHADER_DERIVATIVE_HINT:
			glHint(target, mode);
			break;
		default:
			break;
	}
}

/* ------------------------------------------------------------------------ */
/* textures: bindings, the old formats */

/* base formats (gpu_shader_legacy.glsl) */
enum { B_RGBA = 0, B_RGB, B_ALPHA, B_LUM, B_LUM_ALPHA, B_INTENSITY };
#define B_GENMIPMAP 0x100

static unsigned int *unit_binding(LglUnit *u, GLenum target)
{
	switch (target) {
		case GL_TEXTURE_2D: return &u->bound_2d;
		case GL_TEXTURE_RECTANGLE: return &u->bound_rect;
		case GL_TEXTURE_CUBE_MAP: return &u->bound_cube;
		case GL_TEXTURE_1D: return &u->bound_1d;
		case GL_TEXTURE_3D: return &u->bound_3d;
		default: return NULL;
	}
}

static unsigned int lgl_unit15_2d = 0;

void GPU_lgl_BindTexture(GLenum target, GLuint texture)
{
	const unsigned int e[2] = {target, texture};
	LGL_RECORD(LOP_BINDTEXTURE, e, 2, NULL, 0);
	if (lgl.active_unit >= 0 && lgl.active_unit < LGL_UNITS) {
		unsigned int *b = unit_binding(&lgl.unit[lgl.active_unit], target);
		if (b && *b != texture) {
			*b = texture;
			if (lgl.active_unit < LGL_FF_UNITS) {
				lgl_dirty();
			}
		}
	}
	else if (lgl.active_unit == LGL_PX_UNIT && target == GL_TEXTURE_2D) {
		lgl_unit15_2d = texture;
	}
	glBindTexture(target, texture);
}

unsigned int lgl_unit15_texture(void)
{
	return lgl_unit15_2d;
}

void GPU_lgl_DeleteTextures(GLsizei n, const GLuint *textures)
{
	for (GLsizei i = 0; i < n; i++) {
		const unsigned int t = textures[i];
		if (!t) {
			continue;
		}
		lgl_tex_set_base_format(t, 0);
		for (int u = 0; u < LGL_UNITS; u++) {
			LglUnit *un = &lgl.unit[u];
			if (un->bound_2d == t) un->bound_2d = 0;
			if (un->bound_rect == t) un->bound_rect = 0;
			if (un->bound_cube == t) un->bound_cube = 0;
			if (un->bound_1d == t) un->bound_1d = 0;
			if (un->bound_3d == t) un->bound_3d = 0;
		}
		if (lgl_unit15_2d == t) {
			lgl_unit15_2d = 0;
		}
	}
	lgl_dirty();
	glDeleteTextures(n, textures);
}

GLboolean GPU_lgl_AreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
	(void)textures;
	for (GLsizei i = 0; i < n; i++) {
		residences[i] = GL_TRUE;
	}
	return GL_TRUE;
}

void GPU_lgl_PrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
	(void)n;
	(void)textures;
	(void)priorities;
}

static unsigned int bound_texture(GLenum target)
{
	if (target == GL_PROXY_TEXTURE_2D || target == GL_PROXY_TEXTURE_1D || target == GL_PROXY_TEXTURE_3D ||
	    target == GL_PROXY_TEXTURE_RECTANGLE || target == GL_PROXY_TEXTURE_CUBE_MAP)
	{
		return 0;
	}
	if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
		target = GL_TEXTURE_CUBE_MAP;
	}
	GLenum q;
	switch (target) {
		case GL_TEXTURE_1D: q = GL_TEXTURE_BINDING_1D; break;
		case GL_TEXTURE_3D: q = GL_TEXTURE_BINDING_3D; break;
		case GL_TEXTURE_RECTANGLE: q = GL_TEXTURE_BINDING_RECTANGLE; break;
		case GL_TEXTURE_CUBE_MAP: q = GL_TEXTURE_BINDING_CUBE_MAP; break;
		case GL_TEXTURE_2D_ARRAY: q = GL_TEXTURE_BINDING_2D_ARRAY; break;
		default: q = GL_TEXTURE_BINDING_2D; break;
	}
	GLint t = 0;
	glGetIntegerv(q, &t);
	return (unsigned int)t;
}

/* an old internal format as OpenGL 4.5 has it; r_base: its base format */
static GLint map_internal(GLint internal, int *r_base)
{
	*r_base = B_RGBA;
	switch (internal) {
		case 1: case GL_LUMINANCE: case GL_LUMINANCE4: case GL_LUMINANCE8: case GL_LUMINANCE12:
			*r_base = B_LUM; return GL_R8;
		case GL_LUMINANCE16: *r_base = B_LUM; return GL_R16;
		case GL_LUMINANCE16F_ARB: *r_base = B_LUM; return GL_R16F;
		case GL_LUMINANCE32F_ARB: *r_base = B_LUM; return GL_R32F;
		case GL_ALPHA: case GL_ALPHA4: case GL_ALPHA8: case GL_ALPHA12:
			*r_base = B_ALPHA; return GL_R8;
		case GL_ALPHA16: *r_base = B_ALPHA; return GL_R16;
		case GL_ALPHA16F_ARB: *r_base = B_ALPHA; return GL_R16F;
		case GL_ALPHA32F_ARB: *r_base = B_ALPHA; return GL_R32F;
		case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE4_ALPHA4: case GL_LUMINANCE6_ALPHA2:
		case GL_LUMINANCE8_ALPHA8: case GL_LUMINANCE12_ALPHA4: case GL_LUMINANCE12_ALPHA12:
			*r_base = B_LUM_ALPHA; return GL_RG8;
		case GL_LUMINANCE16_ALPHA16: *r_base = B_LUM_ALPHA; return GL_RG16;
		case GL_LUMINANCE_ALPHA16F_ARB: *r_base = B_LUM_ALPHA; return GL_RG16F;
		case GL_LUMINANCE_ALPHA32F_ARB: *r_base = B_LUM_ALPHA; return GL_RG32F;
		case GL_INTENSITY: case GL_INTENSITY4: case GL_INTENSITY8: case GL_INTENSITY12:
			*r_base = B_INTENSITY; return GL_R8;
		case GL_INTENSITY16: *r_base = B_INTENSITY; return GL_R16;
		case GL_INTENSITY16F_ARB: *r_base = B_INTENSITY; return GL_R16F;
		case GL_INTENSITY32F_ARB: *r_base = B_INTENSITY; return GL_R32F;
		case 3: case GL_RGB: *r_base = B_RGB; return GL_RGB8;
		case 4: case GL_RGBA: return GL_RGBA8;
		case GL_RGB4: case GL_RGB5: case GL_RGB8: case GL_RGB10: case GL_RGB12: case GL_RGB16: case GL_R3_G3_B2:
		case GL_RGB16F: case GL_RGB32F: case GL_SRGB8: case GL_RGB9_E5: case GL_R11F_G11F_B10F:
		case GL_COMPRESSED_RGB: case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
			*r_base = B_RGB; return internal;
		case GL_R8: case GL_R16: case GL_R16F: case GL_R32F: case GL_RG8: case GL_RG16: case GL_RG16F:
		case GL_RG32F:
			*r_base = B_RGB; return internal;
		default:
			return internal;
	}
}

static GLenum map_format(GLenum format)
{
	switch (format) {
		case GL_LUMINANCE: case GL_ALPHA: case GL_INTENSITY: return GL_RED;
		case GL_LUMINANCE_ALPHA: return GL_RG;
		default: return format;
	}
}

static void set_swizzle(GLenum target, int base)
{
	GLint s[4] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
	switch (base) {
		case B_ALPHA: s[0] = s[1] = s[2] = GL_ZERO; s[3] = GL_RED; break;
		case B_LUM: s[0] = s[1] = s[2] = GL_RED; s[3] = GL_ONE; break;
		case B_LUM_ALPHA: s[0] = s[1] = s[2] = GL_RED; s[3] = GL_GREEN; break;
		case B_INTENSITY: s[0] = s[1] = s[2] = s[3] = GL_RED; break;
		default: return;
	}
	glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, s);
}

static GLenum swizzle_target(GLenum target)
{
	return (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) ?
	       GL_TEXTURE_CUBE_MAP : target;
}

static void after_image(GLenum target, GLint level, int base, int set_base)
{
	const unsigned int tex = bound_texture(target);
	if (!tex) {
		return;
	}
	const int old = lgl_tex_base_format(tex);
	if (set_base && level == 0) {
		lgl_tex_set_base_format(tex, (old & B_GENMIPMAP) | base);
		if (base != B_RGBA && base != B_RGB) {
			set_swizzle(swizzle_target(target), base);
		}
		lgl_dirty();
	}
	if ((old & B_GENMIPMAP) && level == 0) {
		glGenerateMipmap(swizzle_target(target));
	}
}

void GPU_lgl_TexImage1D(GLenum target, GLint level, GLint internal, GLsizei w, GLint border, GLenum format,
                        GLenum type, const void *pixels)
{
	int base;
	const GLint in = map_internal(internal, &base);
	(void)border;
	glTexImage1D(target, level, in, w, 0, map_format(format), type, pixels);
	after_image(target, level, base, 1);
}

void GPU_lgl_TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
                        GLenum format, GLenum type, const void *pixels)
{
	int base;
	const GLint in = map_internal(internal, &base);
	(void)border;
	glTexImage2D(target, level, in, w, h, 0, map_format(format), type, pixels);
	after_image(target, level, base, 1);
}

void GPU_lgl_TexSubImage1D(GLenum target, GLint level, GLint x, GLsizei w, GLenum format, GLenum type,
                           const void *pixels)
{
	glTexSubImage1D(target, level, x, w, map_format(format), type, pixels);
	after_image(target, level, 0, 0);
}

void GPU_lgl_TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                           GLenum type, const void *pixels)
{
	glTexSubImage2D(target, level, x, y, w, h, map_format(format), type, pixels);
	after_image(target, level, 0, 0);
}

void GPU_lgl_CopyTexImage1D(GLenum target, GLint level, GLenum internal, GLint x, GLint y, GLsizei w, GLint border)
{
	int base;
	const GLint in = map_internal((GLint)internal, &base);
	(void)border;
	glCopyTexImage1D(target, level, (GLenum)in, x, y, w, 0);
	after_image(target, level, base, 1);
}

void GPU_lgl_CopyTexImage2D(GLenum target, GLint level, GLenum internal, GLint x, GLint y, GLsizei w, GLsizei h,
                            GLint border)
{
	int base;
	const GLint in = map_internal((GLint)internal, &base);
	(void)border;
	glCopyTexImage2D(target, level, (GLenum)in, x, y, w, h, 0);
	after_image(target, level, base, 1);
}

void GPU_lgl_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels)
{
	glGetTexImage(target, level, map_format(format), type, pixels);
}

void GPU_lgl_ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels)
{
	glReadPixels(x, y, w, h, map_format(format), type, pixels);
}

static GLint map_wrap(GLint v)
{
	return (v == GL_CLAMP) ? GL_CLAMP_TO_EDGE : v;
}

static int tex_param_special(GLenum target, GLenum pname, GLint v)
{
	if (pname == GL_GENERATE_MIPMAP) {
		const unsigned int tex = bound_texture(target);
		const int old = lgl_tex_base_format(tex);
		lgl_tex_set_base_format(tex, v ? (old | B_GENMIPMAP) : (old & ~B_GENMIPMAP));
		return 1;
	}
	if (pname == GL_DEPTH_TEXTURE_MODE) {
		set_swizzle(target, (v == GL_LUMINANCE) ? B_LUM : (v == GL_INTENSITY) ? B_INTENSITY :
		                    (v == GL_ALPHA) ? B_ALPHA : B_RGBA);
		return 1;
	}
	if (pname == GL_TEXTURE_PRIORITY || pname == GL_TEXTURE_RESIDENT) {
		return 1;
	}
	return 0;
}

void GPU_lgl_TexParameteri(GLenum target, GLenum pname, GLint param)
{
	if (tex_param_special(target, pname, param)) {
		return;
	}
	if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T || pname == GL_TEXTURE_WRAP_R) {
		param = map_wrap(param);
	}
	glTexParameteri(target, pname, param);
}

void GPU_lgl_TexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	if (tex_param_special(target, pname, (GLint)param)) {
		return;
	}
	if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T || pname == GL_TEXTURE_WRAP_R) {
		glTexParameteri(target, pname, map_wrap((GLint)param));
		return;
	}
	glTexParameterf(target, pname, param);
}

void GPU_lgl_TexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
	if (pname != GL_TEXTURE_BORDER_COLOR && pname != GL_TEXTURE_SWIZZLE_RGBA) {
		GPU_lgl_TexParameteri(target, pname, params[0]);
		return;
	}
	glTexParameteriv(target, pname, params);
}

void GPU_lgl_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
	if (pname != GL_TEXTURE_BORDER_COLOR) {
		GPU_lgl_TexParameterf(target, pname, params[0]);
		return;
	}
	glTexParameterfv(target, pname, params);
}

/* ------------------------------------------------------------------------ */
/* the block of the fixed-function look */

static int env_mode_code(int m)
{
	switch (m) {
		case GL_MODULATE: return 0;
		case GL_REPLACE: return 1;
		case GL_DECAL: return 2;
		case GL_BLEND: return 3;
		case GL_ADD: return 4;
		case GL_COMBINE: return 5;
		default: return 0;
	}
}

static int comb_code(int f)
{
	switch (f) {
		case GL_REPLACE: return 0;
		case GL_MODULATE: return 1;
		case GL_ADD: return 2;
		case GL_ADD_SIGNED: return 3;
		case GL_INTERPOLATE: return 4;
		case GL_SUBTRACT: return 5;
		case GL_DOT3_RGB: return 6;
		case GL_DOT3_RGBA: return 7;
		default: return 1;
	}
}

static int src_code(int s)
{
	switch (s) {
		case GL_TEXTURE: return 0;
		case GL_CONSTANT: return 1;
		case GL_PRIMARY_COLOR: return 2;
		case GL_PREVIOUS: return 3;
		default: return 0;  /* GL_TEXTUREn: this unit's (the crossbar is not emulated) */
	}
}

static int gen_code(int g)
{
	switch (g) {
		case GL_OBJECT_LINEAR: return 1;
		case GL_EYE_LINEAR: return 2;
		case GL_SPHERE_MAP: return 3;
		case GL_REFLECTION_MAP: return 4;
		case GL_NORMAL_MAP: return 5;
		default: return 0;
	}
}

static void normal_matrix(float *r, const float *mv)
{
	float inv[16];
	m16_invert(inv, mv);
	memset(r, 0, sizeof(float[16]));
	for (int c = 0; c < 3; c++) {
		for (int row = 0; row < 3; row++) {
			r[c * 4 + row] = inv[row * 4 + c];
		}
	}
	r[15] = 1.0f;
}

void lgl_build_block(LglBlock *b)
{
	memset(b, 0, sizeof(*b));
	float mv[4][4], proj[4][4], tm[4][4];
	GPU_matrix_get(GPU_MATRIX_MODELVIEW, mv);
	GPU_matrix_get(GPU_MATRIX_PROJECTION, proj);
	memcpy(b->mv, mv, sizeof(b->mv));
	m16_mul(b->mvp, &proj[0][0], &mv[0][0]);
	normal_matrix(b->nmat, &mv[0][0]);
	for (int u = 0; u < LGL_FF_UNITS; u++) {
		GPU_texture_matrix_get(u, tm);
		memcpy(b->texmat[u], tm, sizeof(float[16]));
	}
	for (int i = 0; i < LGL_CLIPS; i++) {
		/* a plane that is off keeps everything (a driver may clip by a
		 * distance written for a plane that is off) */
		const int on = (lgl.clip_on >> i) & 1;
		for (int k = 0; k < 4; k++) {
			b->clip[i][k] = on ? (float)lgl.clip[i][k] : (k == 3 ? 1.0f : 0.0f);
		}
	}
	for (int i = 0; i < LGL_LIGHTS; i++) {
		const LglLight *l = &lgl.lights[i];
		memcpy(b->l_pos[i], l->pos, sizeof(float[4]));
		memcpy(b->l_amb[i], l->amb, sizeof(float[4]));
		memcpy(b->l_dif[i], l->dif, sizeof(float[4]));
		memcpy(b->l_spec[i], l->spec, sizeof(float[4]));
		memcpy(b->l_spot[i], l->spot_dir, sizeof(float[3]));
		b->l_spot[i][3] = (l->spot_cut >= 180.0f) ? -2.0f : cosf(l->spot_cut * (float)(M_PI / 180.0));
		memcpy(b->l_att[i], l->att, sizeof(float[3]));
		b->l_att[i][3] = l->spot_exp;
	}
	for (int f = 0; f < 2; f++) {
		const LglMaterial *m = &lgl.mat[f];
		memcpy(b->m_emi[f], m->emi, sizeof(float[4]));
		memcpy(b->m_amb[f], m->amb, sizeof(float[4]));
		memcpy(b->m_dif[f], m->dif, sizeof(float[4]));
		memcpy(b->m_spec[f], m->spec, sizeof(float[4]));
		b->m_shin[f] = m->shin;
	}
	memcpy(b->scene_amb, lgl.scene_amb, sizeof(float[4]));

	int fl = 0;
	if (lgl.en & E_LIGHTING) fl |= F_LIGHTING;
	if (lgl.two_side) fl |= F_TWO_SIDE;
	if (lgl.local_viewer) fl |= F_LOCAL_VIEWER;
	if (lgl.en & (E_NORMALIZE | E_RESCALE_NORMAL)) fl |= F_NORMALIZE;
	if (lgl.shade_model == GL_FLAT) fl |= F_FLAT;
	if (lgl.en & E_POINT_SMOOTH) fl |= F_POINT_SMOOTH;
	if (lgl.en & E_POLYGON_STIPPLE) fl |= F_POLY_STIPPLE;
	if (lgl.en & E_COLOR_MATERIAL) fl |= F_COLOR_MAT;
	if (lgl.en & E_FOG) fl |= F_FOG;
	if (lgl.en & E_LINE_STIPPLE) fl |= F_LINE_STIPPLE;
	if (lgl.en & E_ALPHA_TEST) fl |= F_ALPHA_TEST;
	b->flags[0] = fl;
	b->flags[1] = (int)lgl.light_on;
	b->flags[2] = (int)lgl.cm_mode;
	b->flags[3] = (lgl.cm_face == GL_FRONT) ? 1 : (lgl.cm_face == GL_BACK) ? 2 : 3;

	b->modes[0] = (int)lgl.alpha_func;
	b->modes[1] = (lgl.fog_mode == GL_LINEAR) ? 1 : (lgl.fog_mode == GL_EXP) ? 2 : 3;
	b->modes[2] = lgl.line_pattern;
	b->modes[3] = lgl.line_factor;
	b->fogv[0] = lgl.fog_start;
	b->fogv[1] = lgl.fog_end;
	b->fogv[2] = lgl.fog_density;
	b->fogv[3] = lgl.alpha_ref;
	memcpy(b->fog_color, lgl.fog_color, sizeof(float[4]));

	for (int un = 0; un < LGL_FF_UNITS; un++) {
		const LglUnit *u = &lgl.unit[un];
		unsigned int tex = 0;
		int on = 0;
		if (u->en & U_CUBE) {
			on = 1;
			tex = u->bound_cube;
		}
		else if (u->en & U_RECT) {
			on = 1;
			tex = u->bound_rect;
		}
		else if (u->en & U_2D) {
			on = 1;
			tex = u->bound_2d;
		}
		b->tex_on[un] = on;
		b->env[un][0] = env_mode_code(u->env_mode);
		b->env[un][1] = lgl_tex_base_format(tex) & 0xff;
		b->env[un][2] = comb_code(u->comb_rgb);
		b->env[un][3] = comb_code(u->comb_alpha);
		b->env_src[un][0] = src_code(u->src_rgb[0]) | (src_code(u->src_rgb[1]) << 4) | (src_code(u->src_rgb[2]) << 8);
		b->env_src[un][1] = src_code(u->src_alpha[0]) | (src_code(u->src_alpha[1]) << 4) |
		                    (src_code(u->src_alpha[2]) << 8);
		b->env_src[un][2] = (u->op_rgb[0] - GL_SRC_COLOR) | ((u->op_rgb[1] - GL_SRC_COLOR) << 4) |
		                    ((u->op_rgb[2] - GL_SRC_COLOR) << 8);
		b->env_src[un][3] = (u->op_alpha[0] - GL_SRC_ALPHA) | ((u->op_alpha[1] - GL_SRC_ALPHA) << 4) |
		                    ((u->op_alpha[2] - GL_SRC_ALPHA) << 8);
		memcpy(b->env_color[un], u->env_color, sizeof(float[4]));
		b->env_scale[un][0] = u->rgb_scale;
		b->env_scale[un][1] = u->alpha_scale;
		for (int k = 0; k < 4; k++) {
			const int bit = U_GEN_S << k;
			b->gen[un][k] = (u->en & bit) ? gen_code(u->gen_mode[k]) : 0;
			memcpy(b->gen_obj[un * 4 + k], u->gen_obj[k], sizeof(float[4]));
			memcpy(b->gen_eye[un * 4 + k], u->gen_eye[k], sizeof(float[4]));
		}
	}

	/* the stipple: rows from the bottom, the high bit of each byte first */
	for (int y = 0; y < 32; y++) {
		unsigned int row = 0;
		for (int x = 0; x < 32; x++) {
			const unsigned char byte = lgl.poly_stipple[y * 4 + (x >> 3)];
			if (byte & (0x80 >> (x & 7))) {
				row |= 1u << x;
			}
		}
		b->pstip[y >> 2][y & 3] = row;
	}
	memcpy(b->px_scale, lgl.px_scale, sizeof(float[4]));
	memcpy(b->px_bias, lgl.px_bias, sizeof(float[4]));
}

/* the old built-ins of the application's programs */
void lgl_classic_state(GPUClassicState *st)
{
	GPU_classic_state_default(st);
	st->lighting = (lgl.en & E_LIGHTING) != 0;
	int count = 0;
	for (int i = 0; i < LGL_LIGHTS; i++) {
		const LglLight *l = &lgl.lights[i];
		GPUClassicLight *cl = &st->lights[i];
		memcpy(cl->position, l->pos, sizeof(float[4]));
		memcpy(cl->ambient, l->amb, sizeof(float[4]));
		memcpy(cl->diffuse, l->dif, sizeof(float[4]));
		memcpy(cl->specular, l->spec, sizeof(float[4]));
		memcpy(cl->spot_direction, l->spot_dir, sizeof(float[3]));
		cl->spot_cutoff = l->spot_cut;
		cl->spot_exponent = l->spot_exp;
		memcpy(cl->attenuation, l->att, sizeof(float[3]));
		if (lgl.light_on & (1u << i)) {
			count = i + 1;
		}
	}
	st->light_count = count;
	memcpy(st->scene_ambient, lgl.scene_amb, sizeof(float[4]));
	memcpy(st->mat_ambient, lgl.mat[0].amb, sizeof(float[4]));
	memcpy(st->mat_diffuse, lgl.mat[0].dif, sizeof(float[4]));
	memcpy(st->mat_specular, lgl.mat[0].spec, sizeof(float[4]));
	memcpy(st->mat_emission, lgl.mat[0].emi, sizeof(float[4]));
	st->shininess = lgl.mat[0].shin;
	st->vertex_color = (lgl.en & E_COLOR_MATERIAL) ? 1 : 0;
	st->texturing = (lgl.unit[0].en & (U_2D | U_RECT | U_CUBE)) != 0;
	st->texenv = (lgl.unit[0].env_mode == GL_REPLACE) ? GPU_TEXENV_REPLACE :
	             (lgl.unit[0].env_mode == GL_DECAL) ? GPU_TEXENV_DECAL :
	             (lgl.unit[0].env_mode == GL_ADD) ? GPU_TEXENV_ADD :
	             (lgl.unit[0].env_mode == GL_BLEND) ? GPU_TEXENV_BLEND : GPU_TEXENV_MODULATE;
	memcpy(st->texenv_color, lgl.unit[0].env_color, sizeof(float[4]));
	st->fog = (lgl.en & E_FOG) ? ((lgl.fog_mode == GL_LINEAR) ? GPU_FOG_LINEAR :
	                              (lgl.fog_mode == GL_EXP) ? GPU_FOG_EXP : GPU_FOG_EXP2) : GPU_FOG_OFF;
	st->fog_start = lgl.fog_start;
	st->fog_end = lgl.fog_end;
	st->fog_density = lgl.fog_density;
	memcpy(st->fog_color, lgl.fog_color, sizeof(float[4]));
	st->alpha_test = (lgl.en & E_ALPHA_TEST) != 0;
	st->alpha_ref = lgl.alpha_ref;
	memcpy(st->current_color, lgl.color, sizeof(float[4]));
}

/* ------------------------------------------------------------------------ */
/* the attribute stacks */

typedef struct LglReal {
	GLint blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a, blend_eq_rgb, blend_eq_a;
	GLfloat blend_color[4];
	GLboolean color_mask[4];
	GLfloat clear_color[4];
	GLint draw_buffer, read_buffer;
	GLint logic_op;
	GLint depth_func;
	GLboolean depth_mask;
	GLdouble clear_depth;
	GLint cull_mode, front_face;
	GLfloat offset_factor, offset_units;
	GLfloat line_width, point_size;
	GLint scissor[4], viewport[4];
	GLdouble depth_range[2];
	GLint st_func, st_ref, st_mask, st_fail, st_zfail, st_zpass, st_write, st_clear;
	GLint unpack[8], pack[8];
	struct { GLenum cap; GLboolean on; } en[16];
	int nen;
	GLuint tex2d[LGL_UNITS], texrect[LGL_UNITS], texcube[LGL_UNITS];
	GLint active;
} LglReal;

typedef struct LglAttrib {
	GLbitfield mask;
	LglState s;          /* the emulation's (the parts restored are copied back) */
	LglReal r;
} LglAttrib;

static LglAttrib *lgl_attrib_stack = NULL;
static int lgl_attrib_top = 0;

static const GLenum real_caps[] = {
	GL_BLEND, GL_CULL_FACE, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_POLYGON_OFFSET_FILL, GL_POLYGON_OFFSET_LINE,
	GL_POLYGON_OFFSET_POINT, GL_LINE_SMOOTH, GL_POLYGON_SMOOTH, GL_STENCIL_TEST, GL_DITHER, GL_COLOR_LOGIC_OP,
	GL_MULTISAMPLE, GL_PROGRAM_POINT_SIZE, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_DEPTH_CLAMP,
};

static void real_save(LglReal *r, GLbitfield mask)
{
	memset(r, 0, sizeof(*r));
	if (mask & (GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | GL_LINE_BIT |
	            GL_SCISSOR_BIT | GL_STENCIL_BUFFER_BIT | GL_MULTISAMPLE_BIT))
	{
		for (unsigned int i = 0; i < sizeof(real_caps) / sizeof(real_caps[0]); i++) {
			r->en[r->nen].cap = real_caps[i];
			r->en[r->nen].on = glIsEnabled(real_caps[i]);
			r->nen++;
		}
	}
	if (mask & GL_COLOR_BUFFER_BIT) {
		glGetIntegerv(GL_BLEND_SRC_RGB, &r->blend_src_rgb);
		glGetIntegerv(GL_BLEND_DST_RGB, &r->blend_dst_rgb);
		glGetIntegerv(GL_BLEND_SRC_ALPHA, &r->blend_src_a);
		glGetIntegerv(GL_BLEND_DST_ALPHA, &r->blend_dst_a);
		glGetIntegerv(GL_BLEND_EQUATION_RGB, &r->blend_eq_rgb);
		glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &r->blend_eq_a);
		glGetFloatv(GL_BLEND_COLOR, r->blend_color);
		glGetBooleanv(GL_COLOR_WRITEMASK, r->color_mask);
		glGetFloatv(GL_COLOR_CLEAR_VALUE, r->clear_color);
		glGetIntegerv(GL_DRAW_BUFFER, &r->draw_buffer);
		glGetIntegerv(GL_LOGIC_OP_MODE, &r->logic_op);
	}
	if (mask & GL_PIXEL_MODE_BIT) {
		glGetIntegerv(GL_READ_BUFFER, &r->read_buffer);
	}
	if (mask & GL_DEPTH_BUFFER_BIT) {
		glGetIntegerv(GL_DEPTH_FUNC, &r->depth_func);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &r->depth_mask);
		glGetDoublev(GL_DEPTH_CLEAR_VALUE, &r->clear_depth);
	}
	if (mask & GL_POLYGON_BIT) {
		glGetIntegerv(GL_CULL_FACE_MODE, &r->cull_mode);
		glGetIntegerv(GL_FRONT_FACE, &r->front_face);
		glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &r->offset_factor);
		glGetFloatv(GL_POLYGON_OFFSET_UNITS, &r->offset_units);
	}
	if (mask & GL_LINE_BIT) {
		glGetFloatv(GL_LINE_WIDTH, &r->line_width);
	}
	if (mask & GL_POINT_BIT) {
		glGetFloatv(GL_POINT_SIZE, &r->point_size);
	}
	if (mask & GL_SCISSOR_BIT) {
		glGetIntegerv(GL_SCISSOR_BOX, r->scissor);
	}
	if (mask & GL_VIEWPORT_BIT) {
		glGetIntegerv(GL_VIEWPORT, r->viewport);
		glGetDoublev(GL_DEPTH_RANGE, r->depth_range);
	}
	if (mask & GL_STENCIL_BUFFER_BIT) {
		glGetIntegerv(GL_STENCIL_FUNC, &r->st_func);
		glGetIntegerv(GL_STENCIL_REF, &r->st_ref);
		glGetIntegerv(GL_STENCIL_VALUE_MASK, &r->st_mask);
		glGetIntegerv(GL_STENCIL_FAIL, &r->st_fail);
		glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &r->st_zfail);
		glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &r->st_zpass);
		glGetIntegerv(GL_STENCIL_WRITEMASK, &r->st_write);
		glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &r->st_clear);
	}
	if (mask & GL_TEXTURE_BIT) {
		r->active = lgl.active_unit;
		for (int u = 0; u < LGL_UNITS; u++) {
			r->tex2d[u] = lgl.unit[u].bound_2d;
			r->texrect[u] = lgl.unit[u].bound_rect;
			r->texcube[u] = lgl.unit[u].bound_cube;
		}
	}
}

static void real_enable(const LglReal *r, GLenum cap)
{
	for (int i = 0; i < r->nen; i++) {
		if (r->en[i].cap == cap) {
			if (r->en[i].on) glEnable(cap); else glDisable(cap);
			return;
		}
	}
}

static void real_restore(const LglReal *r, GLbitfield mask)
{
	if (mask & GL_ENABLE_BIT) {
		for (int i = 0; i < r->nen; i++) {
			if (r->en[i].on) glEnable(r->en[i].cap); else glDisable(r->en[i].cap);
		}
	}
	if (mask & GL_COLOR_BUFFER_BIT) {
		real_enable(r, GL_BLEND);
		real_enable(r, GL_DITHER);
		real_enable(r, GL_COLOR_LOGIC_OP);
		glBlendFuncSeparate((GLenum)r->blend_src_rgb, (GLenum)r->blend_dst_rgb, (GLenum)r->blend_src_a,
		                    (GLenum)r->blend_dst_a);
		glBlendEquationSeparate((GLenum)r->blend_eq_rgb, (GLenum)r->blend_eq_a);
		glBlendColor(r->blend_color[0], r->blend_color[1], r->blend_color[2], r->blend_color[3]);
		glColorMask(r->color_mask[0], r->color_mask[1], r->color_mask[2], r->color_mask[3]);
		glClearColor(r->clear_color[0], r->clear_color[1], r->clear_color[2], r->clear_color[3]);
		glLogicOp((GLenum)r->logic_op);
		GLint fb = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
		if (r->draw_buffer) {
			glDrawBuffer((GLenum)r->draw_buffer);
		}
	}
	if ((mask & GL_PIXEL_MODE_BIT) && r->read_buffer) {
		glReadBuffer((GLenum)r->read_buffer);
	}
	if (mask & GL_DEPTH_BUFFER_BIT) {
		real_enable(r, GL_DEPTH_TEST);
		glDepthFunc((GLenum)r->depth_func);
		glDepthMask(r->depth_mask);
		glClearDepth(r->clear_depth);
	}
	if (mask & GL_POLYGON_BIT) {
		real_enable(r, GL_CULL_FACE);
		real_enable(r, GL_POLYGON_OFFSET_FILL);
		real_enable(r, GL_POLYGON_OFFSET_LINE);
		real_enable(r, GL_POLYGON_OFFSET_POINT);
		real_enable(r, GL_POLYGON_SMOOTH);
		glCullFace((GLenum)r->cull_mode);
		glFrontFace((GLenum)r->front_face);
		glPolygonOffset(r->offset_factor, r->offset_units);
		glPolygonMode(GL_FRONT_AND_BACK, lgl.poly_mode[0]);
	}
	if (mask & GL_LINE_BIT) {
		real_enable(r, GL_LINE_SMOOTH);
		glLineWidth(r->line_width);
	}
	if (mask & GL_POINT_BIT) {
		glPointSize(r->point_size);
	}
	if (mask & GL_SCISSOR_BIT) {
		real_enable(r, GL_SCISSOR_TEST);
		glScissor(r->scissor[0], r->scissor[1], r->scissor[2], r->scissor[3]);
	}
	if (mask & GL_VIEWPORT_BIT) {
		glViewport(r->viewport[0], r->viewport[1], r->viewport[2], r->viewport[3]);
		glDepthRange(r->depth_range[0], r->depth_range[1]);
	}
	if (mask & GL_STENCIL_BUFFER_BIT) {
		real_enable(r, GL_STENCIL_TEST);
		glStencilFunc((GLenum)r->st_func, r->st_ref, (GLuint)r->st_mask);
		glStencilOp((GLenum)r->st_fail, (GLenum)r->st_zfail, (GLenum)r->st_zpass);
		glStencilMask((GLuint)r->st_write);
		glClearStencil(r->st_clear);
	}
	if (mask & GL_MULTISAMPLE_BIT) {
		real_enable(r, GL_MULTISAMPLE);
	}
	if (mask & GL_TEXTURE_BIT) {
		for (int u = 0; u < LGL_UNITS; u++) {
			glActiveTexture(GL_TEXTURE0 + u);
			glBindTexture(GL_TEXTURE_2D, r->tex2d[u]);
			glBindTexture(GL_TEXTURE_RECTANGLE, r->texrect[u]);
			glBindTexture(GL_TEXTURE_CUBE_MAP, r->texcube[u]);
		}
		glActiveTexture(GL_TEXTURE0 + (GLenum)r->active);
	}
}

void GPU_lgl_PushAttrib(GLbitfield mask)
{
	const unsigned int e = mask;
	LGL_RECORD(LOP_PUSHATTRIB, &e, 1, NULL, 0);
	if (!lgl_attrib_stack) {
		lgl_attrib_stack = MEM_callocN(sizeof(LglAttrib) * LGL_ATTRIBSTACK, "lgl attribute stack");
	}
	if (lgl_attrib_top >= LGL_ATTRIBSTACK) {
		lgl_note_once("the attribute stack is full");
		return;
	}
	LglAttrib *a = &lgl_attrib_stack[lgl_attrib_top++];
	a->mask = mask;
	memcpy(&a->s, &lgl, sizeof(LglState));
	real_save(&a->r, mask);
}

void GPU_lgl_PopAttrib(void)
{
	LGL_RECORD(LOP_POPATTRIB, NULL, 0, NULL, 0);
	if (lgl_attrib_top <= 0) {
		lgl_note_once("glPopAttrib with an empty attribute stack");
		return;
	}
	const LglAttrib *a = &lgl_attrib_stack[--lgl_attrib_top];
	const GLbitfield mask = a->mask;
	const LglState *s = &a->s;

	if (mask & GL_CURRENT_BIT) {
		memcpy(lgl.color, s->color, sizeof(lgl.color));
		memcpy(lgl.normal, s->normal, sizeof(lgl.normal));
		memcpy(lgl.tex, s->tex, sizeof(lgl.tex));
		memcpy(lgl.raster, s->raster, sizeof(lgl.raster));
		memcpy(lgl.raster_color, s->raster_color, sizeof(lgl.raster_color));
		memcpy(lgl.raster_tex, s->raster_tex, sizeof(lgl.raster_tex));
		lgl.raster_valid = s->raster_valid;
		lgl.edge = s->edge;
	}
	if (mask & GL_ENABLE_BIT) {
		lgl.en = s->en;
		lgl.light_on = s->light_on;
		for (int u = 0; u < LGL_UNITS; u++) {
			lgl.unit[u].en = s->unit[u].en;
		}
		for (int i = 0; i < LGL_CLIPS; i++) {
			const int on = (s->clip_on >> i) & 1;
			if (on) glEnable(GL_CLIP_DISTANCE0 + i); else glDisable(GL_CLIP_DISTANCE0 + i);
		}
		lgl.clip_on = s->clip_on;
	}
	if (mask & GL_LIGHTING_BIT) {
		memcpy(lgl.lights, s->lights, sizeof(lgl.lights));
		memcpy(lgl.mat, s->mat, sizeof(lgl.mat));
		memcpy(lgl.scene_amb, s->scene_amb, sizeof(lgl.scene_amb));
		lgl.two_side = s->two_side;
		lgl.local_viewer = s->local_viewer;
		lgl.separate_spec = s->separate_spec;
		lgl.cm_face = s->cm_face;
		lgl.cm_mode = s->cm_mode;
		lgl.shade_model = s->shade_model;
		lgl.light_on = s->light_on;
		lgl.en = (lgl.en & ~(E_LIGHTING | E_COLOR_MATERIAL)) | (s->en & (E_LIGHTING | E_COLOR_MATERIAL));
	}
	if (mask & GL_COLOR_BUFFER_BIT) {
		lgl.alpha_func = s->alpha_func;
		lgl.alpha_ref = s->alpha_ref;
		lgl.en = (lgl.en & ~(E_ALPHA_TEST | E_LOGIC_INDEX)) | (s->en & (E_ALPHA_TEST | E_LOGIC_INDEX));
	}
	if (mask & GL_POLYGON_BIT) {
		memcpy(lgl.poly_mode, s->poly_mode, sizeof(lgl.poly_mode));
		lgl.en = (lgl.en & ~E_POLYGON_STIPPLE) | (s->en & E_POLYGON_STIPPLE);
	}
	if (mask & GL_POLYGON_STIPPLE_BIT) {
		memcpy(lgl.poly_stipple, s->poly_stipple, sizeof(lgl.poly_stipple));
	}
	if (mask & GL_LINE_BIT) {
		lgl.line_factor = s->line_factor;
		lgl.line_pattern = s->line_pattern;
		lgl.en = (lgl.en & ~E_LINE_STIPPLE) | (s->en & E_LINE_STIPPLE);
	}
	if (mask & GL_POINT_BIT) {
		lgl.en = (lgl.en & ~(E_POINT_SMOOTH | E_POINT_SPRITE)) | (s->en & (E_POINT_SMOOTH | E_POINT_SPRITE));
	}
	if (mask & GL_FOG_BIT) {
		lgl.fog_mode = s->fog_mode;
		lgl.fog_start = s->fog_start;
		lgl.fog_end = s->fog_end;
		lgl.fog_density = s->fog_density;
		lgl.fog_index = s->fog_index;
		memcpy(lgl.fog_color, s->fog_color, sizeof(lgl.fog_color));
		lgl.en = (lgl.en & ~E_FOG) | (s->en & E_FOG);
	}
	if (mask & GL_TRANSFORM_BIT) {
		GPU_lgl_MatrixMode(s->matrix_mode);
		memcpy(lgl.clip, s->clip, sizeof(lgl.clip));
		for (int i = 0; i < LGL_CLIPS; i++) {
			const int on = (s->clip_on >> i) & 1;
			if (on) glEnable(GL_CLIP_DISTANCE0 + i); else glDisable(GL_CLIP_DISTANCE0 + i);
		}
		lgl.clip_on = s->clip_on;
		lgl.en = (lgl.en & ~(E_NORMALIZE | E_RESCALE_NORMAL)) | (s->en & (E_NORMALIZE | E_RESCALE_NORMAL));
	}
	if (mask & GL_TEXTURE_BIT) {
		for (int u = 0; u < LGL_UNITS; u++) {
			LglUnit *d = &lgl.unit[u];
			const LglUnit *o = &s->unit[u];
			const int textop = d->textop;
			float texmat[LGL_TEXSTACK][4][4];
			memcpy(texmat, d->texmat, sizeof(texmat));
			memcpy(d, o, sizeof(*d));
			/* the matrices are not part of it */
			d->textop = textop;
			memcpy(d->texmat, texmat, sizeof(texmat));
		}
		lgl.active_unit = s->active_unit;
	}
	if (mask & GL_PIXEL_MODE_BIT) {
		memcpy(lgl.px_scale, s->px_scale, sizeof(lgl.px_scale));
		memcpy(lgl.px_bias, s->px_bias, sizeof(lgl.px_bias));
		lgl.depth_scale = s->depth_scale;
		lgl.depth_bias = s->depth_bias;
		memcpy(lgl.zoom, s->zoom, sizeof(lgl.zoom));
		lgl.map_color = s->map_color;
	}
	if (mask & GL_LIST_BIT) {
		lgl.list_base = s->list_base;
	}
	if (mask & GL_EVAL_BIT) {
		lgl.en = (lgl.en & ~(E_MAP1_VERTEX_3 | E_MAP1_VERTEX_4 | E_AUTO_NORMAL)) |
		         (s->en & (E_MAP1_VERTEX_3 | E_MAP1_VERTEX_4 | E_AUTO_NORMAL));
		lgl.grid1_n = s->grid1_n;
		lgl.grid1_u1 = s->grid1_u1;
		lgl.grid1_u2 = s->grid1_u2;
	}
	real_restore(&a->r, mask);
	lgl_dirty();
}

typedef struct LglClientAttrib {
	GLbitfield mask;
	LglArray pos, col, nor, tex[LGL_UNITS], gen[LGL_GENERIC];
	int client_unit;
	unsigned int array_buffer, ebo;
	GLint store[16];
} LglClientAttrib;

static LglClientAttrib lgl_client_stack[LGL_ATTRIBSTACK];
static int lgl_client_top = 0;

static const GLenum store_names[16] = {
	GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS,
	GL_UNPACK_SKIP_PIXELS, GL_UNPACK_ALIGNMENT, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES,
	GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS,
	GL_PACK_SKIP_PIXELS, GL_PACK_ALIGNMENT, GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES,
};

void GPU_lgl_PushClientAttrib(GLbitfield mask)
{
	if (lgl_client_top >= LGL_ATTRIBSTACK) {
		lgl_note_once("the client attribute stack is full");
		return;
	}
	LglClientAttrib *a = &lgl_client_stack[lgl_client_top++];
	a->mask = mask;
	if (mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
		a->pos = lgl.arr_pos;
		a->col = lgl.arr_col;
		a->nor = lgl.arr_nor;
		memcpy(a->tex, lgl.arr_tex, sizeof(a->tex));
		memcpy(a->gen, lgl.arr_gen, sizeof(a->gen));
		a->client_unit = lgl.client_unit;
		a->array_buffer = lgl.app_array_buffer;
		a->ebo = lgl.app_ebo;
	}
	if (mask & GL_CLIENT_PIXEL_STORE_BIT) {
		for (int i = 0; i < 16; i++) {
			glGetIntegerv(store_names[i], &a->store[i]);
		}
	}
}

void GPU_lgl_PopClientAttrib(void)
{
	if (lgl_client_top <= 0) {
		lgl_note_once("glPopClientAttrib with an empty client attribute stack");
		return;
	}
	const LglClientAttrib *a = &lgl_client_stack[--lgl_client_top];
	if (a->mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
		lgl.arr_pos = a->pos;
		lgl.arr_col = a->col;
		lgl.arr_nor = a->nor;
		memcpy(lgl.arr_tex, a->tex, sizeof(a->tex));
		memcpy(lgl.arr_gen, a->gen, sizeof(a->gen));
		lgl.client_unit = a->client_unit;
		lgl.app_array_buffer = a->array_buffer;
		glBindBuffer(GL_ARRAY_BUFFER, a->array_buffer);
		lgl.app_ebo = a->ebo;
	}
	if (a->mask & GL_CLIENT_PIXEL_STORE_BIT) {
		for (int i = 0; i < 16; i++) {
			glPixelStorei(store_names[i], a->store[i]);
		}
	}
}

void lgl_state_free(void)
{
	if (lgl_attrib_stack) {
		MEM_freeN(lgl_attrib_stack);
		lgl_attrib_stack = NULL;
	}
	lgl_attrib_top = 0;
	lgl_client_top = 0;
	if (lgl_extensions) {
		free(lgl_extensions);
		lgl_extensions = NULL;
	}
}
