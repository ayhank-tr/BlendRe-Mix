"""BGE-Remix p10c: the tests for the logic bricks (P10.10-P10.13) (called from run_p10.py).

normal(args, out, scenes, E, tests, sheets)   with the fake runtime (--mock)
  T10.c1 Live brick states        p10c_states in the player, every brick's
                                  colour at set tics from the brick log
                                  (BGE_REMIX_P10_BRICK_LOG)
  T10.c2 Missing target and file  p10c_missing: Red
  T10.c3 The log changes nothing  p10c_states with and without the log: the
                                  game's own state the same every tic
  T10.c4 State groups and arrows  the Remix View's model (_bge_gameview) of
                                  p10c_states_view: groups, columns, lines,
                                  arrows, message arrows, warnings
  T10.c5 Starting states          set through RNA as the panel does, the
                                  model marks them, the file keeps them
  T10.c6 A picked subject         p10c_subject: picked from the list, the
                                  message reaches its listener (not before)
  T10.c7 The Timer sensor         once and repeating, at the right tics
  T10.c8 The RTX Remix actuator   the settings reach the fake runtime; in
                                  the Classic pipeline nothing, and Red
  T10.c9 Stock 2.79b              p10c_new_bricks opened, played and saved
                                  in the official 2.79b; ours reopens it
  T10.c10 Earlier phases          P4's, P6's and P7's results in this round
rtx(args, out, scenes, E, tests, sheets, report)   the real runtime
  S10.c1 The RTX Remix actuator   a pulse given while Remix starts waits
                                  and is sent once it runs
"""

import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE = os.path.join(HERE, "p10c_probe.py")

# each brick's colour at set tics (p10c_states; P10.10's table). Delays:
# state 1's fires on tic 31 (30 tics counted), state 2's 61 tics after state
# 2 began (tic 92). State actuators run first in a tic (KX_StateActuator), so
# a state's bricks switch within the tic they are entered or left.
STATES_EXPECT = {
    10: {"S1_delay": "YELLOW", "C1_and": "WHITE", "A1_to2": "WHITE", "A1_send": "WHITE",
         "S2_always": "BLACK", "C2_and": "BLACK", "A2_servo": "BLACK", "S2_delay": "BLACK", "C2_next": "BLACK",
         "A2_to3": "BLACK", "S3_always": "BLACK", "C3_and": "BLACK", "A3_prop": "BLACK"},
    31: {"S1_delay": "GREEN", "C1_and": "GREEN", "A1_to2": "GREEN", "A1_send": "GREEN",
         "S2_always": "WHITE", "C2_and": "WHITE", "A2_servo": "WHITE", "S2_delay": "YELLOW", "C2_next": "WHITE",
         "A2_to3": "WHITE", "S3_always": "BLACK", "C3_and": "BLACK", "A3_prop": "BLACK"},
    32: {"S1_delay": "BLACK", "C1_and": "BLACK", "A1_to2": "BLACK", "A1_send": "BLACK",
         "S2_always": "GREEN", "C2_and": "GREEN", "A2_servo": "GREEN", "S2_delay": "YELLOW", "C2_next": "WHITE"},
    50: {"S1_delay": "BLACK", "A1_send": "BLACK", "S2_always": "GREEN", "C2_and": "WHITE", "A2_servo": "BLUE",
         "S2_delay": "YELLOW", "C2_next": "WHITE", "A2_to3": "WHITE", "S3_always": "BLACK"},
    92: {"S2_delay": "GREEN", "C2_next": "GREEN", "A2_to3": "GREEN", "A2_servo": "BLUE", "S2_always": "BLACK",
         "C2_and": "BLACK", "S3_always": "WHITE", "C3_and": "WHITE", "A3_prop": "WHITE"},
    93: {"A2_servo": "BLACK", "S2_delay": "BLACK", "C2_next": "BLACK", "A2_to3": "BLACK",
         "S3_always": "GREEN", "C3_and": "GREEN", "A3_prop": "GREEN"},
    120: {"S3_always": "GREEN", "C3_and": "WHITE", "A3_prop": "WHITE", "A2_servo": "BLACK", "S1_delay": "BLACK"},
}
STATES_MASKS = {10: 1, 50: 2, 120: 4}   # Brain's state mask
STATES_FRAMES = 125
GV_EL_ACTIVE = 2


