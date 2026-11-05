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

P9.2's spheres (p9_rs_*.blend): twelve spheres in two rows, each a P8
Physical material (p9_rs_physical) and the same made RTX Remix materials
(p9_rs_nodes*: plain albedo, an albedo image, roughness 0, 0.5 and 1, gold
metal, emissive, a normal map, an alpha cut-out, glass, a thin film, a
height map), with Remix off and on, with ACES, with a black sky for F12.
P9.2's build (p9_rb_*.blend): four materials before and after picking RTX
Remix, and two of them back to Classic.

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
                                     ("P9R_right", (4.0, 0.0, 2.0), (0.0, math.radians(-90.0), 0.0))):
        # (each wall faces into the room: the right one turned the other way)
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
             "right_wall": screen_rect(scene, [(3.95, 0.5, 2.2), (3.95, 3.0, 3.4)]),
             "table": screen_rect(scene, [(-1.1, -0.2, 0.85), (-0.85, 1.2, 0.85)]),
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


# ---------------------------------------------------------------------------
# P9.2: the RTX Remix Material node

# (label, base colour, metallic, roughness, extra physical settings, what
# else: 'image' an albedo image, 'normal' a normal map, 'cutout' an albedo
# image with cut-out alpha, 'glass', 'film' a thin film, 'height' a height
# map; the last two only on the node, which only Remix draws)
REMIX_SPHERES = [
    ("albedo", (0.75, 0.25, 0.2), 0.0, 0.5, {}, None),
    ("albedo_image", (1.0, 1.0, 1.0), 0.0, 0.5, {}, 'image'),
    ("rough_000", (0.55, 0.6, 0.65), 0.0, 0.0, {}, None),
    ("rough_050", (0.6, 0.55, 0.65), 0.0, 0.5, {}, None),
    ("rough_100", (0.65, 0.6, 0.55), 0.0, 1.0, {}, None),
    ("gold", (1.0, 0.77, 0.34), 1.0, 0.3, {}, None),
    ("emissive", (0.2, 0.21, 0.22), 0.0, 0.5, {"emission_strength": 2.0}, None),
    ("normal_map", (0.62, 0.62, 0.6), 0.0, 0.4, {}, 'normal'),
    ("alpha_cutout", (1.0, 1.0, 1.0), 0.0, 0.5, {}, 'cutout'),
    ("glass", (0.95, 0.97, 1.0), 0.0, 0.05, {"transmission": 1.0, "ior": 1.5}, 'glass'),
    ("thin_film", (0.5, 0.5, 0.55), 0.0, 0.2, {}, 'film'),
    ("height", (0.6, 0.6, 0.58), 0.0, 0.6, {}, 'height'),
]
THIN_FILM_NM = 400.0


def slot_image(ma, img, color=False, normal=False, alpha=False):
    """a UV texture slot holding an image"""
    tex = bpy.data.textures.new(ma.name + "_" + img.name, 'IMAGE')
    tex.image = img
    tex.use_normal_map = normal
    slot = ma.texture_slots.add()
    slot.texture = tex
    slot.texture_coords = 'UV'
    slot.use_map_color_diffuse = color
    slot.use_map_normal = normal
    slot.use_map_alpha = alpha
    if normal:
        slot.normal_map_space = 'TANGENT'
    return slot


