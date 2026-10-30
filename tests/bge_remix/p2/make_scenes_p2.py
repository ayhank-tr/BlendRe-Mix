# BGE-Remix P2 test scenes. Runs inside Blender 2.79b (Python 3.5: no f-strings).
#
#   blender --background --factory-startup --python make_scenes_p2.py -- <out_dir>
#
# Writes:
#   p2_room.blend       a lit room: floor, walls, a few objects and materials,
#                       one lamp of each type, a world colour, a camera with
#                       lens shift; RTX Remix on (scene property bge_remix)
#   p2_instances.blend  1,000 copies of one cube over a floor (one mesh)
#   p2_expected.json    what the tests expect Remix to receive (lamps, materials)
#
# Both scenes carry the P2 probe (a Python controller on an empty): it counts
# frames, records frame times, writes the camera matrices, takes a screenshot
# and ends the game. Settings come from environment variables:
#   BGE_REMIX_P2_OUT          result JSON
#   BGE_REMIX_P2_FRAMES       frames to run (default 300)
#   BGE_REMIX_P2_SECONDS      run at least this long (RTX tests: warm-up)
#   BGE_REMIX_P2_SHOT         PNG screenshot of the game window, taken at
#   BGE_REMIX_P2_SHOT_FRAME   this frame (default 100; with SECONDS: near the end)

import json
import math
import os
import sys

import bpy
from mathutils import Euler, Vector

PROBE = '''\
# BGE-Remix P2 probe
import json
import os
import time

import bge

g = bge.logic.globalDict
now = time.perf_counter()
if "p2" not in g:
    g["p2"] = {"start": now, "times": [], "cpu": [], "camera": None, "shot": None}
state = g["p2"]
state["times"].append(now)
state["cpu"].append(time.process_time())
frames = len(state["times"])
env = os.environ
max_frames = int(env.get("BGE_REMIX_P2_FRAMES", "300"))
seconds = float(env.get("BGE_REMIX_P2_SECONDS", "0"))
elapsed = now - state["start"]

if frames == 60:
    cam = bge.logic.getCurrentScene().active_camera
    state["camera"] = {
        "name": cam.name,
        "modelview": [list(r) for r in cam.modelview_matrix],
        "projection": [list(r) for r in cam.projection_matrix],
    }

shot = env.get("BGE_REMIX_P2_SHOT")
shot_frame = int(env.get("BGE_REMIX_P2_SHOT_FRAME", "100"))
if shot and state["shot"] is None:
    if (seconds > 0 and elapsed >= seconds - 1.0 and frames >= shot_frame) or (seconds <= 0 and frames == shot_frame):
        bge.render.makeScreenshot(shot)
        state["shot"] = {"frame": frames, "path": shot}

if frames >= max_frames and elapsed >= seconds:
    times = state["times"]
    # frame times: without the first 30 frames, or the first half of a timed run
    first = len(times) // 2 if seconds > 0 else min(30, len(times) // 4)
    window = times[first:]
    deltas = sorted(b - a for a, b in zip(window, window[1:]))
    # P4.8 (B4.5): the process's CPU time over the measured frames, as a share of one core
    cpu = state["cpu"][first:]
    cpu_share = (cpu[-1] - cpu[0]) / (window[-1] - window[0]) if len(window) > 1 and window[-1] > window[0] else None
    result = {
        "frames": frames,
        "seconds": elapsed,
        "measured_frames": len(deltas),
        "frame_ms_median": 1000.0 * deltas[len(deltas) // 2] if deltas else None,
        "frame_ms_p95": 1000.0 * deltas[int(len(deltas) * 0.95)] if deltas else None,
        "cpu_share": cpu_share,
        "camera": state["camera"],
        "screenshot": state["shot"],
        "window": [bge.render.getWindowWidth(), bge.render.getWindowHeight()],
    }
    path = env.get("BGE_REMIX_P2_OUT")
    if path:
        with open(path, "w") as f:
            json.dump(result, f, indent=2)
    bge.logic.endGame()
'''


