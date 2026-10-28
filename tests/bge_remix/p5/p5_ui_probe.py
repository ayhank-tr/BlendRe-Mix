# BGE-Remix P5 UI probe: drives Blender's UI on p5_studio.blend (Python 3.5).
#   blender -p 0 0 1600 900 p5_studio.blend --python p5_ui_probe.py
#
# A list of steps, each an action on the 3D view or the scene, then a wait
# (Remix refines, Blender redraws), then a record: the RTX Remix engine's
# counters (_bge_remix.view_stats), the selection, the 3D view's position in
# the window and a screenshot of the window.
#
#   BGE_REMIX_P5_OUT     result JSON
#   BGE_REMIX_P5_DIR     where the screenshots go
#   BGE_REMIX_P5_WAIT    tenths of a second to wait after each step (default 30)
#   BGE_REMIX_P5_PARTS   which parts run: edit,select,ortho,split,game (default all)
#
# After "open" the probe waits until the 3D view shows a picture: the first
# time RTX Remix runs (or after its shader cache is gone) it compiles its ray
# tracing shaders for some seconds and draws black meanwhile.

import json
import os
import traceback

import bgl
import bpy
from bpy_extras import view3d_utils
from mathutils import Vector

try:
    import _bge_remix
except ImportError:
    _bge_remix = None

OUT = os.environ.get("BGE_REMIX_P5_OUT", "p5_ui.json")
DIR = os.environ.get("BGE_REMIX_P5_DIR", os.path.dirname(os.path.abspath(OUT)))
WAIT = int(os.environ.get("BGE_REMIX_P5_WAIT", "30"))
PARTS = set(os.environ.get("BGE_REMIX_P5_PARTS", "edit,select,ortho,split,game").split(","))

result = {"steps": [], "errors": [], "finished": False}
state = {"main_area": None}


def stats():
    if _bge_remix is None:
        return {}
    try:
        return json.loads(_bge_remix.view_stats())
    except Exception as ex:
        return {"error": repr(ex)}


def view3d_areas():
    win = bpy.context.window_manager.windows[0]
    return [a for a in win.screen.areas if a.type == 'VIEW_3D']


def main_area():
    if state["main_area"] is None or state["main_area"] not in view3d_areas():
        areas = view3d_areas()
        state["main_area"] = max(areas, key=lambda a: a.width * a.height) if areas else None
    return state["main_area"]


def override(area=None):
    win = bpy.context.window_manager.windows[0]
    area = area or main_area()
    region = [r for r in area.regions if r.type == 'WINDOW'][0]
    # only the place: the object and mode come from the scene (operators change them)
    return {"window": win, "screen": win.screen, "area": area, "region": region, "scene": bpy.context.scene}


def region_rect(area):
    r = [x for x in area.regions if x.type == 'WINDOW'][0]
    return [r.x, r.y, r.width, r.height]


def ob(name):
    return bpy.context.scene.objects.get(name)


def shade(mode, area=None):
    (area or main_area()).spaces.active.viewport_shade = mode


READY_STEPS = {"open"}
READY_TIMEOUT = 1800  # tenths of a second


