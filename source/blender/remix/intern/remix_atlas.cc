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

/** \file remix_atlas.cc
 *  \ingroup remix
 *
 * BGE-Remix P10.4: the atlases' Blender parts (see BRMX_atlas.h): what a
 * material asks for, the preset baked at several Seeds through
 * bake_material (remix_bake.cc, unchanged) and made into the atlas, packed
 * pages, the faces' picks, and the bake for Python (the Blender UI).
 *
 * The material's settings (written by bge_remix.atlas):
 *   bge_remix_atlas          1 tiles, 2 flipbook, 3 packed (0 or none: off)
 *   bge_remix_atlas_size     a cell's side in pixels (else the Bake Size)
 *   bge_remix_atlas_sharp    the default tile set: 512-pixel tiles, not 256
 *   bge_remix_atlas_colours  tiles: 2 (16 tiles) or 3 (81)
 *   bge_remix_atlas_seed     tiles: the corner colours' hash
 *   bge_remix_atlas_regular  tiles: 1 for a regular pattern (bricks): the
 *                            colours' Seeds keep the pattern in step
 *   bge_remix_atlas_frames   flipbook: 16 (4 x 4) or 64 (8 x 8)
 *   bge_remix_atlas_fps      flipbook: frames per second (from the speed)
 *   bge_remix_atlas_shift    flipbook: how far the pattern moves over one loop
 *                            (tiles; the frames' count / 64)
 *   bge_remix_atlas_morph    flipbook: how far the noise moves through its
 *                            third axis over one loop (its units: the waves'
 *                            shape changes; 0: moved only)
 *   bge_remix_atlas_normal_gain  flipbook: its normal map's slopes times this
 * Packed images (their own settings): bge_remix_atlas_page (the page
 * image's name), bge_remix_atlas_cell; a page: bge_remix_atlas_cells
 * (x, y, w, h, repeat per image), bge_remix_atlas_gutter.
 */

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "BRMX_atlas.h"
#include "BRMX_bake.h"
#include "BRMX_material.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "DNA_customdata_types.h"
#include "DNA_ID.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_node_types.h"
#include "DNA_scene_types.h"

#include "BLI_listbase.h"
#include "BLI_math.h"

#include "BKE_cdderivedmesh.h"
#include "BKE_customdata.h"
#include "BKE_DerivedMesh.h"
#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_main.h"
#include "BKE_material_physical.h"
#include "BKE_node.h"
}

