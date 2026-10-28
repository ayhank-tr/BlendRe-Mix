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

/** \file bridge_read.cpp
 *  \ingroup remix_bridge
 *
 * Interface 6 (P5): reading Remix's picture into memory, for F12.
 *
 * Copy mode already has the frame in memory. In direct mode the picture is a
 * Vulkan image that only OpenGL (GL_NV_draw_vulkan_image) can reach, and
 * Blender renders F12 on a thread without OpenGL. The bridge makes a small
 * OpenGL context of its own for each read, on the calling thread: it waits
 * for Remix's "done" semaphore, draws the image into an offscreen buffer,
 * signals "resume" and reads the pixels back. The caller's OpenGL context (if
 * the thread has one) is made current again afterwards.
 */

#include <algorithm>
#include <cstring>
#include <vector>

#include "bridge_intern.h"

#include <GL/gl.h>

namespace brb {

typedef unsigned long long BRB_GLuint64;

#define BRB_GL_FRAMEBUFFER 0x8D40
#define BRB_GL_RENDERBUFFER 0x8D41
#define BRB_GL_COLOR_ATTACHMENT0 0x8CE0
#define BRB_GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define BRB_GL_RGBA8 0x8058
#define BRB_GL_CLAMP_TO_EDGE 0x812F

typedef void (APIENTRY *GenNames)(GLsizei n, GLuint *names);
typedef void (APIENTRY *DeleteNames)(GLsizei n, const GLuint *names);
typedef void (APIENTRY *BindName)(GLenum target, GLuint name);
typedef void (APIENTRY *FramebufferRenderbuffer)(GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb);
typedef void (APIENTRY *RenderbufferStorage)(GLenum target, GLenum format, GLsizei w, GLsizei h);
typedef GLenum (APIENTRY *CheckFramebufferStatus)(GLenum target);
typedef void (APIENTRY *SamplerParameteri)(GLuint sampler, GLenum pname, GLint param);
typedef void (APIENTRY *DrawVkImageNV)(BRB_GLuint64 image, GLuint sampler, GLfloat x0, GLfloat y0, GLfloat x1,
                                       GLfloat y1, GLfloat z, GLfloat s0, GLfloat t0, GLfloat s1, GLfloat t1);
typedef void (APIENTRY *VkSemaphoreNV)(BRB_GLuint64 semaphore);


struct ReadContext {
	HWND hwnd;
	HDC dc;
	HGLRC rc;
	HDC prev_dc;
	HGLRC prev_rc;
	ReadContext() : hwnd(nullptr), dc(nullptr), rc(nullptr), prev_dc(nullptr), prev_rc(nullptr) {}

	bool open()
	{
		static bool registered = false;
		static const wchar_t *CLASS = L"BGERemixBridgeRead";
		HINSTANCE inst = GetModuleHandleW(nullptr);
		if (!registered) {
			WNDCLASSEXW wc = {};
			wc.cbSize = sizeof(wc);
			wc.style = CS_OWNDC;
			wc.lpfnWndProc = DefWindowProcW;
			wc.hInstance = inst;
			wc.lpszClassName = CLASS;
			if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
				log("frame read: RegisterClassExW failed: %lu", GetLastError());
				return false;
			}
			registered = true;
		}
		hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, CLASS, L"BGE-Remix (read)", WS_POPUP, 0, 0, 16, 16,
		                       nullptr, nullptr, inst, nullptr);
		if (!hwnd) {
			log("frame read: CreateWindowExW failed: %lu", GetLastError());
			return false;
		}
		dc = GetDC(hwnd);
		PIXELFORMATDESCRIPTOR pfd = {};
		pfd.nSize = sizeof(pfd);
		pfd.nVersion = 1;
		pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pfd.iPixelType = PFD_TYPE_RGBA;
		pfd.cColorBits = 32;
		pfd.cAlphaBits = 8;
		pfd.iLayerType = PFD_MAIN_PLANE;
		const int pf = dc ? ChoosePixelFormat(dc, &pfd) : 0;
		if (!pf || !SetPixelFormat(dc, pf, &pfd)) {
			log("frame read: no pixel format (%lu)", GetLastError());
			return false;
		}
		rc = wglCreateContext(dc);
		if (!rc) {
			log("frame read: wglCreateContext failed: %lu", GetLastError());
			return false;
		}
		return begin();
	}

	/* make it current, remembering the thread's own context */
	bool begin()
	{
		prev_dc = wglGetCurrentDC();
		prev_rc = wglGetCurrentContext();
		if (!wglMakeCurrent(dc, rc)) {
			log("frame read: wglMakeCurrent failed: %lu", GetLastError());
			return false;
		}
		return true;
	}

	/* the thread's own context again */
	void end()
	{
		wglMakeCurrent(prev_dc, prev_rc);
		prev_dc = nullptr;
		prev_rc = nullptr;
	}

	~ReadContext()
	{
		if (rc) {
			if (wglGetCurrentContext() == rc) {
				wglMakeCurrent(prev_dc, prev_rc);
			}
			wglDeleteContext(rc);
		}
		if (dc) {
			ReleaseDC(hwnd, dc);
		}
		if (hwnd) {
			DestroyWindow(hwnd);
		}
	}
};

