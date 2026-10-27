# BGE-Remix P4 test scenes. Runs inside Blender 2.79b (Python 3.5: no f-strings).
#
#   blender --background --factory-startup --python make_scenes_p4.py -- <out_dir>
#
# Writes (see the P4 plan for the drawings):
#   p4_characters.blend  three Mannequins (Walker on a 2.5 m circle, Twin on
#                        the spot sharing Walker's mesh, Waver), a pulsing
#                        shape-key ball, a Suzanne with Subdivision Surface,
#                        a bending two-bone bar with a red tip
#   p4_crowd.blend       25 Mannequins in a 5 x 5 grid walking on the spot
#   p4_physics.blend     150 cubes and 50 balls (rigid bodies) dropped onto a
#                        ramp, a soft-body cloth over a fixed ball
#   p4_fountain.blend    a fountain spawning drops (dynamic) and Halo sparks,
#                        a Billboard tree, a Blender particle emitter (P4.6)
#   p4_picking.blend     five targets in a row (cube, ball, cone without
#                        collision, alpha-clip leaf, Walker) for picking
#   p4_expected.json     what the tests expect
#
# The characters are animated by p4_logic.py from the logic tic count (not
# the clock), so the same tic shows the same pose in OpenGL and in Remix;
# BGE_REMIX_P4_FREEZE=N stops every animation and motion at tic N.

import json
import math
import os
import random
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p3"))
import make_scenes_p2 as p2  # noqa: E402  (probe, lamps, camera, save)
import make_scenes_p3 as p3  # noqa: E402  (images, materials)

SIZE = 128


# ---------------------------------------------------------------------------
# The logic: deterministic animation from the tic count

LOGIC = '''\
# BGE-Remix P4: animation and motion from the logic tic count
import math
import os

import bge

c = bge.logic.getCurrentController()
ob = c.owner
freeze = int(os.environ.get("BGE_REMIX_P4_FREEZE", "0"))
t = ob.get("t", 0)
if not (freeze and t >= freeze):
    t += 1
ob["t"] = t
if "action" in ob:
    start, length = ob["start"], ob["length"]
    frame = start + (t * ob["speed"] + ob["phase"]) % length
    if not ob.get("playing"):
        ob.playAction(ob["action"], start, start + length, layer=0, play_mode=bge.logic.KX_ACTION_MODE_LOOP,
                      speed=0.0)
        ob["playing"] = True
    ob.setActionFrame(frame, 0)
if ob.get("radius", 0.0) > 0.0:
    # around (cx, cy), facing the way it walks (the Mannequin looks down -Y)
    a = t * ob["turn"]
    r = ob["radius"]
    ob.worldPosition = (ob["cx"] + r * math.cos(a), ob["cy"] + r * math.sin(a), ob.worldPosition.z)
    ob.worldOrientation = (0.0, 0.0, a + math.pi)
'''

FOUNTAIN = '''\
# BGE-Remix P4: the fountain spawns drops and sparks (fixed seed)
import math
import os
import random

import bge

c = bge.logic.getCurrentController()
ob = c.owner
scene = bge.logic.getCurrentScene()
g = bge.logic.globalDict
if "p4_rng" not in g:
    g["p4_rng"] = random.Random(42)
rng = g["p4_rng"]
freeze = int(os.environ.get("BGE_REMIX_P4_FREEZE", "0"))
t = ob.get("t", 0) + 1
ob["t"] = t
if not (freeze and t >= freeze):
    for i in range(5):
        d = scene.addObject("Drop", ob, 120)
        a = rng.uniform(0.0, 2.0 * math.pi)
        tilt = math.radians(rng.uniform(0.0, 20.0))
        speed = rng.uniform(3.2, 3.8)
        d.setLinearVelocity((speed * math.sin(tilt) * math.cos(a), speed * math.sin(tilt) * math.sin(a),
                             speed * math.cos(tilt)), False)
    for i in range(3):
        s = scene.addObject("Spark", ob, 42)
        s.worldPosition = (ob.worldPosition.x + rng.uniform(-0.3, 0.3), ob.worldPosition.y + rng.uniform(-0.3, 0.3),
                           ob.worldPosition.z + rng.uniform(0.0, 0.2))
'''

PICKER = '''\
# BGE-Remix P4: picking at 9 points, physics ray and Remix pick (T4.4)
import json
import os

import bge

c = bge.logic.getCurrentController()
ob = c.owner
scene = bge.logic.getCurrentScene()
cam = scene.active_camera
w, h = bge.render.getWindowWidth(), bge.render.getWindowHeight()
g = bge.logic.globalDict
if "p4_pick" not in g:
    g["p4_pick"] = {"i": 0, "f": 0, "results": [], "start": 40}
st = g["p4_pick"]
st["f"] += 1
points = ob["points"].split(";")
STEP = 12   # frames per point: the answer arrives within a few

def target(spec):
    """'Name' (its centre), 'Name@x,y,z' (a point in its space), 'Name#bone'
    (the posed bone's tip), 'xy@0.5,0.05' (screen)"""
    if "#" in spec:
        name, bone = spec.split("#")
        o = scene.objects[name]
        return cam.getScreenPosition(o.worldTransform * o.channels[bone].pose_tail)
    if spec.startswith("xy@"):
        x, y = [float(v) for v in spec[3:].split(",")]
        return x, y
    if "@" in spec:
        name, off = spec.split("@")
        o = scene.objects[name]
        p = o.worldTransform * __import__("mathutils").Vector([float(v) for v in off.split(",")])
        return cam.getScreenPosition(p)
    return cam.getScreenPosition(scene.objects[spec])

def name_of(o):
    return o.name if o is not None else None

if st["f"] >= st["start"] and st["i"] < len(points):
    k = (st["f"] - st["start"]) % STEP
    spec = points[st["i"]].split("=")
    label, where = spec[0], spec[1]
    nx, ny = target(where)
    px, py = int(nx * w), int(ny * h)
    if k == 0:
        bge.render.setMousePosition(px, py)
    # (official 2.79b has neither: T4.0 plays this scene there too)
    pick = getattr(bge.render, "remixPick", None)
    highlight = getattr(bge.render, "remixHighlight", None)
    remix = pick(px, py, True) if pick else None
    if remix is not None and highlight:
        highlight([remix])
    if k == STEP - 1:
        hit = cam.getScreenRay(nx, ny, 200.0)
        over = c.sensors["over"].hitObject if "over" in c.sensors else None
        st["results"].append({"point": label, "x": px, "y": py, "physics": name_of(hit),
                              "remix": name_of(remix), "mouse_over": name_of(over)})
        shot = os.environ.get("BGE_REMIX_P4_PICK_SHOTS")
        if shot:
            bge.render.makeScreenshot(os.path.join(shot, "T4.4_%d_%s.png" % (st["i"], label)))
        st["i"] += 1
        if highlight:
            highlight([])
    if st["i"] == len(points):
        path = os.environ.get("BGE_REMIX_P4_PICK_OUT")
        if path:
            with open(path, "w") as f:
                json.dump(st["results"], f, indent=2)
'''


