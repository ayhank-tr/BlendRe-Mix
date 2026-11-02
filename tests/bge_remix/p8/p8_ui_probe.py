# BGE-Remix P8 probe: runs inside Blender's UI (Python 3.5), then quits.
#   blender -p 0 0 1600 900 <scene.blend> --python p8_ui_probe.py
#
#   BGE_REMIX_P8_VIEW=<png>   T8.5: the camera view in Textured shading, as
#                             the 3D view draws it (an OpenGL render of the
#                             view, without overlays)
#   BGE_REMIX_P8_GAME=1       T8.8: plays the scene once in the 3D view
#                             (embedded), through RTX Remix
#   BGE_REMIX_P8_LAMPS=<dir>  T8.12: the startup scene's lamp set to each
#                             type in turn, in Material and Textured shading;
#                             the whole window redrawn and saved each time
#   BGE_REMIX_P8_SCREENS=<dir>  T8.31: each of the screens named in
#                             BGE_REMIX_P8_SCREEN_LIST (comma separated) shown
#                             and saved whole, its areas listed; then the
#                             Default screen's 3D view in edit mode and in
#                             wireframe
#   BGE_REMIX_P8_SELECT=<json>  T8.32: clicks on select_pick's boxes (camera
#                             view), a box selection, clicks on a grid's
#                             vertices in edit mode; what each selected
#   BGE_REMIX_P8_BGL=<dir>    T8.33: a Python draw handler drawing with bgl in
#                             the 3D view; the window saved, the queries' answers
#   BGE_REMIX_P8_BENCH=<json> B8.15: a 1,000,000-face grid drawn in the 3D view
#                             (solid, wireframe, edit mode) and the whole
#                             window; milliseconds per redraw
#   BGE_REMIX_P8_DONE=<json>  what happened

import json
import os
import time
import traceback

import bpy

VIEW = os.environ.get("BGE_REMIX_P8_VIEW")
GAME = os.environ.get("BGE_REMIX_P8_GAME")
LAMPS = os.environ.get("BGE_REMIX_P8_LAMPS")
DONE = os.environ.get("BGE_REMIX_P8_DONE", "p8_ui.json")
SCREENS = os.environ.get("BGE_REMIX_P8_SCREENS")
SCREEN_LIST = [n for n in os.environ.get("BGE_REMIX_P8_SCREEN_LIST", "Default").split(",") if n]
SELECT = os.environ.get("BGE_REMIX_P8_SELECT")
BGL = os.environ.get("BGE_REMIX_P8_BGL")
BENCH = os.environ.get("BGE_REMIX_P8_BENCH")

result = {"errors": [], "finished": False}
state = {"ticks": 0}


def view3d():
    win = bpy.context.window_manager.windows[0]
    areas = [a for a in win.screen.areas if a.type == 'VIEW_3D']
    if not areas:
        return None
    area = max(areas, key=lambda a: a.width * a.height)
    region = [r for r in area.regions if r.type == 'WINDOW'][0]
    return {"window": win, "screen": win.screen, "area": area, "region": region, "scene": bpy.context.scene}


def step(name, fn):
    try:
        result[name] = fn()
    except Exception:
        result["errors"].append("%s: %s" % (name, traceback.format_exc()))


def render_view():
    o = view3d()
    space = o["area"].spaces.active
    space.viewport_shade = 'TEXTURED'
    space.show_only_render = True
    space.show_world = True
    space.region_3d.view_perspective = 'CAMERA'
    o["area"].tag_redraw()
    bpy.ops.render.opengl(o, view_context=True)
    bpy.data.images["Render Result"].save_render(VIEW)
    return {"path": VIEW, "written": os.path.isfile(VIEW), "shade": space.viewport_shade,
            "material_mode": bpy.context.scene.game_settings.material_mode}


def region_rect(r):
    return {"x": r.x, "y": r.y, "width": r.width, "height": r.height}


