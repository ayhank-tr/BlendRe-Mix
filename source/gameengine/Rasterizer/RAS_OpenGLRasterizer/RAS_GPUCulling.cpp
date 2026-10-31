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

/** \file gameengine/Rasterizer/RAS_OpenGLRasterizer/RAS_GPUCulling.cpp
 *  \ingroup bgerastogl
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "RAS_GPUCulling.h"

#include "MT_Matrix4x4.h"
#include "GPU_glew.h"

extern "C" {
#include "GPU_glcheck.h"
#include "GPU_layer.h"
}

/* level 0: the depth texture's viewport (a multisampled one: sample 0) */
static const char *cs_first =
        "layout(local_size_x = 8, local_size_y = 8) in;\n"
        "#ifdef MULTISAMPLE\n"
        "uniform sampler2DMS depth;\n"
        "#else\n"
        "uniform sampler2D depth;\n"
        "#endif\n"
        "uniform ivec4 viewport;\n"
        "layout(r32f, binding = 0) writeonly uniform image2D level0;\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (p.x >= viewport.z || p.y >= viewport.w) return;\n"
        "	float d = texelFetch(depth, p + viewport.xy, 0).r;\n"
        "	imageStore(level0, p, vec4(d));\n"
        "}\n";

/* level n: the farthest of the texels below it (an odd edge takes the third) */
static const char *cs_down =
        "layout(local_size_x = 8, local_size_y = 8) in;\n"
        "layout(r32f, binding = 0) readonly uniform image2D src;\n"
        "layout(r32f, binding = 1) writeonly uniform image2D dst;\n"
        "uniform ivec2 srcsize;\n"
        "uniform ivec2 dstsize;\n"
        "void main()\n"
        "{\n"
        "	ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "	if (p.x >= dstsize.x || p.y >= dstsize.y) return;\n"
        "	ivec2 s = p * 2;\n"
        "	int nx = (p.x == dstsize.x - 1 && (srcsize.x & 1) == 1) ? 3 : 2;\n"
        "	int ny = (p.y == dstsize.y - 1 && (srcsize.y & 1) == 1) ? 3 : 2;\n"
        "	float d = 0.0;\n"
        "	for (int y = 0; y < ny; y++)\n"
        "		for (int x = 0; x < nx; x++)\n"
        "			d = max(d, imageLoad(src, min(s + ivec2(x, y), srcsize - 1)).r);\n"
        "	imageStore(dst, p, vec4(d));\n"
        "}\n";

/* each instance's sphere against the pyramid, seen from the last frame */
static const char *cs_cull =
        "layout(local_size_x = 64) in;\n"
        "layout(std430, binding = 8) readonly buffer Matrices { mat4 mv[]; };\n"
        "layout(std430, binding = 9) writeonly buffer Visible { uint visible[]; };\n"
        "layout(std430, binding = 10) buffer Command { uint cmd[]; };\n"
        "uniform int count;\n"
        "uniform uint cmdbase;\n"     /* the command's first word */
        "uniform vec4 sphere;\n"      /* local centre, radius */
        "uniform mat4 toprev;\n"      /* this frame's view space to the last frame's */
        "uniform mat4 prevproj;\n"
        "uniform sampler2D pyramid;\n"
        "uniform vec2 pyrsize;\n"
        "uniform int levels;\n"
        "float depth_of(vec3 v)\n"
        "{\n"
        "	vec4 c = prevproj * vec4(v, 1.0);\n"
        "	return c.z / c.w * 0.5 + 0.5;\n"
        "}\n"
        "void main()\n"
        "{\n"
        "	uint i = gl_GlobalInvocationID.x;\n"
        "	if (i >= uint(count)) return;\n"
        "	mat4 m = mv[i];\n"
        "	vec3 cv = (m * vec4(sphere.xyz, 1.0)).xyz;\n"
        "	float s = max(length(m[0].xyz), max(length(m[1].xyz), length(m[2].xyz)));\n"
        "	float r = sphere.w * s;\n"
        "	vec3 pv = (toprev * vec4(cv, 1.0)).xyz;\n"
        "	bool shown = true;\n"
        "	bool persp = prevproj[2][3] != 0.0;\n"
        "	if (!persp || -pv.z - r > 0.0) {\n"
        "		vec4 pc = prevproj * vec4(pv, 1.0);\n"
        "		vec2 ndc = pc.xy / pc.w;\n"
        "		vec2 ext = vec2(r * abs(prevproj[0][0]), r * abs(prevproj[1][1])) / (persp ? (-pv.z - r) : 1.0);\n"
        "		vec2 lo = ndc - ext, hi = ndc + ext;\n"
        "		if (all(greaterThan(lo, vec2(-1.0))) && all(lessThan(hi, vec2(1.0)))) {\n"
        "			vec2 a = lo * 0.5 + 0.5, b = hi * 0.5 + 0.5;\n"
        "			vec2 px = (b - a) * pyrsize;\n"
        "			float lod = clamp(ceil(log2(max(max(px.x, px.y), 1.0))), 0.0, float(levels - 1));\n"
        "			float d = max(max(textureLod(pyramid, a, lod).r, textureLod(pyramid, vec2(b.x, a.y), lod).r),\n"
        "			              max(textureLod(pyramid, vec2(a.x, b.y), lod).r, textureLod(pyramid, b, lod).r));\n"
        "			float dnear = depth_of(vec3(pv.xy, pv.z + r));\n"
        "			if (dnear > d) shown = false;\n"
        "		}\n"
        "	}\n"
        "	if (shown) {\n"
        "		uint k = atomicAdd(cmd[cmdbase + 1u], 1u);\n"
        "		visible[cmd[cmdbase + 4u] + k] = i;\n"
        "	}\n"
        "}\n";

