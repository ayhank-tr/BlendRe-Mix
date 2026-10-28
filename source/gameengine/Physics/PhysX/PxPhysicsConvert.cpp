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

/** \file PxPhysicsConvert.cpp
 *  \ingroup physx
 *
 * The parts of the PhysX environment and controller that read Blender's
 * objects and the game engine's (kept apart from the simulation, which the
 * PhysX tests build without Blender).
 */

#include "PxPhysicsEnvironment.h"

#include <algorithm>
#include <map>
#include <vector>
#include <cmath>
#include <cstdio>

#include "PxPhysicsAPI.h"

#include "PxCharacter.h"
#include "PxPhysicsController.h"
#include "PxShapes.h"
#include "PxSystem.h"

#include "CcdPhysicsController.h"   /* CcdShapeConstructionInfo */
#include "CcdPhysicsEnvironment.h"  /* the culling tree */

#include "PHY_IMotionState.h"
#include "PHY_Pro.h"
#include "MT_Matrix3x3.h"

#include "KX_GameObject.h"
#include "KX_ClientObjectInfo.h"
#include "KX_PythonInit.h"
#include "KX_BlenderSceneConverter.h"
#include "KX_KetsjiEngine.h"
#include "RAS_MeshObject.h"
#include "RAS_MaterialBucket.h"
#include "RAS_TexVert.h"
#include "SG_Node.h"

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

static PxVec3 px(const MT_Vector3 &v)
{
	return PxVec3((float)v.x(), (float)v.y(), (float)v.z());
}

PxPhysicsEnvironment *PxPhysicsEnvironment::Create(Scene *blenderscene, bool visualizePhysics, bool useGpu)
{
	if (!PxSystem::Get()) {
		return NULL;
	}
	PxPhysicsEnvironment *env = new PxPhysicsEnvironment(useGpu, new CcdPhysicsEnvironment((blenderscene->gm.mode & WO_DBVT_CULLING) != 0));
	if (!env->m_scene) {
		delete env;
		return NULL;
	}
	env->SetDeactivationLinearTreshold(blenderscene->gm.lineardeactthreshold);
	env->SetDeactivationAngularTreshold(blenderscene->gm.angulardeactthreshold);
	env->SetDeactivationTime(blenderscene->gm.deactivationtime);
	if (visualizePhysics) {
		env->SetDebugMode(1);
	}
	printf("BGE-Remix: scene \"%s\" uses PhysX on the %s\n", blenderscene->id.name + 2, env->DeviceName());
	fflush(stdout);
	return env;
}

PHY_ICharacter *PxPhysicsEnvironment::GetCharacterController(KX_GameObject *ob)
{
	PxPhysicsController *ctrl = dynamic_cast<PxPhysicsController *>(ob->GetPhysicsController());
	return ctrl ? ctrl->GetCharacter() : NULL;
}

void PxPhysicsEnvironment::SetupObjectConstraints(KX_GameObject *obj_src, KX_GameObject *obj_dest,
                                                  bRigidBodyJointConstraint *dat)
{
	/* as Bullet's: the pivot and a full frame, scaled by the object */
	PHY_IPhysicsController *phy_src = obj_src->GetPhysicsController();
	PHY_IPhysicsController *phy_dest = obj_dest->GetPhysicsController();

	MT_Matrix3x3 localCFrame(MT_Vector3(dat->axX, dat->axY, dat->axZ));
	MT_Vector3 axis0 = localCFrame.getColumn(0);
	MT_Vector3 axis1 = localCFrame.getColumn(1);
	MT_Vector3 axis2 = localCFrame.getColumn(2);
	MT_Vector3 scale = obj_src->NodeGetWorldScaling();

	const int constraintId = CreateConstraint(
	        phy_src, phy_dest, (PHY_ConstraintType)dat->type,
	        (float)(dat->pivX * scale.x()), (float)(dat->pivY * scale.y()), (float)(dat->pivZ * scale.z()),
	        (float)(axis0.x() * scale.x()), (float)(axis0.y() * scale.y()), (float)(axis0.z() * scale.z()),
	        (float)(axis1.x() * scale.x()), (float)(axis1.y() * scale.y()), (float)(axis1.z() * scale.z()),
	        (float)(axis2.x() * scale.x()), (float)(axis2.y() * scale.y()), (float)(axis2.z() * scale.z()),
	        dat->flag);
	if (!constraintId) {
		return;
	}
	int dof = 0, dof_max = 0, dofbit = 0;
	switch (dat->type) {
		case PHY_GENERIC_6DOF_CONSTRAINT:
			dof_max = 6;
			dofbit = 1;
			break;
		case PHY_CONE_TWIST_CONSTRAINT:
			dof = 3;
			dof_max = 6;
			dofbit = 1 << 3;
			break;
		case PHY_LINEHINGE_CONSTRAINT:
		case PHY_ANGULAR_CONSTRAINT:
			dof = 3;
			dof_max = 4;
			dofbit = 1 << 3;
			break;
		default:
			break;
	}
	for (; dof < dof_max; dof++) {
		if (dat->flag & dofbit) {
			SetConstraintParam(constraintId, dof, dat->minLimit[dof], dat->maxLimit[dof]);
		}
		else {
			/* min > max: free */
			SetConstraintParam(constraintId, dof, 1.0f, -1.0f);
		}
		dofbit <<= 1;
	}
}

