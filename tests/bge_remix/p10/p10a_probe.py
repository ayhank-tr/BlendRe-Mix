# BGE-Remix p10a probe: drives the texture remaster in Blender's UI (it needs
# Blender's OpenGL context for its bakes), then quits. Python 3.5.
#   blender -p 0 0 1600 900 <file> --python p10a_probe.py
#
# Parts (BGE_REMIX_P10A_PARTS, comma separated):
#   fit     (p10a_fit.blend) every preset's catalogue built
#           (BGE_REMIX_REMASTER_CATALOGUE); each preset baked at known
#           settings (BGE_REMIX_P10A_FIT, JSON) and fitted again; the sign
#           fitted (it must score low)
#   catalogue  the catalogue only (the RTX run's, for apply's fits)
#   apply   (the Apply scene) saved as BGE_REMIX_P10A_COPY first; the audit;
#           the walls fitted and accepted; the preview square shown and
#           closed by a save; Apply (materials, .p8 copies, every texture
#           baked and encoded); two saves (the second removes the copies)
#   views   (the RTX PC, with apply) Debug mode's picture before and with
#           the preview square (BGE_REMIX_P10A_DIR)
# Results: BGE_REMIX_P10A_OUT (JSON).

import json
import os
import time
import traceback

import bpy

OUT = os.environ.get("BGE_REMIX_P10A_OUT", "p10a.json")
DIR = os.environ.get("BGE_REMIX_P10A_DIR", os.path.dirname(os.path.abspath(OUT)))
PARTS = os.environ.get("BGE_REMIX_P10A_PARTS", "fit").split(",")
COPY = os.environ.get("BGE_REMIX_P10A_COPY", "")
WAIT = int(os.environ.get("BGE_REMIX_P10A_WAIT", "30"))   # ticks (0.1 s) for Remix to draw
FIT = json.loads(os.environ.get("BGE_REMIX_P10A_FIT", "{}") or "{}")

result = {"steps": {}, "errors": [], "finished": False}
QUEUE = []


def T():
    from bge_remix import remaster_tool
    return remaster_tool


def window():
    return bpy.context.window_manager.windows[0]


def main_area():
    areas = [a for a in window().screen.areas if a.type in ('GAME_VIEW', 'VIEW_3D')]
    areas.sort(key=lambda a: (a.type != 'GAME_VIEW', -a.width * a.height))
    return areas[0] if areas else None


def override():
    win = window()
    area = main_area()
    region = [r for r in area.regions if r.type == 'WINDOW'][0] if area else None
    return {"window": win, "screen": win.screen, "area": area, "region": region, "scene": bpy.context.scene}


def screenshot(name):
    """the window's picture (cut to the main area by the test)"""
    path = os.path.join(DIR, name + ".png")
    try:
        win = window()
        bpy.ops.screen.screenshot({"window": win, "screen": win.screen}, filepath=path, full=True)
        a = main_area()
        return {"path": path if os.path.isfile(path) else None, "area": [a.x, a.y, a.width, a.height] if a else None}
    except Exception as ex:
        result["errors"].append("screenshot %s: %r" % (name, ex))
        return {"path": None}


# ---------------------------------------------------------------------------
# fit

def s_catalogue():
    t0 = time.time()
    counts = {}

    def progress(key, done, total):
        counts[key] = total
    missing = T().catalogues_missing()
    T().build_all_catalogues(progress)
    return {"built": missing, "entries": counts, "seconds": round(time.time() - t0, 1),
            "folder": T().catalogue_folder(), "missing_after": T().catalogues_missing()}


def _image_from_bytes(name, data, size):
    import numpy as np
    old = bpy.data.images.get(name)
    if old is not None:
        bpy.data.images.remove(old)
    ima = bpy.data.images.new(name, size, size, alpha=False)
    px = np.frombuffer(data, dtype=np.uint8).astype(np.float32) / 255.0
    ima.pixels = px.tolist()
    return ima


