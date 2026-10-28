"""BGE-Remix phase 5 tests: the 3D view and F12.

CI (fake runtime, no GPU; copy mode):
  T5.0  the quality settings reach Remix (F12 from the command line)
  T5.1  editing in a Rendered 3D view: each step sends only what changed
  T5.2  select by click: the right object, and Remix outlines exactly it
  T5.3  F12: a still at 1280 x 720 (the fake picture, 1:1), and p5_anim's 24
        frames with the marker where the animation puts it
  T5.5  P in a Rendered view: the game takes Remix (started once) and the
        view gets it back with everything
  T5.6  sizes and views: the picture follows the view; a second Rendered
        view and an orthographic view draw Solid with the note
  T5.7  OpenGL state unchanged by every draw; everything outside the 3D view
        pixel-identical in Solid and in Remix
  T5.8  without the runtime: the reason once, Solid, F12 reports it, no crash
RTX (your PC):
  T5.0-T5.7 in direct mode, with the sign-off sheets (T5.1, T5.3), T5.4
  (F12 and the game draw P3's room the same), B5.1-B5.6

  python run_p5.py --ours <build> --mock <mock> --out <dir>
  python run_p5.py --ours <build> --out <dir> --rtx
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
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p3"))
import run_p2 as p2  # noqa: E402  (running Blender, the fake runtime's log, pictures)

PROBE = os.path.join(HERE, "p5_ui_probe.py")
WINDOW = ["-p", "0", "0", "1600", "900"]
QUALITY = {"bge_remix_preset": 1, "bge_remix_dlss": 2, "bge_remix_rr": 0, "bge_remix_volumetrics": 0}
QUALITY_LINES = ["config rtx.graphicsPreset = 3", "config rtx.dlssPreset = 2", "config rtx.qualityDLSS = 1",
                 "config rtx.enableRayReconstruction = False", "config rtx.volumetrics.enable = False"]


def make_scenes(ours, out):
    scenes = os.path.join(out, "scenes")
    if os.path.isdir(scenes):
        shutil.rmtree(scenes, ignore_errors=True)
    os.makedirs(scenes, exist_ok=True)
    code, text, _ = p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup",
                            "--python", os.path.join(HERE, "make_scenes_p5.py"), "--", scenes],
                           p2.base_env(out, "make"), os.path.join(out, "make_scenes_p5.log"))
    expected = p2.read_json(os.path.join(scenes, "p5_expected.json"))
    if code != 0 or not expected:
        print("\n".join(text.splitlines()[-40:]))
        raise SystemExit("making the P5 test scenes failed, see make_scenes_p5.log")
    for name, tb in expected.get("failed", {}).items():
        print("making the %s scene failed:\n%s" % (name, tb))
    return scenes, expected


def fresh_tmp(out, name):
    """a TEMP of its own: the bridge's log (bge-remix.log) and crash reports land there"""
    tmp = os.path.join(out, "tmp_" + name)
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp, exist_ok=True)
    return tmp


def bridge_log(tmp):
    return p2.read_lines(os.path.join(tmp, "bge-remix.log"))


def ui_run(ours, out, name, blend, env, parts, wait=30, timeout=1200):
    """Blender's UI on a scene, driven by p5_ui_probe.py"""
    res = os.path.join(out, name + ".json")
    shots = os.path.join(out, name + "_shots")
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots, exist_ok=True)
    if os.path.isfile(res):
        os.remove(res)
    tmp = fresh_tmp(out, name)
    e = p2.base_env(out, name)
    e.update({"BGE_REMIX_P5_OUT": res, "BGE_REMIX_P5_DIR": shots, "BGE_REMIX_P5_PARTS": parts,
              "BGE_REMIX_P5_WAIT": str(wait), "TEMP": tmp, "TMP": tmp,
              "BGE_REMIX_P2_OUT": os.path.join(out, name + "_game.json"),
              "BGE_REMIX_STATS": os.path.join(out, name + "_game_stats.json"),
              "BGE_REMIX_P2_SHOT": os.path.join(out, name + "_game.png")})
    e.update(env)
    code, text, secs = p2.run([p2.exe(ours, "blender")] + WINDOW + [blend, "--python", PROBE], e,
                              os.path.join(out, name + ".log"), timeout=timeout, cwd=out)
    probe = p2.read_json(res)
    steps = {s["step"]: s for s in probe.get("steps", [])}
    crash = [n for n in os.listdir(tmp) if n.endswith(".crash.txt")]
    for n in crash:
        shutil.copy2(os.path.join(tmp, n), os.path.join(out, name + "_" + n))
    if code != 0 or not probe.get("finished"):
        print("---- %s: exit %s; the end of its output:" % (name, code))
        print("\n".join("    " + l for l in text.splitlines()[-30:]))
    return {"exit": code, "seconds": round(secs, 1), "probe": probe, "steps": steps, "text": text, "tmp": tmp,
            "errors": probe.get("errors", []), "crash": crash,
            "game": p2.read_json(os.path.join(out, name + "_game.json")),
            "game_stats": p2.read_json(os.path.join(out, name + "_game_stats.json"))}


