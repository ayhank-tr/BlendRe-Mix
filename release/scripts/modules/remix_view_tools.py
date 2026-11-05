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

"""BGE-Remix P10.7-P10.9: the Remix View's Edit and Run modes, the parts
that are plain Python (the operators and panels are in
bl_ui/space_view3d_gameview_modes.py; the tests use this module directly).

Edit (P10.7): what an element is in Blender's data, the settings Blender's
own editors show for it, wiring two elements, adding and removing elements.

The settings hub (P10.8): every setting that affects the game, once, each
with where else Blender shows it (found by running Blender's own Properties
panels with a layout that records what they draw).

Run (P10.9): what a mock-up needs, and the session of a run: the file's
values, the suspended game's values written into the file, what changed
while suspended, what Resume can take.

Blender 2.79's Python is 3.5: no f-strings, no annotations, no dict order.
"""

import bpy

try:
    import _bge_gameview as gv
except ImportError:  # a build without the game engine
    gv = None


# the layers in their clusters (as the Remix View's header shows them)
CLUSTERS = (
    ("Entity", ("OBJECTS", "CAMERAS", "INTERFACE")),
    ("Factor", ("RENDERING", "PHYSICS", "MEDIA", "DATA", "FLUIDS", "ARTICULATIONS")),
    ("Behaviour", ("LOGIC", "PYTHON", "ANIMATION", "MESSAGES", "NAVIGATION", "DESTRUCTION")),
    ("Gate", ("INPUT", "SCENES", "REMIX", "PERFORMANCE", "TIME", "LEFT_OUT", "UNASSIGNED")),
)
CLUSTER_OF = {layer: cluster for cluster, layers in CLUSTERS for layer in layers}
LAYER_NAMES = {
    "OBJECTS": "Objects", "CAMERAS": "Cameras", "INTERFACE": "Interface", "RENDERING": "Rendering",
    "PHYSICS": "Physics", "MEDIA": "Media", "DATA": "Data", "FLUIDS": "Fluids", "ARTICULATIONS": "Articulations",
    "LOGIC": "Logic", "PYTHON": "Python", "ANIMATION": "Animation", "MESSAGES": "Messages",
    "NAVIGATION": "Navigation", "DESTRUCTION": "Destruction", "INPUT": "Input", "SCENES": "Scenes",
    "REMIX": "Remix", "PERFORMANCE": "Performance", "TIME": "Time", "LEFT_OUT": "Left out",
    "UNASSIGNED": "Unassigned",
}


def find_game_view_area(context):
    """the Remix View area of the context (or the window's largest)"""
    area = context.area
    if area is not None and area.type == 'GAME_VIEW':
        return area
    best = None
    for a in context.window.screen.areas:
        if a.type == 'GAME_VIEW' and (best is None or a.width * a.height > best.width * best.height):
            best = a
    return best


def view_override(context, area=None):
    """a context for the C operators of a Remix View: its main region"""
    area = area or find_game_view_area(context)
    if area is None:
        return None
    region = [r for r in area.regions if r.type == 'WINDOW'][0]
    # (no scene: the context's own, which Stop's No replaces when it puts the file back)
    return {"window": context.window, "screen": context.window.screen, "area": area, "region": region}


def tag_redraw(context):
    for window in context.window_manager.windows:
        for area in window.screen.areas:
            if area.type in {'GAME_VIEW', 'PROPERTIES', 'LOGIC_EDITOR', 'VIEW_3D'}:
                area.tag_redraw()


def model_changed():
    """the file changed: the model is rebuilt at the next draw (BL_GameViewTagDirty)"""
    if gv is not None:
        gv.tag_dirty()


# ---------------------------------------------------------------------------
# Edit (P10.7): an element in Blender's data

BRICK_KINDS = {"sensor": "sensors", "controller": "controllers", "actuator": "actuators"}


def element_kind(el):
    """('sensor', 'ALWAYS') from 'sensor:ALWAYS'"""
    kind = el["kind"]
    if ":" in kind:
        a, b = kind.split(":", 1)
        return a, b
    return kind, ""


def owner_object(scene, el):
    return scene.objects.get(el["owner"]) if el.get("owner") else None


def find_sound_actuator(scene, el):
    """a sound element is its Sound actuator (its owner is the camera for a sound without a place)"""
    ob = owner_object(scene, el)
    if ob is not None:
        a = ob.game.actuators.get(el["name"])
        if a is not None and a.type == 'SOUND':
            return ob, a
    found = []
    for o in scene.objects:
        a = o.game.actuators.get(el["name"])
        if a is not None and a.type == 'SOUND':
            found.append((o, a))
    return found[0] if len(found) == 1 else (None, None)


def find_message_actuator(scene, el):
    ob = owner_object(scene, el)
    if ob is None:
        return None, None
    subject = "" if el["name"] == "(no subject)" else el["name"]
    for a in ob.game.actuators:
        if a.type == 'MESSAGE' and a.subject == subject:
            return ob, a
    return ob, None


def find_message_sensor(scene, el):
    ob = owner_object(scene, el)
    if ob is None:
        return None, None
    info = el.get("info", "")
    name = info[len("heard by "):] if info.startswith("heard by ") else ""
    s = ob.game.sensors.get(name) if name else None
    if s is not None and s.type == 'MESSAGE':
        return ob, s
    subject = "" if el["name"] == "(any subject)" else el["name"]
    for s in ob.game.sensors:
        if s.type == 'MESSAGE' and s.subject == subject:
            return ob, s
    return ob, None


