"""BGE-Remix P9.4: the texture wall (run inside our Blender, background;
make_scenes_p9 calls make(folder, E)).

p9_texture_wall.blend         eighteen square tiles facing an orthographic
                              camera, three rows of six, each a Classic
                              material with an image texture slot read by UV
                              (one asymmetric image, so a flip or a mirror
                              shows), lit by one sun under a black sky, ACES:
                              Remix off (the OpenGL fallback's picture)
p9_texture_wall_remix.blend   the same with RTX Remix on

Each tile shows one texture setting. The first ten go to Remix as they are
(its wrap and filter per material, the transform in the UVs sent): plain,
scale 2, an offset, repeat 3 x 2, Extend, Clip, Mirror (both axes, and X
alone), Interpolation off, a flipped image. The other eight are baked
(bake_slots): influence 0.5, a second slot multiplied, one added, Generated
coordinates, a colour ramp, Negative, the Checker extension, and a colour and
a normal map slot whose extensions differ.

Each material has its own specular hardness, so its roughness (the fake
runtime logs it) tells the tiles apart. E["texture_wall"] holds what each
tile should give: Remix's wrap modes (as the runtime numbers them: 0 clamp,
1 repeat, 2 mirrored repeat, 3 clip), its filter (0 nearest, 1 linear), the
UVs' range sent, the way it went (direct or baked) and whether the game
draws the setting (the gate compares only those: the game's GLSL draws a
slot's scale and offset but not repeats, mirror, flip, Extend, Clip or
Interpolation off).
"""

import math

import bpy

import make_scenes_p9 as m9

HALF = 0.6          # a tile's half size
STEP_X, ROW_Z = 1.9, (2.0, 0.0, -2.0)
IMAGE = "P94_letter"

IDENTITY = [0.0, 0.0, 1.0, 1.0]

# (label, settings, way, (wrap u, wrap v), filter, uv range, the game draws it)
TILES = [
    ("plain", {}, "direct", (1, 1), 1, IDENTITY, True),
    ("scale_2", {"scale": (2.0, 2.0, 1.0)}, "direct", (1, 1), 1, [-0.5, -0.5, 1.5, 1.5], True),
    ("offset", {"offset": (0.25, 0.1, 0.0)}, "direct", (1, 1), 1, [0.25, 0.1, 1.25, 1.1], True),
    ("repeat_3x2", {"repeat": (3, 2)}, "direct", (1, 1), 1, [0.0, 0.0, 3.0, 2.0], False),
    ("extend", {"extension": 'EXTEND', "scale": (2.0, 2.0, 1.0)}, "direct", (0, 0), 1,
     [-0.5, -0.5, 1.5, 1.5], False),
    ("clip", {"extension": 'CLIP', "scale": (2.0, 2.0, 1.0)}, "direct", (3, 3), 1, [-0.5, -0.5, 1.5, 1.5], False),
    ("mirror", {"repeat": (2, 2), "mirror": (True, True)}, "direct", (2, 2), 1, [0.0, 0.0, 2.0, 2.0], False),
    ("mirror_x", {"repeat": (2, 1), "mirror": (True, False)}, "direct", (2, 1), 1, [0.0, 0.0, 2.0, 1.0], False),
    ("no_interpolation", {"interpolation": False}, "direct", (1, 1), 0, IDENTITY, False),
    # flipped after the slot's scale (2 along X): u' = v, v' = 2 u - 0.5
    ("flip", {"flip": True, "scale": (2.0, 1.0, 1.0)}, "direct", (1, 1), 1, [0.0, -0.5, 1.0, 1.5], False),
    ("influence_half", {"influence": 0.5}, "baked", (1, 1), 1, IDENTITY, True),
    ("multiply", {"second": 'MULTIPLY'}, "baked", (1, 1), 1, IDENTITY, True),
    ("add", {"second": 'ADD'}, "baked", (1, 1), 1, IDENTITY, True),
    ("generated", {"coords": 'ORCO'}, "baked", (1, 1), 1, IDENTITY, True),
    ("colour_ramp", {"ramp": True}, "baked", (1, 1), 1, IDENTITY, True),
    ("negative", {"negative": True}, "baked", (1, 1), 1, IDENTITY, True),
    ("checker", {"extension": 'CHECKER'}, "baked", (1, 1), 1, IDENTITY, True),
    ("mixed_extensions", {"normal_extend": True}, "baked", (1, 1), 1, IDENTITY, True),
]

# why each baked tile is baked (Match's note says it)
BAKE_WHY = {
    "influence_half": "influence", "multiply": "blend", "add": "blend", "generated": "coordinates",
    "colour_ramp": "Colors panel", "negative": "Negative", "checker": "Checker",
    "mixed_extensions": "differs",
}
WRAP_NAMES = {0: "clamp", 1: "repeat", 2: "mirrored repeat", 3: "clip"}


def letter(x, y):
    """an asymmetric picture: a colour gradient with a dark L along the left
    and bottom edges and a light dot near the top right"""
    if x < 0.18 or y < 0.14:
        return (0.06, 0.05, 0.08, 1.0)
    if (x - 0.75) ** 2 + (y - 0.75) ** 2 < 0.012:
        return (0.95, 0.92, 0.6, 1.0)
    return (0.15 + 0.7 * x, 0.2 + 0.55 * y, 0.45, 1.0)


