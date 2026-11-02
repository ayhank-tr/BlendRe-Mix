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

/** \file RAS_ScreenEffects.cpp
 *  \ingroup bgerastogl
 *
 * BGE-Remix P8 Part C (P8.23): screen effects (RAS_ScreenEffects.h).
 */

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

#include "RAS_ScreenEffects.h"

#include "GPU_glew.h"
extern "C" {
#include "BLI_math.h"
#include "GPU_glcheck.h"
#include "GPU_layer.h"
}

enum { P_AO = 0, P_DOF, P_MBLUR, P_BLOOM_PRE, P_BLOOM_DOWN, P_BLOOM_UP, P_BLOOM_ADD, P_LUM, P_EXPOSURE, P_COUNT };

#define CS_HEAD \
	"layout(local_size_x = 8, local_size_y = 8) in;\n" \
	"uniform ivec2 size;\n"

/* the picture times what the scene leaves of the light around each point:
 * the horizon of the depth around it in a few directions, out to the
 * World's AO distance */
static const char *cs_ao =
        CS_HEAD
        "uniform sampler2D src, depth;\n"
        "layout(rgba16f, binding = 0) uniform writeonly image2D dst;\n"
        "uniform mat4 projinv, proj;\n"
        "uniform float radius, energy;\n"
        "vec3 at(ivec2 p)\n"
        "{\n"
        "	p = clamp(p, ivec2(0), size - 1);\n"
        "	vec2 uv = (vec2(p) + 0.5) / vec2(size);\n"
        "	float d = texelFetch(depth, p, 0).r;\n"
        "	vec4 v = projinv * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
        "	return v.xyz / v.w;\n"
        "}\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (any(greaterThanEqual(p, size))) return;\n"
        "	vec4 c = texelFetch(src, p, 0);\n"
        "	float d = texelFetch(depth, p, 0).r;\n"
        "	if (d >= 1.0) { imageStore(dst, p, c); return; }\n"
        "	vec3 P = at(p);\n"
        "	vec3 dx = at(p + ivec2(1, 0)) - at(p - ivec2(1, 0));\n"
        "	vec3 dy = at(p + ivec2(0, 1)) - at(p - ivec2(0, 1));\n"
        "	vec3 N = normalize(cross(dx, dy));\n"
        "	if (dot(N, P) > 0.0) N = -N;\n"
        "	float rpx = radius * proj[0][0] * 0.5 * float(size.x) / max(-P.z, 1e-3);\n"
        "	rpx = clamp(rpx, 2.0, 64.0);\n"
        "	float noise = fract(52.9829189 * fract(dot(vec2(p), vec2(0.06711056, 0.00583715))));\n"
        "	float occ = 0.0;\n"
        "	const int DIRS = 6, STEPS = 6;\n"
        "	for (int i = 0; i < DIRS; i++) {\n"
        "		float a = (float(i) + noise) * 6.2831853 / float(DIRS);\n"
        "		vec2 dir = vec2(cos(a), sin(a));\n"
        "		float best = 0.0;\n"
        "		for (int s = 1; s <= STEPS; s++) {\n"
        "			vec2 o = dir * rpx * (float(s) - 0.5 * noise) / float(STEPS);\n"
        "			vec3 S = at(p + ivec2(round(o)));\n"
        "			vec3 v = S - P;\n"
        "			float l = length(v);\n"
        "			if (l < 1e-4 || l > radius) continue;\n"
        "			float h = dot(N, v / l) - 0.1;\n"
        "			best = max(best, h * (1.0 - l / radius));\n"
        "		}\n"
        "		occ += best;\n"
        "	}\n"
        "	float ao = clamp(1.0 - occ / float(DIRS), 0.0, 1.0);\n"
        "	imageStore(dst, p, vec4(c.rgb * mix(1.0, ao, energy), c.a));\n"
        "}\n";

/* the circle of confusion of a thin lens focused at "focus" (pixels), and a
 * gather over a disc of it */
