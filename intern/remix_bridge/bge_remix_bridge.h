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
 * 1: P1 (load, start, triangle test). 2: P2 (scene interface, copy and window modes).
 * 3: P3 (textures, glass, subsurface, alpha test).
 * 4: P4 (skinning, draw categories, picking, particle systems).
 * 5: P4.8 (direct presentation: brb_frame_vk).
 * 8: P8.9 (disk lights, lights' volumetric strength). */
#define BRB_ABI_VERSION 8

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
	BRB_MODE_DIRECT = 4, /* P4.8: Remix has no window; the game draws its Vulkan image itself
	                      * (GL_NV_draw_vulkan_image, brb_frame_vk) */
} brb_mode;

/* Positions and normals in the object's own space; uv as in Blender
 * (origin bottom left); color is 0xAABBGGRR. */
typedef struct brb_vertex {
	float position[3];
	float normal[3];
	float uv[2];
	uint32_t color;
} brb_vertex;

typedef enum brb_material_type {
	BRB_MATERIAL_OPAQUE = 0,      /* Remix's opaque PBR material (also see-through by opacity) */
	BRB_MATERIAL_TRANSLUCENT = 1, /* glass: refracts, tinted by transmittance */
} brb_material_type;

/* Colours are linear here, as in Blender; the bridge hands Remix the
 * sRGB-encoded values its shaders expect (albedo, emission, transmittance). */
typedef struct brb_material_desc {
	uint64_t hash;               /* identifies the material; never 0 */
	float albedo[3];             /* linear; ignored where albedo_texture is set */
	float opacity;               /* 1 = opaque */
	float roughness;             /* 0..1, perceptual */
	float metallic;
	float emissive[3];           /* linear colour of the light the surface gives off */
	float emissive_intensity;    /* 0 = not emissive */
	int alpha_blend;             /* 1: blend by opacity (and the albedo texture's alpha) */

	/* interface 3 (P3) */
	int type;                    /* brb_material_type */
	int alpha_test;              /* 1: cut out where alpha <= alpha_cutoff (no blending) */
	float alpha_cutoff;          /* 0..1 */
	/* DDS files (brb_texture_write); NULL or "" = none. Remix keeps the path. */
	const wchar_t *albedo_texture;    /* sRGB colour, alpha = opacity */
	const wchar_t *normal_texture;    /* BRB_TEXTURE_NORMAL */
	const wchar_t *roughness_texture; /* BRB_TEXTURE_SCALAR */
	const wchar_t *metallic_texture;  /* BRB_TEXTURE_SCALAR */
	const wchar_t *emissive_texture;  /* sRGB colour, times emissive_intensity */
	int filter_nearest;          /* 1: no texture filtering (pixel art) */
	int clamp;                   /* 1: textures clamp at the edge instead of repeating */
	/* BRB_MATERIAL_TRANSLUCENT */
	float ior;                   /* refractive index, e.g. 1.5 */
	float transmittance[3];      /* colour after transmittance_distance of glass */
	float transmittance_distance;/* Blender units */
	int thin_walled;             /* 1: a thin shell (no refraction offset) */
	/* subsurface scattering (BRB_MATERIAL_OPAQUE) */
	int subsurface;              /* 1: on */
	float subsurface_color[3];   /* single-scattering albedo, linear */
	float subsurface_radius[3];  /* mean free path per channel, Blender units */
	/* interface 7 (P8.8) */
	int additive;                /* 1: added to what is behind (Blender's Add blending) */
} brb_material_desc;

/* interface 4 (P4): bone weights of a skinned surface, bones_per_vertex
 * values per vertex in each array. Remix moves each vertex by the bone
 * matrices of the draw (brb_draw_desc.bones), weighted. */
#define BRB_MAX_BONES_PER_VERTEX 4
#define BRB_MAX_BONES 256
typedef struct brb_skin_desc {
	uint32_t bones_per_vertex;   /* 1 .. BRB_MAX_BONES_PER_VERTEX */
	const float *weights;        /* adding up to 1 per vertex */
	const uint32_t *bones;       /* indices into the draw's bone matrices */
} brb_skin_desc;

