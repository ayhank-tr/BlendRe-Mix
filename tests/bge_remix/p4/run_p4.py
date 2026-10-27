"""BGE-Remix phase 4 tests: dynamic content.

CI (fake runtime, no GPU):
  T4.1a  skinning data: the Mannequin's mesh arrives once with at most 4
         weights per vertex adding up to 1 and 15 bones; bone matrices every
         frame; no mesh sent again
  T4.1b  the same maths as BGE: bone matrices applied to the rest pose (the
         way Remix 1.6.1 reads them) land within 1 mm of BGE's own skinning
  T4.1c  fallback: the shape-key ball is sent again as it pulses, the
         Subdivision Surface monkey once, subdivided
  T4.2a  200 rigid bodies drawn every frame and moving; the cloth sent
         again as it falls, in world space
  T4.3a  fountain: no mesh created per spawned drop or spark; Halo sparks
         face the camera, the Billboard tree turns upright
  T4.4a  picking: physics as in stock BGE, Remix picks through the fake
         runtime, the outline is asked for
  T4.5a  a Blender particle system reaches Remix as GPU particles
  T4.0   the five scenes play in official 2.79b
RTX (your PC):
  T4.1 characters next to OpenGL (sign-off), bending bar: bone space
  T4.2 physics, T4.3 fountain (with T4.5's sparks), T4.4 picking
  B4.1-B4.4 benchmarks

  python run_p4.py --ours <build> --mock <mock> --official <2.79b> --out <dir>
  python run_p4.py --ours <build> --out <dir> --rtx
"""

import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
import run_p2 as p2  # noqa: E402  (running Blender, the fake runtime's log)

SKIN_FRAMES = "100,150,200"


def make_scenes(ours, out):
    scenes = os.path.join(out, "scenes")
    if os.path.isdir(scenes):
        shutil.rmtree(scenes, ignore_errors=True)
    os.makedirs(scenes, exist_ok=True)
    code, text, _ = p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup",
                            "--python", os.path.join(HERE, "make_scenes_p4.py"), "--", scenes],
                           p2.base_env(out, "make"), os.path.join(out, "make_scenes_p4.log"))
    expected = p2.read_json(os.path.join(scenes, "p4_expected.json"))
    if code != 0 or not expected:
        print("\n".join(text.splitlines()[-40:]))
        raise SystemExit("making the P4 test scenes failed, see make_scenes_p4.log")
    for name, tb in expected.get("failed", {}).items():
        # the tests of that scene fail; the others still run
        print("making the %s scene failed:\n%s" % (name, tb))
    return scenes, expected


def player(ours, out, name, blend, env, **kw):
    """p2.player, and the end of the game's output on the CI page when it
    did not run cleanly (the log files are in the results artifact too)"""
    r = p2.player(ours, out, name, blend, env, **kw)
    if r["exit"] != 0 or r["traceback_in_output"] or not r["frames"]:
        try:
            with open(os.path.join(out, name + ".log"), encoding="utf-8", errors="replace") as f:
                tail = f.read().splitlines()[-30:]
        except OSError:
            tail = ["(no log)"]
        print("---- %s: exit %s, %s frames; the end of its output:" % (name, r["exit"], r["frames"]))
        print("\n".join("    " + l for l in tail))
    return r


def hexfield(line, key):
    """a hash field as an int (parse_fields would read some as numbers)"""
    m = re.search(r"\b%s=([0-9a-fA-F]+)\b" % key, line)
    return int(m.group(1), 16) if m else None


def hashes(kind, name):
    """the hashes the renderer may give (BGE names meshes with or without ME)"""
    return {p2.fnv1a("%s:%s" % (kind, name)), p2.fnv1a("%s:ME%s" % (kind, name))}


def mesh_lines(lines, hs):
    return [p2.parse_fields(l) for l in lines if l.startswith("CreateMesh ") and hexfield(l, "hash") in hs]


def presents(lines):
    out = []
    for l in lines:
        if l.startswith("Present "):
            f = p2.parse_fields(l)
            f["bonehash"] = l.split("bonehash=")[1].split()[0] if "bonehash=" in l else ""
            out.append(f)
    return out


def positions(text):
    return [[float(v) for v in p.split(",")] for p in text.split(";") if p]


