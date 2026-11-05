"""BGE-Remix P9.7: the Remix View's scene (run inside our Blender, background,
from make_scenes_p9.py).

p9_remix_view.blend  the study's six spheres with RTX Remix on, saved with
                     the Default screen's 3D View made a Remix View (Check
                     mode, every layer) looking through the camera

make(folder, E) records E["remix_view"]: the file, the spheres' objects and
their screen rectangles (as E["spheres"]).
"""

import os

import bpy

import make_scenes_p9 as m9


def remix_view_areas():
    """the 3D View areas of every screen, the largest first"""
    areas = []
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type in ('VIEW_3D', 'GAME_VIEW'):
                areas.append((screen.name, area))
    areas.sort(key=lambda sa: -(sa[1].width * sa[1].height))
    return areas


def make(folder, E):
    spheres = {}
    m9.make_spheres(folder, spheres)  # the study's spheres (the same files again)
    S = spheres["spheres"]
    bpy.ops.wm.open_mainfile(filepath=os.path.join(folder, "p9_study_spheres_remix.blend"))
    made = []
    areas = remix_view_areas()
    areas.sort(key=lambda sa: sa[0] != "Default")   # the Default screen's first
    for screen_name, area in areas[:1]:
        space = area.spaces.active
        space.is_game_view = True   # the Remix View (P7's RNA name)
        space.game_view_mode = 'CHECK'
        space.region_3d.view_perspective = 'CAMERA'
        made.append(screen_name)
        break
    path = os.path.join(folder, "p9_remix_view.blend")
    bpy.ops.wm.save_as_mainfile(filepath=path, check_existing=False, compress=False)
    E["remix_view"] = {"file": "p9_remix_view", "screens": made,
                       "objects": ["P9_" + label for label in S["order"]],
                       "rects": S["rects"], "order": S["order"], "size": S["size"]}
