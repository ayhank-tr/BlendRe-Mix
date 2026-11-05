# BGE-Remix P10: the texture remaster's measures, in numpy only (no bpy).
#
# Python 3.5 (Blender 2.79): no f-strings, no variable annotations. numpy as
# Blender 2.79 bundles it: nothing newer than numpy 1.10's API.
#
# What is here:
#   P10.1  each image's kind (regular, random, unique) from its frequency
#          content (FFT), its self-similarity and its edge density; its
#          painted light (a broad brightness slope); texel density
#   P10.2  the features an image is matched by (Bourque and Dudek 2004:
#          the Fourier magnitude and Laplacian-pyramid histograms; ours: a
#          few colour statistics), their distance, the 0..1 score, a
#          Nelder-Mead simplex search, the main colour
#   P10.3  derived channels for images that stay: the painted light evened
#          out, height from brightness detail at several scales, the normal
#          from the height, roughness from brightness and kind; the value
#          check's rules
#
# Images are numpy arrays, rows bottom first (as Blender's pixels), float
# 0..1. Colour images are read in linear light where it matters.

import math

import numpy as np

WORK = 256                  # the size images are measured at (the catalogue's bake size)
SHARP_PX_PER_M = 512.0      # P10.1: the sharpness target (pixels per metre; to be tuned on the pictures)

# P10.1: the kind's thresholds (tuned on the generated test images: noise,
# bricks, a sign; see the guide)
UNIQUE_SPREAD = 0.55        # the edges' spread over the image (its 4 x 4 blocks' coefficient of variation)
REGULAR_PEAKS = 0.18        # the share of the spectrum's power in sharp peaks
REGULAR_SELF = 0.35         # the autocorrelation's strongest repeat (away from the centre)
PEAK_RATIO = 12.0           # a peak: a 3 x 3 sum of the spectrum this many times its ring's mean
PAINTED_LIGHT = 0.3         # P10.1: a broad slope this strong (0..1: the dark side's share darker) is reported

# P10.2: the score
SCORE_SCALE = 0.25          # score = 1 - distance / SCORE_SCALE (clipped): 0.25 or more apart scores 0
SCORE_THRESHOLD = 0.5       # below it the plan becomes "derive channels"

KINDS = ('REGULAR', 'RANDOM', 'UNIQUE')
KIND_NAMES = {'REGULAR': "regular", 'RANDOM': "random", 'UNIQUE': "unique"}

# P10.3: the value check's limits. The Physically Based database
# (physicallybased.info) puts real non-metals' albedo between about 0.02
# (charcoal, fresh asphalt 0.04) and about 0.9 (fresh snow); P9's presets
# keep every pixel inside 0.01..0.95 (p96_tests); metals reflect 0.5 or
# more. Roughness: below 0.05 only water, glass and polished surfaces.
ALBEDO_MEAN = (0.02, 0.90)  # a material's mean albedo (luminance, linear)
ALBEDO_PIXEL = (0.01, 0.95)  # its darkest and brightest pixels (1st and 99th percentiles)
METAL_ALBEDO = 0.5          # a metal's albedo (luminance) at least
ROUGH_LIMITS = (0.05, 0.98)


# ---------------------------------------------------------------------------
# pixels

def from_bytes(data, w, h, channels=4):
    """RGBA bytes (bottom row first) as an (h, w, channels) float array"""
    a = np.frombuffer(data, dtype=np.uint8).reshape(h, w, channels)
    return a.astype(np.float32) / 255.0


def from_floats(flat, w, h, channels=4):
    """Blender's image.pixels (floats, bottom row first) as an array"""
    return np.asarray(flat, dtype=np.float32).reshape(h, w, channels)


def srgb_to_linear(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1.0 / 2.4) - 0.055)


def luminance(rgb):
    return rgb[..., 0] * 0.2126 + rgb[..., 1] * 0.7152 + rgb[..., 2] * 0.0722


