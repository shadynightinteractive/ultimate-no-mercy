#!/usr/bin/env python3
"""Every arena's ramp and stage playable where it stands, from its models (the user, 2026-11-02: "making every stage playable on the closeup ramp
without transition, even the Revenge and WM 2000 ones. Use collision detection based on the models on the ramp. Irish whips should crash opponents
into them"). data/stagecollide/COLLISION.md has what the game's wall engine allows; src/arena_ringstage.c runs what this makes.

From the arena's stage set (the parts the match draws, in the ring's world: gen/wmport.py's pack + the generated headers) and its crowd and barricades:
  - a grid of CELL units over the stage. A cell's floors are the up-facing triangles over it. The ground is flooded from the aisle's mouth, a step of
    at most STEP between neighbours: a slope climbs, a truss's top doesn't;
  - a cell is blocked if anything steep comes through a man's body over its ground, or a top stands low over it (a box);
  - what's walkable: reachable, not blocked, room for a man (a gap under the game's own 60 is no way through);
  - the aisle is the game's +-140 up to the stage's front: rails or not, nobody walks behind the crowd;
  - the walkable area's edge: the walls, with the walkable side on the right of each wall's way (the game's walls are one-sided: COLLISION.md section 1). One
    open path from the aisle's mouth round the stage and back (spliced into the barricade's chain) and closed loops round each prop (free-standing, as
    the game's rooms have: area 21's 156-159). A wall with something there: a crash (kind 1, state 0x15). An edge with nothing there, a drop: a teeter (kind 0x400);
  - a coarse map for the running game (GRID units): ground height, or not walkable: the man's lift, the CPU's way round.

Slow (minutes), so its results are kept in data/stagecollide/result/<n>.json, keyed by a hash of what went in; python arenas_collide.py makes them, gen/arenas_walls.py reads
them. python arenas_collide.py --pictures: top-down pictures of each in data/stagecollide/pics, with the walls."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp, ROOT
import hashlib, json, math, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.abspath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(HERE, "..", "src") + os.sep
RESULT = os.path.join(DEV, "data", "stagecollide", "result") + os.sep
PICS = os.path.join(DEV, "data", "stagecollide", "pics") + os.sep
NMF = nmp("/tmp/devbox-0/nmfiles/f/%04x.bin")
PACK = os.path.join(HERE, "..", "build", "wmport.bin")

CELL = 20
GRID = 40             # (the running game's map: u8 per cell)
STEP = 24             # (rise between neighbouring cells: RAW's ramp is 4 a cell)
BODY0, BODY1 = 12, 110  # (what a man runs into, above his feet: his legs and body. Higher reads RAW's arches, which curve in over his head, as solid: the user's whips would crash in the open)
LOW_TOP = 170         # (a top lower than this over the floor: a box, too high to step on)
UP = 0.70             # (a triangle's normal's y: a floor)
X0, X1 = -2400, 2400
AISLE_X, MOUTH_Z, DOOR_LINE = 140, 530, 800   # (the game's aisle: x +-140; the barricade's line; the old door across it)
ROOM = 3              # (cells: 60, the narrowest a man goes through)
SIMPLIFY = 14.0       # (units: a wall's edge may stray this far from the cells' staircase)
BUMP = 2              # (cells: within this of a blocked cell, a wall is a crash; past it the edge is a drop)
BEHIND = 90           # (units: a model this close behind a wall makes it a crash)
BACK_MARGIN = 60      # (units: how far behind the set's last model the floor still counts)

GAME_NAMES = ["RAW IS WAR", "NO MERCY", "SMACKDOWN", "KING OF THE RING", "SUMMERSLAM", "SURVIVOR SERIES", "ROYAL RUMBLE", "WRESTLEMANIA", "BACKLASH", "ARMAGEDDON"]

# ---- the models ---------------------------------------------------------------------------------------------------
def nums(s): return [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", s)]
def s8(b): return b - 256 if b > 127 else b
def mesh(d):
    sc = (d[0] & 0x7F) + 1; nv = d[1] & 0x7F; nt = d[2]; off = [s8(x) * 16 for x in d[4:7]]
    pts = [tuple((s8(d[8 + 8 * i + k]) + off[k]) * sc for k in range(3)) for i in range(nv)]
    o = 8 + 8 * nv
    return pts, [tuple(d[o + 3 * i:o + 3 * i + 3]) for i in range(nt)]

class Models:
    def __init__(self):
        self.pack = open(PACK, "rb").read()
        wf = open(SRC + "wmport_files.h").read()
        m = re.search(r"wmp_files\[WMP_FILES_N\]\[2\] = \{.*?\n(.*?)\n\};", wf, re.S)
        self.files = [(int(a, 0), int(b, 0)) for a, b in re.findall(r"\{\s*(\d+|0x[0-9A-Fa-f]+),\s*(\d+|0x[0-9A-Fa-f]+)\s*\}", m.group(1))]
    def data(self, i):
        if i >= 0x5000:
            o, n = self.files[i - 0x5000]; return self.pack[o:o + n]
        return open(NMF % i, "rb").read()
    def tris(self, ids, dz=0, zmin=None):
        out = []
        for i in ids:
            pts, tt = mesh(self.data(i))
            P = [(p[0], p[1], p[2] + dz) for p in pts]
            for a, b, c in tt:
                if max(a, b, c) >= len(P): continue
                if zmin is not None and max(P[a][2], P[b][2], P[c][2]) < zmin: continue
                out.append((P[a], P[b], P[c]))
        return out

def arena_list():
    """(n, name, stage set z, [set meshes], [crowd meshes])"""
    sys.path.insert(0, HERE)
    import arenas as _ar
    def main_u16(o, n): return list(struct.unpack(">%dH" % n, _ar.main_data(0x80050000 + o, 2 * n)))
    game_crowd = main_u16(0x70, 10) + main_u16(0xF8, 15) + main_u16(0x1BC, 3) + main_u16(0x1DC, 9) + main_u16(0x220, 22) + main_u16(0x2AC, 22)
    out = []
    t = open(SRC + "arena_raw_files.h").read()
    out.append((0, "RAW IS WAR", 1500, nums(re.search(r"xraw_set_mesh\[XRAW_SET_N\] = \{(.*?)\}", t, re.S).group(1)), game_crowd))
    t = open(SRC + "arena_stage_files.h").read()
    n_ = nums(re.search(r"xstage_n\[[^\]]*\] = \{(.*?)\}", t, re.S).group(1))
    for v in range(1, 10):
        out.append((v, GAME_NAMES[v], 1500, nums(re.search(r"xstage%d_mesh\[[^\]]*\] = \{(.*?)\}" % v, t, re.S).group(1))[:n_[v]], game_crowd))
    t = open(SRC + "arena_ring_files.h").read()
    def ring(name, k):
        mm = re.search(r"xring_%s\[XRING_N\]\[\d+\] = \{(.*?)\};" % name, t, re.S)
        return nums(re.findall(r"\{([^{}]*)\}", mm.group(1))[k])
    n_ = nums(re.search(r"xring_set_n\[XRING_N\] = \{(.*?)\}", t, re.S).group(1))
    names = re.findall(r'"([^"]+)"', re.search(r"xarena_names\[XARENA_N\] = \{(.*?)\}", open(SRC + "arena_files.h").read(), re.S).group(1))
    for k in range(len(names)):
        crowd = sum((ring(x, k) for x in ("barricade_mesh", "stands_mesh", "g1_mesh", "g2_mesh", "g3_mesh", "g4_mesh")), [])
        out.append((10 + k, names[k], 0, ring("set_mesh", k)[:n_[k]], crowd))
    return out

# ---- geometry -----------------------------------------------------------------------------------------------------
def norm(a, b, c):
    ux, uy, uz = b[0]-a[0], b[1]-a[1], b[2]-a[2]; vx, vy, vz = c[0]-a[0], c[1]-a[1], c[2]-a[2]
    nx, ny, nz = uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx
    l = math.sqrt(nx*nx+ny*ny+nz*nz) or 1.0
    return nx/l, ny/l, nz/l

def cells_of(poly, zmin, nx, nz):
    """the cells an x-z polygon passes through"""
    out = set()
    zs = [p[1] for p in poly]
    k0, k1 = int((min(zs) - zmin) // CELL), int((max(zs) - zmin) // CELL)
    n = len(poly)
    for k in range(max(0, k0), min(nz - 1, k1) + 1):
        zlo, zhi = zmin + k * CELL, zmin + (k + 1) * CELL
        xs = []
        for j in range(n):
            a, b = poly[j], poly[(j + 1) % n]
            if (a[1] < zlo and b[1] < zlo) or (a[1] > zhi and b[1] > zhi): continue
            if a[1] == b[1]: xs += [a[0], b[0]]; continue
            t0 = max(0.0, min(1.0, (zlo - a[1]) / (b[1] - a[1])))
            t1 = max(0.0, min(1.0, (zhi - a[1]) / (b[1] - a[1])))
            xs += [a[0] + t0 * (b[0] - a[0]), a[0] + t1 * (b[0] - a[0])]
        if not xs: continue
        i0, i1 = int((min(xs) - X0) // CELL), int((max(xs) - X0) // CELL)
        for i in range(max(0, i0), min(nx - 1, i1) + 1): out.add((i, k))
    return out

def slab(tri, y0, y1):
    p = list(tri)
    for lim, keep in ((y0, lambda v: v >= y0), (y1, lambda v: v <= y1)):
        q = []
        for i in range(len(p)):
            a, b = p[i], p[(i + 1) % len(p)]
            ka, kb = keep(a[1]), keep(b[1])
            if ka: q.append(a)
            if ka != kb:
                t = (lim - a[1]) / (b[1] - a[1])
                q.append(tuple(a[j] + t * (b[j] - a[j]) for j in range(3)))
        p = q
        if not p: return []
    return [(v[0], v[2]) for v in p]

def height_at(tri, x, z):
    a, b, c = tri
    d = (b[2]-c[2])*(a[0]-c[0]) + (c[0]-b[0])*(a[2]-c[2])
    if abs(d) < 1e-6: return None
    l0 = ((b[2]-c[2])*(x-c[0]) + (c[0]-b[0])*(z-c[2])) / d
    l1 = ((c[2]-a[2])*(x-c[0]) + (a[0]-c[0])*(z-c[2])) / d
    l2 = 1 - l0 - l1
    if l0 < -1e-3 or l1 < -1e-3 or l2 < -1e-3:
        # (the cell's centre just off the triangle: its nearest place on it)
        l0, l1, l2 = max(0.0, l0), max(0.0, l1), max(0.0, l2); s = l0 + l1 + l2
        l0, l1, l2 = l0 / s, l1 / s, l2 / s
    return l0*a[1] + l1*b[1] + l2*c[1]

# ---- the solve ----------------------------------------------------------------------------------------------------
def flood(start, ok):
    seen = {start}; todo = [start]
    while todo:
        c = todo.pop()
        for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            e = (c[0] + d[0], c[1] + d[1])
            if e not in seen and ok(e): seen.add(e); todo.append(e)
    return seen

def solve(set_tris, crowd_tris):
    zmax = max(p[2] for t in set_tris for p in t) + 2 * CELL
    zmin = MOUTH_Z
    nx, nz = (X1 - X0) // CELL, int((zmax - zmin) // CELL) + 1
    floors, steep = {}, []
    for t in set_tris:
        n = norm(*t)
        if abs(n[1]) >= UP:
            for c in cells_of([(v[0], v[2]) for v in t], zmin, nx, nz):
                h = height_at(t, X0 + (c[0] + .5) * CELL, zmin + (c[1] + .5) * CELL)
                if h is not None: floors.setdefault(c, []).append(h)
        else: steep.append(t)
    # the aisle: the game's own floor, whether the set's models have it or the ring's lists do: up the middle at its height, wherever the models have
    # no floor, till they've had one for a while
    cx = (0 - X0) // CELL; half = AISLE_X // CELL
    y, run = 0.0, 0
    for k in range(nz):
        have = [f for f in floors.get((cx, k), []) if abs(f - y) <= STEP]
        if have:
            y = min(have, key=lambda f: abs(f - y)); run += 1
            if run > 40: break
        else:
            run = 0
            for i in range(cx - half, cx + half): floors.setdefault((i, k), []).append(y)
    # the ground
    start = (cx, 0)
    ground = {start: 0.0}; todo = [start]
    while todo:
        c = todo.pop(); h = ground[c]
        for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            e = (c[0] + d[0], c[1] + d[1])
            if e in ground or e not in floors: continue
            near = [f for f in floors[e] if abs(f - h) <= STEP]
            if near:
                ground[e] = min(near, key=lambda f: abs(f - h)); todo.append(e)
    # what's in a man's way
    blocked = set()
    for t in steep:
        fp = cells_of([(v[0], v[2]) for v in t], zmin, nx, nz)
        for c in fp:
            if c not in ground: continue
            g = ground[c]
            cut = slab(t, g + BODY0, g + BODY1)
            if cut and c in cells_of(cut, zmin, nx, nz): blocked.add(c)
    for c, g in ground.items():
        for f in floors.get(c, []):
            if g + STEP < f < g + LOW_TOP: blocked.add(c)
    # the crowd and barricades: nobody walks under them, however high they hang (QA 2026-11-02 crash_survivor_series: the men in among the stands). Their whole footprint
    for t in crowd_tris:
        for c in cells_of([(v[0], v[2]) for v in t], zmin, nx, nz):
            if c in ground: blocked.add(c)
    # (the aisle between the barricades, to the old door's line: the game's, whatever stands by it)
    for k_ in range((DOOR_LINE - zmin) // CELL + 1):
        for i_ in range(cx - half, cx + half):
            blocked.discard((i_, k_))
            ground.setdefault((i_, k_), 0.0)
    # the aisle's own sides: up to the stage's front (where the rails, or the crowd's, end and it opens), nobody off it
    def x_of(i): return X0 + i * CELL
    front = DOOR_LINE
    lo, hi = cx - half - 1, cx + half
    gap = 0
    for k in range((DOOR_LINE - zmin) // CELL, nz):
        side = [(lo - j, k) for j in range(0, 3)] + [(hi + j, k) for j in range(0, 3)]
        closed_l = any(c in blocked or c not in ground for c in side[:3])
        closed_r = any(c in blocked or c not in ground for c in side[3:])
        if closed_l and closed_r: gap = 0; front = zmin + (k + 1) * CELL
        else:
            gap += 1
            if gap > 4: break
    # (the set's last model is the stage's back: a floor that goes on behind it is a picture fading to black, nothing to stand on: SUPERBRAWL's)
    zback = max([max(v[2] for v in t) for t in steep] or [zmax]) + BACK_MARGIN
    free = {c for c in ground if c not in blocked and zmin + (c[1] + 1) * CELL <= zback and (zmin + c[1] * CELL >= front or x_of(c[0]) >= -AISLE_X and x_of(c[0]) + CELL <= AISLE_X)}
    # room for a man: he goes where a ROOM x ROOM block of free cells goes, block by block: the blocks that reach the aisle's mouth, and only they, are what's walkable. (Only the free
    # cells in some such block wasn't enough: two blocks that touch at a corner joined up places no man gets between: SURVIVOR SERIES WM 2000's skull, 40 from the aisle's side.)
    fit = {(i, k) for i, k in free if all((i + a_, k + b_) in free for a_ in range(ROOM) for b_ in range(ROOM))}
    seeds = [c for c in fit if c[1] == 0]
    seen_fit = set()
    for c in seeds:
        if c not in seen_fit: seen_fit |= flood(c, lambda e: e in fit)
    walk = {(i + a_, k + b_) for i, k in seen_fit for a_ in range(ROOM) for b_ in range(ROOM)}
    if start not in walk: walk = flood(start, lambda e: e in free)
    return dict(zmin=zmin, nx=nx, nz=nz, ground=ground, blocked=blocked, walk=walk, front=front, floors=floors, steep=steep)

# ---- the walls ----------------------------------------------------------------------------------------------------
def edges_of(walk, zmin):
    """the walkable area's edge, each piece with the walkable side on its right: {from: [to, ...]} (a travel (dx, dz) has its normal (dz, -dx))"""
    out = {}
    def add(a, b): out.setdefault(a, []).append(b)
    for i, k in walk:
        x, z = X0 + i * CELL, zmin + k * CELL
        if (i, k - 1) not in walk and k > 0: add((x + CELL, z), (x, z))            # (the bottom)
        if (i, k + 1) not in walk: add((x, z + CELL), (x + CELL, z + CELL))        # (the top)
        if (i - 1, k) not in walk: add((x, z), (x, z + CELL))                      # (the left)
        if (i + 1, k) not in walk: add((x + CELL, z + CELL), (x + CELL, z))        # (the right)
    return out

