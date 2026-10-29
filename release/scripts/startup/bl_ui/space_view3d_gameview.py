# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>

# BGE-Remix P7: the Game View's header, menus, panels and operators.
# The Game View is a 3D View with is_game_view set; its model comes from the
# _bge_gameview module (BlenderRoutines/BL_GameView.cpp).

import bpy
from bpy.types import Menu, Operator, Panel
from bpy.props import FloatProperty, IntProperty, StringProperty

try:
    import _bge_gameview as gv
except ImportError:  # a build without the game engine
    gv = None


KNOWN_PROP = "_gameview_known"
TARGETS_PROP = "_gameview_targets"

# the layers in their clusters, as the header shows them
CLUSTERS = (
    ("Entity", ("OBJECTS", "CAMERAS", "INTERFACE")),
    ("Factor", ("RENDERING", "PHYSICS", "MEDIA", "DATA", "FLUIDS", "ARTICULATIONS")),
    ("Behaviour", ("LOGIC", "PYTHON", "ANIMATION", "MESSAGES", "NAVIGATION", "DESTRUCTION")),
    ("Gate", ("INPUT", "SCENES", "REMIX", "PERFORMANCE", "TIME", "LEFT_OUT", "UNASSIGNED")),
)

STATE_ICONS = {
    "RED": 'COLORSET_01_VEC',
    "BLACK": 'COLORSET_10_VEC',
    "WHITE": 'COLORSET_13_VEC',
    "GREEN": 'COLORSET_03_VEC',
    "BLUE": 'COLORSET_04_VEC',
    "YELLOW": 'COLORSET_09_VEC',
}
STATE_NAMES = {
    "RED": "Red: unavailable",
    "BLACK": "Black: available, not set up",
    "WHITE": "White: ready",
    "GREEN": "Green: active",
    "BLUE": "Blue: start / continue",
    "YELLOW": "Yellow: wait / stop",
}
LEVEL_ICONS = {"RED": 'CANCEL', "BLACK": 'ERROR', "NOTE": 'INFO'}

TARGET_FIELDS = (
    ("fps", "Frame rate", "FPS"),
    ("objects", "Objects", ""),
    ("lights", "Lights", ""),
    ("sounds", "Sounds", ""),
    ("bodies", "Physics bodies", ""),
    ("triangles", "Triangles", ""),
    ("texture_mb", "Texture memory", "MB"),
    ("shadow_size", "Shadow map size", "px"),
)

TARGET_PRESETS = {
    "RTX_3080_60": ("RTX 3080, 60 FPS",
                    dict(fps=60, objects=10000, lights=8, sounds=32, bodies=2000, triangles=2000000,
                         texture_mb=4000.0, shadow_size=2048)),
    "GTX_1060_30": ("GTX 1060, 30 FPS",
                    dict(fps=30, objects=3000, lights=4, sounds=24, bodies=500, triangles=500000,
                         texture_mb=2000.0, shadow_size=1024)),
}
TARGET_DEFAULTS = TARGET_PRESETS["RTX_3080_60"][1]


def is_game_view(context):
    view = context.space_data
    return gv is not None and view is not None and view.type == 'VIEW_3D' and view.is_game_view


def scene_uses_remix(scene):
    engine = scene.render.engine
    return engine == 'RTX_REMIX' or (engine == 'BLENDER_GAME' and scene.get("bge_remix", 0) == 1)


def targets(scene):
    t = dict(TARGET_DEFAULTS)
    group = scene.get(TARGETS_PROP)
    if group is not None:
        for key in t:
            if key in group:
                t[key] = group[key]
    return t


def known_keys(scene):
    return list(scene.get(KNOWN_PROP, []))


def redraw_game_views(context):
    for window in context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'GAME_VIEW':
                area.tag_redraw()


# -----------------------------------------------------------------------------
# operators (the C ones: view3d.gameview_select, _frame, _refresh)

