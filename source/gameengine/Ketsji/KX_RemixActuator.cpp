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

/** \file gameengine/Ketsji/KX_RemixActuator.cpp
 *  \ingroup ketsji
 *
 * BGE-Remix P10.12: the RTX Remix actuator (KX_RemixActuator.h).
 */

#include <stddef.h>

#include "KX_RemixActuator.h"
#include "KX_GameObject.h"
#include "KX_KetsjiEngine.h"
#include "KX_Scene.h"
#include "SCA_LogicManager.h"

#ifdef WITH_REMIX
#  include "KX_RemixRenderer.h"
#endif

KX_RemixActuator::KX_RemixActuator(SCA_IObject *gameobj, KX_KetsjiEngine *engine, const char *setting,
                                   const char *value)
	: SCA_IActuator(gameobj, KX_ACT_REMIX),
	  m_setting(setting ? setting : ""),
	  m_value(value ? value : ""),
	  m_engine(engine),
	  m_pending(false)
{
}

KX_RemixActuator::~KX_RemixActuator()
{
}

CValue *KX_RemixActuator::GetReplica()
{
	KX_RemixActuator *replica = new KX_RemixActuator(*this);
	replica->ProcessReplica();
	replica->m_pending = false;
	return replica;
}

bool KX_RemixActuator::CanReachRemix()
{
#ifdef WITH_REMIX
	if (m_setting.IsEmpty()) {
		return false;
	}
	KX_RemixRenderer *remix = m_engine ? m_engine->GetRemix() : NULL;
	if (!remix) {
		return false;
	}
	if (remix->IsRunning()) {
		return true;
	}
	/* not running yet: the actuator's scene asks for Remix (the RTX Remix
	 * pipeline with Remix on) */
	KX_GameObject *ob = static_cast<KX_GameObject *>(GetParent());
	KX_Scene *scene = ob ? ob->GetScene() : NULL;
	return scene && KX_RemixRenderer::GetSettings(scene, -1, -1).enabled != 0;
#else
	return false;
#endif
}

bool KX_RemixActuator::Update()
{
	/* (Update runs on an event, or on every tick while it stays active) */
	if (m_posevent) {
		m_pending = true;
	}
	RemoveAllEvents();
	if (!m_pending) {
		return false;
	}
#ifdef WITH_REMIX
	KX_RemixRenderer *remix = m_engine ? m_engine->GetRemix() : NULL;
	if (remix && remix->IsRunning()) {
		/* as bge.render.remixSetOption(setting, value) */
		remix->SetOption(m_setting.ReadPtr(), m_value.ReadPtr());
		m_pending = false;
		/* (for the live brick states: it fired now) */
		if (m_logicManager) {
			SetFiredTick(m_logicManager->GetTick());
		}
		return false;
	}
	if (CanReachRemix()) {
		/* Remix is starting: keep the pulse for its first frame */
		return true;
	}
#endif
	/* the Classic pipeline: nothing to do */
	m_pending = false;
	return false;
}

#ifdef WITH_PYTHON

/* ------------------------------------------------------------------------- */
/* Python functions                                                          */
/* ------------------------------------------------------------------------- */

/* Integration hooks ------------------------------------------------------- */
PyTypeObject KX_RemixActuator::Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"KX_RemixActuator",
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
	&SCA_IActuator::Type,
	0,0,0,0,0,0,
	py_base_new
};

PyMethodDef KX_RemixActuator::Methods[] = {
	{NULL, NULL} //Sentinel
};

PyAttributeDef KX_RemixActuator::Attributes[] = {
	KX_PYATTRIBUTE_STRING_RW("setting", 0, 128, false, KX_RemixActuator, m_setting),
	KX_PYATTRIBUTE_STRING_RW("value", 0, 64, false, KX_RemixActuator, m_value),
	{ NULL }	//Sentinel
};

#endif // WITH_PYTHON
