# BGE-Remix P7 probe: drives the Game View in Blender's UI (Python 3.5).
#   blender -p 0 0 1600 900 <scene.blend> --python p7_probe.py
#
# Parts (BGE_REMIX_P7_PARTS, comma separated):
#   editor    T7.0: the editor-type menu, opening, moving around, closing; a
#             copy of the file saved with a Game View open (BGE_REMIX_P7_SAVE)
#   model     the model: layers, kinds, elements, warnings (and Blender's own
#             lists of kinds, for the coverage test)
#   known     T7.2: marks the first left-out warning as known, then again
#   pictures  T7.5: the camera view with each cluster's layers on
#   walls     T7.5: where the avatars of Hidden and Shown fall, with every
#             layer on and with none
#   bench     B7.1 rebuild time, B7.2 draw time
#   edit      B7.3 from an edit to the view
#   remix     T7.7: the holograms over RTX Remix's picture (RTX PC)
# Results: BGE_REMIX_P7_OUT (JSON); screenshots in BGE_REMIX_P7_DIR.

import json
import os
import time
import traceback

import bpy
from bpy_extras import view3d_utils
from mathutils import Vector

try:
    import _bge_gameview as gv
except ImportError:
    gv = None

OUT = os.environ.get("BGE_REMIX_P7_OUT", "p7.json")
DIR = os.environ.get("BGE_REMIX_P7_DIR", os.path.dirname(os.path.abspath(OUT)))
PARTS = os.environ.get("BGE_REMIX_P7_PARTS", "model").split(",")
SAVE = os.environ.get("BGE_REMIX_P7_SAVE", os.path.join(DIR, "t70_saved.blend"))
TAG = os.environ.get("BGE_REMIX_P7_TAG", "p7")
WAIT = int(os.environ.get("BGE_REMIX_P7_WAIT", "20"))

CLUSTERS = {
    "entity": {"OBJECTS", "CAMERAS", "INTERFACE"},
    "factor": {"RENDERING", "PHYSICS", "MEDIA", "DATA", "FLUIDS", "ARTICULATIONS"},
    "behaviour": {"LOGIC", "PYTHON", "ANIMATION", "MESSAGES", "NAVIGATION", "DESTRUCTION"},
    "gate": {"INPUT", "SCENES", "REMIX", "PERFORMANCE", "TIME", "LEFT_OUT", "UNASSIGNED"},
}
ALL = set().union(*CLUSTERS.values())
VIEW_ALIGNED = 128

result = {"steps": [], "errors": [], "finished": False, "tag": TAG}
state = {"area": None}


# ---------------------------------------------------------------------------
# Blender's UI

def window():
    return bpy.context.window_manager.windows[0]


def main_area():
    a = state["area"]
    areas = [x for x in window().screen.areas if x.type in ('VIEW_3D', 'GAME_VIEW')]
    if a is None or a not in areas:
        a = max(areas, key=lambda x: x.width * x.height) if areas else None
        state["area"] = a
    return a


def region():
    return [r for r in main_area().regions if r.type == 'WINDOW'][0]


def override():
    win = window()
    return {"window": win, "screen": win.screen, "area": main_area(), "region": region(),
            "scene": bpy.context.scene}


def view():
    return main_area().spaces.active


def screenshot(name):
    path = os.path.join(DIR, "%s_%s.png" % (TAG, name))
    try:
        bpy.ops.screen.screenshot(override(), filepath=path, full=False)
        return path
    except Exception as ex:
        result["errors"].append("screenshot %s: %r" % (name, ex))
        return None


def region_box():
    """where the 3D region is in the area's screenshot (top-left origin)"""
    a, r = main_area(), region()
    x = r.x - a.x
    y_top = (a.y + a.height) - (r.y + r.height)
    return {"x": x, "y": y_top, "w": r.width, "h": r.height, "area_w": a.width, "area_h": a.height}


def game_view(on=True):
    main_area().type = 'GAME_VIEW' if on else 'VIEW_3D'


def layers(names):
    view().game_view_layers = set(names)


def camera_view():
    rv3d = view().region_3d
    rv3d.view_perspective = 'CAMERA'


def model():
    return gv.model() if gv else None


# ---------------------------------------------------------------------------
# element positions (as the Game View places them)

