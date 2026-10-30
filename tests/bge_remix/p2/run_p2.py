#!/usr/bin/env python3
"""BGE-Remix phase 2 tests and benchmarks (host side, Python 3.8+).

CI, with the fake runtime (no GPU needed):
  python run_p2.py --ours <folder with blenderplayer.exe> --mock <folder with mock d3d9.dll> --out <folder>

  T2.2  1,000 instances: one mesh, 1,001 draws per frame, nothing created later
  T2.3a the copied picture reaches the game window, BGE's text on top
  T2.4a Remix off: no Remix at all; Remix on but missing or failing: OpenGL + notice
  T2.5  the camera matrices Remix gets equal BGE's
  T2.6  lamps arrive as the right Remix lights
  T2.7  materials arrive as set in the scene
  T2.0  the C and Python sides find the same runtime folder; versions match

Your PC, with the real runtime (RTX GPU):
  python run_p2.py --ours <folder> --out <folder> --rtx

  T2.1 lit room (+ its Cycles render), T2.3 text on top, T2.4 switching and
  T2.9 the embedded player (one Blender session), C1 capture retry,
  B2.1 frame rates, B2.2 1,000 instances, B2.3 cost of copy mode.

T2.8 (regression) is the P0 and P1 runs in the same CI job.
"""

import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
TIMEOUT = 600
FILL = (64, 128, 192)  # the fake runtime's picture (R, G, B)


# ---------------------------------------------------------------------------
# Running things

def exe(folder, name):
    path = os.path.join(folder, name + ".exe")
    if not os.path.isfile(path):
        raise SystemExit("not found: " + path)
    return path


