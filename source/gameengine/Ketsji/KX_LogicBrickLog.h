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

/** \file KX_LogicBrickLog.h
 *  \ingroup ketsji
 *
 * BGE-Remix P10.10: the live brick states (KX_LogicBrickState.h) written
 * every logic tick for the tests, in the player as in Blender, when
 * BGE_REMIX_P10_BRICK_LOG names a file. One line per object and per brick:
 *
 *   tick <tab> scene <tab> object <tab> brick <tab> kind <tab> state
 *
 * kind is the Remix View's ("sensor:DELAY", "actuator:MOTION", ...; "object"
 * for the object's own line, whose state is its state mask); state is RED,
 * BLACK, WHITE, GREEN, BLUE or YELLOW. Unset, it costs one test a tick.
 */

#ifndef __KX_LOGICBRICKLOG_H__
#define __KX_LOGICBRICKLOG_H__

#include "KX_LogicBrickState.h"

class KX_Scene;

/* the brick's kind, as the Remix View names it ("sensor:DELAY", ...) */
const char *KX_LogicBrickKind(SCA_ILogicBrick *brick);
/* "RED", "BLACK", "WHITE", "GREEN", "BLUE", "YELLOW" */
const char *KX_LogicBrickStateName(KX_BrickLiveState state);

/* after the scene's LogicEndFrame (KX_KetsjiEngine::NextFrame) */
void KX_LogicBrickLogTick(KX_Scene *scene);

#endif  /* __KX_LOGICBRICKLOG_H__ */
