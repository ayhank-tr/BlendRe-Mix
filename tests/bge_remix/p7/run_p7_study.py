"""BGE-Remix P7.0 study (the Game View, before building it).

S7.1  dry conversion: the game's converter builds each study scene inside
      Blender without running the game (3 times in a row); what it made, by
      kind; what the scene holds that it did not make; the time and memory
      of each part; the editor still works afterwards
S7.2  holograms over the Solid view (CI and RTX) and over RTX Remix's picture
      (RTX only), without and with the depth pass that hides them behind
      objects; the time they take to draw

  python run_p7_study.py --ours <build> --out <dir>          (CI)
  python run_p7_study.py --ours <build> --out <dir> --rtx    (your PC)

A study: the numbers are reported; a run fails only if something broke
(Blender crashed, the conversion did not finish, the editor stopped working).
"""

import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))
import run_p2 as p2  # noqa: E402  (running Blender, contact sheets)

PROBE = os.path.join(HERE, "p7_study_probe.py")
WINDOW = ["-p", "0", "0", "1600", "900"]
SCENES = ("gv_study_mixed", "gv_study_count_1000", "gv_study_count_5000", "gv_study_count_20000")
LEFT_OUT = {"Speaker": "SPEAKER", "Path": "CURVE", "Blob": "META"}


def make_scenes(ours, out):
    scenes = os.path.join(out, "scenes")
    shutil.rmtree(scenes, ignore_errors=True)
    os.makedirs(scenes, exist_ok=True)
    code, text, _ = p2.run([p2.exe(ours, "blender"), "--background", "--factory-startup",
                            "--python", os.path.join(HERE, "make_scenes_p7_study.py"), "--", scenes],
                           p2.base_env(out, "make"), os.path.join(out, "make_scenes_p7_study.log"))
    expected = p2.read_json(os.path.join(scenes, "p7_study_expected.json"))
    if code != 0 or not expected:
        print("\n".join(text.splitlines()[-40:]))
        raise SystemExit("making the P7 study scenes failed, see make_scenes_p7_study.log")
    for name, tb in expected.get("failed", {}).items():
        print("making the %s scene failed:\n%s" % (name, tb))
    return scenes, expected


def ui_run(ours, out, name, blend, parts, wait=30, timeout=2400):
    res = os.path.join(out, name + ".json")
    report = os.path.join(out, name + "_dry.json")
    for f in (res, report):
        if os.path.isfile(f):
            os.remove(f)
    e = p2.base_env(out, name)
    e.update({"BGE_REMIX_P7_OUT": res, "BGE_REMIX_P7_DIR": out, "BGE_REMIX_P7_REPORT": report,
              "BGE_REMIX_P7_PARTS": parts, "BGE_REMIX_P7_WAIT": str(wait), "BGE_REMIX_P7_REPEAT": "3"})
    code, text, secs = p2.run([p2.exe(ours, "blender")] + WINDOW + [blend, "--python", PROBE], e,
                              os.path.join(out, name + ".log"), timeout=timeout, cwd=out)
    probe = p2.read_json(res)
    if code != 0 or not probe.get("finished"):
        print("---- %s: exit %s; the end of its output:" % (name, code))
        print("\n".join("    " + l for l in text.splitlines()[-30:]))
    for err in probe.get("errors", []):
        print("---- %s: %s" % (name, err))
    return {"exit": code, "seconds": round(secs, 1), "probe": probe, "report": p2.read_json(report),
            "steps": {s["step"]: s for s in probe.get("steps", [])}, "text": text}


