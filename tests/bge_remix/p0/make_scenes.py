# BGE-Remix P0 test harness: builds the three test scenes.
#
# Runs inside Blender 2.79b (Python 3.5: no f-strings).
#   blender --background --factory-startup --python make_scenes.py -- <out_dir>
#
# Writes:
#   t01_ui.blend      default scene, opened by the UI test (a file argument
#                     also keeps the splash screen away)
#   t02_render.blend  default scene at 640x360, for Blender Render and Cycles
#   t03_bge.blend     default scene with a Python controller that runs the
#                     game for 300 frames, records frame times and quits

import os
import sys

import bpy

FRAMES = 300

BGE_PROBE = '''\
# BGE-Remix T0.3 probe: counts frames, records frame times, ends the game.
import json
import os
import time

import bge

FRAMES = %d

g = bge.logic.globalDict
now = time.perf_counter()
if "remix_t03" not in g:
    g["remix_t03"] = {"start": now, "times": []}
state = g["remix_t03"]
state["times"].append(now)

own = bge.logic.getCurrentController().owner
own.applyRotation((0.0, 0.0, 0.02), False)

if len(state["times"]) >= FRAMES:
    times = state["times"]
    deltas = [b - a for a, b in zip(times, times[1:])]
    deltas.sort()
    result = {
        "frames": len(times),
        "seconds": times[-1] - state["start"],
        "frame_ms_mean": 1000.0 * sum(deltas) / len(deltas),
        "frame_ms_median": 1000.0 * deltas[len(deltas) // 2],
        "frame_ms_p95": 1000.0 * deltas[int(len(deltas) * 0.95)],
    }
    path = os.environ.get("BGE_REMIX_T03_OUT")
    if path:
        with open(path, "w") as f:
            json.dump(result, f, indent=2)
    bge.logic.endGame()
''' % FRAMES


def out_dir():
    argv = sys.argv
    if "--" not in argv:
        raise SystemExit("usage: blender -b -P make_scenes.py -- <out_dir>")
    path = os.path.abspath(argv[argv.index("--") + 1])
    os.makedirs(path, exist_ok=True)
    return path


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=False)
    return bpy.context.scene


def save(path):
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, compress=False)
    print("BGE-Remix: wrote", path)


def make_ui(folder):
    reset()
    save(os.path.join(folder, "t01_ui.blend"))


def make_render(folder):
    scene = reset()
    scene.render.resolution_x = 640
    scene.render.resolution_y = 360
    scene.render.resolution_percentage = 100
    scene.render.use_border = False
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '8'
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 4
    scene.cycles.samples = 16
    scene.cycles.seed = 0
    scene.cycles.use_animated_seed = False
    scene.cycles.device = 'CPU'
    save(os.path.join(folder, "t02_render.blend"))


def make_bge(folder):
    scene = reset()
    scene.render.engine = 'BLENDER_GAME'
    gs = scene.game_settings
    gs.resolution_x = 640
    gs.resolution_y = 360
    gs.use_frame_rate = False   # uncapped, so frame times are a benchmark
    gs.vsync = 'OFF'
    gs.show_debug_properties = False
    gs.show_framerate_profile = False

    cube = scene.objects["Cube"]
    scene.objects.active = cube
    cube.select = True

    text = bpy.data.texts.new("t03_probe.py")
    text.write(BGE_PROBE)

    bpy.ops.logic.sensor_add(type='ALWAYS', object=cube.name)
    bpy.ops.logic.controller_add(type='PYTHON', object=cube.name)
    sensor = cube.game.sensors[-1]
    controller = cube.game.controllers[-1]
    sensor.use_pulse_true_level = True
    sensor.tick_skip = 0
    controller.mode = 'SCRIPT'
    controller.text = text
    sensor.link(controller)
    save(os.path.join(folder, "t03_bge.blend"))


def main():
    folder = out_dir()
    make_ui(folder)
    make_render(folder)
    make_bge(folder)


main()
