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

/** \file KX_RemixActuator.h
 *  \ingroup ketsji
 *
 * BGE-Remix P10.12: the RTX Remix actuator. On a pulse it sends one of RTX
 * Remix's own settings (rtx.*) with its value to Remix, as
 * bge.render.remixSetOption does. A pulse given while Remix is still
 * starting is kept and sent on its first frame. In the Classic pipeline it
 * does nothing (the Remix View warns about it).
 */

#ifndef __KX_REMIXACTUATOR_H__
#define __KX_REMIXACTUATOR_H__

#include "SCA_IActuator.h"
#include "STR_String.h"

class KX_KetsjiEngine;

class KX_RemixActuator : public SCA_IActuator
{
	Py_Header
	STR_String m_setting;
	STR_String m_value;
	KX_KetsjiEngine *m_engine;
	bool m_pending;   /* a pulse came while Remix was starting */

public:
	KX_RemixActuator(SCA_IObject *gameobj, KX_KetsjiEngine *engine, const char *setting, const char *value);
	virtual ~KX_RemixActuator();
	virtual CValue *GetReplica();
	virtual bool Update();

	/* for the live brick states: waiting for Remix to start (Yellow), and
	 * whether the setting can reach Remix at all (no: Red) */
	bool IsPending() const { return m_pending; }
	bool CanReachRemix();

#ifdef WITH_PYTHON
	/* --------------------------------------------------------------------- */
	/* Python interface ---------------------------------------------------- */
	/* --------------------------------------------------------------------- */
#endif
};

#endif  /* __KX_REMIXACTUATOR_H__ */
