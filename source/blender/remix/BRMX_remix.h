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

/** \file BRMX_remix.h
 *  \ingroup remix
 *
 * BGE-Remix, Blender side: loads bge_remix_bridge.dll (next to blender.exe)
 * on first use and offers it to Python as the built-in module "_bge_remix".
 */

#ifndef __BRMX_REMIX_H__
#define __BRMX_REMIX_H__

#ifdef __cplusplus
extern "C" {
#endif

struct _object;

/* Python module "_bge_remix" (registered in bpy_interface.c's inittab). */
struct _object *BRMX_initPython(void);

/* Shut Remix down if it was started (also runs on Python exit). */
void BRMX_exit(void);

#ifdef __cplusplus
}
#endif

#endif  /* __BRMX_REMIX_H__ */
