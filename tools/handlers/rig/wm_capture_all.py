#!/usr/bin/env python3
# wm_capture_all.py (wm2kall, 2026-11-13): every WrestleMania 2000 Superstar's four costumes, as WrestleMania 2000 builds
# them in a match -- four men a match (wm_cap.py: its Tag Match, the four picks written on its Decision screen, the costume
# forced for all four), costume by costume. One emulator at a time, below normal priority (wm.py).
#   python wm_capture_all.py [k,k,...]        (roster indexes; default: every real Superstar -- records 1-50 and 67-74)
# -> cap\all_c<c>_g<g>_ram.bin (+ _vs.png, _ring.png) and cap\all_index.json: {"k:c": [ram file, slot]}, kept as it goes (a
#    rerun skips what's in it). Each slot is checked: the game ID the match holds for it is the one asked for.
import json, os, struct, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wm, wm_cap
from handlers import RIG
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "handlers", "wm2000"))

CAP = os.path.join(RIG, "cap")
INDEX = os.path.join(CAP, "all_index.json")
REAL = list(range(1, 51)) + list(range(67, 75))      # (0 and 75: the game's dummies; 51-66: its empty Create-a-Wrestler slots)

def roster_ids():
    """roster index -> game ID, read from the ROM's records (the select screen's RAM has the same table)"""
    ram = open(os.path.join(CAP, wm_cap.START_STATE + "_ram.bin"), "rb").read()
    r32 = lambda a: struct.unpack_from(">I", ram, a & 0x7FFFFF)[0]
    return {k: struct.unpack_from(">H", ram, (r32(0x800411B0 + 4 * k) + 4) & 0x7FFFFF)[0] for k in range(0x4C)}

def main():
    only = [int(x) for x in sys.argv[1].split(",")] if len(sys.argv) > 1 else REAL
    ids = roster_ids()
    index = json.load(open(INDEX)) if os.path.exists(INDEX) else {}
    for c in range(4):
        todo = [k for k in only if "%d:%d" % (k, c) not in index]
        for g in range(0, len(todo), 4):
            four = todo[g:g + 4]
            picks = four + [four[0]] * (4 - len(four))        # (a short group: its first man again in the empty slots)
            tag = "all_c%d_%s" % (c, "_".join("%d" % k for k in four))
            t0 = time.time()
            M = wm_cap.capture(tag, [ids[k] for k in picks], c)
            ok = []
            for s, k in enumerate(picks[:len(four)]):
                got = M.r32(wm_cap.PICK_IDS + 4 * s)
                desc_mesh = M.r16(0x8009DF90 + s * 0x258 + 8)         # (part 1's mesh: a model is there)
                if got == ids[k] and desc_mesh:
                    index["%d:%d" % (k, c)] = [tag + "_ram.bin", s]; ok.append(k)
                else:
                    print("  slot %d: wanted %04X, the match holds %04X, part 1 mesh %X" % (s, ids[k], got, desc_mesh), flush=True)
            json.dump(index, open(INDEX, "w"), indent=0)
            print("costume %d: %s -> %s ok %s (%.0f s)" % (c, four, tag, ok, time.time() - t0), flush=True)
    print("done: %d costumes in %s" % (len(index), INDEX))

if __name__ == "__main__":
    main()
