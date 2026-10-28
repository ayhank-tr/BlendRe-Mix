# BGE-Remix P6: adapts the 2008 bathroom game demo (saved by Blender 2.47) to
# Blender 2.79 and makes the P6 test copies. Your file is only read; the
# copies go to <out_dir>. Runs inside our Blender (Python 3.5: no f-strings).
#
#   blender --background bathroom_final_dof_noise.blend --python adapt_bathroom.py -- <out_dir>
#
# What changes (the scene is adapted to 2.79, not the engine to the scene):
#   - the rail: an invisible static bar along the curtain rail is the other
#     end of the five rings' 6DOF joints (2.47 fixed joints without a target
#     to the world; 2.79 skips them, so the rings fell);
#   - the ceiling hook: an invisible static object holds the lamp cord's top
#     ball joint;
#   - the bath's stray ball joint without a target (a static object: it did
#     nothing in 2.47) is removed;
#   - MouseLook.py and texmove rewritten for the 2.79 API (bge.logic, owner,
#     sensors and actuators by name), with the same behaviour;
#   - RTX Remix on; a test driver (p6_driver.py) that does nothing unless
#     BGE_REMIX_P6_DRIVE is set (then it presses the keys by itself, adds the
#     duck, records where things are and takes pictures);
#   - three copies: Bullet, PhysX on the CPU, PhysX on the GPU.
# The four missing textures stay missing.

import json
import os
import sys

import bpy
from mathutils import Vector

RINGS = ["Empty.002", "Empty.003", "Empty.004", "Empty.005", "Empty.006"]
CORD = "Cube.008"
BULB = "Sphere.001"
BATH = "Cube.003"
CURTAIN = "Plane.002"      # the curtain on the active layer (Plane.001 is a coarser copy on layer 5)

MOUSELOOK = '''\
# MouseLook.py (tutorialsforblender3D.com, Blender 2.46), for the 2.79 API
# by BGE-Remix (P6): the same behaviour
import os

import bge

controller = bge.logic.getCurrentController()
player = controller.owner
mouse = controller.sensors["Mouse"]
lookLeftRight = controller.actuators["LookLeftRight"]
lookUpDown = controller.actuators["LookUpDown"]

# the P6 test driver steers the camera itself
if not os.environ.get("BGE_REMIX_P6_DRIVE"):
    width = bge.render.getWindowWidth()
    height = bge.render.getWindowHeight()

    # distance moved from the screen centre
    x = width / 2 - mouse.position[0]
    y = height / 2 - mouse.position[1]
    # the first time, no jerk
    if "mouseInit" not in player:
        x = 0
        y = 0
        player["mouseInit"] = 1

    sensitivity = 0.001
    leftRight = x * sensitivity
    upDown = y * sensitivity

    lookLeftRight.dRot = (0.0, 0.0, leftRight)
    lookLeftRight.useLocalDRot = False
    lookUpDown.dRot = (upDown, 0.0, 0.0)
    lookUpDown.useLocalDRot = True
    controller.activate(lookLeftRight)
    controller.activate(lookUpDown)

    # centre the mouse in the window
    bge.render.setMousePosition(int(width / 2), int(height / 2))
'''

TEXMOVE = '''\
# texmove (UV scroller), for the 2.79 API by BGE-Remix (P6): the same behaviour
import bge

own = bge.logic.getCurrentController().owner
mesh = own.meshes[0]
speed = own["speed"] / 2

for i in range(mesh.getVertexArrayLength(0)):
    vertex = mesh.getVertex(0, i)
    uv = vertex.UV
    uv[0] = uv[0] + speed      # "V" (Y) in the 2008 comment
    vertex.UV = uv
'''

