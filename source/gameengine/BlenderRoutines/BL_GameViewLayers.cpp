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

/** \file gameengine/BlenderRoutines/BL_GameViewLayers.cpp
 *  \ingroup blroutines
 *
 * BGE-Remix P7: every layer of the Game View: its elements (from Blender's
 * own data, checked against what the game's converter made), their warnings
 * and the shapes drawn for them (ranges, links, outlines). The same code
 * makes the in-game report (BL_GameViewReportGame).
 */

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef WITH_PYTHON
#  include <Python.h>
#endif

#include "BL_GameViewBuild.h"

extern "C" {
	#include "MEM_guardedalloc.h"

	#include "DNA_action_types.h"
	#include "DNA_actuator_types.h"
	#include "DNA_anim_types.h"
	#include "DNA_armature_types.h"
	#include "DNA_camera_types.h"
	#include "DNA_constraint_types.h"
	#include "DNA_controller_types.h"
	#include "DNA_curve_types.h"
	#include "DNA_group_types.h"
	#include "DNA_image_types.h"
	#include "DNA_key_types.h"
	#include "DNA_lamp_types.h"
	#include "DNA_material_types.h"
	#include "DNA_mesh_types.h"
	#include "DNA_meshdata_types.h"
	#include "DNA_modifier_types.h"
	#include "DNA_node_types.h"
	#include "DNA_object_force.h"
	#include "DNA_object_types.h"
	#include "DNA_particle_types.h"
	#include "DNA_property_types.h"
	#include "DNA_scene_types.h"
	#include "DNA_sensor_types.h"
	#include "DNA_sound_types.h"
	#include "DNA_text_types.h"
	#include "DNA_texture_types.h"
	#include "DNA_vfont_types.h"
	#include "DNA_world_types.h"

	#include "BKE_fcurve.h"
	#include "BKE_idprop.h"
	#include "BKE_image.h"
	#include "BKE_main.h"
	#include "BKE_material.h"
	#include "BKE_customdata.h"
	#include "BKE_object.h"
	#include "BKE_text.h"

	#include "BLI_fileops.h"
	#include "BLI_listbase.h"
	#include "BLI_math.h"
	#include "BLI_path_util.h"
	#include "BLI_string.h"
	#include "BLI_utildefines.h"

	#include "IMB_imbuf_types.h"

	#include "../../blender/windowmanager/wm_event_types.h"
}

#ifdef WITH_AUDASPACE
#  include AUD_SOUND_H
#endif

#ifdef WITH_REMIX
#  include "BRMX_bake.h"
#  include "BRMX_loader.h"
#  include "BRMX_material.h"
#endif

/* the scene's ID property holding the PhysX choice (P6, DNA_scene_types.h) */
#ifndef BGE_PHYSX_IDPROP
#  define BGE_PHYSX_IDPROP "_bge_physx"
#endif

namespace {

static std::string fmt(const char *format, ...)
#ifdef __GNUC__
	__attribute__((format(printf, 1, 2)))
#endif
	;

static std::string fmt(const char *format, ...)
{
	char buf[512];
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	return buf;
}

const char *name_of(const Object *ob)
{
	return ob ? ob->id.name + 2 : "";
}

const char *ob_type_id(short type)
{
	switch (type) {
		case OB_MESH: return "MESH";
		case OB_CURVE: return "CURVE";
		case OB_SURF: return "SURFACE";
		case OB_MBALL: return "META";
		case OB_FONT: return "FONT";
		case OB_ARMATURE: return "ARMATURE";
		case OB_LATTICE: return "LATTICE";
		case OB_EMPTY: return "EMPTY";
		case OB_CAMERA: return "CAMERA";
		case OB_LAMP: return "LAMP";
		case OB_SPEAKER: return "SPEAKER";
		default: return "OTHER";
	}
}

const char *ob_type_name(short type)
{
	switch (type) {
		case OB_MESH: return "Mesh";
		case OB_CURVE: return "Curve";
		case OB_SURF: return "Surface";
		case OB_MBALL: return "Metaball";
		case OB_FONT: return "Text";
		case OB_ARMATURE: return "Armature";
		case OB_LATTICE: return "Lattice";
		case OB_EMPTY: return "Empty";
		case OB_CAMERA: return "Camera";
		case OB_LAMP: return "Lamp";
		case OB_SPEAKER: return "Speaker";
		default: return "Object";
	}
}

/* the game makes objects of these types */
bool game_type(short type)
{
	return ELEM(type, OB_MESH, OB_EMPTY, OB_ARMATURE, OB_CAMERA, OB_LAMP, OB_FONT);
}

/* World > Physics type, as the Physics panel shows it (rna_object.c) */
int physics_type(const Object *ob)
{
	const int f = ob->gameflag;
	if (!(f & OB_COLLISION)) {
		if (f & OB_OCCLUDER) return OB_BODY_TYPE_OCCLUDER;
		if (f & OB_NAVMESH) return OB_BODY_TYPE_NAVMESH;
		return OB_BODY_TYPE_NO_COLLISION;
	}
	if (f & OB_CHARACTER) return OB_BODY_TYPE_CHARACTER;
	if (f & OB_SENSOR) return OB_BODY_TYPE_SENSOR;
	if (!(f & OB_DYNAMIC)) return OB_BODY_TYPE_STATIC;
	if (!(f & (OB_RIGID_BODY | OB_SOFT_BODY))) return OB_BODY_TYPE_DYNAMIC;
	if (f & OB_RIGID_BODY) return OB_BODY_TYPE_RIGID;
	return OB_BODY_TYPE_SOFT;
}

const char *physics_id(int t)
{
	static const char *ids[] = {"NO_COLLISION", "STATIC", "DYNAMIC", "RIGID_BODY", "SOFT_BODY", "OCCLUDER",
	                            "SENSOR", "NAVMESH", "CHARACTER"};
	return (t >= 0 && t <= 8) ? ids[t] : "NO_COLLISION";
}

const char *physics_name(int t)
{
	static const char *names[] = {"No Collision", "Static", "Dynamic", "Rigid Body", "Soft Body", "Occluder",
	                              "Sensor", "Navigation Mesh", "Character"};
	return (t >= 0 && t <= 8) ? names[t] : "?";
}

bool moving(int t)
{
	return ELEM(t, OB_BODY_TYPE_DYNAMIC, OB_BODY_TYPE_RIGID, OB_BODY_TYPE_SOFT, OB_BODY_TYPE_CHARACTER);
}

/* the collision bounds the game uses (without "Collision Bounds", meshes
 * that do not move are triangle meshes and moving objects spheres) */
int bounds_type(const Object *ob, int t)
{
	if (ob->gameflag & OB_BOUNDS) {
		return ob->collision_boundtype;
	}
	if (ob->type == OB_MESH && !moving(t)) {
		return OB_BOUND_TRIANGLE_MESH;
	}
	return OB_BOUND_SPHERE;
}

const char *bounds_id(int b)
{
	switch (b) {
		case OB_BOUND_BOX: return "BOX";
		case OB_BOUND_SPHERE: return "SPHERE";
		case OB_BOUND_CYLINDER: return "CYLINDER";
		case OB_BOUND_CONE: return "CONE";
		case OB_BOUND_CONVEX_HULL: return "CONVEX_HULL";
		case OB_BOUND_TRIANGLE_MESH: return "TRIANGLE_MESH";
		case OB_BOUND_CAPSULE: return "CAPSULE";
		default: return "BOX";
	}
}

const char *bounds_name(int b)
{
	switch (b) {
		case OB_BOUND_BOX: return "Box";
		case OB_BOUND_SPHERE: return "Sphere";
		case OB_BOUND_CYLINDER: return "Cylinder";
		case OB_BOUND_CONE: return "Cone";
		case OB_BOUND_CONVEX_HULL: return "Convex Hull";
		case OB_BOUND_TRIANGLE_MESH: return "Triangle Mesh";
		case OB_BOUND_CAPSULE: return "Capsule";
		default: return "Box";
	}
}

const char *sensor_id(short type)
{
	switch (type) {
		case SENS_ALWAYS: return "ALWAYS";
		case SENS_TOUCH: return "COLLISION";  /* old files: shown as a collision sensor */
		case SENS_NEAR: return "NEAR";
		case SENS_KEYBOARD: return "KEYBOARD";
		case SENS_PROPERTY: return "PROPERTY";
		case SENS_MOUSE: return "MOUSE";
		case SENS_COLLISION: return "COLLISION";
		case SENS_RADAR: return "RADAR";
		case SENS_RANDOM: return "RANDOM";
		case SENS_RAY: return "RAY";
		case SENS_MESSAGE: return "MESSAGE";
		case SENS_JOYSTICK: return "JOYSTICK";
		case SENS_ACTUATOR: return "ACTUATOR";
		case SENS_DELAY: return "DELAY";
		case SENS_ARMATURE: return "ARMATURE";
		default: return NULL;
	}
}

const char *controller_id(short type)
{
	switch (type) {
		case CONT_LOGIC_AND: return "LOGIC_AND";
		case CONT_LOGIC_OR: return "LOGIC_OR";
		case CONT_LOGIC_NAND: return "LOGIC_NAND";
		case CONT_LOGIC_NOR: return "LOGIC_NOR";
		case CONT_LOGIC_XOR: return "LOGIC_XOR";
		case CONT_LOGIC_XNOR: return "LOGIC_XNOR";
		case CONT_EXPRESSION: return "EXPRESSION";
		case CONT_PYTHON: return "PYTHON";
		default: return NULL;
	}
}

const char *actuator_id(short type)
{
	switch (type) {
		case ACT_ACTION: return "ACTION";
		case ACT_SHAPEACTION: return "SHAPE_ACTION";
		case ACT_ARMATURE: return "ARMATURE";
		case ACT_CAMERA: return "CAMERA";
		case ACT_CONSTRAINT: return "CONSTRAINT";
		case ACT_EDIT_OBJECT: return "EDIT_OBJECT";
		case ACT_2DFILTER: return "FILTER_2D";
		case ACT_GAME: return "GAME";
		case ACT_MESSAGE: return "MESSAGE";
		case ACT_OBJECT: return "MOTION";
		case ACT_MOUSE: return "MOUSE";
		case ACT_PARENT: return "PARENT";
		case ACT_PROPERTY: return "PROPERTY";
		case ACT_RANDOM: return "RANDOM";
		case ACT_SCENE: return "SCENE";
		case ACT_SOUND: return "SOUND";
		case ACT_STATE: return "STATE";
		case ACT_STEERING: return "STEERING";
		case ACT_VISIBILITY: return "VISIBILITY";
		default: return NULL;
	}
}

const char *property_id(short type)
{
	switch (type) {
		case GPROP_BOOL: return "BOOL";
		case GPROP_INT: return "INT";
		case GPROP_FLOAT: return "FLOAT";
		case GPROP_STRING: return "STRING";
		case GPROP_TIME: return "TIMER";
		default: return NULL;
	}
}

const char *lamp_id(short type)
{
	switch (type) {
		case LA_LOCAL: return "POINT";
		case LA_SUN: return "SUN";
		case LA_SPOT: return "SPOT";
		case LA_HEMI: return "HEMI";
		case LA_AREA: return "AREA";
		default: return "POINT";
	}
}

const char *joint_id(int type)
{
	switch (type) {
		case CONSTRAINT_RB_BALL: return "BALL";
		case CONSTRAINT_RB_HINGE: return "HINGE";
		case CONSTRAINT_RB_CONETWIST: return "CONE_TWIST";
		case CONSTRAINT_RB_GENERIC6DOF: return "GENERIC_6_DOF";
		default: return NULL;
	}
}

/* a key's name, for the Input layer */
std::string key_name(int key)
{
	if (key >= AKEY && key <= ZKEY) return std::string(1, (char)('A' + key - AKEY));
	if (key >= ZEROKEY && key <= NINEKEY) return std::string(1, (char)('0' + key - ZEROKEY));
	if (key >= F1KEY && key <= F12KEY) return fmt("F%d", key - F1KEY + 1);
	if (key >= PAD0 && key <= PAD9) return fmt("Numpad %d", key - PAD0);
	switch (key) {
		case 0: return "";
		case SPACEKEY: return "Space";
		case RETKEY: return "Enter";
		case ESCKEY: return "Esc";
		case TABKEY: return "Tab";
		case BACKSPACEKEY: return "Backspace";
		case DELKEY: return "Delete";
		case LEFTARROWKEY: return "Left";
		case RIGHTARROWKEY: return "Right";
		case UPARROWKEY: return "Up";
		case DOWNARROWKEY: return "Down";
		case LEFTSHIFTKEY: return "Left Shift";
		case RIGHTSHIFTKEY: return "Right Shift";
		case LEFTCTRLKEY: return "Left Ctrl";
		case RIGHTCTRLKEY: return "Right Ctrl";
		case LEFTALTKEY: return "Left Alt";
		case RIGHTALTKEY: return "Right Alt";
		case PAGEUPKEY: return "Page Up";
		case PAGEDOWNKEY: return "Page Down";
		case HOMEKEY: return "Home";
		case ENDKEY: return "End";
		case INSERTKEY: return "Insert";
		default: return fmt("key %d", key);
	}
}

std::string mouse_event_name(int type)
{
	switch (type) {
		case BL_SENS_MOUSE_LEFT_BUTTON: return "Left button";
		case BL_SENS_MOUSE_MIDDLE_BUTTON: return "Middle button";
		case BL_SENS_MOUSE_RIGHT_BUTTON: return "Right button";
		case BL_SENS_MOUSE_WHEEL_UP: return "Wheel up";
		case BL_SENS_MOUSE_WHEEL_DOWN: return "Wheel down";
		case BL_SENS_MOUSE_MOVEMENT: return "Movement";
		case BL_SENS_MOUSE_MOUSEOVER: return "Over the object";
		case BL_SENS_MOUSE_MOUSEOVER_ANY: return "Over any object";
		default: return fmt("event %d", type);
	}
}

/* a file path from Blender's data, made absolute, exists (packed counts as present) */
bool file_present(Main *bmain, const ID *id, const char *path, bool packed)
{
	if (packed) {
		return true;
	}
	if (!path || !path[0]) {
		return false;
	}
	if (STREQ(path, "<builtin>")) {
		return true;
	}
	char abs[FILE_MAX];
	BLI_strncpy(abs, path, sizeof(abs));
	BLI_path_abs(abs, ID_BLEND_PATH(bmain, id));
	return BLI_exists(abs) != 0;
}

/* an object's lowest and highest point above its origin, in the world */
void ob_extent_z(Object *ob, float *r_bottom, float *r_top)
{
	BoundBox *bb = ob->type == OB_MESH || ob->type == OB_FONT || ob->type == OB_ARMATURE ?
	                   BKE_object_boundbox_get(ob) : NULL;
	if (!bb) {
		*r_bottom = -0.2f;
		*r_top = 0.25f;
		return;
	}
	float lo = FLT_MAX, hi = -FLT_MAX;
	for (int i = 0; i < 8; i++) {
		float co[3];
		mul_v3_m4v3(co, ob->obmat, bb->vec[i]);
		lo = min_ff(lo, co[2]);
		hi = max_ff(hi, co[2]);
	}
	*r_bottom = lo - ob->obmat[3][2];
	*r_top = hi - ob->obmat[3][2];
}

/* world-space bounds of an object (its origin for objects without a box) */
void ob_world_bounds(Object *ob, float r_min[3], float r_max[3])
{
	BoundBox *bb = ob->type == OB_MESH || ob->type == OB_FONT ? BKE_object_boundbox_get(ob) : NULL;
	if (!bb) {
		copy_v3_v3(r_min, ob->obmat[3]);
		copy_v3_v3(r_max, ob->obmat[3]);
		return;
	}
	INIT_MINMAX(r_min, r_max);
	for (int i = 0; i < 8; i++) {
		float co[3];
		mul_v3_m4v3(co, ob->obmat, bb->vec[i]);
		minmax_v3v3_v3(r_min, r_max, co);
	}
}

/* a game object's estimated triangles */
int ob_triangles(Object *ob)
{
	if (ob->type != OB_MESH || !ob->data) {
		return 0;
	}
	Mesh *me = (Mesh *)ob->data;
	return std::max(0, me->totloop - 2 * me->totpoly);
}

bool brick_every_frame(const bSensor *s)
{
	return (s->pulse & SENS_PULSE_REPEAT) && s->freq == 0;
}

/* a Python script, compiled to check it; the error ("line N: ...") or "" */
std::string compile_check(const std::string &source, const char *filename)
{
#ifdef WITH_PYTHON
	if (!Py_IsInitialized()) {
		return "";
	}
	PyGILState_STATE gil = PyGILState_Ensure();
	std::string err;
	PyObject *code = Py_CompileString(source.c_str(), filename, Py_file_input);
	if (code) {
		Py_DECREF(code);
	}
	else {
		PyObject *type, *value, *tb;
		PyErr_Fetch(&type, &value, &tb);
		PyErr_NormalizeException(&type, &value, &tb);
		long line = -1;
		std::string msg = "it does not compile";
		if (value) {
			PyObject *l = PyObject_GetAttrString(value, "lineno");
			if (l) {
				line = PyLong_AsLong(l);
				Py_DECREF(l);
			}
			PyErr_Clear();
			PyObject *m = PyObject_GetAttrString(value, "msg");
			if (m && PyUnicode_Check(m)) {
				msg = PyUnicode_AsUTF8(m);
			}
			Py_XDECREF(m);
			PyErr_Clear();
		}
		Py_XDECREF(type);
		Py_XDECREF(value);
		Py_XDECREF(tb);
		err = line > 0 ? fmt("line %ld: %s", line, msg.c_str()) : msg;
	}
	PyGILState_Release(gil);
	return err;
#else
	(void)source;
	(void)filename;
	return "";
#endif
}

/* calls from Blender 2.4x's game API that 2.79 no longer has */
std::string old_api(const std::string &source)
{
	static const char *calls[] = {"GameLogic.", "import GameLogic", "GameKeys", "import Rasterizer",
	                              "getOwner(", "getSensor(", "getActuator(", "addActiveActuator(",
	                              "getPosition(", "setPosition(", "getOrientation(", "setOrientation(",
	                              ".getProperty(", ".setProperty(", NULL};
	for (int i = 0; calls[i]; i++) {
		if (source.find(calls[i]) != std::string::npos) {
			return calls[i];
		}
	}
	return "";
}

/* the files a script loads with LibLoad("...") or LibLoad(logic.expandPath("...")) */
/* a quoted path at i, or one inside expandPath("...") there */
static bool quoted_path_at(const std::string &src, size_t i, std::string &r_path)
{
	while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) i++;
	const size_t ep = src.find("expandPath(", i);
	if (ep != std::string::npos && ep < i + 24 && src.find_first_of("\"'", i) > ep) {
		i = ep + 11;
		while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) i++;
	}
	if (i >= src.size() || (src[i] != '"' && src[i] != '\'')) return false;
	const char q = src[i];
	const size_t end = src.find(q, i + 1);
	if (end == std::string::npos) return false;
	r_path = src.substr(i + 1, end - i - 1);
	return true;
}

