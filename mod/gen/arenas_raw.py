"""No Mercy's own RAW IS WAR (venue 0): its match stage set made the detailed one the entrances' close shots draw
(src/arena.c arena_raw_tables; data/arenaport/rawstage/: the offline looks).

The match's set (0x800507FC[0]: 13 pieces 0x575-0x581) is a low-detail copy of the close scene's (scene 10, the
entrance overlay's close shots: tables 0x80052280[0].., 73 main pieces 0xA90-0xAEA, data/arenaport/NM_ENTRANCE.md):
the same stage at 0.8 x the close scene's size, its floor 160 up (the close scene stands its Superstar on the deck, at
y 0) and 1040 further along z (in the stage-set node's space: the node itself is 1500 along z, func_8001D8EC). Matched
on the pieces both have: the big screen (0x57B x +-460, y 370..830, z 1000 <- 0xAA7 x +-580, y 260..840, z -10), the
deck's front face (0x578 y 0..160, z 810 <- 0xA92 y -200..0, z -290), the doorway (0x57F z 1190 <- 0xAEA z 190), the
side screens (z 950 <- -110..-70), the whole width (+-820..830 <- +-1030..1040) and height (870 <- 880). So the close
set goes in at exactly the old one's place and size: p -> 0.8 p + (0, 160, 1040), CLOSE_TO_MATCH.

Kept from the old set: the ramp (0x576) and the floor round the ring (0x581: last, as it was). The close set's own ramp
and landing (0xA9B, 0xA9A) are left out (the old ramp stays), its deck cut at its front face (the close deck reaches
150 past it: z -440; cut at -290 the deck ends where the old one did, at the ramp's top), the face's gap for its own
wider ramp closed (0xA92: +-180, the old ramp +-110).

The stage set's parts: src/arena.c's arena_set_parts (ARENA_SET_MAX, 64 -- the game's array 0x8007B8A0 had room for 15;
mod/NOTES.md "Arenas restructured"), each one mesh (<= 127 points, <= 127 triangles: func_8000C398 reads the count
signed) with one picture. The whole close set (2026-11-01: while the set had 15 places its pictures were packed four to
a 64 x 64, the truss only its faces toward the ring, the arches' second copies, the logo's dark copy and the doorway's
side walls left out): every piece of the main list, the extra list and the single model in the close scene's order,
each with its own picture (the game's files as they are), pieces of one picture next to each other one part; the
side towers each side its own parts (beams, then posts).
Not animated: the fist screens are their first frames (the close scene's 0xAA5 / 0xAA6 animate, 12 palettes); the stage
set has no animation. Vertex colours: the close set's own.
Called by gen/wmport.py build_all after gen/arenas_entrance.py: build(F) puts the made files in F's pack and writes
src/arena_raw_files.h."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp, ROOT
import math, struct, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

NMF = nmp("/tmp/devbox-0/nmfiles/f/%04x.bin")
OUT_H = nmp("/home/devbox/work/arcade/src/arena_raw_files.h")
SET_MAX = 64                                    # (src/arena.c ARENA_SET_MAX: the game's array had 15)
SCALE, MOVE = 1.0, (0.0, 160.0, 1245.0)        # CLOSE_TO_MATCH: p -> SCALE p + MOVE (2026-11-02, the user: "scale it normal"; was 0.8, +1040: the deck's front stays at the ramp's top)
DECK_FRONT = -440                               # (the close deck's front: nothing cut now, the face moved to it)
FACE_Z = -290                                   # (the close scene's: its face 0xA92's z)

def nfile(f): return open(NMF % f, "rb").read()
def s8(b): return b - 256 if b > 127 else b

# The old set (0x800507FC[0] / 0x80050824[0], 13), and the pieces of it kept
OLD_MESH = [0x57F, 0x580, 0x578, 0x576, 0x577, 0x57B, 0x57C, 0x57D, 0x575, 0x57E, 0x579, 0x57A, 0x581]
OLD_PAL = [0x823, 0x821, 0x813, 0x811, 0x825, 0x81D, 0x819, 0x81B, 0x80F, 0x81F, 0x815, 0x817, 0x827]
RAMP, RING_FLOOR = (0x576, 0x811), (0x581, 0x827)
# The close scene's (index 0; data/arenaport/entrance/scenetab.py on cap/ent_step3_ram.bin): main list (its palettes),
# extra list (bias 500), animated pieces' first frames, single model.
# [stagelists 2026-11-19] The main list, its palettes and the extra list are the game's own file numbers: read from the No
# Mercy ROM, not copied here (they were until 2026-11-19). The same places and check number as the first start's builder
# (exe/WWFNoMercyRecomp/src/main/arcade_firstrun_stages.cpp raw_lists): three big-endian 16-bit lists one after the other,
# each padded to 4 bytes -- 73 pieces (+0), their 73 palettes (+0x94), the extra list's 13 (+0x128) -- 0x142 bytes.
NM_ROM = nmp("/home/devbox/work/nomercy.z64")  # (WWF No Mercy (USA) (Rev 1))
LIST_PLACES = ((0x514A0, "No Mercy (USA) Rev 1"), (0x51540, "No Mercy (USA) v1.0"))
LISTS_BYTES, LISTS_FNV = 0x142, 0xEC9595F4A422D990  # (FNV-1a 64 of the 0x142 bytes: a check number, not the bytes)

def _fnv1a64(b):
    h = 0xCBF29CE484222325
    for x in b: h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h

def close_lists(path=None):
    """(CLOSE, CLOSE_PAL, EXTRA) from the ROM: the place whose bytes have the check number; stops if none does"""
    rom = open(path or NM_ROM, "rb").read()
    for at, _ in LIST_PLACES:
        b = rom[at:at + LISTS_BYTES]
        if len(b) == LISTS_BYTES and _fnv1a64(b) == LISTS_FNV:
            u16 = lambda o, n: list(struct.unpack(">%dH" % n, b[o:o + 2 * n]))
            return u16(0, 73), u16(0x94, 73), u16(0x128, 13)
    raise SystemExit("arenas_raw.py: %s does not hold RAW IS WAR's close scene lists where No Mercy (USA) Rev 1 (0x%X) or "
                     "v1.0 (0x%X) has them" % (os.path.basename(path or NM_ROM), LIST_PLACES[0][0], LIST_PLACES[1][0]))

# the extra list (0x80052354[0], bias 500, its offset 0, 0, 0: data/arenaport/entrance/cap/ent_step3_ram.bin): the arches'
# second copies (0xE36), the WWF.com sign
CLOSE, CLOSE_PAL, EXTRA = close_lists()
PAL_OF = dict(zip(CLOSE, CLOSE_PAL))
PAL_OF.update({0xA94: 0xE22, 0xAA5: 0xE44, 0xAA6: 0xE4C, 0xA8F: 0xE16, 0xA93: 0xE1E})
SCREEN = 0xAA7                                  # (its picture 0xE33 = the old screen 0x57B's 0x81E: tron.c finds it by it)

# ---- meshes ------------------------------------------------------------------------------------------------------
def mesh(f):
    from arenas_ring import Mesh_from_file
    return Mesh_from_file(nfile(f))

def to_match(m):
    return m.map(lambda p: tuple(SCALE * c + d for c, d in zip(p, MOVE)))

def front(m):
    """the triangles facing the ring (-z) and the points they use"""
    from arenas_ring import Mesh
    keep = []
    for t in m.tris:
        a, b, c = (m.pts[i] for i in t)
        nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if nz < 0: keep.append(t)
    used = sorted({i for t in keep for i in t}); at = {v: k for k, v in enumerate(used)}
    return Mesh([m.pts[i] for i in used], [m.uv[i] for i in used], [m.rgb[i] for i in used],
                [tuple(at[i] for i in t) for t in keep], m.b1, m.b3)

def cut_deck(m):
    """a deck floor's points in front of the face (z < DECK_FRONT) brought back to it, texels and colour along"""
    from arenas_ring import Mesh
    pts, uv, rgb = list(m.pts), list(m.uv), list(m.rgb)
    for i, p in enumerate(m.pts):
        if p[2] >= DECK_FRONT: continue
        # its row's next point back (the same x): the floors are grids
        j = min((k for k, q in enumerate(m.pts) if q[0] == p[0] and q[2] > p[2]), key=lambda k: m.pts[k][2])
        t = (DECK_FRONT - p[2]) / float(m.pts[j][2] - p[2])
        pts[i] = (p[0], p[1], DECK_FRONT)
        uv[i] = tuple(int(round(a + t * (b - a))) for a, b in zip(m.uv[i], m.uv[j]))
        rgb[i] = bytes(int(round(a + t * (b - a))) for a, b in zip(m.rgb[i], m.rgb[j]))
    return Mesh(pts, uv, rgb, list(m.tris), m.b1, m.b3)