def f12(ours, out, name, blend, env, settings=None, anim=False, frame=1, timeout=1800):
    """F12 from the command line: blender -b <blend> [settings] -o ... -f N | -a"""
    folder = os.path.join(out, name)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder, exist_ok=True)
    tmp = fresh_tmp(out, name)
    e = p2.base_env(out, name)
    e.update({"TEMP": tmp, "TMP": tmp})
    e.update(env)
    cmd = [p2.exe(ours, "blender"), "--background", blend]
    if settings:
        expr = "import bpy; s = bpy.context.scene; " + "; ".join(
            "s[%r] = %r" % (k, v) if k.startswith("bge_") else "s.%s = %r" % (k, v) for k, v in settings.items())
        cmd += ["--python-expr", expr]
    cmd += ["-o", os.path.join(folder, "f12_####"), "-F", "PNG", "-x", "1"]
    cmd += ["-a"] if anim else ["-f", str(frame)]
    code, text, secs = p2.run(cmd, e, os.path.join(out, name + ".log"), timeout=timeout, cwd=out)
    files = sorted(os.path.join(folder, n) for n in os.listdir(folder) if n.endswith(".png"))
    return {"exit": code, "seconds": round(secs, 1), "files": files, "text": text, "tmp": tmp,
            "traceback": "Traceback" in text}


def diff(a, b, key):
    return (b.get("stats", {}).get(key, 0) or 0) - (a.get("stats", {}).get(key, 0) or 0)


def bridge(step, key):
    return step.get("stats", {}).get("bridge", {}).get(key)


def main_region(step, key="region"):
    for v in step.get("views", []):
        if v.get("main"):
            return v[key]
    return None


def outside_identical(a_path, b_path, rect):
    """pixels outside rect (x, y, w, h from the window's bottom left) the same in both"""
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None, {}
    if not a_path or not b_path or not os.path.isfile(a_path) or not os.path.isfile(b_path) or not rect:
        return False, {"missing": True}
    a = np.asarray(Image.open(a_path).convert("RGB"), dtype=np.int32)
    b = np.asarray(Image.open(b_path).convert("RGB"), dtype=np.int32)
    if a.shape != b.shape:
        return False, {"shapes": [a.shape, b.shape]}
    h = a.shape[0]
    mask = np.ones(a.shape[:2], dtype=bool)
    x, y, w, hh = rect
    # the whole 3D view area (rect): its header's shading button changes too
    bottom = h - y
    mask[max(bottom - hh - 2, 0):min(bottom + 2, h), max(x - 2, 0):x + w + 2] = False
    d = np.abs(a - b).max(axis=2)
    changed = int((d[mask] > 0).sum())
    return changed == 0, {"changed_pixels": changed}


def picture_of(path, rect=None):
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None
    if not path or not os.path.isfile(path):
        return None
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.int32)
    if rect:
        h = a.shape[0]
        x, y, w, hh = rect
        a = a[h - y - hh:h - y, x:x + w]
    return a


# ---------------------------------------------------------------------------
# CI

