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

/** \file gameengine/BlenderRoutines/BL_GameView.cpp
 *  \ingroup blroutines
 *
 * BGE-Remix P7: the Game View's model: the registry of every kind, the
 * build (the game's converter run inside Blender without the game, then
 * the layers, BL_GameViewLayers.cpp), the in-game report and the Python
 * module _bge_gameview.
 */

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "GPU_glew.h"

#include "KX_BlenderCanvas.h"
#include "KX_BlenderKeyboardDevice.h"
#include "KX_BlenderMouseDevice.h"
#include "KX_BlenderSystem.h"

#include "KX_KetsjiEngine.h"
#include "KX_BlenderSceneConverter.h"
#include "KX_PythonInit.h"
#include "KX_Scene.h"
#include "KX_GameObject.h"

#include "SCA_IObject.h"
#include "SCA_ISensor.h"
#include "SCA_IController.h"
#include "SCA_IActuator.h"
#include "EXP_ListValue.h"

#include "RAS_OpenGLRasterizer.h"
#include "NG_LoopBackNetworkDeviceInterface.h"

#include "BL_GameView.h"
#include "BL_GameViewBuild.h"

extern "C" {
	#include "DNA_material_types.h"
	#include "DNA_scene_types.h"
	#include "DNA_screen_types.h"
	#include "DNA_view3d_types.h"
	#include "DNA_windowmanager_types.h"

	#include "BKE_context.h"
	#include "BKE_global.h"
	#include "BKE_main.h"
	#include "BKE_sound.h"

	#include "BLI_blenlib.h"
	#include "PIL_time.h"
}

/* ------------------------------------------------------------------------- */
/* layers */

static const struct {
	const char *id, *name;
	int cluster;
} g_layers[GV_LAYER_COUNT] = {
	{"OBJECTS", "Objects", GV_CLUSTER_ENTITY},
	{"CAMERAS", "Cameras", GV_CLUSTER_ENTITY},
	{"INTERFACE", "Interface", GV_CLUSTER_ENTITY},
	{"RENDERING", "Rendering", GV_CLUSTER_FACTOR},
	{"PHYSICS", "Physics", GV_CLUSTER_FACTOR},
	{"MEDIA", "Media", GV_CLUSTER_FACTOR},
	{"DATA", "Data", GV_CLUSTER_FACTOR},
	{"FLUIDS", "Fluids", GV_CLUSTER_FACTOR},
	{"ARTICULATIONS", "Articulations", GV_CLUSTER_FACTOR},
	{"LOGIC", "Logic", GV_CLUSTER_BEHAVIOUR},
	{"PYTHON", "Python", GV_CLUSTER_BEHAVIOUR},
	{"ANIMATION", "Animation", GV_CLUSTER_BEHAVIOUR},
	{"MESSAGES", "Messages", GV_CLUSTER_BEHAVIOUR},
	{"NAVIGATION", "Navigation", GV_CLUSTER_BEHAVIOUR},
	{"DESTRUCTION", "Destruction", GV_CLUSTER_BEHAVIOUR},
	{"INPUT", "Input", GV_CLUSTER_GATE},
	{"SCENES", "Scenes", GV_CLUSTER_GATE},
	{"REMIX", "Remix", GV_CLUSTER_GATE},
	{"PERFORMANCE", "Performance", GV_CLUSTER_GATE},
	{"TIME", "Time", GV_CLUSTER_GATE},
	{"LEFT_OUT", "Left out", GV_CLUSTER_GATE},
	{"UNASSIGNED", "Unassigned", GV_CLUSTER_NONE},
};

static const char *g_clusters[] = {"Entity", "Factor", "Behaviour", "Gate", "None"};

extern "C" const char *BL_GameViewLayerName(int layer)
{
	return (layer >= 0 && layer < GV_LAYER_COUNT) ? g_layers[layer].name : "?";
}

extern "C" const char *BL_GameViewLayerId(int layer)
{
	return (layer >= 0 && layer < GV_LAYER_COUNT) ? g_layers[layer].id : "?";
}

extern "C" int BL_GameViewLayerCluster(int layer)
{
	return (layer >= 0 && layer < GV_LAYER_COUNT) ? g_layers[layer].cluster : GV_CLUSTER_NONE;
}

extern "C" const char *BL_GameViewClusterName(int cluster)
{
	return (cluster >= 0 && cluster <= GV_CLUSTER_NONE) ? g_clusters[cluster] : "?";
}

