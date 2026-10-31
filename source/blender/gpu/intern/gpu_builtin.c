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

/** \file blender/gpu/intern/gpu_builtin.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.2: the drawing layer's built-in shaders (GLSL 4.50 core).
 * Inputs: bge_pos, bge_col, bge_texco, bge_nor; matrices from the layer's
 * stack (bge_mvp, bge_mv, bge_proj, bge_normalmat).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_lights.h"
#include "GPU_shader.h"

#include "gpu_private.h"

/* the old built-ins' places (below) */
static void old_builtins_free(void);

/* ---- sources ---- */

static const char *vert_common =
        "layout(location = 0) in vec4 bge_pos;\n"
        "in vec4 bge_col;\n"
        "in vec2 bge_texco;\n"
        "uniform mat4 bge_mvp;\n"
        "out vec4 v_col;\n"
        "out vec2 v_texco;\n"
        "void main()\n"
        "{\n"
        "	gl_Position = bge_mvp * bge_pos;\n"
        "	v_col = bge_col;\n"
        "	v_texco = bge_texco;\n"
        "}\n";

static const char *frag_flat =
        "uniform vec4 bge_color;\n"
        "out vec4 bge_fragcolor;\n"
        "void main() { bge_fragcolor = bge_color; }\n";

static const char *frag_smooth =
        "in vec4 v_col;\n"
        "out vec4 bge_fragcolor;\n"
        "void main() { bge_fragcolor = v_col; }\n";

static const char *frag_image =
        "in vec4 v_col;\n"
        "in vec2 v_texco;\n"
        "uniform sampler2D bge_tex;\n"
        "out vec4 bge_fragcolor;\n"
        "void main() { bge_fragcolor = texture(bge_tex, v_texco) * v_col; }\n";

static const char *frag_text =
        "in vec4 v_col;\n"
        "in vec2 v_texco;\n"
        "uniform sampler2D bge_tex;\n"
        "out vec4 bge_fragcolor;\n"
        "void main() { bge_fragcolor = vec4(v_col.rgb, v_col.a * texture(bge_tex, v_texco).a); }\n";

/* lines of any width: each segment made a screen-space quad; the distance
 * along the line (in pixels) gives the dashes */
static const char *vert_line =
        "layout(location = 0) in vec4 bge_pos;\n"
        "in vec4 bge_col;\n"
        "uniform mat4 bge_mvp;\n"
        "out vec4 l_col;\n"
        "void main() { gl_Position = bge_mvp * bge_pos; l_col = bge_col; }\n";

static const char *geom_line =
        "layout(lines) in;\n"
        "layout(triangle_strip, max_vertices = 4) out;\n"
        "uniform vec2 bge_viewport;\n"
        "uniform float bge_line_width;\n"
        "in vec4 l_col[];\n"
        "out vec4 g_col;\n"
        "noperspective out float g_dist;\n"
        "void main()\n"
        "{\n"
        "	vec4 p0 = gl_in[0].gl_Position, p1 = gl_in[1].gl_Position;\n"
        "	vec2 half_vp = 0.5 * bge_viewport;\n"
        "	vec2 s0 = p0.xy / p0.w * half_vp, s1 = p1.xy / p1.w * half_vp;\n"
        "	vec2 d = s1 - s0;\n"
        "	float len = length(d);\n"
        "	vec2 dir = (len > 0.0) ? d / len : vec2(1.0, 0.0);\n"
        "	vec2 n = vec2(-dir.y, dir.x) * (0.5 * bge_line_width) / half_vp;\n"
        "	g_col = l_col[0]; g_dist = 0.0;\n"
        "	gl_Position = vec4(p0.xy + n * p0.w, p0.zw); EmitVertex();\n"
        "	g_col = l_col[0]; g_dist = 0.0;\n"
        "	gl_Position = vec4(p0.xy - n * p0.w, p0.zw); EmitVertex();\n"
        "	g_col = l_col[1]; g_dist = len;\n"
        "	gl_Position = vec4(p1.xy + n * p1.w, p1.zw); EmitVertex();\n"
        "	g_col = l_col[1]; g_dist = len;\n"
        "	gl_Position = vec4(p1.xy - n * p1.w, p1.zw); EmitVertex();\n"
        "	EndPrimitive();\n"
        "}\n";

static const char *frag_line =
        "in vec4 g_col;\n"
        "noperspective in float g_dist;\n"
        "uniform vec2 bge_dash;\n"
        "out vec4 bge_fragcolor;\n"
        "void main()\n"
        "{\n"
        "	if (bge_dash.y > 0.0 && mod(g_dist, bge_dash.x + bge_dash.y) > bge_dash.x) discard;\n"
        "	bge_fragcolor = g_col;\n"
        "}\n";

static const char *vert_point =
        "layout(location = 0) in vec4 bge_pos;\n"
        "in vec4 bge_col;\n"
        "uniform mat4 bge_mvp;\n"
        "uniform float bge_point_size;\n"
        "out vec4 v_col;\n"
        "void main() { gl_Position = bge_mvp * bge_pos; gl_PointSize = bge_point_size; v_col = bge_col; }\n";

static const char *frag_point =
        "in vec4 v_col;\n"
        "uniform int bge_point_round;\n"
        "out vec4 bge_fragcolor;\n"
        "void main()\n"
        "{\n"
        "	if (bge_point_round != 0 && length(gl_PointCoord - vec2(0.5)) > 0.5) discard;\n"
        "	bge_fragcolor = v_col;\n"
        "}\n";

