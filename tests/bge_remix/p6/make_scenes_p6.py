# BGE-Remix P6 test scenes. Runs inside Blender 2.79b (Python 3.5: no f-strings).
#
#   blender --background --factory-startup --python make_scenes_p6.py -- <out_dir>
#
# Writes (no outside assets):
#   p6_physics_bullet.blend, p6_physics_physx_cpu.blend, p6_physics_physx_gpu.blend
#       the same scene with each engine (World > Physics): a 20 m grey floor
#       with a box stack (4 levels of 5 x 5 boxes of 0.5 m), three balls
#       dropped from 3 m (bounce 0, 0.5, 0.9), a 30 degree ramp with two
#       boxes (friction 0.1 and 0.9), a seesaw (a plank on a hinge, a box
#       dropped on one end), a pendulum (three balls in a ball-joint chain),
#       a slider (a block on a 6DOF joint free along X, pushed), a ball
#       falling through a ghost box and a sensor box past a near sensor, a
#       character walking and jumping, a four-wheel car driving straight
#       then turning, and a small cloth (soft body)
#   p6_bench_<engine>.blend   a floor and boxes added by script
#                             (BGE_REMIX_P6_BOXES of them) for B6.1
#   p6_expected.json          what the tests expect
#
# The probe (p6_probe.py) runs every logic tic; with "-g fixedtime = 1" every
# tic is one 1/60 s physics step, so the runs are the same on any machine.

import json
import math
import os
import sys
import traceback

import bpy
from mathutils import Euler, Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p3"))
import make_scenes_p2 as p2  # noqa: E402
import make_scenes_p3 as p3  # noqa: E402

TICS = 600
ENGINES = (("bullet", 'BULLET', None), ("physx_cpu", 'PHYSX', 'CPU'), ("physx_gpu", 'PHYSX', 'GPU'))

STACK = (-7.0, -7.0)
BALLS = ((-8.0, 2.0, 0.0), (-7.0, 2.0, 0.5), (-6.0, 2.0, 0.9))  # x, y, bounce
DROP = 3.0          # bottom of the balls above the floor
BALL_R = 0.25
RAMP = (-6.0, 5.0)
SEESAW = (7.0, 7.0)
PENDULUM = (2.0, 8.5, 4.0)
SLIDER = (7.0, -7.0, 1.0)
SENSORS = (7.0, -2.0)
CHARACTER = (-2.0, -7.0)
CAR = (1.0, -4.0)   # heading +Y (a negative engine force with the axle along -X)
CLOTH = (-3.0, 2.0, 2.0)


# ---------------------------------------------------------------------------
# The probe: positions every tic, sensors, rays, the end of the game

