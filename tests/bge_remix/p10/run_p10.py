"""BGE-Remix P10: the texture remaster, the Remix View's edit and run, the
logic bricks brought up to date (run_local.bat p10; run_p10_rtx.bat).

--normal (no RTX card needed, the fake Remix runtime): every track's tests
  that need no real Remix (p10a_tests.normal, p10b_tests.normal,
  p10c_tests.normal)
--rtx (the real RTX Remix runtime): the tracks' pictures against Remix
  (p10a_tests.rtx, p10b_tests.rtx, p10c_tests.rtx)

The tracks (each its own scene module and test module):
  p10a  the texture remaster: the audit (P10.1), preset fitting and bakes
        (P10.2), derived channels and the value check (P10.3), atlases
        (P10.4: its own modules, p10a4), the panel and Apply (P10.5)
  p10b  the Remix View's Edit and Run modes (P10.6-P10.9)
  p10c  the logic bricks (P10.10-P10.13)
"""

import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p9"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p8"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
import run_p2 as p2  # noqa: E402  (running Blender, contact sheets)
import run_p8 as p8  # noqa: E402  (the player, pictures, the fake runtime)
import run_p9 as r9  # noqa: E402,F401  (P9's helpers: pipe_play, mock_materials, textures_in, ...)

TRACKS = ("p10a", "p10a4", "p10b", "p10c")


def make_scenes(ours, out):
    scenes = os.path.join(out, "scenes")
    shutil.rmtree(scenes, ignore_errors=True)
    os.makedirs(scenes, exist_ok=True)
    code, text, _ = p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup", "--python",
                            os.path.join(HERE, "make_scenes_p10.py"), "--", scenes],
                           p2.base_env(out, "make"), os.path.join(out, "make_scenes_p10.log"), timeout=1800)
    expected = p2.read_json(os.path.join(scenes, "p10_expected.json")) or {}
    if code != 0 or not expected:
        print("\n".join((text or "").splitlines()[-40:]))
        raise SystemExit("making the P10 scenes failed, see make_scenes_p10.log")
    return scenes, expected


def track_modules():
    """the tracks' test modules; BGE_REMIX_P10_TRACKS (comma separated, e.g.
    "p10a,p10c") runs only those (a fix round's reruns)"""
    import importlib
    only = [t.strip() for t in os.environ.get("BGE_REMIX_P10_TRACKS", "").split(",") if t.strip()]
    return [(t, importlib.import_module(t + "_tests")) for t in TRACKS if not only or t in only]


def summary(title, tests, sheets, report_lines, name):
    lines = [title, "=" * 70, ""]
    for test, t in tests.items():
        lines.append("%-56s %s" % (test, p2.mark(t["ok"])))
        for check, ok in t["checks"].items():
            lines.append("      %s %s" % ("ok    " if ok else "FAILED", check))
    if report_lines:
        lines += [""] + report_lines
    if sheets:
        lines += ["", "Picture sheets:"] + ["  %-16s %s" % kv for kv in sheets.items() if kv[1]]
    ok = all(t["ok"] for t in tests.values()) and bool(tests)
    lines += ["", "ALL PASSED" if ok else "SOMETHING FAILED (see results_%s.json and the logs)" % name]
    return {"all_ok": ok, "tests": tests, "sheets": sheets}, lines


def run_tracks(args, out, rtx):
    scenes, E = make_scenes(args.ours, out)
    tests, sheets, report = {}, {}, []
    for track, mod in track_modules():
        try:
            if rtx:
                mod.rtx(args, out, scenes, E, tests, sheets, report)
            else:
                mod.normal(args, out, scenes, E, tests, sheets)
        except Exception:
            import traceback
            tests["%s: the track's tests ran" % track] = {"ok": False, "checks": {"ran without an error": False},
                                                          "detail": traceback.format_exc()}
    if E.get("failed"):
        tests["P10 scenes made"] = {"ok": False, "checks": {k + " made": False for k in E["failed"]},
                                    "detail": E["failed"]}
    name = "p10_rtx" if rtx else "p10"
    title = ("BGE-Remix P10 (RTX run: your RTX card, the real RTX Remix runtime)" if rtx else
             "BGE-Remix P10: the texture remaster, the Remix View's edit and run, the logic bricks (normal run)")
    res, lines = summary(title, tests, sheets, report, name)
    return res, lines, name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--official")
    ap.add_argument("--mock")
    ap.add_argument("--results", help="the phases' results")
    ap.add_argument("--normal", action="store_true", help="the normal run (the fake runtime)")
    ap.add_argument("--rtx", action="store_true", help="the RTX run (the real runtime)")
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    p8.OURS["build"] = args.ours
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    args.results = os.path.abspath(args.results or os.path.dirname(out))
    if args.rtx:
        res, lines, name = run_tracks(args, out, True)
    else:
        if not args.mock:
            ap.error("--mock is needed (the fake RTX Remix runtime)")
        res, lines, name = run_tracks(args, out, False)
    with open(os.path.join(out, "results_%s.json" % name), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, "summary_%s.txt" % name), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
