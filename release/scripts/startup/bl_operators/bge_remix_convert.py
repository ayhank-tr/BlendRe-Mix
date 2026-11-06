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

"""BGE-Remix P9.3: old materials become RTX Remix materials when a file opens.

Only materials used in a game scene (render engine Blender Game) convert:

- Physical (P8): P9.2's builder, as picking RTX Remix does.
- Cycles node material: a new tree with the RTX Remix node, read by the
  rules P8's translator uses for Remix (remix_material.cc): Principled,
  Diffuse, Glossy, Glass, Refraction, Emission, Subsurface, Transparent; a
  see-through Mix keeps its opacity, a Mix or Add of a surface and an
  emission keeps the glow, any other Mix the stronger surface, Add the first.
  Images wired straight in become Texture nodes.
- Blender Internal node material: the graph stays and feeds the node (what
  feeds the Output node: colour to Albedo, alpha to Opacity); the Output
  node stays wired, so stock 2.79 draws the graph.
- A P8 RTX Remix block in use: its values and images then set the node's
  inputs (the block itself stays: Remix still applies it over the node).

Classic materials without a block in use stay Classic. The original is kept
as a fake-user copy "<name>.p8", removed at the second save. Blender run in
the background (scripts, the tests) converts only when BGE_REMIX_CONVERT=1
or when convert_file() is called; BGE_REMIX_CONVERT=0 turns it off. The report: a line per material in the
console, a summary in the status bar, the text block "BGE-Remix conversion".
"""

import os

import bpy
from bpy.app.handlers import persistent
from bpy.props import StringProperty
from bpy.types import Operator

from .bge_remix_material import (REMIX_NODE, is_remix, remix_node, make_remix_material, hardness_to_roughness,
                                 _set, _data_image)

P8_SUFFIX = ".p8"
SAVES_KEY = "bge_remix_p8_saves"
REPORT_TEXT = "BGE-Remix conversion"


# ---------------------------------------------------------------------------
# which materials


def game_materials():
    """the materials of the objects in game scenes, each once, without the
    .p8 copies and linked ones"""
    mats, seen = [], set()
    for sc in bpy.data.scenes:
        if sc.render.engine != 'BLENDER_GAME':
            continue
        for ob in sc.objects:
            for slot in ob.material_slots:
                ma = slot.material
                if ma is None or ma.name in seen or ma.library is not None or SAVES_KEY in ma:
                    continue
                seen.add(ma.name)
                mats.append(ma)
    return mats


def block_in_use(ma):
    b = ma.get("bge_remix")
    return bool(b) and bool(b.get("use", 0))


def old_kind(ma):
    """'CYCLES', 'BI_NODES', 'PHYSICAL', 'BLOCK' (a Classic material with a
    Remix block in use) or None (stays as it is)"""
    if is_remix(ma):
        return None
    nt = ma.node_tree
    if ma.use_nodes and nt is not None:
        idnames = {n.bl_idname for n in nt.nodes}
        if "ShaderNodeOutputMaterial" in idnames:
            return 'CYCLES'
        if "ShaderNodeOutput" in idnames:
            return 'BI_NODES'
    if ma.get("bge_physical", 0):
        return 'PHYSICAL'
    if block_in_use(ma):
        return 'BLOCK'
    return None


# ---------------------------------------------------------------------------
# Texture nodes for images


def image_texture(ima, closest=False, clamp=None):
    """a Blender Internal image texture showing ima (one already made for it
    with the same settings, or a new one)"""
    ext = clamp or 'REPEAT'
    for tex in bpy.data.textures:
        if (tex.type == 'IMAGE' and tex.image == ima and tex.extension == ext and
                tex.use_interpolation == (not closest)):
            return tex
    tex = bpy.data.textures.new(ima.name, 'IMAGE')
    tex.image = ima
    tex.extension = ext
    tex.use_interpolation = not closest
    return tex


class TreeBuilder:
    """the nodes of a new RTX Remix tree: one Geometry node at UV, a Texture
    node per image"""

    def __init__(self, nt, node):
        self.nt, self.node = nt, node
        self.geometry = None
        self.y = 300

    def texture_node(self, tex):
        if self.geometry is None:
            self.geometry = self.nt.nodes.new("ShaderNodeGeometry")
            self.geometry.location = (-760, 0)
        t = self.nt.nodes.new("ShaderNodeTexture")
        t.texture = tex
        t.location = (-380, self.y)
        self.y -= 260
        self.nt.links.new(self.geometry.outputs["UV"], t.inputs["Vector"])
        return t

    def image_into(self, socket_name, tex, alpha=False, data=False):
        t = self.texture_node(tex)
        self.nt.links.new(t.outputs["Value" if alpha else "Color"], self.node.inputs[socket_name])
        if data:
            _data_image(tex)
        return t


