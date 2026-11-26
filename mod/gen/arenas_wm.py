"""WrestleMania 2000's arenas in No Mercy: each of its seven venues an arcade arena of its own (data/wmarena/WMARENA.md;
every venue: data/wmarena/all/ALL.md) -- RAW IS WAR (venue 0, "RAW IS WAR WM 2000"), SUNDAY NIGHT HEAT (1), ROYAL RUMBLE (2),
KING OF THE RING (3), SURVIVOR SERIES (4), WRESTLEMANIA (5), SUMMERSLAM (6) (" WM 2000" where No Mercy has an arena of
that name). gen/arenas.py's ARENAS names one as ("<name>", "wm") for RAW IS WAR or ("<name>", "wm<v>"); everything here
takes the venue v (VENUES: what differs per venue -- its 3D entrance scene, aprons, its match stage's place and the
pieces of WM2000's own match stage kept, the photo and logo); RAW IS WAR's output is what it was before (v 0).
Run on its own (python arenas_wm.py [v]) it writes a header and a dry-run pack into data/wmarena/out/ (file IDs from
DRY_FILE0).

What follows is RAW IS WAR's story (the first one made; the others the same way):

WM2000 is No Mercy's direct ancestor: its arena is the same kind of data (per-venue file-ID lists in its main data segment,
read by builders of the same shape: 0x8001B06C ring / corners / barricades, 0x8001CB30 crowd, 0x8000F804 = No Mercy's
func_80010314), its meshes No Mercy's AKI format byte for byte (the vertex converter 0x8000B4EC = func_8000C1A8 but for the
alternate form's colour: 127 there, 200 here) and its units No Mercy's (ring +-280 at 90, barricades 530). One difference
in the drawing: WM2000's builder always sets the combiner FC15FE2B (TEXEL0 x SHADE_ALPHA: the picture as it is, vertex
colours ignored); No Mercy's picks it only when the list's first mesh has byte 1 bit 7 (else TEXEL0 x SHADE). Rig-checked
(data/wmarena/rig/nmcomb.py): with that bit No Mercy draws a list at full picture brightness whatever its vertex colours.
So every WM2000 arena mesh made here has byte 1 bit 7 (WM_LOOK) -- no brightening guess as Revenge's needed.

WM2000's entrance close-up is NOT its match stage: it is a 3D scene of Revenge's kind (offset-table files, F3D Vtx, a
52-byte scene table in its match overlay at 0x80105D70: data/wmarena/wm3d.py) -- scene 0, files 0x2F (geometry) / 0x30
(pictures), drawn at x0.3 with the arena hidden: the big RAW IS WAR screen in its truss, WWF.com, the doorway with the WWF
logo, curtains, the grate deck and the walkway, the WWF Attitude towers (scene 1 = the same with Sunday Night Heat's
screen). That is the "detailed close-up stage". Made here:
  - the match's stage set (src/arena.c's parts array): the 3D set merged by picture at SET_SCALE x set + STAGE_MOVE -- its
    doorway where WM2000's match stage has its doorway (z 2270), its deck at the match deck's height -- plus WM2000's
    own ramp (0x6FD, its slope: z 540..1830, the user's "ramp kept"); the 3D walkway cut at the deck's front;
  - the close scene (No Mercy's entrance close shots, scene index XARENA_BASE's tables: room for 392): the 3D set's 31
    models as they are, placed so WM2000's pose spot (the Superstars at z ~-100 in its close shots) is the origin, its
    grate deck carried on toward No Mercy's close camera;
  - everything round the ring as WM2000 RAW has it (the arena_ring.c lists): mat, aprons (RAW IS WAR), corners, floor,
    barricades (open on the stage side), the crowd (walls, tiers, the sign holders, the front row of hands), red ropes;
  - the ARENA page label / photo / VS backdrop (rendered by tools/romrender);
  - the Titantron rows (tron_panels: the screen mesh's extent and its picture's fold) for both scenes."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp, ROOT
import json, math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

WMF = nmp("/tmp/devbox-0/wm2k_files/%04x.bin")
WMA = ROOT + "/data/wmarena"
SCENES_JSON = WMA + "/wm3d_scenes.json"        # (data/wmarena/wm3d.py --ram rig/cap/close1_ram.bin: WM2000's scene table)
VENUES_JSON = WMA + "/wm_venues.json"          # (data/wmarena/all/venues_json.py: wmtab.py's lists for venues 0..6)
OUT_DIR = WMA + "/out"
OUT_H = OUT_DIR + "/arena_wm_files.h"           # (integration: src/arena_wm_files.h)
NAME = "RAW IS WAR WM 2000"
DRY_FILE0 = 0x7400                               # (the dry run's first file ID: not the pack's)
WM_LOOK = 0x80                                   # mesh byte 1 bit 7: No Mercy's combiner FC15FE2B, WM2000's own
SET_MAX = 64                                     # (src/arena.c ARENA_SET_MAX: the game's array had 15)

def wfile(i): return open(WMF % i, "rb").read()
def s8(b): return b - 256 if b > 127 else b

# ---- WM2000 RAW IS WAR's lists (venue 0; its data segment, read by data/wmarena/wmtab.py from rig RAM) ---------------
MAT = (0x5C2, 0x5C0)                                     # list A: 0x80047A1C[0] / 0x80047A28[0] (the mat's picture mirrored)
APRONS = [(0x586, 0x64B), (0x587, 0x64B), (0x588, 0x64B), (0x589, 0x64B)]   # 0x80047A50 / 0x80047A5C[0] (0x585: palette 0, not drawn)
FLOOR = [(0x5C4, 0x647), (0x5C3, 0x649)]                 # 0x80047A84[0] / 0x80047AA4[0] (its node turned 180 degrees)
BARR_A = [0x5BB, 0x5BA, 0x5BC]; BARR_B = [0x5BF, 0x5BD, 0x5BE]; BARR_PAL = 0x645   # 0x8004797C / 0x8004799C [0]; per corner node
# (the corner node turned 0 -- the stage side, +z -- has the middle 0x5BB undrawn: palette list 0x800479BC[0] = 0, 645, 645)
CORNER = [(0x57E, 0x57F), (0x581, 0x582), (0x5B9, None)]  # post (texture 0x580), pad (0x583), hook (no picture: colour 0E0E0E)
# the crowd (0x8001CB30): four side groups under a root turned 180 degrees, two palette lists (frames); the first entry of
# each (0x5B5-0x5B8) has palette 0 (not drawn). Group 2 (the stage's side, per venue: 0x80047B5C[0]) starts with the stage.
CROWD = [
    ([0x5EA, 0x5EB, 0x5EC, 0x5ED, 0x5EE, 0x5EF, 0x5E4, 0x5E5, 0x5E6, 0x5E7, 0x5E8, 0x5E9, 0x5DC, 0x5DD, 0x5DE, 0x5DF, 0x5D5, 0x5E0, 0x5E1, 0x5E2, 0x5E3, 0x5D8, 0x5D9, 0x5DA, 0x5DB],
     [0x64D, 0x64F, 0x64D, 0x64D, 0x64F, 0x64F, 0x625, 0x627, 0x625, 0x625, 0x627, 0x627, 0x637, 0x62F, 0x631, 0x637, 0x639, 0x63D, 0x63F, 0x641, 0x643, 0x62D, 0x631, 0x635, 0x62D],
     [0x64D, 0x64F, 0x64D, 0x64D, 0x64F, 0x64F, 0x625, 0x627, 0x625, 0x625, 0x627, 0x627, 0x635, 0x62D, 0x633, 0x635, 0x63B, 0x641, 0x643, 0x63D, 0x63F, 0x62F, 0x633, 0x637, 0x62F]),
    ([0x601, 0x602, 0x5F0, 0x5F1, 0x5FF, 0x600, 0x5F7, 0x5F8, 0x5F9, 0x5FA, 0x5F2, 0x5FB, 0x5FC, 0x5FD, 0x5FE, 0x5F3, 0x5F4, 0x5F5, 0x5F6],
     [0x64D, 0x64F, 0x629, 0x62B, 0x625, 0x627, 0x637, 0x62F, 0x631, 0x637, 0x639, 0x63D, 0x63F, 0x641, 0x643, 0x62D, 0x631, 0x635, 0x62D],
     [0x64D, 0x64F, 0x629, 0x62B, 0x625, 0x627, 0x635, 0x62D, 0x633, 0x635, 0x63B, 0x641, 0x643, 0x63D, 0x63F, 0x62F, 0x633, 0x637, 0x62F]),
    ([0x60A, 0x60B, 0x60C, 0x60D, 0x603, 0x60E, 0x60F, 0x610, 0x611, 0x606, 0x607, 0x608, 0x609],
     [0x637, 0x62F, 0x631, 0x637, 0x639, 0x63D, 0x63F, 0x641, 0x643, 0x62D, 0x631, 0x635, 0x62D],
     [0x635, 0x62D, 0x633, 0x635, 0x63B, 0x641, 0x643, 0x63D, 0x63F, 0x62F, 0x633, 0x637, 0x62F]),
    ([0x623, 0x624, 0x612, 0x613, 0x621, 0x622, 0x619, 0x61A, 0x61B, 0x61C, 0x614, 0x61D, 0x61E, 0x61F, 0x620, 0x615, 0x616, 0x617, 0x618],
     [0x64D, 0x64F, 0x629, 0x62B, 0x625, 0x627, 0x637, 0x62F, 0x631, 0x637, 0x639, 0x63D, 0x63F, 0x641, 0x643, 0x62D, 0x631, 0x635, 0x62D],
     [0x64D, 0x64F, 0x629, 0x62B, 0x625, 0x627, 0x635, 0x62D, 0x633, 0x635, 0x63B, 0x641, 0x643, 0x63D, 0x63F, 0x62F, 0x633, 0x637, 0x62F])]
WM_STAGE = [0x6F5, 0x6F6, 0x6F7, 0x6F8, 0x6F9, 0x6FA, 0x6FB, 0x6FC, 0x6FD]   # group 2's head: WM2000's match stage (pictures 661 665 659 65F 65D 651 663 657 655)
WM_STAGE_PAL = [0x661, 0x665, 0x659, 0x65F, 0x65D, 0x651, 0x663, 0x657, 0x655]
RAMP = (0x6FD, 0x655)                                    # kept: its slope (x +-140, y 0 at z 540 to 190 at z 1830)
FRONT_ROW = ([0x58B, 0x58C], 0x58D, 0x58F)               # 0x80047160 / 68 / 70 (0x58A palette 0): the hands, three nodes turned
FRONT_ROW_TURNS = (270, 180, 90)                         # (90 / 0 / -90 under the crowd's root, 180)
# the crowd's tiers by what they are (back to front): No Mercy's lists by bias (stands 20000, g1 2500, g2 2000, g3 1500, g4 500)
WALLS = {0x64D, 0x64F}; TIERS = {0x625, 0x627, 0x629, 0x62B}
BACK_ROW = {0x5DC, 0x5DD, 0x5DE, 0x5DF, 0x5F7, 0x5F8, 0x5F9, 0x5FA, 0x60A, 0x60B, 0x60C, 0x60D, 0x619, 0x61A, 0x61B, 0x61C}
BARS = {0x5D5, 0x5F2, 0x603, 0x614}
MID_ROW = {0x5E0, 0x5E1, 0x5E2, 0x5E3, 0x5FB, 0x5FC, 0x5FD, 0x5FE, 0x60E, 0x60F, 0x610, 0x611, 0x61D, 0x61E, 0x61F, 0x620}
FRONT_SIGNS = {0x5D8, 0x5D9, 0x5DA, 0x5DB, 0x5F3, 0x5F4, 0x5F5, 0x5F6, 0x606, 0x607, 0x608, 0x609, 0x615, 0x616, 0x617, 0x618}
ROPE_RGB = [(0x90, 0x18, 0x28), (0x60, 0x08, 0x10), (0x80, 0x14, 0x20), (0x80, 0x14, 0x20)]   # WM2000's ropes (a rope's 4
                        # vertices: R / G / B tables 0x80047C34 / 38 / 3C, read by 0x8001E178 -- one set, not per venue)
TABLE = True             # No Mercy's announce table, steel steps and JR / King kept (the user, 2026-10-31: "Add JR and King
                         # here"): src/arena_ring.c leaves the game's table path alone for this arena (xring_table 1: no rules
                         # bit 0x100000, ann_on as it is, no per-frame hiding, no collision moves), so the base venue's own
                         # walls stand -- No Mercy's -z barricade at z -690 (corners (+-530, -615) .. (+-455, -690)), the table's
                         # walls (x +-113, z -690 .. -455: announce.c). WM2000's barricades are made to stand on those lines
                         # (table_side), its floor round the ring carried out to them, the -z side's near crowd moved back.
# the -z side moved out (z <= -470 only): WM2000's -530 -> -690, its corner diagonals' -500 -> -655 and -470 -> -620 (No
# Mercy's: the straight at -690 for |x| <= 460, the diagonal to (530, -620), the sides from there)
TABLE_Z = (-470.0, -620.0, 70.0 / 60.0)          # (z0, its new place, the stretch beyond it)
TABLE_CROWD_SHIFT = -170                         # the -z side's sign holders and hands back behind the moved barricade

# ---- meshes --------------------------------------------------------------------------------------------------------
def tex_size(pal):
    t = wfile(pal + 1); return t[0] + 1, t[1] + 1, t[4], t[5]

def wm_mesh(f, pal=None, look=True):
    """a WM2000 arena mesh file as arenas_ring.Mesh: the alternate form's u16 texels / colourless points made the
    plain form (colour 255: WM2000 ignores colours, and WM_LOOK makes No Mercy too), texels moved by whole repeats"""
    from arenas_ring import Mesh
    d = wfile(f)
    alt = d[0] & 0x80; sc = (d[0] & 0x7F) + 1; nv = d[1] & 0x7F; nt = d[2]
    off = [s8(x) * 16 for x in d[4:7]]; pu, pv = (d[7] >> 4) * 16, (d[7] & 15) * 16
    pts, uv, rgb = [], [], []
    for i in range(nv):
        o = 8 + 8 * i
        pts.append(tuple((s8(d[o + k]) + off[k]) * sc for k in range(3)))
        if alt: uv.append(struct.unpack_from(">HH", d, o + 4)); rgb.append(bytes([255] * 3))
        else: uv.append((d[o + 3] + pu, d[o + 4] + pv)); rgb.append(bytes(d[o + 5:o + 8]) if not look else bytes([255] * 3))
    o = 8 + 8 * nv
    tris = [tuple(d[o + 3 * i:o + 3 * i + 3]) for i in range(nt)]
    if pal: uv = rebase(uv, *tex_size(pal))
    return Mesh(pts, uv, rgb, tris, WM_LOOK if look else 0, d[3])

def rebase(uv, w, h, cms=0, cmt=0):
    """texels moved by whole repeats (two for a mirrored picture; none for a clamped one) to start at 0"""
    def one(vals, size, fl):
        if fl & 2: return vals
        per = size * (2 if fl & 1 else 1)
        b = (min(vals) // per) * per
        return [c - b for c in vals]
    us = one([a for a, b in uv], w, cms); vs = one([b for a, b in uv], h, cmt)
    return list(zip(us, vs))

def roty(m, deg):
    """turned about y by a multiple of 90 degrees the way WM2000's row-vector node matrices turn it ((x, z) -> (z, -x) at 90)"""
    q = (deg // 90) % 4
    def f(p):
        x, y, z = p
        for _ in range(q): x, z = z, -x
        return (x, y, z)
    return m.map(f)

ALT_GREY = 200                          # (No Mercy's converter: the alternate form's points are colour 200 -- WM2000's 127)
def encode(m):
    """No Mercy's mesh file of m: the plain form (u8 texels + a page: Mesh.file) when its texels fit, else the alternate
    form (byte 0 bit 7: u16 texels, no colour -- No Mercy draws it 200 grey): func_8000C1A8 reads both"""
    try: return m.file()
    except ValueError: pass
    pts = m.pts
    assert 0 < len(pts) <= 127 and len(m.tris) <= 127, (len(pts), len(m.tris))
    assert all(0 <= u < 1024 and 0 <= v < 1024 for u, v in m.uv), "texels for the alternate form"
    for sc in range(1, 129):
        off = [int(round((min(p[k] for p in pts) + max(p[k] for p in pts)) / 2 / sc / 16)) for k in range(3)]
        q = [[p[k] / sc - off[k] * 16 for k in range(3)] for p in pts]
        if all(-128 <= o <= 127 for o in off) and all(-128 <= c <= 127 and c == int(c) for v in q for c in v): break
    else: raise ValueError("no scale fits")
    out = bytearray([0x80 | (sc - 1), m.b1 | len(pts), len(m.tris), m.b3] + [o & 0xFF for o in off] + [0])
    for v, (u, w) in zip(q, m.uv): out += bytes(int(x) & 0xFF for x in v) + b"\0" + struct.pack(">HH", int(u), int(w))
    for t in m.tris: out += bytes(t)
    if len(out) & 1: out += b"\0"
    return bytes(out)

def is_alt(m): return encode(m)[0] & 0x80 != 0

def fits(m):
    try: encode(m); return True
    except (AssertionError, ValueError): return False

def merge_greedy(items):
    """[(key, Mesh)] -> [(key, Mesh)]: meshes with one key put together in order while the result still encodes exactly
    (No Mercy's mesh: <= 127 points / 127 triangles, the points on one grid, one texture page)"""
    from arenas_ring import Mesh
    out = []
    for key, m in items:
        g = next((g for g in out if g[0] == key and fits(g[1] + m)), None)
        if g: g[1] = g[1] + m
        else: out.append([key, m])
    return [tuple(g) for g in out]

# EMPTY with WM_LOOK (a list's first mesh picks the list's combiner: never let an EMPTY pick it)
def empty():
    from arenas_ring import Mesh
    return Mesh([(0, 10, 0)] * 3, [(0, 0)] * 3, [bytes([255] * 3)] * 3, [(0, 1, 2)], WM_LOOK, 0)

# ---- the 3D entrance set (WM2000 scene 0: files 0x2F / 0x30) -----------------------------------------------------------
SCENE = 0
SET_SCALE = 0.3                         # (WM2000's: its scene nodes' world matrices, rig close1_ram)
DOOR_Z_SET = 170.0 / SET_SCALE          # the doorway's truss (models 10-12: z 537..672 in the set) -- roughly z 170 x 0.3
MATCH_DOOR_Z = 2270.0                   # WM2000's match stage: its doorway 0x6F6 and back wall at z 2270 (world)
MATCH_DECK_Y = 187.0                    # the 3D walkway's top (y 10 x 0.3) at the ramp's top, 190
STAGE_MOVE = (0.0, MATCH_DECK_Y, MATCH_DOOR_Z - 170.0)    # the match: SET_SCALE x set + this (doorway z ~2270, deck front 1800)
DECK_FRONT_SET = -1000.0                # the set's deck (grate floor, model 18) front edge: the walkway (model 19) cut here
CLOSE_POSE_Z = -100.0                   # WM2000's close shots: the Superstars at z ~-100 (rig: their bones at z -85..-120)
# Changed for new features (Ultimate No Mercy): the stagescale agent, 2026-11-20. No Mercy's Superstar poses at z -515 of
# its close scene (the vanilla game's close shots: its entrant at -530 / -500 in steps 5 / 11, the camera at (0, 75, -998)),
# not at the origin: WrestleMania 2000's pose spot goes there (NM_POSE_Z), so the set stands behind him as far as in
# WrestleMania 2000's own entrances -- placed with its pose spot at the origin it stood 515 too far back, the Superstar out
# on the walkway. The grate deck no longer carried on toward the camera (that, and its front skirt left out, made up for
# the camera seeing past the deck there): WrestleMania 2000's deck and skirt as they are, the walkway in front.
NM_POSE_Z = -515.0
CLOSE_MOVE = (0.0, 0.0, -CLOSE_POSE_Z + NM_POSE_Z)  # the close scene: SET_SCALE x set + this (the pose spot at No Mercy's)
LIT = 255                               # the close set's colour = vertex alpha x LIT / 255 (WM2000: TEXEL0 x SHADE_ALPHA x PRIM)
SCREEN_MODEL = 24                       # the big screen (node 0, flag 0x100: WM2000's video screen)
MIRROR_MODELS = {29, 30}                # node 27 (flag 0x2000: drawn twice, mirrored -- the deck's front skirt; inferred)
CLOSE_SKIP = set()                      # (stagescale 2026-11-20: none -- was {29, 30}, the deck's front skirt and a strip
                                        #  under it, covered by the deck carried on toward the camera)

def entries(d):
    n = struct.unpack_from(">I", d, 0)[0]
    offs = list(struct.unpack_from(">%dI" % n, d, 4)) + [len(d)]
    return [d[offs[i]:offs[i + 1]] for i in range(n)]

# ---- every venue (WM2000's venue select: Exhibition's arena logos; the venue byte 0x800B1014 -> s16 0x8007CE82 & 7, its
# 3D entrance scene the same index: data/wmarena/all/ALL.md). The same in all seven (the rig's tables, wm_venues.json):
# the mat, corners, floor, barricades, the crowd but its stage side, the front row, ropes, glows. Per venue: the aprons'
# picture, crowd group 2 (its stage at its head, the sign holders after it -- two fewer at Survivor Series and
# WrestleMania), the 3D scene. match = (scale, y, z): the 3D scene in the match at scale x its close scale (WM2000's world:
# 0.3 x (rec[+32] / 2048 x set + (0, 0, rec[+34]))), raised y and moved z so it stands where WM2000's own match stage
# stands (its low-detail copy: RAW / Heat the doorway at z 2270, the deck at the ramp's top; the others their front plane
# / floor, measured from the two: ALL.md); keep: WM2000's own match-stage pieces kept (RAW / Heat's ramp, the others'
# aisle floor from the barricade: (mesh, picture, z: the triangles kept those starting before it, or None)); raw_set:
# scenes 0 / 1, the RAW IS WAR set (its truss's back faces left out, the walkway cut at the deck, the skirt, the close
# deck carried on). photo: (unused since romrender: the page photo and backdrop are rendered); logo: the page photo's box.
VENUES = {
    0: dict(name="RAW IS WAR WM 2000", match=(1.0, 187.0, 2100.0), keep=[(0x6FD, 0x655, 1830)],
            photo="wm_raw_fly.png", logo=0x65F, raw_set=True),
    1: dict(name="SUNDAY NIGHT HEAT", match=(1.0, 187.0, 2100.0), keep=[(0x6FD, 0x655, 1830)],
            photo="wm_heat_fly.png", logo=0x669, raw_set=True),
    2: dict(name="ROYAL RUMBLE WM 2000", match=(1.0, 0.0, 1957.0), keep=[(0x6CF, 0x66D, None)], photo="wm_rumble_fly.png", logo=0x67F),
    3: dict(name="KING OF THE RING WM 2000", match=(1.0, 0.0, 1957.0), keep=[(0x6C5, 0x66D, None)], photo="wm_kotr_fly.png", logo=0x673),
    4: dict(name="SURVIVOR SERIES WM 2000", match=(1.0, 0.0, 1535.0), keep=[(0x6E7, 0x6A9, None)], photo="wm_survivor_fly.png", logo=0x6AD),
    5: dict(name="WRESTLEMANIA WM 2000", match=(0.8, 0.0, 1578.0), keep=[(0x6F1, 0x6B9, None)], photo="wm_wm_fly.png", logo=0x6C1),
    6: dict(name="SUMMERSLAM WM 2000", match=(1.0, 0.0, 1700.0), keep=[(0x6DC, 0x68D, None)], photo="wm_summerslam_fly.png", logo=0x68B),
}
CROWD_OWN = set(range(0x603, 0x612))    # (group 2's own crowd meshes: what follows the stage at its head)
_VC = {}
class Venue: pass
def venue(v=0):
    """venue v's config (VENUES + its tables in wm_venues.json + its scene's record): a Venue"""
    if isinstance(v, Venue): return v
    if v in _VC: return _VC[v]
    c = VENUES[v]; V = Venue(); V.v = v; V.scene = v; V.__dict__.update(c)
    V.raw_set = c.get("raw_set", False)
    T = json.load(open(VENUES_JSON))[str(v)]
    h = lambda xs: [int(x) for x in xs]
    ap = T["apron"]; V.aprons = [(m, p) for m, p in zip(h(ap["mesh"]), h(ap["pal"])) if p]
    groups = []
    for g, G in enumerate(T["crowd"]):
        ms, pa, pb = h(G["mesh"]), h(G["palA"]), h(G["palB"])
        keep = [i for i in range(len(ms)) if pa[i] and (g != 2 or ms[i] in CROWD_OWN)]
        groups.append(([ms[i] for i in keep], [pa[i] for i in keep], [pb[i] for i in keep]))
    V.crowd = groups
    sc = json.load(open(SCENES_JSON))["scenes"][V.scene]
    s32, s34 = struct.unpack_from(">HH", bytes.fromhex(sc["raw52"]), 32)
    V.sc = SET_SCALE * (s32 / 2048.0); V.oz = SET_SCALE * s34
    ms, dy, dz = c["match"]
    V.match_scale = V.sc * ms; V.match_move = (0.0, dy, ms * V.oz + dz)
    V.close_move = (0.0, 0.0, V.oz - CLOSE_POSE_Z + NM_POSE_Z)
    V.screen_model = next((n["first"] for n in sc["nodes"] if n["flags"] & 0x100), None)
    V.mirror_models = {k for n in sc["nodes"] if n["flags"] & 0x2000 for k in range(n["first"], n["first"] + n["count"])}
    V.close_skip = CLOSE_SKIP if V.raw_set else set()
    _VC[v] = V
    return V

def wm_venue(a):
    """gen/arenas.py's ARENAS second field -> WM2000's venue (None: not one of WM2000's)"""
    if a == "wm": return 0
    if isinstance(a, str) and a.startswith("wm") and a[2:].isdigit(): return int(a[2:])
    return None

def scene(Vn=0):
    return json.load(open(SCENES_JSON))["scenes"][venue(Vn).scene]

def rot_y(x, y, z, deg):
    """a node's turn (WM2000's: data/wmarena/wm3d.py rot_y -- rig-checked on Royal Rumble's spotlights)"""
    if not deg: return (x, y, z)
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return (x * c + z * s, y, -x * s + z * c)

_MODELS = {}
def models(Vn=0):
    """the scene's models in drawing order (its node list: plain nodes; sprites -- flags 0x8000 -- left out; a model
    used by several nodes once per node, turned and placed as each):
    [{k, verts (set units), st (texels), alpha, tris, tex (entry, palette entry, ci8), size, wrap}]"""
    Vn = venue(Vn)
    if Vn.v in _MODELS: return _MODELS[Vn.v]
    sc = scene(Vn); geo = entries(wfile(sc["geo_file"]))
    out = []
    for nd in sc["nodes"]:
        if nd["flags"] & 0x8000: continue
        for k in range(nd["first"], nd["first"] + nd["count"]):
            m = sc["models"][k]
            vb, tb = geo[m["vtx_entry"]], geo[m["tri_entry"]]
            V, ST, A = [], [], []
            for v in range(m["n_verts"]):
                x, y, z, _, s, t, r, g, b, a = struct.unpack_from(">hhhHhhBBBB", vb, 16 * v)
                x, y, z = rot_y(x, y, z, nd["ry_deg"])
                V.append((x + nd["pos"][0], y + nd["pos"][1], z + nd["pos"][2])); ST.append((s / 32.0, t / 32.0)); A.append(a)
            starts, acc = [], 0
            for n, at in m["batches"]: starts.append((at, acc)); acc += n
            T, cur = [], 0
            for ti in range(m["n_tris"]):
                for at, o in starts:
                    if at == ti: cur = o
                a_, b_, c_, rot = tb[4 * ti:4 * ti + 4]
                tri = [(a_, b_, c_), (b_, c_, a_), (c_, a_, b_)][rot] if rot < 3 else (a_, b_, c_)
                T.append(tuple(cur + i for i in tri))
            item = prep_uv(dict(k=k, verts=V, st=ST, alpha=A, tris=T, tex=(m["tex_entry"], m["pal_entry"], m["ci8"]),
                                size=(m["width"], m["height"]), wrap=(m["cms"], m["cmt"])))
            out.append(item)
            if k in Vn.mirror_models:
                mm = dict(item); mm["k"] = k + 1000
                mm["verts"] = [(-x, y, z) for x, y, z in V]; mm["tris"] = [(a, c, b) for a, b, c in T]
                out.append(mm)
    _MODELS[Vn.v] = out
    return out

UV_MAX = 1023 - 64                      # (texels a mesh can hold: the plain form u8 + its page; the alternate form
                                        # u16, made S = u << 5 in a s16 by the converter: < 1024 -- encode() picks)
def prep_uv(m):
    """the model's texels moved by whole repeats to start at 0; a model whose texels still span more than a mesh can hold
    (the truss beams: a 64-texel picture repeated along 2700 units) made of its triangles each with points of its own,
    each moved by its own whole repeats"""
    (w, h), (cms, cmt) = m["size"], m["wrap"]
    pu, pv = w * (2 if cms & 1 else 1), h * (2 if cmt & 1 else 1)
    def base(vals, per): return math.floor(min(vals) / per + 1e-6) * per
    bu, bv = base([s for s, t in m["st"]], pu), base([t for s, t in m["st"]], pv)
    st = [(s - bu, t - bv) for s, t in m["st"]]
    if max(s for s, t in st) <= UV_MAX and max(t for s, t in st) <= UV_MAX:
        return dict(m, st=st)
    # triangles whose own texels span more than a mesh holds (the long truss beams: up to 767) halved along their
    # longest edge in texels until they fit (positions, texels and alpha at the midpoint)
    P, Q, Al = list(m["verts"]), list(m["st"]), list(m["alpha"])
    def span(t): return max(max(Q[i][k] for i in t) - min(Q[i][k] for i in t) for k in (0, 1))
    todo, tris = list(m["tris"]), []
    while todo:
        t = todo.pop()
        if span(t) <= UV_MAX - 64: tris.append(t); continue
        e = max(((0, 1), (1, 2), (2, 0)), key=lambda e: max(abs(Q[t[e[0]]][k] - Q[t[e[1]]][k]) for k in (0, 1)))
        i, j = t[e[0]], t[e[1]]; o = t[3 - e[0] - e[1]]
        n = len(P)
        P.append(tuple((a + b) / 2.0 for a, b in zip(P[i], P[j]))); Q.append(tuple((a + b) / 2.0 for a, b in zip(Q[i], Q[j])))
        Al.append((Al[i] + Al[j]) // 2)
        todo += [(i, n, o), (n, j, o)]          # (i -> j is one of the triangle's own edges in its order: the winding kept)
    # the triangles in clusters, each moved by its own whole repeats and within UV_MAX, sharing points inside a cluster
    clusters = []               # [[tris], set(points)]
    def fits(pts):
        su, sv = [Q[i][0] for i in pts], [Q[i][1] for i in pts]
        return (max(su) - base(su, pu) <= UV_MAX) and (max(sv) - base(sv, pv) <= UV_MAX)
    for t in tris:
        c = next((c for c in clusters if fits(c[1] | set(t))), None)
        if c: c[0].append(t); c[1] |= set(t)
        else: clusters.append([[t], set(t)])
    V, ST, A, T = [], [], [], []
    for ct, cp in clusters:
        cp = sorted(cp); at = {}
        bu, bv = base([Q[i][0] for i in cp], pu), base([Q[i][1] for i in cp], pv)
        for i in cp:
            at[i] = len(V); V.append(P[i]); ST.append((Q[i][0] - bu, Q[i][1] - bv)); A.append(Al[i])
        T += [tuple(at[i] for i in t) for t in ct]
    return dict(m, verts=V, st=ST, alpha=A, tris=T)

def depth(m):
    """back to front for the views from the ring's side (-z): the model's mean z, the far first (No Mercy draws a list in
    its order, no depth buffer; WM2000's own node order isn't back to front -- its curtains come after the screen)"""
    return -sum(p[2] for p in m["verts"]) / float(len(m["verts"]))

# ---- drawing order: No Mercy draws a list in its order with no depth buffer (render mode 0F0A7008: no Z compare / update),
# so the pieces go in an order that's right from the cameras that see them: each piece's depth rendered from VIEWS (a
# coarse Z-buffer per piece); where two cover the same pixels, the nearer goes after; a topological order of that, ties
# and cycles by the mean depth (far first)
def _look(eye, at, fovy=33.0, aspect=4 / 3.0):
    import numpy as np
    eye, at = np.array(eye, float), np.array(at, float)
    f = at - eye; f /= np.linalg.norm(f); r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r); u = np.cross(r, f)
    c = 1 / math.tan(math.radians(fovy) / 2)
    return eye, r, u, f, c / aspect, c

def _depth_map(pts, tris, cam, W=160, H=120):
    import numpy as np
    eye, r, u, f, cx, cy = cam
    P = np.array(pts, float) - eye
    x, y, z = P @ r, P @ u, P @ f
    D = np.full((H, W), np.inf)
    for a, b, c in tris:
        zz = z[[a, b, c]]
        if zz.min() < 10: continue
        sx = (x[[a, b, c]] / zz * cx + 1) * W / 2; sy = (1 - y[[a, b, c]] / zz * cy) * H / 2
        area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0])
        if area >= 0: continue                     # (culled: the back -- as the game)
        x0, x1 = max(0, int(sx.min())), min(W - 1, int(sx.max()) + 1); y0, y1 = max(0, int(sy.min())), min(H - 1, int(sy.max()) + 1)
        if x0 > x1 or y0 > y1: continue
        X, Y = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        l0 = ((sx[1] - X) * (sy[2] - Y) - (sx[2] - X) * (sy[1] - Y)) / area
        l1 = ((sx[2] - X) * (sy[0] - Y) - (sx[0] - X) * (sy[2] - Y)) / area
        l2 = 1 - l0 - l1
        ins = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        d = 1 / (l0 / zz[0] + l1 / zz[1] + l2 / zz[2])
        sub = D[y0:y1 + 1, x0:x1 + 1]; m = ins & (d < sub); sub[m] = d[m]
    return D

