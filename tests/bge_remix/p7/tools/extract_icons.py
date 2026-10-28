"""Extract Blender icons from blender_icons.svg into one small SVG each.
Gradient fills become their mean stop colour (Blender's SVG importer has
no gradients). Usage: extract_icons.py <blender_icons.svg> <outdir> NAME..."""
import re, sys, copy, math
from lxml import etree

SVG = "{http://www.w3.org/2000/svg}"
XL = "{http://www.w3.org/1999/xlink}href"

def parse_transform(t):
    m = [1, 0, 0, 1, 0, 0]
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", t or ""):
        a = [float(x) for x in re.split(r"[ ,]+", args.strip()) if x]
        if name == "translate":
            n = [1, 0, 0, 1, a[0], a[1] if len(a) > 1 else 0]
        elif name == "scale":
            n = [a[0], 0, 0, a[1] if len(a) > 1 else a[0], 0, 0]
        elif name == "matrix":
            n = a
        elif name == "rotate":
            r = math.radians(a[0]); c, s = math.cos(r), math.sin(r)
            n = [c, s, -s, c, 0, 0]
            if len(a) == 3:
                cx, cy = a[1], a[2]
                n = mul(mul([1, 0, 0, 1, cx, cy], n), [1, 0, 0, 1, -cx, -cy])
        else:
            continue
        m = mul(m, n)
    return m

def mul(a, b):
    return [a[0]*b[0] + a[2]*b[1], a[1]*b[0] + a[3]*b[1], a[0]*b[2] + a[2]*b[3],
            a[1]*b[2] + a[3]*b[3], a[0]*b[4] + a[2]*b[5] + a[4], a[1]*b[4] + a[3]*b[5] + a[5]]

def apply(m, x, y):
    return m[0]*x + m[2]*y + m[4], m[1]*x + m[3]*y + m[5]

def hexrgb(c):
    c = c.strip().lstrip("#")
    if len(c) == 3:
        c = "".join(ch * 2 for ch in c)
    return [int(c[i:i + 2], 16) for i in (0, 2, 4)]

def main():
    src, out, names = sys.argv[1], sys.argv[2], sys.argv[3:]
    tree = etree.parse(src)
    root = tree.getroot()
    byid = {e.get("id"): e for e in root.iter() if e.get("id")}

    def stops(gid, seen=0):
        g = byid.get(gid)
        if g is None or seen > 5:
            return []
        st = [s for s in g if s.tag == SVG + "stop"]
        if not st and g.get(XL):
            return stops(g.get(XL)[1:], seen + 1)
        res = []
        for s in st:
            style = dict(kv.split(":", 1) for kv in (s.get("style") or "").split(";") if ":" in kv)
            col = style.get("stop-color", s.get("stop-color", "#808080"))
            if col.startswith("#"):
                res.append(hexrgb(col))
        return res

    def flatten(el):
        for e in el.iter():
            style = e.get("style")
            if style:
                def rep(mo):
                    st = stops(mo.group(2))
                    if not st:
                        return mo.group(1) + "none"
                    avg = [sum(c[i] for c in st) // len(st) for i in range(3)]
                    return mo.group(1) + "#%02x%02x%02x" % tuple(avg)
                style = re.sub(r"((?:fill|stroke):)url\(#([^)]+)\)", rep, style)
                e.set("style", style)
            for attr in ("fill", "stroke"):
                v = e.get(attr)
                if v and v.startswith("url("):
                    st = stops(v[5:-1])
                    e.set(attr, "#%02x%02x%02x" % tuple(sum(c[i] for c in st) // len(st) for i in range(3)) if st else "none")

    for name in names:
        g = byid.get("ICON_" + name)
        if g is None:
            print("missing", name)
            continue
        # transforms of the ancestors
        chain = []
        p = g.getparent()
        while p is not None and p is not root:
            chain.append(p.get("transform") or "")
            p = p.getparent()
        chain.reverse()
        m = [1, 0, 0, 1, 0, 0]
        for t in chain:
            m = mul(m, parse_transform(t))
        m = mul(m, parse_transform(g.get("transform")))
        # the 16 x 16 cell: the first rect of that size
        cell = None
        for r in g.iter(SVG + "rect"):
            if r.get("width") == "16" and r.get("height") == "16":
                inner = []
                q = r.getparent()
                while q is not g:
                    inner.append(q.get("transform") or "")
                    q = q.getparent()
                rm = m
                for t in reversed(inner):
                    rm = mul(rm, parse_transform(t))
                rm = mul(rm, parse_transform(r.get("transform")))
                x0, y0 = float(r.get("x")), float(r.get("y"))
                pts = [apply(rm, x0 + dx, y0 + dy) for dx in (0, 16) for dy in (0, 16)]
                cell = (min(p[0] for p in pts), min(p[1] for p in pts))
                break
        if cell is None:
            print("no cell", name)
            continue
        gg = copy.deepcopy(g)
        flatten(gg)
        # drop the invisible cell rects
        for r in list(gg.iter(SVG + "rect")):
            if "opacity:0;" in (r.get("style") or "") and r.get("width") == "16":
                r.getparent().remove(r)
        svg = etree.Element(SVG + "svg", nsmap={None: SVG[1:-1], "xlink": XL[1:XL.index("}")]})
        svg.set("width", "16"); svg.set("height", "16")
        svg.set("viewBox", "%g %g 16 16" % cell)
        outer = svg
        for t in chain:
            w = etree.SubElement(outer, SVG + "g")
            if t:
                w.set("transform", t)
            outer = w
        outer.append(gg)
        etree.ElementTree(svg).write("%s/%s.svg" % (out, name), xml_declaration=True, encoding="utf-8")
        print("ok", name, cell)

main()
