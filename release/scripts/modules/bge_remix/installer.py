# BGE-Remix: download, check and unpack the RTX Remix runtime.
#
# Plain Python (no bpy), so the tests can run it directly. Python 3.5.
#
#   install(target_dir, pin, progress=None, cancel=None) -> (ok, message)
#   remove(target_dir) -> (ok, message)
#
# The package is downloaded to "<target>.download", its size and SHA-256 are
# checked, it is unpacked into "<target>.partial" and only then renamed to
# <target>. A failure at any step leaves nothing behind.

import hashlib
import os
import shutil
import urllib.request
import zipfile

CHUNK = 1024 * 1024


class Cancelled(Exception):
    pass


def _report(progress, phase, done, total):
    if progress is not None:
        progress(phase, done, total)


def _check_cancel(cancel):
    if cancel is not None and cancel():
        raise Cancelled()


def _rmtree(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)


def _rmfile(path):
    try:
        if os.path.isfile(path):
            os.remove(path)
    except OSError:
        pass


def download(url, path, size, progress=None, cancel=None):
    """Download url to path; returns the SHA-256 hex digest."""
    digest = hashlib.sha256()
    done = 0
    request = urllib.request.Request(url, headers={"User-Agent": "BGE-Remix (Blender 2.79b)"})
    with urllib.request.urlopen(request, timeout=60) as response, open(path, "wb") as f:
        total = size or int(response.headers.get("Content-Length") or 0)
        while True:
            _check_cancel(cancel)
            chunk = response.read(CHUNK)
            if not chunk:
                break
            f.write(chunk)
            digest.update(chunk)
            done += len(chunk)
            _report(progress, "download", done, total)
    return digest.hexdigest(), done


def unpack(zip_path, folder, progress=None, cancel=None):
    with zipfile.ZipFile(zip_path) as z:
        members = z.infolist()
        total = sum(m.file_size for m in members)
        done = 0
        for m in members:
            _check_cancel(cancel)
            name = m.filename.replace("\\", "/")
            if name.startswith("/") or ".." in name.split("/"):
                raise ValueError("unsafe path in package: " + m.filename)
            z.extract(m, folder)
            done += m.file_size
            _report(progress, "unpack", done, total)


def install(target, pin, progress=None, cancel=None):
    """Install the pinned runtime into the folder `target`."""
    target = os.path.abspath(target)
    zip_path = target + ".download"
    partial = target + ".partial"
    parent = os.path.dirname(target)
    try:
        os.makedirs(parent, exist_ok=True)
        _rmtree(partial)
        _rmfile(zip_path)

        sha, got = download(pin.URL, zip_path, pin.SIZE, progress, cancel)
        if pin.SIZE and got != pin.SIZE:
            raise ValueError("the download is incomplete (%d of %d bytes)" % (got, pin.SIZE))
        if sha.lower() != pin.SHA256.lower():
            raise ValueError("the download is damaged or not the expected file (SHA-256 does not match)")

        unpack(zip_path, partial, progress, cancel)
        if not os.path.isfile(os.path.join(partial, "d3d9.dll")):
            raise ValueError("the package has no d3d9.dll")

        _rmtree(target)
        os.rename(partial, target)
        return True, "RTX Remix %s installed in %s" % (pin.VERSION, target)
    except Cancelled:
        return False, "Installation cancelled"
    except Exception as ex:
        return False, "Installation failed: %s" % ex
    finally:
        _rmfile(zip_path)
        _rmtree(partial)


def remove(target):
    target = os.path.abspath(target)
    if not os.path.isdir(target):
        return True, "Nothing to remove"
    try:
        shutil.rmtree(target)
    except OSError as ex:
        return False, "Could not remove %s: %s (is Blender still using it?)" % (target, ex)
    return True, "Removed " + target
