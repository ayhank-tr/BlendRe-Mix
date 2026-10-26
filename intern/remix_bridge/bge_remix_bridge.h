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

/** \file bge_remix_bridge.h
 *  \ingroup remix_bridge
 *
 * BGE-Remix: the plain C interface of bge_remix_bridge.dll.
 *
 * The bridge is the only code that talks to the RTX Remix runtime. Blender
 * loads the DLL on first use (LoadLibrary + GetProcAddress of the functions
 * below), so nothing here is linked into blender.exe.
 *
 * Strings are UTF-8, paths are UTF-16 (wchar_t). All calls must come from the
 * thread that created the Remix window (Blender's main thread).
 */

#ifndef __BGE_REMIX_BRIDGE_H__
#define __BGE_REMIX_BRIDGE_H__

#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BGE_REMIX_BRIDGE_EXPORTS
#  define BRB_API __declspec(dllexport)
#else
#  define BRB_API __declspec(dllimport)
#endif

/* Bumped whenever a function below changes; Blender refuses a bridge DLL
 * with another value instead of calling into it.
 * 1: P1 (load, start, triangle test). 2: P2 (scene interface, copy and window modes). */
#define BRB_ABI_VERSION 2

typedef enum brb_status {
	BRB_STATUS_NOT_LOADED = 0,           /* nothing tried yet */
	BRB_STATUS_NOT_INSTALLED = 1,        /* no runtime in the given folder */
	BRB_STATUS_INCOMPATIBLE_VERSION = 2, /* runtime built for another API minor version */
	BRB_STATUS_LOAD_FAILED = 3,          /* the runtime DLL or one it needs did not load */
	BRB_STATUS_STARTUP_FAILED = 4,       /* loaded, but Remix refused to start (GPU, driver, Vulkan) */
	BRB_STATUS_READY = 5,                /* loaded, not started */
	BRB_STATUS_RUNNING = 6,              /* started, rendering into its window */
} brb_status;

typedef struct brb_triangle_result {
	int frames;               /* frames presented */
	double seconds;           /* wall time of the loop */
	double frame_ms_mean;
	double frame_ms_median;
	double frame_ms_p95;
	int present_errors;       /* Present calls that did not return success */
	int screenshot_saved;     /* 1 when the screenshot file was written */
	int capture_requested;    /* 1 when the capture hotkey was sent */
	int last_error;           /* last remixapi_ErrorCode seen, 0 if none */
} brb_triangle_result;

/* Always BRB_ABI_VERSION of the DLL. */
BRB_API int brb_abi_version(void);

/* Remix API version the bridge was compiled against, e.g. "0.6.5". */
BRB_API const char *brb_api_version(void);

/* Load the runtime from a folder containing Remix's d3d9.dll.
 * Returns READY or the reason it is not usable. Safe to call again. */
BRB_API brb_status brb_load(const wchar_t *runtime_dir);

BRB_API brb_status brb_get_status(void);

/* Readable explanation of the current status (English, UTF-8). */
BRB_API const char *brb_status_text(void);

/* Path of the bridge's log file (UTF-8). */
BRB_API const char *brb_log_path(void);

/* Create the bridge's window and start Remix in it, then apply the BGE-Remix
 * configuration (Z up, right-handed, metres). Needs READY. */
BRB_API brb_status brb_startup(int width, int height, int visible);

/* Set one Remix option (rtx.conf key). Returns the remixapi_ErrorCode. */
BRB_API int brb_set_config(const char *key, const char *value);

/* Draw the P1 test scene (a lit triangle over a floor) for `seconds`.
 * screenshot_path (BMP) may be NULL. capture != 0 presses Remix's capture
 * hotkey half-way through. Needs RUNNING. Returns 0 on success. */
BRB_API int brb_triangle_test(double seconds, const wchar_t *screenshot_path, int capture,
                              brb_triangle_result *r_result);

/* Shut Remix down and unload it. Safe to call at any time, also twice. */
BRB_API void brb_shutdown(void);

/* ------------------------------------------------------------------------- */
/* Interface version 2: scenes (P2) */

/* How Remix's picture reaches the screen. */
typedef enum brb_mode {
	BRB_MODE_NONE = 0,
	BRB_MODE_WINDOW = 1, /* P1: a top-level window of the bridge's own (brb_startup) */
	BRB_MODE_COPY = 2,   /* each frame is copied out (brb_frame_pixels) and drawn by the game */
	BRB_MODE_CHILD = 3,  /* Remix presents into a child window over the game window */
} brb_mode;

/* Positions and normals in the object's own space; uv as in Blender
 * (origin bottom left); color is 0xAABBGGRR. */
typedef struct brb_vertex {
	float position[3];
	float normal[3];
	float uv[2];
	uint32_t color;
} brb_vertex;

