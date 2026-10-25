#!/usr/bin/env python3
"""BGE-Remix phase 0 tests and benchmarks.

Runs the same tests on a BGE-Remix build and on official Blender 2.79b, and
checks that BGE-Remix behaves like the official release.

  T0.1  Blender's UI starts (no splash), draws, and quits cleanly
  T0.2  F12 render of the default scene, Blender Render and Cycles (CPU),
        compared pixel by pixel with official 2.79b
  T0.3  The game engine runs the default scene for 300 frames in blenderplayer

Benchmarks: UI startup time (median of 3), render times, BGE frame times.

Usage (host Python 3.8+, Pillow and numpy):
  python run_p0.py --ours <folder with blender.exe> --official <folder> --out <folder>
           [--mesa <folder with Mesa opengl32.dll>]   (CI machines without a GPU)
"""

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TIMEOUT = 300  # seconds per launch
STARTUP_RUNS = 3

# Pixel tolerances against official 2.79b. A different compiler may round
# floating point slightly differently, so exact equality is not required.
LIMITS = {
    "BLENDER_RENDER": {"mean": 0.25, "share_over_2": 0.001},
    "CYCLES": {"mean": 1.0, "share_over_8": 0.01},
}


def exe(folder, name):
    path = os.path.join(folder, name + (".exe" if os.name == "nt" else ""))
    if not os.path.isfile(path):
        raise SystemExit("not found: " + path)
    return path


