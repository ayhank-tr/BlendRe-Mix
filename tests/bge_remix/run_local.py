"""BGE-Remix: the tests on this PC, in place of GitHub's test machines.

GitHub builds Blender and the player (the "blendremix-win64" download, which
also holds the fake Remix runtime the tests use). This script unpacks the
newest such download from C:\\BGE-Remix\\transfer, runs the phases' tests on
this PC's graphics card, and packs the results as
C:\\BGE-Remix\\transfer\\p0-results.zip, as GitHub's runs did.

  run_local.bat              every phase (P0 to P8)
  run_local.bat p8           only P8 (any list works: p4 p8)

Needs, once: Python 3 with pillow and numpy (python -m pip install pillow
numpy), and the official Blender 2.79b unpacked into
C:\\BGE-Remix\\tests-local\\blender-2.79b-windows64. Windows open and close
while it runs: leave the PC alone until it says it has finished.
"""

import bisect
import datetime
import glob
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

ROOT = r"C:\BGE-Remix"
TRANSFER = os.path.join(ROOT, "transfer")
LOCAL = os.path.join(ROOT, "tests-local")
OFFICIAL = os.path.join(LOCAL, "blender-2.79b-windows64")
OURS = os.path.join(LOCAL, "ours")
MOCK = os.path.join(LOCAL, "mock")
MOCK_SHIM = os.path.join(LOCAL, "mock_shim")
RESULTS = os.path.join(LOCAL, "results")
HERE = os.path.dirname(os.path.abspath(__file__))
PHASES = ["p0", "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8"]


def say(text=""):
    print(text, flush=True)


def stop(text):
    say()
    say("STOPPED: " + text)
    sys.exit(1)


def check_python():
    missing = []
    for name in ("PIL", "numpy"):
        try:
            __import__(name)
        except ImportError:
            missing.append(name)
    if missing:
        stop("Python lacks pillow or numpy. Run this once, then start again:\n\n"
             "    python -m pip install pillow numpy")


def unpack_build():
    zips = glob.glob(os.path.join(TRANSFER, "blendremix-win64*.zip"))
    if not zips:
        stop("no build download in %s.\nDownload \"blendremix-win64\" from the GitHub run's page into that "
             "folder." % TRANSFER)
    newest = max(zips, key=os.path.getmtime)
    say("Build: %s (downloaded %s)" % (os.path.basename(newest),
                                       datetime.datetime.fromtimestamp(os.path.getmtime(newest))
                                       .strftime("%Y-%m-%d %H:%M")))
    for d in (OURS, MOCK, MOCK_SHIM):
        shutil.rmtree(d, ignore_errors=True)
    os.makedirs(OURS)
    with zipfile.ZipFile(newest) as z:
        z.extractall(OURS)
    # a download that holds the folder itself rather than its contents
    if not os.path.isfile(os.path.join(OURS, "blender.exe")):
        found = glob.glob(os.path.join(OURS, "*", "blender.exe"))
        if found:
            inner = os.path.dirname(found[0])
            for name in os.listdir(inner):
                shutil.move(os.path.join(inner, name), os.path.join(OURS, name))
            os.rmdir(inner)
    if not os.path.isfile(os.path.join(OURS, "blender.exe")):
        stop("the download has no blender.exe: is it the \"blendremix-win64\" one?")
    mock = os.path.join(OURS, "bge_remix_test_mock")
    if not os.path.isdir(os.path.join(mock, "mock")):
        stop("the download has no fake Remix runtime (bge_remix_test_mock): it was built before the tests "
             "moved to this PC; push the latest bundle and download the new build")
    shutil.move(os.path.join(mock, "mock"), MOCK)
    shutil.move(os.path.join(mock, "mock_shim"), MOCK_SHIM)
    shutil.rmtree(mock, ignore_errors=True)
    return newest


def commands():
    run = lambda phase, script: [sys.executable, os.path.join(HERE, phase, script)]  # noqa: E731
    off = ["--official", OFFICIAL]
    return {
        "p0": run("p0", "run_p0.py") + ["--ours", OURS] + off + ["--out", RESULTS],
        "p1": run("p1", "run_p1.py") + ["--ours", OURS, "--mock", MOCK, "--mock-shim", MOCK_SHIM,
                                         "--out", os.path.join(RESULTS, "p1")],
        "p2": run("p2", "run_p2.py") + ["--ours", OURS, "--mock", MOCK, "--out", os.path.join(RESULTS, "p2")],
        "p3": run("p3", "run_p3.py") + ["--ours", OURS, "--mock", MOCK] + off + ["--out", os.path.join(RESULTS, "p3")],
        "p4": run("p4", "run_p4.py") + ["--ours", OURS, "--mock", MOCK] + off + ["--out", os.path.join(RESULTS, "p4")],
        "p5": run("p5", "run_p5.py") + ["--ours", OURS, "--mock", MOCK, "--out", os.path.join(RESULTS, "p5")],
        "p6": run("p6", "run_p6.py") + ["--ours", OURS] + off + ["--out", os.path.join(RESULTS, "p6")],
        "p7": run("p7", "run_p7.py") + ["--ours", OURS] + off + ["--mock", MOCK, "--out", os.path.join(RESULTS, "p7")],
        "p8": run("p8", "run_p8.py") + ["--ours", OURS] + off + ["--mock", MOCK, "--out", os.path.join(RESULTS, "p8")],
    }


