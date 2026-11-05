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

/** \file KX_LogicBrickState.h
 *  \ingroup ketsji
 *
 * BGE-Remix P10.10: a logic brick's live state while the game runs, in
 * Ayhan's colours. The Remix View's Run mode (P10.9, BlenderRoutines) asks
 * for it every frame for each brick it shows; the numbers are the same as
 * BL_GameView.h's GVState.
 */

#ifndef __KX_LOGICBRICKSTATE_H__
#define __KX_LOGICBRICKSTATE_H__

class SCA_ILogicBrick;

enum KX_BrickLiveState {
	KX_BRICK_RED = 0,     /* unavailable: its object, target or file is missing */
	KX_BRICK_BLACK,       /* available, in a state the object is not in */
	KX_BRICK_WHITE,       /* ready: in an active state, not fired this frame */
	KX_BRICK_GREEN,       /* active: fired or running this frame */
	KX_BRICK_BLUE,        /* continuing: an actuator still at work */
	KX_BRICK_YELLOW       /* waiting: a delay, timer or animation not yet due */
};

/* the brick's state this frame (call after the scene's logic ran for the
 * frame); built_red: the Remix View's build found it unavailable */
KX_BrickLiveState KX_LogicBrickLiveState(SCA_ILogicBrick *brick, bool built_red);

#endif  /* __KX_LOGICBRICKSTATE_H__ */
