"""BGE-Remix phase 6 tests: PhysX.

CI (no GPU; OpenGL, RTX Remix off):
  T6.0  PhysX in World > Physics; a PhysX scene starts and says so; stock
        Blender 2.79b opens a PhysX file with Bullet
  T6.1  rigid bodies: the stack stands, the balls bounce to their heights
        (within 10%), the ramp boxes slide or stick
  T6.2  joints: pendulum, seesaw and slider move within their joints;
        nothing drifts off its pivot
  T6.3  sensors and rays: each sensor fires on the expected tics; rays hit
        the expected object
  T6.4  character and vehicle: the capsule walks and jumps; the car drives
        straight, then turns
  T6.5  the same game with either engine: Bullet and PhysX end with the
        stack, balls and jointed objects within 5 cm and 5 degrees
  T6.7  no GPU: a GPU scene runs on the CPU with one warning; cloth stays still
  (T6.6, regression, is the P0-P5 runs in the same CI job)
RTX (your PC):
  T6.0  the GPU scene runs on the GPU
  T6.5g the GPU against Bullet; the cloth falls on the GPU
  T6.8  the bathroom on PhysX (GPU) in RTX Remix: rings, curtain, lamp,
        stool, duck, drops; a picture sheet and an animation
  T6.9  the bathroom, Bullet against PhysX: the same moments side by side
  B6.1  physics step with 1,000 and 5,000 boxes: Bullet, PhysX CPU, PhysX GPU
  B6.2  the curtain's step time on the GPU
  B6.3  the bathroom's frame rate in RTX Remix, 1080p, PhysX GPU

  python run_p6.py --ours <build> --official <2.79b> --out <dir>
  python run_p6.py --ours <build> --out <dir> --rtx [--bathroom <file>]
"""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
import run_p2 as p2  # noqa: E402  (running Blender)

ENGINES = ("bullet", "physx_cpu", "physx_gpu")
BATHROOM = r"C:\BGE-Remix\transfer\bathroom_final_dof_noise.blend"
FIXED = ["-g", "fixedtime", "=", "1"]   # one 1/60 s physics step per logic tic


# ---------------------------------------------------------------------------
# Running

def make_scenes(ours, out):
    scenes = os.path.join(out, "scenes")
    if os.path.isdir(scenes):
        shutil.rmtree(scenes, ignore_errors=True)
    os.makedirs(scenes, exist_ok=True)
    code, text, _ = p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup",
                            "--python", os.path.join(HERE, "make_scenes_p6.py"), "--", scenes],
                           p2.base_env(out, "make"), os.path.join(out, "make_scenes_p6.log"))
    expected = p2.read_json(os.path.join(scenes, "p6_expected.json"))
    if code != 0 or not expected:
        print("\n".join(text.splitlines()[-40:]))
        raise SystemExit("making the P6 test scenes failed, see make_scenes_p6.log")
    for name, tb in expected.get("failed", {}).items():
        print("making the %s scene failed:\n%s" % (name, tb))
    return scenes, expected


def log_text(out, name):
    try:
        with open(os.path.join(out, name + ".log"), encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def player(ours, out, name, blend, env=None, size=(640, 360), args=(), remix=False, timeout=1800):
    extra = [] if remix else ["-g", "remix", "=", "0"]
    r = p2.player(ours, out, name, blend, dict(env or {}), size=size, args=list(args) + extra, timeout=timeout)
    r["log"] = log_text(out, name)
    if r["exit"] != 0 or r["traceback_in_output"] or not r["frames"]:
        print("---- %s: exit %s, %s frames; the end of its output:" % (name, r["exit"], r["frames"]))
        print("\n".join("    " + l for l in r["log"].splitlines()[-30:]))
    return r


def ran(r, frames):
    return r["exit"] == 0 and r["frames"] >= frames and not r["traceback_in_output"]


# ---------------------------------------------------------------------------
# Geometry

def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def matrix(e):
    """rotation matrix of XYZ euler angles (Blender's order)"""
    cx, cy, cz = math.cos(e[0]), math.cos(e[1]), math.cos(e[2])
    sx, sy, sz = math.sin(e[0]), math.sin(e[1]), math.sin(e[2])
    return [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz],
            [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz],
            [-sy, sx * cy, cx * cy]]


