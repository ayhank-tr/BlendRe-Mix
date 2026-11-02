"""BGE-Remix P8 tests (P8 plan v2: one rendering system on OpenGL 4.5).

Part A, build A1 (the OpenGL 4.5 foundation):
T8.1  OpenGL 4.5: Blender and the player run in 4.5 (the log says which
      profile); a card below 4.5 gets the message (a test switch stands in
      for one); the drawing layer's self-test passes; no driver error in
      any run (debug contexts, BGE_REMIX_GL_DEBUG=1)
T8.2  cache and recovery: a second start loads the shaders the first saved;
      a driver reset (simulated) stops the game with a message, no crash
T8.3  old shaders: custom_shaders (old-style GLSL) and every 2D filter match
      stock 2.79; each translation also builds for core; the broken shader
      is reported
T8.35 tiers: the pictures are the same with Tier 1 forced

Part A, build A2 (meshes as batches, one mode):
Meshes as batches: the deform scene (a cylinder bent by an armature, a cube
      with a shape key, a plain cube) as stock 2.79 draws it at frames 20 and
      50, the bent and shaped ones moving between them
T8.4, T8.5, T8.7 (the fallback drawn with the built-in classic shader),
T8.8, T8.9 run on batches; B8.6 (game drawing) early reading

Part A, build A3 (bakes on the graphics card):
T8.11 also: a second start draws no bake (every one found in the cache by
      its content); B8.4 reports the bakes' time on the graphics card

After Part A, the 3D view fix (held back from the first build):
T8.21 also: in the editor, changing the startup lamp's type keeps the 3D
      view and every other editor drawing (Material and Textured shading),
      and the shadows put the drawing state back

Part B (the game on OpenGL 4.5; one build):
T8.14 game drawing: classic_look (mist types, stereo modes, dome) and
      game_overlays (text, debug properties, physics shapes, the profile,
      drawLine, render to texture, a bge.texture mirror) as stock 2.79;
      mt_bench the same with each storage setting
T8.15 speed without change: instancing, GPU culling, GPU skinning, one-pass
      shadows, clustered lighting and the shader cache give the same picture
      on and off; GPU culling leaves crowd's hidden half undrawn (its counts,
      BGE_REMIX_DRAW_STATS=1)
T8.16 pipelines: Classic as stock; Modern (Standard) as Classic, exposure 1
      doubles the light, Filmic applies; the setting both ways; cut-out edges
      smoothed with anti-aliasing (Modern and Classic)
T8.17 GPU skinning: crowd's walkers (bones and shape keys) as the CPU poses
      them
T8.18 Remix mode: the overlays over Remix's picture (fake runtime); P2-P5
T8.20 many lamps: each of 256 lamps lights its spot; with 8 lamps clustered
      equals per-lamp; a lamp's type changes while playing
T8.21 also: crowd's characters cast, the same with GPU skinning
T8.22 sky and mist: each sky and Sky & Atmosphere as F12, Modern as Classic;
      mist with distance and height; Remix's sky picture and fog (log);
      Modern's light shafts and their shadows
T8.23 reflections: the chrome ball's walls, the floor mirror, the moving
      cube in both, Static and Planar Env Maps, screen-space reflections
T8.24 volumes: the fog cube and glowing sphere as F12, the baked smoke,
      Remix's fog inside the volume (log)
B8.7-B8.9, B8.11, B8.12 reported (B8.6, B8.10 with Part A's); their targets
      are for the RTX run

Part C (P8.20-P8.25):
T8.25 hair, halos, particles: hair cards and halos as F12's strands and halo
      maths (region colours); Remix gets hair cards, halo sprites and the GPU
      emitters (fake runtime's log); None systems are not sent
T8.26 transparency: Modern blends the same in two drawing orders (weighted
      blended OIT); Classic's sorting too
T8.27 left-out objects: curve, surface, metaball, text, wire, displaced
      plane, vertex colours, ocean in the game and in Remix (log); the
      displaced mesh the same in both
T8.28 camera and colour: each effect changes the game's picture where it
      should; Remix gets the matching settings (log); a 2D filter over Remix
T8.29 Remix match: remix_extras' fields reach Remix (log); the node material
      Remix mode cannot read is baked; the match report; the game draws from
      the compressed files Remix reads
T8.30 panels: the game scene hides Blender Internal's own options and marks
      the ones that work in only some renderers (drawn into a stand-in
      layout); whether each shown option changes a picture is judged on the
      picture sheet
B8.13, B8.14 reported; their targets are for the RTX run

The first build's tests, renumbered (P8 plan v2):
T8.4  one mode (was T8.1)            T8.5  Multitexture's features (T8.2)
T8.6  GLSL unchanged (T8.3)          T8.7  fallback (T8.4)
T8.8  3D view (T8.5)                 T8.9  Python (T8.6)
T8.10 procedurals in GLSL (T8.7)     T8.11 procedurals baked (T8.8)
T8.12 one physical material (T8.9)   T8.13 Classic into Remix (T8.10)
T8.19 lights (T8.11)                 T8.21 shadows (T8.12)
T8.34 regression: P0-P7 unchanged (T8.20)
B8.1-B8.5, B8.10 (shadows) reported; their targets are for the RTX run

  python run_p8.py --ours <build> --official <stock 2.79b> --mock <fake runtime> --out <dir>   (CI)
  python run_p8.py ... --rtx --before C:\\BGE-Remix\\results   (P8.30's RTX run: run_p8_rtx.bat)
"""

import argparse
import json
import os
import re
import time
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

