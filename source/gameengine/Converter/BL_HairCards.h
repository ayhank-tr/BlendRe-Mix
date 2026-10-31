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

/** \file BL_HairCards.h
 *  \ingroup bgeconv
 *
 * BGE-Remix P8 Part C (P8.20): an object's hair (its Hair particle
 * systems, children included, at the viewport amount) as flat ribbons, a
 * mesh the game draws with the hair's material and sends to Remix like any
 * other. Widths come from the material's Strand settings (root, tip,
 * shape, minimum; pixels are turned into lengths at the scene camera's
 * distance); the ribbons face the scene camera as the game starts.
 */

#ifndef __BL_HAIRCARDS_H__
#define __BL_HAIRCARDS_H__

class RAS_MeshObject;
class KX_Scene;
class KX_BlenderSceneConverter;
struct Object;

/* NULL when the object has no hair to draw */
RAS_MeshObject *BL_ConvertHairCards(Object *ob, KX_Scene *kxscene, KX_BlenderSceneConverter *converter,
                                    bool libloading);

/* BGE-Remix P8 Part C (P8.22): a mesh whose material maps a texture to
 * Displace, made finer (simple subdivision, about 64,000 faces at most)
 * and moved along its normals by the texture as Blender Internal does
 * ((value - 0.5) times the Displace factor), for Modern scenes (the
 * "tessellation" switch); NULL when nothing displaces it. The game and
 * Remix draw the result. */
struct Mesh;
struct Scene;
struct Mesh *BL_DisplacedMesh(struct Object *ob, struct Mesh *mesh, struct Scene *scene,
                              KX_BlenderSceneConverter *converter);

#endif  /* __BL_HAIRCARDS_H__ */
