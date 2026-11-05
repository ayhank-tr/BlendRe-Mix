"""BGE-Remix p10a: the tests for the texture remaster (P10.1-P10.3, P10.5)
(called from run_p10.py; the scenes from p10a_scenes, the UI probe
p10a_probe.py).

normal (the fake Remix runtime):
  T10.1  the audit: noise random, bricks regular, the sign unique; the two
         densities within 10 %; the missing channels; the side-lit image's
         painted light (and none on its flat-lit copy); the plans; the file
         unchanged but for the report
  T10.2  the value check: each out-of-range box found, with a fix; the fine
         one not
  T10.3  derived channels for the sign (approximate, unit normals, height
         and roughness in range); the side-lit copy evened out to its
         flat-lit one; the amount slider
  T10.4  the tile bakes (dumped by the fake runtime's run): Brick repeats
         without a seam and has no black brick; Rock's cross-faded band
         keeps its contrast (no ghosting); both wrap around
  T10.5  Classic colour images reach Remix times Diffuse Intensity (the
         brightness finding): the image's file tagged with its scale, the
         colour 1
  T10.6  preset fitting: each preset baked at known settings is fitted
         again (the right preset, each setting within its tolerance, a
         high score); the sign scores low (derive channels)
  T10.7  the preview square: the original and the result side by side, and
         nothing of it saved
  T10.8  Apply: the materials written (fitted presets, derived images in the
         remaster folder), the .p8 copies kept and removed at the second
         save, every texture encoded up front: the game starts with none
         waiting (the fake runtime)
rtx (the real runtime):
  S10.1  Brick and Rock tiles at large size in Remix: no seam where the
         tiles meet, no ghosting in the cross-faded bands
  S10.2  the Classic tiles against Remix within r9.TOL_GATE (the 25 levels)
  S10.3  Apply, then the game in Remix: no texture waiting at the start;
         the preview square in the Remix View's Debug picture
  S10.4  the 2K encode time (and 1K's) with Remix running, reported
"""

import json
import os
import shutil

TOL_DENSITY = 0.10          # P10.1: densities within 10 %
CONTRAST_KEPT = 0.85        # P10.2: a cross-faded band's detail against the tile's middle (P9.6's: about 0.7)
BLACK_BRICKS = 0.002        # P10.2: the share of a Brick tile's pixels with red under 15 (the black brick)
TOL_EVEN = 0.08             # P10.3: the evened side-lit copy against the flat-lit one (relative; 0.05 here)
EVEN_GAIN = 0.3             # ... and at most this share of the side-lit copy's own difference (0.24 here)
GHOST_MIN = 0.8             # S10.1: the bands' detail against the middles' in Remix's picture

NAMES = {
    "audit": "T10.1 The audit (P10.1)",
    "values": "T10.2 The value check (P10.3)",
    "derived": "T10.3 Derived channels (P10.3)",
    "tiles": "T10.4 Tile bakes: whole repeats, contrast kept (P10.2, fake runtime)",
    "classic": "T10.5 Classic colour images carry Diffuse Intensity (P10.3, fake runtime)",
    "fit": "T10.6 Preset fitting (P10.2)",
    "preview": "T10.7 The preview square (P10.5)",
    "apply": "T10.8 Apply: no texture waiting at the start (P10.5, fake runtime)",
}
RTX_NAMES = {
    "tiles": "S10.1 Brick and Rock tiles in Remix: no seam, no ghosting (P10.2)",
    "classic": "S10.2 Classic tiles against Remix (P10.3, the gate)",
    "apply": "S10.3 Apply, then the game in Remix (P10.5)",
    "encode": "S10.4 2K bakes' encode time with Remix running (P10.2)",
}

AUDIT_CHECK = r'''
import bpy, json, sys
argv = sys.argv[sys.argv.index("--") + 1:]
target = argv[0]
r = {}
try:
    import numpy as np
    import bge_remix
    from bge_remix import remaster_tool as T
    from bge_remix import remaster as R
    before = T.file_fingerprint()
    a = bge_remix.remaster_audit()
    after = T.file_fingerprint()
    r["audit"] = a
    r["unchanged"] = before == after
    r["changed"] = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))[:20]
    text = bpy.data.texts.get(T.AUDIT_TEXT)
    r["text"] = text.as_string()[:4000] if text else None
    # P10.3: derived channels (numpy only: no bake needed)
    sign = bpy.data.images["P10A_sign"]
    albedo, rough, height, normal = T.derived_channels(sign, T.audit_row(sign.name))
    n = normal * 2.0 - 1.0
    ln = np.sqrt((n ** 2).sum(axis=2))
    r["derived"] = {"shape": list(albedo.shape), "normal_len": [float(ln.min()), float(ln.max())],
                    "normal_z_min": float(n[..., 2].min()), "height": [float(height.min()), float(height.max())],
                    "height_spread": float(height.std()), "rough": [float(rough.min()), float(rough.max())],
                    "albedo": [float(albedo.min()), float(albedo.max())]}
    side, flat = bpy.data.images["P10A_side_lit"], bpy.data.images["P10A_flat_lit"]
    out = {}
    for amount in (0.0, 0.5, 1.0):
        alb = T.derived_channels(side, T.audit_row(side.name), amount)[0]
        lum = R.luminance(alb)
        out[str(amount)] = {"painted": R.painted_light(lum)[0]}
    flat_rgb = T.image_rgb(flat)
    even = T.derived_channels(side, T.audit_row(side.name), 1.0)[0]
    fl, ev = R.luminance(flat_rgb), R.luminance(even)
    fl, ev = fl / fl.mean(), ev / ev.mean()
    out["evened_vs_flat"] = float(np.abs(fl - ev).mean())
    out["side_vs_flat"] = float(np.abs(fl - R.luminance(T.image_rgb(side)) / R.luminance(T.image_rgb(side)).mean()).mean())
    out["flat_painted"] = R.painted_light(R.luminance(flat_rgb))[0]
    r["light"] = out
except Exception:
    import traceback
    r["error"] = traceback.format_exc()
with open(target, "w") as f:
    json.dump(r, f, indent=1, default=str)
print("P10A-AUDIT done")
'''