def _extent(p): return (max(q[0] for q in p) - min(q[0] for q in p)) * (max(q[2] for q in p) - min(q[2] for q in p))

def painter(pieces, views):
    """pieces: [(points (world), triangles)] -> their drawing order (indices), far first"""
    import numpy as np
    n = len(pieces)
    maps = [[_depth_map(p, t, _look(*v)) for p, t in pieces] for v in views]
    after = [[0] * n for _ in range(n)]          # after[i][j]: pixels where i is nearer than j (i must come after j)
    for vm in maps:
        for i in range(n):
            for j in range(i + 1, n):
                both = np.isfinite(vm[i]) & np.isfinite(vm[j])
                if not both.any(): continue
                after[i][j] += int((vm[i][both] < vm[j][both]).sum()); after[j][i] += int((vm[j][both] < vm[i][both]).sum())
    mean = [-sum(q[2] for q in p) / len(p) for p, t in pieces]
    # floors first (a piece less than 5 units high: the deck, the walkway on it), lowest first -- coplanar pieces' depths
    # don't tell which lies on which
    flat = sorted([i for i, (p, t) in enumerate(pieces) if max(q[1] for q in p) - min(q[1] for q in p) < 5],
                  key=lambda i: (max(q[1] for q in pieces[i][0]), -_extent(pieces[i][0])))   # (the wide deck under the walkway)
    order, left = list(flat), set(range(n)) - set(flat)
    while left:
        # the piece nothing left must come before (fewest pixels nearer than the others left), far first on ties
        best = min(left, key=lambda i: (sum(after[i][j] for j in left if j != i and after[i][j] > after[j][i]), mean[i]))
        order.append(best); left.remove(best)
    return order

