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

extern "C" {
#  include "BKE_idprop.h"
#  include "DNA_ID.h"
#  include "DNA_lamp_types.h"
#  include "DNA_material_types.h"
#  include "DNA_object_types.h"
#  include "DNA_scene_types.h"
}

#include "BRMX_loader.h"  /* C linkage inside */

/* The world colour and the lamps, in Blender Internal's units, are turned
 * into Remix's physical units so that a white surface lit by a lamp of
 * energy 1 at distance D comes out as bright as in Blender Internal (far
 * from the lamp); bge_remix_light_scale is a global factor on top, tuned by
 * eye against the room's Cycles render (P2 sign-off). */
static const float SUN_ANGULAR_DIAMETER_DEG = 0.53f;
static const float MIN_LIGHT_RADIUS = 0.02f;
static const double NOTICE_SECONDS = 8.0;
/* Chosen by eye against the room's Cycles render (P2 sign-off, RTX 3080):
 * with 1, the lamps outshine everything else under Remix's automatic
 * exposure (the glowing panel disappears) */
static const float DEFAULT_LIGHT_SCALE = 0.125f;
/* Chosen against the room's Cycles render (P2 sign-off, RTX 3080, run 7):
 * +2 stops keeps shadows and the spot pool's contrast; +3 burns out the
 * glowing panel. Same default as the Rasterizer panel's Exposure. */
static const float DEFAULT_EXPOSURE = 2.0f;
/* Remix's own default for rtx.localtonemap.exposure (1.6.1) */
static const double LOCAL_TONEMAP_EXPOSURE = 0.75;
static const double PI = 3.14159265358979323846;

/* ------------------------------------------------------------------------- */
/* Helpers */

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

