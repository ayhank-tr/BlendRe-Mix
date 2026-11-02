# BGE-Remix P7 test scenes (the Game View). Runs inside Blender 2.79b
# (Python 3.5: no f-strings).
#
#   blender --background --factory-startup --python make_scenes_p7.py -- <out_dir>
#
# Everything is made here: no outside assets (no Yo Frankie material, no
# bathroom). The tones, the checker picture and the short test-pattern video
# are made by this script too.
#
#   gv_clean.blend    a small level with one of each kind, all set up right:
#                     the six object types the game makes, a parent and a
#                     group; two cameras; a HUD overlay scene with a text;
#                     each lamp type; bodies of each collision shape, a
#                     hinge and a car; a 3D mono sound, music and a video on
#                     a screen; game properties, saved and loaded; every
#                     sensor, controller and actuator type; a Python script
#                     and a module; animations (an armature, a shape key); a
#                     message; a navigation mesh with a steering agent; a
#                     scene change; a second file loaded with LibLoad; a
#                     timer. PhysX on the CPU.
#   gv_broken.blend   the same level with one fault per warning (see FAULTS)
#   gv_remix.blend    a small RTX Remix scene: what Remix leaves out
#   gv_walls.blend    holograms behind a wall (hidden) and in front (shown)
#   gv_count_<n>.blend   the level's logic unit repeated to about n elements
#   gv_lib.blend      the file gv_clean loads with LibLoad
#   p7_expected.json  what each scene holds and the faults gv_broken has

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

# gv_broken's faults: (code, owner object ("" at scene level), level, what)
FAULTS = [
    ("REN_IMAGE_MISSING", "Poster", "RED", "a missing texture file"),
    ("PHY_GHOST_MESH", "GhostWall", "NOTE", "a Ghost with Triangle Mesh bounds (PhysX)"),
    ("MED_STEREO_3D", "Radio", "NOTE", "a stereo sound set to 3D"),
    ("LOG_UNCONNECTED", "Brain", "BLACK", "an unconnected brick"),
    ("LOG_NO_TARGET", "Brain", "BLACK", "an actuator aimed at nothing"),
    ("PY_SYNTAX", "Brain", "RED", "a script with an error"),
    ("ANI_BONES", "Rig", "NOTE", "an animation made for another armature"),
    ("MSG_NO_LISTENER", "Ball", "NOTE", "a message nobody hears"),
    ("NAV_TARGET_OFF", "Agent", "NOTE", "a steering target off the navigation mesh"),
    ("INP_KEY_TWICE", "Camera", "NOTE", "one key used twice"),
    ("SCN_NEVER_REACHED", "", "NOTE", "a scene never reached"),
    ("DAT_LIBLOAD_MISSING", "Loader", "RED", "a missing LibLoad file"),
    ("TIM_TIMER_NEVER_ENDS", "Player", "NOTE", "a timer that never ends"),
    ("LEFT_PARTICLES", "Fountain", "RED", "a particle system (left out)"),
]

# gv_remix's warnings (what RTX Remix leaves out or cannot keep)
REMIX_EXPECTED = [
    ("RMX_NO_UV", "Sign", "BLACK", "a texture not mapped by UV"),
    ("RMX_FILTER2D", "Brain", "NOTE", "a 2D filter"),
    ("RMX_DEFORM", "Arm", "NOTE", "a mesh deformed by an armature"),
]


# ---------------------------------------------------------------------------
# assets

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


