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

/** \file gameengine/BlenderRoutines/BL_GameViewRun.cpp
 *  \ingroup blroutines
 *
 * BGE-Remix P10.9: the Remix View's Run mode (BL_GameViewRun.h): the hooks
 * into the embedded loop, the live data (BL_GameViewLive.h), the snapshot a
 * suspend keeps and a resume puts back, the file as it was when Run started,
 * and their Python functions in _bge_gameview.
 *
 * Nothing here keeps a pointer into Blender's data or the game across a
 * return to Blender: the snapshot is by object names, so the file may change
 * in any way while the game is suspended (undo, deleting, another file).
 */

#include <stdio.h>
#include <string.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "GPU_glew.h"

#include "KX_KetsjiEngine.h"
#include "KX_Scene.h"
#include "KX_GameObject.h"
#include "KX_Camera.h"
#include "KX_ClientObjectInfo.h"
#include "KX_LogicBrickState.h"
#ifdef WITH_REMIX
#  include "KX_RemixRenderer.h"  /* IsRunning: Remix's first frame */
#endif

#include "SCA_IObject.h"
#include "SCA_ISensor.h"
#include "SCA_IController.h"
#include "SCA_IActuator.h"

#include "EXP_ListValue.h"
#include "EXP_IntValue.h"
#include "EXP_FloatValue.h"
#include "EXP_BoolValue.h"
#include "EXP_StringValue.h"

#include "PHY_IPhysicsController.h"
#include "PHY_IPhysicsEnvironment.h"

#ifdef WITH_BULLET
#  include "CcdPhysicsEnvironment.h"
#  include "CcdPhysicsController.h"
#  include "btBulletDynamicsCommon.h"
#endif

#include "BL_GameView.h"
#include "BL_GameViewLive.h"
extern "C" {
	#include "PIL_time.h"
}
#include "BL_GameViewRun.h"

extern "C" {
	#include "DNA_object_types.h"
	#include "DNA_property_types.h"
	#include "DNA_scene_types.h"
	#include "DNA_screen_types.h"
	#include "DNA_userdef_types.h"
	#include "DNA_windowmanager_types.h"

	#include "BKE_context.h"
	#include "BKE_global.h"
	#include "BKE_main.h"

	#include "BLI_blenlib.h"

	#include "BLO_undofile.h"
	#include "BLO_writefile.h"

	#include "IMB_imbuf.h"
	#include "IMB_imbuf_types.h"

	#include "MEM_guardedalloc.h"

	#include "../../blender/windowmanager/wm_event_types.h"
	#include "WM_types.h"
}

#ifdef WITH_PYTHON
#  include "Python.h"
#endif

/* ------------------------------------------------------------------------- */
/* the state of a run */