def text(name, body):
    t = bpy.data.texts.get(name) or bpy.data.texts.new(name)
    t.clear()
    t.write(body)
    return t


def logic(ob, body_name, body, pulse=True, sensor_type='ALWAYS', extra_sensor=None):
    """An Always sensor (every tic) running a Python script on ob."""
    bpy.context.scene.objects.active = ob
    bpy.ops.logic.sensor_add(type=sensor_type, object=ob.name)
    bpy.ops.logic.controller_add(type='PYTHON', object=ob.name)
    sensor = ob.game.sensors[-1]
    controller = ob.game.controllers[-1]
    sensor.use_pulse_true_level = pulse
    sensor.tick_skip = 0
    controller.mode = 'SCRIPT'
    controller.text = text(body_name, body)
    sensor.link(controller)
    if extra_sensor:
        bpy.ops.logic.sensor_add(type='MOUSE', name=extra_sensor, object=ob.name)
        s = ob.game.sensors[-1]
        s.mouse_event = 'MOUSEOVERANY'
        s.use_pulse_true_level = True
        s.link(controller)
    return controller


def prop(ob, name, value):
    kind = {bool: 'BOOL', int: 'INT', float: 'FLOAT', str: 'STRING'}[type(value)]
    bpy.context.scene.objects.active = ob
    bpy.ops.object.game_property_new(type=kind, name=name)
    ob.game.properties[name].value = value


def select_only(ob):
    for o in bpy.context.scene.objects:
        o.select = False
    ob.select = True
    bpy.context.scene.objects.active = ob


# ---------------------------------------------------------------------------
# The Mannequin

BONES = [
    # name, head, tail, parent
    ("hips", (0, 0, 0.95), (0, 0, 1.10), None),
    ("spine", (0, 0, 1.10), (0, 0, 1.30), "hips"),
    ("chest", (0, 0, 1.30), (0, 0, 1.50), "spine"),
    ("neck", (0, 0, 1.50), (0, 0, 1.60), "chest"),
    ("head", (0, 0, 1.60), (0, 0, 1.85), "neck"),
    ("upper_arm.L", (0.22, 0, 1.45), (0.24, 0, 1.17), "chest"),
    ("forearm.L", (0.24, 0, 1.17), (0.25, 0, 0.90), "upper_arm.L"),
    ("upper_arm.R", (-0.22, 0, 1.45), (-0.24, 0, 1.17), "chest"),
    ("forearm.R", (-0.24, 0, 1.17), (-0.25, 0, 0.90), "upper_arm.R"),
    ("thigh.L", (0.10, 0, 0.95), (0.10, 0, 0.52), "hips"),
    ("shin.L", (0.10, 0, 0.52), (0.10, 0, 0.10), "thigh.L"),
    ("foot.L", (0.10, 0, 0.10), (0.10, -0.16, 0.03), "shin.L"),
    ("thigh.R", (-0.10, 0, 0.95), (-0.10, 0, 0.52), "hips"),
    ("shin.R", (-0.10, 0, 0.52), (-0.10, 0, 0.10), "thigh.R"),
    ("foot.R", (-0.10, 0, 0.10), (-0.10, -0.16, 0.03), "shin.R"),
]
BLEND = 0.06  # half width of the weight blend at each joint (m)


def smooth(u):
    u = min(max(u, 0.0), 1.0)
    return u * u * (3.0 - 2.0 * u)


def chain_weights(s, joints, bones):
    """weights along a chain: bones[i] runs from joints[i] to joints[i+1]
    (arc lengths); bones[0] may be a parent before the chain (joints[0] = 0)"""
    for j in range(1, len(joints) - 1):
        d = s - joints[j]
        if abs(d) < BLEND:
            u = smooth((d + BLEND) / (2 * BLEND))
            return [(bones[j - 1], 1.0 - u), (bones[j], u)]
    for i in range(len(bones)):
        if s < joints[i + 1] or i == len(bones) - 1:
            return [(bones[i], 1.0)]


