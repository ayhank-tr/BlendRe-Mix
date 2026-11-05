# BGE-Remix P10: the texture remaster in Blender (bpy): the audit (P10.1),
# the catalogue and the fits (P10.2), derived channels and the value check
# (P10.3), the preview and Apply (P10.5). The measures themselves are in
# remaster.py (numpy only).
#
# Python 3.5 (Blender 2.79): no f-strings, no variable annotations.

import math
import os
import time

import bpy
import numpy as np

from . import remaster as R

try:
    import _bge_remix
except ImportError:  # a build without WITH_REMIX
    _bge_remix = None

AUDIT_TEXT = "BGE-Remix remaster audit"
CATALOGUE_VERSION = 1           # the catalogue's layout (with the presets' VERSION in its file names)
CATALOGUE_SIZE = R.WORK         # 256 px bakes
REMASTER_FOLDER = "remaster"    # P10.5: derived images, next to the .blend (the proposed answer)
BAKE_SIZES = (1024, 2048, 4096)
DEFAULT_BAKE_SIZE = 2048
DERIVED_DEPTH = 0.01            # P10.3: a derived height's depth (m), the RTX Remix node's Displace In
PREVIEW_KEY = "bge_remix_remaster_preview"
APPLIED_KEY = "bge_remix_remaster"
SLOT_ALBEDO, SLOT_ROUGHNESS, SLOT_NORMAL, SLOT_HEIGHT = 0, 1, 3, 5

# the last audit (the panel's list), by file
_state = {"audit": None, "fits": {}}


def presets_module():
    from bl_operators import bge_remix_presets as P
    return P


def material_module():
    from bl_operators import bge_remix_material as M
    return M


# ---------------------------------------------------------------------------
# which images the game uses

def game_scenes():
    """the game's scenes (as P9.3's conversion takes them)"""
    return [sc for sc in bpy.data.scenes if sc.render.engine == 'BLENDER_GAME']


REMIX_CHANNELS = {"Albedo": "colour", "Normal": "normal", "Roughness": "roughness", "Height": "height",
                  "Metallic": "metallic", "Emissive Color": "emission", "Opacity": "opacity"}


def _upstream_images(node, depth=0):
    """the images of the Texture nodes feeding a socket's node (through
    Mapping, Invert and the like, a few nodes deep)"""
    out = []
    if node is None or depth > 3:
        return out
    if node.bl_idname == "ShaderNodeTexture" and node.texture is not None and node.texture.type == 'IMAGE' and \
            getattr(node.texture, "image", None) is not None:
        out.append((node.texture.image, node.texture))
        return out
    if node.bl_idname == "ShaderNodeGroup":
        return out
    for sock in node.inputs:
        for link in sock.links:
            out += _upstream_images(link.from_node, depth + 1)
    return out


def material_images(ma):
    """[(image, channel, texture or None, slot or None)] a material uses,
    and the channels it has: {"roughness", "normal", "height", "colour"}"""
    M = material_module()
    found, has = [], set()
    if ma is None:
        return found, has
    if M.is_remix(ma):
        node = M.remix_node(ma)
        for sock in node.inputs:
            ch = REMIX_CHANNELS.get(sock.name)
            if ch is None or not sock.is_linked:
                continue
            has.add(ch)
            for link in sock.links:
                for ima, tex in _upstream_images(link.from_node):
                    found.append((ima, ch, tex, None))
        return found, has
    for i, slot in enumerate(ma.texture_slots):
        if slot is None or slot.texture is None or not ma.use_textures[i]:
            continue
        tex = slot.texture
        image = getattr(tex, "image", None) if tex.type == 'IMAGE' else None
        chans = []
        if slot.use_map_color_diffuse:
            chans.append("colour")
        if slot.use_map_normal:
            chans.append("normal")
        if slot.use_map_hardness:
            chans.append("roughness")
        if slot.use_map_alpha:
            chans.append("opacity")
        if slot.use_map_emit:
            chans.append("emission")
        for ch in chans:
            has.add(ch)
            if image is not None:
                found.append((image, ch, tex, slot))
    return found, has


def _mesh_arrays(ob):
    """an object's mesh as numpy arrays: world vertex positions, loops'
    vertices, polygons' loop starts, totals, materials, the active UV
    layer's coordinates (or None), the face images (or None)"""
    me = ob.data
    nv, nl, np_ = len(me.vertices), len(me.loops), len(me.polygons)
    co = np.zeros(nv * 3, dtype=np.float64)
    me.vertices.foreach_get("co", co)
    co = co.reshape(nv, 3)
    m = np.array(ob.matrix_world, dtype=np.float64)
    co = np.dot(co, m[:3, :3].T) + m[:3, 3]
    lv = np.zeros(nl, dtype=np.int64)
    me.loops.foreach_get("vertex_index", lv)
    ls = np.zeros(np_, dtype=np.int64)
    lt = np.zeros(np_, dtype=np.int64)
    mi = np.zeros(np_, dtype=np.int64)
    me.polygons.foreach_get("loop_start", ls)
    me.polygons.foreach_get("loop_total", lt)
    me.polygons.foreach_get("material_index", mi)
    uv = None
    if me.uv_layers.active is not None:
        uv = np.zeros(nl * 2, dtype=np.float64)
        me.uv_layers.active.data.foreach_get("uv", uv)
        uv = uv.reshape(nl, 2)
    faces = None
    if me.uv_textures.active is not None:
        faces = [d.image for d in me.uv_textures.active.data]
    return co, lv, ls, lt, mi, uv, faces


