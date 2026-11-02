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

/** \file KX_WorldInfo.h
 *  \ingroup ketsji
 */

#ifndef __KX_WORLDINFO_H__
#define __KX_WORLDINFO_H__

#include "MT_Scalar.h"
#include "KX_KetsjiEngine.h"
#include "EXP_PyObjectPlus.h"

#ifdef USE_MATHUTILS
void KX_WorldInfo_Mathutils_Callback_Init(void);
#endif

struct Scene;
struct World;

class KX_WorldInfo : public PyObjectPlus
{
	Py_Header

	STR_String m_name;
	bool m_do_color_management;
	bool m_hasworld;
	bool m_hasmist;
	short m_misttype;
	float m_miststart;
	float m_mistdistance;
	float m_mistintensity;
	float m_mistcolor[3];
	float m_backgroundcolor[3];
	float m_ambientcolor[3];
	float m_con_mistcolor[3];
	float m_con_backgroundcolor[3];
	float m_con_ambientcolor[3];
	/* BGE-Remix P8 Part B (P8.17): the world drawn as the background (a
	 * blend or textured sky, or a sun lamp's sky), the zenith, mist height */
	struct Scene *m_blenderscene;
	struct World *m_blenderworld;
	bool m_drawsky;
	float m_zenithcolor[3];
	float m_misthi;

public:
	/**
	 * Mist options
	 */
	enum MistType {
		KX_MIST_QUADRATIC,
		KX_MIST_LINEAR,
		KX_MIST_INV_QUADRATIC,
	};

	KX_WorldInfo(Scene *blenderscene, World *blenderworld);
	~KX_WorldInfo();

	const STR_String &GetName();
	bool hasWorld();
	void setUseMist(bool enable);
	void setMistType(short type);
	void setMistStart(float d);
	void setMistDistance(float d);
	void setMistIntensity(float intensity);
	void setMistColor(float r, float g, float b);
	void setBackColor(float r, float g, float b);
	const float *getBackColorConverted() const;
	/* BGE-Remix: the horizon colour, scene linear */
	const float *getBackColor() const { return m_backgroundcolor; }
	void setAmbientColor(float r, float g, float b);
	void UpdateBackGround();
	void UpdateWorldSettings();
	/* BGE-Remix P8 Part B (P8.17): draws the sky (nothing when the clear
	 * colour already is the world) */
	void RenderBackground(class RAS_IRasterizer *rasty);
	bool DrawsSky() const { return m_drawsky; }

#ifdef WITH_PYTHON
	/* attributes */
	static PyObject *pyattr_get_mist_typeconst(void *self_v, const KX_PYATTRIBUTE_DEF *attrdef);
	static PyObject *pyattr_get_mist_color(void *self_v, const KX_PYATTRIBUTE_DEF *attrdef);
	static int pyattr_set_mist_color(void *self_v, const KX_PYATTRIBUTE_DEF *attrdef, PyObject *value);
	static PyObject *pyattr_get_back_color(void *self_v, const KX_PYATTRIBUTE_DEF *attrdef);
	static int pyattr_set_back_color(void *self_v, const KX_PYATTRIBUTE_DEF *attrdef, PyObject *value);
	static PyObject *pyattr_get_ambient_color(void *self_v, const KX_PYATTRIBUTE_DEF *attrdef);
	static int pyattr_set_ambient_color(void *self_v, const KX_PYATTRIBUTE_DEF *attrdef, PyObject *value);
	virtual PyObject *py_repr(void);
#endif
};

#endif  /* __KX_WORLDINFO_H__ */
