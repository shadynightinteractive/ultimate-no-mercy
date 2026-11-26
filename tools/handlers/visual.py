#!/usr/bin/env python3
"""visual (modcheck, 2026-11-13): the visual check -- every imported wrestler and costume, as the SOURCE game draws him and as
OUR game does, compared automatically, region by region; mismatches flagged with the pictures side by side.

Ours: a QA run of tool\\scenario.py's Superstar Select tour (every costume of his on the select screen, many times over).
The source: the rig's pictures of the same man in the source game (rig\\nm_sel.py: No Mercy Plus's own select, the same
screen, framing and pose as ours; rig\\rv_shots.py: Revenge; Badd Blood's select). Each side has a PLATE -- the same screen
without him (ours: the per-pixel median of the run's select pictures, where he is missing most of the time; No Mercy
Plus's: the frames before his model has loaded) -- so the figure is what differs from it.

Not pixel-exact (poses, lighting and resolution differ): the figure is cut into six bands by height (head and hair,
shoulders and chest, the torso, hips, legs, feet) and each band compared by its colours (a 64-colour histogram's overlap
and the mean colour, in Lab) and its shape (how wide the figure is there). A band past the threshold is a MISMATCH.

  python tool\\visual.py nm  --name <who> --run <qa run> --id 0x0390 --rig <rig picture prefix> [--costumes 4] --out <dir>
  python tool\\visual.py cross --name <who> --run <qa run> --id ... --src <picture> [--src-plate <picture>] --view upper ...
-> <dir>\\<who>.txt (each costume, each band, PASS / MISMATCH) and <who>_c<k>.png (the two side by side, the bands marked).
"""
import argparse, csv, glob, json, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
if os.name == "nt":                # (the machine is shared: below normal priority, RULES.md)
    import ctypes; ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)

# No Mercy's Superstar Select: where player 1's / the CPU's model stands, as fractions of the screen (ours 1280 x 960, the
# rig's 640 x 240: the same 4:3 picture). The name bar covers his feet from 0.74 down.
NM_BOX = (0.42, 0.20, 0.69, 0.76)
BANDS = [("head", 0.00, 0.15), ("chest", 0.15, 0.33), ("torso", 0.33, 0.50), ("hips", 0.50, 0.64), ("legs", 0.64, 0.86),
         ("feet", 0.86, 1.00)]
W, H = 160, 320                    # (the box, as compared)
THRESH = 0.48                      # a band's score past this is a mismatch (calibrated: work/visual_cal.txt -- the known-good No Mercy Plus men's right costumes all under it, worst 0.44)

def lab(rgb):
    """sRGB 0..1 -> CIE Lab"""
    c = np.where(rgb > 0.04045, ((rgb + 0.055) / 1.055) ** 2.4, rgb / 12.92)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ M.T / np.array([0.9505, 1.0, 1.089])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], axis=-1)

def boxed(img, box):
    w, h = img.size
    return img.crop((int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h))).resize((W, H), Image.BILINEAR)

def figure(img, plate, box, thr=34, largest=False):
    """the figure: where the picture differs from its plate (both boxed, a little blurred), cleaned of specks"""
    a = np.asarray(boxed(img, box).filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float32)
    b = np.asarray(boxed(plate, box).filter(ImageFilter.GaussianBlur(1.2)), dtype=np.float32)
    m = np.abs(a - b).max(axis=2) > thr
    mi = Image.fromarray((m * 255).astype(np.uint8))
    mi = mi.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(3))
    m = np.asarray(mi) > 127
    if largest and m.any():
        # (another engine's plate isn't perfect -- hiding Revenge's slot-B man moves other things on screen a little: keep
        #  his body, the biggest piece, and the pieces at least a fifth its size)
        from scipy import ndimage
        lab_, n = ndimage.label(m)
        if n > 1:
            sz = ndimage.sum(m, lab_, range(1, n + 1))
            m = np.isin(lab_, [i + 1 for i, v in enumerate(sz) if v >= 0.2 * sz.max()])
    return np.asarray(boxed(img, box), dtype=np.float32) / 255.0, m

# the bands in HEAD heights from the top of his head (a man is about 8): the same body part in either picture, whatever its
# scale, framing or where the screen cuts him off (No Mercy's select cuts him at the shins behind the name bar, our arcade's
# at the ankles; Revenge's close-ups at the thighs)
BANDS = [("head", 0.0, 1.0), ("chest", 1.0, 2.4), ("torso", 2.4, 3.6), ("hips", 3.6, 4.6), ("legs", 4.6, 6.4), ("feet", 6.4, 8.0)]

