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

/** \file BL_SkinDeformer.h
 *  \ingroup bgeconv
 */

#ifndef __BL_SKINDEFORMER_H__
#define __BL_SKINDEFORMER_H__

#ifdef _MSC_VER
#  pragma warning (disable:4786)  /* get rid of stupid stl-visual compiler debug warning */
#endif  /* WIN32 */

#include "CTR_HashedPtr.h"
#include "BL_MeshDeformer.h"
#include "BL_ArmatureObject.h"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "BKE_armature.h"

#include "RAS_Deformer.h"

#include <string>
#include <vector>


class BL_SkinDeformer : public BL_MeshDeformer  
{
public:
//	void SetArmatureController (BL_ArmatureController *cont);
	virtual void Relink(CTR_Map<class CTR_HashedPtr, void*>*map);
	void SetArmature (class BL_ArmatureObject *armobj);

	BL_SkinDeformer(BL_DeformableGameObject *gameobj,
	                struct Object *bmeshobj,
	                class RAS_MeshObject *mesh,
	                BL_ArmatureObject* arma = NULL);

	/* this second constructor is needed for making a mesh deformable on the fly. */
	BL_SkinDeformer(BL_DeformableGameObject *gameobj,
	                struct Object *bmeshobj_old,
	                struct Object *bmeshobj_new,
	                class RAS_MeshObject *mesh,
	                bool release_object,
	                bool recalc_normal,
	                BL_ArmatureObject* arma = NULL);

	virtual RAS_Deformer *GetReplica();
	virtual void ProcessReplica();

	virtual ~BL_SkinDeformer();
	bool Update (void);
	bool UpdateInternal (bool shape_applied);
	bool Apply (class RAS_IPolyMaterial *polymat);
	bool UpdateBuckets(void) 
	{
		// update the deformer and all the mesh slots; Apply() does it well, so just call it.
		return Apply(NULL);
	}
	bool PoseUpdated(void)
		{ 
			if (m_armobj && m_lastArmaUpdate!=m_armobj->GetLastFrame()) {
				return true;
			}
			return false;
		}

	void ForceUpdate()
	{
		m_lastArmaUpdate = -1.0;
	};

	/* BGE-Remix (P4): RTX Remix skins the mesh on the GPU instead. */
	struct RemixWeights {
		unsigned int bones[4];   /* deform group index; RemixGroupCount() = no bone (identity) */
		float weights[4];        /* the strongest 4, adding up to 1 */
		int count;               /* influences before keeping 4 */
	};
	/* Whether this deformer's maths is plain vertex-group skinning with
	 * linear blending (what Remix does); r_why says why not. */
	bool RemixCanSkin(std::string &r_why);
	/* Number of deform groups (vertex groups) of the mesh object. */
	int RemixGroupCount();
	/* The deform group names in order: two objects with the same mesh and
	 * the same groups can share one skinned Remix mesh. */
	std::string RemixGroupKey();
	/* Weights per original (Blender) vertex, by mvert index. */
	bool RemixVertexWeights(std::vector<RemixWeights> &r_weights);
	/* RemixGroupCount() + 1 OpenGL matrices (column major): each deform
	 * group's bone, mapping the rest mesh to the posed mesh in the mesh
	 * object's space (the last one is the identity), as BGEDeformVerts does. */
	void RemixBoneMatrices(std::vector<float> &r_matrices);
	/* On: Update() leaves the CPU skinning out (Remix does it). */
	void SetRemixGPU(bool on);
	bool GetRemixGPU() const { return m_remixGPU; }
	BL_ArmatureObject *RemixArmature() { return m_armobj; }
	virtual bool ShareVertexArray()
	{
		return false;
	}

protected:
	BL_ArmatureObject*		m_armobj;	//	Our parent object
	float					m_time;
	double					m_lastArmaUpdate;
	//ListBase*				m_defbase;
	float					m_obmat[4][4];	// the reference matrix for skeleton deform
	bool					m_releaseobject;
	bool					m_poseApplied;
	bool					m_recalcNormal;
	bool					m_copyNormals; // dirty flag so we know if Apply() needs to copy normal information (used for BGEDeformVerts())
	struct bPoseChannel**	m_dfnrToPC;
	short					m_deformflags;
	bool					m_remixGPU;

	/* m_dfnrToPC from the current pose (needs ApplyPose) */
	void InitGroupMap();

	void BlenderDeformVerts();
	void BGEDeformVerts();

	void UpdateTransverts();


#ifdef WITH_CXX_GUARDEDALLOC
	MEM_CXX_CLASS_ALLOC_FUNCS("GE:BL_SkinDeformer")
#endif
};

#endif  /* __BL_SKINDEFORMER_H__ */