/* ------------------------------------------------------------------------- */
/* conversion: Blender's settings into a controller, as Bullet's ConvertObject */

/* GPU soft bodies: each drawn vertex is told which simulated vertex moves it
 * (as Bullet: the nearest, in the mesh's own coordinates) */
static void map_soft_vertices(CcdShapeConstructionInfo *mesh)
{
	RAS_MeshObject *rasMesh = mesh ? mesh->GetMesh() : NULL;
	const int n = mesh ? mesh->m_vertexArray.size() / 3 : 0;
	if (!rasMesh || n == 0) {
		return;
	}
	std::map<std::vector<float>, int> exact;
	for (int i = 0; i < n; i++) {
		exact.insert(std::make_pair(std::vector<float>{mesh->m_vertexArray[3 * i], mesh->m_vertexArray[3 * i + 1],
		                                               mesh->m_vertexArray[3 * i + 2]}, i));
	}
	RAS_MeshSlot::iterator it;
	for (int m = 0; m < rasMesh->NumMaterials(); m++) {
		RAS_MeshSlot *slot = rasMesh->GetMeshMaterial(m)->m_baseslot;
		for (slot->begin(it); !slot->end(it); slot->next(it)) {
			for (size_t i = it.startvertex; i < it.endvertex; i++) {
				RAS_TexVert *v = &it.vertex[i];
				const float *xyz = v->getXYZ();
				std::map<std::vector<float>, int>::const_iterator f = exact.find(std::vector<float>{xyz[0], xyz[1], xyz[2]});
				int best = 0;
				if (f != exact.end()) {
					best = f->second;
				}
				else {
					float bestd = 1e30f;
					for (int j = 0; j < n; j++) {
						const float dx = mesh->m_vertexArray[3 * j] - xyz[0], dy = mesh->m_vertexArray[3 * j + 1] - xyz[1],
						            dz = mesh->m_vertexArray[3 * j + 2] - xyz[2];
						const float d = dx * dx + dy * dy + dz * dz;
						if (d < bestd) {
							bestd = d;
							best = j;
						}
					}
				}
				v->setSoftBodyIndex((short)best);
			}
		}
	}
}

