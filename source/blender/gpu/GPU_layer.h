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

/** \file GPU_layer.h
 *  \ingroup gpu
 *
 * BGE-Remix P8.2: the drawing layer. Everything new draws through it with
 * OpenGL 4.5 core features only: buffers edited without binding, a streaming
 * buffer for data that changes every frame, vertex formats and batches, an
 * immediate-style API in place of glBegin/glEnd, our own matrix stack, one
 * owner of OpenGL state (scopes that save and restore it, and setters), and
 * built-in shaders for the classic looks. While old drawing code remains,
 * the matrix stack is synced with the old OpenGL matrices at the borders.
 */

#ifndef __GPU_LAYER_H__
#define __GPU_LAYER_H__

#include <stddef.h>

#include "BLI_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct GPUShader;

/* ---- buffers ---- */

typedef struct GPUBuf GPUBuf;

/* immutable storage; "dynamic" allows GPU_buf_update */
GPUBuf *GPU_buf_create(size_t size, const void *data, bool dynamic, const char *label);
void GPU_buf_update(GPUBuf *buf, size_t offset, size_t size, const void *data);
void GPU_buf_free(GPUBuf *buf);
unsigned int GPU_buf_id(const GPUBuf *buf);
size_t GPU_buf_size(const GPUBuf *buf);

/* ---- streaming: data that changes every frame ---- */

/* room for "size" bytes in the streaming buffer, written by the caller and
 * read by the next draws; the buffer and the offset to draw from */
void *GPU_stream_alloc(size_t size, unsigned int *r_buffer, size_t *r_offset);
/* the end of a frame: the part written this frame is fenced */
void GPU_stream_frame_end(void);
/* after writing what GPU_stream_alloc gave: a request too large for the
 * streaming buffer got a buffer of its own, mapped until this is called */
void GPU_stream_written(unsigned int buffer);

/* ---- vertex formats ---- */

typedef enum GPUCompType {
	GPU_COMP_F32 = 0,
	GPU_COMP_U8,
	GPU_COMP_I8,
	GPU_COMP_U16,
	GPU_COMP_I16,
	GPU_COMP_U32,
	GPU_COMP_I32,
} GPUCompType;

#define GPU_VERTFORMAT_MAX 16

typedef struct GPUVertAttr {
	char name[32];
	unsigned char comp, len, normalize, integer;
	unsigned short offset;
} GPUVertAttr;

typedef struct GPUVertFormat {
	GPUVertAttr attrs[GPU_VERTFORMAT_MAX];
	int count, stride;
} GPUVertFormat;

void GPU_vertformat_clear(GPUVertFormat *format);
/* returns the attribute's index; "integer" attributes stay integers */
int GPU_vertformat_add(GPUVertFormat *format, const char *name, GPUCompType comp, int len, bool normalize);
int GPU_vertformat_add_int(GPUVertFormat *format, const char *name, GPUCompType comp, int len);

/* ---- batches: meshes kept on the card ---- */

typedef struct GPUBatch GPUBatch;

/* prim: GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_LINE_LOOP, GL_TRIANGLES,
 * GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN; indices are optional (32-bit) */
GPUBatch *GPU_batch_create(unsigned int prim, const GPUVertFormat *format,
                           const void *vertices, int vertex_count,
                           const unsigned int *indices, int index_count,
                           bool dynamic, const char *label);
/* new vertices (dynamic batches; the same count or fewer) */
void GPU_batch_update(GPUBatch *batch, const void *vertices, int vertex_count);
/* draws with the shader (bound here), its attributes matched by name */
void GPU_batch_draw(GPUBatch *batch, struct GPUShader *shader);
void GPU_batch_draw_instanced(GPUBatch *batch, struct GPUShader *shader, int instances);
void GPU_batch_free(GPUBatch *batch);

/* ---- built-in shaders ---- */

typedef enum GPULayerShader {
	GPU_BUILTIN_FLAT = 0,   /* one colour (bge_color) */
	GPU_BUILTIN_SMOOTH,     /* vertex colours */
	GPU_BUILTIN_IMAGE,      /* a texture times the vertex colour */
	GPU_BUILTIN_TEXT,       /* a glyph texture's alpha (coverage), times the colour */
	GPU_BUILTIN_LINE,       /* lines of any width, solid or dashed */
	GPU_BUILTIN_POINT,      /* points of any size, square or round */
	GPU_BUILTIN_CLASSIC,    /* the fixed-function look: per-vertex lights, texture, fog */
	GPU_BUILTIN_COUNT,
} GPULayerShader;

struct GPUShader *GPU_builtin_shader(GPULayerShader type);

/* the line style (GPU_BUILTIN_LINE): width in pixels, dash and gap lengths
 * in pixels (gap 0: solid) */
