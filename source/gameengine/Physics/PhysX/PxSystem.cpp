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

/** \file PxSystem.cpp
 *  \ingroup physx
 */

#include "PxSystem.h"

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <malloc.h>
#  include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "PxPhysicsAPI.h"

using namespace physx;

PxSystem *PxSystem::s_system = NULL;
bool PxSystem::s_failed = false;

namespace {

class Allocator : public PxAllocatorCallback
{
public:
	void *allocate(size_t size, const char *, const char *, int) override
	{
#ifdef _WIN32
		return _aligned_malloc(size, 16);
#else
		void *p = NULL;
		if (posix_memalign(&p, 16, size) != 0) {
			return NULL;
		}
		return p;
#endif
	}
	void deallocate(void *ptr) override
	{
#ifdef _WIN32
		_aligned_free(ptr);
#else
		free(ptr);
#endif
	}
};

class ErrorCallback : public PxErrorCallback
{
public:
	void reportError(PxErrorCode::Enum code, const char *message, const char *file, int line) override
	{
		/* PhysX's own messages: warnings once each, errors always */
		if (code == PxErrorCode::eDEBUG_INFO || code == PxErrorCode::eNO_ERROR) {
			return;
		}
		const char *kind = (code == PxErrorCode::eDEBUG_WARNING || code == PxErrorCode::ePERF_WARNING) ? "warning" :
		                   "error";
		std::string key = std::string(file ? file : "") + ":" + std::to_string(line);
		if (kind[0] == 'w') {
			if (!m_seen.insert(key).second) {
				return;
			}
		}
		printf("PhysX %s: %s\n", kind, message ? message : "");
		fflush(stdout);
	}

private:
	std::set<std::string> m_seen;
};

Allocator g_allocator;
ErrorCallback g_errors;

}  /* namespace */

struct PxSystem::MaterialCache
{
	std::map<std::pair<float, float>, PxMaterial *> materials;
};

PxSystem::PxSystem()
    : m_foundation(NULL),
      m_physics(NULL),
      m_dispatcher(NULL),
      m_cuda(NULL),
      m_cudaTried(false),
      m_cooking(NULL),
      m_unitCylinder(NULL),
      m_unitCone(NULL),
      m_materials(new MaterialCache())
{
}

PxSystem::~PxSystem()
{
	for (auto &kv : m_materials->materials) {
		kv.second->release();
	}
	delete m_materials;
	if (m_unitCylinder) {
		m_unitCylinder->release();
	}
	if (m_unitCone) {
		m_unitCone->release();
	}
	if (m_dispatcher) {
		m_dispatcher->release();
	}
	if (m_physics) {
		PxCloseExtensions();
		m_physics->release();
	}
#if PX_SUPPORT_GPU_PHYSX
	if (m_cuda) {
		m_cuda->release();
	}
#endif
	delete m_cooking;
	if (m_foundation) {
		m_foundation->release();
	}
}

bool PxSystem::Init()
{
	m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, g_allocator, g_errors);
	if (!m_foundation) {
		return false;
	}
	PxTolerancesScale scale;  /* metres, 10 m/s typical speed: Blender's units */
	m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, scale, false, NULL);
	if (!m_physics) {
		return false;
	}
	PxInitExtensions(*m_physics, NULL);
	m_cooking = new PxCookingParams(scale);
	m_cooking->meshWeldTolerance = 0.0f;
	/* keep triangle order information: rays report the mesh polygon hit */
	m_cooking->suppressTriangleMeshRemapTable = false;
	/* GPU scenes collide triangle meshes on the GPU; cloth needs it too */
	m_cooking->buildGPUData = true;

	/* worker threads: all cores but the one running the game loop */
	unsigned int threads = 2;
	const char *env = getenv("BGE_REMIX_PHYSX_THREADS");
	if (env && atoi(env) >= 0) {
		threads = (unsigned int)atoi(env);
	}
	else {
#ifdef _WIN32
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		threads = si.dwNumberOfProcessors > 1 ? si.dwNumberOfProcessors - 1 : 1;
#else
		threads = 3;
#endif
		if (threads > 8) {
			threads = 8;
		}
	}
	m_dispatcher = PxDefaultCpuDispatcherCreate(threads);
	printf("BGE-Remix: PhysX %d.%d.%d started (%u worker threads)\n", PX_PHYSICS_VERSION_MAJOR,
	       PX_PHYSICS_VERSION_MINOR, PX_PHYSICS_VERSION_BUGFIX, threads);
	fflush(stdout);
	return m_dispatcher != NULL;
}