RAS_GPUCulling::RAS_GPUCulling()
    :m_valid(false),
    m_mainview(false),
    m_depthcopy(0),
    m_pyramid(0),
    m_levels(0),
    m_prog_first(0),
    m_prog_firstms(0),
    m_prog_down(0),
    m_prog_cull(0),
    m_failed(false),
    m_tested(0)
{
	m_size[0] = m_size[1] = 0;
	m_copysize[0] = m_copysize[1] = 0;
	memset(m_prevview, 0, sizeof(m_prevview));
	memset(m_prevproj, 0, sizeof(m_prevproj));
}

RAS_GPUCulling::~RAS_GPUCulling()
{
	Free();
}

void RAS_GPUCulling::Free()
{
	if (m_depthcopy)
		glDeleteTextures(1, &m_depthcopy);
	if (m_pyramid)
		glDeleteTextures(1, &m_pyramid);
	m_depthcopy = m_pyramid = 0;
	unsigned int *progs[4] = {&m_prog_first, &m_prog_firstms, &m_prog_down, &m_prog_cull};
	for (int i = 0; i < 4; i++) {
		if (*progs[i])
			glDeleteProgram(*progs[i]);
		*progs[i] = 0;
	}
	m_valid = false;
	m_failed = false;
}

bool RAS_GPUCulling::Programs()
{
	if (m_failed)
		return false;
	if (m_prog_cull)
		return true;
	char first_ms[4096];
	snprintf(first_ms, sizeof(first_ms), "#define MULTISAMPLE\n%s", cs_first);
	m_prog_first = GPU_compute_program(cs_first, "BGE-Remix depth pyramid");
	m_prog_firstms = GPU_compute_program(first_ms, "BGE-Remix depth pyramid (multisampled)");
	m_prog_down = GPU_compute_program(cs_down, "BGE-Remix depth pyramid level");
	m_prog_cull = GPU_compute_program(cs_cull, "BGE-Remix occlusion culling");
	if (!m_prog_first || !m_prog_firstms || !m_prog_down || !m_prog_cull) {
		printf("BGE-Remix: GPU occlusion culling is off (its compute programs do not build)\n");
		m_failed = true;
		return false;
	}
	return true;
}

