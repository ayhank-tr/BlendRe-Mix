"""BGE-Remix P7 tests: the Game View (see and check).

T7.0  the editor: Game View is in the editor-type menu; it opens, moves
      around and closes; a file saved with a Game View opens in stock 2.79
T7.1  coverage: every kind the engine knows has a layer; in gv_clean every
      element appears and nothing is Unassigned
T7.2  left out: gv_broken's left-out items are listed with their reasons;
      marking one as known silences it and keeps it listed
T7.3  checks: gv_clean has no warnings; in gv_broken each fault is flagged
      once, on the right element, in the right colour (and gv_remix's)
T7.4  the same checks in the game: running gv_broken reports the same
      warnings as the Game View
T7.5  drawing: pictures of each cluster on gv_clean (against reference
      pictures once they exist); holograms behind walls are hidden
T7.6  regression: the P1-P6 results of the same run (CI)
T7.7  Remix background (RTX): holograms over RTX Remix's picture
B7.1  building the view of gv_count at each size
B7.2  the Game View's frame rate, every layer on, Plain, 5,000 elements
B7.3  from changing a setting to the view showing it

  python run_p7.py --ours <build> --official <stock 2.79b> --mock <fake runtime> --out <dir>   (CI)
  python run_p7.py --ours <build> --out <dir> --rtx                                           (your PC)
"""

import argparse
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
import run_p2 as p2  # noqa: E402  (running Blender, contact sheets)

PROBE = os.path.join(HERE, "p7_probe.py")
REFERENCE = os.path.join(HERE, "reference")
WINDOW = ["-p", "0", "0", "1600", "900"]
CLUSTERS = ("entity", "factor", "behaviour", "gate", "all")
COUNTS = (1000, 5000, 20000)
LEVEL_STATE = {"RED": ("RED",), "BLACK": ("BLACK",), "NOTE": ("WHITE", "GREEN")}

QUITTER = r'''
import bpy
scene = bpy.data.scenes["Level"]
ob = bpy.data.objects.new("Quitter", None)
scene.objects.link(ob)
scene.objects.active = ob
bpy.ops.logic.sensor_add(type='DELAY', object=ob.name)
bpy.ops.logic.controller_add(type='LOGIC_AND', object=ob.name)
bpy.ops.logic.actuator_add(type='GAME', object=ob.name)
s, c, a = ob.game.sensors[-1], ob.game.controllers[-1], ob.game.actuators[-1]
s.delay = 90
a.mode = 'QUIT'
s.link(c)
a.link(c)
bpy.ops.wm.save_as_mainfile(filepath=bpy.data.filepath.replace("gv_broken", "gv_broken_run"), copy=True)
'''

STOCK_OPEN = r'''
import bpy, sys
areas = sorted({a.type for s in bpy.data.screens for a in s.areas})
print("P7 stock open:", bpy.data.filepath, len(bpy.data.objects), "objects;", " ".join(areas))
sys.stdout.flush()
'''

# layer: owner: text [CODE] (the text may hold colons itself)
CHECK_LINE = re.compile(r"^BGE-Remix check: ([^:]+): ([^:]*): (.*) \[([A-Z0-9_]+)\]\s*$")


# ---------------------------------------------------------------------------
# running

def make_scenes(ours, out):
    scenes = os.path.join(out, "scenes")
    shutil.rmtree(scenes, ignore_errors=True)
    os.makedirs(scenes, exist_ok=True)
    code, text, _ = p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup",
                            "--python", os.path.join(HERE, "make_scenes_p7.py"), "--", scenes],
                           p2.base_env(out, "make"), os.path.join(out, "make_scenes_p7.log"), timeout=1800)
    expected = p2.read_json(os.path.join(scenes, "p7_expected.json"))
    if code != 0 or not expected:
        print("\n".join(text.splitlines()[-40:]))
        raise SystemExit("making the P7 scenes failed, see make_scenes_p7.log")
    for name, tb in expected.get("failed", {}).items():
        print("making the %s scene failed:\n%s" % (name, tb))
    blend = os.path.join(scenes, "gv_broken.blend")
    if os.path.isfile(blend):
        p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup", blend, "--python-expr", QUITTER],
               p2.base_env(out, "quitter"), os.path.join(out, "make_quitter.log"))
    return scenes, expected


