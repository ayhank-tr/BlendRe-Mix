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

"""BGE-Remix P9.6: preset materials.

A preset is a group node, "Remix Preset: <name>", wired into the material's
RTX Remix Material node (Albedo, Roughness, Height). Inside: Blender
Internal's procedural textures (clouds, musgrave, voronoi, distorted noise,
wood, marble, stucci, magic, blend) mixed by Math and MixRGB nodes; Brick's
pattern is a group of Math nodes ("Remix Bricks"). The group's inputs: Scale
(repeats per UV unit), Colour (the base colour, linear), Wear, Wetness
(Tarmac and Dirt road only) and Seed (moves the pattern).

The game draws the nodes as they are (the RTX Remix node bumps the surface
by the height, Displace In deep). Remix gets them baked: the material is
marked a tile ("bge_remix_tile"), so its bake is one seamless square over UV
0..1 (its edges cross-faded, remix_bake.cc), its height a height map and
the bump a tangent-space normal map. Units: Scale per UV unit, one UV unit
taken as one metre ("bge_remix_tile_metres"); Displace In and the
subsurface radius (mean free path per channel) in metres.
"""

import bpy
from bpy.types import Menu, Operator
from bpy.props import EnumProperty

from .bge_remix_material import is_remix, make_remix_material, remix_node

# BGE-Remix P10.2: version 2: Brick's value per brick never 0 - 1 (the
# Math node's Modulo gives -1 for 0 in the game's GLSL: one black brick at
# each tile corner, where its column and row hash to 0); the tiles' modes
VERSION = 2
GROUP_PREFIX = "Remix Preset: "
BRICKS_GROUP = "Remix Bricks"

# skin: linear albedo and mean free path (mm) of light and dark skin
# (Christensen and Burley's measured examples); medium is their mean
_SKIN_LIGHT = ((0.623, 0.433, 0.343), (4.82, 1.69, 1.09))
_SKIN_DARK = ((0.436, 0.227, 0.131), (3.67, 1.37, 0.68))
_SKIN_MEDIUM = (tuple((a + b) * 0.5 for a, b in zip(_SKIN_LIGHT[0], _SKIN_DARK[0])),
                tuple((a + b) * 0.5 for a, b in zip(_SKIN_LIGHT[1], _SKIN_DARK[1])))


def _mm(v):
    return tuple(x * 0.001 for x in v)


# key: label, base colour (linear), the albedo's range as factors of the
# colour (the pattern's darkest and lightest), the roughness's range (dry),
# Displace In (m), wetness, and the RTX Remix node's other values
PRESETS = {
    'ROCK': dict(label="Rock", colour=(0.34, 0.33, 0.31), albedo=(0.55, 1.3), rough=(0.65, 0.95),
                 displace=0.03),
    'GRAVEL': dict(label="Gravel", colour=(0.14, 0.13, 0.12), albedo=(0.5, 1.6), rough=(0.7, 0.95),
                   displace=0.02),
    'TARMAC': dict(label="Tarmac", colour=(0.043, 0.041, 0.040), albedo=(0.7, 2.6), rough=(0.6, 0.92),
                   displace=0.005, wet=(0.1, 0.55)),
    'GRASS': dict(label="Grass", colour=(0.105, 0.133, 0.041), albedo=(0.55, 1.6), rough=(0.55, 0.9),
                  displace=0.01),
    'BRICK': dict(label="Brick", colour=(0.262, 0.095, 0.061), albedo=(0.6, 1.4), rough=(0.7, 0.95),
                  displace=0.01, mortar=(0.33, 0.31, 0.28)),
    'BARK': dict(label="Tree bark", colour=(0.08, 0.06, 0.045), albedo=(0.5, 1.6), rough=(0.75, 0.95),
                 displace=0.02),
    'WATER': dict(label="Water", colour=(0.55, 0.85, 0.80), albedo=(0.1, 0.3), rough=(0.02, 0.08),
                  displace=0.004, remix_type='TRANSLUCENT', ior=1.33, distance=1.0),
    'SKIN_LIGHT': dict(label="Skin (light)", colour=_SKIN_LIGHT[0], albedo=(0.9, 1.08), rough=(0.35, 0.6),
                       displace=0.0015, subsurface=_mm(_SKIN_LIGHT[1])),
    'SKIN_MEDIUM': dict(label="Skin (medium)", colour=_SKIN_MEDIUM[0], albedo=(0.9, 1.08), rough=(0.35, 0.6),
                        displace=0.0015, subsurface=_mm(_SKIN_MEDIUM[1])),
    'SKIN_DARK': dict(label="Skin (dark)", colour=_SKIN_DARK[0], albedo=(0.9, 1.08), rough=(0.35, 0.6),
                      displace=0.0015, subsurface=_mm(_SKIN_DARK[1])),
    'DIRT': dict(label="Dirt road / mud", colour=(0.256, 0.118, 0.091), albedo=(0.6, 1.3), rough=(0.7, 0.95),
                 displace=0.02, wet=(0.15, 0.5)),
    'SAND': dict(label="Sand", colour=(0.440, 0.386, 0.231), albedo=(0.85, 1.12), rough=(0.8, 0.98),
                 displace=0.005),
    'SNOW': dict(label="Snow", colour=(0.85, 0.85, 0.87), albedo=(0.7, 1.05), rough=(0.45, 0.8),
                 displace=0.01, subsurface=(0.010, 0.014, 0.020)),
    'CONCRETE': dict(label="Concrete", colour=(0.51, 0.51, 0.50), albedo=(0.7, 1.12), rough=(0.75, 0.95),
                     displace=0.003),
}
# BGE-Remix P10.2: the presets whose pattern repeats exactly inside the
# tile (regular: whole repeats, no cross-fade); the rest are random
# (cross-faded, their contrast kept). The tile modes as remix_bake.cc reads
# them ("bge_remix_tile_mode"; BRMX_bake.h TileMode)
REGULAR = ('BRICK',)
TILE_WHOLE, TILE_KEEP_CONTRAST = 1, 2
ORDER = ('ROCK', 'GRAVEL', 'TARMAC', 'GRASS', 'BRICK', 'BARK', 'WATER', 'SKIN_LIGHT', 'SKIN_MEDIUM',
         'SKIN_DARK', 'DIRT', 'SAND', 'SNOW', 'CONCRETE')
