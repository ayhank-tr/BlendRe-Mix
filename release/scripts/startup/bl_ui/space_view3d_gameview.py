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

# BGE-Remix P7: the Remix View's header, menus, panels and operators (P9.7:
# the Game View renamed, with a Check and a Debug mode).
# The Remix View is a 3D View with is_game_view set; its model comes from the
# _bge_gameview module (BlenderRoutines/BL_GameView.cpp). Check mode is P7's
# view: holograms, layers, warnings, targets. Debug mode shows RTX Remix's
# picture without the holograms: Remix's debug views, a statistics line and
# what Remix got for the active object (clicks select, as in a 3D View).
# P10.6: Edit and Run join them (space_view3d_gameview_modes.py): Edit edits
# the game through its avatars, Run plays it in the view. Each mode shows
# only its own tools.

import bpy
from bpy.types import Menu, Operator, Panel
from bpy.props import BoolProperty, EnumProperty, FloatProperty, IntProperty, StringProperty

try:
    import _bge_gameview as gv
except ImportError:  # a build without the game engine
    gv = None

try:
    import bge_remix
except ImportError:
    bge_remix = None


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

# BGE-Remix P9.7: Remix's debug views (rtx.debugView.debugViewIdx) that drew
# a picture in the P9.0 study (views 1 to 60 of runtime 1.6.1), named where
# the study could tell what they show; any other index can be typed in
DEBUG_VIEWS = (2, 3, 4, 5, 7, 8, 13, 14, 16, 17, 18, 19, 24, 25, 26, 27, 28, 30, 32, 34,
               41, 43, 44, 45, 46, 47, 48, 50, 51, 52, 54)
DEBUG_VIEW_NAMES = {
    25: "Diffuse Albedo (linear)",
    27: "Roughness (squared)",
    28: "Roughness (perceptual)",
    32: "Emissive Radiance",
    34: "Base Albedo (sRGB)",
    54: "Diffuse Albedo",
}

# Remix's display options for a debug view (rtx.debugView.*)
DISPLAY_TYPES = (
    ('STANDARD', "Standard", "The values as they are, scaled to the value range", 0),
    ('BGR', "BGR Exclusive", "Blue, green, red bands for the value range", 1),
    ('EV100', "EV100", "Light values on a log scale (the EV range)", 2),
    ('WAVEFORM', "HDR Waveform", "A waveform of the HDR values", 3),
)
FALSE_COLOURS = (
    ('NONE', "Off", "No false colour", 0),
    ('LUMINANCE', "Luminance", "The brightness through a colour spectrum", 1),
    ('RED', "Red", "The red channel through a colour spectrum", 2),
    ('GREEN', "Green", "The green channel through a colour spectrum", 3),
    ('BLUE', "Blue", "The blue channel through a colour spectrum", 4),
    ('ALPHA', "Alpha", "The alpha channel through a colour spectrum", 5),
)
MATCH_ICONS = {0: 'FILE_TICK', 1: 'INFO', 2: 'ERROR'}
MATCH_NAMES = {0: "exactly", 1: "approximately", 2: "not at all"}
# the Remix categories (bge_remix_bridge.h BRB_CATEGORY_*)
CATEGORY_NAMES = ((1, "Particle"), (2, "Particle Emitter"), (4, "Hair"), (8, "Animated Water"),
                  (16, "Terrain"), (32, "Decal (Static)"), (64, "Decal (Dynamic)"), (128, "Sky"),
                  (256, "World UI"), (512, "Beam"), (1024, "Ignore"))


def is_game_view(context):
    """a Remix View (the RNA name is P7's)"""
    view = context.space_data
    return gv is not None and view is not None and view.type == 'VIEW_3D' and view.is_game_view


def view_mode(context):
    """'CHECK', 'DEBUG' (P9.7), 'EDIT' or 'RUN' (P10.6)"""
    return context.space_data.game_view_mode


