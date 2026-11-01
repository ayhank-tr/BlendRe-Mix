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

/** \file blender/gpu/intern/gpu_legacy_variants.c
 *  \ingroup gpu
 *
 * BGE-Remix P8.26: the typed forms of the old per-vertex calls, each
 * funnelled into one function of gpu_legacy.c. Written by
 * scripts/gen_legacy.py: change that, not this.
 */

#define GPU_LEGACY_NO_REDIRECT
#include "GPU_glew.h"

#include "gpu_legacy_private.h"


void GPU_lgl_Vertex2s(GLshort x, GLshort y)
{
	lgl_vertex4f((float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_Vertex2sv(const GLshort *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_Vertex2i(GLint x, GLint y)
{
	lgl_vertex4f((float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_Vertex2iv(const GLint *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_Vertex2f(GLfloat x, GLfloat y)
{
	lgl_vertex4f((float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_Vertex2fv(const GLfloat *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_Vertex2d(GLdouble x, GLdouble y)
{
	lgl_vertex4f((float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_Vertex2dv(const GLdouble *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_Vertex3s(GLshort x, GLshort y, GLshort z)
{
	lgl_vertex4f((float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_Vertex3sv(const GLshort *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_Vertex3i(GLint x, GLint y, GLint z)
{
	lgl_vertex4f((float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_Vertex3iv(const GLint *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_Vertex3f(GLfloat x, GLfloat y, GLfloat z)
{
	lgl_vertex4f((float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_Vertex3fv(const GLfloat *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_Vertex3d(GLdouble x, GLdouble y, GLdouble z)
{
	lgl_vertex4f((float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_Vertex3dv(const GLdouble *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_Vertex4s(GLshort x, GLshort y, GLshort z, GLshort w)
{
	lgl_vertex4f((float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_Vertex4sv(const GLshort *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_Vertex4i(GLint x, GLint y, GLint z, GLint w)
{
	lgl_vertex4f((float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_Vertex4iv(const GLint *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_Vertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	lgl_vertex4f((float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_Vertex4fv(const GLfloat *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_Vertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
	lgl_vertex4f((float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_Vertex4dv(const GLdouble *v)
{
	lgl_vertex4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_Color3b(GLbyte r, GLbyte g, GLbyte b)
{
	lgl_color4f((float)(r) / 127.0f, (float)(g) / 127.0f, (float)(b) / 127.0f, 1.0f);
}

void GPU_lgl_Color3bv(const GLbyte *v)
{
	lgl_color4f((float)(v[0]) / 127.0f, (float)(v[1]) / 127.0f, (float)(v[2]) / 127.0f, 1.0f);
}

void GPU_lgl_Color3s(GLshort r, GLshort g, GLshort b)
{
	lgl_color4f((float)(r) / 32767.0f, (float)(g) / 32767.0f, (float)(b) / 32767.0f, 1.0f);
}

void GPU_lgl_Color3sv(const GLshort *v)
{
	lgl_color4f((float)(v[0]) / 32767.0f, (float)(v[1]) / 32767.0f, (float)(v[2]) / 32767.0f, 1.0f);
}

void GPU_lgl_Color3i(GLint r, GLint g, GLint b)
{
	lgl_color4f((float)(r) / 2147483647.0f, (float)(g) / 2147483647.0f, (float)(b) / 2147483647.0f, 1.0f);
}

void GPU_lgl_Color3iv(const GLint *v)
{
	lgl_color4f((float)(v[0]) / 2147483647.0f, (float)(v[1]) / 2147483647.0f, (float)(v[2]) / 2147483647.0f, 1.0f);
}

void GPU_lgl_Color3f(GLfloat r, GLfloat g, GLfloat b)
{
	lgl_color4f((float)(r), (float)(g), (float)(b), 1.0f);
}

void GPU_lgl_Color3fv(const GLfloat *v)
{
	lgl_color4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_Color3d(GLdouble r, GLdouble g, GLdouble b)
{
	lgl_color4f((float)(r), (float)(g), (float)(b), 1.0f);
}

void GPU_lgl_Color3dv(const GLdouble *v)
{
	lgl_color4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_Color3ub(GLubyte r, GLubyte g, GLubyte b)
{
	lgl_color4f((float)(r) / 255.0f, (float)(g) / 255.0f, (float)(b) / 255.0f, 1.0f);
}

void GPU_lgl_Color3ubv(const GLubyte *v)
{
	lgl_color4f((float)(v[0]) / 255.0f, (float)(v[1]) / 255.0f, (float)(v[2]) / 255.0f, 1.0f);
}

void GPU_lgl_Color3us(GLushort r, GLushort g, GLushort b)
{
	lgl_color4f((float)(r) / 65535.0f, (float)(g) / 65535.0f, (float)(b) / 65535.0f, 1.0f);
}

void GPU_lgl_Color3usv(const GLushort *v)
{
	lgl_color4f((float)(v[0]) / 65535.0f, (float)(v[1]) / 65535.0f, (float)(v[2]) / 65535.0f, 1.0f);
}

void GPU_lgl_Color3ui(GLuint r, GLuint g, GLuint b)
{
	lgl_color4f((float)(r) / 4294967295.0f, (float)(g) / 4294967295.0f, (float)(b) / 4294967295.0f, 1.0f);
}

void GPU_lgl_Color3uiv(const GLuint *v)
{
	lgl_color4f((float)(v[0]) / 4294967295.0f, (float)(v[1]) / 4294967295.0f, (float)(v[2]) / 4294967295.0f, 1.0f);
}

void GPU_lgl_Color4b(GLbyte r, GLbyte g, GLbyte b, GLbyte a)
{
	lgl_color4f((float)(r) / 127.0f, (float)(g) / 127.0f, (float)(b) / 127.0f, (float)(a) / 127.0f);
}

void GPU_lgl_Color4bv(const GLbyte *v)
{
	lgl_color4f((float)(v[0]) / 127.0f, (float)(v[1]) / 127.0f, (float)(v[2]) / 127.0f, (float)(v[3]) / 127.0f);
}

void GPU_lgl_Color4s(GLshort r, GLshort g, GLshort b, GLshort a)
{
	lgl_color4f((float)(r) / 32767.0f, (float)(g) / 32767.0f, (float)(b) / 32767.0f, (float)(a) / 32767.0f);
}

void GPU_lgl_Color4sv(const GLshort *v)
{
	lgl_color4f((float)(v[0]) / 32767.0f, (float)(v[1]) / 32767.0f, (float)(v[2]) / 32767.0f, (float)(v[3]) / 32767.0f);
}

void GPU_lgl_Color4i(GLint r, GLint g, GLint b, GLint a)
{
	lgl_color4f((float)(r) / 2147483647.0f, (float)(g) / 2147483647.0f, (float)(b) / 2147483647.0f, (float)(a) / 2147483647.0f);
}

void GPU_lgl_Color4iv(const GLint *v)
{
	lgl_color4f((float)(v[0]) / 2147483647.0f, (float)(v[1]) / 2147483647.0f, (float)(v[2]) / 2147483647.0f, (float)(v[3]) / 2147483647.0f);
}

void GPU_lgl_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
	lgl_color4f((float)(r), (float)(g), (float)(b), (float)(a));
}

void GPU_lgl_Color4fv(const GLfloat *v)
{
	lgl_color4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_Color4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a)
{
	lgl_color4f((float)(r), (float)(g), (float)(b), (float)(a));
}

void GPU_lgl_Color4dv(const GLdouble *v)
{
	lgl_color4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
	lgl_color4f((float)(r) / 255.0f, (float)(g) / 255.0f, (float)(b) / 255.0f, (float)(a) / 255.0f);
}

void GPU_lgl_Color4ubv(const GLubyte *v)
{
	lgl_color4f((float)(v[0]) / 255.0f, (float)(v[1]) / 255.0f, (float)(v[2]) / 255.0f, (float)(v[3]) / 255.0f);
}

void GPU_lgl_Color4us(GLushort r, GLushort g, GLushort b, GLushort a)
{
	lgl_color4f((float)(r) / 65535.0f, (float)(g) / 65535.0f, (float)(b) / 65535.0f, (float)(a) / 65535.0f);
}

void GPU_lgl_Color4usv(const GLushort *v)
{
	lgl_color4f((float)(v[0]) / 65535.0f, (float)(v[1]) / 65535.0f, (float)(v[2]) / 65535.0f, (float)(v[3]) / 65535.0f);
}

void GPU_lgl_Color4ui(GLuint r, GLuint g, GLuint b, GLuint a)
{
	lgl_color4f((float)(r) / 4294967295.0f, (float)(g) / 4294967295.0f, (float)(b) / 4294967295.0f, (float)(a) / 4294967295.0f);
}

void GPU_lgl_Color4uiv(const GLuint *v)
{
	lgl_color4f((float)(v[0]) / 4294967295.0f, (float)(v[1]) / 4294967295.0f, (float)(v[2]) / 4294967295.0f, (float)(v[3]) / 4294967295.0f);
}

void GPU_lgl_Normal3b(GLbyte x, GLbyte y, GLbyte z)
{
	lgl_normal3f((float)(x) / 127.0f, (float)(y) / 127.0f, (float)(z) / 127.0f);
}

void GPU_lgl_Normal3bv(const GLbyte *v)
{
	lgl_normal3f((float)(v[0]) / 127.0f, (float)(v[1]) / 127.0f, (float)(v[2]) / 127.0f);
}

void GPU_lgl_Normal3s(GLshort x, GLshort y, GLshort z)
{
	lgl_normal3f((float)(x) / 32767.0f, (float)(y) / 32767.0f, (float)(z) / 32767.0f);
}

void GPU_lgl_Normal3sv(const GLshort *v)
{
	lgl_normal3f((float)(v[0]) / 32767.0f, (float)(v[1]) / 32767.0f, (float)(v[2]) / 32767.0f);
}

void GPU_lgl_Normal3i(GLint x, GLint y, GLint z)
{
	lgl_normal3f((float)(x) / 2147483647.0f, (float)(y) / 2147483647.0f, (float)(z) / 2147483647.0f);
}

void GPU_lgl_Normal3iv(const GLint *v)
{
	lgl_normal3f((float)(v[0]) / 2147483647.0f, (float)(v[1]) / 2147483647.0f, (float)(v[2]) / 2147483647.0f);
}

void GPU_lgl_Normal3f(GLfloat x, GLfloat y, GLfloat z)
{
	lgl_normal3f((float)(x), (float)(y), (float)(z));
}

void GPU_lgl_Normal3fv(const GLfloat *v)
{
	lgl_normal3f((float)(v[0]), (float)(v[1]), (float)(v[2]));
}

void GPU_lgl_Normal3d(GLdouble x, GLdouble y, GLdouble z)
{
	lgl_normal3f((float)(x), (float)(y), (float)(z));
}

void GPU_lgl_Normal3dv(const GLdouble *v)
{
	lgl_normal3f((float)(v[0]), (float)(v[1]), (float)(v[2]));
}

void GPU_lgl_TexCoord1s(GLshort s_)
{
	lgl_texcoord4f(0, (float)(s_), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_TexCoord1sv(const GLshort *v)
{
	lgl_texcoord4f(0, (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord1s(GLenum target, GLshort s_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord1s(target, s_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord1sv(GLenum target, const GLshort *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord1sv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_TexCoord1i(GLint s_)
{
	lgl_texcoord4f(0, (float)(s_), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_TexCoord1iv(const GLint *v)
{
	lgl_texcoord4f(0, (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord1i(GLenum target, GLint s_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord1i(target, s_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord1iv(GLenum target, const GLint *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord1iv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_TexCoord1f(GLfloat s_)
{
	lgl_texcoord4f(0, (float)(s_), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_TexCoord1fv(const GLfloat *v)
{
	lgl_texcoord4f(0, (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord1f(GLenum target, GLfloat s_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord1f(target, s_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord1fv(GLenum target, const GLfloat *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord1fv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_TexCoord1d(GLdouble s_)
{
	lgl_texcoord4f(0, (float)(s_), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_TexCoord1dv(const GLdouble *v)
{
	lgl_texcoord4f(0, (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord1d(GLenum target, GLdouble s_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord1d(target, s_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord1dv(GLenum target, const GLdouble *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord1dv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_TexCoord2s(GLshort s_, GLshort t_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), 0.0f, 1.0f);
}

void GPU_lgl_TexCoord2sv(const GLshort *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord2s(GLenum target, GLshort s_, GLshort t_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord2s(target, s_, t_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord2sv(GLenum target, const GLshort *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord2sv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_TexCoord2i(GLint s_, GLint t_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), 0.0f, 1.0f);
}

void GPU_lgl_TexCoord2iv(const GLint *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord2i(GLenum target, GLint s_, GLint t_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord2i(target, s_, t_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord2iv(GLenum target, const GLint *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord2iv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_TexCoord2f(GLfloat s_, GLfloat t_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), 0.0f, 1.0f);
}

void GPU_lgl_TexCoord2fv(const GLfloat *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord2f(GLenum target, GLfloat s_, GLfloat t_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord2f(target, s_, t_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord2fv(GLenum target, const GLfloat *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord2fv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_TexCoord2d(GLdouble s_, GLdouble t_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), 0.0f, 1.0f);
}

void GPU_lgl_TexCoord2dv(const GLdouble *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord2d(GLenum target, GLdouble s_, GLdouble t_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord2d(target, s_, t_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), 0.0f, 1.0f);
}

void GPU_lgl_MultiTexCoord2dv(GLenum target, const GLdouble *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord2dv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_TexCoord3s(GLshort s_, GLshort t_, GLshort r_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), (float)(r_), 1.0f);
}

void GPU_lgl_TexCoord3sv(const GLshort *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_MultiTexCoord3s(GLenum target, GLshort s_, GLshort t_, GLshort r_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord3s(target, s_, t_, r_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), (float)(r_), 1.0f);
}

void GPU_lgl_MultiTexCoord3sv(GLenum target, const GLshort *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord3sv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_TexCoord3i(GLint s_, GLint t_, GLint r_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), (float)(r_), 1.0f);
}

void GPU_lgl_TexCoord3iv(const GLint *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_MultiTexCoord3i(GLenum target, GLint s_, GLint t_, GLint r_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord3i(target, s_, t_, r_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), (float)(r_), 1.0f);
}

void GPU_lgl_MultiTexCoord3iv(GLenum target, const GLint *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord3iv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_TexCoord3f(GLfloat s_, GLfloat t_, GLfloat r_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), (float)(r_), 1.0f);
}

void GPU_lgl_TexCoord3fv(const GLfloat *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_MultiTexCoord3f(GLenum target, GLfloat s_, GLfloat t_, GLfloat r_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord3f(target, s_, t_, r_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), (float)(r_), 1.0f);
}

void GPU_lgl_MultiTexCoord3fv(GLenum target, const GLfloat *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord3fv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_TexCoord3d(GLdouble s_, GLdouble t_, GLdouble r_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), (float)(r_), 1.0f);
}

void GPU_lgl_TexCoord3dv(const GLdouble *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_MultiTexCoord3d(GLenum target, GLdouble s_, GLdouble t_, GLdouble r_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord3d(target, s_, t_, r_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), (float)(r_), 1.0f);
}

void GPU_lgl_MultiTexCoord3dv(GLenum target, const GLdouble *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord3dv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_TexCoord4s(GLshort s_, GLshort t_, GLshort r_, GLshort q_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), (float)(r_), (float)(q_));
}

void GPU_lgl_TexCoord4sv(const GLshort *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_MultiTexCoord4s(GLenum target, GLshort s_, GLshort t_, GLshort r_, GLshort q_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord4s(target, s_, t_, r_, q_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), (float)(r_), (float)(q_));
}

void GPU_lgl_MultiTexCoord4sv(GLenum target, const GLshort *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord4sv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_TexCoord4i(GLint s_, GLint t_, GLint r_, GLint q_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), (float)(r_), (float)(q_));
}

void GPU_lgl_TexCoord4iv(const GLint *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_MultiTexCoord4i(GLenum target, GLint s_, GLint t_, GLint r_, GLint q_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord4i(target, s_, t_, r_, q_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), (float)(r_), (float)(q_));
}

void GPU_lgl_MultiTexCoord4iv(GLenum target, const GLint *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord4iv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_TexCoord4f(GLfloat s_, GLfloat t_, GLfloat r_, GLfloat q_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), (float)(r_), (float)(q_));
}

void GPU_lgl_TexCoord4fv(const GLfloat *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_MultiTexCoord4f(GLenum target, GLfloat s_, GLfloat t_, GLfloat r_, GLfloat q_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord4f(target, s_, t_, r_, q_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), (float)(r_), (float)(q_));
}

void GPU_lgl_MultiTexCoord4fv(GLenum target, const GLfloat *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord4fv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_TexCoord4d(GLdouble s_, GLdouble t_, GLdouble r_, GLdouble q_)
{
	lgl_texcoord4f(0, (float)(s_), (float)(t_), (float)(r_), (float)(q_));
}

void GPU_lgl_TexCoord4dv(const GLdouble *v)
{
	lgl_texcoord4f(0, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_MultiTexCoord4d(GLenum target, GLdouble s_, GLdouble t_, GLdouble r_, GLdouble q_)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord4d(target, s_, t_, r_, q_);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(s_), (float)(t_), (float)(r_), (float)(q_));
}

void GPU_lgl_MultiTexCoord4dv(GLenum target, const GLdouble *v)
{
	if (!GPU_lgl_on) {
		glMultiTexCoord4dv(target, v);
		return;
	}
	lgl_texcoord4f((int)(target - GL_TEXTURE0), (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_RasterPos2s(GLshort x, GLshort y)
{
	lgl_rasterpos4f((float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_RasterPos2sv(const GLshort *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_RasterPos2i(GLint x, GLint y)
{
	lgl_rasterpos4f((float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_RasterPos2iv(const GLint *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_RasterPos2f(GLfloat x, GLfloat y)
{
	lgl_rasterpos4f((float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_RasterPos2fv(const GLfloat *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_RasterPos2d(GLdouble x, GLdouble y)
{
	lgl_rasterpos4f((float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_RasterPos2dv(const GLdouble *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_RasterPos3s(GLshort x, GLshort y, GLshort z)
{
	lgl_rasterpos4f((float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_RasterPos3sv(const GLshort *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_RasterPos3i(GLint x, GLint y, GLint z)
{
	lgl_rasterpos4f((float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_RasterPos3iv(const GLint *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_RasterPos3f(GLfloat x, GLfloat y, GLfloat z)
{
	lgl_rasterpos4f((float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_RasterPos3fv(const GLfloat *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_RasterPos3d(GLdouble x, GLdouble y, GLdouble z)
{
	lgl_rasterpos4f((float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_RasterPos3dv(const GLdouble *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_RasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w)
{
	lgl_rasterpos4f((float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_RasterPos4sv(const GLshort *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_RasterPos4i(GLint x, GLint y, GLint z, GLint w)
{
	lgl_rasterpos4f((float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_RasterPos4iv(const GLint *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_RasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	lgl_rasterpos4f((float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_RasterPos4fv(const GLfloat *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_RasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
	lgl_rasterpos4f((float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_RasterPos4dv(const GLdouble *v)
{
	lgl_rasterpos4f((float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_Rects(GLshort x1, GLshort y1, GLshort x2, GLshort y2)
{
	lgl_rect4f((float)x1, (float)y1, (float)x2, (float)y2);
}

void GPU_lgl_Rectsv(const GLshort *v1, const GLshort *v2)
{
	lgl_rect4f((float)v1[0], (float)v1[1], (float)v2[0], (float)v2[1]);
}

void GPU_lgl_Recti(GLint x1, GLint y1, GLint x2, GLint y2)
{
	lgl_rect4f((float)x1, (float)y1, (float)x2, (float)y2);
}

void GPU_lgl_Rectiv(const GLint *v1, const GLint *v2)
{
	lgl_rect4f((float)v1[0], (float)v1[1], (float)v2[0], (float)v2[1]);
}

void GPU_lgl_Rectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
	lgl_rect4f((float)x1, (float)y1, (float)x2, (float)y2);
}

void GPU_lgl_Rectfv(const GLfloat *v1, const GLfloat *v2)
{
	lgl_rect4f((float)v1[0], (float)v1[1], (float)v2[0], (float)v2[1]);
}

void GPU_lgl_Rectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2)
{
	lgl_rect4f((float)x1, (float)y1, (float)x2, (float)y2);
}

void GPU_lgl_Rectdv(const GLdouble *v1, const GLdouble *v2)
{
	lgl_rect4f((float)v1[0], (float)v1[1], (float)v2[0], (float)v2[1]);
}

void GPU_lgl_EvalCoord1f(GLfloat u)
{
	lgl_evalcoord1f((float)u);
}

void GPU_lgl_EvalCoord1fv(const GLfloat *u)
{
	lgl_evalcoord1f((float)u[0]);
}

void GPU_lgl_EvalCoord1d(GLdouble u)
{
	lgl_evalcoord1f((float)u);
}

void GPU_lgl_EvalCoord1dv(const GLdouble *u)
{
	lgl_evalcoord1f((float)u[0]);
}

void GPU_lgl_VertexAttrib1s(GLuint index, GLshort x)
{
	if (!GPU_lgl_on) {
		glVertexAttrib1s(index, x);
		return;
	}
	lgl_attrib4f(index, (float)(x), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib1sv(GLuint index, const GLshort *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib1sv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib1f(GLuint index, GLfloat x)
{
	if (!GPU_lgl_on) {
		glVertexAttrib1f(index, x);
		return;
	}
	lgl_attrib4f(index, (float)(x), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib1fv(GLuint index, const GLfloat *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib1fv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib1d(GLuint index, GLdouble x)
{
	if (!GPU_lgl_on) {
		glVertexAttrib1d(index, x);
		return;
	}
	lgl_attrib4f(index, (float)(x), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib1dv(GLuint index, const GLdouble *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib1dv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), 0.0f, 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib2s(GLuint index, GLshort x, GLshort y)
{
	if (!GPU_lgl_on) {
		glVertexAttrib2s(index, x, y);
		return;
	}
	lgl_attrib4f(index, (float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib2sv(GLuint index, const GLshort *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib2sv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib2f(GLuint index, GLfloat x, GLfloat y)
{
	if (!GPU_lgl_on) {
		glVertexAttrib2f(index, x, y);
		return;
	}
	lgl_attrib4f(index, (float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib2fv(GLuint index, const GLfloat *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib2fv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib2d(GLuint index, GLdouble x, GLdouble y)
{
	if (!GPU_lgl_on) {
		glVertexAttrib2d(index, x, y);
		return;
	}
	lgl_attrib4f(index, (float)(x), (float)(y), 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib2dv(GLuint index, const GLdouble *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib2dv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), 0.0f, 1.0f);
}

void GPU_lgl_VertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z)
{
	if (!GPU_lgl_on) {
		glVertexAttrib3s(index, x, y, z);
		return;
	}
	lgl_attrib4f(index, (float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_VertexAttrib3sv(GLuint index, const GLshort *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib3sv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z)
{
	if (!GPU_lgl_on) {
		glVertexAttrib3f(index, x, y, z);
		return;
	}
	lgl_attrib4f(index, (float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_VertexAttrib3fv(GLuint index, const GLfloat *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib3fv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_VertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z)
{
	if (!GPU_lgl_on) {
		glVertexAttrib3d(index, x, y, z);
		return;
	}
	lgl_attrib4f(index, (float)(x), (float)(y), (float)(z), 1.0f);
}

void GPU_lgl_VertexAttrib3dv(GLuint index, const GLdouble *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib3dv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), 1.0f);
}

void GPU_lgl_VertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4s(index, x, y, z, w);
		return;
	}
	lgl_attrib4f(index, (float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_VertexAttrib4sv(GLuint index, const GLshort *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4sv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4f(index, x, y, z, w);
		return;
	}
	lgl_attrib4f(index, (float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_VertexAttrib4fv(GLuint index, const GLfloat *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4fv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_VertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4d(index, x, y, z, w);
		return;
	}
	lgl_attrib4f(index, (float)(x), (float)(y), (float)(z), (float)(w));
}

void GPU_lgl_VertexAttrib4dv(GLuint index, const GLdouble *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4dv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_VertexAttrib4bv(GLuint index, const GLbyte *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4bv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_VertexAttrib4iv(GLuint index, const GLint *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4iv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_VertexAttrib4ubv(GLuint index, const GLubyte *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4ubv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_VertexAttrib4usv(GLuint index, const GLushort *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4usv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_VertexAttrib4uiv(GLuint index, const GLuint *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4uiv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]), (float)(v[1]), (float)(v[2]), (float)(v[3]));
}

void GPU_lgl_VertexAttrib4Nbv(GLuint index, const GLbyte *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4Nbv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]) / 127.0f, (float)(v[1]) / 127.0f, (float)(v[2]) / 127.0f, (float)(v[3]) / 127.0f);
}

void GPU_lgl_VertexAttrib4Nsv(GLuint index, const GLshort *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4Nsv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]) / 32767.0f, (float)(v[1]) / 32767.0f, (float)(v[2]) / 32767.0f, (float)(v[3]) / 32767.0f);
}

void GPU_lgl_VertexAttrib4Niv(GLuint index, const GLint *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4Niv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]) / 2147483647.0f, (float)(v[1]) / 2147483647.0f, (float)(v[2]) / 2147483647.0f, (float)(v[3]) / 2147483647.0f);
}

void GPU_lgl_VertexAttrib4Nubv(GLuint index, const GLubyte *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4Nubv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]) / 255.0f, (float)(v[1]) / 255.0f, (float)(v[2]) / 255.0f, (float)(v[3]) / 255.0f);
}

void GPU_lgl_VertexAttrib4Nusv(GLuint index, const GLushort *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4Nusv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]) / 65535.0f, (float)(v[1]) / 65535.0f, (float)(v[2]) / 65535.0f, (float)(v[3]) / 65535.0f);
}

void GPU_lgl_VertexAttrib4Nuiv(GLuint index, const GLuint *v)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4Nuiv(index, v);
		return;
	}
	lgl_attrib4f(index, (float)(v[0]) / 4294967295.0f, (float)(v[1]) / 4294967295.0f, (float)(v[2]) / 4294967295.0f, (float)(v[3]) / 4294967295.0f);
}

void GPU_lgl_VertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w)
{
	if (!GPU_lgl_on) {
		glVertexAttrib4Nub(index, x, y, z, w);
		return;
	}
	lgl_attrib4f(index, x / 255.0f, y / 255.0f, z / 255.0f, w / 255.0f);
}

/* old functions the emulation does not do (bgl's notes) */
static const char *lgl_unsupported_names[] = {
	"glAccum",
	"glClearAccum",
	"glClearIndex",
	"glCopyPixels",
	"glEdgeFlag",
	"glEdgeFlagv",
	"glEdgeFlagPointer",
	"glEvalCoord2d",
	"glEvalCoord2dv",
	"glEvalCoord2f",
	"glEvalCoord2fv",
	"glEvalMesh2",
	"glEvalPoint2",
	"glFeedbackBuffer",
	"glGetMapdv",
	"glGetMapfv",
	"glGetMapiv",
	"glGetPixelMapfv",
	"glGetPixelMapuiv",
	"glGetPixelMapusv",
	"glIndexMask",
	"glIndexd",
	"glIndexdv",
	"glIndexf",
	"glIndexfv",
	"glIndexi",
	"glIndexiv",
	"glIndexs",
	"glIndexsv",
	"glIndexub",
	"glIndexubv",
	"glIndexPointer",
	"glMap2d",
	"glMap2f",
	"glMapGrid2d",
	"glMapGrid2f",
	"glPassThrough",
	"glPixelMapfv",
	"glPixelMapuiv",
	"glPixelMapusv",
	"glInterleavedArrays",
	"glGetPointerv",
	"glRenderMode",
	"glSelectBuffer",
	"glInitNames",
	"glLoadName",
	"glPushName",
	"glPopName",
	"glColorP3ui",
	"glColorP3uiv",
	"glColorP4ui",
	"glColorP4uiv",
	"glNormalP3ui",
	"glNormalP3uiv",
	"glSecondaryColorP3ui",
	"glSecondaryColorP3uiv",
	"glTexCoordP1ui",
	"glTexCoordP1uiv",
	"glTexCoordP2ui",
	"glTexCoordP2uiv",
	"glTexCoordP3ui",
	"glTexCoordP3uiv",
	"glTexCoordP4ui",
	"glTexCoordP4uiv",
	"glVertexP2ui",
	"glVertexP2uiv",
	"glVertexP3ui",
	"glVertexP3uiv",
	"glVertexP4ui",
	"glVertexP4uiv",
	"glMultiTexCoordP1ui",
	"glMultiTexCoordP1uiv",
	"glMultiTexCoordP2ui",
	"glMultiTexCoordP2uiv",
	"glMultiTexCoordP3ui",
	"glMultiTexCoordP3uiv",
	"glMultiTexCoordP4ui",
	"glMultiTexCoordP4uiv",
	NULL,
};


const char **gpu_lgl_unsupported_names(void)
{
	return lgl_unsupported_names;
}
