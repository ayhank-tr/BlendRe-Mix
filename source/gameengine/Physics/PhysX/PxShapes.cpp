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

/** \file PxShapes.cpp
 *  \ingroup physx
 */

#include "PxShapes.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "PxPhysicsAPI.h"
#include "PxSystem.h"

#include "CcdPhysicsController.h"  /* CcdShapeConstructionInfo: the mesh reader */

using namespace physx;

namespace {

struct Cooked {
	PxConvexMesh *convex;
	PxTriangleMesh *triangles;
	Cooked() : convex(NULL), triangles(NULL) {}
};

std::map<const CcdShapeConstructionInfo *, Cooked> g_cooked;

PxConvexMesh *cook_convex(const CcdShapeConstructionInfo *mesh)
{
	Cooked &c = g_cooked[mesh];
	if (c.convex) {
		return c.convex;
	}
	PxSystem *sys = PxSystem::Get();
	const int n = mesh->m_vertexArray.size() / 3;
	if (!sys || n < 4) {
		return NULL;
	}
	std::vector<PxVec3> pts(n);
	for (int i = 0; i < n; i++) {
		pts[i] = PxVec3(mesh->m_vertexArray[3 * i], mesh->m_vertexArray[3 * i + 1], mesh->m_vertexArray[3 * i + 2]);
	}
	PxConvexMeshDesc desc;
	desc.points.count = n;
	desc.points.stride = sizeof(PxVec3);
	desc.points.data = pts.data();
	desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
	c.convex = PxCreateConvexMesh(sys->Cooking(), desc, sys->Physics()->getPhysicsInsertionCallback());
	if (!c.convex) {
		/* flat or degenerate: cook again with the vertices moved apart a little */
		desc.flags |= PxConvexFlag::eQUANTIZE_INPUT | PxConvexFlag::ePLANE_SHIFTING;
		c.convex = PxCreateConvexMesh(sys->Cooking(), desc, sys->Physics()->getPhysicsInsertionCallback());
	}
	return c.convex;
}

PxTriangleMesh *cook_triangles(const CcdShapeConstructionInfo *mesh)
{
	Cooked &c = g_cooked[mesh];
	if (c.triangles) {
		return c.triangles;
	}
	PxSystem *sys = PxSystem::Get();
	const int nv = mesh->m_vertexArray.size() / 3;
	const int nt = (int)mesh->m_triFaceArray.size() / 3;
	if (!sys || nv < 3 || nt < 1) {
		return NULL;
	}
	std::vector<PxVec3> pts(nv);
	for (int i = 0; i < nv; i++) {
		pts[i] = PxVec3(mesh->m_vertexArray[3 * i], mesh->m_vertexArray[3 * i + 1], mesh->m_vertexArray[3 * i + 2]);
	}
	PxTriangleMeshDesc desc;
	desc.points.count = nv;
	desc.points.stride = sizeof(PxVec3);
	desc.points.data = pts.data();
	desc.triangles.count = nt;
	desc.triangles.stride = 3 * sizeof(int);
	desc.triangles.data = mesh->m_triFaceArray.data();
	c.triangles = PxCreateTriangleMesh(sys->Cooking(), desc, sys->Physics()->getPhysicsInsertionCallback());
	return c.triangles;
}

PxVec3 abs3(const PxVec3 &v)
{
	return PxVec3(fabsf(v.x), fabsf(v.y), fabsf(v.z));
}

/* PhysX's capsule lies along X, Bullet's (btCapsuleShapeZ) along Z */
const PxQuat kXtoZ(-PxHalfPi, PxVec3(0.0f, 1.0f, 0.0f));
/* our unit cone points along Z, the radar's (btConeShape) along Y */
const PxQuat kZtoY(-PxHalfPi, PxVec3(1.0f, 0.0f, 0.0f));

}  /* namespace */

PxShapeDesc::PxShapeDesc()
    : m_type(PHY_SHAPE_NONE),
      m_halfExtents(0.5f),
      m_radius(1.0f),
      m_height(1.0f),
      m_coneAxisY(false),
      m_margin(0.0f),
      m_mesh(NULL),
      m_refs(1)
{
}

PxShapeDesc::~PxShapeDesc()
{
	for (Child &c : m_children) {
		c.desc->Release();
	}
	if (m_mesh) {
		const CcdShapeConstructionInfo *mesh = m_mesh;
		if (m_mesh->Release() == 0) {
			Uncook(const_cast<CcdShapeConstructionInfo *>(mesh));
		}
	}
}