namespace {

enum { SNAP_BOOL = 0, SNAP_INT, SNAP_FLOAT, SNAP_STRING, SNAP_TIMER };

struct SnapProp {
	std::string name;
	int type;
	double num;
	std::string str;
};

struct SnapObject {
	std::string name;
	bool replica;          /* added while the game played */
	float mat[4][4];       /* its place in the world */
	float linv[3], angv[3];
	bool dynamic;
	unsigned int state;    /* the logic states it is in */
	unsigned int init_state;  /* the file's starting states when it was kept */
	bool visible;
	std::vector<SnapProp> props;
};

struct Snapshot {
	bool valid;
	std::string scene;
	bool other_scene;      /* the game was in another scene (or file) */
	int frames;
	std::string camera;
	std::vector<SnapObject> objects;
	std::vector<std::string> ended;  /* objects of the file the game had ended */
	Snapshot() : valid(false), other_scene(false), frames(0) {}
};

struct LiveId {
	std::string kind, owner, name;
};

struct Run {
	bool active;
	bContext *C;
	ARegion *ar;
	Scene *blender_scene;     /* the scene Run started (compared, never used after) */
	int resume, mockup, suspend_after;
	float suspend_seconds;
	int from_remix;
	double start_time, remix_time;  /* when the run started, when Remix first drew (0: not yet) */
	int remix_frame;                /* the frame Remix first drew (-1: none) */
	double remix_played;            /* seconds played since then, at the end (-1: none) */
	bool due;                       /* the run's own suspend is due (decided once, at a drawn frame) */
	int frame[4];
	std::string shot;
	GVRunOverlayFn overlay;
	std::set<std::string> keep;
	int frames;
	int outcome;
	bool suspend_req, stop_req;
	bool restored;
	bool converted;
	bool shot_taken;
	bool eat_release;
	KX_KetsjiEngine *engine;
	/* the file's objects the game started with (a reference each, so an ended
	 * one's memory is not reused by an added one while the game plays) */
	std::set<KX_GameObject *> started;
	std::vector<std::string> started_names;
	/* live */
	std::vector<GVLive> live;
	std::vector<LiveId> live_ids;
	std::vector<std::string> live_prev;
	int live_generation;
	std::map<std::string, std::vector<float> > live_mats;
	Run()
	    : active(false), C(NULL), ar(NULL), blender_scene(NULL), resume(0), mockup(0), suspend_after(0),
	      suspend_seconds(0.0f), from_remix(0), start_time(0.0), remix_time(0.0), remix_frame(-1), remix_played(-1.0),
	      overlay(NULL), frames(0), outcome(GV_RUN_OUT_NONE), suspend_req(false), stop_req(false), restored(false),
	      converted(false), shot_taken(false), eat_release(false), engine(NULL), live_generation(-1)
	{
		memset(frame, 0, sizeof(frame));
	}
};

Run &run()
{
	static Run r;
	return r;
}

Snapshot &snapshot()
{
	static Snapshot s;
	return s;
}

MemFile *g_file = NULL;

void release_started(Run &r)
{
	for (KX_GameObject *go : r.started) {
		go->Release();
	}
	r.started.clear();
}

/* the scene Run started, in the running game (NULL: the game is elsewhere) */
KX_Scene *start_scene(KX_KetsjiEngine *engine, bool *r_other)
{
	if (r_other) {
		*r_other = false;
	}
	if (!engine) {
		return NULL;
	}
	KX_SceneList *scenes = engine->CurrentScenes();
	if (!scenes || scenes->empty()) {
		return NULL;
	}
	for (KX_Scene *scene : *scenes) {
		if (scene->GetBlenderScene() == run().blender_scene) {
			return scene;
		}
	}
	if (r_other) {
		*r_other = true;
	}
	return scenes->front();
}

std::map<std::string, KX_GameObject *> objects_by_name(KX_Scene *scene)
{
	std::map<std::string, KX_GameObject *> map;
	CListValue *list = scene->GetObjectList();
	for (int i = 0; i < list->GetCount(); i++) {
		KX_GameObject *go = (KX_GameObject *)list->GetValue(i);
		/* the first of a name wins: the file's object before what was added */
		map.insert(std::make_pair(std::string(go->GetName().ReadPtr()), go));
	}
	return map;
}

void world_matrix(KX_GameObject *go, float r_mat[4][4])
{
	const MT_Matrix3x3 &ori = go->NodeGetWorldOrientation();
	const MT_Vector3 &scale = go->NodeGetWorldScaling();
	const MT_Point3 &pos = go->NodeGetWorldPosition();
	for (int c = 0; c < 3; c++) {
		for (int r = 0; r < 3; r++) {
			r_mat[c][r] = (float)(ori[r][c] * scale[c]);
		}
		r_mat[c][3] = 0.0f;
	}
	r_mat[3][0] = (float)pos[0];
	r_mat[3][1] = (float)pos[1];
	r_mat[3][2] = (float)pos[2];
	r_mat[3][3] = 1.0f;
}

/* a game property as a snapshot entry (false: a kind the snapshot skips) */
bool snap_prop(CValue *v, const char *name, SnapProp &r)
{
	r.name = name;
	r.num = 0.0;
	switch (v->GetValueType()) {
		case VALUE_BOOL_TYPE:
			r.type = SNAP_BOOL;
			r.num = v->GetNumber();
			return true;
		case VALUE_INT_TYPE:
			r.type = SNAP_INT;
			r.num = v->GetNumber();
			return true;
		case VALUE_FLOAT_TYPE:
			r.type = v->GetProperty("timer") ? SNAP_TIMER : SNAP_FLOAT;
			r.num = v->GetNumber();
			return true;
		case VALUE_STRING_TYPE:
			r.type = SNAP_STRING;
			r.str = v->GetText().ReadPtr();
			return true;
		default:
			return false;
	}
}

void set_prop(KX_GameObject *go, const SnapProp &p)
{
	CValue *v = NULL;
	switch (p.type) {
		case SNAP_BOOL: v = new CBoolValue(p.num != 0.0); break;
		case SNAP_INT: v = new CIntValue((cInt)p.num); break;
		case SNAP_FLOAT: v = new CFloatValue((float)p.num); break;
		case SNAP_STRING: v = new CStringValue(p.str.c_str(), p.name.c_str()); break;
		default: return;  /* timers need the scene's time manager: only the file's are kept */
	}
	go->SetProperty(p.name.c_str(), v);
	v->Release();
}

bool blender_has_prop(Object *ob, const char *name)
{
	if (!ob) {
		return false;
	}
	for (bProperty *p = (bProperty *)ob->prop.first; p; p = p->next) {
		if (STREQ(p->name, name)) {
			return true;
		}
	}
	return false;
}

void apply_live(KX_GameObject *go, const SnapObject &o)
{
	if (o.dynamic && go->GetPhysicsController()) {
		go->setLinearVelocity(MT_Vector3(o.linv[0], o.linv[1], o.linv[2]), false);
		go->setAngularVelocity(MT_Vector3(o.angv[0], o.angv[1], o.angv[2]), false);
	}
	go->SetVisible(o.visible, false);
}

/* ------------------------------------------------------------------------- */
/* the snapshot */

void capture(KX_KetsjiEngine *engine)
{
	Run &r = run();
	Snapshot &s = snapshot();
	s = Snapshot();
	bool other = false;
	KX_Scene *scene = start_scene(engine, &other);
	if (!scene) {
		return;
	}
	s.valid = true;
	s.scene = scene->GetName().ReadPtr();
	s.other_scene = other;
	s.frames = r.frames;
	if (scene->GetActiveCamera()) {
		s.camera = scene->GetActiveCamera()->GetName().ReadPtr();
	}
	std::set<std::string> alive;
	CListValue *list = scene->GetObjectList();
	for (int i = 0; i < list->GetCount(); i++) {
		KX_GameObject *go = (KX_GameObject *)list->GetValue(i);
		const char *name = go->GetName().ReadPtr();
		if (STREQ(name, "__default__cam__")) {
			continue;
		}
		SnapObject o;
		o.name = name;
		o.replica = r.started.count(go) == 0;
		if (!o.replica) {
			alive.insert(o.name);
		}
		world_matrix(go, o.mat);
		o.dynamic = go->GetPhysicsController() && go->IsDynamic();
		for (int k = 0; k < 3; k++) {
			o.linv[k] = o.angv[k] = 0.0f;
		}
		if (o.dynamic) {
			const MT_Vector3 lv = go->GetLinearVelocity(false), av = go->GetAngularVelocity(false);
			for (int k = 0; k < 3; k++) {
				o.linv[k] = (float)lv[k];
				o.angv[k] = (float)av[k];
			}
		}
		o.state = go->GetState();
		Object *bob = go->GetBlenderObject();
		o.init_state = bob ? bob->init_state : 0;
		o.visible = go->GetVisible();
		std::vector<STR_String> names = go->GetPropertyNames();
		for (const STR_String &pn : names) {
			if (pn.ReadPtr()[0] == ':') {
				continue;  /* the engine's own ("::timebomb") */
			}
			CValue *v = go->GetProperty(pn);
			SnapProp p;
			if (v && snap_prop(v, pn.ReadPtr(), p)) {
				o.props.push_back(p);
			}
		}
		s.objects.push_back(o);
	}
	for (const std::string &n : r.started_names) {
		if (!alive.count(n)) {
			s.ended.push_back(n);
		}
	}
}

void restore(KX_Scene *scene)
{
	Snapshot &s = snapshot();
	std::map<std::string, KX_GameObject *> objs = objects_by_name(scene);
	CListValue *inactive = scene->GetInactiveList();
	for (const SnapObject &o : s.objects) {
		if (!o.replica) {
			std::map<std::string, KX_GameObject *>::iterator it = objs.find(o.name);
			if (it == objs.end()) {
				continue;  /* gone from the file while suspended */
			}
			KX_GameObject *go = it->second;
			/* its place and the file's properties come from the file (Python
			 * wrote the suspended game's into it, then you edited them) */
			apply_live(go, o);
			Object *bob = go->GetBlenderObject();
			if (!bob || bob->init_state == o.init_state) {
				go->SetState(o.state);  /* else: you changed its starting states */
			}
			for (const SnapProp &p : o.props) {
				if (!blender_has_prop(bob, p.name.c_str())) {
					set_prop(go, p);  /* added while it played (by a script) */
				}
			}
			continue;
		}
		/* added while it played: added again from its original */
		KX_GameObject *orig = (KX_GameObject *)inactive->FindValue(o.name.c_str());
		if (!orig) {
			continue;
		}
		KX_GameObject *go = (KX_GameObject *)scene->AddReplicaObject(orig, NULL, 0);
		if (!go) {
			continue;
		}
		MT_Matrix3x3 ori;
		MT_Vector3 scale;
		for (int c = 0; c < 3; c++) {
			const float len = sqrtf(o.mat[c][0] * o.mat[c][0] + o.mat[c][1] * o.mat[c][1] + o.mat[c][2] * o.mat[c][2]);
			scale[c] = len;
			for (int k = 0; k < 3; k++) {
				ori[k][c] = len > 1e-12f ? o.mat[c][k] / len : (k == c ? 1.0f : 0.0f);
			}
		}
		go->NodeSetLocalScale(scale);
		go->NodeSetLocalOrientation(ori);
		go->NodeSetLocalPosition(MT_Point3(o.mat[3][0], o.mat[3][1], o.mat[3][2]));
		go->NodeUpdateGS(0);
		apply_live(go, o);
		go->SetState(o.state);
		for (const SnapProp &p : o.props) {
			set_prop(go, p);
		}
		go->Release();  /* AddReplicaObject's reference: the scene keeps its own */
	}
	for (const std::string &n : s.ended) {
		std::map<std::string, KX_GameObject *>::iterator it = objs.find(n);
		if (it != objs.end()) {
			scene->RemoveObject(it->second);
		}
	}
	if (!s.camera.empty() && (!scene->GetActiveCamera() || s.camera != scene->GetActiveCamera()->GetName().ReadPtr())) {
		std::list<KX_Camera *> *cams = scene->GetCameras();
		for (KX_Camera *cam : *cams) {
			if (s.camera == cam->GetName().ReadPtr()) {
				scene->SetActiveCamera(cam);
				break;
			}
		}
	}
}

/* ------------------------------------------------------------------------- */
/* live data */

const char *element_brick_kind(const char *kind)
{
	if (STRPREFIX(kind, "sensor:")) return "S";
	if (STRPREFIX(kind, "controller:")) return "C";
	if (STRPREFIX(kind, "actuator:")) return "A";
	return NULL;
}

SCA_ILogicBrick *find_brick(KX_GameObject *go, const char *which, const char *name)
{
	if (which[0] == 'S') {
		for (SCA_ISensor *s : go->GetSensors()) {
			if (STREQ(s->GetName().ReadPtr(), name)) return s;
		}
	}
	else if (which[0] == 'C') {
		for (SCA_IController *c : go->GetControllers()) {
			if (STREQ(c->GetName().ReadPtr(), name)) return c;
		}
	}
	else {
		for (SCA_IActuator *a : go->GetActuators()) {
			if (STREQ(a->GetName().ReadPtr(), name)) return a;
		}
	}
	return NULL;
}

void value_text(CValue *v, char *buf, size_t len)
{
	switch (v->GetValueType()) {
		case VALUE_BOOL_TYPE:
			BLI_strncpy(buf, v->GetNumber() != 0.0 ? "True" : "False", len);
			break;
		case VALUE_INT_TYPE:
			BLI_snprintf(buf, len, "%lld", (long long)v->GetNumber());
			break;
		case VALUE_FLOAT_TYPE:
			BLI_snprintf(buf, len, "%.4g", v->GetNumber());
			break;
		default:
			BLI_strncpy(buf, v->GetText().ReadPtr(), len);
			break;
	}
}

/* how many bodies each object touches (Bullet's contact manifolds) */
std::map<KX_GameObject *, int> contacts(KX_Scene *scene)
{
	std::map<KX_GameObject *, int> n;
#ifdef WITH_BULLET
	CcdPhysicsEnvironment *env = dynamic_cast<CcdPhysicsEnvironment *>(scene->GetPhysicsEnvironment());
	btDispatcher *dispatcher = env ? env->GetDispatcher() : NULL;
	if (!dispatcher) {
		return n;
	}
	for (int i = 0; i < dispatcher->getNumManifolds(); i++) {
		btPersistentManifold *mf = dispatcher->getManifoldByIndexInternal(i);
		if (!mf || mf->getNumContacts() == 0) {
			continue;
		}
		const btCollisionObject *bodies[2] = {(const btCollisionObject *)mf->getBody0(),
		                                      (const btCollisionObject *)mf->getBody1()};
		for (int k = 0; k < 2; k++) {
			if (!bodies[k]) continue;
			CcdPhysicsController *ctrl = static_cast<CcdPhysicsController *>(bodies[k]->getUserPointer());
			KX_ClientObjectInfo *info = ctrl ? static_cast<KX_ClientObjectInfo *>(ctrl->GetNewClientInfo()) : NULL;
			if (info && info->m_gameobject) {
				n[info->m_gameobject]++;
			}
		}
	}
#else
	(void)scene;
#endif
	return n;
}

void update_live(KX_Scene *scene)
{
	Run &r = run();
	const GVModel *m = BL_GameViewModel();
	if (!m) {
		r.live.clear();
		r.live_ids.clear();
		r.live_generation = -1;
		return;
	}
	if (r.live_generation != m->generation || (int)r.live.size() != m->element_count) {
		GVLive zero;
		memset(&zero, 0, sizeof(zero));
		r.live.assign(m->element_count, zero);
		r.live_ids.assign(m->element_count, LiveId());
		r.live_prev.assign(m->element_count, std::string());
		for (int i = 0; i < m->element_count; i++) {
			r.live_ids[i].kind = m->elements[i].kind;
			r.live_ids[i].owner = m->elements[i].owner;
			r.live_ids[i].name = m->elements[i].name;
		}
		r.live_generation = m->generation;
	}
	std::map<std::string, KX_GameObject *> objs = objects_by_name(scene);
	std::map<KX_GameObject *, int> touching = contacts(scene);
	r.live_mats.clear();
	for (std::map<std::string, KX_GameObject *>::iterator it = objs.begin(); it != objs.end(); ++it) {
		std::vector<float> mat(16);
		world_matrix(it->second, (float(*)[4])&mat[0]);
		r.live_mats[it->first] = mat;
	}
	for (int i = 0; i < m->element_count; i++) {
		const GVElement &e = m->elements[i];
		GVLive &l = r.live[i];
		KX_GameObject *go = NULL;
		if (e.owner[0]) {
			std::map<std::string, KX_GameObject *>::iterator it = objs.find(e.owner);
			go = it == objs.end() ? NULL : it->second;
		}
		l.present = (!e.owner[0] || go) ? 1 : 0;
		l.state = e.state;
		l.fired = 0;
		l.text[0] = '\0';
		if (!l.present) {
			continue;
		}
		const char *brick = element_brick_kind(e.kind);
		if (brick && go) {
			SCA_ILogicBrick *b = find_brick(go, brick, e.name);
			/* BGE-Remix P10.10's contract: the brick's live colour */
			l.state = (short)KX_LogicBrickLiveState(b, e.state == GV_STATE_RED);
			l.fired = l.state == GV_STATE_GREEN;
		}
		else if (STRPREFIX(e.kind, "property:") && go) {
			CValue *v = go->GetProperty(e.name);
			if (v) {
				value_text(v, l.text, sizeof(l.text));
				l.fired = r.live_prev[i] != l.text && !r.live_prev[i].empty();
				r.live_prev[i] = l.text;
			}
		}
		else if (STRPREFIX(e.kind, "body:") && go) {
			std::map<KX_GameObject *, int>::iterator it = touching.find(go);
			const int n = it == touching.end() ? 0 : it->second;
			if (n) {
				BLI_snprintf(l.text, sizeof(l.text), "%d contact%s", n, n == 1 ? "" : "s");
				l.fired = 1;
			}
		}
		if (l.fired) {
			l.fired_frames++;
		}
	}
}

/* the run asked to suspend by itself is due (a number of frames, or of
 * seconds, from the start or from Remix's first frame) */
bool run_due(const Run &r)
{
	if (r.suspend_after > 0 && r.frames >= r.suspend_after) {
		return true;
	}
	if (r.suspend_seconds > 0.0f) {
		const double now = PIL_check_seconds_timer();
		double from = r.start_time;
		if (r.from_remix) {
			if (r.remix_time > 0.0) {
				from = r.remix_time;
			}
			else if (now - r.start_time < 120.0) {
				return false;  /* Remix has not drawn yet */
			}
		}
		return now - from >= r.suspend_seconds;
	}
	return false;
}

void take_shot(const Run &r)
{
	const int x = r.frame[0], y = r.frame[1];
	const int w = r.frame[2] - r.frame[0], h = r.frame[3] - r.frame[1];
	if (w <= 0 || h <= 0) {
		return;
	}
	ImBuf *ibuf = IMB_allocImBuf(w, h, 32, IB_rect);
	if (!ibuf) {
		return;
	}
	glReadBuffer(GL_BACK);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, ibuf->rect);
	ibuf->ftype = IMB_FTYPE_PNG;
	ibuf->planes = 24;
	IMB_saveiff(ibuf, r.shot.c_str(), IB_rect);
	IMB_freeImBuf(ibuf);
}

}  /* namespace */