def _fail(tests, name, why, detail=None):
    tests[name] = {"ok": False, "checks": {why: False}, "detail": detail}


def _put(tests, name, c, detail):
    tests[name] = {"ok": bool(c) and all(c.values()), "checks": c, "detail": detail}


def read_brick_log(path):
    """the brick log: {tic: {(object, brick): state}}, {tic: {object: state
    mask}}, {(object, brick): kind}"""
    states, masks, kinds = {}, {}, {}
    if not os.path.isfile(path):
        return states, masks, kinds
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("#"):
                continue
            parts = line.rstrip("\r\n").split("\t")
            if len(parts) != 6:
                continue
            tick, _scene, ob, brick, kind, state = parts
            try:
                t = int(tick)
            except ValueError:
                continue
            if kind == "object":
                masks.setdefault(t, {})[ob] = int(state)
            else:
                states.setdefault(t, {})[(ob, brick)] = state
                kinds[(ob, brick)] = kind
    return states, masks, kinds


def play_logged(args, out, tag, blend, frames, env=None, build=None, log=True):
    import run_p8 as p8
    path = os.path.join(out, tag + "_bricks.tsv")
    if os.path.isfile(path):
        os.remove(path)
    e = {"BGE_REMIX_P10_BRICK_LOG": path if log else None}
    e.update(env or {})
    r = p8.play(build or args.ours, out, tag, blend, e, frames=frames)
    r["bricks"], r["masks"], r["kinds"] = read_brick_log(path)
    r["log"] = path
    r.pop("text", None)
    return r


def ticks_in(states, ob, brick, state):
    return sorted(t for t, s in states.items() if s.get((ob, brick)) == state)


def run_summary(r):
    return {"ran": r.get("ran"), "exit": r.get("exit"), "frames": r.get("frames"), "crashed": r.get("crashed"),
            "traceback": r.get("traceback_in_output"), "log": r.get("log"), "tics_logged": len(r.get("bricks") or {})}


def probe(args, out, tag, blend, env):
    import run_p2 as p2
    import run_p8 as p8
    res = os.path.join(out, tag + "_p10c.json")
    if os.path.isfile(res):
        os.remove(res)
    folder = os.path.join(out, tag)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    e = p2.base_env(out, tag)
    e.update(p8.GL_ENV)
    e.update(env)
    e.update({"BGE_REMIX_P10C_OUT": res, "BGE_REMIX_P10C_DIR": folder})
    code, text, secs = p2.run([p2.exe(args.ours, "blender"), "--disable-crash-handler"] + p8.WINDOW +
                              [blend, "--python", PROBE], e, os.path.join(out, tag + ".log"), timeout=900, cwd=out)
    r = p2.read_json(res) or {}
    r.update({"exit": code, "seconds": round(secs, 1), "folder": folder})
    r["ran"] = code == 0 and bool(r.get("finished"))
    return r


def blender_json(build, out, tag, body, blend, extra=()):
    """a script in a background Blender (ours or stock) that writes JSON to
    its first argument"""
    import run_p2 as p2
    import run_p8 as p8
    target = os.path.join(out, tag + ".json")
    if os.path.isfile(target):
        os.remove(target)
    code, text, _ = p8.blender_script(build, out, tag, body, [target] + list(extra), blend=blend, timeout=600)
    r = p2.read_json(target) or {}
    r["_exit"] = code
    r["_traceback"] = "Traceback" in (text or "")
    return r


# ---------------------------------------------------------------------------
# P10.10

