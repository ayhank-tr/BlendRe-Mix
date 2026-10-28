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

/** \file PxDeformable.cpp
 *  \ingroup physx
 *
 * Blender's Soft Body settings on PhysX's deformables:
 *   - an open mesh is cloth (PxDeformableSurface): its own triangles are
 *     simulated, vertex for vertex;
 *   - a closed mesh is a soft volume (PxDeformableVolume): PhysX fills it with
 *     tetrahedra, the mesh's vertices follow the tetrahedra they sit in.
 * Stiffness, bending, volume preservation, friction, damping, mass and
 * solver iterations come from the Soft Body panel. Joints to a rigid body pin
 * the nearest vertex to that body (Bullet's anchors); a joint to nothing pins
 * the vertex where it is (Bullet: the node's mass becomes 0).
 */

#include "PxDeformable.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

#include "PxPhysicsAPI.h"

#include "PxPhysicsController.h"
#include "PxPhysicsEnvironment.h"
#include "PxShapes.h"
#include "PxSystem.h"

#include "PHY_IMotionState.h"
#include "CcdPhysicsController.h"  /* CcdShapeConstructionInfo: the mesh */

#include "DNA_object_force.h"  /* BulletSoftBody */

#if PX_SUPPORT_GPU_PHYSX
#  include "cudamanager/PxCudaContext.h"
#  include "cudamanager/PxCudaContextManager.h"
#  include "extensions/PxCudaHelpersExt.h"
#  include "extensions/PxDeformableSurfaceExt.h"
#  include "extensions/PxDeformableVolumeExt.h"
#  include "extensions/PxTetrahedronMeshExt.h"
#endif

using namespace physx;

#if PX_SUPPORT_GPU_PHYSX

namespace {

/* the mesh as the converter read it (CcdShapeConstructionInfo: one vertex per
 * mesh vertex used by collision faces, triangles indexing them), scaled */
struct InputMesh {
	std::vector<PxVec3> verts;  /* local, scaled */
	std::vector<PxU32> tris;
};

bool input_mesh(const PxCtrlInfo &ci, InputMesh &r)
{
	const PxShapeDesc *desc = ci.shape;
	if (!desc || !desc->m_mesh) {
		return false;
	}
	const CcdShapeConstructionInfo *mesh = desc->m_mesh;
	const int nv = mesh->m_vertexArray.size() / 3;
	const int nt = (int)mesh->m_triFaceArray.size() / 3;
	if (nv < 3 || nt < 1) {
		return false;
	}
	r.verts.resize(nv);
	for (int i = 0; i < nv; i++) {
		r.verts[i] = PxVec3(mesh->m_vertexArray[3 * i] * ci.scaling.x, mesh->m_vertexArray[3 * i + 1] * ci.scaling.y,
		                    mesh->m_vertexArray[3 * i + 2] * ci.scaling.z);
	}
	r.tris.clear();
	for (int t = 0; t < nt; t++) {
		const int a = mesh->m_triFaceArray[3 * t], b = mesh->m_triFaceArray[3 * t + 1],
		          c = mesh->m_triFaceArray[3 * t + 2];
		if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv || a == b || b == c || a == c) {
			continue;
		}
		r.tris.push_back((PxU32)a);
		r.tris.push_back((PxU32)b);
		r.tris.push_back((PxU32)c);
	}
	return !r.tris.empty();
}

/* closed: every edge is shared by exactly two triangles */
bool is_closed(const std::vector<PxU32> &tris)
{
	std::map<std::pair<PxU32, PxU32>, int> edges;
	for (size_t t = 0; t + 2 < tris.size(); t += 3) {
		for (int k = 0; k < 3; k++) {
			PxU32 a = tris[t + k], b = tris[t + (k + 1) % 3];
			if (a > b) {
				std::swap(a, b);
			}
			edges[std::make_pair(a, b)]++;
		}
	}
	for (const auto &e : edges) {
		if (e.second != 2) {
			return false;
		}
	}
	return !edges.empty();
}