def run_ci(args, out):
    scenes, expected = make_scenes(args.ours, out)
    mock = os.path.abspath(args.mock)
    studio = os.path.join(scenes, "p5_studio.blend")
    anim = os.path.join(scenes, "p5_anim.blend")
    st_exp = expected.get("studio") or {}
    res = {}

    def mock_env(name, extra=None):
        log = os.path.join(out, name + "_mock.log")
        if os.path.isfile(log):
            os.remove(log)
        env = {"BGE_REMIX_RUNTIME_DIR": mock, "BGE_REMIX_MOCK_MODE": "ok", "BGE_REMIX_MOCK_LOG": log,
               "BGE_REMIX_CACHE_DIR": os.path.join(out, "cache"), "BGE_REMIX_TEXTURE_NO_GPU": "1",
               "BGE_REMIX_PRESENT": "copy", "BGE_REMIX_P2_FRAMES": "120", "BGE_REMIX_P2_SECONDS": "0"}
        env.update(extra or {})
        return log, env

    def guarded(keys, fn):
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

    # ---- the UI run: T5.1, T5.2, T5.5, T5.6, T5.7
    def ui():
        log, env = mock_env("ui", {"BGE_REMIX_GL_CHECK": "1"})
        r = ui_run(args.ours, out, "ui", studio, env, "edit,select,game,ortho,split")
        s = r["steps"]
        ran = r["exit"] == 0 and r["probe"].get("finished") is True and not r["crash"]
        errors = [e.strip().splitlines()[-1] for e in r["errors"]]
        mlines = p2.read_lines(log)

        def step(name):
            return s.get(name, {})

        opened = step("open")
        c = {
            "Blender ran the steps and quit cleanly": ran,
            "no step raised an error (%s)" % ("; ".join(errors) or "none"): not errors,
            "Rendered shading drew Remix frames": (opened.get("stats", {}).get("view_frames", 0) or 0) > 0,
            "the 3D view has Remix": opened.get("stats", {}).get("owner") == 2,
            "opening sent the scene's %d meshes" % st_exp.get("meshes", 0):
                diff(step("solid"), opened, "meshes_sent") == st_exp.get("meshes"),
            "and drew %d objects, %d lights" % (st_exp.get("draws", 0), st_exp.get("lights", 0)):
                bridge(opened, "draws") == st_exp.get("draws") and bridge(opened, "lights") == st_exp.get("lights"),
            "moving the cube sent no mesh and no material":
                diff(opened, step("move"), "meshes_sent") == 0 and diff(opened, step("move"), "materials_sent") == 0,
            "recolouring the monkey sent its material, no mesh":
                diff(step("move"), step("recolour"), "materials_sent") >= 1 and
                diff(step("move"), step("recolour"), "meshes_sent") == 0,
            "editing the ball sent its mesh (only)": 1 <= diff(step("recolour"), step("edit"), "meshes_sent") <= 2,
            "the subdivision change sent one mesh": diff(step("edit_exit"), step("subdivision"), "meshes_sent") == 1,
            "dimming the lamp sent the light, no mesh":
                diff(step("subdivision"), step("lamp"), "lights_sent") >= 1 and
                diff(step("subdivision"), step("lamp"), "meshes_sent") == 0,
            "adding a cone sent one mesh and draws it":
                diff(step("lamp"), step("add"), "meshes_sent") == 1 and bridge(step("add"), "draws") == st_exp.get("draws", 0) + 1,
            "deleting it removed it":
                diff(step("add"), step("delete"), "meshes_removed") == 1 and bridge(step("delete"), "draws") == st_exp.get("draws"),
            "undo brought it back": "AddedCone" in step("undo").get("objects", []) and
                bridge(step("undo"), "draws") == st_exp.get("draws", 0) + 1,
            # undo replaces every object, mesh and material in memory, and Blender
            # gives the view a new engine: it sends the scene once more, and Remix
            # keeps nothing twice (8 meshes: the 7 and the cone)
            "and the view sent the scene again, nothing twice": diff(step("delete"), step("undo"), "view_full_syncs") == 1 and
                bridge(step("undo"), "meshes") == st_exp.get("meshes", 0) + 1,
        }
        res["T5.1"] = dict(p2.test(c), detail={k: {"stats": v.get("stats"), "objects": v.get("objects")}
                                                for k, v in s.items() if not k.startswith("select_")})

        c = {}
        for name in ("Monkey", "GlassCube", "CheckerBall", "Marker", "Pillar.2", "Floor"):
            st = step("select_" + name)
            c["click on %s selects it" % name] = st.get("selected") == [name]
            c["click on %s: Remix outlines one object" % name] = st.get("stats", {}).get("highlighted") == 1
        res["T5.2"] = dict(p2.test(c), detail={k: {"selected": v.get("selected"), "click": v.get("click"),
                                                    "highlighted": v.get("stats", {}).get("highlighted")}
                                                for k, v in s.items() if k.startswith("select_")})

        before, after = step("before_game"), step("after_game")
        creates = [l for l in mlines if l.startswith("CreateD3D9")]
        game_ran = (r["game"].get("frames", 0) or 0) >= 120
        c = {
            "the game ran 120 frames": game_ran,
            "through Remix": r["game_stats"].get("running") is True,
            "Remix started once (one device for the view and the game)": len(creates) == 1,
            "the view has Remix again afterwards": after.get("stats", {}).get("owner") == 2,
            "and sent everything again": diff(before, after, "view_full_syncs") >= 1 and
                bridge(after, "draws") == bridge(before, "draws"),
        }
        res["T5.5"] = dict(p2.test(c), detail={"game": r["game"], "game_stats": {k: r["game_stats"].get(k) for k in
                                                                               ("running", "mode", "frames")},
                                                "creates": len(creates)})

        split, ortho = step("split"), step("ortho")
        reg = main_region(split) or [0, 0, 0, 0]
        sizes = [v["region"][2:] for v in split.get("views", [])]
        c = {
            "after the split, Remix's picture has its view's (new) size":
                len(sizes) == 2 and [bridge(split, "width"), bridge(split, "height")] in sizes and
                [bridge(split, "width"), bridge(split, "height")] != (main_region(step("camera")) or [0, 0, 0, 0])[2:],
            "the second Rendered view draws Solid with the note":
                "another 3D view" in (split.get("stats", {}).get("note") or "") and diff(step("camera"), split, "view_fallbacks") >= 1,
            "an orthographic view draws Solid with the note":
                "perspective" in (ortho.get("stats", {}).get("note") or ""),
        }
        res["T5.6"] = dict(p2.test(c), detail={"region": reg, "bridge": split.get("stats", {}).get("bridge"),
                                                "notes": [split.get("stats", {}).get("note"),
                                                          ortho.get("stats", {}).get("note")]})

        same, info = outside_identical(step("solid").get("screenshot"), opened.get("screenshot"),
                                       main_region(opened, "area"))
        last = step("split") or step("camera") or opened
        c = {
            "no OpenGL state changed by any draw (BGE_REMIX_GL_CHECK)": ran and
                (last.get("stats", {}).get("gl_differences", 1) == 0),
            "everything outside the 3D view the same in Solid and in Remix": bool(same),
        }
        gl_lines = [l for l in r["text"].splitlines() if l.startswith("BGE-Remix GL check")]
        res["T5.7"] = dict(p2.test(c), detail=dict(info, gl_lines=gl_lines[:10]))

    guarded(["T5.1", "T5.2", "T5.5", "T5.6", "T5.7"], ui)

    # ---- F12: T5.0 and T5.3
    def f12_tests():
        settings = dict(QUALITY, bge_remix_f12_frames=2)
        log, env = mock_env("f12_still")
        r = f12(args.ours, out, "f12_still", studio, env, settings=settings)
        img = picture_of(r["files"][0]) if r["files"] else None
        size_ok = img is not None and img.shape[1] == 1280 and img.shape[0] == 720
        colour = img[360, 640].tolist() if img is not None else None
        blog = bridge_log(r["tmp"])
        c = {
            "Blender rendered without an error": r["exit"] == 0 and not r["traceback"] and len(r["files"]) == 1,
            "1280 x 720": size_ok,
            "Remix's picture as it is (the fake runtime's colour, 1:1)": colour is not None and
                all(abs(a - b) <= 2 for a, b in zip(colour, p2.FILL)),
        }
        res["T5.3"] = {"still": dict(p2.test(c), detail={"colour": colour, "files": r["files"], "seconds": r["seconds"]})}
        c = {"%s" % line: any(l.endswith(line) or (line in l) for l in blog) for line in QUALITY_LINES}
        res["T5.0"] = dict(p2.test(c), detail={"config": [l for l in blog if " config " in l or "config rtx" in l][-20:]})

        fr = expected.get("anim") or {}
        n = fr.get("frames", 24)
        per = 3 + 1  # settle frames + 1 frame each (bge_remix_f12_frames = 1)
        listed = ",".join(str(per * k - 1) for k in range(1, n + 1))
        log, env = mock_env("f12_anim", {"BGE_REMIX_MOCK_DRAWS_FRAME": listed})
        r = f12(args.ours, out, "f12_anim", anim, env, settings={"bge_remix_f12_frames": 1}, anim=True)
        marker = p2.fnv1a("render:mesh:MarkerMesh")
        lines = p2.read_lines(log)
        xs = {}
        for l in lines:
            if not l.startswith("Draw "):
                continue
            f = p2.parse_fields(l)
            m = re.search(r"\bmesh=([0-9a-fA-F]+)\b", l)
            if m and int(m.group(1), 16) == marker:
                mm = [float(v) for v in str(f["m"]).split(",")] if isinstance(f.get("m"), str) else f.get("m")
                xs[int(f["frame"])] = mm[3]
        got = [xs.get(per * k - 1) for k in range(1, n + 1)]
        want = fr.get("marker_x", [])
        c = {
            "Blender rendered the animation without an error": r["exit"] == 0 and not r["traceback"],
            "%d pictures" % n: len(r["files"]) == n,
            "the marker where the animation puts it, every frame": len(got) == len(want) and
                all(g is not None and abs(g - w) < 1e-3 for g, w in zip(got, want)),
        }
        res["T5.3"]["animation"] = dict(p2.test(c), detail={"marker_x": got, "expected": want, "seconds": r["seconds"]})
        res["T5.3"]["ok"] = res["T5.3"]["still"]["ok"] and res["T5.3"]["animation"]["ok"]

    guarded(["T5.0", "T5.3"], f12_tests)

    # ---- T5.8: no runtime
    def missing():
        empty = os.path.join(out, "no_runtime")
        os.makedirs(empty, exist_ok=True)
        env = {"BGE_REMIX_RUNTIME_DIR": empty, "BGE_REMIX_CACHE_DIR": os.path.join(out, "cache")}
        r = ui_run(args.ours, out, "ui_missing", studio, env, "", wait=20)
        opened = r["steps"].get("open", {})
        note = opened.get("stats", {}).get("note") or ""
        fr = f12(args.ours, out, "f12_missing", studio, env)
        c = {
            "Blender ran and quit cleanly": r["exit"] == 0 and r["probe"].get("finished") is True,
            "the view says why (%s)" % note: bool(note) and opened.get("stats", {}).get("view_frames", 1) == 0,
            # an engine's error makes "blender -b -f" end with exit code 1 (G.is_break)
            "F12 reports why and Blender ends cleanly": fr["exit"] in (0, 1) and "Blender quit" in fr["text"] and
                "No RTX Remix runtime" in fr["text"] and not fr["traceback"],
        }
        res["T5.8"] = dict(p2.test(c), detail={"note": note, "f12_exit": fr["exit"],
                                               "f12_errors": [l for l in fr["text"].splitlines() if "Error" in l]})

    guarded(["T5.8"], missing)

    keys = ["T5.0", "T5.1", "T5.2", "T5.3", "T5.5", "T5.6", "T5.7", "T5.8"]
    res["all_ok"] = all(res.get(k, {}).get("ok") for k in keys)
    names = {"T5.0": "Quality settings", "T5.1": "Editing in a Rendered view", "T5.2": "Select by click",
             "T5.3": "F12 still and animation", "T5.5": "Game from a Remix view", "T5.6": "Sizes and views",
             "T5.7": "OpenGL state and UI", "T5.8": "Without the runtime"}
    lines = ["BGE-Remix phase 5 results (CI, fake runtime)", "=" * 44, ""]
    for k in keys:
        lines.append("%s %-28s %s" % (k, names[k], p2.mark(res.get(k, {}).get("ok"))))
        r = res.get(k, {})
        for sub in ([r] + [r[x] for x in ("still", "animation") if x in r]):
            for name, ok in (sub.get("checks") or {}).items():
                if not ok:
                    lines.append("     failed: " + name)
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p5.json and the .log files)"]
    return res, lines