/* ------------------------------------------------------------------------- */
/* the C interface */

/* the references a run took are given back before the engine stops
 * (BL_GameViewRunStopping); any left over belong to a game that is gone:
 * forgotten, never released (a leak is better than freeing into a scene
 * that no longer exists) */
static void forget_started(Run &r)
{
	if (!r.started.empty()) {
		printf("BGE-Remix Run: %d object references left over (kept)\n", (int)r.started.size());
	}
	r.started.clear();
}

extern "C" void BL_GameViewRunBegin(bContext *C, ARegion *ar, const GVRunOptions *opts)
{
	Run &r = run();
	forget_started(r);
	r.active = true;
	r.C = C;
	r.ar = ar;
	r.blender_scene = CTX_data_scene(C);
	r.resume = opts->resume;
	r.mockup = opts->mockup;
	r.suspend_after = opts->suspend_after;
	r.suspend_seconds = opts->suspend_seconds;
	r.from_remix = opts->from_remix;
	r.start_time = PIL_check_seconds_timer();
	r.remix_time = 0.0;
	r.remix_frame = -1;
	r.remix_played = -1.0;
	r.due = false;
	memcpy(r.frame, opts->frame, sizeof(r.frame));
	r.shot = opts->shot ? opts->shot : "";
	r.overlay = opts->overlay;
	r.keep.clear();
	if (opts->keep) {
		const char *p = opts->keep;
		while (*p) {
			const char *nl = strchr(p, '\n');
			const size_t n = nl ? (size_t)(nl - p) : strlen(p);
			if (n) {
				r.keep.insert(std::string(p, n));
			}
			p += n + (nl ? 1 : 0);
		}
	}
	r.frames = 0;
	r.outcome = GV_RUN_OUT_NONE;
	r.suspend_req = r.stop_req = false;
	r.restored = false;
	r.converted = false;
	r.shot_taken = false;
	r.eat_release = false;
	r.engine = NULL;
	r.started_names.clear();
	r.live.clear();
	r.live_ids.clear();
	r.live_prev.clear();
	r.live_generation = -1;
	r.live_mats.clear();
}

