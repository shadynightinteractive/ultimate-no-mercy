#!/usr/bin/env python3
"""check_folders.py (2026-11-13, imports2): the offline check of the two imported folders -- each holds what its source has.

    python tool\\check_folders.py <wrestlers folder>      (-> prints, and <folder>\\check_report.txt)

  goldust  his model.json read the way the game reads it (aki/model.py read: his files given numbers), each field's number
           turned back into the No Mercy Plus file it holds (by its bytes) -> == No Mercy Plus's own builder's descriptor for
           that costume, every byte but the ones the game keeps (0x254-0x257, the entrance attire's); every file
           == No Mercy Plus's (read from its ROM).
  vader    his model.json read the same way -> every field's file == the bytes the capture's part had (mesh from Badd
           Blood's ROM, picture and palette as the match drew them, after nm_skin); his descriptors == the handler's.
  both     theme.bin == the ROM's bytes where the bank's record says; video.bin == his list's frame files, in order;
           moves.json's slots == the mapping; the packed moveset unpacked by the game's code == moves.json but the slots
           the packing can't say (counted, the reference's there); name, size from the record.
A negative test (--neg): one byte changed in a copy of each folder's model file, theme and video -> each must be caught.
"""
import argparse, json, os, shutil, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from handlers.aki import model as M
from handlers.baddblood import game as BB
from handlers.customs import nmplus as NP
from handlers.nomercy import game as NMG

