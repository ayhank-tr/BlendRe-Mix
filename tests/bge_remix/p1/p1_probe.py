# BGE-Remix P1 probe: runs inside Blender 2.79b (Python 3.5, no f-strings).
#
#   blender --background --factory-startup --python p1_probe.py -- <case> <result.json> [args]
#
# Cases (CI, with the fake runtime from mock_remix):
#   missing        T1.2  no runtime in the folder
#   version        T1.3  the runtime reports another API version
#   sequence       T1.4  load, start, draw, and leave shutdown to Blender's exit
#   startup_fail   T1.5  Remix refuses to start; Blender still renders
#   installer      T1.6  install a test package, reject a bad one, remove
# Cases (your PC, real runtime):
#   real_install   T1.8  download and install the pinned 557 MB runtime
#   rtx            T1.1 + C1 + B1.2 + B1.3  path-traced triangle, capture, timings
#
# The folder used as runtime comes from BGE_REMIX_RUNTIME_DIR (set by run_p1.py).

import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import zipfile

import bpy

import _bge_remix
import bge_remix
import bge_remix.installer as installer

argv = sys.argv[sys.argv.index("--") + 1:]
CASE, OUT = argv[0], argv[1]
EXTRA = argv[2:]

result = {"case": CASE, "ok": False, "checks": {}}


def check(name, cond, detail=None):
    result["checks"][name] = {"ok": bool(cond), "detail": detail}
    return bool(cond)


def write():
    result["ok"] = bool(result["checks"]) and all(c["ok"] for c in result["checks"].values())
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)


def bridge_ok():
    info = _bge_remix.bridge_info()
    result["bridge"] = info
    return check("bridge loads", info["loaded"], info["error"]) and \
        check("bridge interface", info["interface"] == _bge_remix.INTERFACE, info["interface"])


def case_missing():
    bridge_ok()
    code, text = bge_remix.status()
    check("status before use", code == "NOT_INSTALLED", [code, text])
    code, text = bge_remix.load()
    check("load reports not installed", code == "NOT_INSTALLED", [code, text])
    code, text = bge_remix.startup(320, 180, False)
    check("startup refused", code == "NOT_INSTALLED", [code, text])


def case_version():
    bridge_ok()
    code, text = bge_remix.load()
    check("load reports incompatible version", code == "INCOMPATIBLE_VERSION", [code, text])
    code, text = bge_remix.startup(320, 180, False)
    check("startup refused", code == "INCOMPATIBLE_VERSION", [code, text])


def case_sequence():
    bridge_ok()
    code, text = bge_remix.load()
    check("load ready", code == "READY", [code, text])
    code, text = bge_remix.startup(640, 360, False)
    check("startup running", code == "RUNNING", [code, text])
    shot = os.path.splitext(OUT)[0] + ".bmp"
    r = _bge_remix.triangle_test(1.0, shot, False)
    result["triangle"] = r
    check("triangle test ok", r["ok"] and r["frames"] > 0, r)
    # No shutdown here on purpose: Blender's exit must do it (checked in the log).


def case_startup_fail():
    bridge_ok()
    code, text = bge_remix.load()
    check("load ready", code == "READY", [code, text])
    code, text = bge_remix.startup(320, 180, False)
    check("startup failed", code == "STARTUP_FAILED", [code, text])
    check("message names the driver", "driver" in text.lower(), text)
    # Blender itself keeps working: render the default scene.
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_RENDER'
    scene.render.resolution_x, scene.render.resolution_y = 320, 180
    scene.render.resolution_percentage = 100
    path = os.path.splitext(OUT)[0] + "_render.png"
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    check("Blender still renders", os.path.isfile(path), path)


