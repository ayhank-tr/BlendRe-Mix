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

/** \file KX_FramePacing.h
 *  \ingroup ketsji
 *
 * BGE-Remix (CPU performance, from BGE NEXT): when the frame rate is limited, the game used to
 * spin on the CPU until the next logic tic was due. It now sleeps on a precise timer instead,
 * waking a little early so the tic still starts on time. BGE_REMIX_NO_PACING=1 spins as before.
 */

#ifndef __KX_FRAMEPACING_H__
#define __KX_FRAMEPACING_H__

/* Sleep for about `seconds` (less than that, never more than a fraction of a millisecond
 * over). Returns false when pacing is switched off or the wait is too short to sleep. */
bool KX_PaceSleep(double seconds);

#endif  /* __KX_FRAMEPACING_H__ */
