#!/usr/bin/env python3
"""nm_import.py (nmhandler, 2026-11-13): the stock No Mercy roster as standard wrestler folders, from the user's own ROM.

    python nm_import.py --out <wrestlers folder> [--rom <No Mercy ROM>] [--only id,id,...]

Writes <out>\\No Mercy\\<string id>\\ for each of the game's 64 stock Superstars (ordinals 1-64: wrestlers, managers,
officials), each holding everything the game builds for him, so the game can load him from the folder and keep its own
tables only as the fallback:
  wrestler.json   "builtin" (which built-in he is: his frozen string ID), his record -- name, short name, nickname, height,
                  weight, music slot, Titantron video -- and "per_costume" for a costume whose record differs from the first
                  (a few have another name, size or theme in one costume); "looks": his four costumes' 38 looks fields (his
                  model reference: the game builds his model from them out of its built-in parts); "moveset": the packed
                  216 bytes (his moves file and abilities file, as the game reads them); "source": where each came from
  moves.json      his moveset as a plain slot list (the movesets agent's format): 292 x [move, flag, flag, flag], each move
                  "nomercy:<number>" in No Mercy's own move library, and his 55 abilities -- the game's own unpack of his
                  packed moveset (func_80005EFC, run from the ROM)
  photo.bin       his select photo (16-colour palette + 64 x 64 4-bit picture), the ROM's own files for his photo number;
  photo_c1.bin .. a costume with a photo of its own (a few are another man in that costume: Taka's Funaki, Jim Ross's
                  Jerry Lawler ...) has its own file, named for the costume
The string IDs are the frozen built-in IDs (mod\\gen\\roster_ids.json, rosterids). Writes <out>\\import_report.txt.
"""
import argparse, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers.aki import folder as FO
from handlers.nomercy import game as G

COLLECTION = "No Mercy"
ID_BENOIT_ROM, ID_BENOIT_ARCADE = 0x0201, 0x0205   # (the ROM's Benoit is 0x0201; in this game Big Show has that box and
                                                   #  Benoit is 0x0205: mod src/roster.c ID_BENOIT_NEW, bigshow.c)
RECORD_KEYS = ("name", "short_name", "nick", "height", "weight", "music", "video")

def builtin_ids():
    """game number -> frozen string ID (mod\\gen\\roster_ids.json, read only)"""
    j = json.load(open(os.path.join(G.DEV, "mod", "gen", "roster_ids.json"), encoding="utf-8"))
    return {v: k for k, v in j.items()}

def record_of(c):
    """a costume's record fields, as wrestler.json names them"""
    s = c["struct"]
    return {"name": G.text(c["name"]), "short_name": G.text(c["short"]), "nick": G.text(c["nick"]) or None,
            "height": s["height"], "weight": s["weight"], "music": s["music"], "video": s["video"]}

def folder_of(nm, code, w, sids):
    """-> (wrestler.json, moves.json, photo.bin bytes, report line)"""
    arcade_id = ID_BENOIT_ARCADE if w["id"] == ID_BENOIT_ROM else w["id"]
    sid = sids[arcade_id]
    recs = [record_of(c) for c in w["costumes"]]
    top = recs[0]
    per = [{k: v for k, v in r.items() if v != top[k]} for r in recs]
    looks = [c["looks"] for c in w["costumes"]]
    photos = sorted({l[G.PHOTO_FIELD] for l in looks})
    photo, _ = nm.photo(looks[0][G.PHOTO_FIELD])
    # (a costume whose photo number isn't costume 0's: photo_c<c>.bin, the first costume that has that number)
    more = {}
    for c in range(1, 4):
        p = looks[c][G.PHOTO_FIELD]
        if p != looks[0][G.PHOTO_FIELD] and all(looks[k][G.PHOTO_FIELD] != p for k in range(1, c)): more["photo_c%d.bin" % c] = nm.photo(p)[0]
    slots, abil = code.unpack_moves(w["moves"], w["abilities"])
    s0 = w["costumes"][0]["struct"]
    j = {"about": "%s, a stock Superstar of WWF No Mercy, written from the user's own ROM by the No Mercy handler "
                  "(tools\\handlers\\nm_import.py, 2026-11-13). The game loads him from this folder, field by "
                  "field over its built-in data. Deleting this folder removes him from the game (stock_roster.json lists him); "
                  "run the handler again (--only %s) to bring him back: the only way back."
                  % (top["name"], sid),
         "id": sid, "builtin": sid}
    j.update(top)
    if any(per): j["per_costume"] = per
    j["looks"] = looks
    j["moveset"] = (w["moves"] + w["abilities"]).hex()
    j["source"] = {"game": G.GAME, "handler": "nomercy (2026-11-13)", "rom_sha1": nm.files.sha1,
                   "ordinal": w["ordinal"], "rom_id": "0x%04X" % w["id"],
                   "moves_file": "0x%04X" % s0["moves_file"], "abilities_file": "0x%04X" % s0["abilities_file"],
                   "appearance_entries": [c["struct"]["appearance"] for c in w["costumes"]],
                   "names_entries": [c["struct"]["names"] for c in w["costumes"]],
                   "photo": photos, "music_songs": sorted({nm.entrance(r["music"])["song"] for r in recs}),
                   "model": "built by the game from \"looks\" out of No Mercy's own parts (func_80018A3C)",
                   "music": "a slot of the game's entrance table (its song and entrance lights)"}
    mv = {"about": "%s's moveset: 292 slots [move, flag, flag, flag] as the game unpacks his packed moveset (func_80005EFC), "
                   "each move in No Mercy's own move library (\"nomercy:<move number>\"), and his 55 abilities." % top["name"],
          "slots": [["nomercy:%d" % s[0]] + s[1:] for s in slots], "abilities": abil}
    rep = "%-22s 0x%04X ord %2d  %d costumes%s, photo %s, moves 0x%04X/0x%04X" % (
        sid, arcade_id, w["ordinal"], 4, " (per_costume: %s)" % ", ".join("c%d %s" % (c, "/".join(sorted(p))) for c, p in enumerate(per) if p) if any(per) else "",
        "/".join(str(p) for p in photos), s0["moves_file"], s0["abilities_file"])
    return sid, j, mv, photo, more, rep

