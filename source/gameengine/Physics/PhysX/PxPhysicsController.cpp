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

/** \file PxPhysicsController.cpp
 *  \ingroup physx
 */

#include "PxPhysicsController.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "PxPhysicsAPI.h"

#include "PxCharacter.h"
#include "PxDeformable.h"
#include "PxMotionState.h"
#include "PxPhysicsEnvironment.h"
#include "PxShapes.h"
#include "PxSystem.h"

#include "CcdPhysicsController.h"  /* CcdShapeConstructionInfo::UpdateMesh */

#include "KX_GameObject.h"
#include "KX_ClientObjectInfo.h"
#include "SG_Node.h"

using namespace physx;

/* ------------------------------------------------------------------------- */
/* helpers */

static PxVec3 px(const MT_Vector3 &v)
{
	return PxVec3((float)v.x(), (float)v.y(), (float)v.z());
}

static MT_Vector3 mt(const PxVec3 &v)
{
	return MT_Vector3(v.x, v.y, v.z);
}

static PxMat33 px(const MT_Matrix3x3 &m)
{
	/* MT rows -> PhysX columns */
	return PxMat33(PxVec3(m[0][0], m[1][0], m[2][0]), PxVec3(m[0][1], m[1][1], m[2][1]),
	               PxVec3(m[0][2], m[1][2], m[2][2]));
}

static MT_Matrix3x3 mt(const PxMat33 &m)
{
	return MT_Matrix3x3(m.column0.x, m.column1.x, m.column2.x, m.column0.y, m.column1.y, m.column2.y, m.column0.z,
	                    m.column1.z, m.column2.z);
}

static PxQuat quat_of(const PxMat33 &m)
{
	PxQuat q(m);
	if (!q.isFinite() || q.magnitudeSquared() < 1e-12f) {
		return PxQuat(PxIdentity);
	}
	return q.getNormalized();
}

/* Bullet: velocity *= (1 - d) each second; PhysX: velocity *= 1 / (1 + dt c)
 * each step. The PhysX coefficient that loses the same speed per step. */
static float px_damping(float d, float dt)
{
	if (d <= 0.0f) {
		return 0.0f;
	}
	if (d >= 1.0f) {
		return 1e4f;
	}
	if (dt <= 0.0f) {
		dt = 1.0f / 60.0f;
	}
	return (powf(1.0f - d, -dt) - 1.0f) / dt;
}

/* ------------------------------------------------------------------------- */

PxCtrlInfo::PxCtrlInfo()
    : shape(NULL),
      scaling(1.0f),
      mass(0.0f),
      inertiaFactor(1.0f),
      friction(0.5f),
      restitution(0.0f),
      linearDamping(0.0f),
      angularDamping(0.0f),
      margin(0.04f),
      radius(1.0f),
      clampVelMin(0.0f),
      clampVelMax(0.0f),
      clampAngVelMin(0.0f),
      clampAngVelMax(0.0f),
      dyna(false),
      rigid(false),
      sensor(false),
      noResponse(false),
      character(false),
      soft(false),
      disableSleep(false),
      anisotropic(false),
      anisotropicFriction(1.0f),
      linearFactor(1.0f),
      angularFactor(1.0f),
      doFh(false),
      doRotFh(false),
      fhSpring(0.0f),
      fhDamping(0.0f),
      fhDistance(0.0f),
      fhNormal(false),
      stepHeight(0.0f),
      jumpSpeed(0.0f),
      fallSpeed(0.0f),
      maxJumps(1),
      bsoft(NULL),
      filterGroup(PXF_STATIC),
      filterMask(PXF_ALL ^ PXF_STATIC)
{
}

PxPhysicsController::PxPhysicsController(const PxCtrlInfo &info, PHY_IMotionState *motionState,
                                         PxPhysicsEnvironment *env)
    : m_inScene(false),
      m_sleepTime(0.0f),
      m_slept(false),
      m_info(info),
      m_motionState(motionState),
      m_env(env),
      m_actor(NULL),
      m_newClientInfo(NULL),
      m_registerCount(0),
      m_kinematic(info.sensor),
      m_suspended(false),
      m_savedKinematic(false),
      m_parentCtrl(NULL),
      m_character(NULL),
      m_soft(NULL),
      m_lastKinematic(PxIdentity),
      m_kinematicMoved(false)
{
	if (m_info.shape) {
		m_info.shape->AddRef();
	}
	CreateActor();
}

PxPhysicsController::PxPhysicsController(const PxPhysicsController &other)
    : PHY_IPhysicsController(),
      m_inScene(false),
      m_sleepTime(0.0f),
      m_slept(false),
      m_info(other.m_info),
      m_motionState(other.m_motionState),
      m_env(other.m_env),
      m_actor(NULL),
      m_newClientInfo(NULL),
      m_registerCount(0),
      m_kinematic(other.m_kinematic && !other.m_suspended),
      m_suspended(false),
      m_savedKinematic(false),
      m_parentCtrl(NULL),
      m_character(NULL),
      m_soft(NULL),
      m_lastKinematic(PxIdentity),
      m_kinematicMoved(false)
{
	/* a suspended original: the replica starts as the object was made */
	if (other.m_suspended) {
		m_info = other.m_saved;
		m_kinematic = other.m_savedKinematic;
	}
	/* compound children added at run time stay with the original */
	m_info.shape = other.m_info.shape ? other.m_info.shape->Copy() : NULL;
}

