#!/usr/bin/env python3
"""rev_wayon_freeze.py (revmoves3, 2026-11-21): the Revenge library slots whose No Mercy move goes on to another moveset entry
by its record while Revenge's own move ends there -- their record edit, frozen in tools\\romimport\\frozen\\records_damage.json
(the importer's record changes: ri_revmoves.cpp and aki_import.py --records both lay them over the slot's record).

Why: a library move "revenge:<pair>@<No Mercy move>" plays its Revenge animations with the No Mercy move's record. Some No
Mercy moves go on, by the record's +4 (a s16: the moveset entry the game goes into next -- 16362 / 16368, a go-behind, to 77,
the back grapple; 4182 to 85; 5380 to 70; 6072 to 183), into another move straight from where their own animations leave the
men. Revenge says per move whether it does: its code data (the man's Revenge file + 1, 24 bytes a code) byte 15, the code it
goes on to -- 0 for a move that ends. (Over every Revenge list, on the moves whose animations are a No Mercy move's byte for
byte -- but byte 0 -- byte 15 is non-zero on 3,044 of the 3,050 No Mercy moves that go on by +4 and 0 on all 67,181 that end;
the six others are slams that go on to the ground grapple, entries 236 / 240, which Revenge reaches another way: left out
here.) A move that goes on into a hold has byte 15 = 0 too and a hold block whose loop starts where its animation ends
(gen\\revanims.py rev_block, the importer's own test): left as it is. The rest end in Revenge and went on in No Mercy into a
move made for the men in another place -- Kanyon's counter (revenge:13ce_13cf, No Mercy's own 808 / 818 byte for byte, moves
that end) left the man staggering 115-137 away and the back grapple that followed caught air and ended them 300+ apart (the
move audit of 2026-11-20); corner moves ending with the man lying went on into a standing front-grapple move. Each such slot's
record gets byte 5 (the low byte of +4; the entries are below 256) = 0: the move ends after its animations, as in Revenge --
what the WrestleMania 2000 handler does from that game's own record (wm2000:1522 on 16362: "records" {"11": {"5": 0}}).

    python rev_wayon_freeze.py write <worktree> <records_damage.json>
        lays {"5": 0} over each such slot of records_damage.json (a slot's other bytes kept; each man's slots in order)
    python rev_wayon_freeze.py check <worktree> <records_damage.json>
        the developer check: the file's byte-5 edits are exactly these (exit 1 otherwise)
The library IDs: the worktree's tools\\handlers\\handlers\\revenge\\library_ids.json. Every Revenge Superstar's list and code
data (his Revenge file + 2 / + 1: revlib.py's rows_all.json "rev", the built-in ports' too -- a move in no list of his own is
judged by every list that has it). Reads the worktree's data\\extracted (the ROMs' files), mod\\gen and
tools\\handlers\\work\\revenge_library; writes only <records_damage.json> (write)."""
import json, os, re, struct, sys
sys.dont_write_bytecode = True
OFF = 0x110B                              # (a Revenge list's value + 0x110B: the animation file)
NM_ANIMS, NM_RECIX, NM_RECS = 0x1F0, 0x1F1, 0x1EF
GROUND = (236, 240, 244)                  # (a slam's way on to the ground grapple: Revenge's byte 15 says nothing of it)

def setup(wt):
    os.environ.setdefault("NM_MOD_DIR", os.path.join(wt, "mod"))
    sys.path.insert(0, wt); sys.path.insert(0, os.path.join(wt, "mod", "gen")); sys.path.insert(0, os.path.join(wt, "data", "revport"))
    import revanims as RA
    ex = os.path.join(wt, "data", "extracted")
    rev = lambda f: open(os.path.join(ex, "rev_files", "%04x.bin" % f), "rb").read()
    nmf = lambda f: open(os.path.join(ex, "nmfiles", "f", "%04x.bin" % f), "rb").read()
    ram = open(os.path.join(ex, "rev_bm_ram.bin"), "rb").read()
    T = [struct.unpack_from(">h", ram, 0x30570 + 2 * i)[0] for i in range(279)]   # (code -> its place in a list)
    return RA, rev, nmf, T

def edits(wt):
    RA, rev, nmf, T = setup(wt)
    rix, recs = nmf(NM_RECIX), nmf(NM_RECS)
    def rec(m):
        r = struct.unpack_from(">h", rix, 2 * m)[0]
        return recs[36 * r:36 * r + 36]
    way_on = lambda m: struct.unpack(">h", rec(m)[4:6])[0]
    flags = lambda m: struct.unpack(">I", rec(m)[0x20:0x24])[0]
    rows = json.load(open(os.path.join(wt, "tools", "handlers", "work", "revenge_library", "rows_all.json"), encoding="utf-8"))
    lists = {}
    for sid, r in rows.items():
        f = int(r["rev"], 16)
        lists[sid] = (struct.unpack(">763H", rev(f + 2)), rev(f + 1))
    def codes_in(rl, fa, fd):
        return [c for c in range(239) if rl[T[c]] and rl[T[c]] + OFF == fa and fd - OFF in rl[T[c] + 1:T[c + 1] + 1]]
    ids = json.load(open(os.path.join(wt, "tools", "handlers", "handlers", "revenge", "library_ids.json"), encoding="utf-8"))
    out = {}
    for sid, j in ids.items():
        if sid == "about" or sid not in lists: continue
        for slot, v in j["ids"].items():
            lid, _, at = v.partition("@")
            m = int(at.split("/")[-1])
            if not re.fullmatch(r"revenge:[0-9a-f]+_[0-9a-f]+", lid): continue   # (a move with its hold block "~"; a hand-made one)
            w = way_on(m)
            if not w or flags(m) & 0x1E00 or w in GROUND: continue
            fa, fd = (int(x, 16) for x in lid.split(":")[1].split("_"))
            own = codes_in(lists[sid][0], fa, fd)
            where = [(sid, c) for c in own] if own else [(s, c) for s, (rl, _) in lists.items() for c in codes_in(rl, fa, fd)]
            if not where or any(lists[s][1][24 * c + 15] for s, c in where): continue      # (none has it / Revenge's goes on)
            if any(RA.rev_block(lists[s][0], T, c)[0] is not None for s, c in where): continue  # (it goes on into a hold)
            out.setdefault(sid, {})[slot] = {"5": 0}
    return out

def main():
    cmd, wt, path = sys.argv[1:4]
    want = edits(wt)
    cur = json.load(open(path, encoding="utf-8"))
    if cmd == "check":
        have = {sid: {s: {"5": r["5"]} for s, r in v.items() if "5" in r} for sid, v in cur.items()}
        have = {k: v for k, v in have.items() if v}
        bad = sorted(set(have) ^ set(want)) + sorted(s for s in set(have) & set(want) if have[s] != want[s])
        print("rev_wayon_freeze check: %d slots on %d men, %s" % (sum(len(v) for v in want.values()), len(want), "SAME" if not bad else "DIFFERENT: %s" % bad))
        sys.exit(1 if bad else 0)
    for sid, slots in want.items():
        r = cur.setdefault(sid, {})
        for slot, e in slots.items(): r.setdefault(slot, {}).update(e)
        cur[sid] = {k: r[k] for k in sorted(r, key=int)}
    open(path, "w", encoding="utf-8", newline="").write(json.dumps(cur, indent=1).replace("\n", "\r\n"))   # (the file's own form: CRLF, no last newline)
    print("rev_wayon_freeze write: %d slots on %d men -> %s" % (sum(len(v) for v in want.values()), len(want), path))

if __name__ == "__main__":
    main()
