"""BGE-Remix P9.0: Rally Cross, a second real game (run_rally.bat).

Rally Cross BGE (Blend Swap #84139, CC BY 3.0, saved by Blender 2.77) plays
in stock 2.79b but glitches and crashes in BGE-Remix, and crashes at once
with RTX Remix on. The game stays on this PC (C:\\BGE-Remix\\rallycross, or
the Downloads folder it came in); only this script is in the repository.

What it does: a test copy of the level (beside it) where the car is driven by
simulated keys (the game's keyboard sensors become property sensors a probe
sets: throttle held, a left and a right turn, a brake), played for 15 seconds
in stock 2.79b, in our build (OpenGL) and in our build with RTX Remix; our
runs under Windows' console debugger, so a crash prints where it happened.
Each of our two ways is played again with one of the game's suspects
switched off: the water (its two bge.texture renders, clip planes and GLSL
shader), the 2D filters, the damage (shape keys); with Remix also without
Remix's live bge.texture pictures. The answer: which run crashes or
differs, and where.

--diag (run_local.bat rallydiag, run_rally_diag.bat): the glitch
diagnostics. Stock and ours (OpenGL) as they are, with the sun's shadow off
and with the water's surface hidden; Remix as it is, without the water's
scripts, without the 2D filters and without the water's surface; ours once
more writing the sun's shadow map out (BGE_REMIX_SHADOW_DUMP). The probe
records the camera and the lamps at each picture.

--diag2 (run_local.bat rallydiag2, run_rally_diag2.bat): OpenGL only. Ours
with each of P8's techniques switched off in turn (BGE_REMIX_NO_<name>) and
all of them together; stock and ours with groups of the level's objects
hidden (the two domes, two planes, the trees, the posts) and with the
ground's node material off.
"""

import argparse
import json
import os
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p8"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
import run_p2 as p2  # noqa: E402  (running Blender, contact sheets)
import run_p8 as p8  # noqa: E402  (the debugger, OpenGL debug output)

GAME_DIRS = [r"C:\BGE-Remix\rallycross", r"C:\Users\ayhan\Downloads\Rally Cross BGE"]
LEVEL = os.path.join("RG_update_blend", "RG_update.blend")
TICKS = 900                # 15 seconds at the game's 60 ticks
SHOTS = [120, 420, 840]
# (sensor name, first tick, last tick): which of the car's keys is held when
ROUTE = [("Gas", 60, 880), ("Left", 300, 360), ("Right", 500, 560), ("Brake", 700, 760)]
TIMEOUT = 600

PROBE = r"""
# BGE-Remix P9.0: Rally Cross's driving probe (made by run_rally.py)
import json, os, time
import bge

g = bge.logic.globalDict
rc = g.get("bgeremix_rc")
run_id = os.environ.get("BGE_REMIX_RALLY_RUN", "")
if rc is None or rc.get("run") != run_id:
    rc = g["bgeremix_rc"] = {"run": run_id, "tick": 0, "times": [], "shots": [], "car": [], "speed_max": 0.0,
                             "route": json.loads(os.environ.get("BGE_REMIX_RALLY_ROUTE", "[]")),
                             "owners": json.loads(os.environ.get("BGE_REMIX_RALLY_OWNERS", "{}"))}
rc["tick"] += 1
tick = rc["tick"]
rc["times"].append(time.perf_counter())
scene = bge.logic.getCurrentScene()
held = set(key for key, a, b in rc["route"] if a <= tick <= b)
for key, owners in rc["owners"].items():
    for name in owners:
        ob = scene.objects.get(name)
        if ob is not None:
            ob["bgeremix_k_" + key] = 1 if key in held else 0
car = scene.objects.get("Cube")
if car is not None:
    try:
        speed = abs(float(car.get("speed", 0.0)))
    except (TypeError, ValueError):
        speed = 0.0
    rc["speed_max"] = max(rc["speed_max"], speed)
    if tick %% 60 == 0:
        rc["car"].append({"tick": tick, "pos": [round(v, 2) for v in car.worldPosition], "speed": round(speed, 2)})
SHOTS = %(shots)r
if tick in SHOTS:
    base = os.environ.get("BGE_REMIX_RALLY_SHOT")
    if base:
        path = "%%s_%%d.png" %% (base, SHOTS.index(tick))
        bge.render.makeScreenshot(path)
        rc["shots"].append(path)
        # P9.0 diagnostics: where the camera and the lamps were
        cam = scene.active_camera
        views = {"tick": tick, "camera": None, "lamps": {}}
        if cam is not None:
            views["camera"] = {"name": cam.name, "matrix": [list(r) for r in cam.worldTransform],
                               "projection": [list(r) for r in cam.projection_matrix],
                               "clip": [cam.near, cam.far]}
        for lamp in scene.lights:
            views["lamps"][lamp.name] = {"type": lamp.type, "matrix": [list(r) for r in lamp.worldTransform]}
        rc.setdefault("views", []).append(views)
        # P8.1: the engine's revs (the rev counter's bars) at each picture
        gb = scene.objects.get("Gearbox")
        try:
            rc.setdefault("rpm", []).append(round(float(gb["rpm"]), 1) if gb is not None else None)
        except (KeyError, TypeError, ValueError):
            rc.setdefault("rpm", []).append(None)
if tick >= int(os.environ.get("BGE_REMIX_RALLY_TICKS", "%(ticks)d")):
    t = rc["times"][60:]
    d = sorted(b - a for a, b in zip(t, t[1:]))
    out = {"ticks": tick, "shots": rc["shots"], "car": rc["car"], "speed_max": round(rc["speed_max"], 2),
           "car_found": car is not None, "scene": scene.name, "views": rc.get("views", []), "rpm": rc.get("rpm", []),
           "frame_ms_median": 1000.0 * d[len(d) // 2] if d else None,
           "frame_ms_p95": 1000.0 * d[int(len(d) * 0.95)] if d else None}
    path = os.environ.get("BGE_REMIX_RALLY_OUT")
    if path:
        with open(path, "w") as f:
            json.dump(out, f, indent=2)
    bge.logic.endGame()
"""

