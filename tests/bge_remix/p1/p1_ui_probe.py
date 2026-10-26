# BGE-Remix P1 UI probe (T1.2, UI part): runs inside Blender's UI (Python 3.5).
#   blender t01_ui.blend --python p1_ui_probe.py
#
# Opens Preferences > System, where the RTX Remix box is drawn, takes a
# screenshot and quits. Python errors in the box's drawing code show up in
# Blender's output, which run_p1.py checks.

import json
import os

import bpy

OUT = os.environ.get("BGE_REMIX_P1_UI_OUT", "p1_ui.json")
SHOT = os.path.splitext(OUT)[0] + ".png"
result = {"opened": False, "screenshot": None, "errors": []}


def override():
    win = bpy.context.window_manager.windows[0]
    area = max(win.screen.areas, key=lambda a: a.width * a.height)
    region = [r for r in area.regions if r.type == 'WINDOW'][0]
    return {"window": win, "screen": win.screen, "area": area, "region": region}


class P1UIProbe(bpy.types.Operator):
    bl_idname = "wm.bge_remix_p1_ui_probe"
    bl_label = "BGE-Remix P1 UI probe"
    _timer = None
    _ticks = 0

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        self._ticks += 1
        if self._ticks == 5:
            try:
                context.user_preferences.active_section = 'SYSTEM'
                # Show the preferences in the main area (no extra window needed)
                o = override()
                o["area"].type = 'USER_PREFERENCES'
                result["opened"] = True
            except Exception as ex:
                result["errors"].append("open: %r" % ex)
        if self._ticks < 25:
            return {'PASS_THROUGH'}
        context.window_manager.event_timer_remove(self._timer)
        try:
            bpy.ops.screen.screenshot(override(), filepath=SHOT, full=True)
            result["screenshot"] = SHOT
        except Exception as ex:
            result["errors"].append("screenshot: %r" % ex)
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


bpy.utils.register_class(P1UIProbe)
try:
    bpy.ops.wm.bge_remix_p1_ui_probe('INVOKE_DEFAULT')
except Exception:
    bpy.ops.wm.bge_remix_p1_ui_probe(override(), 'INVOKE_DEFAULT')