# ---------------------------------------------------------------------------
# Cycles: the translator's rules (remix_material.cc, Cycles part)


class CyclesReading:
    """what Remix reads from a Cycles material: the node's values, its
    images and the notes (anything approximated)"""

    def __init__(self):
        self.values = {"Albedo": (0.64, 0.64, 0.64, 1.0), "Roughness": 0.5, "Metallic": 0.0, "Opacity": 1.0}
        self.images = {}      # input -> (image node, alpha, data)
        self.remix_type = 'OPAQUE'
        self.see_through = False
        self.notes = []

    @staticmethod
    def link(sock):
        return sock.links[0] if sock is not None and sock.is_linked and sock.links else None

    def image_node(self, sock):
        l = self.link(sock)
        if l and l.from_node.bl_idname == "ShaderNodeTexImage" and l.from_node.image is not None:
            return l.from_node, l.from_socket.name
        return None, None

    def unsupported(self, node, name):
        if self.link(node.inputs.get(name)):
            self.notes.append("%s: %s is wired from something Remix does not read; its value is used" %
                              (node.name, name))

    def colour(self, node, name, default=(1.0, 1.0, 1.0)):
        s = node.inputs.get(name)
        if s is None:
            return tuple(default)
        v = tuple(s.default_value)
        return v[:3]

    def value(self, node, name, default):
        s = node.inputs.get(name)
        return float(s.default_value) if s is not None else default

    def colour_or_image(self, node, name, target):
        img, out = self.image_node(node.inputs.get(name))
        if img is not None and out == "Color":
            self.images[target] = (img, False, False)
            return None
        self.unsupported(node, name)
        return self.colour(node, name)

    def value_or_image(self, node, name, default, target):
        img, out = self.image_node(node.inputs.get(name))
        if img is not None:
            self.images[target] = (img, out == "Alpha", True)
            return default
        return self.value(node, name, default)

    def normal_map(self, node):
        l = self.link(node.inputs.get("Normal"))
        if not l:
            return
        nm = l.from_node
        if nm.bl_idname == "ShaderNodeNormalMap":
            if nm.space != 'TANGENT':
                self.notes.append("%s: only tangent-space normal maps are read" % nm.name)
                return
            img, _ = self.image_node(nm.inputs.get("Color"))
            if img is not None:
                self.images["Normal"] = (img, False, True)
                return
        self.notes.append("%s: its normal is wired from something Remix does not read" % node.name)

    def glass_tint(self, c):
        self.values["Transmittance"] = tuple(x * x for x in c[:3]) + (1.0,)

    def albedo(self, c):
        if c is not None:
            self.values["Albedo"] = tuple(c[:3]) + (1.0,)

    def principled(self, n):
        self.albedo(self.colour_or_image(n, "Base Color", "Albedo"))
        self.values["Metallic"] = self.value_or_image(n, "Metallic", 0.0, "Metallic")
        self.values["Roughness"] = self.value_or_image(n, "Roughness", 0.5, "Roughness")
        self.normal_map(n)
        transmission = self.value(n, "Transmission", 0.0)
        subsurface = self.value(n, "Subsurface", 0.0)
        if transmission > 0.5:
            self.remix_type = 'TRANSLUCENT'
            self.values["IOR"] = self.value(n, "IOR", 1.45)
            self.glass_tint(self.values["Albedo"])
            if "Albedo" in self.images:
                self.notes.append("%s: glass tinted by an image, the image is left out" % n.name)
                del self.images["Albedo"]
        elif subsurface > 0.0:
            self.values["Subsurface"] = 1.0
            self.values["Subsurface Color"] = self.colour(n, "Subsurface Color", (0.8, 0.8, 0.8)) + (1.0,)
            radius = self.colour(n, "Subsurface Radius", (1.0, 0.2, 0.1))
            self.values["Subsurface Radius"] = tuple(r * subsurface for r in radius)

    def shader(self, n, depth=0):
        if n is None or depth > 8:
            return
        t = n.bl_idname
        if t == "ShaderNodeBsdfPrincipled":
            self.principled(n)
        elif t == "ShaderNodeBsdfDiffuse":
            self.albedo(self.colour_or_image(n, "Color", "Albedo"))
            self.values["Roughness"], self.values["Metallic"] = 1.0, 0.0
            self.normal_map(n)
        elif t == "ShaderNodeBsdfGlossy":
            self.albedo(self.colour_or_image(n, "Color", "Albedo"))
            self.values["Roughness"] = self.value_or_image(n, "Roughness", 0.5, "Roughness")
            self.values["Metallic"] = 1.0
            self.normal_map(n)
        elif t in ("ShaderNodeBsdfGlass", "ShaderNodeBsdfRefraction"):
            self.remix_type = 'TRANSLUCENT'
            self.glass_tint(self.colour(n, "Color"))
            self.values["IOR"] = self.value(n, "IOR", 1.45)
        elif t == "ShaderNodeEmission":
            self.values["Albedo"] = (0.0, 0.0, 0.0, 1.0)
            self.values["Roughness"] = 1.0
            c = self.colour_or_image(n, "Color", "Emissive Color")
            if c is not None:
                self.values["Emissive Color"] = tuple(c) + (1.0,)
            self.values["Emissive Intensity"] = self.value(n, "Strength", 1.0)
        elif t == "ShaderNodeSubsurfaceScattering":
            c = self.colour_or_image(n, "Color", "Albedo")
            self.albedo(c)
            self.values["Subsurface"] = 1.0
            self.values["Subsurface Color"] = self.values["Albedo"]
            radius = self.colour(n, "Radius", (1.0, 1.0, 1.0))
            scale = self.value(n, "Scale", 1.0)
            self.values["Subsurface Radius"] = tuple(r * scale for r in radius)
            self.values["Roughness"] = 1.0
            self.normal_map(n)
        elif t == "ShaderNodeBsdfTransparent":
            self.values["Opacity"] = 0.0
            self.see_through = True
        elif t == "ShaderNodeMixShader":
            self.mix(n, depth)
        elif t == "ShaderNodeAddShader":
            self.add(n, depth)
        else:
            self.notes.append("%s: not a shader Remix reads; a flat colour is used" % n.name)
            s = n.inputs.get("Color")
            if s is not None and not s.is_linked:
                self.albedo(tuple(s.default_value))

    def from_socket(self, sock):
        l = self.link(sock)
        return l.from_node if l else None

    def mix(self, n, depth):
        fac_sock = n.inputs[0]
        a, b = self.from_socket(n.inputs[1]), self.from_socket(n.inputs[2])
        ta = a is not None and a.bl_idname == "ShaderNodeBsdfTransparent"
        tb = b is not None and b.bl_idname == "ShaderNodeBsdfTransparent"
        fac = float(fac_sock.default_value)
        if ta != tb:
            # see-through: the other shader, opacity from the factor or from
            # the colour image's alpha
            other = b if ta else a
            self.shader(other, depth + 1)
            img, out = self.image_node(fac_sock)
            albedo = self.images.get("Albedo")
            if img is not None and out == "Alpha" and albedo and albedo[0].image == img.image:
                self.images["Opacity"] = (img, True, False)
                self.values["Opacity"] = 1.0
            else:
                self.values["Opacity"] = fac if ta else 1.0 - fac
            self.see_through = True
            return
        ea = a is not None and a.bl_idname == "ShaderNodeEmission"
        eb = b is not None and b.bl_idname == "ShaderNodeEmission"
        if ea != eb:
            # a surface that also glows
            emission = a if ea else b
            self.shader(b if ea else a, depth + 1)
            self.values["Emissive Color"] = self.colour(emission, "Color") + (1.0,)
            self.values["Emissive Intensity"] = self.value(emission, "Strength", 1.0) * ((1.0 - fac) if ea else fac)
            return
        self.notes.append("%s: a mix of two surfaces, the stronger one is used" % n.name)
        self.shader(a if fac < 0.5 else b, depth + 1)

    def add(self, n, depth):
        a, b = self.from_socket(n.inputs[0]), self.from_socket(n.inputs[1])
        if b is not None and b.bl_idname == "ShaderNodeEmission" and (a is None or a.bl_idname != "ShaderNodeEmission"):
            a, b = b, a
        if a is not None and a.bl_idname == "ShaderNodeEmission" and b is not None:
            self.shader(b, depth + 1)
            self.values["Emissive Color"] = self.colour(a, "Color") + (1.0,)
            self.values["Emissive Intensity"] = self.value(a, "Strength", 1.0)
            return
        self.notes.append("%s: two surfaces added, the first one is used" % n.name)
        self.shader(a, depth + 1)


