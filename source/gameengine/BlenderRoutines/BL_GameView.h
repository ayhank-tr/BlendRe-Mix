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

/** \file BL_GameView.h
 *  \ingroup blroutines
 *
 * BGE-Remix P7: the Game View's model. The game's own converter builds the
 * scene inside Blender without running the game; the model lists every
 * element the game is made of, by layer, with its state colour and its
 * warnings, and the shapes (ranges, links, outlines) the Game View draws.
 * The editor (view3d_gameview.c) draws it; Python (_bge_gameview) reads it.
 * The same checks run when a game starts (BL_GameViewReportGame).
 *
 * Plain C: included by the editors.
 */

#ifndef __BL_GAMEVIEW_H__
#define __BL_GAMEVIEW_H__

#ifdef __cplusplus
extern "C" {
#endif

struct ARegion;
struct Main;
struct Object;
struct Scene;
struct bContext;

/* the layers, in their clusters (Entity, Factor, Behaviour, Gate) */
typedef enum GVLayer {
	/* Entity */
	GV_LAYER_OBJECTS = 0,
	GV_LAYER_CAMERAS,
	GV_LAYER_INTERFACE,
	/* Factor */
	GV_LAYER_RENDERING,
	GV_LAYER_PHYSICS,
	GV_LAYER_MEDIA,
	GV_LAYER_DATA,
	GV_LAYER_FLUIDS,
	GV_LAYER_ARTICULATIONS,
	/* Behaviour */
	GV_LAYER_LOGIC,
	GV_LAYER_PYTHON,
	GV_LAYER_ANIMATION,
	GV_LAYER_MESSAGES,
	GV_LAYER_NAVIGATION,
	GV_LAYER_DESTRUCTION,
	/* Gate */
	GV_LAYER_INPUT,
	GV_LAYER_SCENES,
	GV_LAYER_REMIX,
	GV_LAYER_PERFORMANCE,
	GV_LAYER_TIME,
	GV_LAYER_LEFTOUT,
	/* not a layer of the design: anything no layer claims, shown with a warning */
	GV_LAYER_UNASSIGNED,
	GV_LAYER_COUNT
} GVLayer;

#define GV_LAYERS_ALL ((1 << GV_LAYER_COUNT) - 1)

typedef enum GVCluster {
	GV_CLUSTER_ENTITY = 0,
	GV_CLUSTER_FACTOR,
	GV_CLUSTER_BEHAVIOUR,
	GV_CLUSTER_GATE,
	GV_CLUSTER_NONE
} GVCluster;

/* Ayhan's state colours */
typedef enum GVState {
	GV_STATE_RED = 0,    /* unavailable */
	GV_STATE_BLACK,      /* available, not set up */
	GV_STATE_WHITE,      /* ready */
	GV_STATE_GREEN,      /* active */
	GV_STATE_BLUE,       /* start / continue (Run, P8) */
	GV_STATE_YELLOW      /* wait / stop (Run, P8) */
} GVState;

/* GVElement.flag */
enum {
	GV_EL_HIDDEN_LAYER = (1 << 0),  /* on a hidden layer, waiting to be added */
	GV_EL_ACTIVE = (1 << 1),        /* the active camera, the starting scene */
	GV_EL_KNOWN = (1 << 2),         /* every warning on it marked as known */
	GV_EL_OVER_TARGET = (1 << 3),   /* over a target limit (flashing outline) */
	GV_EL_SCENE_LEVEL = (1 << 4),   /* no object: placed at the scene's centre */
	GV_EL_SMALL = (1 << 5),         /* a small avatar (logic bricks, tags) */
	GV_EL_NOT_IN_GAME = (1 << 6),   /* in the file, not made by the converter */
	GV_EL_VIEW_ALIGNED = (1 << 7),  /* offset[0] runs along the view's right, not world X */
	GV_EL_PLACED = (1 << 8),        /* its offset was set by its layer (no row layout) */
};

typedef struct GVElement {
	int id;
	short layer;          /* GVLayer */
	short state;          /* GVState */
	int flag;
	char kind[48];        /* registry kind, e.g. "sensor:KEYBOARD" */
	char name[64];        /* what it is called (brick name, object name, ...) */
	char owner[64];       /* the object it belongs to ("" at scene level) */
	char avatar[40];      /* icon name (an avatar in datafiles/gameview) */
	char info[96];        /* one line for the N panel */
	float offset[3];      /* place relative to the owner's origin (world axes) */
	float pos[3];         /* world position when built (scene-level elements) */
	float heat;           /* Performance: share of the scene's estimated cost (0..1) */
	int warning_first;    /* into the model's warnings */
	int warning_count;
} GVElement;

/* GVWarning.level: what the warning does to the element's state colour */
enum {
	GV_WARN_NOTE = 0,     /* no change (advice, or over a target: the flashing outline) */
	GV_WARN_BLACK,        /* not set up: the element stays Black */
	GV_WARN_RED,          /* unavailable: the element is Red */
};

typedef struct GVWarning {
	int element;          /* index into elements */
	short layer;
	short level;          /* GV_WARN_* */
	short mandatory;      /* a mandatory limit (cannot be changed), not a target */
	short target;         /* over a target limit (not a state) */
	short known;          /* marked as known: listed, not warning */
	short pad;
	char code[32];
	char key[160];        /* code|owner|name: what "mark as known" stores */
	char text[160];
} GVWarning;

/* GVShape.type */
enum {
	GV_SHAPE_LINE = 0,     /* a: from element, b: to element (p: offsets) */
	GV_SHAPE_SPHERE,       /* a: element, p[0] radius */
	GV_SHAPE_CONE,         /* a: element, along the owner's -Z: p[0] length, p[1] half angle (rad) */
	GV_SHAPE_FRUSTUM,      /* a: camera element: p[0] near, p[1] far, p[2] half fov x, p[3] half fov y, p[4] ortho scale (0 = perspective) */
	GV_SHAPE_BOX,          /* a: element, owner's axes: p[0..2] half size, p[3..5] centre (local) */
	GV_SHAPE_CAPSULE,      /* a: element: p[0] radius, p[1] height */
	GV_SHAPE_RING,         /* a: element: p[0] radius (horizontal) */
	GV_SHAPE_ARROW,        /* a: element, owner's -Z: p[0] length */
	GV_SHAPE_POINTS,       /* points[] world positions joined in order (trails) */
	GV_SHAPE_TRIS,         /* points[] world triangles (navigation mesh) */
	GV_SHAPE_RECT,         /* a world rectangle: p[0..2] centre, p[3] half width, p[4] half depth (planes, panels) */
	GV_SHAPE_ARC,          /* like LINE, bowed upwards (messages, scene changes) */
	GV_SHAPE_PANEL,        /* a: element whose owner is a camera: a rectangle facing the camera, p[0] half width, p[1] half height, p[2] distance */
};

/* GVShape: p[6] for LINE and ARC: 1 = between the owners' origins, 0 = between the avatars
 * SPHERE: p[0] radius, p[1..3] centre in the owner's axes (scaled) */

typedef struct GVShape {
	short type;
	short layer;
	short state;
	short dashed;
	int a, b;             /* element indices (-1: none) */
	float p[8];
	int point_first;      /* into the model's points (POINTS, TRIS) */
	int point_count;
} GVShape;

typedef struct GVModel {
	int generation;       /* changes with every build */
	char scene[64];
	int converted;        /* the game's converter built the scene */
	double build_ms;      /* the last build (converter and checks) */
	double convert_ms;
	int element_count;
	const GVElement *elements;
	int warning_count;
	const GVWarning *warnings;
	int shape_count;
	const GVShape *shapes;
	int point_count;
	const float (*points)[3];
	int counts[GV_LAYER_COUNT];       /* elements per layer */
	int warnings_per_layer[GV_LAYER_COUNT];
	float centre[3];      /* the scene's centre (scene-level elements) */
	float top;            /* the scene's highest point */
} GVModel;

/* names and clusters (for the UI and the tests) */
const char *BL_GameViewLayerName(int layer);    /* "Objects", "Cameras", ... */
const char *BL_GameViewLayerId(int layer);      /* "OBJECTS", "CAMERAS", ... */
int BL_GameViewLayerCluster(int layer);         /* GVCluster */
const char *BL_GameViewClusterName(int cluster);

/* Build the model of the scene: the game's converter runs inside Blender
 * without starting the game (it needs the window's OpenGL context and a 3D
 * view region), then every layer's elements and checks. Returns 1 on
 * success. */
int BL_GameViewBuild(struct bContext *C, struct ARegion *ar);

/* The last model built (NULL before the first build). */
const GVModel *BL_GameViewModel(void);

/* The editor tells the model the file changed (a rebuild is due). */
void BL_GameViewTagDirty(void);
int BL_GameViewIsDirty(void);
/* forget the model (file loaded, undo) */
void BL_GameViewClear(void);

/* The selected element (-1: none), shared by the view and the N panel. */
int BL_GameViewSelected(void);
void BL_GameViewSetSelected(int element);

/* When a game starts: the same checks on the scene the game converted,
 * printed as "BGE-Remix check: <layer>: <owner>: <text>". Defined as a C++
 * function in BL_GameView.cpp (KX_Scene is C++). */

/* Python: the _bge_gameview module (bpy_interface.c) */
struct _object;
struct _object *BL_GameView_initPython(void);

#ifdef __cplusplus
}

class KX_Scene;
class KX_BlenderSceneConverter;
void BL_GameViewReportGame(KX_Scene *kxscene, struct Main *maggie, KX_BlenderSceneConverter *converter = NULL);
#endif

#endif  /* __BL_GAMEVIEW_H__ */