def run(cmd, env, log, timeout=TIMEOUT, cwd=None):
    full = dict(os.environ)
    for k, v in env.items():
        if v is None:
            full.pop(k, None)
        else:
            full[k] = v
    start = time.time()
    try:
        p = subprocess.run(cmd, env=full, timeout=timeout, cwd=cwd,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        code, out = p.returncode, p.stdout
    except subprocess.TimeoutExpired as ex:
        code, out = "timeout", ex.stdout or b""
    with open(log, "wb") as f:
        f.write(out)
    return code, out.decode("utf-8", "replace"), time.time() - start


def base_env(out, name):
    env = {}
    for key, sub in (("BLENDER_USER_CONFIG", "config"), ("BLENDER_USER_SCRIPTS", "scripts")):
        path = os.path.join(out, "user_" + name, sub)
        os.makedirs(path, exist_ok=True)
        env[key] = path
    return env


def read_json(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def read_lines(path):
    if not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8", errors="replace") as f:
        return [l.strip() for l in f if l.strip()]


def make_scenes(ours, out):
    scenes = os.path.join(out, "scenes")
    os.makedirs(scenes, exist_ok=True)
    code, text, _ = run([exe(ours, "blender"), "--background", "--factory-startup",
                         "--python", os.path.join(HERE, "make_scenes_p2.py"), "--", scenes],
                        base_env(out, "make"), os.path.join(out, "make_scenes_p2.log"))
    if code != 0 or "Traceback" in text:
        # the end of Blender's output, readable on the CI page
        print("\n".join(text.splitlines()[-40:]))
        raise SystemExit("making the P2 test scenes failed, see make_scenes_p2.log")
    return scenes, read_json(os.path.join(scenes, "p2_expected.json"))


def player(ours, out, name, blend, env_extra, size=(640, 360), args=(), timeout=TIMEOUT, cwd=None):
    """Run blenderplayer on a P2 scene; returns the probe's and the engine's results."""
    res = os.path.join(out, name + ".json")
    stats = os.path.join(out, name + "_stats.json")
    shot = os.path.join(out, name + ".png")
    for p in (res, stats, shot):
        if os.path.isfile(p):
            os.remove(p)
    env = base_env(out, name)
    env.update({"BGE_REMIX_P2_OUT": res, "BGE_REMIX_STATS": stats, "BGE_REMIX_P2_SHOT": shot})
    env.update(env_extra)
    cmd = [exe(ours, "blenderplayer"), "-w", str(size[0]), str(size[1])] + list(args) + [blend]
    code, text, secs = run(cmd, env, os.path.join(out, name + ".log"), timeout=timeout, cwd=cwd)
    data = {"exit": code, "seconds_total": round(secs, 1), "probe": read_json(res), "stats": read_json(stats),
            "screenshot": shot if os.path.isfile(shot) else None,
            "traceback_in_output": "Traceback" in text}
    data["frames"] = data["probe"].get("frames", 0)
    return data


# ---------------------------------------------------------------------------
# The fake runtime's log

def fnv1a(text):
    h = 1469598103934665603
    for c in text.encode("utf-8"):
        h ^= c
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h or 1


def parse_fields(line):
    """'CreateLight hash=1f type=sphere pos=1,2,3' -> {'hash': '1f', 'type': 'sphere', 'pos': [1, 2, 3]}"""
    out = {}
    for key, value in re.findall(r"(\w+)=(\S+)", line):
        if key == "hash":
            out[key] = value
            continue
        parts = value.split(",")
        try:
            nums = [float(p) for p in parts]
            out[key] = nums if len(nums) > 1 else nums[0]
        except ValueError:
            out[key] = value
    return out


def created(lines, prefix):
    """{hash (int): fields} of the 'Create...' lines, the last one per hash."""
    result = {}
    for l in lines:
        if l.startswith(prefix + " "):
            f = parse_fields(l)
            result[int(f["hash"], 16)] = f
    return result


def srgb(c):
    """linear -> sRGB-encoded, as the bridge sends colours (Remix's shaders
    decode them from gamma themselves); a list is converted per channel"""
    if isinstance(c, list):
        return [srgb(x) for x in c]
    c = min(max(float(c), 0.0), 1.0)
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1.0 / 2.4) - 0.055


def close(a, b, tol=1e-3):
    if isinstance(a, list) or isinstance(b, list):
        if not isinstance(a, list) or not isinstance(b, list) or len(a) != len(b):
            return False
        return all(abs(x - y) <= tol * max(1.0, abs(y)) for x, y in zip(a, b))
    return abs(a - b) <= tol * max(1.0, abs(b))


# ---------------------------------------------------------------------------
# CI checks

def check_sequence(lines, stats):
    presents = [l for l in lines if l.startswith("Present")]
    checks = {
        "Remix's own device (copy mode)": "CreateD3D9 editor=0" in lines and
        any(l.startswith("CreateDeviceEx hwnd_valid=1 child=0") for l in lines) and "RegisterD3D9Device ok=1" in lines,
        "one Present per Remix frame": len(presents) == stats.get("frames", -1) and len(presents) > 0,
        "Shutdown last, at the player's exit": bool(lines) and lines[-1] == "Shutdown",
        "no window message after Shutdown": not [l for l in lines if l.startswith("WndProc after")],
        "no runtime errors": stats.get("errors", 1) == 0,
        "every mesh: triangles clockwise, as Remix wants": all(
            parse_fields(l).get("winding") == "cw" for l in lines if l.startswith("CreateMesh ")),
    }
    return checks


def check_camera(lines, probe):
    cam = (probe or {}).get("camera")
    logged = [parse_fields(l) for l in lines if l.startswith("Camera ")]
    if not cam or not logged:
        return {"camera seen": False}, {}
    last = logged[-1]
    mv, pr = cam["modelview"], cam["projection"]
    # OpenGL memory order is column major: element (row r, column c) at c * 4 + r
    view_expected = [mv[r][c] for c in range(4) for r in range(4)]
    proj_d3d = [row[:] for row in pr]
    proj_d3d[2] = [0.5 * pr[2][c] + 0.5 * pr[3][c] for c in range(4)]
    proj_expected = [proj_d3d[r][c] for c in range(4) for r in range(4)]
    checks = {
        "a still camera stays still": len(logged) <= 3,
        "view matrix as BGE's (1e-4)": close(last["view"], view_expected, 1e-4),
        "projection as BGE's, depth 0..1 (1e-4)": close(last["proj"], proj_expected, 1e-4),
        "lens shift reaches Remix": abs(proj_expected[8]) > 1e-3 and close(last["proj"][8], proj_expected[8], 1e-4),
    }
    return checks, {"logged": last, "bge_view": view_expected, "bge_proj_d3d": proj_expected}


def check_lamps(lines, expected):
    lights = created(lines, "CreateLight")
    checks = {}
    detail = {}
    dome = [f for f in lights.values() if f.get("type") == "dome"]
    for e in expected["lamps"]:
        name, kind = e["name"], e["type"]
        if kind == 'HEMI':
            continue
        f = lights.get(fnv1a("light:" + name))
        detail[name] = f
        if f is None:
            checks[name + " arrives"] = False
            continue
        want = {"POINT": "sphere", "SPOT": "sphere", "SUN": "distant", "AREA": "rect"}[kind]
        ok = f.get("type") == want
        if kind in ("POINT", "SPOT"):
            ok = ok and close(f["pos"], e["position"]) and close(f["radius"], max(e["soft"], 0.02))
        if kind == "SPOT":
            ok = ok and f.get("shaped") == 1 and close(f["cone"], e["cone_deg"]) and \
                close(f["softness"], e["softness"]) and close(f["dir"], e["direction"])
        if kind == "POINT":
            ok = ok and f.get("shaped") == 0
        if kind == "SUN":
            ok = ok and close(f["dir"], e["direction"]) and f.get("angle", 0) > 0
        if kind == "AREA":
            ok = ok and close(f["pos"], e["position"]) and close(f["size"], e["size"]) and \
                close(f["dir"], e["direction"])
        ok = ok and all(r > 0 for r in f.get("radiance", [0]))
        checks["%s -> %s as in the scene" % (name, want)] = ok
    hemi = [e for e in expected["lamps"] if e["type"] == 'HEMI']
    want_dome = list(expected["world_horizon"])
    # P8.9: hemi lamps light at the sky's scale (the world is not scaled)
    for h in hemi:
        want_dome = [w + c * h["energy"] for w, c in zip(want_dome, h["color"])]
    checks["world + hemi -> one dome light"] = len(dome) == 1 and dome[0].get("texture") == 1 and \
        close(dome[0]["radiance"], want_dome)
    detail["dome"] = dome
    return checks, detail


def check_materials(lines, expected):
    mats = created(lines, "CreateMaterial")
    checks = {}
    detail = {}
    for e in expected["materials"]:
        f = mats.get(fnv1a("material:MA" + e["name"])) or mats.get(fnv1a("material:" + e["name"]))
        detail[e["name"]] = f
        if f is None:
            checks[e["name"] + " arrives"] = False
            continue
        ok = close(f["albedo"], srgb(e["albedo"])) and close(f["opacity"], e["opacity"]) and f.get("blend") == e["blend"]
        if e["emit"] > 0:
            ok = ok and close(f["intensity"], e["emit"]) and close(f["emissive"], srgb(e["color"]))
        else:
            ok = ok and f.get("intensity") == 0
        checks["%s: colour, alpha, emit" % e["name"]] = ok
    # the see-through box is drawn as blended, nothing else is
    presents = [parse_fields(l) for l in lines if l.startswith("Present")]
    want = sum(1 for e in expected["materials"] if e["blend"])
    checks["%d see-through object(s) drawn blended in every frame" % want] = bool(presents) and all(
        p.get("blended") == want for p in presents)
    return checks, detail


def check_instances(lines, expected):
    meshes = [l for l in lines if l.startswith("CreateMesh ")]
    first_present = next((i for i, l in enumerate(lines) if l.startswith("Present")), len(lines))
    late = [l for l in lines[first_present:] if l.startswith("CreateMesh ")]
    presents = [parse_fields(l) for l in lines if l.startswith("Present")]
    hashes = {int(parse_fields(l)["hash"], 16) for l in meshes}
    want = {fnv1a("mesh:" + n) for n in expected["meshes"]}
    n = expected["instances"]
    checks = {
        "two meshes in total (the cube and the floor)": len(meshes) == 2 and hashes == want,
        "%d draws in every frame" % (n + 1): bool(presents) and all(p.get("draws") == n + 1 for p in presents),
        "no meshes created after the first frame": not late,
        "triangles clockwise, as Remix wants": bool(meshes) and all(parse_fields(l).get("winding") == "cw" for l in meshes),
    }
    return checks, {"meshes": meshes, "frames": len(presents)}


def image_pixels(path):
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None
    if not path or not os.path.isfile(path):
        return None
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int32)