static const char *cs_dof =
        CS_HEAD
        "uniform sampler2D src, depth;\n"
        "layout(rgba16f, binding = 0) uniform writeonly image2D dst;\n"
        "uniform vec3 camz;\n"            /* near, far, orthographic */
        "uniform vec4 lens;\n"            /* focus, focal length, aperture (f / N), pixels per metre on the sensor */
        "float viewz(float d)\n"
        "{\n"
        "	float n = camz.x, f = camz.y;\n"
        "	return (camz.z > 0.5) ? n + d * (f - n) : n * f / max(f - d * (f - n), 1e-8);\n"
        "}\n"
        "float coc(float d)\n"
        "{\n"
        "	float z = viewz(d);\n"
        "	float c = lens.z * lens.y * abs(z - lens.x) / max(z * abs(lens.x - lens.y), 1e-6);\n"
        "	return clamp(c * lens.w, 0.0, 24.0);\n"
        "}\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (any(greaterThanEqual(p, size))) return;\n"
        "	float r = coc(texelFetch(depth, p, 0).r);\n"
        "	vec4 c = texelFetch(src, p, 0);\n"
        "	if (r < 0.5) { imageStore(dst, p, c); return; }\n"
        "	vec4 sum = c;\n"
        "	float wsum = 1.0;\n"
        "	const int TAPS = 48;\n"
        "	for (int i = 1; i < TAPS; i++) {\n"
        "		float t = sqrt((float(i) + 0.5) / float(TAPS));\n"
        "		float a = float(i) * 2.3999632;\n"
        "		vec2 o = vec2(cos(a), sin(a)) * t * r;\n"
        "		ivec2 q = clamp(p + ivec2(round(o)), ivec2(0), size - 1);\n"
        "		float rq = coc(texelFetch(depth, q, 0).r);\n"
        /* a tap counts when its own blur reaches this far (no sharp bleed) */
        "		float w = smoothstep(0.0, 1.0, rq - length(o) + 1.0);\n"
        "		sum += texelFetch(src, q, 0) * w;\n"
        "		wsum += w;\n"
        "	}\n"
        "	imageStore(dst, p, sum / wsum);\n"
        "}\n";

/* the camera's motion during the shutter: each pixel's place last frame
 * (its depth through last frame's view) */
static const char *cs_mblur =
        CS_HEAD
        "uniform sampler2D src, depth;\n"
        "layout(rgba16f, binding = 0) uniform writeonly image2D dst;\n"
        "uniform mat4 reproject;\n"       /* this frame's clip space to last frame's */
        "uniform float shutter;\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (any(greaterThanEqual(p, size))) return;\n"
        "	vec2 uv = (vec2(p) + 0.5) / vec2(size);\n"
        "	float d = texelFetch(depth, p, 0).r;\n"
        "	vec4 prev = reproject * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
        "	vec2 puv = prev.xy / max(prev.w, 1e-6) * 0.5 + 0.5;\n"
        "	vec2 v = (uv - puv) * vec2(size) * shutter;\n"
        "	float l = length(v);\n"
        "	vec4 c = texelFetch(src, p, 0);\n"
        "	if (l < 0.5) { imageStore(dst, p, c); return; }\n"
        "	if (l > 32.0) v *= 32.0 / l;\n"
        "	vec4 sum = vec4(0.0);\n"
        "	const int N = 12;\n"
        "	for (int i = 0; i < N; i++) {\n"
        "		vec2 o = v * ((float(i) + 0.5) / float(N) - 0.5);\n"
        "		sum += texelFetch(src, clamp(p + ivec2(round(o)), ivec2(0), size - 1), 0);\n"
        "	}\n"
        "	imageStore(dst, p, sum / float(N));\n"
        "}\n";

/* bloom: what is brighter than the threshold (soft knee), at half size */
static const char *cs_bloom_pre =
        CS_HEAD
        "uniform sampler2D src;\n"
        "layout(rgba16f, binding = 0) uniform writeonly image2D dst;\n"
        "uniform float threshold;\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (any(greaterThanEqual(p, size))) return;\n"
        "	vec2 uv = (vec2(p) + 0.5) / vec2(size);\n"
        "	vec3 c = textureLod(src, uv, 0.0).rgb;\n"
        "	float b = max(c.r, max(c.g, c.b));\n"
        "	float knee = threshold * 0.5;\n"
        "	float soft = clamp(b - threshold + knee, 0.0, 2.0 * knee);\n"
        "	soft = soft * soft / (4.0 * knee + 1e-5);\n"
        "	float w = max(soft, b - threshold) / max(b, 1e-5);\n"
        "	imageStore(dst, p, vec4(c * w, 1.0));\n"
        "}\n";

