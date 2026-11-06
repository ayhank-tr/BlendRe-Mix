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

"""BGE-Remix P10: the texture remaster's operators (the Image editor's RTX
Remix Remaster panel, space_image.py).

Audit (P10.1) lists every image the game uses and what it needs; it changes
nothing in the file but its report, the text "BGE-Remix remaster audit".
Build Catalogue and Fit (P10.2) match an image with the presets. Each image
is then accepted, adjusted (its sliders) or rejected; Apply (P10.5) writes
the accepted ones' RTX Remix materials, keeps the originals as <name>.p8
until the file is saved twice, and bakes and encodes everything for Remix
up front. Preview shows the original and the result side by side in the
Remix View.
"""

import bpy
from bpy.app.handlers import persistent
from bpy.props import (BoolProperty, EnumProperty, FloatProperty, FloatVectorProperty, IntProperty,
                       PointerProperty, StringProperty)
from bpy.types import Operator, PropertyGroup

PLANS = (('FIT', "Fit a Preset", "Replace the image by a fitted preset (colour, roughness, normal, height)"),
         ('DERIVE', "Derive Channels", "Keep the image; add roughness, normal and height made from it "
          "(approximate), its painted light reduced"),
         ('KEEP', "Keep", "Keep the image and its material as they are"))
DECISIONS = (('NONE', "Undecided", ""), ('ACCEPT', "Accepted", "Apply writes this image's plan"),
             ('ADJUSTED', "Adjusted", "Accepted with changed settings"),
             ('REJECT', "Rejected", "Apply leaves this image as it is"))


def tool():
    from bge_remix import remaster_tool
    return remaster_tool


@persistent
def _close_preview(_dummy):
    """the preview never goes into the file"""
    try:
        tool().preview_clear()
    except Exception:
        pass


@persistent
def _forget(_dummy):
    try:
        T = tool()
        T._state["audit"] = None
        T._state["fits"] = {}
    except Exception:
        pass


def _adjusted(self, _context):
    if self.fitted and self.decision in {'NONE', 'ACCEPT'} and not self.get("_setting"):
        self.decision = 'ADJUSTED'


def _coarseness(self, context):
    """coarser: bigger features, so a smaller Scale (four times per step of 1)"""
    if self.get("_setting") or not self.fitted:
        return
    self.scale = max(0.05, self.fit_scale * 2.0 ** ((self.fit_coarseness - self.coarseness) * 4.0))


def _regularity(self, context):
    """less regular: more wear"""
    if self.get("_setting") or not self.fitted:
        return
    self.wear = min(1.0, max(0.0, self.fit_wear + (self.fit_regularity - self.regularity)))


class BGERemixRemaster(PropertyGroup):
    """BGE-Remix P10: an image's remaster settings (kept with the image)"""
    plan = EnumProperty(name="Plan", items=PLANS, default='KEEP', update=_adjusted)
    decision = EnumProperty(name="Decision", items=DECISIONS, default='NONE')
    fitted = BoolProperty(name="Fitted", default=False)
    preset = StringProperty(name="Preset")
    score = FloatProperty(name="Score", min=0.0, max=1.0, description="How close the fit is (1: the same)")
    scale = FloatProperty(name="Scale", min=0.05, max=1000.0, default=1.0, update=_adjusted,
                          description="The preset's repeats per UV unit")
    wear = FloatProperty(name="Wear", min=0.0, max=1.0, default=0.3, update=_adjusted)
    wetness = FloatProperty(name="Wetness", min=0.0, max=1.0, default=0.0, update=_adjusted)
    seed = IntProperty(name="Seed", min=0, max=1000, default=0, update=_adjusted)
    colour = FloatVectorProperty(name="Colour", subtype='COLOR', size=3, min=0.0, max=1.0, default=(0.5, 0.5, 0.5),
                                 update=_adjusted)
    tile_metres = FloatProperty(name="Tile Size", min=0.001, default=1.0, unit='LENGTH',
                                description="The metres one repeat of the image covers on its mesh")
    coarseness = FloatProperty(name="Coarseness", min=0.0, max=1.0, update=_coarseness,
                               description="Fine (0) to coarse (1): changes the preset's Scale")
    regularity = FloatProperty(name="Regularity", min=0.0, max=1.0, update=_regularity,
                               description="How evenly the pattern repeats: changes the preset's Wear")
    direction = FloatProperty(name="Direction", min=0.0, max=1.0,
                              description="How much the pattern runs one way (measured; the presets have no "
                                          "direction setting)")
    light_amount = FloatProperty(name="Painted Light", min=0.0, max=1.0, default=1.0, subtype='FACTOR',
                                 update=_adjusted, description="How much of the painted-in light is evened out "
                                 "(derived channels)")
    bake_size = EnumProperty(name="Bake Size", items=(('1024', "1K", ""), ('2048', "2K", ""), ('4096', "4K", "")),
                             default='2048', description="The size of the preset's bake for Remix")
    # what the fit found (the sliders move from it)
    fit_scale = FloatProperty(default=1.0)
    fit_wear = FloatProperty(default=0.3)
    fit_coarseness = FloatProperty(default=0.5)
    fit_regularity = FloatProperty(default=0.0)

    @classmethod
    def register(cls):
        bpy.types.Image.bge_remix_remaster = PointerProperty(type=cls)
        bpy.types.WindowManager.bge_remix_remaster_image = StringProperty(
            name="Remaster Image", description="The image the remaster's preview shows")
        if _close_preview not in bpy.app.handlers.save_pre:
            bpy.app.handlers.save_pre.append(_close_preview)
        if _forget not in bpy.app.handlers.load_post:
            bpy.app.handlers.load_post.append(_forget)

    @classmethod
    def unregister(cls):
        del bpy.types.Image.bge_remix_remaster
        del bpy.types.WindowManager.bge_remix_remaster_image
        if _close_preview in bpy.app.handlers.save_pre:
            bpy.app.handlers.save_pre.remove(_close_preview)
        if _forget in bpy.app.handlers.load_post:
            bpy.app.handlers.load_post.remove(_forget)