void PxShapeDesc::Release()
{
	if (--m_refs <= 0) {
		delete this;
	}
}

PxShapeDesc *PxShapeDesc::Copy() const
{
	PxShapeDesc *d = new PxShapeDesc();
	d->m_type = m_type;
	d->m_halfExtents = m_halfExtents;
	d->m_radius = m_radius;
	d->m_height = m_height;
	d->m_coneAxisY = m_coneAxisY;
	d->m_margin = m_margin;
	d->m_mesh = m_mesh;
	if (d->m_mesh) {
		d->m_mesh->AddRef();
	}
	for (const Child &c : m_children) {
		/* children added at run time (owner set) belong to the original */
		if (c.owner) {
			continue;
		}
		Child cc = c;
		cc.desc = c.desc->Copy();
		d->m_children.push_back(cc);
	}
	return d;
}

bool PxShapeDesc::AddOne(PxRigidActor *actor, const PxTransform &local, const PxVec3 &scaleIn, PxMaterial *material,
                         const PxFilterData &filter, PxShapeFlags flags, bool asDynamic,
                         std::vector<PxShape *> &r_shapes) const
{
	PxSystem *sys = PxSystem::Get();
	if (!sys) {
		return false;
	}
	if (scaleIn.x < 0.0f || scaleIn.y < 0.0f || scaleIn.z < 0.0f) {
		PxSystem::WarnOnce("negative-scale", "objects with a negative scale collide as if mirrored back");
	}
	const PxVec3 scale = abs3(scaleIn).maximum(PxVec3(1e-4f));
	PxTransform pose = local;
	PxShape *shape = NULL;

	switch (m_type) {
		case PHY_SHAPE_BOX: {
			PxVec3 h = abs3(m_halfExtents).multiply(scale).maximum(PxVec3(1e-4f));
			shape = PxRigidActorExt::createExclusiveShape(*actor, PxBoxGeometry(h), *material, flags);
			break;
		}
		case PHY_SHAPE_SPHERE: {
			/* Bullet scales a sphere by the object's X scale */
			float r = std::max(m_radius * scale.x, 1e-4f);
			shape = PxRigidActorExt::createExclusiveShape(*actor, PxSphereGeometry(r), *material, flags);
			break;
		}
		case PHY_SHAPE_CAPSULE: {
			float r = std::max(m_radius * std::max(scale.x, scale.y), 1e-4f);
			float hh = std::max(0.5f * m_height * scale.z, 0.0f);
			if (hh < 1e-4f) {
				shape = PxRigidActorExt::createExclusiveShape(*actor, PxSphereGeometry(r), *material, flags);
			}
			else {
				shape = PxRigidActorExt::createExclusiveShape(*actor, PxCapsuleGeometry(r, hh), *material, flags);
				pose = local * PxTransform(kXtoZ);
			}
			break;
		}
		case PHY_SHAPE_CYLINDER: {
			PxConvexMesh *unit = sys->UnitCylinder();
			if (!unit) {
				return false;
			}
			PxVec3 s = abs3(m_halfExtents).multiply(scale).maximum(PxVec3(1e-4f));
			shape = PxRigidActorExt::createExclusiveShape(*actor, PxConvexMeshGeometry(unit, PxMeshScale(s)),
			                                              *material, flags);
			break;
		}
		case PHY_SHAPE_CONE: {
			PxConvexMesh *unit = sys->UnitCone();
			if (!unit) {
				return false;
			}
			PxVec3 s(m_radius * scale.x, m_radius * scale.y, 0.5f * m_height * scale.z);
			if (m_coneAxisY) {
				s = PxVec3(m_radius * scale.x, m_radius * scale.z, 0.5f * m_height * scale.y);
				pose = local * PxTransform(kZtoY);
			}
			s = s.maximum(PxVec3(1e-4f));
			shape = PxRigidActorExt::createExclusiveShape(*actor, PxConvexMeshGeometry(unit, PxMeshScale(s)),
			                                              *material, flags);
			break;
		}
		case PHY_SHAPE_POLYTOPE: {
			PxConvexMesh *convex = m_mesh ? cook_convex(m_mesh) : NULL;
			if (!convex) {
				return false;
			}
			shape = PxRigidActorExt::createExclusiveShape(*actor, PxConvexMeshGeometry(convex, PxMeshScale(scale)),
			                                              *material, flags);
			break;
		}
		case PHY_SHAPE_MESH: {
			if (!m_mesh) {
				return false;
			}
			if (asDynamic) {
				PxSystem::WarnOnce("dynamic-mesh",
				                   "a moving object with Triangle Mesh bounds collides as its convex hull under PhysX");
				PxConvexMesh *convex = cook_convex(m_mesh);
				if (!convex) {
					return false;
				}
				shape = PxRigidActorExt::createExclusiveShape(*actor, PxConvexMeshGeometry(convex, PxMeshScale(scale)),
				                                              *material, flags);
			}
			else {
				PxTriangleMesh *tri = cook_triangles(m_mesh);
				if (!tri) {
					return false;
				}
				PxTriangleMeshGeometry geom(tri, PxMeshScale(scale), PxMeshGeometryFlag::eDOUBLE_SIDED);
				shape = PxRigidActorExt::createExclusiveShape(*actor, geom, *material, flags);
			}
			break;
		}
		default:
			return false;
	}
	if (!shape) {
		return false;
	}
	shape->setLocalPose(pose);
	if ((m_type == PHY_SHAPE_POLYTOPE || m_type == PHY_SHAPE_MESH) && m_margin > 0.0f) {
		shape->setContactOffset(m_margin + 0.02f);
		shape->setRestOffset(m_margin);
	}
	shape->setSimulationFilterData(filter);
	shape->setQueryFilterData(filter);
	shape->userData = const_cast<PxShapeDesc *>(this);
	r_shapes.push_back(shape);
	return true;
}

