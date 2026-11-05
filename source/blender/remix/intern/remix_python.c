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

/** \file remix_python.c
 *  \ingroup remix
 *
 * The "_bge_remix" Python module. bge_remix_bridge.dll is loaded the first
 * time a function here needs it, never at startup, so Blender pays nothing
 * when Remix is not used and keeps working when the DLL is missing.
 */

#include <Python.h>
#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "BLI_utildefines.h"

#include "BRMX_loader.h"
#include "BRMX_remaster.h"
#include "BRMX_remix.h"
#include "BRMX_session.h"
#include "BRMX_view.h"

/* ------------------------------------------------------------------------- */
/* Bridge DLL (loaded by the shared loader, BRMX_loader.h) */

#define B (BRMX_bridge())

static int bridge_ensure(void)
{
	return BRMX_bridge() != NULL;
}

/* ------------------------------------------------------------------------- */
/* Python */

static const char *status_name(brb_status s)
{
	switch (s) {
		case BRB_STATUS_NOT_LOADED: return "NOT_LOADED";
		case BRB_STATUS_NOT_INSTALLED: return "NOT_INSTALLED";
		case BRB_STATUS_INCOMPATIBLE_VERSION: return "INCOMPATIBLE_VERSION";
		case BRB_STATUS_LOAD_FAILED: return "LOAD_FAILED";
		case BRB_STATUS_STARTUP_FAILED: return "STARTUP_FAILED";
		case BRB_STATUS_READY: return "READY";
		case BRB_STATUS_RUNNING: return "RUNNING";
	}
	return "UNKNOWN";
}

static PyObject *status_tuple(void)
{
	if (!bridge_ensure()) {
		return Py_BuildValue("(ss)", "NO_BRIDGE", BRMX_bridge_error());
	}
	return Py_BuildValue("(ss)", status_name(B->get_status()), B->status_text());
}

PyDoc_STRVAR(brmx_bridge_info_doc,
".. function:: bridge_info()\n"
"\n"
"   Information about bge_remix_bridge.dll (loads it if needed).\n"
"\n"
"   :rtype: dict with loaded, error, path, interface, api_version, log_path\n");
static PyObject *brmx_bridge_info(PyObject *UNUSED_self)
{
	const int ok = bridge_ensure();
	PyObject *path = PyUnicode_FromWideChar(BRMX_bridge_path(), -1);
	PyObject *d = Py_BuildValue(
	        "{s:O,s:s,s:N,s:i,s:s,s:s}",
	        "loaded", ok ? Py_True : Py_False,
	        "error", ok ? "" : BRMX_bridge_error(),
	        "path", path,
	        "interface", ok ? B->abi_version() : 0,
	        "api_version", ok ? B->api_version() : "",
	        "log_path", ok ? B->log_path() : "");
	return d;
}

PyDoc_STRVAR(brmx_status_doc,
".. function:: status()\n"
"\n"
"   :return: (status, text), status one of NO_BRIDGE, NOT_LOADED, NOT_INSTALLED,\n"
"      INCOMPATIBLE_VERSION, LOAD_FAILED, STARTUP_FAILED, READY, RUNNING.\n"
"   :rtype: tuple\n");
static PyObject *brmx_status(PyObject *UNUSED_self)
{
	return status_tuple();
}

PyDoc_STRVAR(brmx_load_doc,
".. function:: load(runtime_dir)\n"
"\n"
"   Load the RTX Remix runtime from a folder that contains its d3d9.dll.\n"
"\n"
"   :return: (status, text)\n");
static PyObject *brmx_load(PyObject *UNUSED_self, PyObject *args)
{
	PyObject *dir_obj;
	wchar_t *dir;

	if (!PyArg_ParseTuple(args, "U:load", &dir_obj)) {
		return NULL;
	}
	if (!bridge_ensure()) {
		return status_tuple();
	}
	dir = PyUnicode_AsWideCharString(dir_obj, NULL);
	if (dir == NULL) {
		return NULL;
	}
	B->load(dir);
	PyMem_Free(dir);
	return status_tuple();
}