PROBE = '''\
# BGE-Remix P6 probe
import json
import math
import os

import bge

g = bge.logic.globalDict
scene = bge.logic.getCurrentScene()
env = os.environ
if "p6" not in g:
    g["p6"] = {"tic": 0, "track": {}, "sensors": {}, "rays": {}, "physics_ms": [], "every": {}}
st = g["p6"]
st["tic"] += 1
tic = st["tic"]
tics = int(env.get("BGE_REMIX_P6_TICS", "TICS"))
objects = scene.objects

def pose(ob):
    p = ob.worldPosition
    e = ob.worldOrientation.to_euler()
    return [round(p.x, 4), round(p.y, 4), round(p.z, 4), round(e.x, 4), round(e.y, 4), round(e.z, 4)]

# every tic: the moving things; the stack every 30 tics and at the end
for name in TRACK_EVERY:
    if name in objects:
        st["track"].setdefault(name, []).append(pose(objects[name]))
if tic % 30 == 0 or tic == tics:
    for name in TRACK_SOMETIMES:
        if name in objects:
            st["every"].setdefault(name, []).append([tic] + pose(objects[name]))

# sensors: the tics they are positive
for owner, sensor in SENSORS:
    if owner in objects and sensor in objects[owner].sensors and objects[owner].sensors[sensor].positive:
        st["sensors"].setdefault(owner + "." + sensor, []).append(tic)

# rays (T6.3)
if tic == 100:
    for label, a, b in RAYS:
        hit = scene.objects["Probe"].rayCast(b, a, 0.0, "", 0, 0, 0)
        st["rays"][label] = [hit[0].name if hit[0] else None, [round(v, 3) for v in hit[1]] if hit[1] else None]

# the physics time (B6.1)
info = bge.logic.getProfileInfo()
if "Physics:" in info:
    st["physics_ms"].append(info["Physics:"][0])

# a screenshot at a tic (RTX sheets)
shot = env.get("BGE_REMIX_P6_SHOT")
if shot and tic == int(env.get("BGE_REMIX_P6_SHOT_TIC", "0")):
    bge.render.makeScreenshot(shot)

if tic >= tics:
    ms = st["physics_ms"][60:] or st["physics_ms"]
    out = {"frames": tic, "track": st["track"], "every": st["every"], "sensors": st["sensors"], "rays": st["rays"],
           "physics_ms_mean": sum(ms) / len(ms) if ms else None, "objects": len(objects)}
    path = env.get("BGE_REMIX_P2_OUT")
    if path:
        with open(path, "w") as f:
            json.dump(out, f)
    bge.logic.endGame()
'''

CHARACTER_LOGIC = '''\
# BGE-Remix P6: the character walks along +X from tic 30 to 90, jumps at 120
import bge
c = bge.logic.getCurrentController()
ob = c.owner
t = ob.get("t", 0) + 1
ob["t"] = t
ch = bge.constraints.getCharacter(ob)
if ch:
    ch.walkDirection = (0.05, 0.0, 0.0) if 30 <= t < 90 else (0.0, 0.0, 0.0)
    if t == 120:
        ch.jump()
    if t % 10 == 0:
        print("BGE-Remix P6 character t=%d onGround=%d" % (t, ch.onGround))
'''

CAR_LOGIC = '''\
# BGE-Remix P6: a four-wheel car: 2 s straight, 1.5 s turning, coasting, braking
import bge
c = bge.logic.getCurrentController()
ob = c.owner
t = ob.get("t", 0) + 1
ob["t"] = t
if t == 1:
    cid = bge.constraints.createConstraint(ob.getPhysicsId(), 0, 11)
    car = bge.constraints.getVehicleConstraint(cid.getConstraintId())
    ob["cid"] = cid.getConstraintId()
    for i, (name, steer) in enumerate(WHEELS):
        w = bge.logic.getCurrentScene().objects[name]
        car.addWheel(w, w.worldPosition - ob.worldPosition, (0, 0, -1), (-1, 0, 0), 0.3, 0.3, steer)
        car.setSuspensionStiffness(20.0, i)
        car.setSuspensionDamping(2.3, i)
        car.setSuspensionCompression(4.4, i)
        car.setTyreFriction(10.0, i)
        car.setRollInfluence(0.1, i)
car = bge.constraints.getVehicleConstraint(ob["cid"]) if "cid" in ob else None
if car:
    # forward (+Y) with a negative force; wheels 0, 1 at +Y steer, 2, 3 drive
    force = -60.0 if 60 <= t < 180 else 0.0
    brake = 10.0 if t >= 300 else 0.0
    steer = 0.3 if 180 <= t < 270 else 0.0
    for i in range(4):
        car.applyEngineForce(force if i >= 2 else 0.0, i)
        car.applyBraking(brake, i)
        if i < 2:
            car.setSteeringValue(steer, i)
'''

SLIDER_LOGIC = '''\
# BGE-Remix P6: the slider block is pushed along +X at tic 30
import bge
ob = bge.logic.getCurrentController().owner
t = ob.get("t", 0) + 1
ob["t"] = t
if t == 30:
    ob.setLinearVelocity((3.0, 0.0, 0.0), False)
'''


# ---------------------------------------------------------------------------
# Helpers