def features(rgb, m, view="full", u_scale=1.0, shift=0.0):
    """per band: (64-bin colour histogram, mean Lab, width in head heights) of the figure, and his silhouette"""
    ys, xs = np.nonzero(m)
    if len(ys) < 200: return None
    x0, x1 = np.percentile(xs, 1), np.percentile(xs, 99) + 1
    # (his top is his head's: taken in the middle of his width -- hands raised over his head are beside it)
    mid = (xs >= x0 + 0.3 * (x1 - x0)) & (xs <= x0 + 0.7 * (x1 - x0))
    y0 = int(np.percentile(ys[mid], 0.5)) if mid.sum() > 50 else int(ys.min())
    y1 = int(ys.max()) + 1
    # his neck: the narrowest row below the head, in the middle columns (the head's height is the unit)
    cols = slice(int(x0 + 0.15 * (x1 - x0)), int(x1 - 0.15 * (x1 - x0)) + 1)
    wid = m[:, cols].sum(axis=1).astype(np.float32)
    span = y1 - y0
    lo, hi = y0 + max(3, int(0.05 * span)), y0 + max(6, int(0.30 * span))
    seg = wid[lo:hi]
    if len(seg) < 3: return None
    k = int(np.argmin(np.convolve(seg, np.ones(3) / 3, mode="same")[1:-1])) + 1
    u = max(4.0, float(lo + k - y0)) * u_scale
    out = {"_u": u}
    L = lab(rgb)
    q = np.clip((rgb * 4).astype(int), 0, 3); idx = q[..., 0] * 16 + q[..., 1] * 4 + q[..., 2]
    hue = hue16(rgb)
    xc0, xc1 = x0 + (x1 - x0) / 3.0, x0 + 2 * (x1 - x0) / 3.0
    colx = np.arange(m.shape[1])
    centre = (colx >= xc0) & (colx < xc1)
    for name, a, b in BANDS:
        r0, r1 = int(y0 + (a + shift) * u), int(y0 + (b + shift) * u)
        r0 = max(r0, y0)
        if r0 >= y1 or (min(r1, y1) - r0) < 0.6 * (r1 - r0): out[name] = None; continue
        r1 = min(r1, y1)
        mm = m[r0:r1]
        n = int(mm.sum())
        if n < 30: out[name] = None; continue
        hist = np.bincount(idx[r0:r1][mm], minlength=64).astype(np.float64); hist /= hist.sum()
        width = float(mm.any(axis=0).sum()) / u
        # (by hue, the middle third and the sides apart: a shirt for none darkens the arms, a logo the chest -- the
        #  whole band's colours alone don't tell Mideon's black T-shirt from Goldust's black-and-gold suit)
        hc = np.bincount(hue[r0:r1][:, centre][mm[:, centre]], minlength=16).astype(np.float64)
        hs = np.bincount(hue[r0:r1][:, ~centre][mm[:, ~centre]], minlength=16).astype(np.float64)
        hc /= max(1.0, hc.sum()); hs /= max(1.0, hs.sum())
        out[name] = (hist, L[r0:r1][mm].mean(axis=0), width, hc, hs)
    out["_bbox"] = (int(x0), int(y0), int(x1), int(y1))
    out["_area"] = int(m.sum())
    hy, hx = np.nonzero(m[y0:int(y0 + u)])
    out["_hx"] = float(hx.mean()) if len(hx) else 0.5 * (x0 + x1)      # (the middle of his head)
    # (his silhouette over the first 5 head heights, in head units: the pose -- ours most like the source's is compared)
    r1 = min(y1, int(y0 + 5 * u)); cx = 0.5 * (x0 + x1)
    sil = Image.fromarray((m[y0:r1, max(0, int(cx - 2 * u)):int(cx + 2 * u)] * 255).astype(np.uint8)).resize((24, 30), Image.BILINEAR)
    out["_sil"] = np.asarray(sil, dtype=np.float32) / 255.0
    return out

def hue16(rgb):
    """per pixel: 12 hue bins for coloured pixels (saturation > 0.3, value > 0.2), 4 grey levels for the rest"""
    mx = rgb.max(axis=2); mn = rgb.min(axis=2); d = mx - mn + 1e-6
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    h = np.where(mx == r, ((g - b) / d) % 6, np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) / 6.0
    sat = d / (mx + 1e-6)
    col = (sat > 0.3) & (mx > 0.2)
    return np.where(col, np.clip((h * 12).astype(int), 0, 11), 12 + np.clip((mx * 4).astype(int), 0, 3)).astype(np.int64)

