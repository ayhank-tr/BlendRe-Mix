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

# BGE-Remix P10.6-P10.9: the Remix View's Edit and Run modes: their
# operators, menus and panels (the plain Python is in
# release/scripts/modules/remix_view_tools.py).
#
# Edit: a click on an avatar selects its element (and makes its object the
# active one); the sidebar shows its settings as Blender's own editors draw
# them (the same RNA: a change in either shows in both, and undo works); a
# drag from one avatar to another wires them (view3d.gameview_wire_drag
# calls gameview.wire); the Element menu (W) adds and removes elements; the
# settings hub lists every setting that affects the game.
#
# Run: the game plays in the Remix View's own area, the whole scene or a
# mock-up; Suspend writes the game's places and property values into the
# file and goes to Edit mode; Continue asks Start over or Resume; Stop asks
# whether to keep the changes made while suspended.
#
# Every pop-up's answer is an operator argument (choice, keep), so the tests
# answer them; BGE_REMIX_NO_DIALOGS=1 shows none.

import os

import bpy
from bpy.app.handlers import persistent
from bpy.types import Menu, Operator, Panel
from bpy.props import EnumProperty, IntProperty, StringProperty

import remix_view_tools as rvt

try:
    import _bge_gameview as gv
except ImportError:  # a build without the game engine
    gv = None

from .space_view3d_gameview import STATE_ICONS, STATE_NAMES, draw_warning_row, is_game_view


def view_mode(context):
    return context.space_data.game_view_mode


def no_dialogs():
    return os.environ.get("BGE_REMIX_NO_DIALOGS") == "1"


def selected_element():
    if gv is None:
        return None
    m = gv.model()
    if m is None or m["selected"] < 0:
        return None
    return gv.element(m["selected"])


def set_mode(context, mode):
    area = rvt.find_game_view_area(context)
    if area is not None:
        area.spaces.active.game_view_mode = mode
        area.tag_redraw()


S = rvt.SESSION


# -----------------------------------------------------------------------------
# Edit (P10.7) operators

class EditOperator:
    @classmethod
    def poll(cls, context):
        return is_game_view(context) and view_mode(context) == 'EDIT'


class GAMEVIEW_OT_wire(EditOperator, Operator):
    """Wire two avatars: a sensor to a controller, a controller to an actuator, an actuator to the object """ \
        """it acts on, a message to its listener, a child to its parent"""
    bl_idname = "gameview.wire"
    bl_label = "Wire"
    bl_options = {'REGISTER', 'UNDO'}

    source = IntProperty(name="From", description="The element dragged from", default=-1, min=-1)
    target = IntProperty(name="To", description="The element dropped on", default=-1, min=-1)

    def execute(self, context):
        src, tgt = gv.element(self.source), gv.element(self.target)
        if src is None or tgt is None:
            self.report({'WARNING'}, "Drag from one avatar to another")
            return {'CANCELLED'}
        ok, message = rvt.wire(context.scene, src, tgt)
        self.report({'INFO'} if ok else {'WARNING'}, message)
        if not ok:
            return {'CANCELLED'}
        rvt.model_changed()
        rvt.tag_redraw(context)
        return {'FINISHED'}


ADD_WHAT = (('SENSOR', "Sensor", ""), ('CONTROLLER', "Controller", ""), ('ACTUATOR', "Actuator", ""),
            ('BODY', "Body", ""), ('SOUND', "Sound", ""))


class GAMEVIEW_OT_element_add(EditOperator, Operator):
    """Add a logic brick, a physics body or a sound to the object"""
    bl_idname = "gameview.element_add"
    bl_label = "Add Element"
    bl_options = {'REGISTER', 'UNDO'}

    owner = StringProperty(name="Object", description="The object it is added to")
    what = EnumProperty(name="What", items=ADD_WHAT)
    type = StringProperty(name="Type", description="The brick's type (ALWAYS, LOGIC_AND, MOTION...) or the "
                                                   "body's (STATIC, DYNAMIC...)")
    name = StringProperty(name="Name", description="The brick's name (empty: Blender names it)")

    def execute(self, context):
        owner = self.owner or (context.active_object.name if context.active_object else "")
        ok, message = rvt.add_element(context, owner, self.what, self.type, self.name)
        self.report({'INFO'} if ok else {'WARNING'}, message)
        if not ok:
            return {'CANCELLED'}
        rvt.model_changed()
        rvt.tag_redraw(context)
        return {'FINISHED'}


