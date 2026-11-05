"""BGE-Remix p10a4: the tests for the texture remaster's atlases (P10.4) (called from run_p10.py).

normal (the fake Remix runtime; scenes from p10a4_scenes):
  T10.4a  tile sets: the 16- and 81-tile Rock floors (and a 16-tile Brick
          patch) reach Remix as atlases: their layout (16 or 81 cells, gaps,
          mipmaps), the material's images the atlas's, the floor's mesh split
          (its UVs inside 0..1), every square's tile the one its corners say
          and neighbours agreeing; on the baked atlas itself: every two tiles
          that can meet meet without a seam, and a large tiling of it repeats
          far less at the tile's period than the plain tile
  T10.4b  the water flipbook: Remix's material a 4 x 4 sprite sheet at the
          speed's frame rate, its frames changing, the last running into the
          first without a jump (measured on the baked sheet)
  T10.4c  packing: the twenty images packed onto pages (each cell's gap
          right, none overlapping), the materials reading the pages (fewer
          textures), the squares' UVs inside their cells, the far camera
          nearer than where a page would bleed (and the warning further out)
  T10.4d  the UV editor: a tile picked by hand for a face (the operator),
          saved, reopened, and sent to Remix for that face's square
rtx (the real runtime):
  S10.4a  the floors in Remix: no seam where tiles meet; the repeat at the
          tile's period (autocorrelation) of the plain floor, 16 and 81 tiles
  S10.4b  the water in Remix: the picture changes over time
  S10.4c  packed against not packed, near and far: within the gate
  S10.4d  the picked tile shows in Remix (and only on its face)
"""

import math
import os
import shutil

SEAM_RATIO, SEAM_SLACK = 2.0, 3.0   # as P9.6: a step across over twice the steps beside, plus 3 levels
REPEAT_MAX = 0.8                    # the tiles' autocorrelation at the period (the plain tile's is about 1)
LOOP_RATIO, LOOP_SLACK = 1.5, 2.0   # the loop's last step against the frames' steps
TOL_PACK = 5.0                      # packed against not packed, mean colour per square (Remix)
TOL_SAME = 2.0                      # two Remix pictures of the same squares (another run)
PICK_SHOWS = 4.0                    # the picked face's square changes at least this much

# ---------------------------------------------------------------------------
# the tiles' arithmetic (as remix_atlas_core.cc and bge_remix.atlas)

_M32 = 0xFFFFFFFF


def corner_colour(i, j, seed, colours):
    h = (((i & _M32) * 0x8da6b343) + ((j & _M32) * 0xd8163841) + ((seed & _M32) * 0xcb1ab31f) + 0x9e3779b9) & _M32
    h ^= h >> 16
    h = (h * 0x7feb352d) & _M32
    h ^= h >> 15
    h = (h * 0x846ca68b) & _M32
    h ^= h >> 16
    return h % colours


def tile_corners(t, colours):
    out = []
    for _ in range(4):
        out.append(t % colours)
        t //= colours
    return out


def square_tile(i, j, seed, colours):
    c = [corner_colour(i, j, seed, colours), corner_colour(i + 1, j, seed, colours),
         corner_colour(i + 1, j + 1, seed, colours), corner_colour(i, j + 1, seed, colours)]
    return c[0] + colours * (c[1] + colours * (c[2] + colours * c[3]))


# ---------------------------------------------------------------------------
# helpers

def _ppm(path):
    """a dumped image (PPM, top row first) as an array, rows from the
    bottom (as the atlas's cells are placed); None without it"""
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None
    if not path or not os.path.isfile(path):
        return None
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    return a[::-1]


def _find(folder, suffix):
    if not os.path.isdir(folder):
        return None
    for f in sorted(os.listdir(folder)):
        if f.endswith(suffix):
            return os.path.join(folder, f)
    return None


def _mock_meshes(lines):
    out = []
    for l in lines:
        if not l.startswith("CreateMesh "):
            continue
        d = {}
        for part in l.split(" ")[1:]:
            if "=" in part:
                k, v = part.split("=", 1)
                d[k] = v
        out.append(d)
    return out


def _floats(text):
    try:
        return [float(x) for x in text.split(",")]
    except (AttributeError, ValueError):
        return None


def _material(mats, name):
    """the fake runtime's last material whose albedo file names it"""
    found = [m for m in mats if name in m.get("albedo_tex", "") or name in m.get("normal_tex", "")]
    return found[-1] if found else None


def _meshes_of(meshes, hashes):
    return [m for m in meshes if any(h in (m.get("materials") or "").split(",") for h in hashes)]


