"""BGE-Remix p10b: the tests for the Remix View's Edit and Run modes (P10.6-P10.9) (called from run_p10.py).

normal(args, out, scenes, E, tests, sheets)   with the fake runtime (--mock)
    T10.6  four modes, each with only its own tools
    T10.7a Edit: a click selects an avatar; its settings are Blender's own RNA
    T10.7b Edit: the logic scene wired through the avatars; the warnings follow
    T10.7c Edit: undo and redo of a wiring
    T10.7d Edit: an actuator to its object, a message to its listener, a
           child to its parent
    T10.7e Edit: the avatar's menu adds and removes a brick, a body, a sound
    T10.7f Edit: the saved file holds the changes (read back by another Blender)
    T10.8a the settings hub: every setting once, by cluster and layer
    T10.8b the settings hub: a change shows in Blender's editor and back
    T10.9a Run on the whole scene; the live holograms
    T10.9b Run as a mock-up of one object: only it and what it needs play
    T10.9c suspend, change a property and a place, Resume
    T10.9d Start over: it restarts with the changes
    T10.9e a change Resume cannot take: only Start over, with its reason
    T10.9f Stop, then No: the file as it was; Yes: the changes kept
    P9.7's Remix View tests again (the earlier phase unchanged)
rtx(args, out, scenes, E, tests, sheets, report)   on the RTX PC
    S10.9  Run in RTX Remix: the holograms over Remix's picture
"""

import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE = os.path.join(HERE, "p10b_probe.py")
FRAMES = 30
RTX_FRAMES = 600
TOL = 0.051        # one tic's step (a frame may hold one tic more or less than counted)

READ_BACK = r'''
import json, sys
import bpy
out = sys.argv[sys.argv.index("--") + 1]
sc = bpy.context.scene
ob = sc.objects["Logic"]
g = ob.game
r = {
    "always_links": [c.name for c in g.sensors["Always"].controllers],
    "and_links": [a.name for a in g.controllers["And"].actuators],
    "track_object": g.actuators["Track"].track_object.name if g.actuators["Track"].track_object else None,
    "hear_subject": sc.objects["Listener"].game.sensors["Hear"].subject,
    "child_parent": sc.objects["Child"].parent.name if sc.objects["Child"].parent else None,
    "child_world": list(sc.objects["Child"].matrix_world.translation),
    "sensors": [s.name for s in g.sensors],
    "sounds": [a.name for a in g.actuators if a.type == 'SOUND'],
    "target_physics": sc.objects["Target"].game.physics_type,
    "motion_offset": list(g.actuators["Motion"].offset_location),
}
with open(out, "w") as f:
    json.dump(r, f, indent=1)
'''


def probe(args, out, tag, blend, part, env, timeout=900, frames=FRAMES):
    import run_p2 as p2
    import run_p8 as p8
    res = os.path.join(out, tag + "_p10b.json")
    if os.path.isfile(res):
        os.remove(res)
    folder = os.path.join(out, tag)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    e = p2.base_env(out, tag)
    e.update(p8.GL_ENV)
    e.update(env)
    e.update({"BGE_REMIX_P10B_OUT": res, "BGE_REMIX_P10B_PART": part, "BGE_REMIX_P10B_DIR": folder,
              "BGE_REMIX_P10B_SAVE": os.path.join(folder, "p10b_edited.blend"),
              "BGE_REMIX_P10B_FRAMES": str(frames)})
    code, text, secs = p2.run([p2.exe(args.ours, "blender"), "--disable-crash-handler"] + p8.WINDOW +
                              [blend, "--python", PROBE], e, os.path.join(out, tag + ".log"), timeout=timeout,
                              cwd=out)
    r = p2.read_json(res) or {}
    r.update({"exit": code, "seconds": round(secs, 1), "folder": folder, "traceback": "Traceback" in (text or "")})
    r["ran"] = code == 0 and bool(r.get("finished")) and not r.get("errors")
    return r


def _fail(tests, name, why, detail=None):
    tests[name] = {"ok": False, "checks": {why: False}, "detail": detail}


