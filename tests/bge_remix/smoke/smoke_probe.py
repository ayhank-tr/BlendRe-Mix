# BGE-Remix crash check: what a person does by hand in Blender's window, step
# by step, with RTX Remix (Python 3.5, run inside our Blender with its UI):
#   blender -p 0 0 1600 900 [file.blend] --python smoke_probe.py
#
# Without a file it builds its own scene: the kinds of content old games hold
# (Classic materials with images and procedural textures, several UV maps,
# vertex colours, material slots a face points past, modifiers, an armature,
# hair, a curve, a text, a group instance, loose vertices, every lamp kind)
# and a game that ends itself after two seconds. With a file (the person's
# own, on their PC: never in the tests) the same steps run on it.
#
# Each step's name is written to BGE_REMIX_SMOKE_OUT before it runs, so a
# crash names the step it happened in. Steps can be picked with
# BGE_REMIX_SMOKE_STEPS (comma separated; default: all).

import json
import os
import time
import traceback

import bpy

try:
    import bge_remix
except ImportError:
    bge_remix = None

OUT = os.environ.get("BGE_REMIX_SMOKE_OUT", "smoke.json")
DIR = os.path.dirname(os.path.abspath(OUT))
PICK = [s for s in os.environ.get("BGE_REMIX_SMOKE_STEPS", "").split(",") if s]
REMIX_WAIT = float(os.environ.get("BGE_REMIX_SMOKE_REMIX_WAIT", "60"))  # seconds for Remix's first frames
OWN_FILE = bool(bpy.data.filepath)

result = {"steps": {}, "order": [], "errors": [], "finished": False, "doing": None, "own_file": OWN_FILE,
          "started": time.time()}


def write():
    try:
        with open(OUT, "w") as f:
            json.dump(result, f, indent=1, default=str)
    except (OSError, TypeError, ValueError):
        pass


def window():
    return bpy.context.window_manager.windows[0]


def view_area():
    areas = [a for a in window().screen.areas if a.type in ('VIEW_3D', 'GAME_VIEW')]
    areas.sort(key=lambda a: -a.width * a.height)
    return areas[0] if areas else None


def override(area=None):
    win = window()
    area = area or view_area()
    region = [r for r in area.regions if r.type == 'WINDOW'][0] if area else None
    return {"window": win, "screen": win.screen, "area": area, "region": region, "scene": bpy.context.scene}


def view():
    return view_area().spaces.active


def redraw():
    for a in window().screen.areas:
        a.tag_redraw()


def stats():
    try:
        return bge_remix.view_stats() if bge_remix is not None else None
    except Exception as ex:
        return "error: %r" % ex


# ---------------------------------------------------------------------------
# the scene (our own content)

def _mesh(name, verts, faces, scene, location=(0, 0, 0)):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.update()
    ob = bpy.data.objects.new(name, me)
    ob.location = location
    scene.objects.link(ob)
    return ob


def _grid(name, n, size, scene, location):
    verts = [((i / n - 0.5) * size, (j / n - 0.5) * size, 0.0) for j in range(n + 1) for i in range(n + 1)]
    faces = [(j * (n + 1) + i, j * (n + 1) + i + 1, (j + 1) * (n + 1) + i + 1, (j + 1) * (n + 1) + i)
             for j in range(n) for i in range(n)]
    return _mesh(name, verts, faces, scene, location)


def _cube(name, scene, location, size=1.0):
    s = size / 2.0
    verts = [(-s, -s, -s), (s, -s, -s), (s, s, -s), (-s, s, -s), (-s, -s, s), (s, -s, s), (s, s, s), (-s, s, s)]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    return _mesh(name, verts, faces, scene, location)


def _uv(ob, name):
    ob.data.uv_textures.new(name)


def _classic(name, colour, intensity=0.8):
    ma = bpy.data.materials.new(name)
    ma.diffuse_color = colour
    ma.diffuse_intensity = intensity
    return ma


