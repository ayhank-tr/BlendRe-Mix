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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file remix_session.c
 *  \ingroup remix
 *
 * One running Remix shared by the game, the 3D view and F12 (BRMX_session.h).
 */

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdio.h>

#include "DNA_ID.h"
#include "DNA_scene_types.h"

#include "BKE_idprop.h"

#include "BRMX_loader.h"
#include "BRMX_session.h"

/* ------------------------------------------------------------------------- */
/* Lock */

static INIT_ONCE lock_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION lock_cs;

static BOOL CALLBACK lock_init(PINIT_ONCE once, PVOID param, PVOID *context)
{
	(void)once;
	(void)param;
	(void)context;
	InitializeCriticalSection(&lock_cs);
	return TRUE;
}

void BRMX_lock(void)
{
	InitOnceExecuteOnce(&lock_once, lock_init, NULL, NULL);
	EnterCriticalSection(&lock_cs);
}

void BRMX_unlock(void)
{
	LeaveCriticalSection(&lock_cs);
}

/* ------------------------------------------------------------------------- */
/* Owner */

static struct {
	int kind;
	void *owner;
	BRMX_LostFn lost;
} owner = {BRMX_OWNER_NONE, NULL, NULL};

int BRMX_owner_take(int kind, void *who, BRMX_LostFn lost)
{
	const BRMX_Bridge *bridge = BRMX_bridge();
	if (owner.owner == who) {
		owner.kind = kind;
		owner.lost = lost;
		return 0;
	}
	if (owner.owner) {
		BRMX_LostFn prev_lost = owner.lost;
		void *prev = owner.owner;
		printf("BGE-Remix: RTX Remix goes from the %s to the %s.\n",
		       owner.kind == BRMX_OWNER_GAME ? "game" : owner.kind == BRMX_OWNER_VIEW ? "3D view" : "render",
		       kind == BRMX_OWNER_GAME ? "game" : kind == BRMX_OWNER_VIEW ? "3D view" : "render");
		fflush(stdout);
		if (bridge && bridge->get_status() == BRB_STATUS_RUNNING) {
			bridge->scene_clear();
		}
		owner.owner = NULL;
		if (prev_lost) {
			prev_lost(prev);
		}
	}
	owner.kind = kind;
	owner.owner = who;
	owner.lost = lost;
	return 1;
}

void BRMX_owner_release(void *who)
{
	if (who && owner.owner == who) {
		owner.kind = BRMX_OWNER_NONE;
		owner.owner = NULL;
		owner.lost = NULL;
	}
}

int BRMX_owner_kind(void)
{
	return owner.kind;
}

void *BRMX_owner(void)
{
	return owner.owner;
}

/* ------------------------------------------------------------------------- */
/* Quality settings (P5.1) */

static int idprop_int(IDProperty *group, const char *name, int def)
{
	IDProperty *p;
	if (!group) {
		return def;
	}
	p = IDP_GetPropertyFromGroup(group, name);
	if (!p) {
		return def;
	}
	switch (p->type) {
		case IDP_INT: return IDP_Int(p);
		case IDP_FLOAT: return (int)IDP_Float(p);
		case IDP_DOUBLE: return (int)IDP_Double(p);
		default: return def;
	}
}

void BRMX_quality_default(BRMX_Quality *r)
{
	r->preset = 0;
	r->dlss = 0;
	r->ray_reconstruction = 1;
	r->volumetrics = 1;
}

void BRMX_quality_from_scene(const Scene *scene, BRMX_Quality *r)
{
	IDProperty *props = scene ? scene->id.properties : NULL;
	BRMX_quality_default(r);
	r->preset = idprop_int(props, "bge_remix_preset", 0);
	r->dlss = idprop_int(props, "bge_remix_dlss", 0);
	r->ray_reconstruction = idprop_int(props, "bge_remix_rr", 1) != 0;
	r->volumetrics = idprop_int(props, "bge_remix_volumetrics", 1) != 0;
	if (r->preset < 0 || r->preset > 4) {
		r->preset = 0;
	}
	if (r->dlss < 0 || r->dlss > 5) {
		r->dlss = 0;
	}
}

/* what Remix has (its defaults until something was sent) */
static BRMX_Quality sent = {0, 0, 1, 1};

int BRMX_quality_apply(const BRMX_Quality *q)
{
	/* Remix's enums (rtx_options.h, rtx_dlss.h): GraphicsPreset Ultra 0, High 1,
	 * Medium 2, Low 3, Custom 4, Auto 5; DLSSProfile UltraPerf 0, MaxPerf 1,
	 * Balanced 2, MaxQuality 3, Auto 4, FullResolution 5; DlssPreset Off 0,
	 * On 1, Custom 2. A graphics preset resets the DLSS level unless the DLSS
	 * preset is Custom (P4 B4.6). */
	static const char *presets[5] = {"5", "3", "2", "1", "0"};
	static const char *profiles[6] = {"4", "0", "1", "2", "3", "5"};
	const BRMX_Bridge *bridge = BRMX_bridge();
	int n = 0;
	if (!bridge || !q) {
		return 0;
	}
	if (q->preset != sent.preset) {
		bridge->set_config("rtx.graphicsPreset", presets[q->preset]);
		n++;
	}
	if (q->dlss != sent.dlss || (q->preset != sent.preset && q->dlss != 0)) {
		bridge->set_config("rtx.dlssPreset", q->dlss ? "2" : "1");
		bridge->set_config("rtx.qualityDLSS", profiles[q->dlss]);
		n++;
	}
	if (q->ray_reconstruction != sent.ray_reconstruction) {
		bridge->set_config("rtx.enableRayReconstruction", q->ray_reconstruction ? "True" : "False");
		n++;
	}
	if (q->volumetrics != sent.volumetrics) {
		bridge->set_config("rtx.volumetrics.enable", q->volumetrics ? "True" : "False");
		n++;
	}
	sent = *q;
	if (n) {
		printf("BGE-Remix: quality preset %d, DLSS %d, ray reconstruction %s, volumetrics %s.\n", q->preset, q->dlss,
		       q->ray_reconstruction ? "on" : "off", q->volumetrics ? "on" : "off");
		fflush(stdout);
	}
	return n;
}