extern "C" int BL_GameViewRunFinish(void)
{
	Run &r = run();
	forget_started(r);
	r.active = false;
	r.engine = NULL;
	r.C = NULL;
	r.ar = NULL;
	r.live_mats.clear();
	if (r.outcome == GV_RUN_OUT_NONE) {
		r.outcome = r.converted ? GV_RUN_OUT_STOP : GV_RUN_OUT_ERROR;
	}
	return r.outcome;
}

extern "C" int BL_GameViewRunActive(void)
{
	return run().active ? 1 : 0;
}

extern "C" int BL_GameViewRunHasSnapshot(void)
{
	return snapshot().valid ? 1 : 0;
}

extern "C" void BL_GameViewRunForget(void)
{
	snapshot() = Snapshot();
}

extern "C" int BL_GameViewRunFrames(void)
{
	return run().frames;
}

extern "C" void BL_GameViewRunButtons(int winx, int winy, int r_suspend[4], int r_stop[4])
{
	const int u = (int)(U.pixelsize > 0.0f ? U.pixelsize : 1.0f);
	const int h = 22 * u, w = 92 * u, gap = 8 * u, top = winy - 8 * u;
	r_suspend[0] = gap;
	r_suspend[1] = top - h;
	r_suspend[2] = gap + w;
	r_suspend[3] = top;
	r_stop[0] = 2 * gap + w;
	r_stop[1] = top - h;
	r_stop[2] = 2 * gap + 2 * w;
	r_stop[3] = top;
	(void)winx;
}

