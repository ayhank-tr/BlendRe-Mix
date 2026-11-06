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

/** \file gameengine/BlenderRoutines/BL_GameViewLogic.cpp
 *  \ingroup blroutines
 *
 * BGE-Remix P10.11: the logic layer's states. Each object's bricks are
 * grouped by state: one "state:GROUP" element per state its logic uses
 * ("State 2"), its bricks laid out in a column under it (a controller in its
 * first state; a sensor or actuator with the first controller of its object
 * linked to it), and a dashed line from each group to every controller in
 * that state (a controller can be in several). The states the object starts
 * in are marked (Green, active); live ones come from Run mode. Each State
 * actuator is drawn as arrows from the groups of the controllers driving it
 * to the groups of the states it enters.
 */

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "BL_GameViewBuild.h"

extern "C" {
	#include "DNA_action_types.h"
	#include "DNA_actuator_types.h"
	#include "DNA_anim_types.h"
	#include "DNA_controller_types.h"
	#include "DNA_key_types.h"
	#include "DNA_mesh_types.h"
	#include "DNA_object_types.h"
	#include "DNA_property_types.h"
	#include "DNA_sensor_types.h"

	#include "BKE_main.h"

	#include "BLI_fileops.h"
	#include "BLI_listbase.h"
	#include "BLI_path_util.h"
	#include "BLI_string.h"
	#include "BLI_utildefines.h"
}

#define GV_STATES 30
#define GV_NO_STATE 30   /* the column of bricks in no state (unlinked) */

static int lowest_state(unsigned int mask)
{
	for (int b = 0; b < GV_STATES; b++) {
		if (mask & (1u << b)) {
			return b;
		}
	}
	return -1;
}

static std::string brick_key(const char *what, const char *owner, const char *name)
{
	return std::string(what) + "|" + owner + "|" + name;
}

