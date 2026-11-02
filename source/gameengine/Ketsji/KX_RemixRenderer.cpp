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
#include <set>
#include <tuple>

#include "GPU_glew.h"  /* BGE-Remix P8.26: the old calls redirected */

#include "KX_RemixRenderer.h"
#include "KX_Sprites.h"
#include "KX_FontObject.h"  /* P8.20: the particle description */

#include "KX_Scene.h"
#include "KX_GameObject.h"
#include "KX_Light.h"
#include "KX_Camera.h"
#include "KX_WorldInfo.h"

#include "RAS_ICanvas.h"
#include "RAS_ILightObject.h"
#include "RAS_IPolygonMaterial.h"
#include "RAS_IRasterizer.h"  /* P8.30: RAS_RENDER_3DPOLYGON_TEXT */
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
#  include "DNA_modifier_types.h"
#  include "DNA_userdef_types.h"
#  include "BKE_scene.h"
#  include "DNA_camera_types.h"
#  include "BKE_modifier.h"
#  include "DNA_scene_types.h"
#  include "DNA_world_types.h"
#  include "DNA_texture_types.h"
#  include "DNA_image_types.h"
#  include "BKE_object.h"
#  include "BKE_material.h"
#  include "BKE_image.h"
#  include "BKE_bmfont.h"
#  include "BLI_string_utf8.h"
}
#include "GPU_material.h"

#include "BRMX_loader.h"  /* C linkage inside */
#include "BRMX_bake.h"
#include "BRMX_light.h"
#include "BRMX_material.h"
#include "BRMX_present.h"
#include "BRMX_session.h"

/* The world colour and the lamps, in Blender Internal's units, are turned
 * into Remix's physical units so that a white surface lit by a lamp of
 * energy 1 at distance D comes out as bright as in Blender Internal (far
 * from the lamp); bge_remix_light_scale is a global factor on top (1 by
 * default since P8.30). */
static const double NOTICE_SECONDS = 8.0;
/* P8.30: brmx::DEFAULT_LIGHT_SCALE and DEFAULT_EXPOSURE (BRMX_light.h):
 * Remix's automatic exposure off, the lamps in their own units, no stops
 * added (the P2 room matches Cycles; dark worlds stay dark) */
static const float DEFAULT_LIGHT_SCALE = 1.0f;
static const float DEFAULT_EXPOSURE = 0.0f;
static const double PI = 3.14159265358979323846;

/* P8.30: bge.texture pictures for Remix (LiveTexture) */
static KX_RemixRenderer *g_live_owner = NULL;

static void live_texture_hook(Image *ima, const unsigned int *rgba, int width, int height)
{
	if (g_live_owner) {
		g_live_owner->LiveTexture(ima, rgba, width, height);
	}
}

/* pictures a second sent to Remix (BGE_REMIX_LIVE_TEXTURE_HZ, default 5; 0:
 * none) */
static double live_hz()
{
	static double hz = -1.0;
	if (hz < 0.0) {
		const char *env = getenv("BGE_REMIX_LIVE_TEXTURE_HZ");
		hz = (env && env[0]) ? atof(env) : 5.0;
		if (!(hz >= 0.0)) {
			hz = 5.0;
		}
	}
	return hz;
}

static bool live_texture_due(Image *ima)
{
	return g_live_owner && g_live_owner->LiveTextureDue(ima);
}


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

/* P8.30: Remix stays on across scene and file switches (KX_RemixRenderer.h) */
static bool g_remix_keep_on = false;

void KX_RemixRenderer::ResetKeepOn()
{
	g_remix_keep_on = false;
}

