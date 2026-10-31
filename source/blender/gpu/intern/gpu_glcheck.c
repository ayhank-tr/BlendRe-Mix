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

/** \file blender/gpu/intern/gpu_glcheck.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.1: the card's capabilities and the OpenGL 4.5 check, driver
 * messages, debugger names, GPU timers, driver resets and the state check.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include "MEM_guardedalloc.h"

#include "BLI_utildefines.h"
#include "BLI_string.h"

#include "GPU_glew.h"
#include "GPU_glcheck.h"

#include "gpu_private.h"

static GPUCaps gpu_caps = {0};

static bool env_is(const char *name, const char *value)
{
	const char *v = getenv(name);
	return v && STREQ(v, value);
}

const GPUCaps *GPU_caps(void)
{
	return &gpu_caps;
}

static void gpu_caps_fail(const char *renderer)
{
	char msg[1024];
	BLI_snprintf(msg, sizeof(msg),
	             "BGE-Remix needs OpenGL 4.5.\n\n"
	             "This graphics card or driver offers OpenGL %d.%d (%s).\n\n"
	             "Update the graphics driver, or use a card from 2012 or later "
	             "(NVIDIA GTX 400 series, AMD Radeon HD 7000, Intel 6th-generation Core or newer).",
	             gpu_caps.major, gpu_caps.minor, renderer ? renderer : "unknown");
	fprintf(stderr, "BGE-Remix GL: %s\n", msg);
	fflush(stderr);
#ifdef _WIN32
	if (!env_is("BGE_REMIX_NO_DIALOGS", "1")) {
		MessageBoxA(NULL, msg, "BGE-Remix", MB_OK | MB_ICONERROR);
	}
#endif
	exit(3);
}

/* ---- driver messages ---- */

static struct {
	bool on;
	int errors, messages, suppressed;
	unsigned int seen_ids[64];
	int seen_counts[64];
} gpu_msgs = {false};

static const char *msg_source(GLenum s)
{
	switch (s) {
		case GL_DEBUG_SOURCE_API: return "api";
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "window system";
		case GL_DEBUG_SOURCE_SHADER_COMPILER: return "shader compiler";
		case GL_DEBUG_SOURCE_THIRD_PARTY: return "third party";
		case GL_DEBUG_SOURCE_APPLICATION: return "application";
		default: return "other";
	}
}

static const char *msg_type(GLenum t)
{
	switch (t) {
		case GL_DEBUG_TYPE_ERROR: return "error";
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "undefined behavior";
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "deprecated";
		case GL_DEBUG_TYPE_PORTABILITY: return "portability";
		case GL_DEBUG_TYPE_PERFORMANCE: return "performance";
		default: return "other";
	}
}

static void GLAPIENTRY gpu_glcheck_proc(
        GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei UNUSED(length),
        const GLchar *message, const GLvoid *UNUSED(user))
{
	const bool error = (type == GL_DEBUG_TYPE_ERROR || type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR);
	if (!error && (type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR || type == GL_DEBUG_TYPE_PUSH_GROUP ||
	               type == GL_DEBUG_TYPE_POP_GROUP || type == GL_DEBUG_TYPE_MARKER ||
	               severity == GL_DEBUG_SEVERITY_NOTIFICATION || severity == GL_DEBUG_SEVERITY_LOW))
	{
		return;
	}
	if (error) {
		gpu_msgs.errors++;
	}
	else {
		gpu_msgs.messages++;
	}
	/* the same message at most 5 times */
	int slot = -1;
	for (int i = 0; i < 64; i++) {
		if (gpu_msgs.seen_counts[i] == 0 || gpu_msgs.seen_ids[i] == id) {
			slot = i;
			break;
		}
	}
	if (slot >= 0) {
		gpu_msgs.seen_ids[slot] = id;
		if (++gpu_msgs.seen_counts[slot] > 5) {
			gpu_msgs.suppressed++;
			return;
		}
	}
	fprintf(stderr, "BGE-Remix GL %s: %s %s %u: %s\n", error ? "error" : "message",
	        msg_source(source), msg_type(type), id, message);
}