# the group's outputs, and the RTX Remix node's inputs they feed
OUTPUTS = (("Albedo", "Albedo"), ("Roughness", "Roughness"), ("Height", "Height"))


# ---------------------------------------------------------------------------
# building nodes

class _Builder:
    """nodes in a tree (a group), placed in a grid as they come"""

    def __init__(self, tree, prefix):
        self.tree, self.prefix = tree, prefix
        self.n = 0
        self.gin = tree.nodes.new("NodeGroupInput")
        self.gin.location = (-1400, 0)
        self.gout = tree.nodes.new("NodeGroupOutput")
        self.gout.location = (1400, 0)

    def node(self, idname, **props):
        n = self.tree.nodes.new(idname)
        col, row = divmod(self.n, 8)
        n.location = (-1200 + 200 * col, 600 - 160 * row)
        self.n += 1
        for k, v in props.items():
            setattr(n, k, v)
        return n

    def feed(self, sock, v):
        if isinstance(v, bpy.types.NodeSocket):
            self.tree.links.new(v, sock)
        else:
            sock.default_value = v

    def m(self, op, a, b=0.0, clamp=False):
        """a Math node: a op b (floats or sockets)"""
        n = self.node("ShaderNodeMath", operation=op, use_clamp=clamp)
        self.feed(n.inputs[0], a)
        self.feed(n.inputs[1], b)
        return n.outputs[0]

    def rng(self, t, lo, hi, clamp=False):
        """lo + (hi - lo) * t"""
        return self.m('ADD', self.m('MULTIPLY', t, hi - lo), lo, clamp)

    def lerp(self, a, b, t):
        """a + (b - a) * t (a, b floats or sockets)"""
        return self.m('ADD', a, self.m('MULTIPLY', self.m('SUBTRACT', b, a), t))

    def mix(self, blend, c1, c2, fac=1.0, clamp=False):
        """a MixRGB node (colours: sockets, floats as grey, or RGB tuples)"""
        n = self.node("ShaderNodeMixRGB", blend_type=blend, use_clamp=clamp)
        self.feed(n.inputs["Fac"], fac)
        for sock, v in ((n.inputs["Color1"], c1), (n.inputs["Color2"], c2)):
            if isinstance(v, (int, float)):
                v = (v, v, v)
            if isinstance(v, tuple) and len(v) == 3:
                v = v + (1.0,)
            self.feed(sock, v)
        return n.outputs["Color"]

    def rgb(self, r, g, b):
        n = self.node("ShaderNodeCombineRGB")
        for sock, v in zip(n.inputs, (r, g, b)):
            self.feed(sock, v)
        return n.outputs["Image"]

    def sep(self, colour):
        n = self.node("ShaderNodeSeparateRGB")
        self.feed(n.inputs["Image"], colour)
        return n.outputs["R"], n.outputs["G"], n.outputs["B"]

    def tex(self, part, kind, co, **settings):
        """a Texture node holding a new procedural texture, read at co"""
        t = bpy.data.textures.new("%s%s" % (self.prefix, part), kind)
        for k, v in settings.items():
            setattr(t, k, v)
        n = self.node("ShaderNodeTexture", texture=t)
        self.feed(n.inputs["Vector"], co)
        return n

    def stretch(self, co, sx, sy, sz=1.0):
        return self.mix('MULTIPLY', co, (sx, sy, sz))

    def out(self, name, v):
        self.feed(self.gout.inputs[name], v)


