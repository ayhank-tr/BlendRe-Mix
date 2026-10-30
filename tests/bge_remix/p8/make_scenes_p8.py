# BGE-Remix P8 test scenes, Part A (materials). Runs inside Blender 2.79b
# (Python 3.5: no f-strings).
#
#   blender --background --factory-startup --python make_scenes_p8.py -- <out_dir>           (ours)
#   blender --background --factory-startup --python make_scenes_p8.py -- <out_dir> --stock   (official 2.79b)
#
# Ours writes:
#   glsl_features.blend  one object per GLSL feature: a node material, a ramp,
#                        a normal map, a shadow, environment light, mist
#                        (Classic materials, as in stock 2.79's GLSL)
#   fallback.blend       a material made to fail on purpose (BGE_REMIX_GLSL_FAIL)
#                        next to a normal one
#   proc_textures.blend  one shadeless panel per procedural type and setting,
#                        UV mapped; a row with bump under a sun; one panel
#                        without a UV map
#   proc_animated.blend  one procedural panel whose texture a script changes
#                        while playing (embedded: bpy)
#   pbr_spheres.blend    physical spheres from non-metal to metal and smooth
#                        to rough; glass, subsurface and emissive spheres
#   pbr_classic.blend    the same in Classic shading
#   classic_remix.blend  Classic materials Remix ignored before P8: a ray
#                        mirror, raytraced glass, subsurface, Z transparency,
#                        Add blending, an invisible material
#   p8_expected.json     the layout (screen rectangles) and what each test expects
#
# The official 2.79b writes (--stock), saved in Multitexture mode:
#   mt_features.blend    one object per Multitexture-only feature
#   mt_bench.blend       2,000 lit, textured objects and 8 lamps
#
# Every scene has the P2 probe (frames, frame times, a screenshot) and an
# orthographic camera, so each object's rectangle on screen is known
# (layout_rect). No Yo Frankie material, no bathroom.

import json
import math
import os
import sys
import traceback

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p3"))
import make_scenes_p2 as p2  # noqa: E402  (probe, lamps, materials)
import make_scenes_p3 as p3  # noqa: E402  (images, UV geometry)

# the game window and the orthographic view (Blender units across); the CI
# machines' screen is 1024x768, so the window is smaller than 1280x720
WIDTH, HEIGHT = 960, 540
ORTHO = 12.0
VIEW_H = ORTHO * HEIGHT / WIDTH

STOCK = "--stock" in sys.argv


def out_dir():
    argv = sys.argv
    path = os.path.abspath(argv[argv.index("--") + 1])
    os.makedirs(path, exist_ok=True)
    return path


def layout_rect(x, z, half):
    """The screen rectangle (left, top, right, bottom; pixels, top-left
    origin) of a square of half-size `half` at (x, z), shrunk to its inner
    80% so edges do not count."""
    h = half * 0.8
    l = (x - h) / ORTHO + 0.5
    r = (x + h) / ORTHO + 0.5
    t = 0.5 - (z + h) / VIEW_H
    b = 0.5 - (z - h) / VIEW_H
    return [int(round(l * WIDTH)), int(round(t * HEIGHT)), int(round(r * WIDTH)), int(round(b * HEIGHT))]


def base(name):
    scene = p2.empty_scene()
    scene.name = name
    scene.game_settings.resolution_x = WIDTH
    scene.game_settings.resolution_y = HEIGHT
    if not STOCK:
        scene.game_settings.material_mode = 'GLSL'
    scene.render.resolution_x = WIDTH
    scene.render.resolution_y = HEIGHT
    scene.render.resolution_percentage = 100
    scene.render.use_antialiasing = False
    w = bpy.data.worlds.new("World")
    w.horizon_color = (0.05, 0.05, 0.08)
    w.zenith_color = (0.05, 0.05, 0.08)
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


def facing_plane(scene, name, x, z, size, ma, uv=True):
    """A square facing the camera (-Y), with a 0..1 UV map unless uv=False."""
    rot = (math.radians(90.0), 0.0, 0.0)
    if uv:
        return p3.uv_plane(scene, name, size, (x, 0.0, z), rot, ma)
    return p2.plane(scene, name, size, (x, 0.0, z), rot, ma)


def classic_lamps():
    """P8.9: new lamps are physical; the Part A scenes were made (and checked)
    with each lamp's own falloff, so they keep it"""
    if not STOCK:
        for la in bpy.data.lamps:
            la["bge_physical"] = 0


def save(scene, path, physical=None, lamps_physical=False):
    """physical: None keeps each material's own shading (new materials are
    physical), False makes all Classic; lamps_physical: False makes every lamp
    use its own falloff, True keeps each lamp's model"""
    if physical is False:
        for ma in bpy.data.materials:
            ma["bge_physical"] = 0
    if not lamps_physical:
        classic_lamps()
    p2.add_probe(scene)
    scene.update()
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, compress=False)
    print("BGE-Remix: wrote", path)


def classic(name, color=(1.0, 1.0, 1.0), shadeless=False, spec=0.0):
    ma = p2.material(name, color, ref=1.0, hard=50)
    ma.specular_intensity = spec
    ma.use_shadeless = shadeless
    if not STOCK:
        ma["bge_physical"] = 0
    return ma


# ---------------------------------------------------------------------------
# proc_textures

# (label, texture type, settings): one panel each
PROCEDURALS = [
    ("blend_lin", 'BLEND', {"progression": 'LINEAR'}),
    ("blend_quad", 'BLEND', {"progression": 'QUADRATIC'}),
    ("blend_ease", 'BLEND', {"progression": 'EASING'}),
    ("blend_diag", 'BLEND', {"progression": 'DIAGONAL'}),
    ("blend_sphere", 'BLEND', {"progression": 'SPHERICAL'}),
    ("blend_halo", 'BLEND', {"progression": 'QUADRATIC_SPHERE'}),
    ("blend_radial", 'BLEND', {"progression": 'RADIAL', "use_flip_axis": 'VERTICAL'}),
    ("clouds", 'CLOUDS', {"noise_scale": 0.3, "noise_depth": 2}),
    ("clouds_color", 'CLOUDS', {"cloud_type": 'COLOR', "noise_basis": 'ORIGINAL_PERLIN', "noise_scale": 0.4}),
    ("clouds_hard", 'CLOUDS', {"noise_type": 'HARD_NOISE', "noise_basis": 'IMPROVED_PERLIN', "noise_scale": 0.3}),
    ("clouds_band", 'CLOUDS', {"noise_scale": 0.3, "colorband": True}),
    ("clouds_bricon", 'CLOUDS', {"noise_scale": 0.3, "intensity": 1.3, "contrast": 1.6}),
    ("wood_bands", 'WOOD', {"wood_type": 'BANDS', "noise_basis_2": 'SIN'}),
    ("wood_rings", 'WOOD', {"wood_type": 'RINGS', "noise_basis_2": 'SAW'}),
    ("wood_bandnoise", 'WOOD', {"wood_type": 'BANDNOISE', "noise_basis_2": 'TRI', "turbulence": 4.0}),
    ("wood_ringnoise", 'WOOD', {"wood_type": 'RINGNOISE', "turbulence": 3.0, "noise_scale": 0.4}),
    ("marble_soft", 'MARBLE', {"marble_type": 'SOFT', "noise_scale": 0.4}),
    ("marble_sharp", 'MARBLE', {"marble_type": 'SHARP', "noise_basis_2": 'SAW', "noise_scale": 0.4}),
    ("marble_sharper", 'MARBLE', {"marble_type": 'SHARPER', "noise_basis_2": 'TRI', "noise_scale": 0.4}),
    ("magic_2", 'MAGIC', {"noise_depth": 2, "turbulence": 5.0}),
    ("magic_5", 'MAGIC', {"noise_depth": 5, "turbulence": 3.0}),
    ("stucci_plastic", 'STUCCI', {"stucci_type": 'PLASTIC', "noise_scale": 0.2, "turbulence": 8.0}),
    ("stucci_wallin", 'STUCCI', {"stucci_type": 'WALL_IN', "noise_scale": 0.2, "turbulence": 8.0}),
    ("noise", 'NOISE', {}),
    ("musgrave_multi", 'MUSGRAVE', {"musgrave_type": 'MULTIFRACTAL', "noise_scale": 0.4}),
    ("musgrave_ridged", 'MUSGRAVE', {"musgrave_type": 'RIDGED_MULTIFRACTAL', "noise_scale": 0.4, "offset": 1.0,
                                      "gain": 2.0}),
    ("musgrave_hybrid", 'MUSGRAVE', {"musgrave_type": 'HYBRID_MULTIFRACTAL', "noise_scale": 0.4, "offset": 0.7}),
    ("musgrave_fbm", 'MUSGRAVE', {"musgrave_type": 'FBM', "noise_scale": 0.4, "octaves": 2.5}),
    ("musgrave_hetero", 'MUSGRAVE', {"musgrave_type": 'HETERO_TERRAIN', "noise_scale": 0.4, "offset": 0.5}),
    ("voronoi_f1", 'VORONOI', {"noise_scale": 0.3}),
    ("voronoi_color", 'VORONOI', {"noise_scale": 0.3, "color_mode": 'POSITION_OUTLINE',
                                  "distance_metric": 'MANHATTAN'}),
    ("distnoise", 'DISTORTED_NOISE', {"noise_scale": 0.4, "distortion": 2.0}),
]