def s_fits():
    from bl_operators import bge_remix_presets as P
    tool = T()
    out = {}
    size = 512
    for key in P.ORDER:
        t0 = time.time()
        try:
            s = {"Scale": FIT.get("scale_brick", 2.0) if key in P.REGULAR else FIT["settings"]["Scale"],
                 "Wear": FIT["settings"]["Wear"], "Seed": FIT["settings"]["Seed"]}
            if P.PRESETS[key].get("wet"):
                s["Wetness"] = FIT["settings"]["Wetness"]
            colour = [min(1.0, c * t) for c, t in zip(P.PRESETS[key]["colour"], FIT["tint"])]
            with tool.Scratch():
                baker = tool.PresetBaker(key)
                data = baker.bake(s, size, colour)[0]
            ima = _image_from_bytes("P10F_test_" + key, data, size)
            fit = tool.fit_image(ima)
            out[key] = {"truth": {"settings": s, "colour": colour}, "fit": fit,
                        "seconds": round(time.time() - t0, 1)}
            bpy.data.images.remove(ima)
        except Exception:
            out[key] = {"error": traceback.format_exc()}
    return out


def s_sign():
    tool = T()
    ima = bpy.data.images.get(FIT.get("sign", "P10F_sign"))
    if ima is None:
        return {"error": "no sign image"}
    return {"fit": tool.fit_image(ima)}


# ---------------------------------------------------------------------------
# apply

def copies():
    from bl_operators import bge_remix_convert as C
    return {m.name: int(m.get(C.SAVES_KEY, -1)) for m in bpy.data.materials if m.name.endswith(C.P8_SUFFIX)}


def s_copy():
    if COPY:
        os.makedirs(os.path.dirname(COPY), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=COPY, check_existing=False, compress=False)
    return {"file": bpy.data.filepath}


def s_audit():
    a = T().remaster_audit()
    return {"plans": {r["image"]: r.get("plan") for r in a["images"]},
            "kinds": {r["image"]: r.get("kind") for r in a["images"]}}


def s_fit_walls():
    from bl_operators.bge_remix_remaster import store_fit
    tool = T()
    if tool.catalogues_missing():
        tool.build_all_catalogues()
    out = {}
    for row in tool.last_audit()["images"]:
        ima = bpy.data.images.get(row["image"])
        if ima is None or not row["image"].startswith("P10P_"):
            continue
        rm = ima.bge_remix_remaster
        rm.plan = row["plan"]
        if row["plan"] == 'FIT':
            fit = tool.fit_image(ima, row)
            store_fit(ima, fit)
            out[ima.name] = {"fit": fit}
        else:
            out[ima.name] = {"plan": row["plan"]}
        rm.decision = 'ACCEPT'
        out[ima.name]["plan_now"] = rm.plan
        out[ima.name]["decision"] = rm.decision
    return out


def _preview_state():
    tool = T()
    obs = [o for o in bpy.data.objects if o.get(tool.PREVIEW_KEY)]
    return {"objects": sorted(o.name for o in obs),
            "materials": {o.name: (o.active_material.name if o.active_material else None) for o in obs},
            "result_has_preset": any(o.active_material and o.active_material.node_tree and
                                     any(n.bl_idname == "ShaderNodeGroup" for n in o.active_material.node_tree.nodes)
                                     for o in obs if o.name.endswith("result")),
            "temp_materials": sorted(m.name for m in bpy.data.materials if m.get(tool.PREVIEW_KEY))}


def s_preview():
    tool = T()
    name = next((n for n in sorted(tool._state["fits"]) if n.startswith("P10P_")), "P10P_noise")
    bpy.context.window_manager.bge_remix_remaster_image = name
    ret = bpy.ops.image.bge_remix_remaster_preview(override(), image=name)
    st = _preview_state()
    st["ret"] = list(ret)
    st["image"] = name
    return st


def s_preview_shot():
    return screenshot("p10a_preview")


def s_debug():
    try:
        ret = list(bpy.ops.gameview.mode(override(), mode='DEBUG'))
    except Exception as ex:
        ret = ["error: %r" % ex]
    return {"ret": ret, "mode": main_area().spaces.active.game_view_mode if main_area() else None}


def s_debug_shot():
    return screenshot("p10a_debug_before")


def s_preview_saved():
    """a save closes the preview: nothing of it in the file"""
    bpy.ops.wm.save_mainfile()
    st = _preview_state()
    st["copies"] = copies()
    return st


