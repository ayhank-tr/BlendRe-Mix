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

/** \file BRMX_present.h
 *  \ingroup remix
 *
 * BGE-Remix (P5.3): putting Remix's picture on screen with OpenGL, shared by
 * the game engine and the 3D view.
 *
 * - Direct mode: Remix's own Vulkan image is drawn with
 *   GL_NV_draw_vulkan_image (wait for "done", draw, signal "resume").
 * - Copy mode: the copied frame is uploaded into a texture and drawn.
 *
 * Every draw saves and restores the OpenGL state it touches, so the caller's
 * drawing (BGE's text, Blender's UI) carries on as before. All calls need the
 * OpenGL context current, from the thread that owns it.
 */

#ifndef __BRMX_PRESENT_H__
#define __BRMX_PRESENT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Whether direct mode can be used with the current OpenGL context: the
 * driver offers GL_NV_draw_vulkan_image (or BGE_REMIX_FAKE_VK_DRAW=1 stands
 * in for it, printing what would be done, for CI). BGE_REMIX_PRESENT=copy
 * says no. When not, r_reason (if given) says why. */
int BRMX_present_direct_available(const char **r_reason);

/* 1 when BGE_REMIX_FAKE_VK_DRAW's stand-ins are in use. */
int BRMX_present_fake(void);

/* Draw the frame the bridge has just presented over the rectangle x, y,
 * width, height of the window (OpenGL window coordinates: bottom left
 * origin), in the bridge's mode. Returns 1 when the picture was drawn, 2 when
 * Remix's frame was taken but holds no picture (it changed size on that
 * frame; nothing drawn), 0 when there was nothing to draw. */
int BRMX_present_draw(int x, int y, int width, int height);

/* Frames drawn in direct mode since the program started (the stats). */
int BRMX_present_direct_frames(void);

/* Free the OpenGL objects of the presenter (a texture and a sampler); with
 * the context current. */
void BRMX_present_free(void);

#ifdef __cplusplus
}
#endif

#endif  /* __BRMX_PRESENT_H__ */
