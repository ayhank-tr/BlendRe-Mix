"""BGE-Remix crash check (run_local.bat smoke; run_smoke.bat): our Blender
in its window, with the real RTX Remix runtime, doing by itself what a person
does by hand (smoke_probe.py), under Windows' console debugger when it is
installed. A crash is reported with the step it happened in and its stack in
function names; a hang with the step it hung in. A few minutes.

With BGE_REMIX_SMOKE_FILE set (run_smoke.bat "C:\\path\\file.blend"), the same
steps run on that file instead of our own scene: the person's own file, on
their PC, never part of the tests.
"""

import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import crashinfo  # noqa: E402

PROBE = os.path.join(HERE, "smoke_probe.py")
WINDOW = ["-p", "0", "0", "1600", "900"]
TIMEOUT = 900


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--file", default=os.environ.get("BGE_REMIX_SMOKE_FILE", ""))
    ap.add_argument("--steps", default=os.environ.get("BGE_REMIX_SMOKE_STEPS", ""))
    args = ap.parse_args()
    out = os.path.abspath(args.out)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    exe = os.path.join(args.ours, "blender.exe")
    if not os.path.isfile(exe):
        exe = os.path.join(args.ours, "blender")
    res = os.path.join(out, "smoke.json")
    dbg_log = os.path.join(out, "debugger.log")
    blender_args = ["--disable-crash-handler"] + WINDOW
    if args.file:
        blender_args.append(args.file)
    blender_args += ["--python", PROBE]
    cmd = crashinfo.command(exe, blender_args, dbg_log, os.path.join(out, "debugger.cdb"))
    env = {
        "BGE_REMIX_SMOKE_OUT": res,
        "BGE_REMIX_SMOKE_STEPS": args.steps or None,
        # the bakes' steps written as they go (a crash in one names it)
        "BGE_REMIX_TRACE": "1",
        "BGE_REMIX_CONVERT": "0" if not args.file else None,
        "BLENDER_USER_CONFIG": os.path.join(out, "user", "config"),
        "BLENDER_USER_SCRIPTS": os.path.join(out, "user", "scripts"),
    }
    for k in ("BLENDER_USER_CONFIG", "BLENDER_USER_SCRIPTS"):
        os.makedirs(env[k], exist_ok=True)
    code, text = crashinfo.run(cmd, env, os.path.join(out, "console.log"), TIMEOUT)
    r = {}
    try:
        with open(res, encoding="utf-8") as f:
            r = json.load(f)
    except (OSError, ValueError):
        pass
    rep = crashinfo.crash_report(dbg_log, os.path.join(args.ours, "blender.map"))
    # Remix's own log of this run
    temp_log = os.path.join(os.environ.get("TEMP", ""), "bge-remix.log")
    if os.path.isfile(temp_log):
        try:
            shutil.copy(temp_log, os.path.join(out, "bge-remix.log"))
        except OSError:
            pass

    lines = ["BGE-Remix crash check" + (" on %s" % args.file if args.file else " (our own scene)"),
             "=" * 70, ""]
    if not crashinfo.debugger():
        lines.append("(Windows' console debugger is not installed: a crash is found, its stack is not)")
    for name, st in (r.get("steps") or {}).items():
        lines.append("  %-6s %-28s %6.1f s" % ("ok" if st.get("ok") else "ERROR", name, st.get("seconds") or 0))
    for e in r.get("errors") or []:
        lines += ["", "Python error in " + e.strip()]
    crashed = rep.get("crashed") or (code not in (0, "timeout") and not r.get("finished"))
    hung = code == "timeout"
    bakes = [l for l in text.splitlines() if l.startswith("BGE-Remix bake:")][-6:]
    lines.append("")
    if crashed:
        lines.append("CRASHED in step %s (exit %s)" % (r.get("doing") or r.get("last_step") or "?", code))
        lines += crashinfo.report_lines(rep)
        if bakes:
            lines += ["  the bake's last steps:"] + ["    " + b for b in bakes]
    elif hung:
        lines.append("HUNG in step %s (no end after %d s)" % (r.get("doing") or "?", TIMEOUT))
    ok = (not crashed and not hung and bool(r.get("finished")) and not r.get("errors"))
    lines += ["", "ALL PASSED" if ok else "SOMETHING FAILED (see the lines above; logs in %s)" % out]
    text_out = "\n".join(lines) + "\n"
    with open(os.path.join(out, "summary_smoke.txt"), "w") as f:
        f.write(text_out)
    with open(os.path.join(out, "results_smoke.json"), "w") as f:
        json.dump({"all_ok": ok, "exit": code, "probe": r, "crash": rep}, f, indent=1, default=str)
    print(text_out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