def cycles_output(nt):
    outs = [n for n in nt.nodes if n.bl_idname == "ShaderNodeOutputMaterial"]
    for n in outs:
        if getattr(n, "is_active_output", False):
            return n
    return outs[0] if outs else None


def convert_cycles(ma):
    """ma's Cycles tree becomes an RTX Remix tree (the Cycles tree stays in
    the .p8 copy); returns the notes"""
    nt = ma.node_tree
    r = CyclesReading()
    out = cycles_output(nt)
    surface = r.from_socket(out.inputs["Surface"]) if out is not None else None
    if surface is None:
        r.notes.append("no surface shader: Blender's default grey")
    else:
        r.shader(surface)
    # the images as Blender Internal textures (before the Cycles nodes go)
    textures = {}
    for target, (img, alpha, data) in r.images.items():
        closest = getattr(img, "interpolation", 'Linear') == 'Closest'
        ext = {'EXTEND': 'EXTEND', 'CLIP': 'CLIP'}.get(getattr(img, "extension", 'REPEAT'))
        textures[target] = (image_texture(img.image, closest, ext), alpha, data)
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    output = nt.nodes.new("ShaderNodeOutput")
    output.location = (300, 0)
    node = nt.nodes.new(REMIX_NODE)
    node.location = (0, 0)
    node.width = 240
    for name, value in r.values.items():
        _set(node, name, value)
    node.remix_type = r.remix_type
    if r.see_through and ma.game_settings.alpha_blend == 'OPAQUE':
        ma.game_settings.alpha_blend = 'ALPHA'
    b = TreeBuilder(nt, node)
    for target, (tex, alpha, data) in textures.items():
        b.image_into(target, tex, alpha=alpha, data=data)
    # stock 2.79 draws the Output node's own colour and alpha
    output.label = "Stock 2.79's colour (the RTX Remix node is the output)"
    output.inputs["Color"].default_value = tuple(node.inputs["Albedo"].default_value)[:3] + (1.0,)
    output.inputs["Alpha"].default_value = 1.0 if node.inputs["Opacity"].is_linked else \
        node.inputs["Opacity"].default_value
    # the material's own colour too, for anything that reads it
    ma.diffuse_color = tuple(node.inputs["Albedo"].default_value)[:3]
    return r.notes


