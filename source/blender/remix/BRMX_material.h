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

/** \file BRMX_material.h
 *  \ingroup remix
 *
 * BGE-Remix: Blender materials and images as RTX Remix sees them (C++).
 *
 * One translator for every place that sends materials to Remix (the game
 * engine now, the 3D view later), so a fix helps all of them:
 *
 *   1. 2.79's Cycles nodes, when the material uses nodes with a Cycles
 *      Material Output: Principled, Diffuse, Glossy, Glass, Refraction,
 *      Emission, Subsurface Scattering, Transparent (mixed in), Image
 *      Texture, Normal Map
 *   2. else the physical material (P8.7, BKE_material_physical.h; the RTX
 *      Remix panel's values were moved into it): base colour, metallic,
 *      roughness, glass, subsurface, emission, game transparency, and the
 *      texture slots
 *   3. else Blender Internal (Classic): colour, specular hardness, emit,
 *      shadeless, game transparency, and image texture slots (Color,
 *      Normal, Emit, Hardness)
 *
 * Procedural texture slots are baked per mesh (P8.6, BRMX_bake.h), and so
 * are node materials the translator cannot follow (P8.24).
 *
 * P8.24: the material's Remix block (ID properties "bge_remix", the RTX
 * Remix panel) sets any of Remix's fields over the translation: each slot
 * a value and an optional image, with its colour space, channel, compression
 * and largest size; glass, subsurface, anisotropy, thin film, height map,
 * blending, alpha test, filtering, wrapping and the Remix categories. Every
 * field is reported as reaching Remix exactly, approximately or not at all
 * (material_match).
 *
 * The texture cache turns the images these refer to into DDS files (Remix
 * reads nothing else), next to the .blend in remix_cache/, encoded once and
 * reused while the image does not change.
 */

#ifndef __BRMX_MATERIAL_H__
#define __BRMX_MATERIAL_H__

#ifndef __cplusplus
#  error "BRMX_material.h is C++"
#endif

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "BRMX_loader.h"

struct Image;
struct Material;

