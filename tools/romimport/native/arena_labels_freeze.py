"""arena_labels_freeze.py (phase3 p3wmport, 2026-11-19): the arcade arenas' ARENA-page labels, frozen for the native pack
builder -- and the developer's check that they are what the Python makes.

mod\\gen\\arenas.py label(text) draws an arena's name in the Anton font (mod\\gen\\fonts, SIL Open Font License) with PIL /
FreeType, squeezes it with Lanczos, rings it with an outline: a 128 x 32 CI4 picture after the game's TEX header and the
arena labels' palette (No Mercy's file 0x462B's first 48 bytes). The pixels are OURS -- our font, our names -- not the
game's, and a byte-exact FreeType + Lanczos port is out of all proportion, so they are frozen here (FREEZE rule:
tools\\romimport\\frozen\\arena_labels.json, text -> the 2048 pixel bytes in hex; producer arenas.label). The header and
palette are always the player's ROM's (ri_wmarenas.cpp puts them in front).

    python arena_labels_freeze.py make  <No Mercy files folder (%04x.bin)>   writes the frozen file
    python arena_labels_freeze.py check <No Mercy files folder (%04x.bin)>   label(text) == header + frozen, for every text
"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(SRC, "mod", "gen"))
FROZEN = os.path.join(SRC, "tools", "romimport", "frozen", "arena_labels.json")
# every label the pack has: gen/arenas.py LABEL (Revenge's six) and gen/arenas_wm.py VENUES' names (WrestleMania 2000's seven)
TEXTS = ["MONDAY NITRO", "HALLOWEEN HAVOC", "BASH AT THE BEACH", "RAW IS WAR WM 2000", "SOULED OUT", "SUPERBRAWL", "STARRCADE",
         "SUNDAY NIGHT HEAT", "ROYAL RUMBLE WM 2000", "KING OF THE RING WM 2000", "SURVIVOR SERIES WM 2000",
         "WRESTLEMANIA WM 2000", "SUMMERSLAM WM 2000"]

def main():
    mode, nmfiles = sys.argv[1], sys.argv[2]
    import arenas
    arenas.NMF = os.path.join(nmfiles, "%04x.bin")
    head = open(arenas.NMF % 0x462B, "rb").read()[:48]
    if mode == "make":
        out = {}
        for t in TEXTS:
            b = arenas.label(t); assert b[:48] == head
            out[t] = b[48:].hex()
        json.dump(out, open(FROZEN, "w"), indent=0)
        print("wrote %s (%d labels)" % (FROZEN, len(out)))
        return
    fr = json.load(open(FROZEN))
    bad = [t for t in TEXTS if arenas.label(t) != head + bytes.fromhex(fr[t])]
    print("arena labels: %d of %d SAME%s" % (len(TEXTS) - len(bad), len(TEXTS), (": DIFFERENT " + ", ".join(bad)) if bad else ""))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
