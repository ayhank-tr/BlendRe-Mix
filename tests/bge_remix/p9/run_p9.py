"""BGE-Remix P9: the pipelines (run_local.bat p9; run_p9_rtx.bat) and P9.0's
study (run_local.bat p9study; run_p9_study_rtx.bat; run_p9_menu.bat).

--pipelines (normal, no RTX card needed; P9.1):
  the room saved as P8 saved its four pipeline combinations opens in the
  right pipeline with the right Remix switch; the pipeline setting by hand
  (ACES on the way to RTX Remix); Classic unchanged (against stock 2.79b, the
  look fixes left out); RTX Remix's OpenGL fallback lit by the sky, the sun
  always casting shadows, ACES when set; with the fake Remix runtime, Remix
  on in RTX Remix only, its settings reaching it, the game switching from
  the RTX Remix room to a Classic copy and back; getRenderPipeline and
  isRemixDrawing.
--pipelines-rtx (the real RTX Remix runtime; P9.1): the six spheres and the
  room in the fallback against Remix, with ACES (and Remix's other tone
  mappers) and with the view transform (a progress report, not a gate).

--study (normal, no RTX card needed):
  the study's scenes made; bge.render.remixSetOption checked (False in
  OpenGL, True with the fake Remix runtime, its settings reaching it); a
  short debug-view sweep with the fake runtime; the dry conversion of every
  P0-P8 test file (what P9.3 would make of each material, how each texture
  slot's settings would reach Remix), nothing changed.
--rtx (the real RTX Remix runtime):
  the six spheres in the OpenGL fallback against Remix, sphere by sphere;
  Remix's debug views 1 to 1,000 in turn (which ones draw a picture of
  their own), a picture sheet of them; Remix's own menu switched on, a
  picture of it.
--menu (by hand): the spheres in the player with Remix and Remix's own menu
  on, until you close the window.
"""

import argparse
import glob
import hashlib
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p8"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
import run_p2 as p2  # noqa: E402  (running Blender, contact sheets)
import run_p8 as p8  # noqa: E402  (the player, pictures, the fake runtime)

DV_FIRST, DV_LAST = 1, 1000
DV_START, DV_STEP = 300, 3      # the first debug view at frame 300, one every 3 frames
DV_SIZE = (480, 270)            # the sweep's window (a picture per view)
TOL_SPHERE = 10.0               # a sphere's mean colour, fallback against Remix (P8.30's match)


def make_scenes(ours, out):
    scenes = os.path.join(out, "scenes")
    shutil.rmtree(scenes, ignore_errors=True)
    os.makedirs(scenes, exist_ok=True)
    code, text, _ = p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup", "--python",
                            os.path.join(HERE, "make_scenes_p9.py"), "--", scenes],
                           p2.base_env(out, "make"), os.path.join(out, "make_scenes_p9.log"), timeout=900)
    expected = p2.read_json(os.path.join(scenes, "p9_expected.json")) or {}
    if code != 0 or not expected.get("spheres") or expected.get("failed"):
        print("\n".join((text or "").splitlines()[-40:]))
        raise SystemExit("making the P9 study scenes failed, see make_scenes_p9.log")
    return scenes, expected


def study_play(ours, out, tag, blend, mode, env=None, frames=300, size=None, debug=False):
    """the player with the study's logic in a mode; its own result file"""
    res = os.path.join(out, tag + "_p9.json")
    if os.path.isfile(res):
        os.remove(res)
    e = {"BGE_REMIX_P9_MODE": mode, "BGE_REMIX_P9_OUT": res}
    e.update(env or {})
    was = p8.SIZE
    if size:
        p8.SIZE = size
    try:
        r = p8.play(ours, out, tag, blend, e, frames=frames, debug=debug)
    finally:
        p8.SIZE = was
    r["p9"] = p2.read_json(res) or {}
    r.pop("text", None)
    return r


def mock_lines(log):
    return p2.read_lines(log) if os.path.isfile(log) else []


def config_sets(lines, key):
    return [l.split("=", 1)[1] for l in lines if l.startswith("Config %s=" % key)]


# ---------------------------------------------------------------------------
# the dry conversion

def test_files(results):
    """every P0-P8 test scene (the phases' scene folders), no copies made
    for single runs"""
    found = []
    for phase in ["p%d" % i for i in range(9)]:
        for path in glob.glob(os.path.join(results, phase, "**", "*.blend"), recursive=True):
            low = path.lower().replace("\\", "/")
            if "/user_" in low or "/cache" in low or "/tmp" in low:
                continue
            found.append(path)
    return sorted(set(found))