# the test copies: argv = level, folder, probe, info; each copy is
# rally_test_<variant>.blend beside the level
SETUP = r"""
import bpy, json, os, sys
argv = sys.argv[sys.argv.index("--") + 1:]
level, folder, probe_path, info_path, variants = argv
VARIANTS = json.loads(variants)
WATER = ("watertexture.py", "watershader_high")
DAMAGE = ("damage_controller",)
info = {"variants": {}, "keys": {}, "owners": {}, "switched_off": {}}
for variant in VARIANTS:
    for remix in (False, True):
        bpy.ops.wm.open_mainfile(filepath=level)
        scene = bpy.context.scene
        info["scene"] = scene.name
        info["scenes"] = [s.name for s in bpy.data.scenes]
        # the keyboard sensors become property sensors on bgeremix_k_<key>
        for ob in list(scene.objects):
            for i in range(len(ob.game.sensors)):
                s = ob.game.sensors[i]
                if s.type != 'KEYBOARD' or s.use_all_keys or s.key == 'NONE':
                    continue
                key, name = s.key, s.name
                info["keys"].setdefault(name, key)
                owners = info["owners"].setdefault(key, [])
                if ob.name not in owners:
                    owners.append(ob.name)
                prop = "bgeremix_k_" + key
                if prop not in ob.game.properties:
                    scene.objects.active = ob
                    bpy.ops.object.game_property_new(type='INT', name=prop)
                s.type = 'PROPERTY'
                s = ob.game.sensors[i]
                s.evaluation_type = 'PROPEQUAL'
                s.property = prop
                s.value = "1"
        off = []
        for ob in scene.objects:
            for c in ob.game.controllers:
                text = getattr(c, "text", None)
                tname = text.name if text else ""
                if (variant == "no_water" and tname in WATER) or (variant == "no_damage" and tname in DAMAGE):
                    try:
                        c.active = False
                    except AttributeError:
                        for s in list(c.link_sensors) if hasattr(c, "link_sensors") else []:
                            s.unlink(c)
                    off.append("%s: controller %s (%s)" % (ob.name, c.name, tname))
            for a in ob.game.actuators:
                if variant == "no_filters" and a.type == 'FILTER_2D':
                    try:
                        a.active = False
                    except AttributeError:
                        a.mode = 'DISABLE'
                    off.append("%s: 2D filter %s" % (ob.name, a.name))
        if variant == "no_sun_shadow":
            # P9.0 diagnostics: the sun lamps cast no shadow
            for la in bpy.data.lamps:
                if la.type == 'SUN' and la.shadow_method != 'NOSHADOW':
                    la.shadow_method = 'NOSHADOW'
                    off.append("lamp %s: shadow" % la.name)
        if variant == "no_water_plane":
            # P9.0 diagnostics: the water's surface (and its scripts) on
            # layer 20, which the scene does not show
            for ob in list(scene.objects):
                if any(getattr(c, "text", None) is not None and c.text.name in WATER for c in ob.game.controllers):
                    ob.layers = [i == 19 for i in range(20)]
                    off.append("%s: moved to layer 20" % ob.name)
        # P9.0 diagnostics 2: groups of the level's objects hidden (moved
        # to layer 20), the ground's node material off
        groups = {"no_domes": ("Sphere",), "no_planes": ("Plane.000", "Plane.004"), "no_trees": ("Three",),
                  "no_circles": ("Circle.",)}
        if variant in groups:
            for ob in list(scene.objects):
                if ob.name.startswith(groups[variant]) and ob.type == 'MESH':
                    ob.layers = [i == 19 for i in range(20)]
                    off.append(ob.name)
        if variant == "ground_simple":
            for ma in bpy.data.materials:
                if ma.use_nodes and ma.name == "Ground":
                    ma.use_nodes = False
                    off.append("material %s: nodes" % ma.name)
        info["switched_off"][variant] = off
        if remix:
            for sc in bpy.data.scenes:
                sc["bge_remix"] = 1
        # the probe
        probe = bpy.data.objects.new("BGERemixProbe", None)
        scene.objects.link(probe)
        scene.objects.active = probe
        text = bpy.data.texts.new("bgeremix_rc_probe.py")
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
        name = "rally_test_%s%s" % (variant, "_remix" if remix else "")
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, name + ".blend"), check_existing=False, copy=True)
        info["variants"][name] = True
info["frame_rate"] = bpy.context.scene.game_settings.fps
info["material_mode"] = bpy.context.scene.game_settings.material_mode
with open(info_path, "w") as f:
    json.dump(info, f, indent=2)
"""