class GAMEVIEW_OT_element_remove(EditOperator, Operator):
    """Remove the element: a logic brick, a body (its object keeps no physics) or a sound"""
    bl_idname = "gameview.element_remove"
    bl_label = "Remove Element"
    bl_options = {'REGISTER', 'UNDO'}

    element = IntProperty(name="Element", default=-1, min=-1)

    def execute(self, context):
        el = gv.element(self.element)
        if el is None:
            return {'CANCELLED'}
        ok, message = rvt.remove_element(context, el)
        self.report({'INFO'} if ok else {'WARNING'}, message)
        if not ok:
            return {'CANCELLED'}
        gv.select(-1)
        rvt.model_changed()
        rvt.tag_redraw(context)
        return {'FINISHED'}


class GAMEVIEW_OT_element_menu(EditOperator, Operator):
    """The selected avatar's menu: add or remove elements"""
    bl_idname = "gameview.element_menu"
    bl_label = "Element Menu"

    def execute(self, context):
        bpy.ops.wm.call_menu(name="GAMEVIEW_MT_element")
        return {'FINISHED'}


# -----------------------------------------------------------------------------
# the settings hub (P10.8) operators

def hub_open(context):
    return set(k for k in context.window_manager.gameview_hub_open.split(";") if k)


class GAMEVIEW_OT_hub_toggle(Operator):
    """Show or hide this group of settings"""
    bl_idname = "gameview.hub_toggle"
    bl_label = "Show or Hide"

    key = StringProperty(name="Group")

    def execute(self, context):
        keys = hub_open(context)
        if self.key in keys:
            keys.discard(self.key)
        else:
            keys.add(self.key)
        context.window_manager.gameview_hub_open = ";".join(sorted(keys))
        return {'FINISHED'}


HUB_CACHE = {}
LAST_SHOW = {}


class GAMEVIEW_OT_hub_show(Operator):
    """Show where else Blender shows this setting (the Properties editor's tab)"""
    bl_idname = "gameview.hub_show"
    bl_label = "Show in Blender"

    key = StringProperty(name="Setting")

    def execute(self, context):
        row = HUB_CACHE.get(self.key)
        if row is None:
            for r in rvt.hub_rows(context):
                HUB_CACHE[r["key"]] = r
            row = HUB_CACHE.get(self.key)
        if row is None:
            return {'CANCELLED'}
        LAST_SHOW.clear()
        LAST_SHOW.update({"key": self.key, "where": list(row["where"]), "areas": 0})
        if not row["where"]:
            self.report({'INFO'}, "Only the hub shows this setting")
            return {'FINISHED'}
        tab, label = row["where"][0]
        if row["owner"]:
            ob = context.scene.objects.get(row["owner"])
            if ob is not None:
                context.scene.objects.active = ob
        shown = 0
        for window in context.window_manager.windows:
            for area in window.screen.areas:
                if area.type == 'PROPERTIES':
                    try:
                        area.spaces.active.context = rvt.TAB_IDS.get(tab, 'RENDER')
                        area.tag_redraw()
                        shown += 1
                    except TypeError:
                        pass
        LAST_SHOW["areas"] = shown
        LAST_SHOW["tab"] = rvt.TAB_IDS.get(tab, 'RENDER')
        text = rvt.where_text(row)
        self.report({'INFO'}, text if shown else text + " (no Properties editor is open)")
        return {'FINISHED'}


# -----------------------------------------------------------------------------
# Run (P10.9) operators

