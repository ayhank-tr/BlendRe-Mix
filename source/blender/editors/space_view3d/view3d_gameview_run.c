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

/** \file blender/editors/space_view3d/view3d_gameview_run.c
 *  \ingroup spview3d
 *
 * BGE-Remix P10.9: the Remix View's Run mode, its two C operators (Python's
 * gameview.run, gameview.run_continue and gameview.run_stop drive them):
 *
 * - view3d.gameview_play: the game plays inside the Remix View's own area
 *   (2.79's embedded loop, which blocks Blender until it ends), the whole
 *   scene or a mock-up, until Suspend, Stop or the game's own end;
 *   BL_GameViewRun.h hooks the loop;
 * - view3d.gameview_file_state: the file as it was when Run started, kept
 *   in memory (as an undo step is) and put back by Stop, then No.
 */

#include <stdlib.h>
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
#include "BLI_listbase.h"
#include "BLI_rect.h"
#include "BLI_string.h"
#include "BLI_callbacks.h"

#include "BKE_blendfile.h"
#include "BKE_context.h"
#include "BKE_depsgraph.h"
#include "BKE_global.h"
#include "BKE_main.h"
#include "BKE_report.h"
#include "BKE_scene.h"
#include "BKE_screen.h"

#include "BIF_gl.h"

#include "GPU_draw.h"

#include "RNA_access.h"
#include "RNA_define.h"

#include "WM_api.h"
#include "WM_types.h"

#include "ED_render.h"
#include "ED_screen.h"
#include "ED_util.h"
#include "ED_view3d.h"

#include "view3d_intern.h"  /* own include */

#ifdef WITH_GAMEENGINE

#include "BL_GameView.h"
#include "BL_GameViewRun.h"
#include "BL_System.h"

/* the embedded game's settings from the scene (view3d_view.c's
 * game_set_commmandline_options, which is static there) */
static void gv_run_commandline_options(GameData *gm)
{
	SYS_SystemHandle syshandle = SYS_GetSystem();
	int test;
	if (!syshandle) {
		return;
	}
	test = (U.gameflags & USER_DISABLE_MIPMAP);
	GPU_set_mipmap(!test);
	SYS_WriteCommandLineInt(syshandle, "nomipmap", test);
	test = (gm->flag & GAME_SHOW_FRAMERATE);
	SYS_WriteCommandLineInt(syshandle, "show_framerate", test);
	SYS_WriteCommandLineInt(syshandle, "show_profile", test);
	test = (gm->flag & GAME_SHOW_DEBUG_PROPS);
	SYS_WriteCommandLineInt(syshandle, "show_properties", test);
	test = (gm->flag & GAME_SHOW_PHYSICS);
	SYS_WriteCommandLineInt(syshandle, "show_physics", test);
	test = (gm->flag & GAME_ENABLE_ALL_FRAMES);
	SYS_WriteCommandLineInt(syshandle, "fixedtime", test);
	test = (gm->flag & GAME_ENABLE_ANIMATION_RECORD);
	SYS_WriteCommandLineInt(syshandle, "animation_record", test);
	test = (gm->flag & GAME_IGNORE_DEPRECATION_WARNINGS);
	SYS_WriteCommandLineInt(syshandle, "ignore_deprecation_warnings", test);
	test = (gm->matmode == GAME_MAT_MULTITEX);
	SYS_WriteCommandLineInt(syshandle, "blender_material", test);
	test = (gm->matmode == GAME_MAT_GLSL);
	SYS_WriteCommandLineInt(syshandle, "blender_glsl_material", test);
	test = (gm->flag & GAME_DISPLAY_LISTS);
	SYS_WriteCommandLineInt(syshandle, "displaylists", test);
}

static bool gv_run_scene_ok(const Scene *scene)
{
	return STREQ(scene->r.engine, "BLENDER_GAME") || STREQ(scene->r.engine, "RTX_REMIX");
}

static int gameview_run_poll(bContext *C)
{
	View3D *v3d = CTX_wm_view3d(C);
	return v3d && (v3d->gameview_flag & V3D_GAMEVIEW) && CTX_wm_window(C) && CTX_wm_screen(C);
}

