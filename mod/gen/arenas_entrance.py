"""WCW/nWo Revenge's arenas in the entrances' close shots (src/arena_entrance.c; data/arenaport/NM_ENTRANCE.md).

No Mercy's entrance close-up (a Superstar on the stage, the camera close: entrance steps 5 / 11) is not the arena: the
entrance overlay builds a scene of its own for it, "other scene" venue + 10 (func_8010B778 -> func_8001B3E4(venue +
10) -> func_8001F2D8 / func_80020B58, index i = venue), hides the arena (func_8001E7A4(0)) and shows it
(func_8002173C(1)) for the close shot. Its pieces come from tables of their own, indexed by i, not the stage-set
tables arena.c repoints -- so with one of ours in venue XARENA_BASE the close shots showed that venue's own (7:
WrestleMania's letters, truss and crowd, files 0xD20-0xD56).

The close scene's world: the Superstar stands at the origin, facing -z; the camera is at about (+-215, 320, -1120)
looking at him (read off the view at 0x80069890 in both venues 0 and 7); the scene's pieces are moved by the
venue's place (0x80050690 + 16 i: x, y, z, a turn; WrestleMania (0, 0, 310, 0)). Units are the arena's (Revenge's
and No Mercy's are the same), so Revenge's stage set goes in as it is, moved so the spot where Revenge's
Superstars stand in its close shots comes to the origin (PLACE).

The pieces: Revenge's stage list (arenas.table: 0x80036CB4 ..) without its empty root, the flat ones on the floor
first (no depth buffer: one node, drawn in list order), then the rest in Revenge's order; plus Revenge's aisle floor
and its two barricades (0x80036C8C: 2F5 311 313, the arena's aisle pictures). No 15-piece limit here: the close
scene's parts are 0x8007D890.. (392 of 0x34 bytes before the next node, 0x80082848). Called by gen/wmport.py
build_all after arenas.build (the files go in the same pack); writes src/arena_entrance_files.h."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import arenas

OUT_H = nmp("/home/devbox/work/arcade/src/arena_entrance_files.h")
CLOSE_MAX = 48              # (ours: Nitro 31, Havoc 31; the game's own most: RAW IS WAR 73)
AISLE = (0x2F5, 0x311, 0x313)   # Revenge's aisle meshes (0x80036C8C), their pictures the arena's aisle list
# where Revenge's Superstars stand in its close shots (Revenge's units, its stage at +z): the scene is moved by
# minus it. Nitro: in front of the truss gate (0x361/0x362 at z 1270) between the big WCW letters (z 1130..1260);
# Havoc: on the HALLOWEEN HAVOC floor logo (0x394, z 1260..1560) in front of the face (z 1480). Rig-checked
# (data/arenaport/NM_ENTRANCE.md section 5).
STAND = {0: (0, 0, 1100), 4: (0, 0, 1250), 3: (0, 0, 1200)}   # (Bash at the Beach: on the sand before the hut and palms, the sign behind)

def close_ids(v):
    """[(Revenge's mesh file, its picture's palette file (texture = + 1))] in drawing order, for Revenge's arena v"""
    t = arenas.table(v)
    pieces = [(m, p) for m, p in zip(t["meshes"], t["pals"]) if m != arenas.STAGE_ROOT]
    pieces += list(zip(AISLE, t["aisle"]))
    flat = lambda m: {y for _, y, _ in arenas.points(arenas.rfile(m))} == {0}
    pieces.sort(key=lambda mp: 0 if flat(mp[0]) else 1)       # (stable: Revenge's order otherwise)
    return pieces

# Revenge's pieces are all vertex colour 73 grey (Revenge lights them brighter); No Mercy's close scenes' own pieces
# run up to 223..255 (WrestleMania's 0xD30: 85 and 223; RAW's 0x57B: 42 and 223), and the entrance light multiplies
# them -- at 73 the stage is a dark shape behind the Superstar (rig shot cap/nitro_s5_60.png). BRIGHT: the colour
# they get here (None: as they are).
BRIGHT = 200

