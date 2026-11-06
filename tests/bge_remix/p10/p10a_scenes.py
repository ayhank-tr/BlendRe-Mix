"""BGE-Remix p10a: the scenes for the texture remaster (P10.1-P10.3, P10.5).

make(folder, E) runs inside our Blender (from make_scenes_p10.py), saves its
.blend files into folder and writes what the tests expect into E["p10a"].
Every image is made here with numpy, so its right answer is known.

p10a_audit.blend     the audit's scene (P10.1, P10.3's value check):
                     a noise wall, a brick wall and a sign (classed random,
                     regular, unique); a 64 px image on a 10 m wall (6.4
                     px/m) and a 2048 px one on a 1 m box (2048 px/m); a
                     colour-only material and a complete one (colour,
                     roughness, normal: only height missing); a flat-lit and
                     a side-lit copy of one image (dark on the left); six
                     small boxes whose values are out of range (too bright,
                     too dark, half metal, dark metal, a mirror) or fine
p10a_tiles(_remix)   Brick and Rock presets on large squares, three tiles
                     across and up (P10.2's tile faults: no seam, no ghosting)
p10a_classic(_remix) Classic tiles with a colour image at Diffuse Intensity
                     0.8, 0.5 and 1.0, and a face image at 0.8, under a sun
                     and a black sky, ACES (P10.3's brightness finding)
p10a_fit.blend       a plain game scene the fit probe bakes in, and a sign
                     (P10.2: each preset baked at known settings there is
                     fitted again; the sign must not fit)
p10a_encode1024_remix, p10a_encode2048_remix
                     a Brick preset baked at 1K, at 2K (P10.2's open
                     question: the encode time with Remix running)
apply/p10a_apply_remix.blend
                     the Apply scene (P10.5): a noise wall (fit), a brick
                     wall (fit) and a sign (derive), RTX Remix on, opened in a
                     Remix View (Debug mode for the preview square)
"""

import math
import os

import bpy
import numpy as np

import make_scenes_p9 as m9
import p96_scenes as s96

p2, p3 = m9.p2, m9.p3

IMAGE_SIZE = 512
DENSITY_WALL = 10.0          # m: the 64 px image's wall (10 x 10 m, UV 0..1)
DENSITY_BOX = 1.0            # m: the 2048 px image's box (each face UV 0..1)
TILE_PRESETS = ('BRICK', 'ROCK')
# (label, Diffuse Intensity, a face image, no specular); fix round 1: a
# matte tile (Specular Intensity 0: roughness 1 in Remix), to tell Remix's
# own reflection from the colour's scale (reported, not gated)
CLASSIC_TILES = (("ref080", 0.8, False, False), ("ref050", 0.5, False, False), ("ref100", 1.0, False, False),
                 ("face080", 0.8, True, False), ("matte080", 0.8, False, True))

# a 5 x 6 block font (the sign's letters, drawn as blocks: no outside fonts)
FONT = {
    'B': ["1110", "1001", "1110", "1001", "1001", "1110"],
    'G': ["0111", "1000", "1011", "1001", "1001", "0111"],
    'E': ["1111", "1000", "1110", "1000", "1000", "1111"],
    'R': ["1110", "1001", "1110", "1010", "1001", "1001"],
    'M': ["10001", "11011", "10101", "10001", "10001", "10001"],
    'I': ["111", "010", "010", "010", "010", "111"],
    'X': ["1001", "1001", "0110", "0110", "1001", "1001"],
    ' ': ["00", "00", "00", "00", "00", "00"],
}


# ---------------------------------------------------------------------------
# images (numpy, linear colours, rows bottom first)

def noise(n, beta=2.0, seed=0, fmin=2.0):
    """periodic fractal noise (a 1/f^beta spectrum), mean 0, spread 1"""
    rs = np.random.RandomState(seed)
    w = rs.randn(n, n)
    f = np.fft.fftfreq(n) * n
    fy, fx = np.meshgrid(f, f, indexing='ij')
    r = np.sqrt(fx ** 2 + fy ** 2)
    k = 1.0 / np.maximum(r, fmin) ** (beta / 2.0)
    g = np.real(np.fft.ifft2(np.fft.fft2(w) * k))
    return (g - g.mean()) / g.std()


