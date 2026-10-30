"""BGE-Remix P8 tests, Parts A (materials) and B (lamps and shadows).

T8.1  one mode: no mode switch in the UI; new and old files play as GLSL;
      saved files open in stock 2.79 as GLSL
T8.2  Multitexture's features: mt_features in P8 looks like mt_features in
      stock 2.79 Multitexture, feature by feature (lighting excepted), and
      each scripted feature works
T8.3  GLSL unchanged: glsl_features in P8 matches stock 2.79 GLSL; Classic
      materials (pbr_classic) look as before
T8.4  fallback: the failing material draws with the fixed-function path, is
      reported in the console and the Game View; its neighbour is unaffected
T8.5  3D view: Textured mode of each test scene matches the game's picture
T8.6  Python: the old calls run, every lamp setting can be changed
T8.7  procedurals in GLSL: each panel of proc_textures in the game matches
      F12 (stock 2.79), the bumped row too; Noise only for its look
T8.8  procedurals baked: each baked image matches Blender's own bake; a
      second start reuses the files; blenderplayer and Blender's 3D view
      bake; the animated texture is baked again; the panel without a UV
      map is reported
T8.9  one physical material: pbr_spheres against its reference pictures;
      every value reaches Remix exactly (the fake runtime's log); F12 shows
      the translated values; the file opens in stock 2.79
T8.10 Classic into Remix: each classic_remix material reaches Remix as the
      right kind; the invisible material is hidden
T8.11 lights: each lamp of the lamps scene against F12 (our build), area
      lamps included; the light group, the spot halo, a lamp a script
      changes; Remix gets each lamp's size, shape and power (the fake
      runtime's log); Only Shadow lamps are not sent
T8.12 shadows: every lamp type casts; edges soften with the light's size;
      no acne under grazing light; a cut-out fence's holes show in its
      shadow (Simple and Variance); the far sun shadow is there; static
      lamps stop drawing their shadow; contact shadows where objects touch
T8.20 regression: P0-P7 unchanged (their results from the same CI run;
      P7's own tests are the Game View's, built and tested with P8)
B8.1-B8.5, B8.7 reported (their targets are for the RTX run at the end of P8)

  python run_p8.py --ours <build> --official <stock 2.79b> --mock <fake runtime> --out <dir>   (CI)
"""

import argparse
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p3"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p7"))
import run_p2 as p2  # noqa: E402  (running Blender, the fake runtime's log, contact sheets)
import run_p3 as p3  # noqa: E402  (materials in the fake runtime's log)
import run_p7 as p7  # noqa: E402  (the Game View probe)

UI_PROBE = os.path.join(HERE, "p8_ui_probe.py")
REFERENCE = os.path.join(HERE, "reference")
SIZE = (960, 540)  # as make_scenes_p8.py: fits the CI machines' 1024x768 screen
WINDOW = ["-p", "0", "0", "1600", "900"]
BAKE_TEST_SIZE = 256
GLSL_CONSTANT = 2  # bge.render.KX_BLENDER_GLSL_MATERIAL

# picture tolerances: the mean difference of a region, 0-255 per channel
TOL_SAME_CODE = 3.0     # the same GLSL code in both builds
TOL_F12 = 6.0           # the game's GLSL against Blender Internal's F12
TOL_F12_BUMP = 10.0     # bumped and lit panels against F12
TOL_MULTITEX = 12.0     # GLSL against Multitexture's fixed-function drawing
TOL_VIEW = 8.0          # the 3D view against the game
TOL_BAKE = 8.0          # a baked image against Blender's own bake
TOL_BAKE_NORMAL = 16.0  # a baked normal map against Blender's
TOL_LAMP = 10.0         # a lamp's tile: mean brightness, game against F12
TOL_LAMP_SHAPE = 14.0   # a lamp's tile: its light's shape (aligned difference)

# ---------------------------------------------------------------------------
# scripts run inside Blender (Python 3.5)

UI_CHECK = r'''
import bpy, json, os
import bl_ui
folder = os.path.dirname(bl_ui.__file__)
found = []
for name in sorted(os.listdir(folder)):
    if not name.endswith(".py"):
        continue
    with open(os.path.join(folder, name), encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            if "material_mode" in line and ".prop(" in line and not line.strip().startswith("#"):
                found.append("%s:%d" % (name, n))
sc = bpy.data.scenes.new("P8New")
r = {"switches": found, "new_scene": sc.game_settings.material_mode,
     "new_scene_color_management": sc.game_settings.use_glsl_color_management}
sc.game_settings.material_mode = 'MULTITEXTURE'
r["after_setting_multitexture"] = sc.game_settings.material_mode
print("P8-UI " + json.dumps(r))
'''

OPEN_SAVE = r'''
import bpy, json, sys
out = sys.argv[sys.argv.index("--") + 1]
sc = bpy.context.scene
r = {"mode": sc.game_settings.material_mode, "color_management": sc.game_settings.use_glsl_color_management,
     "object_color": {ob.name: bool(ob.active_material and ob.active_material.use_object_color)
                      for ob in sc.objects if ob.name.startswith("ObjCol")}}
bpy.ops.wm.save_as_mainfile(filepath=out, copy=True)
print("P8-OPEN " + json.dumps(r))
'''

STOCK_MODE = r'''
import bpy, json
print("P8-STOCK " + json.dumps({"mode": bpy.context.scene.game_settings.material_mode,
                                "objects": len(bpy.data.objects)}))
'''

STOCK_OPEN = r'''
import bpy, json
ma = bpy.data.materials.get("S_m4_r0")
print("P8-STOCKOPEN " + json.dumps({"materials": len(bpy.data.materials),
                                    "props": sorted(k for k in ma.keys() if k.startswith("bge_")) if ma else []}))
'''

# the GL copies of the Remix scenes (their pictures are OpenGL's), the bump
# row left out (B8.3), mt_bench in physical shading (B8.5)
VARIANTS = r'''
import bpy, os, sys
folder = sys.argv[sys.argv.index("--") + 1]
def save(name):
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, name + ".blend"), copy=True)
for name in ("proc_textures", "pbr_spheres", "pbr_classic", "classic_remix"):
    path = os.path.join(folder, name + ".blend")
    if not os.path.isfile(path):
        continue
    bpy.ops.wm.open_mainfile(filepath=path)
    for sc in bpy.data.scenes:
        sc["bge_remix"] = 0
    save(name + "_gl")
    if name == "proc_textures":
        for ob in list(bpy.data.objects):
            if ob.name.startswith("P_bump_"):
                bpy.data.objects.remove(ob, do_unlink=True)
        save("proc_textures_gl_nobump")
path = os.path.join(folder, "mt_bench.blend")
if os.path.isfile(path):
    bpy.ops.wm.open_mainfile(filepath=path)
    for ma in bpy.data.materials:
        ma["bge_physical"] = 1
    save("mt_bench_physical")
print("P8-VARIANTS done")
'''

RENDER = r'''
import bpy, json, os, sys
args = sys.argv[sys.argv.index("--") + 1:]
out, watch = args[0], args[1:]
sc = bpy.context.scene
def snap(ma):
    return {"diffuse_intensity": round(ma.diffuse_intensity, 3), "specular_intensity": round(ma.specular_intensity, 3),
            "specular_shader": ma.specular_shader, "hardness": ma.specular_hardness,
            "mirror": bool(ma.raytrace_mirror.use), "reflect": round(ma.raytrace_mirror.reflect_factor, 3),
            "fresnel": round(ma.raytrace_mirror.fresnel, 3), "gloss": round(ma.raytrace_mirror.gloss_factor, 3),
            "transparency": bool(ma.use_transparency), "method": ma.transparency_method,
            "ior": round(ma.raytrace_transparency.ior, 3), "sss": bool(ma.subsurface_scattering.use),
            "emit": round(ma.emit, 3)}
before = {n: snap(bpy.data.materials[n]) for n in watch if n in bpy.data.materials}
seen = {}
def stats(*_args):
    for n in before:
        s = snap(bpy.data.materials[n])
        if s != before[n]:
            seen[n] = s
bpy.app.handlers.render_stats.append(stats)
sc.render.filepath = out
sc.render.use_file_extension = False
sc.render.image_settings.file_format = 'PNG'
bpy.ops.render.render(write_still=True)
after = {n: snap(bpy.data.materials[n]) for n in before}
print("P8-RENDER " + json.dumps({"before": before, "during": seen, "after": after, "written": os.path.isfile(out)}))
'''

