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

/** \file PxVehicle.cpp
 *  \ingroup physx
 *
 * The vehicle model is Bullet's btRaycastVehicle:
 * Copyright (c) 2005 Erwin Coumans http://continuousphysics.com/Bullet/
 * Permission to use, copy, modify, distribute and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies.
 */

#include "PxVehicle.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "PxPhysicsAPI.h"

#include "PxPhysicsController.h"
#include "PxPhysicsEnvironment.h"
#include "PHY_IMotionState.h"

using namespace physx;

namespace {

/* btVehicleTuning's defaults */
const float kSuspensionStiffness = 5.88f;
const float kSuspensionCompression = 0.83f;
const float kSuspensionDamping = 0.88f;
const float kMaxSuspensionTravelCm = 500.0f;
const float kFrictionSlip = 10.5f;
const float kMaxSuspensionForce = 6000.0f;

PxVec3 column(const PxMat33 &m, int i)
{
	return m[i];
}

class WheelRayFilter : public PxQueryFilterCallback
{
public:
	const PxRigidActor *chassis;
	PxQueryHitType::Enum preFilter(const PxFilterData &, const PxShape *shape, const PxRigidActor *actor,
	                               PxHitFlags &) override
	{
		/* as Bullet's raycaster: only objects with a contact response */
		if (actor == chassis || shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE)) {
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

PxRaycastVehicle::PxRaycastVehicle(PxPhysicsEnvironment *env, PxPhysicsController *chassis, int id)
    : m_env(env),
      m_chassis(chassis),
      m_id(id),
      m_right(0),
      m_up(2),
      m_forward(1),
      m_lin(0.0f),
      m_ang(0.0f),
      m_invMass(0.0f),
      m_invInertia(0.0f),
      m_pose(PxIdentity)
{
}

PxTransform PxRaycastVehicle::ChassisTransform() const
{
	PxRigidDynamic *body = m_chassis ? m_chassis->GetDynamic() : NULL;
	return body ? body->getGlobalPose() : PxTransform(PxIdentity);
}

void PxRaycastVehicle::AddWheel(PHY_IMotionState *motionState, MT_Vector3 connectionPoint, MT_Vector3 downDirection,
                                MT_Vector3 axleDirection, float suspensionRestLength, float wheelRadius,
                                bool hasSteering)
{
	Wheel w;
	memset((void *)&w, 0, sizeof(w));
	w.motionState = motionState;
	w.connectionCS = PxVec3(connectionPoint[0], connectionPoint[1], connectionPoint[2]);
	w.directionCS = PxVec3(downDirection[0], downDirection[1], downDirection[2]);
	w.axleCS = PxVec3(axleDirection[0], axleDirection[1], axleDirection[2]);
	w.restLength = suspensionRestLength;
	w.radius = wheelRadius;
	w.suspensionStiffness = kSuspensionStiffness;
	w.dampingCompression = kSuspensionCompression;
	w.dampingRelaxation = kSuspensionDamping;
	w.frictionSlip = kFrictionSlip;
	w.maxSuspensionTravelCm = kMaxSuspensionTravelCm;
	w.maxSuspensionForce = kMaxSuspensionForce;
	w.rollInfluence = 0.1f;
	w.steeringWheel = hasSteering;
	w.suspensionLength = suspensionRestLength;
	w.clippedInvContactDotSuspension = 1.0f;
	w.skid = 1.0f;
	w.worldTransform = PxTransform(PxIdentity);
	m_pose = ChassisTransform();
	m_wheels.push_back(w);
	UpdateWheelTransform(m_wheels.back());
}

void PxRaycastVehicle::UpdateWheelTransformWS(Wheel &w)
{
	w.inContact = false;
	w.hardPointWS = m_pose.transform(w.connectionCS);
	w.directionWS = m_pose.q.rotate(w.directionCS);
	w.axleWS = m_pose.q.rotate(w.axleCS);
}

void PxRaycastVehicle::UpdateWheelTransform(Wheel &w)
{
	UpdateWheelTransformWS(w);
	const PxVec3 up = -w.directionWS;
	const PxVec3 right = w.axleWS;
	const PxVec3 fwd = up.cross(right).getNormalized();
	const PxQuat steering(w.steering, up.getNormalized());
	const PxQuat rotating(-w.rotation, right.getNormalized());
	const PxMat33 basis(right, fwd, up);
	const PxMat33 m = PxMat33(steering) * PxMat33(rotating) * basis;
	w.worldTransform = PxTransform(w.hardPointWS + w.directionWS * w.suspensionLength, PxQuat(m).getNormalized());
}

PxVec3 PxRaycastVehicle::InvInertiaWorld(const PxVec3 &v) const
{
	/* R diag(I^-1) R^T v, the centre of mass at the origin */
	const PxVec3 local = m_pose.q.rotateInv(v);
	return m_pose.q.rotate(local.multiply(m_invInertia));
}

PxVec3 PxRaycastVehicle::VelocityAt(const PxVec3 &relPos) const
{
	return m_lin + m_ang.cross(relPos);
}

void PxRaycastVehicle::ApplyImpulse(const PxVec3 &impulse, const PxVec3 &relPos)
{
	m_lin += impulse * m_invMass;
	m_ang += InvInertiaWorld(relPos.cross(impulse));
}

float PxRaycastVehicle::ImpulseDenominator(const PxVec3 &pos, const PxVec3 &normal) const
{
	const PxVec3 r = pos - m_pose.p;
	const PxVec3 c0 = r.cross(normal);
	const PxVec3 vec = InvInertiaWorld(c0).cross(r);
	return m_invMass + normal.dot(vec);
}

void PxRaycastVehicle::RayCast(Wheel &w)
{
	UpdateWheelTransformWS(w);
	const float raylen = w.restLength + w.radius;
	const PxVec3 source = w.hardPointWS;
	w.contactPointWS = source + w.directionWS * raylen;

	PxScene *scene = m_env ? m_env->GetScene() : NULL;
	PxRigidDynamic *body = m_chassis->GetDynamic();
	bool hit = false;
	PxRaycastBuffer buf;
	if (scene && body && w.directionWS.magnitudeSquared() > 1e-12f && raylen > 0.0f) {
		WheelRayFilter filter;
		filter.chassis = body;
		PxQueryFilterData fd(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
		hit = scene->raycast(source, w.directionWS.getNormalized(), raylen * w.directionWS.magnitude(), buf,
		                     PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eMESH_BOTH_SIDES, fd, &filter) &&
		      buf.hasBlock;
	}
	if (hit) {
		const float param = buf.block.distance / (raylen * w.directionWS.magnitude());
		w.contactNormalWS = buf.block.normal.getNormalized();
		w.inContact = true;
		const float hitDistance = param * raylen;
		w.suspensionLength = hitDistance - w.radius;
		const float minLen = w.restLength - w.maxSuspensionTravelCm * 0.01f;
		const float maxLen = w.restLength + w.maxSuspensionTravelCm * 0.01f;
		w.suspensionLength = std::min(std::max(w.suspensionLength, minLen), maxLen);
		w.contactPointWS = buf.block.position;
		const float denominator = w.contactNormalWS.dot(w.directionWS);
		const PxVec3 vel = VelocityAt(w.contactPointWS - m_pose.p);
		const float projVel = w.contactNormalWS.dot(vel);
		if (denominator >= -0.1f) {
			w.suspensionRelativeVelocity = 0.0f;
			w.clippedInvContactDotSuspension = 1.0f / 0.1f;
		}
		else {
			const float inv = -1.0f / denominator;
			w.suspensionRelativeVelocity = projVel * inv;
			w.clippedInvContactDotSuspension = inv;
		}
	}
	else {
		w.suspensionLength = w.restLength;
		w.suspensionRelativeVelocity = 0.0f;
		w.contactNormalWS = -w.directionWS;
		w.clippedInvContactDotSuspension = 1.0f;
	}
}

void PxRaycastVehicle::UpdateSuspension()
{
	const float chassisMass = m_invMass > 0.0f ? 1.0f / m_invMass : 0.0f;
	for (Wheel &w : m_wheels) {
		if (w.inContact) {
			float force = w.suspensionStiffness * (w.restLength - w.suspensionLength) * w.clippedInvContactDotSuspension;
			const float damping = w.suspensionRelativeVelocity < 0.0f ? w.dampingCompression : w.dampingRelaxation;
			force -= damping * w.suspensionRelativeVelocity;
			w.suspensionForce = std::max(force * chassisMass, 0.0f);
		}
		else {
			w.suspensionForce = 0.0f;
		}
	}
}

void PxRaycastVehicle::UpdateFriction(float step)
{
	const int n = (int)m_wheels.size();
	if (!n) {
		return;
	}
	std::vector<PxVec3> forward(n, PxVec3(0.0f)), axle(n, PxVec3(0.0f));
	std::vector<float> forwardImpulse(n, 0.0f), sideImpulse(n, 0.0f);

	for (int i = 0; i < n; i++) {
		Wheel &w = m_wheels[i];
		if (!w.inContact) {
			continue;
		}
		const PxMat33 basis(w.worldTransform.q);
		axle[i] = column(basis, m_right);
		const PxVec3 &normal = w.contactNormalWS;
		axle[i] -= normal * axle[i].dot(normal);
		axle[i].normalizeSafe();
		forward[i] = normal.cross(axle[i]);
		forward[i].normalizeSafe();
		/* Bullet's resolveSingleBilateral against the fixed ground */
		const PxVec3 rel = w.contactPointWS - m_pose.p;
		const float jac = ImpulseDenominator(w.contactPointWS, axle[i]);
		const float relVel = axle[i].dot(VelocityAt(rel));
		sideImpulse[i] = jac > 0.0f ? -0.2f * relVel / jac : 0.0f;
	}

	const float sideFactor = 1.0f, fwdFactor = 0.5f;
	bool sliding = false;
	for (int i = 0; i < n; i++) {
		Wheel &w = m_wheels[i];
		float rollingFriction = 0.0f;
		if (w.inContact) {
			if (w.engineForce != 0.0f) {
				rollingFriction = w.engineForce * step;
			}
			else {
				const float maxImpulse = w.brake ? w.brake : 0.0f;
				const float denom = ImpulseDenominator(w.contactPointWS, forward[i]);
				const float jacInv = denom > 0.0f ? 1.0f / denom : 0.0f;
				const float vrel = forward[i].dot(VelocityAt(w.contactPointWS - m_pose.p));
				float j1 = -vrel * jacInv;
				j1 = std::min(j1, maxImpulse);
				j1 = std::max(j1, -maxImpulse);
				rollingFriction = j1;
			}
		}
		forwardImpulse[i] = 0.0f;
		w.skid = 1.0f;
		if (w.inContact) {
			const float maximp = w.suspensionForce * step * w.frictionSlip;
			const float maximpSquared = maximp * maximp;
			forwardImpulse[i] = rollingFriction;
			const float x = forwardImpulse[i] * fwdFactor;
			const float y = sideImpulse[i] * sideFactor;
			const float impulseSquared = x * x + y * y;
			if (impulseSquared > maximpSquared) {
				sliding = true;
				w.skid *= maximp / sqrtf(impulseSquared);
			}
		}
	}
	if (sliding) {
		for (int i = 0; i < n; i++) {
			if (sideImpulse[i] != 0.0f && m_wheels[i].skid < 1.0f) {
				forwardImpulse[i] *= m_wheels[i].skid;
				sideImpulse[i] *= m_wheels[i].skid;
			}
		}
	}
	const PxVec3 up = column(PxMat33(m_pose.q), m_up);
	for (int i = 0; i < n; i++) {
		Wheel &w = m_wheels[i];
		PxVec3 rel = w.contactPointWS - m_pose.p;
		if (forwardImpulse[i] != 0.0f) {
			ApplyImpulse(forward[i] * forwardImpulse[i], rel);
		}
		if (sideImpulse[i] != 0.0f) {
			/* Bullet's ROLLING_INFLUENCE_FIX */
			rel -= up * (up.dot(rel) * (1.0f - w.rollInfluence));
			ApplyImpulse(axle[i] * sideImpulse[i], rel);
		}
	}
}

void PxRaycastVehicle::UpdateVehicle(float step)
{
	PxRigidDynamic *body = m_chassis ? m_chassis->GetDynamic() : NULL;
	if (!body || m_chassis->IsKinematic() || !body->getScene()) {
		return;
	}
	m_pose = body->getGlobalPose();
	m_lin = body->getLinearVelocity();
	m_ang = body->getAngularVelocity();
	m_invMass = body->getInvMass();
	m_invInertia = body->getMassSpaceInvInertiaTensor();

	for (Wheel &w : m_wheels) {
		UpdateWheelTransform(w);
	}
	for (Wheel &w : m_wheels) {
		RayCast(w);
	}
	UpdateSuspension();
	for (Wheel &w : m_wheels) {
		const float force = std::min(w.suspensionForce, w.maxSuspensionForce);
		ApplyImpulse(w.contactNormalWS * force * step, w.contactPointWS - m_pose.p);
	}
	UpdateFriction(step);
	const PxVec3 fwd0 = column(PxMat33(m_pose.q), m_forward);
	for (Wheel &w : m_wheels) {
		const PxVec3 vel = VelocityAt(w.hardPointWS - m_pose.p);
		if (w.inContact) {
			PxVec3 fwd = fwd0 - w.contactNormalWS * fwd0.dot(w.contactNormalWS);
			w.deltaRotation = fwd.dot(vel) * step / w.radius;
		}
		w.rotation += w.deltaRotation;
		w.deltaRotation *= 0.99f;
	}
	const bool moving = (m_lin - body->getLinearVelocity()).magnitudeSquared() > 0.0f ||
	                    (m_ang - body->getAngularVelocity()).magnitudeSquared() > 0.0f;
	body->setLinearVelocity(m_lin, false);
	body->setAngularVelocity(m_ang, false);
	if (moving) {
		bool engine = false;
		for (const Wheel &w : m_wheels) {
			engine = engine || w.engineForce != 0.0f || w.steering != 0.0f;
		}
		if (engine || m_lin.magnitudeSquared() > 1e-4f) {
			body->wakeUp();
		}
	}
}

void PxRaycastVehicle::SyncWheels()
{
	m_pose = ChassisTransform();
	for (Wheel &w : m_wheels) {
		UpdateWheelTransform(w);
		if (w.motionState) {
			const PxTransform &t = w.worldTransform;
			w.motionState->SetWorldOrientation(t.q.x, t.q.y, t.q.z, t.q.w);
			w.motionState->SetWorldPosition(t.p.x, t.p.y, t.p.z);
		}
	}
}

void PxRaycastVehicle::GetWheelPosition(int i, float &x, float &y, float &z) const
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		const PxVec3 &p = m_wheels[i].worldTransform.p;
		x = p.x;
		y = p.y;
		z = p.z;
	}
}

void PxRaycastVehicle::GetWheelOrientationQuaternion(int i, float &x, float &y, float &z, float &w) const
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		const PxQuat &q = m_wheels[i].worldTransform.q;
		x = q.x;
		y = q.y;
		z = q.z;
		w = q.w;
	}
}