/* the fixed-function look: lighting per vertex as OpenGL 1.x computed it
 * (infinite viewer, one-sided), colour material, texture environment, fog
 * from the eye distance, alpha test */
static const char *vert_classic =
        "layout(location = 0) in vec4 bge_pos;\n"
        "in vec3 bge_nor;\n"
        "in vec4 bge_col;\n"
        "in vec2 bge_texco;\n"
        "uniform mat4 bge_mv;\n"
        "uniform mat4 bge_proj;\n"
        "uniform mat3 bge_normalmat;\n"
        "struct ClassicLight { vec4 position; vec4 ambient; vec4 diffuse; vec4 specular; vec4 spot; vec4 att; };\n"
        "uniform ClassicLight bge_cl_lights[8];\n"
        "uniform int bge_cl_count;\n"
        "uniform int bge_cl_lighting;\n"
        "uniform int bge_cl_vcol;\n"
        "uniform vec4 bge_cl_scene_ambient;\n"
        "uniform vec4 bge_cl_mat_ambient;\n"
        "uniform vec4 bge_cl_mat_diffuse;\n"
        "uniform vec4 bge_cl_mat_specular;\n"
        "uniform vec4 bge_cl_mat_emission;\n"
        "uniform float bge_cl_shininess;\n"
        "uniform mat4 bge_cl_texmat;\n"
        "out vec4 v_col;\n"
        "out vec2 v_texco;\n"
        "out float v_fogz;\n"
        "out float v_fogh;\n"
        "uniform vec4 bge_cl_fog_hrow;\n"
        "void main()\n"
        "{\n"
        "	vec4 eye = bge_mv * bge_pos;\n"
        "	vec4 diffmat = (bge_cl_vcol == 1) ? bge_col : bge_cl_mat_diffuse;\n"
        "	vec4 ambmat = (bge_cl_vcol == 1) ? bge_col : bge_cl_mat_ambient;\n"
        "	vec4 c;\n"
        "	if (bge_cl_lighting == 0) {\n"
        "		c = (bge_cl_vcol != 0) ? bge_col : bge_cl_mat_diffuse;\n"
        "	}\n"
        "	else {\n"
        "		vec3 N = normalize(bge_normalmat * bge_nor);\n"
        "		c = bge_cl_mat_emission + bge_cl_scene_ambient * ambmat;\n"
        "		for (int i = 0; i < bge_cl_count; i++) {\n"
        "			ClassicLight L = bge_cl_lights[i];\n"
        "			vec3 VP;\n"
        "			float att = 1.0;\n"
        "			if (L.position.w == 0.0) {\n"
        "				VP = normalize(L.position.xyz);\n"
        "			}\n"
        "			else {\n"
        "				VP = L.position.xyz - eye.xyz;\n"
        "				float d = length(VP);\n"
        "				VP /= max(d, 1e-8);\n"
        "				att = 1.0 / max(L.att.x + L.att.y * d + L.att.z * d * d, 1e-8);\n"
        "			}\n"
        "			if (L.spot.w < 180.0) {\n"
        "				float sd = dot(-VP, normalize(L.spot.xyz));\n"
        "				att *= (sd < cos(radians(L.spot.w))) ? 0.0 : pow(max(sd, 0.0), L.att.w);\n"
        "			}\n"
        "			float ndl = max(dot(N, VP), 0.0);\n"
        "			vec4 term = L.ambient * ambmat + ndl * L.diffuse * diffmat;\n"
        "			if (ndl > 0.0) {\n"
        "				vec3 H = normalize(VP + vec3(0.0, 0.0, 1.0));\n"
        "				term += pow(max(dot(N, H), 0.0), bge_cl_shininess) * L.specular * bge_cl_mat_specular;\n"
        "			}\n"
        "			c += att * term;\n"
        "		}\n"
        "		c.a = diffmat.a;\n"
        "		c = clamp(c, 0.0, 1.0);\n"
        "	}\n"
        "	if (bge_cl_vcol == 2) c *= bge_col;\n"
        "	v_col = c;\n"
        "	v_texco = (bge_cl_texmat * vec4(bge_texco, 0.0, 1.0)).xy;\n"
        "	v_fogz = abs(eye.z);\n"
        "	v_fogh = dot(bge_cl_fog_hrow, vec4(eye.xyz, 1.0));\n"
        "	gl_Position = bge_proj * eye;\n"
        "}\n";

