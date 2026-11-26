#!/usr/bin/env python3
"""wm_painter_freeze.py (phase3 p3wmvenues, 2026-11-19): the drawing orders of WrestleMania 2000's venues' stage sets and
close scenes, frozen for the native builder (ri_wmvenues.cpp) -- and the developer's check that they are what the Python
picks.

mod\\gen\\arenas_wm.py painter(pieces, views) orders a list's pieces for No Mercy's depth-buffer-less drawing: it renders
each piece's depth from a few cameras into coarse numpy depth maps (160 x 120), counts where one piece is nearer than
another and picks a topological order of that (ties by mean depth). That is a heuristic CHOICE whose float-exact port
(numpy rasterising, barycentric divisions) is out of proportion, so under the FREEZE rule its result -- for each venue v
the index order stage_parts(v) and close_parts(v) take their pieces in (indexes into the list painter() is given) -- is
our data: tools\\romimport\\frozen\\wm_painter_order.json (producer arenas_wm.painter). The pieces themselves (meshes,
pictures) are always made from the player's ROM.

    python wm_painter_freeze.py make  <private folder>    runs the Python's painter() for the 7 venues x 2, writes the file
    python wm_painter_freeze.py check <private folder>    the Python's painter() order == the frozen one, all 7 x 2

(<private folder>: as wmvenue_check.py -- the Python reads the ROM-only reference tree's WrestleMania 2000 files and JSON.)"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wmvenue_check
FROZEN = os.path.join(wmvenue_check.SRC, "tools", "romimport", "frozen", "wm_painter_order.json")


def orders(priv):
    """{v: {"stage": [...], "close": [...]}} -- painter()'s results as stage_parts / close_parts call it"""
    A = wmvenue_check.load(priv)
    real, got = A.painter, {}
    def rec(pieces, views):
        o = real(pieces, views)
        got[{id(A.MATCH_VIEWS): "stage", id(A.CLOSE_VIEWS): "close"}[id(views)]] = (len(pieces), list(o))
        return o
    A.painter = rec
    out = {}
    for v in range(7):
        got.clear(); A.stage_parts(v); A.close_parts(v)
        out[str(v)] = {k: got[k] for k in ("stage", "close")}
    A.painter = real
    return out


def main():
    mode, priv = sys.argv[1], sys.argv[2]
    got = orders(priv)
    if mode == "make":
        L = ['{', '  "producer": "mod/gen/arenas_wm.py painter(): stage_parts(v) with MATCH_VIEWS, close_parts(v) with CLOSE_VIEWS",',
             '  "what": "per venue v (0..6): n = the pieces painter() is given, order = the indexes in drawing order (far first)",',
             '  "venues": {']
        for v in range(7):
            row = ", ".join('"%s": {"n": %d, "order": [%s]}' % (k, got[str(v)][k][0], ", ".join(str(i) for i in got[str(v)][k][1]))
                            for k in ("stage", "close"))
            L.append('    "%d": {%s}%s' % (v, row, "," if v < 6 else ""))
        L += ['  }', '}']
        open(FROZEN, "w", newline="\n").write("\n".join(L) + "\n")
        print("wrote %s" % FROZEN)
        return
    fr = json.load(open(FROZEN))["venues"]
    bad = []
    for v in range(7):
        for k in ("stage", "close"):
            n, o = got[str(v)][k]
            if fr[str(v)][k] != {"n": n, "order": o}: bad.append("%d %s" % (v, k))
    print("painter orders: %d of 14 SAME%s" % (14 - len(bad), (": DIFFERENT " + ", ".join(bad)) if bad else ""))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
