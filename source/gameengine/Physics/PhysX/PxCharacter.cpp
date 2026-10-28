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

/** \file PxCharacter.cpp
 *  \ingroup physx
 */

#include "PxCharacter.h"

#include <algorithm>
#include <cmath>

#include "PxPhysicsAPI.h"

#include "PxPhysicsController.h"
#include "PxPhysicsEnvironment.h"
#include "PxShapes.h"
#include "PxSystem.h"

#include "CcdPhysicsController.h"  /* CcdShapeConstructionInfo */

using namespace physx;

namespace {

/* the character's capsule from its collision bounds */
void capsule_of(const PxShapeDesc *desc, const PxVec3 &scale, float &r_radius, float &r_height)
{
	const PxVec3 s(fabsf(scale.x), fabsf(scale.y), fabsf(scale.z));
	float radius = 0.5f, half = 0.5f;
	switch (desc ? desc->m_type : PHY_SHAPE_NONE) {
		case PHY_SHAPE_SPHERE:
			radius = desc->m_radius * s.x;
			half = radius;
			break;
		case PHY_SHAPE_CAPSULE:
			radius = desc->m_radius * std::max(s.x, s.y);
			half = 0.5f * desc->m_height * s.z + radius;
			break;
		case PHY_SHAPE_BOX:
		case PHY_SHAPE_CYLINDER:
			radius = std::max(desc->m_halfExtents.x * s.x, desc->m_halfExtents.y * s.y);
			half = desc->m_halfExtents.z * s.z;
			break;
		case PHY_SHAPE_CONE:
			radius = desc->m_radius * std::max(s.x, s.y);
			half = 0.5f * desc->m_height * s.z;
			break;
		case PHY_SHAPE_MESH:
		case PHY_SHAPE_POLYTOPE:
			if (desc->m_mesh && desc->m_mesh->m_vertexArray.size() >= 3) {
				PxBounds3 b = PxBounds3::empty();
				const int n = desc->m_mesh->m_vertexArray.size() / 3;
				for (int i = 0; i < n; i++) {
					b.include(PxVec3(desc->m_mesh->m_vertexArray[3 * i] * s.x,
					                 desc->m_mesh->m_vertexArray[3 * i + 1] * s.y,
					                 desc->m_mesh->m_vertexArray[3 * i + 2] * s.z));
				}
				const PxVec3 e = b.getExtents();
				radius = std::max(e.x, e.y);
				half = e.z;
			}
			break;
		case PHY_SHAPE_COMPOUND:
			if (!desc->m_children.empty()) {
				capsule_of(desc->m_children[0].desc, scale.multiply(desc->m_children[0].scale), r_radius, r_height);
				return;
			}
			break;
		default:
			break;
	}
	r_radius = std::max(radius, 0.01f);
	/* PhysX: the height between the two sphere centres */
	r_height = std::max(2.0f * (half - r_radius), 0.01f);
}

class NoSensors : public PxQueryFilterCallback
{
public:
	const PxRigidActor *self;
	PxQueryHitType::Enum preFilter(const PxFilterData &, const PxShape *shape, const PxRigidActor *actor,
	                               PxHitFlags &) override
	{
		if (actor == self || shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE)) {
			return PxQueryHitType::eNONE;
		}
		return PxQueryHitType::eBLOCK;
	}
	PxQueryHitType::Enum postFilter(const PxFilterData &, const PxQueryHit &, const PxShape *,
	                                const PxRigidActor *) override
	{
		return PxQueryHitType::eBLOCK;
	}
};

}  /* namespace */

PxCharacter::PxCharacter(PxPhysicsController *ctrl, PxPhysicsEnvironment *env, const PxTransform &pose)
    : m_ctrl(ctrl),
      m_env(env),
      m_controller(NULL),
      m_walk(0.0f),
      m_velocity(0.0f),
      m_verticalVelocity(0.0f),
      m_gravity(9.8f * 3.0f),
      m_jumpSpeed(10.0f),
      m_fallSpeed(55.0f),
      m_maxJumps(1),
      m_jumpCount(0),
      m_onGround(false),
      m_orientation(pose.q),
      m_position(pose.p),
      m_radius(0.5f),
      m_height(1.0f),
      m_stepOffset(0.0f)
{
	const PxCtrlInfo &ci = ctrl->GetInfo();
	if (ci.jumpSpeed > 0.0f) {
		m_jumpSpeed = ci.jumpSpeed;
	}
	if (ci.fallSpeed > 0.0f) {
		m_fallSpeed = ci.fallSpeed;
	}
	m_maxJumps = ci.maxJumps ? ci.maxJumps : 1;
	capsule_of(ci.shape, ci.scaling, m_radius, m_height);
	m_stepOffset = std::min(std::max(ci.stepHeight, 0.0f), m_height + 2.0f * m_radius - 0.01f);
}