def _ok_ret(ret):
    return isinstance(ret, list) and "FINISHED" in ret


def _near(a, b, tol=1e-3):
    try:
        return abs(float(a) - float(b)) <= tol
    except (TypeError, ValueError):
        return False


def _base(r):
    return {"ran": r.get("ran"), "exit": r.get("exit"), "errors": r.get("errors"), "last_step": r.get("last_step")}


# ---------------------------------------------------------------------------
# normal

MODE_PANELS = {
    "CHECK": {"GAMEVIEW_PT_element", "GAMEVIEW_PT_warnings", "GAMEVIEW_PT_targets", "GAMEVIEW_PT_model"},
    "DEBUG": {"GAMEVIEW_PT_targets", "GAMEVIEW_PT_inspect", "GAMEVIEW_PT_remix_stats",
              "GAMEVIEW_PT_remaster_preview"},  # (P10.5: the remaster preview)
    "EDIT": {"GAMEVIEW_PT_edit_element", "GAMEVIEW_PT_settings_hub"},
    "RUN": {"GAMEVIEW_PT_run"},
}


def t_modes(s, base, tests):
    m = s.get("s_modes") or {}
    shown = m.get("shown") or {}
    c = {"the probe ran": bool(base["ran"]),
         "the modes are CHECK, DEBUG, EDIT and RUN": m.get("items") == ["CHECK", "DEBUG", "EDIT", "RUN"]}
    for mode, want in sorted(MODE_PANELS.items()):
        got = set((shown.get(mode) or {}).get("panels") or [])
        c["%s mode is set" % mode.title()] = (shown.get(mode) or {}).get("mode") == mode
        c["%s mode shows only its own panels" % mode.title()] = got == want
    tests["T10.6 Remix View: four modes (P10.6)"] = {"ok": all(c.values()), "checks": c,
                                                      "detail": dict(base, modes=m)}