# whole stops: Remix came out about two stops darker than Cycles (P2 RTX run 5)
EXPOSURES = (1, 2, 3)


def scale_tag(scale):
    return ("%g" % scale).replace(".", "_")


def out_dir():
    argv = sys.argv
    if "--" not in argv:
        raise SystemExit("usage: blender -b -P make_scenes_p2.py -- <out_dir>")
    path = os.path.abspath(argv[argv.index("--") + 1])
    os.makedirs(path, exist_ok=True)
    return path


def empty_scene():
    bpy.ops.wm.read_factory_settings(use_empty=False)
    scene = bpy.context.scene
    for ob in list(scene.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    scene.render.engine = 'BLENDER_GAME'
    gs = scene.game_settings
    gs.resolution_x = 1280
    gs.resolution_y = 720
    gs.use_frame_rate = False   # uncapped: frame times are benchmarks
    gs.vsync = 'OFF'
    gs.show_debug_properties = False
    gs.show_framerate_profile = False
    gs.material_mode = 'GLSL'
    return scene


def material(name, color, ref=0.8, alpha=1.0, emit=0.0, hard=50, blend='OPAQUE'):
    ma = bpy.data.materials.new(name)
    ma.diffuse_color = color
    ma.diffuse_intensity = ref
    ma.alpha = alpha
    ma.emit = emit
    ma.specular_hardness = hard
    ma.game_settings.alpha_blend = blend
    if alpha < 1.0:
        ma.use_transparency = True
    return ma


def plane(scene, name, size, location, rotation, ma):
    """A plane facing +Z before rotation."""
    s = size / 2.0
    me = bpy.data.meshes.new(name)
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    me.update()
    me.materials.append(ma)
    ob = bpy.data.objects.new(name, me)
    ob.location = location
    ob.rotation_euler = rotation
    scene.objects.link(ob)
    return ob


def cube(scene, name, size, location, ma, mesh=None):
    if mesh is None:
        import bmesh
        mesh = bpy.data.meshes.new(name + "Mesh")
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=size)  # outward normals
        bm.to_mesh(mesh)
        bm.free()
        mesh.update()
        mesh.materials.append(ma)
    ob = bpy.data.objects.new(name, mesh)
    ob.location = location
    scene.objects.link(ob)
    return ob


def lamp(scene, name, kind, location, rotation=(0, 0, 0), energy=1.0, color=(1, 1, 1), distance=10.0,
         soft=0.2):
    la = bpy.data.lamps.new(name, kind)
    la.energy = energy
    la.color = color
    la.distance = distance
    if hasattr(la, "shadow_soft_size"):  # not on hemi lamps
        la.shadow_soft_size = soft
    ob = bpy.data.objects.new(name, la)
    ob.location = location
    ob.rotation_euler = rotation
    scene.objects.link(ob)
    return ob


def add_probe(scene):
    probe = bpy.data.objects.new("Probe", None)
    scene.objects.link(probe)
    scene.objects.active = probe
    text = bpy.data.texts.new("p2_probe.py")
    text.write(PROBE)
    bpy.ops.logic.sensor_add(type='ALWAYS', object=probe.name)
    bpy.ops.logic.controller_add(type='PYTHON', object=probe.name)
    sensor = probe.game.sensors[-1]
    controller = probe.game.controllers[-1]
    sensor.use_pulse_true_level = True
    sensor.tick_skip = 0
    controller.mode = 'SCRIPT'
    controller.text = text
    sensor.link(controller)


def look_at(ob, target):
    direction = Vector(target) - ob.location
    ob.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()


def camera(scene, location, target, lens=30.0, shift_x=0.0):
    cam = bpy.data.cameras.new("Camera")
    cam.lens = lens
    cam.shift_x = shift_x
    cam.clip_start = 0.1
    cam.clip_end = 100.0
    ob = bpy.data.objects.new("Camera", cam)
    ob.location = location
    scene.objects.link(ob)
    look_at(ob, target)
    scene.camera = ob
    return ob