CLOSE_VIEWS = [((215, 320, -1120), (10, 145, 25)), ((-215, 320, -1120), (-10, 145, 25)), ((0, 161, -1088), (0, 150, 300)),
               ((0, 300, -700), (0, 300, 300))]
# (stagescale 2026-11-20: the views go with the set, NM_POSE_Z on z -- the same views of it as before)
CLOSE_VIEWS = [((e[0], e[1], e[2] + NM_POSE_Z), (t[0], t[1], t[2] + NM_POSE_Z)) for e, t in CLOSE_VIEWS]
MATCH_VIEWS = [((534, 276, -1228), (343, 256, -766)), ((0, 400, -400), (0, 400, 2000)), ((0, 900, -1400), (0, 300, 1500)),
               ((800, 300, -800), (0, 300, 2000)), ((-800, 300, -800), (0, 300, 2000)), ((0, 250, 700), (0, 400, 2200))]

def set_picture(tex, Vn=0):
    """("pic", texture file, palette file): the scene's picture entry with No Mercy's header (w-1, h-1; b4/b5 the wrap)"""
    e = entries(wfile(scene(Vn)["tex_file"]))
    t = bytearray(e[tex[0]]); w, h = 1 << t[6], 1 << t[7]
    t[0], t[1] = w - 1, h - 1
    m = next(m for m in scene(Vn)["models"] if (m["tex_entry"], m["pal_entry"]) == tex[:2])
    t[4], t[5] = m["cms"], m["cmt"]
    return ("pic", bytes(t), bytes(e[tex[1]][:32 if not tex[2] else 512]))