KX_RemixRenderer::Settings KX_RemixRenderer::GetSettings(KX_Scene *scene, int override_enabled, int override_fast)
{
	Settings s = {0, 0, DEFAULT_LIGHT_SCALE, DEFAULT_EXPOSURE, 0};
	Scene *bscene = scene ? scene->GetBlenderScene() : NULL;
	IDProperty *props = bscene ? bscene->id.properties : NULL;
	/* P5: a scene rendered with the RTX Remix engine plays with Remix too */
	s.enabled = idprop_int(props, "bge_remix", 0) != 0 || (bscene && strcmp(bscene->r.engine, "RTX_REMIX") == 0);
	s.window_mode = idprop_int(props, "bge_remix_fast", 0) != 0;
	s.explicit_off = !s.enabled && idprop_int(props, "bge_remix", -1) == 0;
	s.light_scale = idprop_float(props, "bge_remix_light_scale", DEFAULT_LIGHT_SCALE);
	s.exposure = idprop_float(props, "bge_remix_exposure", DEFAULT_EXPOSURE);
	if (override_enabled >= 0) {
		s.enabled = override_enabled != 0;
	}
	if (override_fast >= 0) {
		s.window_mode = override_fast != 0;
	}
	/* P8.30 diagnostics: BGE_REMIX_LIGHT_SCALE overrides the scene's */
	const char *ls = getenv("BGE_REMIX_LIGHT_SCALE");
	if (ls && ls[0]) {
		s.light_scale = (float)atof(ls);
	}
	/* and BGE_REMIX_EXPOSURE the scene's exposure (stops) */
	const char *ex = getenv("BGE_REMIX_EXPOSURE");
	if (ex && ex[0]) {
		s.exposure = (float)atof(ex);
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
	m_skykey = 0;
	m_volume_scan = 0;
	m_mist_said = false;
	m_instanced_groups = 0;
	m_live_updates = 0;
	g_live_owner = this;
	KX_LiveTextureHook = live_texture_hook;
	KX_LiveTextureDue = live_texture_due;
	m_materials_waiting = 0;
	m_materials_swapped = 0;
	m_material_notes = 0;
	m_bakes = m_rebakes = 0;
	m_bake_ms = 0.0;
	m_bakes_cached = 0;
	m_bake_gpu_ms = 0.0;
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
	if (g_live_owner == this) {
		g_live_owner = NULL;
		KX_LiveTextureHook = NULL;
		KX_LiveTextureDue = NULL;
	}
	for (size_t i = 0; i < m_volume_objs.size(); i++)
		m_volume_objs[i]->Release();
	m_volume_objs.clear();
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
	/* the bge.texture pictures' files */
	for (auto &it : m_live) {
		const std::wstring *files[3] = {&it.second.old, &it.second.ready, &it.second.writing};
		for (int i = 0; i < 3; i++) {
			if (!files[i]->empty()) {
				DeleteFileW(files[i]->c_str());
			}
		}
	}
	m_live.clear();
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
		SetNotice("2D filters work on RTX Remix's picture; filters that read depth see none.");
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
	{
		/* P8.22: Blender's anisotropic filtering (User Preferences) */
		char value[32];
		snprintf(value, sizeof(value), "%.1f", std::max(1.0f, (float)U.anisotropic_filter));
		m_bridge->set_config("rtx.useAnisotropicFiltering", U.anisotropic_filter > 1 ? "True" : "False");
		m_bridge->set_config("rtx.maxAnisotropySamples", value);
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
	/* P8.30 diagnostics: BGE_REMIX_RTX_SET "rtx.a=1;rtx.b=2" sets Remix
	 * options after the scene's own */
	if (const char *set = getenv("BGE_REMIX_RTX_SET")) {
		std::string all(set);
		size_t at = 0;
		while (at < all.size()) {
			size_t end = all.find(';', at);
			if (end == std::string::npos) {
				end = all.size();
			}
			const std::string kv = all.substr(at, end - at);
			const size_t eq = kv.find('=');
			if (eq != std::string::npos && eq > 0) {
				const std::string key = kv.substr(0, eq), value = kv.substr(eq + 1);
				m_bridge->set_config(key.c_str(), value.c_str());
				printf("BGE-Remix: BGE_REMIX_RTX_SET %s = %s\n", key.c_str(), value.c_str());
			}
			at = end + 1;
		}
	}
	fflush(stdout);
	return true;
}

void KX_RemixRenderer::ApplyExposure(const Settings &settings)
{
	/* stops on top of Remix's automatic exposure (source/blender/remix/intern/remix_light.cc) */
	brmx::apply_exposure(m_bridge, settings.exposure);
	/* (it turns automatic exposure off: the camera effects are sent again) */
	m_fx_sent.clear();
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

/* P8.30: a 2D screen (a menu): an orthographic camera and no lamps. It is
 * drawn in OpenGL as its author made it, the game staying in Remix; an
 * orthographic scene with lamps is ray traced (brmx::camera_for_remix) */
static bool is_2d_screen(KX_Scene *scene)
{
	KX_Camera *cam = scene ? scene->GetActiveCamera() : NULL;
	if (!cam || cam->GetCameraData()->m_perspective) {
		return false;
	}
	CListValue *lights = scene->GetLightList();
	return !lights || lights->GetCount() == 0;
}

static void note_2d_screen(KX_Scene *scene)
{
	static std::set<std::string> said;
	const std::string name = scene->GetName().ReadPtr();
	if (said.insert(name).second) {
		printf("BGE-Remix: scene \"%s\" is a 2D screen (an orthographic camera, no lamps): drawn in OpenGL, "
		       "the game stays in RTX Remix\n", name.c_str());
	}
}

KX_Scene *KX_RemixRenderer::PickSceneImpl(const std::vector<KX_Scene *> &scenes, bool stereo, bool dome,
                                          int override_enabled, int override_fast)
{
	if (m_running) {
		/* the scene keeps Remix as long as it exists */
		for (KX_Scene *s : scenes) {
			if (s == m_scene) {
				if (is_2d_screen(s)) {
					note_2d_screen(s);
					return NULL;
				}
				return m_scene;
			}
		}
		m_scene = NULL;
	}

	/* The first scene (bottom of the stack) with Remix on gets it. Only one:
	 * Remix draws one picture per frame. */
	KX_Scene *wanted = NULL;
	Settings settings = {0, 0, DEFAULT_LIGHT_SCALE, DEFAULT_EXPOSURE, 0};
	bool first = true;
	for (KX_Scene *s : scenes) {
		/* the -g override applies to the first scene only */
		settings = GetSettings(s, first ? override_enabled : -1, first ? override_fast : -1);
		/* P8.30: a game that has drawn through Remix keeps it (the bottom
		 * scene it switched to or loaded, unless that turns Remix off
		 * itself; not an overlay over a scene drawn in OpenGL) */
		if (!settings.enabled && g_remix_keep_on && first && !settings.explicit_off && override_enabled != 0) {
			settings.enabled = 1;
			static bool said = false;
			if (!said) {
				printf("BGE-Remix: scene \"%s\" stays in RTX Remix (the game started in it)\n",
				       s->GetName().ReadPtr());
				said = true;
			}
		}
		first = false;
		if (settings.enabled) {
			wanted = s;
			break;
		}
	}
	if (!wanted) {
		return NULL;
	}
	if (is_2d_screen(wanted)) {
		/* the game is in Remix all the same: the scenes after it keep it */
		note_2d_screen(wanted);
		g_remix_keep_on = true;
		return NULL;
	}
	if (m_running) {
		/* another scene asks for Remix after the first one went */
		m_scene = wanted;
		m_settings = settings;
		ApplyExposure(settings);
		g_remix_keep_on = true;
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
	if (!Start(wanted, w, h, settings)) {
		return NULL;
	}
	g_remix_keep_on = true;
	return m_scene;
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
		if (r.from_cache) {
			printf("BGE-Remix: material \"%s\" on mesh \"%s\": procedural textures from the cache (%.0f ms)\n",
			       e.name.c_str(), e.mesh->GetName().ReadPtr(), (now_seconds() - t0) * 1000.0);
			m_bakes_cached++;
		}
		else {
			printf("BGE-Remix: material \"%s\" on mesh \"%s\": procedural textures baked (%.0f ms, %.1f ms on "
			       "the graphics card)\n", e.name.c_str(), e.mesh->GetName().ReadPtr(), (now_seconds() - t0) * 1000.0,
			       r.gpu_ms);
			if (r.gpu_ms > 0.0) {
				m_bake_gpu_ms += r.gpu_ms;
			}
		}
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
		/* P8.30: a bge.texture picture written for Remix */
		std::map<Image *, LiveEntry>::const_iterator live = req.image ? m_live.find(req.image) : m_live.end();
		if (live != m_live.end() && !live->second.ready.empty()) {
			paths[i] = live->second.ready;
			continue;
		}
		const brmx::TextureCache::State st = m_cache->request(req, paths[i]);
		if (st != brmx::TextureCache::READY) {
			paths[i].clear();
			waiting = waiting || st == brmx::TextureCache::WAITING;
		}
	}
	for (int i = 0; i < brmx::TEX_COUNT; i++) {
		*brmx::texture_field(d, i) = paths[i].empty() ? NULL : paths[i].c_str();
	}
	m_bridge->material_create(&d);
	if (waiting != e.waiting) {
		m_materials_waiting += waiting ? 1 : -1;
	}
	e.waiting = waiting;
}

/* P8.30: bge.texture's pictures reach Remix: a few times a second the
 * picture is written to a file of its own (two in turn) and the materials
 * using its image are sent again with it (Remix reads a material's
 * textures when the material is made) */
bool KX_RemixRenderer::LiveTextureDue(Image *ima)
{
	const double hz = live_hz();
	if (!m_running || !m_bridge || !ima || hz <= 0.0) {
		return false;
	}
	std::map<Image *, LiveEntry>::const_iterator it = m_live.find(ima);
	if (it == m_live.end()) {
		return true;
	}
	return it->second.ticket <= 0 && now_seconds() - it->second.last >= 1.0 / hz;
}

void KX_RemixRenderer::LiveTexture(Image *ima, const unsigned int *rgba, int width, int height)
{
	if (!rgba || width <= 0 || height <= 0 || !LiveTextureDue(ima)) {
		return;
	}
	LiveEntry &e = m_live[ima];
	e.last = now_seconds();
	wchar_t name[64];
	swprintf(name, 64, L"\\live_%p_%d.dds", (void *)ima, e.flip);
	const std::wstring folder = brmx::TextureCache::folder();
	CreateDirectoryW(folder.c_str(), NULL);
	e.writing = folder + name;
	brb_texture_desc desc;
	memset(&desc, 0, sizeof(desc));
	desc.path = e.writing.c_str();
	desc.kind = BRB_TEXTURE_COLOR;
	desc.width = width;
	desc.height = height;
	desc.rgba8 = (const unsigned char *)rgba;
	e.ticket = m_bridge->texture_submit(&desc);
	if (e.ticket <= 0) {
		e.ticket = 0;
	}
}

void KX_RemixRenderer::UpdateLiveTextures()
{
	for (auto &it : m_live) {
		LiveEntry &e = it.second;
		if (e.ticket <= 0) {
			continue;
		}
		brb_texture_result r;
		memset(&r, 0, sizeof(r));
		const int state = m_bridge->texture_poll(e.ticket, &r);
		if (state == 0) {
			continue;  /* still being written */
		}
		e.ticket = 0;
		if (state != 1) {
			continue;
		}
		/* the picture two back goes (the last one may still be loading) */
		if (!e.old.empty()) {
			DeleteFileW(e.old.c_str());
		}
		e.old = e.ready;
		e.ready = e.writing;
		e.flip++;
		m_live_updates++;
		for (auto &m : m_materials) {
			MaterialEntry &me = m.second;
			for (int i = 0; i < brmx::TEX_COUNT && me.spec; i++) {
				if (me.spec->textures[i].image == it.first) {
					SendMaterial(me);
					break;
				}
			}
		}
		static bool said = false;
		if (!said) {
			printf("BGE-Remix: a bge.texture picture reaches Remix (rewritten a few times a second)\n");
			said = true;
		}
	}
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
      categories(0),
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
		/* BGE-Remix P8 Part B (P8.19): a volume is Remix's fog inside it, not a surface */
		if (pm && pm->IsVolume()) {
			continue;
		}
		if (mode & RAS_IPolyMaterial::SHADOW) {
			/* fake blob shadows, cast to the ground in OpenGL: Remix casts real ones */
			g.shadow_faces++;
			continue;
		}
		/* P8.30: Face Texture text: its letters are drawn by DrawFaceText */
		if (mode & RAS_IRasterizer::RAS_RENDER_3DPOLYGON_TEXT) {
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
		/* P8.22: a Wire material as thin tubes along the edges (Remix draws
		 * no lines) */
		if (ma && ma->material_type == MA_TYPE_WIRE && !weights) {
			const float radius = 0.004f;
			std::set<std::pair<uint32_t, uint32_t> > edges;
			for (size_t t = 0; t + 2 < idx.size(); t += 3) {
				for (int k = 0; k < 3; k++) {
					uint32_t a = idx[t + k], b = idx[t + (k + 1) % 3];
					/* the same corner may be several vertices (split normals) */
					for (uint32_t *e : {&a, &b}) {
						for (uint32_t j = 0; j < *e; j++) {
							if (memcmp(v[j].position, v[*e].position, sizeof(v[j].position)) == 0) {
								*e = j;
								break;
							}
						}
					}
					if (a != b) {
						edges.insert(std::make_pair(std::min(a, b), std::max(a, b)));
					}
				}
			}
			std::vector<brb_vertex> tv;
			std::vector<uint32_t> ti;
			for (std::set<std::pair<uint32_t, uint32_t> >::const_iterator e = edges.begin(); e != edges.end(); ++e) {
				const float *pa = v[e->first].position, *pb = v[e->second].position;
				float d[3], s1[3], s2[3];
				sub_v3_v3v3(d, pb, pa);
				if (normalize_v3(d) < 1e-8f) {
					continue;
				}
				ortho_basis_v3v3_v3(s1, s2, d);
				const uint32_t base = (uint32_t)tv.size();
				for (int k = 0; k < 4; k++) {
					const float ang = (float)k * (float)M_PI_2;
					float n[3];
					for (int c = 0; c < 3; c++) {
						n[c] = cosf(ang) * s1[c] + sinf(ang) * s2[c];
					}
					for (int end = 0; end < 2; end++) {
						brb_vertex o = v[end ? e->second : e->first];
						const float *p = end ? pb : pa;
						for (int c = 0; c < 3; c++) {
							o.position[c] = p[c] + n[c] * radius;
						}
						copy_v3_v3(o.normal, n);
						tv.push_back(o);
					}
				}
				for (int k = 0; k < 4; k++) {
					const uint32_t a0 = base + 2 * k, a1 = a0 + 1, b0 = base + 2 * ((k + 1) % 4), b1 = b0 + 1;
					const uint32_t q[6] = {a0, b0, b1, a0, b1, a1};
					ti.insert(ti.end(), q, q + 6);
				}
			}
			if (!ti.empty()) {
				v.swap(tv);
				idx.swap(ti);
				g.double_sided = true;
			}
		}
		/* see-through as the translator decided (game settings, Cycles'
		 * Transparent BSDF, the RTX Remix panel) */
		const MaterialEntry &me = EnsureMaterial(scene, pm, mesh);
		g.materials.push_back(me.hash);
		g.blend = g.blend || me.blend;
		g.particle = g.particle || me.particle;
		g.categories |= me.spec ? me.spec->categories : 0u;
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
		/* BGE-Remix P8 Part B (P8.19): a volume is Remix's fog inside it, not a surface */
		if (pm && pm->IsVolume()) {
			continue;
		}
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
		g.categories |= me.spec ? me.spec->categories : 0u;
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
	e.categories = g.categories;
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
		e.categories = g.categories;
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
	d.categories = (e.particle ? BRB_CATEGORY_PARTICLE : 0) | e.categories;
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
	skin->ForceCPUSkinning();
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
		/* P8 Part B: Remix reads the CPU's vertices (not OpenGL's skinning) */
		skin->ForceCPUSkinning();
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
			e.categories = g.categories;
			m_deform_uploads++;
		}
	}
	brb_draw_desc d;
	memset(&d, 0, sizeof(d));
	d.mesh = e.hash;
	memcpy(d.model, m, sizeof(d.model));
	d.flags = (e.double_sided ? BRB_DRAW_DOUBLE_SIDED : 0) | (e.blend ? BRB_DRAW_BLEND : 0);
	d.categories = (e.particle ? BRB_CATEGORY_PARTICLE : 0) | e.categories;
	/* P8.22: an Ocean surface is Remix's animated water */
	if (Object *bob = obj->GetBlenderObject()) {
		if (modifiers_findByType(bob, eModifierType_Ocean)) {
			d.categories |= BRB_CATEGORY_ANIMATED_WATER;
		}
	}
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
/* Halo materials (P8.20): each vertex a sprite facing the camera, its
 * picture Blender Internal's halo (rings, lines, stars) as emitted light
 * added to what is behind; flares are left to Remix's bloom */

static Material *halo_material_of(Object *ob)
{
	for (int c = 1; ob && c <= ob->totcol; c++) {
		Material *ma = give_current_material(ob, c);
		if (ma && ma->material_type == MA_TYPE_HALO) {
			return ma;
		}
	}
	return NULL;
}

uint64_t KX_RemixRenderer::HaloMaterial(Material *ma)
{
	GPUHaloStyle style;
	KX_HaloStyle(ma, &style);
	style.flares = 0;
	const uint64_t key = hash_bytes(&style, sizeof(style), 1469598103934665603ull);
	std::map<const void *, uint64_t>::iterator it = m_halo_materials.find(ma);
	const uint64_t hash = UniqueHash("halo", ma->id.name + 2, ma);
	if (it != m_halo_materials.end() && it->second == key) {
		return hash;
	}
	const int size = 128;
	wchar_t hex[24];
	swprintf(hex, 24, L"%016llx", (unsigned long long)key);
	const std::wstring folder = brmx::TextureCache::folder();
	CreateDirectoryW(folder.c_str(), NULL);
	const std::wstring path = folder + L"\\halo_k" + hex + L".dds";
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		std::vector<float> rgba((size_t)size * size * 4);
		KX_HaloPicture(&style, size, &rgba[0]);
		/* 8-bit sRGB, as colour textures are written */
		std::vector<unsigned char> px(rgba.size());
		for (size_t i = 0; i < rgba.size(); i++) {
			float v = std::min(std::max(rgba[i], 0.0f), 1.0f);
			if ((i & 3) != 3) {
				v = (v <= 0.0031308f) ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
			}
			px[i] = (unsigned char)(v * 255.0f + 0.5f);
		}
		brb_texture_desc desc;
		memset(&desc, 0, sizeof(desc));
		desc.path = path.c_str();
		desc.kind = BRB_TEXTURE_COLOR;
		desc.width = size;
		desc.height = size;
		desc.rgba8 = &px[0];
		if (m_bridge->texture_write(&desc, NULL) != 0) {
			return 0;
		}
	}
	brb_material_desc d;
	memset(&d, 0, sizeof(d));
	d.hash = hash;
	d.opacity = 1.0f;
	d.roughness = 1.0f;
	d.emissive[0] = d.emissive[1] = d.emissive[2] = 1.0f;
	d.emissive_intensity = 1.0f;
	d.emissive_texture = path.c_str();
	d.type = BRB_MATERIAL_OPAQUE;
	d.alpha_blend = 1;
	d.additive = 1;
	d.clamp = 1;
	m_bridge->material_create(&d);
	m_halo_materials[ma] = key;
	return hash;
}

bool KX_RemixRenderer::DrawHalos(KX_Scene *scene, KX_GameObject *obj, Object *ob, const float model[16],
                                 const MT_Matrix3x3 &camori, uint32_t pick)
{
	Material *ma = halo_material_of(ob);
	Mesh *me = (ob && ob->type == OB_MESH) ? (Mesh *)ob->data : NULL;
	if (!ma || !me || me->totvert <= 0) {
		return false;
	}
	const uint64_t mat = HaloMaterial(ma);
	if (!mat) {
		return false;
	}
	/* the camera's right and up in the object's space (so the sprites keep
	 * Halo Size in world units) */
	float m[4][4], inv[4][4];
	memcpy(m, model, sizeof(m));
	invert_m4_m4(inv, m);
	float right[3], up[3], back[3];
	for (int k = 0; k < 3; k++) {
		const float r[3] = {(float)camori[0][0], (float)camori[1][0], (float)camori[2][0]};
		const float u[3] = {(float)camori[0][1], (float)camori[1][1], (float)camori[2][1]};
		const float b[3] = {(float)camori[0][2], (float)camori[1][2], (float)camori[2][2]};
		right[k] = inv[0][k] * r[0] + inv[1][k] * r[1] + inv[2][k] * r[2];
		up[k] = inv[0][k] * u[0] + inv[1][k] * u[1] + inv[2][k] * u[2];
		back[k] = inv[0][k] * b[0] + inv[1][k] * b[1] + inv[2][k] * b[2];
	}
	normalize_v3(back);
	Geometry g;
	g.verts.resize(1);
	g.indices.resize(1);
	g.materials.push_back(mat);
	const float s = ma->hasize;
	const float corner[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
	for (int v = 0; v < me->totvert; v++) {
		const uint32_t base = (uint32_t)g.verts[0].size();
		for (int c = 0; c < 4; c++) {
			brb_vertex bv;
			memset(&bv, 0, sizeof(bv));
			for (int k = 0; k < 3; k++) {
				bv.position[k] = me->mvert[v].co[k] + (right[k] * corner[c][0] + up[k] * corner[c][1]) * s;
				bv.normal[k] = back[k];
			}
			bv.uv[0] = 0.5f + 0.5f * corner[c][0];
			bv.uv[1] = 0.5f - 0.5f * corner[c][1];
			bv.color = 0xffffffffu;
			g.verts[0].push_back(bv);
		}
		const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
		for (int k = 0; k < 6; k++) {
			g.indices[0].push_back(base + idx[k]);
		}
	}
	g.content = hash_bytes(&g.verts[0][0], g.verts[0].size() * sizeof(brb_vertex), 1469598103934665603ull);
	g.double_sided = true;
	g.blend = true;

	DeformEntry &e = m_deformed[(const char *)obj + 1];
	const std::string name = obj->GetName().ReadPtr();
	if (e.hash == 0 || e.name != name || e.scene != scene) {
		if (e.hash) {
			m_bridge->mesh_destroy(e.hash);
			m_hashes.erase(e.hash);
		}
		e.hash = UniqueHash("halos", name, (const char *)obj + 1);
		e.scene = scene;
		e.name = name;
		e.content = 0;
	}
	e.last_seen = m_frames;
	if (e.content != g.content) {
		if (!SendGeometry(e.hash, g)) {
			e.content = 0;
			return false;
		}
		e.content = g.content;
	}
	brb_draw_desc d;
	memset(&d, 0, sizeof(d));
	d.mesh = e.hash;
	memcpy(d.model, model, sizeof(d.model));
	d.flags = BRB_DRAW_DOUBLE_SIDED | BRB_DRAW_BLEND;
	d.pick_id = pick;
	m_bridge->frame_draw_ex(&d);
	return true;
}

/* ------------------------------------------------------------------------- */
/* Blender particle systems as Remix GPU particles (P4.6) */

/* P8.30: Face Texture text (a bitmap font image and the object's Text
 * property): each letter a quad cut from the font image, placed as the game
 * places it (GPU_render_text_layer), in the object's space */
static bool mesh_has_face_text(RAS_MeshObject *mesh)
{
	for (std::list<RAS_MeshMaterial>::iterator mit = mesh->GetFirstMaterial(); mit != mesh->GetLastMaterial(); ++mit) {
		RAS_IPolyMaterial *pm = mit->m_bucket ? mit->m_bucket->GetPolyMaterial() : NULL;
		if (pm && (pm->GetDrawingMode() & RAS_IRasterizer::RAS_RENDER_3DPOLYGON_TEXT)) {
			return true;
		}
	}
	return false;
}

bool KX_RemixRenderer::DrawFaceText(KX_Scene *scene, KX_GameObject *obj, RAS_MeshObject *mesh, const float model[16],
                                    uint32_t pick)
{
	const STR_String &text = obj->GetPropertyText("Text");
	const char *str = text.ReadPtr();
	const size_t len = (size_t)text.Length();
	Geometry g;
	bool blend = false;
	for (std::list<RAS_MeshMaterial>::iterator mit = mesh->GetFirstMaterial(); mit != mesh->GetLastMaterial(); ++mit) {
		RAS_IPolyMaterial *pm = mit->m_bucket ? mit->m_bucket->GetPolyMaterial() : NULL;
		RAS_MeshSlot *slot = mit->m_baseslot;
		if (!pm || !slot || !(pm->GetDrawingMode() & RAS_IRasterizer::RAS_RENDER_3DPOLYGON_TEXT) || len == 0) {
			continue;
		}
		MTexPoly *tp = pm->GetMTexPoly();
		Image *ima = tp ? (Image *)tp->tpage : NULL;
		ImBuf *ibuf = ima ? BKE_image_get_first_ibuf(ima) : NULL;
		if (!ibuf) {
			continue;
		}
		const MaterialEntry &me = EnsureMaterial(scene, pm, NULL);
		blend = blend || me.blend;
		std::vector<brb_vertex> verts;
		std::vector<uint32_t> idx;
		float cx, cy, sx, sy, tx, ty, mx, my, advance;
		matrixGlyph(ibuf, ' ', &cx, &cy, &sx, &sy, &tx, &ty, &mx, &my, &advance);
		const float advance_tab = advance * 4.0f;
		RAS_MeshSlot::iterator it;
		for (slot->begin(it); !slot->end(it); slot->next(it)) {
			const int type = it.array->m_type;
			if (type != RAS_DisplayArray::TRIANGLE && type != RAS_DisplayArray::QUAD) {
				continue;
			}
			for (size_t i = 0; i + (size_t)type <= it.totindex; i += type) {
				float v[4][3], uv[4][2];
				for (int j = 0; j < type; j++) {
					const RAS_TexVert &tv = it.vertex[it.index[i + j]];
					memcpy(v[j], tv.getXYZ(), sizeof(v[j]));
					memcpy(uv[j], tv.getUV(0), sizeof(uv[j]));
				}
				/* (as the game: its quad case reads the fourth corner's z) */
				float lo = std::min(std::min(v[0][1], v[1][1]), v[2][1]);
				float hi = std::max(std::max(v[0][1], v[1][1]), v[2][1]);
				if (type == RAS_DisplayArray::QUAD) {
					lo = std::min(lo, v[3][2]);
					hi = std::max(hi, v[3][2]);
				}
				const float line_height = (hi - lo) * 1.2f;
				float offx = 0.0f, offy = 0.0f, line_start = 0.0f;
				for (size_t index = 0; index < len;) {
					unsigned int ch = BLI_str_utf8_as_unicode_and_size_safe(str + index, &index);
					if (ch == '\n') {
						offx += line_start;
						offy -= line_height;
						line_start = 0.0f;
						continue;
					}
					if (ch == '\t') {
						offx += advance_tab;
						line_start -= advance_tab;
						continue;
					}
					if (ch > 0xffff) {
						ch = '?';
					}
					matrixGlyph(ibuf, (unsigned short)ch, &cx, &cy, &sx, &sy, &tx, &ty, &mx, &my, &advance);
					const uint32_t base = (uint32_t)verts.size();
					for (int j = 0; j < type; j++) {
						brb_vertex o;
						memset(&o, 0, sizeof(o));
						o.position[0] = sx * v[j][0] + mx + offx;
						o.position[1] = sy * v[j][1] + my + offy;
						o.position[2] = v[j][2];
						o.normal[2] = 1.0f;
						o.uv[0] = (uv[j][0] - cx) * sx + tx;
						o.uv[1] = (uv[j][1] - cy) * sy + ty;
						o.color = 0xffffffffu;
						verts.push_back(o);
					}
					const uint32_t tri[6] = {0, 1, 2, 0, 2, 3};
					for (int k = 0; k < (type == RAS_DisplayArray::QUAD ? 6 : 3); k++) {
						idx.push_back(base + tri[k]);
					}
					offx += advance;
					line_start -= advance;
				}
			}
		}
		BKE_image_release_ibuf(ima, ibuf, NULL);
		if (!idx.empty()) {
			g.verts.push_back(verts);
			g.indices.push_back(idx);
			g.materials.push_back(me.hash);
		}
	}
	if (g.verts.empty()) {
		return false;
	}
	/* normals: the quads' own (the letters lie in the faces' plane) */
	for (size_t s = 0; s < g.verts.size(); s++) {
		std::vector<brb_vertex> &vv = g.verts[s];
		for (size_t t = 0; t + 2 < g.indices[s].size(); t += 3) {
			const float *a = vv[g.indices[s][t]].position, *b = vv[g.indices[s][t + 1]].position,
			            *c = vv[g.indices[s][t + 2]].position;
			float n[3];
			normal_tri_v3(n, a, b, c);
			for (int k = 0; k < 3; k++) {
				memcpy(vv[g.indices[s][t + k]].normal, n, sizeof(n));
			}
		}
		g.content = hash_bytes(&vv[0], vv.size() * sizeof(brb_vertex), g.content ? g.content : 1469598103934665603ull);
	}
	DeformEntry &e = m_deformed[(const char *)obj + 3];
	const std::string name = obj->GetName().ReadPtr();
	if (e.hash == 0 || e.name != name || e.scene != scene) {
		if (e.hash) {
			m_bridge->mesh_destroy(e.hash);
			m_hashes.erase(e.hash);
		}
		e.hash = UniqueHash("facetext", name, (const char *)obj + 3);
		e.scene = scene;
		e.name = name;
		e.content = 0;
	}
	e.last_seen = m_frames;
	if (e.content != g.content) {
		if (!SendGeometry(e.hash, g)) {
			e.content = 0;
			return false;
		}
		e.content = g.content;
	}
	brb_draw_desc d;
	memset(&d, 0, sizeof(d));
	d.mesh = e.hash;
	memcpy(d.model, model, sizeof(d.model));
	d.flags = BRB_DRAW_DOUBLE_SIDED | (blend ? BRB_DRAW_BLEND : 0);
	d.pick_id = pick;
	m_bridge->frame_draw_ex(&d);
	return true;
}

/* P8.30: the material Remix draws a particle system's particles with. The
 * game draws particles unlit, in their material's colour and image (halos
 * as glows), so Remix gets them as a glow too (the RTX run: lit, they came
 * out as dark squares in dark scenes). 0: none (the emitter's own) */
/* P8.30: how the particles' material is made (BGE_REMIX_PARTICLE_MAT). The
 * third diagnostic run: Remix's particles show neither a glow nor blending
 * (blended, they vanish; unblended, they are lit), so they keep the
 * emitter's own material ("lit", as before) */
enum { PMAT_LIT = 0, PMAT_LITGLOW, PMAT_GLOW, PMAT_GLOW_NOBLEND };
static int particle_mat_mode()
{
	static int mode = -1;
	if (mode < 0) {
		const char *env = getenv("BGE_REMIX_PARTICLE_MAT");
		mode = PMAT_LIT;
		if (env && env[0]) {
			mode = !strcmp(env, "litglow") ? PMAT_LITGLOW : !strcmp(env, "glow") ? PMAT_GLOW :
			       !strcmp(env, "glow_noblend") ? PMAT_GLOW_NOBLEND : PMAT_LIT;
			printf("BGE-Remix: BGE_REMIX_PARTICLE_MAT %s\n", env);
		}
	}
	return mode;
}

uint64_t KX_RemixRenderer::ParticleMaterial(KX_Scene *scene, Object *ob, ParticleSystem *psys)
{
	ParticleSettings *part = psys->part;
	const int mode = particle_mat_mode();
	if (mode == PMAT_LIT) {
		return 0;
	}
	Material *ma = give_current_material(ob, part->omat);
	if (!ma) {
		return 0;
	}
	if (part->ren_as == PART_DRAW_HALO) {
		return HaloMaterial(ma);
	}
	const std::pair<const void *, const void *> key(ma, psys);
	const std::string name = std::string(ma->id.name + 2) + "@" + psys->name;
	std::map<std::pair<const void *, const void *>, MaterialEntry>::iterator it = m_materials.find(key);
	if (it != m_materials.end() && it->second.name == name && it->second.scene == scene) {
		return it->second.hash;
	}
	MaterialEntry e;
	e.spec.reset(new brmx::MaterialSpec());
	brmx::translate_material(ma, *e.spec);
	brb_material_desc &d = e.spec->desc;
	if (e.spec->textures[brmx::TEX_ALBEDO].image && !e.spec->textures[brmx::TEX_EMISSIVE].image) {
		e.spec->textures[brmx::TEX_EMISSIVE] = e.spec->textures[brmx::TEX_ALBEDO];
	}
	d.emissive[0] = ma->r;
	d.emissive[1] = ma->g;
	d.emissive[2] = ma->b;
	d.emissive_intensity = 1.0f;
	if (mode != PMAT_LITGLOW) {
		d.albedo[0] = d.albedo[1] = d.albedo[2] = 0.0f;
		d.metallic = 0.0f;
		d.roughness = 1.0f;
	}
	if (mode == PMAT_GLOW_NOBLEND) {
		d.alpha_blend = 0;
		d.opacity = 1.0f;
	}
	else if (ma->game.alpha_blend == GEMAT_ADD) {
		d.alpha_blend = 1;
		d.additive = 1;
	}
	d.hash = UniqueHash("particles", name, &m_materials[key]);
	e.hash = d.hash;
	e.scene = scene;
	e.name = name;
	e.blend = d.alpha_blend != 0;
	e.particle = true;
	SendMaterial(e);
	m_materials[key] = e;
	return e.hash;
}

/* P8.30: the emitter's mesh with the particles' material (Remix's particles
 * take the emitter's material; the emitter itself stays hidden) */
uint64_t KX_RemixRenderer::EmitterMesh(KX_Scene *scene, KX_GameObject *obj, RAS_MeshObject *mesh,
                                       ParticleSystem *psys, uint64_t material)
{
	DeformEntry &e = m_deformed[(const char *)psys + 2];
	const std::string name = std::string(obj->GetName().ReadPtr()) + "@" + psys->name;
	e.last_seen = m_frames;
	if (e.hash && e.name == name && e.scene == scene && e.content == material) {
		return e.hash;
	}
	if (e.hash) {
		m_bridge->mesh_destroy(e.hash);
		m_hashes.erase(e.hash);
	}
	Geometry g;
	Collect(scene, mesh, NULL, NULL, g);
	for (size_t i = 0; i < g.materials.size(); i++) {
		g.materials[i] = material;
	}
	e.hash = UniqueHash("emitter", name, (const char *)psys + 2);
	e.scene = scene;
	e.name = name;
	e.content = material;
	e.double_sided = true;
	e.blend = true;
	e.particle = true;
	e.categories = 0;
	if (!SendGeometry(e.hash, g)) {
		m_hashes.erase(e.hash);
		e.hash = 0;
	}
	return e.hash;
}

bool KX_RemixRenderer::DrawEmitters(KX_Scene *scene, KX_GameObject *obj, Object *ob, const float model[16],
                                    uint32_t pick)
{
	RAS_MeshObject *mesh = obj->GetMeshCount() ? obj->GetMesh(0) : NULL;
	MeshEntry *me = NULL;
	if (!mesh || !EnsureMesh(scene, mesh, me) || !me) {
		return false;
	}
	Scene *bscene = scene->GetBlenderScene();
	bool any = false, hide = true;
	for (ParticleSystem *psys = (ParticleSystem *)ob->particlesystem.first; psys; psys = psys->next) {
		ParticleSettings *part = psys->part;
		if (!part || part->type != PART_EMITTER || (psys->flag & PSYS_DISABLED) || part->totpart <= 0) {
			continue;
		}
		/* BGE-Remix P8 Part C (P8.20): None and Path systems draw nothing;
		 * Object and Group particles are the game's own objects (KX_Sprites),
		 * which Remix draws as such */
		if (part->ren_as == PART_DRAW_NOT || part->ren_as == PART_DRAW_PATH || part->ren_as == PART_DRAW_OB ||
		    part->ren_as == PART_DRAW_GR)
		{
			if (part->draw & PART_DRAW_EMITTER) {
				hide = false;
			}
			continue;
		}
		/* the description the game's own simulation uses too */
		GPUParticleDesc gd;
		KX_ParticleDescription(bscene, psys, m_gravity, m_colliders, m_collider_bounce, &gd);
		brb_particle_desc p;
		memset(&p, 0, sizeof(p));
		p.max_particles = (uint32_t)gd.max_particles;
		p.spawn_rate = gd.spawn_rate;
		p.lifetime_min = gd.lifetime_min;
		p.lifetime_max = gd.lifetime_max;
		p.speed_normal = gd.speed_normal;
		p.cone_angle_deg = gd.cone_angle_deg;
		p.speed_from_motion = gd.speed_from_motion;
		p.gravity = gd.gravity;
		p.drag = gd.drag;
		p.size_min = gd.size_min;
		p.size_max = gd.size_max;
		p.size_end_scale = gd.size_end_scale;
		memcpy(p.color_start, gd.color_start, sizeof(p.color_start));
		memcpy(p.color_end, gd.color_end, sizeof(p.color_end));
		p.rotation_speed = gd.rotation_speed;
		p.collide = gd.collide;
		p.restitution = gd.restitution;
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
			NoteOnce(std::string("particle system \"") + psys->name + "\": shown as sprites (Line rendering)");
		}
		if (part->draw & PART_DRAW_EMITTER) {
			hide = false;
		}
		/* P8.30: Remix's newer particle fields: a random start rotation from
		 * Blender's Random rotation; the RTX runs try the others
		 * (BGE_REMIX_PARTICLE_SPAWN_UV=1, BGE_REMIX_PARTICLE_SPRITE_MODE=n) */
		p.rotation_deviation_deg = std::min(std::max(part->randrotfac, 0.0f), 1.0f) * 180.0f;
		{
			const char *uv = getenv("BGE_REMIX_PARTICLE_SPAWN_UV");
			const char *sm = getenv("BGE_REMIX_PARTICLE_SPRITE_MODE");
			p.use_spawn_texcoords = (uv && uv[0] == '1') ? 1 : 0;
			p.sprite_sheet_mode = (sm && sm[0]) ? atoi(sm) : 0;
		}
		/* P8.30 diagnostics: BGE_REMIX_PARTICLE_TEST=1 sends plain, large,
		 * slow, opaque white particles; the description said once */
		const char *ptest = getenv("BGE_REMIX_PARTICLE_TEST");
		if (ptest && ptest[0] == '1') {
			p.max_particles = 500;
			p.spawn_rate = 50.0f;
			p.lifetime_min = 3.0f;
			p.lifetime_max = 4.0f;
			p.speed_normal = 1.0f;
			p.cone_angle_deg = 20.0f;
			p.speed_from_motion = 0.0f;
			p.gravity = 0.0f;
			p.drag = 0.0f;
			p.size_min = p.size_max = 0.3f;
			p.size_end_scale = 1.0f;
			for (int k = 0; k < 4; k++) {
				p.color_start[k] = p.color_end[k] = 1.0f;
			}
			p.rotation_speed = 0.0f;
			p.collide = 0;
			p.billboard = 0;
			p.align_to_velocity = 0;
		}
		if (m_particles_said.insert(psys).second) {
			printf("BGE-Remix particles \"%s\" to Remix%s: max %u, %.1f/s, life %.2f-%.2f s, speed %.3f, cone %.1f, "
			       "gravity %.2f, drag %.2f, size %.3f-%.3f (end x%.2f), colour %.2f %.2f %.2f %.2f -> %.2f %.2f %.2f %.2f, "
			       "billboard %d, velocity-aligned %d\n", psys->name, (ptest && ptest[0] == '1') ? " (test values)" : "",
			       p.max_particles, p.spawn_rate, p.lifetime_min, p.lifetime_max, p.speed_normal, p.cone_angle_deg,
			       p.gravity, p.drag, p.size_min, p.size_max, p.size_end_scale, p.color_start[0], p.color_start[1],
			       p.color_start[2], p.color_start[3], p.color_end[0], p.color_end[1], p.color_end[2], p.color_end[3],
			       p.billboard, p.align_to_velocity);
		}
		brb_draw_desc d;
		memset(&d, 0, sizeof(d));
		d.mesh = me->hash;
		d.flags = me->double_sided ? BRB_DRAW_DOUBLE_SIDED : 0;
		/* P8.30: the particles glow in their material's colour and image */
		const uint64_t pmat = ParticleMaterial(scene, ob, psys);
		const uint64_t pmesh = pmat ? EmitterMesh(scene, obj, mesh, psys, pmat) : 0;
		if (pmesh) {
			d.mesh = pmesh;
			d.flags = BRB_DRAW_DOUBLE_SIDED | (particle_mat_mode() == PMAT_GLOW_NOBLEND ? 0 : BRB_DRAW_BLEND);
		}
		memcpy(d.model, model, sizeof(d.model));
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
	 * light), plus the hemi lamps; BGE-Remix P8 Part B (P8.17): a blend or
	 * textured sky, or a sun lamp's, as the dome's picture */
	KX_WorldInfo *world = scene->GetWorldInfo();
	uint64_t skykey = 0;
	const wchar_t *sky = SkyTexture(scene, &skykey);
	/* P8.30 diagnostics: BGE_REMIX_DOME "ambient" lights with what lights
	 * the game (the world's ambient colour, plus its horizon colour times
	 * Environment Lighting's energy when that is on), a number scales the
	 * dome */
	const char *dome_env = getenv("BGE_REMIX_DOME");
	const bool dome_ambient = dome_env && strcmp(dome_env, "ambient") == 0;
	const float dome_scale = (dome_env && dome_env[0] && !dome_ambient) ? (float)atof(dome_env) : 1.0f;
	Scene *dome_sce = scene->GetBlenderScene();
	World *dome_wo = dome_sce ? dome_sce->world : NULL;
	if (sky) {
		dome[0] = dome[1] = dome[2] = 1.0f;
	}
	else if (dome_ambient && dome_wo) {
		const float amb[3] = {dome_wo->ambr, dome_wo->ambg, dome_wo->ambb};
		const float env = (dome_wo->mode & WO_ENV_LIGHT) ? dome_wo->ao_env_energy : 0.0f;
		for (int k = 0; k < 3; k++) {
			dome[k] += amb[k] + (&dome_wo->horr)[k] * env;
		}
	}
	else if (world && world->hasWorld()) {
		const float *bg = world->getBackColor();
		for (int k = 0; k < 3; k++) {
			dome[k] += bg[k];
		}
	}
	if (dome_env && dome_env[0] && !dome_ambient) {
		for (int k = 0; k < 3; k++) {
			dome[k] *= std::max(dome_scale, 0.0f);
		}
	}
	static bool dome_said = false;
	if (dome_env && dome_env[0] && !dome_said) {
		printf("BGE-Remix: BGE_REMIX_DOME %s: the dome %.4f %.4f %.4f\n", dome_env, dome[0], dome[1], dome[2]);
		dome_said = true;
	}
	brb_light_desc d;
	memset(&d, 0, sizeof(d));
	/* a new picture is a new light (Remix's lights do not change) */
	d.hash = UniqueHash("dome", scene->GetName().ReadPtr(), world) ^ (skykey * 0x9E3779B97F4A7C15ull);
	if (d.hash == 0) {
		d.hash = 1;
	}
	if (brmx::dome_light(dome, d)) {
		d.dome_texture = sky;
		SendLight(scene, world, d);
	}
	UpdateFog(scene);
	UpdateCameraEffects(scene, scene->GetActiveCamera());

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

/* BGE-Remix P8 Part B (P8.17): the sky picture: drawn by the game's world
 * shader into an equirectangular picture when what it shows changed, and
 * written as a DDS named by its content (kept in the texture cache folder) */
static void sky_hash(uint64_t &h, const void *data, size_t size)
{
	const unsigned char *b = (const unsigned char *)data;
	for (size_t i = 0; i < size; i++) {
		h ^= b[i];
		h *= 1099511628211ull;
	}
}

const wchar_t *KX_RemixRenderer::SkyTexture(KX_Scene *scene, uint64_t *r_key)
{
	*r_key = 0;
	KX_WorldInfo *world = scene->GetWorldInfo();
	Scene *sce = scene->GetBlenderScene();
	World *wo = sce ? sce->world : NULL;
	if (!world || !world->hasWorld() || !world->DrawsSky() || !wo) {
		return NULL;
	}
	/* what the picture shows: the world's sky settings and textures, the
	 * colours the game sets, and a sun lamp's sky with its direction */
	uint64_t h = 14695981039346656037ull;
	sky_hash(h, &wo->skytype, sizeof(wo->skytype));
	sky_hash(h, world->getBackColor(), sizeof(float) * 3);
	sky_hash(h, &wo->zenr, sizeof(float) * 3);
	for (int i = 0; i < MAX_MTEX; i++) {
		MTex *mtex = wo->mtex[i];
		if (!mtex || !mtex->tex) {
			continue;
		}
		sky_hash(h, mtex->tex->id.name, strlen(mtex->tex->id.name));
		if (mtex->tex->ima) {
			sky_hash(h, mtex->tex->ima->id.name, strlen(mtex->tex->ima->id.name));
		}
		sky_hash(h, &mtex->texco, sizeof(mtex->texco));
		sky_hash(h, &mtex->mapto, sizeof(mtex->mapto));
		sky_hash(h, mtex->ofs, sizeof(mtex->ofs));
		sky_hash(h, mtex->size, sizeof(mtex->size));
		sky_hash(h, &mtex->colfac, sizeof(float) * 4);
		sky_hash(h, &mtex->tex->noisesize, sizeof(float) * 8);
	}
	CListValue *lights = scene->GetLightList();
	for (int i = 0; i < lights->GetCount(); i++) {
		KX_LightObject *light = (KX_LightObject *)lights->GetValue(i);
		Object *ob = light->GetBlenderObject();
		Lamp *la = (ob && ob->type == OB_LAMP) ? (Lamp *)ob->data : NULL;
		if (la && la->type == LA_SUN && (la->sun_effect_type & LA_SUN_EFFECT_SKY)) {
			const MT_Vector3 z = light->NodeGetWorldOrientation().getColumn(2);
			const int q[3] = {(int)floor(z[0] * 1000.0 + 0.5), (int)floor(z[1] * 1000.0 + 0.5),
			                  (int)floor(z[2] * 1000.0 + 0.5)};
			sky_hash(h, q, sizeof(q));
			sky_hash(h, &la->horizon_brightness, sizeof(float) * 6);
			sky_hash(h, &la->atm_turbidity, sizeof(float));
			sky_hash(h, &la->skyblendtype, sizeof(short));
			sky_hash(h, &la->skyblendfac, sizeof(float));
			sky_hash(h, &la->sky_exposure, sizeof(float));
			sky_hash(h, &la->sky_colorspace, sizeof(short));
			break;
		}
	}
	if (h == m_skykey && !m_skypath.empty()) {
		*r_key = h;
		return m_skypath.c_str();
	}

	const int width = 1024, height = width / 2;
	wchar_t hex[24];
	swprintf(hex, 24, L"%016llx", (unsigned long long)h);
	const std::wstring folder = brmx::TextureCache::folder();
	CreateDirectoryW(folder.c_str(), NULL);
	const std::wstring path = folder + L"\\sky_k" + hex + L".dds";
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
		std::vector<float> pixels((size_t)width * height * 4);
		if (!GPU_world_equirect(sce, wo, width, &pixels[0])) {
			return NULL;
		}
		brb_texture_desc desc;
		memset(&desc, 0, sizeof(desc));
		desc.path = path.c_str();
		desc.kind = BRB_TEXTURE_HDR;
		desc.width = width;
		desc.height = height;
		desc.rgbaf = &pixels[0];
		/* P8.24: Remix samples the sky by direction, in the order this
		 * file always had (interface 10's files otherwise start at the bottom) */
		desc.flags = BRB_TEXTURE_TOP_FIRST;
		if (m_bridge->texture_write(&desc, NULL) != 0) {
			return NULL;
		}
		printf("BGE-Remix: the world's sky sent to Remix as a picture\n");
	}
	m_skykey = h;
	m_skypath = path;
	*r_key = h;
	return m_skypath.c_str();
}

/* BGE-Remix P8 Part C (P8.23): in a Modern scene, motion blur (the Render
 * tab's), bloom, auto exposure and vignette (the Modern panel's) drive
 * Remix's own. A Classic scene leaves Remix's defaults alone (its bloom and
 * auto exposure are on), as before P8: the P8.30 RTX run found older scenes
 * brighter or darker in Remix once both were switched off for them. Remix's
 * runtime has no depth of field option (rtx.dof.* is refused). */
void KX_RemixRenderer::UpdateCameraEffects(KX_Scene *scene, KX_Camera *cam)
{
	Scene *sc = scene->GetBlenderScene();
	static bool forced = false;  /* P8.30 diagnostics: Classic scenes too */
	if (!forced) {
		ForcedAutoExposure();
		forced = true;
	}
	if (!sc || BKE_scene_game_pipeline(sc) != 1) {
		return;
	}
	const bool dof = BKE_scene_game_fx(sc, "depth_of_field", 0.0f) != 0.0f;
	float fstop = 2.8f, focus = 10.0f;
	Object *camob = cam ? cam->GetBlenderObject() : sc->camera;
	if (camob && camob->type == OB_CAMERA && camob->data) {
		const Camera *ca = (const Camera *)camob->data;
		fstop = ca->gpu_dof.fstop;
		focus = (ca->YF_dofdist > 0.0f) ? ca->YF_dofdist : ca->gpu_dof.focus_distance;
		if (ca->dof_ob) {
			/* the focus object's distance along the view, as the game's
			 * depth of field (RAS_Pipeline::ScreenEffectSettings) */
			float d[3];
			sub_v3_v3v3(d, ca->dof_ob->obmat[3], camob->obmat[3]);
			float axis[3] = {-camob->obmat[2][0], -camob->obmat[2][1], -camob->obmat[2][2]};
			normalize_v3(axis);
			focus = std::max(dot_v3v3(d, axis), 0.01f);
		}
	}
	const bool mblur = (sc->r.mode & R_MBLUR) != 0;
	const bool bloom = BKE_scene_game_fx(sc, "bloom", 0.0f) != 0.0f;
	const float bloom_intensity = BKE_scene_game_fx(sc, "bloom_intensity", 0.05f);
	const bool autoexp = BKE_scene_game_fx(sc, "auto_exposure", 0.0f) != 0.0f;
	const float vignette = BKE_scene_game_fx(sc, "vignette", 0.0f);
	char buf[512];
	snprintf(buf, sizeof(buf), "%d %.3f %.3f %d %.3f %d %.3f %d %.3f", dof, fstop, focus, mblur, sc->r.blurfac, bloom,
	         bloom_intensity, autoexp, vignette);
	if (m_fx_sent == buf) {
		return;
	}
	m_fx_sent = buf;
	const char *on[2] = {"False", "True"};
	char v[64];
	m_bridge->set_config("rtx.postfx.enable", on[mblur || vignette > 0.0f]);
	m_bridge->set_config("rtx.postfx.enableMotionBlur", on[mblur]);
	snprintf(v, sizeof(v), "%.3f", sc->r.blurfac);
	m_bridge->set_config("rtx.postfx.exposureFraction", v);
	m_bridge->set_config("rtx.postfx.enableVignette", on[vignette > 0.0f]);
	snprintf(v, sizeof(v), "%.3f", vignette);
	m_bridge->set_config("rtx.postfx.vignetteIntensity", v);
	m_bridge->set_config("rtx.bloom.enable", on[bloom]);
	snprintf(v, sizeof(v), "%.3f", bloom_intensity * 20.0f);
	m_bridge->set_config("rtx.bloom.burnIntensity", v);
	m_bridge->set_config("rtx.autoExposure.enabled", on[autoexp]);
	ForcedAutoExposure();
	printf("BGE-Remix: camera effects to Remix: motion blur %s, bloom %s, auto exposure %s, vignette %.2f "
	       "(depth of field %s: Remix has none)\n", on[mblur], on[bloom], on[autoexp], vignette, on[dof]);
}

/* P8.30 diagnostics: BGE_REMIX_AUTOEXPOSURE 0 or 1 forces Remix's auto
 * exposure, in any scene */
void KX_RemixRenderer::ForcedAutoExposure()
{
	const char *env = getenv("BGE_REMIX_AUTOEXPOSURE");
	if (env && (env[0] == '0' || env[0] == '1')) {
		m_bridge->set_config("rtx.autoExposure.enabled", env[0] == '1' ? "True" : "False");
		static bool said = false;
		if (!said) {
			printf("BGE-Remix: BGE_REMIX_AUTOEXPOSURE %s: Remix's auto exposure forced\n", env);
			said = true;
		}
	}
}

/* BGE-Remix P8 Part B (P8.17): the world's mist as Remix's fog (its
 * volumetrics): the light left after the mist's distance and the mist's
 * colour as what the fog scatters; sent when it changes */
void KX_RemixRenderer::UpdateFog(KX_Scene *scene)
{
	KX_WorldInfo *world = scene->GetWorldInfo();
	Scene *sce = scene->GetBlenderScene();
	World *wo = sce ? sce->world : NULL;
	char key[256] = "";
	float distance = 0.0f, transmit[3] = {0.05f, 0.05f, 0.05f}, albedo[3] = {1.0f, 1.0f, 1.0f};

	/* P8.19: the volume object the camera is in: its density and colours */
	if ((m_volume_scan++ % 60) == 0) {
		for (size_t i = 0; i < m_volume_objs.size(); i++)
			m_volume_objs[i]->Release();
		m_volume_objs.clear();
		CListValue *objects = scene->GetObjectList();
		for (int i = 0; i < objects->GetCount(); i++) {
			KX_GameObject *obj = (KX_GameObject *)objects->GetValue(i);
			Object *ob = obj->GetBlenderObject();
			for (int c = 1; ob && ob->type == OB_MESH && c <= ob->totcol; c++) {
				Material *ma = give_current_material(ob, c);
				if (ma && ma->material_type == MA_TYPE_VOLUME) {
					obj->AddRef();
					m_volume_objs.push_back(obj);
					break;
				}
			}
		}
	}
	KX_Camera *cam = scene->GetActiveCamera();
	for (size_t i = 0; cam && i < m_volume_objs.size() && !key[0]; i++) {
		KX_GameObject *obj = m_volume_objs[i];
		Object *ob = obj->GetBlenderObject();
		BoundBox *bb = ob ? BKE_object_boundbox_get(ob) : NULL;
		if (!bb || !obj->GetVisible())
			continue;
		MT_Transform t = obj->GetSGNode()->GetWorldTransform();
		t.invert(t);
		const MT_Point3 p = t(cam->NodeGetWorldPosition());
		float lo[3], hi[3];
		INIT_MINMAX(lo, hi);
		for (int k = 0; k < 8; k++)
			minmax_v3v3_v3(lo, hi, bb->vec[k]);
		if (p[0] < lo[0] || p[1] < lo[1] || p[2] < lo[2] || p[0] > hi[0] || p[1] > hi[1] || p[2] > hi[2])
			continue;
		Material *ma = NULL;
		for (int c = 1; c <= ob->totcol && !ma; c++) {
			Material *m = give_current_material(ob, c);
			ma = (m && m->material_type == MA_TYPE_VOLUME) ? m : NULL;
		}
		if (!ma)
			continue;
		/* the light left after one unit of density is the transmission
		 * colour (as F12's absorption): Remix measures it over the distance
		 * that holds one unit */
		distance = 1.0f / std::max(ma->vol.density * ma->vol.density_scale, 1e-3f);
		copy_v3_v3(transmit, ma->vol.transmission_col);
		for (int k = 0; k < 3; k++)
			albedo[k] = std::min(ma->vol.reflection * ma->vol.reflection_col[k], 1.0f);
		snprintf(key, sizeof(key), "volume %s %.4f|%.3f %.3f %.3f|%.3f %.3f %.3f", ob->id.name + 2, distance,
		         transmit[0], transmit[1], transmit[2], albedo[0], albedo[1], albedo[2]);
	}

	/* the world's mist: what passes start + distance is 5 %.
	 * P8.30: left out unless BGE_REMIX_MIST_FOG asks for it: Remix's fog
	 * turned the RTX run's whole picture black, however thin. A number
	 * sends it, scaling its distance (larger: thinner fog) */
	static float mist_factor = -1.0f;
	if (mist_factor < 0.0f) {
		const char *env = getenv("BGE_REMIX_MIST_FOG");
		const float f = (env && env[0]) ? (float)atof(env) : 0.0f;
		mist_factor = (f > 0.0f && f < 1e4f) ? f : 0.0f;
		if (env && env[0]) {
			printf("BGE-Remix: BGE_REMIX_MIST_FOG %s: the world's mist %s\n", env,
			       mist_factor > 0.0f ? "goes to Remix as fog" : "is left out of Remix");
		}
	}
	const bool mist = !key[0] && world && world->hasWorld() && wo && (wo->mode & WO_MIST);
	if (mist && mist_factor > 0.0f) {
		distance = std::max(wo->miststa + wo->mistdist, 0.1f) * mist_factor;
		copy_v3_v3(albedo, world->getBackColor());
		snprintf(key, sizeof(key), "mist %.4f|%.4f, %.4f, %.4f", distance, albedo[0], albedo[1], albedo[2]);
	}
	else if (mist && !m_mist_said) {
		printf("BGE-Remix: the world's mist is left out of Remix (Remix's fog turns the picture black; "
		       "BGE_REMIX_MIST_FOG=1 sends it)\n");
		m_mist_said = true;
	}
	if (m_fogsent == key)
		return;
	m_fogsent = key;
	char value[96];
	if (!key[0]) {
		/* neither: clear air again */
		m_bridge->set_config("rtx.volumetrics.transmittanceMeasurementDistanceMeters", "10000.0");
		m_bridge->set_config("rtx.volumetrics.transmittanceColor", "1.0, 1.0, 1.0");
		printf("BGE-Remix: Remix's fog cleared\n");
		return;
	}
	m_bridge->set_config("rtx.volumetrics.enable", "True");
	snprintf(value, sizeof(value), "%.4f", distance);
	m_bridge->set_config("rtx.volumetrics.transmittanceMeasurementDistanceMeters", value);
	snprintf(value, sizeof(value), "%.4f, %.4f, %.4f", transmit[0], transmit[1], transmit[2]);
	m_bridge->set_config("rtx.volumetrics.transmittanceColor", value);
	snprintf(value, sizeof(value), "%.4f, %.4f, %.4f", albedo[0], albedo[1], albedo[2]);
	m_bridge->set_config("rtx.volumetrics.singleScatteringAlbedo", value);
	printf("BGE-Remix: Remix's fog from the %s\n", key);
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
	UpdateLiveTextures();

	float v[16], p[16], rv[16], rp[16];
	view.getValue(v);
	proj.getValue(p);
	/* P8.30: an orthographic camera as a distant, narrow perspective one */
	if (brmx::camera_for_remix(v, p, rv, rp)) {
		static bool said = false;
		if (!said) {
			printf("BGE-Remix: an orthographic camera reaches Remix as a distant, narrow-angle perspective one\n");
			said = true;
		}
	}
	m_bridge->frame_camera(rv, rp);

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

	/* P8.30: copies of one mesh gathered for one instanced draw */
	struct InstanceGroup {
		brb_draw_desc first;
		std::vector<float> models;
	};
	static const bool gpu_instancing = !(getenv("BGE_REMIX_GPU_INSTANCING") &&
	                                     getenv("BGE_REMIX_GPU_INSTANCING")[0] == '0');
	std::map<std::tuple<uint64_t, int, uint32_t>, InstanceGroup> groups;
	m_instanced_groups = 0;
	for (int i = 0; i < objects->GetCount(); i++) {
		KX_GameObject *obj = (KX_GameObject *)objects->GetValue(i);
		/* P8.22: text objects as their text's geometry */
		if (obj->GetVisible() && obj->GetGameObjectType() == SCA_IObject::OBJ_TEXT) {
			RAS_MeshObject *tm = static_cast<KX_FontObject *>(obj)->GetRemixMesh();
			MeshEntry *te = NULL;
			if (tm && EnsureMesh(scene, tm, te) && te) {
				brb_draw_desc d;
				memset(&d, 0, sizeof(d));
				d.mesh = te->hash;
				object_matrix(obj, d.model);
				d.flags = (te->double_sided ? BRB_DRAW_DOUBLE_SIDED : 0) | (te->blend ? BRB_DRAW_BLEND : 0);
				d.pick_id = PickId(obj);
				m_bridge->frame_draw_ex(&d);
				draws++;
			}
			continue;
		}
		if (!obj->GetVisible() || obj->GetMeshCount() == 0) {
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
		Object *ob = obj->GetBlenderObject();
		/* P8.20: halo materials as sprites (their faces are not drawn) */
		if (ob && ob->type == OB_MESH && halo_material_of(ob)) {
			MT_Matrix3x3 camori;
			camori.setIdentity();
			if (cam) {
				camori = cam->NodeGetWorldOrientation();
			}
			if (DrawHalos(scene, obj, ob, model, camori, pick)) {
				draws++;
			}
			continue;
		}
		/* Blender particle systems */
		bool hide_emitter = false;
		if (ob && ob->particlesystem.first) {
			hide_emitter = DrawEmitters(scene, obj, ob, model, pick);
		}
		/* P8.30: Face Texture text, letter by letter */
		if (obj->GetMeshCount() > 0 && mesh_has_face_text(obj->GetMesh(0)) &&
		    DrawFaceText(scene, obj, obj->GetMesh(0), model, pick))
		{
			draws++;
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
			d.categories = (e->particle ? BRB_CATEGORY_PARTICLE : 0) | e->categories;
			m_particle_draws += e->particle ? 1 : 0;
			/* P8.20: hair ribbons (BL_HairCards) */
			if (strncmp(obj->GetMesh(m)->GetName().ReadPtr(), "BGE-Remix hair", 14) == 0) {
				d.categories |= BRB_CATEGORY_HAIR_CARDS;
			}
			d.pick_id = pick;
			/* P8.30: copies of one mesh in one call (Remix's GPU instancing;
			 * BGE_REMIX_GPU_INSTANCING=0 turns it off); not billboards (their
			 * matrix is the camera's) nor particles; picking keeps them apart */
			if (gpu_instancing && !e->billboard && !e->particle && !m_pick_wanted) {
				InstanceGroup &grp = groups[std::make_tuple(d.mesh, d.flags, d.categories)];
				if (grp.models.empty()) {
					grp.first = d;
				}
				grp.models.insert(grp.models.end(), d.model, d.model + 16);
				continue;
			}
			m_bridge->frame_draw_ex(&d);
			draws++;
		}
	}
	for (auto &it : groups) {
		InstanceGroup &grp = it.second;
		const uint32_t count = (uint32_t)(grp.models.size() / 16);
		if (count == 1) {
			m_bridge->frame_draw_ex(&grp.first);
		}
		else {
			m_bridge->frame_draw_instanced(&grp.first, grp.models.data(), count);
			m_instanced_groups++;
		}
		draws++;
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
	fprintf(f, "  \"instanced_groups\": %d,\n  \"live_texture_updates\": %d,\n", m_instanced_groups,
	        m_live_updates);
	fprintf(f, "  \"procedural_bakes\": %d,\n  \"procedural_rebakes\": %d,\n  \"procedural_bake_ms\": %.2f,\n",
	        m_bakes, m_rebakes, m_bake_ms);
	fprintf(f, "  \"procedural_bakes_cached\": %d,\n  \"procedural_bake_gpu_ms\": %.2f,\n",
	        m_bakes_cached, m_bake_gpu_ms);
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
