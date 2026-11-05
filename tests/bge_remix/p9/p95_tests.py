# BGE-Remix P9.5: the Texture tab's tests (normal run; called from run_p9.py)

import os

# the Texture tab drawn into a stand-in layout (as P8's T8.30), in Blender
TAB_CHECK = r'''
import bpy, json, os, sys
argv = sys.argv[sys.argv.index("--") + 1:]
target, blend, classic_name, remix_name = argv[0], argv[1], argv[2], argv[3]
bpy.ops.wm.open_mainfile(filepath=blend)
import bl_ui.properties_texture as pt

class Rec:
    def __init__(self, log):
        self.log = log
        self.active = self.enabled = True
        self.alignment = 'EXPAND'
        self.scale_x = self.scale_y = 1.0
        self.operator_context = 'EXEC_DEFAULT'
    def _sub(self, *a, **k):
        return Rec(self.log)
    row = column = split = box = column_flow = _sub
    def prop(self, data, name, **k):
        self.log.append(("prop", data, name))
    def label(self, text="", **k):
        self.log.append(("label", text))
    def operator(self, idname, text="", **k):
        op = Op()
        self.log.append(("operator", idname, text, op))
        return op
    def template_list(self, listtype, *a, **k):
        self.log.append(("template_list", listtype))
    def template_ID(self, data, prop, **k):
        self.log.append(("template_ID", data, prop))
    def __getattr__(self, name):
        def any_call(*a, **k):
            return Rec(self.log)
        return any_call

class Op:
    pass

class Space:
    texture_context = 'MATERIAL'
    use_pin_id = False
    pin_id = None
    use_limited_texture_context = True

class Ctx:
    def __init__(self, ma, node=None):
        self.material, self.scene, self.space_data = ma, bpy.context.scene, Space()
        self.texture_node = node
        self.texture = getattr(node, "texture", None) if node is not None else None
        self.texture_slot = None
        self.lamp = self.world = self.brush = self.line_style = self.particle_system = None
        self.texture_user = None

class Panel:
    pass

def draw(cls, ctx):
    log = []
    p = Panel()
    p.layout = Rec(log)
    err = None
    try:
        if cls.poll(ctx):
            cls.draw(p, ctx)
        else:
            log.append(("not shown",))
    except Exception as ex:
        import traceback
        err = traceback.format_exc()
    return log, err

r = {}
classic = bpy.data.materials[classic_name]
remix = bpy.data.materials[remix_name]
# Classic: the slots as before
log, err = draw(bpy.types.TEXTURE_PT_context_texture, Ctx(classic))
r["classic_error"] = err
r["classic_slot_list"] = any(e[0] == "template_list" and e[1] == "TEXTURE_UL_texslots" for e in log)
r["classic_remix_buttons"] = [e[2] for e in log if e[0] == "operator" and e[1] == "texture.bge_remix_texture_node"]
r["classic_slots"] = [s.texture.name for s in classic.texture_slots if s is not None and s.texture is not None]
r["classic_input_panel"] = bool(bpy.types.TEXTURE_PT_remix_input.poll(Ctx(classic)))
# RTX Remix: the texture nodes, labelled
log, err = draw(bpy.types.TEXTURE_PT_context_texture, Ctx(remix))
r["remix_error"] = err
r["remix_slot_list"] = any(e[0] == "template_list" for e in log)
buttons = [(e[2], e[3]) for e in log if e[0] == "operator" and e[1] == "texture.bge_remix_texture_node"]
r["remix_entries"] = [t for t, _ in buttons]
nt = remix.node_tree
r["entries"] = {}
for text, op in buttons:
    node = nt.nodes.get(getattr(op, "node", ""))
    st = {"text": text, "node": getattr(op, "node", None)}
    if node is not None:
        # the button makes it the active texture node (as the operator does)
        nt.nodes.active = node
        st["active"] = nt.nodes.active == node
        ctx = Ctx(remix, node)
        clog, cerr = draw(bpy.types.TEXTURE_PT_context_texture, ctx)
        ilog, ierr = draw(bpy.types.TEXTURE_PT_remix_input, ctx)
        st["errors"] = [e for e in (cerr, ierr) if e]
        st["texture_shown"] = any(e[0] == "template_ID" and e[1] == node and e[2] == "texture" for e in clog)
        st["labels"] = [e[1] for e in ilog if e[0] == "label"]
        props = [(e[1], e[2]) for e in ilog if e[0] == "prop"]
        st["props"] = [n for _, n in props]
        tex = node.texture
        st["texture"] = tex.name if tex else None
        mapping = pt.remix_mapping_node(node)
        st["mapping"] = mapping.name if mapping else None
        # change them the way the tab does (its properties are the node's)
        for data, name in props:
            if name == "extension" and data == tex:
                data.extension = 'CLIP'
            elif name == "use_interpolation" and data == tex:
                data.use_interpolation = False
            elif name == "scale" and mapping is not None and data == mapping:
                data.scale = (3.0, 4.0, 1.0)
            elif name == "translation" and mapping is not None and data == mapping:
                data.translation = (0.25, 0.5, 0.0)
        # read back from the material itself
        n2 = bpy.data.materials[remix_name].node_tree.nodes[node.name]
        t2 = n2.texture
        st["after"] = {"extension": t2.extension if t2 else None,
                       "use_interpolation": t2.use_interpolation if t2 else None}
        m2 = pt.remix_mapping_node(n2)
        if m2 is not None:
            st["after"]["scale"] = list(m2.scale)
            st["after"]["translation"] = list(m2.translation)
    r["entries"][text] = st
with open(target, "w") as f:
    json.dump(r, f, indent=1)
print("P9-TEXTURE-TAB done")
'''