# ---------------------------------------------------------------------------
# RTX

CYCLES_SETUP = r'''
# p5_studio in Cycles for the sign-off: the lamps and the sky as BGE-Remix
# sends them to Remix, node materials as they are, the others as diffuse.
import math, sys
import bpy
out = sys.argv[sys.argv.index("--") + 1]
samples = int(sys.argv[sys.argv.index("--") + 2])
size = [int(v) for v in sys.argv[sys.argv.index("--") + 3].split("x")]
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = samples
scene.render.resolution_x, scene.render.resolution_y = size
scene.render.resolution_percentage = 100
try:
    scene.view_settings.view_transform = 'Filmic'
    scene.view_settings.look = 'Filmic - Base Contrast'
except Exception:
    pass
hemi = [0.0, 0.0, 0.0]
scale = scene.get("bge_remix_light_scale", 0.125)
for ob in scene.objects:
    if ob.type != 'LAMP':
        continue
    la = ob.data
    E, D = la.energy * scale * 8.0, getattr(la, "distance", 10.0)
    la.use_nodes = True
    em = la.node_tree.nodes.get("Emission")
    if la.type in ('POINT', 'SPOT'):
        strength = 4.0 * math.pi * math.pi * E * D
    elif la.type == 'SUN':
        strength = math.pi * E
    else:
        strength = 4.0 * math.pi * E * D
    em.inputs["Strength"].default_value = strength
    em.inputs["Color"].default_value = list(la.color) + [1.0]
w = scene.world
w.use_nodes = True
bg = w.node_tree.nodes.get("Background")
hc = w.horizon_color
bg.inputs["Color"].default_value = [hc[0], hc[1], hc[2], 1.0]
for ma in bpy.data.materials:
    if ma.use_nodes:
        continue
    ma.use_nodes = True
    nt = ma.node_tree
    nt.nodes.clear()
    outn = nt.nodes.new("ShaderNodeOutputMaterial")
    diff = nt.nodes.new("ShaderNodeBsdfDiffuse")
    diff.inputs["Color"].default_value = [c * ma.diffuse_intensity for c in ma.diffuse_color] + [1.0]
    nt.links.new(diff.outputs[0], outn.inputs["Surface"])
scene.render.filepath = out
scene.render.image_settings.file_format = 'PNG'
bpy.ops.render.render(write_still=True)
'''