# the startup scene (factory settings) saved as a file
STARTUP_SAVE = r'''
import bpy, sys
bpy.ops.wm.save_as_mainfile(filepath=sys.argv[sys.argv.index("--") + 1], copy=True)
print("P8-STARTUP saved")
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

# P8 v2 (T8.1): every run of our build in a debug context, driver messages
# logged; no Windows message boxes in CI
GL_ENV = {"BGE_REMIX_GL_DEBUG": "1", "BGE_REMIX_NO_DIALOGS": "1"}
GL_ERROR = re.compile(r"^BGE-Remix GL error: .*$", re.M)
DELIBERATE_MISTAKE = "not_declared_anywhere"  # make_scenes_p8.py, the custom shader "Broken"
GL_VERSION = re.compile(r"^BGE-Remix GL: (\d+)\.(\d+) (compatibility|core)( \(debug\))?, tier (\d)(.*)$", re.M)
GL_LOG = {}
OURS = {"build": None}


def note_gl(build, tag, text):
    """what a run of our build says about OpenGL: its version line, errors"""
    if not OURS["build"] or os.path.abspath(build) != OURS["build"]:
        return
    errors = GL_ERROR.findall(text or "")
    # the custom shader scene's deliberate mistake (its "Broken" shader) is
    # meant to fail: its compiler errors, on the lines that name the mistake,
    # are not driver errors
    loc = re.compile(r"shader compiler error \d+: (\d+:\d+)\(|shader compiler error \d+: (\d+\(\d+\)) :")
    def where(e):
        m = loc.search(e)
        return (m.group(1) or m.group(2)) if m else None
    meant = {where(e) for e in errors if DELIBERATE_MISTAKE in e and where(e)}
    errors = [e for e in errors if not (where(e) and where(e) in meant)]
    m = GL_VERSION.search(text or "")
    GL_LOG[tag] = {"errors": len(errors), "first": errors[:5],
                   "version": [int(m.group(1)), int(m.group(2))] if m else None,
                   "profile": m.group(3) if m else None, "debug": bool(m and m.group(4)),
                   "tier": int(m.group(5)) if m else None, "line": m.group(0) if m else None}


def play(build, out, tag, blend, env=None, frames=300, seconds=0, debug=False, gl_debug=True):
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
    e.update(GL_ENV if gl_debug else {"BGE_REMIX_NO_DIALOGS": "1"})
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
    note_gl(build, tag, text)
    r = {"exit": code, "seconds_total": round(secs, 1), "probe": p2.read_json(res), "stats": p2.read_json(stats),
         "screenshot": shot if os.path.isfile(shot) else None, "traceback_in_output": "Traceback" in text,
         "text": text, "debugger": cmd[0] == CDB, "crashed": bool(CRASH_LINE.search(text)), "pageheap": pageheap,
         # the player's own crash report (module + offset per call)
         "crash_report": [l.strip() for l in text.splitlines() if l.startswith("BGE-Remix crash")][:42]}
    if r["crash_report"]:
        r["crashed"] = True
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


def ui_probe(build, out, tag, blend, env, timeout=900, extra=()):
    done = os.path.join(out, tag + "_ui.json")
    if os.path.isfile(done):
        os.remove(done)
    e = p2.base_env(out, tag)
    e.update({"BGE_REMIX_P8_DONE": done})
    e.update(GL_ENV)
    e.update(env)
    # without Blender's crash handler, so a crash leaves a dump (CI)
    # (no scene: Blender's own startup scene)
    code, text, secs = p2.run([p2.exe(build, "blender"), "--disable-crash-handler"] + list(extra) + WINDOW +
                              ([blend] if blend else ["--factory-startup"]) + ["--python", UI_PROBE], e,
                              os.path.join(out, tag + ".log"), timeout=timeout, cwd=out)
    note_gl(build, "ui_" + tag, text)
    r = p2.read_json(done)
    r.update({"exit": code, "seconds": round(secs, 1), "text": text})
    r["ran"] = code == 0 and bool(r.get("finished"))
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
        "the failing material is drawn with its texture": look is not None and look[1] > 20.0,
        "it is drawn with the built-in classic shader":
            "a material without its GLSL shader is drawn with the built-in classic shader" in r["text"],
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
    # P8.7: by content: the second start finds every bake in the cache and draws nothing
    c["a second start draws no bake (every one from the cache)"] = \
        s2.get("procedural_bakes_cached") == len(names) + len(bumps)
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

    # the 3D view in the editor: changing the lamp's type keeps every editor drawing
    cc, dd = lamp_editor(runs.get("lamp_editor", {}))
    c.update(cc)
    d["editor"] = dd
    return c, d


def window_crop(a, rect, height):
    """a region (window coordinates, from the bottom left) out of a screenshot"""
    if a is None or not rect:
        return None
    x, y, w, h = rect["x"], rect["y"], rect["width"], rect["height"]
    return a[height - (y + h):height - y, x:x + w]


def lamp_editor(r):
    """the startup scene's lamp set to each type in turn, the window redrawn
    each time: the 3D view shows each change, the Properties editor keeps its
    interface, and the shadows put the drawing state back (GL check)"""
    import numpy as np
    lamps = r.get("lamps") or {}
    shots = lamps.get("shots") or []
    c = {"the editor run made its pictures": bool(shots) and all(s["written"] for s in shots)}
    d = {"errors": r.get("errors"), "exit": r.get("exit"), "lamp": lamps.get("lamp"), "shadow": lamps.get("shadow")}
    text = r.get("text", "") or ""
    left = [int(m) for m in re.findall(r"BGE-Remix GL check: shadows drawn, (\d+) left changed", text)]
    d["gl_check"] = {"shadow_passes": len(left), "left_changed": sum(left),
                     "changed_during": sorted(set(re.findall(r"BGE-Remix GL check: shadows changed: ([a-z ]+)", text)))}
    c["the editor drew lamp shadows"] = len(left) > 0
    c["the shadows put the drawing state back"] = len(left) > 0 and sum(left) == 0
    if not c["the editor run made its pictures"]:
        return c, d
    pics = [picture(s["path"]) for s in shots]
    height = pics[0].shape[0]
    view, props = lamps.get("view"), lamps.get("properties")
    d["views"], d["properties"] = {}, {}
    updates, keeps = True, True
    for shade in ("MATERIAL", "TEXTURED"):
        idx = [i for i, s in enumerate(shots) if s["shade"] == shade]
        crops = [brightness(window_crop(pics[i], view, height)) for i in idx]
        ui = [float(brightness(window_crop(pics[i], props, height)).std()) for i in idx] if props else []
        changed = []
        for k in range(1, len(idx)):
            changed.append(round(float((np.abs(crops[k] - crops[k - 1]) > 10.0).mean()), 4))
        back = round(float(np.abs(crops[-1] - crops[0]).mean()), 2)
        d["views"][shade.lower()] = {"changed_from_previous": changed, "last_hemi_vs_first": back,
                                     "types": [shots[i]["type"] for i in idx]}
        d["properties"][shade.lower()] = [round(v, 1) for v in ui]
        updates = updates and all(v >= 0.002 for v in changed) and back < 2.0
        keeps = keeps and bool(ui) and all(v >= 0.6 * ui[0] for v in ui)
    c["the 3D view shows each lamp type (Material and Textured)"] = updates
    c["the other editors keep their interface"] = keeps
    return c, d


# ---------------------------------------------------------------------------
# P8 v2 Part A, build A1: the OpenGL 4.5 foundation

def t81_gl(gl):
    """OpenGL 4.5 in Blender and the player, the message below 4.5, the
    layer's self-test, no driver error in any run"""
    c, d = {}, {}
    text = gl["selftest"].get("text", "")
    m = re.search(r"BGE-Remix layer self-test: (\d+) failed", text)
    c["the drawing layer's self-test ran"] = m is not None
    c["the drawing layer's self-test passes"] = m is not None and int(m.group(1)) == 0
    d["self_test"] = [l for l in text.splitlines() if "layer self-test:" in l]

    player = {t: v for t, v in GL_LOG.items() if not t.startswith("ui_") and t != "t81_fake_old_card"}
    ui = {t: v for t, v in GL_LOG.items() if t.startswith("ui_")}
    ok45 = lambda v: v["version"] is not None and tuple(v["version"]) >= (4, 5)  # noqa: E731
    c["the player runs in OpenGL 4.5 or later (every run)"] = bool(player) and all(ok45(v) for v in player.values())
    c["Blender runs in OpenGL 4.5 or later"] = bool(ui) and all(ok45(v) for v in ui.values())
    c["the runs had debug contexts (driver messages on)"] = bool(player) and all(v["debug"] for v in player.values())
    # Part C step 8: a core context unless --gl-compat asked for the other
    compat_tags = {t for t in GL_LOG if "compat" in t}
    core_runs = {t: v["profile"] for t, v in GL_LOG.items() if t not in compat_tags and t != "t81_fake_old_card"}
    c["Blender and the player run in a core context"] = bool(core_runs) and \
        all(p == "core" for p in core_runs.values())
    c["--gl-compat gives a compatibility context"] = bool(compat_tags) and \
        all(GL_LOG[t]["profile"] == "compatibility" for t in compat_tags)
    d["not_core"] = sorted(t for t, p in core_runs.items() if p != "core")[:20]
    d["versions"] = sorted({v["line"] for v in GL_LOG.values() if v["line"]})
    d["runs_without_a_version_line"] = sorted(t for t, v in GL_LOG.items() if not v["line"])[:20]

    fake = gl["fake"]
    c["an older card gets the message and the program stops"] = \
        fake.get("exit") == 3 and "BGE-Remix needs OpenGL 4.5" in fake.get("text", "")
    d["fake_old_card"] = {"exit": fake.get("exit"),
                          "message": [l for l in fake.get("text", "").splitlines() if "needs OpenGL" in l][:2]}

    errors = {t: v["first"] for t, v in GL_LOG.items() if v["errors"]}
    c["no driver error in any run"] = not errors
    d["driver_errors"] = errors
    d["driver_error_counts"] = {t: v["errors"] for t, v in GL_LOG.items() if v["errors"]}
    return c, d


def t82_cache(gl):
    """the shader cache, and a driver reset"""
    c, d = {}, {}
    rx = re.compile(r"BGE-Remix shader cache: (\d+) loaded, (\d+) built, (\d+) saved")

    def counts(r):
        m = rx.findall(r.get("text", ""))
        return [int(x) for x in m[-1]] if m else None

    first, second = counts(gl["cache_first"]), counts(gl["cache_second"])
    d["first_start"] = {"loaded_built_saved": first, "seconds": gl["cache_first"].get("seconds_total")}
    d["second_start"] = {"loaded_built_saved": second, "seconds": gl["cache_second"].get("seconds_total")}
    no_binaries = "the driver offers no program binaries"
    if no_binaries in gl["cache_first"].get("text", "") and no_binaries in gl["cache_second"].get("text", ""):
        # Mesa on Windows offers no program binaries: the cache must stay off
        # cleanly here; saving and loading are checked in the RTX run
        d["cache"] = "the driver offers no program binaries; saving and loading are checked in the RTX run"
        c["without program binaries the cache stays off"] = bool(first) and bool(second) and \
            first[0] == 0 and first[2] == 0 and second[0] == 0 and second[2] == 0
    else:
        c["the first start saves its shaders"] = bool(first) and first[2] > 0
        c["a second start loads them and builds none"] = bool(first) and bool(second) and \
            second[0] >= first[2] and second[1] == 0
    c["both starts ran"] = bool(gl["cache_first"].get("ran")) and bool(gl["cache_second"].get("ran"))

    r = gl["reset"]
    text = r.get("text", "")
    c["a driver reset (simulated) is reported"] = "the graphics driver was reset (simulated)" in text
    c["the game stops with a message"] = "the game has stopped" in text
    c["no crash"] = r.get("exit") == 0 and not r.get("crashed")
    d["reset"] = {"exit": r.get("exit"), "frames": r.get("frames"),
                  "lines": [l for l in text.splitlines() if "reset" in l or "stopped" in l][:4]}
    return c, d


def whole_diff(a, b):
    if a is None or b is None or a.shape != b.shape:
        return None
    return round(float(abs(a - b).mean()), 2)


def t83_shaders(expected, runs, shots, cf_dirs):
    """old-style custom shaders and 2D filters: translated, built for core
    too, and drawn as stock 2.79 draws them"""
    c, d = {}, {}
    E = expected.get("custom_shaders")
    F = expected.get("custom_filters")
    if not E or not F:
        return {"the custom shader scenes were made": False}, {}
    text = runs.get("custom_shaders", {}).get("text", "")
    built = len(re.findall(r"GLSL translate custom shader: built,", text))
    not_built = len(re.findall(r"GLSL translate custom shader: not built", text))
    core_ok = len(re.findall(r"GLSL translate custom shader: built, \d+ not translated, core ok", text))
    d["custom_shaders"] = {"built": built, "not_built": not_built, "core_ok": core_ok}
    good = len(E["order"]) - 1
    c["each working shader is translated and built"] = built == good
    c["each translation also builds for core"] = core_ok == good
    c["the broken shader is reported with its line"] = not_built == 1 and "does not build" in text and \
        re.search(r"\d+:\d+\(\d+\): error|ERROR: \d+:\d+|\d+\(\d+\) : error", text) is not None
    a, b = picture(shots.get("custom_shaders")), picture(shots.get("stock_custom_shaders"))
    # ("Lighting" reads the fixed-function lamps, which stock 2.79's GLSL mode
    # leaves to the driver: Mesa's defaults light it as ours does, NVIDIA's
    # leave it dark; reported, not checked)
    rects = {k: v for k, v in E["rects"].items() if k not in (E["broken"], "Lighting")}
    cc, dd = compare_rects(a, b, rects, TOL_SAME_CODE, prefix="custom shader ")
    if "Lighting" in E["rects"]:
        dd["Lighting (reported)"] = region_diff(a, b, E["rects"]["Lighting"])
    c.update(cc)
    d["custom_shader_differences"] = dd
    c["the custom shader pictures were made (ours, stock)"] = a is not None and b is not None

    ftext = runs.get("custom_filters", {}).get("text", "")
    fbuilt = len(re.findall(r"GLSL translate 2D filter: built,", ftext))
    fcore = len(re.findall(r"GLSL translate 2D filter: built, \d+ not translated, core ok", ftext))
    glsl_filters = [m for m in F["modes"] if m != "motionblur"]
    d["filters"] = {"built": fbuilt, "core_ok": fcore}
    c["each 2D filter is translated and built"] = fbuilt >= len(glsl_filters)
    c["each 2D filter's translation builds for core"] = fcore >= len(glsl_filters)
    diffs = {}
    for mode in F["modes"]:
        pa = picture(os.path.join(cf_dirs[0], "filter_%s.png" % mode))
        pb = picture(os.path.join(cf_dirs[1], "filter_%s.png" % mode))
        diffs[mode] = whole_diff(pa, pb)
        c["2D filter %s matches stock 2.79" % mode] = diffs[mode] is not None and diffs[mode] <= TOL_SAME_CODE
    d["filter_differences"] = diffs
    return c, d


def t84_batches(expected, runs, df_dirs):
    """P8.4: meshes as batches; the CPU's changes (an armature's bend, a shape
    key) reach the card each frame: the deform scene against stock 2.79"""
    c, d = {}, {}
    E = expected.get("deform")
    if not E:
        return {"the deform scene was made": False}, {}
    rects, frames = E["rects"], E["frames"]
    c["the deform scene played (ours, stock)"] = bool(runs.get("deform", {}).get("ran")) and \
        bool(runs.get("stock_deform", {}).get("ran"))
    pics = {}
    for frame in frames:
        a = picture(os.path.join(df_dirs[0], "deform_%d.png" % frame))
        b = picture(os.path.join(df_dirs[1], "deform_%d.png" % frame))
        pics[frame] = a
        for name, rect in rects.items():
            v = region_diff(a, b, rect)
            d["%s at frame %d" % (name, frame)] = v
            c["%s at frame %d as in stock 2.79 (within %.0f)" % (name, frame, TOL_SAME_CODE)] = \
                v is not None and v <= TOL_SAME_CODE
    first, last = pics.get(frames[0]), pics.get(frames[-1])
    moved = {name: region_diff(first, last, rect) for name, rect in rects.items()}
    d["frame %d against frame %d" % (frames[0], frames[-1])] = moved
    c["the bend moves the cylinder between the frames"] = (moved["Cylinder"] or 0.0) > 3.0
    c["the shape key moves the cube between the frames"] = (moved["ShapeCube"] or 0.0) > 3.0
    c["the plain cube stays the same"] = moved["PlainCube"] is not None and moved["PlainCube"] <= 1.0
    return c, d


def t835_tiers(gl, shots):
    c, d = {}, {}
    for key, name in (("tier1_glsl", "glsl_features"), ("tier1_cs", "custom_shaders")):
        r = gl[key]
        c["%s: Tier 1 forced (log)" % name] = "(forced)" in (GL_LOG.get(key_tag(key), {}).get("line") or "")
        md = whole_diff(picture(r.get("screenshot")), picture(shots.get(name)))
        d[name] = md
        c["%s: the same picture at Tier 1" % name] = md is not None and md <= 1.0
    return c, d


def key_tag(key):
    return {"tier1_glsl": "t835_tier1_glsl", "tier1_cs": "t835_tier1_custom_shaders"}[key]


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

    # P8 v2 Part A1: OpenGL 4.5, the layer, the shader cache, resets, old shaders, tiers
    gl = {}
    gl["selftest"] = play(args.ours, out, "t81_selftest", S("glsl_features"),
                          {"BGE_REMIX_GL_SELFTEST": os.path.join(out, "layer_selftest")}, frames=60)
    gl["fake"] = play(args.ours, out, "t81_fake_old_card", S("glsl_features"), {"BGE_REMIX_GL_FAKE_VERSION": "3.3"},
                      frames=60)
    cache_dir = os.path.join(out, "shader_cache_t82")
    shutil.rmtree(cache_dir, ignore_errors=True)
    # (a screenshot at frame 50: T8.15 compares the pictures built and loaded)
    gl["cache_first"] = play(args.ours, out, "t82_cache_first", S("glsl_features"),
                             {"BGE_REMIX_SHADER_CACHE": cache_dir, "BGE_REMIX_P2_SHOT_FRAME": "50"}, frames=60)
    gl["cache_second"] = play(args.ours, out, "t82_cache_second", S("glsl_features"),
                              {"BGE_REMIX_SHADER_CACHE": cache_dir, "BGE_REMIX_P2_SHOT_FRAME": "50"}, frames=60)
    gl["reset"] = play(args.ours, out, "t82_reset", S("glsl_features"), {"BGE_REMIX_GL_SIMULATE_RESET": "30"})
    cf_dirs = (os.path.join(out, "filters_ours"), os.path.join(out, "filters_stock"))
    for dpath in cf_dirs:
        shutil.rmtree(dpath, ignore_errors=True)
        os.makedirs(dpath, exist_ok=True)
    if os.path.isfile(S("custom_shaders")):
        runs["custom_shaders"] = play(args.ours, out, "game_custom_shaders", S("custom_shaders"))
        shots["custom_shaders"] = runs["custom_shaders"].get("screenshot")
        runs["custom_filters"] = play(args.ours, out, "game_custom_filters", S("custom_filters"),
                                      {"BGE_REMIX_P8_FILTER_DIR": cf_dirs[0]}, frames=180)
        if args.official:
            r = play(args.official, out, "stock_custom_shaders", S("custom_shaders"))
            shots["stock_custom_shaders"] = r.get("screenshot")
            play(args.official, out, "stock_custom_filters", S("custom_filters"),
                 {"BGE_REMIX_P8_FILTER_DIR": cf_dirs[1]}, frames=180)
        gl["tier1_cs"] = play(args.ours, out, "t835_tier1_custom_shaders", S("custom_shaders"),
                              {"BGE_REMIX_GL_TIER": "1"})
    else:
        gl["tier1_cs"] = {}
    gl["tier1_glsl"] = play(args.ours, out, "t835_tier1_glsl", S("glsl_features"), {"BGE_REMIX_GL_TIER": "1"})

    # P8 v2 Part A2: meshes the CPU changes (an armature, a shape key)
    df_dirs = (os.path.join(out, "deform_ours"), os.path.join(out, "deform_stock"))
    for dpath in df_dirs:
        shutil.rmtree(dpath, ignore_errors=True)
        os.makedirs(dpath, exist_ok=True)
    if os.path.isfile(S("deform")):
        runs["deform"] = play(args.ours, out, "game_deform", S("deform"), {"BGE_REMIX_P8_DEFORM_DIR": df_dirs[0]},
                              frames=120)
        if args.official:
            runs["stock_deform"] = play(args.official, out, "stock_deform", S("deform"),
                                        {"BGE_REMIX_P8_DEFORM_DIR": df_dirs[1]}, frames=120)

    # the editor: the startup scene's lamp changed to each type in the 3D view
    lamp_dir = os.path.join(out, "lamp_editor")
    os.makedirs(lamp_dir, exist_ok=True)
    # (the startup scene saved to a file and opened from it: Blender shows no
    # splash screen over the 3D view then)
    startup = os.path.join(out, "editor_startup.blend")
    if os.path.isfile(startup):
        os.remove(startup)
    blender_script(args.ours, out, "editor_startup", STARTUP_SAVE, [startup])
    runs["lamp_editor"] = ui_probe(args.ours, out, "t812_lamp_editor", startup if os.path.isfile(startup) else None,
                                   {"BGE_REMIX_P8_LAMPS": lamp_dir, "BGE_REMIX_GL_CHECK": "1"})

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

    # P8 v2 Part B: the game on OpenGL 4.5
    B = partb_runs(args, out, S, expected)
    # P8 v2 Part C
    C = partc_runs(args, out, S, expected)
    # Part C steps 7-8: the editors on OpenGL 4.5 core
    U = editor_runs(args, out, S, expected, startup)

    tests = {}
    plan = [("T8.4 One mode", lambda: t81(args, out, scenes, mt_ours, api)),
            ("T8.5 Multitexture's features", lambda: t82(expected, mt_ours, mt_stock)),
            ("T8.6 GLSL unchanged", lambda: t83(expected, shots)),
            ("T8.7 Fallback", lambda: t84(args, out, scenes, expected, runs["fallback"])),
            ("T8.8 3D view", lambda: t85(args, out, scenes, expected, shots)),
            ("T8.9 Python", lambda: t86(api)),
            ("T8.10 Procedurals in GLSL", lambda: t87(args, out, scenes, expected, shots)),
            ("T8.11 Procedurals baked", lambda: t88(args, out, scenes, expected)),
            ("T8.12 One physical material", lambda: t89(args, out, scenes, expected, shots)),
            ("T8.13 Classic into Remix", lambda: t810(args, out, scenes, expected)),
            ("T8.19 Lights", lambda: t811(args, out, scenes, expected, shots)),
            ("T8.21 Shadows", lambda: t812(args, out, scenes, expected, shots, runs)),
            ("T8.2 Cache and recovery", lambda: t82_cache(gl)),
            ("T8.3 Old shaders", lambda: t83_shaders(expected, runs, shots, cf_dirs)),
            ("T8.35 Tiers", lambda: t835_tiers(gl, shots)),
            ("Meshes as batches (deform scene)", lambda: t84_batches(expected, runs, df_dirs)),
            ("T8.14 Game drawing", lambda: t814_drawing(B, expected)),
            ("T8.15 Speed without change", lambda: t815_speed(B, runs, shots, gl)),
            ("T8.16 Pipelines", lambda: t816_pipelines(B, expected, shots)),
            ("T8.17 GPU skinning", lambda: t817_skinning(B, expected)),
            ("T8.18 Remix mode", lambda: t818_remix_mode(B, expected, out)),
            ("T8.20 Many lamps", lambda: t820_many_lamps(B, expected)),
            ("T8.21 Shadows: crowd", lambda: t821_crowd_shadows(B, expected)),
            ("T8.22 Sky and mist", lambda: t822_sky(B, expected)),
            ("T8.23 Reflections", lambda: t823_reflections(B, expected)),
            ("T8.24 Volumes", lambda: t824_volumes(B, expected)),
            ("T8.25 Hair, halos, particles", lambda: t825_hair(C, expected)),
            ("T8.26 Transparency", lambda: t826_transparency(C, expected)),
            ("T8.27 Left-out objects", lambda: t827_left_out(C, expected)),
            ("T8.28 Camera and colour", lambda: t828_camera(C, expected)),
            ("T8.29 Remix match", lambda: t829_match(C, expected)),
            ("T8.30 Panels", lambda: t830_panels(C, expected)),
            ("T8.37 Remix across scene switches", lambda: t837_keep(C, expected)),
            ("T8.31 Editors unchanged", lambda: t831_editors(U, expected)),
            ("T8.32 Selection", lambda: t832_selection(U, expected)),
            ("T8.33 bgl", lambda: t833_bgl(U, expected))]
    needs_official = ("T8.4", "T8.5", "T8.6", "T8.3", "Meshes", "T8.14")
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
    tests["T8.34 Regression (P0-P7)"] = {"ok": all(c.values()), "checks": c, "detail": d}
    # T8.1 last: it reads every run's log
    try:
        c, d = t81_gl(gl)
    except Exception:
        import traceback
        c, d = {"the test ran": False}, {"error": traceback.format_exc()}
    tests["T8.1 OpenGL 4.5"] = {"ok": bool(c) and all(bool(v) for v in c.values()), "checks": c, "detail": d}
    ran = {k: bool(r.get("ran")) for k, r in list(runs.items()) + list(B["runs"].items()) + list(C["runs"].items()) +
           list(U["runs"].items())}
    tests["The game ran (every scene)"] = {"ok": all(ran.values()), "checks": ran, "detail": {}}

    bench = benchmarks(args, out, scenes, runs)
    bench.update(partb_benchmarks(args, out, S, expected))
    bench.update(partc_benchmarks(args, out, S))
    bench.update(editor_benchmarks(U))
    res = {"tests": tests, "bench": bench, "all_ok": all(t["ok"] for t in tests.values())}
    # the self-test picture as PNG for the sheet
    try:
        from PIL import Image
        ppm = os.path.join(out, "layer_selftest", "layer_selftest.ppm")
        if os.path.isfile(ppm):
            Image.open(ppm).save(os.path.join(out, "layer_selftest", "layer_selftest.png"))
    except Exception:
        pass
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
             ("shadows_far: game", shots.get("shadows_far")),
             ("custom_shaders: ours (translated)", shots.get("custom_shaders")),
             ("custom_shaders: stock 2.79", shots.get("stock_custom_shaders")),
             ("deform at frame 50: ours (batches)", os.path.join(out, "deform_ours", "deform_50.png")),
             ("deform at frame 50: stock 2.79", os.path.join(out, "deform_stock", "deform_50.png")),
             ("layer self-test", os.path.join(out, "layer_selftest", "layer_selftest.png")),
             ("classic_look (linear mist): ours", B["runs"].get("b_classic_look_mist_linear", {}).get("screenshot")),
             ("classic_look (anaglyph): ours", B["runs"].get("b_classic_look_stereo_anaglyph", {}).get("screenshot")),
             ("classic_look (dome): ours", B["runs"].get("b_classic_look_dome", {}).get("screenshot")),
             ("game_overlays: ours", B["runs"].get("b_game_overlays", {}).get("screenshot")),
             ("game_overlays: stock 2.79", B["runs"].get("b_stock_game_overlays", {}).get("screenshot")),
             ("game_overlays over Remix (fake runtime)", B["runs"].get("b_game_overlays_remix", {}).get("screenshot")),
             ("glsl_features: Modern, Filmic", B["runs"].get("b_glsl_features_modern_filmic", {}).get("screenshot")),
             ("pbr_spheres: Modern, Standard", B["runs"].get("b_pbr_spheres_modern_standard", {}).get("screenshot")),
             ("crowd (small) at frame 60", B["runs"].get("b_crowd", {}).get("shots", {}).get(60)),
             ("many_lamps at frame 50", B["runs"].get("b_many_lamps", {}).get("shots", {}).get(50)),
             ("world_sky: blend sky (Classic)", B["runs"].get("b_world_sky_blend_classic", {}).get("screenshot")),
             ("world_sky: blend sky, F12", B["f12"].get("b_f12_world_sky_blend_classic")),
             ("world_sky: atmosphere, sun low (Modern)",
              B["runs"].get("b_world_sky_atmosphere_low_modern", {}).get("screenshot")),
             ("world_sky: light shafts (Modern)", B["runs"].get("b_world_sky_shafts_modern", {}).get("screenshot")),
             ("reflections at frame 40", B["runs"].get("b_reflections", {}).get("shots", {}).get(40)),
             ("reflections, Modern with SSR", B["runs"].get("b_reflections_modern", {}).get("shots", {}).get(40)),
             ("volumes: game", B["runs"].get("b_volumes", {}).get("screenshot")),
             ("volumes (no smoke): F12", B["f12"].get("b_f12_volumes")),
             ("hair_particles: game", C["runs"].get("c_hair_particles", {}).get("screenshot")),
             ("hair_particles: F12", C["f12"].get("c_f12_hair_particles")),
             ("hair_particles: Modern", C["runs"].get("c_hair_particles_modern", {}).get("screenshot")),
             ("transparency: Modern", C["runs"].get("c_transparency_modern", {}).get("screenshot")),
             ("transparency: Modern, other order",
              C["runs"].get("c_transparency_modern_shuffled", {}).get("screenshot")),
             ("left_out: game (Modern)", C["runs"].get("c_left_out", {}).get("screenshot")),
             ("camera_fx: none", C["runs"].get("c_camera_fx_none", {}).get("screenshot")),
             ("camera_fx: every effect", C["runs"].get("c_camera_fx_all", {}).get("screenshot")),
             ("camera_fx: 2D filter over Remix (fake runtime)",
              C["runs"].get("c_camera_fx_filter_remix", {}).get("screenshot")),
             ("remix_extras: game (shared texture cache)",
              C["runs"].get("c_remix_extras_shared", {}).get("screenshot")),
             ("editors_ui Default: core (emulated)", U["shot"].get(("core", "Default"))),
             ("editors_ui Default: --gl-compat", U["shot"].get(("compat", "Default"))),
             ("editors_ui Default: stock 2.79", U["shot"].get(("stock", "Default"))),
             ("editors_ui Animation: core", U["shot"].get(("core", "Animation"))),
             ("editors_ui Animation: stock 2.79", U["shot"].get(("stock", "Animation"))),
             ("editors_ui Default, edit mode: core", U["shot"].get(("core", "Default_edit"))),
             ("editors_ui Default, edit mode: stock 2.79", U["shot"].get(("stock", "Default_edit"))),
             ("bgl in the 3D view: core", U["bgl_shot"].get("core")),
             ("bgl in the 3D view: stock 2.79", U["bgl_shot"].get("stock")),
             ("bgl in a game: core", U["runs"].get("u_bgl_game", {}).get("screenshot")),
             ("bgl in a game: stock 2.79", U["runs"].get("u_stock_bgl_game", {}).get("screenshot"))]
    res["sheet"] = p2.contact_sheet(sheet, os.path.join(out, "sheet_p8a.png"))

    lines = ["BGE-Remix P8 tests, plan v2: Parts A, B and C (CI)", "=" * 70, ""]
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




# ---------------------------------------------------------------------------
# P8 v2 Part B: the game on OpenGL 4.5 (T8.14-T8.24, B8.6-B8.12)

TOL_OVERLAY = 6.0   # overlays and bge.texture against stock 2.79 (lines, text)
TOL_SPEED = 1.0     # a speed feature on and off
TOL_SKY = 10.0      # the game's sky against F12 (aligned, blurred)
TOL_VOLUME = 16.0   # a volume against F12 (aligned, blurred)
B_FRAMES = 110      # Part B runs: the probe's screenshot at frame 100

PIPELINE_CHECK = r'''
import bpy, json
sc = bpy.context.scene
r = {"opened": sc.game_settings.render_pipeline}
new = bpy.data.scenes.new("P8Pipeline")
r["new_scene"] = new.game_settings.render_pipeline
sc.game_settings.render_pipeline = 'MODERN'
r["set_modern"] = sc.game_settings.render_pipeline
r["stored"] = sc.get("bge_remix_pipeline")
sc.game_settings.render_pipeline = 'CLASSIC'
r["set_classic"] = sc.game_settings.render_pipeline
print("P8-PIPELINE " + json.dumps(r))
'''

# mt_bench saved with each storage setting (T8.14)
STORAGE = r'''
import bpy, os, sys
folder = sys.argv[sys.argv.index("--") + 1]
done = []
for kind in ("AUTO", "VERTEX_ARRAY", "VERTEX_BUFFER_OBJECT"):
    bpy.ops.wm.open_mainfile(filepath=os.path.join(folder, "mt_bench.blend"))
    bpy.context.scene.game_settings.raster_storage = kind
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "mt_bench_storage_%s.blend" % kind.lower()), copy=True)
    done.append(kind)
