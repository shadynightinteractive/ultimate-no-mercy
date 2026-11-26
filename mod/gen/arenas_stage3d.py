"""WCW/nWo Revenge's real 3D stages (data/arenaport/STAGE3D.md) in place of its flat stage set: in the entrances' close
shots (gen/arenas_entrance.py) and at the end of the arena in the match (gen/arenas.py / gen/arenas_ring.py).

Revenge has two stages per arena: the flat set of one-quad pictures (REV_ARENAS.md: what it draws in the match, and
what gen/arenas.py brought over first) and a 3D one -- the venue menu's backdrop scene, which stays loaded and is drawn
again in its entrance close-ups (Nitro: geometry file 0x3E, pictures 0x3F; extracted to data/arenaport/stage3d/<name>/
meshes.json by its extract.py, world-space points of the set, texture coordinates in repeats of the picture, the
"normal" bytes and the vertex alpha -- Revenge's only light: TEXEL0 x SHADE_ALPHA). Revenge draws it at x0.3 (the
same size as the flat set: the gate 360 tall), in list order, no depth buffer, back faces culled.

Made here, for an arena in STAGE3D:
  - each model a No Mercy mesh (the format of gen/arenas_ring.py's Mesh): the points x0.3 and moved, on the grid of
    the mesh's scale; the texture coordinates in texels, moved by whole repeats (two for a mirrored picture) to start
    at 0; the colour the vertex alpha (x CLOSE_GREY / 255 in the close shots, where the entrance's light adds to it);
  - its pictures: the picture file's own (CI4, RGBA5551 palette), the header's width / height filled in, bytes 4 / 5
    the picture's repeat (0 wrap, 1 mirror: only the floor's).
The close scene takes the 29 models as they are (in Revenge's order, room for 392), placed so where Revenge's
Superstars stop to pose (0.3 x set + (16, 11, -57) = (+-49, 0, -511)) is the close scene's origin, plus a floor
piece in front reaching toward the camera (No Mercy's close camera is further back than Revenge's). The match's
stage set (15 places) takes them merged by picture (RING_PARTS / ring_groups), at 0.3 x set + (0, 0, ring_z): its
floor's front edge where the flat set's aisle ends (its rails to z 1230), the old aisle's floor kept in front (AISLE)."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp, ROOT
import json, math, os, re, struct

STAGE3D = {0: ("nitro", 0x3F), 4: ("havoc", 0x47), 3: ("bash", 0x3D),
           1: ("souledout", 0x43), 2: ("superbrawl", 0x45), 5: ("starrcade", 0x41)}   # Revenge's arena -> (its scene, its picture file)
SCALE = 0.3
AISLE_END = 1230.0                  # the match: the 3D floor's front edge where the old set's aisle ends (Revenge's aisle
                                    # rails 0x319-0x31E: x +-170, z 530..1230) -- the user: "the closeup stage needs to
                                    # start at the end of that aisle" (the aisle as it was)
# Changed for new features (Ultimate No Mercy): the stagescale agent, 2026-11-20. The close scene is placed as Revenge's own
# entrance places the set (data/arenaport/STAGE3D.md 5; read again per venue from Revenge's entrances in the rig, all six
# alike: world = set x 0.3 turned 2 degrees about y + (16.2, 11.5, -57.4)), its Superstars' pose spot (-511.5) moved onto No
# Mercy's (-515: the vanilla game's close shots, its entrant at z -530 / -500 in steps 5 / 11, the camera at (0, 75, -998)
# for the pose, (0, 196, -1280) to open -- Revenge's (0, 200, -1280)). The old placement (CLOSE_Z, 454 on z) put Revenge's
# pose spot at the origin, as if No Mercy's Superstar posed there: the set stood 515 too far behind him. Revenge's 1-degree
# tilt and its +11.5 are left out: the floor stays at y 0 under No Mercy's Superstar all the way (Revenge draws without a
# depth buffer, its Superstars at y 0 on a floor 19 higher).
CLOSE_C, CLOSE_S = 0.2998172640, 0.0104698490    # (0.3 x cos 2 degrees, 0.3 x sin 2 degrees)
CLOSE_TX, CLOSE_TZ = 16.2, -60.9                 # (Revenge's 16.2, -57.4 + (-515 - -511.5))
CLOSE_NEAR = -1250.0                # the close shots' floor reaches this far toward the camera (Revenge's ends at -778; No
                                    # Mercy's entrant walks on to z -1035 as the camera pulls back)
CLOSE_GREY = 255                    # (Revenge's light: the picture x the vertex alpha, white -- the entrance's colour taken
                                    #  off in its arenas, mod/src/media.c; it was 200 under No Mercy's tinted light)

def close_pt(p):
    """a set point where the close scene has it (Revenge's entrance placement, above)"""
    x, y, z = p
    return (CLOSE_C * x + CLOSE_S * z + CLOSE_TX, SCALE * y + 0.0, CLOSE_C * z - CLOSE_S * x + CLOSE_TZ)

def place(p, move):
    """a set point placed: move a function (the close scene's close_pt) or the match's (0, 0, z) at SCALE"""
    return move(p) if callable(move) else tuple(SCALE * c + d for c, d in zip(p, move))
# the match's set: Revenge's models merged by picture (each within 127 points / 127 triangles), in its drawing order --
# Nitro's as first made (QA'd); the others by ring_groups
RING_PARTS = {0: [[0], [1, 3], [2, 4], [5, 6, 7, 8, 9, 10], [11, 12, 13, 14, 15, 16], [17], [18],
                  [19, 20, 21], [24, 25, 26], [22, 27], [23, 28]]}
RING_SKIP = {}                      # (none: Bash's small plant panel, model 15, left out while the set had 15 places, is back)
# the old set's aisle floor, under the stage (the user: "the aisle retained"): Nitro's / Havoc's light beams (x +-170,
# z 500..1240), Bash's sand (x +-550, z 540..1340), Souled Out's / Starrcade's light beams, SuperBrawl's red carpet
# (0x382: x +-170, z 500..1240) -- one mesh, the arena's picture
AISLE = {0: [0x314], 4: [0x314], 3: [0x3B2, 0x3B3], 1: [0x314], 2: [0x382], 5: [0x314]}

def ring_z(v):
    """where the set goes in the match: 0.3 x set + (0, 0, this) -- its floor's front edge on AISLE_END"""
    return AISLE_END - SCALE * min(p[2] for p in models(v)[0]["verts"])

def ring_groups(v):
    """the models merged by picture, in order of their picture's first use, each part within 127 points / 127 triangles
    (the game's mesh loader, func_8000C398, reads the triangle count as a signed byte: 128 on is "none", and the
    build crashed -- Havoc's six tombstones, 204, QA entrance_havoc 2026-10-31 13:39)"""
    ms = models(v); skip = RING_SKIP.get(v, [])
    groups = []                 # [[texture, [models], points, triangles]]
    for k, m in enumerate(ms):
        if k in skip: continue
        nv, nt = len(m["verts"]), len(m["tris"])
        g = next((g for g in groups if g[0] == m["texture"] and g[2] + nv <= 127 and g[3] + nt <= 127), None)
        if g: g[1].append(k); g[2] += nv; g[3] += nt
        else: groups.append([m["texture"], [k], nv, nt])
    return [g[1] for g in groups]

def models(v):
    """the scene's models its plain nodes draw (data/arenaport/stage3d/<name>/meshes.json), then the ones it draws through
    its scene table's other records (EXTRA): Bash at the Beach's"""
    name, _ = STAGE3D[v]
    ms = json.load(open(os.path.join(ROOT, "data", "arenaport", "stage3d", name, "meshes.json")))["meshes"]
    return ms + [extra_model(v, k) for k in EXTRA.get(v, ())]

# Models no plain node draws (data/arenaport/STAGE3D.md 4 / 7: reached through the scene table's +6 / +16 fields). Bash
# at the Beach (scene 3, geometry 0x3C): its picture board under the hut -- a 2 x 2 wall of 64 x 32 CI8 photos on the
# diagonal by the boardwalk (x 500..1348, z -848..0, y 0..900: models 13-16, the slideshow's first frame -- 17-20, its
# second, the same four places: the BASH AT THE BEACH logo; the stage set and the close scene don't animate), and the
# beach umbrella (model 0) on the other side. Their points are the set's own (the nodes at the origin, as all of Bash's).
# The board is the arena's screen (tron.c: the entrance video / the hard camera on it; arena_screen_files.h).
EXTRA = {3: [0, 13, 14, 15, 16]}
BOARD = {3: [13, 14, 15, 16]}                   # (the screen's tiles: scene models)

# Souled Out's box (scene 1, model 1: a box 1200 wide, 1125 tall, 250 deep, tapering back to the wall -- its sides and
# top only): Revenge's entrance closes its front with the yellow "wcw nWo SOULED OUT" board (the flat set's picture
# 0x359 / palette 0x358, which the flat set stands at the aisle's end in the match) and opens it, glowing, for the pose
# (rig: data/arenaport/rev3/sheet_seq1.png). Here its front closed: a quad of that picture in the box's mouth (set z 0),
# facing the ring, in the match's set and the close scene.
DOOR = {1: (0x358, ((-600, 0, 0), (600, 0, 0), (600, 1125, 0), (-600, 1125, 0)))}

def door_part(v, move, grey):
    """(mesh file, picture) of the arena's DOOR: the board's picture across the quad (x +600 its left edge as seen
    from the ring, the top its top row)"""
    from arenas_ring import Mesh
    pal, q = DOOR[v]
    pts = [place(p, move) for p in q]
    sc = max(1, math.ceil(max(max(p[k] for p in pts) - min(p[k] for p in pts) for k in range(3)) / 230.0))
    pts = [tuple(int(round(c / sc)) * sc for c in p) for p in pts]
    uv = [(64, 64), (0, 64), (0, 0), (64, 0)]
    # (facing -z: the winding of the box's own faces, outward = (b - a) x (c - a))
    return Mesh(pts, uv, [bytes([grey] * 3)] * 4, [(0, 2, 1), (0, 3, 2)]).file(), pal

def fold(tex):
    """tron.c's picture fold (TRON_ALL.md 1): the texels its material loads (2 KB at most), sum and rotate-xor of words"""
    b = (bytes(tex[8:]) + bytes(2048))[:min(2048, len(tex) - 8)]
    a = x = 0
    for (w,) in struct.iter_unpack(">I", b):
        a = (a + w) & 0xFFFFFFFF; x = (((x << 1) | (x >> 31)) & 0xFFFFFFFF) ^ w
    return a, x

def screens(v):
    """the arena's screen (BOARD) in the match's set and the close scene: {scene: ((x0, x1, y0, y1), [sig per tile])} --
    the tiles' points as their mesh files have them (each tile a part of its own in both: its own picture)"""
    from arenas_ring import Mesh_from_file
    if v not in BOARD: return {}
    ms = models(v); at = [i for i, m in enumerate(ms) if m.get("node") == -1 and m["model"] in BOARD[v]]
    out = {}
    for scene, move, grey in (("match", (0.0, 0.0, ring_z(v)), 255), ("close", close_pt, CLOSE_GREY)):
        pts, sigs = [], []
        for k in at:
            mb, pic = mesh(v, [k], move, grey)
            pts += Mesh_from_file(mb).pts; sigs.append(fold(pic[1]))
        out[scene] = ((min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts), max(p[1] for p in pts)), sigs)
    return out