PyDoc_STRVAR(brmx_startup_doc,
".. function:: startup(width=1280, height=720, visible=True)\n"
"\n"
"   Start Remix in a window of its own (needs status READY).\n"
"\n"
"   :return: (status, text)\n");
static PyObject *brmx_startup(PyObject *UNUSED_self, PyObject *args, PyObject *kw)
{
	static const char *kwlist[] = {"width", "height", "visible", NULL};
	int width = 1280, height = 720, visible = 1;

	if (!PyArg_ParseTupleAndKeywords(args, kw, "|iip:startup", (char **)kwlist, &width, &height, &visible)) {
		return NULL;
	}
	if (bridge_ensure()) {
		B->startup(width, height, visible);
	}
	return status_tuple();
}

PyDoc_STRVAR(brmx_set_config_doc,
".. function:: set_config(key, value)\n"
"\n"
"   Set one Remix option (rtx.conf key and value).\n"
"\n"
"   :return: Remix error code, 0 on success\n");
static PyObject *brmx_set_config(PyObject *UNUSED_self, PyObject *args)
{
	const char *key, *value;

	if (!PyArg_ParseTuple(args, "ss:set_config", &key, &value)) {
		return NULL;
	}
	if (!bridge_ensure()) {
		return PyLong_FromLong(-1);
	}
	return PyLong_FromLong(B->set_config(key, value));
}

PyDoc_STRVAR(brmx_triangle_test_doc,
".. function:: triangle_test(seconds=5.0, screenshot='', capture=False)\n"
"\n"
"   Draw the P1 test scene for a while (needs status RUNNING).\n"
"\n"
"   :arg screenshot: BMP file to save the last frame to, or '' for none.\n"
"   :arg capture: press Remix's capture hotkey half-way through.\n"
"   :rtype: dict\n");
static PyObject *brmx_triangle_test(PyObject *UNUSED_self, PyObject *args, PyObject *kw)
{
	static const char *kwlist[] = {"seconds", "screenshot", "capture", NULL};
	double seconds = 5.0;
	PyObject *shot_obj = NULL;
	int capture = 0;
	wchar_t *shot = NULL;
	brb_triangle_result r;
	int rc;

	if (!PyArg_ParseTupleAndKeywords(args, kw, "|dUp:triangle_test", (char **)kwlist,
	                                 &seconds, &shot_obj, &capture))
	{
		return NULL;
	}
	memset(&r, 0, sizeof(r));
	if (!bridge_ensure()) {
		PyErr_SetString(PyExc_RuntimeError, BRMX_bridge_error());
		return NULL;
	}
	if (shot_obj) {
		shot = PyUnicode_AsWideCharString(shot_obj, NULL);
		if (shot == NULL) {
			return NULL;
		}
	}
	Py_BEGIN_ALLOW_THREADS
	rc = B->triangle_test(seconds, shot, capture, &r);
	Py_END_ALLOW_THREADS
	if (shot) {
		PyMem_Free(shot);
	}
	return Py_BuildValue(
	        "{s:O,s:i,s:d,s:d,s:d,s:d,s:i,s:O,s:O,s:i}",
	        "ok", rc == 0 ? Py_True : Py_False,
	        "frames", r.frames,
	        "seconds", r.seconds,
	        "frame_ms_mean", r.frame_ms_mean,
	        "frame_ms_median", r.frame_ms_median,
	        "frame_ms_p95", r.frame_ms_p95,
	        "present_errors", r.present_errors,
	        "screenshot_saved", r.screenshot_saved ? Py_True : Py_False,
	        "capture_requested", r.capture_requested ? Py_True : Py_False,
	        "last_error", r.last_error);
}

