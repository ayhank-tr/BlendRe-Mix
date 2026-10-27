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
}

namespace brmx {

const char *texture_slot_name(int slot)
{
	static const char *names[TEX_COUNT] = {"albedo", "normal", "roughness", "metallic", "emissive"};
	return (slot >= 0 && slot < TEX_COUNT) ? names[slot] : "?";
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

static void glass_tint(const float c[3], float r_transmittance[3])
{
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

enum { PANEL_OPAQUE = 0, PANEL_GLASS = 1, PANEL_SUBSURFACE = 2 };
enum { PANEL_ALPHA_OPAQUE = 0, PANEL_ALPHA_BLEND = 1, PANEL_ALPHA_CLIP = 2 };

bool from_panel(const Material *ma, MaterialSpec &s)
{
	if (!prop_int(ma, "bge_remix_override", 0)) {
		return false;
	}
	brb_material_desc &d = s.desc;
	s.source = MaterialSpec::PANEL;
	const int type = prop_int(ma, "bge_remix_type", PANEL_OPAQUE);
	set3(d.albedo, 0.8f, 0.8f, 0.8f);
	prop_float3(ma, "bge_remix_albedo", d.albedo);
	d.opacity = prop_float(ma, "bge_remix_opacity", 1.0f);
	d.roughness = prop_float(ma, "bge_remix_roughness", 0.5f);
	d.metallic = prop_float(ma, "bge_remix_metallic", 0.0f);
	set3(d.emissive, 1.0f, 1.0f, 1.0f);
	prop_float3(ma, "bge_remix_emissive", d.emissive);
	d.emissive_intensity = prop_float(ma, "bge_remix_emissive_strength", 0.0f);
	const int alpha = prop_int(ma, "bge_remix_alpha", PANEL_ALPHA_OPAQUE);
	d.alpha_blend = alpha == PANEL_ALPHA_BLEND;
	d.alpha_test = alpha == PANEL_ALPHA_CLIP;
	d.alpha_cutoff = prop_float(ma, "bge_remix_alpha_cutoff", 0.5f);
	if (alpha == PANEL_ALPHA_OPAQUE) {
		d.opacity = 1.0f;
	}
	if (type == PANEL_GLASS) {
		d.type = BRB_MATERIAL_TRANSLUCENT;
		d.ior = prop_float(ma, "bge_remix_ior", 1.5f);
		set3(d.transmittance, 1.0f, 1.0f, 1.0f);
		prop_float3(ma, "bge_remix_transmittance", d.transmittance);
	}
	else if (type == PANEL_SUBSURFACE) {
		d.subsurface = 1;
		memcpy(d.subsurface_color, d.albedo, sizeof(d.subsurface_color));
		prop_float3(ma, "bge_remix_subsurface_radius", d.subsurface_radius);
	}
	s.textures[TEX_ALBEDO] = texture(prop_image(ma, "bge_remix_albedo_image"), BRB_TEXTURE_COLOR);
	s.textures[TEX_NORMAL] = texture(prop_image(ma, "bge_remix_normal_image"), BRB_TEXTURE_NORMAL);
	s.textures[TEX_ROUGHNESS] = texture(prop_image(ma, "bge_remix_roughness_image"), BRB_TEXTURE_SCALAR);
	s.textures[TEX_METALLIC] = texture(prop_image(ma, "bge_remix_metallic_image"), BRB_TEXTURE_SCALAR);
	s.textures[TEX_EMISSIVE] = texture(prop_image(ma, "bge_remix_emissive_image"), BRB_TEXTURE_COLOR);
	for (int i = 0; i < TEX_COUNT; i++) {
		if (!s.textures[i].image) {
			s.textures[i] = TextureRequest();
		}
	}
	return true;
}

/* ------------------------------------------------------------------------- */
/* 3. Blender Internal */

void from_blender_internal(const Material *ma, MaterialSpec &s)
{
	brb_material_desc &d = s.desc;
	s.source = MaterialSpec::BLENDER_INTERNAL;
	set3(d.albedo, ma->r * ma->ref, ma->g * ma->ref, ma->b * ma->ref);
	d.opacity = ma->alpha;
	d.roughness = hardness_to_roughness(ma);
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
	}
	const int blend = ma->game.alpha_blend;
	d.alpha_blend = (blend == GEMAT_ALPHA || blend == GEMAT_ALPHA_SORT || blend == GEMAT_ADD) ? 1 : 0;
	d.alpha_test = (blend == GEMAT_CLIP) ? 1 : 0;
	if (!d.alpha_blend) {
		d.opacity = 1.0f;
	}

	/* image texture slots */
	Image *albedo_image = NULL;
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (!mtex || !mtex->tex || (ma->septex & (1 << i))) {
			continue;
		}
		const Tex *tex = mtex->tex;
		const std::string slot = std::string("texture slot ") + (mtex->tex->id.name + 2);
		if (tex->type != TEX_IMAGE || !tex->ima) {
			if (mtex->mapto & (MAP_COL | MAP_NORM | MAP_EMIT | MAP_HAR)) {
				note(s, slot + ": not an image texture, left out");
			}
			continue;
		}
		if (mtex->texco != TEXCO_UV) {
			note(s, slot + ": not mapped by UV, left out");
			continue;
		}
		if (tex->extend == TEX_EXTEND || tex->extend == TEX_CLIP) {
			d.clamp = 1;
		}
		if ((mtex->mapto & MAP_COL) && !s.textures[TEX_ALBEDO].image) {
			s.textures[TEX_ALBEDO] = texture(tex->ima, BRB_TEXTURE_COLOR);
			albedo_image = tex->ima;
		}
		if ((mtex->mapto & MAP_NORM) && !s.textures[TEX_NORMAL].image) {
			if ((tex->imaflag & TEX_NORMALMAP) && mtex->normapspace == MTEX_NSPACE_TANGENT) {
				s.textures[TEX_NORMAL] = texture(tex->ima, BRB_TEXTURE_NORMAL);
			}
			else if (tex->imaflag & TEX_NORMALMAP) {
				note(s, slot + ": only tangent-space normal maps are read");
			}
			else {
				note(s, slot + ": bump map (not a normal map), left out");
			}
		}
		if ((mtex->mapto & MAP_EMIT) && !s.textures[TEX_EMISSIVE].image) {
			s.textures[TEX_EMISSIVE] = texture(tex->ima, BRB_TEXTURE_COLOR);
			set3(d.emissive, 1.0f, 1.0f, 1.0f);
			if (d.emissive_intensity <= 0.0f) {
				d.emissive_intensity = mtex->emitfac;
			}
		}
		if ((mtex->mapto & MAP_HAR) && !s.textures[TEX_ROUGHNESS].image) {
			/* bright = hard = smooth */
			s.textures[TEX_ROUGHNESS] = texture(tex->ima, BRB_TEXTURE_SCALAR, 0, BRB_TEXTURE_INVERT);
		}
		if ((mtex->mapto & MAP_ALPHA) && tex->ima != albedo_image) {
			note(s, slot + ": alpha from an image other than the colour texture, left out");
		}
		else if ((mtex->mapto & MAP_ALPHA) && tex->ima == albedo_image) {
			/* the image's alpha is the opacity (a sprite: material alpha 0,
			 * the texture brings it back), as BGE draws it */
			d.opacity = 1.0f;
		}
	}
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

	void image_options(const bNode *img)
	{
		const NodeTexImage *t = (const NodeTexImage *)img->storage;
		if (t) {
			if (t->interpolation == SHD_INTERP_CLOSEST) {
				s.desc.filter_nearest = 1;
			}
			if (t->extension == SHD_IMAGE_EXTENSION_EXTEND || t->extension == SHD_IMAGE_EXTENSION_CLIP) {
				s.desc.clamp = 1;
			}
		}
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
			glass_tint(d.albedo, d.transmittance);
			if (s.textures[TEX_ALBEDO].image) {
				note(s, std::string(n->name) + ": glass tinted by an image, the image is left out");
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
					glass_tint(c, d.transmittance);
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
				shader(a, depth + 1);
				return;
			}
			default: {
				/* a shader Remix mode does not know: its colour, if it has one */
				note(s, std::string(n->name) + ": not a shader Remix mode reads; a flat colour is used");
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

}  /* namespace */

void translate_material(const Material *ma, MaterialSpec &r_spec)
{
	defaults(r_spec);
	if (!ma) {
		return;
	}
	/* whatever the material's source: sprites and particles */
	r_spec.particle = prop_int(ma, "bge_remix_particle", 0) != 0 ||
	                  (ma->game.face_orientation & GEMAT_HALO) != 0;
	if (from_panel(ma, r_spec)) {
		return;
	}
	if (from_nodes(ma, r_spec)) {
		return;
	}
	from_blender_internal(ma, r_spec);
}

}  /* namespace brmx */
