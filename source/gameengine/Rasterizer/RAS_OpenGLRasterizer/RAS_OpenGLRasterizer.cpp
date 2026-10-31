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

/** \file gameengine/Rasterizer/RAS_OpenGLRasterizer/RAS_OpenGLRasterizer.cpp
 *  \ingroup bgerastogl
 */

 
#include <math.h>
#include <stdlib.h>
 
#include "RAS_OpenGLRasterizer.h"

#include "GPU_glew.h"

#include "RAS_ICanvas.h"
#include "RAS_Rect.h"
#include "RAS_TexVert.h"
#include "RAS_MeshObject.h"
#include "RAS_Polygon.h"
#include "RAS_ILightObject.h"
#include "MT_CmMatrix4x4.h"

#include "RAS_OpenGLLight.h"
#include "RAS_OpenGLOffScreen.h"
#include "RAS_OpenGLSync.h"

#include "RAS_StorageBatch.h"
#include "RAS_Pipeline.h"
#include "RAS_GPUCulling.h"

#include "GPU_draw.h"
#include "GPU_extensions.h"
#include "GPU_material.h"
#include "GPU_shader.h"

extern "C"{
	#include "BLF_api.h"
	#include "BKE_DerivedMesh.h"
}


// XXX Clean these up <<<
#include "EXP_Value.h"
#include "KX_Scene.h"
#include "KX_RayCast.h"
#include "KX_GameObject.h"
// >>>

#ifndef M_PI
#define M_PI		3.14159265358979323846
#endif

/**
 *  32x32 bit masks for vinterlace stereo mode
 */
static GLuint left_eye_vinterlace_mask[32];
static GLuint right_eye_vinterlace_mask[32];

/**
 *  32x32 bit masks for hinterlace stereo mode.
 *  Left eye = &hinterlace_mask[0]
 *  Right eye = &hinterlace_mask[1]
 */
static GLuint hinterlace_mask[33];

RAS_OpenGLRasterizer::RAS_OpenGLRasterizer(RAS_ICanvas* canvas, RAS_STORAGE_TYPE storage)
	:RAS_IRasterizer(canvas),
	m_2DCanvas(canvas),
	m_fogenabled(false),
	m_time(0.0f),
	m_campos(0.0f, 0.0f, 0.0f),
	m_camortho(false),
	m_camnegscale(false),
	m_stereomode(RAS_STEREO_NOSTEREO),
	m_curreye(RAS_STEREO_LEFTEYE),
	m_eyeseparation(0.0f),
	m_focallength(0.0f),
	m_setfocallength(false),
	m_noOfScanlines(32),
	m_motionblur(0),
	m_motionblurvalue(-1.0f),
	m_usingoverrideshader(false),
	m_clientobject(NULL),
	m_auxilaryClientInfo(NULL),
	m_drawingmode(KX_TEXTURED),
	m_texco_num(0),
	m_attrib_num(0),
	//m_last_alphablend(GPU_BLEND_SOLID),
	m_last_frontface(true),
	m_materialCachingInfo(0),
	m_storage_type(storage)
{
	m_viewmatrix.setIdentity();
	m_viewinvmatrix.setIdentity();
	
	for (int i = 0; i < 32; i++)
	{
		left_eye_vinterlace_mask[i] = 0x55555555;
		right_eye_vinterlace_mask[i] = 0xAAAAAAAA;
		hinterlace_mask[i] = (i&1)*0xFFFFFFFF;
	}
	hinterlace_mask[32] = 0;

	m_prevafvalue = GPU_get_anisotropic();

	/* BGE-Remix P8.4: the game's meshes are batches on the card, whatever
	 * storage the file asks for (Display Lists, Vertex Arrays or VBO) */
	m_classic = false;
	m_mbtex[0] = m_mbtex[1] = 0;
	m_mbsize[0] = m_mbsize[1] = 0;
	m_mbshader = NULL;
	m_stereotex = 0;
	m_stereorect[0] = m_stereorect[1] = m_stereorect[2] = m_stereorect[3] = 0;
	m_stereoleft = false;
	m_stereoshader = NULL;
	m_pipeline = new RAS_Pipeline();
	m_culling = new RAS_GPUCulling();
	m_mainview_drawn = false;
	m_scene_first = false;
	static_cast<RAS_StorageBatch *>(m_storage)->SetCulling(m_culling);
	GPU_classic_state_default(&m_state);
	m_storage = new RAS_StorageBatch(&m_texco_num, m_texco, &m_attrib_num, m_attrib, m_attrib_layer, &m_classic,
	                                 &m_state);

	/* BGE-Remix P8 Part B: eight lamps, as the old fixed-function lighting had */
	m_numgllights = 8;
}



RAS_OpenGLRasterizer::~RAS_OpenGLRasterizer()
{
	// Restore the previous AF value
	GPU_set_anisotropic(m_prevafvalue);

	if (m_storage)
		delete m_storage;
	FreeMotionBlur();
	delete m_pipeline;
	delete m_culling;
}

bool RAS_OpenGLRasterizer::Init()
{
	bool storage_init;
	GPU_state_init();


	m_ambr = 0.0f;
	m_ambg = 0.0f;
	m_ambb = 0.0f;
	GPU_classic_state_default(&m_state);

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	//m_last_alphablend = GPU_BLEND_SOLID;
	GPU_set_material_alpha_blend(GPU_BLEND_SOLID);
	m_state.alpha_test = false;

	glFrontFace(GL_CCW);
	m_last_frontface = true;

	m_redback = 0.4375f;
	m_greenback = 0.4375f;
	m_blueback = 0.4375f;
	m_alphaback = 0.0f;

	glClearColor(m_redback,m_greenback,m_blueback,m_alphaback);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


	glShadeModel(GL_SMOOTH);

	storage_init = m_storage->Init();

	return true && storage_init;
}


void RAS_OpenGLRasterizer::SetAmbientColor(float color[3])
{
	m_ambr = color[0];
	m_ambg = color[1];
	m_ambb = color[2];
}

void RAS_OpenGLRasterizer::SetAmbient(float factor)
{
	m_state.scene_ambient[0] = m_ambr * factor;
	m_state.scene_ambient[1] = m_ambg * factor;
	m_state.scene_ambient[2] = m_ambb * factor;
	m_state.scene_ambient[3] = 1.0f;
}

void RAS_OpenGLRasterizer::SetBackColor(float color[3])
{
	m_redback = color[0];
	m_greenback = color[1];
	m_blueback = color[2];
	m_alphaback = 0.0f;
}

void RAS_OpenGLRasterizer::SetFogHeight(float height)
{
	m_state.fog_height = height;
}

void RAS_OpenGLRasterizer::SetFog(short type, float start, float dist, float intensity, float color[3])
{
	/* BGE-Remix P8 Part B: the game's state, not glFog */
	m_state.fog_density = intensity / 10.0f;
	m_state.fog_start = start;
	m_state.fog_end = start + dist;
	m_state.fog_color[0] = color[0];
	m_state.fog_color[1] = color[1];
	m_state.fog_color[2] = color[2];
	m_state.fog_color[3] = 1.0f;
}

void RAS_OpenGLRasterizer::EnableFog(bool enable)
{
	m_fogenabled = enable;
}

void RAS_OpenGLRasterizer::DisplayFog()
{
	m_state.fog = ((m_drawingmode >= KX_SOLID) && m_fogenabled) ? GPU_FOG_LINEAR : GPU_FOG_OFF;
}

bool RAS_OpenGLRasterizer::SetMaterial(const RAS_IPolyMaterial& mat)
{
	return mat.Activate(this, m_materialCachingInfo);
}



void RAS_OpenGLRasterizer::Exit()
{
	m_storage->Exit();

	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glClearDepth(1.0f);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(m_redback, m_greenback, m_blueback, m_alphaback);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDepthMask (GL_TRUE);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_ONE, GL_ZERO);
	
	glDisable(GL_POLYGON_STIPPLE);
	
	glDisable(GL_LIGHTING);
	if (GLEW_EXT_separate_specular_color || GLEW_VERSION_1_2)
		glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL, GL_SINGLE_COLOR);
	
	EndFrame();
}