static bool ident_char(char c)
{
	return isalnum((unsigned char)c) || c == '_';
}

/* LibLoad("file.blend"), LibLoad(expandPath("//file.blend")), or
 * LibLoad(path) where path = one of those earlier in the script */
std::vector<std::string> libload_paths(const std::string &src)
{
	std::vector<std::string> paths;
	size_t at = 0;
	while ((at = src.find("LibLoad(", at)) != std::string::npos) {
		size_t i = at + 8;
		const size_t call = at;
		at = i;
		while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) i++;
		std::string path;
		if (quoted_path_at(src, i, path)) {
			paths.push_back(path);
			continue;
		}
		/* a variable: its last assignment before the call */
		size_t e = i;
		while (e < src.size() && ident_char(src[e])) e++;
		if (e == i) continue;
		const std::string name = src.substr(i, e - i);
		size_t found = std::string::npos;
		for (size_t p = src.find(name); p != std::string::npos && p < call; p = src.find(name, p + 1)) {
			if (p > 0 && (ident_char(src[p - 1]) || src[p - 1] == '.')) continue;
			size_t q = p + name.size();
			if (q < src.size() && ident_char(src[q])) continue;
			while (q < src.size() && (src[q] == ' ' || src[q] == '\t')) q++;
			if (q + 1 < src.size() && src[q] == '=' && src[q + 1] != '=') {
				found = q + 1;
			}
		}
		if (found != std::string::npos && quoted_path_at(src, found, path)) {
			paths.push_back(path);
		}
	}
	return paths;
}

std::string text_source(Text *text)
{
	char *buf = txt_to_buf(text);
	std::string s = buf ? buf : "";
	if (buf) {
		MEM_freeN(buf);
	}
	return s;
}

/* project a point into a camera's view: x, y in -1..1 when on screen */
bool camera_project(Object *camob, float aspect, const float co[3], float r_ndc[2], float *r_depth)
{
	if (!camob || camob->type != OB_CAMERA) {
		return false;
	}
	Camera *cam = (Camera *)camob->data;
	float imat[4][4], p[3];
	invert_m4_m4(imat, camob->obmat);
	mul_v3_m4v3(p, imat, co);
	*r_depth = -p[2];
	if (cam->type == CAM_ORTHO) {
		const float half = cam->ortho_scale * 0.5f;
		r_ndc[0] = p[0] / half;
		r_ndc[1] = p[1] / (half / aspect);
		return true;
	}
	if (-p[2] <= 1e-4f) {
		return false;
	}
	const float tanx = (cam->sensor_x * 0.5f) / max_ff(cam->lens, 1e-3f);
	r_ndc[0] = (p[0] / -p[2]) / tanx;
	r_ndc[1] = (p[1] / -p[2]) / (tanx / aspect);
	return true;
}

int idprop_int(IDProperty *group, const char *name, int def)
{
	IDProperty *p = group ? IDP_GetPropertyFromGroup(group, name) : NULL;
	if (!p) return def;
	if (p->type == IDP_INT) return IDP_Int(p);
	if (p->type == IDP_DOUBLE) return (int)IDP_Double(p);
	if (p->type == IDP_FLOAT) return (int)IDP_Float(p);
	return def;
}

float idprop_float(IDProperty *group, const char *name, float def)
{
	IDProperty *p = group ? IDP_GetPropertyFromGroup(group, name) : NULL;
	if (!p) return def;
	if (p->type == IDP_INT) return (float)IDP_Int(p);
	if (p->type == IDP_DOUBLE) return (float)IDP_Double(p);
	if (p->type == IDP_FLOAT) return IDP_Float(p);
	return def;
}

#ifdef WITH_REMIX
/* the RTX Remix runtime is installed (its folder holds d3d9.dll) */
bool remix_runtime_present()
{
#  ifdef _WIN32
	wchar_t dir[1024];
	if (!BRMX_runtime_dir(dir, 1000)) {
		return false;
	}
	std::wstring path = std::wstring(dir) + L"\\d3d9.dll";
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#  else
	return false;
#  endif
}
#endif

}  /* namespace */

/* ------------------------------------------------------------------------- */

GVTargets::GVTargets()
    : fps(60), objects(10000), lights(8), sounds(32), bodies(2000), triangles(2000000), texture_mb(4000.0f),
      shadow_size(2048)
{
}

GVBuilder::GVBuilder(Main *bmain, Scene *scene, const GVConverted &conv)
    : top(0.0f), m_main(bmain), m_scene(scene), m_conv(conv), m_camera(NULL), m_physx(0), m_remix(false)
{
	zero_v3(centre);
}

int GVBuilder::Add(const char *kind, const char *name, Object *owner, const char *info)
{
	const GVKind *k = BL_GameViewKind(kind);
	GVElement e;
	memset(&e, 0, sizeof(e));
	e.id = (int)elements.size();
	e.layer = k ? k->layer : GV_LAYER_UNASSIGNED;
	e.state = GV_STATE_GREEN;
	BLI_strncpy(e.kind, kind, sizeof(e.kind));
	BLI_strncpy(e.name, name ? name : "", sizeof(e.name));
	BLI_strncpy(e.owner, owner ? owner->id.name + 2 : "", sizeof(e.owner));
	BLI_strncpy(e.avatar, k ? k->avatar : "QUESTION", sizeof(e.avatar));
	BLI_strncpy(e.info, info ? info : "", sizeof(e.info));
	if (k && k->is_small) {
		e.flag |= GV_EL_SMALL;
	}
	if (!owner) {
		e.flag |= GV_EL_SCENE_LEVEL;
	}
	elements.push_back(e);
	m_owner.push_back(owner);
	m_warn.push_back(std::vector<GVWarning>());
	const int idx = e.id;
	if (!k) {
		Warn(idx, "UNASSIGNED", GV_WARN_NOTE, "no layer of the Game View claims this kind of element");
	}
	return idx;
}

void GVBuilder::Warn(int el, const char *code, int level, const char *text, bool mandatory, bool target)
{
	if (el < 0 || el >= (int)elements.size()) {
		return;
	}
	GVWarning w;
	memset(&w, 0, sizeof(w));
	w.element = el;
	w.layer = elements[el].layer;
	w.level = (short)level;
	w.mandatory = mandatory ? 1 : 0;
	w.target = target ? 1 : 0;
	BLI_strncpy(w.code, code, sizeof(w.code));
	BLI_strncpy(w.text, text, sizeof(w.text));
	BLI_snprintf(w.key, sizeof(w.key), "%s|%s|%s", code, elements[el].owner, elements[el].name);
	m_warn[el].push_back(w);
}

int GVBuilder::Shape(int type, int layer, int a, int b)
{
	GVShape s;
	memset(&s, 0, sizeof(s));
	s.type = (short)type;
	s.layer = (short)layer;
	s.state = GV_STATE_WHITE;
	s.a = a;
	s.b = b;
	s.point_first = -1;
	shapes.push_back(s);
	return (int)shapes.size() - 1;
}

int GVBuilder::Points(int shape, const std::vector<float> &xyz)
{
	shapes[shape].point_first = (int)(points.size() / 3);
	shapes[shape].point_count = (int)(xyz.size() / 3);
	points.insert(points.end(), xyz.begin(), xyz.end());
	return shapes[shape].point_first;
}

bool GVBuilder::InGame(Object *ob) const
{
	unsigned int lay = 0;
	for (Scene *sce = m_scene; sce; sce = sce->set) {
		lay |= sce->lay;
	}
	return (ob->lay & lay) != 0;
}

bool GVBuilder::Converted(Object *ob) const
{
	return !m_conv.converted || m_conv.objects.count(ob->id.name + 2) != 0;
}

float GVBuilder::Top(Object *ob) const
{
	float lo, hi;
	ob_extent_z(ob, &lo, &hi);
	return hi;
}

/* ------------------------------------------------------------------------- */

const std::vector<bSensor *> &GVBuilder::SensorsOf(const bController *c) const
{
	static const std::vector<bSensor *> none;
	std::map<const bController *, std::vector<bSensor *> >::const_iterator it = m_sensorsOf.find(c);
	return it == m_sensorsOf.end() ? none : it->second;
}

void GVBuilder::Build()
{
	/* the scene's objects, and those of its set scenes (the converter makes both) */
	std::set<Object *> seen;
	for (Scene *sce = m_scene; sce; sce = sce->set) {
		for (Base *base = (Base *)sce->base.first; base; base = base->next) {
			if (seen.insert(base->object).second) {
				m_objects.push_back(base->object);
			}
		}
	}
	m_camera = m_scene->camera;
	for (Object *ob : m_objects) {
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) {
			for (int i = 0; i < s->totlinks; i++) {
				if (s->links && s->links[i]) {
					m_sensorsOf[s->links[i]].push_back(s);
				}
			}
		}
	}

	IDProperty *props = m_scene->id.properties;
	const int physx = idprop_int(props, BGE_PHYSX_IDPROP, 0);
	m_physx = m_scene->gm.physicsEngine == WOPHY_NONE ? -1 : (physx == 1 || physx == 2 ? physx : 0);
	m_remix = idprop_int(props, "bge_remix", 0) != 0 || STREQ(m_scene->r.engine, "RTX_REMIX");

	IDProperty *t = props ? IDP_GetPropertyFromGroup(props, "_gameview_targets") : NULL;
	if (t && t->type == IDP_GROUP) {
		m_targets.fps = idprop_int(t, "fps", m_targets.fps);
		m_targets.objects = idprop_int(t, "objects", m_targets.objects);
		m_targets.lights = idprop_int(t, "lights", m_targets.lights);
		m_targets.sounds = idprop_int(t, "sounds", m_targets.sounds);
		m_targets.bodies = idprop_int(t, "bodies", m_targets.bodies);
		m_targets.triangles = idprop_int(t, "triangles", m_targets.triangles);
		m_targets.texture_mb = idprop_float(t, "texture_mb", m_targets.texture_mb);
		m_targets.shadow_size = idprop_int(t, "shadow_size", m_targets.shadow_size);
	}
	IDProperty *known = props ? IDP_GetPropertyFromGroup(props, "_gameview_known") : NULL;
	if (known && known->type == IDP_IDPARRAY) {
		IDProperty *arr = IDP_IDPArray(known);
		for (int i = 0; i < known->len; i++) {
			if (arr[i].type == IDP_STRING) {
				m_known.insert(IDP_String(&arr[i]));
			}
		}
	}

	/* the scene's centre and top (scene-level elements float above it) */
	float lo[3], hi[3];
	INIT_MINMAX(lo, hi);
	int n = 0;
	for (Object *ob : m_objects) {
		if (!InGame(ob) || !game_type(ob->type)) {
			continue;
		}
		float mn[3], mx[3];
		ob_world_bounds(ob, mn, mx);
		minmax_v3v3_v3(lo, hi, mn);
		minmax_v3v3_v3(lo, hi, mx);
		n++;
	}
	if (n == 0) {
		zero_v3(lo);
		zero_v3(hi);
	}
	mid_v3_v3v3(centre, lo, hi);
	top = hi[2];

	/* the scene itself first: scene-wide warnings hang on it */
	Add("scene", m_scene->id.name + 2, NULL, "the starting scene");
	elements[0].flag |= GV_EL_ACTIVE;

	Objects();
	Cameras();
	Interface();
	Rendering();
	Physics();
	Media();
	Logic();     /* before Data, Python, Messages, Input: they use the bricks */
	Data();
	Python();
	Animation();
	Messages();
	Navigation();
	Input();
	Scenes();
	Remix();
	Performance();
	Time();
	LeftOut();

	States();
	Layout();
	Flatten();
}

