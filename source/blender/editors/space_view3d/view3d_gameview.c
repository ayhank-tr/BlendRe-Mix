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

/** \file blender/editors/space_view3d/view3d_gameview.c
 *  \ingroup spview3d
 *
 * BGE-Remix P7: the Game View. A 3D View with V3D_GAMEVIEW set shows the
 * model the game's converter builds (BL_GameView.h) as holograms over the
 * scene: an avatar for every element, coloured by its state, with its ranges,
 * links and outlines, layer by layer.
 *
 * The model is rebuilt when the file changes: at once when the last build was
 * quick (under 0.25 s), else after two quiet seconds (builds under 5 s), else
 * only with Refresh. Avatars follow their objects between builds.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_screen_types.h"
#include "DNA_space_types.h"
#include "DNA_userdef_types.h"
#include "DNA_view3d_types.h"
#include "DNA_windowmanager_types.h"

#include "BLI_utildefines.h"
#include "BLI_fileops.h"
#include "BLI_ghash.h"
#include "BLI_listbase.h"
#include "BLI_math.h"
#include "BLI_path_util.h"
#include "BLI_string.h"

#include "BKE_appdir.h"
#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_screen.h"

#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "BLF_api.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "GPU_draw.h"

#include "PIL_time.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_screen.h"
#include "ED_view3d.h"

#include "UI_resources.h"

#include "view3d_intern.h"  /* own include */

#ifdef WITH_GAMEENGINE

#include "BL_GameView.h"

/* ------------------------------------------------------------------------- */
/* the rebuild policy */

#define GV_REBUILD_AT_ONCE_MS 250.0   /* quick builds: rebuild on every change */
#define GV_REBUILD_IDLE_MS 5000.0     /* slower builds: after GV_IDLE_SECONDS quiet; slower still: Refresh only */
#define GV_IDLE_SECONDS 2.0
#define GV_TIMER_STEP 0.5

static double gv_last_change = 0.0;     /* when the file last changed */
static wmTimer *gv_timer = NULL;        /* redraws while waiting (idle rebuild, flashing outlines) */
static bool gv_flashing = false;        /* the last draw had flashing outlines */

static bool gv_timer_valid(wmWindowManager *wm)
{
	wmTimer *wt;
	if (!gv_timer) {
		return false;
	}
	for (wt = wm->timers.first; wt; wt = wt->next) {
		if (wt == gv_timer) {
			return true;
		}
	}
	gv_timer = NULL;  /* freed with its window */
	return false;
}

static void gv_timer_set(wmWindowManager *wm, wmWindow *win, bool on)
{
	const bool valid = gv_timer_valid(wm);
	if (on && !valid && win) {
		gv_timer = WM_event_add_timer_notifier(wm, win, NC_SPACE | ND_SPACE_VIEW3D, GV_TIMER_STEP);
	}
	else if (!on && valid) {
		WM_event_remove_timer_notifier(wm, gv_timer->win, gv_timer);
		gv_timer = NULL;
	}
}

/* the file changed (view3d_main_region_listener) */
void view3d_gameview_tag_dirty(void)
{
	BL_GameViewTagDirty();
	gv_last_change = PIL_check_seconds_timer();
}

void view3d_gameview_file_loaded(void)
{
	BL_GameViewClear();
	gv_last_change = PIL_check_seconds_timer();
}

/* build now (the window's OpenGL context is current, as when drawing) */
static bool gv_build(bContext *C, ARegion *ar)
{
	wmWindow *win = CTX_wm_window(C);
	ListBase queue;
	int ok;

	/* like a game start: no events get through while the converter runs */
	if (win) {
		queue = win->queue;
		BLI_listbase_clear(&win->queue);
	}
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	ok = BL_GameViewBuild(C, ar);
	GPU_state_init();
	GPU_set_tpage(NULL, 0, 0);
	glPopAttrib();
	if (win) {
		BLI_movelisttolist(&queue, &win->queue);
		win->queue = queue;
	}
	return ok != 0;
}

/* at the start of a Game View's draw: rebuild when due */
void view3d_gameview_update(const bContext *C, ARegion *ar)
{
	const GVModel *m = BL_GameViewModel();
	bool build = false, wait = false;

	if (m == NULL) {
		build = true;
	}
	else if (BL_GameViewIsDirty() && !G.moving) {
		if (m->build_ms < GV_REBUILD_AT_ONCE_MS) {
			build = true;
		}
		else if (m->build_ms < GV_REBUILD_IDLE_MS) {
			if (PIL_check_seconds_timer() - gv_last_change >= GV_IDLE_SECONDS) {
				build = true;
			}
			else {
				wait = true;
			}
		}
	}
	if (build) {
		ScrArea *sa = CTX_wm_area(C);
		gv_build((bContext *)C, ar);
		/* the header and the N panel show the model too */
		if (sa) {
			ARegion *other;
			for (other = sa->regionbase.first; other; other = other->next) {
				if (other != ar) {
					ED_region_tag_redraw(other);
				}
			}
		}
	}
	gv_timer_set(CTX_wm_manager(C), CTX_wm_window(C), wait || gv_flashing);
}

void view3d_gameview_region_exit(wmWindowManager *wm)
{
	gv_flashing = false;
	gv_timer_set(wm, NULL, false);
}

/* ------------------------------------------------------------------------- */
/* avatars: datafiles/gameview/<ICON>.obj and <ICON>_tex.png */

typedef struct GVAvatar {
	GLuint list_face;   /* the textured front and back */
	GLuint list_edge;   /* the side walls (in the state's colour) */
	GLuint tex;
	bool ok;
} GVAvatar;

static GHash *gv_avatars = NULL;