def _watch_pixels():
    """after the 3D view has drawn: the brightest of 16 x 16 pixels in its
    middle (0..1), while a step waits for a picture"""
    if not state.get("watch"):
        return
    try:
        vp = bgl.Buffer(bgl.GL_INT, 4)
        bgl.glGetIntegerv(bgl.GL_VIEWPORT, vp)
        x, y, w, h = vp[0], vp[1], vp[2], vp[3]
        n = 16
        buf = bgl.Buffer(bgl.GL_FLOAT, n * n * 4)
        bgl.glReadPixels(x + w // 2 - n // 2, y + h // 2 - n // 2, n, n, bgl.GL_RGBA, bgl.GL_FLOAT, buf)
        values = buf.to_list()
        state["px_max"] = max(v for i, v in enumerate(values) if i % 4 != 3)
    except Exception as ex:
        state["px_max"] = None
        result["errors"].append("watching the pixels: %r" % ex)
        state["watch"] = False


def picture_ready():
    v = state.get("px_max")
    return v is not None and v > 0.05


def screenshot(name):
    path = os.path.join(DIR, "p5_" + name + ".png")
    try:
        bpy.ops.screen.screenshot(override(), filepath=path, full=True)
        return path
    except Exception as ex:
        result["errors"].append("screenshot %s: %r" % (name, ex))
        return None


def record(name, extra=None):
    areas = view3d_areas()
    entry = {
        "step": name,
        "stats": stats(),
        "selected": sorted(o.name for o in bpy.context.scene.objects if o.select),
        "objects": sorted(o.name for o in bpy.context.scene.objects),
        "active": bpy.context.scene.objects.active.name if bpy.context.scene.objects.active else None,
        "views": [{"region": region_rect(a), "area": [a.x, a.y, a.width, a.height],
                   "shade": a.spaces.active.viewport_shade, "main": a == main_area()} for a in areas],
        "window": [bpy.context.window_manager.windows[0].width, bpy.context.window_manager.windows[0].height],
        "screenshot": screenshot(name),
    }
    if extra:
        entry.update(extra)
    result["steps"].append(entry)
    print("P5 probe: %s %s" % (name, json.dumps(entry["stats"])[:300]), flush=True)


# ---------------------------------------------------------------------------
# Actions

def a_solid():
    shade('SOLID')
    main_area().spaces.active.region_3d.view_perspective = 'CAMERA'


def a_open():
    shade('RENDERED')


def a_move():
    ob("GlassCube").location.x += 1.0


def a_recolour():
    ma = bpy.data.materials["MonkeyRed"]
    for node in ma.node_tree.nodes:
        if node.type == 'BSDF_PRINCIPLED':
            node.inputs["Base Color"].default_value = (0.05, 0.15, 0.8, 1.0)


def a_edit():
    import bmesh
    ball = ob("CheckerBall")
    for o in bpy.context.scene.objects:
        o.select = False
    ball.select = True
    bpy.context.scene.objects.active = ball
    bpy.ops.object.mode_set(override(), mode='EDIT')
    me = ball.data
    bm = bmesh.from_edit_mesh(me)
    for v in bm.verts:
        if v.co.z > 0.4:
            v.co.x *= 1.6
            v.co.y *= 1.6
    bmesh.update_edit_mesh(me)


def a_edit_exit():
    bpy.ops.object.mode_set(override(), mode='OBJECT')


def a_subdivision():
    ob("Monkey").modifiers["Subsurf"].levels = 1


def a_lamp():
    ob("Lamp").data.energy *= 0.5


def a_add():
    # 'EXEC_DEFAULT', True: an undo step, as the menu's operator makes (from a
    # script Blender makes none)
    # a small cone at the front left, in the picture and clear of the objects
    # T5.2 clicks on (undo brings it back for those clicks); the same place as
    # ADDED_CONE in make_scenes_p5.py
    bpy.ops.mesh.primitive_cone_add(override(), 'EXEC_DEFAULT', True, radius1=0.3, depth=0.6,
                                    location=(-1.6, -2.5, 0.3))
    bpy.context.scene.objects.active.name = "AddedCone"
    bpy.ops.ed.undo_push(message="Rename the cone")


def a_delete():
    for o in bpy.context.scene.objects:
        o.select = o.name == "AddedCone"
    bpy.ops.object.delete(override(), 'EXEC_DEFAULT', True)


def a_undo():
    bpy.ops.ed.undo(override())


def click(name):
    def act():
        # Blender draws lamps (and their lines to the ground) for selection
        # too; they must not be in the way of the clicks
        for o in bpy.context.scene.objects:
            if o.type == 'LAMP':
                o.hide_select = True
        area = main_area()
        region = [r for r in area.regions if r.type == 'WINDOW'][0]
        rv3d = area.spaces.active.region_3d
        o = ob(name)
        if name == "Floor":
            co = Vector((-1.0, -1.0, 0.0))
        else:
            co = o.matrix_world.translation.copy()
            if name.startswith("Pillar"):
                co.z += 0.75
        xy = view3d_utils.location_3d_to_region_2d(region, rv3d, co)
        state["click"] = [int(xy.x), int(xy.y)] if xy else None
        if xy:
            bpy.ops.view3d.select(override(), location=(int(xy.x), int(xy.y)), extend=False, deselect=False,
                                  toggle=False)
    return act


def a_ortho():
    main_area().spaces.active.region_3d.view_perspective = 'ORTHO'


def a_camera():
    main_area().spaces.active.region_3d.view_perspective = 'CAMERA'


def a_split():
    # the main view gets smaller (its picture must follow), and a second
    # Rendered view appears (it shows Solid and the note)
    bpy.ops.screen.area_split(override(), direction='VERTICAL', factor=0.6)
    for a in view3d_areas():
        a.spaces.active.viewport_shade = 'RENDERED'


# ---------------------------------------------------------------------------
# Benchmarks (RTX run): B5.1 orbit, B5.2 latency, B5.3 idle, B5.6 memory

def process_ram_mb():
    try:
        import ctypes
        from ctypes import wintypes

        class PMC(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                        ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]
        c = PMC()
        c.cb = ctypes.sizeof(c)
        h = ctypes.windll.kernel32.GetCurrentProcess()
        ctypes.windll.psapi.GetProcessMemoryInfo(h, ctypes.byref(c), c.cb)
        return c.PagefileUsage / (1024.0 * 1024.0)
    except Exception:
        return None


def vram_mb():
    try:
        import subprocess
        p = subprocess.run(["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=20)
        return float(p.stdout.decode().strip().splitlines()[0])
    except Exception:
        return None


def swap(n=1):
    bpy.ops.wm.redraw_timer(override(), type='DRAW_WIN_SWAP', iterations=n)


def timed_change(fn):
    import time
    t0 = time.perf_counter()
    fn()
    bpy.context.scene.update()
    swap(1)
    return (time.perf_counter() - t0) * 1000.0


def orbit_view(frames=300):
    import math
    import time
    from mathutils import Euler, Quaternion
    rv3d = main_area().spaces.active.region_3d
    rv3d.view_perspective = 'PERSP'
    rv3d.view_location = (0.0, 0.0, 0.8)
    rv3d.view_distance = 8.5
    rv3d.view_rotation = Euler((math.radians(75.0), 0.0, 0.0)).to_quaternion()
    swap(30)  # Remix warms up
    times = []
    step = Quaternion((0.0, 0.0, 1.0), math.radians(1.2))
    for i in range(frames):
        rv3d.view_rotation = step * rv3d.view_rotation
        t0 = time.perf_counter()
        swap(1)
        times.append((time.perf_counter() - t0) * 1000.0)
    times = sorted(times[30:])
    return times[len(times) // 2] if times else None


def a_bench(full=True):
    import time
    def act():
        out = {}
        bpy.ops.screen.screen_full_area(override())
        state["main_area"] = None
        shade('RENDERED')
        swap(10)
        region = [r for r in main_area().regions if r.type == 'WINDOW'][0]
        out["width"], out["height"] = region.width, region.height
        out["orbit_ms_median"] = orbit_view()
        if full:
            main_area().spaces.active.region_3d.view_perspective = 'CAMERA'
            swap(30)
            out["move_ms"] = timed_change(lambda: setattr(ob("GlassCube").location, "x", ob("GlassCube").location.x + 0.5))
            ma = bpy.data.materials["MonkeyRed"]
            p = [n for n in ma.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'][0]
            out["colour_ms"] = timed_change(lambda: p.inputs["Base Color"].__setattr__("default_value", (0.1, 0.6, 0.2, 1.0)))

            def edit_ball():
                me = ob("CheckerBall").data
                for v in me.vertices:
                    if v.co.z > 0.4:
                        v.co.x *= 1.2
                me.update()
            out["edit_ms"] = timed_change(edit_ball)
            out["subdivision_ms"] = timed_change(lambda: setattr(ob("Monkey").modifiers["Subsurf"], "levels", 3))
            # B5.3: refined, then nothing for 5 s
            swap(90)
            time.sleep(1.5)
            swap(1)
            f0 = (stats().get("bridge") or {}).get("frames")
            state["idle_from"] = f0
            out["idle_start_frames"] = f0
            # B5.6: Rendered off and on 20 times; the memory after each time
            # tells a leak (growing all along) from pools that fill once
            ram0, vram0 = process_ram_mb(), vram_mb()
            out["vram_each_mb"] = []
            out["ram_each_mb"] = []
            for i in range(20):
                shade('SOLID')
                swap(3)
                shade('RENDERED')
                swap(10)
                v, r = vram_mb(), process_ram_mb()
                out["vram_each_mb"].append(None if v is None or vram0 is None else round(v - vram0, 1))
                out["ram_each_mb"].append(None if r is None or ram0 is None else round(r - ram0, 1))
            shade('SOLID')
            swap(3)
            shade('RENDERED')
            swap(30)
            ram1, vram1 = process_ram_mb(), vram_mb()
            out["ram_growth_mb"] = None if ram0 is None or ram1 is None else round(ram1 - ram0, 1)
            out["vram_growth_mb"] = None if vram0 is None or vram1 is None else round(vram1 - vram0, 1)
            # refine, then the idle count starts (the step's wait does not redraw)
            swap(90)
            time.sleep(1.5)
            swap(1)
            out["idle_start_frames"] = (stats().get("bridge") or {}).get("frames")
        state["bench"] = out
    return act


def a_idle_start():
    """B5.3 starts here: the bench step's own wait (which asks for redraws, 10
    a second) is over; from now on nothing asks the view to draw"""
    b = state.get("bench")
    if b is not None:
        b["idle_start_frames"] = (stats().get("bridge") or {}).get("frames")


def a_game():
    o = override()
    print("P5 probe: starting the game", flush=True)
    bpy.ops.view3d.game_start(o)
    print("P5 probe: the game returned to Blender", flush=True)


STEPS = [("solid", a_solid), ("open", a_open)]
if "edit" in PARTS:
    STEPS += [("move", a_move), ("recolour", a_recolour), ("edit", a_edit), ("edit_exit", a_edit_exit),
              ("subdivision", a_subdivision), ("lamp", a_lamp), ("add", a_add), ("delete", a_delete),
              ("undo", a_undo)]
if "select" in PARTS:
    STEPS += [("select_" + n, click(n)) for n in ("Monkey", "GlassCube", "CheckerBall", "Marker", "Pillar.2",
                                                     "Floor")]
if "game" in PARTS:
    STEPS += [("before_game", lambda: None), ("game", a_game), ("after_game", lambda: None)]
if "ortho" in PARTS:
    STEPS += [("ortho", a_ortho), ("camera", a_camera)]
if "split" in PARTS:
    STEPS += [("split", a_split)]
if "bench" in PARTS:
    STEPS = [("solid", a_solid), ("bench", a_bench(True)), ("idle", a_idle_start)]
if "bench_view" in PARTS:
    STEPS = [("solid", a_solid), ("bench", a_bench(False))]
QUIET = {"idle"}  # steps whose wait does not ask for redraws (B5.3)


class P5UIProbe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p5_ui_probe"
    bl_label = "BGE-Remix P5 UI probe"
    _timer = None

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        state["ticks"] = state.get("ticks", 0) + 1
        if state["ticks"] < 10:
            return {'PASS_THROUGH'}
        i = state.get("i", 0)
        if i < len(STEPS):
            phase = state.get("phase", "act")
            name, fn = STEPS[i]
            if phase == "act":
                try:
                    fn()
                except Exception:
                    result["errors"].append("%s: %s" % (name, traceback.format_exc()))
                state["phase"] = "wait"
                state["until"] = state["ticks"] + (50 if name in QUIET else WAIT)
                if name in READY_STEPS:
                    state["watch"] = True
                    state["px_max"] = None
                    state["ready_from"] = state["ticks"]
                # Blender redraws the views; Remix refines meanwhile
                if name not in QUIET:
                    for a in view3d_areas():
                        a.tag_redraw()
            elif state["ticks"] >= state["until"] and state.get("watch") and not picture_ready() and \
                    state["ticks"] - state["ready_from"] < READY_TIMEOUT:
                # still black: Remix is getting ready
                for a in view3d_areas():
                    a.tag_redraw()
            elif state["ticks"] >= state["until"]:
                extra = {"click": state.pop("click", None)} if name.startswith("select_") else None
                if state.get("watch"):
                    state["watch"] = False
                    ready = picture_ready()
                    extra = dict(extra or {}, ready=ready,
                                 ready_seconds=round((state["ticks"] - state["ready_from"]) / 10.0, 1))
                    print("P5 probe: the view %s after %.1f s" % ("shows a picture" if ready else "is still black",
                                                                  extra["ready_seconds"]), flush=True)
                if name == "bench":
                    extra = {"bench": state.get("bench")}
                if name == "idle":
                    b = state.get("bench") or {}
                    f1 = (stats().get("bridge") or {}).get("frames")
                    f0 = b.get("idle_start_frames")
                    b["idle_frames"] = None if f0 is None or f1 is None else f1 - f0
                    extra = {"bench": b}
                try:
                    record(name, extra)
                except Exception:
                    result["errors"].append("record %s: %s" % (name, traceback.format_exc()))
                state["i"] = i + 1
                state["phase"] = "act"
            elif name not in QUIET:
                for a in view3d_areas():
                    a.tag_redraw()
            return {'PASS_THROUGH'}
        context.window_manager.event_timer_remove(self._timer)
        result["finished"] = True
        with open(OUT, "w") as f:
            json.dump(result, f, indent=2)
        context.user_preferences.view.use_quit_dialog = False
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


bpy.utils.register_class(P5UIProbe)
bpy.types.SpaceView3D.draw_handler_add(_watch_pixels, (), 'WINDOW', 'POST_PIXEL')
try:
    bpy.ops.wm.bge_remix_p5_ui_probe('INVOKE_DEFAULT')
except Exception:
    bpy.ops.wm.bge_remix_p5_ui_probe(override(), 'INVOKE_DEFAULT')