/* ------------------------------------------------------------------------- */
/* Entity: Objects */

void GVBuilder::Objects()
{
	/* objects something in the logic adds during play */
	std::set<Object *> added;
	for (Object *ob : m_objects) {
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type == ACT_EDIT_OBJECT) {
				bEditObjectActuator *ea = (bEditObjectActuator *)a->data;
				if (ea && ea->type == ACT_EDOB_ADD_OBJECT && ea->ob) {
					added.insert(ea->ob);
				}
			}
		}
	}
	int count = 0;
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) {
			continue;
		}
		std::string info = ob_type_name(ob->type);
		if (ob->type == OB_MESH) {
			info += fmt(", %d triangles", ob_triangles(ob));
		}
		if (ob->parent) {
			info += std::string(", child of ") + name_of(ob->parent);
		}
		const int el = Add(fmt("object:%s", ob_type_id(ob->type)).c_str(), name_of(ob), ob, info.c_str());
		m_objectElement[name_of(ob)] = el;
		if (!InGame(ob)) {
			elements[el].flag |= GV_EL_HIDDEN_LAYER;
			if (!added.count(ob)) {
				Warn(el, "OBJ_NEVER_ADDED", GV_WARN_NOTE,
				     "on a hidden layer, and nothing in the logic adds it (a script may)");
			}
		}
		else {
			count++;
		}
		if (ob->id.lib && (ob->id.lib->id.tag & LIB_TAG_MISSING)) {
			Warn(el, "OBJ_LIB_MISSING", GV_WARN_RED, fmt("its library file is missing (%s)", ob->id.lib->name).c_str(),
			     true);
		}
		/* a group instance: a dashed box around what it shows */
		if ((ob->transflag & OB_DUPLIGROUP) && ob->dup_group) {
			const int g = Add("group:INSTANCE", ob->dup_group->id.name + 2, ob,
			                  fmt("shows the group %s", ob->dup_group->id.name + 2).c_str());
			float mn[3], mx[3];
			INIT_MINMAX(mn, mx);
			bool any = false;
			for (GroupObject *go = (GroupObject *)ob->dup_group->gobject.first; go; go = go->next) {
				if (!go->ob) continue;
				float a[3], b[3];
				ob_world_bounds(go->ob, a, b);
				minmax_v3v3_v3(mn, mx, a);
				minmax_v3v3_v3(mn, mx, b);
				any = true;
			}
			if (any) {
				const int s = Shape(GV_SHAPE_BOX, GV_LAYER_OBJECTS, g);
				shapes[s].dashed = 1;
				for (int k = 0; k < 3; k++) {
					shapes[s].p[k] = (mx[k] - mn[k]) * 0.5f;
					/* the group's own origin offset (dupli_ofs) is where the instance sits */
					shapes[s].p[3 + k] = (mx[k] + mn[k]) * 0.5f - ob->dup_group->dupli_ofs[k];
				}
			}
		}
	}
	/* parents: a line from each child to its parent */
	for (Object *ob : m_objects) {
		if (!ob->parent || !m_objectElement.count(name_of(ob)) || !m_objectElement.count(name_of(ob->parent))) {
			continue;
		}
		const int s = Shape(GV_SHAPE_LINE, GV_LAYER_OBJECTS, m_objectElement[name_of(ob)],
		                    m_objectElement[name_of(ob->parent)]);
		shapes[s].p[6] = 1.0f;
	}
	if (count > m_targets.objects) {
		Warn(0, "OBJ_COUNT", GV_WARN_NOTE,
		     fmt("%d objects in the game: more than the target's %d", count, m_targets.objects).c_str(), false, true);
		m_warn[0].back().layer = GV_LAYER_OBJECTS;
	}
}

/* ------------------------------------------------------------------------- */
/* Entity: Cameras */

void GVBuilder::Cameras()
{
	/* cameras something in the logic makes active */
	std::set<Object *> made_active;
	for (Object *ob : m_objects) {
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type == ACT_SCENE) {
				bSceneActuator *sa = (bSceneActuator *)a->data;
				if (sa && sa->type == ACT_SCENE_CAMERA && sa->camera) {
					made_active.insert(sa->camera);
				}
			}
		}
	}
	/* how far the scene reaches, for the far plane */
	float lo[3], hi[3];
	INIT_MINMAX(lo, hi);
	for (Object *ob : m_objects) {
		if (InGame(ob) && game_type(ob->type)) {
			float a[3], b[3];
			ob_world_bounds(ob, a, b);
			minmax_v3v3_v3(lo, hi, a);
			minmax_v3v3_v3(lo, hi, b);
		}
	}
	const float aspect = m_scene->gm.yplay > 0 ? (float)m_scene->gm.xplay / (float)m_scene->gm.yplay : 16.0f / 9.0f;
	bool any = false;
	for (Object *ob : m_objects) {
		if (ob->type != OB_CAMERA || !ob->data) {
			continue;
		}
		Camera *cam = (Camera *)ob->data;
		const bool active = ob == m_scene->camera;
		const int el = Add("camera", name_of(ob), ob,
		                   fmt("%s, lens %.0f mm, clipping %.2f to %.0f m%s", cam->type == CAM_ORTHO ? "orthographic" : "perspective",
		                       cam->lens, cam->clipsta, cam->clipend, active ? ", the active camera" : "").c_str());
		if (active) {
			elements[el].flag |= GV_EL_ACTIVE;
			any = true;
		}
		/* the view cone (drawn to at most 60 m) */
		const int s = Shape(GV_SHAPE_FRUSTUM, GV_LAYER_CAMERAS, el);
		const float tanx = (cam->sensor_x * 0.5f) / max_ff(cam->lens, 1e-3f);
		shapes[s].p[0] = cam->clipsta;
		shapes[s].p[1] = min_ff(cam->clipend, 60.0f);
		shapes[s].p[2] = atanf(tanx);
		shapes[s].p[3] = atanf(tanx / aspect);
		shapes[s].p[4] = cam->type == CAM_ORTHO ? cam->ortho_scale : 0.0f;
		shapes[s].state = active ? GV_STATE_GREEN : GV_STATE_WHITE;

		if (!active && !made_active.count(ob) && InGame(ob)) {
			Warn(el, "CAM_NEVER_ACTIVE", GV_WARN_BLACK, "nothing makes it the active camera (a script may)");
		}
		if (cam->clipsta < 0.01f || cam->clipend / max_ff(cam->clipsta, 1e-6f) > 100000.0f) {
			Warn(el, "CAM_NEAR", GV_WARN_NOTE,
			     fmt("near clipping %.3f m with far %.0f m: distant surfaces will flicker", cam->clipsta,
			         cam->clipend).c_str());
		}
		if (any || active) {
			float far_needed = 0.0f;
			for (int c = 0; c < 8; c++) {
				const float co[3] = {(c & 1) ? hi[0] : lo[0], (c & 2) ? hi[1] : lo[1], (c & 4) ? hi[2] : lo[2]};
				far_needed = max_ff(far_needed, len_v3v3(co, ob->obmat[3]));
			}
			if (active && cam->type != CAM_ORTHO && far_needed > cam->clipend) {
				Warn(el, "CAM_FAR_SHORT", GV_WARN_NOTE,
				     fmt("far clipping %.0f m, the scene reaches %.0f m: its far part is cut off", cam->clipend,
				         far_needed).c_str());
			}
		}
		for (Object *other : m_objects) {
			if (other == ob || other->type != OB_MESH || !InGame(other) || other == ob->parent) {
				continue;
			}
			float a[3], b[3];
			ob_world_bounds(other, a, b);
			const float *p = ob->obmat[3];
			if (p[0] > a[0] && p[0] < b[0] && p[1] > a[1] && p[1] < b[1] && p[2] > a[2] && p[2] < b[2] &&
			    min_fff(b[0] - a[0], b[1] - a[1], b[2] - a[2]) > 0.05f)
			{
				Warn(el, "CAM_INSIDE", GV_WARN_NOTE, fmt("the camera is inside %s", name_of(other)).c_str());
				break;
			}
		}
	}
	if (!m_scene->camera) {
		Warn(0, "CAM_NONE", GV_WARN_RED, "the scene has no active camera: the player has nothing to show it with",
		     true);
		m_warn[0].back().layer = GV_LAYER_CAMERAS;
	}
}

/* ------------------------------------------------------------------------- */
/* Entity: Interface (the player's screen; overlay scenes' HUDs) */