def store_fit(ima, fit):
    """a fit's result into the image's settings"""
    r = ima.bge_remix_remaster
    r["_setting"] = 1
    try:
        r.fitted = True
        r.preset = fit["preset"]
        r.score = fit["score"]
        r.plan = fit["plan"]
        s = fit["settings"]
        r.scale = s.get("Scale", 1.0)
        r.wear = s.get("Wear", 0.3)
        r.wetness = s.get("Wetness", 0.0)
        r.seed = int(round(s.get("Seed", 0.0)))
        r.colour = fit["colour"][:3]
        r.tile_metres = fit.get("tile_metres") or 1.0
        per = fit.get("perceptual") or {}
        r.coarseness = r.fit_coarseness = per.get("coarseness", 0.5)
        r.regularity = r.fit_regularity = min(1.0, fit.get("regularity") or 0.0)
        r.direction = per.get("direction", 0.0)
        r.fit_scale = r.scale
        r.fit_wear = r.wear
        if r.decision == 'ADJUSTED':
            r.decision = 'NONE'
    finally:
        del r["_setting"]


def decision_of(ima):
    """the image's plan as Apply writes it (the fit with the sliders' settings)"""
    r = ima.bge_remix_remaster
    d = {"plan": r.plan}
    if r.plan == 'FIT' and r.fitted:
        from bl_operators.bge_remix_presets import PRESETS
        s = {"Scale": r.scale, "Wear": r.wear, "Seed": float(r.seed)}
        if PRESETS.get(r.preset, {}).get("wet"):
            s["Wetness"] = r.wetness
        d["fit"] = {"preset": r.preset, "settings": s, "colour": list(r.colour), "tile_metres": r.tile_metres}
    return d


def _image_from(context, name):
    if name:
        return bpy.data.images.get(name)
    sima = getattr(context, "space_data", None)
    return getattr(sima, "image", None)


class IMAGE_OT_bge_remix_remaster_audit(Operator):
    """List every image the game uses, with what each needs for the best """ \
        """RTX Remix picture (changes nothing in the file)"""
    bl_idname = "image.bge_remix_remaster_audit"
    bl_label = "Audit"

    def execute(self, context):
        r = tool().remaster_audit()
        plans = [i.get("plan") for i in r["images"]]
        self.report({'INFO'}, "%d images: %d to fit, %d to derive, %d to keep; %d value problems (see the text "
                    "\"%s\")" % (len(plans), plans.count('FIT'), plans.count('DERIVE'), plans.count('KEEP'),
                                 len(r["values"]), tool().AUDIT_TEXT))
        # each image's plan from the audit, unless decided already
        for row in r["images"]:
            ima = bpy.data.images.get(row["image"])
            if ima is not None and not ima.bge_remix_remaster.fitted and ima.bge_remix_remaster.decision == 'NONE' \
                    and row.get("plan") and ima.bge_remix_remaster.plan != row["plan"]:
                ima.bge_remix_remaster["_setting"] = 1
                ima.bge_remix_remaster.plan = row["plan"]
                del ima.bge_remix_remaster["_setting"]
        return {'FINISHED'}


