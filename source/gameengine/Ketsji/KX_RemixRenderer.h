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

/** \file KX_RemixRenderer.h
 *  \ingroup ketsji
 *
 * BGE-Remix: renders a game scene through NVIDIA RTX Remix.
 *
 * The scene is handed to Remix each frame: one Remix mesh per BGE mesh
 * (created once, shared by every object using it), one instance per
 * visible object, the lamps and the world as Remix lights, and BGE's own
 * camera matrices. In copy mode the finished picture comes back and is drawn
 * into the game's viewport with OpenGL, so BGE's text, debug drawing and
 * overlay scenes stay on top. In window mode (blenderplayer only) Remix
 * presents into a child window over the game window instead.
 *
 * Remix on but unusable (not installed, too old, no RTX card): the game
 * runs in OpenGL and a one-line notice says why.
 *
 * P4, moving content: armature-skinned meshes go to Remix once with their
 * bone weights and are skinned by Remix on the GPU (only the bone matrices
 * each frame); every other deformer (shape keys, modifiers, soft bodies,
 * armatures Remix cannot reproduce) runs on the CPU as in BGE and the
 * object's mesh is sent again on the frames it changed. Halo and Billboard
 * faces turn to the camera, Shadow faces are left out (Remix casts real
 * shadows), Blender particle systems become Remix GPU particles, and every
 * object drawn gets a picking ID (bge.render.remixPick / remixHighlight).
 */

#ifndef __KX_REMIXRENDERER_H__
#define __KX_REMIXRENDERER_H__

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "MT_Matrix4x4.h"

#include "bge_remix_bridge.h"  /* C linkage inside */

class KX_Scene;
class KX_Camera;
class KX_GameObject;
class KX_LightObject;
class RAS_ICanvas;
class RAS_MeshObject;
class RAS_IPolyMaterial;
class RAS_Rect;
class RAS_Deformer;
class BL_SkinDeformer;
struct BRMX_Bridge;
struct brb_light_desc;
struct Object;
namespace brmx {
struct MaterialSpec;
class TextureCache;
}

class KX_RemixRenderer
{
public:
	struct Settings {
		int enabled;        /* 1: render through Remix */
		int window_mode;    /* 1: present into a child window (blenderplayer) */
		float light_scale;  /* brightness factor for lamps */
		float exposure;     /* stops added to Remix's automatic exposure (default +2) */
	};

	/* Scene settings: the custom properties bge_remix, bge_remix_fast and
	 * bge_remix_light_scale of the Blender scene (Render panel), unless the
	 * player's -g remix / -g remix_fast override them (-1: no override). */
	static Settings GetSettings(KX_Scene *scene, int override_enabled, int override_fast);

	KX_RemixRenderer(RAS_ICanvas *canvas, bool window_mode_allowed);
	~KX_RemixRenderer();

	/* Called once per frame before the scenes render. Returns the scene to
	 * render through Remix this frame, or NULL for none (OpenGL), starting
	 * Remix on first use. */
	KX_Scene *PickScene(const std::vector<KX_Scene *> &scenes, bool stereo, bool dome,
	                    int override_enabled, int override_fast);

	/* Hand the scene to Remix and show the picture in the viewport.
	 * view and proj: the matrices the game engine uses for this camera. */
	void RenderScene(KX_Scene *scene, KX_Camera *cam, const MT_Matrix4x4 &view,
	                 const MT_Matrix4x4 &proj, const RAS_Rect &viewport);

	/* The scene is about to be removed: forget (and destroy) its objects. */
	void RemoveScene(KX_Scene *scene);

	/* A 2D filter was skipped because of Remix (one notice per game). */
	void NoteSkippedFilters();

	/* Text to show on screen for a few seconds, or NULL. */
	const char *GetNotice(double now);

	/* For tests: BGE_REMIX_STATS names a JSON file written when the game ends. */
	void WriteStats();

