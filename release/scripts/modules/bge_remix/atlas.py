# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>

"""BGE-Remix P10.4: atlases for RTX Remix (the texture remaster).

Only Remix gets them: the game's OpenGL draws the presets live (no repeat,
no tiles), and the file's meshes and images are not changed. Everything
here marks materials and images with settings that the game's Remix side
(source/blender/remix/intern/remix_atlas.cc) reads when it starts, and,
when OpenGL is there (the Blender UI, not --background), bakes now so the
game starts with the files in its texture cache.

The three functions P10.5's Apply calls:

  bake_tile_set(material, size, colours=2)
      Corner-matched tiles (Wang tiles) of a preset material.
  bake_flipbook(material, frames=16)
      A looping sprite sheet of an animated preset; Remix plays it.
  pack_images(images, ...)
      Small images packed into shared pages (an option: off unless called).

Faces can be given a tile (or a packed page's cell) by hand: set_face_cell,
kept in the mesh's face layer "bge_remix_cell" (0: by position; n: tile or
cell n - 1). The UV editor's RTX Remix Atlas panel uses these too.
"""

import math

import bpy

TILES, FLIPBOOK, PACKED = 1, 2, 3
KINDS = {TILES: "tiles", FLIPBOOK: "flipbook", PACKED: "packed"}
PICK_LAYER = "bge_remix_cell"
# presets whose pattern is regular (P10.2's term): their corner colours keep
# the pattern in step (whole repeats), only the noise on it differs
REGULAR = {'BRICK'}
# how fast an animated preset's pattern drifts over the surface (m/s)
SPEEDS = {'WATER': 0.15}
DEFAULT_SPEED = 0.05
# a flipbook frame moves the pattern by this share of the tile (so the
# frame rate is the speed over it)
FRAME_STEP = 1.0 / 64.0
PAGE_PREFIX = "BGE-Remix page "
PREVIEW_PREFIX = "BGE-Remix atlas: "

# the atlas's corner patterns (remix_atlas_core.cc: the same), row by row
# from the bottom: every 2 x 2 pattern once, wrapping round
_TORUS = {
    2: (0, 0, 0, 1,
        0, 0, 1, 0,
        1, 0, 1, 1,
        0, 1, 1, 1),
    3: (0, 0, 0, 0, 0, 0, 0, 0, 1,
        0, 0, 1, 0, 2, 1, 1, 2, 0,
        2, 0, 1, 0, 1, 0, 1, 0, 2,
        0, 1, 0, 1, 1, 1, 2, 0, 2,
        0, 2, 2, 1, 0, 2, 1, 1, 2,
        2, 1, 0, 2, 1, 1, 1, 1, 2,
        2, 0, 2, 2, 1, 2, 2, 1, 1,
        2, 1, 0, 1, 2, 2, 2, 2, 0,
        0, 1, 2, 2, 0, 2, 1, 1, 2),
}

_M32 = 0xFFFFFFFF


# ---------------------------------------------------------------------------
# tiles: which tile a UV square shows (as remix_atlas_core.cc)

def corner_colour(i, j, seed, colours):
    """the colour (0 .. colours - 1) of UV grid corner (i, j)"""
    h = (((i & _M32) * 0x8da6b343) + ((j & _M32) * 0xd8163841) + ((seed & _M32) * 0xcb1ab31f) + 0x9e3779b9) & _M32
    h ^= h >> 16
    h = (h * 0x7feb352d) & _M32
    h ^= h >> 15
    h = (h * 0x846ca68b) & _M32
    h ^= h >> 16
    return h % colours if colours > 0 else 0


def tile_index(sw, se, ne, nw, colours):
    """a tile's index from its corners' colours (south-west, south-east,
    north-east, north-west)"""
    return sw + colours * (se + colours * (ne + colours * nw))


def tile_corners(tile, colours):
    """the corners' colours of a tile (south-west, south-east, north-east,
    north-west)"""
    out = []
    for _ in range(4):
        out.append(tile % colours)
        tile //= colours
    return out


def square_tile(i, j, seed, colours):
    """the tile UV square (i, j) shows (its corners agree with its
    neighbours')"""
    return tile_index(corner_colour(i, j, seed, colours), corner_colour(i + 1, j, seed, colours),
                      corner_colour(i + 1, j + 1, seed, colours), corner_colour(i, j + 1, seed, colours), colours)


