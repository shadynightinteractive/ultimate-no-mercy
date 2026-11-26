#!/usr/bin/env python3
"""check_wm2000.py (wm2kall, 2026-11-13): the offline checks of the WrestleMania 2000 handler's folders.

    python check_wm2000.py <out folder> [--neg]

  capture  the rig's four-men-a-match captures (rig\\wm_capture_all.py) give the very models of the captures the built-in
           ports are made of (data\\wmport\\cap\\k29_c*, k41_c*, one man a match): for Jarrett and The Blue Meanie, all four
           costumes, No Mercy's descriptor (gen/wmport.py nm_descriptor) and every part's mesh, picture and palette equal
  folder   every folder: wrestler.json's record == the ROM's record (height, weight; name from the select markup or his
           fix), body type == his looks' byte 8 (women: 15), size == No Mercy's code for the scale his capture holds;
           model\\: 4 costumes, every file listed there, each descriptor marked a WrestleMania 2000 model (skel.h magic) and
           built again from the captures byte for byte (build_model_caps); theme.bin / .meta == the conversion of the
           sound his music slot plays; video.bin == his video's frame files in list order (sampled at 7.5 a second);
           photo.bin 2080 bytes; moves.json 292 slots + 55 abilities, and the packed moveset unpacks (the game's own
           unpacking, gen/mkroster.py) to moves.json but for the slots the import says fell back
  --neg    the same on a copy with one thing wrong in each of four folders: each must be caught
Writes <out>\\check_wm2000.txt.
"""
import json, os, shutil, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers import RIG
from handlers.aki import model as M
from handlers.wm2000 import game as G
from handlers.baddblood import game as BB
from handlers.revenge import body as RB
import wm2k_import as I

def check_capture(wm, caps, say):
    import wmport as W
    for k in (29, 41):
        for c in range(4):
            ram, slot = caps.get(k, c)
            d1, p1 = W.capture(ram, slot)
            d0, p0 = W.capture(open(os.path.join(G.DEV, "data", "wmport", "cap", "k%d_c%d" % (k, c), "ram.bin"), "rb").read(), 0)
            say(W.nm_descriptor(d0) == W.nm_descriptor(d1) and p0 == p1,
                "capture k%d c%d: the four-a-match capture (%s slot %d) == the built-in port's capture (No Mercy's descriptor, %d parts)"
                % (k, c, caps.index["%d:%d" % (k, c)][0], slot, len(p0)))

