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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/gpu/intern/gpu_legacy_glu.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.26: GLU's quadrics and matrix helpers, drawn with the
 * (redirected) old calls: GLU itself calls the real old functions, which a
 * core context does not have. Shapes and normals as GLU 1.3 makes them.
 */

#include <math.h>
#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "GPU_glew.h"

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

#ifndef GLU_FILL
#  define GLU_POINT 100010
#  define GLU_LINE 100011
#  define GLU_FILL 100012
#  define GLU_SILHOUETTE 100013
#  define GLU_SMOOTH 100000
#  define GLU_FLAT 100001
#  define GLU_NONE 100002
#  define GLU_OUTSIDE 100020
#  define GLU_INSIDE 100021
#endif

typedef struct LglQuadric {
	GLenum style, normals, orientation;
	GLboolean texture;
} LglQuadric;

void *GPU_glu_NewQuadric(void)
{
	LglQuadric *q = MEM_callocN(sizeof(LglQuadric), "glu quadric");
	q->style = GLU_FILL;
	q->normals = GLU_SMOOTH;
	q->orientation = GLU_OUTSIDE;
	return q;
}

void GPU_glu_DeleteQuadric(void *q)
{
	if (q) {
		MEM_freeN(q);
	}
}

void GPU_glu_QuadricDrawStyle(void *q, GLenum style)
{
	((LglQuadric *)q)->style = style;
}

void GPU_glu_QuadricNormals(void *q, GLenum normals)
{
	((LglQuadric *)q)->normals = normals;
}

void GPU_glu_QuadricOrientation(void *q, GLenum orientation)
{
	((LglQuadric *)q)->orientation = orientation;
}

void GPU_glu_QuadricTexture(void *q, GLboolean texture)
{
	((LglQuadric *)q)->texture = texture;
}

static void normal3(const LglQuadric *q, float x, float y, float z)
{
	if (q->normals == GLU_NONE) {
		return;
	}
	if (q->orientation == GLU_INSIDE) {
		glNormal3f(-x, -y, -z);
	}
	else {
		glNormal3f(x, y, z);
	}
}

static void texco(const LglQuadric *q, float s, float t)
{
	if (q->texture) {
		glTexCoord2f(s, t);
	}
}

void GPU_glu_Sphere(void *qv, GLdouble radius, GLint slices, GLint stacks)
{
	const LglQuadric *q = qv;
	const float r = (float)radius;
	const float nsign = (q->orientation == GLU_INSIDE) ? -1.0f : 1.0f;
	(void)nsign;
	if (slices < 2 || stacks < 1) {
		return;
	}
	if (q->style == GLU_FILL) {
		for (int j = 0; j < stacks; j++) {
			const float rho0 = (float)M_PI * j / stacks, rho1 = (float)M_PI * (j + 1) / stacks;
			glBegin(GL_QUAD_STRIP);
			for (int i = 0; i <= slices; i++) {
				const float theta = (i == slices) ? 0.0f : 2.0f * (float)M_PI * i / slices;
				const float x = -sinf(theta), y = cosf(theta);
				const float s = 1.0f - (float)i / slices;
				if (q->orientation == GLU_INSIDE) {
					normal3(q, x * sinf(rho1), y * sinf(rho1), cosf(rho1));
					texco(q, s, 1.0f - (float)(j + 1) / stacks);
					glVertex3f(x * sinf(rho1) * r, y * sinf(rho1) * r, cosf(rho1) * r);
					normal3(q, x * sinf(rho0), y * sinf(rho0), cosf(rho0));
					texco(q, s, 1.0f - (float)j / stacks);
					glVertex3f(x * sinf(rho0) * r, y * sinf(rho0) * r, cosf(rho0) * r);
				}
				else {
					normal3(q, x * sinf(rho0), y * sinf(rho0), cosf(rho0));
					texco(q, s, 1.0f - (float)j / stacks);
					glVertex3f(x * sinf(rho0) * r, y * sinf(rho0) * r, cosf(rho0) * r);
					normal3(q, x * sinf(rho1), y * sinf(rho1), cosf(rho1));
					texco(q, s, 1.0f - (float)(j + 1) / stacks);
					glVertex3f(x * sinf(rho1) * r, y * sinf(rho1) * r, cosf(rho1) * r);
				}
			}
			glEnd();
		}
	}
	else if (q->style == GLU_LINE || q->style == GLU_SILHOUETTE) {
		/* the stacks' circles, then the slices' meridians */
		for (int j = 1; j < stacks; j++) {
			const float rho = (float)M_PI * j / stacks;
			glBegin(GL_LINE_LOOP);
			for (int i = 0; i < slices; i++) {
				const float theta = 2.0f * (float)M_PI * i / slices;
				const float x = cosf(theta) * sinf(rho), y = sinf(theta) * sinf(rho), z = cosf(rho);
				normal3(q, x, y, z);
				glVertex3f(x * r, y * r, z * r);
			}
			glEnd();
		}
		for (int i = 0; i < slices; i++) {
			const float theta = 2.0f * (float)M_PI * i / slices;
			glBegin(GL_LINE_STRIP);
			for (int j = 0; j <= stacks; j++) {
				const float rho = (float)M_PI * j / stacks;
				const float x = cosf(theta) * sinf(rho), y = sinf(theta) * sinf(rho), z = cosf(rho);
				normal3(q, x, y, z);
				glVertex3f(x * r, y * r, z * r);
			}
			glEnd();
		}
	}
	else {
		glBegin(GL_POINTS);
		for (int j = 0; j <= stacks; j++) {
			const float rho = (float)M_PI * j / stacks;
			for (int i = 0; i < slices; i++) {
				const float theta = 2.0f * (float)M_PI * i / slices;
				const float x = cosf(theta) * sinf(rho), y = sinf(theta) * sinf(rho), z = cosf(rho);
				normal3(q, x, y, z);
				glVertex3f(x * r, y * r, z * r);
			}
		}
		glEnd();
	}
}