def tube(points, radii, seg=12, step=0.03):
    """rings along a polyline; returns (verts, faces, arc length per vertex)"""
    verts, faces, arcs = [], [], []
    samples = []
    total = 0.0
    for i in range(len(points) - 1):
        a, b = Vector(points[i]), Vector(points[i + 1])
        n = max(int((b - a).length / step), 1)
        for k in range(n):
            u = k / float(n)
            samples.append((a.lerp(b, u), radii[i] + (radii[i + 1] - radii[i]) * u, total + (b - a).length * u,
                            (b - a).normalized()))
        total += (b - a).length
    a, b = Vector(points[-2]), Vector(points[-1])
    samples.append((b, radii[-1], total, (b - a).normalized()))
    for c, r, s, d in samples:
        side = Vector((1, 0, 0)) if abs(d.x) < 0.9 else Vector((0, 1, 0))
        x = d.cross(side).normalized()
        y = d.cross(x).normalized()
        base = len(verts)
        for k in range(seg):
            a = 2 * math.pi * k / seg
            verts.append(tuple(c + (x * math.cos(a) + y * math.sin(a)) * r))
            arcs.append(s)
        if base:
            for k in range(seg):
                faces.append((base - seg + k, base - seg + (k + 1) % seg, base + (k + 1) % seg, base + k))
    return verts, faces, arcs


def mannequin_mesh(name):
    """one mesh, about 3,000 triangles, with 15 vertex groups"""
    verts, faces, weights = [], [], []

    def add(vs, fs, ws):
        base = len(verts)
        verts.extend(vs)
        faces.extend(tuple(base + i for i in f) for f in fs)
        weights.extend(ws)

    # torso: hips to neck, a tube along Z
    pts = [(0, 0, 0.92), (0, 0, 1.10), (0, 0, 1.30), (0, 0, 1.50), (0, 0, 1.60)]
    vs, fs, arcs = tube(pts, [0.16, 0.15, 0.18, 0.20, 0.06])
    joints = [0.0, 0.18, 0.38, 0.58, 0.68]
    ws = []
    for v, s in zip(vs, arcs):
        w = chain_weights(s, joints, ["hips", "spine", "chest", "neck"])
        # a belt where hips and thighs pull on the torso: 3 and 4 bones a vertex
        if v[2] < 1.14:
            if v[0] > 0.02:
                w = w + [("thigh.L", 0.15)]
            elif v[0] < -0.02:
                w = w + [("thigh.R", 0.15)]
            else:
                w = w + [("thigh.L", 0.08), ("thigh.R", 0.08)]
        ws.append(w)
    add(vs, fs, ws)
    # head: a sphere
    rings, seg = 8, 12
    hv, hf, hw = [], [], []
    for i in range(rings + 1):
        th = math.pi * i / rings
        for k in range(seg):
            ph = 2 * math.pi * k / seg
            hv.append((0.11 * math.sin(th) * math.cos(ph), 0.11 * math.sin(th) * math.sin(ph), 1.72 + 0.12 * math.cos(th)))
            hw.append([("head", 1.0)] if i < rings - 1 else [("neck", 0.5), ("head", 0.5)])
    for i in range(rings):
        for k in range(seg):
            a, b = i * seg + k, i * seg + (k + 1) % seg
            hf.append((a, b, b + seg, a + seg))
    add(hv, hf, hw)
    # arms and legs
    for side, sx in (("L", 1.0), ("R", -1.0)):
        pts = [(0.21 * sx, 0, 1.47), (0.24 * sx, 0, 1.17), (0.25 * sx, 0, 0.90), (0.25 * sx, 0, 0.84)]
        vs, fs, arcs = tube(pts, [0.055, 0.045, 0.04, 0.035])
        l1 = (Vector(pts[1]) - Vector(pts[0])).length
        add(vs, fs, [chain_weights(s, [0.0, 0.05, l1, 10.0], ["chest", "upper_arm." + side, "forearm." + side])
                     for s in arcs])
        pts = [(0.10 * sx, 0, 0.97), (0.10 * sx, 0, 0.52), (0.10 * sx, 0, 0.10), (0.10 * sx, -0.17, 0.03)]
        vs, fs, arcs = tube(pts, [0.075, 0.06, 0.045, 0.04])
        l1 = (Vector(pts[1]) - Vector(pts[0])).length
        l2 = (Vector(pts[2]) - Vector(pts[1])).length
        add(vs, fs, [chain_weights(s, [0.0, 0.05, l1, l1 + l2, 10.0],
                                   ["hips", "thigh." + side, "shin." + side, "foot." + side]) for s in arcs])
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    for poly in me.polygons:
        poly.use_smooth = True
    return me, weights


def armature(scene, name, data=None):
    if data is None:
        data = bpy.data.armatures.new(name + "Data")
        ob = bpy.data.objects.new(name, data)
        scene.objects.link(ob)
        select_only(ob)
        bpy.ops.object.mode_set(mode='EDIT')
        for bname, head, tail, parent in BONES_NOW:
            eb = data.edit_bones.new(bname)
            eb.head = head
            eb.tail = tail
            eb.roll = 0.0
            if parent:
                eb.parent = data.edit_bones[parent]
                eb.use_connect = False
        bpy.ops.object.mode_set(mode='OBJECT')
    else:
        ob = bpy.data.objects.new(name, data)
        scene.objects.link(ob)
    return ob


BONES_NOW = BONES


def skinned(scene, name, me, weights, arm_ob, ma):
    ob = bpy.data.objects.new(name, me)
    scene.objects.link(ob)
    if not me.materials:
        me.materials.append(ma)
    groups = {}
    for bname, _, _, _ in BONES_NOW:
        groups[bname] = ob.vertex_groups.new(bname)
    if weights is not None:
        for vi, wl in enumerate(weights):
            for bname, w in wl:
                groups[bname].add([vi], w, 'ADD')
    mod = ob.modifiers.new("Armature", 'ARMATURE')
    mod.object = arm_ob
    mod.use_vertex_groups = True
    mod.use_bone_envelopes = False
    ob.parent = arm_ob
    ob.game.physics_type = 'NO_COLLISION'
    return ob


def rotate_bone(pb, axis, degrees):
    """a rotation about an armature-space axis, as the pose bone's local one"""
    m = pb.bone.matrix_local.to_3x3()
    r = Quaternion(Vector(axis), math.radians(degrees)).to_matrix()
    pb.rotation_quaternion = (m.inverted() * r * m).to_quaternion()