def ui_run(ours, out, tag, blend, parts, extra_env=None, timeout=2400):
    res = os.path.join(out, tag + ".json")
    if os.path.isfile(res):
        os.remove(res)
    e = p2.base_env(out, tag)
    e.update({"BGE_REMIX_P7_OUT": res, "BGE_REMIX_P7_DIR": out, "BGE_REMIX_P7_PARTS": parts,
              "BGE_REMIX_P7_TAG": tag, "BGE_REMIX_P7_SAVE": os.path.join(out, tag + "_saved.blend")})
    e.update(extra_env or {})
    code, text, secs = p2.run([p2.exe(ours, "blender")] + WINDOW + [blend, "--python", PROBE], e,
                              os.path.join(out, tag + ".log"), timeout=timeout, cwd=out)
    probe = p2.read_json(res)
    if code != 0 or not probe.get("finished"):
        print("---- %s: exit %s; the end of its output:" % (tag, code))
        print("\n".join("    " + l for l in text.splitlines()[-30:]))
    for err in probe.get("errors", []):
        print("---- %s: %s" % (tag, err))
    steps = {s["step"]: s for s in probe.get("steps", [])}
    return {"exit": code, "seconds": round(secs, 1), "finished": probe.get("finished", False),
            "errors": probe.get("errors", []), "steps": steps}


def stock_open(official, out, blend):
    if not official or not os.path.isfile(blend):
        return None
    code, text, _ = p2.run([p2.exe(official, "blender"), "--background", "--factory-startup", blend,
                            "--python-expr", STOCK_OPEN], p2.base_env(out, "stock"),
                           os.path.join(out, "t70_stock_open.log"), timeout=300)
    line = [l for l in text.splitlines() if l.startswith("P7 stock open:")]
    return {"exit": code, "line": line[0] if line else None}


def game_report(ours, out, blend):
    if not os.path.isfile(blend):
        return {"ran": False}
    code, text, _ = p2.run([p2.exe(ours, "blenderplayer"), "-w", "640", "360", blend], p2.base_env(out, "t74"),
                           os.path.join(out, "t74_game.log"), timeout=300, cwd=os.path.dirname(blend))
    found = set()
    total = None
    for line in text.splitlines():
        m = CHECK_LINE.match(line.strip())
        if m:
            found.add((m.group(1), m.group(2), m.group(4)))
        elif line.startswith("BGE-Remix check:") and "warnings" in line:
            total = line.strip()
    return {"ran": True, "exit": code, "warnings": sorted(found), "total_line": total}


# ---------------------------------------------------------------------------
# reading the model

def model_of(r):
    return r["steps"].get("model") or {}


def layer_names(m):
    return {lid: name for lid, name, _c in m.get("layers", [])}


def active_warnings(m):
    """unknown warnings: (layer id, code, owner, element) with the element's state"""
    els = m.get("elements", [])
    out = []
    for w in m.get("warnings", []):
        if w["known"]:
            continue
        e = els[w["element"]] if w["element"] < len(els) else {}
        out.append({"layer": w["layer"], "code": w["code"], "owner": e.get("owner", ""), "name": e.get("name", ""),
                    "state": e.get("state"), "level": w["level"], "text": w["text"], "element": w["element"]})
    return out


