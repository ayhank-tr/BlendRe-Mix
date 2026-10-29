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

#ifndef __BKE_MATERIAL_PHYSICAL_H__
#define __BKE_MATERIAL_PHYSICAL_H__

/** \file BKE_material_physical.h
 *  \ingroup bke
 *
 * BGE-Remix P8.7: one physical material read by the game (GLSL), Blender
 * Internal's F12 and RTX Remix. The values are the material's own colour,
 * alpha and game transparency, plus hidden ID properties (bge_*), so files
 * still open in stock 2.79:
 *
 *   bge_physical              1: physical shading; 0 or missing: Classic
 *                             (Blender Internal's diffuse and specular
 *                             shaders, how every older file opens)
 *   bge_metallic              0..1
 *   bge_roughness             0..1
 *   bge_specular              0..1, 0.5 = 4% reflection head-on
 *   bge_transmission, bge_ior glass
 *   bge_subsurface, bge_subsurface_radius
 *   bge_emission, bge_emission_strength
 *
 * The base colour is the material's diffuse colour (r, g, b). In a
 * physical material the texture influences mean: Color the base colour,
 * Hardness the roughness, Ray Mirror the metallic value, Specular the
 * specular value, Emit the emission strength.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct Main;
struct Material;

typedef struct MaterialPhysical {
	float base[3];
	float alpha;
	float metallic, roughness, specular;
	float transmission, ior;
	float subsurface, subsurface_radius[3];
	float emission[3], emission_strength;
} MaterialPhysical;

/* whether the material uses physical shading (NULL: no) */
bool BKE_material_is_physical(const struct Material *ma);

/* its values (the defaults where a property is missing) */
void BKE_material_physical_get(const struct Material *ma, MaterialPhysical *r_phys);

/* a new material: physical, with the default values */
void BKE_material_physical_init(struct Material *ma);

/* the roughness Blender Internal's hardness stands for, and back (the
 * same rule everywhere: roughness = (2 / (hardness + 2)) ^ 0.25) */
float BKE_material_hardness_to_roughness(float har);
float BKE_material_roughness_to_hardness(float roughness);

/* F12 (Blender Internal): a physical material's values written into
 * Blender Internal's settings (diffuse intensity, Cook-Torrance specular,
 * ray mirror, raytraced glass, subsurface, emit) while a render uses the
 * material, and put back after (init_render_material, end_render_material).
 * Nothing happens for Classic materials. */
void BKE_material_physical_render_begin(struct Material *ma);
void BKE_material_physical_render_end(struct Material *ma);

/* BGE-Remix P8.7, file loading: the RTX Remix panel's values (bge_remix_*,
 * with its override switch on) become the physical material's */
void BKE_material_physical_from_remix_panel(struct Main *bmain);

#ifdef __cplusplus
}
#endif

#endif  /* __BKE_MATERIAL_PHYSICAL_H__ */