def _hashes(mats, name):
    return set(m.get("hash") for m in mats if name in m.get("albedo_tex", "") or name in m.get("normal_tex", ""))


def tile_seams(atlas, lay):
    """every pair of tiles that can meet (matching corners), side by side and
    one above the other: the mean step across where they meet against the
    tiles' own steps"""
    import numpy as np
    C = lay["colours"]
    cells = lay["cells"]
    worst, inside = 0.0, []
    for x, y, w, h in cells:
        c = atlas[y:y + h, x:x + w]
        inside.append(float(np.abs(c[:, 1:] - c[:, :-1]).mean()))
        inside.append(float(np.abs(c[1:] - c[:-1]).mean()))
    step = float(np.median(inside))
    bad = []
    for t, (x, y, w, h) in enumerate(cells):
        ct = tile_corners(t, C)
        for u, (ux, uy, uw, uh) in enumerate(cells):
            cu = tile_corners(u, C)
            if ct[1] == cu[0] and ct[2] == cu[3]:
                d = float(np.abs(atlas[y:y + h, x + w - 1] - atlas[uy:uy + uh, ux]).mean())
                worst = max(worst, d)
                if d > SEAM_RATIO * step + SEAM_SLACK:
                    bad.append([t, u, "x", round(d, 1)])
            if ct[3] == cu[0] and ct[2] == cu[1]:
                d = float(np.abs(atlas[y + h - 1, x:x + w] - atlas[uy, ux:ux + uw]).mean())
                worst = max(worst, d)
                if d > SEAM_RATIO * step + SEAM_SLACK:
                    bad.append([t, u, "y", round(d, 1)])
    return {"step": round(step, 2), "worst_across": round(worst, 2), "seams": bad[:20], "seam_count": len(bad)}


def _grey(a):
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722


def autocorrelation(g, lag):
    """the picture's (grey, its broad light taken out) normalised
    autocorrelation at lag pixels, across and up, averaged"""
    import numpy as np
    g = np.asarray(g, dtype=np.float64)
    # the broad light out: minus a 2 * lag box mean
    k = max(3, int(lag))
    pad = np.pad(g, k, mode='reflect')
    c = np.cumsum(np.cumsum(pad, 0), 1)
    c = np.pad(c, ((1, 0), (1, 0)), mode='constant')
    n = 2 * k + 1
    box = (c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n]) / float(n * n)
    d = g - box[:g.shape[0], :g.shape[1]]
    d = d - d.mean()
    var = float((d * d).mean())
    if var <= 1e-9 or lag >= min(d.shape):
        return None
    ax = float((d[:, :-lag] * d[:, lag:]).mean()) / var
    ay = float((d[:-lag] * d[lag:]).mean()) / var
    return round((ax + ay) / 2.0, 3)


def tiling(atlas, lay, cols, rows):
    """a large floor of the atlas's tiles (cols x rows squares), each the tile
    its corners say, content only"""
    import numpy as np
    c = lay["cells"][0][2]
    out = np.zeros((rows * c, cols * c, 3))
    for j in range(rows):
        for i in range(cols):
            x, y, w, h = lay["cells"][square_tile(i, j, lay["seed"], lay["colours"])]
            out[j * c:(j + 1) * c, i * c:(i + 1) * c] = atlas[y:y + c, x:x + c]
    return out


def plain_tiling(tile, cols, rows):
    import numpy as np
    return np.tile(tile, (rows, cols, 1))


def mock_run(args, out, tag, blend, p8, r9, seconds=20):
    dump, cache, adump = (os.path.join(out, tag + x) for x in ("_bakes", "_cache", "_atlas"))
    for folder in (dump, cache, adump):
        shutil.rmtree(folder, ignore_errors=True)
        os.makedirs(folder)
    log, env = p8.mock_env(args, out, tag, cache, dump=dump)
    env["BGE_REMIX_ATLAS_DUMP"] = adump
    run = r9.pipe_play(args.ours, out, tag, blend, env, seconds=seconds)
    lines = r9.mock_lines(log)
    return run, lines, dump, adump


def _layout_json(dump, name):
    import json
    p = _find(dump, name + ".json")
    if not p:
        return None
    with open(p) as f:
        d = json.load(f)
    return d


# ---------------------------------------------------------------------------
# normal

