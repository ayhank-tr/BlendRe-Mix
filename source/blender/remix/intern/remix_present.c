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

/** \file remix_present.c
 *  \ingroup remix
 *
 * Remix's picture on screen (BRMX_present.h). Moved here from the game
 * engine's KX_RemixRenderer (P4.8) so that the 3D view draws it the same way.
 */

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GPU_glew.h"

#include "BRMX_loader.h"
#include "BRMX_present.h"

typedef void (APIENTRY *BRMX_DrawVkImageNV)(GLuint64 vkImage, GLuint sampler, GLfloat x0, GLfloat y0, GLfloat x1,
                                           GLfloat y1, GLfloat z, GLfloat s0, GLfloat t0, GLfloat s1, GLfloat t1);
typedef void (APIENTRY *BRMX_VkSemaphoreNV)(GLuint64 vkSemaphore);

static struct {
	int checked;              /* the functions were looked for */
	int fake;
	BRMX_DrawVkImageNV draw;
	BRMX_VkSemaphoreNV wait;
	BRMX_VkSemaphoreNV signal;
	const char *reason;
	GLuint sampler;
	GLuint texture;
	int tex_w, tex_h;
	int direct_frames;
	int error_noted;
} present = {0};

/* CI has no NVIDIA driver: with BGE_REMIX_FAKE_VK_DRAW=1 these stand in for the
 * extension and print what would be done, so the tests can check that every
 * presented frame is waited for, drawn and resumed exactly once, in that order. */
static void APIENTRY fake_draw_vk(GLuint64 image, GLuint sampler, GLfloat x0, GLfloat y0, GLfloat x1, GLfloat y1,
                                  GLfloat z, GLfloat s0, GLfloat t0, GLfloat s1, GLfloat t1)
{
	(void)sampler;
	(void)z;
	(void)s0;
	(void)s1;
	printf("BGE-Remix direct: draw %llx at %g,%g..%g,%g t %g..%g\n", (unsigned long long)image, x0, y0, x1, y1, t0,
	       t1);
}

static void APIENTRY fake_wait_vk(GLuint64 semaphore)
{
	printf("BGE-Remix direct: wait %llx\n", (unsigned long long)semaphore);
}

static void APIENTRY fake_signal_vk(GLuint64 semaphore)
{
	printf("BGE-Remix direct: signal %llx\n", (unsigned long long)semaphore);
	fflush(stdout);
}

static int env_is(const char *name, const char *value)
{
	char buf[16];
	const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
	return n > 0 && n < sizeof(buf) && _stricmp(buf, value) == 0;
}

