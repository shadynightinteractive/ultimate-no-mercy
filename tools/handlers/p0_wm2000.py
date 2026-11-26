#!/usr/bin/env python3
"""p0_wm2000.py (wm2kall, 2026-11-13): are the WrestleMania 2000 handler's Jeff Jarrett and The Blue Meanie the same men as
today's built-in ports? (P0: offline, exact -- p0_check.py's checks, for the two WrestleMania 2000 ports)

    python p0_wm2000.py <out folder>

For each WrestleMania 2000 folder with a built-in twin (its fixes' legacy_game_id is in roster_ext.h's ext_late):
  model    each costume's descriptor == the built-in one (data\\gen_data\\wmport_models.pkl, which gen/mkovr.py compiles into
           parts_ovr.h -- checked too) byte for byte but the file fields and 0x254; each file field: the folder's file has the
           bytes of the built-in file it stands for (mod\\build\\wmport.bin, or the cartridge's own); pairs stay pairs
  photo    photo.bin == the built-in photo (mod\\src\\jarrett_photo.h / meanie_photo.h: palette + picture)
  record   name, short name, height, weight, music, video == roster_ext.h ext_late; no nick
  looks    4 x 38 fields == the built-in port's (parts_ext.h ext_looks)
  moveset  moves.json's 292 slots and 55 abilities == the game's own unpacking of the built-in packed moveset with its cells
           set (as roster.c sets them); the packed 216 bytes == the built-in's
  theme    (what 0.82 changes) theme.bin / theme.meta == the Badd Blood handler's conversion of the sound his music SLOT plays
           (the entrance table: slot 23 -> sound 22, slot 30 -> sound 35), and that is the sound named in the report
Writes <out>\\p0_wm2000.txt.
"""
import json, os, re, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers.aki import model as M
from handlers.baked import Baked
from handlers.wm2000 import game as G
from p0_check import ovr_models
from wm2k_import import costume0

PHOTO_H = {"jarrett": ("jarrett_photo.h", "jj"), "meanie": ("meanie_photo.h", None)}

def built_photo(key):
    t = open(os.path.join(G.DEV, "mod", "src", PHOTO_H[key][0])).read()
    tl = re.search(r"_photo_tlut\[32\] = \{(.*?)\};", t, re.S).group(1)
    ci = re.search(r"_photo_ci4\[2048\] = \{(.*?)\};", t, re.S).group(1)
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", tl)) + bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", ci))