static const char *frag_classic =
        "in vec4 v_col;\n"
        "in vec2 v_texco;\n"
        "in float v_fogz;\n"
        "in float v_fogh;\n"
        "uniform float bge_cl_fog_height;\n"
        "uniform sampler2D bge_tex;\n"
        "uniform int bge_cl_texturing;\n"
        "uniform int bge_cl_texenv;\n"
        "uniform vec4 bge_cl_texenv_color;\n"
        "uniform int bge_cl_fog;\n"
        "uniform vec3 bge_cl_fog_params;\n"
        "uniform vec4 bge_cl_fog_color;\n"
        "uniform float bge_cl_alpha_ref;\n"
        "uniform int bge_cl_alpha_test;\n"
        "out vec4 bge_fragcolor;\n"
        "void main()\n"
        "{\n"
        "	vec4 c = v_col;\n"
        "	if (bge_cl_texturing != 0) {\n"
        "		vec4 t = texture(bge_tex, v_texco);\n"
        "		if (bge_cl_texenv == 0) c *= t;\n"
        "		else if (bge_cl_texenv == 1) c = t;\n"
        "		else if (bge_cl_texenv == 2) c.rgb = mix(c.rgb, t.rgb, t.a);\n"
        "		else if (bge_cl_texenv == 3) { c.rgb += t.rgb; c.a *= t.a; }\n"
        "		else { c.rgb = mix(c.rgb, bge_cl_texenv_color.rgb, t.rgb); c.a *= t.a; }\n"
        "	}\n"
        "	if (bge_cl_alpha_test != 0 && !(c.a > bge_cl_alpha_ref)) discard;\n"
        "	if (bge_cl_fog != 0) {\n"
        "		float f;\n"
        "		if (bge_cl_fog == 1) f = (bge_cl_fog_params.y - v_fogz) / max(bge_cl_fog_params.y - bge_cl_fog_params.x, 1e-8);\n"
        "		else if (bge_cl_fog == 2) f = exp(-bge_cl_fog_params.z * v_fogz);\n"
        "		else f = exp(-(bge_cl_fog_params.z * v_fogz) * (bge_cl_fog_params.z * v_fogz));\n"
        /* BGE-Remix P8 Part B (P8.17): none above the mist's height */
        "		if (bge_cl_fog_height != 0.0) {\n"
        "			float a = 1.0 - clamp(f, 0.0, 1.0);\n"
        "			if (v_fogh > bge_cl_fog_height) a = 0.0;\n"
        "			else if (v_fogh > 0.0) { float h = (bge_cl_fog_height - v_fogh) / bge_cl_fog_height; a *= h * h; }\n"
        "			f = 1.0 - a;\n"
        "		}\n"
        "		c.rgb = mix(bge_cl_fog_color.rgb, c.rgb, clamp(f, 0.0, 1.0));\n"
        "	}\n"
        "	bge_fragcolor = clamp(c, 0.0, 1.0);\n"
        "}\n";

/* ---- shaders ---- */

static struct GPUShader *gpu_builtins[GPU_BUILTIN_COUNT] = {NULL};
static bool gpu_builtin_failed[GPU_BUILTIN_COUNT] = {false};

static struct {
	float line_width, dash, gap;
	float point_size;
	bool point_round;
} gpu_style = {1.0f, 0.0f, 0.0f, 1.0f, false};

struct GPUShader *GPU_builtin_shader(GPULayerShader type)
{
	if (type < 0 || type >= GPU_BUILTIN_COUNT) {
		return NULL;
	}
	if (!gpu_builtins[type] && !gpu_builtin_failed[type]) {
		const char *vert = vert_common, *frag = NULL, *geom = NULL;
		switch (type) {
			case GPU_BUILTIN_FLAT: frag = frag_flat; break;
			case GPU_BUILTIN_SMOOTH: frag = frag_smooth; break;
			case GPU_BUILTIN_IMAGE: frag = frag_image; break;
			case GPU_BUILTIN_TEXT: frag = frag_text; break;
			case GPU_BUILTIN_LINE: vert = vert_line; geom = geom_line; frag = frag_line; break;
			case GPU_BUILTIN_POINT: vert = vert_point; frag = frag_point; break;
			case GPU_BUILTIN_CLASSIC: vert = vert_classic; frag = frag_classic; break;
			default: break;
		}
		gpu_builtins[type] = GPU_shader_create_ex(vert, frag, geom, NULL, NULL, 0, 0, 0,
		                                          GPU_SHADER_FLAGS_GLSL450 | GPU_SHADER_FLAGS_CORE);
		if (!gpu_builtins[type]) {
			gpu_builtin_failed[type] = true;
			fprintf(stderr, "BGE-Remix layer: built-in shader %d failed\n", (int)type);
		}
	}
	struct GPUShader *sh = gpu_builtins[type];
	if (sh) {
		const GLuint program = GPU_shader_program(sh);
		GLint loc;
		if (type == GPU_BUILTIN_LINE) {
			GLint vp[4];
			glGetIntegerv(GL_VIEWPORT, vp);
			if ((loc = glGetUniformLocation(program, "bge_viewport")) >= 0) {
				glProgramUniform2f(program, loc, (float)vp[2], (float)vp[3]);
			}
			if ((loc = glGetUniformLocation(program, "bge_line_width")) >= 0) {
				glProgramUniform1f(program, loc, gpu_style.line_width);
			}
			if ((loc = glGetUniformLocation(program, "bge_dash")) >= 0) {
				glProgramUniform2f(program, loc, gpu_style.dash, gpu_style.gap);
			}
		}
		else if (type == GPU_BUILTIN_POINT) {
			if ((loc = glGetUniformLocation(program, "bge_point_size")) >= 0) {
				glProgramUniform1f(program, loc, gpu_style.point_size);
			}
			if ((loc = glGetUniformLocation(program, "bge_point_round")) >= 0) {
				glProgramUniform1i(program, loc, gpu_style.point_round ? 1 : 0);
			}
		}
	}
	return sh;
}

void GPU_line_style(float width, float dash, float gap)
{
	gpu_style.line_width = width > 0.0f ? width : 1.0f;
	gpu_style.dash = dash;
	gpu_style.gap = gap;
}

void GPU_point_style(float size, bool round)
{
	gpu_style.point_size = size > 0.0f ? size : 1.0f;
	gpu_style.point_round = round;
}

