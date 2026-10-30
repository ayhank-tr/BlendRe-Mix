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

/** \file remix_view.cc
 *  \ingroup remix
 *
 * The "RTX Remix" render engine: the 3D view and F12 (BRMX_view.h).
 *
 * One Sync per 3D view (and one per F12 render) turns Blender's data into
 * Remix objects: a mesh per mesh datablock that is drawn as it is (shared by
 * every object and duplicate using it) or per object when modifiers, shape
 * keys, edit mode or object materials make it the object's own; the P3
 * material translator; the lamps and the world as the game engine sends them
 * (remix_light.cc). An update sends only what Blender tagged as changed:
 * moving an object sends nothing (positions go with every frame).
 */

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
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "GPU_glew.h"

extern "C" {
#include "MEM_guardedalloc.h"

#include "DNA_ID.h"
#include "DNA_camera_types.h"
#include "DNA_lamp_types.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_object_types.h"
#include "DNA_particle_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_view3d_types.h"
#include "DNA_world_types.h"

#include "BLI_listbase.h"
#include "BLI_math.h"
/* BLI_threads.h would need pthread's headers */
int BLI_thread_is_main(void);
#include "BLI_utildefines.h"

#include "BKE_anim.h"
#include "BKE_camera.h"
#include "BKE_customdata.h"
#include "BKE_DerivedMesh.h"
#include "BKE_global.h"
#include "BKE_idprop.h"
#include "BKE_lamp_physical.h"
#include "BKE_main.h"
#include "BKE_material.h"
#include "BKE_mesh.h"
#include "BKE_scene.h"
#include "BLI_threads.h"

#include "RE_engine.h"
#include "RE_pipeline.h"
}

#include "BRMX_bake.h"
#include "BRMX_light.h"
#include "BRMX_loader.h"
#include "BRMX_material.h"
#include "BRMX_present.h"
#include "BRMX_session.h"
#include "BRMX_view.h"

namespace {

/* ------------------------------------------------------------------------- */
/* Helpers */

/* the view keeps drawing this long after the last change, while Remix
 * refines its picture (denoiser, DLSS history) */
const double REFINE_SECONDS = 1.0;
/* a 3D view that has not drawn for this long gives Remix up to another one */
const double VIEW_IDLE_SECONDS = 2.0;

double now_seconds()
{
	static LARGE_INTEGER freq = {};
	if (freq.QuadPart == 0) {
		QueryPerformanceFrequency(&freq);
	}
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart / (double)freq.QuadPart;
}

bool env_is(const char *name, const char *value)
{
	char buf[32];
	const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
	return n > 0 && n < sizeof(buf) && _stricmp(buf, value) == 0;
}

uint64_t fnv1a(const std::string &s, uint64_t h = 1469598103934665603ull)
{
	for (size_t i = 0; i < s.size(); i++) {
		h ^= (unsigned char)s[i];
		h *= 1099511628211ull;
	}
	return h;
}

uint64_t hash_bytes(const void *data, size_t n, uint64_t h)
{
	const unsigned char *p = (const unsigned char *)data;
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= 1099511628211ull;
	}
	return h;
}

int idprop_int(IDProperty *group, const char *name, int def)
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

float idprop_float(IDProperty *group, const char *name, float def)
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

/* The scene's light scale and exposure, as the game reads them (Rasterizer panel). */
void scene_lighting(const Scene *scene, float &r_light_scale, float &r_exposure)
{
	IDProperty *props = scene->id.properties;
	r_light_scale = idprop_float(props, "bge_remix_light_scale", brmx::DEFAULT_LIGHT_SCALE);
	r_exposure = idprop_float(props, "bge_remix_exposure", brmx::DEFAULT_EXPOSURE);
	if (!(r_light_scale >= 0.0f) || r_light_scale > 1e6f) {
		r_light_scale = brmx::DEFAULT_LIGHT_SCALE;
	}
	if (!(r_exposure >= -10.0f && r_exposure <= 10.0f)) {
		r_exposure = brmx::DEFAULT_EXPOSURE;
	}
}

/* ------------------------------------------------------------------------- */
/* Counters for the tests (BRMX_view_stats_json) */

struct Stats {
	int view_updates = 0, view_draws = 0, view_frames = 0, view_fallbacks = 0, view_full_syncs = 0;
	int meshes_sent = 0, materials_sent = 0, lights_sent = 0, meshes_removed = 0;
	/* the last update or full sync */
	int last_meshes = 0, last_materials = 0, last_lights = 0, last_draws = 0;
	int highlighted = 0;       /* objects outlined in the last frame */
	int gl_differences = 0;    /* BGE_REMIX_GL_CHECK */
	int resized_frames = 0;    /* frames Remix changed size on */
	int main_reloads = 0;      /* undo/redo/reload steps the view started over for */
	int bakes = 0;             /* P8.6: procedural texture bakes */
	int width = 0, height = 0; /* the last view frame's size */
	int render_syncs = 0, render_frames = 0, render_reads = 0, render_width = 0, render_height = 0;
	double render_seconds = 0.0; /* the last F12, from sync to result */
	double first_frame_ms = 0.0; /* the last full sync's time */
	std::string last_note;
};
Stats g_stats;

/* ------------------------------------------------------------------------- */
/* Sync: Blender's data into Remix objects */

struct MeshRec {
	bool known;
	bool geometry;        /* Remix has it (else nothing to draw) */
	uint64_t hash;
	unsigned gen;         /* the update that last used it */
	bool double_sided, blend, particle, edit;
	uint64_t materials;   /* the materials it was made with */
	/* P8.4: its parts, (material slot, face image or NULL), for the
	 * materials it keeps alive */
	std::vector<std::pair<int, Image *>> parts;
	uint64_t bakes;       /* P8.6: bake_signature() of its materials with procedural textures */
	MeshRec() : known(false), geometry(false), hash(0), gen(0), double_sided(false), blend(false), particle(false),
	            edit(false), materials(0), bakes(0) {}
};

struct MaterialRec {
	bool known;
	uint64_t hash;
	unsigned gen;
	unsigned sent_gen;    /* the update that last sent it */
	std::string name;
	std::shared_ptr<brmx::MaterialSpec> spec;
	bool blend, particle, waiting;
	const brmx::BakeResult *bake; /* P8.6: the bake its spec was made with */
	uint64_t bake_signature;
	MaterialRec() : known(false), hash(0), gen(0), sent_gen(0), blend(false), particle(false), waiting(false),
	                bake(NULL), bake_signature(0) {}
};

/* The key of an object's mesh record: the object's own mesh, or the mesh
 * shared by all that draw it as it is */
const void *mesh_key(Object *ob)
{
	Mesh *me = (Mesh *)ob->data;
	struct BMEditMesh *em = (ob->mode & OB_MODE_EDIT) ? me->edit_btmesh : NULL;
	bool object_materials = false;
	for (int i = 0; i < ob->totcol && ob->matbits; i++) {
		object_materials = object_materials || ob->matbits[i];
	}
	const bool own = em || ob->modifiers.first || me->key || ob->partype == PARSKEL || object_materials;
	return own ? (const void *)ob : (const void *)me;
}

/* ------------------------------------------------------------------------- */
/* P8.6: procedural textures baked per (material, mesh), shared by the 3D
 * views and F12 (which renders on a thread without OpenGL and uses what was
 * baked before it started, BRMX_render_prepare). Under the lock. */

struct BakeStoreEntry {
	brmx::BakeResult result;
	uint64_t geometry; /* the mesh it was baked on */
	bool ok;
	bool noted;
	BakeStoreEntry() : geometry(0), ok(false), noted(false) {}
};
std::map<std::pair<const Material *, const void *>, BakeStoreEntry> g_bakes;

/* the mesh's shape and UV map */
uint64_t geometry_signature(DerivedMesh *dm)
{
	uint64_t h = 1469598103934665603ull;
	const MVert *mv = dm->getVertArray(dm);
	const int nv = dm->getNumVerts(dm);
	for (int i = 0; mv && i < nv; i++) {
		h = hash_bytes(mv[i].co, sizeof(mv[i].co), h);
	}
	const MLoopUV *uv = (const MLoopUV *)dm->getLoopDataArray(dm, CD_MLOOPUV);
	const int nl = dm->getNumLoops(dm);
	for (int i = 0; uv && i < nl; i++) {
		h = hash_bytes(uv[i].uv, sizeof(uv[i].uv), h);
	}
	return hash_bytes(&nl, sizeof(nl), h);
}

