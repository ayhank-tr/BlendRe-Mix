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

"""BGE-Remix P9.2: a material's kind, Classic or RTX Remix.

Picking RTX Remix turns the material into a node material at once: an RTX
Remix Material node as its output (the Output node, unwired, holds its albedo
and opacity for stock 2.79), its values taken from the
material (a Physical material's as they are; a Classic one's colour,
specular hardness, emit and alpha), and a Texture node for every texture
slot, read at the slot's coordinates and wired by what the slot affects.
Picking Classic again switches the nodes off and keeps the tree, so picking
RTX Remix once more brings it back as it was.
"""

import bpy
from bpy.types import Operator

REMIX_NODE = "ShaderNodeRemixMaterial"

# a slot's coordinates as the Geometry node's output (others: Generated)
COORDS = {'UV': "UV", 'ORCO': "Orco", 'GLOBAL': "Global", 'NORMAL': "Normal", 'OBJECT': "Global"}


def remix_node(mat):
    """the material's RTX Remix Material node, its output (one wired to an
    Output node first, as older trees had it)"""
    if mat is None or mat.node_tree is None:
        return None
    nodes = [n for n in mat.node_tree.nodes if n.bl_idname == REMIX_NODE and not n.mute]
    for n in nodes:
        for link in mat.node_tree.links:
            if link.from_node == n and link.to_node.bl_idname == "ShaderNodeOutput":
                return n
    return nodes[0] if nodes else None


def is_remix(mat):
    """an RTX Remix material: nodes on, holding an RTX Remix Material node"""
    return bool(mat) and mat.use_nodes and remix_node(mat) is not None


def kind(mat):
    """'RTX_REMIX', 'PHYSICAL' (P8's, until P9.3 converts it) or 'CLASSIC'"""
    if is_remix(mat):
        return 'RTX_REMIX'
    if mat is not None and not mat.use_nodes and mat.get("bge_physical", 0):
        return 'PHYSICAL'
    return 'CLASSIC'


def hardness_to_roughness(har):
    # the rule BKE_material_hardness_to_roughness uses
    return (2.0 / (max(har, 1.0) + 2.0)) ** 0.25


def _values(mat):
    """the node's values from the material: (inputs by name, remix_type)"""
    v = {}
    physical = bool(mat.get("bge_physical", 0))
    col = tuple(mat.diffuse_color)
    v["Opacity"] = mat.alpha
    remix_type = 'OPAQUE'
    if physical:
        v["Albedo"] = col + (1.0,)
        v["Metallic"] = float(mat.get("bge_metallic", 0.0))
        v["Roughness"] = float(mat.get("bge_roughness", 0.5))
        emission = tuple(mat.get("bge_emission", (1.0, 1.0, 1.0)))[:3]
        v["Emissive Color"] = emission + (1.0,)
        v["Emissive Intensity"] = float(mat.get("bge_emission_strength", 0.0))
        if float(mat.get("bge_transmission", 0.0)) >= 0.5:
            remix_type = 'TRANSLUCENT'
            v["Transmittance"] = col + (1.0,)
        v["IOR"] = float(mat.get("bge_ior", 1.5))
        v["Subsurface"] = float(mat.get("bge_subsurface", 0.0))
        v["Subsurface Color"] = col + (1.0,)
        radius = tuple(mat.get("bge_subsurface_radius", (1.0, 0.2, 0.1)))[:3]
        v["Subsurface Radius"] = radius
    else:
        ref = mat.diffuse_intensity
        v["Albedo"] = (col[0] * ref, col[1] * ref, col[2] * ref, 1.0)
        v["Roughness"] = 1.0 if mat.specular_intensity < 0.01 else hardness_to_roughness(mat.specular_hardness)
        # a ray mirror is a metal (as the translator reads Classic materials)
        mirror = mat.raytrace_mirror
        v["Metallic"] = mirror.reflect_factor if mirror.use else 0.0
        v["Emissive Color"] = col + (1.0,)
        v["Emissive Intensity"] = mat.emit
    return v, remix_type


def _input(node, name):
    return node.inputs.get(name)


def _set(node, name, value):
    sock = _input(node, name)
    if sock is None:
        return
    try:
        sock.default_value = value
    except (TypeError, ValueError):
        pass