def t_live_states(args, out, scenes, E, tests):
    name = "T10.c1 Live brick states (P10.10)"
    S = E.get("states")
    if not S:
        return _fail(tests, name, "the p10c states scene was made", E.get("failed"))
    r = play_logged(args, out, "p10c_states", os.path.join(scenes, S["file"] + ".blend"), STATES_FRAMES)
    st, masks = r["bricks"], r["masks"]
    c, wrong = {"the scene played (the probe's %d frames)" % STATES_FRAMES: bool(r["ran"]),
                "the brick log was written": bool(st)}, {}
    for tick in sorted(STATES_EXPECT):
        got = st.get(tick, {})
        bad = {b: [want, got.get(("Brain", b))] for b, want in STATES_EXPECT[tick].items()
               if got.get(("Brain", b)) != want}
        if bad:
            wrong[tick] = bad
        c["tic %d: Brain's %d bricks as the table says" % (tick, len(STATES_EXPECT[tick]))] = not bad
    for tick, mask in sorted(STATES_MASKS.items()):
        c["tic %d: Brain in state mask %d" % (tick, mask)] = masks.get(tick, {}).get("Brain") == mask
    heard = ticks_in(st, "Listener", "L_hear", "GREEN")
    got = ticks_in(st, "Listener", "L_got", "GREEN")
    c["the message: Listener's sensor Green once, within two tics of the send (tic 31)"] = \
        len(heard) == 1 and 31 <= heard[0] <= 33
    c["... and its controller and actuator Green on that tic"] = \
        bool(heard) and got == heard and ticks_in(st, "Listener", "L_and", "GREEN") == heard
    c["Listener White between messages (tics 10 and 50)"] = all(
        st.get(t, {}).get(("Listener", b)) == "WHITE" for t in (10, 50) for b in ("L_hear", "L_and", "L_got"))
    c["the RTX Remix actuator in a Classic scene: Red"] = all(
        st.get(t, {}).get(("Settings", "X_remix")) == "RED" for t in (10, 50, 120))
    _put(tests, name, c, {"run": run_summary(r), "wrong": wrong, "listener_green": heard})
    return r


def t_missing(args, out, scenes, E, tests):
    name = "T10.c2 A missing target and a missing file: Red (P10.10)"
    M = E.get("missing")
    if not M:
        return _fail(tests, name, "the p10c missing scene was made", E.get("failed"))
    r = play_logged(args, out, "p10c_missing", os.path.join(scenes, M["file"] + ".blend"), 20)
    at = r["bricks"].get(10, {})
    ob = M["object"]
    c = {"the scene played": bool(r["ran"])}
    for b in M["red"]:
        c["%s Red (%s)" % (b, M["why"].get(b, ""))] = at.get((ob, b)) == "RED"
    for b in M["not_red"]:
        c["%s not Red (nothing missing)" % b] = at.get((ob, b)) not in (None, "RED")
    _put(tests, name, c, {"run": run_summary(r), "at_tic_10": {k[1]: v for k, v in at.items() if k[0] == ob}})


def t_log_neutral(args, out, scenes, E, tests):
    import run_p2 as p2
    name = "T10.c3 The brick states only read: the game plays the same (P10.10)"
    S = E.get("states")
    if not S:
        return _fail(tests, name, "the p10c states scene was made", E.get("failed"))
    blend = os.path.join(scenes, S["file"] + ".blend")
    traces = {}
    runs = {}
    for tag, log in (("p10c_trace_log", True), ("p10c_trace_nolog", False)):
        # (its own name: <tag>.json is the play's result file, which would overwrite it)
        path = os.path.join(out, tag + "_trace.json")
        if os.path.isfile(path):
            os.remove(path)
        runs[tag] = play_logged(args, out, tag, blend, STATES_FRAMES, {"BGE_REMIX_P10C_TRACE": path}, log=log)
        traces[tag] = p2.read_json(path) if os.path.isfile(path) else None
    a, b = traces["p10c_trace_log"], traces["p10c_trace_nolog"]
    first = None
    if a and b:
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                first = {"tic": i + 1, "with_log": x, "without": y}
                break
    c = {"both runs played": all(r["ran"] for r in runs.values()),
         "the log was written only when asked": bool(runs["p10c_trace_log"]["bricks"]) and
         not runs["p10c_trace_nolog"]["bricks"],
         "the game's trace was written (120 tics)": bool(a) and bool(b) and len(a) == len(b) == 120,
         "the same every tic (Brain's position, state, properties)": bool(a) and a == b}
    _put(tests, name, c, {"first_difference": first, "runs": {k: run_summary(v) for k, v in runs.items()}})


# ---------------------------------------------------------------------------
# P10.11

def _el(els, owner, name, prefix=None):
    for e in els:
        if e["owner"] == owner and e["name"] == name and (prefix is None or e["kind"].startswith(prefix)):
            return e
    return None