def _image_slot(ma, img, uv_layer=""):
    tex = bpy.data.textures.new(ma.name + "_img", 'IMAGE')
    tex.image = img
    slot = ma.texture_slots.add()
    slot.texture = tex
    slot.texture_coords = 'UV'
    slot.uv_layer = uv_layer
    return slot


def _procedural_slot(ma, kind, coords='ORCO', to_normal=False):
    tex = bpy.data.textures.new(ma.name + "_" + kind.lower(), kind)
    slot = ma.texture_slots.add()
    slot.texture = tex
    slot.texture_coords = coords
    if to_normal:
        slot.use_map_color_diffuse = False
        slot.use_map_normal = True
    return slot


def s_build():
    if OWN_FILE:
        return {"file": bpy.data.filepath, "objects": len(bpy.data.objects)}
    scene = bpy.context.scene
    for ob in list(scene.objects):
        scene.objects.unlink(ob)
    scene.render.engine = 'BLENDER_RENDER'
    img = bpy.data.images.new("SM_grid", 256, 256)
    img.generated_type = 'UV_GRID'
    alpha = bpy.data.images.new("SM_alpha", 128, 128, alpha=True)
    alpha.generated_type = 'COLOR_GRID'

    floor = _grid("SM_floor", 8, 20.0, scene, (0, 0, 0))
    _uv(floor, "UVMap")
    _uv(floor, "Second")
    ma = _classic("SM_floor_image", (0.8, 0.8, 0.8))
    _image_slot(ma, img, "Second")
    floor.data.materials.append(ma)

    # procedural textures (Remix bakes them per mesh), on ORCO and on UVs,
    # one to the normal
    rock = _cube("SM_clouds", scene, (-4, 0, 1), 2.0)
    _uv(rock, "UVMap")
    ma = _classic("SM_clouds", (0.5, 0.45, 0.4))
    _procedural_slot(ma, 'CLOUDS', 'ORCO')
    _procedural_slot(ma, 'WOOD', 'UV', to_normal=True)
    rock.data.materials.append(ma)

    # vertex colours, an alpha image, two materials and a face pointing past them
    multi = _cube("SM_multi", scene, (0, 0, 1), 2.0)
    _uv(multi, "UVMap")
    multi.data.vertex_colors.new("Col")
    m1 = _classic("SM_vcol", (1, 1, 1))
    m1.use_vertex_color_paint = True
    m2 = _classic("SM_alpha", (1, 1, 1))
    m2.use_transparency = True
    m2.alpha = 0.0
    s2 = _image_slot(m2, alpha, "UVMap")
    s2.use_map_alpha = True
    m3 = _classic("SM_marble", (0.9, 0.9, 0.85))
    _procedural_slot(m3, 'MARBLE', 'UV')
    for m in (m1, m2, m3):
        multi.data.materials.append(m)
    for i, p in enumerate(multi.data.polygons):
        p.material_index = i % 3
    multi.data.polygons[5].material_index = 7      # past the slots, as some old files have

    # modifiers
    sub = _cube("SM_subsurf", scene, (4, 0, 1), 1.5)
    sub.data.materials.append(m3)
    sub.modifiers.new("Subsurf", 'SUBSURF').levels = 2
    sub.modifiers.new("Mirror", 'MIRROR')

    # an armature deforming a mesh
    arm_data = bpy.data.armatures.new("SM_arm")
    arm = bpy.data.objects.new("SM_arm", arm_data)
    arm.location = (0, 4, 0)
    scene.objects.link(arm)
    scene.objects.active = arm
    bpy.ops.object.mode_set(mode='EDIT')
    b = arm_data.edit_bones.new("Bone")
    b.head, b.tail = (0, 0, 0), (0, 0, 2)
    bpy.ops.object.mode_set(mode='OBJECT')
    skin = _cube("SM_skinned", scene, (0, 4, 1), 1.0)
    skin.data.materials.append(m1)
    vg = skin.vertex_groups.new("Bone")
    vg.add(list(range(len(skin.data.vertices))), 1.0, 'REPLACE')
    skin.modifiers.new("Armature", 'ARMATURE').object = arm
    arm.pose.bones["Bone"].rotation_mode = 'XYZ'
    arm.pose.bones["Bone"].rotation_euler = (0.3, 0, 0)

    # hair
    hairy = _grid("SM_hair", 2, 2.0, scene, (-4, 4, 0))
    hairy.data.materials.append(_classic("SM_hair_ma", (0.3, 0.6, 0.2)))
    hairy.modifiers.new("Hair", 'PARTICLE_SYSTEM')
    ps = hairy.particle_systems[0].settings
    ps.type = 'HAIR'
    ps.count = 200
    # hair drawing an object with a procedural texture (grass in old games)
    blade = _cube("SM_blade", scene, (-8, 8, 0), 0.2)
    bma = _classic("SM_blade_clouds", (0.3, 0.7, 0.2))
    _procedural_slot(bma, 'CLOUDS', 'ORCO')
    blade.data.materials.append(bma)
    ps.render_type = 'OBJECT'
    ps.dupli_object = blade

    # a curve, a text, loose vertices, a mesh with edges only
    cu = bpy.data.curves.new("SM_curve", 'CURVE')
    cu.bevel_depth = 0.1
    spline = cu.splines.new('BEZIER')
    spline.bezier_points.add(1)
    spline.bezier_points[0].co, spline.bezier_points[1].co = (0, 0, 0), (2, 0, 1)
    cob = bpy.data.objects.new("SM_curve", cu)
    cob.location = (4, 4, 1)
    scene.objects.link(cob)
    txt = bpy.data.objects.new("SM_text", bpy.data.curves.new("SM_text", 'FONT'))
    txt.data.body = "RTX"
    txt.location = (-2, -4, 1)
    scene.objects.link(txt)
    _mesh("SM_points", [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], scene, (2, -4, 1))
    me = bpy.data.meshes.new("SM_edges")
    me.from_pydata([(0, 0, 0), (1, 0, 1)], [(0, 1)], [])
    eob = bpy.data.objects.new("SM_edges", me)
    eob.location = (3, -4, 1)
    scene.objects.link(eob)

    # a group instanced by an empty
    grp = bpy.data.groups.new("SM_group")
    member = _cube("SM_group_cube", scene, (0, -8, 1), 1.0)
    member.data.materials.append(_classic("SM_group_ma", (0.2, 0.3, 0.9)))
    grp.objects.link(member)
    inst = bpy.data.objects.new("SM_instance", None)
    inst.dupli_type = 'GROUP'
    inst.dupli_group = grp
    inst.location = (6, -6, 0)
    scene.objects.link(inst)

    # lamps and the camera
    for kind, loc in (('SUN', (0, -6, 10)), ('POINT', (3, 3, 4)), ('SPOT', (-3, 3, 5)), ('HEMI', (0, 0, 12))):
        la = bpy.data.lamps.new("SM_" + kind.lower(), kind)
        lob = bpy.data.objects.new("SM_" + kind.lower(), la)
        lob.location = loc
        scene.objects.link(lob)
    cam = bpy.data.objects.new("SM_camera", bpy.data.cameras.new("SM_camera"))
    cam.location = (0, -18, 8)
    cam.rotation_euler = (1.2, 0, 0)
    scene.objects.link(cam)
    scene.camera = cam

    # a game that ends itself: two seconds, then Quit
    logic = rock
    bpy.context.scene.objects.active = logic
    bpy.ops.logic.sensor_add(type='DELAY', object=logic.name)
    sen = logic.game.sensors[-1]
    sen.delay = 120
    bpy.ops.logic.controller_add(type='LOGIC_AND', object=logic.name)
    con = logic.game.controllers[-1]
    bpy.ops.logic.actuator_add(type='GAME', object=logic.name)
    act = logic.game.actuators[-1]
    act.mode = 'QUIT'
    sen.link(con)
    con.link(actuator=act)
    bpy.ops.logic.sensor_add(type='ALWAYS', object=logic.name)
    spin = logic.game.sensors[-1]
    bpy.ops.logic.controller_add(type='LOGIC_AND', object=logic.name)
    sc = logic.game.controllers[-1]
    bpy.ops.logic.actuator_add(type='MOTION', object=logic.name)
    mot = logic.game.actuators[-1]
    mot.offset_rotation = (0, 0, 0.02)
    spin.link(sc)
    sc.link(actuator=mot)
    return {"objects": len(scene.objects)}


