"""WCW/nWo Revenge's arenas in No Mercy (src/arena.c): Monday Nitro, Halloween Havoc, Bash at the Beach, Souled Out,
SuperBrawl, Starrcade (each its own arena).

Revenge's arena is the game's ring and stands (the same in its six) with, per arena, a stage set, the floor by the ring
and the aisle's pictures, and the ring's (data/arenaport/REV_ARENAS.md: its table at 0x80036C74.., read from a match's
RAM). Its meshes are No Mercy's format and its units No Mercy's (the ring mat +-280 at 90 in both; the stage at +z in
both), so the pieces come over as they are, in a base arena's stage set (data/arenaport/NM_ARENAS.md: 0x800507FC ..),
the game's own list of up to 15 pieces drawn in order, back to front, behind everything else:
  - the stage's own pieces in Revenge's order, those with one picture side by side made one mesh (the truss's two
    towers, ...: 15 at most); the aisle's six rails one;
  - Revenge's floor by the ring on the stage's side (its mesh 0x2F5, the arena's picture);
  - the base arena's floor round the ring (No Mercy's: its last piece, as in all ten).
Left out: Revenge's crowd beside the aisle (No Mercy's stands are all round) and its stage's empty root (0x2F4).
Called by gen/wmport.py build_all (the files go in its pack); writes src/arena_files.h."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp, ROOT
import struct

REV = nmp("/tmp/devbox-0/rev_files/%04x.bin")
REV_RAM = ROOT + "/data/arenaport/rev/cap/m%d_ram.bin"      # (a Revenge match's RAM in each arena)
OUT_H = nmp("/home/devbox/work/arcade/src/arena_files.h")
BASE = 7                    # the base arena (WrestleMania: its stage flat, as Revenge's)
BASE_FLOOR = (0x5D0, 0x8C1)  # its floor round the ring (mesh, palette)
SET_MAX = 64                # the stage set's parts: src/arena.c ARENA_SET_MAX (the game's array 0x8007B8A0 had 15;
                            # mod/NOTES.md "Arenas restructured")
ARENAS = [("MONDAY NITRO", 0), ("HALLOWEEN HAVOC", 4), ("BASH AT THE BEACH", 3),   # (name, Revenge's arena)
          ("RAW IS WAR WM 2000", "wm"),   # (WrestleMania 2000's RAW IS WAR: gen/arenas_wm.py -- its own rows everywhere)
          ("SOULED OUT", 1), ("SUPERBRAWL", 2), ("STARRCADE", 5)]   # (2026-11-01 evening, arenas-rev: Revenge's 3D stages, data/arenaport/rev3)
# WrestleMania 2000's other venues (gen/arenas_wm.py VENUES, "wm<v>": its venue v; data/wmarena/all/ALL.md) -- after the
# rows above (an arena's number is 10 + its place here: new arenas go at the end, so none moves)
WM_ARENAS = [("SUNDAY NIGHT HEAT", "wm1"), ("ROYAL RUMBLE WM 2000", "wm2"), ("KING OF THE RING WM 2000", "wm3"), ("SURVIVOR SERIES WM 2000", "wm4"), ("WRESTLEMANIA WM 2000", "wm5"), ("SUMMERSLAM WM 2000", "wm6")]
ARENAS += WM_ARENAS
STAGE_ROOT, AISLE_FLOOR = 0x2F4, 0x2F5
RAILS = set(range(0x319, 0x31F))        # the aisle's rails (one picture, 0x30B)
CROWD = set(range(0x31F, 0x348)) - RAILS  # Revenge's crowd beside the aisle

def s8(b): return b - 256 if b > 127 else b
def rfile(f): return open(REV % f, "rb").read()

def table(v):
    ram = open(REV_RAM % v, "rb").read()
    r32 = lambda a: struct.unpack_from(">I", ram, a & 0x7FFFFF)[0]
    r16 = lambda a: struct.unpack_from(">H", ram, a & 0x7FFFFF)[0]
    n = struct.unpack_from(">b", ram, (0x80036CAC + v) & 0x7FFFFF)[0]
    return dict(meshes=[r16(r32(0x80036CB4 + 4 * v) + 2 * i) for i in range(n)],
                pals=[r16(r32(0x80036CCC + 4 * v) + 2 * i) for i in range(n)],
                ring=[r16(r32(0x80036C74 + 4 * v) + 2 * i) for i in range(6)],
                aisle=[r16(r32(0x80036C94 + 4 * v) + 2 * i) for i in range(3)])

def points(d):
    sc = (d[0] & 0x7F) + 1; off = [s8(x) * 16 for x in d[4:7]]; nv = d[1] & 0x7F
    return [tuple((s8(d[8 + 8 * i + k]) + off[k]) * sc for k in range(3)) for i in range(nv)]

def merge(files):
    """meshes with one picture (the same flags and texture page) as one: their points as they were"""
    ds = [rfile(f) for f in files]
    h = ds[0]
    assert all(d[1] & 0x80 == h[1] & 0x80 and d[3] == h[3] and d[7] == h[7] for d in ds), [hex(f) for f in files]
    pts, verts, tris = [], [], []
    for d in ds:
        nv, nt = d[1] & 0x7F, d[2]
        base = len(verts)
        pts += points(d)
        verts += [d[8 + 8 * i + 3:8 + 8 * i + 8] for i in range(nv)]
        o = 8 + 8 * nv
        tris += [bytes(x + base for x in d[o + 3 * i:o + 3 * i + 3]) for i in range(nt)]
    assert len(verts) <= 127 and len(tris) <= 255
    for sc in range(1, 129):     # the smallest scale the points fit at (s8 + offset * 16)
        off = [int(round((min(p[k] for p in pts) + max(p[k] for p in pts)) / 2 / sc / 16)) for k in range(3)]
        q = [[p[k] / sc - off[k] * 16 for k in range(3)] for p in pts]
        if all(-128 <= o <= 127 for o in off) and all(-128 <= c <= 127 and c == int(c) for v in q for c in v): break
    else: raise ValueError("no scale fits %s" % [hex(f) for f in files])
    out = bytearray([(h[0] & 0x80) | (sc - 1), (h[1] & 0x80) | len(verts), len(tris), h[3]] + [o & 0xFF for o in off] + [h[7]])
    for v, rest in zip(q, verts): out += bytes(int(c) & 0xFF for c in v) + rest
    for t in tris: out += t
    if len(out) & 1: out += b"\0"
    assert points(out) == pts, files
    return bytes(out)

def pic_pair(F, p):
    """a piece's picture in F's pack: (texture, palette) files -- p Revenge's palette file (its texture p + 1), or
    ("pic", texture bytes, palette bytes) (gen/arenas_stage3d.py's)"""
    if isinstance(p, tuple): return F.pair(p[1], p[2])
    return F.pair(rfile(p + 1), rfile(p))

def stage(v):
    """[(mesh bytes, picture: Revenge's palette file or gen/arenas_stage3d.py's)] in drawing order; the table.
    Revenge's 3D stage where there is one (gen/arenas_stage3d.py), else its flat set"""
    import arenas_stage3d
    if v in arenas_stage3d.STAGE3D: return arenas_stage3d.ring_parts(v), table(v)
    t = table(v)
    groups = []                 # [[files], pal]
    for m, p in zip(t["meshes"], t["pals"]):
        if m == STAGE_ROOT or m in CROWD: continue
        key = "rails" if m in RAILS else (p, rfile(m)[3], rfile(m)[7])
        g = next((g for g in groups if g[0] == key), None)
        if g: g[1].append(m)
        else: groups.append([key, [m], p])
    # the rails nearest: last of the stage's own
    groups.sort(key=lambda g: g[0] == "rails")
    return [(merge(fs) if len(fs) > 1 else rfile(fs[0]), p) for key, fs, p in groups], t

# The ring's aprons: No Mercy's side is two meshes, each its own 128 x 32 picture across its half (565: x 0..280,
# 566: -280..0, ...: the venue's list right, left, right, left ...); Revenge's one 128 x 32 picture across the whole
# side (u 0..63 the left half, 64..127 the right) -- each half of it made a picture of its own, twice as wide.
def apron_half(t, right):
    w, h = t[0] + 1, t[1] + 1
    assert (w, h) == (128, 32) and struct.unpack_from(">H", t, 2)[0] == 16, "a CI4 128 x 32 apron"
    px = t[8:]; out = bytearray(t[:8])
    nib = lambda i: (px[i >> 1] >> (0 if i & 1 else 4)) & 15
    for y in range(h):
        row = [nib(y * w + (64 if right else 0) + x // 2) for x in range(w)]
        out += bytes(row[x] << 4 | row[x + 1] for x in range(0, w, 2))
    return bytes(out)
# The hanging banners (the light truss's last two pieces; 0x8005078C[v]: 80B 80B 80D, then the venue's twice): a
# 64 x 64 picture of the arena's (Nitro: the stage's "WCW Monday Nitro Live" sign; Havoc: its ring's logo).
BANNER = {0: 0x36B, 4: 0x3AA, 3: 0x3C4, 1: 0x35A, 5: 0x3D8,
          2: 0x385}           # (SuperBrawl has no ring logo: its "SuperBrawl VIII" sign, 64 x 32; the truss isn't drawn)

# ---- the Versus ARENA page's pictures (data/arenaport/ARENA_PAGE.md; arena.c) and the VS screen's backdrop ----------
# a row's label: 128 x 32 CI4 in the arena labels' palette (0x462B's: 0 clear, 1 the outline, 2..15 the grey ramp, so
# the game lights it orange under the cursor as its own): the name in a condensed bold like the labels' (Anton, below), a small TM
# after it, the outline round all of it. The page's photo: the arena in No Mercy (rendered by tools/romrender, below)
# in a white frame with its logo in a framed box at the lower right, as the game's are: 128 x 96,
# six 64 x 32 CI8 tiles (each its own 256-colour palette, the alpha bit clear round the logo), column by column. The
# VS / loading screens' backdrop (func_80117CCC, 0x80148DE0[13 + venue]): 320 x 240, forty 64 x 30 CI4 tiles row by
# row, one 16-colour palette at the head of the first -- forty files one after another.
NMF = nmp("/tmp/devbox-0/nmfiles/f/%04x.bin")
LABEL = {0: "MONDAY NITRO", 4: "HALLOWEEN HAVOC", 3: "BASH AT THE BEACH", 1: "SOULED OUT", 2: "SUPERBRAWL", 5: "STARRCADE"}
LOGO = {0: 0x36B, 4: 0x3AA, 3: 0x3C4, 1: 0x35A, 2: 0x38B, 5: 0x3D8}   # (SuperBrawl: its 128 x 32 banner, fitted)
# (merge084b, 2026-11-15: the labels' font is Anton -- SIL Open Font License, gen/fonts/OFL.txt; from github.com/google/fonts,
#  ofl/anton -- kept beside this file, so the generator reads nothing outside the mod's own folder. It was the system's
#  Impact font, read from the system's font folder: a path outside the game's folder, and a font not ours to ship.)
FONT = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "fonts", "Anton-Regular.ttf")
LABEL_CAP, LABEL_TOP = 20.5, 4.75     # (rows: the labels' capitals as they were with Impact)

def c5551(r, g, b, a=1): return (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | a

def label(text):
    from PIL import Image, ImageDraw, ImageFont
    import numpy as np
    head = open(NMF % 0x462B, "rb").read()[:48]            # (TEX header + the arena labels' palette)
    S = 8
    # (merge084b: the capitals as tall and as high as Impact's were -- LABEL_CAP rows, their top at row LABEL_TOP -- whatever
    #  the font's own proportions; the TM 8/26 of the name's size, 19/26 of it lower, as before)
    probe = ImageFont.truetype(FONT, 100 * S); cap = probe.getbbox("H")
    size = max(1, round(100 * S * LABEL_CAP * S / (cap[3] - cap[1])))
    f = ImageFont.truetype(FONT, size); ft = ImageFont.truetype(FONT, max(1, round(size * 8 / 26)))
    y0 = round(LABEL_TOP * S) - f.getbbox("H")[1]
    tw = f.getbbox(text)[2]; mw = ft.getbbox("TM")[2]
    im = Image.new("L", (tw + mw + 2 * S, 32 * S)); d = ImageDraw.Draw(im)
    d.text((0, y0), text, font=f, fill=255); d.text((tw + S, y0 + round(size * 19 / 26)), "TM", font=ft, fill=255)
    w = min(122, round(im.width / S))                      # (squeezed to fit the label as the long ones are)
    a = np.asarray(im.resize((w, 32), Image.LANCZOS), np.float32)
    out = np.zeros((32, 128), np.uint8); x0 = (128 - w) // 2
    body = a > 60
    out[:, x0:x0 + w][body] = np.clip(2 + np.round(a[body] / 255 * 13), 2, 15).astype(np.uint8)
    b = out > 0; ring = np.zeros_like(b)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1): ring |= np.roll(np.roll(b, dy, 0), dx, 1)
    out[ring & ~b] = 1
    flat = out.reshape(-1)
    return head + bytes(int(flat[i]) << 4 | int(flat[i + 1]) for i in range(0, flat.size, 2))

# (romrender 2026-11-18, the user's decision A: the page's photo and the VS backdrop are RENDERED by our own renderer,
#  tools/romrender/rr_arena.py, from the arena's own meshes and pictures as the pack gets them (arenas_ring.scene) -- no
#  screenshots. One camera and light for every arena; the page's layout as before, its resampling and palettes rr's.)
_os_ = __import__("os"); _sys_ = __import__("sys")
_sys_.path.insert(0, _os_.path.join(_os_.path.dirname(_os_.path.dirname(_os_.path.dirname(_os_.path.abspath(__file__)))), "tools", "romrender"))
import rr_arena as RA

def rr_pieces(v):
    """the arena as the game draws it (arenas_ring.scene): [(mesh file, TEX, palette, quarter turns)]"""
    import arenas_ring as R
    out = []
    for grp, bias, m, p in R.scene(v):
        try: mb = m.file()
        except (ValueError, AssertionError): continue
        if isinstance(p, tuple): t, q = p[1], p[2]
        elif p == "white": t, q = R.white()
        elif p == "signs": t, q = R.signs()
        else: t, q = rfile(p + 1), rfile(p)
        out.append((mb, t, q, 0))
    return out

# an arena's sky, drawn behind its picture (rr_arena.add_sky): Bash at the Beach's -- its 3D stage's three sky walls
# (scene models 2, 3, 1: left, back, right), their pictures left to right
SKY = {3: ("t07_p06.png", "t05_p04.png", "t03_p02.png")}

def rr_sky(v):
    """[(TEX, palette)] of the arena's sky pictures, left to right (None: no sky)"""
    if v not in SKY: return None
    import arenas_stage3d as A3
    ms = A3.models(v)
    return [A3.picture(v, next(m for m in ms if m["texture"] == name))[1:] for name in SKY[v]]

_PHOTOS = {}
def photo_px(v, W, H):
    if (v, W, H) not in _PHOTOS: _PHOTOS[(v, W, H)] = RA.photo(rr_pieces(v), W, H, sky=rr_sky(v))
    return _PHOTOS[(v, W, H)]

def preview(v):
    """the six tiles' files (bytes), k = column * 3 + row"""
    img = RA.page(photo_px(v, 100, 74), RA.texture_rgba(rfile(LOGO[v] + 1), rfile(LOGO[v])))
    return RA.page_tiles(img, open(NMF % 0x4641, "rb").read()[:16])

def backdrop(v):
    """the forty files: 320 x 240 CI4, 64 x 30 tiles row by row, the palette at the head of the first"""
    return RA.backdrop_tiles(photo_px(v, 320, 240))

def first_of(F, files):
    """files one after another in F's pack (the game reads them as first + k: no sharing): the first's id"""
    first = F.file0 + len(F.data)
    F.data += files
    return first

def build(F, file0):
    """the arenas' files into F (gen/wmport.py's Files; its first id file0); writes OUT_H"""
    F.file0 = file0
    rows = []
    for name, v in ARENAS:
        import arenas_wm
        if arenas_wm.wm_venue(v) is not None:   # (WrestleMania 2000's: its stage set, aprons, label, photo, backdrop)
            rows.append(arenas_wm.arena_row(F, name, arenas_wm.wm_venue(v))); continue
        pieces, t = stage(v)
        meshes, pals = [], []
        for mb, p in pieces:
            tex, pal = pic_pair(F, p)
            meshes.append(F.one(mb)); pals.append(pal)
        _, pal = F.pair(rfile(t["aisle"][0] + 1), rfile(t["aisle"][0]))
        if len(meshes) < SET_MAX: meshes.append(F.one(rfile(AISLE_FLOOR))); pals.append(pal)
        assert len(meshes) <= SET_MAX, (name, len(meshes))
        ap = t["ring"][2]           # (the apron's palette: Revenge's ring list mat, logo, 4 aprons)
        _, right = F.pair(apron_half(rfile(ap + 1), True), rfile(ap))
        _, left = F.pair(apron_half(rfile(ap + 1), False), rfile(ap))
        _, banner = F.pair(rfile(BANNER[v] + 1), rfile(BANNER[v]))
        lab = F.one(label(LABEL[v]))
        prev = first_of(F, preview(v)); back = first_of(F, backdrop(v))
        rows.append((name, v, meshes, pals, [right, left] * 4, [0x80B, 0x80B, 0x80D, banner, banner], lab, prev, back))
    L = ["// generated by gen/arenas.py: WCW/nWo Revenge's arenas (arena.c): each one's stage set, in the base arena's",
         "// place (its meshes / palettes, the texture the palette's file + 1: gen/wmport.py's pack)",
         "#define XARENA_N %d" % len(rows), "#define XARENA_BASE %d   // (the base arena: the game's venue)" % BASE,
         "#define XARENA_SET_MAX %d   // (src/arena.c ARENA_SET_MAX: the stage set's parts)" % SET_MAX,
         "static const char* const xarena_names[XARENA_N] = { %s };" % ", ".join('"%s"' % r[0] for r in rows)]
    # the ARENA page's order (the user, 2026-11-01: WrestleMania 2000's after No Mercy's, then Revenge's last) -- each
    # game's in its own venue order; an arena's number (10 + its row) stays as it is
    wm_v = lambda v: 0 if v == "wm" else int(v[2:])
    page = sorted(range(len(rows)), key=lambda k: (0, wm_v(rows[k][1])) if isinstance(rows[k][1], str) else (1, rows[k][1]))
    L += ["static const u8 xarena_page_order[XARENA_N] = { %s };   // (the rows above, as the ARENA page lists them)"
          % ", ".join(str(k) for k in page)]
    # (each arena's stage set and aprons: gen/arenas_ring.py's xring_* -- its own block, arena.c)
    L += ["static const u16 xarena_truss_pal[XARENA_N][5] = {   // (0x8005078C[v]: the light truss, the 2 banners)"]
    L += ["    { %s }," % ", ".join("0x%X" % x for x in r[5]) for r in rows]
    L += ["};", "// the ARENA page: a row's label (128 x 32 CI4), the photo's six tiles (first + k), the VS backdrop's forty (first + t)"]
    L += ["static const u16 xarena_label_file[XARENA_N] = { %s };" % ", ".join("0x%X" % r[6] for r in rows)]
    L += ["static const u16 xarena_preview_file[XARENA_N] = { %s };" % ", ".join("0x%X" % r[7] for r in rows)]
    L += ["static const u16 xarena_backdrop_file[XARENA_N] = { %s };" % ", ".join("0x%X" % r[8] for r in rows)]
    open(OUT_H, "w").write("\n".join(L) + "\n")
    write_tables(rows)         # (each arena's own per-venue entries and table data: src/arena_table_files.h)
    import arenas_ring          # ... and everything round the ring Revenge's (src/arena_ring.c: data/arenaport/RINGSIDE.md)
    arenas_ring.build(F)
    return rows


# ---- each arena's own per-venue entries and announce table (src/arena_table_files.h; mod/NOTES.md "Arenas restructured",
# step 4) ------------------------------------------------------------------------------------------------------------
# An arcade arena plays with the venue byte XARENA_BASE; what the game reads by that byte outside the arena data is the
# arena's own (arena.c points the readers at it while it's in play): the entrances' nameplate font (func_8000A470:
# u16 0x80045980[v % 10]) and ovl_b's close-shot bytes (func_80109F88: u8 0x801257B8[3 v]) -- each arena's own copy of
# the values it had (venue XARENA_BASE's, the ROM's: no pointer to them). The announce table (and the steps) every arena
# with one has as its own data: the game's table lists (0x8005004C steps, 0x800500B8 places / meshes / palettes) with the
# arcade's places built in -- the table 75 toward the ring (announce.c ANN_TABLE_UP: JR and King sit at it; until step 4
# announce.c wrote that into the game's data). A table block: 0x100 bytes at the game's offsets (0x80050000 + o).
SIX_ROM = _os.path.join(ROOT, "six-engine", "NoMercySix", "nomercy.z64")
ANN_TABLE_UP = 75.0
TABLE_Z = (0xC0, 0xCC, 0xD8, 0xE4)          # (the four pieces' z: -605, -545, -605, -605 in the ROM)
def main_data(a, n):
    rom = open(SIX_ROM, "rb").read()
    o = 0x1050 + a - 0x80000450              # (main_1050: the ROM 0x1050 at 0x80000450)
    return rom[o:o + n]
def ovl_b_data(a, n):
    rom = open(SIX_ROM, "rb").read()
    o = 0x81210 + a - 0x80106760             # (ovl_b: the ROM 0x81210 at 0x80106760)
    return rom[o:o + n]
def table_block():
    b = bytearray(main_data(0x80050000, 0x100))
    for o in TABLE_Z:
        z = struct.unpack_from(">f", b, o)[0]
        struct.pack_into(">f", b, o, z + ANN_TABLE_UP)
    keep = set(range(0x4C, 0x64)) | set(range(0xB8, 0xF8))   # (the steps' lists, the table's: all the block's readers read)
    return bytes(b[i] if i in keep else 0 for i in range(0x100))
def write_tables(rows):
    font = struct.unpack(">H", main_data(0x80045980 + 2 * BASE, 2))[0]
    close = ovl_b_data(0x801257B8 + 3 * BASE, 3)
    tb = table_block()
    L = ["// generated by gen/arenas.py write_tables: each arena's own per-venue entries and announce table data (arena.c)",
         "#define XTABLE_BLOCK 0x100",
         "// the arcade arenas' own nameplate font (func_8000A470's u16 0x80045980[v %% 10]) and close-shot bytes (ovl_b",
         "// 0x801257B8[3 v]): their own values (venue %d's in the ROM, as they had: 0x%X; %s)" % (BASE, font, close.hex()),
         "static const u16 xarena_font[XARENA_N] = { %s };" % ", ".join("0x%X" % font for r in rows),
         "static const u8 xarena_close_bytes[XARENA_N][3] = { %s };" % ", ".join("{ %s }" % ", ".join("0x%02X" % c for c in close) for r in rows),
         "// every arena's own table block (the game's ten, then the arcade's): the steps' and table's lists at the game's",
         "// offsets, the table's four pieces 75 toward the ring (announce.c ANN_TABLE_UP)",
         "static const u8 xtable_block[10 + XARENA_N][XTABLE_BLOCK] __attribute__((aligned(8))) = {"]
    for k in range(10 + len(rows)):
        L.append("    { %s }," % ", ".join("0x%02X" % c for c in tb))
    L.append("};")
    L += effects_and_vs(rows)
    open(OUT_H.replace("arena_files.h", "arena_table_files.h"), "w").write("\n".join(L) + "\n")

# Step 5: the rest of what the game reads by the venue byte, each arcade arena's own:
#  - ovl_b's entrance effect sets (func_8011D2F8: 8-byte entries {set, script list} at 0x80132CC0, by index venue /
#    + 10 / + 20 -- the sparks round the stage in the close shots): each arcade arena its own copy of the effect data
#    (ovl_b 0x80131960..0x80132CC0 from the ROM, its own pointers into it made its own) and its own table into it;
#  - ovl_c's VS-screen pieces (func_80116194: a struct of three pictures by 0x801497FC[v], 0x24 bytes) and the two
#    7 x 10 tables of func_8011649C (0x80149D68 / 0x80149E80 [row][v]): its own copies, every venue's slot its own.
# The values are the ones they had (venue XARENA_BASE's, the ROM's): now each arena's own, nothing pointing at the venue's.
FX_LO, FX_HI, FX_TAB, FX_N = 0x80131960, 0x80132CC0, 0x80132CC0, 30
def ovl_c_data(a, n):
    rom = open(SIX_ROM, "rb").read()
    o = 0xAE390 + a - 0x80106760             # (ovl_c: the ROM 0xAE390 at 0x80106760)
    return rom[o:o + n]
def effects_and_vs(rows):
    blob = ovl_b_data(FX_LO, FX_HI - FX_LO)
    words = list(struct.unpack(">%dI" % (len(blob) // 4), blob))
    tab = list(struct.unpack(">%dI" % (2 * FX_N), ovl_b_data(FX_TAB, 8 * FX_N)))
    ref = lambda w, k: ("(u32)(xfx%d_blob + %d)" % (k, (w - FX_LO) // 4)) if FX_LO <= w < FX_HI and not (w - FX_LO) % 4 else "0x%08Xu" % w
    vs_ptr = struct.unpack(">I", ovl_c_data(0x801497FC + 4 * BASE, 4))[0]
    vs = ovl_c_data(vs_ptr, 0x24)
    ta = [struct.unpack(">I", ovl_c_data(0x80149D68 + 40 * r + 4 * BASE, 4))[0] for r in range(7)]
    tb = [struct.unpack(">I", ovl_c_data(0x80149E80 + 40 * r + 4 * BASE, 4))[0] for r in range(7)]
    L = ["// each arcade arena's own entrance effect data (ovl_b 0x%X..0x%X's, its pointers its own) and table (0x%X's)" % (FX_LO, FX_HI, FX_TAB),
         "#define XFX_N %d" % FX_N]
    for k in range(len(rows)):
        L.append("static u32 xfx%d_blob[%d] = { %s };" % (k, len(words), ", ".join(ref(w, k) for w in words)))
        L.append("static u32 xfx%d_tab[%d] = { %s };" % (k, 2 * FX_N, ", ".join(ref(w, k) for w in tab)))
    L.append("static u32* const xfx_tab[XARENA_N] = { %s };" % ", ".join("xfx%d_tab" % k for k in range(len(rows))))
    L.append("// its own VS-screen pieces (func_80116194's struct, venue %d's in the ROM at 0x%X) and func_8011649C's values" % (BASE, vs_ptr))
    L.append("static const u8 xvs_struct[0x24] __attribute__((aligned(4))) = { %s };" % ", ".join("0x%02X" % b for b in vs))
    L.append("static const u32 xvs_rows_a[7] = { %s };   // (0x80149D68 [row][v])" % ", ".join("0x%X" % v for v in ta))
    L.append("static const u32 xvs_rows_b[7] = { %s };   // (0x80149E80 [row][v])" % ", ".join("0x%X" % v for v in tb))
    return L