float PxRaycastVehicle::GetWheelRotation(int i) const
{
	return (i >= 0 && i < (int)m_wheels.size()) ? m_wheels[i].rotation : 0.0f;
}

void PxRaycastVehicle::SetSteeringValue(float steering, int i)
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		m_wheels[i].steering = steering;
	}
}

void PxRaycastVehicle::ApplyEngineForce(float force, int i)
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		m_wheels[i].engineForce = force;
	}
}

void PxRaycastVehicle::ApplyBraking(float braking, int i)
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		m_wheels[i].brake = braking;
	}
}

void PxRaycastVehicle::SetWheelFriction(float friction, int i)
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		m_wheels[i].frictionSlip = friction;
	}
}

void PxRaycastVehicle::SetSuspensionStiffness(float v, int i)
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		m_wheels[i].suspensionStiffness = v;
	}
}

void PxRaycastVehicle::SetSuspensionDamping(float v, int i)
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		m_wheels[i].dampingRelaxation = v;
	}
}

void PxRaycastVehicle::SetSuspensionCompression(float v, int i)
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		m_wheels[i].dampingCompression = v;
	}
}

void PxRaycastVehicle::SetRollInfluence(float v, int i)
{
	if (i >= 0 && i < (int)m_wheels.size()) {
		m_wheels[i].rollInfluence = v;
	}
}

void PxRaycastVehicle::SetCoordinateSystem(int rightIndex, int upIndex, int forwardIndex)
{
	m_right = rightIndex;
	m_up = upIndex;
	m_forward = forwardIndex;
}