# ---------------------------------------------------------------------------
# the steps

def shade(kind):
    def fn():
        view().viewport_shade = kind
        return {"shade": kind}
    fn.__name__ = "s_shade_" + kind.lower()
    return fn


def s_engine_game():
    bpy.context.scene.render.engine = 'BLENDER_GAME'
    return {"engine": bpy.context.scene.render.engine}


def s_pipeline_remix():
    gs = bpy.context.scene.game_settings
    gs.render_pipeline = 'RTX_REMIX'
    return {"pipeline": gs.render_pipeline}


def s_remix_on():
    bpy.context.scene.bge_remix = 'RTX_REMIX'
    return {"remix": bpy.context.scene.bge_remix}


def s_perspective():
    r3d = view().region_3d
    if not r3d.is_perspective:
        bpy.ops.view3d.view_persportho(override())
    return {"perspective": view().region_3d.is_perspective}


def s_rendered():
    view().viewport_shade = 'RENDERED'
    return {"shade": "RENDERED"}


def s_rendered_check():
    return {"stats": stats(), "seconds": round(time.time() - result["started"], 1)}


def s_rendered_again_check():
    return s_rendered_check()


def s_orbit():
    for _ in range(4):
        bpy.ops.view3d.view_orbit(override(), type='ORBITLEFT')
    return {"stats": stats()}