typedef struct brb_surface_desc {
	const brb_vertex *vertices;
	uint32_t vertex_count;
	const uint32_t *indices;     /* triangles */
	uint32_t index_count;
	uint64_t material;           /* hash of a material created before */
	const brb_skin_desc *skin;   /* interface 4: NULL = not skinned */
} brb_surface_desc;

typedef enum brb_light_type {
	BRB_LIGHT_SPHERE = 0,        /* position, radius, optional cone */
	BRB_LIGHT_RECT = 1,          /* position, direction, x/y axes, size */
	BRB_LIGHT_DISTANT = 2,       /* direction (where the light travels), angular diameter */
	BRB_LIGHT_DOME = 3,          /* uniform light from all around (sky) */
	BRB_LIGHT_DISK = 4,          /* interface 8: position, direction, x/y axes, radii in size[], optional cone */
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
	float volumetric;            /* interface 8: the light's strength in volumetric fog (Remix's 1 = 0 here) */
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
	/* interface 4 */
	int skinned_draws;           /* last frame: draws with bone matrices */
	int bones;                   /* last frame: bone matrices sent */
	int emitters;                /* last frame: GPU particle emitters drawn */
	int picks;                   /* pick requests answered since the last reset */
	/* interface 6 */
	int resets;                  /* direct mode: picture size changes since the last reset */
	int reads;                   /* frames read with brb_frame_read since the last reset */
} brb_frame_stats;

/* Start Remix for a game. mode is BRB_MODE_COPY (a hidden window of the
 * bridge's own; the game draws the copied picture), BRB_MODE_CHILD (a child
 * window of parent_hwnd at x, y, client coordinates, top left) or
 * BRB_MODE_DIRECT (no window; the game draws Remix's image, brb_frame_vk).
 * Needs READY. Remix keeps running after the game ends, for the next one (and
 * for the 3D view and F12, P5); brb_scene_clear removes the game's objects. */
BRB_API brb_status brb_startup_ex(int mode, int width, int height, void *parent_hwnd, int x, int y);

BRB_API int brb_get_mode(void);

/* New render size (and position, child mode). Returns 0 on success.
 * Direct mode (interface 6): Remix's own present call resizes its picture to
 * its hidden window, but only when the width and the height both change
 * (rtx_remix_api.cpp, Remix 1.6.1); the bridge resizes the window and, when
 * only one side changes, goes through an in-between size. The new size shows
 * one or two frames later; until then brb_frame_vk returns 2 for the frames
 * Remix resized on. */
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
#define BRB_DRAW_DOUBLE_SIDED 1 /* no back-face culling */
#define BRB_DRAW_BLEND 2        /* the mesh has see-through (alpha blended) surfaces */
BRB_API int brb_frame_draw(uint64_t mesh, const float model[16], int flags);
BRB_API int brb_frame_light(uint64_t light);
BRB_API int brb_frame_end(void);

/* Copy mode: the last frame, 32-bit BGRA rows from the top, valid until the
 * next brb_frame_begin. NULL when there is none. */
BRB_API const void *brb_frame_pixels(int *r_width, int *r_height, int *r_pitch);

/* Direct mode: after a brb_frame_end that presented, the Vulkan image
 * (VkImage) and its two binary semaphores (VkSemaphore): "done" is signalled
 * when the picture is ready, and Remix waits for "resume" before it renders
 * the next frame. The caller MUST wait for done and then signal resume exactly
 * once for every frame this returns 1 or 2 for, or Remix stops for good.
 * 2 (interface 6): Remix changed its picture size on this frame and the image
 * holds no picture yet: wait and signal, but do not draw it. Returns 0 when no
 * frame waits (not direct mode, or already taken). */
BRB_API int brb_frame_vk(uint64_t *r_image, uint64_t *r_done, uint64_t *r_resume, int *r_width, int *r_height);

