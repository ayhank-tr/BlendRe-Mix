# BGE-Remix p10b probe: drives the Remix View's Edit and Run modes in
# Blender's UI (Python 3.5), then quits.
#   blender -p 0 0 1600 900 <file> --python p10b_probe.py
#
# Parts (BGE_REMIX_P10B_PART):
#   edit   p10b_edit.blend: avatars selected, their settings as the sidebar
#          draws them, wired by gameview.wire (what a drag between avatars
#          calls), undo and redo, elements added and removed through the
#          avatar's menu; the warnings after each; a copy saved
#          (BGE_REMIX_P10B_SAVE) for the test to read back
#   hub    p10b_edit.blend: the settings hub's rows, drawn into a recording
#          layout, a change made through the hub and read through Blender's
#          own panel and the other way round, its link to the Properties
#          editor
#   run    p10b_run.blend: Run on the whole scene and as a mock-up; suspend,
#          changes, Resume, Start over; a change Resume cannot take; Stop
#          with No and with Yes
#   rtx    p10b_run_remix.blend (the RTX PC): Run on the whole scene in RTX
#          Remix, a picture of the holograms over Remix's
# Results: BGE_REMIX_P10B_OUT (JSON); pictures into BGE_REMIX_P10B_DIR.

import json
import os
import traceback

import bpy

try:
    import _bge_gameview as gv
except ImportError:
    gv = None
import remix_view_tools as rvt
import bl_ui.space_view3d_gameview_modes as modes

OUT = os.environ.get("BGE_REMIX_P10B_OUT", "p10b.json")
DIR = os.environ.get("BGE_REMIX_P10B_DIR", os.path.dirname(os.path.abspath(OUT)))
PART = os.environ.get("BGE_REMIX_P10B_PART", "edit")
SAVE = os.environ.get("BGE_REMIX_P10B_SAVE", os.path.join(DIR, "p10b_edited.blend"))
FRAMES = int(os.environ.get("BGE_REMIX_P10B_FRAMES", "30"))
WAIT = int(os.environ.get("BGE_REMIX_P10B_WAIT", "5"))

result = {"part": PART, "steps": {}, "errors": [], "finished": False}
QUEUE = []


def window():
    return bpy.context.window_manager.windows[0]


def main_area():
    areas = [a for a in window().screen.areas if a.type in ('GAME_VIEW', 'VIEW_3D')]
    areas.sort(key=lambda a: (a.type != 'GAME_VIEW', -a.width * a.height))
    return areas[0] if areas else None


def region(kind='WINDOW'):
    return [r for r in main_area().regions if r.type == kind][0]


def override():
    win = window()
    # (no scene: Stop's No puts the file back, which replaces it)
    return {"window": win, "screen": win.screen, "area": main_area(), "region": region()}


def view():
    return main_area().spaces.active


class Ctx(object):
    """the probe's context for the modes' Python: the Remix View's"""

    def __init__(self):
        o = override()
        self.window = o["window"]
        self.screen = o["screen"]
        self.area = o["area"]
        self.region = o["region"]
        self.space_data = view()

    def __getattr__(self, name):
        return getattr(bpy.context, name)


def scene():
    return bpy.context.scene


def mode(m):
    bpy.ops.gameview.mode(override(), mode=m)
    return view().game_view_mode


def refresh():
    bpy.ops.view3d.gameview_refresh(override())


def find(kind_prefix, owner, name=None):
    """an element's index by kind, owner and name (indices change with every build)"""
    for e in gv.elements():
        if e["kind"].startswith(kind_prefix) and e["owner"] == owner and (name is None or e["name"] == name):
            return e["index"]
    return -1


def warning_codes(owner, name):
    out = []
    for w in gv.warnings():
        if w["owner"] == owner and w["name"] == name and not w["known"]:
            out.append(w["code"])
    return sorted(out)