# Inside Blender (the user's way: open the file, press P): blender.exe opens
# the test copy and starts the game in the 3D view once per entry of
# BGE_REMIX_RALLY_SEQUENCE ([tag, rasterizer]), then quits
EMBEDDED = r"""
import json, os
import bpy
out_dir = os.environ["BGE_REMIX_RALLY_DIR"]
seq = json.loads(os.environ["BGE_REMIX_RALLY_SEQUENCE"])
state = {"i": 0, "ticks": 0, "results": []}

def view3d():
    win = bpy.context.window_manager.windows[0]
    for area in win.screen.areas:
        if area.type == 'VIEW_3D':
            region = [r for r in area.regions if r.type == 'WINDOW'][0]
            return {"window": win, "screen": win.screen, "area": area, "region": region,
                    "scene": bpy.context.scene}
    return None

class RallyEmbedded(bpy.types.Operator):
    bl_idname = "wm.bge_remix_rally_embedded"
    bl_label = "BGE-Remix Rally Cross embedded"
    _timer = None

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        state["ticks"] += 1
        if state["ticks"] < 10:
            return {'PASS_THROUGH'}
        if state["i"] < len(seq):
            tag, mode = seq[state["i"]][0], seq[state["i"]][1]
            ticks = seq[state["i"]][2] if len(seq[state["i"]]) > 2 else 0
            state["i"] += 1
            if ticks:
                os.environ["BGE_REMIX_RALLY_TICKS"] = str(ticks)
            for sc in bpy.data.scenes:
                sc.bge_remix = mode
            os.environ["BGE_REMIX_RALLY_RUN"] = tag
            os.environ["BGE_REMIX_RALLY_OUT"] = os.path.join(out_dir, tag + ".json")
            os.environ["BGE_REMIX_RALLY_SHOT"] = os.path.join(out_dir, tag)
            os.environ["BGE_REMIX_STATS"] = os.path.join(out_dir, tag + "_stats.json")
            print("Rally probe: starting %s (%s)" % (tag, mode), flush=True)
            try:
                bpy.ops.view3d.game_start(view3d())
                state["results"].append([tag, "returned"])
            except Exception as ex:
                state["results"].append([tag, repr(ex)])
            print("Rally probe: %s returned to Blender" % tag, flush=True)
            with open(os.path.join(out_dir, "embedded_steps.json"), "w") as f:
                json.dump(state["results"], f, indent=2)
            return {'PASS_THROUGH'}
        context.window_manager.event_timer_remove(self._timer)
        context.user_preferences.view.use_quit_dialog = False
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}

bpy.utils.register_class(RallyEmbedded)
bpy.ops.wm.bge_remix_rally_embedded('INVOKE_DEFAULT')
"""
# (session, test copy, [(run, rasterizer)]): one Blender per session
SESSIONS = [
    ("in_blender_opengl_then_remix", "rally_test_plain",
     [("blender_opengl", "OPENGL"), ("blender_remix", "RTX_REMIX"), ("blender_opengl_again", "OPENGL"),
      ("blender_remix_again", "RTX_REMIX")]),
    ("in_blender_remix_first", "rally_test_plain", [("blender_remix_first", "RTX_REMIX")]),
    ("in_blender_remix_no_water", "rally_test_no_water", [("blender_remix_no_water", "RTX_REMIX")]),
    ("in_blender_remix_no_filters", "rally_test_no_filters", [("blender_remix_no_filters", "RTX_REMIX")]),
]