	/* P4.8: BGE's own time in the frame that just ended, by part (seconds; from the game
	 * engine's profiler), for the stats' time split. */
	void AddProfileSample(double logic, double physics, double animations, double scenegraph);

	/* For tests: with BGE_REMIX_TRACE set, prints (and flushes) a step. */
	static void Trace(const char *what);

	/* bge.render.remixPick: the object Remix drew at a pixel of the game
	 * window (top left origin). The answer comes a frame or two after the
	 * first call for a point; until then the answer for the previous point
	 * (exact = false) or NULL (exact = true). NULL when nothing is there. */
	KX_GameObject *Pick(int x, int y, bool exact);
	/* bge.render.remixHighlight: outline these objects (empty: none). */
	void SetHighlight(const std::vector<KX_GameObject *> &objects, const float color[3]);
	bool IsRunning() const { return m_running; }

private:
	/* What one mesh hands to Remix (Collect), one entry per surface. */
	struct Geometry {
		std::vector<std::vector<brb_vertex> > verts;
		std::vector<std::vector<uint32_t> > indices;
		std::vector<uint64_t> materials;
		std::vector<std::vector<float> > weights;   /* skinned: bones_per_vertex per vertex */
		std::vector<std::vector<uint32_t> > bones;
		uint32_t bones_per_vertex;
		bool double_sided;
		bool blend;
		bool particle;
		int billboard;     /* 0 no, 1 Halo (faces the camera), 2 Billboard (turns around Z) */
		int shadow_faces;  /* surfaces left out (Shadow face orientation) */
		uint64_t content;  /* hash of the positions and normals */
		int max_influences;/* skinned: most bones on one vertex before keeping 4 */
		Geometry();
	};
	struct MeshEntry {
		uint64_t hash;
		KX_Scene *scene;
		int polygons;      /* to notice a different mesh at a reused address */
		std::string name;
		bool double_sided;
		bool blend;        /* has alpha-blended surfaces */
		bool particle;
		int billboard;
	};
	/* a mesh skinned by Remix: one per mesh and armature */
	struct SkinEntry {
		uint64_t hash;
		KX_Scene *scene;
		std::string name;
		int polygons;
		int groups;        /* deform groups; bones sent = groups + 1 */
		bool double_sided;
		bool blend;
		bool particle;
	};
	/* an object deformed on the CPU: its own Remix mesh */
	struct DeformEntry {
		uint64_t hash;
		KX_Scene *scene;
		std::string name;
		uint64_t content;  /* last sent */
		int last_seen;     /* frame */
		bool double_sided;
		bool blend;
		bool particle;
	};
	struct PickEntry {
		uint32_t id;
		int last_seen;
	};
	struct MaterialEntry {
		uint64_t hash;
		KX_Scene *scene;
		std::string name;
		std::shared_ptr<brmx::MaterialSpec> spec; /* what the translator made of it */
		bool blend;        /* alpha blended */
		bool particle;     /* drawn as a particle (Halo, or the panel's Particle) */
		bool waiting;      /* sent without some textures that are still being encoded */
		/* P8.6: a material with procedural textures, made for one mesh */
		RAS_IPolyMaterial *pm;
		RAS_MeshObject *mesh;
		float obmat[16];           /* where the mesh was when baked */
		uint64_t bake_signature;   /* to bake again when the textures change */
		double bake_checked;
		MaterialEntry() : hash(0), scene(NULL), blend(false), particle(false), waiting(false), pm(NULL), mesh(NULL),
		                  bake_signature(0), bake_checked(0.0) {}
	};
	struct LightEntry {
		uint64_t hash;
		KX_Scene *scene;
		std::vector<unsigned char> desc; /* last description sent */
		int frame;                       /* BGE-Remix P8.9: the last frame it was sent in */
		LightEntry() : hash(0), scene(NULL), frame(0) {}
	};
	int m_light_frame;                   /* BGE-Remix P8.9: counts UpdateLights calls */