static int gameview_play_exec(bContext *C, wmOperator *op)
{
	Scene *startscene = CTX_data_scene(C);
	Main *bmain = CTX_data_main(C);
	ScrArea *prevsa = CTX_wm_area(C);
	ARegion *prevar = CTX_wm_region(C), *ar;
	wmWindow *prevwin = CTX_wm_window(C);
	View3D *v3d = CTX_wm_view3d(C);
	RegionView3D *rv3d;
	GVRunOptions opts;
	ListBase queue_back;
	rcti cam_frame;
	char *keep, *shot;
	int outcome;

	if (!gv_run_scene_ok(startscene)) {
		BKE_report(op->reports, RPT_ERROR, "Run plays game scenes: set the scene's engine to Blender Game");
		return OPERATOR_CANCELLED;
	}
	if (CTX_data_mode_enum(C) != CTX_MODE_OBJECT) {
		BKE_report(op->reports, RPT_ERROR, "Run needs Object Mode");
		return OPERATOR_CANCELLED;
	}
	/* the Remix View's own area: its main region (the button is in the header) */
	ar = prevsa ? BKE_area_find_region_type(prevsa, RGN_TYPE_WINDOW) : NULL;
	if (!ar || !ar->regiondata) {
		BKE_report(op->reports, RPT_ERROR, "Run plays inside a Remix View");
		return OPERATOR_CANCELLED;
	}
	CTX_wm_region_set(C, ar);
	rv3d = ar->regiondata;

	/* the model the live data is made for, up to date */
	view3d_operator_needs_opengl(C);
	if (!BL_GameViewModel() || BL_GameViewIsDirty()) {
		view3d_gameview_build(C, ar);
	}

	/* redraw to hide any menus and pop-ups: Blender's own loop waits until
	 * the game ends. BGE-Remix P10.9 fix: a Python context override (a script
	 * passing its own "area") would make every area's drawing see that area,
	 * and a Properties header then finds no Properties data and crashes: the
	 * override is set aside for the redraw */
	{
		void *py_dict = CTX_py_dict_get(C);
		CTX_py_dict_set(C, NULL);
		WM_redraw_windows(C);
		CTX_py_dict_set(C, py_dict);
	}

	BLI_callback_exec(bmain, &startscene->id, BLI_CB_EVT_GAME_PRE);
	gv_run_commandline_options(&startscene->gm);

	if ((rv3d->persp == RV3D_CAMOB) && (startscene->gm.framing.type == SCE_GAMEFRAMING_BARS) &&
	    (startscene->gm.stereoflag != STEREO_DOME))
	{
		rctf cam_framef;
		ED_view3d_calc_camera_border(startscene, ar, v3d, rv3d, &cam_framef, false);
		cam_frame.xmin = cam_framef.xmin + ar->winrct.xmin;
		cam_frame.xmax = cam_framef.xmax + ar->winrct.xmin;
		cam_frame.ymin = cam_framef.ymin + ar->winrct.ymin;
		cam_frame.ymax = cam_framef.ymax + ar->winrct.ymin;
		BLI_rcti_isect(&ar->winrct, &cam_frame, &cam_frame);
	}
	else {
		cam_frame = ar->winrct;
	}

	keep = RNA_string_get_alloc(op->ptr, "keep", NULL, 0);
	shot = RNA_string_get_alloc(op->ptr, "shot", NULL, 0);
	memset(&opts, 0, sizeof(opts));
	opts.resume = RNA_boolean_get(op->ptr, "resume");
	opts.mockup = RNA_boolean_get(op->ptr, "mockup");
	opts.suspend_after = RNA_int_get(op->ptr, "suspend_after");
	opts.suspend_seconds = RNA_float_get(op->ptr, "suspend_seconds");
	opts.from_remix = RNA_boolean_get(op->ptr, "from_remix");
	opts.frame[0] = cam_frame.xmin;
	opts.frame[1] = cam_frame.ymin;
	opts.frame[2] = cam_frame.xmax;
	opts.frame[3] = cam_frame.ymax;
	opts.keep = keep;
	opts.shot = (shot && shot[0]) ? shot : NULL;
	opts.overlay = view3d_gameview_draw_live;

	/* as view3d.game_start: the editor's OpenGL state and the window's
	 * events are put aside while the game runs */
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	queue_back = prevwin->queue;
	BLI_listbase_clear(&prevwin->queue);

	BL_GameViewRunBegin(C, ar, &opts);
	StartKetsjiShell(C, ar, &cam_frame, 1);
	outcome = BL_GameViewRunFinish();

	MEM_SAFE_FREE(keep);
	MEM_SAFE_FREE(shot);

	/* the window was closed while the game ran */
	if (BLI_findindex(&CTX_wm_manager(C)->windows, prevwin) == -1) {
		prevwin = NULL;
		CTX_wm_window_set(C, NULL);
	}
	if (prevwin) {
		CTX_wm_window_set(C, prevwin);
		CTX_wm_area_set(C, prevsa);
		CTX_wm_region_set(C, prevar);
		prevwin->queue = queue_back;
		ED_area_tag_redraw(prevsa);
	}
	GPU_state_init();
	GPU_set_tpage(NULL, 0, 0);
	glPopAttrib();

	BKE_scene_set_background(CTX_data_main(C), startscene);
	BLI_callback_exec(bmain, &startscene->id, BLI_CB_EVT_GAME_POST);

	if (outcome == GV_RUN_OUT_ERROR) {
		BKE_report(op->reports, RPT_ERROR, "The game could not start");
		return OPERATOR_CANCELLED;
	}
	return OPERATOR_FINISHED;
}

