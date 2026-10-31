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

/** \file blender/gpu/intern/gpu_probes.c
 *  \ingroup gpu
 *
 * BGE-Remix P8 Part B (P8.18): reflection probes, the sky cube, planar
 * mirrors and the screen-space reflections' history (see GPU_probes.h).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_math.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_lights.h"
#include "GPU_probes.h"

#include "gpu_private.h"

#define PROBES_MAX 64
#define PROBE_BINDING 14
#define SKY_SIZE 64

/* the cube faces as OpenGL's cube maps hold them (direction, up) */
static const float face_dir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
static const float face_up[6][3] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};

static struct {
	GLuint tex;              /* GL_TEXTURE_CUBE_MAP_ARRAY, RGBA16F */
	GLuint fbo, depth;       /* drawing a face */
	int size, mips, slots;
	unsigned char used[PROBES_MAX];
	float list[PROBES_MAX][8];  /* center, reach; slot, drawn */
	bool listdirty;
	GLuint listbuf;
	GLuint filterprog;
	bool filterfailed;
	GLint prevfb, prevvp[4];

	GLuint sky;              /* the world's cube (0: none) */
	GLuint skyfbo;

	GLuint mirror[GPU_MIRRORS], mirrorfbo[GPU_MIRRORS], mirrordepth[GPU_MIRRORS];
	int mirrorsize[GPU_MIRRORS][2];
	int mirror_used1;        /* the picture the draws read, plus one (0: none) */
	float clip[4];
	bool clip_on;

	GLuint history, historyfbo;
	int historysize[2];
	bool ssr;
} pr = {0};

int GPU_probe_size(void)
{
	static int size = 0;
	if (!size) {
		size = 128;
		const char *env = getenv("BGE_REMIX_PROBE_SIZE");
		if (env && atoi(env) >= 16) {
			size = atoi(env);
		}
		int p = 16;
		while (p < size && p < 1024) {
			p *= 2;
		}
		size = p;
	}
	return size;
}

int GPU_probes_unit_cube(void)
{
	return GPU_lights_unit_shadowpool() - 2;
}

int GPU_probes_unit_sky(void)
{
	return GPU_lights_unit_shadowpool() - 3;
}

int GPU_probes_unit_mirror(void)
{
	return GPU_lights_unit_shadowpool() - 4;
}

int GPU_probes_unit_history(void)
{
	return GPU_lights_unit_shadowpool() - 5;
}

/* ---- probes ---- */

static void probes_make(int slots)
{
	const int size = GPU_probe_size();
	int mips = 1;
	while ((size >> mips) >= 8 && mips < 6) {
		mips++;
	}
	GLuint tex;
	glCreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &tex);
	glTextureStorage3D(tex, mips, GL_RGBA16F, size, size, slots * 6);
	glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	GPU_debug_label(GL_TEXTURE, tex, "BGE-Remix reflection probes");
	if (pr.tex) {
		/* the probes drawn so far stay */
		for (int m = 0; m < mips; m++) {
			glCopyImageSubData(pr.tex, GL_TEXTURE_CUBE_MAP_ARRAY, m, 0, 0, 0, tex, GL_TEXTURE_CUBE_MAP_ARRAY, m, 0, 0,
			                   0, size >> m, size >> m, pr.slots * 6);
		}
		glDeleteTextures(1, &pr.tex);
	}
	pr.tex = tex;
	pr.size = size;
	pr.mips = mips;
	pr.slots = slots;
	if (!pr.fbo) {
		glCreateFramebuffers(1, &pr.fbo);
		glCreateRenderbuffers(1, &pr.depth);
		glNamedRenderbufferStorage(pr.depth, GL_DEPTH_COMPONENT24, size, size);
		glNamedFramebufferRenderbuffer(pr.fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, pr.depth);
	}
}

int GPU_probe_alloc(void)
{
	for (int i = 0; i < PROBES_MAX; i++) {
		if (!pr.used[i]) {
			if (i >= pr.slots) {
				probes_make(min_ii(max_ii(4, pr.slots * 2), PROBES_MAX));
			}
			pr.used[i] = 1;
			memset(pr.list[i], 0, sizeof(pr.list[i]));
			pr.listdirty = true;
			return i;
		}
	}
	return -1;
}

void GPU_probe_free(int probe)
{
	if (probe >= 0 && probe < PROBES_MAX) {
		pr.used[probe] = 0;
		pr.listdirty = true;
	}
}

