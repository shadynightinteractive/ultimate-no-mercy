#!/usr/bin/env python3
"""wm_rivals.py (rostertags, 2026-11-21): WrestleMania 2000's own rivals, as the handler's data (handlers\\wm2000\\rivals.json).

WrestleMania 2000 keeps, in each Superstar's abilities file, the same four ties No Mercy has (No Mercy's Create-a-Wrestler
calls them "Rival 1", "Rival 2", "Rival 3" and "Accompanied by": abilities 31-34, twelve-bit Superstar IDs) -- read here with
the handler's own unpacking (gen/mkroster.py's port_wm_moveset reads the same file: wm2k.unpack_moves). Its rivals are
abilities 31-33 (Austin's: The Undertaker, Big Show, The Rock; Edge's: Gangrel; the Hardys': Faarooq and Bradshaw); 34 is whom
he comes out with (Edge -> Christian, Jarrett -> Debra), not a rival. Each pair once, either way round, both men WrestleMania
2000's own folders (the handler's string IDs); a tie to someone the import makes no folder for (its two built-in ports, a
record past its roster) is left out.

    python tools\\handlers\\wm_rivals.py            write handlers\\wm2000\\rivals.json from the ROM
    python tools\\handlers\\wm_rivals.py --check    exit 1 unless the file is what the ROM says

Both importers (wm2k_import.py, exe src\\romimport ri_build) copy the file as it is into wrestlers\\WrestleMania 2000\\rivals.json.
"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "mod", "gen"))
from handlers.wm2000 import game as G

OUT = os.path.join(HERE, "handlers", "wm2000", "rivals.json")
ABOUT = ("WrestleMania 2000's own rivals (rostertags, 2026-11-21): each Superstar's \"Rival 1-3\" (abilities 31-33 of his abilities "
         "file, the same ties No Mercy has), read from the ROM by tools\\handlers\\wm_rivals.py, each pair once. The importers copy "
         "this file into wrestlers\\WrestleMania 2000\\rivals.json (only if none is there). Names are this collection's folders. "
         "Edit freely: add pairs to \"rivals\", or list pairs in \"not_rivals\" to take them out.")

def pairs():
    wm = G.WM2000()
    G.use_rom_files(wm)
    import wm2k
    fx = G.fixes()
    sid_of_k = {f["wm2000"]["roster_index"]: s for s, f in fx.items() if "legacy_game_id" not in f}
    R = wm.roster()
    k_of_id = {e["id"]: k for k, e in enumerate(R) if e.get("abilities")}
    out, seen = [], set()
    for k in sorted(sid_of_k):
        e = R[k]
        _, ab = wm2k.unpack_moves(wm.files(e["moves"]), wm.files(e["abilities"]))
        for r in ab[31:34]:
            o = sid_of_k.get(k_of_id.get(r))
            if not o or o == sid_of_k[k]: continue
            key = tuple(sorted((sid_of_k[k], o)))
            if key in seen: continue
            seen.add(key)
            out.append([sid_of_k[k], o])
    return out

def text(ps):
    """the file: one pair a line"""
    rows = ",\n".join("  " + json.dumps(p) for p in ps)
    return '{\n "about": %s,\n "rivals": [\n%s\n ]\n}\n' % (json.dumps(ABOUT), rows)

def main():
    t = text(pairs())
    if "--check" in sys.argv:
        have = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
        print("SAME" if have == t else "DIFFERS"); sys.exit(0 if have == t else 1)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f: f.write(t)
    print("%s: %d pairs" % (OUT, len(json.loads(t)["rivals"])))

if __name__ == "__main__":
    main()
