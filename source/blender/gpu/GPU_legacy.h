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

/** \file GPU_legacy.h
 *  \ingroup gpu
 *
 * BGE-Remix P8.26: the old OpenGL the editors, the game's leftovers and
 * Python's bgl still call, on an OpenGL 4.5 core context. Each old call is
 * redirected here: while the emulation is on (GPU_lgl_on: a core context,
 * or BGE_REMIX_GL_EMULATE=1) it is done with core features only
 * (gpu_legacy.c: immediate mode, client arrays, display lists, the fixed
 * function look as a shader, the matrix and attribute stacks, raster
 * operations); otherwise the real function is called, as before.
 *
 * Written by tests/../scripts/gen_legacy.py: change that, not this.
 *
 * Files that must call the real functions define GPU_LEGACY_NO_REDIRECT
 * before including GPU_glew.h (the prototypes stay).
 */

#ifndef __GPU_LEGACY_H__
#define __GPU_LEGACY_H__

#ifdef __cplusplus
extern "C" {
#endif

/* the emulation is on (set once the context is known: GPU_init) */
extern int GPU_lgl_on;

/* the ARB shader-object calls, for the core names */
void GPU_lgl_DeleteObject(GLuint a);
void GPU_lgl_GetObjectParameteriv(GLuint a, GLenum b, GLint *c);
void GPU_lgl_GetInfoLog(GLuint a, GLsizei b, GLsizei *c, GLchar *d);

/* GLU's quadrics and matrices, drawn with the calls above (GLU itself
 * calls the real old functions) */
void *GPU_glu_NewQuadric(void);
void GPU_glu_DeleteQuadric(void *q);
void GPU_glu_QuadricDrawStyle(void *q, GLenum style);
void GPU_glu_QuadricNormals(void *q, GLenum normals);
void GPU_glu_QuadricOrientation(void *q, GLenum orientation);
void GPU_glu_QuadricTexture(void *q, GLboolean texture);
void GPU_glu_Sphere(void *q, GLdouble radius, GLint slices, GLint stacks);
void GPU_glu_Cylinder(void *q, GLdouble base, GLdouble top, GLdouble height, GLint slices, GLint stacks);
void GPU_glu_Disk(void *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops);
void GPU_glu_PartialDisk(void *q, GLdouble inner, GLdouble outer, GLint slices, GLint loops,
                         GLdouble start, GLdouble sweep);
void GPU_glu_Perspective(GLdouble fovy, GLdouble aspect, GLdouble znear, GLdouble zfar);
void GPU_glu_Ortho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);
void GPU_glu_LookAt(GLdouble ex, GLdouble ey, GLdouble ez, GLdouble cx, GLdouble cy, GLdouble cz,
                    GLdouble ux, GLdouble uy, GLdouble uz);
void GPU_glu_PickMatrix(GLdouble x, GLdouble y, GLdouble w, GLdouble h, const GLint viewport[4]);

/* for bgl: whether "glName" is an old function the emulation does not do */
int GPU_lgl_unsupported(const char *name);

/* ---- OpenGL 1.1 (gl.h): the real function unless emulating ---- */

