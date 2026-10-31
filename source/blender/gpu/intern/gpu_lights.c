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

/** \file blender/gpu/intern/gpu_lights.c
 *  \ingroup gpu
 *
 * BGE-Remix P8 Part B (P8.15, P8.16): the shadow pool and the clustered
 * lamps (see GPU_lights.h).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_math_base.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_lights.h"
#include "GPU_probes.h"

#include "BKE_material.h"

#include "gpu_private.h"

/* the pool's largest side, and how much memory it may take */
#define POOL_MAX_SIZE 4096
#define POOL_MIN_SIZE 64
#define POOL_MAX_BYTES (256u * 1024u * 1024u)
#define POOL_MAX_LAYERS 2048

static struct {
	GLuint tex;               /* GL_TEXTURE_2D_ARRAY, GL_DEPTH_COMPONENT24, immutable */
	GLuint fbo;
	GLuint blank;             /* one layer of depth 1, bound while a map is drawn */
	int size, layers;         /* what the texture holds */
	int want;                 /* the size the lamps ask for (the largest) */
	unsigned char *used;      /* per layer */
	unsigned int gen;

	/* the pass being drawn */
	bool drawing;
	GLint prev_fbo, prev_viewport[4];
	GLboolean prev_scissor, prev_clamp, prev_offset;
	GLfloat prev_factor, prev_units;
	int views;
	float clip_from_first[6][4][4];
	int view_layer;
	bool shadow_pass;
} pool = {0};

bool GPU_shadow_layered(void)
{
	return GPU_caps()->vs_layer && GPU_speed_feature("layered_shadows");
}

int GPU_shadow_pool_size(void)
{
	return pool.size;
}

unsigned int GPU_shadow_pool_generation(void)
{
	return pool.gen;
}

/* the size the pool's memory allows for "layers" layers */
static int pool_fit_size(int want, int layers)
{
	int size = POOL_MIN_SIZE;
	while (size < want && size < POOL_MAX_SIZE) {
		size *= 2;
	}
	while (size > POOL_MIN_SIZE && (unsigned long long)size * size * 4u * (unsigned)layers > POOL_MAX_BYTES) {
		size /= 2;
	}
	return size;
}

static void pool_make(int size, int layers)
{
	if (pool.tex) {
		glDeleteTextures(1, &pool.tex);
		pool.tex = 0;
	}
	if (!pool.fbo) {
		glCreateFramebuffers(1, &pool.fbo);
		glNamedFramebufferDrawBuffer(pool.fbo, GL_NONE);
		glNamedFramebufferReadBuffer(pool.fbo, GL_NONE);
		GPU_debug_label(GL_FRAMEBUFFER, pool.fbo, "BGE-Remix shadow pool");
	}
	glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &pool.tex);
	glTextureStorage3D(pool.tex, 1, GL_DEPTH_COMPONENT24, size, size, layers);
	glTextureParameteri(pool.tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTextureParameteri(pool.tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTextureParameteri(pool.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(pool.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(pool.tex, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	GPU_debug_label(GL_TEXTURE, pool.tex, "BGE-Remix shadow pool");
	const float one = 1.0f;
	glClearTexImage(pool.tex, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &one);

	unsigned char *used = MEM_callocN((size_t)layers, "shadow pool layers");
	if (pool.used) {
		memcpy(used, pool.used, (size_t)min_ii(layers, pool.layers));
		MEM_freeN(pool.used);
	}
	pool.used = used;
	pool.size = size;
	pool.layers = layers;
	pool.gen++;
	printf("BGE-Remix shadows: the pool holds %d maps of %d x %d\n", layers, size, size);
}

int GPU_shadow_pool_alloc(int views, int size)
{
	if (views <= 0 || views > 6) {
		return -1;
	}
	pool.want = max_ii(pool.want, size);
	int first = -1;
	for (int i = 0; pool.used && i + views <= pool.layers && first < 0; i++) {
		int k = 0;
		while (k < views && !pool.used[i + k]) {
			k++;
		}
		if (k == views) {
			first = i;
		}
	}
	int layers = pool.layers;
	if (first < 0) {
		/* more layers (the used ones stay where they are) */
		int last = pool.layers;
		while (last > 0 && !pool.used[last - 1]) {
			last--;
		}
		first = last;
		layers = max_ii(max_ii(8, pool.layers * 2), first + views);
		if (layers > POOL_MAX_LAYERS) {
			return -1;
		}
	}
	const int fit = pool_fit_size(pool.want, layers);
	if (layers != pool.layers || fit != pool.size) {
		pool_make(fit, layers);
	}
	memset(pool.used + first, 1, (size_t)views);
	return first;
}

void GPU_shadow_pool_free(int layer, int views)
{
	if (!pool.used || layer < 0) {
		return;
	}
	for (int i = layer; i < layer + views && i < pool.layers; i++) {
		pool.used[i] = 0;
	}
}

void GPU_shadow_pool_begin(int layer, int views, bool layered, bool depth_clamp)
{
	if (!pool.tex || layer < 0) {
		return;
	}
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &pool.prev_fbo);
	glGetIntegerv(GL_VIEWPORT, pool.prev_viewport);
	pool.prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
	pool.prev_clamp = glIsEnabled(GL_DEPTH_CLAMP);
	pool.prev_offset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &pool.prev_factor);
	glGetFloatv(GL_POLYGON_OFFSET_UNITS, &pool.prev_units);

	/* the lamp's layers only (a clear of a layered target clears them all) */
	const float one = 1.0f;
	glClearTexSubImage(pool.tex, 0, 0, 0, layer, pool.size, pool.size, views, GL_DEPTH_COMPONENT, GL_FLOAT, &one);

	if (layered) {
		glNamedFramebufferTexture(pool.fbo, GL_DEPTH_ATTACHMENT, pool.tex, 0);
	}
	else {
		glNamedFramebufferTextureLayer(pool.fbo, GL_DEPTH_ATTACHMENT, pool.tex, 0, layer);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, pool.fbo);
	glViewport(0, 0, pool.size, pool.size);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_TRUE);
	if (depth_clamp) {
		glEnable(GL_DEPTH_CLAMP);
	}
	/* slope-scaled bias on the casters (the receivers add a normal offset) */
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1.0f, 1.0f);
	pool.drawing = true;
	pool.shadow_pass = true;
}