def brighter(pic, k=255.0 / ALT_GREY):
    """("pic", texture, palette) with its palette's colours x k (an alternate-form mesh's 200 grey made 255 again)"""
    q = bytearray(pic[2])
    for i in range(0, len(q), 2):
        c = struct.unpack_from(">H", q, i)[0]
        r, g, b = [min(31, int(round(((c >> sh) & 31) * k))) for sh in (11, 6, 1)]
        struct.pack_into(">H", q, i, r << 11 | g << 6 | b << 1 | (c & 1))
    return ("pic", pic[1], bytes(q))

def picture_for(m, pic):
    """the picture a 3D-set mesh is drawn with: brighter() when it went the alternate form (its colour fixed 200)"""
    return brighter(pic) if is_alt(m) and not (m.b1 & WM_LOOK) else pic

def set_mesh(ms, move, lit=LIT, look=False, clip=None, scale=SET_SCALE, screen=SCREEN_MODEL):
    """models (one picture) as one No Mercy mesh: scale x set + move on the mesh's grid, texels by whole repeats,
    colour = alpha x lit / 255 (look: WM_LOOK -- the picture as it is); clip(points, st) -> (points, st) per model"""
    from arenas_ring import Mesh
    pts, uv, rgb, tris = [], [], [], []
    w, h = ms[0]["size"]
    for m in ms:
        V, ST = m["verts"], m["st"]
        if clip: V, ST = clip(V, ST)
        n = len(pts)
        pts += [tuple(scale * c + d for c, d in zip(p, move)) for p in V]
        uv += [(s, t) for s, t in ST]
        rgb += [bytes([(255 if m["k"] == screen else a) * lit // 255] * 3) for a in m["alpha"]]   # (the screen: lit)
        tris += [tuple(i + n for i in t) for t in m["tris"]]
    uv = [(min(max(int(round(a)), 0), UV_MAX), min(max(int(round(b)), 0), UV_MAX)) for a, b in uv]   # (prep_uv: from 0)
    # the grid of the scale the points need: their span (s8 points) and their middle (s8 offsets of 16: 2032 x scale)
    span = max(max(p[k] for p in pts) - min(p[k] for p in pts) for k in range(3))
    mid = max(abs(max(p[k] for p in pts) + min(p[k] for p in pts)) / 2.0 for k in range(3))
    sc = max(1, math.ceil(span / 230.0), math.ceil(mid / 1900.0))
    pts = [tuple(int(round(c / sc)) * sc for c in p) for p in pts]
    return Mesh(pts, uv, rgb, tris, WM_LOOK if look else 0, 0)

def cut_walkway(V, ST):
    """the walkway (model 19: x +-405, z -5000..537) from the deck's front back (its texels along)"""
    zs = sorted(set(p[2] for p in V))
    if min(zs) >= DECK_FRONT_SET: return V, ST
    out_v, out_t = list(V), list(ST)
    for i, p in enumerate(V):
        if p[2] >= DECK_FRONT_SET: continue
        j = min((k for k, q in enumerate(V) if q[0] == p[0] and q[2] > p[2]), key=lambda k: V[k][2])
        t = (DECK_FRONT_SET - p[2]) / float(V[j][2] - p[2])
        out_v[i] = (p[0], p[1] + t * (V[j][1] - p[1]), DECK_FRONT_SET)
        out_t[i] = tuple(a + t * (b - a) for a, b in zip(ST[i], ST[j]))
    return out_v, out_t

def cut_skirt(y_min):
    """the deck's front skirt (model 29: y -2000..0) only down to y_min (the arena floor under the raised deck)"""
    def f(V, ST):
        ys = [p[1] for p in V]; lo, hi = min(ys), max(ys)
        return [(x, max(y, y_min), z) for x, y, z in V], ST      # (its picture is all black: no texels to move)
    return f

EYES = [(0, 300, -1100), (800, 300, -800), (-800, 300, -800), (0, 300, 0), (500, 250, 300), (-500, 250, 300)]
def facing(m, move, scale=SET_SCALE):
    """the model with only the triangles some match camera (EYES: round the ring) sees from the front (the others are
    culled from everywhere a match looks from); the sign: the screen's quad (model 24) faces the ring"""
    def side(t, e):
        a, b, c = (tuple(scale * q + d for q, d in zip(m["verts"][i], move)) for i in t)
        n = [(b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]), (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
             (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])]
        return sum(n[k] * (e[k] - a[k]) for k in range(3))
    keep = [t for t in m["tris"] if any(side(t, e) * FRONT_SIGN > 0 for e in EYES)]
    used = sorted({i for t in keep for i in t}); at = {v: k for k, v in enumerate(used)}
    return dict(m, verts=[m["verts"][i] for i in used], st=[m["st"][i] for i in used], alpha=[m["alpha"][i] for i in used],
                tris=[tuple(at[i] for i in t) for t in keep])
FRONT_SIGN = 1                          # (set by _front_sign(): the screen faces the ring)
def _front_sign(Vn=0):
    global FRONT_SIGN
    Vn = venue(Vn)
    m = next(m for m in models(Vn) if m["k"] == Vn.screen_model)
    a, b, c = (tuple(Vn.match_scale * q + d for q, d in zip(m["verts"][i], Vn.match_move)) for i in m["tris"][0])
    n = [(b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]), (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
         (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])]
    e = EYES[0]; FRONT_SIGN = 1 if sum(n[k] * (e[k] - a[k]) for k in range(3)) > 0 else -1
TRUSS_TEX = 1                           # (the truss picture: its beams' faces no match camera sees left out)
# (while the match's set had 15 places, two pictures were drawn with the curtains' instead: the lower curtain panels'
# (models 25 / 26, picture 17) and the deck's black front skirt's (models 29 + its mirror, picture 23). The set's parts
# are src/arena.c's now (ARENA_SET_MAX): each its own picture again.)
TEX_REMAP = {}

_STAGE = {}
def stage_parts(Vn=0):
    """the match's stage set: [(name, Mesh, picture)] in drawing order, <= SET_MAX -- the 3D set merged by picture (the screen
    its own part: tron.c's row), WM2000's own match-stage pieces kept (RAW IS WAR's ramp's slope)"""
    Vn = venue(Vn)
    if Vn.v in _STAGE: return _STAGE[Vn.v]
    SC, MV, SCR = Vn.match_scale, Vn.match_move, Vn.screen_model
    if Vn.raw_set:
        _front_sign(Vn)
        ms = [facing(m, MV, SC) if m["tex"][0] == TRUSS_TEX else m for m in models(Vn) if m["k"] != 30 and m["k"] != 1030]
    else: ms = list(models(Vn))
    groups = []                 # [[tex, [models]]] in first-use order, split at 127 points / triangles
    by_tex = {m["tex"][0]: m["tex"] for m in ms}
    for i, m in enumerate(ms):
        if m["tex"][0] in TEX_REMAP:
            to, st = TEX_REMAP[m["tex"][0]]
            m = dict(m, tex=by_tex[to], st=[st] * len(m["verts"]) if st else m["st"]); ms[i] = m
    for m in ms:
        g = next((g for g in groups if g[0] == m["tex"] and m["k"] != SCR and SCR not in [x["k"] for x in g[1]]
                  and sum(len(x["verts"]) for x in g[1]) + len(m["verts"]) <= 127
                  and sum(len(x["tris"]) for x in g[1]) + len(m["tris"]) <= 127), None)
        if g: g[1].append(m)
        else: groups.append([m["tex"], [m]])
    groups.sort(key=lambda g: depth(dict(verts=[p for m in g[1] for p in m["verts"]])))
    skirt_floor = -MV[1] / SC         # (the skirt down to the arena floor, y 0)
    out = []
    for tex, g in groups:
        clip = None
        if Vn.raw_set and any(m["k"] == 19 for m in g): clip = cut_walkway
        if Vn.raw_set and any(m["k"] % 1000 == 29 for m in g): clip = cut_skirt(skirt_floor)
        name = "screen" if SCR is not None and g[0]["k"] == SCR else "set t%d" % tex[0]
        mm = set_mesh(g, MV, clip=clip, scale=SC, screen=SCR)
        out.append((name, mm, picture_for(mm, set_picture(tex, Vn))))
    # WM2000's own match-stage pieces kept: RAW IS WAR's ramp -- its slope (triangles 1 and 3: z 540..1830), the top flat
    # part left to the 3D walkway; the others' aisle floor from the barricade (under the 3D scene's floors: drawn first)
    for f, p, zcut in Vn.keep:
        r = roty(wm_mesh(f, p), 180)
        if zcut is not None: r.tris = [t for t in r.tris if min(r.pts[i][2] for i in t) < zcut]
        out.append(("ramp" if Vn.raw_set else "aisle", r, p))
    assert len(out) <= SET_MAX, [n for n, m, p in out]
    order = painter([(m.pts, m.tris) for n, m, p in out], MATCH_VIEWS)
    _STAGE[Vn.v] = [out[i] for i in order]
    return _STAGE[Vn.v]

_CLOSE = {}
def close_parts(Vn=0):
    """the close scene: [(name, Mesh, picture)] -- every model in WM2000's order at its scale x set + its close move (WM2000's
    pose spot at No Mercy's, NM_POSE_Z)"""
    Vn = venue(Vn)
    if Vn.v in _CLOSE: return _CLOSE[Vn.v]
    CM, SC = Vn.close_move, Vn.sc
    out = []
    for m in sorted(models(Vn), key=depth):
        if m["k"] in Vn.close_skip or m["k"] - 1000 in Vn.close_skip: continue
        mm = set_mesh([m], CM, scale=SC, screen=Vn.screen_model); out.append(("m%d" % m["k"], mm, picture_for(mm, set_picture(m["tex"], Vn))))
    order = painter([(m.pts, m.tris) for n, m, p in out], CLOSE_VIEWS)
    _CLOSE[Vn.v] = [out[i] for i in order]
    return _CLOSE[Vn.v]

# ---- everything round the ring (src/arena_ring.c's lists) ---------------------------------------------------------------
CAGE = 250.0 / 280.0
def caged(m):
    return m.map(lambda p: (int(round(p[0] * CAGE / 10.0)) * 10, p[1], int(round(p[2] * CAGE / 10.0)) * 10) if abs(p[0]) < 400 and abs(p[2]) < 400 else p)

def table_side(m):
    """the -z side moved out to No Mercy's table lines (TABLE): points at z <= TABLE_Z[0] stretched out (z only)"""
    if not TABLE: return m
    z0, z1, k = TABLE_Z
    return m.map(lambda q: (q[0], q[1], int(round(z1 + (q[2] - z0) * k))) if q[2] <= z0 else q)

def ring_lists(Vn=0):
    """{list: [(Mesh, picture)]} -- pictures: a WM2000 palette file (texture + 1), "hook" (16 x 16 of 0E0E0E), or a
    corner's (texture, palette) pair"""
    Vn = venue(Vn)
    L = {}
    L["A"] = [(wm_mesh(*MAT), MAT[1])]
    L["apron"] = [(wm_mesh(m, p), p) for m, p in Vn.aprons]
    L["floor"] = [(table_side(roty(wm_mesh(m, p), 180)), p) for m, p in FLOOR]
    bar = []
    for c in range(4):
        a = [roty(wm_mesh(f, BARR_PAL), 90 * c) for f in BARR_A if not (c == 0 and f == 0x5BB)]
        b = [roty(wm_mesh(f, BARR_PAL), 90 * c) for f in BARR_B]
        from arenas_ring import join
        bar += [(table_side(join(a)), BARR_PAL), (table_side(join(b)), BARR_PAL)]
    L["barricade"] = bar
    L["corner"] = [(wm_mesh(CORNER[0][0], CORNER[0][1]), (0x580, 0x57F)), (wm_mesh(CORNER[1][0], CORNER[1][1]), (0x583, 0x582)),
                   (wm_mesh(CORNER[2][0]), "hook")]
    # the crowd: pieces by tier, merged across sides by picture pair (lists are drawn whole, in order, by bias)
    tiers = {"walls": [], "tiers": [], "back": [], "bars": [], "mid": [], "front": []}
    for g, (meshes, pa, pb) in enumerate(Vn.crowd):
        for f, a, b in zip(meshes, pa, pb):
            m = roty(wm_mesh(f, a), 180)
            if TABLE and g == 0 and a not in WALLS and a not in TIERS:      # (group 0 is the -z side: its near rows)
                m = m.map(lambda q: (q[0], q[1], q[2] + TABLE_CROWD_SHIFT))
            t = ("walls" if a in WALLS else "tiers" if a in TIERS else "back" if f in BACK_ROW else "bars" if f in BARS
                 else "mid" if f in MID_ROW else "front" if f in FRONT_SIGNS else None)
            assert t, hex(f)
            tiers[t].append(((a, b), m))
    hands = []
    for r in FRONT_ROW_TURNS:
        from arenas_ring import join
        h = roty(join([wm_mesh(f, FRONT_ROW[1]) for f in FRONT_ROW[0]]), r)
        if TABLE and sum(q[2] for q in h.pts) < 0: h = h.map(lambda q: (q[0], q[1], q[2] + TABLE_CROWD_SHIFT))   # (the -z side's)
        hands.append(((FRONT_ROW[1], FRONT_ROW[2]), h))
    M = {k: merge_greedy(v) for k, v in tiers.items()}
    L["stands"] = [(m, a) for (a, b), m in M["walls"] + M["tiers"]]          # (static: frames A = B)
    assert all(a == b for (a, b), m in M["walls"] + M["tiers"])
    L["g1"] = [(m, ab) for ab, m in M["back"]]
    L["g2"] = [(m, ab) for ab, m in M["bars"] + M["mid"]]
    L["g3"] = [(m, ab) for ab, m in M["front"]]
    L["g4"] = [(m, ab) for ab, m in merge_greedy(hands)]
    return L

GROUPS = [("stands", 15, False), ("g1", 3, True), ("g2", 9, True), ("g3", 22, True), ("g4", 22, True)]

# ---- the ARENA page's pictures and the VS backdrop (gen/arenas.py's makers) --------------------------------------------
# (romrender 2026-11-18, the user's decision A: RENDERED by our own renderer, tools/romrender/rr_arena.py, from the venue's
#  pieces as the pack gets them -- its stage set and everything round the ring (ring_lists), the corners turned to their
#  four sides -- with gen/arenas.py's one camera for every arena. Was a WM2000 rig shot, pics/wm_*_fly.png.)
def rr_pieces(Vn=0):
    Vn = venue(Vn)
    out = []
    def add(m, p, turns=0):
        pic = _pic(p)
        out.append((encode(m), pic[1], pic[2], turns))
    for n, m, p in stage_parts(Vn): add(m, p)
    L = ring_lists(Vn)
    for name, n_, two in GROUPS:
        for m, p in L[name]: add(m, p[0] if two else p)
    for m, p in L["floor"]: add(m, p)
    for m, p in L["apron"]: add(m, p)
    add(L["A"][0][0], L["A"][0][1])
    for m, p in L["barricade"]: add(m, p)
    for r in range(4):
        for m, p in L["corner"]: add(m, p, r)
    return out

_PHOTOS = {}
def photo_px(Vn, W, H):
    Vn = venue(Vn)
    if (Vn.v, W, H) not in _PHOTOS:
        import arenas
        _PHOTOS[(Vn.v, W, H)] = arenas.RA.photo(rr_pieces(Vn), W, H)
    return _PHOTOS[(Vn.v, W, H)]

def preview(Vn=0):
    """the photo's six 64 x 32 CI8 tiles (gen/arenas.py preview's layout) -> (tiles, the 128 x 96 RGBA page)"""
    import arenas
    Vn = venue(Vn)
    img = arenas.RA.page(photo_px(Vn, 100, 74), arenas.RA.texture_rgba(wfile(Vn.logo + 1), wfile(Vn.logo)))
    return arenas.RA.page_tiles(img, open(arenas.NMF % 0x4641, "rb").read()[:16]), img

def backdrop(Vn=0):
    import arenas
    px = photo_px(Vn, 320, 240)
    return arenas.RA.backdrop_tiles(px), px

# ---- the Titantron (src/tron_panels_all.h rows) -------------------------------------------------------------------------
def fold(b):
    b = (bytes(b) + bytes(2048))[:2048]; a = x = 0
    for (w,) in struct.iter_unpack(">I", b):
        a = (a + w) & 0xFFFFFFFF; x = (((x << 1) | (x >> 31)) & 0xFFFFFFFF) ^ w
    return a, x

# ---- the files and the header ------------------------------------------------------------------------------------------
def build(F, k_arena=None, v=0):
    """everything for venue v into F (a dry run's Files, F.file0 set); writes OUT_H -> a dict of what was made (k_arena:
    the arcade's arena number for the tron rows, 10 + its place in gen/arenas.py's ARENAS)"""
    import arenas
    from arenas_ring import Mesh_from_file
    Vn = venue(v)
    k_arena = 13 if k_arena is None else k_arena
    def pal_of(p):
        if isinstance(p, tuple) and p[0] == "pic": return F.pair(p[1], p[2])[1]
        if isinstance(p, tuple): return F.pair(wfile(p[0]), wfile(p[1]))[1]
        if p == "hook": return F.pair(bytes([15, 15, 0, 16, 0, 0, 4, 4]) + bytes([0x11] * 128), struct.pack(">16H", 0, 0x1085, *([0] * 14)))[1]   # (0E0E0E)
        if p == "white": return F.pair(bytes([15, 15, 0, 16, 0, 0, 4, 4]) + bytes([0x11] * 128), struct.pack(">16H", 0, 0xFFFF, *([0] * 14)))[1]
        return F.pair(wfile(p + 1), wfile(p))[1]
    mesh = lambda m: F.one(encode(m))
    E = mesh(empty()); W = pal_of("white")
    R = {}
    # the match's stage set
    st = stage_parts(Vn)
    R["set"] = [(n, mesh(m), pal_of(p)) for n, m, p in st]
    R["tron_match"] = R["tron_close"] = None
    if Vn.screen_model is not None:
        scr = Mesh_from_file(encode(next(m for n, m, p in st if n == "screen")))
        scr_pic = next(p for n, m, p in st if n == "screen")
        sig = fold(scr_pic[1][8:8 + 2048])
        R["tron_match"] = (min(q[0] for q in scr.pts), max(q[0] for q in scr.pts), min(q[1] for q in scr.pts), max(q[1] for q in scr.pts)) + sig
    # the close scene
    cl = close_parts(Vn)
    R["close"] = [(n, mesh(m), pal_of(p)) for n, m, p in cl]
    if Vn.screen_model is not None:
        cs = Mesh_from_file(encode(next(m for n, m, p in cl if n == "m%d" % Vn.screen_model)))
        R["tron_close"] = (min(q[0] for q in cs.pts), max(q[0] for q in cs.pts), min(q[1] for q in cs.pts), max(q[1] for q in cs.pts)) + sig
    # round the ring
    L = ring_lists(Vn)
    R["A"] = (mesh(L["A"][0][0]), mesh(caged(L["A"][0][0])), pal_of(L["A"][0][1]))
    ap = L["apron"] + [(empty(), "white")] * (8 - len(L["apron"]))
    R["apron_mesh"] = [mesh(m) for m, p in ap]; R["apron_mesh_cage"] = [mesh(caged(m)) for m, p in ap]
    R["apron_pal"] = [pal_of(p) for m, p in ap]
    R["floor_mesh"] = [mesh(m) for m, p in L["floor"]] + [mesh(caged(m)) for m, p in L["floor"]]
    R["floor_pal"] = [pal_of(p) for m, p in L["floor"]]
    bar = L["barricade"] + [(empty(), "white")] * (10 - len(L["barricade"]))
    R["barricade_mesh"] = [mesh(m) for m, p in bar]; R["barricade_pal"] = [pal_of(p) for m, p in bar]
    R["corner_mesh"] = [mesh(m) for m, p in L["corner"]] + [mesh(m.map(lambda q: (q[0] - 28, q[1], q[2] + 28))) for m, p in L["corner"]]
    R["corner_pal"] = [pal_of(p) for m, p in L["corner"]]
    for name, n, two in GROUPS:
        items = L[name]
        assert len(items) <= n, (name, len(items))
        items = items + [(empty(), ("white", "white") if two else "white")] * (n - len(items))
        R[name + "_mesh"] = [mesh(m) for m, p in items]
        if two:
            R[name + "_pal_a"] = [pal_of(p[0]) for m, p in items]; R[name + "_pal_b"] = [pal_of(p[1]) for m, p in items]
        else: R[name + "_pal_a"] = [pal_of(p) for m, p in items]
    R["upper_mesh"], R["upper_pal"], R["truss_mesh"] = [E], [W], [E] * 5
    # the ARENA page / VS backdrop
    R["label"] = F.one(arenas.label(Vn.name))
    tiles, _ = preview(Vn); R["preview"] = arenas.first_of(F, tiles)
    bd, _ = backdrop(Vn); R["backdrop"] = arenas.first_of(F, bd)
    write_header(R, k_arena, Vn.name)
    return R

def write_header(R, k_arena, NAME=NAME):
    def arr(name, typ, vals, note=""):
        return "static const %s %s[%d] = { %s };%s" % (typ, name, len(vals), ", ".join("0x%X" % v for v in vals), ("   // " + note) if note else "")
    L = ["// generated by gen/arenas_wm.py: WrestleMania 2000's RAW IS WAR as the arcade's arena \"%s\" (data/wmarena/WMARENA.md)" % NAME,
         "// meshes / palettes (the texture: the palette's file + 1); every WM2000 mesh has byte 1 bit 7 (No Mercy's combiner",
         "// FC15FE2B: the picture as it is, as WM2000 draws its arena)",
         "#define XWM_NAME \"%s\"" % NAME, "#define XWM_TABLE %d   // (No Mercy's announce table, steps, JR and King kept: arena_ring.c xring_table)" % (1 if TABLE else 0),
         "// the match's stage set (the base venue's 0x800507FC / 0x80050824 / 0x8005084C): WM2000's 3D entrance set + its ramp",
         "#define XWM_SET_N %d" % len(R["set"]),
         arr("xwm_set_mesh", "u16", [m for n, m, p in R["set"]]), arr("xwm_set_pal", "u16", [p for n, m, p in R["set"]]),
         "// (%s)" % ", ".join(n for n, m, p in R["set"]),
         "// the entrances' close scene (scene index XARENA_BASE: 0x80052280 / 0x800522BC / 0x800522F8 / 0x80052334, place 0x80050690)",
         "#define XWM_CLOSE_N %d" % len(R["close"]),
         arr("xwm_close_mesh", "u16", [m for n, m, p in R["close"]]), arr("xwm_close_pal", "u16", [p for n, m, p in R["close"]]),
         "static const float xwm_close_place[4] = { 0.0f, 0.0f, 0.0f, 0.0f };   // (the meshes are placed already: CLOSE_MOVE)",
         "// round the ring (src/arena_ring.c's lists)",
         "static const u16 xwm_logo[3] = { 0x%X, 0x%X, 0x%X };   // list A: WM2000's mat (0x5C2), the cage's, its picture" % R["A"],
         arr("xwm_apron_mesh", "u16", R["apron_mesh"], "0x800507B4: WM2000's 4 aprons, EMPTY x 4"),
         arr("xwm_apron_mesh_cage", "u16", R["apron_mesh_cage"]), arr("xwm_apron_pal", "u16", R["apron_pal"], "0x800507D4[v]"),
         arr("xwm_floor_mesh", "u16", R["floor_mesh"], "0x80050064: 2, the cage's 2"), arr("xwm_floor_pal", "u16", R["floor_pal"]),
         arr("xwm_barricade_mesh", "u16", R["barricade_mesh"], "0x80050070: per side 2 (the stage side's middle open)"),
         arr("xwm_barricade_pal", "u16", R["barricade_pal"]),
         arr("xwm_corner_mesh", "u16", R["corner_mesh"], "0x800500A0: post, pad, hook; the cage's"), arr("xwm_corner_pal", "u16", R["corner_pal"])]
    for name, n, two in GROUPS:
        L.append(arr("xwm_%s_mesh" % name, "u16", R[name + "_mesh"]))
        L.append(arr("xwm_%s_pal_a" % name, "u16", R[name + "_pal_a"]))
        if two: L.append(arr("xwm_%s_pal_b" % name, "u16", R[name + "_pal_b"]))
    L += [arr("xwm_upper_mesh", "u16", R["upper_mesh"]), arr("xwm_upper_pal", "u16", R["upper_pal"]),
          arr("xwm_truss_mesh", "u16", R["truss_mesh"]),
          "static const u8 xwm_rope_rgb[3][4] = { %s };" % ", ".join("{ %s }" % ", ".join("0x%02X" % c[k] for c in ROPE_RGB) for k in range(3)),
          "// the ARENA page: label (128 x 32 CI4), the photo's six tiles (first + k), the VS backdrop's forty (first + t)",
          "#define XWM_LABEL_FILE 0x%X" % R["label"], "#define XWM_PREVIEW_FILE 0x%X" % R["preview"], "#define XWM_BACKDROP_FILE 0x%X" % R["backdrop"],
          "// tron_panels_all.h rows (venue = the arcade's arena %d): the screen's extent in its mesh, its picture's fold" % k_arena,
          "#define XWM_TRON_MATCH { %d, %d, %d, %d, %d, 0x%08Xu, 0x%08Xu, 0 }" % ((k_arena,) + R["tron_match"]) if R["tron_match"] else "// (no screen)",
          "#define XWM_TRON_CLOSE { %d, %d, %d, %d, %d, 0x%08Xu, 0x%08Xu, 0 }" % ((k_arena,) + R["tron_close"]) if R["tron_close"] else ""]
    os.makedirs(os.path.dirname(OUT_H), exist_ok=True)
    open(OUT_H, "w").write("\n".join(L) + "\n")

# ---- the integration's entry points (data/wmarena/integrate.patch: gen/arenas.py, gen/arenas_ring.py, gen/arenas_entrance.py) ----
def _pic(p):
    """a picture as ("pic", texture, palette): a WM2000 palette file (texture + 1), a (texture, palette) pair, "hook", a pic"""
    if isinstance(p, tuple) and p[0] == "pic": return p
    if isinstance(p, tuple): return ("pic", wfile(p[0]), wfile(p[1]))
    if p == "hook": return ("pic", bytes([15, 15, 0, 16, 0, 0, 4, 4]) + bytes([0x11] * 128), struct.pack(">16H", 0, 0x1085, *([0] * 14)))
    return ("pic", wfile(p + 1), wfile(p))

def stage_pieces(v=0):
    """the match's stage set as gen/arenas.py's stage(v) gives Revenge's: [(mesh bytes, ("pic", texture, palette))]"""
    return [(encode(m), _pic(p)) for n, m, p in stage_parts(v)]

def close_pieces(v=0):
    """the close scene as gen/arenas_entrance.close_pieces(v) gives it: [(mesh bytes, ("pic", ...))], placed already"""
    return [(encode(m), _pic(p)) for n, m, p in close_parts(v)]

def arena_row(F, name=None, v=0):
    """gen/arenas.py build()'s row for this arena: (name, "wm", set meshes, set palettes, apron palettes [8], truss
    palettes [5], label, preview, backdrop) -- the apron palettes: arena_ring's (its 4 aprons, white); the truss: No
    Mercy's own (arena_ring empties the truss meshes while ours is built)"""
    import arenas
    name = name or venue(v).name
    meshes, pals = [], []
    for mb, p in stage_pieces(v):
        meshes.append(F.one(mb)); pals.append(arenas.pic_pair(F, p)[1])
    L = ring_lists(v)
    ap = [arenas.pic_pair(F, _pic(p))[1] for m, p in L["apron"]]
    white = arenas.pic_pair(F, ("pic", bytes([15, 15, 0, 16, 0, 0, 4, 4]) + bytes([0x11] * 128), struct.pack(">16H", 0, 0xFFFF, *([0] * 14))))[1]
    lab = F.one(arenas.label(name))
    prev = arenas.first_of(F, preview(v)[0]); back = arenas.first_of(F, backdrop(v)[0])
    return (name, "wm" if v == 0 else "wm%d" % v, meshes, pals, ap + [white] * (8 - len(ap)), [0x80B, 0x80B, 0x80D, white, white], lab, prev, back)

def ring_row(F, mesh, pal, v=0):
    """gen/arenas_ring.py build()'s per-arena lists for this arena: {list: [(mesh id, palette id(s))]} -- mesh(Mesh) and
    pal(picture) are arenas_ring's makers (its pack)"""
    L = ring_lists(v)
    out = {"logo": (mesh(L["A"][0][0]), mesh(caged(L["A"][0][0])), pal(_pic(L["A"][0][1])))}
    out["apron"] = [mesh(m) for m, p in L["apron"]]; out["apron_cage"] = [mesh(caged(m)) for m, p in L["apron"]]
    out["apron_pal"] = [pal(_pic(p)) for m, p in L["apron"]]
    out["floor"] = [mesh(m) for m, p in L["floor"]] + [mesh(caged(m)) for m, p in L["floor"]]
    out["floor_pal"] = [pal(_pic(p)) for m, p in L["floor"]]
    out["barricade"] = [(mesh(m), pal(_pic(p))) for m, p in L["barricade"]]
    out["corner"] = [mesh(m) for m, p in L["corner"]] + [mesh(m.map(lambda q: (q[0] - 28, q[1], q[2] + 28))) for m, p in L["corner"]]
    out["corner_pal"] = [pal(_pic(p)) for m, p in L["corner"]]
    for name, n, two in GROUPS:
        out[name] = [(mesh(m), (pal(_pic(p[0])), pal(_pic(p[1]))) if two else (pal(_pic(p)),) * 2) for m, p in L[name]]
    out["set"] = [(mesh(m), pal(_pic(p))) for n, m, p in stage_parts(v)]
    out["rope_rgb"] = ROPE_RGB
    out["empty"] = mesh(empty())
    out["table"] = 1 if TABLE else 0
    return out

def tron_rows(k_arena, v=0):
    """the two tron_panels_all.h rows (C text): the match's screen and the close scene's ([] for a venue without a screen)"""
    Vn = venue(v)
    if Vn.screen_model is None: return []
    st, cl = stage_parts(v), close_parts(v)
    from arenas_ring import Mesh_from_file
    def ext(m):
        q = Mesh_from_file(encode(m)) if not is_alt(m) else m
        return (min(p[0] for p in q.pts), max(p[0] for p in q.pts), min(p[1] for p in q.pts), max(p[1] for p in q.pts))
    sm = next((m, p) for n, m, p in st if n == "screen"); cm = next((m, p) for n, m, p in cl if n == "m%d" % Vn.screen_model)
    sig = fold(sm[1][1][8:8 + 2048])
    return ["    { %d, %d, %d, %d, %d, 0x%08Xu, 0x%08Xu, 0 },   // %s: match -- WM2000's 3D set's big screen (its %s picture)" % ((k_arena,) + ext(sm[0]) + sig + (Vn.name, "RAW IS WAR" if Vn.v == 0 else "own")),
            "    { %d, %d, %d, %d, %d, 0x%08Xu, 0x%08Xu, 0 },   //   ... close-up: the same screen in the close scene" % ((k_arena,) + ext(cm[0]) + sig)]

class DryFiles:
    """gen/wmport.py's Files for a dry run (IDs from DRY_FILE0; nothing goes into the game's pack)"""
    def __init__(self, file0=DRY_FILE0): self.data = []; self.by = {}; self.file0 = file0
    def one(self, b):
        if b not in self.by: self.by[b] = self.file0 + len(self.data); self.data.append(b)
        return self.by[b]
    def pair(self, tex, pal):
        key = ("pair", tex, pal)
        if key not in self.by: self.by[key] = self.file0 + len(self.data); self.data += [pal, tex]
        q = self.by[key]; return q + 1, q
    def get(self, i): return self.data[i - self.file0]

if __name__ == "__main__":
    F = DryFiles()
    vv = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    if vv: OUT_H = OUT_DIR + "/arena_wm%d_files.h" % vv
    R = build(F, v=vv)
    import pickle
    pickle.dump({"file0": F.file0, "data": F.data, "R": R}, open(OUT_DIR + ("/arena_wm_dry.pkl" if not vv else "/arena_wm%d_dry.pkl" % vv), "wb"))
    print("files %d, %d bytes -> %s" % (len(F.data), sum(len(b) for b in F.data), OUT_H))
    print("stage set %d parts: %s" % (len(R["set"]), ", ".join(n for n, m, p in R["set"])))
    print("close scene %d parts" % len(R["close"]))
    for name, n, two in GROUPS: print("%s: %d of %d" % (name, sum(1 for m in R[name + "_mesh"] if m != R["upper_mesh"][0]), n))
    print("tron match", R["tron_match"], "close", R["tron_close"])
