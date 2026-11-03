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

Both hold the study's logic object (P9Study): with BGE_REMIX_P9_MODE set it
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
    E["spheres"] = {"files": ["p9_study_spheres", "p9_study_spheres_remix"], "rects": rects,
                    "order": [s[0] for s in SPHERES], "size": [WIDTH, HEIGHT]}


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    folder = argv[0]
    os.makedirs(folder, exist_ok=True)
    E = {"failed": {}}
    try:
        make_spheres(folder, E)
    except Exception:
        import traceback
        E["failed"]["spheres"] = traceback.format_exc()
        print("BGE-Remix: making the study spheres failed:\n" + E["failed"]["spheres"])
    with open(os.path.join(folder, "p9_expected.json"), "w") as f:
        json.dump(E, f, indent=1)
    print("BGE-Remix: wrote", os.path.join(folder, "p9_expected.json"))


main()
