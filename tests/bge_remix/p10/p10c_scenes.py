"""BGE-Remix p10c: the scenes for the logic bricks (P10.10-P10.13).

make(folder, E) runs inside our Blender (from make_scenes_p10.py, Python
3.5), saves its .blend files into folder and writes what the tests expect
into E["p10c"]. Every scene carries the P2 probe (it ends the game after
BGE_REMIX_P2_FRAMES logic tics). The scenes play one logic tic a frame
(the frame rate not kept: Blender's fixed time), so a tic number is a frame
number in the brick log.

p10c_states.blend       "Brain" (a dynamic cube on a floor) with three
                        states: state 1 a Delay of 30 tics, then a State
                        actuator into state 2 and a Message "go"; state 2 an
                        Always into a servo Motion (1 m/s along X) and a Delay
                        of 60 tics into state 3; state 3 an Always into a
                        Property actuator. "Listener" hears "go". "Settings"
                        has an RTX Remix actuator in this Classic scene and a
                        Message "lost" that nobody hears.
p10c_states_view.blend  the same, saved with the Default screen's 3D View
                        made a Remix View (Check mode)
p10c_missing.blend      "Owner": a Track To and an Add Object with no
                        object, a Game actuator starting a file that does
                        not exist, a Set Parent with no parent, an Actuator
                        sensor watching an actuator that does not exist, a
                        Property sensor watching a property it does not
                        have, a Python controller with no script; and a
                        healthy Keyboard -> And -> Property chain
p10c_timer.blend        "Clock": a Timer of 0.5 s (once) and one of 0.25 s
                        (repeating), each into a Property actuator; a script
                        that reads their Python attributes once
p10c_remix.blend        RTX Remix on: "Panel" with an Always into an RTX
                        Remix actuator (rtx.emissiveIntensity = 2.5, picked
                        from the list as the editor's button does) and a
                        Delay of 40 tics into another (rtx.pathMaxBounces =
                        7)
p10c_remix_classic.blend  the same in the Classic pipeline
p10c_subject_before.blend "Sender" sends a message with no subject to
                        "Hearer", whose Message sensor waits for "ping"
                        ("Other" listens for "pong")
p10c_subject.blend      the same after the subject was picked from the
                        list (logic.bge_remix_subject): "ping"
p10c_new_bricks.blend   for stock 2.79b: a Timer sensor (once, 0.5 s), a
                        repeating one, an RTX Remix actuator, each wired to
                        an And controller and a Property actuator, in the
                        Classic pipeline
"""

import os

import bpy

import make_scenes_p2 as p2s

STATES_TICS = {"delay1": 30, "delay2": 60}
TIMER = {"once": 0.5, "repeat": 0.25}
REMIX = {"start": ("rtx.emissiveIntensity", "2.5"), "later": ("rtx.pathMaxBounces", "7"), "later_tics": 40}

TRACE = '''\
# BGE-Remix p10c: the game's own state every tic (the brick log must not change it)
import json
import os
import bge
path = os.environ.get("BGE_REMIX_P10C_TRACE")
if path:
    g = bge.logic.globalDict.setdefault("p10c_trace", [])
    sc = bge.logic.getCurrentScene()
    b = sc.objects["Brain"]
    lis = sc.objects["Listener"]
    g.append([round(v, 5) for v in b.worldPosition] + [b.state, b["done"], lis["heard"]])
    if len(g) in (60, 120):
        with open(path, "w") as f:
            json.dump(g, f)
'''

PY_CHECK = '''\
# BGE-Remix p10c: the new bricks' Python types (once)
import json
import os
import bge
path = os.environ.get("BGE_REMIX_P10C_PY")
if path:
    r = {}
    try:
        own = bge.logic.getCurrentController().owner
        for s in own.sensors:
            if s.name.startswith("T_"):
                r[s.name] = {"type": type(s).__name__, "seconds": round(s.seconds, 4), "repeat": bool(s.repeat)}
        for a in own.actuators:
            if a.name.startswith("R_"):
                r[a.name] = {"type": type(a).__name__, "setting": a.setting, "value": a.value}
    except Exception as ex:
        r["error"] = repr(ex)
    with open(path, "w") as f:
        json.dump(r, f)
'''


# ---------------------------------------------------------------------------
# helpers