def _close(a, b, tol):
    try:
        return abs(float(a) - float(b)) <= tol
    except (TypeError, ValueError):
        return False


def _fail(tests, name, why, detail=None):
    tests[name] = {"ok": False, "checks": {why: False}, "detail": detail}


def _scene_ok(E, part, tests, name):
    A = (E.get("p10a") or {})
    if part not in A:
        _fail(tests, name, "the %s scene was made" % part, (A.get("failed") or {}).get(part) or E.get("failed"))
        return None
    return A[part]


def probe(args, out, tag, blend, parts, env, timeout=3600):
    import run_p2 as p2
    import run_p8 as p8
    res = os.path.join(out, tag + "_p10a.json")
    if os.path.isfile(res):
        os.remove(res)
    folder = os.path.join(out, tag)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    e = p2.base_env(out, tag)
    e.update(p8.GL_ENV)
    e.update(env)
    e.update({"BGE_REMIX_P10A_OUT": res, "BGE_REMIX_P10A_PARTS": parts, "BGE_REMIX_P10A_DIR": folder})
    code, text, secs = p2.run([p2.exe(args.ours, "blender"), "--disable-crash-handler"] + p8.WINDOW +
                              [blend, "--python", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                               "p10a_probe.py")],
                              e, os.path.join(out, tag + ".log"), timeout=timeout, cwd=out)
    r = p2.read_json(res) or {}
    r.update({"exit": code, "seconds": round(secs, 1), "folder": folder})
    r["ran"] = code == 0 and bool(r.get("finished"))
    return r


def catalogue_dir(out):
    return os.path.join(out, "p10a_catalogue")


# ---------------------------------------------------------------------------
# normal