# Blender Internal's own bake of each UV-mapped panel: the colour (Texture)
# of the colour panels, the tangent-space normal of the bumped ones
BI_BAKE = r'''
import bpy, json, os, sys
args = sys.argv[sys.argv.index("--") + 1:]
out, size = args[0], int(args[1])
sc = bpy.context.scene
r = sc.render
r.bake_margin = 0
r.use_bake_clear = True
r.use_bake_selected_to_active = False
done = []
panels = [ob for ob in sc.objects if ob.type == 'MESH' and ob.name.startswith("P_") and len(ob.data.uv_textures)]
for ob in panels:
    img = bpy.data.images.new("bake_" + ob.name, size, size)
    for d in ob.data.uv_textures.active.data:
        d.image = img
for ob in panels:
    for o in sc.objects:
        o.select = False
    ob.select = True
    sc.objects.active = ob
    if ob.name.startswith("P_bump_"):
        r.bake_type = 'NORMALS'
        r.bake_normal_space = 'TANGENT'
    else:
        r.bake_type = 'TEXTURE'
    bpy.ops.object.bake_image()
    img = bpy.data.images["bake_" + ob.name]
    img.filepath_raw = os.path.join(out, ob.name + ".png")
    img.file_format = 'PNG'
    img.save()
    done.append(ob.name)
print("P8-BAKE " + json.dumps(done))
'''


# ---------------------------------------------------------------------------
# running

def blender_script(build, out, tag, body, args=(), blend=None, timeout=900):
    script = os.path.join(out, "p8_%s.py" % tag)
    with open(script, "w", encoding="utf-8") as f:
        f.write(body)
    cmd = [p2.exe(build, "blender"), "--background"]
    cmd += [blend] if blend else ["--factory-startup"]
    cmd += ["--python", script, "--"] + [str(a) for a in args]
    code, text, secs = p2.run(cmd, p2.base_env(out, tag), os.path.join(out, tag + ".log"), timeout=timeout)
    return code, text, secs


def make_scenes(args, out):
    scenes = os.path.join(out, "scenes")
    shutil.rmtree(scenes, ignore_errors=True)
    os.makedirs(scenes, exist_ok=True)
    maker = os.path.join(HERE, "make_scenes_p8.py")
    code, text, _ = p2.run([p2.exe(args.ours, "blender"), "--background", "--factory-startup",
                            "--python", maker, "--", scenes],
                           p2.base_env(out, "make"), os.path.join(out, "make_scenes_p8.log"), timeout=1800)
    expected = p2.read_json(os.path.join(scenes, "p8_expected.json"))
    if code != 0 or not expected:
        print("\n".join(text.splitlines()[-40:]))
        raise SystemExit("making the P8 scenes failed, see make_scenes_p8.log")
    if args.official:
        code, text, _ = p2.run([p2.exe(args.official, "blender"), "--background", "--factory-startup",
                                "--python", maker, "--", scenes, "--stock"],
                               p2.base_env(out, "make_stock"), os.path.join(out, "make_scenes_p8_stock.log"),
                               timeout=1800)
        stock = p2.read_json(os.path.join(scenes, "p8_expected_stock.json"))
        if code != 0 or not stock:
            print("\n".join(text.splitlines()[-40:]))
            print("making the stock P8 scenes failed, see make_scenes_p8_stock.log")
        expected["stock"] = stock
        expected["failed"].update({"stock " + k: v for k, v in stock.get("failed", {}).items()})
    for name, tb in expected.get("failed", {}).items():
        print("making the %s scene failed:\n%s" % (name, tb))
    blender_script(args.ours, out, "variants", VARIANTS, [scenes])
    return scenes, expected


# Windows' console debugger (the Windows SDK's), when the machine has it: a
# crash then prints where it happened (module + offset, read with the
# linker's map, blenderplayer.map)
CDB = r"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
GFLAGS = os.path.join(os.path.dirname(CDB), "gflags.exe")
ON_CRASH = ".echo BGE-Remix crash; .lastevent; r; kn 60; lm; q"
CRASH_LINE = re.compile(r"^BGE-Remix crash\s*$", re.M)  # the echo, not the command line that holds it


def play(build, out, tag, blend, env=None, frames=300, seconds=0, debug=False):
    """blenderplayer at SIZE (960x540); the probe's screenshot at frame 100;
    debug: under the console debugger (when there is one), with Windows'
    page heap (each allocation followed by a guard page, so a write past its
    end stops right there) and Blender's own allocator in place of mimalloc"""
    res, stats, shot = (os.path.join(out, tag + x) for x in (".json", "_stats.json", ".png"))
    for p in (res, stats, shot):
        if os.path.isfile(p):
            os.remove(p)
    e = p2.base_env(out, tag)
    e.update({"BGE_REMIX_P2_OUT": res, "BGE_REMIX_STATS": stats, "BGE_REMIX_P2_SHOT": shot,
              "BGE_REMIX_P2_FRAMES": str(frames), "BGE_REMIX_P2_SECONDS": str(seconds), "BGE_REMIX_RUNTIME_DIR": None})
    e.update(env or {})
    cmd = [p2.exe(build, "blenderplayer"), "-w", str(SIZE[0]), str(SIZE[1]), blend]
    pageheap = False
    if debug and os.path.isfile(CDB):
        handlers = "; ".join('sxe -c "%s" %s' % (ON_CRASH, ev) for ev in ("av", "c0000374", "c0000409"))
        cmd = [CDB, "-G", "-sins", "-c", handlers + "; g"] + cmd
        e["BGE_REMIX_NO_MIMALLOC"] = "1"
        if os.path.isfile(GFLAGS):
            pageheap = p2.run([GFLAGS, "/p", "/enable", "blenderplayer.exe", "/full"], {},
                              os.path.join(out, tag + "_gflags.log"), timeout=60)[0] == 0
    try:
        code, text, secs = p2.run(cmd, e, os.path.join(out, tag + ".log"), timeout=1800,
                                  cwd=os.path.dirname(blend))
    finally:
        if pageheap:
            p2.run([GFLAGS, "/p", "/disable", "blenderplayer.exe"], {}, os.path.join(out, tag + "_gflags_off.log"),
                   timeout=60)
    r = {"exit": code, "seconds_total": round(secs, 1), "probe": p2.read_json(res), "stats": p2.read_json(stats),
         "screenshot": shot if os.path.isfile(shot) else None, "traceback_in_output": "Traceback" in text,
         "text": text, "debugger": cmd[0] == CDB, "crashed": bool(CRASH_LINE.search(text)), "pageheap": pageheap}
    r["frames"] = r["probe"].get("frames", 0)
    r["ran"] = r["exit"] == 0 and r["frames"] >= frames and not r["traceback_in_output"] and not r["crashed"]
    return r


def log_text(out, tag):
    path = os.path.join(out, tag + ".log")
    if not os.path.isfile(path):
        return ""
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


def mock_env(args, out, tag, cache, size=BAKE_TEST_SIZE, dump=None):
    log = os.path.join(out, tag + "_mock.log")
    if os.path.isfile(log):
        os.remove(log)
    env = {"BGE_REMIX_RUNTIME_DIR": os.path.abspath(args.mock), "BGE_REMIX_MOCK_MODE": "ok",
           "BGE_REMIX_MOCK_LOG": log, "BGE_REMIX_CACHE_DIR": cache, "BGE_REMIX_TEXTURE_NO_GPU": "1",
           "BGE_REMIX_BAKE_SIZE": str(size), "BGE_REMIX_BAKE_DUMP": dump}
    return log, env


def ui_probe(build, out, tag, blend, env, timeout=900):
    done = os.path.join(out, tag + "_ui.json")
    if os.path.isfile(done):
        os.remove(done)
    e = p2.base_env(out, tag)
    e.update({"BGE_REMIX_P8_DONE": done})
    e.update(env)
    # without Blender's crash handler, so a crash leaves a dump (CI)
    code, text, secs = p2.run([p2.exe(build, "blender"), "--disable-crash-handler"] + WINDOW +
                              [blend, "--python", UI_PROBE], e,
                              os.path.join(out, tag + ".log"), timeout=timeout, cwd=out)
    r = p2.read_json(done)
    r.update({"exit": code, "seconds": round(secs, 1), "text": text})
    return r


