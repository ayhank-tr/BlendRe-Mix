"""BGE-Remix phase 3 tests: materials and the texture cache.

CI (fake runtime, no GPU):
  python run_p3.py --ours <build> --mock <folder with the fake d3d9.dll> --official <2.79b> --out <dir>

  T3.1a  every grid ball's material reaches Remix with the right type, values
         and texture slots
  T3.2a  the cache holds the room's images as DDS files (BC7 colour, BC5
         normals, BC4 roughness, full mips); the bridge's encoder checked
         directly: octahedral normals, row order, BC6H
  T3.2b  a second run encodes nothing; with one image changed, only it
  T3.3   both test files open in official 2.79b, play in OpenGL, and keep the
         RTX Remix panel's values when saved there
  T3.4   the ball with the RTX Remix panel arrives with the panel's values
  T3.5   the unsupported node graph gives a flat colour and one log line

RTX (your PC):
  python run_p3.py --ours <build> --out <dir> --rtx

  T3.1 grid next to its Cycles render (sign-off sheet), T3.2 the textured
  room, T3.6 textures arriving late, C1 capture, B3.1-B3.4 benchmarks.
"""

import argparse
import ctypes
import glob
import json
import math
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
sys.path.insert(0, HERE)
import run_p2 as p2  # noqa: E402  (running Blender, the fake runtime's log)
import dds_check  # noqa: E402

FMT = {"col": 98, "nrm": 83, "r": 80, "ri": 80}


# ---------------------------------------------------------------------------
# Scenes and runs

def make_scenes(ours, out):
    scenes = os.path.join(out, "scenes")
    if os.path.isdir(scenes):
        shutil.rmtree(scenes, ignore_errors=True)
    os.makedirs(scenes, exist_ok=True)
    code, text, _ = p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup",
                            "--python", os.path.join(HERE, "make_scenes_p3.py"), "--", scenes],
                           p2.base_env(out, "make"), os.path.join(out, "make_scenes_p3.log"))
    if code != 0 or "Traceback" in text:
        print("\n".join(text.splitlines()[-40:]))
        raise SystemExit("making the P3 test scenes failed, see make_scenes_p3.log")
    return scenes, p2.read_json(os.path.join(scenes, "p3_expected.json"))


def fresh(path):
    shutil.rmtree(path, ignore_errors=True)
    os.makedirs(path, exist_ok=True)
    return path


def cache_files(cache):
    return sorted(os.path.basename(p) for p in glob.glob(os.path.join(cache, "*.dds")))


def material_lines(lines):
    return p2.created(lines, "CreateMaterial")


def find_material(mats, name):
    return mats.get(p2.fnv1a("material:MA" + name)) or mats.get(p2.fnv1a("material:" + name))


def tex_name(field):
    """'C:\\...\\Checker_col_1a2b3c4d.dds' -> ('Checker', 'col')"""
    if not field or field == "-":
        return None
    base = os.path.basename(str(field).replace("\\", "/"))
    parts = base[:-4].rsplit("_", 2) if base.endswith(".dds") else []
    return (parts[0], parts[1]) if len(parts) == 3 else None


def notes_for(text, name):
    tag = 'BGE-Remix: material "MA%s":' % name
    return [l for l in text.splitlines() if l.startswith(tag)]


# ---------------------------------------------------------------------------
# CI checks

def check_material(f, e):
    """Compare one CreateMaterial line with its expectation; returns failures."""
    bad = []
    if f is None:
        return ["arrives"]
    if "type" in e and f.get("type") != e["type"]:
        bad.append("type %s, expected %s" % (f.get("type"), e["type"]))
    pairs = [("albedo", "albedo"), ("metallic", "metallic"), ("roughness", "roughness"), ("opacity", "opacity"),
             ("ior", "ior"), ("transmittance", "transmittance"), ("emissive", "emissive"),
             ("intensity", "intensity"), ("sss_radius", "sss_radius"), ("distance", "distance")]
    colours = ("albedo", "transmittance", "emissive")  # the bridge sends these sRGB-encoded
    for key, field in pairs:
        want = p2.srgb(e[key]) if key in colours and key in e else e.get(key)
        if key in e and not (field in f and p2.close(f[field], want, 2e-3)):
            bad.append("%s %s, expected %s" % (key, f.get(field), want))
    if "blend" in e and f.get("blend") != e["blend"]:
        bad.append("blend %s, expected %s" % (f.get("blend"), e["blend"]))
    if "subsurface" in e and f.get("subsurface") != e["subsurface"]:
        bad.append("subsurface %s" % f.get("subsurface"))
    want = e.get("textures", {})
    for slot in ("albedo", "normal", "roughness", "metallic", "emissive"):
        got = tex_name(f.get(slot + "_tex"))
        exp = tuple(want[slot]) if slot in want else None
        if got != exp:
            bad.append("%s texture %s, expected %s" % (slot, got, exp))
    return bad


