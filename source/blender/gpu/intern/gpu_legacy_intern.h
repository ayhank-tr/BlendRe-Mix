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

/** \file blender/gpu/intern/gpu_legacy_intern.h
 *  \ingroup gpu
 *
 * BGE-Remix P8.26: the OpenGL emulation's state, shared by gpu_legacy.c
 * (drawing), gpu_legacy_state.c (the fixed-function state and its stacks)
 * and gpu_legacy_raster.c (raster operations, display lists, evaluators).
 */

#ifndef __GPU_LEGACY_INTERN_H__
#define __GPU_LEGACY_INTERN_H__

#include <stddef.h>

#include "gpu_legacy_private.h"

#define LGL_UNITS 8          /* texture units with matrices and bindings */
#define LGL_FF_UNITS 4       /* units the fixed-function look samples */
#define LGL_GENERIC 16       /* generic attributes */
#define LGL_LIGHTS 8
#define LGL_CLIPS 6
#define LGL_TEXSTACK 8       /* texture matrix stack depth */
#define LGL_ATTRIBSTACK 16
#define LGL_PX_UNIT 15       /* the unit the emulation's own pixel textures use */

/* attribute sources by meaning (also the locations in the emulation's program) */
enum {
	S_POS = 0, S_COL, S_NOR, S_TEX0, S_TEX1, S_TEX2, S_TEX3, S_DIST,
	S_FIXED
};

/* per-vertex slots of immediate mode besides the position: a slot is kept
 * only when its value changes within the block (otherwise the block draws
 * with the value as a constant) */
#define X_COL 0
#define X_NOR 1
#define X_TEX0 2             /* texture units 0-3: X_TEX0 + unit */
#define X_TEX1 3
#define X_GEN0 6
#define X_SLOTS (X_GEN0 + LGL_GENERIC)

/* feature bits of the fixed-function look (gpu_shader_legacy.glsl) */
enum {
	F_LIGHTING = 1, F_TWO_SIDE = 2, F_LOCAL_VIEWER = 4, F_NORMALIZE = 8, F_FLAT = 16,
	F_POINT_SMOOTH = 32, F_POLY_STIPPLE = 64, F_COLOR_MAT = 128, F_FOG = 256,
	F_LINE_STIPPLE = 512, F_ALPHA_TEST = 1024,
};

/* the enables the emulation keeps (bits of lgl.en) */
enum {
	E_LIGHTING = (1 << 0), E_COLOR_MATERIAL = (1 << 1), E_NORMALIZE = (1 << 2), E_RESCALE_NORMAL = (1 << 3),
	E_FOG = (1 << 4), E_ALPHA_TEST = (1 << 5), E_LINE_STIPPLE = (1 << 6), E_POLYGON_STIPPLE = (1 << 7),
	E_POINT_SMOOTH = (1 << 8), E_MAP1_VERTEX_3 = (1 << 9), E_MAP1_VERTEX_4 = (1 << 10),
	E_AUTO_NORMAL = (1 << 11), E_COLOR_SUM = (1 << 12), E_POINT_SPRITE = (1 << 13),
	E_LOGIC_INDEX = (1 << 14),
};

/* per unit enables (lgl.unit[u].en) */
enum {
	U_1D = 1, U_2D = 2, U_3D = 4, U_CUBE = 8, U_RECT = 16,
	U_GEN_S = 32, U_GEN_T = 64, U_GEN_R = 128, U_GEN_Q = 256,
};

/* the std140 block of the fixed-function look */
typedef struct LglBlock {
	float mvp[16], mv[16], nmat[16];
	float texmat[LGL_FF_UNITS][16];
	float clip[LGL_CLIPS][4];
	float l_pos[8][4], l_amb[8][4], l_dif[8][4], l_spec[8][4], l_spot[8][4], l_att[8][4];
	float m_emi[2][4], m_amb[2][4], m_dif[2][4], m_spec[2][4];
	float m_shin[4];
	float scene_amb[4];
	int flags[4];
	int modes[4];
	float fogv[4];
	float fog_color[4];
	int tex_on[4];
	int env[4][4];
	int env_src[4][4];
	float env_color[4][4];
	float env_scale[4][4];
	int gen[4][4];
	float gen_obj[16][4];
	float gen_eye[16][4];
	unsigned int pstip[8][4];
	float px_scale[4];
	float px_bias[4];
} LglBlock;

