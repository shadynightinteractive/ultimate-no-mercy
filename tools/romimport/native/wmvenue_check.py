#!/usr/bin/env python3
"""wmvenue_check.py (phase3 p3wmvenues, 2026-11-19): ri_cli wmvenue's output (ri_wmvenues.cpp: WrestleMania 2000's seven
venues as arcade arenas, their data side) against mod\\gen\\arenas_wm.py itself, byte for byte.

The Python is imported UNCHANGED from this source tree; only its data paths are pointed at the ROM-only reference tree
(module globals set after import, all read-only): arenas_wm.WMF -> <private>\\romonly\\data\\extracted\\wm2k_files (the
chain's WrestleMania 2000 files), SCENES_JSON / VENUES_JSON -> <private>\\romonly\\data\\wmarena\\*.json, and gen/arenas.py's
REV_RAM (arenas_ring reads Revenge's lists from it when imported) -> <private>\\romonly\\data\\arenaport\\rev\\cap. Each venue
is written as text the same way ri_cli wmvenue writes it (dump() below: names, hex of every mesh / picture, numbers).

    python wmvenue_check.py <private folder> <ri_cli wmvenue out folder> [--py <folder for the Python's text>] [v ...]

ri_cli wmvenue writes <out>\\<v>.txt; this writes (or, with --py and a file already there, reuses) py_<v>.txt and compares.
Prints SAME / DIFFERENT per venue (the first differing line); exit 1 when anything differs."""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
TRON_K = (13, 22)          # (two arena numbers for the tron rows' text: the number is only printed)


def load(priv):
    """arenas_wm imported from this tree with its data paths on the ROM-only reference tree"""
    sys.path.insert(0, os.path.join(SRC, "mod", "gen"))
    ro = os.path.join(priv, "romonly", "data")
    import arenas
    arenas.REV_RAM = os.path.join(ro, "arenaport", "rev", "cap", "m%d_ram.bin")
    import arenas_wm
    arenas_wm.WMF = os.path.join(ro, "extracted", "wm2k_files", "%04x.bin")
    arenas_wm.SCENES_JSON = os.path.join(ro, "wmarena", "wm3d_scenes.json")
    arenas_wm.VENUES_JSON = os.path.join(ro, "wmarena", "wm_venues.json")
    return arenas_wm


def dump(A, v):
    """venue v as text: the same lines ri_cli wmvenue writes (ri_cli_wmvenue.inc)"""
    from arenas_ring import Mesh_from_file
    V = A.venue(v)
    out = ["venue %d %s" % (v, V.name), "has_screen %d" % (V.screen_model is not None)]
    def pic(p): q = A._pic(p); return q[1].hex() + " " + q[2].hex()
    def piece(sec, i, name, mb, p): out.append("%s %d %s %s %s" % (sec, i, name or "-", mb.hex(), pic(p)))
    st, cl = A.stage_parts(v), A.close_parts(v)
    out.append("stage %d" % len(st))
    for i, (n, m, p) in enumerate(st): piece("stage", i, n, A.encode(m), p)
    out.append("close %d" % len(cl))
    for i, (n, m, p) in enumerate(cl): piece("close", i, n, A.encode(m), p)
    L = A.ring_lists(v)
    m, p = L["A"][0]
    piece("A", 0, "", A.encode(m), p); out.append("A_cage %s" % A.encode(A.caged(m)).hex())
    for sec in ("apron", "floor"):
        out.append("%s %d" % (sec, len(L[sec])))
        for i, (m, p) in enumerate(L[sec]):
            piece(sec, i, "", A.encode(m), p); out.append("%s_cage %d %s" % (sec, i, A.encode(A.caged(m)).hex()))
    out.append("barricade %d" % len(L["barricade"]))
    for i, (m, p) in enumerate(L["barricade"]): piece("barricade", i, "", A.encode(m), p)
    out.append("corner %d" % len(L["corner"]))
    for i, (m, p) in enumerate(L["corner"]):
        piece("corner", i, "", A.encode(m), p)
        out.append("corner_cage %d %s" % (i, A.encode(m.map(lambda q: (q[0] - 28, q[1], q[2] + 28))).hex()))
    for g, (name, n, two) in enumerate(A.GROUPS):
        out.append("group %d %s %d" % (g, name, len(L[name])))
        for i, (m, p) in enumerate(L[name]):
            a, b = (p[0], p[1]) if two else (p, p)
            out.append("item %d %d %s %s %s" % (g, i, A.encode(m).hex(), pic(a), pic(b)))
    out.append("empty %s" % A.encode(A.empty()).hex())
    rr = A.rr_pieces(v)
    out.append("rr %d" % len(rr))
    for i, (mb, t, pl, turns) in enumerate(rr): out.append("rr %d %s %s %s %d" % (i, mb.hex(), t.hex(), pl.hex(), turns))
    out.append("logo %s %s" % (A.wfile(V.logo + 1).hex(), A.wfile(V.logo).hex()))
    if V.screen_model is not None:
        # (tron_rows' numbers: its ext() and fold() as it computes them)
        def ext(m):
            q = Mesh_from_file(A.encode(m)) if not A.is_alt(m) else m
            return (min(p[0] for p in q.pts), max(p[0] for p in q.pts), min(p[1] for p in q.pts), max(p[1] for p in q.pts))
        sm = next((m, p) for n, m, p in st if n == "screen"); cm = next((m, p) for n, m, p in cl if n == "m%d" % V.screen_model)
        sig = A.fold(sm[1][1][8:8 + 2048])
        out.append("tron_match %d %d %d %d" % ext(sm[0])); out.append("tron_close %d %d %d %d" % ext(cm[0]))
        out.append("sig %08x %08x" % sig)
    for k in TRON_K:
        for r in A.tron_rows(k, v): out.append("row %s" % r)
    return "\n".join(out) + "\n"


def main():
    args = sys.argv[1:]
    if len(args) < 2: print(__doc__); sys.exit(2)
    priv, nat = args[0], args[1]
    pyd = None
    rest = []
    i = 2
    while i < len(args):
        if args[i] == "--py": pyd = args[i + 1]; i += 2
        else: rest.append(int(args[i])); i += 1
    vs = rest or list(range(7))
    pyd = pyd or nat
    A = load(priv)
    bad = 0
    for v in vs:
        pf = os.path.join(pyd, "py_%d.txt" % v)
        if os.path.exists(pf) and pyd != nat: py = open(pf).read()
        else:
            py = dump(A, v)
            os.makedirs(pyd, exist_ok=True); open(pf, "w", newline="\n").write(py)
        nf = os.path.join(nat, "%d.txt" % v)
        na = open(nf).read() if os.path.exists(nf) else None
        if na == py: print("venue %d: SAME (%d lines)" % (v, py.count("\n"))); continue
        bad += 1
        if na is None: print("venue %d: DIFFERENT (no %s)" % (v, nf)); continue
        pl, nl = py.split("\n"), na.split("\n")
        k = next((k for k in range(min(len(pl), len(nl))) if pl[k] != nl[k]), min(len(pl), len(nl)))
        cut = lambda s: s if len(s) < 200 else s[:200] + "..."
        print("venue %d: DIFFERENT at line %d\n  py:     %s\n  native: %s" % (v, k + 1, cut(pl[k] if k < len(pl) else "(end)"),
                                                                        cut(nl[k] if k < len(nl) else "(end)")))
    print("wmvenue: %d of %d SAME" % (len(vs) - bad, len(vs)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