PxPhysicsController::~PxPhysicsController()
{
	if (m_env) {
		m_env->RemoveController(this);
	}
	DestroyActor();
	delete m_motionState;
	if (m_info.shape) {
		m_info.shape->Release();
	}
}

/* ------------------------------------------------------------------------- */
/* the PhysX objects */

PxTransform PxPhysicsController::MotionStateTransform() const
{
	float pos[3], ori[12];
	m_motionState->GetWorldPosition(pos[0], pos[1], pos[2]);
	m_motionState->GetWorldOrientation(ori);
	PxMat33 m(PxVec3(ori[0], ori[1], ori[2]), PxVec3(ori[4], ori[5], ori[6]), PxVec3(ori[8], ori[9], ori[10]));
	/* the orientation without the scale */
	PxVec3 c0 = m.column0, c1 = m.column1, c2 = m.column2;
	c0.normalizeSafe();
	c1 = (c1 - c0 * c0.dot(c1));
	c1.normalizeSafe();
	c2 = c0.cross(c1);
	return PxTransform(PxVec3(pos[0], pos[1], pos[2]), quat_of(PxMat33(c0, c1, c2)));
}

PxFilterData PxPhysicsController::FilterData() const
{
	PxFilterData f;
	f.word0 = (PxU32)m_info.filterGroup;
	f.word1 = (PxU32)m_info.filterMask;
	PxU32 group = 0xffff, mask = 0xffff;
	if (m_newClientInfo) {
		KX_GameObject *gameobj = KX_GameObject::GetClientObject((KX_ClientObjectInfo *)m_newClientInfo);
		if (gameobj) {
			group = gameobj->GetUserCollisionGroup();
			mask = gameobj->GetUserCollisionMask();
		}
	}
	f.word2 = (group & 0xffff) | ((mask & 0xffff) << 16);
	f.word3 = (Registered() ? PXW_REPORT : 0) | (IsTrigger() ? PXW_TRIGGER : 0);
	return f;
}

void PxPhysicsController::UpdateFilter()
{
	if (!m_actor) {
		return;
	}
	const PxFilterData f = FilterData();
	std::vector<PxShape *> shapes(m_actor->getNbShapes());
	if (!shapes.empty()) {
		m_actor->getShapes(shapes.data(), (PxU32)shapes.size());
	}
	for (PxShape *s : shapes) {
		s->setSimulationFilterData(f);
		s->setQueryFilterData(f);
	}
	if (m_inScene && m_env && m_env->GetScene()) {
		m_env->GetScene()->resetFiltering(*m_actor);
	}
}

bool PxPhysicsController::IsStaticActor() const
{
	return m_actor && m_actor->getType() == PxActorType::eRIGID_STATIC;
}

PxRigidDynamic *PxPhysicsController::GetDynamic()
{
	return (m_actor && m_actor->getType() == PxActorType::eRIGID_DYNAMIC) ? static_cast<PxRigidDynamic *>(m_actor) : NULL;
}

void PxPhysicsController::CreateActor()
{
	PxSystem *sys = PxSystem::Get();
	if (!sys || !m_info.shape || !m_motionState) {
		return;
	}
	PxPhysics &physics = *sys->Physics();
	const PxTransform pose = MotionStateTransform();
	m_lastKinematic = pose;

	if (m_info.character && m_env) {
		/* the PhysX character is made when the object enters the scene */
		m_character = new PxCharacter(this, m_env, pose);
		return;
	}
	if (m_info.soft && m_env && m_env->UsesGpu()) {
		m_soft = PxDeformable::Create(this, m_env, pose);
		if (m_soft) {
			return;
		}
	}
	if (m_info.soft) {
		/* no GPU: the soft body stays where it is */
		PxSystem::WarnOnce("soft-cpu", "soft bodies need the GPU (PhysX GPU in World > Physics); they stay still");
	}

	const bool dynamicActor = (m_info.dyna && !m_info.soft) || m_kinematic;
	if (dynamicActor) {
		PxRigidDynamic *body = physics.createRigidDynamic(pose);
		if (m_kinematic) {
			body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
		}
		m_actor = body;
	}
	else {
		m_actor = physics.createRigidStatic(pose);
	}
	m_actor->userData = this;

	PxShapeFlags flags = PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eVISUALIZATION;
	flags |= IsTrigger() ? PxShapeFlags(PxShapeFlag::eTRIGGER_SHAPE) : PxShapeFlags(PxShapeFlag::eSIMULATION_SHAPE);
	m_shapes.clear();
	m_shapeOwners.clear();
	PxMaterial *material = sys->Material(m_info.friction, m_info.restitution);
	m_info.shape->CreateShapes(m_actor, m_info.scaling, material, FilterData(), flags,
	                           dynamicActor && !m_kinematic, m_shapes, &m_shapeOwners);
	if (m_info.anisotropic) {
		PxSystem::WarnOnce("anisotropic", "anisotropic friction is not available under PhysX (plain friction is used)");
	}
	ApplyMass();
}

