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

/** \file gameengine/Converter/BL_SkinDeformer.cpp
 *  \ingroup bgeconv
 */

#ifdef _MSC_VER
#  pragma warning (disable:4786)
#endif

// Eigen3 stuff used for BGEDeformVerts
#include <Eigen/Core>
#include <Eigen/LU>

#include <algorithm>
#include <cstring>
#include <utility>

#include "BL_SkinDeformer.h"
#include "CTR_Map.h"
#include "STR_HashedString.h"
#include "RAS_IPolygonMaterial.h"
#include "RAS_MeshObject.h"
#include "RAS_GPUSkin.h"
#include "GPU_glew.h"
extern "C" {
#include "GPU_layer.h"
}

//#include "BL_ArmatureController.h"
#include "DNA_armature_types.h"
#include "DNA_action_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "BLI_utildefines.h"
#include "BKE_armature.h"
#include "BKE_action.h"
#include "MT_Point3.h"

extern "C"{
	#include "BKE_lattice.h"
	#include "BKE_deform.h"
}
 

#include "BLI_blenlib.h"
#include "BLI_math.h"

#define __NLA_DEFNORMALS
//#undef __NLA_DEFNORMALS

static short get_deformflags(struct Object *bmeshobj)
{
	short flags = ARM_DEF_VGROUP;

	ModifierData *md;
	for (md = (ModifierData *)bmeshobj->modifiers.first; md; md = md->next)
	{
		if (md->type == eModifierType_Armature)
		{
			flags |= ((ArmatureModifierData*)md)->deformflag;
			break;
		}
	}

	return flags;
}

BL_SkinDeformer::BL_SkinDeformer(BL_DeformableGameObject *gameobj,
                                 struct Object *bmeshobj,
                                 class RAS_MeshObject *mesh,
                                 BL_ArmatureObject* arma)
							:	//
							BL_MeshDeformer(gameobj, bmeshobj, mesh),
							m_armobj(arma),
							m_lastArmaUpdate(-1),
							//m_defbase(&bmeshobj->defbase),
							m_releaseobject(false),
							m_poseApplied(false),
							m_recalcNormal(true),
							m_copyNormals(false),
							m_dfnrToPC(NULL),
							m_remixGPU(false),
							m_gpuskin(NULL),
							m_gpustate(0)
{
	copy_m4_m4(m_obmat, bmeshobj->obmat);
	m_deformflags = get_deformflags(bmeshobj);
};

BL_SkinDeformer::BL_SkinDeformer(
	BL_DeformableGameObject *gameobj,
	struct Object *bmeshobj_old,	// Blender object that owns the new mesh
	struct Object *bmeshobj_new,	// Blender object that owns the original mesh
	class RAS_MeshObject *mesh,
	bool release_object,
	bool recalc_normal,
	BL_ArmatureObject* arma)	:
		BL_MeshDeformer(gameobj, bmeshobj_old, mesh),
		m_armobj(arma),
		m_lastArmaUpdate(-1),
		//m_defbase(&bmeshobj_old->defbase),
		m_releaseobject(release_object),
		m_recalcNormal(recalc_normal),
		m_poseApplied(false),
		m_copyNormals(false),
		m_dfnrToPC(NULL),
		m_remixGPU(false),
		m_gpuskin(NULL),
		m_gpustate(0)
	{
		// this is needed to ensure correct deformation of mesh:
		// the deformation is done with Blender's armature_deform_verts() function
		// that takes an object as parameter and not a mesh. The object matrice is used
		// in the calculation, so we must use the matrix of the original object to
		// simulate a pure replacement of the mesh.
		copy_m4_m4(m_obmat, bmeshobj_new->obmat);
		m_deformflags = get_deformflags(bmeshobj_new);
	}

BL_SkinDeformer::~BL_SkinDeformer()
{
	if (m_releaseobject && m_armobj)
		m_armobj->Release();
	if (m_dfnrToPC)
		delete [] m_dfnrToPC;
	delete m_gpuskin;
}

void BL_SkinDeformer::Relink(CTR_Map<class CTR_HashedPtr, void*>*map)
{
	if (m_armobj) {
		void **h_obj = (*map)[m_armobj];

		if (h_obj)
			m_armobj = (BL_ArmatureObject*)(*h_obj);
		else
			m_armobj=NULL;
	}

	BL_MeshDeformer::Relink(map);
}