def new_scene():
    scene = p2s.empty_scene()
    scene.game_settings.fps = 60
    floor_ma = p2s.material("P10C_floor", (0.5, 0.5, 0.5))
    floor = p2s.plane(scene, "Floor", 40.0, (0, 0, 0), (0, 0, 0), floor_ma)
    floor.game.physics_type = 'STATIC'
    p2s.lamp(scene, "Sun", 'SUN', (0, -6, 8), (0.8, 0, 0), energy=1.0)
    p2s.camera(scene, (0.0, -14.0, 6.0), (0.0, 0.0, 1.0))
    return scene


def thing(scene, name, location, physics='STATIC', colour=(0.7, 0.4, 0.2)):
    ma = p2s.material("P10C_" + name, colour)
    ob = p2s.cube(scene, name, 1.0, location, ma)
    ob.game.physics_type = physics
    if physics == 'DYNAMIC':
        ob.game.mass = 1.0
    return ob


def holder(scene, name, location):
    ob = bpy.data.objects.new(name, None)
    ob.location = location
    scene.objects.link(ob)
    return ob


def sensor(ob, kind, name, **values):
    bpy.ops.logic.sensor_add(type=kind, object=ob.name)
    s = ob.game.sensors[-1]
    s.name = name
    for k, v in values.items():
        setattr(s, k, v)
    return s


def controller(ob, kind, name, state=1, **values):
    bpy.ops.logic.controller_add(type=kind, object=ob.name)
    c = ob.game.controllers[-1]
    c.name = name
    c.states = state
    for k, v in values.items():
        setattr(c, k, v)
    return c


def actuator(ob, kind, name, **values):
    bpy.ops.logic.actuator_add(type=kind, object=ob.name)
    a = ob.game.actuators[-1]
    a.name = name
    for k, v in values.items():
        setattr(a, k, v)
    return a


def wire(s_list, c, a_list):
    for s in s_list:
        s.link(c)
    for a in a_list:
        c.link(actuator=a)


def game_prop(scene, ob, name, kind='INT', value=0):
    scene.objects.active = ob
    bpy.ops.object.game_property_new(type=kind, name=name)
    ob.game.properties[name].value = value


def start_states(ob, states):
    for i in range(30):
        ob.game.states_initial[i] = (i + 1) in states


def state_mask(states):
    return [(i + 1) in states for i in range(30)]


def save(scene, folder, name, pipeline=0, remix=0):
    for ma in bpy.data.materials:
        ma["bge_physical"] = 0
    for la in bpy.data.lamps:
        la["bge_physical"] = 0
    for sc in bpy.data.scenes:
        sc["bge_remix_pipeline"] = pipeline   # 0 Classic, 1 RTX Remix
        sc["bge_remix"] = remix
    scene.update()
    path = os.path.join(folder, name + ".blend")
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, compress=False, copy=True)
    return name


def remix_view(scene):
    """the Default screen's largest 3D View made a Remix View (Check mode)"""
    areas = []
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type in ('VIEW_3D', 'GAME_VIEW'):
                areas.append((screen.name != "Default", -(area.width * area.height), area))
    areas.sort(key=lambda t: (t[0], t[1]))
    if not areas:
        return False
    space = areas[0][2].spaces.active
    space.is_game_view = True
    space.game_view_mode = 'CHECK'
    space.region_3d.view_perspective = 'CAMERA'
    return True


def brick_names(ob):
    return {"sensors": [s.name for s in ob.game.sensors], "controllers": [c.name for c in ob.game.controllers],
            "actuators": [a.name for a in ob.game.actuators]}


# ---------------------------------------------------------------------------
# the scenes