void GPU_classic_state_default(GPUClassicState *state)
{
	memset(state, 0, sizeof(*state));
	const float amb[4] = {0.2f, 0.2f, 0.2f, 1.0f}, dif[4] = {0.8f, 0.8f, 0.8f, 1.0f};
	const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	memcpy(state->scene_ambient, amb, sizeof(amb));
	memcpy(state->mat_ambient, amb, sizeof(amb));
	memcpy(state->mat_diffuse, dif, sizeof(dif));
	memcpy(state->mat_specular, black, sizeof(black));
	memcpy(state->mat_emission, black, sizeof(black));
	for (int i = 0; i < 8; i++) {
		GPUClassicLight *l = &state->lights[i];
		l->position[2] = 1.0f;
		l->ambient[3] = l->diffuse[3] = l->specular[3] = 1.0f;
		l->spot_direction[2] = -1.0f;
		l->spot_cutoff = 180.0f;
		l->attenuation[0] = 1.0f;
	}
	for (int c = 0; c < 4; c++) {
		state->lights[0].diffuse[c] = state->lights[0].specular[c] = 1.0f;
		state->current_color[c] = 1.0f;
	}
	state->fog_end = 1.0f;
	state->fog_density = 1.0f;
}

/* BGE-Remix P8 Part B: the classic shader's uniform places, found once per
 * program (it draws every shader-less object now) */
#define CLASSIC_LOCS 96
static struct {
	GLuint program;
	int count;
	char names[CLASSIC_LOCS][40];
	GLint locs[CLASSIC_LOCS];
} gpu_classic_locs = {0};

static GLint classic_loc(GLuint program, const char *name)
{
	if (gpu_classic_locs.program != program) {
		gpu_classic_locs.program = program;
		gpu_classic_locs.count = 0;
	}
	for (int i = 0; i < gpu_classic_locs.count; i++) {
		if (STREQ(gpu_classic_locs.names[i], name)) {
			return gpu_classic_locs.locs[i];
		}
	}
	const GLint loc = glGetUniformLocation(program, name);
	if (gpu_classic_locs.count < CLASSIC_LOCS && strlen(name) < 40) {
		strcpy(gpu_classic_locs.names[gpu_classic_locs.count], name);
		gpu_classic_locs.locs[gpu_classic_locs.count++] = loc;
	}
	return loc;
}

static void uniform_v4(GLuint program, const char *name, const float v[4])
{
	const GLint loc = classic_loc(program, name);
	if (loc >= 0) {
		glProgramUniform4fv(program, loc, 1, v);
	}
}

static void uniform_i(GLuint program, const char *name, int v)
{
	const GLint loc = classic_loc(program, name);
	if (loc >= 0) {
		glProgramUniform1i(program, loc, v);
	}
}

static void uniform_f(GLuint program, const char *name, float v)
{
	const GLint loc = classic_loc(program, name);
	if (loc >= 0) {
		glProgramUniform1f(program, loc, v);
	}
}

void GPU_classic_uniforms(struct GPUShader *shader, const GPUClassicState *st)
{
	const GLuint program = GPU_shader_program(shader);
	if (!program) {
		return;
	}
	char name[64];
	const int count = st->light_count < 0 ? 0 : (st->light_count > 8 ? 8 : st->light_count);
	uniform_i(program, "bge_cl_count", count);
	uniform_i(program, "bge_cl_lighting", st->lighting ? 1 : 0);
	uniform_i(program, "bge_cl_vcol", st->vertex_color);
	uniform_v4(program, "bge_cl_scene_ambient", st->scene_ambient);
	uniform_v4(program, "bge_cl_mat_ambient", st->mat_ambient);
	uniform_v4(program, "bge_cl_mat_diffuse", st->mat_diffuse);
	uniform_v4(program, "bge_cl_mat_specular", st->mat_specular);
	uniform_v4(program, "bge_cl_mat_emission", st->mat_emission);
	uniform_f(program, "bge_cl_shininess", st->shininess);
	for (int i = 0; i < count; i++) {
		const GPUClassicLight *l = &st->lights[i];
		const float spot[4] = {l->spot_direction[0], l->spot_direction[1], l->spot_direction[2], l->spot_cutoff};
		const float att[4] = {l->attenuation[0], l->attenuation[1], l->attenuation[2], l->spot_exponent};
		snprintf(name, sizeof(name), "bge_cl_lights[%d].position", i);
		uniform_v4(program, name, l->position);
		snprintf(name, sizeof(name), "bge_cl_lights[%d].ambient", i);
		uniform_v4(program, name, l->ambient);
		snprintf(name, sizeof(name), "bge_cl_lights[%d].diffuse", i);
		uniform_v4(program, name, l->diffuse);
		snprintf(name, sizeof(name), "bge_cl_lights[%d].specular", i);
		uniform_v4(program, name, l->specular);
		snprintf(name, sizeof(name), "bge_cl_lights[%d].spot", i);
		uniform_v4(program, name, spot);
		snprintf(name, sizeof(name), "bge_cl_lights[%d].att", i);
		uniform_v4(program, name, att);
	}
	uniform_i(program, "bge_cl_texturing", st->texturing ? 1 : 0);
	uniform_i(program, "bge_cl_texenv", st->texenv);
	uniform_v4(program, "bge_cl_texenv_color", st->texenv_color);
	uniform_i(program, "bge_cl_fog", st->fog);
	{
		const GLint loc = classic_loc(program, "bge_cl_fog_params");
		if (loc >= 0) {
			glProgramUniform3f(program, loc, st->fog_start, st->fog_end, st->fog_density);
		}
	}
	uniform_v4(program, "bge_cl_fog_color", st->fog_color);
	uniform_f(program, "bge_cl_fog_height", st->fog_height);
	uniform_v4(program, "bge_cl_fog_hrow", st->fog_hrow);
	uniform_f(program, "bge_cl_alpha_ref", st->alpha_ref);
	uniform_i(program, "bge_cl_alpha_test", st->alpha_test ? 1 : 0);
	{
		const GLint loc = classic_loc(program, "bge_cl_texmat");
		if (loc >= 0) {
			float m[4][4];
			GPU_texture_matrix_get(0, m);
			glProgramUniformMatrix4fv(program, loc, 1, GL_FALSE, &m[0][0]);
		}
	}
}