# ---------------------------------------------------------------------------
# pictures

def picture(path):
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None
    if not path or not os.path.isfile(path):
        return None
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)


def crop(a, rect):
    left, top, right, bottom = rect
    return a[top:bottom, left:right]


def region_diff(a, b, rect=None):
    if a is None or b is None or a.shape != b.shape:
        return None
    if rect:
        a, b = crop(a, rect), crop(b, rect)
    return round(float(abs(a - b).mean()), 2)


def blurred(a, radius=1.5):
    """a picture softened a little, so a sub-pixel difference in framing
    (OpenGL's pixel centres against Blender Internal's) counts less"""
    if a is None:
        return None
    from PIL import Image, ImageFilter
    import numpy as np
    im = Image.fromarray(a.clip(0, 255).astype("uint8")).filter(ImageFilter.GaussianBlur(radius))
    return np.asarray(im, dtype=np.float64)


def aligned_diff(a, b, rect, reach=2):
    """the region's mean difference at the best shift of up to `reach`
    pixels (a and b blurred first)"""
    if a is None or b is None or a.shape != b.shape:
        return None
    left, top, right, bottom = rect
    h, w = a.shape[:2]
    best = None
    for dy in range(-reach, reach + 1):
        for dx in range(-reach, reach + 1):
            if top + dy < 0 or left + dx < 0 or bottom + dy > h or right + dx > w:
                continue
            d = float(abs(a[top:bottom, left:right] - b[top + dy:bottom + dy, left + dx:right + dx]).mean())
            best = d if best is None else min(best, d)
    return None if best is None else round(best, 2)


def region_look(a, rect):
    """mean and spread (standard deviation) of a region's brightness"""
    if a is None:
        return None
    g = crop(a, rect).mean(axis=2)
    return [round(float(g.mean()), 1), round(float(g.std()), 1)]


def compare_rects(a, b, rects, tol, prefix="", aligned=False):
    """{name: difference} and a check per region (aligned: pictures from two
    different renderers, see aligned_diff)"""
    c, d = {}, {}
    if aligned:
        a, b = blurred(a), blurred(b)
    for name, rect in rects.items():
        md = aligned_diff(a, b, rect) if aligned else region_diff(a, b, rect)
        d[name] = md
        c["%s%s within %.0f" % (prefix, name, tol)] = md is not None and md <= tol
    return c, d


def reference_check(out, name, path):
    """against tests/bge_remix/p8/reference/<name>.png once it is there"""
    img = picture(path)
    os.makedirs(os.path.join(out, "new_reference"), exist_ok=True)
    if img is None:
        return {"picture of %s made" % name: False}, {}
    shutil.copy2(path, os.path.join(out, "new_reference", name + ".png"))
    ref = picture(os.path.join(REFERENCE, name + ".png"))
    if ref is None:
        return {"picture of %s made" % name: True}, {name: "no reference picture yet (this run's is in new_reference)"}
    md = region_diff(img, ref)
    return {"%s matches its reference" % name: md is not None and md < 4.0}, {name: md}


def tagged(text, tag):
    return p3.tagged_json(text, tag) or {}


def check_lines(text, code):
    """the Game View's checks as the game prints them at its start"""
    out = []
    for line in text.splitlines():
        m = p7.CHECK_LINE.match(line.strip())
        if m and m.group(4) == code:
            out.append({"layer": m.group(1), "owner": m.group(2), "text": m.group(3)})
    return out


# ---------------------------------------------------------------------------
# the tests

def t81(args, out, scenes, mt_ours, api):
    c, d = {}, {}
    code, text, _ = blender_script(args.ours, out, "t81_ui", UI_CHECK)
    ui = tagged(text, "P8-UI")
    c["no material mode switch in the UI"] = bool(ui) and not ui.get("switches")
    c["a new scene is GLSL"] = ui.get("new_scene") == "GLSL"
    c["setting Multitexture from Python leaves GLSL"] = ui.get("after_setting_multitexture") == "GLSL"
    mt = os.path.join(scenes, "mt_features.blend")
    copy = os.path.join(out, "t81_saved_by_ours.blend")
    code, text, _ = blender_script(args.ours, out, "t81_open", OPEN_SAVE, [copy], blend=mt)
    opened = tagged(text, "P8-OPEN")
    c["an old Multitexture file opens as GLSL"] = opened.get("mode") == "GLSL"
    # stock 2.79 coloured a material by its object only with Object Color
    # ticked, in Multitexture too (its picture of mt_features), so it stays off
    c["a new scene keeps GLSL Color Management on"] = ui.get("new_scene_color_management") is True
    c["the old Multitexture file gets GLSL Color Management off (colours as Multitexture drew them)"] = \
        opened.get("color_management") is False
    c["Object Color left as the file had it"] = bool(opened.get("object_color")) and \
        not any(opened["object_color"].values())
    c["the old file plays as GLSL"] = mt_ours.get("mt", {}).get("mode") == GLSL_CONSTANT
    c["a new file plays as GLSL"] = api.get("getMaterialMode", {}).get("value") == GLSL_CONSTANT
    code, text, _ = blender_script(args.official, out, "t81_stock", STOCK_MODE, blend=copy)
    stock = tagged(text, "P8-STOCK")
    c["the saved file opens in stock 2.79 as GLSL"] = code == 0 and stock.get("mode") == "GLSL"
    d.update({"ui": ui, "opened": opened, "stock": stock})
    return c, d


