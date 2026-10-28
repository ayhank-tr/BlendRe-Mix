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

/** \file PxPhysicsEnvironment.cpp
 *  \ingroup physx
 */

#include "PxPhysicsEnvironment.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "PxPhysicsAPI.h"

#include "PxCharacter.h"
#include "PxDeformable.h"
#include "PxMotionState.h"
#include "PxPhysicsController.h"
#include "PxShapes.h"
#include "PxSystem.h"
#include "PxVehicle.h"

#include "CcdPhysicsController.h"   /* CcdShapeConstructionInfo */
#include "CcdPhysicsEnvironment.h"  /* the culling tree */

#include "PHY_IMotionState.h"
#include "PHY_Pro.h"
#include "MT_Matrix3x3.h"

#include "KX_GameObject.h"
#include "KX_PythonInit.h"  /* KX_RasterizerDrawDebugLine */
#include "KX_BlenderSceneConverter.h"
#include "KX_KetsjiEngine.h"
#include "RAS_MeshObject.h"

#include "DNA_constraint_types.h"
#include "DNA_object_force.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"

extern "C" {
#include "BLI_utildefines.h"
#include "BKE_object.h"
}

using namespace physx;

#define PX_CONSTRAINT_DISABLE_LINKED_COLLISION 0x80

static PxVec3 px(const MT_Vector3 &v)
{
	return PxVec3((float)v.x(), (float)v.y(), (float)v.z());
}

static MT_Vector3 mt(const PxVec3 &v)
{
	return MT_Vector3(v.x, v.y, v.z);
}

/* ------------------------------------------------------------------------- */
/* collision filtering: the same pairs as Bullet's broadphase */

static PxFilterFlags filter_shader(PxFilterObjectAttributes a0, PxFilterData f0, PxFilterObjectAttributes a1,
                                   PxFilterData f1, PxPairFlags &pairFlags, const void *, PxU32)
{
	/* collision classes (static, sensor, ...) */
	if ((f0.word0 & f1.word1) == 0 || (f1.word0 & f0.word1) == 0) {
		return PxFilterFlag::eSUPPRESS;
	}
	/* the game objects' collision groups and masks (both ways, as Bullet) */
	const PxU32 g0 = f0.word2 & 0xffff, m0 = f0.word2 >> 16;
	const PxU32 g1 = f1.word2 & 0xffff, m1 = f1.word2 >> 16;
	if ((g0 & m1) == 0 || (g1 & m0) == 0) {
		return PxFilterFlag::eSUPPRESS;
	}
	const PxFilterObjectType::Enum t0 = PxGetFilterObjectType(a0), t1 = PxGetFilterObjectType(a1);
	const bool deformable = t0 == PxFilterObjectType::eDEFORMABLE_SURFACE ||
	                        t0 == PxFilterObjectType::eDEFORMABLE_VOLUME ||
	                        t1 == PxFilterObjectType::eDEFORMABLE_SURFACE ||
	                        t1 == PxFilterObjectType::eDEFORMABLE_VOLUME;
	const bool trigger = PxFilterObjectIsTrigger(a0) || PxFilterObjectIsTrigger(a1);
	if (trigger && deformable) {
		/* PhysX triggers do not sense GPU cloth or soft bodies */
		return PxFilterFlag::eSUPPRESS;
	}
	if (trigger) {
		pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
		return PxFilterFlag::eDEFAULT;
	}
	/* no contacts between static or kinematic objects (kept by the filtering
	 * mode only for triggers) */
	const bool fixed0 = PxGetFilterObjectType(a0) == PxFilterObjectType::eRIGID_STATIC ||
	                    PxFilterObjectIsKinematic(a0);
	const bool fixed1 = PxGetFilterObjectType(a1) == PxFilterObjectType::eRIGID_STATIC ||
	                    PxFilterObjectIsKinematic(a1);
	if (fixed0 && fixed1) {
		return PxFilterFlag::eSUPPRESS;
	}
	pairFlags = PxPairFlag::eCONTACT_DEFAULT;
	/* GPU cloth and soft bodies: contacts without reports */
	if (deformable) {
		return PxFilterFlag::eDEFAULT;
	}
	if ((f0.word3 | f1.word3) & PXW_REPORT) {
		pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND | PxPairFlag::eNOTIFY_TOUCH_PERSISTS |
		             PxPairFlag::eNOTIFY_TOUCH_LOST | PxPairFlag::eNOTIFY_CONTACT_POINTS;
	}
	return PxFilterFlag::eDEFAULT;
}

/* ------------------------------------------------------------------------- */
/* contact and trigger reports */

class PxPhysicsEnvironment::EventCallback : public PxSimulationEventCallback
{
public:
	EventCallback(PxPhysicsEnvironment *env) : m_env(env) {}

	void onContact(const PxContactPairHeader &header, const PxContactPair *pairs, PxU32 nbPairs) override
	{
		if (header.flags & (PxContactPairHeaderFlag::eREMOVED_ACTOR_0 | PxContactPairHeaderFlag::eREMOVED_ACTOR_1)) {
			return;
		}
		PxPhysicsController *a = (PxPhysicsController *)header.actors[0]->userData;
		PxPhysicsController *b = (PxPhysicsController *)header.actors[1]->userData;
		if (!a || !b) {
			return;
		}
		for (PxU32 i = 0; i < nbPairs; i++) {
			const PxContactPair &cp = pairs[i];
			if (cp.flags & (PxContactPairFlag::eREMOVED_SHAPE_0 | PxContactPairFlag::eREMOVED_SHAPE_1)) {
				continue;
			}
			m_env->m_stats.contactReports++;
			if (cp.events & PxPairFlag::eNOTIFY_TOUCH_LOST) {
				m_env->ContactLost(a, b);
				continue;
			}
			PxContactPairPoint pt[1];
			const PxU32 n = cp.contactCount ? cp.extractContacts(pt, 1) : 0;
			if (n) {
				/* PhysX: the normal points from the second shape to the first,
				 * as Bullet's normal on B (towards A): kept */
				const PxVec3 pa = pt[0].position, pb = pt[0].position - pt[0].normal * pt[0].separation;
				m_env->ContactFound(a, b, mt(pa), mt(pb), mt(pt[0].normal));
			}
			else {
				const PxVec3 pa = static_cast<PxRigidActor *>(header.actors[0])->getGlobalPose().p;
				const PxVec3 pb = static_cast<PxRigidActor *>(header.actors[1])->getGlobalPose().p;
				m_env->ContactFound(a, b, mt(pa), mt(pb), mt((pa - pb).getNormalized()));
			}
		}
	}

	void onTrigger(PxTriggerPair *pairs, PxU32 count) override
	{
		for (PxU32 i = 0; i < count; i++) {
			const PxTriggerPair &tp = pairs[i];
			if (tp.flags & (PxTriggerPairFlag::eREMOVED_SHAPE_TRIGGER | PxTriggerPairFlag::eREMOVED_SHAPE_OTHER)) {
				continue;
			}
			PxPhysicsController *a = (PxPhysicsController *)tp.triggerActor->userData;
			PxPhysicsController *b = (PxPhysicsController *)tp.otherActor->userData;
			if (!a || !b) {
				continue;
			}
			m_env->m_stats.triggerReports++;
			if (tp.status == PxPairFlag::eNOTIFY_TOUCH_LOST) {
				m_env->ContactLost(a, b);
			}
			else {
				const PxVec3 pa = static_cast<PxRigidActor *>(tp.triggerActor)->getGlobalPose().p;
				const PxVec3 pb = static_cast<PxRigidActor *>(tp.otherActor)->getGlobalPose().p;
				m_env->ContactFound(a, b, mt(pa), mt(pb), mt((pa - pb).getNormalized()));
			}
		}
	}