def angle_between(e1, e2):
    a, b = matrix(e1), matrix(e2)
    tr = sum(a[i][k] * b[i][k] for i in range(3) for k in range(3))
    return math.degrees(math.acos(max(-1.0, min(1.0, (tr - 1.0) / 2.0))))


def heading(p):
    """the direction the car's +Y points to (degrees)"""
    m = matrix(p[3:6])
    return math.degrees(math.atan2(m[1][1], m[0][1]))


def first_rebound(track, radius, drop):
    """the bottom's height at the top of the first bounce"""
    z = [p[2] - radius for p in track]
    up = next((i for i in range(1, len(z)) if z[i] > z[i - 1] + 1e-4), None)
    if up is None:
        return 0.0
    top = up
    while top + 1 < len(z) and z[top + 1] >= z[top]:
        top += 1
    return z[top]


def tic_ranges(tics):
    return [tics[0], tics[-1]] if tics else None


# ---------------------------------------------------------------------------
# The checks of one run of p6_physics

def check_rigid(probe, expected):
    tr, ev = probe.get("track", {}), probe.get("every", {})
    c, d = {}, {}
    worst = 0.0
    for name, want in expected["stack"].items():
        last = ev.get(name, [[0, 0, 0, -99]])[-1]
        worst = max(worst, dist(last[1:4], want))
    c["the stack stands (every box within 5 cm of its place)"] = worst < 0.05
    d["stack_worst_m"] = round(worst, 4)
    for name, info in expected["balls"].items():
        h = first_rebound(tr.get(name, [[0, 0, 0]]), 0.25, 3.0)
        want = info["rebound"]
        ok = h < 0.1 if want < 0.1 else abs(h - want) <= 0.1 * want
        c["%s (bounce %.1f) rebounds to %.2f m (+-10%%)" % (name, info["bounce"], want)] = ok
        d[name + "_rebound_m"] = round(h, 3)
    r0, r1 = tr.get("RampBox_0", []), tr.get("RampBox_1", [])
    if r0 and r1:
        d["ramp_slide_m"] = round(dist(r0[0][:3], r0[min(119, len(r0) - 1)][:3]), 3)
        d["ramp_stick_m"] = round(dist(r1[0][:3], r1[-1][:3]), 3)
    c["the ramp box with friction 0.1 slides (over 0.5 m in 2 s)"] = d.get("ramp_slide_m", 0) > 0.5
    c["the ramp box with friction 0.9 sticks (under 5 cm)"] = d.get("ramp_stick_m", 9) < 0.05
    return c, d


