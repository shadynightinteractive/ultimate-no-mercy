#!/usr/bin/env python3
"""imports2.py (2026-11-13, imports2): two wrestlers imported from mods as standard wrestler folders.

    python tool\\imports2.py --out <wrestlers folder> [--only vader,goldust]

  vader     from WWF WarZone - Badd Blood (a WrestleMania 2000 mod): handlers\\baddblood\\game.py
  goldust   from No Mercy Plus V3 (a No Mercy mod), the customs case: handlers\\customs\\nmplus.py -- his looks and model are
            No Mercy Plus's (its Create-a-Wrestler Goldust parts: it has no Goldust Superstar); what No Mercy Plus hasn't
            for him (a name on its roster, size, moveset, theme, Titantron) comes from Badd Blood's Goldust, its roster's
            32nd, the same way as Vader's -- each field's "source" says which.
Writes <out>\\Imported\\<id>\\ (wrestler.json, model\\, moves.json, photo.bin, theme.bin + theme.meta, video.bin) and
<out>\\import_report.txt. The ROMs are read in place, never copied. Badd Blood's models, theme bank and video lists come
from the rig's captures (imports2\\rig\\cap: rig\\capture.py, rig\\bb.py); run those first.
"""
import argparse, hashlib, json, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers.aki import model as M, photo as PH, folder as FO, romtable as RT
from handlers.baddblood import game as BB
from handlers.customs import nmplus as NP
from handlers.nomercy import game as NMG

DEV = BB.DEV
CAP = os.path.join(BB.RIG, "cap")
COLLECTION = "Imported"
# (the stock folders nm_import.py writes, read only: NM_STOCK_FOLDERS, else the game's wrestlers\No Mercy -- 2026-11-13,
#  final082: was the nmhandler agent's output folder)
STOCK = os.environ.get("NM_STOCK_FOLDERS") or os.path.join(DEV, "mod", "wrestlers", "No Mercy")

def stock(sid):
    j = json.load(open(os.path.join(STOCK, sid, "wrestler.json")))
    return j, j["source"]["ordinal"]

# A plain costume in this game's looks (gen/mkparts.py JJ_BASE: the ported men's base, every part its plainest), for a
# wrestler whose model is his folder's own: what the game itself still reads of the looks is the body type (his belt and
# his hands' class), the skin, the photo (revbody's notes: handlers\revenge\body.py).
JJ_BASE = bytes.fromhex("02 03 0e 00 00 00 00 00 00 00 00 00 00 00 00 01 02 00 00 00 00 07 00 08 00 1c 00 00 01 09 09 02 09 00 00 00 00 09")
# (The mod tells a folder wrestler's costumes apart by their looks -- mod parts.c folder_costume: the costume whose fields
#  match most -- so four alike are all taken for his first: each gets the hat colour of its own number, no hat being worn,
#  as gen/mkparts.py tells the ported men's costumes apart. Seen in qa run 123424_i2_tag before this: every model costume 1.)
def plain_looks(body, photo, c):
    return NMG.looks_of(NP.set_app(JJ_BASE, body=body, ring=111, ring_c1=0, ring_c2=0, upper=0, upper_c1=0, upper_c2=0,
                                   wrist=0, wrist_c=0, lknee=0, lknee_c=0, rknee=0, rknee_c=0, boots=1, boots_c1=0, boots_c2=0,
                                   face=0, hair=2, front_hair=0, facial_hair=0, mask=0, hat=0, hat_c=c, photo=photo))

def write_theme_video(d, rom, ram, files, e, w, report, who):
    data, meta, about, _ = BB.theme_folder(rom, ram, e["music"])
    open(os.path.join(d, "theme.bin"), "wb").write(data); open(os.path.join(d, "theme.meta"), "wb").write(meta)
    vid, vabout = BB.video(files, ram, e["video"])
    open(os.path.join(d, "video.bin"), "wb").write(vid)
    w["source"]["theme"] = about; w["source"]["video"] = vabout
    report.append("%s: theme = Badd Blood's music bank sound %d (instrument %d, %d bytes of VADPCM at %.0f a second, tuning %s) made "
                  "%d a second: %.1f s, loop from %.1f s; video %d: %d frames written (%d list entries, frames %s)"
                  % (who, about["sound"], about["instrument"], about["bytes"], about["source_rate"], about["tuning"], about["rate"],
                     about["seconds"], about["loop_from_s"], e["video"],
                  vabout["frames_written"], vabout["entries"], " ".join(vabout["frame_files"])))

def moves_json(d, slots, ab, about):
    json.dump({"about": about, "slots": [list(s) for s in slots], "abilities": list(ab)}, open(os.path.join(d, "moves.json"), "w"))