# P9.0, the second Rally Cross run: Remix crashed inside Blender at the
# whole 3D view's size (2566 x 1442), 48 seconds into its picture, after an
# OpenGL game; the standalone runs (960 x 540, 15 seconds) never did. These
# runs (run_local.bat rallybig) separate size, length and Blender:
# (tag, test copy, width, height, ticks)
BIG_PLAYER = [("ours_remix_big", "rally_test_plain_remix", 2560, 1440, 3600),
              ("ours_remix_long", "rally_test_plain_remix", 960, 540, 3600)]
# (session, test copy, Blender's window [x, y, w, h], [(run, rasterizer, ticks)])
BIG_SESSIONS = [
    ("in_blender_big", "rally_test_plain", [0, 0, 2600, 1500],
     [("blender_big_opengl", "OPENGL", 900), ("blender_big_remix", "RTX_REMIX", 3600)]),
]

# the test copies' variants: the ordinary run's, and the diagnostics'
VARIANTS = ["plain", "no_water", "no_filters", "no_damage"]
DIAG_VARIANTS = ["plain", "no_water", "no_filters", "no_sun_shadow", "no_water_plane"]
DIAG2_VARIANTS = ["plain", "no_domes", "no_planes", "no_trees", "no_circles", "ground_simple"]
# our build's P8 techniques, each switched off (BGE_REMIX_NO_<name>)
DIAG2_SWITCHES = ["instancing", "gpu_culling", "gpu_skinning", "layered_shadows", "clustered_lights",
                  "screen_effects", "texture_cache", "tessellation", "oit"]


def diag_plan(args, out):
    """P9.0 glitch diagnostics: each suspect off in stock and ours (OpenGL),
    Remix's pictures of each, and our sun's shadow map written out"""
    plan = []
    for variant in ("plain", "no_sun_shadow", "no_water_plane"):
        if args.official:
            plan.append(("stock_%s" % variant, args.official, "stock", "rally_test_%s" % variant, {}))
        plan.append(("ours_%s" % variant, args.ours, "ours", "rally_test_%s" % variant, {}))
    for variant in ("plain", "no_water", "no_filters", "no_water_plane"):
        plan.append(("ours_remix_%s" % variant, args.ours, "ours", "rally_test_%s_remix" % variant, {}))
    dump = os.path.join(out, "shadow_dump")
    shutil.rmtree(dump, ignore_errors=True)
    os.makedirs(dump, exist_ok=True)
    plan.append(("ours_shadow_dump", args.ours, "ours", "rally_test_plain", {"BGE_REMIX_SHADOW_DUMP": dump}))
    return plan


def diag2_plan(args, out):
    """P9.0 glitch diagnostics 2 (OpenGL only): our P8 techniques switched
    off one at a time and all together; groups of the level's objects hidden
    in stock and ours"""
    plan = []
    if args.official:
        plan.append(("stock_plain", args.official, "stock", "rally_test_plain", {}))
    plan.append(("ours_plain", args.ours, "ours", "rally_test_plain", {}))
    every = {}
    for name in DIAG2_SWITCHES:
        env = {"BGE_REMIX_NO_" + name.upper(): "1"}
        if name == "instancing":
            env["BGE_REMIX_GPU_INSTANCING"] = "0"
        every.update(env)
        plan.append(("ours_no_" + name, args.ours, "ours", "rally_test_plain", env))
    plan.append(("ours_none_of_them", args.ours, "ours", "rally_test_plain", every))
    for variant in DIAG2_VARIANTS[1:]:
        if args.official:
            plan.append(("stock_" + variant, args.official, "stock", "rally_test_" + variant, {}))
        plan.append(("ours_" + variant, args.ours, "ours", "rally_test_" + variant, {}))
    return plan