static bool gv_avatar_load(const char *name, GVAvatar *av)
{
	const char *dir = BKE_appdir_folder_id(BLENDER_DATAFILES, "gameview");
	char path[FILE_MAX], file[FILE_MAX];
	float (*v)[3] = NULL, (*vt)[2] = NULL;
	int totv = 0, totvt = 0, allocv = 0, allocvt = 0;
	char line[256];
	bool edge = false;
	FILE *fp;
	ImBuf *ibuf;
	int pass;

	memset(av, 0, sizeof(*av));
	if (!dir) {
		return false;
	}
	BLI_snprintf(file, sizeof(file), "%s.obj", name);
	BLI_join_dirfile(path, sizeof(path), dir, file);
	fp = BLI_fopen(path, "r");
	if (!fp) {
		return false;
	}
	/* the vertices first, then the faces in two display lists */
	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == 'v' && line[1] == ' ') {
			if (totv == allocv) {
				allocv = allocv ? allocv * 2 : 256;
				v = MEM_reallocN_id(v, sizeof(*v) * allocv, __func__);
			}
			sscanf(line + 2, "%f %f %f", &v[totv][0], &v[totv][1], &v[totv][2]);
			totv++;
		}
		else if (line[0] == 'v' && line[1] == 't') {
			if (totvt == allocvt) {
				allocvt = allocvt ? allocvt * 2 : 64;
				vt = MEM_reallocN_id(vt, sizeof(*vt) * allocvt, __func__);
			}
			sscanf(line + 3, "%f %f", &vt[totvt][0], &vt[totvt][1]);
			totvt++;
		}
	}
	av->list_face = glGenLists(2);
	av->list_edge = av->list_face + 1;
	for (pass = 0; pass < 2; pass++) {
		glNewList(pass == 0 ? av->list_face : av->list_edge, GL_COMPILE);
		glBegin(GL_QUADS);
		rewind(fp);
		edge = false;
		while (fgets(line, sizeof(line), fp)) {
			if (STRPREFIX(line, "usemtl")) {
				edge = strstr(line, "edge") != NULL;
			}
			else if (line[0] == 'f' && line[1] == ' ' && edge == (pass == 1)) {
				int vi[4] = {0, 0, 0, 0}, ti[4] = {0, 0, 0, 0}, k;
				if (pass == 0) {
					sscanf(line + 2, "%d/%d %d/%d %d/%d %d/%d", &vi[0], &ti[0], &vi[1], &ti[1], &vi[2], &ti[2], &vi[3],
					       &ti[3]);
				}
				else {
					sscanf(line + 2, "%d %d %d %d", &vi[0], &vi[1], &vi[2], &vi[3]);
				}
				for (k = 0; k < 4; k++) {
					if (vi[k] < 1 || vi[k] > totv) {
						break;
					}
				}
				if (k < 4) {
					continue;
				}
				if (pass == 1) {
					/* a flat normal for the walls' shading */
					float n[3];
					normal_quad_v3(n, v[vi[0] - 1], v[vi[1] - 1], v[vi[2] - 1], v[vi[3] - 1]);
					glNormal3fv(n);
				}
				for (k = 0; k < 4; k++) {
					if (pass == 0 && ti[k] >= 1 && ti[k] <= totvt) {
						glTexCoord2fv(vt[ti[k] - 1]);
					}
					glVertex3fv(v[vi[k] - 1]);
				}
			}
		}
		glEnd();
		glEndList();
	}
	fclose(fp);
	MEM_SAFE_FREE(v);
	MEM_SAFE_FREE(vt);

	BLI_snprintf(file, sizeof(file), "%s_tex.png", name);
	BLI_join_dirfile(path, sizeof(path), dir, file);
	ibuf = IMB_loadiffname(path, IB_rect, NULL);
	if (ibuf && ibuf->rect) {
		glGenTextures(1, &av->tex);
		glBindTexture(GL_TEXTURE_2D, av->tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ibuf->x, ibuf->y, 0, GL_RGBA, GL_UNSIGNED_BYTE, ibuf->rect);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	if (ibuf) {
		IMB_freeImBuf(ibuf);
	}
	av->ok = true;
	return true;
}

static const GVAvatar *gv_avatar(const char *name)
{
	GVAvatar *av;
	if (!gv_avatars) {
		gv_avatars = BLI_ghash_str_new(__func__);
	}
	av = BLI_ghash_lookup(gv_avatars, name);
	if (!av) {
		av = MEM_callocN(sizeof(GVAvatar), __func__);
		gv_avatar_load(name, av);
		BLI_ghash_insert(gv_avatars, BLI_strdup(name), av);
	}
	if (!av->ok && !STREQ(name, "QUESTION")) {
		return gv_avatar("QUESTION");
	}
	return av->ok ? av : NULL;
}

/* ------------------------------------------------------------------------- */
/* the scene's objects by name (owners), rebuilt every draw: objects come and go */

static GHash *gv_owner_map(Main *bmain)
{
	GHash *map = BLI_ghash_str_new(__func__);
	Object *ob;
	for (ob = bmain->object.first; ob; ob = ob->id.next) {
		/* linked objects can share a name: the first wins */
		BLI_ghash_reinsert(map, ob->id.name + 2, ob, NULL, NULL);
	}
	return map;
}

typedef struct GVView {
	ARegion *ar;
	RegionView3D *rv3d;
	View3D *v3d;
	GHash *owners;
	const GVModel *m;
	float right[3], up[3], back[3];
	int layers;
	int selected;
	double time;
} GVView;

static void gv_view_init(GVView *gv, const bContext *C, ARegion *ar)
{
	memset(gv, 0, sizeof(*gv));
	gv->ar = ar;
	gv->rv3d = ar->regiondata;
	gv->v3d = CTX_wm_view3d(C);
	gv->m = BL_GameViewModel();
	gv->owners = gv_owner_map(CTX_data_main(C));
	normalize_v3_v3(gv->right, gv->rv3d->viewinv[0]);
	normalize_v3_v3(gv->up, gv->rv3d->viewinv[1]);
	normalize_v3_v3(gv->back, gv->rv3d->viewinv[2]);
	gv->layers = gv->v3d ? gv->v3d->gameview_layers : GV_LAYERS_ALL;
	gv->selected = BL_GameViewSelected();
	gv->time = PIL_check_seconds_timer();
}

static void gv_view_free(GVView *gv)
{
	if (gv->owners) {
		BLI_ghash_free(gv->owners, NULL, NULL);
	}
}