def element_data(scene, el):
    """what an element is in Blender's data: (what, object, data)
    what: 'brick', 'body', 'sound', 'lamp', 'material', 'property', 'object',
    'camera', 'world', 'message_send', 'message_receive' or None"""
    kind, sub = element_kind(el)
    ob = owner_object(scene, el)
    if kind in BRICK_KINDS:
        if ob is None:
            return None, None, None
        return "brick", ob, getattr(ob.game, BRICK_KINDS[kind]).get(el["name"])
    if kind == "body":
        return "body", ob, ob.game if ob is not None else None
    if kind == "sound":
        o, a = find_sound_actuator(scene, el)
        return "sound", o, a
    if kind == "lamp":
        return "lamp", ob, ob.data if ob is not None and ob.type == 'LAMP' else None
    if kind == "material":
        return "material", ob, bpy.data.materials.get(el["name"])
    if kind == "property":
        return "property", ob, ob.game.properties.get(el["name"]) if ob is not None else None
    if kind in {"object", "group"}:
        return "object", ob, ob
    if kind == "camera":
        return "camera", ob, ob.data if ob is not None and ob.type == 'CAMERA' else None
    if kind == "world":
        return "world", None, bpy.data.worlds.get(el["name"])
    if kind == "message":
        if sub == "SEND":
            o, a = find_message_actuator(scene, el)
            return "message_send", o, a
        o, s = find_message_sensor(scene, el)
        return "message_receive", o, s
    return None, ob, None


# ---------------------------------------------------------------------------
# Blender's own Properties panels, drawn into another layout (Edit mode's
# sidebar) or into a recorder (the settings hub)

class ContextShim(object):
    """a context whose members (object, lamp, material, ...) are set by hand;
    everything else is the real context's"""

    def __init__(self, context, **members):
        object.__setattr__(self, "_context", context)
        object.__setattr__(self, "_members", members)

    def __getattr__(self, name):
        members = object.__getattribute__(self, "_members")
        if name in members:
            return members[name]
        return getattr(object.__getattribute__(self, "_context"), name)


class _PanelSelf(object):
    """what a Panel's draw needs of self"""

    def __init__(self, layout):
        self.layout = layout
        self.bl_space_type = 'PROPERTIES'
        self.bl_region_type = 'WINDOW'


# panels that say they show for the game engine but hold nothing of the game
NOT_GAME_PANELS = {"RENDER_PT_bake", "SCENE_PT_unit", "SCENE_PT_keying_sets", "SCENE_PT_keying_set_paths"}


def game_panels(bl_context):
    """Blender's own Properties panels of one tab that the game engine shows
    (the context and preview panels left out: an ID chooser, a picture; and
    the custom properties: the game reads them through Python only)"""
    out = []
    for cls in bpy.types.Panel.__subclasses__():
        if getattr(cls, "bl_space_type", None) != 'PROPERTIES':
            continue
        if getattr(cls, "bl_context", None) != bl_context:
            continue
        engines = getattr(cls, "COMPAT_ENGINES", None)
        if engines is None or 'BLENDER_GAME' not in engines:
            continue
        name = cls.__name__
        if "_context_" in name or "_preview" in name or "custom_props" in name or name in NOT_GAME_PANELS:
            continue
        out.append(cls)
    return out


def panel_shows(cls, ctx):
    poll = getattr(cls, "poll", None)
    if poll is None:
        return True
    try:
        return bool(poll(ctx))
    except Exception:
        return False


def draw_panel(cls, layout, ctx):
    """a Properties panel's own drawing, into layout (False: it failed)"""
    me = _PanelSelf(layout)
    try:
        if hasattr(cls, "draw_header"):
            row = layout.row()
            me.layout = row
            cls.draw_header(me, ctx)
            me.layout = layout
        cls.draw(me, ctx)
        return True
    except Exception as ex:
        layout.label(text="%s: %s" % (getattr(cls, "bl_label", cls.__name__), ex), icon='ERROR')
        return False


class Recorder(object):
    """a stand-in for a UILayout that records what is drawn with prop()"""

    def __init__(self, log):
        object.__setattr__(self, "_log", log)

    def __setattr__(self, name, value):
        pass  # active, enabled, alignment, an operator's properties: nothing to keep

    def _record(self, data, prop):
        if data is not None and isinstance(prop, str):
            self._log.append((data, prop))
        return Recorder(self._log)

    def prop(self, data, prop, *args, **kw):
        return self._record(data, prop)

    def prop_enum(self, data, prop, *args, **kw):
        return self._record(data, prop)

    def props_enum(self, data, prop, *args, **kw):
        return self._record(data, prop)

    def prop_menu_enum(self, data, prop, *args, **kw):
        return self._record(data, prop)

    def prop_search(self, data, prop, *args, **kw):
        return self._record(data, prop)

    def __getattr__(self, name):
        log = object.__getattribute__(self, "_log")

        def anything(*args, **kw):
            return Recorder(log)
        return anything

    def __bool__(self):
        return True


def record_panel(cls, ctx):
    """[(data, prop)] a Properties panel draws"""
    log = []
    me = _PanelSelf(Recorder(log))
    try:
        if hasattr(cls, "draw_header"):
            cls.draw_header(me, ctx)
        cls.draw(me, ctx)
    except Exception:
        pass
    return log


TAB_NAMES = {"render": "Render", "scene": "Scene", "world": "World", "physics": "Physics", "object": "Object",
             "data": "Data", "material": "Material"}