int GPU_glcheck_error_count(void)
{
	return gpu_msgs.errors;
}

void GPU_debug_label(unsigned int identifier, unsigned int name, const char *label)
{
	if (gpu_caps.debug && name && label && (GLEW_VERSION_4_3 || GLEW_KHR_debug)) {
		glObjectLabel(identifier, name, -1, label);
	}
}

void GPU_debug_group_begin(const char *name)
{
	if (gpu_caps.debug && (GLEW_VERSION_4_3 || GLEW_KHR_debug)) {
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
	}
}

void GPU_debug_group_end(void)
{
	if (gpu_caps.debug && (GLEW_VERSION_4_3 || GLEW_KHR_debug)) {
		glPopDebugGroup();
	}
}

/* ---- the card ---- */

static bool has_ext(const char *name)
{
	GLint n = 0;
	glGetIntegerv(GL_NUM_EXTENSIONS, &n);
	for (GLint i = 0; i < n; i++) {
		const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
		if (e && STREQ(e, name)) {
			return true;
		}
	}
	return false;
}

void GPU_caps_init(void)
{
	const char *version = (const char *)glGetString(GL_VERSION);
	const char *renderer = (const char *)glGetString(GL_RENDERER);
	const char *vendor = (const char *)glGetString(GL_VENDOR);

	memset(&gpu_caps, 0, sizeof(gpu_caps));
	glGetIntegerv(GL_MAJOR_VERSION, &gpu_caps.major);
	glGetIntegerv(GL_MINOR_VERSION, &gpu_caps.minor);
	if (gpu_caps.major <= 0 && version) {
		sscanf(version, "%d.%d", &gpu_caps.major, &gpu_caps.minor);
	}
	const char *fake = getenv("BGE_REMIX_GL_FAKE_VERSION");
	if (fake && fake[0]) {
		sscanf(fake, "%d.%d", &gpu_caps.major, &gpu_caps.minor);
	}
	if (gpu_caps.major < 4 || (gpu_caps.major == 4 && gpu_caps.minor < 5)) {
		gpu_caps_fail(renderer);
		return;
	}

	GLint mask = 0, flags = 0;
	glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
	glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
	gpu_caps.core_profile = (mask & GL_CONTEXT_CORE_PROFILE_BIT) != 0;
	gpu_caps.debug = (flags & GL_CONTEXT_FLAG_DEBUG_BIT) != 0;
	GLint strategy = 0;
	glGetIntegerv(GL_RESET_NOTIFICATION_STRATEGY, &strategy);
	gpu_caps.robustness = (strategy == GL_LOSE_CONTEXT_ON_RESET);

	const bool tier1 = env_is("BGE_REMIX_GL_TIER", "1");
	if (!tier1) {
		gpu_caps.bindless = has_ext("GL_ARB_bindless_texture");
		gpu_caps.sparse = has_ext("GL_ARB_sparse_texture");
		gpu_caps.vk_interop = has_ext("GL_EXT_memory_object") && has_ext("GL_EXT_semaphore");
		gpu_caps.aniso = has_ext("GL_ARB_texture_filter_anisotropic") ||
		                 has_ext("GL_EXT_texture_filter_anisotropic");
		gpu_caps.vs_layer = has_ext("GL_ARB_shader_viewport_layer_array");
	}
	gpu_caps.tier = (gpu_caps.bindless || gpu_caps.sparse || gpu_caps.vk_interop || gpu_caps.aniso ||
	                 gpu_caps.vs_layer) ? 2 : 1;

	BLI_snprintf(gpu_caps.text, sizeof(gpu_caps.text),
	             "%d.%d %s%s, tier %d%s%s%s%s%s%s%s, %s / %s / %s",
	             gpu_caps.major, gpu_caps.minor, gpu_caps.core_profile ? "core" : "compatibility",
	             gpu_caps.debug ? " (debug)" : "", gpu_caps.tier,
	             tier1 ? " (forced)" : "",
	             gpu_caps.bindless ? " bindless" : "", gpu_caps.sparse ? " sparse" : "",
	             gpu_caps.vk_interop ? " vulkan-interop" : "", gpu_caps.aniso ? " anisotropic" : "",
	             gpu_caps.vs_layer ? " vertex-layer" : "",
	             gpu_caps.robustness ? ", resets reported" : "",
	             vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?");
	printf("BGE-Remix GL: %s\n", gpu_caps.text);

	/* driver messages, in debug contexts */
	if (gpu_caps.debug && (GLEW_VERSION_4_3 || GLEW_KHR_debug)) {
		gpu_msgs.on = true;
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback((GLDEBUGPROC)gpu_glcheck_proc, NULL);
		glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
	}
}

/* ---- timers ---- */

#define TIMER_QUERIES 3

struct GPUTimer {
	char name[64];
	GLuint queries[TIMER_QUERIES][2];
	bool pending[TIMER_QUERIES];
	int next;
	double last_ms;
};

GPUTimer *GPU_timer_create(const char *name)
{
	GPUTimer *t = MEM_callocN(sizeof(GPUTimer), "GPUTimer");
	BLI_strncpy(t->name, name ? name : "", sizeof(t->name));
	glGenQueries(TIMER_QUERIES * 2, &t->queries[0][0]);
	t->last_ms = -1.0;
	return t;
}

void GPU_timer_free(GPUTimer *timer)
{
	if (timer) {
		glDeleteQueries(TIMER_QUERIES * 2, &timer->queries[0][0]);
		MEM_freeN(timer);
	}
}

static void timer_collect(GPUTimer *t)
{
	for (int i = 0; i < TIMER_QUERIES; i++) {
		if (!t->pending[i]) {
			continue;
		}
		GLint ready = 0;
		glGetQueryObjectiv(t->queries[i][1], GL_QUERY_RESULT_AVAILABLE, &ready);
		if (ready) {
			GLuint64 a = 0, b = 0;
			glGetQueryObjectui64v(t->queries[i][0], GL_QUERY_RESULT, &a);
			glGetQueryObjectui64v(t->queries[i][1], GL_QUERY_RESULT, &b);
			t->last_ms = (double)(b - a) / 1.0e6;
			t->pending[i] = false;
		}
	}
}

void GPU_timer_begin(GPUTimer *timer)
{
	timer_collect(timer);
	if (timer->pending[timer->next]) {
		/* still in flight: wait for it rather than lose it */
		GLuint64 dummy;
		glGetQueryObjectui64v(timer->queries[timer->next][1], GL_QUERY_RESULT, &dummy);
		timer_collect(timer);
		timer->pending[timer->next] = false;
	}
	glQueryCounter(timer->queries[timer->next][0], GL_TIMESTAMP);
}

void GPU_timer_end(GPUTimer *timer)
{
	glQueryCounter(timer->queries[timer->next][1], GL_TIMESTAMP);
	timer->pending[timer->next] = true;
	timer->next = (timer->next + 1) % TIMER_QUERIES;
}

double GPU_timer_ms(GPUTimer *timer)
{
	timer_collect(timer);
	return timer->last_ms;
}

const char *GPU_timer_name(const GPUTimer *timer)
{
	return timer->name;
}

/* ---- driver resets ---- */

static struct {
	bool happened;
	int checks, simulate_at;
	bool init;
} gpu_reset = {false};

bool GPU_reset_check(void)
{
	if (gpu_reset.happened) {
		return false;
	}
	if (!gpu_reset.init) {
		const char *sim = getenv("BGE_REMIX_GL_SIMULATE_RESET");
		gpu_reset.simulate_at = (sim && sim[0]) ? atoi(sim) : 0;
		gpu_reset.init = true;
	}
	gpu_reset.checks++;
	GLenum status = GL_NO_ERROR;
	if (gpu_caps.robustness && GLEW_VERSION_4_5) {
		status = glGetGraphicsResetStatus();
	}
	const bool simulated = gpu_reset.simulate_at > 0 && gpu_reset.checks >= gpu_reset.simulate_at;
	if (status == GL_NO_ERROR && !simulated) {
		return false;
	}
	gpu_reset.happened = true;
	fprintf(stderr, "BGE-Remix GL: the graphics driver was reset%s (status 0x%x)\n",
	        simulated ? " (simulated)" : "", (unsigned int)status);
	fflush(stderr);
	return true;
}

bool GPU_reset_happened(void)
{
	return gpu_reset.happened;
}

void GPU_reset_dialog(const char *message)
{
	fprintf(stderr, "BGE-Remix GL: %s\n", message);
	fflush(stderr);
#ifdef _WIN32
	if (!env_is("BGE_REMIX_NO_DIALOGS", "1")) {
		MessageBoxA(NULL, message, "BGE-Remix", MB_OK | MB_ICONWARNING);
	}
#endif
}

/* ---- the state check ---- */

static void str_append(char *dst, const char *src, size_t maxlen)
{
	const size_t len = strlen(dst);
	if (len + 1 < maxlen) {
		BLI_strncpy(dst + len, src, maxlen - len);
	}
}

static struct {
	bool init, on;
	int compares, differences;
} gpu_sc = {false};

bool GPU_state_check_enabled(void)
{
	if (!gpu_sc.init) {
		gpu_sc.on = env_is("BGE_REMIX_GL_CHECK", "1");
		gpu_sc.init = true;
	}
	return gpu_sc.on;
}

enum {
	SC_FRAMEBUFFER, SC_READ_FRAMEBUFFER, SC_PROGRAM, SC_ACTIVE_TEXTURE, SC_TEXTURE_2D, SC_VERTEX_ARRAY,
	SC_ARRAY_BUFFER, SC_MATRIX_MODE, SC_BLEND, SC_BLEND_SRC, SC_BLEND_DST, SC_DEPTH_TEST, SC_DEPTH_MASK,
	SC_DEPTH_FUNC, SC_CULL_FACE, SC_SCISSOR_TEST, SC_STENCIL_TEST, SC_COLOR_MASK, SC_LIGHTING,
	SC_TEXTURE_2D_ON, SC_POLYGON_MODE, SC_UNPACK_ALIGNMENT, SC_PACK_ALIGNMENT, SC_POLYGON_OFFSET,
	SC_DRAW_BUFFER, SC_VIEWPORT, /* 4 */
	SC_SCISSOR_BOX = SC_VIEWPORT + 4, /* 4 */
	SC_COUNT = SC_SCISSOR_BOX + 4,
};

static const char *sc_names[SC_COUNT] = {
	"framebuffer", "read framebuffer", "program", "active texture", "texture 2D", "vertex array",
	"array buffer", "matrix mode", "blend", "blend source", "blend destination", "depth test", "depth mask",
	"depth function", "cull face", "scissor test", "stencil test", "colour mask", "lighting",
	"texture 2D enabled", "polygon mode", "unpack alignment", "pack alignment", "polygon offset",
	"draw buffer", "viewport x", "viewport y", "viewport w", "viewport h",
	"scissor x", "scissor y", "scissor w", "scissor h",
};

void GPU_state_snapshot(GPUStateSnapshot *snap)
{
	int *v = snap->ints;
	memset(snap, 0, sizeof(*snap));
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &v[SC_FRAMEBUFFER]);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &v[SC_READ_FRAMEBUFFER]);
	glGetIntegerv(GL_CURRENT_PROGRAM, &v[SC_PROGRAM]);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &v[SC_ACTIVE_TEXTURE]);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &v[SC_TEXTURE_2D]);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &v[SC_VERTEX_ARRAY]);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &v[SC_ARRAY_BUFFER]);
	if (!gpu_caps.core_profile) {
		glGetIntegerv(GL_MATRIX_MODE, &v[SC_MATRIX_MODE]);
		v[SC_LIGHTING] = glIsEnabled(GL_LIGHTING);
		v[SC_TEXTURE_2D_ON] = glIsEnabled(GL_TEXTURE_2D);
	}
	v[SC_BLEND] = glIsEnabled(GL_BLEND);
	glGetIntegerv(GL_BLEND_SRC_RGB, &v[SC_BLEND_SRC]);
	glGetIntegerv(GL_BLEND_DST_RGB, &v[SC_BLEND_DST]);
	v[SC_DEPTH_TEST] = glIsEnabled(GL_DEPTH_TEST);
	GLboolean dm = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &dm);
	v[SC_DEPTH_MASK] = dm;
	glGetIntegerv(GL_DEPTH_FUNC, &v[SC_DEPTH_FUNC]);
	v[SC_CULL_FACE] = glIsEnabled(GL_CULL_FACE);
	v[SC_SCISSOR_TEST] = glIsEnabled(GL_SCISSOR_TEST);
	v[SC_STENCIL_TEST] = glIsEnabled(GL_STENCIL_TEST);
	GLboolean cm[4] = {1, 1, 1, 1};
	glGetBooleanv(GL_COLOR_WRITEMASK, cm);
	v[SC_COLOR_MASK] = cm[0] | (cm[1] << 1) | (cm[2] << 2) | (cm[3] << 3);
	GLint pm[2] = {0, 0};
	glGetIntegerv(GL_POLYGON_MODE, pm);
	v[SC_POLYGON_MODE] = pm[0];
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &v[SC_UNPACK_ALIGNMENT]);
	glGetIntegerv(GL_PACK_ALIGNMENT, &v[SC_PACK_ALIGNMENT]);
	v[SC_POLYGON_OFFSET] = glIsEnabled(GL_POLYGON_OFFSET_FILL);
	glGetIntegerv(GL_DRAW_BUFFER, &v[SC_DRAW_BUFFER]);
	glGetIntegerv(GL_VIEWPORT, &v[SC_VIEWPORT]);
	glGetIntegerv(GL_SCISSOR_BOX, &v[SC_SCISSOR_BOX]);
	if (!gpu_caps.core_profile) {
		glGetFloatv(GL_MODELVIEW_MATRIX, &snap->floats[0]);
		glGetFloatv(GL_PROJECTION_MATRIX, &snap->floats[16]);
	}
}

