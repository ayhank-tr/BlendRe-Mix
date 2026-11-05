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

/** \file remix_material.cc
 *  \ingroup remix
 *
 * The material translator (see BRMX_material.h).
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "BRMX_bake.h"
#include "BRMX_material.h"

extern "C" {
#include "DNA_ID.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_texture_types.h"

#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_main.h"
#include "BKE_node.h"

#include "BLI_listbase.h"

#include "GPU_material.h"

#include "BKE_material_physical.h"

#include "IMB_colormanagement.h"
}

namespace brmx {

const char *texture_slot_name(int slot)
{
	static const char *names[TEX_COUNT] = {"albedo", "normal", "roughness", "metallic", "emissive", "height",
	                                       "tangent", "transmittance", "sss_transmittance", "sss_thickness",
	                                       "sss_albedo", "sss_radius"};
	return (slot >= 0 && slot < TEX_COUNT) ? names[slot] : "?";
}

const wchar_t **texture_field(brb_material_desc &d, int slot)
{
	switch (slot) {
		case TEX_ALBEDO: return &d.albedo_texture;
		case TEX_NORMAL: return &d.normal_texture;
		case TEX_ROUGHNESS: return &d.roughness_texture;
		case TEX_METALLIC: return &d.metallic_texture;
		case TEX_EMISSIVE: return &d.emissive_texture;
		case TEX_HEIGHT: return &d.height_texture;
		case TEX_TANGENT: return &d.tangent_texture;
		case TEX_TRANSMITTANCE: return &d.transmittance_texture;
		case TEX_SSS_TRANSMITTANCE: return &d.subsurface_transmittance_texture;
		case TEX_SSS_THICKNESS: return &d.subsurface_thickness_texture;
		case TEX_SSS_ALBEDO: return &d.subsurface_albedo_texture;
		case TEX_SSS_RADIUS: return &d.subsurface_radius_texture;
	}
	return NULL;
}

namespace {

/* ------------------------------------------------------------------------- */
/* Small helpers */

void set3(float r[3], float a, float b, float c)
{
	r[0] = a;
	r[1] = b;
	r[2] = c;
}

void note(MaterialSpec &s, const std::string &text)
{
	s.notes.push_back(text);
}

/* P8.24: a field carried approximately or not at all (the worst mark stays) */
void mark(MaterialSpec &s, const char *field, int level, const std::string &why)
{
	std::pair<int, std::string> &m = s.marks[field];
	if (m.second.empty() || level >= m.first) {
		m.first = level;
		m.second = why;
	}
}

/* Blender Internal's specular hardness (Blinn-Phong exponent) as a GGX
 * roughness: alpha = sqrt(2 / (n + 2)), roughness = sqrt(alpha). */
float hardness_to_roughness(const Material *ma)
{
	return (ma->spec < 0.01f) ? 1.0f : (float)std::pow(2.0 / ((double)ma->har + 2.0), 0.25);
}

/* Cycles' Glass colour tints light once at each surface it crosses, so
 * light through a solid object is tinted by the colour squared, whatever
 * its size. Remix tints by distance travelled inside: the colour squared
 * after half a metre matches Cycles for objects about half a metre thick
 * (the test balls are 0.8 m across). */
static const float GLASS_TINT_DISTANCE = 0.5f;

static void glass_tint(const float c[3], float r_transmittance[3], MaterialSpec &s)
{
	mark(s, "transmittance", MATCH_APPROX, "Cycles tints glass at each surface, Remix by the distance inside");
	for (int i = 0; i < 3; i++) {
		r_transmittance[i] = c[i] * c[i];
	}
}