def material(name, color, friction=0.5, bounce=0.0):
    ma = p3.bi_material(name, color, hard=30)
    ma.physics.friction = friction
    ma.physics.elasticity = bounce
    return ma


def box(scene, name, size, location, ma, physics='RIGID_BODY', mass=1.0, rotation=(0, 0, 0), unscaled=False):
    """unscaled: the size in the mesh, the object's scale 1 (a joint's
    limits are in units of the object's scale)"""
    if unscaled:
        ob = p2.cube(scene, name, size, location, ma)
    else:
        ob = p2.cube(scene, name, 1.0, location, ma)
        ob.scale = size if isinstance(size, (tuple, list)) else (size, size, size)
    ob.rotation_euler = rotation
    ob.game.physics_type = physics
    # box bounds for static boxes too: Bullet adds a triangle mesh's margin
    # (4 cm) around it, things would rest that far above
    ob.game.use_collision_bounds = True
    ob.game.collision_bounds_type = 'BOX'
    if physics in ('RIGID_BODY', 'DYNAMIC'):
        ob.game.mass = mass
    return ob


def ball(scene, name, radius, location, ma, physics='RIGID_BODY', mass=1.0):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, size=radius, location=location)
    ob = bpy.context.object
    ob.name = name
    ob.data.name = name + "Mesh"
    ob.data.materials.append(ma)
    bpy.ops.object.shade_smooth()
    ob.game.physics_type = physics
    ob.game.use_collision_bounds = True
    ob.game.collision_bounds_type = 'SPHERE'
    ob.game.radius = radius
    ob.game.mass = mass
    return ob


def anchor(scene, name, location):
    """an invisible static ghost: the other end of a joint (2.79 makes joints
    only to objects with physics, and empties get none)"""
    ob = p2.cube(scene, name, 0.1, location, material(name + "Ma", (0.2, 0.2, 0.2)))
    ob.game.physics_type = 'STATIC'
    ob.game.use_collision_bounds = True
    ob.game.collision_bounds_type = 'BOX'
    ob.game.use_ghost = True
    ob.hide_render = True   # the game's Invisible
    return ob


def joint(ob, target, kind, pivot=(0, 0, 0), axis=(0, 0, 0)):
    c = ob.constraints.new('RIGID_BODY_JOINT')
    c.target = target
    c.pivot_type = kind
    c.pivot_x, c.pivot_y, c.pivot_z = pivot
    c.axis_x, c.axis_y, c.axis_z = axis
    return c


def logic(scene, ob, name, body, sensor='ALWAYS'):
    scene.objects.active = ob
    bpy.ops.logic.sensor_add(type=sensor, object=ob.name)
    bpy.ops.logic.controller_add(type='PYTHON', object=ob.name)
    s = ob.game.sensors[-1]
    c = ob.game.controllers[-1]
    s.use_pulse_true_level = True
    s.tick_skip = 0
    c.mode = 'SCRIPT'
    t = bpy.data.texts.new(name)
    t.write(body)
    c.text = t
    s.link(c)
    return c


def watched_sensor(scene, ob, kind, name, **settings):
    """a sensor linked to an AND controller, so the engine evaluates it"""
    scene.objects.active = ob
    bpy.ops.logic.sensor_add(type=kind, name=name, object=ob.name)
    s = ob.game.sensors[-1]
    for k, v in settings.items():
        setattr(s, k, v)
    bpy.ops.logic.controller_add(type='LOGIC_AND', object=ob.name)
    s.link(ob.game.controllers[-1])
    return s


def set_engine(scene, engine, device):
    """World > Physics; on a build without PhysX in the list (an older
    build making the scenes) the scene ID property the list is kept in"""
    gs = scene.game_settings
    try:
        gs.physics_engine = engine
        if device:
            gs.physx_device = device
    except (TypeError, AttributeError):
        gs.physics_engine = 'BULLET'
        if engine == 'PHYSX':
            scene["_bge_physx"] = 2 if device == 'GPU' else 1
        elif "_bge_physx" in scene:
            del scene["_bge_physx"]


