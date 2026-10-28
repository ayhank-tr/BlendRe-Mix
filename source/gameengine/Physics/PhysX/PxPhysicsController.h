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

/** \file PxPhysicsController.h
 *  \ingroup physx
 *
 * BGE-Remix (P6): one game object's physics on PhysX, with the behaviour of
 * Bullet's CcdPhysicsController (the same Blender settings, the same
 * conventions: the object's origin is its centre of mass, damping per second,
 * friction and bounce multiplied, static objects that the game moves become
 * kinematic, Dynamic (not Rigid Body) objects do not turn on contact).
 */

#ifndef __PXPHYSICSCONTROLLER_H__
#define __PXPHYSICSCONTROLLER_H__

#include <vector>

#include "PHY_IPhysicsController.h"
#include "PHY_IMotionState.h"
#include "MT_Matrix3x3.h"
#include "MT_Point3.h"
#include "MT_Vector3.h"

#include "foundation/PxTransform.h"
#include "foundation/PxVec3.h"
#include "PxFiltering.h"

class PxPhysicsEnvironment;
class PxCharacter;
class PxDeformable;
struct PxShapeDesc;

namespace physx {
class PxRigidActor;
class PxRigidDynamic;
class PxShape;
}

/* collision classes, as Bullet's CcdConstructionInfo::CollisionFilterGroups */
enum PxFilterClass {
	PXF_DEFAULT = 1,
	PXF_STATIC = 2,
	PXF_KINEMATIC = 4,
	PXF_DEBRIS = 8,
	PXF_SENSOR = 16,
	PXF_CHARACTER = 32,
	PXF_ALL = 63,
};

/* flags in a shape's filter data word3 */
enum {
	PXW_REPORT = 1,     /* a touch or collision sensor listens: report contacts */
	PXW_TRIGGER = 2,    /* a trigger shape (sensor, ghost) */
	PXW_DEBRIS = 4,
};

struct PxCtrlInfo
{
	PxCtrlInfo();

	PxShapeDesc *shape;          /* owned reference */
	physx::PxVec3 scaling;
	float mass;
	float inertiaFactor;         /* Blender's Form Factor / 0.4 */
	float friction, restitution;
	float linearDamping, angularDamping;  /* Bullet's: velocity *= (1 - d) per second */
	float margin;
	float radius;                /* Blender's Radius (Fh, sensors) */
	float clampVelMin, clampVelMax, clampAngVelMin, clampAngVelMax;
	bool dyna;                   /* moves by physics */
	bool rigid;                  /* Rigid Body: turns on contact */
	bool sensor;                 /* Sensor physics type, near and radar sensors */
	bool noResponse;             /* Ghost, Sensor, Character: no contact response */
	bool character;
	bool soft;
	bool disableSleep;           /* No Sleeping */
	bool anisotropic;
	physx::PxVec3 anisotropicFriction;
	physx::PxVec3 linearFactor, angularFactor;  /* 0 = locked axis */
	/* Fh springs */
	bool doFh, doRotFh;
	float fhSpring, fhDamping, fhDistance;
	bool fhNormal;
	/* character */
	float stepHeight, jumpSpeed, fallSpeed;
	unsigned char maxJumps;
	/* soft body (Blender's bsoft, see PxDeformable) */
	struct BulletSoftBody *bsoft;
	int filterGroup, filterMask;
};

class PxPhysicsController : public PHY_IPhysicsController
{
	friend class PxPhysicsEnvironment;

public:
	PxPhysicsController(const PxCtrlInfo &info, PHY_IMotionState *motionState, PxPhysicsEnvironment *env);
	virtual ~PxPhysicsController();

	/* PHY_IController */
	virtual void *GetNewClientInfo() { return m_newClientInfo; }
	virtual void SetNewClientInfo(void *clientinfo);
	virtual void SetPhysicsEnvironment(PHY_IPhysicsEnvironment *env);

	/* PHY_IPhysicsController */
	virtual bool SynchronizeMotionStates(float time);
	virtual void WriteMotionStateToDynamics(bool nondynaonly);
	virtual void WriteDynamicsToMotionState() {}
	virtual PHY_IMotionState *GetMotionState() { return m_motionState; }
	virtual void PostProcessReplica(PHY_IMotionState *motionstate, PHY_IPhysicsController *parentctrl);

	virtual void RelativeTranslate(const MT_Vector3 &dloc, bool local);
	virtual void RelativeRotate(const MT_Matrix3x3 &rot, bool local);
	virtual MT_Matrix3x3 GetOrientation();
	virtual void SetOrientation(const MT_Matrix3x3 &orn);
	virtual void SetPosition(const MT_Vector3 &pos);
	virtual void GetPosition(MT_Vector3 &pos) const;
	virtual void SetScaling(const MT_Vector3 &scale);
	virtual void SetTransform();

	virtual MT_Scalar GetMass();
	virtual void SetMass(MT_Scalar newmass);

	virtual void ApplyImpulse(const MT_Point3 &attach, const MT_Vector3 &impulse, bool local);
	virtual void ApplyTorque(const MT_Vector3 &torque, bool local);
	virtual void ApplyForce(const MT_Vector3 &force, bool local);
	virtual void SetAngularVelocity(const MT_Vector3 &ang_vel, bool local);
	virtual void SetLinearVelocity(const MT_Vector3 &lin_vel, bool local);
	virtual void ResolveCombinedVelocities(float, float, float, float, float, float) {}

