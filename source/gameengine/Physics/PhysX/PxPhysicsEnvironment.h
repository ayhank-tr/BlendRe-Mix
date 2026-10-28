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

/** \file PxPhysicsEnvironment.h
 *  \ingroup physx
 *
 * BGE-Remix (P6): a game scene's physics on PhysX 5, chosen per scene in
 * World > Physics (CPU or GPU), next to Bullet's CcdPhysicsEnvironment and with
 * its behaviour: the same sensors and callbacks, rays, joints and their
 * parameters, vehicle, character, Fh springs. View culling keeps using
 * Bullet's tree (a culling-only CcdPhysicsEnvironment inside).
 */

#ifndef __PXPHYSICSENVIRONMENT_H__
#define __PXPHYSICSENVIRONMENT_H__

#include <map>
#include <set>
#include <string>
#include <vector>

#include "PHY_IPhysicsEnvironment.h"
#include "PHY_DynamicTypes.h"

#include "foundation/PxTransform.h"

class PxPhysicsController;
class PxRaycastVehicle;
class PxCharacter;
class PxDeformable;
class CcdPhysicsEnvironment;
class CcdGraphicController;
struct Scene;
struct PxCtrlInfo;

namespace physx {
class PxScene;
class PxJoint;
class PxControllerManager;
class PxShape;
class PxRigidActor;
}

class PxPhysicsEnvironment : public PHY_IPhysicsEnvironment
{
public:
	/* the scene's PhysX world; useGpu: rigid bodies on the GPU and GPU cloth
	 * and soft bodies (falls back to the CPU with a warning). NULL when PhysX
	 * cannot start. */
	static PxPhysicsEnvironment *Create(Scene *blenderscene, bool visualizePhysics, bool useGpu);
	/* without a Blender scene (tests); culling: the view culling tree (a
	 * CcdPhysicsEnvironment, owned) or NULL */
	PxPhysicsEnvironment(bool useGpu, PHY_IPhysicsEnvironment *culling);
	virtual ~PxPhysicsEnvironment();

	/* PHY_IPhysicsEnvironment */
	virtual void BeginFrame() {}
	virtual void EndFrame() {}
	virtual bool ProceedDeltaTime(double curTime, float timeStep, float interval);
	virtual void DebugDrawWorld();
	virtual void SetFixedTimeStep(bool useFixedTimeStep, float fixedTimeStep) {}
	virtual float GetFixedTimeStep() { return 0.0f; }
	virtual int GetDebugMode() const { return m_debugMode; }
	virtual void SetDebugMode(int debugMode);
	virtual void SetNumIterations(int numIter);
	virtual void SetNumTimeSubSteps(int numTimeSubSteps) { m_numTimeSubSteps = numTimeSubSteps > 0 ? numTimeSubSteps : 1; }
	virtual int GetNumTimeSubSteps() { return m_numTimeSubSteps; }
	virtual void SetDeactivationTime(float dTime);
	virtual void SetDeactivationLinearTreshold(float linTresh);
	virtual void SetDeactivationAngularTreshold(float angTresh);
	virtual void SetContactBreakingTreshold(float) {}
	virtual void SetCcdMode(int ccdMode);
	virtual void SetSolverSorConstant(float) {}
	virtual void SetSolverType(int) {}
	virtual void SetSolverTau(float) {}
	virtual void SetSolverDamping(float) {}
	virtual void SetLinearAirDamping(float) {}
	virtual void SetUseEpa(bool) {}

	virtual void SetGravity(float x, float y, float z);
	virtual void GetGravity(MT_Vector3 &grav);

	virtual int CreateConstraint(PHY_IPhysicsController *ctrl, PHY_IPhysicsController *ctrl2, PHY_ConstraintType type,
	                             float pivotX, float pivotY, float pivotZ,
	                             float axis0X, float axis0Y, float axis0Z,
	                             float axis1X = 0, float axis1Y = 0, float axis1Z = 0,
	                             float axis2X = 0, float axis2Y = 0, float axis2Z = 0, int flag = 0);
	virtual void RemoveConstraintById(int constraintid);
	virtual float GetAppliedImpulse(int constraintid);

	virtual PHY_IVehicle *GetVehicleConstraint(int constraintId);
	virtual PHY_ICharacter *GetCharacterController(class KX_GameObject *ob);

	virtual PHY_IPhysicsController *RayTest(PHY_IRayCastFilterCallback &filterCallback, float fromX, float fromY,
	                                        float fromZ, float toX, float toY, float toZ);

	virtual bool CullingTest(PHY_CullingCallback callback, void *userData, MT_Vector4 *planeNormals, int planeNumber,
	                         int occlusionRes, const int *viewport, float modelview[16], float projection[16]);

	virtual void AddSensor(PHY_IPhysicsController *ctrl);
	virtual void RemoveSensor(PHY_IPhysicsController *ctrl);
	virtual void AddTouchCallback(int response_class, PHY_ResponseCallback callback, void *user);
	virtual bool RequestCollisionCallback(PHY_IPhysicsController *ctrl);
	virtual bool RemoveCollisionCallback(PHY_IPhysicsController *ctrl);
	virtual PHY_IPhysicsController *CreateSphereController(float radius, const MT_Vector3 &position);
	virtual PHY_IPhysicsController *CreateConeController(float coneradius, float coneheight);

	virtual void SetConstraintParam(int constraintId, int param, float value, float value1);
	virtual float GetConstraintParam(int constraintId, int param);