def t_edit(s, base, tests, sheets, saved):
    import run_p2 as p2
    st = s.get("s_edit_start") or {}
    sel = s.get("s_edit_select") or {}
    c = {
        "Edit mode is on": st.get("mode") == 'EDIT',
        "the click selected the Motion actuator": (sel.get("selected") or {}).get("name") == "Motion" and
        _ok_ret(sel.get("ret")),
        "its object became the active one": sel.get("active") == "Logic",
        "the sidebar draws the brick's own RNA (the logic editor's)": bool(sel.get("same_rna")) and
        "offset_location" in (sel.get("props") or []),
        "a change in the sidebar shows in the logic editor": _near((sel.get("sidebar_to_logic") or [0])[0], 0.1),
        "a change in the logic editor shows in the sidebar": _near((sel.get("logic_to_sidebar") or [0])[0], 0.2),
        "a body's settings are Blender's own Physics panel": "mass" in (sel.get("body_props") or []) and
        "physics_type" in (sel.get("body_props") or []),
    }
    tests["T10.7a Edit: a click selects, the settings are Blender's own (P10.7)"] = {
        "ok": all(c.values()), "checks": c, "detail": dict(base, start=st, select=sel)}

    w = s.get("s_edit_wire") or {}
    before = st.get("before") or {}
    after = w.get("after") or {}
    c = {
        "before: the three bricks warn (not linked)": all("LOG_UNCONNECTED" in before.get(n, [])
                                                          for n in ("Always", "And", "Motion")),
        "the sensor wired to the controller": _ok_ret(w.get("sensor_controller")),
        "the controller wired to the actuator": _ok_ret(w.get("controller_actuator")),
        "the links are in the file": (w.get("links") or [[], []]) == [["And"], ["Motion"]],
        "after: none of the three warns about links": all("LOG_UNCONNECTED" not in after.get(n, ["?"])
                                                          for n in ("Always", "And", "Motion")),
    }
    tests["T10.7b Edit: the logic scene wired through its avatars (P10.7)"] = {
        "ok": all(c.values()), "checks": c, "detail": dict(base, wire=w)}

    u = s.get("s_edit_undo") or {}
    c = {
        "undo takes the last wire back": _ok_ret(u.get("undo")) and u.get("after_undo") == [],
        "the warning comes back with it": "LOG_UNCONNECTED" in (u.get("warnings_after_undo") or []),
        "redo puts it back": _ok_ret(u.get("redo")) and u.get("after_redo") == ["Motion"],
        "the warning goes again": "LOG_UNCONNECTED" not in (u.get("warnings_after_redo") or ["?"]),
    }
    tests["T10.7c Edit: undo and redo of a wiring (P10.7)"] = {"ok": all(c.values()), "checks": c,
                                                               "detail": dict(base, undo=u)}

    t = s.get("s_edit_targets") or {}
    cb, ca = t.get("child_world_before") or [0, 0, 0], t.get("child_world_after") or [9, 9, 9]
    c = {
        "the Track To actuator warned (no object)": "LOG_NO_TARGET" in (t.get("track_before") or []),
        "actuator to object: it tracks Target": _ok_ret(t.get("actuator_object")) and t.get("track_object") == "Target",
        "its warning went": "LOG_NO_TARGET" not in (t.get("track_after") or ["?"]),
        "message to listener: the listener hears \"ping\"": _ok_ret(t.get("message_listener")) and
        t.get("hear_subject") == "ping",
        "child to parent: Child's parent is Parent": _ok_ret(t.get("child_parent")) and
        t.get("child_parent_name") == "Parent",
        "the child stayed where it was": all(_near(a, b) for a, b in zip(cb, ca)),
        "a pair that cannot be wired is refused": not _ok_ret(t.get("wrong_pair")),
    }
    tests["T10.7d Edit: actuator to object, message to listener, child to parent (P10.7)"] = {
        "ok": all(c.values()), "checks": c, "detail": dict(base, targets=t)}

    a = s.get("s_edit_add_remove") or {}
    menu = a.get("menu") or []
    c = {
        "a sound added": _ok_ret(a.get("add_sound")) and bool(a.get("sound_elements")),
        "it warns: no sound chosen": "MED_NO_SOUND" in (a.get("sound_warnings") or []),
        "the sound removed": _ok_ret(a.get("remove_sound")) and a.get("sounds_after") == [],
        "a body added (Target is static)": _ok_ret(a.get("add_body")) and bool(a.get("target_body")),
        "the body removed": _ok_ret(a.get("remove_body")) and not a.get("target_body_after"),
        "a brick added (a Keyboard sensor)": _ok_ret(a.get("add_sensor")) and bool(a.get("key_element")),
        "it warns: not linked": "LOG_UNCONNECTED" in (a.get("key_warnings") or []),
        "the avatar's menu offers Add Sound and Remove": "Add Sound" in menu and any(x.startswith("Remove")
                                                                                    for x in menu),
    }
    tests["T10.7e Edit: the avatar's menu adds and removes (P10.7)"] = {
        "ok": all(c.values()), "checks": c, "detail": dict(base, add_remove=a)}

    c = {
        "the edited file was saved": bool((s.get("s_edit_save") or {}).get("saved")),
        "the links are in it": saved.get("always_links") == ["And"] and saved.get("and_links") == ["Motion"],
        "Track To's object": saved.get("track_object") == "Target",
        "the listener's subject": saved.get("hear_subject") == "ping",
        "the child's parent": saved.get("child_parent") == "Parent",
        "the added sensor, the removed sound and body": "Key" in (saved.get("sensors") or []) and
        saved.get("sounds") == [] and saved.get("target_physics") == "NO_COLLISION",
        "the sidebar's setting": _near((saved.get("motion_offset") or [0])[0], 0.2),
    }
    tests["T10.7f Edit: the file holds the changes (P10.7)"] = {"ok": all(c.values()), "checks": c,
                                                                "detail": dict(base, read_back=saved)}
    sheets["p10b_edit"] = p2.contact_sheet(
        [("Edit: Motion selected", sel.get("shot")), ("Edit: after the changes", (s.get("s_edit_save") or {}).get("shot"))],
        os.path.join(base.get("out", ""), "sheet_p10b_edit.png")) if base.get("out") else None


