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
"""

import argparse
import json
import os
import re
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
if rc is None:
    rc = g["bgeremix_rc"] = {"tick": 0, "times": [], "shots": [], "car": [], "speed_max": 0.0,
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
if tick >= %(ticks)d:
    t = rc["times"][60:]
    d = sorted(b - a for a, b in zip(t, t[1:]))
    out = {"ticks": tick, "shots": rc["shots"], "car": rc["car"], "speed_max": round(rc["speed_max"], 2),
           "car_found": car is not None, "scene": scene.name,
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
level, folder, probe_path, info_path = argv
VARIANTS = ["plain", "no_water", "no_filters", "no_damage"]
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


def play(build, out, tag, blend, kind, game, env_extra, route, owners):
    res = os.path.join(out, tag + ".json")
    stats = os.path.join(out, tag + "_stats.json")
    for p in (res, stats):
        if os.path.isfile(p):
            os.remove(p)
    e = p2.base_env(out, tag)
    e.update({"BGE_REMIX_RALLY_OUT": res, "BGE_REMIX_RALLY_SHOT": os.path.join(out, tag),
              "BGE_REMIX_RALLY_ROUTE": json.dumps(route), "BGE_REMIX_RALLY_OWNERS": json.dumps(owners),
              "BGE_REMIX_STATS": stats, "BGE_REMIX_RUNTIME_DIR": None})
    cmd = [p2.exe(build, "blenderplayer"), "-w", str(p8.SIZE[0]), str(p8.SIZE[1]), blend]
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
    return {"exit": code, "seconds": round(time.time() - t0, 1), "ticks": r.get("ticks", 0),
            "finished": r.get("ticks", 0) >= TICKS, "crash": crash_excerpt(text),
            "speed_max": r.get("speed_max"), "car": r.get("car"), "shots": r.get("shots", []),
            "frame_ms_median": r.get("frame_ms_median"), "frame_ms_p95": r.get("frame_ms_p95"),
            "errors": [m.group(0) for m in ERROR_LINE.finditer(text)][:12],
            "gl_errors": p8.GL_ERROR.findall(text)[:12], "gl_error_count": len(p8.GL_ERROR.findall(text)),
            "bge_remix_lines": sorted(set(BGE_LINE.findall(text)))[:40],
            "remix": {k: st.get(k) for k in ("running", "errors", "draws_last_frame", "lights_last_frame",
                                             "textures_failed", "live_texture_updates", "game_ms_median")}
            if st else None}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--official")
    ap.add_argument("--out", required=True)
    ap.add_argument("--game", help="Rally Cross's folder (default: %s)" % " or ".join(GAME_DIRS))
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
    p8.blender_script(args.ours, out, "rally_setup", SETUP, [level, folder, probe, info_path], timeout=1800)
    info = p2.read_json(info_path) or {}
    keys, owners = info.get("keys", {}), info.get("owners", {})
    route = [(keys[name], a, b) for name, a, b in ROUTE if name in keys]
    runs = {}
    plan = []
    if args.official:
        plan.append(("stock_plain", args.official, "stock", "rally_test_plain", {}))
    for variant in ("plain", "no_water", "no_filters", "no_damage"):
        plan.append(("ours_%s" % variant, args.ours, "ours", "rally_test_%s" % variant, {}))
    for variant in ("plain", "no_water", "no_filters", "no_damage"):
        plan.append(("ours_remix_%s" % variant, args.ours, "ours", "rally_test_%s_remix" % variant, {}))
    plan.append(("ours_remix_no_live", args.ours, "ours", "rally_test_plain_remix", {"BGE_REMIX_LIVE_TEXTURE_HZ": "0"}))
    for tag, build, kind, name, env in plan:
        blend = os.path.join(folder, name + ".blend")
        if not os.path.isfile(blend):
            runs[tag] = {"missing": blend}
            continue
        print("Rally Cross: %s" % tag, flush=True)
        runs[tag] = play(build, out, tag, blend, kind, game, env, route, owners)
    # the test copies go again
    for name in info.get("variants", {}):
        p = os.path.join(folder, name + ".blend")
        if os.path.isfile(p):
            os.remove(p)
    # pictures: each run's three shots side by side
    items = []
    for tag in runs:
        for i, tick in enumerate(SHOTS):
            path = os.path.join(out, "%s_%d.png" % (tag, i))
            items.append(("%s, tick %d" % (tag, tick), path if os.path.isfile(path) else None))
    sheet = p2.contact_sheet(items, os.path.join(out, "sheet_rally.png")) if items else None
    result = {"game": game, "info": info, "route": route, "runs": runs, "sheet": sheet}
    with open(os.path.join(out, "results_rally.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    lines += ["Game: %s" % game, "Route (key, from tick, to tick): %s" % route, ""]
    lines.append("%-24s %-8s %-6s %-9s %-7s %-8s %s" % ("run", "result", "ticks", "speed max", "GL errs",
                                                        "frame ms", "first problem"))
    for tag, r in runs.items():
        if "missing" in r:
            lines.append("%-24s missing (%s)" % (tag, r["missing"]))
            continue
        state = "CRASH" if r["crash"] else ("ok" if r["finished"] else "stopped")
        first = (r["crash"] or [None])[1] if r["crash"] and len(r["crash"]) > 1 else \
            ((r["errors"] or [""])[0] or (r["gl_errors"] or [""])[0])
        lines.append("%-24s %-8s %-6s %-9s %-7s %-8s %s" % (
            tag, state, r["ticks"], r["speed_max"], r["gl_error_count"],
            "%.2f" % r["frame_ms_median"] if r["frame_ms_median"] else "-", (first or "")[:90]))
    lines += ["", "Picture sheet: %s" % sheet, "Details: results_rally.json and each run's .log", "",
              "FINISHED"]
    print("\n".join(lines))
    with open(os.path.join(out, "summary_rally.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