def band_score(f1, f2):
    """0 = the same; a band's colours (histogram overlap, mean colour) and shape (width)"""
    if f1 is None or f2 is None: return None
    h = 1.0 - float(np.minimum(f1[0], f2[0]).sum())
    de = float(np.linalg.norm(f1[1] - f2[1])) / 100.0
    wd = abs(f1[2] - f2[2])
    return 0.5 * h + 0.8 * de + 0.15 * wd

def fit(fs, rgb, m, scales=10, shifts=(-0.5, -0.25, 0.0, 0.25, 0.5)):
    """our figure's bands lined up with the source's: the neck found in a picture is a few pixels off with hair, a turned
    head or a beard, a man's size on screen differs by his height, and he stands in another pose -- so ours is tried at
    0.8 to 1.25 times its own head height, and each band up to half a head higher or lower; per band the closest is kept,
    at the scale whose worst band is best. (Lenient on purpose: a wrong part -- a shirt for none -- differs in every one.)"""
    best = None
    for r in np.linspace(0.8, 1.25, scales):
        per = {}
        fo0 = None
        for sh in shifts:
            fo = features(rgb, m, u_scale=float(r), shift=sh)
            if fo is None: continue
            if sh == 0.0: fo0 = fo
            for k, v in compare(fs, fo).items():
                if v is not None and (k not in per or v < per[k][0]): per[k] = (v, fo[k])
        if fo0 is None or len(per) < 2: continue
        rows = {k: v[0] for k, v in per.items()}
        sc = max(rows.values())
        if best is None or sc < best[0]: best = (sc, fo0, rows)
    return best

# ---- the same engine: the two pictures in the same pose, compared place by place ------------------------------------------
AW, AH = 32, 48                    # (him from the top of his head down 6 head heights, 4 wide, at this size)
PX_BANDS = [("head", 0, 8), ("chest", 8, 19), ("torso", 19, 29), ("hips", 29, 37), ("legs", 37, 48)]
def aligned(rgb, m, f):
    """his figure cut out in head units -- from the top of his head 6 head heights down, 2 either side of his middle -- at
    AW x AH: (Lab picture, mask), the same body places at the same pixels in any picture of him"""
    x0, y0, x1, y1 = f["_bbox"]; u = f["_u"]
    ys, xs = np.nonzero(m[y0:int(y0 + 3 * u)])
    cx = float(xs.mean()) if len(xs) else 0.5 * (x0 + x1)
    box = (int(cx - 2 * u), int(y0), int(cx + 2 * u), int(y0 + 6 * u))
    def cut(a, mode):
        im = Image.fromarray(a)
        return np.asarray(im.crop(box).resize((AW, AH), mode))
    A = cut((np.clip(rgb, 0, 1) * 255).astype(np.uint8), Image.BOX).astype(np.float32) / 255.0
    Mk = cut((m * 255).astype(np.uint8), Image.BILINEAR) > 127
    return lab(A), Mk

def pose_pair(srcs, ours):
    """of the source's poses and ours, the pair most alike in silhouette (overlap over union of the aligned masks)"""
    best = None
    for i, (Ls, Ms) in enumerate(srcs):
        for j, (Lo, Mo) in enumerate(ours):
            iou = float((Ms & Mo).sum()) / max(1, (Ms | Mo).sum())
            if best is None or iou > best[0]: best = (iou, i, j)
    return best

def px_rows(Ls, Ms, Lo, Mo):
    """per band: the mean colour difference (Lab, / 100) where both are him, and the share of either not covered by both"""
    out = {}
    for name, r0, r1 in PX_BANDS:
        both = Ms[r0:r1] & Mo[r0:r1]; either = Ms[r0:r1] | Mo[r0:r1]
        if both.sum() < 15: out[name] = None; continue
        # (the share of the band's pixels whose colour is clearly another -- Lab 30 apart or more: a black shoulder where the
        #  suit's is gold -- rather than the mean, which a few such places hardly move; the pictures are a little blurred
        #  first, and the silhouettes' disagreement counts a little too)
        d = np.linalg.norm(Ls[r0:r1] - Lo[r0:r1], axis=2)
        out[name] = float((d[both] > 30).mean()) + 0.3 * (1.0 - both.sum() / max(1, either.sum()))
    return out

def pose_alike(fs, fo, view="full"):
    """0..1: how alike two silhouettes are (overlap over union), both in head units from the top of the head"""
    a = fs["_sil"]; b = fo["_sil"]
    return float(np.minimum(a, b).sum() / max(1e-6, np.maximum(a, b).sum()))

def compare(fs, fo):
    rows = {}
    for name, _, _ in BANDS:
        if name in fs and name in fo: rows[name] = band_score(fs[name], fo[name])
    return rows