def s_edit_mode():
    scene = bpy.context.scene
    meshes = [o for o in scene.objects if o.type == 'MESH' and not o.library]
    if not meshes:
        return {"skipped": "no mesh"}
    scene.objects.active = meshes[0]
    bpy.ops.object.mode_set(mode='EDIT')
    return {"object": meshes[0].name}


def s_object_mode():
    bpy.ops.object.mode_set(mode='OBJECT')
    return {"stats": stats()}


def s_add_cube():
    bpy.ops.mesh.primitive_cube_add(location=(0, 0, 6))
    return {"added": bpy.context.object.name}


def s_delete_cube():
    ob = bpy.context.object
    name = ob.name if ob else None
    bpy.ops.object.delete(override())
    return {"deleted": name}


def s_undo():
    bpy.ops.ed.undo()
    return {"stats": stats()}


def s_play():
    # the game ends itself (our scene: after two seconds; a person's file:
    # BGE_REMIX_SMOKE_PLAY=0 leaves this out)
    if OWN_FILE and os.environ.get("BGE_REMIX_SMOKE_PLAY", "0") != "1":
        return {"skipped": "a person's file: only with BGE_REMIX_SMOKE_PLAY=1"}
    view().viewport_shade = 'MATERIAL'
    t = time.time()
    bpy.ops.view3d.game_start(override())
    return {"played_seconds": round(time.time() - t, 1)}


def s_f12():
    if OWN_FILE:
        return {"skipped": "a person's file"}
    scene = bpy.context.scene
    scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = 640, 360, 100
    try:
        scene.bge_remix_f12_frames = 4
    except AttributeError:
        pass
    t = time.time()
    bpy.ops.render.render()
    return {"seconds": round(time.time() - t, 1)}


