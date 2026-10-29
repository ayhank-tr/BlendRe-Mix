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

/** \file remix_bake.cc
 *  \ingroup remix
 *
 * BGE-Remix P8.6: procedural textures baked into images for RTX Remix
 * (see BRMX_bake.h). The material is drawn in UV space into an offscreen
 * buffer, once per value, and read back.
 */

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "BRMX_bake.h"
#include "BRMX_material.h"

#include "GPU_glew.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "DNA_customdata_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_texture_types.h"

#include "BLI_math.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_DerivedMesh.h"
#include "BKE_idprop.h"
#include "BKE_material_physical.h"
#include "BKE_mesh.h"

#include "GPU_framebuffer.h"
#include "GPU_material.h"
}

namespace brmx {

namespace {

const int DILATE_PASSES = 16;

uint64_t fnv(const void *data, size_t n, uint64_t h)
{
	const unsigned char *p = (const unsigned char *)data;
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

template<typename T> uint64_t fnv_v(const T &v, uint64_t h)
{
	return fnv(&v, sizeof(v), h);
}

float srgb_to_linear(float c)
{
	return (c < 0.04045f) ? ((c < 0.0f) ? 0.0f : c * (1.0f / 12.92f)) : powf((c + 0.055f) * (1.0f / 1.055f), 2.4f);
}

float linear_to_srgb(float c)
{
	return (c < 0.0031308f) ? ((c < 0.0f) ? 0.0f : c * 12.92f) : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

unsigned char to_byte(float v)
{
	return (unsigned char)std::max(0, std::min(255, (int)lroundf(v * 255.0f)));
}

/* --------------------------------------------------------------------- */
/* drawing in UV space */

int g_slot = 0;
GPUMaterial *g_material = NULL;

/* for tests (BGE_REMIX_TRACE): each step of a bake, printed and flushed, so
 * a crash shows where it happened */
void trace(const char *step, const std::string &what)
{
	static int on = -1;
	if (on < 0) {
		const char *env = getenv("BGE_REMIX_TRACE");
		on = (env && env[0]) ? 1 : 0;
	}
	if (on) {
		printf("BGE-Remix bake: %s %s\n", step, what.c_str());
		fflush(stdout);
	}
}

/* The game engine leaves its own vertex arrays enabled (and pointing into
 * its buffers) between draws; a bake draws with Blender's DerivedMesh code,
 * so it starts from no arrays and no buffers, and puts the game's back. */
struct CleanArrays {
	GLint array_buffer, element_buffer, client_texture;
	CleanArrays()
	{
		glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
		glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &element_buffer);
		glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &client_texture);
		glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		GLint attribs = 0;
		glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &attribs);
		for (GLint i = 0; i < attribs && i < 32; i++) {
			glDisableVertexAttribArray(i);
		}
		GLint units = 0;
		glGetIntegerv(GL_MAX_TEXTURE_COORDS, &units);
		for (GLint i = 0; i < units && i < 8; i++) {
			glClientActiveTexture(GL_TEXTURE0 + i);
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		}
		glClientActiveTexture(GL_TEXTURE0);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
	}
	~CleanArrays()
	{
		glPopClientAttrib();
		glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
		glClientActiveTexture(client_texture);
	}
};

int set_material(int matnr, void *attribs)
{
	if (matnr != g_slot || !g_material) {
		return 0;
	}
	GPU_material_vertex_attributes(g_material, (GPUVertexAttribs *)attribs);
	return 1;
}

/* One value of the material over its UV map: RGBA bytes, alpha 255 where
 * the surface was drawn. */
bool render_value(Scene *scene, Material *ma, DerivedMesh *dm, int slot, const float obmat[4][4], int size,
                  int channel, float scale, std::vector<unsigned char> &r_px, std::string &r_error)
{
	trace("shader", std::to_string(channel));
	GPUMaterial *mat = GPU_material_bake_create(scene, ma, channel, scale);
	if (!mat) {
		r_error = "its shader for baking could not be built";
		return false;
	}
	char err[256] = "";
	trace("offscreen", std::to_string(size));
	GPUOffScreen *ofs = GPU_offscreen_create(size, size, 0, err);
	if (!ofs) {
		GPU_material_bake_free(mat);
		r_error = std::string("no offscreen buffer for baking (") + err + ")";
		return false;
	}

	CleanArrays arrays;
	GPU_offscreen_bind(ofs, true);
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glViewport(0, 0, size, size);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_LIGHTING);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadMatrixf(&obmat[0][0]);