def main():
    out = sys.argv[1]
    b = Baked()
    fx = G.fixes()
    import mkparts as P, mkroster as K
    from aki_import import LOOK_OFFSETS
    from handlers.baddblood import game as BB
    ovr = ovr_models()
    wm = G.WM2000()
    sel = open(os.path.join(G.RIG, "cap", "sel1_ram.bin"), "rb").read()
    R = wm.roster()
    lines, bad_total = [], 0
    for sid, f in fx.items():
        if "legacy_game_id" not in f: continue
        d = os.path.join(out, G.COLLECTION, sid)
        probs = []
        if not os.path.exists(os.path.join(d, "wrestler.json")):
            lines.append("%-16s MISSING" % sid); bad_total += 1; continue
        w = json.load(open(os.path.join(d, "wrestler.json")))
        descs, files = M.read(d, first=0x6400)
        for c in range(4):
            bk = b.models[(f["key"], c)]
            if ovr.get(costume0(f["key"]) + c) != bk: probs.append("c%d: the pkl model isn't parts_ovr.h's" % c)
            fd = descs[c]
            ff = set(o for o in M.FIELDS if struct.unpack_from(">H", fd, o)[0] in files)
            for i in range(0x270):
                if i == 0x254 or any(o <= i < o + 2 for o in ff): continue
                if fd[i] != bk[i]: probs.append("c%d byte 0x%X: %02X, built-in %02X" % (c, i, fd[i], bk[i])); break
            for o in sorted(ff):
                mine = files[struct.unpack_from(">H", fd, o)[0]]
                theirs = b.file(struct.unpack_from(">H", bk, o)[0])
                if mine != theirs: probs.append("c%d field 0x%X: file differs (%d / %s bytes)" % (c, o, len(mine), theirs and len(theirs)))
            for k in range(1, 28):
                o = 8 * k if k <= 20 else 0xA8 + 8 * (k - 21)
                t1, p1 = struct.unpack_from(">HH", fd, o + 2); t2, p2 = struct.unpack_from(">HH", bk, o + 2)
                if (t1 == p1 + 1) != (t2 == p2 + 1): probs.append("c%d part %d: the pairing differs" % (c, k))
        if open(os.path.join(d, "photo.bin"), "rb").read() != built_photo(f["key"]): probs.append("photo differs from the built-in")
        e = b.late[b.late_index(int(f["legacy_game_id"], 16))]
        for k1, k2 in (("name", "name"), ("short_name", "short"), ("height", "h"), ("weight", "w"), ("music", "music"), ("video", "video")):
            if w.get(k1) != e[k2]: probs.append("%s %r, built-in %r" % (k1, w.get(k1), e[k2]))
        for c in range(4):
            want = [b.looks[costume0(f["key"]) + c][o] for o in LOOK_OFFSETS]
            if (w.get("looks") or [None] * 4)[c] != want: probs.append("looks c%d differ" % c)
        if w.get("nick", "") is not None: probs.append("nick %r, the built-in has none" % w.get("nick"))
        mv = json.load(open(os.path.join(d, "moves.json")))
        row = b.late_row[b.late_index(int(f["legacy_game_id"], 16))]
        packed = b.movesets[row]
        saved = []
        for (lst, cell, move) in b.cells[row]:
            a_ = K.EV.r32(0x80045500 + 4 * lst) + 2 * cell
            saved.append((a_, K.EV.r16(a_))); K.EV.w16(a_, move)
        back, bab = K.unpack(K.EV, K.VAN_UNPACK, packed[:0xB8], packed[0xB8:])
        for a_, old in saved: K.EV.w16(a_, old)
        diff = [i for i in range(292) if tuple(back[i]) != tuple(mv["slots"][i])]
        if diff: probs.append("moveset: %d slots differ, first %s: handler %s, built-in %s" % (len(diff), diff[0], mv["slots"][diff[0]], back[diff[0]]))
        if list(bab) != list(mv["abilities"]): probs.append("abilities differ")
        if bytes.fromhex(w["moveset"]) != packed: probs.append("packed moveset isn't the built-in one")
        # the theme: 0.82's correction -- the sound of his music slot, not the slot's number taken as a sound
        rec = R[f["wm2000"]["roster_index"]]
        tb, tm = open(os.path.join(d, "theme.bin"), "rb").read(), open(os.path.join(d, "theme.meta"), "rb").read()
        again, again_meta, about, _ = BB.theme_folder(wm.files.rom, sel, rec["music"])
        if (tb, tm) != (again, again_meta): probs.append("theme.bin / theme.meta aren't the conversion of slot %d's sound" % rec["music"])
        theme_note = "theme = music slot %d -> sound %d" % (rec["music"], about["sound"])
        bad_total += len(probs)
        lines.append("%-16s %s" % (sid, ("IDENTICAL to the built-in port (model 4 costumes: descriptor + %d files; photo; record; looks 4 x 38; "
                                         "no nick; moveset 292 slots + abilities + packed); %s" % (len(files), theme_note)) if not probs
                                        else "DIFFERENT: " + "; ".join(probs[:6])))
    lines.append("%d folders checked, %d differences" % (len([l for l in lines]), bad_total))
    print("\n".join(lines))
    open(os.path.join(out, "p0_wm2000.txt"), "w").write("\n".join(lines) + "\n")
    sys.exit(1 if bad_total else 0)

if __name__ == "__main__":
    main()
