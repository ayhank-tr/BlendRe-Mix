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

/** \file RAS_GPUSkin.h
 *  \ingroup bgerast
 *
 * BGE-Remix P8 Part B (P8.13): what a deformer gives the rasterizer to deform
 * its mesh on the graphics card, once a frame: the mesh's rest positions and
 * normals, every vertex group weight, relative shape keys, the faces (for
 * normals made again from them, as BL_MeshDeformer::RecalcNormals does), and
 * for each display array the mesh vertex and the face of each of its
 * vertices. Each frame the deformer sets the bone matrices and key values
 * and counts "frame" on; the rasterizer's compute passes write the deformed
 * positions and normals into the display arrays' buffers on the card, which
 * every pass then draws (main, shadows, probes).
 */

#ifndef __RAS_GPUSKIN_H__
#define __RAS_GPUSKIN_H__

#include <map>
#include <vector>

class RAS_DisplayArray;

struct RAS_GPUSkin {
	enum { NORMALS_REST = 0, NORMALS_BONE = 1, NORMALS_FACES = 2 };

	/* made once */
	int nverts;                          /* mesh vertices */
	std::vector<float> rest;             /* per mesh vertex: position xyz 0, normal xyz 0 */
	std::vector<unsigned int> woffset;   /* per mesh vertex: its first weight (nverts + 1) */
	std::vector<unsigned int> weights;   /* (group, weight's bits) pairs */
	int ngroups;
	int nkeys;
	std::vector<float> keys;             /* per key, per mesh vertex: delta xyz 0 */
	int normals;                         /* NORMALS_* */
	std::vector<unsigned int> faces;     /* 4 mesh vertices per face (a triangle's 4th: ~0) */
	std::vector<unsigned int> vfoffset;  /* per mesh vertex: its first face (nverts + 1) */
	std::vector<unsigned int> vfaces;
	struct Array {
		std::vector<unsigned int> orig;  /* per array vertex: its mesh vertex */
		std::vector<unsigned int> face;  /* per array vertex: its face when flat (~0: smooth) */
	};
	std::map<const RAS_DisplayArray *, Array> arrays;

	/* each frame */
	unsigned int frame;                  /* counted on when the pose or the keys change */
	std::vector<float> bones;            /* per group: the deform matrix, then the pose channel's */
	std::vector<float> keyvalues;        /* per key */

	/* the rasterizer's (buffers on the card, the frame they hold) */
	void *gpu;
	void (*gpu_free)(void *gpu);

	RAS_GPUSkin() :nverts(0), ngroups(0), nkeys(0), normals(NORMALS_REST), frame(1), gpu(0), gpu_free(0) {}
	~RAS_GPUSkin() { if (gpu && gpu_free) gpu_free(gpu); }
};

#endif  /* __RAS_GPUSKIN_H__ */
