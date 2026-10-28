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

/** \file gameengine/BlenderRoutines/BL_GameViewStudy.cpp
 *  \ingroup blroutines
 *
 * BGE-Remix P7.0 study: "dry conversion". The game's own converter builds
 * the current scene inside Blender, as when the game starts (P in the 3D
 * View), but the game does not run: every element it made is listed, then
 * everything is taken down again. Writes a JSON report: what was made, by
 * kind; what in the scene was not made; how long each part took and how much
 * memory it used.
 */

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#endif

#include <stdio.h>
#include <map>
#include <set>
#include <string>
#include <typeinfo>
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
#include "KX_Camera.h"
#include "KX_Light.h"

#include "SCA_IObject.h"
#include "SCA_ISensor.h"
#include "SCA_IController.h"
#include "SCA_IActuator.h"
#include "EXP_ListValue.h"

#include "PHY_IPhysicsController.h"
#include "RAS_MeshObject.h"
#include "RAS_OpenGLRasterizer.h"

#include "NG_LoopBackNetworkDeviceInterface.h"

#include "BL_System.h"

extern "C" {
	#include "DNA_object_types.h"
	#include "DNA_object_force.h"
	#include "DNA_particle_types.h"
	#include "DNA_sensor_types.h"
	#include "DNA_controller_types.h"
	#include "DNA_actuator_types.h"
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

#ifdef WITH_AUDASPACE
#  include AUD_DEVICE_H
#endif

namespace {

double process_mb()
{
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS_EX pmc;
	memset(&pmc, 0, sizeof(pmc));
	pmc.cb = sizeof(pmc);
	if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc))) {
		return pmc.PrivateUsage / (1024.0 * 1024.0);
	}
#endif
	return -1.0;
}

std::string clean_type(const char *name)
{
	std::string s(name ? name : "?");
	/* MSVC: "class SCA_KeyboardSensor"; GCC: "18SCA_KeyboardSensor" */
	const char *prefixes[] = {"class ", "struct "};
	for (const char *p : prefixes) {
		if (s.compare(0, strlen(p), p) == 0) {
			s = s.substr(strlen(p));
		}
	}
	size_t i = 0;
	while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
		i++;
	}
	return s.substr(i);
}

std::string json_str(const std::string &s)
{
	std::string r = "\"";
	for (char c : s) {
		if (c == '"' || c == '\\') {
			r += '\\';
			r += c;
		}
		else if ((unsigned char)c < 0x20) {
			char buf[8];
			BLI_snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
			r += buf;
		}
		else {
			r += c;
		}
	}
	return r + "\"";
}

const char *ob_type_name(short type)
{
	switch (type) {
		case OB_EMPTY: return "EMPTY";
		case OB_MESH: return "MESH";
		case OB_CURVE: return "CURVE";
		case OB_SURF: return "SURFACE";
		case OB_FONT: return "TEXT";
		case OB_MBALL: return "META";
		case OB_LAMP: return "LAMP";
		case OB_CAMERA: return "CAMERA";
		case OB_SPEAKER: return "SPEAKER";
		case OB_LATTICE: return "LATTICE";
		case OB_ARMATURE: return "ARMATURE";
		default: return "OTHER";
	}
}

const char *body_name(const Object *ob)
{
	if (ob->gameflag & OB_SOFT_BODY) return "SOFT_BODY";
	if (ob->gameflag & OB_CHARACTER) return "CHARACTER";
	if (ob->gameflag & OB_RIGID_BODY) return "RIGID_BODY";
	if (ob->gameflag & OB_DYNAMIC) return "DYNAMIC";
	if (ob->gameflag & OB_SENSOR) return "SENSOR";
	if (ob->gameflag & OB_NAVMESH) return "NAVMESH";
	if (ob->gameflag & OB_OCCLUDER) return "OCCLUDER";
	if (ob->gameflag & OB_COLLISION) return "STATIC";
	return "NO_COLLISION";
}

struct StudyReport {
	std::map<std::string, int> kinds;            /* kind -> count, as converted */
	std::map<std::string, int> blender;          /* the same, counted in Blender's data */
	std::vector<std::string> elements;           /* the first elements, one line each */
	std::set<std::string> converted;             /* names of converted objects */
	std::vector<std::string> left_out;           /* not converted, with the reason */
	int element_count = 0;

	void add(const std::string &kind, const std::string &owner, const std::string &name, const char *layer)
	{
		kinds[kind]++;
		element_count++;
		if (elements.size() < 400) {
			elements.push_back("{\"kind\": " + json_str(kind) + ", \"object\": " + json_str(owner) +
			                   ", \"name\": " + json_str(name) + ", \"layer\": " + json_str(layer) + "}");
		}
	}
};