def t82(expected, mt_ours, mt_stock):
    rects = dict(expected.get("stock", {}).get("mt_features", {}))
    lit = {k: v for k, v in rects.items() if k.startswith("lit_")}
    # setBlending: stock 2.79's own picture shows no visible effect without a
    # custom shader; ours applies it (P8.2), so it is checked for working
    # the texture mapped to alpha: Multitexture applied its alpha twice (the
    # ramp shows as alpha squared in stock 2.79's picture); ours blends as F12
    # does, so it is checked against that blend
    feats = {k: v for k, v in rects.items() if k not in lit and k not in ("setblending", "alpha_texture")}
    a, b = picture(mt_ours.get("shot_b")), picture(mt_stock.get("shot_b"))
    # fixed-function and GLSL drawing: compared as two renderers (text's
    # one-pixel glyph cells differ in filtering)
    c, d = compare_rects(a, b, feats, TOL_MULTITEX, aligned=True)
    if a is not None and "alpha_texture" in rects:
        left, top, right, bottom = rects["alpha_texture"]
        row = a[(top + bottom) // 2, left:right]
        back = [0.1 * 255, 0.4 * 255, 0.8 * 255]   # MT_alpha_back, colours as they are
        tex = [0.9 * 255, 0.9 * 255, 0.2 * 255]    # MT_gradient's colour; its alpha runs 0 to 1 across
        n = len(row)
        errs = []
        for j in range(n):
            u = 0.1 + 0.8 * j / max(1, n - 1)    # the rectangle is the plane's inner 80%
            want = [bk * (1.0 - u) + tx * u for bk, tx in zip(back, tex)]
            errs.append(sum(abs(float(row[j][k]) - want[k]) for k in range(3)) / 3.0)
        err = round(sum(errs) / len(errs), 2)
        c["the texture's alpha blends to the plane behind, as F12 does, within 6"] = err <= 6.0
        d["alpha_texture"] = {"mean_difference_from_straight_alpha": err}
    blend = None
    if a is not None and "setblending" in rects:
        left, top, right, bottom = rects["setblending"]
        blend = [round(float(v), 1) for v in a[top:bottom, left:right].reshape(-1, 3).mean(axis=0)]
    solid = [102.0, 51.0, 26.0]  # MT_blended's colour (0.4, 0.2, 0.1) drawn on its own
    c["setBlending (ONE, ONE) adds the card behind to the square's colour"] = blend is not None and \
        all(x - s > 30.0 for x, s in zip(blend, solid))
    d["setblending"] = {"ours": blend, "drawn_alone": solid}
    c["both pictures made"] = a is not None and b is not None
    m_o, m_s = mt_ours.get("mt", {}), mt_stock.get("mt", {})
    c["a lamp's type changed while playing"] = m_o.get("type_after") is not None and \
        m_o.get("type_after") != m_o.get("type_before") and m_o.get("type_after") == m_s.get("type_after")
    a0 = picture(mt_ours.get("shot_a"))
    md = region_diff(a0, a, lit.get("lit_2"))
    c["the changed lamp changes the picture"] = md is not None and md > 2.0
    c["played in ours and in stock 2.79"] = mt_ours.get("ran") and mt_stock.get("ran")
    d.update({"lamp": {"ours": m_o, "stock": m_s, "change": md}})
    return c, d


def t83(expected, shots):
    c, d = {}, {}
    rects = expected.get("glsl_features", {})
    a, b = picture(shots.get("glsl_features")), picture(shots.get("stock_glsl_features"))
    cc, dd = compare_rects(a, b, rects, TOL_SAME_CODE, "glsl_features: ")
    c.update(cc)
    whole = region_diff(a, b)
    c["glsl_features: the whole picture within %.0f" % TOL_SAME_CODE] = whole is not None and whole <= TOL_SAME_CODE
    d["glsl_features"] = dict(dd, whole=whole)
    a, b = picture(shots.get("pbr_classic_gl")), picture(shots.get("stock_pbr_classic"))
    rects = {s["name"]: s["rect"] for s in expected.get("pbr_classic", {}).get("spheres", []) +
             expected.get("pbr_classic", {}).get("extras", [])}
    cc, dd = compare_rects(a, b, rects, TOL_SAME_CODE)
    bad = [k for k, v in cc.items() if not v]
    c["pbr_classic: every Classic sphere as in stock 2.79"] = bool(cc) and not bad
    d["pbr_classic"] = dd
    return c, d


def t84(args, out, scenes, expected, normal_run):
    blend = os.path.join(scenes, "fallback.blend")
    r = play(args.ours, out, "t84_fail", blend, {"BGE_REMIX_GLSL_FAIL": "FailMe"})
    rects = expected.get("fallback", {})
    a, n = picture(r.get("screenshot")), picture(normal_run.get("screenshot"))
    fail_rect, other = rects.get("FailMe"), rects.get("Neighbour")
    console = [l for l in r["text"].splitlines() if l.startswith('BGE-Remix: material "FailMe": its GLSL shader')]
    report = check_lines(r["text"], "REN_GLSL_FALLBACK")
    gv = p7.ui_run(args.ours, out, "t84_gameview", blend, "model", {"BGE_REMIX_GLSL_FAIL": "FailMe"})
    ws = [w for w in p7.active_warnings(p7.model_of(gv)) if w["code"] == "REN_GLSL_FALLBACK"]
    look = region_look(a, fail_rect) if a is not None else None
    same = None
    if a is not None:
        x, y = crop(a, fail_rect), crop(a, other)
        h, w = min(x.shape[0], y.shape[0]), min(x.shape[1], y.shape[1])
        same = region_diff(x[:h, :w], y[:h, :w])
    c = {
        "the game ran": r["ran"],
        "reported once in the console": len(console) == 1,
        "reported in the game's checks": len(report) == 1,
        "reported in the Game View, on FailMe": len(ws) == 1 and ws[0]["name"] == "FailMe",
        "the failing material is drawn with its texture (fixed-function)": look is not None and look[1] > 20.0,
        "it looks like its neighbour": same is not None and same <= TOL_F12,
        "the neighbour is unaffected": (lambda v: v is not None and v <= 2.0)(region_diff(a, n, other)),
    }
    return c, {"console": console, "report": report, "game_view": ws, "look": look, "same": same}


def t85(args, out, scenes, expected, shots):
    """each scene's camera view in Textured mode against the game's picture"""
    stock = expected.get("stock", {})
    # the script's changes (setBlending, the lamp that becomes a sun and
    # lights every sphere) are not in the 3D view
    # (and the stand-in bitmap font is no real Blender font: the 3D view
    # writes its text, the game shows the font image)
    mt_rects = {k: v for k, v in stock.get("mt_features", {}).items()
                if k not in ("setblending", "text") and not k.startswith("lit_")}
    sets = {
        # (Noise is random: checked for its look in T8.7 only)
        "proc_textures_gl": {p["name"]: p["rect"] for p in expected["proc_textures"]["panels"] +
                             expected["proc_textures"]["bumps"] if not p.get("noise")},
        "pbr_spheres_gl": {s["name"]: s["rect"] for s in expected["pbr_spheres"]["spheres"] +
                           expected["pbr_spheres"]["extras"]},
        "pbr_classic_gl": {s["name"]: s["rect"] for s in expected["pbr_classic"]["spheres"] +
                           expected["pbr_classic"]["extras"]},
        "classic_remix_gl": {e["name"]: e["rect"] for e in expected["classic_remix"]},
        "glsl_features": expected["glsl_features"],
        "fallback": expected["fallback"],
        "mt_features": mt_rects,
    }
    c, d = {}, {}
    for name, rects in sets.items():
        blend = os.path.join(scenes, name + ".blend")
        if not os.path.isfile(blend):
            c[name + ": scene made"] = False
            continue
        view = os.path.join(out, "t85_view_%s.png" % name)
        r = ui_probe(args.ours, out, "t85_" + name, blend, {"BGE_REMIX_P8_VIEW": view})
        a, b = picture(view), picture(shots.get(name))
        cc, dd = compare_rects(a, b, rects, TOL_VIEW, aligned=True)
        bad = sorted(k for k, v in dd.items() if v is None or v > TOL_VIEW)
        c["%s: the 3D view matches the game" % name] = a is not None and b is not None and not bad
        d[name] = {"differences": dd, "outside": bad, "probe_errors": r.get("errors"), "view": r.get("view")}
    return c, d


def t86(api):
    c = {name: bool(v.get("ok")) for name, v in api.items()}
    c["the probe ran"] = bool(api)
    if api:
        c["getMaterialMode gives GLSL, before and after"] = \
            api.get("getMaterialMode", {}).get("value") == GLSL_CONSTANT and \
            api.get("getMaterialMode after", {}).get("value") == GLSL_CONSTANT
        got = api.get("lamp settings", {}).get("value") or []
        c["every lamp setting changed"] = len(got) == 6 and abs(got[0] - 2.0) < 1e-3 and \
            p2.close(got[1], [1.0, 0.5, 0.5]) and abs(got[2] - 20.0) < 1e-3 and abs(got[3] - 50.0) < 0.5 and \
            abs(got[4] - 0.3) < 1e-3
    return c, api


def t87(args, out, scenes, expected, shots):
    if not args.official:
        return {"official 2.79b given (--official)": False}, {}
    f12 = os.path.join(out, "t87_f12_proc_textures.png")
    code, text, _ = blender_script(args.official, out, "t87_f12", RENDER, [f12],
                                   blend=os.path.join(scenes, "proc_textures_gl.blend"), timeout=1800)
    a, b = picture(shots.get("proc_textures_gl")), picture(f12)
    c = {"F12 rendered by stock 2.79": b is not None, "the game's picture made": a is not None}
    sa, sb = blurred(a), blurred(b)
    d = {"panels": {}, "bumps": {}, "noise": None}
    for p in expected["proc_textures"]["panels"]:
        if p["noise"]:
            la, lb = region_look(a, p["rect"]), region_look(b, p["rect"])
            ok = la is not None and lb is not None and abs(la[0] - lb[0]) < 15 and abs(la[1] - lb[1]) < 15
            c["noise: its look (brightness and spread)"] = ok
            d["noise"] = {"game": la, "f12": lb}
            continue
        md = aligned_diff(sa, sb, p["rect"])
        d["panels"][p["name"]] = md
        c["%s within %.0f" % (p["name"], TOL_F12)] = md is not None and md <= TOL_F12
    for p in expected["proc_textures"]["bumps"]:
        md = aligned_diff(sa, sb, p["rect"])
        d["bumps"][p["name"]] = md
        c["%s (bumped, lit) within %.0f" % (p["name"], TOL_F12_BUMP)] = md is not None and md <= TOL_F12_BUMP
    d["f12"] = f12
    return c, d


def baked_hash(name, mesh):
    return p2.fnv1a("material:MA%s@%s" % (name, mesh))


def t88(args, out, scenes, expected):
    c, d = {}, {}
    blend = os.path.join(scenes, "proc_textures.blend")
    cache = p3.fresh(os.path.join(out, "cache_proc"))
    dump = p3.fresh(os.path.join(out, "bake_dump"))
    log, env = mock_env(args, out, "t88_first", cache, dump=dump)
    env["BGE_REMIX_TRACE"] = "1"  # each step of a bake, to find a crash
    first = play(args.ours, out, "t88_first", blend, env, frames=300, seconds=15)
    mats = p3.material_lines(p2.read_lines(log))
    names = [p["name"] for p in expected["proc_textures"]["panels"]]
    bumps = [p["name"] for p in expected["proc_textures"]["bumps"]]
    st = first["stats"]
    c["blenderplayer ran (fake runtime)"] = first["ran"]
    c["blenderplayer baked every material"] = st.get("procedural_bakes") == len(names) + len(bumps)
    missing = []
    for n in names + bumps:
        f = mats.get(baked_hash("M_" + n, "P_" + n))
        slot = "normal_tex" if n in bumps else "albedo_tex"
        if not f or ("MAM_%s_P_%s_" % (n, n)) not in str(f.get(slot, "")):
            missing.append(n)
    c["each material reached Remix with its baked texture"] = not missing
    d["without their baked texture"] = missing
    c["every baked texture encoded, none failed"] = st.get("textures_waiting") == 0 and st.get("textures_failed") == 0

    # against Blender's own bake
    bi = p3.fresh(os.path.join(out, "bi_bake"))
    if args.official:
        code, text, _ = blender_script(args.official, out, "t88_bi_bake", BI_BAKE, [bi, BAKE_TEST_SIZE],
                                       blend=os.path.join(scenes, "proc_textures_gl.blend"), timeout=1800)
    diffs, bad = {}, []
    for n in names + bumps:
        if n == "noise":
            continue
        ours = picture(os.path.join(dump, "MAM_%s_P_%s_%s.ppm" % (n, n, "normal" if n in bumps else "albedo")))
        theirs = picture(os.path.join(bi, "P_%s.png" % n))
        md = aligned_diff(blurred(ours), blurred(theirs), [2, 2, BAKE_TEST_SIZE - 2, BAKE_TEST_SIZE - 2]) \
            if ours is not None and theirs is not None and ours.shape == theirs.shape else None
        diffs[n] = md
        tol = TOL_BAKE_NORMAL if n in bumps else TOL_BAKE
        if md is None or md > tol:
            bad.append(n)
    c["each baked image matches Blender's own bake"] = bool(args.official) and bool(diffs) and not bad
    d["bake_differences"] = diffs
    d["bake_outside"] = bad

    # a second start: the same files
    log2, env2 = mock_env(args, out, "t88_again", cache)
    again = play(args.ours, out, "t88_again", blend, env2, frames=300, seconds=15)
    s2 = again["stats"]
    c["a second start reuses the files (nothing encoded)"] = again["ran"] and s2.get("textures_encoded") == 0 and \
        (s2.get("textures_reused") or 0) >= len(names) + len(bumps)
    d["first"], d["again"] = st, s2
    d["exits"] = {"first": first["exit"], "first_crashed": first["crashed"], "again": again["exit"]}

    # the panel without a UV map
    reports = check_lines(first["text"], "RMX_BAKE_NO_UV")
    c["the panel without a UV map is reported (once)"] = len(reports) == 1 and "P_no_uv" in reports[0]["owner"]
    c["and left plain in Remix"] = 'MAM_no_uv" on mesh "P_no_uv"' in first["text"]
    d["no_uv"] = reports

    # Blender's 3D view: the animated texture, baked again
    anim = os.path.join(scenes, "proc_animated.blend")
    cache3 = p3.fresh(os.path.join(out, "cache_anim"))
    log3, env3 = mock_env(args, out, "t88_animated", cache3)
    env3["BGE_REMIX_TRACE"] = "1"
    stats3 = os.path.join(out, "t88_animated_stats.json")
    env3.update({"BGE_REMIX_P8_GAME": "1", "BGE_REMIX_STATS": stats3, "BGE_REMIX_P2_FRAMES": "200",
                 "BGE_REMIX_P2_SECONDS": "8", "BGE_REMIX_P2_OUT": os.path.join(out, "t88_animated_probe.json")})
    r = ui_probe(args.ours, out, "t88_animated", anim, env3)
    s3 = p2.read_json(stats3)
    lines = [l for l in p2.read_lines(log3) if l.startswith("CreateMaterial ")]
    h = "%x" % baked_hash("M_animated", "P_animated")
    textures = {p2.parse_fields(l).get("albedo_tex") for l in lines if p2.parse_fields(l).get("hash") == h}
    textures.discard("-")
    c["Blender's 3D view played it and baked"] = bool(r.get("game")) and (s3.get("procedural_bakes") or 0) >= 1
    c["the animated texture is baked again (twice)"] = (s3.get("procedural_rebakes") or 0) >= 2 and len(textures) >= 2
    d["animated"] = {"stats": s3, "textures": sorted(textures), "probe_errors": r.get("errors")}
    return c, d


def material_problems(f, e):
    """a CreateMaterial line against what is expected of it"""
    if e.get("absent"):
        return [] if f is None else ["arrives, though invisible"]
    if f is None:
        return ["does not arrive"]
    bad = []
    for key, want in e.items():
        if key in ("name", "material", "rect", "absent"):
            continue
        if key == "metallic_min":
            if not f.get("metallic", 0) >= want:
                bad.append("metallic %s, expected at least %s" % (f.get("metallic"), want))
            continue
        field = key
        got = f.get(field)
        if key in ("albedo", "transmittance", "emissive"):
            want = p2.srgb(list(want))
        if isinstance(want, str):
            ok = got == want
        else:
            ok = got is not None and p2.close(got, want, 2e-3)
        if not ok:
            bad.append("%s %s, expected %s" % (key, got, want))
    return bad


def t89(args, out, scenes, expected, shots):
    c, d = {}, {}
    cc, dd = reference_check(out, "pbr_spheres", shots.get("pbr_spheres_gl"))
    c.update(cc)
    d["reference"] = dd
    cache = p3.fresh(os.path.join(out, "cache_pbr"))
    log, env = mock_env(args, out, "t89_remix", cache)
    r = play(args.ours, out, "t89_remix", os.path.join(scenes, "pbr_spheres.blend"), env)
    mats = p3.material_lines(p2.read_lines(log))
    base = expected["pbr_spheres"]["base"]
    want = {s["material"]: {"type": "opaque", "albedo": base, "metallic": s["metallic"], "roughness": s["roughness"],
                            "opacity": 1.0}
            for s in expected["pbr_spheres"]["spheres"]}
    want["S_glass"] = {"type": "translucent", "ior": 1.5, "transmittance": [0.85, 0.95, 1.0]}
    want["S_subsurface"] = {"type": "opaque", "albedo": [0.9, 0.6, 0.5], "subsurface": 1.0,
                            "sss_radius": [1.0, 0.4, 0.2]}
    want["S_emissive"] = {"type": "opaque", "emissive": [1.0, 0.5, 0.1], "intensity": 2.0}
    problems = {}
    for name, e in want.items():
        bad = material_problems(p3.find_material(mats, name), e)
        if bad:
            problems[name] = bad
    c["the player ran (fake runtime)"] = r["ran"]
    c["every value reaches Remix exactly"] = not problems
    d["remix_problems"] = problems

    # F12 in ours: the translated values while rendering, put back after
    f12 = os.path.join(out, "t89_f12_pbr_spheres.png")
    watch = ["S_m0_r4", "S_m4_r0", "S_glass", "S_subsurface", "S_emissive"]
    code, text, _ = blender_script(args.ours, out, "t89_f12", RENDER, [f12] + watch,
                                   blend=os.path.join(scenes, "pbr_spheres_gl.blend"), timeout=2400)
    got = tagged(text, "P8-RENDER")
    du = got.get("during", {})
    metal, plastic = du.get("S_m4_r0", {}), du.get("S_m0_r4", {})
    glass, sss, emit = du.get("S_glass", {}), du.get("S_subsurface", {}), du.get("S_emissive", {})
    c["F12 rendered"] = bool(got.get("written"))
    c["F12: the metal sphere as a tinted mirror, no diffuse"] = metal.get("mirror") is True and \
        metal.get("diffuse_intensity", 1) < 0.01 and metal.get("reflect", 0) > 0.99 and metal.get("fresnel", 1) == 0.0 \
        and metal.get("specular_shader") == "COOKTORR"
    c["F12: the rough plastic sphere as full diffuse"] = abs(plastic.get("diffuse_intensity", 0) - 1.0) < 0.01
    c["F12: glass as raytraced transparency, IOR 1.5"] = glass.get("transparency") is True and \
        glass.get("method") == "RAYTRACE" and abs(glass.get("ior", 0) - 1.5) < 0.01
    c["F12: subsurface on"] = sss.get("sss") is True
    c["F12: emit 2"] = abs(emit.get("emit", 0) - 2.0) < 0.01
    c["F12: every value put back after"] = bool(got) and got.get("before") == got.get("after")
    d["f12"] = got

    if args.official:
        code, text, _ = blender_script(args.official, out, "t89_stock", STOCK_OPEN,
                                       blend=os.path.join(scenes, "pbr_spheres.blend"))
        so = tagged(text, "P8-STOCKOPEN")
        c["the file opens in stock 2.79 (its values kept)"] = code == 0 and so.get("materials", 0) >= 28 and \
            "bge_metallic" in so.get("props", [])
        d["stock"] = so
    else:
        c["official 2.79b given (--official)"] = False
    return c, d


def t820(out):
    """P0-P7 unchanged: the results each phase's runner wrote in this CI run
    (P0 in the results folder itself, P1-P7 in results/p1 ... results/p7)"""
    c, d = {}, {}
    root = os.path.dirname(out)
    paths = [("P0", os.path.join(root, "results.json"))]
    paths += [("P%d" % n, os.path.join(root, "p%d" % n, "results_p%d.json" % n)) for n in range(1, 8)]
    for name, path in paths:
        data = p2.read_json(path)
        d[name] = data.get("all_ok") if data else "not run in this run"
        c["%s unchanged (its tests ran and pass)" % name] = bool(data) and bool(data.get("all_ok"))
    return c, d


def t810(args, out, scenes, expected):
    cache = p3.fresh(os.path.join(out, "cache_classic"))
    log, env = mock_env(args, out, "t810_remix", cache)
    r = play(args.ours, out, "t810_remix", os.path.join(scenes, "classic_remix.blend"), env)
    lines = p2.read_lines(log)
    mats = p3.material_lines(lines)
    c, d = {"the player ran (fake runtime)": r["ran"]}, {}
    for e in expected["classic_remix"]:
        f = p3.find_material(mats, e["material"])
        bad = material_problems(f, e)
        c["%s reaches Remix as expected" % e["name"]] = not bad
        d[e["name"]] = {"got": f, "problems": bad}
    meshes = p2.created(lines, "CreateMesh")
    inv = meshes.get(p2.fnv1a("mesh:invisibleMesh"))
    c["the invisible sphere's faces are not sent"] = inv is None or inv.get("surfaces") == 0
    d["invisible_mesh"] = inv
    return c, d


# ---------------------------------------------------------------------------
# Part B: lamps and shadows

def brightness(a):
    return None if a is None else a.mean(axis=2)


def rect_mean(a, rect):
    return None if a is None else round(float(crop(a, rect).mean()), 1)


def remix_light_problems(f, e):
    """what differs between a light in the fake runtime's log and what the
    lamp should give (remix_light.cc's formulas, make_scenes_p8.remix_expect)"""
    if f is None:
        return ["not sent"]
    bad = []
    if f.get("type") != e["type"]:
        bad.append("type %s, not %s" % (f.get("type"), e["type"]))
        return bad
    if "radiance" in e and not p2.close(f.get("radiance"), e["radiance"], tol=2e-3):
        bad.append("radiance %s, not %s" % (f.get("radiance"), e["radiance"]))
    for key, field in (("radius", "radius"), ("radii", "radii"), ("size", "size"), ("angle", "angle"),
                       ("cone", "cone"), ("volumetric", "volumetric")):
        if key in e and not p2.close(f.get(field), e[key], tol=2e-3):
            bad.append("%s %s, not %s" % (field, f.get(field), e[key]))
    return bad


def t811(args, out, scenes, expected, shots):
    """lights: each lamp in the game against F12 (our build: physical
    falloff), area lamps included; the light group; the script's change;
    Remix gets each lamp's size, shape and power; Only Shadow lamps are not
    sent"""
    E = expected.get("lamps")
    if not E:
        return {"the lamps scene was made": False}, {}
    c, d = {}, {"cells": {}}
    f12 = os.path.join(out, "t811_f12_lamps.png")
    blender_script(args.ours, out, "t811_f12", RENDER, [f12], blend=os.path.join(scenes, "lamps_gl.blend"),
                   timeout=1800)
    a, b = picture(shots.get("lamps_gl")), picture(f12)
    c["F12 rendered"] = b is not None
    c["the game's picture made"] = a is not None
    if a is not None and b is not None:
        sa, sb = blurred(a), blurred(b)
        for name, cell in E["cells"].items():
            if name in E["f12_skip"]:
                continue
            ga, gb = rect_mean(a, cell["rect"]), rect_mean(b, cell["rect"])
            md = aligned_diff(sa, sb, cell["rect"])
            d["cells"][name] = {"game": ga, "f12": gb, "difference": md}
            c["%s: as bright as F12 (within %.0f)" % (name, TOL_LAMP)] = abs(ga - gb) <= TOL_LAMP
            c["%s: its light's shape as F12's (within %.0f)" % (name, TOL_LAMP_SHAPE)] = \
                md is not None and md <= TOL_LAMP_SHAPE
        # the light group: lit by its group's lamp (left) only
        l, t, r, btm = E["cells"]["light_group"]["rect"]
        mid = (l + r) // 2
        left, right = rect_mean(a, [l, t, mid, btm]), rect_mean(a, [mid, t, r, btm])
        d["light_group_halves"] = [left, right]
        c["light group: only the group's lamp lights its material"] = left > right + 10.0
        # the halo adds its cone over the light
        halo = rect_mean(a, E["cells"]["spot_halo"]["rect"])
        twin = rect_mean(a, E["cells"]["spot_no_halo"]["rect"])
        d["halo"] = {"with": halo, "without": twin}
        c["the spot halo shows in the game"] = halo > twin + 3.0
        # the lamp the script changed looks like the one it copied
        ra, rb = E["cells"]["changed_by_script"]["rect"], E["cells"]["point_physical_small"]["rect"]
        w, h = min(ra[2] - ra[0], rb[2] - rb[0]), min(ra[3] - ra[1], rb[3] - rb[1])
        ca = a[ra[1]:ra[1] + h, ra[0]:ra[0] + w]
        cb = a[rb[1]:rb[1] + h, rb[0]:rb[0] + w]
        md = round(float(abs(ca - cb).mean()), 2)
        d["changed_by_script"] = md
        c["a lamp changed by a script lights as its new settings say"] = md <= 4.0

    # Remix (the fake runtime's log)
    cache = p3.fresh(os.path.join(out, "cache_lamps"))
    log, env = mock_env(args, out, "t811_remix", cache)
    r = play(args.ours, out, "t811_remix", os.path.join(scenes, "lamps.blend"), env)
    lines = p2.read_lines(log)
    lights = p2.created(lines, "CreateLight")
    c["the player ran (fake runtime)"] = r["ran"]
    d["remix"] = {}
    by_name = {e["name"]: e for e in E["remix"]}
    for e in E["remix"]:
        if e["name"] in (E["only_shadow"], E["changing"]) or e["type"] == "dome":
            continue
        f = lights.get(p2.fnv1a("light:" + e["name"]))
        bad = remix_light_problems(f, e)
        d["remix"][e["name"]] = {"got": f, "problems": bad}
        c["Remix: %s as its size, shape and power say" % e["name"]] = not bad
    only = lights.get(p2.fnv1a("light:" + E["only_shadow"]))
    d["remix"][E["only_shadow"]] = only
    c["Remix: the Only Shadow lamp is not sent"] = only is None
    like = dict(by_name[E["like"]], name=E["changing"])
    f = lights.get(p2.fnv1a("light:" + E["changing"]))
    bad = remix_light_problems(f, like)
    d["remix"][E["changing"]] = {"got": f, "problems": bad}
    c["Remix: the lamp the script changed arrives changed"] = not bad
    return c, d


def shadow_trace(text):
    """{lamp: times its shadow was drawn} (BGE_REMIX_TRACE_SHADOWS)"""
    counts = {}
    for m in re.finditer(r"^BGE-Remix shadow: (\S+) drawn", text, re.M):
        counts[m.group(1)] = counts.get(m.group(1), 0) + 1
    return counts


def darkening(on, off):
    """how much darker the picture with shadows is, per pixel (0-255)"""
    if on is None or off is None or on.shape != off.shape:
        return None
    return brightness(off) - brightness(on)


def shadow_mask(dark, rect, level=20.0):
    l, t, r, b = rect
    return dark[t:b, l:r] > level


def penumbra(dark, off, rect):
    """pixels partly in shadow: between 15% and 85% of the light that
    reaches them without shadows. Measured against the picture without
    shadows, so the lamp's falloff across the shadow does not count."""
    import numpy as np
    l, t, r, b = rect
    region, lit = dark[t:b, l:r], brightness(off)[t:b, l:r]
    seen = lit > 20.0
    part = np.zeros_like(region)
    part[seen] = np.clip(region[seen] / lit[seen], 0.0, 1.0)
    return int(((part > 0.15) & (part < 0.85)).sum())


def t812(args, out, scenes, expected, shots, runs):
    """shadows: every lamp type casts; edges soften with the light's size;
    no acne on grazing light; a cut-out fence's holes show in its shadow
    (Simple and Variance maps); the far sun shadow is there; static lamps
    stop drawing their shadow; contact shadows appear where objects touch"""
    import numpy as np
    E, F = expected.get("shadows"), expected.get("shadows_far")
    if not E or not F:
        return {"the shadows scenes were made": False}, {}
    c, d = {}, {}
    on, off, noc = (picture(shots.get(n)) for n in ("shadows", "shadows_noshadow", "shadows_nocontact"))
    dark = darkening(on, off)
    c["the pictures made (shadows on, off, without contact shadows)"] = dark is not None and noc is not None
    if dark is None or noc is None:
        return c, d
    cells = E["cells"]

    # every lamp type casts, away from the lamp
    d["cast"] = {}
    for name in ("point_hard", "spot", "sun", "area"):
        cell = cells[name]
        mask = shadow_mask(dark, cell["rect"])
        frac = float(mask.mean())
        ys, xs = np.nonzero(mask)
        away = False
        if len(xs):
            cx, cy = xs.mean() + cell["rect"][0], ys.mean() + cell["rect"][1]
            px, py = cell["center"]
            lx, ly = cell["lamp"]
            away = (cx - px) * (px - lx) + (cy - py) * (py - ly) > 0
        d["cast"][name] = {"shadowed": round(frac, 3), "away_from_the_lamp": away}
        c["%s lamp casts (away from the lamp)" % cell["kind"].lower()] = frac > 0.02 and away

    # softer with a bigger light
    hard, soft = (penumbra(dark, off, cells[n]["rect"]) for n in ("point_hard", "point_soft"))
    d["penumbra_pixels"] = {"size 0.05": hard, "size 0.4": soft}
    c["shadow edges soften with the light's size"] = soft > 1.5 * max(hard, 1)

    # no acne under grazing light (nothing casts there)
    d["acne"] = {}
    for name in ("graze_sun", "graze_point"):
        rect = cells[name]["rect"]
        frac = float(shadow_mask(dark, rect, 12.0).mean())
        lit = rect_mean(off, rect)
        d["acne"][name] = {"darkened": round(frac, 4), "lit": lit}
        c["no acne: %s" % name] = frac < 0.02 and lit > 20.0

    # the fence's holes let light through its shadow
    d["fence"] = {}
    for name in ("fence", "fence_variance"):
        l, t, r, b = cells[name]["rect"]
        region = dark[t:b, l:r]
        full = float(np.percentile(region, 98))
        strong = region > 0.5 * full
        ys, xs = np.nonzero(strong)
        through = None
        if full > 30.0 and len(xs):
            box = region[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
            through = round(float((box < 0.3 * full).mean()), 3)
        d["fence"][name] = {"full_shadow": round(full, 1), "lit_through": through}
        c["the fence's holes show in its shadow (%s)" % ("Variance" if "variance" in name else "Simple")] = \
            through is not None and 0.15 < through < 0.85

    # contact shadows: only near the slab
    cell = cells["contact"]
    l, t, r, b = cell["rect"]
    extra = brightness(noc)[t:b, l:r] - brightness(on)[t:b, l:r]
    mask = extra > 10.0
    reach = int(0.5 * E["px_per_unit"])
    sl, st, sr, sb = cell["slab"]
    near = np.zeros_like(mask)
    near[max(st - reach - t, 0):sb + reach - t, max(sl - reach - l, 0):sr + reach - l] = True
    count = int(mask.sum())
    inside = int((mask & near).sum())
    d["contact"] = {"darker_pixels": count, "near_the_slab": inside, "cell_pixels": int(mask.size)}
    # a band along the slab, not the whole tile
    c["contact shadows appear where objects touch"] = count >= 30 and inside >= 0.9 * count and \
        count <= 0.3 * mask.size

    # the far sun shadow (and the near one)
    fon, foff = picture(shots.get("shadows_far")), picture(shots.get("shadows_far_noshadow"))
    fdark = darkening(fon, foff)
    d["far"] = {}
    for name, pts in F["points"].items():
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        rect = [int(min(xs)) - 5, int(min(ys)) - 2, int(max(xs)) + 6, int(max(ys)) + 3]
        v = None if fdark is None else round(float(crop(fdark, rect).mean()), 1)
        d["far"][name] = v
        c["the %s sun shadow is there" % name] = v is not None and v > 12.0

    # static lamps stop drawing their shadow
    counts = shadow_trace(runs.get("shadows", {}).get("text", ""))
    d["drawn"] = counts
    c["a static lamp stops drawing its shadow"] = 0 < counts.get("S_static", 0) <= 3
    c["a moving lamp draws its shadow again"] = counts.get("S_moving", 0) >= 10
    c["a lamp draws again when something near it moves"] = counts.get("S_static_mover", 0) >= 10
    return c, d


# ---------------------------------------------------------------------------

def run(args, out):
    scenes, expected = make_scenes(args, out)
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    shots, runs = {}, {}

    # the game's pictures (OpenGL)
    for name in ("proc_textures_gl", "pbr_spheres_gl", "pbr_classic_gl", "classic_remix_gl", "glsl_features",
                 "fallback"):
        # the trace says how each material's first draw was set up (orco)
        runs[name] = play(args.ours, out, "game_" + name, S(name),
                          {"BGE_REMIX_TRACE": "1"} if name == "proc_textures_gl" else None)
        shots[name] = runs[name].get("screenshot")
    api_path = os.path.join(out, "t86_api.json")
    runs["api"] = play(args.ours, out, "game_api", S("glsl_features"), {"BGE_REMIX_P8_API": api_path}, frames=60)

    # Part B: lamps and shadows (the shadows run says when each lamp draws its shadow)
    # (each lamp's shadow buffer is written out once, for checking the maps)
    for name in ("lamps_gl", "shadows", "shadows_noshadow", "shadows_nocontact", "shadows_far", "shadows_far_noshadow"):
        if os.path.isfile(S(name)):
            dump = os.path.join(out, "shadow_dump_" + name)
            os.makedirs(dump, exist_ok=True)
            env = {"BGE_REMIX_SHADOW_DUMP": dump}
            if name == "shadows":
                env["BGE_REMIX_TRACE_SHADOWS"] = "1"
            runs[name] = play(args.ours, out, "game_" + name, S(name), env)
            shots[name] = runs[name].get("screenshot")
    api = p2.read_json(api_path)

    def mt_run(build, tag):
        res = {}
        # not tag + ".json": that is the probe's own result file
        a, b, j = (os.path.join(out, tag + x) for x in ("_a.png", "_b.png", "_mt.json"))
        for p in (a, b, j):
            if os.path.isfile(p):
                os.remove(p)
        r = play(build, out, tag, S("mt_features"), {"BGE_REMIX_P8_SHOT_A": a, "BGE_REMIX_P8_SHOT_B": b,
                                                      "BGE_REMIX_P8_MT": j}, frames=150, debug=(build == args.ours))
        res.update({"ran": r["ran"], "shot_a": a, "shot_b": b, "mt": p2.read_json(j), "screenshot": r["screenshot"],
                    "exit": r["exit"], "crashed": r["crashed"], "debugger": r["debugger"]})
        return res

    mt_ours = mt_run(args.ours, "game_mt_features") if os.path.isfile(S("mt_features")) else {}
    shots["mt_features"] = mt_ours.get("screenshot")
    mt_stock = {}
    if args.official:
        mt_stock = mt_run(args.official, "stock_mt_features") if os.path.isfile(S("mt_features")) else {}
        for name in ("glsl_features", "pbr_classic_gl"):
            r = play(args.official, out, "stock_" + name, S(name))
            shots["stock_" + name.replace("_gl", "")] = r.get("screenshot")

    tests = {}
    plan = [("T8.1 One mode", lambda: t81(args, out, scenes, mt_ours, api)),
            ("T8.2 Multitexture's features", lambda: t82(expected, mt_ours, mt_stock)),
            ("T8.3 GLSL unchanged", lambda: t83(expected, shots)),
            ("T8.4 Fallback", lambda: t84(args, out, scenes, expected, runs["fallback"])),
            ("T8.5 3D view", lambda: t85(args, out, scenes, expected, shots)),
            ("T8.6 Python", lambda: t86(api)),
            ("T8.7 Procedurals in GLSL", lambda: t87(args, out, scenes, expected, shots)),
            ("T8.8 Procedurals baked", lambda: t88(args, out, scenes, expected)),
            ("T8.9 One physical material", lambda: t89(args, out, scenes, expected, shots)),
            ("T8.10 Classic into Remix", lambda: t810(args, out, scenes, expected)),
            ("T8.11 Lights", lambda: t811(args, out, scenes, expected, shots)),
            ("T8.12 Shadows", lambda: t812(args, out, scenes, expected, shots, runs))]
    needs_official = ("T8.1", "T8.2", "T8.3")
    for name, fn in plan:
        if not args.official and name.split()[0] in needs_official:
            tests[name] = {"ok": False, "checks": {"official 2.79b given (--official)": False}, "detail": {}}
            continue
        try:
            c, d = fn()
        except Exception as ex:
            import traceback
            c, d = {"the test ran": False}, {"error": traceback.format_exc()}
            print(name, ex)
        tests[name] = {"ok": bool(c) and all(bool(v) for v in c.values()), "checks": c, "detail": d}
    c, d = t820(out)
    tests["T8.20 Regression (P0-P7)"] = {"ok": all(c.values()), "checks": c, "detail": d}
    ran = {k: bool(r.get("ran")) for k, r in runs.items()}
    tests["The game ran (every scene)"] = {"ok": all(ran.values()), "checks": ran, "detail": {}}

    bench = benchmarks(args, out, scenes, runs)
    res = {"tests": tests, "bench": bench, "all_ok": all(t["ok"] for t in tests.values())}
    sheet = [("proc_textures: game", shots.get("proc_textures_gl")),
             ("proc_textures: F12 (stock 2.79)", os.path.join(out, "t87_f12_proc_textures.png")),
             ("pbr_spheres: game", shots.get("pbr_spheres_gl")),
             ("pbr_spheres: F12", os.path.join(out, "t89_f12_pbr_spheres.png")),
             ("mt_features: ours (GLSL)", mt_ours.get("shot_b")),
             ("mt_features: stock 2.79 (Multitexture)", mt_stock.get("shot_b")),
             ("glsl_features: ours", shots.get("glsl_features")),
             ("glsl_features: stock 2.79", shots.get("stock_glsl_features")),
             ("classic_remix: game", shots.get("classic_remix_gl")),
             ("fallback: FailMe failing on purpose", os.path.join(out, "t84_fail.png")),
             ("lamps: game", shots.get("lamps_gl")),
             ("lamps: F12", os.path.join(out, "t811_f12_lamps.png")),
             ("shadows: game", shots.get("shadows")),
             ("shadows: without shadows", shots.get("shadows_noshadow")),
             ("shadows_far: game", shots.get("shadows_far"))]
    res["sheet"] = p2.contact_sheet(sheet, os.path.join(out, "sheet_p8a.png"))

    lines = ["BGE-Remix P8 tests, Parts A and B (CI)", "=" * 38, ""]
    for name, t in tests.items():
        lines.append("%-34s %s" % (name, p2.mark(t["ok"])))
        for check, ok in t["checks"].items():
            if not ok:
                lines.append("      failed: " + check)
    lines += ["", "Benchmarks (reported here; their targets are for the RTX run):"]
    for name, v in bench.items():
        lines.append("  %-26s %s" % (name, json.dumps(v)))
    lines.append("Picture sheet: %s" % res.get("sheet"))
    lines += ["", "ALL PASSED" if res["all_ok"] else "SOMETHING FAILED (see results_p8a.json and the .log files)"]
    return res, lines


def benchmarks(args, out, scenes, runs):
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    b = {}

    def ms(r):
        return r.get("probe", {}).get("frame_ms_median")

    def start(r):
        p = r.get("probe", {})
        return round(r["seconds_total"] - p["seconds"], 2) if p.get("seconds") is not None else None

    ours = play(args.ours, out, "b81_ours", S("mt_bench"), frames=600)
    stock = play(args.official, out, "b81_stock", S("mt_bench"), frames=600) if args.official else {}
    b["B8.1 frame time (ms)"] = {"ours": ms(ours), "stock_multitexture": ms(stock) if stock else None}
    b["B8.2 start time (s)"] = {"ours": start(ours), "stock_multitexture": start(stock) if stock else None}
    full = play(args.ours, out, "b83_bump", S("proc_textures_gl"), frames=600)
    flat = play(args.ours, out, "b83_nobump", S("proc_textures_gl_nobump"), frames=600)
    b["B8.3 procedural cost (ms)"] = {"with_bump": ms(full), "without_bump": ms(flat)}
    bake = {}
    for size in (1024, 2048):
        cache = p3.fresh(os.path.join(out, "cache_b84_%d" % size))
        for when in ("first", "cached"):
            tag = "b84_%d_%s" % (size, when)
            log, env = mock_env(args, out, tag, cache, size=size)
            r = play(args.ours, out, tag, S("proc_textures"), env, frames=300, seconds=30)
            st = r["stats"]
            bake["%d %s" % (size, when)] = {"bake_ms": st.get("procedural_bake_ms"),
                                            "encoded": st.get("textures_encoded"), "reused": st.get("textures_reused"),
                                            "start_s": start(r)}
    b["B8.4 baking"] = bake
    phys = play(args.ours, out, "b85_physical", S("mt_bench_physical"), frames=600)
    b["B8.5 physical vs Classic (ms)"] = {"physical": ms(phys), "classic": ms(ours)}
    shadows = {}
    for name in ("0", "1", "4", "8", "sun", "hard"):
        path = S("shadows_bench_" + name)
        if os.path.isfile(path):
            shadows[name] = ms(play(args.ours, out, "b87_" + name, path, frames=600))
    b["B8.7 shadows (ms): casting point lamps 0/1/4/8, the sun only, 8 hard"] = shadows
    return b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--official")
    ap.add_argument("--mock", required=True)
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    if args.official:
        args.official = os.path.abspath(args.official)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    res, lines = run(args, out)
    with open(os.path.join(out, "results_p8a.json"), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, "summary_p8a.txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