PxCharacter::~PxCharacter()
{
	RemoveFromScene();
}

void PxCharacter::AddToScene()
{
	PxSystem *sys = PxSystem::Get();
	PxControllerManager *mgr = m_env ? m_env->ControllerManager() : NULL;
	if (m_controller || !sys || !mgr) {
		return;
	}
	const PxCtrlInfo &ci = m_ctrl->GetInfo();
	const PxTransform pose = m_ctrl->MotionStateTransform();
	m_position = pose.p;
	m_orientation = pose.q;
	PxCapsuleControllerDesc desc;
	desc.radius = m_radius;
	desc.height = m_height;
	desc.position = PxExtendedVec3(m_position.x, m_position.y, m_position.z);
	desc.upDirection = PxVec3(0.0f, 0.0f, 1.0f);
	desc.slopeLimit = cosf(45.0f * PxPi / 180.0f);
	desc.stepOffset = m_stepOffset;
	desc.contactOffset = 0.02f;
	desc.material = sys->Material(ci.friction, ci.restitution);
	desc.climbingMode = PxCapsuleClimbingMode::eCONSTRAINED;
	desc.userData = m_ctrl;
	if (!desc.isValid()) {
		desc.stepOffset = 0.0f;
	}
	m_controller = mgr->createController(desc);
	if (m_controller && m_controller->getActor()) {
		m_controller->getActor()->userData = m_ctrl;
	}
}

void PxCharacter::RemoveFromScene()
{
	if (m_controller) {
		m_position = Pose().p;
		m_controller->release();
		m_controller = NULL;
	}
}

PxRigidDynamic *PxCharacter::Actor()
{
	return m_controller ? m_controller->getActor() : NULL;
}

PxTransform PxCharacter::Pose() const
{
	if (!m_controller) {
		return PxTransform(m_position, m_orientation);
	}
	const PxExtendedVec3 &p = m_controller->getPosition();
	return PxTransform(PxVec3((float)p.x, (float)p.y, (float)p.z), m_orientation);
}

void PxCharacter::SetPosition(const PxVec3 &p)
{
	m_position = p;
	if (m_controller) {
		m_controller->setPosition(PxExtendedVec3(p.x, p.y, p.z));
	}
}

void PxCharacter::Jump()
{
	/* Bullet: on the ground, or jumps left */
	if (m_onGround || m_jumpCount < m_maxJumps) {
		if (m_onGround) {
			m_jumpCount = 0;
		}
		m_verticalVelocity = m_jumpSpeed;
		m_jumpCount++;
		m_onGround = false;
	}
}

void PxCharacter::SetWalkDirection(const MT_Vector3 &dir)
{
	m_walk = PxVec3((float)dir.x(), (float)dir.y(), (float)dir.z());
}

MT_Vector3 PxCharacter::GetWalkDirection()
{
	return MT_Vector3(m_walk.x, m_walk.y, m_walk.z);
}

void PxCharacter::Step(float dt)
{
	if (!m_controller || dt <= 0.0f) {
		return;
	}
	/* the object's own turn (the game turns characters by hand) */
	if (m_ctrl) {
		m_orientation = m_ctrl->MotionStateTransform().q;
	}
	m_verticalVelocity -= m_gravity * dt;
	if (m_verticalVelocity < -m_fallSpeed) {
		m_verticalVelocity = -m_fallSpeed;
	}
	const PxVec3 before = Pose().p;
	/* as Bullet: the walk direction is a step per tick */
	const PxVec3 disp = m_walk + PxVec3(0.0f, 0.0f, m_verticalVelocity * dt);

	NoSensors filter;
	filter.self = m_controller->getActor();
	PxFilterData fd = m_ctrl ? m_ctrl->FilterData() : PxFilterData();
	PxControllerFilters filters(&fd, &filter);
	filters.mFilterFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER;
	const PxControllerCollisionFlags hit = m_controller->move(disp, 0.0001f, dt, filters);

	if (hit & PxControllerCollisionFlag::eCOLLISION_DOWN) {
		if (m_verticalVelocity <= 0.0f) {
			m_verticalVelocity = 0.0f;
			m_onGround = true;
			m_jumpCount = 0;
		}
	}
	else {
		m_onGround = false;
	}
	if ((hit & PxControllerCollisionFlag::eCOLLISION_UP) && m_verticalVelocity > 0.0f) {
		m_verticalVelocity = 0.0f;
	}
	m_velocity = (Pose().p - before) / dt;
}
