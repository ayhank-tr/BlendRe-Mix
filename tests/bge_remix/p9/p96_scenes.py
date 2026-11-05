"""BGE-Remix P9.6: the preset materials' scenes (run inside our Blender,
background, from make_scenes_p9's main: make(folder, E)).

p9_presets(_remix, _aces, _aces_remix)  every preset (14: twelve, skin in
                              three tones) as a tile (a square facing the
                              camera, UV 0..1: one tile) in the top row and a
                              sphere below it, on a grey backdrop, under one
                              sun and the world's sky; orthographic camera
p9_presets_seams_aces(_remix) four presets (Rock, Brick, Grass, Concrete) on
                              large squares, UV 0..3 (three tiles across and
                              up): where the tiles meet, Remix's baked tile
                              repeats; the game draws the procedurals through

E["presets"]: the files, each tile's and sphere's rectangle on screen, the
materials, the presets' table values, and the seams' squares with the
pixel columns and rows where the tiles meet.
"""

import math

import bpy

import make_scenes_p9 as m9

p2, p3 = m9.p2, m9.p3

TILE = 0.72            # a tile's side (m)
TILE_Z, SPHERE_Z = 1.3, -1.0
SEAM_PRESETS = ('ROCK', 'BRICK', 'GRASS', 'CONCRETE')
SEAM_SIZE, SEAM_REPEAT = 2.9, 3


def uv_square(scene, name, size, location, ma, repeat=1.0):
    """a square facing the camera (-Y), its UVs 0..repeat (u to the right, v
    up)"""
    s = size / 2.0
    me = bpy.data.meshes.new(name)
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    me.uv_textures.new("UVMap")
    for loop, uv in zip(me.uv_layers[0].data, ((0, 0), (repeat, 0), (repeat, repeat), (0, repeat))):
        loop.uv = uv
    me.update()
    me.materials.append(ma)
    ob = bpy.data.objects.new(name, me)
    ob.location = location
    ob.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    scene.objects.link(ob)
    ob.game.physics_type = 'NO_COLLISION'
    return ob


def screen_x(x):
    return int(round((x / m9.ORTHO + 0.5) * m9.WIDTH))


def screen_y(z):
    return int(round((0.5 - z / m9.VIEW_H) * m9.HEIGHT))


def scene_base(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = m9.base(name)
    back = bpy.data.materials.new("P9PR_backdrop")
    back.diffuse_color = (0.35, 0.35, 0.35)
    back["bge_physical"] = 1
    back["bge_roughness"] = 0.8
    wall = p2.plane(scene, "P9PR_backdrop", 30.0, (0.0, 3.0, 0.0), (math.radians(90.0), 0.0, 0.0), back)
    wall.game.physics_type = 'NO_COLLISION'
    sun = p2.lamp(scene, "Sun", 'SUN', (0.0, -10.0, 6.0), (math.radians(50.0), 0.0, math.radians(-30.0)),
                  energy=1.0)
    sun.data.shadow_soft_size = 0.2
    sun.data.shadow_method = 'RAY_SHADOW'
    return scene


def finish(scene):
    m9.logic(scene, "P9Study", m9.STUDY_SCRIPT)
    p2.add_probe(scene)
    for sc in bpy.data.scenes:
        sc["bge_remix_pipeline"] = 1
        sc["bge_remix"] = 0
    scene.update()


def make(folder, E):
    from bl_operators.bge_remix_presets import PRESETS, ORDER, apply_preset
    scene = scene_base("P9Presets")
    n = len(ORDER)
    step = m9.ORTHO / (n + 0.5)
    rects, mats, groups = {}, {}, {}
    for i, key in enumerate(ORDER):
        ma = bpy.data.materials.new("P9PR_" + key)
        g = apply_preset(ma, key)
        groups[key] = g.node_tree.name
        x = (i - (n - 1) / 2.0) * step
        uv_square(scene, "P9PR_tile_" + key, TILE, (x, 0.0, TILE_Z), ma)
        ob = p3.sphere(scene, "P9PR_sphere_" + key, (x, 0.0, SPHERE_Z), ma, radius=TILE * 0.5)
        ob.game.physics_type = 'NO_COLLISION'
        rects["tile_" + key] = m9.layout_rect(x, TILE_Z, TILE * 0.5)
        rects["sphere_" + key] = m9.layout_rect(x, SPHERE_Z, TILE * 0.5 * 0.7)
        mats[key] = ma.name
    finish(scene)
    files = [m9.save_copy(folder, "p9_presets", remix=0, tone=0),
             m9.save_copy(folder, "p9_presets_remix", remix=1, tone=0),
             m9.save_copy(folder, "p9_presets_aces", remix=0, tone=1),
             m9.save_copy(folder, "p9_presets_aces_remix", remix=1, tone=1)]

    # the seams: large squares, three tiles across and up
    scene = scene_base("P9PresetSeams")
    seams = {}
    k = len(SEAM_PRESETS)
    for i, key in enumerate(SEAM_PRESETS):
        ma = bpy.data.materials.new("P9PS_" + key)
        apply_preset(ma, key)
        x = (i - (k - 1) / 2.0) * (SEAM_SIZE + 0.1)
        uv_square(scene, "P9PS_" + key, SEAM_SIZE, (x, 0.0, 0.0), ma, repeat=float(SEAM_REPEAT))
        s = SEAM_SIZE / 2.0
        # the square's inner part on screen, and where its tiles meet
        rect = [screen_x(x - s * 0.95), screen_y(s * 0.95), screen_x(x + s * 0.95), screen_y(-s * 0.95)]
        cols = [screen_x(x - s + j * SEAM_SIZE / SEAM_REPEAT) for j in range(1, SEAM_REPEAT)]
        rows = [screen_y(-s + j * SEAM_SIZE / SEAM_REPEAT) for j in range(1, SEAM_REPEAT)]
        seams[key] = {"material": ma.name, "rect": rect, "columns": cols, "rows": rows}
    finish(scene)
    files += [m9.save_copy(folder, "p9_presets_seams_aces", remix=0, tone=1),
              m9.save_copy(folder, "p9_presets_seams_aces_remix", remix=1, tone=1)]

    table = {}
    for key in ORDER:
        p = PRESETS[key]
        table[key] = {"label": p["label"], "colour": list(p["colour"]), "albedo": list(p["albedo"]),
                      "rough": list(p["rough"]), "displace": p["displace"], "wet": bool(p.get("wet")),
                      "remix_type": p.get("remix_type", 'OPAQUE'), "ior": p.get("ior"),
                      "subsurface": list(p["subsurface"]) if p.get("subsurface") else None}
    E["presets"] = {"files": files, "rects": rects, "order": list(ORDER), "materials": mats, "groups": groups,
                    "table": table, "seams": seams, "size": [m9.WIDTH, m9.HEIGHT],
                    # out of the gate: Remix draws water as glass (it refracts the backdrop),
                    # skin with subsurface light (flat skin tiles paler than the game's)
                    "gate_out": ["WATER", "SKIN_LIGHT", "SKIN_MEDIUM", "SKIN_DARK"]}