def check_picture(shot):
    a = image_pixels(shot)
    if a is None:
        return {"screenshot": False}, {}
    h, w, _ = a.shape
    center = a[h // 2, w // 2].tolist()
    fill = (abs(a - FILL).max(axis=2) <= 3)
    # BGE's framerate text: top left, drawn over the picture
    corner = fill[: h // 6, : w // 3]
    checks = {
        "the picture fills the window": bool(fill.mean() > 0.9) and all(abs(c - f) <= 3 for c, f in zip(center, FILL)),
        "BGE's text drawn on top (top left)": bool(corner.mean() < 0.995),
    }
    return checks, {"center": center, "fill_share": float(fill.mean()), "corner_fill_share": float(corner.mean())}


def test(checks, run_ok=True):
    return {"ok": bool(checks) and all(checks.values()) and run_ok, "checks": checks}


def run_ci(args, out):
    scenes, expected = make_scenes(args.ours, out)
    room = os.path.join(scenes, "p2_room.blend")
    inst = os.path.join(scenes, "p2_instances.blend")
    mock = os.path.abspath(args.mock)
    res = {}

    def mock_env(name, mode="ok", runtime=mock):
        log = os.path.join(out, name + "_mock.log")
        if os.path.isfile(log):
            os.remove(log)
        return log, {"BGE_REMIX_RUNTIME_DIR": runtime, "BGE_REMIX_MOCK_MODE": mode, "BGE_REMIX_MOCK_LOG": log}

    def ran(r, frames=300):
        return r["exit"] == 0 and r["frames"] >= frames and not r["traceback_in_output"]

    # The room with the fake runtime, BGE's framerate display on
    log, env = mock_env("room")
    r = player(args.ours, out, "room", room, env, args=["-g", "show_framerate", "=", "1"])
    lines = read_lines(log)
    seq = check_sequence(lines, r["stats"])
    seq["the player ran 300 frames"] = ran(r)
    seq["Remix rendered the room (copy mode)"] = r["stats"].get("running") is True and r["stats"].get("mode") == "copy"
    res["room"] = {"ok": all(seq.values()), "checks": seq, "run": r}
    c, d = check_camera(lines, r["probe"])
    res["T2.5"] = dict(test(c, ran(r)), detail=d)
    c, d = check_lamps(lines, expected)
    res["T2.6"] = dict(test(c, ran(r)), detail=d)
    c, d = check_materials(lines, expected)
    res["T2.7"] = dict(test(c, ran(r)), detail=d)
    c, d = check_picture(r["screenshot"])
    res["T2.3a"] = dict(test(c, ran(r)), detail=d)

    # The exposure setting: Remix's local tone mapper (its default) and the
    # global one both get it (0.75 is Remix's default local exposure)
    log, env = mock_env("exposure")
    r = player(args.ours, out, "exposure", os.path.join(scenes, "p2_room_exposure_2.blend"), env)
    config = [l for l in read_lines(log) if "Config " in l]
    room_config = [l for l in lines if "Config " in l]
    c = {
        "the player ran 300 frames": ran(r),
        "default exposure +1: local tone mapper 0.75 x 2 = 1.5":
            any("rtx.localtonemap.exposure=1.5" in l for l in room_config),
        "default exposure +1: global tone mapper bias 1": any("rtx.tonemap.exposureBias=1" in l for l in room_config),
        "exposure +2: local tone mapper 0.75 x 4 = 3": any("rtx.localtonemap.exposure=3" in l for l in config),
        "exposure +2: global tone mapper bias 2": any("rtx.tonemap.exposureBias=2" in l for l in config),
    }
    res["T2.1e"] = dict(test(c), detail={"config": config})

    # 1,000 instances
    log, env = mock_env("instances")
    r = player(args.ours, out, "instances", inst, env)
    c, d = check_instances(read_lines(log), expected["instances"])
    c["the player ran 300 frames"] = ran(r)
    res["T2.2"] = dict(test(c), detail=d, run=r)

    # Remix off, Remix missing, Remix failing
    log_off, env = mock_env("off")
    off = player(args.ours, out, "off", room, env, args=["-g", "remix", "=", "0"])
    _, env = mock_env("missing", runtime=os.path.join(out, "no-runtime-here"))
    missing = player(args.ours, out, "missing", room, env)
    _, env = mock_env("fail", mode="startup_fail")
    fail = player(args.ours, out, "fail", room, env)
    notice_missing = missing["stats"].get("notice", "")
    notice_fail = fail["stats"].get("notice", "")
    c = {
        "off: 300 frames in OpenGL": ran(off) and off["stats"].get("running") is False,
        "off: Remix not touched": not read_lines(log_off),
        "missing: 300 frames in OpenGL": ran(missing) and missing["stats"].get("running") is False,
        "missing: the notice says it is not installed": "not installed" in notice_missing.lower() or
        "no rtx remix runtime" in notice_missing.lower(),
        "failing: 300 frames in OpenGL": ran(fail) and fail["stats"].get("running") is False,
        "failing: the notice names the driver": "driver" in notice_fail.lower(),
    }
    res["T2.4a"] = dict(test(c), detail={"off": off, "missing": missing, "fail": fail})

    # Runtime folder: C (game engine) and Python (Preferences) agree
    res["T2.0"] = check_loader(args.ours, out)

    keys = ("room", "T2.1e", "T2.2", "T2.3a", "T2.4a", "T2.5", "T2.6", "T2.7", "T2.0")
    names = {"room": "Room through Remix (call sequence)", "T2.1e": "Exposure setting reaches Remix",
             "T2.2": "1,000 instances",
             "T2.3a": "Picture in the window, text on top", "T2.4a": "Off, missing, failing: OpenGL",
             "T2.5": "Camera matrices", "T2.6": "Lamps", "T2.7": "Materials",
             "T2.0": "Runtime folder (C = Python), versions"}
    res["all_ok"] = all(res[k]["ok"] for k in keys)
    lines = ["BGE-Remix phase 2 results (CI, fake runtime)", "=" * 44, ""]
    for k in keys:
        lines.append("%-6s %-40s %s" % (k, names[k], mark(res[k]["ok"])))
        for name, ok in res[k]["checks"].items():
            if not ok:
                lines.append("         failed: " + name)
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p2.json and the .log files)"]
    return res, lines


LOADER_PROBE = r'''
import json, os, sys
import bge_remix, _bge_remix
out = sys.argv[sys.argv.index("--") + 1]
res = {}
weird = os.path.join(os.path.dirname(out), "Ayhan Köse çığ", "remix runtime")
os.environ["BGE_REMIX_RUNTIME_DIR"] = weird
res["env"] = [bge_remix.runtime_dir(), _bge_remix.runtime_dir()]
del os.environ["BGE_REMIX_RUNTIME_DIR"]
old = bge_remix._read_settings().get("runtime_dir")
bge_remix.set_runtime_dir(weird)
res["settings"] = [bge_remix.runtime_dir(), _bge_remix.runtime_dir()]
bge_remix.set_runtime_dir(old)
res["default"] = [bge_remix.runtime_dir(), _bge_remix.runtime_dir()]
from bge_remix import pin
res["pin"] = pin.VERSION
with open(out, "w", encoding="utf-8") as f:
    json.dump(res, f, indent=2)
'''


def check_loader(ours, out):
    script = os.path.join(out, "p2_loader_probe.py")
    with open(script, "w", encoding="utf-8") as f:
        f.write(LOADER_PROBE)
    res_path = os.path.join(out, "T2.0.json")
    env = base_env(out, "loader")
    env["BGE_REMIX_RUNTIME_DIR"] = None
    code, text, _ = run([exe(ours, "blender"), "--background", "--factory-startup", "--python", script,
                         "--", res_path], env, os.path.join(out, "T2.0.log"))
    r = read_json(res_path)
    header = open(os.path.join(REPO, "source", "blender", "remix", "BRMX_loader.h"), encoding="utf-8").read()
    m = re.search(r'#define BRMX_RUNTIME_VERSION "([^"]+)"', header)
    norm = lambda p: os.path.normcase(os.path.normpath(p)) if p else p
    checks = {"probe ran": code == 0 and "Traceback" not in text and bool(r)}
    for k in ("env", "settings", "default"):
        pair = r.get(k) or [None, None]
        checks["same folder (%s)" % k] = bool(pair[0]) and norm(pair[0]) == norm(pair[1])
    checks["runtime version in C = pin.py"] = bool(m) and m.group(1) == r.get("pin")
    return dict(test(checks), detail=r)


# ---------------------------------------------------------------------------
# RTX (your PC)

EMBEDDED_PROBE = r'''
# Runs inside Blender's UI: starts the room in the 3D view three times
# (OpenGL, RTX Remix, OpenGL) and quits.
import json, os, sys
import bpy
out_dir = os.environ["BGE_REMIX_P2_EMBEDDED"]
runs = [("opengl_1", 'OPENGL'), ("remix", 'RTX_REMIX'), ("opengl_2", 'OPENGL')]
state = {"i": 0, "ticks": 0, "results": []}

def view3d():
    win = bpy.context.window_manager.windows[0]
    for area in win.screen.areas:
        if area.type == 'VIEW_3D':
            region = [r for r in area.regions if r.type == 'WINDOW'][0]
            return {"window": win, "screen": win.screen, "area": area, "region": region,
                    "scene": bpy.context.scene}
    return None

class P2Embedded(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p2_embedded"
    bl_label = "BGE-Remix P2 embedded"
    _timer = None

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        state["ticks"] += 1
        if state["ticks"] < 10:
            return {'PASS_THROUGH'}
        if state["i"] < len(runs):
            name, mode = runs[state["i"]]
            state["i"] += 1
            context.scene.bge_remix = mode
            os.environ["BGE_REMIX_P2_OUT"] = os.path.join(out_dir, "embedded_%s.json" % name)
            # Remix's first start in blender.exe prepares its shaders (black meanwhile)
            os.environ["BGE_REMIX_P2_SECONDS"] = "40" if mode == 'RTX_REMIX' else "6"
            os.environ["BGE_REMIX_STATS"] = os.path.join(out_dir, "embedded_%s_stats.json" % name)
            os.environ["BGE_REMIX_P2_SHOT"] = os.path.join(out_dir, "embedded_%s.png" % name)
            o = view3d()
            print("P2 probe: starting the %s game" % name, flush=True)
            try:
                bpy.ops.view3d.game_start(o)
                state["results"].append([name, "ok"])
            except Exception as ex:
                state["results"].append([name, repr(ex)])
            print("P2 probe: the %s game returned to Blender" % name, flush=True)
            return {'PASS_THROUGH'}
        context.window_manager.event_timer_remove(self._timer)
        with open(os.path.join(out_dir, "embedded.json"), "w") as f:
            json.dump(state["results"], f, indent=2)
        context.user_preferences.view.use_quit_dialog = False
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}

bpy.utils.register_class(P2Embedded)
bpy.ops.wm.bge_remix_p2_embedded('INVOKE_DEFAULT')
'''


def has_picture(path):
    a = image_pixels(path)
    if a is None:
        return None, {}
    s = {"mean": float(a.mean()), "std": float(a.std())}
    return s["mean"] > 5 and s["std"] > 3, s


def bmp_to_png(path):
    try:
        from PIL import Image
        if path and os.path.isfile(path):
            png = os.path.splitext(path)[0] + ".png"
            Image.open(path).convert("RGB").save(png)
            return png
    except Exception:
        pass
    return None


def find_captures(since, roots):
    found = []
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            if dirpath[len(root):].count(os.sep) > 6:
                dirnames[:] = []
                continue
            for n in filenames:
                if n.startswith("capture") and ".usd" in n:
                    p = os.path.join(dirpath, n)
                    try:
                        if os.path.getmtime(p) >= since - 2:
                            found.append(p)
                    except OSError:
                        pass
    return found


def collect_logs(out, since, roots):
    dest = os.path.join(out, "logs")
    os.makedirs(dest, exist_ok=True)
    bridge = os.path.join(tempfile.gettempdir(), "bge-remix.log")
    cands = [bridge] if os.path.isfile(bridge) else []
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            if dirpath[len(root):].count(os.sep) > 4:
                dirnames[:] = []
                continue
            cands += [os.path.join(dirpath, n) for n in filenames if n.lower().endswith(".log") and
                      "rtx-remix" in dirpath.lower() or n == "bge-remix.log"]
    copied = []
    for p in cands:
        try:
            if os.path.getmtime(p) >= since - 2:
                shutil.copy2(p, os.path.join(dest, os.path.basename(p)))
                copied.append(p)
        except OSError:
            pass
    return copied


def fps(ms):
    return round(1000.0 / ms, 1) if ms else None


def run_rtx(args, out):
    scenes, expected = make_scenes(args.ours, out)
    room = os.path.join(scenes, "p2_room.blend")
    inst = os.path.join(scenes, "p2_instances.blend")
    since = time.time()
    res = {}
    clean = {"BGE_REMIX_RUNTIME_DIR": None, "BGE_REMIX_MOCK_LOG": None}

    def timed(seconds, extra=None):
        e = dict(clean)
        e.update({"BGE_REMIX_P2_SECONDS": str(seconds), "BGE_REMIX_P2_FRAMES": "60",
                  "BGE_REMIX_P2_SHOT_FRAME": "30"})
        e.update(extra or {})
        return e

    def still(seconds):
        # For the sign-off pictures: Remix compiles any shader it is missing
        # before the first frame instead of showing black meanwhile. A new
        # shader took the whole 12 s spot run (P2 RTX run 5: no ray-traced
        # frame, Remix throttled to 33 ms a frame while compiling).
        return timed(seconds, {"RTX_ENABLE_ASYNC_COMPILATION": "0"})

    print("T2.1: the room in blenderplayer, 1920x1080 (the first start prepares Remix's shaders)...", flush=True)
    t21 = player(args.ours, out, "T2.1_room", room, timed(40), size=(1920, 1080), cwd=out)
    ok, img = has_picture(t21["screenshot"])
    res["T2.1"] = {"ok": t21["exit"] == 0 and t21["stats"].get("running") is True and bool(ok),
                   "run": t21, "image": img,
                   "note": "compare T2.1_room.png with cycles_room.png and sign off"}

    print("T2.1: the room's Cycles render (CPU, about a few minutes)...", flush=True)
    res["T2.1"]["cycles"] = cycles_render(args.ours, out, scenes)
    spot_cycles = cycles_render(args.ours, out, scenes, "p2_spot")

    print("T2.1: the room at higher exposure, and the spot lamp alone (sign-off)...", flush=True)
    sheet = [("Cycles", res["T2.1"]["cycles"]["image"]),
             ("Remix (light scale 0.125, default exposure +1)", t21["screenshot"])]
    sheet_runs = {}
    for ev in EXPOSURES:
        tag = ("%g" % ev).replace(".", "_")
        name = "T2.1_exposure_" + tag
        r = player(args.ours, out, name, os.path.join(scenes, "p2_room_exposure_%s.blend" % tag),
                   still(15), size=(1280, 720), cwd=out)
        sheet.append(("Remix, exposure +%g" % ev, r["screenshot"]))
        sheet_runs[name] = r
    r = player(args.ours, out, "T2.1_spot", os.path.join(scenes, "p2_spot.blend"), still(15), size=(1280, 720),
               cwd=out)
    sheet_runs["T2.1_spot"] = r
    sheet += [("Cycles, spot lamp only", spot_cycles["image"]), ("Remix, spot lamp only", r["screenshot"])]
    res["T2.1"]["compare"] = contact_sheet(sheet, os.path.join(out, "T2.1_compare.png"))
    # every sign-off picture must show something (an all-black one means
    # Remix never ray-traced a frame)
    black = [n for n, rr in sorted(sheet_runs.items()) if not has_picture(rr["screenshot"])[0]]
    res["T2.1"]["black_pictures"] = black
    if black:
        res["T2.1"]["ok"] = False

    print("T2.3: BGE's framerate text over the Remix picture...", flush=True)
    t23 = player(args.ours, out, "T2.3_text", room, timed(10), size=(1280, 720),
                 args=["-g", "show_framerate", "=", "1"], cwd=out)
    ok, img = has_picture(t23["screenshot"])
    res["T2.3"] = {"ok": t23["exit"] == 0 and t23["stats"].get("running") is True and bool(ok), "run": t23,
                   "note": "look at T2.3_text.png: the framerate text must be readable over the picture"}

    print("T2.4 + T2.9: OpenGL, Remix, OpenGL in one Blender session (embedded player)...", flush=True)
    res["T2.4"], res["T2.9"] = embedded(args.ours, out, scenes)

    print("B2.1: frame rates (20 s each)...", flush=True)
    b21 = {}
    for name, size, extra in (("1080p copy", (1920, 1080), []), ("1440p copy", (2560, 1440), []),
                              ("1080p window", (1920, 1080), ["-g", "remix_fast", "=", "1"])):
        tag = "B2.1_" + name.replace(" ", "_")
        env = timed(20, {"BGE_REMIX_SCREENSHOT": os.path.join(out, tag + "_remix.bmp"),
                         "BGE_REMIX_SCREENSHOT_FRAME": "600"})
        r = player(args.ours, out, tag, room, env, size=size, args=extra, cwd=out)
        p, s = r["probe"], r["stats"]
        b21[name] = {"window": p.get("window"), "render": [s.get("width"), s.get("height")], "mode": s.get("mode"),
                     "frame_ms_median": p.get("frame_ms_median"), "frame_ms_p95": p.get("frame_ms_p95"),
                     "fps": fps(p.get("frame_ms_median")), "remix_shot": bmp_to_png(os.path.join(out, tag + "_remix.bmp")),
                     "exit": r["exit"]}
    res["B2.1"] = b21
    res["B2.1"]["gate_60fps_1080p"] = (b21["1080p copy"]["fps"] or 0) >= 60

    print("B2.2: 1,000 instances at 1080p...", flush=True)
    r = player(args.ours, out, "B2.2_instances", inst, timed(20), size=(1920, 1080), cwd=out)
    s = r["stats"]
    res["B2.2"] = {"frame_ms_median": r["probe"].get("frame_ms_median"), "fps": fps(r["probe"].get("frame_ms_median")),
                   "handover_ms_median": s.get("handover_ms_median"), "handover_ms_p95": s.get("handover_ms_p95"),
                   "draws": s.get("draws_last_frame"), "meshes_created": s.get("meshes_created"), "exit": r["exit"]}

    copy = player_stats(out, "B2.1_1080p_copy")
    res["B2.3"] = {"copy_ms_median": copy.get("copy_ms_median"), "upload_ms_median": copy.get("upload_ms_median"),
                   "present_ms_median": copy.get("present_ms_median")}

    # C1: Remix 1.6.1 crashes when it captures geometry sent through its API
    # without textures (GameCapturer::captureMaterial reads the missing albedo
    # texture; RTX 3080, P2 run 1). Off until P3 gives materials textures;
    # BGE_REMIX_P2_C1=1 runs it anyway.
    res["C1"] = {"captures": [], "mesh_files": 0, "skipped": os.environ.get("BGE_REMIX_P2_C1") != "1"}
    if not res["C1"]["skipped"]:
        res["C1"] = run_c1(args, out, room, timed)

    res["logs"] = collect_logs(out, since, [out, args.ours])
    res["all_ok"] = all(res[k]["ok"] for k in ("T2.1", "T2.3", "T2.4", "T2.9"))

    lines = ["BGE-Remix phase 2 results (RTX)", "=" * 31, ""]
    lines.append("T2.1 Lit room in blenderplayer        %s   (sign-off: T2.1_compare.png)" %
                 mark(res["T2.1"]["ok"]))
    if res["T2.1"].get("black_pictures"):
        lines.append("     black picture(s): " + ", ".join(res["T2.1"]["black_pictures"]))
    lines.append("T2.3 Framerate text on top            %s   (look at T2.3_text.png)" % mark(res["T2.3"]["ok"]))
    lines.append("T2.4 OpenGL / Remix / OpenGL          %s" % mark(res["T2.4"]["ok"]))
    lines.append("T2.9 Embedded player through Remix    %s" % mark(res["T2.9"]["ok"]))
    c1 = res["C1"]
    if c1.get("skipped"):
        lines.append("C1   Capture                          skipped (crashes Remix 1.6.1 without textures; P3)")
    else:
        lines.append("C1   Capture                          %s   %d stage file(s), %d mesh file(s)" % (
            "yes " if c1["captures"] and c1["mesh_files"] else "no  ", len(c1["captures"]), c1["mesh_files"]))
    lines += ["", "Benchmarks", "-" * 10]
    for name, b in b21.items():
        if not isinstance(b, dict):
            continue
        lines.append("B2.1 %-13s window %s, Remix %s (%s): median %s ms = %s FPS, 95th pct %s ms" % (
            name, b["window"], b["render"], b["mode"], _r(b["frame_ms_median"]), b["fps"], _r(b["frame_ms_p95"])))
    lines.append("     gate (60 FPS at 1080p, copy mode): %s" % ("met" if res["B2.1"]["gate_60fps_1080p"] else "NOT met"))
    b22 = res["B2.2"]
    lines.append("B2.2 1,000 instances: median %s ms = %s FPS; hand-over median %s ms, 95th pct %s ms" % (
        _r(b22["frame_ms_median"]), b22["fps"], _r(b22["handover_ms_median"]), _r(b22["handover_ms_p95"])))
    b23 = res["B2.3"]
    lines.append("B2.3 copy mode: copy %s ms + upload %s ms per frame (Present %s ms)" % (
        _r(b23["copy_ms_median"]), _r(b23["upload_ms_median"]), _r(b23["present_ms_median"])))
    res["warmup"] = warmups(out)
    slow = [(n, w) for n, w in sorted(res["warmup"].items()) if w is None or w > 1.0]
    lines += ["", "Remix warm-up (start to first ray-traced frame)", "-" * 47]
    if slow:
        for n, w in slow:
            lines.append("%-24s %s" % (n, "never ray-traced" if w is None else "%.1f s" % w))
    else:
        lines.append("every run under 1 s")
    if res["T2.4"].get("crash_files"):
        lines.append("")
        lines.append("Blender crash report(s): " + ", ".join(res["T2.4"]["crash_files"]))
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p2_rtx.json and the .log files)"]
    return res, lines