bool BL_SkinDeformer::Apply(RAS_IPolyMaterial *mat)
{
	// We do everything in UpdateInternal() now so we can thread it.
	// All that is left is telling the rasterizer if we've changed the mesh
	bool retval = !m_poseApplied;
	m_poseApplied = true;
	return retval;
}

RAS_Deformer *BL_SkinDeformer::GetReplica()
{
	BL_SkinDeformer *result;

	result = new BL_SkinDeformer(*this);
	/* there is m_armobj that must be fixed but we cannot do it now, it will be done in Relink */
	result->ProcessReplica();
	return result;
}

void BL_SkinDeformer::ProcessReplica()
{
	BL_MeshDeformer::ProcessReplica();
	m_lastArmaUpdate = -1;
	/* the copy has its own arrays: its own GPU skin */
	m_gpuskin = NULL;
	m_gpustate = 0;
	m_releaseobject = false;
	m_dfnrToPC = NULL;
	m_remixGPU = false;
}

void BL_SkinDeformer::BlenderDeformVerts()
{
	float obmat[4][4];	// the original object matrix
	Object* par_arma = m_armobj->GetArmatureObject();

	// save matrix first
	copy_m4_m4(obmat, m_objMesh->obmat);
	// set reference matrix
	copy_m4_m4(m_objMesh->obmat, m_obmat);

	armature_deform_verts( par_arma, m_objMesh, NULL, m_transverts, NULL, m_bmesh->totvert, m_deformflags, NULL, NULL );
		
	// restore matrix 
	copy_m4_m4(m_objMesh->obmat, obmat);

#ifdef __NLA_DEFNORMALS
		if (m_recalcNormal)
			RecalcNormals();
#endif
}

void BL_SkinDeformer::BGEDeformVerts()
{
	Object *par_arma = m_armobj->GetArmatureObject();
	MDeformVert *dverts = m_bmesh->dvert;
	bDeformGroup *dg;
	int defbase_tot;
	Eigen::Matrix4f pre_mat, post_mat, chan_mat, norm_chan_mat;

	if (!dverts)
		return;

	defbase_tot = BLI_listbase_count(&m_objMesh->defbase);
	(void)dg;

	InitGroupMap();

	post_mat = Eigen::Matrix4f::Map((float*)m_obmat).inverse() * Eigen::Matrix4f::Map((float*)m_armobj->GetArmatureObject()->obmat);
	pre_mat = post_mat.inverse();

	MDeformVert *dv= dverts;
	MDeformWeight *dw;

	for (int i=0; i<m_bmesh->totvert; ++i, dv++)
	{
		float contrib = 0.f, weight, max_weight=-1.f;
		bPoseChannel *pchan=NULL;
		Eigen::Map<Eigen::Vector3f> norm = Eigen::Vector3f::Map(m_transnors[i]);
		Eigen::Vector4f vec(0, 0, 0, 1);
		Eigen::Vector4f co(m_transverts[i][0],
							m_transverts[i][1],
							m_transverts[i][2],
							1.f);

		if (!dv->totweight)
			continue;

		co = pre_mat * co;

		dw= dv->dw;

		for (unsigned int j= dv->totweight; j != 0; j--, dw++)
		{
			const int index = dw->def_nr;

			if (index < defbase_tot && (pchan=m_dfnrToPC[index]))
			{
				weight = dw->weight;

				if (weight)
				{
					chan_mat = Eigen::Matrix4f::Map((float*)pchan->chan_mat);

					// Update Vertex Position
					vec.noalias() += (chan_mat*co - co)*weight;

					// Save the most influential channel so we can use it to update the vertex normal
					if (weight > max_weight)
					{
						max_weight = weight;
						norm_chan_mat = chan_mat;
					}

					contrib += weight;
				}
			}
		}
		
		// Update Vertex Normal
		norm = norm_chan_mat.topLeftCorner<3, 3>()*norm;

		co.noalias() += vec/contrib;
		co[3] = 1.f; // Make sure we have a 1 for the w component!

		co = post_mat * co;

		m_transverts[i][0] = co[0];
		m_transverts[i][1] = co[1];
		m_transverts[i][2] = co[2];
	}
	m_copyNormals = true;
}

