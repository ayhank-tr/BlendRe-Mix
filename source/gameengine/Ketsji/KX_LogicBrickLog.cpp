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

/** \file gameengine/Ketsji/KX_LogicBrickLog.cpp
 *  \ingroup ketsji
 *
 * BGE-Remix P10.10: the test-only log of the live brick states
 * (KX_LogicBrickLog.h).
 */

#include <stdio.h>
#include <stdlib.h>

#include "KX_LogicBrickLog.h"
#include "KX_GameObject.h"
#include "KX_Scene.h"
#include "EXP_ListValue.h"
#include "SCA_IActuator.h"
#include "SCA_IController.h"
#include "SCA_ISensor.h"
#include "SCA_LogicManager.h"

static FILE *brick_log()
{
	static bool checked = false;
	static FILE *log = NULL;
	if (!checked) {
		checked = true;
		const char *path = getenv("BGE_REMIX_P10_BRICK_LOG");
		if (path && path[0]) {
			log = fopen(path, "w");
			if (log) {
				fprintf(log, "# BGE-Remix P10.10 brick log: tick, scene, object, brick, kind, state\n");
			}
			else {
				printf("BGE-Remix: could not write the brick log %s\n", path);
			}
		}
	}
	return log;
}

static void log_brick(FILE *log, int tick, const char *scene, const char *object, SCA_ILogicBrick *brick)
{
	fprintf(log, "%d\t%s\t%s\t%s\t%s\t%s\n", tick, scene, object, brick->GetName().ReadPtr(),
	        KX_LogicBrickKind(brick), KX_LogicBrickStateName(KX_LogicBrickLiveState(brick, false)));
}

void KX_LogicBrickLogTick(KX_Scene *scene)
{
	FILE *log = brick_log();
	if (!log || !scene) {
		return;
	}
	SCA_LogicManager *lm = scene->GetLogicManager();
	const int tick = lm ? lm->GetTick() : 0;
	const char *sname = scene->GetName().ReadPtr();
	CListValue *objects = scene->GetObjectList();
	for (int i = 0; objects && i < objects->GetCount(); i++) {
		KX_GameObject *ob = static_cast<KX_GameObject *>(objects->GetValue(i));
		const SCA_SensorList &sensors = ob->GetSensors();
		const SCA_ControllerList &controllers = ob->GetControllers();
		const SCA_ActuatorList &actuators = ob->GetActuators();
		if (sensors.empty() && controllers.empty() && actuators.empty()) {
			continue;
		}
		const char *oname = ob->GetName().ReadPtr();
		fprintf(log, "%d\t%s\t%s\t\tobject\t%u\n", tick, sname, oname, ob->GetState());
		for (size_t k = 0; k < sensors.size(); k++) {
			log_brick(log, tick, sname, oname, sensors[k]);
		}
		for (size_t k = 0; k < controllers.size(); k++) {
			log_brick(log, tick, sname, oname, controllers[k]);
		}
		for (size_t k = 0; k < actuators.size(); k++) {
			log_brick(log, tick, sname, oname, actuators[k]);
		}
	}
	fflush(log);
}