def skinned_lines(lines, prefix):
    """{(frame, pick): [positions]} of 'Skinned' (fake runtime) or 'BGE-Remix skincheck' (player) lines"""
    out = {}
    for l in lines:
        if not l.startswith(prefix):
            continue
        head, _, pos = l.partition(" pos=")
        f = p2.parse_fields(head)
        out[(int(f["frame"]), int(f["pick"]))] = positions(pos)
    return out


def draws(lines):
    out = []
    for l in lines:
        if l.startswith("Draw "):
            f = p2.parse_fields(l)
            f["mesh_int"] = hexfield(l, "mesh")
            out.append(f)
    return out


def column(m, c):
    """column c of the logged 3 x 4 rows"""
    return [m[r * 4 + c] for r in range(3)]


def angle(a, b):
    la = math.sqrt(sum(x * x for x in a))
    lb = math.sqrt(sum(x * x for x in b))
    if la < 1e-9 or lb < 1e-9:
        return 180.0
    d = sum(x * y for x, y in zip(a, b)) / (la * lb)
    return math.degrees(math.acos(max(-1.0, min(1.0, d))))


# ---------------------------------------------------------------------------
# CI

def run_ci(args, out):
    scenes, expected = make_scenes(args.ours, out)
    mock = os.path.abspath(args.mock)
    res = {}

    def mock_env(name, extra=None):
        log = os.path.join(out, name + "_mock.log")
        if os.path.isfile(log):
            os.remove(log)
        env = {"BGE_REMIX_RUNTIME_DIR": mock, "BGE_REMIX_MOCK_MODE": "ok", "BGE_REMIX_MOCK_LOG": log,
               "BGE_REMIX_CACHE_DIR": os.path.join(out, "cache"), "BGE_REMIX_TEXTURE_NO_GPU": "1",
               "BGE_REMIX_P2_FRAMES": "300", "BGE_REMIX_P2_SECONDS": "0"}
        env.update(extra or {})
        return log, env

    def ran(r, frames=300):
        return r["exit"] == 0 and r["frames"] >= frames and not r["traceback_in_output"]

    def text_of(name):
        with open(os.path.join(out, name + ".log"), encoding="utf-8", errors="replace") as f:
            return f.read()

    def guarded(scene, keys, fn):
        """run one scene's checks; a scene that could not be made, or an
        error in the checks, fails its tests with the reason"""
        if expected.get(scene) is None:
            why = (expected.get("failed", {}).get(scene) or "not made").strip().splitlines()[-1]
            for k in keys:
                res[k] = dict(p2.test({"the %s scene was made (%s)" % (scene, why): False}))
            return
        try:
            fn()
        except Exception:
            import traceback
            text = traceback.format_exc()
            print(text)
            for k in keys:
                if k not in res:
                    res[k] = dict(p2.test({"checks ran without an error (%s)" % text.strip().splitlines()[-1]: False}),
                                  traceback=text)

    def characters():
        # T4.1a-c: the characters
        ch = expected["characters"]
        log, env = mock_env("characters", {"BGE_REMIX_SKIN_CHECK": SKIN_FRAMES, "BGE_REMIX_MOCK_SKIN_FRAMES": SKIN_FRAMES,
                                           "BGE_REMIX_P2_SECONDS": "4"})
        r = player(args.ours, out, "characters", os.path.join(scenes, "p4_characters.blend"), env)
        lines = p2.read_lines(log)
        st = r["stats"]
        skin = mesh_lines(lines, hashes("skin", ch["mannequin_mesh"]))
        bar = mesh_lines(lines, hashes("skin", ch["bar_mesh"]))
        pres = [p for p in presents(lines)][10:]
        want_bones = 3 * (ch["groups"] + 1) + (ch["bar_groups"] + 1)
        c = {
            "the player ran 300 frames": ran(r),
            "the Mannequin's skinned mesh was sent once": len(skin) == 1,
            "the bar's skinned mesh was sent once": len(bar) == 1,
            "4 bones per vertex (the belt has vertices with 4)": bool(skin) and skin[0].get("bpv") == 4,
            "every weight between 0 and 1, adding up to at most 1": bool(skin) and skin[0].get("bad_weights") == 0 and
            skin[0].get("weight_overflow", 1) < 1e-3,
            "bone indices below 16 (15 groups and the identity)": bool(skin) and skin[0].get("max_bone", 99) <= ch["groups"],
            "4 skinned draws every frame (3 Mannequins and the bar)": bool(pres) and all(p.get("skinned") == 4 for p in pres),
            "%d bone matrices every frame" % want_bones: bool(pres) and all(p.get("bones") == want_bones for p in pres),
            "the bones move (30 or more different poses)": len(set(p["bonehash"] for p in pres)) >= 30,
            "two skinned meshes for four objects": st.get("skin_meshes") == 2 and st.get("skinned_objects") == 4,
            "no object fell back to CPU skinning": st.get("cpu_skin_notes") == 0,
        }
        res["T4.1a"] = dict(p2.test(c), detail={"skin_mesh": skin, "bar_mesh": bar, "stats": st,
                                                "present_sample": pres[:3]})

        gpu = skinned_lines(lines, "Skinned ")
        out_lines = text_of("characters").splitlines()
        cpu = skinned_lines(out_lines, "BGE-Remix skincheck ")
        formula = skinned_lines(out_lines, "BGE-Remix skinformula ")

        def compare(x, y):
            """{key: max distance} of two position sets, and where the worst one is"""
            errs, where = {}, {}
            for key, a in x.items():
                b = y.get(key)
                if b is None or len(a) != len(b):
                    errs[str(key)] = "missing" if b is None else "%d vs %d vertices" % (len(a), len(b))
                    continue
                d = [math.sqrt(sum((u - v) ** 2 for u, v in zip(p, q))) for p, q in zip(a, b)]
                i = max(range(len(d)), key=lambda k: d[k]) if d else 0
                errs[str(key)] = max(d) if d else 0.0
                where[str(key)] = {"vertex": i, "of": len(d), "a": a[i] if d else None, "b": b[i] if d else None,
                                   "over_1mm": sum(1 for e in d if e > 0.001)}
            worst = max([e for e in errs.values() if isinstance(e, float)] or [0.0])
            return errs, where, worst

        errs, where, worst = compare(cpu, gpu)
        ferr, fwhere, fworst = compare(cpu, formula)
        gerr, gwhere, gworst = compare(formula, gpu)
        c = {
            "12 comparisons (4 objects on 3 frames)": len(cpu) == 12,
            "every one matched with the fake runtime's": all(isinstance(e, float) for e in errs.values()) and bool(errs),
            "within 1 mm of BGE's own skinning": bool(errs) and worst < 0.001,
            "our bone matrices and weights reproduce BGE's skinning (on the CPU)": bool(ferr) and fworst < 0.001,
            "the bridge hands Remix the same (fake runtime = formula)": bool(gerr) and gworst < 0.001,
        }
        res["T4.1b"] = dict(p2.test(c), detail={"max_error_m": worst, "per_object_frame": errs,
                                                "bge_vs_formula": {"max": fworst, "worst": fwhere},
                                                "formula_vs_remix": {"max": gworst, "worst": gwhere}})

        pulse = mesh_lines(lines, hashes("deformed", ch["pulse"]))
        monkey = mesh_lines(lines, hashes("deformed", ch["monkey"]))
        base_indices = ch["monkey_faces"] * 6
        c = {
            "the pulsing ball sent again as it changes (10 times or more)": len(pulse) >= 10,
            "the monkey sent once": len(monkey) == 1,
            "the monkey subdivided (more than 8 times its own triangles)": bool(monkey) and
            monkey[0].get("indices", 0) > 8 * base_indices,
            "deformed on the CPU: 2 objects": st.get("deformed_objects") == 2,
        }
        res["T4.1c"] = dict(p2.test(c), detail={"pulse_sends": len(pulse), "monkey": monkey})


    guarded('characters', ['T4.1a', 'T4.1b', 'T4.1c'], characters)

    def physics():
        # T4.2a: physics
        ph = expected["physics"]
        log, env = mock_env("physics", {"BGE_REMIX_MOCK_DRAWS_FRAME": "20,280"})
        r = player(args.ours, out, "physics", os.path.join(scenes, "p4_physics.blend"), env)
        lines = p2.read_lines(log)
        dl = draws(lines)
        box_hashes = set()
        for k in range(5):
            box_hashes |= hashes("mesh", "Box%d_000Mesh" % k)
        ball_hashes = hashes("mesh", "BallMesh")
        early = {d["pick"]: d for d in dl if d["frame"] == 20}
        late = {d["pick"]: d for d in dl if d["frame"] == 280}
        moved = 0
        bodies = [p for p, d in late.items() if d["mesh_int"] in box_hashes | ball_hashes]
        for pid in bodies:
            if pid in early:
                a, b = early[pid]["m"], late[pid]["m"]
                if math.sqrt(sum((a[i] - b[i]) ** 2 for i in (3, 7, 11))) > 0.5:
                    moved += 1
        cloth = [d for d in dl if d["mesh_int"] in hashes("deformed", ph["cloth"])]
        identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]
        cloth_sends = mesh_lines(lines, hashes("deformed", ph["cloth"]))
        pres = presents(lines)[5:]
        c = {
            "the player ran 300 frames": ran(r),
            "200 rigid bodies drawn": len(bodies) == 200,
            "180 or more of them fell more than 0.5 m": moved >= 180,
            "every frame draws all of them": bool(pres) and all(p.get("draws", 0) >= 200 for p in pres),
            "the cloth sent again as it falls (20 times or more)": len(cloth_sends) >= 20,
            "the cloth drawn in world space": bool(cloth) and all(p2.close(d["m"], identity, 1e-4) for d in cloth),
        }
        res["T4.2a"] = dict(p2.test(c), detail={"bodies": len(bodies), "moved": moved, "cloth_sends": len(cloth_sends),
                                                "stats": r["stats"]})


    guarded('physics', ['T4.2a'], physics)

    def fountain():
        # T4.3a, T4.5a: the fountain
        fo = expected["fountain"]
        log, env = mock_env("fountain", {"BGE_REMIX_MOCK_DRAWS_FRAME": "400", "BGE_REMIX_P2_FRAMES": "420",
                                         "BGE_REMIX_P2_SECONDS": "8"})
        r = player(args.ours, out, "fountain", os.path.join(scenes, "p4_fountain.blend"), env)
        lines = p2.read_lines(log)
        dl = draws(lines)
        cam = fo["camera"]
        sparks = [d for d in dl if d["mesh_int"] in hashes("mesh", fo["spark_mesh"])]
        trees = [d for d in dl if d["mesh_int"] in hashes("mesh", fo["tree_mesh"])]
        spark_err = 0.0
        for d in sparks:
            m = d["m"]
            pos = [m[3], m[7], m[11]]
            spark_err = max(spark_err, angle(column(m, 0), [cam[i] - pos[i] for i in range(3)]))
        tree_err = 0.0
        for d in trees:
            m = d["m"]
            pos = [m[3], m[7], m[11]]
            to_cam = [cam[0] - pos[0], cam[1] - pos[1], 0.0]
            tree_err = max(tree_err, angle(column(m, 0), to_cam), angle(column(m, 2), [0, 0, 1]))
        drop_sends = mesh_lines(lines, hashes("mesh", "DropMesh"))
        spark_sends = mesh_lines(lines, hashes("mesh", fo["spark_mesh"]))
        pres = presents(lines)
        most = max([p.get("draws", 0) for p in pres] or [0])
        c = {
            "the player ran 8 s": ran(r, 420),
            "drop and spark meshes created once each": len(drop_sends) == 1 and len(spark_sends) == 1,
            "400 or more objects drawn at once": most >= 400,
            "sparks drawn as particles": r["stats"].get("particle_draws", 0) >= 20,
            "sparks face the camera within 1 degree": bool(sparks) and spark_err < 1.0,
            "the tree upright and turned to the camera within 1 degree": bool(trees) and tree_err < 1.0,
        }
        res["T4.3a"] = dict(p2.test(c), detail={"sparks_checked": len(sparks), "spark_error_deg": spark_err,
                                                "tree_error_deg": tree_err, "most_draws": most, "stats": r["stats"]})

        em = fo["emitter"]
        emit = [p2.parse_fields(l) for l in lines if l.startswith("Emitter ") and
                hexfield(l, "mesh") in hashes("mesh", em["mesh"])]
        e = emit[-1] if emit else {}
        life = e.get("life", [0, 0]) if isinstance(e.get("life"), list) else [0, 0]
        size = e.get("size", [0, 0]) if isinstance(e.get("size"), list) else [0, 0]
        c = {
            "the emitter reaches Remix with GPU particles": bool(emit) and r["stats"].get("gpu_emitters", 0) >= 1,
            "rate %.1f per second" % em["rate"]: p2.close(e.get("rate", 0), em["rate"], 1e-2),
            "lifetime %.2f to %.2f s" % (em["life_min"], em["life_max"]): p2.close(life, [em["life_min"], em["life_max"]], 1e-2),
            "start speed 3 m/s (300 cm/s) along the normal": p2.close(e.get("speed", 0), em["speed_cm"], 1e-3),
            "spread %.1f degrees" % em["cone"]: p2.close(e.get("cone", 0), em["cone"], 1e-2),
            "gravity %.0f cm/s2" % em["gravity_cm"]: p2.close(e.get("gravity", 0), em["gravity_cm"], 1e-2),
            "size 1.5 to 3 cm": p2.close(size, [em["size_min_cm"], em["size_max_cm"]], 1e-2),
            "the emitter plane itself hidden": e.get("hide") == em["hide"],
            "bounces off the floor (Collision)": e.get("collide") == em["collide"] and
            p2.close(e.get("restitution", 0), em["restitution"], 1e-2),
        }
        res["T4.5a"] = dict(p2.test(c), detail={"emitter": e})


    guarded('fountain', ['T4.3a', 'T4.5a'], fountain)

    def picking():
        # T4.4a: picking
        pk = expected["picking"]
        pick_out = os.path.join(out, "picking_points.json")
        if os.path.isfile(pick_out):
            os.remove(pick_out)
        log, env = mock_env("picking", {"BGE_REMIX_P4_PICK_OUT": pick_out, "BGE_REMIX_P4_FREEZE": "20"})
        r = player(args.ours, out, "picking", os.path.join(scenes, "p4_picking.blend"), env, size=(1280, 720))
        lines = p2.read_lines(log)
        got = {p["point"]: p for p in (p2.read_json(pick_out) or [])}
        c = {"the player ran 300 frames": ran(r), "all 9 points tried": len(got) == len(pk["points"])}
        for name in pk["points"]:
            g = got.get(name, {})
            c["%s: physics %s" % (name, " or ".join(str(x) for x in pk["physics"][name]))] = \
                g.get("physics") in pk["physics"][name]
            c["%s: Remix pick %s" % (name, " or ".join(str(x) for x in pk["fake"][name]))] = \
                g.get("remix") in pk["fake"][name]
        c["outlines asked for"] = any(p.get("highlighted", 0) > 0 for p in presents(lines))
        res["T4.4a"] = dict(p2.test(c), detail={"points": got, "stats": r["stats"]})


    guarded('picking', ['T4.4a'], picking)

    # T4.0: official 2.79b
    res["T4.0"] = check_stock(args, out, scenes)

    keys = ("T4.1a", "T4.1b", "T4.1c", "T4.2a", "T4.3a", "T4.4a", "T4.5a", "T4.0")
    names = {"T4.1a": "Skinning data", "T4.1b": "Same maths as BGE", "T4.1c": "Deform fallback",
             "T4.2a": "Physics", "T4.3a": "Fountain, billboards", "T4.4a": "Picking",
             "T4.5a": "Blender particles", "T4.0": "Plays in official 2.79b"}
    res["all_ok"] = all(res[k]["ok"] for k in keys)
    lines = ["BGE-Remix phase 4 results (CI, fake runtime)", "=" * 44, ""]
    for k in keys:
        lines.append("%-6s %-26s %s" % (k, names[k], p2.mark(res[k]["ok"])))
        for name, ok in res[k]["checks"].items():
            if not ok:
                lines.append("         failed: " + name)
        if not res[k]["ok"] and res[k].get("detail"):
            text = json.dumps(res[k]["detail"], default=str)
            lines.append("         detail: " + (text[:4000] + " ..." if len(text) > 4000 else text))
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p4.json and the .log files)"]
    return res, lines