class IMAGE_OT_bge_remix_remaster_select(Operator):
    """Show this image"""
    bl_idname = "image.bge_remix_remaster_select"
    bl_label = "Show Image"
    bl_options = {'INTERNAL'}

    image = StringProperty()

    def execute(self, context):
        ima = bpy.data.images.get(self.image)
        sima = getattr(context, "space_data", None)
        if ima is not None and sima is not None and sima.type == 'IMAGE_EDITOR':
            sima.image = ima
        context.window_manager.bge_remix_remaster_image = self.image
        return {'FINISHED'}


class IMAGE_OT_bge_remix_remaster_catalogue(Operator):
    """Bake every preset's catalogue now (about 200 small bakes each, kept in Blender's user folder; """ \
        """Fit builds only the likely ones, on first use)"""
    bl_idname = "image.bge_remix_remaster_catalogue"
    bl_label = "Build Catalogue"

    _timer = None
    _steps = None

    @classmethod
    def poll(cls, context):
        return not bpy.app.background

    def _next(self):
        T = tool()
        while True:
            if self._gen is None:
                if not self._keys:
                    return None
                self._key = self._keys.pop(0)
                self._gen = T.catalogue_build(self._key)
            try:
                return next(self._gen)
            except StopIteration:
                self._gen = None
                self._done_keys += 1

    def modal(self, context, event):
        if event.type == 'ESC':
            context.window_manager.event_timer_remove(self._timer)
            context.window_manager.progress_end()
            self.report({'WARNING'}, "Catalogue stopped (the presets done so far are kept)")
            return {'CANCELLED'}
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        for _ in range(8):
            step = self._next()
            if step is None:
                context.window_manager.event_timer_remove(self._timer)
                context.window_manager.progress_end()
                self.report({'INFO'}, "Catalogue ready (%d presets)" % self._total_keys)
                return {'FINISHED'}
            self._count += 1
            context.window_manager.progress_update(self._count)
        if context.area:
            context.area.tag_redraw()
        return {'RUNNING_MODAL'}

    def invoke(self, context, event):
        T = tool()
        if not T.can_bake():
            self.report({'ERROR'}, "Baking needs RTX Remix support and Blender's window")
            return {'CANCELLED'}
        self._keys = T.catalogues_missing()
        if not self._keys:
            self.report({'INFO'}, "The catalogue is ready")
            return {'FINISHED'}
        self._total_keys = len(self._keys)
        self._gen = None
        self._done_keys = 0
        self._count = 0
        total = sum(len(T.catalogue_settings(k)) for k in self._keys)
        context.window_manager.progress_begin(0, total)
        self._timer = context.window_manager.event_timer_add(0.01, context.window)
        context.window_manager.modal_handler_add(self)
        return {'RUNNING_MODAL'}

    def execute(self, context):
        T = tool()
        if not T.can_bake():
            self.report({'ERROR'}, "Baking needs RTX Remix support and Blender's window")
            return {'CANCELLED'}
        T.build_all_catalogues()
        return {'FINISHED'}


class IMAGE_OT_bge_remix_remaster_fit(Operator):
    """Match the image with its likely presets (their catalogues built on first use, then a simplex search)"""
    bl_idname = "image.bge_remix_remaster_fit"
    bl_label = "Fit"

    image = StringProperty(description="The image (empty: the one shown)")
    all = BoolProperty(name="All", description="Every image whose plan is to fit a preset")

    def execute(self, context):
        T = tool()
        if T.last_audit() is None:
            T.remaster_audit()
        if self.all:
            names = [r["image"] for r in T.last_audit()["images"] if r.get("plan") == 'FIT']
        else:
            ima = _image_from(context, self.image)
            names = [ima.name] if ima is not None else []
        done = []
        wm = context.window_manager
        # (the progress: the likely presets' catalogues built on first use,
        # a few hundred bakes each time one is missing, then the fits)
        state = {"n": 0}

        def progress(key, i, total):
            state["n"] += 1
            wm.progress_update(state["n"])
            if i == 1:
                print("BGE-Remix remaster: building the %s catalogue (%d bakes, once)" % (key, total))
        wm.progress_begin(0, 10000)
        try:
            for name in names:
                ima = bpy.data.images[name]
                fit = T.fit_image(ima, T.audit_row(name), progress=progress)
                store_fit(ima, fit)
                done.append("%s: %s %.2f%s" % (name, fit["preset"], fit["score"],
                                               " (catalogue built: %s)" % ", ".join(fit["built"])
                                               if fit.get("built") else ""))
                state["n"] += 1
                wm.progress_update(state["n"])
        except RuntimeError as ex:
            self.report({'ERROR'}, str(ex))
            return {'CANCELLED'}
        finally:
            wm.progress_end()
        self.report({'INFO'}, "; ".join(done) if done else "nothing to fit")
        return {'FINISHED'}