def _areas(co, lv, ls, lt, uv, polys):
    """the surface area (m^2), the UV area and the UV range (u, v) of the
    chosen polygons (a fan of triangles each)"""
    a3, a2 = 0.0, 0.0
    umin, umax = [1e30, 1e30], [-1e30, -1e30]
    for p in polys:
        s, t = int(ls[p]), int(lt[p])
        if t < 3:
            continue
        idx = np.arange(s, s + t)
        P = co[lv[idx]]
        e1 = P[1:-1] - P[0]
        e2 = P[2:] - P[0]
        a3 += float(np.sqrt((np.cross(e1, e2) ** 2).sum(axis=1)).sum()) * 0.5
        if uv is not None:
            U = uv[idx]
            f1 = U[1:-1] - U[0]
            f2 = U[2:] - U[0]
            a2 += float(np.abs(f1[:, 0] * f2[:, 1] - f1[:, 1] * f2[:, 0]).sum()) * 0.5
            for k in (0, 1):
                umin[k] = min(umin[k], float(U[:, k].min()))
                umax[k] = max(umax[k], float(U[:, k].max()))
    rng = [max(0.0, umax[k] - umin[k]) for k in (0, 1)] if a2 > 0 else [0.0, 0.0]
    return a3, a2, rng


def gather():
    """every image the game scenes' objects use: {image name: {"image",
    "channels": set, "uses": [{"material", "object", "scene", "channel",
    "face", "area", "uv_area", "uv_range", "slot_scale"}], "materials":
    {material name: channels it has}}}"""
    out = {}
    seen_objects = set()
    for sc in game_scenes():
        for ob in sc.objects:
            if ob.type != 'MESH' or ob.data is None or (ob.name, sc.name) in seen_objects:
                continue
            seen_objects.add((ob.name, sc.name))
            arrays = None
            for k, ms in enumerate(ob.material_slots):
                ma = ms.material
                if ma is None or ma.get(PREVIEW_KEY):
                    continue
                imgs, has = material_images(ma)
                # face images: the material draws its faces' own images
                if getattr(ma, "use_face_texture", False) and not material_module().is_remix(ma):
                    if arrays is None:
                        arrays = _mesh_arrays(ob)
                    faces = arrays[6] or []
                    for ima in set(faces[p] for p in range(len(faces)) if arrays[4][p] == k and faces[p]):
                        imgs.append((ima, "colour", None, None))
                        has.add("colour")
                for ima, ch, tex, slot in imgs:
                    if ima is None:
                        continue
                    if arrays is None:
                        arrays = _mesh_arrays(ob)
                    co, lv, ls, lt, mi, uv, faces = arrays
                    polys = [p for p in range(len(ls)) if mi[p] == k and
                             (tex is not None or faces is None or faces[p] == ima)]
                    a3, a2, rng = _areas(co, lv, ls, lt, uv, polys)
                    sx, sy = 1.0, 1.0
                    if slot is not None:
                        sx, sy = abs(slot.scale[0]), abs(slot.scale[1])
                    if tex is not None:
                        sx *= max(getattr(tex, "repeat_x", 1), 1)
                        sy *= max(getattr(tex, "repeat_y", 1), 1)
                    e = out.setdefault(ima.name, {"image": ima, "channels": set(), "uses": [], "materials": {}})
                    e["channels"].add(ch)
                    e["materials"][ma.name] = sorted(has)
                    e["uses"].append({"material": ma.name, "object": ob.name, "scene": sc.name, "channel": ch,
                                      "face": tex is None, "area": a3, "uv_area": a2 * sx * sy,
                                      "uv_range": [rng[0] * sx, rng[1] * sy], "slot": k})
    return out


# ---------------------------------------------------------------------------
# pixels

def image_rgb(ima, size=None):
    """an image's colour (linear, rows bottom first) as an (h, w, 3) array,
    made size x size first when given (on a copy: the image is not
    changed); None without pixels"""
    if ima is None or ima.size[0] <= 0 or ima.size[1] <= 0:
        return None
    work = ima
    copy = None
    try:
        if size is not None and (ima.size[0] > size or ima.size[1] > size):
            copy = ima.copy()
            copy.scale(size, size)
            work = copy
        w, h = work.size[0], work.size[1]
        if w <= 0 or h <= 0:
            return None
        px = R.from_floats(work.pixels[:], w, h, 4)
        float_image = bool(getattr(ima, "is_float", False))
    finally:
        if copy is not None:
            bpy.data.images.remove(copy)
    rgb = px[..., :3].astype(np.float64)
    return rgb if float_image else R.srgb_to_linear(rgb)


def baked_rgb(data, size):
    """a bake's colour (sRGB bytes) as linear (size, size, 3)"""
    return R.srgb_to_linear(R.from_bytes(data, size, size)[..., :3].astype(np.float64))


# ---------------------------------------------------------------------------
# P10.1: the audit