def screenshot(name):
    path = os.path.join(DIR, name + ".png")
    try:
        win = window()
        bpy.ops.screen.screenshot({"window": win, "screen": win.screen}, filepath=path, full=True)
        return path if os.path.isfile(path) else None
    except Exception as ex:
        result["errors"].append("screenshot %s: %r" % (name, ex))
        return None


def show_sidebar():
    ui = region('UI')
    if ui.width <= 1:
        bpy.ops.view3d.properties(override())


class LogLayout(object):
    """a layout that records what is drawn: props, operators, labels"""

    def __init__(self, log):
        object.__setattr__(self, "_log", log)

    def __setattr__(self, name, value):
        pass

    def prop(self, data, prop, *a, **kw):
        self._log.append(("prop", data, prop))
        return LogLayout(self._log)

    def operator(self, idname, *a, **kw):
        item = OpProps(idname, kw.get("text", ""))
        self._log.append(("operator", item))
        return item

    def label(self, *a, **kw):
        self._log.append(("label", kw.get("text", a[0] if a else "")))
        return LogLayout(self._log)

    def __getattr__(self, name):
        log = object.__getattribute__(self, "_log")

        def anything(*a, **kw):
            return LogLayout(log)
        return anything


class OpProps(object):
    def __init__(self, idname, text):
        self.__dict__["idname"] = idname
        self.__dict__["text"] = text
        self.__dict__["props"] = {}

    def __setattr__(self, name, value):
        self.__dict__["props"][name] = value


def drawn(fn, *args):
    log = []
    fn(LogLayout(log), *args)
    return log


# ---------------------------------------------------------------------------
# edit

def s_modes():
    """each mode's panels in the sidebar, and its RNA identifier"""
    import bl_ui.space_view3d_gameview as gvui
    panels = [c for c in tuple(gvui.classes) + tuple(modes.classes) if issubclass(c, bpy.types.Panel)]
    items = [i.identifier for i in bpy.types.SpaceView3D.bl_rna.properties["game_view_mode"].enum_items]
    shown = {}
    for m in ('CHECK', 'DEBUG', 'EDIT', 'RUN'):
        got = mode(m)
        c = Ctx()
        names = []
        for cls in panels:
            try:
                if cls.poll(c):
                    names.append(cls.__name__)
            except Exception as ex:
                names.append("%s raised %r" % (cls.__name__, ex))
        shown[m] = {"mode": got, "panels": sorted(names)}
    view().game_view_background = 'PLAIN'  # (Debug mode turned RTX Remix's picture on)
    return {"items": items, "shown": shown}


def s_edit_start():
    m = mode('EDIT')
    refresh()
    show_sidebar()
    ob = scene().objects["Logic"]
    return {"mode": m, "elements": len(gv.elements()),
            "before": {n: warning_codes("Logic", n) for n in ("Always", "And", "Motion", "Track")},
            "links": [len(ob.game.sensors["Always"].controllers), len(ob.game.controllers["And"].actuators)]}


def s_edit_select():
    el = find("actuator:", "Logic", "Motion")
    ret = bpy.ops.view3d.gameview_select(override(), element=el)
    sel = modes.selected_element()
    what, ob, data = rvt.element_data(scene(), sel) if sel else (None, None, None)
    same = data is not None and data == scene().objects["Logic"].game.actuators["Motion"]
    # the sidebar's drawing: Blender's RNA of the brick itself
    log = drawn(modes.draw_edit_element, Ctx())
    props = sorted(set(p for k, d, p in [x for x in log if x[0] == "prop"] if d == data))
    # a change through the sidebar's RNA shows in the logic editor's, and the other way round
    data.offset_location = (0.1, 0.0, 0.0)
    seen_logic = tuple(round(v, 4) for v in scene().objects["Logic"].game.actuators["Motion"].offset_location)
    scene().objects["Logic"].game.actuators["Motion"].offset_location = (0.2, 0.0, 0.0)
    seen_side = tuple(round(v, 4) for v in data.offset_location)
    # a body's settings: Blender's own Physics panel drawn into the sidebar
    crate = find("body:", "Crate")
    bpy.ops.view3d.gameview_select(override(), element=crate)
    log2 = drawn(modes.draw_edit_element, Ctx())
    body_props = sorted(set(p for k, d, p in [x for x in log2 if x[0] == "prop"]
                            if getattr(d, "id_data", None) == scene().objects["Crate"]))
    active = scene().objects.active.name if scene().objects.active else None
    bpy.ops.view3d.gameview_select(override(), element=find("actuator:", "Logic", "Motion"))
    return {"ret": list(ret), "selected": sel, "what": what, "same_rna": same, "props": props,
            "sidebar_to_logic": list(seen_logic), "logic_to_sidebar": list(seen_side),
            "body_props": body_props, "active_after_body": active,
            "active": scene().objects.active.name if scene().objects.active else None,
            "shot": screenshot("p10b_edit_select")}


