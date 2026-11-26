"""wmmodels.py (romimport, 2026-11-18): WrestleMania 2000's models built by WrestleMania 2000's own code from the ROM, as
RAM images in place of the rig's match captures -- the ones the WM2000 importer (handlers_rig\\wm2000\\cap\\all_c*_ram.bin,
rig\\wm_capture_all.py's Tag Matches: 4 men a match) and gen\\wmport.py (wmport\\cap\\k<k>_c<c>\\ram.bin, wmcap.py: man k's
record in Jarrett's place 29, loaded as Jarrett's Select ID 0x406) read.

How (all of it the game's code in mipsemu, on images made from the ROM by load maps):
 1. the Superstar Select image (loadmaps\\wm_sel1.json): the game's 0x800F6180 (its move tables, files 0x93 / 0x94 / 0x3AD)
    then its roster builder 0x8000ADA4: every roster place's 4 costume records (66 bytes, 0x80090320) and its 188-byte
    record (the table *0x8004138C) -- both equal to the capture's, byte for byte;
 2. each man in the match gets his participant struct (244 bytes at 0x8008DC30 + 244 i, his ID in the list 0x800B0FC0, his
    costume byte at 0x8009E8F0 + i) from the game's own 0x8000B048 on that image (as its Select does when the match starts);
    the model loader reads the scale byte there (+3, via 0x80009A34);
 3. the match image (loadmaps\\wm_match.json) + the costume records' packed looks + the participants, the object pool given a
    scratch base (0x80700000; its counts 0) -> the per-slot loader 0x800144C0(slot, ID, costume), one slot after another.
Hooks: file info 0x80003DD4 / read 0x80003E98 (the unpacked file from the ROM), malloc 0x80000898 (a bump heap), free
0x800009A0 (nothing), 0x8002A250 / 0x8002A2C0 (0).

The plan of the rig's matches (which men, costumes and slots a file holds) is our data: frozen\\wm2000_rig_index.json.

    python wmmodels.py <data folder> [--check <the project's data folder>] [--only rig|wmport]

--check compares, per model, what the consumers read (wmport.capture's descriptor and every part's mesh / picture /
palette via nm_descriptor, the slot's scale floats, the hands' kinds table) with the capture's."""
import json, os, struct, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(SRC, "rig", "harness"))
import images, mipsemu
from roms import Roms

ROSTER = 0x800411B0            # roster place -> record pointer (76)
COSTUMES = 0x80090320          # 4 x 66-byte costume record a place; packed looks at +45 (21 bytes)
RECORDS = 0x80294A10           # the 188-byte records (the table *0x8004138C; this address = the capture's heap block)
PART_LIST, PART_STRUCT, PART_COSTUME = 0x800B0FC0, 0x8008DC30, 0x8009E8F0
POOL = 0x80700000
SCALE0, SCALE_STRIDE, HAND_KINDS = 0x8005B138, 0xC0, 0x80041CC4

def emu(img, F, heap):
    E = mipsemu.Emu(img)
    H = [heap]
    def malloc(e): n = (e.r[4] + 15) & ~15; e.r[2] = H[0]; H[0] += n; return True
    def rd_info(e): e.w32(e.r[4] + 8, len(F(e.r[5] & 0xFFFF))); e.r[2] = e.r[4]; return True
    def rd_file(e): d = F(e.r[5] & 0xFFFF); e.w32(e.r[4] + 8, len(d)); e.wbytes(e.r[6], d); e.r[2] = e.r[4]; return True
    def ret0(e): e.r[2] = 0; return True
    E.hooks.update({0x80000898: malloc, 0x800009A0: lambda e: True, 0x80003DD4: rd_info, 0x80003E98: rd_file,
                    0x8002A250: ret0, 0x8002A2C0: ret0})
    E.heap = H
    return E

def select_built(R, game="wm2000", sel_map="wm_sel1"):
    """the Select image with the game's roster built (step 1): an Emu whose memory holds it"""
    F = R.files(game)
    E = emu(images.build(json.load(open(os.path.join(HERE, "loadmaps", sel_map + ".json"))), R), F, 0x80600000)
    E.call(0x800F6180, [], max_steps=50_000_000)
    E.w32(0x8004138C, RECORDS); E.wbytes(RECORDS, bytes(76 * 188))
    E.call(0x8000ADA4, [], max_steps=300_000_000)
    return E

def participant(S, gid, c):
    """his 244-byte participant struct: the game's 0x8000B048 on the built Select image"""
    snap = bytes(S.m[0x600000:0x800000])
    S.wbytes(0x807A0000, bytes(244)); S.call(0x8000B048, [gid, c, 0x807A0000], max_steps=20_000_000)
    p = S.rbytes(0x807A0000, 244)
    S.m[0x600000:0x800000] = snap
    return p

def match_base(R, S, match_map="wm_match"):
    m = bytearray(images.build(json.load(open(os.path.join(HERE, "loadmaps", match_map + ".json"))), R))
    for i in range(76 * 4):
        a = (COSTUMES & 0x7FFFFF) + 66 * i + 45; m[a:a + 21] = S.m[a:a + 21]
    struct.pack_into(">I", m, 0x55C10, POOL)          # the object pool (0x800018FC): our scratch block, empty
    struct.pack_into(">hh", m, 0x56580, 0, 0)         # (its count; the registered list's count)
    return bytes(m)

def ids_of(img):
    r32 = lambda a: struct.unpack_from(">I", img, a & 0x7FFFFF)[0]
    return {k: struct.unpack_from(">H", img, (r32(ROSTER + 4 * k) + 4) & 0x7FFFFF)[0] for k in range(76)}

