#!/usr/bin/env python3
"""export_movex.py (handlers, 2026-11-12, PROTOTYPE): the last baked piece of a Revenge port's moves into his folder, as data.

    python export_movex.py <out folder>      (after aki_import.py; reads work\\rev_anims_regen.json -- gen/revanims.py's
                                              output, made again from the ROM and the movesets by work\\regen_revanims.py
                                              and checked byte for byte against the live gen_data\\rev_anims.json and
                                              mod\\src\\movex_data.h)
Writes per ported wrestler <folder>\\moves\\movex.json (his rows: which moveset entry plays which Revenge animation pair,
the record bytes and first bytes made No Mercy's, what it goes on to) and moves\\anims\\<name>_a.bin / _d.bin (the attacker's
and the defender's animation files, as the pack has them), and checks each file against mod\\build\\wmport.bin (P0).
The game doesn't read these yet: today movex.c finds a port's rows by his ext_late index (roster_ext.h), so a folder
wrestler gets them only by having the port's moveset bytes. Reading them from the folder is migration step M2."""
import json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers.revenge import game as G
from handlers.baked import Baked

def main():
    out = sys.argv[1]
    j = json.load(open(os.path.join(HERE, "work", "rev_anims_regen.json")))
    b = Baked()
    rma = {m.group(1): int(m.group(2), 16) for m in re.finditer(r"#define RMA_(\w+) 0x([0-9A-F]+)u", open(os.path.join(G.DEV, "mod", "src", "rev_move_anims.h")).read())}
    files = {f["name"]: f for f in j["files"]}
    bad = 0; lines = []
    for sid, fx in G.fixes().items():
        d = os.path.join(out, "Revenge", sid)
        if not os.path.exists(d): continue
        key = fx["key"] if "second_look_of" not in fx else G.fixes()[fx["second_look_of"]]["key"]
        rows = [r for r in j["rows"] if r["key"] == key]
        os.makedirs(os.path.join(d, "moves", "anims"), exist_ok=True)
        names = sorted({r["name"] for r in rows if r.get("name")})
        same = 0
        for n in names:
            fid = rma[n]
            for side, k in (("a", 0), ("d", 1)):
                o, sz = b.wmp[fid + k - b.wmp0]
                data = b.pack[o:o + sz]
                open(os.path.join(d, "moves", "anims", "%s_%s.bin" % (n.lower(), side)), "wb").write(data)
                # (P0: the pack's file is the Revenge file with No Mercy's first byte, as revanims.py made it)
                src = files.get(n)
                if src and isinstance(src.get("fa"), int):
                    rev = open(os.path.join(G.DEV, "data", "extracted", "rev_files", "%04x.bin" % (src["fa"] if k == 0 else src["fd"])), "rb").read()
                    want = bytes([src["b0a"] if k == 0 else src["b0d"]]) + rev[1:]
                    if want == data: same += 1
                    else: bad += 1; lines.append("  %s %s: the pack's file isn't Revenge's with No Mercy's first byte" % (sid, n))
                else: same += 1      # (a joined or No Mercy file: carried as the pack has it)
        json.dump({"about": "his moves played with Revenge's own animations (gen/revanims.py's rows for him; mod movex.c plays them): "
                            "per row his moveset entry, the animation pair (anims/<name>_a.bin, _d.bin), the stand-in move",
                   "rows": rows, "files": [files[n] for n in names if n in files]},
                  open(os.path.join(d, "moves", "movex.json"), "w"), indent=1)
        lines.append("%-16s %3d rows, %3d animation pairs, %d files the same as the pack's" % (sid, len(rows), len(names), same))
    print("\n".join(lines)); print("%d differences" % bad)

if __name__ == "__main__":
    main()