def t_hub(s, base, tests, sheets):
    import run_p2 as p2
    h = s.get("s_hub_rows") or {}
    c = {
        "the probe ran": bool(base["ran"]),
        "the hub lists settings": h.get("rows", 0) > 100,
        "each setting is listed once": bool(h.get("unique")),
        "every game setting, Remix setting and object game setting is there": h.get("missing") == [],
        "the panel draws each row once": bool(h.get("drawn_unique")),
        "the panel draws every row": bool(h.get("drawn_equals_rows")),
        "rows are grouped by the four clusters": set(h.get("clusters") or []) <= {"Entity", "Factor", "Behaviour",
                                                                                  "Gate"} and
        len(h.get("clusters") or []) >= 3,
        "each row's layer is a Remix View layer": bool(h.get("layers_known")),
        "the rows Blender's panels show link to them (40 or more)": h.get("with_link", 0) >= 40,
    }
    tests["T10.8a Settings hub: every setting once, by cluster and layer (P10.8)"] = {
        "ok": all(c.values()), "checks": c, "detail": dict(base, hub=h)}

    rt = (s.get("s_hub_round_trip") or {}).get("rows") or {}
    c = {"the probe ran": bool(base["ran"])}
    for key, r in sorted(rt.items()):
        name = key.split("|")[-1]
        tab = ((r.get("where") or [[None]])[0] or [None])[0]
        c["%s: listed, with a link" % name] = bool(r.get("listed")) and bool(r.get("where"))
        c["%s: a change in the hub shows in Blender's %s panel" % (name, r.get("panel"))] = \
            _same(r.get("hub_to_editor"), r.get("want_hub"))
        c["%s: a change in Blender's panel shows in the hub" % name] = _same(r.get("editor_to_hub"),
                                                                            r.get("want_editor"))
        c["%s: the link opens the Properties editor's tab" % name] = \
            (r.get("show") or {}).get("areas", 0) > 0 and (r.get("show") or {}).get("tab") in (r.get("properties_tab")
                                                                                             or []) and tab is not None
    tests["T10.8b Settings hub: both ways (P10.8)"] = {"ok": all(c.values()), "checks": c,
                                                       "detail": dict(base, round_trip=rt)}
    shot = (s.get("s_hub_round_trip") or {}).get("shot")
    sheets["p10b_hub"] = p2.contact_sheet([("Settings hub", shot)],
                                          os.path.join(base["out"], "sheet_p10b_hub.png")) if base.get("out") else None


def _same(a, b, tol=1e-3):
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(_near(x, y, tol) for x, y in zip(a, b))
    return _near(a, b, tol)


def _obj(r, name):
    return ((r.get("snapshot") or {}).get("objects") or {}).get(name) or {}


def _live(r, kind, owner, name=None):
    for l in r.get("live") or []:
        if l["kind"].startswith(kind) and l["owner"] == owner and (name is None or l["name"] == name):
            return l
    return {}