# ---------------------------------------------------------------------------
# Blender Internal node materials: the graph feeds the node


def convert_bi_nodes(ma):
    nt = ma.node_tree
    notes = []
    out = next((n for n in nt.nodes if n.bl_idname == "ShaderNodeOutput" and n.is_active_output), None)
    if out is None:
        out = next((n for n in nt.nodes if n.bl_idname == "ShaderNodeOutput"), None)
    node = nt.nodes.new(REMIX_NODE)
    node.location = (out.location[0] + 300, out.location[1] - 260) if out is not None else (0, 0)
    node.width = 240
    # the Material node's specular and hardness (the first one wired)
    mats = [n for n in nt.nodes if n.bl_idname in ("ShaderNodeMaterial", "ShaderNodeExtendedMaterial") and
            n.material is not None]
    if mats:
        m = mats[0].material
        _set(node, "Roughness", 1.0 if m.specular_intensity < 0.01 else hardness_to_roughness(m.specular_hardness))
        _set(node, "Emissive Color", tuple(m.diffuse_color) + (1.0,))
        _set(node, "Emissive Intensity", m.emit)
    if out is not None:
        for out_name, in_name in (("Color", "Albedo"), ("Alpha", "Opacity")):
            sock = out.inputs[out_name]
            if sock.is_linked:
                nt.links.new(sock.links[0].from_socket, node.inputs[in_name])
            else:
                _set(node, in_name, tuple(sock.default_value) if in_name == "Albedo" else sock.default_value)
    else:
        notes.append("no Output node: the RTX Remix node keeps its defaults")
    if node.inputs["Albedo"].is_linked or node.inputs["Opacity"].is_linked:
        notes.append("the node graph feeds the node: Remix gets it baked")
    return notes


# ---------------------------------------------------------------------------
# the P8 RTX Remix block: its values and images into the node