void GPU_lgl_Begin(GLenum a);
void GPU_lgl_End(void);
void GPU_lgl_Bitmap(GLsizei a, GLsizei b, GLfloat c, GLfloat d, GLfloat e, GLfloat f, const GLubyte *g);
void GPU_lgl_DrawPixels(GLsizei a, GLsizei b, GLenum c, GLenum d, const void *e);
void GPU_lgl_PixelTransferf(GLenum a, GLfloat b);
void GPU_lgl_PixelTransferi(GLenum a, GLint b);
void GPU_lgl_PixelZoom(GLfloat a, GLfloat b);
void GPU_lgl_MatrixMode(GLenum a);
void GPU_lgl_LoadIdentity(void);
void GPU_lgl_LoadMatrixf(const GLfloat *a);
void GPU_lgl_LoadMatrixd(const GLdouble *a);
void GPU_lgl_MultMatrixf(const GLfloat *a);
void GPU_lgl_MultMatrixd(const GLdouble *a);
void GPU_lgl_PushMatrix(void);
void GPU_lgl_PopMatrix(void);
void GPU_lgl_Translatef(GLfloat a, GLfloat b, GLfloat c);
void GPU_lgl_Translated(GLdouble a, GLdouble b, GLdouble c);
void GPU_lgl_Rotatef(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void GPU_lgl_Rotated(GLdouble a, GLdouble b, GLdouble c, GLdouble d);
void GPU_lgl_Scalef(GLfloat a, GLfloat b, GLfloat c);
void GPU_lgl_Scaled(GLdouble a, GLdouble b, GLdouble c);
void GPU_lgl_Ortho(GLdouble a, GLdouble b, GLdouble c, GLdouble d, GLdouble e, GLdouble f);
void GPU_lgl_Frustum(GLdouble a, GLdouble b, GLdouble c, GLdouble d, GLdouble e, GLdouble f);
void GPU_lgl_Enable(GLenum a);
void GPU_lgl_Disable(GLenum a);
GLboolean GPU_lgl_IsEnabled(GLenum a);
void GPU_lgl_GetIntegerv(GLenum a, GLint *b);
void GPU_lgl_GetFloatv(GLenum a, GLfloat *b);
void GPU_lgl_GetDoublev(GLenum a, GLdouble *b);
void GPU_lgl_GetBooleanv(GLenum a, GLboolean *b);
const GLubyte * GPU_lgl_GetString(GLenum a);
void GPU_lgl_TexImage1D(GLenum a, GLint b, GLint c, GLsizei d, GLint e, GLenum f, GLenum g, const void *h);
void GPU_lgl_TexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void *i);
void GPU_lgl_TexSubImage1D(GLenum a, GLint b, GLint c, GLsizei d, GLenum e, GLenum f, const void *g);
void GPU_lgl_TexSubImage2D(GLenum a, GLint b, GLint c, GLint d, GLsizei e, GLsizei f, GLenum g, GLenum h, const void *i);
void GPU_lgl_CopyTexImage1D(GLenum a, GLint b, GLenum c, GLint d, GLint e, GLsizei f, GLint g);
void GPU_lgl_CopyTexImage2D(GLenum a, GLint b, GLenum c, GLint d, GLint e, GLsizei f, GLsizei g, GLint h);
void GPU_lgl_GetTexImage(GLenum a, GLint b, GLenum c, GLenum d, void *e);
void GPU_lgl_ReadPixels(GLint a, GLint b, GLsizei c, GLsizei d, GLenum e, GLenum f, void *g);
void GPU_lgl_TexParameteri(GLenum a, GLenum b, GLint c);
void GPU_lgl_TexParameterf(GLenum a, GLenum b, GLfloat c);
void GPU_lgl_TexParameteriv(GLenum a, GLenum b, const GLint *c);
void GPU_lgl_TexParameterfv(GLenum a, GLenum b, const GLfloat *c);
void GPU_lgl_TexEnvi(GLenum a, GLenum b, GLint c);
void GPU_lgl_TexEnvf(GLenum a, GLenum b, GLfloat c);
void GPU_lgl_TexEnviv(GLenum a, GLenum b, const GLint *c);
void GPU_lgl_TexEnvfv(GLenum a, GLenum b, const GLfloat *c);
void GPU_lgl_GetTexEnviv(GLenum a, GLenum b, GLint *c);
void GPU_lgl_GetTexEnvfv(GLenum a, GLenum b, GLfloat *c);
void GPU_lgl_TexGeni(GLenum a, GLenum b, GLint c);
void GPU_lgl_TexGenf(GLenum a, GLenum b, GLfloat c);
void GPU_lgl_TexGend(GLenum a, GLenum b, GLdouble c);
void GPU_lgl_TexGeniv(GLenum a, GLenum b, const GLint *c);
void GPU_lgl_TexGenfv(GLenum a, GLenum b, const GLfloat *c);
void GPU_lgl_TexGendv(GLenum a, GLenum b, const GLdouble *c);
void GPU_lgl_GetTexGeniv(GLenum a, GLenum b, GLint *c);
void GPU_lgl_GetTexGenfv(GLenum a, GLenum b, GLfloat *c);
void GPU_lgl_GetTexGendv(GLenum a, GLenum b, GLdouble *c);
void GPU_lgl_Lightf(GLenum a, GLenum b, GLfloat c);
void GPU_lgl_Lighti(GLenum a, GLenum b, GLint c);
void GPU_lgl_Lightfv(GLenum a, GLenum b, const GLfloat *c);
void GPU_lgl_Lightiv(GLenum a, GLenum b, const GLint *c);
void GPU_lgl_GetLightfv(GLenum a, GLenum b, GLfloat *c);
void GPU_lgl_GetLightiv(GLenum a, GLenum b, GLint *c);
void GPU_lgl_LightModelf(GLenum a, GLfloat b);
void GPU_lgl_LightModeli(GLenum a, GLint b);
void GPU_lgl_LightModelfv(GLenum a, const GLfloat *b);
void GPU_lgl_LightModeliv(GLenum a, const GLint *b);
void GPU_lgl_Materialf(GLenum a, GLenum b, GLfloat c);
void GPU_lgl_Materiali(GLenum a, GLenum b, GLint c);
void GPU_lgl_Materialfv(GLenum a, GLenum b, const GLfloat *c);
void GPU_lgl_Materialiv(GLenum a, GLenum b, const GLint *c);
void GPU_lgl_GetMaterialfv(GLenum a, GLenum b, GLfloat *c);
void GPU_lgl_GetMaterialiv(GLenum a, GLenum b, GLint *c);
void GPU_lgl_ColorMaterial(GLenum a, GLenum b);
void GPU_lgl_ShadeModel(GLenum a);
void GPU_lgl_AlphaFunc(GLenum a, GLfloat b);
void GPU_lgl_LineStipple(GLint a, GLushort b);
void GPU_lgl_PolygonStipple(const GLubyte *a);
void GPU_lgl_GetPolygonStipple(GLubyte *a);
void GPU_lgl_ClipPlane(GLenum a, const GLdouble *b);
void GPU_lgl_GetClipPlane(GLenum a, GLdouble *b);
void GPU_lgl_PushAttrib(GLbitfield a);
void GPU_lgl_PopAttrib(void);
void GPU_lgl_PushClientAttrib(GLbitfield a);
void GPU_lgl_PopClientAttrib(void);
void GPU_lgl_EnableClientState(GLenum a);
void GPU_lgl_DisableClientState(GLenum a);
void GPU_lgl_VertexPointer(GLint a, GLenum b, GLsizei c, const void *d);
void GPU_lgl_NormalPointer(GLenum a, GLsizei b, const void *c);
void GPU_lgl_ColorPointer(GLint a, GLenum b, GLsizei c, const void *d);
void GPU_lgl_TexCoordPointer(GLint a, GLenum b, GLsizei c, const void *d);
void GPU_lgl_InterleavedArrays(GLenum a, GLsizei b, const void *c);
void GPU_lgl_ArrayElement(GLint a);
void GPU_lgl_DrawArrays(GLenum a, GLint b, GLsizei c);
void GPU_lgl_DrawElements(GLenum a, GLsizei b, GLenum c, const void *d);
void GPU_lgl_NewList(GLuint a, GLenum b);
void GPU_lgl_EndList(void);
void GPU_lgl_CallList(GLuint a);
void GPU_lgl_CallLists(GLsizei a, GLenum b, const void *c);
GLuint GPU_lgl_GenLists(GLsizei a);
void GPU_lgl_DeleteLists(GLuint a, GLsizei b);
GLboolean GPU_lgl_IsList(GLuint a);
void GPU_lgl_ListBase(GLuint a);
void GPU_lgl_Map1f(GLenum a, GLfloat b, GLfloat c, GLint d, GLint e, const GLfloat *f);
void GPU_lgl_Map1d(GLenum a, GLdouble b, GLdouble c, GLint d, GLint e, const GLdouble *f);
void GPU_lgl_MapGrid1f(GLint a, GLfloat b, GLfloat c);
void GPU_lgl_MapGrid1d(GLint a, GLdouble b, GLdouble c);
void GPU_lgl_EvalMesh1(GLenum a, GLint b, GLint c);
void GPU_lgl_EvalPoint1(GLint a);
void GPU_lgl_Fogf(GLenum a, GLfloat b);
void GPU_lgl_Fogi(GLenum a, GLint b);
void GPU_lgl_Fogfv(GLenum a, const GLfloat *b);
void GPU_lgl_Fogiv(GLenum a, const GLint *b);
void GPU_lgl_PolygonMode(GLenum a, GLenum b);
GLint GPU_lgl_RenderMode(GLenum a);
void GPU_lgl_SelectBuffer(GLsizei a, GLuint *b);
void GPU_lgl_InitNames(void);
void GPU_lgl_LoadName(GLuint a);
void GPU_lgl_PushName(GLuint a);
void GPU_lgl_PopName(void);
void GPU_lgl_Hint(GLenum a, GLenum b);
void GPU_lgl_BindTexture(GLenum a, GLuint b);
void GPU_lgl_DeleteTextures(GLsizei a, const GLuint *b);
GLboolean GPU_lgl_AreTexturesResident(GLsizei a, const GLuint *b, GLboolean *c);
void GPU_lgl_PrioritizeTextures(GLsizei a, const GLuint *b, const GLclampf *c);
void GPU_lgl_Vertex2s(GLshort a, GLshort b);
void GPU_lgl_Vertex2sv(const GLshort *a);
void GPU_lgl_Vertex2i(GLint a, GLint b);
void GPU_lgl_Vertex2iv(const GLint *a);
void GPU_lgl_Vertex2f(GLfloat a, GLfloat b);
void GPU_lgl_Vertex2fv(const GLfloat *a);
void GPU_lgl_Vertex2d(GLdouble a, GLdouble b);
void GPU_lgl_Vertex2dv(const GLdouble *a);
void GPU_lgl_Vertex3s(GLshort a, GLshort b, GLshort c);
void GPU_lgl_Vertex3sv(const GLshort *a);
void GPU_lgl_Vertex3i(GLint a, GLint b, GLint c);
void GPU_lgl_Vertex3iv(const GLint *a);
void GPU_lgl_Vertex3f(GLfloat a, GLfloat b, GLfloat c);
void GPU_lgl_Vertex3fv(const GLfloat *a);
void GPU_lgl_Vertex3d(GLdouble a, GLdouble b, GLdouble c);
void GPU_lgl_Vertex3dv(const GLdouble *a);
void GPU_lgl_Vertex4s(GLshort a, GLshort b, GLshort c, GLshort d);
void GPU_lgl_Vertex4sv(const GLshort *a);
void GPU_lgl_Vertex4i(GLint a, GLint b, GLint c, GLint d);
void GPU_lgl_Vertex4iv(const GLint *a);
void GPU_lgl_Vertex4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void GPU_lgl_Vertex4fv(const GLfloat *a);
void GPU_lgl_Vertex4d(GLdouble a, GLdouble b, GLdouble c, GLdouble d);
void GPU_lgl_Vertex4dv(const GLdouble *a);
void GPU_lgl_Color3b(GLbyte a, GLbyte b, GLbyte c);
void GPU_lgl_Color3bv(const GLbyte *a);
void GPU_lgl_Color3s(GLshort a, GLshort b, GLshort c);
void GPU_lgl_Color3sv(const GLshort *a);
void GPU_lgl_Color3i(GLint a, GLint b, GLint c);
void GPU_lgl_Color3iv(const GLint *a);
void GPU_lgl_Color3f(GLfloat a, GLfloat b, GLfloat c);
void GPU_lgl_Color3fv(const GLfloat *a);
void GPU_lgl_Color3d(GLdouble a, GLdouble b, GLdouble c);
void GPU_lgl_Color3dv(const GLdouble *a);
void GPU_lgl_Color3ub(GLubyte a, GLubyte b, GLubyte c);
void GPU_lgl_Color3ubv(const GLubyte *a);
void GPU_lgl_Color3us(GLushort a, GLushort b, GLushort c);
void GPU_lgl_Color3usv(const GLushort *a);
void GPU_lgl_Color3ui(GLuint a, GLuint b, GLuint c);
void GPU_lgl_Color3uiv(const GLuint *a);
void GPU_lgl_Color4b(GLbyte a, GLbyte b, GLbyte c, GLbyte d);
void GPU_lgl_Color4bv(const GLbyte *a);
void GPU_lgl_Color4s(GLshort a, GLshort b, GLshort c, GLshort d);
void GPU_lgl_Color4sv(const GLshort *a);
void GPU_lgl_Color4i(GLint a, GLint b, GLint c, GLint d);
void GPU_lgl_Color4iv(const GLint *a);
void GPU_lgl_Color4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void GPU_lgl_Color4fv(const GLfloat *a);
void GPU_lgl_Color4d(GLdouble a, GLdouble b, GLdouble c, GLdouble d);
void GPU_lgl_Color4dv(const GLdouble *a);
void GPU_lgl_Color4ub(GLubyte a, GLubyte b, GLubyte c, GLubyte d);
void GPU_lgl_Color4ubv(const GLubyte *a);
void GPU_lgl_Color4us(GLushort a, GLushort b, GLushort c, GLushort d);
void GPU_lgl_Color4usv(const GLushort *a);
void GPU_lgl_Color4ui(GLuint a, GLuint b, GLuint c, GLuint d);
void GPU_lgl_Color4uiv(const GLuint *a);
void GPU_lgl_Normal3b(GLbyte a, GLbyte b, GLbyte c);
void GPU_lgl_Normal3bv(const GLbyte *a);
void GPU_lgl_Normal3s(GLshort a, GLshort b, GLshort c);
void GPU_lgl_Normal3sv(const GLshort *a);
void GPU_lgl_Normal3i(GLint a, GLint b, GLint c);
void GPU_lgl_Normal3iv(const GLint *a);
void GPU_lgl_Normal3f(GLfloat a, GLfloat b, GLfloat c);
void GPU_lgl_Normal3fv(const GLfloat *a);
void GPU_lgl_Normal3d(GLdouble a, GLdouble b, GLdouble c);
void GPU_lgl_Normal3dv(const GLdouble *a);
void GPU_lgl_TexCoord1s(GLshort a);
void GPU_lgl_TexCoord1sv(const GLshort *a);
void GPU_lgl_TexCoord1i(GLint a);
void GPU_lgl_TexCoord1iv(const GLint *a);
void GPU_lgl_TexCoord1f(GLfloat a);
void GPU_lgl_TexCoord1fv(const GLfloat *a);
void GPU_lgl_TexCoord1d(GLdouble a);
void GPU_lgl_TexCoord1dv(const GLdouble *a);
void GPU_lgl_TexCoord2s(GLshort a, GLshort b);
void GPU_lgl_TexCoord2sv(const GLshort *a);
void GPU_lgl_TexCoord2i(GLint a, GLint b);
void GPU_lgl_TexCoord2iv(const GLint *a);
void GPU_lgl_TexCoord2f(GLfloat a, GLfloat b);
void GPU_lgl_TexCoord2fv(const GLfloat *a);
void GPU_lgl_TexCoord2d(GLdouble a, GLdouble b);
void GPU_lgl_TexCoord2dv(const GLdouble *a);
void GPU_lgl_TexCoord3s(GLshort a, GLshort b, GLshort c);
void GPU_lgl_TexCoord3sv(const GLshort *a);
void GPU_lgl_TexCoord3i(GLint a, GLint b, GLint c);
void GPU_lgl_TexCoord3iv(const GLint *a);
void GPU_lgl_TexCoord3f(GLfloat a, GLfloat b, GLfloat c);
void GPU_lgl_TexCoord3fv(const GLfloat *a);
void GPU_lgl_TexCoord3d(GLdouble a, GLdouble b, GLdouble c);
void GPU_lgl_TexCoord3dv(const GLdouble *a);
void GPU_lgl_TexCoord4s(GLshort a, GLshort b, GLshort c, GLshort d);
void GPU_lgl_TexCoord4sv(const GLshort *a);
void GPU_lgl_TexCoord4i(GLint a, GLint b, GLint c, GLint d);
void GPU_lgl_TexCoord4iv(const GLint *a);
void GPU_lgl_TexCoord4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void GPU_lgl_TexCoord4fv(const GLfloat *a);
void GPU_lgl_TexCoord4d(GLdouble a, GLdouble b, GLdouble c, GLdouble d);
void GPU_lgl_TexCoord4dv(const GLdouble *a);
void GPU_lgl_RasterPos2s(GLshort a, GLshort b);
void GPU_lgl_RasterPos2sv(const GLshort *a);
void GPU_lgl_RasterPos2i(GLint a, GLint b);
void GPU_lgl_RasterPos2iv(const GLint *a);
void GPU_lgl_RasterPos2f(GLfloat a, GLfloat b);
void GPU_lgl_RasterPos2fv(const GLfloat *a);
void GPU_lgl_RasterPos2d(GLdouble a, GLdouble b);
void GPU_lgl_RasterPos2dv(const GLdouble *a);
void GPU_lgl_RasterPos3s(GLshort a, GLshort b, GLshort c);
void GPU_lgl_RasterPos3sv(const GLshort *a);
void GPU_lgl_RasterPos3i(GLint a, GLint b, GLint c);
void GPU_lgl_RasterPos3iv(const GLint *a);
void GPU_lgl_RasterPos3f(GLfloat a, GLfloat b, GLfloat c);
void GPU_lgl_RasterPos3fv(const GLfloat *a);
void GPU_lgl_RasterPos3d(GLdouble a, GLdouble b, GLdouble c);
void GPU_lgl_RasterPos3dv(const GLdouble *a);
void GPU_lgl_RasterPos4s(GLshort a, GLshort b, GLshort c, GLshort d);
void GPU_lgl_RasterPos4sv(const GLshort *a);
void GPU_lgl_RasterPos4i(GLint a, GLint b, GLint c, GLint d);
void GPU_lgl_RasterPos4iv(const GLint *a);
void GPU_lgl_RasterPos4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void GPU_lgl_RasterPos4fv(const GLfloat *a);
void GPU_lgl_RasterPos4d(GLdouble a, GLdouble b, GLdouble c, GLdouble d);
void GPU_lgl_RasterPos4dv(const GLdouble *a);
void GPU_lgl_Rects(GLshort a, GLshort b, GLshort c, GLshort d);
void GPU_lgl_Rectsv(const GLshort *a, const GLshort *b);
void GPU_lgl_Recti(GLint a, GLint b, GLint c, GLint d);
void GPU_lgl_Rectiv(const GLint *a, const GLint *b);
void GPU_lgl_Rectf(GLfloat a, GLfloat b, GLfloat c, GLfloat d);
void GPU_lgl_Rectfv(const GLfloat *a, const GLfloat *b);
void GPU_lgl_Rectd(GLdouble a, GLdouble b, GLdouble c, GLdouble d);
void GPU_lgl_Rectdv(const GLdouble *a, const GLdouble *b);
void GPU_lgl_EvalCoord1f(GLfloat a);
void GPU_lgl_EvalCoord1fv(const GLfloat *a);
void GPU_lgl_EvalCoord1d(GLdouble a);
void GPU_lgl_EvalCoord1dv(const GLdouble *a);

/* ---- GLEW functions: always through the emulation's function ---- */