def _interface(ng, p):
    """the group's inputs and outputs"""
    def fl(name, default, lo, hi):
        s = ng.inputs.new("NodeSocketFloat", name)
        s.default_value, s.min_value, s.max_value = default, lo, hi
    fl("Scale", 1.0, 0.01, 1000.0)
    c = ng.inputs.new("NodeSocketColor", "Colour")
    c.default_value = tuple(p["colour"]) + (1.0,)
    fl("Wear", 0.3, 0.0, 1.0)
    if p.get("wet"):
        fl("Wetness", 0.0, 0.0, 1.0)
    fl("Seed", 0.0, 0.0, 1000.0)
    ng.outputs.new("NodeSocketColor", "Albedo")
    ng.outputs.new("NodeSocketFloat", "Roughness")
    ng.outputs.new("NodeSocketFloat", "Height")


def _coords(b):
    """the pattern's coordinates: UV 0..1 (the Geometry node's -1..1 halved)
    times Scale, moved by the seed (and by 10, so they stay positive over the
    tile's cross-faded band: the Math node's modulo flips below 0)"""
    scale, seed = b.gin.outputs["Scale"], b.gin.outputs["Seed"]
    geo = b.node("ShaderNodeGeometry")
    half = b.m('MULTIPLY', scale, 0.5)
    sc = b.mix('MULTIPLY', geo.outputs["UV"], half)
    off = [b.m('ADD', half, b.m('ADD', b.m('MULTIPLY', seed, k), 10.0)) for k in (17.17, 31.31, 7.77)]
    return b.mix('ADD', sc, b.rgb(*off))


def _finish(b, p, tone, rough, height, wet_mask=None):
    """Albedo = Colour x tone (clamped), Roughness and Height clamped; wet:
    darker and smoother where wet_mask (0..1) says"""
    colour = b.gin.outputs["Colour"]
    albedo = b.mix('MULTIPLY', colour, tone)
    if p.get("wet"):
        wet_rough, wet_dark = p["wet"]
        w = b.gin.outputs["Wetness"]
        if wet_mask is not None:
            w = b.m('MULTIPLY', w, wet_mask, clamp=True)
        albedo = b.mix('MULTIPLY', albedo, wet_dark, fac=w)
        rough = b.lerp(rough, wet_rough, w)
    b.out("Albedo", b.mix('MIX', albedo, albedo, fac=0.0, clamp=True))
    b.out("Roughness", b.m('ADD', rough, 0.0, clamp=True))
    b.out("Height", b.m('ADD', height, 0.0, clamp=True))


def _wear(b, tone, rough, mask, tone_to, rough_by):
    """wear where mask (0..1): tone towards tone x tone_to, roughness + rough_by"""
    w = b.m('MULTIPLY', b.gin.outputs["Wear"], mask, clamp=True)
    tone = b.m('MULTIPLY', tone, b.lerp(1.0, tone_to, w))
    rough = b.m('ADD', rough, b.m('MULTIPLY', w, rough_by))
    return tone, rough


# the presets' patterns: each gives (tone, roughness, height, wet mask)

def _rock(b, p, co):
    big = b.tex("big", 'CLOUDS', co, noise_scale=0.5, noise_depth=5, noise_basis='IMPROVED_PERLIN')
    fine = b.tex("fine", 'CLOUDS', co, noise_scale=0.06, noise_depth=3, noise_basis='IMPROVED_PERLIN')
    cells = b.tex("cracks", 'VORONOI', co, noise_scale=0.35, weight_1=-1.0, weight_2=1.0)
    crack = b.m('SUBTRACT', 1.0, b.m('MULTIPLY', cells.outputs["Value"], 8.0, clamp=True))
    height = b.m('SUBTRACT', b.m('ADD', b.m('MULTIPLY', big.outputs["Value"], 0.65),
                                 b.m('MULTIPLY', fine.outputs["Value"], 0.35)), b.m('MULTIPLY', crack, 0.5))
    lo, hi = p["albedo"]
    tone = b.m('MULTIPLY', b.rng(fine.outputs["Value"], lo + 0.15, hi / 1.15),
               b.m('SUBTRACT', 1.0, b.m('MULTIPLY', crack, 0.3)))
    rough = b.rng(fine.outputs["Value"], p["rough"][0] + 0.1, p["rough"][1])
    tone, rough = _wear(b, tone, rough, big.outputs["Value"], 1.15, -0.1)
    return tone, rough, height, None


