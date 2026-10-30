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

#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "GPU_glew.h"
#include "GPU_layer.h"
#include "GPU_shader.h"

#include "gpu_private.h"

/* ---- sources ---- */

static const char *vert_common =
        "in vec4 bge_pos;\n"
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
        "void main() { bge_fragcolor = vec4(v_col.rgb, v_col.a * texture(bge_tex, v_texco).r); }\n";

/* lines of any width: each segment made a screen-space quad; the distance
 * along the line (in pixels) gives the dashes */
static const char *vert_line =
        "in vec4 bge_pos;\n"
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
        "in vec4 bge_pos;\n"
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
        "in vec4 bge_pos;\n"
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
        "out vec4 v_col;\n"
        "out vec2 v_texco;\n"
        "out float v_fogz;\n"
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
        "	v_texco = bge_texco;\n"
        "	v_fogz = abs(eye.z);\n"
        "	gl_Position = bge_proj * eye;\n"
        "}\n";

static const char *frag_classic =
        "in vec4 v_col;\n"
        "in vec2 v_texco;\n"
        "in float v_fogz;\n"
        "uniform sampler2D bge_tex;\n"
        "uniform int bge_cl_texturing;\n"
        "uniform int bge_cl_texenv;\n"
        "uniform vec4 bge_cl_texenv_color;\n"
        "uniform int bge_cl_fog;\n"
        "uniform vec3 bge_cl_fog_params;\n"
        "uniform vec4 bge_cl_fog_color;\n"
        "uniform float bge_cl_alpha_ref;\n"
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
        "	if (bge_cl_alpha_ref > 0.0 && c.a < bge_cl_alpha_ref) discard;\n"
        "	if (bge_cl_fog != 0) {\n"
        "		float f;\n"
        "		if (bge_cl_fog == 1) f = (bge_cl_fog_params.y - v_fogz) / max(bge_cl_fog_params.y - bge_cl_fog_params.x, 1e-8);\n"
        "		else if (bge_cl_fog == 2) f = exp(-bge_cl_fog_params.z * v_fogz);\n"
        "		else f = exp(-(bge_cl_fog_params.z * v_fogz) * (bge_cl_fog_params.z * v_fogz));\n"
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
	state->fog_end = 1.0f;
	state->fog_density = 1.0f;
}

static void uniform_v4(GLuint program, const char *name, const float v[4])
{
	const GLint loc = glGetUniformLocation(program, name);
	if (loc >= 0) {
		glProgramUniform4fv(program, loc, 1, v);
	}
}

static void uniform_i(GLuint program, const char *name, int v)
{
	const GLint loc = glGetUniformLocation(program, name);
	if (loc >= 0) {
		glProgramUniform1i(program, loc, v);
	}
}

static void uniform_f(GLuint program, const char *name, float v)
{
	const GLint loc = glGetUniformLocation(program, name);
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
		const GLint loc = glGetUniformLocation(program, "bge_cl_fog_params");
		if (loc >= 0) {
			glProgramUniform3f(program, loc, st->fog_start, st->fog_end, st->fog_density);
		}
	}
	uniform_v4(program, "bge_cl_fog_color", st->fog_color);
	uniform_f(program, "bge_cl_alpha_ref", st->alpha_ref);
}

void gpu_builtin_exit(void)
{
	for (int i = 0; i < GPU_BUILTIN_COUNT; i++) {
		if (gpu_builtins[i]) {
			GPU_shader_free(gpu_builtins[i]);
			gpu_builtins[i] = NULL;
		}
		gpu_builtin_failed[i] = false;
	}
}