/* Save what Remix shows (copy mode: the last frame; window modes: the
 * window) as a 24-bit BMP. Returns 1 when written; 0 in direct mode, where
 * the game reads its own window back. */
BRB_API int brb_save_bmp(const wchar_t *path);

BRB_API void brb_get_stats(brb_frame_stats *r_stats);
BRB_API void brb_reset_stats(void);

/* ------------------------------------------------------------------------- */
/* Interface version 4: dynamic content (P4) */

/* What Remix should treat a draw as (brb_draw_desc.categories) */
#define BRB_CATEGORY_PARTICLE 1          /* a sprite or particle (Remix's particle handling) */
#define BRB_CATEGORY_PARTICLE_EMITTER 2  /* spawns Remix GPU particles (brb_draw_desc.particles) */

/* Remix's own GPU particles, spawned from the draw's triangles. Lengths
 * in Blender units (metres), times in seconds; colours linear RGBA. "start"
 * values apply at birth, "end" values at the end of a particle's life. */
typedef struct brb_particle_desc {
	uint32_t max_particles;
	float spawn_rate;            /* particles per second */
	float lifetime_min, lifetime_max;
	float speed_normal;          /* start speed along the surface normal */
	float cone_angle_deg;        /* random spread around the normal (half angle) */
	float speed_from_motion;     /* times the emitter's own velocity */
	float gravity;               /* downward acceleration (9.81 = Earth) */
	float drag;                  /* 0 = none */
	float size_min, size_max;    /* start size (width = height) */
	float size_end_scale;        /* end size = start size times this */
	float color_start[4];
	float color_end[4];
	float rotation_speed;        /* turns per second, random sign */
	int collide;                 /* 1: bounce off the scene */
	float restitution;           /* kept speed after a bounce, 0..1 */
	int align_to_velocity;
	int hide_emitter;            /* 1: the emitter mesh itself is not drawn */
	int billboard;               /* 0 face the camera, 1 upright (turn around Z), 2 lie flat (face up) */
} brb_particle_desc;

/* One draw with everything interface 4 adds. bones: bone_count OpenGL
 * matrices (column major), each mapping the mesh's rest pose to its pose,
 * in the object's own space (model applies after). */
typedef struct brb_draw_desc {
	uint64_t mesh;
	float model[16];
	int flags;                   /* BRB_DRAW_* */
	uint32_t categories;         /* BRB_CATEGORY_* */
	uint32_t pick_id;            /* 0 = not pickable */
	const float *bones;
	uint32_t bone_count;         /* at most BRB_MAX_BONES */
	const brb_particle_desc *particles; /* with BRB_CATEGORY_PARTICLE_EMITTER */
} brb_draw_desc;

BRB_API int brb_frame_draw_ex(const brb_draw_desc *desc);

/* Ask which pick_ids Remix drew in a rectangle of its picture (pixels, top
 * left origin, right and bottom exclusive). The answer arrives a frame or
 * two later; a new request replaces one still waiting. Returns 0 when sent. */
BRB_API int brb_pick_request(int left, int top, int right, int bottom);

/* The answer to the last request: 1 = ready (r_ids gets up to max ids, sorted,
 * r_count how many there were), 0 = still waiting or none asked, -1 = the
 * request failed. An answer is handed out once. */
BRB_API int brb_pick_result(uint32_t *r_ids, int max, int *r_count);

/* Outline these pick_ids in this frame's picture (call every frame). */
BRB_API int brb_highlight(const uint32_t *ids, int count, float r, float g, float b);

/* ------------------------------------------------------------------------- */
/* Interface version 3: textures (P3)
 *
 * Remix loads material textures only as DDS files. These calls write one,
 * with a full mip chain, in the format Remix expects for its kind. They do
 * not need Remix to be running (only the bridge loaded). */

