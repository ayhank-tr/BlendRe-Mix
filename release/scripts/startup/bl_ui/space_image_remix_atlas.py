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

"""BGE-Remix P10.4: the UV editor's RTX Remix Atlas panel (sidebar) and its
drawing over the UVs: the atlas's cells (tiles: each UV square with the
tile Remix shows there, picked ones marked), what the last check found in
red. The operators: bl_operators/bge_remix_atlas.py."""

import bpy
from bpy.types import Panel

DRAW_KEY = "bge_remix_atlas_draw"


def _material(context):
    ob = context.edit_object or context.active_object
    return ob.active_material if ob is not None else None


class IMAGE_PT_bge_remix_atlas(Panel):
    bl_space_type = 'IMAGE_EDITOR'
    bl_region_type = 'UI'
    bl_label = "RTX Remix Atlas"
    bl_options = {'DEFAULT_CLOSED'}

    @classmethod
    def poll(cls, context):
        return _material(context) is not None

    def draw(self, context):
        _ensure_drawing()
        from bge_remix import atlas
        layout = self.layout
        ma = _material(context)
        lay = atlas.atlas_of(ma)
        col = layout.column(align=True)
        col.label(text="Material: " + ma.name)
        if lay is None:
            col.label(text="Remix: its plain textures")
        elif lay["kind"] == "tiles":
            col.label(text="Remix: %d tiles, %d px each%s" % (lay["tiles"], lay["pitch"],
                                                            " (the default)" if lay.get("default") else ""))
        elif lay["kind"] == "flipbook":
            col.label(text="Remix: %d frames, %d a second" % (lay["frames"], lay["fps"]))
        else:
            col.label(text="Remix: packed, cell %d of %s" % (lay["cell"], lay["page"]))
        if ma.get("bge_remix_tile"):
            row = layout.row(align=True)
            row.operator("image.bge_remix_tile_set")
            row.operator("image.bge_remix_flipbook")
        layout.operator("image.bge_remix_pack_images")
        row = layout.row(align=True)
        if lay is not None:
            row.operator("image.bge_remix_atlas_off")
        if "bge_remix_atlas" in ma:
            row.operator("image.bge_remix_atlas_default")
        layout.separator()
        row = layout.row(align=True)
        row.operator("image.bge_remix_atlas_show", text="Hide Cells" if context.scene.get("bge_remix_atlas_show")
                     else "Show Cells")
        row.operator("image.bge_remix_atlas_check")
        if context.edit_object is not None:
            box = layout.box()
            box.label(text="Selected faces show:")
            row = box.row(align=True)
            row.operator("image.bge_remix_pick_cell", text="By Position").cell = -1
            count = len(lay.get("cells") or []) if lay is not None and lay["kind"] != "flipbook" else 0
            grid = box.column_flow(columns=8, align=True)
            for k in range(min(count, 81)):
                grid.operator("image.bge_remix_pick_cell", text=str(k)).cell = k
        from bl_operators.bge_remix_atlas import LAST_CHECK
        if LAST_CHECK.get("material") == ma.name:
            problems = LAST_CHECK.get("problems") or []
            box = layout.box()
            box.label(text="Check: fine" if not problems else "Check: %d problems" % len(problems),
                      icon='FILE_TICK' if not problems else 'ERROR')
            for text in problems[:8]:
                box.label(text=text)


# ---------------------------------------------------------------------------
# drawing over the UVs

def _uv_faces(ob):
    """the object's faces as (index, material slot, [uv], selected), from
    the edit mesh when editing"""
    me = ob.data
    if ob.mode == 'EDIT':
        import bmesh
        bm = bmesh.from_edit_mesh(me)
        uvl = bm.loops.layers.uv.active
        if uvl is None:
            return []
        return [(f.index, f.material_index, [l[uvl].uv.copy() for l in f.loops], f.select) for f in bm.faces]
    uvl = me.uv_layers.active if me.uv_layers else None
    if uvl is None:
        return []
    return [(p.index, p.material_index, [uvl.data[k].uv.copy() for k in p.loop_indices], p.select)
            for p in me.polygons]


def _picks(ob):
    from bge_remix import atlas
    try:
        return atlas.face_cells(ob.data)
    except (AttributeError, ReferenceError):
        return []


def _rect(v2d, x0, y0, x1, y1, colour, width=1.0):
    import bgl
    a = v2d.view_to_region(x0, y0, clip=False)
    b = v2d.view_to_region(x1, y1, clip=False)
    bgl.glLineWidth(width)
    bgl.glColor4f(*colour)
    bgl.glBegin(bgl.GL_LINE_LOOP)
    bgl.glVertex2f(a[0], a[1])
    bgl.glVertex2f(b[0], a[1])
    bgl.glVertex2f(b[0], b[1])
    bgl.glVertex2f(a[0], b[1])
    bgl.glEnd()