void GVBuilder::Interface()
{
	Object *camob = m_scene->camera;
	if (!camob) {
		return;
	}
	const float aspect = m_scene->gm.yplay > 0 ? (float)m_scene->gm.xplay / (float)m_scene->gm.yplay : 16.0f / 9.0f;
	const float dist = 2.5f, halfh = 0.55f, halfw = halfh * aspect;
	float fwd[3], right[3], up[3];
	negate_v3_v3(fwd, camob->obmat[2]);
	normalize_v3(fwd);
	normalize_v3_v3(right, camob->obmat[0]);
	normalize_v3_v3(up, camob->obmat[1]);

	const int panel = Add("interface:SCREEN", "screen", camob,
	                      fmt("the player's screen, %d x %d", m_scene->gm.xplay, m_scene->gm.yplay).c_str());
	GVElement &pe = elements[panel];
	pe.flag |= GV_EL_PLACED;
	madd_v3_v3v3fl(pe.offset, pe.offset, fwd, dist);
	madd_v3_v3fl(pe.offset, up, halfh + 0.18f);
	const int s = Shape(GV_SHAPE_PANEL, GV_LAYER_INTERFACE, panel);
	shapes[s].p[0] = halfw;
	shapes[s].p[1] = halfh;
	shapes[s].p[2] = dist;

	const bool cursor = (m_scene->gm.flag & GAME_SHOW_MOUSE) != 0;
	const int cur = Add("interface:CURSOR", "mouse cursor", camob, cursor ? "the mouse cursor is shown" :
	                    "the mouse cursor is hidden");
	elements[cur].flag |= GV_EL_PLACED;
	madd_v3_v3v3fl(elements[cur].offset, elements[cur].offset, fwd, dist - 0.01f);
	madd_v3_v3fl(elements[cur].offset, right, halfw * 0.85f);
	madd_v3_v3fl(elements[cur].offset, up, -halfh * 0.8f);
	if (!cursor) {
		elements[cur].state = GV_STATE_WHITE;
	}

	/* overlay scenes: what is on them, placed on the panel */
	std::set<Scene *> overlays;
	for (Object *ob : m_objects) {
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type == ACT_SCENE) {
				bSceneActuator *sa = (bSceneActuator *)a->data;
				if (sa && sa->type == ACT_SCENE_ADD_FRONT && sa->scene) {
					overlays.insert(sa->scene);
				}
			}
		}
	}
	for (Scene *ov : overlays) {
		Object *ovcam = ov->camera;
		if (!ovcam) {
			Warn(panel, "UI_NO_CAMERA", GV_WARN_BLACK,
			     fmt("the overlay scene %s has no camera of its own", ov->id.name + 2).c_str());
			continue;
		}
		struct Item {
			int el;
			float ndc[2];
			float half[2];
		};
		std::vector<Item> items;
		for (Base *base = (Base *)ov->base.first; base; base = base->next) {
			Object *ob = base->object;
			if ((ob->lay & ov->lay) == 0 || !ELEM(ob->type, OB_MESH, OB_FONT)) {
				continue;
			}
			float ndc[2], depth;
			if (!camera_project(ovcam, aspect, ob->obmat[3], ndc, &depth)) {
				continue;
			}
			const bool text = ob->type == OB_FONT;
			std::string info = fmt("in the overlay scene %s", ov->id.name + 2);
			const int el = Add(text ? "interface:TEXT" : "interface:ITEM", name_of(ob), camob, info.c_str());
			GVElement &e = elements[el];
			e.flag |= GV_EL_PLACED;
			copy_v3_v3(e.offset, pe.offset);
			madd_v3_v3fl(e.offset, right, std::min(std::max(ndc[0], -1.2f), 1.2f) * halfw);
			madd_v3_v3fl(e.offset, up, std::min(std::max(ndc[1], -1.2f), 1.2f) * halfh);
			madd_v3_v3fl(e.offset, fwd, -0.02f);
			/* its size on the screen */
			float dims[3];
			BKE_object_dimensions_get(ob, dims);
			Camera *oc = (Camera *)ovcam->data;
			const float scale = oc->type == CAM_ORTHO ? oc->ortho_scale * 0.5f :
			                    max_ff(depth, 1e-3f) * (oc->sensor_x * 0.5f) / max_ff(oc->lens, 1e-3f);
			Item it = {el, {ndc[0], ndc[1]}, {dims[0] * 0.5f / scale, dims[1] * 0.5f / scale * aspect}};
			items.push_back(it);
			/* off the screen at 16:9 or 4:3 */
			const float ratios[2] = {16.0f / 9.0f, 4.0f / 3.0f};
			const char *names[2] = {"16:9", "4:3"};
			for (int r = 0; r < 2; r++) {
				float n2[2], d2;
				if (camera_project(ovcam, ratios[r], ob->obmat[3], n2, &d2) && (fabsf(n2[0]) > 1.0f || fabsf(n2[1]) > 1.0f)) {
					Warn(el, "UI_OFFSCREEN", GV_WARN_NOTE, fmt("off the screen at %s", names[r]).c_str());
					break;
				}
			}
			if (text) {
				Curve *cu = (Curve *)ob->data;
				if (cu && cu->vfont && !file_present(m_main, &cu->vfont->id, cu->vfont->name, cu->vfont->packedfile != NULL)) {
					Warn(el, "UI_FONT_MISSING", GV_WARN_RED, fmt("its font file is missing (%s)", cu->vfont->name).c_str(),
					     true);
				}
				/* text height on the target screen */
				const float pixels = dims[1] / scale * 0.5f * (float)max_ii(m_scene->gm.yplay, 1);
				if (pixels > 0.0f && pixels < 10.0f) {
					Warn(el, "UI_TEXT_SMALL", GV_WARN_NOTE,
					     fmt("about %.0f pixels high at %d x %d: hard to read", pixels, m_scene->gm.xplay,
					         m_scene->gm.yplay).c_str());
				}
			}
		}
		for (size_t i = 0; i < items.size(); i++) {
			for (size_t j = i + 1; j < items.size(); j++) {
				const Item &a = items[i], &b = items[j];
				if (fabsf(a.ndc[0] - b.ndc[0]) < a.half[0] + b.half[0] && fabsf(a.ndc[1] - b.ndc[1]) < a.half[1] + b.half[1]) {
					Warn(a.el, "UI_OVERLAP", GV_WARN_NOTE, fmt("covers %s", elements[b.el].name).c_str());
				}
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Factor: Rendering */

void GVBuilder::Rendering()
{
	int lamps = 0;
	double texture_bytes = 0.0;
	std::set<Image *> images;
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) {
			continue;
		}
		if (ob->type == OB_MESH) {
			for (short i = 1; i <= ob->totcol; i++) {
				Material *ma = give_current_material(ob, i);
				if (!ma) {
					continue;
				}
				const int el = Add("material", ma->id.name + 2, ob, ma->use_nodes ? "node material" : "Blender Internal");
				/* P8: a material whose GLSL shader could not be built */
				if (m_conv.glsl_fallback.count(ma->id.name + 2)) {
					Warn(el, "REN_GLSL_FALLBACK", GV_WARN_NOTE,
					     "its GLSL shader could not be built: drawn without it (fixed-function), looks simpler");
				}
				for (int t = 0; t < MAX_MTEX; t++) {
					MTex *mtex = ma->mtex[t];
					if (!mtex || !mtex->tex || mtex->tex->type != TEX_IMAGE || !mtex->tex->ima) {
						continue;
					}
					Image *ima = mtex->tex->ima;
					if (ima->source == IMA_SRC_MOVIE) {
						continue;  /* Media */
					}
					if (ima->source == IMA_SRC_FILE &&
					    !file_present(m_main, &ima->id, ima->name, BKE_image_has_packedfile(ima)))
					{
						Warn(el, "REN_IMAGE_MISSING", GV_WARN_RED,
						     fmt("image %s: the file is missing (%s)", ima->id.name + 2, ima->name).c_str(), true);
					}
					if (images.insert(ima).second && BKE_image_has_ibuf(ima, NULL)) {
						ImBuf *ibuf = BKE_image_acquire_ibuf(ima, NULL, NULL);
						if (ibuf) {
							texture_bytes += (double)ibuf->x * ibuf->y * 4.0 * 1.33;
						}
						BKE_image_release_ibuf(ima, ibuf, NULL);
					}
				}
			}
		}
		if (ob->type == OB_LAMP && ob->data) {
			Lamp *la = (Lamp *)ob->data;
			const int el = Add(fmt("lamp:%s", lamp_id(la->type)).c_str(), name_of(ob), ob,
			                   fmt("energy %.2f, reach %.1f m%s", la->energy, la->dist,
			                       (la->mode & LA_SHAD_BUF) ? fmt(", shadow %d px", la->bufsize).c_str() : "").c_str());
			if (InGame(ob)) {
				lamps++;
			}
			if (la->type == LA_LOCAL) {
				const int s = Shape(GV_SHAPE_SPHERE, GV_LAYER_RENDERING, el);
				shapes[s].p[0] = la->dist;
				shapes[s].dashed = 1;
			}
			else if (la->type == LA_SPOT) {
				const int s = Shape(GV_SHAPE_CONE, GV_LAYER_RENDERING, el);
				shapes[s].p[0] = la->dist;
				shapes[s].p[1] = la->spotsize * 0.5f;
				shapes[s].dashed = (la->mode & LA_SHAD_BUF) ? 0 : 1;
			}
			else {
				const int s = Shape(GV_SHAPE_ARROW, GV_LAYER_RENDERING, el);
				shapes[s].p[0] = 2.0f;
			}
			if ((la->mode & LA_SHAD_BUF) && la->bufsize > m_targets.shadow_size) {
				Warn(el, "REN_SHADOW_SIZE", GV_WARN_NOTE,
				     fmt("shadow map %d px: more than the target's %d", la->bufsize, m_targets.shadow_size).c_str(), false,
				     true);
			}
		}
		/* levels of detail: a ring at each switch distance */
		int level = 0;
		for (LodLevel *lod = (LodLevel *)ob->lodlevels.first; lod; lod = lod->next, level++) {
			if (level == 0) {
				continue;
			}
			const int el = Add("lod", fmt("level %d", level).c_str(), ob,
			                   fmt("%s from %.1f m", lod->source ? name_of(lod->source) : "?", lod->distance).c_str());
			const int s = Shape(GV_SHAPE_RING, GV_LAYER_RENDERING, el);
			shapes[s].p[0] = lod->distance;
			shapes[s].dashed = 1;
		}
		/* 2D filters (also a Remix warning, Remix()) */
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type == ACT_2DFILTER) {
				Add("filter2d", a->name, ob, "a 2D filter on the whole picture");
			}
		}
	}
	if (m_scene->world) {
		World *wo = m_scene->world;
		const int el = Add("world", wo->id.name + 2, NULL,
		                   fmt("horizon %.2f %.2f %.2f, ambient %.2f %.2f %.2f", wo->horr, wo->horg, wo->horb, wo->ambr,
		                       wo->ambg, wo->ambb).c_str());
		(void)el;
		if ((wo->mode & WO_MIST) && m_camera && m_objectElement.count(name_of(m_camera))) {
			const int mist = Add("mist", "mist", NULL, fmt("from %.1f m, full at %.1f m", wo->miststa,
			                                                 wo->miststa + wo->mistdist).c_str());
			const int cam = m_objectElement[name_of(m_camera)];
			int s = Shape(GV_SHAPE_SPHERE, GV_LAYER_RENDERING, cam);
			shapes[s].p[0] = wo->miststa;
			shapes[s].dashed = 1;
			s = Shape(GV_SHAPE_SPHERE, GV_LAYER_RENDERING, cam);
			shapes[s].p[0] = wo->miststa + wo->mistdist;
			(void)mist;
		}
	}
	if (lamps > m_targets.lights) {
		Warn(0, "REN_LIGHTS", GV_WARN_NOTE,
		     fmt("%d lamps: more than the target's %d (GLSL lights every material with every lamp)", lamps,
		         m_targets.lights).c_str(), false, true);
		m_warn[0].back().layer = GV_LAYER_RENDERING;
	}
	if (texture_bytes / (1024.0 * 1024.0) > m_targets.texture_mb) {
		Warn(0, "REN_TEXTURE_MEMORY", GV_WARN_NOTE,
		     fmt("about %.0f MB of textures: more than the target's %.0f MB", texture_bytes / (1024.0 * 1024.0),
		         m_targets.texture_mb).c_str(), false, true);
		m_warn[0].back().layer = GV_LAYER_RENDERING;
	}
}

/* ------------------------------------------------------------------------- */
/* Factor: Physics */

void GVBuilder::Physics()
{
	static const char *engines[] = {"Bullet", "PhysX (CPU)", "PhysX (GPU)"};
	Add(m_physx < 0 ? "engine:NONE" : (m_physx == 0 ? "engine:BULLET" : "engine:PHYSX"),
	    m_physx < 0 ? "no physics" : engines[m_physx], NULL, "the scene's physics engine (World > Physics)");
	int bodies = 0;
	std::map<std::string, int> body_el;
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) {
			continue;
		}
		const int t = physics_type(ob);
		if (t == OB_BODY_TYPE_NO_COLLISION || t == OB_BODY_TYPE_NAVMESH) {
			continue;
		}
		const int b = bounds_type(ob, t);
		std::string info = fmt("%s, %s bounds", physics_name(t), bounds_name(b));
		if (moving(t)) {
			info += fmt(", mass %.2f", ob->mass);
		}
		if (ob->gameflag & OB_GHOST) info += ", Ghost";
		if (ob->gameflag & OB_ACTOR) info += ", Actor";
		const int el = Add(fmt("body:%s", physics_id(t)).c_str(), name_of(ob), ob, info.c_str());
		body_el[name_of(ob)] = el;
		if (moving(t) && InGame(ob)) {
			bodies++;
		}
		/* the collision shape */
		BoundBox *bb = ob->type == OB_MESH ? BKE_object_boundbox_get(ob) : NULL;
		float half[3] = {0.5f, 0.5f, 0.5f}, mid[3] = {0, 0, 0};
		if (bb) {
			for (int k = 0; k < 3; k++) {
				half[k] = (bb->vec[6][k] - bb->vec[0][k]) * 0.5f;
				mid[k] = (bb->vec[6][k] + bb->vec[0][k]) * 0.5f;
			}
		}
		float scale[3];
		mat4_to_size(scale, ob->obmat);
		if (b == OB_BOUND_SPHERE) {
			const int s = Shape(GV_SHAPE_SPHERE, GV_LAYER_PHYSICS, el);
			shapes[s].p[0] = (ob->gameflag & OB_BOUNDS) ? max_fff(half[0] * scale[0], half[1] * scale[1], half[2] * scale[2])
			                                            : ob->inertia * max_fff(scale[0], scale[1], scale[2]);
		}
		else if (b == OB_BOUND_CAPSULE || t == OB_BODY_TYPE_CHARACTER) {
			const int s = Shape(GV_SHAPE_CAPSULE, GV_LAYER_PHYSICS, el);
			shapes[s].p[0] = max_ff(half[0] * scale[0], half[1] * scale[1]);
			shapes[s].p[1] = 2.0f * half[2] * scale[2];
		}
		else {
			const int s = Shape(GV_SHAPE_BOX, GV_LAYER_PHYSICS, el);
			copy_v3_v3(shapes[s].p, half);
			copy_v3_v3(shapes[s].p + 3, mid);
			/* hulls and meshes are drawn as their box, dashed */
			shapes[s].dashed = ELEM(b, OB_BOUND_CONVEX_HULL, OB_BOUND_TRIANGLE_MESH) ? 1 : 0;
		}
		if (t == OB_BODY_TYPE_SOFT && m_physx == 1) {
			Warn(el, "PHY_SOFT_CPU", GV_WARN_RED, "soft bodies under PhysX need the GPU (PhysX GPU in World > Physics)",
			     true);
		}
		if (moving(t) && b == OB_BOUND_TRIANGLE_MESH && m_physx > 0) {
			Warn(el, "PHY_TRI_MOVING", GV_WARN_NOTE,
			     "a moving object with Triangle Mesh bounds collides as its convex hull under PhysX", true);
		}
		if ((ob->gameflag & OB_GHOST) && b == OB_BOUND_TRIANGLE_MESH && m_physx > 0) {
			Warn(el, "PHY_GHOST_MESH", GV_WARN_NOTE,
			     "a Ghost with Triangle Mesh bounds senses with its convex hull under PhysX", true);
		}
		if (scale[0] < 0.0f || scale[1] < 0.0f || scale[2] < 0.0f || is_negative_m4(ob->obmat)) {
			Warn(el, "PHY_NEG_SCALE", GV_WARN_NOTE, "negative scale: it collides as if mirrored back");
		}
		if (ELEM(b, OB_BOUND_CONVEX_HULL, OB_BOUND_TRIANGLE_MESH) && bb) {
			const float thin = min_fff(2.0f * half[0] * fabsf(scale[0]), 2.0f * half[1] * fabsf(scale[1]),
			                           2.0f * half[2] * fabsf(scale[2]));
			if (thin > 0.0f && thin < 2.0f * ob->margin && b == OB_BOUND_CONVEX_HULL) {
				Warn(el, "PHY_THIN", GV_WARN_NOTE,
				     fmt("%.3f m thin: less than twice its collision margin (%.2f m)", thin, ob->margin).c_str());
			}
		}
	}
	/* joints (Rigid Body Joint constraints) */
	for (Object *ob : m_objects) {
		for (bConstraint *con = (bConstraint *)ob->constraints.first; con; con = con->next) {
			if (con->type != CONSTRAINT_TYPE_RIGIDBODYJOINT || !con->data) {
				continue;
			}
			bRigidBodyJointConstraint *dat = (bRigidBodyJointConstraint *)con->data;
			const char *jid = joint_id(dat->type);
			const int el = Add(jid ? fmt("joint:%s", jid).c_str() : "joint:OTHER", con->name, ob,
			                   dat->tar ? fmt("to %s", name_of(dat->tar)).c_str() : "to the world");
			int s = Shape(GV_SHAPE_SPHERE, GV_LAYER_PHYSICS, el);
			shapes[s].p[0] = 0.08f;
			shapes[s].p[1] = dat->pivX;
			shapes[s].p[2] = dat->pivY;
			shapes[s].p[3] = dat->pivZ;
			if (dat->tar && m_objectElement.count(name_of(dat->tar)) && m_objectElement.count(name_of(ob))) {
				s = Shape(GV_SHAPE_LINE, GV_LAYER_PHYSICS, m_objectElement[name_of(ob)], m_objectElement[name_of(dat->tar)]);
				shapes[s].p[6] = 1.0f;
			}
			if (physics_type(ob) == OB_BODY_TYPE_NO_COLLISION ||
			    (dat->tar && physics_type(dat->tar) == OB_BODY_TYPE_NO_COLLISION))
			{
				Warn(el, "PHY_JOINT_NO_PHYSICS", GV_WARN_RED,
				     fmt("joins %s, which has no physics: Blender 2.79 does not make this joint",
				         physics_type(ob) == OB_BODY_TYPE_NO_COLLISION ? name_of(ob) : name_of(dat->tar)).c_str(), true);
			}
			if (dat->tar && moving(physics_type(ob)) && moving(physics_type(dat->tar))) {
				const float a = max_ff(ob->mass, 1e-6f), b = max_ff(dat->tar->mass, 1e-6f);
				if (a / b > 10.0f || b / a > 10.0f) {
					Warn(el, "PHY_MASS_RATIO", GV_WARN_NOTE,
					     fmt("masses %.2f and %.2f: a heavy body on a light one makes joints unstable", ob->mass,
					         dat->tar->mass).c_str());
				}
			}
		}
	}
	if (bodies > m_targets.bodies) {
		Warn(0, "PHY_BODIES", GV_WARN_NOTE,
		     fmt("%d moving bodies: more than the target's %d", bodies, m_targets.bodies).c_str(), false, true);
		m_warn[0].back().layer = GV_LAYER_PHYSICS;
	}
}

/* ------------------------------------------------------------------------- */
/* Factor: Media (sound and video) */