static const char *cs_bloom_down =
        CS_HEAD
        "uniform sampler2D src;\n"
        "uniform float level;\n"
        "layout(rgba16f, binding = 0) uniform writeonly image2D dst;\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (any(greaterThanEqual(p, size))) return;\n"
        "	vec2 t = 1.0 / vec2(size);\n"
        "	vec2 uv = (vec2(p) + 0.5) * t;\n"
        "	vec3 c = textureLod(src, uv, level).rgb * 0.5;\n"
        "	c += textureLod(src, uv + vec2(-0.5, -0.5) * t, level).rgb * 0.125;\n"
        "	c += textureLod(src, uv + vec2(0.5, -0.5) * t, level).rgb * 0.125;\n"
        "	c += textureLod(src, uv + vec2(-0.5, 0.5) * t, level).rgb * 0.125;\n"
        "	c += textureLod(src, uv + vec2(0.5, 0.5) * t, level).rgb * 0.125;\n"
        "	imageStore(dst, p, vec4(c, 1.0));\n"
        "}\n";

/* each level gets the one below it (larger) added, blurred by a tent */
static const char *cs_bloom_up =
        CS_HEAD
        "uniform sampler2D src;\n"
        "uniform float level;\n"           /* the smaller level, read */
        "layout(rgba16f, binding = 0) uniform image2D dst;\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (any(greaterThanEqual(p, size))) return;\n"
        "	vec2 t = 1.0 / vec2(size);\n"
        "	vec2 uv = (vec2(p) + 0.5) * t;\n"
        "	vec3 c = vec3(0.0);\n"
        "	for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {\n"
        "		float w = (2.0 - abs(float(i))) * (2.0 - abs(float(j))) / 16.0;\n"
        "		c += textureLod(src, uv + vec2(i, j) * t, level).rgb * w;\n"
        "	}\n"
        "	vec4 own = imageLoad(dst, p);\n"
        "	imageStore(dst, p, vec4(own.rgb + c, 1.0));\n"
        "}\n";

static const char *cs_bloom_add =
        CS_HEAD
        "uniform sampler2D src, bloom;\n"
        "uniform float intensity;\n"
        "layout(rgba16f, binding = 0) uniform writeonly image2D dst;\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (any(greaterThanEqual(p, size))) return;\n"
        "	vec2 uv = (vec2(p) + 0.5) / vec2(size);\n"
        "	vec4 c = texelFetch(src, p, 0);\n"
        "	vec3 b = textureLod(bloom, uv, 0.0).rgb;\n"
        "	imageStore(dst, p, vec4(c.rgb + b * intensity * c.a, c.a));\n"
        "}\n";

/* log luminance at a quarter of the size (its mips give the mean) */
static const char *cs_lum =
        CS_HEAD
        "uniform sampler2D src;\n"
        "layout(r16f, binding = 0) uniform writeonly image2D dst;\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (any(greaterThanEqual(p, size))) return;\n"
        "	vec2 uv = (vec2(p) + 0.5) / vec2(size);\n"
        "	vec3 c = textureLod(src, uv, 0.0).rgb;\n"
        "	float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
        "	imageStore(dst, p, vec4(log2(max(l, 1e-4)), 0.0, 0.0, 0.0));\n"
        "}\n";

/* the exposure follows the mean brightness (middle grey at 0.18) */
static const char *cs_exposure =
        "layout(local_size_x = 1) in;\n"
        "uniform sampler2D lum;\n"
        "uniform float top, rate;\n"         /* the 1x1 mip; 1 - exp(-dt * speed), 1 to start */
        "layout(r32f, binding = 0) uniform image2D dst;\n"
        "void main()\n"
        "{\n"
        "	float avg = textureLod(lum, vec2(0.5), top).r;\n"
        "	float target = clamp(0.18 / exp2(avg), 1.0 / 64.0, 64.0);\n"
        "	float prev = imageLoad(dst, ivec2(0)).r;\n"
        "	if (!(prev > 0.0)) prev = target;\n"
        "	imageStore(dst, ivec2(0), vec4(mix(prev, target, rate), 0.0, 0.0, 0.0));\n"
        "}\n";