def normal(args, out, scenes, E, tests, sheets):
    import run_p2 as p2
    import run_p8 as p8
    T = E.get("texture_tab")
    name = "T9.10 The Texture tab (P9.5)"
    if not T:
        tests[name] = {"ok": False, "checks": {"the P9.5 scene was made": False}, "detail": E.get("failed")}
        return
    target = os.path.join(out, "texture_tab_check.json")
    if os.path.isfile(target):
        os.remove(target)
    p8.blender_script(args.ours, out, "p9_texture_tab", TAB_CHECK,
                      [target, os.path.join(scenes, T["file"] + ".blend"), T["classic"], T["remix"]])
    r = p2.read_json(target) or {}
    c = {}
    c["the tab drew without an error"] = bool(r) and not r.get("classic_error") and not r.get("remix_error")
    c["Classic: its texture slots as in 2.79 (the slot list, its two textures)"] = \
        bool(r.get("classic_slot_list")) and r.get("classic_slots") == T["classic_slots"] and \
        not r.get("classic_remix_buttons") and not r.get("classic_input_panel")
    c["RTX Remix: no slot list"] = r.get("remix_slot_list") is False
    entries = r.get("entries") or {}
    labels = sorted(e.split(":")[0] for e in (r.get("remix_entries") or []))
    c["RTX Remix: 3 entries labelled Albedo, Normal, Roughness (%s)" % ", ".join(labels)] = \
        labels == ["Albedo", "Normal", "Roughness"]
    by_tex = {st.get("texture"): st for st in entries.values()}
    for tex, label in sorted(T["labels"].items()):
        st = by_tex.get(tex) or {}
        c["%s: listed as %s, selected shows its texture and the input it feeds" % (tex, label)] = \
            bool(st.get("active")) and bool(st.get("texture_shown")) and not st.get("errors") and \
            any(l == "Feeds: " + label for l in st.get("labels") or []) and \
            (st.get("text") or "").startswith(label + ":")
    img = by_tex.get(T["mapped"]) or {}
    after = img.get("after") or {}
    c["the image's extension and interpolation, changed in the tab, change its texture"] = \
        after.get("extension") == 'CLIP' and after.get("use_interpolation") is False
    c["its mapping, changed in the tab, changes the Mapping node"] = \
        img.get("mapping") is not None and \
        [round(x, 3) for x in after.get("scale") or []] == [3.0, 4.0, 1.0] and \
        [round(x, 3) for x in after.get("translation") or []] == [0.25, 0.5, 0.0]
    rough = by_tex.get("TT_remix_clouds") or {}
    c["the procedural: its strength shown"] = "intensity" in (rough.get("props") or []) or \
        "default_value" in (rough.get("props") or [])
    tests[name] = {"ok": all(c.values()), "checks": c, "detail": r}