def lit(d, c=BRIGHT):
    """a mesh's vertex colours all c (Revenge's flat 73 grey made No Mercy's light)"""
    if c is None: return d
    d = bytearray(d); nv = d[1] & 0x7F
    for i in range(nv): d[8 + 8 * i + 5:8 + 8 * i + 8] = bytes((c, c, c))
    return bytes(d)

def close_pieces(v):
    """[(mesh bytes, picture: Revenge's palette file or gen/arenas_stage3d.py's)] in drawing order: Revenge's 3D stage
    where there is one (its models as they are, placed in arenas_stage3d), else its flat set"""
    import arenas_stage3d
    import arenas_wm
    if arenas_wm.wm_venue(v) is not None:   # (WrestleMania 2000's 3D entrance sets: gen/arenas_wm.py, placed in their points)
        return arenas_wm.close_pieces(arenas_wm.wm_venue(v))
    if v in arenas_stage3d.STAGE3D: return arenas_stage3d.close_parts(v)
    return [(lit(arenas.rfile(m)), p) for m, p in close_ids(v)]

def place(v):
    import arenas_stage3d
    import arenas_wm
    if arenas_wm.wm_venue(v) is not None or v in arenas_stage3d.STAGE3D: return (0.0, 0.0, 0.0, 0.0)     # (placed in its points)
    x, y, z = STAND[v]
    return (float(-x), float(-y), float(-z), 0.0)

def build(F, rows=None):
    """the close scenes' files into F (gen/wmport.py's Files, after arenas.build: the same pictures are shared);
    writes OUT_H. rows: arenas.build's (to keep its order); None: arenas.ARENAS."""
    order = [(r[0], r[1]) for r in rows] if rows else arenas.ARENAS
    out = []
    for name, v in order:
        meshes, pals = [], []
        for mb, p in close_pieces(v):
            _, pal = arenas.pic_pair(F, p)
            meshes.append(F.one(mb)); pals.append(pal)
        assert len(meshes) <= CLOSE_MAX, (name, len(meshes))
        out.append((name, v, meshes, pals, place(v)))
    L = ["// generated by gen/arenas_entrance.py: WCW/nWo Revenge's arenas in the entrances' close shots (arena_entrance.c):",
         "// the close scene of venue XARENA_BASE (\"other scene\" index XARENA_BASE: 0x80052280 ..) made each one's stage",
         "#define XCLOSE_MAX %d" % CLOSE_MAX,
         "static const s16 xclose_n[XARENA_N] = { %s };" % ", ".join(str(len(r[2])) for r in out),
         "static const u16 xclose_mesh[XARENA_N][XCLOSE_MAX] = {"]
    L += ["    { %s },   // %s (arena %s)" % (", ".join("0x%X" % x for x in r[2]), r[0], r[1]) for r in out]
    L += ["};", "static const u16 xclose_pal[XARENA_N][XCLOSE_MAX] = {"]
    L += ["    { %s }," % ", ".join("0x%X" % x for x in r[3]) for r in out]
    L += ["};", "static const float xclose_place[XARENA_N][4] = {   // (0x80050690 + 16 i: x, y, z, the turn)"]
    L += ["    { %s }," % ", ".join("%.1ff" % x for x in r[4]) for r in out]
    L += ["};"]
    open(OUT_H, "w").write("\n".join(L) + "\n")
    return out

if __name__ == "__main__":         # (a look: the lists, no files written)
    for name, v in arenas.ARENAS:
        if isinstance(v, str): continue      # (WrestleMania 2000's)
        ids = close_ids(v)
        print(name, len(ids), "pieces, place", place(v))
        print("  mesh", " ".join("%X" % m for m, _ in ids))
        print("  pal ", " ".join("%X" % p for _, p in ids))