def _gravel(b, p, co):
    stones = b.tex("stones", 'VORONOI', co, noise_scale=0.025, color_mode='POSITION', distance_metric='DISTANCE')
    fine = b.tex("grain", 'CLOUDS', co, noise_scale=0.005, noise_depth=2)
    height = b.m('SUBTRACT', 1.0, b.m('MULTIPLY', stones.outputs["Value"], 1.6))
    r = b.sep(stones.outputs["Color"])[0]
    tone = b.rng(r, p["albedo"][0], p["albedo"][1] / 1.15)
    rough = b.rng(fine.outputs["Value"], p["rough"][0] + 0.05, p["rough"][1])
    # wear: dust settled between the stones
    low = b.m('SUBTRACT', 1.0, b.m('ADD', height, 0.0, clamp=True))
    tone, rough = _wear(b, tone, rough, low, 1.15, 0.0)
    return tone, rough, height, None


def _tarmac(b, p, co):
    grain = b.tex("aggregate", 'VORONOI', co, noise_scale=0.004, color_mode='POSITION')
    macro = b.tex("patches", 'CLOUDS', co, noise_scale=0.3, noise_depth=3)
    stone = b.sep(grain.outputs["Color"])[0]
    bump = b.m('SUBTRACT', 1.0, b.m('MULTIPLY', grain.outputs["Value"], 1.5), clamp=True)
    height = b.m('ADD', b.m('MULTIPLY', bump, 0.6), b.m('MULTIPLY', macro.outputs["Value"], 0.4))
    tone = b.rng(stone, p["albedo"][0], 1.3)
    rough = b.rng(macro.outputs["Value"], p["rough"][0] + 0.1, p["rough"][1])
    # wear: the binder worn and faded, the stones show
    tone, rough = _wear(b, tone, rough, b.rng(macro.outputs["Value"], 0.6, 1.0), 2.0, -0.05)
    # puddles in the hollows
    wet = b.m('SUBTRACT', 1.4, macro.outputs["Value"])
    return tone, rough, height, wet


def _grass(b, p, co):
    blades = b.tex("blades", 'DISTORTED_NOISE', b.stretch(co, 1.0, 0.35), noise_scale=0.02, distortion=2.0)
    patches = b.tex("patches", 'CLOUDS', co, noise_scale=0.4, noise_depth=3)
    v = blades.outputs["Value"]
    height = b.m('ADD', b.m('MULTIPLY', v, 0.8), b.m('MULTIPLY', patches.outputs["Value"], 0.2))
    tone = b.rng(v, p["albedo"][0], 1.25)
    # wear: dry, yellowed patches (a colour, so the tone becomes one)
    w = b.m('MULTIPLY', b.gin.outputs["Wear"], patches.outputs["Value"], clamp=True)
    tone = b.mix('MULTIPLY', tone, (1.6, 1.3, 0.7), fac=w)
    rough = b.rng(v, p["rough"][0], p["rough"][1] - 0.05)
    return tone, rough, height, None


def _bricks_group():
    """Math nodes: brick courses (each row moved half a brick), mortar
    joints, a value per brick. Inputs: Vector (positive), Columns and Rows
    (bricks and courses per unit), Mortar (the joint's width, units), Period
    (the pattern's repeat, units: the value per brick repeats with it)"""
    ng = bpy.data.node_groups.get(BRICKS_GROUP)
    if ng is not None and ng.get("bge_remix_preset_version") == VERSION:
        return ng
    ng = bpy.data.node_groups.new(BRICKS_GROUP, 'ShaderNodeTree')
    ng["bge_remix_preset_version"] = VERSION
    ng.inputs.new("NodeSocketVector", "Vector")
    for name, v in (("Columns", 4.0), ("Rows", 12.0), ("Mortar", 0.01), ("Period", 1.0)):
        s = ng.inputs.new("NodeSocketFloat", name)
        s.default_value = v
        s.min_value, s.max_value = 0.0, 10000.0
    ng.outputs.new("NodeSocketFloat", "Mortar")
    ng.outputs.new("NodeSocketFloat", "Brick")
    ng.outputs.new("NodeSocketFloat", "Edge")
    b = _Builder(ng, "Remix Bricks ")
    g = b.gin.outputs
    x0, y0, _ = b.sep(g["Vector"])
    floor = lambda v: b.m('ROUND', b.m('SUBTRACT', v, 0.5))  # noqa: E731
    y = b.m('MULTIPLY', y0, g["Rows"])
    row = floor(y)
    fy = b.m('SUBTRACT', y, row)
    odd = b.m('MODULO', row, 2.0)
    x = b.m('ADD', b.m('MULTIPLY', x0, g["Columns"]), b.m('MULTIPLY', odd, 0.5))
    col = floor(x)
    fx = b.m('SUBTRACT', x, col)
    mx = b.m('MULTIPLY', g["Mortar"], g["Columns"])
    my = b.m('MULTIPLY', g["Mortar"], g["Rows"])
    mortar = b.m('MAXIMUM', b.m('LESS_THAN', fx, mx), b.m('LESS_THAN', fy, my))
    # the distance to the brick's nearest edge, as a share of the joint
    ex = b.m('MINIMUM', b.m('SUBTRACT', fx, mx), b.m('SUBTRACT', 1.0, fx))
    ey = b.m('MINIMUM', b.m('SUBTRACT', fy, my), b.m('SUBTRACT', 1.0, fy))
    edge = b.m('MINIMUM', b.m('DIVIDE', ex, mx), b.m('DIVIDE', ey, my))
    # a value per brick: a sine hash of its column and row, each taken
    # modulo the pattern's repeat so the tile repeats with it
    cm = b.m('MODULO', col, b.m('MULTIPLY', g["Columns"], g["Period"]))
    rm = b.m('MODULO', row, b.m('MULTIPLY', g["Rows"], g["Period"]))
    s = b.m('SINE', b.m('ADD', b.m('MULTIPLY', cm, 12.9898), b.m('MULTIPLY', rm, 78.233)))
    # its fraction as v - floor(v) (P10.2: not Modulo, which the game's GLSL
    # makes -1 for 0: the brick at column and row 0 came out black)
    v = b.m('MULTIPLY', b.m('ABSOLUTE', s), 43758.5453)
    brick = b.m('SUBTRACT', v, floor(v))
    b.out("Mortar", mortar)
    b.out("Brick", brick)
    b.out("Edge", b.m('ADD', edge, 0.0, clamp=True))
    return ng


