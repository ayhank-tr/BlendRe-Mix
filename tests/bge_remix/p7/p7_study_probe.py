# BGE-Remix P7.0 study probe: drives Blender's UI (Python 3.5).
#   blender -p 0 0 1600 900 <scene.blend> --python p7_study_probe.py
#
# Parts (BGE_REMIX_P7_PARTS, comma separated):
#   convert  the dry conversion (view3d.gameview_study_convert), repeated
#            BGE_REMIX_P7_REPEAT times; its report goes to BGE_REMIX_P7_REPORT
#   plain    study holograms over the Solid view (with the depth pass)
#   remix    study holograms over RTX Remix's picture (Rendered shading):
#            without the depth pass, then with it
# Results: BGE_REMIX_P7_OUT (JSON); screenshots in BGE_REMIX_P7_DIR.
#
# The study holograms: the avatars made from Blender's icons
# (study_avatars/*.obj, *_tex.png) floating above some objects, each in one
# state colour; drawn with bgl after the view, see-through, a ring under
# each. With the depth pass the objects' own shapes are drawn first into the
# depth buffer only, so a hologram behind an object is hidden.

import json
import math
import os
import time
import traceback

import bgl
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
AVATARS = os.path.join(HERE, "study_avatars")
OUT = os.environ.get("BGE_REMIX_P7_OUT", "p7_study.json")
DIR = os.environ.get("BGE_REMIX_P7_DIR", os.path.dirname(os.path.abspath(OUT)))
REPORT = os.environ.get("BGE_REMIX_P7_REPORT", os.path.join(DIR, "dry_convert.json"))
REPEAT = int(os.environ.get("BGE_REMIX_P7_REPEAT", "3"))
PARTS = set(os.environ.get("BGE_REMIX_P7_PARTS", "convert").split(","))
WAIT = int(os.environ.get("BGE_REMIX_P7_WAIT", "30"))

# object -> (avatar, state); only objects that exist get one
HOLOGRAMS = {
    "Cube": ("LOGIC", "green"),
    "Ball": ("SPEAKER", "blue"),
    "Receiver": ("LOGIC", "yellow"),
    "Camera": ("OUTLINER_OB_CAMERA", "white"),
    "Spot": ("LAMP_SPOT", "white"),
    "Title": ("FONT_DATA", "white"),
    "Rig": ("ACTION", "blue"),
    "GhostBox": ("PHYSICS", "red"),
    "Spawn": ("OUTLINER_OB_MESH", "black"),
    "Smoke": ("ERROR", "red"),
    "Floor": ("PHYSICS", "ghost"),
}
STATES = {
    "red": ((1.0, 0.12, 0.08), 1.0, 1.0),
    "black": ((0.85, 0.85, 0.9), 0.25, 1.0),
    "white": ((1.0, 1.0, 1.0), 1.0, 1.0),
    "green": ((0.1, 1.0, 0.3), 1.0, 1.0),
    "ghost": ((0.1, 1.0, 0.3), 1.0, 0.35),
    "blue": ((0.15, 0.45, 1.0), 1.0, 1.0),
    "yellow": ((1.0, 0.82, 0.08), 1.0, 1.0),
}

result = {"steps": [], "errors": [], "finished": False}
state = {"holo": False, "depth": False, "draw_ms": [], "main_area": None}


# ---------------------------------------------------------------------------
# Blender's UI

def view3d_areas():
    win = bpy.context.window_manager.windows[0]
    return [a for a in win.screen.areas if a.type == 'VIEW_3D']


def main_area():
    if state["main_area"] is None or state["main_area"] not in view3d_areas():
        areas = view3d_areas()
        state["main_area"] = max(areas, key=lambda a: a.width * a.height) if areas else None
    return state["main_area"]


def override():
    win = bpy.context.window_manager.windows[0]
    area = main_area()
    region = [r for r in area.regions if r.type == 'WINDOW'][0]
    return {"window": win, "screen": win.screen, "area": area, "region": region, "scene": bpy.context.scene}


def shade(mode):
    main_area().spaces.active.viewport_shade = mode


def screenshot(name):
    path = os.path.join(DIR, "p7_" + name + ".png")
    try:
        bpy.ops.screen.screenshot(override(), filepath=path, full=True)
        return path
    except Exception as ex:
        result["errors"].append("screenshot %s: %r" % (name, ex))
        return None


def process_mb():
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
        ctypes.windll.psapi.GetProcessMemoryInfo(ctypes.windll.kernel32.GetCurrentProcess(), ctypes.byref(c), c.cb)
        return c.PagefileUsage / (1024.0 * 1024.0)
    except Exception:
        return None


# ---------------------------------------------------------------------------
# Study holograms