bool RAS_OpenGLRasterizer::BeginFrame(double time)
{
	m_time = time;

	// Blender camera routine destroys the settings
	if (m_drawingmode < KX_SOLID)
	{
		glDisable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
	}
	else
	{
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
	}

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	//m_last_alphablend = GPU_BLEND_SOLID;
	GPU_set_material_alpha_blend(GPU_BLEND_SOLID);
	m_state.alpha_test = false;

	glFrontFace(GL_CCW);
	m_last_frontface = true;

	glShadeModel(GL_SMOOTH);

	glEnable(GL_MULTISAMPLE_ARB);

	/* BGE-Remix P8 Part B: the layer's matrices start from the old ones
	 * (the canvas and the 3D view set them) */
	GPU_matrix_from_gl();

	m_2DCanvas->BeginFrame();

	// Render Tools
	m_clientobject = NULL;
	m_lastlightlayer = -1;
	m_lastauxinfo = NULL;
	m_lastlighting = true; /* force disable in DisableOpenGLLights() */
	DisableOpenGLLights();

	m_storage->BeginFrame();
	
	return true;
}



void RAS_OpenGLRasterizer::SetDrawingMode(int drawingmode)
{
	m_drawingmode = drawingmode;

	if (m_drawingmode == KX_WIREFRAME)
		glDisable(GL_CULL_FACE);

	m_storage->SetDrawingMode(drawingmode);
}

int RAS_OpenGLRasterizer::GetDrawingMode()
{
	return m_drawingmode;
}


void RAS_OpenGLRasterizer::SetDepthMask(DepthMask depthmask)
{
	glDepthMask(depthmask == KX_DEPTHMASK_DISABLED ? GL_FALSE : GL_TRUE);
}


void RAS_OpenGLRasterizer::ClearColorBuffer()
{
	m_2DCanvas->ClearColor(m_redback,m_greenback,m_blueback,m_alphaback);
	m_2DCanvas->ClearBuffer(RAS_ICanvas::COLOR_BUFFER);
}


void RAS_OpenGLRasterizer::ClearDepthBuffer()
{
	m_2DCanvas->ClearBuffer(RAS_ICanvas::DEPTH_BUFFER);
}


void RAS_OpenGLRasterizer::ClearCachingInfo(void)
{
	m_materialCachingInfo = 0;
}

void RAS_OpenGLRasterizer::FlushDebugShapes(SCA_IScene *scene)
{
	std::vector<OglDebugShape> &debugShapes = m_debugShapes[scene];
	if (debugShapes.empty())
		return;

	/* BGE-Remix P8 Part B: debug and physics lines (and bge.render's
	 * drawLine) with the layer's line shader, all of a frame in one draw */
	GPU_line_style(1.0f, 0.0f, 0.0f);
	GPU_imm_begin(GL_LINES, GPU_BUILTIN_LINE);
	for (unsigned int i = 0; i < debugShapes.size(); i++) {
		const OglDebugShape& sh = debugShapes[i];
		GPU_imm_color4f(sh.m_color[0], sh.m_color[1], sh.m_color[2], 1.0f);
		if (sh.m_type == OglDebugShape::LINE) {
			GPU_imm_vertex3f(sh.m_pos.x(), sh.m_pos.y(), sh.m_pos.z());
			GPU_imm_vertex3f(sh.m_param.x(), sh.m_param.y(), sh.m_param.z());
			continue;
		}
		/* a circle: its segments */
		static const MT_Vector3 worldUp(0.0f, 0.0f, 1.0f);
		MT_Vector3 norm = sh.m_param;
		MT_Matrix3x3 tr;
		if (norm.fuzzyZero() || norm == worldUp)
		{
			tr.setIdentity();
		}
		else
		{
			MT_Vector3 xaxis, yaxis;
			xaxis = MT_cross(norm, worldUp);
			yaxis = MT_cross(xaxis, norm);
			tr.setValue(xaxis.x(), xaxis.y(), xaxis.z(),
				yaxis.x(), yaxis.y(), yaxis.z(),
				norm.x(), norm.y(), norm.z());
		}
		MT_Scalar rad = sh.m_param2.x();
		int n = (int)sh.m_param2.y();
		for (int j = 0; j < n; j++) {
			for (int e = 0; e < 2; e++) {
				MT_Scalar theta = ((j + e) % n) * (float)M_PI * 2 / n;
				MT_Vector3 pos(cosf(theta) * rad, sinf(theta) * rad, 0.0f);
				pos = pos * tr;
				pos += sh.m_pos;
				GPU_imm_vertex3f(pos.x(), pos.y(), pos.z());
			}
		}
	}
	GPU_imm_end();

	debugShapes.clear();
}

void RAS_OpenGLRasterizer::EndFrame()
{
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glDisable(GL_MULTISAMPLE_ARB);

	m_2DCanvas->EndFrame();
}

void RAS_OpenGLRasterizer::SetRenderArea()
{
	RAS_Rect area;
	// only above/below stereo method needs viewport adjustment
	switch (m_stereomode)
	{
		case RAS_STEREO_ABOVEBELOW:
			switch (m_curreye) {
				case RAS_STEREO_LEFTEYE:
					// upper half of window
					area.SetLeft(0);
					area.SetBottom(m_2DCanvas->GetHeight() -
						int(m_2DCanvas->GetHeight() - m_noOfScanlines) / 2);
	
					area.SetRight(int(m_2DCanvas->GetWidth()));
					area.SetTop(int(m_2DCanvas->GetHeight()));
					m_2DCanvas->SetDisplayArea(&area);
					break;
				case RAS_STEREO_RIGHTEYE:
					// lower half of window
					area.SetLeft(0);
					area.SetBottom(0);
					area.SetRight(int(m_2DCanvas->GetWidth()));
					area.SetTop(int(m_2DCanvas->GetHeight() - m_noOfScanlines) / 2);
					m_2DCanvas->SetDisplayArea(&area);
					break;
			}
			break;
		case RAS_STEREO_3DTVTOPBOTTOM:
			switch (m_curreye) {
				case RAS_STEREO_LEFTEYE:
					// upper half of window
					area.SetLeft(0);
					area.SetBottom(m_2DCanvas->GetHeight() -
						m_2DCanvas->GetHeight() / 2);
	
					area.SetRight(m_2DCanvas->GetWidth());
					area.SetTop(m_2DCanvas->GetHeight());
					m_2DCanvas->SetDisplayArea(&area);
					break;
				case RAS_STEREO_RIGHTEYE:
					// lower half of window
					area.SetLeft(0);
					area.SetBottom(0);
					area.SetRight(m_2DCanvas->GetWidth());
					area.SetTop(m_2DCanvas->GetHeight() / 2);
					m_2DCanvas->SetDisplayArea(&area);
					break;
			}
			break;
		case RAS_STEREO_SIDEBYSIDE:
			switch (m_curreye)
			{
				case RAS_STEREO_LEFTEYE:
					// Left half of window
					area.SetLeft(0);
					area.SetBottom(0);
					area.SetRight(m_2DCanvas->GetWidth()/2);
					area.SetTop(m_2DCanvas->GetHeight());
					m_2DCanvas->SetDisplayArea(&area);
					break;
				case RAS_STEREO_RIGHTEYE:
					// Right half of window
					area.SetLeft(m_2DCanvas->GetWidth()/2);
					area.SetBottom(0);
					area.SetRight(m_2DCanvas->GetWidth());
					area.SetTop(m_2DCanvas->GetHeight());
					m_2DCanvas->SetDisplayArea(&area);
					break;
			}
			break;
		default:
			// every available pixel
			area.SetLeft(0);
			area.SetBottom(0);
			area.SetRight(int(m_2DCanvas->GetWidth()));
			area.SetTop(int(m_2DCanvas->GetHeight()));
			m_2DCanvas->SetDisplayArea(&area);
			break;
	}
}
	
void RAS_OpenGLRasterizer::SetStereoMode(const StereoMode stereomode)
{
	m_stereomode = stereomode;
}

