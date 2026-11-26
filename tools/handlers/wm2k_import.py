#!/usr/bin/env python3
"""wm2k_import.py (wm2kall, 2026-11-13): WWF WrestleMania 2000's whole roster as standard wrestler folders, from the user's
own ROM (roms\\WWF_WrestleMania_2000.zip, read in place: the archive is read in memory, nothing is unpacked or copied).

    python wm2k_import.py --out <wrestlers folder> [--only id,id,...]

Writes <out>\\WrestleMania 2000\\<string id>\\ for each of its 58 Superstars (handlers\\wm2000\\game.py says what comes from
where): wrestler.json, model\\ (his own model, four costumes), moves.json, photo.bin, theme.bin + theme.meta, video.bin (none
for a man the game gives no video), and <out>\\import_report.txt -- per wrestler what came over and how.

  jeff_jarrett, the_blue_meanie   the two ported before: made exactly as the built-in ports are (the handler's prototype:
                                  the same captures, photo, looks, packed moveset), so the folders are the same men
                                  (p0_wm2000.py proves it); their themes are the corrected ones (their records' music
                                  SLOTS 23 and 30 play sounds 22 and 35 -- the entrance table), as 0.82's built-ins.
  the other 56                    everything from the ROM and the rig: model (rig\\wm_capture_all.py's captures), name,
                                  height, weight, body type (his looks' byte 8), size (WrestleMania 2000's own scale of
                                  him, from the match), moveset, theme, video, photo (rig\\wm_sel.py's render of him).
                                  (2026-11-13, wm2klib) His moveset's slots whose WrestleMania 2000 move No Mercy has no
                                  move for name it in the built-in WrestleMania 2000 library (wmlib.py: "wm2000:<move>@<No
                                  Mercy move>"); his packed moveset (wrestler.json) is the handler's No Mercy moves still.
Needs the rig's captures first (NM_HANDLERS_RIG, else data\\handlers_rig): cap\\all_index.json + the match RAM images,
cap\\sel1_ram.bin (its Superstar Select: the music bank and the entrance table), cap\\photo_<k>.png (+ .json: the box).
"""
import argparse, json, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers import RIG
from handlers.aki import model as M, photo as PH, folder as FO
from handlers.wm2000 import game as G
from handlers.baddblood import game as BB
from handlers.revenge import body as RB

CAP = os.path.join(RIG, "cap")
ID0 = 0x03A0               # (his game number pinned, 0x03A0 + his roster index: the QA tool names a folder man by number;
                           #  past the Revenge folders' 0x0340-0x0390 and the two imports' 0x0390 / 0x0391)
SCALE0, SCALE_STRIDE = 0x8005B138, 0xC0    # (WrestleMania 2000's whole-model scale per slot in a match: x, y, z at + 0, 0x14,
                                           #  0x28 -- found in the rig: Jarrett / Meanie (5'13") 1.00949, Viscera (5'18")
                                           #  1.0211, X-Pac (5'12") 1.00729 in one match)

class Caps:
    """the rig's captures: (his roster index, costume) -> (match RAM, slot), the RAM images read once each"""
    def __init__(self):
        self.index = json.load(open(os.path.join(CAP, "all_index.json")))
        self.ram = {}
    def get(self, k, c):
        f, slot = self.index["%d:%d" % (k, c)]
        if f not in self.ram:
            if len(self.ram) > 6: self.ram.pop(next(iter(self.ram)))
            self.ram[f] = open(os.path.join(CAP, f), "rb").read()
        return self.ram[f], slot

def scale_of(ram, slot):
    s = [struct.unpack_from(">f", ram, (SCALE0 + SCALE_STRIDE * slot + o) & 0x7FFFFF)[0] for o in (0, 0x14, 0x28)]
    assert max(s) - min(s) < 1e-6 and 0.9 < s[0] < 1.2, "slot %d: no uniform scale at 0x%X (%s)" % (slot, SCALE0, s)
    return s[0]