void list_object(StudyReport &r, KX_GameObject *gameobj, bool active)
{
	Object *ob = gameobj->GetBlenderObject();
	const std::string name = gameobj->GetName().ReadPtr();
	r.converted.insert(name);
	const char *where = active ? "active" : "hidden layer";
	r.add(std::string("object:") + (ob ? ob_type_name(ob->type) : "NONE") + (active ? "" : " (hidden layer)"), name,
	      name, "Objects");

	for (int i = 0; i < gameobj->GetMeshCount(); i++) {
		RAS_MeshObject *mesh = gameobj->GetMesh(i);
		r.add("mesh", name, mesh->GetName().ReadPtr(), "Rendering");
		for (int m = 0; m < mesh->NumMaterials(); m++) {
			r.add("material", name, mesh->GetMaterialName(m).ReadPtr(), "Rendering");
		}
	}
	if (PHY_IPhysicsController *ctrl = gameobj->GetPhysicsController()) {
		r.add("physics:" + clean_type(typeid(*ctrl).name()) + ":" + (ob ? body_name(ob) : "?"), name,
		      ob ? body_name(ob) : "", "Physics");
	}
	for (SCA_ISensor *s : gameobj->GetSensors()) {
		r.add("sensor:" + clean_type(typeid(*s).name()), name, s->GetName().ReadPtr(), "Logic");
	}
	for (SCA_IController *c : gameobj->GetControllers()) {
		r.add("controller:" + clean_type(typeid(*c).name()), name, c->GetName().ReadPtr(),
		      clean_type(typeid(*c).name()) == "SCA_PythonController" ? "Python" : "Logic");
	}
	for (SCA_IActuator *a : gameobj->GetActuators()) {
		r.add("actuator:" + clean_type(typeid(*a).name()), name, a->GetName().ReadPtr(), "Logic");
	}
	std::vector<STR_String> props = gameobj->GetPropertyNames();
	for (const STR_String &p : props) {
		CValue *v = gameobj->GetProperty(p);
		r.add("property:" + clean_type(v ? typeid(*v).name() : "none"), name, p.ReadPtr(), "Data");
	}
	(void)where;
}

/* the same kinds, counted in Blender's own data (what the converter should make) */
void count_blender(StudyReport &r, Scene *scene)
{
	for (Scene *sce_iter = scene; sce_iter; sce_iter = sce_iter->set) {
		for (Base *base = (Base *)sce_iter->base.first; base; base = (Base *)base->next) {
			Object *ob = base->object;
			r.blender[std::string("object:") + ob_type_name(ob->type)]++;
			int n = 0;
			for (bSensor *s = (bSensor *)ob->sensors.first; s; s = s->next) n++;
			r.blender["sensors"] += n;
			n = 0;
			for (bController *c = (bController *)ob->controllers.first; c; c = c->next) n++;
			r.blender["controllers"] += n;
			n = 0;
			for (bActuator *a = (bActuator *)ob->actuators.first; a; a = a->next) n++;
			r.blender["actuators"] += n;
			n = 0;
			for (ParticleSystem *p = (ParticleSystem *)ob->particlesystem.first; p; p = p->next) n++;
			r.blender["particle systems"] += n;
			if (n) {
				r.left_out.push_back("{\"object\": " + json_str(ob->id.name + 2) +
				                     ", \"what\": \"particle system\", \"why\": \"the game engine has no particle systems\"}");
			}
			if (ob->pd && ob->pd->forcefield) {
				r.left_out.push_back("{\"object\": " + json_str(ob->id.name + 2) +
				                     ", \"what\": \"force field\", \"why\": \"the game engine ignores force fields\"}");
			}
		}
	}
}

}  /* namespace */

