# BGE-Remix T0.1 probe: runs inside Blender 2.79b's UI (Python 3.5).
#   blender t01_ui.blend --python ui_probe.py
#
# Waits until the window has drawn a few times, records when that happened
# and which OpenGL driver is in use, saves a screenshot, then quits Blender.
# Results go to the JSON file named by BGE_REMIX_T01_OUT.

import json
import os
import time

import bgl
import bpy

OUT = os.environ.get("BGE_REMIX_T01_OUT", "t01_result.json")
SHOT = os.path.splitext(OUT)[0] + ".png"
TICKS = 20  # timer ticks (0.1 s apart) before quitting

result = {
    "first_tick": None,
    "gl_vendor": None,
    "gl_renderer": None,
    "gl_version": None,
    "blender_version": bpy.app.version_string,
    "screenshot": None,
    "errors": [],
}


def window_override():
    wm = bpy.context.window_manager
    win = wm.windows[0]
    area = max(win.screen.areas, key=lambda a: a.width * a.height)
    region = [r for r in area.regions if r.type == 'WINDOW'][0]
    return {"window": win, "screen": win.screen, "area": area, "region": region}


def write():
    with open(OUT, "w") as f:
        json.dump(result, f, indent=2)


class BGERemixProbe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p0_probe"
    bl_label = "BGE-Remix P0 probe"

    _timer = None
    _ticks = 0

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        self._ticks += 1
        if result["first_tick"] is None:
            result["first_tick"] = time.time()
            try:
                result["gl_vendor"] = bgl.glGetString(bgl.GL_VENDOR)
                result["gl_renderer"] = bgl.glGetString(bgl.GL_RENDERER)
                result["gl_version"] = bgl.glGetString(bgl.GL_VERSION)
            except Exception as ex:
                result["errors"].append("glGetString: %r" % ex)
        if self._ticks < TICKS:
            return {'PASS_THROUGH'}
        context.window_manager.event_timer_remove(self._timer)
        try:
            bpy.ops.screen.screenshot(window_override(), filepath=SHOT, full=True)
            result["screenshot"] = SHOT
        except Exception as ex:
            result["errors"].append("screenshot: %r" % ex)
        write()
        bpy.ops.wm.quit_blender()
        return {'FINISHED'}

    def invoke(self, context, event):
        wm = context.window_manager
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}


bpy.utils.register_class(BGERemixProbe)
try:
    bpy.ops.wm.bge_remix_p0_probe('INVOKE_DEFAULT')
except Exception as ex:
    # No window in the script's context: start the probe with an override.
    result["errors"].append("invoke without window: %r" % ex)
    bpy.ops.wm.bge_remix_p0_probe(window_override(), 'INVOKE_DEFAULT')
