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
#include <string.h>

#include "DNA_ID.h"
#include "DNA_scene_types.h"

#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BKE_idprop.h"
#include "BKE_scene.h"

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
/* BGE-Remix P9.7: hash collisions (the Remix View's warnings) */

static int hash_collisions = 0;
static char hash_collision_last[128] = "";

void BRMX_hash_collision_note(const char *what)
{
	hash_collisions++;
	if (what) {
		BLI_strncpy(hash_collision_last, what, sizeof(hash_collision_last));
	}
}

int BRMX_hash_collisions(const char **r_last)
{
	if (r_last) {
		*r_last = hash_collision_last;
	}
	return hash_collisions;
}

/* ------------------------------------------------------------------------- */
/* BGE-Remix P9.7: the Remix View's debug options, kept for a game started
 * from Blender */

#define DEBUG_OPTIONS 16
static struct {
	char key[64];
	char value[32];
} debug_options[DEBUG_OPTIONS];
static int debug_option_count = 0;

static void debug_option_send(int i)
{
	const BRMX_Bridge *bridge = BRMX_bridge();
	if (bridge && bridge->get_status() == BRB_STATUS_RUNNING) {
		bridge->set_config(debug_options[i].key, debug_options[i].value);
	}
}

void BRMX_debug_option_set(const char *key, const char *value)
{
	int i;
	if (!key || !value || strncmp(key, "rtx.", 4) != 0) {
		return;
	}
	for (i = 0; i < debug_option_count; i++) {
		if (strcmp(debug_options[i].key, key) == 0) {
			break;
		}
	}
	if (i == debug_option_count) {
		if (debug_option_count == DEBUG_OPTIONS) {
			return;
		}
		debug_option_count++;
		BLI_strncpy(debug_options[i].key, key, sizeof(debug_options[i].key));
	}
	BLI_strncpy(debug_options[i].value, value, sizeof(debug_options[i].value));
	debug_option_send(i);
}