void GPU_line_style(float width, float dash, float gap);
/* the point style (GPU_BUILTIN_POINT) */
void GPU_point_style(float size, bool round);

/* the classic look (GPU_BUILTIN_CLASSIC), as fixed-function OpenGL had it */
typedef struct GPUClassicLight {
	float position[4];       /* eye space; w 0: directional */
	float ambient[4], diffuse[4], specular[4];
	float spot_direction[3]; /* eye space */
	float spot_cutoff;       /* degrees; 180: no spot */
	float spot_exponent;
	float attenuation[3];    /* constant, linear, quadratic */
} GPUClassicLight;

enum { GPU_TEXENV_MODULATE = 0, GPU_TEXENV_REPLACE, GPU_TEXENV_DECAL, GPU_TEXENV_ADD, GPU_TEXENV_BLEND };
enum { GPU_FOG_OFF = 0, GPU_FOG_LINEAR, GPU_FOG_EXP, GPU_FOG_EXP2 };

typedef struct GPUClassicState {
	bool lighting;
	int light_count;
	GPUClassicLight lights[8];
	float scene_ambient[4];
	float mat_ambient[4], mat_diffuse[4], mat_specular[4], mat_emission[4];
	float shininess;
	int vertex_color;        /* 0 off, 1 colour material (ambient and diffuse), 2 multiply */
	bool texturing;
	int texenv;              /* GPU_TEXENV_* */
	float texenv_color[4];   /* GPU_TEXENV_BLEND */
	int fog;                 /* GPU_FOG_* */
	float fog_start, fog_end, fog_density;
	float fog_color[4];
	float alpha_ref;         /* discard below this alpha (0: off) */
	/* BGE-Remix P8 Part B: what else the old built-ins held */
	bool alpha_test;         /* alpha test on (fragments pass when alpha > alpha_ref) */
	float current_color[4];  /* the colour drawn with (colour material follows it) */
	/* BGE-Remix P8 Part B (P8.17): the mist's height (0: none) and the row of
	 * the inverse view matrix that gives a point's height */
	float fog_height;
	float fog_hrow[4];
} GPUClassicState;

void GPU_classic_state_default(GPUClassicState *state);
void GPU_classic_uniforms(struct GPUShader *shader, const GPUClassicState *state);
/* BGE-Remix P8 Part B: the texture environment of unit 0 (texturing on,
 * its mode and colour) read from OpenGL; lamps, material and fog come from
 * the game's own state */
void GPU_classic_texenv_from_gl(GPUClassicState *state);
/* BGE-Remix P8.4: depth only, for the game's shadow maps (the core front
 * end: the game's batches, the layer's matrices) */
struct GPUShader *GPU_builtin_game_depth(void);

/* BGE-Remix P8 Part B: the old built-ins a game shader reads (material
 * shaders and translated custom shaders): the matrices from the layer's
 * stack, the texture matrices, and lamps, material, light model, fog and
 * alpha test from "state" (NULL: matrices only). Uniform places are found
 * once per program and frame. */
void GPU_old_builtins(unsigned int program, const GPUClassicState *state);
/* programs may be made again under the same name: places found anew */
void GPU_old_builtins_frame(void);
/* a program is going (its name may be given again) */
void GPU_old_builtins_forget(unsigned int program);

/* ---- texture matrices (the old GL_TEXTURE matrix of units 0-7) ---- */

/* m NULL: identity */
void GPU_texture_matrix_set(int unit, const float m[4][4]);
void GPU_texture_matrix_get(int unit, float r_m[4][4]);

/* ---- immediate drawing (in place of glBegin/glEnd) ---- */