def t_audit(args, out, scenes, E, tests):
    import run_p2 as p2
    import run_p8 as p8
    A = _scene_ok(E, "audit", tests, NAMES["audit"])
    if A is None:
        for k in ("values", "derived"):
            _fail(tests, NAMES[k], "the audit scene was made")
        return
    target = os.path.join(out, "p10a_audit.json")
    if os.path.isfile(target):
        os.remove(target)
    p8.blender_script(args.ours, out, "p10a_audit", AUDIT_CHECK, [target],
                      blend=os.path.join(scenes, A["file"] + ".blend"))
    R = p2.read_json(target) or {}
    a = R.get("audit") or {}
    rows = {r["image"]: r for r in a.get("images", [])}
    base = {"error": R.get("error")}
    c = {"the audit ran": bool(a) and not R.get("error")}
    for name, kind in A["kinds"].items():
        r = rows.get(name) or {}
        c["%s is classed %s (%s: %s)" % (name, kind.lower(), r.get("kind"), r.get("kind_measures"))] = \
            r.get("kind") == kind
        c["%s's plan: %s (%s)" % (name, A["plans"][name], r.get("plan"))] = r.get("plan") == A["plans"][name]
    for name, want in A["density"].items():
        got = (rows.get(name) or {}).get("density")
        c["%s: %.4g px/m within %d %% (%s)" % (name, want, TOL_DENSITY * 100, got)] = \
            got is not None and abs(got - want) <= TOL_DENSITY * want
    for name, want in A["missing"].items():
        got = (rows.get(name) or {}).get("missing")
        c["%s: missing %s (%s)" % (name, ", ".join(want), got)] = got == want
    for name, side in A["painted"].items():
        r = rows.get(name) or {}
        if side:
            c["%s: painted light reported, darker on the %s (%s, %s)" % (name, side, r.get("painted_light"),
                                                                        r.get("painted_side"))] = \
                r.get("painted_side") == side
        else:
            c["%s: no painted light (%s)" % (name, r.get("painted_light"))] = r.get("painted_side") is None
    c["the file unchanged by the audit (but its report)%s" % (": " + ", ".join(R.get("changed") or [])
                                                             if R.get("changed") else "")] = R.get("unchanged") is True
    c["the report written to the text \"BGE-Remix remaster audit\""] = bool(R.get("text")) and \
        "remaster audit" in (R.get("text") or "")
    tests[NAMES["audit"]] = {"ok": all(c.values()), "checks": c, "detail": dict(base, rows=rows)}

    # P10.3: the value check
    vals = a.get("values") or []
    c = {}
    for ma, field in A["values"].items():
        mine = [v for v in vals if v["material"] == ma]
        if field is None:
            c["%s: no problem (%s)" % (ma, [v["problem"] for v in mine])] = not mine
        else:
            hit = [v for v in mine if v["field"] == field]
            c["%s: its %s found (%s)" % (ma, field, hit[0]["problem"] if hit else mine)] = bool(hit)
            c["%s: with a suggested fix" % ma] = bool(hit) and bool(hit[0].get("fix"))
    tests[NAMES["values"]] = {"ok": all(c.values()) and bool(c), "checks": c, "detail": {"values": vals}}

    # P10.3: derived channels
    d = R.get("derived") or {}
    L = R.get("light") or {}
    c = {"derived for the sign": bool(d)}
    if d:
        c["the sign's normals unit length, facing out (%s, z %.2f)" % (d["normal_len"], d["normal_z_min"])] = \
            d["normal_len"][0] > 0.98 and d["normal_len"][1] < 1.02 and d["normal_z_min"] > 0.0
        c["its height 0..1 with detail (spread %.3f)" % d["height_spread"]] = \
            d["height"][0] >= 0.0 and d["height"][1] <= 1.0 and d["height_spread"] > 0.02
        c["its roughness 0.3..0.95 (%s)" % d["rough"]] = d["rough"][0] >= 0.3 and d["rough"][1] <= 0.95
        c["its size kept (no upscaling: %s)" % d["shape"]] = d["shape"][:2] == [512, 512]
    sign_row = rows.get(A["sign"]) or {}
    c["the sign is planned to derive channels"] = sign_row.get("plan") == 'DERIVE'
    if L:
        p0, p5, p1 = (L.get(k, {}).get("painted") for k in ("0.0", "0.5", "1.0"))
        c["the side-lit copy's painted light reduced (%.2f -> %.2f -> %.2f, the flat copy %.2f)" % (
            p0, p5, p1, L.get("flat_painted", -1))] = p0 > p5 > p1 and p1 < 0.15
        c["evened out, it is the flat-lit copy within %.0f %% (%.3f; as it was %.3f)" % (
            TOL_EVEN * 100, L.get("evened_vs_flat", 1), L.get("side_vs_flat", 1))] = \
            L.get("evened_vs_flat", 1.0) <= TOL_EVEN and \
            L.get("evened_vs_flat", 1.0) <= EVEN_GAIN * L.get("side_vs_flat", 0.0)
    tests[NAMES["derived"]] = {"ok": all(c.values()), "checks": c, "detail": {"derived": d, "light": L}}


def _ppm(path):
    try:
        from PIL import Image
        import numpy as np
    except ImportError:
        return None
    try:
        return np.asarray(Image.open(path).convert("RGB"), dtype=np.float64)
    except OSError:
        return None


def _detail(a):
    """each pixel's difference from its 5 x 5 neighbourhood's mean (the
    fine detail), per column"""
    import numpy as np
    g = a.mean(axis=2) if a.ndim == 3 else a
    k = np.ones(5) / 5.0
    b = np.apply_along_axis(lambda r: np.convolve(np.concatenate([r[-2:], r, r[:2]]), k, mode='valid'), 1, g)
    b = np.apply_along_axis(lambda r: np.convolve(np.concatenate([r[-2:], r, r[:2]]), k, mode='valid'), 0, b)
    return np.abs(g - b).mean(axis=0)


def band_contrast(a):
    """a baked tile's cross-faded band (its last eighth, across and up)
    against its middle: the fine detail's ratio"""
    import numpy as np
    n = a.shape[1]
    w = max(1, int(round(n * 0.125)))
    det_x = _detail(a)
    det_y = _detail(np.transpose(a, (1, 0, 2)))
    mid = slice(int(n * 0.4), int(n * 0.6))
    rx = float(det_x[n - w:].mean() / max(det_x[mid].mean(), 1e-6))
    ry = float(det_y[:w].mean() / max(det_y[mid].mean(), 1e-6))  # (ppm rows from the top: v's far band is the top)
    return round(rx, 3), round(ry, 3)


