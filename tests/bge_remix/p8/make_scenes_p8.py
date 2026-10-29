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

# the game window and the orthographic view (Blender units across)
WIDTH, HEIGHT = 1280, 720
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


def save(scene, path, physical=None):
    """physical: None keeps each material's own shading (new materials are
    physical), False makes all Classic"""
    if physical is False:
        for ma in bpy.data.materials:
            ma["bge_physical"] = 0
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
# changes the texture while playing (embedded: Blender's own data); Remix bakes it again
import bge
import bpy
g = bge.logic.globalDict
n = g.get("p8_anim", 0) + 1
g["p8_anim"] = n
if n in (40, 120):
    tex = bpy.data.textures["T_animated"]
    tex.noise_scale = 0.12 if n == 40 else 0.5
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
    p2.add_probe(scene)
    scene.update()
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "mt_bench.blend"), check_existing=False,
                                compress=False)


# ---------------------------------------------------------------------------

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
                  ("fallback", lambda: make_fallback(folder, E))]
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