/* ------------------------------------------------------------------------- */
/* the registry: every kind of element the Game View knows, its layer and its
 * avatar (an icon in datafiles/gameview). The coverage test (T7.1) checks
 * that every type Blender's game engine offers has an entry here. */

#define K(kind, layer, avatar) {kind, GV_LAYER_##layer, avatar, 0}
#define KS(kind, layer, avatar) {kind, GV_LAYER_##layer, avatar, 1}

static const GVKind g_kinds[] = {
	/* objects (Object.type): the game makes objects of six types */
	K("object:MESH", OBJECTS, "OUTLINER_OB_MESH"),
	K("object:EMPTY", OBJECTS, "OUTLINER_OB_EMPTY"),
	K("object:ARMATURE", OBJECTS, "OUTLINER_OB_ARMATURE"),
	K("object:CAMERA", OBJECTS, "OUTLINER_OB_CAMERA"),
	K("object:LAMP", OBJECTS, "OUTLINER_OB_LAMP"),
	K("object:FONT", OBJECTS, "OUTLINER_OB_FONT"),
	/* ... and none of these */
	K("object:CURVE", LEFTOUT, "OUTLINER_OB_CURVE"),
	K("object:SURFACE", LEFTOUT, "OUTLINER_OB_SURFACE"),
	K("object:META", LEFTOUT, "OUTLINER_OB_META"),
	K("object:LATTICE", LEFTOUT, "OUTLINER_OB_LATTICE"),
	K("object:SPEAKER", LEFTOUT, "OUTLINER_OB_SPEAKER"),
	K("group:INSTANCE", OBJECTS, "GROUP"),

	K("camera", CAMERAS, "CAMERA_DATA"),

	K("interface:SCREEN", INTERFACE, "GV_SCREEN"),
	KS("interface:TEXT", INTERFACE, "FONT_DATA"),
	KS("interface:ITEM", INTERFACE, "TEXTURE"),
	KS("interface:CURSOR", INTERFACE, "RESTRICT_SELECT_OFF"),

	KS("material", RENDERING, "MATERIAL_DATA"),
	KS("texture", RENDERING, "TEXTURE"),
	K("lamp:POINT", RENDERING, "LAMP_POINT"),
	K("lamp:SUN", RENDERING, "LAMP_SUN"),
	K("lamp:SPOT", RENDERING, "LAMP_SPOT"),
	K("lamp:HEMI", RENDERING, "LAMP_HEMI"),
	K("lamp:AREA", RENDERING, "LAMP_AREA"),
	KS("lod", RENDERING, "MOD_DECIM"),
	K("world", RENDERING, "WORLD"),
	K("mist", RENDERING, "MOD_SMOKE"),
	KS("filter2d", RENDERING, "SEQ_PREVIEW"),

	/* physics (physics_type, collision_bounds_type, pivot_type, engines) */
	K("body:NO_COLLISION", PHYSICS, "PHYSICS"),
	K("body:STATIC", PHYSICS, "PHYSICS"),
	K("body:DYNAMIC", PHYSICS, "PHYSICS"),
	K("body:RIGID_BODY", PHYSICS, "PHYSICS"),
	K("body:SOFT_BODY", PHYSICS, "MOD_SOFT"),
	K("body:OCCLUDER", PHYSICS, "RESTRICT_VIEW_ON"),
	K("body:SENSOR", PHYSICS, "PHYSICS"),
	K("body:NAVMESH", NAVIGATION, "GV_NAVMESH"),
	K("body:CHARACTER", PHYSICS, "POSE_HLT"),
	K("bounds:BOX", PHYSICS, "PHYSICS"),
	K("bounds:SPHERE", PHYSICS, "PHYSICS"),
	K("bounds:CYLINDER", PHYSICS, "PHYSICS"),
	K("bounds:CONE", PHYSICS, "PHYSICS"),
	K("bounds:CONVEX_HULL", PHYSICS, "PHYSICS"),
	K("bounds:TRIANGLE_MESH", PHYSICS, "PHYSICS"),
	K("bounds:CAPSULE", PHYSICS, "PHYSICS"),
	KS("joint:BALL", PHYSICS, "CONSTRAINT"),
	KS("joint:HINGE", PHYSICS, "CONSTRAINT"),
	KS("joint:CONE_TWIST", PHYSICS, "CONSTRAINT"),
	KS("joint:GENERIC_6_DOF", PHYSICS, "CONSTRAINT"),
	K("engine:NONE", PHYSICS, "MOD_PHYSICS"),
	K("engine:BULLET", PHYSICS, "MOD_PHYSICS"),
	K("engine:PHYSX", PHYSICS, "MOD_PHYSICS"),

	K("sound", MEDIA, "SPEAKER"),
	K("listener", MEDIA, "GV_EAR"),
	K("video", MEDIA, "FILE_MOVIE"),

	/* game properties (GameProperty.type) */
	KS("property:BOOL", DATA, "BUTS"),
	KS("property:INT", DATA, "BUTS"),
	KS("property:FLOAT", DATA, "BUTS"),
	KS("property:STRING", DATA, "BUTS"),
	KS("property:TIMER", DATA, "TIME"),
	K("globaldict", DATA, "DISK_DRIVE"),
	K("libload", DATA, "LINK_BLEND"),

	/* logic bricks (Sensor.type, Controller.type, Actuator.type) */
	KS("sensor:ACTUATOR", LOGIC, "PLUG"),
	KS("sensor:ALWAYS", LOGIC, "LOOP_FORWARDS"),
	KS("sensor:ARMATURE", LOGIC, "ARMATURE_DATA"),
	KS("sensor:COLLISION", LOGIC, "GV_COLLISION"),
	KS("sensor:DELAY", LOGIC, "TIME"),
	KS("sensor:JOYSTICK", LOGIC, "LOGIC"),
	KS("sensor:KEYBOARD", LOGIC, "GV_KEYBOARD"),
	KS("sensor:MESSAGE", LOGIC, "GV_MESSAGE"),
	KS("sensor:MOUSE", LOGIC, "HAND"),
	KS("sensor:NEAR", LOGIC, "PROP_ON"),
	KS("sensor:PROPERTY", LOGIC, "BUTS"),
	KS("sensor:RADAR", LOGIC, "GV_RADAR"),
	KS("sensor:RANDOM", LOGIC, "RNDCURVE"),
	KS("sensor:RAY", LOGIC, "FORWARD"),
	KS("controller:LOGIC_AND", LOGIC, "GV_AND"),
	KS("controller:LOGIC_OR", LOGIC, "GV_OR"),
	KS("controller:LOGIC_NAND", LOGIC, "GV_NAND"),
	KS("controller:LOGIC_NOR", LOGIC, "GV_NOR"),
	KS("controller:LOGIC_XOR", LOGIC, "GV_XOR"),
	KS("controller:LOGIC_XNOR", LOGIC, "GV_XNOR"),
	KS("controller:EXPRESSION", LOGIC, "SYNTAX_ON"),
	KS("controller:PYTHON", LOGIC, "SCRIPT"),
	KS("actuator:ACTION", LOGIC, "ACTION"),
	KS("actuator:SHAPE_ACTION", LOGIC, "ACTION"),
	KS("actuator:ARMATURE", LOGIC, "POSE_DATA"),
	KS("actuator:CAMERA", LOGIC, "CAMERA_DATA"),
	KS("actuator:CONSTRAINT", LOGIC, "CONSTRAINT"),
	KS("actuator:EDIT_OBJECT", LOGIC, "EDIT"),
	KS("actuator:FILTER_2D", LOGIC, "SEQ_PREVIEW"),
	KS("actuator:GAME", LOGIC, "GAME"),
	KS("actuator:MESSAGE", LOGIC, "GV_MESSAGE"),
	KS("actuator:MOTION", LOGIC, "MAN_TRANS"),
	KS("actuator:MOUSE", LOGIC, "HAND"),
	KS("actuator:PARENT", LOGIC, "LINK_BLEND"),
	KS("actuator:PROPERTY", LOGIC, "BUTS"),
	KS("actuator:RANDOM", LOGIC, "RNDCURVE"),
	KS("actuator:SCENE", LOGIC, "SCENE_DATA"),
	KS("actuator:SOUND", LOGIC, "SPEAKER"),
	KS("actuator:STATE", LOGIC, "SETTINGS"),
	KS("actuator:STEERING", LOGIC, "CURVE_PATH"),
	KS("actuator:VISIBILITY", LOGIC, "RESTRICT_VIEW_OFF"),

	K("python:SCRIPT", PYTHON, "SCRIPT"),
	K("python:MODULE", PYTHON, "FILE_SCRIPT"),

	K("action", ANIMATION, "ACTION"),

	K("message:SEND", MESSAGES, "GV_MESSAGE"),
	K("message:RECEIVE", MESSAGES, "GV_MESSAGE"),

	K("navmesh", NAVIGATION, "GV_NAVMESH"),
	K("steering", NAVIGATION, "CURVE_PATH"),
	KS("obstacle", NAVIGATION, "PROP_CON"),

	K("input:CONTROLLER", INPUT, "GV_KEYBOARD"),
	KS("input:KEY", INPUT, "GV_KEYBOARD"),
	KS("input:MOUSE", INPUT, "HAND"),
	KS("input:JOYSTICK", INPUT, "LOGIC"),

	K("scene", SCENES, "SCENE_DATA"),

	K("remix:RUNTIME", REMIX, "LIGHTPAINT"),
	KS("remix:MESH", REMIX, "MESH_DATA"),
	KS("remix:MATERIAL", REMIX, "MATSPHERE"),
	KS("remix:LIGHT", REMIX, "LAMP_DATA"),
	K("remix:PARTICLES", REMIX, "PARTICLES"),

	K("performance:FRAME", PERFORMANCE, "SORTTIME"),
	KS("performance:OBJECT", PERFORMANCE, "SORTTIME"),

	K("time:CLOCK", TIME, "TIME"),
	KS("time:DELAY", TIME, "TIME"),
	KS("time:TIMER", TIME, "TIME"),
	KS("time:RHYTHM", TIME, "LOOP_BACK"),

	K("leftout:PARTICLES", LEFTOUT, "PARTICLES"),
	K("leftout:FORCE_FIELD", LEFTOUT, "FORCE_FORCE"),
	K("leftout:CONSTRAINT", LEFTOUT, "CONSTRAINT"),
	K("leftout:NODES", LEFTOUT, "NODETREE"),
	K("leftout:NOT_MADE", LEFTOUT, "ERROR"),

	K("unassigned", UNASSIGNED, "QUESTION"),
};

