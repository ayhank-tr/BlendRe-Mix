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

/** \file RAS_ILightObject.h
 *  \ingroup bgerast
 */

#ifndef __RAS_LIGHTOBJECT_H__
#define __RAS_LIGHTOBJECT_H__

class RAS_ICanvas;

class KX_Camera;
class KX_Scene;

class MT_Transform;
class MT_Matrix4x4;

struct Image;

class RAS_ILightObject
{
public:
	enum LightType {
		LIGHT_SPOT,
		LIGHT_SUN,
		LIGHT_NORMAL
	};
	bool	m_modified;
	int		m_layer;
	void	*m_scene;
	void	*m_light;
	
	float	m_energy;
	float	m_distance;
	float	m_shadowclipstart;
	float	m_shadowfrustumsize;
	float	m_shadowclipend;
	float	m_shadowbias;
	float	m_shadowbleedbias;
	short	m_shadowmaptype;
	float	m_shadowcolor[3];

	float	m_color[3];

	float	m_att1;
	float	m_att2;
	float	m_coeff_const, m_coeff_lin, m_coeff_quad;
	float	m_spotsize;
	float	m_spotblend;

	/* BGE-Remix P8.9: the shared light description (BKE_lamp_physical.h) */
	bool	m_physical;      /* physical falloff (point, spot) */
	float	m_size;          /* point/spot: the light's radius */
	float	m_areasize[2];   /* area lamps: width and height */
	float	m_sunangle;      /* sun: angular diameter, degrees */
	bool	m_onlyshadow;

	/* BGE-Remix P8.10: contact shadows' reach (0: none); the shadow drawn last
	 * (its signature: the lamp's views and the casters near it), so a lamp
	 * whose shadow would come out the same is not drawn again */
	float	m_contact;
	unsigned int	m_shadowsig;
	bool	m_shadowvalid;

	LightType	m_type;
	
	bool	m_nodiffuse;
	bool	m_nospecular;
	bool	m_glsl;

	virtual ~RAS_ILightObject() {}
	virtual RAS_ILightObject* Clone() = 0;

	virtual bool HasShadowBuffer() = 0;
	virtual int GetShadowBindCode() = 0;
	virtual MT_Matrix4x4 GetShadowMatrix() = 0;
	virtual int GetShadowLayer() = 0;
	virtual void BindShadowBuffer(RAS_ICanvas *canvas, KX_Camera *cam, MT_Transform& camtrans) = 0;
	virtual void UnbindShadowBuffer() = 0;
	/* BGE-Remix P8.10: the buffer's views (spot 1, cube 6, sun cascades 4),
	 * set one after BindShadowBuffer (which sets view 0); the camera the
	 * sun's cascades follow (column-major 4x4s); the views' signature */
	virtual int ShadowViewCount() = 0;
	virtual void SetShadowView(RAS_ICanvas *canvas, KX_Camera *cam, MT_Transform& camtrans, int view) = 0;
	virtual void SetShadowCamera(const float camtoworld[16], const float winmat[16]) = 0;
	virtual unsigned int ShadowSignature() = 0;
	virtual Image *GetTextureImage(short texslot) = 0;
	virtual void Update() = 0;
};

#endif  /* __RAS_LIGHTOBJECT_H__ */
