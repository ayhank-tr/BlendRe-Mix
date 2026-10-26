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

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "BLI_utildefines.h"

#include "BKE_appdir.h"

#include "BRMX_remix.h"
#include "bge_remix_bridge.h"

/* ------------------------------------------------------------------------- */
/* Bridge DLL */

typedef int (*brb_abi_version_fn)(void);
typedef const char *(*brb_text_fn)(void);
typedef brb_status (*brb_load_fn)(const wchar_t *);
typedef brb_status (*brb_get_status_fn)(void);
typedef brb_status (*brb_startup_fn)(int, int, int);
typedef int (*brb_set_config_fn)(const char *, const char *);
typedef int (*brb_triangle_test_fn)(double, const wchar_t *, int, brb_triangle_result *);
typedef void (*brb_shutdown_fn)(void);

static struct {
	int tried;
	HMODULE dll;
	char error[512];
	wchar_t path[MAX_PATH];

	brb_abi_version_fn abi_version;
	brb_text_fn api_version;
	brb_load_fn load;
	brb_get_status_fn get_status;
	brb_text_fn status_text;
	brb_text_fn log_path;
	brb_startup_fn startup;
	brb_set_config_fn set_config;
	brb_triangle_test_fn triangle_test;
	brb_shutdown_fn shutdown;
} bridge = {0};

static int bridge_ensure(void)
{
	const char *dir;
	int n;

	if (bridge.tried) {
		return bridge.dll != NULL;
	}
	bridge.tried = 1;

	dir = BKE_appdir_program_dir();
	n = MultiByteToWideChar(CP_UTF8, 0, dir, -1, bridge.path, MAX_PATH - 32);
	if (n <= 0) {
		strcpy(bridge.error, "could not find Blender's own folder");
		return 0;
	}
	wcscat(bridge.path, L"\\bge_remix_bridge.dll");

	bridge.dll = LoadLibraryExW(bridge.path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (bridge.dll == NULL) {
		_snprintf(bridge.error, sizeof(bridge.error),
		          "bge_remix_bridge.dll did not load (Windows error %lu)", GetLastError());
		return 0;
	}

#define BRIDGE_FN(name) \
	bridge.name = (brb_##name##_fn)GetProcAddress(bridge.dll, "brb_" #name); \
	if (bridge.name == NULL) { \
		_snprintf(bridge.error, sizeof(bridge.error), "bge_remix_bridge.dll lacks brb_" #name); \
		goto fail; \
	} (void)0

	BRIDGE_FN(abi_version);
	if (bridge.abi_version() != BRB_ABI_VERSION) {
		_snprintf(bridge.error, sizeof(bridge.error),
		          "bge_remix_bridge.dll is from another build (interface %d, expected %d)",
		          bridge.abi_version(), BRB_ABI_VERSION);
		goto fail;
	}
	bridge.api_version = (brb_text_fn)GetProcAddress(bridge.dll, "brb_api_version");
	bridge.status_text = (brb_text_fn)GetProcAddress(bridge.dll, "brb_status_text");
	bridge.log_path = (brb_text_fn)GetProcAddress(bridge.dll, "brb_log_path");
	if (!bridge.api_version || !bridge.status_text || !bridge.log_path) {
		strcpy(bridge.error, "bge_remix_bridge.dll is incomplete");
		goto fail;
	}
	BRIDGE_FN(load);
	BRIDGE_FN(get_status);
	BRIDGE_FN(startup);
	BRIDGE_FN(set_config);
	BRIDGE_FN(triangle_test);
	BRIDGE_FN(shutdown);
#undef BRIDGE_FN
	return 1;

fail:
	FreeLibrary(bridge.dll);
	bridge.dll = NULL;
	return 0;
}

void BRMX_exit(void)
{
	if (bridge.dll && bridge.shutdown) {
		bridge.shutdown();
	}
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
		return Py_BuildValue("(ss)", "NO_BRIDGE", bridge.error);
	}
	return Py_BuildValue("(ss)", status_name(bridge.get_status()), bridge.status_text());
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
	PyObject *path = PyUnicode_FromWideChar(bridge.path, -1);
	PyObject *d = Py_BuildValue(
	        "{s:O,s:s,s:N,s:i,s:s,s:s}",
	        "loaded", ok ? Py_True : Py_False,
	        "error", ok ? "" : bridge.error,
	        "path", path,
	        "interface", ok ? bridge.abi_version() : 0,
	        "api_version", ok ? bridge.api_version() : "",
	        "log_path", ok ? bridge.log_path() : "");
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
	bridge.load(dir);
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
		bridge.startup(width, height, visible);
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
	return PyLong_FromLong(bridge.set_config(key, value));
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
		PyErr_SetString(PyExc_RuntimeError, bridge.error);
		return NULL;
	}
	if (shot_obj) {
		shot = PyUnicode_AsWideCharString(shot_obj, NULL);
		if (shot == NULL) {
			return NULL;
		}
	}
	Py_BEGIN_ALLOW_THREADS
	rc = bridge.triangle_test(seconds, shot, capture, &r);
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

PyDoc_STRVAR(brmx_shutdown_doc,
".. function:: shutdown()\n"
"\n"
"   Shut Remix down and unload it (also happens when Blender quits).\n");
static PyObject *brmx_shutdown(PyObject *UNUSED_self)
{
	BRMX_exit();
	Py_RETURN_NONE;
}

static PyMethodDef brmx_methods[] = {
	{"bridge_info", (PyCFunction)brmx_bridge_info, METH_NOARGS, brmx_bridge_info_doc},
	{"status", (PyCFunction)brmx_status, METH_NOARGS, brmx_status_doc},
	{"load", (PyCFunction)brmx_load, METH_VARARGS, brmx_load_doc},
	{"startup", (PyCFunction)brmx_startup, METH_VARARGS | METH_KEYWORDS, brmx_startup_doc},
	{"set_config", (PyCFunction)brmx_set_config, METH_VARARGS, brmx_set_config_doc},
	{"triangle_test", (PyCFunction)brmx_triangle_test, METH_VARARGS | METH_KEYWORDS, brmx_triangle_test_doc},
	{"shutdown", (PyCFunction)brmx_shutdown, METH_NOARGS, brmx_shutdown_doc},
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
