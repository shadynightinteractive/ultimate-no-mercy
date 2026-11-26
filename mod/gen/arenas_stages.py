"""No Mercy's own arenas (venues 1-9): each one's match stage set made the detailed one its entrances' close shots draw
(src/arena.c arena_stage_tables), as gen/arenas_raw.py did RAW IS WAR's (venue 0) -- the close set at the old one's
place and size, the old ramp / aisle and the floor round the ring kept.

The match's set (0x800507FC[v]: 9-15 pieces) is a low-detail copy of the close scene's (scene 10 + v, the entrance
overlay's close shots: tables 0x80052280.., data/arenaport/NM_ENTRANCE.md; arenas_stages_tab.py has both). Each
venue's SPEC gives the transform found by matching the pieces both have (p -> s p + d: the close scene's points with
its place added -- and its extra list's offset -- to the stage-set node's space), what's kept of the old set and
where in the drawing order, the close pieces left out, the floors cut where the old stage's front is, and how the
pictures are put together to fit the game's limits:
  - the parts (src/arena.c's array, ARENA_SET_MAX: the game's 0x8007B8A0 had 15), each one mesh with one picture: pieces with one picture together, in the close scene's
    order as far as that allows (main list, extra list, the animated pieces' first frames, the single models);
  - a mesh <= 127 points and <= 127 triangles (func_8000C398 reads the triangle count as a signed byte);
  - pictures put together into one (a CI4 picture's most: 4096 texels, 2 KB -- TMEM's half) with one 16-colour palette
    (median cut) where a venue needs it (SPEC "pack"), each piece's texels moved into its cell;
  - triangles no camera of the match can see left out: only those facing some point of the box the match's and the
    entrances' cameras move in (CAM: the ring, the aisle up to the stage's front) at more than a glancing angle --
    the game culls back faces, so the close scene's backs of things (seen from behind it the close camera never
    is) and the far sides of beams are never drawn in the match anyway.
No depth buffer: the set is drawn in list order (painter's), so it's the close scene's order. Not animated (the close
scenes' animated pieces are their first frames). Vertex colours: the close sets' own (SmackDown's close set has
the other vertex form, its pictures repeating past 255 texels: colour 200 grey, u / v u16 -- kept so).
Called by gen/wmport.py build_all after gen/arenas_raw.py: build(F) puts the made files in F's pack and writes
src/arena_stage_files.h. python arenas_stages.py [V ...]: a dry run (the parts, their points / triangles)."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp, ROOT
import math, struct, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arenas_ring
from arenas_stages_tab import MATCH, CLOSE

NMF = nmp("/tmp/devbox-0/nmfiles/f/%04x.bin")
OUT_H = nmp("/home/devbox/work/arcade/src/arena_stage_files.h")
SET_MAX = 64                                    # (src/arena.c ARENA_SET_MAX: the game's array had 15)
LIMIT = 127                                     # (points, triangles a mesh)
NAMES = ["RAW IS WAR", "NO MERCY", "SMACKDOWN", "KING OF THE RING", "SUMMERSLAM", "SURVIVOR SERIES", "ROYAL RUMBLE",
         "WRESTLEMANIA", "BACKLASH", "ARMAGEDDON"]

def nfile(f): return open(NMF % f, "rb").read()
def s8(b): return b - 256 if b > 127 else b

# ---- meshes ------------------------------------------------------------------------------------------------------
class Mesh(arenas_ring.Mesh):
    """arenas_ring.Mesh with its vertex form carried along (alt: u, v u16, the colour 200 grey)"""
    alt = 0
    def _with(self, m, alt=None):
        out = Mesh(m.pts, m.uv, m.rgb, m.tris, m.b1, m.b3); out.alt = self.alt if alt is None else alt
        return out
    def map(self, fp=None, fuv=None, frgb=None): return self._with(arenas_ring.Mesh.map(self, fp, fuv, frgb))
    def __add__(self, o): return self._with(arenas_ring.Mesh.__add__(self, o), self.alt | o.alt)
    def sub(self, tris):
        """the mesh of these triangles (and the points they use)"""
        used = sorted({i for t in tris for i in t}); at = {v: k for k, v in enumerate(used)}
        m = Mesh([self.pts[i] for i in used], [self.uv[i] for i in used], [self.rgb[i] for i in used],
                 [tuple(at[i] for i in t) for t in tris], self.b1, self.b3)
        m.alt = self.alt
        return m

# No Mercy's mesh (func_8000C398 / func_8000C1A8; data/arenaport/NM_ARENAS.md section 2) in its two vertex forms:
# byte 0 bit 7 clear -- u, v bytes (+ the page, byte 7) and r g b; set -- u, v u16 at +4 / +6 (no page), colour 200
def mesh_of(b):
    d = b; alt = d[0] >> 7; sc = (d[0] & 0x7F) + 1; nv = d[1] & 0x7F; nt = d[2]; off = [s8(x) * 16 for x in d[4:7]]
    pu, pv = (d[7] >> 4) * 16, (d[7] & 15) * 16
    pts = [tuple((s8(d[8 + 8 * i + k]) + off[k]) * sc for k in range(3)) for i in range(nv)]
    if alt:
        uv = [struct.unpack_from(">HH", d, 8 + 8 * i + 4) for i in range(nv)]
        rgb = [bytes([200, 200, 200])] * nv
    else:
        uv = [(d[8 + 8 * i + 3] + pu, d[8 + 8 * i + 4] + pv) for i in range(nv)]
        rgb = [bytes(d[8 + 8 * i + 5:8 + 8 * i + 8]) for i in range(nv)]
    o = 8 + 8 * nv
    m = Mesh(pts, uv, rgb, [tuple(d[o + 3 * i:o + 3 * i + 3]) for i in range(nt)], d[1] & 0x80, d[3])
    m.alt = alt
    return m

def mesh_file(m):
    """a mesh file of m: the byte form if its u, v fit a page (+ 0..255), else the u16 one (colour 200 grey)"""
    pts = m.pts
    assert 0 < len(pts) <= LIMIT and len(m.tris) <= LIMIT, (len(pts), len(m.tris))
    for sc in range(1, 129):
        off = [int(round((min(p[k] for p in pts) + max(p[k] for p in pts)) / 2 / sc / 16)) for k in range(3)]
        q = [[p[k] / sc - off[k] * 16 for k in range(3)] for p in pts]
        if all(-128 <= o <= 127 for o in off) and all(-128 <= c <= 127 and c == int(c) for v in q for c in v): break
    else: raise ValueError("no scale fits")
    pages = lambda k: [p for p in range(16) if all(0 <= t[k] - 16 * p <= 255 for t in m.uv)]
    pu, pv = pages(0), pages(1)
    alt = m.alt or not pu or not pv
    if alt:
        assert all(0 <= u <= 0xFFFF and 0 <= v <= 0xFFFF for u, v in m.uv)
        out = bytearray([0x80 | (sc - 1), m.b1 | len(pts), len(m.tris), m.b3] + [o & 0xFF for o in off] + [0])
        for v, (u, w) in zip(q, m.uv): out += bytes(int(x) & 0xFF for x in v) + b"\0" + struct.pack(">HH", u, w)
    else:
        pu, pv = max(pu), max(pv)
        out = bytearray([sc - 1, m.b1 | len(pts), len(m.tris), m.b3] + [o & 0xFF for o in off] + [pu << 4 | pv])
        for v, (u, w), c in zip(q, m.uv, m.rgb): out += bytes(int(x) & 0xFF for x in v) + bytes([u - 16 * pu, w - 16 * pv]) + c
    for t in m.tris: out += bytes(t)
    if len(out) & 1: out += b"\0"
    return bytes(out)

def on_grid(m):
    """the points on the grid of the scale their part needs (No Mercy's: s8 + 16 x offset, times the scale)"""
    span = max(max(p[k] for p in m.pts) - min(p[k] for p in m.pts) for k in range(3))
    sc = max(1, math.ceil(span / 230.0))
    return m.map(lambda p: tuple(int(round(c / sc)) * sc for c in p))

def normal(m, t):
    a, b, c = (m.pts[i] for i in t)
    u = [b[k] - a[k] for k in range(3)]; w = [c[k] - a[k] for k in range(3)]
    return (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])