# the coordinates some panels use instead of UV (mapping as a render does it)
COORDS = {
    "wood_ringnoise": ("ORCO", None),
    "marble_sharp": ("OBJECT", "Origin"),
    "magic_5": ("GLOBAL", None),
    "musgrave_fbm": ("UV", "swizzle"),
    "voronoi_f1": ("UV", "size"),
}

BUMPS = [("bump_clouds", 'CLOUDS', {"noise_scale": 0.25}),
         ("bump_marble", 'MARBLE', {"noise_scale": 0.4}),
         ("bump_wood", 'WOOD', {"wood_type": 'RINGNOISE', "turbulence": 3.0}),
         ("bump_musgrave", 'MUSGRAVE', {"noise_scale": 0.4}),
         ("bump_voronoi", 'VORONOI', {"noise_scale": 0.3}),
         ("bump_stucci", 'STUCCI', {"noise_scale": 0.2, "turbulence": 8.0})]


def procedural(name, kind, settings):
    tex = bpy.data.textures.new(name, kind)
    for key, value in settings.items():
        if key == "colorband":
            tex.use_color_ramp = True
            el = tex.color_ramp.elements
            el[0].color = (0.1, 0.2, 0.6, 1.0)
            el[1].color = (1.0, 0.8, 0.2, 1.0)
            el.new(0.5).color = (0.9, 0.1, 0.1, 1.0)
        else:
            setattr(tex, key, value)
    return tex


def proc_material(name, tex, shadeless=True, bump=False, coords=("UV", None), origin=None):
    ma = classic("M_" + name, (1.0, 1.0, 1.0), shadeless=shadeless)
    slot = ma.texture_slots.add()
    slot.texture = tex
    kind, extra = coords
    slot.texture_coords = kind
    if kind == 'OBJECT':
        slot.object = origin
    if extra == "swizzle":
        slot.mapping_x, slot.mapping_y, slot.mapping_z = 'Y', 'X', 'Z'
    if extra == "size":
        slot.scale = (2.0, 1.5, 1.0)
        slot.offset = (0.3, -0.2, 0.0)
    if bump:
        slot.use_map_color_diffuse = False
        slot.use_map_normal = True
        slot.normal_factor = 1.0
    else:
        slot.use_map_color_diffuse = True
        slot.diffuse_color_factor = 1.0
        slot.color = (0.0, 0.0, 0.0)
        slot.blend_type = 'MIX'
    return ma


def make_proc_textures(folder, E):
    scene = base("Procedurals")
    origin = bpy.data.objects.new("Origin", None)
    origin.location = (0.3, 0.0, -0.4)
    origin.scale = (0.8, 0.8, 0.8)
    scene.objects.link(origin)
    panels = []
    cols, size, step = 8, 1.2, 1.38
    for i, (name, kind, settings) in enumerate(PROCEDURALS):
        r, c = divmod(i, cols)
        x = (c - (cols - 1) / 2.0) * step
        z = 2.75 - r * step
        tex = procedural("T_" + name, kind, settings)
        ma = proc_material(name, tex, coords=COORDS.get(name, ("UV", None)), origin=origin)
        facing_plane(scene, "P_" + name, x, z, size, ma)
        panels.append({"name": name, "kind": kind, "rect": layout_rect(x, z, size / 2.0), "noise": kind == 'NOISE'})
    # the bump row: lit, white, a texture mapped to Normal
    bumps = []
    for i, (name, kind, settings) in enumerate(BUMPS):
        x = (i - (len(BUMPS)) / 2.0) * step
        z = 2.75 - 4 * step
        tex = procedural("T_" + name, kind, settings)
        ma = proc_material(name, tex, shadeless=False, bump=True)
        facing_plane(scene, "P_" + name, x, z, size, ma)
        bumps.append({"name": name, "rect": layout_rect(x, z, size / 2.0)})
    # a panel without a UV map (reported for Remix, which bakes on UV maps)
    tex = procedural("T_no_uv", 'CLOUDS', {"noise_scale": 0.3})
    ma = proc_material("no_uv", tex)
    x = (len(BUMPS) - len(BUMPS) / 2.0) * step
    facing_plane(scene, "P_no_uv", x, 2.75 - 4 * step, size, ma, uv=False)
    sun = p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 0.0), (math.radians(60.0), 0.0, math.radians(-30.0)),
                  energy=1.0)
    sun.data.use_specular = False
    scene["bge_remix"] = 1
    save(scene, os.path.join(folder, "proc_textures.blend"), physical=False)
    E["proc_textures"] = {"panels": panels, "bumps": bumps, "no_uv": "M_no_uv",
                          "materials": ["M_" + n for n, _k, _s in PROCEDURALS] + ["M_" + n for n, _k, _s in BUMPS]}


ANIMATOR = r'''
# changes the texture while playing (embedded: Blender's own data), 1.5 and
# 4 seconds in (Remix looks for changes a few times a second); Remix bakes
# it again each time
import time
import bge
import bpy
g = bge.logic.globalDict
now = time.perf_counter()
t0 = g.setdefault("p8_anim_t0", now)
step = g.get("p8_anim_step", 0)
if step < 2 and now - t0 >= (1.5, 4.0)[step]:
    tex = bpy.data.textures["T_animated"]
    tex.noise_scale = (0.12, 0.5)[step]
    g["p8_anim_step"] = step + 1
'''


def make_proc_animated(folder):
    scene = base("Animated")
    tex = procedural("T_animated", 'CLOUDS', {"noise_scale": 0.3})
    ma = proc_material("animated", tex)
    facing_plane(scene, "P_animated", 0.0, 0.0, 4.0, ma)
    ob = bpy.data.objects.new("Animator", None)
    scene.objects.link(ob)
    logic(scene, ob, "p8_animator.py", ANIMATOR)
    scene["bge_remix"] = 1
    save(scene, os.path.join(folder, "proc_animated.blend"), physical=False)


def logic(scene, ob, name, body):
    scene.objects.active = ob
    text = bpy.data.texts.new(name)
    text.write(body)
    bpy.ops.logic.sensor_add(type='ALWAYS', object=ob.name)
    bpy.ops.logic.controller_add(type='PYTHON', object=ob.name)
    sensor = ob.game.sensors[-1]
    controller = ob.game.controllers[-1]
    sensor.use_pulse_true_level = True
    controller.mode = 'SCRIPT'
    controller.text = text
    sensor.link(controller)


# ---------------------------------------------------------------------------
# pbr_spheres and pbr_classic

METALLIC = (0.0, 0.25, 0.5, 0.75, 1.0)
ROUGHNESS = (0.05, 0.25, 0.5, 0.75, 1.0)
BASE = (0.8, 0.35, 0.1)


def sphere(scene, name, x, z, ma, radius=0.5):
    ob = p3.sphere(scene, name, (x, 0.0, z), ma, radius=radius)
    return ob


def physical(name, base_col, metallic=0.0, roughness=0.5, **extra):
    ma = bpy.data.materials.new(name)
    ma.diffuse_color = base_col
    ma["bge_physical"] = 1
    ma["bge_metallic"] = metallic
    ma["bge_roughness"] = roughness
    for k, v in extra.items():
        ma["bge_" + k] = v
    return ma


def classic_like(name, base_col, metallic, roughness):
    """the Classic material nearest to a physical one, made by hand"""
    ma = classic(name, base_col, spec=0.5 * (1.0 - metallic) + metallic)
    ma.diffuse_intensity = 1.0 - metallic
    ma.specular_hardness = int(max(1, min(511, 2.0 / max(roughness, 0.05) ** 4 - 2.0)))
    ma.specular_color = tuple(1.0 * (1.0 - metallic) + c * metallic for c in base_col)
    return ma