static Object *gv_owner(const GVView *gv, const GVElement *e)
{
	return e->owner[0] ? BLI_ghash_lookup(gv->owners, e->owner) : NULL;
}

/* where an element's avatar is now; false: its owner is gone */
static bool gv_element_pos(const GVView *gv, const GVElement *e, float r[3])
{
	Object *ob = gv_owner(gv, e);
	if (e->owner[0] && !ob) {
		return false;
	}
	if (!ob) {
		copy_v3_v3(r, e->pos);
		return true;
	}
	copy_v3_v3(r, ob->obmat[3]);
	if (e->flag & GV_EL_VIEW_ALIGNED) {
		madd_v3_v3fl(r, gv->right, e->offset[0]);
		r[2] += e->offset[2];
	}
	else {
		add_v3_v3(r, e->offset);
	}
	return true;
}

/* where the element's object is (its origin; the avatar for scene-level ones) */
static bool gv_element_origin(const GVView *gv, const GVElement *e, float r[3])
{
	Object *ob = gv_owner(gv, e);
	if (e->owner[0] && !ob) {
		return false;
	}
	copy_v3_v3(r, ob ? ob->obmat[3] : e->pos);
	return true;
}

static bool gv_layer_shown(const GVView *gv, int layer)
{
	return (gv->layers & (1 << layer)) != 0;
}

static float gv_avatar_size(const GVElement *e)
{
	return (e->flag & GV_EL_SMALL) ? 0.24f : 0.48f;
}

/* ------------------------------------------------------------------------- */
/* colours: Ayhan's states */

static void gv_state_colour(int state, float r[3])
{
	static const float colours[][3] = {
		{0.92f, 0.16f, 0.14f},  /* Red: unavailable */
		{0.06f, 0.06f, 0.07f},  /* Black: available, not set up */
		{0.95f, 0.95f, 0.95f},  /* White: ready */
		{0.18f, 0.85f, 0.32f},  /* Green: active */
		{0.25f, 0.52f, 1.00f},  /* Blue: start / continue (Run) */
		{1.00f, 0.84f, 0.16f},  /* Yellow: wait / stop (Run) */
	};
	copy_v3_v3(r, colours[CLAMPIS(state, 0, 5)]);
}

/* lines in a state's colour: black lines get a light core to be seen on dark */
static void gv_line_colour(int state, float alpha)
{
	float c[3];
	gv_state_colour(state, c);
	if (state == GV_STATE_BLACK) {
		c[0] = c[1] = c[2] = 0.32f;
	}
	glColor4f(c[0], c[1], c[2], alpha);
}

/* ------------------------------------------------------------------------- */
/* shapes */

static void gv_circle(const float centre[3], const float ax[3], const float ay[3], float radius, int segments)
{
	int i;
	glBegin(GL_LINE_LOOP);
	for (i = 0; i < segments; i++) {
		const float a = (float)(2.0 * M_PI * i / segments);
		float co[3];
		copy_v3_v3(co, centre);
		madd_v3_v3fl(co, ax, cosf(a) * radius);
		madd_v3_v3fl(co, ay, sinf(a) * radius);
		glVertex3fv(co);
	}
	glEnd();
}

static void gv_arc_line(const float a[3], const float b[3], bool arc)
{
	glBegin(GL_LINE_STRIP);
	if (!arc) {
		glVertex3fv(a);
		glVertex3fv(b);
	}
	else {
		const float h = 0.25f * len_v3v3(a, b) + 0.3f;
		int i;
		for (i = 0; i <= 16; i++) {
			const float t = i / 16.0f;
			float co[3];
			interp_v3_v3v3(co, a, b, t);
			co[2] += h * 4.0f * t * (1.0f - t);
			glVertex3fv(co);
		}
	}
	glEnd();
}

/* the owner's axes without its scale */
static void gv_owner_axes(Object *ob, float r_x[3], float r_y[3], float r_z[3])
{
	if (ob) {
		normalize_v3_v3(r_x, ob->obmat[0]);
		normalize_v3_v3(r_y, ob->obmat[1]);
		normalize_v3_v3(r_z, ob->obmat[2]);
	}
	else {
		const float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, z[3] = {0, 0, 1};
		copy_v3_v3(r_x, x);
		copy_v3_v3(r_y, y);
		copy_v3_v3(r_z, z);
	}
}