void GVBuilder::Media()
{
	int sounds = 0;
	if (m_camera) {
		static const char *models[] = {"none", "inverse", "inverse clamped", "linear", "linear clamped",
		                               "exponent", "exponent clamped"};
		const int dm = m_scene->audio.distance_model;
		const int el = Add("listener", "listener", m_camera,
		                   fmt("Doppler %.2f, speed of sound %.0f m/s, distance model %s", m_scene->audio.doppler_factor,
		                       m_scene->audio.speed_of_sound, (dm >= 0 && dm <= 6) ? models[dm] : "?").c_str());
		(void)el;
	}
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) {
			continue;
		}
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type != ACT_SOUND || !a->data) {
				continue;
			}
			bSoundActuator *sa = (bSoundActuator *)a->data;
			const bool three_d = (sa->flag & ACT_SND_3D_SOUND) != 0;
			bSound *snd = sa->sound;
			std::string info = snd ? fmt("%s, %s, volume %.2f", snd->id.name + 2, three_d ? "3D" : "no position",
			                             sa->volume) : std::string("no sound chosen");
			Object *owner = (three_d || !m_camera) ? ob : m_camera;
			const int el = Add("sound", a->name, owner, info.c_str());
			sounds++;
			if (!snd) {
				Warn(el, "MED_NO_SOUND", GV_WARN_BLACK, "no sound chosen");
				continue;
			}
			if (!file_present(m_main, &snd->id, snd->name, snd->packedfile != NULL)) {
				Warn(el, "MED_SOUND_MISSING", GV_WARN_RED, fmt("the sound file is missing (%s)", snd->name).c_str(), true);
			}
			if (three_d) {
				int s = Shape(GV_SHAPE_SPHERE, GV_LAYER_MEDIA, el);
				shapes[s].p[0] = sa->sound3D.reference_distance;
				s = Shape(GV_SHAPE_SPHERE, GV_LAYER_MEDIA, el);
				shapes[s].p[0] = min_ff(sa->sound3D.max_distance, 1000.0f);
				shapes[s].dashed = 1;
				if (sa->sound3D.cone_outer_angle < 360.0f) {
					s = Shape(GV_SHAPE_CONE, GV_LAYER_MEDIA, el);
					shapes[s].p[0] = min_ff(sa->sound3D.max_distance, 20.0f);
					shapes[s].p[1] = DEG2RADF(sa->sound3D.cone_outer_angle * 0.5f);
				}
#ifdef WITH_AUDASPACE
				if (snd->playback_handle) {
					AUD_SoundInfo si = AUD_getInfo((AUD_Sound *)snd->playback_handle);
					if ((int)si.specs.channels > 1) {
						Warn(el, "MED_STEREO_3D", GV_WARN_NOTE,
						     "a stereo sound set to 3D is not placed in 3D (only mono sounds are)", true);
					}
				}
#endif
			}
		}
		/* movie textures */
		if (ob->type == OB_MESH) {
			std::set<Image *> seen;
			for (short i = 1; i <= ob->totcol; i++) {
				Material *ma = give_current_material(ob, i);
				if (!ma) continue;
				for (int t = 0; t < MAX_MTEX; t++) {
					MTex *mtex = ma->mtex[t];
					if (!mtex || !mtex->tex || mtex->tex->type != TEX_IMAGE || !mtex->tex->ima ||
					    mtex->tex->ima->source != IMA_SRC_MOVIE || !seen.insert(mtex->tex->ima).second)
					{
						continue;
					}
					Image *ima = mtex->tex->ima;
					const int el = Add("video", ima->id.name + 2, ob,
					                   "a movie texture: the game shows its first frame unless a script plays it (bge.texture)");
					if (!file_present(m_main, &ima->id, ima->name, BKE_image_has_packedfile(ima))) {
						Warn(el, "MED_VIDEO_MISSING", GV_WARN_RED, fmt("the video file is missing (%s)", ima->name).c_str(),
						     true);
					}
				}
			}
		}
	}
	if (sounds > m_targets.sounds) {
		Warn(0, "MED_SOUNDS", GV_WARN_NOTE,
		     fmt("%d sounds: more than the target's %d can play at once", sounds, m_targets.sounds).c_str(), false, true);
		m_warn[0].back().layer = GV_LAYER_MEDIA;
	}
}

/* ------------------------------------------------------------------------- */
/* Behaviour: Logic (every brick, its links) */

void GVBuilder::Logic()
{
	std::map<void *, int> brick;   /* brick pointer -> element */
	std::set<void *> linked_actuators;
	for (Object *ob : m_objects) {
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			for (int i = 0; i < c->totlinks; i++) {
				if (c->links[i]) linked_actuators.insert(c->links[i]);
			}
		}
	}
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) {
			continue;
		}
		const std::string obname = name_of(ob);
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) {
			const char *id = sensor_id(s->type);
			std::string info;
			if (s->type == SENS_KEYBOARD && s->data) {
				bKeyboardSensor *ks = (bKeyboardSensor *)s->data;
				info = ks->type ? std::string("any key") : key_name(ks->key);
			}
			else if (s->type == SENS_MOUSE && s->data) {
				info = mouse_event_name(((bMouseSensor *)s->data)->type);
			}
			else if (s->type == SENS_DELAY && s->data) {
				bDelaySensor *ds = (bDelaySensor *)s->data;
				info = fmt("after %d tics, for %d", ds->delay, ds->duration);
			}
			else if (s->type == SENS_PROPERTY && s->data) {
				info = std::string("watches ") + ((bPropertySensor *)s->data)->name;
			}
			else if (s->type == SENS_MESSAGE && s->data) {
				info = std::string("subject ") + ((bMessageSensor *)s->data)->subject;
			}
			const int el = Add(id ? fmt("sensor:%s", id).c_str() : "sensor:OTHER", s->name, ob, info.c_str());
			brick[s] = el;
			m_brickElement[obname + "/" + s->name] = el;
			if (s->flag & SENS_DEACTIVATE) {
				elements[el].state = GV_STATE_WHITE;
				BLI_strncpy(elements[el].info, "deactivated", sizeof(elements[el].info));
			}
			else if (s->totlinks == 0) {
				Warn(el, "LOG_UNCONNECTED", GV_WARN_BLACK, "not linked to a controller");
			}
			if (s->type == SENS_NEAR && s->data) {
				const int sh = Shape(GV_SHAPE_SPHERE, GV_LAYER_LOGIC, el);
				shapes[sh].p[0] = ((bNearSensor *)s->data)->dist;
				shapes[sh].dashed = 1;
			}
			else if (s->type == SENS_RADAR && s->data) {
				const int sh = Shape(GV_SHAPE_CONE, GV_LAYER_LOGIC, el);
				shapes[sh].p[0] = ((bRadarSensor *)s->data)->range;
				shapes[sh].p[1] = DEG2RADF(((bRadarSensor *)s->data)->angle * 0.5f);
				shapes[sh].dashed = 1;
			}
			if (m_conv.converted && !(s->flag & SENS_DEACTIVATE) && !m_conv.bricks.count(obname + "/" + s->name)) {
				Warn(el, "LOG_NOT_MADE", GV_WARN_BLACK, "the converter made nothing for it");
			}
		}
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			const char *id = controller_id(c->type);
			std::string info = fmt("states %s", "");
			std::string states;
			for (int b = 0; b < 30; b++) {
				if (c->state_mask & (1u << b)) {
					states += fmt("%s%d", states.empty() ? "" : ",", b + 1);
				}
			}
			info = std::string("state ") + states;
			const int el = Add(id ? fmt("controller:%s", id).c_str() : "controller:OTHER", c->name, ob, info.c_str());
			brick[c] = el;
			m_brickElement[obname + "/" + c->name] = el;
			if (c->flag & CONT_DEACTIVATE) {
				elements[el].state = GV_STATE_WHITE;
				BLI_strncpy(elements[el].info, "deactivated", sizeof(elements[el].info));
				continue;
			}
			if (SensorsOf(c).empty()) {
				Warn(el, "LOG_UNCONNECTED", GV_WARN_BLACK, "no sensor is linked to it");
			}
			if (c->totlinks == 0 && c->type != CONT_PYTHON) {
				Warn(el, "LOG_UNCONNECTED", GV_WARN_BLACK, "no actuator is linked to it");
			}
			/* states the object never enters */
			if (!(c->state_mask & ob->init_state)) {
				bool entered = false;
				for (bActuator *a = (bActuator *)ob->actuators.first; a && !entered; a = a->next) {
					if (a->type == ACT_STATE && a->data) {
						bStateActuator *st = (bStateActuator *)a->data;
						if (st->type != ACT_STATE_REMOVE && (st->mask & c->state_mask)) {
							entered = true;
						}
					}
				}
				if (!entered) {
					Warn(el, "LOG_STATE_NEVER", GV_WARN_NOTE,
					     fmt("in state %s, which the object never enters (no State actuator sets it)",
					         states.c_str()).c_str());
				}
			}
			if (m_conv.converted && !m_conv.bricks.count(obname + "/" + c->name)) {
				Warn(el, "LOG_NOT_MADE", GV_WARN_BLACK, "the converter made nothing for it");
			}
		}
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			const char *id = actuator_id(a->type);
			const int el = Add(id ? fmt("actuator:%s", id).c_str() : "actuator:OTHER", a->name, ob, "");
			brick[a] = el;
			m_brickElement[obname + "/" + a->name] = el;
			if (a->flag & ACT_DEACTIVATE) {
				elements[el].state = GV_STATE_WHITE;
				BLI_strncpy(elements[el].info, "deactivated", sizeof(elements[el].info));
				continue;
			}
			if (!linked_actuators.count(a)) {
				Warn(el, "LOG_UNCONNECTED", GV_WARN_BLACK, "no controller is linked to it");
			}
			/* actuators aimed at nothing */
			const char *missing = NULL;
			switch (a->type) {
				case ACT_EDIT_OBJECT: {
					bEditObjectActuator *ea = (bEditObjectActuator *)a->data;
					if (ea && (ea->type == ACT_EDOB_ADD_OBJECT || ea->type == ACT_EDOB_TRACK_TO) && !ea->ob) {
						missing = "no object chosen";
					}
					if (ea && ea->type == ACT_EDOB_REPLACE_MESH && !ea->me) {
						missing = "no mesh chosen";
					}
					if (ea) {
						static const char *modes[] = {"Add Object", "End Object", "Replace Mesh", "Track To", "Dynamics"};
						BLI_strncpy(elements[el].info, (ea->type >= 0 && ea->type <= 4) ? modes[ea->type] : "",
						            sizeof(elements[el].info));
					}
					break;
				}
				case ACT_CAMERA:
					if (a->data && !((bCameraActuator *)a->data)->ob) missing = "no object to follow";
					break;
				case ACT_PARENT: {
					bParentActuator *pa = (bParentActuator *)a->data;
					if (pa && pa->type == ACT_PARENT_SET && !pa->ob) missing = "no parent chosen";
					break;
				}
				case ACT_PROPERTY: {
					bPropertyActuator *pa = (bPropertyActuator *)a->data;
					if (pa && pa->type == ACT_PROP_COPY && !pa->ob) missing = "no object to copy from";
					break;
				}
				default:
					break;
			}
			if (missing) {
				Warn(el, "LOG_NO_TARGET", GV_WARN_BLACK, missing);
			}
			if (m_conv.converted && !m_conv.bricks.count(obname + "/" + a->name)) {
				Warn(el, "LOG_NOT_MADE", GV_WARN_BLACK, "the converter made nothing for it");
			}
		}
		/* the object's states */
		if (m_objectElement.count(obname) && ob->controllers.first) {
			GVElement &oe = elements[m_objectElement[obname]];
			std::string init;
			for (int b = 0; b < 30; b++) {
				if (ob->init_state & (1u << b)) init += fmt("%s%d", init.empty() ? "" : ",", b + 1);
			}
			BLI_strncpy(oe.info + strlen(oe.info), fmt(", starts in state %s", init.c_str()).c_str(), sizeof(oe.info) - strlen(oe.info));
		}
	}
	/* links: sensor -> controller -> actuator (also across objects) */
	for (Object *ob : m_objects) {
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			if (!brick.count(c)) continue;
			for (bSensor *s : SensorsOf(c)) {
				if (brick.count(s)) {
					Shape(GV_SHAPE_LINE, GV_LAYER_LOGIC, brick[s], brick[c]);
				}
			}
			for (int i = 0; i < c->totlinks; i++) {
				if (c->links[i] && brick.count(c->links[i])) {
					Shape(GV_SHAPE_LINE, GV_LAYER_LOGIC, brick[c], brick[c->links[i]]);
				}
			}
			/* every frame into a script */
			if (c->type == CONT_PYTHON) {
				for (bSensor *s : SensorsOf(c)) {
					if (brick_every_frame(s)) {
						Warn(brick[c], "LOG_EVERY_FRAME", GV_WARN_NOTE,
						     fmt("%s fires it every frame: its script runs every frame", s->name).c_str());
						break;
					}
				}
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Factor: Data (game properties, globalDict) */

void GVBuilder::Data()
{
	int saves = 0, loads = 0;
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) {
			continue;
		}
		const std::string obname = name_of(ob);
		/* what the object's bricks read and write */
		std::map<std::string, std::string> reads, writes;  /* property -> brick */
		std::string expressions;
		auto has_prop = [&](const char *name) {
			for (bProperty *p = (bProperty *)ob->prop.first; p; p = p->next) {
				if (STREQ(p->name, name)) return true;
			}
			return false;
		};
		auto need = [&](const char *prop, const char *brickname, bool write) {
			if (!prop || !prop[0]) return;
			(write ? writes : reads)[prop] = brickname;
			if (!has_prop(prop)) {
				auto it = m_brickElement.find(obname + "/" + brickname);
				if (it != m_brickElement.end()) {
					Warn(it->second, "DAT_PROP_MISSING", GV_WARN_BLACK,
					     fmt("uses the property \"%s\", which %s does not have", prop, obname.c_str()).c_str());
					m_warn[it->second].back().layer = GV_LAYER_DATA;
				}
			}
		};
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) {
			if (s->type == SENS_PROPERTY && s->data) need(((bPropertySensor *)s->data)->name, s->name, false);
			if (s->type == SENS_KEYBOARD && s->data) {
				bKeyboardSensor *ks = (bKeyboardSensor *)s->data;
				need(ks->targetName, s->name, true);
				need(ks->toggleName, s->name, true);
			}
		}
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			if (c->type == CONT_EXPRESSION && c->data) {
				expressions += ((bExpressionCont *)c->data)->str;
				expressions += " ";
			}
		}
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type == ACT_PROPERTY && a->data) need(((bPropertyActuator *)a->data)->name, a->name, true);
			if (a->type == ACT_RANDOM && a->data) need(((bRandomActuator *)a->data)->propname, a->name, true);
			if (a->type == ACT_ACTION && a->data) {
				bActionActuator *aa = (bActionActuator *)a->data;
				if (aa->type == ACT_ACTION_FROM_PROP) need(aa->name, a->name, false);
				need(aa->frameProp, a->name, true);
			}
			if (a->type == ACT_MESSAGE && a->data) {
				bMessageActuator *ma = (bMessageActuator *)a->data;
				if (ma->bodyType == ACT_MESG_PROP) need(ma->body, a->name, false);
			}
			if (a->type == ACT_GAME && a->data) {
				bGameActuator *ga = (bGameActuator *)a->data;
				if (ga->type == ACT_GAME_SAVECFG) saves++;
				if (ga->type == ACT_GAME_LOADCFG) loads++;
			}
		}
		for (bProperty *p = (bProperty *)ob->prop.first; p; p = p->next) {
			const char *id = property_id(p->type);
			std::string value;
			switch (p->type) {
				case GPROP_BOOL: value = p->data ? "True" : "False"; break;
				case GPROP_INT: value = fmt("%d", p->data); break;
				case GPROP_FLOAT:
				case GPROP_TIME: {
					float f;
					memcpy(&f, &p->data, sizeof(f));
					value = fmt("%g", f);
					break;
				}
				case GPROP_STRING: value = p->poin ? fmt("\"%s\"", (char *)p->poin) : "\"\""; break;
				default: break;
			}
			const int el = Add(id ? fmt("property:%s", id).c_str() : "property:OTHER", p->name, ob,
			                   fmt("= %s", value.c_str()).c_str());
			const bool in_expr = expressions.find(p->name) != std::string::npos;
			if (reads.count(p->name) && !writes.count(p->name) && p->type != GPROP_TIME) {
				Warn(el, "DAT_READ_NEVER_SET", GV_WARN_NOTE,
				     fmt("read by %s, but nothing in the logic sets it (a script may)", reads[p->name].c_str()).c_str());
			}
			if (writes.count(p->name) && !reads.count(p->name) && !in_expr) {
				Warn(el, "DAT_SET_NEVER_READ", GV_WARN_NOTE,
				     fmt("set by %s, but nothing in the logic reads it (a script may)", writes[p->name].c_str()).c_str());
			}
			if (m_conv.converted && InGame(ob) && !m_conv.properties.count(obname + "/" + p->name)) {
				Warn(el, "DAT_NOT_MADE", GV_WARN_BLACK, "the converter made nothing for it");
			}
		}
	}
	if (saves || loads) {
		const int el = Add("globaldict", "globalDict", NULL,
		                   fmt("saved by %d Game actuators, loaded by %d", saves, loads).c_str());
		if (saves && !loads) {
			Warn(el, "DAT_SAVE_NO_LOAD", GV_WARN_NOTE, "saved, but nothing in the logic loads it (a script may)");
		}
		if (loads && !saves) {
			Warn(el, "DAT_LOAD_NO_SAVE", GV_WARN_NOTE, "loaded, but nothing in the logic saves it (a script may)");
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Behaviour: Python (scripts and modules, one node each) */

void GVBuilder::Python()
{
	std::map<std::string, int> node;   /* script or module -> element */
	char blenddir[FILE_MAX] = "";
	BLI_split_dir_part(m_main->name, blenddir, sizeof(blenddir));
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) {
			continue;
		}
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			if (c->type != CONT_PYTHON || !c->data) {
				continue;
			}
			bPythonCont *pc = (bPythonCont *)c->data;
			const bool module = pc->mode == CONT_PY_MODULE;
			const std::string key = module ? std::string("module:") + pc->module :
			                        std::string("script:") + (pc->text ? pc->text->id.name + 2 : "");
			int el;
			auto it = node.find(key);
			if (it != node.end()) {
				el = it->second;
				/* another object using the same script or module: a line to it */
				if (m_objectElement.count(name_of(ob))) {
					const int s = Shape(GV_SHAPE_LINE, GV_LAYER_PYTHON, el, m_objectElement[name_of(ob)]);
					shapes[s].dashed = 1;
				}
				continue;
			}
			if (!module) {
				el = Add("python:SCRIPT", pc->text ? pc->text->id.name + 2 : "(no script)", ob,
				         fmt("run by %s", c->name).c_str());
				node[key] = el;
				if (!pc->text) {
					Warn(el, "PY_NO_SCRIPT", GV_WARN_BLACK, "the Python controller has no script");
					continue;
				}
				const std::string src = text_source(pc->text);
				const std::string err = compile_check(src, pc->text->id.name + 2);
				if (!err.empty()) {
					Warn(el, "PY_SYNTAX", GV_WARN_RED, fmt("does not compile, %s", err.c_str()).c_str());
				}
				const std::string old = old_api(src);
				if (!old.empty()) {
					Warn(el, "PY_OLD_API", GV_WARN_NOTE, fmt("an old 2.4x call: %s", old.c_str()).c_str());
				}
				LibLoads(src, pc->text->id.name + 2, ob, &pc->text->id);
				continue;
			}
			el = Add("python:MODULE", pc->module, ob, fmt("run by %s", c->name).c_str());
			node[key] = el;
			const char *dot = strrchr(pc->module, '.');
			if (!dot || dot == pc->module) {
				Warn(el, "PY_MODULE_FORMAT", GV_WARN_BLACK, "a module controller needs module.function");
				continue;
			}
			const std::string modname(pc->module, dot - pc->module);
			const std::string first = modname.substr(0, modname.find('.'));
			Text *text = (Text *)BLI_findstring(&m_main->text, (first + ".py").c_str(), offsetof(ID, name) + 2);
			char path[FILE_MAX];
			BLI_join_dirfile(path, sizeof(path), blenddir, (first + ".py").c_str());
			if (text) {
				const std::string err = compile_check(text_source(text), text->id.name + 2);
				if (!err.empty()) {
					Warn(el, "PY_SYNTAX", GV_WARN_RED, fmt("%s does not compile, %s", text->id.name + 2, err.c_str()).c_str());
				}
				const std::string old = old_api(text_source(text));
				if (!old.empty()) {
					Warn(el, "PY_OLD_API", GV_WARN_NOTE, fmt("an old 2.4x call: %s", old.c_str()).c_str());
				}
				LibLoads(text_source(text), text->id.name + 2, ob, &text->id);
			}
			else if (!blenddir[0] || !BLI_exists(path)) {
				Warn(el, "PY_MISSING", GV_WARN_RED,
				     fmt("the module %s is missing (no text %s.py in the file, no %s.py beside it)", first.c_str(),
				         first.c_str(), first.c_str()).c_str());
			}
		}
	}
}