def debug_view_name(idx):
    if idx <= 0:
        return "Final Picture"
    return DEBUG_VIEW_NAMES.get(idx, "View %d" % idx)


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
    """Change the targets the Remix View checks the game against (none of them is a limit of the engine)"""
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
# BGE-Remix P9.7: Debug mode's settings (the window manager's: this Blender
# session's, not saved in the file; a game started from Blender keeps them)

def _send(key, value):
    if bge_remix is not None:
        bge_remix.debug_option(key, value)


def _enum_value(items, identifier):
    for item in items:
        if item[0] == identifier:
            return item[3]
    return 0


def send_debug_view(wm):
    _send("rtx.debugView.debugViewIdx", int(wm.remix_debug_view))


def send_display(wm):
    _send("rtx.debugView.displayType", _enum_value(DISPLAY_TYPES, wm.remix_debug_display))
    _send("rtx.debugView.minValue", float(wm.remix_debug_min))
    _send("rtx.debugView.maxValue", float(wm.remix_debug_max))
    _send("rtx.debugView.evMinValue", int(wm.remix_debug_ev_min))
    _send("rtx.debugView.evMaxValue", int(wm.remix_debug_ev_max))
    _send("rtx.debugView.pseudoColorMode", _enum_value(FALSE_COLOURS, wm.remix_debug_false_colour))
    _send("rtx.debugView.overlayOnTopOfRenderOutput", bool(wm.remix_debug_overlay))
    _send("rtx.debugView.overlayOpacity", float(wm.remix_debug_overlay_opacity))


def any_debug_view(context):
    """a Remix View in Debug mode in any window"""
    for window in context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'GAME_VIEW' and area.spaces.active.game_view_mode == 'DEBUG':
                return True
    return False


def apply_mode(context):
    """Remix's debug options for the modes the Remix Views are in: Debug
    sends the chosen debug view and turns Remix's hash collision detection
    on; with none in Debug mode, Remix shows its final picture again"""
    wm = context.window_manager
    if any_debug_view(context):
        _send("rtx.hashCollisionDetection.enable", True)
        send_display(wm)
        send_debug_view(wm)
    else:
        _send("rtx.debugView.debugViewIdx", 0)
        _send("rtx.hashCollisionDetection.enable", False)


def _debug_view_update(self, context):
    if any_debug_view(context):
        send_debug_view(self)
    redraw_game_views(context)


def _display_update(self, context):
    if any_debug_view(context):
        send_display(self)
    redraw_game_views(context)


def register_settings():
    wm = bpy.types.WindowManager
    wm.remix_debug_view = IntProperty(
        name="Debug View", min=0, max=1000, update=_debug_view_update,
        description="The RTX Remix debug view shown in Debug mode (rtx.debugView.debugViewIdx; 0: the final picture)")
    wm.remix_debug_display = EnumProperty(
        name="Display", items=DISPLAY_TYPES, update=_display_update,
        description="How the debug view shows its values (rtx.debugView.displayType)")
    wm.remix_debug_min = FloatProperty(
        name="Minimum", default=0.0, update=_display_update,
        description="The value shown as black (rtx.debugView.minValue)")
    wm.remix_debug_max = FloatProperty(
        name="Maximum", default=1.0, update=_display_update,
        description="The value shown as white (rtx.debugView.maxValue)")
    wm.remix_debug_ev_min = IntProperty(
        name="EV Minimum", default=-4, update=_display_update,
        description="The bottom of the EV100 display's range (rtx.debugView.evMinValue)")
    wm.remix_debug_ev_max = IntProperty(
        name="EV Maximum", default=4, update=_display_update,
        description="The top of the EV100 display's range (rtx.debugView.evMaxValue)")
    wm.remix_debug_false_colour = EnumProperty(
        name="False Colour", items=FALSE_COLOURS, update=_display_update,
        description="Show a value through a colour spectrum, Standard display only (rtx.debugView.pseudoColorMode)")
    wm.remix_debug_overlay = BoolProperty(
        name="Over the Picture", default=False, update=_display_update,
        description="Draw the debug view over Remix's final picture (rtx.debugView.overlayOnTopOfRenderOutput)")
    wm.remix_debug_overlay_opacity = FloatProperty(
        name="Opacity", default=1.0, min=0.0, max=1.0, subtype='FACTOR', update=_display_update,
        description="How opaque the debug view is over the picture (rtx.debugView.overlayOpacity)")