BLOCK_VALUES = (
    # (block slot, node input, kind: 'c' colour, 'v' value)
    ("albedo", "Albedo", 'c'), ("opacity", "Opacity", 'v'), ("roughness", "Roughness", 'v'),
    ("metallic", "Metallic", 'v'), ("emissive", "Emissive Color", 'c'),
    ("transmittance", "Transmittance", 'c'), ("sss_transmittance", "Subsurface Color", 'c'),
    ("sss_radius", "Subsurface Radius", 'r'),
)
BLOCK_IMAGES = {"albedo": "Albedo", "opacity": "Opacity", "roughness": "Roughness", "metallic": "Metallic",
                "normal": "Normal", "emissive": "Emissive Color", "height": "Height",
                "transmittance": "Transmittance"}


def apply_block(ma, node):
    """the block's values and images set the node's inputs (the block stays:
    Remix applies it over the node as it did over the translation)"""
    b = ma.get("bge_remix")
    if not b:
        return []
    notes = []
    try:
        cats = int(b.get("categories", 0))
    except (TypeError, ValueError):
        cats = 0
    if cats:
        items = node.bl_rna.properties["categories"].enum_items
        # the node's values are the block's (bge_remix_bridge.h), shifted
        # by SH_NODE_REMIX_CATEGORY_SHIFT (4)
        node.categories = {it.identifier for it in items if (it.value >> 4) & cats}
    if not b.get("use", 0):
        return notes
    builder = TreeBuilder(ma.node_tree, node)
    builder.y = -400
    for slot, name, kind in BLOCK_VALUES:
        s = b.get(slot)
        if not s or not s.get("use", 0):
            continue
        if kind in ('c', 'r') and "color" in s:
            c = tuple(s["color"])[:3]
            _set(node, name, c if kind == 'r' else c + (1.0,))
        elif kind == 'v' and "value" in s:
            _set(node, name, float(s["value"]))
        if slot == "emissive":
            _set(node, "Emissive Intensity", float(b.get("emissive_intensity", 1.0)))
    for slot, name in BLOCK_IMAGES.items():
        s = b.get(slot)
        ima = s.get("image") if s and s.get("use", 0) else None
        if isinstance(ima, bpy.types.Image):
            builder.image_into(name, image_texture(ima), alpha=(slot == "opacity"),
                               data=slot in ("roughness", "metallic", "normal", "height"))
    if b.get("use_glass", 0):
        node.remix_type = 'TRANSLUCENT'
        _set(node, "IOR", float(b.get("ior", 1.5)))
        _set(node, "Transmittance Distance", float(b.get("transmittance_distance", 0.5)))
        node.use_thin_walled = bool(b.get("thin_walled", 0))
        _set(node, "Thin Wall Thickness", float(b.get("thin_wall_thickness", 0.001)))
    if b.get("use_subsurface", 0) and node.remix_type != 'TRANSLUCENT':
        _set(node, "Subsurface", 1.0)
    if b.get("use_anisotropy", 0):
        _set(node, "Anisotropy", float(b.get("anisotropy", 0.0)))
    if b.get("use_thin_film", 0):
        _set(node, "Thin Film", float(b.get("thin_film_thickness", 400.0)))
    if b.get("use_displacement", 0):
        _set(node, "Displace In", float(b.get("displace_in", 0.05)))
        _set(node, "Displace Out", float(b.get("displace_out", 0.0)))
    notes.append("its RTX Remix block set the node's inputs (the block still applies in Remix)")
    return notes


# ---------------------------------------------------------------------------
# one material, the file


def keep_original(ma):
    """the original as a fake-user copy <name>.p8 (removed at the second
    save)"""
    name = ma.name + P8_SUFFIX
    old = bpy.data.materials.get(name)
    if old is not None:
        bpy.data.materials.remove(old)
    copy = ma.copy()
    copy.name = name
    copy.use_fake_user = True
    copy[SAVES_KEY] = 0
    return copy


def convert_material(ma):
    """ma becomes an RTX Remix material; returns (old kind, notes) or None"""
    k = old_kind(ma)
    if k is None:
        return None
    keep_original(ma)
    if k == 'CYCLES':
        notes = convert_cycles(ma)
        node = remix_node(ma)
    elif k == 'BI_NODES':
        notes = convert_bi_nodes(ma)
        node = remix_node(ma)
    else:
        node = make_remix_material(ma)
        notes = []
    notes += apply_block(ma, node)
    # stock 2.79's colour: the node's albedo, the block's too (an Output node
    # still wired, a Blender Internal graph's, keeps drawing the graph)
    out = next((n for n in ma.node_tree.nodes if n.bl_idname == "ShaderNodeOutput"), None)
    if out is not None and not out.inputs["Color"].is_linked:
        out.inputs["Color"].default_value = tuple(node.inputs["Albedo"].default_value)[:3] + (1.0,)
    return k, notes