def t_model(args, out, scenes, E, tests, sheets):
    import run_p2 as p2
    names = ["T10.c4 State groups and arrows in the Remix View (P10.11)",
             "T10.c5 Starting states set and saved (P10.11)"]
    S = E.get("states")
    if not S:
        for n in names:
            _fail(tests, n, "the p10c states scene was made", E.get("failed"))
        return
    saved = os.path.join(out, "p10c_states_saved.blend")
    if os.path.isfile(saved):
        os.remove(saved)
    new_start = S["new_start"]["Brain"]
    r = probe(args, out, "p10c_view", os.path.join(scenes, S["view_file"] + ".blend"),
              {"BGE_REMIX_P10C_SAVED": saved, "BGE_REMIX_P10C_NEW_START": ",".join("%d" % s for s in new_start),
               "BGE_REMIX_P10C_OBJECT": "Brain"})
    steps = r.get("steps", {})
    view, built, rebuilt = steps.get("s_view") or {}, steps.get("s_build") or {}, steps.get("s_rebuild") or {}
    base = {"ran": r["ran"], "errors": r.get("errors"), "exit": r.get("exit"), "view": view}

    els = built.get("logic") or []
    shapes = built.get("shapes")
    groups = {e["name"]: e for e in els if e["owner"] == "Brain" and e["kind"] == "state:GROUP"}
    want_groups = {"State %d" % s for s in (1, 2, 3)}
    c = {"the probe ran": r["ran"], "the model was built": bool(built.get("model")),
         "_bge_gameview.shapes is there": shapes is not None,
         "Brain's states as groups: State 1, 2, 3": set(groups) == want_groups}
    g1 = groups.get("State 1") or {}
    c["State 1 marked: the object starts in it (Green, active)"] = \
        g1.get("state") == "GREEN" and bool(g1.get("flag", 0) & GV_EL_ACTIVE) and "starts" in g1.get("info", "")
    c["States 2 and 3 not marked (White, entered by a State actuator)"] = all(
        (groups.get(n) or {}).get("state") == "WHITE" and "State actuator" in (groups.get(n) or {}).get("info", "")
        for n in ("State 2", "State 3"))
    # columns: each brick nearest (along the view's right) to its group's header
    columns = {}
    for brick, state in S["groups"]["Brain"].items():
        e = None
        for prefix in ("sensor:", "controller:", "actuator:"):
            e = e or _el(els, "Brain", brick, prefix)
        if not e or not groups:
            columns[brick] = None
            continue
        x = e["offset"][0]
        nearest = min(groups.values(), key=lambda g: abs(g["offset"][0] - x))
        columns[brick] = nearest["name"]
    c["each brick in its state's column (S1_delay ... A3_prop)"] = all(
        columns.get(b) == "State %d" % s for b, s in S["groups"]["Brain"].items())
    brick_z = [e["offset"][2] for e in els if e["owner"] == "Brain" and
               e["kind"].split(":")[0] in ("sensor", "controller", "actuator")]
    c["each column topped by its group"] = bool(groups) and bool(brick_z) and all(
        g["offset"][2] > max(brick_z) for g in groups.values())
    by_index = {e["index"]: e for e in els}
    members = {}
    arrows = []
    for s in shapes or []:
        a, b = by_index.get(s["a"]), by_index.get(s["b"])
        if not a or not b:
            continue
        if s["type"] == "LINE" and s["dashed"] and a["kind"] == "state:GROUP" and a["owner"] == "Brain":
            members.setdefault(a["name"].split()[-1], []).append(b["name"])
        if s["type"] == "ARC" and s["arrow"] and a["kind"] == "state:GROUP" and b["kind"] == "state:GROUP" and \
                a["owner"] == "Brain":
            arrows.append([int(a["name"].split()[-1]), int(b["name"].split()[-1])])
    c["a dashed line from each group to its controllers"] = \
        {k: sorted(v) for k, v in members.items()} == {k: sorted(v) for k, v in S["members"]["Brain"].items()}
    c["State actuators as arrows between the groups: 1 -> 2, 2 -> 3"] = \
        sorted(arrows) == sorted(S["arrows"]["Brain"])
    msgs = built.get("messages") or []
    mby = {e["index"]: e for e in msgs}
    marcs = [(mby.get(s["a"]), mby.get(s["b"]), s["arrow"]) for s in (built.get("message_shapes") or [])
             if s["type"] == "ARC"]
    c["messages: an arrow from Brain's \"go\" to Listener's"] = any(
        a and b and arrow and a["owner"] == "Brain" and a["name"] == "go" and b["owner"] == "Listener"
        for a, b, arrow in marcs)
    warns = built.get("warnings") or []
    c["P7's warning stays: nothing listens for \"lost\""] = any(
        w["code"] == "MSG_NO_LISTENER" and w["owner"] == "Settings" for w in warns)
    c["the RTX Remix actuator in a Classic scene is warned about (Remix layer)"] = any(
        w["code"] == "RMX_ACTUATOR_CLASSIC" and w["name"] == "X_remix" and w["layer"] == "REMIX" for w in warns)
    c["the Starting States panel and the pick operators are registered"] = \
        bool(view.get("panel_registered")) and bool(view.get("subject_op")) and bool(view.get("setting_op"))
    shot = (steps.get("s_shot") or {}).get("shot")
    sheets["p10c_logic_states"] = p2.contact_sheet([("Remix View, logic layer: Brain's states", shot)],
                                                   os.path.join(out, "sheet_p10c_logic_states.png"))
    _put(tests, names[0], c, dict(base, groups=groups, columns=columns, members=members, arrows=arrows,
                                  message_arcs=len(marcs)))

    # starting states
    st = steps.get("s_start_states") or {}
    rels = rebuilt.get("logic") or []
    rgroups = {e["name"]: e for e in rels if e["owner"] == "Brain" and e["kind"] == "state:GROUP"}
    g3 = rgroups.get("State 3") or {}
    check = blender_json(args.ours, out, "p10c_saved_check", SAVED_CHECK, saved)
    c = {"the probe set them through RNA": st.get("now") == new_start,
         "the rebuilt model marks State 3 too (Green, active)":
             g3.get("state") == "GREEN" and bool(g3.get("flag", 0) & GV_EL_ACTIVE),
         "State 1 still marked": (rgroups.get("State 1") or {}).get("state") == "GREEN",
         "the file was saved": bool((steps.get("s_save") or {}).get("exists")),
         "the saved file starts Brain in states %s" % new_start: check.get("Brain") == new_start,
         "... and the other objects as before": check.get("Listener") == S["start"]["Listener"]}
    _put(tests, names[1], c, dict(base, set=st, saved=check))