void BL_SkinDeformer::InitGroupMap()
{
	if (m_dfnrToPC != NULL)
		return;
	Object *par_arma = m_armobj->GetArmatureObject();
	const int defbase_tot = BLI_listbase_count(&m_objMesh->defbase);
	m_dfnrToPC = new bPoseChannel*[defbase_tot > 0 ? defbase_tot : 1];
	int i;
	bDeformGroup *dg;
	for (i=0, dg=(bDeformGroup*)m_objMesh->defbase.first;
		dg;
		++i, dg = dg->next)
	{
		m_dfnrToPC[i] = BKE_pose_channel_find_name(par_arma->pose, dg->name);

		if (m_dfnrToPC[i] && m_dfnrToPC[i]->bone->flag & BONE_NO_DEFORM)
			m_dfnrToPC[i] = NULL;
	}
}

/* ------------------------------------------------------------------------- */
/* BGE-Remix (P4): GPU skinning through RTX Remix */

int BL_SkinDeformer::RemixGroupCount()
{
	return BLI_listbase_count(&m_objMesh->defbase);
}

std::string BL_SkinDeformer::RemixGroupKey()
{
	std::string key;
	for (bDeformGroup *dg = (bDeformGroup *)m_objMesh->defbase.first; dg; dg = dg->next) {
		key += dg->name;
		key += '/';
	}
	return key;
}

bool BL_SkinDeformer::RemixCanSkin(std::string &r_why)
{
	if (!m_armobj || !m_bmesh || !m_bmesh->dvert) {
		r_why = "no armature or no vertex groups";
		return false;
	}
	if (RemixGroupCount() + 1 > 256) {
		r_why = "more than 255 vertex groups";
		return false;
	}
	if (m_armobj->GetVertDeformType() == ARM_VDEF_BGE_CPU) {
		/* BGEDeformVerts: vertex groups, linear blending, nothing else */
		return true;
	}
	if (!(m_deformflags & ARM_DEF_VGROUP)) {
		r_why = "the armature does not deform by vertex groups";
		return false;
	}
	if (m_deformflags & ARM_DEF_ENVELOPE) {
		r_why = "bone envelopes";
		return false;
	}
	if (m_deformflags & ARM_DEF_QUATERNION) {
		r_why = "preserve volume (dual quaternion)";
		return false;
	}
	if (m_deformflags & ARM_DEF_INVERT_VGROUP) {
		r_why = "an inverted vertex group";
		return false;
	}
	/* B-bones bend inside a bone: not one matrix per bone */
	bArmature *arm = m_armobj->GetArmature();
	if (arm) {
		Object *par_arma = m_armobj->GetArmatureObject();
		bPose *pose = par_arma ? par_arma->pose : NULL;
		for (bPoseChannel *pchan = pose ? (bPoseChannel *)pose->chanbase.first : NULL; pchan; pchan = pchan->next) {
			if (pchan->bone && !(pchan->bone->flag & BONE_NO_DEFORM) && pchan->bone->segments > 1) {
				r_why = "B-bones (bone segments)";
				return false;
			}
		}
	}
	return true;
}

bool BL_SkinDeformer::RemixVertexWeights(std::vector<RemixWeights> &r_weights)
{
	if (!m_bmesh || !m_bmesh->dvert) {
		return false;
	}
	m_armobj->ApplyPose();
	InitGroupMap();
	m_armobj->RestorePose();

	const int groups = RemixGroupCount();
	r_weights.resize(m_bmesh->totvert);
	MDeformVert *dv = m_bmesh->dvert;
	for (int i = 0; i < m_bmesh->totvert; i++, dv++) {
		RemixWeights &w = r_weights[i];
		std::vector<std::pair<float, unsigned int> > infl;
		MDeformWeight *dw = dv->dw;
		for (int j = 0; j < dv->totweight; j++, dw++) {
			if (dw->def_nr < groups && m_dfnrToPC[dw->def_nr] && dw->weight > 0.0f) {
				infl.push_back(std::make_pair(dw->weight, (unsigned int)dw->def_nr));
			}
		}
		std::sort(infl.begin(), infl.end(),
		          [](const std::pair<float, unsigned int> &a, const std::pair<float, unsigned int> &b) {
		              return a.first > b.first;
		          });
		w.count = (int)infl.size();
		float sum = 0.0f;
		for (int k = 0; k < 4; k++) {
			if (k < (int)infl.size()) {
				w.bones[k] = infl[k].second;
				w.weights[k] = infl[k].first;
				sum += infl[k].first;
			}
			else {
				w.bones[k] = (unsigned int)groups;
				w.weights[k] = 0.0f;
			}
		}
		if (sum > 0.0f) {
			for (int k = 0; k < 4; k++) {
				w.weights[k] /= sum;
			}
		}
		else {
			/* no deforming bone: BGE leaves the vertex where it is */
			w.bones[0] = (unsigned int)groups;
			w.weights[0] = 1.0f;
		}
	}
	return true;
}