def dry_conversion(ours, out, results):
    files = test_files(results)
    target = os.path.join(out, "dry_conversion.json")
    if os.path.isfile(target):
        os.remove(target)
    listing = os.path.join(out, "dry_files.txt")
    with open(listing, "w", encoding="utf-8") as f:
        f.write("\n".join(files) + "\n")
    data = {"files": {}, "failed": {}}
    # (in groups: one Blender per 40 files keeps a failure to one group)
    for k in range(0, len(files), 40):
        part = os.path.join(out, "dry_conversion_%03d.json" % (k // 40))
        code, text, _ = p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup", "--python",
                                os.path.join(HERE, "dry_convert_p9.py"), "--", part] + files[k:k + 40],
                               p2.base_env(out, "dry"), os.path.join(out, "dry_conversion_%03d.log" % (k // 40)),
                               timeout=1800)
        got = p2.read_json(part) or {}
        data["files"].update(got.get("files", {}))
        data["failed"].update(got.get("failed", {}))
        for path in files[k:k + 40]:
            if path not in got.get("files", {}) and path not in got.get("failed", {}):
                data["failed"][path] = "not reached (exit %s)" % code
    with open(target, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    return summarize_dry(data, results)


def summarize_dry(data, results):
    kinds, inputs, ways, reasons, today = {}, {}, {}, {}, {"baked today": 0, "exactly today": 0}
    per_file = []
    seen = set()
    for path, f in sorted(data["files"].items()):
        rel = os.path.relpath(path, results)
        mats = f.get("materials", [])
        per_file.append((rel, len(mats), sum(1 for m in mats if m["becomes"] == "RTX Remix")))
        for m in mats:
            key = (m["name"], m["kind"], json.dumps(m.get("inputs"), sort_keys=True),
                   json.dumps([s["way"] for s in m["slots"]]))
            unique = key not in seen
            seen.add(key)
            label = "%s -> %s%s" % (m["kind"], m["becomes"], " (Remix block in use)" if m["remix_block"] else "")
            k = kinds.setdefault(label, [0, 0])
            k[0] += 1
            k[1] += 1 if unique else 0
            if not unique:
                continue
            for field, state in (m.get("inputs") or {}).items():
                inputs.setdefault(state, 0)
                inputs[state] += 1
            for s in m["slots"]:
                ways[s["way"]] = ways.get(s["way"], 0) + 1
                for r in s["reasons"]:
                    reasons[r] = reasons.get(r, 0) + 1
            t = m.get("today") or {}
            if t.get("baked"):
                today["baked today"] += 1
            if t and not t.get("baked") and t.get("not at all", 1) == 0 and t.get("approximately", 1) == 0:
                today["exactly today"] += 1
    return {"files": len(data["files"]), "failed": data["failed"], "kinds": kinds, "node_inputs": inputs,
            "slot_ways": ways, "bake_reasons": dict(sorted(reasons.items(), key=lambda kv: -kv[1])),
            "today": today, "per_file": per_file}


def dry_lines(s):
    lines = ["Dry conversion: %d test files read, %d could not be (nothing changed)" % (s["files"], len(s["failed"]))]
    for path, why in list(s["failed"].items())[:10]:
        lines.append("    could not read %s: %s" % (path, why.strip().splitlines()[-1][:100]))
    lines.append("  Materials (all uses / different ones):")
    for label, (n, u) in sorted(s["kinds"].items(), key=lambda kv: -kv[1][0]):
        lines.append("    %-62s %5d / %d" % (label, n, u))
    lines.append("  Node materials' Remix inputs (different materials): " +
                 ", ".join("%s %d" % kv for kv in sorted(s["node_inputs"].items())))
    lines.append("  Texture slots, how their settings would reach Remix (different materials):")
    for way, n in sorted(s["slot_ways"].items(), key=lambda kv: -kv[1]):
        lines.append("    %-30s %5d" % (way, n))
    lines.append("  Why slots would be baked:")
    for r, n in list(s["bake_reasons"].items())[:20]:
        lines.append("    %-50s %5d" % (r, n))
    lines.append("  Today (P8's translator, different materials): %d baked, %d reaching Remix exactly" %
                 (s["today"]["baked today"], s["today"]["exactly today"]))
    return lines


# ---------------------------------------------------------------------------
# the normal study run

def run_study(args, out):
    scenes, E = make_scenes(args.ours, out)
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    tests = {}
    c, d = {}, {}
    # remixSetOption in OpenGL: False
    r = study_play(args.ours, out, "api_opengl", S("p9_study_spheres"), "api", frames=200)
    d["opengl"] = r["p9"]
    c["the spheres play in OpenGL"] = bool(r["ran"])
    c["remixSetOption says False when Remix is not drawing"] = r["p9"].get("on") is False
    # with the fake runtime: True, and the setting reaches it
    cache = os.path.join(out, "cache_api")
    log, env = p8.mock_env(args, out, "api_mock", cache)
    r = study_play(args.ours, out, "api_mock", S("p9_study_spheres_remix"), "api", env=env, frames=200)
    lines = mock_lines(log)
    sets = config_sets(lines, "rtx.debugView.debugViewIdx")
    d["mock"] = {"result": r["p9"], "settings reaching the runtime": sets}
    c["the spheres play with the fake Remix runtime"] = bool(r["ran"])
    c["remixSetOption says True while Remix draws"] = r["p9"].get("on") is True
    c["the setting reaches Remix (2, then 0)"] = sets[-2:] == ["2", "0"]
    # a short sweep with the fake runtime: the study's machinery
    dv = os.path.join(out, "sweep_mock")
    shutil.rmtree(dv, ignore_errors=True)
    os.makedirs(dv)
    log, env = p8.mock_env(args, out, "sweep_mock", os.path.join(out, "cache_sweep"))
    env.update({"BGE_REMIX_P9_DIR": dv, "BGE_REMIX_P9_FIRST": "1", "BGE_REMIX_P9_LAST": "5",
                "BGE_REMIX_P9_START": "120"})
    r = study_play(args.ours, out, "sweep_mock", S("p9_study_spheres_remix"), "sweep", env=env,
                   frames=120 + DV_STEP * 7 + 20, size=DV_SIZE)
    shots = sorted(os.listdir(dv))
    sets = config_sets(mock_lines(log), "rtx.debugView.debugViewIdx")
    d["sweep_mock"] = {"pictures": shots, "settings": sets, "done": r["p9"].get("done")}
    c["a short sweep sets each view and takes a picture of it (0 to 5)"] = \
        len(shots) == 6 and sets[:6] == [str(i) for i in range(6)] and bool(r["p9"].get("done"))
    tests["S9.1 remixSetOption"] = {"ok": all(c.values()), "checks": c, "detail": d}
    # the dry conversion
    summary = dry_conversion(args.ours, out, args.results)
    c = {"every P0-P8 test file read (%d)" % summary["files"]: summary["files"] > 0 and not summary["failed"]}
    tests["S9.4 Dry conversion"] = {"ok": all(c.values()), "checks": c, "detail": summary}
    res = {"tests": tests, "all_ok": all(t["ok"] for t in tests.values())}
    lines = ["BGE-Remix P9.0 study (normal run)", "=" * 70, ""]
    for name, t in tests.items():
        lines.append("%-34s %s" % (name, p2.mark(t["ok"])))
        for check, ok in t["checks"].items():
            lines.append("      %s %s" % ("ok    " if ok else "FAILED", check))
    lines += [""] + dry_lines(summary) + ["", "Details: results_p9_study.json, dry_conversion.json", ""]
    lines.append("ALL PASSED" if res["all_ok"] else "SOMETHING FAILED (see results_p9_study.json and the logs)")
    return res, lines, "p9_study"


# ---------------------------------------------------------------------------
# P9.1: the pipelines (normal run)

TOL_CLASSIC = 3.0      # Classic against stock 2.79b (the same GLSL code)
TOL_SAME = 0.5         # two files that must draw the same picture
SKY_GAIN = 3.0         # the sky's light on a wall or the floor (levels, at least)
TOL_SKY_NONE = 1.0     # Classic: the sky lights nothing
STOCK_SURFACES = ("floor", "back_wall", "left_wall", "right_wall", "table")

OPEN_CHECK = r'''
import bpy, json, os, sys
argv = sys.argv[sys.argv.index("--") + 1:]
target, folder, names = argv[0], argv[1], argv[2:]
r = {"files": {}}

def state(sc):
    gs = sc.game_settings
    remix = getattr(sc, "bge_remix", None)
    if remix is None:
        remix = "RTX_REMIX" if sc.get("bge_remix") else "OPENGL"
    return {"pipeline": gs.render_pipeline, "remix": remix, "tone": gs.remix_tone_mapping,
            "stored_pipeline": sc.get("bge_remix_pipeline"), "stored_tone": sc.get("bge_remix_tonemap")}

def path(n):
    return os.path.join(folder, n + ".blend")

for n in names:
    bpy.ops.wm.open_mainfile(filepath=path(n))
    r["files"][n] = state(bpy.context.scene)
# the pipeline set by hand: ACES on the way to RTX Remix, kept on the way back
bpy.ops.wm.open_mainfile(filepath=path("p9_pipe_classic_gl"))
sc = bpy.context.scene
sc.game_settings.render_pipeline = 'RTX_REMIX'
r["to_rtx"] = state(sc)
sc.game_settings.render_pipeline = 'CLASSIC'
r["to_classic"] = state(sc)
r["new_scene"] = state(bpy.data.scenes.new("P9New"))
# a P8 file moved to RTX Remix on opening, put back in Classic by hand,
# saved and opened again: it stays Classic
bpy.ops.wm.open_mainfile(filepath=path("p9_pipe_classic_remix"))
bpy.context.scene.game_settings.render_pipeline = 'CLASSIC'
again = path("p9_pipe_roundtrip")
bpy.ops.wm.save_as_mainfile(filepath=again, copy=True)
bpy.ops.wm.open_mainfile(filepath=again)
r["roundtrip"] = state(bpy.context.scene)
with open(target, "w") as f:
    json.dump(r, f, indent=1)
print("P9-OPEN done")
'''


def pipe_play(build, out, tag, blend, env=None, frames=170):
    """the player, the room's own answers (getRenderPipeline,
    isRemixDrawing at frames 30, 90 and 150)"""
    res = os.path.join(out, tag + "_pipe.json")
    if os.path.isfile(res):
        os.remove(res)
    e = {"BGE_REMIX_P9_PIPE_OUT": res}
    e.update(env or {})
    r = p8.play(build, out, tag, blend, e, frames=frames)
    r["pipe"] = (p2.read_json(res) or {}).get("samples", {})
    return r


def samples_say(r, pipeline, drawing):
    got = r.get("pipe") or {}
    return bool(got) and all(v.get("pipeline") == pipeline and v.get("drawing") is drawing for v in got.values())


def run_pipelines(args, out):
    scenes, E = make_scenes(args.ours, out)
    P = E.get("pipelines")
    if not P:
        raise SystemExit("making the P9.1 room failed, see make_scenes_p9.log")
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    tests, sheets = {}, {}
    # T9.1: opening the files; the setting by hand
    names = [f["file"] for f in P["files"]]
    target = os.path.join(out, "open_check.json")
    if os.path.isfile(target):
        os.remove(target)
    p8.blender_script(args.ours, out, "p9_open", OPEN_CHECK, [target, scenes] + names)
    R = p2.read_json(target) or {}
    c, d = {}, {"opened": R.get("files"), "by_hand": {k: R.get(k) for k in ("to_rtx", "to_classic", "new_scene",
                                                                              "roundtrip")}}
    for f in P["files"]:
        got = (R.get("files") or {}).get(f["file"]) or {}
        want = f["opens"]
        c["%s opens as %s, Remix %s, %s" % (f["file"], want[0], want[1], want[2])] = \
            [got.get("pipeline"), got.get("remix"), got.get("tone")] == want
    t = R.get("to_rtx") or {}
    c["Classic moved to RTX Remix by hand: ACES"] = t.get("pipeline") == "RTX_REMIX" and t.get("tone") == "ACES"
    t = R.get("to_classic") or {}
    c["and back to Classic: Classic, ACES kept"] = t.get("pipeline") == "CLASSIC" and t.get("tone") == "ACES"
    t = R.get("new_scene") or {}
    c["a new scene starts in RTX Remix (the view transform)"] = \
        t.get("pipeline") == "RTX_REMIX" and t.get("tone") == "VIEW_TRANSFORM"
    t = R.get("roundtrip") or {}
    c["a P8 file put back in Classic by hand stays Classic when saved and opened again"] = \
        t.get("pipeline") == "CLASSIC"
    tests["T9.1 Old files and the Pipeline setting"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # the OpenGL pictures
    gl = {}
    for name in ("p9_pipe_classic_gl", "p9_pipe_classic_gl_shadow", "p9_pipe_classic_gl_nosky",
                 "p9_pipe_modern_gl", "p9_pipe_modern_gl_shadow", "p9_pipe_modern_gl_nosky", "p9_pipe_aces_gl"):
        gl[name] = pipe_play(args.ours, out, name, S(name))
    pic = {k: p8.shot_of(r) for k, r in gl.items()}
    rects = P["rects"]
    surfaces = ("floor", "back_wall", "left_wall", "right_wall")

    # T9.2: Classic unchanged
    c, d = {}, {}
    c["the Classic files played"] = all(gl[k]["ran"] for k in gl if "classic" in k)
    c["getRenderPipeline says CLASSIC, isRemixDrawing False"] = all(
        samples_say(gl[k], "CLASSIC", False) for k in gl if "classic" in k)
    if args.official:
        stock = p8.play(args.official, out, "stock_p9_pipe_classic_gl", S("p9_pipe_classic_gl"))
        # the room's surfaces (P8 draws the world's sky in Classic, stock
        # 2.79b its horizon colour: the sky is P8's T8.22; the ball is
        # Physical, which stock does not know)
        sp = p8.shot_of(stock)
        md = {k: p8.region_diff(p8.blurred(pic["p9_pipe_classic_gl"]), p8.blurred(sp), rects[k])
              if pic["p9_pipe_classic_gl"] is not None and sp is not None else None for k in STOCK_SURFACES}
        d["against stock 2.79b (walls, floor, table)"] = md
        c["Classic as stock 2.79b draws the room (each surface within %.0f)" % TOL_CLASSIC] = \
            all(v is not None and v <= TOL_CLASSIC for v in md.values())
        sheets["classic_stock"] = p2.contact_sheet([("ours, Classic", gl["p9_pipe_classic_gl"].get("screenshot")),
                                                    ("stock 2.79b", stock.get("screenshot"))],
                                                   os.path.join(out, "sheet_p9_classic_stock.png"))
    sky = {k: p8.region_diff(pic["p9_pipe_classic_gl"], pic["p9_pipe_classic_gl_nosky"], rects[k]) for k in surfaces}
    d["Classic, sky against black sky (walls, floor)"] = sky
    c["Classic: the sky lights nothing"] = all(v is not None and v <= TOL_SKY_NONE for v in sky.values())
    md = p8.whole_diff(pic["p9_pipe_classic_gl"], pic["p9_pipe_classic_gl_shadow"])
    d["Classic, the sun's shadow switched on"] = md
    c["Classic: the sun casts a shadow only when it is set to (%s)" % md] = md is not None and md > 2 * TOL_SAME
    tests["T9.2 Classic unchanged"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # T9.3: RTX Remix's OpenGL fallback
    c, d = {}, {}
    c["the RTX Remix files played"] = all(gl[k]["ran"] for k in gl if "classic" not in k)
    c["getRenderPipeline says RTX_REMIX, isRemixDrawing False (Remix off)"] = all(
        samples_say(gl[k], "RTX_REMIX", False) for k in gl if "classic" not in k)
    a, b = pic["p9_pipe_modern_gl"], pic["p9_pipe_modern_gl_nosky"]
    gain = {}
    for k in surfaces:
        if a is None or b is None:
            gain[k] = None
        else:
            gain[k] = round(float(p8.crop(a, rects[k]).mean() - p8.crop(b, rects[k]).mean()), 1)
    d["the sky's light (levels added, walls and floor)"] = gain
    c["the sky lights the room (each surface at least %.0f brighter)" % SKY_GAIN] = \
        all(v is not None and v >= SKY_GAIN for v in gain.values())
    md = p8.whole_diff(pic["p9_pipe_modern_gl"], pic["p9_pipe_modern_gl_shadow"])
    d["the sun's shadow switched on as well"] = md
    c["the sun casts its shadow anyway (the same picture: %s)" % md] = md is not None and md <= TOL_SAME
    md = p8.whole_diff(pic["p9_pipe_modern_gl"], pic["p9_pipe_classic_gl"])
    d["against Classic"] = md
    md = p8.whole_diff(pic["p9_pipe_modern_gl"], pic["p9_pipe_aces_gl"])
    d["ACES against the view transform"] = md
    text = lambda k: gl[k].get("text") or ""  # noqa: E731
    c["ACES when set (the picture changes: %s)" % md] = md is not None and md > 2.0 and \
        "tone mapping ACES" in text("p9_pipe_aces_gl")
    c["P8's files keep the view transform"] = "tone mapping ACES" not in text("p9_pipe_modern_gl")
    tests["T9.3 RTX Remix's OpenGL fallback"] = {"ok": all(c.values()), "checks": c, "detail": d}
    sheets["pipelines"] = p2.contact_sheet(
        [(k.replace("p9_pipe_", ""), gl[k].get("screenshot")) for k in gl], os.path.join(out, "sheet_p9_pipelines.png"))

    # T9.4: the fake Remix runtime
    c, d = {}, {}
    runs = {}
    for name in ("p9_pipe_classic_remix", "p9_pipe_modern_remix", "p9_pipe_aces_remix", "p9_pipe_classic_kept"):
        log, env = p8.mock_env(args, out, name + "_mock", os.path.join(out, "cache_" + name))
        r = pipe_play(args.ours, out, name + "_mock", S(name), env)
        r["lines"] = mock_lines(log)
        runs[name] = r
        d[name] = {"running": (r.get("stats") or {}).get("running"), "samples": r.get("pipe")}
    for name in ("p9_pipe_classic_remix", "p9_pipe_modern_remix", "p9_pipe_aces_remix"):
        r = runs[name]
        c["%s: Remix draws it" % name] = bool(r["ran"]) and (r.get("stats") or {}).get("running") is True and \
            samples_say(r, "RTX_REMIX", True)
    r = runs["p9_pipe_classic_kept"]
    c["p9_pipe_classic_kept (Classic, the switch left on): OpenGL draws it"] = bool(r["ran"]) and \
        (r.get("stats") or {}).get("running") is not True and samples_say(r, "CLASSIC", False)
    lines = runs["p9_pipe_aces_remix"]["lines"]
    sent = {k: config_sets(lines, k)[-1:] for k in ("rtx.tonemappingMode", "rtx.tonemap.finalizeWithACES",
                                                    "rtx.pathMaxBounces")}
    d["ACES file: settings sent"] = sent
    c["ACES: Remix's global tone mapper finished with ACES"] = sent["rtx.tonemappingMode"] == ["0"] and \
        sent["rtx.tonemap.finalizeWithACES"] == ["True"]
    c["a curated setting reaches Remix (Max Bounces 8)"] = sent["rtx.pathMaxBounces"] == ["8"]
    lines = runs["p9_pipe_modern_remix"]["lines"]
    c["the view transform: Remix's own tone mapping left alone"] = \
        not config_sets(lines, "rtx.tonemappingMode") and not config_sets(lines, "rtx.tonemap.finalizeWithACES")
    # the RTX Remix room, its Classic copy, the room again
    log, env = p8.mock_env(args, out, "p9_pipe_switch_mock", os.path.join(out, "cache_switch"))
    env["BGE_REMIX_P9_PIPE_SWITCH"] = "1"
    r = pipe_play(args.ours, out, "p9_pipe_switch_mock", S(P["switch"]), env)
    got = r.get("pipe") or {}
    d["switch"] = got
    want = {"30": ["P9Room", "RTX_REMIX", True], "90": ["P9RoomClassic", "CLASSIC", False],
            "150": ["P9Room", "RTX_REMIX", True]}
    c["the game switches Remix off for the Classic copy and on again for the room"] = bool(r["ran"]) and all(
        [(got.get(k) or {}).get(x) for x in ("scene", "pipeline", "drawing")] == v for k, v in want.items())
    c["no crash, no Python error"] = all(not x.get("crashed") and not x.get("traceback_in_output")
                                         for x in list(runs.values()) + [r])
    tests["T9.4 Remix on and off (the fake runtime)"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # P9.2
    p92_tests(args, out, scenes, E, tests, sheets)

    res = {"tests": tests, "sheets": sheets, "all_ok": all(t["ok"] for t in tests.values())}
    lines = ["BGE-Remix P9.1-P9.2: Classic and RTX Remix, the RTX Remix Material node (normal run)", "=" * 70, ""]
    for name, t in tests.items():
        lines.append("%-44s %s" % (name, p2.mark(t["ok"])))
        for check, ok in t["checks"].items():
            lines.append("      %s %s" % ("ok    " if ok else "FAILED", check))
    lines += ["", "Picture sheets:"] + ["  %-14s %s" % kv for kv in sheets.items()]
    lines += ["", "ALL PASSED" if res["all_ok"] else "SOMETHING FAILED (see results_p9.json and the logs)"]
    return res, lines, "p9"


# ---------------------------------------------------------------------------
# P9.2: the RTX Remix Material node

TOL_TWIN = 0.5      # a sphere as the same material with P8's Physical surface
TOL_F12_NODE = 6.0  # F12 against the game (P8's)
TOL_GATE = 10.0     # the fallback against Remix (P8's), every sphere but glass and thin film
F12_OUT = ("glass", "alpha_cutout")  # raytraced glass, soft-edged cut-out in F12

MATCH_CHECK = r'''
import bpy, json, os, sys
argv = sys.argv[sys.argv.index("--") + 1:]
target, folder = argv[0], argv[1]
r = {}
try:
    import bge_remix
except ImportError:
    bge_remix = None
from bl_operators.bge_remix_material import kind, remix_node

def node_state(ma):
    node = remix_node(ma)
    s = {"kind": kind(ma), "use_nodes": ma.use_nodes, "has_tree": ma.node_tree is not None,
         "node": node is not None}
    if node is not None:
        s["values"] = {sock.name: (list(sock.default_value) if hasattr(sock.default_value, "__len__")
                                   else sock.default_value) for sock in node.inputs}
        s["links"] = {}
        for link in ma.node_tree.links:
            if link.to_node == node:
                s["links"][link.to_socket.name] = [link.from_node.bl_idname, link.from_socket.name,
                                                    getattr(getattr(link.from_node, "texture", None), "name", None)]
        out = next((n for n in ma.node_tree.nodes if n.bl_idname == "ShaderNodeOutput"), None)
        # P9.2: the node is the output; the Output node, unwired, holds the
        # albedo for stock 2.79
        s["output_unwired"] = out is not None and not out.inputs["Color"].is_linked
        s["output_color"] = list(out.inputs["Color"].default_value)[:3] if out is not None else None
        s["albedo"] = list(node.inputs["Albedo"].default_value)[:3]
        s["remix_type"] = node.remix_type
        s["blend"] = node.blend
    if bge_remix is not None:
        try:
            s["match"] = bge_remix.material_match(ma)
        except Exception as ex:
            s["match"] = {"error": str(ex)}
    return s

for name in argv[2:]:
    bpy.ops.wm.open_mainfile(filepath=os.path.join(folder, name + ".blend"))
    r[name] = {ma.name: node_state(ma) for ma in bpy.data.materials if ma.users and ma.name.startswith("P9R")}
with open(target, "w") as f:
    json.dump(r, f, indent=1)
print("P9-MATCH done")
'''


def mock_materials(lines):
    """the fake runtime's CreateMaterial lines, as dicts"""
    mats = []
    for l in lines:
        if not l.startswith("CreateMaterial "):
            continue
        d = {}
        for part in l.split(" ")[1:]:
            if "=" in part:
                k, v = part.split("=", 1)
                d[k] = v
        mats.append(d)
    return mats


def srgb(v):
    """Remix's colour constants are sRGB values (the bridge encodes Blender's
    linear ones, bridge_scene.cpp srgb3)"""
    def one(c):
        c = min(max(c, 0.0), 1.0)
        return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1.0 / 2.4) - 0.055
    return [one(c) for c in v]


def near(text, want, tol=0.002):
    try:
        got = [float(x) for x in text.split(",")]
    except (ValueError, AttributeError):
        return False
    want = want if isinstance(want, (list, tuple)) else [want]
    return len(got) == len(want) and all(abs(a - b) <= tol for a, b in zip(got, want))


def sphere_found(mats, label, e, film_nm):
    """the sphere's material among the fake runtime's, by what sets it apart"""
    what = e["what"]
    tex = lambda m, key, name: name in m.get(key, "")  # noqa: E731
    for m in mats:
        if what == 'image' and tex(m, "albedo_tex", "P9RS_checker") and m.get("alphatest") == "0":
            return m
        if what == 'cutout' and tex(m, "albedo_tex", "P9RS_cutout") and m.get("alphatest") == "1":
            return m
        if what == 'normal' and tex(m, "normal_tex", "P9RS_bumps"):
            return m
        if what == 'glass' and m.get("type") == "translucent" and near(m.get("ior"), e["ior"]):
            return m
        if what == 'film' and m.get("thinfilm") == "1" and near(m.get("thinfilm_nm"), film_nm, 0.01):
            return m
        if what == 'height' and tex(m, "height_tex", "P9RS_heights") and near(m.get("displace_in"), 0.05):
            return m
        if what is None and m.get("type") == "opaque" and near(m.get("albedo"), srgb(e["albedo"])) and \
                near(m.get("roughness"), e["roughness"]) and near(m.get("metallic"), e["metallic"]):
            if e["intensity"] > 0.0:
                if near(m.get("intensity"), e["intensity"]) and near(m.get("emissive"), srgb(e["emission"])):
                    return m
            else:
                return m
    return None


def rect_diffs(a, b, rects, labels, blur=False):
    out = {}
    for k in labels:
        if a is None or b is None or a.shape != b.shape:
            out[k] = None
            continue
        aa, bb = (p8.blurred(a), p8.blurred(b)) if blur else (a, b)
        out[k] = p8.region_diff(aa, bb, rects[k])
    return out


def srgb_to_linear(v):
    c = v / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def aces_inverse(y):
    """the fallback's ACES curve (RAS_Pipeline.cpp, Narkowicz's fit) undone"""
    aces = lambda x: max(0.0, min(1.0, (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)))  # noqa: E731
    lo, hi = 0.0, 64.0
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        if aces(mid) < y:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def p92_tests(args, out, scenes, E, tests, sheets):
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    R, B = E.get("remix_spheres"), E.get("remix_build")
    if not R or not B:
        tests["T9.5 The RTX Remix Material node"] = {"ok": False, "checks": {"the P9.2 scenes were made": False},
                                                     "detail": E.get("failed")}
        return
    order, rects = R["order"], R["rects"]
    # the node's view of things (Blender, background)
    target = os.path.join(out, "match_check.json")
    if os.path.isfile(target):
        os.remove(target)
    p8.blender_script(args.ours, out, "p9_match", MATCH_CHECK,
                      [target, scenes, "p9_rs_nodes", "p9_rb_before", "p9_rb_after", "p9_rb_back"])
    M = p2.read_json(target) or {}

    # T9.5: the node draws as the Physical surface, F12 reads it, Remix gets it
    c, d = {}, {}
    twin = pipe_play(args.ours, out, "p9_rs_physical", S("p9_rs_physical"))
    node = pipe_play(args.ours, out, "p9_rs_nodes", S("p9_rs_nodes"))
    c["the spheres played (Physical twins and RTX Remix materials)"] = bool(twin["ran"] and node["ran"])
    diffs = rect_diffs(p8.shot_of(twin), p8.shot_of(node), rects, order)
    d["against the Physical twins"] = diffs
    for k in order:
        c["%s: as its Physical twin (within %.1f: %s)" % (k, TOL_TWIN, diffs[k])] = diffs[k] is not None and \
            diffs[k] <= TOL_TWIN
    dark = pipe_play(args.ours, out, "p9_rs_nodes_dark", S("p9_rs_nodes_dark"))
    f12 = os.path.join(out, "p9_rs_nodes_dark_f12.png")
    if os.path.isfile(f12):
        os.remove(f12)
    p8.blender_script(args.ours, out, "p9_f12", p8.RENDER, [f12], blend=S("p9_rs_nodes_dark"), timeout=1800)
    fd = rect_diffs(p8.shot_of(dark), p8.picture(f12), rects, order, blur=True)
    d["F12 against the game (black sky)"] = fd
    c["F12 rendered the RTX Remix materials"] = os.path.isfile(f12)
    for k in order:
        if k in F12_OUT:
            continue
        c["%s: F12 as the game (within %.0f: %s)" % (k, TOL_F12_NODE, fd[k])] = fd[k] is not None and \
            fd[k] <= TOL_F12_NODE
    log, env = p8.mock_env(args, out, "p9_rs_nodes_mock", os.path.join(out, "cache_rs"))
    r = pipe_play(args.ours, out, "p9_rs_nodes_mock", S("p9_rs_nodes_remix"), env)
    mats = mock_materials(mock_lines(log))
    d["the fake runtime's materials"] = len(mats)
    c["Remix drew the RTX Remix materials (the fake runtime)"] = bool(r["ran"]) and \
        (r.get("stats") or {}).get("running") is True
    found = {}
    for k in order:
        m = sphere_found(mats, k, R["spheres"][k], R["thin_film_nm"])
        found[k] = m
        c["%s: Remix got its inputs" % k] = m is not None
    d["materials found"] = {k: (v and {x: v.get(x) for x in ("type", "albedo", "roughness", "metallic", "intensity",
                                                             "albedo_tex", "normal_tex", "height_tex")})
                            for k, v in found.items()}
    nodes = M.get("p9_rs_nodes") or {}
    match_ok = {}
    for k in order:
        st = nodes.get(R["spheres"][k]["material"]) or {}
        mt = st.get("match") or {}
        ok = st.get("kind") == "RTX_REMIX" and mt.get("source") == 5
        fields = {f["field"]: f for f in mt.get("fields", [])}
        if k == "thin_film":
            ok = ok and "Remix only" in (fields.get("thin film") or {}).get("note", "")
        if k == "height":
            ok = ok and "Remix only" in (fields.get("height") or {}).get("note", "")
        if k == "albedo_image":
            ok = ok and "P9RS_checker" in (fields.get("albedo") or {}).get("note", "")
        match_ok[k] = ok
        c["%s: Match reads the node" % k] = ok
    d["match"] = {k: (nodes.get(R["spheres"][k]["material"]) or {}).get("match") for k in order}
    tests["T9.5 The RTX Remix Material node"] = {"ok": all(c.values()), "checks": c, "detail": d}
    sheets["remix_spheres"] = p2.contact_sheet([("P8 Physical twins", twin.get("screenshot")),
                                                ("RTX Remix materials", node.get("screenshot")),
                                                ("black sky, game", dark.get("screenshot")),
                                                ("black sky, F12", f12 if os.path.isfile(f12) else None)],
                                               os.path.join(out, "sheet_p9_remix_spheres.png"))

    # T9.6: picking RTX Remix, and Classic again
    c, d = {}, {}
    mn = B["materials"]
    after = M.get("p9_rb_after") or {}
    back = M.get("p9_rb_back") or {}
    st = {k: after.get(v) or {} for k, v in mn.items()}
    d["after"] = {k: {x: v.get(x) for x in ("kind", "links", "remix_type", "blend")} for k, v in st.items()}
    for k, v in st.items():
        c["%s: a node tree, its RTX Remix Material node the output (the Output node holds the albedo)" % k] = \
            v.get("kind") == "RTX_REMIX" and bool(v.get("output_unwired")) and v.get("output_color") is not None \
            and all(abs(a - b) < 1e-3 for a, b in zip(v["output_color"], v.get("albedo") or []))
    vals = lambda k: st[k].get("values") or {}  # noqa: E731
    cc = B["classic_colour"]
    c["classic_colour: its colour and hardness copied (albedo, roughness)"] = \
        near(",".join(str(x) for x in vals("classic_colour").get("Albedo", [])[:3]), cc["albedo"], 0.002) and \
        abs(vals("classic_colour").get("Roughness", -1) - cc["roughness"]) < 0.002
    links = st["classic_images"].get("links") or {}
    c["classic_images: its images wired (colour, normal, alpha)"] = \
        (links.get("Albedo") or [None, None])[1] == "Color" and (links.get("Normal") or [None, None])[1] == "Color" \
        and (links.get("Opacity") or [None, None])[1] == "Value"
    ph = B["physical"]
    c["physical: its values copied (albedo, metallic, roughness)"] = \
        near(",".join(str(x) for x in vals("physical").get("Albedo", [])[:3]), ph["albedo"], 0.002) and \
        abs(vals("physical").get("Metallic", -1) - ph["metallic"]) < 0.002 and \
        abs(vals("physical").get("Roughness", -1) - ph["roughness"]) < 0.002
    c["physical_proc: its procedural wired to the roughness"] = "Roughness" in (st["physical_proc"].get("links") or {})
    mt = st["physical_proc"].get("match") or {}
    c["physical_proc: the procedural reaches Remix baked (Match)"] = bool(int(mt.get("bake_slots", 0)) & 2)
    bk = {k: back.get(v) or {} for k, v in mn.items()}
    for k in ("classic_colour", "classic_images"):
        c["%s: back to Classic, its tree kept" % k] = bk[k].get("kind") == "CLASSIC" and bool(bk[k].get("has_tree"))
    runs = {n: pipe_play(args.ours, out, n, S(n)) for n in ("p9_rb_before", "p9_rb_after", "p9_rb_back")}
    pic = {n: p8.shot_of(r) for n, r in runs.items()}
    brects = B["rects"]
    md = p8.region_diff(pic["p9_rb_before"], pic["p9_rb_after"], brects["physical"])
    d["physical: before against after"] = md
    c["physical: looks the same after (within %.1f: %s)" % (TOL_TWIN, md)] = md is not None and md <= TOL_TWIN
    for k in ("classic_colour", "classic_images"):
        md = p8.region_diff(pic["p9_rb_before"], pic["p9_rb_back"], brects[k])
        d["%s: back to Classic against before" % k] = md
        c["%s: back to Classic as it was (within %.1f: %s)" % (k, TOL_TWIN, md)] = md is not None and md <= TOL_TWIN
    log, env = p8.mock_env(args, out, "p9_rb_after_mock", os.path.join(out, "cache_rb"))
    r = pipe_play(args.ours, out, "p9_rb_after_mock", S("p9_rb_after_remix"), env)
    mats = mock_materials(mock_lines(log))
    proc = [m for m in mats if near(m.get("albedo"), srgb([0.5, 0.6, 0.4]))]
    d["physical_proc in the fake runtime"] = proc[:2]
    c["physical_proc: Remix got a baked roughness image"] = bool(r["ran"]) and \
        any(m.get("roughness_tex", "-") != "-" for m in proc)
    if args.official:
        stock = p8.play(args.official, out, "stock_p9_rb_after", S("p9_rb_after"))
        sp = p8.shot_of(stock)
        means = {k: (None if sp is None else round(float(p8.crop(sp, brects[k]).mean()), 1)) for k in brects}
        d["stock 2.79b: each sphere's mean"] = means
        c["stock 2.79b opens the file and plays it"] = bool(stock.get("frames"))
        c["stock 2.79b draws each sphere (not black)"] = all(v is not None and v > 40.0 for v in means.values())
        sheets["remix_build_stock"] = stock.get("screenshot")
    tests["T9.6 Picking RTX Remix"] = {"ok": all(c.values()), "checks": c, "detail": d}
    sheets["remix_build"] = p2.contact_sheet([("before", runs["p9_rb_before"].get("screenshot")),
                                              ("RTX Remix", runs["p9_rb_after"].get("screenshot")),
                                              ("two back to Classic", runs["p9_rb_back"].get("screenshot"))],
                                             os.path.join(out, "sheet_p9_remix_build.png"))


def p92_rtx(args, out, scenes, E, tests, sheets, report_lines):
    """the gate: the fallback within 10 of Remix for every sphere but glass and
    thin film (ACES); and a diagnostic: Remix's debug views 1-60 of the P9.0
    spheres (gold, emissive)"""
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    R = E.get("remix_spheres")
    if not R:
        tests["S9.7 RTX Remix materials against Remix (the gate)"] = {"ok": False, "checks": {"made": False},
                                                                      "detail": E.get("failed")}
        return
    shot = {"BGE_REMIX_P2_SHOT_FRAME": "300"}
    gl = p8.play(args.ours, out, "rs_aces_opengl", S("p9_rs_nodes_aces"), shot, frames=p8.REMIX_FRAMES)
    rtx = p8.play(args.ours, out, "rs_aces_remix", S("p9_rs_nodes_aces_remix"), p8.REMIX_ENV, frames=p8.REMIX_FRAMES,
                  debug=True)
    rows = sphere_compare({"spheres": {"order": R["order"], "rects": R["rects"]}}, gl.get("screenshot"),
                          rtx.get("screenshot"))
    c = {"the RTX Remix materials ran in OpenGL and in Remix": bool(gl["ran"] and rtx["ran"])}
    for label, row in rows.items():
        if label in R["gate_out"]:
            continue
        c["%s: the fallback within %.0f of Remix (%s)" % (label, TOL_GATE, row and row["mean_diff"])] = \
            bool(row) and row["mean_diff"] <= TOL_GATE
    tests["S9.7 RTX Remix materials against Remix (the gate)"] = {"ok": all(c.values()), "checks": c, "detail": rows}
    sheets["remix_spheres"] = p2.contact_sheet([("fallback, ACES", gl.get("screenshot")),
                                                ("RTX Remix, ACES", rtx.get("screenshot"))],
                                               os.path.join(out, "sheet_p9_remix_spheres_rtx.png"))
    report_lines += ["", "RTX Remix materials, mean colour fallback -> Remix (gate %.0f; out: %s):" % (
        TOL_GATE, ", ".join(R["gate_out"]))]
    for label, row in rows.items():
        report_lines.append("  %-16s %s" % (label, "-" if not row else "%s -> %s  diff %.1f  brightness x%s" % (
            row["opengl"], row["remix"], row["mean_diff"], row["brightness_ratio"])))
    # diagnostic: the light's balance. The fallback (ACES) against Remix twice:
    # its tone curve off (Remix then draws the light itself, no ACES: the
    # light, before any curve, on both sides), and its global tone mapper
    # with ACES at a fixed exposure (auto exposure off: does Remix's curve
    # draw our ACES?)
    L = E.get("light_balance")
    if L:
        bal = {}
        for case in ("sun", "sky"):
            a = p8.play(args.ours, out, "lb_%s_opengl" % case, S("p9_lb_%s" % case), shot, frames=p8.REMIX_FRAMES)
            env = dict(p8.REMIX_ENV, BGE_REMIX_RTX_SET="rtx.tonemap.tonemappingEnabled=False")
            b = p8.play(args.ours, out, "lb_%s_remix" % case, S("p9_lb_%s_remix" % case), env, frames=p8.REMIX_FRAMES,
                        debug=True)
            env = dict(p8.REMIX_ENV, BGE_REMIX_RTX_SET="rtx.autoExposure.enabled=False;rtx.tonemap.exposureBias=0")
            c = p8.play(args.ours, out, "lb_%s_remix_fixed" % case, S("p9_lb_%s_remix" % case), env,
                        frames=p8.REMIX_FRAMES, debug=True)
            pa, pb, pc = (p8.picture(r.get("screenshot")) for r in (a, b, c))
            rows = {}
            for label in L["order"]:
                rect = L["rects"][label]
                if pa is None or pb is None or pa.shape != pb.shape:
                    rows[label] = None
                    continue
                # per pixel: the fallback's light under its ACES, Remix's (no curve)
                fa = p8.crop(pa, rect).reshape(-1, 3)
                fb = p8.crop(pb, rect).reshape(-1, 3)
                la = [sum(aces_inverse(srgb_to_linear(v)) for v in fa[:, k]) / len(fa) for k in range(3)]
                lb = [float(sum(srgb_to_linear(v) for v in fb[:, k]) / len(fb)) for k in range(3)]
                clipped = float((fb >= 254.5).mean())
                # (Remix's light is cut at white: no ratio for a clipped sphere)
                row = {"fallback_light": [round(float(v), 3) for v in la], "remix_light": [round(v, 3) for v in lb],
                       "ratio": round(sum(lb) / max(sum(la), 1e-6), 3) if clipped < 0.5 else None,
                       "remix_clipped": round(clipped, 3),
                       "fallback": [round(float(v), 1) for v in fa.mean(axis=0)]}
                if pc is not None and pc.shape == pa.shape:
                    fc = p8.crop(pc, rect).reshape(-1, 3)
                    row["remix_fixed"] = [round(float(v), 1) for v in fc.mean(axis=0)]
                    row["fixed_diff"] = round(float(abs(fc.mean(axis=0) - fa.mean(axis=0)).mean()), 1)
                if label in L["emission"]:
                    e = L["emission"][label]
                    y = (e * (2.51 * e + 0.03)) / (e * (2.43 * e + 0.59) + 0.14)
                    y = min(max(y, 0.0), 1.0)
                    srgb = 12.92 * y if y <= 0.0031308 else 1.055 * y ** (1.0 / 2.4) - 0.055
                    row["our_aces"] = round(255.0 * srgb, 1)
                rows[label] = row
            bal[case] = rows
            sheets["light_balance_" + case] = p2.contact_sheet(
                [("fallback, ACES", a.get("screenshot")), ("Remix, tone curve off", b.get("screenshot")),
                 ("Remix, global + ACES, exposure fixed", c.get("screenshot"))],
                os.path.join(out, "sheet_p9_light_balance_%s.png" % case))
        tests["S9.9 Light balance (a diagnostic)"] = {"ok": True, "checks": {}, "detail": bal}
        report_lines += ["", "Light balance: the light before any curve (fallback: its ACES undone; Remix: tone "
                         "curve off), ratio Remix / fallback, share of Remix's pixels clipped to white:"]
        for case, rows in bal.items():
            for label, row in rows.items():
                report_lines.append("  %-4s %-10s %s" % (case, label, "-" if not row else
                                    "%s -> %s  ratio %s  clipped %s" % (
                                        row["fallback_light"], row["remix_light"],
                                        row["ratio"] if row["ratio"] is not None else "- (white in Remix)",
                                        row["remix_clipped"])))
        report_lines += ["", "Light balance: Remix's global tone mapper + ACES at a fixed exposure against the "
                         "fallback (glowing spheres: what our ACES draws for that light):"]
        for case, rows in bal.items():
            for label, row in rows.items():
                if not row:
                    report_lines.append("  %-4s %-10s -" % (case, label))
                    continue
                report_lines.append("  %-4s %-10s %s -> %s  diff %s%s" % (
                    case, label, row["fallback"], row.get("remix_fixed", "-"), row.get("fixed_diff", "-"),
                    "  (our ACES %s)" % row["our_aces"] if "our_aces" in row else ""))
    # diagnostic: debug views 1-60 of the spheres (what Remix receives)
    dv = os.path.join(out, "debug_views_spheres")
    shutil.rmtree(dv, ignore_errors=True)
    os.makedirs(dv)
    env = dict(p8.REMIX_ENV)
    env.update({"BGE_REMIX_P9_DIR": dv, "BGE_REMIX_P9_FIRST": "1", "BGE_REMIX_P9_LAST": "60",
                "BGE_REMIX_P9_START": str(DV_START), "BGE_REMIX_P9_STEP": str(DV_STEP)})
    r = study_play(args.ours, out, "debug_views_spheres", S("p9_study_spheres_aces_remix"), "sweep", env=env,
                   frames=DV_START + DV_STEP * 62 + 30, debug=True)
    views = sweep_report(dv)
    real = [v for v in views if v["kind"] == "a view"]
    sheets["spheres_debug_views"] = thumb_sheet([("final picture", os.path.join(dv, "dv_0000.png"))] +
                                                [("view %d" % v["index"], v["path"]) for v in real],
                                                os.path.join(out, "sheet_p9_spheres_debug_views.png"))
    tests["S9.8 Debug views of the spheres (a diagnostic)"] = {
        "ok": True, "checks": {"the sweep ran": bool(r["p9"].get("done"))},
        "detail": {"views": [v["index"] for v in real], "folder": dv}}


# P9.2 diagnostic: Remix's tone curve on colours. The grey row gives Remix's
# own curve T (light in, picture out); each rule then predicts the coloured
# spheres from T, and the report says which comes closest
CC_LUMA = (0.2126, 0.7152, 0.0722)


def linear_to_srgb255(y):
    y = min(max(y, 0.0), 1.0)
    return 255.0 * (12.92 * y if y <= 0.0031308 else 1.055 * y ** (1.0 / 2.4) - 0.055)


def curve_from(points):
    """T(x) through (light, picture's linear value) points, straight in
    log2(light) between them and above them, in proportion below them"""
    import math
    pts = sorted((math.log2(x), y) for x, y in points if x > 0.0)

    def T(x):
        if x <= 0.0:
            return 0.0
        u = math.log2(x)
        if u < pts[0][0]:
            # below the dimmest grey: in proportion (every curve is straight
            # near black)
            return min(max(pts[0][1] * x / 2.0 ** pts[0][0], 0.0), 1.0)
        i = 0
        while i < len(pts) - 2 and u > pts[i + 1][0]:
            i += 1
        (u0, y0), (u1, y1) = pts[i], pts[i + 1]
        return min(max(y0 + (y1 - y0) * (u - u0) / (u1 - u0), 0.0), 1.0)
    return T


def colour_predictions(T, c, t=0.5):
    l = sum(w * v for w, v in zip(CC_LUMA, c))
    m = max(c)
    a = [T(v) for v in c]
    b = [v * T(l) / l for v in c] if l > 0.0 else [0.0] * 3
    x = [v * T(m) / m for v in c] if m > 0.0 else [0.0] * 3
    return {"a per channel": a, "b brightness": b, "c brightest channel": x,
            "d blend": [(1.0 - t) * p + t * q for p, q in zip(a, b)]}


def colour_curve_rtx(args, out, S, E, tests, sheets, report_lines):
    C = E.get("colour_curve")
    if not C:
        return
    shot = {"BGE_REMIX_P2_SHOT_FRAME": "300"}
    gl = p8.play(args.ours, out, "cc_opengl", S("p9_cc"), shot, frames=p8.REMIX_FRAMES).get("screenshot")
    runs = [("as set (global + ACES, exposure +0.3)", "cc_remix", None)]
    pg = p8.picture(gl)
    detail, items = {}, [("fallback, ACES", gl)]
    for label, tag, rtx_set in runs:
        env = dict(p8.REMIX_ENV)
        if rtx_set:
            env["BGE_REMIX_RTX_SET"] = rtx_set
        png = p8.play(args.ours, out, tag, S("p9_cc_remix"), env, frames=p8.REMIX_FRAMES, debug=True).get("screenshot")
        items.append(("Remix, " + label, png))
        pr = p8.picture(png)
        if pr is None:
            detail[label] = None
            continue
        if pg is not None and pg.shape == pr.shape and float(abs(pr - pg).max()) < 1.0:
            # the same picture as the fallback's: Remix did not draw it
            detail[label] = {"not_drawn_by_remix": True}
            continue
        meas = {k: [float(v) for v in p8.crop(pr, C["rects"][k]).reshape(-1, 3).mean(axis=0)] for k in C["order"]}
        ours = {k: [float(v) for v in p8.crop(pg, C["rects"][k]).reshape(-1, 3).mean(axis=0)]
                for k in C["order"]} if pg is not None and pg.shape == pr.shape else {}
        greys = [(C["spheres"][k]["level"], sum(srgb_to_linear(v) for v in meas[k]) / 3.0)
                 for k in C["order"] if C["spheres"][k]["row"] == "grey"]
        T = curve_from(greys)
        coloured = [k for k in C["order"] if C["spheres"][k]["row"] != "grey"]

        def err(rule, t=0.5, rows=None):
            e = []
            for k in coloured:
                sp = C["spheres"][k]
                if rows and sp["row"] not in rows:
                    continue
                c = [v * sp["level"] for v in sp["colour"]]
                pred = colour_predictions(T, c, t)[rule]
                e.append(sum(abs(linear_to_srgb255(y) - m) for y, m in zip(pred, meas[k])) / 3.0)
            return round(sum(e) / len(e), 1) if e else None
        best_t = min((i / 20.0 for i in range(21)), key=lambda t: err("d blend", t))
        rules = ("a per channel", "b brightness", "c brightest channel", "d blend")
        names = []
        for k in coloured:
            if C["spheres"][k]["row"] not in names:
                names.append(C["spheres"][k]["row"])
        rows_err = {rule: {row: err(rule, best_t, (row,)) for row in names} for rule in rules}
        ours_err = None
        if ours:
            d = [sum(abs(a - b) for a, b in zip(ours[k], meas[k])) / 3.0 for k in coloured]
            ours_err = round(sum(d) / len(d), 1)
        detail[label] = {"grey_curve": [[x, round(linear_to_srgb255(y), 1)] for x, y in greys],
                         "rules": {rule: err(rule, best_t) for rule in rules}, "blend_t": best_t,
                         "rows": rows_err, "fallback_diff": ours_err,
                         "measured": {k: [round(v, 1) for v in meas[k]] for k in C["order"]},
                         "fallback": {k: [round(v, 1) for v in ours[k]] for k in ours}}
    tests["S9.12 Remix's tone curve on colours (a diagnostic)"] = {"ok": True, "checks": {}, "detail": detail}
    sheets["colour_curve"] = p2.contact_sheet(items, os.path.join(out, "sheet_p9_colour_curve.png"))
    report_lines += ["", "Remix's tone curve on colours (54 glowing spheres; each rule predicts the coloured "
                     "spheres from Remix's own grey row; mean difference, 0-255):"]
    for label, d in detail.items():
        if not d or d.get("not_drawn_by_remix"):
            report_lines.append("  %s: -%s" % (label, " (Remix did not draw it: the fallback's picture)" if d else ""))
            continue
        report_lines.append("  %s" % label)
        report_lines.append("      grey row (light -> picture): %s" % "  ".join(
            "%g->%s" % (x, y) for x, y in d["grey_curve"]))
        for rule, e in d["rules"].items():
            report_lines.append("      %-22s %s%s   by row: %s" % (
                rule, e, " (t %.2f: 0 per channel, 1 brightness)" % d["blend_t"] if rule == "d blend" else "",
                "  ".join("%s %s" % kv for kv in d["rows"][rule].items())))
        report_lines.append("      our fallback (per-channel ACES) against Remix: %s" % d["fallback_diff"])
        for row in ("orange", "red", "blue"):
            ks = [k for k in E["colour_curve"]["order"] if E["colour_curve"]["spheres"][k]["row"] == row]
            report_lines.append("      %-6s Remix %s" % (row, "  ".join(str(d["measured"][k]) for k in ks)))
            if d["fallback"]:
                report_lines.append("      %-6s ours  %s" % (row, "  ".join(str(d["fallback"][k]) for k in ks)))


# P9.2b: do Remix's settings take effect while a game runs? The RTX Remix
# materials played with each suspect set three ways: while the game runs (as
# the scene's settings are), the same after Remix's graphics preset is set
# to Custom, and in an rtx.conf Remix reads before it starts. Each picture
# against the unchanged one (0: the setting did nothing); every play keeps
# the bridge's log (what Remix answered each setting) and Remix's own
SET_SUSPECTS = [
    ("bloom off", "rtx.bloom.enable", "False"),
    ("texture detail +2 (mip bias -2)", "rtx.nativeMipBias", "-2"),
    ("upscaling off, full size", "rtx.upscalerType", "0", "rtx.resolutionScale", "1"),
]
SET_PRESETS = [("graphics preset Custom", "4"), ("graphics preset Ultra", "0")]
SET_DETAIL = ("albedo_image", "normal_map", "alpha_cutout")


def bridge_log_path():
    return os.path.join(os.environ.get("TMP") or os.environ.get("TEMP") or ".", "bge-remix.log")


def settings_play(args, out, tag, blend, rtx_set=None, conf=None):
    """one Remix play; its new bridge log lines and Remix's log kept as
    <tag>_bridge.log and <tag>_remix-dxvk.log"""
    bl = bridge_log_path()
    start = os.path.getsize(bl) if os.path.isfile(bl) else 0
    conf_path = os.path.join(os.path.dirname(blend), "rtx.conf")
    if conf:
        with open(conf_path, "w") as f:
            f.write("".join("%s = %s\n" % kv for kv in conf))
    env = dict(p8.REMIX_ENV)
    if rtx_set:
        env["BGE_REMIX_RTX_SET"] = rtx_set
    try:
        r = p8.play(args.ours, out, tag, blend, env, frames=p8.REMIX_FRAMES, debug=True)
    finally:
        if conf and os.path.isfile(conf_path):
            os.remove(conf_path)
    text = ""
    if os.path.isfile(bl):
        with open(bl, "rb") as f:
            f.seek(start if os.path.getsize(bl) >= start else 0)
            text = f.read().decode("utf-8", "replace")
    with open(os.path.join(out, tag + "_bridge.log"), "w", encoding="utf-8") as f:
        f.write(text)
    remix_log = os.path.join(os.path.dirname(blend), "rtx-remix", "logs", "remix-dxvk.log")
    rtext = ""
    if os.path.isfile(remix_log):
        shutil.copyfile(remix_log, os.path.join(out, tag + "_remix-dxvk.log"))
        with open(remix_log, "r", encoding="utf-8", errors="replace") as f:
            rtext = f.read()
    return r.get("screenshot"), text, rtext


def settings_rtx(args, out, S, E, tests, sheets, report_lines):
    R = E.get("remix_spheres")
    if not R:
        return
    import re
    blend = S("p9_rs_nodes_aces_remix")
    gate_gl = os.path.join(out, "rs_aces_opengl.png")
    pg = p8.picture(gate_gl)
    rows, items = [], []

    def measure(label, png, blog, rlog, keys):
        pr = p8.picture(png)
        row = {"label": label, "vs_unchanged": p8.picture_diff(base_png, png) if base_png else None}
        cmp = sphere_compare({"spheres": {"order": R["order"], "rects": R["rects"]}}, gate_gl, png)
        gated = [v["mean_diff"] for k, v in cmp.items() if k not in R["gate_out"] and v]
        row["gate"] = "%d/%d mean %s" % (sum(1 for d in gated if d <= TOL_GATE), len(gated),
                                         round(sum(gated) / len(gated), 1) if gated else None)
        row["detail"] = {}
        for k in SET_DETAIL:
            if pg is not None and pr is not None and pg.shape == pr.shape:
                row["detail"][k] = round(float(p8.crop(pr, R["rects"][k]).reshape(-1, 3).mean(axis=1).std()), 1)
        row["answers"] = sorted(set(m for k in keys for m in re.findall(r"config %s = \S+ -> \d+" % re.escape(k),
                                                                         blog)))
        row["remix_says"] = sorted(set(m.strip() for m in re.findall(r"info:\s+(.*(?:[Pp]reset|graphics settings).*)", rlog)))
        rows.append(row)
        items.append((label, png))

    base_png, blog, rlog = settings_play(args, out, "set_unchanged", blend)
    measure("unchanged", base_png, blog, rlog, ["rtx.graphicsPreset"])
    for label, value in SET_PRESETS:
        png, blog, rlog = settings_play(args, out, "set_preset%s" % value, blend, "rtx.graphicsPreset=%s" % value)
        measure(label + " (while running)", png, blog, rlog, ["rtx.graphicsPreset"])
    for n, sus in enumerate(SET_SUSPECTS):
        label, pairs = sus[0], list(zip(sus[1::2], sus[2::2]))
        keys = [k for k, _ in pairs]
        rset = ";".join("%s=%s" % kv for kv in pairs)
        png, blog, rlog = settings_play(args, out, "set%d_run" % n, blend, rset)
        measure(label + ": while running", png, blog, rlog, keys)
        png, blog, rlog = settings_play(args, out, "set%d_custom" % n, blend, "rtx.graphicsPreset=4;" + rset)
        measure(label + ": preset Custom, then it", png, blog, rlog, keys + ["rtx.graphicsPreset"])
        png, blog, rlog = settings_play(args, out, "set%d_conf" % n, blend, conf=pairs)
        measure(label + ": in rtx.conf, before start", png, blog, rlog, keys)
    tests["S9.13 Remix settings while a game runs (P9.2b, a report)"] = {"ok": True, "checks": {}, "detail": rows}
    sheets["settings"] = p2.contact_sheet([("fallback, ACES", gate_gl)] + items,
                                          os.path.join(out, "sheet_p9_settings.png"))
    fallback_detail = {}
    if pg is not None:
        fallback_detail = {k: round(float(p8.crop(pg, R["rects"][k]).reshape(-1, 3).mean(axis=1).std()), 1)
                           for k in SET_DETAIL}
    report_lines += ["", "Remix settings while a game runs (difference from the unchanged picture: 0 = no effect; "
                     "the gate; texture detail, the fallback's: %s):" % fallback_detail]
    for row in rows:
        report_lines.append("  %-44s vs unchanged %-6s gate %-14s detail %s" % (
            row["label"], row["vs_unchanged"], row["gate"], row["detail"]))
        if row["answers"]:
            report_lines.append("      Remix answered: %s" % "; ".join(row["answers"]))
        if row["remix_says"]:
            report_lines.append("      Remix's log: %s" % "; ".join(row["remix_says"]))


def run_pipelines_rtx(args, out):
    """the fallback against Remix: the six spheres and the room, with ACES
    (Remix's tone mappers in turn) and with the view transform"""
    scenes, E = make_scenes(args.ours, out)
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    tests, sheets, report = {}, {}, {}
    shot = {"BGE_REMIX_P2_SHOT_FRAME": "300"}
    gl_aces = p8.play(args.ours, out, "spheres_aces_opengl", S("p9_study_spheres_aces"), shot,
                      frames=p8.REMIX_FRAMES)
    gl_view = p8.play(args.ours, out, "spheres_view_opengl", S("p9_study_spheres"), shot, frames=p8.REMIX_FRAMES)
    variants = [("ACES: global + ACES (as set)", "spheres_aces_remix", "p9_study_spheres_aces_remix", None),
                ("ACES file, ACES alone (tone curve off)", "spheres_aces_only", "p9_study_spheres_aces_remix",
                 "rtx.tonemap.tonemappingEnabled=False"),
                ("ACES file, Remix's local tone mapper + ACES", "spheres_aces_local", "p9_study_spheres_aces_remix",
                 "rtx.tonemappingMode=1"),
                ("view transform (P8's Modern)", "spheres_view_remix", "p9_study_spheres_remix", None)]
    c, items = {}, []
    for label, tag, blend, rtx_set in variants:
        env = dict(p8.REMIX_ENV)
        if rtx_set:
            env["BGE_REMIX_RTX_SET"] = rtx_set
        r = p8.play(args.ours, out, tag, S(blend), env, frames=p8.REMIX_FRAMES, debug=True)
        base = gl_view if "view" in tag else gl_aces
        rows = sphere_compare(E, base.get("screenshot"), r.get("screenshot"))
        diffs = [row["mean_diff"] for row in rows.values() if row]
        report[label] = {"rows": rows, "mean": round(sum(diffs) / len(diffs), 1) if diffs else None}
        c["%s: ran" % label] = bool(r["ran"])
        items.append((label, r.get("screenshot")))
    tests["S9.5 Six spheres: fallback against Remix (a report, not a gate)"] = {
        "ok": all(c.values()), "checks": c, "detail": report}
    sheets["spheres"] = p2.contact_sheet([("fallback, ACES", gl_aces.get("screenshot")),
                                          ("fallback, view transform", gl_view.get("screenshot"))] + items,
                                         os.path.join(out, "sheet_p9_spheres_tonemap.png"))
    # the room
    c, d, items = {}, {}, []
    for label, gl_name, rtx_name in (("ACES", "p9_pipe_aces_gl", "p9_pipe_aces_remix"),
                                     ("view transform", "p9_pipe_modern_gl", "p9_pipe_modern_remix")):
        a = p8.play(args.ours, out, gl_name + "_rtxrun", S(gl_name), shot, frames=p8.REMIX_FRAMES)
        b = p8.play(args.ours, out, rtx_name + "_rtxrun", S(rtx_name), p8.REMIX_ENV, frames=p8.REMIX_FRAMES,
                    debug=True)
        md = p8.picture_diff(a.get("screenshot"), b.get("screenshot"))
        d[label] = {"mean_diff": md}
        c["room, %s: ran in OpenGL and in Remix" % label] = bool(a["ran"] and b["ran"])
        items += [("room fallback, " + label, a.get("screenshot")), ("room Remix, " + label, b.get("screenshot"))]
    tests["S9.6 The room: fallback against Remix (a report, not a gate)"] = {"ok": all(c.values()), "checks": c,
                                                                            "detail": d}
    p92_lines = []
    p92_rtx(args, out, scenes, E, tests, sheets, p92_lines)
    colour_curve_rtx(args, out, S, E, tests, sheets, p92_lines)
    settings_rtx(args, out, S, E, tests, sheets, p92_lines)
    sheets["room"] = p2.contact_sheet(items, os.path.join(out, "sheet_p9_room.png"))
    res = {"tests": tests, "sheets": sheets, "all_ok": all(t["ok"] for t in tests.values())}
    lines = ["BGE-Remix P9.1-P9.2 (RTX run: your RTX card, the real RTX Remix runtime)", "=" * 70, ""]
    for name, t in tests.items():
        lines.append("%-62s %s" % (name, p2.mark(t["ok"])))
        for check, ok in t["checks"].items():
            lines.append("      %s %s" % ("ok    " if ok else "FAILED", check))
    lines += ["", "Spheres, mean colour difference fallback -> Remix (P8's tolerance: %.0f):" % TOL_SPHERE]
    for label, rep in report.items():
        lines.append("  %s: mean %s" % (label, rep["mean"]))
        for sphere, row in rep["rows"].items():
            lines.append("      %-16s %s" % (sphere, "-" if not row else "%s -> %s  diff %.1f  brightness x%s" % (
                row["opengl"], row["remix"], row["mean_diff"], row["brightness_ratio"])))
    lines += ["", "The room, mean difference fallback -> Remix:"] + \
             ["  %-16s %s" % (k, v["mean_diff"]) for k, v in d.items()]
    lines += p92_lines
    lines += ["", "Picture sheets:"] + ["  %-10s %s" % kv for kv in sheets.items()]
    lines += ["", "ALL PASSED" if res["all_ok"] else "SOMETHING FAILED (see results_p9_rtx.json and the logs)"]
    return res, lines, "p9_rtx"


# ---------------------------------------------------------------------------
# the RTX study run

def sphere_compare(E, gl_shot, rtx_shot):
    a, b = p8.picture(gl_shot), p8.picture(rtx_shot)
    rows = {}
    for label in E["spheres"]["order"]:
        rect = E["spheres"]["rects"][label]
        if a is None or b is None or a.shape != b.shape:
            rows[label] = None
            continue
        ma, mb = p8.crop(a, rect).reshape(-1, 3).mean(axis=0), p8.crop(b, rect).reshape(-1, 3).mean(axis=0)
        la, lb = float(ma.mean()), float(mb.mean())
        rows[label] = {"opengl": [round(float(v), 1) for v in ma], "remix": [round(float(v), 1) for v in mb],
                       "mean_diff": round(float(abs(ma - mb).mean()), 1),
                       "brightness_ratio": round(lb / la, 2) if la > 1 else None}
    return rows


def picture_stats(path, small=(96, 54)):
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None
    try:
        img = Image.open(path).convert("RGB")
    except OSError:
        return None
    a = np.asarray(img.resize(small), dtype=np.float64)
    return {"mean": float(a.mean()), "std": float(a.std()), "small": a,
            "hash": hashlib.md5(np.round(a / 8.0).astype("uint8").tobytes()).hexdigest()}


def sweep_report(folder):
    """which debug views draw a picture of their own: not blank, not the
    final picture, not the same as a lower index's"""
    final = picture_stats(os.path.join(folder, "dv_0000.png"))
    views, seen = [], {}
    for path in sorted(glob.glob(os.path.join(folder, "dv_*.png"))):
        idx = int(os.path.basename(path)[3:7])
        if idx == 0:
            continue
        st = picture_stats(path)
        if st is None:
            continue
        diff = float(abs(st["small"] - final["small"]).mean()) if final else None
        blank = st["std"] < 2.0
        same_final = diff is not None and diff < 2.0
        first = seen.get(st["hash"])
        if first is None:
            seen[st["hash"]] = idx
        kind = "blank" if blank else "final picture" if same_final else \
            ("same as %d" % first if first is not None else "a view")
        views.append({"index": idx, "kind": kind, "mean": round(st["mean"], 1), "std": round(st["std"], 1),
                      "diff_from_final": None if diff is None else round(diff, 1), "path": path})
    return views


def thumb_sheet(items, target, cols=6, w=320, h=180):
    """many small pictures, labelled, six to a row"""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    rows = max(1, (len(items) + cols - 1) // cols)
    sheet = Image.new("RGB", (cols * w, rows * h), (0, 0, 0))
    for i, (label, path) in enumerate(items):
        if path and os.path.isfile(path):
            img = Image.open(path).convert("RGB").resize((w, h))
        else:
            img = Image.new("RGB", (w, h), (40, 0, 0))
        d = ImageDraw.Draw(img)
        d.rectangle([0, 0, w, 16], fill=(0, 0, 0))
        d.text((4, 2), label, fill=(255, 255, 0))
        sheet.paste(img, ((i % cols) * w, (i // cols) * h))
    sheet.save(target)
    return target


def run_rtx(args, out):
    scenes, E = make_scenes(args.ours, out)
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    tests, sheets = {}, {}
    # the six spheres: the OpenGL fallback against Remix
    gl = p8.play(args.ours, out, "spheres_opengl", S("p9_study_spheres"), {"BGE_REMIX_P2_SHOT_FRAME": "300"},
                 frames=p8.REMIX_FRAMES)
    rtx = p8.play(args.ours, out, "spheres_remix", S("p9_study_spheres_remix"), p8.REMIX_ENV,
                  frames=p8.REMIX_FRAMES, debug=True)
    rows = sphere_compare(E, gl.get("screenshot"), rtx.get("screenshot"))
    c = {"the spheres ran in OpenGL and in Remix": bool(gl["ran"] and rtx["ran"])}
    for label, row in rows.items():
        c["%s: Remix within %.0f of the fallback (%s)" % (label, TOL_SPHERE, row and row["mean_diff"])] = \
            bool(row) and row["mean_diff"] <= TOL_SPHERE
    tests["S9.2 Six spheres (a study: reported, not a gate)"] = {"ok": True, "checks": c, "detail": rows}
    sheets["spheres"] = p2.contact_sheet([("OpenGL fallback (Modern)", gl.get("screenshot")),
                                          ("RTX Remix", rtx.get("screenshot"))],
                                         os.path.join(out, "sheet_p9_spheres.png"))
    # Remix's debug views, one after another
    dv = os.path.join(out, "debug_views")
    shutil.rmtree(dv, ignore_errors=True)
    os.makedirs(dv)
    env = dict(p8.REMIX_ENV)
    env.update({"BGE_REMIX_P9_DIR": dv, "BGE_REMIX_P9_FIRST": str(DV_FIRST), "BGE_REMIX_P9_LAST": str(DV_LAST),
                "BGE_REMIX_P9_START": str(DV_START), "BGE_REMIX_P9_STEP": str(DV_STEP)})
    frames = DV_START + DV_STEP * (DV_LAST - DV_FIRST + 2) + 30
    t0 = time.time()
    r = study_play(args.ours, out, "debug_views", S("p9_study_spheres_remix"), "sweep", env=env, frames=frames,
                   size=DV_SIZE, debug=True)
    views = sweep_report(dv)
    real = [v for v in views if v["kind"] == "a view"]
    st = r["p9"]
    c = {"the sweep ran to the end (%s of %d)" % (st.get("last_set"), DV_LAST): bool(st.get("done")),
         "Remix took every setting": bool(st.get("set")) and all(st.get("set", {}).values()),
         "no crash": not r.get("crashed")}
    by_kind = {}
    for v in views:
        k = v["kind"] if not v["kind"].startswith("same as") else "same as a lower index"
        by_kind[k] = by_kind.get(k, 0) + 1
    tests["S9.3 Remix's debug views"] = {
        "ok": all(c.values()), "checks": c,
        "detail": {"views_with_a_picture_of_their_own": [v["index"] for v in real], "by_kind": by_kind,
                   "seconds": round(time.time() - t0), "crash_report": r.get("crash_report"), "views": views}}
    # thumbnails: every view of its own; the rest are deleted (blank, the
    # final picture, a repeat)
    for v in views:
        if v["kind"] != "a view" and os.path.isfile(v["path"]):
            os.remove(v["path"])
    sheets["debug_views"] = thumb_sheet([("final picture", os.path.join(dv, "dv_0000.png"))] +
                                             [("view %d" % v["index"], v["path"]) for v in real],
                                             os.path.join(out, "sheet_p9_debug_views.png"))
    # Remix's own menu, switched on
    r = study_play(args.ours, out, "menu_picture", S("p9_study_spheres_remix"), "menu",
                   env=dict(p8.REMIX_ENV, BGE_REMIX_P9_MENU_FRAME="200"), frames=p8.REMIX_FRAMES)
    diff = p8.picture_diff(rtx.get("screenshot"), r.get("screenshot")) if r.get("screenshot") else None
    c = {"Remix took rtx.showUI": r["p9"].get("took") is True,
         "the picture changed with the menu on (%s)" % diff: diff is not None and diff > 2.0}
    tests["S9.3b Remix's own menu"] = {"ok": all(c.values()), "checks": c,
                                       "detail": {"picture": r.get("screenshot"), "diff_from_without": diff}}
    sheets["menu"] = p2.contact_sheet([("Remix", rtx.get("screenshot")),
                                       ("Remix with its menu on", r.get("screenshot"))],
                                      os.path.join(out, "sheet_p9_menu.png"))
    res = {"tests": tests, "sheets": sheets, "all_ok": all(t["ok"] for t in tests.values())}
    lines = ["BGE-Remix P9.0 study (RTX run: your RTX card, the real RTX Remix runtime)", "=" * 70, ""]
    for name, t in tests.items():
        lines.append("%-48s %s" % (name, p2.mark(t["ok"])))
        for check, ok in t["checks"].items():
            lines.append("      %s %s" % ("ok    " if ok else "FAILED", check))
    lines += ["", "Spheres (mean colour, OpenGL fallback -> Remix):"]
    for label, row in rows.items():
        lines.append("  %-16s %s" % (label, "-" if not row else "%s -> %s  diff %.1f  brightness x%s" % (
            row["opengl"], row["remix"], row["mean_diff"], row["brightness_ratio"])))
    lines += ["", "Debug views with a picture of their own (%d): %s" % (
        len(real), ", ".join(str(v["index"]) for v in real) or "none"),
        "  the rest: " + ", ".join("%s %d" % kv for kv in sorted(by_kind.items()) if kv[0] != "a view")]
    lines += ["", "Picture sheets:"] + ["  %-12s %s" % kv for kv in sheets.items()]
    lines += ["", "ALL PASSED" if res["all_ok"] else "SOMETHING FAILED (see results_p9_study_rtx.json and the logs)"]
    return res, lines, "p9_study_rtx"


def run_menu(args, out):
    """by hand: Remix's own menu over our picture until the window closes"""
    scenes, E = make_scenes(args.ours, out)
    env = dict(p8.REMIX_ENV, BGE_REMIX_P9_MENU_FRAME="120", BGE_REMIX_P2_FRAMES="100000000")
    print("The spheres open with RTX Remix; its menu comes up after two seconds.\n"
          "Try: click a tab at the top of the menu, drag a slider, click a check box, type a number into a box.\n"
          "Close the game window (or press Esc) when done.", flush=True)
    r = study_play(args.ours, out, "menu_by_hand", os.path.join(scenes, "p9_study_spheres_remix.blend"), "menu",
                   env=env, frames=1)
    lines = ["BGE-Remix P9.0: Remix's own menu, by hand", "=" * 70, "",
             "Remix took rtx.showUI: %s" % r["p9"].get("took"), "",
             "Tell Claude what worked: the tabs, sliders, check boxes, typing; anything odd.", "", "FINISHED"]
    return {"all_ok": True, "tests": {}, "menu": r["p9"]}, lines, "p9_menu"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--official")
    ap.add_argument("--mock")
    ap.add_argument("--results", help="the phases' results (the dry conversion reads their test files)")
    ap.add_argument("--study", action="store_true")
    ap.add_argument("--rtx", action="store_true")
    ap.add_argument("--menu", action="store_true")
    ap.add_argument("--pipelines", action="store_true", help="P9.1's tests (normal run)")
    ap.add_argument("--pipelines-rtx", action="store_true", help="P9.1's RTX run")
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    p8.OURS["build"] = args.ours
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    args.results = os.path.abspath(args.results or os.path.dirname(out))
    if args.pipelines_rtx:
        res, lines, name = run_pipelines_rtx(args, out)
    elif args.pipelines:
        if not args.mock:
            ap.error("--mock is needed (the fake RTX Remix runtime)")
        res, lines, name = run_pipelines(args, out)
    elif args.rtx:
        res, lines, name = run_rtx(args, out)
    elif args.menu:
        res, lines, name = run_menu(args, out)
    else:
        if not args.mock:
            ap.error("--mock is needed (the fake RTX Remix runtime)")
        res, lines, name = run_study(args, out)
    with open(os.path.join(out, "results_%s.json" % name), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, "summary_%s.txt" % name), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
