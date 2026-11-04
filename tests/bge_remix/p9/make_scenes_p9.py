"""BGE-Remix P9.0: the study's scenes (run inside our Blender, background).

p9_study_spheres.blend        six spheres in a row on a grey backdrop, under
                              one sun and the world's sky, an orthographic
                              camera (each sphere's rectangle on screen is
                              known); each sphere a P8 Physical material (the
                              shading the RTX Remix Material node will use):
                              grey roughness 0.5, grey roughness 0.05, grey
                              roughness 1, gold metal roughness 0.3, orange
                              emissive strength 2, clear glass (transmission
                              1, IOR 1.5). Modern pipeline, Remix off: the
                              OpenGL fallback's picture
p9_study_spheres_remix.blend  the same with RTX Remix on
p9_study_spheres_aces(_remix) P9.1: the same two with ACES tone mapping

P9.1's room (p9_pipe_*.blend): a floor, three walls open to the sky, a table
with a box and a sphere, a sun without a shadow of its own and a point lamp,
saved as P8 saved its four pipeline combinations, as P9 saves a Classic scene
whose Remix switch was left on, once for each look fix (the sun's shadow on,
a black sky), with ACES (Remix off and on), and with a Classic copy of the
room the game switches to and back from (p9_pipe_switch).

All hold the study's logic object (P9Study): with BGE_REMIX_P9_MODE set it
calls bge.render.remixSetOption ("api": once; "sweep": every debug view in
turn, a picture of each; "menu": Remix's own menu on); unset, it does
nothing.

argv (after --): folder
"""

import json
import math
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p3"))
import make_scenes_p2 as p2  # noqa: E402  (probe, lamps, materials)
import make_scenes_p3 as p3  # noqa: E402  (spheres)

WIDTH, HEIGHT = 960, 540
ORTHO = 12.0
VIEW_H = ORTHO * HEIGHT / WIDTH
RADIUS = 0.7

# (label, base colour, metallic, roughness, extra physical settings)
SPHERES = [
    ("grey_r050", (0.6, 0.6, 0.6), 0.0, 0.5, {}),
    ("grey_r005", (0.6, 0.6, 0.6), 0.0, 0.05, {}),
    ("grey_r100", (0.6, 0.6, 0.6), 0.0, 1.0, {}),
    ("gold_metal", (1.0, 0.77, 0.34), 1.0, 0.3, {}),
    ("orange_emissive", (0.2, 0.2, 0.2), 0.0, 0.5, {"emission_strength": 2.0}),
    ("clear_glass", (0.95, 0.97, 1.0), 0.0, 0.05, {"transmission": 1.0, "ior": 1.5}),
]

STUDY_SCRIPT = r'''
# BGE-Remix P9.0: the study's use of bge.render.remixSetOption
import json, os
import bge
env = os.environ
mode = env.get("BGE_REMIX_P9_MODE", "")
g = bge.logic.globalDict
n = g.get("p9_n", 0) + 1
g["p9_n"] = n
out = env.get("BGE_REMIX_P9_OUT", "")

def save(state):
    if out:
        with open(out, "w") as f:
            json.dump(state, f, indent=1)

if mode == "api" and n == 120:
    # once: a debug view on, then off again (False when Remix is not drawing)
    on = bge.render.remixSetOption("rtx.debugView.debugViewIdx", 2)
    off = bge.render.remixSetOption("rtx.debugView.debugViewIdx", 0)
    save({"mode": mode, "frame": n, "on": on, "off": off})
elif mode == "menu" and n == int(env.get("BGE_REMIX_P9_MENU_FRAME", "120")):
    # Remix's own menu (Alt+X in Remix), simple view
    took = bge.render.remixSetOption("rtx.showUI", 1)
    save({"mode": mode, "frame": n, "took": took})
elif mode == "sweep":
    # from START on, every STEP frames the next debug view (0 first: the
    # final picture), a picture of it STEP - 1 frames later
    start = int(env.get("BGE_REMIX_P9_START", "300"))
    step = int(env.get("BGE_REMIX_P9_STEP", "3"))
    first, last = int(env.get("BGE_REMIX_P9_FIRST", "1")), int(env.get("BGE_REMIX_P9_LAST", "1000"))
    folder = env.get("BGE_REMIX_P9_DIR", "")
    st = g.setdefault("p9_sweep", {"mode": mode, "first": first, "last": last, "set": {}, "done": False})
    if n >= start and folder and not st["done"]:
        k, phase = divmod(n - start, step)
        idx = 0 if k == 0 else first + k - 1
        if idx > last:
            st["done"] = True
            bge.render.remixSetOption("rtx.debugView.debugViewIdx", 0)
            save(st)
        elif phase == 0:
            st["set"][str(idx)] = bool(bge.render.remixSetOption("rtx.debugView.debugViewIdx", idx))
            st["last_set"] = idx
            if idx % 50 == 0:
                save(st)  # (where a crash stopped it)
        elif phase == step - 1:
            bge.render.makeScreenshot(os.path.join(folder, "dv_%04d.png" % idx))
'''


