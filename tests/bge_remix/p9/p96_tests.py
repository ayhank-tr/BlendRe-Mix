"""BGE-Remix P9.6: the preset materials' tests (run from run_p9: normal(...)
in the normal run, rtx(...) in the RTX run; the scenes from p96_scenes).

normal (no RTX card needed):
  T9.6a  every preset builds (on a new material, and in the scene): the group
         node "Remix Preset: <name>", its inputs (Scale, Colour, Wear, Seed;
         Wetness for Tarmac and Dirt road), its Albedo, Roughness and Height
         wired into the RTX Remix node, the node's values as the presets'
         table says (Displace In, translucent water at IOR 1.33, skin and
         snow's subsurface), the table's values in their ranges, the
         material marked a tile; Match bakes its albedo, roughness, height
         and normal
  T9.6b  the fake runtime: every preset reaches Remix with baked albedo,
         roughness, normal and height images (the relief presets' height at
         their depth); a preset's tile baked once for its tile and sphere
         (the cache); the baked tiles repeat without a seam (each image's
         wrap-around step against its neighbouring steps); bakes counted and
         timed
rtx (the real runtime):
  S9.6a  each tile, the game (ACES) against Remix within r9.TOL_GATE (water
         out: Remix refracts it); the spheres reported
  S9.6b  the large squares in Remix: no visible seam where the tiles meet
         (the pixel step across each boundary against the steps beside it)
  bake times in the report
"""

import os
import shutil

# P9.8 (decided 5 Nov): known, fixed with P10's tile bakes; BGE-Remix P10.2:
# fixed (Brick's tile holds whole repeats, no cross-fade; its black brick
# at each tile corner, the Math node's Modulo of 0 in the game's GLSL, gone):
# Brick is gated again
SEAMS_KNOWN = {}

PRESET_CHECK = r'''
import bpy, json, os, sys, time
argv = sys.argv[sys.argv.index("--") + 1:]
target, blend = argv[0], argv[1]
r = {"fresh": {}, "scene": {}}
try:
    import bge_remix
except ImportError:
    bge_remix = None
from bl_operators.bge_remix_presets import PRESETS, ORDER, OUTPUTS, apply_preset, preset_node
from bl_operators.bge_remix_material import kind, remix_node

def vec(v):
    return list(v) if hasattr(v, "__len__") else v

def state(ma):
    node = remix_node(ma)
    g = preset_node(ma)
    s = {"kind": kind(ma), "group_node": g is not None, "tile": ma.get("bge_remix_tile"),
         "preset": ma.get("bge_remix_preset")}
    if g is not None:
        s["group"] = g.node_tree.name
        s["inputs"] = [i.name for i in g.inputs]
        s["input_values"] = {i.name: vec(i.default_value) for i in g.inputs}
        s["nodes"] = len(g.node_tree.nodes)
        s["textures"] = sorted(n.texture.type for n in g.node_tree.nodes
                               if n.bl_idname == "ShaderNodeTexture" and n.texture)
    if node is not None:
        s["values"] = {k: vec(node.inputs[k].default_value) for k in
                       ("Displace In", "IOR", "Transmittance", "Subsurface", "Subsurface Radius", "Metallic")}
        s["remix_type"] = node.remix_type
        s["wired"] = {}
        for link in ma.node_tree.links:
            if link.to_node == node:
                s["wired"][link.to_socket.name] = [link.from_node.bl_idname, link.from_socket.name]
    if bge_remix is not None:
        try:
            s["match"] = bge_remix.material_match(ma)
        except Exception as ex:
            s["match"] = {"error": str(ex)}
    return s

# on new materials, one at a time (an RTX Remix material first for half of
# them: both ways in)
for i, key in enumerate(ORDER):
    try:
        ma = bpy.data.materials.new("fresh_" + key)
        if i % 2:
            from bl_operators.bge_remix_material import make_remix_material
            make_remix_material(ma)
        t0 = time.time()
        apply_preset(ma, key)
        ms = (time.time() - t0) * 1000.0
        # twice: the old group node goes, the group is shared
        apply_preset(ma, key)
        s = state(ma)
        s["build_ms"] = round(ms, 1)
        s["group_nodes"] = sum(1 for n in ma.node_tree.nodes if n.bl_idname == "ShaderNodeGroup")
        r["fresh"][key] = s
    except Exception as ex:
        import traceback
        r["fresh"][key] = {"error": traceback.format_exc()}
r["outputs"] = [list(o) for o in OUTPUTS]
bpy.ops.wm.open_mainfile(filepath=blend)
for ma in bpy.data.materials:
    if ma.users and ma.name.startswith("P9PR_") and ma.get("bge_remix_preset"):
        r["scene"][ma.name] = state(ma)
with open(target, "w") as f:
    json.dump(r, f, indent=1)
print("P96-CHECK done")
'''