def load_obj(name):
    v, vt, faces = [], [], []
    mat = None
    with open(os.path.join(AVATARS, name + ".obj")) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == "v":
                v.append(Vector([float(x) for x in p[1:4]]))
            elif p[0] == "vt":
                vt.append((float(p[1]), float(p[2])))
            elif p[0] == "usemtl":
                mat = p[1]
            elif p[0] == "f":
                idx = [x.split("/") for x in p[1:]]
                faces.append((mat, [int(i[0]) - 1 for i in idx],
                              [int(i[1]) - 1 for i in idx] if len(idx[0]) > 1 and idx[0][1] else None))
    return {"v": v, "vt": vt, "faces": faces}


def texture(name):
    img = bpy.data.images.load(os.path.join(AVATARS, name + "_tex.png"), check_existing=True)
    img.gl_load(bgl.GL_LINEAR, bgl.GL_LINEAR)
    code = img.bindcode
    return code[0] if hasattr(code, "__len__") else code


AV = {}


def avatar(name):
    if name not in AV:
        AV[name] = (load_obj(name), texture(name))
    return AV[name]


def depth_shapes():
    """triangles of the objects' own shapes in world space (for the depth pass)"""
    if "shapes" in state:
        return state["shapes"]
    tris = []
    scene = bpy.context.scene
    for ob in scene.objects:
        if ob.type != 'MESH' or not ob.is_visible(scene):
            continue
        me = ob.to_mesh(scene, True, 'PREVIEW')
        mw = ob.matrix_world
        for poly in me.polygons:
            pts = [mw * me.vertices[i].co for i in poly.vertices]
            for k in range(1, len(pts) - 1):
                tris.append((pts[0], pts[k], pts[k + 1]))
        bpy.data.meshes.remove(me)
    state["shapes"] = tris
    return tris


def draw_ring(center, radius, col, alpha):
    bgl.glColor4f(col[0], col[1], col[2], 0.9 * alpha)
    bgl.glLineWidth(2.0)
    bgl.glBegin(bgl.GL_LINE_LOOP)
    for i in range(48):
        a = 2.0 * math.pi * i / 48
        bgl.glVertex3f(center.x + radius * math.cos(a), center.y + radius * math.sin(a), center.z)
    bgl.glEnd()


def draw_avatar(name, st, base, facing):
    mesh, tex = avatar(name)
    col, dark, alpha = STATES[st]
    size = 0.8
    rot = Matrix.Rotation(facing, 4, 'Z') * Matrix.Rotation(math.radians(90), 4, 'X')
    m = Matrix.Translation(base + Vector((0, 0, size * 0.5 + 0.15))) * rot * Matrix.Scale(size, 4)
    # edges (side walls) first, then the pictured faces
    bgl.glDisable(bgl.GL_TEXTURE_2D)
    bgl.glColor4f(col[0], col[1], col[2], 0.85 * alpha)
    bgl.glBegin(bgl.GL_QUADS)
    for mat, vi, ti in mesh["faces"]:
        if mat == "edge":
            for i in vi:
                p = m * mesh["v"][i]
                bgl.glVertex3f(p.x, p.y, p.z)
    bgl.glEnd()
    bgl.glEnable(bgl.GL_TEXTURE_2D)
    bgl.glBindTexture(bgl.GL_TEXTURE_2D, tex)
    bgl.glTexEnvi(bgl.GL_TEXTURE_ENV, bgl.GL_TEXTURE_ENV_MODE, bgl.GL_MODULATE)
    bgl.glColor4f(dark, dark, dark, 0.92 * alpha)
    bgl.glBegin(bgl.GL_QUADS)
    for mat, vi, ti in mesh["faces"]:
        if mat == "face":
            for i, t in zip(vi, ti):
                bgl.glTexCoord2f(*mesh["vt"][t])
                p = m * mesh["v"][i]
                bgl.glVertex3f(p.x, p.y, p.z)
    bgl.glEnd()
    bgl.glDisable(bgl.GL_TEXTURE_2D)
    draw_ring(base + Vector((0, 0, 0.02)), size * 0.42, col, alpha)


