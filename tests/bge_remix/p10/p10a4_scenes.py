"""BGE-Remix p10a4: the scenes for the texture remaster's atlases (P10.4).

make(folder, E) runs inside our Blender (from make_scenes_p10.py), saves its
.blend files into folder and writes what the tests expect into E["p10a4"].

All under one sun, an orthographic camera looking straight at them (80
pixels a metre at 960 x 540), as P9.6's seams scene:

p10a4_floor_plain       a large Rock floor (12.4 x 7 m, one UV unit a
                        metre: the preset's tile repeats every metre), its
                        plain tile in Remix (the repeat to beat)
p10a4_floor_tiles16     the same floor, its material a 16-tile set (two
                        corner colours); a small Brick square in the corner
                        as a 16-tile set too (a regular pattern)
p10a4_floor_tiles81     the same floor as an 81-tile set (three colours)
p10a4_water             a water square (P9.6's Water preset) as a 16-frame
                        flipbook, in front of a striped backdrop (the glass
                        refracts it, so the waves show)
p10a4_images_near       twenty small generated images (32 to 128 pixels,
                        each its own colour and pattern) on twenty squares,
                        four of them repeating (UV 0..2), one with Extend and
                        UVs past its edge; not packed
p10a4_packed_near       the same, packed into pages (pack_images)
p10a4_images_far        the twenty, seen from far away (a perspective
                        camera: each square a few pixels, the pages' far
                        mip levels)
p10a4_packed_far        the same, packed
p10a4_pick              eight floor faces (each one UV square) with the
                        16-tile Rock set; nothing picked by hand (the tests
                        pick a tile for face 5 with the UV editor's
                        operator, save and reopen)

Each Remix file also has its "_remix" twin (RTX Remix on); the file names
above are Remix off.
"""

import math
import os

import bpy

import make_scenes_p9 as m9
import p96_scenes as s96

p2, p3 = m9.p2, m9.p3

FLOOR_W, FLOOR_H = 12.4, 7.0      # metres (one UV unit a metre)
PX_PER_M = m9.WIDTH / m9.ORTHO    # 80
TILE_SIZE = 256                   # a tile's pixels in these files (the tests' bake size too)
WATER_FRAMES = 16
IMAGES = 20
PICK_FACE, PICK_TILE, PICK_OTHER = 5, 7, 12
PACK_PAGE, PACK_PADDING = 1024, 16
FAR_DISTANCE = 30.0               # the far camera (metres; it sees to 1000 m)

SHOTS_SCRIPT = r'''
# BGE-Remix P10.4: pictures over time (the flipbook): from AFTER seconds
# after Remix's first frame, one every GAP seconds into the SHOTS paths
import os, time
import bge
env = os.environ
shots = [s for s in env.get("BGE_REMIX_P10A4_SHOTS", "").split(";") if s]
g = bge.logic.globalDict
st = g.setdefault("p10a4", {"start": None, "taken": 0})
if shots and bge.render.isRemixDrawing() and st["start"] is None:
    st["start"] = time.perf_counter()
if shots and st["start"] is not None and st["taken"] < len(shots):
    after = float(env.get("BGE_REMIX_P10A4_AFTER", "30"))
    gap = float(env.get("BGE_REMIX_P10A4_GAP", "0.4"))
    if time.perf_counter() - st["start"] >= after + gap * st["taken"]:
        bge.render.makeScreenshot(shots[st["taken"]])
        st["taken"] += 1
'''


def screen_rect(x0, z0, x1, z1):
    """a rectangle of the view (metres) on screen (left, top, right, bottom)"""
    return [s96.screen_x(x0), s96.screen_y(z1), s96.screen_x(x1), s96.screen_y(z0)]


def quad(scene, name, x0, z0, x1, z1, ma, uv0=(0.0, 0.0), uv1=(1.0, 1.0)):
    """a rectangle facing the camera (-Y) at y 0, its UVs uv0..uv1"""
    me = bpy.data.meshes.new(name)
    me.from_pydata([(x0, 0.0, z0), (x1, 0.0, z0), (x1, 0.0, z1), (x0, 0.0, z1)], [], [(0, 1, 2, 3)])
    me.uv_textures.new("UVMap")
    for loop, uv in zip(me.uv_layers[0].data, ((uv0[0], uv0[1]), (uv1[0], uv0[1]), (uv1[0], uv1[1]),
                                               (uv0[0], uv1[1]))):
        loop.uv = uv
    me.update()
    me.materials.append(ma)
    ob = bpy.data.objects.new(name, me)
    scene.objects.link(ob)
    ob.game.physics_type = 'NO_COLLISION'
    return ob