def _clock(stamp):
    """'[20:34:31.529]' -> seconds since midnight"""
    m = re.match(r"\[(\d+):(\d+):(\d+(?:\.\d+)?)\]", stamp)
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3)) if m else None


def warmups(out):
    """Seconds from Remix's start to its first ray-traced frame, per run log
    (None: it never ray-traced). Remix shows black until then."""
    found = {}
    for n in sorted(os.listdir(out)):
        if not n.endswith(".log") or n.startswith(("cycles", "make_", "C1")):
            continue
        started = traced = None
        last = None
        for line in read_lines(os.path.join(out, n)):
            if line.startswith("["):
                last = _clock(line.split(" ", 1)[0])
            if "BGE-Remix: RTX Remix renders scene" in line:
                started = last
                traced = None
            elif "Opacity Micromap: enabled" in line and started is not None and traced is None:
                traced = last
                found[n[:-4] + ("" if n != "embedded.log" else " (Remix run)")] = max(traced - started, 0.0)
        if started is not None and traced is None:
            found[n[:-4]] = None
    return found


def run_c1(args, out, room, timed):
    print("C1: capture (replacement assets off from the start)...", flush=True)
    since = time.time()
    r = player(args.ours, out, "C1_capture", room,
               timed(30, {"DXVK_RTX_CAPTURE_ENABLE_ON_FRAME": "900", "BGE_REMIX_NO_REPLACEMENTS": "1"}),
               size=(1280, 720), cwd=out)
    time.sleep(3)
    caps = find_captures(since, [out, args.ours])
    meshes = 0
    for c in caps:
        mdir = os.path.join(os.path.dirname(c), "meshes")
        if os.path.isdir(mdir):
            meshes += len(os.listdir(mdir))
    return {"captures": caps, "mesh_files": meshes, "exit": r["exit"], "skipped": False}