def bridge_checks(ours, out):
    """The bridge's encoder called directly (ctypes): formats, mips, normal
    encoding, row order, BC6H."""
    checks = {}
    detail = {}
    try:
        dll = ctypes.CDLL(os.path.join(ours, "bge_remix_bridge.dll"))
    except OSError as ex:
        return {"bridge loads": False}, {"error": str(ex)}

    class Desc(ctypes.Structure):
        _fields_ = [("path", ctypes.c_wchar_p), ("kind", ctypes.c_int), ("width", ctypes.c_int),
                    ("height", ctypes.c_int), ("rgba8", ctypes.c_void_p), ("rgbaf", ctypes.c_void_p),
                    ("channel", ctypes.c_int), ("flags", ctypes.c_int)]

    class Result(ctypes.Structure):
        _fields_ = [("state", ctypes.c_int), ("gpu", ctypes.c_int), ("dxgi_format", ctypes.c_int),
                    ("mips", ctypes.c_int), ("seconds", ctypes.c_double), ("error", ctypes.c_char * 128)]

    dll.brb_texture_write.argtypes = [ctypes.POINTER(Desc), ctypes.POINTER(Result)]
    folder = fresh(os.path.join(out, "bridge_textures"))
    W, H = 96, 64  # not powers of two

    def write(name, kind, data8=None, dataf=None, channel=0, flags=0):
        d = Desc()
        d.path = os.path.join(folder, name)
        d.kind, d.width, d.height, d.channel, d.flags = kind, W, H, channel, flags | 1  # CPU: as CI
        if data8 is not None:
            buf = (ctypes.c_ubyte * len(data8))(*data8)
            d.rgba8 = ctypes.cast(buf, ctypes.c_void_p)
        else:
            buf = (ctypes.c_float * len(dataf))(*dataf)
            d.rgbaf = ctypes.cast(buf, ctypes.c_void_p)
        r = Result()
        rc = dll.brb_texture_write(ctypes.byref(d), ctypes.byref(r))
        detail[name] = {"rc": rc, "format": r.dxgi_format, "mips": r.mips, "seconds": r.seconds,
                        "error": r.error.decode("utf-8", "replace")}
        return rc == 0, d.path

    # a vertical gradient: the top row of the DDS must be Blender's top row
    grad = []
    for y in range(H):
        for x in range(W):
            grad += [x * 255 // W, y * 255 // (H - 1), 128, 255]
    ok, path = write("grad_g.dds", 2, data8=grad, channel=1)
    top = dds_check.first_block(path) if ok else None
    checks["scalar map: BC4, full mips, top row first"] = ok and dds_check.read_header(path)["dxgi"] == 80 and \
        dds_check.read_header(path)["mips"] == dds_check.full_mips(W, H) and top is not None and min(top[:4]) > 0.9
    # a constant tilted normal (OpenGL: +x right, +y up the image)
    nx, ny = 0.5, 0.2
    nz = math.sqrt(1 - nx * nx - ny * ny)
    px = [int(round((c * 0.5 + 0.5) * 255)) for c in (nx, ny, nz)] + [255]
    ok, path = write("tilt_n.dds", 1, data8=px * (W * H))
    blk = dds_check.first_block(path) if ok else None
    n = dds_check.octahedral_to_normal(*blk[0]) if blk else (0, 0, 1)
    err = dds_check.angle_deg(n, (nx, -ny, nz))  # Remix's v runs down the image
    detail["tilt_n.dds"]["decoded"] = n
    detail["tilt_n.dds"]["error_deg"] = err
    checks["normal map: BC5, octahedral, within 2 degrees (green flipped for Remix)"] = \
        ok and dds_check.read_header(path)["dxgi"] == 83 and err < 2.0
    ok, path = write("grad_c.dds", 0, data8=grad)
    # BC7_UNORM holding sRGB values: Remix decodes colour maps from gamma
    # itself, an _SRGB format (99) would be decoded twice
    checks["colour map: BC7, sRGB values in a UNORM format"] = ok and dds_check.read_header(path)["dxgi"] == 98
    hdr = []
    for i in range(W * H):
        hdr += [4.0 * (i % W) / W, 2.0, 0.5, 1.0]
    ok, path = write("hdr.dds", 3, dataf=hdr)
    checks["HDR map: BC6H"] = ok and dds_check.read_header(path)["dxgi"] == 95
    return checks, detail


def run_ci(args, out):
    scenes, expected = make_scenes(args.ours, out)
    mock = os.path.abspath(args.mock)
    res = {}

    def mock_env(name, cache):
        log = os.path.join(out, name + "_mock.log")
        if os.path.isfile(log):
            os.remove(log)
        return log, {"BGE_REMIX_RUNTIME_DIR": mock, "BGE_REMIX_MOCK_MODE": "ok", "BGE_REMIX_MOCK_LOG": log,
                     "BGE_REMIX_CACHE_DIR": cache, "BGE_REMIX_TEXTURE_NO_GPU": "1",
                     # long enough for the encoder to finish and the materials to switch
                     "BGE_REMIX_P2_FRAMES": "300", "BGE_REMIX_P2_SECONDS": "15"}

    def ran(r):
        return r["exit"] == 0 and r["frames"] >= 300 and not r["traceback_in_output"]

    def player_text(name):
        with open(os.path.join(out, name + ".log"), encoding="utf-8", errors="replace") as f:
            return f.read()

    # T3.1a, T3.4, T3.5: the grid
    cache = fresh(os.path.join(out, "cache_grid"))
    log, env = mock_env("grid", cache)
    r = p2.player(args.ours, out, "grid", os.path.join(scenes, "p3_grid.blend"), env)
    lines = p2.read_lines(log)
    mats = material_lines(lines)
    text = player_text("grid")
    c, d = {}, {}
    for e in expected["grid"]:
        if e["source"] == "panel" or e.get("notes"):
            continue
        f = find_material(mats, e["name"])
        bad = check_material(f, e)
        d[e["name"]] = {"got": f, "problems": bad}
        c[e["name"]] = not bad
    others = [l for l in text.splitlines() if l.startswith("BGE-Remix: material ") and "MAUnsupported" not in l]
    c["no other ball needs a note"] = not others
    c["the player ran 300 frames"] = ran(r)
    c["every texture arrived (none still waiting)"] = r["stats"].get("textures_waiting") == 0 and \
        r["stats"].get("textures_failed") == 0
    res["T3.1a"] = dict(p2.test(c), detail=d, others=others, stats=r["stats"])

    gold = next(e for e in expected["grid"] if e["source"] == "panel")
    f = find_material(mats, gold["name"])
    bad = check_material(f, gold)
    res["T3.4"] = dict(p2.test({"PanelGold has the panel's gold metal, not its own blue": not bad}),
                       detail={"got": f, "problems": bad})
    uns = next(e for e in expected["grid"] if e.get("notes"))
    f = find_material(mats, uns["name"])
    bad = check_material(f, uns)
    notes = notes_for(text, uns["name"])
    res["T3.5"] = dict(p2.test({"flat colour from the node's own value": not bad,
                                "exactly one log line about it": len(notes) == 1,
                                "the player ran 300 frames": ran(r)}),
                       detail={"got": f, "problems": bad, "notes": notes})

    # T3.2a: the room fills a new cache
    cache = fresh(os.path.join(out, "cache_room"))
    log, env = mock_env("room", cache)
    r = p2.player(args.ours, out, "room", os.path.join(scenes, "p3_room.blend"), env)
    files = cache_files(cache)
    c, d = {}, {"files": files}
    for name, tag, fmt in expected["room_cache"]:
        hits = [fn for fn in files if fn.startswith(name + "_" + tag + "_")]
        hdr = dds_check.read_header(os.path.join(cache, hits[0])) if len(hits) == 1 else None
        d[name + "_" + tag] = hdr
        c["%s as %s: %s, full mips" % (name, tag, dds_check.DXGI.get(fmt))] = bool(hdr) and hdr["dxgi"] == fmt and \
            hdr["mips"] == dds_check.full_mips(hdr["width"], hdr["height"])
    c["nothing else in the cache"] = len(files) == len(expected["room_cache"])
    mats = material_lines(p2.read_lines(log))
    for e in expected["room"]:
        f = find_material(mats, e["name"])
        bad = check_material(f, e)
        d[e["name"]] = {"got": f, "problems": bad}
        c["%s: textures from the cache" % e["name"]] = not bad
    c["the player ran 300 frames"] = ran(r)
    c["every texture encoded once, none failed"] = r["stats"].get("textures_encoded") == len(expected["room_cache"]) \
        and r["stats"].get("textures_failed") == 0
    bc, bd = bridge_checks(args.ours, out)
    c.update(bc)
    d["bridge"] = bd
    res["T3.2a"] = dict(p2.test(c), detail=d, stats=r["stats"])

    # T3.2b: the same cache again, then with one image changed
    log, env = mock_env("room_again", cache)
    again = p2.player(args.ours, out, "room_again", os.path.join(scenes, "p3_room.blend"), env)
    log, env = mock_env("room_changed", cache)
    changed = p2.player(args.ours, out, "room_changed", os.path.join(scenes, "p3_room_changed.blend"), env)
    n = len(expected["room_cache"])
    new_files = [fn for fn in cache_files(cache) if fn not in files]
    res["T3.2b"] = dict(p2.test({
        "second run: nothing encoded, %d reused" % n: again["stats"].get("textures_encoded") == 0 and
        again["stats"].get("textures_reused") == n,
        "one image changed: only it encoded": changed["stats"].get("textures_encoded") == 1 and
        changed["stats"].get("textures_reused") == n - 1,
        "the new file is the floor's roughness map": len(new_files) == 1 and new_files[0].startswith("TilesRough_r_"),
        "both runs 300 frames": ran(again) and ran(changed),
    }), detail={"again": again["stats"], "changed": changed["stats"], "new_files": new_files})

    # T3.3: stock 2.79b
    res["T3.3"] = check_stock(args, out, scenes)

    keys = ("T3.1a", "T3.2a", "T3.2b", "T3.3", "T3.4", "T3.5")
    names = {"T3.1a": "Grid through the translator", "T3.2a": "Cache contents", "T3.2b": "Cache reuse",
             "T3.3": "Opens in stock 2.79b", "T3.4": "Panel wins", "T3.5": "Unsupported nodes"}
    res["all_ok"] = all(res[k]["ok"] for k in keys)
    lines = ["BGE-Remix phase 3 results (CI, fake runtime)", "=" * 44, ""]
    for k in keys:
        lines.append("%-6s %-34s %s" % (k, names[k], p2.mark(res[k]["ok"])))
        for name, ok in res[k]["checks"].items():
            if not ok:
                lines.append("         failed: " + name)
                prob = res[k].get("detail", {}).get(name)
                if isinstance(prob, dict) and prob.get("problems"):
                    lines.append("                 " + "; ".join(prob["problems"]))
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p3.json and the .log files)"]
    return res, lines