void GPU_shadow_pool_view(int layer)
{
	if (!pool.drawing) {
		return;
	}
	glNamedFramebufferTextureLayer(pool.fbo, GL_DEPTH_ATTACHMENT, pool.tex, 0, layer);
	glBindFramebuffer(GL_FRAMEBUFFER, pool.fbo);
}

void GPU_shadow_pool_end(void)
{
	if (!pool.drawing) {
		return;
	}
	pool.drawing = false;
	pool.shadow_pass = false;
	pool.views = 0;
	glNamedFramebufferTexture(pool.fbo, GL_DEPTH_ATTACHMENT, 0, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)pool.prev_fbo);
	glViewport(pool.prev_viewport[0], pool.prev_viewport[1], pool.prev_viewport[2], pool.prev_viewport[3]);
	if (pool.prev_scissor) {
		glEnable(GL_SCISSOR_TEST);
	}
	if (!pool.prev_clamp) {
		glDisable(GL_DEPTH_CLAMP);
	}
	if (!pool.prev_offset) {
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
	glPolygonOffset(pool.prev_factor, pool.prev_units);
}

int GPU_shadow_pool_layer_texture(int layer, unsigned int *r_view, unsigned int *r_gen)
{
	if (!pool.tex || layer < 0 || layer >= pool.layers) {
		return -1;
	}
	if (*r_view && *r_gen != pool.gen) {
		glDeleteTextures(1, r_view);
		*r_view = 0;
	}
	if (!*r_view) {
		glGenTextures(1, r_view);
		glTextureView(*r_view, GL_TEXTURE_2D, pool.tex, GL_DEPTH_COMPONENT24, 0, 1, (GLuint)layer, 1);
		*r_gen = pool.gen;
	}
	return (int)*r_view;
}

void GPU_shadow_pool_read(int layer, float *r_depth)
{
	if (!pool.tex || layer < 0 || layer >= pool.layers) {
		return;
	}
	glGetTextureSubImage(pool.tex, 0, 0, 0, layer, pool.size, pool.size, 1, GL_DEPTH_COMPONENT, GL_FLOAT,
	                     (GLsizei)(sizeof(float) * pool.size * pool.size), r_depth);
}

void GPU_shadow_views_set(int count, const float (*clip_from_first)[4][4], int layer)
{
	pool.views = (count > 1 && count <= 6) ? count : 0;
	if (pool.views) {
		memcpy(pool.clip_from_first, clip_from_first, sizeof(float[4][4]) * (size_t)count);
	}
	pool.view_layer = layer;
}

int GPU_shadow_views_count(void)
{
	return pool.views;
}

/* for gpu_builtin.c's old built-ins */
void gpu_lights_views_get(int *r_count, const float **r_mats, int *r_layer)
{
	*r_count = pool.views;
	*r_mats = &pool.clip_from_first[0][0][0];
	*r_layer = pool.view_layer;
}

void GPU_shadow_pass_set(bool on)
{
	pool.shadow_pass = on;
}

bool GPU_shadow_pass(void)
{
	return pool.shadow_pass;
}

int GPU_lights_unit_shadowpool(void)
{
	static int unit = -1;
	if (unit < 0) {
		GLint n = 16;
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &n);
		unit = max_ii(n - 1, 2);
	}
	return unit;
}

int GPU_lights_unit_scenedepth(void)
{
	return GPU_lights_unit_shadowpool() - 1;
}

