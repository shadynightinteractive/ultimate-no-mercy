"""movelib_rev.py (romimport, 2026-11-17): content\\movelib_revenge.bin from the player's ROMs alone.

    python movelib_rev.py <out file> [--check <reference .bin>]

What it reads: the WCW/nWo Revenge ROM and the No Mercy (Rev 1) ROM (roms.py, by fingerprint), and our own index
tools\\handlers\\handlers\\revenge\\library_index.json -- each library move's ID, the No Mercy move it is usually played on,
and the Revenge FILE NUMBERS of its pair and its hold block (no game bytes: numbers only). That index is the library
revlib.py made on 2026-11-13 (movelib2), frozen: its order, names and "base" moves came from steps (the 24 ports'
rows, the moveset mapping's float distances, movelib's 78 found in a pack that no longer holds them) that a fresh run
cannot remake; the bytes come from the ROMs every time.

The bytes, as revlib.py makes them (same layout, same rules -- revlib.py main, "movelib_revenge.bin"):
  - an animation = the Revenge file with its first byte made No Mercy's (B0: learned from the animations both games have,
    No Mercy files 0x3322..0x43FF against Revenge 0x110B..0x1CFF, the most usual value; gen/revanims.py byte0_table);
  - its 36-byte record = Revenge's record (file 0x20F, from file 0xE39) mapped byte by byte to No Mercy's (file 0x2BF from
    0x2DD9: gen/revanims.py rec_conv, with its REC_SOUND override);
  - (2026-11-20, revmoves2: the cross-reference) a Revenge animation that No Mercy has too -- the same bytes but the first
    (xref: 1,538 of the library's 2,064 files) -- IS No Mercy's: its bytes and its record are the No Mercy file's (of
    several, the one whose record agrees most with Revenge's own; the lowest number among equals). Only the others are
    converted, by rules measured on the 3,110 files both games have (anim_conv): the first byte's end-pose kind by
    Revenge's kind (the kind of No Mercy's file for 76.6% of them, standing / lying / other for 98.4%), its turn bits
    kept (99.6%); the record's end level (byte 0, low half) kept (97.8%), the rest of byte 0 and bytes 1 .. 35 by the
    value No Mercy's file has for Revenge's (87.5% .. 100% a byte; 93.4% of all bytes), the frames kept, the sounds by
    their own mapping (and REC_SOUND). Before, every file was converted -- a twin's record too, with byte 0 whole (its
    end level wrong for 1 in 13 of them);
  - one copy of each animation; a pair two in a row; a hold block 8 in a row;
  - (2026-11-20, revmoves) a row's "fx": the move's animations after its pair -- No Mercy asks a double team's lead's move for
    the man's (file + 2) and a turnbuckle dive's for its landings and miss (+ 2 .. 4): Revenge has them after the pair, in
    its Superstars' lists as in its files (0x1C93 / 94 / 95; 0x1BC5 .. 0x1BC9). Fresh copies after all the others, named
    after the data: "NMLIBX01", u32 count, { u32 row, u32 first animation, u32 how many } each (the exe's
    move_library_read; an exe or a reader before it reads nothing past the data). The moves, IDs and stamp don't change.
No capture, saved state, pickle or extracted folder is read."""
import collections, hashlib, json, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from roms import Roms, SRC

INDEX = os.path.join(SRC, "tools", "handlers", "handlers", "revenge", "library_index.json")

# (gen/revanims.py's constants)
REV_RECS, REV_REC0 = 0x020F, 0xE39
NM_RECS, NM_REC0, NM_RECN = 0x2BF, 0x2DD9, 0x169B
FRAME_AT = (15, 16, 18, 20, 22, 24, 26, 28, 30, 32)
SOUND_AT = (20, 22, 24, 26, 28, 30)
REC_SOUND = {0x1545: {0x8D: 0x1D}}       # (ours: Nash's jackknife's thud, the user 2026-10-30)

def most(c):
    """Counter.most_common(1)[0][0]: the largest count, the first inserted among equals"""
    return c.most_common(1)[0][0]

def byte0_table(NM, RV):
    nm = {}
    for f in range(0x3322, 0x4400):
        d = NM(f)
        if len(d) > 8: nm.setdefault(d[1:], collections.Counter())[d[0]] += 1
    m = collections.defaultdict(collections.Counter)
    for f in range(0x110B, 0x1D00):
        d = RV(f)
        if len(d) > 8 and d[1:] in nm:
            for b, c in nm[d[1:]].items(): m[d[0]][b] += c
    return {a: most(c) for a, c in m.items()}

