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

/** \file BRMX_atlas.h
 *  \ingroup remix
 *
 * BGE-Remix P10.4: atlases for RTX Remix (the game's OpenGL draws presets
 * live and needs none of this).
 *
 *   tiles     a preset baked as corner-matched tiles (Wang tiles): each
 *             corner of a tile has one of 2 (16 tiles) or 3 (81 tiles)
 *             colours, each colour the preset at its own Seed; the tiles go
 *             into one atlas laid out as a valid tiling (each 2 x 2 corner
 *             pattern once, wrapping round: a de Bruijn torus), with a gap
 *             round each cell filled with the tile carried on past its edge.
 *             The mesh sent to Remix is split along its UV grid (one square
 *             per UV unit) and each square shows the tile whose corners
 *             agree with its neighbours': a corner's colour is a hash of the
 *             grid corner. A face can be given a tile by hand (its
 *             "bge_remix_cell" face layer: tile + 1).
 *   flipbook  an animated preset baked as a looping run of frames into a
 *             sprite sheet (16 frames, 4 x 4); Remix plays it by itself (the
 *             material's sprite sheet rows, columns and frames per second:
 *             it picks the frame by time, for the whole material).
 *   packed    small images packed into shared pages (by Python with xatlas,
 *             bge_remix.atlas.pack_images): the material's textures read
 *             the page, the mesh's UVs are moved into the image's cell.
 *
 * Every atlas gets its own mipmaps: each level filtered within the cells,
 * and where a level's gap has gone, the cells' edges averaged (tiles: over
 * all tiles meeting on that kind of edge, so neighbours agree; frames and
 * repeating images: their opposite edges, so the wrap agrees).
 *
 * The pure parts (layouts, composing, mipmaps, splitting) are in
 * remix_atlas_core.cc, the Blender parts in remix_atlas.cc.
 */

#ifndef __BRMX_ATLAS_H__
#define __BRMX_ATLAS_H__

#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

#include "bge_remix_bridge.h"

struct Material;
struct Mesh;
struct Scene;