def faults_check(m, expected, allowed_extra=()):
    """each expected fault flagged once, on the right object, in the right colour; no others"""
    c, d = {}, {"found": [], "extra": []}
    ws = active_warnings(m)
    for f in expected:
        hits = [w for w in ws if w["code"] == f["code"] and w["owner"] == f["owner"]]
        ok_once = len(hits) == 1
        ok_colour = ok_once and hits[0]["state"] in LEVEL_STATE[f["level"]]
        c["%s flagged once on %s (%s)" % (f["what"], f["owner"] or "the scene", f["code"])] = ok_once
        c["%s in the right colour" % f["what"]] = ok_colour
        d["found"].append({"fault": f, "hits": hits})
    codes = {(f["code"], f["owner"]) for f in expected}
    d["extra"] = [w for w in ws if (w["code"], w["owner"]) not in codes and w["code"] not in allowed_extra]
    c["no other warnings"] = not d["extra"]
    return c, d


# ---------------------------------------------------------------------------
# pictures

def load(path):
    from PIL import Image
    return Image.open(path).convert("RGB") if path and os.path.isfile(path) else None


def crop_region(img, box):
    return img.crop((box["x"], box["y"], box["x"] + box["w"], box["y"] + box["h"]))


def mean_diff(a, b):
    from PIL import ImageChops, ImageStat
    if a is None or b is None or a.size != b.size:
        return None
    diff = ImageChops.difference(a, b)
    return round(sum(ImageStat.Stat(diff).mean) / 3.0, 2)


def spot(img, box, xy, r):
    """a square around a point given in region coordinates (bottom-left origin)"""
    x = box["x"] + xy[0]
    y = box["y"] + (box["h"] - xy[1])
    r = max(4, int(r))
    return img.crop((int(x - r), int(y - r), int(x + r), int(y + r)))


def reference_check(out, tag, shots, kind):
    """compare with tests/bge_remix/p7/reference/<kind>/ once it holds pictures"""
    c, d = {}, {}
    ref_dir = os.path.join(REFERENCE, kind)
    new_dir = os.path.join(out, "new_reference", kind)
    for name, (path, box) in shots.items():
        img = load(path)
        c["picture of %s made" % name] = img is not None
        if img is None:
            continue
        crop = crop_region(img, box)
        ref_path = os.path.join(ref_dir, "%s_%s.png" % (tag, name))
        ref = load(ref_path)
        os.makedirs(new_dir, exist_ok=True)
        crop.save(os.path.join(new_dir, "%s_%s.png" % (tag, name)))
        if ref is None:
            d[name] = "no reference picture yet (this run's is in new_reference)"
            continue
        md = mean_diff(crop, ref)
        d[name] = {"mean_difference": md}
        c["%s matches its reference" % name] = md is not None and md < 6.0
    return c, d


# ---------------------------------------------------------------------------
# the tests

def t70(r, stock):
    s = r["steps"]
    c = {
        "Game View is in the editor-type menu": bool(s.get("menu", {}).get("game_view_in_menu")),
        "it opens and builds its model": s.get("opened", {}).get("area_type") == "GAME_VIEW" and
        bool(s.get("opened", {}).get("built")),
        "it moves around": bool(s.get("moved", {}).get("moved")),
        "a file is saved with it open": bool(s.get("save", {}).get("exists")),
        "it closes (back to a 3D View)": s.get("closed", {}).get("area_type") == "VIEW_3D" and
        not s.get("closed", {}).get("is_game_view", True),
    }
    if stock is not None:
        c["the file opens in stock 2.79"] = stock["exit"] == 0 and bool(stock["line"])
    return c, {"stock": stock, "opened": s.get("opened")}