void GPU_probe_place(int probe, const float center[3], float reach, bool drawn)
{
	if (probe < 0 || probe >= PROBES_MAX) {
		return;
	}
	const float v[8] = {center[0], center[1], center[2], reach, (float)probe, drawn ? 1.0f : 0.0f, 0.0f, 0.0f};
	if (memcmp(pr.list[probe], v, sizeof(v)) != 0) {
		memcpy(pr.list[probe], v, sizeof(v));
		pr.listdirty = true;
	}
}

static void face_view(int face, const float center[3], float r_view[4][4])
{
	float viewinv[4][4], x[3];
	cross_v3_v3v3(x, face_dir[face], face_up[face]);
	unit_m4(viewinv);
	copy_v3_v3(viewinv[0], x);
	copy_v3_v3(viewinv[1], face_up[face]);
	negate_v3_v3(viewinv[2], face_dir[face]);
	copy_v3_v3(viewinv[3], center);
	invert_m4_m4(r_view, viewinv);
}

void GPU_probe_face_begin(int probe, int face, float r_view[4][4], float r_proj[4][4], int r_size[1])
{
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &pr.prevfb);
	glGetIntegerv(GL_VIEWPORT, pr.prevvp);
	glNamedFramebufferTextureLayer(pr.fbo, GL_COLOR_ATTACHMENT0, pr.tex, 0, probe * 6 + face);
	glNamedFramebufferDrawBuffer(pr.fbo, GL_COLOR_ATTACHMENT0);
	glBindFramebuffer(GL_FRAMEBUFFER, pr.fbo);
	glViewport(0, 0, pr.size, pr.size);
	const float zero[4] = {0.0f, 0.0f, 0.0f, 1.0f}, one = 1.0f;
	glClearNamedFramebufferfv(pr.fbo, GL_COLOR, 0, zero);
	glClearNamedFramebufferfv(pr.fbo, GL_DEPTH, 0, &one);
	const float near = 0.05f, far = max_ff(pr.list[probe][3], 0.1f);
	face_view(face, pr.list[probe], r_view);
	perspective_m4(r_proj, -near, near, -near, near, near, far);
	r_size[0] = pr.size;
}

void GPU_probe_face_end(void)
{
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)pr.prevfb);
	glViewport(pr.prevvp[0], pr.prevvp[1], pr.prevvp[2], pr.prevvp[3]);
}

/* each mip level from the one above, blurred over a GGX lobe of its
 * roughness (the view along the normal, as split-sum prefiltering does) */
static const char *filter_cs =
        "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
        "uniform samplerCubeArray src;\n"
        "layout(rgba16f, binding = 0) uniform writeonly imageCubeArray dst;\n"
        "uniform int size, layer, srclod;\n"
        "uniform float roughness;\n"
        "vec3 face_dir(int f, vec2 st)\n"
        "{\n"
        "	if (f == 0) return vec3(1.0, -st.y, -st.x);\n"
        "	if (f == 1) return vec3(-1.0, -st.y, st.x);\n"
        "	if (f == 2) return vec3(st.x, 1.0, st.y);\n"
        "	if (f == 3) return vec3(st.x, -1.0, -st.y);\n"
        "	if (f == 4) return vec3(st.x, -st.y, 1.0);\n"
        "	return vec3(-st.x, -st.y, -1.0);\n"
        "}\n"
        "vec2 hammersley(uint i, uint n)\n"
        "{\n"
        "	uint b = (i << 16u) | (i >> 16u);\n"
        "	b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);\n"
        "	b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);\n"
        "	b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);\n"
        "	b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);\n"
        "	return vec2(float(i) / float(n), float(b) * 2.3283064365386963e-10);\n"
        "}\n"
        "void main()\n"
        "{\n"
        "	ivec3 p = ivec3(gl_GlobalInvocationID);\n"
        "	if (p.x >= size || p.y >= size) return;\n"
        "	vec2 st = (vec2(p.xy) + 0.5) / float(size) * 2.0 - 1.0;\n"
        "	vec3 N = normalize(face_dir(p.z, st));\n"
        "	vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);\n"
        "	vec3 T = normalize(cross(up, N)), B = cross(N, T);\n"
        "	float a = roughness * roughness;\n"
        "	vec3 sum = vec3(0.0);\n"
        "	float w = 0.0;\n"
        "	const uint n = 48u;\n"
        "	for (uint i = 0u; i < n; i++) {\n"
        "		vec2 xi = hammersley(i, n);\n"
        "		float phi = 6.28318531 * xi.x;\n"
        "		float ct = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));\n"
        "		float s = sqrt(1.0 - ct * ct);\n"
        "		vec3 H = T * (s * cos(phi)) + B * (s * sin(phi)) + N * ct;\n"
        "		vec3 L = 2.0 * dot(N, H) * H - N;\n"
        "		float nl = dot(N, L);\n"
        "		if (nl > 0.0) {\n"
        "			sum += textureLod(src, vec4(L, float(layer)), float(srclod)).rgb * nl;\n"
        "			w += nl;\n"
        "		}\n"
        "	}\n"
        "	imageStore(dst, ivec3(p.xy, layer * 6 + p.z), vec4(sum / max(w, 1e-6), 1.0));\n"
        "}\n";