# (an operator called from Python pushes no undo step unless asked: the UI's
# buttons and the drag between avatars do, so the probe asks, 'EXEC_DEFAULT', True)
def wire(src, tgt):
    try:
        return list(bpy.ops.gameview.wire(override(), 'EXEC_DEFAULT', True, source=src, target=tgt))
    except RuntimeError as ex:
        return ["error: %s" % ex]


def s_edit_wire():
    out = {}
    out["sensor_controller"] = wire(find("sensor:", "Logic", "Always"), find("controller:", "Logic", "And"))
    refresh()
    out["controller_actuator"] = wire(find("controller:", "Logic", "And"), find("actuator:", "Logic", "Motion"))
    refresh()
    ob = scene().objects["Logic"]
    out["links"] = [[c.name for c in ob.game.sensors["Always"].controllers],
                    [a.name for a in ob.game.controllers["And"].actuators]]
    out["after"] = {n: warning_codes("Logic", n) for n in ("Always", "And", "Motion")}
    return out


def s_edit_undo():
    ret = list(bpy.ops.ed.undo(override()))
    refresh()
    ob = scene().objects["Logic"]
    undone = [a.name for a in ob.game.controllers["And"].actuators]
    undone_warn = warning_codes("Logic", "Motion")
    ret2 = list(bpy.ops.ed.redo(override()))
    refresh()
    ob = scene().objects["Logic"]
    redone = [a.name for a in ob.game.controllers["And"].actuators]
    return {"undo": ret, "after_undo": undone, "warnings_after_undo": undone_warn, "redo": ret2,
            "after_redo": redone, "warnings_after_redo": warning_codes("Logic", "Motion")}


def s_edit_targets():
    out = {}
    out["track_before"] = warning_codes("Logic", "Track")
    out["actuator_object"] = wire(find("actuator:", "Logic", "Track"), find("object:", "Target"))
    out["message_listener"] = wire(find("message:SEND", "Logic"), find("message:RECEIVE", "Listener"))
    child_before = list(scene().objects["Child"].matrix_world.translation)
    out["child_parent"] = wire(find("object:", "Child"), find("object:", "Parent"))
    refresh()
    sc = scene()
    track = sc.objects["Logic"].game.actuators["Track"]
    out["track_object"] = track.track_object.name if track.track_object else None
    out["track_after"] = warning_codes("Logic", "Track")
    out["hear_subject"] = sc.objects["Listener"].game.sensors["Hear"].subject
    out["child_parent_name"] = sc.objects["Child"].parent.name if sc.objects["Child"].parent else None
    out["child_world_before"] = child_before
    out["child_world_after"] = list(sc.objects["Child"].matrix_world.translation)
    out["message_warnings"] = sorted(w["code"] for w in gv.warnings() if w["layer"] == "MESSAGES")
    out["wrong_pair"] = wire(find("sensor:", "Logic", "Always"), find("object:", "Target"))
    return out