static void normalize3(float v[3])
{
	const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (len > 1e-8f) {
		v[0] /= len;
		v[1] /= len;
		v[2] /= len;
	}
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

/* ------------------------------------------------------------------------- */
/* Settings */

KX_RemixRenderer::Settings KX_RemixRenderer::GetSettings(KX_Scene *scene, int override_enabled, int override_fast)
{
	Settings s = {0, 0, DEFAULT_LIGHT_SCALE, DEFAULT_EXPOSURE};
	Scene *bscene = scene ? scene->GetBlenderScene() : NULL;
	IDProperty *props = bscene ? bscene->id.properties : NULL;
	s.enabled = idprop_int(props, "bge_remix", 0) != 0;
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
      m_texture(0),
      m_tex_w(0),
      m_tex_h(0),
      m_notice_until(0.0),
      m_notice_shown(false),
      m_filters_noted(false),
      m_frames(0),
      m_last_draws(0),
      m_last_lights(0),
      m_screenshot_frame(120)
{
	m_settings.enabled = 0;
	m_settings.window_mode = 0;
	m_settings.light_scale = DEFAULT_LIGHT_SCALE;
	m_settings.exposure = DEFAULT_EXPOSURE;
	std::wstring f;
	if (env_path(L"BGE_REMIX_SCREENSHOT_FRAME", f)) {
		m_screenshot_frame = _wtoi(f.c_str());
	}
	m_status = "off";
}

KX_RemixRenderer::~KX_RemixRenderer()
{
	Trace("game end: writing stats");
	WriteStats();
	if (m_bridge && m_running) {
		/* Remix keeps running for the next game; this game's objects go. */
		ShowRemixWindow(false);
		Trace("game end: clearing the Remix scene");
		m_bridge->scene_clear();
	}
	if (m_texture) {
		Trace("game end: freeing the picture texture");
		glDeleteTextures(1, (GLuint *)&m_texture);
	}
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

	/* Remix may still be running from the test in Preferences, in its own
	 * window. Unloading and loading the runtime again in one process is not
	 * known to be safe, so the game stays in OpenGL this time. */
	if (status == BRB_STATUS_RUNNING && m_bridge->get_mode() != mode) {
		m_status = "running in another mode";
		SetNotice("RTX Remix is already running for the Preferences test; restart Blender to use it in the game. "
		          "The game runs in OpenGL.");
		return false;
	}
	status = m_bridge->startup_ex(mode, width, height, parent, x, y);
	if (status != BRB_STATUS_RUNNING) {
		m_status = m_bridge->status_text();
		SetNotice(std::string(m_bridge->status_text()) + " The game runs in OpenGL.");
		return false;
	}
	m_bridge->reset_stats();
	ApplyExposure(settings);
	m_running = true;
	m_mode = mode;
	m_scene = scene;
	m_settings = settings;
	m_width = width;
	m_height = height;
	m_status = (mode == BRB_MODE_COPY) ? "running (copy mode)" : "running (window mode)";
	printf("BGE-Remix: RTX Remix renders scene \"%s\" (%s, %dx%d).\n", scene->GetName().ReadPtr(),
	       mode == BRB_MODE_COPY ? "copy mode" : "window mode", width, height);
	fflush(stdout);
	return true;
}

void KX_RemixRenderer::ApplyExposure(const Settings &settings)
{
	/* Stops on top of Remix's automatic exposure. Remix's default tone mapper
	 * (rtx.tonemappingMode Local) ignores rtx.tonemap.exposureBias and scales
	 * by rtx.localtonemap.exposure instead (P2 RTX run 5: four exposures,
	 * one picture), so both are set: the local factor from Remix's default,
	 * the global bias for scenes that switch to the global tone mapper. */
	char value[32];
	snprintf(value, sizeof(value), "%g", LOCAL_TONEMAP_EXPOSURE * std::pow(2.0, (double)settings.exposure));
	m_bridge->set_config("rtx.localtonemap.exposure", value);
	snprintf(value, sizeof(value), "%g", settings.exposure);
	m_bridge->set_config("rtx.tonemap.exposureBias", value);
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

uint64_t KX_RemixRenderer::EnsureMaterial(KX_Scene *scene, RAS_IPolyMaterial *pm)
{
	const std::string name = pm ? pm->GetMaterialName().ReadPtr() : "__default__";
	std::map<const void *, MaterialEntry>::iterator it = m_materials.find(pm);
	if (it != m_materials.end()) {
		if (it->second.name == name) {
			return it->second.hash;
		}
		/* a different material at the address of a freed one */
		m_bridge->material_destroy(it->second.hash);
		m_hashes.erase(it->second.hash);
		m_materials.erase(it);
	}

	brb_material_desc d;
	memset(&d, 0, sizeof(d));
	d.hash = UniqueHash("material", name, pm);
	/* Blender's default material */
	d.albedo[0] = d.albedo[1] = d.albedo[2] = 0.8f * 0.8f;
	d.opacity = 1.0f;
	d.roughness = 0.5f;

	Material *ma = pm ? pm->GetBlenderMaterial() : NULL;
	if (ma) {
		d.albedo[0] = ma->r * ma->ref;
		d.albedo[1] = ma->g * ma->ref;
		d.albedo[2] = ma->b * ma->ref;
		d.opacity = ma->alpha;
		/* Blinn-Phong hardness to a GGX roughness: alpha = sqrt(2 / (n + 2)),
		 * roughness = sqrt(alpha) */
		d.roughness = (ma->spec < 0.01f) ? 1.0f : (float)std::pow(2.0 / ((double)ma->har + 2.0), 0.25);
		d.metallic = 0.0f;
		if (ma->emit > 0.0f) {
			d.emissive[0] = ma->r;
			d.emissive[1] = ma->g;
			d.emissive[2] = ma->b;
			d.emissive_intensity = ma->emit;
		}
		if (ma->mode & MA_SHLESS) {
			/* shadeless: shows its own colour, whatever the light */
			d.albedo[0] = d.albedo[1] = d.albedo[2] = 0.0f;
			d.emissive[0] = ma->r;
			d.emissive[1] = ma->g;
			d.emissive[2] = ma->b;
			d.emissive_intensity = 1.0f;
		}
		const int blend = ma->game.alpha_blend;
		d.alpha_blend = (blend == GEMAT_ALPHA || blend == GEMAT_ALPHA_SORT || blend == GEMAT_ADD) ? 1 : 0;
		if (!d.alpha_blend) {
			d.opacity = 1.0f;
		}
	}
	m_bridge->material_create(&d);
	MaterialEntry e;
	e.hash = d.hash;
	e.scene = scene;
	e.name = name;
	m_materials[pm] = e;
	return d.hash;
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

	std::vector<std::vector<brb_vertex> > verts;
	std::vector<std::vector<uint32_t> > indices;
	std::vector<uint64_t> materials;
	bool double_sided = false;
	bool blend = false;

	for (std::list<RAS_MeshMaterial>::iterator mit = mesh->GetFirstMaterial(); mit != mesh->GetLastMaterial(); ++mit) {
		RAS_MeshSlot *slot = mit->m_baseslot;
		RAS_IPolyMaterial *pm = mit->m_bucket ? mit->m_bucket->GetPolyMaterial() : NULL;
		if (!slot) {
			continue;
		}
		std::vector<brb_vertex> v;
		std::vector<uint32_t> idx;
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
			double_sided = true;
		}
		if (ma && (ma->game.alpha_blend == GEMAT_ALPHA || ma->game.alpha_blend == GEMAT_ALPHA_SORT ||
		           ma->game.alpha_blend == GEMAT_ADD))
		{
			blend = true;
		}
		materials.push_back(EnsureMaterial(scene, pm));
		verts.push_back(v);
		indices.push_back(idx);
	}

	MeshEntry e;
	e.hash = UniqueHash("mesh", name, mesh);
	e.scene = scene;
	e.polygons = polygons;
	e.name = name;
	e.double_sided = double_sided;
	e.blend = blend;

	std::vector<brb_surface_desc> surfaces(verts.size());
	for (size_t s = 0; s < verts.size(); s++) {
		surfaces[s].vertices = verts[s].data();
		surfaces[s].vertex_count = (uint32_t)verts[s].size();
		surfaces[s].indices = indices[s].data();
		surfaces[s].index_count = (uint32_t)indices[s].size();
		surfaces[s].material = materials[s];
	}
	if (surfaces.empty() ||
	    m_bridge->mesh_create(e.hash, surfaces.data(), (uint32_t)surfaces.size()) != 0)
	{
		/* nothing to draw; remembered so it is not tried every frame */
		e.hash = 0;
	}
	r_entry = &(m_meshes[mesh] = e);
	return e.hash != 0;
}

void KX_RemixRenderer::SendLight(KX_Scene *scene, const void *key, const brb_light_desc &desc)
{
	LightEntry &e = m_lights[key];
	const unsigned char *bytes = (const unsigned char *)&desc;
	if (e.hash != desc.hash || e.desc.size() != sizeof(desc) || memcmp(e.desc.data(), bytes, sizeof(desc)) != 0) {
		/* new or changed (moved, recoloured...): creating with the same hash
		 * replaces the light */
		m_bridge->light_create(&desc);
		e.hash = desc.hash;
		e.scene = scene;
		e.desc.assign(bytes, bytes + sizeof(desc));
	}
	m_bridge->frame_light(desc.hash);
}

void KX_RemixRenderer::UpdateLights(KX_Scene *scene, float light_scale)
{
	float dome[3] = {0.0f, 0.0f, 0.0f};

	CListValue *lights = scene->GetLightList();
	for (int i = 0; i < lights->GetCount(); i++) {
		KX_LightObject *light = (KX_LightObject *)lights->GetValue(i);
		RAS_ILightObject *data = light->GetLightData();
		Object *ob = light->GetBlenderObject();
		Lamp *la = (ob && ob->type == OB_LAMP) ? (Lamp *)ob->data : NULL;
		if (!data || !light->GetVisible() || (la && (la->mode & LA_NEG))) {
			continue;
		}
		const float energy = data->m_energy * light_scale;
		if (energy <= 0.0f) {
			continue;
		}
		const float color[3] = {std::fabs(data->m_color[0]), std::fabs(data->m_color[1]), std::fabs(data->m_color[2])};
		const int type = la ? la->type : (data->m_type == RAS_ILightObject::LIGHT_SUN ? LA_SUN :
		                                  data->m_type == RAS_ILightObject::LIGHT_SPOT ? LA_SPOT : LA_LOCAL);
		if (type == LA_HEMI) {
			/* Hemi: light from the whole sky, added to the dome */
			for (int k = 0; k < 3; k++) {
				dome[k] += color[k] * energy;
			}
			continue;
		}

		const MT_Matrix3x3 &ori = light->NodeGetWorldOrientation();
		const MT_Point3 &pos = light->NodeGetWorldPosition();
		brb_light_desc d;
		memset(&d, 0, sizeof(d));
		d.hash = UniqueHash("light", light->GetName().ReadPtr(), light);
		d.position[0] = (float)pos[0];
		d.position[1] = (float)pos[1];
		d.position[2] = (float)pos[2];
		/* lamps shine along their local -Z */
		column(ori, 2, d.direction);
		d.direction[0] = -d.direction[0];
		d.direction[1] = -d.direction[1];
		d.direction[2] = -d.direction[2];
		normalize3(d.direction);
		const float dist = std::max(data->m_distance, 0.001f);
		float scale = 1.0f;

		if (type == LA_SUN) {
			/* Remix's distant light "radiance" is the irradiance it gives (as
			 * Remix converts D3D9 sun lights): pi * E. Dividing by the sun's
			 * solid angle burnt the sunlit floor out (RTX 3080, P2 room). */
			d.type = BRB_LIGHT_DISTANT;
			d.angular_diameter_deg = SUN_ANGULAR_DIAMETER_DEG;
			scale = (float)(PI * energy);
		}
		else if (type == LA_AREA && la) {
			d.type = BRB_LIGHT_RECT;
			column(ori, 0, d.x_axis);
			column(ori, 1, d.y_axis);
			normalize3(d.x_axis);
			normalize3(d.y_axis);
			const MT_Vector3 &s = light->NodeGetWorldScaling();
			d.size[0] = std::max(la->area_size * (float)std::fabs(s[0]), 0.001f);
			d.size[1] = std::max((la->area_shape == LA_AREA_SQUARE ? la->area_size : la->area_sizey) *
			                     (float)std::fabs(s[1]), 0.001f);
			/* intensity pi * E * D over the lamp's area */
			scale = (float)(PI * energy * dist / (d.size[0] * d.size[1]));
		}
		else {
			/* point and spot: a small sphere; Blender Internal's falloff
			 * E * D / (D + r^2) is E * D / r^2 far away, so the intensity is
			 * pi * E * D, spread over the sphere's disc pi * R^2 */
			d.type = BRB_LIGHT_SPHERE;
			d.radius = std::max(la ? la->area_size : 0.1f, MIN_LIGHT_RADIUS);
			scale = energy * dist / (d.radius * d.radius);
			if (type == LA_SPOT) {
				d.shaped = 1;
				const double half = data->m_spotsize * 0.5;
				d.cone_angle_deg = (float)(half * 180.0 / PI);
				/* Remix fades the cone edge over coneSoftness in cosine units
				 * (smoothstep(cos(cone), cos(cone) + softness, cos(angle))), as
				 * Blender does over (1 - cos(cone)) * blend; passing the blend
				 * itself dimmed and blurred the pool (P2 RTX run 5) */
				const float blend = std::min(std::max(data->m_spotblend, 0.0f), 1.0f);
				d.cone_softness = (float)((1.0 - std::cos(half)) * blend);
			}
		}
		d.radiance[0] = color[0] * scale;
		d.radiance[1] = color[1] * scale;
		d.radiance[2] = color[2] * scale;
		SendLight(scene, light, d);
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
	if (dome[0] > 0.0f || dome[1] > 0.0f || dome[2] > 0.0f) {
		brb_light_desc d;
		memset(&d, 0, sizeof(d));
		d.hash = UniqueHash("dome", scene->GetName().ReadPtr(), world);
		d.type = BRB_LIGHT_DOME;
		d.radiance[0] = dome[0];
		d.radiance[1] = dome[1];
		d.radiance[2] = dome[2];
		SendLight(scene, world, d);
	}
}

void KX_RemixRenderer::RenderScene(KX_Scene *scene, KX_Camera *cam, const MT_Matrix4x4 &view,
                                   const MT_Matrix4x4 &proj, const RAS_Rect &viewport)
{
	if (!m_running || !m_bridge) {
		return;
	}
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

	float v[16], p[16];
	view.getValue(v);
	proj.getValue(p);
	m_bridge->frame_camera(v, p);

	int draws = 0;
	CListValue *objects = scene->GetObjectList();
	for (int i = 0; i < objects->GetCount(); i++) {
		KX_GameObject *obj = (KX_GameObject *)objects->GetValue(i);
		if (!obj->GetVisible() || obj->GetMeshCount() == 0 || obj->GetGameObjectType() == SCA_IObject::OBJ_TEXT) {
			continue;
		}
		float model[16];
		bool have_matrix = false;
		for (int m = 0; m < obj->GetMeshCount(); m++) {
			MeshEntry *e = NULL;
			if (!EnsureMesh(scene, obj->GetMesh(m), e) || !e) {
				continue;
			}
			if (!have_matrix) {
				object_matrix(obj, model);
				have_matrix = true;
			}
			m_bridge->frame_draw(e->hash, model, (e->double_sided ? BRB_DRAW_DOUBLE_SIDED : 0) |
			                                     (e->blend ? BRB_DRAW_BLEND : 0));
			draws++;
		}
	}

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

	MaybeScreenshot();

	if (m_mode == BRB_MODE_COPY) {
		const double t2 = now_seconds();
		DrawPicture(width, height);
		m_upload_ms.push_back((float)((now_seconds() - t2) * 1000.0));
	}
	(void)cam;
	(void)draws;
}

/* ------------------------------------------------------------------------- */
/* Picture */

void KX_RemixRenderer::DrawPicture(int width, int height)
{
	int w = 0, h = 0, pitch = 0;
	const void *pixels = m_bridge->frame_pixels(&w, &h, &pitch);
	if (!pixels || w <= 0 || h <= 0) {
		return;
	}
	/* the shader program is not on the attribute stack */
	GLint program = 0;
	if (GLEW_VERSION_2_0) {
		glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	}
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);

	if (!m_texture) {
		glGenTextures(1, (GLuint *)&m_texture);
	}
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glBindTexture(GL_TEXTURE_2D, m_texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
	if (w != m_tex_w || h != m_tex_h) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
		m_tex_w = w;
		m_tex_h = h;
	}
	else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
	}

	glMatrixMode(GL_TEXTURE);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	if (GLEW_VERSION_2_0) {
		glUseProgram(0);
	}
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LIGHTING);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	glDisable(GL_STENCIL_TEST);
	glDepthMask(GL_FALSE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	/* the picture's rows go from the top; OpenGL's from the bottom */
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
	glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);
	glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
	glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
	glEnd();

	glBindTexture(GL_TEXTURE_2D, 0);
	glMatrixMode(GL_TEXTURE);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();

	glPopClientAttrib();
	glPopAttrib();
	if (GLEW_VERSION_2_0) {
		glUseProgram((GLuint)program);
	}
	(void)width;
	(void)height;
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
	fprintf(f, "  \"mode\": \"%s\",\n", m_mode == BRB_MODE_COPY ? "copy" : m_mode == BRB_MODE_CHILD ? "window" : "none");
	fprintf(f, "  \"status\": \"%s\",\n", status.c_str());
	fprintf(f, "  \"notice\": \"%s\",\n", notice.c_str());
	fprintf(f, "  \"frames\": %d,\n", m_frames);
	fprintf(f, "  \"width\": %d,\n  \"height\": %d,\n", m_width, m_height);
	fprintf(f, "  \"draws_last_frame\": %d,\n  \"lights_last_frame\": %d,\n", m_last_draws, m_last_lights);
	fprintf(f, "  \"meshes_created\": %d,\n  \"materials_created\": %d,\n  \"lights_created\": %d,\n",
	        st.meshes_created, st.materials_created, st.lights_created);
	fprintf(f, "  \"errors\": %d,\n  \"last_error\": %d,\n", st.errors, st.last_error);
	fprintf(f, "  \"handover_ms_median\": %.4f,\n  \"handover_ms_p95\": %.4f,\n",
	        median(m_handover_ms), percentile95(m_handover_ms));
	fprintf(f, "  \"present_ms_median\": %.4f,\n  \"copy_ms_median\": %.4f,\n  \"upload_ms_median\": %.4f\n",
	        median(m_present_ms), median(m_copy_ms), median(m_upload_ms));
	fprintf(f, "}\n");
	fclose(f);
}

#endif  /* WITH_REMIX */