PyDoc_STRVAR(brmx_runtime_dir_doc,
".. function:: runtime_dir()\n"
"\n"
"   The runtime folder as the game engine finds it (environment variable,\n"
"   settings.json, default), so Python and C can be checked against each other.\n"
"\n"
"   :rtype: str\n");
static PyObject *brmx_runtime_dir(PyObject *UNUSED_self)
{
	wchar_t dir[MAX_PATH * 2];
	if (!BRMX_runtime_dir(dir, (int)(sizeof(dir) / sizeof(dir[0])))) {
		Py_RETURN_NONE;
	}
	return PyUnicode_FromWideChar(dir, -1);
}

PyDoc_STRVAR(brmx_shutdown_doc,
".. function:: shutdown()\n"
"\n"
"   Shut Remix down and unload it (also happens when Blender quits).\n");
static PyObject *brmx_shutdown(PyObject *UNUSED_self)
{
	BRMX_exit();
	Py_RETURN_NONE;
}

/* ------------------------------------------------------------------------- */
/* P5: the RTX Remix render engine (release/scripts/startup/render_remix).
 * The engine passes its own pointers (as_pointer()); see BRMX_view.h. */

#define PTR(type, v) ((type *)(uintptr_t)(v))

PyDoc_STRVAR(brmx_view_update_doc,
".. function:: view_update(engine, data, scene, region, space, region_data)\n"
"\n"
"   RenderEngine.view_update: the scene changed (pointers from as_pointer()).\n");
static PyObject *brmx_view_update(PyObject *UNUSED_self, PyObject *args)
{
	unsigned long long engine, data, scene, region, space, rv3d;
	if (!PyArg_ParseTuple(args, "KKKKKK", &engine, &data, &scene, &region, &space, &rv3d)) {
		return NULL;
	}
	BRMX_view_update(PTR(struct RenderEngine, engine), PTR(struct Main, data), PTR(struct Scene, scene),
	                 PTR(struct ARegion, region), PTR(struct View3D, space), PTR(struct RegionView3D, rv3d));
	Py_RETURN_NONE;
}

PyDoc_STRVAR(brmx_view_draw_doc,
".. function:: view_draw(engine, data, scene, region, space, region_data)\n"
"\n"
"   RenderEngine.view_draw: draw the view. True when it should be drawn again soon.\n"
"\n"
"   :rtype: bool\n");
static PyObject *brmx_view_draw(PyObject *UNUSED_self, PyObject *args)
{
	unsigned long long engine, data, scene, region, space, rv3d;
	int again;
	if (!PyArg_ParseTuple(args, "KKKKKK", &engine, &data, &scene, &region, &space, &rv3d)) {
		return NULL;
	}
	again = BRMX_view_draw(PTR(struct RenderEngine, engine), PTR(struct Main, data), PTR(struct Scene, scene),
	                       PTR(struct ARegion, region), PTR(struct View3D, space), PTR(struct RegionView3D, rv3d));
	return PyBool_FromLong(again);
}

PyDoc_STRVAR(brmx_engine_free_doc,
".. function:: engine_free(engine)\n"
"\n"
"   The engine goes away: its 3D view and F12 data go too.\n");
static PyObject *brmx_engine_free(PyObject *UNUSED_self, PyObject *args)
{
	unsigned long long engine;
	if (!PyArg_ParseTuple(args, "K", &engine)) {
		return NULL;
	}
	BRMX_view_free(PTR(struct RenderEngine, engine));
	BRMX_render_free(PTR(struct RenderEngine, engine));
	Py_RETURN_NONE;
}

PyDoc_STRVAR(brmx_render_sync_doc,
".. function:: render_sync(engine, data, scene)\n"
"\n"
"   RenderEngine.update (F12): read the scene. Returns (ok, message).\n"
"\n"
"   :rtype: tuple\n");
static PyObject *brmx_render_sync(PyObject *UNUSED_self, PyObject *args)
{
	unsigned long long engine, data, scene;
	const char *message = "";
	int ok;
	if (!PyArg_ParseTuple(args, "KKK", &engine, &data, &scene)) {
		return NULL;
	}
	Py_BEGIN_ALLOW_THREADS
	ok = BRMX_render_sync(PTR(struct RenderEngine, engine), PTR(struct Main, data), PTR(struct Scene, scene),
	                      &message);
	Py_END_ALLOW_THREADS
	return Py_BuildValue("(Ns)", PyBool_FromLong(ok), message ? message : "");
}