def extra_model(v, k):
    """scene model k as a meshes.json mesh (its points as they are: the node at the origin)"""
    S = json.load(open(os.path.join(ROOT, "data", "arenaport", "stage3d", "scenes.json")))["scenes"][v]
    m = S["models"][k]
    geo = entries(open(nmp("/tmp/devbox-0/rev_files/%04x.bin") % S["geo_file"], "rb").read())
    vb, tb = geo[m["vtx_entry"]], geo[m["tri_entry"]]
    V, UV, RGBA = [], [], []
    for i in range(m["n_verts"]):
        x, y, z, _, s_, t_, r, g, b, a = struct.unpack_from(">hhhHhhBBBB", vb, 16 * i)
        V.append([x, y, z]); UV.append([s_ / 32.0 / m["width"], t_ / 32.0 / m["height"]]); RGBA.append([r, g, b, a])
    starts, acc = [], 0
    for n, at in m["batches"]: starts.append((at, acc)); acc += n
    T, cur = [], 0
    for ti in range(m["n_tris"]):
        for at, off in starts:
            if at == ti: cur = off
        a_, b_, c_, rot = tb[4 * ti:4 * ti + 4]
        tri = [(a_, b_, c_), (b_, c_, a_), (c_, a_, b_)][rot] if rot < 3 else (a_, b_, c_)
        T.append([cur + tri[0], cur + tri[1], cur + tri[2]])
    return {"name": "x_m%02d" % k, "node": -1, "model": k, "texture": "t%02d_p%02d.png" % (m["tex_entry"], m["pal_entry"]),
            "tex_size": [m["width"], m["height"]], "ci8": m["ci8"],
            "wrap": {"cms": m["cms"], "masks": m["masks"], "cmt": m["cmt"], "maskt": m["maskt"]},
            "verts": V, "uv": UV, "rgba": RGBA, "tris": T}