def s_edit_add_remove():
    out = {}
    o = override()
    out["add_sound"] = list(bpy.ops.gameview.element_add(o, 'EXEC_DEFAULT', True, owner="Logic", what='SOUND'))
    refresh()
    sounds = [e for e in gv.elements() if e["kind"] == "sound"]
    out["sound_elements"] = [e["name"] for e in sounds]
    out["sound_warnings"] = warning_codes(sounds[0]["owner"], sounds[0]["name"]) if sounds else []
    if sounds:
        out["remove_sound"] = list(bpy.ops.gameview.element_remove(o, 'EXEC_DEFAULT', True, element=sounds[0]["index"]))
    refresh()
    out["sounds_after"] = [e["name"] for e in gv.elements() if e["kind"] == "sound"]
    out["add_body"] = list(bpy.ops.gameview.element_add(o, 'EXEC_DEFAULT', True, owner="Target", what='BODY', type='STATIC'))
    refresh()
    out["target_body"] = find("body:", "Target") >= 0
    out["remove_body"] = list(bpy.ops.gameview.element_remove(o, 'EXEC_DEFAULT', True, element=find("body:", "Target")))
    refresh()
    out["target_body_after"] = find("body:", "Target") >= 0
    out["add_sensor"] = list(bpy.ops.gameview.element_add(o, 'EXEC_DEFAULT', True, owner="Logic", what='SENSOR', type='KEYBOARD',
                                                          name="Key"))
    refresh()
    out["key_element"] = find("sensor:KEYBOARD", "Logic", "Key") >= 0
    out["key_warnings"] = warning_codes("Logic", "Key")
    # the avatar's menu lists them
    bpy.ops.view3d.gameview_select(o, element=find("sensor:KEYBOARD", "Logic", "Key"))
    log = drawn(lambda layout, c: modes.GAMEVIEW_MT_element.draw(type("M", (), {"layout": layout})(), c), Ctx())
    out["menu"] = [x[1].text for x in log if x[0] == "operator"]
    return out


def s_edit_save():
    bpy.ops.wm.save_as_mainfile(filepath=SAVE, copy=True, check_existing=False, compress=False)
    return {"saved": SAVE if os.path.isfile(SAVE) else None, "shot": screenshot("p10b_edit_after")}


def queue_edit():
    QUEUE.extend([(WAIT, s_modes), (WAIT, s_edit_start), (WAIT, s_edit_select), (2, s_edit_wire), (2, s_edit_undo),
                  (2, s_edit_targets), (2, s_edit_add_remove), (WAIT, s_edit_save)])


# ---------------------------------------------------------------------------
# hub

def s_hub_rows():
    mode('EDIT')
    show_sidebar()
    c = Ctx()
    rows = rvt.hub_rows(c)
    keys = [r["key"] for r in rows]
    sc = scene()
    gs = sc.game_settings

    def settable(data):
        out = []
        for p in data.bl_rna.properties:
            if p.identifier == "rna_type" or p.is_readonly or p.type == 'COLLECTION':
                continue
            if p.type == 'POINTER' and not rvt.is_id_pointer(p):
                continue
            out.append(p.identifier)
        return out
    have = set((r["id_type"], r["id_name"], r["path"], r["prop"]) for r in rows)
    missing = []
    for p in settable(gs):
        if ("Scene", sc.name, "game_settings", p) not in have:
            missing.append("game_settings." + p)
    for name in rvt.remix_scene_settings():
        if name in sc.bl_rna.properties and ("Scene", sc.name, "", name) not in have:
            missing.append(name)
    for ob in sc.objects:
        for p in settable(ob.game):
            if ("Object", ob.name, "game", p) not in have:
                missing.append(ob.name + ".game." + p)
    # drawn: every row once, all groups open
    log = drawn(lambda layout, cc: modes.draw_hub(layout, cc, expand_all=True), c)
    drawn_keys = []
    for item in log:
        if item[0] != "prop":
            continue
        data, prop = item[1], item[2]
        if not hasattr(data, "id_data") or data.id_data is None:
            continue
        idb = data.id_data
        if rvt._id_type(idb) is None:
            continue  # the hub's own search field (the window manager's)
        try:
            path = "" if data == idb else data.path_from_id()
        except ValueError:
            continue
        drawn_keys.append("%s:%s|%s|%s" % (rvt._id_type(idb), idb.name, path, prop))
    per_layer = {}
    for r in rows:
        per_layer[r["layer"]] = per_layer.get(r["layer"], 0) + 1
    return {"rows": len(rows), "unique": len(set(keys)) == len(keys), "missing": missing,
            "drawn": len(drawn_keys), "drawn_unique": len(set(drawn_keys)) == len(drawn_keys),
            "drawn_equals_rows": set(drawn_keys) == set(keys),
            "not_drawn": sorted(set(keys) - set(drawn_keys))[:20],
            "per_layer": per_layer, "clusters": sorted(set(r["cluster"] for r in rows)),
            "with_link": sum(1 for r in rows if r["where"]),
            "layers_known": all(r["layer"] in rvt.CLUSTER_OF for r in rows),
            "sample": [dict(r, where=[list(w) for w in r["where"]]) for r in rows
                       if r["prop"] in {"physics_gravity", "mass", "horizon_color", "fps", "exit_key"}][:12]}