bool can_bake()
{
	return BLI_thread_is_main() && wglGetCurrentContext() != NULL;
}

/* The bake of material slot `slot` (1-based) on this mesh: baked now when
 * missing or out of date and this thread can (dm given), else the last one. */
const brmx::BakeResult *bake_for(Scene *scene, Object *ob, const void *key, Material *ma, int slot, DerivedMesh *dm)
{
	BakeStoreEntry &e = g_bakes[std::make_pair((const Material *)ma, key)];
	const uint64_t sig = brmx::bake_signature(ma);
	const uint64_t geo = dm ? geometry_signature(dm) : e.geometry;
	if (e.ok && e.result.signature == sig && e.geometry == geo) {
		return &e.result;
	}
	if (dm && can_bake()) {
		const std::string name = std::string(ma->id.name + 2) + "_" + (ob->id.name + 2);
		e.ok = brmx::bake_material(scene, ma, dm, slot, ob->obmat, name, e.result);
		e.geometry = geo;
		if (!e.ok && !e.noted) {
			printf("BGE-Remix: material \"%s\" on \"%s\": %s\n", ma->id.name + 2, ob->id.name + 2,
			       e.result.error.empty() ? "its procedural textures could not be baked" : e.result.error.c_str());
			e.noted = true;
		}
		g_stats.bakes++;
	}
	return e.ok ? &e.result : NULL;
}

struct LightRec {
	uint64_t hash;
	unsigned gen;
	std::vector<unsigned char> desc;
	LightRec() : hash(0), gen(0) {}
};

struct DrawItem {
	uint64_t mesh;
	float model[16];
	int flags;
	uint32_t pick;
};

class Sync {
public:
	explicit Sync(const char *prefix) : m_prefix(prefix), m_gen(0), m_next_pick(0), m_waiting(0) {}

	/* Remix's objects were removed by someone else: forget them. */
	void forget()
	{
		m_meshes.clear();
		m_materials.clear();
		m_lights.clear();
		m_hashes.clear();
		m_draws.clear();
		m_frame_lights.clear();
		m_pick_ids.clear();
		m_pick_objects.clear();
		m_waiting = 0;
	}

	/* Remove our objects from Remix and forget them. */
	void clear(const BRMX_Bridge *b)
	{
		for (auto &kv : m_meshes) {
			if (kv.second.geometry) {
				b->mesh_destroy(kv.second.hash);
			}
		}
		for (auto &kv : m_materials) {
			b->material_destroy(kv.second.hash);
		}
		for (auto &kv : m_lights) {
			b->light_destroy(kv.second.hash);
		}
		forget();
	}

	/* Read the scene. full: everything is sent again (else what Blender
	 * tagged); for_render: F12's visibility. */
	void update(const BRMX_Bridge *b, Main *bmain, Scene *scene, unsigned int lay, bool full, bool for_render)
	{
		m_bridge = b;
		m_scene = scene;
		m_full = full;
		m_for_render = for_render;
		m_gen++;
		m_draws.clear();
		m_pick_objects.clear();
		m_dome[0] = m_dome[1] = m_dome[2] = 0.0f;
		m_frame_lights.clear();
		/* the images that exist now (a mesh part's face image may be gone) */
		m_live_images.clear();
		Main *images_main = bmain ? bmain : G.main;
		if (images_main) {
			for (ID *id = (ID *)images_main->image.first; id; id = (ID *)id->next) {
				m_live_images.insert(id);
			}
		}
		scene_lighting(scene, m_light_scale, m_exposure);
		const int meshes0 = g_stats.meshes_sent, materials0 = g_stats.materials_sent, lights0 = g_stats.lights_sent;

		EvaluationContext *eval_ctx = bmain ? bmain->eval_ctx : (G.main ? G.main->eval_ctx : NULL);
		Scene *sce_iter;
		Base *base;
		for (SETLOOPER(scene, sce_iter, base)) {
			Object *ob = base->object;
			if (!(base->lay & lay) || hidden(ob)) {
				continue;
			}
			const uint32_t pick = pick_id(ob);
			if ((ob->transflag & OB_DUPLI) && eval_ctx) {
				ListBase *lb = object_duplilist(eval_ctx, scene, ob);
				if (lb) {
					for (DupliObject *dob = (DupliObject *)lb->first; dob; dob = dob->next) {
						Object *o = dob->ob;
						if (dob->no_draw || !o || hidden(o)) {
							continue;
						}
						if (o->type == OB_MESH) {
							add_mesh(o, dob->mat, pick);
						}
						/* BGE-Remix P8.9: lamps in group instances light too */
						else if (o->type == OB_LAMP) {
							char name[256];
							snprintf(name, sizeof(name), "%s/%s/%d", ob->id.name + 2, o->id.name + 2,
							         dob->persistent_id[0]);
							add_lamp(o, dob->mat, name);
						}
					}
					free_object_duplilist(lb);
				}
			}
			if (hide_original(ob)) {
				continue;
			}
			if (ob->type == OB_MESH) {
				add_mesh(ob, ob->obmat, pick);
			}
			else if (ob->type == OB_LAMP) {
				add_lamp(ob, ob->obmat, NULL);
			}
		}
		/* the world's horizon colour as the sky, plus the hemi lamps */
		if (scene->world) {
			m_dome[0] += scene->world->horr;
			m_dome[1] += scene->world->horg;
			m_dome[2] += scene->world->horb;
		}
		brb_light_desc d;
		memset(&d, 0, sizeof(d));
		d.hash = unique_hash("dome", scene->id.name + 2, scene);
		if (brmx::dome_light(m_dome, d)) {
			send_light(scene, d);
		}

		/* what is no longer used */
		for (auto it = m_meshes.begin(); it != m_meshes.end();) {
			if (it->second.gen != m_gen) {
				if (it->second.geometry) {
					b->mesh_destroy(it->second.hash);
					g_stats.meshes_removed++;
				}
				m_hashes.erase(it->second.hash);
				it = m_meshes.erase(it);
			}
			else {
				++it;
			}
		}
		for (auto it = m_lights.begin(); it != m_lights.end();) {
			if (it->second.gen != m_gen) {
				b->light_destroy(it->second.hash);
				m_hashes.erase(it->second.hash);
				it = m_lights.erase(it);
			}
			else {
				++it;
			}
		}
		for (auto it = m_materials.begin(); it != m_materials.end();) {
			if (it->second.gen != m_gen) {
				b->material_destroy(it->second.hash);
				m_hashes.erase(it->second.hash);
				if (it->second.waiting) {
					m_waiting--;
				}
				it = m_materials.erase(it);
			}
			else {
				++it;
			}
		}
		m_signature = signature(scene, lay);
		g_stats.last_meshes = g_stats.meshes_sent - meshes0;
		g_stats.last_materials = g_stats.materials_sent - materials0;
		g_stats.last_lights = g_stats.lights_sent - lights0;
		g_stats.last_draws = (int)m_draws.size();
	}

	/* Textures that finished encoding: their materials go again. Returns
	 * the number of materials still waiting. */
	int update_textures()
	{
		if (m_waiting <= 0) {
			return 0;
		}
		m_cache.poll();
		for (auto &kv : m_materials) {
			MaterialRec &r = kv.second;
			if (!r.waiting) {
				continue;
			}
			bool still = false;
			for (int i = 0; i < brmx::TEX_COUNT && !still; i++) {
				std::wstring path;
				if (r.spec->textures[i].wanted() &&
				    m_cache.request(r.spec->textures[i], path) == brmx::TextureCache::WAITING)
				{
					still = true;
				}
			}
			if (!still) {
				send_material(r);
			}
		}
		return m_waiting;
	}

	int waiting() const { return m_waiting; }