def check_joints(probe, expected):
    tr = probe.get("track", {})
    c, d = {}, {}
    pend = expected["pendulum"]
    chain = [tr.get(n, []) for n in pend["chain"]]
    if all(chain):
        links = []
        for k in range(len(chain[0])):
            prev = pend["anchor"]
            for j in range(3):
                links.append(abs(dist(prev, chain[j][k][:3]) - 1.0))
                prev = chain[j][k][:3]
        d["pendulum_link_error_m"] = round(max(links), 4)
        d["pendulum_end"] = [p[-1][:3] for p in chain]
        c["the pendulum's links stay 1 m (within 10 cm while it falls)"] = max(links) < 0.1
        c["the pendulum comes to rest hanging down (within 15 cm)"] = all(
            dist(chain[j][-1][:3], pend["rest"][j]) < 0.15 for j in range(3))
    plank = tr.get("Plank", [])
    if plank:
        drift = max(dist(p[:3], plank[0][:3]) for p in plank)
        off_axis = max(max(abs(p[4]), abs(p[5])) for p in plank)
        d.update(seesaw_drift_m=round(drift, 4), seesaw_off_axis_rad=round(off_axis, 4), seesaw_tilt=plank[-1][3])
        c["the seesaw stays on its hinge (within 3 cm)"] = drift < 0.03
        c["the seesaw turns only about its hinge (under 3 degrees off)"] = off_axis < math.radians(3)
        c["the box tips the seesaw (its +Y end down, over 10 degrees)"] = plank[-1][3] < -math.radians(10)
    sl = tr.get("Slider", [])
    if sl:
        start = sl[0][:3]
        side = max(max(abs(p[1] - start[1]), abs(p[2] - start[2])) for p in sl)
        d.update(slider_side_m=round(side, 4), slider_end=sl[-1][:3], slider_max_x=max(p[0] for p in sl))
        c["the slider keeps to its axis (within 2 cm)"] = side < 0.02
        top = max(p[0] for p in sl)
        c["the slider reaches its limit (+1 m) and stops there (within 5 cm)"] = \
            abs(top - expected["slider"]["end"][0]) < 0.05
    return c, d


def check_sensors(probe, expected):
    c, d = {}, {}
    got = probe.get("sensors", {})
    for name, (first, last) in expected["sensors"].items():
        t = tic_ranges(got.get(name, []))
        d[name] = t
        c["%s fires from tic %d to %d (+-3)" % (name, first, last)] = bool(t) and abs(t[0] - first) <= 3 and \
            abs(t[1] - last) <= 3
    rays = probe.get("rays", {})
    for label, (obj, z) in expected["rays"].items():
        hit = rays.get(label, [None, None])
        d["ray_" + label] = hit
        c["ray %s hits %s at z %.2f" % (label, obj, z)] = hit[0] == obj and hit[1] is not None and \
            abs(hit[1][2] - z) < 0.05
    return c, d


def check_moving(probe, expected):
    tr = probe.get("track", {})
    c, d = {}, {}
    ch = tr.get("Character", [])
    if ch:
        rest = ch[25][2]
        walked = ch[99][0] - ch[0][0]
        top = max(p[2] for p in ch[115:200]) - rest
        d.update(char_walk_m=round(walked, 3), char_jump_m=round(top, 3), char_rest_z=rest, char_end=ch[-1][:3])
        c["the character walks 3 m (+-15 cm)"] = abs(walked - expected["character"]["walk"]) < 0.15
        c["the character jumps %.2f m (+-20 cm)" % expected["character"]["jump"]] = \
            abs(top - expected["character"]["jump"]) < 0.2
        c["the character lands again"] = abs(ch[-1][2] - rest) < 0.05
    car = tr.get("Car", [])
    if len(car) > 300:
        straight = abs(heading(car[179]) - heading(car[59]))
        turned = abs(heading(car[299]) - heading(car[179]))
        moved = car[179][1] - car[59][1]
        still = dist(car[-1][:3], car[-31][:3])
        d.update(car_straight_deg=round(straight, 2), car_turn_deg=round(turned, 1), car_forward_m=round(moved, 2),
                 car_end=car[-1][:3])
        c["the car drives straight (over 1.5 m, under 3 degrees)"] = moved > 1.5 and straight < 3.0
        c["then turns (over 20 degrees)"] = turned > 20.0
        c["and stops (braking)"] = still < 0.05
        c["and stays upright on its wheels"] = all(abs(p[3]) < 0.3 and abs(p[4]) < 0.3 for p in car[30:])
    return c, d