static void gv_draw_shape(const GVView *gv, const GVShape *s, float alpha)
{
	const GVModel *m = gv->m;
	const GVElement *ea = (s->a >= 0 && s->a < m->element_count) ? &m->elements[s->a] : NULL;
	const GVElement *eb = (s->b >= 0 && s->b < m->element_count) ? &m->elements[s->b] : NULL;
	Object *ob = ea ? gv_owner(gv, ea) : NULL;
	float o[3], ax[3], ay[3], az[3];
	const float wz[3] = {0, 0, 1}, wx[3] = {1, 0, 0}, wy[3] = {0, 1, 0};
	int i;

	if (s->type != GV_SHAPE_POINTS && s->type != GV_SHAPE_TRIS && s->type != GV_SHAPE_RECT) {
		if (!ea || !gv_element_origin(gv, ea, o)) {
			return;
		}
	}
	gv_owner_axes(ob, ax, ay, az);
	gv_line_colour(s->state, alpha);
	if (s->dashed) {
		glEnable(GL_LINE_STIPPLE);
		glLineStipple(2, 0x00FF);
	}

	switch (s->type) {
		case GV_SHAPE_LINE:
		case GV_SHAPE_ARC: {
			float a[3], b[3];
			bool oka, okb;
			if (!eb) {
				break;
			}
			if (s->p[6] != 0.0f) {
				oka = gv_element_origin(gv, ea, a);
				okb = gv_element_origin(gv, eb, b);
			}
			else {
				oka = gv_element_pos(gv, ea, a);
				okb = gv_element_pos(gv, eb, b);
			}
			if (oka && okb) {
				gv_arc_line(a, b, s->type == GV_SHAPE_ARC);
			}
			break;
		}
		case GV_SHAPE_SPHERE: {
			float c[3];
			const float local[3] = {s->p[1], s->p[2], s->p[3]};
			if (ob) {
				mul_v3_m4v3(c, ob->obmat, local);
			}
			else {
				add_v3_v3v3(c, o, local);
			}
			gv_circle(c, wx, wy, s->p[0], 48);
			gv_circle(c, wx, wz, s->p[0], 48);
			gv_circle(c, wy, wz, s->p[0], 48);
			break;
		}
		case GV_SHAPE_CONE:
		case GV_SHAPE_ARROW: {
			float tip[3], dir[3];
			negate_v3_v3(dir, az);
			if (s->type == GV_SHAPE_ARROW) {
				madd_v3_v3v3fl(tip, o, dir, s->p[0]);
				glBegin(GL_LINES);
				glVertex3fv(o);
				glVertex3fv(tip);
				for (i = 0; i < 4; i++) {
					float co[3];
					const float a = (float)(M_PI_2 * i);
					madd_v3_v3v3fl(co, tip, dir, -0.25f);
					madd_v3_v3fl(co, ax, cosf(a) * 0.1f);
					madd_v3_v3fl(co, ay, sinf(a) * 0.1f);
					glVertex3fv(tip);
					glVertex3fv(co);
				}
				glEnd();
			}
			else {
				const float half = min_ff(s->p[1], DEG2RADF(89.0f));
				const float len = s->p[0] * cosf(half), rad = s->p[0] * sinf(half);
				float base[3];
				madd_v3_v3v3fl(base, o, dir, len);
				gv_circle(base, ax, ay, rad, 48);
				glBegin(GL_LINES);
				for (i = 0; i < 4; i++) {
					float co[3];
					const float a = (float)(M_PI_2 * i);
					copy_v3_v3(co, base);
					madd_v3_v3fl(co, ax, cosf(a) * rad);
					madd_v3_v3fl(co, ay, sinf(a) * rad);
					glVertex3fv(o);
					glVertex3fv(co);
				}
				glEnd();
			}
			break;
		}
		case GV_SHAPE_FRUSTUM: {
			float corners[2][4][3];
			const float d[2] = {s->p[0], s->p[1]};
			int k;
			for (k = 0; k < 2; k++) {
				const float hx = s->p[4] > 0.0f ? s->p[4] * 0.5f : d[k] * tanf(s->p[2]);
				const float hy = s->p[4] > 0.0f ? s->p[4] * 0.5f * tanf(s->p[3]) / max_ff(tanf(s->p[2]), 1e-6f) :
				                                  d[k] * tanf(s->p[3]);
				for (i = 0; i < 4; i++) {
					const float sx = (i == 0 || i == 3) ? -1.0f : 1.0f, sy = (i < 2) ? -1.0f : 1.0f;
					copy_v3_v3(corners[k][i], o);
					madd_v3_v3fl(corners[k][i], az, -d[k]);
					madd_v3_v3fl(corners[k][i], ax, sx * hx);
					madd_v3_v3fl(corners[k][i], ay, sy * hy);
				}
			}
			for (k = 0; k < 2; k++) {
				glBegin(GL_LINE_LOOP);
				for (i = 0; i < 4; i++) {
					glVertex3fv(corners[k][i]);
				}
				glEnd();
			}
			glBegin(GL_LINES);
			for (i = 0; i < 4; i++) {
				glVertex3fv(o);
				glVertex3fv(corners[1][i]);
			}
			glEnd();
			break;
		}
		case GV_SHAPE_BOX: {
			float co[8][3];
			static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4},
			                                 {0, 4}, {1, 5}, {2, 6}, {3, 7}};
			for (i = 0; i < 8; i++) {
				float local[3];
				local[0] = s->p[3] + ((i == 0 || i == 3 || i == 4 || i == 7) ? -s->p[0] : s->p[0]);
				local[1] = s->p[4] + ((i < 2 || i == 4 || i == 5) ? -s->p[1] : s->p[1]);
				local[2] = s->p[5] + (i < 4 ? -s->p[2] : s->p[2]);
				if (ob) {
					mul_v3_m4v3(co[i], ob->obmat, local);
				}
				else {
					add_v3_v3v3(co[i], o, local);
				}
			}
			glBegin(GL_LINES);
			for (i = 0; i < 12; i++) {
				glVertex3fv(co[edges[i][0]]);
				glVertex3fv(co[edges[i][1]]);
			}
			glEnd();
			break;
		}
		case GV_SHAPE_CAPSULE: {
			const float r = s->p[0], h = max_ff(s->p[1] * 0.5f - r, 0.0f);
			float c0[3], c1[3];
			madd_v3_v3v3fl(c0, o, az, -h);
			madd_v3_v3v3fl(c1, o, az, h);
			gv_circle(c0, ax, ay, r, 32);
			gv_circle(c1, ax, ay, r, 32);
			glBegin(GL_LINES);
			for (i = 0; i < 4; i++) {
				float a0[3], a1[3];
				const float a = (float)(M_PI_2 * i);
				copy_v3_v3(a0, c0);
				madd_v3_v3fl(a0, ax, cosf(a) * r);
				madd_v3_v3fl(a0, ay, sinf(a) * r);
				add_v3_v3v3(a1, a0, c1);
				sub_v3_v3(a1, c0);
				glVertex3fv(a0);
				glVertex3fv(a1);
			}
			glEnd();
			/* the round ends */
			for (i = 0; i < 2; i++) {
				const float *c = i ? c1 : c0;
				const float sign = i ? 1.0f : -1.0f;
				int k, axis;
				for (axis = 0; axis < 2; axis++) {
					const float *side = axis ? ay : ax;
					glBegin(GL_LINE_STRIP);
					for (k = 0; k <= 16; k++) {
						const float a = (float)(M_PI * k / 16);
						float co[3];
						copy_v3_v3(co, c);
						madd_v3_v3fl(co, side, cosf(a) * r);
						madd_v3_v3fl(co, az, sign * sinf(a) * r);
						glVertex3fv(co);
					}
					glEnd();
				}
			}
			break;
		}
		case GV_SHAPE_RING:
			gv_circle(o, wx, wy, s->p[0], 48);
			break;
		case GV_SHAPE_POINTS:
			if (s->point_first >= 0 && s->point_count > 1) {
				glBegin(GL_LINE_STRIP);
				for (i = 0; i < s->point_count; i++) {
					glVertex3fv(m->points[s->point_first + i]);
				}
				glEnd();
			}
			break;
		case GV_SHAPE_TRIS:
			if (s->point_first >= 0 && s->point_count >= 3) {
				float c[3];
				gv_state_colour(s->state, c);
				glColor4f(c[0], c[1], c[2], 0.15f * alpha);
				glBegin(GL_TRIANGLES);
				for (i = 0; i + 2 < s->point_count; i++) {
					glVertex3fv(m->points[s->point_first + i]);
				}
				glEnd();
				gv_line_colour(s->state, alpha);
				for (i = 0; i + 2 < s->point_count; i += 3) {
					glBegin(GL_LINE_LOOP);
					glVertex3fv(m->points[s->point_first + i]);
					glVertex3fv(m->points[s->point_first + i + 1]);
					glVertex3fv(m->points[s->point_first + i + 2]);
					glEnd();
				}
			}
			break;
		case GV_SHAPE_RECT: {
			float c[3] = {s->p[0], s->p[1], s->p[2]}, col[3];
			gv_state_colour(s->state, col);
			glColor4f(col[0], col[1], col[2], 0.12f * alpha);
			glBegin(GL_QUADS);
			glVertex3f(c[0] - s->p[3], c[1] - s->p[4], c[2]);
			glVertex3f(c[0] + s->p[3], c[1] - s->p[4], c[2]);
			glVertex3f(c[0] + s->p[3], c[1] + s->p[4], c[2]);
			glVertex3f(c[0] - s->p[3], c[1] + s->p[4], c[2]);
			glEnd();
			gv_line_colour(s->state, alpha);
			glBegin(GL_LINE_LOOP);
			glVertex3f(c[0] - s->p[3], c[1] - s->p[4], c[2]);
			glVertex3f(c[0] + s->p[3], c[1] - s->p[4], c[2]);
			glVertex3f(c[0] + s->p[3], c[1] + s->p[4], c[2]);
			glVertex3f(c[0] - s->p[3], c[1] + s->p[4], c[2]);
			glEnd();
			break;
		}
		case GV_SHAPE_PANEL: {
			float c[3], col[3];
			const float sx[4] = {-1, 1, 1, -1}, sy[4] = {-1, -1, 1, 1};
			madd_v3_v3v3fl(c, o, az, -s->p[2]);
			gv_state_colour(s->state, col);
			glColor4f(col[0], col[1], col[2], 0.08f * alpha);
			glBegin(GL_QUADS);
			for (i = 0; i < 4; i++) {
				float co[3];
				copy_v3_v3(co, c);
				madd_v3_v3fl(co, ax, sx[i] * s->p[0]);
				madd_v3_v3fl(co, ay, sy[i] * s->p[1]);
				glVertex3fv(co);
			}
			glEnd();
			gv_line_colour(s->state, alpha);
			glBegin(GL_LINE_LOOP);
			for (i = 0; i < 4; i++) {
				float co[3];
				copy_v3_v3(co, c);
				madd_v3_v3fl(co, ax, sx[i] * s->p[0]);
				madd_v3_v3fl(co, ay, sy[i] * s->p[1]);
				glVertex3fv(co);
			}
			glEnd();
			break;
		}
		default:
			break;
	}
	if (s->dashed) {
		glDisable(GL_LINE_STIPPLE);
	}
}