RAS_ScreenEffects::RAS_ScreenEffects()
    : m_failed(false), m_ao(0), m_bloom(0), m_bloom_levels(0), m_lum(0), m_lum_levels(0), m_exposure(0),
      m_exp_on(false), m_exp_reset(true), m_sampler_linear(0), m_sampler_point(0), m_have_prev(false),
      m_last_time(0.0)
{
	memset(m_prog, 0, sizeof(m_prog));
	m_size[0] = m_size[1] = 0;
	m_tex[0] = m_tex[1] = 0;
	memset(m_prev_vp, 0, sizeof(m_prev_vp));
}

RAS_ScreenEffects::~RAS_ScreenEffects()
{
	Free();
}

void RAS_ScreenEffects::Free()
{
	for (int i = 0; i < P_COUNT; i++) {
		if (m_prog[i])
			glDeleteProgram(m_prog[i]);
		m_prog[i] = 0;
	}
	if (m_tex[0])
		glDeleteTextures(2, m_tex);
	if (m_bloom)
		glDeleteTextures(1, &m_bloom);
	if (m_lum)
		glDeleteTextures(1, &m_lum);
	if (m_exposure)
		glDeleteTextures(1, &m_exposure);
	if (m_sampler_linear)
		glDeleteSamplers(1, &m_sampler_linear);
	if (m_sampler_point)
		glDeleteSamplers(1, &m_sampler_point);
	m_tex[0] = m_tex[1] = m_bloom = m_lum = m_exposure = m_sampler_linear = m_sampler_point = 0;
	m_size[0] = m_size[1] = 0;
	m_failed = false;
	m_have_prev = false;
	m_exp_reset = true;
}

#define effect_programs m_prog

bool RAS_ScreenEffects::Programs()
{
	if (m_failed)
		return false;
	if (effect_programs[0])
		return true;
	const char *src[P_COUNT] = {cs_ao, cs_dof, cs_mblur, cs_bloom_pre, cs_bloom_down, cs_bloom_up, cs_bloom_add,
	                            cs_lum, cs_exposure};
	const char *names[P_COUNT] = {"BGE-Remix ambient occlusion", "BGE-Remix depth of field", "BGE-Remix motion blur",
	                              "BGE-Remix bloom (bright)", "BGE-Remix bloom (down)", "BGE-Remix bloom (up)",
	                              "BGE-Remix bloom (add)", "BGE-Remix brightness", "BGE-Remix exposure"};
	for (int i = 0; i < P_COUNT; i++) {
		effect_programs[i] = GPU_compute_program(src[i], names[i]);
		if (!effect_programs[i]) {
			for (int k = 0; k < i; k++) {
				glDeleteProgram(effect_programs[k]);
				effect_programs[k] = 0;
			}
			m_failed = true;
			printf("BGE-Remix: the screen effects are off (a pass does not build)\n");
			return false;
		}
	}
	return true;
}

static unsigned int tex2d(GLenum format, int w, int h, int levels, const char *label)
{
	GLuint t = 0;
	glCreateTextures(GL_TEXTURE_2D, 1, &t);
	glTextureStorage2D(t, levels, format, w, h);
	glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	GPU_debug_label(GL_TEXTURE, t, label);
	return t;
}

void RAS_ScreenEffects::Targets(int w, int h)
{
	if (m_tex[0] && m_size[0] == w && m_size[1] == h)
		return;
	if (m_tex[0])
		glDeleteTextures(2, m_tex);
	if (m_bloom)
		glDeleteTextures(1, &m_bloom);
	if (m_lum)
		glDeleteTextures(1, &m_lum);
	m_tex[0] = tex2d(GL_RGBA16F, w, h, 1, "BGE-Remix effects A");
	m_tex[1] = tex2d(GL_RGBA16F, w, h, 1, "BGE-Remix effects B");
	const int bw = std::max(w / 2, 1), bh = std::max(h / 2, 1);
	m_bloom_levels = 1;
	while (m_bloom_levels < 8 && (bw >> m_bloom_levels) >= 4 && (bh >> m_bloom_levels) >= 4)
		m_bloom_levels++;
	m_bloom = tex2d(GL_RGBA16F, bw, bh, m_bloom_levels, "BGE-Remix bloom");
	const int lw = std::max(w / 4, 1), lh = std::max(h / 4, 1);
	m_lum_levels = 1;
	while ((std::max(lw, lh) >> m_lum_levels) > 0)
		m_lum_levels++;
	m_lum = tex2d(GL_R16F, lw, lh, m_lum_levels, "BGE-Remix brightness");
	if (!m_exposure) {
		m_exposure = tex2d(GL_R32F, 1, 1, 1, "BGE-Remix exposure");
		m_exp_reset = true;
	}
	m_size[0] = w;
	m_size[1] = h;
}