def folder_of_sid(w, sids):
    """his frozen string ID (Benoit by the number he has in this game)"""
    return sids[ID_BENOIT_ARCADE if w["id"] == ID_BENOIT_ROM else w["id"]]

def compact(path):
    """rewrites a JSON file with each list of plain numbers on one line (his 38 looks fields per costume), for reading"""
    import re
    t = open(path, encoding="utf-8").read()
    t = re.sub(r"\[\s*(-?\d+(?:,\s*-?\d+)*)\s*\]", lambda m: "[" + ", ".join(x.strip() for x in m.group(1).split(",")) + "]", t)
    open(path, "w", encoding="utf-8", newline="\n").write(t)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--rom", default=None)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    nm = G.NoMercy(a.rom)
    code = G.Code(nm)
    sids = builtin_ids()
    only = {x for x in a.only.split(",") if x}
    report = ["No Mercy handler: ROM sha1 %s" % nm.files.sha1]
    n = 0
    for w in G.stock(nm):
        sid, j, mv, photo, more, rep = folder_of(nm, code, w, sids)
        if only and sid not in only: continue
        d = FO.write(a.out, COLLECTION, sid, j, photo)
        for name, b in more.items(): open(os.path.join(d, name), "wb").write(b)
        compact(os.path.join(d, "wrestler.json"))
        with open(os.path.join(d, "moves.json"), "w", encoding="utf-8", newline="\n") as f: json.dump(mv, f)
        report.append(rep)
        n += 1
    report.append("%d folders in %s" % (n, os.path.join(a.out, COLLECTION)))
    # (2026-11-13, removal) The collection's stock list: every stock Superstar of this ROM, written or not (--only), so the
    # game knows a missing folder of one of them is a man removed, not a man never written (exe arcade_wrestlers.cpp).
    stock = [folder_of_sid(w, sids) for w in G.stock(nm)]
    lst = {"about": "The stock Superstars of WWF No Mercy this collection holds (the No Mercy handler, 2026-11-13). A man listed "
                    "here whose folder is gone is REMOVED from the game: no box, never drawn or cast, while what still names him "
                    "(saves, stories) has his stand-in play him, and a title he held is vacated. Bring him back with the handler "
                    "(nm_import.py --only <id>): the only way back.",
           "handler": "nomercy", "rom_sha1": nm.files.sha1, "builtin": stock}
    os.makedirs(os.path.join(a.out, COLLECTION), exist_ok=True)
    p = os.path.join(a.out, COLLECTION, "stock_roster.json")
    with open(p, "w", encoding="utf-8", newline="\n") as f: json.dump(lst, f, indent=1)
    compact(p)
    report.append("stock list: %s (%d)" % (p, len(stock)))
    open(os.path.join(a.out, "import_report.txt"), "w", newline="\n").write("\n".join(report) + "\n")
    print("\n".join(report))

if __name__ == "__main__":
    main()