def walk_action(arm_ob):
    arm_ob.animation_data_create()
    act = bpy.data.actions.get("Walk")
    if act:
        return act
    act = bpy.data.actions.new("Walk")
    arm_ob.animation_data.action = act
    pose = arm_ob.pose
    for f in range(1, 26, 3):
        ph = 2 * math.pi * (f - 1) / 24.0
        swing = 30.0 * math.sin(ph)
        rotate_bone(pose.bones["thigh.L"], (1, 0, 0), swing)
        rotate_bone(pose.bones["thigh.R"], (1, 0, 0), -swing)
        rotate_bone(pose.bones["shin.L"], (1, 0, 0), -max(0.0, math.sin(ph + math.pi / 2)) * 40.0)
        rotate_bone(pose.bones["shin.R"], (1, 0, 0), -max(0.0, math.sin(ph - math.pi / 2)) * 40.0)
        rotate_bone(pose.bones["upper_arm.L"], (1, 0, 0), -0.6 * swing)
        rotate_bone(pose.bones["upper_arm.R"], (1, 0, 0), 0.6 * swing)
        rotate_bone(pose.bones["forearm.L"], (1, 0, 0), 15.0)
        rotate_bone(pose.bones["forearm.R"], (1, 0, 0), 15.0)
        pose.bones["hips"].location = (0.0, 0.02 * abs(math.cos(ph)), 0.0)
        for b in ("thigh.L", "thigh.R", "shin.L", "shin.R", "upper_arm.L", "upper_arm.R", "forearm.L", "forearm.R"):
            pose.bones[b].keyframe_insert("rotation_quaternion", frame=f)
        pose.bones["hips"].keyframe_insert("location", frame=f)
    return act


def wave_action(arm_ob):
    arm_ob.animation_data_create()
    act = bpy.data.actions.new("Wave")
    arm_ob.animation_data.action = act
    pose = arm_ob.pose
    for f in range(1, 26, 4):
        ph = 2 * math.pi * (f - 1) / 24.0
        rotate_bone(pose.bones["upper_arm.R"], (0, 1, 0), 150.0)
        rotate_bone(pose.bones["forearm.R"], (0, 1, 0), 30.0 * math.sin(ph))
        for b in ("upper_arm.R", "forearm.R"):
            pose.bones[b].keyframe_insert("rotation_quaternion", frame=f)
    return act


def reset_pose(arm_ob):
    for pb in arm_ob.pose.bones:
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)


def character(scene, name, location, ma, action, phase=0.0, radius=0.0, turn=0.0, center=(0.0, 0.0), share=None,
              rotation=0.0):
    """an armature (with the logic) and its skinned Mannequin mesh; share:
    (mesh, armature data) of another Mannequin to use the same"""
    global BONES_NOW
    BONES_NOW = BONES
    if share:
        me, data = share
        arm_ob = armature(scene, name, data)
        weights = None
    else:
        arm_ob = armature(scene, name)
        me, weights = mannequin_mesh(name + "Mesh")
    # a shared mesh keeps its weights; the object only needs the group
    # names, in the same order
    mesh_ob = skinned(scene, name + "Mesh", me, weights, arm_ob, ma)
    arm_ob.location = location
    arm_ob.rotation_euler = (0, 0, rotation)
    arm_ob.animation_data_create()
    act = walk_action(arm_ob) if action == "Walk" else wave_action(arm_ob)
    arm_ob.animation_data.action = None
    reset_pose(arm_ob)
    prop(arm_ob, "action", act.name)
    prop(arm_ob, "start", 1.0)
    prop(arm_ob, "length", 24.0)
    prop(arm_ob, "speed", 0.5)  # 24 frames in 48 tics: a step a second at 60 tics
    prop(arm_ob, "phase", float(phase))
    if radius > 0.0:
        prop(arm_ob, "radius", float(radius))
        prop(arm_ob, "turn", float(turn))
        prop(arm_ob, "cx", float(center[0]))
        prop(arm_ob, "cy", float(center[1]))
    logic(arm_ob, "p4_logic.py", LOGIC)
    return arm_ob, mesh_ob


# ---------------------------------------------------------------------------
# Other test objects

def pulse_ball(scene, name, location, ma):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, size=0.3, location=location)
    ob = bpy.context.object
    ob.name = name
    ob.data.name = name + "Mesh"
    ob.data.materials.append(ma)
    bpy.ops.object.shade_smooth()
    ob.shape_key_add("Basis")
    big = ob.shape_key_add("Big")
    for i, v in enumerate(ob.data.vertices):
        n = v.co.normalized()
        # bigger, with bumps: the shape is easy to tell from the basis
        big.data[i].co = v.co + n * (0.08 + 0.05 * math.sin(6 * math.atan2(n.y, n.x)))
    key = ob.data.shape_keys
    key.animation_data_create()
    for f, val in ((1, 0.0), (21, 1.0), (41, 0.0)):
        big.value = val
        big.keyframe_insert("value", frame=f)
    key.animation_data.action.name = "Pulse"
    big.value = 0.0
    ob.game.physics_type = 'NO_COLLISION'
    prop(ob, "action", "Pulse")
    prop(ob, "start", 1.0)
    prop(ob, "length", 40.0)
    prop(ob, "speed", 0.5)
    prop(ob, "phase", 0.0)
    logic(ob, "p4_logic.py", LOGIC)
    return ob


def smooth_monkey(scene, name, location, ma):
    bpy.ops.mesh.primitive_monkey_add(radius=0.35, location=location)
    ob = bpy.context.object
    ob.name = name
    ob.data.name = name + "Mesh"
    ob.data.materials.append(ma)
    ob.rotation_euler = (0, 0, math.radians(180))
    bpy.ops.object.shade_smooth()
    mod = ob.modifiers.new("Subsurf", 'SUBSURF')
    mod.levels = 2
    mod.render_levels = 2
    ob.game.physics_type = 'NO_COLLISION'
    return ob, len(ob.data.polygons)