	/* One frame: the camera, every draw and light; outlines around these
	 * objects. */
	void send_frame(const BRMX_Bridge *b, const float view[16], const float proj[16],
	                const std::vector<uint32_t> &highlight)
	{
		b->frame_begin();
		b->frame_camera(view, proj);
		for (const DrawItem &it : m_draws) {
			brb_draw_desc d;
			memset(&d, 0, sizeof(d));
			d.mesh = it.mesh;
			memcpy(d.model, it.model, sizeof(d.model));
			d.flags = it.flags;
			d.pick_id = it.pick;
			b->frame_draw_ex(&d);
		}
		for (uint64_t h : m_frame_lights) {
			b->frame_light(h);
		}
		if (!highlight.empty()) {
			/* Blender's selection colour (Remix has one outline colour) */
			b->highlight(highlight.data(), (int)highlight.size(), 1.0f, 0.55f, 0.1f);
		}
		b->frame_end();
	}

	/* The picking IDs of the selected objects among those drawn (from the
	 * scene's bases: an object deleted since the last update is not touched). */
	void selected(Scene *scene, unsigned int lay, std::vector<uint32_t> &r_ids) const
	{
		r_ids.clear();
		Scene *sce_iter;
		Base *base;
		for (SETLOOPER(scene, sce_iter, base)) {
			if (!(base->flag & SELECT) || !(base->lay & lay)) {
				continue;
			}
			std::map<const Object *, uint32_t>::const_iterator it = m_pick_ids.find(base->object);
			if (it != m_pick_ids.end() && m_pick_objects.count(it->second)) {
				r_ids.push_back(it->second);
			}
		}
	}

	/* What the scene is made of (its objects and their visibility): an object
	 * added, deleted or hidden, or an undo, changes it, also when Blender
	 * calls no update for it. */
	static uint64_t signature(Scene *scene, unsigned int lay)
	{
		uint64_t h = 1469598103934665603ull;
		Scene *sce_iter;
		Base *base;
		for (SETLOOPER(scene, sce_iter, base)) {
			const void *p = base->object;
			const unsigned int bits[2] = {base->lay & lay, base->object ? (unsigned int)base->object->restrictflag : 0u};
			h = hash_bytes(&p, sizeof(p), h);
			h = hash_bytes(bits, sizeof(bits), h);
		}
		return h;
	}

	uint64_t last_signature() const { return m_signature; }

	float exposure() const { return m_exposure; }
	size_t draws() const { return m_draws.size(); }

private:
	bool hidden(const Object *ob) const
	{
		return (ob->restrictflag & (m_for_render ? OB_RESTRICT_RENDER : OB_RESTRICT_VIEW)) != 0;
	}

	/* as Cycles: duplicating vertices or faces hides the object itself; a
	 * particle emitter shows only when one of its systems says so */
	static bool hide_original(const Object *ob)
	{
		if (ob->transflag & (OB_DUPLIVERTS | OB_DUPLIFACES)) {
			return true;
		}
		if ((ob->transflag & OB_DUPLIPARTS) && ob->particlesystem.first) {
			for (ParticleSystem *psys = (ParticleSystem *)ob->particlesystem.first; psys; psys = psys->next) {
				if (psys->part && (psys->part->draw & PART_DRAW_EMITTER)) {
					return false;
				}
			}
			return true;
		}
		return false;
	}

	uint32_t pick_id(Object *ob)
	{
		std::map<const Object *, uint32_t>::iterator it = m_pick_ids.find(ob);
		uint32_t id;
		if (it != m_pick_ids.end()) {
			id = it->second;
		}
		else {
			id = ++m_next_pick;
			m_pick_ids[ob] = id;
		}
		m_pick_objects[id] = ob;
		return id;
	}