	/* the view is the world: Global and Object coordinates as placed */
	float view[4][4], viewinv[4][4], ob[4][4];
	float obcol[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	unit_m4(view);
	unit_m4(viewinv);
	copy_m4_m4(ob, (float (*)[4])obmat);
	trace("bind", "");
	GPU_material_bind(mat, ~0, ~0, 0.0, 1, view, viewinv, NULL, false);
	GPU_material_bind_uniforms(mat, ob, view, obcol, 1.0f, NULL, NULL);
	g_slot = slot;
	g_material = mat;
	trace("draw", std::to_string(slot));
	dm->drawMappedFacesGLSL(dm, set_material, NULL, NULL);
	g_material = NULL;
	GPU_material_unbind(mat);
	trace("read", "");

	r_px.assign((size_t)size * size * 4, 0);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	GPU_offscreen_read_pixels(ofs, GL_UNSIGNED_BYTE, r_px.data());

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_TEXTURE);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopAttrib();
	GPU_offscreen_unbind(ofs, true);
	GPU_offscreen_free(ofs);
	GPU_material_bake_free(mat);
	trace("done", "");
	return true;
}

/* Spread the drawn pixels into the empty ones around them, so texture
 * filtering at the UV islands' edges does not pick up the background. */
void dilate(std::vector<unsigned char> &px, int w, int h)
{
	std::vector<unsigned char> next;
	for (int pass = 0; pass < DILATE_PASSES; pass++) {
		next = px;
		bool changed = false;
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				unsigned char *p = &next[((size_t)y * w + x) * 4];
				if (px[((size_t)y * w + x) * 4 + 3] != 0) {
					continue;
				}
				int sum[3] = {0, 0, 0}, n = 0;
				for (int dy = -1; dy <= 1; dy++) {
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = x + dx, ny = y + dy;
						if ((dx || dy) && nx >= 0 && ny >= 0 && nx < w && ny < h) {
							const unsigned char *q = &px[((size_t)ny * w + nx) * 4];
							if (q[3] != 0) {
								sum[0] += q[0];
								sum[1] += q[1];
								sum[2] += q[2];
								n++;
							}
						}
					}
				}
				if (n) {
					p[0] = (unsigned char)(sum[0] / n);
					p[1] = (unsigned char)(sum[1] / n);
					p[2] = (unsigned char)(sum[2] / n);
					p[3] = 255;
					changed = true;
				}
			}
		}
		px.swap(next);
		if (!changed) {
			break;
		}
	}
}

/* tests: BGE_REMIX_BAKE_DUMP=<folder> writes every baked image there as
 * <name>.ppm (RGB, top row first) */
void dump_image(const BakedImage &img)
{
	const char *dir = getenv("BGE_REMIX_BAKE_DUMP");
	if (!dir || !dir[0] || img.width <= 0) {
		return;
	}
	std::string name;
	for (char c : img.name) {
		name += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
	}
	const std::string path = std::string(dir) + "/" + name + ".ppm";
	FILE *f = fopen(path.c_str(), "wb");
	if (!f) {
		return;
	}
	fprintf(f, "P6\n%d %d\n255\n", img.width, img.height);
	for (int y = img.height - 1; y >= 0; y--) {
		for (int x = 0; x < img.width; x++) {
			fwrite(&img.rgba[((size_t)y * img.width + x) * 4], 1, 3, f);
		}
	}
	fclose(f);
}