	bool Start(KX_Scene *scene, int width, int height, const Settings &settings);
	KX_Scene *PickSceneImpl(const std::vector<KX_Scene *> &scenes, bool stereo, bool dome,
	                        int override_enabled, int override_fast);
	void SetNotice(const std::string &text);
	uint64_t UniqueHash(const std::string &kind, const std::string &name, const void *key);
	bool EnsureMesh(KX_Scene *scene, RAS_MeshObject *mesh, MeshEntry *&r_entry);
	/* client: the object whose own (deformed) vertices to read, NULL = the
	 * mesh's shared ones; weights (by Blender vertex): a skinned mesh */
	void Collect(KX_Scene *scene, RAS_MeshObject *mesh, void *client, const void *weights, Geometry &g,
	             uint32_t identity_bone = 0);
	void CollectDerived(KX_Scene *scene, RAS_MeshObject *mesh, struct DerivedMesh *dm, Geometry &g);
	bool SendGeometry(uint64_t hash, const Geometry &g);
	bool DrawSkinned(KX_Scene *scene, KX_GameObject *obj, RAS_Deformer *def, const float model[16], uint32_t pick);
	bool DrawDeformed(KX_Scene *scene, KX_GameObject *obj, RAS_Deformer *def, const float model[16], uint32_t pick);
	bool DrawEmitters(KX_Scene *scene, KX_GameObject *obj, struct Object *ob, const float model[16], uint32_t pick);
	void SkinCheck(KX_GameObject *obj, BL_SkinDeformer *skin, uint64_t mesh, const float model[16], uint32_t pick);
	void ReleaseSkinning(KX_Scene *scene);
	void PruneDynamic(KX_Scene *scene);
	uint32_t PickId(KX_GameObject *obj);
	void UpdatePicking(KX_Scene *scene);
	bool ObjectAlive(KX_GameObject *obj);
	void NoteOnce(const std::string &text);
	const MaterialEntry &EnsureMaterial(KX_Scene *scene, RAS_IPolyMaterial *mat, RAS_MeshObject *mesh);
	void SendMaterial(MaterialEntry &e);
	void UpdateTextures(KX_Scene *scene);
	/* P8.6: procedural textures baked into images, per mesh */
	void Bake(MaterialEntry &e);
	void UpdateBakes(KX_Scene *scene);
	void ShowRemixWindow(bool show);
	void ApplyExposure(const Settings &settings);
	void UpdateLights(KX_Scene *scene, float light_scale);
	void SendLight(KX_Scene *scene, const void *key, const struct brb_light_desc &desc);
	void MaybeScreenshot();
	bool SaveWindowBmp(const std::wstring &path, int x, int y, int width, int height);

	const BRMX_Bridge *m_bridge;
	RAS_ICanvas *m_canvas;
	bool m_window_mode_allowed;
	bool m_tried;          /* tried to start (once per game) */
	bool m_running;
	int m_mode;            /* brb_mode */
	KX_Scene *m_scene;     /* the scene rendered through Remix */
	Settings m_settings;
	int m_width, m_height; /* render size */
	int m_x, m_y;          /* window mode: position in the game window */
	bool m_shown;          /* window mode: the child window is visible */

	std::map<const void *, MeshEntry> m_meshes;
	/* by polygon material, and mesh for those with baked procedural textures */
	std::map<std::pair<const void *, const void *>, MaterialEntry> m_materials;
	float m_bake_obmat[16];    /* P8.6: the object drawn now, for bakes */
	double m_bakes_checked;
	std::map<const void *, LightEntry> m_lights;
	std::map<uint64_t, const void *> m_hashes;
	/* by mesh and armature names (BGE gives every armature object its own
	 * copy of the armature data, CI run 29) and scene */
	std::map<std::pair<std::string, const void *>, SkinEntry> m_skins;
	std::map<const void *, DeformEntry> m_deformed;                    /* object */
	KX_Scene *m_gpu_scene;     /* objects of this scene are skinned by Remix */
	bool m_cpu_skinning;       /* BGE_REMIX_CPU_SKINNING=1: never on the GPU (B4.1) */
	std::string m_skin_check;  /* BGE_REMIX_SKIN_CHECK: frames to compare with BGE's skinning */
	std::set<std::string> m_noted;
	bool m_colliders;          /* this frame: some object has Collision on (GPU particles bounce) */
	float m_collider_bounce;
	float m_gravity;