namespace brmx {

namespace {

const char *PICK_LAYER = "bge_remix_cell";

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

IDProperty *prop(const ID *id, const char *name)
{
	return (id && id->properties) ? IDP_GetPropertyFromGroup(id->properties, name) : NULL;
}

int prop_int(const ID *id, const char *name, int def)
{
	const IDProperty *p = prop(id, name);
	if (!p) {
		return def;
	}
	if (p->type == IDP_INT) {
		return IDP_Int(p);
	}
	if (p->type == IDP_DOUBLE) {
		return (int)IDP_Double(p);
	}
	if (p->type == IDP_FLOAT) {
		return (int)IDP_Float(p);
	}
	return def;
}

float prop_float(const ID *id, const char *name, float def)
{
	const IDProperty *p = prop(id, name);
	if (!p) {
		return def;
	}
	if (p->type == IDP_DOUBLE) {
		return (float)IDP_Double(p);
	}
	if (p->type == IDP_FLOAT) {
		return IDP_Float(p);
	}
	if (p->type == IDP_INT) {
		return (float)IDP_Int(p);
	}
	return def;
}

std::string prop_string(const ID *id, const char *name)
{
	const IDProperty *p = prop(id, name);
	return (p && p->type == IDP_STRING && IDP_String(p)) ? std::string(IDP_String(p)) : std::string();
}

/* an int array property's values */
bool prop_ints(const ID *id, const char *name, std::vector<int> &r)
{
	r.clear();
	const IDProperty *p = prop(id, name);
	if (!p || p->type != IDP_ARRAY || p->len <= 0) {
		return false;
	}
	for (int i = 0; i < p->len; i++) {
		if (p->subtype == IDP_INT) {
			r.push_back(((const int *)IDP_Array(p))[i]);
		}
		else if (p->subtype == IDP_DOUBLE) {
			r.push_back((int)((const double *)IDP_Array(p))[i]);
		}
		else if (p->subtype == IDP_FLOAT) {
			r.push_back((int)((const float *)IDP_Array(p))[i]);
		}
		else {
			return false;
		}
	}
	return true;
}

/* the preset's group node input (P9.6's "Remix Preset: ..." group) */
bNodeSocketValueFloat *preset_input(Material *ma, const char *name, bool &r_linked)
{
	r_linked = false;
	if (!ma || !ma->nodetree) {
		return NULL;
	}
	for (bNode *n = (bNode *)ma->nodetree->nodes.first; n; n = n->next) {
		if (n->type != NODE_GROUP || !n->id || !prop(n->id, "bge_remix_preset")) {
			continue;
		}
		for (bNodeSocket *s = (bNodeSocket *)n->inputs.first; s; s = s->next) {
			if (strcmp(s->name, name) == 0 && s->type == SOCK_FLOAT && s->default_value) {
				r_linked = s->link != NULL;
				return (bNodeSocketValueFloat *)s->default_value;
			}
		}
	}
	return NULL;
}

/* the largest power of two not above n */
int pow2_floor(int n)
{
	int p = 1;
	while (p * 2 <= n) {
		p *= 2;
	}
	return p;
}

/* as remix_bake.cc names and encodes each of a bake's images */
const char *slot_suffix(int slot)
{
	switch (slot) {
		case BAKE_ALBEDO: return "_albedo";
		case BAKE_ROUGHNESS: return "_roughness";
		case BAKE_EMISSIVE: return "_emissive";
		case BAKE_NORMAL: return "_normal";
		case BAKE_HEIGHT: return "_height";
		default: return "_metallic";
	}
}

int slot_kind(int slot)
{
	switch (slot) {
		case BAKE_ALBEDO:
		case BAKE_EMISSIVE: return BRB_TEXTURE_COLOR;
		case BAKE_NORMAL: return BRB_TEXTURE_NORMAL;
		default: return BRB_TEXTURE_SCALAR;
	}
}

TextureRequest slot_request(int slot)
{
	TextureRequest t;
	t.kind = slot_kind(slot);
	t.channel = 0;
	return t;
}

/* the default tile set's largest tile (pixels): Ayhan's decision (7 Nov),
 * 256 (a 1024 atlas, a quick first start); a material marked Sharp
 * ("bge_remix_atlas_sharp"), seen up close: 512 */
const int DEFAULT_TILE_SIZE = 256;
const int SHARP_TILE_SIZE = 512;

/* the flipbook: P9.6's coordinates move by these a Seed (bge_remix_presets
 * _coords: across, and through the noise's third axis); how far the noise
 * moves through its third axis over a loop (water's waves are 0.4 across:
 * they change shape), and its normal map made stronger (Remix showed only
 * thin lines moving, 6 Nov) */
const float SEED_XY[2] = {17.17f, 31.31f};
const float SEED_Z = 7.77f;
const float DEFAULT_MORPH = 0.6f;
const float DEFAULT_NORMAL_GAIN = 2.0f;

/* the Seed put back whatever happens */
struct Restore {
	bNodeSocketValueFloat *seed;
	float seed_value;
	Restore() : seed(NULL), seed_value(0.0f) {}
	~Restore()
	{
		if (seed) {
			seed->value = seed_value;
		}
	}
};

/* the direction a flipbook's pattern moves (P9.6's Seed's, in the plane) */
const float FLOW_DIR[2] = {0.4810f, 0.8767f};

void trace(const std::string &what)
{
	static int on = -1;
	if (on < 0) {
		const char *env = getenv("BGE_REMIX_TRACE");
		on = (env && env[0]) ? 1 : 0;
	}
	if (on) {
		printf("BGE-Remix atlas: %s\n", what.c_str());
		fflush(stdout);
	}
}

std::string safe(const std::string &s)
{
	std::string r;
	for (size_t i = 0; i < s.size(); i++) {
		const char c = s[i];
		r += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') ? c :
		                                                                                                        '_';
	}
	return r;
}

/* tests (BGE_REMIX_BAKE_DUMP=<folder>): the atlas's top level as
 * <name>.ppm (RGB, top row first) and its layout as <name>.json */
void dump_atlas(const BakedImage &img, const AtlasLayout &L)
{
	const char *dir = getenv("BGE_REMIX_BAKE_DUMP");
	if (!dir || !dir[0] || img.width <= 0 || img.rgba.size() < (size_t)img.width * img.height * 4) {
		return;
	}
	/* "<material>_tiles.albedo": an atlas is no seamless tile (the earlier
	 * phases' tests read every "*_albedo.ppm" as one) */
	std::string name = img.name;
	const size_t us = name.rfind('_');
	if (us != std::string::npos) {
		name[us] = '.';
	}
	const std::string base = std::string(dir) + "/" + safe(name.substr(0, us)) +
	                         (us != std::string::npos ? "." + safe(name.substr(us + 1)) : std::string());
	FILE *f = fopen((base + ".ppm").c_str(), "wb");
	if (f) {
		fprintf(f, "P6\n%d %d\n255\n", img.width, img.height);
		for (int y = img.height - 1; y >= 0; y--) {
			for (int x = 0; x < img.width; x++) {
				fwrite(&img.rgba[((size_t)y * img.width + x) * 4], 1, 3, f);
			}
		}
		fclose(f);
	}
	f = fopen((base + ".json").c_str(), "w");
	if (f) {
		fprintf(f, "{\"kind\": %d, \"width\": %d, \"height\": %d, \"cols\": %d, \"rows\": %d, \"pitch\": %d, "
		           "\"gutter\": %d, \"colours\": %d, \"frames\": %d, \"fps\": %d, \"seed\": %u, \"levels\": %d, "
		           "\"cells\": [",
		        L.kind, L.width, L.height, L.cols, L.rows, L.pitch, L.gutter, L.colours, L.frames, L.fps, L.seed,
		        img.levels);
		for (size_t i = 0; i < L.cells.size(); i++) {
			fprintf(f, "%s[%d, %d, %d, %d]", i ? ", " : "", L.cells[i].x, L.cells[i].y, L.cells[i].w, L.cells[i].h);
		}
		fprintf(f, "]}\n");
		fclose(f);
	}
}

/* the last Python bake's albedo, smaller */
std::vector<unsigned char> g_preview;
int g_preview_w = 0, g_preview_h = 0;

void keep_preview(const BakedImage &img)
{
	int f = 1;
	while (img.width / f > 1024 || img.height / f > 1024) {
		f *= 2;
	}
	g_preview_w = std::max(1, img.width / f);
	g_preview_h = std::max(1, img.height / f);
	g_preview.assign((size_t)g_preview_w * g_preview_h * 4, 0);
	for (int y = 0; y < g_preview_h; y++) {
		for (int x = 0; x < g_preview_w; x++) {
			for (int c = 0; c < 4; c++) {
				unsigned s = 0;
				for (int dy = 0; dy < f; dy++) {
					for (int dx = 0; dx < f; dx++) {
						s += img.rgba[((size_t)(y * f + dy) * img.width + x * f + dx) * 4 + c];
					}
				}
				g_preview[((size_t)y * g_preview_w + x) * 4 + c] = (unsigned char)(s / (unsigned)(f * f));
			}
		}
	}
}

}  /* namespace */

bool atlas_tiles_by_default(const Material *ma)
{
	/* Ayhan's decision (7 Nov): a random preset's Remix texture is a 16-tile
	 * set unless the material says otherwise ("bge_remix_atlas" 0: a single
	 * tile); regular presets (Brick) and Water (its own: the flipbook) keep
	 * the single tile */
	if (!ma || prop(&ma->id, "bge_remix_atlas") || !bake_is_tile(ma)) {
		return false;
	}
	const std::string key = prop_string(&ma->id, "bge_remix_preset");
	if (key.empty() || key == "BRICK" || key == "WATER") {
		return false;
	}
	return prop_int(&ma->id, "bge_remix_tile_mode", 0) != TILE_WHOLE;
}

int atlas_kind(const Material *ma)
{
	if (atlas_tiles_by_default(ma)) {
		return ATLAS_TILES;
	}
	const int k = ma ? prop_int(&ma->id, "bge_remix_atlas", 0) : 0;
	return (k >= ATLAS_TILES && k <= ATLAS_PACKED) ? k : ATLAS_NONE;
}

bool atlas_bake(Scene *scene, Material *ma, const std::string &name, BakeResult &r,
                std::shared_ptr<AtlasLayout> &r_layout)
{
	r = BakeResult();
	r_layout.reset();
	const int kind = atlas_kind(ma);
	if (kind != ATLAS_TILES && kind != ATLAS_FLIPBOOK) {
		r.error = "no tile set or flipbook asked for";
		return false;
	}
	if (!bake_is_tile(ma)) {
		r.error = "only a preset's tile can be made into a tile set or a flipbook";
		return false;
	}
	bool linked = false;
	bNodeSocketValueFloat *seed = preset_input(ma, "Seed", linked);
	if (!seed || linked) {
		r.error = seed ? "its preset's Seed is wired to something: the tiles need it free" :
		                 "it has no preset with a Seed";
		return false;
	}
	bNodeSocketValueFloat *scale = preset_input(ma, "Scale", linked);
	const float s_scale = (scale && !linked) ? std::max(scale->value, 0.01f) : 1.0f;
	const unsigned slots = bake_slots(ma);
	/* a cell's size: the material's bge_remix_atlas_size, else its Bake Size
	 * (BGE_REMIX_BAKE_SIZE, the tests', over both) */
	int size = prop_int(&ma->id, "bge_remix_atlas_size", 0);
	if (size < 16 || size > 8192 || getenv("BGE_REMIX_BAKE_SIZE")) {
		size = bake_size(ma);
		/* by default (no choice made) at most 256 pixels a tile (Sharp:
		 * 512), so a game's first start stays quick */
		if (atlas_tiles_by_default(ma)) {
			size = std::min(size, prop_int(&ma->id, "bge_remix_atlas_sharp", 0) ? SHARP_TILE_SIZE : DEFAULT_TILE_SIZE);
		}
	}
	std::shared_ptr<AtlasLayout> L(new AtlasLayout());
	const bool regular = prop_int(&ma->id, "bge_remix_atlas_regular", 0) != 0;
	const int frames_asked = prop_int(&ma->id, "bge_remix_atlas_frames", 16) == 64 ? 64 : 16;
	const float shift = std::max(0.0f, std::min(prop_float(&ma->id, "bge_remix_atlas_shift",
	                                                       (float)frames_asked / 64.0f), 4.0f));
	const float morph = std::max(0.0f, std::min(prop_float(&ma->id, "bge_remix_atlas_morph", DEFAULT_MORPH), 8.0f));
	const float normal_gain = std::max(0.25f, std::min(prop_float(&ma->id, "bge_remix_atlas_normal_gain",
	                                                              DEFAULT_NORMAL_GAIN), 8.0f));
	if (kind == ATLAS_TILES) {
		const int colours = prop_int(&ma->id, "bge_remix_atlas_colours", 2) == 3 ? 3 : 2;
		const int pitch = pow2_floor(std::min(size, colours == 2 ? 1024 : 512));
		atlas_tiles_layout(colours, pitch, (unsigned)prop_int(&ma->id, "bge_remix_atlas_seed", 0), *L);
	}
	else {
		const int pitch = pow2_floor(std::min(size, frames_asked == 16 ? 512 : 256));
		atlas_flipbook_layout(frames_asked, prop_int(&ma->id, "bge_remix_atlas_fps", 8), pitch, *L);
	}
	if (L->kind == ATLAS_NONE) {
		r.error = "its atlas's size is too small";
		return false;
	}
	/* the atlas's content key: the material (its signature holds the node
	 * values and the bake size) and the atlas's own settings */
	{
		/* (2: a flipbook from one bake; 3: from a bake per phase; 4: the
		 * tiles' contrast kept per source, 7 Nov: the darker ones remade) */
		static const char version[] = "BGE-Remix atlas 5"; /* (5: 256-pixel default tiles) */
		uint64_t h = fnv(version, sizeof(version), 1469598103934665603ull);
		h = fnv_v(bake_signature(ma), h);
		h = fnv_v(L->kind, h);
		h = fnv_v(L->colours, h);
		h = fnv_v(L->frames, h);
		h = fnv_v(L->fps, h);
		h = fnv_v(L->pitch, h);
		h = fnv_v(L->seed, h);
		h = fnv_v(regular, h);
		h = fnv_v(shift, h);
		h = fnv_v(morph, h);
		h = fnv_v(normal_gain, h);
		h = fnv_v(size, h);
		L->key = h ? h : 1;
	}
	const std::string atlas_name = name + (kind == ATLAS_TILES ? "_tiles" : "_flipbook");

	/* made before: its files are in the cache */
	{
		bool all = slots != 0;
		std::shared_ptr<const BakedImage> found[BAKE_SLOTS];
		for (int s = 0; s < BAKE_SLOTS && all; s++) {
			if (!(slots & (1u << s))) {
				continue;
			}
			std::shared_ptr<BakedImage> img(new BakedImage());
			img->name = atlas_name + slot_suffix(s);
			img->width = L->width;
			img->height = L->height;
			img->key = L->key;
			img->cached = true;
			TextureRequest req = slot_request(s);
			req.baked = img;
			all = TextureCache::cached(req);
			found[s] = img;
		}
		if (all) {
			for (int s = 0; s < BAKE_SLOTS; s++) {
				r.images[s] = found[s];
			}
			r.from_cache = true;
			r.gpu_ms = 0.0;
			r.signature = bake_signature(ma);
			/* (as bake_material finds them for an RTX Remix material) */
			bool alpha = false;
			remix_node_bake_slots(ma, &alpha);
			r.has_alpha = alpha && (slots & (1u << BAKE_ALBEDO));
			if (slots & (1u << BAKE_EMISSIVE)) {
				r.emissive_intensity = std::max(
				        BKE_remix_node_value(BKE_material_remix_node(ma), SH_REMIX_IN_EMISSIVE_INTENSITY), 1.0f);
			}
			r_layout = L;
			trace("from the cache: " + atlas_name);
			return true;
		}
	}

	/* the Seeds of the parts: tiles, one a corner colour (bricks: steps of
	 * 100/101 of the Scale move the pattern by whole repeats, P9.6's
	 * coordinates moving 17.17 and 31.31 a Seed; only the noise changes);
	 * flipbook (Ayhan's decision, 7 Nov): one a phase of the loop, the Seed
	 * stepped so the noise moves through its third axis (P9.6: 7.77 a Seed:
	 * the waves change shape) and, along with it, across (17.17, 31.31 a
	 * Seed: put back on the CPU, key_shift, so the drift is the flow's) */
	std::vector<float> seeds, key_shift;
	unsigned frame_slots = ~0u;
	const float s0 = seed->value;
	if (kind == ATLAS_TILES) {
		const float step = regular ? 100.0f / 101.0f * s_scale : 7.31f;
		for (int c = 0; c < L->colours; c++) {
			seeds.push_back(s0 + step * (float)(c + 1));
		}
	}
	else {
		const int keys = morph > 0.0f ? L->frames : 1;
		const float span = morph / SEED_Z;
		for (int j = 0; j < keys; j++) {
			const float ds = span * (float)j / (float)L->frames;
			seeds.push_back(s0 + ds);
			/* (the pattern moved back by what the Seed moved it, in tiles: a
			 * UV unit spans Scale of the preset's coordinates) */
			key_shift.push_back(SEED_XY[0] * ds / s_scale);
			key_shift.push_back(SEED_XY[1] * ds / s_scale);
		}
		/* glass (water): Remix shows its normal map, the rest stays put:
		 * the phases bake only that (the first bakes everything) */
		const bNode *node = BKE_material_remix_node(ma);
		if (node && (node->custom1 & SH_NODE_REMIX_TRANSLUCENT)) {
			frame_slots = 1u << BAKE_NORMAL;
		}
	}

	Restore restore;
	restore.seed = seed;
	restore.seed_value = s0;
	std::vector<BakeResult> parts(seeds.size());
	double gpu_ms = 0.0;
	for (size_t i = 0; i < seeds.size(); i++) {
		seed->value = seeds[i];
		char part[32];
		snprintf(part, sizeof(part), "_part%02d", (int)i);
		trace("baking " + atlas_name + part);
		/* at the cell's size, always drawn (the pixels are needed here) */
		/* (no mesh: a preset's tile is drawn on bake_material's own square,
		 * with its material slots; fix round 2: the square made here had
		 * none, and the P10 guard against such meshes refused every part) */
		if (!bake_material(scene, ma, NULL, 1, NULL, name + part, parts[i], L->pitch, false,
		                   i == 0 ? ~0u : frame_slots)) {
			r.error = parts[i].error.empty() ? std::string("a part of it could not be baked") : parts[i].error;
			return false;
		}
		if (parts[i].from_cache) {
			r.error = "a part of it was found in the cache without its pixels";
			return false;
		}
		gpu_ms += std::max(parts[i].gpu_ms, 0.0);
	}
	seed->value = s0;

	/* each image of the bake made into the atlas, with its mipmaps */
	for (int s = 0; s < BAKE_SLOTS; s++) {
		if (!parts[0].images[s]) {
			continue;
		}
		std::shared_ptr<BakedImage> out(new BakedImage());
		out->name = atlas_name + slot_suffix(s);
		out->key = L->key;
		bool ok;
		if (kind == ATLAS_TILES) {
			std::vector<const BakedImage *> src;
			for (int c = 0; c < L->colours; c++) {
				src.push_back(parts[c].images[s].get());
			}
			ok = atlas_compose_tiles(*L, slot_kind(s), regular, src, *out);
		}
		else {
			/* the phases' keys of this image (one when only the first has it) */
			std::vector<const BakedImage *> keys;
			std::vector<float> ks;
			for (size_t j = 0; j < parts.size(); j++) {
				if (parts[j].images[s]) {
					keys.push_back(parts[j].images[s].get());
					ks.push_back(key_shift[j * 2]);
					ks.push_back(key_shift[j * 2 + 1]);
				}
			}
			if (keys.size() != parts.size()) {
				keys.resize(1);
				ks.resize(2);
			}
			ok = atlas_compose_flipbook(*L, slot_kind(s), keys, ks, shift * FLOW_DIR[0], shift * FLOW_DIR[1],
			                            slot_kind(s) == BRB_TEXTURE_NORMAL ? normal_gain : 1.0f, *out);
		}
		if (!ok) {
			r.error = "its parts did not fit together (sizes)";
			return false;
		}
		std::vector<unsigned char> levels;
		int count = 1;
		atlas_mips(*L, slot_kind(s), out->rgba.data(), out->width, out->height, levels, count);
		out->rgba.swap(levels);
		out->levels = count;
		dump_atlas(*out, *L); /* (its top level) */
		r.images[s] = out;
	}
	r.has_alpha = parts[0].has_alpha;
	r.emissive_intensity = parts[0].emissive_intensity;
	r.signature = bake_signature(ma);
	r.gpu_ms = gpu_ms;
	r_layout = L;
	printf("BGE-Remix: material \"%s\": %s (%d %s at %d pixels, %d bakes)\n", name.c_str(),
	       kind == ATLAS_TILES ? "a corner-matched tile set" : "a flipbook", (int)L->cells.size(),
	       kind == ATLAS_TILES ? "tiles" : "frames", L->pitch, (int)seeds.size());
	fflush(stdout);
	return true;
}

namespace {

/* Remix's relief (its parallax: displace_in + displace_out) is a depth in
 * texture coordinates (1.6.1's pomGetStep), while a preset's is in metres,
 * one UV unit a metre: a cell covering `share` of the atlas's UVs needs it
 * that much smaller (sign-off fix, 7 Nov: a 16-tile set's Gravel dug 4 x
 * deeper, 81 tiles 9 x, and came out dark with self-shadowing) */
void scale_relief(MaterialSpec &spec, float share)
{
	if (share > 0.0f && share < 1.0f) {
		spec.desc.displace_in *= share;
		spec.desc.displace_out *= share;
	}
}

}  /* namespace */

void atlas_apply(const AtlasLayout &L, MaterialSpec &spec)
{
	if (L.kind == ATLAS_TILES && !L.cells.empty() && L.width > 0) {
		scale_relief(spec, (float)L.cells[0].w / (float)L.width);
	}
	if (L.kind == ATLAS_FLIPBOOK && L.cols > 0) {
		/* (Remix's sprite sheet puts the UVs into the frame before the
		 * parallax) */
		scale_relief(spec, 1.0f / (float)L.cols);
	}
	if (L.kind == ATLAS_FLIPBOOK) {
		/* Remix plays it: frame (time x fps) mod frames, at column frame %
		 * cols and row frame / cols, the UVs wrapping inside the frame */
		spec.desc.sprite_rows = L.rows;
		spec.desc.sprite_cols = L.cols;
		spec.desc.sprite_fps = L.fps;
	}
}

std::shared_ptr<AtlasLayout> atlas_packed(const Material *ma, MaterialSpec &spec, std::string &r_note)
{
	r_note.clear();
	if (atlas_kind(ma) != ATLAS_PACKED || !G.main) {
		return std::shared_ptr<AtlasLayout>();
	}
	int cell = -1;
	Image *pages[TEX_COUNT] = {NULL};
	for (int i = 0; i < TEX_COUNT; i++) {
		const TextureRequest &req = spec.textures[i];
		if (!req.wanted()) {
			continue;
		}
		if (!req.image) {
			r_note = "it has baked textures: not packed";
			return std::shared_ptr<AtlasLayout>();
		}
		const std::string page = prop_string(&req.image->id, "bge_remix_atlas_page");
		const int c = prop_int(&req.image->id, "bge_remix_atlas_cell", -1);
		if (page.empty() || c < 0 || (cell >= 0 && c != cell)) {
			r_note = std::string("image \"") + (req.image->id.name + 2) + "\" is not packed with the others";
			return std::shared_ptr<AtlasLayout>();
		}
		cell = c;
		pages[i] = (Image *)BLI_findstring(&G.main->image, page.c_str(), offsetof(ID, name) + 2);
		if (!pages[i]) {
			r_note = "its page \"" + page + "\" is missing";
			return std::shared_ptr<AtlasLayout>();
		}
	}
	if (cell < 0) {
		return std::shared_ptr<AtlasLayout>();
	}
	/* the layout, from the first page (they share it) */
	const Image *first = NULL;
	for (int i = 0; i < TEX_COUNT && !first; i++) {
		first = pages[i];
	}
	std::vector<int> cells, size;
	if (!prop_ints(&first->id, "bge_remix_atlas_cells", cells) || cells.size() % 5 != 0 ||
	    !prop_ints(&first->id, "bge_remix_atlas_size", size) || size.size() != 2 ||
	    cell >= (int)(cells.size() / 5))
	{
		r_note = "its page has no layout";
		return std::shared_ptr<AtlasLayout>();
	}
	std::shared_ptr<AtlasLayout> L(new AtlasLayout());
	L->kind = ATLAS_PACKED;
	L->width = size[0];
	L->height = size[1];
	L->gutter = std::max(0, prop_int(&first->id, "bge_remix_atlas_gutter", 0));
	L->own_cell = cell;
	for (size_t i = 0; i + 4 < cells.size(); i += 5) {
		AtlasCell c;
		c.x = cells[i];
		c.y = cells[i + 1];
		c.w = cells[i + 2];
		c.h = cells[i + 3];
		c.repeat = cells[i + 4] != 0;
		L->cells.push_back(c);
	}
	uint64_t h = fnv(first->id.name, strlen(first->id.name), 1469598103934665603ull);
	h = fnv(cells.data(), cells.size() * sizeof(int), h);
	L->key = h;
	for (int i = 0; i < TEX_COUNT; i++) {
		if (pages[i]) {
			spec.textures[i].image = pages[i];
			spec.textures[i].atlas = L;
			spec.textures[i].max_size = 0; /* (the page as it is: the cells' places) */
		}
	}
	/* (its relief: as atlas_apply's) */
	const AtlasCell &own = L->cells[cell];
	if (L->width > 0 && L->height > 0) {
		scale_relief(spec, 0.5f * ((float)own.w / (float)L->width + (float)own.h / (float)L->height));
	}
	return L;
}

void atlas_face_picks(Mesh *me, std::vector<int> &r_picks)
{
	r_picks.clear();
	if (!me) {
		return;
	}
	const int *layer = (const int *)CustomData_get_layer_named(&me->pdata, CD_PROP_INT, PICK_LAYER);
	if (!layer) {
		return;
	}
	bool any = false;
	for (int i = 0; i < me->totpoly && !any; i++) {
		any = layer[i] > 0;
	}
	if (!any) {
		return;
	}
	/* the game's polygons are the tessellated faces, as it converts them
	 * (BL_BlenderDataConversion: CDDM_from_mesh, DM_ensure_tessface) */
	DerivedMesh *dm = CDDM_from_mesh(me);
	DM_ensure_tessface(dm);
	const int nf = dm->getNumTessFaces(dm);
	const int *orig = (const int *)dm->getTessFaceDataArray(dm, CD_ORIGINDEX);
	r_picks.assign((size_t)nf, 0);
	for (int f = 0; f < nf; f++) {
		const int p = orig ? orig[f] : f;
		r_picks[f] = (p >= 0 && p < me->totpoly) ? std::max(layer[p], 0) : 0;
	}
	dm->release(dm);
}

void atlas_dump_squares(const std::string &mesh, const std::string &material, const AtlasLayout &L,
                        const std::vector<AtlasSquare> &squares)
{
	const char *dir = getenv("BGE_REMIX_ATLAS_DUMP");
	if (!dir || !dir[0]) {
		return;
	}
	const std::string path = std::string(dir) + "/" + safe(mesh) + "__" + safe(material) + ".json";
	FILE *f = fopen(path.c_str(), "w");
	if (!f) {
		return;
	}
	fprintf(f, "{\"mesh\": \"%s\", \"material\": \"%s\", \"kind\": %d, \"colours\": %d, \"seed\": %u, "
	           "\"own_cell\": %d, \"squares\": [",
	        safe(mesh).c_str(), safe(material).c_str(), L.kind, L.colours, L.seed, L.own_cell);
	for (size_t i = 0; i < squares.size(); i++) {
		fprintf(f, "%s[%d, %d, %d, %d]", i ? ", " : "", squares[i].i, squares[i].j, squares[i].cell,
		        squares[i].picked ? 1 : 0);
	}
	fprintf(f, "]}\n");
	fclose(f);
}

}  /* namespace brmx */

