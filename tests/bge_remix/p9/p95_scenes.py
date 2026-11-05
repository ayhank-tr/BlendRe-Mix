# BGE-Remix P9.5: the Texture tab's test scene (run inside Blender by
# make_scenes_p9.py)

"""p9_texture_tab: a Classic material with two texture slots (an image and
a Clouds procedural) and an RTX Remix material built by P9.2's builder from
three slots: a colour image (scaled 2x: a Mapping node in front), a normal
image, and a Clouds procedural on the hardness (Classic: through an Invert
node into Roughness)."""


def make(folder, E):
    import bpy
    import make_scenes_p9 as m9
    from bl_operators.bge_remix_material import make_remix_material
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = m9.base("P9TextureTab")
    checker = m9.p3.image("TT_checker", m9.p3.checker, size=64)
    bumps = m9.p3.image("TT_bumps", m9.p3.bumps, size=64, non_color=True)

    classic = m9.classic_material("TT_classic", (0.8, 0.8, 0.8))
    m9.slot_image(classic, checker, color=True)
    clouds = bpy.data.textures.new("TT_classic_clouds", 'CLOUDS')
    s = classic.texture_slots.add()
    s.texture = clouds
    s.texture_coords = 'UV'
    s.use_map_color_diffuse = False
    s.use_map_hardness = True

    remix = m9.classic_material("TT_remix", (0.8, 0.8, 0.8))
    remix.specular_intensity = 0.5
    colour = m9.slot_image(remix, checker, color=True)
    colour.scale = (2.0, 2.0, 1.0)
    m9.slot_image(remix, bumps, normal=True)
    rc = bpy.data.textures.new("TT_remix_clouds", 'CLOUDS')
    s = remix.texture_slots.add()
    s.texture = rc
    s.texture_coords = 'UV'
    s.use_map_color_diffuse = False
    s.use_map_hardness = True
    make_remix_material(remix)

    for i, ma in enumerate((classic, remix)):
        ob = m9.p3.sphere(scene, "TT_" + ma.name, ((i - 0.5) * 2.0, 0.0, 0.0), ma, radius=0.7)
        ob.game.physics_type = 'NO_COLLISION'
    scene.update()
    m9.save_copy(folder, "p9_texture_tab", remix=0, tone=1)
    E["texture_tab"] = {
        "file": "p9_texture_tab", "classic": classic.name, "remix": remix.name,
        "classic_slots": ["TT_classic_TT_checker", "TT_classic_clouds"],
        "labels": {"TT_remix_TT_checker": "Albedo", "TT_remix_TT_bumps": "Normal", "TT_remix_clouds": "Roughness"},
        "mapped": "TT_remix_TT_checker"}
