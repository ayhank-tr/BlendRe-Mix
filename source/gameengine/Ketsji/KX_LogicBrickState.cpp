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
 * BGE-Remix P10.10: a logic brick's live state (KX_LogicBrickState.h), in
 * Ayhan's colours. It only reads: the game runs exactly as it would without
 * it. What each brick counts as:
 *
 *   every brick  Red     the Remix View's build found it unavailable, it has
 *                        no object, or the converter found its target or
 *                        file missing (SCA_ILogicBrick::IsMissing)
 *   sensor       Green   it sent a positive pulse this tick, or it is
 *                        positive (in an active state)
 *                Black   no controller of an active state listens to it
 *                        (the engine does not run it), or it is suspended
 *                Yellow  a Delay counting its delay, a Timer not yet due
 *                White   ready, not positive
 *                Red     also: an Actuator sensor watching no actuator, a
 *                        Property sensor watching a property its object
 *                        does not have
 *   controller   Green   it ran this tick with a positive result (a Python
 *                        or Expression controller: it ran)
 *                Black   its states are not ones the object is in
 *                White   ready
 *                Red     also: a Python controller with no script, or one
 *                        that failed to compile or import
 *   actuator     Green   it got a positive pulse this tick (the RTX Remix
 *                        actuator: also the tick it sent its setting)
 *                Yellow  an Action actuator asked to play while another
 *                        action holds its layer; an RTX Remix actuator
 *                        waiting for Remix to start
 *                Blue    still at work after its pulse (motion, servo
 *                        motion, an action or sound playing, a camera
 *                        following, a constraint, a steering, ...)
 *                Black   no controller of an active state drives it
 *                White   ready, idle
 *                Red     also: a Track To, Camera, Parent or Property
 *                        (copy) actuator whose target is gone (ended in the
 *                        game); an RTX Remix actuator that cannot reach
 *                        Remix (the Classic pipeline, no setting)
 */

#include "KX_LogicBrickState.h"
#include "KX_LogicBrickLog.h"

#include "SCA_ILogicBrick.h"
#include "SCA_ISensor.h"
#include "SCA_IController.h"
#include "SCA_IActuator.h"
#include "SCA_LogicManager.h"

/* sensors */
#include "SCA_ActuatorSensor.h"
#include "SCA_AlwaysSensor.h"
#include "SCA_DelaySensor.h"
#include "SCA_JoystickSensor.h"
#include "SCA_KeyboardSensor.h"
#include "SCA_MouseSensor.h"
#include "SCA_PropertySensor.h"
#include "SCA_RandomSensor.h"
#include "SCA_TimerSensor.h"
#include "KX_ArmatureSensor.h"
#include "KX_NearSensor.h"
#include "KX_NetworkMessageSensor.h"
#include "KX_RadarSensor.h"
#include "KX_RaySensor.h"
#include "KX_TouchSensor.h"

/* controllers */
#include "SCA_ANDController.h"
#include "SCA_ExpressionController.h"
#include "SCA_NANDController.h"
#include "SCA_NORController.h"
#include "SCA_ORController.h"
#include "SCA_PythonController.h"
#include "SCA_XNORController.h"
#include "SCA_XORController.h"

/* actuators */
#include "BL_ActionActuator.h"
#include "KX_CameraActuator.h"
#include "KX_ParentActuator.h"
#include "KX_RemixActuator.h"
#include "KX_TrackToActuator.h"
#include "SCA_PropertyActuator.h"

#include <vector>

/* ------------------------------------------------------------------------- */
/* what a brick is (the Remix View's kinds: "sensor:DELAY", ...) */