def bending_bar(scene, location, ma, tip_ma):
    """a 1 m bar along X with two bones; the outer half bends 90 degrees
    about Y and back over 40 frames; the last 10 cm glow red"""
    global BONES_NOW
    BONES_NOW = [("root", (0, 0, 0), (0.5, 0, 0), None), ("tip", (0.5, 0, 0), (1.0, 0, 0), "root")]
    arm_ob = armature(scene, "Bar")
    verts, faces, weights, mats = [], [], [], []
    n = 20
    for i in range(n + 1):
        x = i / float(n)
        for y, z in ((-0.05, -0.05), (0.05, -0.05), (0.05, 0.05), (-0.05, 0.05)):
            verts.append((x, y, z))
            u = smooth((x - 0.45) / 0.1)
            weights.append([("root", 1.0 - u), ("tip", u)] if 0.0 < u < 1.0 else [("tip" if u >= 1.0 else "root", 1.0)])
    for i in range(n):
        for k in range(4):
            a, b = i * 4 + k, i * 4 + (k + 1) % 4
            faces.append((a, b, b + 4, a + 4))
            mats.append(1 if i >= n - 2 else 0)
    faces.append((0, 3, 2, 1))
    mats.append(0)
    faces.append((n * 4, n * 4 + 1, n * 4 + 2, n * 4 + 3))
    mats.append(1)
    me = bpy.data.meshes.new("BarMesh")
    me.from_pydata(verts, [], faces)
    me.update()
    me.materials.append(ma)
    me.materials.append(tip_ma)
    for poly, m in zip(me.polygons, mats):
        poly.material_index = m
    ob = skinned(scene, "BarMesh", me, weights, arm_ob, ma)
    arm_ob.location = location
    arm_ob.animation_data_create()
    act = bpy.data.actions.new("Bend")
    arm_ob.animation_data.action = act
    pb = arm_ob.pose.bones["tip"]
    for f, deg in ((1, 0.0), (21, 90.0), (41, 0.0)):
        rotate_bone(pb, (0, 1, 0), -deg)
        pb.keyframe_insert("rotation_quaternion", frame=f)
    arm_ob.animation_data.action = None
    reset_pose(arm_ob)
    prop(arm_ob, "action", "Bend")
    prop(arm_ob, "start", 1.0)
    prop(arm_ob, "length", 40.0)
    prop(arm_ob, "speed", 0.5)
    prop(arm_ob, "phase", 0.0)
    logic(arm_ob, "p4_logic.py", LOGIC)
    BONES_NOW = BONES
    return arm_ob, ob


def ground(scene, size=16.0, wall=True, color=(0.5, 0.5, 0.5)):
    grey = p3.bi_material("Ground", color, hard=10)
    floor = p2.plane(scene, "Floor", size, (0, 0, 0), (0, 0, 0), grey)
    back = None
    if wall:
        back = p2.plane(scene, "Backdrop", size, (0, size / 4.0, size / 4.0), (math.radians(90), 0, 0), grey)
    return floor, back


def lights(scene):
    p2.world(scene, (0.25, 0.27, 0.30))
    p2.lamp(scene, "Sun", 'SUN', (0, 0, 10), (math.radians(40), 0, math.radians(30)), energy=1.0)


def mannequin_material():
    return p3.bi_material("MannequinSkin", (0.55, 0.62, 0.75), hard=30)


def finish(scene, path):
    p3.finish(scene, path)


# ---------------------------------------------------------------------------
# The scenes

def make_characters(folder):
    scene = p3.base_scene()
    lights(scene)
    ground(scene)
    skin = mannequin_material()
    walker, walker_mesh = character(scene, "Walker", (0, 0, 0), skin, "Walk", radius=2.5, turn=0.008,
                                    center=(0.0, 0.5))
    share = (walker_mesh.data, walker.data)
    twin, _ = character(scene, "Twin", (3.4, 2.0, 0), skin, "Walk", phase=12.0, share=share)
    waver, _ = character(scene, "Waver", (-3.4, 2.0, 0), skin, "Wave", share=share)
    ball = pulse_ball(scene, "Pulse", (-1.2, -2.6, 0.8), p3.bi_material("PulseRed", (0.8, 0.25, 0.2), hard=40))
    pedestal = p2.cube(scene, "Pedestal", 0.4, (-1.2, -2.6, 0.2), p3.bi_material("Stone", (0.6, 0.6, 0.58), hard=10))
    monkey, monkey_faces = smooth_monkey(scene, "SmoothMonkey", (1.2, -2.6, 0.6),
                                         p3.bi_material("MonkeyGold", (0.8, 0.6, 0.2), hard=60))
    bar_arm, bar = bending_bar(scene, (1.9, -3.6, 0.35), p3.bi_material("BarGrey", (0.7, 0.7, 0.7), hard=30),
                               p2.material("BarTip", (1.0, 0.0, 0.0), ref=1.0, emit=2.0))
    p2.camera(scene, (0.0, -9.0, 3.0), (0.0, 0.5, 0.9), lens=28.0)
    finish(scene, os.path.join(folder, "p4_characters.blend"))
    return {
        "characters": ["Walker", "Twin", "Waver"],
        "mannequin_mesh": walker_mesh.data.name,
        "groups": len(BONES),
        "bar_mesh": "BarMesh",
        "bar_groups": 2,
        "pulse": "Pulse",
        "monkey": "SmoothMonkey",
        "monkey_faces": monkey_faces,
        "camera": [0.0, -9.0, 3.0],
    }