def pbr_scene(folder, name, physical_shading, E):
    scene = base("PBR")
    scene.world.horizon_color = (0.25, 0.27, 0.3)
    scene.world.zenith_color = (0.05, 0.08, 0.2)
    scene.world.ambient_color = (0.05, 0.05, 0.05)
    spheres = []
    step = 1.25
    for r, m in enumerate(METALLIC):
        for c, rough in enumerate(ROUGHNESS):
            x = (c - 3.0) * step
            z = 2.5 - r * step
            label = "m%d_r%d" % (r, c)
            if physical_shading:
                ma = physical("S_" + label, BASE, m, rough)
            else:
                ma = classic_like("S_" + label, BASE, m, rough)
            sphere(scene, label, x, z, ma)
            spheres.append({"name": label, "material": ma.name, "metallic": m, "roughness": rough,
                            "rect": layout_rect(x, z, 0.5 * 0.7)})
    extras = []
    x = 3.0 * step
    if physical_shading:
        glass = physical("S_glass", (0.85, 0.95, 1.0), 0.0, 0.05, transmission=1.0, ior=1.5)
        glass.game_settings.alpha_blend = 'ALPHA'
        sss = physical("S_subsurface", (0.9, 0.6, 0.5), 0.0, 0.5, subsurface=1.0)
        sss["bge_subsurface_radius"] = [1.0, 0.4, 0.2]
        emit = physical("S_emissive", (0.2, 0.2, 0.2), 0.0, 0.5, emission_strength=2.0)
        emit["bge_emission"] = [1.0, 0.5, 0.1]
    else:
        glass = classic("S_glass", (0.85, 0.95, 1.0), spec=0.5)
        glass.use_transparency = True
        glass.transparency_method = 'RAYTRACE'
        glass.alpha = 0.1
        glass.raytrace_transparency.ior = 1.5
        sss = classic("S_subsurface", (0.9, 0.6, 0.5), spec=0.3)
        sss.subsurface_scattering.use = True
        emit = classic("S_emissive", (1.0, 0.5, 0.1))
        emit.emit = 2.0
    for i, ma in enumerate((glass, sss, emit)):
        z = 2.5 - i * step
        sphere(scene, ma.name[2:], x, z, ma)
        extras.append({"name": ma.name[2:], "material": ma.name, "rect": layout_rect(x, z, 0.35)})
    sun = p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 5.0), (math.radians(50.0), 0.0, math.radians(-35.0)),
                  energy=1.0)
    sun.data.shadow_soft_size = 0.2
    point = p2.lamp(scene, "Fill", 'POINT', (-4.0, -6.0, -2.0), energy=0.4, distance=15.0)
    scene.render.use_raytrace = True
    scene["bge_remix"] = 1
    save(scene, os.path.join(folder, name + ".blend"))
    E[name] = {"spheres": spheres, "extras": extras, "base": list(BASE)}


# ---------------------------------------------------------------------------
# classic_remix

def make_classic_remix(folder, E):
    scene = base("ClassicRemix")
    items = []
    step = 1.6
    chrome = classic("C_chrome", (0.8, 0.8, 0.8), spec=0.5)
    chrome.raytrace_mirror.use = True
    chrome.raytrace_mirror.reflect_factor = 1.0
    chrome.raytrace_mirror.fresnel = 0.0
    chrome.raytrace_mirror.gloss_factor = 0.9
    chrome.mirror_color = (0.9, 0.85, 0.7)
    items.append(("chrome", chrome, {"type": "opaque", "metallic_min": 0.99, "roughness": 0.1}))
    glass = classic("C_glass", (0.3, 0.5, 1.0), spec=0.5)
    glass.use_transparency = True
    glass.transparency_method = 'RAYTRACE'
    glass.alpha = 0.1
    glass.raytrace_transparency.ior = 1.33
    glass.raytrace_transparency.filter = 0.5
    items.append(("glass", glass, {"type": "translucent", "ior": 1.33}))
    sss = classic("C_subsurface", (0.9, 0.7, 0.6), spec=0.3)
    sss.subsurface_scattering.use = True
    sss.subsurface_scattering.radius = (1.0, 0.5, 0.25)
    sss.subsurface_scattering.scale = 0.2
    items.append(("subsurface", sss, {"type": "opaque", "subsurface": 1}))
    ztransp = classic("C_ztransp", (0.2, 0.8, 0.3), spec=0.3)
    ztransp.use_transparency = True
    ztransp.transparency_method = 'Z_TRANSPARENCY'
    ztransp.alpha = 0.4
    items.append(("ztransp", ztransp, {"type": "opaque", "blend": 1, "opacity": 0.4}))
    add = classic("C_add", (1.0, 0.6, 0.2), spec=0.0)
    add.game_settings.alpha_blend = 'ADD'
    items.append(("add", add, {"type": "opaque", "blendtype": 8}))
    invisible = classic("C_invisible", (1.0, 0.0, 1.0))
    invisible.game_settings.invisible = True
    items.append(("invisible", invisible, {"absent": True}))
    expect = []
    for i, (label, ma, values) in enumerate(items):
        x = (i - (len(items) - 1) / 2.0) * step
        sphere(scene, label, x, 0.0, ma, radius=0.6)
        e = {"name": label, "material": ma.name, "rect": layout_rect(x, 0.0, 0.4)}
        e.update(values)
        expect.append(e)
    p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 5.0), (math.radians(50.0), 0.0, math.radians(-35.0)), energy=1.0)
    scene["bge_remix"] = 1
    save(scene, os.path.join(folder, "classic_remix.blend"), physical=False)
    E["classic_remix"] = expect


# ---------------------------------------------------------------------------
# glsl_features and fallback (Classic, as stock 2.79 GLSL draws them)

API_PROBE = r'''
# BGE-Remix P8 (T8.6): the old Python calls still run
import json, os
import bge
g = bge.logic.globalDict
n = g.get("p8_api", 0) + 1
g["p8_api"] = n
path = os.environ.get("BGE_REMIX_P8_API")
# only when asked: the lamp changes would change the picture other tests read
if path and n == 20:
    r = {}
    def check(name, fn):
        try:
            r[name] = {"ok": True, "value": fn()}
        except Exception as ex:
            r[name] = {"ok": False, "value": repr(ex)}
    check("getMaterialMode", lambda: bge.render.getMaterialMode())
    check("setMaterialMode", lambda: bge.render.setMaterialMode(bge.render.KX_BLENDER_MULTITEX_MATERIAL))
    check("getMaterialMode after", lambda: bge.render.getMaterialMode())
    check("GLSL constant", lambda: bge.render.KX_BLENDER_GLSL_MATERIAL)
    check("setGLSLMaterialSetting", lambda: bge.render.setGLSLMaterialSetting("lights", True))
    check("getGLSLMaterialSetting", lambda: bge.render.getGLSLMaterialSetting("lights"))
    sc = bge.logic.getCurrentScene()
    ob = sc.objects["NodeBall"]
    check("getShader", lambda: str(type(ob.meshes[0].materials[0].getShader())))
    check("bge.texture", lambda: str(bge.texture.ImageFFmpeg))
    lamp = sc.objects["Spot"]
    def lamp_settings():
        lamp.energy = 2.0
        lamp.color = [1.0, 0.5, 0.5]
        lamp.distance = 20.0
        lamp.spotsize = 50.0
        lamp.spotblend = 0.3
        lamp.type = lamp.SPOT
        return [lamp.energy, list(lamp.color), lamp.distance, lamp.spotsize, lamp.spotblend, lamp.type]
    check("lamp settings", lamp_settings)
    with open(path, "w") as f:
        json.dump(r, f, indent=1, default=str)
'''