def front(m):
    """the triangles facing the ring (-z): a truss's faces toward it (gen/arenas_raw.py's)"""
    keep = [t for t in m.tris if normal(m, t)[2] < 0]
    return m.sub(keep) if keep else None

def clip(m, axis, keep_ge, at):
    """m cut by the plane p[axis] = at: what's on the kept side (>= at if keep_ge, else <=); triangles across it cut,
    new points with their texels and colour along the edge"""
    side = lambda p: (p[axis] - at) * (1 if keep_ge else -1) >= -1e-6
    pts, uv, rgb, tris = list(m.pts), list(m.uv), list(m.rgb), []
    cache = {}
    def cut(i, j):
        key = (min(i, j), max(i, j))
        if key in cache: return cache[key]
        p, q = m.pts[i], m.pts[j]; t = (at - p[axis]) / float(q[axis] - p[axis])
        pts.append(tuple(a + t * (b - a) if k != axis else at for k, (a, b) in enumerate(zip(p, q))))
        uv.append(tuple(int(round(a + t * (b - a))) for a, b in zip(m.uv[i], m.uv[j])))
        rgb.append(bytes(int(round(a + t * (b - a))) for a, b in zip(m.rgb[i], m.rgb[j])))
        cache[key] = len(pts) - 1
        return cache[key]
    for t in m.tris:
        ins = [side(m.pts[i]) for i in t]
        if all(ins): tris.append(t); continue
        if not any(ins): continue
        poly = []
        for k in range(3):
            i, j = t[k], t[(k + 1) % 3]
            if ins[k]: poly.append(i)
            if ins[k] != ins[(k + 1) % 3]: poly.append(cut(i, j))
        for k in range(1, len(poly) - 1): tris.append((poly[0], poly[k], poly[k + 1]))
    out = Mesh(pts, uv, rgb, tris, m.b1, m.b3); out.alt = m.alt
    return out.sub(tris) if tris else None

