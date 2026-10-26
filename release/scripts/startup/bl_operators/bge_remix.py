# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>

# BGE-Remix: operators behind the RTX Remix box in Preferences > System.
# Python 3.5 (Blender 2.79): no f-strings.

import os
import tempfile
import threading

from bpy.props import StringProperty
from bpy.types import Operator


def _redraw_prefs(context):
    for win in context.window_manager.windows:
        for area in win.screen.areas:
            if area.type == 'USER_PREFERENCES':
                area.tag_redraw()


class WM_OT_bge_remix_install(Operator):
    """Download the RTX Remix runtime from NVIDIA's GitHub page, check it and install it"""
    bl_idname = "wm.bge_remix_install"
    bl_label = "Download RTX Remix Runtime"

    # shared with the Preferences box while a download runs
    running = False
    message = ""

    _timer = None
    _thread = None
    _state = None

    def _work(self, target, pin):
        import bge_remix.installer as installer
        state = self._state

        def progress(phase, done, total):
            state["phase"] = phase
            state["done"] = done
            state["total"] = total

        ok, msg = installer.install(target, pin, progress, lambda: state["cancel"])
        state["result"] = (ok, msg)

    def invoke(self, context, event):
        import bge_remix
        from bge_remix import pin
        if bge_remix.pin_for_install() is not pin:  # a test package: no dialog
            return self.execute(context)
        return context.window_manager.invoke_props_dialog(self, width=520)

    def draw(self, context):
        import bge_remix
        from bge_remix import pin
        from bge_remix.ui import _wrap
        col = self.layout.column(align=True)
        col.label(text="RTX Remix %s, %d MB, from github.com/NVIDIAGameWorks/rtx-remix" % (
            pin.VERSION, round(pin.SIZE / 1e6)))
        col.label(text="Into: " + bge_remix.runtime_dir())
        col.separator()
        _wrap(col, pin.LICENSE_NOTE, width=80)
        col.separator()
        col.label(text="Click OK to download. The file is checked before it is installed.")

    def execute(self, context):
        import bge_remix
        if WM_OT_bge_remix_install.running:
            self.report({'WARNING'}, "A download is already running")
            return {'CANCELLED'}
        bge_remix.shutdown()  # files of a loaded runtime can't be replaced
        pin = bge_remix.pin_for_install()
        target = bge_remix.runtime_dir()

        self._state = {"phase": "download", "done": 0, "total": pin.SIZE, "cancel": False, "result": None}
        self._thread = threading.Thread(target=self._work, args=(target, pin), daemon=True)
        self._thread.start()
        WM_OT_bge_remix_install.running = True
        WM_OT_bge_remix_install.message = "Starting the download..."

        wm = context.window_manager
        wm.progress_begin(0, 100)
        self._timer = wm.event_timer_add(0.25, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}

    def modal(self, context, event):
        if event.type == 'ESC':
            self._state["cancel"] = True
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}

        st = self._state
        total = st["total"] or 1
        percent = 100.0 * st["done"] / total
        if st["phase"] == "download":
            WM_OT_bge_remix_install.message = "Downloading: %d of %d MB (Esc cancels)" % (
                st["done"] // 1000000, total // 1000000)
            context.window_manager.progress_update(percent * 0.8)
        else:
            WM_OT_bge_remix_install.message = "Unpacking: %d%%" % percent
            context.window_manager.progress_update(80 + percent * 0.2)
        _redraw_prefs(context)

        if st["result"] is None:
            return {'PASS_THROUGH'}

        wm = context.window_manager
        wm.event_timer_remove(self._timer)
        wm.progress_end()
        WM_OT_bge_remix_install.running = False
        WM_OT_bge_remix_install.message = ""
        _redraw_prefs(context)
        ok, msg = st["result"]
        self.report({'INFO'} if ok else {'ERROR'}, msg)
        return {'FINISHED'} if ok else {'CANCELLED'}


class WM_OT_bge_remix_remove(Operator):
    """Delete the installed RTX Remix runtime"""
    bl_idname = "wm.bge_remix_remove"
    bl_label = "Remove RTX Remix Runtime"

    def invoke(self, context, event):
        return context.window_manager.invoke_confirm(self, event)

    def execute(self, context):
        import bge_remix
        import bge_remix.installer as installer
        bge_remix.shutdown()
        ok, msg = installer.remove(bge_remix.runtime_dir())
        self.report({'INFO'} if ok else {'ERROR'}, msg)
        _redraw_prefs(context)
        return {'FINISHED'} if ok else {'CANCELLED'}


class WM_OT_bge_remix_set_folder(Operator):
    """Choose the folder the RTX Remix runtime is installed in (or will be)"""
    bl_idname = "wm.bge_remix_set_folder"
    bl_label = "Choose RTX Remix Folder"

    directory = StringProperty(subtype='DIR_PATH')

    def invoke(self, context, event):
        import bge_remix
        self.directory = bge_remix.runtime_dir()
        context.window_manager.fileselect_add(self)
        return {'RUNNING_MODAL'}

    def execute(self, context):
        import bge_remix
        bge_remix.shutdown()
        bge_remix.set_runtime_dir(self.directory)
        _redraw_prefs(context)
        return {'FINISHED'}


class WM_OT_bge_remix_reset_folder(Operator):
    """Go back to the default RTX Remix folder"""
    bl_idname = "wm.bge_remix_reset_folder"
    bl_label = "Default RTX Remix Folder"

    def execute(self, context):
        import bge_remix
        bge_remix.shutdown()
        bge_remix.set_runtime_dir(None)
        _redraw_prefs(context)
        return {'FINISHED'}


class WM_OT_bge_remix_test(Operator):
    """Start RTX Remix in its own window and show a path-traced test scene for 5 seconds"""
    bl_idname = "wm.bge_remix_test"
    bl_label = "Test RTX Remix"

    def execute(self, context):
        import bge_remix
        code, text = bge_remix.startup(1280, 720, True)
        if code != "RUNNING":
            self.report({'ERROR'}, text)
            return {'CANCELLED'}
        shot = os.path.join(tempfile.gettempdir(), "bge-remix-test.bmp")
        r = bge_remix._bge_remix.triangle_test(5.0, shot, False)
        if r["ok"] and not bge_remix.has_picture(shot):
            self.report({'WARNING'}, "RTX Remix is running but still preparing its shaders "
                        "(the first start can take a minute). Run the test again.")
        elif r["ok"]:
            self.report({'INFO'}, "RTX Remix works: %d frames, %.1f ms per frame" % (
                r["frames"], r["frame_ms_median"]))
        else:
            self.report({'ERROR'}, "RTX Remix test had errors (Remix error %d), see %s" % (
                r["last_error"], bge_remix._bge_remix.bridge_info()["log_path"]))
        _redraw_prefs(context)
        return {'FINISHED'}


classes = (
    WM_OT_bge_remix_install,
    WM_OT_bge_remix_remove,
    WM_OT_bge_remix_set_folder,
    WM_OT_bge_remix_reset_folder,
    WM_OT_bge_remix_test,
)