void PxPhysicsController::DestroyActor()
{
	if (m_env && m_inScene) {
		m_env->RemoveController(this);
	}
	if (m_character) {
		delete m_character;  /* releases its actor */
		m_character = NULL;
		m_actor = NULL;
	}
	if (m_soft) {
		delete m_soft;
		m_soft = NULL;
	}
	if (m_actor) {
		m_actor->release();
		m_actor = NULL;
	}
	m_shapes.clear();
	m_shapeOwners.clear();
}

void PxPhysicsController::ApplyMass()
{
	PxRigidDynamic *body = GetDynamic();
	if (!body || m_character) {
		return;
	}
	const float dt = m_env ? m_env->Step() : 1.0f / 60.0f;
	if (m_kinematic) {
		body->setMass(1.0f);
		body->setMassSpaceInertiaTensor(PxVec3(1.0f));
		return;
	}
	const float mass = m_info.mass > 0.0f ? m_info.mass : 1.0f;
	/* Blender: the object's origin is its centre of mass */
	const PxVec3 origin(0.0f);
	if (!m_shapes.empty()) {
		PxRigidBodyExt::setMassAndUpdateInertia(*body, mass, &origin, true);
	}
	else {
		body->setMass(mass);
		body->setCMassLocalPose(PxTransform(PxIdentity));
		body->setMassSpaceInertiaTensor(PxVec3(mass * 0.4f));
	}
	if (m_info.rigid) {
		body->setMassSpaceInertiaTensor(body->getMassSpaceInertiaTensor() * std::max(m_info.inertiaFactor, 1e-3f));
	}
	else {
		/* Dynamic (not Rigid Body): contacts do not turn it */
		body->setMassSpaceInertiaTensor(PxVec3(0.0f));
	}
	body->setLinearDamping(px_damping(m_info.linearDamping, dt));
	body->setAngularDamping(px_damping(m_info.angularDamping, dt));
	body->setMaxDepenetrationVelocity(10.0f);
	body->setMaxAngularVelocity(1000.0f);
	if (m_env) {
		/* Bullet sleeps below these speeds: kinetic energy per mass */
		body->setSleepThreshold(m_info.disableSleep ? 0.0f : m_env->SleepEnergy());
	}
	PxRigidDynamicLockFlags lock;
	if (m_info.linearFactor.x == 0.0f) {
		lock |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
	}
	if (m_info.linearFactor.y == 0.0f) {
		lock |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
	}
	if (m_info.linearFactor.z == 0.0f) {
		lock |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
	}
	if (m_info.rigid) {
		if (m_info.angularFactor.x == 0.0f) {
			lock |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
		}
		if (m_info.angularFactor.y == 0.0f) {
			lock |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
		}
		if (m_info.angularFactor.z == 0.0f) {
			lock |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
		}
	}
	body->setRigidDynamicLockFlags(lock);
}

void PxPhysicsController::RebuildShapes()
{
	PxSystem *sys = PxSystem::Get();
	if (!sys || !m_actor || m_character || m_soft) {
		return;
	}
	if (m_env) {
		m_env->ForgetPairs(this);
	}
	for (PxShape *s : m_shapes) {
		m_actor->detachShape(*s);
	}
	m_shapes.clear();
	m_shapeOwners.clear();
	PxShapeFlags flags = PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eVISUALIZATION;
	flags |= IsTrigger() ? PxShapeFlags(PxShapeFlag::eTRIGGER_SHAPE) : PxShapeFlags(PxShapeFlag::eSIMULATION_SHAPE);
	const bool asDynamic = GetDynamic() && !m_kinematic;
	m_info.shape->CreateShapes(m_actor, m_info.scaling, sys->Material(m_info.friction, m_info.restitution),
	                           FilterData(), flags, asDynamic, m_shapes, &m_shapeOwners);
	ApplyMass();
	if (PxRigidDynamic *body = GetDynamic()) {
		if (m_inScene && !m_kinematic) {
			body->wakeUp();
		}
	}
}