def t71(m, expected):
    c, d = {}, {"missing": {}}
    kinds = {k[0]: k[1] for k in m.get("kinds", [])}
    for group, ids in (m.get("rna") or {}).items():
        missing = [i for i in ids if kinds.get("%s:%s" % (group, i), "UNASSIGNED") == "UNASSIGNED"]
        d["missing"][group] = missing
        c["every %s type has a layer" % group] = not missing and bool(ids)
    counts = (m.get("model") or {}).get("counts", {})
    c["gv_clean: nothing Unassigned"] = counts.get("UNASSIGNED", 1) == 0
    els = m.get("elements", [])
    obj_names = {e["name"] for e in els if e["kind"].startswith(("object:", "group:"))}
    bricks = {e["owner"] + "/" + e["name"] for e in els if e["layer"] == "LOGIC"}
    props = {e["owner"] + "/" + e["name"] for e in els if e["kind"].startswith("property:")}
    miss_obj = [o for o in expected.get("objects", []) if o not in obj_names]
    miss_brick = [b for b in expected.get("bricks", []) if b not in bricks]
    miss_prop = [p for p in expected.get("properties", []) if p not in props]
    c["gv_clean: every object appears"] = not miss_obj
    c["gv_clean: every logic brick appears"] = not miss_brick
    c["gv_clean: every game property appears"] = not miss_prop
    d.update({"objects_missing": miss_obj, "bricks_missing": miss_brick, "properties_missing": miss_prop,
              "counts": counts})
    return c, d


def t72(m, r):
    ws = [w for w in m.get("warnings", []) if w["layer"] == "LEFT_OUT"]
    k = r["steps"].get("known_mark", {})
    u = r["steps"].get("known_unmark", {})
    c = {
        "gv_broken's particle system is listed as left out, with its reason":
            any(w["code"] == "LEFT_PARTICLES" and "particle" in w["text"] for w in ws),
        "marking it as known silences it": bool(k.get("known_now")) and k.get("after") == (k.get("before") or 0) - 1,
        "and keeps it listed": bool(k.get("still_listed")) and bool(k.get("element_kept")),
        "unmarking it makes it warn again": bool(u.get("warns_again")),
    }
    return c, {"left_out": ws, "mark": k, "unmark": u}


def t73(clean, broken, remix, expected):
    c, d = {}, {}
    ws = active_warnings(clean)
    bad_states = [{"kind": e["kind"], "name": e["name"], "owner": e["owner"], "state": e["state"]}
                  for e in clean.get("elements", []) if e["state"] not in ("WHITE", "GREEN")]
    c["gv_clean has no warnings"] = bool(clean.get("module")) and not ws
    c["gv_clean: every element White or Green"] = bool(clean.get("elements")) and not bad_states
    d["clean_warnings"] = ws
    d["clean_not_white_or_green"] = bad_states
    if broken.get("module"):
        cb, db = faults_check(broken, expected.get("broken", {}).get("faults", []))
        c.update({"gv_broken: " + k: v for k, v in cb.items()})
        d["broken"] = db
    else:
        c["gv_broken was read"] = False
    if remix.get("module"):
        cr, dr = faults_check(remix, expected.get("remix", {}).get("expected", []))
        c.update({"gv_remix: " + k: v for k, v in cr.items()})
        els = remix.get("elements", [])
        c["gv_remix: the particle system goes to RTX Remix (not left out)"] = \
            any(e["kind"] == "remix:PARTICLES" and e["owner"] == "Sparks" for e in els) and \
            not any(e["kind"] == "leftout:PARTICLES" for e in els)
        d["remix"] = dr
    return c, d


def t74(broken, game):
    names = layer_names(broken)
    scene = (broken.get("model") or {}).get("scene", "")
    view = set()
    for w in active_warnings(broken):
        view.add((names.get(w["layer"], w["layer"]), w["owner"] or scene, w["code"]))
    got = {tuple(x) for x in game.get("warnings", []) if x[1] != "Quitter"}
    c = {"the game ran and reported its checks": game.get("ran") and bool(game.get("total_line")),
         "the game reports the same warnings as the Game View": bool(view) and got == view}
    return c, {"only_in_view": sorted(view - got), "only_in_game": sorted(got - view), "total": game.get("total_line")}