	uint64_t unique_hash(const std::string &kind, const std::string &name, const void *key)
	{
		/* stable across runs (from names), unique within the scene */
		const std::string base = m_prefix + ":" + kind + ":" + name;
		uint64_t h = fnv1a(base);
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
			h = fnv1a(base + buf);
		}
	}

	/* face_image: P8.4, the faces' image for a material with Face Textures
	 * (one Remix material per image, as the game makes) */
	/* P8.6: a material with procedural textures is made per mesh (mesh_key,
	 * with the object, its slot and, when this update built the mesh, its
	 * derived mesh to bake on) */
	MaterialRec &ensure_material(Material *ma, Image *face_image = NULL, const void *mesh_key = NULL,
	                             Object *ob = NULL, int slot = 0, DerivedMesh *dm = NULL)
	{
		if (!brmx::uses_face_image(ma)) {
			face_image = NULL;
		}
		const bool baked = mesh_key && ob && brmx::bake_slots(ma) != 0;
		MaterialRec &r = m_materials[std::make_pair(std::make_pair((const Material *)ma, (const Image *)face_image),
		                                            baked ? mesh_key : (const void *)NULL)];
		std::string name = ma ? std::string(ma->id.name + 2) : std::string("__default__");
		if (face_image) {
			name += std::string(" / ") + (face_image->id.name + 2);
		}
		if (baked) {
			name += std::string(" @ ") + (ob->id.name + 2);
		}
		const brmx::BakeResult *bake = baked ? bake_for(m_scene, ob, mesh_key, ma, slot, dm) : NULL;
		const bool tagged = ((ma && (ma->id.tag & LIB_TAG_ID_RECALC_ALL)) ||
		                     (face_image && (face_image->id.tag & LIB_TAG_ID_RECALC_ALL))) &&
		                    r.sent_gen != m_gen;
		/* a new bake (the store keeps one result per material and mesh, so a
		 * changed one is told by its signature) */
		const bool rebaked = baked && bake && (bake != r.bake || bake->signature != r.bake_signature);
		if (!r.known || r.name != name || tagged || rebaked || (m_full && r.sent_gen != m_gen)) {
			if (!r.known || r.name != name) {
				r.hash = unique_hash("material", name,
				                     (face_image || baked) ? (const void *)&r : (const void *)ma);
			}
			r.known = true;
			r.name = name;
			r.spec.reset(new brmx::MaterialSpec());
			brmx::translate_material(ma, *r.spec);
			brmx::apply_face_image(ma, face_image, *r.spec);
			if (bake) {
				brmx::apply_bake(*bake, *r.spec);
			}
			r.bake = bake;
			r.bake_signature = bake ? bake->signature : 0;
			r.spec->desc.hash = r.hash;
			for (size_t i = 0; i < r.spec->notes.size(); i++) {
				printf("BGE-Remix: material \"%s\": %s\n", name.c_str(), r.spec->notes[i].c_str());
			}
			r.blend = r.spec->desc.alpha_blend != 0;
			r.particle = r.spec->particle;
			send_material(r);
			r.sent_gen = m_gen;
		}
		r.gen = m_gen;
		return r;
	}

	void send_material(MaterialRec &r)
	{
		/* the textures that are ready; the others follow (update_textures) */
		brb_material_desc d = r.spec->desc;
		std::wstring paths[brmx::TEX_COUNT];
		bool waiting = false;
		for (int i = 0; i < brmx::TEX_COUNT; i++) {
			const brmx::TextureRequest &req = r.spec->textures[i];
			if (!req.wanted()) {
				continue;
			}
			const brmx::TextureCache::State st = m_cache.request(req, paths[i]);
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
		g_stats.materials_sent++;
		if (waiting != r.waiting) {
			m_waiting += waiting ? 1 : -1;
		}
		r.waiting = waiting;
	}

	/* the materials an object's mesh is drawn with */
	static uint64_t material_signature(Object *ob)
	{
		uint64_t h = 1469598103934665603ull;
		const int n = std::max<int>(ob->totcol, 1);
		for (int i = 1; i <= n; i++) {
			Material *ma = give_current_material(ob, (short)i);
			h = hash_bytes(&ma, sizeof(ma), h);
		}
		return h;
	}

	/* P8.6: the procedural textures its materials bake */
	static uint64_t bake_signature(Object *ob)
	{
		uint64_t h = 1469598103934665603ull;
		const int n = std::max<int>(ob->totcol, 1);
		for (int i = 1; i <= n; i++) {
			Material *ma = give_current_material(ob, (short)i);
			if (brmx::bake_slots(ma)) {
				const uint64_t b = brmx::bake_signature(ma);
				h = hash_bytes(&b, sizeof(b), h);
			}
		}
		return h;
	}

	void add_mesh(Object *ob, float mat[4][4], uint32_t pick)
	{
		Mesh *me = (Mesh *)ob->data;
		if (!me) {
			return;
		}
		/* edit mode: the mesh being edited (BKE_editmesh_from_object, without bmesh's headers) */
		struct BMEditMesh *em = (ob->mode & OB_MODE_EDIT) ? me->edit_btmesh : NULL;
		bool object_materials = false;
		for (int i = 0; i < ob->totcol && ob->matbits; i++) {
			object_materials = object_materials || ob->matbits[i];
		}
		/* the object's own mesh, or the mesh shared by all that draw it as it is */
		const bool own = em || ob->modifiers.first || me->key || ob->partype == PARSKEL || object_materials;
		const void *key = mesh_key(ob);
		MeshRec &r = m_meshes[key];
		const uint64_t materials = material_signature(ob);
		const uint64_t bakes = bake_signature(ob);
		const bool dirty = !r.known || (m_full && r.gen != m_gen) || r.edit != (em != NULL) ||
		                   r.materials != materials || r.bakes != bakes ||
		                   (r.gen != m_gen && (((ob->id.tag & LIB_TAG_ID_RECALC_DATA) && own) ||
		                                       (me->id.tag & LIB_TAG_ID_RECALC_ALL)));
		if (dirty && r.gen != m_gen) {
			if (!r.known) {
				const std::string name = own ? std::string(ob->id.name + 2) : std::string(me->id.name + 2);
				r.hash = unique_hash("mesh", name, key);
			}
			r.known = true;
			r.edit = em != NULL;
			r.materials = materials;
			r.bakes = bakes;
			const bool had = r.geometry;
			r.geometry = build_mesh(ob, em, r, key);
			if (had && !r.geometry) {
				m_bridge->mesh_destroy(r.hash);
			}
		}
		else if (r.gen != m_gen) {
			/* the materials it uses stay alive (and go again when Blender tagged
			 * them); their blending decides the draw's */
			r.double_sided = r.blend = r.particle = false;
			for (size_t p = 0; p < r.parts.size(); p++) {
				Material *ma = give_current_material(ob, (short)(r.parts[p].first + 1));
				Image *ima = r.parts[p].second;
				if (ima && !m_live_images.count((const ID *)ima)) {
					ima = NULL;
				}
				const MaterialRec &mr = ensure_material(ma, ima, key, ob, r.parts[p].first + 1);
				r.double_sided = r.double_sided || !ma || !(ma->game.flag & GEMAT_BACKCULL);
				r.blend = r.blend || mr.blend;
				r.particle = r.particle || mr.particle;
			}
		}
		r.gen = m_gen;
		if (!r.geometry) {
			return;
		}
		DrawItem item;
		item.mesh = r.hash;
		memcpy(item.model, mat, sizeof(item.model));
		item.flags = (r.double_sided ? BRB_DRAW_DOUBLE_SIDED : 0) | (r.blend ? BRB_DRAW_BLEND : 0);
		item.pick = pick;
		m_draws.push_back(item);
	}

	/* The mesh as the view shows it (modifiers, edit mode), by material:
	 * as the game engine reads a deformed mesh (KX_RemixRenderer::CollectDerived). */
	bool build_mesh(Object *ob, struct BMEditMesh *em, MeshRec &r, const void *key)
	{
		CustomDataMask mask = CD_MASK_BAREMESH | CD_MASK_MLOOPUV | CD_MASK_MTEXPOLY;
		/* P8.6: a bake reads the original coordinates and the vertex colours */
		for (int i = 1; i <= std::max<int>(ob->totcol, 1); i++) {
			if (brmx::bake_slots(give_current_material(ob, (short)i))) {
				mask |= CD_MASK_ORCO | CD_MASK_MCOL | CD_MASK_MLOOPCOL;
				break;
			}
		}
		DerivedMesh *dm = NULL;
		if (em) {
			DerivedMesh *final = NULL;
			editbmesh_get_derived_cage_and_final(m_scene, ob, em, mask, &final);
			dm = final;
		}
		else {
			dm = mesh_get_derived_final(m_scene, ob, mask);
		}
		if (!dm) {
			return false;
		}
		/* loop triangles: every kind of derived mesh has them (a subdivided one
		 * from the GPU may have no tessellated faces) */
		const MLoopTri *tris = dm->getLoopTriArray(dm);
		const int ntri = dm->getNumLoopTri(dm);
		const MLoop *loops = dm->getLoopArray(dm);
		const MPoly *polys = dm->getPolyArray(dm);
		const MVert *mv = dm->getVertArray(dm);
		const int nv = dm->getNumVerts(dm);
		const MLoopUV *uv = (const MLoopUV *)dm->getLoopDataArray(dm, CD_MLOOPUV);
		/* P8.4: the faces' images, for the materials with Face Textures */
		const MTexPoly *mtpoly = (const MTexPoly *)dm->getPolyDataArray(dm, CD_MTEXPOLY);
		if (!tris || !loops || !polys || !mv || ntri <= 0) {
			return false;
		}
		const int slots = std::max<int>(ob->totcol, 1);
		std::vector<bool> slot_face_image(slots, false);
		for (int s = 0; s < slots; s++) {
			slot_face_image[s] = mtpoly && brmx::uses_face_image(give_current_material(ob, (short)(s + 1)));
		}
		/* one part per material slot, split by face image where it matters */
		typedef std::pair<int, Image *> PartKey;
		std::map<PartKey, std::vector<brb_vertex>> verts;
		std::map<PartKey, std::vector<uint32_t>> indices;
		int last_poly = -1;
		float pno[3] = {0.0f, 0.0f, 1.0f};
		for (int t = 0; t < ntri; t++) {
			const MLoopTri &lt = tris[t];
			const MPoly &poly = polys[lt.poly];
			const int slot = std::min<int>(std::max<int>(poly.mat_nr, 0), slots - 1);
			bool ok = true;
			for (int k = 0; k < 3; k++) {
				ok = ok && (int)loops[lt.tri[k]].v < nv;
			}
			if (!ok) {
				continue;
			}
			const bool smooth = (poly.flag & ME_SMOOTH) != 0;
			if (!smooth && (int)lt.poly != last_poly) {
				/* flat: the polygon's normal, as the game uses a quad's */
				BKE_mesh_calc_poly_normal(&poly, loops + poly.loopstart, mv, pno);
				last_poly = (int)lt.poly;
			}
			const PartKey part(slot, slot_face_image[slot] ? mtpoly[lt.poly].tpage : NULL);
			std::vector<brb_vertex> &v = verts[part];
			std::vector<uint32_t> &idx = indices[part];
			const uint32_t base = (uint32_t)v.size();
			for (int k = 0; k < 3; k++) {
				const unsigned int l = lt.tri[k];
				const MVert &vert = mv[loops[l].v];
				brb_vertex o;
				memcpy(o.position, vert.co, sizeof(o.position));
				if (smooth) {
					normal_short_to_float_v3(o.normal, vert.no);
				}
				else {
					memcpy(o.normal, pno, sizeof(o.normal));
				}
				o.uv[0] = uv ? uv[l].uv[0] : 0.0f;
				o.uv[1] = uv ? uv[l].uv[1] : 0.0f;
				o.color = 0xFFFFFFFFu;
				v.push_back(o);
				idx.push_back(base + (uint32_t)k);
			}
		}
		std::vector<brb_surface_desc> surfaces;
		r.double_sided = r.blend = r.particle = false;
		r.parts.clear();
		for (std::map<PartKey, std::vector<uint32_t>>::iterator it = indices.begin(); it != indices.end(); ++it) {
			if (it->second.empty()) {
				continue;
			}
			const PartKey &part = it->first;
			Material *ma = give_current_material(ob, (short)(part.first + 1));
			if (ma && ma->game.face_orientation == GEMAT_SHADOW) {
				continue; /* as the game: Remix casts real shadows */
			}
			if (ma && (ma->game.flag & GEMAT_INVISIBLE)) {
				continue; /* P8.8: Invisible, as the game draws it */
			}
			if (!ma || !(ma->game.flag & GEMAT_BACKCULL)) {
				r.double_sided = true;
			}
			MaterialRec &mr = ensure_material(ma, part.second, key, ob, part.first + 1, dm);
			r.parts.push_back(part);
			r.blend = r.blend || mr.blend;
			r.particle = r.particle || mr.particle;
			std::vector<brb_vertex> &pv = verts[part];
			brb_surface_desc sd;
			memset(&sd, 0, sizeof(sd));
			sd.vertices = pv.data();
			sd.vertex_count = (uint32_t)pv.size();
			sd.indices = it->second.data();
			sd.index_count = (uint32_t)it->second.size();
			sd.material = mr.hash;
			surfaces.push_back(sd);
		}
		if (surfaces.empty()) {
			return false;
		}
		const int err = m_bridge->mesh_create(r.hash, surfaces.data(), (uint32_t)surfaces.size());
		g_stats.meshes_sent++;
		return err == 0;
	}

	/* dupli_name: a lamp of a group instance (its own light, keyed by name) */
	void add_lamp(Object *ob, float mat[4][4], const char *dupli_name)
	{
		Lamp *la = (Lamp *)ob->data;
		/* BGE-Remix P8.9: Only Shadow lamps are left out (they darken only) */
		if (!la || (la->mode & (LA_NEG | LA_ONLYSHADOW))) {
			return;
		}
		brmx::LampInput in;
		in.type = la->type;
		in.energy = la->energy;
		in.color[0] = la->r;
		in.color[1] = la->g;
		in.color[2] = la->b;
		in.dist = la->dist;
		in.spotsize = la->spotsize;
		in.spotblend = la->spotblend;
		in.have_lamp = true;
		in.area_size = la->area_size;
		in.area_sizey = la->area_sizey;
		in.area_square = la->area_shape == LA_AREA_SQUARE;
		in.physical = BKE_lamp_is_physical(la);
		in.sun_angle = BKE_lamp_sun_angle(la);
		in.halo = (la->type == LA_SPOT && (la->mode & LA_HALO)) ? la->haint : 0.0f;
		float size[3];
		mat4_to_size(size, mat);
		in.scale[0] = size[0];
		in.scale[1] = size[1];
		brb_light_desc d;
		memset(&d, 0, sizeof(d));
		const void *key = ob;
		if (dupli_name) {
			d.hash = unique_hash("light", dupli_name, NULL);
			key = (const void *)(uintptr_t)d.hash;
		}
		else {
			d.hash = unique_hash("light", ob->id.name + 2, ob);
		}
		if (brmx::lamp_light(in, mat[3], mat[0], mat[1], mat[2], m_light_scale, d, m_dome) == 1) {
			send_light(key, d);
		}
	}

	void send_light(const void *key, const brb_light_desc &d)
	{
		LightRec &r = m_lights[key];
		const unsigned char *bytes = (const unsigned char *)&d;
		if (m_full || r.hash != d.hash || r.desc.size() != sizeof(d) || memcmp(r.desc.data(), bytes, sizeof(d)) != 0) {
			/* new or changed: creating with the same hash replaces the light */
			if (r.hash && r.hash != d.hash) {
				m_bridge->light_destroy(r.hash);
			}
			m_bridge->light_create(&d);
			g_stats.lights_sent++;
			r.hash = d.hash;
			r.desc.assign(bytes, bytes + sizeof(d));
		}
		r.gen = m_gen;
		m_frame_lights.push_back(d.hash);
	}

	std::string m_prefix;
	const BRMX_Bridge *m_bridge = NULL;
	Scene *m_scene = NULL;
	bool m_full = false, m_for_render = false;
	unsigned m_gen;
	std::map<const void *, MeshRec> m_meshes;
	/* by material, face image (P8.4) and, for baked procedural textures, mesh (P8.6) */
	std::map<std::pair<std::pair<const Material *, const Image *>, const void *>, MaterialRec> m_materials;
	std::set<const ID *> m_live_images;
	std::map<const void *, LightRec> m_lights;
	std::map<uint64_t, const void *> m_hashes;
	std::vector<DrawItem> m_draws;
	std::vector<uint64_t> m_frame_lights;
	std::map<const Object *, uint32_t> m_pick_ids;
	std::map<uint32_t, const Object *> m_pick_objects;
	uint32_t m_next_pick;
	uint64_t m_signature = 0;
	float m_dome[3] = {0.0f, 0.0f, 0.0f};
	float m_light_scale = brmx::DEFAULT_LIGHT_SCALE;
	float m_exposure = brmx::DEFAULT_EXPOSURE;
	brmx::TextureCache m_cache;
	int m_waiting;
};

