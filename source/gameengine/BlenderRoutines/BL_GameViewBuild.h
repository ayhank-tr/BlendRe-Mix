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

/** \file BL_GameViewBuild.h
 *  \ingroup blroutines
 *
 * BGE-Remix P7: building the Game View's model (C++, internal). What the
 * game's converter made (GVConverted) and Blender's own data go in; the
 * elements of every layer, their states, warnings and shapes come out.
 */

#ifndef __BL_GAMEVIEWBUILD_H__
#define __BL_GAMEVIEWBUILD_H__

#include <map>
#include <set>
#include <string>
#include <vector>

#include "BL_GameView.h"

struct ID;
struct Main;
struct Object;
struct Scene;
struct bController;
struct bSensor;
class KX_Scene;

/* what the game's converter made of the scene */
struct GVConverted {
	bool converted;
	std::set<std::string> objects;      /* every game object (also on hidden layers) */
	std::set<std::string> hidden;       /* game objects on hidden layers */
	std::set<std::string> bricks;       /* "object/sensor", "object/controller", "object/actuator" names */
	std::set<std::string> physics;      /* objects with a physics controller */
	std::set<std::string> properties;   /* "object/property" */
	std::set<std::string> glsl_fallback; /* P8: materials drawn without their GLSL shader */
	double convert_ms;
	GVConverted() : converted(false), convert_ms(0.0) {}
};

/* what the game made of a converted KX_Scene */
void BL_GameViewCollect(KX_Scene *kxscene, GVConverted &r_conv);

/* one entry of the registry: every kind the Game View knows */
struct GVKind {
	const char *kind;     /* "sensor:KEYBOARD" */
	int layer;            /* GVLayer */
	const char *avatar;   /* icon name */
	int is_small;         /* a small avatar (bricks, tags) */
};

const GVKind *BL_GameViewKinds(int *r_count);
const GVKind *BL_GameViewKind(const char *kind);

/* the target limits (scene ID property "_gameview_targets") */
struct GVTargets {
	int fps;
	int objects;
	int lights;
	int sounds;
	int bodies;
	int triangles;
	float texture_mb;
	int shadow_size;
	GVTargets();
};

class GVBuilder {
public:
	GVBuilder(Main *bmain, Scene *scene, const GVConverted &conv);
	void Build();

	/* results (flattened by Build) */
	std::vector<GVElement> elements;
	std::vector<GVWarning> warnings;
	std::vector<GVShape> shapes;
	std::vector<float> points;  /* x, y, z ... */
	float centre[3];
	float top;

private:
	Main *m_main;
	Scene *m_scene;
	const GVConverted &m_conv;
	GVTargets m_targets;
	std::set<std::string> m_known;
	std::vector<Object *> m_owner;                /* per element (NULL: scene level) */
	std::vector<std::vector<GVWarning> > m_warn;  /* per element */
	std::vector<Object *> m_objects;              /* the scene's objects (and its set scenes') */
	std::map<std::string, int> m_objectElement;   /* object name -> its Objects element */
	std::map<std::string, int> m_brickElement;    /* "object/brick" -> element */
	Object *m_camera;
	int m_physx;                                  /* 0 Bullet, 1 PhysX CPU, 2 PhysX GPU, -1 none */
	bool m_remix;                                 /* the scene uses RTX Remix */
	std::set<std::string> m_libloads;             /* files loaded by scripts */
	/* the sensors linked to each controller, from the sensors' own links:
	 * a controller's slinks are not saved in files (readfile clears them;
	 * only the logic editor fills them again) */
	std::map<const bController *, std::vector<bSensor *> > m_sensorsOf;
	const std::vector<bSensor *> &SensorsOf(const bController *c) const;

	/* adding */
	int Add(const char *kind, const char *name, Object *owner, const char *info = "");
	void Warn(int el, const char *code, int level, const char *text, bool mandatory = false, bool target = false);
	int Shape(int type, int layer, int a, int b = -1);
	int Points(int shape, const std::vector<float> &xyz);
	bool InGame(Object *ob) const;
	bool Converted(Object *ob) const;
	float Top(Object *ob) const;

	/* the layers */
	void Objects();
	void Cameras();
	void Interface();
	void Rendering();
	void Physics();
	void Media();
	void Data();
	void Logic();
	void Python();
	void LibLoads(const std::string &src, const char *script, Object *ob, struct ID *id);
	void Animation();
	void Messages();
	void Navigation();
	void Input();
	void Scenes();
	void Remix();
	void Performance();
	void Time();
	void LeftOut();

	/* after: states, places, flattening */
	void States();
	void Layout();
	void Flatten();
};

#endif  /* __BL_GAMEVIEWBUILD_H__ */
