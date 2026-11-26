"""WCW/nWo Revenge's ringside in No Mercy (src/arena_ring.c): everything round the ring as Revenge has it.

gen/arenas.py brings Revenge's stage set, aprons and the truss banners; this brings the rest
(data/arenaport/RINGSIDE.md): the ring (Revenge's mat and, at Halloween Havoc, its logo; its posts, cable wraps and
yellow turnbuckles), the floor round it (Nitro's flames, Havoc's stone), its barricades with their signs, its crowd
(three sides and the stage's), the ropes' colour -- and none of No Mercy's own: no stands, no upper decks, no steel
steps, no announce table (arena_ring.c keeps the game from building it and moves its collision walls), no light
truss (TRUSS). Revenge's pieces come over as they are (the same mesh format and units: data/arenaport/REV_ARENAS.md)
except:
  - their vertex colours. Revenge lights its arena: every vertex's "colour" is the normal (73, 73, 73) =
    (1, 1, 1)/sqrt(3); No Mercy's arena isn't lit (TEXEL0 x SHADE, func_80010314), so as they are they're drawn at
    73/255 of their brightness (the Havoc face in data/arenaport/pics/havoc_stage.png: 0.25 of its picture). Made
    BRIGHT here, the stage set's too (it's rebuilt here: arena_ring.c gives the base venue this set, not
    gen/arenas.py's);
  - Revenge draws one side's pieces four times, turned (its ring nodes, func_8000E920 / func_8000EF90); No Mercy's
    lists are in world space, so each side is its own copy here, turned (rot);
  - No Mercy's lists have fixed lengths (the code's): a list's spare places get EMPTY (one triangle of no area).
Where each goes (No Mercy's lists, func_8001B548 / func_8001CD34; the order a list is drawn in is its order):
  0x80050040 / 44  ring list A (bias 0): the logo (Havoc) or EMPTY;           the cage match's at +4
  0x800507B4 / C4  the 8 "aprons" (bias 150): Revenge's 4 aprons, its mat, 3 EMPTY (one picture per side, as
                   Revenge's: no halves); palettes 0x800507D4[v]
  0x80050064 / 68  the floor round the ring (bias 400): Revenge's floor (0x2F5) turned four ways, as two meshes
  0x80050070       the 10 barricade pieces (each its own node, sorted by its centre): per side the signs (0x30D
                   above 0x30F in one 64 x 64 picture: their palettes are the same) and the bars (0x30B: the middle,
                   the outer faces; the aisle's rails on the stage's side), 2 EMPTY
  0x800500A0 / A8  the corner (4 turned nodes): post 0x2ED, cable wraps 0x2F0, yellow turnbuckles 0x2F3 (vertex
                   colours: a white picture)
  0x800500F8       the stands (bias 20000): Revenge's far crowd (0x346 / 0x347) on the 3 crowd sides, 12 EMPTY
  0x80050160..[v]  the upper decks (bias 40000): 1 EMPTY
  0x800501BC .. 0x800502AC  the 4 near-row groups (biases 2500 .. 500, two palette lists each: the crowd's two
                   frames, as Revenge's lists A and B): Revenge's crowd tiers back to front
  0x80050780       the light truss (bias 500): EMPTY x 5 unless TRUSS
  0x80052860..     the ropes' colour set (func_80021980): Revenge's grey
  0x800507FC[v]..  the stage set: gen/arenas.py's pieces less its floors and rails (here now), BRIGHT
Called by gen/arenas.py build() (or gen/wmport.py build_all after it): build(F) puts the files in F's pack and
writes src/arena_ring_files.h."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp, ROOT
import struct
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arenas
from arenas import rfile, s8

OUT_H = nmp("/home/devbox/work/arcade/src/arena_ring_files.h")
BRIGHT = 0xFF           # Revenge's pieces' vertex colour in No Mercy (its lit (73,73,73) normals -> this grey)
TRUSS = False           # No Mercy's light truss over the ring and its 2 banners (Revenge has none)
CAGE = 250.0 / 280.0    # No Mercy's cage match ring: +-250 (its lists at +4 / 0x800507C4 / 0x80050068 / 0x800500A8)
ROPE_RGB = [(0x60, 0x60, 0x60), (0x08, 0x08, 0x08), (0x38, 0x38, 0x38), (0x38, 0x38, 0x38)]  # (a rope's 4 vertices:
                        # Revenge's ropes in its matches, grey 0x60 on top to near black underneath: rev_*_mat_apron.png)
NORMAL = bytes([0x49, 0x49, 0x49])
MAT_Y, LOGO_Y = 90, 100 # Revenge's ring mat (0x2F6) and its centre logo (0x34F): their heights (data/arenaport/RINGSIDE.md)

# ---- meshes as data ---------------------------------------------------------------------------------------------
class Mesh:
    """a mesh's points (world), u v (texels, the page added), r g b, triangles; its two flag bytes"""
    def __init__(self, pts, uv, rgb, tris, b1=0, b3=0):
        self.pts, self.uv, self.rgb, self.tris, self.b1, self.b3 = pts, uv, rgb, tris, b1, b3
    @staticmethod
    def rev(f):
        d = rfile(f)
        sc = (d[0] & 0x7F) + 1; nv = d[1] & 0x7F; nt = d[2]; off = [s8(x) * 16 for x in d[4:7]]
        pu, pv = (d[7] >> 4) * 16, (d[7] & 15) * 16
        pts = [tuple((s8(d[8 + 8 * i + k]) + off[k]) * sc for k in range(3)) for i in range(nv)]
        uv = [(d[8 + 8 * i + 3] + pu, d[8 + 8 * i + 4] + pv) for i in range(nv)]
        rgb = [bytes(d[8 + 8 * i + 5:8 + 8 * i + 8]) for i in range(nv)]
        o = 8 + 8 * nv
        tris = [tuple(d[o + 3 * i:o + 3 * i + 3]) for i in range(nt)]
        return Mesh(pts, uv, rgb, tris, d[1] & 0x80, d[3])
    def map(self, fp=None, fuv=None, frgb=None):
        return Mesh([fp(p) for p in self.pts] if fp else list(self.pts), [fuv(t) for t in self.uv] if fuv else list(self.uv),
                    [frgb(c) for c in self.rgb] if frgb else list(self.rgb), list(self.tris), self.b1, self.b3)
    def rot(self, r):       # a quarter turn r times about y (the side r of Revenge's turned nodes)
        def f(p):
            x, y, z = p
            for _ in range(r & 3): x, z = z, -x
            return (x, y, z)
        return self.map(f)
    def lit(self):          # Revenge's normals made a colour (its own colours, the yellow turnbuckles', kept)
        return self.map(frgb=lambda c: bytes([BRIGHT] * 3) if c == NORMAL else c)
    def __add__(self, o):
        assert self.b1 == o.b1 and self.b3 == o.b3, "pieces of one mesh: one flags"
        n = len(self.pts)
        return Mesh(self.pts + o.pts, self.uv + o.uv, self.rgb + o.rgb, self.tris + [tuple(i + n for i in t) for t in o.tris], self.b1, self.b3)
    def file(self):
        """No Mercy's mesh file (func_8000C398): the smallest scale its points fit at, one texture page"""
        pts = self.pts
        assert 0 < len(pts) <= 127 and len(self.tris) <= 127, (len(pts), len(self.tris))   # (func_8000C398: signed bytes)
        for sc in range(1, 129):
            off = [int(round((min(p[k] for p in pts) + max(p[k] for p in pts)) / 2 / sc / 16)) for k in range(3)]
            q = [[p[k] / sc - off[k] * 16 for k in range(3)] for p in pts]
            if all(-128 <= o <= 127 for o in off) and all(-128 <= c <= 127 and c == int(c) for v in q for c in v): break
        else: raise ValueError("no scale fits")
        # the texture page (one per mesh, 16s added to every u / v): the largest that leaves them all bytes, as
        # Revenge's own (0x84: u + 128, v + 64 -- a whole number of its picture's widths: the picture repeats)
        pu = max(p for p in range(16) if all(0 <= u - 16 * p <= 255 for u, v in self.uv))
        pv = max(p for p in range(16) if all(0 <= v - 16 * p <= 255 for u, v in self.uv))
        out = bytearray([sc - 1, self.b1 | len(pts), len(self.tris), self.b3] + [o & 0xFF for o in off] + [pu << 4 | pv])
        for v, (u, w), c in zip(q, self.uv, self.rgb):
            out += bytes(int(x) & 0xFF for x in v) + bytes([u - 16 * pu, w - 16 * pv]) + c
        for t in self.tris: out += bytes(t)
        if len(out) & 1: out += b"\0"
        return bytes(out)

