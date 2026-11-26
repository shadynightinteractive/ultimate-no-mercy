#!/usr/bin/env python3
"""The championship list's "Custom Story" and "Versus" rows as picture labels in the game's own lettering: their
letters cut from the ROM's menu labels (C m o from "Championship", u s t e from "Guest Referee", S from "Survival", r
from "Records", y from "Royal Rumble", V from "Pay Per View") and set again on a 128 x 32 CI4 with "World
Heavyweight"'s palette (index 0 clear, 1 the dark outline, 2..15 the grey ramp), the outline made again round the
whole word as the game's labels have it. Writes src/label_tex.h (files.c serves them as FILE_LABEL_CUSTOM /
FILE_LABEL_VERSUS, logic.c storylist_file).

    python mod/gen/labeltex.py [out.h]
"""
import os, sys, struct
import numpy as np
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp

FILES = nmp("/tmp/devbox-0/nmfiles/f")

def load(fid):
    b = open(os.path.join(FILES, "%04x.bin" % fid), "rb").read()
    assert b[:4] == b"TEX\0"
    w, h = struct.unpack(">HH", b[4:8]); npal = struct.unpack(">H", b[10:12])[0]
    pal = [struct.unpack(">H", b[16 + 2 * i:18 + 2 * i])[0] for i in range(npal)]
    px = np.frombuffer(b[16 + 2 * npal:16 + 2 * npal + w * h // 2], np.uint8)
    a = np.empty(w * h, np.uint8); a[0::2] = px >> 4; a[1::2] = px & 15
    return a.reshape(h, w), pal, b[:16 + 2 * npal]

def lum(c):   # RGBA5551 -> brightness
    return ((c >> 11) & 31) + ((c >> 6) & 31) + ((c >> 1) & 31)

# each letter: (file, first column, end column) -- the body's columns in the label (its outline made again)
GLYPHS = {"C": (0x448E, 5, 15), "m": (0x448E, 36, 50), "o": (0x448E, 68, 76), "u": (0x448B, 17, 27),
          "s": (0x448B, 37, 46), "t": (0x448B, 46, 53), "S": (0x448D, 14, 23), "r": (0x4491, 55, 62),
          "y": (0x4488, 28, 38), "V": (0x4489, 79, 90), "e": (0x448B, 28, 37)}
SPACE = 5

def make(text, pal_file=0x4617, width=128):
    base, pal, head = load(pal_file)
    target = [lum(c) for c in pal]
    cols = []
    for ch in text:
        if ch == " ": cols.append(np.zeros((32, SPACE), np.uint8)); continue
        fid, x0, x1 = GLYPHS[ch]
        a, p, _ = load(fid)
        g = a[:, x0:x1].copy()
        g[g < 2] = 0                                                  # (the body only)
        # its palette's brightness -> the target palette's nearest index
        m = {i: min(range(2, 16), key=lambda j: abs(target[j] - lum(p[i]))) for i in range(2, 16)}
        g = np.vectorize(lambda v: m.get(int(v), 0))(g).astype(np.uint8)
        cols.append(g); cols.append(np.zeros((32, 1), np.uint8))       # (a column between letters: the outline's)
    word = np.hstack(cols[:-1])
    assert word.shape[1] <= width - 4, word.shape
    out = np.zeros((32, width), np.uint8)
    x = (width - word.shape[1]) // 2
    out[:, x:x + word.shape[1]] = word
    body = out > 0
    ring = np.zeros_like(body)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            ring |= np.roll(np.roll(body, dy, 0), dx, 1)
    out[ring & ~body] = 1
    return out, head

# (ljround8, 2026-11-23, the user: the Lumberjack Match row's "text is too small", "it looks odd" -- it was the game's text
#  maker's condensed lettering. Now cut from the ROM's labels as the rows round it are: L a r "Match" from "Ladder Match",
#  u m b e from "Royal Rumble", c k from "Smackdown Mall"; the ROM has no j, so it is the i of "Exhibition" with its stem
#  carried down and turned left as the labels' y and g tails are. Sixteen letters are wider than a 128 label: the word is
#  squeezed to the label's width as the game's own long rows are drawn narrower ("Triple Threat Match"), each column the
#  average of the columns it takes in, by brightness, to 114 columns (6-120, as the game's labels sit). It is a table made
#  from the ROM: the package ships it zeroed and the first start makes it again (exe arcade_firstrun_labels.cpp
#  make_lumberjack -- the same steps; tools/firstrun/mkplan.py's labels port). Until that port had it, the installed game
#  drew the row with no letters (r9-r22).)
PIECES = {"L": (0x448C, 7, 15), "a": (0x448C, 15, 25), "r": (0x448C, 56, 64), "Match": (0x448C, 70, 121),
          "u": (0x4488, 68, 78), "m": (0x4488, 78, 94), "b": (0x4488, 94, 104), "e": (0x4488, 110, 120),
          "c": (0x448F, 34, 43), "k": (0x448F, 43, 52)}
I_PIECE = (0x4487, 34, 39)          # (the i of "Exhibition": its dot rows 7-9, stem 10-22)

def piece(fid, x0, x1, target):
    a, p, _ = load(fid)
    g = a[:, x0:x1].copy()
    g[g < 2] = 0
    m = {i: min(range(2, 16), key=lambda j: abs(target[j] - lum(p[i]))) for i in range(2, 16)}
    return np.vectorize(lambda v: m.get(int(v), 0))(g).astype(np.uint8)

def j_piece(target):
    i = piece(*I_PIECE, target)
    stem_rows = [y for y in range(i.shape[0]) if i[y].any()]
    base = max(stem_rows)                                   # (the i's foot: the baseline)
    g = np.zeros((32, i.shape[1] + 3), np.uint8)
    g[:, 3:] = i
    row = i[base - 2].copy()                                # (a row of the stem's middle, its shading)
    for y in range(base - 1, base + 3): g[y, 3:] = row      # (the stem carried down past the baseline)
    tail = row[row > 0]
    v = int(tail.max()) if len(tail) else 15
    for y in (base + 2, base + 3):                          # (the tail turned left under the line)
        g[y, 0:3] = v
        g[y, 3:3 + 2] = v
    g[base + 3, 3:] = 0; g[base + 3, 3:5] = v
    return g

# (ljmenu10, 2026-11-24, the user: the Lumberjack MATCH page's settings rows' "text ... needs to match the text of the other
#  menu items exactly" -- they were the text maker's. Now "Lumberjacks: 12" and "Picked: Random" / "Picked: Select" are cut
#  from the ROM's labels as "Lumberjack Match" is: P from "Pay Per View", i from "Exhibition", d from "Ladder", s from
#  "Guest Referee", the colon and the space after it from "Ladder: Single", and "Random" / "Select" the ROM's own labels'
#  words whole. The labels' lettering has no figures (only 1 to 4, smaller, in "CPU 1P 2P 3P 4P"), so the count's figures
#  are the game's own menu figures -- the NUMBER page's (file 0x4588, ten of 32 x 48, the Royal Rumble's "Number of
#  Superstars") -- their fill, without the green border, set at the labels' capital height (16 rows, 7-22, as L of
#  "Ladder") by its cover of each new pixel, each pixel's brightness its cover of the label palette's white, the outline
#  made round the whole word as the labels' is. Their palette is Single Match's (file 0x44A1), the page's first row.)
PIECES.update({"s": (0x448B, 37, 46), "P": (0x4489, 9, 19), "i": (0x4487, 34, 39), "d": (0x448C, 25, 35),
               ":": (0x44A6, 62, 69), "Random": (0x44B1, 15, 82), "Select": (0x44B2, 21, 73)})
FIGURES = 0x4588                    # (the NUMBER page's figures: 0..9 down the file, 48 rows each)
FIG_CAP, FIG_TOP = 16, 7            # (the labels' capital height and its first row)
LJ_COUNT_MIN, LJ_COUNT_MAX = 2, 30  # (the page's LUMBERJACKS range: logic.c lj_mc, NM_SLOTS_CAP - 2)

def figure(d, target):
    a, _, _ = load(FIGURES)
    m = (a[d * 48:(d + 1) * 48] >= 3).astype(np.float64)          # (the fill: index 2 is its border, 0 / 1 clear)
    rows = np.where(m.any(axis=1))[0]; cols = np.where(m.any(axis=0))[0]
    m = m[rows.min():rows.max() + 1, cols.min():cols.max() + 1]
    h0, w0 = m.shape
    h1 = FIG_CAP; w1 = max(1, (2 * w0 * h1 + h0) // (2 * h0))
    g = np.zeros((32, w1), np.uint8)
    for y in range(h1):
        for x in range(w1):
            ya, yb = y * h0 / h1, (y + 1) * h0 / h1
            xa, xb = x * w0 / w1, (x + 1) * w0 / w1
            acc = 0.0; area = 0.0
            for sy in range(int(ya), min(h0, int(np.ceil(yb)))):
                wy = min(yb, sy + 1) - max(ya, sy)
                for sx in range(int(xa), min(w0, int(np.ceil(xb)))):
                    wx = min(xb, sx + 1) - max(xa, sx)
                    acc += m[sy, sx] * wy * wx; area += wy * wx
            c = acc / area
            if c >= 0.25:
                lv = target[15] * min(1.0, c)
                g[FIG_TOP + y, x] = min(range(2, 16), key=lambda j: abs(target[j] - lv))
    return g

def make_squeezed(parts, width=128, pal_file=0x4617, room=14, up=1):
    base, pal, head = load(pal_file)
    target = [lum(c) for c in pal]
    cols = []
    for name in parts:
        if name == " ": cols.append(np.zeros((32, SPACE), np.uint8)); continue
        g = j_piece(target) if name == "j" else figure(int(name), target) if name.isdigit() else piece(*PIECES[name], target)
        if name == "j" and len(cols) >= 2:                  # (its tail tucked under the letter before, as the labels' y does:
            prev = cols[-2]; cols.pop()                     #  no spacer, its three tail columns over the last three)
            joined = np.hstack([prev, np.zeros((32, 1), np.uint8)])
            joined[:, -3:] = np.maximum(joined[:, -3:], g[:, 0:3])
            cols[-1] = joined; g = g[:, 3:]
        cols.append(g); cols.append(np.zeros((32, 1), np.uint8))
    word = np.hstack(cols[:-1])
    w0, w1 = word.shape[1], min(word.shape[1], width - room)
    if w1 < w0:                                             # (squeezed: each new column the average of what it covers)
        L = np.vectorize(lambda v: target[v] if v >= 2 else 0)(word).astype(np.float64)
        B = (word >= 2).astype(np.float64)
        out_l = np.zeros((32, w1)); out_b = np.zeros((32, w1))
        for x in range(w1):
            a0, a1 = x * w0 / w1, (x + 1) * w0 / w1
            for sx in range(int(a0), min(w0, int(np.ceil(a1)))):
                wgt = min(a1, sx + 1) - max(a0, sx)
                if wgt > 0: out_l[:, x] += L[:, sx] * wgt; out_b[:, x] += B[:, sx] * wgt
        span = w0 / w1
        sq = np.zeros((32, w1), np.uint8)
        for y in range(32):
            for x in range(w1):
                if out_b[y, x] / span >= 0.5:
                    lv = out_l[y, x] / out_b[y, x]
                    sq[y, x] = min(range(2, 16), key=lambda j: abs(target[j] - lv))
        word = sq
    out = np.zeros((32, width), np.uint8)
    x = (width - word.shape[1]) // 2
    out[:, x:x + word.shape[1]] = word
    if up: out[:-up] = out[up:].copy(); out[-up:] = 0          # (the j's tail kept inside the rows the labels use)
    body = out > 0
    ring = np.zeros_like(body)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            ring |= np.roll(np.roll(body, dy, 0), dx, 1)
    out[ring & ~body] = 1
    return out, head

def tex_bytes(a, head):
    f = a.reshape(-1)
    return head + bytes(((f[0::2] << 4) | f[1::2]).tolist())

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "label_tex.h")
    s = ""
    for text, name in (("Custom Story", "label_custom_story"), ("Versus", "label_versus")):
        a, head = make(text)
        b = tex_bytes(a, head)
        s += "// generated by gen/labeltex.py: \"%s\" in the game's label lettering (a TEX file, CI4 128 x 32)\n" % text
        s += "static const unsigned char %s[%d] __attribute__((aligned(8))) = {\n" % (name, len(b))
        for i in range(0, len(b), 16): s += "  " + ",".join("0x%02x" % v for v in b[i:i + 16]) + ",\n"
        s += "};\n"
        for r in a[4:27]: print("".join(" .:-=+*#%@ABCDEF"[x] for x in r))
    a, head = make_squeezed(["L", "u", "m", "b", "e", "r", "j", "a", "c", "k", " ", "Match"])
    b = tex_bytes(a, head)
    s += "// generated by gen/labeltex.py: \"Lumberjack Match\" in the game's label lettering, squeezed to the label (a TEX file, CI4 128 x 32)\n"
    s += "static const unsigned char label_lumberjack[%d] __attribute__((aligned(8))) = {\n" % len(b)
    for i in range(0, len(b), 16): s += "  " + ",".join("0x%02x" % v for v in b[i:i + 16]) + ",\n"
    s += "};\n"
    for r in a[4:30]: print("".join(" .:-=+*#%@ABCDEF"[x] for x in r))
    # (ljmenu10: the Lumberjack MATCH page's settings rows -- label_lj_count: "Lumberjacks: 2" .. "Lumberjacks: 30", one
    #  2096-byte picture a count from 2; label_lj_pick: "Picked: Random", "Picked: Select". Tables made from the ROM, as
    #  label_lumberjack: shipped zeroed, made again by the first start, exe arcade_firstrun_labels.cpp port_labels)
    LJ_WORD = ["L", "u", "m", "b", "e", "r", "j", "a", "c", "k", "s", ":"]
    for name, rows in (("label_lj_count", [LJ_WORD + list(str(n)) for n in range(LJ_COUNT_MIN, LJ_COUNT_MAX + 1)]),
                       ("label_lj_pick", [["P", "i", "c", "k", "e", "d", ":", w] for w in ("Random", "Select")])):
        b = b""
        for parts in rows:
            a, head = make_squeezed(parts, pal_file=0x44A1, up=0)
            b += tex_bytes(a, head)
        s += "// generated by gen/labeltex.py: the Lumberjack MATCH page's %s pictures in the game's label lettering (TEX files, CI4 128 x 32, %d of 2096 bytes)\n" % (name, len(rows))
        s += "static const unsigned char %s[%d] __attribute__((aligned(8))) = {\n" % (name, len(b))
        for i in range(0, len(b), 16): s += "  " + ",".join("0x%02x" % v for v in b[i:i + 16]) + ",\n"
        s += "};\n"
        for r in a[4:30]: print("".join(" .:-=+*#%@ABCDEF"[x] for x in r))
    open(out, "w", newline="\n").write(s)