/* The defaults: Blender's default material (grey, half rough). */
void defaults(MaterialSpec &s)
{
	memset(&s.desc, 0, sizeof(s.desc));
	set3(s.desc.albedo, 0.8f * 0.8f, 0.8f * 0.8f, 0.8f * 0.8f);
	s.desc.opacity = 1.0f;
	s.desc.roughness = 0.5f;
	s.desc.type = BRB_MATERIAL_OPAQUE;
	s.desc.alpha_cutoff = 0.5f;
	s.desc.ior = 1.5f;
	set3(s.desc.transmittance, 1.0f, 1.0f, 1.0f);
	/* Remix tints glass by the distance light travels inside it: the
	 * transmittance is the colour left after this many metres */
	s.desc.transmittance_distance = GLASS_TINT_DISTANCE;
	set3(s.desc.subsurface_color, 0.8f, 0.8f, 0.8f);
	set3(s.desc.subsurface_radius, 1.0f, 0.2f, 0.1f);
	for (int i = 0; i < TEX_COUNT; i++) {
		s.textures[i] = TextureRequest();
	}
	s.notes.clear();
	s.source = MaterialSpec::DEFAULT;
	s.particle = false;
	s.bake_slots = 0;
	s.categories = 0;
	s.locked = 0;
	s.shadeless = false;
	s.marks.clear();
	s.nodes_baked = false;
	/* P9.4: Remix's own wrap (repeat) and filter; the UVs as they are */
	s.uv_transform = false;
	const float id[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
	memcpy(s.uv_matrix, id, sizeof(s.uv_matrix));
	s.placement.clear();
}

TextureRequest texture(Image *ima, int kind, int channel = 0, int flags = 0)
{
	TextureRequest t;
	t.image = ima;
	t.kind = kind;
	t.channel = channel;
	t.flags = flags;
	return t;
}

Image *image_by_name(const char *name)
{
	if (!name || !name[0] || !G.main) {
		return NULL;
	}
	return (Image *)BLI_findstring(&G.main->image, name, offsetof(ID, name) + 2);
}

/* ------------------------------------------------------------------------- */
/* 1. The RTX Remix panel (custom properties, see properties_material.py;
 *    the defaults here match the panel's) */

IDProperty *prop(const Material *ma, const char *name)
{
	IDProperty *group = ma->id.properties;
	return group ? IDP_GetPropertyFromGroup(group, name) : NULL;
}

int prop_int(const Material *ma, const char *name, int def)
{
	IDProperty *p = prop(ma, name);
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

float prop_float(const Material *ma, const char *name, float def)
{
	IDProperty *p = prop(ma, name);
	if (!p) {
		return def;
	}
	switch (p->type) {
		case IDP_INT: return (float)IDP_Int(p);
		case IDP_FLOAT: return IDP_Float(p);
		case IDP_DOUBLE: return (float)IDP_Double(p);
	}
	return def;
}

void prop_float3(const Material *ma, const char *name, float r[3])
{
	IDProperty *p = prop(ma, name);
	if (!p || p->type != IDP_ARRAY || p->len < 3) {
		return;
	}
	for (int i = 0; i < 3; i++) {
		if (p->subtype == IDP_FLOAT) {
			r[i] = ((const float *)IDP_Array(p))[i];
		}
		else if (p->subtype == IDP_DOUBLE) {
			r[i] = (float)((const double *)IDP_Array(p))[i];
		}
		else if (p->subtype == IDP_INT) {
			r[i] = (float)((const int *)IDP_Array(p))[i];
		}
	}
}

Image *prop_image(const Material *ma, const char *name)
{
	IDProperty *p = prop(ma, name);
	return (p && p->type == IDP_STRING) ? image_by_name(IDP_String(p)) : NULL;
}

/* ------------------------------------------------------------------------- */
/* 3. Blender Internal */

/* P8.24: the fields a texture slot's influences reach */
void mark_slot_fields(MaterialSpec &s, int mapto, int level, const std::string &why)
{
	if (mapto & MAP_COL) {
		mark(s, "albedo", level, why);
	}
	if (mapto & MAP_NORM) {
		mark(s, "normal", level, why);
	}
	if (mapto & MAP_EMIT) {
		mark(s, "emissive", level, why);
	}
	if (mapto & MAP_HAR) {
		mark(s, "roughness", level, why);
	}
	if (mapto & MAP_RAYMIRR) {
		mark(s, "metallic", level, why);
	}
	if (mapto & MAP_ALPHA) {
		mark(s, "opacity", level, why);
	}
}

void image_slots(const Material *ma, MaterialSpec &s, bool physical);

/* P8.8: Classic settings Remix can show: a ray mirror (metal), raytraced
 * glass, subsurface scattering, Z transparency, Add blending */
void classic_extras(const Material *ma, MaterialSpec &s)
{
	brb_material_desc &d = s.desc;
	if ((ma->mode & MA_TRANSP) && (ma->mode & MA_ZTRANSP) && ma->alpha < 1.0f && !d.alpha_blend && !d.alpha_test) {
		/* Z transparency: see-through by the alpha, as F12 */
		d.alpha_blend = 1;
		d.opacity = ma->alpha;
	}
	if (ma->game.alpha_blend == GEMAT_ADD) {
		d.additive = 1;
	}
	if ((ma->mode & MA_RAYMIRROR) && ma->ray_mirror > 0.0f) {
		const float rm = std::min(ma->ray_mirror, 1.0f);
		if (ma->fresnel_mir <= 0.0f) {
			/* a mirror of the same strength at every angle: metal, in the
			 * mirror's colour */
			d.metallic = rm;
			for (int i = 0; i < 3; i++) {
				d.albedo[i] = d.albedo[i] * (1.0f - rm) + (&ma->mirr)[i] * rm;
			}
		}
		/* with Fresnel: Remix's own Fresnel reflects; the gloss blurs */
		d.roughness = std::min(std::max(1.0f - ma->gloss_mir, 0.0f), 1.0f);
		mark(s, "metallic", MATCH_APPROX, "from Ray Mirror");
		mark(s, "roughness", MATCH_APPROX, "from Ray Mirror's gloss");
	}
	if ((ma->mode & MA_TRANSP) && (ma->mode & MA_RAYTRANSP) && ma->alpha < 1.0f) {
		d.type = BRB_MATERIAL_TRANSLUCENT;
		d.ior = std::min(std::max(ma->ang, 1.0f), 3.0f);
		/* Filter tints the light passing through by the diffuse colour */
		const float f = std::min(std::max(ma->filter, 0.0f), 1.0f);
		set3(d.transmittance, 1.0f - f + f * ma->r, 1.0f - f + f * ma->g, 1.0f - f + f * ma->b);
		d.alpha_blend = 0;
		d.additive = 0;
		mark(s, "transmittance", MATCH_APPROX, "Raytraced Transparency's Filter and colour");
	}
	if ((ma->sss_flag & MA_DIFF_SSS) && d.type != BRB_MATERIAL_TRANSLUCENT) {
		d.subsurface = 1;
		memcpy(d.subsurface_color, ma->sss_col, sizeof(d.subsurface_color));
		for (int i = 0; i < 3; i++) {
			d.subsurface_radius[i] = ma->sss_radius[i] * ma->sss_scale;
		}
		mark(s, "subsurface", MATCH_APPROX, "Blender Internal's subsurface radius and scale");
	}
}

static void shadeless_glow(const Material *ma, MaterialSpec &s, bool face_image);

void from_blender_internal(const Material *ma, MaterialSpec &s)
{
	brb_material_desc &d = s.desc;
	s.source = MaterialSpec::BLENDER_INTERNAL;
	s.bake_slots = bake_slots(ma);
	set3(d.albedo, ma->r * ma->ref, ma->g * ma->ref, ma->b * ma->ref);
	d.opacity = ma->alpha;
	d.roughness = hardness_to_roughness(ma);
	mark(s, "roughness", MATCH_APPROX, "from Blender Internal's specular hardness");
	d.metallic = 0.0f;
	if (ma->emit > 0.0f) {
		set3(d.emissive, ma->r, ma->g, ma->b);
		d.emissive_intensity = ma->emit;
	}
	if (ma->mode & MA_SHLESS) {
		/* shadeless: shows its own colour, whatever the light */
		set3(d.albedo, 0.0f, 0.0f, 0.0f);
		set3(d.emissive, ma->r, ma->g, ma->b);
		d.emissive_intensity = 1.0f;
		mark(s, "emissive", MATCH_APPROX, "Shadeless: shown as a glow of its colour");
	}
	const int blend = ma->game.alpha_blend;
	d.alpha_blend = (blend == GEMAT_ALPHA || blend == GEMAT_ALPHA_SORT || blend == GEMAT_ADD) ? 1 : 0;
	d.alpha_test = (blend == GEMAT_CLIP) ? 1 : 0;
	if (!d.alpha_blend) {
		d.opacity = 1.0f;
	}

	image_slots(ma, s, false);
	shadeless_glow(ma, s, false);
	classic_extras(ma, s);
}

/* The image texture slots (P8.7: a physical material's Hardness slot is
 * its roughness, its Ray Mirror slot its metallic value) */
/* P8.30: a shadeless surface with a colour image glows with the image
 * (the RTX run: textured shadeless screens came out plain white). The
 * image stays the albedo too, for its alpha; face_image: the image replaces
 * the colour (Face Textures) */
static void shadeless_glow(const Material *ma, MaterialSpec &s, bool face_image)
{
	brb_material_desc &d = s.desc;
	if (ma->mode & MA_SHLESS) {
		s.shadeless = true;
	}
	if (!(ma->mode & MA_SHLESS) || !s.textures[TEX_ALBEDO].image || s.textures[TEX_EMISSIVE].image ||
	    (s.locked & (1u << TEX_EMISSIVE)))
	{
		return;
	}
	s.textures[TEX_EMISSIVE] = s.textures[TEX_ALBEDO];
	if (face_image) {
		set3(d.emissive, 1.0f, 1.0f, 1.0f);
	}
	d.emissive_intensity = 1.0f;
	set3(d.albedo, 0.0f, 0.0f, 0.0f);
	mark(s, "emissive", MATCH_APPROX, "Shadeless: shown as a glow of its image");
}

/* ------------------------------------------------------------------------- */
/* P9.4: a texture's settings as Remix takes them: its wrap and filter modes
 * (per material in Remix) and a transform of the mesh's UVs (per material
 * here, on the vertices sent) */

/* wrap modes as brb_material_desc.wrap_u/v: Remix's + 1 */
enum { WRAP_CLAMP = 1, WRAP_REPEAT = 2, WRAP_MIRRORED = 3, WRAP_CLIP = 4 };

struct Sampling {
	int wrap_u, wrap_v;
	bool nearest;
	float m[6]; /* u' = m0 u + m1 v + m2, v' = m3 u + m4 v + m5 */
	Sampling() : wrap_u(WRAP_REPEAT), wrap_v(WRAP_REPEAT), nearest(false)
	{
		const float id[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
		memcpy(m, id, sizeof(m));
	}
	bool identity() const
	{
		return m[0] == 1.0f && m[1] == 0.0f && m[2] == 0.0f && m[3] == 0.0f && m[4] == 1.0f && m[5] == 0.0f;
	}
	bool same(const Sampling &o) const
	{
		if (wrap_u != o.wrap_u || wrap_v != o.wrap_v || nearest != o.nearest) {
			return false;
		}
		for (int i = 0; i < 6; i++) {
			if (std::fabs(m[i] - o.m[i]) > 1e-5f) {
				return false;
			}
		}
		return true;
	}
};

static const char *wrap_name(int w)
{
	switch (w) {
		case WRAP_CLAMP: return "clamp";
		case WRAP_MIRRORED: return "mirrored repeat";
		case WRAP_CLIP: return "clip";
	}
	return "repeat";
}

/* An image texture's own settings over a mapping already in m (the slot's
 * or the Mapping node's): its extension and interpolation, its repeat
 * counts and its axes flipped. False: something only a bake can draw
 * (Checker, crop, the Colors panel). */
static bool image_sampling(const Tex *tex, Sampling &r, std::string *r_why)
{
	if (tex->extend == TEX_CHECKER) {
		if (r_why) *r_why = "the Checker extension";
		return false;
	}
	if (tex->cropxmin != 0.0f || tex->cropymin != 0.0f || tex->cropxmax != 1.0f || tex->cropymax != 1.0f) {
		if (r_why) *r_why = "a crop";
		return false;
	}
	if (tex->bright != 1.0f || tex->contrast != 1.0f || tex->saturation != 1.0f || tex->rfac != 1.0f ||
	    tex->gfac != 1.0f || tex->bfac != 1.0f || (tex->flag & TEX_COLORBAND))
	{
		if (r_why) *r_why = "the Colors panel (brightness, contrast, saturation, RGB, ramp)";
		return false;
	}
	r.nearest = !(tex->imaflag & TEX_INTERPOL);
	int wu = WRAP_REPEAT, wv = WRAP_REPEAT;
	float ru = 1.0f, rv = 1.0f;
	if (tex->extend == TEX_EXTEND) {
		wu = wv = WRAP_CLAMP;
	}
	else if (tex->extend == TEX_CLIP || tex->extend == TEX_CLIPCUBE) {
		wu = wv = WRAP_CLIP;
	}
	else {
		/* Repeat: its counts, mirrored when asked (each axis) */
		ru = (float)std::max<short>(tex->xrepeat, 1);
		rv = (float)std::max<short>(tex->yrepeat, 1);
		if (tex->flag & TEX_REPEAT_XMIR) {
			wu = WRAP_MIRRORED;
		}
		if (tex->flag & TEX_REPEAT_YMIR) {
			wv = WRAP_MIRRORED;
		}
	}
	/* Flip X/Y Axis swaps the image's axes before the repeats */
	float a[6];
	memcpy(a, r.m, sizeof(a));
	if (tex->imaflag & TEX_IMAROT) {
		const float t[6] = {a[3], a[4], a[5], a[0], a[1], a[2]};
		memcpy(a, t, sizeof(a));
	}
	for (int i = 0; i < 3; i++) {
		a[i] *= ru;
		a[3 + i] *= rv;
	}
	memcpy(r.m, a, sizeof(a));
	r.wrap_u = wu;
	r.wrap_v = wv;
	return true;
}

/* the material channels a slot feeds, as bake slots */
static unsigned slot_channels(const MTex *mtex, bool physical)
{
	unsigned c = 0;
	if (mtex->mapto & (MAP_COL | MAP_ALPHA)) {
		c |= 1u << BAKE_ALBEDO;
	}
	if (!physical && (mtex->mapto & MAP_REF)) {
		c |= 1u << BAKE_ALBEDO;
	}
	if (mtex->mapto & MAP_HAR) {
		c |= 1u << BAKE_ROUGHNESS;
	}
	if (mtex->mapto & MAP_EMIT) {
		c |= 1u << BAKE_EMISSIVE;
	}
	if (mtex->mapto & MAP_NORM) {
		c |= 1u << BAKE_NORMAL;
	}
	if (physical && (mtex->mapto & MAP_RAYMIRR)) {
		c |= 1u << BAKE_METALLIC;
	}
	return c;
}

/* Why an image slot cannot go to Remix as it is ("": it can), and its
 * sampling when it can */
static std::string slot_bake_reason(const Material *ma, const MTex *mtex, bool physical, Sampling &r)
{
	const Tex *tex = mtex->tex;
	if (mtex->texco != TEXCO_UV) {
		return "coordinates other than UV";
	}
	if (mtex->mapping != MTEX_FLAT || mtex->projx != PROJ_X || mtex->projy != PROJ_Y) {
		return "a projection other than flat X, Y";
	}
	if (mtex->texflag & (MTEX_NEGATIVE | MTEX_RGBTOINT | MTEX_STENCIL)) {
		return (mtex->texflag & MTEX_NEGATIVE) ? "Negative" : (mtex->texflag & MTEX_RGBTOINT) ? "RGB to intensity" :
		                                                                                         "Stencil";
	}
	if (mtex->blendtype != MTEX_BLEND) {
		return "a blend other than Mix";
	}
	if (((mtex->mapto & MAP_COL) && mtex->colfac < 1.0f) || ((mtex->mapto & MAP_ALPHA) && mtex->alphafac < 1.0f) ||
	    ((mtex->mapto & MAP_HAR) && mtex->hardfac < 1.0f) || ((mtex->mapto & MAP_NORM) && mtex->norfac != 1.0f) ||
	    (physical && (mtex->mapto & MAP_RAYMIRR) && mtex->raymirrfac < 1.0f))
	{
		return "an influence other than 1";
	}
	if (!physical && (mtex->mapto & MAP_REF)) {
		return "diffuse intensity";
	}
	if ((mtex->mapto & MAP_NORM) && !((tex->imaflag & TEX_NORMALMAP) && mtex->normapspace == MTEX_NSPACE_TANGENT)) {
		return (tex->imaflag & TEX_NORMALMAP) ? "a normal map not in tangent space" : "a bump map";
	}
	/* the slot's scale and offset, as the game's GLSL maps them
	 * (gpu_material.c: size * uv + ofs + 0.5 - 0.5 * size) */
	Sampling s;
	s.m[0] = mtex->size[0];
	s.m[2] = mtex->ofs[0] + 0.5f - 0.5f * mtex->size[0];
	s.m[4] = mtex->size[1];
	s.m[5] = mtex->ofs[1] + 0.5f - 0.5f * mtex->size[1];
	std::string why;
	if (!image_sampling(tex, s, &why)) {
		return why;
	}
	r = s;
	(void)ma;
	return "";
}

struct SlotPlan {
	unsigned bake;             /* bake slots for the image slots */
	unsigned direct_slots;     /* MTex indices (bits) that go as they are */
	std::string why[MAX_MTEX]; /* why a slot is baked */
	Sampling sampling;         /* the one the direct slots share */
	bool any_direct;
	SlotPlan() : bake(0), direct_slots(0), any_direct(false) {}
};

static void plan_image_slots(const Material *ma, bool physical, unsigned other_bakes, SlotPlan &p)
{
	unsigned taken = 0;
	unsigned slot_ch[MAX_MTEX] = {0};
	Sampling samp[MAX_MTEX];
	Image *albedo = NULL;
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (!mtex || !mtex->tex || (ma->septex & (1 << i)) || mtex->tex->type != TEX_IMAGE || !mtex->tex->ima) {
			continue;
		}
		const unsigned ch = slot_channels(mtex, physical);
		if (!ch) {
			continue;
		}
		slot_ch[i] = ch;
		std::string why = slot_bake_reason(ma, mtex, physical, samp[i]);
		/* an alpha slot on the colour's own image: its alpha, as it is */
		const bool alpha_only = (mtex->mapto & MAP_ALPHA) && !(mtex->mapto & (MAP_COL | MAP_REF));
		if (why.empty() && alpha_only && mtex->tex->ima != albedo) {
			why = "alpha from another image than the colour's";
		}
		if (why.empty() && !alpha_only && (taken & ch)) {
			why = "another slot in the same channel";
		}
		if (why.empty() && (mtex->mapto & MAP_COL)) {
			albedo = mtex->tex->ima;
		}
		if (!why.empty()) {
			p.why[i] = why;
			p.bake |= ch;
			/* a channel baked takes the slots before it in it too */
			for (int j = 0; j < i; j++) {
				if ((p.direct_slots & (1u << j)) && (slot_ch[j] & ch)) {
					p.direct_slots &= ~(1u << j);
					p.why[j] = "another slot in the same channel";
					p.bake |= slot_ch[j];
				}
			}
		}
		else {
			p.direct_slots |= 1u << i;
		}
		taken |= ch;
	}
	/* the direct ones: one sampling for all, else all baked; a transform
	 * only with nothing else baked (a bake is drawn in the mesh's UVs) */
	bool first = true, agree = true;
	for (int i = 0; i < MAX_MTEX; i++) {
		if (!(p.direct_slots & (1u << i))) {
			continue;
		}
		if (first) {
			p.sampling = samp[i];
			first = false;
		}
		else if (!p.sampling.same(samp[i])) {
			agree = false;
		}
	}
	const bool moved = !first && agree && !p.sampling.identity() && ((p.bake | other_bakes) != 0);
	if (!agree || moved) {
		for (int i = 0; i < MAX_MTEX; i++) {
			if (p.direct_slots & (1u << i)) {
				p.why[i] = agree ? "a transform while something else is baked" :
				                   "its extension, interpolation or transform differs from another slot's";
				p.bake |= slot_ch[i];
			}
		}
		p.direct_slots = 0;
	}
	p.any_direct = p.direct_slots != 0;
}

/* the procedural slots' bakes (BRMX_bake.h) */
static unsigned procedural_bakes(const Material *ma, bool physical)
{
	unsigned b = 0;
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (mtex && mtex->tex && !(ma->septex & (1 << i)) && GPU_texture_is_procedural(mtex->tex)) {
			b |= slot_channels(mtex, physical);
		}
	}
	return b;
}

/* the sampling into the material (Remix's wrap and filter, the UVs) */
static void apply_sampling(const Sampling &smp, MaterialSpec &s)
{
	brb_material_desc &d = s.desc;
	d.wrap_u = smp.wrap_u;
	d.wrap_v = smp.wrap_v;
	d.clamp = (smp.wrap_u == WRAP_CLAMP || smp.wrap_u == WRAP_CLIP) ? 1 : 0;
	d.filter_nearest = smp.nearest ? 1 : 0;
	s.placement = std::string("Remix's wrap ") + wrap_name(smp.wrap_u) +
	              (smp.wrap_v != smp.wrap_u ? std::string(" / ") + wrap_name(smp.wrap_v) : std::string()) +
	              (smp.nearest ? ", no filtering" : ", filtered");
	if (!smp.identity()) {
		s.uv_transform = true;
		memcpy(s.uv_matrix, smp.m, sizeof(s.uv_matrix));
		s.placement += "; scale, offset, repeats or flip in the mesh's UVs";
	}
}

static const char *bake_field(unsigned ch)
{
	if (ch & (1u << BAKE_ALBEDO)) return "albedo";
	if (ch & (1u << BAKE_ROUGHNESS)) return "roughness";
	if (ch & (1u << BAKE_NORMAL)) return "normal";
	if (ch & (1u << BAKE_EMISSIVE)) return "emissive";
	return "metallic";
}

}  /* namespace */