void GPU_lights_bind_textures(unsigned int scenedepth_texture)
{
	if (!pool.blank) {
		const float one = 1.0f;
		glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &pool.blank);
		glTextureStorage3D(pool.blank, 1, GL_DEPTH_COMPONENT24, 1, 1, 1);
		glTextureParameteri(pool.blank, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(pool.blank, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(pool.blank, GL_TEXTURE_COMPARE_MODE, GL_NONE);
		glClearTexImage(pool.blank, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &one);
	}
	/* never the pool while a map of it is drawn */
	glBindTextureUnit((GLuint)GPU_lights_unit_shadowpool(),
	                  (pool.tex && !pool.shadow_pass) ? pool.tex : pool.blank);
	if (scenedepth_texture) {
		glBindTextureUnit((GLuint)GPU_lights_unit_scenedepth(), scenedepth_texture);
	}
}

void GPU_lights_program_samplers(unsigned int program)
{
	GLint loc = glGetUniformLocation(program, "bge_shadowpool");
	if (loc >= 0) {
		glProgramUniform1i(program, loc, GPU_lights_unit_shadowpool());
	}
	loc = glGetUniformLocation(program, "bge_scenedepth");
	if (loc >= 0) {
		glProgramUniform1i(program, loc, GPU_lights_unit_scenedepth());
	}
	GPU_probes_program_samplers(program);
	/* never read; on a unit of its own type */
	loc = glGetUniformLocation(program, "bge_noramp");
	if (loc >= 0) {
		glProgramUniform1i(program, loc, GPU_lights_unit_scenedepth());
	}
}

/* ------------------------------------------------------------------------ */
/* P8.15: the clustered lamps */

#define CL_X 16
#define CL_Y 9
#define CL_Z 24
#define CL_COUNT (CL_X * CL_Y * CL_Z)

static const char *cluster_cs =
        "layout(local_size_x = 64) in;\n"
        "struct BgeLamp {\n"
        "	vec4 co_type, vec_dist, col_energy, spot;\n"
        "	mat4 imat;\n"
        "	vec4 falloff, coeff;\n"
        "	vec4 area[4];\n"
        "	vec4 shadow_color, shadow_params, shadow_clip, contact, cascade_scale, cascade_range;\n"
        "	mat4 shadowmat[4];\n"
        "	vec4 bound;\n"
        "	uvec4 bits;\n"
        "	vec4 extra;\n"
        "};\n"
        "layout(std430, binding = 11) readonly buffer BgeLamps { BgeLamp lamps[]; };\n"
        "layout(std430, binding = 13) writeonly buffer BgeClusterMasks { uint mask[]; };\n"
        "uniform mat4 view;\n"
        "uniform mat4 projinv;\n"
        "uniform vec4 depth;\n"
        "uniform uint nlamps;\n"
        "uniform uint words;\n"
        /* the view space point on the ray through ndc at view depth z */
        "vec3 at_depth(vec2 ndc, float z)\n"
        "{\n"
        "	vec4 a = projinv * vec4(ndc, -1.0, 1.0);\n"
        "	vec4 b = projinv * vec4(ndc, 1.0, 1.0);\n"
        "	a /= a.w;\n"
        "	b /= b.w;\n"
        "	float t = (-z - a.z) / (b.z - a.z);\n"
        "	return mix(a.xyz, b.xyz, t);\n"
        "}\n"
        "void main()\n"
        "{\n"
        "	uint c = gl_GlobalInvocationID.x;\n"
        "	if (c >= 3456u) return;\n"
        "	uint tx = c % 16u, ty = (c / 16u) % 9u, k = c / 144u;\n"
        "	float z0 = depth.x * pow(depth.y / depth.x, float(k) / 24.0);\n"
        "	float z1 = depth.x * pow(depth.y / depth.x, float(k + 1u) / 24.0);\n"
        "	vec2 n0 = vec2(tx, ty) / vec2(16.0, 9.0) * 2.0 - 1.0;\n"
        "	vec2 n1 = vec2(tx + 1u, ty + 1u) / vec2(16.0, 9.0) * 2.0 - 1.0;\n"
        "	vec3 lo = vec3(1e30), hi = vec3(-1e30);\n"
        "	for (int i = 0; i < 8; i++) {\n"
        "		vec2 n = vec2((i & 1) != 0 ? n1.x : n0.x, (i & 2) != 0 ? n1.y : n0.y);\n"
        "		vec3 p = at_depth(n, (i & 4) != 0 ? z1 : z0);\n"
        "		lo = min(lo, p);\n"
        "		hi = max(hi, p);\n"
        "	}\n"
        "	for (uint w = 0u; w < words; w++) {\n"
        "		uint bits = 0u;\n"
        "		for (uint b = 0u; b < 32u; b++) {\n"
        "			uint li = w * 32u + b;\n"
        "			if (li >= nlamps) break;\n"
        "			vec4 bound = lamps[li].bound;\n"
        "			bool inside = true;\n"
        "			if (bound.w > 0.0) {\n"
        "				vec3 cv = (view * vec4(bound.xyz, 1.0)).xyz;\n"
        "				vec3 d = max(max(lo - cv, cv - hi), vec3(0.0));\n"
        "				inside = dot(d, d) <= bound.w * bound.w;\n"
        "			}\n"
        "			if (inside) bits |= 1u << b;\n"
        "		}\n"
        "		mask[c * words + w] = bits;\n"
        "	}\n"
        "}\n";

static struct {
	GLuint prog;
	bool failed;
	GLint loc_view, loc_projinv, loc_depth, loc_nlamps, loc_words;
	GLuint lampbuf, infobuf, maskbuf;
	size_t lampbuf_size, maskbuf_size;
	unsigned int hash;
	int count;
	bool uploaded;
	bool halos;  /* a record has a halo (the fog grid draws its shafts) */
	/* what the masks were made for */
	float view[4][4], proj[4][4];
	int viewport[4];
	bool valid;
} cl = {0};

static void m4_invert(float r[4][4], const float m[4][4])
{
	/* general 4x4 inverse (cofactors), in double */
	double inv[16], det;
	const float *a = &m[0][0];
	inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
	inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
	inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
	inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
	inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
	inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
	inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
	inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
	inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
	inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
	inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
	inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
	inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
	inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
	inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
	inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
	det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
	det = (det != 0.0) ? 1.0 / det : 0.0;
	float *o = &r[0][0];
	for (int i = 0; i < 16; i++) {
		o[i] = (float)(inv[i] * det);
	}
}

static void cluster_bind(void)
{
	if (cl.lampbuf) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, cl.lampbuf);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, cl.infobuf);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, cl.maskbuf);
	}
}

