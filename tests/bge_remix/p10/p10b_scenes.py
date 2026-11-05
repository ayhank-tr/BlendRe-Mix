"""BGE-Remix p10b: the scenes for the Remix View's Edit and Run modes (P10.6-P10.9).

make(folder, E) runs inside our Blender (from make_scenes_p10.py), saves its
.blend files into folder and writes what the tests expect into E["p10b"].

p10b_edit.blend  Edit mode and the settings hub. "Logic" holds an Always
                 sensor, an And controller and a Motion actuator, none of
                 them linked (each warns: not linked), a Track To actuator
                 with no object (it warns: no object chosen) and a Message
                 actuator sending "ping"; "Listener" has a Message sensor with
                 no subject; "Target", "Child" and "Parent" are plain cubes;
                 "Crate" is a dynamic box on the "Ground". The Default
                 screen's 3D View is a Remix View in Edit mode, through the
                 camera.
p10b_run.blend   Run mode. "Mover" moves 0.05 m along X every logic tic
                 (Always, And, Motion), counts its tics in "count" and copies
                 "speed" from "Target" (a Property actuator in Copy mode:
                 Target is what its logic needs); "Bystander" moves along Y
                 on its own; "Crate" is a dynamic box resting on the
                 "Ground" (a contact every frame); a camera and a sun. The
                 Remix View is in Run mode, through the camera. Classic
                 pipeline, RTX Remix off.
p10b_run_remix.blend  the same in the RTX Remix pipeline with RTX Remix on
                 (the RTX run).
"""

import math
import os

import bpy

import make_scenes_p2 as p2

STEP = 0.05          # Mover's and Bystander's metres per logic tic
MOVER_START = (-3.0, 0.0, 0.5)
BYSTANDER_START = (3.0, -3.0, 0.5)
TARGET_SPEED = 2.5


def sensor(ob, kind, name):
    bpy.context.scene.objects.active = ob
    bpy.ops.logic.sensor_add(type=kind, object=ob.name)
    s = ob.game.sensors[-1]
    s.name = name
    return s


def controller(ob, kind, name):
    bpy.context.scene.objects.active = ob
    bpy.ops.logic.controller_add(type=kind, object=ob.name)
    c = ob.game.controllers[-1]
    c.name = name
    return c


def actuator(ob, kind, name):
    bpy.context.scene.objects.active = ob
    bpy.ops.logic.actuator_add(type=kind, object=ob.name)
    a = ob.game.actuators[-1]
    a.name = name
    return a


def prop(ob, name, kind, value):
    bpy.context.scene.objects.active = ob
    bpy.ops.object.game_property_new(type=kind, name=name)
    ob.game.properties[name].value = value


def remix_view(state):
    """the Default screen's largest 3D View made a Remix View, through the camera"""
    areas = []
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type in ('VIEW_3D', 'GAME_VIEW'):
                areas.append((screen.name != "Default", -(area.width * area.height), screen.name, area))
    areas.sort(key=lambda t: t[:3])
    if not areas:
        return None
    area = areas[0][3]
    space = area.spaces.active
    space.is_game_view = True
    space.game_view_mode = state
    space.region_3d.view_perspective = 'CAMERA'
    return areas[0][2]


def base(name):
    scene = p2.empty_scene()
    scene.name = name
    gs = scene.game_settings
    gs.use_frame_rate = True   # 60 frames a second: a tic a frame
    gs.fps = 60
    gs.vsync = 'OFF'
    w = bpy.data.worlds.new("World")
    w.horizon_color = (0.35, 0.4, 0.5)
    w.ambient_color = (0.1, 0.1, 0.1)
    scene.world = w
    p2.camera(scene, (0.0, -14.0, 9.0), (0.0, 0.0, 0.0), lens=28.0)
    p2.lamp(scene, "Sun", 'SUN', (2.0, -4.0, 8.0), (math.radians(40.0), 0.0, math.radians(20.0)), energy=1.0)
    ground = p2.plane(scene, "Ground", 20.0, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0),
                      p2.material("Ground", (0.35, 0.35, 0.35)))
    ground.game.physics_type = 'STATIC'
    return scene


def cube(scene, name, location, colour, physics='NO_COLLISION'):
    ob = p2.cube(scene, name, 1.0, location, p2.material(name, colour))
    ob.game.physics_type = physics
    if physics in ('DYNAMIC', 'RIGID_BODY'):
        ob.game.use_collision_bounds = True
        ob.game.collision_bounds_type = 'BOX'
        ob.game.mass = 1.0
    return ob


