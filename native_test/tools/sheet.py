# sheet.py <out.png> file file ...: stick figures of animation files (first, a third, two thirds, last key), to tell which is which by eye. Side view (z across, y up), root drift printed
import math, struct, sys
sys.path.insert(0, '../..'); sys.path.insert(0, '../../data/announce')
from nmpaths import nmp
import fk
from PIL import Image, ImageDraw
F = nmp('/tmp/devbox-0/nmfiles/f/%04x.bin')
LINES = [(1, 2), (2, 3), (3, 5), (3, 12), (12, 11), (11, 9), (3, 19), (19, 16), (16, 17), (1, 7), (7, 6), (6, 8), (1, 14), (14, 13), (13, 15)]
fs = [int(x, 16) for x in sys.argv[2:]]
W, H = 96, 150
sheet = Image.new('RGB', (W * 4 * 4 + 40, (H + 16) * ((len(fs) + 3) // 4)), (24, 24, 28))
dr = ImageDraw.Draw(sheet)
for i, f in enumerate(fs):
    x0 = (i % 4) * (W * 4 + 10); y0 = (i // 4) * (H + 16)
    try:
        kfs, n = fk.decode(open(F % f, 'rb').read())
    except Exception as e:
        dr.text((x0, y0), '%x: %s' % (f, e), fill=(255, 80, 80)); continue
    pts = []
    for k in (0, len(kfs) // 3, 2 * len(kfs) // 3, len(kfs) - 1):
        M, P = fk.pose(kfs[k]); pts.append(P)
    r0, r1 = kfs[0][1][1], kfs[-1][1][1]
    dr.text((x0 + 2, y0 + 1), '%x n=%d d=(%.0f,%.0f,%.0f)' % (f, n, r1[0] - r0[0], r1[1] - r0[1], r1[2] - r0[2]), fill=(255, 255, 255))
    for j, P in enumerate(pts):
        cx = x0 + W * j + W // 2; base = y0 + H + 6
        q = lambda p: (cx + (p[2] - P[1][2]) * 0.55 + (p[0] - P[1][0]) * 0.3, base - (p[1] + 90) * 0.65)
        for a, b in LINES: dr.line([q(P[a]), q(P[b])], fill=(120, 230, 255) if a not in (12, 11, 19, 16) else (255, 190, 90), width=2)
        hd = q(P[5]); dr.ellipse((hd[0] - 4, hd[1] - 9, hd[0] + 4, hd[1] - 1), outline=(255, 255, 255))
sheet.save(sys.argv[1])
