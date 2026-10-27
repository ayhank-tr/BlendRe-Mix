# BGE-Remix P3 test scenes. Runs inside Blender 2.79b (Python 3.5: no f-strings).
#
#   blender --background --factory-startup --python make_scenes_p3.py -- <out_dir>
#
# Writes:
#   p3_grid.blend          20 balls in four rows, one Remix material type per
#                          row (see the P3 plan's drawing):
#                            row 1  Principled: plastic rough 0.1 / 0.5 / 0.9,
#                                   metal rough 0.1 / 0.5
#                            row 2  Glass IOR 1.5, blue glass IOR 1.33,
#                                   Principled transmission 1, Principled mixed
#                                   with Transparent (alpha 0.5), Blender
#                                   Internal alpha 0.5
#                            row 3  Emission 1 and 5, Subsurface Scattering
#                                   node, Principled subsurface 0.5, an
#                                   unsupported node graph (T3.5)
#                            row 4  packed colour, normal and roughness maps,
#                                   Blender Internal colour + normal slots,
#                                   RTX Remix panel (gold metal, T3.4)
#                          on a grey floor before a grey wall, sun + area lamp
#                          + sky, camera straight on
#   p3_room.blend          the P2 room, textured: tiled floor (packed colour,
#                          normal, roughness), brick back wall (external PNGs
#                          next to the .blend), plaster side walls and ceiling
#                          (Blender Internal slot, packed), the red box with
#                          Blender's generated UV grid, glass box, glowing sign
#   p3_room_changed.blend  the room with one pixel of the floor's roughness map
#                          changed (T3.2b: only that image is encoded again)
#   p3_expected.json       what the tests expect Remix to receive
#
# Both scenes carry the P2 probe (frame count, frame times, screenshot, end).

import json
import math
import os
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
import make_scenes_p2 as p2  # noqa: E402  (probe, lamps, camera, save)

SIZE = 256  # test images: small, so CI's CPU encoding stays quick


# ---------------------------------------------------------------------------
# Images

def image(name, pixel, size=SIZE, alpha=True, pack=True, non_color=False):
    """An image filled by pixel(x, y) -> (r, g, b, a), x, y in 0..1 from the
    bottom left, packed into the .blend."""
    img = bpy.data.images.new(name, size, size, alpha=alpha)
    # before the pixels: changing the colour space frees a generated image's buffer
    if non_color:
        img.colorspace_settings.name = 'Non-Color'
    px = []
    for j in range(size):
        y = (j + 0.5) / size
        for i in range(size):
            px.extend(pixel((i + 0.5) / size, y))
    img.pixels = px
    if pack:
        img.pack(as_png=True)
    return img


def external(name, pixel, folder, size=SIZE, non_color=False):
    """The same as a PNG file next to the .blend, not packed."""
    img = image(name + "_tmp", pixel, size=size, pack=False)
    path = os.path.join(folder, name + ".png")
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    bpy.data.images.remove(img)
    out = bpy.data.images.load(path)
    out.name = name
    if non_color:
        out.colorspace_settings.name = 'Non-Color'
    return out


def checker(x, y, n=8, a=(0.9, 0.9, 0.85), b=(0.15, 0.2, 0.5)):
    c = a if (int(x * n) + int(y * n)) % 2 == 0 else b
    return (c[0], c[1], c[2], 1.0)


def normal_rgb(nx, ny, nz):
    l = math.sqrt(nx * nx + ny * ny + nz * nz)
    return (nx / l * 0.5 + 0.5, ny / l * 0.5 + 0.5, nz / l * 0.5 + 0.5, 1.0)


def bumps(x, y, n=6):
    """round bumps in a grid, as a tangent-space normal map (OpenGL)"""
    fx, fy = (x * n) % 1.0 - 0.5, (y * n) % 1.0 - 0.5
    r = math.sqrt(fx * fx + fy * fy)
    if r > 0.4:
        return normal_rgb(0, 0, 1)
    s = 1.5 * math.sin(r / 0.4 * math.pi)
    return normal_rgb(fx / max(r, 1e-6) * s * 0.5, fy / max(r, 1e-6) * s * 0.5, 1.0)


def tiles(x, y, n=4, grout=0.04):
    fx, fy = (x * n) % 1.0, (y * n) % 1.0
    edge = min(fx, fy, 1 - fx, 1 - fy) < grout
    return (0.25, 0.25, 0.27, 1.0) if edge else (0.75, 0.72, 0.66, 1.0)