SCOPES = (('SCENE', "Whole Scene", "Play the whole scene"),
          ('MOCKUP', "Chosen Objects", "A mock-up: play the selected objects and what they need (their logic's "
                                       "targets, parents and children, the camera, the lamps); the rest is hidden"))


def _names(text):
    return [n.strip() for n in text.replace(",", "\n").split("\n") if n.strip()]


def _play(op, context, resume, suspend_after, shot):
    o = rvt.view_override(context)
    if o is None:
        op.report({'ERROR'}, "Run plays inside a Remix View")
        return {'CANCELLED'}
    o["area"].spaces.active.game_view_mode = 'RUN'
    S["state"] = 'RUNNING'
    try:
        ret = bpy.ops.view3d.gameview_play(o, resume=resume, mockup=S["scope"] == 'MOCKUP', keep="\n".join(S["keep"]),
                                           suspend_after=suspend_after, shot=shot)
    except RuntimeError as ex:
        ret = {'CANCELLED'}
        op.report({'ERROR'}, str(ex))
    res = gv.run_result()
    S["last"] = res
    if 'FINISHED' not in ret or res["outcome"] == 'ERROR':
        # it did not start: as before
        S["state"] = 'SUSPENDED' if S["suspends"] else 'IDLE'
        if S["state"] == 'IDLE':
            bpy.ops.view3d.gameview_file_state(o, action='FREE')
            rvt.session_reset()
        return {'CANCELLED'}
    S["frame"] += res["frames"]
    try:
        if res["outcome"] == 'SUSPEND':
            _suspended(context, o)
        else:
            _ended(context, o)
    except Exception as ex:
        # never stuck in a run that is not there: Stop (No) puts the file back
        import traceback
        traceback.print_exc()
        S["state"] = 'SUSPENDED' if res["outcome"] == 'SUSPEND' else ('ASK_KEEP' if S["suspends"] else 'IDLE')
        if S["state"] == 'IDLE':
            bpy.ops.view3d.gameview_file_state(o, action='FREE')
            rvt.session_reset()
        op.report({'ERROR'}, "Run: %s" % ex)
        return {'CANCELLED'}
    return {'FINISHED'}


def _suspended(context, o):
    scene = context.scene
    snap = gv.run_snapshot()
    if not S["file"]:
        S["file"] = rvt.read_all(scene)
    S["other_scene"] = bool(snap is None or snap["other_scene"])
    if snap is not None and not snap["other_scene"]:
        rvt.pose(scene, snap)
    S["posed"] = rvt.read_all(scene)
    S["base"] = rvt.snapshot_base(scene)
    S["suspends"] += 1
    S["state"] = 'SUSPENDED'
    o["area"].spaces.active.game_view_mode = 'EDIT'
    rvt.model_changed()
    bpy.ops.ed.undo_push(o, message="Remix View: suspended")


def _ended(context, o):
    scene = context.scene
    if S["posed"]:
        rvt.fold_edits(scene)
        rvt.unpose(scene)
    if S["suspends"]:
        S["state"] = 'ASK_KEEP'
        o["area"].spaces.active.game_view_mode = 'RUN'
        if not no_dialogs():
            context.window_manager.popup_menu(draw_keep_popup, title="Keep the changes made while suspended?",
                                              icon='QUESTION')
    else:
        _finish(context, o, True)


def _finish(context, o, keep):
    if keep:
        bpy.ops.view3d.gameview_file_state(o, action='FREE')
    else:
        bpy.ops.view3d.gameview_file_state(o, action='RESTORE')
    gv.run_forget()
    rvt.session_reset()
    rvt.model_changed()
    if keep:
        bpy.ops.ed.undo_push(o, message="Remix View: stopped")
    area = rvt.find_game_view_area(context)
    if area is not None:
        area.spaces.active.game_view_mode = 'RUN'
        area.tag_redraw()