def check_stock(args, out, scenes):
    if not args.official:
        return dict(p2.test({"official 2.79b given (--official)": False}))
    official = os.path.abspath(args.official)
    c, d = {}, {}
    for name in ("p4_characters", "p4_crowd", "p4_physics", "p4_fountain", "p4_picking"):
        env = {"BGE_REMIX_P2_FRAMES": "300", "BGE_REMIX_P2_SECONDS": "0"}
        r = player(official, out, "stock_" + name, os.path.join(scenes, name + ".blend"), env)
        d[name] = {"exit": r["exit"], "frames": r["frames"]}
        c["%s plays 300 frames" % name] = r["exit"] == 0 and r["frames"] >= 300 and not r["traceback_in_output"]
    return dict(p2.test(c), detail=d)


# ---------------------------------------------------------------------------
# RTX (your PC)

def red_centroid(path):
    """where the bending bar's glowing red tip is (pixels), or None"""
    a = p2.image_pixels(path)
    if a is None:
        return None
    import numpy as np
    m = (a[:, :, 0] > 170) & (a[:, :, 1] < 90) & (a[:, :, 2] < 90)
    if m.sum() < 20:
        return None
    ys, xs = np.nonzero(m)
    return [float(xs.mean()), float(ys.mean()), int(m.sum())]