#undef K
#undef KS

const GVKind *BL_GameViewKinds(int *r_count)
{
	*r_count = (int)(sizeof(g_kinds) / sizeof(g_kinds[0]));
	return g_kinds;
}

const GVKind *BL_GameViewKind(const char *kind)
{
	static std::map<std::string, const GVKind *> index;
	if (index.empty()) {
		int n;
		const GVKind *k = BL_GameViewKinds(&n);
		for (int i = 0; i < n; i++) {
			index[k[i].kind] = &k[i];
		}
	}
	auto it = index.find(kind);
	return it == index.end() ? NULL : it->second;
}

/* ------------------------------------------------------------------------- */
/* the model */

namespace {

struct Store {
	std::vector<GVElement> elements;
	std::vector<GVWarning> warnings;
	std::vector<GVShape> shapes;
	std::vector<float> points;
	GVModel model;
	bool built;
	bool dirty;
	int generation;
	int selected;
	Store() : built(false), dirty(true), generation(0), selected(-1)
	{
		memset(&model, 0, sizeof(model));
	}
};

Store &store()
{
	static Store s;
	return s;
}

void publish(GVBuilder &b, const char *scene, bool converted, double build_ms, double convert_ms)
{
	Store &s = store();
	s.elements.swap(b.elements);
	s.warnings.swap(b.warnings);
	s.shapes.swap(b.shapes);
	s.points.swap(b.points);
	GVModel &m = s.model;
	memset(&m, 0, sizeof(m));
	m.generation = ++s.generation;
	BLI_strncpy(m.scene, scene, sizeof(m.scene));
	m.converted = converted ? 1 : 0;
	m.build_ms = build_ms;
	m.convert_ms = convert_ms;
	m.element_count = (int)s.elements.size();
	m.elements = s.elements.empty() ? NULL : &s.elements[0];
	m.warning_count = (int)s.warnings.size();
	m.warnings = s.warnings.empty() ? NULL : &s.warnings[0];
	m.shape_count = (int)s.shapes.size();
	m.shapes = s.shapes.empty() ? NULL : &s.shapes[0];
	m.point_count = (int)(s.points.size() / 3);
	m.points = s.points.empty() ? NULL : (const float(*)[3])&s.points[0];
	for (const GVElement &e : s.elements) {
		m.counts[e.layer]++;
	}
	for (const GVWarning &w : s.warnings) {
		if (!w.known) {
			m.warnings_per_layer[w.layer]++;
		}
	}
	m.centre[0] = b.centre[0];
	m.centre[1] = b.centre[1];
	m.centre[2] = b.centre[2];
	m.top = b.top;
	s.built = true;
	s.dirty = false;
	if (s.selected >= m.element_count) {
		s.selected = -1;
	}
}

}  /* namespace */