def checker(path, size=64):
    img = bpy.data.images.new("checker_tmp", size, size)
    px = []
    for y in range(size):
        for x in range(size):
            on = ((x // 8) + (y // 8)) % 2
            px += [0.9, 0.9, 0.9, 1.0] if on else [0.2, 0.3, 0.5, 1.0]
    img.pixels = px
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)


def video(path):
    """a few frames of a coloured test pattern, as an AVI (Blender Internal render)"""
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_RENDER'
    r = scene.render
    r.resolution_x, r.resolution_y, r.resolution_percentage = 64, 48, 100
    r.image_settings.file_format = 'AVI_JPEG'
    r.filepath = path
    scene.frame_start, scene.frame_end = 1, 12
    w = bpy.data.worlds.new("Pattern")
    scene.world = w
    w.horizon_color = (0.2, 0.6, 0.9)
    w.keyframe_insert("horizon_color", frame=1)
    w.horizon_color = (0.9, 0.3, 0.2)
    w.keyframe_insert("horizon_color", frame=12)
    bpy.ops.render.render(animation=True)
    # Blender names the movie <path><start>-<end>.avi unless the name has a #
    made = path + "0001-0012.avi"
    if os.path.isfile(made):
        if os.path.isfile(path + ".avi"):
            os.remove(path + ".avi")
        os.rename(made, path + ".avi")
    return path + ".avi"


# ---------------------------------------------------------------------------
# helpers

def sensor(ob, kind, name=None):
    bpy.context.scene.objects.active = ob
    bpy.ops.logic.sensor_add(type=kind, object=ob.name)
    s = ob.game.sensors[-1]
    if name:
        s.name = name
    return s


def controller(ob, kind, name=None):
    bpy.context.scene.objects.active = ob
    bpy.ops.logic.controller_add(type=kind, object=ob.name)
    c = ob.game.controllers[-1]
    if name:
        c.name = name
    return c


def actuator(ob, kind, name=None):
    bpy.context.scene.objects.active = ob
    bpy.ops.logic.actuator_add(type=kind, object=ob.name)
    a = ob.game.actuators[-1]
    if name:
        a.name = name
    return a


def chain(ob, s_kind, c_kind, a_kind, name):
    """sensor -> controller -> actuator on ob, all named after name"""
    s = sensor(ob, s_kind, name + "S")
    c = controller(ob, c_kind, name + "C")
    a = actuator(ob, a_kind, name + "A")
    s.link(c)
    a.link(c)
    return s, c, a


def key_chain(ob, key, c_kind, a_kind, name):
    s, c, a = chain(ob, 'KEYBOARD', c_kind, a_kind, name)
    s.key = key
    return s, c, a


def prop(ob, name, kind, value):
    bpy.context.scene.objects.active = ob
    bpy.ops.object.game_property_new(type=kind, name=name)
    ob.game.properties[name].value = value


def script(name, body):
    t = bpy.data.texts.new(name)
    t.write(body)
    return t


def py_chain(ob, text, name):
    """an Always sensor (once, not every frame) running a script"""
    s = sensor(ob, 'ALWAYS', name + "S")
    c = controller(ob, 'PYTHON', name + "C")
    c.mode = 'SCRIPT'
    c.text = text
    s.link(c)
    return s, c


def empty(scene, name, location):
    ob = bpy.data.objects.new(name, None)
    ob.location = location
    scene.objects.link(ob)
    return ob


def mesh_ob(scene, name, op, location, ma, **kw):
    bpy.ops.object.select_all(action='DESELECT')
    op(location=location, **kw)
    ob = bpy.context.active_object
    ob.name = name
    ob.data.name = name
    if ma:
        ob.data.materials.append(ma)
    return ob


def body(ob, physics, bounds=None, mass=1.0):
    ob.game.physics_type = physics
    if bounds:
        ob.game.use_collision_bounds = True
        ob.game.collision_bounds_type = bounds
    if physics in ('DYNAMIC', 'RIGID_BODY'):
        ob.game.mass = mass


def textured(name, image, coords='UV'):
    ma = p2.material(name, (0.8, 0.8, 0.8))
    tex = bpy.data.textures.new(name, 'IMAGE')
    tex.image = image
    slot = ma.texture_slots.add()
    slot.texture = tex
    slot.texture_coords = coords
    return ma


def uv_plane(scene, name, size, location, rotation, ma):
    ob = p2.plane(scene, name, size, location, rotation, ma)
    ob.data.uv_textures.new("UVMap")
    uv = ob.data.uv_layers[0].data
    for i, co in enumerate(((0, 0), (1, 0), (1, 1), (0, 1))):
        uv[i].uv = co
    return ob


def set_physx(scene):
    gs = scene.game_settings
    try:
        gs.physics_engine = 'PHYSX'
        gs.physx_device = 'CPU'
    except (TypeError, AttributeError):
        gs.physics_engine = 'BULLET'
        scene["_bge_physx"] = 1


def new_game_scene(name):
    sc = bpy.data.scenes.new(name)
    sc.render.engine = 'BLENDER_GAME'
    return sc


def scene_camera(sc, name, location, target, ortho=None):
    cam = bpy.data.cameras.new(name)
    cam.clip_start, cam.clip_end = 0.1, 100.0
    if ortho:
        cam.type = 'ORTHO'
        cam.ortho_scale = ortho
    ob = bpy.data.objects.new(name, cam)
    ob.location = location
    sc.objects.link(ob)
    p2.look_at(ob, target)
    sc.camera = ob
    return ob


# ---------------------------------------------------------------------------
# the level

CAR_SCRIPT = """import bge
from bge import constraints

own = bge.logic.getCurrentController().owner
scene = bge.logic.getCurrentScene()
if "car" not in own:
    cid = constraints.createConstraint(own.getPhysicsId(), 0, constraints.VEHICLE_CONSTRAINT).getConstraintId()
    car = constraints.getVehicleConstraint(cid)
    for i, name in enumerate(("WheelFL", "WheelFR", "WheelRL", "WheelRR")):
        x = -0.9 if i % 2 == 0 else 0.9
        y = 1.2 if i < 2 else -1.2
        car.addWheel(scene.objects[name], [x, y, -0.3], [0, 0, -1], [-1, 0, 0], 0.2, 0.35, i < 2)
    own["car"] = cid
"""

HELLO_SCRIPT = """import bge
own = bge.logic.getCurrentController().owner
print("hello from", own.name)
"""

MODULE_SCRIPT = """import bge


def run(cont):
    cont.owner["visits"] = cont.owner.get("visits", 0) + 1
"""

LOAD_SCRIPT = """import bge
path = bge.logic.expandPath("//%s")
if path not in bge.logic.LibList():
    bge.logic.LibLoad(path, "Scene")
"""


def make_lib(folder):
    p2.empty_scene()
    scene = bpy.context.scene
    p2.cube(scene, "Crate", 1.0, (0, 0, 0.5), p2.material("Wood", (0.6, 0.4, 0.2)))
    p2.save(os.path.join(folder, "gv_lib.blend"))


def make_level(folder, broken, remix=False):
    """gv_clean, or gv_broken with FAULTS (returns the expected content)"""
    scene = p2.empty_scene()
    scene.name = "Level"
    p2.world(scene, (0.05, 0.06, 0.08))
    set_physx(scene)
    expect = {"objects": [], "bricks": [], "properties": []}

    grey = p2.material("Grey", (0.6, 0.6, 0.6))
    red = p2.material("Red", (0.8, 0.15, 0.1))
    blue = p2.material("Blue", (0.15, 0.3, 0.8))
    checker_img = bpy.data.images.load(os.path.join(folder, "gv_checker.png"))
    checker_img.filepath = "//gv_checker.png"
    floor_ma = textured("Checker", checker_img)

    # the floor (a static triangle mesh) and the hinge's post
    floor = uv_plane(scene, "Floor", 30.0, (0, 0, 0), (0, 0, 0), floor_ma)
    body(floor, 'STATIC')

    # the player (box), the ball (sphere), and one body of each other shape
    player = p2.cube(scene, "Player", 1.0, (-2, 0, 0.5), red)
    body(player, 'RIGID_BODY', 'BOX')
    ball = mesh_ob(scene, "Ball", bpy.ops.mesh.primitive_uv_sphere_add, (2, 0, 0.5), blue, size=0.5)
    body(ball, 'DYNAMIC', 'SPHERE')
    barrel = mesh_ob(scene, "Barrel", bpy.ops.mesh.primitive_cylinder_add, (4, 2, 0.5), grey, radius=0.4, depth=1.0)
    body(barrel, 'RIGID_BODY', 'CYLINDER')
    cone = mesh_ob(scene, "Cone", bpy.ops.mesh.primitive_cone_add, (5, -2, 0.5), grey, radius1=0.5, depth=1.0)
    body(cone, 'RIGID_BODY', 'CONE')
    rock = mesh_ob(scene, "Rock", bpy.ops.mesh.primitive_ico_sphere_add, (-5, -2, 0.6), grey, size=0.6)
    body(rock, 'RIGID_BODY', 'CONVEX_HULL')
    pill = mesh_ob(scene, "Pill", bpy.ops.mesh.primitive_cylinder_add, (-5, 2, 1.0), grey, radius=0.3, depth=2.0)
    body(pill, 'RIGID_BODY', 'CAPSULE')
    hero = p2.cube(scene, "Hero", 0.6, (0, 4, 0.9), blue)
    hero.scale = (1, 1, 3)
    body(hero, 'CHARACTER')

    # a hinge: a door on a post
    post = p2.cube(scene, "Post", 0.3, (7, 4, 1.0), grey)
    post.scale = (1, 1, 6.6)
    body(post, 'STATIC', 'BOX')
    door = p2.cube(scene, "Door", 1.0, (7.6, 4, 1.0), grey)
    door.scale = (1, 0.1, 1.8)
    body(door, 'RIGID_BODY', 'BOX')
    hinge = door.constraints.new('RIGID_BODY_JOINT')
    hinge.target = post
    hinge.pivot_type = 'HINGE'
    hinge.pivot_x = -0.5
    hinge.axis_z = 0.0

    # a car: a chassis and four wheels (made a vehicle by its script)
    chassis = p2.cube(scene, "Chassis", 1.0, (-8, 6, 0.8), red)
    chassis.scale = (1.8, 3.0, 0.5)
    body(chassis, 'RIGID_BODY', 'BOX', mass=1.0)
    for i, name in enumerate(("WheelFL", "WheelFR", "WheelRL", "WheelRR")):
        x = -8 + (-0.9 if i % 2 == 0 else 0.9)
        y = 6 + (1.2 if i < 2 else -1.2)
        w = mesh_ob(scene, name, bpy.ops.mesh.primitive_cylinder_add, (x, y, 0.4), grey, radius=0.35, depth=0.2,
                    rotation=(0, math.radians(90), 0))
        body(w, 'NO_COLLISION')
    car_text = script("gv_car.py", CAR_SCRIPT)
    py_chain(chassis, car_text, "Car")

    # a parent and a group
    hat = p2.cube(scene, "Hat", 0.3, (-2, 0, 1.2), blue)
    hat.parent = player
    hat.location = (0, 0, 0.7)
    body(hat, 'NO_COLLISION')
    grp = bpy.data.groups.new("Fence")
    for i in range(3):
        f = p2.cube(scene, "FencePost%d" % i, 0.2, (12 + i, -12, 0.5), grey)
        body(f, 'STATIC', 'BOX')
        grp.objects.link(f)
    inst = empty(scene, "FenceCopy", (12, -8, 0))
    inst.dupli_type = 'GROUP'
    inst.dupli_group = grp
    body(inst, 'NO_COLLISION')

    # lamps of each type
    p2.lamp(scene, "Point", 'POINT', (0, -3, 3))
    p2.lamp(scene, "Sun", 'SUN', (0, 0, 8), (math.radians(30), 0, 0))
    p2.lamp(scene, "Spot", 'SPOT', (3, -3, 4), (math.radians(40), 0, math.radians(30)))
    p2.lamp(scene, "Hemi", 'HEMI', (-3, -3, 5))
    p2.lamp(scene, "Area", 'AREA', (-6, 0, 4), (math.radians(20), 0, 0))

    # two cameras: the second becomes active from the logic
    cam = p2.camera(scene, (0, -22, 10), (0, 0, 0.5))
    cam2 = scene_camera(scene, "Camera2", (18, 0, 6), (0, 0, 0.5))
    scene.camera = cam

    # a text object in the level
    cu = bpy.data.curves.new("Title", 'FONT')
    cu.body = "Game View"
    title = bpy.data.objects.new("Title", cu)
    title.location = (-3, 8, 0.1)
    scene.objects.link(title)
    body(title, 'NO_COLLISION')

    # the logic's home: one empty holding most brick types
    brain = empty(scene, "Brain", (0, -6, 1))
    body(brain, 'NO_COLLISION')

    # properties (each one read and set by the logic) and the timer
    prop(player, "health", 'INT', 100)
    prop(player, "speed", 'FLOAT', 2.5)
    prop(player, "alive", 'BOOL', True)
    prop(player, "label", 'STRING', "player")
    prop(player, "clock", 'TIMER', 0.0)
    # health: an Expression reads it, a Property actuator sets it
    s, c, a = key_chain(player, 'W', 'LOGIC_AND', 'MOTION', "Move")
    a.offset_location = (0, 0.1, 0)
    s = sensor(player, 'ALWAYS', "LifeS")
    c = controller(player, 'EXPRESSION', "LifeC")
    c.expression = "health > 0 and alive and speed > 1 and label != \"\""
    a = actuator(player, 'PROPERTY', "HurtA")
    a.mode = 'ADD'
    a.property = "health"
    a.value = "-1"
    s.link(c)
    a.link(c)
    s = sensor(player, 'KEYBOARD', "PaceS")
    s.key = 'Q'
    c = controller(player, 'LOGIC_AND', "PaceC")
    for pname, val in (("speed", "3.0"), ("alive", "True"), ("label", "\"hero\"")):
        a = actuator(player, 'PROPERTY', "Set" + pname.title() + "A")
        a.mode = 'ASSIGN'
        a.property = pname
        a.value = val
        a.link(c)
    s.link(c)
    # the timer: checked by a Property sensor
    s = sensor(player, 'PROPERTY', "ClockS")
    s.evaluation_type = 'PROPGREATERTHAN'
    s.property = "clock"
    s.value = "30"
    c = controller(player, 'LOGIC_AND', "ClockC")
    a = actuator(player, 'VISIBILITY', "BlinkA")
    s.link(c)
    a.link(c)
    # globalDict: saved and loaded by Game actuators
    s, c, a = key_chain(brain, 'F5', 'LOGIC_AND', 'GAME', "Save")
    a.mode = 'SAVECFG'
    s, c, a = key_chain(brain, 'F9', 'LOGIC_AND', 'GAME', "Load")
    a.mode = 'LOADCFG'

    # every sensor type (each one linked; the actuators each need a controller)
    s, c, a = chain(brain, 'ALWAYS', 'LOGIC_OR', 'MOTION', "Spin")
    a.offset_rotation = (0, 0, 0.01)
    s, c, a = chain(brain, 'DELAY', 'LOGIC_NAND', 'STATE', "Wait")
    s.delay = 30
    a.operation = 'SET'
    a.states[0] = True
    s, c, a = chain(brain, 'JOYSTICK', 'LOGIC_NOR', 'RANDOM', "Pad")
    a.property = "luck"
    a.distribution = 'INT_UNIFORM'
    prop(brain, "luck", 'INT', 0)
    s, c, a = chain(brain, 'PROPERTY', 'LOGIC_XOR', 'FILTER_2D', "Luck")
    s.evaluation_type = 'PROPCHANGED'
    s.property = "luck"
    a.mode = 'SEPIA'
    s, c, a = chain(brain, 'MOUSE', 'LOGIC_XNOR', 'MOUSE', "Look")
    s.mouse_event = 'LEFTCLICK'
    a.mode = 'LOOK'
    s, c, a = chain(brain, 'RANDOM', 'LOGIC_AND', 'CAMERA', "Follow")
    a.object = player
    a.min, a.max, a.height = 4.0, 8.0, 2.0
    s, c, a = chain(brain, 'ACTUATOR', 'LOGIC_AND', 'CONSTRAINT', "Keep")
    s.actuator = "SpinA"
    a.mode = 'LOC'
    a.limit = 'LOCZ'
    a.limit_min = 0.5
    a.limit_max = 3.0
    s, c, a = chain(player, 'COLLISION', 'LOGIC_AND', 'PARENT', "Grab")
    a.mode = 'SETPARENT'
    a.object = brain
    s, c, a = chain(player, 'NEAR', 'LOGIC_AND', 'EDIT_OBJECT', "Shoot")
    s.distance = 2.0
    a.mode = 'ADDOBJECT'
    s, c, a = chain(player, 'RADAR', 'LOGIC_AND', 'SOUND', "Beep")
    s.distance = 8.0
    jingle = bpy.data.sounds.load(os.path.join(folder, "tone_mono.wav"))
    jingle.filepath = "//tone_mono.wav"
    a.sound = jingle
    a.use_sound_3d = True
    s, c, a = chain(player, 'RAY', 'LOGIC_AND', 'VISIBILITY', "Look")
    s.range = 5.0

    # the object the player shoots: on hidden layer 2, added by the logic
    bullet = p2.cube(scene, "Bullet", 0.2, (0, 0, 5), red)
    bullet.layers = [i == 1 for i in range(20)]
    body(bullet, 'DYNAMIC', 'BOX')
    player.game.actuators["ShootA"].object = bullet

    # music (stereo, not 3D) on a radio
    radio = empty(scene, "Radio", (6, -6, 1))
    body(radio, 'NO_COLLISION')
    music = bpy.data.sounds.load(os.path.join(folder, "tone_stereo.wav"))
    music.filepath = "//tone_stereo.wav"
    s, c, a = key_chain(radio, 'M', 'LOGIC_AND', 'SOUND', "Music")
    a.sound = music
    a.use_sound_3d = False

    # a video on a screen
    movie = bpy.data.images.load(os.path.join(folder, "gv_video.avi"))
    movie.filepath = "//gv_video.avi"
    screen = uv_plane(scene, "Screen", 3.0, (-10, 8, 2), (math.radians(90), 0, 0), textured("Screen", movie))
    body(screen, 'NO_COLLISION')

    # an armature and its skinned mesh, an action played on it
    arm = bpy.data.armatures.new("Rig")
    rig = bpy.data.objects.new("Rig", arm)
    rig.location = (9, -3, 0)
    scene.objects.link(rig)
    scene.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    bone = arm.edit_bones.new("Bone")
    bone.head, bone.tail = (0, 0, 0), (0, 0, 2)
    bpy.ops.object.mode_set(mode='OBJECT')
    target = empty(scene, "Reach", (9, -1, 2))
    body(target, 'NO_COLLISION')
    ik = rig.pose.bones["Bone"].constraints.new('IK')
    ik.name = "IK"
    ik.target = target
    limb = p2.cube(scene, "Arm", 0.5, (9, -3, 1), grey)
    limb.parent = rig
    limb.location = (0, 0, 1)
    vg = limb.vertex_groups.new("Bone")
    vg.add([v.index for v in limb.data.vertices], 1.0, 'REPLACE')
    limb.modifiers.new("Armature", 'ARMATURE').object = rig
    body(limb, 'NO_COLLISION')
    wave_act = bpy.data.actions.new("Wave")
    fc = wave_act.fcurves.new('pose.bones["Bone"].rotation_quaternion', index=1, action_group="Bone")
    fc.keyframe_points.insert(1, 0.0)
    fc.keyframe_points.insert(20, 0.4)
    rig.animation_data_create()
    rig.animation_data.action = wave_act
    body(rig, 'NO_COLLISION')
    s, c, a = chain(rig, 'ARMATURE', 'LOGIC_AND', 'ACTION', "Wave")
    s.bone = "Bone"
    s.constraint = "IK"
    s.test_type = 'STATECHG'
    a.action = wave_act
    a.frame_start, a.frame_end = 1, 20
    a.play_mode = 'LOOPSTOP'
    s, c, a = chain(rig, 'ALWAYS', 'LOGIC_AND', 'ARMATURE', "Pose")
    a.mode = 'RUN'

    # a shape key and its action
    face = p2.cube(scene, "Face", 0.6, (9, 3, 0.6), grey)
    body(face, 'STATIC', 'BOX')
    face.shape_key_add("Basis")
    smile = face.shape_key_add("Smile")
    for v in smile.data:
        v.co.z *= 1.2
    smile_act = bpy.data.actions.new("SmileAct")
    fc = smile_act.fcurves.new('key_blocks["Smile"].value')
    fc.keyframe_points.insert(1, 0.0)
    fc.keyframe_points.insert(10, 1.0)
    s, c, a = key_chain(face, 'S', 'LOGIC_AND', 'ACTION', "Smile")
    a.action = smile_act
    a.frame_start, a.frame_end = 1, 10

    # a message: the ball sends, the receiver hears and counts
    s, c, a = key_chain(ball, 'P', 'LOGIC_AND', 'MESSAGE', "Ping")
    a.subject = "ping"
    a.to_property = "Receiver"
    recv = empty(scene, "Receiver", (0, 3, 1))
    body(recv, 'NO_COLLISION')
    prop(recv, "pings", 'INT', 0)
    s, c, a = chain(recv, 'MESSAGE', 'LOGIC_AND', 'PROPERTY', "Count")
    s.subject = "ping"
    a.mode = 'ADD'
    a.property = "pings"
    a.value = "1"
    s, c, a = chain(recv, 'PROPERTY', 'LOGIC_AND', 'VISIBILITY', "Seen")
    s.evaluation_type = 'PROPGREATERTHAN'
    s.property = "pings"
    s.value = "3"

    # Python: a script, a module, and a LibLoad
    py_chain(brain, script("gv_hello.py", HELLO_SCRIPT), "Hello")
    script("gv_mod.py", MODULE_SCRIPT)
    s = sensor(brain, 'ALWAYS', "VisitS")
    c = controller(brain, 'PYTHON', "VisitC")
    c.mode = 'MODULE'
    c.module = "gv_mod.run"
    s.link(c)
    loader = empty(scene, "Loader", (-6, -6, 1))
    body(loader, 'NO_COLLISION')
    load_text = script("gv_load.py", LOAD_SCRIPT % "gv_lib.blend")
    py_chain(loader, load_text, "Load")

    # navigation: a navigation mesh, an agent steering to a goal on it
    nav = p2.plane(scene, "NavArea", 10.0, (-8, -8, 0.01), (0, 0, 0), grey)
    navmesh = nav
    try:
        bpy.ops.object.select_all(action='DESELECT')
        nav.select = True
        scene.objects.active = nav
        bpy.ops.mesh.navmesh_make()
        navmesh = bpy.context.active_object
        navmesh.name = "Navmesh"
        bpy.data.objects.remove(nav, do_unlink=True)
    except Exception:
        nav.name = "Navmesh"
    navmesh.game.physics_type = 'NAVMESH'
    agent = p2.cube(scene, "Agent", 0.5, (-10, -10, 0.3), blue)
    body(agent, 'DYNAMIC', 'BOX')
    goal = empty(scene, "Goal", (-6, -6, 0.1))
    body(goal, 'NO_COLLISION')
    s, c, a = key_chain(agent, 'G', 'LOGIC_AND', 'STEERING', "Steer")
    a.mode = 'PATHFOLLOWING'
    a.navmesh = navmesh
    a.target = goal

    # the scenes: a HUD overlay with a text, another level, the second camera
    hud = new_game_scene("HUD")
    scene_camera(hud, "HUDCam", (0, 0, 10), (0, 0, 0), ortho=10.0)
    hcu = bpy.data.curves.new("Score", 'FONT')
    hcu.body = "Score 0"
    hcu.align_x = 'CENTER'
    score = bpy.data.objects.new("Score", hcu)
    score.location = (0, 3, 0)
    hud.objects.link(score)
    level2 = new_game_scene("Level2")
    p2.plane(level2, "Ground", 10.0, (0, 0, 0), (0, 0, 0), grey)
    scene_camera(level2, "Level2Cam", (0, -10, 5), (0, 0, 0))
    s, c, a = key_chain(brain, 'H', 'LOGIC_AND', 'SCENE', "ShowHud")
    a.mode = 'ADDFRONT'
    a.scene = hud
    s, c, a = key_chain(brain, 'J', 'LOGIC_AND', 'SCENE', "HideHud")
    a.mode = 'REMOVE'
    a.scene = hud
    s, c, a = key_chain(brain, 'N', 'LOGIC_AND', 'SCENE', "Next")
    a.mode = 'SET'
    a.scene = level2
    s, c, a = key_chain(brain, 'C', 'LOGIC_AND', 'SCENE', "Look2")
    a.mode = 'CAMERA'
    a.camera = cam2

    faults = []
    if broken:
        faults = add_faults(scene, folder, locals())
    if remix:
        scene["bge_remix"] = 1

    for ob in scene.objects:
        expect["objects"].append(ob.name)
        for coll in (ob.game.sensors, ob.game.controllers, ob.game.actuators):
            expect["bricks"] += [ob.name + "/" + b.name for b in coll]
        expect["properties"] += [ob.name + "/" + p.name for p in ob.game.properties]
    expect["faults"] = faults
    expect["scenes"] = [s.name for s in bpy.data.scenes]
    return expect


def add_faults(scene, folder, v):
    """gv_broken: one fault for each entry of FAULTS"""
    grey = v["grey"]
    faults = []

    # a missing texture file
    img = bpy.data.images.new("PosterImage", 8, 8)
    img.source = 'FILE'
    img.filepath = "//missing_poster.png"
    uv_plane(scene, "Poster", 2.0, (-12, 0, 1.5), (math.radians(90), 0, 0), textured("Poster", img))
    body(scene.objects["Poster"], 'NO_COLLISION')
    # a Ghost with Triangle Mesh bounds under PhysX
    gw = p2.cube(scene, "GhostWall", 1.0, (12, 0, 1), grey)
    gw.scale = (0.2, 3, 2)
    body(gw, 'STATIC', 'TRIANGLE_MESH')
    gw.game.use_ghost = True
    # the music in 3D (it is stereo)
    v["radio"].game.actuators["MusicA"].use_sound_3d = True
    # an unconnected brick
    sensor(v["brain"], 'ALWAYS', "LooseS")
    # an actuator aimed at nothing
    s, c, a = chain(v["brain"], 'ALWAYS', 'LOGIC_AND', 'EDIT_OBJECT', "Aim")
    a.mode = 'TRACKTO'
    # a script with an error
    py_chain(v["brain"], script("gv_oops.py", "import bge\ndef oops(:\n    pass\n"), "Oops")
    # an animation made for another armature
    other = bpy.data.actions.new("TailWag")
    fc = other.fcurves.new('pose.bones["Tail"].rotation_quaternion', index=1, action_group="Tail")
    fc.keyframe_points.insert(1, 0.0)
    fc.keyframe_points.insert(10, 0.5)
    s, c, a = chain(v["rig"], 'ALWAYS', 'LOGIC_AND', 'ACTION', "Wag")
    a.action = other
    a.layer = 1
    a.frame_start, a.frame_end = 1, 10
    # a message nobody hears
    s, c, a = key_chain(v["ball"], 'O', 'LOGIC_AND', 'MESSAGE', "Shout")
    a.subject = "nobody"
    # the steering target off the navigation mesh
    v["goal"].location = (8, 12, 0.1)
    # one key used twice on the player (W also hides it)
    s, c, a = key_chain(v["player"], 'W', 'LOGIC_AND', 'VISIBILITY', "Hide")
    a.use_visible = False
    # a scene never reached
    attic = new_game_scene("Attic")
    scene_camera(attic, "AtticCam", (0, -5, 3), (0, 0, 0))
    # the LibLoad file is missing
    v["load_text"].clear()
    v["load_text"].write(LOAD_SCRIPT % "gv_missing_lib.blend")
    # a timer nothing checks
    prop(v["player"], "stopwatch", 'TIMER', 0.0)
    # a particle system
    fountain = p2.cube(scene, "Fountain", 0.5, (-12, -6, 0.25), grey)
    scene.objects.active = fountain
    bpy.ops.object.particle_system_add()
    body(fountain, 'STATIC', 'BOX')

    for code, owner, level, what in FAULTS:
        faults.append({"code": code, "owner": owner, "level": level, "what": what})
    return faults


# ---------------------------------------------------------------------------
# the other scenes

def make_remix(folder):
    """what RTX Remix leaves out: a texture not mapped by UV, a 2D filter, a
    deforming mesh; a particle system (drawn by Remix, not left out)"""
    scene = p2.empty_scene()
    scene.name = "Remix"
    p2.world(scene, (0.05, 0.06, 0.08))
    grey = p2.material("Grey", (0.6, 0.6, 0.6))
    floor = p2.plane(scene, "Floor", 12.0, (0, 0, 0), (0, 0, 0), grey)
    body(floor, 'STATIC')
    img = bpy.data.images.load(os.path.join(folder, "gv_checker.png"))
    img.filepath = "//gv_checker.png"
    sign = p2.cube(scene, "Sign", 1.0, (-2, 0, 0.5), textured("SignMat", img, coords='GLOBAL'))
    body(sign, 'STATIC', 'BOX')
    brain = empty(scene, "Brain", (0, -3, 1))
    body(brain, 'NO_COLLISION')
    s, c, a = key_chain(brain, 'B', 'LOGIC_AND', 'FILTER_2D', "Blur")
    a.mode = 'BLUR'
    arm = bpy.data.armatures.new("Rig")
    rig = bpy.data.objects.new("Rig", arm)
    rig.location = (3, 0, 0)
    scene.objects.link(rig)
    scene.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')
    bone = arm.edit_bones.new("Bone")
    bone.head, bone.tail = (0, 0, 0), (0, 0, 2)
    bpy.ops.object.mode_set(mode='OBJECT')
    body(rig, 'NO_COLLISION')
    limb = p2.cube(scene, "Arm", 0.5, (3, 0, 1), grey)
    limb.parent = rig
    limb.location = (0, 0, 1)
    vg = limb.vertex_groups.new("Bone")
    vg.add([v.index for v in limb.data.vertices], 1.0, 'REPLACE')
    limb.modifiers.new("Armature", 'ARMATURE').object = rig
    body(limb, 'NO_COLLISION')
    sparks = p2.cube(scene, "Sparks", 0.3, (0, 2, 0.2), grey)
    scene.objects.active = sparks
    bpy.ops.object.particle_system_add()
    body(sparks, 'STATIC', 'BOX')
    p2.lamp(scene, "Sun", 'SUN', (0, 0, 8), (math.radians(30), 0, 0))
    p2.camera(scene, (0, -10, 5), (0, 0, 0.5))
    scene["bge_remix"] = 1
    scene.view_settings.view_transform = 'Default'
    p2.save(os.path.join(folder, "gv_remix.blend"))
    return {"expected": [{"code": c, "owner": o, "level": l, "what": w} for c, o, l, w in REMIX_EXPECTED],
            "remix_particles": ["Sparks"]}


def make_walls(folder):
    """a wall between the camera and the object Hidden; Shown stands in front,
    off to the side, so its holograms do not cover Hidden's on the screen"""
    scene = p2.empty_scene()
    scene.name = "Walls"
    p2.world(scene, (0.05, 0.06, 0.08))
    grey = p2.material("Grey", (0.6, 0.6, 0.6))
    p2.plane(scene, "Floor", 20.0, (0, 0, 0), (0, 0, 0), grey)
    wall = p2.cube(scene, "Wall", 1.0, (0, 0, 2.5), grey)
    wall.scale = (8, 0.3, 5)
    for name, x, y in (("Hidden", 1.5, 4.0), ("Shown", -2.5, -4.0)):
        ob = p2.cube(scene, name, 1.0, (x, y, 0.5), grey)
        body(ob, 'RIGID_BODY', 'BOX')
        chain(ob, 'ALWAYS', 'LOGIC_AND', 'MOTION', name)
    p2.lamp(scene, "Sun", 'SUN', (0, 0, 8), (math.radians(30), 0, 0))
    p2.camera(scene, (0, -14, 2.5), (0, 0, 2.0), lens=35.0)
    p2.save(os.path.join(folder, "gv_walls.blend"))
    return {"hidden": "Hidden", "shown": "Shown"}


def make_count(folder, n):
    """the level's logic unit (a rigid body with a property and three bricks)
    repeated to about n elements (9 elements each)"""
    scene = p2.empty_scene()
    scene.name = "Count"
    p2.world(scene, (0.05, 0.06, 0.08))
    grey = p2.material("Grey", (0.6, 0.6, 0.6))
    units = max(1, n // 9)
    side = int(math.ceil(math.sqrt(units)))
    floor = p2.plane(scene, "Floor", side * 1.5 + 4, (0, 0, 0), (0, 0, 0), grey)
    body(floor, 'STATIC')
    p2.lamp(scene, "Sun", 'SUN', (0, 0, 10), (math.radians(30), 0, 0))
    p2.camera(scene, (0, -side * 1.2, side * 0.8), (0, 0, 0))
    first = p2.cube(scene, "Box", 0.5, (0, 0, 0.25), grey)
    body(first, 'RIGID_BODY', 'BOX')
    s, c, a = chain(first, 'ALWAYS', 'LOGIC_AND', 'PROPERTY', "Tick")
    prop(first, "ticks", 'INT', 0)
    a.mode = 'ADD'
    a.property = "ticks"
    a.value = "1"
    off = (side - 1) * 0.75
    first.location = (-off, -off, 0.25)
    for i in range(1, units):
        ob = first.copy()
        ob.location = ((i % side) * 1.5 - off, (i // side) * 1.5 - off, 0.25)
        scene.objects.link(ob)
    p2.save(os.path.join(folder, "gv_count_%d.blend" % n))
    return {"units": units}


def main():
    folder = p2.out_dir()
    tone(os.path.join(folder, "tone_mono.wav"), 1)
    tone(os.path.join(folder, "tone_stereo.wav"), 2)
    checker(os.path.join(folder, "gv_checker.png"))
    expected = {"failed": {}}
    try:
        bpy.ops.wm.read_factory_settings(use_empty=False)
        expected["video"] = video(os.path.join(folder, "gv_video"))
    except Exception:
        expected["failed"]["video"] = traceback.format_exc()
        print(expected["failed"]["video"])
    jobs = [("lib", lambda: make_lib(folder)),
            ("clean", lambda: (make_level(folder, False),
                               p2.save(os.path.join(folder, "gv_clean.blend")))[0]),
            ("broken", lambda: (make_level(folder, True),
                                p2.save(os.path.join(folder, "gv_broken.blend")))[0]),
            ("remix", lambda: make_remix(folder)),
            ("walls", lambda: make_walls(folder))]
    jobs += [("count_%d" % n, (lambda n=n: make_count(folder, n))) for n in COUNTS]
    for name, fn in jobs:
        try:
            expected[name] = fn()
        except Exception:
            expected["failed"][name] = traceback.format_exc()
            print(expected["failed"][name])
    with open(os.path.join(folder, "p7_expected.json"), "w") as f:
        json.dump(expected, f, indent=2)


main()