def check(root, out):
    bad = 0
    def say(ok, what):
        nonlocal bad
        out.append(("ok   " if ok else "DIFF ") + what); bad += (not ok)
    bb = BB.BaddBlood(); R = bb.roster()
    sel = open(os.path.join(BB.RIG, "cap", "vsel_ram.bin"), "rb").read()
    sys.path.insert(0, os.path.join(BB.DEV, "mod", "gen")); import mkroster as K
    for sid, k in (("vader", 19), ("goldust", 32)):
        d = os.path.join(root, "Imported", sid); e = R[k]
        w = json.load(open(os.path.join(d, "wrestler.json")))
        full, short, nick = BB.plain_name(e["raw_name"])
        say((w["name"], w["short_name"], w["height"], w["weight"]) == (full, short, e["height"], e["weight"]),
            "%s: name, short name, height, weight == Badd Blood's record %d (%s)" % (sid, k, e["raw_name"]))
        # the theme: the folder's sound, decoded, against the ROM's sound decoded and played at its own rate (the folder's is
        # resampled, levelled and encoded again: compared by shape -- the correlation over the whole sound -- not by bytes)
        import numpy as np
        sys.path.insert(0, os.path.join(BB.DEV, "mod", "gen")); import vadpcm
        _, _, ab, (src, src_rate) = BB.theme_folder(bb.files.rom, sel, e["music"])
        tb = open(os.path.join(d, "theme.bin"), "rb").read(); tm = open(os.path.join(d, "theme.meta"), "rb").read()
        order, npred = struct.unpack_from(">ii", tm, 0x18)
        n_ = struct.unpack_from(">I", tm, 0xA4)[0]
        got = np.asarray(vadpcm.decode(tb, tm[0x18:0x18 + 8 + order * npred * 16], n_), float)
        ref = np.interp(np.arange(len(got)) * src_rate / BB.THEME_RATE, np.arange(len(src)), src)
        r = float(np.corrcoef(got, ref)[0, 1])
        again, again_meta, _, _ = BB.theme_folder(bb.files.rom, sel, e["music"])
        say(tb == again and tm == again_meta, "%s: theme.bin / theme.meta == the handler's conversion of the ROM's sound %d (music slot %d), byte for byte"
            % (sid, ab["sound"], e["music"]))
        # (the shape: resampled, levelled to the game's themes -- a loud song limited -- and VADPCM twice: near, not equal)
        say(r > 0.9 and len(tm) == 0xD8,
            "%s: theme.bin decoded vs the ROM's sound %d (music slot %d; %d bytes at %s, decoded, at %.0f a second): correlation %.4f over %.1f s"
            % (sid, ab["sound"], e["music"], ab["bytes"], ab["rom_offset"], src_rate, r, len(got) / BB.THEME_RATE))
        # (2026-11-13, themefix2) The check above compares the folder with the handler's own reading of the source, so a wrong
        # sound or a wrong rate passed it (the "roar": the slot taken for the sound, 2.67 times too slow). These look outside
        # it: the theme must reach as high as a full-speed theme does, and be the sound Badd Blood itself plays for his entrance
        # (its rig recording) at the speed it plays it (theme_check.py).
        import theme_check as TC
        rec = TC.RECORDINGS.get(sid) if os.path.exists(TC.RECORDINGS.get(sid, "")) else None
        lines, fast, match = TC.check(d, rec)
        say(fast, lines[0])
        if rec: say(match, "%s: theme.bin %s -- best GCC-PHAT peaks at the exact rate %s"
                    % (sid, lines[-1].strip("- >"), lines[1].split("(L, R) ")[1]))
        else: say(False, "%s: no recording of Badd Blood's own entrance for him to compare the theme with" % sid)
        v = open(os.path.join(d, "video.bin"), "rb").read()
        lst = BB.video_list(sel, e["video"])
        frames = [v[i:i + BB.FRAME_BYTES] for i in range(0, len(v), BB.FRAME_BYTES)]
        files = {bb.files(f): f for f, _ in lst}
        # (an entry up for less than a 7.5-a-second frame -- the 2-tick static flash -- can fall between two samples)
        long_ = {f for f, t in lst if t & 0x7FFF >= 4}
        # (compared by content: some of its frame files are byte for byte the same picture)
        say(len(v) % BB.FRAME_BYTES == 0 and all(fr in files for fr in frames) and {bb.files(f) for f in long_} <= set(frames),
            "%s: video.bin's %d frames are each one of video %d's frame files, and every frame up for 4 ticks or more is there"
            % (sid, len(frames), e["video"]))
        mv = json.load(open(os.path.join(d, "moves.json")))
        back, bab = K.unpack(K.EV, K.VAN_UNPACK, bytes.fromhex(w["moveset"])[:0xB8], bytes.fromhex(w["moveset"])[0xB8:])
        diff = [i for i in range(292) if tuple(back[i]) != tuple(mv["slots"][i])]
        say(len(diff) == w["source"]["moveset_fallbacks"] and list(bab) == mv["abilities"],
            "%s: the packed moveset unpacked == moves.json but %d slots (the packing can't say them: %s); abilities the same"
            % (sid, len(diff), diff[:12]))
    # goldust's model against No Mercy Plus's builder
    nm = NP.NMPlus()
    descs, got = M.read(os.path.join(root, "Imported", "goldust"))
    w = json.load(open(os.path.join(root, "Imported", "goldust", "wrestler.json")))
    order = [int(x, 16) for x in w["source"]["looks_and_model"]["its_files"]]
    j = json.load(open(os.path.join(root, "Imported", "goldust", "model", "model.json")))
    say(all(got[0x6400 + i] == nm.files(v) for i, v in enumerate(order)), "goldust: his %d model files == No Mercy Plus's (its ROM)" % len(order))
    _, apps = NP.goldust_apps(nm)
    for c in range(4):
        want = nm.model(NP.un_of(apps[c]), NP.STATES[0])
        dd = bytearray(descs[c])
        for at, i in j["costumes"][c]["fields"]: struct.pack_into(">H", dd, at, order[i])
        keep = set(j["costumes"][c]["keep"])
        diff = [hex(x) for x in range(0x270) if x not in keep and dd[x] != want[x]]
        say(not diff, "goldust costume %d: the folder's descriptor (his numbers as No Mercy Plus's files) == No Mercy Plus's builder's, "
                      "all but %d kept bytes%s" % (c, len(keep), "" if not diff else ": differ at %s" % diff[:10]))
    # vader's model against the handler's build from the captures
    caps = [open(os.path.join(BB.RIG, "cap", "vader_c%d_ram.bin" % c), "rb").read() for c in range(4)]
    vd, pool = BB.build_model(bb, 19, caps, [], "vader")
    descs, got = M.read(os.path.join(root, "Imported", "vader"))
    for c in range(4):
        dd = bytearray(descs[c]); ok = True
        for o in M.FIELDS:
            n = struct.unpack_from(">H", dd, o)[0]
            if n in got:
                ok &= got[n] == pool.get(struct.unpack_from(">H", vd[c], o)[0])
                struct.pack_into(">H", dd, o, struct.unpack_from(">H", vd[c], o)[0])
        diff = [hex(x) for x in range(0x270) if x not in M.KEEP and dd[x] != vd[c][x]]
        say(ok and not diff, "vader costume %d: the folder's descriptor and files == the build from the capture (cap\\vader_c%d_ram.bin)" % (c, c))
    return bad

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("folder"); ap.add_argument("--neg", action="store_true"); a = ap.parse_args()
    out = []
    root = a.folder
    if a.neg:          # a copy with one byte changed in each kind of thing: every one must be caught
        neg = os.path.join(HERE, "work", "neg"); shutil.rmtree(neg, ignore_errors=True); shutil.copytree(root, neg); root = neg
        def flip(p, at): b = bytearray(open(p, "rb").read()); b[at] ^= 1; open(p, "wb").write(bytes(b))
        g = os.path.join(neg, "Imported", "goldust"); v = os.path.join(neg, "Imported", "vader")
        flip(os.path.join(g, "model", json.load(open(os.path.join(g, "model", "model.json")))["files"][3]), 40)
        flip(os.path.join(v, "model", "c2.bin"), 0x262)
        flip(os.path.join(g, "theme.bin"), 1000); flip(os.path.join(v, "video.bin"), 5000)
        out.append("NEGATIVE: goldust model file 3, vader c2 descriptor byte 0x262 (his hands kind), goldust theme byte 1000, vader video byte 5000")
    bad = check(root, out)
    out.append("%d differences" % bad)
    open(os.path.join(a.folder, "check_report%s.txt" % ("_neg" if a.neg else "")), "w").write("\n".join(out) + "\n")
    print("\n".join(out))

if __name__ == "__main__":
    main()