DRIVER = '''\
# BGE-Remix P6 test driver: inactive unless BGE_REMIX_P6_DRIVE is set.
# Then, from tic START: pushes ring 1 (the A key) and ring 5 (Q), lifts the
# curtain (Space), adds the duck (left click), records where things are every
# tic and takes the pictures asked for; ends at START + 600.
import json
import os
import time

import bge

env = os.environ
if env.get("BGE_REMIX_P6_DRIVE"):
    g = bge.logic.globalDict
    scene = bge.logic.getCurrentScene()
    objs = scene.objects
    if "p6d" not in g:
        g["p6d"] = {"tic": 0, "rec": [], "times": [], "physics_ms": [], "shots": {}}
        shots = env.get("BGE_REMIX_P6_SHOTS", "")
        for item in shots.split(";"):
            if ":" in item:
                tic, path = item.split(":", 1)
                g["p6d"]["shots"][int(tic)] = path
    st = g["p6d"]
    st["tic"] += 1
    tic = st["tic"]
    st["times"].append(time.perf_counter())
    start = int(env.get("BGE_REMIX_P6_START", "300"))
    t = tic - start

    # the keys' Motion actuators: S on ring 1, Q on ring 5
    if 60 <= t < 120:
        objs["Empty.002"].applyForce((-6.0, 0.0, 0.0), True)
    if 150 <= t < 210:
        objs["Empty.006"].applyForce((6.0, 0.0, 0.0), True)
    if 240 <= t < 300:
        objs["CURTAIN"].applyForce((0.0, 0.0, 20.0), True)
    if t == 330:
        # left click: the camera's child "Empty" adds the duck
        scene.addObject("Duck", "Empty", 0)

    def p(ob):
        v = ob.worldPosition
        return [round(v.x, 4), round(v.y, 4), round(v.z, 4)]

    rec = {"tic": tic}
    for name in ("Empty.002", "Empty.003", "Empty.004", "Empty.005", "Empty.006", "BULB", "Lamp",
                 "Cube.001", "Cube.009"):
        if name in objs:
            rec[name] = p(objs[name])
    curtain = objs["CURTAIN"]
    if curtain.meshes:
        m = curtain.meshes[0]
        zs, tops = [], []
        for i in range(m.getVertexArrayLength(0)):
            xyz = m.getVertex(0, i).XYZ
            zs.append(xyz[2])
            tops.append((xyz[2], xyz[0], xyz[1]))
        rec["curtain_z"] = [round(min(zs), 4), round(max(zs), 4), round(sum(zs) / len(zs), 4)]
        if t in (0, 200, 280, 600):
            tops.sort(reverse=True)
            rec["curtain_top"] = [[round(x, 3), round(y, 3), round(z, 3)] for z, x, y in tops[:15]]
    ducks = [o for o in objs if o.name == "Duck"]
    if ducks:
        d = ducks[-1]
        xs = []
        if d.meshes:
            m = d.meshes[0]
            for i in range(m.getVertexArrayLength(0)):
                xs.append(m.getVertex(0, i).XYZ)
        if xs:
            rec["duck"] = [round(min(v[k] for v in xs), 4) for k in range(3)] + \\
                          [round(max(v[k] for v in xs), 4) for k in range(3)]
        rec["duck_pos"] = p(d)
    rec["drops"] = len([o for o in objs if o.name == "drop"])
    st["rec"].append(rec)
    info = bge.logic.getProfileInfo()
    if "Physics:" in info:
        st["physics_ms"].append(info["Physics:"][0])

    if tic in st["shots"]:
        bge.render.makeScreenshot(st["shots"][tic])

    if t >= int(env.get("BGE_REMIX_P6_LENGTH", "600")):
        times = st["times"][start:]
        deltas = sorted(b - a for a, b in zip(times, times[1:]))
        out = {"frames": tic, "rec": st["rec"],
               "frame_ms_median": 1000.0 * deltas[len(deltas) // 2] if deltas else None,
               "physics_ms_mean": (sum(st["physics_ms"][start:]) / len(st["physics_ms"][start:])
                                   if st["physics_ms"][start:] else None),
               "window": [bge.render.getWindowWidth(), bge.render.getWindowHeight()]}
        path = env.get("BGE_REMIX_P2_OUT")
        if path:
            with open(path, "w") as f:
                json.dump(out, f)
        bge.logic.endGame()
'''