namespace brmx {

struct BakedImage;
struct BakeResult;
struct MaterialSpec;

enum AtlasKind { ATLAS_NONE = 0, ATLAS_TILES = 1, ATLAS_FLIPBOOK = 2, ATLAS_PACKED = 3 };

/* a cell's content in pixels of the top level, from the bottom left */
struct AtlasCell {
	int x, y, w, h;
	bool repeat; /* packed: the image repeats (its gap and wrap carry on), else its edge does */
	AtlasCell() : x(0), y(0), w(0), h(0), repeat(true) {}
};

struct AtlasLayout {
	int kind;
	int width, height; /* the page, pixels */
	int cols, rows;    /* tiles and frames: the grid of cells */
	int pitch;         /* tiles and frames: a grid cell's side (a power of two) */
	int gutter;        /* pixels of gap round each cell's content (frames: 0) */
	int colours;       /* tiles: corner colours (2 or 3) */
	int frames, fps;   /* flipbook */
	unsigned seed;     /* tiles: the corner colours' hash */
	int own_cell;      /* packed: the material's own image */
	std::vector<AtlasCell> cells; /* tiles: by tile index; frames: by frame; packed: the page's images */
	uint64_t key;      /* the atlas's content (the cache files are named by it) */
	AtlasLayout() : kind(ATLAS_NONE), width(0), height(0), cols(0), rows(0), pitch(0), gutter(0), colours(0),
	                frames(0), fps(0), seed(0), own_cell(0), key(0) {}
};

/* ------------------------------------------------------------------------- */
/* the pure parts (remix_atlas_core.cc) */

/* tiles: a tile's index from its corners' colours (south-west, south-east,
 * north-east, north-west), and back */
int atlas_tile_index(int sw, int se, int ne, int nw, int colours);
void atlas_tile_corners(int tile, int colours, int r_corners[4]);

/* tiles: the colour of UV grid corner (i, j) (a hash; the same in
 * bge_remix.atlas.corner_colour), and the tile UV square (i, j) shows */
int atlas_corner_colour(int i, int j, unsigned seed, int colours);
int atlas_square_tile(int i, int j, unsigned seed, int colours);

/* tiles: the corner colours of the atlas's grid (colours^2 a side, row by
 * row from the bottom): every 2 x 2 pattern once, wrapping round; NULL
 * for other colour counts */
const unsigned char *atlas_torus(int colours);

/* the layouts: tiles at `pitch` pixels a cell (its gap pitch / 32); a
 * flipbook of `frames` frames (a square grid) at `pitch` */
void atlas_tiles_layout(int colours, int pitch, unsigned seed, AtlasLayout &r);
void atlas_flipbook_layout(int frames, int fps, int pitch, AtlasLayout &r);

/* tiles: the atlas of one of a bake's images (kind: BRB_TEXTURE_COLOR,
 * NORMAL or SCALAR) from each corner colour's seamless tile (src[colour]),
 * blended by the corners' weights; regular (bricks): a plain blend, the
 * sources' pattern being in step; random: a blend that keeps the detail's
 * contrast */
bool atlas_compose_tiles(const AtlasLayout &L, int kind, bool regular, const std::vector<const BakedImage *> &src,
                         BakedImage &r_out);

/* flipbook: frame k of `frames` from A = the pattern at k / frames of the
 * loop and B = a loop earlier, cross-faded by k / frames keeping the
 * contrast (frame 0 is A alone: the last frame runs into the first) */
bool atlas_compose_flipbook(const AtlasLayout &L, int kind, const std::vector<const BakedImage *> &a,
                            const std::vector<const BakedImage *> &b, BakedImage &r_out);

/* the atlas's mipmaps from its top level (rows from the bottom), every
 * level one after the other into r_levels; r_count levels (to 1 x 1) */
void atlas_mips(const AtlasLayout &L, int kind, const unsigned char *rgba, int width, int height,
                std::vector<unsigned char> &r_levels, int &r_count);

/* the bytes of `count` levels of a width x height image */
size_t atlas_levels_size(int width, int height, int count);

/* a UV square of a split surface, for the tests' dump */
struct AtlasSquare {
	int i, j, cell;
	bool picked;
};

/* tiles and packed: the surface (vertices, triangles) split along the UV
 * grid and its UVs moved into the cells. tri_pick: per triangle, 0 or a
 * cell picked by hand + 1 (may be NULL). r_source: per vertex made, the
 * vertex it came from (the nearest corner: skinned surfaces' weights).
 * r_squares: the squares used. False when nothing was changed (r_source
 * then left empty). */
bool atlas_split(const AtlasLayout &L, std::vector<brb_vertex> &v, std::vector<uint32_t> &idx,
                 const std::vector<int> *tri_pick, std::vector<uint32_t> *r_source,
                 std::vector<AtlasSquare> *r_squares);

/* ------------------------------------------------------------------------- */
/* the Blender parts (remix_atlas.cc) */

/* the atlas a material asks for (its "bge_remix_atlas", set by
 * bge_remix.atlas): ATLAS_TILES or ATLAS_FLIPBOOK (baked presets),
 * ATLAS_PACKED, or ATLAS_NONE */
int atlas_kind(const Material *ma);

/* tiles and flipbooks: the preset baked several times (its Seed moved) and
 * made into the atlas's images (r.images, each with its mipmaps), as
 * bake_material does for the plain tile. Main thread, OpenGL current.
 * False: r.error says why (the caller then bakes the plain tile). */
bool atlas_bake(Scene *scene, Material *ma, const std::string &name, BakeResult &r,
                std::shared_ptr<AtlasLayout> &r_layout);

/* the atlas's fields of the Remix material (flipbook: the sprite sheet) */
void atlas_apply(const AtlasLayout &L, MaterialSpec &spec);

/* packed: the material's textures moved onto their page (when all of them
 * are packed into one cell); the layout to split its surfaces by, else
 * NULL (note: why not, or empty) */
std::shared_ptr<AtlasLayout> atlas_packed(const Material *ma, MaterialSpec &spec, std::string &r_note);

/* per tessellated face of the mesh (the game's polygons, in order): the
 * cell picked by hand + 1, or 0; empty when the mesh has no picks */
void atlas_face_picks(Mesh *me, std::vector<int> &r_picks);

/* tests (BGE_REMIX_ATLAS_DUMP=<folder>): a split surface's squares as
 * <mesh>__<material>.json */
void atlas_dump_squares(const std::string &mesh, const std::string &material, const AtlasLayout &L,
                        const std::vector<AtlasSquare> &squares);

}  /* namespace brmx */

extern "C" {
/* for Python (_bge_remix.atlas_bake): bake a material's atlas now (the
 * Blender UI: OpenGL is there) and encode its files into the cache,
 * waiting for them; JSON text (layout, files, error) */
const char *BRMX_atlas_bake_json(struct Material *ma, struct Scene *scene);
/* the last such bake's albedo atlas, smaller (at most 1024 a side), RGBA
 * rows from the bottom; NULL without one */
const unsigned char *BRMX_atlas_preview(int *r_width, int *r_height);
}

#endif  /* __BRMX_ATLAS_H__ */