print("P8-STORAGE " + ",".join(done))
'''
STORAGES = ("auto", "vertex_array", "vertex_buffer_object")


def play_shots(build, out, tag, blend, env=None, frames=B_FRAMES):
    """play() with the scene's own screenshots (shots_driver) collected by
    frame"""
    folder = os.path.join(out, "shots")
    os.makedirs(folder, exist_ok=True)
    rx = re.compile(r"^%s_(\d+)\.png$" % re.escape(tag))
    for name in os.listdir(folder):
        if rx.match(name):
            os.remove(os.path.join(folder, name))
    e = {"BGE_REMIX_P8_SHOTS": folder, "BGE_REMIX_P8_TAG": tag}
    e.update(env or {})
    r = play(build, out, tag, blend, e, frames=frames)
    r["shots"] = {}
    for name in os.listdir(folder):
        m = rx.match(name)
        if m:
            r["shots"][int(m.group(1))] = os.path.join(folder, name)
    return r


def shot_of(r, frame=None):
    """a run's picture: its own screenshot at the frame, or the probe's"""
    if not r:
        return None
    if frame is not None:
        return picture(r.get("shots", {}).get(frame))
    return picture(r.get("screenshot"))


def text_pixels(a, rect, level=150.0):
    """bright pixels in a region (overlay text on a dark ground)"""
    if a is None:
        return 0
    return int((brightness(crop(a, rect)) > level).sum())


def srgb_to_linear(a):
    import numpy as np
    c = a / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def mock_lines(log):
    return p2.read_lines(log)


def config_values(lines, key):
    """the values the game gave a Remix setting (the fake runtime's log)"""
    out = []
    for l in lines:
        if l.startswith("Config %s=" % key):
            out.append(l.split("=", 1)[1])
    return out


def partb_runs(args, out, S, expected):
    """every Part B run; the pictures and logs the tests read"""
    B = {"runs": {}, "f12": {}, "remix": {}}
    R = B["runs"]
    have = lambda n: os.path.isfile(S(n))  # noqa: E731

    def ours(tag, name, env=None, frames=B_FRAMES, shots=False):
        if not have(name):
            return {}
        R[tag] = (play_shots if shots else play)(args.ours, out, tag, S(name), env, frames=frames)
        return R[tag]

    def stock(tag, name, frames=B_FRAMES):
        if not args.official or not have(name):
            return {}
        R[tag] = play(args.official, out, tag, S(name), frames=frames, gl_debug=False)
        return R[tag]

    def remix(tag, name, frames=B_FRAMES, extra=None):
        if not have(name):
            return {}
        cache = p3.fresh(os.path.join(out, "cache_" + tag))
        log, env = mock_env(args, out, tag, cache)
        env.update(extra or {})
        R[tag] = play(args.ours, out, tag, S(name), env, frames=frames)
        B["remix"][tag] = mock_lines(log)
        return R[tag]

    def f12(tag, name):
        if not have(name):
            return None
        path = os.path.join(out, tag + ".png")
        blender_script(args.ours, out, tag, RENDER, [path], blend=S(name), timeout=1800)
        B["f12"][tag] = path if os.path.isfile(path) else None
        return B["f12"][tag]

    # T8.14: classic_look and game_overlays against stock; mt_bench's storages
    for f in expected.get("classic_look", {}).get("files", []):
        ours("b_" + f, f)
        stock("b_stock_" + f, f)
    ours("b_game_overlays", "game_overlays")
    stock("b_stock_game_overlays", "game_overlays")
    remix("b_game_overlays_remix", "game_overlays_remix")
    if have("mt_bench"):
        blender_script(args.ours, out, "b_storage", STORAGE, [os.path.dirname(S("mt_bench"))])
        for kind in STORAGES:
            ours("b_mt_storage_" + kind, "mt_bench_storage_" + kind)

    # T8.16: the pipelines
    for f in expected.get("pipelines", {}).get("files", []):
        ours("b_" + f, f)
    code, text, _ = blender_script(args.ours, out, "b_pipeline_check", PIPELINE_CHECK, blend=S("glsl_features"))
    B["pipeline_check"] = tagged(text, "P8-PIPELINE")

    # T8.15, T8.17, T8.21: crowd (GPU culling's counts, each speed feature off)
    stats = {k: os.path.join(out, "b_crowd_stats_%s.json" % k) for k in ("on", "nocull")}
    for p in stats.values():
        if os.path.isfile(p):
            os.remove(p)
    ours("b_crowd", "crowd_small", {"BGE_REMIX_DRAW_STATS": "1", "BGE_REMIX_P8_STATS": stats["on"]}, shots=True)
    ours("b_crowd_noinst", "crowd_small", {"BGE_REMIX_NO_INSTANCING": "1"}, shots=True)
    ours("b_crowd_nocull", "crowd_small", {"BGE_REMIX_NO_GPU_CULLING": "1", "BGE_REMIX_DRAW_STATS": "1",
                                            "BGE_REMIX_P8_STATS": stats["nocull"]}, shots=True)
    ours("b_crowd_noskin", "crowd_small", {"BGE_REMIX_NO_GPU_SKINNING": "1"}, shots=True)
    ours("b_crowd_noshadow", "crowd_small_noshadow", shots=True)
    B["crowd_stats"] = {k: p2.read_json(p) for k, p in stats.items()}

    # T8.15, T8.20: lamps
    ours("b_many_lamps", "many_lamps", frames=160, shots=True)
    ours("b_many_lamps_8", "many_lamps_8", frames=B_FRAMES)
    ours("b_many_lamps_8_perlamp", "many_lamps_8", {"BGE_REMIX_NO_CLUSTERED_LIGHTS": "1"})
    ours("b_lamps_perlamp", "lamps_gl", {"BGE_REMIX_NO_CLUSTERED_LIGHTS": "1"}, frames=300)
    ours("b_shadows_nolayered", "shadows", {"BGE_REMIX_NO_LAYERED_SHADOWS": "1"}, frames=300)

    # T8.22: the sky and the mist
    W = expected.get("world_sky", {}).get("files", {})
    for f in W.get("sky", []) + W.get("atmosphere", []) + W.get("mist", []) + W.get("shafts", []):
        ours("b_" + f, f)
    for f in W.get("sky", []) + W.get("atmosphere", []):
        if f.endswith("_classic"):
            f12("b_f12_" + f, f)
    for f in W.get("remix", []):
        remix("b_" + f, f)
    # P8.30: the mist reaches Remix as fog only when asked (Remix's fog
    # turned the RTX run's picture black)
    remix("b_world_sky_mist_remix_fog", "world_sky_mist_remix", extra={"BGE_REMIX_MIST_FOG": "1"})

    # T8.23: reflections
    for f in expected.get("reflections", {}).get("files", []):
        ours("b_" + f, f, frames=90, shots=True)

    # T8.24: volumes
    ours("b_volumes", "volumes")
    ours("b_volumes_nosmoke", "volumes_nosmoke")
    f12("b_f12_volumes", "volumes_nosmoke")
    remix("b_volumes_inside_remix", "volumes_inside_remix", frames=150)
    return B


def t814_drawing(B, expected):
    """classic_look and game_overlays as stock 2.79 draws them; mt_bench the
    same with each storage setting"""
    c, d = {}, {"classic_look": {}, "overlays": {}, "storage": {}}
    R = B["runs"]
    for f in expected.get("classic_look", {}).get("files", []):
        md = whole_diff(shot_of(R.get("b_" + f)), shot_of(R.get("b_stock_" + f)))
        tol = TOL_VIEW if f.endswith("dome") else TOL_SAME_CODE
        d["classic_look"][f] = md
        c["%s as stock 2.79 (within %.0f)" % (f, tol)] = md is not None and md <= tol
    E = expected.get("game_overlays")
    if not E:
        c["the game_overlays scene was made"] = False
    else:
        a, b = shot_of(R.get("b_game_overlays")), shot_of(R.get("b_stock_game_overlays"))
        # (stock 2.79's GLSL draws Face Texture text as white blocks, without
        # the font's picture; ours draws the font as its Multitexture did,
        # checked against that in T8.5)
        rects = {k: v for k, v in E["rects"].items() if k != "face_text"}
        cc, dd = compare_rects(a, b, rects, TOL_OVERLAY, prefix="overlays: ")
        c.update(cc)
        if "face_text" in E["rects"]:
            dd["face_text"] = region_diff(a, b, E["rects"]["face_text"])
        d["overlays"] = dd
        # (bge.render.makeScreenshot takes the picture before the frame rate,
        # profile and debug properties are drawn, in stock 2.79 too: those
        # are checked by eye in the RTX run)
        d["overlay_text_pixels"] = {"ours": text_pixels(a, E["overlay"]), "stock": text_pixels(b, E["overlay"])}
    base = shot_of(R.get("b_mt_storage_auto"))
    for kind in STORAGES[1:]:
        md = whole_diff(base, shot_of(R.get("b_mt_storage_" + kind)))
        d["storage"][kind] = md
        c["mt_bench with storage %s draws as with Auto" % kind] = md is not None and md <= TOL_SPEED
    return c, d


def t815_speed(B, runs, shots, gl):
    """each speed feature gives the same picture on and off; GPU culling
    leaves the hidden half of crowd undrawn"""
    c, d = {}, {}
    R = B["runs"]

    def same(label, a, b):
        md = whole_diff(a, b)
        d[label] = md
        c["%s: the same picture on and off" % label] = md is not None and md <= TOL_SPEED

    for frame in (60, 90):
        on = shot_of(R.get("b_crowd"), frame)
        same("instancing (crowd, frame %d)" % frame, on, shot_of(R.get("b_crowd_noinst"), frame))
        same("GPU culling (crowd, frame %d)" % frame, on, shot_of(R.get("b_crowd_nocull"), frame))
        same("GPU skinning (crowd, frame %d)" % frame, on, shot_of(R.get("b_crowd_noskin"), frame))
    same("one-pass shadows (shadows)", picture(shots.get("shadows")), shot_of(R.get("b_shadows_nolayered")))
    same("clustered lighting (lamps)", picture(shots.get("lamps_gl")), shot_of(R.get("b_lamps_perlamp")))
    same("clustered lighting (many_lamps, 8 lamps)", shot_of(R.get("b_many_lamps_8")),
         shot_of(R.get("b_many_lamps_8_perlamp")))
    same("the shader cache (glsl_features, built and loaded)", shot_of(gl.get("cache_first")),
         shot_of(gl.get("cache_second")))
    d["batching and streaming"] = "always on (no switch): every other picture is drawn with them"
    st = B.get("crowd_stats", {})
    on, off = st.get("on") or {}, st.get("nocull") or {}
    tested, drawn = (on.get("Culling") or [0, -1])[:2]
    d["culling"] = {"on": on, "off": off}
    c["GPU culling tests crowd's rocks and trees"] = tested > 0
    c["the hidden ones are not drawn (at most 75% drawn)"] = tested > 0 and 0 < drawn <= 0.75 * tested
    c["with GPU culling off nothing is culled on the card"] = bool(off) and (off.get("Culling") or [1])[0] == 0
    return c, d


def t816_pipelines(B, expected, shots):
    """Classic as stock; Modern with Standard as Classic, its exposure, its
    view transform; the setting both ways; cut-out edges smoothed"""
    import numpy as np
    c, d = {}, {}
    R = B["runs"]
    md = whole_diff(picture(shots.get("glsl_features")), picture(shots.get("stock_glsl_features")))
    d["classic_vs_stock"] = md
    c["a Classic scene draws as stock 2.79 GLSL (glsl_features)"] = md is not None and md <= TOL_SAME_CODE
    for src, classic_pic in (("glsl_features", shots.get("glsl_features")), ("pbr_spheres", shots.get("pbr_spheres_gl"))):
        std = shot_of(R.get("b_%s_modern_standard" % src))
        md = whole_diff(std, picture(classic_pic))
        d["%s Modern (Standard) vs Classic" % src] = md
        c["%s: Modern with the Standard view as Classic (within %.0f)" % (src, TOL_F12)] = \
            md is not None and md <= TOL_F12
        exp1 = shot_of(R.get("b_%s_modern_standard_exp1" % src))
        ratio = None
        if std is not None and exp1 is not None and std.shape == exp1.shape:
            l0, l1 = srgb_to_linear(std).mean(axis=2), srgb_to_linear(exp1).mean(axis=2)
            mask = (l0 > 0.03) & (l0 < 0.35)
            if mask.sum() > 500:
                ratio = round(float(np.median(l1[mask] / l0[mask])), 3)
        d["%s exposure 1 / exposure 0 (linear)" % src] = ratio
        c["%s: exposure 1 doubles the light" % src] = ratio is not None and 1.7 <= ratio <= 2.3
        filmic = shot_of(R.get("b_%s_modern_filmic" % src))
        md = whole_diff(std, filmic)
        d["%s Filmic vs Standard" % src] = md
        c["%s: the Filmic view transform applies" % src] = md is not None and md > 3.0
    d["remix_exposure"] = "Modern's exposure against Remix's is checked in the RTX run"
    pc = B.get("pipeline_check") or {}
    d["pipeline_setting"] = pc
    c["an old file opens Classic"] = pc.get("opened") == "CLASSIC"
    c["a new scene starts Modern"] = pc.get("new_scene") == "MODERN"
    c["the setting moves a scene both ways"] = pc.get("set_modern") == "MODERN" and pc.get("stored") == 1 and \
        pc.get("set_classic") == "CLASSIC"
    E = expected.get("pipelines")
    if E:
        def edges(tag):
            a = shot_of(R.get(tag))
            if a is None:
                return None
            g = brightness(crop(a, E["fence"]))
            return round(float(((g > 30.0) & (g < 225.0)).mean()), 4)
        aa, aac, noaa = edges("b_pipelines_fence_aa"), edges("b_pipelines_fence_aa_classic"), \
            edges("b_pipelines_fence_noaa")
        d["fence_partial_pixels"] = {"modern_aa": aa, "classic_aa": aac, "no_aa": noaa}
        c["cut-out edges are smooth with anti-aliasing (Modern)"] = None not in (aa, noaa) and aa > 1.5 * noaa + 0.002
        c["cut-out edges are smooth with anti-aliasing (Classic)"] = None not in (aac, noaa) and \
            aac > 1.5 * noaa + 0.002
    return c, d


def t817_skinning(B, expected):
    """crowd's walkers (bones and shape keys) on the card as on the CPU"""
    c, d = {}, {}
    R = B["runs"]
    E = expected.get("crowd_small")
    if not E:
        return {"the crowd scene was made": False}, {}
    for frame in (60, 90):
        md = region_diff(shot_of(R.get("b_crowd"), frame), shot_of(R.get("b_crowd_noskin"), frame), E["walkers"])
        d["frame %d" % frame] = md
        c["frame %d: the walkers as the CPU poses them" % frame] = md is not None and md <= 1.5
    moved = region_diff(shot_of(R.get("b_crowd"), 60), shot_of(R.get("b_crowd"), 90), E["walkers"])
    d["frame 60 against 90"] = moved
    c["the walkers move between the frames"] = moved is not None and moved > 1.0
    return c, d


def t818_remix_mode(B, expected, out):
    """the overlays over Remix's picture (the copy path on CI; memory
    sharing in the RTX run); P2-P5 unchanged"""
    c, d = {}, {}
    E = expected.get("game_overlays")
    r = B["runs"].get("b_game_overlays_remix", {})
    c["the player ran with Remix (fake runtime)"] = bool(r.get("ran"))
    if E:
        # what the game draws over Remix's picture: drawLine's yellow square
        # and the physics shapes' green lines
        a = shot_of(r)
        yellow = green = 0
        if a is not None:
            q = crop(a, E["rects"]["draw_line"])
            yellow = int(((q[..., 0] > 180) & (q[..., 1] > 180) & (q[..., 2] < 120)).sum())
            q = crop(a, E["rects"]["physics"])
            green = int(((q[..., 1] > 180) & (q[..., 0] < 120) & (q[..., 2] < 120)).sum())
        d["over_remix"] = {"yellow_line_pixels": yellow, "green_line_pixels": green}
        c["drawLine's square shows over Remix's picture"] = yellow >= 40
        c["the physics shapes show over Remix's picture"] = green >= 40
    d["shared_memory"] = "Vulkan memory sharing is checked in the RTX run"
    cc, dd = t820(out)
    for k, v in cc.items():
        if k.split()[0] in ("P2", "P3", "P4", "P5"):
            c[k] = v
    d["phases"] = dd
    return c, d


def t820_many_lamps(B, expected):
    """each of the 256 lamps lights its spot; clustered equals per-lamp with
    8 lamps; a lamp's type changes while playing"""
    c, d = {}, {}
    R = B["runs"]
    E = expected.get("many_lamps")
    if not E:
        return {"the many_lamps scene was made": False}, {}
    a = shot_of(R.get("b_many_lamps"), 50)
    c["the street's picture made"] = a is not None
    if a is not None:
        g = brightness(a)
        dark = []
        def at(p):
            return float(g[p[1] - 1:p[1] + 2, p[0] - 1:p[0] + 2].mean())
        for l in E["lamps"]:
            spot = at(l["at"])
            # the darker gap beside it (a casting spot nearby lights one side)
            gap = min(at(l["between"]), at(l.get("between_left", l["between"])))
            if spot < gap + 6.0:
                dark.append({"lamp": l["name"], "spot": round(spot, 1), "between": round(gap, 1)})
        d["lamps_not_lighting"] = dark[:20]
        d["lamps"] = len(E["lamps"])
        c["each of the %d lamps lights its spot" % len(E["lamps"])] = not dark
    md = whole_diff(shot_of(R.get("b_many_lamps_8")), shot_of(R.get("b_many_lamps_8_perlamp")))
    d["8 lamps: clustered vs per-lamp"] = md
    c["with 8 lamps clustered lighting equals the per-lamp picture"] = md is not None and md <= TOL_SPEED
    text = R.get("b_many_lamps", {}).get("text", "")
    changes = re.findall(r"^P8-LAMP-TYPE (\d+) (\d+)", text, re.M)
    d["type_changes"] = changes
    c["the lamp's type changed while playing (60, 120)"] = len(changes) >= 2
    ch = [l for l in E["lamps"] if l["name"] == "ML_changing"]
    if ch and a is not None:
        x, y = ch[0]["at"]
        rect = [max(x - 12, 0), max(y - 12, 0), x + 12, y + 12]
        spot = region_diff(a, shot_of(R.get("b_many_lamps"), 100), rect)
        sun = shot_of(R.get("b_many_lamps"), 150)
        brighter = None if sun is None else round(float(brightness(sun).mean() - brightness(a).mean()), 2)
        d["changed_lamp"] = {"point_to_spot": spot, "sun_brighter_by": brighter}
        c["the changed lamp lights as a spot"] = spot is not None and spot > 1.0
        c["the changed lamp lights as a sun"] = brighter is not None and brighter > 2.0
    return c, d


def t821_crowd_shadows(B, expected):
    """crowd's characters cast, the same with GPU skinning as on the CPU"""
    c, d = {}, {}
    R = B["runs"]
    E = expected.get("crowd_small")
    if not E:
        return {"the crowd scene was made": False}, {}
    on, off = shot_of(R.get("b_crowd"), 60), shot_of(R.get("b_crowd_noshadow"), 60)
    dark = darkening(on, off)
    frac = None if dark is None else round(float(shadow_mask(dark, E["shadows"], 15.0).mean()), 4)
    d["crowd_shadowed"] = frac
    c["crowd's characters cast"] = frac is not None and frac > 0.005
    md = region_diff(on, shot_of(R.get("b_crowd_noskin"), 60), E["shadows"])
    d["crowd_shadows_gpu_vs_cpu"] = md
    c["crowd's shadows the same with GPU skinning"] = md is not None and md <= 1.5
    return c, d


def t822_sky(B, expected):
    """each sky against F12, Modern as Classic, mist with distance and
    height, Remix's sky picture and fog, Modern's light shafts"""
    c, d = {}, {}
    R = B["runs"]
    E = expected.get("world_sky")
    if not E:
        return {"the world_sky scenes were made": False}, {}
    W = E["files"]
    d["sky"] = {}
    for f in W["sky"] + W["atmosphere"]:
        if not f.endswith("_classic"):
            continue
        a, b = shot_of(R.get("b_" + f)), picture(B["f12"].get("b_f12_" + f))
        name = f[len("world_sky_"):-len("_classic")]
        sa, sb = blurred(a), blurred(b)
        sky = aligned_diff(sa, sb, E["sky_rect"])
        ground = aligned_diff(sa, sb, E["ground_rect"])
        modern = whole_diff(a, shot_of(R.get("b_" + f.replace("_classic", "_modern"))))
        d["sky"][name] = {"sky": sky, "ground": ground, "modern_vs_classic": modern}
        c["%s: the sky as F12 (within %.0f)" % (name, TOL_SKY)] = sky is not None and sky <= TOL_SKY
        c["%s: the ground as F12 (within %.0f)" % (name, TOL_SKY)] = ground is not None and ground <= TOL_SKY
        c["%s: Modern as Classic (within %.0f)" % (name, TOL_F12)] = modern is not None and modern <= TOL_F12

    posts = E["posts"]
    d["mist"] = {}
    for f in W["mist"]:
        a = shot_of(R.get("b_" + f))
        # the same view without mist (the blend sky, same pipeline): how much
        # the mist changes each post (Modern's mist is lit fog, not the
        # horizon's colour, so the change is measured, not the colour)
        clear = shot_of(R.get("b_world_sky_blend_" + ("modern" if f.endswith("_modern") else "classic")))
        if a is None or clear is None:
            c["%s played" % f] = False
            continue
        change = lambda rect, pic=a: region_diff(pic, clear, rect)  # noqa: E731
        near, far = change(posts["10"]["lower"]), change(posts["80"]["lower"])
        d["mist"][f] = {"near_post_change": near, "far_post_change": far}
        c["%s: the far post is mistier than the near one" % f] = None not in (near, far) and far > near + 5.0
        if "_height" in f:
            plain = shot_of(R.get("b_" + f.replace("_height", "")))
            up = change(posts["80"]["upper"])
            up0 = None if plain is None else change(posts["80"]["upper"], plain)
            d["mist"][f]["far_post_top_change"] = {"with_height": up, "without": up0}
            c["%s: the mist thins with height" % f] = None not in (up, up0) and up < up0 - 3.0
    # Modern fog: halos become light shafts, shadowed by the posts
    sh, noh, nos = (shot_of(R.get("b_world_sky_%s_modern" % t)) for t in ("shafts", "shafts_nohalo",
                                                                          "shafts_noshadow"))
    rect = E["shaft_rect"]
    shaft = None if sh is None or noh is None else round(rect_mean(sh, rect) - rect_mean(noh, rect), 2)
    shadow = region_diff(sh, nos, rect)
    d["shafts"] = {"brighter_with_the_halo": shaft, "lamp_shadow_difference": shadow}
    c["Modern fog: the spot halo makes a light shaft"] = shaft is not None and shaft > 3.0
    c["Modern fog: the lamp's shadows show in the fog"] = shadow is not None and shadow > 0.5
    # Remix: the sky picture and the mist as fog
    lines = B["remix"].get("b_world_sky_blend_remix", [])
    domes = [l for l in lines if l.startswith("CreateLight") and "type=dome" in l]
    d["remix_sky"] = domes[:3]
    c["Remix gets the sky as a picture"] = any("texture=1" in l for l in domes)
    want = E["mist"]["start"] + E["mist"]["depth"]
    lines = B["remix"].get("b_world_sky_mist_remix", [])
    off = config_values(lines, "rtx.volumetrics.enable")
    lines = B["remix"].get("b_world_sky_mist_remix_fog", [])
    dist = config_values(lines, "rtx.volumetrics.transmittanceMeasurementDistanceMeters")
    d["remix_fog"] = {"distance": dist, "expected": want, "enable": config_values(lines, "rtx.volumetrics.enable"),
                      "enable_by_default": off}
    c["Remix leaves the mist out by default (its fog turned the picture black)"] = \
        bool(B["remix"].get("b_world_sky_mist_remix")) and not any(v == "True" for v in off)
    c["Remix gets the mist as fog with BGE_REMIX_MIST_FOG=1"] = any(abs(float(v) - want) < 0.01 for v in dist if v)
    return c, d


def t823_reflections(B, expected):
    """the chrome ball's walls on the right sides; the floor mirror; the
    moving cube in both; the Env Maps; screen-space reflections"""
    c, d = {}, {}
    R = B["runs"]
    E = expected.get("reflections")
    if not E:
        return {"the reflections scene was made": False}, {}
    a = shot_of(R.get("b_reflections"), 40)
    if a is None:
        return {"the reflections picture made": False}, {}
    l, t, r, b = E["chrome"]
    w = r - l
    strips = {"left": [l, t + (b - t) // 4, l + w // 4, b - (b - t) // 4],
              "right": [r - w // 4, t + (b - t) // 4, r, b - (b - t) // 4],
              "middle": [l + 3 * w // 8, t + 3 * (b - t) // 8, r - 3 * w // 8, b - 3 * (b - t) // 8]}
    col = {k: [round(float(x), 1) for x in crop(a, v).reshape(-1, 3).mean(axis=0)] for k, v in strips.items()}
    d["chrome_colours"] = col
    lr, rg, mb = col["left"], col["right"], col["middle"]
    c["the chrome ball shows the red wall on its left"] = lr[0] > lr[1] + 10 and lr[0] > lr[2] + 10
    c["the chrome ball shows the green wall on its right"] = rg[1] > rg[0] + 10 and rg[1] > rg[2] + 10
    c["the chrome ball shows the blue wall behind the camera in its middle"] = mb[2] > mb[0] + 10 and mb[2] > mb[1]
    sc, sr = region_look(a, E["chrome"]), region_look(a, E["rough"])
    d["spread"] = {"chrome": sc, "rough": sr}
    c["the rougher ball's reflection is blurrier"] = sr[1] < sc[1]
    mirror = region_diff(a, shot_of(R.get("b_reflections_nomirror"), 40), E["patch"])
    d["mirror_vs_none"] = mirror
    c["the floor mirror shows what is above it"] = mirror is not None and mirror > 3.0
    later = shot_of(R.get("b_reflections"), 80)
    moved = {k: region_diff(a, later, E[k]) for k in ("chrome", "patch")}
    d["moving_cube"] = moved
    c["the moving cube appears in the chrome ball"] = (moved["chrome"] or 0.0) > 1.0
    c["the moving cube appears in the mirror"] = (moved["patch"] or 0.0) > 1.0
    noenv = shot_of(R.get("b_reflections_noenv"), 40)
    env = {k: region_diff(a, noenv, E[k]) for k in ("env_static", "env_planar")}
    d["env_maps"] = env
    c["the Static Env Map works"] = (env["env_static"] or 0.0) > 3.0
    c["the Planar Env Map works"] = (env["env_planar"] or 0.0) > 3.0
    ssr = region_diff(shot_of(R.get("b_reflections_modern"), 40), shot_of(R.get("b_reflections_modern_nossr"), 40),
                      E["floor"])
    d["ssr"] = ssr
    c["screen-space reflections add to the floor (Modern)"] = ssr is not None and ssr > 0.3
    return c, d


def t824_volumes(B, expected):
    """the fog cube and the glowing sphere as F12; the smoke shows; Remix's
    fog inside the volume"""
    c, d = {}, {}
    R = B["runs"]
    E = expected.get("volumes")
    if not E:
        return {"the volumes scene was made": False}, {}
    c["the smoke was baked"] = bool(E.get("baked"))
    plain, smoky = shot_of(R.get("b_volumes_nosmoke")), shot_of(R.get("b_volumes"))
    f12 = picture(B["f12"].get("b_f12_volumes"))
    sa, sb = blurred(plain), blurred(f12)
    for name in ("fog", "glow"):
        md = aligned_diff(sa, sb, E["rects"][name])
        d[name] = md
        c["the %s looks as F12 (within %.0f)" % (name, TOL_VOLUME)] = md is not None and md <= TOL_VOLUME
    smoke = region_diff(plain, smoky, E["rects"]["smoke"])
    d["smoke_vs_none"] = smoke
    c["the smoke shows"] = smoke is not None and smoke > 2.0
    r = R.get("b_volumes_inside_remix", {})
    lines = B["remix"].get("b_volumes_inside_remix", [])
    dist = config_values(lines, "rtx.volumetrics.transmittanceMeasurementDistanceMeters")
    d["remix"] = {"distances": dist, "said": [l for l in r.get("text", "").splitlines() if "Remix's fog" in l][:3]}
    c["Remix's fog becomes the volume's when the camera is inside"] = \
        "Remix's fog from the volume %s" % E["fog"]["object"] in r.get("text", "") and \
        any(abs(float(v) - E["fog"]["distance"]) < 0.01 for v in dist if v)
    return c, d


def partb_benchmarks(args, out, S, expected):
    """B8.7-B8.12 (B8.6 and B8.10 with Part A's)"""
    b = {}
    have = lambda n: os.path.isfile(S(n))  # noqa: E731

    def ms(name, tag, env=None, frames=300):
        if not have(name):
            return None
        r = play(args.ours, out, tag, S(name), env, frames=frames, gl_debug=False)
        return r.get("probe", {}).get("frame_ms_median")

    # (the full crowd is slow on CI's software OpenGL: a short clip)
    b["B8.7 GPU-driven drawing (ms): crowd"] = {
        "on": ms("crowd", "b87_crowd_on", frames=80),
        "instancing and GPU culling off": ms("crowd", "b87_crowd_off", {"BGE_REMIX_NO_INSTANCING": "1",
                                                                        "BGE_REMIX_NO_GPU_CULLING": "1"}, frames=80)}
    b["B8.8 Modern pipeline (ms)"] = {
        "glsl_features classic": ms("glsl_features", "b88_glsl_classic"),
        "glsl_features modern": ms("glsl_features_modern_filmic", "b88_glsl_modern"),
        "pbr_spheres classic": ms("pbr_spheres_gl", "b88_pbr_classic"),
        "pbr_spheres modern": ms("pbr_spheres_modern_filmic", "b88_pbr_modern")}
    b["B8.9 many lamps (ms)"] = {"64": ms("many_lamps_64", "b89_64"), "256": ms("many_lamps", "b89_256")}
    b["B8.11 reflections (ms)"] = {f[len("reflections_bench_"):]: ms(f, "b811_" + f)
                                   for f in expected.get("reflections", {}).get("bench", [])}
    b["B8.12 volumes and fog, Modern (ms)"] = {"volumes": ms("volumes_modern", "b812_volumes"),
                                              "mist": ms("world_sky_mist_linear_modern", "b812_mist"),
                                              "shafts": ms("world_sky_shafts_modern", "b812_shafts")}
    return b


def benchmarks(args, out, scenes, runs):
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    b = {}

    def ms(r):
        return r.get("probe", {}).get("frame_ms_median")

    def start(r):
        p = r.get("probe", {})
        return round(r["seconds_total"] - p["seconds"], 2) if p.get("seconds") is not None else None

    ours = play(args.ours, out, "b81_ours", S("mt_bench"), frames=600, gl_debug=False)
    stock = play(args.official, out, "b81_stock", S("mt_bench"), frames=600, gl_debug=False) if args.official else {}
    b["B8.1 frame time (ms)"] = {"ours": ms(ours), "stock_multitexture": ms(stock) if stock else None}
    b["B8.2 start time (s)"] = {"ours": start(ours), "stock_multitexture": start(stock) if stock else None}
    full = play(args.ours, out, "b83_bump", S("proc_textures_gl"), frames=600, gl_debug=False)
    flat = play(args.ours, out, "b83_nobump", S("proc_textures_gl_nobump"), frames=600, gl_debug=False)
    b["B8.3 procedural cost (ms)"] = {"with_bump": ms(full), "without_bump": ms(flat)}
    bake = {}
    for size in (1024, 2048):
        cache = p3.fresh(os.path.join(out, "cache_b84_%d" % size))
        for when in ("first", "cached"):
            tag = "b84_%d_%s" % (size, when)
            log, env = mock_env(args, out, tag, cache, size=size)
            r = play(args.ours, out, tag, S("proc_textures"), env, frames=300, seconds=30, gl_debug=False)
            st = r["stats"]
            bake["%d %s" % (size, when)] = {"bake_ms": st.get("procedural_bake_ms"),
                                            "gpu_ms": st.get("procedural_bake_gpu_ms"),
                                            "from_cache": st.get("procedural_bakes_cached"),
                                            "encoded": st.get("textures_encoded"), "reused": st.get("textures_reused"),
                                            "start_s": start(r)}
    b["B8.4 baking"] = bake
    phys = play(args.ours, out, "b85_physical", S("mt_bench_physical"), frames=600, gl_debug=False)
    b["B8.5 physical vs Classic (ms)"] = {"physical": ms(phys), "classic": ms(ours)}
    shadows = {}
    for name in ("0", "1", "4", "8", "sun", "hard"):
        path = S("shadows_bench_" + name)
        if os.path.isfile(path):
            shadows[name] = ms(play(args.ours, out, "b87_" + name, path, frames=600, gl_debug=False))
    b["B8.10 shadows (ms): casting point lamps 0/1/4/8, the sun only, 8 hard"] = shadows
    # B8.6: the game's drawing with batches against stock 2.79's vertex
    # arrays; the P2 room with RTX Remix off (the P2 file itself draws
    # through Remix, which stock 2.79 does not)
    room = S("room_gl")
    b6 = {"mt_bench": {"ours": ms(ours), "stock": ms(stock) if stock else None}}
    if os.path.isfile(room):
        r_ours = play(args.ours, out, "b86_room_ours", room, frames=600, gl_debug=False)
        r_stock = play(args.official, out, "b86_room_stock", room, frames=600, gl_debug=False) if args.official else {}
        b6["p2_room"] = {"ours": ms(r_ours), "stock": ms(r_stock) if r_stock else None}
    b["B8.6 game drawing (ms)"] = b6
    return b


# ---------------------------------------------------------------------------
# P8 v2 Part C (P8.20-P8.25): T8.25-T8.30, B8.13, B8.14

TOL_STRANDS = 40.0  # hair cards and halos against F12's strands and halo maths (region mean colour)
TOL_ORDER = 3.0     # Modern: the same scene made in two orders (weighted blended transparency)
TOL_SORTED = 6.0    # Classic: sorted blending in two orders
C_FRAMES = 110      # Part C runs: the probe's screenshot at frame 100
C_DRAWS_FRAME = "60"
CAT_HAIR = 1 << 25
CAT_EMITTER = 1 << 23

# every material's Remix match (bge_remix.material_match)
MATCH_CHECK = r'''
import bpy, json
r = {}
try:
    import bge_remix
    for ma in bpy.data.materials:
        r[ma.name] = bge_remix.material_match(ma)