def make_glsl_features(folder, E):
    scene = base("GLSL")
    scene.world.horizon_color = (0.2, 0.22, 0.25)
    scene.world.mist_settings.use_mist = True
    scene.world.mist_settings.start = 15.0
    scene.world.mist_settings.depth = 15.0
    scene.world.light_settings.use_environment_light = True
    scene.world.light_settings.environment_energy = 0.3
    floor = classic("G_floor", (0.6, 0.6, 0.6))
    p2.plane(scene, "Floor", 14.0, (0.0, 2.0, -2.0), (0.0, 0.0, 0.0), floor)
    rects = {}
    # a node material (Blender Internal nodes: Material, then a Mix with a colour)
    nm = classic("G_nodes", (0.8, 0.3, 0.3), spec=0.3)
    nm.use_nodes = True
    nt = nm.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    mn = nt.nodes.new("ShaderNodeMaterial")
    mn.material = classic("G_nodes_inner", (0.2, 0.4, 0.9), spec=0.3)
    mix = nt.nodes.new("ShaderNodeMixRGB")
    mix.inputs[0].default_value = 0.5
    mix.inputs[2].default_value = (1.0, 0.9, 0.2, 1.0)
    outn = nt.nodes.new("ShaderNodeOutput")
    nt.links.new(mn.outputs["Color"], mix.inputs[1])
    nt.links.new(mix.outputs[0], outn.inputs["Color"])
    sphere(scene, "NodeBall", -4.5, 0.0, nm, radius=0.8)
    rects["nodes"] = layout_rect(-4.5, 0.0, 0.5)
    # a ramp
    ramp = classic("G_ramp", (1.0, 1.0, 1.0), spec=0.3)
    ramp.use_diffuse_ramp = True
    el = ramp.diffuse_ramp.elements
    el[0].color = (0.1, 0.0, 0.3, 1.0)
    el[1].color = (1.0, 0.9, 0.3, 1.0)
    sphere(scene, "RampBall", -2.25, 0.0, ramp, radius=0.8)
    rects["ramp"] = layout_rect(-2.25, 0.0, 0.5)
    # a normal map
    nmap = classic("G_normalmap", (0.7, 0.7, 0.75), spec=0.5)
    img = p3.image("G_bumps", p3.bumps, non_color=True)
    p3.bi_slot(nmap, img, normal=True)
    ob = p3.textured_cube(scene, "NormalCube", 1.4, (0.0, 0.0, 0.0), nmap)
    ob.rotation_euler = (0.3, 0.0, 0.6)
    rects["normalmap"] = layout_rect(0.0, 0.0, 0.5)
    # a shadow: a box above the floor under a shadow-casting spot
    caster = classic("G_caster", (0.9, 0.9, 0.9), spec=0.2)
    p2.cube(scene, "Caster", 0.8, (2.4, 1.0, -0.6), caster)
    # environment light and mist act on everything; the far ball shows the mist
    far = classic("G_far", (0.9, 0.9, 0.9))
    b = sphere(scene, "FarBall", 4.6, 0.0, far, radius=0.8)
    b.location.y = 18.0
    rects["mist"] = layout_rect(4.6, 0.0, 0.4)
    spot = p2.lamp(scene, "Spot", 'SPOT', (2.4, -2.0, 4.0), energy=2.0, distance=20.0)
    p2.look_at(spot, (2.4, 1.0, -2.0))
    spot.data.shadow_method = 'BUFFER_SHADOW'
    spot.data.use_shadow = True
    api = bpy.data.objects.new("ApiProbe", None)
    scene.objects.link(api)
    logic(scene, api, "p8_api.py", API_PROBE)
    save(scene, os.path.join(folder, "glsl_features.blend"), physical=False)
    E["glsl_features"] = rects


def make_fallback(folder, E):
    scene = base("Fallback")
    img = p3.image("F_checker", p3.checker)
    rects = {}
    for i, name in enumerate(("FailMe", "Neighbour")):
        ma = classic(name, (1.0, 1.0, 1.0), shadeless=True)
        p3.bi_slot(ma, img, color=True)
        x = -2.5 + 5.0 * i
        facing_plane(scene, "P_" + name, x, 0.0, 4.0, ma)
        rects[name] = layout_rect(x, 0.0, 2.0)
    save(scene, os.path.join(folder, "fallback.blend"), physical=False)
    E["fallback"] = rects


# ---------------------------------------------------------------------------
# mt_features and mt_bench (official 2.79b, Multitexture)

MT_SCRIPT = r'''
# BGE-Remix P8 (T8.2): Multitexture's scripted features
import json, os
import bge
g = bge.logic.globalDict
n = g.get("p8_mt", 0) + 1
g["p8_mt"] = n
sc = bge.logic.getCurrentScene()
if n == 5:
    # setBlending without a custom shader: added to what is behind
    ob = sc.objects["Blended"]
    ob.meshes[0].materials[0].setBlending(bge.logic.BL_ONE, bge.logic.BL_ONE)
if n == 60:
    path = os.environ.get("BGE_REMIX_P8_SHOT_A")
    if path:
        bge.render.makeScreenshot(path)
if n == 70:
    # a lamp's type changed while playing
    lamp = sc.objects["Changing"]
    g["p8_mt_type_before"] = lamp.type
    lamp.type = lamp.SUN
    g["p8_mt_type_after"] = lamp.type
if n == 120:
    path = os.environ.get("BGE_REMIX_P8_SHOT_B")
    if path:
        bge.render.makeScreenshot(path)
    out = os.environ.get("BGE_REMIX_P8_MT")
    if out:
        with open(out, "w") as f:
            json.dump({"mode": bge.render.getMaterialMode(), "type_before": g.get("p8_mt_type_before"),
                       "type_after": g.get("p8_mt_type_after")}, f)
'''


def font_image():
    """a stand-in bitmap font: light glyph cells on a dark ground"""
    def px(x, y):
        cx, cy = (x * 16.0) % 1.0, (y * 16.0) % 1.0
        on = 0.2 < cx < 0.8 and 0.15 < cy < 0.85
        return (1.0, 1.0, 1.0, 1.0) if on else (0.0, 0.0, 0.0, 0.0)
    return p3.image("MT_font", px)


def mt_material(name, color, shadeless=True):
    ma = p2.material(name, color, ref=1.0)
    ma.use_shadeless = shadeless
    ma.specular_intensity = 0.0
    return ma


def make_mt_features(folder, E):
    scene = base("Multitexture")
    scene.game_settings.material_mode = 'MULTITEXTURE'
    rects = {}
    # Face Textures: the UV editor's image per face
    checker = p3.image("MT_checker", p3.checker)
    ma = mt_material("MT_face", (1.0, 1.0, 1.0))
    ma.use_face_texture = True
    ob = facing_plane(scene, "FaceTextured", -4.5, 1.5, 2.2, ma)
    for d in ob.data.uv_textures[0].data:
        d.image = checker
    rects["face_texture"] = layout_rect(-4.5, 1.5, 1.1)
    # bitmap text (a Text property, a font image as the face image)
    font = font_image()
    tm = mt_material("MT_text", (1.0, 1.0, 1.0))
    tm.use_face_texture = True
    tm.use_face_texture_alpha = True
    tm.game_settings.text = True
    tm.game_settings.alpha_blend = 'ALPHA'
    tob = facing_plane(scene, "Text", -1.5, 1.5, 1.2, tm)
    for d in tob.data.uv_textures[0].data:
        d.image = font
    bpy.context.scene.objects.active = tob
    bpy.ops.object.game_property_new(type='STRING', name="Text")
    tob.game.properties["Text"].value = "BGE"
    rects["text"] = layout_rect(-1.5, 1.5, 0.6)
    # setBlending from a script over a grey card
    card = mt_material("MT_card", (0.4, 0.4, 0.4))
    p2.plane(scene, "Card", 2.6, (1.5, 2.0, 1.5), (math.radians(90.0), 0.0, 0.0), card)
    bm = mt_material("MT_blended", (0.4, 0.2, 0.1))
    facing_plane(scene, "Blended", 1.5, 1.5, 1.6, bm)
    rects["setblending"] = layout_rect(1.5, 1.5, 0.8)
    # object colours (Multitexture takes them on every material)
    for i, (name, col) in enumerate((("Red", (1.0, 0.2, 0.2, 1.0)), ("Green", (0.2, 1.0, 0.2, 1.0)))):
        om = mt_material("MT_objcol_" + name, (1.0, 1.0, 1.0))
        ob = facing_plane(scene, "ObjCol" + name, 4.0 + 1.3 * i, 1.5, 1.1, om)
        ob.color = col
        rects["object_color_" + name.lower()] = layout_rect(4.0 + 1.3 * i, 1.5, 0.55)
    # a texture mapped to alpha (turns on alpha blending in Multitexture)
    grad = p3.image("MT_gradient", lambda x, y: (0.9, 0.9, 0.2, x))
    am = mt_material("MT_alpha", (1.0, 1.0, 1.0))
    slot = p3.bi_slot(am, grad, color=True)
    slot.use_map_alpha = True
    am.use_transparency = True
    am.alpha = 0.0
    p2.plane(scene, "AlphaBack", 2.6, (-4.5, 2.0, -1.5), (math.radians(90.0), 0.0, 0.0),
             mt_material("MT_alpha_back", (0.1, 0.4, 0.8)))
    facing_plane(scene, "AlphaMapped", -4.5, -1.5, 2.2, am)
    rects["alpha_texture"] = layout_rect(-4.5, -1.5, 1.1)
    # lit objects: hemi and area lamps, and a lamp a script turns into a sun
    lit = p2.material("MT_lit", (0.8, 0.8, 0.8), ref=0.8)
    for i, x in enumerate((-1.0, 1.5, 4.0)):
        ob = p3.sphere(scene, "Lit%d" % i, (x, 0.0, -1.5), lit, radius=0.8)
        rects["lit_%d" % i] = layout_rect(x, -1.5, 0.45)
    p2.lamp(scene, "Hemi", 'HEMI', (-1.0, -3.0, 1.0), (math.radians(60.0), 0.0, 0.0), energy=0.3)
    area = p2.lamp(scene, "Area", 'AREA', (1.5, -3.0, 0.0), energy=1.0, distance=10.0)
    p2.look_at(area, (1.5, 0.0, -1.5))
    changing = p2.lamp(scene, "Changing", 'SPOT', (4.0, -3.0, 0.0), energy=1.5, distance=10.0)
    p2.look_at(changing, (6.0, 0.0, -1.5))
    driver = bpy.data.objects.new("MTScript", None)
    scene.objects.link(driver)
    logic(scene, driver, "p8_mt.py", MT_SCRIPT)
    scene.game_settings.material_mode = 'MULTITEXTURE'
    classic_lamps()
    p2.add_probe(scene)
    scene.update()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "mt_features.blend"), check_existing=False,
                                compress=False)
    E["mt_features"] = rects