EXPOSURES = (1, 2, 3)  # as in make_scenes_p2.py


def contact_sheet(items, target):
    """The images side by side, two per row, each labelled; for the sign-off."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    tiles = []
    for label, path in items:
        if path and os.path.isfile(path):
            img = Image.open(path).convert("RGB").resize((960, 540))
        else:
            img = Image.new("RGB", (960, 540), (40, 0, 0))
            label += " (missing)"
        d = ImageDraw.Draw(img)
        d.rectangle([0, 0, 960, 34], fill=(0, 0, 0))
        d.text((10, 10), label, fill=(255, 255, 255))
        tiles.append(img)
    rows = (len(tiles) + 1) // 2
    sheet = Image.new("RGB", (1920, 540 * rows), (0, 0, 0))
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % 2) * 960, (i // 2) * 540))
    try:
        sheet.save(target)
    except OSError as ex:
        # Windows refuses to overwrite a picture that is open in a viewer
        # (Errno 22, P2 RTX run 6): keep the run going under another name
        base, ext = os.path.splitext(target)
        target = "%s_%s%s" % (base, time.strftime("%H%M%S"), ext)
        print("note: could not overwrite the old sheet (%s; open in a viewer?), wrote %s" % (ex, target), flush=True)
        sheet.save(target)
    return target


def player_stats(out, name):
    return read_json(os.path.join(out, name + "_stats.json"))


CYCLES_SETUP = r'''
# Turns the P2 room into a Cycles scene with the same light, as BGE-Remix
# converts it for Remix, and renders it.
import math, os, sys
import bpy
out = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 96
scene.render.resolution_x, scene.render.resolution_y = 1280, 720
scene.render.resolution_percentage = 100
# a film-like curve, as Remix's tone mapping, so bright areas don't burn out
try:
    scene.view_settings.view_transform = 'Filmic'
    scene.view_settings.look = 'Filmic - Base Contrast'
except Exception:
    pass
try:
    scene.render.layers[0].cycles.use_denoising = True
except Exception:
    pass
hemi = [0.0, 0.0, 0.0]
for ob in scene.objects:
    if ob.type != 'LAMP':
        continue
    la = ob.data
    E, D = la.energy, getattr(la, "distance", 10.0)
    if la.type == 'HEMI':
        hemi = [h + c * E for h, c in zip(hemi, la.color)]
        la.energy = 0.0
        continue
    la.use_nodes = True
    em = la.node_tree.nodes.get("Emission")
    if la.type in ('POINT', 'SPOT'):
        strength = 4.0 * math.pi * math.pi * E * D     # radiant intensity pi * E * D
    elif la.type == 'SUN':
        strength = math.pi * E                           # irradiance pi * E
    else:
        strength = 4.0 * math.pi * E * D                 # area: pi * E * D over its area
    em.inputs["Strength"].default_value = strength
    em.inputs["Color"].default_value = list(la.color) + [1.0]
w = scene.world
w.use_nodes = True
bg = w.node_tree.nodes.get("Background")
hc = w.horizon_color
bg.inputs["Color"].default_value = [hc[0] + hemi[0], hc[1] + hemi[1], hc[2] + hemi[2], 1.0]
bg.inputs["Strength"].default_value = 1.0
for ma in bpy.data.materials:
    ma.use_nodes = True
    nt = ma.node_tree
    nt.nodes.clear()
    outn = nt.nodes.new("ShaderNodeOutputMaterial")
    diff = nt.nodes.new("ShaderNodeBsdfDiffuse")
    diff.inputs["Color"].default_value = [c * ma.diffuse_intensity for c in ma.diffuse_color] + [1.0]
    shader = diff.outputs[0]
    if ma.emit > 0:
        em = nt.nodes.new("ShaderNodeEmission")
        em.inputs["Color"].default_value = list(ma.diffuse_color) + [1.0]
        em.inputs["Strength"].default_value = ma.emit
        add = nt.nodes.new("ShaderNodeAddShader")
        nt.links.new(shader, add.inputs[0])
        nt.links.new(em.outputs[0], add.inputs[1])
        shader = add.outputs[0]
    if ma.game_settings.alpha_blend != 'OPAQUE' and ma.alpha < 1.0:
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        mix = nt.nodes.new("ShaderNodeMixShader")
        mix.inputs[0].default_value = ma.alpha
        nt.links.new(tr.outputs[0], mix.inputs[1])
        nt.links.new(shader, mix.inputs[2])
        shader = mix.outputs[0]
    nt.links.new(shader, outn.inputs["Surface"])
scene.render.filepath = out
scene.render.image_settings.file_format = 'PNG'
bpy.ops.render.render(write_still=True)
'''


def cycles_render(ours, out, scenes, scene="p2_room"):
    script = os.path.join(out, "p2_cycles_setup.py")
    with open(script, "w", encoding="utf-8") as f:
        f.write(CYCLES_SETUP)
    name = scene.replace("p2_", "cycles_")
    target = os.path.join(out, name + ".png")
    code, text, secs = run([exe(ours, "blender"), "--background", os.path.join(scenes, scene + ".blend"),
                            "--python", script, "--", target], base_env(out, "cycles"),
                           os.path.join(out, name + ".log"), timeout=1800)
    return {"ok": code == 0 and os.path.isfile(target), "image": target, "seconds": round(secs, 1)}


def embedded(ours, out, scenes):
    script = os.path.join(out, "p2_embedded_probe.py")
    with open(script, "w", encoding="utf-8") as f:
        f.write(EMBEDDED_PROBE)
    for n in os.listdir(out):
        if n.startswith("embedded_"):
            os.remove(os.path.join(out, n))
    env = base_env(out, "embedded")
    # Blender writes its crash report (p2_room.crash.txt) into TEMP; P2 RTX
    # run 5 crashed after the Remix game without one in the results
    tmp = os.path.join(out, "tmp_embedded")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp, exist_ok=True)
    env.update({"BGE_REMIX_P2_EMBEDDED": out, "BGE_REMIX_P2_SECONDS": "8", "BGE_REMIX_P2_FRAMES": "60",
                "BGE_REMIX_P2_SHOT_FRAME": "30", "BGE_REMIX_RUNTIME_DIR": None, "BGE_REMIX_TRACE": "1",
                "TEMP": tmp, "TMP": tmp})
    code, text, secs = run([exe(ours, "blender"), os.path.join(scenes, "p2_room.blend"),
                            "--python", script], env, os.path.join(out, "embedded.log"), timeout=900, cwd=out)
    crash_files = []
    for n in os.listdir(tmp):
        if n.endswith(".crash.txt"):
            shutil.copy2(os.path.join(tmp, n), os.path.join(out, "embedded_" + n))
            crash_files.append("embedded_" + n)
        elif n == "bge-remix.log":
            shutil.copy2(os.path.join(tmp, n), os.path.join(out, "embedded_bge-remix.log"))
    runs = {}
    for name in ("opengl_1", "remix", "opengl_2"):
        runs[name] = {"probe": read_json(os.path.join(out, "embedded_%s.json" % name)),
                      "stats": read_json(os.path.join(out, "embedded_%s_stats.json" % name))}
        shot = os.path.join(out, "embedded_%s.png" % name)
        runs[name]["picture"] = has_picture(shot)[0]
    frames = lambda n: runs[n]["probe"].get("frames", 0) >= 60
    t24 = {
        "Blender exited cleanly": code == 0 and "Traceback" not in text,
        "OpenGL, then Remix, then OpenGL ran": all(frames(n) for n in runs),
        "the first and last runs in OpenGL": runs["opengl_1"]["stats"].get("running") is False and
        runs["opengl_2"]["stats"].get("running") is False,
    }
    t29 = {
        "the middle run through Remix (copy mode)": runs["remix"]["stats"].get("running") is True and
        runs["remix"]["stats"].get("mode") == "copy",
        "a picture in Blender's 3D view": bool(runs["remix"]["picture"]),
    }
    return dict(test(t24), runs=runs, exit=code, crash_files=crash_files), dict(test(t29))


def mark(ok):
    return "PASS" if ok else ("----" if ok is None else "FAIL")


def _r(v):
    return "-" if v is None else "%.2f" % v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--mock")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rtx", action="store_true")
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    if args.rtx:
        res, lines = run_rtx(args, out)
        name = "results_p2_rtx"
    else:
        if not args.mock:
            raise SystemExit("--mock is needed without --rtx")
        res, lines = run_ci(args, out)
        name = "results_p2"
    with open(os.path.join(out, name + ".json"), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, name.replace("results", "summary") + ".txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