def static_anchor(scene, name, location, size):
    """an invisible static ghost box (2.79 makes joints only to objects with
    physics, and empties get none)"""
    me = bpy.data.meshes.new(name + "Mesh")
    sx, sy, sz = (s / 2.0 for s in size)
    verts = [(x, y, z) for x in (-sx, sx) for y in (-sy, sy) for z in (-sz, sz)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    me.from_pydata(verts, [], faces)
    me.update()
    ob = bpy.data.objects.new(name, me)
    ob.location = location
    scene.objects.link(ob)
    ob.layers = scene.layers[:]
    ob.game.physics_type = 'STATIC'
    ob.game.use_ghost = True
    ob.hide_render = True   # the game's Invisible
    return ob


def text(name, body):
    t = bpy.data.texts.get(name) or bpy.data.texts.new(name)
    t.clear()
    t.write(body)
    return t


def main():
    argv = sys.argv
    out = os.path.abspath(argv[argv.index("--") + 1]) if "--" in argv else os.getcwd()
    os.makedirs(out, exist_ok=True)
    scene = bpy.data.scenes["Scene"]
    D = bpy.data
    report = {"source": bpy.data.filepath, "changes": []}

    # the rail: along the five rings
    rings = [D.objects[n] for n in RINGS]
    pos = [r.matrix_world.translation for r in rings]
    lo = min(p.x for p in pos)
    hi = max(p.x for p in pos)
    centre = Vector(((lo + hi) / 2.0, sum(p.y for p in pos) / len(pos), sum(p.z for p in pos) / len(pos)))
    rail = static_anchor(scene, "BGE_Remix_Rail", centre, (hi - lo + 0.4, 0.04, 0.04))
    for r in rings:
        for c in r.constraints:
            if c.type == 'RIGID_BODY_JOINT' and c.target is None:
                c.target = rail
    report["changes"].append("rail %s for the rings' joints" % [round(v, 3) for v in centre])

    # the ceiling hook: the cord's top ball joint
    cord = D.objects[CORD]
    for c in cord.constraints:
        if c.type == 'RIGID_BODY_JOINT' and c.target is None:
            hook_at = cord.matrix_world * Vector((c.pivot_x, c.pivot_y, c.pivot_z))
            hook = static_anchor(scene, "BGE_Remix_Hook", hook_at, (0.05, 0.05, 0.05))
            c.target = hook
            report["changes"].append("hook %s for the cord's ball joint" % [round(v, 3) for v in hook_at])

    # the bath's stray joint
    bath = D.objects[BATH]
    for c in list(bath.constraints):
        if c.type == 'RIGID_BODY_JOINT' and c.target is None:
            bath.constraints.remove(c)
            report["changes"].append("removed the bath's ball joint without a target")

    # scripts for the 2.79 API
    text("MouseLook.py", MOUSELOOK)
    text("texmove", TEXMOVE)
    report["changes"].append("MouseLook.py and texmove for 2.79")

    # the test driver (inactive unless BGE_REMIX_P6_DRIVE)
    drv = bpy.data.objects.new("BGE_Remix_P6_Driver", None)
    scene.objects.link(drv)
    drv.layers = scene.layers[:]
    scene.objects.active = drv
    bpy.ops.logic.sensor_add(type='ALWAYS', object=drv.name)
    bpy.ops.logic.controller_add(type='PYTHON', object=drv.name)
    s = drv.game.sensors[-1]
    c = drv.game.controllers[-1]
    s.use_pulse_true_level = True
    s.tick_skip = 0
    c.mode = 'SCRIPT'
    c.text = text("p6_driver.py", DRIVER.replace("CURTAIN", CURTAIN).replace("BULB", BULB))
    s.link(c)

    # RTX Remix on
    scene["bge_remix"] = 1

    # the file packs its textures automatically; the four missing ones cannot
    # be packed and stop the save, so the copies keep the packed ones as they are
    if bpy.data.use_autopack:
        bpy.ops.file.autopack_toggle()

    copies = {}
    for key, engine, device in (("bullet", 'BULLET', None), ("physx_cpu", 'PHYSX', 'CPU'),
                                ("physx_gpu", 'PHYSX', 'GPU')):
        gs = scene.game_settings
        try:
            gs.physics_engine = engine
            if device:
                gs.physx_device = device
        except (TypeError, AttributeError):
            # a build without PhysX in the list: the scene ID property it keeps it in
            gs.physics_engine = 'BULLET'
            if engine == 'PHYSX':
                scene["_bge_physx"] = 2 if device == 'GPU' else 1
            elif "_bge_physx" in scene:
                del scene["_bge_physx"]
        path = os.path.join(out, "bathroom_p6_%s.blend" % key)
        bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, copy=True)
        copies[key] = path
        print("BGE-Remix: wrote", path)
    report["copies"] = copies
    with open(os.path.join(out, "bathroom_p6.json"), "w") as f:
        json.dump(report, f, indent=2)
    print("BGE-Remix: bathroom adapted:", "; ".join(report["changes"]))


main()