def _brick(b, p, co):
    bricks = b.node("ShaderNodeGroup")
    bricks.node_tree = _bricks_group()
    b.feed(bricks.inputs["Vector"], co)
    # whole bricks per tile when Scale is whole (a 215 x 65 mm brick, 10 mm
    # joints: 4 a metre across, 12 courses)
    b.feed(bricks.inputs["Period"], b.gin.outputs["Scale"])
    fine = b.tex("grain", 'CLOUDS', co, noise_scale=0.01, noise_depth=3)
    chips = b.tex("chips", 'CLOUDS', co, noise_scale=0.05, noise_depth=2)
    mortar = bricks.outputs["Mortar"]
    lo, hi = p["albedo"]
    tone = b.m('MULTIPLY', b.rng(bricks.outputs["Brick"], lo / 0.9, hi / 1.1),
               b.rng(fine.outputs["Value"], 0.9, 1.1))
    # the mortar's own colour, as a tone of the brick's colour
    col = p["colour"]
    mort = tuple(m / max(c, 1e-3) for m, c in zip(p["mortar"], col))
    tone = b.mix('MIX', tone, mort, fac=mortar)
    bevel = b.m('ADD', b.m('MULTIPLY', bricks.outputs["Edge"], 4.0), 0.0, clamp=True)
    face = b.m('MULTIPLY', bevel, b.rng(fine.outputs["Value"], 0.8, 1.0))
    # wear: chipped edges and faces
    w = b.m('MULTIPLY', b.gin.outputs["Wear"], chips.outputs["Value"])
    height = b.m('SUBTRACT', face, b.m('MULTIPLY', w, 0.3))
    rough = b.lerp(b.rng(fine.outputs["Value"], p["rough"][0], p["rough"][1] - 0.05), p["rough"][1], mortar)
    return tone, rough, height, None


def _bark(b, p, co):
    ridges = b.tex("ridges", 'MARBLE', b.stretch(co, 1.0, 0.15), marble_type='SHARP', noise_scale=0.12,
                   turbulence=6.0, noise_depth=3)
    flakes = b.tex("flakes", 'STUCCI', co, stucci_type='WALL_OUT', noise_scale=0.03, turbulence=4.0)
    r = ridges.outputs["Value"]
    height = b.m('ADD', b.m('MULTIPLY', r, 0.75), b.m('MULTIPLY', flakes.outputs["Value"], 0.25))
    tone = b.rng(r, p["albedo"][0], 1.2)
    rough = b.rng(flakes.outputs["Value"], p["rough"][0], p["rough"][1])
    # wear: the ridges' tops weathered grey
    tone, rough = _wear(b, tone, rough, r, 1.35, 0.0)
    return tone, rough, height, None


def _water(b, p, co):
    waves = b.tex("waves", 'DISTORTED_NOISE', co, noise_scale=0.4, distortion=0.8)
    ripples = b.tex("ripples", 'CLOUDS', co, noise_scale=0.08, noise_depth=2)
    height = b.m('ADD', b.m('MULTIPLY', waves.outputs["Value"], 0.7), b.m('MULTIPLY', ripples.outputs["Value"], 0.3))
    # the albedo: what stock 2.79 and the fallback show (Remix: the
    # transmittance); wear: foam
    tone = b.rng(ripples.outputs["Value"], p["albedo"][0], p["albedo"][1] * 0.8)
    rough = b.rng(ripples.outputs["Value"], p["rough"][0], p["rough"][0] + 0.03)
    tone, rough = _wear(b, tone, rough, b.m('MULTIPLY', waves.outputs["Value"], 0.5), 1.2, 0.02)
    return tone, rough, height, None