def remix_spheres_scene(name):
    """the spheres of REMIX_SPHERES as P8 Physical materials (two rows of six
    under one sun, shadows on, and the world's sky), and their rectangles"""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = base(name)
    back = bpy.data.materials.new("P9RS_backdrop")
    back.diffuse_color = (0.35, 0.35, 0.35)
    back["bge_physical"] = 1
    back["bge_roughness"] = 0.8
    wall = p2.plane(scene, "P9RS_backdrop", 30.0, (0.0, 3.0, 0.0), (math.radians(90.0), 0.0, 0.0), back)
    wall.game.physics_type = 'NO_COLLISION'
    checker = p3.image("P9RS_checker", p3.checker, size=128)
    cutout = p3.image("P9RS_cutout", lambda x, y: p3.checker(x, y)[:3] + ((1.0 if int(y * 10) % 2 == 0 else 0.0),),
                      size=128)
    bumps = p3.image("P9RS_bumps", p3.bumps, size=128, non_color=True)
    rects, mats = {}, {}
    for i, (label, col, metallic, rough, extra, what) in enumerate(REMIX_SPHERES):
        ma = physical("P9RS_" + label, col, metallic, rough, **extra)
        if label == "emissive":
            ma["bge_emission"] = [1.0, 0.5, 0.1]
        if what == 'glass':
            ma.game_settings.alpha_blend = 'ALPHA'
        if what == 'image':
            slot_image(ma, checker, color=True)
        if what == 'normal':
            slot_image(ma, bumps, normal=True)
        if what == 'cutout':
            slot_image(ma, cutout, color=True, alpha=True)
            ma.alpha = 0.0
            ma.game_settings.alpha_blend = 'CLIP'
        row, k = divmod(i, 6)
        x, z = (k - 2.5) * 1.8, (1.2 if row == 0 else -1.0)
        ob = p3.sphere(scene, "P9RS_" + label, (x, 0.0, z), ma, radius=RADIUS)
        ob.game.physics_type = 'NO_COLLISION'
        rects[label] = layout_rect(x, z, RADIUS * 0.7)
        mats[label] = ma.name
    sun = p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 6.0), (math.radians(50.0), 0.0, math.radians(-30.0)),
                  energy=1.0)
    sun.data.shadow_soft_size = 0.2
    sun.data.shadow_method = 'RAY_SHADOW'
    logic(scene, "P9Study", STUDY_SCRIPT)
    p2.add_probe(scene)
    for sc in bpy.data.scenes:
        sc["bge_remix_pipeline"] = 1
        sc["bge_remix"] = 0
    scene.update()
    return scene, rects, mats


def save_copy(folder, name, remix=None, tone=None):
    for sc in bpy.data.scenes:
        if remix is not None:
            sc["bge_remix"] = remix
        if tone is not None:
            sc["bge_remix_tonemap"] = tone
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, name + ".blend"), check_existing=False,
                                compress=False, copy=True)
    return name


def make_remix_spheres(folder, E):
    """p9_rs_physical: the P8 Physical twins; p9_rs_nodes*: the same made RTX
    Remix materials (Material: RTX Remix), the thin film and height map then
    set on their nodes; with Remix on, with ACES, with a black sky (F12)"""
    from bl_operators.bge_remix_material import make_remix_material
    scene, rects, mats = remix_spheres_scene("P9RemixSpheres")
    files = [save_copy(folder, "p9_rs_physical", remix=0)]
    heights = p3.image("P9RS_heights", p3.tiles_rough, size=128, non_color=True)
    for label, col, metallic, rough, extra, what in REMIX_SPHERES:
        ma = bpy.data.materials[mats[label]]
        node = make_remix_material(ma)
        if what == 'film':
            node.inputs["Thin Film"].default_value = THIN_FILM_NM
        if what == 'height':
            nt = ma.node_tree
            geo = nt.nodes.new("ShaderNodeGeometry")
            tex = nt.nodes.new("ShaderNodeTexture")
            tex.texture = bpy.data.textures.new("P9RS_heights", 'IMAGE')
            tex.texture.image = heights
            nt.links.new(geo.outputs["UV"], tex.inputs["Vector"])
            nt.links.new(tex.outputs["Color"], node.inputs["Height"])
            node.inputs["Displace In"].default_value = 0.05
        if what == 'glass':
            node.blend = 'BLENDED'
    files.append(save_copy(folder, "p9_rs_nodes", remix=0, tone=0))
    files.append(save_copy(folder, "p9_rs_nodes_remix", remix=1, tone=0))
    files.append(save_copy(folder, "p9_rs_nodes_aces", remix=0, tone=1))
    files.append(save_copy(folder, "p9_rs_nodes_aces_remix", remix=1, tone=1))
    # P9.2b: the same with its own resolution scale (Remix reads it only when
    # it starts: the engine sends it before)
    for sc in bpy.data.scenes:
        sc["bge_rtx_resolution_scale"] = 0.5
    files.append(save_copy(folder, "p9_rs_nodes_aces_remix_res50", remix=1, tone=1))
    for sc in bpy.data.scenes:
        del sc["bge_rtx_resolution_scale"]
    # F12 against the game: a black sky (Blender Internal lights nothing by it)
    w = scene.world
    w.horizon_color = (0.0, 0.0, 0.0)
    w.zenith_color = (0.0, 0.0, 0.0)
    files.append(save_copy(folder, "p9_rs_nodes_dark", remix=0, tone=0))
    expect = {}
    for label, col, metallic, rough, extra, what in REMIX_SPHERES:
        expect[label] = {"material": mats[label], "what": what, "albedo": list(col), "metallic": metallic,
                         "roughness": rough, "emission": [1.0, 0.5, 0.1] if label == "emissive" else None,
                         "intensity": extra.get("emission_strength", 0.0), "ior": extra.get("ior")}
    E["remix_spheres"] = {"files": files, "rects": rects, "order": [s[0] for s in REMIX_SPHERES],
                          "spheres": expect, "thin_film_nm": THIN_FILM_NM, "size": [WIDTH, HEIGHT],
                          # out of the gate: Remix draws them its own way (glass refracts,
                          # thin film shimmers); emissive: Remix's ACES treats bright
                          # colours with a colour transform no fallback curve matched
                          # (P9.2's colour test: the best simple rule still 7.7 off)
                          "gate_out": ["glass", "thin_film", "emissive"]}