/* for each point, the index of the same (or nearest) point in 'to' */
std::vector<PxU32> match_points(const std::vector<PxVec3> &points, const PxVec3 *to, PxU32 nto)
{
	struct Key {
		size_t operator()(const PxVec3 &v) const
		{
			PxU32 b[3];
			memcpy(b, &v.x, sizeof(b));
			return (size_t)b[0] * 73856093u ^ (size_t)b[1] * 19349663u ^ (size_t)b[2] * 83492791u;
		}
	};
	struct Eq {
		bool operator()(const PxVec3 &a, const PxVec3 &b) const { return a.x == b.x && a.y == b.y && a.z == b.z; }
	};
	std::unordered_map<PxVec3, PxU32, Key, Eq> exact;
	for (PxU32 i = 0; i < nto; i++) {
		exact.insert(std::make_pair(to[i], i));
	}
	std::vector<PxU32> r(points.size(), 0);
	for (size_t i = 0; i < points.size(); i++) {
		auto it = exact.find(points[i]);
		if (it != exact.end()) {
			r[i] = it->second;
			continue;
		}
		float best = PX_MAX_F32;
		for (PxU32 j = 0; j < nto; j++) {
			const float d = (to[j] - points[i]).magnitudeSquared();
			if (d < best) {
				best = d;
				r[i] = j;
			}
		}
	}
	return r;
}

/* Blender's 0..1 stiffness as Young's modulus (Pa), on a log scale */
float youngs_of(float stiffness, float low, float high)
{
	stiffness = std::min(std::max(stiffness, 0.0f), 1.0f);
	return low * powf(high / low, stiffness);
}

class GpuDeformable : public PxDeformable
{
public:
	GpuDeformable(PxPhysicsController *ctrl, PxPhysicsEnvironment *env, PxCudaContextManager *cuda)
	    : m_ctrl(ctrl),
	      m_env(env),
	      m_cuda(cuda),
	      m_volumeMode(false),
	      m_surface(NULL),
	      m_volume(NULL),
	      m_scene(NULL),
	      m_nDevice(0),
	      m_nSim(0),
	      m_readback(NULL),
	      m_posPinned(NULL),
	      m_velPinned(NULL),
	      m_restPinned(NULL),
	      m_collPinned(NULL),
	      m_mass(1.0f)
	{
	}

	~GpuDeformable() override
	{
		RemoveFromScene(m_scene);
		if (m_surface) {
			m_surface->release();
		}
		if (m_volume) {
			m_volume->release();
		}
		PxVec4 **bufs[5] = {&m_readback, &m_posPinned, &m_velPinned, &m_restPinned, &m_collPinned};
		for (PxVec4 **b : bufs) {
			if (*b) {
				PX_EXT_PINNED_MEMORY_FREE(*m_cuda, *b);
				*b = NULL;
			}
		}
	}

	bool Init(const PxTransform &pose)
	{
		const PxCtrlInfo &ci = m_ctrl->GetInfo();
		InputMesh in;
		if (!input_mesh(ci, in)) {
			return false;
		}
		m_tris = in.tris;
		m_mass = ci.mass > 0.0f ? ci.mass : 1.0f;
		m_volumeMode = is_closed(in.tris);
		const bool ok = m_volumeMode ? InitVolume(in, pose) : InitSurface(in, pose);
		if (!ok) {
			return false;
		}
		Body()->userData = m_ctrl;
		/* the solver iterations of the Soft Body panel */
		const BulletSoftBody *bsb = ci.bsoft;
		if (bsb) {
			Body()->setSolverIterationCounts((PxU32)std::max(bsb->piterations, 1) * (m_volumeMode ? 10u : 1u),
			                                 (PxU32)std::max(bsb->viterations, 1));
			Body()->setLinearDamping(std::max(bsb->kDP, 0.0f));
		}
		Body()->setMaxLinearVelocity(100.0f);
		m_readback = PX_EXT_PINNED_MEMORY_ALLOC(PxVec4, *m_cuda, m_nDevice);
		/* the starting shape, until the first frame comes back */
		m_world.resize(in.verts.size());
		for (size_t i = 0; i < in.verts.size(); i++) {
			m_world[i] = pose.transform(in.verts[i]);
		}
		ComputeNormals();
		m_map.resize(m_world.size());
		for (size_t i = 0; i < m_map.size(); i++) {
			m_map[i] = (int)i;
		}
		m_startPose = pose;
		return true;
	}

