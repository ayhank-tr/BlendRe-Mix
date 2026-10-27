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

# BGE-Remix (P5): the "RTX Remix" render engine.
#
# - A Blender Game scene whose Rasterizer is RTX Remix uses it for Rendered
#   shading in the 3D view and for F12 (RE_engine_id_scene in
#   source/blender/render), while it stays a Blender Game scene.
# - "RTX Remix" in the render engine list does the same for any scene; it
#   shows the game's panels (so P still starts the game, drawn by Remix), the
#   lamp, world and material panels Remix reads, and F12's render panels.
#
# The work is done in C++ (source/blender/remix/intern/remix_view.cc) through
# the _bge_remix module, as Cycles does with _cycles. Python 3.5: no f-strings.

import bpy

try:
    import _bge_remix
except ImportError:  # a build without WITH_REMIX
    _bge_remix = None


class RemixRenderEngine(bpy.types.RenderEngine):
    bl_idname = 'RTX_REMIX'
    bl_label = "RTX Remix"
    bl_use_preview = False
    bl_use_shading_nodes = False
    bl_use_exclude_layers = False
    bl_use_save_buffers = False

    def __init__(self):
        self._remix_ptr = 0
        self._remix_failed = False

    def __del__(self):
        # the RNA pointer may be gone here: the address was kept
        ptr = getattr(self, "_remix_ptr", 0)
        if ptr and _bge_remix is not None:
            _bge_remix.engine_free(ptr)

    def _ptr(self):
        if not self._remix_ptr:
            self._remix_ptr = self.as_pointer()
        return self._remix_ptr

    # F12: the scene is read in update() (Blender's interface is locked then),
    # the frames are rendered in render()
    def update(self, data, scene):
        self._remix_failed = False
        ok, message = _bge_remix.render_sync(self._ptr(), data.as_pointer(), scene.as_pointer())
        if not ok:
            self._remix_failed = True
            self.report({'ERROR'}, message)

    def render(self, scene):
        if self._remix_failed:
            return
        ok, message = _bge_remix.render_frame(self._ptr(), scene.as_pointer())
        if not ok:
            self.report({'ERROR'}, message)

    # Rendered shading in the 3D view
    def view_update(self, context):
        _bge_remix.view_update(self._ptr(), context.blend_data.as_pointer(), context.scene.as_pointer(),
                               context.region.as_pointer(), context.space_data.as_pointer(),
                               context.region_data.as_pointer())

    def view_draw(self, context):
        again = _bge_remix.view_draw(self._ptr(), context.blend_data.as_pointer(), context.scene.as_pointer(),
                                     context.region.as_pointer(), context.space_data.as_pointer(),
                                     context.region_data.as_pointer())
        if again:
            # Remix is refining its picture, or textures are on their way
            self.tag_redraw()


class RENDER_PT_remix(bpy.types.Panel):
    """The RTX Remix engine's settings (the same as a game scene's Rasterizer panel)."""
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "render"
    bl_label = "RTX Remix"
    COMPAT_ENGINES = {'RTX_REMIX'}

    @classmethod
    def poll(cls, context):
        return context.scene.render.engine in cls.COMPAT_ENGINES

    def draw(self, context):
        from bl_ui.properties_game import RENDER_PT_game_remix
        layout = self.layout
        RENDER_PT_game_remix.draw_remix_settings(layout, context.scene)
        try:
            import bge_remix
            ok = bge_remix.available() and bge_remix.installed()
        except Exception:
            ok = False
        if not ok:
            layout.label("RTX Remix is not installed: Preferences > System > RTX Remix", icon='ERROR')


# The panels the engine shows besides its own: every panel of Blender Game
# (the game's settings, physics, logic, lamps, world, materials, textures),
# and F12's render, size and output panels of Blender Render.
_RENDER_PANELS = {"RENDER_PT_render", "RENDER_PT_dimensions", "RENDER_PT_stamp", "RENDER_PT_output",
                  "RENDER_PT_encoding"}
_EXCLUDE_PANELS = {"RENDER_PT_game_remix"}  # the engine always renders with Remix


def _panels():
    for panel in bpy.types.Panel.__subclasses__():
        engines = getattr(panel, "COMPAT_ENGINES", None)
        if not isinstance(engines, set) or panel.__name__ in _EXCLUDE_PANELS:
            continue
        if 'BLENDER_GAME' in engines or panel.__name__ in _RENDER_PANELS:
            yield panel


_added = []


def register():
    if _bge_remix is None:
        return
    bpy.utils.register_class(RemixRenderEngine)
    bpy.utils.register_class(RENDER_PT_remix)
    for panel in _panels():
        if 'RTX_REMIX' not in panel.COMPAT_ENGINES:
            panel.COMPAT_ENGINES.add('RTX_REMIX')
            _added.append(panel)


def unregister():
    if _bge_remix is None:
        return
    for panel in _added:
        panel.COMPAT_ENGINES.discard('RTX_REMIX')
    del _added[:]
    bpy.utils.unregister_class(RENDER_PT_remix)
    bpy.utils.unregister_class(RemixRenderEngine)