def lamp_types():
    """each lamp type after a shadowless hemi, so every picture differs from
    the one before it; the window's own drawing (every editor), as a person
    sees it after changing the type in the Lamp panel"""
    o = view3d()
    win = o["window"]
    space = o["area"].spaces.active
    lamp = bpy.data.objects.get("Lamp") or [ob for ob in bpy.data.objects if ob.type == 'LAMP'][0]
    props = [a for a in win.screen.areas if a.type == 'PROPERTIES']
    # the startup lamp's shadow (a hemi lamp, the last type below, has none)
    shadow = getattr(lamp.data, "shadow_method", None)
    shots = []
    order = ["HEMI", "POINT", "HEMI", "SUN", "HEMI", "AREA", "HEMI", "SPOT", "HEMI"]
    for shade in ("MATERIAL", "TEXTURED"):
        space.viewport_shade = shade
        for i, kind in enumerate(order):
            lamp.data.type = kind
            for a in win.screen.areas:
                a.tag_redraw()
            bpy.ops.wm.redraw_timer(type='DRAW_WIN_SWAP', iterations=2)
            path = os.path.join(LAMPS, "%s_%d_%s.png" % (shade.lower(), i, kind.lower()))
            bpy.ops.screen.screenshot({"window": win, "screen": win.screen, "area": o["area"],
                                       "region": o["region"]}, filepath=path, full=True)
            shots.append({"shade": shade, "type": kind, "path": path, "written": os.path.isfile(path)})
    return {"shots": shots, "window": {"width": win.width, "height": win.height},
            "view": region_rect(o["region"]),
            "properties": region_rect([r for r in props[0].regions if r.type == 'WINDOW'][0]) if props else None,
            "lamp": lamp.name, "shadow": shadow}


def play():
    o = view3d()
    bpy.context.scene["bge_remix"] = 1
    print("P8 probe: starting the game", flush=True)
    bpy.ops.view3d.game_start(o)
    print("P8 probe: the game returned to Blender", flush=True)
    return {"played": True}


# ---- Part C steps 7-8: steps over several timer ticks (a screen change, a
# view change, take effect on the next ticks) ----

QUEUE = []   # (ticks to wait first, function)


def redraw_all(win):
    for a in win.screen.areas:
        a.tag_redraw()
    bpy.ops.wm.redraw_timer(type='DRAW_WIN_SWAP', iterations=2)


def area_list(win):
    """every area and its main region, in the saved picture's pixels
    (top-left origin)"""
    h = win.height
    res = []
    for a in win.screen.areas:
        rs = [r for r in a.regions if r.type == 'WINDOW']
        r = rs[0] if rs else None
        res.append({"type": a.type, "x": a.x, "y": h - (a.y + a.height), "width": a.width, "height": a.height,
                    "region": ({"x": r.x, "y": h - (r.y + r.height), "width": r.width, "height": r.height}
                               if r else None)})
    return res


def shot(win, path):
    o = view3d() or {}
    ctx = {"window": win, "screen": win.screen}
    area = [a for a in win.screen.areas][0]
    ctx["area"] = o.get("area", area)
    ctx["region"] = o.get("region", [r for r in area.regions][0])
    bpy.ops.screen.screenshot(ctx, filepath=path, full=True)
    return os.path.isfile(path)


def queue_screens():
    win = bpy.context.window_manager.windows[0]
    result["screens"] = {"shots": [], "window": {"width": win.width, "height": win.height}}
    for name in SCREEN_LIST:
        sc = bpy.data.screens.get(name)
        if sc is None:
            result["errors"].append("screen %s: not in the file" % name)
            continue

        def show(sc=sc):
            win.screen = sc

        def take(name=name):
            redraw_all(win)
            path = os.path.join(SCREENS, name.replace(" ", "_") + ".png")
            ok = shot(win, path)
            result["screens"]["shots"].append({"screen": name, "path": path, "written": ok,
                                               "areas": area_list(win), "shown": win.screen.name})
        QUEUE.append((0, show))
        QUEUE.append((3, take))

    def edit_setup():
        win.screen = bpy.data.screens["Default"]

    def edit_mode():
        o = view3d()
        cube = bpy.data.objects.get("Cube")
        if cube is None:
            return
        o["scene"].objects.active = cube
        cube.select = True
        bpy.ops.object.mode_set(o, mode='EDIT')
        bpy.ops.mesh.select_all(o, action='SELECT')

    def take_named(tag):
        def take():
            redraw_all(win)
            path = os.path.join(SCREENS, tag + ".png")
            ok = shot(win, path)
            result["screens"]["shots"].append({"screen": tag, "path": path, "written": ok,
                                               "areas": area_list(win), "shown": win.screen.name})
        return take

    def wire():
        o = view3d()
        bpy.ops.object.mode_set(o, mode='OBJECT')
        o["area"].spaces.active.viewport_shade = 'WIREFRAME'

    def solid():
        o = view3d()
        o["area"].spaces.active.viewport_shade = 'SOLID'

    if "Default" in SCREEN_LIST:
        QUEUE.extend([(0, edit_setup), (3, edit_mode), (2, take_named("Default_edit")), (0, wire),
                      (2, take_named("Default_wire")), (0, solid)])