extern "C" const GVModel *BL_GameViewModel(void)
{
	return store().built ? &store().model : NULL;
}

extern "C" void BL_GameViewTagDirty(void)
{
	store().dirty = true;
}

extern "C" int BL_GameViewIsDirty(void)
{
	return store().dirty ? 1 : 0;
}

extern "C" void BL_GameViewClear(void)
{
	Store &s = store();
	s.elements.clear();
	s.warnings.clear();
	s.shapes.clear();
	s.points.clear();
	memset(&s.model, 0, sizeof(s.model));
	s.built = false;
	s.dirty = true;
	s.selected = -1;
}

extern "C" int BL_GameViewSelected(void)
{
	return store().selected;
}

extern "C" void BL_GameViewSetSelected(int element)
{
	Store &s = store();
	s.selected = (element >= 0 && element < (int)s.elements.size()) ? element : -1;
}

/* ------------------------------------------------------------------------- */
/* what the converter made */

void BL_GameViewCollect(KX_Scene *kxscene, GVConverted &r)
{
	r.converted = true;
	CListValue *lists[2] = {kxscene->GetObjectList(), kxscene->GetInactiveList()};
	for (int l = 0; l < 2; l++) {
		CListValue *list = lists[l];
		for (int i = 0; i < list->GetCount(); i++) {
			KX_GameObject *gameobj = (KX_GameObject *)list->GetValue(i);
			const std::string name = gameobj->GetName().ReadPtr();
			r.objects.insert(name);
			if (l == 1) {
				r.hidden.insert(name);
			}
			for (SCA_ISensor *s : gameobj->GetSensors()) {
				r.bricks.insert(name + "/" + s->GetName().ReadPtr());
			}
			for (SCA_IController *c : gameobj->GetControllers()) {
				r.bricks.insert(name + "/" + c->GetName().ReadPtr());
			}
			for (SCA_IActuator *a : gameobj->GetActuators()) {
				r.bricks.insert(name + "/" + a->GetName().ReadPtr());
			}
			if (gameobj->GetPhysicsController()) {
				r.physics.insert(name);
			}
			std::vector<STR_String> props = gameobj->GetPropertyNames();
			for (const STR_String &p : props) {
				r.properties.insert(name + "/" + p.ReadPtr());
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* the build: the game's converter inside Blender, without the game (the
 * P7.0 study's "dry conversion"), then the layers */

static bool dry_convert(bContext *C, ARegion *ar, GVConverted &conv)
{
	struct wmWindowManager *wm = CTX_wm_manager(C);
	struct wmWindow *win = CTX_wm_window(C);
	struct Scene *startscene = CTX_data_scene(C);
	struct Main *blenderdata = CTX_data_main(C);
	View3D *v3d = CTX_wm_view3d(C);
	if (!wm || !win || !startscene || !ar) {
		return false;
	}

	RAS_Rect area_rect;
	area_rect.SetLeft(ar->winrct.xmin);
	area_rect.SetBottom(ar->winrct.ymin);
	area_rect.SetRight(ar->winrct.xmax);
	area_rect.SetTop(ar->winrct.ymax);

	const double t0 = PIL_check_seconds_timer();
	glPushAttrib(GL_ALL_ATTRIB_BITS);

#ifdef WITH_PYTHON
	resetGamePythonPath();
	setGamePythonPath(G.main->name);
	PyGILState_STATE gilstate = PyGILState_Ensure();
	PyObject *pyGlobalDict = PyDict_New();
#endif

	GlobalSettings gs;
	gs.matmode = startscene->gm.matmode;
	gs.glslflag = startscene->gm.flag;

	RAS_ICanvas *canvas = new KX_BlenderCanvas(wm, win, area_rect, ar);
	RAS_IRasterizer *rasterizer = new RAS_OpenGLRasterizer(canvas, RAS_AUTO_STORAGE);
	KX_BlenderKeyboardDevice *keyboarddevice = new KX_BlenderKeyboardDevice();
	KX_BlenderMouseDevice *mousedevice = new KX_BlenderMouseDevice();
	NG_NetworkDeviceInterface *networkdevice = new NG_LoopBackNetworkDeviceInterface();
	KX_BlenderSystem *kxsystem = new KX_BlenderSystem();
	KX_KetsjiEngine *ketsjiengine = new KX_KetsjiEngine(kxsystem);
	ketsjiengine->SetKeyboardDevice(keyboarddevice);
	ketsjiengine->SetMouseDevice(mousedevice);
	ketsjiengine->SetNetworkDevice(networkdevice);
	ketsjiengine->SetCanvas(canvas);
	ketsjiengine->SetRasterizer(rasterizer);
	ketsjiengine->SetRender(false);
	/* the Game View never starts RTX Remix for the game */
	ketsjiengine->SetRemixOptions(0, -1, false);
	ketsjiengine->SetGlobalSettings(&gs);

	int tmp_lay = startscene->lay;
	Object *tmp_camera = startscene->camera;
	if (v3d && v3d->scenelock == 0) {
		startscene->lay = v3d->lay;
		startscene->camera = v3d->camera;
	}
	rasterizer->SetDrawingMode(OB_TEXTURE);

	KX_BlenderSceneConverter *sceneconverter = new KX_BlenderSceneConverter(blenderdata, ketsjiengine);
	ketsjiengine->SetSceneConverter(sceneconverter);
	sceneconverter->SetAlwaysUseExpandFraming(true);
	sceneconverter->SetMaterials(true);
	sceneconverter->SetGLSLMaterials(true);  /* P8: one material mode */
	if (startscene->gm.flag & GAME_NO_MATERIAL_CACHING) {
		sceneconverter->SetCacheMaterials(false);
	}

	KX_Scene *kxscene = new KX_Scene(keyboarddevice, mousedevice, networkdevice, startscene->id.name + 2,
	                                 startscene, canvas);
#ifdef WITH_PYTHON
	PyObject *gameLogic, *gameLogic_keys;
	setupGamePython(ketsjiengine, kxscene, blenderdata, pyGlobalDict, &gameLogic, &gameLogic_keys, 0, NULL);
#endif

	sceneconverter->ConvertScene(kxscene, rasterizer, canvas);
	BL_GameViewCollect(kxscene, conv);
	for (Material *ma : sceneconverter->GetGLSLFailures()) {
		conv.glsl_fallback.insert(ma->id.name + 2);
	}

	sceneconverter->RemoveScene(kxscene);
#ifdef WITH_PYTHON
	PyObject *keys_new = PyDict_Keys(PyModule_GetDict(gameLogic));
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(keys_new); i++) {
		PyObject *item = PyList_GET_ITEM(keys_new, i);
		if (!PySequence_Contains(gameLogic_keys, item)) {
			PyDict_DelItem(PyModule_GetDict(gameLogic), item);
		}
	}
	Py_DECREF(keys_new);
	exitGamePythonScripting();
	Py_DECREF(gameLogic_keys);
#endif
	networkdevice->Disconnect();
	delete sceneconverter;
	if (v3d && v3d->scenelock == 0) {
		startscene->lay = tmp_lay;
		startscene->camera = tmp_camera;
	}
	canvas->SetMouseState(RAS_ICanvas::MOUSE_NORMAL);
	delete ketsjiengine;
	delete kxsystem;
	delete networkdevice;
	delete keyboarddevice;
	delete mousedevice;
	delete rasterizer;
	delete canvas;
#ifdef WITH_PYTHON
	PyDict_Clear(pyGlobalDict);
	Py_DECREF(pyGlobalDict);
	PyGILState_Release(gilstate);
#endif
	glPopAttrib();
	conv.convert_ms = (PIL_check_seconds_timer() - t0) * 1000.0;
	return true;
}

extern "C" int BL_GameViewBuild(struct bContext *C, struct ARegion *ar)
{
	Scene *scene = CTX_data_scene(C);
	Main *bmain = CTX_data_main(C);
	if (!scene || !bmain) {
		return 0;
	}
	const double t0 = PIL_check_seconds_timer();
	GVConverted conv;
	/* the converter only knows game scenes */
	if (STREQ(scene->r.engine, "BLENDER_GAME") || STREQ(scene->r.engine, "RTX_REMIX")) {
		dry_convert(C, ar, conv);
	}
	GVBuilder b(bmain, scene, conv);
	b.Build();
	const double ms = (PIL_check_seconds_timer() - t0) * 1000.0;
	publish(b, scene->id.name + 2, conv.converted, ms, conv.convert_ms);
	return 1;
}

/* ------------------------------------------------------------------------- */
/* when a game starts: the same checks, in the console */

void BL_GameViewReportGame(KX_Scene *kxscene, Main *maggie, KX_BlenderSceneConverter *converter)
{
	Scene *scene = kxscene ? kxscene->GetBlenderScene() : NULL;
	if (!scene || !maggie) {
		return;
	}
	GVConverted conv;
	BL_GameViewCollect(kxscene, conv);
	if (converter) {
		for (Material *ma : converter->GetGLSLFailures()) {
			conv.glsl_fallback.insert(ma->id.name + 2);
		}
	}
	GVBuilder b(maggie, scene, conv);
	b.Build();
	int shown = 0, known = 0;
	for (const GVWarning &w : b.warnings) {
		if (w.known) {
			known++;
			continue;
		}
		const GVElement &e = b.elements[w.element];
		if (shown < 500) {
			printf("BGE-Remix check: %s: %s: %s [%s]\n", BL_GameViewLayerName(w.layer),
			       e.owner[0] ? e.owner : scene->id.name + 2, w.text, w.code);
		}
		shown++;
	}
	printf("BGE-Remix check: %d warnings (%d marked as known) in scene \"%s\"\n", shown, known, scene->id.name + 2);
	fflush(stdout);
}

/* ------------------------------------------------------------------------- */
/* Python: _bge_gameview */

#ifdef WITH_PYTHON

static const char *state_name(int state)
{
	static const char *names[] = {"RED", "BLACK", "WHITE", "GREEN", "BLUE", "YELLOW"};
	return (state >= 0 && state <= GV_STATE_YELLOW) ? names[state] : "?";
}

static PyObject *gv_layers(PyObject *, PyObject *)
{
	PyObject *list = PyList_New(GV_LAYER_COUNT);
	for (int i = 0; i < GV_LAYER_COUNT; i++) {
		PyList_SET_ITEM(list, i, Py_BuildValue("(sss)", g_layers[i].id, g_layers[i].name,
		                                       BL_GameViewClusterName(g_layers[i].cluster)));
	}
	return list;
}

static PyObject *gv_kinds(PyObject *, PyObject *)
{
	int n;
	const GVKind *k = BL_GameViewKinds(&n);
	PyObject *list = PyList_New(n);
	for (int i = 0; i < n; i++) {
		PyList_SET_ITEM(list, i, Py_BuildValue("(sssi)", k[i].kind, BL_GameViewLayerId(k[i].layer), k[i].avatar,
		                                       k[i].is_small));
	}
	return list;
}

static PyObject *gv_model(PyObject *, PyObject *)
{
	const GVModel *m = BL_GameViewModel();
	if (!m) {
		Py_RETURN_NONE;
	}
	PyObject *counts = PyDict_New(), *warns = PyDict_New();
	for (int i = 0; i < GV_LAYER_COUNT; i++) {
		PyObject *v = PyLong_FromLong(m->counts[i]);
		PyDict_SetItemString(counts, g_layers[i].id, v);
		Py_DECREF(v);
		v = PyLong_FromLong(m->warnings_per_layer[i]);
		PyDict_SetItemString(warns, g_layers[i].id, v);
		Py_DECREF(v);
	}
	return Py_BuildValue("{s:s,s:i,s:O,s:d,s:d,s:i,s:i,s:i,s:N,s:N,s:i,s:O}",
	                     "scene", m->scene, "generation", m->generation,
	                     "converted", m->converted ? Py_True : Py_False,
	                     "build_ms", m->build_ms, "convert_ms", m->convert_ms,
	                     "elements", m->element_count, "warnings", m->warning_count, "shapes", m->shape_count,
	                     "counts", counts, "warnings_per_layer", warns,
	                     "selected", BL_GameViewSelected(),
	                     "dirty", BL_GameViewIsDirty() ? Py_True : Py_False);
}

static PyObject *element_dict(const GVModel *m, int i)
{
	const GVElement &e = m->elements[i];
	PyObject *w = PyList_New(e.warning_count);
	for (int k = 0; k < e.warning_count; k++) {
		PyList_SET_ITEM(w, k, PyLong_FromLong(e.warning_first + k));
	}
	return Py_BuildValue("{s:i,s:s,s:s,s:s,s:s,s:s,s:s,s:s,s:i,s:(fff),s:(fff),s:N,s:O,s:O}",
	                     "index", i, "kind", e.kind, "name", e.name, "owner", e.owner,
	                     "layer", BL_GameViewLayerId(e.layer), "state", state_name(e.state),
	                     "avatar", e.avatar, "info", e.info, "flag", e.flag,
	                     "offset", e.offset[0], e.offset[1], e.offset[2],
	                     "pos", e.pos[0], e.pos[1], e.pos[2],
	                     "warnings", w,
	                     "hidden_layer", (e.flag & GV_EL_HIDDEN_LAYER) ? Py_True : Py_False,
	                     "over_target", (e.flag & GV_EL_OVER_TARGET) ? Py_True : Py_False);
}

static PyObject *gv_elements(PyObject *, PyObject *args)
{
	const char *layer = NULL;
	if (!PyArg_ParseTuple(args, "|z", &layer)) {
		return NULL;
	}
	const GVModel *m = BL_GameViewModel();
	PyObject *list = PyList_New(0);
	if (!m) {
		return list;
	}
	for (int i = 0; i < m->element_count; i++) {
		if (layer && strcmp(layer, BL_GameViewLayerId(m->elements[i].layer)) != 0) {
			continue;
		}
		PyObject *d = element_dict(m, i);
		PyList_Append(list, d);
		Py_DECREF(d);
	}
	return list;
}

static PyObject *gv_element(PyObject *, PyObject *args)
{
	int i;
	if (!PyArg_ParseTuple(args, "i", &i)) {
		return NULL;
	}
	const GVModel *m = BL_GameViewModel();
	if (!m || i < 0 || i >= m->element_count) {
		Py_RETURN_NONE;
	}
	return element_dict(m, i);
}

static PyObject *gv_warnings(PyObject *, PyObject *)
{
	const GVModel *m = BL_GameViewModel();
	PyObject *list = PyList_New(0);
	if (!m) {
		return list;
	}
	static const char *levels[] = {"NOTE", "BLACK", "RED"};
	for (int i = 0; i < m->warning_count; i++) {
		const GVWarning &w = m->warnings[i];
		const GVElement &e = m->elements[w.element];
		PyObject *d = Py_BuildValue("{s:i,s:i,s:s,s:s,s:s,s:s,s:s,s:s,s:s,s:O,s:O,s:O}",
		                            "index", i, "element", w.element, "layer", BL_GameViewLayerId(w.layer),
		                            "level", levels[w.level < 0 || w.level > 2 ? 0 : w.level],
		                            "code", w.code, "key", w.key, "text", w.text,
		                            "owner", e.owner, "name", e.name,
		                            "mandatory", w.mandatory ? Py_True : Py_False,
		                            "target", w.target ? Py_True : Py_False,
		                            "known", w.known ? Py_True : Py_False);
		PyList_Append(list, d);
		Py_DECREF(d);
	}
	return list;
}

static PyObject *gv_select(PyObject *, PyObject *args)
{
	int i;
	if (!PyArg_ParseTuple(args, "i", &i)) {
		return NULL;
	}
	BL_GameViewSetSelected(i);
	Py_RETURN_NONE;
}

static PyObject *gv_tag_dirty(PyObject *, PyObject *)
{
	BL_GameViewTagDirty();
	Py_RETURN_NONE;
}

static PyMethodDef gv_methods[] = {
	{"layers", gv_layers, METH_NOARGS, "The layers: (id, name, cluster)"},
	{"kinds", gv_kinds, METH_NOARGS, "The registry: (kind, layer id, avatar, small)"},
	{"model", gv_model, METH_NOARGS, "The last model's summary, or None before the first build"},
	{"elements", gv_elements, METH_VARARGS, "The model's elements, optionally of one layer (layer id)"},
	{"element", gv_element, METH_VARARGS, "One element by index"},
	{"warnings", gv_warnings, METH_NOARGS, "Every warning of the model"},
	{"select", gv_select, METH_VARARGS, "Select an element by index (-1: none)"},
	{"tag_dirty", gv_tag_dirty, METH_NOARGS, "The model is out of date: rebuilt at the next draw"},
	{NULL, NULL, 0, NULL}
};

static struct PyModuleDef gv_module = {
	PyModuleDef_HEAD_INIT, "_bge_gameview", "BGE-Remix Game View model (P7)", -1, gv_methods,
	NULL, NULL, NULL, NULL
};

extern "C" PyObject *BL_GameView_initPython(void)
{
	return PyModule_Create(&gv_module);
}

#else

extern "C" struct _object *BL_GameView_initPython(void)
{
	return NULL;
}

#endif  /* WITH_PYTHON */