except Exception as ex:
    r["error"] = str(ex)
print("P8-MATCH " + json.dumps(r))
'''

# T8.30: which options the panels show in a game scene and in a Blender
# Render one, recorded by drawing them into a stand-in layout
PANELS_CHECK = r'''
import bpy, json

class Rec:
    def __init__(self, log):
        self.log = log
        self.active = True
        self.enabled = True
        self.alignment = 'EXPAND'
        self.scale_x = self.scale_y = 1.0
        self.operator_context = 'EXEC_DEFAULT'
    def _sub(self, *a, **k):
        return Rec(self.log)
    row = column = split = box = column_flow = _sub
    def prop(self, data, name, **k):
        self.log.append(("prop", name))
    def label(self, text="", **k):
        self.log.append(("label", text))
    def __getattr__(self, name):
        def any_call(*a, **k):
            return Rec(self.log)
        return any_call

class Ctx:
    pass

class Self:
    pass

def drawn(panel, ctx):
    log = []
    p = Self()
    p.layout = Rec(log)
    try:
        if hasattr(panel, "poll") and not panel.poll(ctx):
            return None
        if hasattr(panel, "draw_header"):
            panel.draw_header(p, ctx)
        panel.draw(p, ctx)
    except Exception as ex:
        log.append(("error", repr(ex)))
    return log

sc = bpy.context.scene
ma = bpy.data.materials.get("RX_wall")
la = next((l for l in bpy.data.lamps if l.type == 'SUN'), None)
names = ("MATERIAL_PT_options", "MATERIAL_PT_shadow", "MATERIAL_PT_shading", "MATERIAL_PT_mirror",
         "MATERIAL_PT_sss", "MATERIAL_PT_pipeline", "MATERIAL_PT_remix_fields", "MATERIAL_PT_remix_options",
         "DATA_PT_lamp", "TEXTURE_PT_voxeldata", "TEXTURE_PT_ocean", "RENDER_PT_game_effects")
r = {}
for engine in ("BLENDER_GAME", "BLENDER_RENDER"):
    sc.render.engine = engine
    ctx = Ctx()
    ctx.scene = sc
    ctx.material = ma
    ctx.lamp = la
    ctx.texture = None
    ctx.object = None
    ctx.space_data = None
    out = {}
    for n in names:
        cls = getattr(bpy.types, n, None)
        if cls is None:
            out[n] = "missing"
            continue
        log = drawn(cls, ctx) if engine in getattr(cls, "COMPAT_ENGINES", {engine}) else None
        out[n] = log
    r[engine] = out