TAB_IDS = {"render": 'RENDER', "scene": 'SCENE', "world": 'WORLD', "physics": 'PHYSICS', "object": 'OBJECT',
           "data": 'DATA', "material": 'MATERIAL'}


def element_panels(context, what, ob, data):
    """the Properties panels Blender shows for an element, with the context
    they draw in: [(panel class, context shim)]"""
    scene = context.scene
    if what == "body" and ob is not None:
        ctx = ContextShim(context, object=ob, active_object=ob, scene=scene)
        tabs = ("physics",)
    elif what == "lamp" and data is not None:
        ctx = ContextShim(context, object=ob, active_object=ob, lamp=data, scene=scene)
        tabs = ("data",)
    elif what == "camera" and data is not None:
        ctx = ContextShim(context, object=ob, active_object=ob, camera=data, scene=scene)
        tabs = ("data",)
    elif what == "material" and data is not None:
        ctx = ContextShim(context, object=ob, active_object=ob, material=data, scene=scene,
                          material_slot=None)
        tabs = ("material",)
    elif what == "world" and data is not None:
        ctx = ContextShim(context, world=data, scene=scene)
        tabs = ("world",)
    else:
        return []
    out = []
    for tab in tabs:
        for cls in game_panels(tab):
            if panel_shows(cls, ctx):
                out.append((cls, ctx))
    return out


def is_id_pointer(prop):
    t = prop.fixed_type if prop.type == 'POINTER' else None
    while t is not None:
        if t.identifier == "ID":
            return True
        t = t.base
    return False


def rna_settings(data, skip=()):
    """the identifiers of data's editable settings (what a generic sidebar draws)"""
    out = []
    for p in data.bl_rna.properties:
        ident = p.identifier
        if ident == "rna_type" or ident in skip or p.type == 'COLLECTION':
            continue
        if p.type == 'POINTER' and not is_id_pointer(p):
            continue
        out.append(ident)
    return out


BRICK_SKIP = {"show_expanded", "pin", "active"}


def draw_rna(layout, data, skip=()):
    col = layout.column()
    for ident in rna_settings(data, skip):
        col.prop(data, ident)


# ---------------------------------------------------------------------------
# Edit (P10.7): wiring two elements

# the field of each actuator type that names the object it acts on
ACTUATOR_TARGETS = {
    'EDIT_OBJECT': ("track_object", "object"),
    'CAMERA': ("object",),
    'PARENT': ("object",),
    'PROPERTY': ("object",),
    'STEERING': ("target",),
    'ARMATURE': ("target",),
    'MOTION': ("reference_object",),
    'SCENE': ("camera",),
}


def _actuator_target_field(act):
    fields = ACTUATOR_TARGETS.get(act.type)
    if not fields:
        return None
    if act.type == 'EDIT_OBJECT':
        mode = getattr(act, "mode", "")
        if mode == 'ADDOBJECT':
            return "object"
        if mode == 'TRACKTO':
            return "track_object"
        return None
    if act.type == 'PROPERTY' and getattr(act, "mode", "") != 'COPY':
        return None
    if act.type == 'MOTION' and getattr(act, "mode", "") != 'OBJECT_SERVO':
        return None
    if act.type == 'SCENE' and getattr(act, "mode", "") != 'CAMERA':
        return None
    if act.type == 'PARENT' and getattr(act, "mode", "") != 'SETPARENT':
        return None
    return fields[0]


def wire(scene, src, tgt):
    """wire element src to element tgt (gv.element dicts): (ok, message)"""
    ks, _ = element_kind(src)
    kt, _ = element_kind(tgt)
    ws, obs, ds = element_data(scene, src)
    wt, obt, dt = element_data(scene, tgt)
    # the drag's direction does not matter for the logic's own links
    pair = (ks, kt)
    listener_first = ws == "message_receive" or (ks == "sensor" and ds is not None and ds.type == 'MESSAGE')
    sender_second = wt == "message_send" or (kt == "actuator" and dt is not None and dt.type == 'MESSAGE')
    if pair in {("controller", "sensor"), ("actuator", "controller")} or (listener_first and sender_second):
        src, tgt, ks, kt, ds, dt, obs, obt, ws, wt = tgt, src, kt, ks, dt, ds, obt, obs, wt, ws
        pair = (ks, kt)
    if pair == ("sensor", "controller"):
        if ds is None or dt is None:
            return False, "the bricks are not in the file"
        if dt in list(ds.controllers):
            return False, "%s is already linked to %s" % (ds.name, dt.name)
        ds.link(dt)
        return True, "%s linked to %s" % (ds.name, dt.name)
    if pair == ("controller", "actuator"):
        if ds is None or dt is None:
            return False, "the bricks are not in the file"
        if dt in list(ds.actuators):
            return False, "%s is already linked to %s" % (ds.name, dt.name)
        ds.link(actuator=dt)
        return True, "%s linked to %s" % (ds.name, dt.name)
    if ks == "actuator" and wt == "object":
        if ds is None or obt is None:
            return False, "not in the file"
        if ds.type == 'MESSAGE':
            ds.to_property = obt.name
            return True, "%s now sends to %s" % (ds.name, obt.name)
        field = _actuator_target_field(ds)
        if field is None:
            return False, "a %s actuator (in this mode) acts on its own object" % ds.type.replace("_", " ").title()
        if field == "camera" and obt.type != 'CAMERA':
            return False, "a Scene actuator's camera must be a camera"
        setattr(ds, field, obt)
        return True, "%s now acts on %s" % (ds.name, obt.name)
    if ws in {"message_send"} or (ks == "actuator" and ds is not None and ds.type == 'MESSAGE'):
        act = ds
        if act is None:
            return False, "the Message actuator is not in the file"
        if wt == "message_receive" or (kt == "sensor" and dt is not None and dt.type == 'MESSAGE'):
            sens = dt
            if sens is None:
                return False, "the Message sensor is not in the file"
            if not act.subject:
                act.subject = sens.subject or act.name
            sens.subject = act.subject
            if act.to_property and act.to_property != obt.name:
                act.to_property = obt.name
            return True, "%s now hears \"%s\" from %s" % (sens.name, act.subject, obs.name)
        if wt == "object" and obt is not None:
            act.to_property = obt.name
            return True, "%s now sends to %s" % (act.name, obt.name)
        return False, "a message goes to a listener (a Message sensor) or an object"
    if ws == "object" and wt == "object":
        if obs is None or obt is None or obs == obt:
            return False, "pick two objects"
        p = obt
        while p is not None:
            if p == obs:
                return False, "%s is a child of %s: that would be a loop" % (obt.name, obs.name)
            p = p.parent
        world = obs.matrix_world.copy()
        obs.parent = obt
        obs.matrix_parent_inverse = obt.matrix_world.inverted()
        obs.matrix_world = world
        return True, "%s is now a child of %s" % (obs.name, obt.name)
    return False, "a %s cannot be wired to a %s" % (src["kind"].split(":")[0], tgt["kind"].split(":")[0])


