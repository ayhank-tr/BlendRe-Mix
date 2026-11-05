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

/** \file remix_remaster.cc
 *  \ingroup remix
 *
 * BGE-Remix P10.2 and P10.5: the texture remaster's bakes and Apply's
 * up-front encoding (see BRMX_remaster.h).
 */

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "BRMX_bake.h"
#include "BRMX_material.h"
#include "BRMX_remaster.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BLI_threads.h"

#include "BKE_DerivedMesh.h"
#include "BKE_material.h"
}

namespace {

/* Apply's encodes: one cache, kept between calls (its entries are the
 * files asked for, so a second Apply asks only for what changed) */
brmx::TextureCache *g_cache = NULL;
int g_bakes = 0;
/* the bakes' results stay alive until their encodes are done (the cache
 * keeps a pointer to the pixels) */
std::vector<std::shared_ptr<const brmx::BakedImage> > g_alive;

brmx::TextureCache &cache()
{
	if (!g_cache) {
		g_cache = new brmx::TextureCache();
	}
	return *g_cache;
}

void set_error(char *r_error, int len, const char *text)
{
	if (r_error && len > 0) {
		snprintf(r_error, (size_t)len, "%s", text);
	}
}

bool can_draw(char *r_error, int len)
{
	if (!BLI_thread_is_main() || wglGetCurrentContext() == NULL) {
		set_error(r_error, len, "no OpenGL context (Blender in the background cannot bake)");
		return false;
	}
	return true;
}

int ask(const brmx::MaterialSpec &spec)
{
	int n = 0;
	for (int t = 0; t < brmx::TEX_COUNT; t++) {
		const brmx::TextureRequest &req = spec.textures[t];
		if (!req.wanted()) {
			continue;
		}
		if (req.baked) {
			g_alive.push_back(req.baked);
		}
		std::wstring path;
		cache().request(req, path);
		n++;
	}
	return n;
}

}  /* namespace */

extern "C" {

unsigned BRMX_remaster_bake_tile(Scene *scene, Material *ma, int size, unsigned slots,
                                 unsigned char *r_pixels[BRMX_REMASTER_SLOTS], char *r_error, int error_len)
{
	for (int i = 0; i < BRMX_REMASTER_SLOTS; i++) {
		r_pixels[i] = NULL;
	}
	if (!scene || !ma) {
		set_error(r_error, error_len, "no scene or material");
		return 0;
	}
	if (!brmx::bake_is_tile(ma)) {
		set_error(r_error, error_len, "the material is not a preset's tile");
		return 0;
	}
	if (!can_draw(r_error, error_len)) {
		return 0;
	}
	brmx::BakeResult r;
	float unit[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
	const std::string name = std::string("remaster_") + (ma->id.name + 2);
	if (!brmx::bake_material(scene, ma, NULL, 1, unit, name, r, size, false, slots)) {
		set_error(r_error, error_len, r.error.empty() ? "the tile could not be baked" : r.error.c_str());
		return 0;
	}
	unsigned done = 0;
	for (int s = 0; s < BRMX_REMASTER_SLOTS && s < brmx::BAKE_SLOTS; s++) {
		const brmx::BakedImage *img = r.images[s].get();
		if (!img || img->rgba.empty() || img->width != size || img->height != size) {
			continue;
		}
		r_pixels[s] = (unsigned char *)MEM_mallocN(img->rgba.size(), "BGE-Remix remaster bake");
		memcpy(r_pixels[s], img->rgba.data(), img->rgba.size());
		done |= 1u << s;
	}
	if (!done) {
		set_error(r_error, error_len, "nothing was baked");
	}
	return done;
}

void BRMX_remaster_free(unsigned char *pixels)
{
	if (pixels) {
		MEM_freeN(pixels);
	}
}

int BRMX_remaster_precache(Scene *scene, Object *ob, char *r_error, int error_len)
{
	if (!scene || !ob || ob->type != OB_MESH || !ob->data) {
		set_error(r_error, error_len, "not a mesh object");
		return -1;
	}
	Mesh *me = (Mesh *)ob->data;
	int asked = 0;
	DerivedMesh *dm = NULL;
	for (int i = 0; i < ob->totcol; i++) {
		Material *ma = give_current_material(ob, (short)(i + 1));
		if (!ma) {
			continue;
		}
		/* as KX_RemixRenderer::EnsureMaterial: the translation, then each
		 * face image's version, then the bake */
		brmx::MaterialSpec spec;
		brmx::translate_material(ma, spec);
		const unsigned bake = brmx::bake_slots(ma);
		if (bake) {
			if (!can_draw(r_error, error_len)) {
				if (dm) {
					dm->release(dm);
				}
				return -1;
			}
			const bool tile = brmx::bake_is_tile(ma);
			if (!dm && !tile) {
				dm = brmx::bake_mesh_from(me);
			}
			/* the game's names: a tile by its material (ID name, as the game's
			 * polymaterial name), a per-mesh bake by material and mesh. (A
			 * per-mesh bake's key holds where the object stands, as the game
			 * draws it: a float apart from Blender's and the game bakes it
			 * again; presets and images, the remaster's own, are exact.) */
			const std::string name = tile ? std::string(ma->id.name) :
			                                std::string(ma->id.name) + "_" + (me->id.name + 2);
			brmx::BakeResult r;
			if (brmx::bake_material(scene, ma, dm, i + 1, ob->obmat, name, r)) {
				brmx::apply_bake(r, spec);
				if (!r.from_cache) {
					g_bakes++;
				}
			}
		}
		if (brmx::uses_face_image(ma) && me->mtpoly && me->mpoly) {
			std::set<Image *> images;
			for (int p = 0; p < me->totpoly; p++) {
				if (me->mpoly[p].mat_nr == i && me->mtpoly[p].tpage) {
					images.insert(me->mtpoly[p].tpage);
				}
			}
			for (std::set<Image *>::const_iterator it = images.begin(); it != images.end(); ++it) {
				brmx::MaterialSpec one = spec;
				brmx::apply_face_image(ma, *it, one);
				asked += ask(one);
			}
			if (!images.empty()) {
				continue;
			}
		}
		asked += ask(spec);
	}
	if (dm) {
		dm->release(dm);
	}
	return asked;
}

void BRMX_remaster_poll(int *r_waiting, int *r_encoded, int *r_reused, int *r_failed, double *r_seconds,
                        int *r_bakes)
{
	brmx::TextureCache &c = cache();
	c.poll();
	*r_waiting = c.waiting();
	*r_encoded = c.encoded;
	*r_reused = c.reused;
	*r_failed = c.failed;
	*r_seconds = c.encode_seconds;
	*r_bakes = g_bakes;
	if (c.waiting() == 0) {
		g_alive.clear();
	}
}

void BRMX_remaster_reset(void)
{
	if (g_cache && g_cache->waiting() == 0) {
		delete g_cache;
		g_cache = NULL;
		g_alive.clear();
	}
	g_bakes = 0;
}

}  /* extern "C" */
