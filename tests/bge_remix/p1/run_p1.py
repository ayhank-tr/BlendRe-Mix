#!/usr/bin/env python3
"""BGE-Remix phase 1 tests and benchmarks (host side, Python 3.8+).

CI, with the fake runtime (no GPU needed):
  python run_p1.py --ours <folder with blender.exe> --mock <folder with mock d3d9.dll>
                   --mock-shim <folder with mock d3d9.dll, d3d9_x64.dll, remixdep.dll> --out <folder>

  T1.2 runtime missing (background check + the Preferences box in the UI)
  T1.3 wrong API version
  T1.3b the runtime throws while loading (no crash)
  T1.3c the real runtime's DLL layout (shim + implementation + dependency)
  T1.4 startup sequence (checked against the fake runtime's call log)
  T1.5 startup failure
  T1.6 installer

Your PC, with the real runtime (RTX GPU):
  python run_p1.py --ours <folder with blender.exe> --out <folder> --rtx

  T1.8 real install (557 MB download, skipped when already installed)
  T1.1 path-traced triangle, C1 capture, B1.2 Remix startup, B1.3 frame time and VRAM

T1.7 (the P0 tests) and B1.1 (Blender startup) come from run_p0.py.
"""

import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
P0 = os.path.join(os.path.dirname(HERE), "p0")
TIMEOUT = 300
EXPECTED_CONFIG = {
    "rtx.zUp=True",
    "rtx.leftHandedCoordinateSystem=False",
    "rtx.sceneScale=0.01",
    "rtx.captureShowMenuOnHotkey=False",
}


def exe(folder, name):
    path = os.path.join(folder, name + ".exe")
    if not os.path.isfile(path):
        raise SystemExit("not found: " + path)
    return path