class GAMEVIEW_OT_run(Operator):
    """Play the game here: the whole scene, or a mock-up of the selected objects and what they need. """ \
        """Pause or the Suspend button suspends it, the exit key or Stop stops it"""
    bl_idname = "gameview.run"
    bl_label = "Run"

    scope = EnumProperty(name="Play", items=SCOPES)
    objects = StringProperty(name="Objects", description="A mock-up's objects, one name per line (empty: the "
                                                         "selected objects)")
    suspend_after = IntProperty(name="Suspend After", description="Suspend by itself after this many frames "
                                "(0: only when asked)", default=0, min=0)
    shot = StringProperty(name="Picture", description="A picture of the last frame (PNG file)",
                          options={'SKIP_SAVE', 'HIDDEN'})

    @classmethod
    def poll(cls, context):
        return is_game_view(context) and S["state"] == 'IDLE'

    def invoke(self, context, event):
        self.scope = context.window_manager.gameview_run_scope
        return self.execute(context)

    def execute(self, context):
        scene = context.scene
        chosen = _names(self.objects) or [o.name for o in context.selected_objects]
        if self.scope == 'MOCKUP' and not chosen:
            self.report({'WARNING'}, "Select the objects to play")
            return {'CANCELLED'}
        o = rvt.view_override(context)
        if o is None:
            self.report({'ERROR'}, "Run plays inside a Remix View")
            return {'CANCELLED'}
        if 'FINISHED' not in bpy.ops.view3d.gameview_file_state(o, action='SAVE'):
            self.report({'ERROR'}, "The file could not be kept for Stop")
            return {'CANCELLED'}
        rvt.session_reset()
        S["scope"] = self.scope
        S["chosen"] = chosen if self.scope == 'MOCKUP' else []
        S["keep"] = sorted(rvt.mockup_objects(scene, chosen)) if self.scope == 'MOCKUP' else []
        gv.run_forget()
        return _play(self, context, False, self.suspend_after, self.shot)


CHOICES = (('ASK', "Ask", "Show the choice"),
           ('RESUME', "Resume with the Changes", "Go on from where it was suspended, with what you changed"),
           ('START_OVER', "Start Over", "Start the game again from the beginning, with what you changed"))


def draw_continue_popup(self, context):
    layout = self.layout
    offer = rvt.continue_offer(context)
    col = layout.column()
    if offer["resume"]:
        col.operator("gameview.run_continue", text="Resume with the Changes", icon='PLAY').choice = 'RESUME'
        for t in offer["takes"][:12]:
            col.label(text="    " + t)
    else:
        col.label(text="Resume cannot take these changes:", icon='ERROR')
        for r in offer["reasons"][:12]:
            col.label(text="    " + r)
    col.separator()
    col.operator("gameview.run_continue", text="Start Over", icon='FILE_REFRESH').choice = 'START_OVER'


def draw_keep_popup(self, context):
    layout = self.layout
    col = layout.column()
    col.operator("gameview.run_stop", text="Yes: keep them", icon='FILE_TICK').keep = 'YES'
    col.operator("gameview.run_stop", text="No: the file as it was when Run started", icon='LOOP_BACK').keep = 'NO'


class GAMEVIEW_OT_run_continue(Operator):
    """Continue the suspended game: Start over, or Resume with the changes"""
    bl_idname = "gameview.run_continue"
    bl_label = "Continue"

    choice = EnumProperty(name="Choice", items=CHOICES)
    suspend_after = IntProperty(name="Suspend After", default=0, min=0)
    shot = StringProperty(name="Picture", options={'SKIP_SAVE', 'HIDDEN'})

    @classmethod
    def poll(cls, context):
        return is_game_view(context) and S["state"] == 'SUSPENDED'

    def invoke(self, context, event):
        if self.choice == 'ASK' and not no_dialogs():
            context.window_manager.popup_menu(draw_continue_popup, title="Continue the game", icon='PLAY')
            return {'FINISHED'}
        return self.execute(context)

    def execute(self, context):
        scene = context.scene
        if self.choice == 'ASK':
            self.report({'WARNING'}, "Choose Start over or Resume")
            return {'CANCELLED'}
        offer = rvt.continue_offer(context)
        if self.choice == 'RESUME' and not offer["resume"]:
            self.report({'ERROR'}, "Resume cannot take these changes (%s): Start over instead" %
                        "; ".join(offer["reasons"]))
            return {'CANCELLED'}
        rvt.fold_edits(scene)
        if self.choice == 'START_OVER':
            rvt.unpose(scene)
            gv.run_forget()
        S["base"] = None
        return _play(self, context, self.choice == 'RESUME', self.suspend_after, self.shot)


