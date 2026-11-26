#!/usr/bin/env python3
"""p0_nomercy.py (nmhandler, 2026-11-13): is every stock No Mercy folder the same Superstar the game builds? (P0: offline, exact)

    python p0_nomercy.py <out folder> [--rom <No Mercy ROM>]

The truth is what the GAME builds, with its own code, run from the user's ROM (rig\\harness\\mipsemu.py on a RAM image made of
the ROM's main segment; game files served from the ROM by the shared AKI file reader):
  record   for each costume, the ROM's own func_8000B834(id, costume, record) -- the function the game builds a costume record
           with -- into a record full of random bytes; then the folder's record built the way the loader builds it (mod
           src/roster.c stock_build: the names zeroed and copied, the looks packed by the game's func_8000B60C from the
           folder's 38 fields, weight / height / music / video) into the same random bytes. All 0x4E bytes compared: a byte
           the game writes and the folder path doesn't (or the other way round) shows.
  moveset  the ROM's own func_8000B914(id, buffer) (his moves file and abilities file read in) == the folder's 216 packed
           bytes; moves.json's 292 slots and 55 abilities == the game's unpack (func_80005EFC) of those bytes, each move
           "nomercy:<n>"
  model    per costume, the game's model builder (func_80018A3C) on the looks the game unpacks (func_8001AC44) from the
           folder's packed record == on the ROM record's: the 0x270-byte descriptors
  photo    every photo number his looks name: photo.bin / photo_c<k>.bin == the ROM's palette + picture; and served the way the
           mod serves a folder photo (mod src/files.c: TEX header + palette + picture, the picture's 8-byte header) the three
           photo files come out byte for byte as the ROM's
  music    the record's music slot is the game's (the record check), and the entrance table's song for it is the same
  reader   every ROM file read here == data\\extracted\\nmfiles (the project's earlier extraction), byte for byte
Plus the game's records as a running game built them (data\\gen_data\\van_sel_ram.bin, the Superstar Select: ROSTER_RECS /
ROSTER_MOVES) for every ordinal but Benoit's (that image has Big Show in his box), the second, independent truth.
Writes <out>\\p0_report.txt.
"""
import argparse, json, os, random, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers.nomercy import game as G
import nm_import as I

REC_TEST = 0x80790000
MOVES_TEST = 0x80791000
FILES_AT = 0x80600000                      # where file 1 and file 2 go in the image (ROSTER_APP / ROSTER_NAMES point here)
ROSTER_APP, ROSTER_NAMES = 0x80047EA0, 0x80047EA4
BUILD_RECORD, LOAD_MOVES, READ_FILE = 0x8000B834, 0x8000B914, 0x80003C94
TEX_HEADER = bytes([0x54, 0x45, 0x58, 0, 0x00, 0x40, 0x00, 0x40, 0x04, 0x02, 0x00, 0x10, 0, 0, 0, 0])   # (mod src/files.c tex_header)
CI4_HEADER = bytes([0x3F, 0x3F, 0x00, 0x20, 0x00, 0x00, 0x06, 0x06])                                    # (... ci4_header)