def t75(out, clean_r, walls_r, kind):
    shots = {}
    for name in CLUSTERS:
        s = clean_r["steps"].get("cluster_" + name, {})
        if s.get("screenshot"):
            shots["cluster_" + name] = (s["screenshot"], s["region"])
    c, d = reference_check(out, "clean", shots, kind)
    s = walls_r["steps"]
    on, off = load(s.get("walls_on", {}).get("screenshot")), load(s.get("walls_off", {}).get("screenshot"))
    pts = s.get("walls_points", {}).get("points", {})
    box = s.get("walls_points", {}).get("region")
    res = {"hidden": [], "shown": []}
    if on is not None and off is not None and box:
        for owner, key in (("Hidden", "hidden"), ("Shown", "shown")):
            for p in pts.get(owner, []):
                res[key].append({"kind": p["kind"], "difference": mean_diff(spot(on, box, p["xy"], p["r"]),
                                                                             spot(off, box, p["xy"], p["r"]))})
    hid = [x["difference"] for x in res["hidden"] if x["difference"] is not None]
    shown = [x["difference"] for x in res["shown"] if x["difference"] is not None]
    c["holograms behind the wall are hidden"] = bool(hid) and max(hid) < 4.0
    c["holograms in front of the wall are drawn"] = bool(shown) and max(shown) > 10.0
    d["walls"] = res
    return c, d


def t76(out):
    c, d = {}, {}
    root = os.path.dirname(out)
    for n in range(1, 7):
        path = os.path.join(root, "p%d" % n, "results_p%d.json" % n)
        data = p2.read_json(path)
        if not data:
            d["P%d" % n] = "not run in this run"
            continue
        d["P%d" % n] = data.get("all_ok")
        c["P%d unchanged (its tests pass)" % n] = bool(data.get("all_ok"))
    return c, d


def t77(remix_r):
    s = remix_r["steps"]
    holo = load(s.get("remix_setup", {}).get("screenshot"))
    plain = load(s.get("remix_plain", {}).get("screenshot"))
    box = s.get("remix_setup", {}).get("region")
    md = mean_diff(crop_region(holo, box), crop_region(plain, box)) if holo and plain and box else None
    c = {"the Remix background is shown": s.get("remix_setup", {}).get("background") == "REMIX",
         "holograms drawn over the Remix picture": md is not None and md > 1.0}
    return c, {"difference": md}


# ---------------------------------------------------------------------------