RAS_IRasterizer::StereoMode RAS_OpenGLRasterizer::GetStereoMode()
{
	return m_stereomode;
}

bool RAS_OpenGLRasterizer::Stereo()
{
	if (m_stereomode > RAS_STEREO_NOSTEREO) // > 0
		return true;
	else
		return false;
}

bool RAS_OpenGLRasterizer::InterlacedStereo()
{
	return m_stereomode == RAS_STEREO_VINTERLACE || m_stereomode == RAS_STEREO_INTERLACED;
}

void RAS_OpenGLRasterizer::SetEye(const StereoEye eye)
{
	m_curreye = eye;
	switch (m_stereomode)
	{
		case RAS_STEREO_QUADBUFFERED:
			glDrawBuffer(m_curreye == RAS_STEREO_LEFTEYE ? GL_BACK_LEFT : GL_BACK_RIGHT);
			break;
		case RAS_STEREO_ANAGLYPH:
			if (m_curreye == RAS_STEREO_LEFTEYE) {
				glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
			}
			else {
				//glAccum(GL_LOAD, 1.0f);
				glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE);
				ClearDepthBuffer();
			}
			break;
		case RAS_STEREO_VINTERLACE:
		case RAS_STEREO_INTERLACED:
		{
			/* BGE-Remix P8 Part B: no polygon stipple (it does not reach
			 * shaders): the left eye's whole picture is kept, the right eye
			 * draws over it, and FinishStereo puts the left eye's columns
			 * (vertical interlace) or rows back, as the stipple masks chose
			 * them (odd window columns or rows) */
			if (m_curreye == RAS_STEREO_RIGHTEYE) {
				const RAS_Rect& area = m_2DCanvas->GetWindowArea();
				const int r[4] = {area.GetLeft(), area.GetBottom(), area.GetWidth() + 1, area.GetHeight() + 1};
				if (r[2] > 0 && r[3] > 0) {
					if (!m_stereotex || r[2] != m_stereorect[2] || r[3] != m_stereorect[3]) {
						if (m_stereotex)
							glDeleteTextures(1, &m_stereotex);
						glCreateTextures(GL_TEXTURE_2D, 1, &m_stereotex);
						glTextureStorage2D(m_stereotex, 1, GL_RGBA8, r[2], r[3]);
						glTextureParameteri(m_stereotex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
						glTextureParameteri(m_stereotex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
					}
					memcpy(m_stereorect, r, sizeof(r));
					glCopyTextureSubImage2D(m_stereotex, 0, 0, 0, r[0], r[1], r[2], r[3]);
					m_stereoleft = true;
				}
				ClearDepthBuffer();
			}
			else {
				m_stereoleft = false;
			}
			break;
		}
		default:
			break;
	}
}

RAS_IRasterizer::StereoEye RAS_OpenGLRasterizer::GetEye()
{
	return m_curreye;
}


void RAS_OpenGLRasterizer::SetEyeSeparation(const float eyeseparation)
{
	m_eyeseparation = eyeseparation;
}

float RAS_OpenGLRasterizer::GetEyeSeparation()
{
	return m_eyeseparation;
}

void RAS_OpenGLRasterizer::SetFocalLength(const float focallength)
{
	m_focallength = focallength;
	m_setfocallength = true;
}

float RAS_OpenGLRasterizer::GetFocalLength()
{
	return m_focallength;
}

RAS_IOffScreen *RAS_OpenGLRasterizer::CreateOffScreen(int width, int height, int samples, int target)
{
	RAS_IOffScreen *ofs;

	ofs = new RAS_OpenGLOffScreen(m_2DCanvas);

	if (!ofs->Create(width, height, samples, (RAS_IOffScreen::RAS_OFS_RENDER_TARGET)target)) {
		delete ofs;
		return NULL;
	}
	return ofs;
}

RAS_ISync *RAS_OpenGLRasterizer::CreateSync(int type)
{
	RAS_ISync *sync;

	sync = new RAS_OpenGLSync();

	if (!sync->Create((RAS_ISync::RAS_SYNC_TYPE)type)) {
		delete sync;
		return NULL;
	}
	return sync;
}

void RAS_OpenGLRasterizer::SwapBuffers()
{
	m_2DCanvas->SwapBuffers();
}



const MT_Matrix4x4& RAS_OpenGLRasterizer::GetViewMatrix() const
{
	return m_viewmatrix;
}

const MT_Matrix4x4& RAS_OpenGLRasterizer::GetViewInvMatrix() const
{
	return m_viewinvmatrix;
}

void RAS_OpenGLRasterizer::IndexPrimitives_3DText(RAS_MeshSlot& ms,
									class RAS_IPolyMaterial* polymat)
{ 
	/* BGE-Remix P8 Part B: Face Texture text through the layer (the image
	 * shader, the layer's matrices) */
	bool obcolor = ms.m_bObjectColor;
	MT_Vector4& rgba = ms.m_RGBAcolor;
	RAS_MeshSlot::iterator it;
	float obcol[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	if (obcolor) {
		for (int k = 0; k < 4; k++)
			obcol[k] = (float)rgba[k];
	}

	const STR_String& mytext = ((CValue*)m_clientobject)->GetPropertyText("Text");

	for (ms.begin(it); !ms.end(it); ms.next(it)) {
		RAS_TexVert *vertex;
		size_t i, j, numvert;
		
		numvert = it.array->m_type;

		if (it.array->m_type == RAS_DisplayArray::LINE) {
			// line drawing, no text
			GPU_line_style(1.0f, 0.0f, 0.0f);
			GPU_imm_begin(GL_LINES, GPU_BUILTIN_LINE);
			GPU_imm_color4f(obcol[0], obcol[1], obcol[2], obcol[3]);
			for (i=0; i<it.totindex; i+=2)
			{
				vertex = &it.vertex[it.index[i]];
				GPU_imm_vertex3f(vertex->getXYZ()[0], vertex->getXYZ()[1], vertex->getXYZ()[2]);

				vertex = &it.vertex[it.index[i+1]];
				GPU_imm_vertex3f(vertex->getXYZ()[0], vertex->getXYZ()[1], vertex->getXYZ()[2]);
			}
			GPU_imm_end();
		}
		else {
			// triangle and quad text drawing
			for (i=0; i<it.totindex; i+=numvert)
			{
				float  v[4][3];
				const float  *v_ptr[4] = {NULL};
				const float *uv_ptr[4] = {NULL};

				for (j=0; j<numvert; j++) {
					vertex = &it.vertex[it.index[i+j]];

					v[j][0] = vertex->getXYZ()[0];
					v[j][1] = vertex->getXYZ()[1];
					v[j][2] = vertex->getXYZ()[2];
					v_ptr[j] = v[j];

					uv_ptr[j] = vertex->getUV(0);
				}

				GPU_render_text_layer(
				        polymat->GetMTexPoly(), polymat->GetDrawingMode(), mytext, mytext.Length(), polymat->GetMCol(),
				        v_ptr, uv_ptr, obcolor ? obcol : NULL);

				ClearCachingInfo();
			}
		}
	}
}

void RAS_OpenGLRasterizer::SetTexCoordNum(int num)
{
	m_texco_num = num;
	if (m_texco_num > RAS_MAX_TEXCO)
		m_texco_num = RAS_MAX_TEXCO;
}

void RAS_OpenGLRasterizer::SetAttribNum(int num)
{
	m_attrib_num = num;
	if (m_attrib_num > RAS_MAX_ATTRIB)
		m_attrib_num = RAS_MAX_ATTRIB;
}

void RAS_OpenGLRasterizer::SetTexCoord(TexCoGen coords, int unit)
{
	// this changes from material to material
	if (unit < RAS_MAX_TEXCO)
		m_texco[unit] = coords;
}

void RAS_OpenGLRasterizer::SetAttrib(TexCoGen coords, int unit, int layer)
{
	// this changes from material to material
	if (unit < RAS_MAX_ATTRIB) {
		m_attrib[unit] = coords;
		m_attrib_layer[unit] = layer;
	}
}

void RAS_OpenGLRasterizer::IndexPrimitives(RAS_MeshSlot& ms)
{
	/* BGE-Remix P8 Part B: a derived mesh (modifiers) is drawn as batches
	 * too (the slot's m_pDerivedArrays); Blender's mesh drawing is not used */
	m_storage->IndexPrimitives(ms);
}

/* BGE-Remix P8 Part B: the old OpenGL matrices are set from the layer's only
 * where old drawing still reads them (Python's bgl in drawing callbacks, a
 * shader whose translation did not build) */
void RAS_OpenGLRasterizer::BeginScene(struct Scene *scene, bool first, int samples)
{
	/* the canvas in window coordinates, and the part the frame cleared (the
	 * viewport ClearFrame left) */
	GLint clear[4], area[4], fb = 0;
	glGetIntegerv(GL_VIEWPORT, clear);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
	if (fb == 0) {
		m_2DCanvas->SetViewPort(0, 0, m_2DCanvas->GetWidth(), m_2DCanvas->GetHeight());
		glGetIntegerv(GL_VIEWPORT, area);
	}
	else {
		/* drawing into an offscreen target (bge.texture, bge.render): its viewport */
		memcpy(area, clear, sizeof(area));
	}
	const float background[3] = {m_redback, m_greenback, m_blueback};
	m_pipeline->Begin(scene, first, background, samples, area, clear);
	glViewport(clear[0], clear[1], clear[2], clear[3]);
	m_scene_first = first && fb == 0 && !Stereo();
	m_mainview_drawn = false;
}

void RAS_OpenGLRasterizer::EndScene()
{
	/* BGE-Remix P8 Part B (P8.11): the first scene's main view makes the
	 * depth pyramid the next frame culls against (its depth: the pipeline's
	 * target, or the window's) */
	if (m_scene_first && m_mainview_drawn) {
		unsigned int depth = 0;
		bool ms = false;
		int vp[4];
		m_pipeline->DepthTexture(&depth, &ms, vp);
		/* the main camera's viewport (the pipeline's target is the window's size) */
		memcpy(vp, m_mainview_vp, sizeof(vp));
		float view[16], proj[16];
		memcpy(view, m_mainview_view, sizeof(view));
		memcpy(proj, m_mainview_proj, sizeof(proj));
		m_culling->Build(depth, ms, vp, view, proj);
	}
	else if (m_scene_first) {
		m_culling->Invalidate();
	}
	m_scene_first = false;
	m_pipeline->End();
	glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
}

bool RAS_OpenGLRasterizer::IndexPrimitivesInstanced(std::vector<RAS_MeshSlot *>& slots)
{
	float view[4][4];
	GPU_matrix_get(GPU_MATRIX_MODELVIEW, view);
	return static_cast<RAS_StorageBatch *>(m_storage)->IndexPrimitivesInstanced(slots, &view[0][0]);
}

void RAS_OpenGLRasterizer::SetMainView(bool on)
{
	if (!on && m_culling->MainView()) {
		/* the main view was drawn into this viewport */
		glGetIntegerv(GL_VIEWPORT, m_mainview_vp);
	}
	m_culling->SetMainView(on && m_scene_first);
	if (on && m_scene_first) {
		m_mainview_drawn = true;
		/* the view this frame's pyramid is drawn with */
		float v[4][4], p[4][4];
		GPU_matrix_get(GPU_MATRIX_MODELVIEW, v);
		GPU_matrix_get(GPU_MATRIX_PROJECTION, p);
		memcpy(m_mainview_view, v, sizeof(m_mainview_view));
		memcpy(m_mainview_proj, p, sizeof(m_mainview_proj));
	}
}

void RAS_OpenGLRasterizer::GetInstancingStats(int *r_draws, int *r_objects)
{
	RAS_StorageBatch *storage = static_cast<RAS_StorageBatch *>(m_storage);
	*r_draws = storage->InstancedDraws();
	*r_objects = storage->InstancedObjects();
}

void RAS_OpenGLRasterizer::SyncOldMatrices()
{
	GPU_matrix_to_gl();
}

void RAS_OpenGLRasterizer::SetProjectionMatrix(MT_CmMatrix4x4 &mat)
{
	float* matrix = &mat(0, 0);
	GPU_matrix_mode(GPU_MATRIX_PROJECTION);
	GPU_matrix_load((const float (*)[4])matrix);
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);

	m_camortho = (mat(3, 3) != 0.0f);
}

void RAS_OpenGLRasterizer::SetProjectionMatrix(const MT_Matrix4x4 & mat)
{
	float matrix[16];
	/* Get into argument. Looks a bit dodgy, but it's ok. */
	mat.getValue(matrix);
	GPU_matrix_mode(GPU_MATRIX_PROJECTION);
	GPU_matrix_load((const float (*)[4])matrix);
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);

	m_camortho = (mat[3][3] != 0.0f);
}