def trace(edges, zmin):
    """-> the open path from the aisle's mouth (-140, 530) to (140, 530), closed loops"""
    def take(a, came=None):
        opts = edges.get(a)
        if not opts: return None
        if len(opts) == 1 or came is None: return opts.pop(0)
        # (a corner two cells meet at: keep to the walkable side -- the sharpest right turn)
        dx, dz = a[0] - came[0], a[1] - came[1]
        def turn(b):
            ex, ez = b[0] - a[0], b[1] - a[1]
            return dx * ez - dz * ex          # (< 0: right)
        b = min(opts, key=turn); opts.remove(b); return b
    path = [(-AISLE_X, zmin)]
    came = None; a = path[0]
    while True:
        b = take(a, came)
        assert b is not None, "the edge stops at %s" % (a,)
        path.append(b); came, a = a, b
        if a == (AISLE_X, zmin): break
        assert len(path) < 100000
    loops = []
    for a in sorted(edges):
        while edges.get(a):
            lp = [a]; came = None; c = a
            while True:
                b = take(c, came)
                assert b is not None, "a loop stops at %s" % (c,)
                if b == a: break
                lp.append(b); came, c = c, b
            loops.append(lp)
    return path, loops

def dp(pts, tol):
    if len(pts) < 3: return list(pts)
    a, b = pts[0], pts[-1]
    dx, dz = b[0] - a[0], b[1] - a[1]; l = math.hypot(dx, dz)
    far, at = -1.0, 0
    for i in range(1, len(pts) - 1):
        p = pts[i]
        d = abs((p[0] - a[0]) * dz - (p[1] - a[1]) * dx) / l if l else math.hypot(p[0] - a[0], p[1] - a[1])
        if d > far: far, at = d, i
    if far <= tol: return [a, b]
    return dp(pts[:at + 1], tol)[:-1] + dp(pts[at:], tol)