class IMAGE_OT_bge_remix_remaster_decide(Operator):
    """Accept or reject the image's plan"""
    bl_idname = "image.bge_remix_remaster_decide"
    bl_label = "Decide"

    image = StringProperty()
    decision = EnumProperty(items=DECISIONS)

    def execute(self, context):
        ima = _image_from(context, self.image)
        if ima is None:
            return {'CANCELLED'}
        ima.bge_remix_remaster.decision = self.decision
        return {'FINISHED'}


class IMAGE_OT_bge_remix_remaster_preview(Operator):
    """Show the original and the plan's result side by side in the Remix View """ \
        """(closed again when the file is saved)"""
    bl_idname = "image.bge_remix_remaster_preview"
    bl_label = "Preview"

    image = StringProperty()
    show = BoolProperty(default=True)

    def execute(self, context):
        T = tool()
        if not self.show:
            T.preview_clear()
            return {'FINISHED'}
        ima = _image_from(context, self.image or context.window_manager.bge_remix_remaster_image)
        if ima is None:
            self.report({'ERROR'}, "No image chosen")
            return {'CANCELLED'}
        if T.last_audit() is None:
            T.remaster_audit()
        try:
            T.preview_show(context, ima.name, ima.bge_remix_remaster.light_amount)
        except RuntimeError as ex:
            self.report({'ERROR'}, str(ex))
            return {'CANCELLED'}
        context.window_manager.bge_remix_remaster_image = ima.name
        return {'FINISHED'}


class IMAGE_OT_bge_remix_remaster_apply(Operator):
    """Write the accepted images' RTX Remix materials (originals kept as <name>.p8 until the file is """ \
        """saved twice), then bake and encode everything for Remix, so the game starts with every texture in"""
    bl_idname = "image.bge_remix_remaster_apply"
    bl_label = "Apply"

    _timer = None

    def _write(self, context):
        T = tool()
        if T.last_audit() is None:
            T.remaster_audit()
        T.preview_clear()
        changed = []
        for ima in list(bpy.data.images):
            r = ima.bge_remix_remaster
            if r.decision not in {'ACCEPT', 'ADJUSTED'} or r.plan == 'KEEP':
                continue
            changed += T.apply_one(ima.name, decision_of(ima), int(r.bake_size), r.light_amount)
        return changed

    def modal(self, context, event):
        if event.type != 'TIMER':
            return {'PASS_THROUGH'}
        T = tool()
        p = T.precache_poll()
        wm = context.window_manager
        wm.progress_update(max(0, self._asked - p["waiting"]))
        if p["waiting"] > 0:
            return {'RUNNING_MODAL'}
        wm.event_timer_remove(self._timer)
        wm.progress_end()
        self._finish(p)
        return {'FINISHED'}

    def _finish(self, p):
        tool()._state["applied"] = {"materials": self._changed, "asked": self._asked, "poll": dict(p)}
        self.report({'WARNING'} if p["failed"] else {'INFO'},
                    "%d materials written; %d textures for Remix (%d encoded, %d already there, %d failed, %.1f s)" %
                    (len(self._changed), self._asked, p["encoded"], p["reused"], p["failed"], p["seconds"]))

    def invoke(self, context, event):
        try:
            self._changed = self._write(context)
            self._asked = tool().precache_start()
        except RuntimeError as ex:
            self.report({'ERROR'}, str(ex))
            return {'CANCELLED'}
        wm = context.window_manager
        wm.progress_begin(0, max(self._asked, 1))
        self._timer = wm.event_timer_add(0.1, context.window)
        wm.modal_handler_add(self)
        return {'RUNNING_MODAL'}

    def execute(self, context):
        """(from scripts: waits for the encodes)"""
        import time
        T = tool()
        try:
            self._changed = self._write(context)
            self._asked = T.precache_start()
        except RuntimeError as ex:
            self.report({'ERROR'}, str(ex))
            return {'CANCELLED'}
        t0 = time.time()
        p = T.precache_poll()
        while p["waiting"] > 0 and time.time() - t0 < 600.0:
            time.sleep(0.05)
            p = T.precache_poll()
        self._finish(p)
        return {'FINISHED'}


classes = (
    BGERemixRemaster,
    IMAGE_OT_bge_remix_remaster_audit,
    IMAGE_OT_bge_remix_remaster_select,
    IMAGE_OT_bge_remix_remaster_catalogue,
    IMAGE_OT_bge_remix_remaster_fit,
    IMAGE_OT_bge_remix_remaster_decide,
    IMAGE_OT_bge_remix_remaster_preview,
    IMAGE_OT_bge_remix_remaster_apply,
)
