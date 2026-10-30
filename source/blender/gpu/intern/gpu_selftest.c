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

/** \file blender/gpu/intern/gpu_selftest.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.2: the drawing layer's self-test (T8.1). With
 * BGE_REMIX_GL_SELFTEST=<folder>, once OpenGL is up, every part of the layer
 * draws into a 256 x 256 picture: immediate rectangles and colours, a batch,
 * a texture, wide and dashed lines, round points, the classic look (lit, and
 * with fog), the streaming buffer filled past one part, and the matrix stack.
 * Known pixels are checked; the picture is written as layer_selftest.ppm;
 * the layer must leave OpenGL as it found it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_path_util.h"
#include "BLI_fileops.h"
#include "BLI_string.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_shader.h"

#include "gpu_private.h"

#define SIZE 256

typedef struct Check {
	const char *name;
	int x, y;
	unsigned char rgb[3];
	int tol;
	bool want_black;  /* the pixel must be (near) black instead */
} Check;

static int selftest_failures = 0;

static void report(const char *name, bool ok, const char *detail)
{
	printf("BGE-Remix layer self-test: %s %s%s%s\n", name, ok ? "ok" : "FAILED", detail ? " " : "", detail ? detail : "");
	if (!ok) {
		selftest_failures++;
	}
}

static void probe(const unsigned char *pix, const Check *c)
{
	const unsigned char *p = pix + 4 * (c->y * SIZE + c->x);
	bool ok;
	if (c->want_black) {
		ok = p[0] <= c->tol && p[1] <= c->tol && p[2] <= c->tol;
	}
	else {
		ok = abs(p[0] - c->rgb[0]) <= c->tol && abs(p[1] - c->rgb[1]) <= c->tol && abs(p[2] - c->rgb[2]) <= c->tol;
	}
	char detail[96];
	BLI_snprintf(detail, sizeof(detail), "(at %d,%d: %d %d %d)", c->x, c->y, p[0], p[1], p[2]);
	report(c->name, ok, detail);
}