def camera_view():
    o = view3d()
    o["area"].spaces.active.region_3d.view_perspective = 'CAMERA'
    o["area"].spaces.active.viewport_shade = 'SOLID'
    o["area"].tag_redraw()


def region_point(o, co):
    from bpy_extras import view3d_utils
    rv3d = o["area"].spaces.active.region_3d
    p = view3d_utils.location_3d_to_region_2d(o["region"], rv3d, co)
    return None if p is None else [int(round(p[0])), int(round(p[1]))]


def selected_names(scene):
    return sorted(ob.name for ob in scene.objects if ob.select)


def select_clicks():
    o = view3d()
    scene = o["scene"]
    res = {"clicks": [], "region": region_rect(o["region"])}
    names = sorted(ob.name for ob in scene.objects if ob.name.startswith("SP_") and ob.type == 'MESH'
                   and ob.name != "SP_grid")
    for name in names + ["SP_grid"]:
        ob = scene.objects[name]
        pt = region_point(o, ob.matrix_world.translation)
        if name == "SP_back":
            # beside the front box (the back one shows there)
            pt = region_point(o, ob.matrix_world.translation + __import__("mathutils").Vector((-0.9, 0.0, 0.0)))
        bpy.ops.object.select_all(o, action='DESELECT')
        if pt is not None:
            bpy.ops.view3d.select(o, location=tuple(pt), extend=False)
        res["clicks"].append({"target": name, "at": pt, "selected": selected_names(scene),
                              "active": scene.objects.active.name if scene.objects.active else None})
    # an empty spot: nothing
    bpy.ops.object.select_all(o, action='DESELECT')
    empty = [o["region"].width // 2, 8]
    bpy.ops.view3d.select(o, location=tuple(empty), extend=False, deselect=True)
    res["empty"] = {"at": empty, "selected": selected_names(scene)}
    # a box around the top row
    pts = [region_point(o, scene.objects["SP_0_%d" % c].matrix_world.translation) for c in range(4)]
    if all(pts):
        x0 = min(p[0] for p in pts) - 30
        x1 = max(p[0] for p in pts) + 30
        y0 = min(p[1] for p in pts) - 25
        y1 = max(p[1] for p in pts) + 25
        bpy.ops.object.select_all(o, action='DESELECT')
        bpy.ops.view3d.select_border(o, gesture_mode=3, xmin=x0, xmax=x1, ymin=y0, ymax=y1, extend=False)
        res["box"] = {"rect": [x0, y0, x1, y1], "selected": selected_names(scene)}
    result["select"] = res


def select_edit_setup():
    o = view3d()
    scene = o["scene"]
    grid = scene.objects["SP_grid"]
    bpy.ops.object.select_all(o, action='DESELECT')
    grid.select = True
    scene.objects.active = grid
    bpy.ops.object.mode_set(o, mode='EDIT')
    bpy.ops.mesh.select_all(o, action='DESELECT')


def select_edit_clicks():
    o = view3d()
    scene = o["scene"]
    grid = scene.objects["SP_grid"]
    clicks = []
    for k in (0, 5, 10, 15):
        bpy.ops.object.mode_set(o, mode='OBJECT')
        co = grid.matrix_world * grid.data.vertices[k].co
        bpy.ops.object.mode_set(o, mode='EDIT')
        bpy.ops.mesh.select_all(o, action='DESELECT')
        pt = region_point(o, co)
        if pt is not None:
            bpy.ops.view3d.select(o, location=tuple(pt), extend=False)
        bpy.ops.object.mode_set(o, mode='OBJECT')
        chosen = [v.index for v in grid.data.vertices if v.select]
        bpy.ops.object.mode_set(o, mode='EDIT')
        clicks.append({"vertex": k, "at": pt, "selected": chosen})
    bpy.ops.object.mode_set(o, mode='OBJECT')
    result["select"]["edit"] = clicks
    with open(SELECT, "w") as f:
        json.dump(result["select"], f, indent=1)


def queue_select():
    QUEUE.extend([(0, camera_view), (3, select_clicks), (0, select_edit_setup), (3, select_edit_clicks)])


BGL_STATE = {"queries": None}


def bgl_handler():
    import bgl
    import math
    vp = bgl.Buffer(bgl.GL_INT, 4)
    bgl.glGetIntegerv(bgl.GL_VIEWPORT, vp)
    mv = bgl.Buffer(bgl.GL_FLOAT, 16)
    bgl.glGetFloatv(bgl.GL_MODELVIEW_MATRIX, mv)
    blend_before = bgl.glIsEnabled(bgl.GL_BLEND)
    bgl.glEnable(bgl.GL_BLEND)
    bgl.glBlendFunc(bgl.GL_SRC_ALPHA, bgl.GL_ONE_MINUS_SRC_ALPHA)
    bgl.glColor4f(0.1, 0.4, 0.9, 0.6)
    bgl.glRectf(40, 40, 300, 200)
    bgl.glLineWidth(3.0)
    bgl.glColor4f(1.0, 1.0, 0.0, 1.0)
    bgl.glBegin(bgl.GL_LINES)
    for i in range(6):
        bgl.glVertex2f(340.5, 40.5 + i * 30)
        bgl.glVertex2f(600.5, 60.5 + i * 30)
    bgl.glEnd()
    bgl.glLineWidth(1.0)
    bgl.glEnable(bgl.GL_LINE_STIPPLE)
    bgl.glLineStipple(2, 0xAAAA)
    bgl.glColor4f(1.0, 1.0, 1.0, 1.0)
    bgl.glBegin(bgl.GL_LINE_LOOP)
    for x, y in ((630.5, 40.5), (830.5, 40.5), (830.5, 200.5), (630.5, 200.5)):
        bgl.glVertex2f(x, y)
    bgl.glEnd()
    bgl.glDisable(bgl.GL_LINE_STIPPLE)
    bgl.glBegin(bgl.GL_TRIANGLE_FAN)
    bgl.glColor4f(1.0, 1.0, 1.0, 1.0)
    bgl.glVertex2f(730.0, 120.0)
    for i in range(9):
        a = i * 6.2831853 / 8.0
        bgl.glColor4f(0.5 + 0.5 * math.cos(a), 0.5 + 0.5 * math.sin(a), 0.3, 1.0)
        bgl.glVertex2f(730.0 + 60.0 * math.cos(a), 120.0 + 60.0 * math.sin(a))
    bgl.glEnd()
    bgl.glPointSize(6.0)
    bgl.glBegin(bgl.GL_POINTS)
    for i in range(10):
        bgl.glColor4f(i / 9.0, 1.0 - i / 9.0, 0.5, 1.0)
        bgl.glVertex2f(60.5 + i * 24, 260.5)
    bgl.glEnd()
    bgl.glPointSize(1.0)
    px = bgl.Buffer(bgl.GL_FLOAT, [32 * 32 * 4])
    for y in range(32):
        for x in range(32):
            k = (y * 32 + x) * 4
            px[k] = x / 31.0
            px[k + 1] = y / 31.0
            px[k + 2] = 1.0 if ((x // 4 + y // 4) % 2) else 0.2
            px[k + 3] = 1.0
    bgl.glRasterPos2f(360.0, 260.0)
    bgl.glPixelZoom(2.0, 2.0)
    bgl.glDrawPixels(32, 32, bgl.GL_RGBA, bgl.GL_FLOAT, px)
    bgl.glPixelZoom(1.0, 1.0)
    bgl.glPushMatrix()
    bgl.glTranslatef(480.0, 260.0, 0.0)
    bgl.glRotatef(20.0, 0.0, 0.0, 1.0)
    bgl.glColor4f(0.9, 0.3, 0.1, 1.0)
    bgl.glBegin(bgl.GL_QUADS)
    for x, y in ((0, 0), (60, 0), (60, 60), (0, 60)):
        bgl.glVertex2f(x, y)
    bgl.glEnd()
    bgl.glPopMatrix()
    bgl.glDisable(bgl.GL_BLEND)
    BGL_STATE["queries"] = {"viewport": list(vp), "modelview": [round(v, 4) for v in mv],
                            "blend_was_on": bool(blend_before)}


def bgl_add():
    o = view3d()
    o["area"].spaces.active.viewport_shade = 'SOLID'
    BGL_STATE["handle"] = bpy.types.SpaceView3D.draw_handler_add(bgl_handler, (), 'WINDOW', 'POST_PIXEL')
    o["area"].tag_redraw()


def bgl_take():
    win = bpy.context.window_manager.windows[0]
    redraw_all(win)
    path = os.path.join(BGL, "bgl_editor.png")
    ok = shot(win, path)
    o = view3d()
    h = win.height
    r = o["region"]
    result["bgl"] = {"path": path, "written": ok, "queries": BGL_STATE["queries"],
                     "region": {"x": r.x, "y": h - (r.y + r.height), "width": r.width, "height": r.height}}
    bpy.types.SpaceView3D.draw_handler_remove(BGL_STATE["handle"], 'WINDOW')


def queue_bgl():
    QUEUE.extend([(0, bgl_add), (3, bgl_take)])


BENCH_STATE = {}


def bench_setup():
    o = view3d()
    scene = o["scene"]
    for ob in list(scene.objects):
        ob.select = False
    bpy.ops.mesh.primitive_grid_add(o, x_subdivisions=1001, y_subdivisions=1001, radius=4.0)
    grid = bpy.context.scene.objects.active
    BENCH_STATE["grid"] = grid.name
    BENCH_STATE["faces"] = len(grid.data.polygons)
    o["area"].spaces.active.viewport_shade = 'SOLID'


def draw_stats():
    """what the drawing sent since the last call (BGE-Remix only; None in
    stock 2.79)"""
    try:
        import gpu
    except ImportError:
        return None
    f = getattr(gpu, "draw_stats", None)
    return f(reset=True) if f else None


def bench_time(o, kind, iterations, stats=None, key=None):
    draw_stats()
    t0 = time.perf_counter()
    if kind == 'DRAW_WIN_SWAP':
        bpy.ops.wm.redraw_timer(type=kind, iterations=iterations)
    else:
        bpy.ops.wm.redraw_timer(o, type=kind, iterations=iterations)
    ms = round((time.perf_counter() - t0) * 1000.0 / iterations, 3)
    s = draw_stats()
    if s is not None and stats is not None:
        # per redraw; bytes as megabytes
        stats[key] = {k.replace("bytes", "mb"): round(v / iterations / (1048576.0 if "bytes" in k else 1.0), 2)
                      for k, v in s.items()}
    return ms


def bench_run():
    o = view3d()
    space = o["area"].spaces.active
    r = {"faces": BENCH_STATE.get("faces")}
    stats = {}
    bench_time(o, 'DRAW_SWAP', 2)
    r["solid_ms"] = bench_time(o, 'DRAW_SWAP', 10, stats, "solid")
    space.viewport_shade = 'WIREFRAME'
    bench_time(o, 'DRAW_SWAP', 2)
    r["wire_ms"] = bench_time(o, 'DRAW_SWAP', 10, stats, "wire")
    space.viewport_shade = 'SOLID'
    t0 = time.perf_counter()
    bpy.ops.object.mode_set(o, mode='EDIT')
    r["enter_edit_s"] = round(time.perf_counter() - t0, 2)
    bench_time(o, 'DRAW_SWAP', 2)
    r["edit_ms"] = bench_time(o, 'DRAW_SWAP', 5, stats, "edit")
    bpy.ops.object.mode_set(o, mode='OBJECT')
    r["window_ms"] = bench_time(o, 'DRAW_WIN_SWAP', 10, stats, "window")
    if stats:
        r["stats"] = stats
    result["bench"] = r
    with open(BENCH, "w") as f:
        json.dump(r, f, indent=1)


def queue_bench():
    QUEUE.extend([(0, bench_setup), (3, bench_run)])


class P8Probe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p8_probe"
    bl_label = "BGE-Remix P8 probe"
    _timer = None

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        state["ticks"] += 1
        if state["ticks"] < 10:
            return {'PASS_THROUGH'}
        if not state.get("single_done"):
            state["single_done"] = True
            if VIEW:
                step("view", render_view)
            if GAME:
                step("game", play)
            if LAMPS:
                step("lamps", lamp_types)
        # Part C steps 7-8: steps over several ticks
        if not state.get("queued"):
            state["queued"] = True
            if SCREENS:
                step("queue_screens", queue_screens)
            if SELECT:
                step("queue_select", queue_select)
            if BGL:
                step("queue_bgl", queue_bgl)
            if BENCH:
                step("queue_bench", queue_bench)
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
        with open(DONE, "w") as f:
            json.dump(result, f, indent=2)
        context.user_preferences.view.use_quit_dialog = False
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


bpy.utils.register_class(P8Probe)
bpy.ops.wm.bge_remix_p8_probe('INVOKE_DEFAULT')