def _material_state(ma):
    from bl_operators.bge_remix_material import kind, remix_node
    from bl_operators.bge_remix_presets import preset_node
    s = {"kind": kind(ma), "applied": ma.get("bge_remix_remaster"), "tile_metres": ma.get("bge_remix_tile_metres"),
         "tile_mode": ma.get("bge_remix_tile_mode"), "preset": ma.get("bge_remix_preset")}
    node = remix_node(ma)
    if node is not None:
        s["bake_size"] = node.bake_size
        s["wired"] = {}
        for link in ma.node_tree.links:
            if link.to_node == node:
                fn = link.from_node
                entry = [fn.bl_idname]
                if fn.bl_idname == "ShaderNodeTexture" and fn.texture is not None and \
                        getattr(fn.texture, "image", None) is not None:
                    ima = fn.texture.image
                    entry += [ima.name, bpy.path.abspath(ima.filepath), os.path.isfile(bpy.path.abspath(ima.filepath)),
                              bool(ima.get("bge_remix_remaster_approximate"))]
                s["wired"][link.to_socket.name] = entry
        s["displace_in"] = node.inputs["Displace In"].default_value
    g = preset_node(ma)
    if g is not None:
        s["group"] = g.node_tree.name
        s["inputs"] = {i.name: (list(i.default_value) if hasattr(i.default_value, "__len__") else i.default_value)
                       for i in g.inputs}
    return s


def s_apply():
    t0 = time.time()
    ret = bpy.ops.image.bge_remix_remaster_apply('EXEC_DEFAULT')
    tool = T()
    walls = {}
    for ima in bpy.data.images:
        if ima.name.startswith("P10P_") and "remaster" not in ima.name:
            mats = [m for m in bpy.data.materials if m.name == ima.name]
            if mats:
                walls[ima.name] = _material_state(mats[0])
    return {"ret": list(ret), "applied": tool._state.get("applied"), "walls": walls, "copies": copies(),
            "seconds": round(time.time() - t0, 1),
            "remaster_folder": tool.remaster_folder(),
            "remaster_files": sorted(os.listdir(tool.remaster_folder())) if os.path.isdir(tool.remaster_folder())
            else []}


def s_save1():
    bpy.ops.wm.save_mainfile()
    return {"copies": copies()}


def s_save2():
    bpy.ops.wm.save_mainfile()
    return {"copies": copies(), "file": bpy.data.filepath}


def queue():
    if "catalogue" in PARTS:
        QUEUE.append((0, s_catalogue))
    if "fit" in PARTS:
        QUEUE.extend([(0, s_catalogue), (0, s_fits), (0, s_sign)])
    if "apply" in PARTS:
        QUEUE.extend([(0, s_copy), (0, s_audit), (0, s_fit_walls)])
        if "views" in PARTS:
            QUEUE.extend([(0, s_debug), (WAIT, s_debug_shot)])
        QUEUE.append((0, s_preview))
        if "views" in PARTS:
            QUEUE.append((WAIT, s_preview_shot))
        QUEUE.extend([(2, s_preview_saved), (0, s_apply), (0, s_save1), (0, s_save2)])


def step(name, fn):
    print("P10A step", name, flush=True)
    try:
        result["steps"][name] = fn()
    except Exception:
        result["errors"].append("%s: %s" % (name, traceback.format_exc()))
    result["last_step"] = name
    try:
        with open(OUT, "w") as f:
            json.dump(result, f, indent=1, default=str)
    except (OSError, TypeError, ValueError):
        pass


class P10AProbe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p10a_probe"
    bl_label = "BGE-Remix p10a probe"
    _timer = None
    _ticks = 0

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        self._ticks += 1
        for a in window().screen.areas:
            a.tag_redraw()
        if self._ticks < 10:
            return {'PASS_THROUGH'}
        if not result.get("queued"):
            result["queued"] = True
            queue()
        if QUEUE:
            wait, fn = QUEUE[0]
            if wait > 0:
                QUEUE[0] = (wait - 1, fn)
                return {'PASS_THROUGH'}
            QUEUE.pop(0)
            step(fn.__name__, fn)
            return {'PASS_THROUGH'}
        result["finished"] = True
        context.window_manager.event_timer_remove(self._timer)
        with open(OUT, "w") as f:
            json.dump(result, f, indent=1, default=str)
        context.user_preferences.view.use_quit_dialog = False
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


bpy.utils.register_class(P10AProbe)
bpy.ops.wm.bge_remix_p10a_probe('INVOKE_DEFAULT')