def s_remix_view():
    area = view_area()
    area.type = 'GAME_VIEW'
    return {"area": area.type}


def gv_mode(mode):
    def fn():
        bpy.ops.gameview.mode(override(), mode=mode)
        return {"mode": view().game_view_mode, "stats": stats()}
    fn.__name__ = "s_view_" + mode.lower()
    return fn


def s_view_run():
    if OWN_FILE and os.environ.get("BGE_REMIX_SMOKE_PLAY", "0") != "1":
        return {"skipped": "a person's file: only with BGE_REMIX_SMOKE_PLAY=1"}
    bpy.ops.gameview.mode(override(), mode='RUN')
    t = time.time()
    bpy.ops.view3d.gameview_play(override())
    return {"played_seconds": round(time.time() - t, 1)}


def s_back_to_3d():
    area = view_area()
    area.type = 'VIEW_3D'
    return {"area": area.type}


def s_save():
    if OWN_FILE:
        return {"skipped": "a person's file is not saved"}
    path = os.path.join(DIR, "smoke_saved.blend")
    bpy.ops.wm.save_as_mainfile(filepath=path, copy=True)
    return {"file": path}


# (name, ticks of 0.1 s to wait before it)
REMIX_TICKS = int(REMIX_WAIT * 10)
STEPS = [
    (5, s_build),
    # Solid straight to Rendered first, as a person does (the P10 crash: no
    # other shading had drawn the meshes yet)
    (5, shade('SOLID')),
    (5, s_engine_game), (5, s_pipeline_remix), (5, s_remix_on), (5, s_perspective),
    (5, s_rendered), (REMIX_TICKS, s_rendered_check), (5, s_orbit), (30, s_edit_mode), (20, s_object_mode),
    (20, s_add_cube), (20, s_delete_cube), (20, s_undo),
    (30, shade('TEXTURED')), (10, shade('MATERIAL')), (10, s_rendered), (50, s_rendered_again_check),
    (50, s_play), (10, s_f12),
    (10, s_remix_view), (10, gv_mode('CHECK')), (10, gv_mode('DEBUG')), (50, gv_mode('EDIT')),
    (10, s_view_run), (10, gv_mode('CHECK')), (10, s_back_to_3d), (10, s_save),
]
if PICK:
    STEPS = [(w, fn) for w, fn in STEPS if fn.__name__ in PICK or fn.__name__ == "s_build"]
QUEUE = list(STEPS)


def step(fn):
    name = fn.__name__
    # (written before it runs: a crash names the step it happened in)
    result["doing"] = name
    result["order"].append(name)
    write()
    print("SMOKE step", name, flush=True)
    t = time.time()
    try:
        r = fn()
        result["steps"][name] = {"ok": True, "seconds": round(time.time() - t, 2), "result": r}
    except Exception:
        result["steps"][name] = {"ok": False, "seconds": round(time.time() - t, 2)}
        result["errors"].append("%s: %s" % (name, traceback.format_exc()))
    result["doing"] = None
    result["last_step"] = name
    write()


class SmokeProbe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_smoke_probe"
    bl_label = "BGE-Remix crash check"
    _timer = None
    _ticks = 0

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        self._ticks += 1
        redraw()
        if self._ticks < 10:
            return {'PASS_THROUGH'}
        if QUEUE:
            wait, fn = QUEUE[0]
            if wait > 0:
                QUEUE[0] = (wait - 1, fn)
                return {'PASS_THROUGH'}
            QUEUE.pop(0)
            step(fn)
            return {'PASS_THROUGH'}
        result["finished"] = True
        write()
        context.window_manager.event_timer_remove(self._timer)
        context.user_preferences.view.use_quit_dialog = False
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


write()
bpy.utils.register_class(SmokeProbe)
bpy.ops.wm.bge_remix_smoke_probe('INVOKE_DEFAULT')