def noise_image(n=IMAGE_SIZE, seed=0):
    t = 1.0 + 0.25 * noise(n, 2.0, seed)
    return np.clip(t[..., None] * np.array((0.30, 0.28, 0.25)), 0.0, 1.0)


def brick_image(n=IMAGE_SIZE, cols=4, rows=12, seed=1):
    """bricks (cols across, rows up, every other row moved half a brick),
    mortar joints, a tone per brick"""
    rs = np.random.RandomState(seed)
    y, x = np.mgrid[0:n, 0:n] / float(n)
    yy = y * rows
    row = np.floor(yy)
    fy = yy - row
    xx = x * cols + (row % 2) * 0.5
    col = np.floor(xx)
    fx = xx - col
    mortar = (fx < 0.04) | (fy < 0.12)
    vals = rs.rand(cols + 2, rows + 1)
    v = vals[(col % cols).astype(int), row.astype(int)]
    t = 0.7 + 0.6 * v + 0.05 * noise(n, 2.0, seed + 3)
    img = np.clip(t[..., None] * np.array((0.26, 0.10, 0.06)), 0.0, 1.0)
    img[mortar] = (0.33, 0.31, 0.28)
    return img


def sign_image(n=IMAGE_SIZE, text="BGE REMIX", seed=2):
    """a board with dark letters in a line and a dark frame"""
    img = np.zeros((n, n, 3)) + np.array((0.55, 0.52, 0.45))
    img *= (1.0 + 0.03 * noise(n, 2.0, seed))[..., None]
    cell = n // 48
    x0, y0 = n // 10, n // 2
    for ch in text:
        g = FONT[ch]
        h, w = len(g), len(g[0])
        for j, line in enumerate(g):
            for i, c in enumerate(line):
                if c == '1':
                    ys, xs = y0 + (h - 1 - j) * cell, x0 + i * cell
                    img[ys:ys + cell, xs:xs + cell] = (0.05, 0.05, 0.08)
        x0 += (w + 1) * cell
    b = n // 40
    img[:b, :] = 0.1
    img[-b:, :] = 0.1
    img[:, :b] = 0.1
    img[:, -b:] = 0.1
    return img


def side_lit(img):
    """the same image lit from the right: a third as bright on its left edge"""
    ramp = np.linspace(0.35, 1.0, img.shape[1])[None, :, None]
    return img * ramp


def to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1.0 / 2.4) - 0.055)


def bl_image(name, rgb, non_color=False, linear=False):
    """a packed Blender image from an (h, w, 3) array (linear colours;
    non_color: the values as they are)"""
    h, w = rgb.shape[0], rgb.shape[1]
    img = bpy.data.images.new(name, w, h, alpha=False)
    if non_color:
        img.colorspace_settings.name = 'Non-Color'
    px = np.ones((h, w, 4), dtype=np.float32)
    px[..., :3] = rgb if (non_color or linear) else to_srgb(rgb)
    img.pixels = px.reshape(-1).tolist()
    img.pack(as_png=True)
    return img


# ---------------------------------------------------------------------------
# meshes

def plane(scene, name, w, h, location, ma, uv=(1.0, 1.0), facing='Y'):
    """a w x h rectangle standing up (facing -Y), its UVs 0..uv"""
    sx, sy = w / 2.0, h / 2.0
    me = bpy.data.meshes.new(name)
    me.from_pydata([(-sx, -sy, 0), (sx, -sy, 0), (sx, sy, 0), (-sx, sy, 0)], [], [(0, 1, 2, 3)])
    me.uv_textures.new("UVMap")
    for loop, c in zip(me.uv_layers[0].data, ((0, 0), (uv[0], 0), (uv[0], uv[1]), (0, uv[1]))):
        loop.uv = c
    me.update()
    me.materials.append(ma)
    ob = bpy.data.objects.new(name, me)
    ob.location = location
    if facing == 'Y':
        ob.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    scene.objects.link(ob)
    ob.game.physics_type = 'NO_COLLISION'
    return ob