void GVBuilder::LogicStates()
{
	/* the bricks' elements (Logic made them) by kind, object and name */
	std::map<std::string, int> brick_el;
	for (size_t i = 0; i < elements.size(); i++) {
		const GVElement &e = elements[i];
		const char *colon = strchr(e.kind, ':');
		if (e.layer != GV_LAYER_LOGIC || !e.owner[0] || !colon) {
			continue;
		}
		brick_el[brick_key(std::string(e.kind, colon - e.kind).c_str(), e.owner, e.name)] = (int)i;
	}
	for (Object *ob : m_objects) {
		if (!ob->controllers.first) {
			continue;
		}
		const char *obname = ob->id.name + 2;
		std::map<std::string, int>::const_iterator it;

		/* the states: used by its controllers, entered by its State
		 * actuators, and those it starts in (as the converter: the initial
		 * state, else the saved one) */
		unsigned int used = 0, entered = 0;
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			used |= c->state_mask;
		}
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type == ACT_STATE && a->data && ((bStateActuator *)a->data)->type != ACT_STATE_REMOVE) {
				entered |= (unsigned int)((bStateActuator *)a->data)->mask;
			}
		}
		const unsigned int start = ob->init_state ? ob->init_state : (ob->state ? ob->state : 1u);
		const unsigned int all = (used | entered | start) & ((1u << GV_STATES) - 1);
		int group[GV_STATES];
		bool any = false;
		for (int b = 0; b < GV_STATES; b++) {
			group[b] = -1;
			if (!(all & (1u << b))) {
				continue;
			}
			char name[32], info[96];
			BLI_snprintf(name, sizeof(name), "State %d", b + 1);
			if (start & (1u << b)) {
				BLI_strncpy(info, "the object starts in it", sizeof(info));
			}
			else if (entered & (1u << b)) {
				BLI_strncpy(info, "entered by a State actuator", sizeof(info));
			}
			else {
				BLI_strncpy(info, "never entered", sizeof(info));
			}
			const int el = Add("state:GROUP", name, ob, info);
			if (start & (1u << b)) {
				elements[el].flag |= GV_EL_ACTIVE;   /* (Green: the object is in it) */
			}
			else {
				elements[el].state = GV_STATE_WHITE;
			}
			m_elementState[el] = b;
			group[b] = el;
			any = true;
		}
		if (!any) {
			continue;
		}

		/* controllers: in the column of their first state, a line from each
		 * of their states' groups */
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			if ((it = brick_el.find(brick_key("controller", obname, c->name))) == brick_el.end()) {
				continue;
			}
			for (int b = 0; b < GV_STATES; b++) {
				if ((c->state_mask & (1u << b)) && group[b] >= 0) {
					const int sh = Shape(GV_SHAPE_LINE, GV_LAYER_LOGIC, group[b], it->second);
					shapes[sh].dashed = 1;
				}
			}
			m_elementState[it->second] = lowest_state(c->state_mask);
		}
		/* sensors: with the first state of their object's controllers they
		 * feed (none: the no-state column) */
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) {
			if ((it = brick_el.find(brick_key("sensor", obname, s->name))) == brick_el.end()) {
				continue;
			}
			int lowest = GV_STATES;
			for (int i = 0; i < s->totlinks; i++) {
				bController *c = s->links ? s->links[i] : NULL;
				if (c && BLI_findindex(&ob->controllers, c) >= 0) {
					const int b = lowest_state(c->state_mask);
					if (b >= 0) lowest = std::min(lowest, b);
				}
			}
			m_elementState[it->second] = lowest < GV_STATES ? lowest : -1;
		}
		/* actuators: with the first state of their object's controllers that
		 * drive them; State actuators also as arrows between the groups */
		std::set<std::pair<int, int> > arrows;
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if ((it = brick_el.find(brick_key("actuator", obname, a->name))) == brick_el.end()) {
				continue;
			}
			unsigned int from = 0;
			for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
				for (int i = 0; i < c->totlinks; i++) {
					if (c->links && c->links[i] == a) {
						from |= c->state_mask;
					}
				}
			}
			m_elementState[it->second] = lowest_state(from);
			if (a->type != ACT_STATE || !a->data) {
				continue;
			}
			const bStateActuator *st = (const bStateActuator *)a->data;
			if (st->type == ACT_STATE_REMOVE) {
				continue;  /* (it only leaves states) */
			}
			for (int to = 0; to < GV_STATES; to++) {
				if (!(st->mask & (1 << to)) || group[to] < 0) {
					continue;
				}
				if (!from) {
					/* driven by no controller of its object: from the actuator */
					if (arrows.insert(std::make_pair(it->second, group[to])).second) {
						const int sh = Shape(GV_SHAPE_ARC, GV_LAYER_LOGIC, it->second, group[to]);
						shapes[sh].p[7] = 1.0f;
					}
					continue;
				}
				for (int b = 0; b < GV_STATES; b++) {
					if (!(from & (1u << b)) || b == to || group[b] < 0) {
						continue;
					}
					if (arrows.insert(std::make_pair(group[b], group[to])).second) {
						const int sh = Shape(GV_SHAPE_ARC, GV_LAYER_LOGIC, group[b], group[to]);
						shapes[sh].p[7] = 1.0f;  /* an arrowhead at the state entered */
					}
				}
			}
		}
	}
}

/* the owner's elements laid out with its bricks in columns, one per state
 * (P10.11); false when the owner has no state groups (Layout places them as
 * P7 did). Rows as P7's: the main avatars, then sensors, controllers and
 * actuators, each column topped by its state's group (the arrows between
 * the groups bow above everything), then properties and the rest. */
bool GVBuilder::LayoutStates(const std::vector<int> &els, float z0)
{
	bool groups = false;
	for (int i : els) {
		groups |= STRPREFIX(elements[i].kind, "state:");
	}
	if (!groups) {
		return false;
	}
	std::map<int, std::map<int, std::vector<int> > > columns;  /* state -> row -> elements */
	std::map<int, std::vector<int> > plain;                    /* row -> elements */
	for (int i : els) {
		const GVElement &e = elements[i];
		int row = 0;
		if (e.flag & GV_EL_SMALL) {
			if (STRPREFIX(e.kind, "sensor:")) row = 1;
			else if (STRPREFIX(e.kind, "controller:")) row = 2;
			else if (STRPREFIX(e.kind, "actuator:")) row = 3;
			else if (STRPREFIX(e.kind, "state:")) row = 4;
			else if (STRPREFIX(e.kind, "property:")) row = 5;
			else row = 6;
		}
		if (row >= 1 && row <= 4) {
			std::map<int, int>::const_iterator st = m_elementState.find(i);
			const int col = (st != m_elementState.end() && st->second >= 0) ? st->second : GV_NO_STATE;
			columns[col][row].push_back(i);
		}
		else {
			plain[row].push_back(i);
		}
	}
	const float spacing = 0.36f, gap = 0.36f, row_step = 0.42f;
	const float z_rows = z0 + (plain.count(0) ? 0.2f : 0.0f);
	/* the columns side by side, centred over the object */
	std::map<int, float> width;
	float total = 0.0f;
	for (auto &c : columns) {
		size_t n = 1;
		for (auto &r : c.second) {
			n = std::max(n, r.second.size());
		}
		width[c.first] = n * spacing;
		total += width[c.first] + gap;
	}
	total -= gap;
	float x = -0.5f * total;
	for (auto &c : columns) {
		const float centre = x + 0.5f * width[c.first];
		for (auto &r : c.second) {
			const int n = (int)r.second.size();
			for (int k = 0; k < n; k++) {
				GVElement &e = elements[r.second[k]];
				e.offset[0] = centre + (k - (n - 1) * 0.5f) * spacing;
				e.offset[1] = 0.0f;
				e.offset[2] = z_rows + row_step * r.first;
				e.flag |= GV_EL_VIEW_ALIGNED;
			}
		}
		x += width[c.first] + gap;
	}
	for (auto &r : plain) {
		const float step = r.first == 0 ? 0.72f : spacing;
		const int n = (int)r.second.size();
		for (int k = 0; k < n; k++) {
			GVElement &e = elements[r.second[k]];
			e.offset[0] = (k - (n - 1) * 0.5f) * step;
			e.offset[1] = 0.0f;
			e.offset[2] = r.first == 0 ? z0 : z_rows + row_step * r.first;
			e.flag |= GV_EL_VIEW_ALIGNED;
		}
	}
	return true;
}