void PxPhysicsController::MakeKinematic()
{
	/* Bullet: a static object that the game moves becomes kinematic */
	if (m_info.dyna || m_info.sensor || m_info.character || m_info.soft || m_kinematic) {
		return;
	}
	m_kinematic = true;
	PxSystem *sys = PxSystem::Get();
	if (!m_actor || !sys) {
		return;
	}
	/* a kinematic body takes the static's shapes and joints */
	PxRigidActor *old = m_actor;
	PxScene *scene = old->getScene();
	PxRigidDynamic *body = sys->Physics()->createRigidDynamic(old->getGlobalPose());
	body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
	body->userData = this;
	if (scene) {
		scene->removeActor(*old);
	}
	for (PxShape *s : m_shapes) {
		s->acquireReference();
		old->detachShape(*s);
		body->attachShape(*s);
		s->release();
	}
	m_actor = body;
	m_lastKinematic = body->getGlobalPose();
	ApplyMass();
	if (scene) {
		scene->addActor(*body);
	}
	if (m_env) {
		m_env->ForgetPairs(this);
		m_env->RebindJoints(this);
		if (m_inScene) {
			m_env->m_kinematics.insert(this);
		}
	}
	old->release();
}

void PxPhysicsController::Teleport(const PxTransform &t)
{
	if (!m_actor) {
		return;
	}
	if (m_character) {
		m_character->SetPosition(t.p);
		return;
	}
	if (PxRigidDynamic *body = GetDynamic()) {
		if (m_kinematic) {
			if (m_info.sensor || !m_inScene) {
				body->setGlobalPose(t);
			}
			else {
				body->setKinematicTarget(t);
			}
			m_lastKinematic = t;
		}
		else {
			body->setGlobalPose(t, m_inScene);
		}
		return;
	}
	m_actor->setGlobalPose(t);
}

PxTransform PxPhysicsController::ActorPose() const
{
	if (m_character) {
		return m_character->Pose();
	}
	return m_actor ? m_actor->getGlobalPose() : MotionStateTransform();
}

void PxPhysicsController::PullKinematic(float)
{
	if (!m_kinematic || m_info.sensor || !m_actor || !m_inScene || m_character) {
		return;
	}
	const PxTransform t = MotionStateTransform();
	if ((t.p - m_lastKinematic.p).magnitudeSquared() > 1e-12f || fabsf(t.q.dot(m_lastKinematic.q)) < 1.0f - 1e-7f) {
		if (PxRigidDynamic *body = GetDynamic()) {
			body->setKinematicTarget(t);
		}
		m_lastKinematic = t;
	}
}

/* ------------------------------------------------------------------------- */
/* PHY_IController */

void PxPhysicsController::SetNewClientInfo(void *clientinfo)
{
	m_newClientInfo = clientinfo;
	if (m_info.sensor && clientinfo) {
		/* as Bullet: a sensor object follows its game object by itself */
		KX_GameObject *gameobj = KX_GameObject::GetClientObject((KX_ClientObjectInfo *)clientinfo);
		if (gameobj && gameobj->GetSGNode()) {
			SG_Callbacks &callbacks = gameobj->GetSGNode()->GetCallBackFunctions();
			callbacks.m_updatefunc = KX_GameObject::SynchronizeTransformFunc;
		}
	}
	UpdateFilter();
}