MT_Matrix4x4 RAS_OpenGLRasterizer::GetFrustumMatrix(
	float left,
	float right,
	float bottom,
	float top,
	float frustnear,
	float frustfar,
	float focallength,
	bool 
) {
	MT_Matrix4x4 result;
	float mat[16];

	// correction for stereo
	if (Stereo())
	{
			float near_div_focallength;
			float offset;

			// if Rasterizer.setFocalLength is not called we use the camera focallength
			if (!m_setfocallength)
				// if focallength is null we use a value known to be reasonable
				m_focallength = (focallength == 0.f) ? m_eyeseparation * 30.0f
					: focallength;

			near_div_focallength = frustnear / m_focallength;
			offset = 0.5f * m_eyeseparation * near_div_focallength;
			switch (m_curreye) {
				case RAS_STEREO_LEFTEYE:
						left += offset;
						right += offset;
						break;
				case RAS_STEREO_RIGHTEYE:
						left -= offset;
						right -= offset;
						break;
			}
			// leave bottom and top untouched
			if (m_stereomode == RAS_STEREO_3DTVTOPBOTTOM) {
				// restore the vertical frustum because the 3DTV will
				// expand the top and bottom part to the full size of the screen
				bottom *= 2.0f;
				top *= 2.0f;
			}
	}
	
	/* glFrustum's matrix, column-major */
	memset(mat, 0, sizeof(mat));
	mat[0] = 2.0f * frustnear / (right - left);
	mat[5] = 2.0f * frustnear / (top - bottom);
	mat[8] = (right + left) / (right - left);
	mat[9] = (top + bottom) / (top - bottom);
	mat[10] = -(frustfar + frustnear) / (frustfar - frustnear);
	mat[11] = -1.0f;
	mat[14] = -2.0f * frustfar * frustnear / (frustfar - frustnear);
	result.setValue(mat);

	/* it is also the projection now, as glFrustum made it (callers rely on it) */
	GPU_matrix_mode(GPU_MATRIX_PROJECTION);
	GPU_matrix_load((const float (*)[4])mat);
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);

	return result;
}

MT_Matrix4x4 RAS_OpenGLRasterizer::GetOrthoMatrix(
	float left,
	float right,
	float bottom,
	float top,
	float frustnear,
	float frustfar
) {
	MT_Matrix4x4 result;
	float mat[16];

	// stereo is meaningless for orthographic, disable it
	/* glOrtho's matrix, column-major */
	memset(mat, 0, sizeof(mat));
	mat[0] = 2.0f / (right - left);
	mat[5] = 2.0f / (top - bottom);
	mat[10] = -2.0f / (frustfar - frustnear);
	mat[12] = -(right + left) / (right - left);
	mat[13] = -(top + bottom) / (top - bottom);
	mat[14] = -(frustfar + frustnear) / (frustfar - frustnear);
	mat[15] = 1.0f;
	result.setValue(mat);

	/* it is also the projection now, as glOrtho made it (callers rely on it) */
	GPU_matrix_mode(GPU_MATRIX_PROJECTION);
	GPU_matrix_load((const float (*)[4])mat);
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);

	return result;
}


