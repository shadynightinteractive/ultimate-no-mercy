#!/usr/bin/env python3
"""p0_check.py (handlers, 2026-11-12): is a handler's folder the same wrestler as today's baked-in port? (P0: offline, exact)

    python p0_check.py <out folder>

For each Revenge folder that has a baked twin (its fixes' legacy_game_id is in roster_ext.h):
  model    each costume's descriptor == the baked one (data/gen_data/wmport_models.pkl, which gen/mkovr.py compiles into
           parts_ovr.h -- checked too) byte for byte, except the file fields and byte 0x254 (the game's own); each file
           field: the folder's file has the same bytes as the baked file it stands for (mod/build/wmport.bin, or the
           cartridge's own file for a number below 0x4C03); a palette+picture pair stays a pair (picture = palette + 1)
  photo    photo.bin == rev_photos.h's palette + picture
  record   name, short name, height, weight, music, video == roster_ext.h ext_late
  moveset  the handler's 292 slots and 55 abilities (moves.json, mapped from Revenge's list by the handler) == the game's
           own unpacking of the baked packed moveset with the baked cells set (as roster.c sets them)
"""
import json, os, re, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers.aki import model as M
from handlers.baked import Baked
from handlers.revenge import game as G

def ovr_models():
    """parts_ovr.h's models by costume index (the compiled truth)"""
    t = open(os.path.join(G.DEV, "mod", "src", "parts_ovr.h")).read()
    cos = [int(x) for x in re.search(r"ovr_costume\[OVR_N\] = \{([^}]*)\}", t).group(1).split(",")]
    body = t.split("ovr_model[OVR_N][0x270]")[1].split("\n};")[0]
    rows = re.findall(r"\{\n(.*?)\n  \}", body, re.S)
    out = {}
    for c, r in zip(cos, rows): out.setdefault(c, bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", r)))
    return out

def main():
    out = sys.argv[1]
    b = Baked()
    fx = G.fixes()
    import mkparts as P, mkroster as K
    ovr = ovr_models()
    bad_total = 0
    lines = []
    for sid, f in fx.items():
        d = os.path.join(out, "Revenge", sid)
        if not os.path.exists(os.path.join(d, "wrestler.json")): continue
        w = json.load(open(os.path.join(d, "wrestler.json")))
        probs = []
        # model
        descs, files = M.read(d, first=0x6400)
        for c in range(4):
            bk = b.models[(f["key"], c)]
            if ovr.get(P.COSTUME0[f["key"]] + c) != bk: probs.append("c%d: the pkl model isn't parts_ovr.h's" % c)
            fd = descs[c]
            ff = set(o for o in M.FIELDS if struct.unpack_from(">H", fd, o)[0] in files)
            for i in range(0x270):
                if i == 0x254 or any(o <= i < o + 2 for o in ff): continue
                if fd[i] != bk[i]: probs.append("c%d byte 0x%X: %02X, baked %02X" % (c, i, fd[i], bk[i])); break
            for o in sorted(ff):
                mine = files[struct.unpack_from(">H", fd, o)[0]]
                theirs = b.file(struct.unpack_from(">H", bk, o)[0])
                if mine != theirs: probs.append("c%d field 0x%X: file differs (%d / %s bytes)" % (c, o, len(mine), theirs and len(theirs)))
            for k in range(1, 28):                       # (picture = palette + 1, both ways)
                o = 8 * k if k <= 20 else 0xA8 + 8 * (k - 21)
                t1, p1 = struct.unpack_from(">HH", fd, o + 2); t2, p2 = struct.unpack_from(">HH", bk, o + 2)
                if (t1 == p1 + 1) != (t2 == p2 + 1): probs.append("c%d part %d: the pairing differs" % (c, k))
        # photo
        pk = {"brethart": "bret"}.get(f.get("second_look_of") and fx[f["second_look_of"]]["key"] or f["key"], None) or \
             (fx[f["second_look_of"]]["key"] if f.get("second_look_of") else f["key"])
        ph = open(os.path.join(d, "photo.bin"), "rb").read()
        if ph != b.photos.get(pk): probs.append("photo differs from rev_photos.h's %s" % pk)
        # record
        e = b.late[b.late_index(int(f["legacy_game_id"], 16))]
        for k1, k2 in (("name", "name"), ("short_name", "short"), ("height", "h"), ("weight", "w"), ("music", "music"), ("video", "video")):
            if w[k1] != e[k2]: probs.append("%s %r, baked %r" % (k1, w[k1], e[k2]))
        # looks (2026-11-13, handlers1): his 38 fields per costume, as numbers, == the built-in port's costume as the game
        # unpacks it (parts_ext.h ext_looks, which its packed key ext_looks_key unpacks to: parts.c key_of)
        from aki_import import LOOK_OFFSETS
        for c in range(4):
            want = [b.looks[P.COSTUME0[f["key"]] + c][o] for o in LOOK_OFFSETS]
            got = (w.get("looks") or [None] * 4)[c]
            if got != want: probs.append("looks c%d: %s, built-in %s" % (c, got, want))
        if "costumes" in w: probs.append("still has packed \"costumes\"")
        # nick (2026-11-13, handlers1): none (null), as the built-in port's ("" in roster_ext.h: Baked reads only those)
        if w.get("nick", "") is not None: probs.append("nick %r, the built-in has none" % w.get("nick"))
        # moveset
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
        if diff: probs.append("moveset: %d slots differ, first %s: handler %s, baked %s" % (len(diff), diff[0], mv["slots"][diff[0]], back[diff[0]]))
        if list(bab) != list(mv["abilities"]): probs.append("abilities differ")
        if bytes.fromhex(w["moveset"]) != packed: probs.append("packed moveset isn't the baked one")
        bad_total += len(probs)
        lines.append("%-16s %s" % (sid, "IDENTICAL (model 4 costumes: descriptor + %d files; photo; record; looks 4 x 38; no nick; moveset 292 slots + abilities)" % len(files) if not probs else "DIFFERENT: " + "; ".join(probs[:6])))
    print("\n".join(lines))
    print("%d folders checked, %d differences" % (len(lines), bad_total))
    open(os.path.join(out, "p0_report.txt"), "w").write("\n".join(lines) + "\n%d folders checked, %d differences\n" % (len(lines), bad_total))

if __name__ == "__main__":
    main()