void GPU_lgl_UseProgram(GLuint a);
void GPU_lgl_BindVertexArray(GLuint a);
void GPU_lgl_DeleteVertexArrays(GLsizei a, const GLuint *b);
void GPU_lgl_BindBuffer(GLenum a, GLuint b);
void GPU_lgl_DeleteBuffers(GLsizei a, const GLuint *b);
void GPU_lgl_VertexAttribPointer(GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void *f);
void GPU_lgl_VertexAttribIPointer(GLuint a, GLint b, GLenum c, GLsizei d, const void *e);
void GPU_lgl_EnableVertexAttribArray(GLuint a);
void GPU_lgl_DisableVertexAttribArray(GLuint a);
void GPU_lgl_VertexAttribDivisor(GLuint a, GLuint b);
void GPU_lgl_ActiveTexture(GLenum a);
void GPU_lgl_ClientActiveTexture(GLenum a);
void GPU_lgl_DrawRangeElements(GLenum a, GLuint b, GLuint c, GLsizei d, GLenum e, const void *f);
void GPU_lgl_DrawArraysInstanced(GLenum a, GLint b, GLsizei c, GLsizei d);
void GPU_lgl_DrawElementsInstanced(GLenum a, GLsizei b, GLenum c, const void *d, GLsizei e);
void GPU_lgl_ShaderSource(GLuint a, GLsizei b, const GLchar *const *c, const GLint *d);
void GPU_lgl_LinkProgram(GLuint a);
void GPU_lgl_DeleteProgram(GLuint a);
GLint GPU_lgl_GetUniformLocation(GLuint a, const GLchar *b);
GLint GPU_lgl_GetAttribLocation(GLuint a, const GLchar *b);
void GPU_lgl_BindAttribLocation(GLuint a, GLuint b, const GLchar *c);
void GPU_lgl_ProgramParameteriEXT(GLuint a, GLenum b, GLint c);
void GPU_lgl_LoadTransposeMatrixf(const GLfloat *a);
void GPU_lgl_LoadTransposeMatrixd(const GLdouble *a);
void GPU_lgl_MultTransposeMatrixf(const GLfloat *a);
void GPU_lgl_MultTransposeMatrixd(const GLdouble *a);
void GPU_lgl_MultiTexCoord1s(GLenum a, GLshort b);
void GPU_lgl_MultiTexCoord1sv(GLenum a, const GLshort *b);
void GPU_lgl_MultiTexCoord1i(GLenum a, GLint b);
void GPU_lgl_MultiTexCoord1iv(GLenum a, const GLint *b);
void GPU_lgl_MultiTexCoord1f(GLenum a, GLfloat b);
void GPU_lgl_MultiTexCoord1fv(GLenum a, const GLfloat *b);
void GPU_lgl_MultiTexCoord1d(GLenum a, GLdouble b);
void GPU_lgl_MultiTexCoord1dv(GLenum a, const GLdouble *b);
void GPU_lgl_MultiTexCoord2s(GLenum a, GLshort b, GLshort c);
void GPU_lgl_MultiTexCoord2sv(GLenum a, const GLshort *b);
void GPU_lgl_MultiTexCoord2i(GLenum a, GLint b, GLint c);
void GPU_lgl_MultiTexCoord2iv(GLenum a, const GLint *b);
void GPU_lgl_MultiTexCoord2f(GLenum a, GLfloat b, GLfloat c);
void GPU_lgl_MultiTexCoord2fv(GLenum a, const GLfloat *b);
void GPU_lgl_MultiTexCoord2d(GLenum a, GLdouble b, GLdouble c);
void GPU_lgl_MultiTexCoord2dv(GLenum a, const GLdouble *b);
void GPU_lgl_MultiTexCoord3s(GLenum a, GLshort b, GLshort c, GLshort d);
void GPU_lgl_MultiTexCoord3sv(GLenum a, const GLshort *b);
void GPU_lgl_MultiTexCoord3i(GLenum a, GLint b, GLint c, GLint d);
void GPU_lgl_MultiTexCoord3iv(GLenum a, const GLint *b);
void GPU_lgl_MultiTexCoord3f(GLenum a, GLfloat b, GLfloat c, GLfloat d);
void GPU_lgl_MultiTexCoord3fv(GLenum a, const GLfloat *b);
void GPU_lgl_MultiTexCoord3d(GLenum a, GLdouble b, GLdouble c, GLdouble d);
void GPU_lgl_MultiTexCoord3dv(GLenum a, const GLdouble *b);
void GPU_lgl_MultiTexCoord4s(GLenum a, GLshort b, GLshort c, GLshort d, GLshort e);
void GPU_lgl_MultiTexCoord4sv(GLenum a, const GLshort *b);
void GPU_lgl_MultiTexCoord4i(GLenum a, GLint b, GLint c, GLint d, GLint e);
void GPU_lgl_MultiTexCoord4iv(GLenum a, const GLint *b);
void GPU_lgl_MultiTexCoord4f(GLenum a, GLfloat b, GLfloat c, GLfloat d, GLfloat e);
void GPU_lgl_MultiTexCoord4fv(GLenum a, const GLfloat *b);
void GPU_lgl_MultiTexCoord4d(GLenum a, GLdouble b, GLdouble c, GLdouble d, GLdouble e);
void GPU_lgl_MultiTexCoord4dv(GLenum a, const GLdouble *b);
void GPU_lgl_VertexAttrib1s(GLuint a, GLshort b);
void GPU_lgl_VertexAttrib1sv(GLuint a, const GLshort *b);
void GPU_lgl_VertexAttrib1f(GLuint a, GLfloat b);
void GPU_lgl_VertexAttrib1fv(GLuint a, const GLfloat *b);
void GPU_lgl_VertexAttrib1d(GLuint a, GLdouble b);
void GPU_lgl_VertexAttrib1dv(GLuint a, const GLdouble *b);
void GPU_lgl_VertexAttrib2s(GLuint a, GLshort b, GLshort c);
void GPU_lgl_VertexAttrib2sv(GLuint a, const GLshort *b);
void GPU_lgl_VertexAttrib2f(GLuint a, GLfloat b, GLfloat c);
void GPU_lgl_VertexAttrib2fv(GLuint a, const GLfloat *b);
void GPU_lgl_VertexAttrib2d(GLuint a, GLdouble b, GLdouble c);
void GPU_lgl_VertexAttrib2dv(GLuint a, const GLdouble *b);
void GPU_lgl_VertexAttrib3s(GLuint a, GLshort b, GLshort c, GLshort d);
void GPU_lgl_VertexAttrib3sv(GLuint a, const GLshort *b);
void GPU_lgl_VertexAttrib3f(GLuint a, GLfloat b, GLfloat c, GLfloat d);
void GPU_lgl_VertexAttrib3fv(GLuint a, const GLfloat *b);
void GPU_lgl_VertexAttrib3d(GLuint a, GLdouble b, GLdouble c, GLdouble d);
void GPU_lgl_VertexAttrib3dv(GLuint a, const GLdouble *b);
void GPU_lgl_VertexAttrib4s(GLuint a, GLshort b, GLshort c, GLshort d, GLshort e);
void GPU_lgl_VertexAttrib4sv(GLuint a, const GLshort *b);
void GPU_lgl_VertexAttrib4f(GLuint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e);
void GPU_lgl_VertexAttrib4fv(GLuint a, const GLfloat *b);
void GPU_lgl_VertexAttrib4d(GLuint a, GLdouble b, GLdouble c, GLdouble d, GLdouble e);
void GPU_lgl_VertexAttrib4dv(GLuint a, const GLdouble *b);
void GPU_lgl_VertexAttrib4bv(GLuint a, const GLbyte *b);
void GPU_lgl_VertexAttrib4iv(GLuint a, const GLint *b);
void GPU_lgl_VertexAttrib4ubv(GLuint a, const GLubyte *b);
void GPU_lgl_VertexAttrib4usv(GLuint a, const GLushort *b);
void GPU_lgl_VertexAttrib4uiv(GLuint a, const GLuint *b);
void GPU_lgl_VertexAttrib4Nbv(GLuint a, const GLbyte *b);
void GPU_lgl_VertexAttrib4Nsv(GLuint a, const GLshort *b);
void GPU_lgl_VertexAttrib4Niv(GLuint a, const GLint *b);
void GPU_lgl_VertexAttrib4Nubv(GLuint a, const GLubyte *b);
void GPU_lgl_VertexAttrib4Nusv(GLuint a, const GLushort *b);
void GPU_lgl_VertexAttrib4Nuiv(GLuint a, const GLuint *b);
void GPU_lgl_VertexAttrib4Nub(GLuint a, GLubyte b, GLubyte c, GLubyte d, GLubyte e);

#ifdef __cplusplus
}
#endif

/* ---- the redirects ---- */

#ifndef GPU_LEGACY_NO_REDIRECT