def run(cmd, env=None, timeout=TIMEOUT, log=None):
    start = time.time()
    full_env = dict(os.environ)
    if env:
        full_env.update(env)
    try:
        proc = subprocess.run(cmd, env=full_env, timeout=timeout,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        code, out = proc.returncode, proc.stdout
    except subprocess.TimeoutExpired as ex:
        code, out = "timeout", ex.stdout or b""
    if log:
        with open(log, "wb") as f:
            f.write(out)
    return code, time.time() - start, start


def clean_env(out, name):
    """Own user folders per build, so the PC's own Blender settings don't leak in."""
    base = os.path.join(out, "user_" + name)
    env = {}
    for key, sub in (("BLENDER_USER_CONFIG", "config"), ("BLENDER_USER_SCRIPTS", "scripts")):
        path = os.path.join(base, sub)
        os.makedirs(path, exist_ok=True)
        env[key] = path
    return env


def add_mesa(folder, mesa):
    for name in ("opengl32.dll", "libgallium_wgl.dll", "libglapi.dll", "dxil.dll"):
        src = os.path.join(mesa, name)
        if os.path.isfile(src):
            shutil.copy2(src, folder)


def t01(build, name, scenes, out):
    runs = []
    for i in range(STARTUP_RUNS):
        res = os.path.join(out, "t01_%s_%d.json" % (name, i))
        env = clean_env(out, name)
        env["BGE_REMIX_T01_OUT"] = res
        cmd = [exe(build, "blender"), os.path.join(scenes, "t01_ui.blend"),
               "--python", os.path.join(HERE, "ui_probe.py")]
        code, secs, launched = run(cmd, env, log=res.replace(".json", ".log"))
        data = {}
        if os.path.isfile(res):
            with open(res) as f:
                data = json.load(f)
        ok = code == 0 and data.get("first_tick") is not None
        runs.append({
            "ok": ok, "exit": code, "seconds_total": round(secs, 2),
            "startup_s": round(data["first_tick"] - launched, 3) if data.get("first_tick") else None,
            "gl": "%s / %s / %s" % (data.get("gl_vendor"), data.get("gl_renderer"), data.get("gl_version")),
            "errors": data.get("errors", []),
        })
    startups = [r["startup_s"] for r in runs if r["startup_s"] is not None]
    return {
        "ok": all(r["ok"] for r in runs),
        "runs": runs,
        "startup_median_s": round(statistics.median(startups), 3) if startups else None,
    }


def t02(build, name, scenes, out):
    result = {}
    for engine in ("BLENDER_RENDER", "CYCLES"):
        prefix = os.path.join(out, "t02_%s_%s_#" % (engine.lower(), name))
        image = prefix.replace("#", "1") + ".png"
        if os.path.isfile(image):
            os.remove(image)
        cmd = [exe(build, "blender"), "--background", os.path.join(scenes, "t02_render.blend"),
               "--engine", engine, "--render-output", prefix, "--render-format", "PNG",
               "--use-extension", "1", "--render-frame", "1"]
        code, secs, _ = run(cmd, clean_env(out, name), log=prefix.replace("#", "log") + ".txt")
        result[engine] = {"ok": code == 0 and os.path.isfile(image), "exit": code,
                          "seconds": round(secs, 2), "image": image}
    return result


def t03(build, name, scenes, out):
    res = os.path.join(out, "t03_%s.json" % name)
    if os.path.isfile(res):
        os.remove(res)
    env = clean_env(out, name)
    env["BGE_REMIX_T03_OUT"] = res
    cmd = [exe(build, "blenderplayer"), "-w", "640", "360", os.path.join(scenes, "t03_bge.blend")]
    code, secs, _ = run(cmd, env, log=res.replace(".json", ".log"))
    data = {}
    if os.path.isfile(res):
        with open(res) as f:
            data = json.load(f)
    data.update({"ok": code == 0 and data.get("frames", 0) >= 300, "exit": code,
                 "seconds_total": round(secs, 2)})
    return data


def compare(image_a, image_b, engine):
    import numpy as np
    from PIL import Image
    a = np.asarray(Image.open(image_a).convert("RGBA"), dtype=np.int16)
    b = np.asarray(Image.open(image_b).convert("RGBA"), dtype=np.int16)
    if a.shape != b.shape:
        return {"ok": False, "why": "sizes differ: %s vs %s" % (a.shape, b.shape)}
    diff = np.abs(a - b).max(axis=2)
    stats = {
        "mean": float(np.abs(a - b).mean()),
        "max": int(diff.max()),
        "share_over_2": float((diff > 2).mean()),
        "share_over_8": float((diff > 8).mean()),
        "not_blank": bool(a[..., :3].std() > 1.0),
    }
    lim = LIMITS[engine]
    stats["ok"] = stats["not_blank"] and all(stats[k] <= v for k, v in lim.items())
    stats["limits"] = lim
    return stats


def summary_text(res):
    lines = ["BGE-Remix phase 0 results", "=" * 25, ""]
    o, f = res["ours"], res["official"]

    def mark(ok):
        return "PASS" if ok else "FAIL"

    lines.append("T0.1 UI starts and quits     %s   (official: %s)" % (mark(o["t01"]["ok"]), mark(f["t01"]["ok"])))
    for eng in ("BLENDER_RENDER", "CYCLES"):
        c = res["compare"].get(eng, {})
        lines.append("T0.2 F12 %-15s     %s   rendered %s, mean diff %.3f, max %s" % (
            eng, mark(c.get("ok", False)), mark(o["t02"][eng]["ok"]), c.get("mean", -1), c.get("max", "-")))
    lines.append("T0.3 BGE 300 frames          %s   (official: %s)" % (mark(o["t03"]["ok"]), mark(f["t03"]["ok"])))
    lines += ["", "Benchmarks (BGE-Remix vs official 2.79b)", "-" * 40]
    lines.append("UI startup, median of %d     %s s vs %s s" % (
        STARTUP_RUNS, o["t01"]["startup_median_s"], f["t01"]["startup_median_s"]))
    for eng in ("BLENDER_RENDER", "CYCLES"):
        lines.append("F12 %-15s          %s s vs %s s" % (eng, o["t02"][eng]["seconds"], f["t02"][eng]["seconds"]))
    lines.append("BGE frame time, median       %s ms vs %s ms" % (
        _r(o["t03"].get("frame_ms_median")), _r(f["t03"].get("frame_ms_median"))))
    lines.append("BGE frame time, 95th pct     %s ms vs %s ms" % (
        _r(o["t03"].get("frame_ms_p95")), _r(f["t03"].get("frame_ms_p95"))))
    if res.get("build_time"):
        lines.append("Build time (CI)              " + res["build_time"])
    lines += ["", "OpenGL: " + (o["t01"]["runs"][0]["gl"] if o["t01"]["runs"] else "?"), "",
              "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results.json and the .log files)"]
    return "\n".join(lines) + "\n"


def _r(v):
    return "-" if v is None else "%.2f" % v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--official", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--mesa")
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    builds = {"ours": os.path.abspath(args.ours), "official": os.path.abspath(args.official)}
    if args.mesa:
        for folder in builds.values():
            add_mesa(folder, args.mesa)

    scenes = os.path.join(out, "scenes")
    os.makedirs(scenes, exist_ok=True)
    code, _, _ = run([exe(builds["official"], "blender"), "--background", "--factory-startup",
                      "--python", os.path.join(HERE, "make_scenes.py"), "--", scenes],
                     log=os.path.join(out, "make_scenes.log"))
    if code != 0:
        raise SystemExit("making the test scenes failed, see make_scenes.log")

    res = {}
    for name, build in builds.items():
        print("running", name, build, flush=True)
        res[name] = {"t01": t01(build, name, scenes, out),
                     "t02": t02(build, name, scenes, out),
                     "t03": t03(build, name, scenes, out)}
    res["compare"] = {}
    build_time = os.path.join(builds["ours"], "BUILD_TIME.txt")
    if os.path.isfile(build_time):
        with open(build_time) as f:
            res["build_time"] = f.read().strip().replace("build_minutes=", "") + " min"
    for eng in ("BLENDER_RENDER", "CYCLES"):
        a, b = res["ours"]["t02"][eng], res["official"]["t02"][eng]
        if a["ok"] and b["ok"]:
            res["compare"][eng] = compare(a["image"], b["image"], eng)
        else:
            res["compare"][eng] = {"ok": False, "why": "render missing"}
    o = res["ours"]
    res["all_ok"] = (o["t01"]["ok"] and o["t03"]["ok"]
                     and all(res["compare"][e]["ok"] for e in res["compare"]))
    with open(os.path.join(out, "results.json"), "w") as f:
        json.dump(res, f, indent=2)
    text = summary_text(res)
    with open(os.path.join(out, "summary.txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