def uv_box(scene, name, size, location, ma):
    """a cube size metres a side, every face UV 0..1"""
    s = size / 2.0
    v = [(-s, -s, -s), (s, -s, -s), (s, s, -s), (-s, s, -s), (-s, -s, s), (s, -s, s), (s, s, s), (-s, s, s)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    me = bpy.data.meshes.new(name)
    me.from_pydata(v, [], f)
    me.uv_textures.new("UVMap")
    for p in me.polygons:
        for loop_index, c in zip(p.loop_indices, ((0, 0), (1, 0), (1, 1), (0, 1))):
            me.uv_layers[0].data[loop_index].uv = c
    me.update()
    me.materials.append(ma)
    ob = bpy.data.objects.new(name, me)
    ob.location = location
    scene.objects.link(ob)
    ob.game.physics_type = 'NO_COLLISION'
    return ob


def classic(name, img=None, ref=0.8, colour=(0.8, 0.8, 0.8)):
    ma = m9.classic_material(name, colour)
    ma.diffuse_intensity = ref
    ma.specular_intensity = 0.05
    if img is not None:
        m9.slot_image(ma, img, color=True)
    return ma


def physical(name, col, metallic, rough):
    return m9.physical(name, col, metallic, rough)


def lights(scene, black_sky=False):
    sun = p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 6.0), (math.radians(55.0), 0.0, math.radians(15.0)), energy=1.0)
    sun.data.shadow_method = 'NOSHADOW'
    if black_sky:
        w = scene.world
        w.use_sky_blend = False
        w.horizon_color = (0.0, 0.0, 0.0)
        w.zenith_color = (0.0, 0.0, 0.0)
        w.ambient_color = (0.0, 0.0, 0.0)
    return sun


def finish(scene):
    m9.logic(scene, "P9Study", m9.STUDY_SCRIPT)
    p2.add_probe(scene)
    for sc in bpy.data.scenes:
        sc["bge_remix_pipeline"] = 1
    scene.update()


# ---------------------------------------------------------------------------
# the scenes

def make_audit(folder, E):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = m9.base("P10Audit")
    lights(scene)
    want = {"kinds": {}, "density": {}, "missing": {}, "painted": {}, "values": {}, "plans": {}}
    # the kinds
    for label, arr, kind, plan in (("noise", noise_image(), 'RANDOM', 'FIT'), ("brick", brick_image(), 'REGULAR', 'FIT'),
                                   ("sign", sign_image(), 'UNIQUE', 'DERIVE')):
        img = bl_image("P10A_" + label, arr)
        ma = classic("P10A_" + label, img)
        plane(scene, "P10A_" + label, 2.0, 2.0, (len(want["kinds"]) * 2.5 - 2.5, 0.0, 2.5), ma)
        want["kinds"][img.name] = kind
        want["plans"][img.name] = plan
    # the densities: 64 px on a 10 m wall (UV 0..1), 2048 px on a 1 m box
    small = bl_image("P10A_density64", noise_image(64, seed=5))
    plane(scene, "P10A_wall10m", DENSITY_WALL, DENSITY_WALL, (0.0, 8.0, 0.0), classic("P10A_density64", small))
    big = bl_image("P10A_density2048", noise_image(2048, seed=6))
    uv_box(scene, "P10A_box1m", DENSITY_BOX, (6.0, 0.0, 0.0), classic("P10A_density2048", big))
    want["density"][small.name] = 64.0 / DENSITY_WALL
    want["density"][big.name] = 2048.0 / DENSITY_BOX
    # missing channels: colour only, and complete (colour, roughness, normal)
    only = bl_image("P10A_colour_only", noise_image(128, seed=7))
    plane(scene, "P10A_colour_only", 1.0, 1.0, (-4.0, 0.0, 0.0), classic("P10A_colour_only", only))
    full = bl_image("P10A_complete", noise_image(128, seed=8))
    rough = bl_image("P10A_complete_rough", np.repeat((0.5 + 0.2 * noise(128, 2.0, 9))[..., None], 3, axis=2),
                     non_color=True)
    nrm = bl_image("P10A_complete_normal", np.zeros((128, 128, 3)) + np.array((0.5, 0.5, 1.0)), non_color=True)
    ma = classic("P10A_complete", full)
    s = m9.slot_image(ma, rough)
    s.use_map_color_diffuse = False
    s.use_map_hardness = True
    m9.slot_image(ma, nrm, normal=True)
    plane(scene, "P10A_complete", 1.0, 1.0, (-5.5, 0.0, 0.0), ma)
    want["missing"][only.name] = ["roughness", "normal", "height"]
    want["missing"][full.name] = ["height"]
    # painted light: a flat-lit and a side-lit copy (dark on the left)
    flat = noise_image(seed=11)
    img_flat = bl_image("P10A_flat_lit", flat)
    img_side = bl_image("P10A_side_lit", side_lit(flat))
    plane(scene, "P10A_flat_lit", 2.0, 2.0, (-2.5, 0.0, -2.5), classic("P10A_flat_lit", img_flat))
    plane(scene, "P10A_side_lit", 2.0, 2.0, (0.0, 0.0, -2.5), classic("P10A_side_lit", img_side))
    want["painted"] = {img_flat.name: None, img_side.name: "left"}
    # the value check: out of range, and fine
    vals = [("too_bright", classic("P10V_too_bright", None, ref=1.0, colour=(1.0, 1.0, 1.0)), "albedo"),
            ("too_dark", classic("P10V_too_dark", None, ref=0.8, colour=(0.01, 0.01, 0.01)), "albedo"),
            ("half_metal", physical("P10V_half_metal", (0.6, 0.6, 0.6), 0.5, 0.4), "metallic"),
            ("dark_metal", physical("P10V_dark_metal", (0.1, 0.1, 0.1), 1.0, 0.3), "metallic"),
            ("mirror", physical("P10V_mirror", (0.5, 0.5, 0.5), 0.0, 0.0), "roughness"),
            ("fine", physical("P10V_fine", (0.5, 0.5, 0.5), 0.0, 0.5), None)]
    for i, (label, ma, field) in enumerate(vals):
        uv_box(scene, "P10V_" + label, 0.5, (-3.0 + i * 0.8, 0.0, -5.0), ma)
        want["values"][ma.name] = field
    finish(scene)
    path = os.path.join(folder, "p10a_audit.blend")
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, compress=False)
    want["file"] = "p10a_audit"
    want["side_flat"] = [img_side.name, img_flat.name]
    want["sign"] = "P10A_sign"
    E["p10a"]["audit"] = want