#define glBegin(...) (GPU_lgl_on ? GPU_lgl_Begin(__VA_ARGS__) : glBegin(__VA_ARGS__))
#define glEnd(...) (GPU_lgl_on ? GPU_lgl_End(__VA_ARGS__) : glEnd(__VA_ARGS__))
#define glBitmap(...) (GPU_lgl_on ? GPU_lgl_Bitmap(__VA_ARGS__) : glBitmap(__VA_ARGS__))
#define glDrawPixels(...) (GPU_lgl_on ? GPU_lgl_DrawPixels(__VA_ARGS__) : glDrawPixels(__VA_ARGS__))
#define glPixelTransferf(...) (GPU_lgl_on ? GPU_lgl_PixelTransferf(__VA_ARGS__) : glPixelTransferf(__VA_ARGS__))
#define glPixelTransferi(...) (GPU_lgl_on ? GPU_lgl_PixelTransferi(__VA_ARGS__) : glPixelTransferi(__VA_ARGS__))
#define glPixelZoom(...) (GPU_lgl_on ? GPU_lgl_PixelZoom(__VA_ARGS__) : glPixelZoom(__VA_ARGS__))
#define glMatrixMode(...) (GPU_lgl_on ? GPU_lgl_MatrixMode(__VA_ARGS__) : glMatrixMode(__VA_ARGS__))
#define glLoadIdentity(...) (GPU_lgl_on ? GPU_lgl_LoadIdentity(__VA_ARGS__) : glLoadIdentity(__VA_ARGS__))
#define glLoadMatrixf(...) (GPU_lgl_on ? GPU_lgl_LoadMatrixf((const GLfloat *)(__VA_ARGS__)) : glLoadMatrixf((const GLfloat *)(__VA_ARGS__)))
#define glLoadMatrixd(...) (GPU_lgl_on ? GPU_lgl_LoadMatrixd((const GLdouble *)(__VA_ARGS__)) : glLoadMatrixd((const GLdouble *)(__VA_ARGS__)))
#define glMultMatrixf(...) (GPU_lgl_on ? GPU_lgl_MultMatrixf((const GLfloat *)(__VA_ARGS__)) : glMultMatrixf((const GLfloat *)(__VA_ARGS__)))
#define glMultMatrixd(...) (GPU_lgl_on ? GPU_lgl_MultMatrixd((const GLdouble *)(__VA_ARGS__)) : glMultMatrixd((const GLdouble *)(__VA_ARGS__)))
#define glPushMatrix(...) (GPU_lgl_on ? GPU_lgl_PushMatrix(__VA_ARGS__) : glPushMatrix(__VA_ARGS__))
#define glPopMatrix(...) (GPU_lgl_on ? GPU_lgl_PopMatrix(__VA_ARGS__) : glPopMatrix(__VA_ARGS__))
#define glTranslatef(...) (GPU_lgl_on ? GPU_lgl_Translatef(__VA_ARGS__) : glTranslatef(__VA_ARGS__))
#define glTranslated(...) (GPU_lgl_on ? GPU_lgl_Translated(__VA_ARGS__) : glTranslated(__VA_ARGS__))
#define glRotatef(...) (GPU_lgl_on ? GPU_lgl_Rotatef(__VA_ARGS__) : glRotatef(__VA_ARGS__))
#define glRotated(...) (GPU_lgl_on ? GPU_lgl_Rotated(__VA_ARGS__) : glRotated(__VA_ARGS__))
#define glScalef(...) (GPU_lgl_on ? GPU_lgl_Scalef(__VA_ARGS__) : glScalef(__VA_ARGS__))
#define glScaled(...) (GPU_lgl_on ? GPU_lgl_Scaled(__VA_ARGS__) : glScaled(__VA_ARGS__))
#define glOrtho(...) (GPU_lgl_on ? GPU_lgl_Ortho(__VA_ARGS__) : glOrtho(__VA_ARGS__))
#define glFrustum(...) (GPU_lgl_on ? GPU_lgl_Frustum(__VA_ARGS__) : glFrustum(__VA_ARGS__))
#define glEnable(...) (GPU_lgl_on ? GPU_lgl_Enable(__VA_ARGS__) : glEnable(__VA_ARGS__))
#define glDisable(...) (GPU_lgl_on ? GPU_lgl_Disable(__VA_ARGS__) : glDisable(__VA_ARGS__))
#define glIsEnabled(...) (GPU_lgl_on ? GPU_lgl_IsEnabled(__VA_ARGS__) : glIsEnabled(__VA_ARGS__))
#define glGetIntegerv(...) (GPU_lgl_on ? GPU_lgl_GetIntegerv(__VA_ARGS__) : glGetIntegerv(__VA_ARGS__))
#define glGetFloatv(...) (GPU_lgl_on ? GPU_lgl_GetFloatv(__VA_ARGS__) : glGetFloatv(__VA_ARGS__))
#define glGetDoublev(...) (GPU_lgl_on ? GPU_lgl_GetDoublev(__VA_ARGS__) : glGetDoublev(__VA_ARGS__))
#define glGetBooleanv(...) (GPU_lgl_on ? GPU_lgl_GetBooleanv(__VA_ARGS__) : glGetBooleanv(__VA_ARGS__))
#define glGetString(...) (GPU_lgl_on ? GPU_lgl_GetString(__VA_ARGS__) : glGetString(__VA_ARGS__))
#define glTexImage1D(...) (GPU_lgl_on ? GPU_lgl_TexImage1D(__VA_ARGS__) : glTexImage1D(__VA_ARGS__))
#define glTexImage2D(...) (GPU_lgl_on ? GPU_lgl_TexImage2D(__VA_ARGS__) : glTexImage2D(__VA_ARGS__))
#define glTexSubImage1D(...) (GPU_lgl_on ? GPU_lgl_TexSubImage1D(__VA_ARGS__) : glTexSubImage1D(__VA_ARGS__))
#define glTexSubImage2D(...) (GPU_lgl_on ? GPU_lgl_TexSubImage2D(__VA_ARGS__) : glTexSubImage2D(__VA_ARGS__))
#define glCopyTexImage1D(...) (GPU_lgl_on ? GPU_lgl_CopyTexImage1D(__VA_ARGS__) : glCopyTexImage1D(__VA_ARGS__))
#define glCopyTexImage2D(...) (GPU_lgl_on ? GPU_lgl_CopyTexImage2D(__VA_ARGS__) : glCopyTexImage2D(__VA_ARGS__))
#define glGetTexImage(...) (GPU_lgl_on ? GPU_lgl_GetTexImage(__VA_ARGS__) : glGetTexImage(__VA_ARGS__))
#define glReadPixels(...) (GPU_lgl_on ? GPU_lgl_ReadPixels(__VA_ARGS__) : glReadPixels(__VA_ARGS__))
#define glTexParameteri(...) (GPU_lgl_on ? GPU_lgl_TexParameteri(__VA_ARGS__) : glTexParameteri(__VA_ARGS__))
#define glTexParameterf(...) (GPU_lgl_on ? GPU_lgl_TexParameterf(__VA_ARGS__) : glTexParameterf(__VA_ARGS__))
#define glTexParameteriv(...) (GPU_lgl_on ? GPU_lgl_TexParameteriv(__VA_ARGS__) : glTexParameteriv(__VA_ARGS__))
#define glTexParameterfv(...) (GPU_lgl_on ? GPU_lgl_TexParameterfv(__VA_ARGS__) : glTexParameterfv(__VA_ARGS__))
#define glTexEnvi(...) (GPU_lgl_on ? GPU_lgl_TexEnvi(__VA_ARGS__) : glTexEnvi(__VA_ARGS__))
#define glTexEnvf(...) (GPU_lgl_on ? GPU_lgl_TexEnvf(__VA_ARGS__) : glTexEnvf(__VA_ARGS__))
#define glTexEnviv(...) (GPU_lgl_on ? GPU_lgl_TexEnviv(__VA_ARGS__) : glTexEnviv(__VA_ARGS__))
#define glTexEnvfv(...) (GPU_lgl_on ? GPU_lgl_TexEnvfv(__VA_ARGS__) : glTexEnvfv(__VA_ARGS__))
#define glGetTexEnviv(...) (GPU_lgl_on ? GPU_lgl_GetTexEnviv(__VA_ARGS__) : glGetTexEnviv(__VA_ARGS__))
#define glGetTexEnvfv(...) (GPU_lgl_on ? GPU_lgl_GetTexEnvfv(__VA_ARGS__) : glGetTexEnvfv(__VA_ARGS__))
#define glTexGeni(...) (GPU_lgl_on ? GPU_lgl_TexGeni(__VA_ARGS__) : glTexGeni(__VA_ARGS__))
#define glTexGenf(...) (GPU_lgl_on ? GPU_lgl_TexGenf(__VA_ARGS__) : glTexGenf(__VA_ARGS__))
#define glTexGend(...) (GPU_lgl_on ? GPU_lgl_TexGend(__VA_ARGS__) : glTexGend(__VA_ARGS__))
#define glTexGeniv(...) (GPU_lgl_on ? GPU_lgl_TexGeniv(__VA_ARGS__) : glTexGeniv(__VA_ARGS__))
#define glTexGenfv(...) (GPU_lgl_on ? GPU_lgl_TexGenfv(__VA_ARGS__) : glTexGenfv(__VA_ARGS__))
#define glTexGendv(...) (GPU_lgl_on ? GPU_lgl_TexGendv(__VA_ARGS__) : glTexGendv(__VA_ARGS__))
#define glGetTexGeniv(...) (GPU_lgl_on ? GPU_lgl_GetTexGeniv(__VA_ARGS__) : glGetTexGeniv(__VA_ARGS__))
#define glGetTexGenfv(...) (GPU_lgl_on ? GPU_lgl_GetTexGenfv(__VA_ARGS__) : glGetTexGenfv(__VA_ARGS__))
#define glGetTexGendv(...) (GPU_lgl_on ? GPU_lgl_GetTexGendv(__VA_ARGS__) : glGetTexGendv(__VA_ARGS__))
#define glLightf(...) (GPU_lgl_on ? GPU_lgl_Lightf(__VA_ARGS__) : glLightf(__VA_ARGS__))
#define glLighti(...) (GPU_lgl_on ? GPU_lgl_Lighti(__VA_ARGS__) : glLighti(__VA_ARGS__))
#define glLightfv(...) (GPU_lgl_on ? GPU_lgl_Lightfv(__VA_ARGS__) : glLightfv(__VA_ARGS__))
#define glLightiv(...) (GPU_lgl_on ? GPU_lgl_Lightiv(__VA_ARGS__) : glLightiv(__VA_ARGS__))
#define glGetLightfv(...) (GPU_lgl_on ? GPU_lgl_GetLightfv(__VA_ARGS__) : glGetLightfv(__VA_ARGS__))
#define glGetLightiv(...) (GPU_lgl_on ? GPU_lgl_GetLightiv(__VA_ARGS__) : glGetLightiv(__VA_ARGS__))
#define glLightModelf(...) (GPU_lgl_on ? GPU_lgl_LightModelf(__VA_ARGS__) : glLightModelf(__VA_ARGS__))
#define glLightModeli(...) (GPU_lgl_on ? GPU_lgl_LightModeli(__VA_ARGS__) : glLightModeli(__VA_ARGS__))
#define glLightModelfv(...) (GPU_lgl_on ? GPU_lgl_LightModelfv(__VA_ARGS__) : glLightModelfv(__VA_ARGS__))
#define glLightModeliv(...) (GPU_lgl_on ? GPU_lgl_LightModeliv(__VA_ARGS__) : glLightModeliv(__VA_ARGS__))
#define glMaterialf(...) (GPU_lgl_on ? GPU_lgl_Materialf(__VA_ARGS__) : glMaterialf(__VA_ARGS__))
#define glMateriali(...) (GPU_lgl_on ? GPU_lgl_Materiali(__VA_ARGS__) : glMateriali(__VA_ARGS__))
#define glMaterialfv(...) (GPU_lgl_on ? GPU_lgl_Materialfv(__VA_ARGS__) : glMaterialfv(__VA_ARGS__))
#define glMaterialiv(...) (GPU_lgl_on ? GPU_lgl_Materialiv(__VA_ARGS__) : glMaterialiv(__VA_ARGS__))
#define glGetMaterialfv(...) (GPU_lgl_on ? GPU_lgl_GetMaterialfv(__VA_ARGS__) : glGetMaterialfv(__VA_ARGS__))
#define glGetMaterialiv(...) (GPU_lgl_on ? GPU_lgl_GetMaterialiv(__VA_ARGS__) : glGetMaterialiv(__VA_ARGS__))
#define glColorMaterial(...) (GPU_lgl_on ? GPU_lgl_ColorMaterial(__VA_ARGS__) : glColorMaterial(__VA_ARGS__))
#define glShadeModel(...) (GPU_lgl_on ? GPU_lgl_ShadeModel(__VA_ARGS__) : glShadeModel(__VA_ARGS__))
#define glAlphaFunc(...) (GPU_lgl_on ? GPU_lgl_AlphaFunc(__VA_ARGS__) : glAlphaFunc(__VA_ARGS__))
#define glLineStipple(...) (GPU_lgl_on ? GPU_lgl_LineStipple(__VA_ARGS__) : glLineStipple(__VA_ARGS__))
#define glPolygonStipple(...) (GPU_lgl_on ? GPU_lgl_PolygonStipple(__VA_ARGS__) : glPolygonStipple(__VA_ARGS__))
#define glGetPolygonStipple(...) (GPU_lgl_on ? GPU_lgl_GetPolygonStipple(__VA_ARGS__) : glGetPolygonStipple(__VA_ARGS__))
#define glClipPlane(...) (GPU_lgl_on ? GPU_lgl_ClipPlane(__VA_ARGS__) : glClipPlane(__VA_ARGS__))
#define glGetClipPlane(...) (GPU_lgl_on ? GPU_lgl_GetClipPlane(__VA_ARGS__) : glGetClipPlane(__VA_ARGS__))
#define glPushAttrib(...) (GPU_lgl_on ? GPU_lgl_PushAttrib(__VA_ARGS__) : glPushAttrib(__VA_ARGS__))
#define glPopAttrib(...) (GPU_lgl_on ? GPU_lgl_PopAttrib(__VA_ARGS__) : glPopAttrib(__VA_ARGS__))
#define glPushClientAttrib(...) (GPU_lgl_on ? GPU_lgl_PushClientAttrib(__VA_ARGS__) : glPushClientAttrib(__VA_ARGS__))
#define glPopClientAttrib(...) (GPU_lgl_on ? GPU_lgl_PopClientAttrib(__VA_ARGS__) : glPopClientAttrib(__VA_ARGS__))
#define glEnableClientState(...) (GPU_lgl_on ? GPU_lgl_EnableClientState(__VA_ARGS__) : glEnableClientState(__VA_ARGS__))
#define glDisableClientState(...) (GPU_lgl_on ? GPU_lgl_DisableClientState(__VA_ARGS__) : glDisableClientState(__VA_ARGS__))
#define glVertexPointer(...) (GPU_lgl_on ? GPU_lgl_VertexPointer(__VA_ARGS__) : glVertexPointer(__VA_ARGS__))
#define glNormalPointer(...) (GPU_lgl_on ? GPU_lgl_NormalPointer(__VA_ARGS__) : glNormalPointer(__VA_ARGS__))
#define glColorPointer(...) (GPU_lgl_on ? GPU_lgl_ColorPointer(__VA_ARGS__) : glColorPointer(__VA_ARGS__))
#define glTexCoordPointer(...) (GPU_lgl_on ? GPU_lgl_TexCoordPointer(__VA_ARGS__) : glTexCoordPointer(__VA_ARGS__))
#define glInterleavedArrays(...) (GPU_lgl_on ? GPU_lgl_InterleavedArrays(__VA_ARGS__) : glInterleavedArrays(__VA_ARGS__))
#define glArrayElement(...) (GPU_lgl_on ? GPU_lgl_ArrayElement(__VA_ARGS__) : glArrayElement(__VA_ARGS__))
#define glDrawArrays(...) (GPU_lgl_on ? GPU_lgl_DrawArrays(__VA_ARGS__) : glDrawArrays(__VA_ARGS__))
#define glDrawElements(...) (GPU_lgl_on ? GPU_lgl_DrawElements(__VA_ARGS__) : glDrawElements(__VA_ARGS__))
#define glNewList(...) (GPU_lgl_on ? GPU_lgl_NewList(__VA_ARGS__) : glNewList(__VA_ARGS__))
#define glEndList(...) (GPU_lgl_on ? GPU_lgl_EndList(__VA_ARGS__) : glEndList(__VA_ARGS__))
#define glCallList(...) (GPU_lgl_on ? GPU_lgl_CallList(__VA_ARGS__) : glCallList(__VA_ARGS__))
#define glCallLists(...) (GPU_lgl_on ? GPU_lgl_CallLists(__VA_ARGS__) : glCallLists(__VA_ARGS__))
#define glGenLists(...) (GPU_lgl_on ? GPU_lgl_GenLists(__VA_ARGS__) : glGenLists(__VA_ARGS__))
#define glDeleteLists(...) (GPU_lgl_on ? GPU_lgl_DeleteLists(__VA_ARGS__) : glDeleteLists(__VA_ARGS__))
#define glIsList(...) (GPU_lgl_on ? GPU_lgl_IsList(__VA_ARGS__) : glIsList(__VA_ARGS__))
#define glListBase(...) (GPU_lgl_on ? GPU_lgl_ListBase(__VA_ARGS__) : glListBase(__VA_ARGS__))
#define glMap1f(...) (GPU_lgl_on ? GPU_lgl_Map1f(__VA_ARGS__) : glMap1f(__VA_ARGS__))
#define glMap1d(...) (GPU_lgl_on ? GPU_lgl_Map1d(__VA_ARGS__) : glMap1d(__VA_ARGS__))
#define glMapGrid1f(...) (GPU_lgl_on ? GPU_lgl_MapGrid1f(__VA_ARGS__) : glMapGrid1f(__VA_ARGS__))
#define glMapGrid1d(...) (GPU_lgl_on ? GPU_lgl_MapGrid1d(__VA_ARGS__) : glMapGrid1d(__VA_ARGS__))
#define glEvalMesh1(...) (GPU_lgl_on ? GPU_lgl_EvalMesh1(__VA_ARGS__) : glEvalMesh1(__VA_ARGS__))
#define glEvalPoint1(...) (GPU_lgl_on ? GPU_lgl_EvalPoint1(__VA_ARGS__) : glEvalPoint1(__VA_ARGS__))
#define glFogf(...) (GPU_lgl_on ? GPU_lgl_Fogf(__VA_ARGS__) : glFogf(__VA_ARGS__))
#define glFogi(...) (GPU_lgl_on ? GPU_lgl_Fogi(__VA_ARGS__) : glFogi(__VA_ARGS__))
#define glFogfv(...) (GPU_lgl_on ? GPU_lgl_Fogfv(__VA_ARGS__) : glFogfv(__VA_ARGS__))
#define glFogiv(...) (GPU_lgl_on ? GPU_lgl_Fogiv(__VA_ARGS__) : glFogiv(__VA_ARGS__))
#define glPolygonMode(...) (GPU_lgl_on ? GPU_lgl_PolygonMode(__VA_ARGS__) : glPolygonMode(__VA_ARGS__))
#define glRenderMode(...) (GPU_lgl_on ? GPU_lgl_RenderMode(__VA_ARGS__) : glRenderMode(__VA_ARGS__))
#define glSelectBuffer(...) (GPU_lgl_on ? GPU_lgl_SelectBuffer(__VA_ARGS__) : glSelectBuffer(__VA_ARGS__))
#define glInitNames(...) (GPU_lgl_on ? GPU_lgl_InitNames(__VA_ARGS__) : glInitNames(__VA_ARGS__))
#define glLoadName(...) (GPU_lgl_on ? GPU_lgl_LoadName(__VA_ARGS__) : glLoadName(__VA_ARGS__))
#define glPushName(...) (GPU_lgl_on ? GPU_lgl_PushName(__VA_ARGS__) : glPushName(__VA_ARGS__))
#define glPopName(...) (GPU_lgl_on ? GPU_lgl_PopName(__VA_ARGS__) : glPopName(__VA_ARGS__))
#define glHint(...) (GPU_lgl_on ? GPU_lgl_Hint(__VA_ARGS__) : glHint(__VA_ARGS__))
#define glBindTexture(...) (GPU_lgl_on ? GPU_lgl_BindTexture(__VA_ARGS__) : glBindTexture(__VA_ARGS__))
#define glDeleteTextures(...) (GPU_lgl_on ? GPU_lgl_DeleteTextures(__VA_ARGS__) : glDeleteTextures(__VA_ARGS__))
#define glAreTexturesResident(...) (GPU_lgl_on ? GPU_lgl_AreTexturesResident(__VA_ARGS__) : glAreTexturesResident(__VA_ARGS__))
#define glPrioritizeTextures(...) (GPU_lgl_on ? GPU_lgl_PrioritizeTextures(__VA_ARGS__) : glPrioritizeTextures(__VA_ARGS__))
#define glVertex2s(...) (GPU_lgl_on ? GPU_lgl_Vertex2s(__VA_ARGS__) : glVertex2s(__VA_ARGS__))
#define glVertex2sv(...) (GPU_lgl_on ? GPU_lgl_Vertex2sv(__VA_ARGS__) : glVertex2sv(__VA_ARGS__))
#define glVertex2i(...) (GPU_lgl_on ? GPU_lgl_Vertex2i(__VA_ARGS__) : glVertex2i(__VA_ARGS__))
#define glVertex2iv(...) (GPU_lgl_on ? GPU_lgl_Vertex2iv(__VA_ARGS__) : glVertex2iv(__VA_ARGS__))
#define glVertex2f(...) (GPU_lgl_on ? GPU_lgl_Vertex2f(__VA_ARGS__) : glVertex2f(__VA_ARGS__))
#define glVertex2fv(...) (GPU_lgl_on ? GPU_lgl_Vertex2fv(__VA_ARGS__) : glVertex2fv(__VA_ARGS__))
#define glVertex2d(...) (GPU_lgl_on ? GPU_lgl_Vertex2d(__VA_ARGS__) : glVertex2d(__VA_ARGS__))
#define glVertex2dv(...) (GPU_lgl_on ? GPU_lgl_Vertex2dv(__VA_ARGS__) : glVertex2dv(__VA_ARGS__))
#define glVertex3s(...) (GPU_lgl_on ? GPU_lgl_Vertex3s(__VA_ARGS__) : glVertex3s(__VA_ARGS__))
#define glVertex3sv(...) (GPU_lgl_on ? GPU_lgl_Vertex3sv(__VA_ARGS__) : glVertex3sv(__VA_ARGS__))
#define glVertex3i(...) (GPU_lgl_on ? GPU_lgl_Vertex3i(__VA_ARGS__) : glVertex3i(__VA_ARGS__))
#define glVertex3iv(...) (GPU_lgl_on ? GPU_lgl_Vertex3iv(__VA_ARGS__) : glVertex3iv(__VA_ARGS__))
#define glVertex3f(...) (GPU_lgl_on ? GPU_lgl_Vertex3f(__VA_ARGS__) : glVertex3f(__VA_ARGS__))
#define glVertex3fv(...) (GPU_lgl_on ? GPU_lgl_Vertex3fv(__VA_ARGS__) : glVertex3fv(__VA_ARGS__))
#define glVertex3d(...) (GPU_lgl_on ? GPU_lgl_Vertex3d(__VA_ARGS__) : glVertex3d(__VA_ARGS__))
#define glVertex3dv(...) (GPU_lgl_on ? GPU_lgl_Vertex3dv(__VA_ARGS__) : glVertex3dv(__VA_ARGS__))
#define glVertex4s(...) (GPU_lgl_on ? GPU_lgl_Vertex4s(__VA_ARGS__) : glVertex4s(__VA_ARGS__))
#define glVertex4sv(...) (GPU_lgl_on ? GPU_lgl_Vertex4sv(__VA_ARGS__) : glVertex4sv(__VA_ARGS__))
#define glVertex4i(...) (GPU_lgl_on ? GPU_lgl_Vertex4i(__VA_ARGS__) : glVertex4i(__VA_ARGS__))
#define glVertex4iv(...) (GPU_lgl_on ? GPU_lgl_Vertex4iv(__VA_ARGS__) : glVertex4iv(__VA_ARGS__))
#define glVertex4f(...) (GPU_lgl_on ? GPU_lgl_Vertex4f(__VA_ARGS__) : glVertex4f(__VA_ARGS__))
#define glVertex4fv(...) (GPU_lgl_on ? GPU_lgl_Vertex4fv(__VA_ARGS__) : glVertex4fv(__VA_ARGS__))
#define glVertex4d(...) (GPU_lgl_on ? GPU_lgl_Vertex4d(__VA_ARGS__) : glVertex4d(__VA_ARGS__))
#define glVertex4dv(...) (GPU_lgl_on ? GPU_lgl_Vertex4dv(__VA_ARGS__) : glVertex4dv(__VA_ARGS__))
#define glColor3b(...) (GPU_lgl_on ? GPU_lgl_Color3b(__VA_ARGS__) : glColor3b(__VA_ARGS__))
#define glColor3bv(...) (GPU_lgl_on ? GPU_lgl_Color3bv(__VA_ARGS__) : glColor3bv(__VA_ARGS__))
#define glColor3s(...) (GPU_lgl_on ? GPU_lgl_Color3s(__VA_ARGS__) : glColor3s(__VA_ARGS__))
#define glColor3sv(...) (GPU_lgl_on ? GPU_lgl_Color3sv(__VA_ARGS__) : glColor3sv(__VA_ARGS__))
#define glColor3i(...) (GPU_lgl_on ? GPU_lgl_Color3i(__VA_ARGS__) : glColor3i(__VA_ARGS__))
#define glColor3iv(...) (GPU_lgl_on ? GPU_lgl_Color3iv(__VA_ARGS__) : glColor3iv(__VA_ARGS__))
#define glColor3f(...) (GPU_lgl_on ? GPU_lgl_Color3f(__VA_ARGS__) : glColor3f(__VA_ARGS__))
#define glColor3fv(...) (GPU_lgl_on ? GPU_lgl_Color3fv(__VA_ARGS__) : glColor3fv(__VA_ARGS__))
#define glColor3d(...) (GPU_lgl_on ? GPU_lgl_Color3d(__VA_ARGS__) : glColor3d(__VA_ARGS__))
#define glColor3dv(...) (GPU_lgl_on ? GPU_lgl_Color3dv(__VA_ARGS__) : glColor3dv(__VA_ARGS__))
#define glColor3ub(...) (GPU_lgl_on ? GPU_lgl_Color3ub(__VA_ARGS__) : glColor3ub(__VA_ARGS__))
#define glColor3ubv(...) (GPU_lgl_on ? GPU_lgl_Color3ubv(__VA_ARGS__) : glColor3ubv(__VA_ARGS__))
#define glColor3us(...) (GPU_lgl_on ? GPU_lgl_Color3us(__VA_ARGS__) : glColor3us(__VA_ARGS__))
#define glColor3usv(...) (GPU_lgl_on ? GPU_lgl_Color3usv(__VA_ARGS__) : glColor3usv(__VA_ARGS__))
#define glColor3ui(...) (GPU_lgl_on ? GPU_lgl_Color3ui(__VA_ARGS__) : glColor3ui(__VA_ARGS__))
#define glColor3uiv(...) (GPU_lgl_on ? GPU_lgl_Color3uiv(__VA_ARGS__) : glColor3uiv(__VA_ARGS__))
#define glColor4b(...) (GPU_lgl_on ? GPU_lgl_Color4b(__VA_ARGS__) : glColor4b(__VA_ARGS__))
#define glColor4bv(...) (GPU_lgl_on ? GPU_lgl_Color4bv(__VA_ARGS__) : glColor4bv(__VA_ARGS__))
#define glColor4s(...) (GPU_lgl_on ? GPU_lgl_Color4s(__VA_ARGS__) : glColor4s(__VA_ARGS__))
#define glColor4sv(...) (GPU_lgl_on ? GPU_lgl_Color4sv(__VA_ARGS__) : glColor4sv(__VA_ARGS__))
#define glColor4i(...) (GPU_lgl_on ? GPU_lgl_Color4i(__VA_ARGS__) : glColor4i(__VA_ARGS__))
#define glColor4iv(...) (GPU_lgl_on ? GPU_lgl_Color4iv(__VA_ARGS__) : glColor4iv(__VA_ARGS__))
#define glColor4f(...) (GPU_lgl_on ? GPU_lgl_Color4f(__VA_ARGS__) : glColor4f(__VA_ARGS__))
#define glColor4fv(...) (GPU_lgl_on ? GPU_lgl_Color4fv(__VA_ARGS__) : glColor4fv(__VA_ARGS__))
#define glColor4d(...) (GPU_lgl_on ? GPU_lgl_Color4d(__VA_ARGS__) : glColor4d(__VA_ARGS__))
#define glColor4dv(...) (GPU_lgl_on ? GPU_lgl_Color4dv(__VA_ARGS__) : glColor4dv(__VA_ARGS__))
#define glColor4ub(...) (GPU_lgl_on ? GPU_lgl_Color4ub(__VA_ARGS__) : glColor4ub(__VA_ARGS__))
#define glColor4ubv(...) (GPU_lgl_on ? GPU_lgl_Color4ubv(__VA_ARGS__) : glColor4ubv(__VA_ARGS__))
#define glColor4us(...) (GPU_lgl_on ? GPU_lgl_Color4us(__VA_ARGS__) : glColor4us(__VA_ARGS__))
#define glColor4usv(...) (GPU_lgl_on ? GPU_lgl_Color4usv(__VA_ARGS__) : glColor4usv(__VA_ARGS__))
#define glColor4ui(...) (GPU_lgl_on ? GPU_lgl_Color4ui(__VA_ARGS__) : glColor4ui(__VA_ARGS__))
#define glColor4uiv(...) (GPU_lgl_on ? GPU_lgl_Color4uiv(__VA_ARGS__) : glColor4uiv(__VA_ARGS__))
#define glNormal3b(...) (GPU_lgl_on ? GPU_lgl_Normal3b(__VA_ARGS__) : glNormal3b(__VA_ARGS__))
#define glNormal3bv(...) (GPU_lgl_on ? GPU_lgl_Normal3bv(__VA_ARGS__) : glNormal3bv(__VA_ARGS__))
#define glNormal3s(...) (GPU_lgl_on ? GPU_lgl_Normal3s(__VA_ARGS__) : glNormal3s(__VA_ARGS__))
#define glNormal3sv(...) (GPU_lgl_on ? GPU_lgl_Normal3sv(__VA_ARGS__) : glNormal3sv(__VA_ARGS__))
#define glNormal3i(...) (GPU_lgl_on ? GPU_lgl_Normal3i(__VA_ARGS__) : glNormal3i(__VA_ARGS__))
#define glNormal3iv(...) (GPU_lgl_on ? GPU_lgl_Normal3iv(__VA_ARGS__) : glNormal3iv(__VA_ARGS__))
#define glNormal3f(...) (GPU_lgl_on ? GPU_lgl_Normal3f(__VA_ARGS__) : glNormal3f(__VA_ARGS__))
#define glNormal3fv(...) (GPU_lgl_on ? GPU_lgl_Normal3fv(__VA_ARGS__) : glNormal3fv(__VA_ARGS__))
#define glNormal3d(...) (GPU_lgl_on ? GPU_lgl_Normal3d(__VA_ARGS__) : glNormal3d(__VA_ARGS__))
#define glNormal3dv(...) (GPU_lgl_on ? GPU_lgl_Normal3dv(__VA_ARGS__) : glNormal3dv(__VA_ARGS__))
#define glTexCoord1s(...) (GPU_lgl_on ? GPU_lgl_TexCoord1s(__VA_ARGS__) : glTexCoord1s(__VA_ARGS__))
#define glTexCoord1sv(...) (GPU_lgl_on ? GPU_lgl_TexCoord1sv(__VA_ARGS__) : glTexCoord1sv(__VA_ARGS__))
#define glTexCoord1i(...) (GPU_lgl_on ? GPU_lgl_TexCoord1i(__VA_ARGS__) : glTexCoord1i(__VA_ARGS__))
#define glTexCoord1iv(...) (GPU_lgl_on ? GPU_lgl_TexCoord1iv(__VA_ARGS__) : glTexCoord1iv(__VA_ARGS__))
#define glTexCoord1f(...) (GPU_lgl_on ? GPU_lgl_TexCoord1f(__VA_ARGS__) : glTexCoord1f(__VA_ARGS__))
#define glTexCoord1fv(...) (GPU_lgl_on ? GPU_lgl_TexCoord1fv(__VA_ARGS__) : glTexCoord1fv(__VA_ARGS__))
#define glTexCoord1d(...) (GPU_lgl_on ? GPU_lgl_TexCoord1d(__VA_ARGS__) : glTexCoord1d(__VA_ARGS__))
#define glTexCoord1dv(...) (GPU_lgl_on ? GPU_lgl_TexCoord1dv(__VA_ARGS__) : glTexCoord1dv(__VA_ARGS__))
#define glTexCoord2s(...) (GPU_lgl_on ? GPU_lgl_TexCoord2s(__VA_ARGS__) : glTexCoord2s(__VA_ARGS__))
#define glTexCoord2sv(...) (GPU_lgl_on ? GPU_lgl_TexCoord2sv(__VA_ARGS__) : glTexCoord2sv(__VA_ARGS__))
#define glTexCoord2i(...) (GPU_lgl_on ? GPU_lgl_TexCoord2i(__VA_ARGS__) : glTexCoord2i(__VA_ARGS__))
#define glTexCoord2iv(...) (GPU_lgl_on ? GPU_lgl_TexCoord2iv(__VA_ARGS__) : glTexCoord2iv(__VA_ARGS__))
#define glTexCoord2f(...) (GPU_lgl_on ? GPU_lgl_TexCoord2f(__VA_ARGS__) : glTexCoord2f(__VA_ARGS__))
#define glTexCoord2fv(...) (GPU_lgl_on ? GPU_lgl_TexCoord2fv(__VA_ARGS__) : glTexCoord2fv(__VA_ARGS__))
#define glTexCoord2d(...) (GPU_lgl_on ? GPU_lgl_TexCoord2d(__VA_ARGS__) : glTexCoord2d(__VA_ARGS__))
#define glTexCoord2dv(...) (GPU_lgl_on ? GPU_lgl_TexCoord2dv(__VA_ARGS__) : glTexCoord2dv(__VA_ARGS__))
#define glTexCoord3s(...) (GPU_lgl_on ? GPU_lgl_TexCoord3s(__VA_ARGS__) : glTexCoord3s(__VA_ARGS__))
#define glTexCoord3sv(...) (GPU_lgl_on ? GPU_lgl_TexCoord3sv(__VA_ARGS__) : glTexCoord3sv(__VA_ARGS__))
#define glTexCoord3i(...) (GPU_lgl_on ? GPU_lgl_TexCoord3i(__VA_ARGS__) : glTexCoord3i(__VA_ARGS__))
#define glTexCoord3iv(...) (GPU_lgl_on ? GPU_lgl_TexCoord3iv(__VA_ARGS__) : glTexCoord3iv(__VA_ARGS__))
#define glTexCoord3f(...) (GPU_lgl_on ? GPU_lgl_TexCoord3f(__VA_ARGS__) : glTexCoord3f(__VA_ARGS__))
#define glTexCoord3fv(...) (GPU_lgl_on ? GPU_lgl_TexCoord3fv(__VA_ARGS__) : glTexCoord3fv(__VA_ARGS__))
#define glTexCoord3d(...) (GPU_lgl_on ? GPU_lgl_TexCoord3d(__VA_ARGS__) : glTexCoord3d(__VA_ARGS__))
#define glTexCoord3dv(...) (GPU_lgl_on ? GPU_lgl_TexCoord3dv(__VA_ARGS__) : glTexCoord3dv(__VA_ARGS__))
#define glTexCoord4s(...) (GPU_lgl_on ? GPU_lgl_TexCoord4s(__VA_ARGS__) : glTexCoord4s(__VA_ARGS__))
#define glTexCoord4sv(...) (GPU_lgl_on ? GPU_lgl_TexCoord4sv(__VA_ARGS__) : glTexCoord4sv(__VA_ARGS__))
#define glTexCoord4i(...) (GPU_lgl_on ? GPU_lgl_TexCoord4i(__VA_ARGS__) : glTexCoord4i(__VA_ARGS__))
#define glTexCoord4iv(...) (GPU_lgl_on ? GPU_lgl_TexCoord4iv(__VA_ARGS__) : glTexCoord4iv(__VA_ARGS__))
#define glTexCoord4f(...) (GPU_lgl_on ? GPU_lgl_TexCoord4f(__VA_ARGS__) : glTexCoord4f(__VA_ARGS__))
#define glTexCoord4fv(...) (GPU_lgl_on ? GPU_lgl_TexCoord4fv(__VA_ARGS__) : glTexCoord4fv(__VA_ARGS__))
#define glTexCoord4d(...) (GPU_lgl_on ? GPU_lgl_TexCoord4d(__VA_ARGS__) : glTexCoord4d(__VA_ARGS__))
#define glTexCoord4dv(...) (GPU_lgl_on ? GPU_lgl_TexCoord4dv(__VA_ARGS__) : glTexCoord4dv(__VA_ARGS__))
#define glRasterPos2s(...) (GPU_lgl_on ? GPU_lgl_RasterPos2s(__VA_ARGS__) : glRasterPos2s(__VA_ARGS__))
#define glRasterPos2sv(...) (GPU_lgl_on ? GPU_lgl_RasterPos2sv(__VA_ARGS__) : glRasterPos2sv(__VA_ARGS__))
#define glRasterPos2i(...) (GPU_lgl_on ? GPU_lgl_RasterPos2i(__VA_ARGS__) : glRasterPos2i(__VA_ARGS__))
#define glRasterPos2iv(...) (GPU_lgl_on ? GPU_lgl_RasterPos2iv(__VA_ARGS__) : glRasterPos2iv(__VA_ARGS__))
#define glRasterPos2f(...) (GPU_lgl_on ? GPU_lgl_RasterPos2f(__VA_ARGS__) : glRasterPos2f(__VA_ARGS__))
#define glRasterPos2fv(...) (GPU_lgl_on ? GPU_lgl_RasterPos2fv(__VA_ARGS__) : glRasterPos2fv(__VA_ARGS__))
#define glRasterPos2d(...) (GPU_lgl_on ? GPU_lgl_RasterPos2d(__VA_ARGS__) : glRasterPos2d(__VA_ARGS__))
#define glRasterPos2dv(...) (GPU_lgl_on ? GPU_lgl_RasterPos2dv(__VA_ARGS__) : glRasterPos2dv(__VA_ARGS__))
#define glRasterPos3s(...) (GPU_lgl_on ? GPU_lgl_RasterPos3s(__VA_ARGS__) : glRasterPos3s(__VA_ARGS__))
#define glRasterPos3sv(...) (GPU_lgl_on ? GPU_lgl_RasterPos3sv(__VA_ARGS__) : glRasterPos3sv(__VA_ARGS__))
#define glRasterPos3i(...) (GPU_lgl_on ? GPU_lgl_RasterPos3i(__VA_ARGS__) : glRasterPos3i(__VA_ARGS__))
#define glRasterPos3iv(...) (GPU_lgl_on ? GPU_lgl_RasterPos3iv(__VA_ARGS__) : glRasterPos3iv(__VA_ARGS__))
#define glRasterPos3f(...) (GPU_lgl_on ? GPU_lgl_RasterPos3f(__VA_ARGS__) : glRasterPos3f(__VA_ARGS__))
#define glRasterPos3fv(...) (GPU_lgl_on ? GPU_lgl_RasterPos3fv(__VA_ARGS__) : glRasterPos3fv(__VA_ARGS__))
#define glRasterPos3d(...) (GPU_lgl_on ? GPU_lgl_RasterPos3d(__VA_ARGS__) : glRasterPos3d(__VA_ARGS__))
#define glRasterPos3dv(...) (GPU_lgl_on ? GPU_lgl_RasterPos3dv(__VA_ARGS__) : glRasterPos3dv(__VA_ARGS__))
#define glRasterPos4s(...) (GPU_lgl_on ? GPU_lgl_RasterPos4s(__VA_ARGS__) : glRasterPos4s(__VA_ARGS__))
#define glRasterPos4sv(...) (GPU_lgl_on ? GPU_lgl_RasterPos4sv(__VA_ARGS__) : glRasterPos4sv(__VA_ARGS__))
#define glRasterPos4i(...) (GPU_lgl_on ? GPU_lgl_RasterPos4i(__VA_ARGS__) : glRasterPos4i(__VA_ARGS__))
#define glRasterPos4iv(...) (GPU_lgl_on ? GPU_lgl_RasterPos4iv(__VA_ARGS__) : glRasterPos4iv(__VA_ARGS__))
#define glRasterPos4f(...) (GPU_lgl_on ? GPU_lgl_RasterPos4f(__VA_ARGS__) : glRasterPos4f(__VA_ARGS__))
#define glRasterPos4fv(...) (GPU_lgl_on ? GPU_lgl_RasterPos4fv(__VA_ARGS__) : glRasterPos4fv(__VA_ARGS__))
#define glRasterPos4d(...) (GPU_lgl_on ? GPU_lgl_RasterPos4d(__VA_ARGS__) : glRasterPos4d(__VA_ARGS__))
#define glRasterPos4dv(...) (GPU_lgl_on ? GPU_lgl_RasterPos4dv(__VA_ARGS__) : glRasterPos4dv(__VA_ARGS__))
#define glRects(...) (GPU_lgl_on ? GPU_lgl_Rects(__VA_ARGS__) : glRects(__VA_ARGS__))
#define glRectsv(...) (GPU_lgl_on ? GPU_lgl_Rectsv(__VA_ARGS__) : glRectsv(__VA_ARGS__))
#define glRecti(...) (GPU_lgl_on ? GPU_lgl_Recti(__VA_ARGS__) : glRecti(__VA_ARGS__))
#define glRectiv(...) (GPU_lgl_on ? GPU_lgl_Rectiv(__VA_ARGS__) : glRectiv(__VA_ARGS__))
#define glRectf(...) (GPU_lgl_on ? GPU_lgl_Rectf(__VA_ARGS__) : glRectf(__VA_ARGS__))
#define glRectfv(...) (GPU_lgl_on ? GPU_lgl_Rectfv(__VA_ARGS__) : glRectfv(__VA_ARGS__))
#define glRectd(...) (GPU_lgl_on ? GPU_lgl_Rectd(__VA_ARGS__) : glRectd(__VA_ARGS__))
#define glRectdv(...) (GPU_lgl_on ? GPU_lgl_Rectdv(__VA_ARGS__) : glRectdv(__VA_ARGS__))
#define glEvalCoord1f(...) (GPU_lgl_on ? GPU_lgl_EvalCoord1f(__VA_ARGS__) : glEvalCoord1f(__VA_ARGS__))
#define glEvalCoord1fv(...) (GPU_lgl_on ? GPU_lgl_EvalCoord1fv(__VA_ARGS__) : glEvalCoord1fv(__VA_ARGS__))
#define glEvalCoord1d(...) (GPU_lgl_on ? GPU_lgl_EvalCoord1d(__VA_ARGS__) : glEvalCoord1d(__VA_ARGS__))
#define glEvalCoord1dv(...) (GPU_lgl_on ? GPU_lgl_EvalCoord1dv(__VA_ARGS__) : glEvalCoord1dv(__VA_ARGS__))