def make_mt_bench(folder):
    scene = base("Bench")
    scene.game_settings.material_mode = 'MULTITEXTURE'
    img = p3.image("B_checker", p3.checker)
    ma = p2.material("B_lit", (0.8, 0.8, 0.8), ref=0.8)
    p3.bi_slot(ma, img, color=True)
    mesh = None
    n = 0
    for i in range(50):
        for j in range(40):
            x = -5.8 + i * (11.6 / 49.0)
            z = -3.2 + j * (6.4 / 39.0)
            ob = p2.cube(scene, "B%04d" % n, 0.12, (x, 0.0, z), ma, mesh=mesh)
            if mesh is None:
                mesh = ob.data
                mesh.uv_textures.new("UVMap")
            ob.rotation_euler = (0.4 + 0.01 * n, 0.3, 0.2)
            n += 1
    for k in range(8):
        a = 2.0 * math.pi * k / 8.0
        p2.lamp(scene, "L%d" % k, 'POINT', (5.0 * math.cos(a), -3.0, 3.0 * math.sin(a)), energy=0.4,
                distance=12.0)
    scene.game_settings.material_mode = 'MULTITEXTURE'
    classic_lamps()
    p2.add_probe(scene)
    scene.update()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "mt_bench.blend"), check_existing=False,
                                compress=False)


# ---------------------------------------------------------------------------
# Part B: lamps and shadows. The cells of lamps and shadows are tiles facing
# the camera (the "floor"; "up" is toward the camera, -Y), each on its own
# layer with its own lamps ("This Layer Only"), so each lamp lights and
# shadows its tile only.

CELL_X = (-5.0, -3.0, -1.0, 1.0, 3.0, 5.0)
CELL_Z = (2.1, 0.0, -2.1)
TILE = 1.9
LIGHT_SCALE = 0.125  # Remix's bge_remix_light_scale when the scene has none


def cell_center(i):
    return CELL_X[i % 6], CELL_Z[i // 6]


def screen(x, z):
    """a point of the tiles' plane on screen (pixels, top-left origin)"""
    return [(x / ORTHO + 0.5) * WIDTH, (0.5 - z / VIEW_H) * HEIGHT]


def on_layer(ob, k):
    ob.layers = [i == k for i in range(20)]


def tile(scene, name, i, ma):
    x, z = cell_center(i)
    ob = facing_plane(scene, name, x, z, TILE, ma, uv=False)
    on_layer(ob, i)
    return ob


def cell_lamp(scene, name, kind, i, offset, target=None, energy=1.0, distance=1.5, size=0.1, physical=True,
              shadow=False, color=(1.0, 1.0, 1.0)):
    """a lamp of cell i at offset (x, up, z) from the tile's centre, aimed at
    the tile (or target, an offset too), lighting that cell only"""
    x, z = cell_center(i)
    ob = p2.lamp(scene, name, kind, (x + offset[0], -offset[1], z + offset[2]), energy=energy, color=color,
                 distance=distance, soft=size)
    t = target or (0.0, 0.0, 0.0)
    p2.look_at(ob, (x + t[0], -t[1], z + t[2]))
    la = ob.data
    la.use_own_layer = True
    if hasattr(la, "use_shadow"):  # not on hemi lamps
        la.use_shadow = shadow
    if not STOCK:
        la["bge_physical"] = 1 if physical else 0
    on_layer(ob, i)
    return ob


def remix_expect(ob):
    """what the fake runtime's log should show for a lamp (remix_light.cc)"""
    la = ob.data
    E, D, s = la.energy, la.distance, LIGHT_SCALE
    c = list(la.color)
    phys = bool(la.get("bge_physical", 0)) and la.type in ('POINT', 'SPOT')
    e = {"name": ob.name}
    if la.type == 'SUN':
        e.update(type="distant", angle=float(la.get("bge_sun_angle", 0.53)), k=math.pi * E * s)
    elif la.type == 'AREA':
        sx, sy = la.size, (la.size_y if la.shape == 'RECTANGLE' else la.size)
        e.update(type="rect", size=[sx, sy], k=math.pi * E * s * D / (sx * sy))
    elif la.type == 'HEMI':
        e.update(type="dome")
    else:
        r = max(la.shadow_soft_size, 0.02)
        k = E * s * D * D / (r * r) if phys else E * s * D / (r * r)
        if phys and la.type == 'SPOT':
            e.update(type="disk", radii=[r, r], k=k, cone=math.degrees(la.spot_size) / 2.0)
        else:
            e.update(type="sphere", radius=r, k=k)
            if la.type == 'SPOT':
                e["cone"] = math.degrees(la.spot_size) / 2.0
    if la.type == 'SPOT' and la.use_halo:
        e["volumetric"] = 1.0 + la.halo_intensity
    if "k" in e:
        e["radiance"] = [round(v * e["k"], 5) for v in c]
    return e


CHANGE_SCRIPT = r'''
import bge
own = bge.logic.getCurrentController().owner
own["f"] = own.get("f", 0) + 1
if own["f"] == 3:
    sc = bge.logic.getCurrentScene()
    lamp, like = sc.objects["Changing"], sc.objects["L_point_phys_small"]
    lamp.energy = like.energy
    lamp.color = like.color
    lamp.size = like.size
    lamp.distance = like.distance
    print("P8-LAMP-CHANGED")
'''


def make_lamps(folder, E):
    scene = base("Lamps")
    scene.layers = [True] * 20
    floor = classic("L_floor", (0.8, 0.8, 0.8))
    floor.diffuse_intensity = 0.8
    cells = {}
    lamps = []

    def cell(i, name, *lamp_args, **lamp_kw):
        tile(scene, "T_" + name, i, floor)
        x, z = cell_center(i)
        cells[name] = {"cell": i, "rect": layout_rect(x, z, TILE / 2.0)}
        ob = cell_lamp(scene, *lamp_args, **lamp_kw)
        lamps.append(ob)
        return ob

    up = (0.0, 1.0, 0.0)
    la = cell(0, "point_classic_square", "L_point_classic_sq", 'POINT', 0, up, energy=0.8, physical=False,
              size=0.2).data
    la.falloff_type = 'INVERSE_SQUARE'
    la = cell(1, "point_classic_linear", "L_point_classic_lin", 'POINT', 1, up, energy=0.8, physical=False,
              size=0.2).data
    la.falloff_type = 'INVERSE_LINEAR'
    cell(2, "point_physical_small", "L_point_phys_small", 'POINT', 2, up, energy=0.8, distance=1.0, size=0.1)
    cell(3, "point_physical_large", "L_point_phys_large", 'POINT', 3, up, energy=0.8, distance=1.0, size=0.6)
    for i, name, phys in ((4, "spot_classic", False), (5, "spot_physical", True)):
        la = cell(i, name, "L_" + name, 'SPOT', i, (0.0, 1.5, 0.0), energy=1.5, distance=2.0, physical=phys,
                  size=0.3).data
        la.spot_size = math.radians(70.0)
        la.spot_blend = 0.3
    la = cell(6, "sun", "L_sun", 'SUN', 6, (0.8, 1.4, 0.0), energy=0.7).data
    if not STOCK:
        la["bge_sun_angle"] = 2.0
    cell(7, "hemi", "L_hemi", 'HEMI', 7, up, energy=0.6)
    la = cell(8, "area_square", "L_area_square", 'AREA', 8, (0.0, 1.0, 0.0), energy=0.8, distance=1.5).data
    la.shape = 'SQUARE'
    la.size = 0.8
    la = cell(9, "area_rectangle", "L_area_rect", 'AREA', 9, (0.0, 1.0, 0.0), energy=0.8, distance=1.5).data
    la.shape = 'RECTANGLE'
    la.size = 1.2
    la.size_y = 0.4
    la = cell(10, "spot_halo", "L_spot_halo", 'SPOT', 10, (0.0, 1.5, 0.0), energy=1.5, distance=2.0,
              physical=False, size=0.3).data
    la.spot_size = math.radians(40.0)
    la.spot_blend = 0.3
    la.use_halo = True
    la.halo_intensity = 1.0
    # the halo's twin without it (the game draws the halo over the light)
    la = cell(15, "spot_no_halo", "L_spot_no_halo", 'SPOT', 15, (0.0, 1.5, 0.0), energy=1.5, distance=2.0,
              physical=False, size=0.3).data
    la.spot_size = math.radians(40.0)
    la.spot_blend = 0.3
    la = cell(11, "spot_texture", "L_spot_texture", 'SPOT', 11, (0.0, 1.5, 0.0), energy=2.0, distance=2.0,
              physical=False, size=0.3).data
    la.spot_size = math.radians(80.0)
    la.spot_blend = 0.1
    tex = bpy.data.textures.new("LampChecker", 'IMAGE')
    tex.image = p3.image("LampCheckerImage", lambda u, v: p3.checker(u, v, n=4, a=(1.0, 1.0, 1.0),
                                                                      b=(0.2, 0.2, 0.2)))
    slot = la.texture_slots.add()
    slot.texture = tex
    slot.texture_coords = 'VIEW'

    # a light group: the tile is lit by its group's lamp only
    group = bpy.data.groups.new("LightGroup")
    lg_mat = classic("L_floor_group", (0.8, 0.8, 0.8))
    lg_mat.diffuse_intensity = 0.8
    lg_mat.light_group = group
    tile(scene, "T_light_group", 12, lg_mat)
    x, z = cell_center(12)
    cells["light_group"] = {"cell": 12, "rect": layout_rect(x, z, TILE / 2.0)}
    a = cell_lamp(scene, "LG_in_group", 'POINT', 12, (-0.5, 0.8, 0.0), energy=0.6, physical=False, size=0.2)
    b = cell_lamp(scene, "LG_not_in_group", 'POINT', 12, (0.5, 0.8, 0.0), energy=0.6, physical=False, size=0.2)
    group.objects.link(a)
    lamps += [a, b]

    # an Only Shadow lamp (left out of Remix) next to an ordinary one
    cell(13, "only_shadow", "L_fill", 'POINT', 13, up, energy=0.8, distance=1.0, size=0.1)
    o = cell_lamp(scene, "L_only_shadow", 'SPOT', 13, (0.0, 1.5, 0.0), energy=1.0, distance=2.0, shadow=True)
    o.data.use_only_shadow = True
    lamps.append(o)

    # a lamp a script changes into the point_physical_small one's twin
    ch = cell(14, "changed_by_script", "Changing", 'POINT', 14, up, energy=0.2, distance=3.0, size=0.5,
              color=(0.2, 0.3, 1.0))
    driver = bpy.data.objects.new("LampScript", None)
    scene.objects.link(driver)
    logic(scene, driver, "p8_lamp_change.py", CHANGE_SCRIPT)

    E["lamps"] = {"cells": cells, "remix": [remix_expect(ob) for ob in lamps],
                  "only_shadow": "L_only_shadow", "changing": "Changing", "like": "L_point_phys_small",
                  "f12_skip": ["spot_halo", "changed_by_script"]}
    # lamps.blend with RTX Remix on (the fake runtime's log), lamps_gl.blend without
    scene["bge_remix"] = 1
    save(scene, os.path.join(folder, "lamps.blend"), physical=False, lamps_physical=True)
    scene["bge_remix"] = 0
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "lamps_gl.blend"), copy=True)


