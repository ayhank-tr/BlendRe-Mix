# BGE-Remix P5 test scenes. Runs inside Blender 2.79b (Python 3.5: no f-strings).
#
#   blender --background --factory-startup --python make_scenes_p5.py -- <out_dir>
#
# Writes:
#   p5_studio.blend   a Blender Game scene with Rasterizer RTX Remix (the 3D
#                     view and F12 use the RTX Remix engine through it): a 10 m
#                     grey floor and back wall, sun and sky, the camera 8 m in
#                     front and 2.5 m up; a red glossy Monkey (Subdivision
#                     Surface 2) on the left, a 1 m glass cube in the middle, a
#                     checker ball (radius 0.8 m) on the right, a warm point
#                     lamp above in front, a row of five 0.2 m pillars at the
#                     back (one group shown five times), a 0.3 m yellow glowing
#                     marker cube in front; 1280 x 720
#   p5_anim.blend     the same studio rendered with the "RTX Remix" engine of
#                     the engine list, 24 frames: the marker moves 6 m from
#                     left to right, the monkey turns once, the camera moves
#                     1 m closer
#   p5_expected.json  names, positions and counts the tests check
#
# Both carry the P2 probe (frames, screenshot, end of the game).

import json
import math
import os
import sys
import traceback

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p3"))
import make_scenes_p2 as p2  # noqa: E402  (probe, lamps, camera, save)
import make_scenes_p3 as p3  # noqa: E402  (images, node materials)

# T5.6: the game window resized at a logic tic (BGE_REMIX_P5_RESIZE_TIC,
# BGE_REMIX_P5_RESIZE_TO=WxH); Remix's picture must follow in direct mode
RESIZE = '''\
import os

import bge

g = bge.logic.globalDict
tic = int(os.environ.get("BGE_REMIX_P5_RESIZE_TIC", "0") or 0)
if tic:
    n = g.get("p5_tic", 0) + 1
    g["p5_tic"] = n
    if n == tic:
        w, h = [int(v) for v in os.environ.get("BGE_REMIX_P5_RESIZE_TO", "960x540").split("x")]
        bge.render.setWindowSize(w, h)
        print("BGE-Remix test: window resized to %dx%d at tic %d" % (w, h, n), flush=True)
'''

FRAMES = 24
MARKER_X = (-3.0, 3.0)
MARKER_Y = -1.5
MARKER_Z = 0.15
ADDED_CONE = (-1.6, -2.5, 0.3)  # where p5_ui_probe.py adds a cone (T5.1)


def mesh_from(name, ob_name, location, ma, kind, **kw):
    """A primitive (bpy.ops) renamed, with one material."""
    if kind == 'monkey':
        bpy.ops.mesh.primitive_monkey_add(radius=kw.get("radius", 0.8), location=location)
    elif kind == 'cylinder':
        bpy.ops.mesh.primitive_cylinder_add(vertices=24, radius=kw.get("radius", 0.1), depth=kw.get("depth", 1.5),
                                            location=location)
    elif kind == 'cube':
        bpy.ops.mesh.primitive_cube_add(radius=kw.get("radius", 0.5), location=location)
    ob = bpy.context.object
    ob.name = ob_name
    ob.data.name = name
    ob.data.materials.append(ma)
    return ob


def add_resize(scene):
    probe = scene.objects["Probe"]
    scene.objects.active = probe
    text = bpy.data.texts.new("p5_resize.py")
    text.write(RESIZE)
    bpy.ops.logic.sensor_add(type='ALWAYS', object=probe.name)
    bpy.ops.logic.controller_add(type='PYTHON', object=probe.name)
    sensor = probe.game.sensors[-1]
    controller = probe.game.controllers[-1]
    sensor.use_pulse_true_level = True
    sensor.tick_skip = 0
    controller.mode = 'SCRIPT'
    controller.text = text
    sensor.link(controller)