PICK_SCRIPT = r'''
import bpy, json, sys
argv = sys.argv[sys.argv.index("--") + 1:]
blend, saved, other, target = argv[0], argv[1], argv[2], argv[3]
face, tile, other_tile = int(argv[4]), int(argv[5]), int(argv[6])
from bge_remix import atlas
import bmesh
r = {}
for path, t in ((saved, tile), (other, other_tile)):
    bpy.ops.wm.open_mainfile(filepath=blend)
    ob = bpy.data.objects["P10A4_pickfloor"]
    sc = bpy.context.scene
    for o in sc.objects:
        o.select = False
    sc.objects.active = ob
    ob.select = True
    bpy.ops.object.mode_set(mode='EDIT')
    bm = bmesh.from_edit_mesh(ob.data)
    bm.faces.ensure_lookup_table()
    for f in bm.faces:
        f.select = f.index == face
    res = bpy.ops.image.bge_remix_pick_cell(cell=t)
    bpy.ops.object.mode_set(mode='OBJECT')
    r.setdefault("operator", []).append(list(res))
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, copy=False)
# reopened
bpy.ops.wm.open_mainfile(filepath=saved)
ob = bpy.data.objects["P10A4_pickfloor"]
r["cells"] = atlas.face_cells(ob.data)
r["layer"] = ob.data.polygon_layers_int.get(atlas.PICK_LAYER) is not None
r["check"] = {str(k): v for k, v in atlas.check_mesh(ob)["picked"].items()}
with open(target, "w") as f:
    json.dump(r, f)
print("P10A4-PICK done")
'''