def t_tiles(args, out, scenes, E, tests):
    import run_p8 as p8
    import run_p9 as r9
    import p96_tests as t96
    A = _scene_ok(E, "tiles", tests, NAMES["tiles"])
    if A is None:
        return
    dump, cache = os.path.join(out, "p10a_bakes"), os.path.join(out, "cache_p10a_tiles")
    for folder in (dump, cache):
        shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(dump)
    log, env = p8.mock_env(args, out, "p10a_tiles_mock", cache, dump=dump)
    run = r9.pipe_play(args.ours, out, "p10a_tiles_mock", os.path.join(scenes, "p10a_tiles_remix.blend"), env,
                       seconds=15)
    c = {"the tiles ran (the fake runtime)": bool(run["ran"])}
    d = {}
    for key, s in A["seams"].items():
        mat = s["material"]
        c["%s: the tile's mode (%s)" % (key, s["mode"])] = s["mode"] == (1 if key == 'BRICK' else 2)
        for ch in ("albedo", "height"):
            files = [f for f in os.listdir(dump) if mat in f and f.endswith("_%s.ppm" % ch)]
            if not files:
                c["%s: its %s baked" % (key, ch)] = False
                continue
            path = os.path.join(dump, files[0])
            seam = t96._ppm_seam(path)
            a = _ppm(path)
            d["%s %s" % (key, ch)] = {"seam": seam}
            if seam is not None:
                c["%s %s repeats without a seam (wrap %s / %s against steps %s / %s)" % (
                    key, ch, seam["wrap_x"], seam["wrap_y"], seam["step_x"], seam["step_y"])] = seam["seamless"]
            if a is not None and key != 'BRICK':
                rx, ry = band_contrast(a)
                d["%s %s" % (key, ch)]["band_contrast"] = [rx, ry]
                c["%s %s: the cross-faded band keeps its detail (%.2f, %.2f of the middle's; at least %.2f)" % (
                    key, ch, rx, ry, CONTRAST_KEPT)] = rx >= CONTRAST_KEPT and ry >= CONTRAST_KEPT
            if a is not None and key == 'BRICK' and ch == "albedo":
                black = float((a[..., 0] < 15.0).mean())
                d["BRICK black share"] = black
                c["BRICK: no black brick at the tile's corner (%.4f of the pixels)" % black] = black <= BLACK_BRICKS
    tests[NAMES["tiles"]] = {"ok": all(c.values()), "checks": c, "detail": d}


INSPECT_CHECK = r'''
import bpy, json, sys
argv = sys.argv[sys.argv.index("--") + 1:]
target = argv[0]
r = {}
try:
    import bge_remix
    scene = bpy.context.scene
    for ob in scene.objects:
        if ob.name.startswith("P10C_"):
            r[ob.name] = bge_remix.inspect(ob, scene)
except Exception:
    import traceback
    r["error"] = traceback.format_exc()
with open(target, "w") as f:
    json.dump(r, f, indent=1, default=str)
print("P10A-INSPECT done")
'''


def t_classic(args, out, scenes, E, tests):
    import run_p2 as p2
    import run_p8 as p8
    import run_p9 as r9
    A = _scene_ok(E, "classic", tests, NAMES["classic"])
    if A is None:
        return
    # what Remix gets, as the Remix View's Inspect shows it (the image's scale)
    target = os.path.join(out, "p10a_classic_inspect.json")
    if os.path.isfile(target):
        os.remove(target)
    p8.blender_script(args.ours, out, "p10a_classic_inspect", INSPECT_CHECK, [target],
                      blend=os.path.join(scenes, "p10a_classic_remix.blend"))
    ins = p2.read_json(target) or {}
    cache = os.path.join(out, "cache_p10a_classic")
    shutil.rmtree(cache, ignore_errors=True)
    log, env = p8.mock_env(args, out, "p10a_classic_mock", cache)
    run = r9.pipe_play(args.ours, out, "p10a_classic_mock", os.path.join(scenes, "p10a_classic_remix.blend"), env,
                       seconds=10)
    mats = r9.mock_materials(r9.mock_lines(log))
    c = {"the Classic tiles ran (the fake runtime)": bool(run["ran"]),
         "every texture encoded": r9.textures_in(run), "inspected": bool(ins) and not ins.get("error")}
    d = {"inspect error": ins.get("error")}
    for label in A["order"]:
        t = A["tiles"][label]
        mine = [m for m in mats if r9.near(m.get("roughness"), t["roughness"], 0.002) and
                m.get("albedo_tex", "-") != "-"]
        m = mine[-1] if mine else {}
        rep = ins.get("P10C_" + label) or {}
        texs = ((rep.get("materials") or [{}])[0].get("textures") or [])
        alb = next((x for x in texs if x.get("field") == "albedo"), {})
        d[label] = {"mock": {k: m.get(k) for k in ("albedo", "albedo_tex", "roughness")}, "inspect": alb}
        c["%s: Remix got its colour image" % label] = bool(m)
        if t["face"]:
            # (face images as P8 made them: see the guide)
            c["%s: a face image, as P8 sent it (scale %s, colour %s)" % (label, alb.get("scale"), m.get("albedo"))] = \
                _close(alb.get("scale"), 1.0, 1e-3) and r9.near(m.get("albedo"), r9.srgb([t["ref"]] * 3), 0.003)
            continue
        c["%s: its image times Diffuse Intensity %.2f (%s)" % (label, t["ref"], alb.get("scale"))] = \
            _close(alb.get("scale"), t["ref"], 1e-3)
        c["%s: Remix's colour 1 (the image is the colour: %s)" % (label, m.get("albedo"))] = \
            r9.near(m.get("albedo"), [1.0, 1.0, 1.0], 0.002)
    tests[NAMES["classic"]] = {"ok": all(c.values()), "checks": c, "detail": d}


