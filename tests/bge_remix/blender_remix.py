"""BGE-Remix: Blender for testing by hand (blender_remix.bat [file.blend]).

Starts the build the tests unpacked (C:\\BGE-Remix\\tests-local\\ours) under
Windows' console debugger when it is installed, so a crash leaves its stack.
Every run keeps its logs in C:\\BGE-Remix\\crash-reports\\<date and time>
(Blender's console, the debugger's report, Remix's own log, and a summary
with the crash's stack in function names), which the tests never touch. The
last 30 runs are kept.
"""

import datetime
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "smoke"))
import crashinfo  # noqa: E402

OURS = r"C:\BGE-Remix\tests-local\ours"
REPORTS = r"C:\BGE-Remix\crash-reports"
KEEP = 30


def main():
    files = sys.argv[1:]
    exe = os.path.join(OURS, "blender.exe")
    if not os.path.isfile(exe):
        print("No build in %s: run run_local.bat once first." % OURS)
        return 1
    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out = os.path.join(REPORTS, stamp)
    os.makedirs(out, exist_ok=True)
    # the oldest runs go
    runs = sorted(d for d in os.listdir(REPORTS) if os.path.isdir(os.path.join(REPORTS, d)))
    for d in runs[:-KEEP]:
        shutil.rmtree(os.path.join(REPORTS, d), ignore_errors=True)
    dbg_log = os.path.join(out, "debugger.log")
    cmd = crashinfo.command(exe, files, dbg_log, os.path.join(out, "debugger.cdb"))
    print("Blender is starting%s." % (" under the debugger" if crashinfo.debugger() else ""))
    print("Its logs go to %s" % out)
    code, text = crashinfo.run(cmd, {"BGE_REMIX_TRACE": "1"}, os.path.join(out, "console.log"), None)
    temp_log = os.path.join(os.environ.get("TEMP", ""), "bge-remix.log")
    if os.path.isfile(temp_log):
        try:
            shutil.copy(temp_log, os.path.join(out, "bge-remix.log"))
        except OSError:
            pass
    rep = crashinfo.crash_report(dbg_log, os.path.join(OURS, "blender.map"))
    lines = ["BGE-Remix by hand, %s" % stamp, "file: %s" % (" ".join(files) or "(none)"), ""]
    if rep.get("crashed"):
        lines += crashinfo.report_lines(rep)
        bakes = [l for l in text.splitlines() if l.startswith("BGE-Remix bake:")][-6:]
        if bakes:
            lines += ["  the bake's last steps:"] + ["    " + b for b in bakes]
        lines += ["", "Blender crashed. Tell Claude: crash report %s" % stamp]
    else:
        lines.append("Blender ended normally (exit %s)." % code)
    with open(os.path.join(out, "summary.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