PyDoc_STRVAR(brmx_render_frame_doc,
".. function:: render_frame(engine, scene)\n"
"\n"
"   RenderEngine.render (F12): render and fill the result. Returns (ok, message).\n"
"\n"
"   :rtype: tuple\n");
static PyObject *brmx_render_frame(PyObject *UNUSED_self, PyObject *args)
{
	unsigned long long engine, scene;
	const char *message = "";
	int ok;
	if (!PyArg_ParseTuple(args, "KK", &engine, &scene)) {
		return NULL;
	}
	Py_BEGIN_ALLOW_THREADS
	ok = BRMX_render_frame(PTR(struct RenderEngine, engine), PTR(struct Scene, scene), &message);
	Py_END_ALLOW_THREADS
	return Py_BuildValue("(Ns)", PyBool_FromLong(ok), message ? message : "");
}

PyDoc_STRVAR(brmx_view_stats_doc,
".. function:: view_stats()\n"
"\n"
"   Counters of the 3D view and F12, for the tests (JSON text).\n"
"\n"
"   :rtype: str\n");
static PyObject *brmx_view_stats(PyObject *UNUSED_self)
{
	return PyUnicode_FromString(BRMX_view_stats_json());
}

PyDoc_STRVAR(brmx_material_match_doc,
".. function:: material_match(pointer)\n"
"\n"
"   BGE-Remix P8.24: how the material (its as_pointer()) reaches RTX Remix,\n"
"   field by field (JSON text).\n"
"\n"
"   :rtype: str\n");
static PyObject *brmx_material_match(PyObject *UNUSED_self, PyObject *args)
{
	unsigned long long ptr = 0;
	if (!PyArg_ParseTuple(args, "K:material_match", &ptr) || !ptr) {
		if (!PyErr_Occurred()) {
			PyErr_SetString(PyExc_ValueError, "material_match: a material's as_pointer() is needed");
		}
		return NULL;
	}
	return PyUnicode_FromString(BRMX_material_match_json((struct Material *)(uintptr_t)ptr));
}

PyDoc_STRVAR(brmx_inspect_doc,
".. function:: inspect(object_pointer, scene_pointer)\n"
"\n"
"   BGE-Remix P9.7: what RTX Remix gets for the object (its as_pointer(); the\n"
"   scene's for its instance count, or 0): mesh, instances, each material's\n"
"   values, textures and their cache files, and its Match rows (JSON text).\n"
"\n"
"   :rtype: str\n");
static PyObject *brmx_inspect(PyObject *UNUSED_self, PyObject *args)
{
	unsigned long long ob = 0, scene = 0;
	if (!PyArg_ParseTuple(args, "K|K:inspect", &ob, &scene) || !ob) {
		if (!PyErr_Occurred()) {
			PyErr_SetString(PyExc_ValueError, "inspect: an object's as_pointer() is needed");
		}
		return NULL;
	}
	return PyUnicode_FromString(BRMX_object_inspect_json((struct Object *)(uintptr_t)ob,
	                                                     (struct Scene *)(uintptr_t)scene));
}

PyDoc_STRVAR(brmx_debug_option_doc,
".. function:: debug_option(key, value)\n"
"\n"
"   BGE-Remix P9.7: one of the Remix View's debug options (rtx.debugView.*,\n"
"   rtx.hashCollisionDetection.enable): sent now when Remix runs, and kept\n"
"   for a game started from Blender.\n");
static PyObject *brmx_debug_option(PyObject *UNUSED_self, PyObject *args)
{
	const char *key, *value;
	if (!PyArg_ParseTuple(args, "ss:debug_option", &key, &value)) {
		return NULL;
	}
	BRMX_debug_option_set(key, value);
	Py_RETURN_NONE;
}