void BL_SkinDeformer::RemixBoneMatrices(std::vector<float> &r_matrices)
{
	const int groups = RemixGroupCount();
	r_matrices.assign((size_t)(groups + 1) * 16, 0.0f);
	m_armobj->ApplyPose();
	InitGroupMap();
	/* the same matrices as BGEDeformVerts: mesh space -> armature space,
	 * the pose channel, and back */
	Eigen::Matrix4f post_mat = Eigen::Matrix4f::Map((float*)m_obmat).inverse() *
	                           Eigen::Matrix4f::Map((float*)m_armobj->GetArmatureObject()->obmat);
	Eigen::Matrix4f pre_mat = post_mat.inverse();
	for (int i = 0; i <= groups; i++) {
		Eigen::Matrix4f m = Eigen::Matrix4f::Identity();
		if (i < groups && m_dfnrToPC[i]) {
			m = post_mat * Eigen::Matrix4f::Map((float*)m_dfnrToPC[i]->chan_mat) * pre_mat;
		}
		memcpy(&r_matrices[(size_t)i * 16], m.data(), sizeof(float) * 16);
	}
	m_armobj->RestorePose();
	/* the pose of this frame is used */
	m_lastArmaUpdate = m_armobj->GetLastFrame();
}

void BL_SkinDeformer::SetRemixGPU(bool on)
{
	if (m_remixGPU && !on) {
		/* OpenGL again: skin on the CPU from the next update */
		ForceUpdate();
	}
	m_remixGPU = on;
}

void BL_SkinDeformer::UpdateTransverts()
{
	RAS_MeshSlot::iterator it;
	RAS_MeshMaterial *mmat;
	RAS_MeshSlot *slot;
	size_t i, nmat, imat;

	if (m_transverts) {
		// the vertex cache is unique to this deformer, no need to update it
		// if it wasn't updated! We must update all the materials at once
		// because we will not get here again for the other material
		nmat = m_pMeshObject->NumMaterials();
		for (imat=0; imat<nmat; imat++) {
			mmat = m_pMeshObject->GetMeshMaterial(imat);
			if (!mmat->m_slots[(void*)m_gameobj])
				continue;

			slot = *mmat->m_slots[(void*)m_gameobj];

			// for each array
			for (slot->begin(it); !slot->end(it); slot->next(it)) {
				// for each vertex
				// copy the untransformed data from the original mvert
				for (i=it.startvertex; i<it.endvertex; i++) {
					RAS_TexVert& v = it.vertex[i];
					v.SetXYZ(m_transverts[v.getOrigIndex()]);
					if (m_copyNormals)
						v.SetNormal(m_transnors[v.getOrigIndex()]);
				}
			}
		}

		if (m_copyNormals)
			m_copyNormals = false;
	}
}

bool BL_SkinDeformer::UpdateInternal(bool shape_applied)
{
	/* BGE-Remix: Remix skins on the GPU (RemixBoneMatrices) */
	if (m_remixGPU && !shape_applied) {
		return false;
	}
	/* BGE-Remix P8 Part B (P8.13): the graphics card deforms the mesh */
	if (GPUSkinActive()) {
		UpdateGPUSkin();
		return false;
	}
	/* See if the armature has been updated for this frame */
	if (PoseUpdated()) {

		if (!shape_applied) {
			/* store verts locally */
			VerifyStorage();

			/* duplicate */
			for (int v =0; v<m_bmesh->totvert; v++)
			{
				copy_v3_v3(m_transverts[v], m_bmesh->mvert[v].co);
				normal_short_to_float_v3(m_transnors[v], m_bmesh->mvert[v].no);
			}
		}

		m_armobj->ApplyPose();

		if (m_armobj->GetVertDeformType() == ARM_VDEF_BGE_CPU)
			BGEDeformVerts();
		else
			BlenderDeformVerts();

		/* Update the current frame */
		m_lastArmaUpdate=m_armobj->GetLastFrame();

		m_armobj->RestorePose();
		/* dynamic vertex, cannot use display list */
		m_bDynamic = true;

		UpdateTransverts();

		m_poseApplied = false;

		/* indicate that the m_transverts and normals are up to date */
		return true;
	}

	return false;
}

