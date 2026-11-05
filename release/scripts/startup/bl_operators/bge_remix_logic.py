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

"""BGE-Remix P10.11 and P10.12: picking a logic brick's text from a list.

The logic editor draws a small button beside a Message sensor's or Message
actuator's subject, and beside an RTX Remix actuator's setting. It opens a
search list:
  logic.bge_remix_subject  the subjects the game's bricks already use, so a
                           typing slip cannot break a message
  logic.bge_remix_setting  RTX Remix's settings (the Render tab's curated
                           ones and the quality ones, _bge_remix.settings())
The text field stays, for typing anything else.
"""

import bpy
from bpy.types import Operator
from bpy.props import EnumProperty, StringProperty

# the settings when _bge_remix is not there (a build without RTX Remix); the
# same as remix_session.c's lists
SETTINGS_FALLBACK = (
    ("rtx.dlfg.enable", 'BOOL', "True"),
    ("rtx.reflexMode", 'INT', "1"),
    ("rtx.resolutionScale", 'FLOAT', "0.75"),
    ("rtx.pathMinBounces", 'INT', "1"),
    ("rtx.pathMaxBounces", 'INT', "4"),
    ("rtx.enableSecondaryBounces", 'BOOL', "True"),
    ("rtx.enableRussianRoulette", 'BOOL', "True"),
    ("rtx.fireflyFilteringLuminanceThreshold", 'FLOAT', "1000"),
    ("rtx.emissiveIntensity", 'FLOAT', "1"),
    ("rtx.antiCulling.light.enable", 'BOOL', "False"),
    ("rtx.antiCulling.object.enable", 'BOOL', "False"),
    ("rtx.postfx.enableChromaticAberration", 'BOOL', "True"),
    ("rtx.particles.enable", 'BOOL', "True"),
    ("rtx.tonemappingMode", 'INT', "1"),
    ("rtx.tonemap.finalizeWithACES", 'BOOL', "False"),
    ("rtx.localtonemap.finalizeWithACES", 'BOOL', "True"),
    ("rtx.localtonemap.exposure", 'FLOAT', "0.75"),
    ("rtx.tonemap.dynamicRange", 'FLOAT', "15"),
    ("rtx.graphicsPreset", 'INT', "5"),
    ("rtx.dlssPreset", 'INT', "1"),
    ("rtx.qualityDLSS", 'INT', "4"),
    ("rtx.enableRayReconstruction", 'BOOL', "True"),
    ("rtx.volumetrics.enable", 'BOOL', "True"),
)

NONE_ID = "__NONE__"


def remix_settings():
    """RTX Remix's settings the actuator lists: (key, kind, default)"""
    try:
        import _bge_remix
        found = [tuple(s) for s in _bge_remix.settings()]
        if found:
            return found
    except (ImportError, AttributeError):
        pass
    return list(SETTINGS_FALLBACK)


def game_subjects():
    """every message subject the game's logic bricks use, sorted"""
    found = set()
    for ob in bpy.data.objects:
        for s in ob.game.sensors:
            if s.type == 'MESSAGE' and s.subject:
                found.add(s.subject)
        for a in ob.game.actuators:
            if a.type == 'MESSAGE' and a.subject:
                found.add(a.subject)
    return sorted(found)


def find_brick(object_name, brick_name, kind):
    ob = bpy.data.objects.get(object_name)
    if ob is None:
        return None
    bricks = ob.game.sensors if kind == 'SENSOR' else ob.game.actuators
    return bricks.get(brick_name)


# (Blender keeps no copy of an enum callback's strings: they are kept here)
_subject_items = []
_setting_items = []


def subject_items(self, context):
    global _subject_items
    subjects = game_subjects()
    _subject_items = [(s, s, "Used by the game's logic") for s in subjects] or \
        [(NONE_ID, "(no subjects in the game yet)", "")]
    return _subject_items


def setting_items(self, context):
    global _setting_items
    _setting_items = [(key, key, "%s, default %s" % (kind.lower(), default))
                      for key, kind, default in remix_settings()]
    return _setting_items


class LOGIC_OT_bge_remix_subject(Operator):
    """Pick a message subject the game already uses"""
    bl_idname = "logic.bge_remix_subject"
    bl_label = "Pick Subject"
    bl_property = "subject"
    bl_options = {'REGISTER', 'UNDO', 'INTERNAL'}

    object = StringProperty(name="Object", options={'HIDDEN'})
    brick = StringProperty(name="Brick", options={'HIDDEN'})
    kind = StringProperty(name="Kind", description="SENSOR or ACTUATOR", options={'HIDDEN'})
    subject = EnumProperty(name="Subject", items=subject_items)

    def invoke(self, context, event):
        context.window_manager.invoke_search_popup(self)
        return {'RUNNING_MODAL'}

    def execute(self, context):
        if self.subject == NONE_ID:
            return {'CANCELLED'}
        brick = find_brick(self.object, self.brick, self.kind)
        if brick is None or brick.type != 'MESSAGE':
            self.report({'WARNING'}, "No message brick %r on %r" % (self.brick, self.object))
            return {'CANCELLED'}
        brick.subject = self.subject
        return {'FINISHED'}


class LOGIC_OT_bge_remix_setting(Operator):
    """Pick one of RTX Remix's settings"""
    bl_idname = "logic.bge_remix_setting"
    bl_label = "Pick RTX Remix Setting"
    bl_property = "setting"
    bl_options = {'REGISTER', 'UNDO', 'INTERNAL'}

    object = StringProperty(name="Object", options={'HIDDEN'})
    brick = StringProperty(name="Brick", options={'HIDDEN'})
    kind = StringProperty(name="Kind", description="ACTUATOR", options={'HIDDEN'})
    setting = EnumProperty(name="Setting", items=setting_items)

    def invoke(self, context, event):
        context.window_manager.invoke_search_popup(self)
        return {'RUNNING_MODAL'}

    def execute(self, context):
        brick = find_brick(self.object, self.brick, 'ACTUATOR')
        if brick is None or brick.type != 'REMIX':
            self.report({'WARNING'}, "No RTX Remix actuator %r on %r" % (self.brick, self.object))
            return {'CANCELLED'}
        brick.setting = self.setting
        if not brick.value:
            for key, kind, default in remix_settings():
                if key == self.setting:
                    brick.value = default
                    break
        return {'FINISHED'}


classes = (
    LOGIC_OT_bge_remix_subject,
    LOGIC_OT_bge_remix_setting,
)