/* the files a script loads (LibLoad): Data, Red when missing */
void GVBuilder::LibLoads(const std::string &src, const char *script, Object *ob, ID *id)
{
	for (const std::string &path : libload_paths(src)) {
		if (!m_libloads.insert(path).second) {
			continue;
		}
		const int el = Add("libload", path.c_str(), ob, fmt("loaded by %s (LibLoad)", script).c_str());
		if (!file_present(m_main, id, path.c_str(), false)) {
			Warn(el, "DAT_LIBLOAD_MISSING", GV_WARN_RED, fmt("the file %s is missing", path.c_str()).c_str());
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Behaviour: Animation (actions played by actuators) */

void GVBuilder::Animation()
{
	std::set<bAction *> played;
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) {
			continue;
		}
		std::map<int, std::string> layers;  /* action layer -> actuator */
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (!ELEM(a->type, ACT_ACTION, ACT_SHAPEACTION) || !a->data) {
				continue;
			}
			bActionActuator *aa = (bActionActuator *)a->data;
			if (!aa->act) {
				auto it = m_brickElement.find(std::string(name_of(ob)) + "/" + a->name);
				if (it != m_brickElement.end()) {
					Warn(it->second, "ANI_NO_ACTION", GV_WARN_BLACK, "no action chosen");
					m_warn[it->second].back().layer = GV_LAYER_ANIMATION;
				}
				continue;
			}
			played.insert(aa->act);
			const int el = Add("action", aa->act->id.name + 2, ob,
			                   fmt("frames %.0f to %.0f, layer %d, played by %s", aa->sta, aa->end, aa->layer,
			                       a->name).c_str());
			if (layers.count(aa->layer) && a->type == ACT_ACTION) {
				Warn(el, "ANI_SAME_LAYER", GV_WARN_NOTE,
				     fmt("plays on layer %d, as %s does: the two fight", aa->layer, layers[aa->layer].c_str()).c_str());
			}
			layers[aa->layer] = a->name;
			/* made for this armature? */
			if (ob->type == OB_ARMATURE && ob->data) {
				bArmature *arm = (bArmature *)ob->data;
				std::set<std::string> bones;
				std::vector<Bone *> stack;
				for (Bone *b = (Bone *)arm->bonebase.first; b; b = b->next) stack.push_back(b);
				while (!stack.empty()) {
					Bone *b = stack.back();
					stack.pop_back();
					bones.insert(b->name);
					for (Bone *c = (Bone *)b->childbase.first; c; c = c->next) stack.push_back(c);
				}
				for (FCurve *fcu = (FCurve *)aa->act->curves.first; fcu; fcu = fcu->next) {
					const char *path = fcu->rna_path;
					const char *p = path ? strstr(path, "pose.bones[\"") : NULL;
					if (!p) continue;
					p += strlen("pose.bones[\"");
					const char *q = strchr(p, '"');
					if (!q) continue;
					const std::string bone(p, q - p);
					if (!bones.count(bone)) {
						Warn(el, "ANI_BONES", GV_WARN_NOTE,
						     fmt("made for another armature: %s has no bone \"%s\"", name_of(ob), bone.c_str()).c_str());
						break;
					}
				}
			}
			/* where it moves the object: a trail */
			FCurve *loc[3] = {NULL, NULL, NULL};
			for (FCurve *fcu = (FCurve *)aa->act->curves.first; fcu; fcu = fcu->next) {
				if (fcu->rna_path && STREQ(fcu->rna_path, "location") && fcu->array_index >= 0 && fcu->array_index < 3) {
					loc[fcu->array_index] = fcu;
				}
			}
			if ((loc[0] || loc[1] || loc[2]) && aa->end > aa->sta) {
				std::vector<float> xyz;
				const int steps = 40;
				for (int i = 0; i <= steps; i++) {
					const float f = aa->sta + (aa->end - aa->sta) * i / steps;
					for (int k = 0; k < 3; k++) {
						xyz.push_back(loc[k] ? evaluate_fcurve(loc[k], f) : ob->loc[k]);
					}
				}
				const int s = Shape(GV_SHAPE_POINTS, GV_LAYER_ANIMATION, el);
				Points(s, xyz);
			}
		}
	}
	/* actions nothing plays */
	std::string unused;
	int n = 0;
	for (bAction *act = (bAction *)m_main->action.first; act; act = (bAction *)act->id.next) {
		if (played.count(act)) continue;
		bool assigned = false;
		for (Object *ob : m_objects) {
			if (ob->adt && ob->adt->action == act) assigned = true;
		}
		if (assigned) continue;
		if (n < 4) unused += fmt("%s%s", n ? ", " : "", act->id.name + 2);
		n++;
	}
	if (n) {
		Warn(0, "ANI_UNUSED", GV_WARN_NOTE,
		     fmt("%d action%s nothing in the logic plays (a script may): %s%s", n, n > 1 ? "s" : "", unused.c_str(),
		         n > 4 ? ", ..." : "").c_str());
		m_warn[0].back().layer = GV_LAYER_ANIMATION;
	}
}

/* ------------------------------------------------------------------------- */
/* Behaviour: Messages (senders to receivers) */