SAVED_CHECK = r'''
import bpy, json, sys
out = sys.argv[sys.argv.index("--") + 1]
r = {}
for name in ("Brain", "Listener", "Settings"):
    ob = bpy.data.objects.get(name)
    if ob is not None:
        r[name] = [i + 1 for i in range(30) if ob.game.states_initial[i]]
with open(out, "w") as f:
    json.dump(r, f)
'''


def t_subject(args, out, scenes, E, tests):
    name = "T10.c6 A message subject picked from the list reaches its listener (P10.11)"
    U = E.get("subject")
    if not U:
        return _fail(tests, name, "the p10c subject scenes were made", E.get("failed"))
    before = play_logged(args, out, "p10c_subject_before", os.path.join(scenes, U["before_file"] + ".blend"), 40)
    after = play_logged(args, out, "p10c_subject", os.path.join(scenes, U["file"] + ".blend"), 40)
    hob, hs = U["listener"]
    oob, os_ = U["other"]
    c = {"both played": bool(before["ran"]) and bool(after["ran"]),
         "the list holds the subjects the game uses (ping, pong)": U.get("listed") == ["ping", "pong"],
         "picking set the Message actuator's subject": U.get("picked") == ["FINISHED"] and
         U.get("subject_after") == "ping",
         "before the pick: the listener never fires": not ticks_in(before["bricks"], hob, hs, "GREEN"),
         "after the pick: the listener fires (within 3 tics)": any(
             t <= 3 for t in ticks_in(after["bricks"], hob, hs, "GREEN")),
         "the other listener (pong) never fires": not ticks_in(after["bricks"], oob, os_, "GREEN")}
    _put(tests, name, c, {"listed": U.get("listed"), "before": run_summary(before), "after": run_summary(after),
                          "listener_green_after": ticks_in(after["bricks"], hob, hs, "GREEN")})


# ---------------------------------------------------------------------------
# P10.12

def due_tics(seconds, ticrate, repeat, last):
    """the tics a Timer fires on (tic k stands for k / ticrate seconds)"""
    out = []
    n = 1
    while True:
        k = 1
        while k / float(ticrate) + 1e-6 < n * seconds:
            k += 1
        if k > last:
            break
        out.append(k)
        if not repeat:
            break
        n += 1
    return out