def _panel_data(row, c):
    """the data Blender's own panel (the row's first link) draws the setting with"""
    tab, label = row["where"][0]
    sc = scene()
    if row["owner"]:
        ob = sc.objects[row["owner"]]
        ctx = rvt.ContextShim(c, scene=sc, object=ob, active_object=ob)
    else:
        ctx = rvt.ContextShim(c, scene=sc, world=sc.world)
    for cls in rvt.game_panels(tab):
        if (getattr(cls, "bl_label", "") or cls.__name__) != label:
            continue
        for data, prop in rvt.record_panel(cls, ctx):
            if prop != row["prop"] or getattr(data, "id_data", None) is None:
                continue
            idb = data.id_data
            try:
                path = "" if data == idb else data.path_from_id()
            except ValueError:
                continue
            if "%s:%s|%s|%s" % (rvt._id_type(idb), idb.name, path, prop) == row["key"]:
                return data, cls.__name__
    return None, None


def s_hub_round_trip():
    c = Ctx()
    rows = {r["key"]: r for r in rvt.hub_rows(c)}
    sc = scene()
    wanted = [("Scene:%s|game_settings|physics_gravity" % sc.name, 12.5, 7.25),
              ("Object:Crate|game|mass", 3.5, 2.25),
              ("World:%s||horizon_color" % sc.world.name, (0.1, 0.2, 0.3), (0.3, 0.2, 0.1))]
    out = {}
    for key, v_hub, v_editor in wanted:
        row = rows.get(key)
        r = {"listed": row is not None}
        if row is None:
            out[key] = r
            continue
        r["where"] = [list(w) for w in row["where"]]
        hub_data = rvt.resolve_row(row)
        panel_data, panel = _panel_data(row, c) if row["where"] else (None, None)
        r["panel"] = panel
        if hub_data is not None and panel_data is not None:
            setattr(hub_data, row["prop"], v_hub)
            got = getattr(panel_data, row["prop"])
            r["hub_to_editor"] = list(got) if hasattr(got, "__len__") else got
            setattr(panel_data, row["prop"], v_editor)
            got = getattr(hub_data, row["prop"])
            r["editor_to_hub"] = list(got) if hasattr(got, "__len__") else got
        r["want_hub"] = list(v_hub) if isinstance(v_hub, tuple) else v_hub
        r["want_editor"] = list(v_editor) if isinstance(v_editor, tuple) else v_editor
        try:
            bpy.ops.gameview.hub_show(override(), key=key)
        except RuntimeError as ex:
            r["show_error"] = str(ex)
        r["show"] = dict(modes.LAST_SHOW)
        props = [a.spaces.active.context for a in window().screen.areas if a.type == 'PROPERTIES']
        r["properties_tab"] = props
        out[key] = r
    return {"rows": out, "shot": screenshot("p10b_hub")}


def queue_hub():
    QUEUE.extend([(WAIT, s_hub_rows), (2, s_hub_round_trip)])


# ---------------------------------------------------------------------------
# run