def _t_tiles(args, out, S, P, tests, sheets, p8, p2, r9):
    import numpy as np
    F = P.get("floors")
    c, d = {}, {}
    if not F:
        tests["T10.4a Tile sets (P10.4, fake runtime)"] = {"ok": False, "checks": {"the floors were made": False},
                                                           "detail": None}
        return
    runs = {}
    for mode in ("plain", "tiles16", "tiles81"):
        runs[mode] = mock_run(args, out, "p10a4_" + mode + "_mock", S(F[mode]["file"] + "_remix"), p8, r9)
    material = "MA" + F["plain"]["material"]
    # the plain floor: its own tile, the mesh as it is
    run, lines, dump, adump = runs["plain"]
    st = run.get("stats") or {}
    c["plain floor: Remix drew it (the fake runtime)"] = bool(run["ran"]) and st.get("running") is True
    plain_tile = _ppm(_find(dump, material + "_albedo.ppm"))
    c["plain floor: its tile baked"] = plain_tile is not None
    mats = r9.mock_materials(lines)
    meshes = _meshes_of(_mock_meshes(lines), _hashes(mats, material))
    uv = _floats(meshes[-1].get("uvbox")) if meshes else None
    d["plain uvbox"] = uv
    c["plain floor: its UVs as they are (0..%g)" % F["size"][0]] = bool(uv) and uv[2] > 10.0
    repeats = {}
    for mode, colours, tiles in (("tiles16", 2, 16), ("tiles81", 3, 81)):
        run, lines, dump, adump = runs[mode]
        st = run.get("stats") or {}
        tag = "%d tiles" % tiles
        c["%s: Remix drew the floor (the fake runtime)" % tag] = bool(run["ran"]) and st.get("running") is True
        c["%s: every texture encoded before the end" % tag] = r9.textures_in(run)
        lay = _layout_json(dump, material + "_tiles_albedo")
        d[mode + " layout"] = lay and {k: lay.get(k) for k in ("width", "height", "pitch", "gutter", "colours",
                                                                "levels", "seed")}
        c["%s: the atlas's layout (%d cells, %d px, gaps)" % (tag, tiles, F["tile_size"])] = bool(lay) and \
            len(lay["cells"]) == tiles and lay["pitch"] == F["tile_size"] and lay["gutter"] >= 1 and \
            lay["colours"] == colours
        c["%s: its own mipmaps (to 1 x 1)" % tag] = bool(lay) and \
            lay.get("levels") == int(math.log(max(lay["width"], lay["height"]), 2)) + 1
        mats = r9.mock_materials(lines)
        m = _material(mats, material + "_tiles")
        d[mode + " material"] = m and {k: m.get(k) for k in ("albedo_tex", "normal_tex", "roughness_tex", "height_tex")}
        c["%s: Remix's material has the atlas's albedo, roughness, normal and height" % tag] = bool(m) and all(
            "_tiles_" in (m.get(k) or "") for k in ("albedo_tex", "normal_tex", "roughness_tex", "height_tex"))
        meshes = _meshes_of(_mock_meshes(lines), _hashes(mats, material + "_tiles"))
        uv = _floats(meshes[-1].get("uvbox")) if meshes else None
        d[mode + " mesh"] = meshes[-1] if meshes else None
        c["%s: the floor's mesh split, its UVs inside the atlas" % tag] = bool(uv) and \
            all(-1e-4 <= v <= 1.0 + 1e-4 for v in uv) and int(meshes[-1].get("verts", 0)) > 4
        sq = _layout_json(adump, "P10A4_floor__" + material)
        squares = {(s[0], s[1]): s[2] for s in (sq or {}).get("squares", [])}
        want = int(math.ceil(F["size"][0])) * int(math.ceil(F["size"][1]))
        c["%s: %d squares sent" % (tag, want)] = len(squares) == want
        wrong = [k for k, t in squares.items() if t != square_tile(k[0], k[1], sq["seed"], colours)]
        c["%s: each square's tile as its corners say" % tag] = bool(squares) and not wrong
        disagree = 0
        for (i, j), t in squares.items():
            a = tile_corners(t, colours)
            if (i + 1, j) in squares:
                b = tile_corners(squares[(i + 1, j)], colours)
                disagree += (a[1], a[2]) != (b[0], b[3])
            if (i, j + 1) in squares:
                b = tile_corners(squares[(i, j + 1)], colours)
                disagree += (a[3], a[2]) != (b[0], b[1])
        c["%s: neighbouring squares' corners agree" % tag] = bool(squares) and disagree == 0
        atlas = _ppm(_find(dump, material + "_tiles_albedo.ppm"))
        if atlas is None or not lay:
            c["%s: the baked atlas was written" % tag] = False
            continue
        cells = [cc[:4] for cc in lay["cells"]]
        seams = tile_seams(atlas, {"colours": colours, "cells": cells})
        d[mode + " seams (atlas)"] = seams
        c["%s: every two tiles that can meet, meet without a seam (on the atlas: worst %s, steps %s)" % (
            tag, seams["worst_across"], seams["step"])] = seams["seam_count"] == 0
        big = tiling(atlas, {"colours": colours, "cells": cells, "seed": lay["seed"]}, 12, 7)
        period = cells[0][2]
        ac = autocorrelation(_grey(big), period)
        repeats[mode] = ac
        if plain_tile is not None and "plain" not in repeats:
            # the plain tile resampled to the cells' size
            from PIL import Image
            t = np.asarray(Image.fromarray(plain_tile.astype(np.uint8)).resize((period, period)), dtype=np.float64)
            repeats["plain"] = autocorrelation(_grey(plain_tiling(t, 12, 7)), period)
        if mode == "tiles16":
            c["16 tiles: the floor repeats far less at the tile's period (%s; the plain tile %s; at most %.2f)" % (
                ac, repeats.get("plain"), REPEAT_MAX)] = ac is not None and ac <= REPEAT_MAX and \
                (repeats.get("plain") is None or ac < repeats["plain"] - 0.1)
        # the Brick patch (16 tiles, regular)
        if mode == "tiles16":
            bname = "MA" + (F["tiles16"].get("brick") or {}).get("material", "P10A4_brick")
            blay = _layout_json(dump, bname + "_tiles_albedo")
            batlas = _ppm(_find(dump, bname + "_tiles_albedo.ppm"))
            if blay and batlas is not None:
                bs = tile_seams(batlas, {"colours": 2, "cells": [cc[:4] for cc in blay["cells"]]})
                d["brick seams (atlas)"] = bs
                c["Brick (regular), 16 tiles: they meet without a seam (on the atlas: worst %s)" %
                  bs["worst_across"]] = bs["seam_count"] == 0
            else:
                c["Brick (regular), 16 tiles: its atlas baked"] = False
    d["repeat at the tile's period (autocorrelation, the baked atlas)"] = repeats
    tests["T10.4a Tile sets (P10.4, fake runtime)"] = {"ok": all(c.values()), "checks": c, "detail": d}
    P["_repeats_atlas"] = repeats