def compare(a, b, expected):
    """T6.5: where the two runs end (stack, balls, jointed objects)"""
    c, d = {}, {}
    worst = (0.0, 0.0, None)
    ev_a, ev_b = a.get("every", {}), b.get("every", {})
    tr_a, tr_b = a.get("track", {}), b.get("track", {})
    items = []
    for name in expected["stack"]:
        if name in ev_a and name in ev_b:
            items.append((name, ev_a[name][-1][1:], ev_b[name][-1][1:]))
    # (the slider leaves out: Bullet's 6DOF limit bounces back, PhysX's stops)
    for name in ["Ball_0", "Ball_1", "Plank"] + expected["pendulum"]["chain"]:
        if name in tr_a and name in tr_b:
            items.append((name, tr_a[name][-1], tr_b[name][-1]))
    bad = []
    for name, pa, pb in items:
        dp, da = dist(pa[:3], pb[:3]), angle_between(pa[3:6], pb[3:6])
        # balls turn freely: their angle does not count
        if name.startswith("Ball") or name.startswith("Pendulum"):
            da = 0.0
        if dp > worst[0]:
            worst = (dp, worst[1], name)
        worst = (worst[0], max(worst[1], da), worst[2])
        if dp > 0.05 or da > 5.0:
            bad.append([name, round(dp, 3), round(da, 1)])
    c["%d objects end within 5 cm and 5 degrees of each other" % len(items)] = bool(items) and not bad
    d.update(worst_m=round(worst[0], 4), worst_deg=round(worst[1], 2), worst_object=worst[2], outside=bad[:20])
    h_a = first_rebound(tr_a.get("Ball_2", [[0, 0, 0]]), 0.25, 3.0)
    h_b = first_rebound(tr_b.get("Ball_2", [[0, 0, 0]]), 0.25, 3.0)
    c["the bounciest ball rebounds alike (within 10%)"] = h_a > 0 and abs(h_a - h_b) <= 0.1 * h_a
    d["ball2_rebounds"] = [round(h_a, 3), round(h_b, 3)]
    return c, d


def test(checks, run_ok=True):
    return {"ok": bool(checks) and all(checks.values()) and run_ok, "checks": checks}


def physics_runs(args, out, scenes, expected, keys, gpu_expected):
    runs = {}
    for key in keys:
        runs[key] = player(args.ours, out, "physics_" + key, expected["physics"]["files"][key], args=FIXED)
    return runs


def official_check(official, out, blend):
    """stock 2.79b opens the PhysX file: Bullet, the choice kept in the ID property"""
    expr = ("import bpy; sc = bpy.context.scene; "
            "print('P6CHECK', sc.game_settings.physics_engine, sc.get('_bge_physx'))")
    code, text, _ = p2.run([p2.exe(official, "blender"), "--background", blend, "--python-expr", expr],
                           p2.base_env(out, "official"), os.path.join(out, "official_physx_file.log"), timeout=300)
    line = next((l for l in text.splitlines() if l.startswith("P6CHECK")), "")
    return line


# ---------------------------------------------------------------------------
# CI