	/* PxDeformable */
	void AddToScene(PxScene *scene) override
	{
		if (m_scene || !scene) {
			return;
		}
		m_scene = scene;
		scene->addActor(*Body());
		/* the starting state onto the GPU */
		if (m_volumeMode) {
			PxDeformableVolumeExt::copyToDevice(*m_volume, PxDeformableVolumeDataFlag::eALL, m_posPinned, m_velPinned,
			                                     m_collPinned, m_restPinned);
		}
		else {
			PxDeformableSurfaceExt::copyToDevice(*m_surface, PxDeformableSurfaceDataFlag::eALL, m_nDevice, m_posPinned,
			                                      m_velPinned, m_restPinned);
		}
		MakeAttachments();
	}

	void RemoveFromScene(PxScene *scene) override
	{
		if (!m_scene || (scene && scene != m_scene)) {
			return;
		}
		for (Attachment &a : m_attachments) {
			if (a.attachment) {
				a.attachment->release();
				a.attachment = NULL;
			}
		}
		m_scene->removeActor(*Body());
		m_scene = NULL;
	}

	void SynchronizeMotionState(PHY_IMotionState *motionState) override
	{
		if (!m_scene) {
			return;
		}
		if (m_pendingAttachments) {
			MakeAttachments();
		}
		{
			PxVec4 *dev = m_volumeMode ? m_volume->getPositionInvMassBufferD() : m_surface->getPositionInvMassBufferD();
			PxScopedCudaLock lock(*m_cuda);
			m_cuda->getCudaContext()->memcpyDtoH(m_readback, reinterpret_cast<CUdeviceptr>(dev),
			                                     m_nDevice * sizeof(PxVec4));
		}
		if (m_volumeMode) {
			for (size_t i = 0; i < m_world.size(); i++) {
				const PxU32 *t = &m_tetVerts[4 * m_tet[i]];
				const PxVec4 &b = m_bary[i];
				m_world[i] = m_readback[t[0]].getXYZ() * b.x + m_readback[t[1]].getXYZ() * b.y +
				             m_readback[t[2]].getXYZ() * b.z + m_readback[t[3]].getXYZ() * b.w;
			}
		}
		else {
			for (size_t i = 0; i < m_world.size(); i++) {
				m_world[i] = m_readback[m_simIndex[i]].getXYZ();
			}
		}
		ComputeNormals();
		if (motionState) {
			/* as Bullet: the object is at the middle of its bounds */
			PxBounds3 b = PxBounds3::empty();
			for (const PxVec3 &p : m_world) {
				b.include(p);
			}
			const PxVec3 c = b.getCenter();
			motionState->SetWorldPosition(c.x, c.y, c.z);
			motionState->CalculateWorldTransformations();
		}
	}

	const std::vector<PxVec3> &WorldVertices() const override { return m_world; }
	const std::vector<PxVec3> &WorldNormals() const override { return m_normals; }
	const std::vector<int> &VertexMap() const override { return m_map; }
	float Mass() const override { return m_mass; }

	void AddForce(const PxVec3 &force) override
	{
		/* as Bullet: the force on every node, for one step */
		const float dt = m_env ? m_env->Step() : 1.0f / 60.0f;
		ChangeVelocities(force * (dt * (float)m_nSim / m_mass), false);
	}