def _fit_checks(c, key, r, F):
    if "error" in r:
        c["%s: fitted" % key] = False
        return
    fit, truth = r["fit"], r["truth"]
    fam = F["families"].get(key, key)
    got = fit["preset"]
    c["%s: the preset found (%s; ranking %s)" % (key, got, fit.get("ranking"))] = F["families"].get(got, got) == fam
    c["%s: a high score (%.2f, at least %.2f)" % (key, fit["score"], 0.5)] = fit["score"] >= 0.5
    if got != key and F["families"].get(got, got) != fam:
        return
    tol = F["tolerance"]
    for name, want in truth["settings"].items():
        if name == "Seed":
            continue  # (the pattern's place, not its look: not to be found again)
        have = fit["settings"].get(name)
        if name == "Scale":
            ok = have is not None and abs(have - want) <= tol["Scale"] * want
            text = "within %d %%" % (tol["Scale"] * 100)
        else:
            ok = have is not None and abs(have - want) <= tol[name]
            text = "within %.2f" % tol[name]
        c["%s: %s %.3g found again %s (%s)" % (key, name, want, text, have)] = ok
    cw, ch = truth["colour"], fit["colour"]
    c["%s: the colour found again within %.2f (%s against %s)" % (key, tol["colour"], ch, [round(x, 3) for x in cw])] = \
        all(abs(a - b) <= tol["colour"] for a, b in zip(ch, cw))


def t_fit(args, out, scenes, E, tests):
    import run_p8 as p8
    A = _scene_ok(E, "fit", tests, NAMES["fit"])
    if A is None:
        return None
    log, env = p8.mock_env(args, out, "p10a_fit", os.path.join(out, "cache_p10a_fit"))
    env.update({"BGE_REMIX_REMASTER_CATALOGUE": catalogue_dir(out), "BGE_REMIX_P10A_FIT": json.dumps(A)})
    shutil.rmtree(catalogue_dir(out), ignore_errors=True)
    r = probe(args, out, "p10a_fit", os.path.join(scenes, A["file"] + ".blend"), "fit", env)
    s = r.get("steps", {})
    cat = s.get("s_catalogue") or {}
    c = {"the probe ran": r["ran"],
         "every preset's catalogue built (%s s)" % cat.get("seconds"): bool(cat) and not cat.get("missing_after"),
         "about 200 entries each (%s)" % sorted(set((cat.get("entries") or {}).values())):
             bool(cat.get("entries")) and all(150 <= n <= 260 for n in cat["entries"].values())}
    fits = s.get("s_fits") or {}
    for key in sorted(fits):
        _fit_checks(c, key, fits[key], A)
    sign = (s.get("s_sign") or {}).get("fit") or {}
    c["the sign scores low (%.2f, under %.2f): derive channels" % (sign.get("score", -1), 0.5)] = \
        bool(sign) and sign.get("plan") == 'DERIVE'
    tests[NAMES["fit"]] = {"ok": all(c.values()), "checks": c,
                           "detail": {"errors": r.get("errors"), "fits": fits, "sign": sign, "catalogue": cat}}
    return r