/* BGE-Remix P8 Part B: unit 0's texture environment, as the game's
 * Multitexture materials set it (texturing, mode, colour) */
void GPU_classic_texenv_from_gl(GPUClassicState *st)
{
	st->texturing = false;
	st->texenv = GPU_TEXENV_MODULATE;
	if (GPU_caps()->core_profile) {
		return;
	}
	GLint active = GL_TEXTURE0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
	glActiveTexture(GL_TEXTURE0);
	st->texturing = glIsEnabled(GL_TEXTURE_2D) == GL_TRUE;
	if (st->texturing) {
		GLint mode = GL_MODULATE;
		glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &mode);
		switch (mode) {
			case GL_REPLACE: st->texenv = GPU_TEXENV_REPLACE; break;
			case GL_DECAL: st->texenv = GPU_TEXENV_DECAL; break;
			case GL_ADD: st->texenv = GPU_TEXENV_ADD; break;
			case GL_BLEND: st->texenv = GPU_TEXENV_BLEND; break;
			default: st->texenv = GPU_TEXENV_MODULATE; break;  /* modulate, combine */
		}
		glGetTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, st->texenv_color);
	}
	glActiveTexture((GLenum)active);
}

/* BGE-Remix P8.4: the game's shadow maps: depth only, from the game's
 * batches (BGE-Remix P8 Part B: the core front end, the layer's matrices) */
static const char *vert_game_depth =
        "void main() { gl_Position = bge_proj * (BGE_MV * bge_pos); }\n";
static const char *frag_game_depth =
        "void main() { bge_fragcolor = vec4(1.0); }\n";

static struct GPUShader *gpu_game_depth = NULL;
static bool gpu_game_depth_failed = false;

struct GPUShader *GPU_builtin_game_depth(void)
{
	if (!gpu_game_depth && !gpu_game_depth_failed) {
		gpu_game_depth = GPU_shader_create_ex(vert_game_depth, frag_game_depth, NULL, NULL, NULL, 0, 0, 0,
		                                      GPU_SHADER_FLAGS_GLSL450 | GPU_SHADER_FLAGS_CORE |
		                                      GPU_SHADER_FLAGS_FRONTEND);
		if (!gpu_game_depth) {
			gpu_game_depth_failed = true;
			fprintf(stderr, "BGE-Remix layer: the game's depth shader failed\n");
		}
		else {
			GPU_debug_label(GL_PROGRAM, GPU_shader_program(gpu_game_depth), "BGE-Remix game depth");
		}
	}
	return gpu_game_depth;
}

void gpu_builtin_exit(void)
{
	old_builtins_free();
	for (int i = 0; i < GPU_BUILTIN_COUNT; i++) {
		if (gpu_builtins[i]) {
			GPU_shader_free(gpu_builtins[i]);
			gpu_builtins[i] = NULL;
		}
		gpu_builtin_failed[i] = false;
	}
	if (gpu_game_depth) {
		GPU_shader_free(gpu_game_depth);
		gpu_game_depth = NULL;
	}
	gpu_game_depth_failed = false;
}

/* ------------------------------------------------------------------------ */
/* BGE-Remix P8 Part B: the old built-ins of the game's shaders, as uniforms
 * (the material front end's and the translator's core names) */

enum { OB_MV = 0, OB_PROJ, OB_MVP, OB_MVINV, OB_PROJINV, OB_MVPINV, OB_NORMALMAT, OB_MATRICES };
static const char *ob_names[OB_MATRICES] = {
	"bge_mv", "bge_proj", "bge_mvp", "bge_mvinv", "bge_projinv", "bge_mvpinv", "bge_normalmat"};

enum { LS_AMBIENT = 0, LS_DIFFUSE, LS_SPECULAR, LS_POSITION, LS_HALF, LS_SPOTDIR, LS_SPOTEXP, LS_SPOTCUT,
       LS_SPOTCOS, LS_CATT, LS_LATT, LS_QATT, LS_COUNT };
static const char *ls_names[LS_COUNT] = {
	"ambient", "diffuse", "specular", "position", "halfVector", "spotDirection", "spotExponent", "spotCutoff",
	"spotCosCutoff", "constantAttenuation", "linearAttenuation", "quadraticAttenuation"};

enum { MP_EMISSION = 0, MP_AMBIENT, MP_DIFFUSE, MP_SPECULAR, MP_SHININESS, MP_COUNT };
static const char *mp_names[MP_COUNT] = {"emission", "ambient", "diffuse", "specular", "shininess"};