# P8.1: the blocks left at exit ("Error Totblock: n", then with the guarded
# allocator one line per block: "<name> len: <bytes> <address>")
TOTBLOCK = re.compile(r"Error Totblock: (\d+)")
BLOCK_LINE = re.compile(r"^(.*?) len: (\d+) (?:0x)?[0-9A-Fa-f]+", re.M)


def leaked_blocks(text):
    """the leaked blocks by name: {name: [count, bytes]}, most first"""
    m = TOTBLOCK.search(text or "")
    if not m:
        return {}
    out = {}
    for b in BLOCK_LINE.finditer(text[m.end():]):
        e = out.setdefault(b.group(1).strip(), [0, 0])
        e[0] += 1
        e[1] += int(b.group(2))
    return dict(sorted(out.items(), key=lambda kv: -kv[1][0]))


ERROR_LINE = re.compile(r"^.*(Traceback|Error:|Python script error|Segmentation|EXCEPTION_).*$", re.M)
BGE_LINE = re.compile(r"^BGE-Remix(?! GL error).*$", re.M)


def find_game(arg):
    for folder in ([arg] if arg else []) + GAME_DIRS:
        if folder and os.path.isfile(os.path.join(folder, LEVEL)):
            return folder
    return None


def crash_excerpt(text):
    """the debugger's report after the crash: the exception and the stack"""
    m = p8.CRASH_LINE.search(text or "")
    if not m:
        return None
    lines = text[m.start():].splitlines()
    keep = [l for l in lines if l.strip() and not l.startswith(("ModLoad", "start ", "    Image path"))]
    return keep[:90]


def play(build, out, tag, blend, kind, game, env_extra, route, owners, size=None, ticks=TICKS):
    res = os.path.join(out, tag + ".json")
    stats = os.path.join(out, tag + "_stats.json")
    for p in (res, stats):
        if os.path.isfile(p):
            os.remove(p)
    e = p2.base_env(out, tag)
    e.update({"BGE_REMIX_RALLY_OUT": res, "BGE_REMIX_RALLY_SHOT": os.path.join(out, tag),
              "BGE_REMIX_RALLY_ROUTE": json.dumps(route), "BGE_REMIX_RALLY_OWNERS": json.dumps(owners),
              "BGE_REMIX_STATS": stats, "BGE_REMIX_RUNTIME_DIR": None, "BGE_REMIX_RALLY_TICKS": str(ticks)})
    size = size or p8.SIZE
    cmd = [p2.exe(build, "blenderplayer"), "-w", str(size[0]), str(size[1]), blend]
    if kind == "ours":
        e.update(p8.GL_ENV)
        e.update(p8.REMIX_ENV)
        if os.path.isfile(p8.CDB):
            handlers = "; ".join('sxe -c "%s" %s' % (p8.ON_CRASH, ev) for ev in ("av", "c0000374", "c0000409"))
            cmd = [p8.CDB, "-G", "-sins", "-c", handlers + "; g"] + cmd
    else:
        e["BGE_REMIX_NO_DIALOGS"] = "1"
    e.update(env_extra or {})
    t0 = time.time()
    code, text, secs = p2.run(cmd, e, os.path.join(out, tag + ".log"), timeout=TIMEOUT, cwd=os.path.dirname(blend))
    text = text or ""
    r = p2.read_json(res) or {}
    st = p2.read_json(stats) or {}
    m = TOTBLOCK.search(text)
    return {"exit": code, "seconds": round(time.time() - t0, 1), "ticks": r.get("ticks", 0),
            "totblock": int(m.group(1)) if m else 0, "leaked": leaked_blocks(text),
            "finished": r.get("ticks", 0) >= ticks, "crash": crash_excerpt(text), "size": list(size),
            "speed_max": r.get("speed_max"), "car": r.get("car"), "shots": r.get("shots", []),
            "views": r.get("views", []), "rpm": r.get("rpm", []), "frame_ms_median": r.get("frame_ms_median"), "frame_ms_p95": r.get("frame_ms_p95"),
            "errors": [m.group(0) for m in ERROR_LINE.finditer(text)][:12],
            "gl_errors": p8.GL_ERROR.findall(text)[:12], "gl_error_count": len(p8.GL_ERROR.findall(text)),
            "bge_remix_lines": sorted(set(BGE_LINE.findall(text)))[:40],
            "remix": {k: st.get(k) for k in ("running", "errors", "draws_last_frame", "lights_last_frame",
                                             "textures_failed", "live_texture_updates", "game_ms_median")}
            if st else None}