typedef struct LglArray {
	int enabled;
	int size;
	unsigned int type;
	int normalized, integer;
	int stride;           /* as given (0: packed) */
	const void *pointer;  /* or the offset in "buffer" */
	unsigned int buffer;  /* the array buffer bound when it was given */
	unsigned int divisor;
} LglArray;

typedef struct LglLight {
	float amb[4], dif[4], spec[4], pos[4];  /* pos: eye space */
	float spot_dir[3];                      /* eye space */
	float spot_exp, spot_cut;
	float att[3];
} LglLight;

typedef struct LglMaterial {
	float amb[4], dif[4], spec[4], emi[4];
	float shin;
} LglMaterial;

typedef struct LglUnit {
	int en;                      /* U_* */
	/* the texture environment */
	int env_mode;                /* GL_MODULATE ... GL_COMBINE */
	float env_color[4];
	int comb_rgb, comb_alpha;    /* GL enums */
	int src_rgb[3], src_alpha[3], op_rgb[3], op_alpha[3];
	float rgb_scale, alpha_scale;
	int coord_replace;
	/* texture coordinate generation (s, t, r, q) */
	int gen_mode[4];
	float gen_obj[4][4], gen_eye[4][4];
	/* the texture matrix stack (the top goes to the layer) */
	float texmat[LGL_TEXSTACK][4][4];
	int textop;
	/* bound textures */
	unsigned int bound_2d, bound_rect, bound_cube, bound_1d, bound_3d;
} LglUnit;

typedef struct LglState {
	int init;

	/* current values */
	float color[4], normal[3], tex[LGL_UNITS][4], generic[LGL_GENERIC][4];
	float edge;

	/* immediate mode */
	int in_begin;
	unsigned int prim;
	float *verts;                /* the positions, 4 floats each */
	float *xs[X_SLOTS];          /* the slots, 4 floats each */
	float *dist;                 /* line stipple distances, 1 float each */
	int nverts, cap;
	unsigned int xused;          /* slots changed in this block */
	int pos_w;                   /* a position in this block has w other than 1 */
	int color_ub;                /* the current colour came as bytes */
	int col_ub;                  /* every colour of this block came as bytes */
	int flush_overlap;           /* a large block is drawn as it comes (-1: only at its end); the
	                              * vertices a strip's next part starts with */
	float dist_start;            /* a stippled strip's distance where its next part starts */
	/* GPU_draw_stats */
	double stat_draws, stat_verts, stat_bytes, stat_pieces;

	/* client arrays (VAO 0 as the application sees it) */
	LglArray arr_pos, arr_col, arr_nor, arr_tex[LGL_UNITS];
	LglArray arr_gen[LGL_GENERIC];
	int client_unit;

	/* bindings as the application sees them */
	unsigned int app_vao, app_ebo, app_array_buffer, app_program;
	int active_unit;

	/* the fixed-function state */
	unsigned int en;
	unsigned int light_on;
	unsigned int clip_on;
	LglLight lights[LGL_LIGHTS];
	LglMaterial mat[2];
	float scene_amb[4];
	int two_side, local_viewer, separate_spec;
	unsigned int cm_face, cm_mode;
	unsigned int shade_model;
	unsigned int alpha_func;
	float alpha_ref;
	unsigned int fog_mode;
	float fog_start, fog_end, fog_density, fog_color[4], fog_index;
	int line_factor;
	unsigned short line_pattern;
	unsigned char poly_stipple[128];
	double clip[LGL_CLIPS][4];   /* eye space */
	unsigned int matrix_mode;
	LglUnit unit[LGL_UNITS];
	unsigned int poly_mode[2];   /* front, back */

	/* pixels */
	float px_scale[4], px_bias[4], depth_scale, depth_bias;
	float zoom[2];
	int map_color;
	float raster[4];             /* window x, y, z, clip w */
	int raster_valid;
	float raster_color[4];
	float raster_tex[4];

	/* evaluators (one dimension) */
	float map1_u1, map1_u2;
	int map1_order, map1_size;
	float map1_points[64 * 4];
	int grid1_n;
	float grid1_u1, grid1_u2;

	/* selection (the old GL_SELECT: nothing is recorded) */
	unsigned int render_mode;

	/* display lists */
	struct LglList *list;        /* being compiled */
	unsigned int list_name, list_mode, list_base;
	int list_depth;

	/* the state the fixed-function look was last given */
	int dirty;
	float last_mv[16], last_proj[16], last_texmat[LGL_FF_UNITS][16];
	LglBlock block;
	int block_valid;
	/* where the block was last written in the streaming buffer */
	unsigned int block_buf, block_gen;
	size_t block_off;
	double stat_blocks;
} LglState;

