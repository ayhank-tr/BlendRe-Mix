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

/** \file blender/gpu/intern/gpu_volumes.c
 *  \ingroup gpu
 *
 * BGE-Remix P8 Part B (P8.19): volume materials (see GPU_volumes.h).
 */

#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_math.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_lights.h"
#include "GPU_shader.h"
#include "GPU_volumes.h"

#include "gpu_private.h"

/* the box's back faces (a camera inside still sees it), made from the
 * vertex number */
static const char *vol_vert =
        "uniform mat4 mvp;\n"
        "uniform mat4 obmat;\n"
        "uniform vec3 bbmin, bbmax;\n"
        "out vec3 v_world;\n"
        "const int idx[36] = int[36](0,3,1, 0,2,3, 4,5,7, 4,7,6, 0,1,5, 0,5,4, 2,7,3, 2,6,7, 0,6,2, 0,4,6, 1,3,7, 1,7,5);\n"
        "void main()\n"
        "{\n"
        "	int c = idx[gl_VertexID];\n"
        "	vec3 p = mix(bbmin, bbmax, vec3(float(c & 1), float((c >> 1) & 1), float((c >> 2) & 1)));\n"
        "	vec4 w = obmat * vec4(p, 1.0);\n"
        "	v_world = w.xyz;\n"
        "	gl_Position = mvp * w;\n"
        "}\n";