def base(scene_name="P6"):
    scene = p3.base_scene()
    scene.name = scene_name
    gs = scene.game_settings
    gs.fps = 60
    gs.logic_step_max = 5
    gs.physics_step_max = 5
    gs.physics_step_sub = 1
    gs.physics_gravity = 9.81
    p2.world(scene, (0.25, 0.27, 0.30))
    p2.lamp(scene, "Sun", 'SUN', (0, 0, 10), (math.radians(40), 0, math.radians(30)), energy=1.0)
    return scene


def finish(scene, track_every, track_sometimes, sensors, rays, path):
    probe = bpy.data.objects.new("Probe", None)
    scene.objects.link(probe)
    body = (PROBE.replace("TRACK_EVERY", repr(list(track_every)))
                 .replace("TRACK_SOMETIMES", repr(list(track_sometimes)))
                 .replace("SENSORS", repr(list(sensors)))
                 .replace("RAYS", repr(list(rays)))
                 .replace('"TICS"', '"%d"' % TICS))
    logic(scene, probe, "p6_probe.py", body)
    scene["bge_remix"] = 1
    scene.update()
    p2.save(path)


# ---------------------------------------------------------------------------
# p6_physics

def make_physics(folder):
    scene = base()
    expected = {"tics": TICS}
    grey = material("Ground", (0.5, 0.5, 0.5), friction=0.5, bounce=1.0)
    floor = box(scene, "Floor", (20.0, 20.0, 0.2), (0, 0, -0.1), grey, physics='STATIC')

    # the stack: 4 levels of 5 x 5 boxes of 0.5 m
    colours = [material("Box%d" % k, c) for k, c in enumerate(
        [(0.8, 0.2, 0.2), (0.2, 0.7, 0.3), (0.2, 0.4, 0.8), (0.9, 0.8, 0.2), (0.8, 0.8, 0.8)])]
    stack = []
    for lvl in range(4):
        for i in range(5):
            for j in range(5):
                name = "Stack_%d_%d_%d" % (lvl, i, j)
                loc = (STACK[0] + (i - 2) * 0.505, STACK[1] + (j - 2) * 0.505, 0.25 + lvl * 0.5 + 0.002 * lvl)
                box(scene, name, 0.5, loc, colours[(i + j + lvl) % 5])
                stack.append(name)
    expected["stack"] = {n: [STACK[0] + (int(n.split("_")[2]) - 2) * 0.505, STACK[1] + (int(n.split("_")[3]) - 2) * 0.505,
                             0.25 + int(n.split("_")[1]) * 0.5] for n in stack}

    # three balls (bounce 0, 0.5, 0.9) on a floor with bounce 1 (multiplied)
    balls = []
    for k, (x, y, e) in enumerate(BALLS):
        name = "Ball_%d" % k
        ball(scene, name, BALL_R, (x, y, DROP + BALL_R), material("BallMa%d" % k, (0.9, 0.9, 0.9), bounce=e))
        balls.append(name)
    expected["balls"] = {n: {"bounce": e, "rebound": e * e * DROP} for n, (_, _, e) in zip(balls, BALLS)}

    # the ramp: 30 degrees, friction 1; boxes with friction 0.1 and 0.9
    ramp_ma = material("RampMa", (0.55, 0.55, 0.52), friction=1.0)
    tilt = math.radians(30.0)
    # down towards +Y, a wall at its foot stops the sliding box
    ramp = box(scene, "Ramp", (3.0, 4.0, 0.2), (RAMP[0], RAMP[1], 1.2), ramp_ma, physics='STATIC',
               rotation=(-tilt, 0, 0))
    box(scene, "RampStop", (3.0, 0.3, 1.0), (RAMP[0], RAMP[1] + 2.6, 0.5), ramp_ma, physics='STATIC')
    up = Matrix.Rotation(-tilt, 3, 'X') * Vector((0, 0, 1))
    for k, mu in enumerate((0.1, 0.9)):
        centre = Vector((RAMP[0] - 0.7 + 1.4 * k, RAMP[1], 1.2)) + up * (0.1 + 0.2 + 0.01)
        box(scene, "RampBox_%d" % k, 0.4, tuple(centre), material("RampBoxMa%d" % k, (0.3, 0.3 + 0.5 * k, 0.8), friction=mu),
            rotation=(-tilt, 0, 0))
    expected["ramp"] = {"RampBox_0": "slides", "RampBox_1": "sticks"}

    # the seesaw: a plank on a hinge (axis X) over a fulcrum, a box dropped on its +Y end
    wood = material("Wood", (0.6, 0.45, 0.25), friction=1.0)
    box(scene, "Fulcrum", (0.3, 0.3, 0.3), (SEESAW[0], SEESAW[1], 0.15), wood, physics='STATIC')
    plank = box(scene, "Plank", (0.4, 3.0, 0.1), (SEESAW[0], SEESAW[1], 0.6), wood, mass=2.0)
    pivot = anchor(scene, "SeesawPivot", (SEESAW[0], SEESAW[1], 0.6))
    joint(plank, pivot, 'HINGE')
    box(scene, "SeesawBox", 0.3, (SEESAW[0], SEESAW[1] + 1.1, 2.0), material("SeesawBoxMa", (0.9, 0.3, 0.1), friction=1.0),
        mass=3.0)

    # the pendulum: three balls in a ball-joint chain, starting level along +X
    px, py, pz = PENDULUM
    top = anchor(scene, "PendulumAnchor", (px, py, pz))
    prev = top
    chain = []
    for k in range(3):
        b = ball(scene, "Pendulum_%d" % k, 0.15, (px + 1.0 + k, py, pz), material("PendMa%d" % k, (0.8, 0.8, 0.2)),
                 mass=1.0)
        b.game.damping = 0.8
        b.game.rotation_damping = 0.8
        joint(b, prev, 'BALL', pivot=(-1.0, 0, 0))
        chain.append(b.name)
        prev = b
    expected["pendulum"] = {"anchor": [px, py, pz], "chain": chain, "rest": [[px, py, pz - 1.0 - k] for k in range(3)]}

    # the slider: a block on a 6DOF joint, free along X from -1 to 1, pushed
    sx, sy, sz = SLIDER
    rail = anchor(scene, "SliderRail", (sx, sy, sz))
    block = box(scene, "Slider", 0.4, (sx, sy, sz), material("SliderMa", (0.2, 0.8, 0.8)), unscaled=True)
    c = joint(block, rail, 'GENERIC_6_DOF')
    c.use_limit_x, c.limit_min_x, c.limit_max_x = True, -1.0, 1.0
    c.use_limit_y = c.use_limit_z = True
    c.use_angular_limit_x = c.use_angular_limit_y = c.use_angular_limit_z = True
    logic(scene, block, "p6_slider.py", SLIDER_LOGIC)
    expected["slider"] = {"end": [sx + 1.0, sy, sz]}

    # sensors: a ball falls through a ghost box and a sensor box past a near sensor
    gx, gy = SENSORS
    glass = material("Glass", (0.6, 0.8, 0.9))
    ghost = box(scene, "GhostBox", 1.0, (gx, gy, 4.0), glass, physics='STATIC')
    ghost.game.use_ghost = True
    watched_sensor(scene, ghost, 'COLLISION', "touch")
    sensor = box(scene, "SensorBox", 1.0, (gx, gy, 2.0), glass, physics='SENSOR')
    watched_sensor(scene, sensor, 'COLLISION', "touch")
    post = box(scene, "NearPost", (0.2, 0.2, 1.0), (gx + 1.0, gy, 0.5), wood, physics='STATIC')
    watched_sensor(scene, post, 'NEAR', "near", distance=1.2, reset_distance=1.3)
    ball(scene, "Runner", 0.2, (gx, gy, 6.0), material("RunnerMa", (0.9, 0.2, 0.6)))
    g = 9.81

    def tic_at(drop):
        return int(math.ceil(math.sqrt(2.0 * drop / g) * 60.0))
    expected["sensors"] = {
        # the ball (r 0.2, from z 6) inside the ghost box (z 3.5..4.5) and the sensor box (1.5..2.5)
        "GhostBox.touch": [tic_at(6.0 - 4.7), tic_at(6.0 - 3.3)],
        "SensorBox.touch": [tic_at(6.0 - 2.7), tic_at(6.0 - 1.3)],
        # within 1.2 m of the post (from its middle at z 0.5) until the end
        "NearPost.near": [tic_at(6.0 - 1.48), TICS],
    }
    rays = [
        ("stack_top", [STACK[0] + 0.01, STACK[1] + 0.01, 10.0], [STACK[0] + 0.01, STACK[1] + 0.01, -1.0]),
        ("ghost_hit", [gx, gy, 10.0], [gx, gy, -1.0]),
        ("through_sensor", [gx, gy, 3.0], [gx, gy, -1.0]),
        ("floor", [0.5, 0.5, 5.0], [0.5, 0.5, -5.0]),
    ]
    expected["rays"] = {"stack_top": ["Stack_3_2_2", 2.0], "ghost_hit": ["GhostBox", 4.5],
                        "through_sensor": ["Runner", 0.4], "floor": ["Floor", 0.0]}

    # the character: a capsule walking 3 m along +X, then jumping
    cx, cy = CHARACTER
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.3, depth=1.8, location=(cx, cy, 0.95))
    ch = bpy.context.object
    ch.name = "Character"
    ch.data.materials.append(material("CharMa", (0.3, 0.6, 0.3)))
    ch.game.physics_type = 'CHARACTER'
    ch.game.use_collision_bounds = True
    ch.game.collision_bounds_type = 'CAPSULE'
    ch.game.jump_speed = 10.0
    ch.game.fall_speed = 55.0
    ch.game.step_height = 0.15
    logic(scene, ch, "p6_character.py", CHARACTER_LOGIC)
    expected["character"] = {"start": [cx, cy], "walk": 3.0, "jump": 100.0 / (2 * 29.4)}

    # the car: a chassis on four raycast wheels
    vx, vy = CAR
    car_ma = material("CarMa", (0.8, 0.1, 0.1))
    chassis = box(scene, "Car", (1.0, 2.0, 0.4), (vx, vy, 0.9), car_ma, mass=100.0)
    tyre = material("Tyre", (0.1, 0.1, 0.1))
    wheels = []
    for k, (dx, dy) in enumerate(((-0.6, 0.75), (0.6, 0.75), (-0.6, -0.75), (0.6, -0.75))):
        bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=0.3, depth=0.2, location=(vx + dx, vy + dy, 0.6),
                                            rotation=(0, math.radians(90), 0))
        w = bpy.context.object
        w.name = "Wheel_%d" % k
        w.data.materials.append(tyre)
        w.game.physics_type = 'NO_COLLISION'
        wheels.append((w.name, k < 2))
    logic(scene, chassis, "p6_car.py", CAR_LOGIC.replace("WHEELS", repr(wheels)))
    expected["car"] = {"start": [vx, vy]}

    # a small cloth (soft body): falls with Bullet and on the GPU, stays still on the CPU
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=10, y_subdivisions=10, radius=0.5, location=CLOTH)
    cloth = bpy.context.object
    cloth.name = "Cloth"
    cloth.data.materials.append(material("ClothMa", (0.7, 0.15, 0.2)))
    cloth.game.physics_type = 'SOFT_BODY'
    cloth.game.soft_body.linear_stiffness = 0.5
    cloth.game.soft_body.use_bending_constraints = True
    cloth.game.mass = 1.0

    p2.camera(scene, (0.0, -19.0, 11.0), (0.0, 0.0, 0.5), lens=24.0)
    every = balls + ["Plank", "SeesawBox"] + chain + ["Slider", "Runner", "Character", "Car", "Cloth",
                                                       "RampBox_0", "RampBox_1"]
    sensors = [("GhostBox", "touch"), ("SensorBox", "touch"), ("NearPost", "near")]
    paths = {}
    for key, engine, device in ENGINES:
        set_engine(scene, engine, device)
        path = os.path.join(folder, "p6_physics_%s.blend" % key)
        if key == ENGINES[0][0]:
            finish(scene, every, stack, sensors, rays, path)
        else:
            scene.update()
            p2.save(path)
        paths[key] = path
    expected["files"] = paths
    expected["track_every"] = every
    return expected