int GPU_state_compare(const GPUStateSnapshot *before, const GPUStateSnapshot *after, const char *who, int flags)
{
	int n = 0;
	char what[512] = "";
	for (int i = 0; i < SC_COUNT; i++) {
		if (!(flags & GPU_STATE_CHECK_VIEWPORT) && i >= SC_VIEWPORT) {
			break;
		}
		if (before->ints[i] != after->ints[i]) {
			if (n < 8) {
				char item[96];
				BLI_snprintf(item, sizeof(item), "%s%s (%d -> %d)", n ? ", " : "", sc_names[i],
				             before->ints[i], after->ints[i]);
				str_append(what, item, sizeof(what));
			}
			n++;
		}
	}
	if ((flags & GPU_STATE_CHECK_MATRICES) && memcmp(before->floats, after->floats, sizeof(before->floats)) != 0) {
		str_append(what, n ? ", a matrix" : "a matrix", sizeof(what));
		n++;
	}
	gpu_sc.compares++;
	if (n) {
		/* the first 50 are printed; all are counted */
		if (gpu_sc.differences++ < 50) {
			printf("BGE-Remix GL check: %s left changed: %s\n", who, what);
		}
	}
	return n;
}

/* ---- start and exit ---- */

void gpu_glcheck_init(void)
{
	GPU_caps_init();
}

void gpu_glcheck_exit(void)
{
	if (gpu_msgs.on) {
		printf("BGE-Remix GL: %d errors, %d other messages from the driver (%d repeats not shown)\n",
		       gpu_msgs.errors, gpu_msgs.messages, gpu_msgs.suppressed);
	}
	if (gpu_sc.on) {
		printf("BGE-Remix GL check: %d checks, %d with changes left\n", gpu_sc.compares, gpu_sc.differences);
	}
}