KEEPS = (('ASK', "Ask", "Show the choice"),
         ('YES', "Yes", "Keep the changes made while suspended"),
         ('NO', "No", "The file as it was when Run started"))


class GAMEVIEW_OT_run_stop(Operator):
    """Stop the game; a pop-up asks whether to keep the changes made while it was suspended"""
    bl_idname = "gameview.run_stop"
    bl_label = "Stop"

    keep = EnumProperty(name="Keep the Changes", items=KEEPS)

    @classmethod
    def poll(cls, context):
        return is_game_view(context) and S["state"] in {'SUSPENDED', 'ASK_KEEP'}

    def invoke(self, context, event):
        if self.keep == 'ASK' and not no_dialogs():
            context.window_manager.popup_menu(draw_keep_popup, title="Keep the changes made while suspended?",
                                              icon='QUESTION')
            return {'FINISHED'}
        return self.execute(context)

    def execute(self, context):
        if self.keep == 'ASK':
            self.report({'WARNING'}, "Choose Yes or No")
            return {'CANCELLED'}
        o = rvt.view_override(context)
        if o is None:
            return {'CANCELLED'}
        if S["state"] == 'SUSPENDED':
            rvt.fold_edits(context.scene)
            rvt.unpose(context.scene)
        _finish(context, o, self.keep == 'YES')
        return {'FINISHED'}


# -----------------------------------------------------------------------------
# menus

def menu_owner(context):
    el = selected_element()
    if el is not None and el["owner"]:
        return el["owner"]
    return context.active_object.name if context.active_object else ""


def _add_items(layout, context, what, types):
    owner = menu_owner(context)
    for t in types:
        op = layout.operator("gameview.element_add", text=t.replace("LOGIC_", "").replace("_", " ").title())
        op.owner = owner
        op.what = what
        op.type = t


class GAMEVIEW_MT_add_sensor(Menu):
    bl_label = "Add Sensor"

    def draw(self, context):
        _add_items(self.layout, context, 'SENSOR', rvt.SENSOR_TYPES)


class GAMEVIEW_MT_add_controller(Menu):
    bl_label = "Add Controller"

    def draw(self, context):
        _add_items(self.layout, context, 'CONTROLLER', rvt.CONTROLLER_TYPES)


class GAMEVIEW_MT_add_actuator(Menu):
    bl_label = "Add Actuator"

    def draw(self, context):
        _add_items(self.layout, context, 'ACTUATOR', rvt.ACTUATOR_TYPES)


class GAMEVIEW_MT_add_body(Menu):
    bl_label = "Add Body"

    def draw(self, context):
        _add_items(self.layout, context, 'BODY', rvt.BODY_TYPES)


class GAMEVIEW_MT_element(Menu):
    """The selected avatar's menu: add or remove elements"""
    bl_label = "Element"

    def draw(self, context):
        layout = self.layout
        owner = menu_owner(context)
        if not owner:
            layout.label(text="Click an avatar (or an object) first")
            return
        layout.label(text="On " + owner, icon='OBJECT_DATA')
        layout.menu("GAMEVIEW_MT_add_sensor")
        layout.menu("GAMEVIEW_MT_add_controller")
        layout.menu("GAMEVIEW_MT_add_actuator")
        layout.menu("GAMEVIEW_MT_add_body")
        op = layout.operator("gameview.element_add", text="Add Sound", icon='SPEAKER')
        op.owner = owner
        op.what = 'SOUND'
        el = selected_element()
        if el is not None:
            what, _ob, data = rvt.element_data(context.scene, el)
            if what in {"brick", "body", "sound"} and data is not None:
                layout.separator()
                layout.operator("gameview.element_remove", text="Remove " + el["name"], icon='X').element = el["index"]