# BGE-Remix P10.6: the four modes (the identifiers are game_view_mode's)
MODES = (
    ('CHECK', "Check", "The holograms, layers, warnings and targets"),
    ('DEBUG', "Debug", "RTX Remix's picture, its debug views, statistics and inspection"),
    ('EDIT', "Edit", "Edit the game through its avatars: settings, wiring, adding and removing; the settings hub"),
    ('RUN', "Run", "Play the game here, the whole scene or a mock-up, with its live state over it"),
)


class GAMEVIEW_OT_mode(Operator):
    """Check: the holograms, layers, warnings and targets. Debug: RTX Remix's picture with its debug views, """ \
        """statistics and what Remix got for the active object. Edit: edit the game through its avatars. """ \
        """Run: play it here"""
    bl_idname = "gameview.mode"
    bl_label = "Remix View Mode"

    mode = EnumProperty(name="Mode", items=MODES)

    @classmethod
    def poll(cls, context):
        return is_game_view(context)

    def execute(self, context):
        context.space_data.game_view_mode = self.mode
        apply_mode(context)
        redraw_game_views(context)
        return {'FINISHED'}


class GAMEVIEW_OT_debug_view(Operator):
    """Show one of RTX Remix's debug views (0: the final picture)"""
    bl_idname = "gameview.debug_view"
    bl_label = "Debug View"

    index = IntProperty(name="Index", min=0, max=1000)

    def execute(self, context):
        context.window_manager.remix_debug_view = self.index
        return {'FINISHED'}


def remix_stats(context):
    """the statistics line: [(key, text, over its target)], from the 3D
    views' counters (bge_remix.view_stats, the tests' stats file); None
    without RTX Remix"""
    s = bge_remix.view_stats() if bge_remix is not None else None
    if s is None:
        return None
    t = targets(context.scene)
    draws, lights = s.get("last_draws", 0), s.get("last_lights", 0)
    waiting, ms = s.get("textures_waiting", 0), s.get("frame_ms", 0.0)
    return [
        ("draws", "%d draws" % draws, draws > t["objects"]),
        ("lights", "%d lights" % lights, lights > t["lights"]),
        ("textures_waiting", "%d textures waiting" % waiting, False),
        ("frame_ms", "%.1f ms" % ms, ms > 1000.0 / max(t["fps"], 1)),
    ]


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


class GAMEVIEW_MT_debug_view(Menu):
    bl_label = "Debug View"

    def draw(self, context):
        layout = self.layout
        current = context.window_manager.remix_debug_view
        layout.operator("gameview.debug_view", text=debug_view_name(0),
                        icon='RADIOBUT_ON' if current == 0 else 'RADIOBUT_OFF').index = 0
        layout.separator()
        for idx in DEBUG_VIEWS:
            text = "%d  %s" % (idx, DEBUG_VIEW_NAMES[idx]) if idx in DEBUG_VIEW_NAMES else "View %d" % idx
            layout.operator("gameview.debug_view", text=text,
                            icon='RADIOBUT_ON' if current == idx else 'RADIOBUT_OFF').index = idx
        layout.separator()
        layout.prop(context.window_manager, "remix_debug_view", text="Index")


class GAMEVIEW_MT_debug_display(Menu):
    bl_label = "Display"

    def draw(self, context):
        layout = self.layout
        wm = context.window_manager
        layout.prop(wm, "remix_debug_display", text="")
        layout.prop(wm, "remix_debug_min")
        layout.prop(wm, "remix_debug_max")
        layout.prop(wm, "remix_debug_ev_min")
        layout.prop(wm, "remix_debug_ev_max")
        layout.prop(wm, "remix_debug_false_colour", text="")
        layout.separator()
        layout.prop(wm, "remix_debug_overlay")
        layout.prop(wm, "remix_debug_overlay_opacity")


