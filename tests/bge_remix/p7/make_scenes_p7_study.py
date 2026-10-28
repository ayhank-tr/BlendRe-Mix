# BGE-Remix P7.0 study scenes. Runs inside Blender 2.79b (Python 3.5: no f-strings).
#
#   blender --background --factory-startup --python make_scenes_p7_study.py -- <out_dir>
#
# Writes (no outside assets; the two tones are written by this script):
#   gv_study_mixed.blend
#       one of most kinds the game engine knows, all on one small floor:
#       a cube (Rigid Body, Keyboard sensor, And controller, Motion
#       actuator, five game properties), a ball (Dynamic, Always sensor,
#       Python controller with a script, Sound actuator playing a mono tone
#       in 3D, Message actuator), an empty receiving the message (Message
#       sensor, Property actuator), a point, a spot and a sun lamp, the
#       camera, a text object, an armature with a bone, a skinned mesh and
#       an action played by an Action actuator, a static Ghost with Triangle
#       Mesh bounds, a cube on hidden layer 2 added by an Edit Object
#       actuator; and things the game leaves out: a particle system, a force
#       field, a speaker object, a curve and a metaball
#   gv_study_count_<n>.blend (n = 1000, 5000, 20000)
#       n cubes in a grid, each a Rigid Body with one material, two game
#       properties and three logic bricks (Always, And, Motion); one mesh
#       shared by all
#   tone_mono.wav, tone_stereo.wav   0.5 s tones (440 Hz)
#   p7_study_expected.json           what each scene holds

import json
import math
import os
import struct
import sys
import traceback
import wave

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
import make_scenes_p2 as p2  # noqa: E402

COUNTS = (1000, 5000, 20000)