def t_apply(args, out, scenes, E, tests, sheets, rtx=False, report=None):
    """the apply probe (and its preview), then the game on the saved copy"""
    import run_p2 as p2
    import run_p8 as p8
    import run_p9 as r9
    name_apply = RTX_NAMES["apply"] if rtx else NAMES["apply"]
    A = _scene_ok(E, "apply", tests, name_apply)
    if A is None:
        if not rtx:
            _fail(tests, NAMES["preview"], "the apply scene was made")
        return
    tag = "p10a_apply_rtx" if rtx else "p10a_apply"
    folder = os.path.join(out, tag + "_file")
    shutil.rmtree(folder, ignore_errors=True)
    copy = os.path.join(folder, "p10a_applied_remix.blend")
    cache = os.path.join(out, "cache_" + tag)
    shutil.rmtree(cache, ignore_errors=True)
    if rtx:
        env = {"BGE_REMIX_CACHE_DIR": cache}
    else:
        log, env = p8.mock_env(args, out, tag, cache)
    env.update({"BGE_REMIX_REMASTER_CATALOGUE": catalogue_dir(out), "BGE_REMIX_P10A_COPY": copy})
    r = probe(args, out, tag, os.path.join(scenes, A["file"] + ".blend"), "apply,views" if rtx else "apply", env)
    s = r.get("steps", {})
    base = {"ran": r["ran"], "errors": r.get("errors"), "exit": r.get("exit")}

    if not rtx:
        # the preview
        pv, ps = s.get("s_preview") or {}, s.get("s_preview_saved") or {}
        c = {"the probe ran": r["ran"],
             "two squares, the original and the result (%s)" % pv.get("objects"):
                 len(pv.get("objects") or []) == 2,
             "the original square shows the file's material": any(
                 o.endswith("original") and m == pv.get("image") for o, m in (pv.get("materials") or {}).items()),
             "the result square shows a copy of it (the file's material unchanged)": any(
                 o.endswith("result") and m and m.endswith("(remaster preview)")
                 for o, m in (pv.get("materials") or {}).items()),
             "saving closes the preview: none of it in the file (%s, %s)" % (ps.get("objects"),
                                                                            ps.get("temp_materials")):
                 bool(ps) and not ps.get("objects") and not ps.get("temp_materials")}
        tests[NAMES["preview"]] = {"ok": all(c.values()), "checks": c, "detail": dict(base, preview=pv, saved=ps)}

    ap = s.get("s_apply") or {}
    s1, s2 = s.get("s_save1") or {}, s.get("s_save2") or {}
    applied = ap.get("applied") or {}
    poll = applied.get("poll") or {}
    walls = ap.get("walls") or {}
    fits = s.get("s_fit_walls") or {}
    c = {"the probe ran": r["ran"], "Apply finished": ap.get("ret") == ["FINISHED"]}
    for ima, want in A["walls"].items():
        st = walls.get(ima) or {}
        plan = (fits.get(ima) or {}).get("plan_now")
        c["%s: an RTX Remix material now (%s)" % (ima, st.get("kind"))] = st.get("kind") == 'RTX_REMIX'
        if plan == 'FIT':
            fit = (fits.get(ima) or {}).get("fit") or {}
            c["%s: the fitted preset's group node (%s, %s)" % (ima, st.get("group"), fit.get("preset"))] = \
                st.get("preset") == fit.get("preset") and bool(st.get("group"))
            c["%s: its Scale and Wear as fitted" % ima] = abs((st.get("inputs") or {}).get("Scale", -1) -
                                                           (fit.get("settings") or {}).get("Scale", -2)) < 1e-3
            c["%s: its tile the mesh's size (%s m)" % (ima, st.get("tile_metres"))] = \
                abs((st.get("tile_metres") or 0) - (fit.get("tile_metres") or -1)) < 1e-3
            c["%s: baked at 2K for Remix (%s)" % (ima, st.get("bake_size"))] = st.get("bake_size") == 2048.0
        elif plan == 'DERIVE':
            wired = st.get("wired") or {}
            for sock in ("Albedo", "Roughness", "Normal", "Height"):
                w = wired.get(sock) or []
                c["%s: %s from a derived image in the remaster folder (%s)" % (ima, sock, w[1:2])] = \
                    len(w) >= 5 and w[0] == "ShaderNodeTexture" and w[3] is True and w[4] is True and \
                    os.sep + "remaster" + os.sep in (w[2] or "")
        else:
            c["%s: planned (%s)" % (ima, plan)] = False
        c["%s: the plan as the scene expects (%s)" % (ima, want["plan"])] = want["plan"] == 'FIT' or plan == 'DERIVE'
        p8n = ima + ".p8"
        c["%s: its original kept as %s" % (ima, p8n)] = p8n in (ap.get("copies") or {})
        c["%s: still kept after the first save" % ima] = p8n in (s1.get("copies") or {})
        c["%s: removed at the second save" % ima] = bool(s2) and p8n not in (s2.get("copies") or {})
    c["every texture baked and encoded up front (%s asked, %s encoded, %s there, %s failed, %s waiting)" % (
        applied.get("asked"), poll.get("encoded"), poll.get("reused"), poll.get("failed"), poll.get("waiting"))] = \
        bool(applied.get("asked")) and poll.get("waiting") == 0 and poll.get("failed") == 0
    # the game on the saved file: nothing left to bake or encode
    env2 = {"BGE_REMIX_CACHE_DIR": cache}
    if rtx:
        play = p8.play(args.ours, out, tag + "_game", copy, dict(r9.TEXTURE_ENV, **env2), frames=p8.REMIX_FRAMES,
                       seconds=r9.TEXTURE_SECONDS)
    else:
        log2, envm = p8.mock_env(args, out, tag + "_game", cache)
        play = r9.pipe_play(args.ours, out, tag + "_game", copy, envm, seconds=8) if os.path.isfile(copy) else \
            {"ran": False, "stats": {}}
    st = play.get("stats") or {}
    c["the game ran on the saved file"] = bool(play.get("ran"))
    c["the game encoded nothing: every texture was there (%s encoded, %s found)" % (
        st.get("textures_encoded"), st.get("textures_reused"))] = \
        st.get("textures_encoded") == 0 and (st.get("textures_reused") or 0) > 0 and st.get("textures_failed") == 0
    c["the game baked nothing: every bake was there (%s baked, %s from the cache)" % (
        st.get("procedural_bakes"), st.get("procedural_bakes_cached"))] = \
        st.get("procedural_bakes") == st.get("procedural_bakes_cached")
    c["no texture waiting when the game starts (first textured frame %s)" % st.get("first_textured_frame")] = \
        st.get("first_textured_frame") is not None and 0 <= st.get("first_textured_frame") <= 2
    detail = dict(base, apply=ap, saves=[s1, s2], fits=fits, game_stats={k: st.get(k) for k in (
        "textures_encoded", "textures_reused", "textures_failed", "textures_waiting", "procedural_bakes",
        "procedural_bakes_cached", "first_textured_frame", "first_textured_seconds", "encode_seconds")})
    tests[name_apply] = {"ok": all(c.values()), "checks": c, "detail": detail}
    if report is not None:
        report.append("")
        report.append("Apply (%s): %s textures asked, %s encoded in %.1f s by the bridge before the game (%s)" % (
            "RTX" if rtx else "fake runtime", applied.get("asked"), poll.get("encoded"), poll.get("seconds") or 0.0,
            "GPU BC7" if rtx else "CPU"))
    if rtx:
        shots = [("Remix View, Debug", (s.get("s_debug_shot") or {}).get("path")),
                 ("Debug, the preview square (left original, right result)", (s.get("s_preview_shot") or {}).get("path")),
                 ("the game in Remix after Apply", play.get("screenshot"))]
        sheets["p10a_apply"] = p2.contact_sheet(shots, os.path.join(out, "sheet_p10a_apply.png"))


