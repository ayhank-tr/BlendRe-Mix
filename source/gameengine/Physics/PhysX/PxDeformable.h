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

/** \file PxDeformable.h
 *  \ingroup physx
 *
 * BGE-Remix (P6): Blender's Soft Body physics type on PhysX's GPU deformables:
 * an open mesh (a curtain, a flag) becomes cloth (a deformable surface), a
 * closed mesh (a rubber duck) a deformable volume. Joints to rigid bodies
 * become attachments. GPU only: without a GPU scene the object stays still.
 */

#ifndef __PXDEFORMABLE_H__
#define __PXDEFORMABLE_H__

#include <vector>

#include "foundation/PxTransform.h"
#include "foundation/PxVec3.h"

class PxPhysicsController;
class PxPhysicsEnvironment;
class PHY_IMotionState;

namespace physx {
class PxScene;
}

class PxDeformable
{
public:
	/* NULL when the mesh cannot be simulated (the object then stays still) */
	static PxDeformable *Create(PxPhysicsController *ctrl, PxPhysicsEnvironment *env, const physx::PxTransform &pose);
	virtual ~PxDeformable() {}

	virtual void AddToScene(physx::PxScene *scene) = 0;
	virtual void RemoveFromScene(physx::PxScene *scene) = 0;

	/* after each frame: the GPU's vertices back on the CPU, the object's
	 * place (the centre of its vertices) */
	virtual void SynchronizeMotionState(PHY_IMotionState *motionState) = 0;

	/* the simulated vertices in world space, in the order of the mesh
	 * vertices the converter gave (KX_SoftBodyDeformer reads them) */
	virtual const std::vector<physx::PxVec3> &WorldVertices() const = 0;
	/* their normals (from the simulated triangles) */
	virtual const std::vector<physx::PxVec3> &WorldNormals() const = 0;
	/* for each mesh vertex (RAS order), its simulated vertex */
	virtual const std::vector<int> &VertexMap() const = 0;

	virtual float Mass() const = 0;
	virtual void AddForce(const physx::PxVec3 &force) = 0;
	virtual void SetVelocity(const physx::PxVec3 &velocity) = 0;

	/* pin the nearest vertex to a rigid body (or to the world when other is
	 * NULL); pointIsLocal: the point is in the soft body's starting frame */
	virtual void Attach(const physx::PxVec3 &point, PxPhysicsController *other, bool pointIsLocal) = 0;
};

#endif  /* __PXDEFORMABLE_H__ */