def make_remix_build(folder, E):
    """four materials before and after Material: RTX Remix (a coloured
    Classic one, a Classic one with colour, normal and alpha images, a P8
    Physical one, a Physical one with a procedural texture on its roughness),
    and after Material: Classic again"""
    from bl_operators.bge_remix_material import make_remix_material, make_classic_material
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = base("P9RemixBuild")
    back = bpy.data.materials.new("P9RB_backdrop")
    back.diffuse_color = (0.35, 0.35, 0.35)
    back["bge_physical"] = 0
    wall = p2.plane(scene, "P9RB_backdrop", 30.0, (0.0, 3.0, 0.0), (math.radians(90.0), 0.0, 0.0), back)
    wall.game.physics_type = 'NO_COLLISION'
    checker = p3.image("P9RB_checker", lambda x, y: p3.checker(x, y)[:3] + ((1.0 if int(x * 8) % 2 == 0 else 0.4),),
                       size=128)
    bumps = p3.image("P9RB_bumps", p3.bumps, size=128, non_color=True)
    m = {}
    cc = p2.material("P9RB_classic_colour", (0.2, 0.5, 0.8), ref=0.8, hard=50)
    cc["bge_physical"] = 0
    m["classic_colour"] = cc
    ci = p2.material("P9RB_classic_images", (1.0, 1.0, 1.0), ref=1.0, hard=80)
    ci["bge_physical"] = 0
    slot_image(ci, checker, color=True, alpha=True)
    slot_image(ci, bumps, normal=True)
    ci.alpha = 0.0
    ci.game_settings.alpha_blend = 'ALPHA'
    m["classic_images"] = ci
    ph = physical("P9RB_physical", (0.7, 0.4, 0.2), 0.3, 0.4)
    m["physical"] = ph
    pp = physical("P9RB_physical_proc", (0.5, 0.6, 0.4), 0.0, 0.5)
    clouds = bpy.data.textures.new("P9RB_clouds", 'CLOUDS')
    clouds.noise_scale = 0.4
    slot = pp.texture_slots.add()
    slot.texture = clouds
    slot.texture_coords = 'ORCO'
    slot.use_map_color_diffuse = False
    slot.use_map_hardness = True
    m["physical_proc"] = pp
    rects = {}
    for i, label in enumerate(("classic_colour", "classic_images", "physical", "physical_proc")):
        x = (i - 1.5) * 2.4
        ob = p3.sphere(scene, "P9RB_" + label, (x, 0.0, 0.3), m[label], radius=0.9)
        ob.game.physics_type = 'NO_COLLISION'
        rects[label] = layout_rect(x, 0.3, 0.9 * 0.7)
    sun = p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 6.0), (math.radians(50.0), 0.0, math.radians(-30.0)),
                  energy=1.0)
    sun.data.shadow_method = 'RAY_SHADOW'
    for la in bpy.data.lamps:
        la["bge_physical"] = 0
    logic(scene, "P9Study", STUDY_SCRIPT)
    p2.add_probe(scene)
    for sc in bpy.data.scenes:
        sc["bge_remix_pipeline"] = 1
        sc["bge_remix"] = 0
    scene.update()
    files = [save_copy(folder, "p9_rb_before", remix=0)]
    for label in ("classic_colour", "classic_images", "physical", "physical_proc"):
        make_remix_material(m[label])
    files.append(save_copy(folder, "p9_rb_after", remix=0))
    files.append(save_copy(folder, "p9_rb_after_remix", remix=1))
    for label in ("classic_colour", "classic_images"):
        make_classic_material(m[label])
    files.append(save_copy(folder, "p9_rb_back", remix=0))
    E["remix_build"] = {"files": files, "rects": rects,
                        "materials": {k: v.name for k, v in m.items()},
                        "classic_colour": {"albedo": [0.2 * 0.8, 0.5 * 0.8, 0.8 * 0.8],
                                           "roughness": (2.0 / 52.0) ** 0.25},
                        "physical": {"albedo": [0.7, 0.4, 0.2], "metallic": 0.3, "roughness": 0.4}}