def normal(args, out, scenes, E, tests, sheets):
    """the normal run (the fake Remix runtime)"""
    if not getattr(args, "mock", None):
        for n in NAMES.values():
            _fail(tests, n, "the fake runtime was given (--mock)")
        return
    for fn in (t_audit, t_tiles, t_classic):
        try:
            fn(args, out, scenes, E, tests)
        except Exception:
            import traceback
            tests["p10a: %s ran" % fn.__name__] = {"ok": False, "checks": {"ran without an error": False},
                                                    "detail": traceback.format_exc()}
    t_fit(args, out, scenes, E, tests)
    t_apply(args, out, scenes, E, tests, sheets)


# ---------------------------------------------------------------------------
# rtx

def s_tiles(args, out, scenes, E, tests, sheets, report):
    import run_p2 as p2
    import run_p8 as p8
    import run_p9 as r9
    import p96_tests as t96
    A = _scene_ok(E, "tiles", tests, RTX_NAMES["tiles"])
    if A is None:
        return
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    shot = {"BGE_REMIX_P2_SHOT_FRAME": "300"}
    sr = p8.play(args.ours, out, "p10a_tiles_remix", S("p10a_tiles_remix"), r9.TEXTURE_ENV, frames=p8.REMIX_FRAMES,
                 seconds=r9.TEXTURE_SECONDS, debug=True)
    sg = p8.play(args.ours, out, "p10a_tiles_opengl", S("p10a_tiles"), shot, frames=p8.REMIX_FRAMES)
    c = {"the tiles ran in Remix": bool(sr["ran"]), "every texture in Remix before the picture": r9.textures_in(sr)}
    d = {}
    pic, gpic = p8.picture(sr.get("screenshot")), p8.picture(sg.get("screenshot"))
    import numpy as np
    for key, s in A["seams"].items():
        if pic is None:
            c["%s: a picture" % key] = False
            continue
        steps = t96._seam_steps(pic, s["rect"], s["columns"], s["rows"], p8)
        d[key] = {"seams": steps}
        c["%s: no seam where the tiles meet (%s)" % (key, ", ".join("%s/%s" % (x["across"], x["beside"])
                                                                   for x in steps))] = \
            bool(steps) and not any(x["seam"] for x in steps)
        sub = p8.crop(pic, s["rect"])
        det = _detail(sub)
        left = s["rect"][0]
        ratios = []
        for (b0, b1), (m0, m1) in zip(s["bands"], s["middles"]):
            band = det[max(0, b0 - left):max(1, b1 - left)].mean()
            mid = det[max(0, m0 - left):max(1, m1 - left)].mean()
            ratios.append(round(float(band / max(mid, 1e-6)), 3))
        d[key]["band_detail"] = ratios
        if gpic is not None:
            gsub = p8.crop(gpic, s["rect"])
            d[key]["game_band_detail"] = [round(float(_detail(gsub)[max(0, b0 - left):max(1, b1 - left)].mean() /
                                                  max(_detail(gsub)[max(0, m0 - left):max(1, m1 - left)].mean(), 1e-6)),
                                                3) for (b0, b1), (m0, m1) in zip(s["bands"], s["middles"])]
        c["%s: no ghosting: the bands' detail %s of the middles' (at least %.2f)" % (key, ratios, GHOST_MIN)] = \
            bool(ratios) and float(np.min(ratios)) >= GHOST_MIN
    tests[RTX_NAMES["tiles"]] = {"ok": all(c.values()), "checks": c, "detail": d}
    sheets["p10a_tiles"] = p2.contact_sheet([("tiles, game", sg.get("screenshot")),
                                             ("tiles, RTX Remix", sr.get("screenshot"))],
                                            os.path.join(out, "sheet_p10a_tiles.png"))