extern LglState lgl;

/* gpu_legacy.c */
void lgl_flush_note(const char *what);
void lgl_note_once(const char *what);
void lgl_dirty(void);
void lgl_current_mvp(float r_mvp[16]);
void lgl_viewport(int r_vp[4]);
float lgl_point_size(void);
void lgl_draw_quad_pixels(int mode, unsigned int texture, const float corners[4][4], const float uv[4][2],
                          const float color[4]);
int lgl_tex_base_format(unsigned int texture);
void lgl_tex_set_base_format(unsigned int texture, int base);
void lgl_array_element(int i);
void lgl_build_block(LglBlock *b);

/* gpu_legacy_state.c */
void lgl_state_defaults(void);
int lgl_cap_enable(unsigned int cap, int on);   /* 1: the emulation's; 0: OpenGL's */
int lgl_cap_get(unsigned int cap, int *r_on);
int lgl_get(unsigned int pname, double *r_v, int *r_n);
void lgl_classic_state(struct GPUClassicState *st);
void lgl_matrix_changed(void);
void lgl_color_changed(void);
unsigned int lgl_unit15_texture(void);
void lgl_state_free(void);

/* gpu_legacy_raster.c */
int lgl_list_record_begin(int op, const unsigned int *e, int ne, const float *f, int nf);
void lgl_lists_free(void);

/* list operations (gpu_legacy_raster.c records, plays back through the GPU_lgl_ functions) */
enum {
	LOP_BEGIN = 1, LOP_END, LOP_VERTEX, LOP_COLOR, LOP_NORMAL, LOP_TEXCOORD, LOP_ATTRIB,
	LOP_ENABLE, LOP_DISABLE, LOP_MATRIXMODE, LOP_LOADIDENTITY, LOP_LOADMATRIX, LOP_MULTMATRIX,
	LOP_PUSHMATRIX, LOP_POPMATRIX, LOP_MATERIAL, LOP_LIGHT, LOP_LIGHTMODEL, LOP_SHADEMODEL,
	LOP_CALLLIST, LOP_BINDTEXTURE, LOP_COLORMATERIAL, LOP_LINESTIPPLE, LOP_POLYGONMODE,
	LOP_TEXENV, LOP_NORMALSET, LOP_RASTERPOS, LOP_ALPHAFUNC, LOP_PUSHATTRIB, LOP_POPATTRIB,
	LOP_FOG, LOP_TEXGEN, LOP_POLYGONSTIPPLE, LOP_CLIPPLANE,
};

/* while a list is compiled: record, and return from the call when it is
 * GL_COMPILE only */
#define LGL_RECORD(op, e, ne, f, nf) \
	if (lgl.list && lgl_list_record_begin(op, e, ne, f, nf)) { \
		return; \
	} (void)0

#endif  /* __GPU_LEGACY_INTERN_H__ */