# P9.2 diagnostic: the light's balance between Remix and the fallback
# (label, albedo, metallic, roughness, emission intensity): six that only
# glow (the top row: they trace the tone curves), four lit (the bottom row;
# matte_018 dark enough that Remix never clips it)
BALANCE = [
    ("emit_005", (0.0, 0.0, 0.0), 0.0, 1.0, 0.05),
    ("emit_010", (0.0, 0.0, 0.0), 0.0, 1.0, 0.1),
    ("emit_025", (0.0, 0.0, 0.0), 0.0, 1.0, 0.25),
    ("emit_050", (0.0, 0.0, 0.0), 0.0, 1.0, 0.5),
    ("emit_100", (0.0, 0.0, 0.0), 0.0, 1.0, 1.0),
    ("emit_400", (0.0, 0.0, 0.0), 0.0, 1.0, 4.0),
    ("matte", (0.8, 0.8, 0.8), 0.0, 1.0, 0.0),
    ("matte_018", (0.18, 0.18, 0.18), 0.0, 1.0, 0.0),
    ("smooth", (0.8, 0.8, 0.8), 0.0, 0.1, 0.0),
    ("metal", (0.8, 0.8, 0.8), 1.0, 0.3, 0.0),
]


def make_light_balance(folder, E):
    """ten spheres, RTX Remix materials, ACES: on top six that only glow
    (0.05 to 4: the same light in both, to trace the two tone curves), below
    a matte one, a dark matte one, a smooth one, a metal one; lit by one sun
    under a black sky (p9_lb_sun) or by a plain grey sky alone (p9_lb_sky)"""
    from bl_operators.bge_remix_material import make_remix_material
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = base("P9LightBalance")
    rects = {}
    glow = [b for b in BALANCE if b[4] > 0.0]
    lit = [b for b in BALANCE if b[4] <= 0.0]
    for label, col, metallic, rough, emit in BALANCE:
        ma = physical("P9LB_" + label, col, metallic, rough)
        node = make_remix_material(ma)
        node.inputs["Emissive Color"].default_value = (1.0, 1.0, 1.0, 1.0)
        node.inputs["Emissive Intensity"].default_value = emit
        if emit > 0.0:
            i, n, z, r, step = glow.index((label, col, metallic, rough, emit)), len(glow), 1.6, 0.75, 1.9
        else:
            i, n, z, r, step = lit.index((label, col, metallic, rough, emit)), len(lit), -1.5, 0.9, 2.4
        x = (i - (n - 1) / 2.0) * step
        ob = p3.sphere(scene, "P9LB_" + label, (x, 0.0, z), ma, radius=r)
        ob.game.physics_type = 'NO_COLLISION'
        rects[label] = layout_rect(x, z, r * 0.7)
    sun = p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 6.0), (math.radians(45.0), 0.0, 0.0), energy=1.0)
    sun.data.shadow_method = 'NOSHADOW'
    w = scene.world
    w.use_sky_blend = False
    w.horizon_color = (0.0, 0.0, 0.0)
    w.zenith_color = (0.0, 0.0, 0.0)
    w.ambient_color = (0.0, 0.0, 0.0)
    logic(scene, "P9Study", STUDY_SCRIPT)
    p2.add_probe(scene)
    for sc in bpy.data.scenes:
        sc["bge_remix_pipeline"] = 1
    scene.update()
    files = [save_copy(folder, "p9_lb_sun", remix=0, tone=1), save_copy(folder, "p9_lb_sun_remix", remix=1, tone=1)]
    sun.data.energy = 0.0
    w.horizon_color = (0.5, 0.5, 0.5)
    w.zenith_color = (0.5, 0.5, 0.5)
    files += [save_copy(folder, "p9_lb_sky", remix=0, tone=1), save_copy(folder, "p9_lb_sky_remix", remix=1, tone=1)]
    E["light_balance"] = {"files": files, "rects": rects, "order": [b[0] for b in BALANCE],
                          "emission": {b[0]: b[4] for b in BALANCE if b[4] > 0.0}}