def t_timer(args, out, scenes, E, tests):
    import run_p2 as p2
    name = "T10.c7 The Timer sensor, once and repeating (P10.12)"
    T = E.get("timer")
    if not T:
        return _fail(tests, name, "the p10c timer scene was made", E.get("failed"))
    frames = 70
    py = os.path.join(out, "p10c_timer_py.json")
    if os.path.isfile(py):
        os.remove(py)
    r = play_logged(args, out, "p10c_timer", os.path.join(scenes, T["file"] + ".blend"), frames,
                    {"BGE_REMIX_P10C_PY": py})
    st = r["bricks"]
    ob = T["object"]
    once = due_tics(T["seconds"]["once"], T["ticrate"], False, frames)
    rep = due_tics(T["seconds"]["repeat"], T["ticrate"], True, frames)
    pyr = p2.read_json(py)
    all_tics = range(1, frames + 1)
    c = {"the scene played": bool(r["ran"]),
         "T_once Green on tic %s only (%.2f s)" % (once, T["seconds"]["once"]):
             ticks_in(st, ob, "T_once", "GREEN") == once,
         "T_once Yellow before it (waiting), White after": all(
             st.get(t, {}).get((ob, "T_once")) == ("YELLOW" if t < once[0] else "WHITE")
             for t in all_tics if once and t != once[0]),
         "T_rep Green on tics %s (every %.2f s)" % (rep, T["seconds"]["repeat"]):
             ticks_in(st, ob, "T_rep", "GREEN") == rep,
         "T_rep Yellow between": all(st.get(t, {}).get((ob, "T_rep")) == "YELLOW" for t in all_tics if t not in rep),
         "their actuators fired with them": ticks_in(st, ob, "T_once_prop", "GREEN") == once and
         ticks_in(st, ob, "T_rep_prop", "GREEN") == rep,
         "Python: SCA_TimerSensor with seconds and repeat":
             (pyr.get("T_once") or {}).get("type") == "SCA_TimerSensor" and
             (pyr.get("T_once") or {}).get("seconds") == T["seconds"]["once"] and
             (pyr.get("T_rep") or {}).get("repeat") is True}
    _put(tests, name, c, {"run": run_summary(r), "python": pyr, "once_green": ticks_in(st, ob, "T_once", "GREEN"),
                          "rep_green": ticks_in(st, ob, "T_rep", "GREEN")})


def t_remix_actuator(args, out, scenes, E, tests):
    import run_p2 as p2
    import run_p8 as p8
    import run_p9 as r9
    name = "T10.c8 The RTX Remix actuator: the setting reaches the fake runtime (P10.12)"
    R = E.get("remix")
    if not R:
        return _fail(tests, name, "the p10c remix scenes were made", E.get("failed"))
    if not getattr(args, "mock", None):
        return _fail(tests, name, "the fake runtime was given (--mock)")
    ob = R["object"]
    key1, val1 = R["settings"]["R_start"]
    key2, val2 = R["settings"]["R_later"]
    runs = {}
    for tag, f in (("p10c_remix", R["file"]), ("p10c_remix_classic", R["classic_file"])):
        log, env = p8.mock_env(args, out, tag, os.path.join(out, "cache_" + tag))
        py = os.path.join(out, tag + "_py.json")
        if os.path.isfile(py):
            os.remove(py)
        env["BGE_REMIX_P10C_PY"] = py
        rr = play_logged(args, out, tag, os.path.join(scenes, f + ".blend"), 80, env)
        rr["mock"] = r9.mock_lines(log)
        rr["python"] = p2.read_json(py)
        runs[tag] = rr
    on, cl = runs["p10c_remix"], runs["p10c_remix_classic"]
    sets1 = r9.config_sets(on["mock"], key1)
    sets2 = r9.config_sets(on["mock"], key2)
    green1 = ticks_in(on["bricks"], ob, "R_start", "GREEN")
    later = R["later_tics"] + 1
    pyr = on["python"] or {}
    c = {"both played": bool(on["ran"]) and bool(cl["ran"]),
         "the list holds Remix's settings (%s, %s)" % (key1, key2): key1 in R["listed"] and key2 in R["listed"],
         "picking %s from the list filled in its default value" % key1: R.get("prefilled") not in (None, ""),
         "%s = %s reached the fake runtime" % (key1, val1): val1 in sets1,
         "%s = %s reached the fake runtime" % (key2, val2): val2 in sets2,
         "the pulse at the start: Green, and Green again when sent (Remix started after the first tic)":
             1 in green1 and any(t > 1 for t in green1),
         "R_later Green on tic %d (its delay)" % later: later in ticks_in(on["bricks"], ob, "R_later", "GREEN"),
         "never Red with Remix on": not ticks_in(on["bricks"], ob, "R_start", "RED") and
         not ticks_in(on["bricks"], ob, "R_later", "RED"),
         "Python: KX_RemixActuator with setting and value":
             (pyr.get("R_start") or {}).get("type") == "KX_RemixActuator" and
             (pyr.get("R_start") or {}).get("setting") == key1 and (pyr.get("R_start") or {}).get("value") == val1,
         "Classic: nothing reaches Remix": val1 not in r9.config_sets(cl["mock"], key1) and
         val2 not in r9.config_sets(cl["mock"], key2),
         "Classic: both actuators Red": all(cl["bricks"].get(t, {}).get((ob, b)) == "RED"
                                            for t in (10, 60) for b in ("R_start", "R_later"))}
    _put(tests, name, c, {"sets": {key1: sets1, key2: sets2}, "r_start_green": green1,
                          "r_start_yellow": ticks_in(on["bricks"], ob, "R_start", "YELLOW"),
                          "runs": {k: run_summary(v) for k, v in runs.items()}, "python": pyr})