def run(args, out, rtx):
    scenes, expected = make_scenes(args.ours, out)
    kind = "rtx" if rtx else "ci"
    mock_env = {} if (rtx or not args.mock) else {"BGE_REMIX_RUNTIME_DIR": os.path.abspath(args.mock)}
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731

    runs = {}
    runs["clean"] = ui_run(args.ours, out, "clean", S("gv_clean"), "editor,model,pictures,bench,edit")
    runs["broken"] = ui_run(args.ours, out, "broken", S("gv_broken"), "model,known")
    runs["remix"] = ui_run(args.ours, out, "remix", S("gv_remix"), "model" + (",remix" if rtx else ""), mock_env)
    runs["walls"] = ui_run(args.ours, out, "walls", S("gv_walls"), "walls")
    for n in COUNTS:
        runs["count_%d" % n] = ui_run(args.ours, out, "count_%d" % n, S("gv_count_%d" % n), "bench", timeout=3600)
    stock = None if rtx else stock_open(args.official, out, os.path.join(out, "clean_saved.blend"))
    game = game_report(args.ours, out, S("gv_broken_run"))

    clean, broken, remix = model_of(runs["clean"]), model_of(runs["broken"]), model_of(runs["remix"])
    tests = {}
    for name, fn in (("T7.0 The editor", lambda: t70(runs["clean"], stock)),
                     ("T7.1 Coverage", lambda: t71(clean, expected.get("clean", {}))),
                     ("T7.2 Left out", lambda: t72(broken, runs["broken"])),
                     ("T7.3 Checks", lambda: t73(clean, broken, remix, expected)),
                     ("T7.4 Same checks in the game", lambda: t74(broken, game)),
                     ("T7.5 Drawing", lambda: t75(out, runs["clean"], runs["walls"], kind))):
        try:
            c, d = fn()
        except Exception as ex:
            import traceback
            c, d = {"the test ran": False}, {"error": traceback.format_exc()}
            print(name, ex)
        tests[name] = {"ok": all(c.values()) and bool(c), "checks": c, "detail": d}
    if not rtx:
        c, d = t76(out)
        tests["T7.6 Regression"] = {"ok": all(c.values()), "checks": c, "detail": d}
    else:
        c, d = t77(runs["remix"])
        tests["T7.7 Remix background"] = {"ok": all(c.values()), "checks": c, "detail": d}
    for tag, r in runs.items():
        tests.setdefault("Blender ran (all scenes)", {"ok": True, "checks": {}, "detail": {}})
        ok = r["exit"] == 0 and r["finished"] and not r["errors"]
        tests["Blender ran (all scenes)"]["checks"][tag] = ok
        tests["Blender ran (all scenes)"]["ok"] &= ok

    bench = {"B7.1 dry conversion": {}, "B7.2 drawing": {}, "B7.3 after an edit": {}}
    for n in COUNTS:
        s = runs["count_%d" % n]["steps"].get("bench_refresh", {})
        bench["B7.1 dry conversion"][n] = {"elements": s.get("elements"), "refresh_ms": s.get("refresh_ms"),
                                           "convert_ms": s.get("convert_ms")}
    d = runs["count_5000"]["steps"].get("bench_draw", {})
    bench["B7.2 drawing"] = dict(d, target_fps=60)
    e = runs["clean"]["steps"].get("bench_edit_wait", {})
    bench["B7.3 after an edit"] = dict(e, target_ms=100)
    if rtx:
        tests["B7.2 on this PC"] = {"ok": (d.get("fps") or 0) >= 60, "checks": {"60 FPS": (d.get("fps") or 0) >= 60},
                                    "detail": d}
        tests["B7.3 on this PC"] = {"ok": e.get("rebuilt", False) and (e.get("edit_to_view_ms") or 1e9) < 100,
                                    "checks": {"under 0.1 s": (e.get("edit_to_view_ms") or 1e9) < 100}, "detail": e}

    res = {"tests": tests, "bench": bench, "all_ok": all(t["ok"] for t in tests.values())}
    if rtx:
        shots = [("Remix + holograms", runs["remix"]["steps"].get("remix_setup", {}).get("screenshot"))]
        shots += [(n, runs["clean"]["steps"].get("cluster_" + n, {}).get("screenshot")) for n in CLUSTERS]
        res["sheet"] = p2.contact_sheet(shots, os.path.join(out, "sheet_p7.png"))

    lines = ["BGE-Remix P7 tests (%s)" % ("RTX" if rtx else "CI"), "=" * 30, ""]
    for name, t in tests.items():
        lines.append("%-36s %s" % (name, p2.mark(t["ok"])))
        for check, ok in t["checks"].items():
            if not ok:
                lines.append("      failed: " + check)
    lines += ["", "B7.1 building the view (ms; elements):"]
    for n, v in bench["B7.1 dry conversion"].items():
        lines.append("   gv_count_%-6s %8s elements   %s" % (n, v.get("elements"), v.get("refresh_ms")))
    lines.append("B7.2 drawing, 5,000 elements, every layer: %s ms a frame (%s FPS; holograms %s ms)" % (
        d.get("draw_ms"), d.get("fps"), d.get("holograms_ms")))
    lines.append("B7.3 from an edit to the view: %s ms" % e.get("edit_to_view_ms"))
    if res.get("sheet"):
        lines.append("Picture sheet: %s" % res["sheet"])
    lines += ["", "ALL PASSED" if res["all_ok"] else "SOMETHING FAILED (see results_p7*.json and the .log files)"]
    return res, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--official")
    ap.add_argument("--mock")
    ap.add_argument("--rtx", action="store_true")
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    if args.official:
        args.official = os.path.abspath(args.official)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    res, lines = run(args, out, args.rtx)
    name = "results_p7" + ("_rtx" if args.rtx else "")
    with open(os.path.join(out, name + ".json"), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, name.replace("results", "summary") + ".txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