def session(build, out, name, blend, seq, route, owners, window=None):
    """blender.exe with the test copy, the games started in its 3D view"""
    script = os.path.join(out, "rally_embedded.py")
    with open(script, "w", encoding="utf-8") as f:
        f.write(EMBEDDED)
    for entry in seq:
        tag = entry[0]
        for x in (".json", "_stats.json"):
            if os.path.isfile(os.path.join(out, tag + x)):
                os.remove(os.path.join(out, tag + x))
    tmp = os.path.join(out, "tmp_" + name)
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp, exist_ok=True)
    e = p2.base_env(out, name)
    e.update(p8.GL_ENV)
    e.update(p8.REMIX_ENV)
    e.update({"BGE_REMIX_RALLY_DIR": out, "BGE_REMIX_RALLY_SEQUENCE": json.dumps(seq),
              "BGE_REMIX_RALLY_ROUTE": json.dumps(route), "BGE_REMIX_RALLY_OWNERS": json.dumps(owners),
              "BGE_REMIX_RUNTIME_DIR": None, "BGE_REMIX_TRACE": "1", "TEMP": tmp, "TMP": tmp})
    cmd = [p2.exe(build, "blender")] + (["-p"] + [str(v) for v in window] if window else []) + \
        [blend, "--python", script]
    if os.path.isfile(p8.CDB):
        handlers = "; ".join('sxe -c "%s" %s' % (p8.ON_CRASH, ev) for ev in ("av", "c0000374", "c0000409"))
        cmd = [p8.CDB, "-G", "-sins", "-c", handlers + "; g"] + cmd
    print("Rally Cross: %s" % name, flush=True)
    t0 = time.time()
    code, text, secs = p2.run(cmd, e, os.path.join(out, name + ".log"), timeout=TIMEOUT * 2,
                              cwd=os.path.dirname(blend))
    text = text or ""
    crash_files = []
    for n in os.listdir(tmp):
        if n.endswith(".crash.txt") or n == "bge-remix.log":
            shutil.copy2(os.path.join(tmp, n), os.path.join(out, name + "_" + n))
            crash_files.append(name + "_" + n)
    steps = p2.read_json(os.path.join(out, "embedded_steps.json")) or []
    runs = {}
    for entry in seq:
        tag, mode = entry[0], entry[1]
        want = entry[2] if len(entry) > 2 else TICKS
        r = p2.read_json(os.path.join(out, tag + ".json")) or {}
        st = p2.read_json(os.path.join(out, tag + "_stats.json")) or {}
        runs[tag] = {"rasterizer": mode, "ticks": r.get("ticks", 0), "finished": r.get("ticks", 0) >= want,
                     "speed_max": r.get("speed_max"), "frame_ms_median": r.get("frame_ms_median"),
                     "shots": r.get("shots", []),
                     "remix": {k: st.get(k) for k in ("running", "mode", "errors", "draws_last_frame",
                                                      "live_texture_updates")} if st else None}
    return {"exit": code, "seconds": round(time.time() - t0, 1), "crash": crash_excerpt(text),
            "crash_files": crash_files, "steps": steps, "runs": runs,
            "errors": [m.group(0) for m in ERROR_LINE.finditer(text)][:12],
            "gl_error_count": len(p8.GL_ERROR.findall(text)), "gl_errors": p8.GL_ERROR.findall(text)[:12],
            "trace_tail": [l for l in text.splitlines() if l.startswith(("BGE-Remix trace", "Rally probe"))][-12:]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--official")
    ap.add_argument("--out", required=True)
    ap.add_argument("--game", help="Rally Cross's folder (default: %s)" % " or ".join(GAME_DIRS))
    ap.add_argument("--blender-only", action="store_true", help="only the games started inside Blender")
    ap.add_argument("--big", action="store_true", help="only the large and long Remix runs (rallybig)")
    ap.add_argument("--diag", action="store_true", help="the glitch diagnostics (rallydiag): the sun's shadow "
                    "off, the water's surface hidden, Remix pictures of each, a shadow dump")
    ap.add_argument("--diag2", action="store_true", help="the glitch diagnostics 2 (rallydiag2): our P8 "
                    "techniques off one at a time, groups of objects hidden")
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    args.ours = os.path.abspath(args.ours)
    p8.OURS["build"] = args.ours
    lines = ["BGE-Remix P9.0: Rally Cross diagnostic run", "=" * 70, ""]
    game = find_game(args.game)
    if not game:
        lines += ["Rally Cross was not found in %s" % " or ".join(GAME_DIRS), "", "SOMETHING FAILED"]
        print("\n".join(lines))
        with open(os.path.join(out, "summary_rally.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        return 1
    level = os.path.join(game, LEVEL)
    folder = os.path.dirname(level)
    probe = os.path.join(out, "rally_probe.py")
    with open(probe, "w", encoding="utf-8") as f:
        f.write(PROBE % {"shots": SHOTS, "ticks": TICKS})
    info_path = os.path.join(out, "rally_info.json")
    if os.path.isfile(info_path):
        os.remove(info_path)
    variants = DIAG2_VARIANTS if args.diag2 else DIAG_VARIANTS if args.diag else VARIANTS
    p8.blender_script(args.ours, out, "rally_setup", SETUP, [level, folder, probe, info_path, json.dumps(variants)],
                      timeout=1800)
    info = p2.read_json(info_path) or {}
    keys, owners = info.get("keys", {}), info.get("owners", {})
    route = [(keys[name], a, b) for name, a, b in ROUTE if name in keys]
    runs = {}
    plan = []
    if args.big or args.diag or args.diag2:
        args.blender_only = True  # (the ordinary runs left out)
    if args.diag:
        plan = diag_plan(args, out)
    if args.diag2:
        plan = diag2_plan(args, out)
    if args.official and not args.blender_only:
        plan.append(("stock_plain", args.official, "stock", "rally_test_plain", {}))
    for variant in ("plain", "no_water", "no_filters", "no_damage") if not args.blender_only else ():
        plan.append(("ours_%s" % variant, args.ours, "ours", "rally_test_%s" % variant, {}))
    for variant in ("plain", "no_water", "no_filters", "no_damage") if not args.blender_only else ():
        plan.append(("ours_remix_%s" % variant, args.ours, "ours", "rally_test_%s_remix" % variant, {}))
    if not args.blender_only:
        plan.append(("ours_remix_no_live", args.ours, "ours", "rally_test_plain_remix",
                     {"BGE_REMIX_LIVE_TEXTURE_HZ": "0"}))
        # P8.1: the blocks left at exit, by name (Blender's guarded allocator)
        plan.append(("ours_leaks", args.ours, "ours", "rally_test_plain", {"BGE_REMIX_GUARDED_ALLOC": "1"}))
        plan.append(("ours_remix_leaks", args.ours, "ours", "rally_test_plain_remix",
                     {"BGE_REMIX_GUARDED_ALLOC": "1"}))
    for tag, build, kind, name, env in plan:
        blend = os.path.join(folder, name + ".blend")
        if not os.path.isfile(blend):
            runs[tag] = {"missing": blend}
            continue
        print("Rally Cross: %s" % tag, flush=True)
        runs[tag] = play(build, out, tag, blend, kind, game, env, route, owners)
    for tag, name, w, h, ticks in (BIG_PLAYER if args.big else ()):
        blend = os.path.join(folder, name + ".blend")
        print("Rally Cross: %s (%dx%d, %d ticks)" % (tag, w, h, ticks), flush=True)
        runs[tag] = play(args.ours, out, tag, blend, "ours", game, {}, route, owners, size=(w, h), ticks=ticks) \
            if os.path.isfile(blend) else {"missing": blend}
    # inside Blender (the copies without Remix set: the session sets it)
    sessions = {}
    chosen = BIG_SESSIONS if args.big else [] if (args.diag or args.diag2) else [(n, c, None, q) for n, c, q in SESSIONS]
    for name, copy, window, seq in chosen:
        blend = os.path.join(folder, copy + ".blend")
        if os.path.isfile(blend):
            sessions[name] = session(args.ours, out, name, blend, seq, route, owners, window=window)
    # the test copies go again
    for name in info.get("variants", {}):
        p = os.path.join(folder, name + ".blend")
        if os.path.isfile(p):
            os.remove(p)
    # pictures: each run's three shots side by side
    items = []
    tags = list(runs) + [t for s in sessions.values() for t in s["runs"]]
    for tag in tags:
        for i, tick in enumerate(SHOTS):
            path = os.path.join(out, "%s_%d.png" % (tag, i))
            items.append(("%s, tick %d" % (tag, tick), path if os.path.isfile(path) else None))
    sheet = p2.contact_sheet(items, os.path.join(out, "sheet_rally.png")) if items else None
    result = {"game": game, "info": info, "route": route, "runs": runs, "in_blender": sessions, "sheet": sheet}
    with open(os.path.join(out, "results_rally.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    lines += ["Game: %s" % game, "Route (key, from tick, to tick): %s" % route, ""]
    lines.append("%-24s %-8s %-6s %-9s %-7s %-8s %-18s %s" % ("run", "result", "ticks", "speed max", "GL errs",
                                                              "frame ms", "revs at pictures", "first problem"))
    for tag, r in runs.items():
        if "missing" in r:
            lines.append("%-24s missing (%s)" % (tag, r["missing"]))
            continue
        state = "CRASH" if r["crash"] else ("ok" if r["finished"] else "stopped")
        first = (r["crash"] or [None])[1] if r["crash"] and len(r["crash"]) > 1 else \
            ((r["errors"] or [""])[0] or (r["gl_errors"] or [""])[0])
        lines.append("%-24s %-8s %-6s %-9s %-7s %-8s %-18s %s" % (
            tag, state, r["ticks"], r["speed_max"], r["gl_error_count"],
            "%.2f" % r["frame_ms_median"] if r["frame_ms_median"] else "-",
            "/".join("-" if v is None else "%.0f" % v for v in r.get("rpm") or []) or "-", (first or "")[:90]))
    lines += ["", "Blocks left at exit (Error Totblock): " + ", ".join(
        "%s %s" % (tag, r.get("totblock", 0)) for tag, r in runs.items() if "missing" not in r)]
    for tag in ("ours_leaks", "ours_remix_leaks"):
        r = runs.get(tag) or {}
        if r.get("leaked"):
            lines.append("  %s, by name (count, bytes):" % tag)
            for name, (n, size) in list(r["leaked"].items())[:40]:
                lines.append("    %-50s %5d %9d" % (name[:50], n, size))
    lines += ["", "Inside Blender (the game started in the 3D view, as with P):"]
    for name, sres in sessions.items():
        crashed = sres["crash"] or any(f.endswith(".crash.txt") for f in sres["crash_files"])
        state = "CRASH" if crashed else ("exit %s" % sres["exit"])
        lines.append("%-30s %-8s GL errors %-4s %s" % (name, state, sres["gl_error_count"],
                                                        ((sres["crash"] or [None, ""])[1] or "")[:80]))
        for tag, r in sres["runs"].items():
            lines.append("    %-26s %-10s ticks %-5s speed max %-6s frame ms %s" % (
                tag, r["rasterizer"], r["ticks"], r["speed_max"],
                "%.2f" % r["frame_ms_median"] if r["frame_ms_median"] else "-"))
    if args.diag or args.diag2:
        lines += ["", "Switched off in each test copy:"]
        for variant, off in sorted(info.get("switched_off", {}).items()):
            if variant != "plain" and variant in (DIAG2_VARIANTS if args.diag2 else DIAG_VARIANTS[3:]):
                lines.append("    %-16s %s" % (variant, "; ".join(off) or "NOTHING FOUND"))
        dump = os.path.join(out, "shadow_dump")
        files = sorted(os.listdir(dump)) if os.path.isdir(dump) else []
        lines.append("Shadow dump (%s): %s" % (dump, ", ".join(files) or "none written"))
    lines += ["", "Picture sheet: %s" % sheet, "Details: results_rally.json and each run's .log", "",
              "FINISHED"]
    print("\n".join(lines))
    with open(os.path.join(out, "summary_rally.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