typedef enum brb_texture_kind {
	BRB_TEXTURE_COLOR = 0,   /* sRGB colour + alpha -> BC7 (sRGB values, UNORM format: Remix decodes them) */
	BRB_TEXTURE_NORMAL = 1,  /* tangent-space normal map as Blender uses it (OpenGL, RGB) -> octahedral BC5 */
	BRB_TEXTURE_SCALAR = 2,  /* one channel (roughness, metallic...) -> BC4 */
	BRB_TEXTURE_HDR = 3,     /* linear colour beyond 1 -> BC6H */
} brb_texture_kind;

#define BRB_TEXTURE_NO_GPU 1 /* compress on the CPU even when a GPU is there */
#define BRB_TEXTURE_LINEAR 2 /* COLOR: the bytes are linear, not sRGB (encoded to sRGB) */
#define BRB_TEXTURE_INVERT 4 /* SCALAR: store 1 - value */

typedef struct brb_texture_desc {
	const wchar_t *path;         /* the .dds to write; its folder must exist */
	int kind;                    /* brb_texture_kind */
	int width, height;
	/* One of the two, width * height * 4 values, rows from the bottom
	 * (as Blender stores images): */
	const unsigned char *rgba8;  /* 8-bit RGBA (sRGB for colour) */
	const float *rgbaf;          /* float RGBA, linear */
	int channel;                 /* SCALAR: the channel to keep, 0 = R .. 3 = A */
	int flags;                   /* BRB_TEXTURE_* */
} brb_texture_desc;

typedef struct brb_texture_result {
	int state;                   /* 0 waiting, 1 written, -1 failed */
	int gpu;                     /* 1: compressed on the GPU */
	int dxgi_format;             /* the DDS file's DXGI_FORMAT */
	int mips;
	double seconds;              /* encoding time */
	char error[128];
} brb_texture_result;

/* Write the DDS now. Returns 0 on success (r_result may be NULL). */
BRB_API int brb_texture_write(const brb_texture_desc *desc, brb_texture_result *r_result);

/* Copy the pixels and write the DDS on the bridge's encoder thread, one job
 * after the other. Returns a ticket (> 0), or 0 if the job was refused. The
 * file appears complete or not at all. */
BRB_API int brb_texture_submit(const brb_texture_desc *desc);

/* The state of a submitted job (brb_texture_result.state; -2: unknown
 * ticket). A finished job is forgotten after it has been polled once. */
BRB_API int brb_texture_poll(int ticket, brb_texture_result *r_result);

/* Jobs waiting or being encoded. */
BRB_API int brb_texture_pending(void);

/* ------------------------------------------------------------------------- */
/* Interface version 6: the 3D view and F12 (P5) */

/* Read the frame the last brb_frame_end presented into memory: 32-bit RGBA,
 * rows from the top, width * 4 bytes each; max_bytes is the buffer's size.
 * Copy mode: the copied frame. Direct mode: the bridge waits for the frame,
 * draws it with an OpenGL context of its own (any thread; the caller's OpenGL
 * state is not touched) and signals Remix to go on; the frame is then taken,
 * so it is not drawn as well. Returns 1 when read, 0 when there is nothing
 * to read, -1 when the buffer is too small (r_width, r_height still set). */
BRB_API int brb_frame_read(unsigned char *r_rgba, int max_bytes, int *r_width, int *r_height);
/* r_rgba NULL and max_bytes 0: only take the frame (direct mode: wait and
 * signal, so that Remix goes on), e.g. the frames before the last one of an
 * F12 render. Direct mode keeps the bridge's OpenGL context of the calling
 * thread from one read to the next; brb_frame_read_end (same thread) frees it. */
BRB_API void brb_frame_read_end(void);

/* Whether the driver offers GL_NV_draw_vulkan_image (direct mode), checked
 * with an OpenGL context of the bridge's own: for choosing the mode where
 * the caller has no OpenGL context of its own. */
BRB_API int brb_gl_direct_supported(void);

#ifdef __cplusplus
}
#endif

#endif  /* __BGE_REMIX_BRIDGE_H__ */