def _skin(b, p, co):
    blotch = b.tex("blotches", 'CLOUDS', co, noise_scale=0.05, noise_depth=3)
    pores = b.tex("pores", 'VORONOI', co, noise_scale=0.003)
    v = blotch.outputs["Value"]
    height = b.m('ADD', b.m('MULTIPLY', pores.outputs["Value"], 0.8), b.m('MULTIPLY', v, 0.2))
    tone = b.rng(v, p["albedo"][0] + 0.02, p["albedo"][1] - 0.02)
    # a little redder in the blotches
    tone = b.mix('MULTIPLY', tone, (1.04, 0.97, 0.97), fac=v)
    rough = b.rng(v, p["rough"][0], p["rough"][1] - 0.1)
    # wear: dry skin, rougher and paler
    tone, rough = _wear(b, tone, rough, 1.0, 1.04, 0.1)
    return tone, rough, height, None


def _dirt(b, p, co):
    ruts = b.tex("ruts", 'CLOUDS', b.stretch(co, 0.4, 1.0), noise_scale=0.6, noise_depth=2)
    clumps = b.tex("clumps", 'DISTORTED_NOISE', co, noise_scale=0.05, distortion=1.5)
    pebbles = b.tex("pebbles", 'VORONOI', co, noise_scale=0.02, color_mode='POSITION')
    peb = b.m('SUBTRACT', 1.0, b.m('MULTIPLY', pebbles.outputs["Value"], 2.0), clamp=True)
    height = b.m('ADD', b.m('ADD', b.m('MULTIPLY', ruts.outputs["Value"], 0.5),
                            b.m('MULTIPLY', clumps.outputs["Value"], 0.35)), b.m('MULTIPLY', peb, 0.15))
    tone = b.m('MULTIPLY', b.rng(clumps.outputs["Value"], p["albedo"][0], 1.15),
               b.rng(b.sep(pebbles.outputs["Color"])[0], 0.95, 1.1))
    rough = b.rng(clumps.outputs["Value"], p["rough"][0] + 0.05, p["rough"][1])
    # wear: tracks pressed smoother
    tone, rough = _wear(b, tone, rough, b.m('SUBTRACT', 1.0, ruts.outputs["Value"]), 0.9, -0.1)
    # mud in the ruts (the low parts)
    wet = b.m('SUBTRACT', 1.5, b.m('MULTIPLY', height, 1.5))
    return tone, rough, height, wet


def _sand(b, p, co):
    ripples = b.tex("ripples", 'WOOD', b.stretch(co, 8.0, 8.0), wood_type='BANDNOISE', noise_basis_2='SIN',
                    turbulence=3.0, noise_scale=0.25)
    grain = b.tex("grain", 'CLOUDS', co, noise_scale=0.002, noise_depth=1)
    height = b.m('ADD', b.m('MULTIPLY', ripples.outputs["Value"], 0.7), b.m('MULTIPLY', grain.outputs["Value"], 0.3))
    tone = b.rng(grain.outputs["Value"], p["albedo"][0], p["albedo"][1])
    rough = b.rng(grain.outputs["Value"], p["rough"][0], p["rough"][1])
    # wear: churned, damper sand
    tone, rough = _wear(b, tone, rough, b.m('SUBTRACT', 1.0, ripples.outputs["Value"]), 0.9, -0.05)
    return tone, rough, height, None


def _snow(b, p, co):
    drifts = b.tex("drifts", 'CLOUDS', co, noise_scale=0.5, noise_depth=4)
    grain = b.tex("grain", 'CLOUDS', co, noise_scale=0.003, noise_depth=1)
    height = b.m('ADD', b.m('MULTIPLY', drifts.outputs["Value"], 0.8), b.m('MULTIPLY', grain.outputs["Value"], 0.2))
    tone = b.rng(grain.outputs["Value"], 0.95, p["albedo"][1])
    rough = b.rng(drifts.outputs["Value"], p["rough"][0] + 0.1, p["rough"][1])
    # wear: trodden and dirty in the hollows
    tone, rough = _wear(b, tone, rough, b.m('SUBTRACT', 1.0, drifts.outputs["Value"]), 0.75, -0.1)
    return tone, rough, height, None


