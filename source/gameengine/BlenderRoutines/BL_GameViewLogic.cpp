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
	#include "DNA_actuator_types.h"
	#include "DNA_controller_types.h"
	#include "DNA_object_types.h"
	#include "DNA_sensor_types.h"

	#include "BLI_listbase.h"
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
 * P7 did). Rows as P7's: the main avatars, then the state groups, sensors,
 * controllers, actuators, properties and the rest above them. */
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
			if (STRPREFIX(e.kind, "state:")) row = 1;
			else if (STRPREFIX(e.kind, "sensor:")) row = 2;
			else if (STRPREFIX(e.kind, "controller:")) row = 3;
			else if (STRPREFIX(e.kind, "actuator:")) row = 4;
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