void GVBuilder::Messages()
{
	struct Send { int el; std::string subject, to; Object *ob; };
	struct Recv { int el; std::string subject; Object *ob; };
	std::vector<Send> sends;
	std::vector<Recv> recvs;
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) continue;
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type != ACT_MESSAGE || !a->data) continue;
			bMessageActuator *ma = (bMessageActuator *)a->data;
			const std::string to = ma->toPropName[0] == 'O' && ma->toPropName[1] == 'B' ? ma->toPropName + 2 : ma->toPropName;
			const int el = Add("message:SEND", ma->subject[0] ? ma->subject : "(no subject)", ob,
			                   to.empty() ? "to everyone" : fmt("to %s", to.c_str()).c_str());
			Send s = {el, ma->subject, to, ob};
			sends.push_back(s);
			if (!to.empty() && !BLI_findstring(&m_main->object, to.c_str(), offsetof(ID, name) + 2)) {
				Warn(el, "MSG_TO_MISSING", GV_WARN_RED, fmt("sent to %s, which does not exist", to.c_str()).c_str());
			}
			if (!ma->subject[0]) {
				Warn(el, "MSG_NO_SUBJECT", GV_WARN_NOTE, "no subject: only Message sensors without a subject hear it");
			}
		}
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) {
			if (s->type != SENS_MESSAGE || !s->data) continue;
			bMessageSensor *ms = (bMessageSensor *)s->data;
			const int el = Add("message:RECEIVE", ms->subject[0] ? ms->subject : "(any subject)", ob,
			                   fmt("heard by %s", s->name).c_str());
			Recv r = {el, ms->subject, ob};
			recvs.push_back(r);
		}
	}
	std::vector<bool> heard(recvs.size(), false);
	for (const Send &s : sends) {
		bool any = false;
		for (size_t i = 0; i < recvs.size(); i++) {
			const Recv &r = recvs[i];
			const bool subject = r.subject.empty() || r.subject == s.subject;
			const bool addressed = s.to.empty() || s.to == name_of(r.ob);
			if (subject && addressed) {
				Shape(GV_SHAPE_ARC, GV_LAYER_MESSAGES, s.el, r.el);
				any = true;
				heard[i] = true;
			}
		}
		if (!any) {
			Warn(s.el, "MSG_NO_LISTENER", GV_WARN_NOTE, "nothing in the logic listens for it (a script may)");
		}
	}
	for (size_t i = 0; i < recvs.size(); i++) {
		if (!heard[i]) {
			Warn(recvs[i].el, "MSG_NO_SENDER", GV_WARN_NOTE, "nothing in the logic sends it (a script may)");
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Behaviour: Navigation (navigation meshes, steering, obstacles) */

void GVBuilder::Navigation()
{
	std::map<Object *, std::vector<float> > navtris;
	for (Object *ob : m_objects) {
		if (ob->type != OB_MESH || !(ob->gameflag & OB_NAVMESH) || !ob->data) {
			continue;
		}
		Mesh *me = (Mesh *)ob->data;
		const int el = Add("navmesh", name_of(ob), ob, fmt("%d polygons", me->totpoly).c_str());
		std::vector<float> &xyz = navtris[ob];
		for (int p = 0; p < me->totpoly && xyz.size() < 3 * 3 * 20000; p++) {
			const MPoly &mp = me->mpoly[p];
			for (int k = 1; k + 1 < mp.totloop; k++) {
				const int idx[3] = {(int)me->mloop[mp.loopstart].v, (int)me->mloop[mp.loopstart + k].v,
				                    (int)me->mloop[mp.loopstart + k + 1].v};
				for (int v = 0; v < 3; v++) {
					float co[3];
					mul_v3_m4v3(co, ob->obmat, me->mvert[idx[v]].co);
					xyz.push_back(co[0]);
					xyz.push_back(co[1]);
					xyz.push_back(co[2]);
				}
			}
		}
		const int s = Shape(GV_SHAPE_TRIS, GV_LAYER_NAVIGATION, el);
		Points(s, xyz);
	}
	auto on_mesh = [&](Object *nav, const float co[3]) {
		auto it = navtris.find(nav);
		if (it == navtris.end()) return true;  /* unknown: say nothing */
		const std::vector<float> &t = it->second;
		for (size_t i = 0; i + 8 < t.size(); i += 9) {
			const float a[2] = {t[i], t[i + 1]}, b[2] = {t[i + 3], t[i + 4]}, c[2] = {t[i + 6], t[i + 7]};
			if (isect_point_tri_v2(co, a, b, c)) return true;
		}
		return false;
	};
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) continue;
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type != ACT_STEERING || !a->data) continue;
			bSteeringActuator *sa = (bSteeringActuator *)a->data;
			static const char *modes[] = {"seek", "flee", "path following"};
			const int el = Add("steering", a->name, ob,
			                   fmt("%s %s", (sa->type >= 0 && sa->type <= 2) ? modes[sa->type] : "?",
			                       sa->target ? name_of(sa->target) : "(no target)").c_str());
			if (sa->target && m_objectElement.count(name_of(sa->target))) {
				const int s = Shape(GV_SHAPE_LINE, GV_LAYER_NAVIGATION, el, m_objectElement[name_of(sa->target)]);
				shapes[s].dashed = 1;
			}
			if (sa->type == ACT_STEERING_PATHFOLLOWING && !sa->navmesh) {
				Warn(el, "NAV_NO_NAVMESH", GV_WARN_RED, "path following needs a navigation mesh");
			}
			if (!sa->target) {
				Warn(el, "NAV_NO_TARGET", GV_WARN_BLACK, "no target chosen");
			}
			else if (sa->navmesh && !on_mesh(sa->navmesh, sa->target->obmat[3])) {
				Warn(el, "NAV_TARGET_OFF", GV_WARN_NOTE,
				     fmt("%s is off the navigation mesh %s", name_of(sa->target), name_of(sa->navmesh)).c_str());
			}
		}
		if (ob->gameflag & OB_HASOBSTACLE) {
			const int el = Add("obstacle", name_of(ob), ob, fmt("radius %.2f m", ob->obstacleRad).c_str());
			const int s = Shape(GV_SHAPE_RING, GV_LAYER_NAVIGATION, el);
			shapes[s].p[0] = ob->obstacleRad;
			if (m_scene->gm.obstacleSimulation == OBSTSIMULATION_NONE) {
				Warn(el, "NAV_AVOIDANCE_OFF", GV_WARN_NOTE, "obstacle avoidance is off for the scene (World > Obstacle simulation)");
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Gate: Input (keys, mouse, joysticks) */

void GVBuilder::Input()
{
	struct Use { Object *ob; bSensor *s; };
	std::map<std::string, std::vector<Use> > keys, mice, joys;
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) continue;
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) {
			if (!s->data) continue;
			Use u = {ob, s};
			if (s->type == SENS_KEYBOARD) {
				bKeyboardSensor *ks = (bKeyboardSensor *)s->data;
				if (ks->type) {
					keys["any key"].push_back(u);
				}
				else if (ks->key == 0) {
					auto it = m_brickElement.find(std::string(name_of(ob)) + "/" + s->name);
					if (it != m_brickElement.end()) {
						Warn(it->second, "INP_NO_KEY", GV_WARN_BLACK, "no key chosen");
						m_warn[it->second].back().layer = GV_LAYER_INPUT;
					}
				}
				else {
					keys[key_name(ks->key)].push_back(u);
				}
			}
			else if (s->type == SENS_MOUSE) {
				mice[mouse_event_name(((bMouseSensor *)s->data)->type)].push_back(u);
			}
			else if (s->type == SENS_JOYSTICK) {
				joys[fmt("joystick %d", ((bJoystickSensor *)s->data)->joyindex)].push_back(u);
			}
		}
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type == ACT_MOUSE && a->data && ((bMouseActuator *)a->data)->type == 1 &&
			    (m_scene->gm.flag & GAME_SHOW_MOUSE))
			{
				auto it = m_brickElement.find(std::string(name_of(ob)) + "/" + a->name);
				if (it != m_brickElement.end()) {
					Warn(it->second, "INP_MOUSELOOK_CURSOR", GV_WARN_NOTE, "mouse look with the mouse cursor shown");
					m_warn[it->second].back().layer = GV_LAYER_INPUT;
				}
			}
		}
	}
	if (keys.empty() && mice.empty() && joys.empty()) {
		return;
	}
	Object *cam = m_camera;
	const int hub = Add("input:CONTROLLER", "input", cam,
	                    fmt("%d keys, %d mouse events, %d joysticks", (int)keys.size(), (int)mice.size(),
	                        (int)joys.size()).c_str());
	float fwd[3] = {0, 1, 0}, right[3] = {1, 0, 0}, up[3] = {0, 0, 1};
	if (cam) {
		negate_v3_v3(fwd, cam->obmat[2]);
		normalize_v3(fwd);
		normalize_v3_v3(right, cam->obmat[0]);
		normalize_v3_v3(up, cam->obmat[1]);
		elements[hub].flag |= GV_EL_PLACED;
		madd_v3_v3fl(elements[hub].offset, fwd, 1.8f);
		madd_v3_v3fl(elements[hub].offset, right, 1.0f);
		madd_v3_v3fl(elements[hub].offset, up, -0.55f);
	}
	int slot = 0;
	auto place = [&](int el) {
		if (!cam) return;
		GVElement &e = elements[el];
		e.flag |= GV_EL_PLACED;
		copy_v3_v3(e.offset, elements[hub].offset);
		const int row = slot / 6, col = slot % 6;
		madd_v3_v3fl(e.offset, right, -0.5f + col * 0.22f);
		madd_v3_v3fl(e.offset, up, 0.32f + row * 0.22f);
		slot++;
	};
	auto users = [&](int el, const std::vector<Use> &uses) {
		for (const Use &u : uses) {
			auto it = m_brickElement.find(std::string(name_of(u.ob)) + "/" + u.s->name);
			if (it != m_brickElement.end()) {
				const int s = Shape(GV_SHAPE_LINE, GV_LAYER_INPUT, el, it->second);
				shapes[s].dashed = 1;
			}
		}
	};
	for (auto &kv : keys) {
		const int el = Add("input:KEY", kv.first.c_str(), cam, fmt("used by %d sensors", (int)kv.second.size()).c_str());
		place(el);
		users(el, kv.second);
		/* the exit key */
		if (kv.first == key_name(m_scene->gm.exitkey)) {
			Warn(el, "INP_EXIT_KEY", GV_WARN_NOTE, fmt("%s is also the game's exit key", kv.first.c_str()).c_str());
		}
		/* one key doing two things on one object in one state */
		for (size_t i = 0; i < kv.second.size(); i++) {
			for (size_t j = i + 1; j < kv.second.size(); j++) {
				const Use &a = kv.second[i], &b = kv.second[j];
				if (a.ob != b.ob) continue;
				unsigned int sa = 0, sb = 0;
				std::set<void *> acts_a, acts_b;
				for (bController *c = (bController *)a.ob->controllers.first; c; c = c->next) {
					for (bSensor *s : SensorsOf(c)) {
						if (s == a.s) { sa |= c->state_mask; for (int l = 0; l < c->totlinks; l++) acts_a.insert(c->links[l]); }
						if (s == b.s) { sb |= c->state_mask; for (int l = 0; l < c->totlinks; l++) acts_b.insert(c->links[l]); }
					}
				}
				if ((sa & sb) && acts_a != acts_b) {
					Warn(el, "INP_KEY_TWICE", GV_WARN_NOTE,
					     fmt("does two things on %s in one state (%s and %s)", name_of(a.ob), a.s->name, b.s->name).c_str());
				}
			}
		}
	}
	for (auto &kv : mice) {
		const int el = Add("input:MOUSE", kv.first.c_str(), cam, fmt("used by %d sensors", (int)kv.second.size()).c_str());
		place(el);
		users(el, kv.second);
	}
	for (auto &kv : joys) {
		const int el = Add("input:JOYSTICK", kv.first.c_str(), cam, fmt("used by %d sensors", (int)kv.second.size()).c_str());
		place(el);
		users(el, kv.second);
	}
}

/* ------------------------------------------------------------------------- */
/* Gate: Scenes (scenes as stacked planes, the changes between them) */

void GVBuilder::Scenes()
{
	static const char *modes[] = {"restart", "set", "camera", "add overlay", "add background", "remove",
	                              "suspend", "resume"};
	std::map<Scene *, int> scene_el;
	scene_el[m_scene] = 0;
	std::map<Scene *, int> kind;     /* 1 set, 2 overlay, 3 background */
	std::set<Scene *> removed;
	struct Change { Object *ob; bActuator *a; Scene *target; int type; };
	std::vector<Change> changes;
	for (Object *ob : m_objects) {
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type != ACT_SCENE || !a->data) continue;
			bSceneActuator *sa = (bSceneActuator *)a->data;
			if (ELEM(sa->type, ACT_SCENE_RESTART, ACT_SCENE_CAMERA)) continue;
			if (!sa->scene) {
				auto it = m_brickElement.find(std::string(name_of(ob)) + "/" + a->name);
				if (it != m_brickElement.end()) {
					Warn(it->second, "SCN_NO_SCENE", GV_WARN_BLACK, "no scene chosen");
					m_warn[it->second].back().layer = GV_LAYER_SCENES;
				}
				continue;
			}
			Change c = {ob, a, sa->scene, sa->type};
			changes.push_back(c);
			if (sa->type == ACT_SCENE_REMOVE) removed.insert(sa->scene);
			int &k = kind[sa->scene];
			if (sa->type == ACT_SCENE_SET) k = std::max(k, 1);
			if (sa->type == ACT_SCENE_ADD_FRONT) k = 2;
			if (sa->type == ACT_SCENE_ADD_BACK && k != 2) k = 3;
		}
	}
	/* the planes: the starting scene, overlays above, backgrounds below, others beside */
	const float size = 2.5f;
	const float base_z = top + 3.0f;
	auto plane = [&](int el, float x, float z) {
		GVElement &e = elements[el];
		e.pos[0] = centre[0] + x;
		e.pos[1] = centre[1];
		e.pos[2] = z;
		e.flag |= GV_EL_PLACED;
		const int s = Shape(GV_SHAPE_RECT, GV_LAYER_SCENES, el);
		shapes[s].p[0] = e.pos[0];
		shapes[s].p[1] = e.pos[1];
		shapes[s].p[2] = e.pos[2] - 0.1f;
		shapes[s].p[3] = size;
		shapes[s].p[4] = size * 0.6f;
	};
	plane(0, 0.0f, base_z);
	int overlays = 0, backgrounds = 0, others = 0;
	for (Scene *sce = (Scene *)m_main->scene.first; sce; sce = (Scene *)sce->id.next) {
		if (sce == m_scene) continue;
		const bool reached = kind.count(sce) != 0;
		const bool game = STREQ(sce->r.engine, "BLENDER_GAME") || STREQ(sce->r.engine, "RTX_REMIX");
		if (!reached && !game) continue;
		const int k = reached ? kind[sce] : 0;
		const char *what = k == 2 ? "an overlay" : (k == 3 ? "a background" : (k == 1 ? "switched to" : "not reached"));
		const int el = Add("scene", sce->id.name + 2, NULL, what);
		scene_el[sce] = el;
		if (k == 2) plane(el, 0.0f, base_z + 1.4f * (++overlays));
		else if (k == 3) plane(el, 0.0f, base_z - 1.4f * (++backgrounds));
		else plane(el, (size * 2.3f) * (++others), base_z);
		if (!reached) {
			Warn(el, "SCN_NEVER_REACHED", GV_WARN_NOTE, "nothing in the logic switches to it (a script may)");
			elements[el].state = GV_STATE_WHITE;
		}
		if (k == 2 && !removed.count(sce)) {
			Warn(el, "SCN_OVERLAY_NEVER_REMOVED", GV_WARN_NOTE, "added as an overlay, and nothing in the logic removes it");
		}
		if (reached && !sce->camera) {
			Warn(el, "SCN_NO_CAMERA", GV_WARN_RED, "has no camera: nothing of it can be shown", true);
		}
	}
	for (const Change &c : changes) {
		if (!scene_el.count(c.target)) continue;
		const int s = Shape(GV_SHAPE_ARC, GV_LAYER_SCENES, 0, scene_el[c.target]);
		shapes[s].dashed = c.type == ACT_SCENE_REMOVE || c.type == ACT_SCENE_SUSPEND;
		(void)modes;
	}
	BLI_strncpy(elements[0].info, fmt("the starting scene, %d scene changes in its logic", (int)changes.size()).c_str(),
	            sizeof(elements[0].info));
}

/* ------------------------------------------------------------------------- */
/* Gate: Remix (what RTX Remix receives, what it cannot show) */

void GVBuilder::Remix()
{
#ifdef WITH_REMIX
	const bool runtime = remix_runtime_present();
#else
	const bool runtime = false;
#endif
	const int el = Add("remix:RUNTIME", "RTX Remix", NULL,
	                   fmt("%s; runtime %s", m_remix ? "the scene uses RTX Remix" : "the scene does not use RTX Remix",
	                       runtime ? "installed" : "not found").c_str());
	if (!m_remix) {
		elements[el].state = GV_STATE_WHITE;
		return;
	}
	if (!runtime) {
		Warn(el, "RMX_NO_RUNTIME", GV_WARN_RED,
		     "the RTX Remix runtime is not installed (Preferences > System > RTX Remix)", true);
	}
	int lamps = 0;
	for (Object *ob : m_objects) {
		if (!game_type(ob->type) || !InGame(ob)) continue;
		if (ob->type == OB_LAMP) {
			Add("remix:LIGHT", name_of(ob), ob, "a Remix light");
			lamps++;
		}
		if (ob->type != OB_MESH) continue;
		const int me = Add("remix:MESH", name_of(ob), ob, fmt("%d triangles", ob_triangles(ob)).c_str());
		/* a mesh that changes its shape: replacements made for it do not stay */
		bool deform = (ob->parent && ob->parent->type == OB_ARMATURE) || physics_type(ob) == OB_BODY_TYPE_SOFT;
		if (ob->data && ((Mesh *)ob->data)->key) deform = true;
		for (ModifierData *md = (ModifierData *)ob->modifiers.first; md; md = md->next) {
			if (md->type == eModifierType_Armature) deform = true;
		}
		if (deform) {
			Warn(me, "RMX_DEFORM", GV_WARN_NOTE,
			     "changes its shape while playing: Remix replacements made for this mesh will not stay attached", true);
		}
#ifdef WITH_REMIX
		for (short i = 1; i <= ob->totcol; i++) {
			Material *ma = give_current_material(ob, i);
			if (!ma) continue;
			brmx::MaterialSpec spec;
			brmx::translate_material(ma, spec);
			const int mel = Add("remix:MATERIAL", ma->id.name + 2, ob, fmt("%d notes from the translator", (int)spec.notes.size()).c_str());
			for (const std::string &note : spec.notes) {
				const bool uv = note.find("not mapped by UV") != std::string::npos;
				Warn(mel, uv ? "RMX_NO_UV" : "RMX_NOTE", uv ? GV_WARN_BLACK : GV_WARN_NOTE, note.c_str(), true);
			}
			/* P8.6: procedural textures are baked on the mesh's UV map */
			const Mesh *mesh = (const Mesh *)ob->data;
			if (spec.bake_slots && mesh && !CustomData_has_layer(&mesh->ldata, CD_MLOOPUV)) {
				Warn(mel, "RMX_BAKE_NO_UV", GV_WARN_BLACK,
				     "uses procedural textures, which Remix gets baked on the mesh's UV map, and the mesh has none: "
				     "add a UV map (Remix shows the material's plain colour until then)", true);
			}
		}
#endif
	}
	for (Object *ob : m_objects) {
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type != ACT_2DFILTER) continue;
			auto it = m_brickElement.find(std::string(name_of(ob)) + "/" + a->name);
			if (it != m_brickElement.end()) {
				Warn(it->second, "RMX_FILTER2D", GV_WARN_NOTE, "RTX Remix does not show 2D filters", true);
				m_warn[it->second].back().layer = GV_LAYER_REMIX;
			}
		}
	}
	if (lamps > m_targets.lights) {
		Warn(el, "RMX_LIGHTS", GV_WARN_NOTE, fmt("%d lights: more than the target's %d", lamps, m_targets.lights).c_str(),
		     false, true);
	}
}

