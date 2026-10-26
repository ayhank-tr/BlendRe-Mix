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
 */

#ifndef __KX_REMIXRENDERER_H__
#define __KX_REMIXRENDERER_H__

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "MT_Matrix4x4.h"

class KX_Scene;
class KX_Camera;
class KX_GameObject;
class KX_LightObject;
class RAS_ICanvas;
class RAS_MeshObject;
class RAS_IPolyMaterial;
class RAS_Rect;
struct BRMX_Bridge;
struct brb_light_desc;

class KX_RemixRenderer
{
public:
	struct Settings {
		int enabled;        /* 1: render through Remix */
		int window_mode;    /* 1: present into a child window (blenderplayer) */
		float light_scale;  /* brightness factor for lamps */
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

private:
	struct MeshEntry {
		uint64_t hash;
		KX_Scene *scene;
		int polygons;      /* to notice a different mesh at a reused address */
		std::string name;
		bool double_sided;
	};
	struct MaterialEntry {
		uint64_t hash;
		KX_Scene *scene;
		std::string name;
	};
	struct LightEntry {
		uint64_t hash;
		KX_Scene *scene;
		std::vector<unsigned char> desc; /* last description sent */
	};

	bool Start(KX_Scene *scene, int width, int height, const Settings &settings);
	KX_Scene *PickSceneImpl(const std::vector<KX_Scene *> &scenes, bool stereo, bool dome,
	                        int override_enabled, int override_fast);
	void SetNotice(const std::string &text);
	uint64_t UniqueHash(const std::string &kind, const std::string &name, const void *key);
	bool EnsureMesh(KX_Scene *scene, RAS_MeshObject *mesh, MeshEntry *&r_entry);
	uint64_t EnsureMaterial(KX_Scene *scene, RAS_IPolyMaterial *mat);
	void ShowRemixWindow(bool show);
	void UpdateLights(KX_Scene *scene, float light_scale);
	void SendLight(KX_Scene *scene, const void *key, const struct brb_light_desc &desc);
	void DrawPicture(int width, int height);
	void MaybeScreenshot();

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
	std::map<const void *, MaterialEntry> m_materials;
	std::map<const void *, LightEntry> m_lights;
	std::map<uint64_t, const void *> m_hashes;

	unsigned int m_texture;
	int m_tex_w, m_tex_h;

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
};

#endif  /* __KX_REMIXRENDERER_H__ */