def run_rtx(args, out):
    scenes, expected = make_scenes(args.ours, out)
    since = time.time()
    res = {}
    clean = {"BGE_REMIX_RUNTIME_DIR": None, "BGE_REMIX_MOCK_LOG": None}

    def timed(seconds, extra=None, frames=60):
        e = dict(clean)
        e.update({"BGE_REMIX_P2_SECONDS": str(seconds), "BGE_REMIX_P2_FRAMES": str(frames),
                  "BGE_REMIX_P2_SHOT_FRAME": "30", "BGE_REMIX_CACHE_DIR": os.path.join(out, "cache"),
                  "RTX_ENABLE_ASYNC_COMPILATION": "0"})
        e.update(extra or {})
        return e

    def blend(name):
        return os.path.join(scenes, name + ".blend")

    opengl = ["-g", "remix", "=", "0"]
    freeze = {"BGE_REMIX_P4_FREEZE": "90"}

    # T4.1: characters, frozen at tic 90: OpenGL and Remix; two more Remix runs
    # tell which bone space and which weight layout Remix 1.6.1 reads, from
    # where the bending bar's red tip lands (the same place as in OpenGL)
    print("T4.1: the characters in OpenGL and in Remix (three ways of sending bones)...", flush=True)
    gl = player(args.ours, out, "T4.1_opengl", blend("p4_characters"), timed(8, freeze), size=(1280, 720),
                   args=opengl, cwd=out)
    variants = (("default", "Remix (default: object space, spaced weights)", {}),
                ("world", "Remix, bones in world space", {"BGE_REMIX_BONES_WORLD": "1"}),
                ("documented", "Remix, weights as remix_c.h describes", {"BGE_REMIX_SKIN_LAYOUT": "documented"}))
    runs = {}
    for key, label, extra in variants:
        runs[key] = player(args.ours, out, "T4.1_remix_" + key, blend("p4_characters"),
                              timed(10, dict(freeze, **extra)), size=(1280, 720), cwd=out)
    tip = {"opengl": red_centroid(gl["screenshot"])}
    for key in runs:
        tip[key] = red_centroid(runs[key]["screenshot"])

    def near(a, b):
        return bool(a and b) and math.hypot(a[0] - b[0], a[1] - b[1]) < 25.0

    matching = [k for k, _, _ in variants if near(tip["opengl"], tip[k])]
    rx = runs["default"]
    ok_r, _ = p2.has_picture(rx["screenshot"])
    st = rx["stats"]
    res["T4.1"] = {"ok": rx["exit"] == 0 and bool(ok_r) and st.get("skinned_objects") == 4 and "default" in matching,
                   "matching": matching, "bar_tip": tip, "stats": st,
                   "note": "compare T4.1_compare.png: same poses in OpenGL and Remix; sign off"}
    space = ", ".join(matching) if matching else None
    res["T4.1"]["compare"] = p2.contact_sheet(
        [("OpenGL (tic 90)", gl["screenshot"])] + [(label, runs[k]["screenshot"]) for k, label, _ in variants],
        os.path.join(out, "T4.1_compare.png"))

    # T4.2: physics, falling and settled
    print("T4.2: 200 rigid bodies and a cloth...", flush=True)
    fall = player(args.ours, out, "T4.2_falling", blend("p4_physics"), timed(1.6), size=(1280, 720), cwd=out)
    rest = player(args.ours, out, "T4.2_settled", blend("p4_physics"), timed(10), size=(1280, 720), cwd=out)
    gl2 = player(args.ours, out, "T4.2_opengl", blend("p4_physics"), timed(10), size=(1280, 720), args=opengl,
                    cwd=out)
    ok_f, _ = p2.has_picture(fall["screenshot"])
    ok_s, _ = p2.has_picture(rest["screenshot"])
    res["T4.2"] = {"ok": fall["exit"] == 0 and rest["exit"] == 0 and bool(ok_f) and bool(ok_s) and
                   rest["stats"].get("deformed_objects", 0) >= 1,
                   "stats": rest["stats"], "note": "look at T4.2_compare.png: no smearing behind falling bodies"}
    res["T4.2"]["compare"] = p2.contact_sheet(
        [("OpenGL, settled", gl2["screenshot"]), ("Remix, falling", fall["screenshot"]),
         ("Remix, settled", rest["screenshot"])], os.path.join(out, "T4.2_compare.png"))

    # T4.3 (and T4.5): the fountain
    print("T4.3: the fountain...", flush=True)
    fo = player(args.ours, out, "T4.3_fountain", blend("p4_fountain"), timed(8), size=(1280, 720), cwd=out)
    gl3 = player(args.ours, out, "T4.3_opengl", blend("p4_fountain"), timed(8), size=(1280, 720), args=opengl,
                    cwd=out)
    ok_o, _ = p2.has_picture(fo["screenshot"])
    res["T4.3"] = {"ok": fo["exit"] == 0 and bool(ok_o) and fo["stats"].get("gpu_emitters", 0) >= 1,
                   "stats": fo["stats"],
                   "note": "look at T4.3_compare.png: drops, sparks facing the camera, the tree, GPU sparks at the right"}
    res["T4.3"]["compare"] = p2.contact_sheet(
        [("OpenGL", gl3["screenshot"]), ("Remix", fo["screenshot"])], os.path.join(out, "T4.3_compare.png"))

    # T4.4: picking
    print("T4.4: picking at 9 points...", flush=True)
    pk = expected["picking"]
    pick_out = os.path.join(out, "T4.4_points.json")
    shots = os.path.join(out, "T4.4_shots")
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots, exist_ok=True)
    if os.path.isfile(pick_out):
        os.remove(pick_out)
    pr = player(args.ours, out, "T4.4_picking", blend("p4_picking"),
                   timed(6, {"BGE_REMIX_P4_PICK_OUT": pick_out, "BGE_REMIX_P4_FREEZE": "20",
                             "BGE_REMIX_P4_PICK_SHOTS": shots}, frames=300), size=(1280, 720), cwd=out)
    got = {p["point"]: p for p in (p2.read_json(pick_out) or [])}
    checks = {}
    for name in pk["points"]:
        g = got.get(name, {})
        checks[name] = {"physics": g.get("physics"), "remix": g.get("remix"),
                        "physics_ok": g.get("physics") in pk["physics"][name],
                        "remix_ok": g.get("remix") in pk["remix"][name]}
    res["T4.4"] = {"ok": pr["exit"] == 0 and len(got) == len(pk["points"]) and
                   all(v["physics_ok"] and v["remix_ok"] for v in checks.values()),
                   "points": checks, "stats": pr["stats"]}
    pics = []
    for i, name in enumerate(pk["points"]):
        if name in ("cone", "leaf_corner", "walker_hand", "gap"):
            pics.append(("%s: Remix picked %s" % (name, got.get(name, {}).get("remix")),
                         os.path.join(shots, "T4.4_%d_%s.png" % (i, name))))
    res["T4.4"]["compare"] = p2.contact_sheet(pics, os.path.join(out, "T4.4_compare.png"))

    # Benchmarks at 1080p
    print("B4.1-B4.3: frame rates at 1080p (15 s each)...", flush=True)
    bench = {}
    for tag, name, extra in (("B4.1_gpu", "p4_crowd", {}), ("B4.1_cpu", "p4_crowd", {"BGE_REMIX_CPU_SKINNING": "1"}),
                             ("B4.2_physics", "p4_physics", {}), ("B4.3_fountain", "p4_fountain", {})):
        e = timed(15, extra)
        e.pop("RTX_ENABLE_ASYNC_COMPILATION")
        b = player(args.ours, out, tag, blend(name), e, size=(1920, 1080), cwd=out)
        bench[tag] = {"frame_ms_median": b["probe"].get("frame_ms_median"), "fps": p2.fps(b["probe"].get("frame_ms_median")),
                      "frame_ms_p95": b["probe"].get("frame_ms_p95"),
                      "dynamic_ms_mean": b["stats"].get("dynamic_ms_mean"),
                      "skinned_objects": b["stats"].get("skinned_objects"),
                      "deformed_objects": b["stats"].get("deformed_objects"),
                      "draws": b["stats"].get("draws_last_frame")}
    res["B4"] = bench
    res["B4"]["targets_60fps"] = all((bench[k]["fps"] or 0) >= 60 for k in ("B4.1_gpu", "B4.2_physics", "B4.3_fountain"))
    res["B4.4"] = {"pick_frames_mean": pr["stats"].get("pick_frames_mean"),
                   "picks_answered": pr["stats"].get("picks_answered")}

    res["logs"] = p2.collect_logs(out, since, [out, args.ours])
    res["warmup"] = p2.warmups(out)
    res["all_ok"] = all(res[k]["ok"] for k in ("T4.1", "T4.2", "T4.3", "T4.4"))

    lines = ["BGE-Remix phase 4 results (RTX)", "=" * 31, ""]
    lines.append("T4.1 Characters                      %s   bar matches OpenGL with: %s   (sign-off: T4.1_compare.png)" % (
        p2.mark(res["T4.1"]["ok"]), space or "none"))
    lines.append("T4.2 Physics                         %s   (look at T4.2_compare.png)" % p2.mark(res["T4.2"]["ok"]))
    lines.append("T4.3 Fountain and GPU particles      %s   (look at T4.3_compare.png)" % p2.mark(res["T4.3"]["ok"]))
    lines.append("T4.4 Picking                         %s   (T4.4_compare.png)" % p2.mark(res["T4.4"]["ok"]))
    for name, v in res["T4.4"]["points"].items():
        if not (v["physics_ok"] and v["remix_ok"]):
            lines.append("       %-12s physics %s, Remix %s" % (name, v["physics"], v["remix"]))
    lines += ["", "Benchmarks (1080p, copy mode)", "-" * 29]
    for k in ("B4.1_gpu", "B4.1_cpu", "B4.2_physics", "B4.3_fountain"):
        b = bench[k]
        lines.append("%-14s median %s ms = %s FPS, 95th pct %s ms, skinning/deforming %s ms a frame" % (
            k, p2._r(b["frame_ms_median"]), b["fps"], p2._r(b["frame_ms_p95"]), p2._r(b["dynamic_ms_mean"])))
    lines.append("     target (60 FPS): %s" % ("met" if res["B4"]["targets_60fps"] else "NOT met"))
    lines.append("B4.4 pick answered after %s frames on average" % p2._r(res["B4.4"]["pick_frames_mean"]))
    slow = [(n, w) for n, w in sorted(res["warmup"].items()) if w is None or w > 1.0]
    lines += ["", "Remix warm-up (start to first ray-traced frame)", "-" * 47]
    lines += ["%-24s %s" % (n, "never ray-traced" if w is None else "%.1f s" % w) for n, w in slow] or \
        ["every run under 1 s"]
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p4_rtx.json and the .log files)"]
    return res, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--mock")
    ap.add_argument("--official")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rtx", action="store_true")
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    if args.rtx:
        res, lines = run_rtx(args, out)
        name = "results_p4_rtx"
    else:
        if not args.mock:
            raise SystemExit("--mock is needed without --rtx")
        res, lines = run_ci(args, out)
        name = "results_p4"
    with open(os.path.join(out, name + ".json"), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, name.replace("results", "summary") + ".txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