void GPU_glu_Cylinder(void *qv, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks)
{
	const LglQuadric *q = qv;
	if (slices < 2 || stacks < 1) {
		return;
	}
	const float h = (float)height, b = (float)base, t = (float)top;
	const float len = sqrtf(h * h + (b - t) * (b - t));
	const float nz = (len > 0.0f) ? (b - t) / len : 0.0f, nxy = (len > 0.0f) ? h / len : 1.0f;
	if (q->style == GLU_FILL) {
		for (int j = 0; j < stacks; j++) {
			const float z0 = h * j / stacks, z1 = h * (j + 1) / stacks;
			const float r0 = b + (t - b) * j / stacks, r1 = b + (t - b) * (j + 1) / stacks;
			glBegin(GL_QUAD_STRIP);
			for (int i = 0; i <= slices; i++) {
				const float theta = (i == slices) ? 0.0f : 2.0f * (float)M_PI * i / slices;
				const float x = sinf(theta), y = cosf(theta);
				normal3(q, x * nxy, y * nxy, nz);
				if (q->orientation == GLU_INSIDE) {
					texco(q, (float)i / slices, (float)(j + 1) / stacks);
					glVertex3f(x * r1, y * r1, z1);
					texco(q, (float)i / slices, (float)j / stacks);
					glVertex3f(x * r0, y * r0, z0);
				}
				else {
					texco(q, (float)i / slices, (float)j / stacks);
					glVertex3f(x * r0, y * r0, z0);
					texco(q, (float)i / slices, (float)(j + 1) / stacks);
					glVertex3f(x * r1, y * r1, z1);
				}
			}
			glEnd();
		}
	}
	else if (q->style == GLU_LINE || q->style == GLU_SILHOUETTE) {
		const int rings = (q->style == GLU_SILHOUETTE) ? 1 : stacks;
		for (int j = 0; j <= stacks; j += (q->style == GLU_SILHOUETTE) ? stacks : 1) {
			const float z = h * j / stacks, r = b + (t - b) * j / stacks;
			glBegin(GL_LINE_LOOP);
			for (int i = 0; i < slices; i++) {
				const float theta = 2.0f * (float)M_PI * i / slices;
				const float x = sinf(theta), y = cosf(theta);
				normal3(q, x * nxy, y * nxy, nz);
				glVertex3f(x * r, y * r, z);
			}
			glEnd();
		}
		(void)rings;
		glBegin(GL_LINES);
		for (int i = 0; i < slices; i++) {
			const float theta = 2.0f * (float)M_PI * i / slices;
			const float x = sinf(theta), y = cosf(theta);
			normal3(q, x * nxy, y * nxy, nz);
			glVertex3f(x * b, y * b, 0.0f);
			glVertex3f(x * t, y * t, h);
		}
		glEnd();
	}
	else {
		glBegin(GL_POINTS);
		for (int j = 0; j <= stacks; j++) {
			const float z = h * j / stacks, r = b + (t - b) * j / stacks;
			for (int i = 0; i < slices; i++) {
				const float theta = 2.0f * (float)M_PI * i / slices;
				const float x = sinf(theta), y = cosf(theta);
				normal3(q, x * nxy, y * nxy, nz);
				glVertex3f(x * r, y * r, z);
			}
		}
		glEnd();
	}
}