def ids_by_number():
    j = json.load(open(os.path.join(G.DEV, "mod", "gen", "roster_ids.json"), encoding="utf-8"))
    return {v: k for k, v in j.items()}

def write_theme_video(d, wm, sel, e, w, rep, sid):
    if BB.theme_sound(sel, e["music"]) < 0:
        # (the Hos: music slot 0, whose entrance plays no sound (-1) -- no theme.bin, so his base's slot plays, as for any
        #  folder without one; "music" says which: the source's none can't be said in a slot number)
        w["source"]["theme"] = "none: his record's music slot %d plays no sound in WrestleMania 2000 (its sound is -1)" % e["music"]
        line = "theme: none (music slot %d plays no sound)" % e["music"]
    else:
        data, meta, about, _ = BB.theme_folder(wm.files.rom, sel, e["music"])
        open(os.path.join(d, "theme.bin"), "wb").write(data); open(os.path.join(d, "theme.meta"), "wb").write(meta)
        w["source"]["theme"] = about
        line = "theme: music slot %d -> its sound %d (instrument %d, %d bytes VADPCM, tuning %s, played at %.0f a second) made %d a second, %.1f s, loop from %.1f s" % (
            e["music"], about["sound"], about["instrument"], about["bytes"], about["tuning"], about["source_rate"], about["rate"],
            about["seconds"], about["loop_from_s"])
    if e["video"]:
        vid, vabout = BB.video(wm.files, sel, e["video"])
        open(os.path.join(d, "video.bin"), "wb").write(vid)
        w["source"]["video"] = vabout
        line += "; video %d: %d frames (%d list entries, %d frame files)" % (e["video"], vabout["frames_written"], vabout["entries"], len(vabout["frame_files"]))
    else:
        w["source"]["video"] = "none: his record's video is 0 (the game shows none for him)"
        line += "; no video (his record's is 0)"
    rep.append(line)

def moves_json(d, slots, ab, about, records=None):
    j = {"about": about, "slots": [list(s) for s in slots], "abilities": list(ab)}
    if records: j["records"] = {str(i): v for i, v in sorted(records.items())}    # (2026-11-13, movesteps: WrestleMania 2000's record values)
    json.dump(j, open(os.path.join(d, "moves.json"), "w"))

def costume0(key):
    """the built-in port's first costume in parts_ext.h (gen/mkparts.py COSTUME0; Jarrett's, the first port's, is 4 x the
    ext roster, as gen/mkovr.py has it)"""
    import mkparts as P
    return P.COSTUME0[key] if key in P.COSTUME0 else 4 * len(P.R.EXT)