# Match's bake slots (BRMX_bake.h BakeSlot)
SLOT_ALBEDO, SLOT_ROUGHNESS, SLOT_NORMAL, SLOT_HEIGHT = 1 << 0, 1 << 1, 1 << 3, 1 << 5
RELIEF_DEPTH = 0.005   # presets this deep or more: "relief" (a height map that shows)
SEAM_RATIO, SEAM_SLACK = 2.0, 3.0   # a seam: the step across it over twice its neighbours' plus 3 levels


def _near(a, b, tol=1e-3):
    if isinstance(b, (list, tuple)):
        return isinstance(a, (list, tuple)) and len(a) >= len(b) and all(abs(x - y) <= tol for x, y in zip(a, b))
    return a is not None and abs(a - b) <= tol


def _built(s, key, T, outputs):
    """the checks of one material's state (from PRESET_CHECK)"""
    t = T[key]
    want_inputs = ["Scale", "Colour", "Wear"] + (["Wetness"] if t["wet"] else []) + ["Seed"]
    v = s.get("values") or {}
    c = {
        "the group node": s.get("group_node") is True and s.get("group") == "Remix Preset: " + t["label"],
        "its inputs": s.get("inputs") == want_inputs,
        "its colour": _near((s.get("input_values") or {}).get("Colour"), t["colour"]),
        "wired: " + ", ".join(o for o, _ in outputs): all(
            (s.get("wired") or {}).get(i) == ["ShaderNodeGroup", o] for o, i in outputs),
        "Displace In %.4f m" % t["displace"]: _near(v.get("Displace In"), t["displace"], 1e-5),
        "a tile": s.get("tile") == 1 and s.get("preset") == key,
        "type %s" % t["remix_type"]: s.get("remix_type") == t["remix_type"],
    }
    if t["ior"]:
        c["IOR %.2f" % t["ior"]] = _near(v.get("IOR"), t["ior"])
        c["its transmittance the colour"] = _near(v.get("Transmittance"), t["colour"])
    if t["subsurface"]:
        c["subsurface, radius %s m" % t["subsurface"]] = _near(v.get("Subsurface"), 1.0) and \
            _near(v.get("Subsurface Radius"), t["subsurface"], 1e-6)
    mt = s.get("match")
    if mt is not None and "error" not in mt:
        slots = int(mt.get("bake_slots", 0))
        c["Match: albedo, roughness, normal and height baked"] = \
            slots & (SLOT_ALBEDO | SLOT_ROUGHNESS | SLOT_NORMAL | SLOT_HEIGHT) == \
            (SLOT_ALBEDO | SLOT_ROUGHNESS | SLOT_NORMAL | SLOT_HEIGHT)
    return c


def _table_ranges(t):
    """the table's values in their ranges: the albedo (the colour times the
    pattern's range) inside 0.01..0.95, the roughness in 0..1, a depth under
    5 cm, a subsurface radius of 0.1 mm to 5 cm"""
    lo, hi = t["albedo"]
    a_lo = min(t["colour"]) * lo
    a_hi = max(t["colour"]) * hi
    ok = 0.0 < lo <= hi and a_lo >= 0.0 and a_hi <= 0.95 and 0.0 <= t["rough"][0] <= t["rough"][1] <= 1.0 and \
        0.0 < t["displace"] <= 0.05
    if t["subsurface"]:
        ok = ok and all(1e-4 <= r <= 0.05 for r in t["subsurface"])
    if t["ior"]:
        ok = ok and 1.0 < t["ior"] < 3.0
    return ok