/* ------------------------------------------------------------------------- */
/* for Python */

using namespace brmx;

const char *BRMX_atlas_bake_json(Material *ma, Scene *scene)
{
	static std::string json;
	char buf[512];
	std::string error;
	std::shared_ptr<AtlasLayout> L;
	BakeResult r;
	std::vector<std::string> files;
	bool ok = false;
	const std::string name = ma ? std::string(ma->id.name) : std::string();
	if (!ma || !scene) {
		error = "no material or scene";
	}
	else if (G.background) {
		error = "no OpenGL in the background: the game bakes it when it starts";
	}
	else {
		ok = atlas_bake(scene, ma, name, r, L);
		if (!ok) {
			error = r.error;
		}
	}
	if (ok) {
		/* its files encoded now, so the game starts with them */
		TextureCache cache;
		for (int s = 0; s < BAKE_SLOTS; s++) {
			if (!r.images[s]) {
				continue;
			}
			if (s == BAKE_ALBEDO && !r.images[s]->cached) {
				keep_preview(*r.images[s]);
			}
			TextureRequest req = slot_request(s);
			req.baked = r.images[s];
			std::wstring path;
			cache.request(req, path);
			char p8[1024];
			size_t n = 0;
			for (; n < path.size() && n + 1 < sizeof(p8); n++) {
				p8[n] = (path[n] < 128) ? (char)path[n] : '_';
			}
			p8[n] = 0;
			files.push_back(p8);
		}
		for (int wait = 0; cache.waiting() > 0 && wait < 6000; wait++) {
			cache.poll();
#ifdef _WIN32
			Sleep(50);
#endif
		}
		if (cache.failed > 0) {
			ok = false;
			error = "some of its files could not be encoded";
		}
	}
	json = "{";
	snprintf(buf, sizeof(buf), "\"ok\": %s, \"from_cache\": %s, ", ok ? "true" : "false",
	         r.from_cache ? "true" : "false");
	json += buf;
	std::string e;
	for (size_t i = 0; i < error.size(); i++) {
		const char c = error[i];
		e += (c == '"' || c == '\\') ? '\'' : c;
	}
	json += "\"error\": \"" + e + "\"";
	if (L) {
		snprintf(buf, sizeof(buf),
		         ", \"kind\": %d, \"width\": %d, \"height\": %d, \"cols\": %d, \"rows\": %d, \"pitch\": %d, "
		         "\"gutter\": %d, \"colours\": %d, \"frames\": %d, \"fps\": %d, \"seed\": %u, \"cells\": [",
		         L->kind, L->width, L->height, L->cols, L->rows, L->pitch, L->gutter, L->colours, L->frames, L->fps,
		         L->seed);
		json += buf;
		for (size_t i = 0; i < L->cells.size(); i++) {
			snprintf(buf, sizeof(buf), "%s[%d, %d, %d, %d]", i ? ", " : "", L->cells[i].x, L->cells[i].y,
			         L->cells[i].w, L->cells[i].h);
			json += buf;
		}
		json += "]";
	}
	json += ", \"files\": [";
	for (size_t i = 0; i < files.size(); i++) {
		std::string f;
		for (size_t k = 0; k < files[i].size(); k++) {
			f += files[i][k] == '\\' ? '/' : files[i][k];
		}
		json += (i ? ", \"" : "\"") + f + "\"";
	}
	json += "]}";
	return json.c_str();
}

const unsigned char *BRMX_atlas_preview(int *r_width, int *r_height)
{
	*r_width = g_preview_w;
	*r_height = g_preview_h;
	return g_preview.empty() ? NULL : g_preview.data();
}