def run(cmd, env, log, timeout=TIMEOUT):
    full = dict(os.environ)
    full.update(env)
    start = time.time()
    try:
        p = subprocess.run(cmd, env=full, timeout=timeout, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
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


def probe(ours, out, case, env_extra, extra_args=()):
    res = os.path.join(out, "%s.json" % case)
    if os.path.isfile(res):
        os.remove(res)
    env = base_env(out, case)
    env.update(env_extra)
    cmd = [exe(ours, "blender"), "--background", "--factory-startup",
           "--python", os.path.join(HERE, "p1_probe.py"), "--", case, res] + list(extra_args)
    code, text, secs = run(cmd, env, os.path.join(out, "%s.log" % case))
    data = {}
    if os.path.isfile(res):
        with open(res, encoding="utf-8") as f:
            data = json.load(f)
    data["exit"] = code
    data["seconds_total"] = round(secs, 1)
    data["traceback_in_output"] = "Traceback" in text
    data["ok"] = bool(data.get("ok")) and code == 0 and not data["traceback_in_output"]
    return data


def mock_env(mock, mode, log):
    if os.path.isfile(log):
        os.remove(log)
    return {"BGE_REMIX_RUNTIME_DIR": mock, "BGE_REMIX_MOCK_MODE": mode, "BGE_REMIX_MOCK_LOG": log}


def read_lines(path):
    if not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8", errors="replace") as f:
        return [l.strip() for l in f if l.strip()]


def t12_ui(ours, out, scenes):
    res = os.path.join(out, "T1.2_ui.json")
    env = base_env(out, "ui")
    env["BGE_REMIX_RUNTIME_DIR"] = os.path.join(out, "no-runtime-here")
    env["BGE_REMIX_P1_UI_OUT"] = res
    cmd = [exe(ours, "blender"), os.path.join(scenes, "t01_ui.blend"),
           "--python", os.path.join(HERE, "p1_ui_probe.py")]
    code, text, secs = run(cmd, env, os.path.join(out, "T1.2_ui.log"))
    data = {}
    if os.path.isfile(res):
        with open(res) as f:
            data = json.load(f)
    data["exit"] = code
    data["traceback_in_output"] = "Traceback" in text
    data["ok"] = (code == 0 and data.get("opened") and not data.get("errors")
                  and not data["traceback_in_output"])
    return data


def t14_check(seq, log_lines):
    starts = [l for l in log_lines if l.startswith("Startup")]
    config = {l[len("Config "):] for l in log_lines if l.startswith("Config ")}
    # "Present" (P1 mock) or "Present draws=N lights=M" (P2 mock)
    presents = sum(1 for l in log_lines if l == "Present" or l.startswith("Present "))
    shutdowns = [i for i, l in enumerate(log_lines) if l == "Shutdown"]
    after = [l for l in log_lines if l.startswith("WndProc after Shutdown")]
    frames = (seq.get("triangle") or {}).get("frames", -1)
    checks = {
        "one Startup with a real window": starts == ["Startup hwnd_valid=1"],
        "exactly the four config values": config == EXPECTED_CONFIG,
        "one Present per frame": presents == frames and frames > 0,
        "one Shutdown, at Blender's exit": len(shutdowns) == 1 and shutdowns[0] == len(log_lines) - 1,
        "no window message reaches Remix after Shutdown": not after,
    }
    return {"checks": checks, "presents": presents, "frames": frames, "config": sorted(config),
            "ok": all(checks.values())}


def image_stats(bmp):
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return {"ok": None, "why": "Pillow/numpy missing"}
    if not os.path.isfile(bmp):
        return {"ok": False, "why": "no screenshot"}
    img = Image.open(bmp).convert("RGB")
    png = os.path.splitext(bmp)[0] + ".png"
    img.save(png)
    a = np.asarray(img, dtype=np.float32)
    stats = {"png": png, "mean": float(a.mean()), "std": float(a.std())}
    stats["ok"] = stats["mean"] > 5 and stats["std"] > 3
    return stats


def mark(ok):
    return "PASS" if ok else ("----" if ok is None else "FAIL")


def run_ci(args, out):
    scenes = os.path.join(out, "scenes")
    os.makedirs(scenes, exist_ok=True)
    code, _, _ = run([exe(args.ours, "blender"), "--background", "--factory-startup",
                      "--python", os.path.join(P0, "make_scenes.py"), "--", scenes],
                     {}, os.path.join(out, "make_scenes.log"))
    if code != 0:
        raise SystemExit("making the test scenes failed, see make_scenes.log")

    mock = os.path.abspath(args.mock)
    res = {}
    missing = {"BGE_REMIX_RUNTIME_DIR": os.path.join(out, "no-runtime-here")}
    t12 = probe(args.ours, out, "missing", missing)
    t12_ui_res = t12_ui(args.ours, out, scenes)
    res["T1.2"] = {"ok": t12["ok"] and t12_ui_res["ok"], "background": t12, "ui": t12_ui_res}

    log = os.path.join(out, "mock_version.log")
    res["T1.3"] = probe(args.ours, out, "version", mock_env(mock, "version", log))

    log = os.path.join(out, "mock_sequence.log")
    seq = probe(args.ours, out, "sequence", mock_env(mock, "ok", log))
    calls = t14_check(seq, read_lines(log))
    res["T1.4"] = {"ok": seq["ok"] and calls["ok"], "probe": seq, "calls": calls}

    log = os.path.join(out, "mock_startup_fail.log")
    res["T1.5"] = probe(args.ours, out, "startup_fail", mock_env(mock, "startup_fail", log))
    res["T1.5"]["messages_after_failed_start"] = [l for l in read_lines(log) if l.startswith("WndProc after")]
    res["T1.5"]["ok"] = res["T1.5"]["ok"] and not res["T1.5"]["messages_after_failed_start"]

    log = os.path.join(out, "mock_throw.log")
    res["T1.3b"] = probe(args.ours, out, "throw", mock_env(mock, "throw", log))

    log = os.path.join(out, "mock_shim_layout.log")
    res["T1.3c"] = probe(args.ours, out, "shim_layout", mock_env(os.path.abspath(args.mock_shim), "ok", log))
    res["T1.3c"]["messages_after_shutdown"] = [l for l in read_lines(log) if l.startswith("WndProc after")]
    res["T1.3c"]["ok"] = res["T1.3c"]["ok"] and not res["T1.3c"]["messages_after_shutdown"]

    res["T1.6"] = probe(args.ours, out, "installer", {}, [os.path.join(mock, "d3d9.dll")])

    keys = ("T1.2", "T1.3", "T1.3b", "T1.3c", "T1.4", "T1.5", "T1.6")
    res["all_ok"] = all(res[k]["ok"] for k in keys)
    lines = ["BGE-Remix phase 1 results (CI, fake runtime)", "=" * 44, ""]
    names = {"T1.2": "Runtime missing (incl. Preferences box)", "T1.3": "Wrong API version",
             "T1.3b": "Runtime error while loading (no crash)",
             "T1.3c": "Real runtime DLL layout",
             "T1.4": "Startup sequence", "T1.5": "Startup failure", "T1.6": "Installer"}
    for k in keys:
        lines.append("%-5s %-42s %s" % (k, names[k], mark(res[k]["ok"])))
    lines.append("")
    lines.append("T1.4 calls: %d Present for %d frames; config %s" % (
        calls["presents"], calls["frames"], ", ".join(calls["config"])))
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p1.json and the .log files)"]
    return res, lines