def _audit_image(name, e):
    ima = e["image"]
    w, h = ima.size[0], ima.size[1]
    uses = e["uses"]
    colour = "colour" in e["channels"]
    densities = []
    for u in uses:
        if u["area"] > 0 and u["uv_area"] > 0:
            densities.append((R.texel_density(w, h, u["uv_area"], u["area"]), u["object"]))
    densities.sort()
    density = densities[0][0] if densities else 0.0
    repeats = [0.0, 0.0]
    for u in uses:
        repeats = [max(repeats[0], u["uv_range"][0]), max(repeats[1], u["uv_range"][1])]
    missing = set()
    for ma_name, has in e["materials"].items():
        for ch in ("roughness", "normal", "height"):
            if ch not in has:
                missing.add(ch)
    row = {"image": name, "size": [w, h], "channels": sorted(e["channels"]),
           "used_by": sorted(set("%s (%s)" % (u["material"], u["object"]) for u in uses)),
           "materials": sorted(e["materials"]), "objects": sorted(set(u["object"] for u in uses)),
           "missing": [c for c in ("roughness", "normal", "height") if c in missing],
           "density": round(density, 1), "density_object": densities[0][1] if densities else "",
           "densities": [[round(d, 1), o] for d, o in densities],
           "repeats": [round(repeats[0], 3), round(repeats[1], 3)],
           "sharp": density >= R.SHARP_PX_PER_M, "kind": None, "kind_measures": None,
           "painted_light": None, "painted_side": None, "plan": 'KEEP', "notes": []}
    if not colour:
        row["notes"].append("a data image (%s): kept as it is" % ", ".join(sorted(e["channels"])))
        return row
    rgb = image_rgb(ima, size=R.WORK * 2)
    if rgb is None:
        row["notes"].append("no pixels (missing file?)")
        return row
    grey = R.luminance(rgb)
    m = R.kind_measures(grey)
    row["kind"] = R.kind_of(m)
    row["kind_measures"] = m
    strength, side = R.painted_light(grey)
    row["painted_light"] = round(strength, 3)
    row["painted_side"] = side if strength >= R.PAINTED_LIGHT else None
    if strength >= R.PAINTED_LIGHT:
        row["notes"].append("painted light: darker on the %s (%.2f)" % (side, strength))
    if not row["sharp"]:
        row["notes"].append("below the sharpness target (%.0f px/m, the target %.0f): no upscaling in P10" %
                            (density, R.SHARP_PX_PER_M))
    needs = bool(row["missing"] and set(row["missing"]) & {"roughness", "normal"}) or \
        strength >= R.PAINTED_LIGHT or not row["sharp"]
    if row["kind"] == 'UNIQUE':
        row["plan"] = 'DERIVE' if needs else 'KEEP'
    else:
        row["plan"] = 'FIT' if needs else 'KEEP'
    return row


def game_materials():
    from bl_operators.bge_remix_convert import game_materials as gm
    return [ma for ma in gm() if not ma.get(PREVIEW_KEY)]


def value_check():
    """P10.3: every game material's values against the realistic ranges,
    each problem with a suggested fix: [{"material", "field", "problem",
    "fix"}]; also the Classic note (P10.3's finding)"""
    M = material_module()
    out = []
    for ma in game_materials():
        imgs, _has = material_images(ma)
        colour_img = next((ima for ima, ch, _t, _s in imgs if ch == "colour"), None)
        translucent = False
        if M.is_remix(ma):
            node = M.remix_node(ma)
            translucent = node.remix_type == 'TRANSLUCENT'
            alb = node.inputs["Albedo"]
            mean = tuple(alb.default_value)[:3]
            metallic = node.inputs["Metallic"].default_value
            metallic_image = node.inputs["Metallic"].is_linked
            rough = None if node.inputs["Roughness"].is_linked else node.inputs["Roughness"].default_value
            factor = 1.0
            if alb.is_linked and colour_img is None:
                # a preset or other nodes: their values are checked by the
                # preset table (P9.6)
                continue
        else:
            physical = bool(ma.get("bge_physical", 0))
            factor = 1.0 if physical else ma.diffuse_intensity
            mean = tuple(c * factor for c in ma.diffuse_color)
            metallic = float(ma.get("bge_metallic", 0.0)) if physical else \
                (ma.raytrace_mirror.reflect_factor if ma.raytrace_mirror.use else 0.0)
            metallic_image = False
            rough = float(ma.get("bge_roughness", 0.5)) if physical else \
                (1.0 if ma.specular_intensity < 0.01 else M.hardness_to_roughness(ma.specular_hardness))
            if not physical and colour_img is not None and abs(ma.diffuse_intensity - 1.0) > 1e-3:
                out.append({"material": ma.name, "field": "albedo",
                            "problem": "Classic: the colour image times Diffuse Intensity %.2f in the game" %
                            ma.diffuse_intensity,
                            "fix": "none needed: Remix now gets the image with the intensity (P10.3; it read "
                            "about 25 levels brighter before)"})
        low = high = None
        if colour_img is not None:
            rgb = image_rgb(colour_img, size=128)
            if rgb is not None:
                lum = R.luminance(rgb) * factor
                mean = tuple(float(v) * factor for v in rgb.reshape(-1, 3).mean(axis=0))
                low, high = float(np.percentile(lum, 1)), float(np.percentile(lum, 99))
        for field, problem, fix in R.value_problems(mean, low, high, metallic, metallic_image, rough, translucent):
            out.append({"material": ma.name, "field": field, "problem": problem, "fix": fix})
    return out


def remaster_audit(write_text=True):
    """P10.1: the audit of every image the game scenes use (see the guide);
    it changes nothing in the file but the report's text block (when
    write_text). Returns {"images": [...], "values": [...], "target": px/m}"""
    t0 = time.time()
    rows = []
    for name, e in sorted(gather().items()):
        try:
            rows.append(_audit_image(name, e))
        except Exception as ex:  # an image that fails is reported, the rest go on
            rows.append({"image": name, "error": "%s: %s" % (type(ex).__name__, ex), "plan": 'KEEP', "notes": []})
    try:
        values = value_check()
    except Exception as ex:
        values = [{"material": "", "field": "", "problem": "the value check failed: %s" % ex, "fix": ""}]
    result = {"images": rows, "values": values, "target": R.SHARP_PX_PER_M, "seconds": round(time.time() - t0, 2),
              "scenes": [sc.name for sc in game_scenes()]}
    _state["audit"] = result
    if write_text:
        write_report(result)
    return result


PLAN_NAMES = {'FIT': "fit a preset", 'DERIVE': "derive channels", 'KEEP': "keep"}