def snap_objects():
    snap = gv.run_snapshot()
    if not snap:
        return None
    out = {}
    for o in snap["objects"]:
        props = {name: value for name, _kind, value in o["props"]}
        m = o["matrix"]
        out[o["name"]] = {"location": [m[12], m[13], m[14]], "props": props, "replica": o["replica"]}
    return {"objects": out, "names": sorted(n for n, o in out.items() if not o["replica"]),
            "ended": snap["ended"], "frames": snap["frames"], "other_scene": snap["other_scene"]}


def live_summary():
    out = []
    for l in gv.live():
        if l["kind"].startswith(("sensor:", "controller:", "actuator:", "property:", "body:")) or not l["present"]:
            out.append({k: l[k] for k in ("kind", "owner", "name", "state", "fired", "present", "fired_frames",
                                          "text")})
    return out


def blender_state():
    sc = scene()
    out = {}
    for name in ("Mover", "Bystander", "Target"):
        ob = sc.objects.get(name)
        if ob is None:
            continue
        out[name] = {"location": list(ob.location), "props": {p.name: p.value for p in ob.game.properties},
                     "sensors": [s.name for s in ob.game.sensors]}
    return out


def run_op(fn, **kw):
    try:
        return list(fn(override(), **kw))
    except RuntimeError as ex:
        return ["error: %s" % ex]


def after_run(tag):
    return {"ret": None, "result": gv.run_result(), "snapshot": snap_objects(), "live": live_summary(),
            "session": {k: rvt.SESSION[k] for k in ("state", "scope", "keep", "suspends", "frame", "other_scene")},
            "blender": blender_state(), "mode": view().game_view_mode,
            "file_kept": gv.run_file_saved(), "shot": os.path.join(DIR, tag + ".png")}


def s_run_whole():
    mode('RUN')
    ret = run_op(bpy.ops.gameview.run, scope='SCENE', suspend_after=FRAMES, shot=os.path.join(DIR, "run_whole.png"))
    r = after_run("run_whole")
    r["ret"] = ret
    r["stop"] = run_op(bpy.ops.gameview.run_stop, keep='NO')
    r["after_stop"] = blender_state()
    r["state_after_stop"] = rvt.SESSION["state"]
    return r


def s_run_mockup():
    mode('RUN')
    ret = run_op(bpy.ops.gameview.run, scope='MOCKUP', objects="Mover", suspend_after=FRAMES,
                 shot=os.path.join(DIR, "run_mockup.png"))
    r = after_run("run_mockup")
    r["ret"] = ret
    r["mockup_objects"] = rvt.mockup_objects(scene(), ["Mover"])
    r["stop"] = run_op(bpy.ops.gameview.run_stop, keep='NO')
    return r


EDIT_COUNT = 1000
EDIT_Z = 2.0


def s_run_suspend():
    mode('RUN')
    ret = run_op(bpy.ops.gameview.run, scope='SCENE', suspend_after=FRAMES)
    r = after_run("run_suspend")
    r["ret"] = ret
    # changes while suspended: a property and a transform
    mover = scene().objects["Mover"]
    mover.game.properties["count"].value = EDIT_COUNT
    mover.location.z = EDIT_Z
    r["edited"] = blender_state()
    r["offer"] = rvt.continue_offer(Ctx())
    r["popup"] = [(x[0], x[1].props if x[0] == "operator" else x[1]) for x in
                  drawn(lambda layout, c: modes.draw_continue_popup(type("P", (), {"layout": layout})(), c), Ctx())
                  if x[0] in {"operator", "label"}]
    return r


def s_run_resume():
    ret = run_op(bpy.ops.gameview.run_continue, choice='RESUME', suspend_after=FRAMES,
                 shot=os.path.join(DIR, "run_resume.png"))
    r = after_run("run_resume")
    r["ret"] = ret
    return r


def s_run_start_over():
    ret = run_op(bpy.ops.gameview.run_continue, choice='START_OVER', suspend_after=FRAMES)
    r = after_run("run_start_over")
    r["ret"] = ret
    return r