STOCK_SAVE = r'''
import bpy, json, sys
out = sys.argv[sys.argv.index("--") + 1]
ma = bpy.data.materials["PanelGold"]
keys = sorted(k for k in ma.keys() if k.startswith("bge_remix"))
bpy.ops.wm.save_as_mainfile(filepath=out, copy=True)
print("P3-STOCK " + json.dumps({"keys": keys}))
'''

STOCK_READ = r'''
import bpy, json
ma = bpy.data.materials["PanelGold"]
print("P3-OURS " + json.dumps({"override": int(ma.get("bge_remix_override", 0)),
                               "albedo": [round(float(x), 4) for x in ma.get("bge_remix_albedo", [])],
                               "metallic": float(ma.get("bge_remix_metallic", -1))}))
'''


def tagged_json(text, tag):
    for l in text.splitlines():
        if l.startswith(tag + " "):
            try:
                return json.loads(l[len(tag) + 1:])
            except ValueError:
                return None
    return None


def check_stock(args, out, scenes):
    if not args.official:
        return dict(p2.test({"official 2.79b given (--official)": False}))
    official = os.path.abspath(args.official)
    c, d = {}, {}
    for name in ("p3_grid", "p3_room"):
        env = {"BGE_REMIX_P2_FRAMES": "300", "BGE_REMIX_P2_SECONDS": "0"}
        r = p2.player(official, out, "stock_" + name, os.path.join(scenes, name + ".blend"), env)
        d[name] = {"exit": r["exit"], "frames": r["frames"]}
        c["%s plays 300 frames in official 2.79b" % name] = r["exit"] == 0 and r["frames"] >= 300 and \
            not r["traceback_in_output"]
    copy = os.path.join(out, "p3_grid_saved_by_stock.blend")
    script = os.path.join(out, "p3_stock_save.py")
    with open(script, "w") as f:
        f.write(STOCK_SAVE)
    code, text, _ = p2.run([p2.exe(official, "blender"), "--background", os.path.join(scenes, "p3_grid.blend"),
                            "--python", script, "--", copy], p2.base_env(out, "stock"),
                           os.path.join(out, "stock_save.log"))
    keys = (tagged_json(text, "P3-STOCK") or {}).get("keys", [])
    c["official 2.79b sees the panel's custom properties and saves the file"] = code == 0 and \
        "bge_remix_override" in keys and os.path.isfile(copy)
    script = os.path.join(out, "p3_ours_read.py")
    with open(script, "w") as f:
        f.write(STOCK_READ)
    code, text, _ = p2.run([p2.exe(args.ours, "blender"), "--background", copy, "--python", script],
                           p2.base_env(out, "ours_read"), os.path.join(out, "ours_read.log"))
    got = tagged_json(text, "P3-OURS") or {}
    c["reopened in ours: the panel's values are kept"] = got.get("override") == 1 and \
        p2.close(got.get("albedo", []), [1.0, 0.766, 0.336]) and got.get("metallic") == 1.0
    d["keys"] = keys
    d["ours"] = got
    return dict(p2.test(c), detail=d)