def report_lines(result):
    lines = ["BGE-Remix P10.1: the remaster audit (it changes nothing in the file)",
             "Game scenes: %s; the sharpness target %.0f pixels a metre" % (", ".join(result["scenes"]) or "none",
                                                                          result["target"]), ""]
    for r in result["images"]:
        if r.get("error"):
            lines.append("%s: not audited (%s)" % (r["image"], r["error"]))
            continue
        lines.append("%s  %d x %d" % (r["image"], r["size"][0], r["size"][1]))
        lines.append("  used by: %s" % "; ".join(r["used_by"]))
        lines.append("  missing channels: %s" % (", ".join(r["missing"]) or "none"))
        lines.append("  texel density: %.0f px/m (lowest, on %s)%s" % (r["density"], r["density_object"] or "-",
                                                                      "" if r["sharp"] else ", below the target"))
        lines.append("  repeats: %.2g x %.2g" % (r["repeats"][0], r["repeats"][1]))
        if r["kind"]:
            m = r["kind_measures"]
            lines.append("  painted light: %.2f%s" % (r["painted_light"], " (darker on the %s)" % r["painted_side"]
                                                     if r["painted_side"] else ""))
            lines.append("  kind: %s (peaks %.2f, repeat %.2f, edge spread %.2f, edges %.2f)" % (
                R.KIND_NAMES[r["kind"]], m["peaks"], m["self"], m["spread"], m["edges"]))
        lines.append("  plan: %s" % PLAN_NAMES[r["plan"]])
        for n in r["notes"]:
            lines.append("  note: " + n)
        lines.append("")
    lines.append("Values (P10.3), each with a suggested fix:")
    if not result["values"]:
        lines.append("  every material in range")
    for v in result["values"]:
        lines.append("  %s, %s: %s. Fix: %s" % (v["material"], v["field"], v["problem"], v["fix"]))
    return lines


def write_report(result):
    text = bpy.data.texts.get(AUDIT_TEXT) or bpy.data.texts.new(AUDIT_TEXT)
    text.clear()
    text.write("\n".join(report_lines(result)) + "\n")


def last_audit():
    return _state["audit"]


def audit_row(name):
    a = _state["audit"]
    if not a:
        return None
    return next((r for r in a["images"] if r["image"] == name), None)


# ---------------------------------------------------------------------------
# P10.2: bakes of presets

class Scratch:
    """data made while baking (a material, the preset's group and textures)
    removed again afterwards, so the file is as it was"""

    KINDS = ("materials", "node_groups", "textures", "images", "objects", "meshes")

    def __enter__(self):
        self.before = {k: set(x.name for x in getattr(bpy.data, k)) for k in self.KINDS}
        return self

    def __exit__(self, *exc):
        for k in ("objects", "meshes", "materials", "node_groups", "textures", "images"):
            coll = getattr(bpy.data, k)
            for x in [x for x in coll if x.name not in self.before[k]]:
                try:
                    coll.remove(x)
                except (RuntimeError, ReferenceError):
                    pass
        return False


def can_bake():
    return _bge_remix is not None and hasattr(_bge_remix, "bake_tile") and not bpy.app.background


def bake_scene():
    sc = bpy.context.scene
    return sc if sc is not None else bpy.data.scenes[0]


def set_inputs(group_node, settings, colour=None):
    for k, v in settings.items():
        sock = group_node.inputs.get(k)
        if sock is not None:
            sock.default_value = float(v)
    if colour is not None and group_node.inputs.get("Colour") is not None:
        group_node.inputs["Colour"].default_value = tuple(colour)[:3] + (1.0,)


def bake_preset(ma, size, slots=1):
    """the preset material's tile baked (sRGB bytes per slot)"""
    return _bge_remix.bake_tile(ma.as_pointer(), bake_scene().as_pointer(), int(size), int(slots))


class PresetBaker:
    """one scratch material per preset, its settings changed per bake"""

    def __init__(self, key):
        P = presets_module()
        self.key = key
        self.ma = bpy.data.materials.new("BGE-Remix remaster bake " + key)
        self.ma[PREVIEW_KEY] = 1
        self.node = P.apply_preset(self.ma, key)

    def bake(self, settings, size=CATALOGUE_SIZE, colour=None, slots=1):
        set_inputs(self.node, settings, colour)
        return bake_preset(self.ma, size, slots)

    def albedo(self, settings, size=CATALOGUE_SIZE, colour=None):
        return baked_rgb(self.bake(settings, size, colour)[SLOT_ALBEDO], size)


def catalogue_settings(key):
    """about 200 settings per preset: Scale (log steps), Wear, Wetness when
    it has one, Seed"""
    P = presets_module()
    wet = bool(P.PRESETS[key].get("wet"))
    if key in P.REGULAR:
        scales = (0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0)
    else:
        scales = tuple(round(0.5 * (2.0 ** (i * 5.0 / 9.0)), 3) for i in range(10))  # 0.5 .. 16
    wears = (0.0, 0.33, 0.67, 1.0)
    out = []
    if wet:
        for s in scales[:8]:
            for w in wears:
                for t in (0.0, 0.5, 1.0):
                    for seed in (0.0, 3.0):
                        out.append({"Scale": s, "Wear": w, "Wetness": t, "Seed": seed})
    else:
        for s in scales:
            for w in wears:
                for seed in (0.0, 1.0, 2.0, 3.0, 4.0):
                    out.append({"Scale": s, "Wear": w, "Seed": seed})
    return out


def catalogue_folder():
    return bpy.utils.user_resource('DATAFILES', "bge_remix_remaster", create=True)


