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

/** \file BRMX_remaster.h
 *  \ingroup remix
 *
 * BGE-Remix P10.2 and P10.5: what the texture remaster (the Image editor's
 * RTX Remix Remaster panel, bge_remix.remaster) needs from the C side:
 *
 * - a preset's tile baked to pixels, at a given size, always drawn (never
 *   taken from the cache): the catalogue's small bakes, the fits' trials and
 *   the previews (P10.2);
 * - Apply's up-front encoding (P10.5): an object's materials made into
 *   Remix's images exactly as the game makes them (the same translation,
 *   the same bakes and names, the same cache files), so a game started
 *   afterwards finds every texture in the cache.
 *
 * Main thread, with Blender's OpenGL context current (Blender with a
 * window; not in the background).
 */

#ifndef __BRMX_REMASTER_H__
#define __BRMX_REMASTER_H__

#ifdef __cplusplus
extern "C" {
#endif

struct Material;
struct Object;
struct Scene;

#define BRMX_REMASTER_SLOTS 6 /* BRMX_bake.h's BakeSlot: albedo, roughness, emissive, normal, metallic, height */

/* A preset material's tile (the material's "bge_remix_tile") baked at
 * size x size: for each slot asked for (bits 1 << slot) and baked, its
 * pixels (RGBA bytes, bottom row first) in r_pixels[slot], freed with
 * BRMX_remaster_free. Returns the slots baked (0: r_error says why). */
unsigned BRMX_remaster_bake_tile(struct Scene *scene, struct Material *ma, int size, unsigned slots,
                                 unsigned char *r_pixels[BRMX_REMASTER_SLOTS], char *r_error, int error_len);
void BRMX_remaster_free(unsigned char *pixels);

/* P10.5: the object's materials as the game sends them to Remix: their
 * procedural textures and presets baked (a preset's tile named by its
 * material, as the game names it), every image asked of the texture cache
 * (encoded on the bridge's thread when its file is not there yet). Returns
 * the textures asked for (-1: r_error says why). */
int BRMX_remaster_precache(struct Scene *scene, struct Object *ob, char *r_error, int error_len);

/* the encodes started by BRMX_remaster_precache: still waiting, encoded,
 * found in the cache, failed, the encoder's seconds, and bakes drawn */
void BRMX_remaster_poll(int *r_waiting, int *r_encoded, int *r_reused, int *r_failed, double *r_seconds,
                        int *r_bakes);

/* a fresh count (the next Apply) */
void BRMX_remaster_reset(void);

#ifdef __cplusplus
}
#endif

#endif  /* __BRMX_REMASTER_H__ */