# ---------------------------------------------------------------------------
# Edit (P10.7): adding and removing elements

SENSOR_TYPES = ('ALWAYS', 'DELAY', 'KEYBOARD', 'MOUSE', 'PROPERTY', 'MESSAGE', 'COLLISION', 'NEAR', 'RADAR',
                'RAY', 'RANDOM', 'ACTUATOR', 'JOYSTICK', 'ARMATURE')
CONTROLLER_TYPES = ('LOGIC_AND', 'LOGIC_OR', 'LOGIC_NAND', 'LOGIC_NOR', 'LOGIC_XOR', 'LOGIC_XNOR', 'EXPRESSION',
                    'PYTHON')
ACTUATOR_TYPES = ('MOTION', 'PROPERTY', 'EDIT_OBJECT', 'MESSAGE', 'SOUND', 'ACTION', 'CAMERA', 'CONSTRAINT',
                  'FILTER_2D', 'GAME', 'MOUSE', 'PARENT', 'RANDOM', 'SCENE', 'STATE', 'STEERING', 'VISIBILITY',
                  'ARMATURE')
BODY_TYPES = ('STATIC', 'DYNAMIC', 'RIGID_BODY', 'SOFT_BODY', 'SENSOR', 'CHARACTER', 'OCCLUDER')


def _logic_override(context, ob):
    o = context.copy()
    o["object"] = ob
    o["active_object"] = ob
    return o


def add_element(context, owner, what, kind, name=""):
    """add a brick, a body or a sound to an object: (ok, message)"""
    scene = context.scene
    ob = scene.objects.get(owner)
    if ob is None:
        return False, "no object %s" % owner
    if what in {"SENSOR", "CONTROLLER", "ACTUATOR"}:
        coll = getattr(ob.game, what.lower() + "s")
        before = set(b.name for b in coll)
        op = {"SENSOR": bpy.ops.logic.sensor_add, "CONTROLLER": bpy.ops.logic.controller_add,
              "ACTUATOR": bpy.ops.logic.actuator_add}[what]
        op(_logic_override(context, ob), type=kind, name=name, object=ob.name)
        new = [b.name for b in coll if b.name not in before]
        if not new:
            return False, "nothing was added"
        return True, "%s %s added to %s" % (what.lower().title(), new[0], ob.name)
    if what == "BODY":
        if ob.game.physics_type not in {'NO_COLLISION'}:
            return False, "%s already has a body (%s)" % (ob.name, ob.game.physics_type.replace("_", " ").title())
        ob.game.physics_type = kind or 'STATIC'
        return True, "%s is now a %s body" % (ob.name, ob.game.physics_type.replace("_", " ").title())
    if what == "SOUND":
        before = set(b.name for b in ob.game.actuators)
        bpy.ops.logic.actuator_add(_logic_override(context, ob), type='SOUND', name=name or "Sound",
                                   object=ob.name)
        new = [b.name for b in ob.game.actuators if b.name not in before]
        if not new:
            return False, "nothing was added"
        return True, "Sound actuator %s added to %s (choose its sound)" % (new[0], ob.name)
    return False, "cannot add a %s" % what


def remove_element(context, el):
    """remove what an element is (a brick, a body, a sound): (ok, message)"""
    scene = context.scene
    what, ob, data = element_data(scene, el)
    if ob is None or data is None:
        return False, "not in the file"
    kind, _ = element_kind(el)
    if what == "brick":
        op = {"sensor": bpy.ops.logic.sensor_remove, "controller": bpy.ops.logic.controller_remove,
              "actuator": bpy.ops.logic.actuator_remove}[kind]
        arg = {"sensor": "sensor", "controller": "controller", "actuator": "actuator"}[kind]
        name = data.name
        op(_logic_override(context, ob), **{arg: name, "object": ob.name})
        return True, "%s removed from %s" % (name, ob.name)
    if what == "sound":
        name = data.name
        bpy.ops.logic.actuator_remove(_logic_override(context, ob), actuator=name, object=ob.name)
        return True, "%s removed from %s" % (name, ob.name)
    if what == "body":
        ob.game.physics_type = 'NO_COLLISION'
        return True, "%s has no body now" % ob.name
    return False, "a %s is removed in Blender's own editors" % el["kind"].split(":")[0]