unsigned image_slot_bake_slots(const Material *ma)
{
	if (!ma || (ma->use_nodes && ma->nodetree)) {
		return 0;
	}
	const bool physical = BKE_material_is_physical(ma);
	SlotPlan p;
	plan_image_slots(ma, physical, procedural_bakes(ma, physical), p);
	return p.bake;
}

void apply_uv_transform(const MaterialSpec &s, brb_vertex *v, size_t n)
{
	if (!s.uv_transform) {
		return;
	}
	const float *m = s.uv_matrix;
	for (size_t i = 0; i < n; i++) {
		const float u = v[i].uv[0], w = v[i].uv[1];
		v[i].uv[0] = m[0] * u + m[1] * w + m[2];
		v[i].uv[1] = m[3] * u + m[4] * w + m[5];
	}
}

namespace {

void image_slots(const Material *ma, MaterialSpec &s, bool physical)
{
	brb_material_desc &d = s.desc;
	Image *albedo_image = NULL;
	SlotPlan plan;
	plan_image_slots(ma, physical, procedural_bakes(ma, physical), plan);
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (!mtex || !mtex->tex || (ma->septex & (1 << i))) {
			continue;
		}
		const Tex *tex = mtex->tex;
		const std::string slot = std::string("texture slot ") + (mtex->tex->id.name + 2);
		if (tex->type != TEX_IMAGE || !tex->ima) {
			/* P8.6: procedural textures are baked per mesh (BRMX_bake.h) */
			if ((mtex->mapto & (MAP_COL | MAP_NORM | MAP_EMIT | MAP_HAR)) && !GPU_texture_is_procedural(tex)) {
				note(s, slot + ": not an image or procedural texture, left out");
				mark_slot_fields(s, mtex->mapto, MATCH_NONE, slot + ": not an image or procedural texture");
			}
			continue;
		}
		if (!(plan.direct_slots & (1u << i))) {
			/* P9.4: baked with the channels it feeds (bake_slots) */
			if (!plan.why[i].empty()) {
				const unsigned ch = slot_channels(mtex, physical);
				note(s, slot + ": " + plan.why[i] + ", baked");
				for (int b = 0; b < BAKE_SLOTS; b++) {
					if (ch & (1u << b)) {
						mark(s, bake_field(1u << b), MATCH_APPROX, slot + ": " + plan.why[i] + ", baked into an image");
					}
				}
			}
			continue;
		}
		if ((mtex->mapto & MAP_COL) && !s.textures[TEX_ALBEDO].image) {
			s.textures[TEX_ALBEDO] = texture(tex->ima, BRB_TEXTURE_COLOR);
			albedo_image = tex->ima;
		}
		if ((mtex->mapto & MAP_NORM) && !s.textures[TEX_NORMAL].image) {
			s.textures[TEX_NORMAL] = texture(tex->ima, BRB_TEXTURE_NORMAL);
		}
		if ((mtex->mapto & MAP_EMIT) && !s.textures[TEX_EMISSIVE].image) {
			s.textures[TEX_EMISSIVE] = texture(tex->ima, BRB_TEXTURE_COLOR);
			set3(d.emissive, 1.0f, 1.0f, 1.0f);
			if (d.emissive_intensity <= 0.0f) {
				d.emissive_intensity = mtex->emitfac;
			}
		}
		if ((mtex->mapto & MAP_HAR) && !s.textures[TEX_ROUGHNESS].image) {
			/* Classic: bright = hard = smooth; physical: the roughness */
			s.textures[TEX_ROUGHNESS] = texture(tex->ima, BRB_TEXTURE_SCALAR, 0, physical ? 0 : BRB_TEXTURE_INVERT);
		}
		if (physical && (mtex->mapto & MAP_RAYMIRR) && !s.textures[TEX_METALLIC].image) {
			s.textures[TEX_METALLIC] = texture(tex->ima, BRB_TEXTURE_SCALAR);
		}
		if ((mtex->mapto & MAP_ALPHA) && (tex->ima == albedo_image || tex->ima == s.textures[TEX_ALBEDO].image)) {
			/* the image's alpha is the opacity (a sprite: material alpha 0,
			 * the texture brings it back), as BGE draws it */
			d.opacity = 1.0f;
		}
	}
	if (plan.any_direct) {
		apply_sampling(plan.sampling, s);
	}
}

/* ------------------------------------------------------------------------- */
/* P8.7: the physical material (BKE_material_physical.h), read as the game
 * and F12 read it */

void game_alpha(const Material *ma, brb_material_desc &d)
{
	const int blend = ma->game.alpha_blend;
	d.alpha_blend = (blend == GEMAT_ALPHA || blend == GEMAT_ALPHA_SORT || blend == GEMAT_ADD) ? 1 : 0;
	d.alpha_test = (blend == GEMAT_CLIP) ? 1 : 0;
	if (!d.alpha_blend) {
		d.opacity = 1.0f;
	}
}

bool from_physical(const Material *ma, MaterialSpec &s)
{
	if (!BKE_material_is_physical(ma)) {
		return false;
	}
	MaterialPhysical ph;
	BKE_material_physical_get(ma, &ph);
	brb_material_desc &d = s.desc;
	s.source = MaterialSpec::PHYSICAL;
	s.bake_slots = bake_slots(ma);
	memcpy(d.albedo, ph.base, sizeof(d.albedo));
	d.opacity = ph.alpha;
	d.roughness = ph.roughness;
	d.metallic = ph.metallic;
	if (ph.emission_strength > 0.0f) {
		memcpy(d.emissive, ph.emission, sizeof(d.emissive));
		d.emissive_intensity = ph.emission_strength;
	}
	if (ma->mode & MA_SHLESS) {
		set3(d.albedo, 0.0f, 0.0f, 0.0f);
		memcpy(d.emissive, ph.base, sizeof(d.emissive));
		d.emissive_intensity = 1.0f;
	}
	game_alpha(ma, d);
	if (ma->game.alpha_blend == GEMAT_ADD) {
		d.additive = 1; /* P8.8 */
	}
	if (ph.transmission > 0.0f) {
		/* Remix's glass is all or nothing */
		if (ph.transmission < 1.0f) {
			note(s, "transmission between 0 and 1: Remix draws it as glass from 0.5 up, else opaque");
			mark(s, "transmittance", MATCH_APPROX, "Transmission between 0 and 1: all glass or none");
		}
		if (ph.transmission >= 0.5f) {
			d.type = BRB_MATERIAL_TRANSLUCENT;
			d.ior = std::min(std::max(ph.ior, 1.0f), 3.0f);
			memcpy(d.transmittance, ph.base, sizeof(d.transmittance));
		}
	}
	if (ph.subsurface > 0.0f && d.type != BRB_MATERIAL_TRANSLUCENT) {
		d.subsurface = 1;
		memcpy(d.subsurface_color, ph.base, sizeof(d.subsurface_color));
		memcpy(d.subsurface_radius, ph.subsurface_radius, sizeof(d.subsurface_radius));
	}
	image_slots(ma, s, true);
	shadeless_glow(ma, s, false);
	return true;
}

/* ------------------------------------------------------------------------- */
/* 2. Cycles nodes */