def run_ci(args, out):
    scenes, expected = make_scenes(args.ours, out)
    ex = expected["physics"]
    res = {}
    runs = physics_runs(args, out, scenes, expected, ENGINES, False)
    frames = ex["tics"]

    # T6.0
    c = {
        "PhysX in World > Physics": "PHYSX" in expected.get("engine_items", []),
        "the Bullet scene uses Bullet": ran(runs["bullet"], frames) and "uses PhysX" not in runs["bullet"]["log"],
        "the PhysX scene says it uses PhysX (CPU)": 'uses PhysX (CPU)' in runs["physx_cpu"]["log"],
        "PhysX started": "PhysX 5.9.1 started" in runs["physx_cpu"]["log"],
    }
    d = {}
    if args.official:
        line = official_check(args.official, out, ex["files"]["physx_cpu"])
        d["official"] = line
        c["stock 2.79b opens the PhysX file with Bullet (the choice kept)"] = line.split()[1:3] == ["BULLET", "1"]
    res["T6.0"] = dict(test(c), detail=d)

    for key in ("bullet", "physx_cpu"):
        r = runs[key]
        ok = ran(r, frames)
        for tid, fn in (("T6.1", check_rigid), ("T6.2", check_joints), ("T6.3", check_sensors),
                        ("T6.4", check_moving)):
            cc, dd = fn(r["probe"], ex)
            res["%s_%s" % (tid, key)] = dict(test(cc, ok), detail=dd)

    cc, dd = compare(runs["bullet"]["probe"], runs["physx_cpu"]["probe"], ex)
    res["T6.5"] = dict(test(cc, ran(runs["bullet"], frames) and ran(runs["physx_cpu"], frames)), detail=dd)

    # T6.7: the GPU copy on a machine without a GPU
    g = runs["physx_gpu"]
    log = g["log"]
    cloth_cpu = runs["physx_cpu"]["probe"].get("track", {}).get("Cloth", [])
    cloth_bullet = runs["bullet"]["probe"].get("track", {}).get("Cloth", [])
    cc, dd = compare(runs["physx_cpu"]["probe"], g["probe"], ex)
    c = {
        "the GPU scene ran": ran(g, frames),
        "one warning: it runs on the CPU": log.count("asks for the GPU, it runs on the CPU") == 1,
        "the scene says PhysX (CPU)": "uses PhysX (CPU)" in log,
        "it ends as the CPU scene does": all(cc.values()),
        "cloth stays still on the CPU": bool(cloth_cpu) and abs(cloth_cpu[-1][2] - cloth_cpu[0][2]) < 1e-3,
        "with one warning": runs["physx_cpu"]["log"].count("soft bodies need the GPU") == 1,
        "(the same cloth falls with Bullet)": bool(cloth_bullet) and cloth_bullet[-1][2] < cloth_bullet[0][2] - 1.0,
    }
    res["T6.7"] = dict(test(c), detail=dd)

    names = {"T6.0": "Build and choice",
             "T6.1_bullet": "Rigid bodies (Bullet)", "T6.1_physx_cpu": "Rigid bodies (PhysX)",
             "T6.2_bullet": "Joints (Bullet)", "T6.2_physx_cpu": "Joints (PhysX)",
             "T6.3_bullet": "Sensors and rays (Bullet)", "T6.3_physx_cpu": "Sensors and rays (PhysX)",
             "T6.4_bullet": "Character and vehicle (Bullet)", "T6.4_physx_cpu": "Character and vehicle (PhysX)",
             "T6.5": "Same game, either engine", "T6.7": "No GPU"}
    res["runs"] = {k: {"exit": v["exit"], "frames": v["frames"], "physics_ms": v["probe"].get("physics_ms_mean")}
                   for k, v in runs.items()}
    res["all_ok"] = all(res[k]["ok"] for k in names)
    lines = ["BGE-Remix phase 6 results (CI, PhysX on the CPU)", "=" * 48, ""]
    for k in names:
        lines.append("%-16s %-34s %s" % (k, names[k], p2.mark(res[k]["ok"])))
        for name, ok in res[k]["checks"].items():
            if not ok:
                lines.append("         failed: " + name)
    lines += ["", "physics ms per frame: " + ", ".join("%s %s" % (k, _r(v["physics_ms"])) for k, v in res["runs"].items()),
              "", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p6.json and the .log files)"]
    return res, lines


def _r(v):
    return "-" if v is None else "%.2f" % v


# ---------------------------------------------------------------------------
# RTX (your PC)

def adapt_bathroom(args, out):
    folder = os.path.join(out, "bathroom")
    if os.path.isdir(folder):
        shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder, exist_ok=True)
    code, text, _ = p2.run([p2.exe(args.ours, "blender"), "--background", args.bathroom, "--python",
                            os.path.join(HERE, "adapt_bathroom.py"), "--", folder],
                           p2.base_env(out, "adapt"), os.path.join(out, "adapt_bathroom.log"))
    report = p2.read_json(os.path.join(folder, "bathroom_p6.json"))
    if code != 0 or not report.get("copies"):
        print("\n".join(text.splitlines()[-30:]))
    return report