# ---------------------------------------------------------------------------
# RTX (your PC)

CYCLES_P3 = r'''
# The P3 grid in Cycles for the sign-off: lamps as BGE-Remix converts them
# (as for P2), node materials as they are, the others rebuilt as nodes.
import math, sys
import bpy
out = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 128
scene.render.resolution_x, scene.render.resolution_y = 1280, 720
scene.render.resolution_percentage = 100
try:
    scene.view_settings.view_transform = 'Filmic'
    scene.view_settings.look = 'Filmic - Base Contrast'
except Exception:
    pass
try:
    scene.render.layers[0].cycles.use_denoising = True
except Exception:
    pass
hemi = [0.0, 0.0, 0.0]
for ob in scene.objects:
    if ob.type != 'LAMP':
        continue
    la = ob.data
    E, D = la.energy, getattr(la, "distance", 10.0)
    if la.type == 'HEMI':
        hemi = [h + c * E for h, c in zip(hemi, la.color)]
        la.energy = 0.0
        continue
    la.use_nodes = True
    em = la.node_tree.nodes.get("Emission")
    if la.type in ('POINT', 'SPOT'):
        strength = 4.0 * math.pi * math.pi * E * D
    elif la.type == 'SUN':
        strength = math.pi * E
    else:
        strength = 4.0 * math.pi * E * D
    em.inputs["Strength"].default_value = strength
    em.inputs["Color"].default_value = list(la.color) + [1.0]
w = scene.world
w.use_nodes = True
bg = w.node_tree.nodes.get("Background")
hc = w.horizon_color
bg.inputs["Color"].default_value = [hc[0] + hemi[0], hc[1] + hemi[1], hc[2] + hemi[2], 1.0]


def has_cycles_output(ma):
    return ma.use_nodes and ma.node_tree and any(n.bl_idname == "ShaderNodeOutputMaterial" for n in ma.node_tree.nodes)


for ma in bpy.data.materials:
    if has_cycles_output(ma):
        continue
    panel = ma.get("bge_remix_override", 0)
    images = [s.texture.image for s in ma.texture_slots if s and s.texture and s.texture.type == 'IMAGE']
    color_img = next((s.texture.image for s in ma.texture_slots
                      if s and s.texture and s.texture.type == 'IMAGE' and s.use_map_color_diffuse), None)
    normal_img = next((s.texture.image for s in ma.texture_slots
                       if s and s.texture and s.texture.type == 'IMAGE' and s.use_map_normal), None)
    ma.use_nodes = True
    nt = ma.node_tree
    nt.nodes.clear()
    outn = nt.nodes.new("ShaderNodeOutputMaterial")
    p = nt.nodes.new("ShaderNodeBsdfPrincipled")
    if panel:
        p.inputs["Base Color"].default_value = list(ma["bge_remix_albedo"]) + [1.0]
        p.inputs["Metallic"].default_value = float(ma.get("bge_remix_metallic", 0.0))
        p.inputs["Roughness"].default_value = float(ma.get("bge_remix_roughness", 0.5))
    else:
        p.inputs["Base Color"].default_value = [c * ma.diffuse_intensity for c in ma.diffuse_color] + [1.0]
        p.inputs["Roughness"].default_value = (2.0 / (ma.specular_hardness + 2.0)) ** 0.25
        if color_img:
            t = nt.nodes.new("ShaderNodeTexImage")
            t.image = color_img
            nt.links.new(t.outputs["Color"], p.inputs["Base Color"])
        if normal_img:
            t = nt.nodes.new("ShaderNodeTexImage")
            t.image = normal_img
            t.color_space = 'NONE'
            nm = nt.nodes.new("ShaderNodeNormalMap")
            nt.links.new(t.outputs["Color"], nm.inputs["Color"])
            nt.links.new(nm.outputs["Normal"], p.inputs["Normal"])
    shader = p.outputs[0]
    if not panel and ma.game_settings.alpha_blend != 'OPAQUE' and ma.alpha < 1.0:
        tr = nt.nodes.new("ShaderNodeBsdfTransparent")
        mix = nt.nodes.new("ShaderNodeMixShader")
        mix.inputs[0].default_value = ma.alpha
        nt.links.new(tr.outputs[0], mix.inputs[1])
        nt.links.new(shader, mix.inputs[2])
        shader = mix.outputs[0]
    nt.links.new(shader, outn.inputs["Surface"])
scene.render.filepath = out
scene.render.image_settings.file_format = 'PNG'
bpy.ops.render.render(write_still=True)
'''