struct Nodes {
	const bNodeTree *tree;
	MaterialSpec &s;

	bNodeSocket *input(const bNode *node, const char *name) const
	{
		for (bNodeSocket *sock = (bNodeSocket *)node->inputs.first; sock; sock = sock->next) {
			if (strcmp(sock->name, name) == 0) {
				return sock;
			}
		}
		return NULL;
	}

	bNodeSocket *input_at(const bNode *node, int index) const
	{
		return (bNodeSocket *)BLI_findlink(&node->inputs, index);
	}

	/* the link into a socket (from the tree's links: sock->link is runtime data) */
	const bNodeLink *link_to(const bNodeSocket *sock) const
	{
		if (!sock) {
			return NULL;
		}
		for (const bNodeLink *l = (const bNodeLink *)tree->links.first; l; l = l->next) {
			if (l->tosock == sock) {
				return l;
			}
		}
		return NULL;
	}

	float value(const bNode *node, const char *name, float def) const
	{
		const bNodeSocket *sock = input(node, name);
		if (!sock || !sock->default_value) {
			return def;
		}
		if (const bNodeLink *l = link_to(sock)) {
			unsupported(node, name, l);
		}
		if (sock->type == SOCK_FLOAT) {
			return ((const bNodeSocketValueFloat *)sock->default_value)->value;
		}
		return def;
	}

	void color(const bNode *node, const char *name, float r[3]) const
	{
		const bNodeSocket *sock = input(node, name);
		if (!sock || !sock->default_value) {
			return;
		}
		if (sock->type == SOCK_RGBA) {
			memcpy(r, ((const bNodeSocketValueRGBA *)sock->default_value)->value, sizeof(float) * 3);
		}
		else if (sock->type == SOCK_VECTOR) {
			memcpy(r, ((const bNodeSocketValueVector *)sock->default_value)->value, sizeof(float) * 3);
		}
	}

	void unsupported(const bNode *node, const char *socket, const bNodeLink *l) const
	{
		s.notes.push_back(std::string(node->name) + " " + socket + ": linked to " + l->fromnode->name +
		                  ", which Remix mode does not read; its own value is used");
		const std::string why = std::string(node->name) + " " + socket + ": " + l->fromnode->name + " is not followed";
		mark(s, socket_field(socket), MATCH_NONE, why);
	}

	/* P8.24: the Remix field a shader socket feeds */
	static const char *socket_field(const char *socket)
	{
		static const struct { const char *socket, *field; } map[] = {
		    {"Base Color", "albedo"}, {"Color", "albedo"}, {"Roughness", "roughness"}, {"Metallic", "metallic"},
		    {"Normal", "normal"}, {"Strength", "emissive"}, {"IOR", "glass"}, {"Transmission", "glass"},
		    {"Subsurface", "subsurface"}, {"Subsurface Color", "subsurface"}, {"Subsurface Radius", "subsurface"},
		    {"Radius", "subsurface"}, {"Scale", "subsurface"}, {"Fac", "opacity"}, {"Anisotropic", "anisotropy"},
		};
		for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
			if (strcmp(map[i].socket, socket) == 0) {
				return map[i].field;
			}
		}
		return "surface";
	}

	/* An Image Texture node behind the socket (directly), or NULL. */
	const bNode *image_node(const bNodeSocket *sock, const char **r_output = NULL) const
	{
		const bNodeLink *l = link_to(sock);
		if (!l || l->fromnode->type != SH_NODE_TEX_IMAGE || !l->fromnode->id) {
			return NULL;
		}
		if (r_output) {
			*r_output = l->fromsock->name;
		}
		return l->fromnode;
	}

	/* P9.4: the image's extension and interpolation as Remix's wrap and
	 * filter (per material: the first image's, noted when another's differ) */
	void image_options(const bNode *img)
	{
		const NodeTexImage *t = (const NodeTexImage *)img->storage;
		if (!t) {
			return;
		}
		const int wrap = t->extension == SHD_IMAGE_EXTENSION_EXTEND ? WRAP_CLAMP :
		                 t->extension == SHD_IMAGE_EXTENSION_CLIP ? WRAP_CLIP : WRAP_REPEAT;
		const bool nearest = t->interpolation == SHD_INTERP_CLOSEST;
		brb_material_desc &d = s.desc;
		if (d.wrap_u != 0) {
			if (d.wrap_u != wrap || (d.filter_nearest != 0) != nearest) {
				note(s, std::string(img->name) + ": its extension or interpolation differs from another image's; "
				                                  "Remix takes one per material (the first image's)");
				mark(s, "filtering and wrapping", MATCH_APPROX, std::string(img->name) + ": differs from another image");
			}
			return;
		}
		d.wrap_u = d.wrap_v = wrap;
		d.clamp = wrap != WRAP_REPEAT ? 1 : 0;
		d.filter_nearest = nearest ? 1 : 0;
		s.placement = std::string("Remix's wrap ") + wrap_name(wrap) + (nearest ? ", no filtering" : ", filtered");
	}

	/* A colour socket: its value, or an image in a texture slot */
	void color_or_image(const bNode *node, const char *name, float r[3], int slot)
	{
		const bNodeSocket *sock = input(node, name);
		const char *out = NULL;
		if (const bNode *img = image_node(sock, &out)) {
			if (strcmp(out, "Color") == 0) {
				s.textures[slot] = texture((Image *)img->id, BRB_TEXTURE_COLOR);
				image_options(img);
				return;
			}
		}
		if (const bNodeLink *l = link_to(sock)) {
			unsupported(node, name, l);
		}
		color(node, name, r);
	}

	/* A value socket: its value, or a one-channel image in a texture slot
	 * (its red channel from Color, its alpha from Alpha) */
	float value_or_image(const bNode *node, const char *name, float def, int slot)
	{
		const bNodeSocket *sock = input(node, name);
		const char *out = NULL;
		if (const bNode *img = image_node(sock, &out)) {
			const bool alpha = strcmp(out, "Alpha") == 0;
			s.textures[slot] = texture((Image *)img->id, BRB_TEXTURE_SCALAR, alpha ? 3 : 0);
			image_options(img);
			const NodeTexImage *t = (const NodeTexImage *)img->storage;
			if (!alpha && t && t->color_space != SHD_COLORSPACE_NONE) {
				note(s, std::string(img->name) + ": a colour (not Non-Color) image drives " + name +
				            "; its values are used as stored");
				mark(s, socket_field(name), MATCH_APPROX, std::string(img->name) + ": a colour image used as values");
			}
			return def;
		}
		return value(node, name, def);
	}

	void normal_map(const bNode *node)
	{
		const bNodeSocket *sock = input(node, "Normal");
		const bNodeLink *l = link_to(sock);
		if (!l) {
			return;
		}
		const bNode *nm = l->fromnode;
		if (nm->type == SH_NODE_NORMAL_MAP) {
			const NodeShaderNormalMap *nms = (const NodeShaderNormalMap *)nm->storage;
			if (nms && nms->space != SHD_NORMAL_MAP_TANGENT) {
				note(s, std::string(nm->name) + ": only tangent-space normal maps are read");
				mark(s, "normal", MATCH_NONE, std::string(nm->name) + ": not tangent space");
				return;
			}
			if (const bNode *img = image_node(input(nm, "Color"))) {
				s.textures[TEX_NORMAL] = texture((Image *)img->id, BRB_TEXTURE_NORMAL);
				image_options(img);
				return;
			}
		}
		unsupported(node, "Normal", l);
	}

	/* the shader node feeding a shader socket */
	const bNode *shader_from(const bNodeSocket *sock) const
	{
		const bNodeLink *l = link_to(sock);
		return l ? l->fromnode : NULL;
	}

	void principled(const bNode *n)
	{
		brb_material_desc &d = s.desc;
		color_or_image(n, "Base Color", d.albedo, TEX_ALBEDO);
		d.metallic = value_or_image(n, "Metallic", 0.0f, TEX_METALLIC);
		d.roughness = value_or_image(n, "Roughness", 0.5f, TEX_ROUGHNESS);
		normal_map(n);
		const float transmission = value(n, "Transmission", 0.0f);
		const float subsurface = value(n, "Subsurface", 0.0f);
		if (transmission > 0.5f) {
			d.type = BRB_MATERIAL_TRANSLUCENT;
			d.ior = value(n, "IOR", 1.45f);
			glass_tint(d.albedo, d.transmittance, s);
			if (s.textures[TEX_ALBEDO].image) {
				note(s, std::string(n->name) + ": glass tinted by an image, the image is left out");
				mark(s, "transmittance", MATCH_NONE, std::string(n->name) + ": the tinting image");
				s.textures[TEX_ALBEDO] = TextureRequest();
			}
		}
		else if (subsurface > 0.0f) {
			d.subsurface = 1;
			color(n, "Subsurface Color", d.subsurface_color);
			float radius[3] = {1.0f, 0.2f, 0.1f};
			color(n, "Subsurface Radius", radius);
			/* Cycles scales the radius by the Subsurface factor */
			for (int i = 0; i < 3; i++) {
				d.subsurface_radius[i] = radius[i] * subsurface;
			}
		}
	}

	void shader(const bNode *n, int depth)
	{
		brb_material_desc &d = s.desc;
		if (!n || depth > 8) {
			return;
		}
		switch (n->type) {
			case SH_NODE_BSDF_PRINCIPLED:
				principled(n);
				return;
			case SH_NODE_BSDF_DIFFUSE:
				color_or_image(n, "Color", d.albedo, TEX_ALBEDO);
				d.roughness = 1.0f;
				d.metallic = 0.0f;
				normal_map(n);
				return;
			case SH_NODE_BSDF_GLOSSY:
				color_or_image(n, "Color", d.albedo, TEX_ALBEDO);
				d.roughness = value_or_image(n, "Roughness", 0.5f, TEX_ROUGHNESS);
				d.metallic = 1.0f;
				normal_map(n);
				return;
			case SH_NODE_BSDF_GLASS:
			case SH_NODE_BSDF_REFRACTION:
				d.type = BRB_MATERIAL_TRANSLUCENT;
				{
					float c[3] = {1.0f, 1.0f, 1.0f};
					color(n, "Color", c);
					glass_tint(c, d.transmittance, s);
				}
				d.ior = value(n, "IOR", 1.45f);
				return;
			case SH_NODE_EMISSION: {
				set3(d.albedo, 0.0f, 0.0f, 0.0f);
				d.roughness = 1.0f;
				float c[3] = {1.0f, 1.0f, 1.0f};
				color_or_image(n, "Color", c, TEX_EMISSIVE);
				memcpy(d.emissive, c, sizeof(c));
				d.emissive_intensity = value(n, "Strength", 1.0f);
				return;
			}
			case SH_NODE_SUBSURFACE_SCATTERING: {
				color_or_image(n, "Color", d.albedo, TEX_ALBEDO);
				d.subsurface = 1;
				memcpy(d.subsurface_color, d.albedo, sizeof(d.albedo));
				float radius[3] = {1.0f, 1.0f, 1.0f};
				color(n, "Radius", radius);
				const float scale = value(n, "Scale", 1.0f);
				for (int i = 0; i < 3; i++) {
					d.subsurface_radius[i] = radius[i] * scale;
				}
				d.roughness = 1.0f;
				normal_map(n);
				return;
			}
			case SH_NODE_BSDF_TRANSPARENT:
				d.opacity = 0.0f;
				d.alpha_blend = 1;
				return;
			case SH_NODE_MIX_SHADER: {
				const bNodeSocket *fac_sock = input_at(n, 0);
				const bNode *a = shader_from(input_at(n, 1));
				const bNode *b = shader_from(input_at(n, 2));
				const bool ta = a && a->type == SH_NODE_BSDF_TRANSPARENT;
				const bool tb = b && b->type == SH_NODE_BSDF_TRANSPARENT;
				if (ta != tb) {
					/* see-through: the other shader, with opacity from the factor
					 * (or from the colour image's alpha) */
					const bNode *other = ta ? b : a;
					shader(other, depth + 1);
					const char *out = NULL;
					const bNode *img = image_node(fac_sock, &out);
					if (img && strcmp(out, "Alpha") == 0 && s.textures[TEX_ALBEDO].image == (Image *)img->id) {
						d.opacity = 1.0f; /* the albedo texture's alpha */
					}
					else {
						const float fac = value(n, "Fac", 0.5f);
						/* factor 0 shows the first shader, 1 the second */
						d.opacity = ta ? fac : 1.0f - fac;
					}
					d.alpha_blend = 1;
					return;
				}
				const bool ea = a && a->type == SH_NODE_EMISSION;
				const bool eb = b && b->type == SH_NODE_EMISSION;
				if (ea != eb) {
					/* a surface that also glows */
					const float fac = value(n, "Fac", 0.5f);
					const bNode *emission = ea ? a : b;
					shader(ea ? b : a, depth + 1);
					float c[3] = {1.0f, 1.0f, 1.0f};
					color(emission, "Color", c);
					memcpy(d.emissive, c, sizeof(c));
					d.emissive_intensity = value(emission, "Strength", 1.0f) * (ea ? 1.0f - fac : fac);
					return;
				}
				const float fac = value(n, "Fac", 0.5f);
				note(s, std::string(n->name) + ": a mix of two surfaces, the stronger one is used");
				mark(s, "surface", MATCH_APPROX, std::string(n->name) + ": the stronger of two surfaces");
				shader(fac < 0.5f ? a : b, depth + 1);
				return;
			}
			case SH_NODE_ADD_SHADER: {
				const bNode *a = shader_from(input_at(n, 0));
				const bNode *b = shader_from(input_at(n, 1));
				if (b && b->type == SH_NODE_EMISSION && a && a->type != SH_NODE_EMISSION) {
					std::swap(a, b);
				}
				if (a && a->type == SH_NODE_EMISSION && b) {
					shader(b, depth + 1);
					float c[3] = {1.0f, 1.0f, 1.0f};
					color(a, "Color", c);
					memcpy(d.emissive, c, sizeof(c));
					d.emissive_intensity = value(a, "Strength", 1.0f);
					return;
				}
				note(s, std::string(n->name) + ": two surfaces added, the first one is used");
				mark(s, "surface", MATCH_APPROX, std::string(n->name) + ": the first of two added surfaces");
				shader(a, depth + 1);
				return;
			}
			default: {
				/* a shader Remix mode does not know: its colour, if it has one */
				note(s, std::string(n->name) + ": not a shader Remix mode reads; a flat colour is used");
				mark(s, "surface", MATCH_NONE, std::string(n->name) + ": a shader Remix mode does not read");
				for (bNodeSocket *sock = (bNodeSocket *)n->inputs.first; sock; sock = sock->next) {
					if (sock->type == SOCK_RGBA && sock->default_value) {
						memcpy(d.albedo, ((const bNodeSocketValueRGBA *)sock->default_value)->value, sizeof(float) * 3);
						break;
					}
				}
				return;
			}
		}
	}
};