void PxPhysicsEnvironment::ConvertObject(KX_GameObject *gameobj, RAS_MeshObject *meshobj, DerivedMesh *dm,
                                         KX_Scene *kxscene, PHY_ShapeProps *shapeprops, PHY_MaterialProps *smmaterial,
                                         PHY_IMotionState *motionstate, int activeLayerBitInfo, bool isCompoundChild,
                                         bool hasCompoundChildren)
{
	Object *blenderobject = gameobj->GetBlenderObject();

	const bool isdyna = (blenderobject->gameflag & OB_DYNAMIC) != 0;
	const bool issensor = (blenderobject->gameflag & OB_SENSOR) != 0;
	const bool ischar = (blenderobject->gameflag & OB_CHARACTER) != 0;
	bool issoft = (blenderobject->gameflag & OB_SOFT_BODY) != 0;
	const bool isrigid = (blenderobject->gameflag & OB_RIGID_BODY) != 0;

	/* the root parent */
	Object *blenderparent = blenderobject->parent;
	while (blenderparent && blenderparent->parent) {
		blenderparent = blenderparent->parent;
	}
	KX_GameObject *parent = NULL;
	if (blenderparent) {
		KX_BlenderSceneConverter *converter = (KX_BlenderSceneConverter *)KX_GetActiveEngine()->GetSceneConverter();
		parent = converter->FindGameObject(blenderparent);
		issoft = false;
	}

	PxCtrlInfo ci;
	ci.dyna = isdyna;
	ci.sensor = issensor;
	ci.character = ischar;
	ci.soft = issoft;
	ci.rigid = isdyna && isrigid;
	ci.noResponse = (blenderobject->gameflag & (OB_GHOST | OB_SENSOR | OB_CHARACTER)) != 0;
	ci.linearFactor = PxVec3((blenderobject->gameflag2 & OB_LOCK_RIGID_BODY_X_AXIS) ? 0.0f : 1.0f,
	                         (blenderobject->gameflag2 & OB_LOCK_RIGID_BODY_Y_AXIS) ? 0.0f : 1.0f,
	                         (blenderobject->gameflag2 & OB_LOCK_RIGID_BODY_Z_AXIS) ? 0.0f : 1.0f);
	ci.angularFactor = PxVec3((blenderobject->gameflag2 & OB_LOCK_RIGID_BODY_X_ROT_AXIS) ? 0.0f : 1.0f,
	                          (blenderobject->gameflag2 & OB_LOCK_RIGID_BODY_Y_ROT_AXIS) ? 0.0f : 1.0f,
	                          (blenderobject->gameflag2 & OB_LOCK_RIGID_BODY_Z_ROT_AXIS) ? 0.0f : 1.0f);
	ci.mass = isdyna ? (float)shapeprops->m_mass : 0.0f;
	ci.clampVelMin = (float)shapeprops->m_clamp_vel_min;
	ci.clampVelMax = (float)shapeprops->m_clamp_vel_max;
	ci.clampAngVelMin = (float)shapeprops->m_clamp_angvel_min;
	ci.clampAngVelMax = (float)shapeprops->m_clamp_angvel_max;
	ci.stepHeight = ischar ? (float)shapeprops->m_step_height : 0.0f;
	ci.jumpSpeed = ischar ? (float)shapeprops->m_jump_speed : 0.0f;
	ci.fallSpeed = ischar ? (float)shapeprops->m_fall_speed : 0.0f;
	ci.maxJumps = ischar ? shapeprops->m_max_jumps : 0;
	ci.radius = blenderobject->inertia;
	ci.margin = issoft && blenderobject->bsoft ? blenderobject->bsoft->margin : blenderobject->margin;
	ci.bsoft = issoft ? blenderobject->bsoft : NULL;
	ci.disableSleep = (blenderobject->gameflag & OB_COLLISION_RESPONSE) != 0;

	/* the bounds */
	char bounds = isdyna ? OB_BOUND_SPHERE : OB_BOUND_TRIANGLE_MESH;
	if (!(blenderobject->gameflag & OB_BOUNDS)) {
		if (blenderobject->gameflag & OB_SOFT_BODY) {
			bounds = OB_BOUND_TRIANGLE_MESH;
		}
		else if (blenderobject->gameflag & OB_CHARACTER) {
			bounds = OB_BOUND_SPHERE;
		}
	}
	else {
		if (ELEM(blenderobject->collision_boundtype, OB_BOUND_CONVEX_HULL, OB_BOUND_TRIANGLE_MESH) &&
		    blenderobject->type != OB_MESH)
		{
			bounds = OB_BOUND_SPHERE;
		}
		else {
			bounds = blenderobject->collision_boundtype;
		}
	}
	float bounds_center[3], bounds_extends[3];
	BoundBox *bb = BKE_object_boundbox_get(blenderobject);
	if (bb == NULL) {
		bounds_center[0] = bounds_center[1] = bounds_center[2] = 0.0f;
		bounds_extends[0] = bounds_extends[1] = bounds_extends[2] = 1.0f;
	}
	else {
		bounds_extends[0] = 0.5f * fabsf(bb->vec[0][0] - bb->vec[4][0]);
		bounds_extends[1] = 0.5f * fabsf(bb->vec[0][1] - bb->vec[2][1]);
		bounds_extends[2] = 0.5f * fabsf(bb->vec[0][2] - bb->vec[1][2]);
		bounds_center[0] = 0.5f * (bb->vec[0][0] + bb->vec[4][0]);
		bounds_center[1] = 0.5f * (bb->vec[0][1] + bb->vec[2][1]);
		bounds_center[2] = 0.5f * (bb->vec[0][2] + bb->vec[1][2]);
	}
	(void)bounds_center;  /* as Bullet: primitive bounds sit on the origin */

	PxShapeDesc *shape = new PxShapeDesc();
	switch (bounds) {
		case OB_BOUND_SPHERE:
			shape->m_type = PHY_SHAPE_SPHERE;
			shape->m_radius = blenderobject->inertia;
			break;
		case OB_BOUND_BOX:
			shape->m_type = PHY_SHAPE_BOX;
			shape->m_halfExtents = PxVec3(bounds_extends[0], bounds_extends[1], bounds_extends[2]);
			break;
		case OB_BOUND_CYLINDER: {
			const float radius = std::max(bounds_extends[0], bounds_extends[1]);
			shape->m_type = PHY_SHAPE_CYLINDER;
			shape->m_halfExtents = PxVec3(radius, radius, bounds_extends[2]);
			break;
		}
		case OB_BOUND_CONE:
			shape->m_type = PHY_SHAPE_CONE;
			shape->m_radius = std::max(bounds_extends[0], bounds_extends[1]);
			shape->m_height = 2.0f * bounds_extends[2];
			break;
		case OB_BOUND_CAPSULE:
			shape->m_type = PHY_SHAPE_CAPSULE;
			shape->m_radius = std::max(bounds_extends[0], bounds_extends[1]);
			shape->m_height = std::max(2.0f * (bounds_extends[2] - shape->m_radius), 0.0f);
			break;
		case OB_BOUND_CONVEX_HULL: {
			CcdShapeConstructionInfo *mesh = CcdShapeConstructionInfo::FindMesh(meshobj, dm, true);
			if (mesh) {
				mesh->AddRef();
			}
			else {
				mesh = new CcdShapeConstructionInfo();
				if (!mesh->SetMesh(meshobj, dm, true)) {
					mesh->Release();
					mesh = NULL;
				}
			}
			shape->m_type = PHY_SHAPE_POLYTOPE;
			shape->m_mesh = mesh;
			break;
		}
		case OB_BOUND_TRIANGLE_MESH: {
			CcdShapeConstructionInfo *mesh = CcdShapeConstructionInfo::FindMesh(meshobj, dm, false);
			if (mesh) {
				mesh->AddRef();
			}
			else {
				mesh = new CcdShapeConstructionInfo();
				if (!mesh->SetMesh(meshobj, dm, false)) {
					mesh->Release();
					mesh = NULL;
				}
			}
			shape->m_type = PHY_SHAPE_MESH;
			shape->m_mesh = mesh;
			break;
		}
		default:
			break;
	}
	shape->m_margin = std::max(ci.margin, 0.0f);
	if ((shape->m_type == PHY_SHAPE_MESH || shape->m_type == PHY_SHAPE_POLYTOPE) && !shape->m_mesh) {
		shape->Release();
		delete motionstate;
		return;
	}

	if (isCompoundChild) {
		/* the shape joins the parent's (compound) shape */
		PxPhysicsController *parentCtrl = parent ? dynamic_cast<PxPhysicsController *>(parent->GetPhysicsController()) :
		                                           NULL;
		if (parentCtrl && parentCtrl->GetShapeDesc() && parentCtrl->GetShapeDesc()->m_type == PHY_SHAPE_COMPOUND) {
			SG_Node *gameNode = gameobj->GetSGNode();
			SG_Node *parentNode = parent->GetSGNode();
			MT_Vector3 parentScale = parentNode->GetWorldScaling();
			parentScale[0] = 1.0f / parentScale[0];
			parentScale[1] = 1.0f / parentScale[1];
			parentScale[2] = 1.0f / parentScale[2];
			const MT_Vector3 relativeScale = gameNode->GetWorldScaling() * parentScale;
			const MT_Matrix3x3 parentInvRot = parentNode->GetWorldOrientation().transposed();
			const MT_Vector3 relativePos = parentInvRot * ((gameNode->GetWorldPosition() - parentNode->GetWorldPosition()) *
			                                               parentScale);
			const MT_Matrix3x3 relativeRot = parentInvRot * gameNode->GetWorldOrientation();
			PxMat33 rot(PxVec3(relativeRot[0][0], relativeRot[1][0], relativeRot[2][0]),
			            PxVec3(relativeRot[0][1], relativeRot[1][1], relativeRot[2][1]),
			            PxVec3(relativeRot[0][2], relativeRot[1][2], relativeRot[2][2]));
			PxShapeDesc::Child ch;
			ch.desc = shape;  /* the reference moves to the parent */
			ch.local = PxTransform(px(relativePos), PxQuat(rot).getNormalized());
			ch.scale = px(relativeScale);
			ch.owner = NULL;
			parentCtrl->GetShapeDesc()->m_children.push_back(ch);
			/* the parent's actor gets the new shapes */
			parentCtrl->ReinstanceCompound();
			delete motionstate;
			return;
		}
		shape->Release();
		delete motionstate;
		return;
	}
	if (hasCompoundChildren) {
		PxShapeDesc *compound = new PxShapeDesc();
		compound->m_type = PHY_SHAPE_COMPOUND;
		PxShapeDesc::Child ch;
		ch.desc = shape;
		ch.local = PxTransform(PxIdentity);
		ch.scale = PxVec3(1.0f);
		ch.owner = NULL;
		compound->m_children.push_back(ch);
		shape = compound;
	}

	ci.shape = shape;
	ci.friction = (float)smmaterial->m_friction;
	ci.restitution = (float)smmaterial->m_restitution;
	/* Blender's damping is stored inverted ("drag") */
	ci.linearDamping = 1.0f - (float)shapeprops->m_lin_drag;
	ci.angularDamping = 1.0f - (float)shapeprops->m_ang_drag;
	ci.inertiaFactor = (float)shapeprops->m_inertia / 0.4f;
	ci.anisotropic = shapeprops->m_do_anisotropic;
	ci.anisotropicFriction = PxVec3((float)shapeprops->m_friction_scaling[0], (float)shapeprops->m_friction_scaling[1],
	                                (float)shapeprops->m_friction_scaling[2]);
	ci.doFh = shapeprops->m_do_fh;
	ci.doRotFh = shapeprops->m_do_rot_fh;
	ci.fhDamping = (float)smmaterial->m_fh_damping;
	ci.fhDistance = (float)smmaterial->m_fh_distance;
	ci.fhNormal = smmaterial->m_fh_normal;
	ci.fhSpring = (float)smmaterial->m_fh_spring;
	ci.filterGroup = issensor ? PXF_SENSOR : (isdyna ? PXF_DEFAULT : (ischar ? PXF_CHARACTER : PXF_STATIC));
	ci.filterMask = issensor ? (PXF_ALL ^ PXF_SENSOR) :
	                (isdyna ? PXF_ALL : (ischar ? PXF_ALL : (PXF_ALL ^ PXF_STATIC)));
	const MT_Vector3 scaling = gameobj->NodeGetWorldScaling();
	ci.scaling = px(scaling);

	PxPhysicsController *ctrl = new PxPhysicsController(ci, motionstate, this);
	shape->Release();
	if (ctrl->GetDeformable() && ctrl->GetShapeDesc()) {
		map_soft_vertices(ctrl->GetShapeDesc()->m_mesh);
	}

	gameobj->SetPhysicsController(ctrl, isdyna);
	if (isdyna) {
		gameobj->SetRecordAnimation(true);
	}
	ctrl->SetNewClientInfo(gameobj->getClientInfo());

	/* sensor objects are added when a collision sensor uses them */
	if (!issensor && (blenderobject->lay & activeLayerBitInfo) != 0) {
		AddController(ctrl);
	}
	if (parent) {
		ctrl->SuspendDynamics(false);
	}
	ctrl->SetParentCtrl(parent ? dynamic_cast<PxPhysicsController *>(parent->GetPhysicsController()) : NULL);
	(void)kxscene;
}