class GAMEVIEW_MT_remix_stats(Menu):
    """Every counter of the RTX Remix view, against the targets"""
    bl_label = "RTX Remix Statistics"

    def draw(self, context):
        layout = self.layout
        s = bge_remix.view_stats() if bge_remix is not None else None
        if not s:
            layout.label(text="RTX Remix is not available")
            return
        t = targets(context.scene)
        layout.label(text="Targets: %d FPS (%.1f ms), %d objects, %d lights" %
                     (t["fps"], 1000.0 / max(t["fps"], 1), t["objects"], t["lights"]))
        layout.separator()
        bridge = s.pop("bridge", None)
        for key in sorted(s):
            layout.label(text="%s: %s" % (key.replace("_", " "), s[key]))
        if bridge:
            layout.separator()
            layout.label(text="Remix (last frame)")
            for key in sorted(bridge):
                layout.label(text="  %s: %s" % (key.replace("_", " "), bridge[key]))


def draw_stats_line(layout, context):
    stats = remix_stats(context)
    if stats is None:
        return
    row = layout.row(align=True)
    for _key, text, over in stats:
        sub = row.row(align=True)
        sub.alert = over
        sub.menu("GAMEVIEW_MT_remix_stats", text=text)


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

    # P9.7: the mode (P10.6: four)
    mode = view.game_view_mode
    row = layout.row(align=True)
    # (2.79's layout.operator has no depress: the mode shown by its icon)
    for key, text, _desc in MODES:
        row.operator("gameview.mode", text=text,
                     icon='RADIOBUT_ON' if mode == key else 'RADIOBUT_OFF').mode = key
    # P10.9: a suspended game shows in every mode
    from . import space_view3d_gameview_modes as modes
    modes.draw_session_strip(layout, context)
    if mode == 'EDIT':
        modes.draw_edit_header(layout, context)
    elif mode == 'RUN':
        modes.draw_run_header(layout, context)
    if mode == 'DEBUG':
        wm = context.window_manager
        row = layout.row(align=True)
        row.menu("GAMEVIEW_MT_debug_view", text=debug_view_name(wm.remix_debug_view))
        row.menu("GAMEVIEW_MT_debug_display", text="", icon='SETTINGS')
        if not scene_uses_remix(scene):
            row.label(text="Scene not set to RTX Remix", icon='ERROR')
        draw_stats_line(layout, context)
        return

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
# panels (the N panel of a Remix View)

class GameViewPanel:
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    # P9.7: the modes the panel shows in
    modes = {'CHECK'}

    @classmethod
    def poll(cls, context):
        return is_game_view(context) and view_mode(context) in cls.modes and gv.model() is not None


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
    modes = {'CHECK', 'DEBUG'}

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


class DebugPanel:
    """P9.7: Debug mode's panels"""
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'

    @classmethod
    def poll(cls, context):
        return is_game_view(context) and view_mode(context) == 'DEBUG'


def _fmt(value):
    if isinstance(value, list):
        return ", ".join("%.3g" % v for v in value)
    if isinstance(value, float):
        return "%.3g" % value
    return str(value)


def category_names(bits):
    return [name for bit, name in CATEGORY_NAMES if bits & bit]