def folder_record(code, j, c, start):
    """the folder's costume c record as the loader builds it (mod src/roster.c stock_build), over the bytes `start`"""
    rec = bytearray(start)
    r = dict((k, j[k]) for k in I.RECORD_KEYS if k in j)
    r.update((j.get("per_costume") or [{}] * 4)[c])
    def put(at, s, size):                                # (zeroed, then the text: names_into_record's way)
        b = (s or "").encode("latin1")[:size - 1]
        rec[at:at + size] = b + bytes(size - len(b))
    put(0x00, r["name"], 0x15); put(0x15, r["short_name"], 0x0B); put(0x20, r["nick"], 0x11)
    rec[0x31:0x48] = code.pack_looks(G.app_of(j["looks"][c]))
    struct.pack_into(">H", rec, 0x48, r["weight"]); rec[0x4A] = r["height"]; rec[0x4B] = r["music"]; rec[0x4C] = r["video"]
    return bytes(rec)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--rom", default=None)
    a = ap.parse_args()
    nm = G.NoMercy(a.rom)
    code = G.Code(nm)
    E = code.E
    # the game's roster files where its getters look for them
    f1, f2 = nm.files(G.FILE_APPEARANCE), nm.files(G.FILE_NAMES)
    E.wbytes(FILES_AT, f1); E.wbytes(FILES_AT + 0x10000, f2)
    E.w32(ROSTER_APP, FILES_AT); E.w32(ROSTER_NAMES, FILES_AT + 0x10000)
    used = {G.FILE_APPEARANCE, G.FILE_NAMES}
    def read_file(e):                                   # func_80003C94(info, file, dest): the file from the ROM
        f = e.r[5] & 0xFFFF
        e.wbytes(e.r[6], nm.files(f)); used.add(f)
        return True
    E.hooks[READ_FILE] = read_file
    sids = I.builtin_ids()
    rnd = random.Random(20261113)
    van = open(os.path.join(G.DEV, "data", "gen_data", "van_sel_ram.bin"), "rb").read()
    vr32 = lambda x: struct.unpack_from(">I", van, x & 0x7FFFFF)[0]
    van_recs, van_moves = vr32(0x80047E90), vr32(0x80047E94)
    lines, bad_total, checks = [], 0, 0
    for w in G.stock(nm):
        arcade_id = I.ID_BENOIT_ARCADE if w["id"] == I.ID_BENOIT_ROM else w["id"]
        sid = sids[arcade_id]
        d = os.path.join(a.out, I.COLLECTION, sid)
        probs = []
        if not os.path.exists(os.path.join(d, "wrestler.json")):
            lines.append("%-20s MISSING" % sid); bad_total += 1; continue
        j = json.load(open(os.path.join(d, "wrestler.json"), encoding="utf-8"))
        mv = json.load(open(os.path.join(d, "moves.json"), encoding="utf-8"))
        if j.get("builtin") != sid: probs.append("builtin %r" % j.get("builtin"))
        # record, per costume
        for c in range(4):
            start = bytes(rnd.getrandbits(8) for _ in range(G.REC_SIZE))
            E.wbytes(REC_TEST, start)
            E.call(BUILD_RECORD, [w["id"], c, REC_TEST])
            game = E.rbytes(REC_TEST, G.REC_SIZE)
            mine = folder_record(code, j, c, start)
            checks += 1
            if mine != game:
                k = next(i for i in range(G.REC_SIZE) if mine[i] != game[i])
                probs.append("record c%d byte 0x%02X: folder %02X, game %02X" % (c, k, mine[k], game[k]))
            # the running game's record (van_sel_ram.bin), 0x00-0x4C (0x4D: never written by the builder: the heap's byte)
            if w["ordinal"] != 14:
                at = (van_recs + (w["ordinal"] * 4 + c) * G.REC_SIZE) & 0x7FFFFF
                if van[at:at + 0x4D] != mine[:0x4D]: probs.append("record c%d isn't the running game's (van_sel_ram)" % c)
            # model: the game's builder on the looks it unpacks from each record
            mf = code.model(code.unpack_looks(mine[0x31:0x48])); mg = code.model(code.unpack_looks(game[0x31:0x48]))
            if mf != mg: probs.append("model c%d: descriptors differ" % c)
            # music: the slot's song
            if nm.entrance(mine[0x4B])["song"] != nm.entrance(game[0x4B])["song"]: probs.append("music c%d song differs" % c)
        # moveset
        E.wbytes(MOVES_TEST, bytes(rnd.getrandbits(8) for _ in range(0xD8)))
        E.call(LOAD_MOVES, [w["id"], MOVES_TEST])
        game_moves = E.rbytes(MOVES_TEST, 0xD8)
        mine_moves = bytes.fromhex(j["moveset"])
        if mine_moves != game_moves: probs.append("packed moveset differs from func_8000B914's")
        if w["ordinal"] != 14:
            at = (van_moves + w["ordinal"] * 0xD8) & 0x7FFFFF
            if van[at:at + 0xD8] != mine_moves: probs.append("packed moveset isn't the running game's (van_sel_ram)")
        slots, abil = code.unpack_moves(game_moves[:0xB8], game_moves[0xB8:])
        fslots = [[int(s[0].split(":")[1]) if isinstance(s[0], str) else s[0]] + s[1:] for s in mv["slots"]]
        if any(not (isinstance(s[0], str) and s[0].startswith("nomercy:")) for s in mv["slots"]): probs.append("a move not named nomercy:<n>")
        if fslots != slots:
            k = next(i for i in range(292) if fslots[i] != slots[i])
            probs.append("moves.json slot %d: %s, game %s" % (k, fslots[k], slots[k]))
        if mv["abilities"] != abil: probs.append("moves.json abilities differ")
        # photos
        photos = {}
        for c in range(4):
            p = j["looks"][c][G.PHOTO_FIELD]
            if p in photos: continue
            name = "photo.bin" if p == j["looks"][0][G.PHOTO_FIELD] else "photo_c%d.bin" % c
            photos[p] = name
            fp = os.path.join(d, name)
            if not os.path.exists(fp): probs.append("no %s for photo %d" % (name, p)); continue
            pb = open(fp, "rb").read()
            _, rf = nm.photo(p)
            used.update((G.PHOTO_PAL + 2 * p, G.PHOTO_PIC + 2 * p, G.PHOTO_TEX + p))
            if len(pb) != 32 + 2048: probs.append("%s size %d" % (name, len(pb))); continue
            if pb[:32] != rf["pal"] or CI4_HEADER + pb[32:] != rf["pic"] or TEX_HEADER + pb != rf["tex"]:
                probs.append("photo %d: served from %s, a file differs from the ROM's" % (p, name))
        bad_total += len(probs)
        lines.append("%-20s %s" % (sid, ("IDENTICAL (4 records x 0x4E bytes, func_8000B834's and the running game's; 4 models x 0x270; "
                                         "moveset 0xD8 + 292 slots + 55 abilities; photo%s %s; music slots/songs)" %
                                         ("s" if len(photos) > 1 else "", "/".join(str(p) for p in photos)))
                                        if not probs else "DIFFERENT: " + "; ".join(probs[:6])))
    # the shared reader against the project's earlier extraction
    ex = os.path.join(G.DEV, "data", "extracted", "nmfiles", "f")
    rdiff = [f for f in sorted(used) if os.path.exists(os.path.join(ex, "%04x.bin" % f)) and open(os.path.join(ex, "%04x.bin" % f), "rb").read() != nm.files(f)]
    rmiss = [f for f in sorted(used) if not os.path.exists(os.path.join(ex, "%04x.bin" % f))]
    lines.append("reader: %d ROM files used, %d differ from data\\extracted\\nmfiles, %d not extracted there" % (len(used), len(rdiff), len(rmiss)))
    bad_total += len(rdiff)
    lines.append("%d folders checked (%d costume records), %d differences" % (len(lines) - 1, checks, bad_total))
    print("\n".join(lines))
    open(os.path.join(a.out, "p0_report.txt"), "w", newline="\n").write("\n".join(lines) + "\n")
    return 1 if bad_total else 0

if __name__ == "__main__":
    sys.exit(main())