/* ------------------------------------------------------------------------- */
/* BGE-Remix P10.13: the bricks' limits the logic layer flags */

static std::string check_id(const char *object, const char *kind, const char *name)
{
	/* (BL_GameViewLayers.cpp's brick_id: "object/kind/name") */
	return std::string(object) + "/" + kind + "/" + name;
}

/* B2: the file a Start Game actuator opens, looked for as the player does
 * (relative to the game's file, then with "//" in front) */
static bool game_file_found(const char *name, const char *blend)
{
	char path[FILE_MAX];
	if (!name || !name[0]) {
		return false;
	}
	BLI_strncpy(path, name, sizeof(path));
	BLI_path_abs(path, blend);
	if (BLI_exists(path)) {
		return true;
	}
	BLI_snprintf(path, sizeof(path), "//%s", name);
	BLI_path_abs(path, blend);
	return BLI_exists(path) != 0;
}

static bool is_expr_word(const std::string &w)
{
	std::string l = w;
	for (char &ch : l) ch = (char)tolower((unsigned char)ch);
	return l == "and" || l == "or" || l == "not" || l == "true" || l == "false" || l == "if" || l == "else";
}

/* A16: whether an action animates shape keys at all, and one of key's */
static bool action_has_key_curves(const bAction *act)
{
	for (const FCurve *fcu = (const FCurve *)act->curves.first; fcu; fcu = fcu->next) {
		if (fcu->rna_path && STRPREFIX(fcu->rna_path, "key_blocks[")) return true;
	}
	return false;
}

static bool action_moves_a_key(const bAction *act, const Key *key)
{
	if (!key) return false;
	for (const FCurve *fcu = (const FCurve *)act->curves.first; fcu; fcu = fcu->next) {
		if (!fcu->rna_path) continue;
		for (const KeyBlock *kb = (const KeyBlock *)key->block.first; kb; kb = kb->next) {
			char want[128];
			BLI_snprintf(want, sizeof(want), "key_blocks[\"%s\"]", kb->name);
			if (strstr(fcu->rna_path, want)) {
				return true;
			}
		}
	}
	return false;
}