def s_run_cannot_resume():
    r = {}
    r["add"] = run_op(bpy.ops.gameview.element_add, owner="Mover", what='SENSOR', type='ALWAYS', name="Extra")
    r["offer"] = rvt.continue_offer(Ctx())
    r["popup"] = [(x[0], x[1].props if x[0] == "operator" else x[1]) for x in
                  drawn(lambda layout, c: modes.draw_continue_popup(type("P", (), {"layout": layout})(), c), Ctx())
                  if x[0] in {"operator", "label"}]
    r["resume"] = run_op(bpy.ops.gameview.run_continue, choice='RESUME', suspend_after=FRAMES)
    r["state"] = rvt.SESSION["state"]
    r["stop"] = run_op(bpy.ops.gameview.run_stop, keep='NO')
    r["after_no"] = blender_state()
    r["state_after"] = rvt.SESSION["state"]
    r["file_kept_after"] = gv.run_file_saved()
    return r


TARGET_EDIT = 7.5


def s_run_stop_yes():
    mode('RUN')
    r = {"run": run_op(bpy.ops.gameview.run, scope='SCENE', suspend_after=FRAMES)}
    r["suspended"] = blender_state()
    scene().objects["Target"].game.properties["speed"].value = TARGET_EDIT
    r["popup"] = [(x[0], x[1].props if x[0] == "operator" else x[1]) for x in
                  drawn(lambda layout, c: modes.draw_keep_popup(type("P", (), {"layout": layout})(), c), Ctx())
                  if x[0] in {"operator", "label"}]
    r["stop"] = run_op(bpy.ops.gameview.run_stop, keep='YES')
    r["after_yes"] = blender_state()
    r["state_after"] = rvt.SESSION["state"]
    return r


def queue_run():
    QUEUE.extend([(WAIT, s_run_whole), (WAIT, s_run_mockup), (WAIT, s_run_suspend), (2, s_run_resume),
                  (2, s_run_start_over), (2, s_run_cannot_resume), (WAIT, s_run_stop_yes)])


# ---------------------------------------------------------------------------
# rtx

def s_rtx_run():
    mode('RUN')
    view().game_view_background = 'REMIX'
    ret = run_op(bpy.ops.gameview.run, scope='SCENE', suspend_after=FRAMES, shot=os.path.join(DIR, "rtx_run.png"))
    r = after_run("rtx_run")
    r["ret"] = ret
    r["stop"] = run_op(bpy.ops.gameview.run_stop, keep='NO')
    return r


def queue_rtx():
    QUEUE.extend([(WAIT * 4, s_rtx_run)])


# ---------------------------------------------------------------------------

def _clean(value):
    """JSON-able"""
    if isinstance(value, dict):
        return {str(k): _clean(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_clean(v) for v in value]
    if isinstance(value, (int, float, str, bool)) or value is None:
        return value
    return repr(value)


def write():
    try:
        with open(OUT, "w") as f:
            json.dump(_clean(result), f, indent=1)
    except (OSError, TypeError, ValueError) as ex:
        print("P10b probe: could not write the results: %r" % ex, flush=True)


def step(name, fn):
    print("P10b step", name, flush=True)
    try:
        result["steps"][name] = fn()
    except Exception:
        result["errors"].append("%s: %s" % (name, traceback.format_exc()))
    result["last_step"] = name
    write()


class P10bProbe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p10b_probe"
    bl_label = "BGE-Remix p10b probe"
    _timer = None
    _ticks = 0

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        self._ticks += 1
        for a in window().screen.areas:
            a.tag_redraw()
        if self._ticks < 10:
            return {'PASS_THROUGH'}
        if not result.get("queued"):
            result["queued"] = True
            {"edit": queue_edit, "hub": queue_hub, "run": queue_run, "rtx": queue_rtx}[PART]()
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
        write()
        context.user_preferences.view.use_quit_dialog = False
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


bpy.utils.register_class(P10bProbe)
bpy.ops.wm.bge_remix_p10b_probe('INVOKE_DEFAULT')