def make_states(folder, E):
    scene = new_scene()
    brain = thing(scene, "Brain", (-2.0, 0.0, 0.5), 'DYNAMIC')
    game_prop(scene, brain, "done")
    # state 1: a delay, then into state 2 and a message
    s1 = sensor(brain, 'DELAY', "S1_delay", delay=STATES_TICS["delay1"], duration=0)
    c1 = controller(brain, 'LOGIC_AND', "C1_and", 1)
    a1 = actuator(brain, 'STATE', "A1_to2", operation='SET', states=state_mask([2]))
    a1m = actuator(brain, 'MESSAGE', "A1_send", subject="go")
    wire([s1], c1, [a1, a1m])
    # state 2: a servo motion; a delay into state 3
    s2 = sensor(brain, 'ALWAYS', "S2_always")
    c2 = controller(brain, 'LOGIC_AND', "C2_and", 2)
    a2 = actuator(brain, 'MOTION', "A2_servo", mode='OBJECT_SERVO')
    a2.linear_velocity = (1.0, 0.0, 0.0)
    wire([s2], c2, [a2])
    s2d = sensor(brain, 'DELAY', "S2_delay", delay=STATES_TICS["delay2"], duration=0)
    c2n = controller(brain, 'LOGIC_AND', "C2_next", 2)
    a2s = actuator(brain, 'STATE', "A2_to3", operation='SET', states=state_mask([3]))
    wire([s2d], c2n, [a2s])
    # state 3: a property
    s3 = sensor(brain, 'ALWAYS', "S3_always")
    c3 = controller(brain, 'LOGIC_AND', "C3_and", 3)
    a3 = actuator(brain, 'PROPERTY', "A3_prop", mode='ASSIGN', property="done", value="1")
    wire([s3], c3, [a3])
    start_states(brain, [1])

    listener = thing(scene, "Listener", (2.0, 0.0, 0.5), 'STATIC', (0.2, 0.4, 0.7))
    game_prop(scene, listener, "heard")
    ls = sensor(listener, 'MESSAGE', "L_hear", subject="go")
    lc = controller(listener, 'LOGIC_AND', "L_and", 1)
    la = actuator(listener, 'PROPERTY', "L_got", mode='ADD', property="heard", value="1")
    wire([ls], lc, [la])
    start_states(listener, [1])

    settings = holder(scene, "Settings", (0.0, 3.0, 1.0))
    ss = sensor(settings, 'ALWAYS', "X_always")
    sc_ = controller(settings, 'LOGIC_AND', "X_and", 1)
    sr = actuator(settings, 'REMIX', "X_remix", setting="rtx.emissiveIntensity", value="2")
    sl = actuator(settings, 'MESSAGE', "X_lost", subject="lost")
    wire([ss], sc_, [sr, sl])
    start_states(settings, [1])

    tracer = holder(scene, "Tracer", (0.0, -3.0, 1.0))
    text = bpy.data.texts.new("p10c_trace.py")
    text.write(TRACE)
    ts = sensor(tracer, 'ALWAYS', "Z_always", use_pulse_true_level=True, tick_skip=0)
    tc = controller(tracer, 'PYTHON', "Z_trace", 1, mode='SCRIPT', text=text)
    ts.link(tc)
    start_states(tracer, [1])
    p2s.add_probe(scene)
    save(scene, folder, "p10c_states")
    remix_view(scene)
    save(scene, folder, "p10c_states_view")
    E["p10c"]["states"] = {"file": "p10c_states", "view_file": "p10c_states_view", "tics": STATES_TICS,
                           "bricks": {"Brain": brick_names(brain), "Listener": brick_names(listener),
                                      "Settings": brick_names(settings)},
                           # each brick's state group (the layout's columns)
                           "groups": {"Brain": {"S1_delay": 1, "C1_and": 1, "A1_to2": 1, "A1_send": 1,
                                                "S2_always": 2, "C2_and": 2, "A2_servo": 2, "S2_delay": 2,
                                                "C2_next": 2, "A2_to3": 2,
                                                "S3_always": 3, "C3_and": 3, "A3_prop": 3}},
                           "arrows": {"Brain": [[1, 2], [2, 3]]},
                           "members": {"Brain": {"1": ["C1_and"], "2": ["C2_and", "C2_next"], "3": ["C3_and"]}},
                           "start": {"Brain": [1], "Listener": [1], "Settings": [1]},
                           "new_start": {"Brain": [1, 3]}}