def t_run(s, base, tests, sheets, V):
    import run_p2 as p2
    step = V["step"]
    x0, y0 = V["mover_start"][0], V["bystander_start"][1]

    w = s.get("s_run_whole") or {}
    snap = w.get("snapshot") or {}
    mover, by = _obj(w, "Mover"), _obj(w, "Bystander")
    count = (mover.get("props") or {}).get("count", -1)
    c = {
        "Run started and suspended": _ok_ret(w.get("ret")) and (w.get("result") or {}).get("outcome") == "SUSPEND",
        "it played its frames": (w.get("result") or {}).get("frames", 0) >= FRAMES,
        "every object of the scene played": snap.get("names") == sorted(V["all"]),
        "Mover counted its tics": count > 0,
        "Mover moved 0.05 m a tic": _near(mover.get("location", [0])[0], x0 + step * count, TOL),
        "Bystander moved too": (by.get("location") or [0, y0])[1] > y0 + step,
        "Mover's Always sensor has a live state": _live(w, "sensor:", "Mover", "Always").get("state") in
        {"RED", "BLACK", "WHITE", "GREEN", "BLUE", "YELLOW"},
        "the property's live value is shown": _live(w, "property:", "Mover", "count").get("text") == str(count),
        "the crate's contact with the ground is shown": "contact" in _live(w, "body:", "Crate").get("text", ""),
        "the suspended game's place was written into the file": _near(((w.get("blender") or {}).get("Mover") or
                                                                       {}).get("location", [0])[0],
                                                                      mover.get("location", [1])[0], 1e-3),
        "suspended: Edit mode": w.get("mode") == "EDIT",
        "Stop, No: Mover back at its start": _near(((w.get("after_stop") or {}).get("Mover") or {}).get(
            "location", [0])[0], x0),
        "the session ended": w.get("state_after_stop") == "IDLE",
        "the picture of the holograms over the game": os.path.isfile(w.get("shot") or ""),
    }
    tests["T10.9a Run: the whole scene, with live holograms (P10.9)"] = {
        "ok": all(c.values()), "checks": c, "detail": dict(base, whole=w)}

    m = s.get("s_run_mockup") or {}
    snap = m.get("snapshot") or {}
    mover = _obj(m, "Mover")
    count = (mover.get("props") or {}).get("count", -1)
    c = {
        "Run as a mock-up of Mover": _ok_ret(m.get("ret")) and (m.get("result") or {}).get("outcome") == "SUSPEND",
        "it plays Mover, its logic's target, the camera and the lamp": snap.get("names") == V["mockup_plays"],
        "Bystander, the crate and the ground are not in it": not set(snap.get("names") or []) & {"Bystander", "Crate",
                                                                                                  "Ground"},
        "Mover plays": count > 0 and _near(mover.get("location", [0])[0], x0 + step * count, TOL),
        "its target plays (its speed copied)": _near((mover.get("props") or {}).get("speed", -1), V["target_speed"]),
        "the hidden objects' avatars are hidden": not _live(m, "object:", "Bystander").get("present", True),
        "the picture": os.path.isfile(m.get("shot") or ""),
    }
    tests["T10.9b Run: a mock-up of one object (P10.9)"] = {"ok": all(c.values()), "checks": c,
                                                            "detail": dict(base, mockup=m)}

    su, re_ = s.get("s_run_suspend") or {}, s.get("s_run_resume") or {}
    m1, m2 = _obj(su, "Mover"), _obj(re_, "Mover")
    c1 = (m1.get("props") or {}).get("count", -1)
    c2 = (m2.get("props") or {}).get("count", -1)
    k = c2 - 1000
    offer = su.get("offer") or {}
    c = {
        "suspended": (su.get("result") or {}).get("outcome") == "SUSPEND",
        "Resume is offered after a property and a place changed": bool(offer.get("resume")),
        "it lists what Resume takes": any("count" in t for t in offer.get("takes") or []) and
        any("place" in t for t in offer.get("takes") or []),
        "the pop-up offers Resume and Start over": _popup_choices(su) == ["RESUME", "START_OVER"],
        "resumed and suspended again": _ok_ret(re_.get("ret")) and (re_.get("result") or {}).get("outcome") ==
        "SUSPEND",
        "the game went on with the property (1000 and counting)": k > 0,
        "the game went on with the place (z = 2)": _near(m2.get("location", [0, 0, 0])[2], 2.0, 1e-3),
        "Mover went on from where it was suspended": _near(m2.get("location", [0])[0],
                                                           m1.get("location", [0])[0] + step * k, TOL),
        "the count before was the first run's": c1 > 0,
    }
    tests["T10.9c Run: suspend, change, Resume (P10.9)"] = {"ok": all(c.values()), "checks": c,
                                                            "detail": dict(base, suspend=su, resume=re_)}

    so = s.get("s_run_start_over") or {}
    m3, b3 = _obj(so, "Mover"), _obj(so, "Bystander")
    k3 = (m3.get("props") or {}).get("count", -1) - 1000
    steps3 = (b3.get("props") or {}).get("steps", -1)
    x1 = m1.get("location", [0])[0]
    c = {
        "started over and suspended": _ok_ret(so.get("ret")) and (so.get("result") or {}).get("outcome") == "SUSPEND",
        "it restarted with the property you set (1000 and counting)": 0 < k3 <= FRAMES * 2,
        "it restarted with the place you set": _near(m3.get("location", [0, 0, 0])[2], 2.0, 1e-3) and
        _near(m3.get("location", [0])[0], x1 + step * k3, TOL),
        "what you did not change restarted (Bystander from its start)": steps3 > 0 and
        _near((b3.get("location") or [0, 0])[1], y0 + step * steps3, TOL),
    }
    tests["T10.9d Run: Start over, with the changes (P10.9)"] = {"ok": all(c.values()), "checks": c,
                                                                 "detail": dict(base, start_over=so)}

    cr = s.get("s_run_cannot_resume") or {}
    offer = cr.get("offer") or {}
    c = {
        "a new brick added while suspended": _ok_ret(cr.get("add")),
        "Resume is not offered": offer.get("resume") is False,
        "the reason names the new brick": any("Extra" in r for r in offer.get("reasons") or []),
        "the pop-up offers only Start over": _popup_choices(cr) == ["START_OVER"],
        "the pop-up says why": any(isinstance(x, list) and x[0] == "label" and "Extra" in str(x[1])
                                   for x in cr.get("popup") or []),
        "Resume, asked anyway, is refused": not _ok_ret(cr.get("resume")) and cr.get("state") == "SUSPENDED",
        "Stop, No: the file as it was (no new brick, Mover at its start, count 0)":
            "Extra" not in (((cr.get("after_no") or {}).get("Mover") or {}).get("sensors") or ["Extra"]) and
            _near(((cr.get("after_no") or {}).get("Mover") or {}).get("location", [0])[0], x0) and
            ((cr.get("after_no") or {}).get("Mover") or {}).get("props", {}).get("count") == 0,
        "the session ended, the kept file freed": cr.get("state_after") == "IDLE" and not cr.get("file_kept_after"),
    }
    tests["T10.9e Run: a change Resume cannot take (P10.9)"] = {"ok": all(c.values()), "checks": c,
                                                                "detail": dict(base, cannot=cr)}

    y = s.get("s_run_stop_yes") or {}
    after = (y.get("after_yes") or {})
    keep = [x[1].get("keep") for x in y.get("popup") or [] if isinstance(x, list) and x[0] == "operator"]
    c = {
        "the stop pop-up offers Yes and No": keep == ["YES", "NO"],
        "Stop, Yes: the change made while suspended is kept": _near((after.get("Target") or {}).get(
            "props", {}).get("speed", -1), 7.5),
        "what the game moved is not kept (Mover at its start)": _near((after.get("Mover") or {}).get(
            "location", [0])[0], x0),
        "nor its count": (after.get("Mover") or {}).get("props", {}).get("count") == 0,
        "the session ended": y.get("state_after") == "IDLE",
    }
    tests["T10.9f Run: Stop, then No and Yes (P10.9)"] = {"ok": all(c.values()), "checks": c,
                                                          "detail": dict(base, stop_yes=y)}
    if base.get("out"):
        sheets["p10b_run"] = p2.contact_sheet(
            [("Run: the whole scene", w.get("shot")), ("Run: a mock-up of Mover", m.get("shot")),
             ("Run: resumed", os.path.join(base["folder"], "run_resume.png"))],
            os.path.join(base["out"], "sheet_p10b_run.png"))