def faces_floor(scene, name, cols, rows, size, ma):
    """cols x rows faces, each one UV square (u, v the face's column and
    row), centred on the view"""
    verts, faces, uvs = [], [], []
    x0, z0 = -cols * size / 2.0, -rows * size / 2.0
    for r in range(rows):
        for c in range(cols):
            k = len(verts)
            verts += [(x0 + c * size, 0.0, z0 + r * size), (x0 + (c + 1) * size, 0.0, z0 + r * size),
                      (x0 + (c + 1) * size, 0.0, z0 + (r + 1) * size), (x0 + c * size, 0.0, z0 + (r + 1) * size)]
            faces.append((k, k + 1, k + 2, k + 3))
            uvs.append(((c, r), (c + 1, r), (c + 1, r + 1), (c, r + 1)))
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.uv_textures.new("UVMap")
    for p, uv in zip(me.polygons, uvs):
        for li, q in zip(p.loop_indices, uv):
            me.uv_layers[0].data[li].uv = q
    me.update()
    me.materials.append(ma)
    ob = bpy.data.objects.new(name, me)
    scene.objects.link(ob)
    ob.game.physics_type = 'NO_COLLISION'
    rects = []
    for r in range(rows):
        for c in range(cols):
            m = size * 0.15
            rects.append(screen_rect(x0 + c * size + m, z0 + r * size + m, x0 + (c + 1) * size - m,
                                     z0 + (r + 1) * size - m))
    return ob, rects


def preset(name, key):
    from bl_operators.bge_remix_presets import apply_preset
    ma = bpy.data.materials.new(name)
    apply_preset(ma, key)
    return ma


def finish(scene, shots=False):
    if shots:
        m9.logic(scene, "P10A4Shots", SHOTS_SCRIPT)
    s96.finish(scene)


def save_pair(folder, name, files):
    """the file with Remix off, and its _remix twin (ACES, as P9.6's seams)"""
    files.append(m9.save_copy(folder, name, remix=0, tone=1))
    files.append(m9.save_copy(folder, name + "_remix", remix=1, tone=1))


def floor_scene(name, mode):
    """the Rock floor: plain tile, 16 or 81 tiles"""
    from bge_remix import atlas
    scene = s96.scene_base(name)
    ma = preset("P10A4_rock", 'ROCK')
    info = None
    if mode != "plain":
        info = atlas.bake_tile_set(ma, TILE_SIZE, 2 if mode == "tiles16" else 3)
    hw, hh = FLOOR_W / 2.0, FLOOR_H / 2.0
    # one UV unit a metre, the UVs' 0 at the floor's corner
    quad(scene, "P10A4_floor", -hw, -hh, hw, hh, ma, (0.0, 0.0), (FLOOR_W, FLOOR_H))
    brick = None
    if mode == "tiles16":
        bma = preset("P10A4_brick", 'BRICK')
        brick = atlas.bake_tile_set(bma, TILE_SIZE, 2)
        # a 2 x 1 m Brick patch in front of the floor's top-left corner
        ob = quad(scene, "P10A4_brick", -hw + 0.2, hh - 1.2, -hw + 2.2, hh - 0.2, bma, (0.0, 0.0), (2.0, 1.0))
        ob.location.y = -0.05
    finish(scene)
    return scene, ma, info, brick