def s_classic(args, out, scenes, E, tests, sheets, report):
    import run_p2 as p2
    import run_p8 as p8
    import run_p9 as r9
    A = _scene_ok(E, "classic", tests, RTX_NAMES["classic"])
    if A is None:
        return
    S = lambda n: os.path.join(scenes, n + ".blend")  # noqa: E731
    shot = {"BGE_REMIX_P2_SHOT_FRAME": "300"}
    gl = p8.play(args.ours, out, "p10a_classic_opengl", S("p10a_classic"), shot, frames=p8.REMIX_FRAMES)
    rx = p8.play(args.ours, out, "p10a_classic_remix", S("p10a_classic_remix"), r9.TEXTURE_ENV,
                 frames=p8.REMIX_FRAMES, seconds=r9.TEXTURE_SECONDS, debug=True)
    rows = r9.sphere_compare({"spheres": {"order": A["order"], "rects": A["rects"]}}, gl.get("screenshot"),
                             rx.get("screenshot"))
    c = {"the Classic tiles ran in OpenGL and in Remix": bool(gl["ran"] and rx["ran"]),
         "every texture in Remix before the picture": r9.textures_in(rx)}
    for label in A["order"]:
        row = rows.get(label)
        if A["tiles"][label]["face"]:
            continue  # (a face image: reported, not gated; P8 made it so)
        c["%s (Diffuse Intensity %.1f): Remix within %.0f of the game (%s)" % (
            label, A["tiles"][label]["ref"], r9.TOL_GATE,
            "-" if not row else "%s -> %s, %.1f" % (row["opengl"], row["remix"], row["mean_diff"]))] = \
            bool(row) and row["mean_diff"] <= r9.TOL_GATE
    tests[RTX_NAMES["classic"]] = {"ok": all(c.values()), "checks": c, "detail": rows}
    sheets["p10a_classic"] = p2.contact_sheet([("Classic tiles, game", gl.get("screenshot")),
                                               ("Classic tiles, RTX Remix", rx.get("screenshot"))],
                                              os.path.join(out, "sheet_p10a_classic.png"))
    report += ["", "Classic tiles, mean colour game -> Remix (P10.3: the colour image times Diffuse Intensity):"]
    for label in A["order"]:
        row = rows.get(label)
        report.append("  %-8s %s" % (label, "-" if not row else "%s -> %s  diff %.1f" % (
            row["opengl"], row["remix"], row["mean_diff"])))


def s_encode(args, out, scenes, E, tests, sheets, report):
    import run_p8 as p8
    import run_p9 as r9
    A = _scene_ok(E, "encode", tests, RTX_NAMES["encode"])
    if A is None:
        return
    c, d = {}, {}
    for size in ("1024", "2048"):
        cache = os.path.join(out, "cache_p10a_encode" + size)
        shutil.rmtree(cache, ignore_errors=True)
        env = dict(r9.TEXTURE_ENV, BGE_REMIX_CACHE_DIR=cache)
        r = p8.play(args.ours, out, "p10a_encode" + size, os.path.join(scenes, A["files"][size] + ".blend"), env,
                    frames=p8.REMIX_FRAMES, seconds=r9.TEXTURE_SECONDS)
        st = r.get("stats") or {}
        n = st.get("textures_encoded") or 0
        secs = st.get("encode_seconds") or 0.0
        d[size] = {"encoded": n, "seconds": secs, "per_texture": round(secs / n, 3) if n else None,
                   "first_textured_seconds": st.get("first_textured_seconds"), "bake_ms": st.get("procedural_bake_ms")}
        c["the %s px preset ran in Remix, its textures encoded (%s)" % (size, n)] = bool(r["ran"]) and n > 0 and \
            r9.textures_in(r)
    tests[RTX_NAMES["encode"]] = {"ok": all(c.values()), "checks": c, "detail": d}
    report += ["", "P10.2's open question, a Brick preset's bake encoded with Remix running (albedo, roughness, normal, "
               "height; the bridge's one encoder thread):"]
    for size in ("1024", "2048"):
        x = d.get(size) or {}
        report.append("  %s px: %s textures, %s s in all, %s s each; textured %s s after Remix's first frame" % (
            size, x.get("encoded"), x.get("seconds"), x.get("per_texture"), x.get("first_textured_seconds")))


def rtx(args, out, scenes, E, tests, sheets, report):
    """the RTX run (the real RTX Remix runtime)"""
    for fn in (s_tiles, s_classic, s_encode):
        try:
            fn(args, out, scenes, E, tests, sheets, report)
        except Exception:
            import traceback
            tests["p10a: %s ran" % fn.__name__] = {"ok": False, "checks": {"ran without an error": False},
                                                    "detail": traceback.format_exc()}
    # (the catalogue: built here once for the apply probe's fits)
    if not os.path.isdir(catalogue_dir(out)) or not os.listdir(catalogue_dir(out)):
        A = (E.get("p10a") or {}).get("fit")
        if A:
            env = {"BGE_REMIX_REMASTER_CATALOGUE": catalogue_dir(out), "BGE_REMIX_P10A_FIT": json.dumps(A)}
            probe(args, out, "p10a_catalogue_rtx", os.path.join(scenes, A["file"] + ".blend"), "catalogue", env)
    t_apply(args, out, scenes, E, tests, sheets, rtx=True, report=report)
