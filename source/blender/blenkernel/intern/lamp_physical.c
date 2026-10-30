/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/blenkernel/intern/lamp_physical.c
 *  \ingroup bke
 *
 * BGE-Remix P8.9: the shared light description (BKE_lamp_physical.h).
 */

#include <math.h>

#include "DNA_ID.h"
#include "DNA_lamp_types.h"

#include "BLI_math_base.h"
#include "BLI_utildefines.h"

#include "BKE_idprop.h"
#include "BKE_lamp_physical.h"

static IDProperty *prop(const Lamp *la, const char *name)
{
	return (la && la->id.properties) ? IDP_GetPropertyFromGroup(la->id.properties, name) : NULL;
}

static float prop_float(const Lamp *la, const char *name, float def)
{
	IDProperty *p = prop(la, name);
	if (!p) {
		return def;
	}
	switch (p->type) {
		case IDP_FLOAT: return IDP_Float(p);
		case IDP_DOUBLE: return (float)IDP_Double(p);
		case IDP_INT: return (float)IDP_Int(p);
	}
	return def;
}

bool BKE_lamp_is_physical(const Lamp *la)
{
	return la && ELEM(la->type, LA_LOCAL, LA_SPOT) && prop_float(la, "bge_physical", 0.0f) != 0.0f;
}

void BKE_lamp_physical_init(Lamp *la)
{
	IDPropertyTemplate val = {0};
	if (!la) {
		return;
	}
	if (!la->id.properties) {
		la->id.properties = IDP_New(IDP_GROUP, &val, "LampProperties");
	}
	val.i = 1;
	IDP_ReplaceInGroup(la->id.properties, IDP_New(IDP_INT, &val, "bge_physical"));
}

float BKE_lamp_size(const Lamp *la)
{
	return la ? max_ff(la->area_size, 0.0f) : 0.0f;
}

float BKE_lamp_sun_angle(const Lamp *la)
{
	const float angle = prop_float(la, "bge_sun_angle", BKE_LAMP_SUN_ANGLE_DEFAULT);
	return (angle > 0.0f) ? angle : BKE_LAMP_SUN_ANGLE_DEFAULT;
}

bool BKE_lamp_casts_shadow(const Lamp *la)
{
	if (!la) {
		return false;
	}
	switch (la->type) {
		case LA_SPOT:
			return (la->mode & (LA_SHAD_BUF | LA_SHAD_RAY)) != 0;
		case LA_LOCAL:
		case LA_SUN:
		case LA_AREA:
			return (la->mode & LA_SHAD_RAY) != 0;
	}
	return false;
}

float BKE_lamp_contact_distance(const Lamp *la)
{
	return max_ff(prop_float(la, "bge_contact_shadow", 0.0f), 0.0f);
}

void BKE_lamp_physical_coefficients(float distance, float size, float *r_const, float *r_lin, float *r_quad)
{
	/* energy * D^2 / (r^2 + d^2) = energy / (r^2 / D^2 + d^2 / D^2) */
	const float d2 = max_ff(distance * distance, 1e-8f);
	*r_const = max_ff(size * size, 1e-6f) / d2;
	*r_lin = 0.0f;
	*r_quad = 1.0f / d2;
}