# ---------------------------------------------------------------------------
# the settings hub (P10.8)

# a setting's layer, by the struct it belongs to and its name
GAME_SETTING_LAYERS = (
    # (prefix or name, layer); the first that matches wins
    ("resolution_", "INTERFACE"), ("show_fullscreen", "INTERFACE"), ("use_desktop", "INTERFACE"),
    ("depth", "INTERFACE"), ("frequency", "INTERFACE"), ("frame_type", "INTERFACE"), ("frame_color", "INTERFACE"),
    ("show_mouse", "INTERFACE"),
    ("stereo", "CAMERAS"), ("dome_", "CAMERAS"),
    ("remix_", "REMIX"),
    ("render_pipeline", "RENDERING"), ("fx_", "RENDERING"), ("use_glsl_", "RENDERING"), ("material_mode", "RENDERING"),
    ("samples", "RENDERING"), ("use_scene_hysteresis", "RENDERING"), ("scene_hysteresis_", "RENDERING"),
    ("exit_key", "INPUT"),
    ("physics_", "PHYSICS"), ("physx_", "PHYSICS"), ("deactivation_", "PHYSICS"),
    ("show_physics_visualization", "PHYSICS"),
    ("fps", "TIME"), ("logic_step_max", "TIME"), ("use_frame_rate", "TIME"), ("vsync", "TIME"),
    ("use_restrict_animation_updates", "ANIMATION"), ("use_animation_record", "ANIMATION"),
    ("raster_storage", "PERFORMANCE"), ("use_display_lists", "PERFORMANCE"), ("use_material_caching", "PERFORMANCE"),
    ("use_occlusion_culling", "PERFORMANCE"), ("occlusion_culling_", "PERFORMANCE"),
    ("use_activity_culling", "PERFORMANCE"), ("activity_culling_", "PERFORMANCE"),
    ("show_framerate_profile", "PERFORMANCE"),
    ("show_debug_properties", "DATA"),
    ("use_deprecation_warnings", "PYTHON"),
    ("use_auto_start", "SCENES"),
    ("obstacle_", "NAVIGATION"), ("level_height", "NAVIGATION"), ("show_obstacle_", "NAVIGATION"),
)
OBJECT_SETTING_LAYERS = (
    ("states_", "LOGIC"), ("use_all_states", "LOGIC"), ("show_state_panel", "LOGIC"), ("show_debug_state", "LOGIC"),
    ("show_sensors", "LOGIC"), ("show_controllers", "LOGIC"), ("show_actuators", "LOGIC"),
    ("use_obstacle_create", "NAVIGATION"), ("obstacle_radius", "NAVIGATION"),
    ("use_record_animation", "ANIMATION"),
    ("use_activity_culling", "PERFORMANCE"),
)
RENDER_SETTING_LAYERS = (
    ("fps", "ANIMATION"), ("resolution_", "INTERFACE"), ("pixel_aspect", "INTERFACE"), ("use_motion_blur", "RENDERING"),
    ("motion_blur_", "RENDERING"), ("engine", "RENDERING"),
)
TAB_LAYERS = {"render": "RENDERING", "scene": "PHYSICS", "world": "RENDERING", "physics": "PHYSICS",
              "object": "OBJECTS"}


def _match(rules, prop, default):
    for prefix, layer in rules:
        if prop == prefix or (prefix.endswith("_") and prop.startswith(prefix)):
            return layer
    return default


def setting_layer(data, prop, tab=None):
    struct = data.bl_rna.identifier
    if struct == "SceneGameData":
        return _match(GAME_SETTING_LAYERS, prop, TAB_LAYERS.get(tab, "RENDERING"))
    if struct == "SceneGameRecastData":
        return "NAVIGATION"
    if struct == "Scene":
        if prop.startswith("bge_remix") or prop.startswith("bge_rtx_"):
            return "REMIX"
        if prop == "camera":
            return "CAMERAS"
        if prop == "background_set":
            return "SCENES"
        if "audio" in prop:
            return "MEDIA"
        return "RENDERING"
    if struct.startswith("ColorManaged"):
        return "RENDERING"
    if struct == "FFmpegSettings":
        return "MEDIA"
    if struct == "RenderSettings":
        return _match(RENDER_SETTING_LAYERS, prop, "RENDERING")
    if struct in {"GameObjectSettings"}:
        return _match(OBJECT_SETTING_LAYERS, prop, "PHYSICS")
    if struct in {"GameSoftBodySettings"}:
        return "PHYSICS"
    if struct.startswith("World"):
        return "RENDERING"
    if struct in {"Camera", "GPUDOFSettings"}:
        return "CAMERAS"
    if struct == "Object":
        return "OBJECTS" if prop in {"hide_render"} else ("RENDERING" if "lod" in prop else "OBJECTS")
    if struct == "LodLevel":
        return "RENDERING"
    return TAB_LAYERS.get(tab, "UNASSIGNED")


ID_COLLECTIONS = {"Scene": "scenes", "Object": "objects", "World": "worlds", "Camera": "cameras", "Lamp": "lamps",
                  "Material": "materials", "Mesh": "meshes", "Text": "texts"}


def _id_type(id_data):
    t = id_data.bl_rna
    while t is not None:
        if t.identifier in ID_COLLECTIONS:
            return t.identifier
        t = t.base
    return None


def _row_key(id_type, id_name, path, prop):
    return "%s:%s|%s|%s" % (id_type, id_name, path, prop)