def _make_package(folder, mock_dll):
    os.makedirs(folder, exist_ok=True)
    pkg = os.path.join(folder, "test-runtime.zip")
    with zipfile.ZipFile(pkg, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(mock_dll, "d3d9.dll")
        z.writestr("LICENSE.txt", "BGE-Remix test package\n")
        z.writestr("usd/plugInfo.json", "{}\n")
    with open(pkg, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()
    return pkg, sha, os.path.getsize(pkg)


def _file_url(path):
    import pathlib
    return pathlib.Path(os.path.abspath(path)).as_uri()


class _Pin:
    VERSION = "test"


def case_installer():
    mock_dll = EXTRA[0]
    base = os.path.dirname(os.path.abspath(OUT))
    pkg, sha, size = _make_package(os.path.join(base, "package"), mock_dll)
    # a folder with non-English letters, like a user name would have
    target = os.path.join(base, "Ayhan Köse", "remix", "test")
    shutil.rmtree(os.path.join(base, "Ayhan Köse"), ignore_errors=True)

    good = _Pin()
    good.URL, good.SHA256, good.SIZE = _file_url(pkg), sha, size
    steps = []
    ok, msg = installer.install(target, good, lambda p, d, t: steps.append(p))
    check("good package installs", ok and os.path.isfile(os.path.join(target, "d3d9.dll")), msg)
    check("progress reported", "download" in steps and "unpack" in steps, sorted(set(steps)))
    check("no leftovers after install",
          not os.path.exists(target + ".download") and not os.path.exists(target + ".partial"))

    os.environ["BGE_REMIX_RUNTIME_DIR"] = target
    code, text = bge_remix.status()
    check("status sees the install", code == "INSTALLED", [code, text])
    code, text = bge_remix.load()
    check("installed runtime loads", code == "READY", [code, text])
    bge_remix.shutdown()

    bad = _Pin()
    bad.URL, bad.SHA256, bad.SIZE = _file_url(pkg), "0" * 64, size
    target2 = target + "-bad"
    ok, msg = installer.install(target2, bad)
    check("bad checksum rejected", not ok and "SHA-256" in msg, msg)
    check("nothing left after a rejected install",
          not os.path.exists(target2) and not os.path.exists(target2 + ".download")
          and not os.path.exists(target2 + ".partial"))

    ok, msg = installer.remove(target)
    check("remove works", ok and not os.path.exists(target), msg)


def case_real_install():
    from bge_remix import pin
    target = bge_remix.runtime_dir()
    t0 = time.time()
    last = [0]

    def progress(phase, done, total):
        if time.time() - last[0] > 10:
            last[0] = time.time()
            print("  %s %d / %d MB" % (phase, done // 1000000, total // 1000000), flush=True)

    ok, msg = installer.install(target, pin, progress)
    result["seconds"] = round(time.time() - t0, 1)
    result["target"] = target
    check("pinned runtime installs", ok and os.path.isfile(os.path.join(target, "d3d9.dll")), msg)


def _vram_mb():
    try:
        out = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
            universal_newlines=True, timeout=20)
        return int(out.strip().splitlines()[0])
    except Exception as ex:
        return "unknown (%s)" % ex


def _find_captures(since, roots):
    found = []
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            if dirpath[len(root):].count(os.sep) > 5:
                dirnames[:] = []
                continue
            for name in filenames:
                if name.startswith("capture") and ".usd" in name:
                    path = os.path.join(dirpath, name)
                    try:
                        if os.path.getmtime(path) >= since - 2:
                            found.append(path)
                    except OSError:
                        pass
    return found


def case_rtx():
    bridge_ok()
    result["vram_before_mb"] = _vram_mb()
    t0 = time.time()
    code, text = bge_remix.load()
    t_load = time.time()
    check("load ready", code == "READY", [code, text])
    code, text = bge_remix.startup(1920, 1080, True)
    t_start = time.time()
    check("startup running", code == "RUNNING", [code, text])
    if code != "RUNNING":
        return
    first = _bge_remix.triangle_test(0.0, "", False)  # one frame: first frame time
    t_first = time.time()
    result["B1.2"] = {"load_s": round(t_load - t0, 3), "startup_s": round(t_start - t_load, 3),
                      "first_frame_s": round(t_first - t_start, 3), "total_s": round(t_first - t0, 3),
                      "first_frames": first["frames"]}
    result["vram_after_startup_mb"] = _vram_mb()

    shot = os.path.splitext(OUT)[0] + ".bmp"
    since = time.time()
    r = _bge_remix.triangle_test(6.0, shot, True)
    result["T1.1"] = r
    check("T1.1 frames without errors", r["ok"] and r["frames"] > 30, r)
    check("T1.1 screenshot saved", r["screenshot_saved"], shot)

    # B1.3: frame time at 1080p over at least 300 frames
    r3 = _bge_remix.triangle_test(max(5.0, 300 * r["frame_ms_median"] / 1000.0 + 1.0), "", False)
    result["B1.3"] = {"frames": r3["frames"], "frame_ms_median": r3["frame_ms_median"],
                      "frame_ms_p95": r3["frame_ms_p95"], "vram_mb": _vram_mb()}

    time.sleep(3)  # give the capture time to be written
    roots = [os.path.dirname(bpy.app.binary_path), bge_remix.runtime_dir(), os.getcwd()]
    caps = _find_captures(since, roots)
    meshes = []
    for c in caps:
        mdir = os.path.join(os.path.dirname(c), "meshes")
        if os.path.isdir(mdir):
            meshes += os.listdir(mdir)
    result["C1"] = {"captures": caps, "mesh_files": len(meshes)}
    # C1 is recorded, not required: its result decides how P6 captures.


CASES = {
    "missing": case_missing,
    "version": case_version,
    "sequence": case_sequence,
    "startup_fail": case_startup_fail,
    "installer": case_installer,
    "real_install": case_real_install,
    "rtx": case_rtx,
}

try:
    CASES[CASE]()
except Exception:
    import traceback
    check("no exception", False, traceback.format_exc())
    print(traceback.format_exc())
write()