/* ------------------------------------------------------------------------- */
/* avatars and their state rings */

static float gv_pixels(const GVView *gv, const float co[3], float size)
{
	const float px = ED_view3d_pixel_size(gv->rv3d, co);
	return px > 1e-8f ? size / px : 1e6f;
}

static bool gv_element_shown(const GVView *gv, const GVElement *e)
{
	return gv_layer_shown(gv, e->layer);
}

/* the ring around an avatar: Red dashed, Black double, White solid, Green ticks */
static void gv_ring(const GVView *gv, const float pos[3], float size, int state, float alpha)
{
	const float r = size * 0.66f;
	glLineWidth(state == GV_STATE_BLACK ? 3.0f : 2.0f);
	gv_line_colour(state, alpha);
	if (state == GV_STATE_BLACK) {
		float c[3];
		gv_state_colour(GV_STATE_BLACK, c);
		glColor4f(c[0], c[1], c[2], alpha);
		gv_circle(pos, gv->right, gv->up, r, 32);
		gv_circle(pos, gv->right, gv->up, r * 1.14f, 32);
		glLineWidth(1.0f);
		glColor4f(0.45f, 0.45f, 0.45f, alpha);
		gv_circle(pos, gv->right, gv->up, r, 32);
		gv_circle(pos, gv->right, gv->up, r * 1.14f, 32);
	}
	else if (state == GV_STATE_RED) {
		glEnable(GL_LINE_STIPPLE);
		glLineStipple(1, 0x0F0F);
		gv_circle(pos, gv->right, gv->up, r, 32);
		glDisable(GL_LINE_STIPPLE);
	}
	else if (state == GV_STATE_GREEN) {
		int i;
		gv_circle(pos, gv->right, gv->up, r, 32);
		glBegin(GL_LINES);
		for (i = 0; i < 8; i++) {
			const float a = (float)(M_PI_4 * i);
			float c0[3], c1[3];
			copy_v3_v3(c0, pos);
			madd_v3_v3fl(c0, gv->right, cosf(a) * r);
			madd_v3_v3fl(c0, gv->up, sinf(a) * r);
			copy_v3_v3(c1, pos);
			madd_v3_v3fl(c1, gv->right, cosf(a) * r * 1.18f);
			madd_v3_v3fl(c1, gv->up, sinf(a) * r * 1.18f);
			glVertex3fv(c0);
			glVertex3fv(c1);
		}
		glEnd();
	}
	else {
		gv_circle(pos, gv->right, gv->up, r, 32);
	}
	glLineWidth(1.0f);
}