void RAS_GPUCulling::Build(unsigned int depth_texture, bool multisample, const int viewport[4],
                           const float view[16], const float proj[16])
{
	m_valid = false;
	if (!GPU_speed_feature("gpu_culling") || viewport[2] < 2 || viewport[3] < 2 || !Programs())
		return;

	const int w = viewport[2], h = viewport[3];
	if (!m_pyramid || m_size[0] != w || m_size[1] != h) {
		if (m_pyramid)
			glDeleteTextures(1, &m_pyramid);
		m_levels = 1;
		for (int s = (w > h ? w : h); s > 1; s >>= 1)
			m_levels++;
		glCreateTextures(GL_TEXTURE_2D, 1, &m_pyramid);
		glTextureStorage2D(m_pyramid, m_levels, GL_R32F, w, h);
		glTextureParameteri(m_pyramid, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		glTextureParameteri(m_pyramid, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(m_pyramid, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(m_pyramid, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		GPU_debug_label(GL_TEXTURE, m_pyramid, "BGE-Remix depth pyramid");
		m_size[0] = w;
		m_size[1] = h;
	}

	GLuint depth = depth_texture;
	int origin[2] = {viewport[0], viewport[1]};
	if (!depth) {
		/* the window's depth, copied (not possible from a multisampled one) */
		GLint fb = 0, samples = 0;
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fb);
		glGetIntegerv(GL_SAMPLE_BUFFERS, &samples);
		if (samples > 0)
			return;
		if (!m_depthcopy || m_copysize[0] != w || m_copysize[1] != h) {
			if (m_depthcopy)
				glDeleteTextures(1, &m_depthcopy);
			glCreateTextures(GL_TEXTURE_2D, 1, &m_depthcopy);
			glTextureStorage2D(m_depthcopy, 1, GL_DEPTH_COMPONENT32F, w, h);
			glTextureParameteri(m_depthcopy, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(m_depthcopy, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			m_copysize[0] = w;
			m_copysize[1] = h;
		}
		glCopyTextureSubImage2D(m_depthcopy, 0, 0, 0, viewport[0], viewport[1], w, h);
		depth = m_depthcopy;
		multisample = false;
		origin[0] = origin[1] = 0;
	}

	GLint prev_program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);

	const GLuint first = multisample ? m_prog_firstms : m_prog_first;
	glUseProgram(first);
	glBindTextureUnit(0, depth);
	glUniform1i(glGetUniformLocation(first, "depth"), 0);
	glUniform4i(glGetUniformLocation(first, "viewport"), origin[0], origin[1], w, h);
	glBindImageTexture(0, m_pyramid, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
	glDispatchCompute((GLuint)((w + 7) / 8), (GLuint)((h + 7) / 8), 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glUseProgram(m_prog_down);
	int sw = w, sh = h;
	for (int level = 1; level < m_levels; level++) {
		const int dw = (sw > 1) ? sw / 2 : 1, dh = (sh > 1) ? sh / 2 : 1;
		glBindImageTexture(0, m_pyramid, level - 1, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		glBindImageTexture(1, m_pyramid, level, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
		glUniform2i(glGetUniformLocation(m_prog_down, "srcsize"), sw, sh);
		glUniform2i(glGetUniformLocation(m_prog_down, "dstsize"), dw, dh);
		glDispatchCompute((GLuint)((dw + 7) / 8), (GLuint)((dh + 7) / 8), 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		sw = dw;
		sh = dh;
	}
	glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
	glBindTextureUnit(0, 0);
	glUseProgram((GLuint)prev_program);

	memcpy(m_prevview, view, sizeof(m_prevview));
	memcpy(m_prevproj, proj, sizeof(m_prevproj));
	m_valid = true;
}

bool RAS_GPUCulling::Cull(int count, const float center[3], float radius, const float cur_view[16],
                          unsigned int command_buffer, unsigned int command_offset)
{
	if (!m_valid || !m_mainview || !Programs() || count <= 0)
		return false;

	/* this frame's view space to the last frame's */
	MT_Matrix4x4 cur, prev;
	cur.setValue(cur_view);
	prev.setValue(m_prevview);
	cur.invert();
	MT_Matrix4x4 toprev = prev * cur;
	float q[16];
	toprev.getValue(q);

	GLint prev_program = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
	glUseProgram(m_prog_cull);
	/* the whole buffer, the command found by its first word (a range at the
	 * command's offset would break the storage buffers' offset alignment) */
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, command_buffer);
	glUniform1i(glGetUniformLocation(m_prog_cull, "count"), count);
	glUniform1ui(glGetUniformLocation(m_prog_cull, "cmdbase"), (GLuint)(command_offset / sizeof(unsigned int)));
	glUniform4f(glGetUniformLocation(m_prog_cull, "sphere"), center[0], center[1], center[2], radius);
	glUniformMatrix4fv(glGetUniformLocation(m_prog_cull, "toprev"), 1, GL_FALSE, q);
	glUniformMatrix4fv(glGetUniformLocation(m_prog_cull, "prevproj"), 1, GL_FALSE, m_prevproj);
	glBindTextureUnit(0, m_pyramid);
	glUniform1i(glGetUniformLocation(m_prog_cull, "pyramid"), 0);
	glUniform2f(glGetUniformLocation(m_prog_cull, "pyrsize"), (float)m_size[0], (float)m_size[1]);
	glUniform1i(glGetUniformLocation(m_prog_cull, "levels"), m_levels);
	glDispatchCompute((GLuint)((count + 63) / 64), 1, 1);
	glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
	glBindTextureUnit(0, 0);
	glUseProgram((GLuint)prev_program);
	m_tested += count;
	return true;
}

void RAS_GPUCulling::FrameStats()
{
	m_tested = 0;
}
