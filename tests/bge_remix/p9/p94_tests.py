"""BGE-Remix P9.4: texture settings to Remix, the tests (run_p9 calls them).

normal(): the texture wall (p94_scenes) in the fake runtime: each tile's
material as Remix gets it (its wrap modes, its filter, its images straight
or baked), the UVs' range of its mesh (scale, offset, repeats and flip
applied to them), and what Match says of the way each texture went.

rtx(): the gate, the fallback within r9.TOL_GATE of Remix for each tile whose
setting the game draws (ACES). The other tiles (repeats, mirror, flip,
Extend, Clip, Interpolation off: the game's GLSL does not draw them) are
reported against F12 (Blender Internal draws them all), not gated.
"""

import os

WAY_TEST = "T9.9 Texture settings to Remix (P9.4)"
GATE_TEST = "S9.15 The texture wall against Remix (P9.4, the gate)"
TOL_UV = 0.002      # the UVs' range, as the fake runtime logs it (4 decimals)
TOL_ROUGH = 0.002   # a tile's roughness (its specular hardness), to find its material

MATCH_SCRIPT = r'''
import bpy, json, sys
argv = sys.argv[sys.argv.index("--") + 1:]
target = argv[0]
r = {}
try:
    import bge_remix
except ImportError:
    bge_remix = None
for ma in bpy.data.materials:
    if not ma.name.startswith("P94_"):
        continue
    if bge_remix is None:
        r[ma.name] = {"error": "no bge_remix module"}
        continue
    try:
        r[ma.name] = bge_remix.material_match(ma)
    except Exception as ex:
        r[ma.name] = {"error": str(ex)}
with open(target, "w") as f:
    json.dump(r, f, indent=1)
print("P94-MATCH done")
'''

# P9.8: the fake runtime's run, long enough for its textures to be encoded
MOCK_SECONDS = 10


def fields(line):
    """'CreateMaterial hash=1f wrap=1 ...' -> {'hash': '1f', 'wrap': '1', ...} (strings)"""
    d = {}
    for part in line.split(" ")[1:]:
        if "=" in part:
            k, v = part.split("=", 1)
            d[k] = v
    return d


def floats(text):
    try:
        return [float(x) for x in text.split(",")]
    except (ValueError, AttributeError):
        return None


def runtime_view(lines):
    """the last CreateMaterial per hash; each CreateMesh's materials and UV range"""
    mats, meshes = {}, []
    for l in lines:
        if l.startswith("CreateMaterial "):
            f = fields(l)
            mats[f.get("hash")] = f
        elif l.startswith("CreateMesh "):
            f = fields(l)
            meshes.append({"materials": (f.get("materials") or "-").split(","), "uvbox": floats(f.get("uvbox"))})
    return mats, meshes


def find_tile(mats, meshes, t):
    """a tile's material (by its roughness) and its mesh's UV range"""
    found = None
    for h, m in mats.items():
        rough = floats(m.get("roughness"))
        if rough and abs(rough[0] - t["roughness"]) <= TOL_ROUGH and m.get("type") == "opaque":
            found = (h, m)
    if not found:
        return None, None
    uv = None
    for mesh in meshes:
        if found[0] in mesh["materials"]:
            uv = mesh["uvbox"]
    return found[1], uv


def match_note(match, field):
    for f in (match or {}).get("fields", []):
        if f.get("field") == field:
            return f.get("note", "")
    return ""


def normal(args, out, scenes, E, tests, sheets):
    import run_p9 as r9
    import run_p8 as p8
    import run_p2 as p2
    T = E.get("texture_wall")
    if not T:
        tests[WAY_TEST] = {"ok": False, "checks": {"the texture wall was made": False},
                           "detail": (E.get("failed") or {}).get("texture_wall")}
        return
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    c, d = {}, {}
    log, env = p8.mock_env(args, out, "p9_texture_wall_mock", os.path.join(out, "cache_tw"))
    r = r9.pipe_play(args.ours, out, "p9_texture_wall_mock", S("p9_texture_wall_remix"), env, seconds=MOCK_SECONDS)
    c["Remix drew the texture wall (the fake runtime)"] = bool(r["ran"]) and \
        (r.get("stats") or {}).get("running") is True
    c["every texture encoded before the game ended"] = r9.textures_in(r)
    mats, meshes = runtime_view(r9.mock_lines(log))
    d["the fake runtime's materials"] = len(mats)
    image = T["image"]
    got = {}
    for label in T["order"]:
        t = T["tiles"][label]
        m, uv = find_tile(mats, meshes, t)
        got[label] = {"material": m and {k: m.get(k) for k in ("hash", "wrap", "wrap_v", "filter", "albedo_tex",
                                                               "normal_tex", "roughness")},
                      "uvbox": uv}
        c["%s: Remix got its material" % label] = m is not None
        if m is None:
            continue
        albedo = m.get("albedo_tex", "-")
        if t["way"] == "direct":
            c["%s: its image straight (not baked)" % label] = image in albedo and "_kb" not in albedo
        else:
            ok = "_kb" in albedo
            if "normal" in t["baked_fields"]:
                ok = ok and "_kb" in m.get("normal_tex", "-")
            c["%s: baked (%s)" % (label, ", ".join(t["baked_fields"]))] = ok
        c["%s: Remix's wrap %s (u %s, v %s)" % (label, t["wrap_name"], m.get("wrap"), m.get("wrap_v"))] = \
            m.get("wrap") == str(t["wrap"][0]) and m.get("wrap_v") == str(t["wrap"][1])
        c["%s: %s (filter %s)" % (label, "no filtering" if t["nearest"] else "filtered", m.get("filter"))] = \
            m.get("filter") == str(t["filter"])
        c["%s: its UVs sent span %s (%s)" % (label, t["uvbox"], uv)] = uv is not None and len(uv) == 4 and \
            all(abs(a - b) <= TOL_UV for a, b in zip(uv, t["uvbox"]))
    d["per tile"] = got

    # Match: which way each texture went
    target = os.path.join(out, "p94_match.json")
    if os.path.isfile(target):
        os.remove(target)
    p8.blender_script(args.ours, out, "p94_match", MATCH_SCRIPT, [target], blend=S("p9_texture_wall"))
    M = p2.read_json(target) or {}
    notes = {}
    for label in T["order"]:
        t = T["tiles"][label]
        mt = M.get(t["material"]) or {}
        fw = match_note(mt, "filtering and wrapping")
        notes[label] = {"filtering and wrapping": fw}
        if t["way"] == "direct":
            ok = fw.startswith("Remix's wrap %s," % t["wrap_name"]) and \
                (("no filtering" in fw) == t["nearest"]) and (("in the mesh's UVs" in fw) == t["moved"])
            c["%s: Match says Remix's wrap and filter%s" % (label, ", the transform in the UVs" if t["moved"] else
                                                             "")] = ok
        else:
            for field in t["baked_fields"]:
                note = match_note(mt, field)
                notes[label][field] = note
                c["%s: Match says %s is baked (%s)" % (label, field, t["why"])] = \
                    "baked into an image" in note and t["why"] in note
    d["match"] = notes
    tests[WAY_TEST] = {"ok": all(c.values()), "checks": c, "detail": d}