print("P8-PANELS " + json.dumps(r))
'''


def rect_rgb(a, rect):
    """a region's mean colour (r, g, b)"""
    if a is None:
        return None
    return crop(a, rect).reshape(-1, a.shape[-1])[:, :3].mean(axis=0)


def partc_runs(args, out, S, expected):
    """every Part C run; the pictures and logs the tests read"""
    C = {"runs": {}, "f12": {}, "remix": {}}
    R = C["runs"]
    have = lambda n: os.path.isfile(S(n))  # noqa: E731

    def ours(tag, name, env=None, frames=C_FRAMES):
        if not have(name):
            return {}
        R[tag] = play(args.ours, out, tag, S(name), env, frames=frames)
        return R[tag]

    def remix(tag, name, frames=C_FRAMES, cache=None, extra=None, seconds=0):
        if not have(name):
            return {}
        cache = cache or p3.fresh(os.path.join(out, "cache_" + tag))
        log, env = mock_env(args, out, tag, cache)
        env["BGE_REMIX_MOCK_DRAWS_FRAME"] = C_DRAWS_FRAME
        env.update(extra or {})
        R[tag] = play(args.ours, out, tag, S(name), env, frames=frames, seconds=seconds)
        C["remix"][tag] = mock_lines(log)
        return R[tag]

    def f12(tag, name):
        if not have(name):
            return None
        path = os.path.join(out, tag + ".png")
        blender_script(args.ours, out, tag, RENDER, [path], blend=S(name), timeout=1800)
        C["f12"][tag] = path if os.path.isfile(path) else None
        return C["f12"][tag]

    # T8.25: hair, halos, particles
    ours("c_hair_particles", "hair_particles")
    ours("c_hair_particles_modern", "hair_particles_modern")
    f12("c_f12_hair_particles", "hair_particles")
    remix("c_hair_particles_remix", "hair_particles_remix")
    # T8.26: transparency in two orders
    for f in expected.get("transparency", {}).get("files", []):
        ours("c_" + f, f)
    # T8.27: left-out objects
    ours("c_left_out", "left_out")
    remix("c_left_out_remix", "left_out_remix")
    # T8.28: camera effects
    for f in expected.get("camera_fx", {}).get("files", []):
        if f.endswith("_remix"):
            remix("c_" + f, f)
        else:
            ours("c_" + f, f)
    # T8.29: Remix's fields; the shared texture cache (Remix fills it, the
    # game alone then reads it)
    shared = p3.fresh(os.path.join(out, "cache_c_shared"))
    # (the fake runtime runs fast: time for the encoder to write the images)
    remix("c_remix_extras_remix", "remix_extras_remix", cache=shared, seconds=12)
    ours("c_remix_extras_shared", "remix_extras", {"BGE_REMIX_CACHE_DIR": shared})
    if have("remix_extras"):
        code, text, _ = blender_script(args.ours, out, "c_match", MATCH_CHECK, blend=S("remix_extras"))
        C["match"] = tagged(text, "P8-MATCH")
        code, text, _ = blender_script(args.ours, out, "c_panels", PANELS_CHECK, blend=S("remix_extras"))
        C["panels"] = tagged(text, "P8-PANELS")
    # T8.37 (P8.30): Remix kept across scene switches; orthographic cameras
    remix("c_remix_keep", "remix_keep", frames=60)
    return C


def coverage(a, rect, level=20.0):
    """the share of a region's pixels that differ from the picture's
    background (its top-left pixel) by more than level"""
    if a is None:
        return None
    bg = a[2, 2, :3]
    reg = crop(a, rect)[:, :, :3]
    return round(float((abs(reg - bg).max(axis=2) > level).mean()), 3)


def changed_pixels(a, b, level=8.0):
    if a is None or b is None or a.shape != b.shape:
        return None
    return int((abs(a - b)[:, :, :3].mean(axis=2) > level).sum())


def sharpness(a, rect):
    """a region's mean horizontal step between neighbours"""
    if a is None:
        return None
    reg = crop(a, rect)[:, :, :3].mean(axis=2)
    return float(abs(reg[:, 1:] - reg[:, :-1]).mean())


def draws_at(lines, frame=C_DRAWS_FRAME):
    """the fake runtime's Draw lines of one frame: their category flags"""
    cats = []
    for l in lines:
        if l.startswith("Draw frame=%s " % frame):
            m = re.search(r" cats=(\d+)", l)
            if m:
                cats.append(int(m.group(1)))
    return cats


def material_lines(lines):
    return [l for l in lines if l.startswith("CreateMaterial ")]


def field(line, key):
    m = re.search(r"(?:^| )%s=(\S*)" % re.escape(key), line)
    return m.group(1) if m else None


def t825_hair(C, expected):
    """hair cards and halos as F12's strands and halos; Remix gets the hair
    cards, halo sprites and the GPU emitters; None systems not sent"""
    c, d = {}, {}
    E = expected.get("hair_particles")
    if not E:
        return {"the hair_particles scene was made": False}, {}
    R = C["runs"]
    game, f12 = shot_of(R.get("c_hair_particles")), picture(C["f12"].get("c_f12_hair_particles"))
    for k in ("ball", "patch", "halo"):
        a = rect_rgb(game, E["rects"][k]) if game is not None else None
        b = rect_rgb(f12, E["rects"][k]) if f12 is not None else None
        diff = None if a is None or b is None else round(float(abs(a - b).mean()), 2)
        d[k] = {"game": None if a is None else [round(float(x), 1) for x in a],
                "f12": None if b is None else [round(float(x), 1) for x in b], "diff": diff}
        c["%s: the game as F12 (within %.0f)" % (k, TOL_STRANDS)] = diff is not None and diff <= TOL_STRANDS
    text = R.get("c_hair_particles", {}).get("text", "")
    c["the hair ball's hair became ribbons"] = "BGE-Remix hair HP_ball:" in text
    c["the grass became ribbons"] = "BGE-Remix hair HP_patch:" in text
    m = re.search(r"BGE-Remix sprites: (\d+) halo objects \((\d+) halos\), (\d+) particle systems", text)
    d["sprites"] = m.group(0) if m else None
    c["the game draws the halo and the particle systems"] = bool(m) and int(m.group(1)) >= 1 and int(m.group(3)) >= 3
    bg = rect_rgb(game, E["columns"]["none"]) if game is not None else None
    for k in ("halo", "billboard", "line", "object", "group"):
        col = rect_rgb(game, E["columns"][k]) if game is not None else None
        diff = None if col is None or bg is None else round(float(abs(col - bg).mean()), 2)
        d["column_" + k] = diff
        c["%s particles show in the game" % k] = diff is not None and diff > 2.0
    lines = C["remix"].get("c_hair_particles_remix", [])
    cats = draws_at(lines)
    d["remix_draw_categories"] = cats
    c["Remix gets hair cards (2 objects)"] = sum(1 for x in cats if x & CAT_HAIR) >= 2
    em = [l for l in lines if l.startswith("Emitter ")]
    d["remix_emitters"] = em
    c["Remix gets the halo, billboard and line emitters, not None (3)"] = len({field(l, "mesh") for l in em}) == 3
    c["their particles: a count and a rate"] = bool(em) and all(int(field(l, "max") or 0) > 0 and
                                                               float(field(l, "rate") or 0) > 0 for l in em)
    rtext = R.get("c_hair_particles_remix", {}).get("text", "")
    c["Remix gets the halo as a sprite"] = "halo_k" in rtext or any("halo_k" in l for l in lines)
    return c, d


def t826_transparency(C, expected):
    """Modern blends the same whatever the drawing order; Classic's sorting too"""
    c, d = {}, {}
    E = expected.get("transparency")
    if not E:
        return {"the transparency scene was made": False}, {}
    R = C["runs"]
    for mode, a, b, tol in (("Modern", "c_transparency_modern", "c_transparency_modern_shuffled", TOL_ORDER),
                            ("Classic", "c_transparency", "c_transparency_shuffled", TOL_SORTED)):
        pa, pb = shot_of(R.get(a)), shot_of(R.get(b))
        for k, rect in E["rects"].items():
            # (particles are simulated by the frame's time: the smoke is
            # compared by its mean colour, not pixel by pixel)
            if k == "smoke":
                ma, mb = rect_rgb(pa, rect), rect_rgb(pb, rect)
                md = None if ma is None or mb is None else round(float(abs(ma - mb).mean()), 2)
            else:
                md = region_diff(pa, pb, rect)
            d["%s %s" % (mode, k)] = md
            c["%s: %s the same in both orders (within %.0f)" % (mode, k, tol)] = md is not None and md <= tol
    return c, d


def t827_left_out(C, expected):
    """each left-out object in the game and in Remix; the displaced plane the
    same mesh in both"""
    c, d = {}, {}
    E = expected.get("left_out")
    if not E:
        return {"the left_out scene was made": False}, {}
    R = C["runs"]
    a = shot_of(R.get("c_left_out"))
    for k in E["items"]:
        cov = coverage(a, E["rects"][k])
        d[k] = cov
        c["%s shows in the game" % k] = cov is not None and cov > 0.03
    text = R.get("c_left_out", {}).get("text", "")
    c["the text has a shape for Remix"] = re.search(r'text "LO_text" for Remix: [1-9]', text) is not None
    for name in E["as_mesh"]:
        c["%s drawn as a mesh" % name] = ('"%s" drawn as a mesh' % name) in text
    disp = re.findall(r"BGE-Remix displacement %s: (\d+) levels, (\d+) faces" % E["displaced"], text)
    rtext = R.get("c_left_out_remix", {}).get("text", "")
    rdisp = re.findall(r"BGE-Remix displacement %s: (\d+) levels, (\d+) faces" % E["displaced"], rtext)
    d["displacement"] = {"game": disp, "remix": rdisp}
    c["the displaced plane is made finer"] = bool(disp) and int(disp[0][1]) > 1
    c["Remix gets the same displaced mesh"] = bool(disp) and disp == rdisp
    lines = C["remix"].get("c_left_out_remix", [])
    meshes = [l for l in lines if l.startswith("CreateMesh ")]
    d["remix_meshes"] = len(meshes)
    c["Remix gets every left-out object (%d meshes or more)" % len(E["items"])] = len(meshes) >= len(E["items"])
    return c, d


def t828_camera(C, expected):
    """each effect changes the game's picture where it should; Remix gets the
    matching settings; a 2D filter works over Remix's picture"""
    c, d = {}, {}
    E = expected.get("camera_fx")
    if not E:
        return {"the camera_fx scene was made": False}, {}
    R = C["runs"]
    none = shot_of(R.get("c_camera_fx_none"))
    where = {"bloom": "glow_ring", "auto_exposure": "whole", "vignette": "corner_tl", "filmic": "whole"}
    for k in E["effects"]:
        pic = shot_of(R.get("c_camera_fx_" + k))
        if k in where:
            md = region_diff(none, pic, E["rects"][where[k]])
            d[k] = md
            c["%s changes the picture (%s)" % (k, where[k])] = md is not None and md > 1.0
        else:
            # depth of field, motion blur and ambient occlusion change edges
            # and corners: counted in pixels
            n = changed_pixels(none, pic)
            d[k] = n
            c["%s changes the picture (1,000 pixels or more)" % k] = n is not None and n >= 1000
    s0, s1 = sharpness(none, E["rects"]["far"]), sharpness(shot_of(R.get("c_camera_fx_depth_of_field")),
                                                          E["rects"]["far"])
    d["far_sharpness"] = [s0, s1]
    c["depth of field blurs the far wall"] = s0 is not None and s1 is not None and s1 < s0
    v_none = rect_rgb(none, E["rects"]["corner_tl"]) if none is not None else None
    vig = shot_of(R.get("c_camera_fx_vignette"))
    v_on = rect_rgb(vig, E["rects"]["corner_tl"]) if vig is not None else None
    c["the vignette darkens the corner"] = v_none is not None and v_on is not None and v_on.mean() < v_none.mean()
    md = region_diff(none, shot_of(R.get("c_camera_fx_all")), E["rects"]["whole"])
    d["all"] = md
    c["every effect at once"] = md is not None and md > 1.0
    lines = C["remix"].get("c_camera_fx_all_remix", [])
    # (Remix's runtime has no depth of field option: the RTX run found rtx.dof.* refused)
    want = {"rtx.postfx.enableMotionBlur": "True", "rtx.bloom.enable": "True",
            "rtx.autoExposure.enabled": "True", "rtx.postfx.enableVignette": "True"}
    got = {k: config_values(lines, k) for k in want}
    d["remix_config"] = got
    for k, v in want.items():
        c["Remix gets %s=%s" % (k, v)] = v in got[k]
    plain, inverted = shot_of(R.get("c_camera_fx_remix")), shot_of(R.get("c_camera_fx_filter_remix"))
    md = region_diff(plain, inverted, E["rects"]["whole"])
    d["filter_over_remix"] = md
    c["the 2D filter works over Remix's picture"] = md is not None and md > 20.0
    return c, d


def t829_match(C, expected):
    """every field of remix_extras reaches Remix; the node material Remix mode
    cannot read is baked; the game reads the files Remix reads"""
    c, d = {}, {}
    E = expected.get("remix_extras")
    if not E:
        return {"the remix_extras scene was made": False}, {}
    lines = C["remix"].get("c_remix_extras_remix", [])
    mats = material_lines(lines)
    d["materials"] = mats
    any_with = lambda key, test: any(test(field(l, key)) for l in mats)  # noqa: E731
    c["the height map and displacement reach Remix"] = any_with("height_tex", lambda v: bool(v) and v not in ("-", "none", "")) and \
        any_with("displace_in", lambda v: v is not None and abs(float(v) - 0.05) < 1e-3)
    c["the anisotropy reaches Remix"] = any_with("anisotropy", lambda v: v is not None and abs(float(v) - 0.8) < 1e-3)
    c["the thin film reaches Remix"] = any_with("thinfilm_nm", lambda v: v is not None and abs(float(v) - 450.0) < 0.1)
    c["the glass's image reaches Remix"] = any(field(l, "type") == "translucent" and
                                               (field(l, "transmittance_tex") or "-") not in ("-", "none", "")
                                               for l in mats)
    cats = draws_at(lines)
    d["draw_categories"] = cats
    for k, bit in E["categories"].items():
        c["the %s's category reaches Remix" % k] = any(x & bit for x in cats)
    rtext = C["runs"].get("c_remix_extras_remix", {}).get("text", "")
    c["the node material is baked"] = 'material "RX_nodes"' in rtext and ("baked" in rtext or "from the cache" in rtext)
    m = C.get("match") or {}
    d["match_RX_nodes"] = (m.get("RX_nodes") or {}).get("fields")
    nodes = m.get("RX_nodes") or {}
    c["the match report says the node material is baked"] = bool(nodes.get("baked"))
    brushed = {f["field"]: f for f in (m.get("RX_brushed") or {}).get("fields", [])}
    c["the match report: the brushed metal's anisotropy exactly"] = \
        brushed.get("anisotropy", {}).get("level") == 0 and "panel" in brushed.get("anisotropy", {}).get("note", "")
    gtext = C["runs"].get("c_remix_extras_shared", {}).get("text", "")
    mm = re.search(r"BGE-Remix: (\d+) images drawn from the shared texture cache", gtext)
    d["shared"] = mm.group(0) if mm else None
    c["the game draws images from the files Remix reads"] = bool(mm) and int(mm.group(1)) >= 1
    return c, d


GAME_HIDDEN = {"MATERIAL_PT_options": ("use_raytrace", "use_full_oversampling", "offset_z", "pass_index",
                                       "use_light_group_exclusive", "use_light_group_local", "use_uv_project"),
               "MATERIAL_PT_shadow": ("use_ray_shadow_bias", "shadow_buffer_bias", "use_cast_approximate",
                                      "use_transparent_shadows"),
               "MATERIAL_PT_shading": ("translucency", "use_cubic")}
GAME_MARKED = {"MATERIAL_PT_mirror": "F12 and Remix only", "MATERIAL_PT_sss": "F12 and Remix only",
               "MATERIAL_PT_remix_fields": "Remix only", "DATA_PT_lamp": "Not in Remix"}


def t837_keep(C, expected):
    """P8.30: a game that started in Remix keeps it in the scene it switches
    to (no setting of its own), not in one that turns Remix off; its
    orthographic camera reaches Remix as a distant perspective one"""
    c, d = {}, {}
    E = expected.get("remix_keep")
    if not E:
        return {"the remix_keep scene was made": False}, {}
    r = C["runs"].get("c_remix_keep", {})
    text = r.get("text") or ""
    frames = (r.get("stats") or {}).get("frames")
    d["remix_frames"] = frames
    d["notes"] = [l for l in text.splitlines() if "stays in RTX Remix" in l or "orthographic camera" in l][:4]
    c["the game played its three scenes"] = bool(r.get("ran"))
    c["KeepB (no setting) stays in Remix"] = 'scene "KeepB" stays in RTX Remix' in text
    # (KeepA and KeepB play 40 frames each, KeepC 60)
    c["KeepC (Remix turned off) is not drawn by Remix (%s Remix frames)" % frames] = \
        frames is not None and 60 <= frames <= 2 * E["switch_at"] + 20
    c["the orthographic camera reaches Remix as a perspective one"] = \
        "an orthographic camera reaches Remix as a distant" in text
    return c, d


def t830_panels(C, expected):
    """in a game scene the options that do nothing are hidden, the ones that
    work in some renderers marked; Blender Render shows them all"""
    c, d = {}, {}
    p = C.get("panels") or {}
    if not p:
        return {"the panels were drawn": False}, {}
    game, render = p.get("BLENDER_GAME", {}), p.get("BLENDER_RENDER", {})
    props = lambda log: {x[1] for x in (log or []) if x[0] == "prop"}  # noqa: E731
    labels = lambda log: [x[1] for x in (log or []) if x[0] == "label"]  # noqa: E731
    errors = {k: [x for x in (v or []) if x[0] == "error"] for k, v in list(game.items()) + list(render.items())}
    d["errors"] = {k: v for k, v in errors.items() if v}
    c["every panel draws without an error"] = not d["errors"]
    for panel, hidden in GAME_HIDDEN.items():
        shown = props(game.get(panel)) & set(hidden)
        d[panel] = sorted(shown)
        c["%s hides %d options in a game scene" % (panel, len(hidden))] = game.get(panel) is not None and not shown
        c["%s shows them in Blender Render" % panel] = bool(props(render.get(panel)) & set(hidden))
    for panel, text in GAME_MARKED.items():
        c["%s carries the mark \"%s\"" % (panel, text)] = text in labels(game.get(panel))
    c["Blender Internal's pipeline panel is hidden in a game scene"] = game.get("MATERIAL_PT_pipeline") is None
    c["Voxel Data and Ocean panels are hidden in a game scene"] = \
        game.get("TEXTURE_PT_voxeldata") is None and game.get("TEXTURE_PT_ocean") is None
    c["the Render tab has the camera effects"] = "fx_bloom" in props(game.get("RENDER_PT_game_effects"))
    return c, d


def partc_benchmarks(args, out, S):
    """B8.13 and B8.14"""
    b = {}
    have = lambda n: os.path.isfile(S(n))  # noqa: E731

    def ms(name, tag, env=None, frames=300):
        if not have(name):
            return None
        r = play(args.ours, out, tag, S(name), env, frames=frames, gl_debug=False)
        return r.get("probe", {}).get("frame_ms_median")

    b["B8.13 hair and particles (ms)"] = {"hair_particles": ms("hair_particles_modern", "b813_hair"),
                                         "transparency": ms("transparency_modern", "b813_transparency")}
    b["B8.14 screen effects (ms)"] = {"none": ms("camera_fx_none", "b814_none"),
                                      "all": ms("camera_fx_all", "b814_all")}
    return b


# ---------------------------------------------------------------------------
# Part C steps 7-8: the editors on OpenGL 4.5 core (T8.31-T8.33, B8.15)

TOL_UI_COMPAT = 1.5   # an editor drawn by the emulation against the driver's old OpenGL (mean)
TOL_UI_STOCK = 4.0    # an editor against stock 2.79's (mean)
TOL_BGL = 4.0         # bgl's drawing against stock 2.79's (mean)
# editors BGE-Remix adds to (panels, menus, bricks): not compared with stock
UI_OURS_ONLY = ("PROPERTIES", "INFO", "LOGIC_EDITOR", "USER_PREFERENCES")


def editor_runs(args, out, S, expected, startup):
    """the probe in our build (core and --gl-compat) and stock 2.79"""
    U = {"runs": {}, "ui": {}, "shot": {}, "bgl_shot": {}, "select": {}, "bench": {}}
    screens = expected.get("editors_ui", {}).get("screens", [])
    builds = [("core", args.ours, ()), ("compat", args.ours, ("--gl-compat",))]
    if args.official:
        builds.append(("stock", args.official, ()))
    for kind, build, extra in builds:
        tag = "u_%s" % kind
        if os.path.isfile(S("editors_ui")):
            d = os.path.join(out, tag + "_screens")
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(d, exist_ok=True)
            r = ui_probe(build, out, tag + "_editors", S("editors_ui"),
                         {"BGE_REMIX_P8_SCREENS": d, "BGE_REMIX_P8_SCREEN_LIST": ",".join(screens),
                          "BGE_REMIX_P8_BGL": d}, extra=extra)
            U["runs"][tag + "_editors"] = r
            U["ui"][kind] = r
            for sh in r.get("screens", {}).get("shots", []):
                U["shot"][(kind, sh["screen"])] = sh["path"] if sh.get("written") else None
            U["bgl_shot"][kind] = r.get("bgl", {}).get("path")
        if startup and os.path.isfile(startup):
            d = os.path.join(out, tag + "_startup")
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(d, exist_ok=True)
            bench = os.path.join(out, tag + "_bench.json")
            r = ui_probe(build, out, tag + "_startup", startup,
                         {"BGE_REMIX_P8_SCREENS": d, "BGE_REMIX_P8_SCREEN_LIST": "Default",
                          "BGE_REMIX_P8_BENCH": bench,
                          # B8.15 timed as people run it: no debug context
                          "BGE_REMIX_GL_DEBUG": "0"}, extra=extra, timeout=1800)
            U["runs"][tag + "_startup"] = r
            U["ui"][kind + "_startup"] = r
            U["bench"][kind] = r.get("bench")
        if os.path.isfile(S("select_pick")):
            sel = os.path.join(out, tag + "_select.json")
            r = ui_probe(build, out, tag + "_select", S("select_pick"), {"BGE_REMIX_P8_SELECT": sel}, extra=extra)
            U["runs"][tag + "_select"] = r
            U["select"][kind] = r.get("select")
    # bgl in a game: ours (core) and stock
    if os.path.isfile(S("bgl_game")):
        U["runs"]["u_bgl_game"] = play(args.ours, out, "u_bgl_game", S("bgl_game"))
        if args.official:
            U["runs"]["u_stock_bgl_game"] = play(args.official, out, "u_stock_bgl_game", S("bgl_game"))
    return U


def area_rect(area):
    r = area.get("region")
    if not r or r["width"] < 8 or r["height"] < 8:
        return None
    return [r["x"], r["y"], r["x"] + r["width"], r["y"] + r["height"]]


def t831_editors(U, expected):
    """each editor's main region: the emulation against the driver's old
    OpenGL, and against stock 2.79"""
    c, d = {}, {}
    core = U["ui"].get("core", {})
    shots = core.get("screens", {}).get("shots", []) + U["ui"].get("core_startup", {}).get("screens", {}).get("shots", [])
    c["the probe ran in the core context"] = bool(core.get("ran")) and not core.get("errors")
    d["probe_errors"] = (core.get("errors") or [])[:5]
    wanted = expected.get("editors_ui", {}).get("screens", [])
    drawn = {sh["screen"] for sh in core.get("screens", {}).get("shots", []) if sh.get("written")}
    c["every screen drawn and saved"] = bool(wanted) and all(n in drawn for n in wanted) and \
        "Default_edit" in drawn and "Default_wire" in drawn
    d["drawn"] = sorted(drawn)
    worst_compat, worst_stock, over_compat, over_stock = {}, {}, [], []
    for i, sh in enumerate(shots):
        startup = i >= len(core.get("screens", {}).get("shots", []))
        key = ("startup " if startup else "") + sh["screen"]
        a = picture(sh["path"])
        for other, worst, over, tol in (("compat", worst_compat, over_compat, TOL_UI_COMPAT),
                                        ("stock", worst_stock, over_stock, TOL_UI_STOCK)):
            src = U["ui"].get(other + ("_startup" if startup else ""), {})
            match = [x for x in src.get("screens", {}).get("shots", []) if x["screen"] == sh["screen"]]
            b = picture(match[0]["path"]) if match else None
            if b is None:
                continue
            for k, area in enumerate(sh.get("areas", [])):
                if other == "stock" and area["type"] in UI_OURS_ONLY:
                    continue
                rect = area_rect(area)
                if rect is None:
                    continue
                md = region_diff(a, b, rect)
                name = "%s / %s %d" % (key, area["type"], k)
                worst[name] = md
                if md is None or md > tol:
                    over.append(name)
    c["each editor as with --gl-compat (within %.1f)" % TOL_UI_COMPAT] = bool(worst_compat) and not over_compat
    c["each editor as stock 2.79 (within %.1f; BGE-Remix's own editors left out)" % TOL_UI_STOCK] = \
        bool(worst_stock) and not over_stock
    d["over_compat"] = {n: worst_compat[n] for n in over_compat}
    d["over_stock"] = {n: worst_stock[n] for n in over_stock}
    d["largest_compat"] = sorted(worst_compat.items(), key=lambda kv: -(kv[1] or 0))[:6]
    d["largest_stock"] = sorted(worst_stock.items(), key=lambda kv: -(kv[1] or 0))[:6]
    text = core.get("text", "")
    d["emulation_notes"] = sorted(set(l for l in text.splitlines() if "GL emulation:" in l))[:20]
    return c, d


def t832_selection(U, expected):
    """clicks select what is under them"""
    c, d = {}, {}
    E = expected.get("select_pick", {})
    core = U["select"].get("core") or {}
    clicks = {k["target"]: k for k in core.get("clicks", [])}
    d["core"] = core
    boxes = E.get("boxes", [])
    c["each box click selects that box"] = bool(boxes) and \
        all(clicks.get(n, {}).get("selected") == [n] for n in boxes)
    c["the front box over the back one is selected"] = clicks.get(E.get("front"), {}).get("selected") == [E.get("front")]
    c["the back box beside it is selected"] = clicks.get(E.get("back"), {}).get("selected") == [E.get("back")]
    c["a click on nothing selects nothing"] = core.get("empty", {}).get("selected") == []
    c["the box selection takes the top row"] = core.get("box", {}).get("selected") == \
        sorted("SP_0_%d" % k for k in range(4))
    edit = core.get("edit", [])
    c["edit-mode clicks select the vertex under them"] = bool(edit) and \
        all(e.get("selected") == [e["vertex"]] for e in edit)
    for other in ("compat", "stock"):
        o = U["select"].get(other)
        if o is None:
            continue
        same = [k.get("selected") for k in core.get("clicks", [])] == [k.get("selected") for k in o.get("clicks", [])]
        same = same and core.get("box", {}).get("selected") == o.get("box", {}).get("selected")
        same = same and [e.get("selected") for e in edit] == [e.get("selected") for e in o.get("edit", [])]
        c["the same selections as %s" % ("--gl-compat" if other == "compat" else "stock 2.79")] = same
    return c, d


def bgl_rect(r):
    """what the handler draws: x 30-840, y 30-330 from the region's bottom"""
    reg = r.get("region")
    if not reg:
        return None
    left = reg["x"] + 30
    right = min(reg["x"] + 840, reg["x"] + reg["width"])
    bottom = reg["y"] + reg["height"] - 30
    top = max(reg["y"], bottom - 300)
    return [left, top, right, bottom]


def t833_bgl(U, expected):
    c, d = {}, {}
    core = U["ui"].get("core", {}).get("bgl", {})
    a = picture(core.get("path"))
    rect = bgl_rect(core)
    c["the 3D view's bgl drawing saved"] = a is not None and rect is not None
    for other, tol in (("compat", TOL_UI_COMPAT * 2), ("stock", TOL_BGL)):
        o = U["ui"].get(other, {}).get("bgl")
        if o is None:
            continue
        md = region_diff(a, picture(o.get("path")), rect) if rect else None
        d["editor_" + other] = md
        c["bgl in the 3D view as %s (within %.1f)" % ("--gl-compat" if other == "compat" else "stock 2.79", tol)] = \
            md is not None and md <= tol
        q, oq = core.get("queries") or {}, o.get("queries") or {}
        c["bgl's queries answer as %s" % ("--gl-compat" if other == "compat" else "stock 2.79")] = \
            bool(q) and q.get("viewport") == oq.get("viewport") and q.get("modelview") == oq.get("modelview")
        d["queries_" + other] = oq
    d["queries_core"] = core.get("queries")
    g = U["runs"].get("u_bgl_game", {})
    s = U["runs"].get("u_stock_bgl_game", {})
    c["the bgl game ran"] = bool(g.get("ran"))
    rect = expected.get("bgl_game", {}).get("rect")
    if s:
        md = region_diff(picture(g.get("screenshot")), picture(s.get("screenshot")), rect)
        d["game_stock"] = md
        c["bgl in a game as stock 2.79 (within %.1f)" % TOL_BGL] = md is not None and md <= TOL_BGL
    text = g.get("text", "")
    note = [l for l in text.splitlines() if "bgl.glAccum is not emulated" in l]
    d["note"] = note[:2]
    c["the skipped old call is named with its script and line"] = bool(note) and "p8_bgl.py" in note[0] and \
        re.search(r"line \d+", note[0]) is not None
    return c, d


def editor_benchmarks(U):
    """B8.15, and per redraw what the drawing sent in our core run (draws
    and vertices through the emulation, megabytes streamed, waits for the
    card)"""
    bench = {k: dict(v) if isinstance(v, dict) else v for k, v in (U.get("bench") or {}).items()}
    stats = {}
    for kind, v in bench.items():
        if isinstance(v, dict) and "stats" in v:
            s = v.pop("stats")
            if kind == "core":
                stats = s
    return {"B8.15 editor drawing, 1,000,000 faces (ms per redraw)": bench,
            "B8.15 what a core redraw sends": stats}


# ---------------------------------------------------------------------------
# P8.30: the RTX run (your RTX card, the real RTX Remix runtime)
#
# T8.36 Remix: the old test scenes' Remix pictures as the build before P8
#       made them (the earlier phases' RTX runs, C:\BGE-Remix\results); every
#       new scene drawn by Remix; Add blending reaching Remix as emissive
# Part B's items: the sky picture the right way up in Remix's dome; Modern's
#       brightness against Remix's; speed against stock 2.79 (B8.6)
# Every benchmark against its target; the picture sheets for the sign-off
# Remix fixes (P8.30 fix 4): no OpenGL error left before Remix's image is
#       drawn (and one Remix run with OpenGL's debug output); the mist left out
#       of Remix. (Fix 3 measured the lamps' light scale: 0.125 stays, the P2
#       room nearest Cycles; and the mist's fog: black at any distance.)

FRAME_60 = 1000.0 / 60.0
TOL_CYCLES = 2.0   # the P2 room against Cycles: no further than the build before P8 (and this much)
REMIX_FRAMES = 320
# Remix compiles every shader it lacks before its first frame, so the
# pictures are ray traced (P2's RTX runs); the picture at frame 300, once
# Remix's denoiser has settled
REMIX_ENV = {"RTX_ENABLE_ASYNC_COMPILATION": "0", "BGE_REMIX_P2_SHOT_FRAME": "300"}
# scenes whose textures take longer than REMIX_FRAMES to reach Remix the
# first time, and how many seconds they play (the third RTX run:
# proc_textures' 38 bakes were still being encoded at frame 300; the final
# one: 8 still at frame 1800, ten seconds uncapped)
REMIX_LONG = {"proc_textures": 45}

REMIX_VARIANT = r'''
# a copy of each scene for Remix: RTX Remix on (scene property bge_remix);
# orthographic cameras stay (P8.30: the engine sends them as distant
# perspective ones; BGE_REMIX_TEST_PERSP=1 turns them into perspective ones
# here, as before). The lamps scene's copy leaves its hemi lamp
# out: Remix lights the whole scene with a hemi (as sky), and keeps no lamp
# to its own layer, so it washed out every other lamp's cell
import bpy, math, os, sys
from mathutils import Vector
argv = sys.argv[sys.argv.index("--") + 1:]
folder, names = argv[0], argv[1:]
for name in names:
    # "keep:name": the camera kept as it is (name_rtxortho)
    keep = name.startswith("keep:")
    name = name[5:] if keep else name
    path = os.path.join(folder, name + ".blend")
    if not os.path.isfile(path):
        continue
    bpy.ops.wm.open_mainfile(filepath=path)
    for sc in bpy.data.scenes:
        sc["bge_remix"] = 1
        if name == "lamps":
            for o in [o for o in sc.objects if o.type == 'LAMP' and o.data.type == 'HEMI']:
                sc.objects.unlink(o)
        ob = sc.camera
        # (P8.30: the engine now sends orthographic cameras to Remix itself;
        # the conversion here only on request)
        if ob and ob.type == 'CAMERA' and ob.data.type == 'ORTHO' and os.environ.get("BGE_REMIX_TEST_PERSP"):
            cam = ob.data
            view = ob.matrix_world.to_3x3() * Vector((0.0, 0.0, -1.0))
            dist = max(view.normalized().dot(Vector((0.0, 0.0, 0.0)) - ob.matrix_world.translation), 1.0)
            width = cam.ortho_scale
            cam.type = 'PERSP'
            cam.sensor_fit = 'HORIZONTAL'
            cam.angle = 2.0 * math.atan(width * 0.5 / dist)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, name + ("_rtxortho" if keep else "_rtx") + ".blend"),
                                check_existing=False)
