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

/** \file gameengine/Ketsji/KX_FramePacing.cpp
 *  \ingroup ketsji
 */

#include "KX_FramePacing.h"

#include <stdlib.h>

#ifdef WIN32
#  include <windows.h>
#  include <mmsystem.h>
#else
#  include <time.h>
#endif

/* Wake this much before the tic is due: the rest of the wait is the old short spin. */
static const double PACE_MARGIN = 0.0008;
/* Never sleep longer than this in one go (a changed tic rate or clock is picked up soon). */
static const double PACE_MAX = 0.05;

static bool pacing_enabled()
{
	static int on = -1;
	if (on < 0) {
		const char *e = getenv("BGE_REMIX_NO_PACING");
		on = (e && e[0] == '1') ? 0 : 1;
	}
	return on == 1;
}

#ifdef WIN32
#  ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#    define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#  endif

static HANDLE pace_timer()
{
	static HANDLE timer = NULL;
	static bool tried = false;
	if (!tried) {
		tried = true;
		/* Windows 10 1803 and later: a timer precise to about 0.5 ms */
		timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!timer) {
			/* older Windows: the 1 ms system timer makes Sleep precise enough */
			timeBeginPeriod(1);
		}
	}
	return timer;
}
#endif

bool KX_PaceSleep(double seconds)
{
	if (!pacing_enabled()) {
		return false;
	}
	double s = seconds - PACE_MARGIN;
	if (s < 0.0005) {
		return false;
	}
	if (s > PACE_MAX) {
		s = PACE_MAX;
	}
#ifdef WIN32
	HANDLE timer = pace_timer();
	if (timer) {
		LARGE_INTEGER due;
		due.QuadPart = -(LONGLONG)(s * 1.0e7);  /* relative, in 100 ns units */
		if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
			WaitForSingleObject(timer, INFINITE);
			return true;
		}
	}
	Sleep((DWORD)(s * 1000.0));
	return true;
#else
	struct timespec ts;
	ts.tv_sec = (time_t)s;
	ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1.0e9);
	nanosleep(&ts, NULL);
	return true;
#endif
}
