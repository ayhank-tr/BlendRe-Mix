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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file BL_GameViewRun.h
 *  \ingroup blroutines
 *
 * BGE-Remix P10.9: the Remix View's Run mode. The game plays inside the
 * Remix View's own area through 2.79's embedded loop (BL_KetsjiEmbedStart),
 * which this file hooks into:
 *
 * - after the scene is converted: a mock-up keeps only the chosen objects
 *   and what they need; a resume puts the suspended game's state back;
 * - every frame, before the picture is shown: the live data
 *   (BL_GameViewLive.h) and the editor's holograms over the game;
 * - the Suspend and Stop buttons (and keys) drawn over the game;
 * - at the end: a suspend keeps the game's state as a snapshot.
 *
 * Suspend is snapshot and restore: the game ends as on Esc (the engine is
 * gone, Blender is back to normal and the file may change in any way), and
 * Resume starts the game again from the file and puts the snapshot back
 * (by object names, never by pointers). Python (bl_operators/gameview_modes)
 * writes the snapshot's places and property values into the file while the
 * game is suspended, so you edit the frozen game.
 *
 * Plain C, with C++ hooks for BL_KetsjiEmbedStart.cpp.
 */

#ifndef __BL_GAMEVIEWRUN_H__
#define __BL_GAMEVIEWRUN_H__

#ifdef __cplusplus
extern "C" {
#endif

struct ARegion;
struct Main;
struct MemFile;
struct bContext;
struct wmEvent;

/* the editor's holograms over the running game: frame is the game's
 * rectangle in window coordinates (xmin, ymin, xmax, ymax), the matrices
 * the game camera's */
typedef void (*GVRunOverlayFn)(struct bContext *C, struct ARegion *ar, const int frame[4], float viewmat[4][4],
                               float winmat[4][4]);

typedef struct GVRunOptions {
	int resume;              /* put the snapshot back after converting */
	int mockup;              /* play only the objects in keep */
	int suspend_after;       /* suspend after this many frames (0: no) */
	int frame[4];            /* the game's rectangle (window coordinates) */
	const char *keep;        /* mock-up: object names, one per line */
	const char *shot;        /* tests: a picture of the last frame (PNG), or NULL */
	GVRunOverlayFn overlay;
} GVRunOptions;

/* how the last run ended */
enum {
	GV_RUN_OUT_NONE = 0,
	GV_RUN_OUT_SUSPEND,    /* suspended: the snapshot was kept */
	GV_RUN_OUT_STOP,       /* stopped (Stop, the exit key) */
	GV_RUN_OUT_QUIT,       /* the game ended itself */
	GV_RUN_OUT_ERROR,      /* it could not start */
};

/* Around StartKetsjiShell: the hooks are on between the two. */
void BL_GameViewRunBegin(struct bContext *C, struct ARegion *ar, const GVRunOptions *opts);
int BL_GameViewRunFinish(void);      /* GV_RUN_OUT_* */
int BL_GameViewRunActive(void);

/* the snapshot of the last suspend (dropped by Start over, Stop, a new file) */
int BL_GameViewRunHasSnapshot(void);
void BL_GameViewRunForget(void);

/* the Suspend and Stop buttons over the game: region rectangles
 * (xmin, ymin, xmax, ymax) for a region winx x winy */
void BL_GameViewRunButtons(int winx, int winy, int r_suspend[4], int r_stop[4]);
int BL_GameViewRunFrames(void);

/* the file as it was when Run started (Stop, then No, puts it back) */
void BL_GameViewRunFileSave(struct Main *bmain);
struct MemFile *BL_GameViewRunFile(void);
void BL_GameViewRunFileFree(void);

/* Python: Run mode's functions, added to _bge_gameview */
struct _object;
void BL_GameViewRun_addPython(struct _object *module);

#ifdef __cplusplus
}

class KX_Scene;
class KX_KetsjiEngine;

/* hooks for BL_KetsjiEmbedStart.cpp (only while BL_GameViewRunActive) */
void BL_GameViewRunConverted(KX_Scene *scene, KX_KetsjiEngine *engine);
bool BL_GameViewRunEvent(const struct wmEvent *event, struct ARegion *ar);  /* true: the Remix View took it */
int BL_GameViewRunFrame(KX_KetsjiEngine *engine);    /* an exit request, or 0 */
void BL_GameViewRunPreSwap(void *engine);            /* KX_BlenderCanvas, before each swap */
void BL_GameViewRunStopping(KX_KetsjiEngine *engine);  /* the loop ended, the scene still there */
#endif

#endif  /* __BL_GAMEVIEWRUN_H__ */