def stripes(x, y):
    return (0.9, 0.9, 0.9, 1.0) if int(x * 6) % 2 == 0 else (0.35, 0.4, 0.5, 1.0)


def tile(scene, name, x, z, ma):
    """a square facing the camera, UVs 0..1 from its bottom left"""
    s = HALF
    me = bpy.data.meshes.new(name)
    me.from_pydata([(-s, -s, 0.0), (s, -s, 0.0), (s, s, 0.0), (-s, s, 0.0)], [], [(0, 1, 2, 3)])
    me.uv_textures.new("UVMap")
    for loop, uv in zip(me.uv_layers[0].data, ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0))):
        loop.uv = uv
    me.update()
    me.materials.append(ma)
    ob = bpy.data.objects.new(name, me)
    ob.location = (x, 0.0, z)
    ob.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    ob.game.physics_type = 'NO_COLLISION'
    scene.objects.link(ob)
    return ob


def roughness_of(hardness):
    # the translator's rule (hardness_to_roughness)
    return (2.0 / (hardness + 2.0)) ** 0.25


def tile_material(label, index, how, img, second, bumps):
    ma = m9.classic_material("P94_" + label, (0.8, 0.8, 0.8))
    ma.specular_intensity = 0.05
    ma.specular_hardness = 20 + 7 * index
    slot = m9.slot_image(ma, img, color=True)
    tex = slot.texture
    if "scale" in how:
        slot.scale = how["scale"]
    if "offset" in how:
        slot.offset = how["offset"]
    if "extension" in how:
        tex.extension = how["extension"]
    if "repeat" in how:
        tex.repeat_x, tex.repeat_y = how["repeat"]
    if "mirror" in how:
        tex.use_mirror_x, tex.use_mirror_y = how["mirror"]
    if "interpolation" in how:
        tex.use_interpolation = how["interpolation"]
    if how.get("flip"):
        tex.use_flip_axis = True
    if "influence" in how:
        slot.diffuse_color_factor = how["influence"]
    if "coords" in how:
        slot.texture_coords = how["coords"]
    if how.get("ramp"):
        tex.use_color_ramp = True
        els = tex.color_ramp.elements
        els[0].color = (0.1, 0.05, 0.3, 1.0)
        els[1].color = (1.0, 0.8, 0.3, 1.0)
    if how.get("negative"):
        slot.invert = True
    if "second" in how:
        s2 = m9.slot_image(ma, second, color=True)
        s2.blend_type = how["second"]
    if how.get("normal_extend"):
        s2 = m9.slot_image(ma, bumps, normal=True)
        s2.texture.extension = 'EXTEND'
    return ma


def make(folder, E):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = m9.base("P94TextureWall")
    img = m9.p3.image(IMAGE, letter, size=64)
    second = m9.p3.image("P94_stripes", stripes, size=64)
    bumps = m9.p3.image("P94_bumps", m9.p3.bumps, size=64, non_color=True)
    rects, tiles = {}, {}
    for i, (label, how, way, wrap, filt, uvbox, game) in enumerate(TILES):
        ma = tile_material(label, i, how, img, second, bumps)
        row, k = divmod(i, 6)
        x, z = (k - 2.5) * STEP_X, ROW_Z[row]
        tile(scene, "P94_" + label, x, z, ma)
        rects[label] = m9.layout_rect(x, z, HALF)
        tiles[label] = {"material": ma.name, "roughness": roughness_of(ma.specular_hardness), "way": way,
                        "wrap": list(wrap), "filter": filt, "uvbox": uvbox, "game_draws": game,
                        "wrap_name": WRAP_NAMES[wrap[0]] + ("" if wrap[1] == wrap[0] else " / " + WRAP_NAMES[wrap[1]]),
                        "moved": uvbox != IDENTITY, "nearest": filt == 0,
                        "baked_fields": (["albedo", "normal"] if label == "mixed_extensions" else ["albedo"])
                        if way == "baked" else [], "why": BAKE_WHY.get(label)}
    sun = m9.p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 0.0), (math.radians(65.0), 0.0, math.radians(15.0)),
                     energy=1.0)
    sun.data.shadow_method = 'NOSHADOW'
    w = scene.world
    w.use_sky_blend = False
    w.horizon_color = (0.0, 0.0, 0.0)
    w.zenith_color = (0.0, 0.0, 0.0)
    w.ambient_color = (0.0, 0.0, 0.0)
    m9.logic(scene, "P9Study", m9.STUDY_SCRIPT)
    m9.p2.add_probe(scene)
    for sc in bpy.data.scenes:
        sc["bge_remix_pipeline"] = 1
    scene.update()
    files = [m9.save_copy(folder, "p9_texture_wall", remix=0, tone=1),
             m9.save_copy(folder, "p9_texture_wall_remix", remix=1, tone=1)]
    E["texture_wall"] = {"files": files, "rects": rects, "order": [t[0] for t in TILES], "tiles": tiles,
                         "image": IMAGE, "size": [m9.WIDTH, m9.HEIGHT],
                         "gate": [t[0] for t in TILES if t[6]]}