	void onConstraintBreak(PxConstraintInfo *, PxU32) override {}
	void onWake(PxActor **, PxU32) override {}
	void onSleep(PxActor **, PxU32) override {}
	void onAdvance(const PxRigidBody *const *, const PxTransform *, const PxU32) override {}

private:
	PxPhysicsEnvironment *m_env;
};

/* ------------------------------------------------------------------------- */

PxPhysicsEnvironment::PxPhysicsEnvironment(bool useGpu, PHY_IPhysicsEnvironment *culling)
    : m_scene(NULL),
      m_gpu(false),
      m_events(NULL),
      m_nextConstraintId(1),
      m_controllerManager(NULL),
      m_culling(culling),
      m_gravity(0.0f, 0.0f, -9.81f),
      m_numTimeSubSteps(1),
      m_numIterations(10),
      m_debugMode(0),
      m_deactivationTime(2.0f),
      m_linearSleep(0.8f),
      m_angularSleep(1.0f),
      m_lastStep(1.0f / 60.0f),
      m_accumulated(0.0),
      m_ccdMode(0)
{
	for (int i = 0; i < PHY_NUM_RESPONSE; i++) {
		m_triggerCallbacks[i] = NULL;
		m_triggerCallbacksUserPtrs[i] = NULL;
	}
	InitScene(useGpu);
}

void PxPhysicsEnvironment::InitScene(bool useGpu)
{
	PxSystem *sys = PxSystem::Get();
	if (!sys) {
		return;
	}
	m_events = new EventCallback(this);
	PxSceneDesc desc(sys->Physics()->getTolerancesScale());
	desc.gravity = m_gravity;
	desc.cpuDispatcher = sys->Dispatcher();
	desc.filterShader = filter_shader;
	desc.simulationEventCallback = m_events;
	desc.kineKineFilteringMode = PxPairFilteringMode::eKEEP;
	desc.staticKineFilteringMode = PxPairFilteringMode::eKEEP;
	desc.flags |= PxSceneFlag::eENABLE_CCD | PxSceneFlag::eENABLE_PCM;
	desc.solverType = PxSolverType::eTGS;
	desc.wakeCounterResetValue = m_deactivationTime;
	if (useGpu) {
		std::string reason;
		PxCudaContextManager *cuda = sys->Cuda(&reason);
		if (cuda) {
			desc.cudaContextManager = cuda;
			desc.flags |= PxSceneFlag::eENABLE_GPU_DYNAMICS;
			desc.broadPhaseType = PxBroadPhaseType::eGPU;
			m_gpu = true;
		}
		else {
			printf("BGE-Remix PhysX: the scene asks for the GPU, it runs on the CPU (%s)\n", reason.c_str());
			fflush(stdout);
		}
	}
	m_scene = sys->Physics()->createScene(desc);
	if (!m_scene && m_gpu) {
		/* the GPU scene could not be made: CPU */
		printf("BGE-Remix PhysX: the GPU scene could not be made, the scene runs on the CPU\n");
		desc.cudaContextManager = NULL;
		desc.flags &= ~PxSceneFlag::eENABLE_GPU_DYNAMICS;
		desc.broadPhaseType = PxBroadPhaseType::ePABP;
		m_gpu = false;
		m_scene = sys->Physics()->createScene(desc);
	}
	if (m_scene) {
		m_scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, 0.0f);
	}
}

PxPhysicsEnvironment::~PxPhysicsEnvironment()
{
	for (PxRaycastVehicle *v : m_vehicles) {
		delete v;
	}
	m_vehicles.clear();
	while (!m_constraints.empty()) {
		RemoveConstraint(m_constraints.begin()->second);
	}
	/* controllers are deleted by their game objects; take them out first */
	std::set<PxPhysicsController *> left = m_controllers;
	for (PxPhysicsController *c : left) {
		RemoveController(c);
	}
	if (m_controllerManager) {
		m_controllerManager->release();
	}
	if (m_scene) {
		m_scene->release();
	}
	delete m_events;
	delete m_culling;
}

/* ------------------------------------------------------------------------- */
/* controllers in the scene */

void PxPhysicsEnvironment::AddController(PxPhysicsController *ctrl)
{
	if (!ctrl || !m_scene || !m_controllers.insert(ctrl).second) {
		return;
	}
	ctrl->m_inScene = true;
	if (ctrl->GetDeformable()) {
		ctrl->GetDeformable()->AddToScene(m_scene);
	}
	else if (PxCharacter *ch = ctrl->GetCharacter()) {
		ch->AddToScene();
		ctrl->m_actor = ch->Actor();
		ctrl->UpdateFilter();
	}
	else if (PxRigidActor *actor = ctrl->GetActor()) {
		if (!actor->getScene()) {
			m_scene->addActor(*actor);
		}
		if (PxRigidDynamic *body = ctrl->GetDynamic()) {
			if (!ctrl->IsKinematic()) {
				body->setSolverIterationCounts((PxU32)std::max(1, std::min(m_numIterations, 32)), 1);
				body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, m_ccdMode != 0);
			}
		}
	}
	if (ctrl->IsKinematic()) {
		m_kinematics.insert(ctrl);
	}
	m_stats.actors = (int)m_controllers.size();
}

bool PxPhysicsEnvironment::RemoveController(PxPhysicsController *ctrl)
{
	if (!ctrl || !m_controllers.erase(ctrl)) {
		return false;
	}
	ctrl->m_inScene = false;
	m_kinematics.erase(ctrl);
	ForgetPairs(ctrl);
	/* its joints go with it */
	std::vector<int> ids = ctrl->m_constraints;
	for (int id : ids) {
		RemoveConstraintById(id);
	}
	for (size_t i = 0; i < m_vehicles.size();) {
		if (m_vehicles[i]->Chassis() == ctrl) {
			delete m_vehicles[i];
			m_vehicles.erase(m_vehicles.begin() + i);
		}
		else {
			i++;
		}
	}
	if (ctrl->GetDeformable()) {
		ctrl->GetDeformable()->RemoveFromScene(m_scene);
	}
	else if (PxCharacter *ch = ctrl->GetCharacter()) {
		ch->RemoveFromScene();
		ctrl->m_actor = NULL;
	}
	else {
		PxRigidActor *actor = ctrl->GetActor();
		if (actor && actor->getScene() == m_scene) {
			m_scene->removeActor(*actor);
		}
	}
	m_stats.actors = (int)m_controllers.size();
	return true;
}

bool PxPhysicsEnvironment::IsActiveController(PxPhysicsController *ctrl) const
{
	return m_controllers.count(ctrl) != 0;
}

void PxPhysicsEnvironment::ForgetPairs(PxPhysicsController *ctrl)
{
	for (auto it = m_touching.begin(); it != m_touching.end();) {
		if (it->first.first == ctrl || it->first.second == ctrl) {
			it = m_touching.erase(it);
		}
		else {
			++it;
		}
	}
}

void PxPhysicsEnvironment::RebindJoints(PxPhysicsController *ctrl)
{
	for (int id : ctrl->m_constraints) {
		Constraint *c = GetConstraint(id);
		if (c && c->joint) {
			c->joint->setActors(c->c0 ? c->c0->GetActor() : NULL, c->c1 ? c->c1->GetActor() : NULL);
		}
	}
}