/* ------------------------------------------------------------------------- */
/* Starting Remix (main thread) */

/* Copy or direct mode: the game's rule (BGE_REMIX_PRESENT=copy, the CI
 * stand-in BGE_REMIX_FAKE_VK_DRAW, else the driver's GL_NV_draw_vulkan_image);
 * gl_current: Blender's OpenGL context is current on this thread. */
int choose_mode(bool gl_current)
{
	if (env_is("BGE_REMIX_PRESENT", "copy")) {
		return BRB_MODE_COPY;
	}
	if (gl_current || env_is("BGE_REMIX_FAKE_VK_DRAW", "1")) {
		return BRMX_present_direct_available(NULL) ? BRB_MODE_DIRECT : BRB_MODE_COPY;
	}
	const BRMX_Bridge *b = BRMX_bridge();
	return (b && b->gl_direct_supported()) ? BRB_MODE_DIRECT : BRB_MODE_COPY;
}

/* With the lock held. */
bool ensure_running(int width, int height, bool gl_current, std::string &r_message)
{
	const BRMX_Bridge *b = BRMX_bridge();
	if (!b) {
		r_message = std::string("RTX Remix is not available in this build (") + BRMX_bridge_error() + ").";
		return false;
	}
	int status = BRMX_ensure_loaded();
	if (status != BRB_STATUS_READY && status != BRB_STATUS_RUNNING) {
		r_message = b->status_text();
		return false;
	}
	if (status == BRB_STATUS_RUNNING) {
		const int mode = b->get_mode();
		if (mode != BRB_MODE_COPY && mode != BRB_MODE_DIRECT) {
			r_message = "RTX Remix is running for the Preferences test; restart Blender to use it here.";
			return false;
		}
		return true;
	}
	const int mode = choose_mode(gl_current);
	status = b->startup_ex(mode, std::max(width, 8), std::max(height, 8), NULL, 0, 0);
	if (status != BRB_STATUS_RUNNING) {
		r_message = b->status_text();
		return false;
	}
	printf("BGE-Remix: RTX Remix started for the 3D view and F12 (%s mode, %dx%d).\n",
	       mode == BRB_MODE_DIRECT ? "direct" : "copy", width, height);
	fflush(stdout);
	return true;
}

/* Exposure and quality, sent when they change (with the lock held). */
void apply_scene_settings(const Scene *scene, float exposure)
{
	brmx::apply_exposure(BRMX_bridge(), exposure);
	BRMX_Quality q;
	BRMX_quality_from_scene(scene, &q);
	BRMX_quality_apply(&q);
}

struct Lock {
	Lock() { BRMX_lock(); }
	~Lock() { BRMX_unlock(); }
};

/* ------------------------------------------------------------------------- */
/* The 3D view */

struct ViewSession {
	RenderEngine *engine;
	Sync sync;
	bool need_full;
	int main_generation; /* G.main_generation its records point into */
	double last_change;
	double last_draw;
	float last_camera[32];
	ViewSession(RenderEngine *e)
	    : engine(e), sync("view"), need_full(true), main_generation(G.main_generation), last_change(0.0), last_draw(0.0)
	{
		memset(last_camera, 0, sizeof(last_camera));
	}
};

std::map<RenderEngine *, ViewSession *> g_views;
ViewSession *g_view_owner = NULL;    /* the 3D view that shows Remix */
double g_render_until = 0.0;         /* F12 has Remix until then */

ViewSession *view_session(RenderEngine *engine)
{
	ViewSession *&v = g_views[engine];
	if (!v) {
		v = new ViewSession(engine);
	}
	return v;
}