# -----------------------------------------------------------------------------
# the header's parts (space_view3d_gameview.draw_game_view_header calls them)

def draw_session_strip(layout, context):
    """a suspended game (or one waiting for its keep answer), in every mode"""
    state = S["state"]
    if state == 'SUSPENDED':
        row = layout.row(align=True)
        row.label(text="Suspended at frame %d" % S["frame"], icon='PAUSE')
        row.operator("gameview.run_continue", text="Continue...", icon='PLAY').choice = 'ASK'
        row.operator("gameview.run_stop", text="Stop...", icon='X').keep = 'ASK'
    elif state == 'ASK_KEEP':
        row = layout.row(align=True)
        row.label(text="Keep the changes made while suspended?", icon='QUESTION')
        row.operator("gameview.run_stop", text="Yes").keep = 'YES'
        row.operator("gameview.run_stop", text="No").keep = 'NO'


def draw_edit_header(layout, context):
    layout.menu("GAMEVIEW_MT_element", text="Element")


def draw_run_header(layout, context):
    if S["state"] != 'IDLE':
        return
    row = layout.row(align=True)
    row.prop(context.window_manager, "gameview_run_scope", text="")
    row.operator("gameview.run", text="Run", icon='PLAY')


# -----------------------------------------------------------------------------
# panels

def shows_in(context, modes):
    return is_game_view(context) and view_mode(context) in modes and gv is not None and gv.model() is not None


class GAMEVIEW_PT_edit_element(Panel):
    """Edit mode: the selected avatar's settings, as Blender's own editors draw them"""
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_label = "Edit Element"

    @classmethod
    def poll(cls, context):
        return shows_in(context, {'EDIT'})

    def draw(self, context):
        draw_edit_element(self.layout, context)


def draw_edit_element(layout, context):
    el = selected_element()
    if el is None:
        layout.label(text="Click an avatar to edit it")
        layout.label(text="Drag from one avatar to another to wire them")
        layout.menu("GAMEVIEW_MT_element", text="Add...")
        return
    row = layout.row()
    row.label(text=el["name"], icon=STATE_ICONS.get(el["state"], 'NONE'))
    row.menu("GAMEVIEW_MT_element", text="", icon='COLLAPSEMENU')
    col = layout.column(align=True)
    col.label(text="%s, %s" % (el["kind"], STATE_NAMES.get(el["state"], el["state"])))
    if el["owner"]:
        col.label(text="Object: " + el["owner"], icon='OBJECT_DATA')
    if el["warnings"]:
        box = layout.box()
        warnings = gv.warnings()
        for i in el["warnings"]:
            draw_warning_row(box, warnings[i])
    what, ob, data = rvt.element_data(context.scene, el)
    if data is None:
        layout.label(text="Not in the file (edit it in Blender's own editors)", icon='INFO')
        return
    box = layout.box()
    if what == "brick":
        row = box.row()
        row.prop(data, "name", text="")
        row.prop(data, "type", text="")
        rvt.draw_rna(box, data, rvt.BRICK_SKIP | {"name", "type"})
        links = []
        if hasattr(data, "controllers"):
            links = [c.name for c in data.controllers]
        elif hasattr(data, "actuators"):
            links = [a.name for a in data.actuators]
        if links:
            box.label(text="Linked to: " + ", ".join(links), icon='LINKED')
        return
    if what in {"sound", "message_send", "message_receive"}:
        rvt.draw_rna(box, data, rvt.BRICK_SKIP)
        if what == "sound" and getattr(data, "sound", None) is not None:
            sub = layout.box()
            sub.label(text="Sound: " + data.sound.name, icon='SPEAKER')
            rvt.draw_rna(sub, data.sound)
        return
    if what == "property":
        box.prop(data, "name")
        box.prop(data, "type")
        box.prop(data, "value")
        box.prop(data, "show_debug")
        return
    if what == "object":
        panels = rvt.element_panels(context, "body", ob, ob.game)
    else:
        panels = rvt.element_panels(context, what, ob, data)
    if not panels:
        rvt.draw_rna(box, data)
        return
    first = True
    for cls, ctx in panels:
        sub = box if first else layout.box()
        first = False
        sub.label(text=getattr(cls, "bl_label", "") or cls.__name__)
        rvt.draw_panel(cls, sub, ctx)


