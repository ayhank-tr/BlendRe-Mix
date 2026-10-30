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

/** \file gameengine/Ketsji/KX_RemixRenderer.cpp
 *  \ingroup ketsji
 */

#ifdef WITH_REMIX

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "glew-mx.h"

#include "KX_RemixRenderer.h"

#include "KX_Scene.h"
#include "KX_GameObject.h"
#include "KX_Light.h"
#include "KX_Camera.h"
#include "KX_WorldInfo.h"

#include "RAS_ICanvas.h"
#include "RAS_ILightObject.h"
#include "RAS_IPolygonMaterial.h"
#include "RAS_MaterialBucket.h"
#include "RAS_MeshObject.h"
#include "RAS_Rect.h"
#include "RAS_TexVert.h"

#include "EXP_ListValue.h"

#include "MT_Transform.h"

#include "BL_SkinDeformer.h"
#include "BL_ModifierDeformer.h"
#ifdef WITH_BULLET
#  include "KX_SoftBodyDeformer.h"
#endif

#include <typeinfo>

extern "C" {
#  include "BKE_global.h"
#  include "BKE_idprop.h"
#  include "BLI_math.h"
#  include "BKE_DerivedMesh.h"
#  include "DNA_ID.h"
#  include "DNA_lamp_types.h"
#  include "DNA_material_types.h"
#  include "DNA_meshdata_types.h"
#  include "DNA_object_types.h"
#  include "DNA_object_force.h"
#  include "DNA_particle_types.h"
#  include "DNA_scene_types.h"
}

#include "BRMX_loader.h"  /* C linkage inside */
#include "BRMX_bake.h"
#include "BRMX_light.h"
#include "BRMX_material.h"
#include "BRMX_present.h"
#include "BRMX_session.h"

/* The world colour and the lamps, in Blender Internal's units, are turned
 * into Remix's physical units so that a white surface lit by a lamp of
 * energy 1 at distance D comes out as bright as in Blender Internal (far
 * from the lamp); bge_remix_light_scale is a global factor on top, tuned by
 * eye against the room's Cycles render (P2 sign-off). */
static const double NOTICE_SECONDS = 8.0;
/* Chosen by eye against the room's Cycles render (P2 sign-off, RTX 3080):
 * with 1, the lamps outshine everything else under Remix's automatic
 * exposure (the glowing panel disappears) */
static const float DEFAULT_LIGHT_SCALE = 0.125f;
/* Chosen against the material grid's Cycles render (P3 sign-off, RTX 3080):
 * +1 stop matches the backdrop's mid-grey and keeps light balls, glass and
 * metals from washing out. (P2 picked +2 while colours were still decoded
 * from gamma twice, which darkened them.) Same default as the Rasterizer
 * panel's Exposure. */
static const float DEFAULT_EXPOSURE = 1.0f;
static const double PI = 3.14159265358979323846;

/* ------------------------------------------------------------------------- */
/* Helpers */

/* P5: one caller of Remix at a time (the 3D view, F12 on its own thread) */
struct RemixLock {
	RemixLock() { BRMX_lock(); }
	~RemixLock() { BRMX_unlock(); }
};