/* the elements' indices sorted by avatar (rebuilt with every model) */
static int *gv_order = NULL;
static int gv_order_generation = -1;

static const char *gv_order_names = NULL;  /* for the sort */

static int gv_order_cmp(const void *a, const void *b)
{
	const GVElement *els = (const GVElement *)gv_order_names;
	const int ia = *(const int *)a, ib = *(const int *)b;
	const int c = strcmp(els[ia].avatar, els[ib].avatar);
	return c ? c : ia - ib;
}

static const int *gv_avatar_order(const GVModel *m)
{
	if (gv_order_generation != m->generation) {
		int i;
		MEM_SAFE_FREE(gv_order);
		gv_order = MEM_mallocN(sizeof(int) * max_ii(m->element_count, 1), __func__);
		for (i = 0; i < m->element_count; i++) {
			gv_order[i] = i;
		}
		gv_order_names = (const char *)m->elements;
		qsort(gv_order, m->element_count, sizeof(int), gv_order_cmp);
		gv_order_generation = m->generation;
	}
	return gv_order;
}

/* the texture bound last (avatars of a kind are drawn together) */
static GLuint gv_bound_tex = 0;

static void gv_draw_avatar(const GVView *gv, const GVElement *e, const float pos[3], float size, float alpha,
                           bool billboard)
{
	const GVAvatar *av = gv_avatar(e->avatar);
	float mat[4][4], c[3];
	if (!av) {
		return;
	}
	copy_v3_v3(mat[0], gv->right);
	copy_v3_v3(mat[1], gv->up);
	copy_v3_v3(mat[2], gv->back);
	mul_v3_fl(mat[0], size);
	mul_v3_fl(mat[1], size);
	mul_v3_fl(mat[2], size);
	copy_v3_v3(mat[3], pos);
	mat[0][3] = mat[1][3] = mat[2][3] = 0.0f;
	mat[3][3] = 1.0f;

	glPushMatrix();
	glMultMatrixf((float *)mat);
	if (!billboard) {
		gv_state_colour(e->state, c);
		if (e->state == GV_STATE_BLACK) {
			c[0] = c[1] = c[2] = 0.14f;
		}
		glColor4f(c[0], c[1], c[2], alpha);
		glCallList(av->list_edge);
	}
	if (av->tex) {
		glEnable(GL_TEXTURE_2D);
		if (gv_bound_tex != av->tex) {
			glBindTexture(GL_TEXTURE_2D, av->tex);
			gv_bound_tex = av->tex;
		}
	}
	glColor4f(1.0f, 1.0f, 1.0f, alpha);
	glCallList(av->list_face);
	if (av->tex) {
		glDisable(GL_TEXTURE_2D);
	}
	glPopMatrix();
}

/* the elements' avatars (grouped by avatar: one texture bind each), then their rings */
static void gv_draw_elements(GVView *gv, float alpha, bool *r_flashing)
{
	const GVModel *m = gv->m;
	const bool flash_on = fmod(gv->time, 1.0) < 0.5;
	int i;

	const int *order = gv_avatar_order(m);
	int k;

	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	gv_bound_tex = 0;
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.4f * alpha);
	for (k = 0; k < m->element_count; k++) {
		const GVElement *e = &m->elements[order[k]];
		float pos[3], size, px, a = alpha;
		if (!gv_element_shown(gv, e) || !gv_element_pos(gv, e, pos)) {
			continue;
		}
		size = gv_avatar_size(e);
		px = gv_pixels(gv, pos, size);
		if (px < 3.0f) {
			continue;
		}
		if (e->flag & (GV_EL_HIDDEN_LAYER | GV_EL_NOT_IN_GAME)) {
			a *= 0.4f;
		}
		gv_draw_avatar(gv, e, pos, size, a, px < 20.0f);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	gv_bound_tex = 0;
	glDisable(GL_ALPHA_TEST);

	for (i = 0; i < m->element_count; i++) {
		const GVElement *e = &m->elements[i];
		float pos[3], size, a = alpha;
		if (!gv_element_shown(gv, e) || !gv_element_pos(gv, e, pos)) {
			continue;
		}
		size = gv_avatar_size(e);
		if (gv_pixels(gv, pos, size) < 3.0f) {
			continue;
		}
		if (e->flag & (GV_EL_HIDDEN_LAYER | GV_EL_NOT_IN_GAME)) {
			a *= 0.4f;
		}
		gv_ring(gv, pos, size, e->state, a);
		if (e->flag & GV_EL_OVER_TARGET) {
			*r_flashing = true;
			if (flash_on) {
				glLineWidth(3.0f);
				glColor4f(1.0f, 0.55f, 0.08f, a);
				gv_circle(pos, gv->right, gv->up, size * 0.95f, 32);
				glLineWidth(1.0f);
			}
		}
		if (e->heat > 0.0f) {
			/* Performance: a bar of the element's share of the cost */
			float b0[3], b1[3];
			copy_v3_v3(b0, pos);
			madd_v3_v3fl(b0, gv->right, size * 0.85f);
			madd_v3_v3fl(b0, gv->up, -size * 0.5f);
			copy_v3_v3(b1, b0);
			madd_v3_v3fl(b1, gv->up, size * 1.6f * min_ff(e->heat, 1.0f));
			glLineWidth(4.0f);
			glColor4f(1.0f, 1.0f - 0.8f * min_ff(e->heat, 1.0f), 0.1f, a);
			glBegin(GL_LINES);
			glVertex3fv(b0);
			glVertex3fv(b1);
			glEnd();
			glLineWidth(1.0f);
		}
		if (i == gv->selected) {
			float c[3];
			UI_GetThemeColor3fv(TH_ACTIVE, c);
			glLineWidth(2.0f);
			glColor4f(c[0], c[1], c[2], a);
			gv_circle(pos, gv->right, gv->up, size * 0.82f, 32);
			glLineWidth(1.0f);
		}
	}
}

static void gv_draw_shapes(GVView *gv, float alpha)
{
	const GVModel *m = gv->m;
	int i;
	for (i = 0; i < m->shape_count; i++) {
		const GVShape *s = &m->shapes[i];
		if (!gv_layer_shown(gv, s->layer)) {
			continue;
		}
		gv_draw_shape(gv, s, alpha);
	}
}

