# revfix 2026-11-14: WrestleMania 2000 select photos from a chosen pose (rig\wm_sel_all.py took the pose where the most of
# him shows -- often a taunt filling the screen: an arm, a shoulder, Val Venis's chest -- the stock photos are his face and
# shoulders). Here: the first pose (p00, his stance as the cursor lands on him), unless POSE names another; the rest as
# wm_sel_all.photo_picture does it (the figure against the plate, holes filled, head and shoulders; aki/photo.py's crop).
#   python wm_photo.py <rig cap folder (read only)> <out folder> [k=pose ...]
import json, os, sys
import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from handlers import DEV     # (newhome 2026-11-17: the project found from here, as the other handlers)
from handlers.aki import photo as PH
BOX = (0.56, 0.20, 1.00, 1.00)          # (wm_sel_all.py: where the select draws him)
NAME_BOX_TOP = None
def mask(img, plate, thr=40):
    a = np.asarray(img, dtype=np.int16); b = np.asarray(plate, dtype=np.int16)
    m = np.abs(a - b).max(axis=2) > thr
    h, w = m.shape
    out = np.zeros_like(m)
    out[int(BOX[1] * h):int(BOX[3] * h), int(BOX[0] * w):int(BOX[2] * w)] = m[int(BOX[1] * h):int(BOX[3] * h), int(BOX[0] * w):int(BOX[2] * w)]
    return out
def photo(cap, k, pose, out, name_box_top):
    im = Image.open(os.path.join(cap, "sel_k%d_c0_p%02d.png" % (k, pose))).convert("RGB")
    plate = Image.open(os.path.join(cap, "sel_k%d_c0_plate.png" % k)).convert("RGB")
    m = mask(im, plate)
    mi = Image.fromarray((m * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(5))
    m = np.asarray(mi) > 127
    m = ndimage.binary_fill_holes(ndimage.binary_closing(m, structure=np.ones((7, 7)), iterations=2))
    a = np.asarray(im).copy(); a[~m] = 0
    png = os.path.join(out, "photo_%d.png" % k)
    Image.fromarray(a).save(png)
    h, w = m.shape
    ys, xs = np.nonzero(m)
    top = ys.min()
    head = m[top:top + max(4, (h - top) // 5)]
    hx = np.nonzero(head.any(axis=0))[0]
    cx = (hx.min() + hx.max()) / 2.0
    side = min((h - top) * 0.45, name_box_top * h - top)
    sx, sy = 320.0 / w, 240.0 / h
    x0 = max(0.0, cx * sx - side * sy / 2); y0 = max(0.0, top * sy - 2)
    box = [int(round(x0)), int(round(y0)), int(round(x0 + side * sy)), int(round(y0 + side * sy))]
    json.dump({"box": box, "from": "sel_k%d_c0_p%02d.png" % (k, pose)}, open(png[:-4] + ".json", "w"))
    open(os.path.join(out, "photo_%d.bin" % k), "wb").write(PH.photo_from_shot(png, box))
    return box
if __name__ == "__main__":
    cap, out = sys.argv[1], sys.argv[2]
    over = dict((int(a.split("=")[0]), int(a.split("=")[1])) for a in sys.argv[3:])
    sys.path.insert(0, os.path.join(DEV, "tools", "handlers", "rig"))
    nb = 0.72      # (wm_sel.NAME_BOX[1]: read below when the module is importable without the emulator)
    try:
        src = open(os.path.join(DEV, "tools", "handlers", "rig", "wm_sel.py")).read()
        import re
        nb = float(re.search(r"NAME_BOX\s*=\s*\(([^)]*)\)", src).group(1).split(",")[1])
    except Exception as ex: print("NAME_BOX not read (%s): %.2f" % (ex, nb))
    os.makedirs(out, exist_ok=True)
    REAL = list(range(1, 51)) + list(range(67, 75))
    for k in REAL:
        if os.path.exists(os.path.join(cap, "sel_k%d_c0_plate.png" % k)):
            print(k, over.get(k, 0), photo(cap, k, over.get(k, 0), out, nb))