def cycles(ours, out, blend, name, samples=128, size=(1280, 720)):
    script = os.path.join(out, "p5_cycles_setup.py")
    with open(script, "w", encoding="utf-8") as f:
        f.write(CYCLES_SETUP)
    target = os.path.join(out, name + ".png")
    code, text, secs = p2.run([p2.exe(ours, "blender"), "--background", blend, "--python", script, "--", target,
                               str(samples), "%dx%d" % size], p2.base_env(out, "cycles"),
                              os.path.join(out, name + ".log"), timeout=3600)
    return {"ok": code == 0 and os.path.isfile(target), "image": target, "seconds": round(secs, 1)}


def changed_near(a_path, b_path, rect, points, radius=0.22, threshold=40):
    """share of the pixels that changed (by more than threshold) near the
    points (0..1 in the region, bottom left), and how many changed"""
    a, b = picture_of(a_path, rect), picture_of(b_path, rect)
    if a is None or b is None or a.shape != b.shape:
        return None, 0
    import numpy as np
    d = np.abs(a - b).max(axis=2) > threshold
    n = int(d.sum())
    if n == 0:
        return 0.0, 0
    h, w = d.shape
    ys, xs = np.nonzero(d)
    near = np.zeros(n, dtype=bool)
    for px, py in points:
        cx, cy = px * w, (1.0 - py) * h
        near |= ((xs - cx) ** 2 + (ys - cy) ** 2) <= (radius * w) ** 2
    return float(near.mean()), n


def yellow_centroid(path):
    """the marker's centre in a picture (0..1, bottom left): bright yellow pixels"""
    a = picture_of(path)
    if a is None:
        return None
    import numpy as np
    # Remix draws the glowing marker pale yellow (about 250, 249, 130)
    m = (a[:, :, 0] > 200) & (a[:, :, 1] > 200) & (a[:, :, 0] - a[:, :, 2] > 60)
    if m.sum() < 20:
        return None
    ys, xs = np.nonzero(m)
    h, w = m.shape
    return [float(xs.mean()) / w, 1.0 - float(ys.mean()) / h]