static const char *vol_frag_main =
        "in vec3 v_world;\n"
        "uniform mat4 view, viewinv, obinv;\n"
        "uniform vec3 bbmin, bbmax;\n"
        "uniform vec4 p0;\n"     /* density, density scale, scattering, emission */
        "uniform vec4 p1;\n"     /* emission colour, reflection */
        "uniform vec4 p2;\n"     /* transmission colour, asymmetry */
        "uniform vec4 p3;\n"     /* reflection colour, step size */
        "uniform vec4 p4;\n"     /* depth cutoff, shadeless, smoke, flame */
        "uniform vec3 smin, smax;\n"
        "uniform sampler3D smoke_density, smoke_flame;\n"
        "uniform sampler2D scenedepth;\n"
        "uniform float scenedepth_on;\n"
        "uniform vec4 viewport;\n"
        "uniform int lamps_on;\n"
        "uniform int shape;\n"
        "uniform int self_shadow, ext_shadow, srgb;\n"
        "uniform sampler2D scenecolor;\n"
        "out vec4 fragcolor;\n"
        "float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }\n"
        "vec3 to_linear(vec3 c)\n"
        "{\n"
        "	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));\n"
        "}\n"
        "vec3 to_srgb(vec3 c)\n"
        "{\n"
        "	c = max(c, vec3(0.0));\n"
        "	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));\n"
        "}\n"
        /* how far the volume goes from a point (object space) along a
         * direction (object space, a world length per unit) */
        "float exit_t(vec3 po, vec3 dl)\n"
        "{\n"
        "	if (shape == 1) {\n"
        "		vec3 h = max(0.5 * (bbmax - bbmin), vec3(1e-6));\n"
        "		vec3 q = (po - 0.5 * (bbmin + bbmax)) / h, e = dl / h;\n"
        "		float a = max(dot(e, e), 1e-12), b = dot(q, e), cq = dot(q, q) - 1.0;\n"
        "		float disc = b * b - a * cq;\n"
        "		return (disc <= 0.0) ? 0.0 : max((-b + sqrt(disc)) / a, 0.0);\n"
        "	}\n"
        "	vec3 sd = mix(vec3(-1e-8), vec3(1e-8), step(vec3(0.0), dl));\n"
        "	vec3 ia = 1.0 / (dl + sd);\n"
        "	vec3 tf = max((bbmin - po) * ia, (bbmax - po) * ia);\n"
        "	return max(min(min(tf.x, tf.y), tf.z), 0.0);\n"
        "}\n"
        /* Blender Internal's Shaded volumes: the lamp's light dimmed by the
         * volume between the point and the lamp (or the volume's edge) */
        "vec3 light_through(vec3 w, vec3 lv, float maxd, vec3 sigma)\n"
        "{\n"
        "	vec3 po = (obinv * vec4(w, 1.0)).xyz, dl = (obinv * vec4(lv, 0.0)).xyz;\n"
        "	float t = min(exit_t(po, dl), maxd);\n"
        "	if (t <= 0.0) return vec3(1.0);\n"
        "	float dens = p0.x * p0.y;\n"
        "	if (p4.z > 0.5) {\n"
        "		float s = 0.0;\n"
        "		for (int i = 0; i < 6; i++) {\n"
        "			vec3 uvw = (po + dl * t * (float(i) + 0.5) / 6.0 - smin) / max(smax - smin, vec3(1e-6));\n"
        "			if (all(greaterThanEqual(uvw, vec3(0.0))) && all(lessThanEqual(uvw, vec3(1.0))))\n"
        "				s += texture(smoke_density, uvw).r;\n"
        "		}\n"
        "		dens *= s / 6.0;\n"
        "	}\n"
        "	return exp(-dens * t * sigma);\n"
        "}\n"
        "float hg(float g, float ct)\n"
        "{\n"
        "	if (g == 0.0) return 0.07957747;\n"
        "	float t = 1.0 + g * g - 2.0 * g * ct;\n"
        "	return 0.07957747 * (1.0 - g * g) / (t * sqrt(t));\n"
        "}\n"
        "void main()\n"
        "{\n"
        "	vec3 cam = viewinv[3].xyz;\n"
        "	vec3 dir = normalize(v_world - cam);\n"
        "	vec3 o = (obinv * vec4(cam, 1.0)).xyz, d = (obinv * vec4(dir, 0.0)).xyz;\n"
        "	vec3 inv = 1.0 / d;\n"
        "	vec3 ta = (bbmin - o) * inv, tb = (bbmax - o) * inv;\n"
        "	vec3 tlo = min(ta, tb), thi = max(ta, tb);\n"
        "	float t0 = max(max(max(tlo.x, tlo.y), tlo.z), 0.0);\n"
        "	float t1 = min(min(thi.x, thi.y), thi.z);\n"
        /* stopped by what the scene drew */
        "	vec3 dv = mat3(view) * dir;\n"
        "	if (scenedepth_on > 0.5) {\n"
        "		vec2 uv = (gl_FragCoord.xy - viewport.xy) / viewport.zw;\n"
        "		float zd = texture(scenedepth, uv).r;\n"
        "		if (zd < 1.0 && dv.z < 0.0) t1 = min(t1, shadow_view_z(zd) / dv.z);\n"
        "	}\n"
        "	if (t1 <= t0) discard;\n"
        "	float stepl = max(p3.w, (t1 - t0) / 256.0);\n"
        "	vec3 sigma = vec3(1.0) - p2.rgb + p0.z;\n"
        "	vec3 refl = p1.w * p3.rgb * p0.z;\n"
        "	vec3 tr = vec3(1.0), rad = vec3(0.0);\n"
        "	lamp_shadow_use = float(ext_shadow);\n"
        "	for (float t = t0 + 0.5 * stepl; t < t1; t += stepl) {\n"
        "		vec3 w = cam + dir * t;\n"
        "		vec3 po = (obinv * vec4(w, 1.0)).xyz;\n"
        "		if (shape == 1) {\n"
        "			vec3 q = (po - 0.5 * (bbmin + bbmax)) / max(0.5 * (bbmax - bbmin), vec3(1e-6));\n"
        "			if (dot(q, q) > 1.0) continue;\n"
        "		}\n"
        "		float dens = p0.x * p0.y;\n"
        "		vec3 emit = p1.rgb * p0.w;\n"
        "		if (p4.z > 0.5) {\n"
        "			vec3 uvw = (po - smin) / max(smax - smin, vec3(1e-6));\n"
        "			bool inside = all(greaterThanEqual(uvw, vec3(0.0))) && all(lessThanEqual(uvw, vec3(1.0)));\n"
        "			dens *= inside ? texture(smoke_density, uvw).r : 0.0;\n"
        "			if (p4.w > 0.5 && inside) emit += vec3(1.0, 0.45, 0.12) * 4.0 * texture(smoke_flame, uvw).r;\n"
        "		}\n"
        "		if (dens <= 1e-5 && lum(emit) <= 0.0) continue;\n"
        "		float stepd = stepl * dens;\n"
        "		tr *= exp(-stepd * sigma);\n"
        "		vec3 scat = vec3(0.0);\n"
        "		if (p4.y < 0.5 && lamps_on != 0) {\n"
        "			vec3 pv = (view * vec4(w, 1.0)).xyz;\n"
        "			uvec2 tile = uvec2(clamp((gl_FragCoord.xy - cl_viewport.xy) / max(cl_viewport.zw, vec2(1.0)), vec2(0.0), vec2(0.99999)) * vec2(16.0, 9.0));\n"
        "			uint k = uint(clamp(log(max(-pv.z, cl_depth.x) / cl_depth.x) * cl_depth.z, 0.0, 23.0));\n"
        "			uint ci = (k * 9u + tile.y) * 16u + tile.x;\n"
        "			uint W = cl_counts.x;\n"
        "			for (uint wd = 0u; wd < W; wd++) {\n"
        "				uint bits = cl_mask[ci * W + wd];\n"
        "				while (bits != 0u) {\n"
        "					int b = findLSB(bits);\n"
        "					bits &= bits - 1u;\n"
        "					BgeLamp L = lamps[wd * 32u + uint(b)];\n"
        "					float halo;\n"
        "					vec3 lc = lamp_light(L, pv, w, halo);\n"
        "					int type = int(L.co_type.w + 0.5);\n"
        "					vec3 lv = (type == 1 || type == 3) ? -L.vec_dist.xyz : normalize(L.co_type.xyz - w);\n"
        "					if (self_shadow != 0 && lum(lc) > 0.0)\n"
        "						lc *= light_through(w, lv, (type == 1 || type == 3) ? 1e30 : distance(L.co_type.xyz, w), sigma);\n"
        "					scat += lc * hg(p2.w, dot(-dir, lv)) * 3.14159265;\n"
        "				}\n"
        "			}\n"
        "			scat *= refl;\n"
        "		}\n"
        "		rad += stepd * tr * (emit + scat);\n"
        "		if (t > t0 + 0.25 * (t1 - t0) && lum(tr) < p4.x) break;\n"
        "	}\n"
        /* an sRGB target: mixed with what is behind in linear light (the
         * target's copy), else blended over it */
        "	if (srgb != 0) {\n"
        "		vec4 behind = texelFetch(scenecolor, ivec2(gl_FragCoord.xy), 0);\n"
        "		fragcolor = vec4(to_srgb(to_linear(behind.rgb) * tr + rad), behind.a);\n"
        "	}\n"
        "	else\n"
        "		fragcolor = vec4(rad, 1.0 - lum(tr));\n"
        "}\n";