void GPU_glu_PartialDisk(void *qv, GLdouble inner, GLdouble outer, GLint slices, GLint loops, GLdouble start,
                         GLdouble sweep)
{
	const LglQuadric *q = qv;
	if (slices < 2 || loops < 1) {
		return;
	}
	const float ri = (float)inner, ro = (float)outer;
	const float a0 = (float)(start * M_PI / 180.0), da = (float)(sweep * M_PI / 180.0);
	const int full = (sweep >= 360.0 || sweep <= -360.0);
	normal3(q, 0.0f, 0.0f, 1.0f);
	/* GLU measures the angle from +y, clockwise */
#define DX(a) sinf(a)
#define DY(a) cosf(a)
	if (q->style == GLU_FILL) {
		for (int j = 0; j < loops; j++) {
			const float r0 = ri + (ro - ri) * j / loops, r1 = ri + (ro - ri) * (j + 1) / loops;
			glBegin(GL_QUAD_STRIP);
			for (int i = 0; i <= slices; i++) {
				const float a = a0 + da * i / slices;
				if (q->orientation == GLU_INSIDE) {
					texco(q, DX(a) * r0 / ro * 0.5f + 0.5f, DY(a) * r0 / ro * 0.5f + 0.5f);
					glVertex3f(DX(a) * r0, DY(a) * r0, 0.0f);
					texco(q, DX(a) * r1 / ro * 0.5f + 0.5f, DY(a) * r1 / ro * 0.5f + 0.5f);
					glVertex3f(DX(a) * r1, DY(a) * r1, 0.0f);
				}
				else {
					texco(q, DX(a) * r1 / ro * 0.5f + 0.5f, DY(a) * r1 / ro * 0.5f + 0.5f);
					glVertex3f(DX(a) * r1, DY(a) * r1, 0.0f);
					texco(q, DX(a) * r0 / ro * 0.5f + 0.5f, DY(a) * r0 / ro * 0.5f + 0.5f);
					glVertex3f(DX(a) * r0, DY(a) * r0, 0.0f);
				}
			}
			glEnd();
		}
	}
	else if (q->style == GLU_LINE || q->style == GLU_SILHOUETTE) {
		const int sil = (q->style == GLU_SILHOUETTE);
		for (int j = 0; j <= loops; j++) {
			if (sil && j != 0 && j != loops) {
				continue;
			}
			const float r = ri + (ro - ri) * j / loops;
			if (r == 0.0f) {
				continue;
			}
			glBegin(full ? GL_LINE_LOOP : GL_LINE_STRIP);
			for (int i = 0; i < (full ? slices : slices + 1); i++) {
				const float a = a0 + da * i / slices;
				glVertex3f(DX(a) * r, DY(a) * r, 0.0f);
			}
			glEnd();
		}
		if (!sil || !full) {
			for (int i = 0; i <= slices; i++) {
				if (sil && i != 0 && i != slices) {
					continue;
				}
				if (full && i == slices) {
					continue;
				}
				const float a = a0 + da * i / slices;
				glBegin(GL_LINE_STRIP);
				for (int j = 0; j <= loops; j++) {
					const float r = ri + (ro - ri) * j / loops;
					glVertex3f(DX(a) * r, DY(a) * r, 0.0f);
				}
				glEnd();
			}
		}
	}
	else {
		glBegin(GL_POINTS);
		for (int j = 0; j <= loops; j++) {
			const float r = ri + (ro - ri) * j / loops;
			for (int i = 0; i < slices; i++) {
				const float a = a0 + da * i / slices;
				glVertex3f(DX(a) * r, DY(a) * r, 0.0f);
			}
		}
		glEnd();
	}
#undef DX
#undef DY
}

void GPU_glu_Disk(void *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops)
{
	GPU_glu_PartialDisk(q, inner, outer, slices, loops, 0.0, 360.0);
}

void GPU_glu_Perspective(GLdouble fovy, GLdouble aspect, GLdouble znear, GLdouble zfar)
{
	const double f = tan(fovy * M_PI / 360.0) * znear;
	glFrustum(-f * aspect, f * aspect, -f, f, znear, zfar);
}

void GPU_glu_Ortho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
	glOrtho(left, right, bottom, top, -1.0, 1.0);
}

void GPU_glu_LookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz,
                    GLdouble ux, GLdouble uy, GLdouble uz)
{
	double f[3] = {cx - ex, cy - ey, cz - ez};
	double l = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
	if (l > 0.0) {
		f[0] /= l; f[1] /= l; f[2] /= l;
	}
	double s[3] = {f[1] * uz - f[2] * uy, f[2] * ux - f[0] * uz, f[0] * uy - f[1] * ux};
	l = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
	if (l > 0.0) {
		s[0] /= l; s[1] /= l; s[2] /= l;
	}
	const double u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
	const GLdouble m[16] = {
		s[0], u[0], -f[0], 0.0,
		s[1], u[1], -f[1], 0.0,
		s[2], u[2], -f[2], 0.0,
		0.0, 0.0, 0.0, 1.0,
	};
	glMultMatrixd(m);
	glTranslated(-ex, -ey, -ez);
}

void GPU_glu_PickMatrix(GLdouble x, GLdouble y, GLdouble w, GLdouble h, const GLint viewport[4])
{
	if (w <= 0.0 || h <= 0.0) {
		return;
	}
	glTranslated((viewport[2] - 2.0 * (x - viewport[0])) / w, (viewport[3] - 2.0 * (y - viewport[1])) / h, 0.0);
	glScaled(viewport[2] / w, viewport[3] / h, 1.0);
}