void VIEW3D_OT_gameview_play(wmOperatorType *ot)
{
	PropertyRNA *prop;

	ot->name = "Play in the Remix View";
	ot->description = "Play the game inside the Remix View until it is suspended or stopped "
	                  "(Run mode's Run, Continue and Stop use it)";
	ot->idname = "VIEW3D_OT_gameview_play";
	ot->exec = gameview_play_exec;
	ot->poll = gameview_run_poll;
	ot->flag = OPTYPE_INTERNAL;

	RNA_def_boolean(ot->srna, "resume", false, "Resume", "Put the suspended game's state back");
	RNA_def_boolean(ot->srna, "mockup", false, "Mock-up", "Play only the objects in Keep");
	RNA_def_string(ot->srna, "keep", NULL, 0, "Keep", "Mock-up: the objects to play, one name per line");
	RNA_def_int(ot->srna, "suspend_after", 0, 0, INT_MAX, "Suspend After",
	            "Suspend by itself after this many frames (0: only when asked)", 0, 100000);
	RNA_def_float(ot->srna, "suspend_seconds", 0.0f, 0.0f, FLT_MAX, "Suspend After Seconds",
	              "Suspend by itself after this many seconds (0: only when asked)", 0.0f, 3600.0f);
	RNA_def_boolean(ot->srna, "from_remix", false, "From RTX Remix's First Frame",
	                "Count the seconds from the first frame RTX Remix drew (its start takes about half a minute)");
	prop = RNA_def_string(ot->srna, "shot", NULL, 0, "Picture", "A picture of the last frame (PNG file)");
	RNA_def_property_flag(prop, PROP_SKIP_SAVE);
}

/* ------------------------------------------------------------------------- */
/* the file as it was when Run started */

static EnumPropertyItem file_state_items[] = {
	{0, "SAVE", 0, "Save", "Keep the file as it is now"},
	{1, "RESTORE", 0, "Restore", "Put the kept file back"},
	{2, "FREE", 0, "Free", "Drop the kept file"},
	{0, NULL, 0, NULL, NULL}
};

static int gameview_file_state_exec(bContext *C, wmOperator *op)
{
	const int action = RNA_enum_get(op->ptr, "action");
	wmWindowManager *wm = CTX_wm_manager(C);
	Main *bmain = CTX_data_main(C);
	char mainstr[sizeof(G.main->name)];
	int fileflags;
	bool ok;

	if (action == 0) {
		BL_GameViewRunFileSave(bmain);
		if (!BL_GameViewRunFile()) {
			BKE_report(op->reports, RPT_ERROR, "The file could not be kept");
			return OPERATOR_CANCELLED;
		}
		return OPERATOR_FINISHED;
	}
	if (action == 2) {
		BL_GameViewRunFileFree();
		return OPERATOR_FINISHED;
	}
	if (!BL_GameViewRunFile()) {
		BKE_report(op->reports, RPT_ERROR, "No file was kept when Run started");
		return OPERATOR_CANCELLED;
	}
	if (CTX_data_edit_object(C)) {
		BKE_report(op->reports, RPT_ERROR, "Leave Edit Mode first");
		return OPERATOR_CANCELLED;
	}
	/* as a global undo step does (ed_undo_step, read_undosave) */
	ED_viewport_render_kill_jobs(wm, bmain, true);
	if (WM_jobs_test(wm, CTX_data_scene(C), WM_JOB_TYPE_ANY)) {
		BKE_report(op->reports, RPT_ERROR, "Wait for the running jobs to finish first");
		return OPERATOR_CANCELLED;
	}
	BLI_strncpy(mainstr, G.main->name, sizeof(mainstr));
	fileflags = G.fileflags;
	G.fileflags |= G_FILE_NO_UI;
	ok = BKE_blendfile_read_from_memfile(C, BL_GameViewRunFile(), op->reports, 0);
	BLI_strncpy(G.main->name, mainstr, sizeof(G.main->name));
	G.fileflags = fileflags;
	if (!ok) {
		BKE_report(op->reports, RPT_ERROR, "The kept file could not be read back");
		return OPERATOR_CANCELLED;
	}
	DAG_on_visible_update(G.main, false);
	BL_GameViewRunFileFree();
	view3d_gameview_file_loaded();
	WM_event_add_notifier(C, NC_SCENE | ND_LAYER_CONTENT, CTX_data_scene(C));
	WM_event_add_notifier(C, NC_WINDOW, NULL);
	ED_undo_push(C, "Remix View: the file as it was when Run started");
	return OPERATOR_FINISHED;
}

void VIEW3D_OT_gameview_file_state(wmOperatorType *ot)
{
	ot->name = "Remix View Run File";
	ot->description = "Keep the file as it is when Run starts, put it back (Stop, then No) or drop it";
	ot->idname = "VIEW3D_OT_gameview_file_state";
	ot->exec = gameview_file_state_exec;
	ot->flag = OPTYPE_INTERNAL;
	RNA_def_enum(ot->srna, "action", file_state_items, 0, "Action", "");
}

#endif  /* WITH_GAMEENGINE */