void PxPhysicsController::SetPhysicsEnvironment(PHY_IPhysicsEnvironment *env)
{
	PxPhysicsEnvironment *pxenv = dynamic_cast<PxPhysicsEnvironment *>(env);
	if (!pxenv || pxenv == m_env) {
		return;
	}
	const bool was = m_env && m_env->RemoveController(this);
	m_env = pxenv;
	if (was) {
		m_env->AddController(this);
		if (PxRigidDynamic *body = GetDynamic()) {
			if (!m_kinematic) {
				body->wakeUp();
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* PHY_IPhysicsController */

bool PxPhysicsController::SynchronizeMotionStates(float)
{
	if (m_soft) {
		m_soft->SynchronizeMotionState(m_motionState);
		return true;
	}
	if (m_character) {
		const PxTransform t = m_character->Pose();
		m_motionState->SetWorldPosition(t.p.x, t.p.y, t.p.z);
		m_motionState->CalculateWorldTransformations();
		return true;
	}
	PxRigidDynamic *body = GetDynamic();
	if (body && !m_kinematic) {
		const PxTransform t = body->getGlobalPose();
		const PxMat33 m(t.q);
		float ori[12] = {m.column0.x, m.column0.y, m.column0.z, 0.0f, m.column1.x, m.column1.y, m.column1.z, 0.0f,
		                 m.column2.x, m.column2.y, m.column2.z, 0.0f};
		m_motionState->SetWorldOrientation(ori);
		m_motionState->SetWorldPosition(t.p.x, t.p.y, t.p.z);
		m_motionState->CalculateWorldTransformations();
	}
	/* the game changed the object's scale: the shapes follow */
	float s[3];
	m_motionState->GetWorldScaling(s[0], s[1], s[2]);
	if (fabsf(s[0] - m_info.scaling.x) > 1e-5f || fabsf(s[1] - m_info.scaling.y) > 1e-5f ||
	    fabsf(s[2] - m_info.scaling.z) > 1e-5f)
	{
		SetScaling(MT_Vector3(s[0], s[1], s[2]));
	}
	return true;
}

void PxPhysicsController::WriteMotionStateToDynamics(bool)
{
	Teleport(MotionStateTransform());
}

void PxPhysicsController::PostProcessReplica(PHY_IMotionState *motionstate, PHY_IPhysicsController *parentctrl)
{
	m_parentCtrl = dynamic_cast<PxPhysicsController *>(parentctrl);
	m_motionState = motionstate;
	m_registerCount = 0;
	m_constraints.clear();
	m_actor = NULL;
	m_character = NULL;
	m_soft = NULL;
	CreateActor();
	/* sensor objects are added when a sensor needs them */
	if (!m_info.sensor && m_env) {
		m_env->AddController(this);
	}
}

void PxPhysicsController::RelativeTranslate(const MT_Vector3 &dloc, bool local)
{
	if (!m_actor) {
		return;
	}
	if (!m_info.dyna && !m_info.sensor) {
		MakeKinematic();
		return;
	}
	PxTransform t = ActorPose();
	PxVec3 d = px(dloc);
	if (local) {
		d = t.q.rotate(d);
	}
	t.p += d;
	Teleport(t);
}

void PxPhysicsController::RelativeRotate(const MT_Matrix3x3 &rot, bool local)
{
	if (!m_actor) {
		return;
	}
	if (!m_info.dyna && !m_info.sensor) {
		MakeKinematic();
		return;
	}
	PxTransform t = ActorPose();
	const PxMat33 r = px(rot);
	PxMat33 cur(t.q);
	PxMat33 next = local ? cur * r : r * cur;
	t.q = quat_of(next);
	Teleport(t);
}

MT_Matrix3x3 PxPhysicsController::GetOrientation()
{
	return mt(PxMat33(ActorPose().q));
}

void PxPhysicsController::SetOrientation(const MT_Matrix3x3 &orn)
{
	if (!m_actor) {
		return;
	}
	if (!m_info.dyna && !m_info.sensor) {
		MakeKinematic();
	}
	PxTransform t = ActorPose();
	t.q = quat_of(px(orn));
	if (m_info.dyna || m_info.sensor) {
		Teleport(t);
	}
}

void PxPhysicsController::SetPosition(const MT_Vector3 &pos)
{
	if (!m_actor) {
		return;
	}
	if (!m_info.dyna && !m_info.sensor) {
		MakeKinematic();
		return;
	}
	PxTransform t = ActorPose();
	t.p = px(pos);
	Teleport(t);
}

void PxPhysicsController::GetPosition(MT_Vector3 &pos) const
{
	pos = mt(ActorPose().p);
}

void PxPhysicsController::SetScaling(const MT_Vector3 &scale)
{
	const PxVec3 s = px(scale);
	if ((s - m_info.scaling).abs().maxElement() < 1e-5f) {
		return;
	}
	m_info.scaling = s;
	if (m_character) {
		return;  /* the character keeps its capsule */
	}
	RebuildShapes();
}

void PxPhysicsController::SetTransform()
{
	const PxTransform t = MotionStateTransform();
	if (m_info.sensor) {
		Teleport(t);
		return;
	}
	if (m_info.dyna || m_character) {
		/* Bullet sets the transform without waking the body */
		if (PxRigidDynamic *body = GetDynamic()) {
			if (!m_kinematic) {
				body->setGlobalPose(t, false);
				return;
			}
		}
		Teleport(t);
		return;
	}
	/* a static object: kinematic when the game moved it */
	const PxTransform cur = ActorPose();
	if ((t.p - cur.p).magnitudeSquared() > 1e-10f || fabsf(t.q.dot(cur.q)) < 1.0f - 1e-6f) {
		MakeKinematic();
	}
}

MT_Scalar PxPhysicsController::GetMass()
{
	if (m_soft) {
		return m_soft->Mass();
	}
	PxRigidDynamic *body = GetDynamic();
	if (body && m_info.dyna && !m_kinematic) {
		return body->getMass();
	}
	return 0.0f;
}

void PxPhysicsController::SetMass(MT_Scalar newmass)
{
	if (GetDynamic() && !m_suspended && newmass > MT_EPSILON && GetMass() > MT_EPSILON) {
		m_info.mass = (float)newmass;
		ApplyMass();
	}
}

void PxPhysicsController::ApplyImpulse(const MT_Point3 &attach, const MT_Vector3 &impulsein, bool local)
{
	PxVec3 impulse = px(impulsein);
	if (impulse.magnitudeSquared() <= 1e-14f || !m_actor) {
		return;
	}
	if (!m_info.dyna) {
		MakeKinematic();
		return;
	}
	PxRigidDynamic *body = GetDynamic();
	if (!body || m_kinematic) {
		return;
	}
	const PxTransform t = body->getGlobalPose();
	PxVec3 rel;
	if (local) {
		/* as Bullet: the point as given, the impulse turned with the object */
		rel = PxVec3((float)attach.x(), (float)attach.y(), (float)attach.z());
		impulse = t.q.rotate(impulse);
	}
	else {
		rel = PxVec3((float)attach.x(), (float)attach.y(), (float)attach.z()) - t.p;
	}
	PxRigidBodyExt::addForceAtPos(*body, impulse, t.p + rel, PxForceMode::eIMPULSE);
}

void PxPhysicsController::ApplyTorque(const MT_Vector3 &torquein, bool local)
{
	PxVec3 torque = px(torquein);
	if (torque.magnitudeSquared() <= 1e-14f || !m_actor) {
		return;
	}
	if (!m_info.dyna) {
		MakeKinematic();
		return;
	}
	PxRigidDynamic *body = GetDynamic();
	if (!body || m_kinematic) {
		return;
	}
	if (local) {
		torque = body->getGlobalPose().q.rotate(torque);
	}
	if (!m_info.rigid) {
		PxSystem::WarnOnce("dynamic-torque", "torque does not turn Dynamic objects under PhysX (use Rigid Body)");
		return;
	}
	body->addTorque(torque);
}

void PxPhysicsController::ApplyForce(const MT_Vector3 &forcein, bool local)
{
	PxVec3 force = px(forcein);
	if (force.magnitudeSquared() <= 1e-14f) {
		return;
	}
	if (m_soft) {
		m_soft->AddForce(force);
		return;
	}
	if (!m_actor) {
		return;
	}
	if (!m_info.dyna) {
		MakeKinematic();
		return;
	}
	PxRigidDynamic *body = GetDynamic();
	if (!body || m_kinematic) {
		return;
	}
	if (local) {
		force = body->getGlobalPose().q.rotate(force);
	}
	body->addForce(force);
}

void PxPhysicsController::SetAngularVelocity(const MT_Vector3 &ang_vel, bool local)
{
	PxVec3 v = px(ang_vel);
	const float l2 = v.magnitudeSquared();
	if (l2 > 0.0f && l2 <= 1e-14f) {
		v = PxVec3(0.0f);
	}
	if (!m_actor) {
		return;
	}
	if (!m_info.dyna) {
		MakeKinematic();
		return;
	}
	PxRigidDynamic *body = GetDynamic();
	if (!body || m_kinematic) {
		return;
	}
	if (local) {
		v = body->getGlobalPose().q.rotate(v);
	}
	body->setAngularVelocity(v);
}

void PxPhysicsController::SetLinearVelocity(const MT_Vector3 &lin_vel, bool local)
{
	PxVec3 v = px(lin_vel);
	const float l2 = v.magnitudeSquared();
	if (l2 > 0.0f && l2 <= 1e-14f) {
		v = PxVec3(0.0f);
	}
	if (m_soft) {
		m_soft->SetVelocity(v);
		return;
	}
	if (!m_actor) {
		return;
	}
	if (!m_info.dyna) {
		MakeKinematic();
		return;
	}
	PxRigidDynamic *body = GetDynamic();
	if (!body || m_kinematic) {
		return;
	}
	if (local) {
		v = body->getGlobalPose().q.rotate(v);
	}
	body->setLinearVelocity(v);
}

void PxPhysicsController::SetDamping(float linear, float angular)
{
	m_info.linearDamping = linear;
	m_info.angularDamping = angular;
	if (PxRigidDynamic *body = GetDynamic()) {
		if (!m_kinematic) {
			const float dt = m_env ? m_env->Step() : 1.0f / 60.0f;
			body->setLinearDamping(px_damping(linear, dt));
			body->setAngularDamping(px_damping(angular, dt));
		}
	}
}

void PxPhysicsController::RefreshCollisions()
{
	if (!m_inScene) {
		return;
	}
	if (m_env) {
		m_env->ForgetPairs(this);
	}
	UpdateFilter();
}

void PxPhysicsController::SuspendDynamics(bool ghost)
{
	PxRigidDynamic *body = GetDynamic();
	if (!body || m_suspended || m_info.sensor || !m_inScene || m_character) {
		return;
	}
	m_saved = m_info;
	m_savedKinematic = m_kinematic;
	m_suspended = true;
	/* Bullet: static, with no response when asked; here kinematic, so the game
	 * can keep moving it */
	m_info.dyna = false;
	if (ghost) {
		m_info.noResponse = true;
	}
	m_info.filterGroup = PXF_STATIC;
	m_info.filterMask = PXF_ALL ^ PXF_STATIC;
	m_kinematic = true;
	body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);
	m_lastKinematic = body->getGlobalPose();
	if (ghost) {
		RebuildShapes();
	}
	else {
		UpdateFilter();
	}
}

void PxPhysicsController::RestoreDynamics()
{
	PxRigidDynamic *body = GetDynamic();
	if (!body || !m_suspended || !m_inScene) {
		return;
	}
	/* first the place the game gave it meanwhile */
	body->setGlobalPose(MotionStateTransform());
	const bool reshape = m_info.noResponse != m_saved.noResponse;
	const float scaling_x = m_info.scaling.x, scaling_y = m_info.scaling.y, scaling_z = m_info.scaling.z;
	m_info = m_saved;
	m_info.scaling = PxVec3(scaling_x, scaling_y, scaling_z);
	m_kinematic = m_savedKinematic;
	m_suspended = false;
	body->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, m_kinematic);
	if (reshape) {
		RebuildShapes();
	}
	else {
		ApplyMass();
		UpdateFilter();
	}
	if (!m_kinematic) {
		body->wakeUp();
	}
}

MT_Vector3 PxPhysicsController::GetLinearVelocity()
{
	if (m_character) {
		return mt(m_character->Velocity());
	}
	PxRigidDynamic *body = GetDynamic();
	if (body && !m_kinematic) {
		return mt(body->getLinearVelocity());
	}
	return MT_Vector3(0.0f, 0.0f, 0.0f);
}

MT_Vector3 PxPhysicsController::GetAngularVelocity()
{
	PxRigidDynamic *body = GetDynamic();
	if (body && !m_kinematic) {
		return mt(body->getAngularVelocity());
	}
	return MT_Vector3(0.0f, 0.0f, 0.0f);
}

MT_Vector3 PxPhysicsController::GetVelocity(const MT_Point3 &pos)
{
	PxRigidDynamic *body = GetDynamic();
	if (body && !m_kinematic) {
		const PxVec3 rel((float)pos.x(), (float)pos.y(), (float)pos.z());
		return mt(body->getLinearVelocity() + body->getAngularVelocity().cross(rel));
	}
	return MT_Vector3(0.0f, 0.0f, 0.0f);
}

MT_Vector3 PxPhysicsController::GetLocalInertia()
{
	PxRigidDynamic *body = GetDynamic();
	if (body && !m_kinematic) {
		return mt(body->getMassSpaceInertiaTensor());
	}
	return MT_Vector3(0.0f, 0.0f, 0.0f);
}

void PxPhysicsController::SetRigidBody(bool rigid)
{
	PxRigidDynamic *body = GetDynamic();
	if (!body) {
		return;
	}
	m_info.rigid = rigid;
	ApplyMass();
	if (!rigid && !m_kinematic) {
		body->setAngularVelocity(PxVec3(0.0f));
	}
}

PHY_IPhysicsController *PxPhysicsController::GetReplica()
{
	return new PxPhysicsController(*this);
}

PHY_IPhysicsController *PxPhysicsController::GetReplicaForSensors()
{
	/* near and radar sensors' own objects */
	PxCtrlInfo info = m_info;
	info.shape = m_info.shape ? m_info.shape->Copy() : NULL;
	PxPhysicsController *replica = new PxPhysicsController(info, new PxDefaultMotionState(), m_env);
	if (info.shape) {
		info.shape->Release();
	}
	return replica;
}

void PxPhysicsController::SetMargin(float margin)
{
	m_info.margin = margin;
	if (m_info.shape && m_info.shape->m_type != PHY_SHAPE_COMPOUND) {
		m_info.shape->m_margin = std::max(margin, 0.0f);
		RebuildShapes();
	}
}

void PxPhysicsController::SetRadius(float radius)
{
	m_info.radius = radius;
	/* a near sensor's sphere: its size is the distance */
	if (m_info.shape && m_info.shape->m_type == PHY_SHAPE_SPHERE && m_info.sensor) {
		m_info.shape->m_radius = radius;
		for (PxShape *s : m_shapes) {
			if (s->getGeometry().getType() == PxGeometryType::eSPHERE) {
				s->setGeometry(PxSphereGeometry(std::max(radius * m_info.scaling.x, 1e-4f)));
			}
		}
	}
}

bool PxPhysicsController::IsCompound()
{
	return m_info.shape && m_info.shape->m_type == PHY_SHAPE_COMPOUND;
}

void PxPhysicsController::AddCompoundChild(PHY_IPhysicsController *child)
{
	PxPhysicsController *c = dynamic_cast<PxPhysicsController *>(child);
	if (!c || !IsCompound() || !m_actor || !c->m_actor || !c->m_info.shape || c == this) {
		return;
	}
	PxSystem *sys = PxSystem::Get();
	if (!sys) {
		return;
	}
	/* the child's place relative to this object, without this object's scale */
	const PxTransform root = MotionStateTransform();
	const PxTransform cht = c->MotionStateTransform();
	const PxVec3 inv(1.0f / m_info.scaling.x, 1.0f / m_info.scaling.y, 1.0f / m_info.scaling.z);
	PxShapeDesc::Child ch;
	ch.desc = c->m_info.shape->AddRef();
	ch.local = PxTransform(root.q.rotateInv(cht.p - root.p).multiply(inv), root.q.getConjugate() * cht.q);
	ch.scale = c->m_info.scaling.multiply(inv);
	ch.owner = c;
	m_info.shape->m_children.push_back(ch);

	/* only the new child's shapes */
	PxShapeDesc one;
	one.m_type = PHY_SHAPE_COMPOUND;
	one.m_children.push_back(ch);
	ch.desc->AddRef();
	PxShapeFlags flags = PxShapeFlag::eSCENE_QUERY_SHAPE | PxShapeFlag::eVISUALIZATION;
	flags |= IsTrigger() ? PxShapeFlags(PxShapeFlag::eTRIGGER_SHAPE) : PxShapeFlags(PxShapeFlag::eSIMULATION_SHAPE);
	one.CreateShapes(m_actor, m_info.scaling, sys->Material(m_info.friction, m_info.restitution), FilterData(), flags,
	                 GetDynamic() && !m_kinematic, m_shapes, &m_shapeOwners);
	ApplyMass();
	if (m_env) {
		m_env->RemoveController(c);
	}
}

void PxPhysicsController::RemoveCompoundChild(PHY_IPhysicsController *child)
{
	PxPhysicsController *c = dynamic_cast<PxPhysicsController *>(child);
	if (!c || !IsCompound() || !m_actor) {
		return;
	}
	std::vector<PxShapeDesc::Child> &children = m_info.shape->m_children;
	bool found = false;
	for (size_t i = 0; i < children.size(); i++) {
		if (children[i].owner == c) {
			children[i].desc->Release();
			children.erase(children.begin() + i);
			found = true;
			break;
		}
	}
	if (!found) {
		return;
	}
	for (size_t i = 0; i < m_shapes.size();) {
		if (m_shapeOwners[i] == c) {
			m_actor->detachShape(*m_shapes[i]);
			m_shapes.erase(m_shapes.begin() + i);
			m_shapeOwners.erase(m_shapeOwners.begin() + i);
		}
		else {
			i++;
		}
	}
	ApplyMass();
	if (m_env) {
		m_env->ForgetPairs(this);
		m_env->AddController(c);
	}
}

bool PxPhysicsController::ReinstancePhysicsShape(KX_GameObject *from_gameobj, RAS_MeshObject *from_meshobj)
{
	if (!m_info.shape || m_info.shape->m_type != PHY_SHAPE_MESH || !m_info.shape->m_mesh) {
		return false;
	}
	if (!from_gameobj && !from_meshobj) {
		from_gameobj = KX_GameObject::GetClientObject((KX_ClientObjectInfo *)GetNewClientInfo());
	}
	CcdShapeConstructionInfo *mesh = m_info.shape->m_mesh;
	mesh->UpdateMesh(from_gameobj, from_meshobj);
	PxShapeDesc::Uncook(mesh);
	/* every object using this mesh gets the new shape */
	if (m_env) {
		std::vector<PxPhysicsController *> users;
		for (PxPhysicsController *ctrl : m_env->m_controllers) {
			if (ctrl->m_info.shape && ctrl->m_info.shape->m_mesh == mesh) {
				users.push_back(ctrl);
			}
		}
		for (PxPhysicsController *ctrl : users) {
			ctrl->RebuildShapes();
		}
	}
	else {
		RebuildShapes();
	}
	return true;
}

void PxPhysicsController::ReplicateConstraints(KX_GameObject *replica, std::vector<KX_GameObject *> constobj)
{
	if (replica->GetConstraints().size() == 0 || !replica->GetPhysicsController() || !m_env) {
		return;
	}
	std::vector<bRigidBodyJointConstraint *> constraints = replica->GetConstraints();
	for (bRigidBodyJointConstraint *dat : constraints) {
		for (KX_GameObject *member : constobj) {
			if (dat->tar && dat->tar->id.name + 2 == member->GetName() && member->GetPhysicsController()) {
				m_env->SetupObjectConstraints(replica, member, dat);
			}
		}
	}
}

void PxPhysicsController::SimulationTick(float)
{
	PxRigidDynamic *body = GetDynamic();
	if (!body || m_kinematic || !m_info.dyna) {
		return;
	}
	if (m_info.clampVelMax > 0.0f || m_info.clampVelMin > 0.0f) {
		const PxVec3 v = body->getLinearVelocity();
		const float len = v.magnitude();
		if (m_info.clampVelMax > 0.0f && len > m_info.clampVelMax) {
			body->setLinearVelocity(v * (m_info.clampVelMax / len));
		}
		else if (m_info.clampVelMin > 0.0f && len > 1e-6f && len < m_info.clampVelMin) {
			body->setLinearVelocity(v * (m_info.clampVelMin / len));
		}
	}
	if (m_info.clampAngVelMax > 0.0f || m_info.clampAngVelMin > 0.0f) {
		const PxVec3 w = body->getAngularVelocity();
		const float len = w.magnitude();
		if (m_info.clampAngVelMax > 0.0f && len > m_info.clampAngVelMax) {
			body->setAngularVelocity(w * (m_info.clampAngVelMax / len));
		}
		else if (m_info.clampAngVelMin > 0.0f && len > 1e-6f && len < m_info.clampAngVelMin) {
			body->setAngularVelocity(w * (m_info.clampAngVelMin / len));
		}
	}
	if (m_info.disableSleep) {
		body->wakeUp();
	}
}