def tiles_layout(colours, pitch, seed=0):
    """the atlas of a tile set: {"kind", "colours", "tiles", "grid": [cols,
    rows], "pitch", "gutter", "size": [w, h], "seed", "cells": [[x, y, w,
    h] per tile index, pixels from the bottom left], "places": [[col, row]
    per tile]}"""
    g = _TORUS[colours]
    n = colours * colours
    gutter = max(1, pitch // 32)
    cells = [None] * (n * n)
    places = [None] * (n * n)
    for row in range(n):
        for col in range(n):
            t = tile_index(g[row * n + col], g[row * n + (col + 1) % n], g[((row + 1) % n) * n + (col + 1) % n],
                           g[((row + 1) % n) * n + col], colours)
            cells[t] = [col * pitch + gutter, row * pitch + gutter, pitch - 2 * gutter, pitch - 2 * gutter]
            places[t] = [col, row]
    return {"kind": "tiles", "colours": colours, "tiles": n * n, "grid": [n, n], "pitch": pitch, "gutter": gutter,
            "size": [n * pitch, n * pitch], "seed": seed, "cells": cells, "places": places}


def flipbook_layout(frames, pitch, fps):
    """the sprite sheet: frame k at column k % cols, row k // cols (from the
    bottom), as Remix looks for it"""
    side = int(round(math.sqrt(frames)))
    cells = [[(k % side) * pitch, (k // side) * pitch, pitch, pitch] for k in range(frames)]
    return {"kind": "flipbook", "frames": frames, "grid": [side, side], "pitch": pitch, "gutter": 0,
            "size": [side * pitch, side * pitch], "fps": fps, "cells": cells}


# ---------------------------------------------------------------------------
# materials

def _preset_node(material):
    nt = material.node_tree if material is not None else None
    if nt is None:
        return None
    for n in nt.nodes:
        if n.bl_idname == "ShaderNodeGroup" and n.node_tree is not None and n.node_tree.get("bge_remix_preset"):
            return n
    return None


def _check_preset(material):
    """the preset group node, or ValueError saying why the material cannot
    have a tile set or flipbook"""
    if material is None:
        raise ValueError("no material")
    g = _preset_node(material)
    if g is None or not material.get("bge_remix_tile"):
        raise ValueError("material \"%s\" is not a preset (RTX Remix Preset first)" % material.name)
    seed = g.inputs.get("Seed")
    if seed is None or seed.is_linked:
        raise ValueError("material \"%s\": its preset's Seed must be free (not wired)" % material.name)
    return g


def _pow2_floor(n):
    p = 1
    while p * 2 <= n:
        p *= 2
    return p


def _bake_now(material, result):
    """bake and encode now when OpenGL is there (not in the background); the
    game bakes it at its start otherwise"""
    result["baked"] = False
    if bpy.app.background:
        result["note"] = "baked when the game starts (no OpenGL here)"
        return result
    try:
        import _bge_remix
    except ImportError:
        result["note"] = "baked when the game starts (no RTX Remix in this build)"
        return result
    import json
    scene = bpy.context.scene
    r = json.loads(_bge_remix.atlas_bake(material.as_pointer(), scene.as_pointer()))
    result["baked"] = bool(r.get("ok"))
    result["files"] = r.get("files", [])
    result["from_cache"] = bool(r.get("from_cache"))
    if r.get("error"):
        result["error"] = r["error"]
    if result["baked"]:
        result["preview"] = _preview_image(material)
    return result


def _preview_image(material):
    """the last bake's albedo atlas as an image in the file (for the UV
    editor's checks), or None"""
    try:
        import _bge_remix
        got = _bge_remix.atlas_preview()
    except (ImportError, AttributeError):
        return None
    if not got:
        return None
    w, h, px = got
    name = PREVIEW_PREFIX + material.name
    img = bpy.data.images.get(name)
    if img is not None and tuple(img.size) != (w, h):
        bpy.data.images.remove(img)
        img = None
    if img is None:
        img = bpy.data.images.new(name, w, h, alpha=True)
    # (kept when the file is saved: nothing uses it)
    img.use_fake_user = True
    img.pixels[:] = [b / 255.0 for b in px]
    img["bge_remix_atlas_of"] = material.name
    img.pack(as_png=True)
    return img


def bake_tile_set(material, size=1024, colours=2, seed=0):
    """Make a preset material's Remix textures a set of corner-matched tiles.

    material  an RTX Remix material made a preset (the RTX Remix Preset
              menu, P9.6; P10.2's fitted presets are such materials). Its
              preset's Seed must not be wired.
    size      each tile's side in pixels (a power of two; at most 1024 for
              16 tiles, 512 for 81): the atlas is 4 x 4 (or 9 x 9) of them
    colours   2: 16 tiles; 3: 81 tiles (less repeat, a larger atlas)
    seed      moves the tiles' arrangement (which tile each square shows)

    Each corner of a tile has one of the colours, each colour the preset at
    its own Seed (a regular preset, Brick: Seeds that keep the bricks in
    step). For Remix only, every mesh using the material is split along its
    UV grid (one square a UV unit, one tile a square) and each square shows
    the tile whose corners agree with its neighbours'.

    Returns a dict: "kind" ("tiles"), "colours", "tiles" (16 or 81), "grid"
    ([cols, rows]), "pitch" (a cell's side, pixels), "gutter" (the gap round
    each tile, pixels), "size" ([w, h], the atlas), "cells" ([x, y, w, h]
    per tile index, from the bottom left), "seed", "baked" (True: baked and
    encoded now; False: the game bakes it when it starts), "files" (the
    cache's files, when baked), "preview" (an image of the albedo atlas, when
    baked), "note", "error" ("" when none). Raises ValueError when the
    material cannot have one.
    """
    g = _check_preset(material)
    if colours not in (2, 3):
        raise ValueError("colours must be 2 or 3")
    pitch = _pow2_floor(max(16, min(int(size), 1024 if colours == 2 else 512)))
    key = material.get("bge_remix_preset")
    material["bge_remix_atlas"] = TILES
    material["bge_remix_atlas_colours"] = colours
    material["bge_remix_atlas_size"] = pitch
    material["bge_remix_atlas_seed"] = int(seed)
    material["bge_remix_atlas_regular"] = 1 if key in REGULAR else 0
    result = tiles_layout(colours, pitch, int(seed))
    scale = g.inputs["Scale"].default_value if g.inputs.get("Scale") is not None else 1.0
    result.update({"material": material.name, "preset": key, "regular": key in REGULAR, "scale": scale,
                   "note": "", "error": "", "files": [], "preview": None})
    return _bake_now(material, result)


def flipbook_speed(material, speed=None):
    """the flipbook's timing: {"speed" (m/s), "fps", "loop_seconds",
    "shift" (tiles the pattern moves over one loop: frames / 64)}"""
    key = material.get("bge_remix_preset")
    if speed is None:
        speed = SPEEDS.get(key, DEFAULT_SPEED)
    speed = max(float(speed), 1e-4)
    metres = float(material.get("bge_remix_tile_metres", 1.0) or 1.0)
    frames = int(material.get("bge_remix_atlas_frames", 16) or 16)
    fps = int(max(1, min(60, round(speed / (metres * FRAME_STEP)))))
    loop = frames / float(fps)
    return {"speed": speed, "fps": fps, "loop_seconds": loop, "shift": frames * FRAME_STEP}


def bake_flipbook(material, frames=16, speed=None, size=512):
    """Make an animated preset's Remix textures a looping sprite sheet.

    material  a preset material (as for bake_tile_set): P9.6's water, or any
              preset
    frames    16 (4 x 4, the plan's) or 64 (8 x 8)
    speed     how fast the pattern drifts, m/s (None: the preset's own,
              water 0.15); the frame rate comes from it: a frame moves the
              pattern 1/64 of the tile (so fps = speed / (tile x 1/64),
              1 to 60)
    size      a frame's side in pixels (a power of two, at most 512 for 16
              frames, 256 for 64)

    The preset's tile is baked once and moved over one full loop (by
    frames / 64 of the tile): frame k is the tile moved by k / frames of
    that, cross-faded into the tile moved a loop earlier, so the last frame
    runs into the first without a jump. Remix plays it by
    itself, by time, for the whole material (its sprite sheet rows, columns
    and frame rate); the game's OpenGL keeps drawing the preset as it is.

    Returns a dict: "kind" ("flipbook"), "frames", "grid", "pitch", "size",
    "cells" (per frame), "fps", "loop_seconds", "speed", "shift", "baked",
    "files", "preview", "note", "error" (as bake_tile_set). Raises
    ValueError when the material cannot have one.
    """
    _check_preset(material)
    if frames not in (16, 64):
        raise ValueError("frames must be 16 or 64")
    pitch = _pow2_floor(max(16, min(int(size), 512 if frames == 16 else 256)))
    material["bge_remix_atlas_frames"] = frames
    t = flipbook_speed(material, speed)
    material["bge_remix_atlas"] = FLIPBOOK
    material["bge_remix_atlas_size"] = pitch
    material["bge_remix_atlas_fps"] = t["fps"]
    material["bge_remix_atlas_shift"] = t["shift"]
    if "bge_remix_atlas_drift" in material:
        del material["bge_remix_atlas_drift"]
    result = flipbook_layout(frames, pitch, t["fps"])
    result.update(t)
    result.update({"material": material.name, "preset": material.get("bge_remix_preset"), "note": "", "error": "",
                   "files": [], "preview": None})
    return _bake_now(material, result)


def atlas_of(material):
    """what the material has: None, or a dict as the bake_* functions return
    (without baking) or, packed: {"kind": "packed", "page", "cell", "rect"}"""
    if material is None:
        return None
    kind = material.get("bge_remix_atlas", 0)
    if kind == TILES:
        r = tiles_layout(int(material.get("bge_remix_atlas_colours", 2)), int(material.get("bge_remix_atlas_size", 1024)),
                         int(material.get("bge_remix_atlas_seed", 0)))
        r["regular"] = bool(material.get("bge_remix_atlas_regular", 0))
        return r
    if kind == FLIPBOOK:
        r = flipbook_layout(int(material.get("bge_remix_atlas_frames", 16)), int(material.get("bge_remix_atlas_size", 512)),
                            int(material.get("bge_remix_atlas_fps", 8)))
        r["shift"] = material.get("bge_remix_atlas_shift")
        return r
    if kind == PACKED:
        for img in _material_images(material):
            page = img.get("bge_remix_atlas_page")
            if page:
                p = bpy.data.images.get(page)
                cells = _page_cells(p)
                c = int(img.get("bge_remix_atlas_cell", -1))
                return {"kind": "packed", "page": page, "cell": c, "cells": cells,
                        "rect": cells[c][:4] if 0 <= c < len(cells) else None,
                        "size": list(p.get("bge_remix_atlas_size", [0, 0])) if p is not None else [0, 0],
                        "gutter": int(p.get("bge_remix_atlas_gutter", 0)) if p is not None else 0}
    return None


def clear(material):
    """the material's atlas off (its Remix textures as before); a packed
    material's images keep their page until pack_images runs again"""
    for k in ("bge_remix_atlas", "bge_remix_atlas_colours", "bge_remix_atlas_size", "bge_remix_atlas_seed",
              "bge_remix_atlas_regular", "bge_remix_atlas_frames", "bge_remix_atlas_fps", "bge_remix_atlas_drift", "bge_remix_atlas_shift"):
        if k in material:
            del material[k]


# ---------------------------------------------------------------------------
# faces: a tile or cell picked by hand

def set_face_cell(mesh, faces, cell):
    """Give faces of a mesh a tile (or packed cell) by hand.

    mesh   a bpy Mesh (edit mode or not)
    faces  face indices
    cell   the tile's (or cell's) index, or None for "by position" again

    Kept in the mesh's integer face layer "bge_remix_cell" (cell + 1; 0 by
    position): saved in the file, kept by edit mode and by face edits
    (BMesh carries the layer), copied to faces split from them.
    """
    value = 0 if cell is None else int(cell) + 1
    if mesh.is_editmode:
        import bmesh
        bm = bmesh.from_edit_mesh(mesh)
        layer = bm.faces.layers.int.get(PICK_LAYER) or bm.faces.layers.int.new(PICK_LAYER)
        bm.faces.ensure_lookup_table()
        for i in faces:
            bm.faces[i][layer] = value
        bmesh.update_edit_mesh(mesh, False, False)
        return
    layer = mesh.polygon_layers_int.get(PICK_LAYER) or mesh.polygon_layers_int.new(PICK_LAYER)
    for i in faces:
        layer.data[i].value = value


def face_cells(mesh):
    """per face: its tile or cell picked by hand, or None (by position)"""
    if mesh.is_editmode:
        import bmesh
        bm = bmesh.from_edit_mesh(mesh)
        layer = bm.faces.layers.int.get(PICK_LAYER)
        return [(f[layer] - 1 if layer is not None and f[layer] > 0 else None) for f in bm.faces]
    layer = mesh.polygon_layers_int.get(PICK_LAYER)
    if layer is None:
        return [None] * len(mesh.polygons)
    return [(d.value - 1 if d.value > 0 else None) for d in layer.data]


def mesh_squares(mesh, material_index=None, uv_layer=None):
    """the UV squares a mesh's faces cover (those with the material slot,
    or all): {(i, j): [face indices]}"""
    uvl = uv_layer or (mesh.uv_layers.active if mesh.uv_layers else None)
    out = {}
    if uvl is None:
        return out
    for p in mesh.polygons:
        if material_index is not None and p.material_index != material_index:
            continue
        us = [uvl.data[k].uv[0] for k in p.loop_indices]
        vs = [uvl.data[k].uv[1] for k in p.loop_indices]
        for i in range(int(math.floor(min(us))), max(int(math.ceil(max(us))), int(math.floor(min(us))) + 1)):
            for j in range(int(math.floor(min(vs))), max(int(math.ceil(max(vs))), int(math.floor(min(vs))) + 1)):
                out.setdefault((i, j), []).append(p.index)
    return out


# ---------------------------------------------------------------------------
# packing small images

def _material_images(material):
    """the images a material's Remix textures read, as the translator finds
    them: the RTX Remix node's Texture nodes, Blender Internal's image
    slots"""
    out = []
    nt = material.node_tree if material.use_nodes else None
    if nt is not None:
        for n in nt.nodes:
            if n.bl_idname == "ShaderNodeTexture" and n.texture is not None and \
                    getattr(n.texture, "image", None) is not None and any(o.is_linked for o in n.outputs):
                if n.texture.image not in out:
                    out.append(n.texture.image)
            elif n.bl_idname == "ShaderNodeTexImage" and n.image is not None and any(o.is_linked for o in n.outputs):
                if n.image not in out:
                    out.append(n.image)
    else:
        for i, slot in enumerate(material.texture_slots):
            if slot is None or slot.texture is None or not material.use_textures[i]:
                continue
            img = getattr(slot.texture, "image", None)
            if img is not None and img not in out:
                out.append(img)
    return out


def _image_role(material, image):
    """'normal' when the material uses the image as a normal map, else
    'colour' (the page's empty space then grey; any image works)"""
    nt = material.node_tree if material.use_nodes else None
    if nt is not None:
        for n in nt.nodes:
            img = getattr(getattr(n, "texture", None), "image", None) if n.bl_idname == "ShaderNodeTexture" else \
                getattr(n, "image", None)
            if img != image:
                continue
            if n.bl_idname == "ShaderNodeTexture" and n.texture.use_normal_map:
                return 'normal'
            for o in n.outputs:
                for link in o.links:
                    if "normal" in link.to_socket.name.lower():
                        return 'normal'
        return 'colour'
    for slot in material.texture_slots:
        if slot is not None and slot.texture is not None and getattr(slot.texture, "image", None) == image:
            if slot.use_map_normal or getattr(slot.texture, "use_normal_map", False):
                return 'normal'
    return 'colour'


def _repeats(material, image):
    """whether the image repeats (its gap and wrap carry it on) or stops at
    its edge (Clip, Extend)"""
    nt = material.node_tree if material.use_nodes else None
    texs = []
    if nt is not None:
        texs = [n.texture for n in nt.nodes if n.bl_idname == "ShaderNodeTexture" and n.texture is not None and
                getattr(n.texture, "image", None) == image]
        for n in nt.nodes:
            if n.bl_idname == "ShaderNodeTexImage" and n.image == image:
                return n.extension == 'REPEAT'
    else:
        texs = [s.texture for s in material.texture_slots if s is not None and s.texture is not None and
                getattr(s.texture, "image", None) == image]
    return all(getattr(t, "extension", 'REPEAT') in ('REPEAT', 'CHECKER') for t in texs)


def _page_cells(page):
    if page is None:
        return []
    flat = list(page.get("bge_remix_atlas_cells", []))
    return [flat[i:i + 5] for i in range(0, len(flat) - 4, 5)]


def _game_materials(scene=None):
    """the materials the game scenes' meshes use, with their meshes"""
    out = {}
    for sc in ([scene] if scene is not None else bpy.data.scenes):
        for ob in sc.objects:
            if ob.type != 'MESH' or ob.data is None:
                continue
            for i, slot in enumerate(ob.material_slots):
                if slot.material is not None:
                    out.setdefault(slot.material, []).append((ob, i))
    return out


def _shelves(sizes, page, padding):
    """a plain packing (when _bge_remix's xatlas is not there): tallest
    first, in rows, each with its gap"""
    order = sorted(range(len(sizes)), key=lambda i: -sizes[i][1])
    places = [None] * len(sizes)
    pg, x, y, row = 0, padding, padding, 0
    for i in order:
        w, h = sizes[i]
        if x + w + padding > page:
            x, y, row = padding, y + row + 2 * padding, 0
        if y + h + padding > page:
            pg, x, y, row = pg + 1, padding, padding, 0
        places[i] = (pg, x, y)
        x += w + 2 * padding
        row = max(row, h)
    return places


def _texel_density(ob, material_index, image):
    """the image's pixels per metre on the object's faces using the slot (as
    P10.1 measures it: pixels times UV area over surface area)"""
    me = ob.data
    uvl = me.uv_layers.active if me.uv_layers else None
    if uvl is None:
        return None
    scale = ob.matrix_world.to_scale()
    s2 = abs(scale[0] * scale[1] * scale[2]) ** (2.0 / 3.0)
    uv_area = area = 0.0
    for p in me.polygons:
        if p.material_index != material_index:
            continue
        uvs = [uvl.data[k].uv for k in p.loop_indices]
        a = 0.0
        for k in range(len(uvs)):
            a += uvs[k][0] * uvs[(k + 1) % len(uvs)][1] - uvs[(k + 1) % len(uvs)][0] * uvs[k][1]
        uv_area += abs(a) * 0.5
        area += p.area * s2
    if area <= 0.0 or uv_area <= 0.0:
        return None
    return math.sqrt(image.size[0] * image.size[1] * uv_area / area)


def _bleed_distance(scene, density, padding):
    """how far away the page's gap is used up (its mip level past the gap):
    metres from the camera (perspective), 0 when it is already in this
    view (orthographic), None without a camera"""
    cam = scene.camera
    if cam is None or cam.type != 'CAMERA' or not density:
        return None
    level = int(math.floor(math.log(max(padding, 1), 2))) + 1
    texel = 1.0 / density
    res_y = scene.render.resolution_y * scene.render.resolution_percentage / 100.0
    res_x = scene.render.resolution_x * scene.render.resolution_percentage / 100.0
    if cam.data.type == 'ORTHO':
        footprint = cam.data.ortho_scale / max(res_x, res_y, 1.0)
        return 0.0 if footprint >= texel * (2 ** level) else float("inf")
    fov_y = 2.0 * math.atan(math.tan(cam.data.angle * 0.5) * min(res_y / max(res_x, 1.0), 1.0)) \
        if res_x >= res_y else cam.data.angle
    return texel * (2 ** level) * res_y / (2.0 * math.tan(fov_y * 0.5))


def pack_images(images, page_size=2048, padding=8, max_size=512, folder=None, scene=None):
    """Pack small images into shared pages for Remix (an option: nothing
    happens unless this is called).

    images     bpy Images to pack (small unique ones: P10.1's audit's
               "unique" kind); a material is packed when every image its
               Remix textures read is among them, of one size, at most
               max_size a side
    page_size  a page's side in pixels
    padding    the gap round each image (filled with the image carried on:
               wrapped when it repeats, its edge when it stops)
    max_size   larger images are left out
    folder     where the pages are saved as PNG files (None: packed into the
               .blend)
    scene      the game scene to look in (None: every scene)

    Each material's images get one cell (its colour, normal and other images
    at the same place on parallel pages: "BGE-Remix page <n> colour",
    "... normal"); the material is marked packed; for Remix only, its
    meshes' UVs are moved into the cell (split along the UV grid where the
    image repeats) and its textures read the pages. Placed with xatlas
    (MIT), else rows. Mipmaps are made per cell with the edges averaged
    when the game starts.

    Returns a dict: "pages" (the page images' names), "materials" (packed),
    "skipped" ({material: why}), "images" ({image: {"page", "cell",
    "rect"}}), "bleed_level" (the first mip level past the gap),
    "warnings" (pages that would bleed at a distance: [text]),
    "distances" ({material: metres at which the gap is used up}).
    """
    import numpy as np
    wanted = set(i.name for i in images if i is not None)
    mats = _game_materials(scene)
    result = {"pages": [], "materials": [], "skipped": {}, "images": {}, "warnings": [], "distances": {},
              "bleed_level": int(math.floor(math.log(max(padding, 1), 2))) + 1, "page_size": page_size,
              "padding": padding}
    # the sets: a material's images, all wanted, one size
    sets, owner = [], {}  # owner: image name -> the set it is in
    for ma in sorted(mats, key=lambda m: m.name):
        imgs = _material_images(ma)
        if not imgs or not any(i.name in wanted for i in imgs):
            continue
        why = None
        if not all(i.name in wanted for i in imgs):
            why = "not all of its images are among those packed"
        elif len(set(tuple(i.size) for i in imgs)) != 1:
            why = "its images differ in size"
        elif max(imgs[0].size) > max_size or min(imgs[0].size) <= 0:
            why = "its images are larger than %d pixels" % max_size
        elif any(i.name in owner and owner[i.name]["images"] != imgs for i in imgs):
            why = "an image of it is packed with another material's images"
        if why:
            result["skipped"][ma.name] = why
            continue
        same = [s for s in sets if s["images"] == imgs]
        if same:
            same[0]["materials"].append(ma)
            continue
        s = {"material": ma, "materials": [ma], "images": imgs, "size": tuple(imgs[0].size),
             "roles": [_image_role(ma, i) for i in imgs], "repeat": all(_repeats(ma, i) for i in imgs)}
        for i in imgs:
            owner[i.name] = s
        sets.append(s)
    if not sets:
        return result
    sizes = [s["size"] for s in sets]
    places = None
    try:
        import _bge_remix
        places = _bge_remix.atlas_pack(sizes, page_size, padding)
    except (ImportError, AttributeError):
        places = None
    if not places:
        places = _shelves(sizes, page_size, padding)
    pages = {}
    for s, (pg, x, y) in zip(sets, places):
        s["page"], s["x"], s["y"] = pg, x, y
        s["cell"] = len(pages.setdefault(pg, []))
        pages[pg].append(s)
    for pg, members in sorted(pages.items()):
        cells = []
        for s in members:
            cells += [s["x"], s["y"], s["size"][0], s["size"][1], 1 if s["repeat"] else 0]
        roles = sorted(set(r for s in members for r in s["roles"]))
        for role in roles:
            name = "%s%d %s" % (PAGE_PREFIX, pg, role)
            fill = (0.5, 0.5, 1.0, 1.0) if role == 'normal' else (0.5, 0.5, 0.5, 1.0)
            px = np.empty((page_size, page_size, 4), dtype=np.float32)
            px[:] = fill
            for s in members:
                for img, r in zip(s["images"], s["roles"]):
                    if r != role:
                        continue
                    w, h = img.size
                    a = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
                    a = np.pad(a, ((padding, padding), (padding, padding), (0, 0)),
                               mode='wrap' if s["repeat"] else 'edge')
                    px[s["y"] - padding:s["y"] + h + padding, s["x"] - padding:s["x"] + w + padding] = a
                    img["bge_remix_atlas_page"] = name
                    img["bge_remix_atlas_cell"] = s["cell"]
                    result["images"][img.name] = {"page": name, "cell": s["cell"], "rect": [s["x"], s["y"], w, h]}
            page = bpy.data.images.get(name)
            if page is not None and tuple(page.size) != (page_size, page_size):
                bpy.data.images.remove(page)
                page = None
            if page is None:
                page = bpy.data.images.new(name, page_size, page_size, alpha=True)
                if role == 'normal':
                    # (before the pixels: a colour space change frees them)
                    page.colorspace_settings.name = 'Non-Color'
            # fix round 1: a page has no users (only the game reads it): it
            # was dropped when the file was saved, so the game found no page
            page.use_fake_user = True
            page.pixels[:] = px.ravel().tolist()
            page["bge_remix_atlas_cells"] = cells
            page["bge_remix_atlas_size"] = [page_size, page_size]
            page["bge_remix_atlas_gutter"] = padding
            if folder:
                import os
                os.makedirs(folder, exist_ok=True)
                page.filepath_raw = os.path.join(folder, name.replace(" ", "_") + ".png")
                page.file_format = 'PNG'
                page.save()
            else:
                page.pack(as_png=True)
            result["pages"].append(name)
    for s in sets:
        for ma in s["materials"]:
            ma["bge_remix_atlas"] = PACKED
            result["materials"].append(ma.name)
            # the distance at which the gap is used up, on its nearest-to-
            # sharp mesh (the highest density bleeds last)
            for ob, slot in mats.get(ma, []):
                sc = scene or next((x for x in bpy.data.scenes if ob.name in x.objects), None)
                dens = _texel_density(ob, slot, s["images"][0])
                d = _bleed_distance(sc, dens, padding) if sc is not None else None
                if d is None:
                    continue
                prev = result["distances"].get(ma.name)
                result["distances"][ma.name] = d if prev is None else min(prev, d)
            d = result["distances"].get(ma.name)
            cam = (scene or bpy.context.scene).camera
            far = cam.data.clip_end if cam is not None and cam.type == 'CAMERA' else 100.0
            if d is not None and d < far:
                result["warnings"].append(
                    "page %d: \"%s\" bleeds into its neighbours from %s on (its %d-pixel gap is used up at mip "
                    "level %d)" % (s["page"], ma.name, "this view" if d == 0.0 else "%.1f m" % d, padding,
                                   result["bleed_level"]))
    return result


# ---------------------------------------------------------------------------
# checks (the UV editor's): gaps and cell edges

def _pixels(img):
    import numpy as np
    w, h = img.size
    return np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[:, :, :3] * 255.0


def check_tiles(material, image=None):
    """a tile set's atlas (its preview image, from a bake here) checked:
    each tile's gap carries its edge on, and every two tiles that meet
    (matching corners) meet without a step. Returns {"ok", "gaps": [tile
    indices whose gap jumps], "edges": [[left tile, right tile, "x" or "y",
    step]], "step": the tiles' own step, "scale"}; None without a preview"""
    import numpy as np
    lay = atlas_of(material)
    img = image or bpy.data.images.get(PREVIEW_PREFIX + material.name)
    if lay is None or lay["kind"] != "tiles" or img is None:
        return None
    a = _pixels(img)
    f = lay["size"][0] / float(img.size[0])  # (the preview is smaller)
    C = lay["colours"]

    def cell(t):
        x, y, w, h = [int(round(v / f)) for v in lay["cells"][t]]
        return x, y, w, h

    inner = []
    for t in range(lay["tiles"]):
        x, y, w, h = cell(t)
        c = a[y:y + h, x:x + w]
        inner.append(float(np.abs(c[:, 1:] - c[:, :-1]).mean()))
    step = float(np.median(inner)) if inner else 0.0
    limit = 2.0 * step + 3.0
    gaps, edges = [], []
    g = max(int(round(lay["gutter"] / f)), 0)
    for t in range(lay["tiles"]):
        x, y, w, h = cell(t)
        if g >= 1:
            jump = max(float(np.abs(a[y:y + h, x - 1] - a[y:y + h, x]).mean()),
                       float(np.abs(a[y:y + h, x + w] - a[y:y + h, x + w - 1]).mean()))
            if jump > limit:
                gaps.append(t)
    for t in range(lay["tiles"]):
        ct = tile_corners(t, C)
        x, y, w, h = cell(t)
        for u in range(lay["tiles"]):
            cu = tile_corners(u, C)
            if ct[1] == cu[0] and ct[2] == cu[3]:
                ux, uy, uw, uh = cell(u)
                d = float(np.abs(a[y:y + h, x + w - 1] - a[uy:uy + uh, ux]).mean())
                if d > limit:
                    edges.append([t, u, "x", round(d, 2)])
            if ct[3] == cu[0] and ct[2] == cu[1]:
                ux, uy, uw, uh = cell(u)
                d = float(np.abs(a[y + h - 1, x:x + w] - a[uy, ux:ux + uw]).mean())
                if d > limit:
                    edges.append([t, u, "y", round(d, 2)])
    return {"ok": not gaps and not edges, "gaps": gaps, "edges": edges, "step": round(step, 2), "scale": f}


def check_page(page):
    """a packed page checked: each cell's gap carries its image on (wrapped
    or its edge) and no cell's gap reaches another's. Returns {"ok",
    "gaps": [cells whose gap differs], "overlaps": [[cell, cell]]}"""
    import numpy as np
    cells = _page_cells(page)
    pad = int(page.get("bge_remix_atlas_gutter", 0))
    a = _pixels(page)
    gaps, overlaps = [], []
    for k, (x, y, w, h, rep) in enumerate(cells):
        if pad < 1:
            continue
        img = a[y:y + h, x:x + w]
        left = a[y:y + h, x - 1]
        want = img[:, -1] if rep else img[:, 0]
        if float(np.abs(left - want).mean()) > 2.0:
            gaps.append(k)
        for j in range(k):
            x2, y2, w2, h2 = cells[j][:4]
            if not (x >= x2 + w2 + 2 * pad or x2 >= x + w + 2 * pad or y >= y2 + h2 + 2 * pad or
                    y2 >= y + h + 2 * pad):
                overlaps.append([j, k])
    return {"ok": not gaps and not overlaps, "gaps": gaps, "overlaps": overlaps}


def check_mesh(ob, material_index=None):
    """a mesh's faces against its atlas: faces picked by hand (and to which
    cell), picks out of range, and faces whose UVs cross a packed image's
    edge when it does not repeat (shown stretched). Returns {"picked":
    {face: cell}, "bad_picks": [faces], "stretched": [faces], "squares"}"""
    me = ob.data
    slots = [i for i, s in enumerate(ob.material_slots) if s.material is not None and
             (material_index is None or i == material_index) and atlas_of(s.material)]
    picks = face_cells(me)
    out = {"picked": {}, "bad_picks": [], "stretched": [], "squares": 0}
    uvl = me.uv_layers.active if me.uv_layers else None
    for i in slots:
        lay = atlas_of(ob.material_slots[i].material)
        count = len(lay.get("cells") or [])
        squares = mesh_squares(me, i)
        out["squares"] += len(squares)
        for p in me.polygons:
            if p.material_index != i:
                continue
            c = picks[p.index] if p.index < len(picks) else None
            if c is not None:
                out["picked"][p.index] = c
                if c >= count:
                    out["bad_picks"].append(p.index)
            if lay["kind"] == "packed" and uvl is not None and lay.get("cell") is not None and \
                    0 <= lay["cell"] < count and not lay["cells"][lay["cell"]][4]:
                uv = [uvl.data[k].uv for k in p.loop_indices]
                if any(q[0] < 0.0 or q[0] > 1.0 or q[1] < 0.0 or q[1] > 1.0 for q in uv):
                    out["stretched"].append(p.index)
    return out