def bevel_normal(dx0, dx1, dy0, dy1, band, slope):
    """The normal of a raised block with bevelled edges: dx0/dx1/dy0/dy1 are
    the distances to the block's left/right/bottom/top edge, in the same
    units as band; inside the band the surface rises with the given slope
    (1.0 = 45 degrees), facing away from the nearest edge."""
    d = min(dx0, dx1, dy0, dy1)
    if d >= band:
        return normal_rgb(0, 0, 1)
    # a smooth (sine) ramp: steepest half-way up the bevel
    t = slope * math.sin(math.pi * 0.5 * (1.0 - d / band)) * 1.4
    if d == dx0:
        return normal_rgb(-t, 0, 1)
    if d == dx1:
        return normal_rgb(t, 0, 1)
    if d == dy0:
        return normal_rgb(0, -t, 1)
    return normal_rgb(0, t, 1)


def tiles_normal(x, y, n=4, grout=0.04, band=0.16):
    """pillowed tiles: a wide bevel on all four sides, flat sunken grout"""
    fx, fy = (x * n) % 1.0, (y * n) % 1.0
    if min(fx, fy, 1 - fx, 1 - fy) < grout:
        return normal_rgb(0, 0, 1)
    return bevel_normal(fx - grout, 1 - grout - fx, fy - grout, 1 - grout - fy, band, 1.0)


def tiles_rough(x, y, n=4, grout=0.04):
    fx, fy = (x * n) % 1.0, (y * n) % 1.0
    v = 0.9 if min(fx, fy, 1 - fx, 1 - fy) < grout else 0.25
    return (v, v, v, 1.0)


def brick(x, y, rows=8, cols=4, mortar=0.05):
    r = int(y * rows)
    fx = (x * cols + (0.5 if r % 2 else 0.0)) % 1.0
    fy = (y * rows) % 1.0
    if fx < mortar or fy < mortar * 2:
        return (0.6, 0.58, 0.55, 1.0)
    shade = 0.85 + 0.15 * ((r * 7 + int(x * cols + 0.5 * (r % 2)) * 3) % 5) / 4.0
    return (0.55 * shade, 0.22 * shade, 0.15 * shade, 1.0)


def brick_normal(x, y, rows=8, cols=4, mortar=0.05):
    """each brick raised with a bevel on all four sides, flat sunken mortar
    (the same layout as brick())"""
    r = int(y * rows)
    fx = (x * cols + (0.5 if r % 2 else 0.0)) % 1.0
    fy = (y * rows) % 1.0
    if fx < mortar or fy < mortar * 2:
        return normal_rgb(0, 0, 1)
    # in brick-height units: a brick is 2 high per 1 wide here (8 rows, 4 cols)
    w = rows / float(cols)
    return bevel_normal((fx - mortar) * w, (1 - fx) * w, fy - mortar * 2, 1 - fy, 0.25, 1.0)


def plaster(x, y):
    v = 0.78 + 0.06 * math.sin(x * 37.0) * math.sin(y * 23.0)
    return (v, v * 0.97, v * 0.9, 1.0)


def sign(x, y):
    """a glowing sign: a bright frame and an arrow"""
    frame = min(x, y, 1 - x, 1 - y) < 0.08
    arrow = abs(y - 0.5) < 0.08 and 0.2 < x < 0.7 or (0.6 < x < 0.85 and abs(y - 0.5) < (0.85 - x))
    if frame or arrow:
        return (1.0, 0.85, 0.4, 1.0)
    return (0.05, 0.08, 0.15, 1.0)


def stripes(x, y, n=5):
    v = 0.1 + 0.8 * (int(x * n) / max(n - 1, 1))
    return (v, v, v, 1.0)


# ---------------------------------------------------------------------------
# Materials

def nodes_material(name):
    ma = bpy.data.materials.new(name)
    ma.use_nodes = True
    nt = ma.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.is_active_output = True
    return ma, nt, out


def principled(name, color=(0.8, 0.8, 0.8), metallic=0.0, roughness=0.5, **inputs):
    ma, nt, out = nodes_material(name)
    p = nt.nodes.new("ShaderNodeBsdfPrincipled")
    p.inputs["Base Color"].default_value = list(color) + [1.0]
    p.inputs["Metallic"].default_value = metallic
    p.inputs["Roughness"].default_value = roughness
    for k, v in inputs.items():
        p.inputs[k.replace("_", " ")].default_value = v
    nt.links.new(p.outputs[0], out.inputs["Surface"])
    return ma, nt, p