std::shared_ptr<BakedImage> make_image(const std::string &name, int size)
{
	std::shared_ptr<BakedImage> img(new BakedImage());
	img->name = name;
	img->width = img->height = size;
	img->rgba.assign((size_t)size * size * 4, 255);
	return img;
}

bool is_bakeable(const Material *ma)
{
	if (!ma || (ma->use_nodes && ma->nodetree)) {
		return false;
	}
	/* the RTX Remix panel's own values come first (remix_material.cc) */
	return true;
}

}  /* namespace */

unsigned bake_slots(const Material *ma)
{
	unsigned slots = 0;
	if (!is_bakeable(ma)) {
		return 0;
	}
	const bool physical = BKE_material_is_physical(ma);
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (!mtex || !mtex->tex || (ma->septex & (1 << i)) || !GPU_texture_is_procedural(mtex->tex)) {
			continue;
		}
		if (mtex->mapto & (MAP_COL | MAP_ALPHA)) {
			slots |= 1u << BAKE_ALBEDO;
		}
		if (!physical && (mtex->mapto & MAP_REF)) {
			slots |= 1u << BAKE_ALBEDO;
		}
		if (physical && (mtex->mapto & MAP_RAYMIRR)) {
			slots |= 1u << BAKE_METALLIC;
		}
		if (mtex->mapto & MAP_HAR) {
			slots |= 1u << BAKE_ROUGHNESS;
		}
		if (mtex->mapto & MAP_EMIT) {
			slots |= 1u << BAKE_EMISSIVE;
		}
		if (mtex->mapto & MAP_NORM) {
			slots |= 1u << BAKE_NORMAL;
		}
	}
	return slots;
}

uint64_t bake_signature(const Material *ma)
{
	uint64_t h = 1469598103934665603ull;
	if (!ma) {
		return h;
	}
	h = fnv(&ma->r, sizeof(float) * 3, h);
	h = fnv_v(ma->alpha, h);
	h = fnv_v(ma->ref, h);
	h = fnv_v(ma->spec, h);
	h = fnv_v(ma->emit, h);
	h = fnv_v(ma->har, h);
	h = fnv_v(ma->mode, h);
	h = fnv_v(ma->septex, h);
	{
		/* P8.7: the physical values */
		MaterialPhysical ph;
		BKE_material_physical_get(ma, &ph);
		h = fnv(&ph, sizeof(ph), h);
		h = fnv_v(BKE_material_is_physical(ma), h);
	}
	for (int i = 0; i < MAX_MTEX; i++) {
		const MTex *mtex = ma->mtex[i];
		if (!mtex || !mtex->tex) {
			continue;
		}
		const Tex *tex = mtex->tex;
		h = fnv_v(i, h);
		h = fnv_v(mtex->texco, h);
		h = fnv_v(mtex->mapto, h);
		h = fnv_v(mtex->blendtype, h);
		h = fnv_v(mtex->texflag, h);
		h = fnv(mtex->size, sizeof(mtex->size), h);
		h = fnv(mtex->ofs, sizeof(mtex->ofs), h);
		h = fnv_v(mtex->projx, h);
		h = fnv_v(mtex->projy, h);
		h = fnv_v(mtex->projz, h);
		h = fnv(&mtex->r, sizeof(float) * 3, h);
		h = fnv_v(mtex->def_var, h);
		h = fnv_v(mtex->colfac, h);
		h = fnv_v(mtex->norfac, h);
		h = fnv_v(mtex->difffac, h);
		h = fnv_v(mtex->hardfac, h);
		h = fnv_v(mtex->alphafac, h);
		h = fnv_v(mtex->emitfac, h);
		h = fnv_v(mtex->object, h);
		if (mtex->object && mtex->texco == TEXCO_OBJECT) {
			h = fnv(mtex->object->obmat, sizeof(mtex->object->obmat), h);
		}
		h = fnv_v(tex->type, h);
		h = fnv_v(tex->stype, h);
		h = fnv_v(tex->flag, h);
		h = fnv_v(tex->noisesize, h);
		h = fnv_v(tex->turbul, h);
		h = fnv_v(tex->bright, h);
		h = fnv_v(tex->contrast, h);
		h = fnv_v(tex->saturation, h);
		h = fnv_v(tex->rfac, h);
		h = fnv_v(tex->gfac, h);
		h = fnv_v(tex->bfac, h);
		h = fnv_v(tex->mg_H, h);
		h = fnv_v(tex->mg_lacunarity, h);
		h = fnv_v(tex->mg_octaves, h);
		h = fnv_v(tex->mg_offset, h);
		h = fnv_v(tex->mg_gain, h);
		h = fnv_v(tex->dist_amount, h);
		h = fnv_v(tex->ns_outscale, h);
		h = fnv_v(tex->vn_w1, h);
		h = fnv_v(tex->vn_w2, h);
		h = fnv_v(tex->vn_w3, h);
		h = fnv_v(tex->vn_w4, h);
		h = fnv_v(tex->vn_mexp, h);
		h = fnv_v(tex->vn_distm, h);
		h = fnv_v(tex->vn_coltype, h);
		h = fnv_v(tex->noisedepth, h);
		h = fnv_v(tex->noisetype, h);
		h = fnv_v(tex->noisebasis, h);
		h = fnv_v(tex->noisebasis2, h);
		h = fnv_v(tex->nabla, h);
		h = fnv_v(tex->ima, h);
		h = fnv_v(bake_size(ma), h);
		if (tex->coba) {
			h = fnv_v(tex->coba->tot, h);
			h = fnv_v(tex->coba->ipotype, h);
			h = fnv_v(tex->coba->color_mode, h);
			h = fnv(tex->coba->data, sizeof(CBData) * std::max<int>(0, std::min<int>(tex->coba->tot, 32)), h);
		}
	}
	return h;
}