def load_match(base, F, men, parts, swap=None):
    """men: [(ID, costume)] by slot; parts: their participant structs -> the match RAM after the loader for every slot"""
    m = bytearray(base)
    if swap is not None:                               # (wmcap.py: man k's record pointer in place 29, and 29's in k's)
        k = swap; a, b = (ROSTER & 0x7FFFFF) + 4 * k, (ROSTER & 0x7FFFFF) + 4 * 29
        m[a:a + 4], m[b:b + 4] = m[b:b + 4], m[a:a + 4]
    lst = [0xFF] * 40
    for i, ((gid, c), p) in enumerate(zip(men, parts)):
        lst[i] = gid
        a = (PART_STRUCT & 0x7FFFFF) + 244 * i; m[a:a + 244] = p
        m[(PART_COSTUME & 0x7FFFFF) + i] = c
    struct.pack_into(">40H", m, PART_LIST & 0x7FFFFF, *lst)
    E = emu(bytes(m), F, 0x80600000)
    for slot, (gid, c) in enumerate(men):
        E.call(0x800144C0, [slot, gid, c], max_steps=200_000_000)
    return bytes(E.m)

def rig_plan():
    idx = json.load(open(os.path.join(HERE, "frozen", "wm2000_rig_index.json")))
    files = {}
    for key, (fn, slot) in idx.items():
        k, c = map(int, key.split(":")); files.setdefault(fn, {})[slot] = (k, c)
    return {fn: [s[i] for i in sorted(s)] for fn, s in sorted(files.items())}

def use(m, slot):
    """what the consumers read of a slot"""
    sys.path.insert(0, os.path.join(SRC, "mod", "gen"))
    import wmport as W
    d, ps = W.capture(m, slot)
    sc = [struct.unpack_from(">f", m, (SCALE0 + SCALE_STRIDE * slot + o) & 0x7FFFFF)[0] for o in (0, 0x14, 0x28)]
    return W.nm_descriptor(d), [(p["k"], p["mesh"], p["tex"], p["pal"]) for p in ps], sc, bytes(m[HAND_KINDS & 0x7FFFFF:(HAND_KINDS & 0x7FFFFF) + 0x40])

def main():
    out = sys.argv[1]
    ref = sys.argv[sys.argv.index("--check") + 1] if "--check" in sys.argv else None
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    R = Roms(); F = R.files("wm2000")
    bad = 0; n = 0
    def check(rel, m, slots):
        nonlocal bad, n
        if not ref: return ""
        cap = open(os.path.join(ref, rel), "rb").read()
        res = []
        for s in slots:
            a, b = use(cap, s), use(m, s)
            res.append("SAME" if a == b else "DIFFERENT(%s)" % ",".join(nm for nm, x, y in zip(("descriptor/parts", "parts", "scale", "kinds"), a, b) if x != y))
        n += len(slots); bad += sum(1 for r in res if r != "SAME")
        return " ".join(res)
    if only in (None, "wmport", "rig"):
        t = time.time(); S = select_built(R); print("roster built by the game (%.0f s)" % (time.time() - t))
        base = match_base(R, S); ids = ids_of(base)
    if only in (None, "wmport"):
        for k in (29, 41):
            for c in range(4):
                rel = os.path.join("wmport", "cap", "k%d_c%d" % (k, c), "ram.bin")
                men = [(0x406, c)]
                m = load_match(base, F, men, [participant(S, 0x406, c)], swap=k)
                p = os.path.join(out, rel); os.makedirs(os.path.dirname(p), exist_ok=True); open(p, "wb").write(m)
                print("%-46s %s" % (rel, check(rel, m, [0])))
    if only in (None, "rig"):
        for fn, men_kc in rig_plan().items():
            men = [(ids[k], c) for k, c in men_kc]
            m = load_match(base, F, men, [participant(S, g, c) for g, c in men])
            rel = os.path.join("handlers_rig", "wm2000", "cap", fn)
            p = os.path.join(out, rel); os.makedirs(os.path.dirname(p), exist_ok=True); open(p, "wb").write(m)
            print("%-46s %s" % (rel, check(rel, m, list(range(len(men))))), flush=True)
        idx = os.path.join(out, "handlers_rig", "wm2000", "cap", "all_index.json")
        open(idx, "w").write(open(os.path.join(HERE, "frozen", "wm2000_rig_index.json")).read())
    if only in (None, "baddblood"):
        # Badd Blood (a WrestleMania 2000 mod, the same code at every address used here): imports2's Vader, rig\capture.py --
        # his Select ID 0x204 (place 19) in slot 0, the loader's costume forced to c (0x800146CC: lui $a1, c)
        Fb = R.files("baddblood")
        t = time.time(); Sb = select_built(R, "baddblood", "bb_vsel"); print("Badd Blood roster built by the game (%.0f s)" % (time.time() - t))
        bbase = match_base(R, Sb, "bb_match")
        for c in range(4):
            rel = os.path.join("handlers_rig", "imports2", "cap", "vader_c%d_ram.bin" % c)
            m = load_match(bbase, Fb, [(0x204, c)], [participant(Sb, 0x204, c)])
            p = os.path.join(out, rel); os.makedirs(os.path.dirname(p), exist_ok=True); open(p, "wb").write(m)
            print("%-46s %s" % (rel, check(rel, m, [0])), flush=True)
    if ref: print("models: %d checked, %s" % (n, "ALL SAME" if not bad else "%d DIFFERENT" % bad))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