void view_lost(void *owner)
{
	/* the game or F12 took Remix (and removed our objects) */
	ViewSession *v = (ViewSession *)owner;
	v->sync.forget();
	v->need_full = true;
}

void set_fallback(RenderEngine *engine, const std::string &note)
{
	if (!(engine->flag & RE_ENGINE_VIEW_FALLBACK) || g_stats.last_note != note) {
		printf("BGE-Remix: 3D view: %s\n", note.c_str());
		fflush(stdout);
	}
	engine->flag |= RE_ENGINE_VIEW_FALLBACK;
	g_stats.view_fallbacks++;
	g_stats.last_note = note;
	RE_engine_update_stats(engine, "", note.c_str());
}

/* BGE_REMIX_GL_CHECK=1: the OpenGL state Blender's drawing relies on, before
 * and after the view's draw (T5.7) */
struct GLState {
	GLint ints[24];
	GLfloat floats[36];
	void read()
	{
		memset(ints, 0, sizeof(ints));
		memset(floats, 0, sizeof(floats));
		int i = 0;
		glGetIntegerv(GL_CURRENT_PROGRAM, &ints[i++]);
		glGetIntegerv(GL_ACTIVE_TEXTURE, &ints[i++]);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &ints[i++]);
		glGetIntegerv(GL_MATRIX_MODE, &ints[i++]);
		glGetIntegerv(GL_UNPACK_ALIGNMENT, &ints[i++]);
		glGetIntegerv(GL_UNPACK_ROW_LENGTH, &ints[i++]);
		glGetIntegerv(GL_PACK_ALIGNMENT, &ints[i++]);
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &ints[i++]);
		glGetIntegerv(GL_POLYGON_MODE, &ints[i]);
		i += 2;
		glGetIntegerv(GL_VIEWPORT, &ints[i]);
		i += 4;
		glGetIntegerv(GL_SCISSOR_BOX, &ints[i]);
		i += 4;
		ints[i++] = glIsEnabled(GL_BLEND);
		ints[i++] = glIsEnabled(GL_DEPTH_TEST);
		ints[i++] = glIsEnabled(GL_SCISSOR_TEST);
		ints[i++] = glIsEnabled(GL_TEXTURE_2D);
		ints[i++] = glIsEnabled(GL_CULL_FACE);
		ints[i++] = glIsEnabled(GL_LIGHTING);
		glGetFloatv(GL_MODELVIEW_MATRIX, &floats[0]);
		glGetFloatv(GL_PROJECTION_MATRIX, &floats[16]);
		glGetFloatv(GL_CURRENT_COLOR, &floats[32]);
	}
	int compare(const GLState &o) const
	{
		static const char *names[24] = {"program", "active texture", "texture 2D", "matrix mode", "unpack alignment",
		                                "unpack row length", "pack alignment", "framebuffer", "polygon mode",
		                                "polygon mode back", "viewport x", "viewport y", "viewport w", "viewport h",
		                                "scissor x", "scissor y", "scissor w", "scissor h", "blend", "depth test",
		                                "scissor test", "texture 2D enabled", "cull face", "lighting"};
		int n = 0;
		for (int i = 0; i < 24; i++) {
			if (ints[i] != o.ints[i]) {
				printf("BGE-Remix GL check: %s changed (%d -> %d)\n", names[i], ints[i], o.ints[i]);
				n++;
			}
		}
		if (memcmp(floats, o.floats, sizeof(floats)) != 0) {
			printf("BGE-Remix GL check: a matrix or the current colour changed\n");
			n++;
		}
		return n;
	}
};

}  /* namespace */

/* ------------------------------------------------------------------------- */
/* C interface */

extern "C" {

void BRMX_view_update(RenderEngine *engine, Main *bmain, Scene *scene, ARegion *ar, View3D *v3d, RegionView3D *rv3d)
{
	(void)ar;
	(void)rv3d;
	if (!engine || !scene || !v3d) {
		return;
	}
	Lock lock;
	ViewSession *v = view_session(engine);
	g_stats.view_updates++;
	v->last_change = now_seconds();
	const BRMX_Bridge *b = BRMX_bridge();
	if (BRMX_owner() == v && !v->need_full && v->main_generation == G.main_generation && b &&
	    b->get_status() == BRB_STATUS_RUNNING)
	{
		v->sync.update(b, bmain, scene, v3d->lay, false, false);
		apply_scene_settings(scene, v->sync.exposure());
	}
	else {
		v->need_full = true;
	}
}

int BRMX_view_draw(RenderEngine *engine, Main *bmain, Scene *scene, ARegion *ar, View3D *v3d, RegionView3D *rv3d)
{
	if (!engine || !scene || !ar || !v3d || !rv3d) {
		return 0;
	}
	const int width = ar->winx, height = ar->winy;
	const double now = now_seconds();
	GLState before;
	const bool gl_check = env_is("BGE_REMIX_GL_CHECK", "1");
	if (gl_check) {
		before.read();
	}
	Lock lock;
	ViewSession *v = view_session(engine);
	g_stats.view_draws++;
	v->last_draw = now;

	std::string note;
	if (G.is_rendering || BRMX_owner_kind() == BRMX_OWNER_RENDER || now < g_render_until) {
		note = "RTX Remix is rendering (F12)";
	}
	else if (BRMX_owner_kind() == BRMX_OWNER_GAME) {
		note = "RTX Remix is running the game";
	}
	else if (!rv3d->is_persp) {
		note = "RTX Remix needs a perspective view";
	}
	else if (g_view_owner && g_view_owner != v && now - g_view_owner->last_draw < VIEW_IDLE_SECONDS) {
		note = "RTX Remix is shown in another 3D view";
	}
	else if (!ensure_running(width, height, true, note)) {
		/* note set */
	}
	if (!note.empty()) {
		set_fallback(engine, note);
		return 0;
	}
	engine->flag &= ~RE_ENGINE_VIEW_FALLBACK;
	g_view_owner = v;
	const BRMX_Bridge *b = BRMX_bridge();
	if (BRMX_owner_take(BRMX_OWNER_VIEW, v, view_lost)) {
		v->sync.forget();
		v->need_full = true;
	}
	if (v->main_generation != G.main_generation) {
		/* an undo, redo or reload replaced every object, mesh and material:
		 * the records point at freed data (whose addresses Blender may reuse) */
		v->sync.clear(b);
		v->need_full = true;
		v->main_generation = G.main_generation;
		g_stats.main_reloads++;
	}
	b->resize(width, height, 0, 0);
	if (v->need_full) {
		const double t0 = now_seconds();
		v->sync.update(b, bmain, scene, v3d->lay, true, false);
		v->need_full = false;
		v->last_change = now;
		g_stats.view_full_syncs++;
		g_stats.first_frame_ms = (now_seconds() - t0) * 1000.0;
	}
	else if (Sync::signature(scene, v3d->lay) != v->sync.last_signature()) {
		/* objects came or went without an update (deleting does not tag
		 * anything): what is new is sent, what is gone removed */
		v->sync.update(b, bmain, scene, v3d->lay, false, false);
		v->last_change = now;
	}
	apply_scene_settings(scene, v->sync.exposure());
	const int waiting = v->sync.update_textures();

	/* the view's camera: OpenGL matrices, as Blender draws the view */
	float camera[32];
	memcpy(camera, rv3d->viewmat, sizeof(float) * 16);
	memcpy(camera + 16, rv3d->winmat, sizeof(float) * 16);
	if (memcmp(camera, v->last_camera, sizeof(camera)) != 0) {
		memcpy(v->last_camera, camera, sizeof(camera));
		v->last_change = now;
	}
	std::vector<uint32_t> highlight;
	if (!(v3d->flag2 & V3D_RENDER_OVERRIDE)) {
		v->sync.selected(scene, v3d->lay, highlight);
	}
	g_stats.highlighted = (int)highlight.size();
	v->sync.send_frame(b, camera, camera + 16, highlight);
	g_stats.view_frames++;
	g_stats.width = width;
	g_stats.height = height;

	const int got = BRMX_present_draw(ar->winrct.xmin, ar->winrct.ymin, width, height);
	if (got == 2) {
		g_stats.resized_frames++;
	}
	RE_engine_update_stats(engine, "", waiting ? "RTX Remix | textures loading" : "RTX Remix");
	if (gl_check) {
		GLState after;
		after.read();
		g_stats.gl_differences += before.compare(after);
	}
	return (now - v->last_change < REFINE_SECONDS || waiting > 0 || got != 1) ? 1 : 0;
}

void BRMX_view_free(RenderEngine *engine)
{
	Lock lock;
	std::map<RenderEngine *, ViewSession *>::iterator it = g_views.find(engine);
	if (it == g_views.end()) {
		return;
	}
	ViewSession *v = it->second;
	if (BRMX_owner() == v) {
		const BRMX_Bridge *b = BRMX_bridge();
		if (b && b->get_status() == BRB_STATUS_RUNNING) {
			v->sync.clear(b);
		}
		BRMX_owner_release(v);
	}
	if (g_view_owner == v) {
		g_view_owner = NULL;
	}
	delete v;
	g_views.erase(it);
}

/* ------------------------------------------------------------------------- */
/* F12 */

}  /* extern "C" */