template<typename T> static bool proc(T &r_fn, const char *name)
{
	r_fn = (T)wglGetProcAddress(name);
	return r_fn != nullptr;
}

static bool has_extension(const char *name)
{
	const char *ext = (const char *)glGetString(GL_EXTENSIONS);
	if (!ext) {
		return false;
	}
	const size_t n = strlen(name);
	for (const char *p = strstr(ext, name); p; p = strstr(p + 1, name)) {
		if ((p == ext || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\0')) {
			return true;
		}
	}
	return false;
}

static void flip_rows(unsigned char *rgba, int width, int height)
{
	const size_t row = (size_t)width * 4;
	std::vector<unsigned char> tmp(row);
	for (int y = 0; y < height / 2; y++) {
		unsigned char *a = rgba + (size_t)y * row;
		unsigned char *b = rgba + (size_t)(height - 1 - y) * row;
		memcpy(tmp.data(), a, row);
		memcpy(a, b, row);
		memcpy(b, tmp.data(), row);
	}
}

/* The reading context of one thread, kept from one read to the next (F12
 * reads every frame) until brb_frame_read_end. */
struct Reader {
	ReadContext ctx;
	DWORD thread;
	DrawVkImageNV draw = nullptr;
	VkSemaphoreNV wait = nullptr, signal = nullptr;
	GenNames gen_fb = nullptr, gen_rb = nullptr, gen_samplers = nullptr;
	DeleteNames del_fb = nullptr, del_rb = nullptr, del_samplers = nullptr;
	BindName bind_fb = nullptr, bind_rb = nullptr;
	FramebufferRenderbuffer fb_rb = nullptr;
	RenderbufferStorage rb_storage = nullptr;
	CheckFramebufferStatus fb_status = nullptr;
	SamplerParameteri sampler_i = nullptr;
	GLuint fb = 0, rb = 0, sampler = 0;
	int fb_w = 0, fb_h = 0;
	Reader() : thread(GetCurrentThreadId()) {}

	bool load()
	{
		const bool ok = has_extension("GL_NV_draw_vulkan_image") && proc(draw, "glDrawVkImageNV") &&
		                proc(wait, "glWaitVkSemaphoreNV") && proc(signal, "glSignalVkSemaphoreNV") &&
		                proc(gen_fb, "glGenFramebuffers") && proc(gen_rb, "glGenRenderbuffers") &&
		                proc(del_fb, "glDeleteFramebuffers") && proc(del_rb, "glDeleteRenderbuffers") &&
		                proc(bind_fb, "glBindFramebuffer") && proc(bind_rb, "glBindRenderbuffer") &&
		                proc(fb_rb, "glFramebufferRenderbuffer") && proc(rb_storage, "glRenderbufferStorage") &&
		                proc(fb_status, "glCheckFramebufferStatus");
		if (!ok) {
			log("frame read: the bridge's OpenGL context lacks GL_NV_draw_vulkan_image or framebuffers");
			return false;
		}
		proc(gen_samplers, "glGenSamplers");
		proc(del_samplers, "glDeleteSamplers");
		proc(sampler_i, "glSamplerParameteri");
		if (gen_samplers && sampler_i) {
			gen_samplers(1, &sampler);
			sampler_i(sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			sampler_i(sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			sampler_i(sampler, GL_TEXTURE_WRAP_S, BRB_GL_CLAMP_TO_EDGE);
			sampler_i(sampler, GL_TEXTURE_WRAP_T, BRB_GL_CLAMP_TO_EDGE);
		}
		gen_fb(1, &fb);
		gen_rb(1, &rb);
		return true;
	}

	bool size(int w, int h)
	{
		if (w != fb_w || h != fb_h) {
			bind_rb(BRB_GL_RENDERBUFFER, rb);
			rb_storage(BRB_GL_RENDERBUFFER, BRB_GL_RGBA8, w, h);
			bind_fb(BRB_GL_FRAMEBUFFER, fb);
			fb_rb(BRB_GL_FRAMEBUFFER, BRB_GL_COLOR_ATTACHMENT0, BRB_GL_RENDERBUFFER, rb);
			fb_w = w;
			fb_h = h;
		}
		bind_fb(BRB_GL_FRAMEBUFFER, fb);
		return fb_status(BRB_GL_FRAMEBUFFER) == BRB_GL_FRAMEBUFFER_COMPLETE;
	}

	~Reader()
	{
		if (ctx.rc && ctx.begin()) {
			if (fb && del_fb) {
				bind_fb(BRB_GL_FRAMEBUFFER, 0);
				del_fb(1, &fb);
			}
			if (rb && del_rb) {
				del_rb(1, &rb);
			}
			if (sampler && del_samplers) {
				del_samplers(1, &sampler);
			}
			ctx.end();
		}
	}
};

static Reader *g_reader = nullptr;

static Reader *reader()
{
	if (g_reader && g_reader->thread != GetCurrentThreadId()) {
		/* left by another thread that did not end its reads: its window goes
		 * with that thread */
		log("frame read: a reading context of another thread is left behind");
		g_reader = nullptr;
	}
	if (!g_reader) {
		Reader *r = new Reader();
		if (!r->ctx.open()) {
			delete r;
			return nullptr;
		}
		const bool ok = r->load();
		r->ctx.end();
		if (!ok) {
			delete r;
			return nullptr;
		}
		g_reader = r;
	}
	return g_reader;
}

/* Direct mode: the frame waits for us (pending). On success it is taken.
 * rgba NULL and max_bytes 0: only take the frame (wait and signal). */
static int read_direct(unsigned char *rgba, int max_bytes, int *r_width, int *r_height)
{
	uint64_t image = 0, done = 0, resume = 0;
	int w = 0, h = 0;
	if (!device_has_vk()) {
		return 0;
	}
	Reader *rd = reader();
	if (!rd || !rd->ctx.begin()) {
		return 0;
	}
	const bool skip = !rgba && max_bytes == 0;

	/* From here on the frame is ours: "resume" is signalled whatever happens. */
	const int got = device_take_vk(&image, &done, &resume, &w, &h);
	if (r_width) {
		*r_width = w;
	}
	if (r_height) {
		*r_height = h;
	}
	const bool complete = !skip && got == 1 && rd->size(w, h);
	if (complete) {
		glViewport(0, 0, w, h);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	rd->wait((BRB_GLuint64)done);
	if (complete) {
		/* as the game draws it: t from 1 at the bottom row to 0 at the top */
		rd->draw((BRB_GLuint64)image, rd->sampler, 0.0f, 0.0f, (GLfloat)w, (GLfloat)h, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
	}
	rd->signal((BRB_GLuint64)resume);

	int result = 0;
	if (skip) {
		glFlush();
		result = 1;
	}
	else if (got != 1) {
		glFlush();
		log("frame read: Remix changed its picture size on this frame; nothing to read");
	}
	else if (!complete) {
		glFlush();
		log("frame read: the offscreen buffer %dx%d is not usable", w, h);
	}
	else if ((long long)w * h * 4 > (long long)max_bytes || !rgba) {
		glFlush();
		result = -1;
	}
	else {
		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		const GLenum err = glGetError();
		if (err == GL_NO_ERROR) {
			flip_rows(rgba, w, h);
			result = 1;
		}
		else {
			log("frame read: glReadPixels failed (0x%04x)", (unsigned)err);
		}
	}
	rd->ctx.end();
	return result;
}

static int read_copy(unsigned char *rgba, int max_bytes, int *r_width, int *r_height)
{
	if (!rgba && max_bytes == 0) {
		return 1; /* the frame was copied when it was presented */
	}
	int w = 0, h = 0, pitch = 0;
	const unsigned char *px = (const unsigned char *)device_pixels(&w, &h, &pitch);
	if (r_width) {
		*r_width = w;
	}
	if (r_height) {
		*r_height = h;
	}
	if (!px || w <= 0 || h <= 0) {
		return 0;
	}
	if ((long long)w * h * 4 > (long long)max_bytes || !rgba) {
		return -1;
	}
	for (int y = 0; y < h; y++) {
		const unsigned char *src = px + (size_t)y * pitch;
		unsigned char *dst = rgba + (size_t)y * w * 4;
		for (int x = 0; x < w; x++) {
			/* BGRA (X8R8G8B8 or A8R8G8B8) to RGBA, opaque */
			dst[x * 4 + 0] = src[x * 4 + 2];
			dst[x * 4 + 1] = src[x * 4 + 1];
			dst[x * 4 + 2] = src[x * 4 + 0];
			dst[x * 4 + 3] = 255;
		}
	}
	return 1;
}

}  /* namespace brb */

using namespace brb;

extern "C" {

BRB_API int brb_frame_read(unsigned char *r_rgba, int max_bytes, int *r_width, int *r_height)
{
	if (r_width) {
		*r_width = 0;
	}
	if (r_height) {
		*r_height = 0;
	}
	if (g_status != BRB_STATUS_RUNNING) {
		return 0;
	}
	int r = 0;
	try {
		if (g_mode == BRB_MODE_COPY) {
			r = read_copy(r_rgba, max_bytes, r_width, r_height);
		}
		else if (g_mode == BRB_MODE_DIRECT) {
			r = read_direct(r_rgba, max_bytes, r_width, r_height);
		}
	}
	catch (...) {
		log("frame read: unexpected exception");
		r = 0;
	}
	if (r == 1 && r_rgba) {
		g_stats.reads++;
	}
	return r;
}

BRB_API void brb_frame_read_end(void)
{
	if (g_reader && g_reader->thread == GetCurrentThreadId()) {
		delete g_reader;
		g_reader = nullptr;
	}
}

BRB_API int brb_gl_direct_supported(void)
{
	static int cached = -1;
	if (cached < 0) {
		ReadContext ctx;
		cached = 0;
		if (ctx.open()) {
			DrawVkImageNV draw = nullptr;
			VkSemaphoreNV wait = nullptr, signal = nullptr;
			cached = (has_extension("GL_NV_draw_vulkan_image") && proc(draw, "glDrawVkImageNV") &&
			          proc(wait, "glWaitVkSemaphoreNV") && proc(signal, "glSignalVkSemaphoreNV")) ? 1 : 0;
			ctx.end();
		}
		log("OpenGL with GL_NV_draw_vulkan_image: %s", cached ? "yes" : "no");
	}
	return cached;
}

}  /* extern "C" */
