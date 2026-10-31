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

/** \file gameengine/Converter/BL_ShapeDeformer.cpp
 *  \ingroup bgeconv
 */

#ifdef _MSC_VER
#  pragma warning (disable:4786)
#endif

#include "MEM_guardedalloc.h"
#include "BL_ShapeDeformer.h"
#include "CTR_Map.h"
#include "STR_HashedString.h"
#include "RAS_IPolygonMaterial.h"
#include "RAS_MeshObject.h"
#include "RAS_GPUSkin.h"

#include "DNA_anim_types.h"
#include "DNA_armature_types.h"
#include "DNA_action_types.h"
#include "DNA_key_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "BKE_armature.h"
#include "BKE_action.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_key.h"
#include "BKE_fcurve.h"
#include "BKE_ipo.h"
#include "BKE_library.h"
#include "MT_Point3.h"

extern "C"{
	#include "BKE_lattice.h"
	#include "BKE_animsys.h"
}

#include "BLI_blenlib.h"
#include "BLI_math.h"

#define __NLA_DEFNORMALS
//#undef __NLA_DEFNORMALS

BL_ShapeDeformer::BL_ShapeDeformer(BL_DeformableGameObject *gameobj,
                                   Object *bmeshobj,
                                   RAS_MeshObject *mesh)
    :
      BL_SkinDeformer(gameobj,bmeshobj, mesh),
      m_useShapeDrivers(false),
      m_lastShapeUpdate(-1)
{
	m_key = m_bmesh->key ? BKE_key_copy(G.main, m_bmesh->key) : NULL;
};

/* this second constructor is needed for making a mesh deformable on the fly. */
BL_ShapeDeformer::BL_ShapeDeformer(BL_DeformableGameObject *gameobj,
				Object *bmeshobj_old,
				Object *bmeshobj_new,
				RAS_MeshObject *mesh,
				bool release_object,
				bool recalc_normal,
				BL_ArmatureObject* arma)
				:
					BL_SkinDeformer(gameobj, bmeshobj_old, bmeshobj_new, mesh, release_object, recalc_normal, arma),
					m_useShapeDrivers(false),
					m_lastShapeUpdate(-1)
{
	m_key = m_bmesh->key ? BKE_key_copy(G.main, m_bmesh->key) : NULL;
};

BL_ShapeDeformer::~BL_ShapeDeformer()
{
	if (m_key)
	{
		BKE_libblock_free(G.main, m_key);
		m_key = NULL;
	}
};

RAS_Deformer *BL_ShapeDeformer::GetReplica()
{
	BL_ShapeDeformer *result;

	result = new BL_ShapeDeformer(*this);
	result->ProcessReplica();
	return result;
}

void BL_ShapeDeformer::ProcessReplica()
{
	BL_SkinDeformer::ProcessReplica();
	m_lastShapeUpdate = -1;

	m_key = m_key ? BKE_key_copy(G.main, m_key) : NULL;
}

bool BL_ShapeDeformer::LoadShapeDrivers(KX_GameObject* parent)
{
	// Only load shape drivers if we have a key
	if (GetKey() == NULL) {
		m_useShapeDrivers = false;
		return false;
	}

	// Fix drivers since BL_ArmatureObject makes copies
	if (parent->GetGameObjectType() == SCA_IObject::OBJ_ARMATURE && GetKey()->adt) {
		BL_ArmatureObject *arma = (BL_ArmatureObject*)parent;
		FCurve *fcu;

		for (fcu = (FCurve*)GetKey()->adt->drivers.first; fcu; fcu = (FCurve*)fcu->next) {

			DriverVar *dvar;
			for (dvar = (DriverVar*)fcu->driver->variables.first; dvar; dvar = (DriverVar*)dvar->next) {
				DRIVER_TARGETS_USED_LOOPER(dvar)
				{
					if (dtar->id) {
						if ((Object*)dtar->id == arma->GetOrigArmatureObject())
							dtar->id = (ID*)arma->GetArmatureObject();
					}
				}
				DRIVER_TARGETS_LOOPER_END
			}
		}
	}

	// This used to check if we had drivers from this armature,
	// now we just assume we want to use shape drivers
	// and let the animsys handle things.
	m_useShapeDrivers = true;

	return true;
}

bool BL_ShapeDeformer::ExecuteShapeDrivers(void)
{
	if (m_useShapeDrivers && PoseUpdated()) {
		// We don't need an actual time, just use 0
		BKE_animsys_evaluate_animdata(NULL, &GetKey()->id, GetKey()->adt, 0.f, ADT_RECALC_DRIVERS);

		ForceUpdate();
		m_bDynamic = true;
		return true;
	}
	return false;
}