def resample(a, n):
    """a (h, w) or (h, w, c) image as n x n: averaged down by whole
    factors, then sampled bilinearly (the image wraps: textures repeat)"""
    a = np.asarray(a, dtype=np.float64)
    h, w = a.shape[0], a.shape[1]
    fy, fx = max(1, h // n), max(1, w // n)
    if fy > 1 or fx > 1:
        hh, ww = (h // fy) * fy, (w // fx) * fx
        a = a[:hh, :ww]
        shape = (hh // fy, fy, ww // fx, fx) + a.shape[2:]
        a = a.reshape(shape).mean(axis=(1, 3))
        h, w = a.shape[0], a.shape[1]
    if h == n and w == n:
        return a
    ys = (np.arange(n) + 0.5) * (h / float(n)) - 0.5
    xs = (np.arange(n) + 0.5) * (w / float(n)) - 0.5
    y0 = np.floor(ys).astype(int)
    x0 = np.floor(xs).astype(int)
    ty = (ys - y0)
    tx = (xs - x0)
    y0m, y1m = y0 % h, (y0 + 1) % h
    x0m, x1m = x0 % w, (x0 + 1) % w
    rows0, rows1 = a[y0m], a[y1m]
    if a.ndim == 3:
        ty = ty[:, None, None]
        txe = tx[None, :, None]
    else:
        ty = ty[:, None]
        txe = tx[None, :]
    top = rows0[:, x0m] * (1.0 - txe) + rows0[:, x1m] * txe
    bot = rows1[:, x0m] * (1.0 - txe) + rows1[:, x1m] * txe
    return top * (1.0 - ty) + bot * ty


def _freqs(n):
    f = np.fft.fftfreq(n) * n  # cycles per image
    fy, fx = np.meshgrid(f, f, indexing='ij')
    return fy, fx


def blur(g, sigma):
    """a periodic Gaussian blur (sigma in pixels), by the FFT"""
    if sigma <= 0.0:
        return g
    h, w = g.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    k = np.exp(-2.0 * (math.pi ** 2) * (sigma ** 2) * (fx ** 2 + fy ** 2))
    return np.real(np.fft.ifft2(np.fft.fft2(g) * k))


def gradient(g):
    """periodic central differences: (d/dx, d/dy), rows bottom first so +y is up"""
    gx = (np.roll(g, -1, axis=1) - np.roll(g, 1, axis=1)) * 0.5
    gy = (np.roll(g, -1, axis=0) - np.roll(g, 1, axis=0)) * 0.5
    return gx, gy


def box_sum(a, r):
    """the sum over a (2r + 1)^2 square around each element (wrapping)"""
    out = np.zeros_like(a)
    for dy in range(-r, r + 1):
        rolled = np.roll(a, dy, axis=0)
        for dx in range(-r, r + 1):
            out += np.roll(rolled, dx, axis=1)
    return out


# ---------------------------------------------------------------------------
# P10.1: painted light

def light_surface(grey, n=32):
    """the broad brightness slope of an image (h, w): a plane fitted to its
    log brightness at n x n (a slope across the image, not its own large
    shapes); returns it at the image's size as a factor around 1 (its mean
    1), and the plane's coefficients (mean, across, up)"""
    small = resample(grey, n)
    logv = np.log(np.maximum(small, 1e-4))
    c = (np.arange(n) + 0.5) / n * 2.0 - 1.0
    y, x = np.meshgrid(c, c, indexing='ij')
    B = np.stack([np.ones_like(x), x, y], axis=-1).reshape(-1, 3)
    coef = np.linalg.lstsq(B, logv.reshape(-1), rcond=-1)[0]
    h, w = grey.shape[0], grey.shape[1]
    cy = (np.arange(h) + 0.5) / h * 2.0 - 1.0
    cx = (np.arange(w) + 0.5) / w * 2.0 - 1.0
    s = np.exp(cx[None, :] * coef[1] + cy[:, None] * coef[2])
    return s / max(float(s.mean()), 1e-6), coef


def painted_light(grey):
    """P10.1: how much shading looks painted in (0 none .. 1 strong: the
    share the slope darkens the dark side by) and on which side the image
    is darker"""
    _s, coef = light_surface(grey)
    bx, by = float(coef[1]), float(coef[2])
    strength = 1.0 - math.exp(-2.0 * math.sqrt(bx * bx + by * by))
    if abs(bx) >= abs(by):
        side = "left" if bx > 0 else "right"
    else:
        side = "bottom" if by > 0 else "top"
    return float(np.clip(strength, 0.0, 1.0)), side


def even_out(rgb, amount=1.0):
    """P10.3: the painted light reduced: the image divided by its broad
    slope (amount 0: as it is, 1: evened out fully). rgb linear (h, w, 3)"""
    if amount <= 0.0:
        return rgb
    s, _coef = light_surface(luminance(rgb))
    f = 1.0 + amount * (s - 1.0)
    return rgb / np.maximum(f, 1e-3)[..., None]


# ---------------------------------------------------------------------------
# P10.1: the kind

def kind_measures(grey):
    """the measures the kind is decided by: peaks (the share of the
    spectrum's power in sharp peaks: a repeat), self (the autocorrelation's
    strongest repeat away from its centre), spread (how unevenly the edges
    lie over the image: 4 x 4 blocks' coefficient of variation) and edges
    (the share of pixels on an edge)"""
    g = resample(grey, WORK)
    g = g / max(float(g.mean()), 1e-4)
    s, _coef = light_surface(g)
    g = g / s
    z = g - g.mean()
    var = float((z * z).mean())
    if var < 1e-8:
        return {"peaks": 0.0, "self": 0.0, "spread": 0.0, "edges": 0.0, "contrast": 0.0}
    # the spectrum (a window: an image that does not tile shows no false
    # lines along the axes)
    win = np.hanning(WORK)
    P = np.abs(np.fft.fft2(z * np.outer(win, win))) ** 2
    P[0, 0] = 0.0
    fy, fx = _freqs(WORK)
    r = np.sqrt(fx ** 2 + fy ** 2)
    s3 = box_sum(P, 1)
    s9 = box_sum(P, 4)
    ring = (s9 - s3) / 72.0
    local_max = P >= np.maximum.reduce([np.roll(np.roll(P, dy, 0), dx, 1)
                                        for dy in (-1, 0, 1) for dx in (-1, 0, 1)])
    peak = local_max & (s3 > PEAK_RATIO * 9.0 * ring) & (r >= 2.0)
    total = float(P.sum())
    peaks = float(s3[peak].sum() / total) if total > 0 else 0.0
    # the autocorrelation (the image tiles: the FFT's circular one)
    A = np.real(np.fft.ifft2(np.abs(np.fft.fft2(z)) ** 2))
    A /= max(float(A[0, 0]), 1e-12)
    ry = np.minimum(np.arange(WORK), WORK - np.arange(WORK))
    rr = np.sqrt(ry[:, None] ** 2 + ry[None, :] ** 2)
    # its strongest local peak beyond the central lobe (where it first
    # falls below half)
    radial = [float(A[(rr >= k - 0.5) & (rr < k + 0.5)].mean()) for k in range(0, WORK // 2)]
    lobe = next((k for k, v in enumerate(radial) if v < 0.5), WORK // 4)
    amax = A >= np.maximum.reduce([np.roll(np.roll(A, dy, 0), dx, 1)
                                   for dy in (-1, 0, 1) for dx in (-1, 0, 1)])
    cand = amax & (rr > max(2 * lobe, 4))
    selfsim = float(A[cand].max()) if cand.any() else 0.0
    # the edges
    gx, gy = gradient(blur(g, 1.0))
    e = np.sqrt(gx ** 2 + gy ** 2)
    blocks = e.reshape(4, WORK // 4, 4, WORK // 4).mean(axis=(1, 3))
    spread = float(blocks.std() / max(float(blocks.mean()), 1e-8))
    edges = float((e > 0.05).mean())
    return {"peaks": round(peaks, 4), "self": round(max(selfsim, 0.0), 4), "spread": round(spread, 4),
            "edges": round(edges, 4), "contrast": round(math.sqrt(var), 4)}


def kind_of(m):
    """P10.1: 'REGULAR' (a strong repeat inside the image), 'RANDOM' (an
    even spread of detail with no repeat) or 'UNIQUE' (strong structure that
    does not repeat: text, signs, faces)"""
    if m["contrast"] < 0.01:
        # a plain image: nothing to match, nothing that repeats
        return 'RANDOM'
    if m["spread"] >= UNIQUE_SPREAD:
        return 'UNIQUE'
    if m["peaks"] >= REGULAR_PEAKS or m["self"] >= REGULAR_SELF:
        return 'REGULAR'
    return 'RANDOM'


# ---------------------------------------------------------------------------
# P10.1: texel density

def texel_density(width, height, uv_area, surface_area):
    """pixels per metre: the image's pixels times the UV area, over the
    surface's area (square metres)"""
    if surface_area <= 1e-12 or uv_area <= 0.0:
        return 0.0
    return math.sqrt(width * height * uv_area / surface_area)


# ---------------------------------------------------------------------------
# P10.2: matching

RADIAL_BINS = 16
ANGLE_BINS = 8
LEVELS = 5
HIST_BINS = 16
HIST_RANGE = 0.6


def _hellinger(p, q):
    p = np.asarray(p, dtype=np.float64)
    q = np.asarray(q, dtype=np.float64)
    p = p / max(float(p.sum()), 1e-12)
    q = q / max(float(q.sum()), 1e-12)
    return float(math.sqrt(max(0.0, 1.0 - float(np.sqrt(p * q).sum()))))


def _pyramid(t, levels=LEVELS):
    """the Laplacian pyramid's bands (finest first): each level minus its
    blurred self, then halved"""
    bands = []
    cur = t
    for _ in range(levels):
        low = blur(cur, 1.0)
        bands.append(cur - low)
        if cur.shape[0] < 8:
            break
        cur = low.reshape(cur.shape[0] // 2, 2, cur.shape[1] // 2, 2).mean(axis=(1, 3))
    return bands


def main_colours(rgb, k=3, iterations=8):
    """P10.2: a few main colours (k-means on a small copy, linear light):
    [(colour, share)], the largest first"""
    small = resample(rgb, 32).reshape(-1, 3)
    lum = luminance(small)
    order = np.argsort(lum)
    centres = small[order[np.linspace(0, len(order) - 1, k).astype(int)]].copy()
    for _ in range(iterations):
        d = ((small[:, None, :] - centres[None, :, :]) ** 2).sum(axis=2)
        lab = d.argmin(axis=1)
        for j in range(k):
            sel = small[lab == j]
            if len(sel):
                centres[j] = sel.mean(axis=0)
    d = ((small[:, None, :] - centres[None, :, :]) ** 2).sum(axis=2)
    lab = d.argmin(axis=1)
    out = [(centres[j].tolist(), float((lab == j).mean())) for j in range(k)]
    out.sort(key=lambda cs: -cs[1])
    return out


def features(rgb):
    """P10.2: what an image (one repeat of it, linear (h, w, 3)) is matched
    by, as a dict of arrays; its brightness and colour divided out, so a
    preset baked at its own colour compares with the image"""
    rgb = resample(np.asarray(rgb, dtype=np.float64)[..., :3], WORK)
    rgb = even_out(rgb, 1.0)
    L = luminance(rgb)
    mean = max(float(L.mean()), 1e-4)
    t = L / mean
    z = t - t.mean()
    P = np.abs(np.fft.fft2(z)) ** 2
    P[0, 0] = 0.0
    fy, fx = _freqs(WORK)
    r = np.sqrt(fx ** 2 + fy ** 2)
    edges = np.exp(np.linspace(0.0, math.log(WORK / 2.0), RADIAL_BINS + 1))
    radial = np.array([P[(r >= edges[i]) & (r < edges[i + 1])].sum() for i in range(RADIAL_BINS)])
    ang = np.mod(np.arctan2(fy, fx), math.pi)
    sel = (r >= 2.0) & (r <= WORK / 4.0)
    angular = np.histogram(ang[sel], bins=ANGLE_BINS, range=(0.0, math.pi), weights=P[sel])[0]
    hists, stds = [], []
    for b in _pyramid(t):
        h = np.histogram(np.clip(b, -HIST_RANGE, HIST_RANGE), bins=HIST_BINS, range=(-HIST_RANGE, HIST_RANGE))[0]
        hists.append(h / max(float(h.sum()), 1.0))
        stds.append(float(b.std()))
    while len(hists) < LEVELS:
        hists.append(np.zeros(HIST_BINS))
        stds.append(0.0)
    mrgb = rgb.reshape(-1, 3).mean(axis=0)
    c = rgb / np.maximum(mrgb, 1e-4)
    d1 = c[..., 0] - c[..., 1]
    d2 = c[..., 2] - c[..., 1]
    tz = t - t.mean()

    def corr(a, b):
        a = a - a.mean()
        den = math.sqrt(float((a * a).mean()) * float((b * b).mean()))
        return float((a * b).mean() / den) if den > 1e-9 else 0.0
    colour = np.array([float(d1.std()), float(d2.std()), corr(d1, tz), corr(d2, tz)])
    return {"radial": radial / max(float(radial.sum()), 1e-12), "angular": angular / max(float(angular.sum()), 1e-12),
            "hists": np.array(hists), "stds": np.array(stds), "colour": colour, "mean_rgb": mrgb}


FEATURE_KEYS = ("radial", "angular", "hists", "stds", "colour", "mean_rgb")


def pack(f):
    """features as one flat vector (the catalogue's rows)"""
    return np.concatenate([np.asarray(f[k], dtype=np.float64).reshape(-1) for k in FEATURE_KEYS])


def unpack(v):
    v = np.asarray(v, dtype=np.float64)
    sizes = (RADIAL_BINS, ANGLE_BINS, LEVELS * HIST_BINS, LEVELS, 4, 3)
    out, at = {}, 0
    for k, n in zip(FEATURE_KEYS, sizes):
        out[k] = v[at:at + n]
        at += n
    out["hists"] = out["hists"].reshape(LEVELS, HIST_BINS)
    return out


def distance(a, b):
    """P10.2: how far apart two images' features are, 0 (the same) .. 1:
    the spectrum's radial and angular spread (Hellinger), the pyramid's
    histograms and each level's contrast, and the colour statistics"""
    d_rad = _hellinger(a["radial"], b["radial"])
    d_ang = _hellinger(a["angular"], b["angular"])
    d_hist = float(np.mean([0.5 * np.abs(ha - hb).sum() for ha, hb in zip(a["hists"], b["hists"])]))
    sa, sb = np.maximum(a["stds"], 1e-4), np.maximum(b["stds"], 1e-4)
    d_std = float(np.mean(np.minimum(np.abs(np.log(sa / sb)) / math.log(4.0), 1.0)))
    d_col = float(min(1.0, np.abs(a["colour"] - b["colour"]).sum() / 0.6))
    return (1.0 * d_rad + 0.5 * d_ang + 1.0 * d_hist + 1.0 * d_std + 0.5 * d_col) / 4.0


def distance_parts(a, b):
    """the parts of distance(), for the report"""
    sa, sb = np.maximum(a["stds"], 1e-4), np.maximum(b["stds"], 1e-4)
    return {"spectrum": round(_hellinger(a["radial"], b["radial"]), 4),
            "direction": round(_hellinger(a["angular"], b["angular"]), 4),
            "pyramid": round(float(np.mean([0.5 * np.abs(ha - hb).sum() for ha, hb in zip(a["hists"], b["hists"])])),
                             4),
            "contrast": round(float(np.mean(np.minimum(np.abs(np.log(sa / sb)) / math.log(4.0), 1.0))), 4),
            "colour": round(float(min(1.0, np.abs(a["colour"] - b["colour"]).sum() / 0.6)), 4)}


def score_of(d):
    """P10.2: the match's score, 0..1 (1: the same)"""
    return float(max(0.0, min(1.0, 1.0 - d / SCORE_SCALE)))


def perceptual(f):
    """P10.2 (Liu et al. 2018's idea, measured by hand): coarseness (0 fine
    .. 1 coarse: where the spectrum's power lies), regularity (0..1: power in
    sharp peaks, as kind_measures) and direction (0 even in all directions
    .. 1 all in one)"""
    radial = np.asarray(f["radial"])
    centre = float((radial * np.arange(len(radial))).sum())
    coarse = 1.0 - centre / max(len(radial) - 1.0, 1.0)
    ang = np.asarray(f["angular"])
    flat = 1.0 / len(ang)
    direction = float(np.clip((ang.max() - flat) / (1.0 - flat), 0.0, 1.0))
    return {"coarseness": round(float(np.clip(coarse, 0.0, 1.0)), 3), "direction": round(direction, 3)}


def nelder_mead(f, x0, step=0.15, iterations=40, tol=1e-4, lo=0.0, hi=1.0):
    """P10.2: the simplex search (Nelder and Mead 1965) of f over x in
    [lo, hi]^n, from x0; returns (x, f(x), evaluations)"""
    x0 = np.clip(np.asarray(x0, dtype=np.float64), lo, hi)
    n = len(x0)
    pts = [x0]
    for i in range(n):
        p = x0.copy()
        p[i] = p[i] + step if p[i] + step <= hi else p[i] - step
        pts.append(np.clip(p, lo, hi))
    vals = [f(p) for p in pts]
    evals = len(pts)
    for _ in range(iterations):
        order = np.argsort(vals)
        pts = [pts[i] for i in order]
        vals = [vals[i] for i in order]
        if abs(vals[-1] - vals[0]) < tol:
            break
        centroid = np.mean(pts[:-1], axis=0)
        xr = np.clip(centroid + (centroid - pts[-1]), lo, hi)
        fr = f(xr)
        evals += 1
        if fr < vals[0]:
            xe = np.clip(centroid + 2.0 * (centroid - pts[-1]), lo, hi)
            fe = f(xe)
            evals += 1
            if fe < fr:
                pts[-1], vals[-1] = xe, fe
            else:
                pts[-1], vals[-1] = xr, fr
        elif fr < vals[-2]:
            pts[-1], vals[-1] = xr, fr
        else:
            xc = np.clip(centroid + 0.5 * (pts[-1] - centroid), lo, hi)
            fc = f(xc)
            evals += 1
            if fc < vals[-1]:
                pts[-1], vals[-1] = xc, fc
            else:
                for i in range(1, len(pts)):
                    pts[i] = np.clip(pts[0] + 0.5 * (pts[i] - pts[0]), lo, hi)
                    vals[i] = f(pts[i])
                    evals += 1
    best = int(np.argmin(vals))
    return pts[best], vals[best], evals


# ---------------------------------------------------------------------------
# P10.3: derived channels (all approximate)

ROUGH_BASE = {'REGULAR': 0.75, 'RANDOM': 0.82, 'UNIQUE': 0.6}
HEIGHT_SCALES = (1.0, 2.0, 4.0, 8.0, 16.0)


def derive_height(grey):
    """height (0..1) from the brightness detail at several scales: each
    band (a difference of blurs) weighted by its scale, so broad shapes
    stand higher than fine grain; dark is low (crevices)"""
    g = np.asarray(grey, dtype=np.float64)
    g = g / max(float(g.mean()), 1e-4)
    h = np.zeros_like(g)
    for s in HEIGHT_SCALES:
        h += (blur(g, s * 0.5) - blur(g, s)) * math.sqrt(s)
    lo, hi = np.percentile(h, 1), np.percentile(h, 99)
    return np.clip((h - lo) / max(hi - lo, 1e-6), 0.0, 1.0)


def derive_normal(height, depth_m, metres_per_pixel):
    """a tangent-space normal map (OpenGL: green up, 0..1 encoded) from a
    height map: height times depth_m metres, pixels metres_per_pixel apart"""
    hx, hy = gradient(np.asarray(height, dtype=np.float64) * depth_m)
    k = 1.0 / max(metres_per_pixel, 1e-9)
    nx, ny = -hx * k, -hy * k
    nz = np.ones_like(nx)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx / ln, ny / ln, nz / ln], axis=-1) * 0.5 + 0.5


def derive_roughness(grey, kind):
    """roughness from the brightness and the kind: darker (hollows, dirt)
    rougher, brighter a little smoother, around the kind's base"""
    g = np.asarray(grey, dtype=np.float64)
    t = g / max(float(g.mean()), 1e-4)
    base = ROUGH_BASE.get(kind, 0.8)
    return np.clip(base - 0.15 * (t - 1.0), 0.3, 0.95)


def derive(rgb, kind, light_amount=1.0, depth_m=0.01, metres_per_pixel=0.002):
    """P10.3: an image's channels: (albedo linear (h, w, 3), roughness,
    height, normal), the albedo's painted light reduced by light_amount"""
    albedo = even_out(np.asarray(rgb, dtype=np.float64)[..., :3], light_amount)
    grey = luminance(albedo)
    height = derive_height(grey)
    normal = derive_normal(height, depth_m, metres_per_pixel)
    rough = derive_roughness(grey, kind)
    return np.clip(albedo, 0.0, 1.0), rough, height, normal


# ---------------------------------------------------------------------------
# P10.3: the value check

def value_problems(albedo_mean, albedo_low=None, albedo_high=None, metallic=0.0, metallic_image=False,
                   roughness=None, translucent=False):
    """P10.3: a material's values against the realistic ranges: [(field,
    problem, suggested fix)]. albedo_mean: its mean albedo (linear RGB);
    albedo_low/high: its image's 1st and 99th percentile luminance"""
    out = []
    lum = float(albedo_mean[0] * 0.2126 + albedo_mean[1] * 0.7152 + albedo_mean[2] * 0.0722)
    metal = metallic >= 0.5
    if not metal and not translucent:
        if lum < ALBEDO_MEAN[0]:
            out.append(("albedo", "darker than any real surface (mean %.3f, the darkest are about %.2f)" %
                        (lum, ALBEDO_MEAN[0]), "brighten the colour to at least %.2f" % ALBEDO_MEAN[0]))
        elif lum > ALBEDO_MEAN[1]:
            out.append(("albedo", "brighter than any real surface (mean %.3f, fresh snow is about %.2f)" %
                        (lum, ALBEDO_MEAN[1]), "darken the colour to %.2f or less" % ALBEDO_MEAN[1]))
        if albedo_low is not None and albedo_low < ALBEDO_PIXEL[0]:
            out.append(("albedo", "its darkest parts are black (%.3f)" % albedo_low,
                        "lift the image's darkest values to about %.2f" % ALBEDO_PIXEL[0]))
        if albedo_high is not None and albedo_high > ALBEDO_PIXEL[1]:
            out.append(("albedo", "its brightest parts are pure white (%.3f)" % albedo_high,
                        "bring the image's brightest values down to about %.2f" % ALBEDO_PIXEL[1]))
    if 0.05 < metallic < 0.95 and not metallic_image:
        out.append(("metallic", "half metal (%.2f): real surfaces are metal or not" % metallic,
                    "set Metallic to 0 (or 1 for bare metal)"))
    if metal and lum < METAL_ALBEDO:
        out.append(("metallic", "metal with a dark colour (%.3f): metals reflect %.1f or more" % (lum, METAL_ALBEDO),
                    "set Metallic to 0, or brighten the colour (a metal's colour is its reflection)"))
    if roughness is not None and not translucent:
        if roughness < ROUGH_LIMITS[0]:
            out.append(("roughness", "a perfect mirror (%.3f)" % roughness,
                        "raise Roughness to 0.1-0.3 unless it is polished"))
        elif roughness > ROUGH_LIMITS[1]:
            out.append(("roughness", "no reflection at all (%.3f)" % roughness,
                        "lower Roughness to about 0.9"))
    return out
