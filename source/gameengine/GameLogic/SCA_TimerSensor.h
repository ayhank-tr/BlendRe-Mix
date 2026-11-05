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

/** \file SCA_TimerSensor.h
 *  \ingroup gamelogic
 *
 * BGE-Remix P10.12: the Timer sensor. It fires once, a number of seconds
 * after its state becomes active; with Repeat, every that many seconds. It
 * counts the game's logic time as a timer property does (1 / tic rate a
 * logic tick), so it needs no timer property. A firing is a positive pulse
 * for one logic tick, then a negative one (as a Delay sensor with a duration
 * of one tick).
 */

#ifndef __SCA_TIMERSENSOR_H__
#define __SCA_TIMERSENSOR_H__

#include "SCA_ISensor.h"

class SCA_TimerSensor : public SCA_ISensor
{
	Py_Header
	float  m_seconds;
	bool   m_repeat;
	double m_elapsed;     /* logic time since its state became active */
	double m_due;         /* when it fires next */
	bool   m_done;        /* fired once (no repeat) */
	bool   m_lastResult;  /* fired on the last tick */

public:
	SCA_TimerSensor(class SCA_EventManager *eventmgr, SCA_IObject *gameobj, float seconds, bool repeat);
	virtual ~SCA_TimerSensor();
	virtual CValue *GetReplica();
	virtual bool Evaluate();
	virtual bool IsPositiveTrigger();
	virtual void Init();

	/* not due yet (the live brick state's Yellow) */
	bool IsWaiting() const { return !m_done && !m_lastResult; }

#ifdef WITH_PYTHON
	/* --------------------------------------------------------------------- */
	/* Python interface ---------------------------------------------------- */
	/* --------------------------------------------------------------------- */
#endif
};

#endif  /* __SCA_TIMERSENSOR_H__ */