static void dispatch(int w, int h)
{
	glDispatchCompute((GLuint)((w + 7) / 8), (GLuint)((h + 7) / 8), 1);
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

static void use(GLuint p, int w, int h)
{
	glUseProgram(p);
	glUniform2i(glGetUniformLocation(p, "size"), w, h);
}

unsigned int RAS_ScreenEffects::Run(unsigned int color, unsigned int depth, int w, int h, const float view[16],
                                    const float proj[16], const Settings &s, double time)
{
	m_exp_on = false;
	float vp[4][4];
	mul_m4_m4m4(vp, (float (*)[4])proj, (float (*)[4])view);
	const bool have_prev = m_have_prev;
	float prev_vp[4][4];
	memcpy(prev_vp, m_prev_vp, sizeof(prev_vp));
	memcpy(m_prev_vp, vp, sizeof(m_prev_vp));
	m_have_prev = true;
	const float dt = (float)std::max(time - m_last_time, 0.0);
	m_last_time = time;

	if (!s.Any() || w <= 0 || h <= 0 || !Programs())
		return 0;
	Targets(w, h);
	if (!m_sampler_linear) {
		glCreateSamplers(1, &m_sampler_linear);
		glSamplerParameteri(m_sampler_linear, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glSamplerParameteri(m_sampler_linear, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(m_sampler_linear, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(m_sampler_linear, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glCreateSamplers(1, &m_sampler_point);
		glSamplerParameteri(m_sampler_point, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glSamplerParameteri(m_sampler_point, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glSamplerParameteri(m_sampler_point, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(m_sampler_point, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	GLint prog = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prog);

	unsigned int cur = color;
	int next = 0;
	/* src on unit 0 (linear), depth on unit 1 (point), the rest on 2 */
	glBindSampler(0, m_sampler_linear);
	glBindSampler(1, m_sampler_point);
	glBindSampler(2, m_sampler_linear);
	glBindTextureUnit(1, depth);

	float projinv[4][4];
	invert_m4_m4(projinv, (float (*)[4])proj);
	const bool ortho = proj[15] != 0.0f;
	const float zn = ortho ? (proj[14] + 1.0f) / proj[10] : proj[14] / (proj[10] - 1.0f);
	const float zf = ortho ? (proj[14] - 1.0f) / proj[10] : proj[14] / (proj[10] + 1.0f);

	struct Pass {
		static void bind_io(GLuint p, unsigned int src, unsigned int dst)
		{
			glBindTextureUnit(0, src);
			glUniform1i(glGetUniformLocation(p, "src"), 0);
			glUniform1i(glGetUniformLocation(p, "depth"), 1);
			glBindImageTexture(0, dst, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		}
	};

	if (s.ao) {
		const GLuint p = effect_programs[P_AO];
		use(p, w, h);
		glUniformMatrix4fv(glGetUniformLocation(p, "projinv"), 1, GL_FALSE, &projinv[0][0]);
		glUniformMatrix4fv(glGetUniformLocation(p, "proj"), 1, GL_FALSE, proj);
		glUniform1f(glGetUniformLocation(p, "radius"), std::max(s.ao_distance, 0.01f));
		glUniform1f(glGetUniformLocation(p, "energy"), std::min(std::max(s.ao_energy, 0.0f), 1.0f));
		Pass::bind_io(p, cur, m_tex[next]);
		dispatch(w, h);
		cur = m_tex[next];
		next ^= 1;
	}
	if (s.dof && s.fstop > 0.0f && !ortho) {
		const GLuint p = effect_programs[P_DOF];
		use(p, w, h);
		const float f = std::max(s.focal_mm, 1.0f) * 0.001f;
		const float aperture = f / std::max(s.fstop, 0.1f);
		const float px_per_m = (float)w / (std::max(s.sensor_mm, 1.0f) * 0.001f);
		glUniform3f(glGetUniformLocation(p, "camz"), zn, zf, 0.0f);
		glUniform4f(glGetUniformLocation(p, "lens"), std::max(s.focus, f * 1.01f), f, aperture, px_per_m);
		Pass::bind_io(p, cur, m_tex[next]);
		dispatch(w, h);
		cur = m_tex[next];
		next ^= 1;
	}
	if (s.mblur && have_prev && s.shutter > 0.0f) {
		const GLuint p = effect_programs[P_MBLUR];
		use(p, w, h);
		float inv_vp[4][4], reproject[4][4];
		invert_m4_m4(inv_vp, vp);
		mul_m4_m4m4(reproject, prev_vp, inv_vp);
		glUniformMatrix4fv(glGetUniformLocation(p, "reproject"), 1, GL_FALSE, &reproject[0][0]);
		glUniform1f(glGetUniformLocation(p, "shutter"), s.shutter);
		Pass::bind_io(p, cur, m_tex[next]);
		dispatch(w, h);
		cur = m_tex[next];
		next ^= 1;
	}
	if (s.bloom && s.bloom_intensity > 0.0f) {
		const int bw = std::max(w / 2, 1), bh = std::max(h / 2, 1);
		const int levels = std::min(m_bloom_levels, std::max(1, (int)(s.bloom_radius + 0.5f)));
		GLuint p = effect_programs[P_BLOOM_PRE];
		use(p, bw, bh);
		glUniform1f(glGetUniformLocation(p, "threshold"), std::max(s.bloom_threshold, 0.0f));
		glBindTextureUnit(0, cur);
		glUniform1i(glGetUniformLocation(p, "src"), 0);
		glBindImageTexture(0, m_bloom, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		dispatch(bw, bh);
		glBindTextureUnit(0, m_bloom);
		p = effect_programs[P_BLOOM_DOWN];
		for (int l = 1; l < levels; l++) {
			const int lw = std::max(bw >> l, 1), lh = std::max(bh >> l, 1);
			use(p, lw, lh);
			glUniform1i(glGetUniformLocation(p, "src"), 0);
			glUniform1f(glGetUniformLocation(p, "level"), (float)(l - 1));
			glBindImageTexture(0, m_bloom, l, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
			dispatch(lw, lh);
		}
		p = effect_programs[P_BLOOM_UP];
		for (int l = levels - 2; l >= 0; l--) {
			const int lw = std::max(bw >> l, 1), lh = std::max(bh >> l, 1);
			use(p, lw, lh);
			glUniform1i(glGetUniformLocation(p, "src"), 0);
			glUniform1f(glGetUniformLocation(p, "level"), (float)(l + 1));
			glBindImageTexture(0, m_bloom, l, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
			dispatch(lw, lh);
		}
		p = effect_programs[P_BLOOM_ADD];
		use(p, w, h);
		glUniform1f(glGetUniformLocation(p, "intensity"), s.bloom_intensity);
		glBindTextureUnit(2, m_bloom);
		glUniform1i(glGetUniformLocation(p, "bloom"), 2);
		Pass::bind_io(p, cur, m_tex[next]);
		dispatch(w, h);
		cur = m_tex[next];
		next ^= 1;
	}
	if (s.autoexp) {
		const int lw = std::max(w / 4, 1), lh = std::max(h / 4, 1);
		GLuint p = effect_programs[P_LUM];
		use(p, lw, lh);
		glBindTextureUnit(0, cur);
		glUniform1i(glGetUniformLocation(p, "src"), 0);
		glBindImageTexture(0, m_lum, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R16F);
		dispatch(lw, lh);
		glGenerateTextureMipmap(m_lum);
		p = effect_programs[P_EXPOSURE];
		glUseProgram(p);
		glBindTextureUnit(2, m_lum);
		glUniform1i(glGetUniformLocation(p, "lum"), 2);
		glUniform1f(glGetUniformLocation(p, "top"), (float)(m_lum_levels - 1));
		const float rate = m_exp_reset ? 1.0f : 1.0f - expf(-dt * std::max(s.exposure_speed, 0.01f));
		glUniform1f(glGetUniformLocation(p, "rate"), rate);
		glBindImageTexture(0, m_exposure, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);
		glDispatchCompute(1, 1, 1);
		glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		m_exp_reset = false;
		m_exp_on = true;
	}
	else {
		m_exp_reset = true;
	}

	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
	for (int u = 0; u < 3; u++) {
		glBindTextureUnit(u, 0);
		glBindSampler(u, 0);
	}
	glUseProgram((GLuint)prog);
	return (cur != color) ? cur : (m_exp_on ? color : 0);
}