def rtx(args, out, scenes, E, tests, sheets, report_lines):
    import run_p9 as r9
    import run_p8 as p8
    import run_p2 as p2
    T = E.get("texture_wall")
    if not T:
        tests[GATE_TEST] = {"ok": False, "checks": {"the texture wall was made": False},
                            "detail": (E.get("failed") or {}).get("texture_wall")}
        return
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    shot = {"BGE_REMIX_P2_SHOT_FRAME": "300"}
    gl = p8.play(args.ours, out, "tw_aces_opengl", S("p9_texture_wall"), shot, frames=p8.REMIX_FRAMES)
    rtx_run = p8.play(args.ours, out, "tw_aces_remix", S("p9_texture_wall_remix"), r9.TEXTURE_ENV,
                      frames=p8.REMIX_FRAMES, seconds=r9.TEXTURE_SECONDS, debug=True)
    order, rects = T["order"], T["rects"]
    rows = r9.sphere_compare({"spheres": {"order": order, "rects": rects}}, gl.get("screenshot"),
                             rtx_run.get("screenshot"))
    # the pattern too (a flip or a mirror keeps the mean): per pixel, softened
    pg, pr = p8.picture(gl.get("screenshot")), p8.picture(rtx_run.get("screenshot"))
    pattern = r9.rect_diffs(pg, pr, rects, order, blur=True)
    # F12 draws every setting (Blender Internal): the reference for the rest
    f12 = os.path.join(out, "p9_texture_wall_f12.png")
    if os.path.isfile(f12):
        os.remove(f12)
    p8.blender_script(args.ours, out, "p94_f12", p8.RENDER, [f12], blend=S("p9_texture_wall"), timeout=1800)
    pf = p8.picture(f12) if os.path.isfile(f12) else None
    vs_f12 = r9.rect_diffs(pr, pf, rects, order, blur=True)
    game_f12 = r9.rect_diffs(pg, pf, rects, order, blur=True)
    c = {"the texture wall ran in OpenGL and in Remix": bool(gl["ran"] and rtx_run["ran"]),
         "every texture in Remix before the picture": r9.textures_in(rtx_run)}
    for label in T["gate"]:
        row = rows.get(label)
        c["%s: the fallback within %.0f of Remix (%s)" % (label, r9.TOL_GATE, row and row["mean_diff"])] = \
            bool(row) and row["mean_diff"] <= r9.TOL_GATE
    detail = {label: {"mean": rows.get(label), "pattern_game_remix": pattern.get(label),
                      "pattern_remix_f12": vs_f12.get(label), "pattern_game_f12": game_f12.get(label),
                      "gated": label in T["gate"]} for label in order}
    tests[GATE_TEST] = {"ok": all(c.values()), "checks": c, "detail": detail}
    sheets["texture_wall"] = p2.contact_sheet([("fallback, ACES", gl.get("screenshot")),
                                               ("RTX Remix, ACES", rtx_run.get("screenshot")),
                                               ("F12 (Blender Internal)", f12 if pf is not None else None)],
                                              os.path.join(out, "sheet_p9_texture_wall.png"))
    report_lines += ["", "Texture wall, mean colour fallback -> Remix (gate %.0f on the tiles the game draws); "
                     "pattern differences (softened) game/Remix, Remix/F12, game/F12:" % r9.TOL_GATE]
    for label in order:
        row = rows.get(label)
        report_lines.append("  %-17s %-6s %s  pattern %s / %s / %s" % (
            label, "gate" if label in T["gate"] else "report",
            "-" if not row else "%s -> %s  diff %.1f" % (row["opengl"], row["remix"], row["mean_diff"]),
            pattern.get(label), vs_f12.get(label), game_f12.get(label)))
    report_lines.append("  (not gated: the game's GLSL draws no repeats, mirror, flip, Extend, Clip or "
                        "Interpolation off; Remix should be nearer F12 than the game is for those)")
