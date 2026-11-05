# BGE-Remix P9.7 probe: drives the Remix View in Blender's UI (Python 3.5),
# then quits.
#   blender -p 0 0 1600 900 p9_remix_view.blend --python p97_probe.py
#
# Parts (BGE_REMIX_P97_PARTS, comma separated):
#   ui      the rename as Blender lists it; Check and Debug mode; a debug
#           view and display options chosen; a click on a sphere in Debug
#           mode and what Remix got for it (bge_remix.inspect); the
#           statistics line with the counters it reads (written to
#           BGE_REMIX_P97_STATS at the same moment); back to Check: the
#           model's Remix warnings
#   views   (the RTX PC) Debug mode, each listed debug view in turn and the
#           final picture: a screenshot of the view each (BGE_REMIX_P97_DIR)
# Results: BGE_REMIX_P97_OUT (JSON).

import json
import os
import traceback

import bpy
from bpy_extras import view3d_utils

try:
    import _bge_gameview as gv
except ImportError:
    gv = None
try:
    import bge_remix
except ImportError:
    bge_remix = None
import bl_ui.space_view3d_gameview as gvui

OUT = os.environ.get("BGE_REMIX_P97_OUT", "p97.json")
DIR = os.environ.get("BGE_REMIX_P97_DIR", os.path.dirname(os.path.abspath(OUT)))
STATS = os.environ.get("BGE_REMIX_P97_STATS", os.path.join(DIR, "p97_view_stats.json"))
PARTS = os.environ.get("BGE_REMIX_P97_PARTS", "ui").split(",")
WAIT = int(os.environ.get("BGE_REMIX_P97_WAIT", "20"))       # ticks (0.1 s) for Remix to draw
SPHERE = os.environ.get("BGE_REMIX_P97_SPHERE", "P9_grey_r050")

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


def view():
    return main_area().spaces.active


def redraw():
    for a in window().screen.areas:
        a.tag_redraw()


def screenshot(name):
    path = os.path.join(DIR, name + ".png")
    try:
        bpy.ops.screen.screenshot(override(), filepath=path, full=False)
        return path if os.path.isfile(path) else None
    except Exception as ex:
        result["errors"].append("screenshot %s: %r" % (name, ex))
        return None


def stats_now():
    return bge_remix.view_stats() if bge_remix is not None else None


# ---------------------------------------------------------------------------
# ui

def s_rename():
    area_items = {i.identifier: i.name for i in bpy.types.Area.bl_rna.properties["type"].enum_items}
    props = bpy.types.SpaceView3D.bl_rna.properties
    texts = {}
    for key in ("is_game_view", "game_view_layers", "game_view_background", "game_view_dim", "game_view_labels",
                "game_view_mode"):
        if key in props:
            texts["prop " + key] = props[key].name + " | " + props[key].description
    for op in ("view3d.gameview_select", "view3d.gameview_frame", "view3d.gameview_refresh", "gameview.mode",
               "gameview.targets_edit", "gameview.debug_view"):
        mod, name = op.split(".")
        try:
            rna = getattr(getattr(bpy.ops, mod), name).get_rna().bl_rna
            texts["op " + op] = rna.name + " | " + rna.description
        except Exception as ex:
            texts["op " + op] = "missing: %r" % ex
    km = bpy.context.window_manager.keyconfigs.default.keymaps
    return {"area_type_name": area_items.get("GAME_VIEW"), "texts": texts,
            "keymap_remix_view": "Remix View" in km, "keymap_game_view": "Game View" in km,
            "game_view_words": sorted(k for k, t in texts.items() if "Game View" in t),
            "area_type": main_area().type, "is_game_view": view().is_game_view}


def s_check_remix():
    """Check mode with RTX Remix's picture behind the holograms (Remix runs:
    the forced collisions happen and the counters fill)"""
    view().game_view_background = 'REMIX'
    return {"mode": view().game_view_mode, "background": view().game_view_background}


def s_check_shot():
    return {"shot": screenshot("p97_check"), "stats": stats_now()}


def s_debug():
    ret = bpy.ops.gameview.mode(override(), mode='DEBUG')
    return {"ret": list(ret), "mode": view().game_view_mode, "background": view().game_view_background}


def s_debug_shot():
    return {"shot": screenshot("p97_debug_final"), "mode": view().game_view_mode}


