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

/** \file BRMX_session.h
 *  \ingroup remix
 *
 * BGE-Remix (P5): one running Remix for the game, the 3D view and F12.
 *
 * Remix has one device per program and one camera per frame, and its device
 * is not made for calls from several threads (F12 runs on Blender's render
 * thread). So:
 *
 * - a lock: whoever calls Remix holds BRMX_lock() for the whole frame;
 * - an owner: the game, a 3D view or F12. Taking Remix over removes the
 *   previous owner's objects (brb_scene_clear) and tells it so (its "lost"
 *   function), so that it sends everything again when it gets Remix back;
 * - the quality settings of the scene (P5.1), sent when they change.
 */

#ifndef __BRMX_SESSION_H__
#define __BRMX_SESSION_H__

#ifdef __cplusplus
extern "C" {
#endif

struct Scene;

/* Held for a whole frame (or any group of bridge calls); may be taken again
 * by the same thread. */
void BRMX_lock(void);
void BRMX_unlock(void);

enum {
	BRMX_OWNER_NONE = 0,
	BRMX_OWNER_GAME = 1,
	BRMX_OWNER_VIEW = 2,
	BRMX_OWNER_RENDER = 3,
};

typedef void (*BRMX_LostFn)(void *owner);

/* Make owner the user of Remix's scene (call with the lock held). When
 * another owner had it, its objects are removed and its lost function is
 * called (with the lock held). Returns 1 when the owner changed. */
int BRMX_owner_take(int kind, void *owner, BRMX_LostFn lost);
/* The owner is going away (its objects are its own business). */
void BRMX_owner_release(void *owner);
int BRMX_owner_kind(void);
void *BRMX_owner(void);

/* P5.1 quality settings: custom properties of the scene (Render panel),
 * with the values of the panel's menus. */
typedef struct BRMX_Quality {
	int preset;               /* 0 Auto, 1 Low, 2 Medium, 3 High, 4 Ultra */
	int dlss;                 /* 0 Auto, 1 Ultra Performance, 2 Performance, 3 Balanced, 4 Quality, 5 DLAA */
	int ray_reconstruction;   /* 1 on (Remix's default) */
	int volumetrics;          /* 1 on (Remix's default) */
} BRMX_Quality;

void BRMX_quality_default(BRMX_Quality *r_quality);
void BRMX_quality_from_scene(const struct Scene *scene, BRMX_Quality *r_quality);
/* Send what differs from what Remix has. Settings at Remix's defaults are not
 * sent until they were changed, so an rtx.conf keeps working. Returns the
 * number of settings sent. */
int BRMX_quality_apply(const BRMX_Quality *quality);

#ifdef __cplusplus
}
#endif

#endif  /* __BRMX_SESSION_H__ */