	void SetVelocity(const PxVec3 &velocity) override { ChangeVelocities(velocity, true); }

	void Attach(const PxVec3 &point, PxPhysicsController *other, bool pointIsLocal) override
	{
		const PxVec3 world = pointIsLocal ? m_startPose.transform(point) : point;
		const PxU32 v = NearestSimVertex(world);
		if (!other || !other->GetActor()) {
			/* pinned where it is (Bullet: its mass becomes 0) */
			m_posPinned[v].w = 0.0f;
			if (m_scene) {
				Upload(true, false);
			}
			return;
		}
		Attachment a;
		a.vertex = v;
		a.other = other;
		a.attachment = NULL;
		m_attachments.push_back(a);
		m_pendingAttachments = true;
		if (m_scene) {
			MakeAttachments();
		}
	}

private:
	PxDeformableBody *Body() const
	{
		return m_volumeMode ? static_cast<PxDeformableBody *>(m_volume) : static_cast<PxDeformableBody *>(m_surface);
	}

	bool InitSurface(const InputMesh &in, const PxTransform &pose)
	{
		PxSystem *sys = PxSystem::Get();
		PxPhysics &physics = *sys->Physics();
		const PxCtrlInfo &ci = m_ctrl->GetInfo();
		const BulletSoftBody *bsb = ci.bsoft;

		PxCookingParams params = sys->Cooking();
		params.buildGPUData = true;
		params.meshWeldTolerance = 0.0f;
		params.meshPreprocessParams = PxMeshPreprocessingFlags();
		params.buildTriangleAdjacencies = false;
		params.midphaseDesc = PxMeshMidPhase::eBVH34;
		PxTriangleMeshDesc desc;
		desc.points.count = (PxU32)in.verts.size();
		desc.points.stride = sizeof(PxVec3);
		desc.points.data = in.verts.data();
		desc.triangles.count = (PxU32)(in.tris.size() / 3);
		desc.triangles.stride = 3 * sizeof(PxU32);
		desc.triangles.data = in.tris.data();
		PxTriangleMesh *mesh = PxCreateTriangleMesh(params, desc, physics.getPhysicsInsertionCallback());
		if (!mesh) {
			return false;
		}
		/* cloth: thin, its stretch and bending from the panel */
		const float thickness = std::max(ci.margin, 0.005f);
		const float stiff = bsb ? bsb->linStiff : 0.5f;
		const float bend = bsb ? bsb->angStiff : 1.0f;
		const bool bending = bsb && (bsb->flag & OB_BSB_BENDING_CONSTRAINTS);
		PxDeformableSurfaceMaterial *material = physics.createDeformableSurfaceMaterial(
		        youngs_of(stiff, 1.0e6f, 1.0e10f), 0.3f, bsb ? std::max(bsb->kDF, 0.0f) : 0.2f, thickness,
		        (bending ? 1.0e-3f : 1.0e-5f) * std::max(bend, 0.0f), 0.0f, 0.0f);
		m_surface = physics.createDeformableSurface(*m_cuda);
		if (!m_surface || !material) {
			mesh->release();
			return false;
		}
		PxShape *shape = physics.createShape(PxTriangleMeshGeometry(mesh), &material, 1, true,
		                                     PxShapeFlag::eVISUALIZATION | PxShapeFlag::eSCENE_QUERY_SHAPE |
		                                             PxShapeFlag::eSIMULATION_SHAPE);
		material->release();
		if (!shape) {
			mesh->release();
			return false;
		}
		shape->setSimulationFilterData(m_ctrl->FilterData());
		shape->setContactOffset(2.0f * thickness);
		shape->setRestOffset(thickness);
		m_surface->attachShape(*shape);
		shape->release();
		m_surface->setSelfCollisionFilterDistance(2.5f * thickness);
		m_surface->setDeformableBodyFlag(PxDeformableBodyFlag::eDISABLE_SELF_COLLISION,
		                                 !(bsb && (bsb->collisionflags & OB_BSB_COL_VF_SS)));

		/* our vertices in PhysX's order */
		m_nDevice = m_nSim = mesh->getNbVertices();
		m_simIndex = match_points(in.verts, mesh->getVertices(), m_nDevice);
		PxDeformableSurfaceExt::allocateAndInitializeHostMirror(*m_surface, mesh->getVertices(), NULL, NULL, m_mass,
		                                                        pose, m_cuda, m_posPinned, m_velPinned,
		                                                        m_restPinned);
		mesh->release();  /* the shape keeps it */
		return true;
	}