def s_debug_view():
    ret = bpy.ops.gameview.debug_view(override(), index=25)
    wm = bpy.context.window_manager
    wm.remix_debug_false_colour = 'LUMINANCE'
    wm.remix_debug_overlay_opacity = 0.5
    return {"ret": list(ret), "index": wm.remix_debug_view, "name": gvui.debug_view_name(wm.remix_debug_view),
            "listed": list(gvui.DEBUG_VIEWS), "named": {str(k): v for k, v in gvui.DEBUG_VIEW_NAMES.items()}}


def s_debug_view_shot():
    return {"shot": screenshot("p97_debug_view_25")}


def s_click():
    """a click on the sphere in Debug mode: the Remix View's own click
    passes it on; Blender's selection picks the object"""
    scene = bpy.context.scene
    ob = scene.objects.get(SPHERE)
    if ob is None:
        return {"error": "no object " + SPHERE}
    for o in scene.objects:
        o.select = False
    scene.objects.active = None
    co = view3d_utils.location_3d_to_region_2d(region(), view().region_3d, ob.matrix_world.translation)
    if co is None:
        return {"error": "the sphere is not in view"}
    ret = bpy.ops.view3d.select(override(), location=(int(co.x), int(co.y)))
    active = scene.objects.active
    return {"at": [round(co.x, 1), round(co.y, 1)], "ret": list(ret), "active": active.name if active else None}


def s_inspect():
    scene = bpy.context.scene
    ob = scene.objects.active or scene.objects.get(SPHERE)
    r = bge_remix.inspect(ob, scene) if bge_remix is not None else None
    ma = ob.active_material if ob else None
    want = None
    if ma is not None:
        want = {"name": ma.name, "diffuse_color": list(ma.diffuse_color),
                "roughness": float(ma.get("bge_roughness", -1.0)), "metallic": float(ma.get("bge_metallic", -1.0)),
                "remix_material": ma.use_nodes}
    return {"object": ob.name if ob else None, "inspect": r, "material": want}


def s_stats():
    """the statistics line and the counters behind it, at the same moment"""
    line = gvui.remix_stats(bpy.context)
    s = stats_now()
    with open(STATS, "w") as f:
        json.dump(s, f, indent=1)
    return {"line": line, "stats_file": STATS, "targets": gvui.targets(bpy.context.scene)}


def s_back_to_check():
    ret = bpy.ops.gameview.mode(override(), mode='CHECK')
    return {"ret": list(ret), "mode": view().game_view_mode}


def s_warnings():
    bpy.ops.view3d.gameview_refresh(override())
    ws = gv.warnings() if gv else []
    remix = [w for w in ws if w["layer"] == "REMIX"]
    return {"remix_warnings": [{"code": w["code"], "level": w["level"], "text": w["text"]} for w in remix],
            "stats": stats_now(), "shot": screenshot("p97_check_after")}


def queue_ui():
    QUEUE.extend([(0, s_rename), (0, s_check_remix), (WAIT, s_check_shot), (0, s_debug), (WAIT, s_debug_shot),
                  (0, s_debug_view), (WAIT, s_debug_view_shot), (0, s_click), (2, s_inspect), (2, s_stats),
                  (0, s_back_to_check), (WAIT, s_warnings)])


# ---------------------------------------------------------------------------
# views (RTX PC)

def make_view_step(idx):
    def set_view():
        bpy.context.window_manager.remix_debug_view = idx
        return {"index": idx}

    def take():
        return {"index": idx, "shot": screenshot("rv_%04d" % idx)}
    set_view.__name__ = "set_%d" % idx
    take.__name__ = "view_%d" % idx
    return set_view, take


def queue_views():
    QUEUE.append((0, s_debug))
    for idx in (0,) + tuple(gvui.DEBUG_VIEWS):
        set_view, take = make_view_step(idx)
        QUEUE.extend([(WAIT if idx == 0 else 0, set_view), (WAIT, take)])
    QUEUE.append((0, s_back_to_check))


# ---------------------------------------------------------------------------

def step(name, fn):
    try:
        result["steps"][name] = fn()
    except Exception:
        result["errors"].append("%s: %s" % (name, traceback.format_exc()))


class P97Probe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p97_probe"
    bl_label = "BGE-Remix P9.7 probe"
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
            if "ui" in PARTS:
                queue_ui()
            if "views" in PARTS:
                queue_views()
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


bpy.utils.register_class(P97Probe)
bpy.ops.wm.bge_remix_p97_probe('INVOKE_DEFAULT')