'''

# each new scene four ways: the game in Classic and in Modern, F12, Remix
# (the Remix column draws name_rtx, a copy for Remix; "+name": name had
# Remix off)
RTX_SCENES = [
    ("proc_textures", "proc_textures_gl", None, "proc_textures_gl", "proc_textures"),
    ("pbr_spheres", "pbr_spheres_gl", "pbr_spheres_modern_filmic", "pbr_spheres_gl", "pbr_spheres"),
    ("classic_remix", "classic_remix_gl", None, "classic_remix_gl", "classic_remix"),
    ("lamps", "lamps_gl", None, "lamps_gl", "lamps"),
    ("shadows", "shadows", None, "shadows", "+shadows"),
    ("many_lamps", "many_lamps", None, None, "+many_lamps"),
    ("sky: blend", "world_sky_blend_classic", "world_sky_blend_modern", "world_sky_blend_classic",
     "world_sky_blend_remix"),
    ("sky: real", "world_sky_real_classic", "world_sky_real_modern", "world_sky_real_classic",
     "+world_sky_real_classic"),
    ("Sky & Atmosphere", "world_sky_atmosphere_middle_classic", "world_sky_atmosphere_middle_modern",
     "world_sky_atmosphere_middle_classic", "+world_sky_atmosphere_middle_classic"),
    ("mist", "world_sky_mist_linear_height_classic", "world_sky_mist_linear_height_modern",
     "world_sky_mist_linear_height_classic", "world_sky_mist_remix"),
    ("reflections", "reflections", "reflections_modern", "reflections", "+reflections"),
    ("volumes", "volumes", "volumes_modern", "volumes_nosmoke", "+volumes"),
    ("hair_particles", "hair_particles", "hair_particles_modern", "hair_particles", "hair_particles_remix"),
    ("transparency", "transparency", "transparency_modern", "transparency", "+transparency"),
    ("left_out", None, "left_out", "left_out", "left_out_remix"),
    ("camera_fx", "camera_fx_none", "camera_fx_all", "camera_fx_none", "camera_fx_all_remix"),
    ("remix_extras", None, "remix_extras", "remix_extras", "remix_extras_remix"),
    ("game_overlays", "game_overlays", None, None, "game_overlays_remix"),
]

# the earlier phases' RTX pictures, made by the build before P8: (phase,
# picture, how it was taken)
BEFORE = [("p2", "T2.1_room", "the P2 room (1920 x 1080)"),
          ("p3", "T3.2_room", "the P3 textured room (1280 x 720)"),
          ("p4", "T4.1_remix_default", "the P4 characters, frozen at tic 90 (1280 x 720)")]


def grid_sheet(rows, heads, target):
    """rows of pictures under column heads (the four-way sheet)"""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    w, h, top = 480, 270, 24
    cols = len(heads)
    sheet = Image.new("RGB", (w * cols, top + (h + top) * len(rows)), (0, 0, 0))
    d = ImageDraw.Draw(sheet)
    for c, head in enumerate(heads):
        d.text((c * w + 8, 6), head, fill=(255, 255, 0))
    for r, (label, paths) in enumerate(rows):
        y = top + r * (h + top)
        d.text((8, y + 6), label, fill=(255, 255, 255))
        for c, path in enumerate(paths):
            if path and os.path.isfile(path):
                img = Image.open(path).convert("RGB").resize((w, h))
            else:
                img = Image.new("RGB", (w, h), (30, 30, 30))
                ImageDraw.Draw(img).text((8, 8), "(none)" if not path else "(missing)", fill=(160, 160, 160))
            sheet.paste(img, (c * w, y + top))
    try:
        sheet.save(target)
    except OSError:
        base, ext = os.path.splitext(target)
        target = "%s_%s%s" % (base, time.strftime("%H%M%S"), ext)
        sheet.save(target)
    return target


def picture_diff(path_a, path_b):
    """mean difference of two pictures at the first one's size, both
    blurred a little (path tracing's grain)"""
    a, b = picture(path_a), picture(path_b)
    if a is None or b is None:
        return None
    if a.shape != b.shape:
        from PIL import Image
        import numpy as np
        b = np.asarray(Image.fromarray(b.clip(0, 255).astype("uint8")).resize((a.shape[1], a.shape[0])),
                       dtype=np.float64)
    return whole_diff(blurred(a, 2.0), blurred(b, 2.0))


def band_colour(a, rect):
    left, top, right, bottom = rect
    return a[top:bottom, left:right].reshape(-1, 3).mean(axis=0)


def linear_mean(path):
    a = picture(path)
    if a is None:
        return None
    lin = srgb_to_linear(a)
    return float((0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]).mean())


def rtx_before(args, out, before, variants=None):
    """T8.36: the earlier phases' Remix pictures taken again, as their RTX
    runs took them, against the ones the build before P8 made"""
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p4"))
    import run_p4 as p4  # noqa: E402
    clean = {"BGE_REMIX_RUNTIME_DIR": None, "BGE_REMIX_MOCK_LOG": None}
    res = {}

    def timed(seconds, extra=None):
        e = dict(clean)
        e.update({"BGE_REMIX_P2_SECONDS": str(seconds), "BGE_REMIX_P2_FRAMES": "60",
                  "BGE_REMIX_P2_SHOT_FRAME": "30"})
        e.update(extra or {})
        return e

    variants = variants or []

    def others(phase, run_one):
        """the same picture with each variant's environment (P8.30 diagnostics)"""
        shots = {}
        for label, env in variants:
            vkey = re.sub(r"[^a-z0-9]+", "_", label.lower()).strip("_")
            shots[label] = run_one("_" + vkey, env).get("screenshot")
        res[phase]["variants"] = shots

    def keep(phase, name, run):
        ref = os.path.join(before, phase, name + ".png") if before else None
        res[phase] = {"now": run.get("screenshot"), "before": ref if ref and os.path.isfile(ref) else None,
                      "running": run.get("stats", {}).get("running") is True,
                      "picture": bool(p2.has_picture(run.get("screenshot"))[0]) if run.get("screenshot") else False}
        res[phase]["difference"] = picture_diff(res[phase]["now"], res[phase]["before"]) \
            if res[phase]["before"] else None

    try:
        d = os.path.join(out, "before_p2")
        os.makedirs(d, exist_ok=True)
        sc, _ = p2.make_scenes(args.ours, d)
        room = os.path.join(sc, "p2_room.blend")
        keep("p2", "T2.1_room", p2.player(args.ours, d, "T2.1_room", room, timed(40), size=(1920, 1080), cwd=d))
        others("p2", lambda sfx, env: p2.player(args.ours, d, "T2.1_room" + sfx, room, timed(40, env),
                                                size=(1920, 1080), cwd=d))
    except BaseException as ex:  # a SystemExit from a scene maker included
        res.setdefault("p2", {})["error"] = repr(ex)
    try:
        d = os.path.join(out, "before_p3")
        os.makedirs(d, exist_ok=True)
        sc, _ = p3.make_scenes(args.ours, d)
        cache = p3.fresh(os.path.join(d, "cache_room"))
        room = os.path.join(sc, "p3_room.blend")
        p2.player(args.ours, d, "T3.6_cold", room, timed(25, {"BGE_REMIX_CACHE_DIR": cache}), size=(1280, 720), cwd=d)
        keep("p3", "T3.2_room", p2.player(args.ours, d, "T3.2_room", room, timed(25, {"BGE_REMIX_CACHE_DIR": cache}),
                                          size=(1280, 720), cwd=d))
        others("p3", lambda sfx, env: p2.player(args.ours, d, "T3.2_room" + sfx, room,
                                                timed(25, dict(env, BGE_REMIX_CACHE_DIR=cache)), size=(1280, 720),
                                                cwd=d))
    except BaseException as ex:
        res["p3"] = {"error": repr(ex)}
    try:
        d = os.path.join(out, "before_p4")
        os.makedirs(d, exist_ok=True)
        sc, _ = p4.make_scenes(args.ours, d)
        env = timed(10, {"BGE_REMIX_CACHE_DIR": os.path.join(d, "cache"), "RTX_ENABLE_ASYNC_COMPILATION": "0",
                         "BGE_REMIX_P4_FREEZE": "90"})
        keep("p4", "T4.1_remix_default", p4.player(args.ours, d, "T4.1_remix_default",
                                                   os.path.join(sc, "p4_characters.blend"), env, size=(1280, 720),
                                                   cwd=d))
        others("p4", lambda sfx, extra: p4.player(args.ours, d, "T4.1_remix_default" + sfx,
                                                  os.path.join(sc, "p4_characters.blend"), dict(env, **extra),
                                                  size=(1280, 720), cwd=d))
    except BaseException as ex:
        res["p4"] = {"error": repr(ex)}
    return res


def judge_benchmarks(bench):
    """each benchmark against its plan target (60 FPS: 16.7 ms a frame)"""
    c, d = {}, {}

    def get(name, *keys):
        for k, v in bench.items():
            if k.startswith(name + " "):
                for key in keys:
                    v = v.get(key) if isinstance(v, dict) else None
                return v
        return None

    def fps60(label, ms):
        d[label] = ms
        c["%s: 60 FPS (%s ms a frame)" % (label, "%.2f" % ms if ms is not None else "?")] = \
            ms is not None and ms <= FRAME_60

    fps60("B8.1 mt_bench", get("B8.1", "ours"))
    fps60("B8.3 proc_textures with bump", get("B8.3", "with_bump"))
    fps60("B8.7 crowd, instancing and GPU culling on", get("B8.7", "on"))
    fps60("B8.8 glsl_features, Modern", get("B8.8", "glsl_features modern"))
    fps60("B8.8 pbr_spheres, Modern", get("B8.8", "pbr_spheres modern"))
    fps60("B8.9 256 lamps", get("B8.9", "256"))
    fps60("B8.10 4 casting lamps", get("B8.10", "4"))
    fps60("B8.11 1 probe every 10 frames", get("B8.11", "p1_r10"))
    fps60("B8.11 4 probes every 10 frames", get("B8.11", "p4_r10"))
    for key in ("volumes", "mist", "shafts"):
        fps60("B8.12 " + key, get("B8.12", key))
    for key in ("hair_particles", "transparency"):
        fps60("B8.13 " + key, get("B8.13", key))
    fps60("B8.14 every screen effect", get("B8.14", "all"))
    ours, stock = get("B8.6", "mt_bench", "ours"), get("B8.6", "mt_bench", "stock")
    c["B8.6 mt_bench not slower than stock 2.79 (%s against %s ms)" % (ours, stock)] = \
        ours is not None and stock is not None and ours <= stock
    d["B8.6 the P2 room with Remix off (reported: P8's fixed cost)"] = get("B8.6", "p2_room")
    core, st = get("B8.15", "core", "edit_ms"), get("B8.15", "stock", "edit_ms")
    # (the same redraw timed twice differs by a few per cent)
    c["B8.15 edit mode not slower than stock 2.79 (%s against %s ms, 5%% allowed)" % (core, st)] = \
        core is not None and st is not None and core <= st * 1.05
    d["B8.15 a whole-window redraw (reported: a known difference)"] = \
        {"core": get("B8.15", "core", "window_ms"), "stock": get("B8.15", "stock", "window_ms")}
    return c, d


def rtx_fixes(args, out, S, have, remix_runs):
    """P8.30 fix 4: no OpenGL error left before Remix's image is drawn (every
    Remix run; one more with OpenGL's debug output, whose driver messages
    would name the call); the mist left out of Remix"""
    c, d = {}, {}
    left = [k for k, r in remix_runs.items() if "left before Remix's image" in (r.get("text") or "")]
    d["left_before_remix"] = left
    c["no OpenGL error left before Remix's image is drawn (every new scene)"] = bool(remix_runs) and not left
    r = play(args.ours, out, "rtx_lamps_gl_debug", S("lamps_rtx"), REMIX_ENV, frames=120, gl_debug=True) \
        if have("lamps_rtx") else {}
    text = r.get("text") or ""
    d["gl_debug"] = {"errors": GL_ERROR.findall(text)[:8],
                     "left_before_remix": [l for l in text.splitlines() if "left before Remix's image" in l][:3]}
    c["the OpenGL debug run: no driver error, none left before Remix's image"] = \
        bool(r.get("ran")) and not d["gl_debug"]["errors"] and not d["gl_debug"]["left_before_remix"]
    mist = remix_runs.get("mist", {}).get("text") or ""
    c["the mist is left out of Remix (said in the log)"] = "mist is left out of Remix" in mist
    return {"ok": all(c.values()), "checks": c, "detail": d}


def run_rtx(args, out):
    scenes, expected = make_scenes(args, out)
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    have = lambda n: bool(n) and os.path.isfile(S(n))  # noqa: E731
    tests, bench = {}, {}

    # every Remix scene's copy for Remix (Remix on, a perspective camera)
    blender_script(args.ours, out, "remix_variants", REMIX_VARIANT,
                   [scenes] + [r[4].lstrip("+") for r in RTX_SCENES if r[4]])

    # each new scene four ways
    print("RTX run: the new scenes in the game, F12 and RTX Remix...", flush=True)
    rows, remix_runs = [], {}
    for label, classic, modern, f12, remix in RTX_SCENES:
        key = re.sub(r"[^a-z0-9]+", "_", label.lower()).strip("_")
        paths = []
        for kind, name in (("classic", classic), ("modern", modern)):
            r = play(args.ours, out, "rtx_%s_%s" % (key, kind), S(name), frames=110, gl_debug=False) \
                if have(name) else {}
            paths.append(r.get("screenshot"))
        f12_path = None
        if have(f12):
            f12_path = os.path.join(out, "rtx_%s_f12.png" % key)
            blender_script(args.ours, out, "rtx_%s_f12" % key, RENDER, [f12_path], blend=S(f12), timeout=1800)
        paths.append(f12_path if f12_path and os.path.isfile(f12_path) else None)
        name = remix.lstrip("+") + "_rtx" if remix else None
        # (also Remix's own copy of its frame, to tell a picture Remix did
        # not make from one that did not reach the window)
        bmp = os.path.join(out, "rtx_%s_remix_own.bmp" % key)
        seconds = REMIX_LONG.get(key, 0)
        env = dict(REMIX_ENV, BGE_REMIX_SCREENSHOT=bmp, BGE_REMIX_SCREENSHOT_FRAME="300")
        r = play(args.ours, out, "rtx_%s_remix" % key, S(name), env, frames=REMIX_FRAMES, seconds=seconds,
                 gl_debug=False) if have(name) else {}
        if r:
            # (a timed run's picture is taken in its last second)
            r["shot_frame"] = 300
            r["shot_seconds"] = seconds - 1.0 if seconds else None
        if r:
            r["remix_own"] = p2.bmp_to_png(bmp)
        remix_runs[key] = r
        paths.append(r.get("screenshot"))
        rows.append((label, paths))
    sheet_scenes = grid_sheet(rows, ["game: Classic", "game: Modern", "F12", "RTX Remix"],
                              os.path.join(out, "sheet_p8_rtx_scenes.png"))

    # T8.36
    print("RTX run: the earlier phases' Remix pictures again...", flush=True)
    before = rtx_before(args, out, args.before)
    c, d = {}, {"before": before, "new_scenes": {}}
    cycles = os.path.join(args.before, "p2", "cycles_room.png") if args.before else None
    cycles = cycles if cycles and os.path.isfile(cycles) else None
    for phase, label, what in BEFORE:
        b = before.get(phase, {})
        if b.get("error") and not b.get("now"):
            c["%s: taken again" % what] = False
            continue
        c["%s: drawn by Remix" % what] = bool(b.get("running")) and bool(b.get("picture"))
        if not b.get("before"):
            d.setdefault("no_picture_from_before", []).append(what)
        elif phase == "p2":
            # the first RTX run: P8's room is closer to Cycles than the build
            # before P8's was (colours no longer decoded twice); so the check
            # is against Cycles, the P2 sign-off's reference
            now_c, before_c = picture_diff(b.get("now"), cycles), picture_diff(b.get("before"), cycles)
            b["from_cycles"] = {"now": now_c, "before": before_c}
            c["%s: no further from Cycles than before P8 (%s against %s)" % (what, now_c, before_c)] = \
                None not in (now_c, before_c) and now_c <= before_c + TOL_CYCLES
        else:
            # P3: the first RTX run found P8 drawing the sign's texture rightly
            # (navy with a yellow arrow) where the build before P8 did not;
            # P3 and P4: P8.30 turned Remix's automatic exposure off (the P2
            # room against Cycles is the check): on the sheet
            d.setdefault("on_the_sheet_only", []).append("%s (difference %s)" % (what, b.get("difference")))
    missing, late = [], []
    for key, r in remix_runs.items():
        s = r.get("stats", {})
        ok, look = p2.has_picture(r.get("screenshot")) if r.get("screenshot") else (False, {})
        own = p2.has_picture(r.get("remix_own"))[1] if r.get("remix_own") else {}
        textured = s.get("first_textured_frame")
        if r.get("shot_seconds"):
            in_time = textured is not None and textured >= 0 and \
                (s.get("first_textured_seconds") or 0.0) <= r["shot_seconds"]
        else:
            in_time = textured is not None and 0 <= textured <= r.get("shot_frame", REMIX_FRAMES)
        if not in_time:
            late.append("%s (%s)" % (key, textured))
        d["new_scenes"][key] = {"running": s.get("running"), "errors": s.get("errors"), "textured_frame": textured,
                                "draws": s.get("draws_last_frame"), "picture": look, "remix_own_picture": own,
                                "notes": [l for l in (r.get("text") or "").splitlines()
                                          if "Remix's image" in l or "is not an RtxOption" in l][:4]}
        if not (r.get("ran") and s.get("running") is True and not s.get("errors") and ok):
            missing.append(key)
    d["not_drawn_by_remix"] = missing
    d["textures_late"] = late
    c["every new scene's textures in Remix before its picture (%s late)" % (", ".join(late) or "none")] = \
        not late and bool(remix_runs)
    c["every new scene drawn by Remix (a picture, Remix running, no Remix errors)"] = not missing and bool(remix_runs)
    # Add blending as Remix's emissive blending: the sphere glows orange
    # over the background beside it
    shot = picture(remix_runs.get("classic_remix", {}).get("screenshot"))
    add = next((e for e in expected.get("classic_remix", []) if e.get("name") == "add"), None)
    if shot is not None and add:
        left, top, right, bottom = add["rect"]
        inside = band_colour(shot, add["rect"])
        beside = band_colour(shot, [left, max(0, top - 60), right, max(1, top - 40)])
        d["add"] = {"inside": [round(float(x), 1) for x in inside], "above": [round(float(x), 1) for x in beside]}
        c["Add blending reaches Remix as emissive: the sphere glows orange"] = \
            float(inside.mean()) > float(beside.mean()) + 10.0 and float(inside[0]) > float(inside[2]) + 20.0
    else:
        c["Add blending reaches Remix as emissive: the sphere glows orange"] = False
    tests["T8.36 Remix (RTX run)"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # P8.30 fix 4
    print("RTX run: the Remix fixes (OpenGL debug)...", flush=True)
    tests["Remix fixes (RTX run)"] = rtx_fixes(args, out, S, have, remix_runs)

    # Part B's items left for this run
    c, d = {}, {}
    sky = expected.get("world_sky", {}).get("sky_rect", [0, 0, SIZE[0], SIZE[1] // 2])
    sky_row = dict(rows).get("sky: blend", [None] * 4)
    game, remix = picture(sky_row[0]), picture(sky_row[3])
    if game is not None and remix is not None:
        left, top, right, bottom = sky
        h = bottom - top
        upper, lower = [left, top, right, top + h // 5], [left, bottom - h // 5, right, bottom]
        dg = band_colour(game, upper) - band_colour(game, lower)
        dr = band_colour(remix, upper) - band_colour(remix, lower)
        import numpy as np
        cos = float(np.dot(dg, dr) / (np.linalg.norm(dg) * np.linalg.norm(dr) + 1e-6))
        d["sky"] = {"game_top_minus_horizon": [round(float(x), 1) for x in dg],
                    "remix_top_minus_horizon": [round(float(x), 1) for x in dr], "agreement": round(cos, 2)}
        c["the sky picture is the right way up in Remix's dome (zenith at the top)"] = cos > 0.3
    else:
        c["the sky picture is the right way up in Remix's dome (zenith at the top)"] = False
    ratios = {}
    for label in ("sky: blend", "pbr_spheres"):
        row = dict(rows).get(label, [None] * 4)
        m, r = linear_mean(row[1]), linear_mean(row[3])
        ratios[label] = round(m / r, 2) if m and r else None
    d["modern_over_remix_brightness"] = ratios
    c["Modern's brightness within a stop of Remix's (sky: blend, pbr_spheres)"] = \
        all(v is not None and 0.5 <= v <= 2.0 for v in ratios.values())
    tests["Part B's items (RTX run)"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # the benchmarks, and where the crowd's frame goes
    print("RTX run: the benchmarks...", flush=True)
    bench = benchmarks(args, out, scenes, {})
    bench.update(partb_benchmarks(args, out, S, expected))
    bench.update(partc_benchmarks(args, out, S))
    cache_dir = os.path.join(out, "shader_cache_b82")
    shutil.rmtree(cache_dir, ignore_errors=True)
    starts, built = [], []
    for k in ("first", "second"):
        r = play(args.ours, out, "rtx_b82_%s" % k, S("glsl_features"), {"BGE_REMIX_SHADER_CACHE": cache_dir},
                 frames=60, gl_debug=False)
        starts.append(r.get("seconds_total"))
        m = re.findall(r"BGE-Remix shader cache: (\d+) loaded, (\d+) built", r.get("text") or "")
        built.append([int(x) for x in m[-1]] if m else None)
    bench["B8.2 start with the shader cache: seconds, [loaded, built]"] = {"first": [starts[0], built[0]],
                                                                         "second": [starts[1], built[1]]}
    prof = os.path.join(out, "rtx_crowd_categories.json")
    if os.path.isfile(prof):
        os.remove(prof)
    play(args.ours, out, "rtx_crowd_profile", S("crowd"), {"BGE_REMIX_P8_STATS": prof}, frames=90, gl_debug=False)
    bench["B8.7 the crowd's frame by category (ms, percent)"] = p2.read_json(prof)
    startup = os.path.join(out, "editor_startup.blend")
    if os.path.isfile(startup):
        os.remove(startup)
    blender_script(args.ours, out, "editor_startup", STARTUP_SAVE, [startup])
    U = editor_runs(args, out, S, expected, startup)
    bench.update(editor_benchmarks(U))
    c, d = judge_benchmarks(bench)
    # (both starts take about 0.6 s, below what the timing can tell apart:
    # faster means the second builds no shader the first built)
    c["B8.2 the second start builds no shader the first built (%s)" % built] = \
        None not in built and built[0][1] > 0 and built[1][1] == 0 and built[1][0] > 0
    tests["Benchmark targets (RTX run)"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # ColorCopy: a played route on Level 1, against stock 2.79
    print("RTX run: ColorCopy's played route...", flush=True)
    tests["ColorCopy (a played route)"] = run_colorcopy(args, out)

    # the other sheets: mt_features before (stock 2.79's Multitexture) and
    # after; the editors in stock 2.79 and in P8; the old scenes before and now
    sheets = {"scenes": sheet_scenes}
    if args.official and have("mt_features"):
        a = play(args.official, out, "rtx_stock_mt_features", S("mt_features"), frames=150, gl_debug=False)
        b = play(args.ours, out, "rtx_mt_features", S("mt_features"), frames=150, gl_debug=False)
        sheets["mt_features"] = p2.contact_sheet([("mt_features before: stock 2.79 Multitexture", a.get("screenshot")),
                                                  ("mt_features after: P8", b.get("screenshot"))],
                                                 os.path.join(out, "sheet_p8_rtx_mt_features.png"))
    items = []
    for screen in expected.get("editors_ui", {}).get("screens", []) + ["Default", "Default_edit"]:
        if any(screen == x[0].split(": ")[0] for x in items):
            continue
        items.append(("%s: stock 2.79" % screen, U["shot"].get(("stock", screen))))
        items.append(("%s: P8 (core)" % screen, U["shot"].get(("core", screen))))
    sheets["editors"] = p2.contact_sheet(items, os.path.join(out, "sheet_p8_rtx_editors.png"))
    old = []
    for phase, label, what in BEFORE:
        b = before.get(phase, {})
        old += [("%s: before P8" % what, b.get("before")), ("%s: P8" % what, b.get("now"))]
    sheets["before"] = p2.contact_sheet(old, os.path.join(out, "sheet_p8_rtx_before.png"))
    sheets["colorcopy"] = tests["ColorCopy (a played route)"]["detail"].get("sheet")

    res = {"tests": tests, "bench": bench, "sheets": sheets, "all_ok": all(t["ok"] for t in tests.values())}
    lines = ["BGE-Remix P8 RTX run (your RTX card, the real RTX Remix runtime)", "=" * 70, ""]
    for name, t in tests.items():
        lines.append("%-34s %s" % (name, p2.mark(t["ok"])))
        for check, ok in t["checks"].items():
            lines.append("      %s %s" % ("ok    " if ok else "FAILED", check))
    lines += ["", "Benchmarks:"]
    for name, v in bench.items():
        lines.append("  %-26s %s" % (name, json.dumps(v)))
    lines += ["", "Picture sheets for your sign-off:"]
    for name, path in sheets.items():
        lines.append("  %-12s %s" % (name, path))
    lines += ["", "ALL PASSED" if res["all_ok"] else "SOMETHING FAILED (see results_p8_rtx.json and the .log files)"]
    return res, lines


# ---------------------------------------------------------------------------
# P8.30: ColorCopy (C:\BGE-Remix\colorcopy\ColorCopy, a finished BGE 2.79
# puzzle game; its .blend files are "all rights reserved", so the game
# stays on this PC and only this test is in the repository). A test copy
# of Level 1 (beside it, so its links to the pieces, textures and sounds
# hold) gets a probe that holds W, A, S or D on fixed logic ticks through
# a stand-in for bge.logic.keyboard: the same route, tick for tick, in our
# build and in stock 2.79

COLORCOPY_DIR = r"C:\BGE-Remix\colorcopy\ColorCopy"
COLORCOPY_LEVEL = "Level1"
COLORCOPY_TEST = "Level1_bgeremix_test"  # the test copy, beside the level
# (key, the tick it is pressed): held for 2 ticks; a step takes 20 ticks,
# the cube's place is read 40 ticks after each press
COLORCOPY_ROUTE = [(k, 150 + 45 * i) for i, k in enumerate("WWDDSSAAWDSA")]
COLORCOPY_SHOTS = [120, 150 + 45 * 4, 150 + 45 * 8, 150 + 45 * 12]
COLORCOPY_TICKS = 150 + 45 * 12 + 30
TOL_COLORCOPY = 6.0  # a ColorCopy picture against stock 2.79's (blurred)

COLORCOPY_PROBE = r"""
# BGE-Remix P8.30: ColorCopy's route probe (made by run_p8.py)
import json, os, time
import bge

ROUTE = %(route)r
SHOTS = %(shots)r
TICKS = %(ticks)d
KEYS = {"W": bge.events.WKEY, "A": bge.events.AKEY, "S": bge.events.SKEY, "D": bge.events.DKEY}
g = bge.logic.globalDict
cc = g.get("bgeremix_cc")
if cc is None:
    cc = g["bgeremix_cc"] = {"tick": 0, "held": None, "times": [], "steps": [], "shots": []}

    class Keys(object):
        # bge.logic.keyboard, with W, A, S and D held as the route says
        def __init__(self, real):
            self._real = real

        def _patch(self, ev):
            ev = dict(ev)
            for code in KEYS.values():
                ev[code] = bge.logic.KX_INPUT_NONE
            held = bge.logic.globalDict["bgeremix_cc"]["held"]
            if held is not None:
                ev[KEYS[held]] = bge.logic.KX_INPUT_ACTIVE
            return ev

        @property
        def events(self):
            return self._patch(self._real.events)

        @property
        def active_events(self):
            return {k: v for k, v in self._patch(self._real.events).items() if v != bge.logic.KX_INPUT_NONE}

        def __getattr__(self, name):
            return getattr(self._real, name)

    bge.logic.keyboard = Keys(bge.logic.keyboard)

cc["tick"] += 1
tick = cc["tick"]
cc["times"].append(time.perf_counter())
scene = bge.logic.getCurrentScene()
cube = scene.objects.get("cubo")


def where():
    if cube is None:
        return None
    return {"pos": [round(v, 3) for v in cube.worldPosition],
            "rot": [round(v, 3) for v in cube.worldOrientation.to_euler()],
            "color": [round(v, 3) for v in cube.color]}


cc["held"] = None
for i, (key, at) in enumerate(ROUTE):
    if at <= tick < at + 2:
        cc["held"] = key
    if tick == at - 1:
        cc["steps"].append({"key": key, "before": where()})
    if tick == at + 40 and i < len(cc["steps"]):
        cc["steps"][i]["after"] = where()
if tick in SHOTS:
    base = os.environ.get("BGE_REMIX_CC_SHOT")
    if base:
        path = "%%s_%%d.png" %% (base, SHOTS.index(tick))
        bge.render.makeScreenshot(path)
        cc["shots"].append(path)
if tick >= TICKS:
    t = cc["times"][60:]
    d = sorted(b - a for a, b in zip(t, t[1:]))
    out = {"ticks": tick, "steps": cc["steps"], "shots": cc["shots"], "cube_found": cube is not None,
           "scene": scene.name, "frame_ms_median": 1000.0 * d[len(d) // 2] if d else None,
           "frame_ms_p95": 1000.0 * d[int(len(d) * 0.95)] if d else None}
    path = os.environ.get("BGE_REMIX_CC_OUT")
    if path:
        with open(path, "w") as f:
            json.dump(out, f, indent=2)
    bge.logic.endGame()
"""

COLORCOPY_SETUP = r"""
# the ColorCopy test copy: the level with the route probe (run_p8.py)
import bpy, json, sys
argv = sys.argv[sys.argv.index("--") + 1:]
src, dst, info_path, probe_path = argv
bpy.ops.wm.open_mainfile(filepath=src)
scene = bpy.context.scene
probe = bpy.data.objects.new("BGERemixProbe", None)
scene.objects.link(probe)
scene.objects.active = probe
text = bpy.data.texts.new("bgeremix_cc_probe.py")
with open(probe_path, encoding="utf-8") as f:
    text.write(f.read())
bpy.ops.logic.sensor_add(type='ALWAYS', object=probe.name)
bpy.ops.logic.controller_add(type='PYTHON', object=probe.name)
sensor = probe.game.sensors[-1]
controller = probe.game.controllers[-1]
sensor.use_pulse_true_level = True
sensor.tick_skip = 0
controller.mode = 'SCRIPT'
controller.text = text
controller.use_priority = True
sensor.link(controller)
info = {"scene": scene.name, "scenes": [s.name for s in bpy.data.scenes],
        "libraries": [l.filepath for l in bpy.data.libraries],
        "cubes": [o.name for o in scene.objects if o.name.startswith("cubo")],
        "frame_rate": scene.game_settings.fps, "material_mode": scene.game_settings.material_mode}
bpy.ops.wm.save_as_mainfile(filepath=dst, check_existing=False, copy=True)
with open(info_path, "w") as f:
    json.dump(info, f, indent=2)
"""


def run_colorcopy(args, out):
    """the ColorCopy route in our build and in stock 2.79"""
    c, d = {}, {}
    game = getattr(args, "colorcopy", None) or COLORCOPY_DIR
    level = os.path.join(game, "levels", COLORCOPY_LEVEL + ".blend")
    d["game"] = game
    if not os.path.isfile(level):
        c["ColorCopy unpacked (%s)" % level] = False
        return {"ok": False, "checks": c, "detail": d}
    test = os.path.join(game, "levels", COLORCOPY_TEST + ".blend")
    probe = os.path.join(out, "cc_probe.py")
    with open(probe, "w", encoding="utf-8") as f:
        f.write(COLORCOPY_PROBE % {"route": COLORCOPY_ROUTE, "shots": COLORCOPY_SHOTS, "ticks": COLORCOPY_TICKS})
    info = os.path.join(out, "cc_info.json")
    for p in (test, info):
        if os.path.isfile(p):
            os.remove(p)
    blender_script(args.ours, out, "cc_setup", COLORCOPY_SETUP, [level, test, info, probe])
    d["level"] = p2.read_json(info)
    c["the test copy of Level 1 made"] = os.path.isfile(test)
    runs = {}
    builds = [("ours", args.ours)] + ([("stock", args.official)] if args.official else [])
    for kind, build in builds:
        res = os.path.join(out, "cc_%s.json" % kind)
        if os.path.isfile(res):
            os.remove(res)
        e = p2.base_env(out, "cc_" + kind)
        e.update({"BGE_REMIX_CC_OUT": res, "BGE_REMIX_CC_SHOT": os.path.join(out, "cc_%s" % kind),
                  "BGE_REMIX_RUNTIME_DIR": None})
        e.update(GL_ENV if kind == "ours" else {"BGE_REMIX_NO_DIALOGS": "1"})
        code, text, secs = p2.run([p2.exe(build, "blenderplayer"), "-w", str(SIZE[0]), str(SIZE[1]), test], e,
                                  os.path.join(out, "cc_%s.log" % kind), timeout=600, cwd=game)
        if kind == "ours":
            note_gl(build, "cc_ours", text)
        errors = [l for l in (text or "").splitlines() if "Traceback" in l or "Error:" in l or
                  "Python script error" in l]
        runs[kind] = {"exit": code, "seconds": secs, "result": p2.read_json(res) or {}, "python_errors": errors[:8],
                      "gl_errors": GL_ERROR.findall(text or "")[:5] if kind == "ours" else None}
    ours, stock = runs.get("ours", {}), runs.get("stock", {})
    r = ours.get("result", {})
    d["runs"] = {k: {x: v[x] for x in ("exit", "seconds", "python_errors", "gl_errors")} for k, v in runs.items()}
    c["Level 1 plays the whole route in our build (%d ticks)" % COLORCOPY_TICKS] = \
        ours.get("exit") == 0 and r.get("ticks", 0) >= COLORCOPY_TICKS
    c["no Python errors in our build"] = bool(ours) and not ours.get("python_errors")
    c["no OpenGL errors in our build"] = bool(ours) and not ours.get("gl_errors")
    steps = r.get("steps", [])
    moved = [s for s in steps if s.get("before") and s.get("after") and s["before"]["pos"] != s["after"]["pos"]]
    d["steps"] = {"ours": steps, "stock": stock.get("result", {}).get("steps")}
    c["the cube moves on the key presses (%d of %d steps)" % (len(moved), len(COLORCOPY_ROUTE))] = \
        r.get("cube_found") is True and len(moved) >= len(COLORCOPY_ROUTE) // 2
    d["frame_ms"] = {k: [v.get("result", {}).get("frame_ms_median"), v.get("result", {}).get("frame_ms_p95")]
                     for k, v in runs.items()}
    items = []
    if stock:
        sr = stock.get("result", {})
        d["stock_python_errors"] = stock.get("python_errors")

        def same_place(a, b):
            pa, pb = (a.get("after") or {}).get("pos"), (b.get("after") or {}).get("pos")
            return bool(pa) and bool(pb) and all(abs(x - y) < 1e-2 for x, y in zip(pa, pb))
        same = [same_place(a, b) for a, b in zip(steps, sr.get("steps", []))]
        c["the cube ends each step where it does in stock 2.79 (%d of %d)" % (sum(1 for x in same if x),
                                                                              len(COLORCOPY_ROUTE))] = \
            len(same) == len(COLORCOPY_ROUTE) and all(same)
        diffs = []
        for k in range(len(COLORCOPY_SHOTS)):
            a, b = os.path.join(out, "cc_ours_%d.png" % k), os.path.join(out, "cc_stock_%d.png" % k)
            pa, pb = picture(a), picture(b)
            diffs.append(whole_diff(blurred(pa, 2.0), blurred(pb, 2.0)) if pa is not None and pb is not None
                         else None)
            items += [("ColorCopy tick %d: stock 2.79" % COLORCOPY_SHOTS[k], b),
                      ("ColorCopy tick %d: P8" % COLORCOPY_SHOTS[k], a)]
        d["picture_differences"] = diffs
        c["the pictures along the route as stock 2.79's (within %.0f; %s)" % (TOL_COLORCOPY, diffs)] = \
            None not in diffs and all(x <= TOL_COLORCOPY for x in diffs)
    d["sheet"] = p2.contact_sheet(items, os.path.join(out, "sheet_p8_colorcopy.png")) if items else None
    return {"ok": all(c.values()), "checks": c, "detail": d}


# ---------------------------------------------------------------------------
# P8.30 fix 5, step 1: the diagnostic RTX run (run_p8_diag.bat). Remix's
# pictures with one setting changed at a time, for the four differences
# left at the sign-off: dark worlds washed out, particles missing, many_lamps
# dim, the overlays' white squares

# P8.30 diagnostic run 3: the new defaults (automatic exposure off, light
# scale 1, 0 stops) beside the game; the particles' material four ways; the
# overlays with a real bitmap font
PARTICLE_MATS = [("lit and glowing (default)", {}), ("lit, as before", {"BGE_REMIX_PARTICLE_MAT": "lit"}),
                 ("glow", {"BGE_REMIX_PARTICLE_MAT": "glow"}),
                 ("glow, not blended", {"BGE_REMIX_PARTICLE_MAT": "glow_noblend"})]
DIAG = [
    # (row, game picture's scene, Remix scene, [(label, environment)])
    ("classic_remix", "classic_remix_gl", "classic_remix_rtx", [("new defaults", {})]),
    ("shadows", "shadows", "shadows_rtx", [("new defaults", {})]),
    ("pbr_spheres", "pbr_spheres_gl", "pbr_spheres_rtx", [("new defaults", {})]),
    ("sky: blend", "world_sky_blend_classic", "world_sky_blend_remix_rtx", [("new defaults", {})]),
    ("hair_particles", "hair_particles", "hair_particles_remix_rtx", PARTICLE_MATS),
    ("transparency (smoke)", "transparency", "transparency_rtx", PARTICLE_MATS),
    ("game_overlays", "game_overlays", "game_overlays_remix_rtx", [("now", {})]),
]
DIAG_NOTES = ("BGE_REMIX_", "BGE-Remix particles", "Remix's fog", "camera effects to Remix")


def run_diag(args, out):
    scenes, expected = make_scenes(args, out)
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    have = lambda n: bool(n) and os.path.isfile(S(n))  # noqa: E731
    names = sorted({r[2][:-4] for r in DIAG if r[2].endswith("_rtx")})
    blender_script(args.ours, out, "remix_variants", REMIX_VARIANT, [scenes] + names)
    items, detail = [], {}
    for row, game, remix, variants in DIAG:
        key = re.sub(r"[^a-z0-9]+", "_", row.lower()).strip("_")
        if game:
            r = play(args.ours, out, "diag_%s_game" % key, S(game), frames=110, gl_debug=False) if have(game) else {}
            items.append(("%s: the game" % row, r.get("screenshot")))
            shot = r.get("screenshot")
            detail.setdefault(row, {})["game"] = {"picture": p2.has_picture(shot)[1] if shot else None,
                                                  "brightness": round(linear_mean(shot), 3)
                                                  if shot and linear_mean(shot) is not None else None}
        for label, env in variants:
            vkey = re.sub(r"[^a-z0-9]+", "_", label.lower()).strip("_")
            tag = "diag_%s_%s" % (key, vkey)
            r = play(args.ours, out, tag, S(remix), dict(REMIX_ENV, **env), frames=REMIX_FRAMES, gl_debug=False) \
                if have(remix) else {}
            shot = r.get("screenshot")
            items.append(("%s: Remix, %s" % (row, label), shot))
            st = r.get("stats", {})
            v = {"env": env, "running": st.get("running"), "errors": st.get("errors"),
                 "picture": p2.has_picture(shot)[1] if shot else None,
                 "lights": st.get("lights_last_frame"), "gpu_emitters": st.get("gpu_emitters"),
                 "notes": [l for l in (r.get("text") or "").splitlines() if l.startswith(DIAG_NOTES)
                           or "BGE-Remix: BGE_REMIX" in l or "BGE-Remix particles" in l][:6]}
            v["brightness"] = round(linear_mean(shot), 3) if shot and linear_mean(shot) is not None else None
            detail.setdefault(row, {})[label] = v
    # the earlier phases' rooms with the new defaults
    rooms = rtx_before(args, out, args.before)
    cycles = os.path.join(args.before, "p2", "cycles_room.png") if args.before else None
    for phase, label, what in BEFORE:
        b = rooms.get(phase, {})
        row = [("%s: before P8" % what, b.get("before"))]
        if phase == "p2":
            row.append(("%s: Cycles" % what, cycles))
        row.append(("%s: new defaults" % what, b.get("now")))
        items += row
        detail["room " + phase] = {
            "difference_from_before": {lab: picture_diff(path, b.get("before")) if path and b.get("before") else None
                                       for lab, path in row[1:]},
            "from_cycles": {lab: picture_diff(path, cycles) if path and cycles else None for lab, path in row[1:]}
            if phase == "p2" else None,
            "brightness": {lab: round(linear_mean(path), 3) if path and linear_mean(path) is not None else None
                           for lab, path in row}}
    sheet = p2.contact_sheet(items, os.path.join(out, "sheet_p8_diag.png"))
    ran = sum(1 for key, row in detail.items() if not key.startswith("room ")
              for k, v in row.items() if k != "game" and isinstance(v, dict) and v.get("running"))
    total = sum(len(r[3]) for r in DIAG)
    lines = ["BGE-Remix P8.30 diagnostic RTX run", "=" * 70, "",
             "Remix pictures with one setting changed at a time: %d of %d ran" % (ran, total), "",
             "Picture sheet: %s" % sheet, "", "ALL PASSED" if ran == total else "SOMETHING FAILED (see results_p8_diag.json)"]
    return {"detail": detail, "sheet": sheet, "all_ok": ran == total}, lines


def cell_ratios(a, cells):
    """each lamp cell's linear brightness over the whole picture's"""
    if a is None:
        return {}
    lin = srgb_to_linear(a)
    lum = 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]
    whole = float(lum.mean()) or 1e-6
    res = {}
    for name, cell in cells.items():
        left, top, right, bottom = cell["rect"]
        part = lum[max(top, 0):max(bottom, 0), max(left, 0):max(right, 0)]
        if part.size:
            res[name] = float(part.mean()) / whole
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--official")
    ap.add_argument("--mock")
    ap.add_argument("--rtx", action="store_true", help="the RTX run: the real RTX Remix runtime (P8.30)")
    ap.add_argument("--before", help="the earlier phases' RTX results (C:\\BGE-Remix\\results)")
    ap.add_argument("--diag", action="store_true", help="the diagnostic RTX run (P8.30 fix 5)")
    ap.add_argument("--colorcopy", help="ColorCopy's folder (default %s)" % COLORCOPY_DIR)
    ap.add_argument("--colorcopy-only", action="store_true", help="only ColorCopy's played route")
    args = ap.parse_args()
    if not args.rtx and not args.mock and not args.diag and not args.colorcopy_only:
        ap.error("--mock is needed (the fake RTX Remix runtime) unless --rtx")
    args.ours = os.path.abspath(args.ours)
    OURS["build"] = args.ours
    if args.official:
        args.official = os.path.abspath(args.official)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    if args.colorcopy_only:
        t = run_colorcopy(args, out)
        res = {"tests": {"ColorCopy (a played route)": t}, "all_ok": t["ok"]}
        lines = ["BGE-Remix P8.30: ColorCopy's played route", "=" * 70, ""]
        for check, ok in t["checks"].items():
            lines.append("  %s %s" % ("ok    " if ok else "FAILED", check))
        lines += ["", "Picture sheet: %s" % t["detail"].get("sheet"), "",
                  "ALL PASSED" if t["ok"] else "SOMETHING FAILED (see results_p8_colorcopy.json and cc_*.log)"]
        name = "p8_colorcopy"
    elif args.diag:
        res, lines = run_diag(args, out)
        name = "p8_diag"
    else:
        res, lines = run_rtx(args, out) if args.rtx else run(args, out)
        name = "p8_rtx" if args.rtx else "p8a"
    with open(os.path.join(out, "results_%s.json" % name), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, "summary_%s.txt" % name), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