/* the scene's view depth of a depth value (as the shaders' shadow_view_z) */
static const char *vol_view_z =
        "uniform mat4 proj;\n"
        "float shadow_view_z(float d)\n"
        "{\n"
        "	float ndc = d * 2.0 - 1.0;\n"
        "	if (proj[2][3] == 0.0) return (ndc - proj[3][2]) / proj[2][2];\n"
        "	return -proj[3][2] / (ndc + proj[2][2]);\n"
        "}\n";

static GPUShader *vol_shader = NULL;
static bool vol_failed = false;
static GLuint vol_vao = 0;

/* a copy of the target's colours (for sRGB targets), made before each
 * volume so that each sees those drawn before it */
static struct {
	GLuint tex, fbo;
	int size[2];
	bool failed;
} vol_copy = {0};

static bool volume_copy_target(const int vp[4])
{
	if (vol_copy.failed) {
		return false;
	}
	const int w = vp[0] + vp[2], h = vp[1] + vp[3];
	if (w <= 0 || h <= 0) {
		return false;
	}
	if (!vol_copy.tex || vol_copy.size[0] != w || vol_copy.size[1] != h) {
		if (vol_copy.tex) {
			glDeleteTextures(1, &vol_copy.tex);
		}
		glCreateTextures(GL_TEXTURE_2D, 1, &vol_copy.tex);
		glTextureStorage2D(vol_copy.tex, 1, GL_RGBA8, w, h);
		glTextureParameteri(vol_copy.tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(vol_copy.tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		GPU_debug_label(GL_TEXTURE, vol_copy.tex, "BGE-Remix volumes: the target's copy");
		if (!vol_copy.fbo) {
			glCreateFramebuffers(1, &vol_copy.fbo);
		}
		glNamedFramebufferTexture(vol_copy.fbo, GL_COLOR_ATTACHMENT0, vol_copy.tex, 0);
		vol_copy.size[0] = w;
		vol_copy.size[1] = h;
	}
	GLint target = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &target);
	GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	glDisable(GL_SCISSOR_TEST);
	while (glGetError() != GL_NO_ERROR) {
		/* (errors from before: not this copy's) */
	}
	/* (a multisampled window is resolved by the same rectangle) */
	glBlitNamedFramebuffer((GLuint)target, vol_copy.fbo, vp[0], vp[1], w, h, vp[0], vp[1], w, h,
	                       GL_COLOR_BUFFER_BIT, GL_NEAREST);
	const GLenum err = glGetError();
	if (scissor) {
		glEnable(GL_SCISSOR_TEST);
	}
	if (err != GL_NO_ERROR) {
		vol_copy.failed = true;
		printf("BGE-Remix volumes: the target cannot be copied (GL error 0x%x): volumes blend in sRGB\n", err);
		return false;
	}
	return true;
}

static GPUShader *volume_shader(void)
{
	if (!vol_shader && !vol_failed) {
		const char *lamp = gpu_lights_lamp_glsl();
		char *frag = MEM_mallocN(strlen(lamp) + strlen(vol_view_z) + strlen(vol_frag_main) + 1, "volume frag");
		strcpy(frag, lamp);
		strcat(frag, vol_view_z);
		strcat(frag, vol_frag_main);
		vol_shader = GPU_shader_create_ex(vol_vert, frag, NULL, NULL, NULL, 0, 0, 0,
		                                  GPU_SHADER_FLAGS_GLSL450 | GPU_SHADER_FLAGS_CORE);
		MEM_freeN(frag);
		if (!vol_shader) {
			vol_failed = true;
			printf("BGE-Remix: the volume shader does not build\n");
		}
	}
	return vol_shader;
}

void GPU_volume_grid(unsigned int *r_texture, const int res[3], const float *values)
{
	GLint size[3] = {0, 0, 0};
	if (*r_texture) {
		glGetTextureLevelParameteriv(*r_texture, 0, GL_TEXTURE_WIDTH, &size[0]);
		glGetTextureLevelParameteriv(*r_texture, 0, GL_TEXTURE_HEIGHT, &size[1]);
		glGetTextureLevelParameteriv(*r_texture, 0, GL_TEXTURE_DEPTH, &size[2]);
		if (size[0] != res[0] || size[1] != res[1] || size[2] != res[2]) {
			GPU_volume_grid_free(r_texture);
		}
	}
	if (!*r_texture) {
		glCreateTextures(GL_TEXTURE_3D, 1, r_texture);
		glTextureStorage3D(*r_texture, 1, GL_R16F, res[0], res[1], res[2]);
		glTextureParameteri(*r_texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(*r_texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(*r_texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(*r_texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(*r_texture, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		GPU_debug_label(GL_TEXTURE, *r_texture, "BGE-Remix volume grid");
	}
	glTextureSubImage3D(*r_texture, 0, 0, 0, 0, res[0], res[1], res[2], GL_RED, GL_FLOAT, values);
}

void GPU_volume_grid_free(unsigned int *texture)
{
	if (*texture) {
		glDeleteTextures(1, texture);
		*texture = 0;
	}
}

void GPU_volume_draw(const GPUVolume *vol)
{
	GPUShader *shader = volume_shader();
	if (!shader) {
		return;
	}
	float view[4][4], proj[4][4], viewinv[4][4], mvp[4][4], obinv[4][4];
	GPU_matrix_get(GPU_MATRIX_MODELVIEW, view);
	GPU_matrix_get(GPU_MATRIX_PROJECTION, proj);
	invert_m4_m4(viewinv, view);
	invert_m4_m4(obinv, (float (*)[4])vol->obmat);
	mul_m4_m4m4(mvp, proj, view);

	const GLuint p = GPU_shader_program(shader);
	GPU_shader_bind(shader);
	glUniformMatrix4fv(glGetUniformLocation(p, "mvp"), 1, GL_FALSE, &mvp[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(p, "obmat"), 1, GL_FALSE, &vol->obmat[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(p, "view"), 1, GL_FALSE, &view[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(p, "viewinv"), 1, GL_FALSE, &viewinv[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(p, "obinv"), 1, GL_FALSE, &obinv[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(p, "proj"), 1, GL_FALSE, &proj[0][0]);
	glUniform3fv(glGetUniformLocation(p, "bbmin"), 1, vol->bbmin);
	glUniform3fv(glGetUniformLocation(p, "bbmax"), 1, vol->bbmax);
	glUniform4f(glGetUniformLocation(p, "p0"), vol->density, vol->density_scale, vol->scattering, vol->emission);
	glUniform4f(glGetUniformLocation(p, "p1"), vol->emission_col[0], vol->emission_col[1], vol->emission_col[2],
	            vol->reflection);
	glUniform4f(glGetUniformLocation(p, "p2"), vol->transmission_col[0], vol->transmission_col[1],
	            vol->transmission_col[2], vol->asymmetry);
	glUniform4f(glGetUniformLocation(p, "p3"), vol->reflection_col[0], vol->reflection_col[1],
	            vol->reflection_col[2], max_ff(vol->stepsize, 1e-3f));
	glUniform4f(glGetUniformLocation(p, "p4"), vol->depth_cutoff, vol->shadeless ? 1.0f : 0.0f,
	            vol->smoke_density ? 1.0f : 0.0f, vol->smoke_flame ? 1.0f : 0.0f);
	glUniform1i(glGetUniformLocation(p, "shape"), vol->shape);
	glUniform1i(glGetUniformLocation(p, "self_shadow"), vol->self_shadow);
	glUniform1i(glGetUniformLocation(p, "ext_shadow"), vol->ext_shadow);
	glUniform3fv(glGetUniformLocation(p, "smin"), 1, vol->smoke_min);
	glUniform3fv(glGetUniformLocation(p, "smax"), 1, vol->smoke_max);
	int vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	const bool srgb = vol->srgb && volume_copy_target(vp);
	glUniform1i(glGetUniformLocation(p, "srgb"), srgb ? 1 : 0);
	glBindTextureUnit(2, srgb ? vol_copy.tex : 0);
	glUniform1i(glGetUniformLocation(p, "scenecolor"), 2);
	glUniform4f(glGetUniformLocation(p, "viewport"), (float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3]);
	glUniform1f(glGetUniformLocation(p, "scenedepth_on"), gpu_scene_depth_is_on() ? 1.0f : 0.0f);
	/* the scene's depth and the shadow pool on the lights' units, the grids
	 * on two others */
	glUniform1i(glGetUniformLocation(p, "scenedepth"), GPU_lights_unit_scenedepth());
	glUniform1i(glGetUniformLocation(p, "pool"), GPU_lights_unit_shadowpool());
	glUniform1i(glGetUniformLocation(p, "lamps_on"), 1);
	glBindTextureUnit(0, vol->smoke_density);
	glBindTextureUnit(1, vol->smoke_flame);
	glUniform1i(glGetUniformLocation(p, "smoke_density"), 0);
	glUniform1i(glGetUniformLocation(p, "smoke_flame"), 1);

	GLboolean depthmask, cull, blend, depthtest;
	GLint cullface;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthmask);
	cull = glIsEnabled(GL_CULL_FACE);
	blend = glIsEnabled(GL_BLEND);
	depthtest = glIsEnabled(GL_DEPTH_TEST);
	glGetIntegerv(GL_CULL_FACE_MODE, &cullface);
	glDepthMask(GL_FALSE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	/* the box's triangles face out: its back faces are drawn */
	glCullFace(GL_FRONT);
	if (srgb) {
		glDisable(GL_BLEND);
	}
	else {
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}
	if (!vol_vao) {
		glGenVertexArrays(1, &vol_vao);
	}
	glBindVertexArray(vol_vao);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
	glDepthMask(depthmask);
	glCullFace((GLenum)cullface);
	if (!cull) {
		glDisable(GL_CULL_FACE);
	}
	if (blend) {
		glEnable(GL_BLEND);
	}
	else {
		glDisable(GL_BLEND);
	}
	if (depthtest) {
		glEnable(GL_DEPTH_TEST);
	}
	glBindTextureUnit(0, 0);
	glBindTextureUnit(1, 0);
	glBindTextureUnit(2, 0);
	GPU_shader_unbind();
}

void GPU_volumes_exit(void)
{
	if (vol_shader) {
		GPU_shader_free(vol_shader);
		vol_shader = NULL;
	}
	if (vol_vao) {
		glDeleteVertexArrays(1, &vol_vao);
		vol_vao = 0;
	}
	vol_failed = false;
	if (vol_copy.tex) {
		glDeleteTextures(1, &vol_copy.tex);
	}
	if (vol_copy.fbo) {
		glDeleteFramebuffers(1, &vol_copy.fbo);
	}
	memset(&vol_copy, 0, sizeof(vol_copy));
}
