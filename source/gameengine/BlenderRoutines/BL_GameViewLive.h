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

/** \file BL_GameViewLive.h
 *  \ingroup blroutines
 *
 * BGE-Remix P10.9: the Remix View's live data while Run mode plays the game:
 * one entry per element of the model (BL_GameView.h) the run started with,
 * worked out every frame after the scene's logic ran. The editor draws it
 * over the running game (view3d_gameview.c); _bge_gameview.live() gives it
 * to Python (the tests).
 *
 * Plain C: included by the editors.
 */

#ifndef __BL_GAMEVIEWLIVE_H__
#define __BL_GAMEVIEWLIVE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GVLive {
	short state;          /* GVState this frame (logic bricks: KX_LogicBrickLiveState) */
	short fired;          /* this frame: a brick fired, a property changed, a body touches */
	short present;        /* its object is in the running game (a mock-up hides the rest) */
	short pad;
	int fired_frames;     /* frames it fired in this run */
	int pad2;
	char text[40];        /* a short value: a property's value, a body's contacts */
} GVLive;

/* the model generation the live data was made for (-1: none) */
int BL_GameViewLiveGeneration(void);
/* the live data of an element of that model (NULL: none) */
const GVLive *BL_GameViewLiveGet(int element);
/* a Run is playing now (the editor draws the live data over the game) */
int BL_GameViewLiveRunning(void);
/* where an element's object is in the running game (false: not in it) */
int BL_GameViewLiveMatrix(const char *object_name, float r_mat[4][4]);

#ifdef __cplusplus
}
#endif

#endif  /* __BL_GAMEVIEWLIVE_H__ */