def clear_phase(phase):
    """a phase's old results go (the other phases' stay, for P8's regression
    test)"""
    if phase == "p0":
        for name in os.listdir(RESULTS):
            path = os.path.join(RESULTS, name)
            if not re.match(r"^p\d$", name) and name not in ("local.txt", "crashes.txt"):
                if os.path.isdir(path):
                    shutil.rmtree(path, ignore_errors=True)
                else:
                    os.remove(path)
    else:
        shutil.rmtree(os.path.join(RESULTS, phase), ignore_errors=True)


def summary_line(phase):
    pattern = os.path.join(RESULTS, "summary.txt") if phase == "p0" else os.path.join(RESULTS, phase, "summary*.txt")
    files = sorted(glob.glob(pattern))
    if not files:
        return "no summary (the runner stopped early: see runner_%s.log)" % phase
    with open(files[0], encoding="utf-8", errors="replace") as f:
        lines = [l.strip() for l in f if l.strip()]
    return lines[-1] if lines else "empty summary"


def run_phase(phase, cmd):
    log = os.path.join(RESULTS, "runner_%s.log" % phase)
    say()
    say("=== %s: %s" % (phase.upper(), time.strftime("%H:%M:%S")))
    start = time.time()
    env = dict(os.environ)
    env.pop("GALLIUM_DRIVER", None)
    with open(log, "w", encoding="utf-8", errors="replace") as f:
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=LOCAL, env=env,
                             universal_newlines=True, encoding="utf-8", errors="replace")
        for line in p.stdout:
            f.write(line)
            sys.stdout.write(line)
        p.wait()
    minutes = (time.time() - start) / 60.0
    return p.returncode, minutes


def read_map(path):
    """the linker's map: (preferred base, sorted addresses, names)"""
    base, rows = None, []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if base is None and "Preferred load address" in line:
                base = int(line.split()[-1], 16)
            p = line.split()
            if len(p) >= 4 and re.match(r"^[0-9a-f]{4}:[0-9a-f]{8}$", p[0]) and re.match(r"^[0-9a-f]{16}$", p[2]):
                rows.append((int(p[2], 16), p[1], p[-1]))
    rows.sort()
    return base, [r[0] for r in rows], rows


def crash_reports():
    """every crash report in the logs, each call named from the maps"""
    maps = {}
    for exe in ("blenderplayer", "blender"):
        path = os.path.join(OURS, exe + ".map")
        if os.path.isfile(path):
            maps[exe + ".exe"] = read_map(path)
    out = []
    for log in sorted(glob.glob(os.path.join(RESULTS, "**", "*.log"), recursive=True)):
        with open(log, encoding="utf-8", errors="replace") as f:
            lines = [l.rstrip() for l in f if l.startswith("BGE-Remix crash")]
        if not lines:
            continue
        out.append("== " + os.path.relpath(log, RESULTS))
        for l in lines:
            m = re.match(r"^BGE-Remix crash frame (\d+): (\S+)\+0x([0-9a-f]+)$", l)
            if m and m.group(2).lower() in maps:
                base, addrs, rows = maps[m.group(2).lower()]
                a = base + int(m.group(3), 16)
                i = bisect.bisect_right(addrs, a) - 1
                if i >= 0:
                    l += "   %s +0x%x (%s)" % (rows[i][1], a - rows[i][0], rows[i][2])
            out.append(l)
    return out


def main():
    say("BGE-Remix tests on this PC")
    say("=" * 40)
    wanted = [a.lower() for a in sys.argv[1:] if a.lower() in PHASES] or PHASES
    check_python()
    if not os.path.isfile(os.path.join(OFFICIAL, "blender.exe")):
        stop("the official Blender 2.79b is not in %s.\nUnpack blender-2.79b-windows64.zip (from "
             "download.blender.org) into %s" % (OFFICIAL, LOCAL))
    os.makedirs(LOCAL, exist_ok=True)
    build = unpack_build()
    os.makedirs(RESULTS, exist_ok=True)
    say("Phases: %s" % " ".join(p.upper() for p in wanted))
    say("Leave the PC alone until this window says it has finished.")
    cmds = commands()
    done = []
    total = time.time()
    for phase in wanted:
        clear_phase(phase)
        os.makedirs(RESULTS, exist_ok=True)
        code, minutes = run_phase(phase, cmds[phase])
        done.append((phase, code, minutes, summary_line(phase)))
    crashes = crash_reports()
    with open(os.path.join(RESULTS, "crashes.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(crashes) + "\n" if crashes else "no crash reports\n")
    with open(os.path.join(RESULTS, "local.txt"), "w", encoding="utf-8") as f:
        f.write("BGE-Remix tests on this PC, %s\n" % time.strftime("%Y-%m-%d %H:%M"))
        f.write("build: %s\n" % os.path.basename(build))
        for phase, code, minutes, line in done:
            f.write("%-3s exit %-3d %6.1f min  %s\n" % (phase.upper(), code, minutes, line))
        f.write("total %.1f min\n" % ((time.time() - total) / 60.0))
    target = os.path.join(TRANSFER, "p0-results.zip")
    if os.path.isfile(target):
        os.remove(target)
    shutil.make_archive(target[:-4], "zip", root_dir=RESULTS)
    say()
    say("=" * 40)
    for phase, code, minutes, line in done:
        say("%-3s %5.1f min  %s" % (phase.upper(), minutes, line))
    if crashes:
        say("crash reports: %d (in crashes.txt)" % sum(1 for l in crashes if l.startswith("==")))
    say()
    say("FINISHED. The results are in %s" % target)
    say("Say \"ready\" to Claude.")


if __name__ == "__main__":
    main()