extern "C" int BL_GameViewLiveGeneration(void)
{
	return run().live_generation;
}

extern "C" const GVLive *BL_GameViewLiveGet(int element)
{
	Run &r = run();
	const GVModel *m = BL_GameViewModel();
	if (!m || m->generation != r.live_generation || element < 0 || element >= (int)r.live.size()) {
		return NULL;
	}
	return &r.live[element];
}

extern "C" int BL_GameViewLiveRunning(void)
{
	return (run().active && run().engine) ? 1 : 0;
}

extern "C" int BL_GameViewLiveMatrix(const char *object_name, float r_mat[4][4])
{
	Run &r = run();
	std::map<std::string, std::vector<float> >::const_iterator it = r.live_mats.find(object_name);
	if (it == r.live_mats.end()) {
		return 0;
	}
	memcpy(r_mat, &it->second[0], sizeof(float) * 16);
	return 1;
}

extern "C" void BL_GameViewRunFileFree(void)
{
	if (g_file) {
		BLO_memfile_free(g_file);
		MEM_freeN(g_file);
		g_file = NULL;
	}
}

extern "C" void BL_GameViewRunFileSave(Main *bmain)
{
	BL_GameViewRunFileFree();
	g_file = (MemFile *)MEM_callocN(sizeof(MemFile), "BGE-Remix Run file");
	if (!BLO_write_file_mem(bmain, NULL, g_file, G.fileflags)) {
		BL_GameViewRunFileFree();
	}
}