def entries(d):
    n = struct.unpack_from(">I", d, 0)[0]
    offs = list(struct.unpack_from(">%dI" % n, d, 4)) + [len(d)]
    return [d[offs[i]:offs[i + 1]] for i in range(n)]

def picture(v, m):
    """("pic", texture file, palette file) of model m: No Mercy's picture files (gen/arenas.pic_pair packs them)"""
    _, tf = STAGE3D[v]
    e = entries(open(nmp("/tmp/devbox-0/rev_files/%04x.bin") % tf, "rb").read())
    t, p = map(int, re.match(r"t(\d+)_p(\d+)", m["texture"]).groups())
    tex = bytearray(e[t]); w, h = m["tex_size"]
    assert (1 << tex[6], 1 << tex[7]) == (w, h), (m["name"], tex[:8].hex())
    tex[0], tex[1], tex[4], tex[5] = w - 1, h - 1, m["wrap"]["cms"], m["wrap"]["cmt"]
    return ("pic", bytes(tex), bytes(e[p]))

def mesh(v, idx, move, grey, extra=None):
    """the models idx as one No Mercy mesh (gen/arenas_ring.py's Mesh): points SCALE x set + move on the mesh's grid,
    texels from 0, colour the vertex alpha x grey / 255; extra: [(points, uv)] quads of the first model's picture"""
    from arenas_ring import Mesh
    ms = models(v)
    pts, uv, rgb, tris = [], [], [], []
    for k in idx:
        m = ms[k]
        n = len(pts)
        pts += [place(p, move) for p in m["verts"]]
        uv += [tuple(c) for c in m["uv"]]
        rgb += [bytes([m["rgba"][i][3] * grey // 255] * 3) for i in range(len(m["verts"]))]
        tris += [tuple(i + n for i in t) for t in m["tris"]]
    for qp, quv in (extra or []):
        n = len(pts)
        pts += qp; uv += quv; rgb += [bytes([grey] * 3)] * 4; tris += [(n, n + 2, n + 1), (n + 2, n, n + 3)]
    m0 = ms[idx[0]]
    w, h = m0["tex_size"]
    mir = (m0["wrap"]["cms"], m0["wrap"]["cmt"])
    assert all(ms[k]["texture"] == m0["texture"] for k in idx), idx
    # texels, moved by whole repeats (a mirrored picture repeats every two) so the smallest is 0..one repeat
    def rebase(vals, size, mirror):
        per = size * (2 if mirror else 1)
        t = [c * size for c in vals]
        base = math.floor(min(t) / per + 1e-6) * per
        return [min(max(int(round(c - base)), 0), 255 + 240) for c in t]
    us = rebase([a for a, b in uv], w, mir[0]); vs = rebase([b for a, b in uv], h, mir[1])
    # (a span of 256: Revenge's truss -- its last texel row one in, invisible on a repeating truss)
    for vals in (us, vs):
        cap = 255 + 16 * (min(vals) // 16)          # (what the mesh's texture page leaves room for)
        for i, c in enumerate(vals): vals[i] = min(c, cap)
    # the points on the grid of the scale they'll need (No Mercy's: s8 + 16 x offset, times the scale)
    span = max(max(p[k] for p in pts) - min(p[k] for p in pts) for k in range(3))
    sc = max(1, math.ceil(span / 230.0))
    pts = [tuple(int(round(c / sc)) * sc for c in p) for p in pts]
    return Mesh(pts, list(zip(us, vs)), rgb, tris).file(), picture(v, m0)

def ring_parts(v):
    """the match's stage set: [(mesh file, picture)] in drawing order -- the old set's aisle floor first, then the stage"""
    import arenas
    from arenas_ring import Mesh, join
    t = arenas.table(v); pal = dict(zip(t["meshes"], t["pals"]))
    aisle = join([Mesh.rev(m).map(frgb=lambda c: bytes([255] * 3)) for m in AISLE[v]])
    assert len({pal[m] for m in AISLE[v]}) == 1, "one picture"
    parts = RING_PARTS.get(v) or ring_groups(v)
    door = [door_part(v, (0.0, 0.0, ring_z(v)), 255)] if v in DOOR else []
    return [(aisle.file(), pal[AISLE[v][0]])] + [mesh(v, idx, (0.0, 0.0, ring_z(v)), 255) for idx in parts] + door

def close_parts(v):
    """the close scene's pieces: [(mesh file, picture)] in Revenge's order, the floor reaching toward the camera"""
    ms = models(v)
    move = close_pt
    out = []
    for k in range(len(ms)):
        extra = None
        if k == 0:              # (the floor: its near edge carried on to CLOSE_NEAR -- a quad of its own, its texels its own)
            f = ms[0]
            zs = [p[2] for p in f["verts"]]
            near = min(zs)
            zn = (CLOSE_NEAR - CLOSE_TZ) / CLOSE_C
            # the texture coordinates are linear on the floor (u, v = a x + b z + c, fitted to its points); the near
            # edge's two points carried straight on toward the camera
            import numpy as np
            A = np.array([[p[0], p[2], 1.0] for p in f["verts"]])
            fit = np.linalg.lstsq(A, np.array(f["uv"], float), rcond=None)[0]
            nr = [i for i, p in enumerate(f["verts"]) if p[2] == near]
            assert len(nr) == 2, (v, "a floor with a straight near edge")
            q, quv = [], []
            for i in nr:
                x = f["verts"][i][0]
                quv.append(tuple(float(c) for c in np.array([x, zn, 1.0]) @ fit))
                q.append(place((x, 0, zn), move))
            # the quad: near edge (as the floor's near points), then the floor's near edge
            pts = q + [place(f["verts"][i], move) for i in nr]
            uvs = quv + [tuple(f["uv"][i]) for i in nr]
            out.append(mesh_quad(v, pts, uvs, f))
        out.append(mesh(v, [k], move, CLOSE_GREY))
    if v in DOOR: out.append(door_part(v, move, CLOSE_GREY))
    return out

def mesh_quad(v, pts, uvs, f):
    """a floor quad (points a0 a1 b0 b1: two edges, a and b, the same x in order), the floor's picture"""
    from arenas_ring import Mesh
    w, h = f["tex_size"]
    per_u, per_v = 2 * w, 2 * h
    tu = [a * w for a, b in uvs]; tv = [b * h for a, b in uvs]
    bu = math.floor(min(tu) / per_u + 1e-6) * per_u; bv = math.floor(min(tv) / per_v + 1e-6) * per_v
    uv = [(int(round(a - bu)), int(round(b - bv))) for a, b in zip(tu, tv)]
    span = max(max(p[k] for p in pts) - min(p[k] for p in pts) for k in range(3))
    sc = max(1, math.ceil(span / 230.0))
    pts = [tuple(int(round(c / sc)) * sc for c in p) for p in pts]
    # a0 a1 b0 b1 -> triangles facing up (the floor's own winding: (0, 2, 1), (2, 0, 3) on its order)
    m = Mesh(pts, uv, [bytes([CLOSE_GREY] * 3)] * 4, [(0, 2, 1), (1, 2, 3)])
    m.tris = up_facing(m.pts, m.tris)
    return m.file(), picture(v, f)

def up_facing(pts, tris):
    """the triangles wound as the floor's own (seen from above)"""
    out = []
    for a, b, c in tris:
        (ax, _, az), (bx, _, bz), (cx, _, cz) = pts[a], pts[b], pts[c]
        cross = (bx - ax) * (cz - az) - (bz - az) * (cx - ax)
        out.append((a, b, c) if cross < 0 else (a, c, b))
    return out

if __name__ == "__main__":
    for v in STAGE3D:
        for label, parts in (("ring", ring_parts(v)), ("close", close_parts(v))):
            print(label, len(parts), "parts:", " ".join("%d/%d" % (p[0][1] & 0x7F, p[0][2]) for p in parts))