def resolve_row(row):
    """the data a hub row is a setting of (None: gone)"""
    coll = getattr(bpy.data, ID_COLLECTIONS.get(row["id_type"], ""), None)
    if coll is None:
        return None
    idb = coll.get(row["id_name"])
    if idb is None:
        return None
    if not row["path"]:
        return idb
    try:
        return idb.path_resolve(row["path"])
    except ValueError:
        return None


class HubRows(object):
    """the hub's rows, each setting once (keyed by its ID, path and name)"""

    def __init__(self):
        self.rows = []
        self.by_key = {}

    def add(self, data, prop, owner=None, where=None, tab=None):
        if data is None:
            return
        p = data.bl_rna.properties.get(prop)
        if p is None or p.identifier == "rna_type" or p.is_readonly or p.type == 'COLLECTION':
            return
        if p.type == 'POINTER' and not is_id_pointer(p):
            return
        id_data = data.id_data
        if id_data is None:
            return
        id_type = _id_type(id_data)
        if id_type is None:
            return
        if data == id_data:
            path = ""
        else:
            try:
                path = data.path_from_id()
            except ValueError:
                return
        key = _row_key(id_type, id_data.name, path, prop)
        row = self.by_key.get(key)
        if row is None:
            layer = setting_layer(data, prop, tab)
            row = {"key": key, "id_type": id_type, "id_name": id_data.name, "path": path, "prop": prop,
                   "label": p.name or prop, "layer": layer, "cluster": CLUSTER_OF.get(layer, "Gate"),
                   "owner": owner, "where": []}
            self.by_key[key] = row
            self.rows.append(row)
        if where and where not in row["where"]:
            row["where"].append(where)


def remix_scene_settings():
    """the scene's RTX Remix settings (bl_ui/properties_game.py registers them)"""
    names = ["bge_remix", "bge_remix_fast", "bge_remix_light_scale", "bge_remix_exposure", "bge_remix_preset",
             "bge_remix_dlss", "bge_remix_rr", "bge_remix_volumetrics", "bge_remix_f12_mode",
             "bge_remix_f12_frames", "bge_remix_f12_samples"]
    try:
        from bl_ui.properties_game import RTX_CURATED
        for _group, items in RTX_CURATED:
            names.extend(item[0] for item in items)
    except ImportError:
        pass
    return names


def scene_hub_rows(context, hub=None):
    """the scene's settings: game settings, the pipeline and RTX Remix, the
    physics engine, the world"""
    hub = hub or HubRows()
    scene = context.scene
    ctx = ContextShim(context, scene=scene, world=scene.world)
    for tab in ("render", "scene", "world"):
        for cls in game_panels(tab):
            if not panel_shows(cls, ctx):
                continue
            where = (tab, getattr(cls, "bl_label", "") or cls.__name__)
            for data, prop in record_panel(cls, ctx):
                if getattr(data, "id_data", None) is None:
                    continue
                # per-object settings belong to the objects' rows
                if _id_type(data.id_data) == "Object":
                    continue
                hub.add(data, prop, None, where, tab)
    gs = scene.game_settings
    for p in gs.bl_rna.properties:
        hub.add(gs, p.identifier, None, None, "render")
    recast = getattr(gs, "recast_data", None)
    if recast is not None:
        for p in recast.bl_rna.properties:
            hub.add(recast, p.identifier, None, None, "scene")
    for name in remix_scene_settings():
        if name in scene.bl_rna.properties:
            hub.add(scene, name, None, None, "render")
    return hub


def object_hub_rows(context, ob, hub=None):
    """an object's game settings"""
    hub = hub or HubRows()
    ctx = ContextShim(context, scene=context.scene, object=ob, active_object=ob)
    for tab in ("physics", "object"):
        for cls in game_panels(tab):
            if not panel_shows(cls, ctx):
                continue
            where = (tab, getattr(cls, "bl_label", "") or cls.__name__)
            for data, prop in record_panel(cls, ctx):
                if getattr(data, "id_data", None) is None or data.id_data != ob:
                    continue
                hub.add(data, prop, ob.name, where, tab)
    g = ob.game
    for p in g.bl_rna.properties:
        hub.add(g, p.identifier, ob.name, None, "physics")
    soft = getattr(g, "soft_body", None)
    if soft is not None:
        for p in soft.bl_rna.properties:
            hub.add(soft, p.identifier, ob.name, None, "physics")
    return hub


def hub_rows(context, objects=None):
    """every setting that affects the game, once: [row]; objects: the
    objects whose rows to make (None: every object of the scene)"""
    hub = scene_hub_rows(context)
    obs = context.scene.objects if objects is None else objects
    for ob in sorted(obs, key=lambda o: o.name):
        object_hub_rows(context, ob, hub)
    return hub.rows


def where_text(row):
    if not row["where"]:
        return "Only here"
    return "; ".join("Properties > %s > %s" % (TAB_NAMES.get(tab, tab), label) for tab, label in row["where"])


# ---------------------------------------------------------------------------
# Run (P10.9): what a mock-up needs

def logic_targets(ob):
    """the objects an object's logic and joints name"""
    out = []
    for a in ob.game.actuators:
        for field in ACTUATOR_TARGETS.get(a.type, ()):
            v = getattr(a, field, None)
            if v is not None and hasattr(v, "name"):
                out.append((v.name, "a target of %s's %s" % (ob.name, a.name)))
        if a.type == 'MESSAGE' and a.to_property:
            out.append((a.to_property, "%s's %s sends to it" % (ob.name, a.name)))
        if a.type == 'STEERING' and getattr(a, "navmesh", None) is not None:
            out.append((a.navmesh.name, "the navigation mesh of %s's %s" % (ob.name, a.name)))
    for c in ob.constraints:
        if c.type == 'RIGID_BODY_JOINT' and getattr(c, "target", None) is not None:
            out.append((c.target.name, "joined to %s" % ob.name))
    return out