PyDoc_STRVAR(brmx_bake_tile_doc,
".. function:: bake_tile(material_pointer, scene_pointer, size, slots)\n"
"\n"
"   BGE-Remix P10.2: a preset material's tile baked at size x size, always\n"
"   drawn (not from the cache). slots: bits 1 << slot (0 albedo, 1 roughness,\n"
"   2 emissive, 3 normal, 4 metallic, 5 height). Returns {slot: bytes} (RGBA,\n"
"   bottom row first). Needs Blender's OpenGL context (not the background).\n"
"\n"
"   :rtype: dict\n");
static PyObject *brmx_bake_tile(PyObject *UNUSED_self, PyObject *args)
{
	unsigned long long ma = 0, scene = 0;
	int size = 256;
	unsigned int slots = 1;
	unsigned char *px[BRMX_REMASTER_SLOTS];
	char error[256] = "";
	unsigned done;
	PyObject *r;
	int i;
	if (!PyArg_ParseTuple(args, "KK|iI:bake_tile", &ma, &scene, &size, &slots) || !ma || !scene) {
		if (!PyErr_Occurred()) {
			PyErr_SetString(PyExc_ValueError, "bake_tile: a material's and a scene's as_pointer() are needed");
		}
		return NULL;
	}
	if (size < 16 || size > 8192) {
		PyErr_SetString(PyExc_ValueError, "bake_tile: size 16..8192");
		return NULL;
	}
	done = BRMX_remaster_bake_tile(PTR(struct Scene, scene), PTR(struct Material, ma), size, slots, px, error,
	                               (int)sizeof(error));
	if (!done) {
		PyErr_SetString(PyExc_RuntimeError, error[0] ? error : "the tile could not be baked");
		return NULL;
	}
	r = PyDict_New();
	for (i = 0; i < BRMX_REMASTER_SLOTS; i++) {
		if (px[i]) {
			PyObject *key = PyLong_FromLong(i);
			PyObject *val = PyBytes_FromStringAndSize((const char *)px[i], (Py_ssize_t)size * size * 4);
			PyDict_SetItem(r, key, val);
			Py_DECREF(key);
			Py_DECREF(val);
			BRMX_remaster_free(px[i]);
		}
	}
	return r;
}

PyDoc_STRVAR(brmx_precache_doc,
".. function:: precache(object_pointer, scene_pointer)\n"
"\n"
"   BGE-Remix P10.5: the object's materials made into Remix's images as the\n"
"   game makes them (bakes, the texture cache), the encodes started.\n"
"   Returns the textures asked for. Needs Blender's OpenGL context for bakes.\n"
"\n"
"   :rtype: int\n");
static PyObject *brmx_precache(PyObject *UNUSED_self, PyObject *args)
{
	unsigned long long ob = 0, scene = 0;
	char error[256] = "";
	int n;
	if (!PyArg_ParseTuple(args, "KK:precache", &ob, &scene) || !ob || !scene) {
		if (!PyErr_Occurred()) {
			PyErr_SetString(PyExc_ValueError, "precache: an object's and a scene's as_pointer() are needed");
		}
		return NULL;
	}
	n = BRMX_remaster_precache(PTR(struct Scene, scene), PTR(struct Object, ob), error, (int)sizeof(error));
	if (n < 0) {
		PyErr_SetString(PyExc_RuntimeError, error[0] ? error : "precache failed");
		return NULL;
	}
	return PyLong_FromLong(n);
}

PyDoc_STRVAR(brmx_precache_poll_doc,
".. function:: precache_poll()\n"
"\n"
"   BGE-Remix P10.5: the up-front encodes: {waiting, encoded, reused, failed,\n"
"   seconds, bakes}.\n"
"\n"
"   :rtype: dict\n");
static PyObject *brmx_precache_poll(PyObject *UNUSED_self)
{
	int waiting = 0, encoded = 0, reused = 0, failed = 0, bakes = 0;
	double seconds = 0.0;
	BRMX_remaster_poll(&waiting, &encoded, &reused, &failed, &seconds, &bakes);
	return Py_BuildValue("{s:i,s:i,s:i,s:i,s:d,s:i}", "waiting", waiting, "encoded", encoded, "reused", reused,
	                     "failed", failed, "seconds", seconds, "bakes", bakes);
}