def make_floors(folder, E, files):
    from bge_remix import atlas
    out = {}
    for mode in ("plain", "tiles16", "tiles81"):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        scene, ma, info, brick = floor_scene("P10A4Floor_" + mode, mode)
        save_pair(folder, "p10a4_floor_" + mode, files)
        out[mode] = {"file": "p10a4_floor_" + mode, "material": ma.name,
                     "atlas": {k: v for k, v in (info or {}).items() if k != "preview"} if info else None,
                     "brick": {k: v for k, v in (brick or {}).items() if k != "preview"} if brick else None}
    hw, hh = FLOOR_W / 2.0, FLOOR_H / 2.0
    # the floor's inside on screen (the Brick patch's corner left out), and
    # where its tiles meet
    rect = screen_rect(-hw + 0.3, -hh + 0.3, hw - 0.3, hh - 1.5)
    cols = [s96.screen_x(-hw + k) for k in range(1, int(FLOOR_W))]
    rows = [s96.screen_y(-hh + k) for k in range(1, int(FLOOR_H) - 1)]
    out.update({"rect": rect, "columns": cols, "rows": rows, "period_px": PX_PER_M, "size": [FLOOR_W, FLOOR_H],
                "tile_size": TILE_SIZE, "regular": sorted(atlas.REGULAR)})
    return out


def make_water(folder, E, files):
    from bge_remix import atlas
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = s96.scene_base("P10A4Water")
    # a striped backdrop behind the water (its refraction moves with the waves)
    stripes = p3.image("P10A4_stripes", lambda x, y: (0.8, 0.8, 0.75, 1.0) if int(x * 16) % 2 else
                       (0.1, 0.15, 0.3, 1.0), size=256)
    back = bpy.data.materials.new("P10A4_stripes")
    back.diffuse_color = (1.0, 1.0, 1.0)
    back["bge_physical"] = 1
    back["bge_roughness"] = 0.7
    m9.slot_image(back, stripes, color=True)
    ob = quad(scene, "P10A4_backdrop", -6.0, -3.4, 6.0, 3.4, back, (0.0, 0.0), (3.0, 1.7))
    ob.location.y = 1.0
    ma = preset("P10A4_water", 'WATER')
    info = atlas.bake_flipbook(ma, WATER_FRAMES)
    # the water: 4 x 4 m, 2 x 2 tiles
    quad(scene, "P10A4_water", -2.0, -2.0, 2.0, 2.0, ma, (0.0, 0.0), (2.0, 2.0))
    finish(scene, shots=True)
    save_pair(folder, "p10a4_water", files)
    return {"file": "p10a4_water", "material": ma.name, "flipbook": {k: v for k, v in info.items() if k != "preview"},
            "rect": screen_rect(-1.7, -1.7, 1.7, 1.7), "backdrop_rect": screen_rect(3.0, -3.0, 5.5, 3.0)}


def _image_pixel(k):
    """the k-th image's pattern: its own hue, stripes or a checker or rings"""
    import colorsys
    r, g, b = colorsys.hsv_to_rgb((k * 0.137) % 1.0, 0.75, 0.9)
    dark = (r * 0.25, g * 0.25, b * 0.25, 1.0)
    light = (r, g, b, 1.0)
    kind = k % 3

    def px(x, y):
        if kind == 0:
            on = int(x * 4) % 2 == 0
        elif kind == 1:
            on = (int(x * 4) + int(y * 4)) % 2 == 0
        else:
            on = int(math.hypot(x - 0.5, y - 0.5) * 8) % 2 == 0
        # a letter-like mark in one corner, so a flip would show
        if x < 0.25 and y > 0.75:
            on = not on
        return light if on else dark
    return px


IMAGE_SIZES = [32, 64, 128, 48, 96]