def median(values):
    v = sorted(x for x in values if x is not None)
    return v[len(v) // 2] if v else None


def conversion(r):
    """S7.1 for one scene: what broke (checks) and the numbers (detail)"""
    rep = r["report"]
    runs = rep.get("runs", [])
    conv = r["steps"].get("convert", {}).get("convert") or {}
    c = {"Blender ran to the end": r["exit"] == 0 and r["probe"].get("finished", False),
         "the dry conversion finished": "FINISHED" in (conv.get("operator") or []) and len(runs) == 3,
         "the editor works afterwards": bool(r["steps"].get("after_convert", {}).get("moved"))}
    d = {"elements": rep.get("element_count"), "kinds": rep.get("kinds"), "blender": rep.get("blender"),
         "left_out": rep.get("left_out"), "operator_seconds": conv.get("seconds")}
    for key in ("setup_ms", "convert_ms", "list_ms", "teardown_ms", "total_ms"):
        d[key] = [round(x.get(key), 1) for x in runs]
        d[key + "_median"] = median(x.get(key) for x in runs)
    if runs:
        d["memory_mb"] = {"before": runs[0].get("mem_before_mb"), "while_converted": runs[0].get("mem_converted_mb"),
                          "after_run_1": runs[0].get("mem_after_mb"), "after_run_3": runs[-1].get("mem_after_mb")}
        d["kept_after_3_runs_mb"] = round(runs[-1].get("mem_after_mb", 0) - runs[0].get("mem_before_mb", 0), 1)
    return c, d


def mixed_checks(d):
    """what the mixed scene's report must show"""
    kinds = d.get("kinds") or {}
    left = d.get("left_out") or []
    left_names = {x.get("object") for x in left}
    c = {
        "every kind of object in the report": all(("object:" + k) in kinds for k in ("MESH", "EMPTY", "LAMP", "CAMERA",
                                                                                   "ARMATURE", "TEXT")),
        "the hidden-layer cube in the report": any("hidden layer" in k for k in kinds),
        "sensors, controllers and actuators by type": any(k.startswith("sensor:") for k in kinds) and
        any(k.startswith("controller:") for k in kinds) and any(k.startswith("actuator:") for k in kinds),
        "the speaker, curve and metaball are listed as left out": set(LEFT_OUT) <= left_names,
        "the particle system and force field are listed as left out": "Smoke" in left_names and "Fan" in left_names,
    }
    blender = d.get("blender") or {}
    made = sum(v for k, v in kinds.items() if k.startswith("sensor:"))
    c["every sensor in the file was made"] = made == blender.get("sensors")
    return c


def run(args, out, rtx):
    scenes, expected = make_scenes(args.ours, out)
    res, lines = {}, ["BGE-Remix P7.0 study (%s)" % ("RTX" if rtx else "CI"), "=" * 32, ""]
    table = []
    for name in SCENES:
        blend = os.path.join(scenes, name + ".blend")
        if not os.path.isfile(blend):
            res[name] = {"ok": False, "checks": {"the scene was made": False}}
            continue
        parts = "convert"
        if name == "gv_study_mixed":
            parts += ",plain" + (",remix" if rtx else "")
        r = ui_run(args.ours, out, name, blend, parts)
        c, d = conversion(r)
        if name == "gv_study_mixed":
            c.update(mixed_checks(d))
            for step in ("plain_holo", "remix_nodepth", "remix_depth"):
                if step in r["steps"]:
                    s = r["steps"][step]
                    d[step] = {"screenshot": s.get("screenshot"), "draw_ms_median": s.get("draw_ms_median"),
                               "frames": s.get("draw_frames")}
                    c["holograms drawn (%s)" % step] = bool(s.get("draw_frames"))
        res[name] = {"ok": all(c.values()), "checks": c, "detail": d}
        table.append((name, d))
    if rtx and "gv_study_mixed" in res:
        d = res["gv_study_mixed"]["detail"]
        res["sheet"] = p2.contact_sheet([("Solid, depth pass", (d.get("plain_holo") or {}).get("screenshot")),
                                         ("RTX Remix, no depth pass", (d.get("remix_nodepth") or {}).get("screenshot")),
                                         ("RTX Remix, depth pass", (d.get("remix_depth") or {}).get("screenshot"))],
                                        os.path.join(out, "sheet_p7_holograms.png"))

    res["all_ok"] = all(v["ok"] for k, v in res.items() if isinstance(v, dict) and "ok" in v)
    for name in SCENES:
        if name not in res:
            continue
        lines.append("%-24s %s" % (name, p2.mark(res[name]["ok"])))
        for check, ok in res[name]["checks"].items():
            if not ok:
                lines.append("         failed: " + check)
    lines += ["", "S7.1 dry conversion, median of 3 (ms):",
              "   %-22s %9s %7s %8s %6s %9s %7s %9s" % ("scene", "elements", "setup", "convert", "list", "teardown",
                                                        "total", "kept MB")]
    for name, d in table:
        lines.append("   %-22s %9s %7s %8s %6s %9s %7s %9s" % (
            name, d.get("elements"), _r(d.get("setup_ms_median")), _r(d.get("convert_ms_median")),
            _r(d.get("list_ms_median")), _r(d.get("teardown_ms_median")), _r(d.get("total_ms_median")),
            d.get("kept_after_3_runs_mb")))
    mixed = res.get("gv_study_mixed", {}).get("detail", {})
    if mixed:
        lines += ["", "S7.1 gv_study_mixed, what the converter made (kind: count):"]
        for k, v in sorted((mixed.get("kinds") or {}).items()):
            lines.append("   %-50s %d" % (k, v))
        lines.append("   left out: " + ", ".join("%s (%s)" % (x.get("object"), x.get("what"))
                                                for x in mixed.get("left_out") or []))
        lines += ["", "S7.2 holograms, draw time per frame (ms, median):"]
        for step in ("plain_holo", "remix_nodepth", "remix_depth"):
            if step in mixed:
                lines.append("   %-16s %s" % (step, _r(mixed[step].get("draw_ms_median"))))
    if res.get("sheet"):
        lines.append("S7.2 sheet: %s" % res["sheet"])
    lines += ["", "STUDY RAN" if res["all_ok"] else "SOMETHING BROKE (see results_p7_study.json and the .log files)"]
    return res, lines


def _r(v):
    return "-" if v is None else "%.1f" % v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--rtx", action="store_true")
    args = ap.parse_args()
    args.ours = os.path.abspath(args.ours)
    out = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    res, lines = run(args, out, args.rtx)
    name = "results_p7_study" + ("_rtx" if args.rtx else "")
    with open(os.path.join(out, name + ".json"), "w") as f:
        json.dump(res, f, indent=2, default=str)
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, name.replace("results", "summary") + ".txt"), "w") as f:
        f.write(text)
    print(text)
    sys.exit(0 if res["all_ok"] else 1)


if __name__ == "__main__":
    main()