ICON_OF_TAB = {"render": 'SCENE', "scene": 'SCENE_DATA', "world": 'WORLD', "physics": 'PHYSICS',
               "object": 'OBJECT_DATA', "data": 'LAMP_DATA', "material": 'MATERIAL'}


def draw_hub_row(layout, row, data):
    line = layout.row(align=True)
    line.prop(data, row["prop"])
    if row["where"]:
        line.operator("gameview.hub_show", text="", icon=ICON_OF_TAB.get(row["where"][0][0], 'INFO'),
                      emboss=False).key = row["key"]
    else:
        line.label(text="", icon='BLANK1')


def hub_groups(rows):
    """{layer: (scene rows, {object: rows})}"""
    groups = {}
    for row in rows:
        scene_rows, by_object = groups.setdefault(row["layer"], ([], {}))
        if row["owner"]:
            by_object.setdefault(row["owner"], []).append(row)
        else:
            scene_rows.append(row)
    return groups


_HUB_ROWS = {"sig": None, "rows": []}


def hub_signature(context):
    """what the hub's rows depend on (they are made again when it changes)"""
    scene = context.scene
    gs = scene.game_settings
    obs = tuple(sorted((o.name, o.type, o.game.physics_type, o.parent.name if o.parent else "")
                       for o in scene.objects))
    return (scene.name, scene.render.engine, scene.world.name if scene.world else "", gs.render_pipeline, gs.stereo,
            gs.physics_engine, str(getattr(scene, "bge_remix", "")), scene.camera.name if scene.camera else "", obs)


def cached_hub_rows(context):
    sig = hub_signature(context)
    if sig != _HUB_ROWS["sig"]:
        _HUB_ROWS["rows"] = rvt.hub_rows(context)
        _HUB_ROWS["sig"] = sig
        HUB_CACHE.clear()
        for r in _HUB_ROWS["rows"]:
            HUB_CACHE[r["key"]] = r
    return _HUB_ROWS["rows"]


def draw_hub(layout, context, expand_all=False):
    """every setting that affects the game, by cluster and layer (expand_all:
    every group open, for the tests)"""
    wm = context.window_manager
    layout.prop(wm, "gameview_hub_filter", text="", icon='VIEWZOOM')
    filt = wm.gameview_hub_filter.lower().strip()
    opened = hub_open(context)
    rows = cached_hub_rows(context)
    if filt:
        rows = [r for r in rows if filt in r["label"].lower() or filt in r["prop"] or filt in (r["owner"] or "").lower()]
    groups = hub_groups(rows)
    for cluster, layers in rvt.CLUSTERS:
        present = [layer for layer in layers if layer in groups]
        if not present:
            continue
        box = layout.box()
        box.label(text=cluster)
        for layer in present:
            scene_rows, by_object = groups[layer]
            count = len(scene_rows) + sum(len(v) for v in by_object.values())
            is_open = expand_all or bool(filt) or layer in opened
            line = box.row()
            line.operator("gameview.hub_toggle", text="%s (%d)" % (rvt.LAYER_NAMES.get(layer, layer), count),
                          icon='TRIA_DOWN' if is_open else 'TRIA_RIGHT', emboss=False).key = layer
            if not is_open:
                continue
            col = box.column(align=True)
            for row in scene_rows:
                data = rvt.resolve_row(row)
                if data is not None:
                    draw_hub_row(col, row, data)
            for owner in sorted(by_object):
                key = layer + "|" + owner
                ob_open = expand_all or bool(filt) or key in opened
                sub = box.row()
                sub.operator("gameview.hub_toggle", text="%s (%d)" % (owner, len(by_object[owner])),
                             icon='TRIA_DOWN' if ob_open else 'TRIA_RIGHT', emboss=False).key = key
                if not ob_open:
                    continue
                col = box.column(align=True)
                for row in by_object[owner]:
                    data = rvt.resolve_row(row)
                    if data is not None:
                        draw_hub_row(col, row, data)


