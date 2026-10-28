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

/** \file PxSystem.h
 *  \ingroup physx
 *
 * BGE-Remix (P6): the PhysX objects shared by every PhysX scene of the game:
 * foundation, physics, the CPU worker threads, the CUDA context (only when a
 * GPU scene asks for it), cooking parameters and the cooked unit shapes.
 * Made on the first PhysX scene, released when the program ends.
 */

#ifndef __PXSYSTEM_H__
#define __PXSYSTEM_H__

#include <string>

namespace physx {
class PxFoundation;
class PxPhysics;
class PxDefaultCpuDispatcher;
class PxCudaContextManager;
class PxMaterial;
class PxConvexMesh;
class PxTriangleMesh;
struct PxCookingParams;
}

class PxSystem
{
public:
	/* the system, made on first use; NULL when PhysX could not start */
	static PxSystem *Get();
	/* at exit (KX_KetsjiEngine / blenderplayer end) */
	static void Free();

	physx::PxFoundation *Foundation() { return m_foundation; }
	physx::PxPhysics *Physics() { return m_physics; }
	physx::PxDefaultCpuDispatcher *Dispatcher() { return m_dispatcher; }
	const physx::PxCookingParams &Cooking() const;

	/* the CUDA context for GPU scenes: NULL (with the reason) when there is
	 * no usable NVIDIA GPU or PhysXGpu library */
	physx::PxCudaContextManager *Cuda(std::string *r_reason = NULL);

	/* a material with this friction and restitution (shared, multiply combine
	 * as Bullet does) */
	physx::PxMaterial *Material(float friction, float restitution);

	/* unit shapes as convex meshes, Z up: cylinder of radius 1 and half height
	 * 1, cone of radius 1 from z=-1 (base) to z=+1 (apex) */
	physx::PxConvexMesh *UnitCylinder();
	physx::PxConvexMesh *UnitCone();

	/* one-time console warning (key: what it is about) */
	static void WarnOnce(const char *key, const char *message);

private:
	PxSystem();
	~PxSystem();
	bool Init();

	physx::PxFoundation *m_foundation;
	physx::PxPhysics *m_physics;
	physx::PxDefaultCpuDispatcher *m_dispatcher;
	physx::PxCudaContextManager *m_cuda;
	bool m_cudaTried;
	std::string m_cudaReason;
	physx::PxCookingParams *m_cooking;
	physx::PxConvexMesh *m_unitCylinder;
	physx::PxConvexMesh *m_unitCone;
	struct MaterialCache;
	MaterialCache *m_materials;

	static PxSystem *s_system;
	static bool s_failed;
};

#endif  /* __PXSYSTEM_H__ */
