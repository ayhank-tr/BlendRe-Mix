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

/** \file gameengine/GameLogic/SCA_TimerSensor.cpp
 *  \ingroup gamelogic
 *
 * BGE-Remix P10.12: the Timer sensor (SCA_TimerSensor.h).
 */

#include <stddef.h>
#include <math.h>

#include "SCA_TimerSensor.h"
#include "SCA_LogicManager.h"
#include "SCA_EventManager.h"

/* added time is a sum of 1 / tic rate steps: 15 steps of 1/60 make 0.25
 * within a rounding error, which must still count as due */
#define TIMER_EPSILON 1e-6

SCA_TimerSensor::SCA_TimerSensor(class SCA_EventManager *eventmgr, SCA_IObject *gameobj, float seconds, bool repeat)
	: SCA_ISensor(gameobj, eventmgr),
	  m_seconds(seconds),
	  m_repeat(repeat)
{
	Init();
}

SCA_TimerSensor::~SCA_TimerSensor()
{
}

void SCA_TimerSensor::Init()
{
	/* (called when its state becomes active: the sensor registers) */
	m_elapsed = 0.0;
	m_due = (m_seconds > 0.0f) ? (double)m_seconds : 0.0;
	m_done = false;
	m_lastResult = false;
	m_reset = true;
}

CValue *SCA_TimerSensor::GetReplica()
{
	CValue *replica = new SCA_TimerSensor(*this);
	replica->ProcessReplica();
	return replica;
}

bool SCA_TimerSensor::IsPositiveTrigger()
{
	return m_invert ? !m_lastResult : m_lastResult;
}

bool SCA_TimerSensor::Evaluate()
{
	/* the tick's game time, as timer properties count it */
	m_elapsed += m_logicManager ? m_logicManager->GetTickTime() : 0.0;

	bool result = false;
	if (!m_done && m_elapsed + TIMER_EPSILON >= m_due) {
		result = true;
		if (!m_repeat) {
			m_done = true;
		}
		else if (m_seconds > 0.0f) {
			/* the next time due after now (a tick longer than seconds fires
			 * once a tick, it does not catch up) */
			const double steps = floor((m_elapsed + TIMER_EPSILON - m_due) / (double)m_seconds) + 1.0;
			m_due += steps * (double)m_seconds;
		}
		/* (repeating with no seconds: every tick) */
	}
	/* a firing always triggers, even right after another one (every tick) */
	const bool trigger = result || (result != m_lastResult) || (m_reset && m_level);
	m_reset = false;
	m_lastResult = result;
	return trigger;
}

#ifdef WITH_PYTHON

/* ------------------------------------------------------------------------- */
/* Python functions                                                          */
/* ------------------------------------------------------------------------- */

/* Integration hooks ------------------------------------------------------- */
PyTypeObject SCA_TimerSensor::Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"SCA_TimerSensor",
	sizeof(PyObjectPlus_Proxy),
	0,
	py_base_dealloc,
	0,
	0,
	0,
	0,
	py_base_repr,
	0,0,0,0,0,0,0,0,0,
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
	0,0,0,0,0,0,0,
	Methods,
	0,
	0,
	&SCA_ISensor::Type,
	0,0,0,0,0,0,
	py_base_new
};

PyMethodDef SCA_TimerSensor::Methods[] = {
	{NULL, NULL} //Sentinel
};

PyAttributeDef SCA_TimerSensor::Attributes[] = {
	/* (a change counts from the next firing) */
	KX_PYATTRIBUTE_FLOAT_RW("seconds", 0.0f, 100000.0f, SCA_TimerSensor, m_seconds),
	KX_PYATTRIBUTE_BOOL_RW("repeat", SCA_TimerSensor, m_repeat),
	{ NULL }	//Sentinel
};

#endif // WITH_PYTHON