def make_crowd(folder):
    scene = p3.base_scene()
    lights(scene)
    ground(scene, size=24.0)
    skin = mannequin_material()
    first = None
    share = None
    for i in range(5):
        for j in range(5):
            name = "Crowd%d%d" % (i, j)
            loc = ((i - 2) * 1.6, (j - 2) * 1.6, 0)
            arm, mesh = character(scene, name, loc, skin, "Walk", phase=float((i * 5 + j) * 5 % 24), share=share)
            if share is None:
                share = (mesh.data, arm.data)
    p2.camera(scene, (0.0, -11.0, 5.0), (0.0, 0.0, 0.9), lens=26.0)
    finish(scene, os.path.join(folder, "p4_crowd.blend"))
    return {"count": 25}


def make_physics(folder):
    scene = p3.base_scene()
    lights(scene)
    floor, back = ground(scene, size=20.0, wall=False)
    floor.game.physics_type = 'STATIC'
    stone = p3.bi_material("RampStone", (0.55, 0.55, 0.52), hard=10)
    ramp = p2.cube(scene, "Ramp", 1.0, (0, 0.5, 1.2), stone)
    ramp.scale = (4.0, 6.0, 0.2)
    ramp.rotation_euler = (math.radians(20), 0, 0)
    ramp.game.physics_type = 'STATIC'
    for name, loc, sc in (("WallN", (0, 5, 0.3), (10, 0.2, 0.6)), ("WallS", (0, -5, 0.3), (10, 0.2, 0.6)),
                          ("WallE", (5, 0, 0.3), (0.2, 10, 0.6)), ("WallW", (-5, 0, 0.3), (0.2, 10, 0.6))):
        w = p2.cube(scene, name, 1.0, loc, stone)
        w.scale = sc
        w.game.physics_type = 'STATIC'
    colours = [(0.8, 0.2, 0.2), (0.2, 0.7, 0.3), (0.2, 0.4, 0.8), (0.9, 0.8, 0.2), (0.8, 0.8, 0.8)]
    meshes = []
    for k, c in enumerate(colours):
        ob = p2.cube(scene, "Box%d_000" % k, 0.25, (0, 0, -10), p3.bi_material("Box%d" % k, c, hard=40))
        meshes.append(ob.data)
        scene.objects.unlink(ob)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, size=0.15, location=(0, 0, -10))
    ball = bpy.context.object
    ball.data.name = "BallMesh"
    ball.data.materials.append(p3.bi_material("BallWhite", (0.9, 0.9, 0.9), hard=60))
    bpy.ops.object.shade_smooth()
    ball_mesh = ball.data
    scene.objects.unlink(ball)
    bodies = []
    n = 0
    for i in range(10):
        for j in range(10):
            for k in range(2):
                x, y, z = (i - 4.5) * 0.4, (j - 4.5) * 0.4 + 0.5, 4.0 + k * 0.5 + (i + j) % 3 * 0.1
                if n < 150:
                    ob = bpy.data.objects.new("Box_%03d" % n, meshes[n % 5])
                    ob.game.physics_type = 'RIGID_BODY'
                    ob.game.use_collision_bounds = True
                    ob.game.collision_bounds_type = 'BOX'
                    ob.game.mass = 1.0
                else:
                    ob = bpy.data.objects.new("Ball_%03d" % (n - 150), ball_mesh)
                    ob.game.physics_type = 'RIGID_BODY'
                    ob.game.use_collision_bounds = True
                    ob.game.collision_bounds_type = 'SPHERE'
                    ob.game.radius = 0.15
                    ob.game.mass = 0.5
                ob.location = (x, y, z)
                ob.rotation_euler = (0.3 * i, 0.2 * j, 0.1 * k)
                scene.objects.link(ob)
                bodies.append(ob.name)
                n += 1
    # the cloth: a 20 x 20 grid over a fixed ball
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, size=0.4, location=(3.2, -2.8, 0.4))
    pole = bpy.context.object
    pole.name = "ClothBall"
    pole.data.name = "ClothBallMesh"
    pole.data.materials.append(stone)
    pole.game.physics_type = 'STATIC'
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=20, y_subdivisions=20, radius=0.75, location=(3.2, -2.8, 1.4))
    cloth = bpy.context.object
    cloth.name = "Cloth"
    cloth.data.name = "ClothMesh"
    cloth.data.materials.append(p3.bi_material("ClothRed", (0.7, 0.15, 0.2), hard=10))
    cloth.game.physics_type = 'SOFT_BODY'
    sb = cloth.game.soft_body
    sb.linear_stiffness = 0.5
    sb.use_bending_constraints = True
    sb.collision_margin = 0.03
    sb.shape_threshold = 0.0
    sb.use_shape_match = False
    sb.cluster_iterations = 64
    cloth.game.mass = 1.0
    p2.camera(scene, (0.0, -10.0, 6.0), (0.0, 0.0, 0.8), lens=26.0)
    finish(scene, os.path.join(folder, "p4_physics.blend"))
    return {"bodies": bodies, "cloth": "Cloth"}


def dot(x, y):
    """a soft round glow (alpha)"""
    d = math.sqrt((x - 0.5) ** 2 + (y - 0.5) ** 2) * 2.0
    a = max(0.0, 1.0 - d) ** 2
    return (1.0, 0.75, 0.35, a)


def tree(x, y):
    """a simple tree outline: trunk and a round crown (alpha clip)"""
    trunk = abs(x - 0.5) < 0.06 and y < 0.4
    crown = (x - 0.5) ** 2 + ((y - 0.62) * 1.1) ** 2 < 0.13
    if crown:
        return (0.2, 0.5, 0.15, 1.0)
    if trunk:
        return (0.35, 0.22, 0.1, 1.0)
    return (0.0, 0.0, 0.0, 0.0)


def leaf(x, y):
    """a leaf shape in a square: the corners are clear"""
    u, v = (x - 0.5) * 2.0, (y - 0.5) * 2.0
    inside = (u * u) / 0.36 + (v * v) / 0.95 < 1.0
    vein = abs(u) < 0.03 and abs(v) < 0.9
    if inside:
        return (0.25, 0.45, 0.1, 1.0) if vein else (0.3, 0.65, 0.15, 1.0)
    return (0.0, 0.0, 0.0, 0.0)