def layout_rect(x, z, half):
    """a square of half-size `half` at (x, z) on screen (left, top, right,
    bottom; pixels from the top left), its inner 80%"""
    h = half * 0.8
    l = (x - h) / ORTHO + 0.5
    r = (x + h) / ORTHO + 0.5
    t = 0.5 - (z + h) / VIEW_H
    b = 0.5 - (z - h) / VIEW_H
    return [int(round(l * WIDTH)), int(round(t * HEIGHT)), int(round(r * WIDTH)), int(round(b * HEIGHT))]


def base(name):
    scene = p2.empty_scene()
    scene.name = name
    gs = scene.game_settings
    gs.resolution_x, gs.resolution_y = WIDTH, HEIGHT
    gs.material_mode = 'GLSL'
    scene.render.resolution_x, scene.render.resolution_y = WIDTH, HEIGHT
    scene.render.resolution_percentage = 100
    w = bpy.data.worlds.new("World")
    w.use_sky_blend = True
    w.horizon_color = (0.45, 0.5, 0.6)
    w.zenith_color = (0.1, 0.2, 0.45)
    w.ambient_color = (0.0, 0.0, 0.0)
    scene.world = w
    cam = bpy.data.cameras.new("Camera")
    cam.type = 'ORTHO'
    cam.ortho_scale = ORTHO
    cam.clip_start = 0.1
    cam.clip_end = 100.0
    ob = bpy.data.objects.new("Camera", cam)
    ob.location = (0.0, -20.0, 0.0)
    ob.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    scene.objects.link(ob)
    scene.camera = ob
    return scene


def physical(name, base_col, metallic, roughness, **extra):
    ma = bpy.data.materials.new(name)
    ma.diffuse_color = base_col
    ma["bge_physical"] = 1
    ma["bge_metallic"] = metallic
    ma["bge_roughness"] = roughness
    for k, v in extra.items():
        ma["bge_" + k] = v
    return ma


def logic(scene, name, body):
    ob = bpy.data.objects.new(name, None)
    scene.objects.link(ob)
    scene.objects.active = ob
    text = bpy.data.texts.new(name.lower() + ".py")
    text.write(body)
    bpy.ops.logic.sensor_add(type='ALWAYS', object=ob.name)
    bpy.ops.logic.controller_add(type='PYTHON', object=ob.name)
    sensor, controller = ob.game.sensors[-1], ob.game.controllers[-1]
    sensor.use_pulse_true_level = True
    sensor.tick_skip = 0
    controller.mode = 'SCRIPT'
    controller.text = text
    sensor.link(controller)