def mockup_objects(scene, chosen):
    """the objects a mock-up of chosen plays: {name: why}"""
    why = {}
    todo = [(n, "chosen") for n in chosen]
    for ob in scene.objects:
        if ob.type == 'LAMP':
            todo.append((ob.name, "a lamp"))
    if scene.camera is not None:
        todo.append((scene.camera.name, "the camera"))
    while todo:
        name, reason = todo.pop(0)
        if name in why:
            continue
        ob = scene.objects.get(name)
        if ob is None:
            continue
        why[name] = reason
        if ob.parent is not None:
            todo.append((ob.parent.name, "the parent of %s" % name))
        for child in ob.children:
            todo.append((child.name, "a child of %s" % name))
        todo.extend(logic_targets(ob))
    return why


# ---------------------------------------------------------------------------
# Run (P10.9): the session

SESSION = {}


def session_reset():
    SESSION.clear()
    SESSION.update({"state": 'IDLE', "scope": 'SCENE', "chosen": [], "keep": [], "file": {}, "posed": {},
                    "base": None, "suspends": 0, "frame": 0, "other_scene": False, "last": None,
                    "edited": False})


session_reset()

EPS = 1e-4


def _same(a, b):
    if isinstance(a, (tuple, list)) and isinstance(b, (tuple, list)):
        return len(a) == len(b) and all(_same(x, y) for x, y in zip(a, b))
    if isinstance(a, float) or isinstance(b, float):
        try:
            return abs(float(a) - float(b)) <= EPS * max(1.0, abs(float(a)), abs(float(b)))
        except (TypeError, ValueError):
            return False
    return a == b


def read_rotation(ob):
    mode = ob.rotation_mode
    if mode == 'QUATERNION':
        return (mode, tuple(ob.rotation_quaternion))
    if mode == 'AXIS_ANGLE':
        return (mode, tuple(ob.rotation_axis_angle))
    return (mode, tuple(ob.rotation_euler))


def write_rotation(ob, value):
    mode, rot = value
    if ob.rotation_mode != mode:
        ob.rotation_mode = mode
    if mode == 'QUATERNION':
        ob.rotation_quaternion = rot
    elif mode == 'AXIS_ANGLE':
        ob.rotation_axis_angle = rot
    else:
        ob.rotation_euler = rot


# an object's place: its location, rotation and scale (each its own value:
# changing one while suspended keeps the game's other two)
PLACE_FIELDS = ("L", "R", "S")


def read_fields(ob):
    """an object's values a suspend writes and a stop puts back"""
    f = {"L": tuple(ob.location), "R": read_rotation(ob), "S": tuple(ob.scale)}
    for p in ob.game.properties:
        f["P:" + p.name] = p.value
    return f


def write_field(ob, field, value):
    if field == "L":
        ob.location = value
        return
    if field == "R":
        write_rotation(ob, value)
        return
    if field == "S":
        ob.scale = value
        return
    p = ob.game.properties.get(field[2:])
    if p is not None:
        try:
            p.value = value
        except (TypeError, ValueError):
            pass


def read_all(scene):
    return {ob.name: read_fields(ob) for ob in scene.objects}


def _matrix(m):
    from mathutils import Matrix
    return Matrix(((m[0], m[4], m[8], m[12]), (m[1], m[5], m[9], m[13]), (m[2], m[6], m[10], m[14]),
                   (m[3], m[7], m[11], m[15])))


def _depth(ob):
    d = 0
    while ob.parent is not None:
        d += 1
        ob = ob.parent
    return d


PROP_TYPES = {"BOOL": {'BOOL'}, "INT": {'INT'}, "FLOAT": {'FLOAT', 'TIMER'}, "TIMER": {'TIMER', 'FLOAT'},
              "STRING": {'STRING'}}


def pose(scene, snap):
    """write the suspended game's places and property values into the file"""
    live = {o["name"]: o for o in snap["objects"] if not o["replica"]}
    obs = [ob for ob in scene.objects if ob.name in live]
    for depth in sorted(set(_depth(ob) for ob in obs)):
        for ob in obs:
            if _depth(ob) == depth:
                ob.matrix_world = _matrix(live[ob.name]["matrix"])
        scene.update()
    for ob in obs:
        for name, kind, value in live[ob.name]["props"]:
            p = ob.game.properties.get(name)
            if p is None or p.type not in PROP_TYPES.get(kind, ()):
                continue
            try:
                if p.type == 'INT':
                    p.value = int(value)
                elif p.type in {'FLOAT', 'TIMER'}:
                    p.value = float(value)
                elif p.type == 'BOOL':
                    p.value = bool(value)
                else:
                    p.value = str(value)
            except (TypeError, ValueError):
                pass
    scene.update()


def fold_edits(scene):
    """what you changed while suspended becomes the file's value"""
    for name, fields in SESSION["posed"].items():
        ob = scene.objects.get(name)
        if ob is None:
            continue
        now = read_fields(ob)
        for field, posed in fields.items():
            if field in now and not _same(now[field], posed):
                SESSION["file"].setdefault(name, {})[field] = now[field]
                SESSION["edited"] = True
        for field in now:
            if field not in fields:
                SESSION["file"].setdefault(name, {})[field] = now[field]
                SESSION["edited"] = True