def image_node(nt, img, non_color=False, closest=False):
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = img
    if non_color:
        t.color_space = 'NONE'
    if closest:
        t.interpolation = 'Closest'
    return t


def normal_map(nt, img, into):
    t = image_node(nt, img, non_color=True)
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(t.outputs["Color"], nm.inputs["Color"])
    nt.links.new(nm.outputs["Normal"], into.inputs["Normal"])


def shader_material(name, kind, **inputs):
    ma, nt, out = nodes_material(name)
    s = nt.nodes.new(kind)
    for k, v in inputs.items():
        s.inputs[k.replace("_", " ")].default_value = v
    nt.links.new(s.outputs[0], out.inputs["Surface"])
    return ma, nt, s


def bi_material(name, color, hard=50, alpha=1.0, blend='OPAQUE'):
    return p2.material(name, color, ref=1.0, alpha=alpha, hard=hard, blend=blend)


def bi_slot(ma, img, color=False, normal=False):
    tex = bpy.data.textures.new(ma.name + "_" + img.name, 'IMAGE')
    tex.image = img
    if normal:
        tex.use_normal_map = True
    slot = ma.texture_slots.add()
    slot.texture = tex
    slot.texture_coords = 'UV'
    slot.use_map_color_diffuse = color
    slot.use_map_normal = normal
    if normal:
        slot.normal_map_space = 'TANGENT'
    return slot


# ---------------------------------------------------------------------------
# Geometry with UVs

def sphere(scene, name, location, ma, radius=0.4):
    try:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, size=radius, location=location,
                                             calc_uvs=True)
    except TypeError:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, size=radius, location=location)
    ob = bpy.context.object
    ob.name = name
    ob.data.name = name + "Mesh"
    ob.data.materials.append(ma)
    bpy.ops.object.shade_smooth()
    return ob


def textured_cube(scene, name, size, location, ma):
    try:
        bpy.ops.mesh.primitive_cube_add(radius=size / 2.0, location=location, calc_uvs=True)
    except TypeError:
        bpy.ops.mesh.primitive_cube_add(radius=size / 2.0, location=location)
    ob = bpy.context.object
    ob.name = name
    ob.data.name = name + "Mesh"
    ob.data.materials.append(ma)
    return ob


def uv_plane(scene, name, size, location, rotation, ma, repeat=1.0):
    ob = p2.plane(scene, name, size, location, rotation, ma)
    me = ob.data
    me.uv_textures.new("UVMap")
    uvs = [(0, 0), (repeat, 0), (repeat, repeat), (0, repeat)]
    for loop, uv in zip(me.uv_layers[0].data, uvs):
        loop.uv = uv
    return ob


def base_scene():
    scene = p2.empty_scene()
    # the game engine draws GLSL with texture slots
    scene.game_settings.material_mode = 'GLSL'
    return scene


def finish(scene, path):
    p2.add_probe(scene)
    scene["bge_remix"] = 1
    scene.update()
    p2.save(path)


# ---------------------------------------------------------------------------
# The grid

def expect(ma, source, **values):
    e = {"name": ma.name, "source": source}
    e.update(values)
    return e