def make_tiles(folder, E):
    """the presets' tiles at large size (as P9.6's seams squares)"""
    from bl_operators.bge_remix_presets import apply_preset
    scene = s96.scene_base("P10Tiles")
    seams = {}
    k = len(TILE_PRESETS)
    size, rep = s96.SEAM_SIZE, s96.SEAM_REPEAT
    for i, key in enumerate(TILE_PRESETS):
        ma = bpy.data.materials.new("P10T_" + key)
        apply_preset(ma, key)
        # (P10.4, Ayhan's 7 Nov default: a random preset gets 16 tiles in
        # Remix; these tests are about the single tile, so it is chosen)
        ma["bge_remix_atlas"] = 0
        x = (i - (k - 1) / 2.0) * (size + 0.4)
        s96.uv_square(scene, "P10T_" + key, size, (x, 0.0, 0.0), ma, repeat=float(rep))
        s = size / 2.0
        rect = [s96.screen_x(x - s * 0.95), s96.screen_y(s * 0.95), s96.screen_x(x + s * 0.95),
                s96.screen_y(-s * 0.95)]
        cols = [s96.screen_x(x - s + j * size / rep) for j in range(1, rep)]
        rows = [s96.screen_y(-s + j * size / rep) for j in range(1, rep)]
        # where each tile's cross-faded band lies on screen (its last 1/8,
        # before each boundary), and its middle
        band = size / rep * 0.125
        bands = [[s96.screen_x(x - s + j * size / rep - band), s96.screen_x(x - s + j * size / rep)]
                 for j in range(1, rep)]
        middles = [[s96.screen_x(x - s + (j - 0.6) * size / rep), s96.screen_x(x - s + (j - 0.4) * size / rep)]
                   for j in range(1, rep)]
        seams[key] = {"material": ma.name, "rect": rect, "columns": cols, "rows": rows, "bands": bands,
                      "middles": middles, "mode": int(ma.get("bge_remix_tile_mode", 0))}
    s96.finish(scene)
    files = [m9.save_copy(folder, "p10a_tiles", remix=0, tone=1), m9.save_copy(folder, "p10a_tiles_remix", remix=1,
                                                                              tone=1)]
    E["p10a"]["tiles"] = {"files": files, "seams": seams, "size": [m9.WIDTH, m9.HEIGHT]}


