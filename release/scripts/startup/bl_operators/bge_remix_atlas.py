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

"""BGE-Remix P10.4: the atlas tools of the UV editor (its RTX Remix Atlas
panel, bl_ui/space_image_remix_atlas.py): a preset made a tile set or a
flipbook, small images packed, a tile or cell picked for the selected
faces, and the atlas checked (bge_remix.atlas does the work)."""

import bpy
from bpy.types import Operator
from bpy.props import EnumProperty, FloatProperty, IntProperty

# the last check, for the overlay to mark: {"material", "tiles": {...},
# "page": {...}, "mesh": {...}}
LAST_CHECK = {}


def _material(context):
    ob = context.edit_object or context.active_object
    return ob.active_material if ob is not None else None


class IMAGE_OT_bge_remix_tile_set(Operator):
    """Make this preset's Remix textures corner-matched tiles: no repeat """ \
        """in Remix (the game draws the preset live)"""
    bl_idname = "image.bge_remix_tile_set"
    bl_label = "Make Tile Set"
    bl_options = {'REGISTER', 'UNDO'}

    colours = EnumProperty(name="Tiles", items=(('2', "16 tiles", "Two corner colours"),
                                                ('3', "81 tiles", "Three corner colours: less repeat")),
                           default='2')
    size = IntProperty(name="Tile Size", default=1024, min=64, max=1024,
                       description="Each tile's side in pixels (81 tiles: at most 512)")

    @classmethod
    def poll(cls, context):
        ma = _material(context)
        return ma is not None and bool(ma.get("bge_remix_tile"))

    def execute(self, context):
        from bge_remix import atlas
        try:
            r = atlas.bake_tile_set(_material(context), self.size, int(self.colours))
        except ValueError as ex:
            self.report({'ERROR'}, str(ex))
            return {'CANCELLED'}
        self.report({'WARNING'} if r.get("error") else {'INFO'},
                    r.get("error") or "%d tiles, %s" % (r["tiles"], "baked" if r["baked"] else r["note"]))
        return {'FINISHED'}


class IMAGE_OT_bge_remix_flipbook(Operator):
    """Make this preset's Remix textures a looping flipbook: Remix plays it, """ \
        """so the surface moves in Remix"""
    bl_idname = "image.bge_remix_flipbook"
    bl_label = "Make Flipbook"
    bl_options = {'REGISTER', 'UNDO'}

    frames = EnumProperty(name="Frames", items=(('16', "16 (4 x 4)", ""), ('64', "64 (8 x 8)", "")), default='16')
    speed = FloatProperty(name="Speed", default=0.0, min=0.0, max=10.0, unit='VELOCITY',
                          description="How fast the pattern drifts (0: the preset's own)")

    @classmethod
    def poll(cls, context):
        ma = _material(context)
        return ma is not None and bool(ma.get("bge_remix_tile"))

    def execute(self, context):
        from bge_remix import atlas
        try:
            r = atlas.bake_flipbook(_material(context), int(self.frames), self.speed or None)
        except ValueError as ex:
            self.report({'ERROR'}, str(ex))
            return {'CANCELLED'}
        self.report({'WARNING'} if r.get("error") else {'INFO'},
                    r.get("error") or "%d frames at %d per second (a %.1f s loop), %s" % (
                        r["frames"], r["fps"], r["loop_seconds"], "baked" if r["baked"] else r["note"]))
        return {'FINISHED'}


class IMAGE_OT_bge_remix_atlas_off(Operator):
    """Remix gets this material's plain textures (a preset: its single """ \
        """tile), even where 16 tiles are the default"""
    bl_idname = "image.bge_remix_atlas_off"
    bl_label = "Single Tile"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        from bge_remix import atlas
        return atlas.atlas_of(_material(context)) is not None

    def execute(self, context):
        from bge_remix import atlas
        atlas.single_tile(_material(context))
        return {'FINISHED'}


class IMAGE_OT_bge_remix_atlas_default(Operator):
    """Back to the default: a random preset gets 16 tiles in Remix, """ \
        """anything else its plain textures"""
    bl_idname = "image.bge_remix_atlas_default"
    bl_label = "Default"
    bl_options = {'REGISTER', 'UNDO'}

    @classmethod
    def poll(cls, context):
        ma = _material(context)
        return ma is not None and "bge_remix_atlas" in ma

    def execute(self, context):
        from bge_remix import atlas
        atlas.clear(_material(context))
        return {'FINISHED'}


class IMAGE_OT_bge_remix_pack_images(Operator):
    """Pack the game's small images into shared pages for Remix (fewer """ \
        """textures; the picture stays the same)"""
    bl_idname = "image.bge_remix_pack_images"
    bl_label = "Pack Small Images"
    bl_options = {'REGISTER', 'UNDO'}

    max_size = IntProperty(name="Up To", default=256, min=8, max=1024, description="Images this size or smaller")
    page_size = IntProperty(name="Page", default=2048, min=256, max=8192)
    padding = IntProperty(name="Gap", default=8, min=1, max=64, description="Pixels round each image")

    def execute(self, context):
        from bge_remix import atlas
        images = set()
        for ma in atlas._game_materials(context.scene):
            for img in atlas._material_images(ma):
                if 0 < max(img.size) <= self.max_size and not img.name.startswith(atlas.PAGE_PREFIX):
                    images.add(img)
        r = atlas.pack_images(sorted(images, key=lambda i: i.name), self.page_size, self.padding, self.max_size,
                              scene=context.scene)
        for w in r["warnings"]:
            self.report({'WARNING'}, w)
        self.report({'INFO'}, "%d materials on %d pages (%d left out)" % (
            len(r["materials"]), len(r["pages"]), len(r["skipped"])))
        return {'FINISHED'}