MOVE_SCRIPT = r'''
import bge, math
own = bge.logic.getCurrentController().owner
own["f"] = own.get("f", 0) + 1
f = own["f"]
sc = bge.logic.getCurrentScene()
start = bge.logic.globalDict.setdefault("p8_start", {})
for name in ("S_moving", "Mover", "Spinner"):
    ob = sc.objects.get(name)
    if ob is None:
        continue
    p = start.setdefault(name, list(ob.worldPosition))
    if name == "S_moving":
        ob.worldPosition = [p[0] + 0.3 * math.cos(f * 0.2), p[1], p[2] + 0.3 * math.sin(f * 0.2)]
    elif name == "Mover":
        ob.worldPosition = [p[0] + 0.3 * math.sin(f * 0.2), p[1], p[2]]
    else:
        ob.applyRotation([0.0, 0.0, 0.05], False)
'''


def post(scene, name, i, ma, offset=(0.0, 0.0), size=0.25, height=0.8):
    """a box standing on tile i (toward the camera)"""
    x, z = cell_center(i)
    ob = p2.cube(scene, name, size, (x + offset[0], -height / 2.0, z + offset[1]), ma)
    ob.scale = (1.0, height / size, 1.0)
    on_layer(ob, i)
    return ob


def fence(scene, name, i, ma, z_off=0.2, width=1.2, height=0.6):
    """a cut-out fence standing on tile i along X"""
    x, z = cell_center(i)
    ob = p3.uv_plane(scene, name, width, (x, -height / 2.0, z + z_off), (0.0, 0.0, 0.0), ma)
    ob.scale = (1.0, height / width, 1.0)
    on_layer(ob, i)
    return ob


def make_shadows(folder, E):
    scene = base("Shadows")
    scene.layers = [True] * 20
    floor = classic("S_floor", (0.8, 0.8, 0.8))
    solid = classic("S_post", (0.6, 0.55, 0.5))
    holes = classic("S_fence", (0.5, 0.35, 0.2))
    holes.use_transparency = True
    holes.alpha = 0.0
    holes.game_settings.alpha_blend = 'CLIP'
    img = p3.image("FenceHoles", lambda u, v: (0.6, 0.4, 0.2, 1.0 if (int(u * 4) + int(v * 4)) % 2 == 0 else 0.0))
    slot = p3.bi_slot(holes, img, color=True)
    slot.use_map_alpha = True
    cells = {}
    lamp_at = (0.5, 1.4, 0.5)

    def cell(i, name, **extra):
        tile(scene, "T_" + name, i, floor)
        x, z = cell_center(i)
        cells[name] = dict(cell=i, rect=layout_rect(x, z, TILE / 2.0), center=screen(x, z), **extra)

    def lamp_screen(i, offset):
        x, z = cell_center(i)
        return screen(x + offset[0], z + offset[2])

    # every lamp type casts: a post under a point, spot, sun and area lamp
    for i, name, kind, size in ((0, "point_hard", 'POINT', 0.05), (1, "point_soft", 'POINT', 0.4),
                                (2, "spot", 'SPOT', 0.1), (3, "sun", 'SUN', 0.0), (4, "area", 'AREA', 0.0)):
        cell(i, name, lamp=lamp_screen(i, lamp_at), kind=kind)
        post(scene, "P_" + name, i, solid)
        ob = cell_lamp(scene, "S_" + name, kind, i, lamp_at, energy=1.0, distance=2.0, size=max(size, 0.02),
                       shadow=True)
        la = ob.data
        if kind == 'SPOT':
            la.spot_size = math.radians(100.0)
            la.spot_blend = 0.2
            la.shadow_buffer_size = 1024
        if kind == 'SUN':
            la.energy = 0.8
            if not STOCK:
                la["bge_sun_angle"] = 1.0
        if kind == 'AREA':
            la.size = 0.3

    # a cut-out fence (Simple map, point lamp) and the same under a Variance spot
    fence_lamp = (0.0, 1.2, 0.8)
    for i, name, kind in ((5, "fence", 'POINT'), (13, "fence_variance", 'SPOT')):
        cell(i, name, lamp=lamp_screen(i, fence_lamp), fence_z=0.2)
        fence(scene, "F_" + name, i, holes)
        ob = cell_lamp(scene, "S_" + name, kind, i, fence_lamp, target=(0.0, 0.0, -0.2), energy=1.0, distance=2.0,
                       size=0.02, shadow=True)
        if kind == 'SPOT':
            ob.data.spot_size = math.radians(110.0)
            ob.data.shadow_buffer_size = 1024
            ob.data.ge_shadow_buffer_type = 'VARIANCE'

    # slopes: grazing light on an empty tile (no acne: nothing may darken it)
    cell(6, "graze_sun")
    cell_lamp(scene, "S_graze_sun", 'SUN', 6, (-1.0, 0.27, 0.0), energy=1.5, shadow=True)
    cell(7, "graze_point")
    cell_lamp(scene, "S_graze_point", 'POINT', 7, (-1.2, 0.25, 0.0), energy=2.0, distance=2.0, size=0.05,
              shadow=True)

    # contact shadows: a low slab touching the tile; the lamp's large bias
    # leaves a gap at the contact in its map, which contact shadows close
    cell(8, "contact", slab=layout_rect(cell_center(8)[0], cell_center(8)[1], 0.3 / 0.8))
    x, z = cell_center(8)
    slab = p2.cube(scene, "Slab", 0.6, (x, -0.075, z), solid)
    slab.scale = (1.0, 0.25, 1.0)
    on_layer(slab, 8)
    ob = cell_lamp(scene, "S_contact", 'SPOT', 8, (1.4, 0.8, 0.0), energy=1.5, distance=2.0, size=0.05,
                   shadow=True)
    ob.data.spot_size = math.radians(90.0)
    ob.data.shadow_buffer_bias = 5.0
    if not STOCK:
        ob.data["bge_contact_shadow"] = 0.4

    # static and moving lamps (the shadow is drawn again only when needed)
    for i, name in ((9, "static"), (10, "moving"), (11, "static_mover")):
        cell(i, name)
        post(scene, ("Mover" if name == "static_mover" else "P_" + name), i, solid)
        ob = cell_lamp(scene, "S_" + name, 'POINT', i, lamp_at, energy=1.0, distance=2.0, size=0.1, shadow=True)
        ob.data.shadow_buffer_clip_end = 1.6

    driver = bpy.data.objects.new("ShadowScript", None)
    scene.objects.link(driver)
    logic(scene, driver, "p8_shadow_move.py", MOVE_SCRIPT)

    E["shadows"] = {"cells": cells, "px_per_unit": WIDTH / ORTHO}
    path = os.path.join(folder, "shadows.blend")
    save(scene, path, physical=False, lamps_physical=True)
    # the same without contact shadows, and without any shadow
    for la in bpy.data.lamps:
        la["bge_contact_shadow"] = 0.0
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "shadows_nocontact.blend"), copy=True)
    for la in bpy.data.lamps:
        la.use_shadow = False
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "shadows_noshadow.blend"), copy=True)


