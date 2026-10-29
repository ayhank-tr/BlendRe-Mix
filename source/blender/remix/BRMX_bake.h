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

/** \file BRMX_bake.h
 *  \ingroup remix
 *
 * BGE-Remix P8.6: procedural textures for RTX Remix, which only reads
 * texture files. A Blender Internal material whose texture slots use
 * procedural textures is drawn in the mesh's UV space on the graphics card
 * with the same GLSL code the game uses (GPU_material_bake_create), and the
 * results become images: colour (albedo), hardness (roughness), emit
 * (emissive) and bump (a tangent-space normal map). The images go through
 * the texture cache like any other (the DDS files are named by their
 * pixels, so an unchanged bake reuses its file).
 *
 * A bake depends on the mesh (its UV map, and its shape for coordinates
 * other than UV), so these materials are made once per mesh.
 */

#ifndef __BRMX_BAKE_H__
#define __BRMX_BAKE_H__

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

struct DerivedMesh;
struct Material;
struct Mesh;
struct Scene;

namespace brmx {

struct MaterialSpec;

/* the pixels of a bake, bottom row first as Blender's images */
struct BakedImage {
	std::string name; /* for the cache file's name */
	int width, height;
	std::vector<unsigned char> rgba;
	BakedImage() : width(0), height(0) {}
};

enum BakeSlot { BAKE_ALBEDO = 0, BAKE_ROUGHNESS, BAKE_EMISSIVE, BAKE_NORMAL, BAKE_METALLIC, BAKE_SLOTS };

struct BakeResult {
	std::shared_ptr<const BakedImage> images[BAKE_SLOTS];
	bool has_alpha;           /* the albedo's alpha is the material's (textured) alpha */
	float emissive_intensity; /* the emissive image is emit / this */
	uint64_t signature;       /* bake_signature() when baked */
	std::string error;        /* why something was not baked */
	BakeResult() : has_alpha(false), emissive_intensity(1.0f), signature(0) {}
};

/* Which values of a material come from procedural textures (bits 1 <<
 * BakeSlot; 0: nothing to bake). Blender Internal materials only. */
unsigned bake_slots(const Material *ma);

/* A hash of everything a bake reads from the material and its textures,
 * to bake again when it changes. */
uint64_t bake_signature(const Material *ma);

/* The size of a material's baked images: the RTX Remix panel's Bake Size
 * (the material's "bge_remix_bake_size"), else 1024; BGE_REMIX_BAKE_SIZE
 * overrides it (tests). */
int bake_size(const Material *ma);

/* The mesh as the game engine converts it (no modifiers), with the
 * original coordinates a bake reads; release it with dm->release(dm). */
DerivedMesh *bake_mesh_from(Mesh *me);

/* Bake the material slot `slot` (1-based) of dm, placed by obmat. Main
 * thread, with an OpenGL context current. False: r.error says why. */
bool bake_material(Scene *scene, Material *ma, DerivedMesh *dm, int slot, const float obmat[4][4],
                   const std::string &name, BakeResult &r);

/* The baked images into the Remix material. */
void apply_bake(const BakeResult &r, MaterialSpec &spec);

}  /* namespace brmx */

#endif  /* __BRMX_BAKE_H__ */