def _t_water(args, out, S, P, tests, sheets, p8, p2, r9):
    import numpy as np
    W = P.get("water")
    c, d = {}, {}
    if not W:
        tests["T10.4b Water flipbook (P10.4, fake runtime)"] = {"ok": False, "checks": {"made": False}, "detail": None}
        return
    run, lines, dump, adump = mock_run(args, out, "p10a4_water_mock", S(W["file"] + "_remix"), p8, r9)
    st = run.get("stats") or {}
    fb = W["flipbook"]
    material = "MA" + W["material"]
    c["Remix drew the water (the fake runtime)"] = bool(run["ran"]) and st.get("running") is True
    c["every texture encoded before the end"] = r9.textures_in(run)
    mats = r9.mock_materials(lines)
    m = _material(mats, material + "_flipbook")
    d["material"] = m and {k: m.get(k) for k in ("type", "albedo_tex", "normal_tex", "sprite")}
    want = "%d,%d,%d" % (fb["grid"][1], fb["grid"][0], fb["fps"])
    c["Remix's material is a sprite sheet: rows, columns, %d frames a second (%s)" % (fb["fps"], m and m.get("sprite"))] = \
        bool(m) and m.get("sprite") == want
    c["its textures the sheet's"] = bool(m) and "_flipbook_" in (m.get("normal_tex") or "")
    c["the frame rate from the speed (%.2f m/s: %d a second, a %.2f s loop)" % (
        fb["speed"], fb["fps"], fb["loop_seconds"])] = fb["fps"] == max(1, min(60, int(round(fb["speed"] * 64.0))))
    sheet = _ppm(_find(dump, material + "_flipbook_normal.ppm"))
    lay = _layout_json(dump, material + "_flipbook_normal")
    c["the sheet was baked (%d frames)" % fb["frames"]] = sheet is not None and bool(lay) and \
        len(lay["cells"]) == fb["frames"]
    if sheet is not None and lay:
        frames = [sheet[y:y + h, x:x + w] for x, y, w, h in [cc[:4] for cc in lay["cells"]]]
        steps = [float(np.abs(frames[k + 1] - frames[k]).mean()) for k in range(len(frames) - 1)]
        loop = float(np.abs(frames[0] - frames[-1]).mean())
        med = float(np.median(steps))
        d["frame steps"] = [round(s, 2) for s in steps]
        d["the loop's step (last to first)"] = round(loop, 2)
        c["the frames change (median step %.2f levels)" % med] = med > 0.3
        c["the last frame runs into the first without a jump (%.2f against %.2f)" % (loop, med)] = \
            loop <= LOOP_RATIO * med + LOOP_SLACK and loop >= 0.25 * med
    tests["T10.4b Water flipbook (P10.4, fake runtime)"] = {"ok": all(c.values()), "checks": c, "detail": d}


def _t_pack(args, out, S, P, tests, sheets, p8, p2, r9):
    I = P.get("images")
    c, d = {}, {}
    if not I or "near_packed" not in I:
        tests["T10.4c Packing small images (P10.4, fake runtime)"] = {"ok": False, "checks": {"made": False},
                                                                      "detail": I}
        return
    R = I["near_packed"]
    n = I["count"]
    d["pack"] = {k: R.get(k) for k in ("pages", "skipped", "warnings", "bleed_level", "bleed_distance", "checks")}
    c["all %d materials packed (%d)" % (n, len(R["materials"]))] = len(R["materials"]) == n and not R["skipped"]
    c["onto %d page(s)" % len(R["pages"])] = 1 <= len(R["pages"]) <= 2
    c["every page's gaps carry its images on, no two overlapping"] = all(x["ok"] for x in R["checks"].values())
    bd = R.get("bleed_distance")
    c["the far camera (%.0f m) nearer than where a page bleeds (%s m)" % (R["far_distance"], bd and round(bd, 1))] = \
        bd is not None and bd > R["far_distance"]
    c["the warning would come further out (%.0f m)" % R["very_far"]] = bd is not None and bd < R["very_far"]
    runs = {}
    for name in ("p10a4_images_near", "p10a4_packed_near"):
        runs[name] = mock_run(args, out, name + "_mock", S(name + "_remix"), p8, r9)
    files = I["files"]["p10a4_packed_near"]
    for name, (run, lines, dump, adump) in sorted(runs.items()):
        st = run.get("stats") or {}
        c["%s: Remix drew it (the fake runtime)" % name] = bool(run["ran"]) and st.get("running") is True
        c["%s: every texture encoded before the end" % name] = r9.textures_in(run)
        mats = r9.mock_materials(lines)
        tex = set()
        for k, f in sorted(files.items()):
            m = [x for x in mats if x.get("albedo_tex", "-") != "-" and
                 ((f["image"] in x["albedo_tex"]) or ("BGE-Remix_page" in x["albedo_tex"]))]
            tex |= set(x["albedo_tex"] for x in m)
        d[name + " albedo files"] = sorted(tex)
        if name == "p10a4_images_near":
            c["not packed: %d albedo files (%d)" % (n, len(tex))] = len(tex) == n
            continue
        c["packed: the materials read %d page file(s) (fewer textures)" % len(R["pages"])] = \
            len([t for t in tex if "BGE-Remix_page" in t]) >= 1 and \
            all("BGE-Remix_page" in t and t.endswith("_a.dds") for t in tex)
        # each square's mesh: its UVs inside its image's cell
        page = R["pages"][0]
        size = R["page_size"]
        outside = []
        for k, f in sorted(files.items()):
            cell = R["images"].get(f["image"], {}).get("rect")
            sq = _layout_json(adump, "P10A4_" + k + "__MA" + f["material"])
            if sq is None or cell is None:
                outside.append(k)
                continue
            if f["uv"] == "repeat" and len(sq["squares"]) != 4:
                outside.append(k + " (squares %d)" % len(sq["squares"]))
            if f["uv"] == "extend" and len(sq["squares"]) != 1:
                outside.append(k + " (squares %d)" % len(sq["squares"]))
        d["page"] = page
        d["squares not as expected"] = outside
        c["packed: each square split and moved into its image's cell (repeats: 4 squares, Extend: 1)"] = not outside
        uvs = [_floats(m.get("uvbox")) for m in _mock_meshes(lines)]
        c["packed: every mesh's UVs inside the page"] = all(u and all(-1e-4 <= v <= 1.0001 for v in u) for u in uvs
                                                            if u) and size > 0
    tests["T10.4c Packing small images (P10.4, fake runtime)"] = {"ok": all(c.values()), "checks": c, "detail": d}