# ---- what the match's cameras see --------------------------------------------------------------------------------
# Where the cameras are (the stage-set node's space: the ring's middle is z -1500 there, func_8001D8EC): round the
# ring and over it -- x -900 / 0 / 900, y 200 / 700, z -2400 / -600 -- and on the aisle by the stage's front (z1, per
# venue: the entrances' wide shots). A triangle is kept if it faces one of them at more than COS_MIN (SPEC "cos"):
# a face seen nearly edge on from all of them (a beam's top, sides) is a sliver on the screen.
COS_MIN = 0.25
def cam_points(z1):
    return [(x, y, z) for x in (-900, 0, 900) for y in (200, 700) for z in (-2400, -600)] + [(0, 300, z1)]

def seen(m, cams, cmin=COS_MIN):
    keep = []
    for t in m.tris:
        n = normal(m, t); ln = math.sqrt(sum(c * c for c in n))
        if ln == 0: continue
        c0 = [sum(m.pts[i][k] for i in t) / 3.0 for k in range(3)]
        for c in cams:
            d = [c[k] - c0[k] for k in range(3)]; ld = math.sqrt(sum(x * x for x in d))
            if sum(n[k] * d[k] for k in range(3)) > cmin * ln * ld: keep.append(t); break
    return m.sub(keep) if keep else None

