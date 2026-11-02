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

# BGE-Remix P8.25: the property panels in a game scene. Options that do
# nothing in any of the game's renderers are hidden there (they keep their
# values, and Python still reaches them); options that work in only some of
# them carry a small mark. The renderers: the game's OpenGL drawing, F12
# (Blender Internal) and RTX Remix (the game with Remix on, the 3D view's
# Remix View and F12 with the RTX Remix engine).

GAME_ENGINES = {'BLENDER_GAME', 'RTX_REMIX'}

OPENGL_ONLY = "OpenGL only"
REMIX_ONLY = "Remix only"
F12_ONLY = "F12 only"
NOT_REMIX = "Not in Remix"
F12_REMIX = "F12 and Remix only"

# texture types neither the game nor Remix draws
GAME_NO_TEXTURE_TYPES = {'VOXEL_DATA', 'POINT_DENSITY', 'OCEAN'}


def is_game(context):
    """A game scene: the Blender Game or RTX Remix engine."""
    scene = getattr(context, "scene", None)
    return scene is not None and scene.render.engine in GAME_ENGINES


def mark(layout, text):
    """A small mark after an option or at the top of a panel."""
    row = layout.row()
    row.alignment = 'RIGHT'
    row.label(text=text, icon='DOT')


def prop_marked(layout, data, prop, text_mark, **kwargs):
    """An option with its mark beside it."""
    row = layout.row(align=True)
    row.prop(data, prop, **kwargs)
    sub = row.row()
    sub.alignment = 'RIGHT'
    sub.label(text=text_mark)
    return row