def draw_holograms():
    if not state["holo"]:
        return
    t0 = time.perf_counter()
    try:
        scene = bpy.context.scene
        rv3d = bpy.context.region_data
        view_pos = rv3d.view_matrix.inverted().translation
        bgl.glPushAttrib(bgl.GL_ALL_ATTRIB_BITS)
        bgl.glEnable(bgl.GL_DEPTH_TEST)
        if state["depth"]:
            # the objects' shapes into the depth buffer only
            bgl.glClear(bgl.GL_DEPTH_BUFFER_BIT)
            bgl.glColorMask(False, False, False, False)
            bgl.glBegin(bgl.GL_TRIANGLES)
            for a, b, c in depth_shapes():
                bgl.glVertex3f(a.x, a.y, a.z)
                bgl.glVertex3f(b.x, b.y, b.z)
                bgl.glVertex3f(c.x, c.y, c.z)
            bgl.glEnd()
            bgl.glColorMask(True, True, True, True)
        else:
            bgl.glDisable(bgl.GL_DEPTH_TEST)
        bgl.glEnable(bgl.GL_BLEND)
        bgl.glBlendFunc(bgl.GL_SRC_ALPHA, bgl.GL_ONE_MINUS_SRC_ALPHA)
        bgl.glEnable(bgl.GL_ALPHA_TEST)
        bgl.glAlphaFunc(bgl.GL_GREATER, 0.02)
        bgl.glDepthMask(False)
        bgl.glDisable(bgl.GL_LIGHTING)
        bgl.glDisable(bgl.GL_CULL_FACE)
        for obname, (av, st) in sorted(HOLOGRAMS.items()):
            ob = scene.objects.get(obname)
            if ob is None:
                continue
            corners = [ob.matrix_world * Vector(c) for c in ob.bound_box]
            top = max(c.z for c in corners) if ob.type in ('MESH', 'FONT', 'ARMATURE') else ob.matrix_world.translation.z
            base = Vector((ob.matrix_world.translation.x, ob.matrix_world.translation.y, top + 0.1))
            d = view_pos - base
            facing = math.atan2(d.y, d.x) - math.radians(90)
            draw_avatar(av, st, base, facing)
        bgl.glPopAttrib()
    except Exception:
        result["errors"].append("drawing: " + traceback.format_exc())
        state["holo"] = False
    state["draw_ms"].append((time.perf_counter() - t0) * 1000.0)


# ---------------------------------------------------------------------------
# Steps

def a_convert():
    t0 = time.perf_counter()
    mem0 = process_mb()
    ret = bpy.ops.view3d.gameview_study_convert(override(), report=REPORT, repeat=REPEAT)
    state["convert"] = {"operator": list(ret), "seconds": round(time.perf_counter() - t0, 3),
                        "mem_before_mb": mem0, "mem_after_mb": process_mb()}


def a_after_convert():
    """the editor still works after the dry conversion: an object moves, the view draws"""
    ob = bpy.context.scene.objects.get("Cube") or bpy.context.scene.objects.get("Box")
    if ob is not None:
        ob.location.x += 0.5
        state["moved"] = ob.name


def holo(depth, shading):
    def act():
        state["holo"] = True
        state["depth"] = depth
        state["draw_ms"] = []
        shade(shading)
        main_area().spaces.active.region_3d.view_perspective = 'CAMERA'
    return act


STEPS = []
if "convert" in PARTS:
    STEPS += [("convert", a_convert), ("after_convert", a_after_convert)]
if "plain" in PARTS:
    STEPS += [("plain_holo", holo(True, 'SOLID'))]
if "remix" in PARTS:
    STEPS += [("remix_nodepth", holo(False, 'RENDERED')), ("remix_depth", holo(True, 'RENDERED'))]
LONG = {"remix_nodepth": 150}   # Remix's first picture can take seconds (shaders)


def record(name):
    entry = {"step": name, "screenshot": screenshot(name), "mem_mb": process_mb()}
    if state["draw_ms"]:
        ms = sorted(state["draw_ms"])
        entry["draw_ms_median"] = round(ms[len(ms) // 2], 3)
        entry["draw_frames"] = len(ms)
    if name == "convert":
        entry["convert"] = state.get("convert")
    if name == "after_convert":
        entry["moved"] = state.get("moved")
    result["steps"].append(entry)
    print("P7 study probe: %s %s" % (name, json.dumps({k: v for k, v in entry.items() if k != "screenshot"})),
          flush=True)


class P7StudyProbe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p7_study_probe"
    bl_label = "BGE-Remix P7 study probe"
    _timer = None

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        state["ticks"] = state.get("ticks", 0) + 1
        if state["ticks"] < 10:
            return {'PASS_THROUGH'}
        i = state.get("i", 0)
        if i < len(STEPS):
            name, fn = STEPS[i]
            if state.get("phase", "act") == "act":
                try:
                    fn()
                except Exception:
                    result["errors"].append("%s: %s" % (name, traceback.format_exc()))
                state["phase"] = "wait"
                state["until"] = state["ticks"] + LONG.get(name, WAIT)
            elif state["ticks"] >= state["until"]:
                try:
                    record(name)
                except Exception:
                    result["errors"].append("record %s: %s" % (name, traceback.format_exc()))
                state["i"] = i + 1
                state["phase"] = "act"
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


bpy.utils.register_class(P7StudyProbe)
bpy.types.SpaceView3D.draw_handler_add(draw_holograms, (), 'WINDOW', 'POST_VIEW')
try:
    bpy.ops.wm.bge_remix_p7_study_probe('INVOKE_DEFAULT')
except Exception:
    bpy.ops.wm.bge_remix_p7_study_probe(override(), 'INVOKE_DEFAULT')