enum { FOG_COLOR = 0, FOG_DENSITY, FOG_START, FOG_END, FOG_SCALE, FOG_COUNT };
static const char *fog_names[FOG_COUNT] = {"color", "density", "start", "end", "scale"};

enum { LP_AMBIENT = 0, LP_DIFFUSE, LP_SPECULAR, LP_COUNT };

typedef struct OldPlaces {
	unsigned int program;
	unsigned int frame;
	bool matrices, state;
	int mat[OB_MATRICES];
	int texmat[8];
	int light[8][LS_COUNT];
	int front[MP_COUNT], back[MP_COUNT];
	int lm_ambient, lmp_scene;
	int lprod[8][LP_COUNT];
	int fog[FOG_COUNT];
	int alphatest;
	/* BGE-Remix P8 Part B (P8.16): shadow views in one pass, the shadow
	 * pass, contact shadows' depth */
	int views, view_m, view_layer, shadowpass, scenedepth_on;
	bool pass;
} OldPlaces;

#define OLD_PLACES 512  /* a power of two */

static struct {
	OldPlaces *table;
	unsigned int frame;
} gpu_old = {NULL, 1};

void GPU_old_builtins_forget(unsigned int program)
{
	if (gpu_classic_locs.program == program) {
		gpu_classic_locs.program = 0;
		gpu_classic_locs.count = 0;
	}
	if (!gpu_old.table || !program) {
		return;
	}
	for (int i = 0; i < OLD_PLACES; i++) {
		if (gpu_old.table[i].program == program) {
			gpu_old.table[i].frame = 0;
		}
	}
}

void GPU_old_builtins_frame(void)
{
	gpu_old.frame++;
	if (gpu_old.frame == 0) {
		gpu_old.frame = 1;
	}
}

static int old_field(const char *s, const char **names, int count)
{
	for (int i = 0; i < count; i++) {
		if (STREQ(s, names[i])) {
			return i;
		}
	}
	return -1;
}

/* "prefix[i]" at s: the index, and s moved past "]" (-1: not this prefix) */
static int old_index(const char **s, const char *prefix)
{
	const size_t n = strlen(prefix);
	if (strncmp(*s, prefix, n) != 0 || (*s)[n] != '[') {
		return -1;
	}
	const char *p = *s + n + 1;
	int idx = 0;
	while (*p >= '0' && *p <= '9') {
		idx = idx * 10 + (*p - '0');
		p++;
	}
	if (*p != ']') {
		return -1;
	}
	*s = p + 1;
	return idx;
}

static void old_find(OldPlaces *op, unsigned int program)
{
	memset(op, 0xff, sizeof(*op));  /* every place -1 */
	op->program = program;
	op->frame = gpu_old.frame;
	op->matrices = op->state = op->pass = false;

	GLint linked = 0, count = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		return;
	}
	glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
	for (GLint u = 0; u < count; u++) {
		char name[128];
		GLint size = 0;
		GLenum type = 0;
		GLsizei len = 0;
		glGetActiveUniform(program, (GLuint)u, sizeof(name), &len, &size, &type, name);
		if (strncmp(name, "bge_", 4) != 0) {
			continue;
		}
		const char *s = name;
		int i, f;
		if ((f = old_field(name, ob_names, OB_MATRICES)) >= 0) {
			op->mat[f] = glGetUniformLocation(program, name);
			op->matrices = true;
		}
		else if ((i = old_index(&s, "bge_texturematrix")) >= 0) {
			/* the array: each element asked by its own name */
			char el[64];
			for (int k = 0; k < size && k < 8; k++) {
				snprintf(el, sizeof(el), "bge_texturematrix[%d]", k);
				op->texmat[k] = glGetUniformLocation(program, el);
			}
			op->matrices = true;
		}
		else if ((i = old_index(&s, "bge_lightsource")) >= 0) {
			if (i < 8 && *s == '.' && (f = old_field(s + 1, ls_names, LS_COUNT)) >= 0) {
				op->light[i][f] = glGetUniformLocation(program, name);
				op->state = true;
			}
		}
		else if ((i = old_index(&s, "bge_frontlightproduct")) >= 0) {
			if (i < 8 && *s == '.') {
				f = STREQ(s + 1, "ambient") ? LP_AMBIENT : STREQ(s + 1, "diffuse") ? LP_DIFFUSE :
				    STREQ(s + 1, "specular") ? LP_SPECULAR : -1;
				if (f >= 0) {
					op->lprod[i][f] = glGetUniformLocation(program, name);
					op->state = true;
				}
			}
		}
		else if (strncmp(name, "bge_frontmaterial.", 18) == 0) {
			if ((f = old_field(name + 18, mp_names, MP_COUNT)) >= 0) {
				op->front[f] = glGetUniformLocation(program, name);
				op->state = true;
			}
		}
		else if (strncmp(name, "bge_backmaterial.", 17) == 0) {
			if ((f = old_field(name + 17, mp_names, MP_COUNT)) >= 0) {
				op->back[f] = glGetUniformLocation(program, name);
				op->state = true;
			}
		}
		else if (STREQ(name, "bge_lightmodel.ambient")) {
			op->lm_ambient = glGetUniformLocation(program, name);
			op->state = true;
		}
		else if (STREQ(name, "bge_frontlightmodelproduct.sceneColor")) {
			op->lmp_scene = glGetUniformLocation(program, name);
			op->state = true;
		}
		else if (strncmp(name, "bge_fog.", 8) == 0) {
			if ((f = old_field(name + 8, fog_names, FOG_COUNT)) >= 0) {
				op->fog[f] = glGetUniformLocation(program, name);
				op->state = true;
			}
		}
		else if (STREQ(name, "bge_alphatest")) {
			op->alphatest = glGetUniformLocation(program, name);
			op->state = true;
		}
		else if (STREQ(name, "bge_views")) {
			op->views = glGetUniformLocation(program, name);
			op->pass = true;
		}
		else if (STREQ(name, "bge_view_m[0]") || STREQ(name, "bge_view_m")) {
			op->view_m = glGetUniformLocation(program, "bge_view_m[0]");
			op->pass = true;
		}
		else if (STREQ(name, "bge_view_layer")) {
			op->view_layer = glGetUniformLocation(program, name);
			op->pass = true;
		}
		else if (STREQ(name, "bge_shadowpass")) {
			op->shadowpass = glGetUniformLocation(program, name);
			op->pass = true;
		}
		else if (STREQ(name, "bge_scenedepth_on")) {
			op->scenedepth_on = glGetUniformLocation(program, name);
			op->pass = true;
		}
	}
}