// next arguments probably contain redundant info, for later...
void RAS_OpenGLRasterizer::SetViewMatrix(const MT_Matrix4x4 &mat, 
										 const MT_Matrix3x3 & camOrientMat3x3,
										 const MT_Point3 & pos,
										 const MT_Vector3 &scale,
										 bool perspective)
{
	m_viewmatrix = mat;

	// correction for stereo
	if (Stereo() && perspective)
	{
		MT_Vector3 unitViewDir(0.0f, -1.0f, 0.0f);  // minus y direction, Blender convention
		MT_Vector3 unitViewupVec(0.0f, 0.0f, 1.0f);
		MT_Vector3 viewDir, viewupVec;
		MT_Vector3 eyeline;

		// actual viewDir
		viewDir = camOrientMat3x3 * unitViewDir;  // this is the moto convention, vector on right hand side
		// actual viewup vec
		viewupVec = camOrientMat3x3 * unitViewupVec;

		// vector between eyes
		eyeline = viewDir.cross(viewupVec);

		switch (m_curreye) {
			case RAS_STEREO_LEFTEYE:
				{
				// translate to left by half the eye distance
				MT_Transform transform;
				transform.setIdentity();
				transform.translate(-(eyeline * m_eyeseparation / 2.0f));
				m_viewmatrix *= transform;
				}
				break;
			case RAS_STEREO_RIGHTEYE:
				{
				// translate to right by half the eye distance
				MT_Transform transform;
				transform.setIdentity();
				transform.translate(eyeline * m_eyeseparation / 2.0f);
				m_viewmatrix *= transform;
				}
				break;
		}
	}

	bool negX = (scale[0] < 0.0f);
	bool negY = (scale[1] < 0.0f);
	bool negZ = (scale[2] < 0.0f);
	if (negX || negY || negZ) {
		m_viewmatrix.tscale((negX)?-1.0f:1.0f, (negY)?-1.0f:1.0f, (negZ)?-1.0f:1.0f, 1.0);
	}
	m_viewinvmatrix = m_viewmatrix;
	m_viewinvmatrix.invert();
	/* BGE-Remix P8 Part B (P8.17): a point's height from its eye position */
	for (int k = 0; k < 4; k++)
		m_state.fog_hrow[k] = (float)m_viewinvmatrix[2][k];

	// note: getValue gives back column major as needed by OpenGL
	MT_Scalar glviewmat[16];
	m_viewmatrix.getValue(glviewmat);

	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);
	GPU_matrix_load((const float (*)[4])glviewmat);
	m_campos = pos;
	m_camnegscale = negX ^ negY ^ negZ;
}


const MT_Point3& RAS_OpenGLRasterizer::GetCameraPosition()
{
	return m_campos;
}

bool RAS_OpenGLRasterizer::GetCameraOrtho()
{
	return m_camortho;
}

void RAS_OpenGLRasterizer::SetCullFace(bool enable)
{
	if (enable)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
}