def points_of(b):
    d = b; sc = (d[0] & 0x7F) + 1; off = [s8(x) * 16 for x in d[4:7]]
    return [tuple((s8(d[8 + 8 * i + k]) + off[k]) * sc for k in range(3)) for i in range(d[1] & 0x7F)]

def bright(mb):
    """a mesh file (Revenge's, as gen/arenas.py packs them) with Revenge's normals made BRIGHT"""
    b = bytearray(mb); nv = b[1] & 0x7F
    for i in range(nv):
        o = 8 + 8 * i + 5
        if bytes(b[o:o + 3]) == NORMAL: b[o:o + 3] = bytes([BRIGHT] * 3)
    return bytes(b)

def q10(c): return int(round(c / 10.0)) * 10     # (a scaled point kept on the 10s: the mesh's scale 10 fits it)

def grid_scale(m):
    """the grid a part's points go on (No Mercy's mesh: s8 + 16 x offset, times the scale): a power of two, so a point
    two parts share can go on both parts' grids (snap_parts)"""
    span = max(max(p[k] for p in m.pts) - min(p[k] for p in m.pts) for k in range(3))
    sc = 1
    while sc * 230 < span: sc *= 2
    return sc

def snap_parts(ms):
    """the parts' points on their grids, a point two parts share on the coarser one of the two (their edges meet:
    each part rounded on its own grid opened hairline cracks between neighbours -- pictures no longer packed into one
    part, 2026-11-01). ms: Meshes (others passed as they are)"""
    scs = [grid_scale(m) if hasattr(m, "pts") else 0 for m in ms]
    key = lambda p: tuple(int(round(c * 4)) for c in p)
    most = {}
    for m, sc in zip(ms, scs):
        if not sc: continue
        for p in m.pts: most[key(p)] = max(most.get(key(p), 0), sc)
    return [m.map(lambda p, sc=sc: tuple(int(round(c / most[key(p)])) * most[key(p)] for c in p)) if sc else m for m, sc in zip(ms, scs)]