extern "C" MemFile *BL_GameViewRunFile(void)
{
	return g_file;
}

/* ------------------------------------------------------------------------- */
/* the hooks (BL_KetsjiEmbedStart.cpp) */

void BL_GameViewRunConverted(KX_Scene *scene, KX_KetsjiEngine *engine)
{
	Run &r = run();
	r.engine = engine;
	if (scene->GetBlenderScene() != r.blender_scene) {
		return;  /* another file (Game actuator): played as it is */
	}
	r.converted = true;
	if (r.mockup) {
		/* the rest is not in the mock-up: removed as the game's own End Object
		 * does (whatever pointed at them lets go) */
		std::vector<KX_GameObject *> gone;
		CListValue *list = scene->GetObjectList();
		for (int i = 0; i < list->GetCount(); i++) {
			KX_GameObject *go = (KX_GameObject *)list->GetValue(i);
			const char *name = go->GetName().ReadPtr();
			if (!STREQ(name, "__default__cam__") && !r.keep.count(name)) {
				gone.push_back(go);
			}
		}
		for (KX_GameObject *go : gone) {
			scene->RemoveObject(go);
		}
	}
	release_started(r);
	r.started_names.clear();
	CListValue *list = scene->GetObjectList();
	for (int i = 0; i < list->GetCount(); i++) {
		KX_GameObject *go = (KX_GameObject *)list->GetValue(i);
		if (r.started.insert(go).second) {
			go->AddRef();
			r.started_names.push_back(go->GetName().ReadPtr());
		}
	}
	if (r.resume && !r.restored && snapshot().valid && !snapshot().other_scene &&
	    snapshot().scene == scene->GetName().ReadPtr())
	{
		/* (after the started set: what restore adds counts as added by the game) */
		restore(scene);
		r.restored = true;
	}
}

bool BL_GameViewRunEvent(const wmEvent *event, ARegion *ar)
{
	Run &r = run();
	if (event->type == PAUSEKEY && event->val == KM_PRESS) {
		r.suspend_req = true;
		return true;
	}
	if (event->type == LEFTMOUSE && ar) {
		if (event->val == KM_RELEASE && r.eat_release) {
			r.eat_release = false;
			return true;
		}
		if (event->val != KM_PRESS) {
			return false;
		}
		const int x = event->x - ar->winrct.xmin, y = event->y - ar->winrct.ymin;
		int sus[4], stop[4];
		BL_GameViewRunButtons(ar->winx, ar->winy, sus, stop);
		if (x >= sus[0] && x <= sus[2] && y >= sus[1] && y <= sus[3]) {
			r.suspend_req = true;
			r.eat_release = true;
			return true;
		}
		if (x >= stop[0] && x <= stop[2] && y >= stop[1] && y <= stop[3]) {
			r.stop_req = true;
			r.eat_release = true;
			return true;
		}
	}
	return false;
}

