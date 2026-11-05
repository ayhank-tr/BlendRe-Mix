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
    # (a controller's "states" is the number of its state, 1-30)
    used = set(c.states for c in ob.game.controllers)
    entered = set()
    for a in ob.game.actuators:
        if a.type == 'STATE' and a.operation != 'REMOVE':
            entered.update(i + 1 for i in range(STATES) if a.states[i])
    return sorted(used), sorted(entered)


def state_list(states):
    return ", ".join("%d" % s for s in states) if states else "none"


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


classes = (
    GAMEVIEW_PT_logic_states,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