int PxShapeDesc::CreateShapes(PxRigidActor *actor, const PxVec3 &scale, PxMaterial *material, const PxFilterData &filter,
                              PxShapeFlags flags, bool asDynamic, std::vector<PxShape *> &r_shapes,
                              std::vector<void *> *r_owners) const
{
	const size_t before = r_shapes.size();
	if (m_type == PHY_SHAPE_COMPOUND) {
		for (const Child &c : m_children) {
			/* the child's position scales with the parent, its size with both */
			PxTransform local(c.local.p.multiply(scale), c.local.q);
			const size_t n = r_shapes.size();
			c.desc->AddOne(actor, local, scale.multiply(c.scale), material, filter, flags, asDynamic, r_shapes);
			if (r_owners) {
				for (size_t i = n; i < r_shapes.size(); i++) {
					r_owners->push_back(c.owner);
				}
			}
		}
	}
	else {
		AddOne(actor, PxTransform(PxIdentity), scale, material, filter, flags, asDynamic, r_shapes);
		if (r_owners) {
			for (size_t i = before; i < r_shapes.size(); i++) {
				r_owners->push_back(NULL);
			}
		}
	}
	return (int)(r_shapes.size() - before);
}

int PxShapeDesc::OriginalTriangle(const PxShape *shape, unsigned int faceIndex)
{
	if (!shape || !shape->userData) {
		return -1;
	}
	const PxShapeDesc *desc = (const PxShapeDesc *)shape->userData;
	if (desc->m_type != PHY_SHAPE_MESH || !desc->m_mesh ||
	    shape->getGeometry().getType() != PxGeometryType::eTRIANGLEMESH)
	{
		return -1;
	}
	const PxTriangleMeshGeometry &g = static_cast<const PxTriangleMeshGeometry &>(shape->getGeometry());
	const PxU32 *remap = g.triangleMesh ? g.triangleMesh->getTrianglesRemap() : NULL;
	int tri = remap ? (int)remap[faceIndex] : (int)faceIndex;
	if (tri < 0 || tri >= (int)desc->m_mesh->m_polygonIndexArray.size()) {
		return -1;
	}
	return tri;
}

void PxShapeDesc::Uncook(CcdShapeConstructionInfo *mesh)
{
	std::map<const CcdShapeConstructionInfo *, Cooked>::iterator it = g_cooked.find(mesh);
	if (it == g_cooked.end()) {
		return;
	}
	/* shapes made from them keep their own reference */
	if (it->second.convex) {
		it->second.convex->release();
	}
	if (it->second.triangles) {
		it->second.triangles->release();
	}
	g_cooked.erase(it);
}

void PxShapeDesc::FreeCooked()
{
	while (!g_cooked.empty()) {
		Uncook(const_cast<CcdShapeConstructionInfo *>(g_cooked.begin()->first));
	}
}