	bool InitVolume(const InputMesh &in, const PxTransform &pose)
	{
		PxSystem *sys = PxSystem::Get();
		PxPhysics &physics = *sys->Physics();
		const PxCtrlInfo &ci = m_ctrl->GetInfo();
		const BulletSoftBody *bsb = ci.bsoft;

		PxCookingParams params = sys->Cooking();
		params.buildGPUData = true;
		params.meshWeldTolerance = 0.001f;
		params.meshPreprocessParams = PxMeshPreprocessingFlags(PxMeshPreprocessingFlag::eWELD_VERTICES);
		PxSimpleTriangleMesh surface;
		surface.points.count = (PxU32)in.verts.size();
		surface.points.stride = sizeof(PxVec3);
		surface.points.data = in.verts.data();
		surface.triangles.count = (PxU32)(in.tris.size() / 3);
		surface.triangles.stride = 3 * sizeof(PxU32);
		surface.triangles.data = in.tris.data();
		PxDeformableVolumeMesh *vmesh =
		        PxDeformableVolumeExt::createDeformableVolumeMesh(params, surface, 10, physics.getPhysicsInsertionCallback());
		if (!vmesh) {
			return false;
		}
		const float stiff = bsb ? bsb->linStiff : 0.5f;
		/* Volume preservation: nearly incompressible at 1 */
		const float poisson = 0.25f + 0.24f * std::min(std::max(bsb ? bsb->volume : 0.5f, 0.0f), 1.0f);
		PxDeformableVolumeMaterial *material = physics.createDeformableVolumeMaterial(
		        youngs_of(stiff, 1.0e4f, 1.0e7f), poisson, bsb ? std::max(bsb->kDF, 0.0f) : 0.2f, 0.005f);
		m_volume = physics.createDeformableVolume(*m_cuda);
		if (!m_volume || !material) {
			vmesh->release();
			return false;
		}
		PxShape *shape = physics.createShape(PxTetrahedronMeshGeometry(vmesh->getCollisionMesh()), &material, 1, true,
		                                     PxShapeFlag::eVISUALIZATION | PxShapeFlag::eSCENE_QUERY_SHAPE |
		                                             PxShapeFlag::eSIMULATION_SHAPE);
		material->release();
		if (!shape) {
			vmesh->release();
			return false;
		}
		shape->setSimulationFilterData(m_ctrl->FilterData());
		m_volume->attachShape(*shape);
		shape->release();
		m_volume->attachSimulationMesh(*vmesh->getSimulationMesh(), *vmesh->getDeformableVolumeAuxData());
		m_volume->setDeformableBodyFlag(PxDeformableBodyFlag::eDISABLE_SELF_COLLISION, true);
		vmesh->release();  /* the volume keeps its meshes */

		PxDeformableVolumeExt::allocateAndInitializeHostMirror(*m_volume, m_cuda, m_posPinned, m_velPinned,
		                                                       m_collPinned, m_restPinned);
		PxDeformableVolumeExt::transform(*m_volume, pose, 1.0f, m_posPinned, m_velPinned, m_collPinned, m_restPinned);
		PxDeformableVolumeExt::setMass(*m_volume, m_mass, 50.0f, m_posPinned);

		/* our vertices inside the collision tetrahedra (world, at the start) */
		const PxTetrahedronMesh *coll = m_volume->getCollisionMesh();
		m_nDevice = coll->getNbVertices();
		m_nSim = m_volume->getSimulationMesh()->getNbVertices();
		PxArray<PxVec3> tetVerts;
		for (PxU32 i = 0; i < m_nDevice; i++) {
			tetVerts.pushBack(m_collPinned[i].getXYZ());
		}
		const PxU32 ntet = coll->getNbTetrahedrons();
		const bool sixteen = coll->getTetrahedronMeshFlags() & PxTetrahedronMeshFlag::e16_BIT_INDICES;
		PxArray<PxU32> tetIdx;
		for (PxU32 i = 0; i < 4 * ntet; i++) {
			tetIdx.pushBack(sixteen ? (PxU32)((const PxU16 *)coll->getTetrahedrons())[i] :
			                          ((const PxU32 *)coll->getTetrahedrons())[i]);
		}
		PxArray<PxVec3> points;
		for (const PxVec3 &v : in.verts) {
			points.pushBack(pose.transform(v));
		}
		PxArray<PxVec4> bary;
		PxArray<PxU32> links;
		PxTetrahedronMeshExt::createPointsToTetrahedronMap(tetVerts, tetIdx, points, bary, links);
		if (bary.size() != points.size()) {
			return false;
		}
		m_tetVerts.assign(tetIdx.begin(), tetIdx.end());
		m_tet.assign(links.begin(), links.end());
		m_bary.assign(bary.begin(), bary.end());
		return true;
	}