static void gv_draw_labels(GVView *gv)
{
	const GVModel *m = gv->m;
	int i;
	char buf[96];

	for (i = 0; i < m->element_count; i++) {
		const GVElement *e = &m->elements[i];
		float pos[3], co[3], sco[2], size, px;
		int unknown = 0, k;
		if (!gv_element_shown(gv, e) || !gv_element_pos(gv, e, pos)) {
			continue;
		}
		size = gv_avatar_size(e);
		px = gv_pixels(gv, pos, size);
		if (i != gv->selected && ((e->flag & GV_EL_SMALL) ? px < 34.0f : px < 18.0f)) {
			continue;
		}
		copy_v3_v3(co, pos);
		madd_v3_v3fl(co, gv->up, -size * 0.8f);
		if (ED_view3d_project_float_global(gv->ar, co, sco, V3D_PROJ_TEST_CLIP_DEFAULT) != V3D_PROJ_RET_OK) {
			continue;
		}
		for (k = 0; k < e->warning_count; k++) {
			if (!m->warnings[e->warning_first + k].known) {
				unknown++;
			}
		}
		if (unknown) {
			BLI_snprintf(buf, sizeof(buf), "%s (%d)", e->name, unknown);
		}
		else {
			BLI_strncpy(buf, e->name, sizeof(buf));
		}
		{
			const float w = BLF_width_default(buf, sizeof(buf));
			const float x = sco[0] - w * 0.5f, y = sco[1] - 12.0f * U.pixelsize;
			glColor4f(0.0f, 0.0f, 0.0f, 0.85f);
			BLF_draw_default(x + 1.0f, y - 1.0f, 0.0f, buf, sizeof(buf));
			if (unknown) {
				glColor4f(1.0f, 0.62f, 0.55f, 1.0f);
			}
			else if (e->flag & (GV_EL_HIDDEN_LAYER | GV_EL_NOT_IN_GAME)) {
				glColor4f(0.7f, 0.7f, 0.7f, 0.8f);
			}
			else {
				glColor4f(0.95f, 0.97f, 1.0f, 1.0f);
			}
			BLF_draw_default(x, y, 0.0f, buf, sizeof(buf));
		}
	}
}

/* the model's summary in the view's corner */
static void gv_draw_summary(GVView *gv)
{
	const GVModel *m = gv->m;
	char buf[160];
	int total = 0, i;
	rcti rect;
	for (i = 0; i < GV_LAYER_COUNT; i++) {
		total += m->warnings_per_layer[i];
	}
	ED_region_visible_rect(gv->ar, &rect);
	BLI_snprintf(buf, sizeof(buf), "Game View: %s, %d elements, %d warnings%s%s", m->scene, m->element_count, total,
	             m->converted ? "" : " (not a game scene: not converted)",
	             BL_GameViewIsDirty() ? " - out of date" : "");
	glColor4f(0.0f, 0.0f, 0.0f, 0.85f);
	BLF_draw_default(rect.xmin + 11.0f * U.pixelsize, rect.ymin + 9.0f * U.pixelsize, 0.0f, buf, sizeof(buf));
	glColor4f(0.95f, 0.97f, 1.0f, 1.0f);
	BLF_draw_default(rect.xmin + 10.0f * U.pixelsize, rect.ymin + 10.0f * U.pixelsize, 0.0f, buf, sizeof(buf));
}

/* after the scene (or RTX Remix's picture), before the view's info */
void view3d_gameview_draw(const bContext *C, Scene *scene, ARegion *ar, View3D *v3d)
{
	RegionView3D *rv3d = ar->regiondata;
	GVView gv;
	bool flashing = false;

	gv_view_init(&gv, C, ar);
	if (!gv.m) {
		gv_view_free(&gv);
		return;
	}

	glPushAttrib(GL_ALL_ATTRIB_BITS);

	/* dim the scene under the holograms */
	if (v3d->gameview_flag & V3D_GAMEVIEW_DIM) {
		ED_region_pixelspace(ar);
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(0.0f, 0.02f, 0.05f, 0.45f);
		glRecti(0, 0, ar->winx, ar->winy);
	}

	/* the scene's depth: the holograms are hidden behind walls. RTX Remix's
	 * picture has none: the scene's shapes are drawn into the depth alone. */
	if (v3d->drawtype == OB_RENDER || v3d->fx_settings.fx_flag) {  /* post effects draw off screen */
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		ED_view3d_draw_depth(scene, ar, v3d, false);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	}

	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf((float *)rv3d->winmat);
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf((float *)rv3d->viewmat);

	glDisable(GL_LIGHTING);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_LINE_SMOOTH);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	/* hidden behind walls: tested against the scene's depth, writing none
	 * (the holograms are see-through) */
	glDepthFunc(GL_LEQUAL);
	gv_draw_shapes(&gv, 0.9f);
	gv_draw_elements(&gv, 0.92f, &flashing);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_LINE_SMOOTH);

	ED_region_pixelspace(ar);
	if (v3d->gameview_flag & V3D_GAMEVIEW_LABELS) {
		gv_draw_labels(&gv);
	}
	gv_draw_summary(&gv);

	glPopAttrib();
	gv_view_free(&gv);

	/* flashing outlines (over a target) need redraws */
	gv_flashing = flashing;
	if (flashing) {
		gv_timer_set(CTX_wm_manager(C), CTX_wm_window(C), true);
	}
}

/* ------------------------------------------------------------------------- */
/* picking: the element under the mouse (-1: none) */