static OldPlaces *old_places(unsigned int program)
{
	if (!gpu_old.table) {
		gpu_old.table = MEM_callocN(sizeof(OldPlaces) * OLD_PLACES, "BGE-Remix old built-ins");
	}
	unsigned int h = (program * 2654435761u) & (OLD_PLACES - 1);
	OldPlaces *free_slot = NULL;
	for (int n = 0; n < OLD_PLACES; n++, h = (h + 1) & (OLD_PLACES - 1)) {
		OldPlaces *op = &gpu_old.table[h];
		if (op->frame == gpu_old.frame && op->program == program) {
			return op;
		}
		if (op->frame != gpu_old.frame) {
			/* free this frame (entries are only ever added within a frame,
			 * so the search ends at the first free one) */
			free_slot = op;
			break;
		}
	}
	if (!free_slot) {
		/* full (very many programs in one frame): the first slot is reused */
		free_slot = &gpu_old.table[(program * 2654435761u) & (OLD_PLACES - 1)];
	}
	old_find(free_slot, program);
	return free_slot;
}

static void old_v4(GLuint program, int loc, const float v[4])
{
	if (loc >= 0) {
		glProgramUniform4fv(program, loc, 1, v);
	}
}

static void old_f(GLuint program, int loc, float v)
{
	if (loc >= 0) {
		glProgramUniform1f(program, loc, v);
	}
}

static void old_v3(GLuint program, int loc, const float v[3])
{
	if (loc >= 0) {
		glProgramUniform3fv(program, loc, 1, v);
	}
}

static void old_mul_v4(float r[4], const float a[4], const float b[4])
{
	for (int c = 0; c < 4; c++) {
		r[c] = a[c] * b[c];
	}
}