def cycles_grid(ours, out, scenes, scene="p3_grid"):
    script = os.path.join(out, "p3_cycles_setup.py")
    with open(script, "w", encoding="utf-8") as f:
        f.write(CYCLES_P3)
    target = os.path.join(out, "cycles_" + scene.replace("p3_", "") + ".png")
    code, text, secs = p2.run([p2.exe(ours, "blender"), "--background", os.path.join(scenes, scene + ".blend"),
                               "--python", script, "--", target], p2.base_env(out, "cycles"),
                              os.path.join(out, "cycles_" + scene + ".log"), timeout=2400)
    return {"ok": code == 0 and os.path.isfile(target), "image": target, "seconds": round(secs, 1)}


def encode_benchmark(ours, out):
    """B3.1: one 1K, 2K and 4K map as BC7 (GPU and CPU) and BC5 (normals)."""
    try:
        dll = ctypes.CDLL(os.path.join(ours, "bge_remix_bridge.dll"))
    except OSError as ex:
        return {"error": str(ex)}

    class Desc(ctypes.Structure):
        _fields_ = [("path", ctypes.c_wchar_p), ("kind", ctypes.c_int), ("width", ctypes.c_int),
                    ("height", ctypes.c_int), ("rgba8", ctypes.c_void_p), ("rgbaf", ctypes.c_void_p),
                    ("channel", ctypes.c_int), ("flags", ctypes.c_int)]

    class Result(ctypes.Structure):
        _fields_ = [("state", ctypes.c_int), ("gpu", ctypes.c_int), ("dxgi_format", ctypes.c_int),
                    ("mips", ctypes.c_int), ("seconds", ctypes.c_double), ("error", ctypes.c_char * 128)]

    dll.brb_texture_write.argtypes = [ctypes.POINTER(Desc), ctypes.POINTER(Result)]
    folder = fresh(os.path.join(out, "bench_textures"))
    results = {}
    for size in (1024, 2048, 4096):
        # a busy pattern (photo-like detail is the slow case for BC7)
        row = bytes((x * 7 + (x >> 3) * 13) & 255 for x in range(size * 4))
        data = b"".join(row[(y * 12) % 256:] + row[:(y * 12) % 256] for y in range(size))
        buf = ctypes.create_string_buffer(data, len(data))
        for label, kind, flags in (("BC7 GPU", 0, 0), ("BC7 CPU", 0, 1), ("BC5 normal", 1, 1)):
            d = Desc()
            d.path = os.path.join(folder, "b%d_%s.dds" % (size, label.replace(" ", "_")))
            d.kind, d.width, d.height, d.flags = kind, size, size, flags
            d.rgba8 = ctypes.cast(buf, ctypes.c_void_p)
            r = Result()
            rc = dll.brb_texture_write(ctypes.byref(d), ctypes.byref(r))
            results["%dx%d %s" % (size, size, label)] = {
                "ok": rc == 0, "seconds": round(r.seconds, 3), "gpu": r.gpu,
                "error": r.error.decode("utf-8", "replace")}
    return results