KIND_NAMES = {'CYCLES': "a Cycles node material", 'BI_NODES': "a Blender Internal node material",
              'PHYSICAL': "a Physical material", 'BLOCK': "a Classic material with its RTX Remix block in use"}


def convert_file():
    """every game material that converts; returns the report's lines"""
    lines = []
    for ma in game_materials():
        try:
            r = convert_material(ma)
        except Exception as e:  # a material that fails stays as it was (its .p8 copy is there)
            lines.append("%s: not converted (%s: %s)" % (ma.name, type(e).__name__, e))
            continue
        if r is None:
            continue
        k, notes = r
        lines.append("%s: converted from %s%s" % (ma.name, KIND_NAMES[k], "; " + "; ".join(notes) if notes else ""))
    return lines


def write_report(lines):
    text = bpy.data.texts.get(REPORT_TEXT) or bpy.data.texts.new(REPORT_TEXT)
    text.clear()
    text.write("BGE-Remix P9.3: materials converted to RTX Remix when this file opened\n"
               "(each original is kept as <name>.p8 until the file is saved twice)\n\n")
    text.write("\n".join(lines) + "\n")
    for l in lines:
        print("BGE-Remix conversion: " + l)


# ---------------------------------------------------------------------------
# when a file opens, when it is saved


def conversion_wanted():
    """on in Blender with a window; in the background (scripts, the tests)
    only with BGE_REMIX_CONVERT=1; never with BGE_REMIX_CONVERT=0"""
    env = os.environ.get("BGE_REMIX_CONVERT")
    if env == "0":
        return False
    return env == "1" or not bpy.app.background


@persistent
def convert_on_load(_dummy):
    if not conversion_wanted():
        return
    # BGE-Remix P10: old presets brought up to date first (Brick's black brick)
    try:
        from .bge_remix_presets import upgrade_old_presets
        upgraded = upgrade_old_presets()
    except Exception as e:  # the file opens as it was
        upgraded = ["the presets were not brought up to date (%s: %s)" % (type(e).__name__, e)]
    lines = upgraded + convert_file()
    if not lines:
        return
    write_report(lines)
    n = sum(1 for l in lines if ": converted from " in l)
    msg = "BGE-Remix: %d material%s converted to RTX Remix (see the text \"%s\")" % (n, "" if n == 1 else "s",
                                                                                        REPORT_TEXT)
    if upgraded:
        msg += "; presets brought up to date"
    print(msg)
    if not bpy.app.background:
        try:
            bpy.ops.material.bge_remix_convert_report('EXEC_DEFAULT', message=msg)
        except RuntimeError:
            pass


@persistent
def count_saves(_dummy):
    """the .p8 copies go at the second save"""
    for ma in list(bpy.data.materials):
        if SAVES_KEY in ma:
            ma[SAVES_KEY] = int(ma[SAVES_KEY]) + 1
            if ma[SAVES_KEY] >= 2:
                bpy.data.materials.remove(ma)


class MATERIAL_OT_bge_remix_convert_report(Operator):
    """Show BGE-Remix's material conversion in the status bar"""
    bl_idname = "material.bge_remix_convert_report"
    bl_label = "RTX Remix conversion"
    bl_options = {'INTERNAL'}

    message = StringProperty()

    def execute(self, context):
        self.report({'INFO'}, self.message)
        return {'FINISHED'}

    @classmethod
    def register(cls):
        if convert_on_load not in bpy.app.handlers.load_post:
            bpy.app.handlers.load_post.append(convert_on_load)
        if count_saves not in bpy.app.handlers.save_pre:
            bpy.app.handlers.save_pre.append(count_saves)

    @classmethod
    def unregister(cls):
        if convert_on_load in bpy.app.handlers.load_post:
            bpy.app.handlers.load_post.remove(convert_on_load)
        if count_saves in bpy.app.handlers.save_pre:
            bpy.app.handlers.save_pre.remove(count_saves)


classes = (
    MATERIAL_OT_bge_remix_convert_report,
)
