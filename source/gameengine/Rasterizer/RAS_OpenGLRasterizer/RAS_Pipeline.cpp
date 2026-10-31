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

/** \file gameengine/Rasterizer/RAS_OpenGLRasterizer/RAS_Pipeline.cpp
 *  \ingroup bgerastogl
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "RAS_Pipeline.h"

#include "GPU_glew.h"

extern "C" {
#include "DNA_color_types.h"
#include "DNA_scene_types.h"
#include "BKE_scene.h"
#include "IMB_colormanagement.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
#include "GPU_lights.h"
#include "GPU_shader.h"
}

/* the lookup texture: LUT_SIZE^3 over a log2 shaper from 2^LUT_MIN to 2^LUT_MAX */
#define LUT_SIZE 64
#define LUT_MIN -14.0f
#define LUT_MAX 8.0f

static const char *comp_vert =
        "layout(location = 0) in vec4 bge_pos;\n"
        "void main() { gl_Position = vec4(bge_pos.xy, 0.0, 1.0); }\n";

static const char *comp_frag =
        "#ifdef MULTISAMPLE\n"
        "uniform sampler2DMS bge_color;\n"
        "uniform sampler2DMS bge_depth;\n"
        "#else\n"
        "uniform sampler2D bge_color;\n"
        "uniform sampler2D bge_depth;\n"
        "#endif\n"
        "uniform sampler3D bge_lut;\n"
        "uniform int bge_samples;\n"
        "uniform int bge_modern;\n"
        "uniform float bge_exposure;\n"   /* a multiplier */
        "uniform float bge_gamma;\n"      /* the exponent (1 / gamma) */
        /* P8.17: the volumetric fog (GPU_fog_grid) */
        "uniform int bge_fog;\n"
        "uniform sampler3D bge_fogvol;\n"
        "uniform vec4 bge_fogzr;\n"      /* near, far, ln(far / near), slices */
        "uniform vec3 bge_camz;\n"       /* the camera's near, far, orthographic */
        "uniform vec4 bge_fogarea;\n"    /* the view's x, y, width, height */
        "out vec4 bge_fragcolor;\n"
        "const float LUT_N = " "64.0" ";\n"
        "const float LUT_LO = " "-14.0" ";\n"
        "const float LUT_HI = " "8.0" ";\n"
        "vec3 view(vec3 c)\n"
        "{\n"
        "	c = max(c * bge_exposure, vec3(0.0));\n"
        "	vec3 s = clamp((log2(max(c, vec3(1e-30))) - LUT_LO) / (LUT_HI - LUT_LO), 0.0, 1.0);\n"
        "	s = mix(vec3(0.0), s, step(vec3(1e-30), c));\n"
        "	vec3 d = texture(bge_lut, s * ((LUT_N - 1.0) / LUT_N) + 0.5 / LUT_N).rgb;\n"
        "	if (bge_gamma != 1.0) d = pow(max(d, vec3(0.0)), vec3(bge_gamma));\n"
        "	return d;\n"
        "}\n"
        "vec4 fogged(vec4 c, float d)\n"
        "{\n"
        "	if (bge_fog == 0 || d >= 1.0) return c;\n"
        "	float n = bge_camz.x, f = bge_camz.y;\n"
        "	float z = (bge_camz.z > 0.5) ? n + d * (f - n) : n * f / max(f - d * (f - n), 1e-8);\n"
        "	float s = log(max(z, 1e-6) / bge_fogzr.x) / bge_fogzr.z * bge_fogzr.w;\n"
        "	if (s <= 0.0) return c;\n"
        "	vec2 uv = (gl_FragCoord.xy - bge_fogarea.xy) / bge_fogarea.zw;\n"
        "	vec4 g = texture(bge_fogvol, vec3(uv, (clamp(s, 1.0, bge_fogzr.w) - 0.5) / bge_fogzr.w));\n"
        "	if (s < 1.0) g = mix(vec4(0.0, 0.0, 0.0, 1.0), g, s);\n"
        "	return vec4(c.rgb * g.a + g.rgb * c.a, c.a);\n"
        "}\n"
        "vec4 one(vec4 c)\n"
        "{\n"
        "	c.a = clamp(c.a, 0.0, 1.0);\n"
        "	if (bge_modern == 0) return c;\n"
        "	if (c.a <= 0.0) return vec4(0.0);\n"
        "	return vec4(view(c.rgb / c.a) * c.a, c.a);\n"
        "}\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_FragCoord.xy);\n"
        "#ifdef MULTISAMPLE\n"
        "	vec4 acc = vec4(0.0);\n"
        "	for (int i = 0; i < bge_samples; i++) acc += one(fogged(texelFetch(bge_color, p, i), texelFetch(bge_depth, p, i).r));\n"
        "	bge_fragcolor = acc / float(bge_samples);\n"
        "	gl_FragDepth = texelFetch(bge_depth, p, 0).r;\n"
        "#else\n"
        "	bge_fragcolor = one(fogged(texelFetch(bge_color, p, 0), texelFetch(bge_depth, p, 0).r));\n"
        "	gl_FragDepth = texelFetch(bge_depth, p, 0).r;\n"
        "#endif\n"
        "}\n";