int bake_size(const Material *ma)
{
	const char *env = getenv("BGE_REMIX_BAKE_SIZE");
	int n = env ? atoi(env) : 0;
	if (n < 16 && ma && ma->id.properties) {
		const IDProperty *p = IDP_GetPropertyFromGroup(ma->id.properties, "bge_remix_bake_size");
		n = (p && p->type == IDP_INT) ? IDP_Int(p) : 0;
	}
	return (n >= 16 && n <= 8192) ? n : 1024;
}

DerivedMesh *bake_mesh_from(Mesh *me)
{
	if (!me || !me->totvert) {
		return NULL;
	}
	DerivedMesh *dm = CDDM_from_mesh(me);
	const int totvert = dm->getNumVerts(dm);
	float (*orco)[3] = (float (*)[3])MEM_mallocN(sizeof(float[3]) * totvert, "bake orco");
	dm->getVertCos(dm, orco);
	BKE_mesh_orco_verts_transform(me, orco, totvert, 0);
	DM_add_vert_layer(dm, CD_ORCO, CD_ASSIGN, orco);
	return dm;
}

bool bake_material(Scene *scene, Material *ma, DerivedMesh *dm, int slot, const float obmat[4][4],
                   const std::string &name, BakeResult &r)
{
	r = BakeResult();
	r.signature = bake_signature(ma);
	const unsigned slots = bake_slots(ma);
	if (!slots || !dm) {
		return false;
	}
	const bool physical = BKE_material_is_physical(ma);
	MaterialPhysical ph;
	BKE_material_physical_get(ma, &ph);
	if (!CustomData_has_layer(&dm->loopData, CD_MLOOPUV) && !CustomData_has_layer(&dm->faceData, CD_MTFACE)) {
		r.error = "the mesh has no UV map, so its procedural textures cannot be baked";
		return false;
	}
	const int size = bake_size(ma);
	std::vector<unsigned char> color, value;
	trace("material", name);

	/* albedo (and the emissive colour): the colour, with the alpha */
	/* the emissive colour: the base colour (Classic: emit lights the
	 * diffuse colour) or the physical material's emission colour */
	const bool want_color = (slots & (1u << BAKE_ALBEDO)) || ((slots & (1u << BAKE_EMISSIVE)) && !physical);
	if (want_color && !render_value(scene, ma, dm, slot, obmat, size, GPU_BAKE_COLOR, 1.0f, color, r.error)) {
		return false;
	}
	if (slots & (1u << BAKE_ALBEDO)) {
		std::shared_ptr<BakedImage> img = make_image(name + "_albedo", size);
		std::vector<unsigned char> px = color;
		bool alpha_textured = false;
		for (int i = 0; i < MAX_MTEX; i++) {
			const MTex *mtex = ma->mtex[i];
			alpha_textured = alpha_textured || (mtex && mtex->tex && !(ma->septex & (1 << i)) &&
			                                    (mtex->mapto & MAP_ALPHA));
		}
		if (alpha_textured) {
			if (!render_value(scene, ma, dm, slot, obmat, size, GPU_BAKE_ALPHA, 1.0f, value, r.error)) {
				return false;
			}
			dilate(value, size, size);
			r.has_alpha = true;
		}
		dilate(px, size, size);
		for (size_t i = 0; i < (size_t)size * size; i++) {
			img->rgba[i * 4 + 0] = px[i * 4 + 0];
			img->rgba[i * 4 + 1] = px[i * 4 + 1];
			img->rgba[i * 4 + 2] = px[i * 4 + 2];
			img->rgba[i * 4 + 3] = alpha_textured ? value[i * 4 + 0] : 255;
		}
		r.images[BAKE_ALBEDO] = img;
	}

	if (slots & (1u << BAKE_ROUGHNESS)) {
		if (!render_value(scene, ma, dm, slot, obmat, size, GPU_BAKE_HARDNESS, 1.0f, value, r.error)) {
			return false;
		}
		dilate(value, size, size);
		std::shared_ptr<BakedImage> img = make_image(name + "_roughness", size);
		for (size_t i = 0; i < (size_t)size * size; i++) {
			float rough;
			if (physical) {
				/* the physical material's own roughness */
				rough = value[i * 4] / 255.0f;
			}
			else {
				/* as the translator turns hardness into roughness */
				const float har = value[i * 4] * (511.0f / 255.0f);
				rough = (ma->spec < 0.01f) ? 1.0f : BKE_material_hardness_to_roughness(har);
			}
			img->rgba[i * 4 + 0] = img->rgba[i * 4 + 1] = img->rgba[i * 4 + 2] = to_byte(rough);
		}
		r.images[BAKE_ROUGHNESS] = img;
	}

	if (slots & (1u << BAKE_METALLIC)) {
		if (!render_value(scene, ma, dm, slot, obmat, size, GPU_BAKE_METALLIC, 1.0f, value, r.error)) {
			return false;
		}
		dilate(value, size, size);
		std::shared_ptr<BakedImage> img = make_image(name + "_metallic", size);
		for (size_t i = 0; i < (size_t)size * size; i++) {
			img->rgba[i * 4 + 0] = img->rgba[i * 4 + 1] = img->rgba[i * 4 + 2] = value[i * 4];
		}
		r.images[BAKE_METALLIC] = img;
	}

	if (slots & (1u << BAKE_EMISSIVE)) {
		/* the most emit can reach: the material's, plus every emit slot's */
		float scale = std::max(physical ? ph.emission_strength : ma->emit, 0.0f);
		for (int i = 0; i < MAX_MTEX; i++) {
			const MTex *mtex = ma->mtex[i];
			if (mtex && mtex->tex && !(ma->septex & (1 << i)) && (mtex->mapto & MAP_EMIT)) {
				scale += std::fabs(mtex->emitfac);
			}
		}
		scale = std::max(scale, 1e-3f);
		if (!render_value(scene, ma, dm, slot, obmat, size, GPU_BAKE_EMIT, scale, value, r.error)) {
			return false;
		}
		dilate(value, size, size);
		std::vector<unsigned char> px = color;
		if (!physical) {
			dilate(px, size, size);
		}
		std::shared_ptr<BakedImage> img = make_image(name + "_emissive", size);
		for (size_t i = 0; i < (size_t)size * size; i++) {
			const float e = value[i * 4] / 255.0f;
			for (int c = 0; c < 3; c++) {
				const float col = physical ? ph.emission[c] : srgb_to_linear(px[i * 4 + c] / 255.0f);
				img->rgba[i * 4 + c] = to_byte(linear_to_srgb(col * e));
			}
		}
		r.images[BAKE_EMISSIVE] = img;
		r.emissive_intensity = scale;
	}

	if (slots & (1u << BAKE_NORMAL)) {
		if (!render_value(scene, ma, dm, slot, obmat, size, GPU_BAKE_NORMAL, 1.0f, value, r.error)) {
			return false;
		}
		dilate(value, size, size);
		std::shared_ptr<BakedImage> img = make_image(name + "_normal", size);
		for (size_t i = 0; i < (size_t)size * size; i++) {
			const bool drawn = value[i * 4 + 3] != 0;
			img->rgba[i * 4 + 0] = drawn ? value[i * 4 + 0] : 128;
			img->rgba[i * 4 + 1] = drawn ? value[i * 4 + 1] : 128;
			img->rgba[i * 4 + 2] = drawn ? value[i * 4 + 2] : 255;
		}
		r.images[BAKE_NORMAL] = img;
	}
	for (int i = 0; i < BAKE_SLOTS; i++) {
		if (r.images[i]) {
			dump_image(*r.images[i]);
		}
	}
	return true;
}