namespace brmx {

struct BakedImage; /* BRMX_bake.h */

enum TextureSlot {
	TEX_ALBEDO = 0,
	TEX_NORMAL,
	TEX_ROUGHNESS,
	TEX_METALLIC,
	TEX_EMISSIVE,
	/* P8.24 */
	TEX_HEIGHT,
	TEX_TANGENT,
	TEX_TRANSMITTANCE,
	TEX_SSS_TRANSMITTANCE,
	TEX_SSS_THICKNESS,
	TEX_SSS_ALBEDO,
	TEX_SSS_RADIUS,
	TEX_COUNT
};

const char *texture_slot_name(int slot);

/* P8.24: the desc's file path field of a slot */
const wchar_t **texture_field(brb_material_desc &d, int slot);

/* P8.24: how well each of Remix's fields carries the Blender material */
enum MatchLevel { MATCH_EXACT = 0, MATCH_APPROX = 1, MATCH_NONE = 2 };
struct FieldMatch {
	std::string field; /* Remix's name for it */
	int level;         /* MatchLevel */
	std::string note;  /* where the value comes from, or what is lost */
};

/* An image wanted in a material slot, before it is a DDS file. */
struct TextureRequest {
	Image *image;
	/* P8.6: or the pixels of a baked procedural texture (comes first) */
	std::shared_ptr<const BakedImage> baked;
	int kind;     /* brb_texture_kind */
	int channel;  /* scalar maps: 0 = R .. 3 = A */
	int flags;    /* BRB_TEXTURE_INVERT, BRB_TEXTURE_LINEAR */
	int max_size; /* P8.24: the largest width or height (0: as the image is) */
	/* BGE-Remix P10.3: a colour image's colours times this (in linear
	 * light) before encoding: a Classic material's Diffuse Intensity, which
	 * the game multiplies its colour image by and Remix's albedo image does
	 * not take (Remix's albedo colour is only used without an image) */
	float scale;
	TextureRequest() : image(NULL), kind(0), channel(0), flags(0), max_size(0), scale(1.0f) {}
	bool wanted() const { return image != NULL || baked; }
};

struct MaterialSpec {
	/* every value; the texture fields stay NULL here (see TextureCache) */
	brb_material_desc desc;
	TextureRequest textures[TEX_COUNT];
	enum Source { DEFAULT, PANEL, NODES, BLENDER_INTERNAL, PHYSICAL, REMIX_NODE } source;
	/* what the translator could not represent, one line each */
	std::vector<std::string> notes;
	/* P4: drawn as a particle or sprite (Halo face orientation, or the RTX
	 * Remix panel's Particle option) */
	bool particle;
	/* P8.6: values that come from procedural textures, to bake (bits of
	 * 1 << BakeSlot, BRMX_bake.h); the caller bakes them per mesh */
	unsigned bake_slots;
	/* P8.24: the Remix categories the material asks for (BRB_CATEGORY_*) */
	unsigned categories;
	/* P8.24: slots (1 << TextureSlot) the Remix block set: a bake leaves them */
	unsigned locked;
	/* P8.24: fields the translation carried approximately or not at all, by
	 * Remix's field name (see material_match) */
	std::map<std::string, std::pair<int, std::string> > marks;
	/* P8.24: node material baked into Remix's channels */
	bool nodes_baked;
	/* P8.30: a shadeless material: it glows with its colour image (a baked
	 * one too: apply_bake) */
	bool shadeless;
	/* P9.4: the transform every one of the material's direct textures
	 * shares, applied to the UVs sent for its faces (apply_uv_transform):
	 * u' = m[0] u + m[1] v + m[2], v' = m[3] u + m[4] v + m[5] */
	bool uv_transform = false;
	float uv_matrix[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
	/* P9.4: how the textures' settings went (the Match panel's "filtering
	 * and wrapping"): Remix's wrap and filter, the UVs, a bake */
	std::string placement;
};

/* P8.24: every Remix field of the material and how it gets there */
void material_match(const Material *ma, const MaterialSpec &spec, std::vector<FieldMatch> &r);

/* P8.24: the material's Remix block, read (whether it is in force) */
bool remix_block_used(const Material *ma);

/* P8.24: a node material the translator cannot follow, to bake: its Remix
 * block's Bake Nodes (Automatic: when the translation left something out) */
bool nodes_need_bake(const Material *ma);

/* P9.2: an RTX Remix material's inputs to bake (bits of 1 << BakeSlot,
 * BRMX_bake.h): those wired to something other than an image texture read
 * by UV; and whether the albedo's bake carries the opacity */
unsigned remix_node_bake_slots(const Material *ma, bool *r_alpha = NULL);

/* P9.4: a Blender Internal material's image texture slots to bake (bits of
 * 1 << BakeSlot): those whose settings Remix cannot take as they are
 * (influence below 1, a blend other than Mix, several slots in one channel,
 * coordinates other than UV, the Colors panel, Negative, RGB to intensity,
 * Stencil, Checker or crop, a projection or mapping other than flat XY), and
 * all of them when the rest disagree on extension, interpolation or
 * transform, or share a transform while something else is baked (a bake is
 * drawn in the mesh's own UVs). bake_slots() adds them. */
unsigned image_slot_bake_slots(const Material *ma);

/* P9.4: an RTX Remix material's inputs fed by images straight whose
 * settings disagree (extension, interpolation, transform): baked */
unsigned remix_node_disagreeing_slots(const Material *ma);

/* P9.4: the material's UV transform on n vertices (nothing when it has none) */
void apply_uv_transform(const MaterialSpec &s, brb_vertex *v, size_t n);

/* P9.2: the texture cache flips an image's green channel first (a DirectX
 * normal map); never passed to the bridge */
#define BRMX_TEXTURE_FLIP_GREEN (1 << 7)

/* The Remix version of a material (NULL: Blender's default material).
 * desc.hash is left 0 for the caller. */
void translate_material(const Material *ma, MaterialSpec &r_spec);

/* BGE-Remix P8.4: a material with Face Textures takes its colour from the
 * image assigned to the faces (UV editor), as the game draws it. Call after
 * translate_material, once per face image; nothing changes when the
 * material has no Face Textures or face_image is NULL. */
void apply_face_image(const Material *ma, Image *face_image, MaterialSpec &r_spec);

/* Whether the material takes its colour from the faces' images. */
bool uses_face_image(const Material *ma);

/* The texture cache (one per game). All calls from the main thread. */
class TextureCache {
public:
	enum State { MISSING = 0, READY = 1, WAITING = 2, FAILED = 3 };

	TextureCache();

	/* The DDS for this request: READY (r_path set, the file exists), WAITING
	 * (being encoded; ask again later) or FAILED / MISSING (no image). */
	State request(const TextureRequest &req, std::wstring &r_path);

	/* Collect finished encodes; call once per frame while waiting() > 0. */
	void poll();
	int waiting() const;

	/* The folder: BGE_REMIX_CACHE_DIR, else remix_cache next to the .blend,
	 * else in the temp folder. */
	static std::wstring folder();

	/* P8.7: the file a baked image with a content key goes to, and whether
	 * it is there already */
	static std::wstring baked_path(const TextureRequest &req);
	static bool cached(const TextureRequest &req);

	/* BGE-Remix P9.7: for the Remix View's inspection: the file a request
	 * goes to, or for an image (named by its pixels' hash too) the start of
	 * that file's name ("<image>_<kind>_"); whether the file is there */
	static std::wstring file_name_of(const TextureRequest &req, bool *r_exact);

	/* for the tests' statistics */
	int encoded, reused, failed;
	double encode_seconds;

private:
	struct Entry {
		State state;
		std::wstring path;
		int ticket;
		Entry() : state(MISSING), ticket(0) {}
	};
	typedef std::pair<const void *, int> Key; /* the Image or the BakedImage */
	std::map<Key, Entry> m_entries;
	int m_waiting;
};

/* P8.24: the game's OpenGL draws 8-bit colour images from the BC7 files of
 * this cache when they are there (GPU_set_image_cache); on at the game's
 * start, off at its end. */
void install_gl_image_cache(bool on);

}  /* namespace brmx */

#endif  /* __BRMX_MATERIAL_H__ */