bool BL_ShapeDeformer::Update(void)
{
	bool bShapeUpdate = false;
	bool bSkinUpdate = false;

	ExecuteShapeDrivers();

	/* BGE-Remix P8 Part B (P8.13): the keys and bones go to the graphics card */
	if (GPUSkinActive()) {
		UpdateGPUSkin();
		return false;
	}

	/* See if the object shape has changed */
	if (m_lastShapeUpdate != m_gameobj->GetLastFrame()) {
		/* the key coefficient have been set already, we just need to blend the keys */
		Object* blendobj = m_gameobj->GetBlendObject();
		
		/* we will blend the key directly in m_transverts array: it is used by armature as the start position */
		/* m_key can be NULL in case of Modifier deformer */
		if (m_key) {
			WeightsArrayCache cache = {0, NULL};
			float **per_keyblock_weights;

			/* store verts locally */
			VerifyStorage();

			per_keyblock_weights = BKE_keyblock_get_per_block_weights(blendobj, m_key, &cache);
			BKE_key_evaluate_relative(0, m_bmesh->totvert, m_bmesh->totvert, (char *)(float *)m_transverts,
			                          m_key, NULL, per_keyblock_weights, 0); /* last arg is ignored */
			BKE_keyblock_free_per_block_weights(m_key, per_keyblock_weights, &cache);

			m_bDynamic = true;
		}

		// Don't release the weight array as in Blender, it will most likely be reusable on next frame 
		// The weight array are ultimately deleted when the skin mesh is destroyed
		   
		/* Update the current frame */
		m_lastShapeUpdate=m_gameobj->GetLastFrame();

		// As we have changed, the mesh, the skin deformer must update as well.
		// This will force the update
		BL_SkinDeformer::ForceUpdate();
		bShapeUpdate = true;
	}
	// check for armature deform
	bSkinUpdate = BL_SkinDeformer::UpdateInternal(bShapeUpdate && m_bDynamic);

	// non dynamic deformer = Modifer without armature and shape keys, no need to create storage
	if (!bSkinUpdate && bShapeUpdate && m_bDynamic) {
		// this means that there is no armature, we still need to 
		// update the normal (was not done after shape key calculation)

#ifdef __NLA_DEFNORMALS
		if (m_recalcNormal)
			RecalcNormals();
#endif

		// We also need to handle transverts now (used to be in BL_SkinDeformer::Apply())
		UpdateTransverts();
		/* BGE-Remix P8.4: tell the rasterizer the mesh changed (Apply), as the
		 * armature path does: the meshes live on the card and are copied
		 * there only when changed (stock read the arrays at every draw) */
		m_poseApplied = false;
		bSkinUpdate = true;
	}

	return bSkinUpdate;
}

/* BGE-Remix P8 Part B (P8.13): each key as its difference from the key it is
 * relative to; the CPU keeps keys that a vertex group weights, and absolute
 * keys. Each frame: the keys' values (true when they changed). */
bool BL_ShapeDeformer::GPUSkinKeys(bool build)
{
	RAS_GPUSkin *skin = m_gpuskin;
	if (!m_key || !skin)
		return true;
	if (build) {
		if (m_key->type != KEY_RELATIVE || !m_key->refkey)
			return false;
		const int nverts = skin->nverts;
		int n = 0;
		for (KeyBlock *kb = (KeyBlock *)m_key->block.first; kb; kb = kb->next) {
			if (kb == m_key->refkey)
				continue;
			if (kb->vgroup[0] || kb->totelem != nverts)
				return false;
			n++;
		}
		skin->nkeys = n;
		skin->keys.assign((size_t)n * nverts * 4, 0.0f);
		skin->keyvalues.assign((size_t)n, 0.0f);
		int k = 0;
		for (KeyBlock *kb = (KeyBlock *)m_key->block.first; kb; kb = kb->next) {
			if (kb == m_key->refkey)
				continue;
			KeyBlock *rel = (KeyBlock *)BLI_findlink(&m_key->block, kb->relative);
			if (!rel)
				rel = m_key->refkey;
			const float *to = (const float *)kb->data, *from = (const float *)rel->data;
			for (int v = 0; v < nverts; v++) {
				float *d = &skin->keys[((size_t)k * nverts + v) * 4];
				if (to && from)
					sub_v3_v3v3(d, &to[v * 3], &from[v * 3]);
			}
			k++;
		}
		/* the basis is the reference key's */
		const float *basis = (const float *)m_key->refkey->data;
		if (basis && m_key->refkey->totelem == nverts) {
			for (int v = 0; v < nverts; v++)
				copy_v3_v3(&skin->rest[(size_t)v * 8], &basis[v * 3]);
		}
		return true;
	}
	bool changed = false;
	int k = 0;
	for (KeyBlock *kb = (KeyBlock *)m_key->block.first; kb && k < skin->nkeys; kb = kb->next) {
		if (kb == m_key->refkey)
			continue;
		const float value = (kb->flag & KEYBLOCK_MUTE) ? 0.0f : kb->curval;
		if (skin->keyvalues[k] != value) {
			skin->keyvalues[k] = value;
			changed = true;
		}
		k++;
	}
	return changed;
}

Key *BL_ShapeDeformer::GetKey()
{
	return m_key;
}