def _concrete(b, p, co):
    blotch = b.tex("blotches", 'CLOUDS', co, noise_scale=0.25, noise_depth=4)
    pits = b.tex("pits", 'VORONOI', co, noise_scale=0.006)
    grain = b.tex("grain", 'STUCCI', co, stucci_type='WALL_IN', noise_scale=0.03, turbulence=2.0)
    stains = b.tex("stains", 'MUSGRAVE', co, musgrave_type='FBM', noise_scale=0.4, octaves=4.0,
                   noise_intensity=1.0)
    pit = b.m('LESS_THAN', pits.outputs["Value"], 0.12)
    height = b.m('SUBTRACT', b.m('SUBTRACT', 1.0, b.m('MULTIPLY', pit, 0.6)),
                 b.m('MULTIPLY', b.m('SUBTRACT', 1.0, grain.outputs["Value"]), 0.2))
    tone = b.rng(blotch.outputs["Value"], p["albedo"][0] + 0.15, p["albedo"][1])
    rough = b.rng(grain.outputs["Value"], p["rough"][0], p["rough"][1])
    tone, rough = _wear(b, tone, rough, stains.outputs["Value"], 0.75, 0.0)
    return tone, rough, height, None


_PATTERNS = {'ROCK': _rock, 'GRAVEL': _gravel, 'TARMAC': _tarmac, 'GRASS': _grass, 'BRICK': _brick,
             'BARK': _bark, 'WATER': _water, 'SKIN_LIGHT': _skin, 'SKIN_MEDIUM': _skin, 'SKIN_DARK': _skin,
             'DIRT': _dirt, 'SAND': _sand, 'SNOW': _snow, 'CONCRETE': _concrete}


def preset_group(key):
    """the preset's node group (made once a file)"""
    p = PRESETS[key]
    name = GROUP_PREFIX + p["label"]
    for ng in bpy.data.node_groups:
        if ng.get("bge_remix_preset") == key and ng.get("bge_remix_preset_version") == VERSION:
            return ng
    ng = bpy.data.node_groups.new(name, 'ShaderNodeTree')
    ng["bge_remix_preset"] = key
    ng["bge_remix_preset_version"] = VERSION
    _interface(ng, p)
    b = _Builder(ng, "Remix Preset %s " % p["label"])
    co = _coords(b)
    tone, rough, height, wet = _PATTERNS[key](b, p, co)
    _finish(b, p, tone, rough, height, wet)
    return ng


def _bricks_fraction_fixed(ng):
    """BGE-Remix P10 (Ayhan's decision, 6 Nov): a version 1 "Remix Bricks"
    group mended in place: its value per brick (Brick) taken as v - floor(v)
    instead of Modulo(v, 1), which the game's GLSL makes -1 for 0 (one black
    brick per tile). Returns whether it was changed."""
    gout = next((n for n in ng.nodes if n.bl_idname == "NodeGroupOutput"), None)
    if gout is None or gout.inputs.get("Brick") is None or not gout.inputs["Brick"].is_linked:
        return False
    link = gout.inputs["Brick"].links[0]
    mod = link.from_node
    if mod.bl_idname != "ShaderNodeMath" or mod.operation != 'MODULO' or not mod.inputs[0].is_linked or \
            mod.inputs[1].is_linked or abs(mod.inputs[1].default_value - 1.0) > 1e-6:
        return False
    v = mod.inputs[0].links[0].from_socket
    x, y = mod.location
    shift = ng.nodes.new("ShaderNodeMath")
    shift.operation = 'SUBTRACT'
    shift.location = (x, y - 160)
    ng.links.new(v, shift.inputs[0])
    shift.inputs[1].default_value = 0.5
    rnd = ng.nodes.new("ShaderNodeMath")
    rnd.operation = 'ROUND'
    rnd.location = (x + 100, y - 160)
    ng.links.new(shift.outputs[0], rnd.inputs[0])
    frac = ng.nodes.new("ShaderNodeMath")
    frac.operation = 'SUBTRACT'
    frac.location = (x + 200, y)
    ng.links.new(v, frac.inputs[0])
    ng.links.new(rnd.outputs[0], frac.inputs[1])
    ng.links.new(frac.outputs[0], gout.inputs["Brick"])
    ng.nodes.remove(mod)
    return True


def upgrade_old_presets():
    """BGE-Remix P10 (Ayhan's decision, 6 Nov): the presets of a file made
    before P10 brought up to this version when the file opens (P9.3's
    conversion calls it): the Brick pattern's group mended in place, so every
    material keeps its settings and only loses the black brick; the presets'
    groups marked with this version (their nodes are the same). Runs once:
    a group at this version is left alone. Returns the report's lines."""
    lines = []
    for ng in list(bpy.data.node_groups):
        if ng.library is not None:
            continue
        if ng.name.startswith(BRICKS_GROUP) and ng.get("bge_remix_preset_version") == 1:
            fixed = _bricks_fraction_fixed(ng)
            ng["bge_remix_preset_version"] = VERSION
            users = sorted(set(m.name for m in bpy.data.materials if m.node_tree is not None and any(
                n.bl_idname == "ShaderNodeGroup" and n.node_tree is not None and n.node_tree.get("bge_remix_preset")
                and any(k.bl_idname == "ShaderNodeGroup" and k.node_tree == ng for k in n.node_tree.nodes)
                for n in m.node_tree.nodes)))
            lines.append("%s: Brick's pattern brought up to P10 (%s; the black brick at each tile's corner "
                         "gone, every setting kept)%s" % (ng.name, "mended" if fixed else "already right",
                                                          ": " + ", ".join(users) if users else ""))
    for ng in list(bpy.data.node_groups):
        if ng.library is None and ng.get("bge_remix_preset") and ng.get("bge_remix_preset_version") == 1:
            ng["bge_remix_preset_version"] = VERSION
    return lines