class GAMEVIEW_OT_mark_known(Operator):
    """Mark a warning as known: it stays listed, but no longer warns"""
    bl_idname = "gameview.mark_known"
    bl_label = "Mark as Known"
    bl_options = {'REGISTER', 'UNDO'}

    key = StringProperty(name="Warning", description="The warning (code|owner|name)")

    def execute(self, context):
        scene = context.scene
        keys = known_keys(scene)
        if self.key and self.key not in keys:
            keys.append(self.key)
            scene[KNOWN_PROP] = keys
        gv.tag_dirty()
        redraw_game_views(context)
        return {'FINISHED'}


class GAMEVIEW_OT_unmark_known(Operator):
    """The warning warns again"""
    bl_idname = "gameview.unmark_known"
    bl_label = "Unmark Known"
    bl_options = {'REGISTER', 'UNDO'}

    key = StringProperty(name="Warning", description="The warning (code|owner|name)")

    def execute(self, context):
        scene = context.scene
        keys = [k for k in known_keys(scene) if k != self.key]
        scene[KNOWN_PROP] = keys
        gv.tag_dirty()
        redraw_game_views(context)
        return {'FINISHED'}


class GAMEVIEW_OT_targets_preset(Operator):
    """Set every target from a preset"""
    bl_idname = "gameview.targets_preset"
    bl_label = "Targets Preset"
    bl_options = {'REGISTER', 'UNDO'}

    preset = StringProperty(name="Preset")

    def execute(self, context):
        if self.preset not in TARGET_PRESETS:
            return {'CANCELLED'}
        context.scene[TARGETS_PROP] = dict(TARGET_PRESETS[self.preset][1])
        gv.tag_dirty()
        redraw_game_views(context)
        return {'FINISHED'}


class GAMEVIEW_OT_targets_edit(Operator):
    """Change the targets the Game View checks the game against (none of them is a limit of the engine)"""
    bl_idname = "gameview.targets_edit"
    bl_label = "Edit Targets"
    bl_options = {'REGISTER', 'UNDO'}

    fps = IntProperty(name="Frame rate", min=1, max=1000)
    objects = IntProperty(name="Objects", min=1)
    lights = IntProperty(name="Lights", min=0)
    sounds = IntProperty(name="Sounds", min=0)
    bodies = IntProperty(name="Physics bodies", min=0)
    triangles = IntProperty(name="Triangles", min=1)
    texture_mb = FloatProperty(name="Texture memory (MB)", min=1.0)
    shadow_size = IntProperty(name="Shadow map size", min=64, max=16384)

    def invoke(self, context, event):
        for key, value in targets(context.scene).items():
            setattr(self, key, value)
        return context.window_manager.invoke_props_dialog(self)

    def execute(self, context):
        context.scene[TARGETS_PROP] = {key: getattr(self, key) for key, _n, _u in TARGET_FIELDS}
        gv.tag_dirty()
        redraw_game_views(context)
        return {'FINISHED'}


def _step_warning(context, step):
    warnings = [w for w in gv.warnings() if not w["known"]]
    if not warnings:
        return None
    sel = gv.model()["selected"]
    elements = sorted({w["element"] for w in warnings})
    if sel in elements:
        i = (elements.index(sel) + step) % len(elements)
    else:
        i = 0 if step > 0 else len(elements) - 1
    return elements[i]


class GAMEVIEW_OT_next_warning(Operator):
    """Go to the next (or previous) element with a warning"""
    bl_idname = "gameview.next_warning"
    bl_label = "Next Warning"

    previous = bpy.props.BoolProperty(name="Previous")

    @classmethod
    def poll(cls, context):
        return is_game_view(context)

    def execute(self, context):
        el = _step_warning(context, -1 if self.previous else 1)
        if el is None:
            self.report({'INFO'}, "No warnings")
            return {'CANCELLED'}
        return bpy.ops.view3d.gameview_frame(element=el)


class GAMEVIEW_OT_select_owner(Operator):
    """Select the object the element belongs to"""
    bl_idname = "gameview.select_owner"
    bl_label = "Select Object"
    bl_options = {'REGISTER', 'UNDO'}

    owner = StringProperty(name="Object")

    def execute(self, context):
        ob = context.scene.objects.get(self.owner)
        if ob is None:
            return {'CANCELLED'}
        for o in context.selected_objects:
            o.select = False
        ob.select = True
        context.scene.objects.active = ob
        return {'FINISHED'}