extern "C" int BL_GameViewStudyConvert(struct bContext *C, struct ARegion *ar, const char *report_path, int repeat)
{
	struct wmWindowManager *wm = CTX_wm_manager(C);
	struct wmWindow *win = CTX_wm_window(C);
	struct Scene *startscene = CTX_data_scene(C);
	struct Main *blenderdata = CTX_data_main(C);
	View3D *v3d = CTX_wm_view3d(C);

	RAS_Rect area_rect;
	area_rect.SetLeft(ar->winrct.xmin);
	area_rect.SetBottom(ar->winrct.ymin);
	area_rect.SetRight(ar->winrct.xmax);
	area_rect.SetTop(ar->winrct.ymax);

	std::vector<std::string> runs;
	StudyReport report;
	if (repeat < 1) {
		repeat = 1;
	}

#ifdef WITH_PYTHON
	resetGamePythonPath();
	setGamePythonPath(G.main->name);
	PyGILState_STATE gilstate = PyGILState_Ensure();
	PyObject *pyGlobalDict = PyDict_New();
#endif

	for (int run = 0; run < repeat; run++) {
		const double mem0 = process_mb();
		const double t0 = PIL_check_seconds_timer();

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
		/* the study never starts RTX Remix */
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
		if (gs.matmode == GAME_MAT_GLSL) {
			sceneconverter->SetGLSLMaterials(true);
		}
		if (startscene->gm.flag & GAME_NO_MATERIAL_CACHING) {
			sceneconverter->SetCacheMaterials(false);
		}

		KX_Scene *kxscene = new KX_Scene(keyboarddevice, mousedevice, networkdevice, startscene->id.name + 2,
		                                 startscene, canvas);
#ifdef WITH_PYTHON
		PyObject *gameLogic, *gameLogic_keys;
		setupGamePython(ketsjiengine, kxscene, blenderdata, pyGlobalDict, &gameLogic, &gameLogic_keys, 0, NULL);
#endif
		const double t1 = PIL_check_seconds_timer();

		sceneconverter->ConvertScene(kxscene, rasterizer, canvas);

		const double t2 = PIL_check_seconds_timer();
		const double mem_converted = process_mb();

		/* what was made */
		StudyReport r;
		CListValue *objects = kxscene->GetObjectList();
		for (int i = 0; i < objects->GetCount(); i++) {
			list_object(r, (KX_GameObject *)objects->GetValue(i), true);
		}
		CListValue *inactive = kxscene->GetInactiveList();
		for (int i = 0; i < inactive->GetCount(); i++) {
			list_object(r, (KX_GameObject *)inactive->GetValue(i), false);
		}
		r.add("scene", startscene->id.name + 2, startscene->id.name + 2, "Scenes");
		count_blender(r, startscene);
		for (Scene *sce_iter = startscene; sce_iter; sce_iter = sce_iter->set) {
			for (Base *base = (Base *)sce_iter->base.first; base; base = (Base *)base->next) {
				const char *nm = base->object->id.name + 2;
				if (!r.converted.count(nm)) {
					r.left_out.push_back("{\"object\": " + json_str(nm) + ", \"what\": " +
					                     json_str(ob_type_name(base->object->type)) +
					                     ", \"why\": \"the converter made no game object for it\"}");
				}
			}
		}
		const double t3 = PIL_check_seconds_timer();

		/* take everything down again, as at the end of a game */
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
#ifdef WITH_AUDASPACE
		AUD_Device_stopAll(BKE_sound_get_device());
#endif
		const double t4 = PIL_check_seconds_timer();
		const double mem_after = process_mb();

		char buf[512];
		BLI_snprintf(buf, sizeof(buf),
		             "{\"setup_ms\": %.2f, \"convert_ms\": %.2f, \"list_ms\": %.2f, \"teardown_ms\": %.2f, "
		             "\"total_ms\": %.2f, \"mem_before_mb\": %.1f, \"mem_converted_mb\": %.1f, \"mem_after_mb\": %.1f, "
		             "\"elements\": %d}",
		             (t1 - t0) * 1000.0, (t2 - t1) * 1000.0, (t3 - t2) * 1000.0, (t4 - t3) * 1000.0,
		             (t4 - t0) * 1000.0, mem0, mem_converted, mem_after, r.element_count);
		runs.push_back(buf);
		printf("BGE-Remix Game View study: run %d: %s\n", run + 1, buf);
		fflush(stdout);
		if (run == 0) {
			report = r;
		}
	}

#ifdef WITH_PYTHON
	PyDict_Clear(pyGlobalDict);
	Py_DECREF(pyGlobalDict);
	PyGILState_Release(gilstate);
#endif

	FILE *f = BLI_fopen(report_path, "w");
	if (!f) {
		printf("BGE-Remix Game View study: cannot write %s\n", report_path);
		return 0;
	}
	fprintf(f, "{\n\"scene\": %s,\n\"runs\": [", json_str(startscene->id.name + 2).c_str());
	for (size_t i = 0; i < runs.size(); i++) {
		fprintf(f, "%s%s", i ? ", " : "", runs[i].c_str());
	}
	fprintf(f, "],\n\"element_count\": %d,\n\"kinds\": {", report.element_count);
	bool first = true;
	for (auto &kv : report.kinds) {
		fprintf(f, "%s%s: %d", first ? "" : ", ", json_str(kv.first).c_str(), kv.second);
		first = false;
	}
	fprintf(f, "},\n\"blender\": {");
	first = true;
	for (auto &kv : report.blender) {
		fprintf(f, "%s%s: %d", first ? "" : ", ", json_str(kv.first).c_str(), kv.second);
		first = false;
	}
	fprintf(f, "},\n\"left_out\": [");
	for (size_t i = 0; i < report.left_out.size() && i < 200; i++) {
		fprintf(f, "%s%s", i ? ", " : "", report.left_out[i].c_str());
	}
	fprintf(f, "],\n\"elements\": [\n");
	for (size_t i = 0; i < report.elements.size(); i++) {
		fprintf(f, "%s%s", i ? ",\n" : "", report.elements[i].c_str());
	}
	fprintf(f, "\n]\n}\n");
	fclose(f);
	return 1;
}
