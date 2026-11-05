"""BGE-Remix P9.7 tests: the Remix View (P7's Game View renamed, with a Check
and a Debug mode). Called from run_p9.py:

normal(args, out, scenes, E, tests, sheets)   with the fake runtime (--mock)
    P9.7 Remix View: the rename          Blender lists it as the Remix View
    P9.7 Remix View: Check and Debug     the modes switch; Debug drops the
                                         holograms and shows Remix's picture
    P9.7 Remix View: a debug view        the chosen view and display options
                                         reach Remix (rtx.debugView.*)
    P9.7 Remix View: inspecting a sphere a click selects it; what Remix got
    P9.7 Remix View: the statistics line the line matches the counters
    P9.7 Remix View: a hash collision    a forced collision is a warning on
                                         the Remix layer
rtx(args, out, scenes, E, tests, sheets, report_lines)   on the RTX PC
    P9.7 Remix View: every listed debug view draws and differs from the
    final picture
"""

import os
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE = os.path.join(HERE, "p97_probe.py")
SPHERE = "P9_grey_r050"
SPHERE_ROUGHNESS = 0.5
DEBUG_VIEW = 25            # the probe's choice
DIFF_MIN = 2.0             # a view's mean difference from the final picture (as the P9.0 sweep)


def probe(args, out, tag, blend, parts, env, timeout=900):
    import run_p2 as p2
    import run_p8 as p8
    res = os.path.join(out, tag + "_p97.json")
    if os.path.isfile(res):
        os.remove(res)
    folder = os.path.join(out, tag)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    e = p2.base_env(out, tag)
    e.update(p8.GL_ENV)
    e.update(env)
    e.update({"BGE_REMIX_P97_OUT": res, "BGE_REMIX_P97_PARTS": parts, "BGE_REMIX_P97_DIR": folder,
              "BGE_REMIX_P97_STATS": os.path.join(folder, "view_stats.json")})
    code, text, secs = p2.run([p2.exe(args.ours, "blender"), "--disable-crash-handler"] + p8.WINDOW +
                              [blend, "--python", PROBE], e, os.path.join(out, tag + ".log"), timeout=timeout,
                              cwd=out)
    r = p2.read_json(res) or {}
    r.update({"exit": code, "seconds": round(secs, 1), "folder": folder})
    r["ran"] = code == 0 and bool(r.get("finished"))
    return r


def _fail(tests, name, why, detail=None):
    tests[name] = {"ok": False, "checks": {why: False}, "detail": detail}


def _shots_differ(a, b):
    import run_p8 as p8
    return p8.region_diff(p8.picture(a), p8.picture(b))


