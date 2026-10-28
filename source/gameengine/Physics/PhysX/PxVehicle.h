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

/** \file PxVehicle.h
 *  \ingroup physx
 *
 * BGE-Remix (P6): bge.constraints' vehicle (PHY_VEHICLE_CONSTRAINT) on a
 * PhysX body. The wheels, suspension and tyre friction are Bullet's
 * btRaycastVehicle (Erwin Coumans, zlib licence) carried over to PhysX, so
 * vehicle scripts tuned for Bullet drive the same: the same tuning values
 * (suspension stiffness, compression, damping, friction slip, roll
 * influence) mean the same things.
 */

#ifndef __PXVEHICLE_H__
#define __PXVEHICLE_H__

#include <vector>

#include "PHY_IVehicle.h"
#include "foundation/PxTransform.h"
#include "foundation/PxVec3.h"

class PxPhysicsEnvironment;
class PxPhysicsController;
class PHY_IMotionState;

class PxRaycastVehicle : public PHY_IVehicle
{
public:
	PxRaycastVehicle(PxPhysicsEnvironment *env, PxPhysicsController *chassis, int id);
	virtual ~PxRaycastVehicle() {}

	PxPhysicsController *Chassis() { return m_chassis; }

	/* each physics sub-step, before the simulation */
	void UpdateVehicle(float step);
	/* after the frame: the wheels' objects */
	void SyncWheels();

	/* PHY_IVehicle */
	virtual void AddWheel(PHY_IMotionState *motionState, MT_Vector3 connectionPoint, MT_Vector3 downDirection,
	                      MT_Vector3 axleDirection, float suspensionRestLength, float wheelRadius, bool hasSteering);
	virtual int GetNumWheels() const { return (int)m_wheels.size(); }
	virtual void GetWheelPosition(int wheelIndex, float &posX, float &posY, float &posZ) const;
	virtual void GetWheelOrientationQuaternion(int wheelIndex, float &quatX, float &quatY, float &quatZ,
	                                           float &quatW) const;
	virtual float GetWheelRotation(int wheelIndex) const;
	virtual int GetUserConstraintId() const { return m_id; }
	virtual int GetUserConstraintType() const { return 11; /* PHY_VEHICLE_CONSTRAINT */ }
	virtual void SetSteeringValue(float steering, int wheelIndex);
	virtual void ApplyEngineForce(float force, int wheelIndex);
	virtual void ApplyBraking(float braking, int wheelIndex);
	virtual void SetWheelFriction(float friction, int wheelIndex);
	virtual void SetSuspensionStiffness(float suspensionStiffness, int wheelIndex);
	virtual void SetSuspensionDamping(float suspensionDamping, int wheelIndex);
	virtual void SetSuspensionCompression(float suspensionCompression, int wheelIndex);
	virtual void SetRollInfluence(float rollInfluence, int wheelIndex);
	virtual void SetCoordinateSystem(int rightIndex, int upIndex, int forwardIndex);

private:
	struct Wheel {
		PHY_IMotionState *motionState;
		physx::PxVec3 connectionCS, directionCS, axleCS;
		float restLength, radius;
		float suspensionStiffness, dampingCompression, dampingRelaxation;
		float frictionSlip, maxSuspensionTravelCm, maxSuspensionForce;
		float rollInfluence;
		bool steeringWheel;
		float steering, rotation, deltaRotation;
		float engineForce, brake;
		float suspensionForce, suspensionRelativeVelocity, clippedInvContactDotSuspension;
		float skid;
		/* ray results */
		bool inContact;
		physx::PxVec3 hardPointWS, directionWS, axleWS, contactPointWS, contactNormalWS;
		float suspensionLength;
		physx::PxTransform worldTransform;
	};

	void UpdateWheelTransformWS(Wheel &w);
	void UpdateWheelTransform(Wheel &w);
	void RayCast(Wheel &w);
	void UpdateSuspension();
	void UpdateFriction(float step);
	/* Bullet's immediate impulses */
	void ApplyImpulse(const physx::PxVec3 &impulse, const physx::PxVec3 &relPos);
	physx::PxVec3 VelocityAt(const physx::PxVec3 &relPos) const;
	float ImpulseDenominator(const physx::PxVec3 &pos, const physx::PxVec3 &normal) const;
	physx::PxVec3 InvInertiaWorld(const physx::PxVec3 &v) const;
	physx::PxTransform ChassisTransform() const;

	PxPhysicsEnvironment *m_env;
	PxPhysicsController *m_chassis;
	int m_id;
	int m_right, m_up, m_forward;
	std::vector<Wheel> m_wheels;
	/* the chassis' velocities during the update */
	physx::PxVec3 m_lin, m_ang;
	float m_invMass;
	physx::PxVec3 m_invInertia;
	physx::PxTransform m_pose;
};

#endif  /* __PXVEHICLE_H__ */