def _popup_choices(r):
    return [x[1].get("choice") for x in r.get("popup") or [] if isinstance(x, list) and x[0] == "operator"]


def regression_p97(args, out, scenes, E, tests, sheets):
    """P9.7's Remix View tests again, on P9.7's own scene (made by p10b_scenes)"""
    import p97_tests
    E9 = (E.get("p10b") or {}).get("p97")
    if not E9:
        _fail(tests, "P9.7 again: the Remix View unchanged (P10.6)", "P9.7's scene was made",
              (E.get("p10b") or {}).get("p97_failed"))
        return
    t9 = {}
    p97_tests.normal(args, out, scenes, E9, t9, sheets)
    for name, t in t9.items():
        tests["P9.7 again: " + name] = t


def normal(args, out, scenes, E, tests, sheets):
    """the normal run (the fake Remix runtime)"""
    import run_p8 as p8
    V = E.get("p10b") or {}
    names = ["T10.6 Remix View: four modes (P10.6)"]
    if not V.get("edit") or not V.get("run"):
        _fail(tests, names[0], "the p10b scenes were made", (E.get("failed") or {}).get("p10b"))
        return
    if not getattr(args, "mock", None):
        _fail(tests, names[0], "the fake runtime was given (--mock)")
        return

    # Edit
    blend = os.path.join(scenes, V["edit"]["file"] + ".blend")
    _log, env = p8.mock_env(args, out, "p10b_edit", os.path.join(out, "cache_p10b"))
    r = probe(args, out, "p10b_edit", blend, "edit", env)
    s = r.get("steps", {})
    base = dict(_base(r), out=out, folder=r.get("folder"))
    t_modes(s, base, tests)
    saved = {}
    path = (s.get("s_edit_save") or {}).get("saved")
    if path:
        res = os.path.join(out, "p10b_read_back.json")
        p8.blender_script(args.ours, out, "p10b_read_back", READ_BACK, [res], blend=path)
        import run_p2 as p2
        saved = p2.read_json(res) or {}
    t_edit(s, base, tests, sheets, saved)

    # the settings hub
    _log, env = p8.mock_env(args, out, "p10b_hub", os.path.join(out, "cache_p10b"))
    r = probe(args, out, "p10b_hub", blend, "hub", env)
    t_hub(r.get("steps", {}), dict(_base(r), out=out, folder=r.get("folder")), tests, sheets)

    # Run
    blend = os.path.join(scenes, V["run"]["file"] + ".blend")
    _log, env = p8.mock_env(args, out, "p10b_run", os.path.join(out, "cache_p10b"))
    r = probe(args, out, "p10b_run", blend, "run", env, timeout=1200)
    t_run(r.get("steps", {}), dict(_base(r), out=out, folder=r.get("folder")), tests, sheets, V["run"])

    # the earlier phase's Remix View
    regression_p97(args, out, scenes, E, tests, sheets)