namespace {

struct RenderSession {
	RenderEngine *engine;
	Sync sync;
	bool synced;
	bool lost;
	int width, height;
	float view[16], proj[16];
	double t0;
	RenderSession(RenderEngine *e) : engine(e), sync("render"), synced(false), lost(false), width(0), height(0), t0(0.0)
	{
		memset(view, 0, sizeof(view));
		memset(proj, 0, sizeof(proj));
	}
};

std::map<RenderEngine *, RenderSession *> g_renders;

void render_lost(void *owner)
{
	RenderSession *r = (RenderSession *)owner;
	r->sync.forget();
	r->lost = true;
}

/* The scene camera's OpenGL matrices for a picture of width x height. */
bool camera_matrices(RenderEngine *engine, Scene *scene, int width, int height, float r_view[16], float r_proj[16])
{
	Object *camera = engine->re ? RE_GetCamera(engine->re) : scene->camera;
	if (!camera) {
		camera = scene->camera;
	}
	if (!camera) {
		return false;
	}
	float model[4][4], view[4][4];
	RE_engine_get_camera_model_matrix(engine, camera, false, &model[0][0]);
	normalize_m4(model);
	invert_m4_m4(view, model);
	CameraParams params;
	BKE_camera_params_init(&params);
	BKE_camera_params_from_object(&params, camera);
	BKE_camera_params_compute_viewplane(&params, width, height, scene->r.xasp, scene->r.yasp);
	BKE_camera_params_compute_matrix(&params);
	memcpy(r_view, view, sizeof(float) * 16);
	memcpy(r_proj, params.winmat, sizeof(float) * 16);
	return !params.is_ortho;
}

}  /* namespace */