RAS_Pipeline::RAS_Pipeline()
    :m_active(false),
    m_modern(false),
    m_samples(0),
    m_prevfb(0),
    m_fbo(0),
    m_color(0),
    m_depth(0),
    m_fbsamples(0),
    m_fbmodern(false),
    m_lut(0),
    m_exposure(1.0f),
    m_gamma(1.0f)
{
	m_area[0] = m_area[1] = m_area[2] = m_area[3] = 0;
	m_size[0] = m_size[1] = 0;
	m_shader[0] = m_shader[1] = NULL;
	m_shader_failed[0] = m_shader_failed[1] = false;
}

RAS_Pipeline::~RAS_Pipeline()
{
	Free();
}

void RAS_Pipeline::DepthTexture(unsigned int *r_texture, bool *r_multisample, int r_area[4]) const
{
	*r_texture = m_active ? m_depth : 0;
	*r_multisample = m_active && m_samples > 1;
	memcpy(r_area, m_area, sizeof(m_area));
}

void RAS_Pipeline::Free()
{
	if (m_fbo)
		glDeleteFramebuffers(1, &m_fbo);
	if (m_color)
		glDeleteTextures(1, &m_color);
	if (m_depth)
		glDeleteTextures(1, &m_depth);
	m_fbo = m_color = m_depth = 0;
	m_size[0] = m_size[1] = 0;
	for (int i = 0; i < 2; i++) {
		if (m_shader[i])
			GPU_shader_free(m_shader[i]);
		m_shader[i] = NULL;
		m_shader_failed[i] = false;
	}
	if (m_lut)
		glDeleteTextures(1, &m_lut);
	m_lut = 0;
	m_lutkey.clear();
	m_active = false;
}

bool RAS_Pipeline::MakeTarget(int width, int height, int samples, bool modern)
{
	if (m_fbo && m_size[0] == width && m_size[1] == height && m_fbsamples == samples && m_fbmodern == modern)
		return true;

	if (m_fbo)
		glDeleteFramebuffers(1, &m_fbo);
	if (m_color)
		glDeleteTextures(1, &m_color);
	if (m_depth)
		glDeleteTextures(1, &m_depth);
	m_fbo = m_color = m_depth = 0;

	const GLenum format = modern ? GL_RGBA16F : GL_RGBA8;
	if (samples > 1) {
		glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &m_color);
		glTextureStorage2DMultisample(m_color, samples, format, width, height, GL_TRUE);
		glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &m_depth);
		glTextureStorage2DMultisample(m_depth, samples, GL_DEPTH_COMPONENT24, width, height, GL_TRUE);
	}
	else {
		glCreateTextures(GL_TEXTURE_2D, 1, &m_color);
		glTextureStorage2D(m_color, 1, format, width, height);
		glCreateTextures(GL_TEXTURE_2D, 1, &m_depth);
		glTextureStorage2D(m_depth, 1, GL_DEPTH_COMPONENT24, width, height);
		for (int i = 0; i < 2; i++) {
			const GLuint tex = i ? m_depth : m_color;
			glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
	}
	glCreateFramebuffers(1, &m_fbo);
	glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, m_color, 0);
	glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, m_depth, 0);
	GPU_debug_label(GL_FRAMEBUFFER, m_fbo, modern ? "BGE-Remix Modern target" : "BGE-Remix Classic target");

	if (glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("BGE-Remix: the %s pipeline's target (%dx%d, %d samples) is not complete; the scene draws "
		       "straight into the window\n", modern ? "Modern" : "Classic", width, height, samples);
		glDeleteFramebuffers(1, &m_fbo);
		glDeleteTextures(1, &m_color);
		glDeleteTextures(1, &m_depth);
		m_fbo = m_color = m_depth = 0;
		return false;
	}
	m_size[0] = width;
	m_size[1] = height;
	m_fbsamples = samples;
	m_fbmodern = modern;
	return true;
}