def make_spheres(folder, E):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = base("P9Study")
    back = bpy.data.materials.new("P9_backdrop")
    back.diffuse_color = (0.35, 0.35, 0.35)
    back["bge_physical"] = 1
    back["bge_roughness"] = 0.8
    wall = p2.plane(scene, "P9_backdrop", 30.0, (0.0, 3.0, 0.0), (math.radians(90.0), 0.0, 0.0), back)
    wall.game.physics_type = 'NO_COLLISION'
    rects = {}
    step = 1.8
    for i, (label, col, metallic, rough, extra) in enumerate(SPHERES):
        ma = physical("P9_" + label, col, metallic, rough, **extra)
        if label == "orange_emissive":
            ma["bge_emission"] = [1.0, 0.5, 0.1]
        if label == "clear_glass":
            ma.game_settings.alpha_blend = 'ALPHA'
        x = (i - 2.5) * step
        ob = p3.sphere(scene, "P9_" + label, (x, 0.0, 0.3), ma, radius=RADIUS)
        ob.game.physics_type = 'NO_COLLISION'
        rects[label] = layout_rect(x, 0.3, RADIUS * 0.7)
    sun = p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 6.0), (math.radians(50.0), 0.0, math.radians(-30.0)),
                  energy=1.0)
    sun.data.shadow_soft_size = 0.2
    logic(scene, "P9Study", STUDY_SCRIPT)
    p2.add_probe(scene)
    for sc in bpy.data.scenes:
        sc["bge_remix_pipeline"] = 1   # Modern: RTX Remix's OpenGL fallback
        sc["bge_remix"] = 0
    scene.update()
    path = os.path.join(folder, "p9_study_spheres.blend")
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, compress=False)
    for sc in bpy.data.scenes:
        sc["bge_remix"] = 1
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "p9_study_spheres_remix.blend"),
                                check_existing=False, compress=False, copy=True)
    # P9.1: the same with ACES tone mapping (Remix's global tone mapper
    # finished with ACES, the same curve in the fallback), Remix off and on
    for remix in (0, 1):
        for sc in bpy.data.scenes:
            sc["bge_remix"] = remix
            sc["bge_remix_tonemap"] = 1
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "p9_study_spheres_aces%s.blend" %
                                                          ("_remix" if remix else "")),
                                    check_existing=False, compress=False, copy=True)
    E["spheres"] = {"files": ["p9_study_spheres", "p9_study_spheres_remix", "p9_study_spheres_aces",
                              "p9_study_spheres_aces_remix"], "rects": rects,
                    "order": [s[0] for s in SPHERES], "size": [WIDTH, HEIGHT]}


# ---------------------------------------------------------------------------
# P9.1: the pipelines (a lit room saved as P8 saved it, and as P9 does)

PIPE_SCRIPT = r'''
# BGE-Remix P9.1: what the game says of its pipeline and of Remix
import json, os
import bge
env = os.environ
g = bge.logic.globalDict
n = g.get("p9_pipe_n", 0) + 1
g["p9_pipe_n"] = n
st = g.setdefault("p9_pipe", {"samples": {}})
sc = bge.logic.getCurrentScene()
if n in (30, 90, 150):
    st["samples"][str(n)] = {"scene": sc.name, "pipeline": bge.render.getRenderPipeline(),
                             "drawing": bool(bge.render.isRemixDrawing())}
    out = env.get("BGE_REMIX_P9_PIPE_OUT", "")
    if out:
        with open(out, "w") as f:
            json.dump(st, f, indent=1)
if env.get("BGE_REMIX_P9_PIPE_SWITCH") == "1" and n in (60, 120):
    # the RTX Remix room to its Classic copy and back
    sc.replace("P9RoomClassic" if sc.name == "P9Room" else "P9Room")
'''

# (file, pipeline as stored: 0 Classic / 1 P8's Modern, Remix switch, tone
# mapping as stored (None: not stored, a P8 file), changes, what it opens as:
# pipeline, Remix switch, tone mapping)
PIPE_FILES = [
    ("p9_pipe_classic_gl", 0, 0, None, {}, ("CLASSIC", "OPENGL", "VIEW_TRANSFORM")),
    ("p9_pipe_classic_remix", 0, 1, None, {}, ("RTX_REMIX", "RTX_REMIX", "VIEW_TRANSFORM")),
    ("p9_pipe_modern_remix", 1, 1, None, {}, ("RTX_REMIX", "RTX_REMIX", "VIEW_TRANSFORM")),
    ("p9_pipe_modern_gl", 1, 0, None, {}, ("RTX_REMIX", "OPENGL", "VIEW_TRANSFORM")),
    # saved by P9 (its tone mapping stored): Classic, Remix's switch left on
    ("p9_pipe_classic_kept", 0, 1, 0, {}, ("CLASSIC", "RTX_REMIX", "VIEW_TRANSFORM")),
    # the look fixes, one at a time
    ("p9_pipe_modern_gl_shadow", 1, 0, None, {"shadow": True}, ("RTX_REMIX", "OPENGL", "VIEW_TRANSFORM")),
    ("p9_pipe_classic_gl_shadow", 0, 0, None, {"shadow": True}, ("CLASSIC", "OPENGL", "VIEW_TRANSFORM")),
    ("p9_pipe_modern_gl_nosky", 1, 0, None, {"nosky": True}, ("RTX_REMIX", "OPENGL", "VIEW_TRANSFORM")),
    ("p9_pipe_classic_gl_nosky", 0, 0, None, {"nosky": True}, ("CLASSIC", "OPENGL", "VIEW_TRANSFORM")),
    ("p9_pipe_aces_gl", 1, 0, 1, {}, ("RTX_REMIX", "OPENGL", "ACES")),
    ("p9_pipe_aces_remix", 1, 1, 1, {"max_bounces": 8}, ("RTX_REMIX", "RTX_REMIX", "ACES")),
]
PIPE_CAMERA = ((0.0, -9.0, 3.2), (0.0, 0.0, 1.2), 30.0)