int BL_GameViewRunFrame(KX_KetsjiEngine *engine)
{
	Run &r = run();
	r.engine = engine;
	if (r.stop_req) {
		r.outcome = GV_RUN_OUT_STOP;
		return KX_EXIT_REQUEST_BLENDER_ESC;
	}
	/* BGE-Remix P10 sign-off fix: the time-based suspend is decided where the
	 * frame is drawn (BL_GameViewRunPreSwap), not again here: a clock that
	 * crossed the line in between suspended the game without its picture */
	if (r.suspend_req || r.due) {
		r.outcome = GV_RUN_OUT_SUSPEND;
		return KX_EXIT_REQUEST_BLENDER_ESC;
	}
	return 0;
}

void BL_GameViewRunPreSwap(void *engine_v)
{
	Run &r = run();
	KX_KetsjiEngine *engine = (KX_KetsjiEngine *)engine_v;
	if (!r.active || !engine) {
		return;
	}
	r.engine = engine;
	r.frames++;
	KX_Scene *scene = start_scene(engine, NULL);
	if (!scene) {
		return;
	}
	update_live(scene);
	KX_Camera *cam = scene->GetActiveCamera();
	if (r.overlay && cam && r.C && r.ar) {
		float view[4][4], proj[4][4];
		cam->GetWorldToCamera().getValue(&view[0][0]);
		cam->GetProjectionMatrix().getValue(&proj[0][0]);
		r.overlay(r.C, r.ar, r.frame, view, proj);
	}
#ifdef WITH_REMIX
	/* the first frame RTX Remix drew for the game (when the 3D view had
	 * started Remix already, it takes over at once: that can be frame 1) */
	if (r.remix_time == 0.0 && engine->GetRemixScene() == scene && engine->GetRemix() &&
	    engine->GetRemix()->IsRunning())
	{
		r.remix_time = PIL_check_seconds_timer();  /* RTX Remix drew this frame */
		r.remix_frame = r.frames;
	}
#endif
	r.due = r.due || run_due(r);
	const bool last = r.suspend_req || r.stop_req || r.due;
	if (last && !r.shot.empty() && !r.shot_taken) {
		take_shot(r);
		r.shot_taken = true;
	}
}

void BL_GameViewRunStopping(KX_KetsjiEngine *engine)
{
	Run &r = run();
	if (r.remix_time > 0.0) {
		r.remix_played = PIL_check_seconds_timer() - r.remix_time;
	}
	if (r.outcome == GV_RUN_OUT_SUSPEND) {
		capture(engine);
	}
	else if (r.outcome == GV_RUN_OUT_NONE) {
		const int code = engine->GetExitCode();
		if (code == KX_EXIT_REQUEST_BLENDER_ESC || code == KX_EXIT_REQUEST_OUTSIDE) {
			r.outcome = GV_RUN_OUT_STOP;
		}
		else if (code == KX_EXIT_REQUEST_QUIT_GAME || code == KX_EXIT_REQUEST_NO_SCENES_LEFT) {
			r.outcome = GV_RUN_OUT_QUIT;
		}
		/* a restart or another file: the loop goes on */
	}
	release_started(r);
	r.engine = NULL;
}

/* ------------------------------------------------------------------------- */
/* Python */

#ifdef WITH_PYTHON

static const char *outcome_name(int o)
{
	static const char *names[] = {"NONE", "SUSPEND", "STOP", "QUIT", "ERROR"};
	return (o >= 0 && o <= GV_RUN_OUT_ERROR) ? names[o] : "?";
}

static PyObject *gvr_live(PyObject *, PyObject *)
{
	Run &r = run();
	static const char *states[] = {"RED", "BLACK", "WHITE", "GREEN", "BLUE", "YELLOW"};
	PyObject *list = PyList_New(0);
	for (size_t i = 0; i < r.live.size(); i++) {
		const GVLive &l = r.live[i];
		const LiveId &id = r.live_ids[i];
		PyObject *d = Py_BuildValue("{s:i,s:s,s:s,s:s,s:s,s:O,s:O,s:i,s:s}", "index", (int)i, "kind", id.kind.c_str(),
		                            "owner", id.owner.c_str(), "name", id.name.c_str(),
		                            "state", (l.state >= 0 && l.state <= 5) ? states[l.state] : "?",
		                            "fired", l.fired ? Py_True : Py_False, "present", l.present ? Py_True : Py_False,
		                            "fired_frames", l.fired_frames, "text", l.text);
		PyList_Append(list, d);
		Py_DECREF(d);
	}
	return list;
}