def world(scene, horizon):
    w = bpy.data.worlds.new("World")
    w.horizon_color = horizon
    scene.world = w


def save(path):
    # P8.7: new materials are physical; these scenes were made for Classic
    # shading (Blender Internal's), so they keep it
    for ma in bpy.data.materials:
        ma["bge_physical"] = 0
    # P8.9: new lamps are physical too; these scenes keep their own falloff
    for la in bpy.data.lamps:
        la["bge_physical"] = 0
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, compress=False)
    print("BGE-Remix: wrote", path)


def vec(v):
    return [round(float(x), 6) for x in v]


def lamp_expect(ob):
    la = ob.data
    m = ob.matrix_world
    e = {"name": ob.name, "type": la.type, "position": vec(m.translation),
         "direction": vec(-(m.to_3x3() * Vector((0, 0, 1))).normalized()),
         "energy": la.energy, "color": vec(la.color), "soft": getattr(la, "shadow_soft_size", 0.0)}
    if la.type == 'SPOT':
        e["cone_deg"] = math.degrees(la.spot_size) / 2.0
        # Remix's cone softness is in cosine units: Blender's blend region
        e["softness"] = (1.0 - math.cos(la.spot_size / 2.0)) * la.spot_blend
    if la.type == 'AREA':
        e["size"] = [la.size, la.size_y if la.shape == 'RECTANGLE' else la.size]
    return e


def make_room(folder):
    scene = empty_scene()
    world(scene, (0.05, 0.07, 0.10))
    floor = material("Floor", (0.7, 0.7, 0.7))
    wall = material("Wall", (0.8, 0.75, 0.65), ref=0.9, hard=20)
    red = material("Red", (0.8, 0.1, 0.1), hard=100)
    glass = material("Glass", (0.6, 0.8, 1.0), alpha=0.4, blend='ALPHA')
    panel = material("Panel", (1.0, 0.9, 0.7), emit=2.0)

    plane(scene, "Floor", 8.0, (0, 0, 0), (0, 0, 0), floor)
    plane(scene, "Ceiling", 8.0, (0, 0, 4), (math.pi, 0, 0), wall)
    plane(scene, "BackWall", 8.0, (0, 4, 2), (math.radians(90), 0, 0), wall)
    plane(scene, "LeftWall", 8.0, (-4, 0, 2), (0, math.radians(90), 0), wall)
    plane(scene, "RightWall", 8.0, (4, 0, 2), (0, math.radians(-90), 0), wall)
    cube(scene, "RedBox", 1.0, (-1.2, 0.8, 0.5), red)
    cube(scene, "GlassBox", 0.8, (1.2, 0.2, 0.4), glass)
    plane(scene, "LightPanel", 1.0, (0, 3.95, 2.5), (math.radians(90), 0, 0), panel)

    lamps = [
        lamp(scene, "LampPoint", 'POINT', (-2.0, 1.0, 3.0), energy=1.5, color=(1.0, 0.95, 0.9), distance=10.0,
             soft=0.2),
        lamp(scene, "LampSpot", 'SPOT', (2.0, -1.0, 3.5), (math.radians(20), 0, 0), energy=2.0, distance=12.0,
             soft=0.1),
        lamp(scene, "LampSun", 'SUN', (0, 0, 6), (math.radians(45), 0, math.radians(30)), energy=0.5),
        lamp(scene, "LampArea", 'AREA', (0.0, 2.0, 3.9), energy=1.0, distance=8.0, soft=1.0),
        lamp(scene, "LampHemi", 'HEMI', (0, 0, 5), energy=0.2, color=(0.6, 0.7, 1.0)),
    ]
    spot = lamps[1].data
    spot.spot_size = math.radians(60.0)
    spot.spot_blend = 0.3
    area = lamps[3].data
    area.shape = 'RECTANGLE'
    area.size = 1.0
    area.size_y = 0.5

    camera(scene, (0.0, -7.0, 2.0), (0.0, 0.0, 1.2), lens=30.0, shift_x=0.05)
    add_probe(scene)
    scene["bge_remix"] = 1
    scene.update()  # world matrices of the new objects (background mode)
    save(os.path.join(folder, "p2_room.blend"))
    # the same room brighter, for choosing the default exposure (sign-off)
    for ev in EXPOSURES:
        scene["bge_remix_exposure"] = ev
        save(os.path.join(folder, "p2_room_exposure_%s.blend" % scale_tag(ev)))
    del scene["bge_remix_exposure"]

    expected = {
        "lamps": [lamp_expect(ob) for ob in lamps],
        "world_horizon": vec(scene.world.horizon_color),
        # the room keeps the default light scale (KX_RemixRenderer: 0.125)
        "light_scale": scene.get("bge_remix_light_scale", 0.125),
        "materials": [
            {"name": m.name, "albedo": vec(Vector(m.diffuse_color) * m.diffuse_intensity),
             "opacity": m.alpha if m.game_settings.alpha_blend != 'OPAQUE' else 1.0,
             "blend": 1 if m.game_settings.alpha_blend != 'OPAQUE' else 0,
             "emit": m.emit, "color": vec(m.diffuse_color)}
            for m in (floor, wall, red, glass, panel)
        ],
        "meshes": ["Floor", "Ceiling", "BackWall", "LeftWall", "RightWall", "RedBoxMesh", "GlassBoxMesh",
                   "LightPanel"],
    }
    return expected