def _image_node(nt, slot, geometry, location):
    """a Texture node for a texture slot, read at the slot's coordinates
    (through a Mapping node when the slot moves or scales them)"""
    tex = nt.nodes.new("ShaderNodeTexture")
    tex.texture = slot.texture
    tex.location = location
    coords = COORDS.get(slot.texture_coords, "Orco")
    out = geometry.outputs[coords]
    scale, offset = tuple(slot.scale), tuple(slot.offset)
    if scale != (1.0, 1.0, 1.0) or offset != (0.0, 0.0, 0.0):
        mapping = nt.nodes.new("ShaderNodeMapping")
        mapping.location = (location[0] - 380, location[1])
        mapping.scale = scale
        # the game's slot mapping (size * uv + ofs + 0.5 - 0.5 * size, about
        # the image's centre) as the Mapping node's (size * uv + location)
        mapping.translation = tuple(o + 0.5 - 0.5 * s for o, s in zip(offset[:2], scale[:2])) + (offset[2],)
        nt.links.new(out, mapping.inputs["Vector"])
        out = mapping.outputs["Vector"]
    nt.links.new(out, tex.inputs["Vector"])
    if slot.texture_coords == 'UV' and slot.uv_layer:
        geometry.uv_layer = slot.uv_layer
    return tex


def _data_image(tex):
    """an image read as data (normal, roughness, metallic maps): its colours
    not converted to linear, as the texture slots read them"""
    ima = getattr(tex, "image", None) if tex is not None else None
    if ima is not None:
        try:
            ima.colorspace_settings.name = 'Non-Color'
        except TypeError:
            pass


INTENSITY_LABEL = "Diffuse Intensity"


def _keep_intensity(nt, node, ref):
    """the image wired into Albedo times ref (a MixRGB Multiply node, its
    Color2 the grey ref); nothing when ref is 1 or Albedo is not an image"""
    sock = node.inputs["Albedo"]
    if abs(ref - 1.0) < 1e-4 or not sock.is_linked:
        return None
    link = sock.links[0]
    src = link.from_node
    if src.bl_idname != "ShaderNodeTexture" or src.texture is None or src.texture.type != 'IMAGE':
        return None
    mix = nt.nodes.new("ShaderNodeMixRGB")
    mix.blend_type = 'MULTIPLY'
    mix.label = INTENSITY_LABEL
    mix.location = (src.location[0] + 190, src.location[1] - 40)
    mix.inputs["Fac"].default_value = 1.0
    k = max(0.0, float(ref))
    mix.inputs["Color2"].default_value = (k, k, k, 1.0)
    from_socket = link.from_socket
    nt.links.remove(link)
    nt.links.new(from_socket, mix.inputs["Color1"])
    nt.links.new(mix.outputs["Color"], sock)
    return mix