def _text(v2d, x, y, text, colour):
    import bgl
    import blf
    p = v2d.view_to_region(x, y, clip=False)
    bgl.glColor4f(*colour)
    blf.size(0, 11, 72)
    blf.position(0, p[0] + 3, p[1] + 3, 0)
    blf.draw(0, text)


def _draw():
    import math
    import bgl
    from bge_remix import atlas
    try:
        from bl_operators.bge_remix_atlas import LAST_CHECK
    except ImportError:
        LAST_CHECK = {}
    context = bpy.context
    sc = context.scene
    if sc is None or not sc.get("bge_remix_atlas_show"):
        return
    ob = context.edit_object or context.active_object
    space, region = context.space_data, context.region
    if ob is None or ob.type != 'MESH' or space is None or region is None:
        return
    ma = ob.active_material
    lay = atlas.atlas_of(ma)
    if lay is None:
        return
    v2d = region.view2d
    bgl.glEnable(bgl.GL_BLEND)
    red, cyan, yellow = (1.0, 0.2, 0.15, 0.9), (0.3, 0.85, 1.0, 0.7), (1.0, 0.85, 0.2, 0.95)
    if lay["kind"] == "tiles":
        faces = [f for f in _uv_faces(ob) if f[1] == ob.active_material_index]
        picks = _picks(ob)
        squares = {}
        for index, _, uv, _ in faces:
            us, vs = [q[0] for q in uv], [q[1] for q in uv]
            for i in range(int(math.floor(min(us))), max(int(math.ceil(max(us))), int(math.floor(min(us))) + 1)):
                for j in range(int(math.floor(min(vs))), max(int(math.ceil(max(vs))), int(math.floor(min(vs))) + 1)):
                    pick = picks[index] if index < len(picks) else None
                    if (i, j) not in squares or pick is not None:
                        squares[(i, j)] = pick
            if len(squares) > 400:
                break
        bad = set(LAST_CHECK.get("tiles", {}).get("gaps", []) if LAST_CHECK.get("material") == ma.name and
                  LAST_CHECK.get("tiles") else [])
        for (i, j), pick in sorted(squares.items()):
            tile = pick if pick is not None else atlas.square_tile(i, j, lay["seed"], lay["colours"])
            colour = red if tile in bad else (yellow if pick is not None else cyan)
            _rect(v2d, i, j, i + 1, j + 1, colour, 2.0 if pick is not None else 1.0)
            _text(v2d, i, j, ("%d*" if pick is not None else "%d") % tile, colour)
    elif lay["kind"] == "packed":
        page = bpy.data.images.get(lay["page"])
        W, H = lay["size"] if lay.get("size") else (1, 1)
        if space.image is not None and space.image == page and W and H:
            # the page itself: every cell, its gap, the checked ones in red
            chk = LAST_CHECK.get("page") if LAST_CHECK.get("page", {}).get("name") == page.name else None
            marked = set((chk or {}).get("gaps", [])) | set(k for pair in (chk or {}).get("overlaps", [])
                                                         for k in pair)
            g = lay.get("gutter", 0)
            for k, (x, y, w, h, _) in enumerate(lay["cells"]):
                colour = red if k in marked else (yellow if k == lay["cell"] else cyan)
                _rect(v2d, x / W, y / H, (x + w) / W, (y + h) / H, colour, 2.0 if k == lay["cell"] else 1.0)
                if g:
                    _rect(v2d, (x - g) / W, (y - g) / H, (x + w + g) / W, (y + h + g) / H,
                          (colour[0], colour[1], colour[2], 0.35))
                _text(v2d, x / W, y / H, str(k), colour)
        else:
            # the image's own UVs: its cell is the unit square
            _rect(v2d, 0.0, 0.0, 1.0, 1.0, yellow, 2.0)
        mesh = LAST_CHECK.get("mesh") if LAST_CHECK.get("material") == ma.name else None
        if mesh and mesh.get("object") == ob.name and mesh.get("stretched"):
            _text(v2d, 0.0, 1.0, "%d faces run past the image (stretched)" % len(mesh["stretched"]), red)
    bgl.glLineWidth(1.0)
    bgl.glDisable(bgl.GL_BLEND)
    bgl.glColor4f(1.0, 1.0, 1.0, 1.0)


def _ensure_drawing():
    """the drawing, added once a session (kept over a script reload)"""
    ns = bpy.app.driver_namespace
    if ns.get(DRAW_KEY) is None:
        ns[DRAW_KEY] = bpy.types.SpaceImageEditor.draw_handler_add(_draw, (), 'WINDOW', 'POST_PIXEL')


classes = (
    IMAGE_PT_bge_remix_atlas,
)
