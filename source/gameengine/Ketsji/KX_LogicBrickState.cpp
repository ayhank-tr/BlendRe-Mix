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

/** \file gameengine/Ketsji/KX_LogicBrickState.cpp
 *  \ingroup ketsji
 *
 * BGE-Remix P10.10: a logic brick's live state (KX_LogicBrickState.h).
 * The P10 scaffold's stand-in: every brick ready; the logic track (p10c)
 * fills it in.
 */

#include "KX_LogicBrickState.h"
#include "SCA_ILogicBrick.h"

KX_BrickLiveState KX_LogicBrickLiveState(SCA_ILogicBrick *brick, bool built_red)
{
	if (built_red || brick == NULL)
		return KX_BRICK_RED;
	return KX_BRICK_WHITE;
}
