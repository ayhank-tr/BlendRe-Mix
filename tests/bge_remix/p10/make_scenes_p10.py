"""BGE-Remix P10: the test scenes (run inside our Blender, background).

Each track makes its own scenes in its own module, each with make(folder, E):
  p10a_scenes  the texture remaster (P10.1-P10.3, P10.5)
  p10a4_scenes the texture remaster's atlases (P10.4)
  p10b_scenes  the Remix View's Edit and Run modes (P10.6-P10.9)
  p10c_scenes  the logic bricks (P10.10-P10.13)

What each track expects is written into E under its own keys; a track whose
scenes failed is named in E["failed"]. All of it goes to p10_expected.json.

argv (after --): folder
"""

import json
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # the tracks' scene modules
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p9"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p3"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "p2"))

TRACKS = ("p10a", "p10a4", "p10b", "p10c")


def main():
    import importlib
    argv = sys.argv[sys.argv.index("--") + 1:]
    folder = argv[0]
    os.makedirs(folder, exist_ok=True)
    E = {"failed": {}}
    for track in TRACKS:
        try:
            importlib.import_module(track + "_scenes").make(folder, E)
        except Exception:
            E["failed"][track] = traceback.format_exc()
            print("BGE-Remix: making the %s scenes failed:\n%s" % (track, E["failed"][track]))
    with open(os.path.join(folder, "p10_expected.json"), "w") as f:
        json.dump(E, f, indent=1)
    print("BGE-Remix: wrote", os.path.join(folder, "p10_expected.json"))


if __name__ == "__main__":
    main()