def element_world(el):
    if not el["owner"]:
        return Vector(el["pos"])
    ob = bpy.data.objects.get(el["owner"])
    if ob is None:
        return None
    base = ob.matrix_world.translation.copy()
    off = Vector(el["offset"])
    if el["flag"] & VIEW_ALIGNED:
        right = view().region_3d.view_matrix.inverted().col[0].xyz.normalized()
        return base + right * off.x + Vector((0, 0, off.z))
    return base + off


def element_screen(el):
    co = element_world(el)
    if co is None:
        return None
    p = view3d_utils.location_3d_to_region_2d(region(), view().region_3d, co)
    return None if p is None else [round(p.x, 1), round(p.y, 1)]


# ---------------------------------------------------------------------------
# steps: each returns a dict for the result (or None)

def s_editor_menu():
    items = [i.identifier for i in bpy.types.Area.bl_rna.properties["type"].enum_items]
    return {"menu": items, "game_view_in_menu": "GAME_VIEW" in items}


def s_editor_open():
    game_view(True)
    return None


def s_editor_opened():
    m = model()
    return {"area_type": main_area().type, "is_game_view": view().is_game_view,
            "built": m is not None, "elements": m["elements"] if m else 0}


def s_editor_move():
    rv3d = view().region_3d
    before = [list(r) for r in rv3d.view_matrix]
    ov = override()
    bpy.ops.view3d.view_orbit(ov, type='ORBITLEFT')
    bpy.ops.view3d.zoom(ov, delta=1)
    bpy.ops.view3d.view_pan(ov, type='PANRIGHT')
    state["moved_from"] = before
    return None


def s_editor_moved():
    after = [list(r) for r in view().region_3d.view_matrix]
    return {"moved": after != state.get("moved_from")}


def s_editor_save():
    bpy.ops.wm.save_as_mainfile(filepath=SAVE, copy=True, check_existing=False)
    return {"saved": SAVE, "exists": os.path.isfile(SAVE)}


def s_editor_close():
    game_view(False)
    return None


def s_editor_closed():
    r = {"area_type": main_area().type, "is_game_view": view().is_game_view}
    game_view(True)
    return r


def s_refresh():
    t0 = time.perf_counter()
    ret = bpy.ops.view3d.gameview_refresh(override())
    m = model()
    return {"refresh": list(ret), "seconds": round(time.perf_counter() - t0, 4),
            "build_ms": m["build_ms"] if m else None, "convert_ms": m["convert_ms"] if m else None}


def rna_kinds():
    def ids(struct, prop):
        return [i.identifier for i in struct.bl_rna.properties[prop].enum_items]
    t = bpy.types
    return {
        "object": ids(t.Object, "type"),
        "sensor": ids(t.Sensor, "type"),
        "controller": ids(t.Controller, "type"),
        "actuator": ids(t.Actuator, "type"),
        "body": ids(t.GameObjectSettings, "physics_type"),
        "bounds": ids(t.GameObjectSettings, "collision_bounds_type"),
        "lamp": ids(t.Lamp, "type"),
        "property": ids(t.GameProperty, "type"),
        "joint": ids(t.RigidBodyJointConstraint, "pivot_type"),
        "engine": ids(t.SceneGameData, "physics_engine"),
    }


def s_model():
    if gv is None:
        return {"module": False}
    m = model()
    return {"module": True, "model": m, "layers": gv.layers(), "kinds": gv.kinds(), "rna": rna_kinds(),
            "elements": gv.elements(None), "warnings": gv.warnings(),
            "scene_objects": [o.name for o in bpy.context.scene.objects]}


def s_known_mark():
    ws = [w for w in gv.warnings() if w["layer"] == "LEFT_OUT" and not w["known"]]
    if not ws:
        return {"marked": None}
    w = ws[0]
    state["known_key"] = w["key"]
    before = model()["warnings_per_layer"]["LEFT_OUT"]
    bpy.ops.gameview.mark_known(key=w["key"])
    bpy.ops.view3d.gameview_refresh(override())
    after_ws = [x for x in gv.warnings() if x["key"] == w["key"]]
    return {"marked": w["key"], "element": w["element"], "before": before,
            "after": model()["warnings_per_layer"]["LEFT_OUT"],
            "still_listed": bool(after_ws), "known_now": bool(after_ws) and after_ws[0]["known"],
            "element_kept": any(e["name"] == gv.element(w["element"])["name"] for e in gv.elements("LEFT_OUT"))}