def make_grid(folder):
    scene = base_scene()
    p2.world(scene, (0.25, 0.27, 0.30))
    grey = bi_material("GridFloor", (0.5, 0.5, 0.5), hard=10)
    p2.plane(scene, "Floor", 16.0, (0, 0, 0), (0, 0, 0), grey)
    p2.plane(scene, "Backdrop", 16.0, (0, 2.0, 4.0), (math.radians(90), 0, 0), grey)

    checker_img = image("Checker", checker)
    bumps_img = image("Bumps", bumps, non_color=True)
    stripes_img = image("RoughStripes", stripes, non_color=True)
    checker2_img = image("Checker2", lambda x, y: checker(x, y, 4, (0.9, 0.8, 0.3), (0.2, 0.5, 0.2)))

    E = []
    rows = []

    # row 1: Principled
    r1 = []
    for name, color, metal, rough in (("PlasticR01", (0.8, 0.1, 0.1), 0.0, 0.1),
                                       ("PlasticG05", (0.1, 0.6, 0.15), 0.0, 0.5),
                                       ("PlasticB09", (0.1, 0.2, 0.8), 0.0, 0.9),
                                       ("Copper01", (0.95, 0.64, 0.54), 1.0, 0.1),
                                       ("Aluminium05", (0.91, 0.92, 0.92), 1.0, 0.5)):
        ma, nt, p = principled(name, color, metal, rough)
        r1.append(ma)
        E.append(expect(ma, "nodes", type="opaque", albedo=list(color), metallic=metal, roughness=rough, blend=0))
    rows.append(r1)

    # row 2: glass and alpha
    r2 = []
    ma, nt, s = shader_material("Glass15", "ShaderNodeBsdfGlass", Color=(1, 1, 1, 1), IOR=1.5)
    r2.append(ma)
    E.append(expect(ma, "nodes", type="translucent", ior=1.5, transmittance=[1, 1, 1], distance=0.5))
    ma, nt, s = shader_material("GlassBlue133", "ShaderNodeBsdfGlass", Color=(0.5, 0.7, 1.0, 1), IOR=1.33)
    r2.append(ma)
    E.append(expect(ma, "nodes", type="translucent", ior=1.33, transmittance=[0.25, 0.49, 1.0],
                    distance=0.5))  # Cycles tints at both surfaces: the colour squared
    ma, nt, p = principled("PrincipledGlass", (0.95, 0.95, 1.0), 0.0, 0.05, Transmission=1.0, IOR=1.45)
    r2.append(ma)
    E.append(expect(ma, "nodes", type="translucent", ior=1.45, transmittance=[0.9025, 0.9025, 1.0],
                    distance=0.5))
    ma, nt, out = nodes_material("PrincipledAlpha05")
    p = nt.nodes.new("ShaderNodeBsdfPrincipled")
    p.inputs["Base Color"].default_value = (0.9, 0.5, 0.1, 1.0)
    tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    mix = nt.nodes.new("ShaderNodeMixShader")
    mix.inputs["Fac"].default_value = 0.5
    nt.links.new(tr.outputs[0], mix.inputs[1])
    nt.links.new(p.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    r2.append(ma)
    E.append(expect(ma, "nodes", type="opaque", albedo=[0.9, 0.5, 0.1], opacity=0.5, blend=1))
    ma = bi_material("BIAlpha05", (0.2, 0.7, 0.9), alpha=0.5, blend='ALPHA')
    r2.append(ma)
    E.append(expect(ma, "bi", type="opaque", albedo=[0.2, 0.7, 0.9], opacity=0.5, blend=1))
    rows.append(r2)

    # row 3: glow and skin
    r3 = []
    for name, strength in (("Emission1", 1.0), ("Emission5", 5.0)):
        ma, nt, s = shader_material(name, "ShaderNodeEmission", Color=(1.0, 0.9, 0.7, 1), Strength=strength)
        r3.append(ma)
        E.append(expect(ma, "nodes", type="opaque", emissive=[1.0, 0.9, 0.7], intensity=strength))
    ma, nt, s = shader_material("SubsurfaceNode", "ShaderNodeSubsurfaceScattering", Color=(0.9, 0.6, 0.5, 1),
                                Scale=0.1, Radius=(1.0, 0.2, 0.1))
    r3.append(ma)
    E.append(expect(ma, "nodes", type="opaque", albedo=[0.9, 0.6, 0.5], subsurface=1,
                    sss_radius=[0.1, 0.02, 0.01]))
    ma, nt, p = principled("PrincipledSSS05", (0.9, 0.7, 0.6), 0.0, 0.4, Subsurface=0.5)
    p.inputs["Subsurface Color"].default_value = (0.9, 0.7, 0.6, 1.0)
    p.inputs["Subsurface Radius"].default_value = (1.0, 0.2, 0.1)
    r3.append(ma)
    E.append(expect(ma, "nodes", type="opaque", albedo=[0.9, 0.7, 0.6], subsurface=1,
                    sss_radius=[0.5, 0.1, 0.05]))
    ma, nt, p = principled("Unsupported", (0.2, 0.6, 0.2), 0.0, 0.5)
    wave = nt.nodes.new("ShaderNodeTexWave")
    nt.links.new(wave.outputs["Color"], p.inputs["Base Color"])
    r3.append(ma)
    E.append(expect(ma, "nodes", type="opaque", albedo=[0.2, 0.6, 0.2], notes=1))
    rows.append(r3)

    # row 4: textures
    r4 = []
    ma, nt, p = principled("ColourMap", (0.8, 0.8, 0.8), 0.0, 0.5)
    t = image_node(nt, checker_img)
    nt.links.new(t.outputs["Color"], p.inputs["Base Color"])
    r4.append(ma)
    E.append(expect(ma, "nodes", type="opaque", textures={"albedo": ["Checker", "col"]}))
    ma, nt, p = principled("NormalMap", (0.6, 0.6, 0.65), 0.0, 0.3)
    normal_map(nt, bumps_img, p)
    r4.append(ma)
    E.append(expect(ma, "nodes", type="opaque", textures={"normal": ["Bumps", "nrm"]}))
    ma, nt, p = principled("RoughnessMap", (0.7, 0.3, 0.3), 0.0, 0.5)
    t = image_node(nt, stripes_img, non_color=True)
    nt.links.new(t.outputs["Color"], p.inputs["Roughness"])
    r4.append(ma)
    E.append(expect(ma, "nodes", type="opaque", textures={"roughness": ["RoughStripes", "r"]}))
    ma = bi_material("BIColourNormal", (0.8, 0.8, 0.8), hard=60)
    bi_slot(ma, checker2_img, color=True)
    bi_slot(ma, bumps_img, normal=True)
    r4.append(ma)
    E.append(expect(ma, "bi", type="opaque", textures={"albedo": ["Checker2", "col"], "normal": ["Bumps", "nrm"]}))
    ma = bi_material("PanelGold", (0.1, 0.2, 0.9), hard=50)
    ma["bge_remix_override"] = 1
    ma["bge_remix_type"] = 0
    ma["bge_remix_albedo"] = [1.0, 0.766, 0.336]
    ma["bge_remix_metallic"] = 1.0
    ma["bge_remix_roughness"] = 0.25
    r4.append(ma)
    E.append(expect(ma, "panel", type="opaque", albedo=[1.0, 0.766, 0.336], metallic=1.0, roughness=0.25))
    rows.append(r4)

    # rows top to bottom, columns left to right
    for ri, row in enumerate(rows):
        z = 3.6 - ri * 1.0
        for ci, ma in enumerate(row):
            x = -2.4 + ci * 1.2
            sphere(scene, "Ball_" + ma.name, (x, 0.0, z), ma)
    p2.lamp(scene, "Sun", 'SUN', (0, -4, 8), (math.radians(40), 0, math.radians(-25)), energy=0.8)
    area = p2.lamp(scene, "Area", 'AREA', (-3.0, -4.0, 5.0), energy=1.5, distance=10.0, soft=2.0)
    p2.look_at(area, (0, 0, 2.1))
    area.data.size = 2.0
    p2.camera(scene, (0.0, -8.5, 2.1), (0.0, 0.0, 2.1), lens=40.0)
    finish(scene, os.path.join(folder, "p3_grid.blend"))
    return E


# ---------------------------------------------------------------------------
# The textured room

def make_room(folder, changed=False):
    scene = base_scene()
    p2.world(scene, (0.05, 0.07, 0.10))
    tiles_c = image("TilesColour", tiles)
    tiles_n = image("TilesNormal", tiles_normal, non_color=True)
    if changed:
        def rough(x, y):
            v = tiles_rough(x, y)
            return (0.5, 0.5, 0.5, 1.0) if x < 1.0 / SIZE and y < 1.0 / SIZE else v
        tiles_r = image("TilesRough", rough, non_color=True)
    else:
        tiles_r = image("TilesRough", tiles_rough, non_color=True)
    brick_c = external("BrickColour", brick, folder)
    brick_n = external("BrickNormal", brick_normal, folder, non_color=True)
    plaster_img = image("Plaster", plaster)
    uvgrid = bpy.data.images.new("UVGrid", SIZE, SIZE)
    uvgrid.generated_type = 'UV_GRID'
    sign_img = image("Sign", sign)

    floor, nt, p = principled("TiledFloor", (0.8, 0.8, 0.8), 0.0, 0.5)
    t = image_node(nt, tiles_c)
    nt.links.new(t.outputs["Color"], p.inputs["Base Color"])
    t = image_node(nt, tiles_r, non_color=True)
    nt.links.new(t.outputs["Color"], p.inputs["Roughness"])
    normal_map(nt, tiles_n, p)

    brick_m, nt, p = principled("BrickWall", (0.8, 0.8, 0.8), 0.0, 0.8)
    t = image_node(nt, brick_c)
    nt.links.new(t.outputs["Color"], p.inputs["Base Color"])
    normal_map(nt, brick_n, p)

    plaster_m = bi_material("PlasterWall", (0.8, 0.8, 0.8), hard=10)
    bi_slot(plaster_m, plaster_img, color=True)
    grid_m = bi_material("UVGridBox", (0.8, 0.8, 0.8), hard=80)
    bi_slot(grid_m, uvgrid, color=True)
    glass_m, nt, s = shader_material("RoomGlass", "ShaderNodeBsdfGlass", Color=(0.6, 0.8, 1.0, 1), IOR=1.5)
    sign_m, nt, s = nodes_material("SignGlow")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = 2.0
    t = image_node(nt, sign_img)
    nt.links.new(t.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs[0], s.inputs["Surface"])

    uv_plane(scene, "Floor", 8.0, (0, 0, 0), (0, 0, 0), floor, repeat=2.0)
    uv_plane(scene, "Ceiling", 8.0, (0, 0, 4), (math.pi, 0, 0), plaster_m)
    uv_plane(scene, "BackWall", 8.0, (0, 4, 2), (math.radians(90), 0, 0), brick_m, repeat=2.0)
    uv_plane(scene, "LeftWall", 8.0, (-4, 0, 2), (0, math.radians(90), 0), plaster_m)
    uv_plane(scene, "RightWall", 8.0, (4, 0, 2), (0, math.radians(-90), 0), plaster_m)
    textured_cube(scene, "RedBox", 1.0, (-1.2, 0.8, 0.5), grid_m)
    p2.cube(scene, "GlassBox", 0.8, (1.2, 0.2, 0.4), glass_m)
    uv_plane(scene, "LightPanel", 1.0, (0, 3.95, 2.5), (math.radians(90), 0, 0), sign_m)

    lamps = [
        p2.lamp(scene, "LampPoint", 'POINT', (-2.0, 1.0, 3.0), energy=1.5, color=(1.0, 0.95, 0.9), distance=10.0,
                soft=0.2),
        p2.lamp(scene, "LampSpot", 'SPOT', (2.0, -1.0, 3.5), (math.radians(20), 0, 0), energy=2.0, distance=12.0,
                soft=0.1),
        p2.lamp(scene, "LampSun", 'SUN', (0, 0, 6), (math.radians(45), 0, math.radians(30)), energy=0.5),
        p2.lamp(scene, "LampArea", 'AREA', (0.0, 2.0, 3.9), energy=1.0, distance=8.0, soft=1.0),
        p2.lamp(scene, "LampHemi", 'HEMI', (0, 0, 5), energy=0.2, color=(0.6, 0.7, 1.0)),
    ]
    lamps[1].data.spot_size = math.radians(60.0)
    lamps[1].data.spot_blend = 0.3
    lamps[3].data.shape = 'RECTANGLE'
    lamps[3].data.size = 1.0
    lamps[3].data.size_y = 0.5
    p2.camera(scene, (0.0, -7.0, 2.0), (0.0, 0.0, 1.2), lens=30.0, shift_x=0.05)
    name = "p3_room_changed.blend" if changed else "p3_room.blend"
    finish(scene, os.path.join(folder, name))
    return [
        expect(floor, "nodes", textures={"albedo": ["TilesColour", "col"], "roughness": ["TilesRough", "r"],
                                         "normal": ["TilesNormal", "nrm"]}),
        expect(brick_m, "nodes", textures={"albedo": ["BrickColour", "col"], "normal": ["BrickNormal", "nrm"]}),
        expect(plaster_m, "bi", textures={"albedo": ["Plaster", "col"]}),
        expect(grid_m, "bi", textures={"albedo": ["UVGrid", "col"]}),
        expect(glass_m, "nodes", type="translucent", ior=1.5),
        expect(sign_m, "nodes", textures={"emissive": ["Sign", "col"]}, intensity=2.0),
    ]


def main():
    folder = p2.out_dir()
    expected = {"grid": make_grid(folder)}
    expected["room"] = make_room(folder)
    make_room(folder, changed=True)
    # the images of the room's cache: (image, kind tag, DXGI format)
    expected["room_cache"] = [["TilesColour", "col", 98], ["TilesNormal", "nrm", 83], ["TilesRough", "r", 80],
                              ["BrickColour", "col", 98], ["BrickNormal", "nrm", 83], ["Plaster", "col", 98],
                              ["UVGrid", "col", 98], ["Sign", "col", 98]]
    with open(os.path.join(folder, "p3_expected.json"), "w") as f:
        json.dump(expected, f, indent=2)
    print("BGE-Remix: wrote", os.path.join(folder, "p3_expected.json"))


if __name__ == "__main__":
    main()