/* ------------------------------------------------------------------------- */
/* P9.2: the RTX Remix Material node (node_shader_remix_material.c) */

static int socket_index(const ListBase *list, const bNodeSocket *sock)
{
	int i = 0;
	for (const bNodeSocket *s = (const bNodeSocket *)list->first; s; s = s->next, i++) {
		if (s == sock) {
			return i;
		}
	}
	return -1;
}

/* An input fed straight by an image: Blender Internal's Texture node holding
 * an image, read at the Geometry node's UV (as the tree builder wires it),
 * P9.4: or through a Mapping node (scale and location; no rotation, no
 * clipping), whose transform goes in r_sampling with the image's own
 * settings. r_output: the texture node's output used (0 Value = the
 * image's alpha, 1 Color, 2 Normal). NULL: none, or not so (a bake). */
static Image *remix_direct_image(const bNodeTree *tree, const bNodeLink *link, int *r_output, const Tex **r_tex,
                                 Sampling *r_sampling = NULL)
{
	if (!tree || !link || !link->fromnode || link->fromnode->type != SH_NODE_TEXTURE || !link->fromnode->id) {
		return NULL;
	}
	const bNode *tn = link->fromnode;
	const Tex *tex = (const Tex *)tn->id;
	if (tex->type != TEX_IMAGE || !tex->ima) {
		return NULL;
	}
	const bNodeSocket *vec = (const bNodeSocket *)tn->inputs.first;
	Sampling smp;
	bool uv = false;
	for (int hop = 0; hop < 2 && vec; hop++) {
		const bNodeLink *in = NULL;
		for (const bNodeLink *l = (const bNodeLink *)tree->links.first; l; l = l->next) {
			if (l->tosock == vec) {
				in = l;
				break;
			}
		}
		if (!in || !in->fromnode) {
			break;
		}
		if (in->fromnode->type == SH_NODE_GEOMETRY && in->fromsock && strcmp(in->fromsock->name, "UV") == 0) {
			uv = true;
			break;
		}
		if (hop == 0 && in->fromnode->type == SH_NODE_MAPPING && in->fromnode->storage) {
			/* the Mapping node: size, then location, on the UV (as its GLSL) */
			const TexMapping *tm = (const TexMapping *)in->fromnode->storage;
			if (tm->rot[0] != 0.0f || tm->rot[1] != 0.0f || tm->rot[2] != 0.0f ||
			    (tm->flag & (TEXMAP_CLIP_MIN | TEXMAP_CLIP_MAX)) || tm->type != TEXMAP_TYPE_POINT)
			{
				break;
			}
			smp.m[0] = tm->size[0];
			smp.m[2] = tm->loc[0];
			smp.m[4] = tm->size[1];
			smp.m[5] = tm->loc[1];
			vec = (const bNodeSocket *)in->fromnode->inputs.first;
			continue;
		}
		break;
	}
	if (!uv || !image_sampling(tex, smp, NULL)) {
		return NULL;
	}
	if (r_output) {
		*r_output = socket_index(&tn->outputs, link->fromsock);
	}
	if (r_tex) {
		*r_tex = tex;
	}
	if (r_sampling) {
		*r_sampling = smp;
	}
	return tex->ima;
}

/* P9.4: the inputs fed by images straight and their sampling */
struct NodeImages {
	const bNodeLink *links[SH_REMIX_IN_COUNT];
	Sampling sampling[SH_REMIX_IN_COUNT];
	bool direct[SH_REMIX_IN_COUNT];
};

static void node_images(const Material *ma, const bNode *node, NodeImages &r)
{
	const bNodeTree *tree = ma->nodetree;
	for (int i = 0; i < SH_REMIX_IN_COUNT; i++) {
		r.links[i] = BKE_remix_node_link(tree, node, i);
		r.direct[i] = r.links[i] && remix_direct_image(tree, r.links[i], NULL, NULL, &r.sampling[i]);
	}
}

/* P9.4: the bake slot of an input (-1: none) */
static int input_bake_slot(int input)
{
	switch (input) {
		case SH_REMIX_IN_ALBEDO: case SH_REMIX_IN_OPACITY: return BAKE_ALBEDO;
		case SH_REMIX_IN_ROUGHNESS: return BAKE_ROUGHNESS;
		case SH_REMIX_IN_METALLIC: return BAKE_METALLIC;
		case SH_REMIX_IN_NORMAL: return BAKE_NORMAL;
		case SH_REMIX_IN_EMISSIVE: return BAKE_EMISSIVE;
	}
	return -1;
}

}  /* namespace */

unsigned remix_node_disagreeing_slots(const Material *ma)
{
	const bNode *node = BKE_material_remix_node(ma);
	if (!node) {
		return 0;
	}
	NodeImages ni;
	node_images(ma, node, ni);
	const Sampling *first = NULL;
	bool agree = true, other_bakes = false;
	for (int i = 0; i < SH_REMIX_IN_COUNT; i++) {
		if (ni.links[i] && !ni.direct[i] && (input_bake_slot(i) >= 0 || i == SH_REMIX_IN_EMISSIVE_INTENSITY)) {
			other_bakes = true;
		}
		if (!ni.direct[i] || input_bake_slot(i) < 0) {
			continue;
		}
		if (!first) {
			first = &ni.sampling[i];
		}
		else if (!first->same(ni.sampling[i])) {
			agree = false;
		}
	}
	/* agreeing, and no transform or nothing else baked (a bake is drawn in
	 * the mesh's own UVs): sent as they are */
	if (agree && (!first || first->identity() || !other_bakes)) {
		return 0;
	}
	unsigned slots = 0;
	for (int i = 0; i < SH_REMIX_IN_COUNT; i++) {
		if (ni.direct[i] && input_bake_slot(i) >= 0) {
			slots |= 1u << input_bake_slot(i);
		}
	}
	return slots;
}