int view3d_gameview_pick(bContext *C, ARegion *ar, const int mval[2])
{
	GVView gv;
	int i, best = -1;
	float best_d = FLT_MAX;

	gv_view_init(&gv, C, ar);
	if (!gv.m) {
		gv_view_free(&gv);
		return -1;
	}
	for (i = 0; i < gv.m->element_count; i++) {
		const GVElement *e = &gv.m->elements[i];
		float pos[3], sco[2], size, px, d;
		if (!gv_element_shown(&gv, e) || !gv_element_pos(&gv, e, pos)) {
			continue;
		}
		size = gv_avatar_size(e);
		px = gv_pixels(&gv, pos, size);
		if (px < 3.0f) {
			continue;
		}
		if (ED_view3d_project_float_global(ar, pos, sco, V3D_PROJ_TEST_CLIP_DEFAULT) != V3D_PROJ_RET_OK) {
			continue;
		}
		d = len_v2v2(sco, (float[2]){(float)mval[0], (float)mval[1]});
		if (d <= max_ff(px * 0.6f, 6.0f) && d < best_d) {
			best_d = d;
			best = i;
		}
	}
	gv_view_free(&gv);
	return best;
}

/* ------------------------------------------------------------------------- */
/* operators */

static int gameview_poll(bContext *C)
{
	View3D *v3d = CTX_wm_view3d(C);
	return v3d && (v3d->gameview_flag & V3D_GAMEVIEW) && CTX_wm_region_view3d(C);
}

static int gameview_select_invoke(bContext *C, wmOperator *UNUSED(op), const wmEvent *event)
{
	ARegion *ar = CTX_wm_region(C);
	const int el = view3d_gameview_pick(C, ar, event->mval);
	if (el < 0) {
		/* nothing here: the click goes on to Blender's own selection */
		BL_GameViewSetSelected(-1);
		ED_area_tag_redraw(CTX_wm_area(C));
		return OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH;
	}
	BL_GameViewSetSelected(el);
	ED_area_tag_redraw(CTX_wm_area(C));
	return OPERATOR_FINISHED;
}

static int gameview_select_exec(bContext *C, wmOperator *op)
{
	BL_GameViewSetSelected(RNA_int_get(op->ptr, "element"));
	ED_area_tag_redraw(CTX_wm_area(C));
	return OPERATOR_FINISHED;
}

void VIEW3D_OT_gameview_select(wmOperatorType *ot)
{
	ot->name = "Select Game View Element";
	ot->description = "Select an element of the Game View (by clicking its avatar, or by its index)";
	ot->idname = "VIEW3D_OT_gameview_select";
	ot->invoke = gameview_select_invoke;
	ot->exec = gameview_select_exec;
	ot->poll = gameview_poll;
	ot->flag = OPTYPE_REGISTER;
	RNA_def_int(ot->srna, "element", -1, -1, INT_MAX, "Element", "Index of the element (-1: none)", -1, INT_MAX);
}

static int gameview_frame_exec(bContext *C, wmOperator *op)
{
	View3D *v3d = CTX_wm_view3d(C);
	ARegion *ar = CTX_wm_region(C);
	RegionView3D *rv3d = CTX_wm_region_view3d(C);
	const int el = RNA_int_get(op->ptr, "element");
	const int smooth_viewtx = WM_operator_smooth_viewtx_get(op);
	GVView gv;
	float pos[3], ofs[3], dist;
	bool ok = false;

	if (!rv3d) {
		return OPERATOR_CANCELLED;
	}
	gv_view_init(&gv, C, ar);
	if (gv.m && el >= 0 && el < gv.m->element_count) {
		ok = gv_element_pos(&gv, &gv.m->elements[el], pos);
	}
	gv_view_free(&gv);
	if (!ok) {
		return OPERATOR_CANCELLED;
	}
	BL_GameViewSetSelected(el);
	negate_v3_v3(ofs, pos);
	dist = 4.0f;
	if (rv3d->persp == RV3D_CAMOB) {
		rv3d->persp = RV3D_PERSP;
	}
	ED_view3d_smooth_view(C, v3d, ar, smooth_viewtx,
	                      &(const V3D_SmoothParams) {.ofs = ofs, .dist = &dist});
	ED_area_tag_redraw(CTX_wm_area(C));
	return OPERATOR_FINISHED;
}

void VIEW3D_OT_gameview_frame(wmOperatorType *ot)
{
	ot->name = "Frame Game View Element";
	ot->description = "Select an element of the Game View and bring it to the middle of the view";
	ot->idname = "VIEW3D_OT_gameview_frame";
	ot->exec = gameview_frame_exec;
	ot->poll = gameview_poll;
	RNA_def_int(ot->srna, "element", -1, -1, INT_MAX, "Element", "Index of the element", -1, INT_MAX);
}

static int gameview_refresh_exec(bContext *C, wmOperator *UNUSED(op))
{
	ARegion *ar = CTX_wm_region(C);
	view3d_operator_needs_opengl(C);
	gv_build(C, ar);
	ED_area_tag_redraw(CTX_wm_area(C));
	return OPERATOR_FINISHED;
}

void VIEW3D_OT_gameview_refresh(wmOperatorType *ot)
{
	ot->name = "Refresh Game View";
	ot->description = "Build the Game View's model again now (the game's converter, then every layer's checks)";
	ot->idname = "VIEW3D_OT_gameview_refresh";
	ot->exec = gameview_refresh_exec;
	ot->poll = gameview_poll;
}

void view3d_gameview_keymap(wmKeyConfig *keyconf)
{
	wmKeyMap *keymap = WM_keymap_find(keyconf, "Game View", SPACE_VIEW3D, 0);
	WM_keymap_add_item(keymap, "VIEW3D_OT_gameview_select", SELECTMOUSE, KM_PRESS, 0, 0);
	WM_keymap_add_item(keymap, "VIEW3D_OT_gameview_refresh", F5KEY, KM_PRESS, 0, 0);
}

#else  /* WITH_GAMEENGINE */

void view3d_gameview_tag_dirty(void) {}
void view3d_gameview_file_loaded(void) {}
void view3d_gameview_update(const bContext *UNUSED(C), ARegion *UNUSED(ar)) {}
void view3d_gameview_region_exit(wmWindowManager *UNUSED(wm)) {}
void view3d_gameview_draw(const bContext *UNUSED(C), Scene *UNUSED(scene), ARegion *UNUSED(ar),
                          View3D *UNUSED(v3d)) {}
void view3d_gameview_keymap(wmKeyConfig *UNUSED(keyconf)) {}

#endif  /* WITH_GAMEENGINE */