def vram_used():
    try:
        p = subprocess.run(["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=20)
        return int(p.stdout.decode().strip().splitlines()[0])
    except Exception:
        return None


def run_rtx(args, out):
    scenes, expected = make_scenes(args.ours, out)
    grid = os.path.join(scenes, "p3_grid.blend")
    room = os.path.join(scenes, "p3_room.blend")
    since = time.time()
    res = {}
    clean = {"BGE_REMIX_RUNTIME_DIR": None, "BGE_REMIX_MOCK_LOG": None}

    def timed(seconds, cache, extra=None):
        e = dict(clean)
        e.update({"BGE_REMIX_P2_SECONDS": str(seconds), "BGE_REMIX_P2_FRAMES": "60",
                  "BGE_REMIX_P2_SHOT_FRAME": "30", "BGE_REMIX_CACHE_DIR": cache})
        e.update(extra or {})
        return e

    # T3.1: the grid, with Remix's shaders ready before the first frame
    print("T3.1: the material grid through Remix...", flush=True)
    cache_grid = fresh(os.path.join(out, "cache_grid"))
    g = p2.player(args.ours, out, "T3.1_grid", grid, timed(25, cache_grid, {"RTX_ENABLE_ASYNC_COMPILATION": "0"}),
                  size=(1280, 720), cwd=out)
    ok_g, img_g = p2.has_picture(g["screenshot"])
    g1 = p2.player(args.ours, out, "T3.1_grid_exposure_1", os.path.join(scenes, "p3_grid_exposure_1.blend"),
                   timed(15, cache_grid, {"RTX_ENABLE_ASYNC_COMPILATION": "0"}), size=(1280, 720), cwd=out)
    print("T3.1: the grid's Cycles render (CPU, several minutes)...", flush=True)
    cyc = cycles_grid(args.ours, out, scenes)
    res["T3.1"] = {"ok": g["exit"] == 0 and g["stats"].get("running") is True and bool(ok_g) and
                   g["stats"].get("textures_waiting") == 0,
                   "run": g, "cycles": cyc, "note": "compare the grid with its Cycles render and sign off"}

    # T3.6 and B3.2 cold: an empty cache, Remix as a player would start it
    print("T3.6: the textured room with an empty cache...", flush=True)
    cache_room = fresh(os.path.join(out, "cache_room"))
    cold = p2.player(args.ours, out, "T3.6_cold", room, timed(25, cache_room), size=(1280, 720), cwd=out)
    s = cold["stats"]
    res["T3.6"] = {"ok": cold["exit"] == 0 and s.get("running") is True and
                   s.get("textures_encoded") == len(expected["room_cache"]) and s.get("materials_swapped", 0) > 0 and
                   (s.get("frame_ms_max") or 1e9) < 50.0,
                   "frame_ms_max": s.get("frame_ms_max"), "swap_ms_max": s.get("swap_ms_max"),
                   "materials_swapped": s.get("materials_swapped"), "encode_seconds": s.get("encode_seconds"),
                   "stats": s}

    # T3.2 and B3.2 warm: the same cache again
    print("T3.2: the textured room again (full cache)...", flush=True)
    warm = p2.player(args.ours, out, "T3.2_room", room, timed(25, cache_room), size=(1280, 720), cwd=out)
    ok_r, img_r = p2.has_picture(warm["screenshot"])
    res["T3.2"] = {"ok": warm["exit"] == 0 and warm["stats"].get("running") is True and bool(ok_r) and
                   warm["stats"].get("textures_reused") == len(expected["room_cache"]),
                   "run": warm, "note": "look at T3.2_room.png: textures, bumps, the glowing sign"}
    res["B3.2"] = {"cold_first_textured_seconds": s.get("first_textured_seconds"),
                   "cold_first_textured_frame": s.get("first_textured_frame"),
                   "warm_first_textured_frame": warm["stats"].get("first_textured_frame"),
                   "warm_first_textured_seconds": warm["stats"].get("first_textured_seconds")}
    res["B3.2"]["targets_met"] = (res["B3.2"]["warm_first_textured_frame"] or 99) <= 1 and \
        (res["B3.2"]["cold_first_textured_seconds"] or 1e9) < 10.0

    sheet = [("Cycles, material grid", cyc["image"]), ("Remix, material grid (exposure +2, default)", g["screenshot"]),
             ("Remix, material grid (exposure +1)", g1["screenshot"]),
             ("Remix, textured room (full cache)", warm["screenshot"])]
    res["T3.1"]["compare"] = p2.contact_sheet(sheet, os.path.join(out, "T3.1_compare.png"))

    # C1: capture with textured materials
    res["C1"] = run_c1(args, out, room, lambda secs, extra=None: timed(secs, cache_room, extra))

    # B3.1: encoding
    print("B3.1: texture encoding (1K, 2K, 4K)...", flush=True)
    res["B3.1"] = encode_benchmark(args.ours, out)

    # B3.3 and B3.4: frame rates at 1080p, VRAM
    print("B3.3: frame rates at 1080p (20 s each)...", flush=True)
    b33 = {}
    before = vram_used()
    vram = {}
    for name, blend in (("grid", grid), ("room", room)):
        env = dict(os.environ)
        for k, v in timed(20, cache_room if name == "room" else cache_grid).items():
            if v is None:
                env.pop(k, None)
            else:
                env[k] = v
        tag = "B3.3_" + name
        for key, sub in (("BLENDER_USER_CONFIG", "config"), ("BLENDER_USER_SCRIPTS", "scripts")):
            path = os.path.join(out, "user_" + tag, sub)
            os.makedirs(path, exist_ok=True)
            env[key] = path
        env.update({"BGE_REMIX_P2_OUT": os.path.join(out, tag + ".json"),
                    "BGE_REMIX_STATS": os.path.join(out, tag + "_stats.json")})
        with open(os.path.join(out, tag + ".log"), "wb") as logf:
            proc = subprocess.Popen([p2.exe(args.ours, "blenderplayer"), "-w", "1920", "1080", blend], env=env,
                                    cwd=out, stdout=logf, stderr=subprocess.STDOUT)
            time.sleep(14)
            vram[name] = vram_used()
            try:
                proc.wait(timeout=300)
            except subprocess.TimeoutExpired:
                proc.kill()
        probe = p2.read_json(os.path.join(out, tag + ".json"))
        b33[name] = {"frame_ms_median": probe.get("frame_ms_median"), "fps": p2.fps(probe.get("frame_ms_median")),
                     "frame_ms_p95": probe.get("frame_ms_p95")}
    res["B3.3"] = b33
    res["B3.3"]["target_60fps"] = all((b33[n]["fps"] or 0) >= 60 for n in ("grid", "room"))
    res["B3.4"] = {"vram_mb_before": before, "vram_mb_grid": vram.get("grid"), "vram_mb_room": vram.get("room")}

    res["logs"] = p2.collect_logs(out, since, [out, args.ours])
    res["warmup"] = p2.warmups(out)
    res["all_ok"] = all(res[k]["ok"] for k in ("T3.1", "T3.2", "T3.6")) and res["C1"].get("ok", False)

    lines = ["BGE-Remix phase 3 results (RTX)", "=" * 31, ""]
    lines.append("T3.1 Material grid in Remix          %s   (sign-off: T3.1_compare.png)" % p2.mark(res["T3.1"]["ok"]))
    lines.append("T3.2 Textured room                   %s   (look at T3.2_room.png)" % p2.mark(res["T3.2"]["ok"]))
    lines.append("T3.6 Textures arrive late            %s   longest frame %s ms, %s materials switched" % (
        p2.mark(res["T3.6"]["ok"]), p2._r(res["T3.6"]["frame_ms_max"]), res["T3.6"]["materials_swapped"]))
    c1 = res["C1"]
    lines.append("C1   Capture                         %s   %d stage file(s)" % (
        p2.mark(c1.get("ok")), len(c1.get("captures", []))))
    lines += ["", "Benchmarks", "-" * 10]
    for k, v in res["B3.1"].items():
        if isinstance(v, dict):
            lines.append("B3.1 %-22s %s s%s" % (k, v["seconds"], "" if v["ok"] else "  FAILED " + v["error"]))
    b32 = res["B3.2"]
    lines.append("B3.2 first textured frame: empty cache %s s (frame %s), full cache frame %s   targets %s" % (
        p2._r(b32["cold_first_textured_seconds"]), b32["cold_first_textured_frame"],
        b32["warm_first_textured_frame"], "met" if b32["targets_met"] else "NOT met"))
    for n in ("grid", "room"):
        lines.append("B3.3 %-5s 1080p copy mode: median %s ms = %s FPS, 95th pct %s ms" % (
            n, p2._r(b33[n]["frame_ms_median"]), b33[n]["fps"], p2._r(b33[n]["frame_ms_p95"])))
    lines.append("     target (60 FPS): %s" % ("met" if res["B3.3"]["target_60fps"] else "NOT met"))
    b34 = res["B3.4"]
    lines.append("B3.4 VRAM in use: before %s MB, grid %s MB, room %s MB" % (
        b34["vram_mb_before"], b34["vram_mb_grid"], b34["vram_mb_room"]))
    slow = [(n, w) for n, w in sorted(res["warmup"].items()) if w is None or w > 1.0]
    lines += ["", "Remix warm-up (start to first ray-traced frame)", "-" * 47]
    lines += ["%-24s %s" % (n, "never ray-traced" if w is None else "%.1f s" % w) for n, w in slow] or \
        ["every run under 1 s"]
    lines += ["", "ALL PASS" if res["all_ok"] else "SOME TESTS FAILED (see results_p3_rtx.json and the .log files)"]
    return res, lines


def run_c1(args, out, room, timed):
    print("C1: capture with textured materials...", flush=True)
    since = time.time()
    r = p2.player(args.ours, out, "C1_capture", room,
                  timed(30, {"DXVK_RTX_CAPTURE_ENABLE_ON_FRAME": "900", "BGE_REMIX_NO_REPLACEMENTS": "1"}),
                  size=(1280, 720), cwd=out)
    time.sleep(3)
    caps = p2.find_captures(since, [out, args.ours])
    return {"ok": r["exit"] == 0 and bool(caps), "exit": r["exit"], "captures": caps}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--mock")
    ap.add_argument("--official")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rtx", action="store_true")
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    if args.rtx:
        res, lines = run_rtx(args, out)
        name = "results_p3_rtx"
    else:
        if not args.mock:
            raise SystemExit("--mock is needed without --rtx")
        res, lines = run_ci(args, out)
        name = "results_p3"
    with open(os.path.join(out, name + ".json"), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, name.replace("results", "summary") + ".txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