	/* picking */
	std::map<const void *, PickEntry> m_pick_ids;
	std::map<uint32_t, KX_GameObject *> m_pick_frame; /* drawn this frame */
	uint32_t m_next_pick;
	bool m_pick_wanted, m_pick_pending, m_pick_has;
	int m_pick_x, m_pick_y;          /* asked for (window pixels) */
	int m_pick_req_x, m_pick_req_y;  /* the request on its way */
	int m_pick_ans_x, m_pick_ans_y;  /* the last answer's point */
	KX_GameObject *m_pick_answer;
	int m_pick_sent_frame;
	std::vector<KX_GameObject *> m_highlight;
	float m_highlight_color[3];
	int m_vp_x0, m_vp_y0;      /* the viewport's top left corner in the window */
	brmx::TextureCache *m_cache;
	int m_materials_waiting;   /* entries with waiting set */
	int m_materials_swapped;   /* sent again with their textures */
	int m_material_notes;      /* lines the translator could not represent */
	int m_bakes, m_rebakes;    /* P8.6: materials baked, and baked again while playing */
	double m_bake_ms;          /* P8.6: time spent baking */
	int m_bakes_cached;        /* P8.7: bakes found in the cache (nothing drawn) */
	double m_bake_gpu_ms;      /* P8.7: their time on the graphics card */
	double m_max_swap_ms;      /* slowest frame that swapped materials */
	double m_frame_ms_max;     /* longest gap between two Remix frames (after the first 10) */
	double m_last_render;
	double m_last_end;           /* when the previous Remix frame was on screen */
	std::vector<float> m_game_ms; /* BGE's own work between two Remix frames */
	int m_first_textured_frame;  /* first frame with every texture in place (-1: not yet) */
	double m_first_textured_seconds; /* ... seconds after Remix started */
	double m_start_time;

	/* direct mode (P4.8): the shared presenter draws the picture (BRMX_present.h) */
	int m_direct_frames_base;    /* its frame count when this game started */
	std::string m_direct_note;   /* why copy mode was used, for the stats */

	/* P4.8 time split (ms per frame) */
	std::vector<float> m_logic_ms, m_physics_ms, m_anim_ms, m_scenegraph_ms, m_addremove_ms;

	std::string m_notice;
	double m_notice_until;
	bool m_notice_shown;
	bool m_filters_noted;
	std::string m_status;

	/* statistics */
	int m_frames;
	std::vector<float> m_handover_ms;
	std::vector<float> m_present_ms;
	std::vector<float> m_copy_ms;
	std::vector<float> m_upload_ms;
	int m_last_draws, m_last_lights;
	int m_screenshot_frame;
	/* P4 */
	int m_skinned_objects, m_deformed_objects, m_billboards, m_emitters, m_particle_draws; /* last frame */
	int m_deform_uploads;      /* CPU-deformed meshes sent (again) */
	int m_cpu_skin_notes;      /* skinned objects Remix cannot skin */
	int m_shadow_faces;        /* surfaces left out */
	int m_picks_answered;
	int m_pick_frames_total, m_pick_frames_count; /* frames from request to answer */
	double m_skin_ms_total;    /* time spent on bones and CPU deformation */
	int m_skin_ms_frames;
};

#endif  /* __KX_REMIXRENDERER_H__ */