PxControllerManager *PxPhysicsEnvironment::ControllerManager()
{
	if (!m_controllerManager && m_scene) {
		m_controllerManager = PxCreateControllerManager(*m_scene);
	}
	return m_controllerManager;
}

PxPhysicsController *PxPhysicsEnvironment::CreateController(const PxCtrlInfo &info, PHY_IMotionState *motionState,
                                                           bool addToScene)
{
	PxPhysicsController *ctrl = new PxPhysicsController(info, motionState, this);
	if (addToScene) {
		AddController(ctrl);
	}
	return ctrl;
}

/* ------------------------------------------------------------------------- */
/* touching pairs: from found to lost, reported every frame (also while the
 * bodies sleep, as Bullet keeps its contact manifolds) */

void PxPhysicsEnvironment::ContactFound(PxPhysicsController *a, PxPhysicsController *b, const MT_Vector3 &pa,
                                        const MT_Vector3 &pb, const MT_Vector3 &normal)
{
	/* near and radar sensors: the property filter, when the pair begins */
	std::pair<PxPhysicsController *, PxPhysicsController *> key(std::min(a, b), std::max(a, b));
	std::map<std::pair<PxPhysicsController *, PxPhysicsController *>, Touch>::iterator it = m_touching.find(key);
	if (it == m_touching.end()) {
		PxPhysicsController *sensor = a->IsSensor() ? a : (b->IsSensor() ? b : NULL);
		if (sensor && m_triggerCallbacks[PHY_BROADPH_RESPONSE]) {
			PxPhysicsController *other = sensor == a ? b : a;
			if (!m_triggerCallbacks[PHY_BROADPH_RESPONSE](m_triggerCallbacksUserPtrs[PHY_BROADPH_RESPONSE], sensor,
			                                              other, NULL))
			{
				return;
			}
		}
	}
	Touch &t = m_touching[key];
	t.a = a;
	t.b = b;
	t.point1 = pa;
	t.point2 = pb;
	t.normal = normal;
}

void PxPhysicsEnvironment::ContactLost(PxPhysicsController *a, PxPhysicsController *b)
{
	m_touching.erase(std::make_pair(std::min(a, b), std::max(a, b)));
}

void PxPhysicsEnvironment::CallbackTriggers()
{
	PHY_ResponseCallback cb = m_triggerCallbacks[PHY_OBJECT_RESPONSE];
	if (!cb) {
		return;
	}
	/* a copy: callbacks may remove objects */
	std::vector<Touch> pairs;
	pairs.reserve(m_touching.size());
	for (auto &kv : m_touching) {
		pairs.push_back(kv.second);
	}
	for (const Touch &t : pairs) {
		if (!IsActiveController(t.a) || !IsActiveController(t.b)) {
			continue;
		}
		const bool first = t.a->Registered();
		if (!first && !t.b->Registered()) {
			continue;
		}
		PHY_CollData data;
		/* point1 on the object reported on, the normal from the other object towards it (Bullet: normal on B) */
		data.m_point1 = first ? t.point1 : t.point2;
		data.m_point2 = first ? t.point2 : t.point1;
		data.m_normal = first ? t.normal : -t.normal;
		cb(m_triggerCallbacksUserPtrs[PHY_OBJECT_RESPONSE], t.a, t.b, &data);
	}
}

/* ------------------------------------------------------------------------- */
/* stepping */

void PxPhysicsEnvironment::SimulateOnce(float dt)
{
	for (PxPhysicsController *c : m_kinematics) {
		c->PullKinematic(dt);
	}
	for (PxRaycastVehicle *v : m_vehicles) {
		v->UpdateVehicle(dt);
	}
	for (PxPhysicsController *c : m_controllers) {
		if (PxCharacter *ch = c->GetCharacter()) {
			ch->Step(dt);
		}
	}
	m_scene->simulate(dt);
	m_scene->fetchResults(true);
	for (PxPhysicsController *c : m_controllers) {
		c->SimulationTick(dt);
	}
	Deactivate(dt);
	m_stats.steps++;
}

bool PxPhysicsEnvironment::ProceedDeltaTime(double curTime, float timeStep, float interval)
{
	if (!m_scene) {
		return false;
	}
	const auto t0 = std::chrono::steady_clock::now();
	const float subStep = timeStep / (float)std::max(m_numTimeSubSteps, 1);
	if (subStep <= 0.0f) {
		return false;
	}
	m_lastStep = subStep;
	/* as Bullet's stepSimulation(interval, 25, subStep): whole sub-steps, the
	 * rest of the time kept for the next frame */
	m_accumulated += interval;
	int steps = (int)floor(m_accumulated / subStep + 1e-4);
	if (steps > 25) {
		steps = 25;
		m_accumulated = 0.0;
	}
	else {
		m_accumulated -= steps * subStep;
		if (m_accumulated < 0.0) {
			m_accumulated = 0.0;
		}
	}
	for (int i = 0; i < steps; i++) {
		SimulateOnce(subStep);
	}
	if (steps > 0) {
		ProcessFhSprings(steps * subStep);
	}
	for (PxPhysicsController *c : m_controllers) {
		c->SynchronizeMotionStates(timeStep);
	}
	for (PxRaycastVehicle *v : m_vehicles) {
		v->SyncWheels();
	}
	CallbackTriggers();
	m_stats.simulateMs =
	        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	(void)curTime;
	return true;
}

/* Blender's Fh springs (Material physics: Distance, Force, Damping, Align to
 * Normal), as Bullet's ProcessFhSprings */