STOCK_OPEN = r'''
import bpy, json, sys
argv = sys.argv[sys.argv.index("--") + 1:]
out, saved = argv[0], argv[1]
ob = bpy.data.objects.get("Clock")
r = {"version": bpy.app.version_string, "found": ob is not None}
if ob is not None:
    r["sensors"] = [s.name for s in ob.game.sensors]
    r["sensor_rna"] = {s.name: s.bl_rna.identifier for s in ob.game.sensors}
    r["controllers"] = [c.name for c in ob.game.controllers]
    r["actuators"] = [a.name for a in ob.game.actuators]
    r["actuator_rna"] = {a.name: a.bl_rna.identifier for a in ob.game.actuators}
bpy.ops.wm.save_as_mainfile(filepath=saved, check_existing=False, copy=True)
r["saved"] = True
with open(out, "w") as f:
    json.dump(r, f)
'''

OURS_REOPEN = r'''
import bpy, json, sys
out = sys.argv[sys.argv.index("--") + 1]
ob = bpy.data.objects.get("Clock")
r = {"found": ob is not None, "timers": {}, "remix": {}}
if ob is not None:
    for s in ob.game.sensors:
        if s.type == 'TIMER':
            r["timers"][s.name] = [round(s.seconds, 4), bool(s.use_repeat)]
    for a in ob.game.actuators:
        if a.type == 'REMIX':
            r["remix"][a.name] = [a.setting, a.value]
    r["sensors"] = [s.name for s in ob.game.sensors]
    r["actuators"] = [a.name for a in ob.game.actuators]
with open(out, "w") as f:
    json.dump(r, f)
'''


def t_stock(args, out, scenes, E, tests):
    import run_p8 as p8
    name = "T10.c9 The new bricks in stock 2.79b: the file opens and plays, the bricks do nothing (P10.12)"
    N = E.get("new_bricks")
    if not N:
        return _fail(tests, name, "the p10c new-bricks scene was made", E.get("failed"))
    if not getattr(args, "official", None):
        return _fail(tests, name, "the official 2.79b was given (--official)")
    blend = os.path.join(scenes, N["file"] + ".blend")
    saved = os.path.join(out, "p10c_new_bricks_stock_saved.blend")
    if os.path.isfile(saved):
        os.remove(saved)
    opened = blender_json(args.official, out, "p10c_stock_open", STOCK_OPEN, blend, [saved])
    played = p8.play(args.official, out, "stock_p10c_new_bricks", blend, frames=60)
    played.pop("text", None)
    back = blender_json(args.ours, out, "p10c_ours_reopen", OURS_REOPEN, saved) if os.path.isfile(saved) else {}
    ours_play = play_logged(args, out, "p10c_new_bricks_resaved", saved, 60) if os.path.isfile(saved) else {}
    B = N["bricks"]
    c = {"stock 2.79b opens it in the background, no error": opened.get("_exit") == 0 and
         not opened.get("_traceback") and bool(opened.get("found")),
         "stock lists every brick by name": opened.get("sensors") == B["sensors"] and
         opened.get("controllers") == B["controllers"] and opened.get("actuators") == B["actuators"],
         "stock sees the new bricks as plain Sensor and Actuator (no data)":
             all((opened.get("sensor_rna") or {}).get(s) == "Sensor" for s in N["timers"]) and
             all((opened.get("actuator_rna") or {}).get(a) == "Actuator" for a in N["remix"]),
         "stock's player plays it 60 frames, no crash": bool(played.get("ran")) and not played.get("crashed"),
         "stock saves it": os.path.isfile(saved),
         "ours reopens stock's copy: the bricks are back (with their defaults)":
             sorted((back.get("timers") or {}).keys()) == sorted(N["timers"]) and
             sorted((back.get("remix") or {}).keys()) == sorted(N["remix"]),
         "ours plays stock's copy, no crash": bool(ours_play.get("ran"))}
    _put(tests, name, c, {"stock_open": opened, "stock_play": {k: played.get(k) for k in
                                                              ("ran", "exit", "frames", "crashed", "crash_report")},
                          "ours_reopen": back, "ours_play": run_summary(ours_play) if ours_play else None,
                          "note": "stock 2.79b drops the new bricks' data when it saves; ours gives them their "
                                  "defaults again (seconds 1, no setting)"})