namespace {

unsigned node_bake_slots(const Material *ma, bool *r_alpha)
{
	const bNode *node = BKE_material_remix_node(ma);
	unsigned slots = 0;
	if (r_alpha) {
		*r_alpha = false;
	}
	if (!node) {
		return 0;
	}
	const bNodeTree *tree = ma->nodetree;
	struct In {
		int input, slot;
	};
	static const In ins[] = {{SH_REMIX_IN_ALBEDO, BAKE_ALBEDO}, {SH_REMIX_IN_ROUGHNESS, BAKE_ROUGHNESS},
	                         {SH_REMIX_IN_METALLIC, BAKE_METALLIC}, {SH_REMIX_IN_NORMAL, BAKE_NORMAL},
	                         {SH_REMIX_IN_EMISSIVE, BAKE_EMISSIVE}, {SH_REMIX_IN_EMISSIVE_INTENSITY, BAKE_EMISSIVE}};
	for (size_t i = 0; i < sizeof(ins) / sizeof(ins[0]); i++) {
		const bNodeLink *link = BKE_remix_node_link(tree, node, ins[i].input);
		if (link && !remix_direct_image(tree, link, NULL, NULL)) {
			slots |= 1u << ins[i].slot;
		}
	}
	/* P9.4: images straight whose settings disagree: baked */
	slots |= remix_node_disagreeing_slots(ma);
	/* the opacity: the albedo image's alpha (its texture node's Value), or
	 * baked into the albedo's alpha */
	const bNodeLink *op = BKE_remix_node_link(tree, node, SH_REMIX_IN_OPACITY);
	if (op) {
		int out = -1;
		Image *ima = remix_direct_image(tree, op, &out, NULL);
		Image *alb = remix_direct_image(tree, BKE_remix_node_link(tree, node, SH_REMIX_IN_ALBEDO), NULL, NULL);
		if (!(ima && ima == alb && out == 0) || (slots & (1u << BAKE_ALBEDO))) {
			slots |= 1u << BAKE_ALBEDO;
			if (r_alpha) {
				*r_alpha = true;
			}
		}
	}
	return slots;
}

bool from_remix_node(const Material *ma, MaterialSpec &s)
{
	const bNode *node = BKE_material_remix_node(ma);
	if (!node) {
		return false;
	}
	const bNodeTree *tree = ma->nodetree;
	brb_material_desc &d = s.desc;
	MaterialPhysical ph;
	BKE_material_physical_get(ma, &ph);
	s.source = MaterialSpec::REMIX_NODE;
	memcpy(d.albedo, ph.base, sizeof(d.albedo));
	d.opacity = ph.alpha;
	d.roughness = ph.roughness;
	d.metallic = ph.metallic;
	float emissive[3];
	BKE_remix_node_color(node, SH_REMIX_IN_EMISSIVE, emissive);
	const float intensity = BKE_remix_node_value(node, SH_REMIX_IN_EMISSIVE_INTENSITY);
	if (intensity > 0.0f) {
		memcpy(d.emissive, emissive, sizeof(d.emissive));
		d.emissive_intensity = intensity;
	}
	if (ma->mode & MA_SHLESS) {
		set3(d.albedo, 0.0f, 0.0f, 0.0f);
		memcpy(d.emissive, ph.base, sizeof(d.emissive));
		d.emissive_intensity = 1.0f;
	}
	/* blending: the material's game transparency (the node's Blend) */
	game_alpha(ma, d);
	if (ma->game.alpha_blend == GEMAT_ADD) {
		d.additive = 1;
	}
	if (d.alpha_test) {
		d.alpha_cutoff = std::min(std::max(node->custom4, 0.0f), 1.0f);
	}
	if (node->custom1 & SH_NODE_REMIX_TRANSLUCENT) {
		d.type = BRB_MATERIAL_TRANSLUCENT;
		d.ior = std::min(std::max(BKE_remix_node_value(node, SH_REMIX_IN_IOR), 1.0f), 3.0f);
		BKE_remix_node_color(node, SH_REMIX_IN_TRANSMITTANCE, d.transmittance);
		d.transmittance_distance = std::max(BKE_remix_node_value(node, SH_REMIX_IN_TRANSMITTANCE_DISTANCE), 0.001f);
		d.thin_walled = (node->custom1 & SH_NODE_REMIX_THIN_WALLED) ? 1 : 0;
		d.thin_wall_thickness = BKE_remix_node_value(node, SH_REMIX_IN_THIN_WALL_THICKNESS);
	}
	else if (ph.subsurface > 0.0f) {
		d.subsurface = 1;
		BKE_remix_node_color(node, SH_REMIX_IN_SUBSURFACE_COLOR, d.subsurface_color);
		memcpy(d.subsurface_radius, ph.subsurface_radius, sizeof(d.subsurface_radius));
	}
	d.anisotropy = std::min(std::max(BKE_remix_node_value(node, SH_REMIX_IN_ANISOTROPY), -1.0f), 1.0f);
	const float film = BKE_remix_node_value(node, SH_REMIX_IN_THIN_FILM);
	if (film > 0.0f && d.type != BRB_MATERIAL_TRANSLUCENT) {
		d.thin_film = 1;
		d.thin_film_thickness = film;
	}
	d.displace_in = BKE_remix_node_value(node, SH_REMIX_IN_DISPLACE_IN);
	d.displace_out = BKE_remix_node_value(node, SH_REMIX_IN_DISPLACE_OUT);
	s.categories |= (unsigned)(node->custom1 >> SH_NODE_REMIX_CATEGORY_SHIFT) & 0x7ffu;
	if (node->custom1 & SH_NODE_REMIX_PARTICLE) {
		s.particle = true;
	}

	/* what is wired in: images straight, the rest baked (bake_slots) */
	bool baked_alpha = false;
	s.bake_slots = node_bake_slots(ma, &baked_alpha);
	/* P9.4: the images sent as they are share one sampling (else they were
	 * baked: node_bake_slots) */
	Sampling smp;
	bool have_smp = false, smp_differs = false;
	struct In {
		int input;
		const char *field;
		int tex, kind;
	};
	static const In ins[] = {
	    {SH_REMIX_IN_ALBEDO, "albedo", TEX_ALBEDO, BRB_TEXTURE_COLOR},
	    {SH_REMIX_IN_OPACITY, "opacity", -1, 0},
	    {SH_REMIX_IN_ROUGHNESS, "roughness", TEX_ROUGHNESS, BRB_TEXTURE_SCALAR},
	    {SH_REMIX_IN_METALLIC, "metallic", TEX_METALLIC, BRB_TEXTURE_SCALAR},
	    {SH_REMIX_IN_NORMAL, "normal", TEX_NORMAL, BRB_TEXTURE_NORMAL},
	    {SH_REMIX_IN_EMISSIVE, "emissive", TEX_EMISSIVE, BRB_TEXTURE_COLOR},
	    {SH_REMIX_IN_EMISSIVE_INTENSITY, "emissive", -1, 0},
	    {SH_REMIX_IN_HEIGHT, "height", TEX_HEIGHT, BRB_TEXTURE_SCALAR},
	    {SH_REMIX_IN_TRANSMITTANCE, "transmittance", TEX_TRANSMITTANCE, BRB_TEXTURE_COLOR},
	    {SH_REMIX_IN_SUBSURFACE_COLOR, "sss_albedo", TEX_SSS_ALBEDO, BRB_TEXTURE_COLOR},
	    {SH_REMIX_IN_DISPLACE_IN, "height", -1, 0},
	    {SH_REMIX_IN_DISPLACE_OUT, "height", -1, 0},
	    {SH_REMIX_IN_ANISOTROPY, "anisotropy", -1, 0},
	    {SH_REMIX_IN_THIN_FILM, "thin film", -1, 0},
	    {SH_REMIX_IN_IOR, "glass", -1, 0},
	    {SH_REMIX_IN_TRANSMITTANCE_DISTANCE, "transmittance", -1, 0},
	    {SH_REMIX_IN_THIN_WALL_THICKNESS, "glass", -1, 0},
	    {SH_REMIX_IN_SUBSURFACE, "subsurface", -1, 0},
	    {SH_REMIX_IN_SUBSURFACE_RADIUS, "sss_radius", -1, 0},
	};
	for (size_t i = 0; i < sizeof(ins) / sizeof(ins[0]); i++) {
		const In &in = ins[i];
		const bNodeLink *link = BKE_remix_node_link(tree, node, in.input);
		if (!link) {
			continue;
		}
		const std::string what = std::string("the node's ") + link->tosock->name;
		int out = -1;
		const Tex *tex = NULL;
		Sampling one;
		Image *ima = remix_direct_image(tree, link, &out, &tex, &one);
		if (in.input == SH_REMIX_IN_OPACITY) {
			/* the albedo image's alpha (else baked with the albedo) */
			if (!baked_alpha && (d.alpha_blend || d.alpha_test)) {
				d.opacity = 1.0f;
			}
			continue;
		}
		if (in.input == SH_REMIX_IN_EMISSIVE_INTENSITY) {
			/* baked with the colour (bake_slots) */
			continue;
		}
		if (ima && in.tex >= 0) {
			int channel = 0, flags = 0;
			if (in.kind == BRB_TEXTURE_SCALAR && out == 0) {
				channel = 3; /* the texture node's Value: the image's alpha */
			}
			if (in.kind == BRB_TEXTURE_NORMAL && (node->custom1 & SH_NODE_REMIX_NORMAL_DIRECTX)) {
				flags |= BRMX_TEXTURE_FLIP_GREEN;
			}
			if (in.kind == BRB_TEXTURE_NORMAL && out != 1) {
				note(s, what + ": a normal map is read from the texture's Color");
				mark(s, in.field, MATCH_APPROX, what + ": not the texture's Color");
			}
			s.textures[in.tex] = texture(ima, in.kind, channel, flags);
			const int bslot = input_bake_slot(in.input);
			if (bslot < 0 || !(s.bake_slots & (1u << bslot))) {
				if (!have_smp) {
					smp = one;
					have_smp = true;
				}
				else if (!smp.same(one)) {
					smp_differs = true;
				}
			}
			if (in.input == SH_REMIX_IN_ALBEDO) {
				set3(d.albedo, 1.0f, 1.0f, 1.0f);
			}
			if (in.input == SH_REMIX_IN_EMISSIVE) {
				set3(d.emissive, 1.0f, 1.0f, 1.0f);
			}
			continue;
		}
		const bool bakeable = in.input == SH_REMIX_IN_ALBEDO || in.input == SH_REMIX_IN_ROUGHNESS ||
		                      in.input == SH_REMIX_IN_METALLIC || in.input == SH_REMIX_IN_NORMAL ||
		                      in.input == SH_REMIX_IN_EMISSIVE;
		if (!bakeable) {
			note(s, what + ": only an image texture read by UV reaches Remix here; its own value is sent");
			mark(s, in.field, MATCH_NONE, what + ": not an image texture read by UV");
		}
	}
	if (have_smp) {
		Sampling use = smp;
		if (s.bake_slots && !use.identity()) {
			/* left with a transform beside a bake (an input that is not
			 * baked: height, transmittance, subsurface): drawn in the mesh's
			 * own UVs */
			const Sampling plain;
			memcpy(use.m, plain.m, sizeof(use.m));
			note(s, "an image's scale, offset, repeats or flip are not sent beside a baked input");
			mark(s, "filtering and wrapping", MATCH_APPROX, "a transform beside a bake: the mesh's own UVs");
		}
		if (smp_differs) {
			note(s, "images that cannot be baked differ in extension, interpolation or transform; the first's is used");
			mark(s, "filtering and wrapping", MATCH_APPROX, "images differ: the first image's settings");
		}
		apply_sampling(use, s);
	}
	return true;
}

const bNode *cycles_output(const bNodeTree *tree)
{
	const bNode *any = NULL;
	for (const bNode *n = (const bNode *)tree->nodes.first; n; n = n->next) {
		if (n->type == SH_NODE_OUTPUT_MATERIAL) {
			if (n->flag & NODE_DO_OUTPUT) {
				return n;
			}
			any = any ? any : n;
		}
	}
	return any;
}

bool from_nodes(const Material *ma, MaterialSpec &s)
{
	if (!ma->use_nodes || !ma->nodetree) {
		return false;
	}
	const bNode *out = cycles_output(ma->nodetree);
	if (!out) {
		return false;
	}
	s.source = MaterialSpec::NODES;
	brb_material_desc &d = s.desc;
	/* Cycles' defaults, not Blender Internal's */
	set3(d.albedo, 0.8f, 0.8f, 0.8f);
	d.roughness = 0.5f;
	d.metallic = 0.0f;
	d.opacity = 1.0f;
	d.emissive_intensity = 0.0f;
	d.alpha_blend = 0;
	d.alpha_test = 0;
	Nodes nodes = {ma->nodetree, s};
	const bNodeSocket *surface = nodes.input(out, "Surface");
	const bNode *shader = nodes.shader_from(surface);
	if (!shader) {
		note(s, "nothing is linked to the Material Output's Surface; Cycles' grey is used");
		return true;
	}
	nodes.shader(shader, 0);
	/* the game's own transparency setting still asks for blending */
	const int blend = ma->game.alpha_blend;
	if (blend == GEMAT_CLIP && !d.alpha_blend) {
		d.alpha_test = 1;
	}
	return true;
}

/* ------------------------------------------------------------------------- */
/* P8.24: the material's Remix block (ID property group "bge_remix", the
 * RTX Remix panel; properties_material.py's BGERemixMaterial) */

struct Block {
	IDProperty *g;

