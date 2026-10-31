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

/** \file GPU_glcheck.h
 *  \ingroup gpu
 *
 * BGE-Remix P8.1: what the graphics card offers (OpenGL 4.5 and the two
 * tiers), driver messages and names for debuggers, GPU timers, driver
 * resets, and the check that a drawer leaves OpenGL as it found it.
 */

#ifndef __GPU_GLCHECK_H__
#define __GPU_GLCHECK_H__

#include "BLI_sys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the card ---- */

typedef struct GPUCaps {
	int major, minor;        /* the context's version (or the faked one) */
	bool core_profile;       /* a core context (false: compatibility) */
	bool debug;              /* a debug context: driver messages on */
	int tier;                /* 1: OpenGL 4.5; 2: plus extras */
	/* tier 2 extras, each used only when present (never required) */
	bool bindless, sparse, vk_interop, aniso;
	bool vs_layer;           /* the vertex stage picks the layer (P8.16: shadows in one pass) */
	bool robustness;         /* driver resets can be reported */
	char text[512];          /* one line for logs */
} GPUCaps;

const GPUCaps *GPU_caps(void);

/* called by GPU_init: fills the caps; below OpenGL 4.5 it says so (a message
 * box unless BGE_REMIX_NO_DIALOGS=1) and quits. BGE_REMIX_GL_FAKE_VERSION=3.3
 * stands in for an older card; BGE_REMIX_GL_TIER=1 turns the extras off. */
void GPU_caps_init(void);

/* ---- driver messages, names ---- */

/* errors the driver reported since start (BGE_REMIX_GL_DEBUG=1) */
int GPU_glcheck_error_count(void);
/* a name for an OpenGL object (GL_BUFFER, GL_PROGRAM, GL_TEXTURE...) */
void GPU_debug_label(unsigned int identifier, unsigned int name, const char *label);
/* a named pass in GPU debuggers */
void GPU_debug_group_begin(const char *name);
void GPU_debug_group_end(void);

/* ---- timers ---- */

typedef struct GPUTimer GPUTimer;
GPUTimer *GPU_timer_create(const char *name);
void GPU_timer_free(GPUTimer *timer);
void GPU_timer_begin(GPUTimer *timer);
void GPU_timer_end(GPUTimer *timer);
/* the latest finished measurement in milliseconds (-1 until one is ready) */
double GPU_timer_ms(GPUTimer *timer);
const char *GPU_timer_name(const GPUTimer *timer);

/* ---- driver resets ---- */

/* true once, the first time a reset is seen (real, or simulated after N
 * checks with BGE_REMIX_GL_SIMULATE_RESET=N); the reset is reported */
bool GPU_reset_check(void);
bool GPU_reset_happened(void);
/* a message box with the text (unless BGE_REMIX_NO_DIALOGS=1), and the console */
void GPU_reset_dialog(const char *message);

/* ---- the state check (BGE_REMIX_GL_CHECK=1) ---- */

enum {
	GPU_STATE_CHECK_MATRICES = (1 << 0),  /* also the matrices */
	GPU_STATE_CHECK_VIEWPORT = (1 << 1),  /* also the viewport and scissor box */
};

typedef struct GPUStateSnapshot {
	int ints[40];
	float floats[32];
} GPUStateSnapshot;

bool GPU_state_check_enabled(void);
void GPU_state_snapshot(GPUStateSnapshot *snap);
/* prints "BGE-Remix GL check: <who> left changed: <what>" and returns the
 * number of differences; the totals are printed at exit */
int GPU_state_compare(const GPUStateSnapshot *before, const GPUStateSnapshot *after, const char *who, int flags);

#ifdef __cplusplus
}
#endif

#endif  /* __GPU_GLCHECK_H__ */