def make_missing(folder, E):
    scene = new_scene()
    owner = holder(scene, "Owner", (0.0, 0.0, 1.0))
    game_prop(scene, owner, "x")
    key = sensor(owner, 'KEYBOARD', "M_key", key='NINE')
    acts = sensor(owner, 'ACTUATOR', "M_actsens", actuator="NoSuchActuator")
    prop = sensor(owner, 'PROPERTY', "M_propsens", evaluation_type='PROPEQUAL', property="no_such_prop", value="1")
    c = controller(owner, 'LOGIC_AND', "M_and", 1)
    track = actuator(owner, 'EDIT_OBJECT', "M_track", mode='TRACKTO')
    add = actuator(owner, 'EDIT_OBJECT', "M_add", mode='ADDOBJECT')
    gamea = actuator(owner, 'GAME', "M_game", mode='START', filename="//p10c_no_such_game.blend")
    parent = actuator(owner, 'PARENT', "M_parent", mode='SETPARENT')
    ok = actuator(owner, 'PROPERTY', "M_ok_prop", mode='ASSIGN', property="x", value="1")
    wire([key, acts, prop], c, [track, add, gamea, parent, ok])
    py = controller(owner, 'PYTHON', "M_script", 1, mode='SCRIPT')
    key.link(py)
    start_states(owner, [1])
    p2s.add_probe(scene)
    save(scene, folder, "p10c_missing")
    E["p10c"]["missing"] = {"file": "p10c_missing", "object": "Owner",
                            "red": ["M_track", "M_add", "M_game", "M_parent", "M_actsens", "M_propsens", "M_script"],
                            "not_red": ["M_key", "M_and", "M_ok_prop"],
                            "why": {"M_track": "Track To with no object", "M_add": "Add Object with no object",
                                    "M_game": "Start Game From File: the file does not exist",
                                    "M_parent": "Set Parent with no parent",
                                    "M_actsens": "watches an actuator that does not exist",
                                    "M_propsens": "watches a property its object does not have",
                                    "M_script": "a Python controller with no script"}}


def timer_bricks(clock):
    t1 = sensor(clock, 'TIMER', "T_once", seconds=TIMER["once"], use_repeat=False)
    c1 = controller(clock, 'LOGIC_AND', "T_once_and", 1)
    a1 = actuator(clock, 'PROPERTY', "T_once_prop", mode='ADD', property="once", value="1")
    wire([t1], c1, [a1])
    t2 = sensor(clock, 'TIMER', "T_rep", seconds=TIMER["repeat"], use_repeat=True)
    c2 = controller(clock, 'LOGIC_AND', "T_rep_and", 1)
    a2 = actuator(clock, 'PROPERTY', "T_rep_prop", mode='ADD', property="rep", value="1")
    wire([t2], c2, [a2])


def make_timer(folder, E):
    scene = new_scene()
    clock = holder(scene, "Clock", (0.0, 0.0, 1.0))
    game_prop(scene, clock, "once")
    game_prop(scene, clock, "rep")
    timer_bricks(clock)
    text = bpy.data.texts.new("p10c_check.py")
    text.write(PY_CHECK)
    s = sensor(clock, 'ALWAYS', "Y_once")
    c = controller(clock, 'PYTHON', "Y_check", 1, mode='SCRIPT', text=text)
    s.link(c)
    start_states(clock, [1])
    p2s.add_probe(scene)
    save(scene, folder, "p10c_timer")
    E["p10c"]["timer"] = {"file": "p10c_timer", "object": "Clock", "seconds": TIMER,
                          "ticrate": scene.game_settings.fps}


def make_remix(folder, E):
    from bl_operators import bge_remix_logic as logic_ops
    scene = new_scene()
    panel = holder(scene, "Panel", (0.0, 0.0, 1.0))
    s1 = sensor(panel, 'ALWAYS', "R_always")
    c1 = controller(panel, 'LOGIC_AND', "R_always_and", 1)
    a1 = actuator(panel, 'REMIX', "R_start")
    wire([s1], c1, [a1])
    # the setting picked from the list, as the editor's button does
    picked = bpy.ops.logic.bge_remix_setting(object=panel.name, brick=a1.name, kind='ACTUATOR',
                                             setting=REMIX["start"][0])
    prefilled = a1.value
    a1.value = REMIX["start"][1]
    s2 = sensor(panel, 'DELAY', "R_delay", delay=REMIX["later_tics"], duration=0)
    c2 = controller(panel, 'LOGIC_AND', "R_delay_and", 1)
    a2 = actuator(panel, 'REMIX', "R_later", setting=REMIX["later"][0], value=REMIX["later"][1])
    wire([s2], c2, [a2])
    text = bpy.data.texts.new("p10c_check.py")
    text.write(PY_CHECK)
    s = sensor(panel, 'ALWAYS', "Y_once")
    c = controller(panel, 'PYTHON', "Y_check", 1, mode='SCRIPT', text=text)
    s.link(c)
    start_states(panel, [1])
    p2s.add_probe(scene)
    save(scene, folder, "p10c_remix", pipeline=1, remix=1)
    save(scene, folder, "p10c_remix_classic", pipeline=0, remix=0)
    E["p10c"]["remix"] = {"file": "p10c_remix", "classic_file": "p10c_remix_classic", "object": "Panel",
                          "settings": {"R_start": list(REMIX["start"]), "R_later": list(REMIX["later"])},
                          "later_tics": REMIX["later_tics"], "picked": list(picked), "prefilled": prefilled,
                          "listed": [k for k, kind, d in logic_ops.remix_settings()]}