def studio(engine):
    scene = p3.base_scene()
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 720
    scene.render.resolution_percentage = 100
    # what F12 shows is Remix's picture: no second tone curve on top
    scene.view_settings.view_transform = 'Default'
    scene.frame_start = 1
    scene.frame_end = FRAMES
    p2.world(scene, (0.25, 0.32, 0.45))

    grey = p2.material("StudioGrey", (0.55, 0.55, 0.55), ref=1.0, hard=10)
    p2.plane(scene, "Floor", 10.0, (0, 0, 0), (0, 0, 0), grey)
    p2.plane(scene, "Wall", 10.0, (0, 4.0, 5.0), (math.radians(90), 0, 0), grey)

    red, _, _ = p3.principled("MonkeyRed", color=(0.8, 0.05, 0.04), roughness=0.25)
    monkey = mesh_from("MonkeyMesh", "Monkey", (-2.2, 0, 1.0), red, 'monkey')
    bpy.ops.object.shade_smooth()
    mod = monkey.modifiers.new("Subsurf", 'SUBSURF')
    mod.levels = 2
    mod.render_levels = 2

    glass, _, _ = p3.shader_material("CubeGlass", "ShaderNodeBsdfGlass", Color=(0.85, 0.95, 1.0, 1.0), IOR=1.45)
    mesh_from("GlassCubeMesh", "GlassCube", (0, 0, 0.5), glass, 'cube', radius=0.5)

    checker_img = p3.image("StudioChecker", p3.checker)
    ball_ma, nt, p = p3.principled("BallChecker", roughness=0.6)
    t = p3.image_node(nt, checker_img)
    nt.links.new(t.outputs["Color"], p.inputs["Base Color"])
    p3.sphere(scene, "CheckerBall", (2.2, 0, 0.8), ball_ma, radius=0.8)

    # the pillars: one cylinder in a group, on a hidden layer, shown five times
    stone = p2.material("PillarStone", (0.75, 0.72, 0.65), ref=1.0, hard=20)
    pillar = mesh_from("PillarMesh", "PillarSource", (0, 0, 0.75), stone, 'cylinder', radius=0.1, depth=1.5)
    group = bpy.data.groups.new("Pillar")
    group.objects.link(pillar)
    layers = [False] * 20
    layers[19] = True
    pillar.layers = layers
    pillar.location = (0, 0, 0.75)
    for i in range(5):
        e = bpy.data.objects.new("Pillar.%d" % i, None)
        e.dupli_type = 'GROUP'
        e.dupli_group = group
        e.location = (-2.0 + i * 1.0, 3.0, 0.0)
        scene.objects.link(e)

    yellow, _, _ = p3.shader_material("MarkerGlow", "ShaderNodeEmission", Color=(1.0, 0.85, 0.05, 1.0),
                                      Strength=4.0)
    mesh_from("MarkerMesh", "Marker", (0, MARKER_Y, MARKER_Z), yellow, 'cube', radius=0.15)

    sun = p2.lamp(scene, "Sun", 'SUN', (3, -4, 8), rotation=(math.radians(40), 0, math.radians(30)), energy=1.0)
    lamp = p2.lamp(scene, "Lamp", 'POINT', (0, -2.0, 4.0), energy=2.0, color=(1.0, 0.85, 0.65), distance=10.0,
                   soft=0.2)
    p2.camera(scene, (0, -8.0, 2.5), (0, 0, 0.8), lens=35.0)
    for ob in scene.objects:
        ob.select = False
    p2.add_probe(scene)
    add_resize(scene)
    if engine == 'RTX_REMIX':
        try:
            scene.render.engine = 'RTX_REMIX'
        except TypeError:
            # a build without the engine (P4 and older): the tests fail later with a reason
            print("BGE-Remix: this build has no RTX_REMIX engine")
    else:
        scene["bge_remix"] = 1
    scene.update()
    return scene


def project(scene, co):
    """normalized camera-view position (0..1, bottom left) of a world point"""
    from bpy_extras.object_utils import world_to_camera_view
    v = world_to_camera_view(scene, scene.camera, Vector(co))
    return [round(v.x, 4), round(v.y, 4)]


def make_studio(folder):
    scene = studio('BLENDER_GAME')
    path = os.path.join(folder, "p5_studio.blend")
    p2.save(path)
    names = ["Floor", "Wall", "Monkey", "GlassCube", "CheckerBall", "Marker"] + ["Pillar.%d" % i for i in range(5)]
    targets = {}
    for n in ("Monkey", "GlassCube", "CheckerBall", "Marker", "Pillar.2"):
        ob = scene.objects[n]
        co = ob.matrix_world.translation.copy()
        if n == "Pillar.2":
            co.z += 0.75
        targets[n] = project(scene, co)
    targets["Floor"] = project(scene, (-1.0, -1.0, 0.0))
    # the cone p5_ui_probe.py adds (T5.1)
    targets["AddedCone"] = project(scene, ADDED_CONE)
    return {
        "objects": names,
        # meshes Remix gets: floor and wall share nothing (two meshes), monkey (own:
        # modifier), glass cube, ball, marker, one pillar mesh for five copies
        "meshes": 7,
        "draws": 11,
        "lights": 3,  # sun, point lamp, sky
        "targets": targets,
    }


def make_anim(folder):
    scene = studio('RTX_REMIX')
    marker = scene.objects["Marker"]
    monkey = scene.objects["Monkey"]
    cam = scene.camera
    for f, x in ((1, MARKER_X[0]), (FRAMES, MARKER_X[1])):
        scene.frame_set(f)
        marker.location.x = x
        marker.keyframe_insert("location", index=0, frame=f)
    for f, a in ((1, 0.0), (FRAMES, 2.0 * math.pi)):
        monkey.rotation_euler.z = a
        monkey.keyframe_insert("rotation_euler", index=2, frame=f)
    for f, y in ((1, -8.0), (FRAMES, -7.0)):
        cam.location.y = y
        cam.keyframe_insert("location", index=1, frame=f)
    for ob in (marker, monkey, cam):
        for fc in ob.animation_data.action.fcurves:
            for k in fc.keyframe_points:
                k.interpolation = 'LINEAR'
    # where the marker is in each frame, in the picture (0..1 from the bottom left)
    screen = []
    for f in range(1, FRAMES + 1):
        scene.frame_set(f)
        screen.append(project(scene, marker.matrix_world.translation))
    scene.frame_set(1)
    path = os.path.join(folder, "p5_anim.blend")
    p2.save(path)
    xs = [MARKER_X[0] + (MARKER_X[1] - MARKER_X[0]) * (f - 1) / (FRAMES - 1) for f in range(1, FRAMES + 1)]
    return {"frames": FRAMES, "marker_x": [round(x, 5) for x in xs], "marker_y": MARKER_Y, "marker_z": MARKER_Z,
            "marker_screen": screen}


def main():
    folder = p2.out_dir()
    expected = {"failed": {}}
    for name, fn in (("studio", make_studio), ("anim", make_anim)):
        try:
            expected[name] = fn(folder)
        except Exception:
            text = traceback.format_exc()
            print("BGE-Remix: making the %s scene failed:\n%s" % (name, text))
            expected[name] = None
            expected["failed"][name] = text
    with open(os.path.join(folder, "p5_expected.json"), "w") as f:
        json.dump(expected, f, indent=2)
    print("BGE-Remix: wrote", os.path.join(folder, "p5_expected.json"))


if __name__ == "__main__":
    main()
