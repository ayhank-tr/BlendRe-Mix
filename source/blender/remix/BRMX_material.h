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
 *   1. the material's RTX Remix panel, when it is switched on (always wins)
 *   2. else 2.79's Cycles nodes, when the material uses nodes with a Cycles
 *      Material Output: Principled, Diffuse, Glossy, Glass, Refraction,
 *      Emission, Subsurface Scattering, Transparent (mixed in), Image
 *      Texture, Normal Map
 *   3. else Blender Internal: colour, specular hardness, emit, shadeless,
 *      game transparency, and image texture slots (Color, Normal, Emit,
 *      Hardness)
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
#include <string>
#include <vector>

#include "BRMX_loader.h"

struct Image;
struct Material;

namespace brmx {

enum TextureSlot {
	TEX_ALBEDO = 0,
	TEX_NORMAL,
	TEX_ROUGHNESS,
	TEX_METALLIC,
	TEX_EMISSIVE,
	TEX_COUNT
};

const char *texture_slot_name(int slot);

/* An image wanted in a material slot, before it is a DDS file. */
struct TextureRequest {
	Image *image;
	int kind;     /* brb_texture_kind */
	int channel;  /* scalar maps: 0 = R .. 3 = A */
	int flags;    /* BRB_TEXTURE_INVERT, BRB_TEXTURE_LINEAR */
	TextureRequest() : image(NULL), kind(0), channel(0), flags(0) {}
};

struct MaterialSpec {
	/* every value; the texture fields stay NULL here (see TextureCache) */
	brb_material_desc desc;
	TextureRequest textures[TEX_COUNT];
	enum Source { DEFAULT, PANEL, NODES, BLENDER_INTERNAL } source;
	/* what the translator could not represent, one line each */
	std::vector<std::string> notes;
	/* P4: drawn as a particle or sprite (Halo face orientation, or the RTX
	 * Remix panel's Particle option) */
	bool particle;
};

/* The Remix version of a material (NULL: Blender's default material).
 * desc.hash is left 0 for the caller. */
void translate_material(const Material *ma, MaterialSpec &r_spec);

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
	typedef std::pair<const Image *, int> Key;
	std::map<Key, Entry> m_entries;
	int m_waiting;
};

}  /* namespace brmx */

#endif  /* __BRMX_MATERIAL_H__ */
