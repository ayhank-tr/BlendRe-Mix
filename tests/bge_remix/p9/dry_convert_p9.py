"""BGE-Remix P9.0: the dry conversion (run inside our Blender, background).

Opens each test file named and changes nothing. For every material in use it
says what P9.3's conversion on open would make of it, and for every texture
slot how its settings would reach RTX Remix (P9.4: Remix's own fields, the
mesh's UVs, or a bake).

argv (after --): result.json, then the .blend files
"""

import json
import sys
import traceback

import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
target, files = argv[0], argv[1:]

try:
    import bge_remix  # P8's module: how a material reaches Remix today
except ImportError:
    bge_remix = None

# texture slot maps and the influence each one uses
MAPS = [("use_map_color_diffuse", "diffuse_color_factor", "albedo"),
        ("use_map_alpha", "alpha_factor", "opacity"),
        ("use_map_normal", "normal_factor", "normal"),
        ("use_map_specular", "specular_factor", "roughness"),
        ("use_map_hardness", "hardness_factor", "roughness"),
        ("use_map_color_spec", "specular_color_factor", "roughness"),
        ("use_map_emit", "emit_factor", "emissive"),
        ("use_map_displacement", "displacement_factor", "height"),
        ("use_map_mirror", "mirror_factor", "metallic"),
        ("use_map_raymir", "raymir_factor", "metallic"),
        ("use_map_reflect", "reflection_factor", "albedo"),
        ("use_map_ambient", "ambient_factor", "albedo"),
        ("use_map_translucency", "translucency_factor", "opacity"),
        ("use_map_warp", "warp_factor", "uv")]


def colors_changed(tex):
    return (getattr(tex, "use_color_ramp", False) or abs(tex.intensity - 1.0) > 1e-4 or
            abs(tex.contrast - 1.0) > 1e-4 or abs(tex.saturation - 1.0) > 1e-4 or
            any(abs(getattr(tex, "factor_" + c) - 1.0) > 1e-4 for c in "rgb"))


def slots_of(ma):
    """each texture slot in use: its channels and the way its settings
    reach Remix"""
    out, by_channel, xforms, wraps = [], {}, {}, {}
    for i, slot in enumerate(ma.texture_slots):
        if slot is None or slot.texture is None or not ma.use_textures[i]:
            continue
        tex = slot.texture
        channels, reasons, ways = [], [], set()
        for use, factor, channel in MAPS:
            if getattr(slot, use, False):
                channels.append(channel)
                by_channel.setdefault(channel, []).append(i)
                if abs(getattr(slot, factor, 1.0)) < 1.0 - 1e-4:
                    reasons.append("influence below 1 (%s)" % channel)
        image = tex.type == 'IMAGE' and getattr(tex, "image", None) is not None
        if not image:
            reasons.append("procedural %s" % tex.type.lower())
        else:
            if tex.extension == 'CHECKER':
                reasons.append("Checker extension")
            elif tex.extension != 'REPEAT' or tex.use_mirror_x or tex.use_mirror_y or not tex.use_interpolation:
                ways.add("field")
            xf = (tuple(round(v, 4) for v in slot.scale), tuple(round(v, 4) for v in slot.offset),
                  tex.repeat_x, tex.repeat_y, tex.use_flip_axis)
            if xf != ((1.0, 1.0, 1.0), (0.0, 0.0, 0.0), 1, 1, False):
                ways.add("uvs")
            xforms[i] = xf
            wraps[i] = (tex.extension, tex.use_mirror_x, tex.use_mirror_y, tex.use_interpolation)
            if colors_changed(tex):
                reasons.append("Colors panel")
        if slot.texture_coords != 'UV':
            reasons.append("%s coordinates" % {"ORCO": "generated"}.get(slot.texture_coords,
                                                                       slot.texture_coords.lower()))
        if slot.use_rgb_to_intensity:
            reasons.append("RGB to intensity")
        if slot.invert:
            reasons.append("Negative")
        if slot.blend_type != 'MIX':
            reasons.append("blend %s" % slot.blend_type.lower())
        out.append({"slot": i, "texture": tex.name, "type": tex.type, "channels": channels,
                    "reasons": reasons, "ways": sorted(ways)})
    # several slots on one channel, or image slots that disagree on their
    # transform or their extension: one bake for them
    many = {i for ch, idx in by_channel.items() if len(idx) > 1 and ch != "uv" for i in idx}
    disagree = set()
    if len(set(xforms.values())) > 1:
        disagree |= set(xforms)
    if len(set(wraps.values())) > 1:
        disagree |= set(wraps)
    for s in out:
        if s["slot"] in many:
            s["reasons"].append("several slots on one channel")
        if s["slot"] in disagree:
            s["reasons"].append("textures disagree on transform or extension")
        s["way"] = "bake" if s["reasons"] else ("+".join(s["ways"]) if s["ways"] else "direct")
    return out


def image_feed(link, cycles):
    """an input fed straight by an image texture node (through a normal map
    node at most)"""
    node = link.from_node
    if cycles and node.bl_idname == "ShaderNodeNormalMap" and node.inputs["Color"].is_linked:
        node = node.inputs["Color"].links[0].from_node
    if cycles:
        if node.bl_idname != "ShaderNodeTexImage" or node.image is None:
            return False
        vec = node.inputs.get("Vector")
        return not (vec and vec.is_linked and vec.links[0].from_node.bl_idname not in
                    ("ShaderNodeUVMap", "ShaderNodeTexCoord"))
    if node.bl_idname != "ShaderNodeTexture" or node.texture is None or node.texture.type != 'IMAGE':
        return False
    vec = node.inputs.get("Vector")
    return not (vec and vec.is_linked and vec.links[0].from_node.bl_idname != "ShaderNodeGeometry")