const char *KX_LogicBrickKind(SCA_ILogicBrick *brick)
{
	if (brick == NULL) {
		return "";
	}
	if (SCA_ISensor *s = dynamic_cast<SCA_ISensor *>(brick)) {
		/* (the most derived classes first) */
		if (dynamic_cast<KX_RadarSensor *>(s)) return "sensor:RADAR";
		if (dynamic_cast<KX_NearSensor *>(s)) return "sensor:NEAR";
		if (dynamic_cast<KX_TouchSensor *>(s)) return "sensor:COLLISION";
		if (dynamic_cast<SCA_AlwaysSensor *>(s)) return "sensor:ALWAYS";
		if (dynamic_cast<SCA_DelaySensor *>(s)) return "sensor:DELAY";
		if (dynamic_cast<SCA_TimerSensor *>(s)) return "sensor:TIMER";
		if (dynamic_cast<SCA_KeyboardSensor *>(s)) return "sensor:KEYBOARD";
		if (dynamic_cast<SCA_MouseSensor *>(s)) return "sensor:MOUSE";  /* (also Mouse over) */
		if (dynamic_cast<SCA_PropertySensor *>(s)) return "sensor:PROPERTY";
		if (dynamic_cast<SCA_RandomSensor *>(s)) return "sensor:RANDOM";
		if (dynamic_cast<SCA_JoystickSensor *>(s)) return "sensor:JOYSTICK";
		if (dynamic_cast<SCA_ActuatorSensor *>(s)) return "sensor:ACTUATOR";
		if (dynamic_cast<KX_NetworkMessageSensor *>(s)) return "sensor:MESSAGE";
		if (dynamic_cast<KX_RaySensor *>(s)) return "sensor:RAY";
		if (dynamic_cast<KX_ArmatureSensor *>(s)) return "sensor:ARMATURE";
		return "sensor:OTHER";
	}
	if (SCA_IController *c = dynamic_cast<SCA_IController *>(brick)) {
		if (dynamic_cast<SCA_ANDController *>(c)) return "controller:LOGIC_AND";
		if (dynamic_cast<SCA_ORController *>(c)) return "controller:LOGIC_OR";
		if (dynamic_cast<SCA_NANDController *>(c)) return "controller:LOGIC_NAND";
		if (dynamic_cast<SCA_NORController *>(c)) return "controller:LOGIC_NOR";
		if (dynamic_cast<SCA_XORController *>(c)) return "controller:LOGIC_XOR";
		if (dynamic_cast<SCA_XNORController *>(c)) return "controller:LOGIC_XNOR";
		if (dynamic_cast<SCA_ExpressionController *>(c)) return "controller:EXPRESSION";
		if (dynamic_cast<SCA_PythonController *>(c)) return "controller:PYTHON";
		return "controller:OTHER";
	}
	if (SCA_IActuator *a = dynamic_cast<SCA_IActuator *>(brick)) {
		static const struct { SCA_IActuator::KX_ACTUATOR_TYPE type; const char *kind; } kinds[] = {
			{SCA_IActuator::KX_ACT_OBJECT, "actuator:MOTION"},
			{SCA_IActuator::KX_ACT_CAMERA, "actuator:CAMERA"},
			{SCA_IActuator::KX_ACT_SOUND, "actuator:SOUND"},
			{SCA_IActuator::KX_ACT_PROPERTY, "actuator:PROPERTY"},
			{SCA_IActuator::KX_ACT_ADD_OBJECT, "actuator:EDIT_OBJECT"},
			{SCA_IActuator::KX_ACT_END_OBJECT, "actuator:EDIT_OBJECT"},
			{SCA_IActuator::KX_ACT_DYNAMIC, "actuator:EDIT_OBJECT"},
			{SCA_IActuator::KX_ACT_REPLACE_MESH, "actuator:EDIT_OBJECT"},
			{SCA_IActuator::KX_ACT_TRACKTO, "actuator:EDIT_OBJECT"},
			{SCA_IActuator::KX_ACT_CONSTRAINT, "actuator:CONSTRAINT"},
			{SCA_IActuator::KX_ACT_SCENE, "actuator:SCENE"},
			{SCA_IActuator::KX_ACT_RANDOM, "actuator:RANDOM"},
			{SCA_IActuator::KX_ACT_MESSAGE, "actuator:MESSAGE"},
			{SCA_IActuator::KX_ACT_ACTION, "actuator:ACTION"},
			{SCA_IActuator::KX_ACT_GAME, "actuator:GAME"},
			{SCA_IActuator::KX_ACT_VISIBILITY, "actuator:VISIBILITY"},
			{SCA_IActuator::KX_ACT_2DFILTER, "actuator:FILTER_2D"},
			{SCA_IActuator::KX_ACT_PARENT, "actuator:PARENT"},
			{SCA_IActuator::KX_ACT_SHAPEACTION, "actuator:SHAPE_ACTION"},
			{SCA_IActuator::KX_ACT_STATE, "actuator:STATE"},
			{SCA_IActuator::KX_ACT_ARMATURE, "actuator:ARMATURE"},
			{SCA_IActuator::KX_ACT_STEERING, "actuator:STEERING"},
			{SCA_IActuator::KX_ACT_MOUSE, "actuator:MOUSE"},
			{SCA_IActuator::KX_ACT_REMIX, "actuator:REMIX"},
		};
		for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
			if (a->IsType(kinds[i].type)) {
				return kinds[i].kind;
			}
		}
		return "actuator:OTHER";
	}
	return "";
}

const char *KX_LogicBrickStateName(KX_BrickLiveState state)
{
	switch (state) {
		case KX_BRICK_RED: return "RED";
		case KX_BRICK_BLACK: return "BLACK";
		case KX_BRICK_WHITE: return "WHITE";
		case KX_BRICK_GREEN: return "GREEN";
		case KX_BRICK_BLUE: return "BLUE";
		case KX_BRICK_YELLOW: return "YELLOW";
	}
	return "?";
}

/* ------------------------------------------------------------------------- */

static int current_tick(SCA_ILogicBrick *brick)
{
	SCA_LogicManager *lm = brick->GetLogicManager();
	return lm ? lm->GetTick() : -2;
}

static bool fired_now(SCA_ILogicBrick *brick)
{
	const int tick = current_tick(brick);
	return tick > 0 && brick->GetFiredTick() == tick;
}