def catalogue_path(key):
    P = presets_module()
    return os.path.join(catalogue_folder(), "catalogue_%d_%d_%s.npz" % (CATALOGUE_VERSION, P.VERSION, key))


def catalogue_ready(key):
    return os.path.isfile(catalogue_path(key))


def catalogue_build(key):
    """a generator: the preset's catalogue baked entry by entry (yields
    (done, total)), then saved in Blender's user folder"""
    settings = catalogue_settings(key)
    names = sorted(settings[0].keys())
    rows, vals, mains = [], [], []
    with Scratch():
        baker = PresetBaker(key)
        for i, s in enumerate(settings):
            rgb = baker.albedo(s)
            f = R.features(rgb)
            rows.append(R.pack(f))
            vals.append([float(s[n]) for n in names])
            mains.append(R.main_colours(rgb)[0][0])
            yield i + 1, len(settings)
    np.savez(catalogue_path(key), names=np.array(names), settings=np.array(vals), features=np.array(rows),
             mains=np.array(mains))


def catalogue_load(key):
    with np.load(catalogue_path(key)) as d:
        return {"names": [str(n) for n in d["names"]], "settings": d["settings"], "features": d["features"],
                "mains": d["mains"]}


def catalogues_missing():
    return [k for k in presets_module().ORDER if not catalogue_ready(k)]


def build_all_catalogues(progress=None):
    """every missing catalogue, at once (the tests; the panel uses the
    operator, a step at a time)"""
    for key in catalogues_missing():
        for done, total in catalogue_build(key):
            if progress:
                progress(key, done, total)


# ---------------------------------------------------------------------------
# P10.2: fitting

def _colour_prior(key, main):
    """how far the image's main colour is from the preset's own (0..1): it
    tells presets with the same pattern apart (the skins)"""
    P = presets_module()
    c = np.array(P.PRESETS[key]["colour"], dtype=np.float64)
    m = np.maximum(np.array(main, dtype=np.float64), 1e-4)
    lum = lambda v: float(v[0] * 0.2126 + v[1] * 0.7152 + v[2] * 0.0722)  # noqa: E731
    d_lum = min(1.0, abs(math.log(max(lum(m), 1e-4) / max(lum(c), 1e-4))) / math.log(8.0))
    d_hue = min(1.0, float(np.abs(m / max(m.sum(), 1e-6) - c / max(c.sum(), 1e-6)).sum()) / 0.3)
    return 0.5 * d_lum + 0.5 * d_hue


def mesh_metres(row):
    """P10.2: the metres one repeat of the image covers on its mesh (the
    lowest density's mesh): the preset's tile size"""
    e = gather().get(row["image"]) if row else None
    best = None
    for u in (e["uses"] if e else []):
        if u["uv_area"] > 0 and u["area"] > 0:
            m = math.sqrt(u["area"] / u["uv_area"])
            best = m if best is None else max(best, m)
    return best or 1.0


def fit_image(ima, row=None, keys=None, refine=True, candidates=2, iterations=24):
    """P10.2: the image matched against every preset's catalogue, the best
    refined by the simplex search. Returns {"preset", "settings", "colour",
    "score", "distance", "parts", "perceptual", "tile_metres", "ranking",
    "evaluations", "seconds"}"""
    P = presets_module()
    t0 = time.time()
    rgb = image_rgb(ima, size=R.WORK * 2)
    if rgb is None:
        raise RuntimeError("the image has no pixels")
    target = R.features(rgb)
    main = R.main_colours(R.even_out(R.resample(rgb, R.WORK), 1.0))[0][0]
    keys = list(keys or P.ORDER)
    ranking = []
    for key in keys:
        if not catalogue_ready(key):
            continue
        cat = catalogue_load(key)
        best_i, best_d = -1, 1e9
        for i, v in enumerate(cat["features"]):
            d = R.distance(target, R.unpack(v))
            if d < best_d:
                best_i, best_d = i, d
        prior = _colour_prior(key, main)
        ranking.append((best_d + 0.05 * prior, best_d, key, cat, best_i))
    if not ranking:
        raise RuntimeError("no catalogue yet (Build Catalogue first)")
    ranking.sort(key=lambda r: r[0])
    evaluations = 0
    best = None
    with Scratch():
        for _rank, d0, key, cat, i0 in ranking[:max(1, candidates)]:
            names = cat["names"]
            start = {n: float(cat["settings"][i0][k]) for k, n in enumerate(names)}
            ins = [x for x in P.preset_inputs(key) if x[0] in start]
            free = [x for x in ins if not x[3]]
            baker = PresetBaker(key) if (refine and can_bake()) else None

            def settings_of(x):
                s = dict(start)
                for (name, lo, hi, _w), v in zip(free, x):
                    if name == "Scale":
                        s[name] = math.exp(math.log(lo) + float(v) * (math.log(hi) - math.log(lo)))
                    else:
                        s[name] = lo + float(v) * (hi - lo)
                if key in P.REGULAR:
                    s["Scale"] = max(0.5, round(s["Scale"] * 2.0) / 2.0)
                return s

            def x_of(s):
                x = []
                for name, lo, hi, _w in free:
                    if name == "Scale":
                        x.append((math.log(max(s[name], lo)) - math.log(lo)) / (math.log(hi) - math.log(lo)))
                    else:
                        x.append((s[name] - lo) / (hi - lo))
                return np.clip(np.array(x), 0.0, 1.0)

            cache = {}

            def cost(x):
                s = settings_of(x)
                k = tuple(round(s[n], 4) for n in sorted(s))
                if k not in cache:
                    cache[k] = R.distance(target, R.features(baker.albedo(s)))
                return cache[k]

            if baker is not None:
                x, d, n = R.nelder_mead(cost, x_of(start), step=0.12, iterations=iterations)
                evaluations += n
                s = settings_of(x)
                d = cost(x)
                if key in P.REGULAR:
                    # whole repeats: the snapped Scale's neighbours too
                    for scale in (s["Scale"] - 0.5, s["Scale"] + 0.5):
                        if scale >= 0.5:
                            t = dict(s, Scale=scale)
                            dt = cost(x_of(t))
                            if dt < d:
                                s, d = t, dt
                rgb_fit = baker.albedo(s)
            else:
                s, d = start, d0
                rgb_fit = None
            if best is None or d < best["distance"]:
                bake_main = R.main_colours(rgb_fit)[0][0] if rgb_fit is not None else cat["mains"][i0].tolist()
                default = np.array(P.PRESETS[key]["colour"], dtype=np.float64)
                colour = np.clip(default * np.array(main) / np.maximum(np.array(bake_main), 1e-4), 0.0, 1.0)
                fit_f = R.features(rgb_fit) if rgb_fit is not None else R.unpack(cat["features"][i0])
                best = {"preset": key, "settings": {k: round(float(v), 4) for k, v in s.items()},
                        "colour": [round(float(c), 4) for c in colour], "distance": round(float(d), 4),
                        "parts": R.distance_parts(target, fit_f), "perceptual": R.perceptual(fit_f),
                        "regularity": (R.kind_measures(R.luminance(rgb_fit))["peaks"] if rgb_fit is not None
                                       else None)}
    best["score"] = round(R.score_of(best["distance"]), 3)
    best["ranking"] = [[k, round(float(dd), 4)] for _r, dd, k, _c, _i in ranking[:5]]
    best["evaluations"] = evaluations
    best["tile_metres"] = round(mesh_metres(row or audit_row(ima.name)), 4)
    best["seconds"] = round(time.time() - t0, 2)
    best["plan"] = 'FIT' if best["score"] >= R.SCORE_THRESHOLD else 'DERIVE'
    best["target_perceptual"] = R.perceptual(target)
    _state["fits"][ima.name] = best
    return best