extern "C" {

/* P8.6: the procedural textures of the visible meshes (duplicated objects
 * are baked when their originals are) */
static void prebake_for_render(Scene *scene)
{
	Scene *sce_iter;
	Base *base;
	for (SETLOOPER(scene, sce_iter, base)) {
		Object *ob = base->object;
		if (ob->type != OB_MESH || !ob->data || !(base->lay & scene->lay) || (ob->restrictflag & OB_RESTRICT_RENDER)) {
			continue;
		}
		const int n = std::max<int>(ob->totcol, 1);
		bool any = false;
		for (int i = 1; i <= n && !any; i++) {
			any = brmx::bake_slots(give_current_material(ob, (short)i)) != 0;
		}
		if (!any) {
			continue;
		}
		const CustomDataMask mask = CD_MASK_BAREMESH | CD_MASK_MLOOPUV | CD_MASK_MTEXPOLY | CD_MASK_ORCO |
		                            CD_MASK_MCOL | CD_MASK_MLOOPCOL;
		DerivedMesh *dm = mesh_get_derived_final(scene, ob, mask);
		if (!dm) {
			continue;
		}
		for (int i = 1; i <= n; i++) {
			Material *ma = give_current_material(ob, (short)i);
			if (brmx::bake_slots(ma)) {
				bake_for(scene, ob, mesh_key(ob), ma, i, dm);
			}
		}
	}
}

int BRMX_render_prepare(Scene *scene, const char **r_message)
{
	static std::string message;
	message.clear();
	Lock lock;
	if (BRMX_owner_kind() == BRMX_OWNER_GAME) {
		message = "RTX Remix is running the game.";
	}
	else {
		const int w = std::max(scene->r.xsch * scene->r.size / 100, 8);
		const int h = std::max(scene->r.ysch * scene->r.size / 100, 8);
		const bool gl_current = wglGetCurrentContext() != NULL;
		if (ensure_running(w, h, gl_current, message)) {
			/* the 3D views give Remix up until F12 has finished (G.is_rendering)
			 * and the size is set here: Remix's window belongs to this thread,
			 * which the render thread must not wait for */
			g_render_until = now_seconds() + 30.0;
			BRMX_bridge()->resize(w, h, 0, 0);
			/* P8.6: the render thread has no OpenGL: procedural textures are
			 * baked here, for the meshes the render shows */
			if (gl_current) {
				prebake_for_render(scene);
			}
			return 1;
		}
	}
	if (r_message) {
		*r_message = message.c_str();
	}
	return 0;
}

int BRMX_render_sync(RenderEngine *engine, Main *bmain, Scene *scene, const char **r_message)
{
	static std::string message;
	message.clear();
	const BRMX_Bridge *b = BRMX_bridge();
	Lock lock;
	RenderSession *&r = g_renders[engine];
	if (!r) {
		r = new RenderSession(engine);
	}
	r->t0 = now_seconds();
	r->width = std::max(engine->resolution_x, 8);
	r->height = std::max(engine->resolution_y, 8);
	if (b && b->get_status() != BRB_STATUS_RUNNING && BLI_thread_is_main()) {
		/* a render from the command line (blender -b ... -f 1) runs on the main
		 * thread, without the render operator that starts Remix */
		if (ensure_running(r->width, r->height, false, message)) {
			b->resize(r->width, r->height, 0, 0);
			message.clear();
		}
		/* else: why it did not start (no runtime, ...) */
	}
	if (!message.empty()) {
		/* set above */
	}
	else if (!b || b->get_status() != BRB_STATUS_RUNNING) {
		message = "RTX Remix is not running (render again; it starts with the render).";
	}
	else if (BRMX_owner_kind() == BRMX_OWNER_GAME) {
		message = "RTX Remix is running the game.";
	}
	else if (!camera_matrices(engine, scene, r->width, r->height, r->view, r->proj)) {
		message = "RTX Remix renders through a perspective scene camera; the scene has none.";
	}
	if (!message.empty()) {
		if (r_message) {
			*r_message = message.c_str();
		}
		return 0;
	}
	g_render_until = now_seconds() + 30.0;
	BRMX_owner_take(BRMX_OWNER_RENDER, r, render_lost);
	r->lost = false;
	/* set by BRMX_render_prepare on the main thread (the window's); changed
	 * here only if something else resized Remix since */
	brb_frame_stats st;
	b->get_stats(&st);
	if (st.width != r->width || st.height != r->height) {
		b->resize(r->width, r->height, 0, 0);
	}
	r->sync.forget();
	r->sync.update(b, bmain, scene, scene->lay, true, true);
	apply_scene_settings(scene, r->sync.exposure());
	r->synced = true;
	g_stats.render_syncs++;
	g_stats.render_width = r->width;
	g_stats.render_height = r->height;
	return 1;
}

int BRMX_render_frame(RenderEngine *engine, Scene *scene, const char **r_message)
{
	static std::string message;
	message.clear();
	const BRMX_Bridge *b = BRMX_bridge();
	RenderSession *r = NULL;
	{
		Lock lock;
		std::map<RenderEngine *, RenderSession *>::iterator it = g_renders.find(engine);
		r = (it != g_renders.end()) ? it->second : NULL;
	}
	if (!r || !r->synced || !b) {
		message = "RTX Remix could not read the scene.";
		if (r_message) {
			*r_message = message.c_str();
		}
		return 0;
	}
	IDProperty *props = scene->id.properties;
	const bool reference = idprop_int(props, "bge_remix_f12_mode", 0) == 1;
	const int frames = std::min(std::max(idprop_int(props, "bge_remix_f12_frames", 32), 1), 4096);
	const int samples = std::min(std::max(idprop_int(props, "bge_remix_f12_samples", 256), 1), 1024);
	/* frames before the picture counts: Remix settles at the render size (it
	 * changes size on the frame after a resize) and fills its history */
	const int settle = 3;
	const int warmup = 8;

	/* textures first: Remix only reads finished files */
	const double wait_start = now_seconds();
	for (;;) {
		int waiting;
		{
			Lock lock;
			waiting = r->sync.update_textures();
		}
		if (waiting <= 0 || RE_engine_test_break(engine) || now_seconds() - wait_start > 300.0) {
			break;
		}
		RE_engine_update_stats(engine, "", "RTX Remix | encoding textures");
		Sleep(20);
	}

	const int total = reference ? warmup + samples : settle + frames;
	std::vector<unsigned char> pixels((size_t)r->width * r->height * 4);
	int w = 0, h = 0, read = 0;
	if (reference) {
		Lock lock;
		b->set_config("rtx.accumulation.numberOfFramesToAccumulate", "1");
		b->set_config("rtx.useDenoiserReferenceMode", "True");
	}
	for (int i = 0; i < total; i++) {
		if (RE_engine_test_break(engine)) {
			message = "cancelled";
			break;
		}
		{
			Lock lock;
			if (r->lost || BRMX_owner() != r) {
				message = "RTX Remix was taken over by the game during the render.";
				break;
			}
			if (reference && i == warmup) {
				/* from here on the frames add up */
				char value[16];
				snprintf(value, sizeof(value), "%d", samples);
				b->set_config("rtx.accumulation.numberOfFramesToAccumulate", value);
			}
			std::vector<uint32_t> none;
			r->sync.send_frame(b, r->view, r->proj, none);
			g_stats.render_frames++;
			const bool last = i == total - 1;
			/* every presented frame is taken (direct mode: Remix waits for it) */
			read = last ? b->frame_read(pixels.data(), (int)pixels.size(), &w, &h) : b->frame_read(NULL, 0, NULL, NULL);
			if (last && read != 1) {
				message = "RTX Remix's picture could not be read.";
			}
		}
		g_render_until = now_seconds() + 30.0;
		RE_engine_update_progress(engine, (float)(i + 1) / (float)total);
		char info[64];
		snprintf(info, sizeof(info), "RTX Remix | %s %d / %d", reference ? "sample" : "frame",
		         reference ? std::max(i + 1 - warmup, 0) : i + 1, reference ? samples : frames);
		RE_engine_update_stats(engine, "", info);
	}
	{
		Lock lock;
		if (reference) {
			b->set_config("rtx.useDenoiserReferenceMode", "False");
			b->set_config("rtx.accumulation.numberOfFramesToAccumulate", "1024");
		}
		b->frame_read_end();
	}
	if (read == 1 && w > 0 && h > 0 && message.empty()) {
		RenderResult *rr = RE_engine_begin_result(engine, 0, 0, w, h, NULL, NULL);
		RenderLayer *rl = rr ? (RenderLayer *)rr->layers.first : NULL;
		RenderPass *rp = rl ? RE_pass_find_by_name(rl, RE_PASSNAME_COMBINED, RE_engine_active_view_get(engine)) : NULL;
		if (rp && rp->rect && rr->rectx == w && rr->recty == h) {
			/* Remix's picture is display (sRGB) values, rows from the top;
			 * Blender's result is linear, rows from the bottom */
			for (int y = 0; y < h; y++) {
				const unsigned char *src = pixels.data() + (size_t)(h - 1 - y) * w * 4;
				float *dst = rp->rect + (size_t)y * w * 4;
				for (int x = 0; x < w; x++) {
					dst[x * 4 + 0] = srgb_to_linearrgb(src[x * 4 + 0] / 255.0f);
					dst[x * 4 + 1] = srgb_to_linearrgb(src[x * 4 + 1] / 255.0f);
					dst[x * 4 + 2] = srgb_to_linearrgb(src[x * 4 + 2] / 255.0f);
					dst[x * 4 + 3] = 1.0f;
				}
			}
			g_stats.render_reads++;
		}
		else if (rr) {
			message = "RTX Remix's picture does not fit the render result (border render is not supported).";
		}
		if (rr) {
			RE_engine_end_result(engine, rr, 0, 0, 0);
		}
	}
	g_stats.render_seconds = now_seconds() - r->t0;
	if (!message.empty() && message != "cancelled") {
		if (r_message) {
			*r_message = message.c_str();
		}
		return 0;
	}
	return 1;
}

void BRMX_render_free(RenderEngine *engine)
{
	Lock lock;
	std::map<RenderEngine *, RenderSession *>::iterator it = g_renders.find(engine);
	if (it == g_renders.end()) {
		return;
	}
	RenderSession *r = it->second;
	if (BRMX_owner() == r) {
		const BRMX_Bridge *b = BRMX_bridge();
		if (b && b->get_status() == BRB_STATUS_RUNNING) {
			r->sync.clear(b);
		}
		BRMX_owner_release(r);
	}
	/* the 3D views wait a moment: the next frame of an animation follows */
	g_render_until = now_seconds() + 1.0;
	delete r;
	g_renders.erase(it);
}

const char *BRMX_view_stats_json(void)
{
	static std::string json;
	Lock lock;
	char buf[2048];
	snprintf(buf, sizeof(buf),
	         "{\"view_updates\": %d, \"view_draws\": %d, \"view_frames\": %d, \"view_fallbacks\": %d, "
	         "\"view_full_syncs\": %d, \"meshes_sent\": %d, \"materials_sent\": %d, \"lights_sent\": %d, "
	         "\"meshes_removed\": %d, \"last_meshes\": %d, \"last_materials\": %d, \"last_lights\": %d, "
	         "\"last_draws\": %d, \"highlighted\": %d, \"gl_differences\": %d, \"resized_frames\": %d, "
	         "\"width\": %d, \"height\": %d, \"render_syncs\": %d, \"render_frames\": %d, \"render_reads\": %d, "
	         "\"render_width\": %d, \"render_height\": %d, \"render_seconds\": %.3f, \"full_sync_ms\": %.2f, "
	         "\"owner\": %d, \"main_reloads\": %d, \"bakes\": %d, ",
	         g_stats.view_updates, g_stats.view_draws, g_stats.view_frames, g_stats.view_fallbacks,
	         g_stats.view_full_syncs, g_stats.meshes_sent, g_stats.materials_sent, g_stats.lights_sent,
	         g_stats.meshes_removed, g_stats.last_meshes, g_stats.last_materials, g_stats.last_lights,
	         g_stats.last_draws, g_stats.highlighted, g_stats.gl_differences, g_stats.resized_frames, g_stats.width,
	         g_stats.height, g_stats.render_syncs, g_stats.render_frames, g_stats.render_reads, g_stats.render_width,
	         g_stats.render_height, g_stats.render_seconds, g_stats.first_frame_ms, BRMX_owner_kind(),
	         g_stats.main_reloads, g_stats.bakes);
	json = buf;
	std::string note;
	for (char c : g_stats.last_note) {
		if (c == '"' || c == '\\') {
			note += '\\';
		}
		note += c;
	}
	json += "\"note\": \"" + note + "\"";
	const BRMX_Bridge *b = BRMX_bridge();
	if (b && b->get_status() == BRB_STATUS_RUNNING) {
		brb_frame_stats st;
		b->get_stats(&st);
		snprintf(buf, sizeof(buf),
		         ", \"bridge\": {\"frames\": %d, \"draws\": %d, \"lights\": %d, \"meshes\": %d, \"materials\": %d, "
		         "\"light_objects\": %d, \"width\": %d, \"height\": %d, \"resets\": %d, \"reads\": %d, \"errors\": %d, "
		         "\"mode\": %d}",
		         st.frames, st.draws, st.lights, st.meshes, st.materials, st.light_objects, st.width, st.height,
		         st.resets, st.reads, st.errors, b->get_mode());
		json += buf;
	}
	json += "}";
	return json.c_str();
}

}  /* extern "C" */