class GAMEVIEW_PT_settings_hub(Panel):
    """Every setting that affects the game, by cluster and layer, each once, with where else Blender shows it"""
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_label = "Settings Hub"

    @classmethod
    def poll(cls, context):
        return shows_in(context, {'EDIT'})

    def draw(self, context):
        draw_hub(self.layout, context)


class GAMEVIEW_PT_run(Panel):
    """Run mode: what plays, and how the last run went"""
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_label = "Run"

    @classmethod
    def poll(cls, context):
        return shows_in(context, {'RUN'})

    def draw(self, context):
        layout = self.layout
        wm = context.window_manager
        scene = context.scene
        if S["state"] == 'IDLE':
            layout.prop(wm, "gameview_run_scope", expand=True)
            if wm.gameview_run_scope == 'MOCKUP':
                chosen = [o.name for o in context.selected_objects]
                if not chosen:
                    layout.label(text="Select the objects to play", icon='INFO')
                else:
                    why = rvt.mockup_objects(scene, chosen)
                    box = layout.box()
                    box.label(text="Plays %d objects:" % len(why))
                    for name in sorted(why):
                        box.label(text="%s: %s" % (name, why[name]))
            layout.operator("gameview.run", text="Run", icon='PLAY').scope = wm.gameview_run_scope
            layout.label(text="Pause or Suspend: suspend; the exit key or Stop: stop")
        else:
            draw_session_strip(layout.column(), context)
            if S["scope"] == 'MOCKUP':
                layout.label(text="Mock-up of " + ", ".join(S["chosen"]))
        last = S.get("last")
        if last:
            layout.label(text="Last run: %s after %d frames" % (last["outcome"].lower(), last["frames"]))


def register_settings():
    wm = bpy.types.WindowManager
    wm.gameview_run_scope = EnumProperty(name="Play", items=SCOPES,
                                         description="What Run plays: the whole scene, or a mock-up")
    wm.gameview_hub_filter = StringProperty(name="Find", description="Show the settings whose name holds this text")
    wm.gameview_hub_open = StringProperty(name="Open Groups", description="The settings hub's open groups")


@persistent
def _load_pre(_dummy):
    """another file: the run's session and its kept file go"""
    rvt.session_reset()
    if gv is not None:
        gv.run_forget()
        gv.run_file_free()


def register_handlers():
    handlers = bpy.app.handlers.load_pre
    for h in list(handlers):
        if getattr(h, "__name__", "") == "_load_pre" and getattr(h, "__module__", "") == __name__:
            handlers.remove(h)
    handlers.append(_load_pre)


classes = (
    GAMEVIEW_OT_wire,
    GAMEVIEW_OT_element_add,
    GAMEVIEW_OT_element_remove,
    GAMEVIEW_OT_element_menu,
    GAMEVIEW_OT_hub_toggle,
    GAMEVIEW_OT_hub_show,
    GAMEVIEW_OT_run,
    GAMEVIEW_OT_run_continue,
    GAMEVIEW_OT_run_stop,
    GAMEVIEW_MT_add_sensor,
    GAMEVIEW_MT_add_controller,
    GAMEVIEW_MT_add_actuator,
    GAMEVIEW_MT_add_body,
    GAMEVIEW_MT_element,
    GAMEVIEW_PT_edit_element,
    GAMEVIEW_PT_settings_hub,
    GAMEVIEW_PT_run,
)

register_settings()
register_handlers()

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