def rtx(args, out, scenes, E, tests, sheets, report):
    """the RTX run (the real RTX Remix runtime)"""
    import run_p2 as p2
    import run_p8 as p8
    name = "S10.9 Run in RTX Remix: the holograms over Remix's picture (P10.9)"
    V = (E.get("p10b") or {}).get("run_remix")
    if not V:
        _fail(tests, name, "the p10b scenes were made", (E.get("failed") or {}).get("p10b"))
        return
    blend = os.path.join(scenes, V["file"] + ".blend")
    env = dict(p8.REMIX_ENV)
    env["BGE_REMIX_P10B_WAIT"] = "20"
    # (Remix starts with the game's first frame, about 32 s, then needs a moment for its textures)
    r = probe(args, out, "p10b_rtx", blend, "rtx", env, timeout=1800, frames=RTX_FRAMES)
    s = r.get("steps", {})
    run = s.get("s_rtx_run") or {}
    stats = p8.picture(run.get("shot")) if run.get("shot") else None
    c = {
        "the probe ran": bool(r.get("ran")),
        "Run played and suspended": (run.get("result") or {}).get("outcome") == "SUSPEND",
        "the picture of the last frame was taken": stats is not None and float(stats.std()) >= 2.0,
        "the live data was made": bool(run.get("live")),
    }
    tests[name] = {"ok": all(c.values()), "checks": c, "detail": dict(_base(r), run=run)}
    sheets["p10b_rtx_run"] = p2.contact_sheet([("Run in RTX Remix", run.get("shot"))],
                                              os.path.join(out, "sheet_p10b_rtx_run.png"))
    report += ["", "Run in RTX Remix: %s after %s frames; picture %s" % (
        (run.get("result") or {}).get("outcome"), (run.get("result") or {}).get("frames"), run.get("shot"))]