# ---- pictures ----------------------------------------------------------------------------------------------------
def c5551(c): return ((c >> 11) & 31, (c >> 6) & 31, (c >> 1) & 31, c & 1)
def rgba_of(pal):
    """the game's picture pal (+1 the texture) as rows of RGBA5551"""
    t, q = nfile(pal + 1), nfile(pal)
    w, h = t[0] + 1, t[1] + 1
    cols = struct.unpack(">16H", q[:32])
    px = []
    for b in t[8:8 + w * h // 2]: px += [b >> 4, b & 15]
    return [[cols[px[y * w + x]] for x in range(w)] for y in range(h)]
WHITE = ("pic", bytes([7, 7, 0, 16, 2, 2, 3, 3]) + bytes(32), struct.pack(">16H", *([0xFFFF] * 16)))   # (8 x 8, white)
def size_of(pal):
    if isinstance(pal, tuple): t = pal[1]
    else: t = nfile(pal + 1)
    return t[0] + 1, t[1] + 1

def tone(pal):
    """a picture's mean colour (its opaque texels)"""
    cs = [c5551(c) for r in rgba_of(pal) for c in r if c & 1] or [(31, 31, 31, 1)]
    return [sum(c[k] for c in cs) / float(len(cs)) for k in range(3)]

def resize(rows, w, h):
    """rows made w x h: by whole factors down (opaque texels averaged; clear if most were), else nearest"""
    H, W = len(rows), len(rows[0])
    if (W, H) == (w, h): return [list(r) for r in rows]
    fx, fy = W / float(w), H / float(h)
    out = []
    for y in range(h):
        r = []
        for x in range(w):
            if fx >= 1 and fy >= 1 and fx == int(fx) and fy == int(fy):
                cs = [c5551(rows[int(y * fy) + j][int(x * fx) + i]) for j in range(int(fy)) for i in range(int(fx))]
                op = [c for c in cs if c[3]]
                if len(op) * 2 < len(cs): r.append(0); continue
                m = [int(round(sum(c[k] for c in op) / float(len(op)))) for k in range(3)]
                r.append(m[0] << 11 | m[1] << 6 | m[2] << 1 | 1)
            else: r.append(rows[min(H - 1, int((y + 0.5) * fy))][min(W - 1, int((x + 0.5) * fx))])
        out.append(r)
    return out

def make_picture(rows, exact=None):
    """a CI4 picture (texture file, palette file) of rows of RGBA5551 (w x h powers of 2, <= 4096 texels): exact --
    its palette, else 16 colours (median cut over the opaque texels; colour 0 the clear one if any is clear)"""
    from PIL import Image
    h, w = len(rows), len(rows[0])
    assert w * h <= 4096 and w & (w - 1) == 0 and h & (h - 1) == 0, (w, h)       # (2 KB: TMEM's half)
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
        lo = 1 if clear else 0
        def near(c):
            if not (c & 1): return 0
            r, g, b, _ = c5551(c)
            return min(range(lo, len(pal)), key=lambda i: (c5551(pal[i])[0] - r) ** 2 + (c5551(pal[i])[1] - g) ** 2 + (c5551(pal[i])[2] - b) ** 2 if pal[i] & 1 else 1 << 30)
        cache = {}
        idx = [cache.setdefault(c, near(c)) for c in flat]
    tex = bytes([w - 1, h - 1, 0, 16, 2, 2, int(math.log2(w)), int(math.log2(h))]) + bytes(idx[i] << 4 | idx[i + 1] for i in range(0, len(idx), 2))
    return ("pic", tex, struct.pack(">16H", *pal))

def uv_span(m, pal):
    """a piece's texels within one repeat of its picture: the repeat (ku, kv), else None"""
    w, h = size_of(pal)
    us, vs = [t[0] for t in m.uv], [t[1] for t in m.uv]
    ku, kv = min(us) // w, min(vs) // h
    if max(us) > (ku + 1) * w or max(vs) > (kv + 1) * h: return None
    return ku, kv

def into_cell(m, pal, cell):
    """m's texels moved into its picture's cell of a packed one (x, y, w, h)"""
    w, h = size_of(pal); x0, y0, cw, ch = cell
    k = uv_span(m, pal)
    assert k is not None, "a piece whose picture repeats can't be packed"
    ku, kv = k
    f = lambda t: (x0 + min(cw - 1, int(round((t[0] - ku * w) * cw / float(w)))), y0 + min(ch - 1, int(round((t[1] - kv * h) * ch / float(h)))))
    return m.map(fuv=f)

def norm_uv(m, pal):
    """m's texels moved by whole repeats (even ones: mirrored tiles stay so) to start near 0"""
    if isinstance(pal, tuple): return m
    w, h = size_of(pal)
    ku = min(t[0] for t in m.uv) // (2 * w) * 2 * w; kv = min(t[1] for t in m.uv) // (2 * h) * 2 * h
    return m.map(fuv=lambda t: (t[0] - ku, t[1] - kv)) if ku or kv else m

# ---- the venues --------------------------------------------------------------------------------------------------
# SPEC[v]: s, d -- the transform (close scene + place -> the match's set: p -> s p + d);
#   keep  -- the old set's pieces kept [(mesh, palette, where)] (mesh a tuple ((mesh, palette), ..): those made one
#            mesh, palette None -- their picture, or their pack's): where "start", "end", a close mesh (just before the
#            part it's in) or ("after", a close mesh);
#   drop  -- close meshes left out (their aisles, ramps, rails: the old ramp stays; what can't fit, the least seen);
#   clip  -- [(close meshes, axis, ">=" / "<=", at)]: those cut there (match space) -- floors at the old stage's front;
#   z1    -- the cameras' place on the aisle by the stage's front (cam_points); cos -- COS_MIN, {palette / mesh /
#            "all": the cosine};  solid -- palettes of one colour: that colour put in the vertex colours, the picture
#            WHITE (pieces of several such pictures then one part);
#   front -- close meshes (or palettes) of which only the faces toward the ring (-z) are kept (a truss's);
#   all_faces -- close meshes kept whole; alone -- close meshes in a part of their own (a screen whose picture
#            another piece has too: tron.c's extent check);
#   repic -- {palette: palette}: pieces drawn with another picture of the set (one like it: a truss's lattice ..),
#            its mean colour put in the vertex colours;
#   pack  -- [(W, H, [(palette, x, y, w, h)], exact palette or None)]: pictures put together into one W x H;
#   bands -- z (match space) where the set is cut into depths: a part's pieces from one (their middles' z) -- all
#            pieces, or those of band_pals (palettes); order -- {mesh: mesh}: a piece's part drawn just before the
#            other's;
#   screen-- the big screen's close mesh (tron_panels_all.h's match rows: XSTAGE_SCREEN<v>_*).
SPEC = {}

def close_pieces(v):
    """the close scene's pieces in its drawing order: [(mesh id, palette, Mesh with the place (+ offset) added)]"""
    c = CLOSE[v]; pl = c["place"]
    out = []
    def add(f, p, off=(0, 0, 0)):
        m = mesh_of(nfile(f)).map(lambda q: tuple(q[k] + pl[k] + off[k] for k in range(3)))
        out.append((f, p, m))
    for f, p in c["main"]: add(f, p)
    for f, p in c["x1"]: add(f, p, c["x1_off"])
    for g in ("x2", "x3"):
        if c[g]: add(c[g][0], c[g][1][0])
    for f, p in c["m61"]: add(f, p)
    for f, p, m in new_pieces(v): out.append((f, p, m.map(lambda q: tuple(q[k] + pl[k] for k in range(3)))))
    return out

# ---- pieces of the arena's own, new (2026-11-01; mod/NOTES.md "Arenas restructured", step 3) ---------------------------
# SURVIVOR SERIES' Titantron (the user: the real one had a screen in a truss over the stage, where Backlash's sits): a
# screen over the back hexagon frame (the frames stand 0..850 high; z 910 in the match) in a truss frame of the arena's
# own lattice beam (0xF4E, the beams over its frames), the screen's own picture the arena's emblem (0xF34) until the
# video / the hard camera is on it (tron.c, tron_panels_all.h: XSTAGE_SCREEN5_*, its close-up row XSTAGE_CLOSE5_*). In
# the close scene's own space (no place added: the match's s p + d puts it there), its "id" a number no file has; it
# goes in the match's set (through pieces(), as any close piece) and in the close scene's extra list (arena.c: the
# arena's own close block). Both windings: seen() keeps the faces a camera sees.
NEW_SCREEN = 0x7F00
NEW = {5: dict(screen=(-320, 320, 900, 1220), z=900, beam=40, screen_pic=0xF34, truss_pic=0xF4E, grey=0xB2)}

def new_pieces(v):
    """[(id, palette, Mesh)] in the close scene's space (its place not added)"""
    if v not in NEW: return []
    n = NEW[v]; s, d = SPEC[v]["s"], SPEC[v]["d"]; pl = CLOSE[v]["place"]
    to_close = lambda q: tuple(int(round((q[k] - d[k]) / s - pl[k])) for k in range(3))   # (match -> the close scene's)
    x0, x1, y0, y1 = n["screen"]; z, b = n["z"], n["beam"]; g = bytes([n["grey"]] * 3)
    def quad(xa, xb, ya, yb, uv):
        pts = [to_close(q) for q in ((xa, ya, z), (xb, ya, z), (xb, yb, z), (xa, yb, z))]
        tris = [(0, 1, 2), (0, 2, 3), (0, 2, 1), (0, 3, 2)]          # (both windings)
        return Mesh(pts, uv, [g] * 4, tris)
    # the screen: the emblem's middle rows (its picture 64 x 64, the screen 2 : 1)
    scr = quad(x0, x1, y0, y1, [(63, 55), (0, 55), (0, 8), (63, 8)])
    # the truss frame: four beams round it, the lattice (128 x 32) along each, one repeat to 4 x the beam's width
    beams = []
    for xa, xb, ya, yb, along in ((x0 - b, x1 + b, y1, y1 + b, "x"), (x0 - b, x1 + b, y0 - b, y0, "x"),
                                  (x0 - b, x0, y0, y1, "y"), (x1, x1 + b, y0, y1, "y")):
        L = (xb - xa) if along == "x" else (yb - ya)
        u = int(round(L / (4.0 * b) * 128))
        uv = [(u, 31), (0, 31), (0, 0), (u, 0)] if along == "x" else [(0, 31), (0, 0), (u, 0), (u, 31)]
        beams.append(quad(xa, xb, ya, yb, uv))
    import arenas_ring
    truss = arenas_ring.join(beams)
    return [(NEW_SCREEN, n["screen_pic"], scr), (NEW_SCREEN + 1, n["truss_pic"], truss)]

def pieces(v):
    """the venue's pieces in the match's place, cut and culled: [(mesh id, palette, Mesh)]"""
    sp = SPEC[v]; s, d = sp["s"], sp["d"]
    cams = cam_points(sp["z1"])
    out = []
    for f, p, m in close_pieces(v):
        if f in sp.get("drop", ()): continue
        m = m.map(lambda q: tuple(s * q[k] + d[k] for k in range(3)))
        for ids, axis, op, at in sp.get("clip", ()):
            if ids == "all" or f in ids:
                m = clip(m, axis, op == ">=", at)
                if m is None: break
        if m is None: continue
        cos = sp.get("cos", {}); cos = cos.get(p, cos.get(f, cos.get("all", COS_MIN)))
        if f in sp.get("front", ()) or p in sp.get("front", ()): m = front(m)       # (only its faces toward the ring)
        elif f not in sp.get("all_faces", ()): m = seen(m, cams, cos)
        if m is None: continue
        if p in sp.get("solid", ()):                 # (a picture of one colour: it into the vertex colours, the picture white)
            r, g, b, a = c5551(rgba_of(p)[0][0])
            m = m.map(fuv=lambda t: (0, 0), frgb=lambda c: bytes(min(255, int(round(x * k / 31.0))) for x, k in zip(c, (r, g, b))))
            p = WHITE
        if p in sp.get("repic", {}):                 # (drawn with another picture of the set: its texels scaled to it)
            q = sp["repic"][p]; (w0, h0), (w1, h1) = size_of(p), size_of(q)
            k = [min(2.0, max(0.5, a / max(b, 1e-3))) for a, b in zip(tone(p), tone(q))]    # (its colour kept: in the
            m = m.map(fuv=lambda t: (t[0] * w1 // w0, t[1] * h1 // h0),                     #  vertex colours)
                      frgb=lambda c: bytes(min(255, int(round(x * f))) for x, f in zip(c, k))); p = q
        out.append((f, p, norm_uv(m, p)))
    return out

def packed(v):
    """palette -> (the packed picture, its cell) for the venue's packs"""
    sp = SPEC[v]; out = {}
    for W, H, cells, exact in sp.get("pack", ()):
        rows = [[0] * W for _ in range(H)]
        for pal, x, y, w, h in cells:
            r = resize(rgba_of(pal), w, h)
            for j in range(h): rows[y + j][x:x + w] = r[j]
        pic = make_picture(rows, exact)
        for pal, x, y, w, h in cells: out[pal] = (pic, (x, y, w, h))
    return out

def parts(v):
    """[(name, Mesh or a game mesh id, picture: a game palette or ("pic", texture, palette))] in drawing order"""
    sp = SPEC[v]
    pk = packed(v)
    groups = []                                     # [picture key, picture, [Mesh], first index, names]
    ps = pieces(v)
    idx = {f: i for i, (f, p, m) in enumerate(ps)}
    for i, (f, p, m) in enumerate(ps):
        if f in sp.get("order", {}): i = idx[sp["order"][f]] - 0.5     # (drawn just before another piece's part)
        if p in pk: pic, cell = pk[p]; key = id(pic); m = into_cell(m, p, cell)
        elif isinstance(p, tuple): pic, key = p, p
        else: pic, key = p, (nfile(p + 1), nfile(p))         # (the same picture under two palette ids: one)
        if f in sp.get("alone", ()): key = ("alone", f)       # (a part of its own: the screen tron.c finds by its points)
        if sp.get("bands") and (not sp.get("band_pals") or p in sp["band_pals"]):   # (pieces at one depth together)
            zc = sum(q[2] for q in m.pts) / float(len(m.pts))
            key = (key, sum(1 for b in sp["bands"] if zc > b))
        # (a piece too big for a part on its own: cut in two by its triangles)
        chunks = [m]
        while any(len(c.pts) > LIMIT or len(c.tris) > LIMIT for c in chunks):
            c = next(c for c in chunks if len(c.pts) > LIMIT or len(c.tris) > LIMIT); chunks.remove(c)
            n = len(c.tris) // 2; chunks += [c.sub(c.tris[:n]), c.sub(c.tris[n:])]
        for c in chunks:
            g = next((g for g in groups if g[0] == key and len(g[2].pts) + len(c.pts) <= LIMIT and len(g[2].tris) + len(c.tris) <= LIMIT), None)
            if g: g[2] = g[2] + c; g[4].append(f)
            else: groups.append([key, pic, c, i, [f]])
    groups.sort(key=lambda g: g[3])
    snapped = arenas_ring.snap_parts([m for key, pic, m, i, names in groups])
    out = [("%X" % names[0] + ("+%d" % (len(names) - 1) if len(names) > 1 else ""), sm, pic, names) for (key, pic, m, i, names), sm in zip(groups, snapped)]
    for mesh, pal, where in sp.get("keep", ()):
        if isinstance(mesh, tuple):                  # (old pieces made one: [(mesh, palette)], a pack's picture)
            ms = []
            for f, p in mesh:
                m = mesh_of(nfile(f))
                ms.append(into_cell(m, p, pk[p][1]) if p in pk else m)
            name = "old " + "+".join("%X" % f for f, p in mesh)
            mesh, pal = arenas_ring.snap_parts([arenas_ring.join(ms)])[0], (pk[mesh[0][1]][0] if mesh[0][1] in pk else mesh[0][1])
        else: name = "old %X" % mesh
        if where == "start": at = 0
        elif where == "end": at = len(out)
        elif isinstance(where, tuple): at = next(j for j, r in enumerate(out) if where[1] in r[3]) + 1     # ("after", mesh)
        else: at = next(j for j, r in enumerate(out) if where in r[3])
        out.insert(at, (name, mesh, pal, []))
    return [r[:3] for r in out]

def screen_extent(v, rows):
    """the big screen's points as the game gets them (its mesh file): (x0, x1, y0, y1)"""
    sp = SPEC[v]
    for n, m, p in rows:
        if not isinstance(m, int) and not n.startswith("old") and int(n.split("+")[0], 16) == sp.get("screen"):
            q = mesh_of(mesh_file(m)).pts
            return min(t[0] for t in q), max(t[0] for t in q), min(t[1] for t in q), max(t[1] for t in q)
    return None

def build(F):
    """the venues' made files into F (gen/wmport.py's Files); writes OUT_H; -> {venue: [(name, mesh id, palette id)]}"""
    import arenas
    res = {}
    L = ["// generated by gen/arenas_stages.py: No Mercy's own arenas' match stage sets made their entrances' close sets",
         "// (arena.c arena_stage_tables): venue v's 0x800507FC / 0x80050824 / 0x8005084C entries (meshes, palettes: the",
         "// texture the palette's file + 1); XSTAGE_SCREEN_*: the big screen in the set (tron_panels_all.h's match rows:",
         "// the extent of its mesh's points)"]
    tabs = []
    for v in sorted(SPEC):
        ps = parts(v)
        assert len(ps) <= SET_MAX, (v, len(ps))
        rows = []
        for name, m, p in ps:
            mid = m if isinstance(m, int) else F.one(mesh_file(m))
            pid = arenas.pic_pair(F, p)[1] if isinstance(p, tuple) else p
            rows.append((name, mid, pid))
        res[v] = rows
        ext = screen_extent(v, ps)
        if ext:
            L.append("#define XSTAGE_SCREEN%d_X0 %d\n#define XSTAGE_SCREEN%d_X1 %d\n#define XSTAGE_SCREEN%d_Y0 %d\n#define XSTAGE_SCREEN%d_Y1 %d"
                     % (v, ext[0], v, ext[1], v, ext[2], v, ext[3]))
        tabs.append((v, rows))
    # the arenas' new pieces in their close scenes' extra lists (arena.c: the arena's own close block) -- the close
    # scene's space, as the game gets them
    close_new = {}
    for v in sorted(NEW):
        rows = []
        for f, p, m in new_pieces(v): rows.append((f, F.one(mesh_file(m)), p, m))
        close_new[v] = rows
    L.append("#ifndef XSTAGE_SCREEN_ONLY")
    for v, rows in close_new.items():
        L.append("// %s's own pieces in its close scene's extra list (0x80052354 / 0x80052390 / 0x800523CC [v], arena.c)" % NAMES[v])
        L.append("#define XSTAGE%d_CLOSE_N %d" % (v, len(rows)))
        L.append("static const u16 xstage%d_close_mesh[%d] = { %s };" % (v, len(rows), ", ".join("0x%X" % r[1] for r in rows)))
        L.append("static const u16 xstage%d_close_pal[%d] = { %s };" % (v, len(rows), ", ".join("0x%X" % r[2] for r in rows)))
    for v, rows in tabs:
        L.append("// %s (venue %d): %s" % (NAMES[v], v, ", ".join(r[0] for r in rows)))
        L.append("static const u16 xstage%d_mesh[%d] = { %s };" % (v, len(rows), ", ".join("0x%X" % r[1] for r in rows)))
        L.append("static const u16 xstage%d_pal[%d] = { %s };" % (v, len(rows), ", ".join("0x%X" % r[2] for r in rows)))
    L.append("#define XSTAGE_N 10")
    L.append("static const u16* const xstage_mesh[XSTAGE_N] = { %s };" % ", ".join("xstage%d_mesh" % v if v in SPEC else "0" for v in range(10)))
    L.append("static const u16* const xstage_pal[XSTAGE_N] = { %s };" % ", ".join("xstage%d_pal" % v if v in SPEC else "0" for v in range(10)))
    L.append("static const s16 xstage_n[XSTAGE_N] = { %s };" % ", ".join(str(len(dict(tabs)[v])) if v in SPEC else "0" for v in range(10)))
    L.append("#endif")
    open(OUT_H, "w").write("\n".join(L) + "\n")
    write_screens(close_new, res)
    import arenas, arenas_walls, arenas_wm   # (each arena's own walls: src/arena_walls_files.h, arena.c; WM2000's keep the table)
    arenas_walls.build([1 if arenas_wm.wm_venue(v) is not None else 0 for name, v in arenas.ARENAS])
    return res

OUT_SCREENS = OUT_H.replace("arena_stage_files.h", "arena_screen_files.h")
def write_screens(close_new, res):
    """src/arena_screen_files.h: the arenas' new screens for tron_panels_all.h's rows -- each one's extent as its mesh
    files have it and its picture's fold (TRON_ALL.md 1), in the match's set and the close scene"""
    import arenas_stage3d
    from arenas_stage3d import fold
    L = ["// generated by gen/arenas_stages.py: the arenas' new screens (2026-11-01, mod/NOTES.md \"Arenas restructured\" step",
         "// 3) for tron.c -- the extent of the screen's points as the game gets them (x0 x1 y0 y1) and its picture's fold",
         "// (sig0, sig1: TRON_ALL.md 1), in the match's stage set (MATCH) and the entrances' close scene (CLOSE)"]
    for v, rows in close_new.items():
        f, mid, p, m = next(r for r in rows if r[0] == NEW_SCREEN)
        q = mesh_of(mesh_file(m)).pts
        a, b = fold(nfile(p + 1))
        L.append("// %s: its Titantron over the stage (close: its extra list; match: XSTAGE_SCREEN%d_*), the picture 0x%X" % (NAMES[v], v, p + 1))
        L.append("#define XSCREEN%d_CLOSE_X0 %d\n#define XSCREEN%d_CLOSE_X1 %d\n#define XSCREEN%d_CLOSE_Y0 %d\n#define XSCREEN%d_CLOSE_Y1 %d"
                 % (v, min(t[0] for t in q), v, max(t[0] for t in q), v, min(t[1] for t in q), v, max(t[1] for t in q)))
        L.append("#define XSCREEN%d_SIG0 0x%08Xu\n#define XSCREEN%d_SIG1 0x%08Xu" % (v, a, v, b))
    import arenas
    for name, rv in arenas.ARENAS:
        if rv not in arenas_stage3d.BOARD: continue
        k = [n for n, x in arenas.ARENAS].index(name)
        sc = arenas_stage3d.screens(rv)
        L.append("// %s (arena %d): its picture board (Revenge's slideshow, scene 0x3C models %s: four tiles, a part each)"
                 % (name, 10 + k, ", ".join(str(t) for t in arenas_stage3d.BOARD[rv])))
        L.append("#define XBOARD_ARENA %d" % (10 + k))
        for scene, ((x0, x1, y0, y1), sigs) in sorted(sc.items()):
            S = scene.upper()
            L.append("#define XBOARD_%s_X0 %d\n#define XBOARD_%s_X1 %d\n#define XBOARD_%s_Y0 %d\n#define XBOARD_%s_Y1 %d" % (S, x0, S, x1, S, y0, S, y1))
            for i, (a, b) in enumerate(sigs):
                L.append("#define XBOARD_%s_SIG0_%d 0x%08Xu\n#define XBOARD_%s_SIG1_%d 0x%08Xu" % (S, i, a, S, i, b))
    # WrestleMania 2000's venues with a video screen (RAW IS WAR, Sunday Night Heat): each its match and close-up rows
    # (gen/arenas_wm.py tron_rows: the screen's extent in its mesh, its own picture's fold), arena 10 + its place in ARENAS
    import arenas_wm
    wrows = []
    for k, (name, rv) in enumerate(arenas.ARENAS):
        if arenas_wm.wm_venue(rv) is not None: wrows += arenas_wm.tron_rows(10 + k, arenas_wm.wm_venue(rv))
    L.append("// WrestleMania 2000's venues with a screen (gen/arenas_wm.py tron_rows): tron_panels_all.h takes these rows")
    L.append("#define XWM_TRON_ROWS \\")
    L += [r.split("//")[0].rstrip() + " \\" for r in wrows] + [""]
    open(OUT_SCREENS, "w").write("\n".join(L) + "\n")

def stats(v):
    """the pieces by picture (after the cuts and culling): what packing has to work with"""
    ps = pieces(v); by = {}
    for f, p, m in ps: by.setdefault(p, []).append((f, m))
    print("== %s (venue %d): %d pieces, %d pictures, %d points, %d triangles" % (NAMES[v], v, len(ps), len(by),
          sum(len(m.pts) for f, p, m in ps), sum(len(m.tris) for f, p, m in ps)))
    for p, l in by.items():
        w, h = size_of(p)
        npt, ntr = sum(len(m.pts) for f, m in l), sum(len(m.tris) for f, m in l)
        inb = all(uv_span(m, p) is not None for f, m in l)
        bb = [(min(q[k] for f, m in l for q in m.pts), max(q[k] for f, m in l for q in m.pts)) for k in range(3)]
        print("  pal %X %3dx%-3d %2d pieces %4d pts %4d tris %s  x %5d..%5d y %5d..%5d z %5d..%5d  %s" % (p, w, h, len(l), npt, ntr,
              "packable" if inb else "repeats ", *[int(c) for b in bb for c in b], " ".join("%X" % f for f, m in l)))

def report(v):
    ps = parts(v)
    print("== %s (venue %d): %d parts" % (NAMES[v], v, len(ps)))
    for name, m, p in ps:
        if isinstance(m, int): print("  %-12s game mesh 0x%X pal 0x%X" % (name, m, p)); continue
        print("  %-12s %3d points %3d triangles  %s" % (name, len(m.pts), len(m.tris), ("pal 0x%X" % p) if isinstance(p, int) else "made picture"))

import arenas_stages_spec                       # (SPEC's entries, one per venue)
SPEC.update(arenas_stages_spec.SPEC)

if __name__ == "__main__":
    for v in [int(x) for x in sys.argv[1:]] or sorted(SPEC): report(v)