def s_known_unmark():
    key = state.get("known_key")
    if not key:
        return None
    bpy.ops.gameview.unmark_known(key=key)
    bpy.ops.view3d.gameview_refresh(override())
    ws = [x for x in gv.warnings() if x["key"] == key]
    return {"unmarked": key, "warns_again": bool(ws) and not ws[0]["known"]}


def s_pictures_setup():
    camera_view()
    v = view()
    v.game_view_dim = True
    v.game_view_labels = True
    v.game_view_background = 'PLAIN'
    return None


def picture(name, names):
    def act():
        layers(names)
        return None

    def rec():
        return {"screenshot": screenshot(name), "layers": sorted(names), "region": region_box()}
    return act, rec


def s_walls_setup():
    camera_view()
    v = view()
    v.game_view_dim = False
    v.game_view_labels = False
    v.game_view_background = 'PLAIN'
    layers(ALL)
    return None


def s_walls_points():
    """where each avatar of Hidden and Shown is on the screen, and its radius"""
    pts = {}
    right = view().region_3d.view_matrix.inverted().col[0].xyz.normalized()
    for el in gv.elements(None):
        if el["owner"] not in ("Hidden", "Shown"):
            continue
        co = element_world(el)
        p = element_screen(el)
        if co is None or p is None:
            continue
        size = 0.24 if el["flag"] & 32 else 0.48
        q = view3d_utils.location_3d_to_region_2d(region(), view().region_3d, co + right * size * 0.5)
        r = abs(q.x - p[0]) if q else 4.0
        pts.setdefault(el["owner"], []).append({"kind": el["kind"], "xy": p, "r": round(r, 1)})
    return {"points": pts, "region": region_box()}


def s_bench_refresh():
    times = []
    for _ in range(3):
        t0 = time.perf_counter()
        bpy.ops.view3d.gameview_refresh(override())
        times.append(round((time.perf_counter() - t0) * 1000.0, 1))
    m = model()
    return {"refresh_ms": times, "elements": m["elements"], "build_ms": m["build_ms"],
            "convert_ms": m["convert_ms"]}


def s_bench_draw():
    layers(ALL)
    v = view()
    v.game_view_background = 'PLAIN'
    v.game_view_labels = True
    v.game_view_dim = True
    ov = override()
    bpy.ops.wm.redraw_timer(ov, type='DRAW', iterations=2)
    n = 20
    t0 = time.perf_counter()
    bpy.ops.wm.redraw_timer(ov, type='DRAW', iterations=n)
    ms = (time.perf_counter() - t0) * 1000.0 / n
    layers(set())
    t0 = time.perf_counter()
    bpy.ops.wm.redraw_timer(ov, type='DRAW', iterations=n)
    ms_off = (time.perf_counter() - t0) * 1000.0 / n
    layers(ALL)
    return {"draw_ms": round(ms, 2), "fps": round(1000.0 / max(ms, 1e-3), 1),
            "draw_ms_no_layers": round(ms_off, 2), "holograms_ms": round(ms - ms_off, 2)}


def s_bench_edit():
    ob = bpy.context.scene.objects.get("Player") or bpy.context.scene.objects.get("Box")
    state["edit_gen"] = model()["generation"]
    state["edit_t0"] = time.perf_counter()
    ob.game.physics_type = 'DYNAMIC' if ob.game.physics_type != 'DYNAMIC' else 'RIGID_BODY'
    state["fast"] = True
    return None


def s_bench_edit_wait():
    """a waiting step: done when the model was rebuilt (checked every tick)"""
    m = model()
    if m["generation"] == state["edit_gen"] and time.perf_counter() - state["edit_t0"] < 10.0:
        return "again"
    state["fast"] = False
    return {"edit_to_view_ms": round((time.perf_counter() - state["edit_t0"]) * 1000.0, 1),
            "rebuilt": m["generation"] != state["edit_gen"]}


def s_remix_setup():
    camera_view()
    v = view()
    v.game_view_dim = False
    v.game_view_labels = True
    layers(ALL)
    v.game_view_background = 'REMIX'
    return None


def rec_shot(name):
    def rec():
        return {"screenshot": screenshot(name), "region": region_box(), "background": view().game_view_background}
    return rec