bool BL_SkinDeformer::Update(void)
{
	return UpdateInternal(false);
}

/* XXX note: I propose to drop this function */
void BL_SkinDeformer::SetArmature(BL_ArmatureObject *armobj)
{
	// only used to set the object now
	m_armobj = armobj;
}

/* ------------------------------------------------------------------------- */
/* BGE-Remix P8 Part B (P8.13): GPU skinning */

RAS_GPUSkin *BL_SkinDeformer::GetGPUSkin()
{
	return (m_gpustate == 1 && !m_remixGPU) ? m_gpuskin : NULL;
}

bool BL_SkinDeformer::GPUSkinActive()
{
	if (m_gpustate == 0) {
		m_gpustate = BuildGPUSkin() ? 1 : -1;
		if (m_gpustate == 1)
			printf("BGE-Remix: %s is deformed on the graphics card (%d vertices, %d groups, %d keys)\n",
			       m_objMesh->id.name + 2, m_gpuskin->nverts, m_gpuskin->ngroups, m_gpuskin->nkeys);
	}
	if (m_gpustate == 1 && (!GPU_speed_feature("gpu_skinning") || m_pMeshObject->CPUVerticesWanted())) {
		/* on the CPU from now on (the arrays are made again this frame) */
		m_gpustate = -1;
		delete m_gpuskin;
		m_gpuskin = NULL;
		ForceUpdate();
		m_poseApplied = false;
	}
	return m_gpustate == 1;
}