def run_rtx(args, out):
    scenes, expected = make_scenes(args.ours, out)
    studio = os.path.join(scenes, "p5_studio.blend")
    anim = os.path.join(scenes, "p5_anim.blend")
    st_exp = expected.get("studio") or {}
    res = {}
    clean = {"BGE_REMIX_RUNTIME_DIR": None, "BGE_REMIX_MOCK_LOG": None, "BGE_REMIX_PRESENT": None,
             "BGE_REMIX_FAKE_VK_DRAW": None}

    def env(extra=None):
        e = dict(clean)
        e.update(extra or {})
        return e

    # ---- the UI run
    print("T5.1, T5.2, T5.5-T5.7: the studio in a Rendered 3D view (about 4 minutes)...", flush=True)
    ui = ui_run(args.ours, out, "ui", studio, env({"BGE_REMIX_GL_CHECK": "1", "BGE_REMIX_P2_FRAMES": "300",
                                                    "BGE_REMIX_P2_SECONDS": "6"}),
                "edit,select,game,ortho,split", wait=40)
    s = ui["steps"]
    ran = ui["exit"] == 0 and ui["probe"].get("finished") is True and not ui["crash"]
    opened = s.get("open", {})
    reg = main_region(opened)
    tg = st_exp.get("targets", {})

    def near(a, b, names):
        share, n = changed_near(s.get(a, {}).get("screenshot"), s.get(b, {}).get("screenshot"), reg,
                                [tg[x] for x in names if x in tg])
        return share, n

    checks = {"Blender ran the steps and quit cleanly": ran,
              "the view is in direct mode": bridge(opened, "mode") == 4,
              "Remix drew the scene (%s objects)" % bridge(opened, "draws"): bridge(opened, "draws") == st_exp.get("draws"),
              # the first time, Remix compiles its shaders and draws black for some seconds
              "the view showed Remix's picture (after %s s)" % opened.get("ready_seconds"): opened.get("ready") is True}
    local = {}
    for a, b, names in (("open", "move", ["GlassCube"]), ("move", "recolour", ["Monkey"]),
                        ("recolour", "edit", ["CheckerBall"]), ("lamp", "add", ["AddedCone"])):
        share, n = near(a, b, names)
        local[b] = {"share_near": share, "changed": n}
        checks["%s: the picture changed where %s is" % (b, names[0])] = share is not None and n > 200 and share >= 0.6
    res["T5.1"] = {"ok": all(checks.values()), "checks": checks, "local": local,
                   "note": "look at T5.1_compare.png: each step changes what it says"}
    sheet = [(n, s.get(n, {}).get("screenshot")) for n in ("open", "move", "recolour", "edit", "subdivision", "lamp",
                                                            "add", "undo")]
    cyc = cycles(args.ours, out, studio, "cycles_studio")
    sheet.insert(0, ("Cycles (F12, CPU, 128 samples)", cyc["image"]))
    res["T5.1"]["compare"] = p2.contact_sheet(sheet, os.path.join(out, "T5.1_compare.png"))

    c = {}
    for name in ("Monkey", "GlassCube", "CheckerBall", "Marker", "Pillar.2", "Floor"):
        st = s.get("select_" + name, {})
        c["%s selected and outlined" % name] = st.get("selected") == [name] and st.get("stats", {}).get("highlighted") == 1
    res["T5.2"] = {"ok": all(c.values()), "checks": c,
                   "compare": p2.contact_sheet([(n, s.get("select_" + n, {}).get("screenshot"))
                                                for n in ("Monkey", "GlassCube", "CheckerBall", "Pillar.2")],
                                               os.path.join(out, "T5.2_compare.png"))}

    before, after = s.get("before_game", {}), s.get("after_game", {})
    a_b = picture_of(before.get("screenshot"), reg)
    a_a = picture_of(after.get("screenshot"), reg)
    same = None
    if a_b is not None and a_a is not None and a_b.shape == a_a.shape:
        same = float(abs(a_b - a_a).mean())
    c = {"the game ran through Remix": (ui["game"].get("frames", 0) or 0) >= 300 and ui["game_stats"].get("running") is True,
         "the view has Remix again, the same picture (mean difference %s)" % (None if same is None else round(same, 2)):
             after.get("stats", {}).get("owner") == 2 and same is not None and same < 12}
    res["T5.5"] = {"ok": all(c.values()), "checks": c, "game_stats": {k: ui["game_stats"].get(k) for k in
                                                                    ("running", "mode", "frames")}}

    split, ortho = s.get("split", {}), s.get("ortho", {})
    sreg = main_region(split) or [0, 0, 0, 0]
    print("T5.6: the game window resized in direct mode...", flush=True)
    rz = p2.player(args.ours, out, "T5.6_resize", studio,
                   env({"BGE_REMIX_P2_FRAMES": "200", "BGE_REMIX_P2_SECONDS": "6", "BGE_REMIX_P5_RESIZE_TIC": "120",
                        "BGE_REMIX_P5_RESIZE_TO": "960x540"}), size=(1280, 720), cwd=out)
    rs = rz["stats"] or {}
    sizes = [v["region"][2:] for v in split.get("views", [])]
    c = {"after the split, Remix's picture has its view's (new) size":
             [bridge(split, "width"), bridge(split, "height")] in sizes,
         "the second view: Solid and the note": "another 3D view" in (split.get("stats", {}).get("note") or ""),
         "orthographic: Solid and the note": "perspective" in (ortho.get("stats", {}).get("note") or ""),
         "the game's picture follows its window (960 x 540)":
             rz["exit"] == 0 and [rs.get("picture_width"), rs.get("picture_height")] == [960, 540]}
    res["T5.6"] = {"ok": all(c.values()), "checks": c, "game": {k: rs.get(k) for k in
                                                                ("mode", "width", "height", "picture_width",
                                                                 "picture_height", "resets")}}

    same_ui, info = outside_identical(s.get("solid", {}).get("screenshot"), opened.get("screenshot"),
                                      main_region(opened, "area"))
    last = split or opened
    c = {"no OpenGL state changed by any draw": ran and last.get("stats", {}).get("gl_differences", 1) == 0,
         "outside the 3D view the same in Solid and in Remix": bool(same_ui)}
    res["T5.7"] = {"ok": all(c.values()), "checks": c, "detail": info}

    # ---- F12
    print("T5.0, T5.3: F12 stills (Real-time, Reference) and the animation...", flush=True)
    rt = f12(args.ours, out, "T5.3_realtime", studio, env(), settings={"bge_remix_f12_frames": 32})
    ref = f12(args.ours, out, "T5.3_reference", studio, env(),
              settings={"bge_remix_f12_mode": 1, "bge_remix_f12_samples": 256})
    q = f12(args.ours, out, "T5.0_quality", studio, env(), settings=dict(QUALITY, bge_remix_f12_frames=8))
    qlog = bridge_log(q["tmp"])
    # Remix logs no list of its settings: each one is checked by its call's
    # result (0: Remix took it; rtx.graphicsPreset applies the preset at once,
    # rtx_options.cpp graphicsPresetOnChange)
    c = {line + " (Remix took it)": any(line in l and l.rstrip().endswith("-> 0") for l in qlog)
         for line in QUALITY_LINES}
    res["T5.0"] = {"ok": all(c.values()), "checks": c}

    an = f12(args.ours, out, "T5.3_anim", anim, env(), settings={"bge_remix_f12_frames": 16}, anim=True)
    want = (expected.get("anim") or {}).get("marker_screen", [])
    got = [yellow_centroid(f) for f in an["files"]]
    # frames where the whole marker is in the picture (it starts and ends at the edges)
    inside = [0.05 < w[0] < 0.95 and 0.05 < w[1] < 0.95 for w in want]
    off = [None if g is None else round(math.hypot((g[0] - w[0]) * 1280, (g[1] - w[1]) * 720), 1)
           for g, w in zip(got, want)]
    off_inside = [o for o, i in zip(off, inside) if i]
    c = {"Real-time still": rt["exit"] == 0 and len(rt["files"]) == 1 and bool(p2.has_picture(rt["files"][0] if rt["files"] else None)[0]),
         "Reference still": ref["exit"] == 0 and len(ref["files"]) == 1 and bool(p2.has_picture(ref["files"][0] if ref["files"] else None)[0]),
         "24 animation frames": an["exit"] == 0 and len(an["files"]) == len(want),
         "the marker within 10 px of where it belongs, every frame it is in the picture":
             len(off_inside) >= 12 and all(o is not None and o < 10 for o in off_inside)}
    res["T5.3"] = {"ok": all(c.values()), "checks": c, "marker_offset_px": off, "marker_inside": inside,
                   "note": "look at T5.3_compare.png: Real-time and Reference next to Cycles"}
    res["T5.3"]["compare"] = p2.contact_sheet(
        [("Cycles (CPU, 128 samples)", cyc["image"]), ("Remix F12 Real-time (32 frames)", rt["files"][0] if rt["files"] else None),
         ("Remix F12 Reference (256 samples)", ref["files"][0] if ref["files"] else None),
         ("Animation frame 12", an["files"][11] if len(an["files"]) > 11 else None)],
        os.path.join(out, "T5.3_compare.png"))

    # ---- T5.4: F12 and the game draw P3's room the same
    print("T5.4: P3's room in the game and with F12...", flush=True)
    import run_p3 as p3  # noqa: E402
    os.makedirs(os.path.join(out, "p3"), exist_ok=True)
    p3scenes, _ = p3.make_scenes(args.ours, os.path.join(out, "p3"))
    room = os.path.join(p3scenes, "p3_room.blend")
    game = p2.player(args.ours, out, "T5.4_game", room, env({"BGE_REMIX_P2_SECONDS": "10", "BGE_REMIX_P2_FRAMES": "300",
                                                              "BGE_REMIX_P2_SHOT_FRAME": "200"}),
                     size=(1280, 720), cwd=out)
    fr = f12(args.ours, out, "T5.4_f12", room, env(), settings={"bge_remix_f12_frames": 200,
                                                                "render.resolution_x": 1280,
                                                                "render.resolution_y": 720,
                                                                "render.resolution_percentage": 100})
    a, b = picture_of(game["screenshot"]), picture_of(fr["files"][0] if fr["files"] else None)
    md = float(abs(a - b).mean()) if a is not None and b is not None and a.shape == b.shape else None
    res["T5.4"] = {"ok": md is not None and md < 12, "mean_difference": md,
                   "compare": p2.contact_sheet([("game (blenderplayer)", game["screenshot"]),
                                                ("F12", fr["files"][0] if fr["files"] else None)],
                                               os.path.join(out, "T5.4_compare.png"))}

    # ---- benchmarks
    print("B5.1-B5.3, B5.6: the view's frame rate, edit latency, idle, memory...", flush=True)
    bench = {}
    for tag, blend in (("studio", studio), ("p3_room", room)):
        b = ui_run(args.ours, out, "B5_" + tag, blend, env(), "bench" if tag == "studio" else "bench_view", wait=40)
        bench[tag] = (b["steps"].get("bench") or {}).get("bench") or {}
        bench[tag]["exit"] = b["exit"]
    res["B5"] = bench
    view_fps = [p2.fps(bench[t].get("orbit_ms_median")) for t in bench]
    res["gate_view_60fps"] = all(f is not None and f >= 60 for f in view_fps)

    print("B5.4, B5.5: F12 at 1920 x 1080 and the animation's time per frame...", flush=True)
    full = {"render.resolution_x": 1920, "render.resolution_y": 1080, "render.resolution_percentage": 100}
    b54 = {}
    for tag, extra in (("realtime_32", {"bge_remix_f12_frames": 32}),
                       ("reference_256", {"bge_remix_f12_mode": 1, "bge_remix_f12_samples": 256}),
                       ("reference_1024", {"bge_remix_f12_mode": 1, "bge_remix_f12_samples": 1024})):
        r = f12(args.ours, out, "B5.4_" + tag, studio, env(), settings=dict(full, **extra))
        b54[tag] = {"seconds": r["seconds"], "ok": r["exit"] == 0 and len(r["files"]) == 1}
    c1080 = cycles(args.ours, out, studio, "B5.4_cycles", samples=128, size=(1920, 1080))
    b54["cycles_cpu_128"] = {"seconds": c1080["seconds"], "ok": c1080["ok"]}
    res["B5.4"] = b54
    res["B5.5"] = {"seconds_per_frame": round(an["seconds"] / max(len(an["files"]), 1), 2), "frames": len(an["files"])}

    keys = ["T5.0", "T5.1", "T5.2", "T5.3", "T5.4", "T5.5", "T5.6", "T5.7"]
    res["all_ok"] = all(res[k]["ok"] for k in keys)
    names = {"T5.0": "Quality settings", "T5.1": "Editing in a Rendered view", "T5.2": "Select by click",
             "T5.3": "F12 still and animation", "T5.4": "Same picture as the game", "T5.5": "Game from a Remix view",
             "T5.6": "Sizes and views", "T5.7": "OpenGL state and UI"}
    lines = ["BGE-Remix phase 5 results (RTX)", "=" * 31, ""]
    for k in keys:
        note = res[k].get("compare")
        lines.append("%s %-28s %s%s" % (k, names[k], p2.mark(res[k]["ok"]),
                                         ("   (look at %s)" % os.path.basename(note)) if note else ""))
        for name, ok in (res[k].get("checks") or {}).items():
            if not ok:
                lines.append("     failed: " + name)
    if res["T5.4"].get("mean_difference") is not None:
        lines.append("     T5.4 mean difference %.2f (under 12 passes)" % res["T5.4"]["mean_difference"])
    lines += ["", "Benchmarks (your RTX 3080)", "-" * 26]
    for t, b in bench.items():
        lines.append("B5.1 view %-9s %s x %s: median %s ms = %s FPS while orbiting" % (
            t, b.get("width"), b.get("height"), p2._r(b.get("orbit_ms_median")), p2.fps(b.get("orbit_ms_median"))))
    lines.append("     target (60 FPS): %s" % ("met" if res["gate_view_60fps"] else "NOT met"))
    sb = bench.get("studio", {})
    for k, label in (("move_ms", "move"), ("colour_ms", "colour"), ("edit_ms", "ball edit"),
                     ("subdivision_ms", "subdivision 63,000 triangles")):
        lines.append("B5.2 latency %-30s %s ms" % (label, p2._r(sb.get(k))))
    lines.append("B5.3 Remix frames drawn in 5 s without input: %s" % sb.get("idle_frames"))
    lines.append("B5.6 memory after Rendered on/off 20 times: video %s MB, system %s MB more" % (
        sb.get("vram_growth_mb"), sb.get("ram_growth_mb")))
    each = sb.get("vram_each_mb") or []
    if each:
        lines.append("     video memory after each time (MB more): %s" % ", ".join(str(v) for v in each))
    for tag, b in b54.items():
        lines.append("B5.4 F12 1920x1080 %-16s %s s%s" % (tag, b["seconds"], "" if b["ok"] else "  (failed)"))
    lines.append("B5.5 F12 animation: %s s per frame (%d frames, Real-time 16)" % (res["B5.5"]["seconds_per_frame"],
                                                                               res["B5.5"]["frames"]))
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p5_rtx.json and the .log files)"]
    return res, lines


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
        name = "results_p5_rtx"
    else:
        if not args.mock:
            raise SystemExit("--mock is needed without --rtx")
        res, lines = run_ci(args, out)
        name = "results_p5"
    with open(os.path.join(out, name + ".json"), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, name.replace("results", "summary") + ".txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