SHOT_TICS = [360, 420, 480, 540, 600, 660, 720, 840]   # START 300 + the driver's moments


def bathroom_run(args, out, name, blend, size=(1280, 720), shots=None, fixed=True, start=300, length=600):
    env = {"BGE_REMIX_P6_DRIVE": "1", "BGE_REMIX_P6_START": str(start), "BGE_REMIX_P6_LENGTH": str(length)}
    paths = {}
    if shots:
        items = []
        for tic in shots:
            path = os.path.join(out, "%s_%04d.png" % (name, tic))
            if os.path.isfile(path):
                os.remove(path)
            paths[tic] = path
            items.append("%d:%s" % (tic, path))
        env["BGE_REMIX_P6_SHOTS"] = ";".join(items)
    r = player(args.ours, out, name, blend, env, size=size, args=FIXED if fixed else [], remix=True, timeout=3600)
    r["shots"] = paths
    return r


def check_bathroom(probe, physx):
    rec = probe.get("rec", [])
    c, d = {}, {}
    if len(rec) < 600:
        return {"the driven run completed": False}, d
    start = len(rec) - 601
    seg = rec[start:]
    rings = ["Empty.002", "Empty.003", "Empty.004", "Empty.005", "Empty.006"]
    off = max(max(abs(r[n][1] - seg[0][n][1]), abs(r[n][2] - seg[0][n][2])) for r in seg for n in rings if n in r)
    d["ring_off_rail_m"] = round(off, 4)
    c["the rings stay on the rail (within 2 cm)"] = off < 0.02
    m1 = seg[120]["Empty.002"][0] - seg[60]["Empty.002"][0]
    m5 = seg[210]["Empty.006"][0] - seg[150]["Empty.006"][0]
    d.update(ring1_slide_m=round(m1, 3), ring5_slide_m=round(m5, 3))
    c["S slides ring 1 along the rail"] = m1 < -0.2
    c["Q slides ring 5 along the rail"] = m5 > 0.2
    if "curtain_top" in seg[0]:
        ring_x = [seg[0][n][0] for n in rings]
        top = seg[0]["curtain_top"]
        hang = max(min(abs(v[0] - x) for x in ring_x) for v in top[:5])
        d["curtain_top_from_rings_m"] = round(hang, 3)
    zs = [r.get("curtain_z", [0, 0, 0]) for r in seg]
    d["curtain_mean_z"] = [zs[0][2], max(z[2] for z in zs[240:330]), zs[-1][2]]
    c["the curtain hangs from the rings (its top within 50 cm of the rail)"] = max(z[1] for z in zs) > 4.9
    c["Space lifts the curtain (its middle up over 50 cm)"] = max(z[2] for z in zs[240:330]) > zs[230][2] + 0.5
    lamp = [r["Sphere.001"][0] for r in seg if "Sphere.001" in r]
    light = [r["Lamp"][0] for r in seg if "Lamp" in r]
    d["lamp_swing_m"] = round(max(lamp) - min(lamp), 3) if lamp else None
    c["the lamp swings"] = bool(lamp) and max(lamp) - min(lamp) > 0.05
    c["its light swings with it"] = bool(light) and max(light) - min(light) > 0.05
    stool = [r["Cube.001"] for r in seg if "Cube.001" in r]
    d["stool_moved_m"] = round(dist(stool[0], stool[-1]), 3) if stool else None
    c["the stool rests"] = bool(stool) and dist(stool[0], stool[-1]) < 0.1
    ducks = [r for r in seg if "duck" in r]
    if ducks:
        size0 = [ducks[0]["duck"][k + 3] - ducks[0]["duck"][k] for k in range(3)]
        sizes = [[r["duck"][k + 3] - r["duck"][k] for k in range(3)] for r in ducks]
        squash = max(abs(s[k] - size0[k]) for s in sizes for k in range(3))
        d.update(duck_lowest_z=min(r["duck"][2] for r in ducks), duck_squash_m=round(squash, 3))
        c["the duck is added and falls to the floor"] = min(r["duck"][2] for r in ducks) < 0.4
        if physx:
            c["the duck is soft (its shape changes over 2 cm)"] = squash > 0.02
    else:
        c["the duck is added and falls to the floor"] = False
    c["water drops fall from the tap"] = max(r.get("drops", 0) for r in seg) > 0
    return c, d