# (name, act, wait ticks, record)
STEPS = []


def add(name, act, wait=WAIT, rec=None):
    STEPS.append((name, act, wait, rec))


if "editor" in PARTS:
    add("menu", s_editor_menu, 2)
    add("open", s_editor_open, 2)
    add("opened", s_editor_opened, WAIT)
    add("move", s_editor_move, 2)
    add("moved", s_editor_moved, 2)
    add("save", s_editor_save, 5)
    add("close", s_editor_close, 2)
    add("closed", s_editor_closed, WAIT)
else:
    add("open", s_editor_open, WAIT)
add("refresh", s_refresh, 5)
if "model" in PARTS:
    add("model", s_model, 2)
if "known" in PARTS:
    add("known_mark", s_known_mark, 5)
    add("known_unmark", s_known_unmark, 5)
if "pictures" in PARTS:
    add("pictures_setup", s_pictures_setup, WAIT)
    for cname in ("entity", "factor", "behaviour", "gate"):
        act, rec = picture("cluster_" + cname, CLUSTERS[cname])
        add("cluster_" + cname, act, WAIT, rec)
    act, rec = picture("cluster_all", ALL)
    add("cluster_all", act, WAIT, rec)
if "walls" in PARTS:
    add("walls_setup", s_walls_setup, WAIT)
    add("walls_points", s_walls_points, 2)
    act, rec = picture("walls_on", ALL)
    add("walls_on", act, WAIT, rec)
    act, rec = picture("walls_off", set())
    add("walls_off", act, WAIT, rec)
if "bench" in PARTS:
    add("bench_refresh", s_bench_refresh, 5)
    add("bench_draw", s_bench_draw, 5)
if "edit" in PARTS:
    add("bench_edit", s_bench_edit, 1)
    add("bench_edit_wait", s_bench_edit_wait, 1)
if "remix" in PARTS:
    add("remix_setup", s_remix_setup, 150, rec_shot("remix_holograms"))
    act, rec = picture("remix_plain", set())
    add("remix_plain", act, WAIT, rec)


def record(name, entry):
    entry = dict(entry or {})
    entry["step"] = name
    result["steps"].append(entry)
    brief = {k: v for k, v in entry.items() if k not in ("elements", "warnings", "kinds", "layers", "rna",
                                                          "scene_objects", "menu")}
    print("P7 probe: %s %s" % (name, json.dumps(brief, default=str)[:600]), flush=True)


class P7Probe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p7_probe"
    bl_label = "BGE-Remix P7 probe"
    _timer = None

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        state["ticks"] = state.get("ticks", 0) + 1
        if state["ticks"] < 10:
            return {'PASS_THROUGH'}
        if state["ticks"] % 10 and not state.get("fast"):
            return {'PASS_THROUGH'}   # a step tick every 0.1 s (every 0.01 s while timing)
        i = state.get("i", 0)
        if i < len(STEPS):
            name, act, wait, rec = STEPS[i]
            phase = state.get("phase", "act")
            if phase == "act":
                try:
                    out = act()
                except Exception:
                    result["errors"].append("%s: %s" % (name, traceback.format_exc()))
                    out = None
                if out == "again":
                    return {'PASS_THROUGH'}
                state["pending"] = out
                state["phase"] = "wait"
                state["left"] = wait
            else:
                state["left"] -= 1
                if state["left"] <= 0:
                    entry = state.pop("pending", None)
                    if rec is not None:
                        try:
                            entry = dict(entry or {}, **rec())
                        except Exception:
                            result["errors"].append("record %s: %s" % (name, traceback.format_exc()))
                    record(name, entry)
                    state["i"] = i + 1
                    state["phase"] = "act"
            main_area().tag_redraw()
            return {'PASS_THROUGH'}
        context.window_manager.event_timer_remove(self._timer)
        result["finished"] = True
        with open(OUT, "w") as f:
            json.dump(result, f, indent=1, default=str)
        context.user_preferences.view.use_quit_dialog = False
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.01, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


bpy.utils.register_class(P7Probe)
try:
    bpy.ops.wm.bge_remix_p7_probe('INVOKE_DEFAULT')
except Exception:
    bpy.ops.wm.bge_remix_p7_probe(override(), 'INVOKE_DEFAULT')
