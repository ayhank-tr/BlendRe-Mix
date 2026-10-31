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

#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_lights.h"

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
}

void GPU_lights_exit(void)
{
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
}