# ---------------------------------------------------------------------------
# p6_bench: boxes added by script (B6.1)

BENCH_LOGIC = '''\
# BGE-Remix P6: BGE_REMIX_P6_BOXES boxes in a grid, falling onto the floor
import math
import os
import bge
ob = bge.logic.getCurrentController().owner
if not ob.get("done", False):
    ob["done"] = True
    scene = bge.logic.getCurrentScene()
    n = int(os.environ.get("BGE_REMIX_P6_BOXES", "1000"))
    side = int(math.ceil(n ** (1.0 / 3.0)))
    k = 0
    for z in range(side):
        for y in range(side):
            for x in range(side):
                if k >= n:
                    break
                b = scene.addObject("BenchBox", ob, 0)
                b.worldPosition = ((x - side / 2.0) * 0.6, (y - side / 2.0) * 0.6, 1.0 + z * 0.6)
                k += 1
    print("BGE-Remix P6 bench: %d boxes" % k)
'''


def make_bench(folder):
    paths = {}
    scene = base("P6Bench")
    grey = material("BenchGround", (0.5, 0.5, 0.5))
    box(scene, "Floor", (60.0, 60.0, 0.2), (0, 0, -0.1), grey, physics='STATIC')
    for k, (x, y) in enumerate(((-30.5, 0), (30.5, 0), (0, -30.5), (0, 30.5))):
        box(scene, "BenchWall%d" % k, (1.0 if x else 60.0, 60.0 if x else 1.0, 4.0), (x, y, 2.0), grey, physics='STATIC')
    bb = box(scene, "BenchBox", 0.5, (0, 0, -20), material("BenchBoxMa", (0.8, 0.6, 0.2)))
    bb.layers[1] = True
    bb.layers[0] = False
    spawner = bpy.data.objects.new("Spawner", None)
    scene.objects.link(spawner)
    logic(scene, spawner, "p6_bench.py", BENCH_LOGIC)
    p2.camera(scene, (0.0, -30.0, 20.0), (0.0, 0.0, 0.0), lens=24.0)
    for key, engine, device in ENGINES:
        set_engine(scene, engine, device)
        path = os.path.join(folder, "p6_bench_%s.blend" % key)
        if key == ENGINES[0][0]:
            finish(scene, [], [], [], [], path)
        else:
            scene.update()
            p2.save(path)
        paths[key] = path
    return {"files": paths}


def main():
    folder = p2.out_dir()
    expected = {"failed": {}}
    for name, fn in (("physics", make_physics), ("bench", make_bench)):
        try:
            expected[name] = fn(folder)
        except Exception:
            text = traceback.format_exc()
            print("BGE-Remix: making the %s scene failed:\n%s" % (name, text))
            expected[name] = None
            expected["failed"][name] = text
    # T6.0: the engine list
    items = bpy.types.SceneGameData.bl_rna.properties["physics_engine"].enum_items.keys()
    expected["engine_items"] = list(items)
    with open(os.path.join(folder, "p6_expected.json"), "w") as f:
        json.dump(expected, f, indent=2)
    print("BGE-Remix: wrote", os.path.join(folder, "p6_expected.json"))


if __name__ == "__main__":
    main()