def sprite_plane(scene, name, size, location, ma):
    """a square facing +X (BGE's billboard convention) with UVs"""
    s = size / 2.0
    me = bpy.data.meshes.new(name + "Mesh")
    me.from_pydata([(0, -s, -s), (0, s, -s), (0, s, s), (0, -s, s)], [], [(0, 1, 2, 3)])
    me.update()
    me.uv_textures.new("UVMap")
    for loop, uv in zip(me.uv_layers[0].data, [(0, 0), (1, 0), (1, 1), (0, 1)]):
        loop.uv = uv
    me.materials.append(ma)
    ob = bpy.data.objects.new(name, me)
    ob.location = location
    scene.objects.link(ob)
    return ob


def textured_bi(name, img, blend, emit=0.0, orientation='NORMAL', clip=False):
    ma = p2.material(name, (1.0, 1.0, 1.0), ref=1.0, emit=emit, hard=10, blend='ALPHA' if blend else ('CLIP' if clip else 'OPAQUE'))
    ma.use_transparency = blend or clip
    ma.alpha = 0.0 if (blend or clip) else 1.0
    slot = p3.bi_slot(ma, img, color=True)
    slot.use_map_alpha = True
    ma.game_settings.face_orientation = orientation
    ma.game_settings.use_backface_culling = False
    return ma


def make_fountain(folder):
    scene = p3.base_scene()
    p2.world(scene, (0.05, 0.06, 0.08))
    floor, back = ground(scene, size=12.0, color=(0.2, 0.2, 0.22))
    floor.modifiers.new("Collision", 'COLLISION')
    stone = p3.bi_material("BasinStone", (0.5, 0.5, 0.48), hard=10)
    bpy.ops.mesh.primitive_torus_add(major_radius=1.0, minor_radius=0.12, location=(0, 0, 0.12))
    basin = bpy.context.object
    basin.name = "Basin"
    basin.data.materials.append(stone)
    basin.game.physics_type = 'STATIC'
    column = p2.cube(scene, "Column", 1.0, (0, 0, 0.5), stone)
    column.scale = (0.15, 0.15, 1.0)
    column.game.physics_type = 'STATIC'
    emitter = bpy.data.objects.new("Spawner", None)
    emitter.location = (0, 0, 1.1)
    scene.objects.link(emitter)
    prop(emitter, "t", 0)
    logic(emitter, "p4_fountain.py", FOUNTAIN)
    # the templates live on layer 2 (added copies appear on layer 1)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, size=0.04, location=(0, 0, -5))
    drop = bpy.context.object
    drop.name = "Drop"
    drop.data.name = "DropMesh"
    drop.data.materials.append(p3.bi_material("DropBlue", (0.4, 0.65, 1.0), hard=120))
    drop.game.physics_type = 'DYNAMIC'
    drop.game.use_collision_bounds = True
    drop.game.collision_bounds_type = 'SPHERE'
    drop.game.radius = 0.04
    drop.layers = [i == 1 for i in range(20)]
    glow_img = p3.image("SparkGlow", dot, size=SIZE)
    spark_ma = textured_bi("SparkHalo", glow_img, blend=True, emit=3.0, orientation='HALO')
    spark = sprite_plane(scene, "Spark", 0.15, (0, 0, -5), spark_ma)
    spark.game.physics_type = 'NO_COLLISION'
    select_only(spark)
    bpy.ops.logic.sensor_add(type='ALWAYS', object=spark.name)
    bpy.ops.logic.controller_add(type='LOGIC_AND', object=spark.name)
    bpy.ops.logic.actuator_add(type='MOTION', object=spark.name)
    s, c, a = spark.game.sensors[-1], spark.game.controllers[-1], spark.game.actuators[-1]
    s.use_pulse_true_level = True
    a.offset_location = (0.0, 0.0, 0.02)
    a.use_local_location = False
    s.link(c)
    a.link(c)
    spark.layers = [i == 1 for i in range(20)]
    tree_img = p3.image("TreeSprite", tree, size=SIZE)
    tree_ma = textured_bi("TreeBillboard", tree_img, blend=False, orientation='BILLBOARD', clip=True)
    tree_ob = sprite_plane(scene, "Tree", 2.0, (-3.0, 2.0, 1.0), tree_ma)
    tree_ob.game.physics_type = 'NO_COLLISION'
    p2.lamp(scene, "BasinLight", 'POINT', (0, -0.6, 0.6), energy=2.0, color=(0.6, 0.8, 1.0), distance=5.0)
    p2.lamp(scene, "Sun", 'SUN', (0, 0, 10), (math.radians(50), 0, math.radians(30)), energy=0.4)
    # P4.6: a Blender particle system, sparks rising from a small hidden plane
    sparks_ma = p2.material("EmitterSpark", (1.0, 0.5, 0.15), ref=1.0, emit=4.0)
    pe = p2.plane(scene, "SparkEmitter", 0.3, (2.4, -0.6, 0.05), (0, 0, 0), sparks_ma)
    pe.game.physics_type = 'NO_COLLISION'
    select_only(pe)
    bpy.ops.object.particle_system_add()
    part = pe.particle_systems[0].settings
    part.name = "Sparks"
    pe.particle_systems[0].name = "Sparks"
    part.count = 600
    part.frame_start = 1
    part.frame_end = 200
    part.lifetime = 30
    part.lifetime_random = 0.4
    part.normal_factor = 3.0
    part.factor_random = 1.0
    part.object_factor = 0.0
    part.particle_size = 0.03
    part.size_random = 0.5
    part.render_type = 'HALO'
    part.use_render_emitter = False
    part.effector_weights.gravity = 1.0
    p2.camera(scene, (0.0, -4.5, 1.8), (0.0, 0.0, 1.0), lens=24.0)
    finish(scene, os.path.join(folder, "p4_fountain.blend"))
    fps = scene.render.fps / scene.render.fps_base
    rate = 600.0 / (200.0 - 1.0) * fps
    life = 30.0 / fps
    return {
        "camera": [0.0, -4.5, 1.8],
        "spark_mesh": "SparkMesh",
        "tree": "Tree",
        "tree_mesh": "TreeMesh",
        "drops_per_tic": 5,
        "sparks_per_tic": 3,
        "emitter": {"mesh": "SparkEmitter", "rate": rate, "life_max": life, "life_min": life * 0.6,
                    "speed_cm": 300.0, "cone": math.degrees(math.atan2(1.0, 3.0)),
                    "gravity_cm": scene.game_settings.physics_gravity * 100.0,
                    "size_max_cm": 3.0, "size_min_cm": 1.5, "hide": 1, "collide": 1,
                    "restitution": 1.0 - floor.collision.damping, "billboard": 0},
    }


