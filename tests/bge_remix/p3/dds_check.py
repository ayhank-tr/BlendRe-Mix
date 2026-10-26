"""Reads the DDS files of BGE-Remix's texture cache (DX10 header, BC4/BC5/
BC6H/BC7) far enough for the P3 tests: format, size, mips, and the first
block of BC4/BC5 decoded. Python 3, no packages."""

import math
import struct

DXGI = {80: "BC4_UNORM", 83: "BC5_UNORM", 95: "BC6H_UF16", 98: "BC7_UNORM", 99: "BC7_UNORM_SRGB"}


def read_header(path):
    with open(path, "rb") as f:
        data = f.read(148)
    if len(data) < 148 or data[:4] != b"DDS ":
        return None
    h = struct.unpack("<31I", data[4:128])
    height, width, mips = h[2], h[3], max(h[6], 1)
    fourcc = data[84:88]
    fmt = None
    if fourcc == b"DX10":
        fmt = struct.unpack("<I", data[128:132])[0]
    return {"width": width, "height": height, "mips": mips, "dxgi": fmt, "format": DXGI.get(fmt, str(fmt))}


def full_mips(w, h):
    n = 1
    while w > 1 or h > 1:
        w, h = max(w // 2, 1), max(h // 2, 1)
        n += 1
    return n


def _bc4_block(b):
    r0, r1 = b[0], b[1]
    bits = int.from_bytes(b[2:8], "little")
    if r0 > r1:
        pal = [r0, r1] + [((7 - i) * r0 + i * r1) / 7.0 for i in range(1, 7)]
    else:
        pal = [r0, r1] + [((5 - i) * r0 + i * r1) / 5.0 for i in range(1, 5)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] / 255.0 for i in range(16)]


def first_block(path):
    """The 16 texels of the top-left block: [value] for BC4, [(r, g)] for BC5."""
    hdr = read_header(path)
    with open(path, "rb") as f:
        f.seek(148)
        b = f.read(16)
    if hdr["dxgi"] == 80:
        return _bc4_block(b[:8])
    if hdr["dxgi"] == 83:
        return list(zip(_bc4_block(b[:8]), _bc4_block(b[8:16])))
    return None


def octahedral_to_normal(u, v):
    """Remix's unsignedOctahedralToHemisphereDirection."""
    px, py = u * 2 - 1, v * 2 - 1
    tx, ty = (px + py) * 0.5, (px - py) * 0.5
    z = 1 - abs(tx) - abs(ty)
    l = math.sqrt(tx * tx + ty * ty + z * z)
    return (tx / l, ty / l, z / l)


def angle_deg(a, b):
    d = sum(x * y for x, y in zip(a, b))
    la = math.sqrt(sum(x * x for x in a))
    lb = math.sqrt(sum(x * x for x in b))
    return math.degrees(math.acos(max(-1.0, min(1.0, d / (la * lb)))))
