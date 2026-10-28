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

/** \file PxMotionState.h
 *  \ingroup physx
 *
 * A motion state that only stores the transform (near and radar sensors'
 * own objects, tests), as Bullet's DefaultMotionState.
 */

#ifndef __PXMOTIONSTATE_H__
#define __PXMOTIONSTATE_H__

#include <cmath>
#include <cstring>

#include "PHY_IMotionState.h"

class PxDefaultMotionState : public PHY_IMotionState
{
public:
	PxDefaultMotionState()
	{
		m_pos[0] = m_pos[1] = m_pos[2] = 0.0f;
		m_scale[0] = m_scale[1] = m_scale[2] = 1.0f;
		memset(m_ori, 0, sizeof(m_ori));
		m_ori[0] = m_ori[5] = m_ori[10] = 1.0f;
	}
	virtual ~PxDefaultMotionState() {}

	virtual void GetWorldPosition(float &x, float &y, float &z)
	{
		x = m_pos[0];
		y = m_pos[1];
		z = m_pos[2];
	}
	virtual void GetWorldScaling(float &x, float &y, float &z)
	{
		x = m_scale[0];
		y = m_scale[1];
		z = m_scale[2];
	}
	virtual void GetWorldOrientation(float &qx, float &qy, float &qz, float &qw)
	{
		/* from the matrix (columns in m_ori[0..2], [4..6], [8..10]) */
		const float m00 = m_ori[0], m11 = m_ori[5], m22 = m_ori[10];
		const float tr = m00 + m11 + m22;
		if (tr > 0.0f) {
			float s = sqrtf_(tr + 1.0f) * 2.0f;
			qw = 0.25f * s;
			qx = (m_ori[6] - m_ori[9]) / s;
			qy = (m_ori[8] - m_ori[2]) / s;
			qz = (m_ori[1] - m_ori[4]) / s;
		}
		else if (m00 > m11 && m00 > m22) {
			float s = sqrtf_(1.0f + m00 - m11 - m22) * 2.0f;
			qw = (m_ori[6] - m_ori[9]) / s;
			qx = 0.25f * s;
			qy = (m_ori[4] + m_ori[1]) / s;
			qz = (m_ori[8] + m_ori[2]) / s;
		}
		else if (m11 > m22) {
			float s = sqrtf_(1.0f + m11 - m00 - m22) * 2.0f;
			qw = (m_ori[8] - m_ori[2]) / s;
			qx = (m_ori[4] + m_ori[1]) / s;
			qy = 0.25f * s;
			qz = (m_ori[9] + m_ori[6]) / s;
		}
		else {
			float s = sqrtf_(1.0f + m22 - m00 - m11) * 2.0f;
			qw = (m_ori[1] - m_ori[4]) / s;
			qx = (m_ori[8] + m_ori[2]) / s;
			qy = (m_ori[9] + m_ori[6]) / s;
			qz = 0.25f * s;
		}
	}
	virtual void GetWorldOrientation(float *ori) { memcpy(ori, m_ori, sizeof(m_ori)); }
	virtual void SetWorldOrientation(const float *ori) { memcpy(m_ori, ori, sizeof(m_ori)); }
	virtual void SetWorldPosition(float x, float y, float z)
	{
		m_pos[0] = x;
		m_pos[1] = y;
		m_pos[2] = z;
	}
	virtual void SetWorldOrientation(float x, float y, float z, float w)
	{
		const float xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y,
		            wz = w * z;
		memset(m_ori, 0, sizeof(m_ori));
		m_ori[0] = 1.0f - 2.0f * (yy + zz);
		m_ori[1] = 2.0f * (xy + wz);
		m_ori[2] = 2.0f * (xz - wy);
		m_ori[4] = 2.0f * (xy - wz);
		m_ori[5] = 1.0f - 2.0f * (xx + zz);
		m_ori[6] = 2.0f * (yz + wx);
		m_ori[8] = 2.0f * (xz + wy);
		m_ori[9] = 2.0f * (yz - wx);
		m_ori[10] = 1.0f - 2.0f * (xx + yy);
	}
	virtual void CalculateWorldTransformations() {}

	void SetWorldScaling(float x, float y, float z)
	{
		m_scale[0] = x;
		m_scale[1] = y;
		m_scale[2] = z;
	}

private:
	static float sqrtf_(float v) { return v > 0.0f ? sqrtf(v) : 0.0f; }

	float m_pos[3];
	float m_scale[3];
	float m_ori[12];
};

#endif  /* __PXMOTIONSTATE_H__ */