void apply_bake(const BakeResult &r, MaterialSpec &s)
{
	brb_material_desc &d = s.desc;
	if (r.images[BAKE_ALBEDO]) {
		TextureRequest t;
		t.baked = r.images[BAKE_ALBEDO];
		t.kind = BRB_TEXTURE_COLOR;
		s.textures[TEX_ALBEDO] = t;
		/* the bake holds the colour times the reflectivity */
		d.albedo[0] = d.albedo[1] = d.albedo[2] = 1.0f;
		if (r.has_alpha && (d.alpha_blend || d.alpha_test)) {
			d.opacity = 1.0f;
		}
	}
	if (r.images[BAKE_ROUGHNESS]) {
		TextureRequest t;
		t.baked = r.images[BAKE_ROUGHNESS];
		t.kind = BRB_TEXTURE_SCALAR;
		t.channel = 0;
		s.textures[TEX_ROUGHNESS] = t;
	}
	if (r.images[BAKE_EMISSIVE]) {
		TextureRequest t;
		t.baked = r.images[BAKE_EMISSIVE];
		t.kind = BRB_TEXTURE_COLOR;
		s.textures[TEX_EMISSIVE] = t;
		d.emissive[0] = d.emissive[1] = d.emissive[2] = 1.0f;
		d.emissive_intensity = r.emissive_intensity;
	}
	if (r.images[BAKE_NORMAL]) {
		TextureRequest t;
		t.baked = r.images[BAKE_NORMAL];
		t.kind = BRB_TEXTURE_NORMAL;
		s.textures[TEX_NORMAL] = t;
	}
	if (r.images[BAKE_METALLIC]) {
		TextureRequest t;
		t.baked = r.images[BAKE_METALLIC];
		t.kind = BRB_TEXTURE_SCALAR;
		t.channel = 0;
		s.textures[TEX_METALLIC] = t;
	}
}

}  /* namespace brmx */
