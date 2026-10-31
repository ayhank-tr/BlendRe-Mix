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
#   BGE_REMIX_P8_DONE=<json>  what happened

import json
import os
import traceback

import bpy

VIEW = os.environ.get("BGE_REMIX_P8_VIEW")
GAME = os.environ.get("BGE_REMIX_P8_GAME")
LAMPS = os.environ.get("BGE_REMIX_P8_LAMPS")
DONE = os.environ.get("BGE_REMIX_P8_DONE", "p8_ui.json")

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
        if VIEW:
            step("view", render_view)
        if GAME:
            step("game", play)
        if LAMPS:
            step("lamps", lamp_types)
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