void RAS_OpenGLRasterizer::SetLines(bool enable)
{
	if (enable)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void RAS_OpenGLRasterizer::SetSpecularity(float specX,
										  float specY,
										  float specZ,
										  float specval)
{
	m_state.mat_specular[0] = specX;
	m_state.mat_specular[1] = specY;
	m_state.mat_specular[2] = specZ;
	m_state.mat_specular[3] = specval;
}



void RAS_OpenGLRasterizer::SetShinyness(float shiny)
{
	/* OpenGL clamped the shininess to 0..128 */
	m_state.shininess = (shiny < 0.0f) ? 0.0f : (shiny > 128.0f) ? 128.0f : shiny;
}



void RAS_OpenGLRasterizer::SetDiffuse(float difX,float difY,float difZ,float diffuse)
{
	m_state.mat_diffuse[0] = difX;
	m_state.mat_diffuse[1] = difY;
	m_state.mat_diffuse[2] = difZ;
	m_state.mat_diffuse[3] = diffuse;
}

void RAS_OpenGLRasterizer::SetEmissive(float eX, float eY, float eZ, float e)
{
	m_state.mat_emission[0] = eX;
	m_state.mat_emission[1] = eY;
	m_state.mat_emission[2] = eZ;
	m_state.mat_emission[3] = e;
}


double RAS_OpenGLRasterizer::GetTime()
{
	return m_time;
}

void RAS_OpenGLRasterizer::SetPolygonOffset(float mult, float add)
{
	/* lines in wireframe and bounding-box drawing */
	GPU_state_polygon_offset(mult, add, m_drawingmode < KX_SHADED);
}

void RAS_OpenGLRasterizer::EnableMotionBlur(float motionblurvalue)
{
	/* don't just set m_motionblur to 1, but check if it is 0 so
	 * we don't reset a motion blur that is already enabled */
	if (m_motionblur == 0)
		m_motionblur = 1;
	m_motionblurvalue = motionblurvalue;
}

void RAS_OpenGLRasterizer::DisableMotionBlur()
{
	m_motionblur = 0;
	m_motionblurvalue = -1.0f;
}

void RAS_OpenGLRasterizer::SetAlphaBlend(int alphablend)
{
	/* Variance shadow maps don't handle alpha well, best to not allow it for now  */
	if (m_drawingmode == KX_SHADOW && m_usingoverrideshader)
		alphablend = GPU_BLEND_SOLID;
	GPU_set_material_alpha_blend(alphablend);
	/* BGE-Remix P8 Part B: the alpha test is the shaders' (the storage sets
	 * the old one only for drawing that is still fixed-function) */
	GPU_material_alpha_test(alphablend, &m_state.alpha_test, &m_state.alpha_ref);
	/* BGE-Remix P8 Part B (P8.16): cut-out and transparent casters leave
	 * holes in shadow maps where they are less than half covered */
	if (m_drawingmode == KX_SHADOW && alphablend != GPU_BLEND_SOLID && alphablend != GPU_BLEND_ADD) {
		m_state.alpha_test = true;
		m_state.alpha_ref = (alphablend == GPU_BLEND_CLIP) ? m_state.alpha_ref : 0.5f;
	}
	/* BGE-Remix P8 Part B (P8.12): cut-outs (clip alpha) in a multisampled
	 * target become alpha to coverage (smooth edges) */
	if (alphablend == GPU_BLEND_CLIP && m_pipeline->Multisampled()) {
		glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		m_state.alpha_test = false;
	}
	else if (alphablend != GPU_BLEND_ALPHA_TO_COVERAGE) {
		glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
	}
/*
	if (alphablend == m_last_alphablend)
		return;

	if (alphablend == GPU_BLEND_SOLID) {
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else if (alphablend == GPU_BLEND_ADD) {
		glBlendFunc(GL_ONE, GL_ONE);
		glEnable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
	}
	else if (alphablend == GPU_BLEND_ALPHA) {
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.0f);
	}
	else if (alphablend == GPU_BLEND_CLIP) {
		glDisable(GL_BLEND); 
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.5f);
	}

	m_last_alphablend = alphablend;
*/
}

void RAS_OpenGLRasterizer::SetFrontFace(bool ccw)
{
	if (m_camnegscale)
		ccw = !ccw;

	if (m_last_frontface == ccw)
		return;

	if (ccw)
		glFrontFace(GL_CCW);
	else
		glFrontFace(GL_CW);
	
	m_last_frontface = ccw;
}

void RAS_OpenGLRasterizer::SetAnisotropicFiltering(short level)
{
	GPU_set_anisotropic((float)level);
}

short RAS_OpenGLRasterizer::GetAnisotropicFiltering()
{
	return (short)GPU_get_anisotropic();
}

void RAS_OpenGLRasterizer::SetMipmapping(MipmapOption val)
{
	if (val == RAS_IRasterizer::RAS_MIPMAP_LINEAR)
	{
		GPU_set_linear_mipmap(1);
		GPU_set_mipmap(1);
	}
	else if (val == RAS_IRasterizer::RAS_MIPMAP_NEAREST)
	{
		GPU_set_linear_mipmap(0);
		GPU_set_mipmap(1);
	}
	else
	{
		GPU_set_linear_mipmap(0);
		GPU_set_mipmap(0);
	}
}

RAS_IRasterizer::MipmapOption RAS_OpenGLRasterizer::GetMipmapping()
{
	if (GPU_get_mipmap()) {
		if (GPU_get_linear_mipmap()) {
			return RAS_IRasterizer::RAS_MIPMAP_LINEAR;
		}
		else {
			return RAS_IRasterizer::RAS_MIPMAP_NEAREST;
		}
	}
	else {
		return RAS_IRasterizer::RAS_MIPMAP_NONE;
	}
}

void RAS_OpenGLRasterizer::SetUsingOverrideShader(bool val)
{
	m_usingoverrideshader = val;
}

bool RAS_OpenGLRasterizer::GetUsingOverrideShader()
{
	return m_usingoverrideshader;
}

/**
 * Render Tools
 */

/* ProcessLighting performs lighting on objects. the layer is a bitfield that
 * contains layer information. There are 20 'official' layers in blender. A
 * light is applied on an object only when they are in the same layer. OpenGL
 * has a maximum of 8 lights (simultaneous), so 20 * 8 lights are possible in
 * a scene. */

void RAS_OpenGLRasterizer::ProcessLighting(bool uselights, const MT_Transform& viewmat)
{
	bool enable = false;
	int layer= -1;

	/* find the layer */
	if (uselights) {
		if (m_clientobject)
			layer = static_cast<KX_GameObject*>(m_clientobject)->GetLayer();
	}

	/* avoid state switching */
	if (m_lastlightlayer == layer && m_lastauxinfo == m_auxilaryClientInfo)
		return;

	m_lastlightlayer = layer;
	m_lastauxinfo = m_auxilaryClientInfo;

	/* enable/disable lights as needed */
	if (layer >= 0) {
		//enable = ApplyLights(layer, viewmat);
		// taken from blender source, incompatibility between Blender Object / GameObject
		KX_Scene* kxscene = (KX_Scene*)m_auxilaryClientInfo;
		unsigned int count;
		std::vector<RAS_OpenGLLight*>::iterator lit = m_lights.begin();

		/* BGE-Remix P8 Part B: into the game's state (eye space) */
		for (lit = m_lights.begin(), count = 0; !(lit==m_lights.end()) && count < m_numgllights; ++lit)
		{
			RAS_OpenGLLight* light = (*lit);

			if (light->FillClassicLight(kxscene, layer, viewmat, &m_state.lights[count]))
				count++;
		}
		m_state.light_count = (int)count;

		enable = count > 0;
	}

	if (enable)
		EnableOpenGLLights();
	else
		DisableOpenGLLights();
}

void RAS_OpenGLRasterizer::EnableOpenGLLights()
{
	if (m_lastlighting == true)
		return;

	/* BGE-Remix P8 Part B: lighting with colour material (ambient and
	 * diffuse follow the colour), in the game's state */
	m_state.lighting = true;
	m_state.vertex_color = 1;

	m_lastlighting = true;
}

void RAS_OpenGLRasterizer::DisableOpenGLLights()
{
	if (m_lastlighting == false)
		return;

	m_state.lighting = false;
	m_state.light_count = 0;
	m_state.vertex_color = 1;

	m_lastlighting = false;
}

RAS_ILightObject *RAS_OpenGLRasterizer::CreateLight()
{
	return new RAS_OpenGLLight(this);
}

void RAS_OpenGLRasterizer::AddLight(RAS_ILightObject* lightobject)
{
	RAS_OpenGLLight* gllight = dynamic_cast<RAS_OpenGLLight*>(lightobject);
	assert(gllight);
	m_lights.push_back(gllight);
}

void RAS_OpenGLRasterizer::RemoveLight(RAS_ILightObject* lightobject)
{
	RAS_OpenGLLight* gllight = dynamic_cast<RAS_OpenGLLight*>(lightobject);
	assert(gllight);

	std::vector<RAS_OpenGLLight*>::iterator lit =
		std::find(m_lights.begin(),m_lights.end(),gllight);

	if (!(lit==m_lights.end()))
		m_lights.erase(lit);
}

bool RAS_OpenGLRasterizer::RayHit(struct KX_ClientObjectInfo *client, KX_RayCast *result, float *oglmatrix)
{
	if (result->m_hitMesh) {

		RAS_Polygon* poly = result->m_hitMesh->GetPolygon(result->m_hitPolygon);
		if (!poly->IsVisible())
			return false;

		MT_Vector3 resultnormal(result->m_hitNormal);
		MT_Vector3 left(oglmatrix[0],oglmatrix[1],oglmatrix[2]);
		MT_Vector3 dir = -(left.cross(resultnormal)).safe_normalized();
		left = (dir.cross(resultnormal)).safe_normalized();
		// for the up vector, we take the 'resultnormal' returned by the physics

		float maat[16] = {left[0],         left[1],         left[2],         0,
			               dir[0],          dir[1],          dir[2],          0,
		                  resultnormal[0], resultnormal[1], resultnormal[2], 0,
		                  0,               0,               0,               1};

		GPU_matrix_translate(oglmatrix[12],oglmatrix[13],oglmatrix[14]);
		GPU_matrix_mult((const float (*)[4])maat);
		return true;
	}
	else {
		return false;
	}
}

void RAS_OpenGLRasterizer::applyTransform(float* oglmatrix,int objectdrawmode )
{
	/* FIXME:
	blender: intern/moto/include/MT_Vector3.inl:42: MT_Vector3 operator/(const
	MT_Vector3&, double): Assertion `!MT_fuzzyZero(s)' failed.

	Program received signal SIGABRT, Aborted.
	[Switching to Thread 16384 (LWP 1519)]
	0x40477571 in kill () from /lib/libc.so.6
	(gdb) bt
	#7  0x08334368 in MT_Vector3::normalized() const ()
	#8  0x0833e6ec in RAS_OpenGLRasterizer::applyTransform(RAS_IRasterizer*, double*, int) ()
	*/

	if (objectdrawmode & RAS_IPolyMaterial::BILLBOARD_SCREENALIGNED ||
		objectdrawmode & RAS_IPolyMaterial::BILLBOARD_AXISALIGNED)
	{
		// rotate the billboard/halo
		//page 360/361 3D Game Engine Design, David Eberly for a discussion
		// on screen aligned and axis aligned billboards
		// assumed is that the preprocessor transformed all billboard polygons
		// so that their normal points into the positive x direction (1.0f, 0.0f, 0.0f)
		// when new parenting for objects is done, this rotation
		// will be moved into the object

		MT_Point3 objpos (oglmatrix[12],oglmatrix[13],oglmatrix[14]);
		MT_Point3 campos = GetCameraPosition();
		MT_Vector3 dir = (campos - objpos).safe_normalized();
		MT_Vector3 up(0,0,1.0f);

		KX_GameObject* gameobj = (KX_GameObject*)m_clientobject;
		// get scaling of halo object
		MT_Vector3  size = gameobj->GetSGNode()->GetWorldScaling();

		bool screenaligned = (objectdrawmode & RAS_IPolyMaterial::BILLBOARD_SCREENALIGNED)!=0;//false; //either screen or axisaligned
		if (screenaligned)
		{
			up = (up - up.dot(dir) * dir).safe_normalized();
		} else
		{
			dir = (dir - up.dot(dir)*up).safe_normalized();
		}

		MT_Vector3 left = dir.normalized();
		dir = (up.cross(left)).normalized();

		// we have calculated the row vectors, now we keep
		// local scaling into account:

		left *= size[0];
		dir  *= size[1];
		up   *= size[2];

		float maat[16] = {left[0], left[1], left[2], 0,
		                   dir[0],  dir[1],  dir[2],  0,
		                   up[0],   up[1],   up[2],   0,
		                   0,       0,       0,       1};

		GPU_matrix_translate(objpos[0],objpos[1],objpos[2]);
		GPU_matrix_mult((const float (*)[4])maat);

	}
	else {
		if (objectdrawmode & RAS_IPolyMaterial::SHADOW)
		{
			// shadow must be cast to the ground, physics system needed here!
			MT_Point3 frompoint(oglmatrix[12],oglmatrix[13],oglmatrix[14]);
			KX_GameObject *gameobj = (KX_GameObject*)m_clientobject;
			MT_Vector3 direction = MT_Vector3(0,0,-1);

			direction.normalize();
			direction *= 100000;

			MT_Point3 topoint = frompoint + direction;

			KX_Scene* kxscene = (KX_Scene*) m_auxilaryClientInfo;
			PHY_IPhysicsEnvironment* physics_environment = kxscene->GetPhysicsEnvironment();
			PHY_IPhysicsController* physics_controller = gameobj->GetPhysicsController();

			KX_GameObject *parent = gameobj->GetParent();
			if (!physics_controller && parent)
				physics_controller = parent->GetPhysicsController();

			KX_RayCast::Callback<RAS_OpenGLRasterizer, float> callback(this, physics_controller, oglmatrix);
			if (!KX_RayCast::RayTest(physics_environment, frompoint, topoint, callback))
			{
				// couldn't find something to cast the shadow on...
				GPU_matrix_mult((const float (*)[4])oglmatrix);
			}
			else
			{ // we found the "ground", but the cast matrix doesn't take
			  // scaling in consideration, so we must apply the object scale
				MT_Vector3  size = gameobj->GetSGNode()->GetLocalScale();
				GPU_matrix_scale(size[0], size[1], size[2]);
			}
		} else
		{

			// 'normal' object
			GPU_matrix_mult((const float (*)[4])oglmatrix);
		}
	}
}

static void DisableForText()
{
	/* BGE-Remix P8 Part B: text draws through the layer (its own shader and
	 * texture); only the state it shares is reset */
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); /* needed for texture fonts otherwise they render as wireframe */
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
}