#undef glUseProgram
#define glUseProgram GPU_lgl_UseProgram
#undef glBindVertexArray
#define glBindVertexArray GPU_lgl_BindVertexArray
#undef glDeleteVertexArrays
#define glDeleteVertexArrays GPU_lgl_DeleteVertexArrays
#undef glBindBuffer
#define glBindBuffer GPU_lgl_BindBuffer
#undef glDeleteBuffers
#define glDeleteBuffers GPU_lgl_DeleteBuffers
#undef glVertexAttribPointer
#define glVertexAttribPointer GPU_lgl_VertexAttribPointer
#undef glVertexAttribIPointer
#define glVertexAttribIPointer GPU_lgl_VertexAttribIPointer
#undef glEnableVertexAttribArray
#define glEnableVertexAttribArray GPU_lgl_EnableVertexAttribArray
#undef glDisableVertexAttribArray
#define glDisableVertexAttribArray GPU_lgl_DisableVertexAttribArray
#undef glVertexAttribDivisor
#define glVertexAttribDivisor GPU_lgl_VertexAttribDivisor
#undef glActiveTexture
#define glActiveTexture GPU_lgl_ActiveTexture
#undef glClientActiveTexture
#define glClientActiveTexture GPU_lgl_ClientActiveTexture
#undef glDrawRangeElements
#define glDrawRangeElements GPU_lgl_DrawRangeElements
#undef glDrawArraysInstanced
#define glDrawArraysInstanced GPU_lgl_DrawArraysInstanced
#undef glDrawElementsInstanced
#define glDrawElementsInstanced GPU_lgl_DrawElementsInstanced
#undef glShaderSource
#define glShaderSource GPU_lgl_ShaderSource
#undef glLinkProgram
#define glLinkProgram GPU_lgl_LinkProgram
#undef glDeleteProgram
#define glDeleteProgram GPU_lgl_DeleteProgram
#undef glGetUniformLocation
#define glGetUniformLocation GPU_lgl_GetUniformLocation
#undef glGetAttribLocation
#define glGetAttribLocation GPU_lgl_GetAttribLocation
#undef glBindAttribLocation
#define glBindAttribLocation GPU_lgl_BindAttribLocation
#undef glProgramParameteriEXT
#define glProgramParameteriEXT GPU_lgl_ProgramParameteriEXT
#undef glLoadTransposeMatrixf
#define glLoadTransposeMatrixf GPU_lgl_LoadTransposeMatrixf
#undef glLoadTransposeMatrixd
#define glLoadTransposeMatrixd GPU_lgl_LoadTransposeMatrixd
#undef glMultTransposeMatrixf
#define glMultTransposeMatrixf GPU_lgl_MultTransposeMatrixf
#undef glMultTransposeMatrixd
#define glMultTransposeMatrixd GPU_lgl_MultTransposeMatrixd
#undef glMultiTexCoord1s
#define glMultiTexCoord1s GPU_lgl_MultiTexCoord1s
#undef glMultiTexCoord1sv
#define glMultiTexCoord1sv GPU_lgl_MultiTexCoord1sv
#undef glMultiTexCoord1i
#define glMultiTexCoord1i GPU_lgl_MultiTexCoord1i
#undef glMultiTexCoord1iv
#define glMultiTexCoord1iv GPU_lgl_MultiTexCoord1iv
#undef glMultiTexCoord1f
#define glMultiTexCoord1f GPU_lgl_MultiTexCoord1f
#undef glMultiTexCoord1fv
#define glMultiTexCoord1fv GPU_lgl_MultiTexCoord1fv
#undef glMultiTexCoord1d
#define glMultiTexCoord1d GPU_lgl_MultiTexCoord1d
#undef glMultiTexCoord1dv
#define glMultiTexCoord1dv GPU_lgl_MultiTexCoord1dv
#undef glMultiTexCoord2s
#define glMultiTexCoord2s GPU_lgl_MultiTexCoord2s
#undef glMultiTexCoord2sv
#define glMultiTexCoord2sv GPU_lgl_MultiTexCoord2sv
#undef glMultiTexCoord2i
#define glMultiTexCoord2i GPU_lgl_MultiTexCoord2i
#undef glMultiTexCoord2iv
#define glMultiTexCoord2iv GPU_lgl_MultiTexCoord2iv
#undef glMultiTexCoord2f
#define glMultiTexCoord2f GPU_lgl_MultiTexCoord2f
#undef glMultiTexCoord2fv
#define glMultiTexCoord2fv GPU_lgl_MultiTexCoord2fv
#undef glMultiTexCoord2d
#define glMultiTexCoord2d GPU_lgl_MultiTexCoord2d
#undef glMultiTexCoord2dv
#define glMultiTexCoord2dv GPU_lgl_MultiTexCoord2dv
#undef glMultiTexCoord3s
#define glMultiTexCoord3s GPU_lgl_MultiTexCoord3s
#undef glMultiTexCoord3sv
#define glMultiTexCoord3sv GPU_lgl_MultiTexCoord3sv
#undef glMultiTexCoord3i
#define glMultiTexCoord3i GPU_lgl_MultiTexCoord3i
#undef glMultiTexCoord3iv
#define glMultiTexCoord3iv GPU_lgl_MultiTexCoord3iv
#undef glMultiTexCoord3f
#define glMultiTexCoord3f GPU_lgl_MultiTexCoord3f
#undef glMultiTexCoord3fv
#define glMultiTexCoord3fv GPU_lgl_MultiTexCoord3fv
#undef glMultiTexCoord3d
#define glMultiTexCoord3d GPU_lgl_MultiTexCoord3d
#undef glMultiTexCoord3dv
#define glMultiTexCoord3dv GPU_lgl_MultiTexCoord3dv
#undef glMultiTexCoord4s
#define glMultiTexCoord4s GPU_lgl_MultiTexCoord4s
#undef glMultiTexCoord4sv
#define glMultiTexCoord4sv GPU_lgl_MultiTexCoord4sv
#undef glMultiTexCoord4i
#define glMultiTexCoord4i GPU_lgl_MultiTexCoord4i
#undef glMultiTexCoord4iv
#define glMultiTexCoord4iv GPU_lgl_MultiTexCoord4iv
#undef glMultiTexCoord4f
#define glMultiTexCoord4f GPU_lgl_MultiTexCoord4f
#undef glMultiTexCoord4fv
#define glMultiTexCoord4fv GPU_lgl_MultiTexCoord4fv
#undef glMultiTexCoord4d
#define glMultiTexCoord4d GPU_lgl_MultiTexCoord4d
#undef glMultiTexCoord4dv
#define glMultiTexCoord4dv GPU_lgl_MultiTexCoord4dv
#undef glVertexAttrib1s
#define glVertexAttrib1s GPU_lgl_VertexAttrib1s
#undef glVertexAttrib1sv
#define glVertexAttrib1sv GPU_lgl_VertexAttrib1sv
#undef glVertexAttrib1f
#define glVertexAttrib1f GPU_lgl_VertexAttrib1f
#undef glVertexAttrib1fv
#define glVertexAttrib1fv GPU_lgl_VertexAttrib1fv
#undef glVertexAttrib1d
#define glVertexAttrib1d GPU_lgl_VertexAttrib1d
#undef glVertexAttrib1dv
#define glVertexAttrib1dv GPU_lgl_VertexAttrib1dv
#undef glVertexAttrib2s
#define glVertexAttrib2s GPU_lgl_VertexAttrib2s
#undef glVertexAttrib2sv
#define glVertexAttrib2sv GPU_lgl_VertexAttrib2sv
#undef glVertexAttrib2f
#define glVertexAttrib2f GPU_lgl_VertexAttrib2f
#undef glVertexAttrib2fv
#define glVertexAttrib2fv GPU_lgl_VertexAttrib2fv
#undef glVertexAttrib2d
#define glVertexAttrib2d GPU_lgl_VertexAttrib2d
#undef glVertexAttrib2dv
#define glVertexAttrib2dv GPU_lgl_VertexAttrib2dv
#undef glVertexAttrib3s
#define glVertexAttrib3s GPU_lgl_VertexAttrib3s
#undef glVertexAttrib3sv
#define glVertexAttrib3sv GPU_lgl_VertexAttrib3sv
#undef glVertexAttrib3f
#define glVertexAttrib3f GPU_lgl_VertexAttrib3f
#undef glVertexAttrib3fv
#define glVertexAttrib3fv GPU_lgl_VertexAttrib3fv
#undef glVertexAttrib3d
#define glVertexAttrib3d GPU_lgl_VertexAttrib3d
#undef glVertexAttrib3dv
#define glVertexAttrib3dv GPU_lgl_VertexAttrib3dv
#undef glVertexAttrib4s
#define glVertexAttrib4s GPU_lgl_VertexAttrib4s
#undef glVertexAttrib4sv
#define glVertexAttrib4sv GPU_lgl_VertexAttrib4sv
#undef glVertexAttrib4f
#define glVertexAttrib4f GPU_lgl_VertexAttrib4f
#undef glVertexAttrib4fv
#define glVertexAttrib4fv GPU_lgl_VertexAttrib4fv
#undef glVertexAttrib4d
#define glVertexAttrib4d GPU_lgl_VertexAttrib4d
#undef glVertexAttrib4dv
#define glVertexAttrib4dv GPU_lgl_VertexAttrib4dv
#undef glVertexAttrib4bv
#define glVertexAttrib4bv GPU_lgl_VertexAttrib4bv
#undef glVertexAttrib4iv
#define glVertexAttrib4iv GPU_lgl_VertexAttrib4iv
#undef glVertexAttrib4ubv
#define glVertexAttrib4ubv GPU_lgl_VertexAttrib4ubv
#undef glVertexAttrib4usv
#define glVertexAttrib4usv GPU_lgl_VertexAttrib4usv
#undef glVertexAttrib4uiv
#define glVertexAttrib4uiv GPU_lgl_VertexAttrib4uiv
#undef glVertexAttrib4Nbv
#define glVertexAttrib4Nbv GPU_lgl_VertexAttrib4Nbv
#undef glVertexAttrib4Nsv
#define glVertexAttrib4Nsv GPU_lgl_VertexAttrib4Nsv
#undef glVertexAttrib4Niv
#define glVertexAttrib4Niv GPU_lgl_VertexAttrib4Niv
#undef glVertexAttrib4Nubv
#define glVertexAttrib4Nubv GPU_lgl_VertexAttrib4Nubv
#undef glVertexAttrib4Nusv
#define glVertexAttrib4Nusv GPU_lgl_VertexAttrib4Nusv
#undef glVertexAttrib4Nuiv
#define glVertexAttrib4Nuiv GPU_lgl_VertexAttrib4Nuiv
#undef glVertexAttrib4Nub
#define glVertexAttrib4Nub GPU_lgl_VertexAttrib4Nub

