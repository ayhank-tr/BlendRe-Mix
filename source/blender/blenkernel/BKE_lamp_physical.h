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

#ifndef __BKE_LAMP_PHYSICAL_H__
#define __BKE_LAMP_PHYSICAL_H__

/** \file BKE_lamp_physical.h
 *  \ingroup bke
 *
 * BGE-Remix P8.9: one light description for the game (GLSL), Blender
 * Internal's F12 and RTX Remix: type, colour, power, size, shadows. The size
 * is the lamp's own fields (point and spot: "shadow_soft_size", the radius;
 * area: its width and height); the sun's is an angle, a hidden ID property
 * so files still open in stock 2.79:
 *
 *   bge_physical    1: physical falloff (inverse square with the light's
 *                   size); 0 or missing: the lamp's own falloff type (Classic,
 *                   how every older file opens)
 *   bge_sun_angle   the sun's angular diameter in degrees (0.53, the real
 *                   sun's, when missing)
 *   bge_contact_shadow  P8.10: how far contact shadows reach in the game
 *                   (0 or missing: none)
 *
 * Physical falloff: the light at distance d is
 *     energy * D^2 / (r^2 + d^2)
 * (D: the lamp's Distance, where it has its full energy; r: its size), the
 * inverse square law with the light's size, as Remix's sphere light falls
 * off. Blender Internal's "Inverse Coefficients" falloff is the same with
 * constant r^2 / D^2, linear 0 and quadratic 1 / D^2, so F12 and the game's
 * shaders use that.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct Lamp;

/* the real sun's angular diameter, degrees */
#define BKE_LAMP_SUN_ANGLE_DEFAULT 0.53f

/* whether the lamp uses physical falloff (NULL: no) */
bool BKE_lamp_is_physical(const struct Lamp *la);

/* a new lamp: physical */
void BKE_lamp_physical_init(struct Lamp *la);

/* the light's radius (point, spot) */
float BKE_lamp_size(const struct Lamp *la);

/* the sun's angular diameter, degrees */
float BKE_lamp_sun_angle(const struct Lamp *la);

/* P8.10: whether the lamp casts shadows in the game (spot: buffer or ray
 * shadow; point, sun, area: ray shadow, the "Shadow" switch; hemi: never) */
bool BKE_lamp_casts_shadow(const struct Lamp *la);

/* P8.10: how far the lamp's contact shadows reach (0: none) */
float BKE_lamp_contact_distance(const struct Lamp *la);

/* the "Inverse Coefficients" falloff that is the physical one for a lamp of
 * this distance and size */
void BKE_lamp_physical_coefficients(float distance, float size, float *r_const, float *r_lin, float *r_quad);

#ifdef __cplusplus
}
#endif

#endif  /* __BKE_LAMP_PHYSICAL_H__ */