def vader(out, report):
    bb = BB.BaddBlood()
    R = bb.roster(); e = R[19]
    full, short, nick = BB.plain_name(e["raw_name"])
    assert full == "VADER", e["raw_name"]
    caps = [open(os.path.join(CAP, "vader_c%d_ram.bin" % c), "rb").read() for c in range(4)]
    rep = []
    descs, pool = BB.build_model(bb, 19, caps, rep, "vader")
    base, ref = stock("viscera")             # (No Mercy's biggest man: the stance and weight class his moves are made with)
    slots, ab = BB.moveset(bb, e, ref, rep, "vader")
    packed, fell, bad = BB.pack_moveset(slots, ab, ref)
    sel = open(os.path.join(CAP, "vsel_ram.bin"), "rb").read()
    photo, photo_how = PH.photo_from_pool(descs[0], pool)     # (romrender 2026-11-18, the user's decision A: rendered from his model)
    # (his body type: Viscera's, Fat 2 -- No Mercy's own type for a man of his 450 lbs. Badd Blood's looks' first byte is 2 for
    #  all four costumes: WrestleMania 2000's hands' kind index (its table 0x80041CC4 in the match), not a body type here)
    body = [base["looks"][c][0] for c in range(4)]
    w = {"about": "Vader from WWF WarZone - Badd Blood (a WrestleMania 2000 mod), imported by tools\\handlers\\"
                  "imports2.py from the user's own ROM: his model as Badd Blood builds it in a match (captured in the rig), his "
                  "name, size, moveset, theme and Titantron from the ROM. Edit freely: run the handler again to start over.",
         # (his game number pinned -- the QA tool names a folder wrestler by number; his string ID is his folder's name)
         "id": "0x0391",
         "name": full[:20], "short_name": short[:10], "nick": nick,
         "height": e["height"], "weight": e["weight"], "based_on": "viscera",
         "looks": [plain_looks(body[c], base["looks"][c][-1], c) for c in range(4)],
         "moveset": packed.hex(), "model": "model/model.json",
         "source": {"game": BB.GAME, "handler": "baddblood (imports2, 2026-11-13)", "rom_sha1": bb.files.sha1,
                    "roster_index": 19, "game_id": "0x%04X" % e["id"], "record_name": e["raw_name"],
                    "moves_file": "0x%X" % e["moves"], "abilities_file": "0x%X" % e["abilities"],
                    "costume_looks": [lk.hex() for lk in e["looks"]],
                    "body_type": "Viscera's (%s): with his own model it picks only his belt and his hands' class" % body,
                    "model": "rig\\capture.py: a match, slot 0, each costume (cap\\vader_c<k>_ram.bin)",
                    "photo": photo_how,
                    "moveset_fallbacks": fell}}
    d = FO.write(out, COLLECTION, "vader", w, photo)
    M.write(d, descs, pool)
    moves_json(d, slots, ab, "his moveset, slot by slot: Badd Blood's moves file mapped to No Mercy's moves (gen/mkroster.py "
                             "port_wm_moveset, by animation, then by name, else a stand-in or Viscera's)")
    write_theme_video(d, bb.files.rom, sel, bb.files, e, w, rep, "vader")
    FO.write(out, COLLECTION, "vader", w, photo)
    report.append("vader: model %d costumes, %d files; photo; moveset %d slots (%d the packing can't say: moves.json has them)"
                  % (len(descs), len(json.load(open(os.path.join(d, "model", "model.json")))["files"]), len(slots), fell))
    report += ["   " + r for r in rep]