def _t_pick(args, out, S, P, tests, sheets, p8, p2, r9):
    K = P.get("pick")
    c, d = {}, {}
    if not K:
        tests["T10.4d A tile picked by hand (P10.4, UV editor)"] = {"ok": False, "checks": {"made": False},
                                                                    "detail": None}
        return
    target = os.path.join(out, "p10a4_pick.json")
    saved = os.path.join(out, "p10a4_pick_saved_remix.blend")
    other = os.path.join(out, "p10a4_pick_other_remix.blend")
    for p in (target, saved, other):
        if os.path.isfile(p):
            os.remove(p)
    p8.blender_script(args.ours, out, "p10a4_pick", PICK_SCRIPT,
                      [S(K["file"] + "_remix"), saved, other, target, K["face"], K["tile"], K["other_tile"]])
    R = p2.read_json(target) or {}
    d["script"] = R
    want = [None] * K["faces"]
    want[K["face"]] = K["tile"]
    c["the UV editor's Pick Tile ran"] = bool(R.get("operator")) and all(o == ["FINISHED"] for o in R["operator"])
    c["saved in the file: reopened, face %d shows tile %d (the face layer)" % (K["face"], K["tile"])] = \
        R.get("cells") == want and R.get("layer") is True
    c["the atlas check sees the pick"] = R.get("check") == {str(K["face"]): K["tile"]}
    run, lines, dump, adump = mock_run(args, out, "p10a4_pick_mock", saved, p8, r9)
    st = run.get("stats") or {}
    c["Remix drew it (the fake runtime)"] = bool(run["ran"]) and st.get("running") is True
    sq = _layout_json(adump, "P10A4_pickfloor__MA" + K["material"])
    squares = {(s[0], s[1]): (s[2], s[3]) for s in (sq or {}).get("squares", [])}
    d["squares"] = sorted([list(k) + list(v) for k, v in squares.items()])
    fi, fj = K["squares"][K["face"]]
    c["Remix gets tile %d for face %d's square (%d, %d)" % (K["tile"], K["face"], fi, fj)] = \
        squares.get((fi, fj)) == (K["tile"], 1)
    rest = [k for k, v in squares.items() if k != (fi, fj) and
            (v[1] != 0 or v[0] != square_tile(k[0], k[1], K["seed"], K["colours"]))]
    c["the other faces by their place"] = bool(squares) and not rest
    tests["T10.4d A tile picked by hand (P10.4, UV editor)"] = {"ok": all(c.values()), "checks": c, "detail": d}
    P["_pick_files"] = {"saved": saved, "other": other}


def normal(args, out, scenes, E, tests, sheets):
    """the normal run (the fake Remix runtime)"""
    import run_p9 as r9
    import run_p8 as p8
    import run_p2 as p2
    P = E.get("p10a4") or {}
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    for fn in (_t_tiles, _t_water, _t_pack, _t_pick):
        try:
            fn(args, out, S, P, tests, sheets, p8, p2, r9)
        except Exception:
            import traceback
            tests["p10a4: %s ran" % fn.__name__] = {"ok": False, "checks": {"ran without an error": False},
                                                    "detail": traceback.format_exc()}


# ---------------------------------------------------------------------------
# rtx