def node_inputs(ma, cycles):
    """the Remix node's inputs as P9.3 would wire them: value, image or bake"""
    nt = ma.node_tree
    res, notes = {}, []
    if cycles:
        outs = [n for n in nt.nodes if n.bl_idname == "ShaderNodeOutputMaterial"]
        out = next((n for n in outs if getattr(n, "is_active_output", False)), outs[0] if outs else None)
        surf = out.inputs["Surface"] if out else None
        if not surf or not surf.is_linked:
            return {"surface": "none"}, ["no surface shader"]
        shader = surf.links[0].from_node
        if shader.bl_idname != "ShaderNodeBsdfPrincipled":
            return {"surface": "bake"}, ["surface shader %s (baked)" % shader.bl_idname[10:]]
        names = {"Base Color": "albedo", "Metallic": "metallic", "Roughness": "roughness", "Normal": "normal",
                 "Emission": "emissive", "Alpha": "opacity", "Transmission": "translucent", "IOR": "ior",
                 "Subsurface": "subsurface"}
        for sock, field in names.items():
            inp = shader.inputs.get(sock)
            if inp is None:
                continue
            res[field] = "value" if not inp.is_linked else ("image" if image_feed(inp.links[0], True) else "bake")
        return res, notes
    mats = [n for n in nt.nodes if n.bl_idname in ("ShaderNodeMaterial", "ShaderNodeExtendedMaterial")]
    outs = [n for n in nt.nodes if n.bl_idname == "ShaderNodeOutput"]
    if not outs:
        return {"surface": "none"}, ["no output node"]
    out = outs[0]
    col = out.inputs.get("Color")
    if col is None or not col.is_linked:
        res["albedo"] = "value"
    elif col.links[0].from_node in mats:
        node = col.links[0].from_node
        for sock, field in (("Color", "albedo"), ("Spec", "roughness"), ("Refl", "albedo"), ("Normal", "normal")):
            inp = node.inputs.get(sock)
            if inp is None:
                continue
            state = "value" if not inp.is_linked else ("image" if image_feed(inp.links[0], False) else "bake")
            # two node inputs on one Remix field: the stronger kind wins
            order = {"value": 0, "image": 1, "bake": 2}
            if field not in res or order[state] > order[res[field]]:
                res[field] = state
        if node.material is not None and node.material != ma:
            notes.append("Material node reads %s" % node.material.name)
    else:
        res["albedo"] = "image" if image_feed(col.links[0], False) else "bake"
        notes.append("no Material node: the colour comes from %s" % col.links[0].from_node.bl_idname[10:])
    alpha = out.inputs.get("Alpha")
    res["opacity"] = "value" if (alpha is None or not alpha.is_linked) else \
        ("image" if image_feed(alpha.links[0], False) else "bake")
    return res, notes


def material_entry(ma, scene):
    nodes = bool(ma.use_nodes and ma.node_tree)
    cycles = nodes and any(n.bl_idname == "ShaderNodeOutputMaterial" for n in ma.node_tree.nodes)
    physical = int(ma.get("bge_physical", 0)) != 0
    block = getattr(ma, "bge_remix", None)
    remix_block = bool(block is not None and getattr(block, "use", False))
    if nodes:
        kind = "Cycles node material" if cycles else "Blender Internal node material"
    elif physical:
        kind = "Physical"
    else:
        kind = "Classic"
    e = {"name": ma.name, "users": ma.users, "kind": kind, "remix_block": remix_block,
         "becomes": "RTX Remix" if (nodes or physical or remix_block) else "Classic"}
    if nodes:
        e["inputs"], e["notes"] = node_inputs(ma, cycles)
        e["nodes"] = len(ma.node_tree.nodes)
    e["slots"] = slots_of(ma)
    if bge_remix is not None:
        try:
            m = bge_remix.material_match(ma)
            if m:
                levels = [f.get("level") for f in m.get("fields", [])]
                e["today"] = {"baked": bool(m.get("baked")), "exact": levels.count(0),
                              "approximately": levels.count(1), "not at all": levels.count(2)}
        except Exception as ex:
            e["today"] = {"error": str(ex)}
    return e


result = {"files": {}, "failed": {}}
for path in files:
    try:
        bpy.ops.wm.open_mainfile(filepath=path, load_ui=False)
        scene = bpy.context.scene
        mats = [material_entry(ma, scene) for ma in bpy.data.materials if ma.users > 0 and not ma.library]
        result["files"][path] = {"scenes": [s.name for s in bpy.data.scenes], "materials": mats,
                                 "engine": scene.render.engine}
        print("BGE-Remix dry conversion: %s, %d materials" % (path, len(mats)), flush=True)
    except Exception:
        result["failed"][path] = traceback.format_exc()
        print("BGE-Remix dry conversion: %s failed" % path, flush=True)

with open(target, "w") as f:
    json.dump(result, f, indent=1)
print("BGE-Remix dry conversion: wrote", target)