void GPU_probe_filter(int probe)
{
	if (pr.filterfailed || !pr.tex || probe < 0) {
		return;
	}
	if (!pr.filterprog) {
		pr.filterprog = GPU_compute_program(filter_cs, "BGE-Remix probe blur");
		if (!pr.filterprog) {
			pr.filterfailed = true;
			return;
		}
	}
	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glUseProgram(pr.filterprog);
	glBindTextureUnit(0, pr.tex);
	glUniform1i(glGetUniformLocation(pr.filterprog, "src"), 0);
	glUniform1i(glGetUniformLocation(pr.filterprog, "layer"), probe);
	for (int m = 1; m < pr.mips; m++) {
		const int size = pr.size >> m;
		glUniform1i(glGetUniformLocation(pr.filterprog, "size"), size);
		glUniform1i(glGetUniformLocation(pr.filterprog, "srclod"), m - 1);
		glUniform1f(glGetUniformLocation(pr.filterprog, "roughness"), (float)m / (float)(pr.mips - 1));
		glBindImageTexture(0, pr.tex, m, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glDispatchCompute((GLuint)(size + 7) / 8, (GLuint)(size + 7) / 8, 6);
		glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glBindTextureUnit(0, 0);
	glUseProgram((GLuint)program);
}

/* ---- the sky ---- */

void GPU_sky_cube_update(struct Scene *scene, struct World *wo)
{
	if (!pr.sky) {
		int mips = 1;
		while ((SKY_SIZE >> mips) >= 4) {
			mips++;
		}
		glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &pr.sky);
		glTextureStorage2D(pr.sky, mips, GL_RGBA16F, SKY_SIZE, SKY_SIZE);
		glTextureParameteri(pr.sky, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(pr.sky, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		GPU_debug_label(GL_TEXTURE, pr.sky, "BGE-Remix sky cube");
	}
	if (gpu_world_cube_draw(scene, wo, false, pr.sky, SKY_SIZE)) {
		glGenerateTextureMipmap(pr.sky);
	}
	else {
		GPU_sky_cube_clear();
	}
}

void GPU_sky_cube_clear(void)
{
	if (pr.sky) {
		glDeleteTextures(1, &pr.sky);
		pr.sky = 0;
	}
}

/* ---- planar mirrors ---- */

void GPU_mirror_begin(int mirror, int width, int height, const float plane[4])
{
	if (mirror < 0 || mirror >= GPU_MIRRORS) {
		return;
	}
	width = max_ii(width, 8);
	height = max_ii(height, 8);
	if (!pr.mirror[mirror] || pr.mirrorsize[mirror][0] != width || pr.mirrorsize[mirror][1] != height) {
		if (pr.mirror[mirror]) {
			glDeleteTextures(1, &pr.mirror[mirror]);
			glDeleteRenderbuffers(1, &pr.mirrordepth[mirror]);
		}
		else {
			glCreateFramebuffers(1, &pr.mirrorfbo[mirror]);
		}
		int mips = 1;
		while (((width > height ? width : height) >> mips) >= 4 && mips < 8) {
			mips++;
		}
		glCreateTextures(GL_TEXTURE_2D, 1, &pr.mirror[mirror]);
		glTextureStorage2D(pr.mirror[mirror], mips, GL_RGBA16F, width, height);
		glTextureParameteri(pr.mirror[mirror], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(pr.mirror[mirror], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(pr.mirror[mirror], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(pr.mirror[mirror], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glCreateRenderbuffers(1, &pr.mirrordepth[mirror]);
		glNamedRenderbufferStorage(pr.mirrordepth[mirror], GL_DEPTH_COMPONENT24, width, height);
		glNamedFramebufferTexture(pr.mirrorfbo[mirror], GL_COLOR_ATTACHMENT0, pr.mirror[mirror], 0);
		glNamedFramebufferRenderbuffer(pr.mirrorfbo[mirror], GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
		                               pr.mirrordepth[mirror]);
		glNamedFramebufferDrawBuffer(pr.mirrorfbo[mirror], GL_COLOR_ATTACHMENT0);
		pr.mirrorsize[mirror][0] = width;
		pr.mirrorsize[mirror][1] = height;
	}
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &pr.prevfb);
	glGetIntegerv(GL_VIEWPORT, pr.prevvp);
	glBindFramebuffer(GL_FRAMEBUFFER, pr.mirrorfbo[mirror]);
	glViewport(0, 0, width, height);
	const float zero[4] = {0.0f, 0.0f, 0.0f, 1.0f}, one = 1.0f;
	glClearNamedFramebufferfv(pr.mirrorfbo[mirror], GL_COLOR, 0, zero);
	glClearNamedFramebufferfv(pr.mirrorfbo[mirror], GL_DEPTH, 0, &one);
	GPU_clip_plane_set(plane);
	/* the mirrored view turns the triangles around */
	glFrontFace(GL_CW);
}

void GPU_mirror_end(int mirror)
{
	if (mirror < 0 || mirror >= GPU_MIRRORS) {
		return;
	}
	glFrontFace(GL_CCW);
	GPU_clip_plane_set(NULL);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)pr.prevfb);
	glViewport(pr.prevvp[0], pr.prevvp[1], pr.prevvp[2], pr.prevvp[3]);
	glGenerateTextureMipmap(pr.mirror[mirror]);
}

void GPU_mirror_use(int mirror)
{
	pr.mirror_used1 = (mirror >= 0 && mirror < GPU_MIRRORS && pr.mirror[mirror]) ? mirror + 1 : 0;
	if (pr.mirror_used1) {
		glBindTextureUnit((GLuint)GPU_probes_unit_mirror(), pr.mirror[mirror]);
	}
}

void GPU_clip_plane_set(const float plane[4])
{
	if (plane) {
		copy_v4_v4(pr.clip, plane);
		pr.clip_on = true;
		glEnable(GL_CLIP_DISTANCE0);
	}
	else {
		zero_v4(pr.clip);
		pr.clip_on = false;
		glDisable(GL_CLIP_DISTANCE0);
	}
}

bool GPU_clip_plane(float r_plane[4])
{
	copy_v4_v4(r_plane, pr.clip);
	return pr.clip_on;
}

/* ---- screen-space reflections ---- */

void GPU_ssr_history_store(unsigned int framebuffer, int width, int height)
{
	if (width <= 0 || height <= 0) {
		return;
	}
	{
		/* (once, for the tests' logs) */
		static bool said = false;
		if (!said) {
			said = true;
			printf("BGE-Remix screen-space reflections: the frame kept for the next (%dx%d)\n", width, height);
		}
	}
	if (!pr.history || pr.historysize[0] != width || pr.historysize[1] != height) {
		if (pr.history) {
			glDeleteTextures(1, &pr.history);
		}
		else {
			glCreateFramebuffers(1, &pr.historyfbo);
		}
		glCreateTextures(GL_TEXTURE_2D, 1, &pr.history);
		glTextureStorage2D(pr.history, 1, GL_RGBA16F, width, height);
		glTextureParameteri(pr.history, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(pr.history, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(pr.history, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(pr.history, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glNamedFramebufferTexture(pr.historyfbo, GL_COLOR_ATTACHMENT0, pr.history, 0);
		pr.historysize[0] = width;
		pr.historysize[1] = height;
	}
	glNamedFramebufferReadBuffer(framebuffer, GL_COLOR_ATTACHMENT0);
	glNamedFramebufferDrawBuffer(pr.historyfbo, GL_COLOR_ATTACHMENT0);
	glBlitNamedFramebuffer(framebuffer, pr.historyfbo, 0, 0, width, height, 0, 0, width, height,
	                       GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void GPU_ssr_set(bool on)
{
	pr.ssr = on;
}

bool GPU_ssr(void)
{
	return pr.ssr && pr.history != 0;
}

/* ---- for the materials ---- */

void GPU_probes_bind(void)
{
	if (!pr.listbuf) {
		glCreateBuffers(1, &pr.listbuf);
		glNamedBufferStorage(pr.listbuf, sizeof(unsigned int) * 4 + sizeof(pr.list), NULL, GL_DYNAMIC_STORAGE_BIT);
		GPU_debug_label(GL_BUFFER, pr.listbuf, "BGE-Remix probe list");
		pr.listdirty = true;
	}
	if (pr.listdirty) {
		/* the drawn probes, packed (head: count, mip levels) */
		float packed[PROBES_MAX][8];
		unsigned int count = 0;
		for (int i = 0; i < PROBES_MAX; i++) {
			if (pr.used[i] && pr.list[i][5] > 0.5f) {
				memcpy(packed[count++], pr.list[i], sizeof(pr.list[i]));
			}
		}
		const unsigned int head[4] = {count, (unsigned int)pr.mips, 0, 0};
		glNamedBufferSubData(pr.listbuf, 0, sizeof(head), head);
		if (count) {
			glNamedBufferSubData(pr.listbuf, sizeof(head), sizeof(float) * 8 * count, packed);
		}
		pr.listdirty = false;
	}
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, PROBE_BINDING, pr.listbuf);
	if (pr.tex) {
		glBindTextureUnit((GLuint)GPU_probes_unit_cube(), pr.tex);
	}
	if (pr.sky) {
		glBindTextureUnit((GLuint)GPU_probes_unit_sky(), pr.sky);
	}
	if (pr.history) {
		glBindTextureUnit((GLuint)GPU_probes_unit_history(), pr.history);
	}
	if (pr.mirror_used1 && pr.mirror[pr.mirror_used1 - 1]) {
		glBindTextureUnit((GLuint)GPU_probes_unit_mirror(), pr.mirror[pr.mirror_used1 - 1]);
	}
}

void GPU_probes_program_samplers(unsigned int program)
{
	const char *names[4] = {"bge_probes", "bge_skycube", "bge_mirror", "bge_history"};
	const int units[4] = {GPU_probes_unit_cube(), GPU_probes_unit_sky(), GPU_probes_unit_mirror(),
	                      GPU_probes_unit_history()};
	for (int i = 0; i < 4; i++) {
		const GLint loc = glGetUniformLocation(program, names[i]);
		if (loc >= 0) {
			glProgramUniform1i(program, loc, units[i]);
		}
	}
}

void GPU_probes_values(int *r_mirror, float r_mirror_size[2], int *r_sky, float r_plane[4], int *r_ssr)
{
	*r_mirror = pr.mirror_used1 ? 1 : 0;
	if (pr.mirror_used1) {
		r_mirror_size[0] = (float)pr.mirrorsize[pr.mirror_used1 - 1][0];
		r_mirror_size[1] = (float)pr.mirrorsize[pr.mirror_used1 - 1][1];
	}
	else {
		r_mirror_size[0] = r_mirror_size[1] = 1.0f;
	}
	*r_sky = pr.sky ? 1 : 0;
	copy_v4_v4(r_plane, pr.clip);
	*r_ssr = GPU_ssr() ? 1 : 0;
}

void GPU_probes_exit(void)
{
	GLuint tex[3] = {pr.tex, pr.sky, pr.history};
	for (int i = 0; i < 3; i++) {
		if (tex[i]) {
			glDeleteTextures(1, &tex[i]);
		}
	}
	for (int i = 0; i < GPU_MIRRORS; i++) {
		if (pr.mirror[i]) {
			glDeleteTextures(1, &pr.mirror[i]);
			glDeleteRenderbuffers(1, &pr.mirrordepth[i]);
			glDeleteFramebuffers(1, &pr.mirrorfbo[i]);
		}
	}
	if (pr.fbo) {
		glDeleteFramebuffers(1, &pr.fbo);
		glDeleteRenderbuffers(1, &pr.depth);
	}
	if (pr.historyfbo) {
		glDeleteFramebuffers(1, &pr.historyfbo);
	}
	if (pr.listbuf) {
		glDeleteBuffers(1, &pr.listbuf);
	}
	if (pr.filterprog) {
		glDeleteProgram(pr.filterprog);
	}
	memset(&pr, 0, sizeof(pr));
}
