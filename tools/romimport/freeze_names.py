"""freeze_names.py (romimport, 2026-11-18; run once, by the developer): data\\nmport\\names.pkl's key list, frozen as our data.

names.pkl (vnames, nnames: move number -> move name, No Mercy's and No Mercy Plus V3's) was made by a lost producer from
the games' RAM. Its keys and their order are the generators' (mkroster, wmlib, mkcommentary ...); its texts are the games'.
So what is kept (tools\\romimport\\names_index.json) is, per key in the pickle's order, WHERE its text is in the game's
own files: [key, file, offset] -- the 0-ended string at that offset of that file (the first file, in the order below, that
has those bytes followed by a 0); [key, null, text] only for a text that is ours (Python's "None"). names.py rebuilds the
pickle's two dicts from the ROMs with it, and checks them equal to the pickle when it is there.

    python freeze_names.py <names.pkl> <out json>"""
import json, os, pickle, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from roms import Roms

FIRST = (0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x69)

def locate(F, text):
    b = text.encode("latin1") + b"\0"
    order = list(FIRST) + [f for f in range(1, F.count() + 1) if f not in FIRST]
    for f in order:
        d = F(f)
        if d is None: continue
        at = d.find(b)
        if at >= 0: return f, at
    return None

def main():
    v, n = pickle.load(open(sys.argv[1], "rb"))
    R = Roms()
    out = {}
    for label, game, P in (("nomercy", "nomercy", v), ("nmplus_v3", "nmplus_v3", n)):
        F = R.files(game); rows = []; lit = []
        for k, s in P.items():
            if s == "None": rows.append([k, None, s]); continue
            at = locate(F, s)
            if at is None: rows.append([k, None, s]); lit.append((k, s)); continue
            rows.append([k, at[0], at[1]])
        out[label] = rows
        print("%s: %d keys, %d texts not found in its files: %s" % (label, len(rows), len(lit), lit[:10]))
    with open(sys.argv[2], "w", newline="\n") as w:
        w.write("{\n" + ",\n".join('"%s": [\n%s\n]' % (g, ",\n".join(json.dumps(r) for r in rows)) for g, rows in out.items()) + "\n}\n")

if __name__ == "__main__":
    main()