def make_shadows_far(folder, E):
    """a long floor seen from a person's height, a sun from behind: a post
    near the camera and one far away, each with its shadow toward the camera"""
    from bpy_extras.object_utils import world_to_camera_view
    scene = p2.empty_scene()
    scene.name = "ShadowsFar"
    scene.game_settings.resolution_x = WIDTH
    scene.game_settings.resolution_y = HEIGHT
    scene.render.resolution_x = WIDTH
    scene.render.resolution_y = HEIGHT
    scene.render.resolution_percentage = 100
    p2.world(scene, (0.05, 0.05, 0.08))
    floor = classic("SF_floor", (0.8, 0.8, 0.8))
    solid = classic("SF_post", (0.6, 0.55, 0.5))
    p2.plane(scene, "SF_floor", 1.0, (0.0, 25.0, 0.0), (0.0, 0.0, 0.0), floor).scale = (10.0, 70.0, 1.0)
    cam = p2.camera(scene, (0.0, -2.0, 6.0), (0.0, 18.0, 0.0), lens=35.0)
    sun = p2.lamp(scene, "SF_sun", 'SUN', (0.0, 60.0, 20.0), energy=1.0)
    elev = math.radians(30.0)
    p2.look_at(sun, (0.0, 60.0 - math.cos(elev), 20.0 - math.sin(elev)))
    sun.data.use_shadow = True
    sun.data.shadow_buffer_clip_end = 50.0
    if not STOCK:
        sun.data["bge_physical"] = 0
    height, width = 2.5, 1.0
    length = height / math.tan(elev)
    scene.update()
    probes = {}
    for name, y in (("near", 16.0), ("far", 34.0)):
        ob = p2.cube(scene, "SF_" + name, width, (0.0, y, height / 2.0), solid)
        ob.scale = (1.0, 1.0, height / width)
        # the shadow's middle on the floor, toward the camera
        from mathutils import Vector
        pts = []
        for t in (0.45, 0.55, 0.65):
            p = world_to_camera_view(scene, cam, Vector((0.0, y - width / 2.0 - t * length, 0.0)))
            pts.append([p.x * WIDTH, (1.0 - p.y) * HEIGHT])
        probes[name] = pts
    E["shadows_far"] = {"points": probes}
    save(scene, os.path.join(folder, "shadows_far.blend"), physical=False, lamps_physical=True)
    for la in bpy.data.lamps:
        la.use_shadow = False
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "shadows_far_noshadow.blend"), copy=True)


def make_shadows_bench(folder):
    """B8.7: 36 posts and a spinning box on a floor, 8 point lamps and a sun;
    variants cast from 0, 1, 4 or 8 point lamps, the sun only, or 8 with hard
    shadows (the box keeps every shadow being drawn)"""
    scene = p2.empty_scene()
    scene.name = "ShadowBench"
    scene.game_settings.resolution_x = WIDTH
    scene.game_settings.resolution_y = HEIGHT
    p2.world(scene, (0.05, 0.05, 0.08))
    floor = classic("SB_floor", (0.8, 0.8, 0.8))
    solid = classic("SB_post", (0.6, 0.55, 0.5))
    p2.plane(scene, "SB_floor", 20.0, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), floor)
    mesh = None
    for i in range(6):
        for j in range(6):
            ob = p2.cube(scene, "SB_post%d%d" % (i, j), 0.4, (-5.0 + 2.0 * i, -5.0 + 2.0 * j, 0.6), solid, mesh=mesh)
            ob.scale = (1.0, 1.0, 3.0)
            mesh = ob.data
    spinner = p2.cube(scene, "Spinner", 1.0, (0.0, 0.0, 1.5), solid)
    p2.camera(scene, (0.0, -14.0, 9.0), (0.0, 0.0, 0.0), lens=30.0)
    points = []
    for k in range(8):
        a = 2.0 * math.pi * k / 8.0
        ob = p2.lamp(scene, "SB_point%d" % k, 'POINT', (6.0 * math.cos(a), 6.0 * math.sin(a), 3.0), energy=0.3,
                     distance=6.0, soft=0.3)
        ob.data.shadow_buffer_clip_end = 30.0
        points.append(ob.data)
    sun = p2.lamp(scene, "SB_sun", 'SUN', (0.0, 0.0, 10.0), (math.radians(40.0), 0.0, math.radians(30.0)),
                  energy=0.5)
    driver = bpy.data.objects.new("BenchScript", None)
    scene.objects.link(driver)
    logic(scene, driver, "p8_shadow_move.py", MOVE_SCRIPT)
    p2.add_probe(scene)
    scene.update()

    def variant(name, casting, sun_casts=False, size=0.3):
        for k, la in enumerate(points):
            la.use_shadow = k < casting
            la.shadow_soft_size = size
            la["bge_physical"] = 0
        sun.data.use_shadow = sun_casts
        sun.data["bge_physical"] = 0
        bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "shadows_bench_%s.blend" % name), copy=True)

    for n in (0, 1, 4, 8):
        variant(str(n), n)
    variant("sun", 0, sun_casts=True)
    variant("hard", 8, size=0.0)


# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# custom_shaders and custom_filters (P8 v2 Part A: users' old GLSL, T8.3)

CS_SOURCES = {
    "Matrices": (
        """uniform vec4 color;
varying vec3 n;
void main()
{
	n = normalize(gl_NormalMatrix * gl_Normal);
	gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex;
}
""",
        """uniform vec4 color;
varying vec3 n;
void main()
{
	gl_FragColor = color * (0.4 + 0.6 * abs(n.z));
}
"""),
    "Lighting": (
        """#version 120
varying vec3 normal, eye;
void main()
{
	normal = gl_NormalMatrix * gl_Normal;
	eye = vec3(gl_ModelViewMatrix * gl_Vertex);
	gl_Position = ftransform();
}
""",
        """#version 120
varying vec3 normal, eye;
void main()
{
	vec3 L = normalize(gl_LightSource[0].position.xyz - eye * gl_LightSource[0].position.w);
	float d = max(dot(normalize(normal), L), 0.0);
	gl_FragColor = vec4(gl_FrontMaterial.diffuse.rgb * gl_LightSource[0].diffuse.rgb * d + vec3(0.1), 1.0);
}
"""),
    "Textured": (
        """void main()
{
	gl_TexCoord[0] = gl_MultiTexCoord0;
	gl_FrontColor = gl_Color;
	gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
}
""",
        """uniform sampler2D tex;
void main()
{
	gl_FragColor = texture2D(tex, gl_TexCoord[0].st);
}
"""),
    "Tangent": (
        """attribute vec4 Tangent;
varying vec4 t;
void main()
{
	t = Tangent;
	gl_Position = ftransform();
}
""",
        """varying vec4 t;
void main()
{
	gl_FragColor = vec4(abs(t.xyz), 1.0);
}
"""),
    "Timed": (
        """void main()
{
	gl_TexCoord[0] = gl_MultiTexCoord0;
	gl_Position = ftransform();
}
""",
        """uniform sampler2D texture;
uniform float time;
void main()
{
	vec4 sample = texture2D(texture, gl_TexCoord[0].st);
	gl_FragColor = vec4(sample.rgb * (0.5 + 0.5 * sin(time)), 1.0);
}
"""),
    "Broken": (
        """void main()
{
	gl_Position = ftransform();
}
""",
        """void main()
{
	gl_FragColor = vec4(1.0) * not_declared_anywhere;
}
"""),
}