def t_earlier(args, out, tests):
    import run_p2 as p2
    name = "T10.c10 P4's and P6's games and the earlier logic tests unchanged"
    c, d = {}, {}
    results = getattr(args, "results", None) or os.path.dirname(out)
    for phase, what in (("p4", "P4's games (RTX Remix and the player)"), ("p6", "P6's games (PhysX)"),
                        ("p7", "P7's Game View checks of the logic")):
        path = os.path.join(results, phase, "results_%s.json" % phase)
        res = p2.read_json(path)
        d[phase] = {"file": path, "found": bool(res), "all_ok": res.get("all_ok") if res else None}
        c["%s passed in this round (%s)" % (what, phase)] = bool(res) and res.get("all_ok") is True
    _put(tests, name, c, dict(d, note="run with run_local.bat (every phase) so P4, P6 and P7 run before P10"))


# ---------------------------------------------------------------------------

def normal(args, out, scenes, E, tests, sheets):
    """the normal run (the fake Remix runtime)"""
    P = E.get("p10c") or {}
    t_live_states(args, out, scenes, P, tests)
    t_missing(args, out, scenes, P, tests)
    t_log_neutral(args, out, scenes, P, tests)
    t_model(args, out, scenes, P, tests, sheets)
    t_subject(args, out, scenes, P, tests)
    t_timer(args, out, scenes, P, tests)
    t_remix_actuator(args, out, scenes, P, tests)
    t_stock(args, out, scenes, P, tests)
    t_earlier(args, out, tests)


def rtx(args, out, scenes, E, tests, sheets, report):
    """the RTX run (the real RTX Remix runtime): the RTX Remix actuator's
    pulse at the start waits for Remix (about 30 s) and is sent once it runs"""
    import run_p9 as r9
    name = "S10.c1 The RTX Remix actuator with the real runtime (P10.12)"
    R = (E.get("p10c") or {}).get("remix")
    if not R:
        return _fail(tests, name, "the p10c remix scene was made", (E.get("p10c") or {}).get("failed"))
    env = dict(r9.TEXTURE_ENV)
    env["BGE_REMIX_P2_SECONDS"] = "5"
    r = play_logged(args, out, "p10c_remix_rtx", os.path.join(scenes, R["file"] + ".blend"), 120, env)
    ob = R["object"]
    green = ticks_in(r["bricks"], ob, "R_start", "GREEN")
    yellow = ticks_in(r["bricks"], ob, "R_start", "YELLOW")
    c = {"the scene played with Remix": bool(r["ran"]),
         "the pulse at the start was sent once Remix ran (Green again after tic 1)": any(t > 1 for t in green),
         "never Red": not ticks_in(r["bricks"], ob, "R_start", "RED")}
    _put(tests, name, c, {"run": run_summary(r), "green": green, "yellow_tics": len(yellow)})
    report.append("p10c: the RTX Remix actuator's start pulse waited %d tics for Remix (Yellow), sent on tic %s" %
                  (len(yellow), [t for t in green if t > 1][:1]))