/* ARB and EXT names */
#undef glActiveTextureARB
#define glActiveTextureARB glActiveTexture
#undef glClientActiveTextureARB
#define glClientActiveTextureARB glClientActiveTexture
#undef glBindBufferARB
#define glBindBufferARB glBindBuffer
#undef glGenBuffersARB
#define glGenBuffersARB glGenBuffers
#undef glDeleteBuffersARB
#define glDeleteBuffersARB glDeleteBuffers
#undef glBufferDataARB
#define glBufferDataARB glBufferData
#undef glBufferSubDataARB
#define glBufferSubDataARB glBufferSubData
#undef glMapBufferARB
#define glMapBufferARB glMapBuffer
#undef glUnmapBufferARB
#define glUnmapBufferARB glUnmapBuffer
#undef glIsBufferARB
#define glIsBufferARB glIsBuffer
#undef glGetBufferParameterivARB
#define glGetBufferParameterivARB glGetBufferParameteriv
#undef glUseProgramObjectARB
#define glUseProgramObjectARB glUseProgram
#undef glCreateShaderObjectARB
#define glCreateShaderObjectARB glCreateShader
#undef glShaderSourceARB
#define glShaderSourceARB glShaderSource
#undef glCompileShaderARB
#define glCompileShaderARB glCompileShader
#undef glCreateProgramObjectARB
#define glCreateProgramObjectARB glCreateProgram
#undef glAttachObjectARB
#define glAttachObjectARB glAttachShader
#undef glDetachObjectARB
#define glDetachObjectARB glDetachShader
#undef glLinkProgramARB
#define glLinkProgramARB glLinkProgram
#undef glValidateProgramARB
#define glValidateProgramARB glValidateProgram
#undef glGetUniformLocationARB
#define glGetUniformLocationARB glGetUniformLocation
#undef glGetAttribLocationARB
#define glGetAttribLocationARB glGetAttribLocation
#undef glBindAttribLocationARB
#define glBindAttribLocationARB glBindAttribLocation
#undef glVertexAttribPointerARB
#define glVertexAttribPointerARB glVertexAttribPointer
#undef glEnableVertexAttribArrayARB
#define glEnableVertexAttribArrayARB glEnableVertexAttribArray
#undef glDisableVertexAttribArrayARB
#define glDisableVertexAttribArrayARB glDisableVertexAttribArray
#undef glDeleteObjectARB
#define glDeleteObjectARB GPU_lgl_DeleteObject
#undef glGetObjectParameterivARB
#define glGetObjectParameterivARB GPU_lgl_GetObjectParameteriv
#undef glGetInfoLogARB
#define glGetInfoLogARB GPU_lgl_GetInfoLog
#undef glDrawArraysInstancedARB
#define glDrawArraysInstancedARB glDrawArraysInstanced
#undef glDrawElementsInstancedARB
#define glDrawElementsInstancedARB glDrawElementsInstanced
#undef glGenerateMipmapEXT
#define glGenerateMipmapEXT glGenerateMipmap
#undef glBindFramebufferEXT
#define glBindFramebufferEXT glBindFramebuffer
#undef glGenFramebuffersEXT
#define glGenFramebuffersEXT glGenFramebuffers
#undef glDeleteFramebuffersEXT
#define glDeleteFramebuffersEXT glDeleteFramebuffers
#undef glFramebufferTexture2DEXT
#define glFramebufferTexture2DEXT glFramebufferTexture2D
#undef glFramebufferTextureLayerEXT
#define glFramebufferTextureLayerEXT glFramebufferTextureLayer
#undef glCheckFramebufferStatusEXT
#define glCheckFramebufferStatusEXT glCheckFramebufferStatus
#undef glFramebufferRenderbufferEXT
#define glFramebufferRenderbufferEXT glFramebufferRenderbuffer
#undef glBindRenderbufferEXT
#define glBindRenderbufferEXT glBindRenderbuffer
#undef glGenRenderbuffersEXT
#define glGenRenderbuffersEXT glGenRenderbuffers
#undef glDeleteRenderbuffersEXT
#define glDeleteRenderbuffersEXT glDeleteRenderbuffers
#undef glRenderbufferStorageEXT
#define glRenderbufferStorageEXT glRenderbufferStorage
#undef glRenderbufferStorageMultisampleEXT
#define glRenderbufferStorageMultisampleEXT glRenderbufferStorageMultisample
#undef glBlitFramebufferEXT
#define glBlitFramebufferEXT glBlitFramebuffer
#undef glIsFramebufferEXT
#define glIsFramebufferEXT glIsFramebuffer
#undef glDebugMessageCallbackARB
#define glDebugMessageCallbackARB glDebugMessageCallback
#undef glDebugMessageControlARB
#define glDebugMessageControlARB glDebugMessageControl
#undef glDebugMessageInsertARB
#define glDebugMessageInsertARB glDebugMessageInsert
#undef glUniform1fARB
#define glUniform1fARB glUniform1f
#undef glUniform1fvARB
#define glUniform1fvARB glUniform1fv
#undef glUniform1iARB
#define glUniform1iARB glUniform1i
#undef glUniform1ivARB
#define glUniform1ivARB glUniform1iv
#undef glUniform2fARB
#define glUniform2fARB glUniform2f
#undef glUniform2fvARB
#define glUniform2fvARB glUniform2fv
#undef glUniform2iARB
#define glUniform2iARB glUniform2i
#undef glUniform2ivARB
#define glUniform2ivARB glUniform2iv
#undef glUniform3fARB
#define glUniform3fARB glUniform3f
#undef glUniform3fvARB
#define glUniform3fvARB glUniform3fv
#undef glUniform3iARB
#define glUniform3iARB glUniform3i
#undef glUniform3ivARB
#define glUniform3ivARB glUniform3iv
#undef glUniform4fARB
#define glUniform4fARB glUniform4f
#undef glUniform4fvARB
#define glUniform4fvARB glUniform4fv
#undef glUniform4iARB
#define glUniform4iARB glUniform4i
#undef glUniform4ivARB
#define glUniform4ivARB glUniform4iv
#undef glUniformMatrix2fvARB
#define glUniformMatrix2fvARB glUniformMatrix2fv
#undef glUniformMatrix3fvARB
#define glUniformMatrix3fvARB glUniformMatrix3fv
#undef glUniformMatrix4fvARB
#define glUniformMatrix4fvARB glUniformMatrix4fv
#undef glMultiTexCoord1sARB
#define glMultiTexCoord1sARB glMultiTexCoord1s
#undef glMultiTexCoord1svARB
#define glMultiTexCoord1svARB glMultiTexCoord1sv
#undef glMultiTexCoord1iARB
#define glMultiTexCoord1iARB glMultiTexCoord1i
#undef glMultiTexCoord1ivARB
#define glMultiTexCoord1ivARB glMultiTexCoord1iv
#undef glMultiTexCoord1fARB
#define glMultiTexCoord1fARB glMultiTexCoord1f
#undef glMultiTexCoord1fvARB
#define glMultiTexCoord1fvARB glMultiTexCoord1fv
#undef glMultiTexCoord1dARB
#define glMultiTexCoord1dARB glMultiTexCoord1d
#undef glMultiTexCoord1dvARB
#define glMultiTexCoord1dvARB glMultiTexCoord1dv
#undef glMultiTexCoord2sARB
#define glMultiTexCoord2sARB glMultiTexCoord2s
#undef glMultiTexCoord2svARB
#define glMultiTexCoord2svARB glMultiTexCoord2sv
#undef glMultiTexCoord2iARB
#define glMultiTexCoord2iARB glMultiTexCoord2i
#undef glMultiTexCoord2ivARB
#define glMultiTexCoord2ivARB glMultiTexCoord2iv
#undef glMultiTexCoord2fARB
#define glMultiTexCoord2fARB glMultiTexCoord2f
#undef glMultiTexCoord2fvARB
#define glMultiTexCoord2fvARB glMultiTexCoord2fv
#undef glMultiTexCoord2dARB
#define glMultiTexCoord2dARB glMultiTexCoord2d
#undef glMultiTexCoord2dvARB
#define glMultiTexCoord2dvARB glMultiTexCoord2dv
#undef glMultiTexCoord3sARB
#define glMultiTexCoord3sARB glMultiTexCoord3s
#undef glMultiTexCoord3svARB
#define glMultiTexCoord3svARB glMultiTexCoord3sv
#undef glMultiTexCoord3iARB
#define glMultiTexCoord3iARB glMultiTexCoord3i
#undef glMultiTexCoord3ivARB
#define glMultiTexCoord3ivARB glMultiTexCoord3iv
#undef glMultiTexCoord3fARB
#define glMultiTexCoord3fARB glMultiTexCoord3f
#undef glMultiTexCoord3fvARB
#define glMultiTexCoord3fvARB glMultiTexCoord3fv
#undef glMultiTexCoord3dARB
#define glMultiTexCoord3dARB glMultiTexCoord3d
#undef glMultiTexCoord3dvARB
#define glMultiTexCoord3dvARB glMultiTexCoord3dv
#undef glMultiTexCoord4sARB
#define glMultiTexCoord4sARB glMultiTexCoord4s
#undef glMultiTexCoord4svARB
#define glMultiTexCoord4svARB glMultiTexCoord4sv
#undef glMultiTexCoord4iARB
#define glMultiTexCoord4iARB glMultiTexCoord4i
#undef glMultiTexCoord4ivARB
#define glMultiTexCoord4ivARB glMultiTexCoord4iv
#undef glMultiTexCoord4fARB
#define glMultiTexCoord4fARB glMultiTexCoord4f
#undef glMultiTexCoord4fvARB
#define glMultiTexCoord4fvARB glMultiTexCoord4fv
#undef glMultiTexCoord4dARB
#define glMultiTexCoord4dARB glMultiTexCoord4d
#undef glMultiTexCoord4dvARB
#define glMultiTexCoord4dvARB glMultiTexCoord4dv
#undef glVertexAttrib1sARB
#define glVertexAttrib1sARB glVertexAttrib1s
#undef glVertexAttrib1svARB
#define glVertexAttrib1svARB glVertexAttrib1sv
#undef glVertexAttrib1fARB
#define glVertexAttrib1fARB glVertexAttrib1f
#undef glVertexAttrib1fvARB
#define glVertexAttrib1fvARB glVertexAttrib1fv
#undef glVertexAttrib1dARB
#define glVertexAttrib1dARB glVertexAttrib1d
#undef glVertexAttrib1dvARB
#define glVertexAttrib1dvARB glVertexAttrib1dv
#undef glVertexAttrib2sARB
#define glVertexAttrib2sARB glVertexAttrib2s
#undef glVertexAttrib2svARB
#define glVertexAttrib2svARB glVertexAttrib2sv
#undef glVertexAttrib2fARB
#define glVertexAttrib2fARB glVertexAttrib2f
#undef glVertexAttrib2fvARB
#define glVertexAttrib2fvARB glVertexAttrib2fv
#undef glVertexAttrib2dARB
#define glVertexAttrib2dARB glVertexAttrib2d
#undef glVertexAttrib2dvARB
#define glVertexAttrib2dvARB glVertexAttrib2dv
#undef glVertexAttrib3sARB
#define glVertexAttrib3sARB glVertexAttrib3s
#undef glVertexAttrib3svARB
#define glVertexAttrib3svARB glVertexAttrib3sv
#undef glVertexAttrib3fARB
#define glVertexAttrib3fARB glVertexAttrib3f
#undef glVertexAttrib3fvARB
#define glVertexAttrib3fvARB glVertexAttrib3fv
#undef glVertexAttrib3dARB
#define glVertexAttrib3dARB glVertexAttrib3d
#undef glVertexAttrib3dvARB
#define glVertexAttrib3dvARB glVertexAttrib3dv
#undef glVertexAttrib4sARB
#define glVertexAttrib4sARB glVertexAttrib4s
#undef glVertexAttrib4svARB
#define glVertexAttrib4svARB glVertexAttrib4sv
#undef glVertexAttrib4fARB
#define glVertexAttrib4fARB glVertexAttrib4f
#undef glVertexAttrib4fvARB
#define glVertexAttrib4fvARB glVertexAttrib4fv
#undef glVertexAttrib4dARB
#define glVertexAttrib4dARB glVertexAttrib4d
#undef glVertexAttrib4dvARB
#define glVertexAttrib4dvARB glVertexAttrib4dv
#undef glVertexAttrib4bvARB
#define glVertexAttrib4bvARB glVertexAttrib4bv
#undef glVertexAttrib4ivARB
#define glVertexAttrib4ivARB glVertexAttrib4iv
#undef glVertexAttrib4ubvARB
#define glVertexAttrib4ubvARB glVertexAttrib4ubv
#undef glVertexAttrib4usvARB
#define glVertexAttrib4usvARB glVertexAttrib4usv
#undef glVertexAttrib4uivARB
#define glVertexAttrib4uivARB glVertexAttrib4uiv
#undef glVertexAttrib4NbvARB
#define glVertexAttrib4NbvARB glVertexAttrib4Nbv
#undef glVertexAttrib4NsvARB
#define glVertexAttrib4NsvARB glVertexAttrib4Nsv
#undef glVertexAttrib4NivARB
#define glVertexAttrib4NivARB glVertexAttrib4Niv
#undef glVertexAttrib4NubvARB
#define glVertexAttrib4NubvARB glVertexAttrib4Nubv
#undef glVertexAttrib4NusvARB
#define glVertexAttrib4NusvARB glVertexAttrib4Nusv
#undef glVertexAttrib4NuivARB
#define glVertexAttrib4NuivARB glVertexAttrib4Nuiv
#undef glVertexAttrib4NubARB
#define glVertexAttrib4NubARB glVertexAttrib4Nub

/* GLU */
/* (object-like: MSVC's preprocessor gives bgl's expanded argument lists to
 * a function-like macro as one argument) */
#undef gluNewQuadric
#undef gluDeleteQuadric
#define gluNewQuadric GPU_glu_NewQuadric
#define gluDeleteQuadric GPU_glu_DeleteQuadric
#define gluQuadricDrawStyle GPU_glu_QuadricDrawStyle
#define gluQuadricNormals GPU_glu_QuadricNormals
#define gluQuadricOrientation GPU_glu_QuadricOrientation
#define gluQuadricTexture GPU_glu_QuadricTexture
#define gluSphere GPU_glu_Sphere
#define gluCylinder GPU_glu_Cylinder
#define gluDisk GPU_glu_Disk
#define gluPartialDisk GPU_glu_PartialDisk
#define gluPerspective GPU_glu_Perspective
#define gluOrtho2D GPU_glu_Ortho2D
#define gluLookAt GPU_glu_LookAt
#define gluPickMatrix GPU_glu_PickMatrix

#endif  /* GPU_LEGACY_NO_REDIRECT */

#endif  /* __GPU_LEGACY_H__ */