class GAMEVIEW_PT_inspect(DebugPanel, Panel):
    """What RTX Remix got for the active object (click an object to select it)"""
    bl_label = "Inspect"

    def draw(self, context):
        layout = self.layout
        ob = context.active_object
        if ob is None:
            layout.label(text="Click an object to see what Remix got for it")
            return
        r = bge_remix.inspect(ob, context.scene) if bge_remix is not None else None
        if r is None:
            layout.label(text="RTX Remix is not available in this build", icon='ERROR')
            return
        col = layout.column(align=True)
        col.label(text=r["object"], icon='OBJECT_DATA')
        if r["mesh"]:
            col.label(text="Mesh: %s, %d triangles" % (r["mesh"], r["triangles"]))
        col.label(text="Instances: %d (objects sharing its data)" % r["instances"])
        cats = 0
        for mat in r["materials"]:
            if mat.get("match"):
                cats |= mat["match"].get("categories", 0)
        col.label(text="Categories: " + (", ".join(category_names(cats)) or "none"))
        for mat in r["materials"]:
            box = layout.box()
            box.label(text="%d: %s" % (mat["slot"], mat["name"] or "(no material)"), icon='MATERIAL')
            values = mat["values"]
            sub = box.column(align=True)
            for key in ("albedo", "opacity", "roughness", "metallic", "emissive", "emissive_intensity", "ior"):
                sub.label(text="%s: %s" % (key.replace("_", " ").title(), _fmt(values[key])))
            for tex in mat["textures"]:
                name = tex["image"] or ("baked" if tex["baked"] else "")
                row = box.row()
                row.label(text="%s: %s (%s)" % (tex["field"], name, tex["state"].lower()), icon='TEXTURE')
                box.label(text="    " + tex["file"])
            match = mat.get("match")
            if match:
                sub = box.column(align=True)
                for f in match["fields"]:
                    sub.label(text="%s %s: %s" % (f["field"], MATCH_NAMES.get(f["level"], ""), f["note"]),
                              icon=MATCH_ICONS.get(f["level"], 'NONE'))
                for note in match["notes"]:
                    sub.label(text=note, icon='INFO')
        layout.label(text="Cache: " + r["cache_folder"])


class GAMEVIEW_PT_remix_stats(DebugPanel, Panel):
    bl_label = "RTX Remix Statistics"
    bl_options = {'DEFAULT_CLOSED'}

    def draw(self, context):
        GAMEVIEW_MT_remix_stats.draw(self, context)


class GAMEVIEW_PT_remaster_preview(DebugPanel, Panel):
    """BGE-Remix P10.5: the texture remaster's preview square: the chosen
    image's original and its plan's result, side by side, as Remix draws
    them"""
    bl_label = "Remaster Preview"

    def draw(self, context):
        layout = self.layout
        wm = context.window_manager
        name = getattr(wm, "bge_remix_remaster_image", "")
        if not name:
            layout.label(text="Choose an image in the Image editor's RTX Remix Remaster panel")
            return
        layout.label(text=name, icon='IMAGE_DATA')
        row = layout.row(align=True)
        row.operator("image.bge_remix_remaster_preview", text="Show", icon='VISIBLE_IPO_ON').image = name
        row.operator("image.bge_remix_remaster_preview", text="Close", icon='X').show = False
        layout.label(text="Left: the original; right: the result")


classes = (
    GAMEVIEW_OT_mark_known,
    GAMEVIEW_OT_unmark_known,
    GAMEVIEW_OT_targets_preset,
    GAMEVIEW_OT_targets_edit,
    GAMEVIEW_OT_next_warning,
    GAMEVIEW_OT_select_owner,
    GAMEVIEW_OT_mode,
    GAMEVIEW_OT_debug_view,
    GAMEVIEW_MT_view,
    GAMEVIEW_MT_select,
    GAMEVIEW_MT_debug_view,
    GAMEVIEW_MT_debug_display,
    GAMEVIEW_MT_remix_stats,
    GAMEVIEW_PT_element,
    GAMEVIEW_PT_warnings,
    GAMEVIEW_PT_targets,
    GAMEVIEW_PT_model,
    GAMEVIEW_PT_inspect,
    GAMEVIEW_PT_remix_stats,
    GAMEVIEW_PT_remaster_preview,
)

# P9.7: Debug mode's settings (the window manager is Blender's own type)
register_settings()

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