	void ComputeNormals()
	{
		m_normals.assign(m_world.size(), PxVec3(0.0f));
		for (size_t t = 0; t + 2 < m_tris.size(); t += 3) {
			const PxU32 a = m_tris[t], b = m_tris[t + 1], c = m_tris[t + 2];
			const PxVec3 n = (m_world[b] - m_world[a]).cross(m_world[c] - m_world[a]);
			m_normals[a] += n;
			m_normals[b] += n;
			m_normals[c] += n;
		}
		for (PxVec3 &n : m_normals) {
			n.normalizeSafe();
		}
	}

	/* the simulated vertex nearest to a world point (surface: the cloth's
	 * vertices; volume: the simulation mesh), at the start */
	PxU32 NearestSimVertex(const PxVec3 &world) const
	{
		PxU32 best = 0;
		float bestd = PX_MAX_F32;
		for (PxU32 i = 0; i < m_nSim; i++) {
			const float d = (m_posPinned[i].getXYZ() - world).magnitudeSquared();
			if (d < bestd) {
				bestd = d;
				best = i;
			}
		}
		return best;
	}

	void Upload(bool positions, bool velocities)
	{
		if (m_volumeMode) {
			PxDeformableVolumeDataFlags f;
			if (positions) {
				f |= PxDeformableVolumeDataFlag::eSIM_POSITION_INVMASS;
			}
			if (velocities) {
				f |= PxDeformableVolumeDataFlag::eSIM_VELOCITY;
			}
			PxDeformableVolumeExt::copyToDevice(*m_volume, f, m_posPinned, m_velPinned, m_collPinned, m_restPinned);
		}
		else {
			PxDeformableSurfaceDataFlags f;
			if (positions) {
				f |= PxDeformableSurfaceDataFlag::ePOSITION_INVMASS;
			}
			if (velocities) {
				f |= PxDeformableSurfaceDataFlag::eVELOCITY;
			}
			PxDeformableSurfaceExt::copyToDevice(*m_surface, f, m_nDevice, m_posPinned, m_velPinned, m_restPinned);
		}
	}

