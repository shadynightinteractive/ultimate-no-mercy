"""An import's forearms meet No Mercy's hands (2026-11-16, womenfix2 round 2).

The user, on the imported women after womenfix2: "Hands look off like they're not attached to the arm, they aren't aligned."

No Mercy draws an import's hands with its own meshes: the descriptor's palm and fingers at rest (hand pose 0) and its shared
hand-pose meshes in moves (fists, grabs: mod/src/hands.c), all made to fit No Mercy's own forearms -- on No Mercy's models the
palm's top ring sits exactly on the forearm's wrist end (0.0 off: Stephanie, The Rock ...). An import's forearm is its source
game's, and ends where that game's own palm begins: Revenge's women's wrists are 4.7 units in front (+z) of No Mercy's women's
palm 0x118A (their wrists are 6 wide), so the hand hung behind the arm; WrestleMania 2000's Jacqueline 4, Chyna 3; some men 2
(a man's wrist is 7-10 wide). Measured on every installed import: womenfix2 work\wrist_gaps_installed.txt.

The forearm's wrist end is its mesh's SECOND vertex group (mod/src/skel.c setup_wm: func_8000D284(11, 9, 12) /
(16, 17, 19) -- the second group is skinned to the hand's bone 9 / 17, so it turns with the hand). Those vertices are moved by
the gap, across only (x and z: the arm keeps its length; No Mercy's palm reaches 2 units up into the wrist, as on its own
models), onto the palm drawn there. Every No Mercy hand pose's palm has its top ring at the same place as the men's palm
(0x144D / 0x1457 = 0x1112), and the women's within 1 unit of it, so the wrist meets the hand at rest and in every move. A gap
under MIN_GAP (rounded, either axis) is left as it was: under a fifth of a wrist's width.
Only for a model with No Mercy's hands (SKEL_WM_NMHANDS); a model keeping its own source hands is left alone.
"""
import struct

MIN_GAP = 2
NMHANDS = 0x40000000
FLAGS_AT = 0x264
PAIRS = ((11, 9), (16, 17))        # (forearm, palm): right, left

def _s8(v): return v - 256 if v > 127 else v

def _verts(m):
    sc = (m[0] & 0x7F) + 1; nv = m[1] & 0x7F
    off = [_s8(x) * 16 for x in m[4:7]]
    return sc, [[(_s8(m[8 + 8 * j + q]) + off[q]) * sc for q in range(3)] for j in range(nv)]

def gap(fore, palm):
    """(dx, dz) rounded: the palm's top ring centre less the forearm's wrist group centre; None without a wrist group"""
    g1, g2 = fore[3] >> 4, fore[3] & 15
    if not g2: return None
    _, vf = _verts(fore); _, vp = _verts(palm)
    w = vf[g1:g1 + g2]
    top_y = max(v[1] for v in vp)
    top = [v for v in vp if v[1] >= top_y - 2]
    mean = lambda vs, q: sum(v[q] for v in vs) / len(vs)
    return int(round(mean(top, 0) - mean(w, 0))), int(round(mean(top, 2) - mean(w, 2)))

def moved(fore, dx, dz):
    """the forearm with its wrist group moved by (dx, dz) -- None if its format can't hold that"""
    d = bytearray(fore)
    sc = (d[0] & 0x7F) + 1; g1, g2 = d[3] >> 4, d[3] & 15
    if dx % sc or dz % sc: return None
    for j in range(g1, g1 + g2):
        o = 8 + 8 * j
        for q, dv in ((0, dx), (2, dz)):
            v = _s8(d[o + q]) + dv // sc
            if not -128 <= v <= 127: return None
            d[o + q] = v & 0xFF
    return bytes(d)

def fit_wrists(descs, pool, report, key, nmf):
    """each costume's forearms moved onto its palms (pool: the model's files; nmf(n): a cartridge file) -> descriptors"""
    out = []
    for c, d in enumerate(descs):
        d = bytearray(d)
        if struct.unpack_from(">I", d, FLAGS_AT)[0] & NMHANDS:
            for fore_k, palm_k in PAIRS:
                fm, pm = struct.unpack_from(">H", d, 8 * fore_k)[0], struct.unpack_from(">H", d, 8 * palm_k)[0]
                if not fm or not pm or not pool.ours(fm): continue
                fore = pool.get(fm)
                palm = pool.get(pm) if pool.ours(pm) else nmf(pm)
                g = gap(fore, palm)
                if g is None or max(abs(g[0]), abs(g[1])) < MIN_GAP: continue
                new = moved(fore, *g)
                if new is None:
                    report.append("%s %d: part %d's wrist %s off its palm, can't be moved" % (key, c, fore_k, g)); continue
                struct.pack_into(">H", d, 8 * fore_k, pool.one(new))
                report.append("%s %d: part %d's wrist moved (%+d, %+d) onto its palm (%04X)" % (key, c, fore_k, g[0], g[1], pm))
        out.append(bytes(d))
    return out