class IMAGE_OT_bge_remix_pick_cell(Operator):
    """The tile (or packed cell) the selected faces show in Remix, picked """ \
        """by hand (By Position: as their place says)"""
    bl_idname = "image.bge_remix_pick_cell"
    bl_label = "Pick Tile"
    bl_options = {'REGISTER', 'UNDO'}

    cell = IntProperty(name="Tile", default=-1, min=-1, max=255, description="The tile or cell (-1: by position)")

    @classmethod
    def poll(cls, context):
        ob = context.edit_object
        return ob is not None and ob.type == 'MESH'

    def execute(self, context):
        import bmesh
        from bge_remix import atlas
        me = context.edit_object.data
        bm = bmesh.from_edit_mesh(me)
        faces = [f.index for f in bm.faces if f.select]
        if not faces:
            self.report({'WARNING'}, "No faces selected")
            return {'CANCELLED'}
        lay = atlas.atlas_of(_material(context))
        if lay is not None and self.cell >= len(lay.get("cells") or []):
            self.report({'ERROR'}, "The atlas has %d tiles or cells" % len(lay.get("cells") or []))
            return {'CANCELLED'}
        atlas.set_face_cell(me, faces, None if self.cell < 0 else self.cell)
        self.report({'INFO'}, "%d faces: %s" % (len(faces), "by position" if self.cell < 0 else
                                                "tile %d" % self.cell))
        return {'FINISHED'}


class IMAGE_OT_bge_remix_atlas_check(Operator):
    """Check the atlas: each cell's gap, where cells meet, the faces' picks """ \
        """(what fails is marked in red and its faces selected)"""
    bl_idname = "image.bge_remix_atlas_check"
    bl_label = "Check Atlas"

    @classmethod
    def poll(cls, context):
        from bge_remix import atlas
        return atlas.atlas_of(_material(context)) is not None

    def execute(self, context):
        from bge_remix import atlas
        ma = _material(context)
        ob = context.edit_object or context.active_object
        lay = atlas.atlas_of(ma)
        LAST_CHECK.clear()
        LAST_CHECK["material"] = ma.name
        problems = []
        if lay["kind"] == "tiles":
            t = atlas.check_tiles(ma)
            LAST_CHECK["tiles"] = t
            if t is None:
                problems.append("no preview of the tiles here (Make Tile Set in the Blender window first)")
            else:
                problems += ["tile %d: its gap jumps" % g for g in t["gaps"]]
                problems += ["tiles %d and %d meet with a step (%s)" % (a, b, d) for a, b, _, d in t["edges"]]
        elif lay["kind"] == "packed":
            page = bpy.data.images.get(lay["page"])
            if page is not None:
                p = atlas.check_page(page)
                LAST_CHECK["page"] = dict(p, name=page.name)
                problems += ["cell %d: its gap does not carry the image on" % g for g in p["gaps"]]
                problems += ["cells %d and %d: their gaps meet" % (a, b) for a, b in p["overlaps"]]
        if ob is not None and ob.type == 'MESH':
            was_edit = ob.mode == 'EDIT'
            if was_edit:
                bpy.ops.object.mode_set(mode='OBJECT')
            m = atlas.check_mesh(ob, ob.active_material_index)
            LAST_CHECK["mesh"] = dict(m, object=ob.name)
            problems += ["face %d: picked tile %d is not in the atlas" % (f, m["picked"][f]) for f in m["bad_picks"]]
            problems += ["face %d: its UVs run past an image that does not repeat (stretched)" % f
                         for f in m["stretched"]]
            marked = set(m["bad_picks"]) | set(m["stretched"])
            if marked:
                for p in ob.data.polygons:
                    p.select = p.index in marked
            if was_edit:
                bpy.ops.object.mode_set(mode='EDIT')
        LAST_CHECK["problems"] = problems
        for text in problems[:12]:
            self.report({'WARNING'}, text)
        if not problems:
            self.report({'INFO'}, "The atlas is fine")
        for area in context.screen.areas:
            if area.type == 'IMAGE_EDITOR':
                area.tag_redraw()
        return {'FINISHED'}


class IMAGE_OT_bge_remix_atlas_show(Operator):
    """Show the atlas's cells over the UVs (tiles: each square's tile)"""
    bl_idname = "image.bge_remix_atlas_show"
    bl_label = "Show Cells"

    def execute(self, context):
        sc = context.scene
        sc["bge_remix_atlas_show"] = 0 if sc.get("bge_remix_atlas_show") else 1
        for area in context.screen.areas:
            if area.type == 'IMAGE_EDITOR':
                area.tag_redraw()
        return {'FINISHED'}


classes = (
    IMAGE_OT_bge_remix_tile_set,
    IMAGE_OT_bge_remix_flipbook,
    IMAGE_OT_bge_remix_atlas_off,
    IMAGE_OT_bge_remix_atlas_default,
    IMAGE_OT_bge_remix_pack_images,
    IMAGE_OT_bge_remix_pick_cell,
    IMAGE_OT_bge_remix_atlas_check,
    IMAGE_OT_bge_remix_atlas_show,
)