	IDProperty *get(const char *name) const
	{
		return g ? IDP_GetPropertyFromGroup(g, name) : NULL;
	}
	static int as_int(IDProperty *p, int def)
	{
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
	static float as_float(IDProperty *p, float def)
	{
		if (!p) {
			return def;
		}
		switch (p->type) {
			case IDP_INT: return (float)IDP_Int(p);
			case IDP_FLOAT: return IDP_Float(p);
			case IDP_DOUBLE: return (float)IDP_Double(p);
		}
		return def;
	}
	static bool as_float3(IDProperty *p, float r[3])
	{
		if (!p || p->type != IDP_ARRAY || p->len < 3) {
			return false;
		}
		for (int i = 0; i < 3; i++) {
			if (p->subtype == IDP_FLOAT) {
				r[i] = ((const float *)IDP_Array(p))[i];
			}
			else if (p->subtype == IDP_DOUBLE) {
				r[i] = (float)((const double *)IDP_Array(p))[i];
			}
			else {
				return false;
			}
		}
		return true;
	}
	int i(const char *name, int def = 0) const { return as_int(get(name), def); }
	float f(const char *name, float def) const { return as_float(get(name), def); }
	bool f3(const char *name, float r[3]) const { return as_float3(get(name), r); }
	Block sub(const char *name) const
	{
		IDProperty *p = get(name);
		Block b = {(p && p->type == IDP_GROUP) ? p : NULL};
		return b;
	}
	Image *image(const char *name) const
	{
		IDProperty *p = get(name);
		if (p && p->type == IDP_ID && IDP_Id(p) && GS(IDP_Id(p)->name) == ID_IM) {
			return (Image *)IDP_Id(p);
		}
		return NULL;
	}
};

Block remix_block(const Material *ma)
{
	IDProperty *p = prop(ma, "bge_remix");
	Block b = {(p && p->type == IDP_GROUP) ? p : NULL};
	return b;
}

/* The slots of the block: their names, Remix's field, and what they hold */
struct SlotInfo {
	const char *name;  /* the block's sub group, and the field in the match */
	int tex;           /* TextureSlot, -1: a value only */
	int kind;          /* the default compression (brb_texture_kind) */
	int value;         /* 0 none, 1 a number, 3 a colour or vector */
	/* the panel's defaults (properties_material.py, REMIX_SLOTS) */
	float def_value;
	float def_color[3];
};

const SlotInfo SLOTS[] = {
    {"albedo", TEX_ALBEDO, BRB_TEXTURE_COLOR, 3, 0.0f, {0.8f, 0.8f, 0.8f}},
    {"opacity", -1, 0, 1, 1.0f, {0.0f, 0.0f, 0.0f}},
    {"roughness", TEX_ROUGHNESS, BRB_TEXTURE_SCALAR, 1, 0.5f, {0.0f, 0.0f, 0.0f}},
    {"metallic", TEX_METALLIC, BRB_TEXTURE_SCALAR, 1, 0.0f, {0.0f, 0.0f, 0.0f}},
    {"normal", TEX_NORMAL, BRB_TEXTURE_NORMAL, 0, 0.0f, {0.0f, 0.0f, 0.0f}},
    {"tangent", TEX_TANGENT, BRB_TEXTURE_COLOR, 0, 0.0f, {0.0f, 0.0f, 0.0f}},
    {"emissive", TEX_EMISSIVE, BRB_TEXTURE_COLOR, 3, 0.0f, {1.0f, 1.0f, 1.0f}},
    {"height", TEX_HEIGHT, BRB_TEXTURE_SCALAR, 0, 0.0f, {0.0f, 0.0f, 0.0f}},
    {"transmittance", TEX_TRANSMITTANCE, BRB_TEXTURE_COLOR, 3, 0.0f, {1.0f, 1.0f, 1.0f}},
    {"sss_transmittance", TEX_SSS_TRANSMITTANCE, BRB_TEXTURE_COLOR, 3, 0.0f, {0.8f, 0.8f, 0.8f}},
    {"sss_thickness", TEX_SSS_THICKNESS, BRB_TEXTURE_SCALAR, 0, 0.0f, {0.0f, 0.0f, 0.0f}},
    {"sss_albedo", TEX_SSS_ALBEDO, BRB_TEXTURE_COLOR, 0, 0.0f, {0.0f, 0.0f, 0.0f}},
    {"sss_radius", TEX_SSS_RADIUS, BRB_TEXTURE_COLOR, 3, 0.0f, {1.0f, 0.2f, 0.1f}},
};
const int SLOT_COUNT = (int)(sizeof(SLOTS) / sizeof(SLOTS[0]));

/* One slot's image as a texture request, with its own settings */
TextureRequest slot_texture(const Block &slot, Image *ima, const SlotInfo &info, MaterialSpec &s)
{
	TextureRequest t = texture(ima, info.kind);
	/* compression: 0 automatic, 1 BC7, 2 BC6H, 3 BC4, 4 BC5 */
	static const int kinds[5] = {-1, BRB_TEXTURE_COLOR, BRB_TEXTURE_HDR, BRB_TEXTURE_SCALAR, BRB_TEXTURE_NORMAL};
	const int comp = slot.i("compression");
	if (comp > 0 && comp < 5) {
		t.kind = kinds[comp];
		if (t.kind != info.kind) {
			mark(s, info.name, MATCH_APPROX, "compressed in another format than Remix reads this field in");
		}
	}
	/* channel: 0 automatic (red, or the image's colour), 1..4 R, G, B, A */
	const int channel = slot.i("channel");
	if (channel > 0 && channel <= 4) {
		t.channel = channel - 1;
	}
	/* colour space: 0 the image's own, 1 sRGB, 2 linear */
	const int cs = slot.i("colorspace");
	const bool data = IMB_colormanagement_space_name_is_data(ima->colorspace_settings.name);
	if (t.kind == BRB_TEXTURE_COLOR && (cs == 2 || (cs == 0 && data && info.tex != TEX_TANGENT))) {
		t.flags |= BRB_TEXTURE_LINEAR;
	}
	if (info.tex == TEX_TANGENT || info.tex == TEX_SSS_RADIUS) {
		/* directions and lengths: the values as stored */
		t.flags |= (cs == 1) ? 0 : BRB_TEXTURE_LINEAR;
	}
	t.max_size = std::max(slot.i("max_size"), 0);
	return t;
}

void apply_remix_block(const Material *ma, MaterialSpec &s)
{
	const Block b = remix_block(ma);
	if (!b.g) {
		return;
	}
	/* the categories hold whether or not the rest is in force */
	s.categories = (unsigned)b.i("categories");
	if (!b.i("use")) {
		return;
	}
	brb_material_desc &d = s.desc;
	const std::string panel = "set in the RTX Remix panel";
	for (int k = 0; k < SLOT_COUNT; k++) {
		const SlotInfo &info = SLOTS[k];
		const Block slot = b.sub(info.name);
		if (!slot.g || !slot.i("use")) {
			continue;
		}
		s.marks[info.name] = std::make_pair((int)MATCH_EXACT, panel);
		float c[3];
		memcpy(c, info.def_color, sizeof(c));
		const bool has_color = info.value == 3;
		slot.f3("color", c);
		const float v = slot.f("value", info.def_value);
		switch (k) {
			case 0: if (has_color) { memcpy(d.albedo, c, sizeof(c)); } break;
			case 1: d.opacity = std::min(std::max(v, 0.0f), 1.0f); break;
			case 2: d.roughness = std::min(std::max(v, 0.0f), 1.0f); break;
			case 3: d.metallic = std::min(std::max(v, 0.0f), 1.0f); break;
			case 6:
				if (has_color) {
					memcpy(d.emissive, c, sizeof(c));
				}
				d.emissive_intensity = std::max(b.f("emissive_intensity", 1.0f), 0.0f);
				break;
			case 8: if (has_color) { memcpy(d.transmittance, c, sizeof(c)); } break;
			case 9: if (has_color) { memcpy(d.subsurface_color, c, sizeof(c)); } break;
			case 12: if (has_color) { memcpy(d.subsurface_radius, c, sizeof(c)); } break;
		}
		if (info.tex < 0) {
			continue;
		}
		Image *ima = slot.image("image");
		/* the slot is the block's, with or without an image */
		s.textures[info.tex] = ima ? slot_texture(slot, ima, info, s) : TextureRequest();
		s.locked |= 1u << info.tex;
		/* with an image, the image is the colour (as with a bake) */
		if (ima && info.tex == TEX_ALBEDO) {
			set3(d.albedo, 1.0f, 1.0f, 1.0f);
		}
		if (ima && info.tex == TEX_EMISSIVE) {
			set3(d.emissive, 1.0f, 1.0f, 1.0f);
		}
		if (info.tex == TEX_TANGENT && ima) {
			mark(s, "tangent", MATCH_APPROX, "Remix's tangent map format is not checked yet");
		}
	}
	/* glass */
	if (b.i("use_glass")) {
		d.type = BRB_MATERIAL_TRANSLUCENT;
		d.ior = std::min(std::max(b.f("ior", 1.5f), 1.0f), 3.0f);
		d.transmittance_distance = std::max(b.f("transmittance_distance", GLASS_TINT_DISTANCE), 0.001f);
		d.thin_walled = b.i("thin_walled");
		d.thin_wall_thickness = std::max(b.f("thin_wall_thickness", 0.001f), 0.0f);
		d.use_diffuse_layer = b.i("diffuse_layer");
		d.alpha_blend = 0;
		d.additive = 0;
		s.marks["glass"] = std::make_pair((int)MATCH_EXACT, panel);
		s.marks.erase("transmittance");
	}
	/* subsurface */
	if (b.i("use_subsurface") && d.type != BRB_MATERIAL_TRANSLUCENT) {
		d.subsurface = 1;
		d.subsurface_volumetric = b.i("subsurface_volumetric");
		d.subsurface_distance = std::max(b.f("subsurface_distance", 1.0f), 0.0f);
		d.subsurface_anisotropy = b.f("subsurface_anisotropy", 0.0f);
		d.subsurface_radius_scale = std::max(b.f("subsurface_radius_scale", 1.0f), 0.0f);
		s.marks["subsurface"] = std::make_pair((int)MATCH_EXACT, panel);
	}
	if (b.i("use_anisotropy")) {
		d.anisotropy = std::min(std::max(b.f("anisotropy", 0.0f), -1.0f), 1.0f);
		s.marks["anisotropy"] = std::make_pair((int)MATCH_EXACT, panel);
	}
	if (b.i("use_thin_film")) {
		d.thin_film = 1;
		d.thin_film_thickness = std::max(b.f("thin_film_thickness", 400.0f), 0.0f);
		d.alpha_is_thin_film = b.i("alpha_is_thin_film");
		s.marks["thin film"] = std::make_pair((int)MATCH_EXACT, panel);
	}
	if (b.i("use_displacement")) {
		d.displace_in = std::max(b.f("displace_in", 0.05f), 0.0f);
		d.displace_out = std::max(b.f("displace_out", 0.0f), 0.0f);
		s.marks["height"] = std::make_pair((int)MATCH_EXACT, panel);
	}
	/* blending: 0 automatic, 1 none, 2 alpha, 3 additive */
	switch (b.i("blend")) {
		case 1: d.blend_type = -1; d.alpha_blend = 0; d.additive = 0; break;
		case 2: d.blend_type = 1; d.alpha_blend = 1; d.additive = 0; break;
		case 3: d.blend_type = 9; d.alpha_blend = 1; d.additive = 1; break; /* as P8.8's emissive blending (8) */
	}
	d.inverted_blend = b.i("inverted_blend");
	/* alpha test: 0 automatic, else Remix's AlphaTestType + 1 */
	const int at = b.i("alpha_test");
	if (at > 0 && at <= 8) {
		d.alpha_test_type = at;
		d.alpha_cutoff = std::min(std::max(b.f("alpha_cutoff", 0.5f), 0.0f), 1.0f);
	}
	/* filtering: 0 automatic, 1 nearest, 2 linear */
	const int filter = b.i("filter");
	if (filter == 1 || filter == 2) {
		d.filter_nearest = filter == 1 ? 1 : 0;
	}
	/* wrapping: 0 automatic, else Remix's wrap mode + 1 */
	/* (P9.4: automatic keeps what the textures asked for) */
	const int wu = std::min(std::max(b.i("wrap_u"), 0), 4), wv = std::min(std::max(b.i("wrap_v"), 0), 4);
	if (wu > 0) {
		d.wrap_u = wu;
	}
	if (wv > 0) {
		d.wrap_v = wv;
	}
	if (filter == 1 || filter == 2 || wu > 0 || wv > 0) {
		s.placement = "set in the RTX Remix panel";
	}
}

}  /* namespace */

/* P9.2 (BRMX_material.h) */
unsigned remix_node_bake_slots(const Material *ma, bool *r_alpha)
{
	return node_bake_slots(ma, r_alpha);
}

void translate_material(const Material *ma, MaterialSpec &r_spec)
{
	defaults(r_spec);
	if (!ma) {
		return;
	}
	/* whatever the material's source: sprites and particles */
	r_spec.particle = prop_int(ma, "bge_remix_particle", 0) != 0 ||
	                  (ma->game.face_orientation & GEMAT_HALO) != 0;
	/* P8.7: the RTX Remix panel's values are the physical material's now
	 * (BKE_material_physical_from_remix_panel) */
	/* P9.2: an RTX Remix material first */
	if (!from_remix_node(ma, r_spec) && !from_nodes(ma, r_spec) && !from_physical(ma, r_spec)) {
		from_blender_internal(ma, r_spec);
	}
	/* P8.24: the Remix block over the translation */
	apply_remix_block(ma, r_spec);
}

bool uses_face_image(const Material *ma)
{
	return ma && (ma->mode & MA_FACETEXTURE);
}

void apply_face_image(const Material *ma, Image *face_image, MaterialSpec &r_spec)
{
	if (!uses_face_image(ma) || !face_image) {
		return;
	}
	if (r_spec.source == MaterialSpec::NODES || r_spec.source == MaterialSpec::REMIX_NODE) {
		/* as Blender Internal and the game: the node tree decides */
		return;
	}
	if (r_spec.locked & (1u << TEX_ALBEDO)) {
		/* P8.24: the Remix block's albedo */
		return;
	}
	brb_material_desc &d = r_spec.desc;
	if (r_spec.textures[TEX_ALBEDO].image) {
		/* a texture slot (or the panel's image) paints over the face image,
		 * as the game's shader mixes it on top */
		note(r_spec, "a texture slot gives the colour; the face image is left out");
		return;
	}
	r_spec.textures[TEX_ALBEDO] = texture(face_image, BRB_TEXTURE_COLOR);
	if (r_spec.source == MaterialSpec::BLENDER_INTERNAL) {
		/* the image replaces the material colour */
		set3(d.albedo, ma->ref, ma->ref, ma->ref);
	}
	if ((ma->mode & MA_FACETEXTURE_ALPHA) && (d.alpha_blend || d.alpha_test)) {
		/* the image's alpha is the opacity */
		d.opacity = 1.0f;
	}
	shadeless_glow(ma, r_spec, true);
}

bool remix_block_used(const Material *ma)
{
	return ma && remix_block(ma).i("use") != 0;
}

bool nodes_need_bake(const Material *ma)
{
	if (!ma || !ma->use_nodes || !ma->nodetree || !cycles_output(ma->nodetree)) {
		return false;
	}
	/* Bake Nodes: 0 automatic, 1 always, 2 never */
	const int mode = remix_block(ma).i("bake_nodes");
	if (mode == 1 || mode == 2) {
		return mode == 1;
	}
	/* automatic: when the translation left out or approximated part of the
	 * surface (the translator's marks; glass tinting is not a reason) */
	MaterialSpec s;
	defaults(s);
	from_nodes(ma, s);
	for (std::map<std::string, std::pair<int, std::string> >::const_iterator it = s.marks.begin();
	     it != s.marks.end(); ++it)
	{
		const std::string &f = it->first;
		if (f == "albedo" || f == "roughness" || f == "metallic" || f == "normal" || f == "emissive" ||
		    f == "opacity" || f == "surface")
		{
			return true;
		}
	}
	return false;
}

void material_match(const Material *ma, const MaterialSpec &s, std::vector<FieldMatch> &r)
{
	r.clear();
	const brb_material_desc &d = s.desc;
	const bool glass = d.type == BRB_MATERIAL_TRANSLUCENT;
	static const char *source_names[] = {"Blender's default material", "the RTX Remix panel", "the Cycles nodes",
	                                     "Blender Internal's settings", "the physical material",
	                                     "the RTX Remix Material node"};
	const std::string from = std::string("from ") + source_names[s.source];
	const std::string unused = "not used (Remix's default)";
	struct Row {
		const char *field;
		int tex;    /* TextureSlot or -1 */
		bool used;  /* the material has it */
	};
	const Row rows[] = {
	    {"albedo", TEX_ALBEDO, !glass},
	    {"opacity", -1, d.alpha_blend || d.alpha_test},
	    {"roughness", TEX_ROUGHNESS, !glass},
	    {"metallic", TEX_METALLIC, !glass},
	    {"normal", TEX_NORMAL, s.textures[TEX_NORMAL].wanted()},
	    {"tangent", TEX_TANGENT, s.textures[TEX_TANGENT].wanted()},
	    {"anisotropy", -1, d.anisotropy != 0.0f},
	    {"emissive", TEX_EMISSIVE, d.emissive_intensity > 0.0f},
	    {"height", TEX_HEIGHT, s.textures[TEX_HEIGHT].wanted()},
	    {"thin film", -1, d.thin_film != 0},
	    {"glass", -1, glass},
	    {"transmittance", TEX_TRANSMITTANCE, glass},
	    {"subsurface", -1, d.subsurface != 0},
	    {"sss_transmittance", TEX_SSS_TRANSMITTANCE, d.subsurface != 0},
	    {"sss_thickness", TEX_SSS_THICKNESS, s.textures[TEX_SSS_THICKNESS].wanted()},
	    {"sss_albedo", TEX_SSS_ALBEDO, s.textures[TEX_SSS_ALBEDO].wanted()},
	    {"sss_radius", TEX_SSS_RADIUS, d.subsurface != 0},
	    {"blending", -1, d.alpha_blend || d.additive || d.blend_type != 0},
	    {"alpha test", -1, d.alpha_test || d.alpha_test_type != 0},
	    {"filtering and wrapping", -1, true},
	    {"categories", -1, s.categories != 0},
	};
	const std::map<std::string, std::pair<int, std::string> >::const_iterator none = s.marks.end();
	const std::map<std::string, std::pair<int, std::string> >::const_iterator surface = s.marks.find("surface");
	for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		const Row &row = rows[i];
		FieldMatch m;
		m.field = row.field;
		m.level = MATCH_EXACT;
		std::map<std::string, std::pair<int, std::string> >::const_iterator it = s.marks.find(row.field);
		const bool surface_field = row.tex == TEX_ALBEDO || row.tex == TEX_ROUGHNESS || row.tex == TEX_METALLIC ||
		                           row.tex == TEX_NORMAL;
		if (it == none && surface_field && surface != none && !(s.locked & (1u << row.tex))) {
			it = surface;
		}
		if (it != none) {
			m.level = it->second.first;
			m.note = it->second.second;
		}
		else if (!row.used) {
			m.note = unused;
		}
		else if (std::string(row.field) == "filtering and wrapping" && !s.placement.empty()) {
			/* P9.4: which way the textures' settings went */
			m.note = s.placement + " (" + from + ")";
		}
		else if (std::string(row.field) == "categories") {
			m.note = (s.source == MaterialSpec::REMIX_NODE) ? "set on the RTX Remix Material node" :
			                                                  "set in the RTX Remix panel";
		}
		else if (s.source == MaterialSpec::REMIX_NODE &&
		         (std::string(row.field) == "anisotropy" || std::string(row.field) == "thin film" ||
		          std::string(row.field) == "height"))
		{
			/* P9.2: only Remix draws these */
			m.note = "Remix only (the game and F12 do not draw it)";
		}
		else {
			m.note = from;
		}
		if (row.tex >= 0 && s.textures[row.tex].wanted() && m.level == MATCH_EXACT) {
			const TextureRequest &t = s.textures[row.tex];
			if (t.baked) {
				m.level = MATCH_APPROX;
				m.note = (s.source == MaterialSpec::REMIX_NODE) ? "what is wired into the node, baked into an image" :
				         s.nodes_baked ? "the node tree, baked into an image" : "procedural textures, baked into an image";
			}
			else if (t.image) {
				m.note += std::string(" (image ") + (t.image->id.name + 2) + ")";
			}
		}
		r.push_back(m);
	}
	(void)ma;
}

}  /* namespace brmx */