def _rtx_play(args, out, tag, blend, p8, r9, extra=None):
    env = dict(r9.TEXTURE_ENV)
    env.update(extra or {})
    return p8.play(args.ours, out, tag, blend, env, frames=p8.REMIX_FRAMES, seconds=r9.TEXTURE_SECONDS, debug=True)


def rtx(args, out, scenes, E, tests, sheets, report):
    """the RTX run (the real RTX Remix runtime)"""
    import numpy as np
    import run_p9 as r9
    import run_p8 as p8
    import run_p2 as p2
    import p96_tests as t96
    P = E.get("p10a4") or {}
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731

    # S10.4a: the floors
    F = P.get("floors")
    c, d = {}, {}
    if F:
        shots = {}
        for mode in ("plain", "tiles16", "tiles81"):
            r = _rtx_play(args, out, "p10a4_rtx_" + mode, S(F[mode]["file"] + "_remix"), p8, r9)
            shots[mode] = r
            c["%s: ran in Remix, every texture in" % mode] = bool(r["ran"]) and r9.textures_in(r)
        reps = {}
        for mode, r in shots.items():
            pic = p8.picture(r.get("screenshot"))
            if pic is None:
                c["%s: a picture" % mode] = False
                continue
            sub = p8.crop(pic, F["rect"])
            reps[mode] = autocorrelation(_grey(sub), int(F["period_px"]))
            if mode != "plain":
                steps = t96._seam_steps(pic, F["rect"], F["columns"], F["rows"], p8)
                d[mode + " seams"] = steps
                c["%s: no seam where the tiles meet in Remix (%d boundaries)" % (mode, len(steps))] = \
                    bool(steps) and not any(x["seam"] for x in steps)
        d["repeat at the tile's period (Remix's picture)"] = reps
        c["16 tiles: less repeat at the tile's period than the plain tile (%s against %s; at most %.2f)" % (
            reps.get("tiles16"), reps.get("plain"), REPEAT_MAX)] = reps.get("tiles16") is not None and \
            reps.get("plain") is not None and reps["tiles16"] <= REPEAT_MAX and reps["tiles16"] < reps["plain"]
        sheets["p10a4_floors"] = p2.contact_sheet([("plain tile (Remix)", shots["plain"].get("screenshot")),
                                                   ("16 tiles (Remix)", shots["tiles16"].get("screenshot")),
                                                   ("81 tiles (Remix)", shots["tiles81"].get("screenshot"))],
                                                  os.path.join(out, "sheet_p10a4_floors.png"))
        report += ["", "P10.4 tile sets, repeat at the tile's period (autocorrelation; 1 = a plain repeat):",
                   "  Remix's picture: plain %s, 16 tiles %s, 81 tiles %s" % (
                       reps.get("plain"), reps.get("tiles16"), reps.get("tiles81"))]
    else:
        c["the floors were made"] = False
    tests["S10.4a Tile sets in Remix (P10.4)"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # S10.4b: the water moves
    W = P.get("water")
    c, d = {}, {}
    if W:
        paths = [os.path.join(out, "p10a4_water_t%d.png" % k) for k in range(3)]
        for p in paths:
            if os.path.isfile(p):
                os.remove(p)
        gap = 0.4
        r = _rtx_play(args, out, "p10a4_rtx_water", S(W["file"] + "_remix"), p8, r9,
                      {"BGE_REMIX_P10A4_SHOTS": ";".join(paths), "BGE_REMIX_P10A4_AFTER": str(r9.TEXTURE_SECONDS - 6),
                       "BGE_REMIX_P10A4_GAP": str(gap)})
        c["the water ran in Remix, every texture in"] = bool(r["ran"]) and r9.textures_in(r)
        pics = [p8.picture(p) for p in paths]
        if all(p is not None for p in pics):
            water = [float(np.abs(p8.crop(pics[k + 1], W["rect"]) - p8.crop(pics[k], W["rect"])).mean())
                     for k in range(2)]
            still = [float(np.abs(p8.crop(pics[k + 1], W["backdrop_rect"]) - p8.crop(pics[k], W["backdrop_rect"]))
                           .mean()) for k in range(2)]
            d["water's change between pictures"] = [round(x, 2) for x in water]
            d["the backdrop's change"] = [round(x, 2) for x in still]
            c["Remix's water changes over time (%s, the still backdrop %s)" % (
                d["water's change between pictures"], d["the backdrop's change"])] = \
                min(water) > 1.0 and min(water) > 3.0 * max(max(still), 0.2)
        else:
            c["three pictures %.1f s apart" % gap] = False
        sheets["p10a4_water"] = p2.contact_sheet([("water, t", paths[0]), ("water, t + %.1f s" % gap, paths[1])],
                                                 os.path.join(out, "sheet_p10a4_water.png"))
        report += ["", "P10.4 flipbook: %d frames at %d a second (a %.2f s loop); the loop's join is checked on the "
                   "baked sheet (normal run)" % (W["flipbook"]["frames"], W["flipbook"]["fps"],
                                                  W["flipbook"]["loop_seconds"])]
    else:
        c["the water was made"] = False
    tests["S10.4b Water flipbook in Remix (P10.4)"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # S10.4c: packed against not packed
    I = P.get("images")
    c, d = {}, {}
    if I:
        items = []
        for tag in ("near", "far"):
            a = _rtx_play(args, out, "p10a4_rtx_images_" + tag, S("p10a4_images_%s_remix" % tag), p8, r9)
            b = _rtx_play(args, out, "p10a4_rtx_packed_" + tag, S("p10a4_packed_%s_remix" % tag), p8, r9)
            c["%s: both ran in Remix, every texture in" % tag] = bool(a["ran"] and b["ran"]) and \
                r9.textures_in(a) and r9.textures_in(b)
            pa, pb = p8.picture(a.get("screenshot")), p8.picture(b.get("screenshot"))
            if pa is None or pb is None:
                c["%s: pictures" % tag] = False
                continue
            if tag == "near":
                rows = {}
                for k, rect in sorted(I["rects_near"].items()):
                    diff = float(np.abs(p8.crop(pa, rect).mean(axis=(0, 1)) - p8.crop(pb, rect).mean(axis=(0, 1))).max())
                    rows[k] = round(diff, 2)
                d["near: per square"] = rows
                c["near: every square packed within %.0f of not packed (worst %s)" % (TOL_PACK, max(rows.values()))] = \
                    max(rows.values()) <= TOL_PACK
            else:
                diff = float(np.abs(pa - pb).mean())
                d["far: whole picture"] = round(diff, 2)
                c["far: no bleed, packed within %.0f of not packed (%.2f)" % (TOL_PACK, diff)] = diff <= TOL_PACK
            items += [(tag + ", not packed", a.get("screenshot")), (tag + ", packed", b.get("screenshot"))]
        sheets["p10a4_packing"] = p2.contact_sheet(items, os.path.join(out, "sheet_p10a4_packing.png"))
    else:
        c["the images were made"] = False
    tests["S10.4c Packed images in Remix (P10.4)"] = {"ok": all(c.values()), "checks": c, "detail": d}

    # S10.4d: the picked tile shows
    K = P.get("pick")
    c, d = {}, {}
    if K:
        target = os.path.join(out, "p10a4_pick_rtx.json")
        saved = os.path.join(out, "p10a4_pick_saved_remix.blend")
        other = os.path.join(out, "p10a4_pick_other_remix.blend")
        p8.blender_script(args.ours, out, "p10a4_pick_rtx", PICK_SCRIPT,
                          [S(K["file"] + "_remix"), saved, other, target, K["face"], K["tile"], K["other_tile"]])
        a = _rtx_play(args, out, "p10a4_rtx_pick", saved, p8, r9)
        b = _rtx_play(args, out, "p10a4_rtx_pick_other", other, p8, r9)
        c["both ran in Remix, every texture in"] = bool(a["ran"] and b["ran"]) and r9.textures_in(a) and \
            r9.textures_in(b)
        pa, pb = p8.picture(a.get("screenshot")), p8.picture(b.get("screenshot"))
        if pa is not None and pb is not None:
            diffs = [round(float(np.abs(p8.crop(pa, r) - p8.crop(pb, r)).mean()), 2) for r in K["rects"]]
            d["per face: tile %d against tile %d" % (K["tile"], K["other_tile"])] = diffs
            others = [x for k, x in enumerate(diffs) if k != K["face"]]
            c["face %d shows the tile picked for it (%.2f)" % (K["face"], diffs[K["face"]])] = \
                diffs[K["face"]] >= PICK_SHOWS
            c["the other faces unchanged (worst %.2f)" % max(others)] = max(others) <= TOL_SAME
        else:
            c["pictures"] = False
        sheets["p10a4_pick"] = p2.contact_sheet([("face %d: tile %d" % (K["face"], K["tile"]), a.get("screenshot")),
                                                 ("face %d: tile %d" % (K["face"], K["other_tile"]),
                                                  b.get("screenshot"))],
                                                os.path.join(out, "sheet_p10a4_pick.png"))
    else:
        c["the pick floor was made"] = False
    tests["S10.4d A picked tile in Remix (P10.4)"] = {"ok": all(c.values()), "checks": c, "detail": d}