def make_classic(folder, E):
    """P10.3's brightness check: Classic tiles with a colour image"""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = m9.base("P10Classic")
    img = bl_image("P10C_image", noise_image(256, seed=21))
    face = bl_image("P10C_face_image", noise_image(256, seed=21) * np.array((1.0, 0.95, 0.9)))
    rects, tiles = {}, {}
    half = 0.9
    for i, (label, ref, use_face, matte) in enumerate(CLASSIC_TILES):
        ma = classic("P10C_" + label, None if use_face else img, ref=ref)
        ma.specular_hardness = 20 + 20 * i
        if matte:
            ma.specular_intensity = 0.0
        x = (i - (len(CLASSIC_TILES) - 1) / 2.0) * 2.1
        ob = plane(scene, "P10C_" + label, half * 2.0, half * 2.0, (x, 0.0, 0.0), ma)
        if use_face:
            ma.use_face_texture = True
            for d in ob.data.uv_textures.active.data:
                d.image = face
        rects[label] = m9.layout_rect(x, 0.0, half)
        tiles[label] = {"material": ma.name, "ref": ref, "face": use_face, "matte": matte,
                        "roughness": 1.0 if matte else (2.0 / (ma.specular_hardness + 2.0)) ** 0.25}
    lights(scene, black_sky=True)
    finish(scene)
    files = [m9.save_copy(folder, "p10a_classic", remix=0, tone=1),
             m9.save_copy(folder, "p10a_classic_remix", remix=1, tone=1)]
    E["p10a"]["classic"] = {"files": files, "rects": rects, "tiles": tiles, "order": [t[0] for t in CLASSIC_TILES],
                            "size": [m9.WIDTH, m9.HEIGHT]}


# P10.2: each preset baked at these settings (by the fit probe) is fitted
# again; the tolerances per setting (to be tuned on the first run)
FIT_SETTINGS = {"Scale": 2.6, "Wear": 0.5, "Wetness": 0.5, "Seed": 7.0}
FIT_SCALE_BRICK = 2.0
FIT_TINT = (1.1, 1.0, 0.9)   # (the brightest preset, Snow, stays under 1 times its pattern)
FIT_TOL = {"Scale": 0.25, "Wear": 0.25, "Wetness": 0.3, "colour": 0.04}
FAMILIES = {'SKIN_LIGHT': "skin", 'SKIN_MEDIUM': "skin", 'SKIN_DARK': "skin"}
# fix round 1 (the full catalogue, about 3,800 bakes, timed out the PC's
# run): the tests' catalogue is three presets of 9 entries, the fit's search
# short; two presets are fitted again
TEST_CATALOGUE = {"BGE_REMIX_REMASTER_PRESETS": "BRICK,ROCK,CONCRETE", "BGE_REMIX_REMASTER_ENTRIES": "9",
                  "BGE_REMIX_REMASTER_ITERATIONS": "4", "BGE_REMIX_REMASTER_CANDIDATES": "1"}
FIT_PRESETS = ['BRICK', 'ROCK']
# 6 Nov (Ayhan's decision): the catalogue built on first use for the likely
# presets only; the test's entries few (every preset may be built)
LAZY_CATALOGUE = {"BGE_REMIX_REMASTER_ENTRIES": "3", "BGE_REMIX_REMASTER_ITERATIONS": "2",
                  "BGE_REMIX_REMASTER_CANDIDATES": "1"}


def make_fit(folder, E):
    """the fit probe's scene: a plain game scene (the bakes need one) and a
    sign, which must not fit"""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = m9.base("P10Fit")
    lights(scene)
    sign = bl_image("P10F_sign", sign_image(seed=41))
    sign.use_fake_user = True
    # (6 Nov: the lazy catalogue's noise image, a random kind)
    lazy = bl_image("P10F_noise", noise_image(256, seed=43))
    lazy.use_fake_user = True
    finish(scene)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(folder, "p10a_fit.blend"), check_existing=False, compress=False)
    E["p10a"]["fit"] = {"file": "p10a_fit", "sign": sign.name, "settings": FIT_SETTINGS, "scale_brick": FIT_SCALE_BRICK,
                        "tint": FIT_TINT, "tolerance": FIT_TOL, "families": FAMILIES, "presets": FIT_PRESETS,
                        "catalogue_env": TEST_CATALOGUE, "lazy_image": lazy.name,
                        "lazy_env": LAZY_CATALOGUE}