def rec_conv(NM, RV):
    RT, NT = RV(REV_RECS), NM(NM_RECS)
    rv = {}
    for f in range(REV_REC0, REV_REC0 + len(RT) // 36):
        d = RV(f)
        if len(d) > 8: rv.setdefault(d[1:], []).append(f)
    m = [collections.defaultdict(collections.Counter) for k in range(36)]
    sm = collections.defaultdict(collections.Counter)
    for f in range(NM_REC0, NM_REC0 + NM_RECN):
        d = NM(f)
        if len(d) <= 8 or d[1:] not in rv: continue
        nr = NT[36 * (f - NM_REC0):36 * (f - NM_REC0) + 36]
        for r in rv[d[1:]]:
            rr = RT[36 * (r - REV_REC0):36 * (r - REV_REC0) + 36]
            for k in range(36): m[k][rr[k]][nr[k]] += 1
            for a in SOUND_AT:
                if nr[a] and nr[a] == rr[a]: sm[rr[a + 1]][nr[a + 1]] += 1
    conv = [{v: most(c) for v, c in mk.items()} for mk in m]
    snd = {v: most(c) for v, c in sm.items()}
    def rec(f):
        rr = RT[36 * (f - REV_REC0):36 * (f - REV_REC0) + 36]
        out = bytearray(conv[k].get(rr[k], rr[k]) for k in range(36))
        for k in FRAME_AT: out[k] = rr[k]
        for a in SOUND_AT: out[a + 1] = snd.get(rr[a + 1], rr[a + 1])
        for fr, s in REC_SOUND.get(f, {}).items():
            for a in SOUND_AT:
                if rr[a] == fr: out[a + 1] = s
        return bytes(out)
    return rec

# (2026-11-20, revmoves2) The cross-reference and the measured rules. An animation's header word: the end pose's kind << 10 |
# a turn (10 bits: byte 0 = kind << 2 | the turn's top two bits); its record (36 bytes): byte 0 = the pose kind the camera
# reads << 4 | the end level (1 / 3 / 7 the mat, 2 the floor: settle_height_after_animation), then windows, frames, sounds.
KIND_SEEN = {15: 30}     # (Revenge's kind 15 is in no file both games have: 0x1BCC, No Mercy's dive 12430's third role, is
                         #  its 0x40CD but for byte 0 -- kind 30; ri_movelib.cpp the same)

def xref(NM, RV):
    """{Revenge animation file: its No Mercy twin} -- the file with the same bytes but the first (that byte is the header's
    end pose and turn, which the games number differently); of several, the one whose record agrees with Revenge's own in
    the most bytes, the lowest number among equals. From the ROMs, at every first start; never shipped."""
    T = {}
    for g in range(NM_REC0, NM_REC0 + NM_RECN):
        d = NM(g)
        if len(d) > 8: T.setdefault(d[1:], []).append(g)
    RT, NT = RV(REV_RECS), NM(NM_RECS)
    X = {}
    for f in range(REV_REC0, REV_REC0 + len(RT) // 36):
        d = RV(f)
        if len(d) <= 8 or d[1:] not in T: continue
        rr = RT[36 * (f - REV_REC0):36 * (f - REV_REC0) + 36]
        best = None
        for g in T[d[1:]]:
            s = sum(a == b for a, b in zip(NT[36 * (g - NM_REC0):36 * (g - NM_REC0) + 36], rr))
            if best is None or s > best[0]: best = (s, g)
        X[f] = best[1]
    return X

KEY = 0x62               # (an animation is its 4-byte header, then its key poses, 0x62 bytes each: revanims.py _keys)
def pose_class(k):
    """what state_after_animation makes of end pose k: L lying (20-47: 49), R rising (4: 50), C the corner (7, 8, 10: 24 /
    52), S standing (any other: 48, 51 or 1)"""
    return "L" if 20 <= k <= 47 else "R" if k == 4 else "C" if k in (7, 8, 10) else "S"

def last_key(d):
    n = (len(d) - 4) // KEY
    return d[4 + KEY * (n - 1):4 + KEY * n] if n > 0 else None

def nearest_kind(cands, key, cls):
    """the end-pose kind of the No Mercy animation whose last key pose is nearest key (the sum of the bytes' differences;
    the lowest-numbered among equals), among those whose end pose is of class cls; None when there is none"""
    best = None
    for g, k, c, kk in cands:
        if c != cls: continue
        s = sum(abs(x - y) for x, y in zip(kk, key))
        if best is None or s < best[0]: best = (s, k)
    return None if best is None else best[1]

def anim_conv(NM, RV, X=None):
    """f -> (bytes, record) of Revenge animation file f as No Mercy plays it: its twin's own where it has one (xref), else
    converted by the rules measured on the twins (one pair a Revenge file, in file order): the end pose is of the class
    (standing / lying / rising / the corner) Revenge's kind's No Mercy kind is of (99.7% of the twins), and its kind is that
    of the No Mercy animation of that class whose last key pose is nearest the file's own (97.6% of the twins, measured
    against all No Mercy's animations but the twin; the kind alone was 76.6%), the turn bits kept (99.6%); the record's end level kept, the rest of its byte 0 and bytes 1 .. 35 by the No Mercy value of
    Revenge's (where the twins have it), the frames kept, the sounds by their own mapping, REC_SOUND."""
    X = xref(NM, RV) if X is None else X
    RT, NT = RV(REV_RECS), NM(NM_RECS)
    nrec = lambda g: NT[36 * (g - NM_REC0):36 * (g - NM_REC0) + 36]
    rrec = lambda f: RT[36 * (f - REV_REC0):36 * (f - REV_REC0) + 36]
    kind = collections.defaultdict(collections.Counter)
    hi = collections.defaultdict(collections.Counter)
    m = [collections.defaultdict(collections.Counter) for k in range(36)]
    sm = collections.defaultdict(collections.Counter)
    for f in sorted(X):
        g = X[f]; rr, nr = rrec(f), nrec(g)
        kind[RV(f)[0] >> 2][NM(g)[0] >> 2] += 1
        hi[rr[0] >> 4][nr[0] >> 4] += 1
        for k in range(1, 36): m[k][rr[k]][nr[k]] += 1
        for a in SOUND_AT:
            if nr[a] and nr[a] == rr[a]: sm[rr[a + 1]][nr[a + 1]] += 1
    KIND = {k: most(c) for k, c in kind.items()}
    HI = {v: most(c) for v, c in hi.items()}
    conv = [{v: most(c) for v, c in mk.items()} for mk in m]
    snd = {v: most(c) for v, c in sm.items()}
    cands = []
    for g in range(NM_REC0, NM_REC0 + NM_RECN):
        kk = last_key(NM(g))
        if kk is not None: cands.append((g, NM(g)[0] >> 2, pose_class(NM(g)[0] >> 2), kk))
    def conv_anim(f):
        if f in X:
            g = X[f]
            return NM(g), nrec(g)
        d = bytearray(RV(f))
        k = d[0] >> 2
        if k not in KIND and k not in KIND_SEEN: raise KeyError("Revenge file 0x%X: end-pose kind %d is in no file both games have" % (f, k))
        km = KIND.get(k, KIND_SEEN.get(k))
        kk = last_key(d)
        nk = nearest_kind(cands, kk, pose_class(km)) if kk is not None else None
        d[0] = (km if nk is None else nk) << 2 | d[0] & 3
        rr = rrec(f)
        out = bytearray(36)
        out[0] = HI.get(rr[0] >> 4, rr[0] >> 4) << 4 | rr[0] & 15
        for k2 in range(1, 36): out[k2] = conv[k2].get(rr[k2], rr[k2])
        for k2 in FRAME_AT: out[k2] = rr[k2]
        for a in SOUND_AT: out[a + 1] = snd.get(rr[a + 1], rr[a + 1])
        for fr, s in REC_SOUND.get(f, {}).items():
            for a in SOUND_AT:
                if rr[a] == fr: out[a + 1] = s
        return bytes(d), bytes(out)
    return conv_anim

def build(R=None):
    R = R or Roms()
    NM, RV = R.files("nomercy"), R.files("revenge")
    if NM is None or RV is None: return None          # (no Revenge ROM: no library -- the game plays without it)
    # (2026-11-20, revmoves2) The library's layout -- which files share one copy, the pairs' and blocks' numbers, so its moves
    # table and stamp (the mod's move_lib.h has it) -- stays the one the converted files made before (byte0_table, rec_conv:
    # their content is each copy's key); what is written for a copy is the cross-reference's (anim_conv), the first file's.
    B0, REC = byte0_table(NM, RV), rec_conv(NM, RV)
    kinds = collections.defaultdict(collections.Counter)
    for a, b in sorted(B0.items()): kinds[a >> 2][b >> 2] += 1
    CONV = anim_conv(NM, RV)
    content = {}
    def anim(f):
        d = bytearray(RV(f))
        if d[0] in B0: d[0] = B0[d[0]]
        elif d[0] >> 2 in kinds: d[0] = most(kinds[d[0] >> 2]) << 2 | d[0] & 3
        else: d[0] = KIND_SEEN[d[0] >> 2] << 2 | d[0] & 3
        key = (bytes(d), REC(f))
        if key not in content: content[key] = CONV(f)
        return key
    lib = json.load(open(INDEX))
    num = lambda s: int(s, 16)
    anim_ix, anims = {}, []
    def anim_of(content):
        if content not in anim_ix: anim_ix[content] = len(anims); anims.append(content)
        return anim_ix[content]
    rows = []
    for x in lib:
        if "fa" not in x: rows.append((x, 0xFFFFFFFF, 0xFFFFFFFF)); continue
        pk = (anim(num(x["fa"])), anim(num(x["fd"])))
        bk = tuple(anim(num(f)) for f in x["cont"]) if x.get("cont") else None
        pair_ix = anim_of(pk[0]); anim_of(pk[1])
        if anim_ix[pk[1]] != pair_ix + 1: pair_ix = len(anims); anims += [pk[0], pk[1]]
        if bk:
            block_ix = len(anims)
            ix = [anim_ix.get(a) for a in bk]
            if all(i is not None for i in ix) and ix == list(range(ix[0], ix[0] + 8)): block_ix = ix[0]
            else: anims += list(bk)
        else: block_ix = 0xFFFFFFFF
        rows.append((x, pair_ix, block_ix))
    # (2026-11-20, revmoves) a move's roles after its pair (the index's "fx": a double team's man, a turnbuckle dive's
    # landings and miss -- No Mercy asks the move for them, file + 2 ..): fresh copies after every other animation, in a
    # row, named in the extension after the data (ri_movelib.cpp the same). The moves, IDs and stamp don't change.
    more = []
    for i, (x, p, b) in enumerate(rows):
        if not x.get("fx") or p == 0xFFFFFFFF: continue
        more.append((i, len(anims), len(x["fx"])))
        anims += [anim(num(f)) for f in x["fx"]]
    strings, data, mv, an = bytearray(), bytearray(), bytearray(), bytearray()
    for x, p, b in rows:
        off = len(strings); strings += x["id"].encode() + b"\0"
        mv += struct.pack("<IHHII", off, x["base"], x.get("ground", 0), p, b)
    for key in anims:
        b, rec = content[key]
        assert len(rec) == 36
        an += struct.pack("<II", len(data), len(b)) + rec
        data += b + bytes(-len(b) % 8)
    stamp = int(hashlib.md5(bytes(mv) + bytes(strings)).hexdigest()[:8], 16)
    ext = (b"NMLIBX01" + struct.pack("<I", len(more)) + b"".join(struct.pack("<III", *m) for m in more)) if more else b""
    return b"NMMOVLIB" + struct.pack("<IIIIII", 1, len(rows), len(anims), stamp, len(strings), len(data)) + mv + an + strings + data + ext

if __name__ == "__main__":
    out = sys.argv[1]
    blob = build()
    if blob is None: sys.exit("no Revenge ROM (or no No Mercy ROM): nothing made")
    open(out, "wb").write(blob)
    print("%s: %d bytes, md5 %s" % (out, len(blob), hashlib.md5(blob).hexdigest()))
    if "--check" in sys.argv:
        ref = open(sys.argv[sys.argv.index("--check") + 1], "rb").read()
        same = ref == blob
        print("against %s: %s" % (os.path.basename(sys.argv[sys.argv.index("--check") + 1]), "SAME" if same else
              "DIFFERENT (first at %s)" % next((i for i in range(min(len(ref), len(blob))) if ref[i] != blob[i]), min(len(ref), len(blob)))))
        sys.exit(0 if same else 1)