def ported(out, wm, sel, fx, e, report):
    """Jeff Jarrett / The Blue Meanie: as the built-in ports (the prototype's steps), theme and video from the ROM"""
    from handlers.baked import Baked
    from aki_import import looks_fields
    import mkparts as P
    b = Baked()
    rep = []
    descs, pool = G.build_model(fx, rep)
    slots, ab = G.moveset(fx, rep)
    row = b.late_row[b.late_index(int(fx["legacy_game_id"], 16))]
    k = fx["wm2000"]["roster_index"]
    sid = fx["string_id"]
    names = ids_by_number()
    import mkroster as K
    ref_id = K.EV.r16(K.EV.r32(0x80047124 + 16 * fx["moveset"]["reference_ordinal"]))
    w = {"about": "%s from WWF WrestleMania 2000, imported by tools\\handlers\\wm2k_import.py from the user's own ROM: the built-in "
                  "port's model, looks, moveset and photo (the same captures and choices: handlers\\wm2000\\wrestlers\\%s.json), his "
                  "theme and Titantron from the ROM. Edit freely: run the handler again to start over." % (fx["name"], sid),
         "id": "0x%04X" % (ID0 + k), "name": fx["name"], "short_name": fx["short_name"], "nick": None,
         "height": fx["height"], "weight": fx["weight"], "based_on": names.get(ref_id, "0x%04X" % ref_id),
         "music": fx["music_slot"], "video": fx["video"],
         "looks": [looks_fields(b.looks[costume0(fx["key"]) + c]) for c in range(4)],
         "moveset": b.movesets[row].hex(), "model": "model/model.json",
         "source": {"game": G.GAME, "handler": "wm2000 (wm2kall, 2026-11-13; the ported two: handlers' prototype)",
                    "rom_sha1": wm.files.sha1, "roster_index": k, "game_id": "0x%04X" % e["id"], "record_name": e["raw_name"],
                    "fixes": "handlers\\wm2000\\wrestlers\\%s.json" % sid,
                    "model": "data\\wmport\\cap\\k%d_c<0-3> (the captures the built-in port is made of)" % k,
                    "copied_from_the_build": ["looks (the built-in port's 38 fields per costume)", "moveset (the built-in's packed "
                                              "216 bytes: its spare cells are shared by the ported movesets)"],
                    "photo": "his built-in photo, made by hand (handlers\\wm2000\\wrestlers\\%s.photo.bin)" % sid}}
    d = FO.write(out, G.COLLECTION, sid, w, G.photo(fx))
    M.write(d, descs, pool)
    moves_json(d, slots, ab, "his moveset, slot by slot: WrestleMania 2000's moves file mapped to No Mercy's moves (gen/mkroster.py "
                             "port_wm_moveset), as the built-in port's")
    write_theme_video(d, wm, sel, e, w, rep, sid)
    FO.write(out, G.COLLECTION, sid, w, G.photo(fx))
    report.append("%s (WrestleMania 2000's %d, %s): THE BUILT-IN PORT AGAIN -- model 4 costumes (%d files), looks, moveset, photo as "
                  "built in; %s" % (sid, k, e["raw_name"], len(json.load(open(os.path.join(d, "model", "model.json")))["files"]),
                                     rep[-1]))
    report += ["   " + r for r in rep[:-1] if "skin" in r]

WEIGHT_WHY = ("WrestleMania 2000's record weighs him %d, its placeholder for those it gives no weight (No Mercy would read it "
              "as 599 lbs, a super heavyweight): No Mercy's own %d, its %s's code -- the select screen's text for one and the "
              "engine's weight class at %d lbs (handlers\\revenge\\body.py; 2026-11-15)")

