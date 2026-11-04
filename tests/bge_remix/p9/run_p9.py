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

    res = {"tests": tests, "sheets": sheets, "all_ok": all(t["ok"] for t in tests.values())}
    lines = ["BGE-Remix P9.1: Classic and RTX Remix (normal run)", "=" * 70, ""]
    for name, t in tests.items():
        lines.append("%-44s %s" % (name, p2.mark(t["ok"])))
        for check, ok in t["checks"].items():
            lines.append("      %s %s" % ("ok    " if ok else "FAILED", check))
    lines += ["", "Picture sheets:"] + ["  %-14s %s" % kv for kv in sheets.items()]
    lines += ["", "ALL PASSED" if res["all_ok"] else "SOMETHING FAILED (see results_p9.json and the logs)"]
    return res, lines, "p9"


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
    sheets["room"] = p2.contact_sheet(items, os.path.join(out, "sheet_p9_room.png"))
    res = {"tests": tests, "sheets": sheets, "all_ok": all(t["ok"] for t in tests.values())}
    lines = ["BGE-Remix P9.1 (RTX run: your RTX card, the real RTX Remix runtime)", "=" * 70, ""]
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