# -----------------------------------------------------------------------------
# header and menus

class GAMEVIEW_MT_view(Menu):
    bl_label = "View"

    def draw(self, context):
        layout = self.layout
        layout.operator("view3d.gameview_refresh", icon='FILE_REFRESH')
        layout.separator()
        layout.prop(context.space_data, "game_view_dim")
        layout.prop(context.space_data, "game_view_labels")
        layout.separator()
        layout.operator("view3d.properties", icon='MENU_PANEL')
        layout.operator("view3d.toolshelf", icon='MENU_PANEL')
        layout.separator()
        layout.operator("view3d.view_selected").use_all_regions = False
        layout.operator("view3d.view_all").center = False
        layout.operator("view3d.viewnumpad", text="Camera").type = 'CAMERA'
        layout.separator()
        layout.operator("screen.area_dupli")
        layout.operator("screen.screen_full_area")


class GAMEVIEW_MT_select(Menu):
    bl_label = "Select"

    def draw(self, context):
        layout = self.layout
        layout.operator("gameview.next_warning", text="Next Warning").previous = False
        layout.operator("gameview.next_warning", text="Previous Warning").previous = True
        layout.separator()
        layout.operator("view3d.gameview_select", text="None").element = -1


def draw_game_view_header(self, context):
    layout = self.layout
    view = context.space_data
    scene = context.scene

    row = layout.row(align=True)
    row.template_header()

    sub = row.row(align=True)
    sub.menu("GAMEVIEW_MT_view")
    sub.menu("GAMEVIEW_MT_select")
    sub.menu("INFO_MT_add", text="Add")

    # the layers, by cluster
    for _cluster, layers in CLUSTERS:
        sub = layout.row(align=True)
        for layer in layers:
            sub.prop_enum(view, "game_view_layers", layer, text="")

    row = layout.row(align=True)
    row.prop(view, "game_view_background", expand=True)
    if view.game_view_background == 'REMIX' and not scene_uses_remix(scene):
        row.label(text="Scene not set to RTX Remix", icon='ERROR')

    row = layout.row(align=True)
    row.prop(view, "game_view_dim", text="", icon='LAMP_HEMI')
    row.prop(view, "game_view_labels", text="", icon='FONT_DATA')
    row.operator("view3d.gameview_refresh", text="", icon='FILE_REFRESH')

    m = gv.model()
    if m is not None:
        total = sum(m["warnings_per_layer"].values())
        text = "%d elements, %d warnings" % (m["elements"], total)
        if m["dirty"]:
            text += " (out of date)"
        layout.label(text=text, icon='ERROR' if total else 'NONE')


# -----------------------------------------------------------------------------
# panels (the N panel of a Game View)

class GameViewPanel:
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'

    @classmethod
    def poll(cls, context):
        return is_game_view(context) and gv.model() is not None


def draw_warning_row(layout, w):
    row = layout.row(align=True)
    icon = 'FILE_TICK' if w["known"] else LEVEL_ICONS.get(w["level"], 'INFO')
    op = row.operator("view3d.gameview_frame", text=w["text"], icon=icon, emboss=False)
    op.element = w["element"]
    if w["known"]:
        row.operator("gameview.unmark_known", text="", icon='X').key = w["key"]
    else:
        row.operator("gameview.mark_known", text="", icon='FILE_TICK').key = w["key"]


class GAMEVIEW_PT_element(GameViewPanel, Panel):
    bl_label = "Element"

    def draw(self, context):
        layout = self.layout
        m = gv.model()
        el = gv.element(m["selected"]) if m["selected"] >= 0 else None
        if el is None:
            layout.label(text="Click an avatar to see it here")
            return
        col = layout.column()
        col.label(text=el["name"], icon=STATE_ICONS.get(el["state"], 'NONE'))
        col.label(text="Kind: " + el["kind"])
        col.label(text="Layer: " + el["layer"].replace("_", " ").title())
        col.label(text="State: " + STATE_NAMES.get(el["state"], el["state"]))
        if el["owner"]:
            row = col.row()
            row.label(text="Object: " + el["owner"])
            row.operator("gameview.select_owner", text="", icon='RESTRICT_SELECT_OFF').owner = el["owner"]
        if el["info"]:
            col.label(text=el["info"])
        if el["hidden_layer"]:
            col.label(text="On a hidden layer (added by the logic)", icon='RESTRICT_VIEW_ON')
        if el["over_target"]:
            col.label(text="Over a target", icon='ERROR')
        layout.operator("view3d.gameview_frame", text="Frame", icon='ZOOM_SELECTED').element = el["index"]
        if el["warnings"]:
            box = layout.box()
            warnings = gv.warnings()
            for i in el["warnings"]:
                draw_warning_row(box, warnings[i])