def join(ms):
    m = ms[0]
    for o in ms[1:]: m = m + o
    return m

def rev(*fs, r=0):
    return join([Mesh.rev(f).rot(r).lit() for f in fs])

# EMPTY: a list's spare place -- one triangle of no area under the ring (drawn as nothing)
EMPTY = Mesh([(0, 10, 0)] * 3, [(0, 0)] * 3, [bytes([BRIGHT] * 3)] * 3, [(0, 1, 2)])

# ---- pictures ---------------------------------------------------------------------------------------------------
def c5551(r, g, b, a=1): return (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | a
def white():
    """16 x 16 CI4, colour 1 white (the turnbuckles' vertex colours as they are; EMPTY's)"""
    tex = bytes([15, 15, 0, 16, 0, 0, 4, 4]) + bytes([0x11] * 128)
    pal = struct.pack(">16H", 0, 0xFFFF, *([0] * 14))
    return tex, pal
def signs():
    """0x30E above 0x310 (the barricade's signs, left and right: one palette, 0x30D = 0x30F) as one 64 x 64 CI4"""
    a, b = rfile(0x30E), rfile(0x310)
    assert rfile(0x30D) == rfile(0x30F) and a[:2] == b[:2] == bytes([63, 31])
    return bytes([63, 63]) + a[2:6] + bytes([6, 6]) + a[8:8 + 1024] + b[8:8 + 1024], rfile(0x30D)

# ---- Revenge's lists (its RAM: data/arenaport/rev/cap/m0_ram.bin; the same in every arena) ----------------------
def rev_ram():
    ram = open(arenas.REV_RAM % 0, "rb").read()
    r16 = lambda a: struct.unpack_from(">H", ram, a & 0x7FFFFF)[0]
    return lambda a, n: [r16(a + 2 * i) for i in range(n)]
L16 = rev_ram()
SIDE_M, SIDE_A, SIDE_B = L16(0x80036768, 19), L16(0x80036790, 19), L16(0x800367B8, 19)   # the crowd sides (nodes 1-3)
FRONT_M, FRONT_A, FRONT_B = L16(0x80036750, 3), L16(0x80036758, 3), L16(0x80036760, 3)   # the low front row (5-7)
assert SIDE_M[0] == 0x2F4 and SIDE_M[-1] == 0x312 and FRONT_M == [0x2F4, 0x305, 0x306]
STAGE_CROWD = [0x321, 0x322, 0x31F, 0x320, 0x323, 0x324, 0x347, 0x344, 0x345, 0x341, 0x33B, 0x33E]   # (the stage's list)
RAILS = [0x319, 0x31A, 0x31B, 0x31C, 0x31D, 0x31E]
CROWD_SIDES = (1, 2, 3)
# the crowd's tiers, back to front (Revenge's list order): its far crowd, the upper rows, the middle, the front photos
TIER = {0x346: 1, 0x347: 1, 0x342: 2, 0x343: 2, 0x344: 2, 0x345: 2, 0x33F: 3, 0x340: 3, 0x341: 3, 0x321: 3, 0x322: 3}
for f in (0x337, 0x338, 0x339, 0x33A, 0x33B, 0x33C, 0x33D, 0x33E, 0x31F, 0x320, 0x323, 0x324): TIER[f] = 4
for f in (0x305, 0x306): TIER[f] = 5

def crowd_pieces():
    """[(tier, side, [files], palA, palB)]: a side's pieces with one picture pair together"""
    out = []
    def add(side, fs, pa, pb):
        for f, a, b in zip(fs, pa, pb):
            if f in (0x2F4, 0x312): continue
            g = next((g for g in out if g[0] == TIER[f] and g[1] == side and g[3] == a and g[4] == b), None)
            if g: g[2].append(f)
            else: out.append([TIER[f], side, [f], a, b])
    sa = dict(zip(SIDE_M, SIDE_A)); sb = dict(zip(SIDE_M, SIDE_B))
    st = arenas.table(0); stA = dict(zip(st["meshes"], st["pals"]))
    ram = open(arenas.REV_RAM % 0, "rb").read()
    stB = dict(zip(st["meshes"], L16(struct.unpack_from(">I", ram, 0x36CE4)[0], len(st["meshes"]))))
    add(0, STAGE_CROWD, [stA[f] for f in STAGE_CROWD], [stB[f] for f in STAGE_CROWD])
    for s in CROWD_SIDES:
        add(s, SIDE_M, SIDE_A, SIDE_B)
        add(s, FRONT_M, FRONT_A, FRONT_B)
    return [tuple(g) for g in out]

# The crowd at the stage's end carried on back beside Revenge's 3D stage (gen/arenas_stage3d.py: it stands STAGE_BACK
# further back than the flat set it replaced) -- the user: the crowd "back so it meets the stage". Revenge's stage-end
# corner (its stage list's crowd at +x: far crowd, upper rows, photos) once more, moved back STAGE_BACK, and mirrored to
# -x (Revenge's -x corner is a crowd side's, not moved). Not the aisle's strips (0x321 / 0x322): moved back they'd
# stand on the stage's floor. In the crowd lists every Revenge arena shares.
EXT_CROWD = [0x347, 0x344, 0x345, 0x341, 0x33B, 0x33E]
def stage_back():
    import arenas_stage3d
    return int(arenas_stage3d.ring_z(0) - 1220)          # (Nitro's: 730)

def mirror_x(m):
    """a mesh mirrored left for right (its triangles turned round, so it faces as it did)"""
    return Mesh([(-x, y, z) for x, y, z in m.pts], list(m.uv), list(m.rgb), [(a, c, b) for a, b, c in m.tris], m.b1, m.b3)

def crowd_ext():
    """[(tier, Mesh, palA, palB)]: the stage-end corner moved back, both sides, one mesh per picture pair"""
    st = arenas.table(0); stA = dict(zip(st["meshes"], st["pals"]))
    ram = open(arenas.REV_RAM % 0, "rb").read()
    stB = dict(zip(st["meshes"], L16(struct.unpack_from(">I", ram, 0x36CE4)[0], len(st["meshes"]))))
    back = stage_back(); groups = {}
    for f in EXT_CROWD:
        m = Mesh.rev(f).lit().map(lambda p: (p[0], p[1], p[2] + back))
        groups.setdefault((TIER[f], stA[f], stB[f]), []).extend([m, mirror_x(m)])
    return [(tier, join(ms), a, b) for (tier, a, b), ms in groups.items()]

# No Mercy's crowd lists (func_8001CD34): name, its mesh list's length, two palette lists (frames) or one, tiers it takes
GROUPS = [("stands", 15, False, (1,)), ("g1", 3, True, (2,)), ("g2", 9, True, (1, 3)), ("g3", 22, True, (4,)), ("g4", 22, True, (5,))]

def crowd_lists():
    """{group: [(Mesh, palA, palB)]} (the stage's far corner, tier 1, is drawn after the stage: in g2)"""
    lists = {g[0]: [] for g in GROUPS}
    for tier, side, fs, a, b in sorted(crowd_pieces(), key=lambda g: (g[0], g[1])):
        if tier == 1 and side != 0: grp = "stands"
        elif tier == 2 and side != 0: grp = "g1"
        elif tier <= 3: grp = "g2"          # (the stage's far corner and upper rows: after the stage, before the middle)
        else: grp = {4: "g3", 5: "g4"}[tier]
        if grp == "stands": assert a == b, "the stands have one palette list"
        lists[grp].append((rev(*fs, r=side), a, b))
    for tier, m, a, b in crowd_ext():           # (as the stage's own corner: after the stage)
        lists["g2" if tier <= 3 else "g3"].append((m, a, b))
    for name, n, _, _ in GROUPS: assert len(lists[name]) <= n, (name, len(lists[name]))
    return lists

def barricades():
    """[(Mesh, picture)] -- picture "signs" (0x30D / 0x30F stacked) or 0x30B (the bars); per side, 8"""
    out = []
    for side in range(4):
        sg = [Mesh.rev(0x311), Mesh.rev(0x313).map(fuv=lambda t: (t[0], t[1] + 32))]     # (0x30F the lower half)
        bars = []
        if side == 0: bars += [Mesh.rev(f) for f in RAILS]
        else:
            sg.append(Mesh.rev(0x303))               # the outer faces (Revenge's "fixed 3", sides 1-3), the middle's signs
            bars += [Mesh.rev(0x312), Mesh.rev(0x302), Mesh.rev(0x304)]
        out.append((join([m.rot(side).lit() for m in sg]), "signs"))
        out.append((join([m.rot(side).lit() for m in bars]), 0x30B))
    return out

def floor(cage=False):
    """Revenge's floor round the ring (0x2F5, its stage side) turned four ways, as two meshes (sides 0 + 2, 1 + 3)"""
    m = Mesh.rev(0x2F5).lit()
    if cage: m = m.map(lambda p: (q10(p[0] * CAGE), p[1], q10(p[2] * CAGE)) if abs(p[2]) < 400 else p)   # (its inner edge)
    return [m.rot(0) + m.rot(2), m.rot(1) + m.rot(3)]

def ring(cage=False):
    """(list A's mesh: the logo or EMPTY, the 8 "aprons": 4 aprons, the mat, 3 EMPTY) -- the logo's [1]"""
    f = (lambda p: (q10(p[0] * CAGE), p[1], q10(p[2] * CAGE))) if cage else None
    ap = [Mesh.rev(x).lit().map(f) for x in (0x2F8, 0x2F9, 0x2FA, 0x2FB)]
    mat = Mesh.rev(0x2F6).lit().map(f); logo = Mesh.rev(0x34F).lit().map(f)
    # (2026-11-15, the user's playtest: "wrestlers phase through the ring-mat logos; the logos look raised above the mat") Revenge's
    # logo lies at y 100, 10 above its mat (y 90): Revenge draws it after the mat with no depth, so it showed on the mat. The exe's
    # renderer draws the world with depth (since 2026-11-09), and a man standing or lying on the mat was 10 under the logo: hidden in
    # it. Put on the mat's own plane it's drawn over the mat as before -- the renderer's layers by plane (arcade_view.cpp planes_,
    # 2026-11-15): a later draw on a plane already drawn goes over it -- and nobody is under it.
    assert {p[1] for p in logo.pts} == {LOGO_Y} and {p[1] for p in mat.pts} == {MAT_Y}, "Revenge's logo and mat where they were"
    logo = logo.map(lambda p: (p[0], MAT_Y, p[2]))
    return logo, ap + [mat, EMPTY, EMPTY, EMPTY]

def corner(cage=False):
    """post, cable wraps, turnbuckles (Revenge's corner at +x -z; the cage's ring 28 smaller each way)"""
    f = (lambda p: (p[0] - 28, p[1], p[2] + 28)) if cage else None
    return [Mesh.rev(x).lit().map(f) for x in (0x2ED, 0x2F0, 0x2F3)]

def stage(v):
    """gen/arenas.py's stage set (its pieces, Revenge's order) less the rails (barricades here), BRIGHT"""
    pieces, t = arenas.stage(v)
    return [(bright(mb), p) for mb, p in pieces if p != 0x30B]

# ---- the files and the header -----------------------------------------------------------------------------------
def build(F):
    """the ringside's files into F (gen/wmport.py's Files, F.file0 as gen/arenas.py set it); writes OUT_H -- every list
    per arena (XRING_N rows, gen/arenas.py's ARENAS order): Revenge's arenas share theirs, WrestleMania 2000's RAW IS
    WAR has its own (gen/arenas_wm.py ring_row: its meshes with byte 1 bit 7, No Mercy's combiner FC15FE2B)"""
    wt, wp = white(); _, WHITE = F.pair(wt, wp)
    st, sp = signs(); _, SIGNS = F.pair(st, sp)
    def pal(p):
        if isinstance(p, tuple) and p[0] == "pic": return F.pair(p[1], p[2])[1]
        return WHITE if p == "white" else SIGNS if p == "signs" else F.pair(rfile(p + 1), rfile(p))[1]
    E = F.one(EMPTY.file())
    mesh = lambda m: F.one(m.file())
    # Revenge's (the same in all its arenas)
    logo, ap = ring(); logo_c, ap_c = ring(True)
    LOGO, LOGO_C = mesh(logo), mesh(logo_c)
    rev = dict(apron=[mesh(m) for m in ap], apron_cage=[mesh(m) for m in ap_c],
               floor=[mesh(m) for m in floor()] + [mesh(m) for m in floor(True)],
               barricade=[(mesh(m), pal(p)) for m, p in barricades()],
               corner=[mesh(m) for m in corner()] + [mesh(m) for m in corner(True)],
               corner_pal=[pal(0x2EE), pal(0x2F1), WHITE], rope=ROPE_RGB, empty=E, truss=[E] * 5)
    cl = crowd_lists()
    for name, n, two, _ in GROUPS: rev[name] = [(mesh(m), (pal(a), pal(b))) for m, a, b in cl[name]]
    rows = []
    for name, v in arenas.ARENAS:
        import arenas_wm
        if arenas_wm.wm_venue(v) is not None:   # (WrestleMania 2000's venues: their own lists, padded with their own EMPTY)
            w = arenas_wm.ring_row(F, lambda m: F.one(arenas_wm.encode(m)), pal, arenas_wm.wm_venue(v))
            e = w["empty"]
            rows.append(dict(name=name, v=v, logo=w["logo"], apron_mesh=w["apron"] + [e] * (8 - len(w["apron"])),
                             apron_cage=w["apron_cage"] + [e] * (8 - len(w["apron_cage"])),
                             apron=w["apron_pal"] + [WHITE] * (8 - len(w["apron_pal"])), floor_mesh=w["floor"], floor=w["floor_pal"],
                             barricade=w["barricade"], corner=w["corner"], corner_pal=w["corner_pal"], rope=w["rope_rgb"], empty=e,
                             truss=[e] * 5, set=w["set"], table=w["table"], **{g: w[g] for g, n, two, _ in GROUPS}))
            continue
        t = arenas.table(v)
        mp, lp, app = t["ring"][0], t["ring"][1], t["ring"][2]
        # Changed for new features (Ultimate No Mercy): (2026-11-20, revvisual; the user: "the arena/event logos on the wrestling
        # mat on Revenge Arenas, the logo is still glitching out, clipping") list A is No Mercy's own mat: the game moves its item
        # up and down when the ring bounces (a man landing), and nothing else of the ring. Revenge's mat is in the aprons' list
        # (slot 4, still), so a logo in list A bounced alone -- a few units under the mat (hidden) and back over it, a second or
        # more at a time, its matrix not the mat's (the renderer's layers by plane need the same matrix). The logo is now the
        # aprons' slot 5, right after the mat, with the mat's matrix: on its plane, drawn over it. List A as an arena with no
        # logo has it (EMPTY). The palettes are made in the same order as before (pal(lp) first): every file number the same.
        pl = pal(lp) if lp else WHITE
        rows.append(dict(name=name, v=v,
            logo=(E, E, WHITE),
            apron_mesh=rev["apron"][:5] + [LOGO if lp else rev["apron"][5]] + rev["apron"][6:],
            apron_cage=rev["apron_cage"][:5] + [LOGO_C if lp else rev["apron_cage"][5]] + rev["apron_cage"][6:],
            apron=[pal(app)] * 4 + [pal(mp), pl, WHITE, WHITE],
            floor_mesh=rev["floor"], floor=[pal(t["aisle"][0])] * 2,
            barricade=rev["barricade"], corner=rev["corner"], corner_pal=rev["corner_pal"], rope=rev["rope"], empty=E,
            truss=rev["truss"], set=[(F.one(mb), arenas.pic_pair(F, p)[1]) for mb, p in stage(v)], table=0,
            **{g: rev[g] for g, n, two, _ in GROUPS}))
    assert max(len(r["set"]) for r in rows) <= arenas.SET_MAX
    SN = max(len(r["set"]) for r in rows)
    L = ["// generated by gen/arenas_ring.py: the arcade arenas' ringside (arena_ring.c): the base venue's lists while one",
         "// of ours is built, per arena, gen/arenas.py's ARENAS order (meshes, palettes: the texture is the palette's file + 1)",
         "#define XRING_N %d" % len(rows), "#define XRING_EMPTY 0x%X" % E, "#define XRING_WHITE 0x%X" % WHITE,
         "#define XRING_TRUSS %d" % (1 if TRUSS else 0)]
    def arr2(cname, typ, per_row, note=""):
        k = len(per_row[0]); assert all(len(x) == k for x in per_row), cname
        L.append("static const %s %s[XRING_N][%d] = { %s };%s" % (typ, cname, k, ", ".join("{ %s }" % ", ".join("0x%X" % v for v in x) for x in per_row),
                 ("   // " + note) if note else ""))
    def pad(items, k, e): return list(items) + [e] * (k - len(items))
    arr2("xring_logo", "u16", [list(r["logo"]) for r in rows], "list A: mesh, the cage's, palette (0x80050040 / 44 / 48)")
    arr2("xring_apron_mesh", "u16", [r["apron_mesh"] for r in rows], "0x800507B4")
    arr2("xring_apron_mesh_cage", "u16", [r["apron_cage"] for r in rows], "0x800507C4 (the cage's +-250 ring)")
    arr2("xring_apron_pal", "u16", [r["apron"] for r in rows], "0x800507D4[v]")
    arr2("xring_floor_mesh", "u16", [r["floor_mesh"] for r in rows], "0x80050064: 2, then the cage's (0x80050068)")
    arr2("xring_floor_pal", "u16", [r["floor"] for r in rows], "0x8005006C")
    arr2("xring_barricade_mesh", "u16", [pad([m for m, p in r["barricade"]], 10, r["empty"]) for r in rows], "0x80050070")
    arr2("xring_barricade_pal", "u16", [pad([p for m, p in r["barricade"]], 10, WHITE) for r in rows], "0x80050084")
    arr2("xring_corner_mesh", "u16", [r["corner"] for r in rows], "0x800500A0: post, pad / wraps, turnbuckles; the cage's (0x800500A8)")
    arr2("xring_corner_pal", "u16", [r["corner_pal"] for r in rows], "0x800500B0")
    for name, k, two, _ in GROUPS:
        for r in rows: assert len(r[name]) <= k, (r["name"], name, len(r[name]))
        arr2("xring_%s_mesh" % name, "u16", [pad([m for m, p in r[name]], k, r["empty"]) for r in rows])
        arr2("xring_%s_pal_a" % name, "u16", [pad([p[0] for m, p in r[name]], k, WHITE) for r in rows])
        if two: arr2("xring_%s_pal_b" % name, "u16", [pad([p[1] for m, p in r[name]], k, WHITE) for r in rows])
    arr2("xring_upper_mesh", "u16", [[r["empty"]] for r in rows], "0x80050160[v] (count 0x800501B0[v] 1)")
    arr2("xring_upper_pal", "u16", [[WHITE] for r in rows], "0x80050188[v]")
    arr2("xring_truss_mesh", "u16", [r["truss"] for r in rows], "0x80050780 (TRUSS 0: none)")
    L.append("static const u8 xring_rope_rgb[XRING_N][3][4] = { %s };   // the ropes' colour set (R G B tables, a rope's 4 vertices)" %
             ", ".join("{ %s }" % ", ".join("{ %s }" % ", ".join("0x%02X" % c[k] for c in r["rope"]) for k in range(3)) for r in rows))
    L.append("static const u8 xring_table[XRING_N] = { %s };   // 1: No Mercy's announce table, steps, JR and King kept (the game's path)" %
             ", ".join(str(r["table"]) for r in rows))
    L.append("static const s16 xring_set_n[XRING_N] = { %s };   // the stage set (0x8005084C[v])" % ", ".join(str(len(r["set"])) for r in rows))
    L.append("static const u16 xring_set_mesh[XRING_N][%d] = { %s };   // the stage set (arena.c arena_set_mesh)" % (SN, ", ".join("{ %s }" % ", ".join("0x%X" % m for m, p in r["set"]) for r in rows)))
    L.append("static const u16 xring_set_pal[XRING_N][%d] = { %s };   // (arena.c arena_set_pal)" % (SN, ", ".join("{ %s }" % ", ".join("0x%X" % p for m, p in r["set"]) for r in rows)))
    open(OUT_H, "w").write("\n".join(L) + "\n")
    return rows

# ---- a look without the game (data/arenaport/ringside/preview.py draws these) -----------------------------------
def scene(v):
    """[(group, bias, Mesh, palette file (Revenge's) or picture name)] as the game would draw them (lists in order)"""
    t = arenas.table(v)
    out = []
    def P(p): return p
    for mb, p in stage(v): out.append(("set", 10000, Mesh_from_file(mb), p))
    cl = crowd_lists()
    for name, bias in (("stands", 20000), ("g1", 2500), ("g2", 2000), ("g3", 1500), ("g4", 500)):
        for m, a, b in cl[name]: out.append((name, bias, m, a))
    for m in floor(): out.append(("floor", 400, m, t["aisle"][0]))
    logo, ap = ring()
    for m, p in zip(ap[:5], [t["ring"][2]] * 4 + [t["ring"][0]]): out.append(("apron", 150, m, p))
    if t["ring"][1]: out.append(("logo", 0, logo, t["ring"][1]))
    for m, p in barricades(): out.append(("barricade", 0, m, p))
    for r in range(4):
        for m, p in zip(corner(), (0x2EE, 0x2F1, "white")): out.append(("corner", 0, m.rot(r), p))
    return out

def Mesh_from_file(b):
    d = b; sc = (d[0] & 0x7F) + 1; nv = d[1] & 0x7F; nt = d[2]; off = [s8(x) * 16 for x in d[4:7]]
    pu, pv = (d[7] >> 4) * 16, (d[7] & 15) * 16
    pts = [tuple((s8(d[8 + 8 * i + k]) + off[k]) * sc for k in range(3)) for i in range(nv)]
    uv = [(d[8 + 8 * i + 3] + pu, d[8 + 8 * i + 4] + pv) for i in range(nv)]
    rgb = [bytes(d[8 + 8 * i + 5:8 + 8 * i + 8]) for i in range(nv)]
    o = 8 + 8 * nv
    return Mesh(pts, uv, rgb, [tuple(d[o + 3 * i:o + 3 * i + 3]) for i in range(nt)], d[1] & 0x80, d[3])

if __name__ == "__main__":
    # a dry run: the files it would make (no pack written), the lists' lengths
    class FakeF:
        def __init__(self): self.data = []; self.by = {}; self.file0 = 0x7000
        def one(self, b):
            if b not in self.by: self.by[b] = self.file0 + len(self.data); self.data.append(b)
            return self.by[b]
        def pair(self, tex, pal):
            key = ("pair", tex, pal)
            if key not in self.by: self.by[key] = self.file0 + len(self.data); self.data += [pal, tex]
            return self.by[key] + 1, self.by[key]
    F = FakeF()
    OUT_H = os.path.join(ROOT, "data", "arenaport", "ringside", "arena_ring_files.h")
    rows = build(F)
    print("files: %d, %d bytes; header %s" % (len(F.data), sum(len(b) for b in F.data), OUT_H))
    for r in rows: print(r["name"], "stage set", len(r["set"]))
    for k, v in crowd_lists().items(): print(k, len(v))