/* prim may also be GL_QUADS, GL_QUAD_STRIP or GL_POLYGON (made triangles) */
void GPU_imm_begin(unsigned int prim, GPULayerShader shader);
void GPU_imm_color4f(float r, float g, float b, float a);
void GPU_imm_color4ub(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void GPU_imm_texco2f(float u, float v);
void GPU_imm_normal3f(float x, float y, float z);
void GPU_imm_vertex2f(float x, float y);
void GPU_imm_vertex3f(float x, float y, float z);
/* draws what was given since GPU_imm_begin */
void GPU_imm_end(void);
/* the colour for GPU_BUILTIN_FLAT and the texture unit for IMAGE and TEXT */
void GPU_imm_uniform_color(float r, float g, float b, float a);

/* BGE-Remix P8 Part B: one triangle covering the viewport; its corners are
 * attribute 0 (vec2, -1..3: use layout(location = 0)) */
void GPU_draw_fullscreen_triangle(void);

/* ---- speed features (BGE-Remix P8 Part B): each can be switched off
 * ("instancing", "gpu_culling", "gpu_skinning", "layered_shadows",
 * "clustered_lights": the last is read when a material is made); the environment variable
 * BGE_REMIX_NO_<NAME in capitals>=1 switches one off at start ---- */
bool GPU_speed_feature(const char *name);
/* false when the name is unknown */
bool GPU_speed_feature_set(const char *name, bool on);

/* ---- GPU time of the game's passes (BGE-Remix P8 Part B) ---- */

typedef enum GPUPassTimer {
	GPU_PASS_SHADOWS = 0,
	GPU_PASS_OPAQUE,
	GPU_PASS_TRANSPARENT,
	GPU_PASS_SKY,
	GPU_PASS_FILTERS,
	GPU_PASS_OVERLAYS,
	GPU_PASS_COUNT,
} GPUPassTimer;

/* timestamps around a pass (a pass may run several times a frame: summed) */
void GPU_pass_timer_begin(GPUPassTimer pass);
void GPU_pass_timer_end(GPUPassTimer pass);
/* the end of a frame: the results of earlier frames are collected (the
 * card answers some frames later; the game does not wait for them) */
void GPU_pass_timer_frame(void);
/* the pass's GPU time per frame in milliseconds (averaged over recent
 * frames; -1 while unknown) and its name */
float GPU_pass_timer_ms(GPUPassTimer pass);
const char *GPU_pass_timer_name(GPUPassTimer pass);

/* ---- compute (BGE-Remix P8.7) ---- */

/* a compute program from GLSL 4.50 source (the "#version" line is added);
 * 0 when it does not build (the log is printed under "label") */
unsigned int GPU_compute_program(const char *source, const char *label);
/* runs it over a width x height grid in groups of group x group (the
 * program must be bound); a barrier for image and texture reads follows */
void GPU_compute_dispatch_2d(int width, int height, int group);

/* ---- matrices ---- */

typedef enum GPUMatrixMode {
	GPU_MATRIX_MODELVIEW = 0,
	GPU_MATRIX_PROJECTION = 1,
} GPUMatrixMode;

void GPU_matrix_mode(GPUMatrixMode mode);
void GPU_matrix_push(void);
void GPU_matrix_pop(void);
void GPU_matrix_identity(void);
void GPU_matrix_load(const float m[4][4]);
void GPU_matrix_mult(const float m[4][4]);
void GPU_matrix_translate(float x, float y, float z);
void GPU_matrix_scale(float x, float y, float z);
void GPU_matrix_rotate(float degrees, float x, float y, float z);
void GPU_matrix_ortho(float left, float right, float bottom, float top, float znear, float zfar);
void GPU_matrix_frustum(float left, float right, float bottom, float top, float znear, float zfar);
void GPU_matrix_get(GPUMatrixMode mode, float r_m[4][4]);
/* the borders with the old drawing code */
void GPU_matrix_from_gl(void);
void GPU_matrix_to_gl(void);
/* sets bge_mv, bge_mvinv, bge_proj, bge_projinv, bge_mvp and bge_normalmat
 * on the bound shader, where it has them */
void GPU_matrix_uniforms(struct GPUShader *shader);

/* ---- OpenGL state ---- */

typedef enum GPULayerBlend {
	GPU_LAYER_BLEND_NONE = 0,
	GPU_LAYER_BLEND_ALPHA,          /* src alpha, one minus src alpha */
	GPU_LAYER_BLEND_ALPHA_PREMUL,   /* one, one minus src alpha */
	GPU_LAYER_BLEND_ADD,            /* src alpha, one */
	GPU_LAYER_BLEND_MULTIPLY,       /* dst colour, zero */
} GPULayerBlend;

void GPU_state_blend(GPULayerBlend blend);
void GPU_state_depth(bool test, bool write);
void GPU_state_cull(bool on, bool back);
void GPU_state_viewport(int x, int y, int w, int h);
void GPU_state_scissor(bool on, int x, int y, int w, int h);
/* BGE-Remix P8 Part B: polygon offset for filled (and line) polygons;
 * factor and units 0: off */
void GPU_state_polygon_offset(float factor, float units, bool lines);

/* a scope saves the OpenGL state everything relies on (framebuffers,
 * program, vertex array, textures, enables, blend, depth, masks, viewport,
 * scissor, and the old matrices) and puts it back at its end */
typedef struct GPUStateScope {
	int ints[48];
	float matrices[32];
	bool legacy;
} GPUStateScope;

void GPU_state_scope_begin(GPUStateScope *scope);
void GPU_state_scope_end(const GPUStateScope *scope);

#ifdef __cplusplus
}
#endif

#endif  /* __GPU_LAYER_H__ */