class GAMEVIEW_PT_warnings(GameViewPanel, Panel):
    bl_label = "Warnings"

    LIMIT = 300

    def draw(self, context):
        layout = self.layout
        warnings = gv.warnings()
        by_layer = {}
        for w in warnings:
            by_layer.setdefault(w["layer"], []).append(w)
        if not warnings:
            layout.label(text="No warnings", icon='FILE_TICK')
            return
        shown = 0
        for layer_id, name, _cluster in gv.layers():
            ws = by_layer.get(layer_id)
            if not ws:
                continue
            active = [w for w in ws if not w["known"]]
            known = len(ws) - len(active)
            box = layout.box()
            label = "%s: %d" % (name, len(active))
            if known:
                label += " (%d known)" % known
            box.label(text=label)
            # unknown first, the most serious first
            order = {"RED": 0, "BLACK": 1, "NOTE": 2}
            for w in sorted(ws, key=lambda w: (w["known"], order.get(w["level"], 3))):
                if shown >= self.LIMIT:
                    break
                draw_warning_row(box, w)
                shown += 1
        if shown >= self.LIMIT:
            layout.label(text="%d more" % (len(warnings) - shown))


class GAMEVIEW_PT_targets(GameViewPanel, Panel):
    bl_label = "Targets"
    bl_options = {'DEFAULT_CLOSED'}

    def draw(self, context):
        layout = self.layout
        t = targets(context.scene)
        col = layout.column(align=True)
        for key, name, unit in TARGET_FIELDS:
            value = t[key]
            text = ("%g" % value) if isinstance(value, float) else str(value)
            col.label(text="%s: %s %s" % (name, text, unit))
        row = layout.row(align=True)
        for preset, (name, _values) in sorted(TARGET_PRESETS.items()):
            row.operator("gameview.targets_preset", text=name).preset = preset
        layout.operator("gameview.targets_edit", icon='SETTINGS')


class GAMEVIEW_PT_model(GameViewPanel, Panel):
    bl_label = "Layers"
    bl_options = {'DEFAULT_CLOSED'}

    def draw(self, context):
        layout = self.layout
        m = gv.model()
        view = context.space_data
        layout.label(text="Scene: %s" % m["scene"])
        if not m["converted"]:
            layout.label(text="Not a game scene: checks only", icon='INFO')
        layout.label(text="Built in %.0f ms (converter %.0f ms)" % (m["build_ms"], m["convert_ms"]))
        names = {layer_id: name for layer_id, name, _c in gv.layers()}
        for cluster, layers in CLUSTERS:
            box = layout.box()
            box.label(text=cluster)
            col = box.column(align=True)
            for layer in layers:
                row = col.row(align=True)
                row.prop_enum(view, "game_view_layers", layer, text=names.get(layer, layer))
                warn = m["warnings_per_layer"].get(layer, 0)
                row.label(text="%d" % m["counts"].get(layer, 0) + ("  (%d warnings)" % warn if warn else ""))


classes = (
    GAMEVIEW_OT_mark_known,
    GAMEVIEW_OT_unmark_known,
    GAMEVIEW_OT_targets_preset,
    GAMEVIEW_OT_targets_edit,
    GAMEVIEW_OT_next_warning,
    GAMEVIEW_OT_select_owner,
    GAMEVIEW_MT_view,
    GAMEVIEW_MT_select,
    GAMEVIEW_PT_element,
    GAMEVIEW_PT_warnings,
    GAMEVIEW_PT_targets,
    GAMEVIEW_PT_model,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