static void write_ppm(const char *dir, const unsigned char *pix)
{
	char path[FILE_MAX];
	BLI_join_dirfile(path, sizeof(path), dir, "layer_selftest.ppm");
	FILE *f = BLI_fopen(path, "wb");
	if (!f) {
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", SIZE, SIZE);
	for (int y = SIZE - 1; y >= 0; y--) {
		for (int x = 0; x < SIZE; x++) {
			fwrite(pix + 4 * (y * SIZE + x), 3, 1, f);
		}
	}
	fclose(f);
}

static void rect(float x0, float y0, float x1, float y1)
{
	GPU_imm_vertex2f(x0, y0);
	GPU_imm_vertex2f(x1, y0);
	GPU_imm_vertex2f(x1, y1);
	GPU_imm_vertex2f(x0, y1);
}

int GPU_layer_selftest(const char *dir)
{
	GPUStateSnapshot before, after;
	GPUStateScope scope;
	selftest_failures = 0;

	GPU_state_snapshot(&before);
	GPU_state_scope_begin(&scope);

	/* the target */
	GLuint fbo, color, depth;
	glCreateFramebuffers(1, &fbo);
	glCreateTextures(GL_TEXTURE_2D, 1, &color);
	glTextureStorage2D(color, 1, GL_RGBA8, SIZE, SIZE);
	glCreateRenderbuffers(1, &depth);
	glNamedRenderbufferStorage(depth, GL_DEPTH_COMPONENT24, SIZE, SIZE);
	glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, color, 0);
	glNamedFramebufferRenderbuffer(fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
	glNamedFramebufferDrawBuffer(fbo, GL_COLOR_ATTACHMENT0);
	const bool complete = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	report("framebuffer", complete, NULL);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	GPU_state_viewport(0, 0, SIZE, SIZE);
	GPU_state_scissor(false, 0, 0, 0, 0);
	GPU_state_depth(false, true);
	GPU_state_blend(GPU_LAYER_BLEND_NONE);
	GPU_state_cull(false, true);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearDepth(1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* pixel coordinates */
	GPU_matrix_mode(GPU_MATRIX_PROJECTION);
	GPU_matrix_push();
	GPU_matrix_identity();
	GPU_matrix_ortho(0.0f, SIZE, 0.0f, SIZE, -100.0f, 100.0f);
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);
	GPU_matrix_push();
	GPU_matrix_identity();

	/* 1. a flat red rectangle (immediate, quads) */
	GPU_imm_uniform_color(1.0f, 0.0f, 0.0f, 1.0f);
	GPU_imm_begin(GL_QUADS, GPU_BUILTIN_FLAT);
	rect(8, 8, 40, 40);
	GPU_imm_end();

	/* 2. vertex colours (a green and blue quad, moved by the matrix stack) */
	GPU_matrix_push();
	GPU_matrix_translate(48.0f, 0.0f, 0.0f);
	GPU_imm_begin(GL_TRIANGLE_FAN, GPU_BUILTIN_SMOOTH);
	GPU_imm_color4f(0.0f, 1.0f, 0.0f, 1.0f);
	GPU_imm_vertex2f(8, 8);
	GPU_imm_vertex2f(40, 8);
	GPU_imm_color4f(0.0f, 0.0f, 1.0f, 1.0f);
	GPU_imm_vertex2f(40, 40);
	GPU_imm_vertex2f(8, 40);
	GPU_imm_end();
	GPU_matrix_pop();

	/* 3. a batch: an indexed magenta square */
	{
		GPUVertFormat fmt;
		GPU_vertformat_clear(&fmt);
		GPU_vertformat_add(&fmt, "bge_pos", GPU_COMP_F32, 2, false);
		GPU_vertformat_add(&fmt, "bge_col", GPU_COMP_U8, 4, true);
		struct { float p[2]; unsigned char c[4]; } v[4] = {
			{{104, 8}, {255, 0, 255, 255}}, {{136, 8}, {255, 0, 255, 255}},
			{{136, 40}, {255, 0, 255, 255}}, {{104, 40}, {255, 0, 255, 255}},
		};
		const unsigned int idx[6] = {0, 1, 2, 0, 2, 3};
		GPUBatch *b = GPU_batch_create(GL_TRIANGLES, &fmt, v, 4, idx, 6, false, "self-test batch");
		GPU_batch_draw(b, GPU_builtin_shader(GPU_BUILTIN_SMOOTH));
		GPU_batch_free(b);
	}

	/* 4. a 4 x 4 checker texture, nearest filtering */
	GLuint tex;
	{
		unsigned char texels[4 * 4 * 4];
		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				const unsigned char c = ((x + y) & 1) ? 255 : 0;
				unsigned char *t = texels + 4 * (y * 4 + x);
				t[0] = t[1] = t[2] = c;
				t[3] = 255;
			}
		}
		glCreateTextures(GL_TEXTURE_2D, 1, &tex);
		glTextureStorage2D(tex, 1, GL_RGBA8, 4, 4);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTextureSubImage2D(tex, 0, 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, texels);
		glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glBindTextureUnit(0, tex);
		GPU_imm_color4f(1.0f, 1.0f, 1.0f, 1.0f);
		GPU_imm_begin(GL_QUADS, GPU_BUILTIN_IMAGE);
		GPU_imm_texco2f(0, 0); GPU_imm_vertex2f(152, 8);
		GPU_imm_texco2f(1, 0); GPU_imm_vertex2f(184, 8);
		GPU_imm_texco2f(1, 1); GPU_imm_vertex2f(184, 40);
		GPU_imm_texco2f(0, 1); GPU_imm_vertex2f(152, 40);
		GPU_imm_end();
		glBindTextureUnit(0, 0);
	}

	/* 5. a 6-pixel blue line, and a dashed white one */
	GPU_imm_color4f(0.0f, 0.0f, 1.0f, 1.0f);
	GPU_line_style(6.0f, 0.0f, 0.0f);
	GPU_imm_begin(GL_LINES, GPU_BUILTIN_LINE);
	GPU_imm_vertex2f(8, 64);
	GPU_imm_vertex2f(248, 64);
	GPU_imm_end();
	GPU_imm_color4f(1.0f, 1.0f, 1.0f, 1.0f);
	GPU_line_style(2.0f, 10.0f, 10.0f);
	GPU_imm_begin(GL_LINES, GPU_BUILTIN_LINE);
	GPU_imm_vertex2f(8, 80);
	GPU_imm_vertex2f(248, 80);
	GPU_imm_end();
	GPU_line_style(1.0f, 0.0f, 0.0f);

	/* 6. a round yellow point, 12 pixels */
	GPU_point_style(12.0f, true);
	GPU_imm_color4f(1.0f, 1.0f, 0.0f, 1.0f);
	GPU_imm_begin(GL_POINTS, GPU_BUILTIN_POINT);
	GPU_imm_vertex2f(216.0f, 24.0f);
	GPU_imm_end();
	GPU_point_style(1.0f, false);

	/* 7. the classic look: lit by one light from the viewer (diffuse 1, the
	 * material 0.5 grey), then the same with linear fog half way to white */
	{
		struct GPUShader *cl = GPU_builtin_shader(GPU_BUILTIN_CLASSIC);
		GPUClassicState st;
		GPU_classic_state_default(&st);
		st.lighting = true;
		st.light_count = 1;
		st.lights[0].position[0] = 0.0f;
		st.lights[0].position[1] = 0.0f;
		st.lights[0].position[2] = 1.0f;
		st.lights[0].position[3] = 0.0f;
		for (int k = 0; k < 3; k++) {
			st.lights[0].ambient[k] = 0.0f;
			st.lights[0].diffuse[k] = 1.0f;
			st.lights[0].specular[k] = 0.0f;
			st.scene_ambient[k] = 0.0f;
			st.mat_diffuse[k] = 0.5f;
		}
		GPUVertFormat fmt;
		GPU_vertformat_clear(&fmt);
		GPU_vertformat_add(&fmt, "bge_pos", GPU_COMP_F32, 3, false);
		GPU_vertformat_add(&fmt, "bge_nor", GPU_COMP_F32, 3, false);
		const float v[4][6] = {
			{8, 96, -1, 0, 0, 1}, {72, 96, -1, 0, 0, 1}, {72, 160, -1, 0, 0, 1}, {8, 160, -1, 0, 0, 1},
		};
		const unsigned int idx[6] = {0, 1, 2, 0, 2, 3};
		GPUBatch *b = GPU_batch_create(GL_TRIANGLES, &fmt, v, 4, idx, 6, false, "self-test classic");
		GPU_classic_uniforms(cl, &st);
		GPU_batch_draw(b, cl);

		st.fog = GPU_FOG_LINEAR;
		st.fog_start = 0.0f;
		st.fog_end = 2.0f;
		st.fog_color[0] = st.fog_color[1] = st.fog_color[2] = st.fog_color[3] = 1.0f;
		GPU_matrix_push();
		GPU_matrix_translate(80.0f, 0.0f, 0.0f);
		GPU_classic_uniforms(cl, &st);
		GPU_batch_draw(b, cl);
		GPU_matrix_pop();
		GPU_batch_free(b);
	}

	/* 8. the streaming buffer filled past one part: 30,000 small quads */
	GPU_imm_uniform_color(0.0f, 1.0f, 1.0f, 1.0f);
	for (int i = 0; i < 30000; i++) {
		GPU_imm_begin(GL_QUADS, GPU_BUILTIN_FLAT);
		rect(176.0f + (float)(i % 8), 96.0f, 240.0f, 160.0f);
		GPU_imm_end();
	}

	GPU_matrix_pop();
	GPU_matrix_mode(GPU_MATRIX_PROJECTION);
	GPU_matrix_pop();
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);

	/* read back and check */
	unsigned char *pix = MEM_mallocN(SIZE * SIZE * 4, "self-test pixels");
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pix);
	const Check checks[] = {
		{"immediate flat colour", 24, 24, {255, 0, 0}, 2, false},
		{"background stays black", 4, 4, {0, 0, 0}, 2, true},
		{"vertex colours, green corner", 58, 10, {0, 247, 8}, 24, false},
		{"vertex colours, blue corner", 86, 38, {0, 8, 247}, 24, false},
		{"batch with indices", 120, 24, {255, 0, 255}, 2, false},
		{"texture, black cell", 156, 12, {0, 0, 0}, 2, true},
		{"texture, white cell", 164, 12, {255, 255, 255}, 2, false},
		{"wide line, centre", 128, 64, {0, 0, 255}, 2, false},
		{"wide line, 2 pixels off centre", 128, 66, {0, 0, 255}, 2, false},
		{"wide line, outside", 128, 70, {0, 0, 0}, 2, true},
		{"dashed line, dash", 12, 80, {255, 255, 255}, 2, false},
		{"dashed line, gap", 23, 80, {0, 0, 0}, 2, true},
		{"round point, centre", 216, 24, {255, 255, 0}, 2, false},
		{"round point, corner cut", 211, 19, {0, 0, 0}, 2, true},
		{"classic look, lit half grey", 40, 128, {128, 128, 128}, 3, false},
		{"classic look, linear fog half way", 120, 128, {191, 191, 191}, 4, false},
		{"streaming past one part", 208, 128, {0, 255, 255}, 2, false},
	};
	for (size_t i = 0; i < ARRAY_SIZE(checks); i++) {
		probe(pix, &checks[i]);
	}
	if (dir && dir[0]) {
		BLI_dir_create_recursive(dir);
		write_ppm(dir, pix);
	}
	MEM_freeN(pix);

	glDeleteTextures(1, &tex);
	GPU_stream_frame_end();
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &color);
	glDeleteRenderbuffers(1, &depth);

	/* the material library with the core front end */
	report("material library, core front end", GPU_codegen_selftest_core(), NULL);

	GPU_state_scope_end(&scope);
	GPU_state_snapshot(&after);
	const int left = GPU_state_compare(&before, &after, "layer self-test",
	                                   GPU_STATE_CHECK_MATRICES | GPU_STATE_CHECK_VIEWPORT);
	report("OpenGL state put back", left == 0, NULL);

	printf("BGE-Remix layer self-test: %d failed\n", selftest_failures);
	fflush(stdout);
	return selftest_failures;
}