# ---- our side: a QA run of the select tour -----------------------------------------------------------------------------
DIGITS = np.load(os.path.join(HERE, "type_digits.npy"))     # (scripts/type_digits.py: the select's "Type N" digit, 1-4)
DIGIT_BOX = (0.657, 0.828, 0.678, 0.888)
def type_shown(img):
    """the costume the select picture shows, read off its "Type N" digit (0-based), or None"""
    w, h = img.size
    a = np.asarray(img.crop((int(DIGIT_BOX[0] * w), int(DIGIT_BOX[1] * h), int(DIGIT_BOX[2] * w), int(DIGIT_BOX[3] * h)))
                   .resize((16, 20), Image.BILINEAR), dtype=np.int16)
    g = ((a[..., 0] > 165) & (a[..., 1] > 165) & (a[..., 2] > 165)).astype(np.float32)
    if g.sum() < 20: return None
    sc = [float((g * t).sum() / np.sqrt(g.sum() * t.sum())) for t in DIGITS]
    o = sorted(sc, reverse=True)
    return sc.index(o[0]) if o[0] > 0.8 and o[0] - o[1] > 0.15 else None

def is_select(img):
    """No Mercy's Superstar Select: its yellow title at the top left"""
    a = np.asarray(img.resize((320, 240)), dtype=np.int16)
    yel = lambda r: ((r[..., 0] > 150) & (r[..., 1] > 150) & (r[..., 2] < 110))
    # (and not a yellow screen: the title's flashes are yellow all over -- run 171541_mc_texa)
    return float(yel(a[18:60, 25:150]).mean()) > 0.12 and float(yel(a).mean()) < 0.15

def our_shots(run, wid):
    """the run's select pictures of him -> {costume: [pictures]}, [all his select pictures]. Which costume: the picture's own
    "Type N" digit. That it's HIM: state.csv bs_tries (the Superstar under the cursor) all through the 1.5 s either side --
    its rows' times ("ms") and the pictures' (seconds from qa.ps1's start) are seconds apart, so the two are lined up first
    by where the select screen begins in each."""
    rows = []
    for r in csv.DictReader(open(os.path.join(run, "state.csv"))):
        try: bt = int(r["bs_tries"], 0); ms = int(r["ms"])
        except (ValueError, KeyError): continue
        rows.append((ms, bt))
    ms_ = np.array([r[0] for r in rows]); bt_ = np.array([r[1] for r in rows])
    on = (bt_ >> 24) & 1
    if not on.any(): return {}, []
    first_row = ms_[np.argmax(on)]
    pics = []
    for p in sorted(glob.glob(os.path.join(run, "shots", "*.png"))):
        try: t = float(os.path.basename(p).split("s")[0]) * 1000
        except ValueError: continue
        pics.append((t, p))
    first_pic = None
    for t, p in pics:
        if t >= first_row - 5000 and is_select(Image.open(p).convert("RGB")): first_pic = t; break
    if first_pic is None: return {}, []
    off = first_pic - first_row
    # (the rows' times and the pictures' are only roughly lined up: which pictures are HIM is told by the pictures
    #  themselves -- the 1P box shows the photo of the man under the cursor, and his is the one shown longest while the
    #  state says he's under it: his tour of the Type arrows. Run 155918_mc_sel_thrasher: by the times alone, Rikishi's)
    his = ms_[((bt_ >> 24) & 1).astype(bool) & ((bt_ & 0xFFFF) == wid)]
    if len(his) == 0: return {}, []
    t0, t1 = his.min() + off - 8000, his.max() + off + 8000
    cand = []
    for t, p in pics:
        if not t0 <= t <= t1: continue
        im = Image.open(p).convert("RGB")
        if not is_select(im): continue
        w, h = im.size
        ph = np.asarray(im.crop((int(0.70 * w), int(0.17 * h), int(0.89 * w), int(0.40 * h))).resize((8, 8)), dtype=np.float32)
        cand.append((t, p, im, ph))
    groups = []
    for t, p, im, ph in cand:
        for g in groups:
            if np.abs(g[0] - ph).mean() < 12: g[1].append((t, p, im)); break
        else: groups.append([ph, [(t, p, im)]])
    if not groups: return {}, []
    mine = max(groups, key=lambda g: len(g[1]))[1]
    out, sel = {}, []
    for t, p, im in mine:
        sel.append(p)
        c = type_shown(im)
        if c is not None: out.setdefault(c, []).append(p)
    return out, sel