def animation(frames, target, size=(640, 360)):
    try:
        from PIL import Image
    except ImportError:
        return None
    imgs = [Image.open(f).convert("RGB").resize(size) for f in frames if f and os.path.isfile(f)]
    if not imgs:
        return None
    imgs[0].save(target, save_all=True, append_images=imgs[1:], duration=100, loop=0)
    return target


def run_rtx(args, out):
    scenes, expected = make_scenes(args.ours, out)
    ex = expected["physics"]
    res = {}
    lines = ["BGE-Remix phase 6 results (RTX)", "=" * 32, ""]

    # T6.0, T6.5g: the test scene with each engine (OpenGL: the physics only)
    runs = physics_runs(args, out, scenes, expected, ENGINES, True)
    glog = runs["physx_gpu"]["log"]
    c = {"PhysX in World > Physics": "PHYSX" in expected.get("engine_items", []),
         "the GPU scene runs on the GPU": "PhysX uses the GPU" in glog and "uses PhysX (GPU)" in glog}
    res["T6.0"] = dict(test(c), detail={"gpu_line": next((l for l in glog.splitlines() if "uses the GPU" in l), None)})
    cc, dd = compare(runs["bullet"]["probe"], runs["physx_gpu"]["probe"], ex)
    for fn in (check_rigid, check_joints, check_sensors, check_moving):
        c2, d2 = fn(runs["physx_gpu"]["probe"], ex)
        cc.update(c2)
        dd.update(d2)
    cloth = runs["physx_gpu"]["probe"].get("track", {}).get("Cloth", [])
    cc["the cloth falls on the GPU"] = bool(cloth) and cloth[-1][2] < cloth[0][2] - 1.0
    res["T6.5g"] = dict(test(cc, ran(runs["physx_gpu"], ex["tics"])), detail=dd)

    # B6.1: the physics step with many boxes
    bench = {}
    for n in (1000, 5000):
        for key in ENGINES:
            r = player(args.ours, out, "bench_%s_%d" % (key, n), expected["bench"]["files"][key],
                       {"BGE_REMIX_P6_BOXES": str(n)}, args=FIXED)
            bench["%s_%d" % (key, n)] = r["probe"].get("physics_ms_mean")
    res["B6.1"] = {"ok": None, "checks": {}, "physics_ms": bench}

    # T6.8, T6.9, B6.2, B6.3: the bathroom
    report = adapt_bathroom(args, out) if args.bathroom and os.path.isfile(args.bathroom) else {}
    copies = report.get("copies", {})
    if copies:
        gpu = bathroom_run(args, out, "bath_gpu", copies["physx_gpu"],
                           shots=SHOT_TICS + list(range(300, 901, 6)))
        cpu_log = gpu["log"]
        c, d = check_bathroom(gpu["probe"], True)
        c["the bathroom ran on PhysX (GPU) in RTX Remix"] = "uses PhysX (GPU)" in cpu_log
        anim = animation([gpu["shots"].get(t) for t in range(300, 901, 6)], os.path.join(out, "bathroom_physx_gpu.gif"))
        sheet = p2.contact_sheet([("PhysX GPU, tic %d" % t, gpu["shots"].get(t)) for t in SHOT_TICS],
                                 os.path.join(out, "sheet_bathroom_physx_gpu.png"))
        d.update(animation=anim, sheet=sheet)
        res["T6.8"] = dict(test(c, ran(gpu, 900)), detail=d)
        res["B6.2"] = {"ok": None, "checks": {}, "physics_ms_gpu": gpu["probe"].get("physics_ms_mean")}

        bullet = bathroom_run(args, out, "bath_bullet", copies["bullet"], shots=SHOT_TICS)
        c2, d2 = check_bathroom(bullet["probe"], False)
        items = []
        for t in SHOT_TICS:
            items.append(("Bullet, tic %d" % t, bullet["shots"].get(t)))
            items.append(("PhysX GPU, tic %d" % t, gpu["shots"].get(t)))
        sheet2 = p2.contact_sheet(items, os.path.join(out, "sheet_bathroom_bullet_physx.png"))
        res["T6.9"] = dict(test({"the Bullet copy runs": ran(bullet, 900), "the PhysX copy runs": ran(gpu, 900)}),
                           detail={"sheet": sheet2, "bullet_checks": c2, "bullet_detail": d2})

        # B6.3: real time, 1080p, Remix
        fps = bathroom_run(args, out, "bath_fps", copies["physx_gpu"], size=(1920, 1080), fixed=False)
        ms = fps["probe"].get("frame_ms_median")
        res["B6.3"] = {"ok": bool(ms) and 1000.0 / ms >= 60.0, "checks": {"60 FPS": bool(ms) and 1000.0 / ms >= 60.0},
                       "fps": round(1000.0 / ms, 1) if ms else None,
                       "physics_ms": fps["probe"].get("physics_ms_mean")}
    else:
        res["T6.8"] = {"ok": False, "checks": {"the bathroom file was found (--bathroom)": False}}

    keys = [k for k in ("T6.0", "T6.5g", "T6.8", "T6.9", "B6.3") if k in res]
    res["all_ok"] = all(res[k]["ok"] for k in keys)
    names = {"T6.0": "PhysX on the GPU", "T6.5g": "Test scene on the GPU, against Bullet",
             "T6.8": "Bathroom on PhysX (GPU) in Remix", "T6.9": "Bathroom, Bullet against PhysX",
             "B6.3": "Bathroom 60 FPS, 1080p, Remix"}
    for k in keys:
        lines.append("%-6s %-40s %s" % (k, names[k], p2.mark(res[k]["ok"])))
        for name, ok in res[k]["checks"].items():
            if not ok:
                lines.append("         failed: " + name)
    lines.append("")
    lines.append("B6.1 physics ms per frame (1,000 / 5,000 boxes):")
    for key in ENGINES:
        lines.append("   %-10s %8s %8s" % (key, _r(bench.get(key + "_1000")), _r(bench.get(key + "_5000"))))
    if "B6.2" in res:
        lines.append("B6.2 bathroom physics on the GPU (curtain, duck, rigid bodies): %s ms per frame"
                     % _r(res["B6.2"]["physics_ms_gpu"]))
    if "B6.3" in res:
        lines.append("B6.3 bathroom in Remix at 1080p: %s FPS" % res["B6.3"].get("fps"))
    for k, label in (("T6.8", "sheet"), ("T6.8", "animation"), ("T6.9", "sheet")):
        if k in res and res[k].get("detail", {}).get(label):
            lines.append("%s %s: %s" % (k, label, res[k]["detail"][label]))
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p6_rtx.json and the .log files)"]
    return res, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--official")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rtx", action="store_true")
    ap.add_argument("--bathroom", default=BATHROOM)
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    if args.rtx:
        res, lines = run_rtx(args, out)
        name = "results_p6_rtx"
    else:
        res, lines = run_ci(args, out)
        name = "results_p6"
    with open(os.path.join(out, name + ".json"), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, name.replace("results", "summary") + ".txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