/* BGE-Remix P8 Part B: a 2D pass on the layer: pixels as units */
static void layer_2d_begin(int width, int height)
{
	GPU_matrix_mode(GPU_MATRIX_PROJECTION);
	GPU_matrix_push();
	GPU_matrix_identity();
	GPU_matrix_ortho(0, width, 0, height, -100, 100);
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);
	GPU_matrix_push();
	GPU_matrix_identity();
}

static void layer_2d_end()
{
	GPU_matrix_mode(GPU_MATRIX_PROJECTION);
	GPU_matrix_pop();
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);
	GPU_matrix_pop();
}

void RAS_OpenGLRasterizer::RenderBox2D(int xco,
			int yco,
			int width,
			int height,
			float percentage)
{
	/* This is a rather important line :( The gl-mode hasn't been left
	 * behind quite as neatly as we'd have wanted to. I don't know
	 * what cause it, though :/ .*/
	glDisable(GL_DEPTH_TEST);

	layer_2d_begin(width, height);

	yco = height - yco;
	int barsize = 50;

	/* draw in black first */
	GPU_imm_begin(GL_QUADS, GPU_BUILTIN_SMOOTH);
	GPU_imm_color4ub(0, 0, 0, 255);
	GPU_imm_vertex2f(xco + 1 + 1 + barsize * percentage, yco - 1 + 10);
	GPU_imm_vertex2f(xco + 1, yco - 1 + 10);
	GPU_imm_vertex2f(xco + 1, yco - 1);
	GPU_imm_vertex2f(xco + 1 + 1 + barsize * percentage, yco - 1);

	GPU_imm_color4ub(255, 255, 255, 255);
	GPU_imm_vertex2f(xco + 1 + barsize * percentage, yco + 10);
	GPU_imm_vertex2f(xco, yco + 10);
	GPU_imm_vertex2f(xco, yco);
	GPU_imm_vertex2f(xco + 1 + barsize * percentage, yco);
	GPU_imm_end();

	layer_2d_end();
	glEnable(GL_DEPTH_TEST);
}

void RAS_OpenGLRasterizer::RenderText3D(
        int fontid, const char *text, int size, int dpi,
        const float color[4], const float mat[16], float aspect)
{
	/* gl prepping */
	DisableForText();

	/* the actual drawing (BGE-Remix P8 Part B: through the layer) */
	BLF_layer_begin(color);

	/* multiply the text matrix by the object matrix */
	BLF_enable(fontid, BLF_MATRIX|BLF_ASPECT);
	BLF_matrix(fontid, mat);

	/* aspect is the inverse scale that allows you to increase
	 * your resolution without sizing the final text size
	 * the bigger the size, the smaller the aspect */
	BLF_aspect(fontid, aspect, aspect, aspect);

	BLF_size(fontid, size, dpi);
	BLF_position(fontid, 0, 0, 0);
	BLF_draw(fontid, text, 65535);

	BLF_disable(fontid, BLF_MATRIX|BLF_ASPECT);
	BLF_layer_end();
}

void RAS_OpenGLRasterizer::RenderText2D(
        RAS_TEXT_RENDER_MODE mode,
        const char* text,
        int xco, int yco,
        int width, int height)
{
	/* This is a rather important line :( The gl-mode hasn't been left
	 * behind quite as neatly as we'd have wanted to. I don't know
	 * what cause it, though :/ .*/
	DisableForText();
	glDisable(GL_DEPTH_TEST);

	layer_2d_begin(width, height);

	if (mode == RAS_TEXT_PADDED) {
		/* draw in black first */
		const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		BLF_layer_begin(black);
		BLF_size(blf_mono_font, 11, 72);
		BLF_position(blf_mono_font, (float)xco+1, (float)(height-yco-1), 0.0f);
		BLF_draw(blf_mono_font, text, 65535); /* XXX, use real len */
		BLF_layer_end();
	}

	/* the actual drawing */
	const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	BLF_layer_begin(white);
	BLF_size(blf_mono_font, 11, 72);
	BLF_position(blf_mono_font, (float)xco, (float)(height-yco), 0.0f);
	BLF_draw(blf_mono_font, text, 65535); /* XXX, use real len */
	BLF_layer_end();

	layer_2d_end();
	glEnable(GL_DEPTH_TEST);
}

void RAS_OpenGLRasterizer::PushMatrix()
{
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);
	GPU_matrix_push();
}

void RAS_OpenGLRasterizer::PopMatrix()
{
	GPU_matrix_mode(GPU_MATRIX_MODELVIEW);
	GPU_matrix_pop();
}

void RAS_OpenGLRasterizer::FreeMotionBlur()
{
	if (m_mbtex[0])
		glDeleteTextures(2, m_mbtex);
	m_mbtex[0] = m_mbtex[1] = 0;
	m_mbsize[0] = m_mbsize[1] = 0;
	if (m_mbshader)
		GPU_shader_free(m_mbshader);
	m_mbshader = NULL;
	if (m_stereotex)
		glDeleteTextures(1, &m_stereotex);
	m_stereotex = 0;
	if (m_stereoshader)
		GPU_shader_free(m_stereoshader);
	m_stereoshader = NULL;
}

static const char *stereo_vert =
        "layout(location = 0) in vec4 bge_pos;\n"
        "void main() { gl_Position = vec4(bge_pos.xy, 0.0, 1.0); }\n";
static const char *stereo_frag =
        "uniform sampler2D bge_left;\n"
        "uniform vec4 bge_rect;\n"
        "uniform int bge_columns;\n"
        "out vec4 bge_fragcolor;\n"
        "void main()\n"
        "{\n"
        "	float c = (bge_columns != 0) ? gl_FragCoord.x : gl_FragCoord.y;\n"
        "	if (mod(floor(c), 2.0) < 0.5) discard;\n"
        "	bge_fragcolor = texture(bge_left, (gl_FragCoord.xy - bge_rect.xy) / bge_rect.zw);\n"
        "}\n";