def normal(args, out, scenes, E, tests, sheets):
    import run_p2 as p2
    import run_p8 as p8
    import run_p9 as r9
    names = ["P9.7 Remix View: the rename", "P9.7 Remix View: Check and Debug", "P9.7 Remix View: a debug view",
             "P9.7 Remix View: inspecting a sphere", "P9.7 Remix View: the statistics line",
             "P9.7 Remix View: a hash collision"]
    V = E.get("remix_view")
    if not V:
        for n in names:
            _fail(tests, n, "the P9.7 scene was made", E.get("failed"))
        return
    if not getattr(args, "mock", None):
        for n in names:
            _fail(tests, n, "the fake runtime was given (--mock)")
        return
    blend = os.path.join(scenes, V["file"] + ".blend")
    log, env = p8.mock_env(args, out, "p97_ui", os.path.join(out, "cache_p97"))
    env["BGE_REMIX_FORCE_HASH_COLLISION"] = "1"
    env["BGE_REMIX_P97_SPHERE"] = SPHERE
    r = probe(args, out, "p97_ui", blend, "ui", env)
    s = r.get("steps", {})
    lines = r9.mock_lines(log)
    base = {"ran": r["ran"], "errors": r.get("errors"), "exit": r.get("exit")}

    # the rename
    rn = s.get("s_rename") or {}
    c = {
        "the probe ran": r["ran"],
        "the editor-type menu says Remix View": rn.get("area_type_name") == "Remix View",
        "the 3D View's setting says Remix View": (rn.get("texts", {}).get("prop is_game_view") or "")
        .startswith("Remix View |"),
        "its keymap is named Remix View (no Game View keymap)": bool(rn.get("keymap_remix_view")) and
        not rn.get("keymap_game_view", True),
        "no name or tooltip listed says Game View": rn.get("game_view_words") == [],
        "the file opens in a Remix View": rn.get("area_type") == "GAME_VIEW" and bool(rn.get("is_game_view")),
    }
    tests[names[0]] = {"ok": all(c.values()), "checks": c, "detail": dict(base, rename=rn)}

    # the modes
    dbg, back = s.get("s_debug") or {}, s.get("s_back_to_check") or {}
    check_shot = (s.get("s_check_shot") or {}).get("shot")
    debug_shot = (s.get("s_debug_shot") or {}).get("shot")
    diff = _shots_differ(check_shot, debug_shot)
    c = {
        "Debug mode is set": dbg.get("mode") == 'DEBUG',
        "Debug mode shows Remix's picture": dbg.get("background") == 'REMIX',
        "back to Check mode": back.get("mode") == 'CHECK',
        "Debug drops the holograms (its picture differs from Check's)": diff is not None and diff > 0.5,
    }
    tests[names[1]] = {"ok": all(c.values()), "checks": c,
                       "detail": dict(base, debug=dbg, back=back, check_debug_diff=diff)}
    sheets["remix_view_modes"] = p2.contact_sheet(
        [("Check", check_shot), ("Debug, final picture", debug_shot),
         ("Debug, view %d" % DEBUG_VIEW, (s.get("s_debug_view_shot") or {}).get("shot"))],
        os.path.join(out, "sheet_p9_remix_view_modes.png"))

    # a debug view reaches Remix
    dv = s.get("s_debug_view") or {}
    sets = r9.config_sets(lines, "rtx.debugView.debugViewIdx")
    c = {
        "the menu's choice is kept": dv.get("index") == DEBUG_VIEW,
        "it reaches Remix as rtx.debugView.debugViewIdx": str(DEBUG_VIEW) in sets,
        "back in Check mode Remix shows its final picture (0)": bool(sets) and sets[-1] == "0",
        "false colour reaches Remix (pseudoColorMode 1)":
            "1" in r9.config_sets(lines, "rtx.debugView.pseudoColorMode"),
        "the overlay's opacity reaches Remix (0.5)":
            "0.5" in r9.config_sets(lines, "rtx.debugView.overlayOpacity"),
        "Debug mode turns Remix's hash collision detection on":
            "True" in r9.config_sets(lines, "rtx.hashCollisionDetection.enable"),
    }
    tests[names[2]] = {"ok": all(c.values()), "checks": c,
                       "detail": dict(base, chosen=dv, debug_view_sets=sets)}

    # inspecting a sphere
    click, ins = s.get("s_click") or {}, s.get("s_inspect") or {}
    rep = ins.get("inspect") or {}
    mats = rep.get("materials") or []
    m0 = mats[0] if mats else {}
    vals = m0.get("values") or {}
    rough = (vals.get("roughness") or [None])[0]
    match = m0.get("match") or {}
    c = {
        "a click in Debug mode selects the sphere": click.get("active") == SPHERE,
        "Remix's data for it: object and mesh": rep.get("object") == SPHERE and bool(rep.get("mesh")),
        "its triangles and instances": rep.get("triangles", 0) > 0 and rep.get("instances", 0) >= 1,
        "its material": bool(mats) and m0.get("name") == (ins.get("material") or {}).get("name"),
        "its roughness as Remix gets it (%.2f)" % SPHERE_ROUGHNESS:
            rough is not None and abs(rough - SPHERE_ROUGHNESS) < 0.02,
        "each material input with how it got there (Match)":
            bool(match.get("fields")) and all("level" in f and "note" in f for f in match["fields"]),
        "the cache folder": bool(rep.get("cache_folder")),
    }
    tests[names[3]] = {"ok": all(c.values()), "checks": c, "detail": dict(base, click=click, inspect=ins)}

    # the statistics line
    st = s.get("s_stats") or {}
    sf = p2.read_json(st.get("stats_file", "")) if st.get("stats_file") else {}
    line = {k: (text, over) for k, text, over in (st.get("line") or [])}
    t = st.get("targets") or {}
    want = {}
    if sf:
        want = {"draws": "%d draws" % sf.get("last_draws", -1), "lights": "%d lights" % sf.get("last_lights", -1),
                "textures_waiting": "%d textures waiting" % sf.get("textures_waiting", -1),
                "frame_ms": "%.1f ms" % sf.get("frame_ms", -1.0)}
    c = {
        "the counters were written": bool(sf),
        "Remix drew the view (draws counted)": sf.get("view_draws", 0) > 0,
        "the line holds draws, lights, textures waiting and frame time": set(line) == set(want) and bool(want),
        "each figure matches the counters": bool(want) and all(line.get(k, ("",))[0] == v for k, v in want.items()),
        "a figure is red exactly when past its target": bool(t) and bool(sf) and
        line.get("draws", (0, None))[1] == (sf.get("last_draws", 0) > t.get("objects", 0)) and
        line.get("lights", (0, None))[1] == (sf.get("last_lights", 0) > t.get("lights", 0)) and
        line.get("frame_ms", (0, None))[1] == (sf.get("frame_ms", 0.0) > 1000.0 / max(t.get("fps", 60), 1)),
    }
    tests[names[4]] = {"ok": all(c.values()), "checks": c, "detail": dict(base, line=st.get("line"), stats=sf)}

    # a hash collision as a warning
    wr = s.get("s_warnings") or {}
    ws = wr.get("remix_warnings") or []
    hc = [w for w in ws if w["code"] == "RMX_HASH_COLLISION"]
    c = {
        "the forced collisions were counted": ((wr.get("stats") or {}).get("hash_collisions") or 0) > 0,
        "a hash collision warning on the Remix layer": len(hc) == 1,
        "it names what collided": bool(hc) and "the last:" in hc[0]["text"],
    }
    tests[names[5]] = {"ok": all(c.values()), "checks": c, "detail": dict(base, remix_warnings=ws)}