def goldust(out, report):
    nm = NP.NMPlus()
    names = [n for n in nm.roster_names() if n]
    assert "GOLDUST" not in names           # (checked each run: No Mercy Plus's roster has no Goldust)
    game = NMG.NoMercy()                    # (the user's No Mercy: which files this game has the same)
    rep = []
    descs, files, order, fields, keeps, base_app, apps = NP.build(nm, game.files, rep)
    # the folder's model, written as aki/model.py does, the files listed in file-number order (a file and the one after
    # it stay next to each other: the game numbers them so)
    d = os.path.join(out, COLLECTION, "goldust"); os.makedirs(os.path.join(d, "model", "files"), exist_ok=True)
    names_ = []
    for v, b in zip(order, files):
        n = "files/%04x_%s.bin" % (v, hashlib.sha1(b).hexdigest()[:12])
        open(os.path.join(d, "model", n), "wb").write(b); names_.append(n)
    cos = []
    for c in range(4):
        open(os.path.join(d, "model", "c%d.bin" % c), "wb").write(descs[c])
        cos.append({"descriptor": "c%d.bin" % c, "keep": keeps[c], "fields": fields[c]})
    json.dump({"format": 1, "about": "his model as No Mercy Plus V3's own builder makes it of his looks (handlers\\customs\\"
                                     "nmplus.py); a file named 'NNNN_...' is No Mercy Plus's file NNNN", "files": names_,
               "costumes": cos}, open(os.path.join(d, "model", "model.json"), "w"), indent=1)
    bb = BB.BaddBlood(); e = bb.roster()[32]
    full, short, nick = BB.plain_name(e["raw_name"]); assert full == "GOLDUST", e["raw_name"]
    tbase, ref = stock("test")
    slots, ab = BB.moveset(bb, e, ref, rep, "goldust")
    packed, fell, bad = BB.pack_moveset(slots, ab, ref)
    sel = open(os.path.join(CAP, "vsel_ram.bin"), "rb").read()
    # (romrender 2026-11-18: no screenshot -- was a crop of a QA run's shot. His model is No Mercy's format: his attire and face
    #  paint are pictures the game stamps onto the body's at load time, which our renderer doesn't do yet, so his photo is his
    #  face paint's picture made by the renderer's integer steps -- a stand-in, as before there was a screenshot)
    # (the face paint's palette is named at 0x230 of his model -- its picture the file after it, as the game reads them)
    p_ = struct.unpack_from(">H", nm.model(NP.un_of(apps[0]), NP.STATES[0]), 0x230)[0]
    photo = PH.photo_from_picture_rr(nm.files(p_ + 1), nm.files(p_))
    ph_how = "his face paint's picture (No Mercy Plus file 0x%04X), a stand-in made by tools\\romrender" % (p_ + 1)
    w = {"about": "Goldust, the customs case: his looks are No Mercy Plus V3's Create-a-Wrestler Goldust parts (its SmackDown "
                  "Mall's 'Goldust 1' and 'Goldust 2': ring attire, boots, face paint) on No Mercy Plus's Test, and his model is "
                  "the one No Mercy Plus's own code builds of them. No Mercy Plus has no Goldust on its roster, so his name, size, "
                  "moveset, theme and Titantron are Badd Blood's Goldust's (a WrestleMania 2000 mod). Imported by tools\\handlers\\"
                  "imports2.py from the user's own ROMs. Edit freely.",   # (2026-11-14, folders082: the tool's place since final082, as Vader's says)
         "id": "0x0390",          # (pinned, as Vader's)
         "name": full[:20], "short_name": short[:10], "nick": nick, "height": e["height"], "weight": e["weight"],
         "based_on": "test",
         "looks": [NMG.looks_of(NP.set_app(NP.tame(a, tbase["looks"][c][-1], nm.remap, rep), hat_c=c)) for c, a in enumerate(apps)],   # (hat colour: plain_looks)
         "moveset": packed.hex(), "model": "model/model.json",
         "source": {"looks_and_model": {"game": "No Mercy Plus V3 (data\\nmplus\\nmplus.z64 = No Mercy v1.0 + its xdelta)",
                                        "handler": "customs\\nmplus (imports2, 2026-11-13)", "rom_sha1": nm.files.sha1,
                                        "base": "its TEST, appearance %s" % base_app.hex(),
                                        "costumes_in_its_numbers": [a.hex() for a in apps],
                                        "parts": {"ring attire": NP.RING, "mask (face paint)": NP.MASK, "boots": NP.BOOTS},
                                        "its_files": ["0x%04X" % v for v in order]},
                    "record_moves_theme_video": {"game": BB.GAME, "rom_sha1": bb.files.sha1, "roster_index": 32,
                                                 "record_name": e["raw_name"], "game_id": "0x%04X" % e["id"],
                                                 "moves_file": "0x%X" % e["moves"], "abilities_file": "0x%X" % e["abilities"],
                                                 "why": "No Mercy Plus V3 has no Goldust Superstar: no record, moves, theme or video"},
                    "photo": ph_how, "moveset_fallbacks": fell}}
    FO.write(out, COLLECTION, "goldust", w, photo)
    moves_json(d, slots, ab, "his moveset, slot by slot: Badd Blood's Goldust's moves file mapped to No Mercy's moves "
                             "(gen/mkroster.py port_wm_moveset)")
    write_theme_video(d, bb.files.rom, sel, bb.files, e, w, rep, "goldust")
    FO.write(out, COLLECTION, "goldust", w, photo)
    report.append("goldust: model %d costumes, %d No Mercy Plus files; photo: %s; moveset %d slots (%d the packing can't say)"
                  % (len(descs), len(order), ph_how, len(slots), fell))
    report += ["   " + r for r in rep]
    report += ["   " + r for r in nm.remap.report()]     # (modcheck: every stock claim the handler made, checked)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="vader,goldust")
    a = ap.parse_args()
    report = []
    for who in a.only.split(","):
        {"vader": vader, "goldust": goldust}[who](a.out, report)
    open(os.path.join(a.out, "import_report_%s.txt" % "_".join(a.only.split(","))), "w").write("\n".join(report) + "\n")
    print("\n".join(report))

if __name__ == "__main__":
    main()
