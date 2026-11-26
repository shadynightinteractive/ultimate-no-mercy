#!/usr/bin/env python3
"""replay (2026-11-14): where two runs' memory differs at one retrace (nm_replay.cpp NM_REPLAY_DUMP's .ram_<r>.bin).

  python ram_diff.py <a.ram_R.bin> <b.ram_R.bin> [--map <map.json>] [--mod-span <bytes>]

The file is the N64's 8 MB, then the mods' memory from 0x81000000, as the PC holds it (each 32-bit word in the PC's
order). Differences are given by N64 address, merged into ranges, with the structure and member of map.json that holds
each (when --map is given), the largest first.
"""
import json, sys

RAM = 0x800000


def ranges(a, b):
    out, start, last = [], None, None
    n = min(len(a), len(b))
    step = 4
    for w in range(0, n - n % step, step):
        if a[w:w + step] != b[w:w + step]:
            if start is None:
                start = w
            last = w
        elif start is not None and w - last > 32:
            out.append((start, last + step))
            start = None
    if start is not None:
        out.append((start, last + step))
    return out


def address(off):
    return 0x80000000 + off if off < RAM else 0x81000000 + (off - RAM)


def main(argv):
    a = open(argv[1], "rb").read()
    b = open(argv[2], "rb").read()
    members = []
    if "--map" in argv:
        m = json.load(open(argv[argv.index("--map") + 1], encoding="utf-8"))
        for s in m["structures"]:
            for mem in s.get("members") or []:
                try:
                    members.append((int(mem["address"], 16), int(mem["end"], 16), s["id"], mem.get("name")))
                except (KeyError, TypeError, ValueError):
                    pass
    rs = ranges(a, b)
    print("%d bytes compared, %d ranges differ, %d bytes in them" % (min(len(a), len(b)), len(rs), sum(e - s for s, e in rs)))
    for s, e in sorted(rs, key=lambda r: -(r[1] - r[0]))[:60]:
        lo, hi = address(s), address(e)
        holders = sorted({"%s.%s" % (x[2], x[3]) for x in members if x[0] < hi and x[1] > lo})
        print("  0x%08X-0x%08X %6d bytes  %s" % (lo, hi, e - s, ", ".join(holders[:4]) if holders else "(in no structure of the map)"))


if __name__ == "__main__":
    main(sys.argv)
