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

/** \file BRMX_view.h
 *  \ingroup remix
 *
 * BGE-Remix (P5): the "RTX Remix" render engine's work, in C++ for speed.
 * The engine class itself is Python (release/scripts/startup/render_remix),
 * as Cycles', and calls these through the _bge_remix module.
 *
 * - The 3D view (Rendered shading): the scene is read from Blender's data
 *   (what the view shows: modifiers, edit mode, duplicates, lamps, world),
 *   kept in Remix and updated with what changed; each redraw sends the
 *   view's camera and draws Remix's picture into the view.
 * - F12: the scene as the render sees it, the scene camera at the render
 *   size, a number of frames (Real-time) or accumulated samples (Reference),
 *   and the last picture into Blender's render result.
 *
 * Remix has one camera per frame: one 3D view at a time shows it; the others
 * (and orthographic views, and every view while the game or F12 has Remix)
 * ask for Blender's own drawing with RE_ENGINE_VIEW_FALLBACK and a note.
 */

#ifndef __BRMX_VIEW_H__
#define __BRMX_VIEW_H__

#ifdef __cplusplus
extern "C" {
#endif

struct ARegion;
struct Main;
struct RenderEngine;
struct RegionView3D;
struct Scene;
struct View3D;

/* The scene changed (called while Blender's update tags are set). */
void BRMX_view_update(struct RenderEngine *engine, struct Main *bmain, struct Scene *scene,
                      struct ARegion *ar, struct View3D *v3d, struct RegionView3D *rv3d);

/* Draw the view (OpenGL context current, the region's pixel space set).
 * Returns 1 when the view should be drawn again soon (Remix is refining its
 * picture, textures are on their way). */
int BRMX_view_draw(struct RenderEngine *engine, struct Main *bmain, struct Scene *scene,
                   struct ARegion *ar, struct View3D *v3d, struct RegionView3D *rv3d);

/* The engine goes away (main thread). */
void BRMX_view_free(struct RenderEngine *engine);

/* F12, on the main thread before the render job starts: starts Remix if
 * needed (Blender's render thread cannot own Remix's window). Returns 1 when
 * Remix runs; otherwise r_message says why. */
int BRMX_render_prepare(struct Scene *scene, const char **r_message);

/* F12 on the render thread: read the scene (engine.update, while Blender's
 * interface is locked), then render (engine.render). Return 1 on success,
 * else 0 and the message is reported. */
int BRMX_render_sync(struct RenderEngine *engine, struct Main *bmain, struct Scene *scene, const char **r_message);
int BRMX_render_frame(struct RenderEngine *engine, struct Scene *scene, const char **r_message);
void BRMX_render_free(struct RenderEngine *engine);

/* For the tests (BGE_REMIX_STATS): counters of the view and F12, as JSON. */
const char *BRMX_view_stats_json(void);

#ifdef __cplusplus
}
#endif

#endif  /* __BRMX_VIEW_H__ */