def check_folder(d, wm, R, sel, caps, say):
    import mkroster as K
    w = json.load(open(os.path.join(d, "wrestler.json"), encoding="utf-8"))
    s = w["source"]; k = s["roster_index"]; e = R[k]; sid = os.path.basename(d)
    fx = G.fixes()[sid]
    ported = "legacy_game_id" in fx
    if not ported:
        full, short = G.names(e["raw_name"])
        # (2026-11-15, womenfix: WrestleMania 2000's placeholder weight 600 is No Mercy's code in the folder -- 500 a woman's,
        #  501 a non-wrestler's: wm2k_import.py one())
        want_w = e["weight"]
        if want_w is not None and want_w >= 500 and want_w not in RB.NM_CODE_LBS:
            want_w = RB.NM_WOMAN_WEIGHT if G.body_type(e, 0)[0] == G.NM_WOMAN else RB.NM_STAFF_WEIGHT
        say(w["name"] == fx.get("name", full)[:20] and w["short_name"] == short[:10] and w["height"] == e["height"]
            and w["weight"] == want_w, "%s: name, short name, height, weight == the ROM's record %d (weight %s)" % (sid, k, want_w))
        body = [G.body_type(e, c)[0] for c in range(4)]
        say([l[0] for l in w["looks"]] == body, "%s: body type per costume %s == his looks' (byte 8; a woman 15)" % (sid, body))
        ram, slot = caps.get(k, 0)
        say(w["size"] == RB.nm_size_for_scale(I.scale_of(ram, slot)), "%s: size %d == No Mercy's code for his WrestleMania 2000 scale" % (sid, w["size"]))
        descs, files = M.read(d, first=M.POOL0)
        rep = []
        d2, pool = G.build_model_caps(wm, k, [caps.get(k, c) for c in range(4)], rep, sid)
        same = len(descs) == 4 and all(struct.unpack_from(">I", x, 0x268)[0] == 0x574D324B for x in descs)
        # (the folder's files numbered in list order, the build's by its pool: compare each field by the file's bytes)
        for c in range(4):
            for o in M.FIELDS:
                a, b = struct.unpack_from(">H", descs[c], o)[0], struct.unpack_from(">H", d2[c], o)[0]
                fa = files.get(a); fb = pool.get(b) if pool.ours(b) else None
                if (fa if fa is not None else a) != (fb if fb is not None else b): same = False
            rest = [i for i in range(0x270) if i != 0x254 and not any(o <= i < o + 2 for o in M.FIELDS)]
            if any(descs[c][i] != d2[c][i] for i in rest): same = False
        say(same, "%s: model, 4 costumes == built again from the captures (every field by its file's bytes, every other byte)" % sid)
        mv = json.load(open(os.path.join(d, "moves.json")))
        packed = bytes.fromhex(w["moveset"])
        back, bab = K.unpack(K.EV, K.VAN_UNPACK, packed[:0xB8], packed[0xB8:])
        diff = [i for i in range(292) if tuple(back[i]) != tuple(mv["slots"][i])]
        say(len(mv["slots"]) == 292 and len(mv["abilities"]) == 55 and len(diff) <= s["moveset_fallbacks"] and list(bab) == list(mv["abilities"]),
            "%s: moves.json 292 slots + 55 abilities; the packed moveset unpacks to it but %d slots (the import says %d fell back)"
            % (sid, len(diff), s["moveset_fallbacks"]))
        ph = os.path.join(d, "photo.bin")
        say(os.path.exists(ph) and os.path.getsize(ph) == 2080, "%s: photo.bin, 2080 bytes" % sid)
    if BB.theme_sound(sel, e["music"]) >= 0:
        tb, tm = open(os.path.join(d, "theme.bin"), "rb").read(), open(os.path.join(d, "theme.meta"), "rb").read()
        again, again_meta, about, _ = BB.theme_folder(wm.files.rom, sel, e["music"])
        say((tb, tm) == (again, again_meta), "%s: theme == music slot %d's sound %d converted, byte for byte" % (sid, e["music"], about["sound"]))
    else:
        say(not os.path.exists(os.path.join(d, "theme.bin")), "%s: no theme (his music slot plays none)" % sid)
    if e["video"]:
        vb = open(os.path.join(d, "video.bin"), "rb").read()
        again, _ = BB.video(wm.files, sel, e["video"])
        say(vb == again and len(vb) % BB.FRAME_BYTES == 0, "%s: video.bin == video %d's frame files in list order (%d frames)" % (sid, e["video"], len(vb) // BB.FRAME_BYTES))
    else:
        say(not os.path.exists(os.path.join(d, "video.bin")), "%s: no video (his record's is 0)" % sid)

def main():
    out = sys.argv[1]
    neg = "--neg" in sys.argv
    if neg:
        n = os.path.join(os.path.dirname(os.path.abspath(out)), "neg_copy")
        shutil.rmtree(n, ignore_errors=True); shutil.copytree(out, n); out = n
        root = os.path.join(n, G.COLLECTION)
        w = json.load(open(os.path.join(root, "x_pac_wm2000", "wrestler.json"))); w["size"] += 3
        json.dump(w, open(os.path.join(root, "x_pac_wm2000", "wrestler.json"), "w"))
        p = os.path.join(root, "kane_wm2000", "model", "c2.bin"); b = bytearray(open(p, "rb").read()); b[0x250] ^= 1; open(p, "wb").write(b)
        p = os.path.join(root, "chyna_wm2000", "video.bin"); b = bytearray(open(p, "rb").read()); b[600] ^= 1; open(p, "wb").write(b)
        w = json.load(open(os.path.join(root, "godfather_wm2000", "wrestler.json"))); w["looks"][1][0] = 3
        json.dump(w, open(os.path.join(root, "godfather_wm2000", "wrestler.json"), "w"))
    wm = G.WM2000(); R = wm.roster()
    sel = open(os.path.join(RIG, "cap", "sel1_ram.bin"), "rb").read()
    caps = I.Caps()
    L, bad = [], [0]
    def say(ok, what):
        L.append(("ok    " if ok else "WRONG ") + what); bad[0] += 0 if ok else 1
    if not neg: check_capture(wm, caps, say)
    root = os.path.join(out, G.COLLECTION)
    for sid in sorted(os.listdir(root), key=lambda s: json.load(open(os.path.join(root, s, "wrestler.json")))["source"]["roster_index"]):
        check_folder(os.path.join(root, sid), wm, R, sel, caps, say)
    L.append("%d checks, %d WRONG" % (len(L), bad[0]))
    open(os.path.join(out, "check_wm2000%s.txt" % ("_neg" if neg else "")), "w", encoding="utf-8").write("\n".join(L) + "\n")
    print("\n".join(l for l in L if not l.startswith("ok")))

if __name__ == "__main__":
    main()
