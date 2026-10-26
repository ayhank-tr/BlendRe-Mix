# BGE-Remix: the RTX Remix box in Preferences > System. Python 3.5.

import bge_remix
from bge_remix import pin

ICONS = {
    "RUNNING": 'FILE_TICK',
    "READY": 'FILE_TICK',
    "INSTALLED": 'FILE_TICK',
    "NOT_INSTALLED": 'INFO',
    "NO_BRIDGE": 'ERROR',
    "INCOMPATIBLE_VERSION": 'ERROR',
    "LOAD_FAILED": 'ERROR',
    "STARTUP_FAILED": 'ERROR',
}


def _wrap(layout, text, width=48):
    """2.79's labels don't wrap; split long text into lines."""
    line = ""
    for word in text.split():
        if line and len(line) + 1 + len(word) > width:
            layout.label(text=line)
            line = word
        else:
            line = (line + " " + word) if line else word
    if line:
        layout.label(text=line)


def draw_preferences(layout, context):
    col = layout.column()
    col.label(text="RTX Remix:")
    box = col.box()

    if not bge_remix.available():
        box.label(text="Not included in this build.", icon='ERROR')
        return

    from bl_operators.bge_remix import WM_OT_bge_remix_install
    busy = WM_OT_bge_remix_install.running

    code, text = bge_remix.status()
    row = box.row()
    row.label(text=code.replace("_", " ").capitalize(), icon=ICONS.get(code, 'QUESTION'))
    sub = box.column(align=True)
    _wrap(sub, text)

    box.separator()
    sub = box.column(align=True)
    sub.label(text="Runtime %s (Remix API %s)" % (pin.VERSION, pin.API_VERSION))
    sub.label(text="Folder:%s" % (" (custom)" if bge_remix.runtime_dir_is_custom() else ""))
    _wrap(sub, bge_remix.runtime_dir(), width=56)

    row = box.row(align=True)
    row.operator("wm.bge_remix_set_folder", text="Choose Folder...")
    if bge_remix.runtime_dir_is_custom():
        row.operator("wm.bge_remix_reset_folder", text="Default Folder")

    box.separator()
    if busy:
        box.label(text=WM_OT_bge_remix_install.message or "Working...", icon='TIME')
    elif not bge_remix.installed():
        box.operator("wm.bge_remix_install",
                     text="Download Runtime (%d MB)" % round(pin.SIZE / 1e6), icon='URL')
        _wrap(box.column(align=True), pin.LICENSE_NOTE)
    else:
        row = box.row(align=True)
        row.operator("wm.bge_remix_test", text="Test (5 s)", icon='RENDER_STILL')
        row.operator("wm.bge_remix_remove", text="Remove", icon='CANCEL')