	virtual void ExportFile(const char *filename);
	virtual void MergeEnvironment(PHY_IPhysicsEnvironment *other_env);

	virtual void ConvertObject(KX_GameObject *gameobj, RAS_MeshObject *meshobj, DerivedMesh *dm, KX_Scene *kxscene,
	                           PHY_ShapeProps *shapeprops, PHY_MaterialProps *smmaterial,
	                           PHY_IMotionState *motionstate, int activeLayerBitInfo, bool isCompoundChild,
	                           bool hasCompoundChildren);
	virtual void SetupObjectConstraints(KX_GameObject *obj_src, KX_GameObject *obj_dest,
	                                    bRigidBodyJointConstraint *dat);

	/* for controllers */
	physx::PxScene *GetScene() { return m_scene; }
	bool UsesGpu() const { return m_gpu; }
	void AddController(PxPhysicsController *ctrl);
	bool RemoveController(PxPhysicsController *ctrl);
	bool IsActiveController(PxPhysicsController *ctrl) const;
	/* the object's shapes changed: forget the touching pairs it had */
	void ForgetPairs(PxPhysicsController *ctrl);
	/* the object got a new PhysX actor: its joints follow */
	void RebindJoints(PxPhysicsController *ctrl);
	physx::PxControllerManager *ControllerManager();
	float SleepLinear() const { return m_linearSleep; }
	/* PhysX's sleep threshold (kinetic energy per mass) for Bullet's
	 * linear threshold; 0 (never) when the deactivation time is 0, as Bullet */
	float SleepEnergy() const { return m_deactivationTime > 0.0f ? 0.5f * m_linearSleep * m_linearSleep : 0.0f; }
	void UpdateSleeping();
	float SleepAngular() const { return m_angularSleep; }
	float Step() const { return m_lastStep; }
	const physx::PxVec3 &Gravity() const { return m_gravity; }

	/* the culling tree (Bullet's CcdPhysicsEnvironment), for the converter's
	 * graphic controllers */
	PHY_IPhysicsEnvironment *GetCullingEnvironment() { return m_culling; }

	/* tests: a controller from a construction info, without a Blender object */
	PxPhysicsController *CreateController(const PxCtrlInfo &info, PHY_IMotionState *motionState, bool addToScene);

	/* counters for the tests and the stats overlay */
	struct Stats {
		int steps;
		int actors;
		int contactReports;
		int triggerReports;
		double simulateMs;
		Stats() : steps(0), actors(0), contactReports(0), triggerReports(0), simulateMs(0.0) {}
	};
	const Stats &GetStats() const { return m_stats; }
	const char *DeviceName() const { return m_gpu ? "GPU" : "CPU"; }

	/* used by the event callback */
	struct Touch {
		PxPhysicsController *a, *b;
		MT_Vector3 point1, point2, normal;
	};
	void ContactFound(PxPhysicsController *a, PxPhysicsController *b, const MT_Vector3 &pa, const MT_Vector3 &pb,
	                  const MT_Vector3 &normal);
	void ContactLost(PxPhysicsController *a, PxPhysicsController *b);

private:
	void InitScene(bool useGpu);
	void CallbackTriggers();
	void ProcessFhSprings(float interval);
	void SimulateOnce(float dt);
	struct Constraint;
	Constraint *GetConstraint(int id);
	void RemoveConstraint(Constraint *c);
	physx::PxJoint *MakeJoint(PxPhysicsController *c0, PxPhysicsController *c1, PHY_ConstraintType type,
	                          const physx::PxTransform &frameA, const physx::PxTransform &frameB);

	physx::PxScene *m_scene;
	bool m_gpu;
	class EventCallback;
	EventCallback *m_events;
	std::set<PxPhysicsController *> m_controllers;
	std::set<PxPhysicsController *> m_kinematics;

	PHY_ResponseCallback m_triggerCallbacks[PHY_NUM_RESPONSE];
	void *m_triggerCallbacksUserPtrs[PHY_NUM_RESPONSE];
	/* touching pairs (a < b), kept from found to lost, reported each frame */
	std::map<std::pair<PxPhysicsController *, PxPhysicsController *>, Touch> m_touching;

	struct Constraint {
		int id;
		int type;
		physx::PxJoint *joint;
		PxPhysicsController *c0, *c1;
		physx::PxTransform frameA, frameB;  /* in each body's frame */
		float limits[6][2];                 /* 6DOF, cone twist, hinge: low > high = free */
		float linScale[3];                  /* 6DOF: Bullet's frame axes carry the object's
		                                     * scale, its linear limits and motors are in
		                                     * those units */
		bool motor[6];
		float motorVelocity[6], motorForce[6];
		bool spring[6];
		float springStiffness[6], springDamping[6];
	};
	std::map<int, Constraint *> m_constraints;
	int m_nextConstraintId;

	std::vector<PxRaycastVehicle *> m_vehicles;
	physx::PxControllerManager *m_controllerManager;

	PHY_IPhysicsEnvironment *m_culling;

	physx::PxVec3 m_gravity;
	int m_numTimeSubSteps;
	int m_numIterations;
	int m_debugMode;
	float m_deactivationTime;
	float m_linearSleep, m_angularSleep;
	float m_lastStep;
	double m_accumulated;  /* time not simulated yet (less than a sub-step) */
	int m_ccdMode;
	Stats m_stats;

	friend class PxPhysicsController;
	friend class PxRaycastVehicle;
};

#endif  /* __PXPHYSICSENVIRONMENT_H__ */
