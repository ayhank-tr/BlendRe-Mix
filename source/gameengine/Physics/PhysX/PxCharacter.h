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

/** \file PxCharacter.h
 *  \ingroup physx
 *
 * BGE-Remix (P6): the Character physics type on PhysX's character controller,
 * with the behaviour of Bullet's btKinematicCharacterController as the game
 * engine used it: the walk direction is a step per physics tick, gravity
 * 3 x 9.8 m/s2 by default, jumps up to Max Jumps, Step Height, Fall Speed.
 */

#ifndef __PXCHARACTER_H__
#define __PXCHARACTER_H__

#include "PHY_ICharacter.h"
#include "MT_Vector3.h"

#include "foundation/PxTransform.h"
#include "foundation/PxVec3.h"

class PxPhysicsController;
class PxPhysicsEnvironment;

namespace physx {
class PxController;
class PxRigidDynamic;
}

class PxCharacter : public PHY_ICharacter
{
public:
	PxCharacter(PxPhysicsController *ctrl, PxPhysicsEnvironment *env, const physx::PxTransform &pose);
	virtual ~PxCharacter();

	/* PHY_ICharacter */
	virtual void Jump();
	virtual bool OnGround() { return m_onGround; }
	virtual float GetGravity() { return m_gravity; }
	virtual void SetGravity(float gravity) { m_gravity = gravity; }
	virtual unsigned char GetMaxJumps() { return m_maxJumps; }
	virtual void SetMaxJumps(unsigned char maxJumps) { m_maxJumps = maxJumps; }
	virtual unsigned char GetJumpCount() { return m_jumpCount; }
	virtual void SetWalkDirection(const MT_Vector3 &dir);
	virtual MT_Vector3 GetWalkDirection();

	/* the PhysX character exists only while the object is in the scene
	 * (objects on hidden layers wait there) */
	void AddToScene();
	void RemoveFromScene();
	physx::PxRigidDynamic *Actor();
	physx::PxTransform Pose() const;
	void SetPosition(const physx::PxVec3 &p);
	physx::PxVec3 Velocity() const { return m_velocity; }
	/* one physics sub-step */
	void Step(float dt);

private:
	PxPhysicsController *m_ctrl;
	PxPhysicsEnvironment *m_env;
	physx::PxController *m_controller;
	physx::PxVec3 m_walk;
	physx::PxVec3 m_velocity;
	float m_verticalVelocity;
	float m_gravity, m_jumpSpeed, m_fallSpeed;
	unsigned char m_maxJumps, m_jumpCount;
	bool m_onGround;
	physx::PxQuat m_orientation;
	physx::PxVec3 m_position;  /* while not in the scene */
	float m_radius, m_height, m_stepOffset;
};

#endif  /* __PXCHARACTER_H__ */
