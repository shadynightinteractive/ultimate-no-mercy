"""Each arena's own walls (mod/NOTES.md "Arenas restructured", step 4b): the collision records, spans and zone lists the
match overlay's code walks for the ring's floor, the barricade, the announce table, the aisle and the far planes -- in the
overlay's own layout (data: the six-man ROM's ovl_e, the match overlay: records 12 s16 corners, the normal f32 x 3 at
+0x18, id +0x24, flags +0x26, kind +0x28, next +0x2C; a span {first, last} walked by next; a zone list: spans, 0), one
set per arena, the game's ten and the arcade's, written to src/arena_walls_files.h. arena.c points the patched code at
the arena's set as it's built (arena_take); the overlay's own records are never moved (until 2026-11-01 announce.c moved
the table's and arena_ring.c the barricade's in place, each match).

  - an arena with the announce table (the game's ten, RAW IS WAR WM 2000): the game's barricade loop with the table's
    front linked in (10..15, 24..28, 17..21), the table's walls, its top and its planes with announce.c's move built in
    (the table 75 toward the ring: its front -530 -> -455, the walls over its top and its back plane -690 -> -600:
    announce.c table_walls_up's rules), the -z barricade and the far plane at -690, the floor to -690; the straight -z
    side (16) its own record too, linked in when the table breaks (func_8012DCC0 case 2: arena.c's arena_brk_*);
  - an arena without (Revenge's): Revenge's square barricade at 530 on every side (the corners cut 75 as No Mercy's),
    the apron without its steps (the game's 29..40), the floor and the far plane to -530; the table's records its own
    copies (no zone of a table-less match reaches them);
  - every arena: the aisle's walls (x +-140 to z 2000: the game's) its own.
The apron with the steps, the ring's skirt and the ring's floor zone are the ring itself (the same ring in every arena,
its steps the game's prop): the game's spans (0x80147AB8.., 0x80147B30, 0x80146C44 / 0x80146C4C).
Called by gen/wmport.py build_all through gen/arenas_stages.py; python arenas_walls.py: a look."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp, ROOT
import math, struct

SIX_ROM = _os.path.join(ROOT, "six-engine", "NoMercySix", "nomercy.z64")
OUT_H = nmp("/home/devbox/work/arcade/src/arena_walls_files.h")
_ROM = None
def rom():
    global _ROM
    if _ROM is None: _ROM = open(SIX_ROM, "rb").read()
    return _ROM
def ovl_e(a, n): o = 0x144BE0 + a - 0x800D9960; return rom()[o:o + n]   # (ovl_e: the ROM 0x144BE0 at 0x800D9960)

# the game's records (the ROM's ovl_e: its addresses), by id
REC = {10: 0x801470F8, 11: 0x80147128, 12: 0x80147158, 13: 0x80147188, 14: 0x801471B8, 15: 0x801471E8, 16: 0x80147218,
       17: 0x80147248, 18: 0x80147278, 19: 0x801472A8, 20: 0x801472D8, 21: 0x80147308, 22: 0x80147390, 23: 0x801473C0,
       24: 0x80147410, 25: 0x80147440, 26: 0x80147470, 27: 0x801474A0, 28: 0x801474D0, 55: 0x80147520, 56: 0x80147550,
       31: 0x80147600, 29: 0x801475A0, 30: 0x801475D0, 34: 0x80147690, 32: 0x80147630, 33: 0x80147660, 37: 0x80147720,
       35: 0x801476C0, 36: 0x801476F0, 40: 0x801477B0, 38: 0x80147750, 39: 0x80147780, 0: 0x80147B38,
       78: 0x80147C50, 79: 0x80147C80, 80: 0x80147CB0, 81: 0x80147CE0, 82: 0x80147E38, 83: 0x80147D30, 84: 0x80147D60,
       85: 0x80147D90, 86: 0x80147DC0}
# the stage area's (area 10, RAW IS WAR's: row 1 of 0x8014D290 / 0x8014DA6C), for RAW's own joined set below
REC.update({i: 0x801493D0 + 0x30 * (i - 297) for i in range(297, 315)})
REC.update({315: 0x801497D0, 316: 0x80149800, 317: 0x80149830})
G_APRON_Q = [0x80147AB8, 0x80147AC0, 0x80147AC8, 0x80147AD0]   # (the apron with the steps: the game's quadrant spans)
G_SKIRT, G_RING_SPAN, G_RING_LIST = 0x80147B30, 0x80146C44, 0x80146C4C

def game_rec(i):
    d = ovl_e(REC[i], 0x30)
    c = list(struct.unpack_from(">12h", d, 0)); idv, fl, kind = struct.unpack_from(">HHH", d, 0x24)
    assert idv == i, (i, idv)
    return dict(id=i, c=c, fl=fl, kind=kind)

def unit_normal(c):
    a = [c[3 + k] - c[k] for k in range(3)]; b = [c[6 + k] - c[k] for k in range(3)]
    n = [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    l = math.sqrt(sum(x * x for x in n))
    return [x / l for x in n] if l else [0.0, 0.0, 0.0]

# announce.c table_walls_up's move (ANN_TABLE_UP 75), built in: a table wall (x +-113 at all four corners, z -530 / -690)
# -- its front (-530) to -455, its -690 to -600 when it stands over the table's top (y 1000) or is the top (y 90 all);
# the planes after the x = 113 plane (83): the z = -530 one (85) to -455, the z = -690 one (86) to -600
UP = 75
def table_moved(r):
    c = list(r["c"]); xs, ys, zs = c[0::3], c[1::3], c[2::3]
    if all(abs(x) == 113 for x in xs) and all(0 <= y <= 1000 for y in ys) and all(z in (-530, -690) for z in zs):
        high = any(y == 1000 for y in ys); top = all(y == 90 for y in ys)
        for q in range(4):
            if c[3 * q + 2] == -530: c[3 * q + 2] = -530 + UP
            elif c[3 * q + 2] == -690 and (high or top): c[3 * q + 2] = -690 + UP + 15
    if r["id"] == 85 and all(z == -530 for z in zs): c[2::3] = [-530 + UP] * 4
    if r["id"] == 86 and all(z == -690 for z in zs): c[2::3] = [-690 + UP + 15] * 4
    return dict(r, c=c)

# Revenge's square barricade (no table): B from the centre on every side, the corners cut 75; the aisle's opening as the game's
BAR_B, BAR_CUT, AISLE_X, AISLE_Z = 530, 75, 140, 800
def square(r):
    B, C, A, Z = BAR_B, BAR_B - BAR_CUT, AISLE_X, AISLE_Z
    ends = {10: (-A, Z, A, Z), 11: (A, Z, A, B), 12: (A, B, C, B), 13: (C, B, B, C), 14: (B, C, B, -C), 15: (B, -C, C, -B),
            16: (C, -B, -C, -B), 17: (-C, -B, -B, -C), 18: (-B, -C, -B, C), 19: (-B, C, -C, B), 20: (-C, B, -A, B), 21: (-A, B, -A, Z)}
    if r["id"] in ends:
        x0, z0, x1, z1 = ends[r["id"]]
        return dict(r, c=[x0, 100, z0, x1, 100, z1, x1, 0, z1, x0, 0, z0])
    if r["id"] == 56: return dict(r, c=[-B, 0, B, B, 0, B, B, 0, -B, -B, 0, -B])
    if r["id"] in (0, 86): c = list(r["c"]); c[2::3] = [-B if z == -690 else z for z in c[2::3]]; return dict(r, c=c)
    return r

# ---- every arena: the ring and its stage one area (gen/arenas_collide.py made the walls from the models; src/arena_ringstage.c runs them; data/stagecollide/COLLISION.md
# what the game's walls allow). Record 10 (the wall across the aisle, the door to the stage area) gone; 21 and 11 the first and the last of the path from the aisle's
# mouth round the stage; each prop its own closed loop, its own span. A man's y stays the game's (src/arena_ringstage.c lifts his pose).
import json as _json
RESULT = _os.path.join(ROOT, "data", "stagecollide", "result", "%d.json")
RS_COUNT_Z, RS_NEAR = 800, 200
# the door to the hallway: the stage area's own id (0x80145140: the game's code by the id; each venue's), the arcade's on their base venue's
DOOR_ID = {0: 306, 1: 357, 2: 95, 3: 288, 4: 218, 5: 266, 6: 402, 7: 340, 8: 231, 9: 373}
DOOR_HALF = 90
def rs_result(n):
    p = RESULT % n
    return _json.load(open(p)) if _os.path.exists(p) else None
def rs_wall(i, a, b, fl_class, kind):
    (ax, az), (bx, bz) = a, b
    dx, dz = bx - ax, bz - az
    assert dx or dz, "a wall of no length"
    nx, nz = dz, -dx                         # (the walkable side: its normal)
    if abs(nx) >= abs(nz): d = 0x0200 if nx > 0 else 0x0100
    else: d = 0x0400 if nz > 0 else 0x0800
    fl = d | fl_class
    assert fl & 0x30F8, "a wall a whipped man runs on the spot at (COLLISION.md section 3)"
    r = dict(id=i, fl=fl, kind=kind, c=[ax, 1000, az, bx, 1000, bz, bx, 0, bz, ax, 0, az])
    un = unit_normal(r["c"])
    assert un[0] * nx + un[2] * nz > 0, "a wall facing away from the walkable side: %s" % (r,)
    return r
DOOR_MIN = 80                           # (the least of the door across the middle: a whip there must meet it)
DOOR_STEP = 40                          # (a step of the map's stairs the door goes on across)
DOOR_NEAR_Z = 60                        # (QA rs_crash_target: a wall this near the door's z (a slanted door's ends too) is 'at the door'; was 30)
def rs_path_door(res, n):
    """the path's walls [(a, b, crash, door id or 0)]: the door across the stage's middle (x -DOOR_HALF..DOOR_HALF), in the wall
    furthest back there"""
    ws = [(tuple(w["a"]), tuple(w["b"]), w["crash"], 0) for w in res["path"]]
    if n >= 10: return ws, None           # (the arcade's: no door in their pictures, so none in their walls: a whip at the back of the stage is a crash)
    door = DOOR_ID[n]
    # (Ultimate No Mercy, the stages agent, 2026-11-20) The game's ten have their doorway in the middle of the stage's back:
    # the stage area's own door record, moved as the close set is, is RAW IS WAR's x -90..90 at z 2875 (the WWF doorway),
    # NO MERCY's -92..92 at 2785, SMACKDOWN's -155..155 at 2792 .. And the map's walls there are often short (RAW's alcove
    # 180 wide, KING OF THE RING's 180): the rule before -- the furthest-back wall 220 long or more -- put the door on a wing
    # (RAW x -670, NO MERCY -630, BACKLASH -650), off the middle (KING OF THE RING 190) or nowhere (SUMMERSLAM). Now: of the
    # walls along x with the walkable side -z (a slant under 45 degrees too) that reach DOOR_MIN or more into the middle, the
    # furthest back there (its z at the middle of that reach), then the widest reach; the door is that reach, the wall either
    # side of it kept. Where that wall ends short of the very middle (x -DOOR_MIN/2..DOOR_MIN/2) at a step of the map (a back
    # traced in 20-unit stairs: SUMMERSLAM's ridge, x -120..0 at 2390 then 0..100 at 2370), the door goes on across the step
    # (DOOR_STEP or less) to the next wall along x, as one slanted record: a whip at x 0 meets the door, not the stair's
    # corner (QA stagedoor_summerslam 2026-11-20_141737: he crashed at x 1, z 2335). ARMAGEDDON's own door is
    # on the stage's right side (x 184, z 2143..2308), where the walkable map doesn't reach: the middle wall nearest it stands
    # for it.
    def along(k):                         # (a wall along x, way +x: the walkable side -z)
        (a, b) = ws[k][0], ws[k][1]
        return b[0] - a[0] > 0 and abs(b[1] - a[1]) <= b[0] - a[0]
    def step(k):                          # (a step of the stairs: across z, DOOR_STEP or less)
        (a, b) = ws[k][0], ws[k][1]
        return a[0] == b[0] and 0 < abs(b[1] - a[1]) <= DOOR_STEP
    def z_at(k, x):
        (a, b) = ws[k][0], ws[k][1]
        return a[1] + (b[1] - a[1]) * (x - a[0]) // (b[0] - a[0])
    best = None
    for i, (a, b, cr, _) in enumerate(ws):
        if i in (0, len(ws) - 1) or not along(i): continue
        lo, hi = max(a[0], -DOOR_HALF), min(b[0], DOOR_HALF)
        if hi - lo < DOOR_MIN: continue
        zm = z_at(i, (lo + hi) // 2)
        key = (zm, hi - lo, -i)
        if best is None or key > best[0]: best = (key, i, lo, hi)
    if best is None: return ws, None
    _k, i, lo, hi = best
    j = i                                 # (the door from wall i to wall j)
    while lo > -DOOR_MIN // 2 and i - 2 >= 1 and step(i - 1) and along(i - 2): i -= 2; lo = max(ws[i][0][0], -DOOR_HALF)
    while hi < DOOR_MIN // 2 and j + 2 <= len(ws) - 2 and step(j + 1) and along(j + 2): j += 2; hi = min(ws[j][1][0], DOOR_HALF)
    d0, d1 = (lo, z_at(i, lo)), (hi, z_at(j, hi))
    a, cra = ws[i][0], ws[i][2]
    b, crb = ws[j][1], ws[j][2]
    mid = ([(a, d0, cra, 0)] if d0 != a else []) + [(d0, d1, 1, door)] + ([(d1, b, crb, 0)] if d1 != b else [])
    return ws[:i] + mid + ws[j + 1:], ((d0[0] + d1[0]) // 2, (d0[1] + d1[1]) // 2)
def rs_floors(res):
    """the raised ground as floor records (the shadow's, the weapons': 8012DDD8's readers; the man's own lift is the grid's): strips of the map, one height across, those over each
    other with one slope put together"""
    g = res["grid"]; G = 40
    strips = []
    for k in range(g["h"]):
        i = 0; row = g["cells"][k]
        while i < g["w"]:
            h = row[i]
            if h is None or h < 8: i += 1; continue
            j = i
            while j + 1 < g["w"] and row[j + 1] is not None and abs(row[j + 1] - h) <= 4: j += 1
            strips.append(dict(x0=g["x0"] + i * G, x1=g["x0"] + (j + 1) * G, z0=g["z0"] + k * G, z1=g["z0"] + (k + 1) * G, y0=h, y1=h))
            i = j + 1
    out = []
    for t in strips:
        for o in out:
            if o["x0"] == t["x0"] and o["x1"] == t["x1"] and o["z1"] == t["z0"]:
                n_ = (o["z1"] - o["z0"]) / G
                slope = (o["y1"] - o["y0"]) / n_ if n_ else 0
                if abs((t["y0"] + slope) - (o["y1"] + slope if n_ else t["y0"])) <= 6 or o["y1"] == o["y0"] == t["y0"]:
                    o["z1"] = t["z1"]; o["y1"] = t["y1"]; break
        else: out.append(dict(t))
    out.sort(key=lambda o: -(o["x1"] - o["x0"]) * (o["z1"] - o["z0"]))
    return out[:24]

def rs_crash_target(res):
    """QA: (x, z, is it a prop): the middle of a crash wall facing the ring with 320 clear in front of it, a prop's if there is one"""
    g = res["grid"]; G = 40
    def clear(x, z):
        i, k = int((x - g["x0"]) // G), int((z - g["z0"]) // G)
        return 0 <= i < g["w"] and 0 <= k < g["h"] and g["cells"][k][i] is not None
    best = None
    for prop, ws in ((1, [w for p in res["props"] for w in p]), (0, res["path"])):
        for w in ws:
            if not w["crash"]: continue
            (ax, az), (bx, bz) = w["a"], w["b"]
            dx, dz = bx - ax, bz - az; L = (dx * dx + dz * dz) ** .5
            if L < 40 or dz / L * dz / L > 0.25 or dx <= 0: continue          # (along x, the walkable side -z: travel +x has the normal (0, -1))
            mx, mz = (ax + bx) / 2.0, (az + bz) / 2.0
            if res.get("door_at") and abs(mz - res["door_at"][1]) < DOOR_NEAR_Z and abs(mx - res["door_at"][0]) < DOOR_HALF + 50:
                mx = res["door_at"][0] - (DOOR_HALF + 60)      # (beside the door, not at it)
                if mx < min(ax, bx) + 20: mx = res["door_at"][0] + DOOR_HALF + 60
                if mx > max(ax, bx) - 20: continue
            if not all(clear(mx + o, mz - d) for d in range(40, 340, 40) for o in (-40, 0, 40)): continue
            score = (prop, -abs(mx))
            if best is None or score > best[0]: best = (score, (int(mx), int(mz), prop))
    return best[1] if best else (0, res["bbox"][3], 0)

def write_ringstage(path, rows):
    """src/arena_ringstage_files.h: each arena's map, front, arrival; the arrival spots (0x8014638C's) the rest of"""
    spots = list(struct.unpack(">84h", ovl_e(0x8014638C, 168)))
    L = ["// generated by gen/arenas_walls.py write_ringstage from gen/arenas_collide.py's results: each arena's ring and stage one area (src/arena_ringstage.c)",
         "#define XRS_COUNT_Z %d" % RS_COUNT_Z, "#define XRS_NEAR %d" % RS_NEAR, "#define XRS_MOUTH_Z 530", "#define XRS_AISLE_X 140", "#define XRS_GRID 40",
         "#define XRS_BTN_XPLUS 256", "#define XRS_BTN_XMINUS 512", "#define XRS_N %d" % len(rows),
         "typedef struct { s16 x0, z0, w, h; const u8* cells; s16 front, own_y, ax, az, qx, qz, crowd_z, crowd_y; } XrsArena;   // (cells: height / 2, 255 not walkable; own_y: a Rumble's entrant's y is the game's own)",
         "static const s16 xrs_spots[84] = { %s };" % ", ".join(str(v) for v in spots)]
    for n, r in enumerate(rows):
        if r is None: continue
        g = r["grid"]
        cells = [255 if h is None else max(0, min(254, (h + 1) // 2)) for row in g["cells"] for h in row]
        L.append("static const u8 xrs%d_cells[%d] = { %s };   // %s" % (n, len(cells), ",".join(str(v) for v in cells), r["name"]))
    body = []
    for n, r in enumerate(rows):
        if r is None: body.append("{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }"); continue
        g = r["grid"]
        ax, az = r["arrive"]
        qx, qz, qp = rs_crash_target(r)
        print("  crash test %2d %-26s at %5d %5d %s" % (n, r["name"], qx, qz, "prop" if qp else "wall"))
        body.append("{ %d, %d, %d, %d, xrs%d_cells, %d, %d, %d, %d, %d, %d, %d, %d }" % (g["x0"], g["z0"], g["w"], g["h"], n, r["front"], 1 if n in (0, 2) else 0, ax, az, qx, qz, int(r["crowd_z"]), int(r["crowd_y"])))
    L.append("static const XrsArena xrs_arenas[XRS_N] = { %s };" % ", ".join(body))
    open(path, "w").write("\n".join(L) + "\n")

def arena_set(table, ringstage=False):
    """the arena's records [rec], next links {id: id}, spans {name: (first id, last id)}, zone lists, floor zones"""
    ids = [10, 11, 12, 13, 14, 15, 16, 24, 25, 26, 27, 28, 17, 18, 19, 20, 21, 22, 23, 55, 56, 0, 78, 79, 80, 81, 82, 83, 84, 85, 86]
    if not table: ids += [31, 29, 30, 34, 32, 33, 37, 35, 36, 40, 38, 39]
    recs = [game_rec(i) for i in ids]
    recs = [table_moved(r) for r in recs] if table else [square(r) for r in recs]
    loop = [10, 11, 12, 13, 14, 15] + ([24, 25, 26, 27, 28] if table else [16]) + [17, 18, 19, 20, 21]
    props, floors_rs = [], []
    if ringstage:
        ws, door_at = rs_path_door(ringstage, ringstage["n"])
        ringstage["door_at"] = door_at
        path_recs, i_ = [], 600
        for j, (a_, b_, crash, door) in enumerate(ws):
            if door: r = rs_wall(door, a_, b_, 0x2000, 0x0200)
            elif crash: r = rs_wall(i_, a_, b_, 0x0008, 0x0001)
            else: r = rs_wall(i_, a_, b_, 0x2000, 0x0400)
            if j == 0: r["id"] = 21
            if j == len(ws) - 1: r["id"] = 11
            if not door: i_ += 1
            path_recs.append(r)
        recs = [r for r in recs if r["id"] not in (10, 21, 11)] + path_recs
        path_ids = [r["id"] for r in path_recs]
        loop = [x for x in loop if x not in (10, 21, 11)] + []
        # (the chain: 12 .. 20, then the path, whose last, 11, goes on to 12)
        at12 = loop.index(12); loop = loop[at12:] + loop[:at12] + path_ids
        for p in ringstage["props"]:
            ids = []
            for w in p:
                r = rs_wall(i_, tuple(w["a"]), tuple(w["b"]), 0x0008, 0x0001); recs.append(r); ids.append(i_); i_ += 1
            props.append(ids)
        # the floor: the game's 55 to the first of the raised ground (or the set's end), the rest as it is
        fl_rs = rs_floors(ringstage)
        z_end = min([o["z0"] for o in fl_rs if abs(o["x0"] + o["x1"]) <= 400] or [ringstage["bbox"][3] + 200])
        r55 = game_rec(55); cc = list(r55["c"]); cc[2::3] = [z_end if z == 800 else z for z in cc[2::3]]
        recs = [dict(r55, c=cc) if r["id"] == 55 else r for r in recs]
        rt = game_rec(55)
        for o in fl_rs:
            recs.append(dict(rt, id=i_, c=[o["x0"], o["y1"], o["z1"], o["x1"], o["y1"], o["z1"], o["x1"], o["y0"], o["z0"], o["x0"], o["y0"], o["z0"]]))
            floors_rs.append(i_); i_ += 1
        # where he comes back from backstage: by the door, facing the ring
        ringstage["arrive"] = (door_at[0], door_at[1] - 60) if door_at else (0, ringstage["bbox"][3] - 80)
    nxt = {a: b for a, b in zip(loop, loop[1:] + loop[:1])}
    if table: nxt[16] = 17                          # (the straight -z side: linked in at 15 when the table breaks)
    else: nxt.update({24: 25, 25: 26, 26: 27, 27: 28, 28: 24})   # (the table's front: its own copy, out of any zone)
    for ring in ([22, 23], [55] + floors_rs + [56], [0], [78, 79, 80, 81], [82], [83, 84], [85, 86]):
        nxt.update({a: b for a, b in zip(ring, ring[1:] + ring[:1])})
    for ids in props: nxt.update({a: b for a, b in zip(ids, ids[1:] + ids[:1])})
    if not table:
        apr = [31, 29, 30, 34, 32, 33, 37, 35, 36, 40, 38, 39]
        nxt.update({a: b for a, b in zip(apr, apr[1:] + apr[:1])})
    spans = {"bar0": (19, 13), "bar1": (13, 15), "bar2": (14, 18), "bar3": (17, 19), "floor": (55, 56), "aisle": (22, 23),
             "plane": (0, 0), "top4": (78, 81), "top": (82, 82), "t28": (83, 84), "t30": (85, 86)}
    if not table: spans.update({"apr0": (38, 37), "apr1": (31, 37), "apr2": (32, 38), "apr3": (37, 31)})
    prop_spans = []
    for j, ids in enumerate(props):
        spans["prop%d" % j] = (ids[0], ids[-1]); prop_spans.append("prop%d" % j)     # (all of a stage in quadrant 0, gen/arenas_collide.py check: +-45 degrees round +z)
    apron = ["apr%d" % q for q in range(4)] if not table else G_APRON_Q
    lists = {}
    for q in range(4):
        extra = prop_spans if q == 0 else []
        lists["zq%d" % q] = ["bar%d" % q, apron[q]] + extra
        lists["fix%d" % q] = ["floor", G_SKIRT, "bar%d" % q] + extra
        lists["fix%d" % (4 + q)] = ["floor", apron[q], "bar%d" % q] + extra
    lists.update({"top": ["top4"], "t28": ["t28"], "t30": ["t30"], "aisle": ["aisle"], "plane": ["plane"],
                  "toplist": ["top", "floor", "top4"]})
    fz = [(G_RING_SPAN, [G_RING_LIST] * 4)] + ([("top", ["toplist"] * 4)] if table else []) + [("floor", ["fix0", "fix1", "fix2", "fix3"])]
    return recs, nxt, spans, lists, fz

def emit_arena(L, n, table, ringstage=False):
    recs, nxt, spans, lists, fz = arena_set(table, ringstage)
    at = {r["id"]: i for i, r in enumerate(recs)}
    L.append("static XWallRec xw%d_rec[%d] = {" % (n, len(recs)))
    for r in recs:
        nv = unit_normal(r["c"])
        L.append("    { { %s }, { %sf, %sf, %sf }, %d, 0x%04X, 0x%04X, 0, &xw%d_rec[%d] },   // %d" % (", ".join(str(v) for v in r["c"]),
                 *("%.6f" % v for v in nv), r["id"], r["fl"], r["kind"], n, at[nxt[r["id"]]], r["id"]))
    L.append("};")
    sn = list(spans)
    L.append("static u32 xw%d_span[%d][2] = { %s };" % (n, len(sn), ", ".join("{ (u32)&xw%d_rec[%d], (u32)&xw%d_rec[%d] }" % (n, at[a], n, at[b]) for a, b in spans.values())))
    ref = lambda s: ("(u32)xw%d_span[%d]" % (n, sn.index(s))) if isinstance(s, str) else "0x%08Xu" % s
    ln = list(lists)
    lw = max(len(v) for v in lists.values()) + 1              # (the 0 that ends each)
    L.append("static u32 xw%d_list[%d][%d] = { %s };" % (n, len(ln), lw, ", ".join("{ %s }" % ", ".join([ref(s) for s in lists[k]] + ["0"] * (lw - len(lists[k]))) for k in ln)))
    lref = lambda k: ("(u32)xw%d_list[%d]" % (n, ln.index(k))) if isinstance(k, str) else "0x%08Xu" % k
    L.append("static u32 xw%d_fz[%d][5] = { %s, { 0, 0, 0, 0, 0 } };" % (n, len(fz) + 1, ", ".join("{ %s, %s }" % (ref(s), ", ".join(lref(k) for k in ls)) for s, ls in fz)))
    top_fz = 1 if table else -1
    L.append("static const XWalls xw%d = { { %s }, (u32)xw%d_fz, { %s }, %s, %s, %s, %s, %s, (u32)&xw%d_rec[%d], %s, %s, %s, %s, %s };" % (
        n, ", ".join(lref("zq%d" % q) for q in range(4)), n, ", ".join(lref("fix%d" % k) for k in range(8)),
        lref("top"), lref("t28"), lref("t30"), lref("aisle"), lref("plane"), n, at[82],
        "(u32)&xw%d_rec[%d].next" % (n, at[15]), "(u32)&xw%d_rec[%d]" % (n, at[24 if table else 16]),
        "(u32)&xw%d_rec[%d]" % (n, at[16]), "(u32)&xw%d_fz[%d][0]" % (n, top_fz) if table else "0", ref("top") if table else "0"))

def build(arcade_tables):
    """arcade_tables: per arcade arena, 1 when it has the announce table (gen/arenas_ring.py xring_table)"""
    tables = [1] * 10 + list(arcade_tables)
    L = ["// generated by gen/arenas_walls.py: each arena's own walls (arena.c arena_take; mod/NOTES.md \"Arenas",
         "// restructured\" step 4b) -- the game's ten, then the arcade's: records (the match overlay's layout), spans,",
         "// zone lists, floor zones; XWalls: what the patched code is pointed at",
         "typedef struct XWallRec { s16 c[12]; float n[3]; u16 id, fl, kind, pad; struct XWallRec* next; } XWallRec;",
         "_Static_assert(sizeof(XWallRec) == 0x30, \"a collision record is 0x30 bytes\");",
         "typedef struct { u32 zq[4]; u32 fz; u32 fix[8]; u32 top, t28, t30, aisle, plane, rec_top;",
         "                 u32 brk_at, linked, brk_val, brk_zone, brk_span; } XWalls;",
         "// (brk_at: the table-side corner's next, linked: what it is with the table, brk_val: the straight side, for",
         "//  func_8012DCC0 case 2 -- the table broken; brk_zone: the table top's floor zone, cleared then, brk_span what it is",
         "//  with the table; 0 none)",
         "#define XWALLS_N %d" % len(tables)]
    rows = []
    for n, t in enumerate(tables):
        r = rs_result(n)
        if r is not None: r["n"] = n
        emit_arena(L, n, t, ringstage=r)
        rows.append(r)
    L.append("static const XWalls* const xwalls[XWALLS_N] = { %s };" % ", ".join("&xw%d" % n for n in range(len(tables))))
    open(OUT_H, "w").write("\n".join(L) + "\n")
    write_ringstage(OUT_H.replace("arena_walls_files.h", "arena_ringstage_files.h"), rows)

if __name__ == "__main__":
    for t in (1, 0):
        recs, nxt, spans, lists, fz = arena_set(t)
        print("table" if t else "square", len(recs), "records")
        for r in recs: print("  %2d fl %04X kind %04X %s -> %d" % (r["id"], r["fl"], r["kind"], r["c"], nxt[r["id"]]))