def make_remix_material(mat):
    """mat becomes an RTX Remix material (see the module's notes); returns
    the RTX Remix Material node"""
    node = remix_node(mat)
    if node is not None:
        # back from Classic: the tree as it was
        mat.use_nodes = True
        return node
    values, remix_type = _values(mat)
    physical = bool(mat.get("bge_physical", 0))
    slots = [s for i, s in enumerate(mat.texture_slots)
             if s is not None and s.texture is not None and mat.use_textures[i]]
    mat.use_nodes = True
    nt = mat.node_tree
    # Blender's default node material (an empty Material node) goes; any
    # other node the tree had stays, unwired from the output
    for n in list(nt.nodes):
        if n.bl_idname == "ShaderNodeMaterial" and n.material is None:
            nt.nodes.remove(n)
    out = next((n for n in nt.nodes if n.bl_idname == "ShaderNodeOutput" and n.is_active_output), None)
    if out is None:
        out = next((n for n in nt.nodes if n.bl_idname == "ShaderNodeOutput"), None)
    if out is None:
        out = nt.nodes.new("ShaderNodeOutput")
    out.location = (300, 0)
    node = nt.nodes.new(REMIX_NODE)
    node.location = (0, 0)
    node.width = 240
    for name, value in values.items():
        _set(node, name, value)
    node.remix_type = remix_type
    for link in list(nt.links):
        if link.to_node == out:
            nt.links.remove(link)
    # the RTX Remix node is the material's output; the Output node, left
    # unwired, holds its albedo and opacity: what stock 2.79, which does not
    # know the node, draws (the node keeps them in step)
    out.label = "Stock 2.79's colour (the RTX Remix node is the output)"
    out.inputs["Color"].default_value = tuple(node.inputs["Albedo"].default_value)[:3] + (1.0,)
    # the texture slots as Texture nodes
    if slots:
        geometry = nt.nodes.new("ShaderNodeGeometry")
        geometry.location = (-760, 0)
        y = 300
        for slot in slots:
            tex = _image_node(nt, slot, geometry, (-380, y))
            y -= 260
            is_image = slot.texture.type == 'IMAGE' and getattr(slot.texture, "image", None) is not None
            if slot.use_map_color_diffuse:
                nt.links.new(tex.outputs["Color"], node.inputs["Albedo"])
            if slot.use_map_alpha:
                nt.links.new(tex.outputs["Value"], node.inputs["Opacity"])
            if slot.use_map_normal:
                nt.links.new(tex.outputs["Color"], node.inputs["Normal"])
                if is_image:
                    _data_image(slot.texture)
            if slot.use_map_hardness:
                if physical:
                    # a Physical material's Hardness slot is its roughness
                    nt.links.new(tex.outputs["Value" if not is_image else "Color"], node.inputs["Roughness"])
                    if is_image:
                        _data_image(slot.texture)
                else:
                    # Classic: bright is hard, so smooth
                    inv = nt.nodes.new("ShaderNodeInvert")
                    inv.location = (-150, y + 120)
                    nt.links.new(tex.outputs["Color"], inv.inputs["Color"])
                    nt.links.new(inv.outputs["Color"], node.inputs["Roughness"])
            if physical and slot.use_map_raymir:
                nt.links.new(tex.outputs["Color"], node.inputs["Metallic"])
                if is_image:
                    _data_image(slot.texture)
            if slot.use_map_emit:
                nt.links.new(tex.outputs["Color"], node.inputs["Emissive Color"])
                if values.get("Emissive Intensity", 0.0) <= 0.0:
                    _set(node, "Emissive Intensity", slot.emit_factor)
    # BGE-Remix P10 (Ayhan's decision, 6 Nov): a Classic material's Diffuse
    # Intensity kept with a colour image: the game multiplies the image by
    # it, so a Multiply node by the intensity goes between the image and
    # Albedo (Remix gets the image times it, remix_material.cc); without an
    # image the Albedo value already carries it (_values)
    if not physical:
        _keep_intensity(nt, node, mat.diffuse_intensity)
    # (an opacity from a texture: opaque for stock 2.79)
    out.inputs["Alpha"].default_value = 1.0 if node.inputs["Opacity"].is_linked else \
        node.inputs["Opacity"].default_value
    # a Physical material's values are the node's now: back to Classic
    # gives Classic
    if "bge_physical" in mat:
        mat["bge_physical"] = 0
    return node


def make_classic_material(mat):
    """mat becomes a Classic material: its nodes off (the tree kept)"""
    mat.use_nodes = False
    if "bge_physical" in mat:
        mat["bge_physical"] = 0


class MATERIAL_OT_bge_remix_make(Operator):
    """Make this an RTX Remix material: a node tree with an RTX Remix Material node, """ \
        """its values and textures taken from the material"""
    bl_idname = "material.bge_remix_make"
    bl_label = "RTX Remix"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        return getattr(context, "material", None) is not None

    def execute(self, context):
        make_remix_material(context.material)
        return {'FINISHED'}


class MATERIAL_OT_bge_remix_classic(Operator):
    """Make this a Classic material: Blender Internal's settings, as 2.79 drew it """ \
        """(an RTX Remix node tree is kept for going back)"""
    bl_idname = "material.bge_remix_classic"
    bl_label = "Classic"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        return getattr(context, "material", None) is not None

    def execute(self, context):
        make_classic_material(context.material)
        return {'FINISHED'}


classes = (
    MATERIAL_OT_bge_remix_make,
    MATERIAL_OT_bge_remix_classic,
)
