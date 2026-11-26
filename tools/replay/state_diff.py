#!/usr/bin/env python3
"""replay (2026-11-14): two runs' QA state (qa.ps1's state.csv, sampled 20 times a second of the wall clock) compared at
the match frames both happened to sample: for runs that have no per-retrace hashes (a normal run, not in lockstep).

  python state_diff.py <a/state.csv> <b/state.csv>

Says how long each match lasted (its last match frame) and, of the match frames sampled in both, how many show the two
wrestlers somewhere else, and the first of them.
"""
import csv, sys

COLS = ["s0_x", "s0_z", "s1_x", "s1_z", "s0_spirit", "s1_spirit"]


def rows(path):
    out = {}
    last = 0
    for r in csv.DictReader(open(path)):
        try:
            mf = int(r.get("match_frames", "65535"), 0)
            stage = int(r.get("stage", "0"), 0)
        except ValueError:
            continue
        if stage != 2 or mf in (0, 0xFFFF):
            continue
        last = max(last, mf)
        out[mf] = tuple(r.get(c, "") for c in COLS)
    return out, last


def main(a, b):
    ra, la = rows(a)
    rb, lb = rows(b)
    both = sorted(set(ra) & set(rb))
    diff = [f for f in both if ra[f] != rb[f]]
    print("match frames: %d in a, %d in b (the match's length)" % (la, lb))
    print("sampled in both: %d; different (%s): %d%s" % (len(both), ", ".join(COLS), len(diff), (", the first at match frame %d" % diff[0]) if diff else ""))
    if diff:
        f = diff[0]
        print("  a: %s\n  b: %s" % (ra[f], rb[f]))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