	void ChangeVelocities(const PxVec3 &v, bool set)
	{
		if (!m_scene) {
			return;
		}
		PxVec4 *dev = m_volumeMode ? m_volume->getSimVelocityBufferD() : m_surface->getVelocityBufferD();
		{
			PxScopedCudaLock lock(*m_cuda);
			m_cuda->getCudaContext()->memcpyDtoH(m_velPinned, reinterpret_cast<CUdeviceptr>(dev),
			                                     m_nSim * sizeof(PxVec4));
		}
		for (PxU32 i = 0; i < m_nSim; i++) {
			const PxVec3 nv = set ? v : m_velPinned[i].getXYZ() + v;
			m_velPinned[i] = PxVec4(nv, m_velPinned[i].w);
		}
		Upload(false, true);
		Body()->setWakeCounter(0.4f);
	}

	void MakeAttachments()
	{
		m_pendingAttachments = false;
		if (!m_scene) {
			m_pendingAttachments = !m_attachments.empty();
			return;
		}
		PxPhysics &physics = *PxSystem::Get()->Physics();
		for (Attachment &a : m_attachments) {
			if (a.attachment) {
				continue;
			}
			PxRigidActor *actor = a.other->GetActor();
			if (!actor || !actor->getScene()) {
				m_pendingAttachments = true;
				continue;
			}
			/* where the vertex is now, in the body's frame */
			const PxVec3 world = m_posPinned[a.vertex].getXYZ();
			const PxVec4 local(actor->getGlobalPose().transformInv(world), 0.0f);
			PxDeformableAttachmentData data;
			data.actor[0] = Body();
			data.type[0] = PxDeformableAttachmentTargetType::eVERTEX;
			data.indices[0].data = &a.vertex;
			data.indices[0].count = 1;
			data.actor[1] = actor;
			data.type[1] = PxDeformableAttachmentTargetType::eRIGID;
			data.coords[1].data = &local;
			data.coords[1].count = 1;
			a.attachment = physics.createDeformableAttachment(data);
		}
	}

	struct Attachment {
		PxU32 vertex;
		PxPhysicsController *other;
		PxDeformableAttachment *attachment;
	};

	PxPhysicsController *m_ctrl;
	PxPhysicsEnvironment *m_env;
	PxCudaContextManager *m_cuda;
	bool m_volumeMode;
	PxDeformableSurface *m_surface;
	PxDeformableVolume *m_volume;
	PxScene *m_scene;
	PxTransform m_startPose;

	std::vector<PxU32> m_tris;
	std::vector<PxVec3> m_world, m_normals;
	std::vector<int> m_map;
	/* cloth: our vertex -> PhysX's vertex */
	std::vector<PxU32> m_simIndex;
	/* volume: our vertex -> collision tetrahedron and its weights */
	std::vector<PxU32> m_tetVerts, m_tet;
	std::vector<PxVec4> m_bary;

	PxU32 m_nDevice;  /* vertices read back (cloth, or the collision mesh) */
	PxU32 m_nSim;     /* simulated vertices (velocities, pins) */
	PxVec4 *m_readback;
	/* the starting state (pinned host memory), uploaded when entering the scene */
	PxVec4 *m_posPinned, *m_velPinned, *m_restPinned, *m_collPinned;
	float m_mass;

	std::vector<Attachment> m_attachments;
	bool m_pendingAttachments = false;
};

}  /* namespace */

PxDeformable *PxDeformable::Create(PxPhysicsController *ctrl, PxPhysicsEnvironment *env, const PxTransform &pose)
{
	PxSystem *sys = PxSystem::Get();
	PxCudaContextManager *cuda = sys ? sys->Cuda() : NULL;
	if (!cuda || !env || !env->UsesGpu()) {
		return NULL;
	}
	GpuDeformable *d = new GpuDeformable(ctrl, env, cuda);
	if (!d->Init(pose)) {
		PxSystem::WarnOnce("soft-mesh", "a soft body's mesh could not be made into GPU cloth or a soft volume; it stays still");
		delete d;
		return NULL;
	}
	return d;
}

#else  /* no GPU PhysX in this build */

PxDeformable *PxDeformable::Create(PxPhysicsController *, PxPhysicsEnvironment *, const physx::PxTransform &)
{
	return NULL;
}

#endif