def make_edit(folder, E):
    scene = base("P10bEdit")
    logic = cube(scene, "Logic", (0.0, 0.0, 0.5), (0.8, 0.5, 0.2))
    sensor(logic, 'ALWAYS', "Always")
    controller(logic, 'LOGIC_AND', "And")
    motion = actuator(logic, 'MOTION', "Motion")
    motion.offset_location = (STEP, 0.0, 0.0)
    track = actuator(logic, 'EDIT_OBJECT', "Track")
    track.mode = 'TRACKTO'
    send = actuator(logic, 'MESSAGE', "Send")
    send.subject = "ping"
    target = cube(scene, "Target", (4.0, 0.0, 0.5), (0.2, 0.6, 0.9))
    listener = cube(scene, "Listener", (-4.0, 0.0, 0.5), (0.6, 0.2, 0.8))
    sensor(listener, 'MESSAGE', "Hear")
    child = cube(scene, "Child", (0.0, 3.0, 0.5), (0.9, 0.9, 0.2))
    parent = cube(scene, "Parent", (0.0, 5.5, 0.5), (0.2, 0.9, 0.4))
    cube(scene, "Crate", (3.0, -3.0, 0.5), (0.6, 0.45, 0.3), physics='DYNAMIC')
    prop(target, "speed", 'FLOAT', TARGET_SPEED)
    screen = remix_view('EDIT')
    scene.objects.active = logic
    scene.update()
    path = os.path.join(folder, "p10b_edit.blend")
    p2.save(path)
    E["p10b"]["edit"] = {
        "file": "p10b_edit", "screen": screen, "logic": logic.name, "target": target.name,
        "listener": listener.name, "child": child.name, "parent": parent.name,
        "child_location": list(child.location), "subject": "ping", "step": STEP,
    }


def make_run(folder, E, remix=False):
    scene = base("P10bRun")
    mover = cube(scene, "Mover", MOVER_START, (0.9, 0.4, 0.1))
    prop(mover, "count", 'INT', 0)
    prop(mover, "speed", 'FLOAT', 0.0)
    s = sensor(mover, 'ALWAYS', "Always")
    s.use_pulse_true_level = True
    s.tick_skip = 0
    c = controller(mover, 'LOGIC_AND', "And")
    m = actuator(mover, 'MOTION', "Motion")
    m.offset_location = (STEP, 0.0, 0.0)
    m.use_local_location = False
    count = actuator(mover, 'PROPERTY', "Count")
    count.mode = 'ADD'
    count.property = "count"
    count.value = "1"
    target = cube(scene, "Target", (3.0, 3.0, 0.5), (0.2, 0.6, 0.9))
    prop(target, "speed", 'FLOAT', TARGET_SPEED)
    copy = actuator(mover, 'PROPERTY', "CopySpeed")
    copy.mode = 'COPY'
    copy.object = target
    copy.object_property = "speed"
    copy.property = "speed"
    s.link(c)
    for a in (m, count, copy):
        a.link(c)
    by = cube(scene, "Bystander", BYSTANDER_START, (0.5, 0.8, 0.3))
    prop(by, "steps", 'INT', 0)
    bs = sensor(by, 'ALWAYS', "Always")
    bs.use_pulse_true_level = True
    bs.tick_skip = 0
    bc = controller(by, 'LOGIC_AND', "And")
    bm = actuator(by, 'MOTION', "Motion")
    bm.offset_location = (0.0, STEP, 0.0)
    bm.use_local_location = False
    bsteps = actuator(by, 'PROPERTY', "Steps")
    bsteps.mode = 'ADD'
    bsteps.property = "steps"
    bsteps.value = "1"
    bs.link(bc)
    bm.link(bc)
    bsteps.link(bc)
    cube(scene, "Crate", (-1.0, -4.0, 0.5), (0.6, 0.45, 0.3), physics='DYNAMIC')
    screen = remix_view('RUN')
    scene.objects.active = mover
    if remix:
        scene.game_settings.render_pipeline = 'RTX_REMIX'
        scene.bge_remix = 'RTX_REMIX'
    scene.update()
    name = "p10b_run_remix" if remix else "p10b_run"
    path = os.path.join(folder, name + ".blend")
    p2.save(path)
    if remix:
        # p2.save keeps the Classic pipeline: this one is saved again in RTX Remix's
        for sc in bpy.data.scenes:
            sc["bge_remix_pipeline"] = 1
            sc.game_settings.render_pipeline = 'RTX_REMIX'
        bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, compress=False)
    E["p10b"]["run_remix" if remix else "run"] = {
        "file": name, "screen": screen, "mover": "Mover", "target": "Target", "bystander": "Bystander",
        "crate": "Crate", "ground": "Ground", "camera": "Camera", "lamp": "Sun",
        "mover_start": list(MOVER_START), "bystander_start": list(BYSTANDER_START), "step": STEP,
        "target_speed": TARGET_SPEED,
        # a mock-up of Mover plays Mover, its logic's target, the camera and the lamp
        "mockup_of": ["Mover"], "mockup_plays": sorted(["Mover", "Target", "Camera", "Sun"]),
        "all": sorted(["Mover", "Target", "Bystander", "Crate", "Ground", "Camera", "Sun"]),
    }


def make(folder, E):
    E["p10b"] = {}
    make_edit(folder, E)
    make_run(folder, E)
    make_run(folder, E, remix=True)
    # P9.7's Remix View scene, for its tests again (the earlier phase unchanged)
    try:
        import traceback
        import p97_scenes
        E9 = {}
        p97_scenes.make(folder, E9)
        E["p10b"]["p97"] = E9
    except Exception:
        E["p10b"]["p97_failed"] = traceback.format_exc()