static KX_BrickLiveState sensor_state(SCA_ISensor *s)
{
	if (SCA_PropertySensor *ps = dynamic_cast<SCA_PropertySensor *>(s)) {
		if (s->GetParent()->GetProperty(ps->GetPropertyName()) == NULL) {
			return KX_BRICK_RED;  /* its object has no such property (now) */
		}
	}
	if (fired_now(s)) {
		/* (also when its state was left on the same tick) */
		return KX_BRICK_GREEN;
	}
	if (s->IsNoLink() || s->IsSuspended()) {
		/* not run: no controller of an active state listens to it */
		return KX_BRICK_BLACK;
	}
	if (SCA_DelaySensor *ds = dynamic_cast<SCA_DelaySensor *>(s)) {
		if (ds->IsWaiting()) return KX_BRICK_YELLOW;
	}
	else if (SCA_TimerSensor *ts = dynamic_cast<SCA_TimerSensor *>(s)) {
		if (ts->IsWaiting()) return KX_BRICK_YELLOW;
	}
	return s->GetState() ? KX_BRICK_GREEN : KX_BRICK_WHITE;
}

/* what a logic gate sent its actuators on its last run: the gates read
 * their sensors' states, which do not change after the tick's controllers
 * ran (SCA_ANDController::Trigger and the others) */
static bool gate_result(SCA_IController *c, bool *r_known)
{
	std::vector<SCA_ISensor *> &sensors = c->GetLinkedSensors();
	const size_t n = sensors.size();
	size_t positive = 0;
	for (size_t i = 0; i < n; i++) {
		if (sensors[i]->GetState()) positive++;
	}
	*r_known = true;
	if (dynamic_cast<SCA_ANDController *>(c)) return positive == n;
	if (dynamic_cast<SCA_ORController *>(c)) return positive > 0;
	if (dynamic_cast<SCA_NANDController *>(c)) return positive < n;
	if (dynamic_cast<SCA_NORController *>(c)) return positive == 0;
	if (dynamic_cast<SCA_XORController *>(c)) return positive == 1;
	if (dynamic_cast<SCA_XNORController *>(c)) return positive != 1;
	*r_known = false;  /* Python, Expression: it ran */
	return true;
}

static KX_BrickLiveState controller_state(SCA_IController *c)
{
	if (SCA_PythonController *pc = dynamic_cast<SCA_PythonController *>(c)) {
		if (pc->IsBroken()) {
			return KX_BRICK_RED;
		}
	}
	if (fired_now(c)) {
		bool known;
		const bool positive = gate_result(c, &known);
		if (positive || !known) {
			return KX_BRICK_GREEN;
		}
	}
	/* (IsActive: in a state the object is in, SCA_IController::ApplyState) */
	return c->IsActive() ? KX_BRICK_WHITE : KX_BRICK_BLACK;
}

static KX_BrickLiveState actuator_state(SCA_IActuator *a)
{
	if (KX_TrackToActuator *t = dynamic_cast<KX_TrackToActuator *>(a)) {
		if (!t->GetTarget()) return KX_BRICK_RED;
	}
	else if (KX_CameraActuator *ca = dynamic_cast<KX_CameraActuator *>(a)) {
		if (!ca->GetTarget()) return KX_BRICK_RED;
	}
	else if (KX_ParentActuator *pa = dynamic_cast<KX_ParentActuator *>(a)) {
		if (pa->IsMissingParent()) return KX_BRICK_RED;
	}
	else if (SCA_PropertyActuator *pr = dynamic_cast<SCA_PropertyActuator *>(a)) {
		if (pr->IsMissingSource()) return KX_BRICK_RED;
	}
	else if (KX_RemixActuator *ra = dynamic_cast<KX_RemixActuator *>(a)) {
		if (!ra->CanReachRemix()) return KX_BRICK_RED;
	}
	if (fired_now(a)) {
		return KX_BRICK_GREEN;
	}
	/* (IsActive: still in the active list after the tick's update) */
	if (a->IsActive()) {
		if (BL_ActionActuator *aa = dynamic_cast<BL_ActionActuator *>(a)) {
			if (aa->IsWaitingForLayer()) return KX_BRICK_YELLOW;
		}
		else if (KX_RemixActuator *ra = dynamic_cast<KX_RemixActuator *>(a)) {
			if (ra->IsPending()) return KX_BRICK_YELLOW;
		}
		return KX_BRICK_BLUE;
	}
	return a->IsNoLink() ? KX_BRICK_BLACK : KX_BRICK_WHITE;
}

KX_BrickLiveState KX_LogicBrickLiveState(SCA_ILogicBrick *brick, bool built_red)
{
	if (built_red || brick == NULL || brick->GetParent() == NULL || brick->IsMissing())
		return KX_BRICK_RED;
	if (SCA_ISensor *s = dynamic_cast<SCA_ISensor *>(brick))
		return sensor_state(s);
	if (SCA_IController *c = dynamic_cast<SCA_IController *>(brick))
		return controller_state(c);
	if (SCA_IActuator *a = dynamic_cast<SCA_IActuator *>(brick))
		return actuator_state(a);
	return KX_BRICK_WHITE;
}