	virtual float GetLinearDamping() const { return m_info.linearDamping; }
	virtual float GetAngularDamping() const { return m_info.angularDamping; }
	virtual void SetLinearDamping(float damping) { SetDamping(damping, m_info.angularDamping); }
	virtual void SetAngularDamping(float damping) { SetDamping(m_info.linearDamping, damping); }
	virtual void SetDamping(float linear, float angular);

	virtual void RefreshCollisions();
	virtual void SuspendDynamics(bool ghost = false);
	virtual void RestoreDynamics();
	virtual void SetActive(bool) {}

	virtual MT_Vector3 GetLinearVelocity();
	virtual MT_Vector3 GetAngularVelocity();
	virtual MT_Vector3 GetVelocity(const MT_Point3 &pos);
	virtual MT_Vector3 GetLocalInertia();
	virtual void SetRigidBody(bool rigid);

	virtual PHY_IPhysicsController *GetReplica();
	virtual PHY_IPhysicsController *GetReplicaForSensors();

	virtual void CalcXform() {}
	virtual void SetMargin(float margin);
	virtual float GetMargin() const { return m_info.margin; }
	virtual float GetRadius() const { return m_info.radius; }
	virtual void SetRadius(float radius);

	virtual float GetLinVelocityMin() const { return m_info.clampVelMin; }
	virtual void SetLinVelocityMin(float val) { m_info.clampVelMin = val; }
	virtual float GetLinVelocityMax() const { return m_info.clampVelMax; }
	virtual void SetLinVelocityMax(float val) { m_info.clampVelMax = val; }
	virtual void SetAngularVelocityMin(float val) { m_info.clampAngVelMin = val; }
	virtual float GetAngularVelocityMin() const { return m_info.clampAngVelMin; }
	virtual void SetAngularVelocityMax(float val) { m_info.clampAngVelMax = val; }
	virtual float GetAngularVelocityMax() const { return m_info.clampAngVelMax; }

	virtual void AddCompoundChild(PHY_IPhysicsController *child);
	virtual void RemoveCompoundChild(PHY_IPhysicsController *child);

	virtual bool IsDynamic() { return m_info.dyna; }
	virtual bool IsCompound();
	virtual bool IsSuspended() const { return m_suspended; }

	virtual bool ReinstancePhysicsShape(class KX_GameObject *from_gameobj, class RAS_MeshObject *from_meshobj);
	virtual void ReplicateConstraints(class KX_GameObject *gameobj, std::vector<class KX_GameObject *> constobj);

	/* for the environment */
	physx::PxRigidActor *GetActor() { return m_actor; }
	physx::PxRigidDynamic *GetDynamic();
	bool IsKinematic() const { return m_kinematic; }
	bool IsStaticActor() const;
	bool IsSensor() const { return m_info.sensor; }
	bool IsTrigger() const { return m_info.noResponse && !m_info.character; }
	const PxCtrlInfo &GetInfo() const { return m_info; }
	PxShapeDesc *GetShapeDesc() { return m_info.shape; }
	PxPhysicsEnvironment *GetEnvironment() { return m_env; }
	PxCharacter *GetCharacter() { return m_character; }
	PxDeformable *GetDeformable() { return m_soft; }
	void SetDeformable(PxDeformable *soft) { m_soft = soft; }
	PxPhysicsController *GetParentCtrl() { return m_parentCtrl; }
	void SetParentCtrl(PxPhysicsController *p) { m_parentCtrl = p; }

	/* touch and collision sensors (several sensors may use one controller) */
	bool Register() { return m_registerCount++ == 0; }
	bool Unregister() { return --m_registerCount == 0; }
	bool Registered() const { return m_registerCount != 0; }

	/* the filter data of this object's shapes (collision classes, the game
	 * object's collision group and mask, flags) */
	physx::PxFilterData FilterData() const;
	void UpdateFilter();

	/* each simulation sub-step: velocity clamps */
	void SimulationTick(float timestep);
	/* before each step: a kinematic object takes its place from the game */
	void PullKinematic(float timestep);
	/* the transform the game gives the object */
	physx::PxTransform MotionStateTransform() const;

	/* joints that use this object (removed with it) */
	std::vector<int> m_constraints;
	/* the actor is in the scene */
	bool m_inScene;

	/* a compound child joined this object's shape at conversion */
	void ReinstanceCompound() { RebuildShapes(); }

	/* make the PhysX objects (again) from the construction info */
	void CreateActor();
	void DestroyActor();

private:
	PxPhysicsController(const PxPhysicsController &other);  /* GetReplica */

	void ApplyMass();
	void MakeKinematic();
	void Teleport(const physx::PxTransform &t);
	void RebuildShapes();
	physx::PxTransform ActorPose() const;

	PxCtrlInfo m_info;
	PHY_IMotionState *m_motionState;
	PxPhysicsEnvironment *m_env;
	physx::PxRigidActor *m_actor;
	std::vector<physx::PxShape *> m_shapes;
	std::vector<void *> m_shapeOwners;
	void *m_newClientInfo;
	int m_registerCount;
	bool m_kinematic;      /* static that the game moves, or a sensor */
	bool m_suspended;
	PxCtrlInfo m_saved;    /* SuspendDynamics */
	bool m_savedKinematic;
	PxPhysicsController *m_parentCtrl;
	PxCharacter *m_character;
	PxDeformable *m_soft;
	physx::PxTransform m_lastKinematic;
	bool m_kinematicMoved;
};

#endif  /* __PXPHYSICSCONTROLLER_H__ */