PyDoc_STRVAR(brmx_precache_reset_doc,
".. function:: precache_reset()\n"
"\n"
"   BGE-Remix P10.5: the counts back to 0 (the next Apply).\n");
static PyObject *brmx_precache_reset(PyObject *UNUSED_self)
{
	BRMX_remaster_reset();
	Py_RETURN_NONE;
}

static PyMethodDef brmx_methods[] = {
	{"bridge_info", (PyCFunction)brmx_bridge_info, METH_NOARGS, brmx_bridge_info_doc},
	{"status", (PyCFunction)brmx_status, METH_NOARGS, brmx_status_doc},
	{"load", (PyCFunction)brmx_load, METH_VARARGS, brmx_load_doc},
	{"startup", (PyCFunction)brmx_startup, METH_VARARGS | METH_KEYWORDS, brmx_startup_doc},
	{"set_config", (PyCFunction)brmx_set_config, METH_VARARGS, brmx_set_config_doc},
	{"triangle_test", (PyCFunction)brmx_triangle_test, METH_VARARGS | METH_KEYWORDS, brmx_triangle_test_doc},
	{"runtime_dir", (PyCFunction)brmx_runtime_dir, METH_NOARGS, brmx_runtime_dir_doc},
	{"shutdown", (PyCFunction)brmx_shutdown, METH_NOARGS, brmx_shutdown_doc},
	{"view_update", (PyCFunction)brmx_view_update, METH_VARARGS, brmx_view_update_doc},
	{"view_draw", (PyCFunction)brmx_view_draw, METH_VARARGS, brmx_view_draw_doc},
	{"engine_free", (PyCFunction)brmx_engine_free, METH_VARARGS, brmx_engine_free_doc},
	{"render_sync", (PyCFunction)brmx_render_sync, METH_VARARGS, brmx_render_sync_doc},
	{"render_frame", (PyCFunction)brmx_render_frame, METH_VARARGS, brmx_render_frame_doc},
	{"view_stats", (PyCFunction)brmx_view_stats, METH_NOARGS, brmx_view_stats_doc},
	{"material_match", (PyCFunction)brmx_material_match, METH_VARARGS, brmx_material_match_doc},
	{"inspect", (PyCFunction)brmx_inspect, METH_VARARGS, brmx_inspect_doc},
	{"debug_option", (PyCFunction)brmx_debug_option, METH_VARARGS, brmx_debug_option_doc},
	/* BGE-Remix P10.2, P10.5: the texture remaster */
	{"bake_tile", (PyCFunction)brmx_bake_tile, METH_VARARGS, brmx_bake_tile_doc},
	{"precache", (PyCFunction)brmx_precache, METH_VARARGS, brmx_precache_doc},
	{"precache_poll", (PyCFunction)brmx_precache_poll, METH_NOARGS, brmx_precache_poll_doc},
	{"precache_reset", (PyCFunction)brmx_precache_reset, METH_NOARGS, brmx_precache_reset_doc},
	{NULL, NULL, 0, NULL}
};

PyDoc_STRVAR(brmx_module_doc,
"BGE-Remix: access to the NVIDIA RTX Remix runtime through bge_remix_bridge.dll.\n"
"Use the bge_remix package rather than this module directly.");

static struct PyModuleDef brmx_module = {
	PyModuleDef_HEAD_INIT,
	"_bge_remix",
	brmx_module_doc,
	-1,
	brmx_methods,
	NULL, NULL, NULL, NULL
};

static void brmx_atexit(void)
{
	BRMX_exit();
}

PyObject *BRMX_initPython(void)
{
	PyObject *mod = PyModule_Create(&brmx_module);
	if (mod) {
		PyModule_AddIntConstant(mod, "INTERFACE", BRB_ABI_VERSION);
		/* Shut Remix down before Blender's process ends. */
		Py_AtExit(brmx_atexit);
	}
	return mod;
}