def screen_rect(scene, points):
    """the on-screen rectangle (left, top, right, bottom; pixels from the
    top left) around world points, seen by the scene's camera"""
    from bpy_extras.object_utils import world_to_camera_view
    from mathutils import Vector
    xs, ys = [], []
    for co in points:
        v = world_to_camera_view(scene, scene.camera, Vector(co))
        xs.append(v.x)
        ys.append(v.y)
    return [int(round(min(xs) * WIDTH)), int(round((1.0 - max(ys)) * HEIGHT)),
            int(round(max(xs) * WIDTH)), int(round((1.0 - min(ys)) * HEIGHT))]


def classic_material(name, col):
    ma = p2.material(name, col)
    ma["bge_physical"] = 0
    return ma


def box(scene, name, location, size, ma):
    ob = p2.cube(scene, name, 1.0, location, ma)
    ob.scale = size
    ob.game.physics_type = 'NO_COLLISION'
    return ob


def make_pipelines(folder, E):
    """the room: a floor, three walls (open at the front and the top, under
    the world's sky), a table with a box and a sphere on it, a sun (no
    shadow of its own) and a point lamp; Classic materials and lamps but the
    sphere (Physical)"""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = p2.empty_scene()
    scene.name = "P9Room"
    gs = scene.game_settings
    gs.resolution_x, gs.resolution_y = WIDTH, HEIGHT
    gs.material_mode = 'GLSL'
    scene.render.resolution_x, scene.render.resolution_y = WIDTH, HEIGHT
    scene.render.resolution_percentage = 100
    scene.view_settings.view_transform = 'Default'
    w = bpy.data.worlds.new("World")
    w.use_sky_blend = True
    w.horizon_color = (0.45, 0.5, 0.6)
    w.zenith_color = (0.1, 0.2, 0.45)
    w.ambient_color = (0.0, 0.0, 0.0)
    scene.world = w
    loc, target, lens = PIPE_CAMERA
    p2.camera(scene, loc, target, lens=lens)
    floor = classic_material("P9R_floor", (0.55, 0.5, 0.45))
    wall = classic_material("P9R_wall", (0.7, 0.7, 0.7))
    wood = classic_material("P9R_wood", (0.45, 0.3, 0.2))
    red = classic_material("P9R_red", (0.8, 0.15, 0.1))
    ob = p2.plane(scene, "P9R_floor", 8.0, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), floor)
    ob.game.physics_type = 'NO_COLLISION'
    for name, location, rotation in (("P9R_back", (0.0, 4.0, 2.0), (math.radians(90.0), 0.0, 0.0)),
                                     ("P9R_left", (-4.0, 0.0, 2.0), (0.0, math.radians(90.0), 0.0)),
                                     ("P9R_right", (4.0, 0.0, 2.0), (0.0, math.radians(90.0), 0.0))):
        ob = p2.plane(scene, name, 8.0, location, rotation, wall)
        # (the back wall 8 wide and 4 high, the side walls 8 deep and 4 high)
        ob.scale = (1.0, 0.5, 1.0) if name == "P9R_back" else (0.5, 1.0, 1.0)
        ob.game.physics_type = 'NO_COLLISION'
    box(scene, "P9R_table", (0.0, 0.5, 0.8), (2.4, 1.4, 0.1), wood)
    for i, (x, y) in enumerate(((-1.1, -0.1), (1.1, -0.1), (-1.1, 1.1), (1.1, 1.1))):
        box(scene, "P9R_leg_%d" % i, (x, y, 0.375), (0.1, 0.1, 0.75), wood)
    box(scene, "P9R_box", (-0.5, 0.5, 1.1), (0.5, 0.5, 0.5), red)
    ball = physical("P9R_ball", (0.8, 0.8, 0.8), 0.0, 0.4)
    sph = p3.sphere(scene, "P9R_ball", (0.6, 0.5, 1.2), ball, radius=0.35)
    sph.game.physics_type = 'NO_COLLISION'
    sun = p2.lamp(scene, "P9R_sun", 'SUN', (0.0, -6.0, 8.0), (math.radians(50.0), 0.0, math.radians(-30.0)),
                  energy=1.0)
    sun.data.shadow_method = 'NOSHADOW'
    p2.lamp(scene, "P9R_fill", 'POINT', (-2.0, -2.0, 3.0), energy=1.0, distance=8.0)
    for la in bpy.data.lamps:
        la["bge_physical"] = 0
    logic(scene, "P9Pipe", PIPE_SCRIPT)
    p2.add_probe(scene)
    scene.update()
    rects = {"floor": screen_rect(scene, [(-3.0, -3.0, 0.0), (-1.5, -1.5, 0.0)]),
             "back_wall": screen_rect(scene, [(1.8, 3.95, 2.6), (3.4, 3.95, 3.6)]),
             "left_wall": screen_rect(scene, [(-3.95, 0.5, 2.2), (-3.95, 3.0, 3.4)]),
             "ball": screen_rect(scene, [(0.25, 0.5, 0.85), (0.95, 0.5, 1.55)])}

    def store(sc, pipeline, remix, tone):
        sc["bge_remix_pipeline"] = pipeline
        sc["bge_remix"] = remix
        if tone is None:
            if "bge_remix_tonemap" in sc:
                del sc["bge_remix_tonemap"]
        else:
            sc["bge_remix_tonemap"] = tone

    files = []
    for name, pipeline, remix, tone, changes, opens in PIPE_FILES:
        sun.data.shadow_method = 'RAY_SHADOW' if changes.get("shadow") else 'NOSHADOW'
        dark = changes.get("nosky")
        w.horizon_color = (0.0, 0.0, 0.0) if dark else (0.45, 0.5, 0.6)
        w.zenith_color = (0.0, 0.0, 0.0) if dark else (0.1, 0.2, 0.45)
        if "max_bounces" in changes:
            scene["bge_rtx_max_bounces"] = changes["max_bounces"]
        elif "bge_rtx_max_bounces" in scene:
            del scene["bge_rtx_max_bounces"]
        store(scene, pipeline, remix, tone)
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, name + ".blend"), check_existing=False,
                                    compress=False, copy=True)
        files.append({"file": name, "opens": list(opens)})
    sun.data.shadow_method = 'NOSHADOW'
    w.horizon_color, w.zenith_color = (0.45, 0.5, 0.6), (0.1, 0.2, 0.45)
    store(scene, 0, 1, None)
    if "bge_rtx_max_bounces" in scene:
        del scene["bge_rtx_max_bounces"]
    # the room (saved by P8 as Classic + Remix: it opens in RTX Remix) and
    # its Classic copy, a second scene the game switches to and back from
    copy = bpy.data.scenes.new("P9RoomClassic")
    copy.render.engine = 'BLENDER_GAME'
    copy.game_settings.resolution_x, copy.game_settings.resolution_y = WIDTH, HEIGHT
    copy.game_settings.material_mode = 'GLSL'
    copy.view_settings.view_transform = 'Default'
    copy.world = w
    for ob in scene.objects:
        copy.objects.link(ob)
    copy.camera = scene.camera
    copy["bge_remix_pipeline"] = 0
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "p9_pipe_switch.blend"), check_existing=False,
                                compress=False, copy=True)
    E["pipelines"] = {"files": files, "switch": "p9_pipe_switch", "rects": rects, "size": [WIDTH, HEIGHT],
                      "scenes": ["P9Room", "P9RoomClassic"]}


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    folder = argv[0]
    os.makedirs(folder, exist_ok=True)
    E = {"failed": {}}
    import traceback
    for name, maker in (("spheres", make_spheres), ("pipelines", make_pipelines)):
        try:
            maker(folder, E)
        except Exception:
            E["failed"][name] = traceback.format_exc()
            print("BGE-Remix: making the P9 %s scenes failed:\n%s" % (name, E["failed"][name]))
    with open(os.path.join(folder, "p9_expected.json"), "w") as f:
        json.dump(E, f, indent=1)
    print("BGE-Remix: wrote", os.path.join(folder, "p9_expected.json"))


main()