/* ------------------------------------------------------------------------- */
/* Gate: Performance (estimated from the file; live in P8) */

void GVBuilder::Performance()
{
	struct Cost { Object *ob; double cost; int tris; };
	std::vector<Cost> costs;
	double total = 0.0;
	long tris_total = 0;
	int bricks = 0, scripts = 0, bodies = 0, lamps = 0;
	for (Object *ob : m_objects) {
		if (!game_type(ob->type) || !InGame(ob)) continue;
		const int tris = ob_triangles(ob);
		int b = 0;
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) b++;
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			b++;
			if (c->type == CONT_PYTHON) scripts++;
		}
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) b++;
		const bool body = moving(physics_type(ob));
		/* a rough weighting: a script is worth many bricks, a body many triangles */
		const double cost = tris * 0.001 + b * 0.05 + (body ? 1.0 : 0.0) + (ob->type == OB_LAMP ? 2.0 : 0.0);
		Cost c = {ob, cost, tris};
		costs.push_back(c);
		total += cost;
		tris_total += tris;
		bricks += b;
		bodies += body ? 1 : 0;
		lamps += ob->type == OB_LAMP ? 1 : 0;
	}
	const int frame = Add("performance:FRAME", "frame budget", NULL,
	                      fmt("%ld triangles, %d bricks, %d scripts, %d moving bodies, %d lamps; target %d FPS (%.1f ms)",
	                          tris_total, bricks, scripts, bodies, lamps, m_targets.fps, 1000.0f / max_ii(m_targets.fps, 1)).c_str());
	if (tris_total > m_targets.triangles) {
		Warn(frame, "PERF_TRIANGLES", GV_WARN_NOTE,
		     fmt("%ld triangles: more than the target's %d", tris_total, m_targets.triangles).c_str(), false, true);
	}
	/* heat on the costliest objects (at most 50) */
	std::sort(costs.begin(), costs.end(), [](const Cost &a, const Cost &b) { return a.cost > b.cost; });
	for (size_t i = 0; i < costs.size() && i < 50; i++) {
		if (total <= 0.0 || costs[i].cost <= 0.0) break;
		const int el = Add("performance:OBJECT", name_of(costs[i].ob), costs[i].ob,
		                   fmt("about %.0f%% of the scene's estimated cost", 100.0 * costs[i].cost / total).c_str());
		elements[el].heat = (float)(costs[i].cost / costs[0].cost);
	}
}

/* ------------------------------------------------------------------------- */
/* Gate: Time (rates, timers, delays, rhythms) */

void GVBuilder::Time()
{
	const GameData &gm = m_scene->gm;
	const int clock = Add("time:CLOCK", "clock", NULL,
	                      fmt("logic %d tics/s, physics %d substeps, at most %d logic and %d physics steps a frame",
	                          gm.ticrate, gm.physubstep, gm.maxlogicstep, gm.maxphystep).c_str());
	if (m_targets.fps < gm.ticrate) {
		Warn(clock, "TIM_FPS_BELOW", GV_WARN_NOTE,
		     fmt("the target's %d FPS is below the logic rate (%d tics/s): logic runs several steps a frame",
		         m_targets.fps, gm.ticrate).c_str());
	}
	if (gm.maxlogicstep > 5) {
		Warn(clock, "TIM_LOGIC_STEPS", GV_WARN_NOTE,
		     fmt("up to %d logic steps in one frame when the game is slow: it can get slower still", gm.maxlogicstep).c_str());
	}
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) continue;
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) {
			if (s->type == SENS_DELAY && s->data) {
				bDelaySensor *ds = (bDelaySensor *)s->data;
				const int el = Add("time:DELAY", s->name, ob,
				                   fmt("after %d tics, for %d%s", ds->delay, ds->duration,
				                       (ds->flag & SENS_DELAY_REPEAT) ? ", repeating" : "").c_str());
				if (ds->delay == 0 && ds->duration == 0) {
					Warn(el, "TIM_DELAY_ZERO", GV_WARN_BLACK, "waits no time at all");
				}
			}
			else if ((s->pulse & SENS_PULSE_REPEAT) && s->freq > 0) {
				Add("time:RHYTHM", s->name, ob, fmt("fires every %d tics", s->freq + 1).c_str());
			}
		}
		/* what the object's logic checks or resets */
		std::string used;
		for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) {
			if (s->type == SENS_PROPERTY && s->data) {
				used += std::string("|") + ((bPropertySensor *)s->data)->name + "|";
			}
		}
		for (bController *c = (bController *)ob->controllers.first; c; c = c->next) {
			if (c->type == CONT_EXPRESSION && c->data) {
				used += std::string(" ") + ((bExpressionCont *)c->data)->str + " ";
			}
		}
		for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) {
			if (a->type == ACT_PROPERTY && a->data) {
				used += std::string("|") + ((bPropertyActuator *)a->data)->name + "|";
			}
		}
		for (bProperty *p = (bProperty *)ob->prop.first; p; p = p->next) {
			if (p->type == GPROP_TIME) {
				const int el = Add("time:TIMER", p->name, ob, "a timer property (counts seconds)");
				if (used.find(std::string("|") + p->name + "|") == std::string::npos &&
				    used.find(p->name) == std::string::npos)
				{
					Warn(el, "TIM_TIMER_NEVER_ENDS", GV_WARN_NOTE,
					     "counts on forever: nothing in the logic checks or resets it (a script may)");
				}
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Gate: Left out (in the file, not in the game) */

void GVBuilder::LeftOut()
{
	for (Object *ob : m_objects) {
		if (!game_type(ob->type)) {
			const int el = Add(fmt("object:%s", ob_type_id(ob->type)).c_str(), name_of(ob), ob,
			                   fmt("a %s object", ob_type_name(ob->type)).c_str());
			Warn(el, "LEFT_TYPE", GV_WARN_RED,
			     fmt("the game engine makes no %s objects", ob_type_name(ob->type)).c_str(), true);
			continue;
		}
		if (m_conv.converted && !m_conv.objects.count(name_of(ob))) {
			const int el = Add("leftout:NOT_MADE", name_of(ob), ob, ob_type_name(ob->type));
			Warn(el, "LEFT_NOT_MADE", GV_WARN_RED, "the converter made no game object for it", true);
		}
		for (ParticleSystem *psys = (ParticleSystem *)ob->particlesystem.first; psys; psys = psys->next) {
			if (m_remix) {
				/* P4: RTX Remix draws Blender's particle systems as its GPU particles */
				Add("remix:PARTICLES", psys->name, ob, "drawn by RTX Remix as GPU particles (no game logic)");
				continue;
			}
			const int el = Add("leftout:PARTICLES", psys->name, ob, "a particle system");
			Warn(el, "LEFT_PARTICLES", GV_WARN_RED, "the game engine has no particle systems (RTX Remix draws them)",
			     true);
		}
		if (ob->pd && ob->pd->forcefield) {
			const int el = Add("leftout:FORCE_FIELD", name_of(ob), ob, "a force field");
			Warn(el, "LEFT_FORCE_FIELD", GV_WARN_RED, "the game engine ignores force fields", true);
		}
		for (bConstraint *con = (bConstraint *)ob->constraints.first; con; con = con->next) {
			if (con->type == CONSTRAINT_TYPE_RIGIDBODYJOINT) continue;
			const int el = Add("leftout:CONSTRAINT", con->name, ob, "an object constraint");
			Warn(el, "LEFT_CONSTRAINT", GV_WARN_RED,
			     "the game engine ignores object constraints (it makes only Rigid Body Joints)", true);
		}
		if (ob->type == OB_MESH) {
			for (short i = 1; i <= ob->totcol; i++) {
				Material *ma = give_current_material(ob, i);
				if (!ma || !ma->use_nodes || !ma->nodetree) continue;
				bool cycles = false;
				for (bNode *node = (bNode *)ma->nodetree->nodes.first; node; node = node->next) {
					if (STRPREFIX(node->idname, "ShaderNodeBsdf") || STREQ(node->idname, "ShaderNodeOutputMaterial") ||
					    STREQ(node->idname, "ShaderNodeEmission"))
					{
						cycles = true;
						break;
					}
				}
				if (cycles) {
					const int el = Add("leftout:NODES", ma->id.name + 2, ob, "Cycles shader nodes");
					Warn(el, "LEFT_CYCLES_NODES", GV_WARN_RED, "Cycles shader nodes: the game draws this material without them",
					     true);
				}
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* states, places, flattening */

void GVBuilder::States()
{
	for (size_t i = 0; i < elements.size(); i++) {
		GVElement &e = elements[i];
		Object *owner = m_owner[i];
		/* ready, but not in the game when it starts: hidden layers, other states */
		if (owner && (!InGame(owner) || (e.flag & GV_EL_HIDDEN_LAYER))) {
			if (e.state == GV_STATE_GREEN) e.state = GV_STATE_WHITE;
			e.flag |= GV_EL_HIDDEN_LAYER;
		}
		if (STRPREFIX(e.kind, "controller:") && owner) {
			/* a controller outside the starting state waits */
			for (bController *c = (bController *)owner->controllers.first; c; c = c->next) {
				if (STREQ(c->name, e.name) && !(c->state_mask & owner->init_state) && e.state == GV_STATE_GREEN) {
					e.state = GV_STATE_WHITE;
				}
			}
		}
		bool red = false, black = false, target = false;
		for (const GVWarning &w : m_warn[i]) {
			red |= w.level == GV_WARN_RED;
			black |= w.level == GV_WARN_BLACK;
			if (w.target && !m_known.count(w.key)) target = true;
		}
		if (red) e.state = GV_STATE_RED;
		else if (black) e.state = GV_STATE_BLACK;
		if (target) e.flag |= GV_EL_OVER_TARGET;
		/* the warnings' own layers count on the element only through its state */
	}
	for (GVShape &s : shapes) {
		if (s.a >= 0 && s.a < (int)elements.size() && s.state == GV_STATE_WHITE) {
			s.state = elements[s.a].state;
		}
	}
}

void GVBuilder::Layout()
{
	/* per owner: one row of avatars, then rows of small ones (bricks: sensors,
	 * controllers, actuators; then properties; then the rest) */
	std::map<Object *, std::vector<int> > by_owner;
	std::vector<int> scene_level;
	for (size_t i = 0; i < elements.size(); i++) {
		GVElement &e = elements[i];
		if (e.flag & GV_EL_PLACED) continue;
		if (m_owner[i]) by_owner[m_owner[i]].push_back((int)i);
		else scene_level.push_back((int)i);
	}
	for (auto &kv : by_owner) {
		Object *ob = kv.first;
		const float z0 = Top(ob) + 0.55f;
		std::map<int, std::vector<int> > rows;
		for (int i : kv.second) {
			const GVElement &e = elements[i];
			int row = 0;
			if (e.flag & GV_EL_SMALL) {
				if (STRPREFIX(e.kind, "sensor:")) row = 1;
				else if (STRPREFIX(e.kind, "controller:")) row = 2;
				else if (STRPREFIX(e.kind, "actuator:")) row = 3;
				else if (STRPREFIX(e.kind, "property:")) row = 4;
				else row = 5;
			}
			rows[row].push_back(i);
		}
		float z = z0;
		for (auto &r : rows) {
			const bool small_row = r.first > 0;
			const float spacing = small_row ? 0.36f : 0.72f;
			if (r.first > 0) z += 0.42f;
			const int n = (int)r.second.size();
			for (int k = 0; k < n; k++) {
				GVElement &e = elements[r.second[k]];
				e.offset[0] = (k - (n - 1) * 0.5f) * spacing;
				e.offset[1] = 0.0f;
				e.offset[2] = z;
				e.flag |= GV_EL_VIEW_ALIGNED;
			}
			if (r.first == 0) z += 0.2f;
		}
	}
	/* scene-level elements: a ring above the scene */
	const int n = (int)scene_level.size();
	float lo[3] = {centre[0], centre[1], centre[2]};
	float radius = 3.0f;
	for (Object *ob : m_objects) {
		if (InGame(ob) && game_type(ob->type)) {
			radius = max_ff(radius, 0.3f * len_v2v2(ob->obmat[3], lo));
		}
	}
	radius = min_ff(radius, 12.0f);
	for (int k = 0; k < n; k++) {
		GVElement &e = elements[scene_level[k]];
		const float a = (float)(2.0 * M_PI * k / max_ii(n, 1));
		e.pos[0] = centre[0] + radius * cosf(a);
		e.pos[1] = centre[1] + radius * sinf(a);
		e.pos[2] = top + 1.8f;
	}
}

void GVBuilder::Flatten()
{
	warnings.clear();
	for (size_t i = 0; i < elements.size(); i++) {
		GVElement &e = elements[i];
		e.warning_first = (int)warnings.size();
		e.warning_count = (int)m_warn[i].size();
		bool all_known = !m_warn[i].empty();
		for (GVWarning w : m_warn[i]) {
			w.element = (int)i;
			w.known = m_known.count(w.key) ? 1 : 0;
			all_known &= w.known != 0;
			warnings.push_back(w);
		}
		if (all_known) {
			e.flag |= GV_EL_KNOWN;
		}
	}
}