typedef struct brb_material_desc {
	uint64_t hash;               /* identifies the material; never 0 */
	float albedo[3];             /* linear */
	float opacity;               /* 1 = opaque */
	float roughness;             /* 0..1, perceptual */
	float metallic;
	float emissive[3];           /* linear colour of the light the surface gives off */
	float emissive_intensity;    /* 0 = not emissive */
	int alpha_blend;             /* 1: blend by opacity */
} brb_material_desc;

typedef struct brb_surface_desc {
	const brb_vertex *vertices;
	uint32_t vertex_count;
	const uint32_t *indices;     /* triangles */
	uint32_t index_count;
	uint64_t material;           /* hash of a material created before */
} brb_surface_desc;

typedef enum brb_light_type {
	BRB_LIGHT_SPHERE = 0,        /* position, radius, optional cone */
	BRB_LIGHT_RECT = 1,          /* position, direction, x/y axes, size */
	BRB_LIGHT_DISTANT = 2,       /* direction (where the light travels), angular diameter */
	BRB_LIGHT_DOME = 3,          /* uniform light from all around (sky) */
} brb_light_type;

typedef struct brb_light_desc {
	uint64_t hash;               /* never 0 */
	int type;                    /* brb_light_type */
	float position[3];
	float direction[3];          /* normalized */
	float x_axis[3];             /* rect lights */
	float y_axis[3];
	float radius;                /* sphere lights */
	float size[2];               /* rect lights */
	int shaped;                  /* sphere and rect lights: 1 = cone along direction */
	float cone_angle_deg;        /* half angle */
	float cone_softness;
	float angular_diameter_deg;  /* distant lights */
	float radiance[3];
} brb_light_desc;

typedef struct brb_frame_stats {
	int frames;                  /* frames ended since the last reset */
	int draws;                   /* last frame: instances drawn */
	int lights;                  /* last frame: lights drawn */
	int meshes;                  /* meshes alive */
	int materials;               /* materials alive */
	int light_objects;           /* lights alive */
	int meshes_created;          /* since the last reset */
	int materials_created;
	int lights_created;
	int errors;                  /* calls that did not return success */
	int last_error;
	int width, height;           /* current render size */
	double present_ms;           /* last frame: Present */
	double copy_ms;              /* last frame: copying the picture out (copy mode) */
} brb_frame_stats;

/* Start Remix for a game. mode is BRB_MODE_COPY (a hidden window of the
 * bridge's own; the game draws the copied picture) or BRB_MODE_CHILD (a child
 * window of parent_hwnd at x, y, client coordinates, top left). Needs READY.
 * Remix keeps running after the game ends, for the next one; brb_scene_clear
 * removes the game's objects. */
BRB_API brb_status brb_startup_ex(int mode, int width, int height, void *parent_hwnd, int x, int y);

BRB_API int brb_get_mode(void);

/* New render size (and position, child mode). Returns 0 on success. */
BRB_API int brb_resize(int width, int height, int x, int y);

/* Child mode: show or hide Remix's window over the game (hidden while the
 * game draws with OpenGL). */
BRB_API void brb_show(int visible);

/* Objects: identified by their hash. Creating with a hash that exists
 * replaces the object (the bridge destroys the old one first). Return the
 * remixapi_ErrorCode, 0 on success. */
BRB_API int brb_material_create(const brb_material_desc *desc);
BRB_API void brb_material_destroy(uint64_t hash);
BRB_API int brb_mesh_create(uint64_t hash, const brb_surface_desc *surfaces, uint32_t surface_count);
BRB_API void brb_mesh_destroy(uint64_t hash);
BRB_API int brb_light_create(const brb_light_desc *desc);
BRB_API void brb_light_destroy(uint64_t hash);

/* Destroy every object created through the functions above. */
BRB_API void brb_scene_clear(void);

/* One frame: begin, camera, draws and lights, end. view and proj are
 * OpenGL matrices (column major, clip depth -1..1), as the game engine uses
 * them; model is the object's OpenGL matrix. */
BRB_API int brb_frame_begin(void);
BRB_API int brb_frame_camera(const float view[16], const float proj[16]);
BRB_API int brb_frame_draw(uint64_t mesh, const float model[16], int double_sided);
BRB_API int brb_frame_light(uint64_t light);
BRB_API int brb_frame_end(void);

/* Copy mode: the last frame, 32-bit BGRA rows from the top, valid until the
 * next brb_frame_begin. NULL when there is none. */
BRB_API const void *brb_frame_pixels(int *r_width, int *r_height, int *r_pitch);

/* Save what Remix shows (copy mode: the last frame; window modes: the
 * window) as a 24-bit BMP. Returns 1 when written. */
BRB_API int brb_save_bmp(const wchar_t *path);

BRB_API void brb_get_stats(brb_frame_stats *r_stats);
BRB_API void brb_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif  /* __BGE_REMIX_BRIDGE_H__ */