bool BL_SkinDeformer::BuildGPUSkin()
{
	if (!GPUSkinAllowed() || !GPU_speed_feature("gpu_skinning") || !m_bmesh || !m_pMeshObject ||
	    m_pMeshObject->CPUVerticesWanted() || m_remixGPU)
	{
		return false;
	}
	/* the armature: what RemixCanSkin allows (vertex groups, linear blending) */
	if (m_armobj) {
		std::string why;
		if (!RemixCanSkin(why))
			return false;
	}

	/* alpha-sorted faces are sorted on the CPU, from the CPU's vertices */
	for (list<RAS_MeshMaterial>::iterator mit = m_pMeshObject->GetFirstMaterial();
	     mit != m_pMeshObject->GetLastMaterial(); ++mit)
	{
		if (mit->m_bucket && mit->m_bucket->IsZSort())
			return false;
	}

	RAS_GPUSkin *skin = new RAS_GPUSkin();
	m_gpuskin = skin;
	const int nverts = m_bmesh->totvert;
	skin->nverts = nverts;
	skin->rest.resize((size_t)nverts * 8);
	for (int v = 0; v < nverts; v++) {
		float n[3];
		normal_short_to_float_v3(n, m_bmesh->mvert[v].no);
		float *r = &skin->rest[(size_t)v * 8];
		copy_v3_v3(r, m_bmesh->mvert[v].co);
		r[3] = 1.0f;
		copy_v3_v3(r + 4, n);
		r[7] = 0.0f;
	}

	/* every deforming vertex group weight */
	skin->woffset.assign((size_t)nverts + 1, 0);
	if (m_armobj && m_bmesh->dvert) {
		m_armobj->ApplyPose();
		InitGroupMap();
		m_armobj->RestorePose();
		const int groups = BLI_listbase_count(&m_objMesh->defbase);
		skin->ngroups = groups;
		MDeformVert *dv = m_bmesh->dvert;
		for (int v = 0; v < nverts; v++, dv++) {
			skin->woffset[v] = (unsigned int)(skin->weights.size() / 2);
			MDeformWeight *dw = dv->dw;
			for (int j = 0; j < dv->totweight; j++, dw++) {
				if (dw->def_nr < groups && m_dfnrToPC[dw->def_nr] && dw->weight > 0.0f) {
					unsigned int bits;
					memcpy(&bits, &dw->weight, sizeof(bits));
					skin->weights.push_back((unsigned int)dw->def_nr);
					skin->weights.push_back(bits);
				}
			}
		}
		skin->woffset[nverts] = (unsigned int)(skin->weights.size() / 2);
		skin->bones.assign((size_t)groups * 32, 0.0f);
	}

	if (!GPUSkinKeys(true)) {
		delete skin;
		m_gpuskin = NULL;
		return false;
	}

	/* normals as the CPU makes them: the strongest bone's (BGE's own skinning),
	 * made again from the faces (Blender's, and shape keys), or kept */
	if (m_armobj && m_armobj->GetVertDeformType() == ARM_VDEF_BGE_CPU)
		skin->normals = RAS_GPUSkin::NORMALS_BONE;
	else if (m_recalcNormal)
		skin->normals = RAS_GPUSkin::NORMALS_FACES;
	else
		skin->normals = RAS_GPUSkin::NORMALS_REST;

	/* the faces of every material, and each array's vertices */
	std::vector<std::vector<unsigned int> > vfaces((size_t)nverts);
	for (list<RAS_MeshMaterial>::iterator mit = m_pMeshObject->GetFirstMaterial();
	     mit != m_pMeshObject->GetLastMaterial(); ++mit)
	{
		if (!mit->m_slots[(void *)m_gameobj])
			continue;
		RAS_MeshSlot *slot = *mit->m_slots[(void *)m_gameobj];
		RAS_MeshSlot::iterator it;
		for (slot->begin(it); !slot->end(it); slot->next(it)) {
			RAS_GPUSkin::Array &arr = skin->arrays[it.array];
			if (arr.orig.empty()) {
				arr.orig.resize(it.array->m_vertex.size());
				arr.face.assign(it.array->m_vertex.size(), ~0u);
				for (size_t i = 0; i < it.array->m_vertex.size(); i++)
					arr.orig[i] = it.array->m_vertex[i].getOrigIndex();
			}
			const int nvert = (int)it.array->m_type;
			if (nvert < 3)
				continue;
			for (size_t i = 0; i + nvert <= it.totindex; i += nvert) {
				const unsigned int face = (unsigned int)(skin->faces.size() / 4);
				for (int k = 0; k < 4; k++) {
					if (k < nvert) {
						const unsigned short di = it.index[i + k];
						const unsigned int mv = it.array->m_vertex[di].getOrigIndex();
						skin->faces.push_back(mv);
						if (mv < (unsigned int)nverts)
							vfaces[mv].push_back(face);
						if (it.array->m_vertex[di].getFlag() & RAS_TexVert::FLAT)
							arr.face[di] = face;
					}
					else {
						skin->faces.push_back(~0u);
					}
				}
			}
		}
	}
	skin->vfoffset.resize((size_t)nverts + 1);
	for (int v = 0; v < nverts; v++) {
		skin->vfoffset[v] = (unsigned int)skin->vfaces.size();
		skin->vfaces.insert(skin->vfaces.end(), vfaces[v].begin(), vfaces[v].end());
	}
	skin->vfoffset[nverts] = (unsigned int)skin->vfaces.size();

	UpdateGPUSkin();
	return true;
}

bool BL_SkinDeformer::UpdateGPUSkin()
{
	RAS_GPUSkin *skin = m_gpuskin;
	bool changed = GPUSkinKeys(false);
	if (m_armobj && skin->ngroups > 0 && PoseUpdated()) {
		m_armobj->ApplyPose();
		InitGroupMap();
		/* as BGEDeformVerts: mesh space to armature space, the pose channel,
		 * and back; and the channel's own matrix (BGE's normals) */
		Eigen::Matrix4f post_mat = Eigen::Matrix4f::Map((float*)m_obmat).inverse() *
		                           Eigen::Matrix4f::Map((float*)m_armobj->GetArmatureObject()->obmat);
		Eigen::Matrix4f pre_mat = post_mat.inverse();
		for (int i = 0; i < skin->ngroups; i++) {
			Eigen::Matrix4f m = Eigen::Matrix4f::Identity(), c = Eigen::Matrix4f::Identity();
			if (m_dfnrToPC[i]) {
				c = Eigen::Matrix4f::Map((float*)m_dfnrToPC[i]->chan_mat);
				m = post_mat * c * pre_mat;
			}
			memcpy(&skin->bones[(size_t)i * 32], m.data(), sizeof(float) * 16);
			memcpy(&skin->bones[(size_t)i * 32 + 16], c.data(), sizeof(float) * 16);
		}
		m_armobj->RestorePose();
		m_lastArmaUpdate = m_armobj->GetLastFrame();
		changed = true;
	}
	if (changed)
		skin->frame++;
	return changed;
}