def last_fit(name):
    return _state["fits"].get(name)


# ---------------------------------------------------------------------------
# P10.3: derived channels

def derived_channels(ima, row=None, light_amount=1.0):
    """P10.3: (albedo linear, roughness, height, normal) at the image's own
    size (no upscaling), all approximate"""
    rgb = image_rgb(ima)
    if rgb is None:
        raise RuntimeError("the image has no pixels")
    row = row or audit_row(ima.name) or {}
    kind = row.get("kind") or 'UNIQUE'
    density = row.get("density") or 0.0
    w = rgb.shape[1]
    mpp = (1.0 / density) if density > 0 else (1.0 / max(w, 1))
    return R.derive(rgb, kind, light_amount, DERIVED_DEPTH, mpp)


def _new_image(name, arr, colour, alpha=None):
    """a Blender image from an (h, w[, c]) array (rows bottom first)"""
    h, w = arr.shape[0], arr.shape[1]
    old = bpy.data.images.get(name)
    if old is not None:
        bpy.data.images.remove(old)
    ima = bpy.data.images.new(name, w, h, alpha=False)
    if arr.ndim == 2:
        arr = np.repeat(arr[..., None], 3, axis=2)
    rgb = R.linear_to_srgb(arr) if colour else np.clip(arr, 0.0, 1.0)
    px = np.ones((h, w, 4), dtype=np.float32)
    px[..., :3] = rgb
    ima.pixels = px.reshape(-1).tolist()
    if not colour:
        try:
            ima.colorspace_settings.name = 'Non-Color'
        except TypeError:
            pass
    return ima


def remaster_folder():
    if not bpy.data.filepath:
        raise RuntimeError("save the file first: the derived images go into a \"%s\" folder next to it" %
                           REMASTER_FOLDER)
    return os.path.join(os.path.dirname(bpy.path.abspath(bpy.data.filepath)), REMASTER_FOLDER)


def _safe(name):
    return "".join(c if c.isalnum() or c in "-_" else "_" for c in name)


def save_derived(ima, channels):
    """the derived channels as PNG files in the remaster folder, loaded as
    images: {"albedo", "roughness", "height", "normal"} -> image"""
    folder = remaster_folder()
    os.makedirs(folder, exist_ok=True)
    albedo, rough, height, normal = channels
    out = {}
    for tag, arr, colour in (("albedo", albedo, True), ("roughness", rough, False), ("height", height, False),
                             ("normal", normal, False)):
        name = "%s (remaster %s)" % (ima.name, tag)
        tmp = _new_image(name, arr, colour)
        path = os.path.join(folder, "%s_%s.png" % (_safe(ima.name), tag))
        tmp.filepath_raw = path
        tmp.file_format = 'PNG'
        tmp.save()
        bpy.data.images.remove(tmp)
        img = bpy.data.images.load(path, check_existing=False)
        img.name = name
        if not colour:
            try:
                img.colorspace_settings.name = 'Non-Color'
            except TypeError:
                pass
        img["bge_remix_remaster_derived"] = tag
        img["bge_remix_remaster_approximate"] = 1
        try:
            img.filepath = bpy.path.relpath(path)
        except ValueError:
            pass
        out[tag] = img
    return out


# ---------------------------------------------------------------------------
# P10.5: writing the RTX Remix materials

def keep_original(ma):
    """the original kept as <name>.p8 until the second save (P9.3's own
    code); a .p8 copy already there (P9.3's, not saved twice yet) is the
    older original and stays"""
    from bl_operators import bge_remix_convert as C
    if bpy.data.materials.get(ma.name + C.P8_SUFFIX) is not None:
        return bpy.data.materials[ma.name + C.P8_SUFFIX]
    return C.keep_original(ma)