CS_ORDER = ("Matrices", "Lighting", "Textured", "Tangent", "Timed", "Broken")

CS_SCRIPT = r"""
# BGE-Remix P8 v2 (T8.3): old-style custom shaders, set once, the time each frame
import bge
SOURCES = %s
g = bge.logic.globalDict
cont = bge.logic.getCurrentController()
own = cont.owner
key = "p8_cs_" + own.name
n = g.get(key, 0) + 1
g[key] = n
mat = own.meshes[0].materials[0]
sh = mat.getShader()
if sh is not None:
    if n == 1 and not sh.isValid():
        vs, fs = SOURCES[own.name]
        sh.setSource(vs, fs, True)
        if own.name == "Matrices":
            sh.setUniform4f("color", 0.9, 0.5, 0.2, 1.0)
        if own.name in ("Textured", "Timed"):
            sh.setSampler("tex" if own.name == "Textured" else "texture", 0)
        if own.name == "Tangent":
            sh.setAttrib(1)
    if own.name == "Timed" and sh.isValid():
        sh.setUniform1f("time", n * 0.05)
"""


def cs_row(scene, img):
    """six cubes in a row, turned to show three faces; a sun lamp"""
    rects = {}
    for i, name in enumerate(CS_ORDER):
        ma = classic("CS_" + name, (0.8, 0.8, 0.8))
        if name in ("Textured", "Timed"):
            p3.bi_slot(ma, img, color=True)
        x = -5.0 + 2.0 * i
        ob = p3.textured_cube(scene, name, 1.2, (x, 0.0, 0.0), ma)
        ob.rotation_euler = (math.radians(25.0), math.radians(10.0), math.radians(35.0))
        rects[name] = layout_rect(x, 0.0, 0.45)
    la = bpy.data.lamps.new("Sun", 'SUN')
    sun = bpy.data.objects.new("Sun", la)
    sun.location = (0.0, -10.0, 5.0)
    sun.rotation_euler = (math.radians(60.0), 0.0, math.radians(20.0))
    scene.objects.link(sun)
    return rects


def make_custom_shaders(folder, E):
    scene = base("CustomShaders")
    img = p3.image("CS_checker", p3.checker)
    rects = cs_row(scene, img)
    body = CS_SCRIPT % repr({k: CS_SOURCES[k] for k in CS_ORDER})
    for name in CS_ORDER:
        logic(scene, bpy.data.objects[name], "cs_%s.py" % name, body)
    save(scene, os.path.join(folder, "custom_shaders.blend"), physical=False)
    E["custom_shaders"] = {"rects": rects, "order": list(CS_ORDER), "broken": "Broken"}


CF_MODES = ("BLUR", "SHARPEN", "DILATION", "EROSION", "LAPLACIAN", "SOBEL", "PREWITT", "GRAYSCALE", "SEPIA",
            "INVERT", "CUSTOMFILTER", "MOTIONBLUR")

CF_CUSTOM = """uniform sampler2D bgl_RenderedTexture;
uniform sampler2D bgl_DepthTexture;
void main()
{
	vec4 c = texture2D(bgl_RenderedTexture, gl_TexCoord[0].st);
	float d = texture2D(bgl_DepthTexture, gl_TexCoord[0].st).r;
	gl_FragColor = vec4(c.rgb * 0.5 + vec3(d) * 0.5, 1.0);
}
"""

CF_SCRIPT = r"""
# BGE-Remix P8 v2 (T8.3): each 2D filter in turn, a screenshot of each
import os
import bge
MODES = %s
g = bge.logic.globalDict
cont = bge.logic.getCurrentController()
n = g.get("p8_cf", 0) + 1
g["p8_cf"] = n
k, step = divmod(n - 10, 10)
if n >= 10 and k < len(MODES):
    if step == 0:
        cont.activate(cont.actuators["F_" + MODES[k]])
    elif step == 6:
        out = os.environ.get("BGE_REMIX_P8_FILTER_DIR")
        if out:
            bge.render.makeScreenshot(os.path.join(out, "filter_%%s.png" %% MODES[k].lower()))
"""


def make_custom_filters(folder, E):
    scene = base("CustomFilters")
    img = p3.image("CF_checker", p3.checker)
    for i, name in enumerate(CS_ORDER):
        ma = classic("CF_" + name, (0.2 + 0.13 * i, 0.9 - 0.12 * i, 0.4))
        p3.bi_slot(ma, img, color=True)
        x = -5.0 + 2.0 * i
        ob = p3.textured_cube(scene, "F" + name, 1.2, (x, 0.0, 0.0), ma)
        ob.rotation_euler = (math.radians(25.0), math.radians(10.0), math.radians(35.0))
    la = bpy.data.lamps.new("Sun", 'SUN')
    sun = bpy.data.objects.new("Sun", la)
    sun.location = (0.0, -10.0, 5.0)
    sun.rotation_euler = (math.radians(60.0), 0.0, math.radians(20.0))
    scene.objects.link(sun)
    driver = bpy.data.objects.new("FilterDriver", None)
    scene.objects.link(driver)
    logic(scene, driver, "cf_driver.py", CF_SCRIPT % repr(CF_MODES))
    controller = driver.game.controllers[-1]
    text = bpy.data.texts.new("cf_custom.glsl")
    text.write(CF_CUSTOM)
    for mode in CF_MODES:
        bpy.ops.logic.actuator_add(type='FILTER_2D', name="F_" + mode, object=driver.name)
        act = driver.game.actuators[-1]
        act.mode = mode
        act.filter_pass = 0
        if mode == "CUSTOMFILTER":
            act.glsl_shader = text
        if mode == "MOTIONBLUR":
            act.motion_blur_factor = 0.5
        controller.link(actuator=act)
    save(scene, os.path.join(folder, "custom_filters.blend"), physical=False)
    E["custom_filters"] = {"modes": [m.lower() for m in CF_MODES]}


def main():
    folder = out_dir()
    E = {"width": WIDTH, "height": HEIGHT, "failed": {}}
    if STOCK:
        makers = [("mt_features", lambda: make_mt_features(folder, E)), ("mt_bench", lambda: make_mt_bench(folder))]
        target = os.path.join(folder, "p8_expected_stock.json")
    else:
        makers = [("proc_textures", lambda: make_proc_textures(folder, E)),
                  ("proc_animated", lambda: make_proc_animated(folder)),
                  ("pbr_spheres", lambda: pbr_scene(folder, "pbr_spheres", True, E)),
                  ("pbr_classic", lambda: pbr_scene(folder, "pbr_classic", False, E)),
                  ("classic_remix", lambda: make_classic_remix(folder, E)),
                  ("glsl_features", lambda: make_glsl_features(folder, E)),
                  ("fallback", lambda: make_fallback(folder, E)),
                  # Part B
                  ("lamps", lambda: make_lamps(folder, E)),
                  ("shadows", lambda: make_shadows(folder, E)),
                  ("shadows_far", lambda: make_shadows_far(folder, E)),
                  ("shadows_bench", lambda: make_shadows_bench(folder)),
                  # P8 v2 Part A: users' old GLSL
                  ("custom_shaders", lambda: make_custom_shaders(folder, E)),
                  ("custom_filters", lambda: make_custom_filters(folder, E))]
        target = os.path.join(folder, "p8_expected.json")
    for name, make in makers:
        try:
            bpy.ops.wm.read_factory_settings(use_empty=False)
            make()
        except Exception:
            E["failed"][name] = traceback.format_exc()
            print("BGE-Remix: making %s failed:\n%s" % (name, E["failed"][name]))
    with open(target, "w") as f:
        json.dump(E, f, indent=1)
    print("BGE-Remix: wrote", target)


main()