void GPU_old_builtins(unsigned int program, const GPUClassicState *st)
{
	if (!program) {
		return;
	}
	OldPlaces *op = old_places(program);

	if (op->pass) {
		int count = 0, layer = 0;
		const float *mats = NULL;
		gpu_lights_views_get(&count, &mats, &layer);
		if (op->views >= 0) {
			glProgramUniform1i(program, op->views, count);
		}
		if (count > 0 && op->view_m >= 0) {
			glProgramUniformMatrix4fv(program, op->view_m, count, GL_FALSE, mats);
		}
		if (op->view_layer >= 0) {
			glProgramUniform1i(program, op->view_layer, layer);
		}
		if (op->shadowpass >= 0) {
			glProgramUniform1i(program, op->shadowpass, GPU_shadow_pass() ? 1 : 0);
		}
		if (op->scenedepth_on >= 0) {
			glProgramUniform1f(program, op->scenedepth_on, gpu_scene_depth_is_on() ? 1.0f : 0.0f);
		}
	}

	if (op->matrices) {
		float mv[4][4], proj[4][4], m[4][4];
		GPU_matrix_get(GPU_MATRIX_MODELVIEW, mv);
		GPU_matrix_get(GPU_MATRIX_PROJECTION, proj);
		if (op->mat[OB_MV] >= 0) {
			glProgramUniformMatrix4fv(program, op->mat[OB_MV], 1, GL_FALSE, &mv[0][0]);
		}
		if (op->mat[OB_PROJ] >= 0) {
			glProgramUniformMatrix4fv(program, op->mat[OB_PROJ], 1, GL_FALSE, &proj[0][0]);
		}
		if (op->mat[OB_MVP] >= 0 || op->mat[OB_MVPINV] >= 0) {
			float mvp[4][4];
			gpu_layer_m4_mul(mvp, proj, mv);
			if (op->mat[OB_MVP] >= 0) {
				glProgramUniformMatrix4fv(program, op->mat[OB_MVP], 1, GL_FALSE, &mvp[0][0]);
			}
			if (op->mat[OB_MVPINV] >= 0) {
				gpu_layer_m4_invert(m, mvp);
				glProgramUniformMatrix4fv(program, op->mat[OB_MVPINV], 1, GL_FALSE, &m[0][0]);
			}
		}
		if (op->mat[OB_MVINV] >= 0 || op->mat[OB_NORMALMAT] >= 0) {
			gpu_layer_m4_invert(m, mv);
			if (op->mat[OB_MVINV] >= 0) {
				glProgramUniformMatrix4fv(program, op->mat[OB_MVINV], 1, GL_FALSE, &m[0][0]);
			}
			if (op->mat[OB_NORMALMAT] >= 0) {
				/* the transpose of the inverse's upper 3x3 */
				float n[3][3];
				for (int c = 0; c < 3; c++) {
					for (int r = 0; r < 3; r++) {
						n[c][r] = m[r][c];
					}
				}
				glProgramUniformMatrix3fv(program, op->mat[OB_NORMALMAT], 1, GL_FALSE, &n[0][0]);
			}
		}
		if (op->mat[OB_PROJINV] >= 0) {
			gpu_layer_m4_invert(m, proj);
			glProgramUniformMatrix4fv(program, op->mat[OB_PROJINV], 1, GL_FALSE, &m[0][0]);
		}
		for (int k = 0; k < 8; k++) {
			if (op->texmat[k] >= 0) {
				GPU_texture_matrix_get(k, m);
				glProgramUniformMatrix4fv(program, op->texmat[k], 1, GL_FALSE, &m[0][0]);
			}
		}
	}

	if (!st || !op->state) {
		return;
	}

	/* the material: colour material makes ambient and diffuse the colour
	 * drawn with while lighting is on */
	float ambient[4], diffuse[4];
	const bool colmat = st->lighting && st->vertex_color == 1;
	memcpy(ambient, colmat ? st->current_color : st->mat_ambient, sizeof(ambient));
	memcpy(diffuse, colmat ? st->current_color : st->mat_diffuse, sizeof(diffuse));
	for (int side = 0; side < 2; side++) {
		const int *pl = side ? op->back : op->front;
		old_v4(program, pl[MP_EMISSION], st->mat_emission);
		old_v4(program, pl[MP_AMBIENT], ambient);
		old_v4(program, pl[MP_DIFFUSE], diffuse);
		old_v4(program, pl[MP_SPECULAR], st->mat_specular);
		old_f(program, pl[MP_SHININESS], st->shininess);
	}
	old_v4(program, op->lm_ambient, st->scene_ambient);
	if (op->lmp_scene >= 0) {
		float sc[4];
		for (int c = 0; c < 3; c++) {
			sc[c] = st->mat_emission[c] + ambient[c] * st->scene_ambient[c];
		}
		sc[3] = diffuse[3];
		old_v4(program, op->lmp_scene, sc);
	}

	/* the lamps (all eight, as OpenGL kept them; the count says which are on) */
	for (int i = 0; i < 8; i++) {
		const GPUClassicLight *l = &st->lights[i];
		const int *pl = op->light[i];
		old_v4(program, pl[LS_AMBIENT], l->ambient);
		old_v4(program, pl[LS_DIFFUSE], l->diffuse);
		old_v4(program, pl[LS_SPECULAR], l->specular);
		old_v4(program, pl[LS_POSITION], l->position);
		if (pl[LS_HALF] >= 0) {
			float h[4] = {l->position[0], l->position[1], l->position[2], 0.0f};
			float len = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
			if (len > 0.0f) {
				h[0] /= len; h[1] /= len; h[2] /= len;
			}
			h[2] += 1.0f;
			len = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
			if (len > 0.0f) {
				h[0] /= len; h[1] /= len; h[2] /= len;
			}
			old_v4(program, pl[LS_HALF], h);
		}
		old_v3(program, pl[LS_SPOTDIR], l->spot_direction);
		old_f(program, pl[LS_SPOTEXP], l->spot_exponent);
		old_f(program, pl[LS_SPOTCUT], l->spot_cutoff);
		old_f(program, pl[LS_SPOTCOS], l->spot_cutoff >= 180.0f ? -1.0f :
		      cosf(l->spot_cutoff * (float)(M_PI / 180.0)));
		old_f(program, pl[LS_CATT], l->attenuation[0]);
		old_f(program, pl[LS_LATT], l->attenuation[1]);
		old_f(program, pl[LS_QATT], l->attenuation[2]);
		const int *pp = op->lprod[i];
		if (pp[LP_AMBIENT] >= 0 || pp[LP_DIFFUSE] >= 0 || pp[LP_SPECULAR] >= 0) {
			float v[4];
			old_mul_v4(v, l->ambient, ambient);
			old_v4(program, pp[LP_AMBIENT], v);
			old_mul_v4(v, l->diffuse, diffuse);
			old_v4(program, pp[LP_DIFFUSE], v);
			old_mul_v4(v, l->specular, st->mat_specular);
			old_v4(program, pp[LP_SPECULAR], v);
		}
	}

	/* fog */
	old_v4(program, op->fog[FOG_COLOR], st->fog_color);
	old_f(program, op->fog[FOG_DENSITY], st->fog_density);
	old_f(program, op->fog[FOG_START], st->fog_start);
	old_f(program, op->fog[FOG_END], st->fog_end);
	if (op->fog[FOG_SCALE] >= 0) {
		const float range = st->fog_end - st->fog_start;
		old_f(program, op->fog[FOG_SCALE], range != 0.0f ? 1.0f / range : 0.0f);
	}

	/* the alpha test: (on, reference) */
	if (op->alphatest >= 0) {
		glProgramUniform2f(program, op->alphatest, st->alpha_test ? 1.0f : 0.0f, st->alpha_ref);
	}
}

static void old_builtins_free(void)
{
	if (gpu_old.table) {
		MEM_freeN(gpu_old.table);
		gpu_old.table = NULL;
	}
}