def unpose(scene):
    """the file's values back where the suspended game's were written and
    you did not change them"""
    for name, fields in SESSION["posed"].items():
        ob = scene.objects.get(name)
        if ob is None:
            continue
        now = read_fields(ob)
        orig = SESSION["file"].get(name, {})
        for field, posed in fields.items():
            if field in now and field in orig and _same(now[field], posed):
                write_field(ob, field, orig[field])
    SESSION["posed"] = {}
    scene.update()


def _rna_values(data):
    out = []
    for p in data.bl_rna.properties:
        if p.identifier == "rna_type" or p.type == 'COLLECTION':
            continue
        try:
            v = getattr(data, p.identifier)
        except AttributeError:
            continue
        if p.type == 'POINTER':
            v = getattr(v, "name", None) if is_id_pointer(p) else None
            if v is None:
                continue
        elif hasattr(v, "__len__") and not isinstance(v, str):
            v = tuple(v)
        out.append((p.identifier, v))
    return tuple(out)


def structure(scene):
    """what Resume cannot take when it changes: the objects, their bricks,
    their game properties, their parents"""
    st = {}
    for ob in scene.objects:
        g = ob.game
        bricks = []
        for kind, coll in (("sensor", g.sensors), ("controller", g.controllers), ("actuator", g.actuators)):
            for b in coll:
                bricks.append((kind, b.name, b.type))
        st[ob.name] = {"type": ob.type, "parent": ob.parent.name if ob.parent else "",
                       "bricks": sorted(bricks), "props": sorted((p.name, p.type) for p in g.properties)}
    return st


def settings(scene):
    """what Resume takes: each object's logic and physics settings"""
    out = {}
    for ob in scene.objects:
        g = ob.game
        bricks = []
        for coll in (g.sensors, g.controllers, g.actuators):
            for b in coll:
                links = ()
                if hasattr(b, "controllers"):
                    links = tuple(sorted(c.name for c in b.controllers))
                elif hasattr(b, "actuators"):
                    links = tuple(sorted(a.name for a in b.actuators))
                bricks.append((b.name, _rna_values(b), links))
        out[ob.name] = {"logic": tuple(sorted(bricks, key=lambda b: b[0])), "physics": _rna_values(g),
                        "states": g.states_initial[:] if hasattr(g, "states_initial") else ()}
    return out


def continue_offer(context):
    """what Continue offers: {"resume": bool, "reasons": [why only Start
    over], "takes": [what Resume takes]}"""
    scene = context.scene
    reasons, takes = [], []
    if SESSION.get("other_scene"):
        reasons.append("the game had gone to another scene when it was suspended")
    base = SESSION.get("base") or {}
    st_then, set_then = base.get("structure", {}), base.get("settings", {})
    st_now, set_now = structure(scene), settings(scene)
    for name in sorted(set(st_now) - set(st_then)):
        reasons.append("%s is new: the running game has no state for it" % name)
    for name in sorted(set(st_then) - set(st_now)):
        reasons.append("%s was removed" % name)
    for name in sorted(set(st_now) & set(st_then)):
        a, b = st_then[name], st_now[name]
        if a["type"] != b["type"]:
            reasons.append("%s is now a %s" % (name, b["type"].lower()))
        if a["parent"] != b["parent"]:
            reasons.append("%s has a new parent: the game makes parent links when it starts" % name)
        old = {(k, n): t for k, n, t in a["bricks"]}
        new = {(k, n): t for k, n, t in b["bricks"]}
        for k, n in sorted(set(new) - set(old)):
            reasons.append("a new %s on %s: %s" % (k, name, n))
        for k, n in sorted(set(old) - set(new)):
            reasons.append("the %s %s was removed from %s" % (k, n, name))
        for key in sorted(set(old) & set(new)):
            if old[key] != new[key]:
                reasons.append("%s's %s %s is now a %s %s" % (name, key[0], key[1], new[key].lower(), key[0]))
        oldp, newp = dict(a["props"]), dict(b["props"])
        for p in sorted(set(newp) - set(oldp)):
            reasons.append("a new game property on %s: %s" % (name, p))
        for p in sorted(set(oldp) - set(newp)):
            reasons.append("the game property %s was removed from %s" % (p, name))
        for p in sorted(set(oldp) & set(newp)):
            if oldp[p] != newp[p]:
                reasons.append("%s's game property %s is now a %s" % (name, p, newp[p].lower()))
        # what Resume takes
        posed = SESSION["posed"].get(name, {})
        ob = scene.objects.get(name)
        if ob is not None and posed:
            now = read_fields(ob)
            if any(f in posed and not _same(now[f], posed[f]) for f in PLACE_FIELDS):
                takes.append("%s: its place" % name)
            for field in sorted(f for f in posed if f.startswith("P:")):
                if field in now and not _same(now[field], posed[field]):
                    takes.append("%s: the property %s = %s" % (name, field[2:], now[field]))
        if name in set_then and name in set_now:
            if set_then[name]["logic"] != set_now[name]["logic"]:
                takes.append("%s: its logic settings" % name)
            if set_then[name]["physics"] != set_now[name]["physics"]:
                takes.append("%s: its physics settings" % name)
            if set_then[name]["states"] != set_now[name]["states"]:
                takes.append("%s: its starting states" % name)
    return {"resume": not reasons, "reasons": reasons, "takes": takes}


def snapshot_base(scene):
    return {"structure": structure(scene), "settings": settings(scene)}