void RAS_Pipeline::UpdateLUT(const Scene *scene)
{
	const ColorManagedViewSettings *vs = &scene->view_settings;
	const ColorManagedDisplaySettings *ds = &scene->display_settings;
	m_exposure = powf(2.0f, vs->exposure);
	m_gamma = (vs->gamma > 0.0f) ? 1.0f / vs->gamma : 1.0f;

	/* what the table is made from (exposure and gamma are the shader's) */
	char key[512];
	snprintf(key, sizeof(key), "%s|%s|%s|%d|%p", vs->view_transform, vs->look, ds->display_device,
	         vs->flag & COLORMANAGE_VIEW_USE_CURVES, (void *)vs->curve_mapping);
	if (m_lut && m_lutkey == key)
		return;
	m_lutkey = key;

	ColorManagedViewSettings flat = *vs;
	flat.exposure = 0.0f;
	flat.gamma = 1.0f;

	std::vector<float> table((size_t)LUT_SIZE * LUT_SIZE * LUT_SIZE * 3);
	float shaped[LUT_SIZE];
	for (int i = 0; i < LUT_SIZE; i++) {
		shaped[i] = (i == 0) ? 0.0f : powf(2.0f, LUT_MIN + (LUT_MAX - LUT_MIN) * (float)i / (LUT_SIZE - 1));
	}
	size_t n = 0;
	for (int b = 0; b < LUT_SIZE; b++) {
		for (int g = 0; g < LUT_SIZE; g++) {
			for (int r = 0; r < LUT_SIZE; r++) {
				table[n++] = shaped[r];
				table[n++] = shaped[g];
				table[n++] = shaped[b];
			}
		}
	}
	ColormanageProcessor *proc = IMB_colormanagement_display_processor_new(&flat, ds);
	if (proc) {
		IMB_colormanagement_processor_apply(proc, &table[0], LUT_SIZE * LUT_SIZE, LUT_SIZE, 3, false);
		IMB_colormanagement_processor_free(proc);
	}
	else {
		printf("BGE-Remix: the Modern pipeline's view transform could not be made; colours are shown as they are\n");
	}

	if (!m_lut) {
		glCreateTextures(GL_TEXTURE_3D, 1, &m_lut);
		glTextureStorage3D(m_lut, 1, GL_RGB16F, LUT_SIZE, LUT_SIZE, LUT_SIZE);
		glTextureParameteri(m_lut, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(m_lut, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(m_lut, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(m_lut, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(m_lut, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		GPU_debug_label(GL_TEXTURE, m_lut, "BGE-Remix view transform");
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTextureSubImage3D(m_lut, 0, 0, 0, 0, LUT_SIZE, LUT_SIZE, LUT_SIZE, GL_RGB, GL_FLOAT, &table[0]);
	printf("BGE-Remix: Modern pipeline: view transform \"%s\", look \"%s\", display \"%s\"\n",
	       vs->view_transform, vs->look[0] ? vs->look : "None", ds->display_device);
}

GPUShader *RAS_Pipeline::Shader(bool multisampled)
{
	const int i = multisampled ? 1 : 0;
	if (!m_shader[i] && !m_shader_failed[i]) {
		m_shader[i] = GPU_shader_create_ex(comp_vert, comp_frag, NULL, NULL,
		                                   multisampled ? "#define MULTISAMPLE\n" : NULL, 0, 0, 0,
		                                   GPU_SHADER_FLAGS_GLSL450 | GPU_SHADER_FLAGS_CORE);
		if (!m_shader[i]) {
			m_shader_failed[i] = true;
			printf("BGE-Remix: the pipeline's last pass does not build\n");
		}
	}
	return m_shader[i];
}

bool RAS_Pipeline::Begin(const Scene *scene, bool first, const float background[3], int samples,
                         const int area[4], const int clear[4])
{
	m_active = false;
	m_modern = scene && BKE_scene_game_pipeline(scene) == BKE_SCENE_GAME_PIPELINE_MODERN;
	m_samples = (samples > 1) ? samples : 0;
	if (m_samples) {
		GLint max_color = 0, max_depth = 0;
		glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &max_color);
		glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &max_depth);
		const int max_samples = (max_color < max_depth) ? max_color : max_depth;
		if (m_samples > max_samples)
			m_samples = (max_samples > 1) ? max_samples : 0;
	}
	if (!m_modern && !m_samples)
		return false;  /* Classic: straight into the window, as before */
	if (area[2] <= 0 || area[3] <= 0 || Shader(m_samples > 1) == NULL)
		return false;
	if (!MakeTarget(area[0] + area[2], area[1] + area[3], m_samples, m_modern))
		return false;
	if (m_modern)
		UpdateLUT(scene);

	memcpy(m_area, area, sizeof(m_area));
	m_active = true;

	GLint prev = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev);
	m_prevfb = (unsigned int)prev;
	glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

	/* transparent where the scene draws nothing (a later scene goes over the
	 * earlier ones by its alpha, the first keeps the framing bars), the
	 * first scene's background where the window was cleared */
	GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean colormask[4], depthmask;
	glGetBooleanv(GL_COLOR_WRITEMASK, colormask);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depthmask);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glDisable(GL_SCISSOR_TEST);
	const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	const float one = 1.0f;
	glClearNamedFramebufferfv(m_fbo, GL_COLOR, 0, zero);
	glClearNamedFramebufferfv(m_fbo, GL_DEPTH, 0, &one);
	if (first && clear[2] > 0 && clear[3] > 0) {
		const float bg[4] = {background[0], background[1], background[2], 1.0f};
		glEnable(GL_SCISSOR_TEST);
		glScissor(clear[0], clear[1], clear[2], clear[3]);
		glClearNamedFramebufferfv(m_fbo, GL_COLOR, 0, bg);
		glDisable(GL_SCISSOR_TEST);
	}
	if (scissor)
		glEnable(GL_SCISSOR_TEST);
	glColorMask(colormask[0], colormask[1], colormask[2], colormask[3]);
	glDepthMask(depthmask);
	return true;
}

void RAS_Pipeline::End(const float *view, const float *proj, const int *viewport)
{
	if (!m_active)
		return;
	m_active = false;

	/* P8.17: Modern scenes' volumetric fog, from the main camera's view */
	float fogzr[4] = {0.0f, 0.0f, 0.0f, 0.0f}, camz[3] = {0.1f, 100.0f, 0.0f};
	int fogdepth = 0;
	unsigned int fogtex = 0;
	if (m_modern && view && proj && GPU_fog_grid_wanted() &&
	    GPU_fog_grid((const float (*)[4])view, (const float (*)[4])proj))
	{
		fogtex = GPU_fog_grid_texture(fogzr, &fogdepth);
		fogzr[3] = (float)fogdepth;
		const bool ortho = proj[15] != 0.0f;
		camz[0] = ortho ? (proj[14] + 1.0f) / proj[10] : proj[14] / (proj[10] - 1.0f);
		camz[1] = ortho ? (proj[14] - 1.0f) / proj[10] : proj[14] / (proj[10] + 1.0f);
		camz[2] = ortho ? 1.0f : 0.0f;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, m_prevfb);
	GPUShader *shader = Shader(m_samples > 1);
	if (!shader)
		return;

	GPUStateScope scope;
	GPU_state_scope_begin(&scope);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	/* (the colour mask stays: anaglyph stereo's eye writes its channels) */
	/* premultiplied over what is there */
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	/* the scene's depth for the 2D filters */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);
	glDepthMask(GL_TRUE);
	glViewport(m_area[0], m_area[1], m_area[2], m_area[3]);

	const GLuint program = GPU_shader_program(shader);
	GPU_shader_bind(shader);
	glBindTextureUnit(0, m_color);
	glBindTextureUnit(1, m_depth);
	glUniform1i(glGetUniformLocation(program, "bge_color"), 0);
	glUniform1i(glGetUniformLocation(program, "bge_depth"), 1);
	GLint loc;
	if (m_modern && m_lut) {
		glBindTextureUnit(2, m_lut);
		if ((loc = glGetUniformLocation(program, "bge_lut")) >= 0)
			glUniform1i(loc, 2);
	}
	if ((loc = glGetUniformLocation(program, "bge_samples")) >= 0)
		glUniform1i(loc, m_samples);
	if ((loc = glGetUniformLocation(program, "bge_modern")) >= 0)
		glUniform1i(loc, (m_modern && m_lut) ? 1 : 0);
	if ((loc = glGetUniformLocation(program, "bge_exposure")) >= 0)
		glUniform1f(loc, m_exposure);
	if ((loc = glGetUniformLocation(program, "bge_gamma")) >= 0)
		glUniform1f(loc, m_gamma);
	if ((loc = glGetUniformLocation(program, "bge_fog")) >= 0)
		glUniform1i(loc, fogtex ? 1 : 0);
	if (fogtex) {
		glBindTextureUnit(3, fogtex);
		glUniform1i(glGetUniformLocation(program, "bge_fogvol"), 3);
		glUniform4fv(glGetUniformLocation(program, "bge_fogzr"), 1, fogzr);
		glUniform3fv(glGetUniformLocation(program, "bge_camz"), 1, camz);
		const int *vp = viewport ? viewport : m_area;
		glUniform4f(glGetUniformLocation(program, "bge_fogarea"), (float)vp[0], (float)vp[1], (float)vp[2],
		            (float)vp[3]);
	}
	GPU_draw_fullscreen_triangle();
	glBindTextureUnit(0, 0);
	glBindTextureUnit(1, 0);
	glBindTextureUnit(2, 0);
	glBindTextureUnit(3, 0);
	glDepthFunc(GL_LEQUAL);
	GPU_state_scope_end(&scope);
}