void PxPhysicsEnvironment::ProcessFhSprings(float interval)
{
	const int numIter = (int)(interval * (KX_KetsjiEngine::GetTicRate() + 0.001f));
	for (PxPhysicsController *ctrl : m_controllers) {
		const PxCtrlInfo &ci = ctrl->GetInfo();
		if (!ci.doFh && !ci.doRotFh) {
			continue;
		}
		PxRigidDynamic *body = ctrl->GetDynamic();
		if (!body || ctrl->IsKinematic()) {
			continue;
		}
		PxPhysicsController *parentCtrl = ctrl->GetParentCtrl();
		PxRigidDynamic *parentBody = parentCtrl ? parentCtrl->GetDynamic() : NULL;
		PxRigidDynamic *cl = parentBody ? parentBody : body;

		const PxVec3 rayDir(0.0f, 0.0f, -10.0f);
		const PxVec3 from = body->getGlobalPose().p;
		struct NotMe : public PxQueryFilterCallback {
			const PxRigidActor *a, *b;
			PxQueryHitType::Enum preFilter(const PxFilterData &, const PxShape *shape, const PxRigidActor *actor,
			                               PxHitFlags &) override
			{
				if (actor == a || actor == b || shape->getFlags().isSet(PxShapeFlag::eTRIGGER_SHAPE)) {
					return PxQueryHitType::eNONE;
				}
				return PxQueryHitType::eBLOCK;
			}
			PxQueryHitType::Enum postFilter(const PxFilterData &, const PxQueryHit &, const PxShape *,
			                                const PxRigidActor *) override
			{
				return PxQueryHitType::eBLOCK;
			}
		} notMe;
		notMe.a = body;
		notMe.b = parentBody;
		PxRaycastBuffer hit;
		PxQueryFilterData fd(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
		if (!m_scene->raycast(from, rayDir.getNormalized(), rayDir.magnitude(), hit, PxHitFlag::eDEFAULT | PxHitFlag::eMESH_BOTH_SIDES, fd, &notMe) ||
		    !hit.hasBlock)
		{
			continue;
		}
		PxPhysicsController *controller = (PxPhysicsController *)hit.block.actor->userData;
		if (!controller) {
			continue;
		}
		const PxCtrlInfo &hp = controller->GetInfo();
		if (hp.fhDistance < 1e-6f) {
			continue;
		}
		PxRigidDynamic *hitBody = controller->GetDynamic();
		const float distance = hit.block.distance - ci.radius;
		if (distance >= hp.fhDistance) {
			continue;
		}
		const PxVec3 ray_dir = rayDir.getNormalized();
		PxVec3 normal = hit.block.normal.getNormalized();
		const float invMass = cl->getInvMass();
		for (int i = 0; i < numIter; i++) {
			if (ci.doFh) {
				const PxVec3 hitVel = (hitBody && !controller->IsKinematic()) ?
				                              PxRigidBodyExt::getVelocityAtPos(*hitBody, hit.block.position) :
				                              PxVec3(0.0f);
				const PxVec3 rel_vel = cl->getLinearVelocity() - hitVel;
				const float rel_vel_ray = ray_dir.dot(rel_vel);
				const float spring_extent = 1.0f - distance / hp.fhDistance;
				const float i_spring = spring_extent * hp.fhSpring;
				const float i_damp = rel_vel_ray * hp.fhDamping;
				cl->setLinearVelocity(cl->getLinearVelocity() + (-(i_spring + i_damp) * ray_dir));
				if (hp.fhNormal) {
					cl->setLinearVelocity(cl->getLinearVelocity() +
					                      (i_spring + i_damp) * (normal - normal.dot(ray_dir) * ray_dir));
				}
				const PxVec3 lateral = rel_vel - rel_vel_ray * ray_dir;
				const float rel_vel_lateral = lateral.magnitude();
				if (rel_vel_lateral > 1e-6f && invMass > 0.0f) {
					const float friction_factor = hp.friction;
					const float max_friction = friction_factor * std::max(0.0f, i_spring);
					const float rel_mom_lateral = rel_vel_lateral / invMass;
					const PxVec3 friction = (rel_mom_lateral > max_friction) ?
					                                -lateral * (max_friction / rel_vel_lateral) :
					                                -lateral;
					cl->addForce(friction, PxForceMode::eIMPULSE);
				}
			}
			if (ci.doRotFh) {
				const PxVec3 up2 = PxMat33(cl->getGlobalPose().q).column2;
				const PxVec3 t_spring = up2.cross(normal) * hp.fhSpring;
				PxVec3 ang_vel = cl->getAngularVelocity();
				ang_vel -= ang_vel.dot(normal) * normal;
				const PxVec3 t_damp = ang_vel * hp.fhDamping;
				cl->setAngularVelocity(cl->getAngularVelocity() + (t_spring - t_damp));
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* settings */

void PxPhysicsEnvironment::SetDebugMode(int debugMode)
{
	m_debugMode = debugMode;
	if (!m_scene) {
		return;
	}
	const float on = debugMode > 0 ? 1.0f : 0.0f;
	m_scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, on);
	m_scene->setVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, on);
	m_scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LOCAL_FRAMES, on * 0.5f);
	m_scene->setVisualizationParameter(PxVisualizationParameter::eJOINT_LIMITS, on);
	m_scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_POINT, on);
	m_scene->setVisualizationParameter(PxVisualizationParameter::eCONTACT_NORMAL, on * 0.3f);
}

void PxPhysicsEnvironment::DebugDrawWorld()
{
	if (m_debugMode <= 0 || !m_scene) {
		return;
	}
	const PxRenderBuffer &rb = m_scene->getRenderBuffer();
	for (PxU32 i = 0; i < rb.getNbLines(); i++) {
		const PxDebugLine &l = rb.getLines()[i];
		const PxU32 c = l.color0;
		MT_Vector3 color(((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, (c & 0xff) / 255.0f);
		KX_RasterizerDrawDebugLine(mt(l.pos0), mt(l.pos1), color);
	}
	for (PxU32 i = 0; i < rb.getNbTriangles(); i++) {
		const PxDebugTriangle &t = rb.getTriangles()[i];
		const MT_Vector3 color(0.5f, 0.5f, 1.0f);
		KX_RasterizerDrawDebugLine(mt(t.pos0), mt(t.pos1), color);
		KX_RasterizerDrawDebugLine(mt(t.pos1), mt(t.pos2), color);
		KX_RasterizerDrawDebugLine(mt(t.pos2), mt(t.pos0), color);
	}
}

void PxPhysicsEnvironment::SetNumIterations(int numIter)
{
	m_numIterations = numIter > 0 ? numIter : 1;
	for (PxPhysicsController *c : m_controllers) {
		if (PxRigidDynamic *body = c->GetDynamic()) {
			body->setSolverIterationCounts((PxU32)std::max(1, std::min(m_numIterations, 32)), 1);
		}
	}
}

void PxPhysicsEnvironment::SetDeactivationTime(float dTime)
{
	/* Bullet: 0 turns sleeping off (Blender's default); otherwise PhysX's
	 * own wake counter (2 s) is used */
	m_deactivationTime = dTime;
	UpdateSleeping();
}

void PxPhysicsEnvironment::Deactivate(float dt)
{
	if (m_deactivationTime <= 0.0f) {
		return;
	}
	const float lin2 = m_linearSleep * m_linearSleep, ang2 = m_angularSleep * m_angularSleep;
	std::vector<PxPhysicsController *> ready;
	for (PxPhysicsController *c : m_controllers) {
		PxRigidDynamic *body = c->GetDynamic();
		if (!body || c->IsKinematic() || c->GetInfo().disableSleep || !c->GetInfo().dyna || !body->getScene()) {
			continue;
		}
		if (body->isSleeping()) {
			continue;
		}
		if (c->m_slept) {
			/* woken (a touch, a joint, the game): Bullet's activate() starts over */
			c->m_slept = false;
			c->m_sleepTime = 0.0f;
		}
		if (body->getLinearVelocity().magnitudeSquared() < lin2 && body->getAngularVelocity().magnitudeSquared() < ang2) {
			c->m_sleepTime += dt;
			if (c->m_sleepTime > m_deactivationTime) {
				ready.push_back(c);
			}
		}
		else {
			c->m_sleepTime = 0.0f;
		}
	}
	/* the bodies joined to each by joints must be ready too */
	std::set<PxPhysicsController *> done;
	for (PxPhysicsController *c : ready) {
		if (done.count(c)) {
			continue;
		}
		std::vector<PxPhysicsController *> group(1, c), todo(1, c);
		std::set<PxPhysicsController *> seen;
		seen.insert(c);
		bool all = true;
		while (!todo.empty() && all) {
			PxPhysicsController *x = todo.back();
			todo.pop_back();
			for (int id : x->m_constraints) {
				Constraint *k = GetConstraint(id);
				if (!k) {
					continue;
				}
				for (PxPhysicsController *o : {k->c0, k->c1}) {
					if (!o || seen.count(o) || !o->GetDynamic() || o->IsKinematic()) {
						continue;
					}
					seen.insert(o);
					if (o->GetDynamic()->isSleeping()) {
						continue;
					}
					if (o->m_sleepTime <= m_deactivationTime) {
						all = false;
						break;
					}
					group.push_back(o);
					todo.push_back(o);
				}
				if (!all) {
					break;
				}
			}
		}
		for (PxPhysicsController *g : group) {
			done.insert(g);
		}
		if (!all) {
			continue;
		}
		for (PxPhysicsController *g : group) {
			g->GetDynamic()->putToSleep();
			g->m_slept = true;
		}
	}
}

void PxPhysicsEnvironment::UpdateSleeping()
{
	for (PxPhysicsController *c : m_controllers) {
		if (PxRigidDynamic *body = c->GetDynamic()) {
			if (!c->IsKinematic() && !c->GetInfo().disableSleep) {
				body->setSleepThreshold(SleepEnergy());
			}
		}
	}
}

void PxPhysicsEnvironment::SetDeactivationLinearTreshold(float linTresh)
{
	m_linearSleep = linTresh;
	UpdateSleeping();
}

void PxPhysicsEnvironment::SetDeactivationAngularTreshold(float angTresh)
{
	m_angularSleep = angTresh;
}

void PxPhysicsEnvironment::SetCcdMode(int ccdMode)
{
	m_ccdMode = ccdMode;
	for (PxPhysicsController *c : m_controllers) {
		if (PxRigidDynamic *body = c->GetDynamic()) {
			if (!c->IsKinematic()) {
				body->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, ccdMode != 0);
			}
		}
	}
}

void PxPhysicsEnvironment::SetGravity(float x, float y, float z)
{
	m_gravity = PxVec3(x, y, z);
	if (m_scene) {
		m_scene->setGravity(m_gravity);
	}
}

void PxPhysicsEnvironment::GetGravity(MT_Vector3 &grav)
{
	grav = mt(m_gravity);
}

/* ------------------------------------------------------------------------- */
/* rays */

namespace {

class RayFilter : public PxQueryFilterCallback
{
public:
	RayFilter(PHY_IRayCastFilterCallback &cb) : m_cb(cb) {}

	PxQueryHitType::Enum preFilter(const PxFilterData &, const PxShape *, const PxRigidActor *actor,
	                               PxHitFlags &) override
	{
		PxPhysicsController *ctrl = (PxPhysicsController *)actor->userData;
		if (!ctrl) {
			return PxQueryHitType::eNONE;
		}
		/* as Bullet: never sensors (near, radar, Sensor objects) */
		if (ctrl->GetInfo().filterGroup & PXF_SENSOR) {
			return PxQueryHitType::eNONE;
		}
		if (ctrl == m_cb.m_ignoreController) {
			return PxQueryHitType::eNONE;
		}
		return m_cb.needBroadphaseRayCast(ctrl) ? PxQueryHitType::eBLOCK : PxQueryHitType::eNONE;
	}
	PxQueryHitType::Enum postFilter(const PxFilterData &, const PxQueryHit &, const PxShape *,
	                                const PxRigidActor *) override
	{
		return PxQueryHitType::eBLOCK;
	}

private:
	PHY_IRayCastFilterCallback &m_cb;
};

}  /* namespace */

PHY_IPhysicsController *PxPhysicsEnvironment::RayTest(PHY_IRayCastFilterCallback &filterCallback, float fromX,
                                                      float fromY, float fromZ, float toX, float toY, float toZ)
{
	if (!m_scene) {
		return NULL;
	}
	const PxVec3 from(fromX, fromY, fromZ), to(toX, toY, toZ);
	PxVec3 dir = to - from;
	const float len = dir.magnitude();
	if (len < 1e-7f) {
		return NULL;
	}
	dir /= len;
	RayFilter filter(filterCallback);
	PxRaycastBuffer buf;
	PxQueryFilterData fd(PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
	const PxHitFlags flags = PxHitFlag::ePOSITION | PxHitFlag::eNORMAL | PxHitFlag::eFACE_INDEX | PxHitFlag::eUV |
	                         PxHitFlag::eMESH_BOTH_SIDES;  /* as Bullet: both sides of triangles */
	if (!m_scene->raycast(from, dir, len, buf, flags, fd, &filter) || !buf.hasBlock) {
		return NULL;
	}
	const PxRaycastHit &hit = buf.block;
	PxPhysicsController *controller = (PxPhysicsController *)hit.actor->userData;

	PHY_RayCastResult result;
	memset(&result, 0, sizeof(result));
	result.m_controller = controller;
	result.m_hitPoint = mt(hit.position);
	PxVec3 normal = hit.normal;

	const int tri = PxShapeDesc::OriginalTriangle(hit.shape, hit.faceIndex);
	if (tri >= 0) {
		const PxShapeDesc *desc = (const PxShapeDesc *)hit.shape->userData;
		CcdShapeConstructionInfo *mesh = desc->m_mesh;
		result.m_meshObject = mesh->GetMesh();
		result.m_polygon = mesh->m_polygonIndexArray[tri];
		if (filterCallback.m_faceUV && 3 * tri + 2 < (int)mesh->m_triFaceUVcoArray.size()) {
			const CcdShapeConstructionInfo::UVco *uv = &mesh->m_triFaceUVcoArray[3 * tri];
			const float w0 = 1.0f - hit.u - hit.v, w1 = hit.u, w2 = hit.v;
			result.m_hitUV[0] = w0 * uv[0].uv[0] + w1 * uv[1].uv[0] + w2 * uv[2].uv[0];
			result.m_hitUV[1] = w0 * uv[0].uv[1] + w1 * uv[1].uv[1] + w2 * uv[2].uv[1];
			result.m_hitUVOK = 1;
		}
		if (filterCallback.m_faceNormal) {
			/* the triangle's own normal, not turned towards the ray */
			const PxTriangleMeshGeometry &g = static_cast<const PxTriangleMeshGeometry &>(hit.shape->getGeometry());
			PxTriangle t;
			PxMeshQuery::getTriangle(g, PxShapeExt::getGlobalPose(*hit.shape, *hit.actor), hit.faceIndex, t);
			normal = (t.verts[1] - t.verts[0]).cross(t.verts[2] - t.verts[0]);
		}
	}
	if (normal.magnitudeSquared() > 1e-12f) {
		normal.normalize();
	}
	else {
		normal = PxVec3(1.0f, 0.0f, 0.0f);
	}
	result.m_hitNormal = mt(normal);
	filterCallback.reportHit(&result);
	return result.m_controller;
}

bool PxPhysicsEnvironment::CullingTest(PHY_CullingCallback callback, void *userData, MT_Vector4 *planeNormals,
                                       int planeNumber, int occlusionRes, const int *viewport, float modelview[16],
                                       float projection[16])
{
	return m_culling ? m_culling->CullingTest(callback, userData, planeNormals, planeNumber, occlusionRes, viewport,
	                                          modelview, projection) :
	                   false;
}

/* ------------------------------------------------------------------------- */
/* sensors */

void PxPhysicsEnvironment::AddSensor(PHY_IPhysicsController *ctrl)
{
	AddController(static_cast<PxPhysicsController *>(ctrl));
}

void PxPhysicsEnvironment::RemoveSensor(PHY_IPhysicsController *ctrl)
{
	RemoveController(static_cast<PxPhysicsController *>(ctrl));
}

void PxPhysicsEnvironment::AddTouchCallback(int response_class, PHY_ResponseCallback callback, void *user)
{
	m_triggerCallbacks[response_class] = callback;
	m_triggerCallbacksUserPtrs[response_class] = user;
}

bool PxPhysicsEnvironment::RequestCollisionCallback(PHY_IPhysicsController *ctrl)
{
	PxPhysicsController *c = static_cast<PxPhysicsController *>(ctrl);
	const bool first = c->Register();
	if (first) {
		c->UpdateFilter();
	}
	return first;
}

bool PxPhysicsEnvironment::RemoveCollisionCallback(PHY_IPhysicsController *ctrl)
{
	PxPhysicsController *c = static_cast<PxPhysicsController *>(ctrl);
	const bool last = c->Unregister();
	if (last) {
		c->UpdateFilter();
	}
	return last;
}

PHY_IPhysicsController *PxPhysicsEnvironment::CreateSphereController(float radius, const MT_Vector3 &position)
{
	PxCtrlInfo info;
	PxShapeDesc *shape = new PxShapeDesc();
	shape->m_type = PHY_SHAPE_SPHERE;
	shape->m_radius = radius;
	info.shape = shape;
	info.sensor = true;
	info.noResponse = true;
	info.filterGroup = PXF_SENSOR;
	info.filterMask = PXF_ALL ^ PXF_SENSOR;
	info.radius = radius;
	PxDefaultMotionState *ms = new PxDefaultMotionState();
	ms->SetWorldPosition((float)position.x(), (float)position.y(), (float)position.z());
	PxPhysicsController *ctrl = new PxPhysicsController(info, ms, this);
	shape->Release();
	return ctrl;
}

PHY_IPhysicsController *PxPhysicsEnvironment::CreateConeController(float coneradius, float coneheight)
{
	PxCtrlInfo info;
	PxShapeDesc *shape = new PxShapeDesc();
	shape->m_type = PHY_SHAPE_CONE;
	shape->m_radius = coneradius;
	shape->m_height = coneheight;
	shape->m_coneAxisY = true;  /* Bullet's btConeShape points along Y */
	info.shape = shape;
	info.sensor = true;
	info.noResponse = true;
	info.filterGroup = PXF_SENSOR;
	info.filterMask = PXF_ALL ^ PXF_SENSOR;
	PxPhysicsController *ctrl = new PxPhysicsController(info, new PxDefaultMotionState(), this);
	shape->Release();
	return ctrl;
}

/* ------------------------------------------------------------------------- */
/* joints */

PxPhysicsEnvironment::Constraint *PxPhysicsEnvironment::GetConstraint(int id)
{
	std::map<int, Constraint *>::iterator it = m_constraints.find(id);
	return it == m_constraints.end() ? NULL : it->second;
}

void PxPhysicsEnvironment::RemoveConstraint(Constraint *c)
{
	if (!c) {
		return;
	}
	if (c->joint) {
		PxRigidActor *a0, *a1;
		c->joint->getActors(a0, a1);
		if (a0 && a0->is<PxRigidDynamic>() && a0->getScene() &&
		    !(static_cast<PxRigidDynamic *>(a0)->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
		{
			static_cast<PxRigidDynamic *>(a0)->wakeUp();
		}
		if (a1 && a1->is<PxRigidDynamic>() && a1->getScene() &&
		    !(static_cast<PxRigidDynamic *>(a1)->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
		{
			static_cast<PxRigidDynamic *>(a1)->wakeUp();
		}
		c->joint->release();
	}
	for (PxPhysicsController *ctrl : {c->c0, c->c1}) {
		if (ctrl) {
			std::vector<int> &ids = ctrl->m_constraints;
			ids.erase(std::remove(ids.begin(), ids.end(), c->id), ids.end());
		}
	}
	m_constraints.erase(c->id);
	delete c;
}

void PxPhysicsEnvironment::RemoveConstraintById(int constraintid)
{
	if (constraintid == 0) {
		return;
	}
	if (Constraint *c = GetConstraint(constraintid)) {
		RemoveConstraint(c);
		return;
	}
	for (size_t i = 0; i < m_vehicles.size(); i++) {
		if (m_vehicles[i]->GetUserConstraintId() == constraintid) {
			delete m_vehicles[i];
			m_vehicles.erase(m_vehicles.begin() + i);
			return;
		}
	}
}

static PxTransform frame_of(const PxVec3 &pivot, const PxVec3 &x, const PxVec3 &y, const PxVec3 &z)
{
	return PxTransform(pivot, PxQuat(PxMat33(x, y, z)).getNormalized());
}

/* Bullet's btPlaneSpace1: two axes perpendicular to n */
static void plane_space(const PxVec3 &n, PxVec3 &p, PxVec3 &q)
{
	if (fabsf(n.z) > 0.70710678f) {
		const float a = n.y * n.y + n.z * n.z;
		const float k = 1.0f / sqrtf(a);
		p = PxVec3(0.0f, -n.z * k, n.y * k);
		q = PxVec3(a * k, -n.x * p.z, n.x * p.y);
	}
	else {
		const float a = n.x * n.x + n.y * n.y;
		const float k = 1.0f / sqrtf(a);
		p = PxVec3(-n.y * k, n.x * k, 0.0f);
		q = PxVec3(-n.z * p.y, n.z * p.x, a * k);
	}
}

PxJoint *PxPhysicsEnvironment::MakeJoint(PxPhysicsController *c0, PxPhysicsController *c1, PHY_ConstraintType type,
                                         const PxTransform &frameA, const PxTransform &frameB)
{
	PxPhysics &physics = *PxSystem::Get()->Physics();
	PxRigidActor *a0 = c0->GetActor();
	PxRigidActor *a1 = c1 ? c1->GetActor() : NULL;
	switch (type) {
		case PHY_POINT2POINT_CONSTRAINT:
			return PxSphericalJointCreate(physics, a0, frameA, a1, frameB);
		case PHY_LINEHINGE_CONSTRAINT:
			return PxRevoluteJointCreate(physics, a0, frameA, a1, frameB);
		case PHY_ANGULAR_CONSTRAINT: {
			/* a hinge without its ball socket: only the turn about the axis */
			PxD6Joint *d6 = PxD6JointCreate(physics, a0, frameA, a1, frameB);
			d6->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
			d6->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
			d6->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
			d6->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
			return d6;
		}
		case PHY_CONE_TWIST_CONSTRAINT: {
			PxD6Joint *d6 = PxD6JointCreate(physics, a0, frameA, a1, frameB);
			d6->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
			d6->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
			d6->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
			return d6;
		}
		case PHY_GENERIC_6DOF_CONSTRAINT:
			/* Bullet's 6DOF starts with every axis locked */
			return PxD6JointCreate(physics, a0, frameA, a1, frameB);
		default:
			return NULL;
	}
}

int PxPhysicsEnvironment::CreateConstraint(PHY_IPhysicsController *ctrl0, PHY_IPhysicsController *ctrl1,
                                           PHY_ConstraintType type, float pivotX, float pivotY, float pivotZ,
                                           float axisX, float axisY, float axisZ, float axis1X, float axis1Y,
                                           float axis1Z, float axis2X, float axis2Y, float axis2Z, int flags)
{
	PxPhysicsController *c0 = static_cast<PxPhysicsController *>(ctrl0);
	PxPhysicsController *c1 = static_cast<PxPhysicsController *>(ctrl1);
	if (!c0 || !PxSystem::Get()) {
		return 0;
	}
	const PxVec3 pivotInA(pivotX, pivotY, pivotZ);

	/* soft bodies: the nearest point is attached */
	if (c0->GetDeformable() || (c1 && c1->GetDeformable())) {
		if (c0->GetDeformable() && c1 && c1->GetDeformable()) {
			return 0;
		}
		if (c0->GetDeformable()) {
			c0->GetDeformable()->Attach(pivotInA, c1, true);
		}
		else {
			const PxVec3 world = c0->GetActor() ? c0->GetActor()->getGlobalPose().transform(pivotInA) : pivotInA;
			c1->GetDeformable()->Attach(world, c0, false);
		}
		return 0;  /* can't remove soft body anchors (as Bullet) */
	}
	if (type == PHY_VEHICLE_CONSTRAINT) {
		if (!c0->GetDynamic()) {
			return 0;
		}
		PxRaycastVehicle *v = new PxRaycastVehicle(this, c0, m_nextConstraintId++);
		m_vehicles.push_back(v);
		return v->GetUserConstraintId();
	}

	PxRigidActor *a0 = c0->GetActor();
	if (!a0) {
		return 0;
	}
	const bool s0 = !c0->GetDynamic() || c0->IsKinematic();
	const bool s1 = !c1 || !c1->GetDynamic() || c1->IsKinematic();
	if (s0 && s1) {
		return 0;
	}
	if (type != PHY_POINT2POINT_CONSTRAINT && type != PHY_LINEHINGE_CONSTRAINT && type != PHY_ANGULAR_CONSTRAINT &&
	    type != PHY_CONE_TWIST_CONSTRAINT && type != PHY_GENERIC_6DOF_CONSTRAINT)
	{
		return 0;
	}
	if (!c1 && type != PHY_GENERIC_6DOF_CONSTRAINT && type != PHY_CONE_TWIST_CONSTRAINT &&
	    type != PHY_LINEHINGE_CONSTRAINT && type != PHY_ANGULAR_CONSTRAINT)
	{
		return 0;
	}

	/* the frame in A: X along the given axis (PhysX's twist and hinge axis;
	 * Bullet's hinge turns about its frame's Z, the same axis) */
	PxVec3 axisInA(axisX, axisY, axisZ);
	if (axisInA.magnitudeSquared() < 1e-12f) {
		axisInA = PxVec3(1.0f, 0.0f, 0.0f);
	}
	PxVec3 ax1(axis1X, axis1Y, axis1Z), ax2(axis2X, axis2Y, axis2Z);
	/* Bullet keeps the axes' lengths (the object's scale) in its frame */
	const float linScale[3] = {axisInA.magnitude(), ax1.magnitudeSquared() > 1e-12f ? ax1.magnitude() : 1.0f,
	                           ax2.magnitudeSquared() > 1e-12f ? ax2.magnitude() : 1.0f};
	if (ax1.magnitudeSquared() < 1e-12f) {
		axisInA.normalize();
		plane_space(axisInA, ax1, ax2);
	}
	PxVec3 x = axisInA.getNormalized();
	PxVec3 y = ax1 - x * x.dot(ax1);
	y.normalizeSafe();
	if (y.magnitudeSquared() < 0.5f) {
		plane_space(x, y, ax2);
	}
	const PxVec3 z = x.cross(y);
	const PxTransform frameA = frame_of(pivotInA, x, y, z);
	const PxTransform globalA = a0->getGlobalPose() * frameA;
	PxTransform frameB = globalA;
	if (c1 && c1->GetActor()) {
		frameB = c1->GetActor()->getGlobalPose().getInverse() * globalA;
	}

	PxJoint *joint = MakeJoint(c0, c1, type, frameA, frameB);
	if (!joint) {
		return 0;
	}
	const bool disableCollision = (flags & PX_CONSTRAINT_DISABLE_LINKED_COLLISION) != 0;
	joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, !disableCollision);

	Constraint *c = new Constraint();
	memset(c, 0, sizeof(*c));
	c->id = m_nextConstraintId++;
	c->type = type;
	c->joint = joint;
	c->c0 = c0;
	c->c1 = c1;
	c->frameA = frameA;
	c->frameB = frameB;
	for (int i = 0; i < 3; i++) {
		c->linScale[i] = type == PHY_GENERIC_6DOF_CONSTRAINT ? std::max(linScale[i], 1e-6f) : 1.0f;
	}
	for (int i = 0; i < 6; i++) {
		if (type == PHY_GENERIC_6DOF_CONSTRAINT) {
			c->limits[i][0] = c->limits[i][1] = 0.0f;  /* locked */
		}
		else {
			c->limits[i][0] = 1.0f;  /* free */
			c->limits[i][1] = -1.0f;
		}
	}
	joint->userData = c;
	m_constraints[c->id] = c;
	c0->m_constraints.push_back(c->id);
	if (c1) {
		c1->m_constraints.push_back(c->id);
	}
	/* wake the bodies */
	for (PxPhysicsController *ctrl : {c0, c1}) {
		if (ctrl && ctrl->GetDynamic() && !ctrl->IsKinematic() && ctrl->m_inScene) {
			ctrl->GetDynamic()->wakeUp();
		}
	}
	return c->id;
}

/* the D6 motions and limits from the stored limits (low > high: free,
 * low == high: locked) */
static void apply_d6(PxD6Joint *d6, const float lim[6][2], bool coneTwist)
{
	const PxD6Axis::Enum lin[3] = {PxD6Axis::eX, PxD6Axis::eY, PxD6Axis::eZ};
	if (!coneTwist) {
		for (int i = 0; i < 3; i++) {
			const float lo = lim[i][0], hi = lim[i][1];
			if (lo > hi) {
				d6->setMotion(lin[i], PxD6Motion::eFREE);
			}
			else if (lo == hi) {
				d6->setMotion(lin[i], PxD6Motion::eLOCKED);
			}
			else {
				d6->setMotion(lin[i], PxD6Motion::eLIMITED);
				d6->setLinearLimit(lin[i], PxJointLinearLimitPair(lo, hi, PxSpring(0.0f, 0.0f)));
			}
		}
	}
	/* twist: X */
	{
		const float lo = lim[3][0], hi = lim[3][1];
		if (lo > hi) {
			d6->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
		}
		else if (lo == hi) {
			d6->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLOCKED);
		}
		else {
			d6->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);
			d6->setTwistLimit(PxJointAngularLimitPair(std::max(lo, -PxPi * 0.999f), std::min(hi, PxPi * 0.999f)));
		}
	}
	/* swings: Y and Z, a pyramid when limited */
	const float ylo = lim[4][0], yhi = lim[4][1], zlo = lim[5][0], zhi = lim[5][1];
	const bool yFree = ylo > yhi, zFree = zlo > zhi;
	const bool yLock = ylo == yhi, zLock = zlo == zhi;
	d6->setMotion(PxD6Axis::eSWING1, yFree ? PxD6Motion::eFREE : (yLock ? PxD6Motion::eLOCKED : PxD6Motion::eLIMITED));
	d6->setMotion(PxD6Axis::eSWING2, zFree ? PxD6Motion::eFREE : (zLock ? PxD6Motion::eLOCKED : PxD6Motion::eLIMITED));
	if (!yFree && !yLock && !zFree && !zLock) {
		if (coneTwist) {
			d6->setSwingLimit(PxJointLimitCone(std::min(yhi, PxPi * 0.999f), std::min(zhi, PxPi * 0.999f)));
		}
		else {
			d6->setPyramidSwingLimit(PxJointLimitPyramid(std::max(ylo, -PxPi * 0.999f), std::min(yhi, PxPi * 0.999f),
			                                             std::max(zlo, -PxPi * 0.999f), std::min(zhi, PxPi * 0.999f)));
		}
	}
	else if (!yFree && !yLock) {
		d6->setPyramidSwingLimit(PxJointLimitPyramid(std::max(ylo, -PxPi * 0.999f), std::min(yhi, PxPi * 0.999f),
		                                             -PxPi * 0.999f, PxPi * 0.999f));
	}
	else if (!zFree && !zLock) {
		d6->setPyramidSwingLimit(PxJointLimitPyramid(-PxPi * 0.999f, PxPi * 0.999f, std::max(zlo, -PxPi * 0.999f),
		                                             std::min(zhi, PxPi * 0.999f)));
	}
}

static void apply_drives(PxD6Joint *d6, const bool motor[6], const float vel[6], const float force[6],
                         const bool spring[6], const float stiffness[6], const float damping[6])
{
	const PxD6Drive::Enum drives[6] = {PxD6Drive::eX, PxD6Drive::eY, PxD6Drive::eZ,
	                                   PxD6Drive::eTWIST, PxD6Drive::eSWING1, PxD6Drive::eSWING2};
	PxVec3 lin(0.0f), ang(0.0f);
	for (int i = 0; i < 6; i++) {
		if (spring[i]) {
			d6->setDrive(drives[i], PxD6JointDrive(stiffness[i], damping[i], PX_MAX_F32, false));
		}
		else if (motor[i]) {
			/* a velocity motor: no stiffness, strong damping, limited force */
			d6->setDrive(drives[i], PxD6JointDrive(0.0f, 1e5f, force[i], false));
		}
		else {
			d6->setDrive(drives[i], PxD6JointDrive());
		}
		if (motor[i]) {
			if (i < 3) {
				lin[i] = vel[i];
			}
			else {
				ang[i - 3] = vel[i];
			}
		}
	}
	d6->setDriveVelocity(lin, ang, true);
}

void PxPhysicsEnvironment::SetConstraintParam(int constraintId, int param, float value0, float value1)
{
	Constraint *c = GetConstraint(constraintId);
	if (!c || !c->joint) {
		return;
	}
	switch (c->type) {
		case PHY_GENERIC_6DOF_CONSTRAINT: {
			PxD6Joint *d6 = c->joint->is<PxD6Joint>();
			if (!d6) {
				return;
			}
			if (param >= 0 && param <= 5) {
				c->limits[param][0] = value0;
				c->limits[param][1] = value1;
				float lim[6][2];
				memcpy(lim, c->limits, sizeof(lim));
				for (int i = 0; i < 3; i++) {
					if (lim[i][0] <= lim[i][1]) {
						lim[i][0] *= c->linScale[i];
						lim[i][1] *= c->linScale[i];
					}
				}
				apply_d6(d6, lim, false);
			}
			else if (param >= 6 && param <= 11) {
				const int i = param - 6;
				c->motor[i] = value1 > 0.0f;
				c->motorVelocity[i] = value0;
				c->motorForce[i] = value1;
				float vel[6];
				for (int i = 0; i < 6; i++) {
					vel[i] = i < 3 ? c->motorVelocity[i] / c->linScale[i] : c->motorVelocity[i];
				}
				apply_drives(d6, c->motor, vel, c->motorForce, c->spring, c->springStiffness, c->springDamping);
			}
			else if (param >= 12 && param <= 17) {
				const int i = param - 12;
				c->spring[i] = value0 != 0.0f;
				c->springStiffness[i] = value0;
				c->springDamping[i] = value1;
				if (c->spring[i]) {
					/* as Bullet's setEquilibriumPoint: the spring rests where it is now */
					d6->setDrivePosition(d6->getRelativeTransform(), true);
				}
				float vel[6];
				for (int i = 0; i < 6; i++) {
					vel[i] = i < 3 ? c->motorVelocity[i] / c->linScale[i] : c->motorVelocity[i];
				}
				apply_drives(d6, c->motor, vel, c->motorForce, c->spring, c->springStiffness, c->springDamping);
			}
			break;
		}
		case PHY_CONE_TWIST_CONSTRAINT: {
			PxD6Joint *d6 = c->joint->is<PxD6Joint>();
			if (!d6 || param < 3 || param > 5) {
				return;
			}
			/* Bullet: 3 twist span, 4 swing span 2 (Z), 5 swing span 1 (Y);
			 * the high value, below zero = no limit */
			const int slot = param == 3 ? 3 : (param == 4 ? 5 : 4);
			if (value1 < 0.0f) {
				c->limits[slot][0] = 1.0f;
				c->limits[slot][1] = -1.0f;
			}
			else {
				c->limits[slot][0] = -value1;
				c->limits[slot][1] = value1;
			}
			apply_d6(d6, c->limits, true);
			break;
		}
		case PHY_ANGULAR_CONSTRAINT:
		case PHY_LINEHINGE_CONSTRAINT: {
			if (param != 3) {
				return;
			}
			c->limits[3][0] = value0;
			c->limits[3][1] = value1;
			if (PxRevoluteJoint *rev = c->joint->is<PxRevoluteJoint>()) {
				if (value0 > value1) {
					rev->setRevoluteJointFlag(PxRevoluteJointFlag::eLIMIT_ENABLED, false);
				}
				else {
					rev->setLimit(PxJointAngularLimitPair(value0, value1));
					rev->setRevoluteJointFlag(PxRevoluteJointFlag::eLIMIT_ENABLED, true);
				}
			}
			else if (PxD6Joint *d6 = c->joint->is<PxD6Joint>()) {
				if (value0 > value1) {
					d6->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
				}
				else {
					d6->setMotion(PxD6Axis::eTWIST, PxD6Motion::eLIMITED);
					d6->setTwistLimit(PxJointAngularLimitPair(value0, value1));
				}
			}
			break;
		}
		default:
			break;
	}
}

float PxPhysicsEnvironment::GetConstraintParam(int constraintId, int param)
{
	Constraint *c = GetConstraint(constraintId);
	if (!c || !c->joint || c->type != PHY_GENERIC_6DOF_CONSTRAINT) {
		return 0.0f;
	}
	PxD6Joint *d6 = c->joint->is<PxD6Joint>();
	if (!d6) {
		return 0.0f;
	}
	if (param >= 0 && param <= 2) {
		return d6->getRelativeTransform().p[param] / c->linScale[param];
	}
	switch (param) {
		case 3:
			return d6->getTwistAngle();
		case 4:
			return d6->getSwingYAngle();
		case 5:
			return d6->getSwingZAngle();
		default:
			return 0.0f;
	}
}

float PxPhysicsEnvironment::GetAppliedImpulse(int constraintid)
{
	Constraint *c = GetConstraint(constraintid);
	if (!c || !c->joint) {
		return 0.0f;
	}
	PxVec3 force, torque;
	c->joint->getConstraint()->getForce(force, torque);
	return force.magnitude() * m_lastStep;
}

PHY_IVehicle *PxPhysicsEnvironment::GetVehicleConstraint(int constraintId)
{
	for (PxRaycastVehicle *v : m_vehicles) {
		if (v->GetUserConstraintId() == constraintId) {
			return v;
		}
	}
	return NULL;
}

void PxPhysicsEnvironment::ExportFile(const char *filename)
{
	printf("BGE-Remix PhysX: exporting the physics world (%s) is only available with Bullet\n", filename);
}

void PxPhysicsEnvironment::MergeEnvironment(PHY_IPhysicsEnvironment *other_env)
{
	PxPhysicsEnvironment *other = dynamic_cast<PxPhysicsEnvironment *>(other_env);
	if (!other) {
		printf("KX_Scene::MergeScene: Other scene is not using PhysX physics, not merging physics.\n");
		return;
	}
	while (!other->m_controllers.empty()) {
		PxPhysicsController *ctrl = *other->m_controllers.begin();
		other->RemoveController(ctrl);
		ctrl->m_env = this;
		AddController(ctrl);
	}
}