def one(out, wm, sel, caps, fx, e, report, moves_log, lib):
    import imports2 as I2
    import aki_import as AI
    import mkroster as K
    k = fx["wm2000"]["roster_index"]; sid = fx["string_id"]
    full, short = G.names(e["raw_name"])
    name = fx.get("name", full)
    rep = []
    cc = [caps.get(k, c) for c in range(4)]
    descs, pool = G.build_model_caps(wm, k, cc, rep, sid)
    scales = [scale_of(ram, slot) for ram, slot in cc]
    size = RB.nm_size_for_scale(scales[0])
    bodies = [G.body_type(e, c) for c in range(4)]
    body, body_why = bodies[0]
    ref = fx["moveset"]["reference_ordinal"]
    mrep = []
    slots, ab = G.moveset_of(wm, e, ref, mrep, sid)
    # (2026-11-13, wm2klib: the slots that play a library move -- wmlib.py, the WrestleMania 2000 handler's library)
    import wmlib
    lib_ids, lib_kept, lib_same, lib_recs = lib.slot_ids(sid, wmlib.how_of(mrep))   # (movesteps: and the record changes)
    # (2026-11-13, movesteps: the packed moveset as the old slot rule had it -- the slots place_own gave No Mercy's very move
    #  hold one the packing can't say there; moves.json has them -- so a folder's packed moveset stays what it was)
    pack_slots = [G.PLACED_WAS.get(sid, {}).get(i, s) for i, s in enumerate(slots)]
    packed, fell, bad = I2.BB.pack_moveset(pack_slots, ab, ref)
    ref_id = K.EV.r16(K.EV.r32(0x80047124 + 16 * ref))
    names = ids_by_number()
    base = AI.base_looks(ref)
    if body == G.NM_WOMAN:
        # (a woman's type needs a woman's parts in the looks: the game builds its own model of them before his folder's
        #  replaces it -- the Revenge handler's valets, body.py: No Mercy's ordinal 51's four costumes, type 15 in all four)
        looks = [[b[0]] + l[1:] for b, l in zip(bodies, [[body] + x[1:] for x in AI.base_looks(RB.NM_WOMAN_LOOKS)])]
        looks_from = "No Mercy's ordinal %d's four costumes (a woman's parts for a woman's type), their photo field his base's" % RB.NM_WOMAN_LOOKS
        for c in range(4): looks[c][-1] = base[c][-1]
    else:
        looks = [I2.plain_looks(bodies[c][0], base[c][-1], c) for c in range(4)]
        looks_from = "a plain costume (imports2.py plain_looks: the ported men's base, every part its plainest, hat colour = costume)"
    photo, ph_how = PH.photo_from_pool(descs[0], pool)    # (romrender 2026-11-18, the user's decision A: rendered from his model)
    w = {"about": "%s from WWF WrestleMania 2000, imported by tools\\handlers\\wm2k_import.py from the user's own ROM: his model as "
                  "WrestleMania 2000 builds it in a match (captured in the rig), his name, size, body type, moveset, theme and "
                  "Titantron from the ROM. Edit freely: run the handler again to start over." % name,
         "id": "0x%04X" % (ID0 + k), "name": name[:20], "short_name": short[:10], "nick": None,
         "height": e["height"], "weight": e["weight"], "based_on": names.get(ref_id, "0x%04X" % ref_id),
         "size": size, "looks": looks, "moveset": packed.hex(), "model": "model/model.json",
         "source": {"game": G.GAME, "handler": "wm2000 (wm2kall, 2026-11-13)", "rom_sha1": wm.files.sha1, "roster_index": k,
                    "game_id": "0x%04X" % e["id"], "record_name": e["raw_name"], "fixes": "handlers\\wm2000\\wrestlers\\%s.json" % sid,
                    "moves_file": "0x%X" % e["moves"], "abilities_file": "0x%X" % e["abilities"],
                    "costume_looks": [lk.hex() for lk in e["looks"]],
                    "body_type": {"nm_type": body, "why": body_why, "per_costume": [b for b, _ in bodies],
                                  "nm_belt": RB.NM_BELT[body], "nm_hand_class": RB.NM_HAND_CLASS[RB.NM_BELT[body]]},
                    "looks_from": looks_from,
                    "size": {"wm2000_scale": [round(s, 5) for s in scales], "nm_size": size, "nm_scale": RB.scale_of_size(size),
                             "nm_height_scale": RB.nm_scale(e["height"]),
                             "why": "WrestleMania 2000's own whole-model scale of him in a match (x = y = z, his slot's), as No "
                                    "Mercy's size code nearest it (0.97 + 0.00047 x size)"},
                    "reference": {"ordinal": ref, "why": fx["moveset"].get("reference_why", "")},
                    "model": "rig\\wm_capture_all.py captures, its Tag Match with the costume forced (%s)" % ", ".join("c%d: %s slot %d" % (c, caps.index["%d:%d" % (k, c)][0], caps.index["%d:%d" % (k, c)][1]) for c in range(4)),
                    "photo": ph_how, "moveset_fallbacks": fell}}
    if "name_why" in fx: w["source"]["name_fix"] = {"record": full, "why": fx["name_why"]}
    # (2026-11-15, womenfix) WrestleMania 2000 weighs its women and managers 600 -- its placeholder (their heights 36 / 37 are
    #  No Mercy's codes too). In No Mercy 600 is cut to 499, 599 lbs: a super heavyweight, every slam on them a struggle.
    #  No Mercy's own codes in its place: a woman's 500, a non-wrestler's 501 (handlers\revenge\body.py NM_WOMAN_WEIGHT)
    if e["weight"] is not None and e["weight"] >= 500 and e["weight"] not in RB.NM_CODE_LBS:
        w["weight"] = RB.NM_WOMAN_WEIGHT if body == G.NM_WOMAN else RB.NM_STAFF_WEIGHT
        w["source"]["weight_from"] = WEIGHT_WHY % (e["weight"], w["weight"], "woman" if body == G.NM_WOMAN else "non-wrestler",
                                                   RB.NM_CODE_LBS[w["weight"]])
    d = FO.write(out, G.COLLECTION, sid, w, photo)
    M.write(d, descs, pool)
    moves_json(d, wmlib.slots_with_ids(slots, lib_ids), ab, wmlib.ABOUT % len(lib_ids), lib_recs)
    write_theme_video(d, wm, sel, e, w, rep, sid)
    # (newhome 2026-11-17: "weight_from" after "theme" and "video", where the installed folders have it -- womenfix added it to
    #  folders already made -- so a re-import writes them byte for byte)
    if "weight_from" in w["source"]: w["source"]["weight_from"] = w["source"].pop("weight_from")
    w["class"], w["class_from"] = G.wrestler_class(e, fx)       # (rostertags, 2026-11-21: what he is -- last)
    FO.write(out, G.COLLECTION, sid, w, photo)
    import collections
    hows = collections.Counter()
    for l in mrep:
        if l.strip().startswith("%s: {" % sid): hows = eval(l.split(": ", 1)[1])
    moves_log += [l for l in mrep if "slot" in l]
    report.append("%s (WrestleMania 2000's %d, %s): model 4 costumes, %d files; body %s; size %d (WM2000 scale %.4f; No Mercy's for "
                  "his height %.4f); moveset %s (%d slots the packing can't say: moves.json has them; %d slots play a WrestleMania "
                  "2000 library move); photo %s; %s"
                  % (sid, k, e["raw_name"], len(json.load(open(os.path.join(d, "model", "model.json")))["files"]), body_why, size,
                     scales[0], RB.nm_scale(e["height"]) or 0, dict(hows), fell, len(lib_ids), "yes" if photo else "NONE", rep[-1]))
    report += ["   " + r for r in rep[:-1] if "skin" in r]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--rom", default=None)
    a = ap.parse_args()
    wm = G.WM2000(a.rom)
    import wmlib
    lib = wmlib.Library(wm)       # (2026-11-13, wm2klib: the library's moves, from all 58 movesets -- whoever --only names)
    R = wm.roster()
    sel = open(os.path.join(CAP, "sel1_ram.bin"), "rb").read()
    fx = G.fixes()
    only = [x for x in a.only.split(",") if x]
    caps = Caps()
    report, moves_log = [], []
    for sid in sorted(fx, key=lambda s: fx[s]["wm2000"]["roster_index"]):
        if only and sid not in only: continue
        f = fx[sid]; e = R[f["wm2000"]["roster_index"]]
        if "legacy_game_id" in f: ported(a.out, wm, sel, f, e, report)
        else: one(a.out, wm, sel, caps, f, e, report, moves_log, lib)
        print(report[-1] if not report[-1].startswith("   ") else "", flush=True)
    # (rostertags, 2026-11-21) WrestleMania 2000's own rivals (handlers\wm2000\rivals.json: wm_rivals.py, from its Rival 1-3) into
    # the collection, as it is -- never over one there (the player's); ri_build.cpp copies it the same way
    rv = os.path.join(a.out, G.COLLECTION, "rivals.json")
    if not only and not os.path.exists(rv):
        open(rv, "wb").write(open(os.path.join(HERE, "handlers", "wm2000", "rivals.json"), "rb").read())
    tag = "_".join(only) if only and len(only) < 4 else "all"
    open(os.path.join(a.out, "import_report_%s.txt" % tag), "w").write("\n".join(report) + "\n")
    open(os.path.join(a.out, "moves_report_%s.txt" % tag), "w").write("\n".join(moves_log) + "\n")

if __name__ == "__main__":
    main()