def _ppm_seam(path):
    """a baked tile's wrap-around: the mean step from its last column to its
    first (and last row to first) against the mean step between neighbouring
    columns (rows); None without PIL"""
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None
    try:
        a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    except OSError:
        return None
    dx = float(abs(a[:, 1:] - a[:, :-1]).mean())
    dy = float(abs(a[1:] - a[:-1]).mean())
    wx = float(abs(a[:, 0] - a[:, -1]).mean())
    wy = float(abs(a[0] - a[-1]).mean())
    # BGE-Remix P10.2 (fix round 1): and the step half a tile in, the same
    # place in a regular pattern's repeat: Brick's tile starts on a joint,
    # so its wrap-around step is a joint's, as at its middle (the first PC
    # run: wrap 25.7 / 60.3 against steps 2.1 / 5.3, a periodic tile)
    hx = float(abs(a[:, a.shape[1] // 2] - a[:, a.shape[1] // 2 - 1]).mean())
    hy = float(abs(a[a.shape[0] // 2] - a[a.shape[0] // 2 - 1]).mean())
    return {"step_x": round(dx, 2), "wrap_x": round(wx, 2), "step_y": round(dy, 2), "wrap_y": round(wy, 2),
            "half_x": round(hx, 2), "half_y": round(hy, 2),
            "seamless": wx <= SEAM_RATIO * max(dx, hx) + SEAM_SLACK and wy <= SEAM_RATIO * max(dy, hy) + SEAM_SLACK}


def normal(args, out, scenes, E, tests, sheets):
    import run_p9 as r9
    import run_p8 as p8
    import run_p2 as p2
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    P = E.get("presets")
    if not P:
        for name in ("T9.11 Preset materials build (P9.6)", "T9.12 Preset materials reach Remix (P9.6, fake runtime)"):
            tests[name] = {"ok": False, "checks": {"the P9.6 scenes were made": False}, "detail": E.get("failed")}
        return
    T, order = P["table"], P["order"]

    # T9.6a: the presets build
    c, d = {}, {}
    target = os.path.join(out, "p96_check.json")
    if os.path.isfile(target):
        os.remove(target)
    p8.blender_script(args.ours, out, "p96_check", PRESET_CHECK, [target, S("p9_presets")])
    R = p2.read_json(target) or {}
    outputs = [tuple(o) for o in R.get("outputs") or [("Albedo", "Albedo"), ("Roughness", "Roughness"),
                                                      ("Height", "Height")]]
    c["the check ran"] = bool(R)
    d["build ms"] = {}
    for key in order:
        fresh = (R.get("fresh") or {}).get(key) or {}
        scene = (R.get("scene") or {}).get(P["materials"][key]) or {}
        d[key] = {"fresh": fresh, "scene": {k: scene.get(k) for k in ("group", "inputs", "values", "wired",
                                                                      "nodes", "textures")}}
        d["build ms"][key] = fresh.get("build_ms")
        if "error" in fresh:
            c["%s: builds on a new material" % key] = False
            continue
        for what, ok in _built(fresh, key, T, outputs).items():
            c["%s (new material): %s" % (key, what)] = ok
        c["%s (new material): picked twice, one group node" % key] = fresh.get("group_nodes") == 1
        for what, ok in _built(scene, key, T, outputs).items():
            c["%s (scene): %s" % (key, what)] = ok
        c["%s: the table's values in range" % key] = _table_ranges(T[key])
        c["%s: procedural textures inside" % key] = bool(fresh.get("textures")) or key == 'BRICK'
    tests["T9.11 Preset materials build (P9.6)"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # T9.6b: the fake runtime gets them baked
    c, d = {}, {}
    # a fresh cache, so every preset is baked (and its images written)
    dump, cache = os.path.join(out, "p96_bakes"), os.path.join(out, "cache_p96")
    for folder in (dump, cache):
        shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(dump)
    log, env = p8.mock_env(args, out, "p9_presets_mock", cache, dump=dump)
    run = r9.pipe_play(args.ours, out, "p9_presets_mock", S("p9_presets_remix"), env, seconds=20)
    st = run.get("stats") or {}
    c["Remix drew the presets (the fake runtime)"] = bool(run["ran"]) and st.get("running") is True
    c["every texture encoded before the game ended"] = r9.textures_in(run)
    mats = r9.mock_materials(r9.mock_lines(log))
    d["the fake runtime's materials"] = len(mats)
    textured = [m for m in mats if m.get("albedo_tex", "-") != "-"]
    for key in order:
        name = P["materials"][key]
        mine = [m for m in textured if name in m.get("albedo_tex", "")]
        m = mine[0] if mine else None
        d[key] = m and {x: m.get(x) for x in ("type", "albedo_tex", "roughness_tex", "normal_tex", "height_tex",
                                               "displace_in")}
        c["%s: Remix got its baked albedo" % key] = m is not None
        if m is None:
            continue
        if m.get("type") == "translucent":
            # (Remix's translucent materials take no roughness image)
            c["%s: and its normal map (translucent)" % key] = m.get("normal_tex", "-") != "-"
        else:
            c["%s: and roughness, normal map" % key] = m.get("roughness_tex", "-") != "-" and \
                m.get("normal_tex", "-") != "-"
        if T[key]["displace"] >= RELIEF_DEPTH:
            c["%s: a height map, %.3f m deep" % (key, T[key]["displace"])] = \
                m.get("height_tex", "-") != "-" and r9.near(m.get("displace_in"), T[key]["displace"], 1e-4)
    bakes, cached = st.get("procedural_bakes"), st.get("procedural_bakes_cached")
    d["bakes"] = {"baked": bakes, "from the cache": cached, "rebaked": st.get("procedural_rebakes"),
                  "ms": st.get("procedural_bake_ms"), "gpu ms": st.get("procedural_bake_gpu_ms")}
    c["each preset baked once for its tile and sphere (%s baked, %s from the cache)" % (bakes, cached)] = \
        bakes is not None and 0 < bakes <= len(order)
    seams = {}
    for f in sorted(os.listdir(dump)):
        if f.endswith(".ppm") and any(x in f for x in ("_albedo", "_roughness", "_height")):
            seams[f] = _ppm_seam(os.path.join(dump, f))
    d["baked tiles' wrap-around"] = seams
    have = [k for k in order if any(P["materials"][k] in f for f in seams)]
    c["the baked tiles were written (%d of %d presets)" % (len(have), len(order))] = len(have) == len(order)
    for f, s in seams.items():
        if s is not None:
            c["%s repeats without a seam (wrap %s / %s against steps %s / %s)" % (
                f, s["wrap_x"], s["wrap_y"], s["step_x"], s["step_y"])] = s["seamless"]
    tests["T9.12 Preset materials reach Remix (P9.6, fake runtime)"] = {"ok": all(c.values()), "checks": c, "detail": d}
    sheets["presets"] = p2.contact_sheet([("presets (game)", run.get("screenshot"))],
                                         os.path.join(out, "sheet_p96_presets.png"))


def _seam_steps(a, rect, cols, rows, p8):
    """per boundary: the mean pixel step across it, and the median of the
    steps 2 to 8 pixels either side; BGE-Remix P10.2 (fix round 1): and the
    step across the line half a tile in (the same place in a regular
    pattern's repeat: Brick's joints lie on the tile's edges, so a joint's
    step is no seam)"""
    import numpy as np
    sub = p8.crop(a, rect)
    left, top = rect[0], rect[1]
    gx = abs(sub[:, 1:] - sub[:, :-1]).mean(axis=(0, 2))   # gx[i]: step between columns i and i + 1
    gy = abs(sub[1:] - sub[:-1]).mean(axis=(1, 2))
    res = []
    for g, lines, origin in ((gx, cols, left), (gy, rows, top)):
        tile = abs(lines[1] - lines[0]) if len(lines) > 1 else 0
        for line in lines:
            i = line - origin - 1
            if i < 9 or i > len(g) - 10:
                continue
            across = float(g[i - 1:i + 2].max())
            beside = float(np.median(np.concatenate([g[i - 8:i - 1], g[i + 2:i + 9]])))
            half = 0.0
            if tile >= 8:
                j = i - tile // 2 if i - tile // 2 >= 1 else i + tile // 2
                if 1 <= j <= len(g) - 2:
                    half = float(g[j - 1:j + 2].max())
            res.append({"at": line, "across": round(across, 2), "beside": round(beside, 2), "half": round(half, 2),
                        "seam": across > SEAM_RATIO * max(beside, half) + SEAM_SLACK})
    return res


def rtx(args, out, scenes, E, tests, sheets, report_lines):
    import run_p9 as r9
    import run_p8 as p8
    import run_p2 as p2
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    P = E.get("presets")
    if not P:
        for name in ("S9.16 Preset tiles against Remix (P9.6, the gate)", "S9.17 Preset tiles' seams in Remix (P9.6)"):
            tests[name] = {"ok": False, "checks": {"made": False}, "detail": E.get("failed")}
        return
    order = P["order"]
    shot = {"BGE_REMIX_P2_SHOT_FRAME": "300"}
    gl = p8.play(args.ours, out, "p96_aces_opengl", S("p9_presets_aces"), shot, frames=p8.REMIX_FRAMES)
    rt = p8.play(args.ours, out, "p96_aces_remix", S("p9_presets_aces_remix"), r9.TEXTURE_ENV, frames=p8.REMIX_FRAMES,
                 seconds=r9.TEXTURE_SECONDS, debug=True)
    labels = ["tile_" + k for k in order] + ["sphere_" + k for k in order]
    rows = r9.sphere_compare({"spheres": {"order": labels, "rects": P["rects"]}}, gl.get("screenshot"),
                             rt.get("screenshot"))
    c = {"the presets ran in OpenGL and in Remix": bool(gl["ran"] and rt["ran"]),
         "every texture in Remix before the picture": r9.textures_in(rt)}
    for k in order:
        if k in P["gate_out"]:
            continue
        row = rows.get("tile_" + k)
        c["%s: the tile in the game within %.0f of Remix (%s)" % (k, r9.TOL_GATE, row and row["mean_diff"])] = \
            bool(row) and row["mean_diff"] <= r9.TOL_GATE
    tests["S9.16 Preset tiles against Remix (P9.6, the gate)"] = {"ok": all(c.values()), "checks": c, "detail": rows}
    sheets["presets_rtx"] = p2.contact_sheet([("presets, game (ACES)", gl.get("screenshot")),
                                              ("presets, RTX Remix (ACES)", rt.get("screenshot"))],
                                             os.path.join(out, "sheet_p96_presets_rtx.png"))
    report_lines += ["", "Preset materials, mean colour game -> Remix (gate %.0f on the tiles; out: %s):" % (
        r9.TOL_GATE, ", ".join(P["gate_out"]))]
    for label in labels:
        row = rows.get(label)
        report_lines.append("  %-20s %s" % (label, "-" if not row else "%s -> %s  diff %.1f" % (
            row["opengl"], row["remix"], row["mean_diff"])))

    # S9.6b: the seams, in Remix
    c, d = {}, {}
    sr = p8.play(args.ours, out, "p96_seams_remix", S("p9_presets_seams_aces_remix"), r9.TEXTURE_ENV,
                 frames=p8.REMIX_FRAMES, seconds=r9.TEXTURE_SECONDS, debug=True)
    sg = p8.play(args.ours, out, "p96_seams_opengl", S("p9_presets_seams_aces"), shot, frames=p8.REMIX_FRAMES)
    c["the seams' squares ran in Remix"] = bool(sr["ran"])
    c["every texture in Remix before the picture"] = r9.textures_in(sr)
    pic = p8.picture(sr.get("screenshot"))
    for key, s in P["seams"].items():
        if pic is None:
            c["%s: a picture" % key] = False
            continue
        steps = _seam_steps(pic, s["rect"], s["columns"], s["rows"], p8)
        d[key] = steps
        if key in SEAMS_KNOWN:
            d[key + " (known, not gated)"] = SEAMS_KNOWN[key]
            continue
        c["%s: no seam where the tiles meet (%s)" % (key, ", ".join(
            "%s/%s" % (x["across"], x["beside"]) for x in steps))] = bool(steps) and not any(x["seam"] for x in steps)
    tests["S9.17 Preset tiles' seams in Remix (P9.6)"] = {"ok": all(c.values()), "checks": c, "detail": d}
    sheets["presets_seams"] = p2.contact_sheet([("seams, game", sg.get("screenshot")),
                                                ("seams, RTX Remix", sr.get("screenshot"))],
                                               os.path.join(out, "sheet_p96_seams.png"))

    # the bakes' times (the RTX run: on the graphics card)
    report_lines += ["", "Preset bakes (Remix runs):"]
    for tag, r in (("presets", rt), ("seams", sr)):
        st = r.get("stats") or {}
        report_lines.append("  %-8s baked %s, from the cache %s, %s ms (%s ms on the graphics card)" % (
            tag, st.get("procedural_bakes"), st.get("procedural_bakes_cached"), st.get("procedural_bake_ms"),
            st.get("procedural_bake_gpu_ms")))