PxSystem *PxSystem::Get()
{
	if (s_system || s_failed) {
		return s_system;
	}
	s_system = new PxSystem();
	if (!s_system->Init()) {
		printf("BGE-Remix: PhysX could not start; the scene runs without physics\n");
		fflush(stdout);
		delete s_system;
		s_system = NULL;
		s_failed = true;
	}
	return s_system;
}

void PxSystem::Free()
{
	delete s_system;
	s_system = NULL;
	s_failed = false;
}

const PxCookingParams &PxSystem::Cooking() const
{
	return *m_cooking;
}

PxCudaContextManager *PxSystem::Cuda(std::string *r_reason)
{
#if PX_SUPPORT_GPU_PHYSX
	if (!m_cudaTried) {
		m_cudaTried = true;
		if (getenv("BGE_REMIX_PHYSX_NO_GPU")) {
			m_cudaReason = "the GPU is turned off (BGE_REMIX_PHYSX_NO_GPU)";
		}
		else {
			PxCudaContextManagerDesc desc;
			m_cuda = PxCreateCudaContextManager(*m_foundation, desc, PxGetProfilerCallback());
			if (m_cuda && !m_cuda->contextIsValid()) {
				m_cuda->release();
				m_cuda = NULL;
			}
			if (!m_cuda) {
				m_cudaReason = "no NVIDIA GPU with CUDA, or PhysXGpu_64.dll is missing";
			}
			else {
				printf("BGE-Remix: PhysX uses the GPU (%s)\n", m_cuda->getDeviceName());
				fflush(stdout);
			}
		}
	}
	if (!m_cuda && r_reason) {
		*r_reason = m_cudaReason;
	}
	return m_cuda;
#else
	if (r_reason) {
		*r_reason = "this build has no PhysX GPU support";
	}
	return NULL;
#endif
}

PxMaterial *PxSystem::Material(float friction, float restitution)
{
	/* Blender's friction and bounce, combined by multiplying (as Bullet) */
	friction = std::max(friction, 0.0f);
	restitution = std::min(std::max(restitution, 0.0f), 1.0f);
	std::pair<float, float> key(friction, restitution);
	auto it = m_materials->materials.find(key);
	if (it != m_materials->materials.end()) {
		return it->second;
	}
	PxMaterial *m = m_physics->createMaterial(friction, friction, restitution);
	m->setFrictionCombineMode(PxCombineMode::eMULTIPLY);
	m->setRestitutionCombineMode(PxCombineMode::eMULTIPLY);
	m_materials->materials[key] = m;
	return m;
}

static PxConvexMesh *cook_points(const PxCookingParams &params, PxPhysics &physics, const std::vector<PxVec3> &points)
{
	PxConvexMeshDesc desc;
	desc.points.count = (PxU32)points.size();
	desc.points.stride = sizeof(PxVec3);
	desc.points.data = points.data();
	desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
	return PxCreateConvexMesh(params, desc, physics.getPhysicsInsertionCallback());
}

PxConvexMesh *PxSystem::UnitCylinder()
{
	if (!m_unitCylinder) {
		std::vector<PxVec3> pts;
		const int n = 24;
		for (int i = 0; i < n; i++) {
			const float a = 2.0f * PxPi * i / n;
			pts.push_back(PxVec3(cosf(a), sinf(a), -1.0f));
			pts.push_back(PxVec3(cosf(a), sinf(a), 1.0f));
		}
		m_unitCylinder = cook_points(*m_cooking, *m_physics, pts);
	}
	return m_unitCylinder;
}

PxConvexMesh *PxSystem::UnitCone()
{
	if (!m_unitCone) {
		std::vector<PxVec3> pts;
		const int n = 24;
		for (int i = 0; i < n; i++) {
			const float a = 2.0f * PxPi * i / n;
			pts.push_back(PxVec3(cosf(a), sinf(a), -1.0f));
		}
		pts.push_back(PxVec3(0.0f, 0.0f, 1.0f));
		m_unitCone = cook_points(*m_cooking, *m_physics, pts);
	}
	return m_unitCone;
}

void PxSystem::WarnOnce(const char *key, const char *message)
{
	static std::set<std::string> seen;
	if (seen.insert(key).second) {
		printf("BGE-Remix PhysX: %s\n", message);
		fflush(stdout);
	}
}
