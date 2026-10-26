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
