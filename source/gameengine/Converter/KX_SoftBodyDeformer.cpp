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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2001-2002 by NaN Holding BV.
 * All rights reserved.
 *
 * The Original Code is: all of this file.
 *
 * Contributor(s): none yet.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gameengine/Converter/KX_SoftBodyDeformer.cpp
 *  \ingroup bgeconv
 */


#ifdef _MSC_VER
#  pragma warning (disable:4786)
#endif //WIN32

#include "MT_assert.h"

#include "KX_SoftBodyDeformer.h"
#include "RAS_MeshObject.h"
#include "CTR_Map.h"
#include "CTR_HashedPtr.h"

#ifdef WITH_BULLET

#include "CcdPhysicsEnvironment.h"
#include "CcdPhysicsController.h"
#include "BulletSoftBody/btSoftBody.h"

#include "btBulletDynamicsCommon.h"

#ifdef WITH_PHYSX  /* BGE-Remix (P6): soft bodies on PhysX's GPU deformables */
#  include "PxPhysicsController.h"
#  include "PxDeformable.h"
#endif

void KX_SoftBodyDeformer::Relink(CTR_Map<class CTR_HashedPtr, void*>*map)
{
	void **h_obj = (*map)[m_gameobj];

	if (h_obj) {
		m_gameobj = (BL_DeformableGameObject*)(*h_obj);
		m_pMeshObject = m_gameobj->GetMesh(0);
	} else {
		m_gameobj = NULL;
		m_pMeshObject = NULL;
	}
}

bool KX_SoftBodyDeformer::Apply(class RAS_IPolyMaterial *polymat)
{
	PHY_IPhysicsController *phys = m_gameobj->GetPhysicsController();
	if (!phys)
		return false;

	/* BGE-Remix (P6): the soft body is Bullet's or PhysX's */
	btSoftBody *softBody = NULL;
	if (CcdPhysicsController *ctrl = dynamic_cast<CcdPhysicsController *>(phys))
		softBody = ctrl->GetSoftBody();
#ifdef WITH_PHYSX
	PxDeformable *deformable = NULL;
	if (PxPhysicsController *pxctrl = dynamic_cast<PxPhysicsController *>(phys))
		deformable = pxctrl->GetDeformable();
	if (!softBody && !deformable)
		return false;
#else
	if (!softBody)
		return false;
#endif

	//printf("apply\n");
	RAS_MeshSlot::iterator it;
	RAS_MeshMaterial *mmat;
	RAS_MeshSlot *slot;
	size_t i;

	// update the vertex in m_transverts
	Update();

	// The vertex cache can only be updated for this deformer:
	// Duplicated objects with more than one ploymaterial (=multiple mesh slot per object)
	// share the same mesh (=the same cache). As the rendering is done per polymaterial
	// cycling through the objects, the entire mesh cache cannot be updated in one shot.
	mmat = m_pMeshObject->GetMeshMaterial(polymat);
	if (!mmat->m_slots[(void*)m_gameobj])
		return true;

	slot = *mmat->m_slots[(void*)m_gameobj];

	// for each array
	for (slot->begin(it); !slot->end(it); slot->next(it)) 
	{
#ifdef WITH_PHYSX
		if (deformable) {
			const std::vector<physx::PxVec3> &pos = deformable->WorldVertices();
			const std::vector<physx::PxVec3> &nor = deformable->WorldNormals();
			for (i = it.startvertex; i < it.endvertex; i++) {
				RAS_TexVert& v = it.vertex[i];
				const int si = v.getSoftBodyIndex();
				if (si < 0 || si >= (int)pos.size())
					continue;
				v.SetXYZ(MT_Point3(pos[si].x, pos[si].y, pos[si].z));
				if (si < (int)nor.size())
					v.SetNormal(MT_Vector3(nor[si].x, nor[si].y, nor[si].z));
			}
			continue;
		}
#endif
		btSoftBody::tNodeArray&   nodes(softBody->m_nodes);

		int index = 0;
		for (i=it.startvertex; i<it.endvertex; i++,index++) {
			RAS_TexVert& v = it.vertex[i];
			btAssert(v.getSoftBodyIndex() >= 0);

			MT_Point3 pt (
				nodes[v.getSoftBodyIndex()].m_x.getX(),
				nodes[v.getSoftBodyIndex()].m_x.getY(),
				nodes[v.getSoftBodyIndex()].m_x.getZ());
			v.SetXYZ(pt);

			MT_Vector3 normal (
				nodes[v.getSoftBodyIndex()].m_n.getX(),
				nodes[v.getSoftBodyIndex()].m_n.getY(),
				nodes[v.getSoftBodyIndex()].m_n.getZ());
			v.SetNormal(normal);

		}
	}
	return true;
}

#endif
