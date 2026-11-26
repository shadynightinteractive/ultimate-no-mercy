#!/usr/bin/env python3
"""modextra_cmp.py (phase3 p3ports, 2026-11-19): two modextra.bin files compared hole by hole, named by hdrfill.bin's holes.

    python modextra_cmp.py <hdrfill.bin> <modextra.bin A> <modextra.bin B>
Prints each hole that differs (its symbol, its header, how many bytes) and a count; exit 0 when every hole is the same."""
import os, sys
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'firstrun'))
import hdrfill_check as H


def main():
    hf, a, b = sys.argv[1:4]
    holes = H.read_holes(hf)
    A, B = H.read_modextra(a), H.read_modextra(b)
    if [s for s, _ in A] != [s for s, _ in B]: print('the two files have different holes'); return 1
    bad = 0
    for (s, size, flags, header, sym), (_, x), (_, y) in zip(holes, A, B):
        if x != y:
            bad += 1
            print('DIFFERENT %-28s %-24s %d of %d bytes' % (sym, header, sum(1 for p, q in zip(x, y) if p != q), len(x)))
    print('%d holes, %d different' % (len(holes), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
