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

"""BGE-Remix P10.11: the logic's states in the Remix View's Edit mode.

The Starting States panel shows the selected object's starting states
(object.game.states_initial) and which states its logic uses, and sets them.
It shows in Edit mode only (space.game_view_mode 'EDIT', P10.6).

BGE-Remix P10.13: the logic's options, kept as ID properties (an old file has
none: stock 2.79b's behaviour; stock 2.79b keeps them and ignores them):
  object.bge_logic_priority    (A4) objects with a higher number run their
                               controllers first in a tic; 0 keeps stock's order
  scene.bge_physics_steps_own  (A7) Max Physics Steps works as labelled (on
                               for new scenes and files)
  scene.bge_timers_game_time   (A6) timers count a tic's game time when the
                               game runs slow
The logic editor's Logic Order panel shows the object's priority; the game
physics panel (properties_game.py) the scene's two options.
"""

import bpy
from bpy.types import Panel

try:
    import _bge_gameview as gv
except ImportError:
    gv = None

STATES = 30


def selected_object(context):
    """the object of the Remix View's selected element, else the active one"""
    if gv is not None:
        m = gv.model()
        if m is not None and m["selected"] >= 0:
            el = gv.element(m["selected"])
            if el is not None and el["owner"]:
                ob = bpy.data.objects.get(el["owner"])
                if ob is not None:
                    return ob
    return context.active_object


def used_states(ob):
    """the states (1-30) the object's controllers are in, and those its State
    actuators set"""
    # (a controller's "states" is the number of its first state, 1-30;
    # P10.13: state_mask holds every state it is in)
    used = set()
    for c in ob.game.controllers:
        if hasattr(c, "state_mask"):
            used.update(i + 1 for i in range(STATES) if c.state_mask[i])
        else:
            used.add(c.states)
    entered = set()
    for a in ob.game.actuators:
        if a.type == 'STATE' and a.operation != 'REMOVE':
            entered.update(i + 1 for i in range(STATES) if a.states[i])
    return sorted(used), sorted(entered)


def state_list(states):
    return ", ".join("%d" % s for s in states) if states else "none"


def draw_logic_priority(layout, ob):
    """BGE-Remix P10.13 (A4)"""
    if ob is not None and hasattr(ob, "bge_logic_priority"):
        layout.prop(ob, "bge_logic_priority")


def draw_scene_logic_options(layout, scene):
    """BGE-Remix P10.13 (A6, A7): drawn by the game physics panel"""
    if hasattr(scene, "bge_physics_steps_own"):
        col = layout.column()
        col.prop(scene, "bge_physics_steps_own")
        col.prop(scene, "bge_timers_game_time")


class LOGIC_PT_bge_remix_order(Panel):
    """BGE-Remix P10.13 (A4): the active object's logic priority"""
    bl_space_type = 'LOGIC_EDITOR'
    bl_region_type = 'UI'
    bl_label = "Logic Order"

    @classmethod
    def poll(cls, context):
        return context.active_object is not None

    @classmethod
    def register(cls):
        from bpy.props import BoolProperty, IntProperty
        bpy.types.Object.bge_logic_priority = IntProperty(
            name="Logic Priority",
            description="Objects with a higher number run their controllers first in each logic tic "
                        "(0: the usual order, as stock 2.79b)",
            default=0, min=-1000, max=1000)
        bpy.types.Scene.bge_physics_steps_own = BoolProperty(
            name="Own Max Physics Steps",
            description="Max Physics Steps limits the physics steps a frame, as labelled (off: stock 2.79b "
                        "uses Max Logic Steps for it; new files have it on)",
            default=False)
        bpy.types.Scene.bge_timers_game_time = BoolProperty(
            name="Timers Follow Game Time",
            description="When the game runs slower than its logic rate, timers, Timer sensors and added "
                        "objects' life count the game time each logic step stands for (off: one step's time, "
                        "as stock 2.79b)",
            default=False)

    @classmethod
    def unregister(cls):
        for owner, name in ((bpy.types.Object, "bge_logic_priority"), (bpy.types.Scene, "bge_physics_steps_own"),
                            (bpy.types.Scene, "bge_timers_game_time")):
            if hasattr(owner, name):
                delattr(owner, name)

    def draw(self, context):
        layout = self.layout
        ob = context.active_object
        layout.label(text=ob.name, icon='OBJECT_DATA')
        draw_logic_priority(layout, ob)


class GAMEVIEW_PT_logic_states(Panel):
    """BGE-Remix P10.11: the selected object's starting states (Edit mode)"""
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_label = "Starting States"

    @classmethod
    def poll(cls, context):
        view = context.space_data
        return (view is not None and view.type == 'VIEW_3D' and getattr(view, "is_game_view", False) and
                getattr(view, "game_view_mode", "") == 'EDIT' and selected_object(context) is not None)

    def draw(self, context):
        layout = self.layout
        ob = selected_object(context)
        game = ob.game
        layout.label(text=ob.name, icon='OBJECT_DATA')
        layout.label(text="The states it starts in:")
        used, entered = used_states(ob)
        # (three rows of ten, as the logic editor's state buttons)
        col = layout.column(align=True)
        for row_start in range(0, STATES, 10):
            row = col.row(align=True)
            for i in range(row_start, row_start + 10):
                row.prop(game, "states_initial", index=i, text="%d" % (i + 1), toggle=True)
        col = layout.column()
        col.label(text="Its bricks are in states: " + state_list(used))
        col.label(text="State actuators enter: " + state_list(entered))
        never = [s for s in used if s not in entered and not game.states_initial[s - 1]]
        if never:
            col.label(text="Never entered: " + state_list(never), icon='ERROR')
        draw_logic_priority(layout, ob)  # BGE-Remix P10.13 (A4)


classes = (
    LOGIC_PT_bge_remix_order,
    GAMEVIEW_PT_logic_states,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
