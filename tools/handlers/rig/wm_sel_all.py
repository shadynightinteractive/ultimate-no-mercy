#!/usr/bin/env python3
# wm_sel_all.py (wm2kall, 2026-11-13): wm_sel.py's renders of every WrestleMania 2000 Superstar, all four costumes -- the
# visual check's source pictures -- and his photo picture (photo_<k>.png: his first costume's clearest pose, the figure
# on black, and photo_<k>.json: the box aki/photo.py crops, his head and shoulders). (2026-11-14, revfix: the first pose)
#   python wm_sel_all.py [k,k,...]
import json, os, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wm, wm_sel
from handlers import RIG
CAP = os.path.join(RIG, "cap")
REAL = list(range(1, 51)) + list(range(67, 75))
# (where WrestleMania 2000's select draws the model under the cursor, as fractions of the picture: the right side, his
#  head down to the bottom of the screen -- its name box over his belly is part of the plate too)
BOX = (0.56, 0.20, 1.00, 1.00)

def mask(img, plate, thr=40):
    a = np.asarray(img, dtype=np.int16); b = np.asarray(plate, dtype=np.int16)
    m = np.abs(a - b).max(axis=2) > thr
    h, w = m.shape
    out = np.zeros_like(m)
    out[int(BOX[1] * h):int(BOX[3] * h), int(BOX[0] * w):int(BOX[2] * w)] = m[int(BOX[1] * h):int(BOX[3] * h), int(BOX[0] * w):int(BOX[2] * w)]
    return out

def best_pose(prefix):
    plate = Image.open(prefix + "_plate.png").convert("RGB")
    best = None
    for i in range(len(wm_sel.POSES)):
        p = prefix + "_p%02d.png" % i
        if not os.path.exists(p): continue
        im = Image.open(p).convert("RGB")
        n = int(mask(im, plate).sum())
        if best is None or n > best[0]: best = (n, p, im)
    return best, plate

def photo_picture(k):
    """his first costume's clearest pose with everything but him black, and a square box over his head and shoulders in
    aki/photo.py's 320 x 240 picture"""
    # (2026-11-14, revfix: his first pose, his stance as the cursor lands on him -- the pose where the most of him showed was
    #  often a taunt filling the screen: an arm, a shoulder, Val Venis's chest, where the stock photos are his face and shoulders)
    prefix = os.path.join(CAP, "sel_k%d_c0" % k)
    p = prefix + "_p00.png"
    im, plate = Image.open(p).convert("RGB"), Image.open(prefix + "_plate.png").convert("RGB")
    m = mask(im, plate)
    from PIL import ImageFilter
    mi = Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(5))
    m = np.asarray(mi) > 127
    # (his dark clothes against the dark of the screen leave holes: closed and filled, so the photo keeps his whole shape)
    from scipy import ndimage
    m = ndimage.binary_fill_holes(ndimage.binary_closing(m, structure=np.ones((7, 7)), iterations=2))
    a = np.asarray(im).copy(); a[~m] = 0
    out = os.path.join(CAP, "photo_%d.png" % k)
    Image.fromarray(a).save(out)
    h, w = m.shape
    ys, xs = np.nonzero(m)
    top = ys.min()
    # (the head: the figure's rows from its top; its middle across the top fifth of him)
    head = m[top:top + max(4, (h - top) // 5)]
    hx = np.nonzero(head.any(axis=0))[0]
    cx = (hx.min() + hx.max()) / 2.0
    # (head and shoulders: the top 45 % of what the select shows of him, and never down into its name box: wm_sel.NAME_BOX)
    side = min((h - top) * 0.45, wm_sel.NAME_BOX[1] * h - top)
    sx, sy = 320.0 / w, 240.0 / h                # (photo_from_shot crops the picture made 320 x 240)
    x0 = max(0.0, cx * sx - side * sy / 2); y0 = max(0.0, top * sy - 2)
    box = [int(round(x0)), int(round(y0)), int(round(x0 + side * sy)), int(round(y0 + side * sy))]
    json.dump({"box": box, "from": os.path.basename(p)}, open(out[:-4] + ".json", "w"))
    return out

def main():
    only = [int(x) for x in sys.argv[1].split(",")] if len(sys.argv) > 1 else REAL
    M = wm.session(wm_sel.START)
    for k in only:
        for c in range(4):
            if not os.path.exists(os.path.join(CAP, "sel_k%d_c%d_p%02d.png" % (k, c, len(wm_sel.POSES) - 1))):
                wm_sel.render(M, "sel", k, c)
        print(k, "rendered;", photo_picture(k), flush=True)

if __name__ == "__main__":
    main()