def write_fit(ma, fit, bake_size=DEFAULT_BAKE_SIZE):
    """P10.5: ma becomes the fitted preset (its group node, the fit's
    settings and colour), baked for Remix at bake_size"""
    P = presets_module()
    keep_original(ma)
    g = P.apply_preset(ma, fit["preset"])
    set_inputs(g, fit["settings"], fit["colour"])
    ma["bge_remix_tile_metres"] = float(fit.get("tile_metres") or 1.0)
    node = material_module().remix_node(ma)
    if node is not None:
        node.bake_size = float(bake_size)
    ma["bge_remix_bake_size"] = int(bake_size)
    ma[APPLIED_KEY] = "fit"
    return g


def _texture_node(nt, ima, location, geometry):
    tex = bpy.data.textures.new(ima.name, 'IMAGE')
    tex.image = ima
    n = nt.nodes.new("ShaderNodeTexture")
    n.texture = tex
    n.location = location
    nt.links.new(geometry.outputs["UV"], n.inputs["Vector"])
    return n


def write_derived(ma, images):
    """P10.5: ma becomes an RTX Remix material whose albedo, roughness,
    normal and height are the derived images"""
    M = material_module()
    keep_original(ma)
    node = M.remix_node(ma) if M.is_remix(ma) else None
    if node is None:
        node = M.make_remix_material(ma)
    nt = ma.node_tree
    for link in list(nt.links):
        if link.to_node == node and link.to_socket.name in ("Albedo", "Roughness", "Normal", "Height"):
            nt.links.remove(link)
    geometry = next((n for n in nt.nodes if n.bl_idname == "ShaderNodeGeometry"), None) or \
        nt.nodes.new("ShaderNodeGeometry")
    y = 600
    for tag, sock in (("albedo", "Albedo"), ("roughness", "Roughness"), ("normal", "Normal"), ("height", "Height")):
        t = _texture_node(nt, images[tag], (node.location[0] - 400, y), geometry)
        nt.links.new(t.outputs["Color"], node.inputs[sock])
        y -= 260
    node.inputs["Displace In"].default_value = DERIVED_DEPTH
    ma[APPLIED_KEY] = "derived"
    return node


def atlas_module():
    """P10.4's module (the atlas track), when it is there"""
    try:
        from . import atlas
    except ImportError:
        return None
    return atlas


def _animated(ma):
    P = presets_module()
    g = P.preset_node(ma)
    if g is None:
        return bool(ma.get("bge_remix_flipbook"))
    sp = g.inputs.get("Speed")
    return bool(ma.get("bge_remix_flipbook")) or (sp is not None and sp.default_value > 0.0)


def apply_one(name, decision, bake_size=DEFAULT_BAKE_SIZE, light_amount=1.0):
    """P10.5: one accepted (or adjusted) image's plan written into its
    materials; returns [material names] changed"""
    ima = bpy.data.images.get(name)
    if ima is None:
        return []
    row = audit_row(name)
    e = gather().get(name)
    mats = sorted(set(u["material"] for u in (e["uses"] if e else []) if u["channel"] == "colour"))
    plan = decision.get("plan") or (row or {}).get("plan")
    changed = []
    if plan == 'FIT':
        fit = decision.get("fit") or last_fit(name)
        if fit is None:
            raise RuntimeError("%s: not fitted yet" % name)
        atlas = atlas_module()
        for mn in mats:
            ma = bpy.data.materials[mn]
            write_fit(ma, fit, bake_size)
            if atlas is not None:
                if _animated(ma) and hasattr(atlas, "bake_flipbook"):
                    atlas.bake_flipbook(ma, frames=16)
                elif hasattr(atlas, "bake_tile_set"):
                    atlas.bake_tile_set(ma, bake_size, colours=2)
            changed.append(mn)
    elif plan == 'DERIVE':
        images = save_derived(ima, derived_channels(ima, row, light_amount))
        for mn in mats:
            write_derived(bpy.data.materials[mn], images)
            changed.append(mn)
    return changed


def precache_objects():
    """the game scenes' mesh objects, each once (their materials are what
    the game sends to Remix)"""
    out, seen = [], set()
    for sc in game_scenes():
        for ob in sc.objects:
            if ob.type == 'MESH' and ob.name not in seen and not ob.get(PREVIEW_KEY):
                seen.add(ob.name)
                out.append((sc, ob))
    return out


def precache_start():
    """P10.5: every game object's materials baked and their encodes started
    (bge_remix's bridge); returns the textures asked for"""
    if _bge_remix is None or not hasattr(_bge_remix, "precache"):
        raise RuntimeError("this Blender was built without RTX Remix support")
    _bge_remix.precache_reset()
    n = 0
    for sc, ob in precache_objects():
        n += _bge_remix.precache(ob.as_pointer(), sc.as_pointer())
    return n


def precache_poll():
    return _bge_remix.precache_poll() if _bge_remix is not None else {"waiting": 0}


# ---------------------------------------------------------------------------
# P10.5: the preview square (the Remix View's Debug picture)

def preview_clear():
    """the preview's objects, meshes, materials and images removed"""
    for ob in [o for o in bpy.data.objects if o.get(PREVIEW_KEY)]:
        me = ob.data
        for sc in bpy.data.scenes:
            if ob.name in sc.objects:
                sc.objects.unlink(ob)
        bpy.data.objects.remove(ob)
        if me is not None and me.users == 0:
            bpy.data.meshes.remove(me)
    for ma in [m for m in bpy.data.materials if m.get(PREVIEW_KEY) and m.users == 0]:
        bpy.data.materials.remove(ma)
    for ima in [i for i in bpy.data.images if i.get(PREVIEW_KEY) and i.users == 0]:
        bpy.data.images.remove(ima)