def close_face(m):
    """0xA92 with its middle (+-180: the close ramp's gap) closed, the picture carried on mirrored to x 0"""
    from arenas_ring import Mesh
    pts, uv, rgb, tris = list(m.pts), list(m.uv), list(m.rgb), list(m.tris)
    at = {p: i for i, p in enumerate(pts)}
    top, bot = at[(-180, 0, FACE_Z)], at[(-180, -200, FACE_Z)]
    u_mid = int(round(180 / 860.0 * 127))
    n = len(pts)
    pts += [(0, 0, FACE_Z), (0, -200, FACE_Z)]; uv += [(u_mid, 0), (u_mid, 31)]; rgb += [rgb[top], rgb[bot]]
    for x in (-180, 180):
        a, b = at[(x, 0, FACE_Z)], at[(x, -200, FACE_Z)]
        for t in ((a, n, b), (n, n + 1, b)):
            p, q, r = (pts[i] for i in t)
            nz = (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
            tris.append(t if nz < 0 else (t[0], t[2], t[1]))
    pts = [(p[0], p[1], DECK_FRONT if p[2] == FACE_Z else p[2]) for p in pts]
    return Mesh(pts, uv, rgb, tris, m.b1, m.b3)

def retex(m, fu=None, fv=None):
    return m.map(fuv=lambda t: (fu(t[0]) if fu else t[0], fv(t[1]) if fv else t[1]))

def on_grid(m):
    """the points on the grid of the scale their part needs (No Mercy's: s8 + 16 x offset, times the scale)"""
    span = max(max(p[k] for p in m.pts) - min(p[k] for p in m.pts) for k in range(3))
    sc = max(1, math.ceil(span / 230.0))
    return m.map(lambda p: tuple(int(round(c / sc)) * sc for c in p))

# ---- pictures ----------------------------------------------------------------------------------------------------
def rgba_of(pal):
    """the game's picture pal (+1 the texture) as rows of (r, g, b, a) 5-bit + alpha bit"""
    t, q = nfile(pal + 1), nfile(pal)
    w, h = t[0] + 1, t[1] + 1
    cols = struct.unpack(">16H", q[:32])
    px = []
    for b in t[8:8 + w * h // 2]: px += [b >> 4, b & 15]
    return [[cols[px[y * w + x]] for x in range(w)] for y in range(h)], w, h

def c5551(c): return ((c >> 11) & 31, (c >> 6) & 31, (c >> 1) & 31, c & 1)

def make_picture(rows, exact=None):
    """a 64 x 64 CI4 picture (texture file, palette file) of rows of RGBA5551: exact -- the palette, else 16
    colours chosen (median cut over the opaque texels; colour 0 the clear one if any texel is clear)"""
    from PIL import Image
    h, w = len(rows), len(rows[0])
    assert (w, h) == (64, 64)
    flat = [c for r in rows for c in r]
    if exact is not None:
        pal = list(exact); idx = [pal.index(c) for c in flat]
    else:
        clear = any(not (c & 1) for c in flat)
        solid = [c for c in flat if c & 1]
        k = 16 - (1 if clear else 0)
        im = Image.new("RGB", (len(solid), 1))
        im.putdata([tuple(v * 255 // 31 for v in c5551(c)[:3]) for c in solid])
        q = im.quantize(colors=k, method=Image.Quantize.MEDIANCUT)
        qp = q.getpalette()[:3 * k]
        cols = [(qp[3 * i] >> 3) << 11 | (qp[3 * i + 1] >> 3) << 6 | (qp[3 * i + 2] >> 3) << 1 | 1 for i in range(len(qp) // 3)]
        pal = ([0] if clear else []) + cols
        pal += [0] * (16 - len(pal))
        def near(c):
            if not (c & 1): return 0
            r, g, b, _ = c5551(c)
            return min(range(1 if clear else 0, len(pal)), key=lambda i: (c5551(pal[i])[0] - r) ** 2 + (c5551(pal[i])[1] - g) ** 2 + (c5551(pal[i])[2] - b) ** 2 if pal[i] & 1 else 1 << 30)
        cache = {}
        idx = [cache.setdefault(c, near(c)) for c in flat]
    tex = bytes([63, 63, 0, 16, 2, 2, 6, 6]) + bytes(idx[i] << 4 | idx[i + 1] for i in range(0, len(idx), 2))
    return ("pic", tex, struct.pack(">16H", *pal))

def half(rows, axis):
    """every other texel across (axis 0) or down (axis 1)"""
    return [r[::2] for r in rows] if axis == 0 else rows[::2]

def box2(rows):
    """a quarter the size: each 2 x 2 averaged (opaque pictures)"""
    out = []
    for y in range(0, len(rows), 2):
        r = []
        for x in range(0, len(rows[0]), 2):
            cs = [c5551(rows[y + j][x + i]) for j in (0, 1) for i in (0, 1)]
            m = [int(round(sum(c[k] for c in cs) / 4.0)) for k in range(3)]
            r.append(m[0] << 11 | m[1] << 6 | m[2] << 1 | 1)
        out.append(r)
    return out

def side_by_side(a, b): return [ra + rb for ra, rb in zip(a, b)]
def stacked(a, b): return a + b

# ---- the set -----------------------------------------------------------------------------------------------------
def parts():
    """[(name, Mesh or a game mesh file id, picture: a game palette id)] in drawing order -- the close scene's pieces
    (main list, extra list, single model) in its order, each with its own picture, pieces of one picture next to each
    other one part (<= 127 points / triangles)"""
    from arenas_ring import join
    M = lambda f: mesh(f)
    out = []
    def group(name, pieces, pal):
        """pieces (Meshes) of one picture -> parts within LIMIT points / triangles, in order"""
        cur, n_p, n_t, k = [], 0, 0, 0
        for m in pieces:
            if cur and (n_p + len(m.pts) > 127 or n_t + len(m.tris) > 127):
                out.append(("%s %d" % (name, k), join(cur), pal)); k += 1; cur, n_p, n_t = [], 0, 0
            cur.append(m); n_p += len(m.pts); n_t += len(m.tris)
        if cur: out.append(("%s %d" % (name, k) if k else name, join(cur), pal))
    # the deck's front face, its middle closed; the walkway; the four side floors (each its own picture)
    out.append(("face", close_face(M(0xA92)), 0xE1C))
    out.append(("walkway", cut_deck(M(0xA99)), 0xE2C))
    for f in (0xA95, 0xA96, 0xA97, 0xA98): out.append(("floor %X" % f, cut_deck(M(f)), PAL_OF[f]))
    out.append(("ramp", RAMP[0], RAMP[1]))
    # the back wall, the side walls, the doorway's side walls, the doorway
    for f in (0xA90, 0xA91, 0xA93, 0xAEA): out.append(("wall %X" % f, M(f), PAL_OF[f]))
    # the WWF logo: its dark copy behind (its 3D edge), then the front copy -- silver (0xE40), the red swoosh (0xE42)
    group("logo back", [M(f) for f in (0xA9E, 0xAA0, 0xAA2)], 0xE40); group("logo back red", [M(0xAA4)], 0xE42)
    group("logo", [M(f) for f in (0xA9D, 0xA9F, 0xAA1)], 0xE40); group("logo red", [M(0xAA3)], 0xE42)
    # the fist screens (the animated pieces' first frames, each its own picture)
    out.append(("fist L", M(0xAA5), 0xE44)); out.append(("fist R", M(0xAA6), 0xE4C))
    out.append(("screen", M(SCREEN), 0xE32))
    # the truss whole: the screen's frame (0xE3C), its side posts (0xE3E), the side towers (beams 0xE38, posts 0xE3A:
    # each side its own parts), the arches (0xE36)
    frame = [f for f in CLOSE if PAL_OF[f] == 0xE3C]
    posts = [f for f in CLOSE if PAL_OF[f] == 0xE3E]
    towers = [f for f in CLOSE if PAL_OF[f] in (0xE38, 0xE3A)]
    arches = [f for f in CLOSE if PAL_OF[f] == 0xE36]
    group("frame", [M(f) for f in frame], 0xE3C)
    group("posts", [M(f) for f in posts], 0xE3E)
    half_t = len(towers) // 2
    for side, ts in (("R", towers[:half_t]), ("L", towers[half_t:])):
        group("tower %s" % side, [M(f) for f in ts if PAL_OF[f] == 0xE38], 0xE38)
        group("tower %s posts" % side, [M(f) for f in ts if PAL_OF[f] == 0xE3A], 0xE3A)
    group("arches", [M(f) for f in arches], 0xE36)
    # the extra list (drawn after the main list: its bias 500): the arches' second copies, the WWF.com sign; then the
    # single model, the AKI NETWORK sign over the screen
    group("arches back", [M(f) for f in EXTRA if f != 0xA94], 0xE36)
    out.append(("sign WWF.com", M(0xA94), 0xE22))
    out.append(("sign AKI", M(0xA8F), 0xE16))
    out.append(("ring floor", RING_FLOOR[0], RING_FLOOR[1]))
    # in the match's place, on the grid of each part's scale
    from arenas_ring import snap_parts
    out = [(n, m, p) for (n, _, p), m in zip(out, snap_parts([to_match(m) if not isinstance(m, int) else m for n, m, p in out]))]
    assert len(out) <= SET_MAX, len(out)
    return out

LEFT_OUT = "the close ramp / landing (0xA9B 0xA9A: the old ramp stays)"

def build(F):
    """the set's made files into F (gen/wmport.py's Files); writes OUT_H; -> [(name, mesh id, palette id)]"""
    import arenas
    rows = []
    ps = parts()
    for name, m, p in ps:
        mid = m if isinstance(m, int) else F.one(m.file())
        pid = arenas.pic_pair(F, p)[1] if isinstance(p, tuple) else p
        rows.append((name, mid, pid))
    from arenas_ring import Mesh_from_file
    scr = Mesh_from_file(next(m for n, m, p in ps if n == "screen").file())      # (its points as the game gets them)
    xs, ys = [q[0] for q in scr.pts], [q[1] for q in scr.pts]
    L = ["// generated by gen/arenas_raw.py: RAW IS WAR's match stage set made the entrances' close set (arena.c",
         "// arena_raw_tables): venue 0's 0x800507FC / 0x80050824 / 0x8005084C entries (meshes, palettes: the texture the",
         "// palette's file + 1)",
         "// the big screen in the set (tron_panels_all.h's RAW match row: the extent of mesh 0x%X's points)" % next(r[1] for r in rows if r[0] == "screen"),
         "#define XRAW_SCREEN_X0 %d" % min(xs), "#define XRAW_SCREEN_X1 %d" % max(xs),
         "#define XRAW_SCREEN_Y0 %d" % min(ys), "#define XRAW_SCREEN_Y1 %d" % max(ys),
         "#ifndef XRAW_SCREEN_ONLY",
         "#define XRAW_SET_N %d" % len(rows),
         "#define XRAW_FIST_L %d" % [r[0] for r in rows].index("fist L"), "#define XRAW_FIST_R %d" % [r[0] for r in rows].index("fist R"),
         # the fists' pictures, as the close scene animates them (2026-11-02, the user: "bring those animated RAW screens into the
         # arena full time"): 4 frames a side, each a 32-byte palette and a 1032-byte picture: arena.c arena_raw_anim
         "static const u8 xraw_fist_pal[8][32] = { %s };" % ", ".join("{ %s }" % ", ".join(str(b) for b in nfile(0xE44 + 2 * k)[:32]) for k in range(8)),
         "static const u8 xraw_fist_tex[8][1032] = { %s };" % ", ".join("{ %s }" % ", ".join(str(b) for b in nfile(0xE45 + 2 * k)[:1032]) for k in range(8)),
         "static const u16 xraw_set_mesh[XRAW_SET_N] = { %s };" % ", ".join("0x%X" % r[1] for r in rows),
         "static const u16 xraw_set_pal[XRAW_SET_N] = { %s };" % ", ".join("0x%X" % r[2] for r in rows),
         "// (%s)" % ", ".join(r[0] for r in rows),
         "#endif"]
    open(OUT_H, "w").write("\n".join(L) + "\n")
    return rows

if __name__ == "__main__":
    for name, m, p in parts():
        if isinstance(m, int): print("%-13s game mesh 0x%X pal 0x%X" % (name, m, p)); continue
        b = m.file()
        print("%-13s %3d points %3d triangles  %s" % (name, b[1] & 0x7F, b[2], ("pal 0x%X" % p) if isinstance(p, int) else "made picture"))