static int has_gl_extension(const char *name)
{
	const char *ext = (const char *)glGetString(GL_EXTENSIONS);
	const size_t n = strlen(name);
	const char *p;
	if (!ext) {
		return 0;
	}
	for (p = strstr(ext, name); p; p = strstr(p + 1, name)) {
		if ((p == ext || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) {
			return 1;
		}
	}
	return 0;
}

int BRMX_present_direct_available(const char **r_reason)
{
	if (env_is("BGE_REMIX_PRESENT", "copy")) {
		present.reason = "BGE_REMIX_PRESENT=copy";
		if (r_reason) {
			*r_reason = present.reason;
		}
		return 0;
	}
	if (!present.checked) {
		present.checked = 1;
		if (env_is("BGE_REMIX_FAKE_VK_DRAW", "1")) {
			present.fake = 1;
			present.draw = fake_draw_vk;
			present.wait = fake_wait_vk;
			present.signal = fake_signal_vk;
		}
		else if (!has_gl_extension("GL_NV_draw_vulkan_image")) {
			present.reason = "the graphics driver has no GL_NV_draw_vulkan_image";
		}
		else {
			present.draw = (BRMX_DrawVkImageNV)wglGetProcAddress("glDrawVkImageNV");
			present.wait = (BRMX_VkSemaphoreNV)wglGetProcAddress("glWaitVkSemaphoreNV");
			present.signal = (BRMX_VkSemaphoreNV)wglGetProcAddress("glSignalVkSemaphoreNV");
			if (!present.draw || !present.wait || !present.signal) {
				present.draw = NULL;
				present.wait = present.signal = NULL;
				present.reason = "GL_NV_draw_vulkan_image is listed but its functions are missing";
			}
		}
	}
	if (r_reason) {
		*r_reason = present.reason ? present.reason : "";
	}
	return present.draw != NULL;
}

int BRMX_present_fake(void)
{
	return present.fake;
}

int BRMX_present_direct_frames(void)
{
	return present.direct_frames;
}

/* GL_NV_draw_vulkan_image */
static int draw_direct(const BRMX_Bridge *bridge, int x, int y, int width, int height)
{
	GLuint64 image = 0, done = 0, resume = 0;
	int w = 0, h = 0, got;
	GLint program = 0;
	GLenum err;

	got = bridge->frame_vk((uint64_t *)&image, (uint64_t *)&done, (uint64_t *)&resume, &w, &h);
	if (!got) {
		return 0;
	}
	if (!BRMX_present_direct_available(NULL)) {
		/* never here (direct mode is chosen only with the extension) */
		return 0;
	}
	/* Remix has signalled "done" and holds its next frame until "resume": from
	 * here on, resume is signalled whatever happens. */
	if (present.fake) {
		present.wait(done);
		if (got == 1) {
			present.draw(image, 0, (GLfloat)x, (GLfloat)y, (GLfloat)(x + width), (GLfloat)(y + height), 0.0f, 0.0f,
			             1.0f, 1.0f, 0.0f);
			present.direct_frames++;
		}
		present.signal(resume);
		return got;
	}
	if (GLEW_VERSION_2_0) {
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
		glUseProgram(0);
	}
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_FOG);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	if (!present.sampler && GLEW_ARB_sampler_objects) {
		glGenSamplers(1, &present.sampler);
		glSamplerParameteri(present.sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glSamplerParameteri(present.sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glSamplerParameteri(present.sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glSamplerParameteri(present.sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

	present.wait(done);
	if (got == 1) {
		/* window coordinates; the picture's rows go from the top, OpenGL's from
		 * the bottom, so t runs from 1 at the bottom edge to 0 at the top */
		present.draw(image, present.sampler, (GLfloat)x, (GLfloat)y, (GLfloat)(x + width), (GLfloat)(y + height),
		             0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
		present.direct_frames++;
	}
	present.signal(resume);

	err = glGetError();
	if (err != GL_NO_ERROR && !present.error_noted) {
		present.error_noted = 1;
		printf("BGE-Remix: drawing Remix's image failed (OpenGL error 0x%04x); Remix keeps running.\n", err);
		fflush(stdout);
	}
	glPopAttrib();
	if (GLEW_VERSION_2_0) {
		glUseProgram((GLuint)program);
	}
	return got;
}

/* the copied frame, as a texture over the rectangle */
static int draw_copy(const BRMX_Bridge *bridge, int x, int y, int width, int height)
{
	int w = 0, h = 0, pitch = 0;
	const void *pixels = bridge->frame_pixels(&w, &h, &pitch);
	GLint program = 0, texture = 0, active = 0;

	if (!pixels || w <= 0 || h <= 0) {
		return 0;
	}
	/* the shader program and the texture binding are not on the attribute stack */
	if (GLEW_VERSION_2_0) {
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	}
	glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

	if (!present.texture) {
		glGenTextures(1, &present.texture);
	}
	glBindTexture(GL_TEXTURE_2D, present.texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	if (w != present.tex_w || h != present.tex_h) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
		present.tex_w = w;
		present.tex_h = h;
	}
	else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
	}

	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	if (GLEW_VERSION_2_0) {
		glUseProgram(0);
	}
	glViewport(x, y, width, height);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	glDisable(GL_STENCIL_TEST);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	/* the picture's rows go from the top; OpenGL's from the bottom */
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);
	glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
	glEnd();

	glMatrixMode(GL_TEXTURE);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glPopClientAttrib();
	glPopAttrib();
	glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
	glActiveTexture((GLenum)active);
	if (GLEW_VERSION_2_0) {
		glUseProgram((GLuint)program);
	}
	return 1;
}

int BRMX_present_draw(int x, int y, int width, int height)
{
	const BRMX_Bridge *bridge = BRMX_bridge();
	int mode;
	if (!bridge || width <= 0 || height <= 0) {
		return 0;
	}
	mode = bridge->get_mode();
	if (mode == BRB_MODE_DIRECT) {
		return draw_direct(bridge, x, y, width, height);
	}
	if (mode == BRB_MODE_COPY) {
		return draw_copy(bridge, x, y, width, height);
	}
	return 0;
}

void BRMX_present_free(void)
{
	if (present.texture) {
		glDeleteTextures(1, &present.texture);
		present.texture = 0;
		present.tex_w = present.tex_h = 0;
	}
	if (present.sampler && GLEW_ARB_sampler_objects) {
		glDeleteSamplers(1, &present.sampler);
	}
	present.sampler = 0;
}