def make_spot(folder):
    """The room lit by its spot lamp alone (and a dark sky): does the pool of
    light on the floor show in Remix as in Cycles?"""
    bpy.ops.wm.open_mainfile(filepath=os.path.join(folder, "p2_room.blend"))
    scene = bpy.context.scene
    for ob in list(scene.objects):
        if ob.type == 'LAMP' and ob.name != "LampSpot":
            bpy.data.objects.remove(ob, do_unlink=True)
    scene.world.horizon_color = (0.005, 0.005, 0.005)
    save(os.path.join(folder, "p2_spot.blend"))


def make_instances(folder):
    scene = empty_scene()
    world(scene, (0.05, 0.05, 0.06))
    grey = material("Grey", (0.6, 0.6, 0.6))
    orange = material("Orange", (0.9, 0.4, 0.1))
    plane(scene, "Floor", 40.0, (0, 0, 0), (0, 0, 0), grey)
    first = cube(scene, "Box", 0.5, (0, 0, 0), orange)
    mesh = first.data
    n = 0
    for x in range(10):
        for y in range(10):
            for z in range(10):
                loc = (-6.75 + x * 1.5, -6.75 + y * 1.5, 0.5 + z * 1.0)
                if n == 0:
                    first.location = loc
                else:
                    cube(scene, "Box.%03d" % n, 0.5, loc, orange, mesh=mesh)
                n += 1
    lamp(scene, "Sun", 'SUN', (0, 0, 20), (math.radians(40), 0, math.radians(20)), energy=1.0)
    camera(scene, (0.0, -26.0, 14.0), (0.0, 0.0, 4.0), lens=35.0)
    add_probe(scene)
    scene["bge_remix"] = 1
    save(os.path.join(folder, "p2_instances.blend"))
    return {"instances": n, "meshes": ["Floor", "BoxMesh"]}


def main():
    folder = out_dir()
    expected = make_room(folder)
    make_spot(folder)
    expected["instances"] = make_instances(folder)
    with open(os.path.join(folder, "p2_expected.json"), "w") as f:
        json.dump(expected, f, indent=2)


# Blender runs a --python script as __main__; P3 imports this file for its helpers
if __name__ == "__main__":
    main()