def preset_node(mat):
    """the material's preset group node, if it has one"""
    if mat is None or mat.node_tree is None:
        return None
    for n in mat.node_tree.nodes:
        if n.bl_idname == "ShaderNodeGroup" and n.node_tree is not None and n.node_tree.get("bge_remix_preset"):
            return n
    return None


def apply_preset(mat, key):
    """mat becomes the preset: an RTX Remix material (made one first if it
    is not) whose node is fed by the preset's group; returns the group node"""
    p = PRESETS[key]
    node = remix_node(mat) if is_remix(mat) else None
    if node is None:
        node = make_remix_material(mat)
    nt = mat.node_tree
    old = preset_node(mat)
    while old is not None:
        nt.nodes.remove(old)
        old = preset_node(mat)
    # what fed the node before is unwired (the nodes stay in the tree)
    for link in list(nt.links):
        if link.to_node == node:
            nt.links.remove(link)
    ng = preset_group(key)
    g = nt.nodes.new("ShaderNodeGroup")
    g.node_tree = ng
    g.label = ng.name
    g.width = 200
    g.location = (node.location[0] - 300, node.location[1])
    for out_name, in_name in OUTPUTS:
        nt.links.new(g.outputs[out_name], node.inputs[in_name])
    # the node's other values
    sets = {"Metallic": 0.0, "Opacity": 1.0, "Emissive Intensity": 0.0, "Displace In": p["displace"],
            "Displace Out": 0.0, "Subsurface": 0.0}
    node.remix_type = p.get("remix_type", 'OPAQUE')
    if node.remix_type == 'TRANSLUCENT':
        sets["IOR"] = p["ior"]
        sets["Transmittance"] = tuple(p["colour"]) + (1.0,)
        sets["Transmittance Distance"] = p["distance"]
    if p.get("subsurface"):
        sets["Subsurface"] = 1.0
        sets["Subsurface Color"] = tuple(p["colour"]) + (1.0,)
        sets["Subsurface Radius"] = tuple(p["subsurface"])
    for name, v in sets.items():
        sock = node.inputs.get(name)
        if sock is not None:
            sock.default_value = v
    # stock 2.79's colour (the Output node, unwired)
    out = next((n for n in nt.nodes if n.bl_idname == "ShaderNodeOutput"), None)
    if out is not None:
        out.inputs["Color"].default_value = tuple(p["colour"]) + (1.0,)
        out.inputs["Alpha"].default_value = 1.0
    # baked for Remix as one seamless tile (remix_bake.cc); P10.2: whole
    # repeats (regular) or a contrast-keeping cross-fade (random)
    mat["bge_remix_tile"] = 1
    mat["bge_remix_tile_metres"] = 1.0
    mat["bge_remix_preset"] = key
    mat["bge_remix_tile_mode"] = TILE_WHOLE if key in REGULAR else TILE_KEEP_CONTRAST
    return g


def preset_inputs(key):
    """BGE-Remix P10.2: the preset's settings a fit sets, with their ranges:
    [(name, lo, hi, whole)] (whole: snapped to whole numbers; Brick's Scale
    to halves, so the tile holds whole repeats)"""
    p = PRESETS[key]
    ins = [("Scale", 0.5, 16.0, False), ("Wear", 0.0, 1.0, False)]
    if p.get("wet"):
        ins.append(("Wetness", 0.0, 1.0, False))
    ins.append(("Seed", 0.0, 20.0, True))
    return ins


class MATERIAL_OT_bge_remix_preset(Operator):
    """Make this material a preset: a node group of procedural textures """ \
        """feeding its RTX Remix Material node"""
    bl_idname = "material.bge_remix_preset"
    bl_label = "RTX Remix Preset"
    bl_options = {'REGISTER', 'UNDO'}

    preset = EnumProperty(
        name="Preset",
        items=[(k, PRESETS[k]["label"], "") for k in ORDER],
    )

    @classmethod
    def poll(cls, context):
        return getattr(context, "material", None) is not None

    def execute(self, context):
        apply_preset(context.material, self.preset)
        return {'FINISHED'}


class MATERIAL_MT_bge_remix_presets(Menu):
    bl_label = "Presets"

    def draw(self, context):
        layout = self.layout
        for k in ORDER:
            layout.operator("material.bge_remix_preset", text=PRESETS[k]["label"]).preset = k


classes = (
    MATERIAL_OT_bge_remix_preset,
    MATERIAL_MT_bge_remix_presets,
)