void RAS_OpenGLRasterizer::FinishStereo()
{
	if (!m_stereoleft || !m_stereotex)
		return;
	m_stereoleft = false;
	if (m_stereomode != RAS_STEREO_INTERLACED && m_stereomode != RAS_STEREO_VINTERLACE)
		return;
	if (!m_stereoshader) {
		m_stereoshader = GPU_shader_create_ex(stereo_vert, stereo_frag, NULL, NULL, NULL, 0, 0, 0,
		                                      GPU_SHADER_FLAGS_GLSL450 | GPU_SHADER_FLAGS_CORE);
		if (!m_stereoshader)
			return;
	}
	GPUStateScope scope;
	GPU_state_scope_begin(&scope);
	GPU_state_blend(GPU_LAYER_BLEND_NONE);
	GPU_state_depth(false, false);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glViewport(m_stereorect[0], m_stereorect[1], m_stereorect[2], m_stereorect[3]);

	const GLuint program = GPU_shader_program(m_stereoshader);
	GPU_shader_bind(m_stereoshader);
	glBindTextureUnit(0, m_stereotex);
	glUniform1i(glGetUniformLocation(program, "bge_left"), 0);
	glUniform4f(glGetUniformLocation(program, "bge_rect"), (float)m_stereorect[0], (float)m_stereorect[1],
	            (float)m_stereorect[2], (float)m_stereorect[3]);
	glUniform1i(glGetUniformLocation(program, "bge_columns"), m_stereomode == RAS_STEREO_VINTERLACE ? 1 : 0);
	GPU_draw_fullscreen_triangle();
	GPU_state_scope_end(&scope);
}

/* BGE-Remix P8 Part B: motion blur as the accumulation buffer did it (the
 * history becomes history * value + frame * (1 - value), and is shown), with
 * a history texture and one full-screen triangle; it works on windows
 * without an accumulation buffer too */
static const char *mb_vert =
        "layout(location = 0) in vec4 bge_pos;\n"
        "out vec2 uv;\n"
        "void main() { gl_Position = vec4(bge_pos.xy, 0.0, 1.0); uv = bge_pos.xy * 0.5 + 0.5; }\n";
static const char *mb_frag =
        "in vec2 uv;\n"
        "uniform sampler2D bge_frame;\n"
        "uniform sampler2D bge_history;\n"
        "uniform float bge_value;\n"
        "out vec4 bge_fragcolor;\n"
        "void main() { bge_fragcolor = texture(bge_history, uv) * bge_value + texture(bge_frame, uv) * (1.0 - bge_value); }\n";

void RAS_OpenGLRasterizer::MotionBlur()
{
	int state = GetMotionBlurState();
	if (!state)
		return;
	const float motionblurvalue = GetMotionBlurValue();

	GLint vp[4];
	glGetIntegerv(GL_VIEWPORT, vp);
	if (vp[2] <= 0 || vp[3] <= 0)
		return;
	if (!m_mbtex[0] || m_mbsize[0] != vp[2] || m_mbsize[1] != vp[3]) {
		if (m_mbtex[0])
			glDeleteTextures(2, m_mbtex);
		glCreateTextures(GL_TEXTURE_2D, 2, m_mbtex);
		for (int i = 0; i < 2; i++) {
			glTextureStorage2D(m_mbtex[i], 1, GL_RGBA8, vp[2], vp[3]);
			glTextureParameteri(m_mbtex[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(m_mbtex[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTextureParameteri(m_mbtex[i], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTextureParameteri(m_mbtex[i], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		m_mbsize[0] = vp[2];
		m_mbsize[1] = vp[3];
		state = 1;  /* a new history */
	}

	if (state == 1) {
		/* the first frame: the history is the frame */
		glCopyTextureSubImage2D(m_mbtex[1], 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
		SetMotionBlurState(2);
		return;
	}
	if (motionblurvalue < 0.0f || motionblurvalue > 1.0f)
		return;

	if (!m_mbshader) {
		m_mbshader = GPU_shader_create_ex(mb_vert, mb_frag, NULL, NULL, NULL, 0, 0, 0,
		                                  GPU_SHADER_FLAGS_GLSL450 | GPU_SHADER_FLAGS_CORE);
		if (!m_mbshader) {
			printf("BGE-Remix: the motion blur shader does not build; motion blur is skipped\n");
			DisableMotionBlur();
			return;
		}
	}

	glCopyTextureSubImage2D(m_mbtex[0], 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);

	GPUStateScope scope;
	GPU_state_scope_begin(&scope);
	GPU_state_blend(GPU_LAYER_BLEND_NONE);
	GPU_state_depth(false, false);
	glDisable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	const GLuint program = GPU_shader_program(m_mbshader);
	GPU_shader_bind(m_mbshader);
	glBindTextureUnit(0, m_mbtex[0]);
	glBindTextureUnit(1, m_mbtex[1]);
	glUniform1i(glGetUniformLocation(program, "bge_frame"), 0);
	glUniform1i(glGetUniformLocation(program, "bge_history"), 1);
	glUniform1f(glGetUniformLocation(program, "bge_value"), motionblurvalue);
	GPU_draw_fullscreen_triangle();
	glBindTextureUnit(1, 0);
	GPU_state_scope_end(&scope);

	/* what is shown is the next frame's history */
	glCopyTextureSubImage2D(m_mbtex[1], 0, 0, 0, vp[0], vp[1], vp[2], vp[3]);
}

void RAS_OpenGLRasterizer::SetClientObject(void* obj)
{
	if (m_clientobject != obj)
	{
		bool ccw = (obj == NULL || !((KX_GameObject*)obj)->IsNegativeScaling());
		SetFrontFace(ccw);

		m_clientobject = obj;
	}
}

void RAS_OpenGLRasterizer::SetAuxilaryClientInfo(void* inf)
{
	m_auxilaryClientInfo = inf;
}

void RAS_OpenGLRasterizer::PrintHardwareInfo()
{
	#define pprint(x) std::cout << x << std::endl;

	pprint("GL_VENDOR: " << glGetString(GL_VENDOR));
	pprint("GL_RENDERER: " << glGetString(GL_RENDERER));
	pprint("GL_VERSION:  " << glGetString(GL_VERSION));
	bool support=0;
	pprint("Supported Extensions...");
	pprint(" GL_ARB_shader_objects supported?       "<< (GLEW_ARB_shader_objects?"yes.":"no."));

	support= GLEW_ARB_vertex_shader;
	pprint(" GL_ARB_vertex_shader supported?        "<< (support?"yes.":"no."));
	if (support) {
		pprint(" ----------Details----------");
		int max=0;
		glGetIntegerv(GL_MAX_VERTEX_UNIFORM_COMPONENTS_ARB, (GLint*)&max);
		pprint("  Max uniform components." << max);

		glGetIntegerv(GL_MAX_VARYING_FLOATS_ARB, (GLint*)&max);
		pprint("  Max varying floats." << max);

		glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS_ARB, (GLint*)&max);
		pprint("  Max vertex texture units." << max);

		glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS_ARB, (GLint*)&max);
		pprint("  Max combined texture units." << max);
		pprint("");
	}

	support=GLEW_ARB_fragment_shader;
	pprint(" GL_ARB_fragment_shader supported?      "<< (support?"yes.":"no."));
	if (support) {
		pprint(" ----------Details----------");
		int max=0;
		glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS_ARB, (GLint*)&max);
		pprint("  Max uniform components." << max);
		pprint("");
	}

	support = GLEW_ARB_texture_cube_map;
	pprint(" GL_ARB_texture_cube_map supported?     "<< (support?"yes.":"no."));
	if (support) {
		pprint(" ----------Details----------");
		int size=0;
		glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE_ARB, (GLint*)&size);
		pprint("  Max cubemap size." << size);
		pprint("");
	}

	support = GLEW_ARB_multitexture;
	pprint(" GL_ARB_multitexture supported?         "<< (support?"yes.":"no."));
	if (support) {
		pprint(" ----------Details----------");
		int units=0;
		glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, (GLint*)&units);
		pprint("  Max texture units available.  " << units);
		pprint("");
	}

	pprint(" GL_ARB_texture_env_combine supported?  "<< (GLEW_ARB_texture_env_combine?"yes.":"no."));

	pprint(" GL_ARB_texture_non_power_of_two supported  " << (GPU_full_non_power_of_two_support()?"yes.":"no."));
}