static PyObject *gvr_result(PyObject *, PyObject *)
{
	Run &r = run();
	return Py_BuildValue("{s:s,s:i,s:i,s:O,s:O,s:i,s:d}", "outcome", outcome_name(r.outcome), "frames", r.frames,
	                     "live_generation", r.live_generation, "snapshot", snapshot().valid ? Py_True : Py_False,
	                     "other_scene", snapshot().other_scene ? Py_True : Py_False,
	                     "remix_frame", r.remix_frame,
	                     "remix_seconds", r.remix_played);
}

static PyObject *gvr_snapshot(PyObject *, PyObject *)
{
	Snapshot &s = snapshot();
	if (!s.valid) {
		Py_RETURN_NONE;
	}
	static const char *types[] = {"BOOL", "INT", "FLOAT", "STRING", "TIMER"};
	PyObject *objs = PyList_New(0);
	for (const SnapObject &o : s.objects) {
		PyObject *mat = PyList_New(16);
		for (int k = 0; k < 16; k++) {
			PyList_SET_ITEM(mat, k, PyFloat_FromDouble(((const float *)o.mat)[k]));
		}
		PyObject *props = PyList_New(0);
		for (const SnapProp &p : o.props) {
			PyObject *v;
			switch (p.type) {
				case SNAP_BOOL: v = PyBool_FromLong(p.num != 0.0); break;
				case SNAP_INT: v = PyLong_FromLongLong((long long)p.num); break;
				case SNAP_STRING: v = PyUnicode_FromString(p.str.c_str()); break;
				default: v = PyFloat_FromDouble(p.num); break;
			}
			PyObject *t = Py_BuildValue("(ssN)", p.name.c_str(), types[p.type], v);
			PyList_Append(props, t);
			Py_DECREF(t);
		}
		PyObject *d = Py_BuildValue("{s:s,s:O,s:N,s:(fff),s:(fff),s:O,s:I,s:I,s:O,s:N}", "name", o.name.c_str(),
		                            "replica", o.replica ? Py_True : Py_False, "matrix", mat,
		                            "linear_velocity", o.linv[0], o.linv[1], o.linv[2],
		                            "angular_velocity", o.angv[0], o.angv[1], o.angv[2],
		                            "dynamic", o.dynamic ? Py_True : Py_False, "state", o.state,
		                            "init_state", o.init_state, "visible", o.visible ? Py_True : Py_False,
		                            "props", props);
		PyList_Append(objs, d);
		Py_DECREF(d);
	}
	PyObject *ended = PyList_New(0);
	for (const std::string &n : s.ended) {
		PyObject *v = PyUnicode_FromString(n.c_str());
		PyList_Append(ended, v);
		Py_DECREF(v);
	}
	return Py_BuildValue("{s:s,s:O,s:i,s:s,s:N,s:N}", "scene", s.scene.c_str(),
	                     "other_scene", s.other_scene ? Py_True : Py_False, "frames", s.frames,
	                     "camera", s.camera.c_str(), "objects", objs, "ended", ended);
}

static PyObject *gvr_forget(PyObject *, PyObject *)
{
	BL_GameViewRunForget();
	Py_RETURN_NONE;
}

static PyObject *gvr_file_saved(PyObject *, PyObject *)
{
	return PyBool_FromLong(g_file != NULL);
}

static PyObject *gvr_file_free(PyObject *, PyObject *)
{
	BL_GameViewRunFileFree();
	Py_RETURN_NONE;
}

static PyObject *gvr_buttons(PyObject *, PyObject *args)
{
	int winx, winy, sus[4], stop[4];
	if (!PyArg_ParseTuple(args, "ii", &winx, &winy)) {
		return NULL;
	}
	BL_GameViewRunButtons(winx, winy, sus, stop);
	return Py_BuildValue("{s:(iiii),s:(iiii)}", "suspend", sus[0], sus[1], sus[2], sus[3],
	                     "stop", stop[0], stop[1], stop[2], stop[3]);
}

static PyMethodDef gvr_methods[] = {
	{"live", gvr_live, METH_NOARGS, "Run mode: each element's live data of the last frame played"},
	{"run_result", gvr_result, METH_NOARGS, "Run mode: how the last run ended"},
	{"run_snapshot", gvr_snapshot, METH_NOARGS, "Run mode: the suspended game's state (None: none kept)"},
	{"run_forget", gvr_forget, METH_NOARGS, "Run mode: drop the suspended game's state"},
	{"run_file_saved", gvr_file_saved, METH_NOARGS, "Run mode: the file as it was when Run started is kept"},
	{"run_file_free", gvr_file_free, METH_NOARGS, "Run mode: drop the file as it was when Run started"},
	{"run_buttons", gvr_buttons, METH_VARARGS, "Run mode: the Suspend and Stop buttons' rectangles in a region"},
	{NULL, NULL, 0, NULL}
};

extern "C" void BL_GameViewRun_addPython(PyObject *module)
{
	if (module) {
		PyModule_AddFunctions(module, gvr_methods);
	}
}

#else

extern "C" void BL_GameViewRun_addPython(struct _object *) {}

#endif  /* WITH_PYTHON */