def make_encode(folder, E):
    """P10.2's open question: a Brick preset at 1K and at 2K, alone, for the
    encode time with Remix running"""
    from bl_operators.bge_remix_presets import apply_preset
    files = {}
    for size in (1024, 2048):
        scene = s96.scene_base("P10Encode%d" % size)
        ma = bpy.data.materials.new("P10E_brick_%d" % size)
        g = apply_preset(ma, 'BRICK')
        g.inputs["Scale"].default_value = 2.0
        ma["bge_remix_bake_size"] = size
        from bl_operators.bge_remix_material import remix_node
        remix_node(ma).bake_size = float(size)
        s96.uv_square(scene, "P10E_brick", 4.0, (0.0, 0.0, 0.0), ma, repeat=2.0)
        s96.finish(scene)
        files[str(size)] = m9.save_copy(folder, "p10a_encode%d_remix" % size, remix=1, tone=1)
    E["p10a"]["encode"] = {"files": files}


def make_apply(folder, E):
    """P10.5: Apply's scene, in its own folder (its remaster folder and
    cache), opened in a Remix View"""
    bpy.ops.wm.read_factory_settings(use_empty=False)
    scene = bpy.context.scene
    for ob in list(scene.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    scene.name = "P10Apply"
    scene.render.engine = 'BLENDER_GAME'
    gs = scene.game_settings
    gs.resolution_x, gs.resolution_y = m9.WIDTH, m9.HEIGHT
    gs.material_mode = 'GLSL'
    w = bpy.data.worlds.new("World")
    w.horizon_color = (0.45, 0.5, 0.6)
    scene.world = w
    cam = bpy.data.cameras.new("Camera")
    ob = bpy.data.objects.new("Camera", cam)
    ob.location = (0.0, -9.0, 1.2)
    ob.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    scene.objects.link(ob)
    scene.camera = ob
    lights(scene)
    walls = {}
    for i, (label, arr, plan) in enumerate((("noise", noise_image(seed=31), 'FIT'),
                                            ("brick", brick_image(seed=32), 'FIT'),
                                            ("sign", sign_image(seed=33), 'DERIVE'))):
        img = bl_image("P10P_" + label, arr)
        ma = classic("P10P_" + label, img)
        plane(scene, "P10P_" + label, 2.0, 2.0, ((i - 1) * 2.4, 0.0, 1.2), ma)
        walls[img.name] = {"material": ma.name, "plan": plan}
    floor = classic("P10P_floor", None, colour=(0.4, 0.4, 0.4))
    plane(scene, "P10P_floor", 12.0, 12.0, (0.0, 0.0, 0.0), floor, facing='Z')
    finish(scene)
    scene["bge_remix"] = 1
    scene["bge_remix_tonemap"] = 1
    import p97_scenes as s97
    areas = s97.remix_view_areas()
    areas.sort(key=lambda sa: sa[0] != "Default")
    for _screen, area in areas[:1]:
        space = area.spaces.active
        space.is_game_view = True
        space.game_view_mode = 'CHECK'
        space.region_3d.view_perspective = 'CAMERA'
    sub = os.path.join(folder, "apply")
    os.makedirs(sub, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(sub, "p10a_apply_remix.blend"), check_existing=False,
                                compress=False)
    E["p10a"]["apply"] = {"file": os.path.join("apply", "p10a_apply_remix"), "walls": walls}


UPGRADE_SETTINGS = {"Scale": 3.0, "Wear": 0.7, "Seed": 5.0}
UPGRADE_COLOUR = (0.30, 0.12, 0.08)
UPGRADE_METRES = 2.0


def _downgrade_bricks(ng):
    """the version 2 "Remix Bricks" group made as P9.6 made it (version 1):
    its value per brick by Modulo(v, 1)"""
    gout = next(n for n in ng.nodes if n.bl_idname == "NodeGroupOutput")
    frac = gout.inputs["Brick"].links[0].from_node          # v - floor(v)
    v = frac.inputs[0].links[0].from_socket
    rnd = frac.inputs[1].links[0].from_node                  # floor(v) = round(v - 0.5)
    shift = rnd.inputs[0].links[0].from_node
    mod = ng.nodes.new("ShaderNodeMath")
    mod.operation = 'MODULO'
    ng.links.new(v, mod.inputs[0])
    mod.inputs[1].default_value = 1.0
    ng.links.new(mod.outputs[0], gout.inputs["Brick"])
    for n in (frac, rnd, shift):
        ng.nodes.remove(n)
    ng["bge_remix_preset_version"] = 1


def make_upgrade(folder, E):
    """6 Nov (Ayhan's decision): a P9-style file, a Brick preset of version 1
    (its black brick) with its own settings, made as P9.6 saved it"""
    from bl_operators.bge_remix_presets import apply_preset, BRICKS_GROUP
    scene = s96.scene_base("P10Upgrade")
    ma = bpy.data.materials.new("P10U_brick")
    g = apply_preset(ma, 'BRICK')
    for k, v in UPGRADE_SETTINGS.items():
        g.inputs[k].default_value = v
    g.inputs["Colour"].default_value = UPGRADE_COLOUR + (1.0,)
    ma["bge_remix_tile_metres"] = UPGRADE_METRES
    del ma["bge_remix_tile_mode"]            # (P9.6 had no tile modes)
    _downgrade_bricks(bpy.data.node_groups[BRICKS_GROUP])
    g.node_tree["bge_remix_preset_version"] = 1
    s96.uv_square(scene, "P10U_brick", 2.0, (0.0, 0.0, 0.0), ma, repeat=2.0)
    s96.finish(scene)
    m9.save_copy(folder, "p10a_upgrade_v1", remix=0, tone=1)
    E["p10a"]["upgrade"] = {"file": "p10a_upgrade_v1", "material": ma.name, "group": g.node_tree.name,
                            "bricks": BRICKS_GROUP, "settings": UPGRADE_SETTINGS, "colour": list(UPGRADE_COLOUR),
                            "metres": UPGRADE_METRES}


INTENSITY_TILES = (("img050", 0.5, True), ("img100", 1.0, True), ("col050", 0.5, False))


def make_intensity(folder, E):
    """6 Nov (Ayhan's decision): Classic materials to be made RTX Remix (the
    test does it): a colour image at Diffuse Intensity 0.5 and 1.0, and a
    plain colour at 0.5, under a sun and a black sky"""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = m9.base("P10Intensity")
    img = bl_image("P10I_image", noise_image(256, seed=51))
    rects, tiles = {}, {}
    for i, (label, ref, use_img) in enumerate(INTENSITY_TILES):
        ma = classic("P10I_" + label, img if use_img else None, ref=ref, colour=(0.6, 0.55, 0.5))
        x = (i - 1) * 2.6
        plane(scene, "P10I_" + label, 2.0, 2.0, (x, 0.0, 0.0), ma)
        rects[label] = m9.layout_rect(x, 0.0, 1.0)
        tiles[label] = {"material": ma.name, "ref": ref, "image": use_img, "colour": [0.6, 0.55, 0.5]}
    lights(scene, black_sky=True)
    finish(scene)
    m9.save_copy(folder, "p10a_intensity", remix=0, tone=1)
    E["p10a"]["intensity"] = {"file": "p10a_intensity", "rects": rects, "tiles": tiles,
                              "order": [t[0] for t in INTENSITY_TILES]}


def make(folder, E):
    E["p10a"] = {"failed": {}}
    import traceback
    for name, fn in (("audit", make_audit), ("tiles", make_tiles), ("classic", make_classic), ("fit", make_fit),
                     ("encode", make_encode), ("apply", make_apply), ("upgrade", make_upgrade),
                     ("intensity", make_intensity)):
        try:
            fn(folder, E)
        except Exception:
            E["p10a"]["failed"][name] = traceback.format_exc()
            print("BGE-Remix p10a: making the %s scene failed:\n%s" % (name, E["p10a"]["failed"][name]))