from handlers import RIG
REF_PLATE = os.path.join(RIG, "cap", "nm_mosh_plate.png")   # (No Mercy's select without a model: rig/nm_sel.py)
def our_plate(paths, cache):
    """our select screen without him: the median of those of the run's select pictures that show NO model in his place --
    found as the ones closest there to No Mercy's own select without a model (the rig's plate: the same backdrop, at the
    rig's resolution, so it only picks them out; the plate is made of our own pictures). (A median of all of them keeps a
    ghost of him: he stands there most of the time -- seen in runs 160545 / 161514.)"""
    if os.path.exists(cache): return Image.open(cache)
    ref = np.asarray(boxed(Image.open(REF_PLATE).convert("RGB"), NM_BOX).filter(ImageFilter.GaussianBlur(3)), dtype=np.float32)
    sc = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        a = np.asarray(boxed(im, NM_BOX).filter(ImageFilter.GaussianBlur(3)), dtype=np.float32)
        sc.append((float(np.abs(a - ref).max(axis=2).mean()), p))
    sc.sort()
    pick = [p for _, p in sc[:max(3, len(sc) // 6)]]
    arr = np.stack([np.asarray(Image.open(p).convert("RGB")) for p in pick])
    pl = Image.fromarray(np.median(arr, axis=0).astype(np.uint8))
    pl.save(cache)
    return pl

def best_of(paths, plate, box, n=1):
    """his clearest pictures of a costume: the largest figure (standing, facing out -- not turned side-on, not loading)"""
    scored = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        rgb, m = figure(im, plate, box)
        scored.append((int(m.sum()), p))
    scored.sort(reverse=True)
    return [p for _, p in scored[:n]]

# ---- the side by side --------------------------------------------------------------------------------------------------
def sheet(src_img, src_plate, src_box, our_img, our_plate_, rows, fs, fo, title, out_png, view="full"):
    a = boxed(src_img, src_box); b = boxed(our_img, NM_BOX)
    S = Image.new("RGB", (W * 2 + 230, H + 24), (24, 24, 24)); S.paste(a, (0, 24)); S.paste(b, (W, 24))
    d = ImageDraw.Draw(S)
    d.text((4, 4), title[:70], fill=(255, 255, 255))
    for f, x0 in ((fs, 0), (fo, W)):
        if not f: continue
        bx = f["_bbox"]; u = f["_u"]
        for name, ba, bb in BANDS:
            y = 24 + bx[1] + int(ba * u)
            if y > 24 + H: continue
            sc = rows.get(name)
            col = (90, 90, 90) if sc is None else ((230, 40, 40) if sc > THRESH else (60, 200, 60))
            d.line([(x0 + 2, y), (x0 + W - 2, y)], fill=col)
    y = 30
    for name, _, _ in BANDS:
        sc = rows.get(name)
        d.text((2 * W + 8, y), "%-6s %s" % (name, "-" if sc is None else "%.2f %s" % (sc, "MISMATCH" if sc > THRESH else "ok")),
               fill=(255, 90, 90) if sc is not None and sc > THRESH else (220, 220, 220))
        y += 18
    d.text((2 * W + 8, y + 10), "left: the source game", fill=(180, 180, 180))
    d.text((2 * W + 8, y + 28), "right: ours", fill=(180, 180, 180))
    S.save(out_png)

THRESH_PX = 0.30                   # (in place, the same engine: calibrated on the known-good -- work/visual_cal.txt)
def pose_sheet(a, b, box, px, title, out_png):
    A = boxed(a, box); B = boxed(b, NM_BOX)
    S = Image.new("RGB", (W * 2 + 230, H + 24), (24, 24, 24)); S.paste(A, (0, 24)); S.paste(B, (W, 24))
    d = ImageDraw.Draw(S); d.text((4, 4), title[:70], fill=(255, 255, 255))
    y = 30
    for name, _, _ in PX_BANDS:
        v = px.get(name)
        d.text((2 * W + 8, y), "%-6s %s" % (name, "-" if v is None else "%.2f %s" % (v, "MISMATCH" if v > THRESH_PX else "ok")),
               fill=(255, 90, 90) if v is not None and v > THRESH_PX else (220, 220, 220)); y += 18
    S.save(out_png)

# ---- the Goldust reference (No Mercy Plus has no Goldust of its own) -----------------------------------------------------
def reference_apps(env=None):
    """Goldust's reference looks in No Mercy Plus: what the handler chose from the MOD itself (its Test, its Mall's packs,
    hair from its own lists) kept, and every value the handler took from STOCK knowledge ("none" for the top, the pads ...)
    re-derived through the remap as the mod's own number for that meaning -- so the reference shows what the handler MEANT,
    whatever number it used. env: the handler's settings to make the reference for (MODCHECK_OLD_UPPER, MODCHECK_OLD_HAIR)."""
    env = env or {}
    sys.path.insert(0, HERE)
    from handlers.customs import nmplus as NP
    nm = NP.NMPlus(); R = nm.remap
    base = nm.appearance(nm.costume_struct(nm.find("TEST"), 0)["appearance"])
    hair, front = NP.HAIR, NP.FRONT_HAIR
    if env.get("MODCHECK_OLD_HAIR"): hair, front = base[NP.APP_FIELDS.index("hair")], base[NP.APP_FIELDS.index("front_hair")]
    upper = int(env.get("MODCHECK_OLD_UPPER", "0"), 0)
    claims = dict(ring_c1=0, ring_c2=0, upper=upper, upper_c1=0, upper_c2=0, entr=0, entr_c1=0, entr_c2=0, weapon=0, gloves=0,
                  gloves_c=0, tattoo=0, wrist=0, wrist_c=0, lelbow=0, lelbow_c=0, relbow=0, relbow_c=0, lknee=0, lknee_c=0,
                  rknee=0, rknee_c=0, boots_c1=0, boots_c2=0, facial_hair=0, hat=0, hat_c=0)
    meant = {k: R.mod_for_stock("parts." + k, v, "the reference: what stock %d means" % v) for k, v in claims.items()}
    out = []
    for ring, mask in NP.COSTUMES:
        a = NP.set_app(base, hair=hair, front_hair=front, ring=ring, mask=mask, boots=NP.BOOTS)
        out.append(NP.set_app(a, **meant))
    return out

# ---- the checks -------------------------------------------------------------------------------------------------------
def check_nm(name, run, wid, rig_prefix, costumes, out, src_box=NM_BOX):
    """a man from No Mercy's own family: the source's select pictures (rig_prefix_c<k>.png, rig_prefix_plate.png) against ours"""
    os.makedirs(out, exist_ok=True)
    shots, sel = our_shots(run, wid)
    L = ["VISUAL CHECK (modcheck, 2026-11-13): %s" % name, "ours: %s (0x%04X): %d select pictures of him, costumes seen %s"
         % (run, wid, len(sel), sorted(shots)), "source: %s_c*.png" % rig_prefix, "threshold %.2f per band" % THRESH, ""]
    if not sel: L.append("RESULT: NO PICTURES OF HIM IN OUR RUN"); write(out, name, L); return False
    pl = our_plate(sel, os.path.join(out, "%s_our_plate.png" % name))
    splate = Image.open(rig_prefix + "_plate.png").convert("RGB")
    ok = True; worst = 0
    for c in range(costumes):
        sp = rig_prefix + "_c%d.png" % c
        if c not in shots or not os.path.exists(sp):
            L.append("costume %d: MISSING (%s)" % (c + 1, "no picture of ours" if c not in shots else "no source picture")); ok = False; continue
        # (the source's poses as he stands and turns -- rig_prefix_c<k>_p<i>.png, else the one picture -- and his 8 clearest of
        #  ours; each of ours against the source pose most like it in silhouette; the closest pair in colour is kept)
        srcs = []
        for q in (sorted(glob.glob(rig_prefix + "_c%d_p*.png" % c)) or [sp]):
            qi = Image.open(q).convert("RGB"); fq = features(*figure(qi, splate, src_box))
            if fq is not None: srcs.append((fq, qi))
        si = Image.open(sp).convert("RGB")
        fs = features(*figure(si, splate, src_box))
        best = None
        for p in best_of(shots[c], pl, NM_BOX, n=8):
            oi = Image.open(p).convert("RGB")
            rgb_o, m_o = figure(oi, pl, NM_BOX)
            fo = features(rgb_o, m_o)
            if fo is None or not srcs: continue
            fq, qi = max(srcs, key=lambda s_: pose_alike(s_[0], fo))
            f = fit(fq, rgb_o, m_o)
            if f is None: continue
            if best is None or f[0] < best[0]: best = (f[0], p, f[2], f[1], oi, fq, qi)
        if best is not None: si, fs = best[6], best[5]; best = best[:5]
        if best is None: L.append("costume %d: NO FIGURE FOUND" % (c + 1)); ok = False; continue
        sc, p, rows, fo, oi = best
        bad = [k for k, v in rows.items() if v is not None and v > THRESH]
        worst = max(worst, sc)
        L.append("costume %d: %s  ours %s  %s" % (c + 1, "MISMATCH in " + ", ".join(bad) if bad else "PASS", os.path.basename(p),
                 "  ".join("%s %.2f" % (k, v) for k, v in rows.items() if v is not None)))
        # (a place-by-place comparison in the same pose was tried and left out: our select turns him as he stands, the best
        #  pose pair is too loose -- the known-good scored 0.6 and more; aligned()/px_rows() stay for the intent check's kind)
        if bad: ok = False
        sheet(si, splate, src_box, oi, pl, rows, fs, fo, "%s costume %d: source | ours" % (name, c + 1),
              os.path.join(out, "%s_c%d.png" % (name, c)))
    L += ["", "worst band %.2f" % worst, "RESULT: %s" % ("PASS" if ok else "MISMATCH (see the side-by-side pictures)")]
    write(out, name, L)
    return ok

THRESH_X = 1.00                    # (another engine: other lighting, pose, framing and -- the Revenge ports -- No Mercy's skin: work/visual_cal_rv.txt. A smoke test there, not a fine one)
def check_cross(name, run, wid, src, out, view, costumes, box_src=(0.0, 0.0, 1.0, 1.0), thresh=None, bands=None):
    """a man from ANOTHER engine (Revenge, Badd Blood): src = [(picture, its plate)] per costume; only the bands the source
    picture shows (view: the share of his height it shows) are compared; bands: only these (another source of the same man
    in another outfit -- Badd Blood's Goldust for the hair of No Mercy Plus's -- is compared where it should agree)"""
    thresh = thresh or THRESH_X
    os.makedirs(out, exist_ok=True)
    shots, sel = our_shots(run, wid)
    L = ["VISUAL CHECK, another engine (modcheck, 2026-11-13): %s" % name, "ours: %s (0x%04X): %d select pictures of him, "
         "costumes seen %s" % (run, wid, len(sel), sorted(shots)), "source: %s" % (src[0][0] if src else "-"),
         "the source shows %s of his height; threshold %.2f per band" % (view, thresh), ""]
    if not sel: L.append("RESULT: NO PICTURES OF HIM IN OUR RUN"); write(out, name, L); return False
    pl = our_plate(sel, os.path.join(out, "%s_our_plate.png" % name))
    ok = True; worst = 0
    for c in range(costumes):
        if c not in shots or c >= len(src) or not os.path.exists(src[c][0]):
            L.append("costume %d: MISSING" % (c + 1)); ok = False; continue
        si = Image.open(src[c][0]).convert("RGB"); sp = Image.open(src[c][1]).convert("RGB")
        fs = features(*figure(si, sp, box_src, largest=True), view=view)
        best = None
        cands = []
        for p in best_of(shots[c], pl, NM_BOX, n=5):
            oi = Image.open(p).convert("RGB")
            fo = features(*figure(oi, pl, NM_BOX))
            if fs is None or fo is None: continue
            cands.append((pose_alike(fs, fo, view), p, fo, oi))
        for iou, p, fo, oi in sorted(cands, key=lambda x: -x[0])[:8]:     # (every pose of his: front, side, back)
            f = fit(fs, *figure(oi, pl, NM_BOX), scales=6, shifts=(-0.4, 0.0, 0.4))   # (coarser: another engine's picture is no finer a match)
            if f is None: continue
            if best is None or f[0] < best[0]: best = (f[0], p, f[2], f[1], oi)
        if best is None: L.append("costume %d: NO FIGURE FOUND" % (c + 1)); ok = False; continue
        sc, p, rows, fo, oi = best
        if bands: rows = {k: (v if k in bands else None) for k, v in rows.items()}
        bad = [k for k, v in rows.items() if v is not None and v > thresh]
        worst = max(worst, sc)
        L.append("costume %d: %s  ours %s  %s" % (c + 1, "MISMATCH in " + ", ".join(bad) if bad else "PASS", os.path.basename(p),
                 "  ".join("%s %.2f" % (k, v) for k, v in rows.items() if v is not None)))
        if bad: ok = False
        sheet(si, sp, box_src, oi, pl, rows, fs, fo, "%s costume %d: source | ours" % (name, c + 1),
              os.path.join(out, "%s_c%d.png" % (name, c)), view=view)
    L += ["", "worst band %.2f" % worst, "RESULT: %s" % ("PASS" if ok else "MISMATCH (see the side-by-side pictures)")]
    write(out, name, L)
    return ok

THRESH_INTENT = 0.08
def check_intent(name, handler_prefix, intent_prefix, out, costumes=4):
    """the handler's looks against what it MEANT, both in the SOURCE game (No Mercy Plus in the rig, from the same machine
    state, so frame for frame the same pose): rig pictures <handler_prefix>_c<k>_p<i>.png (the looks the handler built his
    model from) and <intent_prefix>_c<k>_p<i>.png (the reference: its stock claims re-read through the remap). Per band
    (head units), the share of his pixels whose colour is clearly another (Lab 30 apart), the median over the poses."""
    os.makedirs(out, exist_ok=True)
    L = ["INTENT CHECK (modcheck, 2026-11-13): %s" % name, "the handler's looks: %s_c*_p*.png" % handler_prefix,
         "what it meant:      %s_c*_p*.png" % intent_prefix, "threshold %.2f per band" % THRESH_INTENT, ""]
    ok = True
    pa, pb = Image.open(handler_prefix + "_plate.png").convert("RGB"), Image.open(intent_prefix + "_plate.png").convert("RGB")
    for c in range(costumes):
        per = {}
        shown = None
        for qa in sorted(glob.glob(handler_prefix + "_c%d_p*.png" % c)):
            qb = intent_prefix + qa[len(handler_prefix):]
            if not os.path.exists(qb): continue
            ia, ib = Image.open(qa).convert("RGB"), Image.open(qb).convert("RGB")
            ra, ma = figure(ia, pa, NM_BOX); rb, mb = figure(ib, pb, NM_BOX)
            fb = features(rb, mb)
            if fb is None: continue
            d = np.linalg.norm(lab(ra) - lab(rb), axis=2)
            x0, y0, x1, y1 = fb["_bbox"]; u = fb["_u"]
            for bn, a, b in BANDS:
                r0, r1 = int(y0 + a * u), int(y0 + b * u)
                un = (ma | mb)[r0:r1]
                if un.sum() < 30: continue
                per.setdefault(bn, []).append(float((d[r0:r1][un] > 30).mean()))
            if shown is None or qa.endswith("_p08.png"): shown = (ia, ib)
        rows = {k: float(np.median(v)) for k, v in per.items()}
        bad = [k for k, v in rows.items() if v > THRESH_INTENT]
        if bad: ok = False
        L.append("costume %d: %s  %s" % (c + 1, "MISMATCH in " + ", ".join(bad) if bad else "PASS",
                 "  ".join("%s %.2f" % (k, v) for k, v in rows.items())))
        if shown:
            A = boxed(shown[1], NM_BOX); B = boxed(shown[0], NM_BOX)
            S = Image.new("RGB", (W * 2 + 230, H + 24), (24, 24, 24)); S.paste(A, (0, 24)); S.paste(B, (W, 24))
            dr = ImageDraw.Draw(S); dr.text((4, 4), "%s costume %d: meant | the handler's looks (No Mercy Plus)" % (name, c + 1), fill=(255, 255, 255))
            y = 30
            for bn, _, _ in BANDS:
                v = rows.get(bn)
                dr.text((2 * W + 8, y), "%-6s %s" % (bn, "-" if v is None else "%.2f %s" % (v, "MISMATCH" if v > THRESH_INTENT else "ok")),
                        fill=(255, 90, 90) if v is not None and v > THRESH_INTENT else (220, 220, 220)); y += 18
            S.save(os.path.join(out, "%s_c%d.png" % (name, c)))
    L += ["", "RESULT: %s" % ("PASS" if ok else "MISMATCH: the looks aren't what the handler meant (see the pictures)")]
    write(out, name, L)
    return ok

def write(out, name, L):
    open(os.path.join(out, name + ".txt"), "w", encoding="utf-8").write("\n".join(L) + "\n")
    print("\n".join(L))

def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    n = sp.add_parser("nm"); n.add_argument("--name", required=True); n.add_argument("--run", required=True)
    n.add_argument("--id", required=True); n.add_argument("--rig", required=True); n.add_argument("--costumes", type=int, default=4)
    n.add_argument("--out", required=True)
    x = sp.add_parser("cross"); x.add_argument("--name", required=True); x.add_argument("--run", required=True)
    x.add_argument("--id", required=True); x.add_argument("--src", action="append", required=True,
                   help="<picture>,<its plate> per costume, in order")
    x.add_argument("--view", default="0.62"); x.add_argument("--box", default="0,0,1,1"); x.add_argument("--out", required=True)
    x.add_argument("--thresh", type=float)
    a = ap.parse_args()
    if a.cmd == "cross":
        src = [tuple(s.split(",")) for s in a.src]
        sys.exit(0 if check_cross(a.name, a.run, int(a.id, 0), src, a.out, a.view, len(src),
                                  tuple(float(v) for v in a.box.split(",")), a.thresh) else 1)
    if a.cmd == "nm": sys.exit(0 if check_nm(a.name, a.run, int(a.id, 0), a.rig, a.costumes, a.out) else 1)

if __name__ == "__main__":
    main()