def images_scene(name, far):
    """twenty squares, five rows of four, each its own image"""
    scene = s96.scene_base(name)
    mats, rects, info = [], {}, {}
    for k in range(IMAGES):
        size = IMAGE_SIZES[k % len(IMAGE_SIZES)]
        img = p3.image("P10A4_img%02d" % k, _image_pixel(k), size=size)
        ma = bpy.data.materials.new("P10A4_img%02d" % k)
        ma.diffuse_color = (1.0, 1.0, 1.0)
        ma["bge_physical"] = 1
        ma["bge_roughness"] = 0.8
        slot = m9.slot_image(ma, img, color=True)
        uv0, uv1, how = (0.0, 0.0), (1.0, 1.0), "plain"
        if k in (3, 8, 13, 18):
            uv0, uv1, how = (0.0, 0.0), (2.0, 2.0), "repeat"
        if k == 11:
            slot.texture.extension = 'EXTEND'
            uv0, uv1, how = (-0.25, -0.25), (1.25, 1.25), "extend"
        row, col = divmod(k, 5)
        x, z = (col - 2.0) * 2.2, (1.5 - row) * 1.6
        quad(scene, "P10A4_sq%02d" % k, x - 0.7, z - 0.7, x + 0.7, z + 0.7, ma, uv0, uv1)
        rects["sq%02d" % k] = screen_rect(x - 0.55, z - 0.55, x + 0.55, z + 0.55)
        info["sq%02d" % k] = {"material": ma.name, "image": img.name, "size": size, "uv": how}
        mats.append(ma)
    if far:
        cam = scene.camera
        cam.data.type = 'PERSP'
        cam.data.lens = 50.0
        cam.data.clip_end = 1000.0
        cam.location = (0.0, -FAR_DISTANCE, 0.0)
    return scene, mats, rects, info


def make_images(folder, E, files):
    from bge_remix import atlas
    out = {}
    for far in (False, True):
        tag = "far" if far else "near"
        for packed in (False, True):
            bpy.ops.wm.read_factory_settings(use_empty=True)
            scene, mats, rects, info = images_scene("P10A4Images_" + tag, far)
            if packed:
                imgs = [bpy.data.images["P10A4_img%02d" % k] for k in range(IMAGES)]
                r = atlas.pack_images(imgs, PACK_PAGE, PACK_PADDING, max_size=128, scene=scene)
                pages = [bpy.data.images[n] for n in r["pages"]]
                r["checks"] = {p.name: atlas.check_page(p) for p in pages}
                # where the gap is used up (the tool warns when its camera
                # sees that far: the far camera, to 1000 m, does; the near,
                # orthographic one at 80 pixels a metre does not)
                dens = [atlas._texel_density(bpy.data.objects["P10A4_sq%02d" % k], 0,
                                             bpy.data.images["P10A4_img%02d" % k]) for k in range(IMAGES)]
                d = [atlas._bleed_distance(scene, x, PACK_PADDING) for x in dens if x]
                d = [x for x in d if x is not None]
                r["bleed_distance"] = min(d) if d else None
                r["far_distance"] = FAR_DISTANCE
                r["clip_end"] = scene.camera.data.clip_end
                out[tag + "_packed"] = r
            finish(scene)
            name = "p10a4_%s_%s" % ("packed" if packed else "images", tag)
            save_pair(folder, name, files)
            out.setdefault("files", {})[name] = info
            out["rects_" + tag] = rects
    out["count"] = IMAGES
    return out


def make_pick(folder, E, files):
    from bge_remix import atlas
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = s96.scene_base("P10A4Pick")
    ma = preset("P10A4_rock", 'ROCK')
    info = atlas.bake_tile_set(ma, TILE_SIZE, 2)
    ob, rects = faces_floor(scene, "P10A4_pickfloor", 4, 2, 2.0, ma)
    finish(scene)
    save_pair(folder, "p10a4_pick", files)
    return {"file": "p10a4_pick", "object": ob.name, "mesh": ob.data.name, "material": ma.name, "face": PICK_FACE,
            "tile": PICK_TILE, "other_tile": PICK_OTHER, "faces": 8, "cols": 4, "rects": rects,
            "seed": info["seed"], "colours": info["colours"],
            # each face's UV square (the face's column and row)
            "squares": [[k % 4, k // 4] for k in range(8)]}


def make(folder, E):
    files = []
    E["p10a4"] = {}
    for part, fn in (("floors", make_floors), ("water", make_water), ("images", make_images), ("pick", make_pick)):
        try:
            E["p10a4"][part] = fn(folder, E, files)
        except Exception:
            import traceback
            E.setdefault("failed", {})["p10a4 " + part] = traceback.format_exc()
            print("BGE-Remix: making the p10a4 %s failed:\n%s" % (part, E["failed"]["p10a4 " + part]))
    E["p10a4"]["files"] = files
    E["p10a4"]["size"] = [m9.WIDTH, m9.HEIGHT]
    E["p10a4"]["folder"] = os.path.abspath(folder)