def tone(path, channels):
    rate = 22050
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for i in range(rate // 2):
            v = int(12000 * math.sin(2.0 * math.pi * 440.0 * i / rate))
            frames += struct.pack("<h", v) * channels
        w.writeframes(bytes(frames))


def bricks(ob, sensor, controller, actuator):
    """one sensor, controller and actuator on ob, linked; returns them"""
    bpy.ops.logic.sensor_add(type=sensor, object=ob.name)
    bpy.ops.logic.controller_add(type=controller, object=ob.name)
    bpy.ops.logic.actuator_add(type=actuator, object=ob.name)
    s, c, a = ob.game.sensors[-1], ob.game.controllers[-1], ob.game.actuators[-1]
    s.link(c)
    a.link(c)
    return s, c, a


def prop(ob, name, kind, value):
    bpy.ops.object.game_property_new(type=kind, name=name)
    ob.game.properties[name].value = value


def make_mixed(folder):
    scene = p2.empty_scene()
    scene.name = "Mixed"
    p2.world(scene, (0.05, 0.06, 0.08))
    grey = p2.material("Grey", (0.6, 0.6, 0.6))
    red = p2.material("Red", (0.8, 0.15, 0.1))
    blue = p2.material("Blue", (0.15, 0.3, 0.8))
    floor = p2.plane(scene, "Floor", 20.0, (0, 0, 0), (0, 0, 0), grey)
    floor.game.physics_type = 'STATIC'

    # the cube: rigid body, keys move it, five properties
    cube = p2.cube(scene, "Cube", 1.0, (-2.0, 0, 0.5), red)
    cube.game.physics_type = 'RIGID_BODY'
    scene.objects.active = cube
    s, c, a = bricks(cube, 'KEYBOARD', 'LOGIC_AND', 'MOTION')
    s.key = 'W'
    a.offset_location = (0, 0.1, 0)
    for name, kind, value in (("health", 'INT', 100), ("speed", 'FLOAT', 2.5), ("alive", 'BOOL', True),
                              ("label", 'STRING', "player"), ("clock", 'TIMER', 0.0)):
        prop(cube, name, kind, value)

    # the ball: dynamic, a script, a sound, a message
    bpy.ops.mesh.primitive_uv_sphere_add(size=0.5, location=(2.0, 0, 0.5))
    ball = bpy.context.active_object
    ball.name = "Ball"
    ball.data.materials.append(blue)
    ball.game.physics_type = 'DYNAMIC'
    text = bpy.data.texts.new("p7_hello.py")
    text.write("import bge\nown = bge.logic.getCurrentController().owner\n")
    s, c, a = bricks(ball, 'ALWAYS', 'PYTHON', 'SOUND')
    c.mode = 'SCRIPT'
    c.text = text
    snd = bpy.data.sounds.load(os.path.join(folder, "tone_mono.wav"))
    a.sound = snd
    a.use_sound_3d = True
    bpy.ops.logic.actuator_add(type='MESSAGE', object=ball.name)
    msg = ball.game.actuators[-1]
    msg.subject = "ping"
    msg.link(c)

    # the receiver
    recv = bpy.data.objects.new("Receiver", None)
    recv.location = (0, 3.0, 1.0)
    scene.objects.link(recv)
    scene.objects.active = recv
    s, c, a = bricks(recv, 'MESSAGE', 'LOGIC_AND', 'PROPERTY')
    s.subject = "ping"
    prop(recv, "pings", 'INT', 0)
    a.mode = 'ADD'
    a.property = "pings"
    a.value = "1"

    # lamps, camera, text
    p2.lamp(scene, "Point", 'POINT', (0, -3, 3))
    p2.lamp(scene, "Spot", 'SPOT', (3, -3, 4), (math.radians(40), 0, math.radians(30)))
    p2.lamp(scene, "Sun", 'SUN', (0, 0, 8), (math.radians(30), 0, 0))
    p2.camera(scene, (0, -12, 6), (0, 0, 0.5))
    cu = bpy.data.curves.new("Title", 'FONT')
    cu.body = "Game View"
    title = bpy.data.objects.new("Title", cu)
    title.location = (-3, 4, 0.1)
    scene.objects.link(title)

    # an armature with one bone, a skinned mesh, an action
    arm = bpy.data.armatures.new("Rig")
    rig = bpy.data.objects.new("Rig", arm)
    rig.location = (5, 3, 0)
    scene.objects.link(rig)
    scene.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    bone = arm.edit_bones.new("Bone")
    bone.head = (0, 0, 0)
    bone.tail = (0, 0, 2)
    bpy.ops.object.mode_set(mode='OBJECT')
    limb = p2.cube(scene, "Limb", 0.5, (5, 3, 1), grey)
    limb.parent = rig
    limb.location = (0, 0, 1)
    vg = limb.vertex_groups.new("Bone")
    vg.add([v.index for v in limb.data.vertices], 1.0, 'REPLACE')
    mod = limb.modifiers.new("Armature", 'ARMATURE')
    mod.object = rig
    act = bpy.data.actions.new("Wave")
    fc = act.fcurves.new('pose.bones["Bone"].rotation_quaternion', index=1, action_group="Bone")
    fc.keyframe_points.insert(1, 0.0)
    fc.keyframe_points.insert(20, 0.4)
    rig.animation_data_create()
    rig.animation_data.action = act
    s, c, a = bricks(rig, 'ALWAYS', 'LOGIC_AND', 'ACTION')
    a.action = act
    a.frame_start = 1
    a.frame_end = 20
    a.play_mode = 'LOOPSTOP'

    # a static ghost with triangle mesh bounds
    ghost = p2.cube(scene, "GhostBox", 1.0, (-5, -2, 0.5), grey)
    ghost.game.physics_type = 'STATIC'
    ghost.game.use_ghost = True
    ghost.game.use_collision_bounds = True
    ghost.game.collision_bounds_type = 'TRIANGLE_MESH'

    # a cube on hidden layer 2, added by the cube
    spawn = p2.cube(scene, "Spawn", 0.3, (8, 0, 0.5), red)
    spawn.layers = [i == 1 for i in range(20)]
    scene.objects.active = cube
    bpy.ops.logic.actuator_add(type='EDIT_OBJECT', object=cube.name)
    add = cube.game.actuators[-1]
    add.mode = 'ADDOBJECT'
    add.object = spawn
    add.link(cube.game.controllers[-1])

    # what the game leaves out
    smoke = p2.cube(scene, "Smoke", 0.5, (-6, 3, 0.25), grey)
    scene.objects.active = smoke
    bpy.ops.object.particle_system_add()
    fan = bpy.data.objects.new("Fan", None)
    fan.location = (-6, -4, 1)
    scene.objects.link(fan)
    scene.objects.active = fan
    bpy.ops.object.forcefield_toggle()
    spk = bpy.data.speakers.new("Speaker")
    spk.sound = snd
    speaker = bpy.data.objects.new("Speaker", spk)
    speaker.location = (6, -4, 1)
    scene.objects.link(speaker)
    path = bpy.data.curves.new("Path", 'CURVE')
    sp = path.splines.new('POLY')
    sp.points.add(2)
    for i, co in enumerate(((0, 0, 0, 1), (1, 1, 0, 1), (2, 0, 0, 1))):
        sp.points[i].co = co
    curve = bpy.data.objects.new("Path", path)
    curve.location = (-8, 0, 0.1)
    scene.objects.link(curve)
    mb = bpy.data.metaballs.new("Blob")
    mb.elements.new()
    blob = bpy.data.objects.new("Blob", mb)
    blob.location = (8, 4, 1)
    scene.objects.link(blob)

    # Rendered shading uses RTX Remix (P5); what the view shows is Remix's picture
    scene["bge_remix"] = 1
    scene.view_settings.view_transform = 'Default'
    p2.save(os.path.join(folder, "gv_study_mixed.blend"))
    return {"objects": sorted(o.name for o in scene.objects),
            "left_out": ["Speaker", "Path", "Blob"], "particle_systems": ["Smoke"], "force_fields": ["Fan"]}


def make_count(folder, n):
    scene = p2.empty_scene()
    scene.name = "Count"
    p2.world(scene, (0.05, 0.06, 0.08))
    grey = p2.material("Grey", (0.6, 0.6, 0.6))
    side = int(math.ceil(math.sqrt(n)))
    floor = p2.plane(scene, "Floor", side * 1.5 + 4, (0, 0, 0), (0, 0, 0), grey)
    floor.game.physics_type = 'STATIC'
    p2.lamp(scene, "Sun", 'SUN', (0, 0, 10), (math.radians(30), 0, 0))
    p2.camera(scene, (0, -side * 1.2, side * 0.8), (0, 0, 0))
    first = p2.cube(scene, "Box", 0.5, (0, 0, 0.25), grey)
    first.game.physics_type = 'RIGID_BODY'
    scene.objects.active = first
    bricks(first, 'ALWAYS', 'LOGIC_AND', 'MOTION')
    prop(first, "hits", 'INT', 0)
    prop(first, "heat", 'FLOAT', 0.0)
    off = (side - 1) * 0.75
    first.location = (-off, -off, 0.25)
    for i in range(1, n):
        ob = first.copy()   # shares the mesh; copies the bricks and properties
        ob.location = ((i % side) * 1.5 - off, (i // side) * 1.5 - off, 0.25)
        scene.objects.link(ob)
    p2.save(os.path.join(folder, "gv_study_count_%d.blend" % n))
    return {"boxes": n, "elements_per_box": 1 + 1 + 1 + 1 + 3 + 2}


def main():
    folder = p2.out_dir()
    tone(os.path.join(folder, "tone_mono.wav"), 1)
    tone(os.path.join(folder, "tone_stereo.wav"), 2)
    expected = {"failed": {}}
    for name, fn in (("mixed", lambda: make_mixed(folder)),) + tuple(
            ("count_%d" % n, (lambda n=n: make_count(folder, n))) for n in COUNTS):
        try:
            expected[name] = fn()
        except Exception:
            expected["failed"][name] = traceback.format_exc()
            print(expected["failed"][name])
    with open(os.path.join(folder, "p7_study_expected.json"), "w") as f:
        json.dump(expected, f, indent=2)


main()
