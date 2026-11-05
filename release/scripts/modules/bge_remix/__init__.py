# BGE-Remix: NVIDIA RTX Remix in Blender 2.79b.
#
# Python front end over the built-in module _bge_remix (which loads
# bge_remix_bridge.dll on first use). Python 3.5: no f-strings.
#
# Where the runtime lives, in this order:
#   1. the environment variable BGE_REMIX_RUNTIME_DIR
#   2. the folder chosen in Preferences > System > RTX Remix (settings.json)
#   3. <Blender user folder>/remix/<pinned version>, e.g.
#      %APPDATA%\Blender Foundation\Blender\2.79\remix\1.6.1

import json
import os

from . import pin

try:
    import _bge_remix
except ImportError:  # a build without WITH_REMIX
    _bge_remix = None


def available():
    """True when this Blender was built with the Remix bridge."""
    return _bge_remix is not None


def _user_base():
    import bpy
    return os.path.join(bpy.utils.resource_path('USER'), "remix")


def _settings_path():
    return os.path.join(_user_base(), "settings.json")


def _read_settings():
    try:
        with open(_settings_path(), "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def _write_settings(data):
    os.makedirs(_user_base(), exist_ok=True)
    with open(_settings_path(), "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def default_runtime_dir():
    return os.path.join(_user_base(), pin.VERSION)


def runtime_dir():
    env = os.environ.get("BGE_REMIX_RUNTIME_DIR")
    if env:
        return env
    chosen = _read_settings().get("runtime_dir")
    if chosen:
        return chosen
    return default_runtime_dir()


def runtime_dir_is_custom():
    return bool(os.environ.get("BGE_REMIX_RUNTIME_DIR") or _read_settings().get("runtime_dir"))


def set_runtime_dir(path):
    """Use another folder for the runtime; None goes back to the default."""
    data = _read_settings()
    if path:
        data["runtime_dir"] = os.path.abspath(path)
    else:
        data.pop("runtime_dir", None)
    _write_settings(data)


def installed():
    return os.path.isfile(os.path.join(runtime_dir(), "d3d9.dll"))


def status():
    """(status, text). Never loads anything: an installed runtime that hasn't
    been used yet shows as INSTALLED."""
    if _bge_remix is None:
        return ("NO_BRIDGE", "This Blender was built without RTX Remix support.")
    code, text = _bge_remix.status()
    if code == "NOT_LOADED":
        if installed():
            return ("INSTALLED", "RTX Remix is installed; it loads when first used.")
        return ("NOT_INSTALLED", "RTX Remix is not installed yet.")
    return code, text


def load():
    if _bge_remix is None:
        return status()
    return _bge_remix.load(runtime_dir())


def startup(width=1280, height=720, visible=True):
    code, text = load()
    if code not in {"READY", "RUNNING"}:
        return code, text
    return _bge_remix.startup(width=width, height=height, visible=visible)


def shutdown():
    if _bge_remix is not None:
        _bge_remix.shutdown()


def material_match(material):
    """BGE-Remix P8.24: how a material reaches RTX Remix, field by field:
    {"fields": [{"field", "level" (0 exactly, 1 approximately, 2 not at all),
    "note"}], "notes": [...], "baked": bool, "categories": int}; None without
    the built-in module."""
    if _bge_remix is None or material is None:
        return None
    return json.loads(_bge_remix.material_match(material.as_pointer()))


def view_stats():
    """the 3D views' counters (BRMX_view_stats_json) as a dict; None without
    the built-in module"""
    if _bge_remix is None:
        return None
    try:
        return json.loads(_bge_remix.view_stats())
    except ValueError:
        return None


def debug_option(key, value):
    """BGE-Remix P9.7: one of the Remix View's debug options (rtx.debugView.*,
    rtx.hashCollisionDetection.enable), sent to Remix when it runs and kept
    for a game started from Blender; bools as True/False"""
    if _bge_remix is None:
        return False
    if isinstance(value, bool):
        text = "True" if value else "False"
    elif isinstance(value, float):
        text = "%g" % value
    else:
        text = str(value)
    _bge_remix.debug_option(key, text)
    return True


def _cache_state(texture):
    """a texture's cache file and state: READY (its file is there), MISSING;
    an image's file is named by its pixels' hash too, so the newest file of
    its name is taken"""
    path, exact = texture.get("file", ""), texture.get("exact", False)
    if not path:
        return "", "MISSING"
    if exact:
        return path, "READY" if os.path.isfile(path) else "MISSING"
    folder, start = os.path.split(path.replace("\\", os.sep))
    try:
        names = [n for n in os.listdir(folder) if n.startswith(start) and n.lower().endswith(".dds")]
    except OSError:
        names = []
    if not names:
        return path + "*.dds", "MISSING"
    names.sort(key=lambda n: os.path.getmtime(os.path.join(folder, n)), reverse=True)
    return os.path.join(folder, names[0]), "READY"


def inspect(obj, scene=None):
    """BGE-Remix P9.7: what RTX Remix gets for an object: {"object", "mesh",
    "triangles", "instances", "cache_folder", "materials": [{"slot", "name",
    "values", "textures": [{"field", "image", "baked", "file", "state"}],
    "match": material_match's}]}; a texture's state is WAITING while the
    3D views still encode textures and its file is not there yet. None
    without the built-in module."""
    if _bge_remix is None or obj is None:
        return None
    r = json.loads(_bge_remix.inspect(obj.as_pointer(), scene.as_pointer() if scene is not None else 0))
    waiting = (view_stats() or {}).get("textures_waiting", 0)
    for mat in r.get("materials", []):
        for tex in mat.get("textures", []):
            tex["file"], tex["state"] = _cache_state(tex)
            if tex["state"] == "MISSING" and waiting:
                tex["state"] = "WAITING"
    return r


def bmp_stats(path, step=97):
    """Mean and standard deviation of a 24-bit BMP's bytes (a quick "is there a
    picture" check, no image library needed). None if unreadable."""
    import struct
    try:
        with open(path, "rb") as f:
            data = f.read()
        offset = struct.unpack_from("<I", data, 10)[0]
    except (OSError, struct.error):
        return None
    px = data[offset::step]
    if not px:
        return None
    mean = sum(px) / len(px)
    var = sum((b - mean) ** 2 for b in px) / len(px)
    return mean, var ** 0.5


def has_picture(path):
    s = bmp_stats(path)
    return s is not None and s[0] > 5 and s[1] > 3


def pin_for_install():
    """The pinned runtime, or a test package named by BGE_REMIX_PIN_OVERRIDE
    (a JSON file with url, sha256, size, version; tests only)."""
    path = os.environ.get("BGE_REMIX_PIN_OVERRIDE")
    if not path:
        return pin

    class _Pin:
        pass

    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    p = _Pin()
    p.URL = data["url"]
    p.SHA256 = data["sha256"]
    p.SIZE = int(data.get("size", 0))
    p.VERSION = data.get("version", "test")
    p.API_VERSION = pin.API_VERSION
    return p


# ---------------------------------------------------------------------------
# BGE-Remix P10: the texture remaster (remaster_tool.py; the measures in
# remaster.py; the Image editor's RTX Remix Remaster panel)

def remaster_audit(write_text=True):
    """BGE-Remix P10.1: every image the game scenes use, with its size, the
    materials and objects using it, its missing channels (roughness, normal,
    height), its texel density (the lowest, pixels a metre), its repeats,
    its painted light, its kind (regular, random, unique) and its plan (fit
    a preset, derive channels, keep); and P10.3's value check of every
    material. Changes nothing in the file but its report, the text
    "BGE-Remix remaster audit" (when write_text). Returns {"images": [...],
    "values": [...], "target": the sharpness target, "scenes": [...]}"""
    from . import remaster_tool
    return remaster_tool.remaster_audit(write_text)


def remaster_fit(image, **kw):
    """BGE-Remix P10.2: the image (bpy.types.Image) matched with the
    presets' catalogues and refined (needs Blender's window, for the bakes):
    {"preset", "settings", "colour", "score", "plan", ...}"""
    from . import remaster_tool
    return remaster_tool.fit_image(image, **kw)
