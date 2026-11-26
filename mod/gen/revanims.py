#!/usr/bin/env python3
"""Revenge's own animations for the ported Superstars' moves No Mercy has no copy of (src/movex.c).

gen/mkroster.py ports Bret Hart's and Jim Neidhart's (and Hollywood Hogan's, Kevin Nash's, Scott Hall's) movesets
from WCW/nWo Revenge: a slot gets the No Mercy move whose animation is Revenge's byte for byte ("exact"), else the
nearest by pose ("near": a stand-in). For the stand-ins this brings Revenge's animations over -- the attacker's and
the defender's, played in the
stand-in's place (its record, its timing: the move as No Mercy runs it, Revenge's motion).

A file's first byte differs between the games for the same motion (an index of another table: Revenge 0 is
No Mercy's 4, 28 its 80, ...; No Mercy plays a file with Revenge's first byte with the limbs stretched): the
imported files get No Mercy's, by the table the files both games have show (BYTE0).

Writes gen_data/rev_anims.json (for gen/wmport.py: the files, each pair one after the other) and
gen_data/movex_data.h (the rows by ext_late index, as movex.c had them until 2026-11-13). Run after gen/mkroster.py, before
gen/wmport.py; then the Revenge handler's movelib.py (tools/handlers, 2026-11-13) makes src/move_lib.h -- the
library moves and the built-in ports' lists movex.c plays -- from rev_anims.json, checking each row against movex_data.h."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys, os, json, struct, collections, re
sys.path.insert(0, nmp("/home/devbox/work/arcade/gen")); sys.path.insert(0, nmp("/home/devbox/work/revport"))
import mkroster as K
import revmoves as RM

OUT_JSON = nmp("/home/devbox/work/gen_data/rev_anims.json")
OUT_H = nmp("/home/devbox/work/gen_data/movex_data.h")   # (2026-11-13, movelib: src/move_lib.h is what the mod builds from)
REV = nmp("/tmp/devbox-0/rev_files/%04x.bin")
NMF = nmp("/tmp/devbox-0/nmfiles/f/%04x.bin")

def byte0_table():
    """Revenge's first byte -> No Mercy's, from the animations both games have (the rest of the file the same)"""
    nm = {}
    for f in range(0x3322, 0x4400):
        p = NMF % f
        if os.path.exists(p):
            d = open(p, "rb").read()
            if len(d) > 8: nm.setdefault(d[1:], collections.Counter())[d[0]] += 1
    m = collections.defaultdict(collections.Counter)
    for f in range(0x110B, 0x1D00):
        p = REV % f
        if not os.path.exists(p): continue
        d = open(p, "rb").read()
        if len(d) > 8 and d[1:] in nm:
            for b, c in nm[d[1:]].items(): m[d[0]][b] += c
    return {a: c.most_common(1)[0][0] for a, c in m.items()}

REV_RECS = 0x020F      # Revenge's animations' records (36 bytes each, from file REV_REC0) -- No Mercy's file 0x2BF (from 0x2DD9)
REV_REC0 = 0xE39
NM_RECS, NM_REC0, NM_RECN = 0x2BF, 0x2DD9, 0x169B

# A record's sounds: (frame, sound) pairs at bytes 20, 22 (the defender's) and 24 .. 30 (the attacker's); frames
# also at 15, 16, 18, 32 (equal in both games for 90% and more of the animations they share). Sound numbers are
# the same in both but for a few (Revenge 0x2D.. -> No Mercy 0x45..; 0xBE: none).
FRAME_AT = (15, 16, 18, 20, 22, 24, 26, 28, 30, 32)
SOUND_AT = (20, 22, 24, 26, 28, 30)
# a sound of our own for a Revenge file at a frame: Nash's jackknife -- the man hitting the mat (frame 0x8D) the
# heaviest thud there is, 0x1D (No Mercy's finishers' most used; its own Jackknife Powerbomb's), the user, 2026-10-30
REC_SOUND = {0x1545: {0x8D: 0x1D}}
def rec_conv():
    """per record byte: Revenge's value -> No Mercy's (the most usual for the animations both games have; the
    games index some tables differently, and No Mercy added a field: bytes 34-35)"""
    RT, NT = open(REV % REV_RECS, "rb").read(), open(NMF % NM_RECS, "rb").read()
    rv = {}
    for f in range(REV_REC0, REV_REC0 + len(RT) // 36):
        p = REV % f
        if os.path.exists(p):
            d = open(p, "rb").read()
            if len(d) > 8: rv.setdefault(d[1:], []).append(f)
    m = [collections.defaultdict(collections.Counter) for k in range(36)]
    sm = collections.defaultdict(collections.Counter)
    for f in range(NM_REC0, NM_REC0 + NM_RECN):
        p = NMF % f
        if not os.path.exists(p): continue
        d = open(p, "rb").read()
        if len(d) <= 8 or d[1:] not in rv: continue
        nr = NT[36 * (f - NM_REC0):36 * (f - NM_REC0) + 36]
        for r in rv[d[1:]]:
            rr = RT[36 * (r - REV_REC0):36 * (r - REV_REC0) + 36]
            for k in range(36): m[k][rr[k]][nr[k]] += 1
            for a in SOUND_AT:                     # (a sound at the same frame in both: the games' numbers for it)
                if nr[a] and nr[a] == rr[a]: sm[rr[a + 1]][nr[a + 1]] += 1
    conv = [{v: c.most_common(1)[0][0] for v, c in mk.items()} for mk in m]
    snd = {v: c.most_common(1)[0][0] for v, c in sm.items()}
    def rec(f):
        rr = RT[36 * (f - REV_REC0):36 * (f - REV_REC0) + 36]
        out = bytearray(conv[k].get(rr[k], rr[k]) for k in range(36))
        # frames as they are, sounds by the sound's own number (byte by byte, a frame or a sound seldom seen
        # there became another's -- 0: Nash's jackknife lost its slam and the man's thud, the user, 2026-10-30)
        for k in FRAME_AT: out[k] = rr[k]
        for a in SOUND_AT: out[a + 1] = snd.get(rr[a + 1], rr[a + 1])
        for fr, s in REC_SOUND.get(f, {}).items():
            for a in SOUND_AT:
                if rr[a] == fr: out[a + 1] = s
        return bytes(out)
    return rec

# No Mercy's own animations joined into one (movex.c): none now. (Bret Hart's front special was the slam (1880's) and
# the Sharpshooter's turnover (16770's) as one file, BRET_SLAM_SS -- the turnover placed from the slam's start, the
# man's root ~190 off at the seam, the slam's record (no sounds) all through. Since 2026-11-01 the two are two moves, as
# in Revenge: 1880's record goes on to his entry 157 -- movex.c. The copies of the Sharpshooter's hold, release and
# turnover, SS_HOLD / SS_REL / SS_TURN, went with it.)
# slot -> Revenge code: his own move (its variants) for a slot No Mercy has nothing like -- Bret Hart's strong
# grapple 15 (Revenge code 8: its five variants, codes 8-12), on the reference's Suplex (16580: five too)
REV_OWN = {"brethart": {15: 8}}
NM_JOIN = []
# No Mercy's own animations on another move of a ported Superstar's: (key, entry, name, the attacker's file, the other
# man's file). Hogan's ground special (mkroster REV_OVERRIDE 155: the Russian Neck Drop's move, 16750) his leg drop (the
# user, 2026-10-30: "We just need Hogan's leg drop to land, the opponent to get hit and take damage as if it's a powerful
# finishing move"): legdrop_pair()
NM_OWN = []   # (none now: Hogan's leg drop is being redone -- data\legdrop\LEGDROP.md; the tried ways in the notes below)
# The Russian Neck Drop as the game has it -- a special grapple at the head with a blow: its record (0x3EA3's: the blow
# at frame 68, byte 15; its sounds about it), its man's side (0x3EA4: struck at 72, left lying) and its rate -- with
# Hogan's side his leg drop (7980's animation: his Revenge one byte for byte), its first frame held so the leg lands at
# 68 (its own blow at 29), its last held to the move's 101 frames. (Tried: the People's Elbow special's move -- of
# another kind, 0x40: the move was cut short, no blow; the drop alone in the slot, with a standing / the Elbow Drop's /
# the People's Elbow's man -- he got up or wasn't struck: the user's tests 2026-10-30 / 31)
RND_A = 0x3EA3
def legdrop_pair(name, fa, fd):
    rnd, ld = nm_file(RND_A), nm_file(fa)
    key = lambda d: [d[4 + 0x62 * i:4 + 0x62 * i + 0x62] for i in range((len(d) - 4) // 0x62)]
    n = struct.unpack(">H", rnd[2:4])[0]                                  # 101
    lk = key(ld)
    keys = [lk[0]] * ((68 - 29) // 2) + lk                               # (a key every 2 frames: the leg down at 68)
    keys = (keys + [keys[-1]] * 60)[:(n + 1) // 2]
    da = struct.pack(">HH", (struct.unpack(">H", ld[:2])[0] & 0xFC00) | (struct.unpack(">H", rnd[:2])[0] & 0x3FF), n) + b"".join(keys)
    dd = nm_file(fd)
    os.makedirs(JOIN_DIR, exist_ok=True)
    open("%s/%s_a.bin" % (JOIN_DIR, name), "wb").write(da); open("%s/%s_d.bin" % (JOIN_DIR, name), "wb").write(dd)
    return dict(name=name, src="join", pa="%s/%s_a.bin" % (JOIN_SB, name), pd="%s/%s_d.bin" % (JOIN_SB, name),
                hdr=[da[:4].hex(), dd[:4].hex()], rec=[nm_rec(RND_A).hex(), nm_rec(fd).hex()])
JOIN_SB = "/home/devbox/work/gen_data/join"      # (as rev_anims.json names them: gen/wmport.py nmp()s them)
JOIN_DIR = nmp(JOIN_SB)

def nm_file(f): return open(NMF % f, "rb").read()
def nm_rec(f):
    RT = open(NMF % NM_RECS, "rb").read()
    return RT[36 * (f - NM_REC0):36 * (f - NM_REC0) + 36]

def NM_ANIM_OF(m):
    """No Mercy's move m: its attacker's animation file (file 0x1F0: s16 per move, + 0x3322)"""
    return struct.unpack_from(">h", nm_file(0x1F0), 2 * m)[0] + 0x3322

# Is his Revenge animation at a slot's place in his list the slot's kind of move? (The place is the five pairs'
# vote: where few of them agree it can be another kind's -- Savage's strong strike (188) played Revenge's place
# 341, a punch down at a fallen man, No Mercy's Jumping Punch byte for byte, the user, 2026-10-30; Snapmare
# Spinebusters, Sleeper Holds Monkey Tosses ...) Two checks: an animation No Mercy has too (byte for byte) is
# one of the moves the slot can hold; and the place lies between those of the slots the pairs agree on (3+ of
# the five exact), in order -- Revenge's list runs as No Mercy's slots do.
_TWINS = None
def twin_moves(a):
    global _TWINS
    if _TWINS is None:
        _TWINS = collections.defaultdict(set)
        for b in range(0, len(RM.NM1) - 10, 10):
            for v in RM.NM1[b:b + 10]:
                if v > 0: _TWINS[RM.nmhash(v + 0x3322)].add(b)
    h = RM.revh.get(a)
    return _TWINS.get(h, set()) if h else set()
def slot_kind_ok(slot, a, pos, pool):
    tw = twin_moves(a)
    if tw and not (tw & pool.get(slot, set())): return False
    trusted = [(s, j) for s, (j, ex, n) in pos.items() if ex >= 3]
    lo = max((j for s, j in trusted if s < slot), default=-1)
    hi = min((j for s, j in trusted if s > slot), default=1 << 20)
    return lo <= pos[slot][0] <= hi
def mixed_pair(name, da, rec_a, nd):
    """his Revenge animation (byte 0 made No Mercy's) with No Mercy's file nd for the other man"""
    os.makedirs(JOIN_DIR, exist_ok=True)
    dd = nm_file(nd)
    open("%s/%s_a.bin" % (JOIN_DIR, name), "wb").write(da); open("%s/%s_d.bin" % (JOIN_DIR, name), "wb").write(dd)
    return dict(name=name, src="join", pa="%s/%s_a.bin" % (JOIN_SB, name), pd="%s/%s_d.bin" % (JOIN_SB, name),
                hdr=[da[:4].hex(), dd[:4].hex()], rec=[rec_a.hex(), nm_rec(nd).hex()])
def join_files(name, fa, fd):
    """the files one after another (a key frame every 2 frames, 0x62 bytes, after the 4-byte header): each
    part from its first key, the next part from where the first left off (the frame after its last key)"""
    out = []
    for files in (fa, fd):
        body, frames = b"", 0
        for k, f in enumerate(files):
            d = open(NMF % f, "rb").read()
            n = struct.unpack(">H", d[2:4])[0]
            keys = [d[4 + 0x62 * i:4 + 0x62 * i + 0x62] for i in range((len(d) - 4) // 0x62)]
            if k < len(files) - 1:
                keys = keys[:(n + 1) // 2]        # (frames 0, 2, ... up to the part's last frame)
                n = 2 * len(keys)
            body += b"".join(keys); frames += n
        last = open(NMF % files[-1], "rb").read()
        hdr = struct.pack(">HH", struct.unpack(">H", last[:2])[0], frames)
        out.append((hdr + body, hdr))
    os.makedirs(JOIN_DIR, exist_ok=True)
    pa, pd = "%s/%s_a.bin" % (JOIN_DIR, name), "%s/%s_d.bin" % (JOIN_DIR, name)
    dbody = out[1][0]
    open(pa, "wb").write(out[0][0]); open(pd, "wb").write(dbody)
    pa, pd = "%s/%s_a.bin" % (JOIN_SB, name), "%s/%s_d.bin" % (JOIN_SB, name)
    RT = open(NMF % NM_RECS, "rb").read()
    rec = lambda f: RT[36 * (f - NM_REC0):36 * (f - NM_REC0) + 36]
    return dict(name=name, src="join", pa=pa, pd=pd, hdr=[out[0][1].hex(), out[1][1].hex()], rec=[rec(fa[0]).hex(), rec(fd[0]).hex()])

# A move that goes on (its record's 0x1E00: a hold's grab -> the hold, a suplex -> its pin ...) plays the move after
# it as a move of its own: the game's continuation entry, the stand-in's move + 1, with its own animation -- the hold
# over and over, then its file + 2 / + 4 / + 6 to end it (ovl_e func_80137044, func_80137364, func_8013C164). Made
# over with only the first move's animation, a port's hold went on in the stand-in's No Mercy hold: The Giant's
# sleeper from behind (Revenge's grab) turned into the Abdominal Neck Wrench's loop, the man ~60 away, and let go
# as that hold does (the user, 2026-11-01: "Revenge wrestlers holds are glitching"). Revenge keeps the same: a move
# code that goes on has a block of its own past the move codes (codes 239-262, 8 files each -- the loop's pair and
# the three endings', No Mercy's order: code 260 is No Mercy's Sharpshooter hold 16352..16359 byte for byte). Which
# block is the one whose loop starts where the move's animation ends, both men (the files' first key against the
# move's last): checked on the 1035 moves of Revenge's 76 lists No Mercy has byte for byte, 1034 right (one a tie).
# The block's pairs go in the pack one after another, and movex.c plays them for the continuation entry.
NM_MREC, NM_MRECIX = 0x1EF, 0x1F1
def nm_flags(m):
    ri = struct.unpack_from(">h", nm_file(NM_MRECIX), 2 * m)[0]
    return struct.unpack_from(">I", nm_file(NM_MREC), 36 * ri + 0x20)[0]
GOES_ON = 0x1E00
BLOCK0, BLOCK1 = 239, 263
def _keys(f):
    d = open(REV % f, "rb").read(); return [d[4 + 0x62 * i:4 + 0x62 * i + 0x62] for i in range((len(d) - 4) // 0x62)]
def _dist(a, b): return sum(abs(x - y) for x, y in zip(a, b))
def rev_block(rl, t, c):
    """the block code c goes on to (None: c ends there -- no block's loop starts near its end), the score, the next"""
    p = t[c]
    if not rl[p] or not rl[p + 1]: return None, 0, 0
    ga, gd = _keys(RM.ranim(rl[p]))[-1], _keys(RM.ranim(rl[p + 1]))[-1]
    sc = []
    for b in range(BLOCK0, BLOCK1):
        if not all(rl[t[b] + k] for k in range(8)): continue
        body = open(REV % RM.ranim(rl[t[b]]), "rb").read()[1:]
        sc.append((_dist(ga, _keys(RM.ranim(rl[t[b]]))[0]) + _dist(gd, _keys(RM.ranim(rl[t[b] + 1]))[0]), b, body))
    if not sc: return None, 0, 0
    sc.sort(key=lambda x: x[:2])
    best = sc[0]
    other = [s for s in sc if s[2] != best[2]]          # (the same files in two blocks: the same block)
    nxt = other[0][0] if other else 1 << 30
    # (the right ones score 1700..5600 and the next another block twice that and more; a move that ends scores
    #  10000 and more with the next block about as near)
    ok = best[0] < 8000 and best[0] * 1.8 < nxt
    return (best[1] if ok else None), best[0], nxt

def main():
    B0 = byte0_table()
    REC = rec_conv()
    ram = open(nmp("/tmp/devbox-0/rev_bm_ram.bin"), "rb").read()       # (Revenge's code -> list table, 0x80030570)
    t = [struct.unpack_from(">h", ram, 0x30570 + 2 * i)[0] for i in range(279)]
    pos = RM.slot_positions(RM.PAIRS)
    pool = RM.slot_pool(K.CAN)
    rows, files, blocks = [], [], []
    late_index = {e["key"]: k for k, e in enumerate(K.LATE)}
    for key in [x["key"] for x in K.LATE if "rev" in x]:   # (every Revenge port, in LATE's order: Bret .. the Giant, ports4's ten)
        e = [x for x in K.LATE if x["key"] == key][0]
        rep = []
        K.port_rev_moveset(key, e["rev"], e["ref"], e["allies"], K.REV_OVERRIDE, rep)
        rl = RM.revlist(e["rev"])
        for l in rep:
            if "slot" not in l or "(near" not in l: continue
            slot = int(l.split("slot")[1].split(":")[0]); j = pos[slot][0]
            if j >= t[238]: continue
            c = max(i for i in range(239) if t[i] <= j)
            n = t[c + 1] - t[c]
            if j != t[c] or n < 2 or not rl[j] or not rl[j + 1]: continue    # (a move's own pair only)
            fa, fd = RM.ranim(rl[j]), RM.ranim(rl[j + 1])
            da, dd = open(REV % fa, "rb").read(), open(REV % fd, "rb").read()
            if da[0] not in B0 or dd[0] not in B0: continue
            name = "%s_%04X_%04X" % (key.upper(), fa, fd)
            if name not in [f["name"] for f in files]:
                files.append(dict(name=name, fa=fa, fd=fd, b0a=B0[da[0]], b0d=B0[dd[0]],
                                  hdr=[bytes([B0[da[0]]]).hex() + da[1:4].hex(), bytes([B0[dd[0]]]).hex() + dd[1:4].hex()],
                                  rec=[REC(fa).hex(), REC(fd).hex()]))
            rows.append(dict(key=key, late=late_index[key], slot=slot, name=name, code=c, stand_in=l.split("->")[1].strip(),
                             move=int(l.split("->")[1].split()[0])))
        # his own moves where No Mercy has no copy of them and the slot kept the reference's: every variant of
        # the slot (entry + k) played with his Revenge code's (code + k) pair, on the reference's move
        for slot, code in REV_OWN.get(key, {}).items():
            for k in range(5):
                j = t[code + k]
                if not rl[j] or not rl[j + 1]: continue
                fa, fd = RM.ranim(rl[j]), RM.ranim(rl[j + 1])
                da, dd = open(REV % fa, "rb").read(), open(REV % fd, "rb").read()
                if da[0] not in B0 or dd[0] not in B0: continue
                name = "%s_%04X_%04X" % (key.upper(), fa, fd)
                if name not in [f["name"] for f in files]:
                    files.append(dict(name=name, fa=fa, fd=fd, b0a=B0[da[0]], b0d=B0[dd[0]],
                                      hdr=[bytes([B0[da[0]]]).hex() + da[1:4].hex(), bytes([B0[dd[0]]]).hex() + dd[1:4].hex()],
                                      rec=[REC(fa).hex(), REC(fd).hex()]))
                rows.append(dict(key=key, late=late_index[key], slot=slot + k, name=name, code=code + k, stand_in="(Revenge's own, on the reference's move)",
                                 move=K.van_unpacked(e["ref"])[0][slot + k][0]))
        # every other slot that kept the reference's move (No Mercy has nothing like his there: Hogan's strong punch
        # and his stomps were The Rock's, the user, 2026-10-30) gets his own Revenge animation on it: a move's pair as
        # above; a strike (Revenge keeps only his side of it) his animation with the reference move's reaction
        done = {r["slot"] for r in rows if r["key"] == key} | {int(l.split("slot")[1].split(":")[0]) for l in rep if "slot" in l and ":" in l.split("slot")[1][:6]}
        rs, _ = K.van_unpacked(e["ref"])
        for slot in range(292):
            rm = rs[slot][0]
            if slot in done or slot not in pos or rm in (0, 0xFFFF): continue
            if re.search(r"double|doomsday", K.vnames.get(rm - rm % 10, ""), re.I): continue   # (a partner's moves)
            j = pos[slot][0]
            if j >= t[238] or not rl[j]: continue
            c = max(i for i in range(239) if t[i] <= j)
            if j != t[c]: continue
            fa = RM.ranim(rl[j]); da = open(REV % fa, "rb").read()
            if da[0] not in B0: continue
            if not slot_kind_ok(slot, fa, pos, pool): continue
            if t[c + 1] - t[c] >= 2 and rl[j + 1]:
                fd = RM.ranim(rl[j + 1]); dd = open(REV % fd, "rb").read()
                if dd[0] not in B0: continue
                name = "%s_%04X_%04X" % (key.upper(), fa, fd)
                if name not in [f["name"] for f in files]:
                    files.append(dict(name=name, fa=fa, fd=fd, b0a=B0[da[0]], b0d=B0[dd[0]],
                                      hdr=[bytes([B0[da[0]]]).hex() + da[1:4].hex(), bytes([B0[dd[0]]]).hex() + dd[1:4].hex()],
                                      rec=[REC(fa).hex(), REC(fd).hex()]))
            else:
                # (a strike: his Revenge swing on the reference move's No Mercy reaction (mixed_pair) -- its hit timed
                # for another swing: Nash's light strike, an overhead club, came down in front of the man and never
                # landed, the user, 2026-10-30. Left the reference's.)
                continue
            rows.append(dict(key=key, late=late_index[key], slot=slot, name=name, code=c, stand_in="(his own, on the reference's %d)" % rm, move=rm))
        # the moves after them (above, rev_block): a move that goes on gets the block Revenge's move goes on to; where
        # Revenge's ends (no block) the slot's move is one that ends too -- the nearest by pose of those the slot can
        # hold (the move's own animation stays his: the German Suplex Pin's bridge after a Revenge suplex, no more)
        def add_pair(name, fa, fd, rec_a=None):
            da, dd = open(REV % fa, "rb").read(), open(REV % fd, "rb").read()
            if name not in [f["name"] for f in files]:
                files.append(dict(name=name, fa=fa, fd=fd, b0a=B0[da[0]], b0d=B0[dd[0]],
                                  hdr=[bytes([B0[da[0]]]).hex() + da[1:4].hex(), bytes([B0[dd[0]]]).hex() + dd[1:4].hex()],
                                  rec=[REC(fa).hex(), REC(fd).hex()]))
        for r in [r for r in rows if r["key"] == key]:
            m = r["move"]
            if m in (0, 0xFFFF) or not (nm_flags(m) & GOES_ON): continue
            d, sc, nxt = rev_block(rl, t, r["code"])
            if d is not None:
                fs = [RM.ranim(rl[t[d] + k]) for k in range(8)]
                if not all(open(REV % f, "rb").read()[0] in B0 for f in fs):
                    print("  %s slot %d: block %d's first byte not known -- left as it was" % (key, r["slot"], d)); continue
                if nm_flags(m + 1) & GOES_ON: print("  %s slot %d: %d + 1 goes on again (not made)" % (key, r["slot"], m))
                names = ["%s_B%d_%04X_%04X" % (key.upper(), d, fs[2 * k], fs[2 * k + 1]) for k in range(4)]
                if names[0] not in [f["name"] for f in files]:
                    for k in range(4): add_pair(names[k], fs[2 * k], fs[2 * k + 1])
                r["cont"] = names[0]
                blocks.append("  %s slot %3d: code %3d -> block %d (%d, next %d) for %d's %d" % (key, r["slot"], r["code"], d, sc, nxt, m, m + 1))
            else:
                base = [v for v in sorted(pool.get(r["slot"], ())) if not (nm_flags(v) & GOES_ON)]
                pr = RM.predict(rl, {r["slot"]: pos[r["slot"]]}, {r["slot"]: base}).get(r["slot"]) if base else None
                if not pr:
                    print("  %s slot %d: %d goes on, Revenge's code %d doesn't, and the slot has no move that ends" % (key, r["slot"], m, r["code"])); continue
                r["carrier"] = pr[0]
                blocks.append("  %s slot %3d: code %3d ends (%d, next %d): %d -> %d %s" % (key, r["slot"], r["code"], sc, nxt, m, pr[0], K.vnames.get(pr[0] - pr[0] % 10, "")))
    for key, entry, name, fa, fd in NM_OWN:
        files.append(legdrop_pair(name, fa, fd))
        rows.append(dict(key=key, late=late_index[key], slot=entry, name=name, code=-1, stand_in="(No Mercy's %04X)" % fa))
    for name, fa, fd in NM_JOIN: files.append(join_files(name, fa, fd))
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    json.dump(dict(files=files, rows=rows), open(OUT_JSON, "w"), indent=1)
    L = ["// generated by gen/revanims.py: the ported Superstars' stand-in moves played with their own (Revenge's)",
         "// animations: { ext_late index, moveset entry, the attacker's file (rev_move_anims.h; the defender's the next), 0, -, -,",
         "// { the file of the move it goes on to: Revenge's block }, the move in its place (0: as it is) }",
         '#include "rev_move_anims.h"',
         "#define MOVEX_FILES_N %d" % len(rows),
         "static const MoveX movex_files[MOVEX_FILES_N] = {"]
    for r in rows:
        L.append("    { %d, %d, RMA_%s, 0, { 0 }, { 0 }, { %s }, %d },   // %s slot %d: Revenge code %d (stand-in %s)" % (
            r["late"], r["slot"], r["name"], "RMA_" + r["cont"] if r.get("cont") else "0", r.get("carrier", 0), r["key"], r["slot"], r["code"], r["stand_in"][:40]))
    L.append("};")
    L += ["// their header (the file's first 4 bytes) and record (file 0x2BF's, Revenge's made No Mercy's), by file - RMA_FIRST",
          "#define RMA_N %d" % (2 * len(files)),
          "static const u8 rma_hdr[RMA_N + 1][4] = {"]
    L += ["    { %s }," % ", ".join("0x%02X" % b for b in bytes.fromhex(h)) for f in files for h in f["hdr"]]
    L += ["    { 0 } };", "static const u8 rma_rec[RMA_N + 1][36] = {"]
    L += ["    { %s }," % ", ".join("0x%02X" % b for b in bytes.fromhex(h)) for f in files for h in f["rec"]]
    L += ["    { 0 } };"]
    open(OUT_H, "w").write("\n".join(L) + "\n")
    print("\n".join(blocks))
    print(len(files), "animation pairs,", len(rows), "moves")

if __name__ == "__main__":
    main()
