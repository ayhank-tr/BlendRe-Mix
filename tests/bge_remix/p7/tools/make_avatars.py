"""An icon becomes an avatar mesh: the icon picture on front and back (cut
out by its alpha) and side walls along its outline, depth 0.12 of its size.
Writes <name>.obj (materials "face" and "edge") and <name>_tex.png.
  make_avatars.py <svg dir> <out dir> [texture size, default 128]"""
import sys, glob, os
import cv2, numpy as np, cairosvg

SIZE = int(sys.argv[3]) if len(sys.argv) > 3 else 128
DEPTH = 0.12

def build(svg, outdir):
    name = os.path.splitext(os.path.basename(svg))[0]
    png = os.path.join(outdir, name + "_tex.png")
    cairosvg.svg2png(url=svg, write_to=png, output_width=SIZE, output_height=SIZE)
    img = cv2.imread(png, cv2.IMREAD_UNCHANGED)
    alpha = (img[:, :, 3] > 100).astype(np.uint8) * 255
    contours, _ = cv2.findContours(alpha, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    v, vt, f = [], [], []
    h = DEPTH / 2
    def P(x, y):  # pixels to -0.5..0.5, y up
        return x / SIZE - 0.5, 0.5 - y / SIZE
    # front and back quads
    for z, flip in ((h, False), (-h, True)):
        base = len(v)
        for (x, y), (u, w) in (((0, SIZE), (0, 0)), ((SIZE, SIZE), (1, 0)), ((SIZE, 0), (1, 1)), ((0, 0), (0, 1))):
            px, py = P(x, y)
            v.append((px, py, z)); vt.append((u, w))
        q = [base + 1, base + 2, base + 3, base + 4]
        f.append(("face", q[::-1] if flip else q))
    # side walls
    nside = 0
    for c in contours:
        c = cv2.approxPolyDP(c, max(0.6, SIZE / 256.0), True).reshape(-1, 2)
        if len(c) < 3:
            continue
        for i in range(len(c)):
            a, b = c[i], c[(i + 1) % len(c)]
            ax, ay = P(*a); bx, by = P(*b)
            base = len(v)
            v += [(ax, ay, h), (bx, by, h), (bx, by, -h), (ax, ay, -h)]
            f.append(("edge", [base + 1, base + 2, base + 3, base + 4]))
            nside += 1
    with open(os.path.join(outdir, name + ".obj"), "w") as o:
        o.write("# BGE-Remix Game View avatar: %s\n" % name)
        for p in v: o.write("v %.4f %.4f %.4f\n" % p)
        for t in vt: o.write("vt %.4f %.4f\n" % t)
        cur = None
        for mat, idx in f:
            if mat != cur:
                o.write("usemtl %s\n" % mat); cur = mat
            if mat == "face":
                o.write("f " + " ".join("%d/%d" % (i, i) for i in idx) + "\n")
            else:
                o.write("f " + " ".join(str(i) for i in idx) + "\n")
    return name, nside

os.makedirs(sys.argv[2], exist_ok=True)
for s in sorted(glob.glob(os.path.join(sys.argv[1], "*.svg"))):
    print(*build(s, sys.argv[2]))
