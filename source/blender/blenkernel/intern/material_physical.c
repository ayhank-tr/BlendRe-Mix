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

/** \file blender/blenkernel/intern/material_physical.c
 *  \ingroup bke
 *
 * BGE-Remix P8.7: the physical material (see BKE_material_physical.h).
 */

#include <math.h>
#include <string.h>
#include <stddef.h>

#include "MEM_guardedalloc.h"

#include "DNA_ID.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_texture_types.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "BKE_idprop.h"
#include "BKE_library.h"
#include "BKE_main.h"
#include "BKE_material_physical.h"
#include "BKE_texture.h"

/* ------------------------------------------------------------------------- */
/* ID properties */

static IDProperty *prop(const Material *ma, const char *name)
{
	return (ma && ma->id.properties) ? IDP_GetPropertyFromGroup(ma->id.properties, name) : NULL;
}

static float prop_float(const Material *ma, const char *name, float def)
{
	const IDProperty *p = prop(ma, name);
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

static int prop_int(const Material *ma, const char *name, int def)
{
	const IDProperty *p = prop(ma, name);
	if (!p) {
		return def;
	}
	switch (p->type) {
		case IDP_INT: return IDP_Int(p);
		case IDP_FLOAT: return (int)IDP_Float(p);
		case IDP_DOUBLE: return (int)IDP_Double(p);
	}
	return def;
}

static void prop_float3(const Material *ma, const char *name, float r[3])
{
	const IDProperty *p = prop(ma, name);
	if (p && p->type == IDP_ARRAY && p->len >= 3) {
		for (int i = 0; i < 3; i++) {
			if (p->subtype == IDP_FLOAT) {
				r[i] = ((const float *)IDP_Array(p))[i];
			}
			else if (p->subtype == IDP_DOUBLE) {
				r[i] = (float)((const double *)IDP_Array(p))[i];
			}
		}
	}
}

static IDProperty *group(Material *ma)
{
	if (!ma->id.properties) {
		IDPropertyTemplate val = {0};
		ma->id.properties = IDP_New(IDP_GROUP, &val, "MaterialProperties");
	}
	return ma->id.properties;
}

static void set_float(Material *ma, const char *name, float value)
{
	IDPropertyTemplate val = {0};
	val.f = value;
	IDP_ReplaceInGroup(group(ma), IDP_New(IDP_FLOAT, &val, name));
}

static void set_int(Material *ma, const char *name, int value)
{
	IDPropertyTemplate val = {0};
	val.i = value;
	IDP_ReplaceInGroup(group(ma), IDP_New(IDP_INT, &val, name));
}

static void set_float3(Material *ma, const char *name, const float v[3])
{
	IDPropertyTemplate val = {0};
	val.array.len = 3;
	val.array.type = IDP_FLOAT;
	IDProperty *p = IDP_New(IDP_ARRAY, &val, name);
	memcpy(IDP_Array(p), v, sizeof(float[3]));
	IDP_ReplaceInGroup(group(ma), p);
}

/* ------------------------------------------------------------------------- */

bool BKE_material_is_physical(const Material *ma)
{
	return ma && prop_int(ma, "bge_physical", 0) != 0;
}

void BKE_material_physical_get(const Material *ma, MaterialPhysical *r)
{
	memset(r, 0, sizeof(*r));
	r->base[0] = r->base[1] = r->base[2] = 0.8f;
	r->alpha = 1.0f;
	r->roughness = 0.5f;
	r->specular = 0.5f;
	r->ior = 1.45f;
	r->subsurface_radius[0] = 1.0f;
	r->subsurface_radius[1] = 0.2f;
	r->subsurface_radius[2] = 0.1f;
	r->emission[0] = r->emission[1] = r->emission[2] = 1.0f;
	if (!ma) {
		return;
	}
	r->base[0] = ma->r;
	r->base[1] = ma->g;
	r->base[2] = ma->b;
	r->alpha = ma->alpha;
	r->metallic = prop_float(ma, "bge_metallic", 0.0f);
	r->roughness = prop_float(ma, "bge_roughness", 0.5f);
	r->specular = prop_float(ma, "bge_specular", 0.5f);
	r->transmission = prop_float(ma, "bge_transmission", 0.0f);
	r->ior = prop_float(ma, "bge_ior", 1.45f);
	r->subsurface = prop_float(ma, "bge_subsurface", 0.0f);
	prop_float3(ma, "bge_subsurface_radius", r->subsurface_radius);
	prop_float3(ma, "bge_emission", r->emission);
	r->emission_strength = prop_float(ma, "bge_emission_strength", 0.0f);
}

void BKE_material_physical_init(Material *ma)
{
	set_int(ma, "bge_physical", 1);
}

float BKE_material_hardness_to_roughness(float har)
{
	return powf(2.0f / (max_ff(har, 1.0f) + 2.0f), 0.25f);
}

float BKE_material_roughness_to_hardness(float roughness)
{
	const float r = max_ff(roughness, 0.05f);
	return CLAMPIS(2.0f / (r * r * r * r) - 2.0f, 1.0f, 511.0f);
}

/* ------------------------------------------------------------------------- */
/* F12: the physical values as Blender Internal settings */

/* the settings a render changes, kept to put back */
typedef struct RenderBackup {
	struct RenderBackup *next, *prev;
	Material *ma;
	float ref, spec, specr, specg, specb, ray_mirror, fresnel_mir, fresnel_mir_i, mirr, mirg, mirb;
	float gloss_mir, gloss_tra, alpha, ang, fresnel_tra, fresnel_tra_i, filter, emit;
	float sss_radius[3], sss_col[3], sss_scale, sss_colfac;
	short har, diff_shader, spec_shader, samp_gloss_mir, samp_gloss_tra, sss_flag;
	int mode;
} RenderBackup;

static ListBase g_render_backups = {NULL, NULL};
static ThreadMutex g_render_backup_lock = BLI_MUTEX_INITIALIZER;

void BKE_material_physical_render_begin(Material *ma)
{
	MaterialPhysical ph;
	if (!BKE_material_is_physical(ma)) {
		return;
	}
	BLI_mutex_lock(&g_render_backup_lock);
	for (RenderBackup *b = g_render_backups.first; b; b = b->next) {
		if (b->ma == ma) {
			/* already translated (a material in two node trees) */
			BLI_mutex_unlock(&g_render_backup_lock);
			return;
		}
	}
	RenderBackup *b = MEM_callocN(sizeof(RenderBackup), "physical render backup");
	b->ma = ma;
	b->ref = ma->ref;
	b->spec = ma->spec;
	b->specr = ma->specr;
	b->specg = ma->specg;
	b->specb = ma->specb;
	b->ray_mirror = ma->ray_mirror;
	b->fresnel_mir = ma->fresnel_mir;
	b->fresnel_mir_i = ma->fresnel_mir_i;
	b->mirr = ma->mirr;
	b->mirg = ma->mirg;
	b->mirb = ma->mirb;
	b->gloss_mir = ma->gloss_mir;
	b->gloss_tra = ma->gloss_tra;
	b->alpha = ma->alpha;
	b->ang = ma->ang;
	b->fresnel_tra = ma->fresnel_tra;
	b->fresnel_tra_i = ma->fresnel_tra_i;
	b->filter = ma->filter;
	b->emit = ma->emit;
	copy_v3_v3(b->sss_radius, ma->sss_radius);
	copy_v3_v3(b->sss_col, ma->sss_col);
	b->sss_scale = ma->sss_scale;
	b->sss_colfac = ma->sss_colfac;
	b->har = ma->har;
	b->diff_shader = ma->diff_shader;
	b->spec_shader = ma->spec_shader;
	b->samp_gloss_mir = ma->samp_gloss_mir;
	b->samp_gloss_tra = ma->samp_gloss_tra;
	b->sss_flag = ma->sss_flag;
	b->mode = ma->mode;
	BLI_addtail(&g_render_backups, b);
	BLI_mutex_unlock(&g_render_backup_lock);

	BKE_material_physical_get(ma, &ph);
	const float metal = CLAMPIS(ph.metallic, 0.0f, 1.0f);
	const bool glass = ph.transmission >= 0.5f;
	/* diffuse: the base colour, less what metal and glass take */
	ma->diff_shader = MA_DIFF_LAMBERT;
	ma->ref = (1.0f - metal) * (glass ? 0.0f : 1.0f - CLAMPIS(ph.transmission, 0.0f, 1.0f));
	/* highlights: white on dielectrics, tinted on metals */
	ma->spec_shader = MA_SPEC_COOKTORR;
	ma->spec = (1.0f - metal) * ph.specular + metal;
	interp_v3_v3v3(&ma->specr, (const float[3]){1.0f, 1.0f, 1.0f}, ph.base, metal);
	ma->har = (short)BKE_material_roughness_to_hardness(ph.roughness);
	/* reflections: metals mirror their colour; dielectrics a little, with
	 * Fresnel; blurred by the roughness */
	ma->mode &= ~MA_RAYMIRROR;
	if (metal > 0.0f || ph.roughness < 0.5f) {
		ma->mode |= MA_RAYMIRROR;
		ma->ray_mirror = metal + (1.0f - metal) * 0.08f * ph.specular * 4.0f;
		ma->fresnel_mir = metal > 0.5f ? 0.0f : 4.0f;
		ma->fresnel_mir_i = 1.25f;
		interp_v3_v3v3(&ma->mirr, (const float[3]){1.0f, 1.0f, 1.0f}, ph.base, metal);
		ma->gloss_mir = 1.0f - ph.roughness;
		ma->samp_gloss_mir = 8;
	}
	/* glass: raytraced transparency with the IOR */
	if (glass) {
		ma->mode = (ma->mode & ~MA_ZTRANSP) | MA_TRANSP | MA_RAYTRANSP;
		ma->alpha = ph.alpha * (1.0f - CLAMPIS(ph.transmission, 0.0f, 1.0f));
		ma->ang = ph.ior;
		ma->fresnel_tra = 0.0f;
		ma->filter = 1.0f;
		ma->gloss_tra = 1.0f - ph.roughness;
		ma->samp_gloss_tra = 8;
		if (!(ma->mode & MA_RAYMIRROR)) {
			ma->mode |= MA_RAYMIRROR;
			ma->ray_mirror = 1.0f;
			ma->fresnel_mir = 4.0f;
			ma->fresnel_mir_i = 1.25f;
			ma->gloss_mir = 1.0f - ph.roughness;
		}
	}
	/* subsurface */
	if (ph.subsurface > 0.0f && !glass) {
		ma->sss_flag |= MA_DIFF_SSS;
		copy_v3_v3(ma->sss_col, ph.base);
		copy_v3_v3(ma->sss_radius, ph.subsurface_radius);
		ma->sss_scale = 0.1f;
		ma->sss_colfac = CLAMPIS(ph.subsurface, 0.0f, 1.0f);
	}
	/* emission: Blender Internal emits the diffuse colour */
	ma->emit = ph.emission_strength;
}

void BKE_material_physical_render_end(Material *ma)
{
	RenderBackup *b;
	BLI_mutex_lock(&g_render_backup_lock);
	for (b = g_render_backups.first; b; b = b->next) {
		if (b->ma == ma) {
			break;
		}
	}
	if (b) {
		ma->ref = b->ref;
		ma->spec = b->spec;
		ma->specr = b->specr;
		ma->specg = b->specg;
		ma->specb = b->specb;
		ma->ray_mirror = b->ray_mirror;
		ma->fresnel_mir = b->fresnel_mir;
		ma->fresnel_mir_i = b->fresnel_mir_i;
		ma->mirr = b->mirr;
		ma->mirg = b->mirg;
		ma->mirb = b->mirb;
		ma->gloss_mir = b->gloss_mir;
		ma->gloss_tra = b->gloss_tra;
		ma->alpha = b->alpha;
		ma->ang = b->ang;
		ma->fresnel_tra = b->fresnel_tra;
		ma->fresnel_tra_i = b->fresnel_tra_i;
		ma->filter = b->filter;
		ma->emit = b->emit;
		copy_v3_v3(ma->sss_radius, b->sss_radius);
		copy_v3_v3(ma->sss_col, b->sss_col);
		ma->sss_scale = b->sss_scale;
		ma->sss_colfac = b->sss_colfac;
		ma->har = b->har;
		ma->diff_shader = b->diff_shader;
		ma->spec_shader = b->spec_shader;
		ma->samp_gloss_mir = b->samp_gloss_mir;
		ma->samp_gloss_tra = b->samp_gloss_tra;
		ma->sss_flag = b->sss_flag;
		ma->mode = b->mode;
		BLI_freelinkN(&g_render_backups, b);
	}
	BLI_mutex_unlock(&g_render_backup_lock);
}

/* ------------------------------------------------------------------------- */
/* the RTX Remix panel (P3) into the physical material */

enum { PANEL_OPAQUE = 0, PANEL_GLASS = 1, PANEL_SUBSURFACE = 2 };
enum { PANEL_ALPHA_OPAQUE = 0, PANEL_ALPHA_BLEND = 1, PANEL_ALPHA_CLIP = 2 };

static Image *panel_image(Main *bmain, const Material *ma, const char *name)
{
	const IDProperty *p = prop(ma, name);
	if (!p || p->type != IDP_STRING || !IDP_String(p)[0]) {
		return NULL;
	}
	return BLI_findstring(&bmain->image, IDP_String(p), offsetof(ID, name) + 2);
}

/* an image texture slot, in the first free slot */
static void add_image_slot(Main *bmain, Material *ma, Image *ima, int mapto, bool normal_map)
{
	if (!ima) {
		return;
	}
	MTex *mtex = BKE_texture_mtex_add_id(&ma->id, -1);
	if (!mtex) {
		return;
	}
	Tex *tex = BKE_texture_add(bmain, ima->id.name + 2);
	tex->type = TEX_IMAGE;
	tex->ima = ima;
	id_us_plus(&ima->id);
	if (normal_map) {
		tex->imaflag |= TEX_NORMALMAP;
		mtex->normapspace = MTEX_NSPACE_TANGENT;
	}
	mtex->tex = tex;
	mtex->texco = TEXCO_UV;
	mtex->mapto = mapto;
}

/* the panel's values, kept by the material after the panel went (its
 * Particle option and Bake Size stay) */
static void remove_panel_props(Material *ma)
{
	static const char *names[] = {
	    "bge_remix_override", "bge_remix_type", "bge_remix_albedo", "bge_remix_opacity", "bge_remix_roughness",
	    "bge_remix_metallic", "bge_remix_emissive", "bge_remix_emissive_strength", "bge_remix_alpha",
	    "bge_remix_alpha_cutoff", "bge_remix_ior", "bge_remix_transmittance", "bge_remix_subsurface_radius",
	    "bge_remix_albedo_image", "bge_remix_normal_image", "bge_remix_roughness_image",
	    "bge_remix_metallic_image", "bge_remix_emissive_image", NULL};
	for (int i = 0; names[i]; i++) {
		IDProperty *p = prop(ma, names[i]);
		if (p) {
			IDP_FreeFromGroup(ma->id.properties, p);
		}
	}
}

void BKE_material_physical_from_remix_panel(Main *bmain)
{
	for (Material *ma = bmain->mat.first; ma; ma = ma->id.next) {
		if (ID_IS_LINKED_DATABLOCK(ma) || !ma->id.properties) {
			continue;
		}
		if (!prop_int(ma, "bge_remix_override", 0)) {
			remove_panel_props(ma);
			continue;
		}
		const int type = prop_int(ma, "bge_remix_type", PANEL_OPAQUE);
		float albedo[3] = {0.8f, 0.8f, 0.8f}, emission[3] = {1.0f, 1.0f, 1.0f};
		float transmittance[3] = {1.0f, 1.0f, 1.0f}, radius[3] = {1.0f, 0.2f, 0.1f};
		prop_float3(ma, "bge_remix_albedo", albedo);
		prop_float3(ma, "bge_remix_emissive", emission);
		prop_float3(ma, "bge_remix_transmittance", transmittance);
		prop_float3(ma, "bge_remix_subsurface_radius", radius);

		BKE_material_physical_init(ma);
		copy_v3_v3(&ma->r, type == PANEL_GLASS ? transmittance : albedo);
		ma->ref = 1.0f;
		set_float(ma, "bge_metallic", prop_float(ma, "bge_remix_metallic", 0.0f));
		set_float(ma, "bge_roughness", prop_float(ma, "bge_remix_roughness", 0.5f));
		set_float3(ma, "bge_emission", emission);
		set_float(ma, "bge_emission_strength", prop_float(ma, "bge_remix_emissive_strength", 0.0f));
		if (type == PANEL_GLASS) {
			set_float(ma, "bge_transmission", 1.0f);
			set_float(ma, "bge_ior", prop_float(ma, "bge_remix_ior", 1.5f));
		}
		else if (type == PANEL_SUBSURFACE) {
			set_float(ma, "bge_subsurface", 1.0f);
			set_float3(ma, "bge_subsurface_radius", radius);
		}
		switch (prop_int(ma, "bge_remix_alpha", PANEL_ALPHA_OPAQUE)) {
			case PANEL_ALPHA_BLEND:
				ma->alpha = prop_float(ma, "bge_remix_opacity", 1.0f);
				ma->game.alpha_blend = GEMAT_ALPHA;
				break;
			case PANEL_ALPHA_CLIP:
				ma->game.alpha_blend = GEMAT_CLIP;
				break;
			default:
				break;
		}
		/* the panel's images become texture slots, as every renderer reads */
		add_image_slot(bmain, ma, panel_image(bmain, ma, "bge_remix_albedo_image"), MAP_COL, false);
		add_image_slot(bmain, ma, panel_image(bmain, ma, "bge_remix_normal_image"), MAP_NORM, true);
		add_image_slot(bmain, ma, panel_image(bmain, ma, "bge_remix_roughness_image"), MAP_HAR, false);
		add_image_slot(bmain, ma, panel_image(bmain, ma, "bge_remix_metallic_image"), MAP_RAYMIRR, false);
		add_image_slot(bmain, ma, panel_image(bmain, ma, "bge_remix_emissive_image"), MAP_EMIT, false);
		/* done: the panel's own values are gone */
		remove_panel_props(ma);
	}
}