void GVBuilder::LogicChecks()
{
	for (Object *ob : m_objects) {
		const char *obname = ob->id.name + 2;
		std::map<std::string, int>::const_iterator it;
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) {
			if ((s->flag & SENS_DEACTIVATE) || !s->data ||
			    (it = m_brickElement.find(check_id(obname, "sensor", s->name))) == m_brickElement.end())
			{
				continue;
			}
			/* B2 (A14): an Actuator sensor watching no actuator of its object */
			if (s->type == SENS_ACTUATOR) {
				const char *act = ((bActuatorSensor *)s->data)->name;
				if (!BLI_findstring(&ob->actuators, act, offsetof(bActuator, name))) {
					Warn(it->second, "LOG_NO_ACTUATOR", GV_WARN_RED,
					     act[0] ? (std::string("watches the actuator \"") + act + "\", which " + obname +
					               " does not have: it never fires").c_str() :
					              "watches no actuator: it never fires");
				}
			}
			/* A11: Near and Radar sensors only see Actor objects */
			if ((s->type == SENS_NEAR || s->type == SENS_RADAR)) {
				bool other_actor = false;
				for (Object *o : m_objects) {
					other_actor |= o != ob && (o->gameflag & OB_ACTOR);
				}
				if (!other_actor) {
					Warn(it->second, "LOG_SEES_ACTORS", GV_WARN_NOTE,
					     "sees only objects with Actor on (Physics), and no other object here has it");
				}
			}
		}
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			if (c->type != CONT_EXPRESSION || (c->flag & CONT_DEACTIVATE) || !c->data ||
			    (it = m_brickElement.find(check_id(obname, "controller", c->name))) == m_brickElement.end())
			{
				continue;
			}
			const char *expr = ((bExpressionCont *)c->data)->str;
			/* A9: the field holds 127 characters: a full one was likely cut */
			if (strlen(expr) >= sizeof(((bExpressionCont *)c->data)->str) - 1) {
				Warn(it->second, "LOG_EXPR_FULL", GV_WARN_NOTE,
				     "the expression fills its 127 characters: a longer one was cut (use a Python controller)");
			}
			/* A10: names the expression reads: a linked sensor wins over a
			 * property of the same name; a sensor not linked to it is not
			 * seen at all (SCA_ExpressionController::FindIdentifier) */
			std::set<std::string> said;
			const std::vector<bSensor *> &linked = SensorsOf(c);
			for (const char *p = expr; *p;) {
				if (!(isalpha((unsigned char)*p) || *p == '_')) {
					if (*p == '"') {  /* (skip a string) */
						const char *q = strchr(p + 1, '"');
						p = q ? q + 1 : p + strlen(p);
					}
					else {
						p++;
					}
					continue;
				}
				const char *q = p;
				while (isalnum((unsigned char)*q) || *q == '_') q++;
				const std::string word(p, q - p);
				p = q;
				if (is_expr_word(word) || !said.insert(word).second) continue;
				bool is_linked = false, is_sensor = false, is_prop = false;
				for (bSensor *s : linked) is_linked |= word == s->name;
				is_sensor = BLI_findstring(&ob->sensors, word.c_str(), offsetof(bSensor, name)) != NULL;
				is_prop = BLI_findstring(&ob->prop, word.c_str(), offsetof(bProperty, name)) != NULL;
				if (is_linked && is_prop) {
					Warn(it->second, "LOG_EXPR_NAME_CLASH", GV_WARN_NOTE,
					     ("\"" + word + "\" is a linked sensor and a property: the expression reads the sensor")
					     .c_str());
				}
				else if (is_sensor && !is_linked) {
					Warn(it->second, "LOG_EXPR_NAME_CLASH", GV_WARN_NOTE,
					     ("\"" + word + "\" is a sensor not linked to this controller: the expression " +
					      (is_prop ? "reads the property of that name" : "cannot read it")).c_str());
				}
			}
		}
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if ((a->flag & ACT_DEACTIVATE) || !a->data ||
			    (it = m_brickElement.find(check_id(obname, "actuator", a->name))) == m_brickElement.end())
			{
				continue;
			}
			/* B2: a Start Game file that is not there */
			if (a->type == ACT_GAME) {
				const bGameActuator *ga = (const bGameActuator *)a->data;
				if ((ga->type == ACT_GAME_LOAD || ga->type == ACT_GAME_START) &&
				    !game_file_found(ga->filename, m_main->name))
				{
					Warn(it->second, "LOG_NO_FILE", GV_WARN_RED,
					     (std::string("starts the game file \"") + ga->filename + "\", which is not there").c_str());
				}
			}
			/* A16: a shape key action (a Shape Action actuator of old files, or an
			 * Action actuator playing one) moving none of the mesh's shape keys */
			if (a->type == ACT_SHAPEACTION || a->type == ACT_ACTION) {
				const bActionActuator *aa = (const bActionActuator *)a->data;
				const Key *key = (ob->type == OB_MESH && ob->data) ? ((const Mesh *)ob->data)->key : NULL;
				const bool shape = aa->act && (a->type == ACT_SHAPEACTION || action_has_key_curves(aa->act));
				if (shape && !action_moves_a_key(aa->act, key)) {
					Warn(it->second, "LOG_SHAPE_NO_KEYS", GV_WARN_NOTE,
					     key ? "its action moves none of the mesh's shape keys: it plays and changes nothing" :
					           "the mesh has no shape keys: it plays and changes nothing");
				}
			}
		}
	}
}