void GPU_lights_cluster(const void *records, int count, size_t record_size, unsigned int hash,
                        const float view[4][4], const float proj[4][4], const int viewport[4])
{
	if (cl.failed || !GLEW_VERSION_4_3) {
		return;
	}
	if (!cl.prog) {
		cl.prog = GPU_compute_program(cluster_cs, "BGE-Remix clustered lamps");
		if (!cl.prog) {
			cl.failed = true;
			return;
		}
		cl.loc_view = glGetUniformLocation(cl.prog, "view");
		cl.loc_projinv = glGetUniformLocation(cl.prog, "projinv");
		cl.loc_depth = glGetUniformLocation(cl.prog, "depth");
		cl.loc_nlamps = glGetUniformLocation(cl.prog, "nlamps");
		cl.loc_words = glGetUniformLocation(cl.prog, "words");
		glCreateBuffers(1, &cl.infobuf);
		glNamedBufferStorage(cl.infobuf, sizeof(float) * 12, NULL, GL_DYNAMIC_STORAGE_BIT);
		GPU_debug_label(GL_BUFFER, cl.infobuf, "BGE-Remix cluster info");
	}

	/* the records, when they changed */
	const size_t bytes = (size_t)max_ii(count, 1) * record_size;
	if (!cl.lampbuf || cl.lampbuf_size < bytes) {
		if (cl.lampbuf) {
			glDeleteBuffers(1, &cl.lampbuf);
		}
		cl.lampbuf_size = (bytes > cl.lampbuf_size * 2) ? bytes : cl.lampbuf_size * 2;
		glCreateBuffers(1, &cl.lampbuf);
		glNamedBufferStorage(cl.lampbuf, (GLsizeiptr)cl.lampbuf_size, NULL, GL_DYNAMIC_STORAGE_BIT);
		GPU_debug_label(GL_BUFFER, cl.lampbuf, "BGE-Remix lamps");
		cl.uploaded = false;
	}
	bool changed = false;
	if (!cl.uploaded || cl.hash != hash || cl.count != count) {
		if (count > 0) {
			glNamedBufferSubData(cl.lampbuf, 0, (GLsizeiptr)(count * record_size), records);
		}
		cl.hash = hash;
		cl.count = count;
		cl.uploaded = true;
		changed = true;
		/* the halo intensity is the record's last vec4's first float */
		cl.halos = false;
		for (int i = 0; i < count; i++) {
			const float *extra = (const float *)((const char *)records + (size_t)i * record_size + record_size - 16);
			if (extra[0] > 0.0f) {
				cl.halos = true;
			}
		}
	}
	const unsigned int words = (unsigned int)max_ii((count + 31) / 32, 1);
	const size_t maskbytes = (size_t)CL_COUNT * words * sizeof(unsigned int);
	if (!cl.maskbuf || cl.maskbuf_size < maskbytes) {
		if (cl.maskbuf) {
			glDeleteBuffers(1, &cl.maskbuf);
		}
		cl.maskbuf_size = maskbytes;
		glCreateBuffers(1, &cl.maskbuf);
		glNamedBufferStorage(cl.maskbuf, (GLsizeiptr)maskbytes, NULL, 0);
		GPU_debug_label(GL_BUFFER, cl.maskbuf, "BGE-Remix cluster masks");
		changed = true;
	}
	if (!changed && cl.valid && memcmp(cl.view, view, sizeof(cl.view)) == 0 &&
	    memcmp(cl.proj, proj, sizeof(cl.proj)) == 0 && memcmp(cl.viewport, viewport, sizeof(cl.viewport)) == 0)
	{
		cluster_bind();
		return;
	}
	memcpy(cl.view, view, sizeof(cl.view));
	memcpy(cl.proj, proj, sizeof(cl.proj));
	memcpy(cl.viewport, viewport, sizeof(cl.viewport));
	cl.valid = true;

	/* the view's depth range from its projection */
	float znear, zfar;
	if (proj[3][3] == 0.0f) {
		znear = proj[3][2] / (proj[2][2] - 1.0f);
		zfar = proj[3][2] / (proj[2][2] + 1.0f);
	}
	else {
		znear = (proj[3][2] + 1.0f) / proj[2][2];
		zfar = (proj[3][2] - 1.0f) / proj[2][2];
	}
	znear = max_ff(znear, 1e-3f);
	zfar = max_ff(zfar, znear * 1.001f);

	float info[12] = {
		(float)viewport[0], (float)viewport[1], (float)viewport[2], (float)viewport[3],
		znear, zfar, (float)CL_Z / logf(zfar / znear), 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
	};
	unsigned int counts[2] = {words, (unsigned int)count};
	memcpy(&info[8], counts, sizeof(counts));
	glNamedBufferSubData(cl.infobuf, 0, sizeof(info), info);

	float projinv[4][4];
	m4_invert(projinv, proj);
	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glUseProgram(cl.prog);
	glUniformMatrix4fv(cl.loc_view, 1, GL_FALSE, &view[0][0]);
	glUniformMatrix4fv(cl.loc_projinv, 1, GL_FALSE, &projinv[0][0]);
	glUniform4f(cl.loc_depth, znear, zfar, 0.0f, 0.0f);
	glUniform1ui(cl.loc_nlamps, (GLuint)count);
	glUniform1ui(cl.loc_words, words);
	cluster_bind();
	glDispatchCompute((CL_COUNT + 63) / 64, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	glUseProgram((GLuint)program);
}

/* ------------------------------------------------------------------------ */
/* P8.17: the Modern pipeline's volumetric fog: the mist (its type, start,
 * distance, intensity and height) as a medium in a grid of cells over the
 * view, lit by the clustered lamps and their shadow maps; spot lamps with
 * Halo scatter in their cone (light shafts). One pass fills each cell, one
 * sums the cells front to back; the pipeline's last pass applies the sum at
 * each pixel's depth. Where the light adds nothing it is the mist as F12
 * shows it: the colour times the mist's visibility plus the mist colour. */

#define FOG_LAMP_DECL \
        "struct BgeLamp {\n" \
        "	vec4 co_type, vec_dist, col_energy, spot;\n" \
        "	mat4 imat;\n" \
        "	vec4 falloff, coeff;\n" \
        "	vec4 area[4];\n" \
        "	vec4 shadow_color, shadow_params, shadow_clip, contact, cascade_scale, cascade_range;\n" \
        "	mat4 shadowmat[4];\n" \
        "	vec4 bound;\n" \
        "	uvec4 bits;\n" \
        "	vec4 extra;\n" \
        "};\n" \
        "layout(std430, binding = 11) readonly buffer BgeLamps { BgeLamp lamps[]; };\n" \
        "layout(std430, binding = 12) readonly buffer BgeClusterInfo {\n" \
        "	vec4 cl_viewport; vec4 cl_depth; uvec4 cl_counts; };\n" \
        "layout(std430, binding = 13) readonly buffer BgeClusterMasks { uint cl_mask[]; };\n"

/* the lamps (records, clusters) and their light at a point, for compute
 * passes and the volumes (gpu_volumes.c) */
static const char *lamp_glsl =
        FOG_LAMP_DECL
        "uniform sampler2DArray pool;\n"
        "float linear_depth(float d, vec3 clip)\n"
        "{\n"
        "	if (clip.z > 0.5) return clip.x + d * (clip.y - clip.x);\n"
        "	return clip.x * clip.y / max(clip.y - d * (clip.y - clip.x), 1e-8);\n"
        "}\n"
        /* a lamp's shadow at a world point (one tap) */
        "float lamp_shadow(BgeLamp L, vec3 w)\n"
        "{\n"
        "	int kind = int(L.shadow_color.w + 0.5);\n"
        "	float layer = L.shadow_clip.w, bias = 0.05;\n"
        "	if (kind == 1) {\n"
        "		vec4 c = L.shadowmat[0] * vec4(w, 1.0);\n"
        "		if (c.w <= 0.0) return 1.0;\n"
        "		vec2 uv = c.xy / c.w;\n"
        "		if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;\n"
        "		float d = texture(pool, vec3(uv, layer)).r;\n"
        "		return (d >= 0.99999 || linear_depth(d, vec3(L.shadow_clip.xy, 0.0)) >= c.w - bias) ? 1.0 : 0.0;\n"
        "	}\n"
        "	if (kind == 2) {\n"
        "		vec3 l = (L.shadowmat[0] * vec4(w, 1.0)).xyz, al = abs(l), f, u;\n"
        "		float face;\n"
        "		if (al.x >= al.y && al.x >= al.z) { face = l.x >= 0.0 ? 0.0 : 1.0; f = vec3(sign(l.x), 0.0, 0.0); u = vec3(0.0, 1.0, 0.0); }\n"
        "		else if (al.y >= al.z) { face = l.y >= 0.0 ? 2.0 : 3.0; f = vec3(0.0, sign(l.y), 0.0); u = vec3(0.0, 0.0, 1.0); }\n"
        "		else { face = l.z >= 0.0 ? 4.0 : 5.0; f = vec3(0.0, 0.0, sign(l.z)); u = vec3(1.0, 0.0, 0.0); }\n"
        "		float zr2 = dot(l, f);\n"
        "		if (zr2 <= L.shadow_clip.x) return 1.0;\n"
        "		vec2 uv = vec2(dot(l, cross(f, u)), dot(l, u)) / (zr2 * L.shadow_clip.z) * 0.5 + 0.5;\n"
        "		float d = texture(pool, vec3(uv, layer + face)).r;\n"
        "		return (d >= 0.99999 || linear_depth(d, vec3(L.shadow_clip.xy, 0.0)) >= zr2 - bias) ? 1.0 : 0.0;\n"
        "	}\n"
        "	if (kind == 3) {\n"
        "		for (int k = 0; k < 4; k++) {\n"
        "			vec3 c = (L.shadowmat[k] * vec4(w, 1.0)).xyz;\n"
        "			if (all(greaterThan(c, vec3(0.0))) && all(lessThan(c, vec3(1.0)))) {\n"
        "				float rg = L.cascade_range[k];\n"
        "				float d = texture(pool, vec3(c.xy, layer + float(k))).r;\n"
        "				return (d >= 0.99999 || d * rg >= c.z * rg - bias) ? 1.0 : 0.0;\n"
        "			}\n"
        "		}\n"
        "	}\n"
        "	return 1.0;\n"
        "}\n"
        /* the light a lamp brings to a point (its colour, falloff, cone, shadow) */
        "vec3 lamp_light(BgeLamp L, vec3 p, vec3 w, out float halo)\n"
        "{\n"
        "	halo = 0.0;\n"
        "	int type = int(L.co_type.w + 0.5);\n"
        "	vec3 col = L.col_energy.rgb * L.col_energy.w;\n"
        "	if (type == 1 || type == 3) return col * lamp_shadow(L, w);\n"
        "	vec3 d = w - L.co_type.xyz;\n"
        "	float dist = length(d), ld = L.vec_dist.w, v = 1.0;\n"
        "	int fo = int(L.falloff.x + 0.5);\n"
        "	if (type != 4) {\n"
        "		if (fo == 1) v = ld / (ld + dist);\n"
        "		else if (fo == 2) v = ld / (ld + dist * dist);\n"
        "		else if (fo == 4) v = ld / (ld + L.falloff.y * dist) * ld / (ld + L.falloff.z * dist * dist);\n"
        "		else if (fo == 5) v = 1.0 / max(L.coeff.x + L.coeff.y * dist + L.coeff.z * dist * dist, 1e-6);\n"
        "		if ((L.bits.x & 64u) != 0u) v *= max(ld - dist, 0.0) / ld;\n"
        "	}\n"
        "	if (type == 2) {\n"
        "		vec3 lp = (L.imat * vec4(w, 1.0)).xyz;\n"
        "		vec2 xy = lp.xy / max(-lp.z, 1e-6) / L.spot.zw;\n"
        "		float inpr = (lp.z < 0.0) ? 1.0 / sqrt(1.0 + dot(xy, xy)) : 0.0;\n"
        "		float t = inpr - L.spot.x;\n"
        "		if (t <= 0.0) return vec3(0.0);\n"
        "		if (L.spot.y > 0.0 && t < L.spot.y) { t /= L.spot.y; v *= t * t * (3.0 - 2.0 * t); }\n"
        "		halo = L.extra.x;\n"
        "	}\n"
        "	if (v <= 0.0) return vec3(0.0);\n"
        "	return col * v * lamp_shadow(L, w);\n"
        "}\n"
;

const char *gpu_lights_lamp_glsl(void)
{
	return lamp_glsl;
}

static const char *fog_inject_cs_main =
        "layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;\n"
        "layout(rgba16f, binding = 0) uniform writeonly image3D cells;\n"
        "uniform mat4 projinv, viewinv, view;\n"
        "uniform ivec3 grid;\n"
        "uniform vec4 zr;\n"         /* near, far, ln(far / near), orthographic */
        "uniform vec4 mist0;\n"      /* on, type, start, distance */
        "uniform vec4 mist1;\n"      /* intensity, height */
        "uniform vec3 mistcol;\n"
        "uniform int lamps_on;\n"
        "float slice_z(float k) { return zr.x * exp(zr.z * k / float(grid.z)); }\n"
        "vec3 at_depth(vec2 ndc, float z)\n"
        "{\n"
        "	vec4 a = projinv * vec4(ndc, -1.0, 1.0);\n"
        "	vec4 b = projinv * vec4(ndc, 1.0, 1.0);\n"
        "	a /= a.w;\n"
        "	b /= b.w;\n"
        "	return mix(a.xyz, b.xyz, (-z - a.z) / (b.z - a.z));\n"
        "}\n"
        /* the mist's visibility at a point (F12's mistfactor) */
        "float mist_vis(vec3 p)\n"
        "{\n"
        "	if (mist0.x < 0.5) return 1.0;\n"
        "	float zc = (zr.w > 0.5) ? -p.z : length(p);\n"
        "	float f = clamp((zc - mist0.z) / max(mist0.w, 1e-6), 0.0, 1.0);\n"
        "	if (mist0.y < 0.5) f *= f; else if (mist0.y > 1.5) f = sqrt(f);\n"
        "	if (mist1.y != 0.0 && f != 0.0) {\n"
        "		float hi = (viewinv * vec4(p, 1.0)).z;\n"
        "		if (hi > mist1.y) f = 0.0;\n"
        "		else if (hi > 0.0) { hi = (mist1.y - hi) / mist1.y; f *= hi * hi; }\n"
        "	}\n"
        "	return (1.0 - f) * (1.0 - mist1.x);\n"
        "}\n"
        "void main()\n"
        "{\n"
        "	ivec3 c = ivec3(gl_GlobalInvocationID);\n"
        "	if (any(greaterThanEqual(c, grid))) return;\n"
        "	vec2 uv = (vec2(c.xy) + 0.5) / vec2(grid.xy);\n"
        "	vec2 ndc = uv * 2.0 - 1.0;\n"
        "	float z0 = (c.z == 0) ? 0.0 : slice_z(float(c.z)), z1 = slice_z(float(c.z + 1));\n"
        "	float zm = slice_z(float(c.z) + 0.5);\n"
        "	vec3 p0 = at_depth(ndc, max(z0, 1e-4)), p1 = at_depth(ndc, z1), pm = at_depth(ndc, zm);\n"
        "	float v0 = (c.z == 0) ? 1.0 : mist_vis(p0), v1 = mist_vis(p1);\n"
        "	float sigma = max(log(max(v0, 1e-6)) - log(max(v1, 1e-6)), 0.0) / max(z1 - z0, 1e-6);\n"
        "	vec3 scat = sigma * mistcol;\n"
        "	float ext = sigma;\n"
        "	if (lamps_on != 0) {\n"
        "		vec3 w = (viewinv * vec4(pm, 1.0)).xyz;\n"
        "		uvec2 t = uvec2(clamp(uv, vec2(0.0), vec2(0.99999)) * vec2(16.0, 9.0));\n"
        "		uint k = uint(clamp(log(max(zm, cl_depth.x) / cl_depth.x) * cl_depth.z, 0.0, 23.0));\n"
        "		uint ci = (k * 9u + t.y) * 16u + t.x;\n"
        "		uint W = cl_counts.x;\n"
        "		for (uint wd = 0u; wd < W; wd++) {\n"
        "			uint bits = cl_mask[ci * W + wd];\n"
        "			while (bits != 0u) {\n"
        "				int b = findLSB(bits);\n"
        "				bits &= bits - 1u;\n"
        "				float halo;\n"
        "				vec3 L = lamp_light(lamps[wd * 32u + uint(b)], pm, w, halo);\n"
        "				float sh = 0.02 * halo;\n"   /* a halo's own medium in its cone */
        "				scat += (sigma + sh) * L * 0.07957747;\n"  /* isotropic: 1 / 4 pi */
        "				ext += sh;\n"
        "			}\n"
        "		}\n"
        "	}\n"
        "	imageStore(cells, c, vec4(scat, ext));\n"
        "}\n";

static const char *fog_sum_cs =
        "layout(local_size_x = 8, local_size_y = 8) in;\n"
        "layout(rgba16f, binding = 0) uniform readonly image3D cells;\n"
        "layout(rgba16f, binding = 1) uniform writeonly image3D sums;\n"
        "uniform ivec3 grid;\n"
        "uniform vec4 zr;\n"
        "float slice_z(float k) { return zr.x * exp(zr.z * k / float(grid.z)); }\n"
        "void main()\n"
        "{\n"
        "	ivec2 c = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (any(greaterThanEqual(c, grid.xy))) return;\n"
        "	vec3 S = vec3(0.0);\n"
        "	float T = 1.0;\n"
        "	for (int k = 0; k < grid.z; k++) {\n"
        "		vec4 s = imageLoad(cells, ivec3(c, k));\n"
        "		float dz = slice_z(float(k + 1)) - ((k == 0) ? 0.0 : slice_z(float(k)));\n"
        "		float tk = exp(-s.a * dz);\n"
        "		S += T * ((s.a > 1e-6) ? s.rgb / s.a * (1.0 - tk) : s.rgb * dz);\n"
        "		T *= tk;\n"
        "		imageStore(sums, ivec3(c, k), vec4(S, T));\n"
        "	}\n"
        "}\n";

static struct {
	GLuint prog_inject, prog_sum;
	bool failed;
	GLuint cells, sums;
	int grid[3];
	float zr[4];
	bool valid;
} fog = {0};

static void fog_grid_size(int r_grid[3])
{
	r_grid[0] = 160;
	r_grid[1] = 90;
	r_grid[2] = 64;
	const char *env = getenv("BGE_REMIX_FOG_GRID");
	if (env && env[0]) {
		int x, y, z;
		if (sscanf(env, "%dx%dx%d", &x, &y, &z) == 3 && x > 0 && y > 0 && z > 0) {
			r_grid[0] = x;
			r_grid[1] = y;
			r_grid[2] = z;
		}
	}
}

bool GPU_fog_grid_wanted(void)
{
	float mist[9];
	gpu_world_mist_get(mist);
	return mist[0] > 0.5f || (cl.count > 0 && cl.halos);
}

bool GPU_fog_grid(const float view[4][4], const float proj[4][4])
{
	fog.valid = false;
	if (fog.failed) {
		return false;
	}
	if (!fog.prog_inject) {
		/* the work group's size first, then the lamps, then the pass */
		const char *nl = strchr(fog_inject_cs_main, '\n') + 1;
		const size_t n1 = (size_t)(nl - fog_inject_cs_main);
		char *src = MEM_mallocN(strlen(fog_inject_cs_main) + strlen(lamp_glsl) + 1, "fog source");
		memcpy(src, fog_inject_cs_main, n1);
		strcpy(src + n1, lamp_glsl);
		strcat(src, nl);
		fog.prog_inject = GPU_compute_program(src, "BGE-Remix fog cells");
		MEM_freeN(src);
		fog.prog_sum = GPU_compute_program(fog_sum_cs, "BGE-Remix fog sums");
		if (!fog.prog_inject || !fog.prog_sum) {
			fog.failed = true;
			return false;
		}
	}
	int grid[3];
	fog_grid_size(grid);
	if (!fog.cells || memcmp(grid, fog.grid, sizeof(grid)) != 0) {
		if (fog.cells) {
			glDeleteTextures(1, &fog.cells);
			glDeleteTextures(1, &fog.sums);
		}
		glCreateTextures(GL_TEXTURE_3D, 1, &fog.cells);
		glTextureStorage3D(fog.cells, 1, GL_RGBA16F, grid[0], grid[1], grid[2]);
		glCreateTextures(GL_TEXTURE_3D, 1, &fog.sums);
		glTextureStorage3D(fog.sums, 1, GL_RGBA16F, grid[0], grid[1], grid[2]);
		glTextureParameteri(fog.sums, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(fog.sums, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(fog.sums, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(fog.sums, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(fog.sums, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		GPU_debug_label(GL_TEXTURE, fog.sums, "BGE-Remix fog");
		memcpy(fog.grid, grid, sizeof(grid));
	}

	/* the depth range: the camera's, at most 2 km */
	const bool ortho = proj[3][3] != 0.0f;
	float znear, zfar;
	if (!ortho) {
		znear = proj[3][2] / (proj[2][2] - 1.0f);
		zfar = proj[3][2] / (proj[2][2] + 1.0f);
	}
	else {
		znear = (proj[3][2] + 1.0f) / proj[2][2];
		zfar = (proj[3][2] - 1.0f) / proj[2][2];
	}
	znear = max_ff(znear, 1e-2f);
	zfar = min_ff(max_ff(zfar, znear * 1.01f), 2000.0f);
	fog.zr[0] = znear;
	fog.zr[1] = zfar;
	fog.zr[2] = logf(zfar / znear);
	fog.zr[3] = ortho ? 1.0f : 0.0f;

	float mist[9], projinv[4][4], viewinv[4][4];
	gpu_world_mist_get(mist);
	m4_invert(projinv, proj);
	m4_invert(viewinv, view);

	GLint program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	GLuint p = fog.prog_inject;
	glUseProgram(p);
	glUniformMatrix4fv(glGetUniformLocation(p, "projinv"), 1, GL_FALSE, &projinv[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(p, "viewinv"), 1, GL_FALSE, &viewinv[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(p, "view"), 1, GL_FALSE, &view[0][0]);
	glUniform3iv(glGetUniformLocation(p, "grid"), 1, grid);
	glUniform4fv(glGetUniformLocation(p, "zr"), 1, fog.zr);
	glUniform4f(glGetUniformLocation(p, "mist0"), mist[0], mist[1], mist[2], mist[3]);
	glUniform4f(glGetUniformLocation(p, "mist1"), mist[4], mist[5], 0.0f, 0.0f);
	glUniform3fv(glGetUniformLocation(p, "mistcol"), 1, mist + 6);
	glUniform1i(glGetUniformLocation(p, "lamps_on"), (cl.lampbuf && cl.count > 0) ? 1 : 0);
	glUniform1i(glGetUniformLocation(p, "pool"), 0);
	glBindTextureUnit(0, pool.tex ? pool.tex : pool.blank);
	cluster_bind();
	glBindImageTexture(0, fog.cells, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((GLuint)(grid[0] + 3) / 4, (GLuint)(grid[1] + 3) / 4, (GLuint)(grid[2] + 3) / 4);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	p = fog.prog_sum;
	glUseProgram(p);
	glUniform3iv(glGetUniformLocation(p, "grid"), 1, grid);
	glUniform4fv(glGetUniformLocation(p, "zr"), 1, fog.zr);
	glBindImageTexture(0, fog.cells, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);
	glBindImageTexture(1, fog.sums, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
	glDispatchCompute((GLuint)(grid[0] + 7) / 8, (GLuint)(grid[1] + 7) / 8, 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
	glBindTextureUnit(0, 0);
	glUseProgram((GLuint)program);
	fog.valid = true;
	return true;
}

unsigned int GPU_fog_grid_texture(float r_zr[4], int r_depth[1])
{
	if (!fog.valid) {
		return 0;
	}
	memcpy(r_zr, fog.zr, sizeof(fog.zr));
	r_depth[0] = fog.grid[2];
	return fog.sums;
}

/* Blender's ramp blend (gpu_material.c has a node-making function of that
 * name) */
void gpu_ramp_blend_rgb(int type, float r_col[3], float fac, const float col[3])
{
	ramp_blend(type, r_col, fac, col);
}

void GPU_lights_exit(void)
{
	if (cl.prog) {
		glDeleteProgram(cl.prog);
	}
	GLuint bufs[3] = {cl.lampbuf, cl.infobuf, cl.maskbuf};
	for (int i = 0; i < 3; i++) {
		if (bufs[i]) {
			glDeleteBuffers(1, &bufs[i]);
		}
	}
	memset(&cl, 0, sizeof(cl));

	if (pool.tex) {
		glDeleteTextures(1, &pool.tex);
	}
	if (pool.blank) {
		glDeleteTextures(1, &pool.blank);
	}
	if (pool.fbo) {
		glDeleteFramebuffers(1, &pool.fbo);
	}
	if (pool.used) {
		MEM_freeN(pool.used);
	}
	memset(&pool, 0, sizeof(pool));
	if (fog.prog_inject) {
		glDeleteProgram(fog.prog_inject);
	}
	if (fog.prog_sum) {
		glDeleteProgram(fog.prog_sum);
	}
	if (fog.cells) {
		glDeleteTextures(1, &fog.cells);
		glDeleteTextures(1, &fog.sums);
	}
	memset(&fog, 0, sizeof(fog));
}