def collect_logs(out, since, roots):
    """Copy Remix's and the bridge's log files written since `since` into out/logs."""
    import shutil
    import tempfile
    dest = os.path.join(out, "logs")
    os.makedirs(dest, exist_ok=True)
    copied = []
    bridge_log = os.path.join(tempfile.gettempdir(), "bge-remix.log")
    candidates = [bridge_log] if os.path.isfile(bridge_log) else []
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            if dirpath[len(root):].count(os.sep) > 4:
                dirnames[:] = []
                continue
            candidates += [os.path.join(dirpath, n) for n in filenames if n.lower().endswith(".log")]
    for path in candidates:
        try:
            if os.path.getmtime(path) >= since - 2:
                target = os.path.join(dest, os.path.basename(path))
                shutil.copy2(path, target)
                copied.append(path)
        except (OSError, ValueError):
            pass
    return copied


def run_rtx(args, out):
    res = {}
    t18 = probe(args.ours, out, "real_install", {})
    res["T1.8"] = t18
    since = time.time()
    rtx = probe(args.ours, out, "rtx", {})
    runtime = (t18.get("target") or os.path.join(os.environ.get("APPDATA", ""),
               "Blender Foundation", "Blender", "2.79", "remix", "1.6.1"))
    res["logs"] = collect_logs(out, since, [args.ours, runtime, os.getcwd()])
    shot = os.path.join(out, "rtx.bmp")
    img = image_stats(shot)
    t11 = rtx.get("checks", {})
    res["T1.1"] = {"ok": rtx["ok"] and img.get("ok") is not False, "probe": rtx, "image": img}
    res["C1"] = rtx.get("C1")
    res["B1.2"] = rtx.get("B1.2")
    res["B1.3"] = rtx.get("B1.3")
    res["vram"] = {"before_mb": rtx.get("vram_before_mb"), "after_startup_mb": rtx.get("vram_after_startup_mb")}
    res["all_ok"] = res["T1.8"]["ok"] and res["T1.1"]["ok"]

    c1 = res["C1"] or {}
    b12 = res["B1.2"] or {}
    b13 = res["B1.3"] or {}
    tri = rtx.get("T1.1") or {}
    lines = ["BGE-Remix phase 1 results (RTX)", "=" * 31, ""]
    lines.append("T1.8 Real install                 %s   (%s s)" % (mark(t18["ok"]), t18.get("seconds", "-")))
    lines.append("T1.1 Path-traced triangle         %s   %s frames, screenshot mean %.1f" % (
        mark(res["T1.1"]["ok"]), tri.get("frames", 0), img.get("mean", -1)))
    lines.append("C1   Capture                      %s   %d stage file(s), %d mesh file(s)" % (
        "yes " if c1.get("captures") and c1.get("mesh_files") else "no  ",
        len(c1.get("captures") or []), c1.get("mesh_files") or 0))
    lines += ["", "Benchmarks", "-" * 10]
    lines.append("B1.2 Remix load / startup / first frame   %s / %s / %s s (total %s s)" % (
        b12.get("load_s"), b12.get("startup_s"), b12.get("first_frame_s"), b12.get("total_s")))
    lines.append("     first picture after               %s s (long on the very first start: shaders)" % (
        b12.get("first_picture_s"),))
    lines.append("     Blender exit code                  %s (0 = clean shutdown)" % rtx.get("exit"))
    lines.append("B1.3 Triangle at 1080p, %s frames         median %s ms, 95th pct %s ms" % (
        b13.get("frames"), _r(b13.get("frame_ms_median")), _r(b13.get("frame_ms_p95"))))
    lines.append("     VRAM before / after startup / after test   %s / %s / %s MB" % (
        res["vram"]["before_mb"], res["vram"]["after_startup_mb"], b13.get("vram_mb")))
    if not t11:
        lines.append("")
        lines.append("Probe output: rtx.log")
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p1_rtx.json and rtx.log)"]
    return res, lines


def _r(v):
    return "-" if v is None else "%.2f" % v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--mock")
    ap.add_argument("--mock-shim", dest="mock_shim")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rtx", action="store_true")
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    if args.rtx:
        res, lines = run_rtx(args, out)
        name = "results_p1_rtx"
    else:
        if not args.mock or not args.mock_shim:
            raise SystemExit("--mock and --mock-shim are needed without --rtx")
        res, lines = run_ci(args, out)
        name = "results_p1"
    with open(os.path.join(out, name + ".json"), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, name.replace("results", "summary") + ".txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