static uint64_t fnv1a(const std::string &s, uint64_t h = 1469598103934665603ull)
{
	for (unsigned char c : s) {
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

static float median(std::vector<float> v)
{
	if (v.empty()) {
		return 0.0f;
	}
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

static float percentile95(std::vector<float> v)
{
	if (v.empty()) {
		return 0.0f;
	}
	std::sort(v.begin(), v.end());
	return v[std::min(v.size() - 1, (size_t)(v.size() * 0.95))];
}

static double now_seconds()
{
	static LARGE_INTEGER freq = {};
	if (freq.QuadPart == 0) {
		QueryPerformanceFrequency(&freq);
	}
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart / (double)freq.QuadPart;
}

/* Through the Win32 API: Blender's C runtime keeps its own copy of the
 * environment, which does not see changes made from Python. */
static bool env_path(const wchar_t *name, std::wstring &r_path)
{
	wchar_t buf[32768];
	const DWORD n = GetEnvironmentVariableW(name, buf, 32768);
	if (n == 0 || n >= 32768) {
		return false;
	}
	r_path.assign(buf, n);
	return true;
}

static int idprop_int(IDProperty *group, const char *name, int def)
{
	if (!group) {
		return def;
	}
	IDProperty *p = IDP_GetPropertyFromGroup(group, name);
	if (!p) {
		return def;
	}
	switch (p->type) {
		case IDP_INT: return IDP_Int(p);
		case IDP_FLOAT: return (int)IDP_Float(p);
		case IDP_DOUBLE: return (int)IDP_Double(p);
		default: return def;
	}
}

static float idprop_float(IDProperty *group, const char *name, float def)
{
	if (!group) {
		return def;
	}
	IDProperty *p = IDP_GetPropertyFromGroup(group, name);
	if (!p) {
		return def;
	}
	switch (p->type) {
		case IDP_INT: return (float)IDP_Int(p);
		case IDP_FLOAT: return IDP_Float(p);
		case IDP_DOUBLE: return (float)IDP_Double(p);
		default: return def;
	}
}

static void column(const MT_Matrix3x3 &m, int c, float r_v[3])
{
	r_v[0] = (float)m[0][c];
	r_v[1] = (float)m[1][c];
	r_v[2] = (float)m[2][c];
}

/* The object's OpenGL matrix, as KX_GameObject::GetOpenGLMatrix computes it
 * (without touching the object's "dirty" flag). */
static void object_matrix(KX_GameObject *obj, float r_m[16])
{
	MT_Transform trans;
	trans.setOrigin(obj->NodeGetWorldPosition());
	trans.setBasis(obj->NodeGetWorldOrientation());
	const MT_Vector3 &s = obj->NodeGetWorldScaling();
	trans.scale(s[0], s[1], s[2]);
	trans.getValue(r_m);
}

/* Halo (screen) and Billboard (axis) faces: the same rotation as
 * RAS_OpenGLRasterizer::applyTransform. The polygons' normal is +X. */
static void billboard_matrix(KX_GameObject *obj, const MT_Point3 &campos, bool screen, float r_m[16])
{
	const MT_Point3 objpos = obj->NodeGetWorldPosition();
	MT_Vector3 dir = (campos - objpos).safe_normalized();
	MT_Vector3 up(0.0, 0.0, 1.0);
	const MT_Vector3 size = obj->NodeGetWorldScaling();
	if (screen) {
		up = (up - up.dot(dir) * dir).safe_normalized();
	}
	else {
		dir = (dir - up.dot(dir) * up).safe_normalized();
	}
	MT_Vector3 left = dir.safe_normalized();
	dir = up.cross(left).safe_normalized();
	left *= size[0];
	dir *= size[1];
	up *= size[2];
	const float m[16] = {(float)left[0], (float)left[1], (float)left[2], 0.0f,
	                     (float)dir[0],  (float)dir[1],  (float)dir[2],  0.0f,
	                     (float)up[0],   (float)up[1],   (float)up[2],   0.0f,
	                     (float)objpos[0], (float)objpos[1], (float)objpos[2], 1.0f};
	memcpy(r_m, m, sizeof(m));
}

static uint64_t hash_bytes(const void *data, size_t n, uint64_t h)
{
	const unsigned char *p = (const unsigned char *)data;
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

/* "1,5,20" holds 5 */
static bool frame_listed(const std::string &list, int frame)
{
	if (list.empty()) {
		return false;
	}
	const std::string l = "," + list + ",";
	char buf[24];
	snprintf(buf, sizeof(buf), ",%d,", frame);
	return l.find(buf) != std::string::npos;
}

/* ------------------------------------------------------------------------- */
/* Settings */

KX_RemixRenderer::Settings KX_RemixRenderer::GetSettings(KX_Scene *scene, int override_enabled, int override_fast)
{
	Settings s = {0, 0, DEFAULT_LIGHT_SCALE, DEFAULT_EXPOSURE};
	Scene *bscene = scene ? scene->GetBlenderScene() : NULL;
	IDProperty *props = bscene ? bscene->id.properties : NULL;
	/* P5: a scene rendered with the RTX Remix engine plays with Remix too */
	s.enabled = idprop_int(props, "bge_remix", 0) != 0 || (bscene && strcmp(bscene->r.engine, "RTX_REMIX") == 0);
	s.window_mode = idprop_int(props, "bge_remix_fast", 0) != 0;
	s.light_scale = idprop_float(props, "bge_remix_light_scale", DEFAULT_LIGHT_SCALE);
	s.exposure = idprop_float(props, "bge_remix_exposure", DEFAULT_EXPOSURE);
	if (override_enabled >= 0) {
		s.enabled = override_enabled != 0;
	}
	if (override_fast >= 0) {
		s.window_mode = override_fast != 0;
	}
	if (!(s.light_scale >= 0.0f) || s.light_scale > 1e6f) {
		s.light_scale = DEFAULT_LIGHT_SCALE;
	}
	if (!(s.exposure >= -10.0f && s.exposure <= 10.0f)) {
		s.exposure = DEFAULT_EXPOSURE;
	}
	return s;
}

/* ------------------------------------------------------------------------- */
/* Life time */

KX_RemixRenderer::KX_RemixRenderer(RAS_ICanvas *canvas, bool window_mode_allowed)
    : m_bridge(NULL),
      m_canvas(canvas),
      m_window_mode_allowed(window_mode_allowed),
      m_tried(false),
      m_running(false),
      m_mode(BRB_MODE_NONE),
      m_scene(NULL),
      m_width(0),
      m_height(0),
      m_x(-1),
      m_y(-1),
      m_shown(true),
      m_direct_frames_base(0),
      m_notice_until(0.0),
      m_notice_shown(false),
      m_filters_noted(false),
      m_frames(0),
      m_last_draws(0),
      m_last_lights(0),
      m_screenshot_frame(120)
{
	m_cache = new brmx::TextureCache();
	m_materials_waiting = 0;
	m_materials_swapped = 0;
	m_material_notes = 0;
	m_bakes = m_rebakes = 0;
	m_bake_ms = 0.0;
	m_light_frame = 0;
	m_bakes_checked = 0.0;
	for (int i = 0; i < 16; i++) {
		m_bake_obmat[i] = (i % 5 == 0) ? 1.0f : 0.0f;
	}
	m_max_swap_ms = 0.0;
	m_frame_ms_max = 0.0;
	m_last_render = 0.0;
	m_last_end = 0.0;
	m_first_textured_frame = -1;
	m_first_textured_seconds = 0.0;
	m_start_time = 0.0;
	m_settings.enabled = 0;
	m_settings.window_mode = 0;
	m_settings.light_scale = DEFAULT_LIGHT_SCALE;
	m_settings.exposure = DEFAULT_EXPOSURE;
	std::wstring f;
	if (env_path(L"BGE_REMIX_SCREENSHOT_FRAME", f)) {
		m_screenshot_frame = _wtoi(f.c_str());
	}
	m_status = "off";
	/* P4 */
	m_gpu_scene = NULL;
	m_cpu_skinning = env_path(L"BGE_REMIX_CPU_SKINNING", f) && f == L"1";
	if (env_path(L"BGE_REMIX_SKIN_CHECK", f)) {
		m_skin_check.assign(f.begin(), f.end());
	}
	m_colliders = false;
	m_collider_bounce = 0.5f;
	m_gravity = 9.81f;
	m_next_pick = 0;
	m_pick_wanted = m_pick_pending = m_pick_has = false;
	m_pick_x = m_pick_y = m_pick_req_x = m_pick_req_y = m_pick_ans_x = m_pick_ans_y = -1;
	m_pick_answer = NULL;
	m_pick_sent_frame = 0;
	m_highlight_color[0] = 1.0f;
	m_highlight_color[1] = 0.6f;
	m_highlight_color[2] = 0.1f;
	m_vp_x0 = m_vp_y0 = 0;
	m_skinned_objects = m_deformed_objects = m_billboards = m_emitters = m_particle_draws = 0;
	m_deform_uploads = m_cpu_skin_notes = m_shadow_faces = m_picks_answered = 0;
	m_pick_frames_total = m_pick_frames_count = 0;
	m_skin_ms_total = 0.0;
	m_skin_ms_frames = 0;
}

KX_RemixRenderer::~KX_RemixRenderer()
{
	Trace("game end: writing stats");
	WriteStats();
	if (m_bridge && m_running) {
		/* Remix keeps running for the next game (and the 3D view, P5); this
		 * game's objects go. */
		BRMX_lock();
		ShowRemixWindow(false);
		Trace("game end: clearing the Remix scene");
		m_bridge->scene_clear();
		BRMX_owner_release(this);
		BRMX_unlock();
	}
	Trace("game end: freeing the picture texture");
	BRMX_present_free();
	delete m_cache;
	Trace("game end: Remix renderer closed");
}

void KX_RemixRenderer::SetNotice(const std::string &text)
{
	m_notice = text;
	m_notice_until = 0.0; /* set when first shown */
	m_notice_shown = false;
	printf("BGE-Remix: %s\n", text.c_str());
	fflush(stdout);
}

const char *KX_RemixRenderer::GetNotice(double now)
{
	if (m_notice.empty()) {
		return NULL;
	}
	if (!m_notice_shown) {
		m_notice_shown = true;
		m_notice_until = now + NOTICE_SECONDS;
	}
	return (now < m_notice_until) ? m_notice.c_str() : NULL;
}

void KX_RemixRenderer::NoteSkippedFilters()
{
	if (!m_filters_noted) {
		m_filters_noted = true;
		SetNotice("2D filters are off while RTX Remix renders this scene.");
	}
}

bool KX_RemixRenderer::Start(KX_Scene *scene, int width, int height, const Settings &settings)
{
	m_tried = true;
	m_bridge = BRMX_bridge();
	if (!m_bridge) {
		m_status = std::string("no bridge: ") + BRMX_bridge_error();
		SetNotice("RTX Remix is not available in this build (" + std::string(BRMX_bridge_error()) +
		          "); the game runs in OpenGL.");
		return false;
	}
	int status = BRMX_ensure_loaded();
	if (status != BRB_STATUS_READY && status != BRB_STATUS_RUNNING) {
		m_status = m_bridge->status_text();
		SetNotice(std::string(m_bridge->status_text()) + " The game runs in OpenGL.");
		return false;
	}

	int mode = BRB_MODE_COPY;
	void *parent = NULL;
	int x = 0, y = 0;
	if (settings.window_mode && m_window_mode_allowed) {
		HDC dc = wglGetCurrentDC();
		HWND hwnd = dc ? WindowFromDC(dc) : NULL;
		if (hwnd) {
			mode = BRB_MODE_CHILD;
			parent = hwnd;
		}
	}
	else if (settings.window_mode) {
		SetNotice("RTX Remix window mode works in blenderplayer only; using copy mode.");
	}
	const char *copy_reason = "";
	if (mode == BRB_MODE_COPY && BRMX_present_direct_available(&copy_reason)) {
		/* P4.8: OpenGL draws Remix's own image; nothing is copied */
		mode = BRB_MODE_DIRECT;
	}
	else if (mode == BRB_MODE_COPY) {
		m_direct_note = copy_reason ? copy_reason : "";
		printf("BGE-Remix: copy mode (%s).\n", m_direct_note.c_str());
		fflush(stdout);
	}

	/* An earlier game in this Blender session started Remix in copy or direct
	 * mode: Remix cannot change between them, so this game uses the same one. */
	if (status == BRB_STATUS_RUNNING && mode != m_bridge->get_mode() &&
	    (mode == BRB_MODE_COPY || mode == BRB_MODE_DIRECT) &&
	    (m_bridge->get_mode() == BRB_MODE_COPY ||
	     (m_bridge->get_mode() == BRB_MODE_DIRECT && BRMX_present_direct_available(NULL))))
	{
		mode = m_bridge->get_mode();
	}

	/* Remix may still be running from the test in Preferences, in its own
	 * window. Unloading and loading the runtime again in one process is not
	 * known to be safe, so the game stays in OpenGL this time. */
	if (status == BRB_STATUS_RUNNING && m_bridge->get_mode() != mode) {
		m_status = "running in another mode";
		SetNotice("RTX Remix is already running for the Preferences test; restart Blender to use it in the game. "
		          "The game runs in OpenGL.");
		return false;
	}
	/* P5: Remix may be in use by the 3D view (its objects go) or by F12 (the
	 * game waits for it to finish) */
	BRMX_lock();
	if (BRMX_owner_kind() == BRMX_OWNER_RENDER || G.is_rendering) {
		BRMX_unlock();
		m_status = "busy rendering";
		SetNotice("RTX Remix is rendering (F12); the game runs in OpenGL.");
		return false;
	}
	status = m_bridge->startup_ex(mode, width, height, parent, x, y);
	if (status != BRB_STATUS_RUNNING) {
		BRMX_unlock();
		m_status = m_bridge->status_text();
		SetNotice(std::string(m_bridge->status_text()) + " The game runs in OpenGL.");
		return false;
	}
	BRMX_owner_take(BRMX_OWNER_GAME, this, NULL);
	m_bridge->reset_stats();
	m_direct_frames_base = BRMX_present_direct_frames();
	ApplyExposure(settings);
	{
		/* P5.1: the scene's preset, DLSS mode, ray reconstruction, volumetrics */
		BRMX_Quality quality;
		BRMX_quality_from_scene(scene->GetBlenderScene(), &quality);
		BRMX_quality_apply(&quality);
	}
	BRMX_unlock();
	m_running = true;
	m_start_time = now_seconds();
	m_mode = mode;
	m_scene = scene;
	m_settings = settings;
	m_width = width;
	m_height = height;
	const char *mode_name = (mode == BRB_MODE_COPY) ? "copy mode" : (mode == BRB_MODE_DIRECT) ? "direct mode" :
	                                                                                           "window mode";
	m_status = std::string("running (") + mode_name + ")";
	printf("BGE-Remix: RTX Remix renders scene \"%s\" (%s, %dx%d).\n", scene->GetName().ReadPtr(),
	       mode_name, width, height);
	fflush(stdout);
	return true;
}

void KX_RemixRenderer::ApplyExposure(const Settings &settings)
{
	/* stops on top of Remix's automatic exposure (source/blender/remix/intern/remix_light.cc) */
	brmx::apply_exposure(m_bridge, settings.exposure);
}

void KX_RemixRenderer::Trace(const char *what)
{
	/* for tests (BGE_REMIX_TRACE): where a crash at the end of a game happens */
	static int on = -1;
	if (on < 0) {
		std::wstring v;
		on = env_path(L"BGE_REMIX_TRACE", v) ? 1 : 0;
	}
	if (on) {
		printf("BGE-Remix trace: %s\n", what);
		fflush(stdout);
	}
}

void KX_RemixRenderer::ShowRemixWindow(bool show)
{
	if (m_running && m_mode == BRB_MODE_CHILD && m_shown != show) {
		m_bridge->show(show ? 1 : 0);
		m_shown = show;
	}
}

KX_Scene *KX_RemixRenderer::PickScene(const std::vector<KX_Scene *> &scenes, bool stereo, bool dome,
                                      int override_enabled, int override_fast)
{
	KX_Scene *picked = PickSceneImpl(scenes, stereo, dome, override_enabled, override_fast);
	if (!picked) {
		/* window mode: OpenGL shows this frame, not the last Remix picture */
		ShowRemixWindow(false);
	}
	if (m_gpu_scene && m_gpu_scene != picked) {
		/* OpenGL draws that scene now: BGE skins its characters again */
		if (std::find(scenes.begin(), scenes.end(), m_gpu_scene) != scenes.end()) {
			ReleaseSkinning(m_gpu_scene);
		}
		m_gpu_scene = NULL;
	}
	return picked;
}

KX_Scene *KX_RemixRenderer::PickSceneImpl(const std::vector<KX_Scene *> &scenes, bool stereo, bool dome,
                                          int override_enabled, int override_fast)
{
	if (m_running) {
		/* the scene keeps Remix as long as it exists */
		for (KX_Scene *s : scenes) {
			if (s == m_scene) {
				return m_scene;
			}
		}
		m_scene = NULL;
	}

	/* The first scene (bottom of the stack) with Remix on gets it. Only one:
	 * Remix draws one picture per frame. */
	KX_Scene *wanted = NULL;
	Settings settings = {0, 0, DEFAULT_LIGHT_SCALE, DEFAULT_EXPOSURE};
	bool first = true;
	for (KX_Scene *s : scenes) {
		/* the -g override applies to the first scene only */
		settings = GetSettings(s, first ? override_enabled : -1, first ? override_fast : -1);
		first = false;
		if (settings.enabled) {
			wanted = s;
			break;
		}
	}
	if (!wanted) {
		return NULL;
	}
	if (m_running) {
		/* another scene asks for Remix after the first one went */
		m_scene = wanted;
		m_settings = settings;
		ApplyExposure(settings);
		return m_scene;
	}
	if (m_tried) {
		return NULL;
	}
	if (stereo || dome) {
		m_tried = true;
		m_status = "stereo or dome";
		SetNotice("RTX Remix does not support stereo or dome rendering; the game runs in OpenGL.");
		return NULL;
	}
	const int w = std::max(m_canvas->GetWidth(), 8);
	const int h = std::max(m_canvas->GetHeight(), 8);
	return Start(wanted, w, h, settings) ? m_scene : NULL;
}

void KX_RemixRenderer::RemoveScene(KX_Scene *scene)
{
	if (!m_bridge) {
		return;
	}
	RemixLock lock;
	for (auto it = m_meshes.begin(); it != m_meshes.end();) {
		if (it->second.scene == scene) {
			if (m_running && it->second.hash) {
				m_bridge->mesh_destroy(it->second.hash);
			}
			m_hashes.erase(it->second.hash);
			it = m_meshes.erase(it);
		}
		else {
			++it;
		}
	}
	for (auto it = m_skins.begin(); it != m_skins.end();) {
		if (it->second.scene == scene) {
			if (m_running && it->second.hash) {
				m_bridge->mesh_destroy(it->second.hash);
			}
			m_hashes.erase(it->second.hash);
			it = m_skins.erase(it);
		}
		else {
			++it;
		}
	}
	for (auto it = m_deformed.begin(); it != m_deformed.end();) {
		if (it->second.scene == scene) {
			if (m_running && it->second.hash) {
				m_bridge->mesh_destroy(it->second.hash);
			}
			m_hashes.erase(it->second.hash);
			it = m_deformed.erase(it);
		}
		else {
			++it;
		}
	}
	if (scene == m_gpu_scene) {
		m_gpu_scene = NULL;
	}
	if (scene == m_scene) {
		m_pick_ids.clear();
		m_pick_frame.clear();
		m_highlight.clear();
		m_pick_answer = NULL;
		m_pick_has = m_pick_pending = m_pick_wanted = false;
	}
	for (auto it = m_lights.begin(); it != m_lights.end();) {
		if (it->second.scene == scene) {
			if (m_running) {
				m_bridge->light_destroy(it->second.hash);
			}
			m_hashes.erase(it->second.hash);
			it = m_lights.erase(it);
		}
		else {
			++it;
		}
	}
	/* after the meshes that use them */
	for (auto it = m_materials.begin(); it != m_materials.end();) {
		if (it->second.scene == scene) {
			if (it->second.waiting) {
				m_materials_waiting--;
			}
			if (m_running) {
				m_bridge->material_destroy(it->second.hash);
			}
			m_hashes.erase(it->second.hash);
			it = m_materials.erase(it);
		}
		else {
			++it;
		}
	}
	if (scene == m_scene) {
		m_scene = NULL;
	}
}

/* ------------------------------------------------------------------------- */
/* Scene translation */

uint64_t KX_RemixRenderer::UniqueHash(const std::string &kind, const std::string &name, const void *key)
{
	/* Stable across runs (from names), so captures and mods can refer to
	 * them; made unique within the game if two things share a name. */
	uint64_t h = fnv1a(kind + ":" + name);
	for (int salt = 1;; salt++) {
		if (h == 0) {
			h = 1;
		}
		std::map<uint64_t, const void *>::iterator it = m_hashes.find(h);
		if (it == m_hashes.end() || it->second == key) {
			m_hashes[h] = key;
			return h;
		}
		char buf[16];
		snprintf(buf, sizeof(buf), "#%d", salt);
		h = fnv1a(kind + ":" + name + buf);
	}
}

const KX_RemixRenderer::MaterialEntry &KX_RemixRenderer::EnsureMaterial(KX_Scene *scene, RAS_IPolyMaterial *pm,
                                                                         RAS_MeshObject *mesh)
{
	const std::string name = pm ? pm->GetMaterialName().ReadPtr() : "__default__";
	Material *ma = pm ? pm->GetBlenderMaterial() : NULL;
	/* P8.6: procedural textures are baked on each mesh's UV map */
	const bool baked = mesh && brmx::bake_slots(ma) != 0;
	const std::pair<const void *, const void *> key(pm, baked ? (const void *)mesh : NULL);
	std::map<std::pair<const void *, const void *>, MaterialEntry>::iterator it = m_materials.find(key);
	if (it != m_materials.end()) {
		if (it->second.name == name) {
			return it->second;
		}
		/* a different material at the address of a freed one */
		m_bridge->material_destroy(it->second.hash);
		m_hashes.erase(it->second.hash);
		if (it->second.waiting) {
			m_materials_waiting--;
		}
		m_materials.erase(it);
	}

	/* the RTX Remix panel, Cycles nodes or Blender Internal settings
	 * (source/blender/remix/intern/remix_material.cc) */
	MaterialEntry e;
	e.spec.reset(new brmx::MaterialSpec());
	brmx::translate_material(ma, *e.spec);
	if (pm) {
		/* P8.4: Face Textures make one polymaterial per face image */
		brmx::apply_face_image(ma, pm->GetBlenderImage(), *e.spec);
	}
	e.spec->desc.hash = baked ? UniqueHash("material", name + "@" + mesh->GetName().ReadPtr(), &m_materials[key])
	                          : UniqueHash("material", name, pm);
	for (size_t i = 0; i < e.spec->notes.size(); i++) {
		printf("BGE-Remix: material \"%s\": %s\n", name.c_str(), e.spec->notes[i].c_str());
		m_material_notes++;
	}
	fflush(stdout);
	e.hash = e.spec->desc.hash;
	e.scene = scene;
	e.name = name;
	e.blend = e.spec->desc.alpha_blend != 0;
	e.particle = e.spec->particle;
	e.waiting = false;
	e.pm = pm;
	if (baked) {
		e.mesh = mesh;
		memcpy(e.obmat, m_bake_obmat, sizeof(e.obmat));
		Bake(e);
	}
	SendMaterial(e);
	MaterialEntry &stored = m_materials[key];
	stored = e;
	return stored;
}

/* P8.6: the procedural textures of a material, baked on its mesh (the mesh
 * as converted, where the first object using it was) */
void KX_RemixRenderer::Bake(MaterialEntry &e)
{
	Material *ma = e.pm ? e.pm->GetBlenderMaterial() : NULL;
	Mesh *me = e.mesh ? e.mesh->GetMesh() : NULL;
	brmx::BakeResult r;
	const double t0 = now_seconds();
	Trace("bake: the mesh");
	DerivedMesh *dm = brmx::bake_mesh_from(me);
	float obmat[4][4];
	memcpy(obmat, e.obmat, sizeof(obmat));
	const std::string name = e.name + "_" + e.mesh->GetName().ReadPtr();
	const bool ok = dm && brmx::bake_material(e.scene->GetBlenderScene(), ma, dm, e.pm->GetMaterialIndex() + 1, obmat,
	                                         name, r);
	Trace("bake: releasing the mesh");
	if (dm) {
		dm->release(dm);
	}
	e.bake_signature = brmx::bake_signature(ma);
	e.bake_checked = now_seconds();
	m_bake_ms += (e.bake_checked - t0) * 1000.0;
	if (ok) {
		brmx::apply_bake(r, *e.spec);
		printf("BGE-Remix: material \"%s\" on mesh \"%s\": procedural textures baked (%.0f ms)\n", e.name.c_str(),
		       e.mesh->GetName().ReadPtr(), (now_seconds() - t0) * 1000.0);
		m_bakes++;
	}
	else {
		NoteOnce("material \"" + e.name + "\" on mesh \"" + e.mesh->GetName().ReadPtr() + "\": " +
		         (r.error.empty() ? std::string("its procedural textures could not be baked") : r.error));
	}
	fflush(stdout);
}

/* P8.6: a texture changed while playing (an animation, a script): baked
 * again, a few times a second at most */
void KX_RemixRenderer::UpdateBakes(KX_Scene *scene)
{
	const double now = now_seconds();
	if (now - m_bakes_checked < 0.25) {
		return;
	}
	m_bakes_checked = now;
	for (std::map<std::pair<const void *, const void *>, MaterialEntry>::iterator it = m_materials.begin();
	     it != m_materials.end(); ++it)
	{
		MaterialEntry &e = it->second;
		if (!e.mesh || e.scene != scene || !e.pm) {
			continue;
		}
		Material *ma = e.pm->GetBlenderMaterial();
		if (brmx::bake_signature(ma) == e.bake_signature) {
			continue;
		}
		/* the translator again, then the bake, under the same hash */
		const uint64_t hash = e.hash;
		e.spec.reset(new brmx::MaterialSpec());
		brmx::translate_material(ma, *e.spec);
		brmx::apply_face_image(ma, e.pm->GetBlenderImage(), *e.spec);
		e.spec->desc.hash = hash;
		Bake(e);
		SendMaterial(e);
		m_rebakes++;
	}
}

void KX_RemixRenderer::SendMaterial(MaterialEntry &e)
{
	/* the textures that are ready; the others follow when they are encoded
	 * (UpdateTextures), so the game starts without waiting for them */
	brb_material_desc d = e.spec->desc;
	std::wstring paths[brmx::TEX_COUNT];
	bool waiting = false;
	for (int i = 0; i < brmx::TEX_COUNT; i++) {
		const brmx::TextureRequest &req = e.spec->textures[i];
		if (!req.wanted()) {
			continue;
		}
		const brmx::TextureCache::State st = m_cache->request(req, paths[i]);
		if (st != brmx::TextureCache::READY) {
			paths[i].clear();
			waiting = waiting || st == brmx::TextureCache::WAITING;
		}
	}
	const wchar_t **slots[brmx::TEX_COUNT] = {&d.albedo_texture, &d.normal_texture, &d.roughness_texture,
	                                         &d.metallic_texture, &d.emissive_texture};
	for (int i = 0; i < brmx::TEX_COUNT; i++) {
		*slots[i] = paths[i].empty() ? NULL : paths[i].c_str();
	}
	m_bridge->material_create(&d);
	if (waiting != e.waiting) {
		m_materials_waiting += waiting ? 1 : -1;
	}
	e.waiting = waiting;
}

void KX_RemixRenderer::UpdateTextures(KX_Scene *scene)
{
	if (m_materials_waiting <= 0) {
		return;
	}
	m_cache->poll();
	const double t0 = now_seconds();
	bool swapped = false;
	for (std::map<std::pair<const void *, const void *>, MaterialEntry>::iterator it = m_materials.begin();
	     it != m_materials.end(); ++it)
	{
		MaterialEntry &e = it->second;
		if (!e.waiting || e.scene != scene) {
			continue;
		}
		/* sent again (same hash: Remix replaces it) once nothing is waiting */
		bool still = false;
		for (int i = 0; i < brmx::TEX_COUNT && !still; i++) {
			std::wstring path;
			if (e.spec->textures[i].wanted() &&
			    m_cache->request(e.spec->textures[i], path) == brmx::TextureCache::WAITING)
			{
				still = true;
			}
		}
		if (!still) {
			SendMaterial(e);
			m_materials_swapped++;
			swapped = true;
		}
	}
	if (swapped) {
		m_max_swap_ms = std::max(m_max_swap_ms, (now_seconds() - t0) * 1000.0);
	}
}

KX_RemixRenderer::Geometry::Geometry()
    : bones_per_vertex(0),
      double_sided(false),
      blend(false),
      particle(false),
      billboard(0),
      shadow_faces(0),
      content(1469598103934665603ull),
      max_influences(0)
{
}

void KX_RemixRenderer::Collect(KX_Scene *scene, RAS_MeshObject *mesh, void *client, const void *weights_in,
                               Geometry &g, uint32_t identity_bone)
{
	typedef BL_SkinDeformer::RemixWeights RW;
	const std::vector<RW> *weights = (const std::vector<RW> *)weights_in;
	g = Geometry();
	std::vector<std::vector<const RW *> > vw;

	for (std::list<RAS_MeshMaterial>::iterator mit = mesh->GetFirstMaterial(); mit != mesh->GetLastMaterial(); ++mit) {
		RAS_MeshSlot *slot = mit->m_baseslot;
		if (client) {
			/* the object's own copy, where its deformer writes */
			RAS_MeshSlot **sp = mit->m_slots[client];
			if (sp && *sp) {
				slot = *sp;
			}
		}
		RAS_IPolyMaterial *pm = mit->m_bucket ? mit->m_bucket->GetPolyMaterial() : NULL;
		if (!slot) {
			continue;
		}
		const int mode = pm ? pm->GetDrawingMode() : 0;
		if (mode & RAS_IPolyMaterial::SHADOW) {
			/* fake blob shadows, cast to the ground in OpenGL: Remix casts real ones */
			g.shadow_faces++;
			continue;
		}
		std::vector<brb_vertex> v;
		std::vector<uint32_t> idx;
		std::vector<const RW *> w;
		RAS_MeshSlot::iterator it;
		for (slot->begin(it); !slot->end(it); slot->next(it)) {
			const uint32_t base = (uint32_t)v.size();
			const size_t count = it.endvertex;
			for (size_t i = 0; i < count; i++) {
				const RAS_TexVert &tv = it.vertex[i];
				brb_vertex o;
				memcpy(o.position, tv.getXYZ(), sizeof(o.position));
				memcpy(o.normal, tv.getNormal(), sizeof(o.normal));
				memcpy(o.uv, tv.getUV(0), sizeof(o.uv));
				memcpy(&o.color, tv.getRGBA(), sizeof(o.color));
				v.push_back(o);
				g.content = hash_bytes(o.position, sizeof(o.position) + sizeof(o.normal), g.content);
				if (weights) {
					const unsigned int orig = tv.getOrigIndex();
					w.push_back(orig < weights->size() ? &(*weights)[orig] : NULL);
				}
			}
			const int type = it.array->m_type;
			for (size_t i = 0; i + (size_t)type <= it.totindex; i += type) {
				const unsigned short *q = it.index + i;
				bool ok = true;
				for (int k = 0; k < type; k++) {
					ok = ok && q[k] < count;
				}
				if (!ok) {
					continue;
				}
				if (type == RAS_DisplayArray::TRIANGLE) {
					idx.push_back(base + q[0]);
					idx.push_back(base + q[1]);
					idx.push_back(base + q[2]);
				}
				else if (type == RAS_DisplayArray::QUAD) {
					/* as OpenGL draws a quad: 0 1 2, 0 2 3 */
					idx.push_back(base + q[0]);
					idx.push_back(base + q[1]);
					idx.push_back(base + q[2]);
					idx.push_back(base + q[0]);
					idx.push_back(base + q[2]);
					idx.push_back(base + q[3]);
				}
			}
		}
		if (idx.empty()) {
			continue;
		}
		Material *ma = pm ? pm->GetBlenderMaterial() : NULL;
		if (!ma || !(ma->game.flag & GEMAT_BACKCULL)) {
			g.double_sided = true;
		}
		/* see-through as the translator decided (game settings, Cycles'
		 * Transparent BSDF, the RTX Remix panel) */
		const MaterialEntry &me = EnsureMaterial(scene, pm, mesh);
		g.materials.push_back(me.hash);
		g.blend = g.blend || me.blend;
		g.particle = g.particle || me.particle;
		if (mode & RAS_IPolyMaterial::BILLBOARD_SCREENALIGNED) {
			g.billboard = 1;
		}
		else if ((mode & RAS_IPolyMaterial::BILLBOARD_AXISALIGNED) && g.billboard == 0) {
			g.billboard = 2;
		}
		g.verts.push_back(v);
		g.indices.push_back(idx);
		vw.push_back(w);
	}

	if (!weights) {
		return;
	}
	/* the fewest bones per vertex that hold every vertex's influences */
	uint32_t bpv = 1;
	for (size_t s = 0; s < vw.size(); s++) {
		for (size_t i = 0; i < vw[s].size(); i++) {
			const RW *r = vw[s][i];
			if (r) {
				g.max_influences = std::max(g.max_influences, r->count);
				bpv = std::max(bpv, (uint32_t)std::min(std::max(r->count, 1), 4));
			}
		}
	}
	g.bones_per_vertex = bpv;
	g.weights.resize(vw.size());
	g.bones.resize(vw.size());
	for (size_t s = 0; s < vw.size(); s++) {
		g.weights[s].assign(vw[s].size() * bpv, 0.0f);
		g.bones[s].assign(vw[s].size() * bpv, 0u);
		for (size_t i = 0; i < vw[s].size(); i++) {
			const RW *r = vw[s][i];
			for (uint32_t k = 0; k < bpv; k++) {
				g.weights[s][i * bpv + k] = r ? r->weights[k] : (k == 0 ? 1.0f : 0.0f);
				/* no weights: the identity bone, the last one */
				g.bones[s][i * bpv + k] = r ? r->bones[k] : identity_bone;
			}
			if (bpv < 4 && r) {
				/* the kept weights add up to 1 again */
				float sum = 0.0f;
				for (uint32_t k = 0; k < bpv; k++) {
					sum += g.weights[s][i * bpv + k];
				}
				if (sum > 0.0f) {
					for (uint32_t k = 0; k < bpv; k++) {
						g.weights[s][i * bpv + k] /= sum;
					}
				}
			}
		}
	}
}

/* A modifier deformer's result (Subdivision Surface, Mirror...): BGE draws
 * the derived mesh itself instead of the vertex arrays. */
void KX_RemixRenderer::CollectDerived(KX_Scene *scene, RAS_MeshObject *mesh, DerivedMesh *dm, Geometry &g)
{
	g = Geometry();
	DM_ensure_tessface(dm);
	const int nf = dm->getNumTessFaces(dm);
	const MFace *faces = dm->getTessFaceArray(dm);
	const MVert *mv = dm->getVertArray(dm);
	const int nv = dm->getNumVerts(dm);
	const MTFace *tf = (const MTFace *)dm->getTessFaceDataArray(dm, CD_MTFACE);
	if (!faces || !mv) {
		return;
	}
	for (std::list<RAS_MeshMaterial>::iterator mit = mesh->GetFirstMaterial(); mit != mesh->GetLastMaterial(); ++mit) {
		RAS_IPolyMaterial *pm = mit->m_bucket ? mit->m_bucket->GetPolyMaterial() : NULL;
		const int mode = pm ? pm->GetDrawingMode() : 0;
		if (mode & RAS_IPolyMaterial::SHADOW) {
			g.shadow_faces++;
			continue;
		}
		const int matnr = pm ? pm->GetMaterialIndex() : 0;
		Material *bma = pm ? pm->GetBlenderMaterial() : NULL;
		if (bma && (bma->game.flag & GEMAT_INVISIBLE)) {
			continue; /* P8.8: Invisible faces are not drawn (the game leaves them out of its arrays) */
		}
		std::vector<brb_vertex> v;
		std::vector<uint32_t> idx;
		for (int f = 0; f < nf; f++) {
			const MFace &mf = faces[f];
			if (mf.mat_nr != matnr) {
				continue;
			}
			const unsigned int corner[4] = {mf.v1, mf.v2, mf.v3, mf.v4};
			const int n = mf.v4 ? 4 : 3;
			bool ok = true;
			for (int k = 0; k < n; k++) {
				ok = ok && (int)corner[k] < nv;
			}
			if (!ok) {
				continue;
			}
			float fno[3];
			if (n == 4) {
				normal_quad_v3(fno, mv[corner[0]].co, mv[corner[1]].co, mv[corner[2]].co, mv[corner[3]].co);
			}
			else {
				normal_tri_v3(fno, mv[corner[0]].co, mv[corner[1]].co, mv[corner[2]].co);
			}
			const uint32_t base = (uint32_t)v.size();
			for (int k = 0; k < n; k++) {
				brb_vertex o;
				memcpy(o.position, mv[corner[k]].co, sizeof(o.position));
				if (mf.flag & ME_SMOOTH) {
					normal_short_to_float_v3(o.normal, mv[corner[k]].no);
				}
				else {
					memcpy(o.normal, fno, sizeof(o.normal));
				}
				o.uv[0] = tf ? tf[f].uv[k][0] : 0.0f;
				o.uv[1] = tf ? tf[f].uv[k][1] : 0.0f;
				o.color = 0xFFFFFFFFu;
				v.push_back(o);
				g.content = hash_bytes(o.position, sizeof(o.position) + sizeof(o.normal), g.content);
			}
			idx.push_back(base);
			idx.push_back(base + 1);
			idx.push_back(base + 2);
			if (n == 4) {
				idx.push_back(base);
				idx.push_back(base + 2);
				idx.push_back(base + 3);
			}
		}
		if (idx.empty()) {
			continue;
		}
		Material *ma = pm ? pm->GetBlenderMaterial() : NULL;
		if (!ma || !(ma->game.flag & GEMAT_BACKCULL)) {
			g.double_sided = true;
		}
		const MaterialEntry &me = EnsureMaterial(scene, pm, mesh);
		g.materials.push_back(me.hash);
		g.blend = g.blend || me.blend;
		g.particle = g.particle || me.particle;
		g.verts.push_back(v);
		g.indices.push_back(idx);
	}
}

bool KX_RemixRenderer::SendGeometry(uint64_t hash, const Geometry &g)
{
	const size_t n = g.verts.size();
	std::vector<brb_surface_desc> surfaces(n);
	std::vector<brb_skin_desc> skins(n);
	for (size_t s = 0; s < n; s++) {
		surfaces[s].vertices = g.verts[s].data();
		surfaces[s].vertex_count = (uint32_t)g.verts[s].size();
		surfaces[s].indices = g.indices[s].data();
		surfaces[s].index_count = (uint32_t)g.indices[s].size();
		surfaces[s].material = g.materials[s];
		surfaces[s].skin = NULL;
		if (g.bones_per_vertex && s < g.weights.size()) {
			skins[s].bones_per_vertex = g.bones_per_vertex;
			skins[s].weights = g.weights[s].data();
			skins[s].bones = g.bones[s].data();
			surfaces[s].skin = &skins[s];
		}
	}
	return !surfaces.empty() && m_bridge->mesh_create(hash, surfaces.data(), (uint32_t)surfaces.size()) == 0;
}

bool KX_RemixRenderer::EnsureMesh(KX_Scene *scene, RAS_MeshObject *mesh, MeshEntry *&r_entry)
{
	const std::string name = mesh->GetName().ReadPtr();
	const int polygons = mesh->NumPolygons();
	std::map<const void *, MeshEntry>::iterator found = m_meshes.find(mesh);
	if (found != m_meshes.end()) {
		if (found->second.name == name && found->second.polygons == polygons) {
			r_entry = &found->second;
			return found->second.hash != 0; /* 0: nothing to draw */
		}
		/* a different mesh at the address of a freed one */
		if (found->second.hash) {
			m_bridge->mesh_destroy(found->second.hash);
			m_hashes.erase(found->second.hash);
		}
		m_meshes.erase(found);
	}

	Geometry g;
	Collect(scene, mesh, NULL, NULL, g);
	if (g.shadow_faces) {
		m_shadow_faces += g.shadow_faces;
		NoteOnce("mesh \"" + name + "\": Shadow faces are left out (Remix casts real shadows)");
	}

	MeshEntry e;
	e.hash = UniqueHash("mesh", name, mesh);
	e.scene = scene;
	e.polygons = polygons;
	e.name = name;
	e.double_sided = g.double_sided;
	e.blend = g.blend;
	e.particle = g.particle;
	e.billboard = g.billboard;
	if (!SendGeometry(e.hash, g)) {
		/* nothing to draw; remembered so it is not tried every frame */
		e.hash = 0;
	}
	r_entry = &(m_meshes[mesh] = e);
	return e.hash != 0;
}

/* ------------------------------------------------------------------------- */
/* Moving content (P4) */

void KX_RemixRenderer::NoteOnce(const std::string &text)
{
	if (m_noted.insert(text).second) {
		printf("BGE-Remix: %s\n", text.c_str());
		fflush(stdout);
	}
}

bool KX_RemixRenderer::ObjectAlive(KX_GameObject *obj)
{
	return obj && m_scene && m_scene->GetObjectList()->SearchValue(obj);
}

bool KX_RemixRenderer::DrawSkinned(KX_Scene *scene, KX_GameObject *obj, RAS_Deformer *def, const float model[16],
                                   uint32_t pick)
{
	/* only the plain armature deformer: shape keys and modifiers subclass it */
	if (m_cpu_skinning || typeid(*def) != typeid(BL_SkinDeformer)) {
		return false;
	}
	BL_SkinDeformer *skin = static_cast<BL_SkinDeformer *>(def);
	std::string why;
	if (!skin->RemixCanSkin(why)) {
		if (m_noted.find("skin:" + std::string(obj->GetName().ReadPtr())) == m_noted.end()) {
			m_noted.insert("skin:" + std::string(obj->GetName().ReadPtr()));
			printf("BGE-Remix: object \"%s\": skinned on the CPU (%s)\n", obj->GetName().ReadPtr(), why.c_str());
			fflush(stdout);
			m_cpu_skin_notes++;
		}
		return false;
	}
	RAS_MeshObject *mesh = obj->GetMesh(0);
	if (!mesh || !skin->RemixArmature()) {
		return false;
	}
	/* Armature copies (BGE gives each armature object its own data) still
	 * share the mesh when the deform groups match: bones are sent per draw. */
	const std::string name = mesh->GetName().ReadPtr();
	const std::pair<std::string, const void *> key(name + "|" + skin->RemixGroupKey(), scene);
	const int polygons = mesh->NumPolygons();
	std::map<std::pair<std::string, const void *>, SkinEntry>::iterator found = m_skins.find(key);
	if (found != m_skins.end() && (found->second.name != name || found->second.polygons != polygons)) {
		if (found->second.hash) {
			m_bridge->mesh_destroy(found->second.hash);
			m_hashes.erase(found->second.hash);
		}
		m_skins.erase(found);
		found = m_skins.end();
	}
	if (found == m_skins.end()) {
		std::vector<BL_SkinDeformer::RemixWeights> weights;
		if (!skin->RemixVertexWeights(weights)) {
			return false;
		}
		SkinEntry &e = m_skins[key];
		Geometry g;
		Collect(scene, mesh, NULL, &weights, g, (uint32_t)skin->RemixGroupCount());
		e.hash = UniqueHash("skin", name, &e);
		e.scene = scene;
		e.name = name;
		e.polygons = polygons;
		e.groups = skin->RemixGroupCount();
		e.double_sided = g.double_sided;
		e.blend = g.blend;
		e.particle = g.particle;
		if (g.max_influences > 4) {
			NoteOnce("mesh \"" + name + "\": vertices with more than 4 bones keep their strongest 4");
		}
		if (!SendGeometry(e.hash, g)) {
			e.hash = 0;
		}
		found = m_skins.find(key);
	}
	const SkinEntry &e = found->second;
	if (!e.hash) {
		return false;
	}
	skin->SetRemixGPU(true);
	m_gpu_scene = scene;

	std::vector<float> bones;
	skin->RemixBoneMatrices(bones);
	brb_draw_desc d;
	memset(&d, 0, sizeof(d));
	d.mesh = e.hash;
	memcpy(d.model, model, sizeof(d.model));
	d.flags = (e.double_sided ? BRB_DRAW_DOUBLE_SIDED : 0) | (e.blend ? BRB_DRAW_BLEND : 0);
	d.categories = e.particle ? BRB_CATEGORY_PARTICLE : 0;
	d.pick_id = pick;
	d.bones = bones.data();
	d.bone_count = (uint32_t)(bones.size() / 16);
	m_bridge->frame_draw_ex(&d);
	m_skinned_objects++;

	if (frame_listed(m_skin_check, m_frames)) {
		SkinCheck(obj, skin, e.hash, model, pick);
	}
	return true;
}

/* For T4.1b: BGE's own CPU skinning of this frame, world positions in the
 * order the fake runtime prints the GPU-skinned ones (first use in the
 * triangle lists). */
void KX_RemixRenderer::SkinCheck(KX_GameObject *obj, BL_SkinDeformer *skin, uint64_t mesh, const float model[16],
                                 uint32_t pick)
{
	/* the GPU formula on the CPU, from the same rest pose, weights and
	 * bones the bridge was given (tells a maths error from a layout one) */
	std::vector<BL_SkinDeformer::RemixWeights> weights;
	skin->RemixVertexWeights(weights);
	std::vector<float> bones;
	skin->RemixBoneMatrices(bones);
	Geometry rest;
	Collect(m_scene, obj->GetMesh(0), NULL, &weights, rest, (uint32_t)skin->RemixGroupCount());

	skin->SetRemixGPU(false);
	skin->ForceUpdate();
	skin->Update();
	Geometry g;
	Collect(m_scene, obj->GetMesh(0), obj, NULL, g);
	skin->SetRemixGPU(true);

	for (int pass = 0; pass < 2; pass++) {
		const Geometry &src = pass ? rest : g;
		std::string out;
		int n = 0;
		for (size_t s = 0; s < src.verts.size(); s++) {
			std::vector<char> seen(src.verts[s].size(), 0);
			/* Visit the corners in the order the bridge sends them: it swaps
			 * the 2nd and 3rd corner of every triangle for Remix's winding. */
			const std::vector<uint32_t> &ix = src.indices[s];
			for (size_t c = 0; c < ix.size(); c++) {
				const size_t base = c - c % 3, corner = c % 3;
				const size_t at = (base + 2 < ix.size() && corner) ? base + 3 - corner : c;
				const uint32_t v = ix[at];
				if (v >= seen.size() || seen[v]) {
					continue;
				}
				seen[v] = 1;
				float p[3];
				memcpy(p, src.verts[s][v].position, sizeof(p));
				if (pass) {
					const uint32_t bpv = src.bones_per_vertex;
					float acc[3] = {0.0f, 0.0f, 0.0f};
					for (uint32_t k = 0; k < bpv; k++) {
						const float wk = src.weights[s][v * bpv + k];
						const uint32_t b = src.bones[s][v * bpv + k];
						if (wk <= 0.0f || (size_t)(b + 1) * 16 > bones.size()) {
							continue;
						}
						const float *m = &bones[(size_t)b * 16];
						for (int r = 0; r < 3; r++) {
							acc[r] += wk * (m[0 * 4 + r] * p[0] + m[1 * 4 + r] * p[1] + m[2 * 4 + r] * p[2] + m[3 * 4 + r]);
						}
					}
					memcpy(p, acc, sizeof(p));
				}
				float w[3];
				for (int r = 0; r < 3; r++) {
					w[r] = model[0 * 4 + r] * p[0] + model[1 * 4 + r] * p[1] + model[2 * 4 + r] * p[2] + model[3 * 4 + r];
				}
				char buf[64];
				snprintf(buf, sizeof(buf), "%s%.4f,%.4f,%.4f", n ? ";" : "", w[0], w[1], w[2]);
				out += buf;
				n++;
			}
		}
		printf("BGE-Remix %s frame=%d mesh=%llx pick=%u object=%s n=%d pos=%s\n",
		       pass ? "skinformula" : "skincheck", m_frames, (unsigned long long)mesh, pick,
		       obj->GetName().ReadPtr(), n, out.c_str());
	}
	fflush(stdout);
}

bool KX_RemixRenderer::DrawDeformed(KX_Scene *scene, KX_GameObject *obj, RAS_Deformer *def, const float model[16],
                                    uint32_t pick)
{
	RAS_MeshObject *mesh = obj->GetMesh(0);
	if (!mesh) {
		return false;
	}
	static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	const float *m = model;
	/* bring BGE's own result up to date (it does nothing when it is) */
	if (BL_SkinDeformer *skin = dynamic_cast<BL_SkinDeformer *>(def)) {
		skin->SetRemixGPU(false);
	}
#ifdef WITH_BULLET
	if (KX_SoftBodyDeformer *soft = dynamic_cast<KX_SoftBodyDeformer *>(def)) {
		/* soft bodies: world-space vertices, written per material */
		for (std::list<RAS_MeshMaterial>::iterator mit = mesh->GetFirstMaterial(); mit != mesh->GetLastMaterial(); ++mit) {
			if (mit->m_bucket) {
				soft->Apply(mit->m_bucket->GetPolyMaterial());
			}
		}
		m = identity;
	}
	else
#endif
	{
		def->Update();
	}

	DeformEntry &e = m_deformed[obj];
	const std::string name = obj->GetName().ReadPtr();
	if (e.hash == 0 || e.name != name || e.scene != scene) {
		if (e.hash) {
			m_bridge->mesh_destroy(e.hash);
			m_hashes.erase(e.hash);
		}
		e.hash = UniqueHash("deformed", name, obj);
		e.scene = scene;
		e.name = name;
		e.content = 0;
	}
	e.last_seen = m_frames;
	/* a deformer that never changes its result (a static modifier stack)
	 * is read once */
	if (e.content == 0 || def->IsDynamic()) {
		Geometry g;
		BL_ModifierDeformer *mod = dynamic_cast<BL_ModifierDeformer *>(def);
		if (mod && !mod->UseVertexArray() && mod->GetFinalMesh()) {
			CollectDerived(scene, mesh, mod->GetFinalMesh(), g);
		}
		else {
			Collect(scene, mesh, obj, NULL, g);
		}
		if (g.verts.empty()) {
			return false;
		}
		if (e.content != g.content) {
			/* changed since it was sent: send again (Remix replaces it) */
			if (!SendGeometry(e.hash, g)) {
				e.content = 0;
				return false;
			}
			e.content = g.content;
			e.double_sided = g.double_sided;
			e.blend = g.blend;
			e.particle = g.particle;
			m_deform_uploads++;
		}
	}
	brb_draw_desc d;
	memset(&d, 0, sizeof(d));
	d.mesh = e.hash;
	memcpy(d.model, m, sizeof(d.model));
	d.flags = (e.double_sided ? BRB_DRAW_DOUBLE_SIDED : 0) | (e.blend ? BRB_DRAW_BLEND : 0);
	d.categories = e.particle ? BRB_CATEGORY_PARTICLE : 0;
	d.pick_id = pick;
	m_bridge->frame_draw_ex(&d);
	m_deformed_objects++;
	return true;
}

void KX_RemixRenderer::ReleaseSkinning(KX_Scene *scene)
{
	CListValue *objects = scene->GetObjectList();
	for (int i = 0; i < objects->GetCount(); i++) {
		KX_GameObject *obj = (KX_GameObject *)objects->GetValue(i);
		RAS_Deformer *def = obj->GetDeformer();
		if (def && typeid(*def) == typeid(BL_SkinDeformer)) {
			static_cast<BL_SkinDeformer *>(def)->SetRemixGPU(false);
		}
	}
}

/* Objects deformed on the CPU that were not drawn for a second: their
 * meshes go (the objects were ended, or are hidden). */
void KX_RemixRenderer::PruneDynamic(KX_Scene *scene)
{
	if (m_frames % 60 != 0) {
		return;
	}
	for (std::map<const void *, DeformEntry>::iterator it = m_deformed.begin(); it != m_deformed.end();) {
		if (it->second.scene == scene && it->second.last_seen < m_frames - 60) {
			if (it->second.hash) {
				m_bridge->mesh_destroy(it->second.hash);
				m_hashes.erase(it->second.hash);
			}
			m_deformed.erase(it++);
		}
		else {
			++it;
		}
	}
	for (std::map<const void *, PickEntry>::iterator it = m_pick_ids.begin(); it != m_pick_ids.end();) {
		if (it->second.last_seen < m_frames - 60) {
			m_pick_ids.erase(it++);
		}
		else {
			++it;
		}
	}
}

/* ------------------------------------------------------------------------- */
/* Blender particle systems as Remix GPU particles (P4.6) */

bool KX_RemixRenderer::DrawEmitters(KX_Scene *scene, KX_GameObject *obj, Object *ob, const float model[16],
                                    uint32_t pick)
{
	RAS_MeshObject *mesh = obj->GetMeshCount() ? obj->GetMesh(0) : NULL;
	MeshEntry *me = NULL;
	if (!mesh || !EnsureMesh(scene, mesh, me) || !me) {
		return false;
	}
	Scene *bscene = scene->GetBlenderScene();
	const float fps = (bscene && bscene->r.frs_sec > 0) ?
	                      (float)bscene->r.frs_sec / std::max(bscene->r.frs_sec_base, 0.001f) : 24.0f;
	bool any = false, hide = true;
	for (ParticleSystem *psys = (ParticleSystem *)ob->particlesystem.first; psys; psys = psys->next) {
		ParticleSettings *part = psys->part;
		if (!part || part->type != PART_EMITTER || (psys->flag & PSYS_DISABLED) || part->totpart <= 0) {
			continue;
		}
		brb_particle_desc p;
		memset(&p, 0, sizeof(p));
		/* emission: totpart over the emission frames, again and again */
		const float frames = std::max(part->end - part->sta, 1.0f);
		p.spawn_rate = (float)part->totpart / frames * fps;
		p.lifetime_max = std::max(part->lifetime, 1.0f) / fps;
		p.lifetime_min = p.lifetime_max * (1.0f - std::min(std::max(part->randlife, 0.0f), 1.0f));
		p.max_particles = (uint32_t)std::max<float>((float)part->totpart, p.spawn_rate * p.lifetime_max * 1.1f + 16.0f);
		p.speed_normal = part->normfac;
		p.cone_angle_deg = (float)(std::atan2((double)part->randfac, (double)std::max(std::fabs(part->normfac), 0.001f)) *
		                           180.0 / PI);
		p.cone_angle_deg = std::min(p.cone_angle_deg, 90.0f);
		p.speed_from_motion = part->obfac;
		const float gweight = part->effector_weights ? part->effector_weights->global_gravity : 1.0f;
		p.gravity = m_gravity * gweight;
		p.drag = part->dragfac;
		p.size_max = std::max(part->size, 0.001f);
		p.size_min = p.size_max * (1.0f - std::min(std::max(part->randsize, 0.0f), 1.0f));
		p.size_end_scale = 1.0f;
		const float start[4] = {1.0f, 1.0f, 1.0f, 1.0f}, end[4] = {1.0f, 1.0f, 1.0f, 0.0f};
		memcpy(p.color_start, start, sizeof(start));
		memcpy(p.color_end, end, sizeof(end)); /* fade out at the end of life */
		p.rotation_speed = part->avemode ? (float)(part->avefac / (2.0 * PI)) : 0.0f;
		p.collide = m_colliders ? 1 : 0;
		p.restitution = m_collider_bounce;
		p.hide_emitter = 1; /* the emitter itself is drawn by the normal draw (or not) */
		p.billboard = 0;
		if (part->ren_as == PART_DRAW_BB) {
			if (part->bb_align == PART_BB_Z) {
				p.billboard = 1;
			}
			else if (part->bb_align == PART_BB_VEL) {
				p.align_to_velocity = 1;
			}
		}
		else if (part->ren_as != PART_DRAW_HALO) {
			NoteOnce(std::string("particle system \"") + psys->name + "\": shown as sprites (Halo or Billboard rendering)");
		}
		if (part->draw & PART_DRAW_EMITTER) {
			hide = false;
		}
		brb_draw_desc d;
		memset(&d, 0, sizeof(d));
		d.mesh = me->hash;
		memcpy(d.model, model, sizeof(d.model));
		d.flags = me->double_sided ? BRB_DRAW_DOUBLE_SIDED : 0;
		d.categories = BRB_CATEGORY_PARTICLE_EMITTER;
		d.pick_id = pick;
		d.particles = &p;
		m_bridge->frame_draw_ex(&d);
		m_emitters++;
		any = true;
	}
	/* true: the emitter mesh is not to be drawn (Blender's "Emitter" off) */
	return any && hide;
}

/* ------------------------------------------------------------------------- */
/* Picking (P4.5) */

uint32_t KX_RemixRenderer::PickId(KX_GameObject *obj)
{
	PickEntry &p = m_pick_ids[obj];
	if (p.id == 0) {
		if (++m_next_pick == 0) {
			++m_next_pick;
		}
		p.id = m_next_pick;
	}
	p.last_seen = m_frames;
	m_pick_frame[p.id] = obj;
	return p.id;
}

void KX_RemixRenderer::UpdatePicking(KX_Scene *scene)
{
	(void)scene;
	/* an answer? */
	if (m_pick_pending) {
		uint32_t ids[16];
		int n = 0;
		const int r = m_bridge->pick_result(ids, 16, &n);
		if (r != 0) {
			m_pick_pending = false;
			m_pick_has = true;
			m_pick_ans_x = m_pick_req_x;
			m_pick_ans_y = m_pick_req_y;
			m_pick_answer = NULL;
			if (r == 1 && n > 0) {
				std::map<uint32_t, KX_GameObject *>::iterator it = m_pick_frame.find(ids[0]);
				m_pick_answer = (it != m_pick_frame.end()) ? it->second : NULL;
			}
			m_picks_answered++;
			m_pick_frames_total += m_frames - m_pick_sent_frame;
			m_pick_frames_count++;
		}
		else if (m_frames - m_pick_sent_frame > 30) {
			m_pick_pending = false; /* lost; ask again */
		}
	}
	/* a new question */
	if (m_pick_wanted && !m_pick_pending) {
		const int x = m_pick_x - m_vp_x0, y = m_pick_y - m_vp_y0;
		if (x >= 0 && y >= 0 && x < m_width && y < m_height &&
		    m_bridge->pick_request(x, y, x + 1, y + 1) == 0)
		{
			m_pick_pending = true;
			m_pick_req_x = m_pick_x;
			m_pick_req_y = m_pick_y;
			m_pick_sent_frame = m_frames;
		}
		m_pick_wanted = false;
	}
	/* outlines */
	if (!m_highlight.empty()) {
		std::vector<uint32_t> ids;
		for (size_t i = 0; i < m_highlight.size(); i++) {
			std::map<const void *, PickEntry>::iterator it = m_pick_ids.find(m_highlight[i]);
			if (it != m_pick_ids.end() && it->second.last_seen == m_frames) {
				ids.push_back(it->second.id);
			}
		}
		if (!ids.empty()) {
			m_bridge->highlight(ids.data(), (int)ids.size(), m_highlight_color[0], m_highlight_color[1],
			                    m_highlight_color[2]);
		}
	}
}

KX_GameObject *KX_RemixRenderer::Pick(int x, int y, bool exact)
{
	if (!m_running) {
		return NULL;
	}
	m_pick_wanted = true;
	m_pick_x = x;
	m_pick_y = y;
	if (!m_pick_has || (exact && (m_pick_ans_x != x || m_pick_ans_y != y))) {
		return NULL;
	}
	return ObjectAlive(m_pick_answer) ? m_pick_answer : NULL;
}

void KX_RemixRenderer::SetHighlight(const std::vector<KX_GameObject *> &objects, const float color[3])
{
	m_highlight = objects;
	if (color) {
		memcpy(m_highlight_color, color, sizeof(m_highlight_color));
	}
}

void KX_RemixRenderer::SendLight(KX_Scene *scene, const void *key, const brb_light_desc &desc)
{
	LightEntry &e = m_lights[key];
	const unsigned char *bytes = (const unsigned char *)&desc;
	e.frame = m_light_frame;
	if (e.hash != desc.hash || e.desc.size() != sizeof(desc) || memcmp(e.desc.data(), bytes, sizeof(desc)) != 0) {
		/* new or changed (moved, recoloured...): creating with the same hash
		 * replaces the light; BGE-Remix P8.9: one under another hash (a
		 * renamed or replaced object) goes first, so none is left alive */
		if (e.hash && e.hash != desc.hash) {
			m_bridge->light_destroy(e.hash);
			m_hashes.erase(e.hash);
		}
		m_bridge->light_create(&desc);
		e.hash = desc.hash;
		e.scene = scene;
		e.desc.assign(bytes, bytes + sizeof(desc));
	}
	m_bridge->frame_light(desc.hash);
}

void KX_RemixRenderer::UpdateLights(KX_Scene *scene, float light_scale)
{
	/* the formulas are shared with the 3D view and F12 (source/blender/remix/intern/remix_light.cc) */
	float dome[3] = {0.0f, 0.0f, 0.0f};
	m_light_frame++;

	CListValue *lights = scene->GetLightList();
	for (int i = 0; i < lights->GetCount(); i++) {
		KX_LightObject *light = (KX_LightObject *)lights->GetValue(i);
		RAS_ILightObject *data = light->GetLightData();
		Object *ob = light->GetBlenderObject();
		Lamp *la = (ob && ob->type == OB_LAMP) ? (Lamp *)ob->data : NULL;
		/* BGE-Remix P8.9: Only Shadow lamps darken, they do not light: Remix
		 * has no such light, so they are left out */
		if (!data || !light->GetVisible() || (la && (la->mode & LA_NEG)) || data->m_onlyshadow) {
			continue;
		}
		brmx::LampInput in;
		in.type = la ? la->type : (data->m_type == RAS_ILightObject::LIGHT_SUN ? LA_SUN :
		                           data->m_type == RAS_ILightObject::LIGHT_SPOT ? LA_SPOT : LA_LOCAL);
		in.energy = data->m_energy;
		in.color[0] = data->m_color[0];
		in.color[1] = data->m_color[1];
		in.color[2] = data->m_color[2];
		in.dist = data->m_distance;
		in.spotsize = data->m_spotsize;
		in.spotblend = data->m_spotblend;
		in.have_lamp = la != NULL;
		/* BGE-Remix P8.9: the size as the game has it now (Python can change it) */
		in.area_size = (in.type == LA_AREA) ? data->m_areasize[0] : data->m_size;
		in.area_sizey = data->m_areasize[1];
		in.area_square = false;
		in.physical = data->m_physical;
		in.sun_angle = data->m_sunangle;
		in.halo = (la && in.type == LA_SPOT && (la->mode & LA_HALO)) ? la->haint : 0.0f;
		const MT_Vector3 &sc = light->NodeGetWorldScaling();
		in.scale[0] = (float)sc[0];
		in.scale[1] = (float)sc[1];

		const MT_Matrix3x3 &ori = light->NodeGetWorldOrientation();
		const MT_Point3 &pos = light->NodeGetWorldPosition();
		const float p[3] = {(float)pos[0], (float)pos[1], (float)pos[2]};
		float ax[3], ay[3], az[3];
		column(ori, 0, ax);
		column(ori, 1, ay);
		column(ori, 2, az);
		brb_light_desc d;
		memset(&d, 0, sizeof(d));
		d.hash = UniqueHash("light", light->GetName().ReadPtr(), light);
		if (brmx::lamp_light(in, p, ax, ay, az, light_scale, d, dome) == 1) {
			SendLight(scene, light, d);
		}
	}

	/* The world: its horizon colour as a uniform sky (as background and as
	 * light), plus the hemi lamps */
	KX_WorldInfo *world = scene->GetWorldInfo();
	if (world && world->hasWorld()) {
		const float *bg = world->getBackColor();
		for (int k = 0; k < 3; k++) {
			dome[k] += bg[k];
		}
	}
	brb_light_desc d;
	memset(&d, 0, sizeof(d));
	d.hash = UniqueHash("dome", scene->GetName().ReadPtr(), world);
	if (brmx::dome_light(dome, d)) {
		SendLight(scene, world, d);
	}

	/* BGE-Remix P8.9: lights of this scene not sent this frame (lamp objects
	 * removed, turned Only Shadow...) are destroyed, not kept alive */
	for (auto it = m_lights.begin(); it != m_lights.end();) {
		if (it->second.scene == scene && it->second.frame != m_light_frame) {
			if (m_running && it->second.hash) {
				m_bridge->light_destroy(it->second.hash);
			}
			m_hashes.erase(it->second.hash);
			it = m_lights.erase(it);
		}
		else {
			++it;
		}
	}
}

void KX_RemixRenderer::RenderScene(KX_Scene *scene, KX_Camera *cam, const MT_Matrix4x4 &view,
                                   const MT_Matrix4x4 &proj, const RAS_Rect &viewport)
{
	if (!m_running || !m_bridge) {
		return;
	}
	RemixLock lock;
	const double t0 = now_seconds();
	/* the window's size (BGE's viewport rectangle includes its right and top
	 * edge: 1920 x 1080 shows as 0..1920, 0..1080) */
	const int width = std::max(viewport.GetRight() - viewport.GetLeft(), 8);
	const int height = std::max(viewport.GetTop() - viewport.GetBottom(), 8);
	/* child window: top-left corner in the game window's client area */
	const int x = viewport.GetLeft();
	const int y = m_canvas->GetHeight() - viewport.GetTop();
	if (width != m_width || height != m_height || (m_mode == BRB_MODE_CHILD && (x != m_x || y != m_y))) {
		if (m_bridge->resize(width, height, x, y) == 0) {
			m_width = width;
			m_height = height;
			m_x = x;
			m_y = y;
		}
	}
	ShowRemixWindow(true);

	m_bridge->frame_begin();
	UpdateBakes(scene);
	UpdateTextures(scene);

	float v[16], p[16];
	view.getValue(v);
	proj.getValue(p);
	m_bridge->frame_camera(v, p);

	int draws = 0;
	m_vp_x0 = x;
	m_vp_y0 = y;
	m_skinned_objects = m_deformed_objects = m_billboards = m_emitters = m_particle_draws = 0;
	m_pick_frame.clear();
	const double ts = now_seconds();
	double dynamic_s = 0.0;
	const MT_Point3 campos = cam ? cam->NodeGetWorldPosition() : MT_Point3(0.0, 0.0, 0.0);
	CListValue *objects = scene->GetObjectList();

	/* GPU particles bounce where Blender's would: off objects with Collision on */
	m_colliders = false;
	Scene *bscene = scene->GetBlenderScene();
	m_gravity = bscene ? std::fabs(bscene->gm.gravity) : 9.81f;
	for (int i = 0; i < objects->GetCount() && bscene; i++) {
		Object *ob = ((KX_GameObject *)objects->GetValue(i))->GetBlenderObject();
		if (ob && ob->pd && ob->pd->deflect) {
			m_colliders = true;
			m_collider_bounce = std::min(std::max(1.0f - ob->pd->pdef_damp, 0.0f), 1.0f);
			break;
		}
	}

	for (int i = 0; i < objects->GetCount(); i++) {
		KX_GameObject *obj = (KX_GameObject *)objects->GetValue(i);
		if (!obj->GetVisible() || obj->GetMeshCount() == 0 || obj->GetGameObjectType() == SCA_IObject::OBJ_TEXT) {
			continue;
		}
		float model[16];
		object_matrix(obj, model);
		memcpy(m_bake_obmat, model, sizeof(m_bake_obmat)); /* P8.6 */
		const uint32_t pick = PickId(obj);
		int first = 0;
		/* armatures, shape keys, modifiers, soft bodies (mesh 0) */
		if (RAS_Deformer *def = obj->GetDeformer()) {
			const double t = now_seconds();
			if (DrawSkinned(scene, obj, def, model, pick) || DrawDeformed(scene, obj, def, model, pick)) {
				first = 1;
				draws++;
			}
			dynamic_s += now_seconds() - t;
		}
		/* Blender particle systems */
		bool hide_emitter = false;
		Object *ob = obj->GetBlenderObject();
		if (ob && ob->particlesystem.first) {
			hide_emitter = DrawEmitters(scene, obj, ob, model, pick);
		}
		for (int m = first; m < obj->GetMeshCount(); m++) {
			if (m == 0 && hide_emitter) {
				continue;
			}
			MeshEntry *e = NULL;
			if (!EnsureMesh(scene, obj->GetMesh(m), e) || !e) {
				continue;
			}
			brb_draw_desc d;
			memset(&d, 0, sizeof(d));
			d.mesh = e->hash;
			if (e->billboard) {
				billboard_matrix(obj, campos, e->billboard == 1, d.model);
				m_billboards++;
			}
			else {
				memcpy(d.model, model, sizeof(d.model));
			}
			d.flags = (e->double_sided ? BRB_DRAW_DOUBLE_SIDED : 0) | (e->blend ? BRB_DRAW_BLEND : 0);
			d.categories = e->particle ? BRB_CATEGORY_PARTICLE : 0;
			m_particle_draws += e->particle ? 1 : 0;
			d.pick_id = pick;
			m_bridge->frame_draw_ex(&d);
			draws++;
		}
	}
	if (m_skinned_objects || m_deformed_objects) {
		m_skin_ms_total += dynamic_s * 1000.0;
		m_skin_ms_frames++;
	}
	(void)ts;
	PruneDynamic(scene);
	UpdatePicking(scene);

	UpdateLights(scene, m_settings.light_scale);
	const double t1 = now_seconds();

	m_bridge->frame_end();

	brb_frame_stats st;
	m_bridge->get_stats(&st);
	m_frames++;
	m_last_draws = st.draws;
	m_last_lights = st.lights;
	if (m_frames > 1) { /* the first frame creates everything */
		m_handover_ms.push_back((float)((t1 - t0) * 1000.0));
	}
	m_present_ms.push_back((float)st.present_ms);
	m_copy_ms.push_back((float)st.copy_ms);
	/* the longest gap between two Remix frames, past the first frames that
	 * create the scene: a texture swap must not make the game stutter */
	if (m_frames > 10 && m_last_render > 0.0) {
		m_frame_ms_max = std::max(m_frame_ms_max, (t0 - m_last_render) * 1000.0);
	}
	m_last_render = t0;
	/* logic, physics, animation and culling: everything BGE did since the
	 * previous Remix frame was on screen (copy mode runs it in series) */
	if (m_frames > 10 && m_last_end > 0.0) {
		m_game_ms.push_back((float)((t0 - m_last_end) * 1000.0));
	}
	if (m_first_textured_frame < 0 && m_materials_waiting == 0) {
		m_first_textured_frame = m_frames;
		m_first_textured_seconds = now_seconds() - m_start_time;
	}

	if (m_mode == BRB_MODE_DIRECT) {
		const double t2 = now_seconds();
		BRMX_present_draw(viewport.GetLeft(), viewport.GetBottom(), width, height);
		m_upload_ms.push_back((float)((now_seconds() - t2) * 1000.0));
		std::wstring path;
		if (m_frames == m_screenshot_frame && env_path(L"BGE_REMIX_SCREENSHOT", path)) {
			SaveWindowBmp(path, viewport.GetLeft(), viewport.GetBottom(), width, height);
		}
	}
	else {
		MaybeScreenshot();
	}

	if (m_mode == BRB_MODE_COPY) {
		const double t2 = now_seconds();
		BRMX_present_draw(viewport.GetLeft(), viewport.GetBottom(), width, height);
		m_upload_ms.push_back((float)((now_seconds() - t2) * 1000.0));
	}
	m_last_end = now_seconds();
	(void)cam;
	(void)draws;
}

/* ------------------------------------------------------------------------- */
/* Picture: drawn by the shared presenter (source/blender/remix/intern/remix_present.c) */

bool KX_RemixRenderer::SaveWindowBmp(const std::wstring &path, int x, int y, int width, int height)
{
	if (width <= 0 || height <= 0) {
		return false;
	}
	const int row = (width * 3 + 3) & ~3;
	std::vector<unsigned char> px((size_t)row * height);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	glPushAttrib(GL_PIXEL_MODE_BIT);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadBuffer(GL_BACK);
	glReadPixels(x, y, width, height, GL_BGR, GL_UNSIGNED_BYTE, px.data());
	glPopAttrib();
	glPopClientAttrib();
	FILE *f = _wfopen(path.c_str(), L"wb");
	if (!f) {
		return false;
	}
	/* BMP rows go from the bottom, as OpenGL's */
	unsigned char head[54] = {'B', 'M'};
	const unsigned int data = (unsigned int)px.size(), size = 54 + data;
	memcpy(head + 2, &size, 4);
	const unsigned int off = 54, info = 40, w = (unsigned int)width, h = (unsigned int)height;
	memcpy(head + 10, &off, 4);
	memcpy(head + 14, &info, 4);
	memcpy(head + 18, &w, 4);
	memcpy(head + 22, &h, 4);
	head[26] = 1;
	head[28] = 24;
	memcpy(head + 34, &data, 4);
	const bool ok = fwrite(head, 1, 54, f) == 54 && fwrite(px.data(), 1, px.size(), f) == px.size();
	fclose(f);
	return ok;
}

void KX_RemixRenderer::MaybeScreenshot()
{
	std::wstring path;
	if (m_frames == m_screenshot_frame && env_path(L"BGE_REMIX_SCREENSHOT", path)) {
		m_bridge->save_bmp(path.c_str());
	}
}

/* ------------------------------------------------------------------------- */
/* Statistics for the tests */

void KX_RemixRenderer::AddProfileSample(double logic, double physics, double animations, double scenegraph)
{
	if (!m_running || m_frames <= 10) {
		return;
	}
	m_logic_ms.push_back((float)(logic * 1000.0));
	m_physics_ms.push_back((float)(physics * 1000.0));
	m_anim_ms.push_back((float)(animations * 1000.0));
	m_scenegraph_ms.push_back((float)(scenegraph * 1000.0));
	m_addremove_ms.push_back((float)(KX_Scene::TakeAddRemoveSeconds() * 1000.0));
}

void KX_RemixRenderer::WriteStats()
{
	std::wstring path;
	if (!env_path(L"BGE_REMIX_STATS", path)) {
		return;
	}
	brb_frame_stats st;
	memset(&st, 0, sizeof(st));
	if (m_bridge && m_running) {
		m_bridge->get_stats(&st);
	}
	FILE *f = _wfopen(path.c_str(), L"wb");
	if (!f) {
		return;
	}
	std::string status = m_status;
	for (size_t i = 0; i < status.size(); i++) {
		if (status[i] == '"' || status[i] == '\\' || (unsigned char)status[i] < 32) {
			status[i] = ' ';
		}
	}
	std::string notice = m_notice;
	for (size_t i = 0; i < notice.size(); i++) {
		if (notice[i] == '"' || notice[i] == '\\' || (unsigned char)notice[i] < 32) {
			notice[i] = ' ';
		}
	}
	fprintf(f, "{\n");
	fprintf(f, "  \"running\": %s,\n", m_running ? "true" : "false");
	fprintf(f, "  \"mode\": \"%s\",\n", m_mode == BRB_MODE_COPY ? "copy" : m_mode == BRB_MODE_CHILD ? "window" :
	                                       m_mode == BRB_MODE_DIRECT ? "direct" : "none");
	fprintf(f, "  \"direct_frames\": %d,\n", BRMX_present_direct_frames() - m_direct_frames_base);
	fprintf(f, "  \"copy_mode_reason\": \"%s\",\n", m_direct_note.c_str());
	fprintf(f, "  \"status\": \"%s\",\n", status.c_str());
	fprintf(f, "  \"notice\": \"%s\",\n", notice.c_str());
	fprintf(f, "  \"frames\": %d,\n", m_frames);
	fprintf(f, "  \"width\": %d,\n  \"height\": %d,\n", m_width, m_height);
	/* P5: Remix's picture size (direct mode follows the window a frame or two later) */
	fprintf(f, "  \"picture_width\": %d,\n  \"picture_height\": %d,\n  \"resets\": %d,\n", st.width, st.height,
	        st.resets);
	fprintf(f, "  \"draws_last_frame\": %d,\n  \"lights_last_frame\": %d,\n", m_last_draws, m_last_lights);
	fprintf(f, "  \"meshes_created\": %d,\n  \"materials_created\": %d,\n  \"lights_created\": %d,\n",
	        st.meshes_created, st.materials_created, st.lights_created);
	fprintf(f, "  \"errors\": %d,\n  \"last_error\": %d,\n", st.errors, st.last_error);
	fprintf(f, "  \"handover_ms_median\": %.4f,\n  \"handover_ms_p95\": %.4f,\n",
	        median(m_handover_ms), percentile95(m_handover_ms));
	fprintf(f, "  \"present_ms_median\": %.4f,\n  \"copy_ms_median\": %.4f,\n  \"upload_ms_median\": %.4f,\n",
	        median(m_present_ms), median(m_copy_ms), median(m_upload_ms));
	fprintf(f, "  \"game_ms_median\": %.4f,\n", median(m_game_ms));
	fprintf(f, "  \"logic_ms_median\": %.4f,\n  \"physics_ms_median\": %.4f,\n", median(m_logic_ms),
	        median(m_physics_ms));
	fprintf(f, "  \"animation_ms_median\": %.4f,\n  \"scenegraph_ms_median\": %.4f,\n", median(m_anim_ms),
	        median(m_scenegraph_ms));
	fprintf(f, "  \"addremove_ms_median\": %.4f,\n", median(m_addremove_ms));
	fprintf(f, "  \"frame_ms_max\": %.4f,\n", m_frame_ms_max);
	fprintf(f, "  \"textures_encoded\": %d,\n  \"textures_reused\": %d,\n  \"textures_failed\": %d,\n",
	        m_cache ? m_cache->encoded : 0, m_cache ? m_cache->reused : 0, m_cache ? m_cache->failed : 0);
	fprintf(f, "  \"textures_waiting\": %d,\n  \"encode_seconds\": %.4f,\n",
	        m_cache ? m_cache->waiting() : 0, m_cache ? m_cache->encode_seconds : 0.0);
	fprintf(f, "  \"first_textured_frame\": %d,\n  \"first_textured_seconds\": %.4f,\n",
	        m_first_textured_frame, m_first_textured_seconds);
	fprintf(f, "  \"materials_swapped\": %d,\n  \"material_notes\": %d,\n  \"swap_ms_max\": %.4f,\n",
	        m_materials_swapped, m_material_notes, m_max_swap_ms);
	fprintf(f, "  \"procedural_bakes\": %d,\n  \"procedural_rebakes\": %d,\n  \"procedural_bake_ms\": %.2f,\n",
	        m_bakes, m_rebakes, m_bake_ms);
	/* P4 (the counts are the last frame's) */
	fprintf(f, "  \"skinned_objects\": %d,\n  \"skinned_draws\": %d,\n  \"bones_last_frame\": %d,\n",
	        m_skinned_objects, st.skinned_draws, st.bones);
	fprintf(f, "  \"skin_meshes\": %d,\n  \"cpu_skinning\": %s,\n  \"cpu_skin_notes\": %d,\n",
	        (int)m_skins.size(), m_cpu_skinning ? "true" : "false", m_cpu_skin_notes);
	fprintf(f, "  \"deformed_objects\": %d,\n  \"deform_uploads\": %d,\n  \"shadow_faces_hidden\": %d,\n",
	        m_deformed_objects, m_deform_uploads, m_shadow_faces);
	fprintf(f, "  \"dynamic_ms_mean\": %.4f,\n", m_skin_ms_frames ? m_skin_ms_total / m_skin_ms_frames : 0.0);
	fprintf(f, "  \"billboards\": %d,\n  \"particle_draws\": %d,\n  \"emitters\": %d,\n  \"gpu_emitters\": %d,\n",
	        m_billboards, m_particle_draws, m_emitters, st.emitters);
	fprintf(f, "  \"picks_answered\": %d,\n  \"pick_frames_mean\": %.3f\n", m_picks_answered,
	        m_pick_frames_count ? (double)m_pick_frames_total / m_pick_frames_count : -1.0);
	fprintf(f, "}\n");
	fclose(f);
}

#endif  /* WITH_REMIX */