def simplify_loop(lp, tol):
    # (cut at the two points furthest apart)
    a = max(range(len(lp)), key=lambda i: (lp[i][0] - lp[0][0]) ** 2 + (lp[i][1] - lp[0][1]) ** 2)
    one = dp(lp[:a + 1], tol); two = dp(lp[a:] + [lp[0]], tol)
    return one[:-1] + two[:-1]

def area2(lp):
    return sum(lp[i][0] * lp[(i + 1) % len(lp)][1] - lp[(i + 1) % len(lp)][0] * lp[i][1] for i in range(len(lp)))

def wall_kind(a, b, r):
    """what a wall is: 1 a crash (a model behind it, in a man's way), 0 a drop (the floor's edge, nothing there). A model off the ground counts: the logo board at the back of
    MONDAY NITRO's stage stands past the floor's edge"""
    blocked, ground, zmin = r["blocked"], r["ground"], r["zmin"]
    dx, dz = b[0] - a[0], b[1] - a[1]; L = math.hypot(dx, dz) or 1.0
    # behind the wall: left of its way (its normal (dz, -dx) points to the walkable side)
    bx, bz = -dz / L, dx / L
    n = max(1, int(L // CELL))
    hits = 0
    for j in range(n):
        t = (j + .5) / n
        x, z = a[0] + t * dx, a[1] + t * dz
        c0 = (int((x - bx * CELL * .7 - X0) // CELL), int((z - bz * CELL * .7 - zmin) // CELL))     # (his side of it: the ground he's on)
        g = ground.get(c0, 0.0)
        found = False
        for s in range(1, BUMP + 2):
            c = (int((x + bx * s * CELL * .7 - X0) // CELL), int((z + bz * s * CELL * .7 - zmin) // CELL))
            if c in blocked: found = True; break
        if not found:
            px, pz = x + bx * BEHIND / 2.0, z + bz * BEHIND / 2.0
            for t3 in r["steep"]:
                xs = [v[0] for v in t3]; zs_ = [v[2] for v in t3]
                if max(xs) < px - BEHIND or min(xs) > px + BEHIND or max(zs_) < pz - BEHIND or min(zs_) > pz + BEHIND: continue
                cut = slab(t3, g + BODY0, g + BODY1)
                if not cut: continue
                # (its part in a man's way close behind this bit of the wall, not in front of it)
                for q in cut:
                    d = (q[0] - x) * bx + (q[1] - z) * bz
                    if 0 <= d <= BEHIND and abs((q[0] - x) * bz - (q[1] - z) * bx) <= max(CELL * 2, BEHIND):
                        found = True; break
                if found: break
        if found: hits += 1
    return 1 if hits * 3 >= n else 0

def segs_of(pts):
    return [(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]

def result_of(set_tris, crowd_tris):
    r = solve(set_tris, crowd_tris)
    path, loops = trace(edges_of(r["walk"], r["zmin"]), r["zmin"])
    path = dp(path, SIMPLIFY)
    props = []
    for lp in loops:
        s = simplify_loop(lp, SIMPLIFY)
        if len(s) < 3: s = lp          # (too small to simplify away: as it is)
        assert area2(s) != 0
        props.append(s)
    out_walls = [dict(a=a, b=b, crash=wall_kind(a, b, r)) for a, b in segs_of(path)]
    prop_walls = [[dict(a=s[i], b=s[(i + 1) % len(s)], crash=1) for i in range(len(s))] for s in props]
    # the running game's map
    W = r["walk"]; zmin = r["zmin"]
    xs = [X0 + i * CELL for i, k in W]; zs = [zmin + k * CELL for i, k in W]
    gx0 = (min(xs) // GRID) * GRID; gx1 = (max(xs) // GRID + 1) * GRID
    gz0 = (min(zs) // GRID) * GRID; gz1 = (max(zs) // GRID + 1) * GRID
    gw, gh = (gx1 - gx0) // GRID, (gz1 - gz0) // GRID
    grid = []
    for gk in range(gh):
        row = []
        for gi in range(gw):
            cs = [((gx0 + gi * GRID + a * CELL - X0) // CELL, (gz0 + gk * GRID + b * CELL - zmin) // CELL) for a in range(GRID // CELL) for b in range(GRID // CELL)]
            hs = [r["ground"][c] for c in cs if c in W]
            row.append(None if len(hs) * 2 < len(cs) else round(sum(hs) / len(hs)))
        grid.append(row)
    front = min(r["front"], max(zs) + CELL - 100)      # (an aisle that ends at a doorway, nothing behind: SUMMERSLAM WM 2000)
    # the crowd's rows by the aisle: how far back they come and how high: the stage's camera is to look over them (src/arena_ringstage.c cam_over_crowd)
    rows = [t for t in crowd_tris if max(v[1] for v in t) <= 300 and max(v[2] for v in t) > DOOR_LINE and min(abs(v[0]) for v in t) <= 1200]
    crowd_z = max([v[2] for t in rows for v in t] or [0]); crowd_y = max([v[1] for t in rows for v in t] or [0])
    return dict(path=out_walls, props=prop_walls, front=front, grid=dict(x0=gx0, z0=gz0, w=gw, h=gh, cells=grid),
                bbox=[min(xs), max(xs) + CELL, min(zs), max(zs) + CELL], crowd_z=crowd_z, crowd_y=crowd_y, ymax=max(r["ground"][c] for c in W), ymin=min(r["ground"][c] for c in W),
                n_walls=len(out_walls) + sum(len(p) for p in prop_walls), walk_cells=len(W), _r=r)

def check(res, name):
    """COLLISION.md's risks, asserted: every wall's normal to the walkable side; no wall with no length; the gap rule; the quadrant"""
    for w in res["path"] + [w for p in res["props"] for w in p]:
        assert w["a"] != w["b"], name
    probs = []
    for w in res["path"] + [w for p in res["props"] for w in p]:
        for p in (w["a"], w["b"]):
            if abs(p[0]) + 60 > p[1]: probs.append(p)
    return probs

# ---- run ----------------------------------------------------------------------------------------------------------
def key_of(M, n, dz, ids, crowd):
    h = hashlib.sha1(("%d %d %d %d %d %d %d %d" % (CELL, GRID, STEP, BODY0, BODY1, LOW_TOP, ROOM, int(SIMPLIFY))).encode())
    h.update(open(__file__, "rb").read())
    for i in ids + crowd: h.update(M.data(i))
    h.update(str(dz).encode())
    return h.hexdigest()

def picture(n, name, res):
    from PIL import Image, ImageDraw
    r = res["_r"]; zmin, nx, nz = r["zmin"], r["nx"], r["nz"]
    S = 2
    im = Image.new("RGB", (nx * S, nz * S), (15, 15, 25)); d = ImageDraw.Draw(im)
    def P(x, z): return (int((X1 - x) / CELL * S), int((nz * CELL - (z - zmin)) / CELL * S))
    for c, g in r["ground"].items():
        col = (40, 40, 50)
        if c in r["walk"]: v = max(0, min(200, int(g))); col = (30 + v // 3, 90 + v // 2, 40 + v)
        x, z = X0 + c[0] * CELL, zmin + c[1] * CELL
        d.rectangle([P(x + CELL, z + CELL), P(x, z)], fill=col)
    for c in r["blocked"]:
        x, z = X0 + c[0] * CELL, zmin + c[1] * CELL
        d.rectangle([P(x + CELL, z + CELL), P(x, z)], fill=(110, 40, 40))
    for w in res["path"]:
        d.line([P(*w["a"]), P(*w["b"])], fill=(255, 255, 0) if w["crash"] else (0, 200, 255), width=2)
    for p in res["props"]:
        for w in p: d.line([P(*w["a"]), P(*w["b"])], fill=(255, 90, 255), width=1)
    d.text((4, 2), "%d %s  walls %d  props %d  y %d..%d front %d" % (n, name, res["n_walls"], len(res["props"]), res["ymin"], res["ymax"], res["front"]), fill=(255, 255, 255))
    return im

def main():
    pics = "--pictures" in sys.argv
    which = [int(a) for a in sys.argv[1:] if a.isdigit()]
    os.makedirs(RESULT, exist_ok=True); os.makedirs(PICS, exist_ok=True)
    M = Models()
    tiles = []
    for n, name, dz, ids, crowd in arena_list():
        if which and n not in which: continue
        key = key_of(M, n, dz, ids, crowd)
        path = RESULT + "%d.json" % n
        if not pics and os.path.exists(path) and json.load(open(path)).get("key") == key:
            print("%2d %-26s up to date" % (n, name)); continue
        st = M.tris(ids, dz); cr = M.tris(crowd, 0, zmin=300)
        res = result_of(st, cr)
        probs = check(res, name)
        keep = {k: v for k, v in res.items() if k != "_r"}
        keep["key"] = key; keep["name"] = name
        json.dump(keep, open(path, "w"))
        print("%2d %-26s walk %5d x %5d..%5d z %5d..%5d y %3d..%3d front %5d walls %3d (path %3d, %2d props) grid %dx%d off-q0 %d" % (n, name, res["walk_cells"], res["bbox"][0], res["bbox"][1],
              res["bbox"][2], res["bbox"][3], res["ymin"], res["ymax"], res["front"], res["n_walls"], len(res["path"]), len(res["props"]), res["grid"]["w"], res["grid"]["h"], len(probs)), flush=True)
        if pics:
            im = picture(n, name, res); im.save(PICS + "%d.png" % n); tiles.append(im)
    if pics and tiles:
        from PIL import Image
        cols = 6; w = max(i.size[0] for i in tiles); h = max(i.size[1] for i in tiles)
        sc = 0.7
        sheet = Image.new("RGB", (int(cols * w * sc), int(((len(tiles) + cols - 1) // cols) * h * sc)), (0, 0, 0))
        for i, t in enumerate(tiles):
            t2 = t.resize((int(t.size[0] * sc), int(t.size[1] * sc)))
            sheet.paste(t2, (int((i % cols) * w * sc), int((i // cols) * h * sc + (h - t.size[1]) * sc)))
        sheet.save(PICS + "sheet.png"); print(sheet.size)

if __name__ == "__main__":
    main()