def _preview_square(scene, name, ma, centre, rot, size):
    s = size * 0.5
    me = bpy.data.meshes.new(name)
    me.from_pydata([(-s, -s, 0), (s, -s, 0), (s, s, 0), (-s, s, 0)], [], [(0, 1, 2, 3)])
    me.uv_textures.new("UVMap")
    for loop, uv in zip(me.uv_layers[0].data, ((0, 0), (1, 0), (1, 1), (0, 1))):
        loop.uv = uv
    me.update()
    me.materials.append(ma)
    ob = bpy.data.objects.new(name, me)
    ob[PREVIEW_KEY] = 1
    ob.location = centre
    ob.rotation_mode = 'QUATERNION'
    ob.rotation_quaternion = rot
    ob.game.physics_type = 'NO_COLLISION'
    scene.objects.link(ob)
    return ob


def preview_material(name, light_amount=1.0):
    """the image's plan as a material (a copy: the file's material stays)"""
    e = gather().get(name)
    ma_name = next((u["material"] for u in (e["uses"] if e else []) if u["channel"] == "colour"), None)
    if ma_name is None:
        raise RuntimeError("%s is not used as a colour" % name)
    src = bpy.data.materials[ma_name]
    row = audit_row(name) or {}
    fit = last_fit(name)
    copy = src.copy()
    copy.name = "%s (remaster preview)" % src.name
    copy[PREVIEW_KEY] = 1
    plan = (fit or {}).get("plan") or row.get("plan")
    if plan == 'FIT' and fit is not None:
        P = presets_module()
        g = P.apply_preset(copy, fit["preset"])
        set_inputs(g, fit["settings"], fit["colour"])
        copy["bge_remix_tile_metres"] = float(fit.get("tile_metres") or 1.0)
    else:
        albedo, rough, height, normal = derived_channels(bpy.data.images[name], row, light_amount)
        imgs = {}
        for tag, arr, colour in (("albedo", albedo, True), ("roughness", rough, False), ("height", height, False),
                                 ("normal", normal, False)):
            imgs[tag] = _new_image("%s (remaster preview %s)" % (name, tag), arr, colour)
            imgs[tag][PREVIEW_KEY] = 1
        M = material_module()
        node = M.remix_node(copy) if M.is_remix(copy) else M.make_remix_material(copy)
        nt = copy.node_tree
        geometry = nt.nodes.new("ShaderNodeGeometry")
        for i, (tag, sock) in enumerate((("albedo", "Albedo"), ("roughness", "Roughness"), ("normal", "Normal"),
                                         ("height", "Height"))):
            t = _texture_node(nt, imgs[tag], (-400, 600 - 260 * i), geometry)
            t.texture[PREVIEW_KEY] = 1
            nt.links.new(t.outputs["Color"], node.inputs[sock])
    return src, copy


def preview_show(context, name, light_amount=1.0):
    """P10.5: two squares in front of the Remix View, the original on the
    left, the plan's result on the right, drawn by Remix as the game would
    be; they go when the preview is closed or the file saved"""
    preview_clear()
    scene = context.scene
    rv3d = None
    for area in context.window.screen.areas if context.window else []:
        if area.type in ('GAME_VIEW', 'VIEW_3D'):
            rv3d = area.spaces.active.region_3d
            if area.type == 'GAME_VIEW':
                break
    src, copy = preview_material(name, light_amount)
    if rv3d is not None:
        rot = rv3d.view_rotation.copy()
        centre = rv3d.view_location.copy()
        size = max(rv3d.view_distance * 0.3, 0.1)
        from mathutils import Vector
        right = rot * Vector((1.0, 0.0, 0.0))
        toward = rot * Vector((0.0, 0.0, 1.0))
    else:
        from mathutils import Quaternion, Vector
        rot = Quaternion((1.0, 0.0, 0.0, 0.0))
        centre = Vector((0.0, 0.0, 0.0))
        size = 1.0
        right = Vector((1.0, 0.0, 0.0))
        toward = Vector((0.0, 0.0, 1.0))
    front = centre + toward * (size * 0.5)
    a = _preview_square(scene, "BGE-Remix remaster preview: original", src, front - right * (size * 0.55), rot, size)
    b = _preview_square(scene, "BGE-Remix remaster preview: result", copy, front + right * (size * 0.55), rot, size)
    scene.update()
    return a, b


def preview_active():
    return any(o.get(PREVIEW_KEY) for o in bpy.data.objects)


def file_fingerprint():
    """the file's materials, images, objects, meshes, textures and node
    groups (their names and main settings) without the remaster's own
    report: for the tests' "the audit changes nothing" """
    fp = {}
    for ma in bpy.data.materials:
        fp["ma:" + ma.name] = [list(ma.diffuse_color), ma.diffuse_intensity, ma.use_nodes, len(ma.keys()),
                               sorted(ma.keys())]
    for ima in bpy.data.images:
        fp["im:" + ima.name] = [list(ima.size), ima.filepath, ima.is_dirty, sorted(ima.keys())]
    for ob in bpy.data.objects:
        fp["ob:" + ob.name] = [list(ob.location), [s.material.name if s.material else None
                                                   for s in ob.material_slots]]
    for k in ("meshes", "textures", "node_groups", "scenes"):
        for x in getattr(bpy.data, k):
            fp[k + ":" + x.name] = x.users
    for t in bpy.data.texts:
        if t.name != AUDIT_TEXT:
            fp["tx:" + t.name] = t.as_string()
    return fp