# P9.2 diagnostic: Remix's tone curve on colours. 54 spheres that only glow,
# on black with no lamps: a row per colour (grey first: Remix's own curve),
# a column per brightness
COLOUR_ROWS = [
    ("grey", (1.0, 1.0, 1.0)),
    ("orange", (1.0, 0.5, 0.1)),
    ("red", (1.0, 0.05, 0.05)),
    ("green", (0.05, 1.0, 0.05)),
    ("blue", (0.05, 0.05, 1.0)),
    ("cyan", (0.05, 1.0, 1.0)),
]
COLOUR_LEVELS = [0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 4.0]


def make_colour_curve(folder, E):
    """the 54 glowing spheres (RTX Remix materials, black albedo, ACES), in
    the fallback and in Remix: p9_cc / p9_cc_remix"""
    from bl_operators.bge_remix_material import make_remix_material
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = base("P9ColourCurve")
    rects, spheres = {}, {}
    for r, (row, col) in enumerate(COLOUR_ROWS):
        for k, level in enumerate(COLOUR_LEVELS):
            label = "%s_%g" % (row, level)
            ma = physical("P9CC_" + label, (0.0, 0.0, 0.0), 0.0, 1.0)
            node = make_remix_material(ma)
            node.inputs["Emissive Color"].default_value = col + (1.0,)
            node.inputs["Emissive Intensity"].default_value = level
            x, z = (k - (len(COLOUR_LEVELS) - 1) / 2.0) * 1.25, (2.5 - r) * 1.05
            ob = p3.sphere(scene, "P9CC_" + label, (x, 0.0, z), ma, radius=0.42)
            ob.game.physics_type = 'NO_COLLISION'
            rects[label] = layout_rect(x, z, 0.42 * 0.7)
            spheres[label] = {"row": row, "colour": list(col), "level": level}
    # a lamp that gives no light: an orthographic scene with no lamps is a
    # 2D screen, drawn in OpenGL even with Remix on (P8.30)
    p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 6.0), (math.radians(45.0), 0.0, 0.0), energy=0.0)
    w = scene.world
    w.use_sky_blend = False
    w.horizon_color = (0.0, 0.0, 0.0)
    w.zenith_color = (0.0, 0.0, 0.0)
    w.ambient_color = (0.0, 0.0, 0.0)
    logic(scene, "P9Study", STUDY_SCRIPT)
    p2.add_probe(scene)
    for sc in bpy.data.scenes:
        sc["bge_remix_pipeline"] = 1
    scene.update()
    files = [save_copy(folder, "p9_cc", remix=0, tone=1), save_copy(folder, "p9_cc_remix", remix=1, tone=1)]
    E["colour_curve"] = {"files": files, "rects": rects, "order": list(rects), "spheres": spheres}

def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    folder = argv[0]
    os.makedirs(folder, exist_ok=True)
    E = {"failed": {}}
    import traceback
    for name, maker in (("spheres", make_spheres), ("pipelines", make_pipelines),
                        ("remix_spheres", make_remix_spheres), ("remix_build", make_remix_build),
                        ("light_balance", make_light_balance), ("colour_curve", make_colour_curve)):
        try:
            maker(folder, E)
        except Exception:
            E["failed"][name] = traceback.format_exc()
            print("BGE-Remix: making the P9 %s scenes failed:\n%s" % (name, E["failed"][name]))
    with open(os.path.join(folder, "p9_expected.json"), "w") as f:
        json.dump(E, f, indent=1)
    print("BGE-Remix: wrote", os.path.join(folder, "p9_expected.json"))


main()