def make_subject(folder, E):
    from bl_operators import bge_remix_logic as logic_ops
    scene = new_scene()
    sender = holder(scene, "Sender", (-2.0, 0.0, 1.0))
    s = sensor(sender, 'ALWAYS', "S_always")
    c = controller(sender, 'LOGIC_AND', "S_and", 1)
    a = actuator(sender, 'MESSAGE', "S_send", subject="")
    wire([s], c, [a])
    hearer = holder(scene, "Hearer", (2.0, 0.0, 1.0))
    game_prop(scene, hearer, "heard")
    hs = sensor(hearer, 'MESSAGE', "H_hear", subject="ping")
    hc = controller(hearer, 'LOGIC_AND', "H_and", 1)
    ha = actuator(hearer, 'PROPERTY', "H_got", mode='ADD', property="heard", value="1")
    wire([hs], hc, [ha])
    other = holder(scene, "Other", (0.0, 2.0, 1.0))
    os_ = sensor(other, 'MESSAGE', "O_hear", subject="pong")
    oc = controller(other, 'LOGIC_AND', "O_and", 1)
    oa = actuator(other, 'PROPERTY', "O_got", mode='ADD', property="heard", value="1")
    game_prop(scene, other, "heard")
    wire([os_], oc, [oa])
    for ob in (sender, hearer, other):
        start_states(ob, [1])
    p2s.add_probe(scene)
    save(scene, folder, "p10c_subject_before")
    listed = logic_ops.game_subjects()
    picked = bpy.ops.logic.bge_remix_subject(object=sender.name, brick=a.name, kind='ACTUATOR', subject="ping")
    save(scene, folder, "p10c_subject")
    E["p10c"]["subject"] = {"before_file": "p10c_subject_before", "file": "p10c_subject", "listed": listed,
                            "picked": list(picked), "subject_after": a.subject,
                            "listener": ["Hearer", "H_hear"], "other": ["Other", "O_hear"]}


def make_new_bricks(folder, E):
    scene = new_scene()
    clock = holder(scene, "Clock", (0.0, 0.0, 1.0))
    game_prop(scene, clock, "once")
    game_prop(scene, clock, "rep")
    game_prop(scene, clock, "ticks")
    timer_bricks(clock)
    s = sensor(clock, 'ALWAYS', "R_always")
    c = controller(clock, 'LOGIC_AND', "R_and", 1)
    r = actuator(clock, 'REMIX', "R_set", setting="rtx.emissiveIntensity", value="2")
    wire([s], c, [r])
    # a healthy chain (counts tics), as in an old game
    s2 = sensor(clock, 'ALWAYS', "K_always", use_pulse_true_level=True)
    c2 = controller(clock, 'LOGIC_AND', "K_and", 1)
    a2 = actuator(clock, 'PROPERTY', "K_count", mode='ADD', property="ticks", value="1")
    wire([s2], c2, [a2])
    start_states(clock, [1])
    p2s.add_probe(scene)
    save(scene, folder, "p10c_new_bricks")
    E["p10c"]["new_bricks"] = {"file": "p10c_new_bricks", "object": "Clock", "bricks": brick_names(clock),
                               "timers": {"T_once": [TIMER["once"], False], "T_rep": [TIMER["repeat"], True]},
                               "remix": {"R_set": ["rtx.emissiveIntensity", "2"]}}


def make(folder, E):
    E["p10c"] = {}
    failed = {}
    for name, fn in (("states", make_states), ("missing", make_missing), ("timer", make_timer),
                     ("remix", make_remix), ("subject", make_subject), ("new_bricks", make_new_bricks)):
        try:
            fn(folder, E)
        except Exception:
            import traceback
            failed[name] = traceback.format_exc()
            print("BGE-Remix p10c: making %s failed:\n%s" % (name, failed[name]))
    if failed:
        E["p10c"]["failed"] = failed
