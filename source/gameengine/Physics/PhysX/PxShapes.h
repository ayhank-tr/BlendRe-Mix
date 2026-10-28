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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file PxShapes.h
 *  \ingroup physx
 *
 * A collision shape as Blender describes it (bounds type and size, or a mesh)
 * and how it becomes PhysX shapes at a given object scale. The mesh data comes
 * from Bullet's CcdShapeConstructionInfo, which reads Blender meshes the way
 * the game engine always has (collision polygons, quads split, polygon index
 * and UV of every triangle for rays); PhysX cooks it once per mesh.
 */

#ifndef __PXSHAPES_H__
#define __PXSHAPES_H__

#include <vector>

#include "PHY_DynamicTypes.h"
#include "foundation/PxTransform.h"
#include "foundation/PxVec3.h"
#include "PxFiltering.h"
#include "PxShape.h"

class CcdShapeConstructionInfo;
class RAS_MeshObject;

namespace physx {
class PxRigidActor;
class PxMaterial;
class PxConvexMesh;
class PxTriangleMesh;
}

struct PxShapeDesc
{
	PxShapeDesc();
	~PxShapeDesc();

	PxShapeDesc *AddRef()
	{
		m_refs++;
		return this;
	}
	void Release();

	/* a copy sharing the mesh (for replicas: compound children may change) */
	PxShapeDesc *Copy() const;

	/* PHY_SHAPE_BOX, _SPHERE, _CYLINDER, _CONE, _CAPSULE, _MESH (triangles),
	 * _POLYTOPE (convex hull of the mesh), _COMPOUND (only children);
	 * Z is the axis of cylinders, cones and capsules (Bullet's *ShapeZ) */
	PHY_ShapeType m_type;
	physx::PxVec3 m_halfExtents;  /* box; cylinder (r, r, half height) */
	float m_radius;               /* sphere, cone, capsule */
	float m_height;               /* cone (whole), capsule (cylinder part) */
	bool m_coneAxisY;             /* the radar sensor's cone (Bullet btConeShape) */
	/* Blender's collision margin. Bullet keeps it inside primitive shapes
	 * but adds it around convex hulls and triangle meshes (things rest that
	 * far from them): those shapes get it as PhysX's rest offset */
	float m_margin;
	CcdShapeConstructionInfo *m_mesh;

	struct Child {
		PxShapeDesc *desc;
		physx::PxTransform local;  /* relative to the actor, unscaled */
		physx::PxVec3 scale;       /* relative scale */
		void *owner;               /* the controller added as a compound child */
	};
	std::vector<Child> m_children;  /* compound: [0] is the object's own shape */

	/* Adds this shape (and its children) to the actor at the object scale.
	 * asDynamic: triangle meshes become convex hulls (PhysX simulates
	 * triangle meshes only on static and kinematic actors). Returns the shapes
	 * made, in order; owner of each in r_owners when given. */
	int CreateShapes(physx::PxRigidActor *actor, const physx::PxVec3 &scale, physx::PxMaterial *material,
	                 const physx::PxFilterData &filter, physx::PxShapeFlags flags, bool asDynamic,
	                 std::vector<physx::PxShape *> &r_shapes, std::vector<void *> *r_owners = NULL) const;

	/* the polygon of the mesh a triangle (as PhysX reports it) comes from, and
	 * the triangle's index in the mesh arrays; -1 when not a triangle mesh */
	static int OriginalTriangle(const physx::PxShape *shape, unsigned int faceIndex);

	/* forget the cooked meshes of this mesh (it was rebuilt) */
	static void Uncook(CcdShapeConstructionInfo *mesh);
	/* at exit */
	static void FreeCooked();

private:
	int m_refs;
	bool AddOne(physx::PxRigidActor *actor, const physx::PxTransform &local, const physx::PxVec3 &scale,
	            physx::PxMaterial *material, const physx::PxFilterData &filter, physx::PxShapeFlags flags,
	            bool asDynamic, std::vector<physx::PxShape *> &r_shapes) const;
};

#endif  /* __PXSHAPES_H__ */
