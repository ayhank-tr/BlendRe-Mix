# BGE-Remix p10c probe: the Remix View's logic layer (P10.11) in Blender's
# UI (Python 3.5), then quits.
#   blender -p 0 0 1600 900 p10c_states_view.blend --python p10c_probe.py
#
# Steps: the model built (Check mode) and read: the logic layer's elements
# (bricks and state groups), its shapes (links, membership lines, state
# arrows), the messages layer's arcs, the warnings; a screenshot. Then the
# Brain's starting states set through RNA as the Starting States panel does
# (state 3 added), the model rebuilt and read again, and the file saved as
# BGE_REMIX_P10C_SAVED.
# With BGE_REMIX_P10C_ONLY_BUILD=1 (P10.13), only the model built and read.
# Results: BGE_REMIX_P10C_OUT (JSON).

import json
import os
import traceback

import bpy

try:
    import _bge_gameview as gv
except ImportError:
    gv = None

OUT = os.environ.get("BGE_REMIX_P10C_OUT", "p10c_probe.json")
DIR = os.environ.get("BGE_REMIX_P10C_DIR", os.path.dirname(os.path.abspath(OUT)))
SAVED = os.environ.get("BGE_REMIX_P10C_SAVED", os.path.join(DIR, "p10c_states_saved.blend"))
NEW_START = [int(s) for s in os.environ.get("BGE_REMIX_P10C_NEW_START", "1,3").split(",")]
OBJECT = os.environ.get("BGE_REMIX_P10C_OBJECT", "Brain")

result = {"steps": {}, "errors": [], "finished": False}
QUEUE = []


def window():
    return bpy.context.window_manager.windows[0]


def main_area():
    areas = [a for a in window().screen.areas if a.type in ('GAME_VIEW', 'VIEW_3D')]
    areas.sort(key=lambda a: (a.type != 'GAME_VIEW', -a.width * a.height))
    return areas[0] if areas else None


def region():
    return [r for r in main_area().regions if r.type == 'WINDOW'][0]


def override():
    win = window()
    return {"window": win, "screen": win.screen, "area": main_area(), "region": region(),
            "scene": bpy.context.scene}


def redraw():
    for a in window().screen.areas:
        a.tag_redraw()


def screenshot(name):
    """the whole window (the sheet crops nothing: the Remix View fills most of it)"""
    path = os.path.join(DIR, name + ".png")
    try:
        win = window()
        bpy.ops.screen.screenshot({"window": win, "screen": win.screen}, filepath=path, full=True)
        return path if os.path.isfile(path) else None
    except Exception as ex:
        result["errors"].append("screenshot %s: %r" % (name, ex))
        return None


def model():
    m = gv.model() if gv else None
    logic = gv.elements("LOGIC") if gv else []
    shapes = gv.shapes("LOGIC") if gv and hasattr(gv, "shapes") else None
    msg_shapes = gv.shapes("MESSAGES") if gv and hasattr(gv, "shapes") else None
    msgs = gv.elements("MESSAGES") if gv else []
    warnings = gv.warnings() if gv else []
    keep = ("index", "kind", "name", "owner", "state", "info", "flag", "offset")
    return {"model": m,
            "logic": [{k: e[k] for k in keep} for e in logic],
            "messages": [{k: e[k] for k in keep} for e in msgs],
            "shapes": shapes, "message_shapes": msg_shapes,
            "warnings": [{"code": w["code"], "level": w["level"], "owner": w["owner"], "name": w["name"],
                          "layer": w["layer"], "text": w["text"], "element": w["element"]} for w in warnings]}


def s_view():
    space = main_area().spaces.active
    return {"area": main_area().type, "is_game_view": space.is_game_view, "mode": space.game_view_mode,
            "modes": [i.identifier for i in bpy.types.SpaceView3D.bl_rna.properties["game_view_mode"].enum_items],
            "panel_registered": hasattr(bpy.types, "GAMEVIEW_PT_logic_states"),
            "subject_op": hasattr(bpy.ops.logic, "bge_remix_subject"),
            "setting_op": hasattr(bpy.ops.logic, "bge_remix_setting")}


def s_build():
    ret = bpy.ops.view3d.gameview_refresh(override())
    r = model()
    r["ret"] = list(ret)
    return r


def s_shot():
    return {"shot": screenshot("p10c_logic_states")}


def s_start_states():
    """as the Starting States panel does: object.game.states_initial"""
    ob = bpy.data.objects[OBJECT]
    for i in range(30):
        ob.game.states_initial[i] = (i + 1) in NEW_START
    if gv:
        gv.tag_dirty()
    return {"object": OBJECT, "set": NEW_START,
            "now": [i + 1 for i in range(30) if ob.game.states_initial[i]]}


def s_rebuild():
    ret = bpy.ops.view3d.gameview_refresh(override())
    r = model()
    r["ret"] = list(ret)
    return r


def s_save():
    bpy.ops.wm.save_as_mainfile(filepath=SAVED, check_existing=False, compress=False, copy=True)
    return {"saved": SAVED, "exists": os.path.isfile(SAVED)}


def queue_all():
    if os.environ.get("BGE_REMIX_P10C_ONLY_BUILD") == "1":
        # (P10.13: the logic layer's checks only)
        QUEUE.extend([(0, s_view), (5, s_build)])
        return
    QUEUE.extend([(0, s_view), (5, s_build), (10, s_shot), (0, s_start_states), (5, s_rebuild), (0, s_save)])


def step(name, fn):
    print("P10C step", name, flush=True)
    try:
        result["steps"][name] = fn()
    except Exception:
        result["errors"].append("%s: %s" % (name, traceback.format_exc()))
    result["last_step"] = name
    try:
        with open(OUT, "w") as f:
            json.dump(result, f, indent=1)
    except (OSError, TypeError, ValueError):
        pass


class P10CProbe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p10c_probe"
    bl_label = "BGE-Remix p10c probe"
    _timer = None
    _ticks = 0

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        self._ticks += 1
        redraw()
        if self._ticks < 10:
            return {'PASS_THROUGH'}
        if not result.get("queued"):
            result["queued"] = True
            queue_all()
        if QUEUE:
            wait, fn = QUEUE[0]
            if wait > 0:
                QUEUE[0] = (wait - 1, fn)
                return {'PASS_THROUGH'}
            QUEUE.pop(0)
            step(fn.__name__, fn)
            return {'PASS_THROUGH'}
        result["finished"] = True
        context.window_manager.event_timer_remove(self._timer)
        with open(OUT, "w") as f:
            json.dump(result, f, indent=1)
        context.user_preferences.view.use_quit_dialog = False
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


bpy.utils.register_class(P10CProbe)
bpy.ops.wm.bge_remix_p10c_probe('INVOKE_DEFAULT')