int BRMX_debug_options_apply(void)
{
	int i;
	for (i = 0; i < debug_option_count; i++) {
		debug_option_send(i);
	}
	return debug_option_count;
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

/* Remix's GraphicsPreset for ours (the panel's Auto, Low, Medium, High,
 * Ultra; Remix: Ultra 0, High 1, Medium 2, Low 3, Custom 4, Auto 5) */
static const char *preset_values[5] = {"5", "3", "2", "1", "0"};

/* BGE-Remix P9.2b: Remix's preset Custom is on because the scene sets its own
 * quality options (BRMX_scene_options_apply) */
static bool custom_preset_sent = false;

int BRMX_quality_apply(const BRMX_Quality *q)
{
	/* Remix's enums (rtx_options.h, rtx_dlss.h): GraphicsPreset Ultra 0, High 1,
	 * Medium 2, Low 3, Custom 4, Auto 5; DLSSProfile UltraPerf 0, MaxPerf 1,
	 * Balanced 2, MaxQuality 3, Auto 4, FullResolution 5; DlssPreset Off 0,
	 * On 1, Custom 2. A graphics preset resets the DLSS level unless the DLSS
	 * preset is Custom (P4 B4.6). */
	static const char *profiles[6] = {"4", "0", "1", "2", "3", "5"};
	const BRMX_Bridge *bridge = BRMX_bridge();
	int n = 0;
	if (!bridge || !q) {
		return 0;
	}
	if (q->preset != sent.preset) {
		bridge->set_config("rtx.graphicsPreset", preset_values[q->preset]);
		/* (a preset replaces a Custom the scene's own options asked for) */
		custom_preset_sent = false;
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

/* ------------------------------------------------------------------------- */
/* The curated settings (P9.1) */

typedef struct CuratedOption {
	const char *prop;   /* the scene's ID property (Render tab, properties_game.py) */
	const char *key;    /* Remix's option */
	char kind;          /* 'b' bool, 'i' int, 'f' float */
	const char *def;    /* Remix's default (rtx.conf may differ: only sent once set) */
	bool preset_quality; /* one a graphics preset sets (P9.2b: needs preset Custom) */
} CuratedOption;

static const CuratedOption curated[] = {
	/* upscaling */
	{"bge_rtx_frame_generation", "rtx.dlfg.enable", 'b', "True"},
	{"bge_rtx_reflex", "rtx.reflexMode", 'i', "1"},
	{"bge_rtx_resolution_scale", "rtx.resolutionScale", 'f', "0.75", true},
	/* quality */
	{"bge_rtx_min_bounces", "rtx.pathMinBounces", 'i', "1", true},
	{"bge_rtx_max_bounces", "rtx.pathMaxBounces", 'i', "4", true},
	{"bge_rtx_secondary_bounces", "rtx.enableSecondaryBounces", 'b', "True", true},
	{"bge_rtx_russian_roulette", "rtx.enableRussianRoulette", 'b', "True", true},
	{"bge_rtx_firefly_threshold", "rtx.fireflyFilteringLuminanceThreshold", 'f', "1000", true},
	/* lighting */
	{"bge_rtx_emissive_intensity", "rtx.emissiveIntensity", 'f', "1"},
	{"bge_rtx_anticull_lights", "rtx.antiCulling.light.enable", 'b', "False"},
	{"bge_rtx_anticull_objects", "rtx.antiCulling.object.enable", 'b', "False"},
	/* effects */
	{"bge_rtx_chromatic_aberration", "rtx.postfx.enableChromaticAberration", 'b', "True"},
	{"bge_rtx_particles", "rtx.particles.enable", 'b', "True"},
	/* picture */
	{"bge_rtx_tonemapper", "rtx.tonemappingMode", 'i', "1"},
	{"bge_rtx_aces_global", "rtx.tonemap.finalizeWithACES", 'b', "False"},
	{"bge_rtx_aces_local", "rtx.localtonemap.finalizeWithACES", 'b', "True"},
	{"bge_rtx_local_exposure", "rtx.localtonemap.exposure", 'f', "0.75"},
	{"bge_rtx_dynamic_range", "rtx.tonemap.dynamicRange", 'f', "15"},
};
#define CURATED_COUNT ((int)(sizeof(curated) / sizeof(curated[0])))

/* BGE-Remix P10.12: the quality settings (P5.1, BRMX_quality_apply), with the
 * values BGE-Remix starts with */
static const CuratedOption quality_options[] = {
	{NULL, "rtx.graphicsPreset", 'i', "5"},
	{NULL, "rtx.dlssPreset", 'i', "1"},
	{NULL, "rtx.qualityDLSS", 'i', "4"},
	{NULL, "rtx.enableRayReconstruction", 'b', "True"},
	{NULL, "rtx.volumetrics.enable", 'b', "True"},
};
#define QUALITY_OPTION_COUNT ((int)(sizeof(quality_options) / sizeof(quality_options[0])))

int BRMX_setting_get(int index, const char **r_key, char *r_kind, const char **r_default)
{
	const CuratedOption *o;
	if (index < 0 || index >= CURATED_COUNT + QUALITY_OPTION_COUNT) {
		return 0;
	}
	o = index < CURATED_COUNT ? &curated[index] : &quality_options[index - CURATED_COUNT];
	*r_key = o->key;
	*r_kind = o->kind;
	*r_default = o->def;
	return 1;
}

/* what was sent ("" = nothing yet: Remix has its default or rtx.conf's) */
static char curated_sent[CURATED_COUNT][32];

static bool curated_value(IDProperty *props, const CuratedOption *o, char *out, size_t size)
{
	IDProperty *p = props ? IDP_GetPropertyFromGroup(props, o->prop) : NULL;
	double v;
	if (!p) {
		return false;
	}
	switch (p->type) {
		case IDP_INT: v = IDP_Int(p); break;
		case IDP_FLOAT: v = IDP_Float(p); break;
		case IDP_DOUBLE: v = IDP_Double(p); break;
		default: return false;
	}
	if (o->kind == 'b') {
		snprintf(out, size, "%s", v != 0.0 ? "True" : "False");
	}
	else if (o->kind == 'i') {
		snprintf(out, size, "%d", (int)v);
	}
	else {
		snprintf(out, size, "%.6g", v);
	}
	return true;
}

/* BGE-Remix P9.2: stops Remix's exposure gets on top of the scene's own:
 * 0.3 for the RTX Remix pipeline's ACES (P9's calibration run, RTX 3080: the
 * RTX Remix materials 9 of 10 within 10 of the fallback, from 5). It goes
 * into the one exposure Remix is sent (apply_exposure), so a change of the
 * scene's exposure keeps it */
float BRMX_scene_exposure_offset(const Scene *scene)
{
	return (scene && BKE_scene_game_rtx_pipeline(scene) &&
	        BKE_scene_game_tonemap(scene) == BKE_SCENE_GAME_TONEMAP_ACES) ? 0.3f : 0.0f;
}

int BRMX_scene_options_apply(const Scene *scene)
{
	const BRMX_Bridge *bridge = BRMX_bridge();
	IDProperty *props = scene ? scene->id.properties : NULL;
	int n = 0, i;
	char value[32];
	/* the RTX Remix pipeline's ACES: Remix's global tone mapper finished with
	 * ACES (the fallback draws the same curve; its exposure: see
	 * BRMX_scene_exposure_offset); the curated settings below may still
	 * change it */
	const bool aces = scene && BKE_scene_game_rtx_pipeline(scene) &&
	                  BKE_scene_game_tonemap(scene) == BKE_SCENE_GAME_TONEMAP_ACES;
	if (!bridge) {
		return 0;
	}
	/* BGE-Remix P9.2b: a graphics preset other than Custom keeps its own
	 * quality values (Remix's Auto: Medium on an RTX 3080), so a scene that
	 * sets its own quality options gets Custom first (Remix then moves the
	 * preset's values to its user layer and keeps ours); without them again,
	 * the scene's preset comes back */
	bool own_quality = false;
	for (i = 0; i < CURATED_COUNT; i++) {
		if (curated[i].preset_quality && curated_value(props, &curated[i], value, sizeof(value))) {
			own_quality = true;
		}
	}
	if (own_quality && !custom_preset_sent) {
		bridge->set_config("rtx.graphicsPreset", "4");
		printf("BGE-Remix: RTX Remix graphics preset Custom (the scene sets its own quality options)\n");
		custom_preset_sent = true;
		/* everything sent again over the preset's values */
		for (i = 0; i < CURATED_COUNT; i++) {
			if (curated[i].preset_quality) {
				curated_sent[i][0] = '\0';
			}
		}
		n++;
	}
	else if (!own_quality && custom_preset_sent) {
		BRMX_Quality q;
		BRMX_quality_from_scene(scene, &q);
		bridge->set_config("rtx.graphicsPreset", preset_values[q.preset]);
		sent.preset = q.preset;
		custom_preset_sent = false;
		n++;
	}
	for (i = 0; i < CURATED_COUNT; i++) {
		const CuratedOption *o = &curated[i];
		bool have = curated_value(props, o, value, sizeof(value));
		if (!have && aces && strcmp(o->key, "rtx.tonemappingMode") == 0) {
			strcpy(value, "0");
			have = true;
		}
		else if (!have && aces && strcmp(o->key, "rtx.tonemap.finalizeWithACES") == 0) {
			strcpy(value, "True");
			have = true;
		}
		if (!have) {
			/* no longer set: back to Remix's default, if something was sent */
			if (curated_sent[i][0] == '\0') {
				continue;
			}
			snprintf(value, sizeof(value), "%s", o->def);
		}
		if (strcmp(value, curated_sent[i]) == 0) {
			continue;
		}
		bridge->set_config(o->key, value);
		printf("BGE-Remix: RTX Remix setting %s = %s\n", o->key, value);
		snprintf(curated_sent[i], sizeof(curated_sent[i]), "%s", have ? value : "");
		n++;
	}
	fflush(stdout);
	return n;
}