def rtx(args, out, scenes, E, tests, sheets, report_lines):
    import run_p8 as p8
    import run_p9 as r9
    name = "P9.7 Remix View: each listed debug view draws"
    V = E.get("remix_view")
    if not V:
        _fail(tests, name, "the P9.7 scene was made", E.get("failed"))
        return
    blend = os.path.join(scenes, V["file"] + ".blend")
    env = dict(p8.REMIX_ENV)
    env["BGE_REMIX_P97_WAIT"] = "20"
    r = probe(args, out, "p97_views", blend, "views", env, timeout=1800)
    folder = r["folder"]
    final = r9.picture_stats(os.path.join(folder, "rv_0000.png"))
    steps = r.get("steps", {})
    shots = sorted((v["index"], v.get("shot")) for k, v in steps.items() if k.startswith("view_") and v)
    c = {"the probe ran": r["ran"], "the final picture was taken": final is not None and final["std"] >= 2.0}
    rows, items = [], [("final picture", os.path.join(folder, "rv_0000.png"))]
    for idx, path in shots:
        if idx == 0:
            continue
        st = r9.picture_stats(path) if path else None
        diff = float(abs(st["small"] - final["small"]).mean()) if st is not None and final is not None else None
        draws = st is not None and st["std"] >= 2.0
        ok = draws and diff is not None and diff >= DIFF_MIN
        c["view %d draws and differs from the final picture" % idx] = ok
        rows.append({"index": idx, "mean": None if st is None else round(st["mean"], 1),
                     "std": None if st is None else round(st["std"], 1),
                     "diff_from_final": None if diff is None else round(diff, 1), "ok": ok})
        items.append(("view %d" % idx, path))
    c["every listed view was shown"] = len(rows) == len(shots) - 1 and len(rows) > 0
    tests[name] = {"ok": all(c.values()), "checks": c,
                   "detail": {"views": rows, "folder": folder, "errors": r.get("errors"), "exit": r.get("exit")}}
    sheets["remix_view_debug_views"] = r9.thumb_sheet(items, os.path.join(out, "sheet_p9_remix_view_debug_views.png"))
    report_lines += ["", "Remix View, debug views of the spheres (mean difference from the final picture; "
                     "gate %.0f):" % DIFF_MIN]
    for row in rows:
        report_lines.append("  view %3d  %s  diff %s" % (row["index"], "ok    " if row["ok"] else "FAILED",
                                                       row["diff_from_final"]))
