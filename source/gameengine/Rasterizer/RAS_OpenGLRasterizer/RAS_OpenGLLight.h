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
 * Contributor(s): Mitchell Stokes
 *
 * ***** END GPL LICENSE BLOCK *****
 */

#include "RAS_ILightObject.h"

class RAS_OpenGLRasterizer;
class MT_Transform;
class KX_Scene;
struct GPULamp;
struct Image;

class RAS_OpenGLLight : public RAS_ILightObject
{

	RAS_OpenGLRasterizer *m_rasterizer;

	GPULamp *GetGPULamp();
	void SetupShadowCamera(RAS_ICanvas *canvas, KX_Camera *cam, MT_Transform& camtrans,
	                       float viewmat[4][4], float winmat[4][4], const int rect[4]);
public:
	RAS_OpenGLLight(RAS_OpenGLRasterizer *ras);
	~RAS_OpenGLLight();

	bool FillClassicLight(KX_Scene *kxscene, int oblayer, const MT_Transform& viewmat,
	                      struct GPUClassicLight *cl);

	RAS_OpenGLLight* Clone() { return new RAS_OpenGLLight(*this); }

	bool HasShadowBuffer();
	int GetShadowBindCode();
	MT_Matrix4x4 GetShadowMatrix();
	int GetShadowLayer();
	void BindShadowBuffer(RAS_ICanvas *canvas, KX_Camera *cam, MT_Transform& camtrans);
	void UnbindShadowBuffer();
	int ShadowViewCount();
	void SetShadowView(RAS_ICanvas *canvas, KX_Camera *cam, MT_Transform& camtrans, int view);
	void SetShadowCamera(const float camtoworld[16], const float winmat[16]);
	unsigned int ShadowSignature();
	Image *GetTextureImage(short texslot);
	void Update();
};