def make_picking(folder):
    scene = p3.base_scene()
    lights(scene)
    floor, back = ground(scene, size=12.0)
    floor.name = "PickFloor"
    back.name = "PickBackdrop"
    back.location = (0, 3.0, 1.25)
    back.scale = (1.0, 0.21, 1.0)  # 2.5 m tall
    floor.game.physics_type = 'STATIC'
    back.game.physics_type = 'STATIC'
    red = p3.bi_material("PickRed", (0.8, 0.2, 0.2), hard=40)
    cube = p2.cube(scene, "PickCube", 0.6, (-2.4, 0, 0.3), red)
    cube.game.physics_type = 'STATIC'
    bpy.ops.mesh.primitive_uv_sphere_add(segments=24, ring_count=12, size=0.3, location=(-1.2, 0, 0.3))
    ball = bpy.context.object
    ball.name = "PickBall"
    ball.data.materials.append(p3.bi_material("PickBlue", (0.2, 0.4, 0.8), hard=40))
    bpy.ops.object.shade_smooth()
    ball.game.physics_type = 'RIGID_BODY'
    ball.game.use_collision_bounds = True
    ball.game.collision_bounds_type = 'SPHERE'
    ball.game.radius = 0.3
    bpy.ops.mesh.primitive_cone_add(radius1=0.3, depth=0.8, location=(0, 0, 0.4))
    cone = bpy.context.object
    cone.name = "PickCone"
    cone.data.materials.append(p3.bi_material("PickGreen", (0.2, 0.7, 0.3), hard=40))
    cone.game.physics_type = 'NO_COLLISION'
    leaf_img = p3.image("LeafClip", leaf, size=SIZE)
    leaf_ma = textured_bi("PickLeafClip", leaf_img, blend=False, clip=True)
    lf = sprite_plane(scene, "PickLeaf", 0.9, (1.2, 0, 0.55), leaf_ma)
    lf.rotation_euler = (0, 0, math.radians(-90))  # +X towards -Y: faces the camera
    lf.game.physics_type = 'STATIC'
    skin = mannequin_material()
    walker, walker_mesh = character(scene, "PickWalker", (2.5, 0.2, 0), skin, "Walk")
    tester = bpy.data.objects.new("PickTester", None)
    scene.objects.link(tester)
    points = [
        ("cube", "PickCube"),
        ("ball", "PickBall"),
        ("cone", "PickCone"),
        ("leaf_centre", "PickLeaf"),
        ("leaf_corner", "PickLeaf@0,0.38,0.38"),
        ("walker_body", "PickWalker@0,0,1.3"),
        ("walker_hand", "PickWalker#forearm.L"),
        ("gap", "PickCone@-0.6,0,0.15"),
        ("sky", "xy@0.5,0.03"),
    ]
    prop(tester, "points", ";".join("%s=%s" % p for p in points))
    logic(tester, "p4_picker.py", PICKER, extra_sensor="over")
    p2.camera(scene, (0.0, -6.0, 1.2), (0.0, 0.0, 0.6), lens=30.0)
    finish(scene, os.path.join(folder, "p4_picking.blend"))
    behind = ["PickBackdrop", "PickFloor"]
    return {
        "points": [p[0] for p in points],
        # what each point should give: physics (stock BGE) and Remix (RTX);
        # the fake runtime picks by screen bounds (no alpha, no exact shapes)
        "physics": {"cube": ["PickCube"], "ball": ["PickBall"], "cone": behind, "leaf_centre": ["PickLeaf"],
                    "leaf_corner": ["PickLeaf"], "walker_body": behind, "walker_hand": behind, "gap": behind,
                    "sky": [None]},
        "remix": {"cube": ["PickCube"], "ball": ["PickBall"], "cone": ["PickCone"], "leaf_centre": ["PickLeaf"],
                  "leaf_corner": behind, "walker_body": ["PickWalkerMesh"], "walker_hand": ["PickWalkerMesh"],
                  "gap": behind, "sky": [None]},
        "fake": {"cube": ["PickCube"], "ball": ["PickBall"], "cone": ["PickCone"], "leaf_centre": ["PickLeaf"],
                 "leaf_corner": ["PickLeaf"], "walker_body": ["PickWalkerMesh"], "walker_hand": ["PickWalkerMesh"],
                 "gap": behind, "sky": [None]},
    }


def main():
    folder = p2.out_dir()
    expected = {}
    expected["characters"] = make_characters(folder)
    expected["crowd"] = make_crowd(folder)
    expected["physics"] = make_physics(folder)
    expected["fountain"] = make_fountain(folder)
    expected["picking"] = make_picking(folder)
    with open(os.path.join(folder, "p4_expected.json"), "w") as f:
        json.dump(expected, f, indent=2)
    print("BGE-Remix: wrote", os.path.join(folder, "p4_expected.json"))


if __name__ == "__main__":
    main()
