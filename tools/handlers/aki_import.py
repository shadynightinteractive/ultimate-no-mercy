#!/usr/bin/env python3
"""aki_import.py (handlers, 2026-11-12, PROTOTYPE): pull wrestlers out of another AKI game into standard wrestler folders.

    python aki_import.py revenge --out <wrestlers folder> [--only id,id,...] [--all] [--legacy-ids 0x0340]

  revenge        the handler (today only revenge; wm2000 and customs are designed, not written: see the report)
  --out          where the folders go: <out>\\Revenge\\<string id>\\ (point WCW_WRESTLERS_DIR at <out> to try them)
  --only         just these (string IDs, as in handlers\\revenge\\wrestlers\\*.json, or slugs of Revenge names)
  --all          every Superstar on Revenge's roster: the ported ones whole, the rest with what the ROM gives without a
                 capture (name, size, moveset) and a note that their model needs one
  --legacy-ids   write the live game's number IDs from this one up (today's exe reads "id": "0x0340"; rosterids' string
                 IDs aren't merged yet). The ported ones then get "based_on" as a number too.
  --library      (2026-11-13, movelib2) the Revenge move library's index (revlib.py's rows_all.json; default: the handler's
                 own, beside this file in work\revenge_library): each slot that plays one of his own Revenge moves gets
                 its library ID in moves.json, "revenge:<attacker file>_<defender file>@<the No Mercy move it's played on>"
                 (movelib's NOTE_for_handlers1) -- the ported men and the rest alike. Without one, No Mercy's numbers only
                 (stand-ins with No Mercy's animations), as before.
  --moves-only   (2026-11-13, movelib2) only moves.json and wrestler.json's "about" of folders already in --out (the models
                 aren't made again)
  --ports-universal (2026-11-14, revfix -- the user: the 24 built-in ports are made like the others, from the ROM by the same
                 logic, not from the old built-in data) the ported men as folders the way --all makes the never-ported ones:
                 Revenge's record, his captured costumes, his body type and size by the rule, the universal moveset (the
                 reference by weight, his own moves where it was left, his slam without the struggle), the Costume Change
                 photo, his theme in the folder; of his fixes (wrestlers\\<id>.json) only what is Revenge's -- PORT_KEEP below;
                 "id" his frozen string ID and "replaces" it (for the 0.84 game: the folder takes the built-in's place and
                 number); his variants exactly Revenge's own four (the user, 2026-11-15: the second looks,
                 wrestlers\\*_b.json, are not made)
  --refresh      (2026-11-14, revfix) the never-ported men's folders already in --out brought up to this handler: the photo from
                 Revenge's own Costume Change (handlers\\revenge\\fixes2.py), Revenge's theme in the folder (theme.py), his own
                 Revenge moves where the reference's were left (fixes2.own_slots), and the records skip.json names (Revenge
                 draws another man for them) listed to be taken out -- everything else, his ID first, as it is
Writes <out>\\import_report.txt: per wrestler what was made and how.
"""
import argparse, json, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers.aki import model as M, photo as PH, folder as FO
from handlers.revenge import game as G
from handlers.baked import Baked
from handlers.revenge import fixes2 as F2, theme as TH      # (2026-11-14, revfix)
DEV = G.DEV

def ids_by_number():
    """game number -> string ID: the frozen built-in IDs, mod\\gen\\roster_ids.json (2026-11-13, final082: was a copy of
    its lines in the tool's work folder)"""
    j = json.load(open(os.path.join(DEV, "mod", "gen", "roster_ids.json"), encoding="utf-8"))
    return {v: k for k, v in j.items()}

def ord_id(o):
    """the game's Superstar of ordinal o: his game number"""
    import mkroster as K
    return K.EV.r16(K.EV.r32(0x80047124 + 16 * o))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game", choices=["revenge"])
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--legacy-ids", default="")
    ap.add_argument("--rest-only", action="store_true", help="with --all: only the ones not ported (the ported folders made before stay)")
    ap.add_argument("--library", default=os.path.join(HERE, "work", "revenge_library", "rows_all.json"))
    ap.add_argument("--moves-only", action="store_true")
    ap.add_argument("--refresh", action="store_true")
    ap.add_argument("--ports-universal", action="store_true")
    ap.add_argument("--records", default="", help="(movelib2) record changes from Revenge's move data: tool\\recdiff.py --damage's json")
    a = ap.parse_args()
    lib = load_library(a.library)
    global REC_FROM_ROM
    REC_FROM_ROM = json.load(open(a.records)) if a.records else {}
    if a.moves_only: return moves_only(a, lib)
    if a.refresh: return refresh(a)
    if a.ports_universal: return ports_universal(a, lib)
    fx = G.fixes()
    names = ids_by_number()
    baked = Baked()
    only = [x for x in a.only.split(",") if x]
    report = []
    legacy = int(a.legacy_ids, 16) if a.legacy_ids else 0
    # (newhome 2026-11-17: the number IDs the installed game has, frozen as data -- handlers\revenge\legacy_ids.json -- so a
    #  re-import gives every man the same number whatever the run's order and skip list were; a man not in the file takes the
    #  next free number above all of them, as before)
    frozen = load_frozen_ids()
    taken = {int(v, 16) for v in frozen.values()}
    def next_legacy(sid=None):
        nonlocal legacy
        if sid in frozen: return frozen[sid]
        while legacy in taken: legacy += 1
        v = legacy; legacy += 1; return "0x%04X" % v
    # the ported ones, in the order the build makes them (a second look after his base: his skin tone is the base's)
    todo = [s for s in fx if (not only or s in only) and not a.rest_only]
    todo.sort(key=lambda s: ("second_look_of" in fx[s], list(fx).index(s)))
    if any("second_look_of" in fx[s] for s in todo):           # (the base's skin first, even if not asked for)
        for s in todo:
            b = fx[s].get("second_look_of")
            if b and b not in todo: G.build_model(fx[b], [])
    made = []
    by_code = {e["code"]: e for e in G.roster()}      # (2026-11-13, revbody: his Revenge record, for his body)
    for sid in todo:
        f = fx[sid]
        rep = []
        base_fx = fx.get(f.get("second_look_of"))
        descs, pool = G.build_model(f, rep, base_fx)
        photo, _ = PH.photo_from_pool(descs[0], pool)       # (romrender 2026-11-18: rendered from his model)
        slots, ab = G.moveset(f, rep)
        k = baked.late_index(int(f["legacy_game_id"], 16))
        row = baked.late_row[k]
        ref_id = ord_id(f["moveset"]["reference_ordinal"])
        # (2026-11-13, revbody) his looks are the built-in port's (below); their body type must be the one the handler gives
        # him from Revenge's body (handlers/revenge/body.py), or the hand choice his fixes keep, with its reason
        e = by_code[int(f["revenge"]["code"], 16)]
        body, body_why = body_type_of(e, f)
        looks = [looks_fields(baked.looks[G_costume0(f) + c]) for c in range(4)]
        if any(l[0] != body for l in looks):
            raise SystemExit("%s: the built-in port's body type %s isn't the handler's %d (%s)" % (sid, [l[0] for l in looks], body, body_why))
        w = {"about": "%s from WCW/nWo Revenge, imported by handlers\\aki_import.py (the revenge handler, prototype) from "
                      "the user's own ROM. Edit freely: run the handler again to start over." % f["name"],
             "id": sid, "name": f["name"], "short_name": f["short_name"],
             "nick": None,          # (2026-11-13, handlers1: none -- Revenge has no nicknames, and the built-in port has none)
             "height": f["height"], "weight": f["weight"],
             "based_on": names.get(ref_id, "0x%04X" % ref_id),
             "music": f["music_slot"], "video": f["video"],
             # (2026-11-13, handlers1: his costumes' looks as numbers, the 38 fields -- no packed key into the build's ext_looks)
             "looks": looks,
             "size": body_size(e),       # (2026-11-13, revbody: his model at Revenge's own scale, not his height's)
             "moveset": baked.movesets[row].hex(),
             "model": "model/model.json",
             "source": {"game": "WCW/nWo Revenge", "handler": "revenge (prototype 2026-11-12)",
                        "moveset_file": f["revenge"]["moveset_file"],
                        "costumes": f["costumes"], "fixes": "handlers\\revenge\\wrestlers\\%s.json" % sid,
                        "body": body_source(e, body, body_why),
                        "moveset_cells": [list(c) for c in baked.cells[row]],
                        "copied_from_the_build": ["looks (the 38 fields as numbers, as gen/mkparts.py makes them for the "
                                                  "ported men: Jeff Jarrett's base with his body and photo; the game builds the "
                                                  "skeleton from them, then the model replaces every part)", "moveset (packed "
                                                  "with the build's spare cells, which are shared by all ported movesets)"]}}
        if legacy:
            w["id"] = next_legacy(sid); w["string_id"] = sid
            w["based_on"] = "0x%04X" % ref_id
        d = FO.write(a.out, "Revenge", sid, w, photo)
        M.write(d, descs, pool)
        json.dump(moves_json(sid, slots, ab, lib, f), open(os.path.join(d, "moves.json"), "w"))
        # (2026-11-14, revfix: his theme in his folder -- the music file the project gave him (mod\music, by his name; a
        #  second look's is his base's), else -- 2026-11-18, reduxthemes -- Revenge Redux's theme for him when the player has
        #  the Redux patch, else Revenge's own entrance song, as the built-in port plays it: handlers\revenge\theme.py write_own)
        w["source"]["theme"] = TH.write_own(d, fx[f["second_look_of"]]["name"] if f.get("second_look_of") else f["name"],
                                             e["code"])   # (his model code: his Redux song)
        FO.write(a.out, "Revenge", sid, w)
        made.append(sid)
        report.append("%s: model %d costumes, %d files; photo; moveset %d slots%s" % (sid, len(descs), len(json.load(open(os.path.join(d, "model", "model.json")))["files"]),
                      len(slots), "" if not rep else " | " + " | ".join(r.strip() for r in rep if "skin" in r)))
    if a.all:      # the rest of Revenge's roster: no capture yet, so no model of their own
        ported_codes = {int(x["revenge"]["code"], 16) for x in fx.values()}     # (the ported ones are made above, whole)
        # (2026-11-12: his captures, if rig/capture_all.py has made them -- {(code, costume): his model's parts as Revenge built it})
        import pickle
        store_p = os.environ.get("AKI_CAPTURES", os.path.join(__import__("handlers").RIG, "cap", "auto.pkl"))   # (the rig folder's: rig\capture_all.py writes it)
        store = pickle.load(open(store_p, "rb")) if os.path.exists(store_p) else {}
        for e in G.roster():
            sid = G.slug(e["name"])
            if e["code"] in ported_codes or sid in fx or sid in made: continue
            if only and sid not in only: continue
            why = F2.skipped(e["code"])                 # (2026-11-14, revfix: a record Revenge draws another man for)
            if why: report.append("%s: code 0x%03X -- no folder: %s" % (sid, e["code"], why)); continue
            if not e["moveset_file"]: report.append("%s: no moveset file known -- skipped" % sid); continue
            ref = 4 if (e["weight"] or 0) >= 200 else 1          # (a default reference: Kane for the big men, else The Rock)
            body, body_why = body_type_of(e)                       # (2026-11-13, revbody: his own body type, body.py)
            from handlers.revenge import body as B
            # (2026-11-15, womenfix) a woman's reference is No Mercy's women's base, Stephanie McMahon (ordinal 51: her looks
            #  are already the women's parts here) -- as the WrestleMania 2000 handler's women -- not The Rock: the slots
            #  Revenge has no move of hers for were The Rock's (his punches, his Rock Bottom), and her base his
            if body == B.NM_WOMAN: ref = B.NM_WOMAN_LOOKS
            rep = []
            f = {"key": sid, "moveset": {"reference_ordinal": ref, "allies": []}, "revenge": {"moveset_file": "0x%X" % e["moveset_file"]}}
            try:
                slots, ab = G.moveset(f, rep)
                packed, fell, bad = G.pack_plain(slots, ab, ref)
            except Exception as ex:
                report.append("%s: moveset failed (%s) -- written without one" % (sid, ex)); packed, fell, bad, slots = None, 0, [], []
            ref_id = ord_id(ref)
            gots = [store.get((e["code"], c)) for c in range(4)]
            descs = pool = photo = None
            if all(gots):          # (his model, Revenge's four costumes; a photo from his face picture until a screenshot crop)
                try:
                    # (2026-11-15, womenfix: a woman with No Mercy's women's hands and skin pictures -- skintone.WOMAN_KIT)
                    descs, pool = G.build_model({"key": sid, "costumes": [{"got": g} for g in gots]}, rep, woman=body == B.NM_WOMAN)
                    # (romrender 2026-11-18, the user's decision A: rendered from his model by our renderer -- was a crop of
                    #  Revenge's Costume Change screen, an emulator screenshot)
                    photo, photo_how = PH.photo_from_pool(descs[0], pool)
                except Exception as ex:
                    report.append("%s: model failed (%s)" % (sid, ex)); descs = None
            w = {"about": "%s from WCW/nWo Revenge, imported by handlers\\aki_import.py (prototype). NO MODEL OF HIS OWN YET: "
                          "Revenge builds his model in a match, so the handler needs a capture of him (captures.json's recipe, "
                          "model code 0x%03X); until then he looks like the man he's based on." % (e["name"], e["code"]),
                 "id": sid, "name": e["name"].upper()[:20], "short_name": e["short"].upper()[:10], "nick": None,
                 "height": e["height"], "weight": e["weight"], "based_on": names.get(ref_id, "0x%04X" % ref_id),
                 "source": {"game": "WCW/nWo Revenge", "handler": "revenge (prototype 2026-11-12)", "code": "0x%03X" % e["code"],
                            "moveset_file": "0x%X" % e["moveset_file"], "costumes": e["costumes"], "model": "needs a capture",
                            "body": body_source(e, body, body_why)}}
            # (2026-11-13, revbody) his looks as numbers: his base's four costumes as the game has them -- the rest of the
            # game reads those fields as it did before -- with his own body type in place of the base's
            # (a valet: a No Mercy woman's costumes for the parts -- a woman's type with a man's parts crashes the game: body.py)
            w["looks"] = [[body] + l[1:] for l in base_looks(B.NM_WOMAN_LOOKS if body == B.NM_WOMAN else ref)]
            w["size"] = body_size(e)       # (2026-11-13, revbody: his model at Revenge's own scale, not his height's)
            if body == B.NM_WOMAN: w["source"]["looks_from"] = "No Mercy's ordinal %d (a woman: type 15)" % B.NM_WOMAN_LOOKS
            # (2026-11-13, revbody) managers and valets have no height on Revenge's records (0'0"): the height whose No Mercy
            # size is Revenge's own scale for him, so he isn't his base's size. The weight stays the base's: Revenge has none.
            # (2026-11-15, womenfix: his size is "size" now, so the height is only what the select screen says and what the
            #  engine reads -- a woman gets No Mercy's women's codes, a man with no weight No Mercy's non-wrestlers': body.py
            #  NM_WOMAN_HEIGHT ... The valets showed 5'0" and The Rock's 275 lbs; the managers The Rock's weight)
            if body == B.NM_WOMAN:
                w["height"], w["weight"] = B.NM_WOMAN_HEIGHT, B.NM_WOMAN_WEIGHT
                w["source"]["height_from"] = HW_WOMAN_WHY % (e["body"]["scale"], w["height"], w["weight"])
            elif e["weight"] is None:
                w["height"], w["weight"] = B.NM_STAFF_HEIGHT, B.NM_STAFF_WEIGHT
                w["source"]["height_from"] = HW_STAFF_WHY % (e["body"]["scale"], w["height"], w["weight"])
            if w["height"] is None:
                w["height"] = body_height_for(e)
                w["source"]["height_from"] = ("Revenge's scale for him (%.2f: his record has no height) -- No Mercy's size for "
                                              "this height is the nearest to it" % e["body"]["scale"])
            for k in ("height", "weight"):              # (managers and the hidden ones have none on Revenge's records: his base's)
                if w[k] is None: del w[k]
            if packed: w["moveset"] = packed.hex()
            if legacy:
                w["id"] = next_legacy(sid); w["string_id"] = sid; w["based_on"] = "0x%04X" % ref_id
            if descs:
                w["about"] = ABOUT_REST % (e["name"], e["code"])
                w["source"]["photo"] = photo_how
                w["model"] = "model/model.json"; w["source"]["model"] = "rig/capture_all.py captures"
            d = FO.write(a.out, "Revenge", sid, w, photo if descs else None)
            if descs: M.write(d, descs, pool)
            if slots:
                mj = moves_json(sid, slots, ab, lib, f)
                mj["slots"], own, left = F2.own_slots(mj["slots"], e["moveset_file"], ref)     # (2026-11-14, revfix)
                mj["slots"], slams = F2.no_struggle(mj["slots"], e["moveset_file"])
                mj["about"] += OWN_ABOUT % (len(own), len(left)) + (SLAM_ABOUT % len(slams) if slams else "")
                mj["slots"], wp = F2.win_pose(mj["slots"])                    # (2026-11-15, revfix)
                if wp: w["source"]["win_pose"] = WIN_WHY % wp
                lbs = e["weight"] + 100 if e["weight"] is not None else None
                ab2 = F2.band_abilities(lbs, body == B.NM_WOMAN, ab[31:35])         # (2026-11-15, revfix)
                if ab2 and w.get("moveset"):
                    w["moveset"], old = F2.repack_abilities(w["moveset"], ab2); mj["abilities"] = ab2
                    w["source"]["abilities"] = ABIL_WHY % F2.band_of(lbs, body == B.NM_WOMAN)
                json.dump(mj, open(os.path.join(d, "moves.json"), "w"))
            # (2026-11-14, revfix: the theme Revenge plays for him, in his folder -- 2026-11-18, reduxthemes: Revenge Redux's song
            #  for his model code when the player has the Redux patch; a music file of his name, the player's, first)
            w["source"]["theme"] = TH.write_own(d, e["name"], e["code"])
            w["music"] = REV_MUSIC_SLOT; w["source"]["music"] = MUSIC_WHY    # (2026-11-15, revfix)
            # (newhome 2026-11-17: a woman's or a non-wrestler's No Mercy weight code -- womenfix's, given to folders already
            #  made -- last, where the installed folders have it, so a re-import writes them byte for byte)
            if e["weight"] is None and "weight" in w: w["weight"] = w.pop("weight")
            w["class"], w["class_from"] = G.wrestler_class(e)       # (rostertags, 2026-11-21: what he is -- last)
            FO.write(a.out, "Revenge", sid, w)
            report.append("%s: code 0x%03X, moveset 0x%X (%d slots took the reference's: the game's packing can't hold his%s)"
                          % (sid, e["code"], e["moveset_file"], fell, "; UNPACK DIFFERS %s" % bad[:5] if bad else "") + ("" if not descs else " -- NOW WITH HIS OWN MODEL (captures)"))
            made.append(sid)
        # (rostertags, 2026-11-21) Revenge's rivals (handlers\revenge\rivals.json, its men's real feuds) into the collection, as
        # it is -- never over one there (the player's); ri_build.cpp copies it the same way
        rv = os.path.join(a.out, "Revenge", "rivals.json")
        if made and not os.path.exists(rv):
            open(rv, "wb").write(open(os.path.join(HERE, "handlers", "revenge", "rivals.json"), "rb").read())
    open(os.path.join(a.out, "import_report.txt"), "w").write("\n".join(report) + "\n")
    print("\n".join(report))
    print("%d folders in %s" % (len(made), a.out))

# ---- (2026-11-15, womenfix) the height and weight of those Revenge gives none -------------------------------------------
HW_WOMAN_WHY = ("Revenge's record has no height or weight for her (0'0\", 0 lbs; her scale %.2f is her \"size\"): No Mercy's "
                "own women's codes, height %d and weight %d -- the select screen's text for a woman and the engine's woman's "
                "weight class (light), as Stephanie, Lita and Trish have (handlers\\revenge\\body.py)")
HW_STAFF_WHY = ("Revenge's record has no weight for him, a manager (his scale %.2f is his \"size\"): No Mercy's own "
                "non-wrestlers' codes, height %d and weight %d -- the select screen's text for one and the engine's weight class, "
                "as Mr. McMahon and Shane McMahon have (handlers\\revenge\\body.py)")

# ---- (2026-11-14, revfix) what a never-ported man's folder says, and --refresh ------------------------------------------
ABOUT_REST = ("%s from WCW/nWo Revenge, imported by handlers\\aki_import.py (prototype): his model as Revenge builds it "
              "(captured from the user's ROM in the rig, model code 0x%03X), his name, size and moveset from the ROM, his "
              "photo from Revenge's own Costume Change screen, his entrance theme the one Revenge plays (theme.bin: Revenge "
              "has none per wrestler -- put a theme of your own in its place). Edit freely.")
OWN_ABOUT = ("; 2026-11-14: %d slots that had kept the reference's move (The Rock's / Kane's) have his own Revenge move of the "
             "slot's kind instead, the No Mercy move whose animation is his byte for byte (handlers\\revenge\\fixes2.py); %d "
             "kept it: no move of his is of the slot's kind")
SLAM_ABOUT = ("; %d strong grapple slots that had No Mercy's 960, the slam a man struggles through, play his own Revenge slam "
              "on the Back Body Flip (16250), a slam that ends -- his Revenge pair from the library (fixes2.no_struggle)")
# (2026-11-14, revfix) What of a port's hand fixes stays when he is made the universal way: only what is Revenge's own.
#   kept:  "extra" / "records" (Hollywood Hogan's leg drop: his Revenge drop, his finisher -- the user's), slot 157 of Jim
#          Neidhart (Revenge's own Boston Crab, No Mercy's 11750 byte for byte), "allies" (his Revenge faction), "stance"
#          (Nash's big-man stand, Luger's), the skin choice (the project's, 0.35.12); "weight_class" is No Mercy's rule's
#          now (2026-11-15: fixes2.nm_weight_class -- the mechanics are No Mercy's), The Giant's Andre class kept
#   gone:  every other slot override -- the reference's stand-ins (188 Big Boot / Punch 01, Hogan's 94 / 213 / 214), the Back
#          Body Flip for Revenge's slam (now fixes2.no_struggle for everyone: his own slam on it), the winning taunts 235
#          (his own standing taunt for Revenge's corner pose: a problem every man with a corner win pose has, not fixed yet
#          for anyone -- see the report); "reference_ordinal" (the universal one, by weight); "body_type" hand choices (the
#          rule's); the looks and packed moveset copied from the build
PORT_KEEP_SLOTS = {"jim_neidhart": [157]}
def ports_universal(a, lib):
    """--ports-universal: the 24 ported men as standard folders, the never-ported men's way (see the usage above)"""
    from handlers.revenge import body as B
    fx = G.fixes()
    only = [x for x in a.only.split(",") if x]
    by_code = {e["code"]: e for e in G.roster()}
    ids = json.load(open(os.path.join(DEV, "mod", "gen", "roster_ids.json"), encoding="utf-8"))
    # (2026-11-15, the user: the ports get exactly Revenge's own variants -- the six second looks, "second_look_of", looks
    #  Revenge doesn't give the man, are not made; their IDs stay frozen and the 0.84 game hides them: exe DROPPED_LOOKS)
    todo = [x for x in fx if (not only or x in only) and "second_look_of" not in fx[x]]
    report = []
    for sid in todo:
        f = fx[sid]; rep = []
        e = by_code[int(f["revenge"]["code"], 16)]
        base_fx = fx.get(f.get("second_look_of"))
        # (2026-11-15, skinqa: twin=False -- he replaces the built-in, so not its twin: the skin step every man gets,
        #  handlers/aki/skintone.py)
        descs, pool = G.build_model(f, rep, base_fx, twin=False)
        photo, photo_how = PH.photo_from_pool(descs[0], pool)      # (romrender 2026-11-18: rendered from his model)
        ref = 4 if (e["weight"] or 0) >= 200 else 1
        mv = dict(f["moveset"])
        keep = {k: v for k, v in mv.get("slot_overrides", {}).items() if int(k) in PORT_KEEP_SLOTS.get(sid, [])}
        uf = {"key": f["key"], "moveset": {"reference_ordinal": ref, "allies": mv.get("allies", []), "weight_class": None,
                                           "stance": mv.get("stance"), "slot_overrides": keep, "extra": mv.get("extra"), "records": mv.get("records")},
              "revenge": f["revenge"]}
        slots, ab = G.moveset(uf, rep)
        # (2026-11-15, revfix: No Mercy's weight class for his Revenge weight, as for every Revenge man -- fixes2.nm_weight_class;
        #  The Giant keeps Andre the Giant's (2, 0, 0), the class No Mercy gives its one man of his size: his port's)
        lbs = e["weight"] + 100 if e["weight"] is not None else None
        nab = F2.band_abilities(lbs, False, ab[31:35])                    # (2026-11-15: No Mercy's neutral block for his band)
        if nab:
            if mv.get("stance") is not None: nab[1] = mv["stance"]      # (his port's stance: Nash's big-man stand, Luger's)
            if sid == "the_giant": nab[2:5] = [2, 0, 0]
            ab = nab
        cls = ab[2:5]
        packed, fell, bad = G.pack_plain(slots, ab, ref)
        mj = moves_json(sid, slots, ab, lib, uf)
        mj["slots"], own, left = F2.own_slots(mj["slots"], e["moveset_file"], ref)
        mj["slots"], slams = F2.no_struggle(mj["slots"], e["moveset_file"])
        mj["slots"], wp = F2.win_pose(mj["slots"])
        mj["about"] += OWN_ABOUT % (len(own), len(left)) + (SLAM_ABOUT % len(slams) if slams else "")
        body, body_why = B.nm_body_type(e, e["body"])
        base_sid = f.get("second_look_of")
        number = int(f["legacy_game_id"], 16)
        w = {"about": ("%s from WCW/nWo Revenge, made by handlers\\aki_import.py --ports-universal from the user's own ROM the way "
                       "every Revenge man is (2026-11-14): his model as Revenge builds it, his record, body, size and moveset from "
                       "the ROM, his photo from Revenge's Costume Change, his theme in the folder. He takes the place of the built-in "
                       "port %s (number 0x%04X): \"replaces\" (the 0.84 game). Edit freely." % (f["name"], sid, number)),
             "id": sid, "replaces": sid, "name": f["name"], "short_name": f["short_name"], "nick": None,
             "height": e["height"] if e["height"] is not None else f["height"], "weight": e["weight"] if e["weight"] is not None else f["weight"],
             "based_on": ids_by_number().get(ord_id(ref), "0x%04X" % ord_id(ref)),
             "looks": [[body] + l[1:] for l in base_looks(B.NM_WOMAN_LOOKS if body == B.NM_WOMAN else ref)],
             "size": body_size(e), "moveset": packed.hex(), "model": "model/model.json",
             "music": f["music_slot"],     # (his built-in's music slot: its entrance effects, the ports' plain lights; theme.bin plays over it)
             "source": {"game": "WCW/nWo Revenge", "handler": "revenge, --ports-universal (2026-11-14)", "code": "0x%03X" % e["code"],
                        "moveset_file": f["revenge"]["moveset_file"], "costumes": f["costumes"], "number": "0x%04X" % number,
                        "body": body_source(e, body, body_why), "photo": photo_how,
                        "fixes_kept": sorted(k for k in ("extra", "records", "allies", "stance") if mv.get(k)) + ["slot %s" % k for k in keep],
                        "abilities": ("No Mercy's neutral abilities for his band (fixes2.band_abilities; weight class %s)%s, his allies his "
                                      "own" % (cls, ", his port's stance" if mv.get("stance") is not None else "")),
                        "win_pose": (WIN_WHY % wp) if wp else None,
                        "moveset_fallbacks": fell}}
        if w["source"]["win_pose"] is None: del w["source"]["win_pose"]
        if ids.get(sid) != number: raise SystemExit("%s: roster_ids.json has 0x%04X, his port 0x%04X" % (sid, ids.get(sid, 0), number))
        coll = os.path.join("Revenge", base_sid) if base_sid else "Revenge"     # (a second look: nested in his base's folder)
        d = FO.write(a.out, coll, sid, w, photo)
        M.write(d, descs, pool)
        json.dump(mj, open(os.path.join(d, "moves.json"), "w"))
        w["source"]["theme"] = TH.write_own(d, base_fx["name"] if base_fx else f["name"])   # (music file > Redux > Revenge's 0x214)
        FO.write(a.out, coll, sid, w)
        report.append("%s (0x%04X): model %d costumes; photo %s; %d own moves, %d slams, %d slots kept the reference's; theme: %s"
                      % (sid, number, len(descs), "CC" if "Costume Change screen" in photo_how else "port's", len(own), len(slams), len(left), w["source"]["theme"]))
        print(report[-1], flush=True)
    open(os.path.join(a.out, "ports_report.txt"), "w").write("\n".join(report) + "\n")

# (2026-11-15, revfix) A folder theme plays his song, but his entrance's EFFECTS (lights, pyro) are his music slot's -- with no
# "music" of his own, his base's: The Rock's (or Kane's) entrance. Revenge's men get the slot the 24 ports' Jim Neidhart has,
# 64: Revenge's song 0x214 (the same his theme.bin is) with No Mercy Plus's plain lights, the ports' entrance (gen/mkcontent.py
# EXT_SLOTS "neidhart"). His theme.bin still plays over it (a folder's theme wins).
REV_MUSIC_SLOT = 64
MUSIC_WHY = ("music slot 64, Jim Neidhart's: Revenge's song 0x214 and the 24 ports' plain entrance lights -- not his base's "
             "(The Rock's / Kane's) entrance; his theme.bin plays over it")
ABIL_WHY = ("abilities No Mercy's neutral ones for the %s band (handlers\\revenge\\nm_band_abilities.json: the value most of No Mercy's "
            "own men of that band have, the weight class with them), his allies his own -- not his reference's (The Rock's / "
            "Kane's) block; Revenge's own ability file isn't mapped yet (2026-11-15)")
WIN_WHY = ("winning taunt (slot 235): %s was a turnbuckle taunt (No Mercy has it only in slot 232) -- played where the winner stands "
           "he climbed an invisible corner; now his own standing taunt, %s (his slot 228's) (2026-11-15)")
CLASS_WHY = ("abilities 2-4 %s: No Mercy's weight class for his Revenge weight (handlers\\revenge\\fixes2.py nm_weight_class: No "
             "Mercy's own men of that weight have it), not his reference's %s -- the engine's lifting, struggles and resisting "
             "are No Mercy's for everyone (the user's rule, 2026-11-15)")
def refresh(a):
    """--refresh: each never-ported man's folder under <out>\\Revenge brought up to the handler (see the usage above)"""
    root = os.path.join(a.out, "Revenge")
    by_sid = {G.slug(e["name"]): e for e in G.roster()}
    report, take_out = [], []
    for sid in sorted(os.listdir(root)):
        d = os.path.join(root, sid)
        wp, mp = os.path.join(d, "wrestler.json"), os.path.join(d, "moves.json")
        if not os.path.exists(wp): continue
        w = json.load(open(wp, encoding="utf-8"))
        if w.get("builtin") or sid not in by_sid or w.get("source", {}).get("fixes"): continue   # (the ports: their own fixes)
        e = by_sid[sid]
        why = F2.skipped(e["code"])
        if why: take_out.append(sid); report.append("%s: TO TAKE OUT -- %s" % (sid, why)); continue
        line = [sid]
        ph, how = F2.photo(d)                       # (romrender 2026-11-18: rendered from the folder's model)
        if ph is not None:
            open(os.path.join(d, "photo.bin"), "wb").write(ph); w.setdefault("source", {})["photo"] = how; line.append("photo")
        else: line.append("photo KEPT (%s)" % how)
        if os.path.exists(mp):
            m = json.load(open(mp))
            ref = 4 if w.get("based_on") in ("0x0004", "kane") else 1
            if w.get("based_on") in ("0x0702", "stephanie_mcmahon"): ref = 51     # (2026-11-15, womenfix: a woman's reference)
            m["slots"], own, left = F2.own_slots(m["slots"], e["moveset_file"], ref)
            m["slots"], slams = F2.no_struggle(m["slots"], e["moveset_file"])
            if own or slams or "; 2026-11-14:" not in m["about"]:   # (a second pass changes nothing: its words stay the first's)
                m["about"] = m["about"].split("; 2026-11-14:")[0] + OWN_ABOUT % (len(own), len(left)) + (SLAM_ABOUT % len(slams) if slams else "")
            if slams: line.append("%d struggle slams made his Revenge slam (%s)" % (len(slams), " ".join("%d:%s->%s" % x for x in slams)))
            # (2026-11-15, revfix: his winning taunt standing, not a turnbuckle one -- fixes2.win_pose)
            m["slots"], wpose = F2.win_pose(m["slots"])
            if wpose: line.append("win pose %s -> %s" % wpose); w.setdefault("source", {})["win_pose"] = WIN_WHY % wpose
            # (2026-11-15, revfix: his abilities No Mercy's neutral ones for his band, his weight class with them, his allies
            #  his own -- fixes2.band_abilities; was his reference's whole block)
            woman = w.get("source", {}).get("body", {}).get("nm_body_type") == 15
            from handlers.revenge import body as _B     # (2026-11-15, womenfix: the engine's pounds -- engine_lbs)
            if w.get("moveset"):
                import mkroster as _K
                _b = bytes.fromhex(w["moveset"]); _old = list(_K.unpack(_K.EV, _K.VAN_UNPACK, _b[:0xB8], _b[0xB8:])[1])
                ab = F2.band_abilities(_B.engine_lbs(w.get("weight")), woman, _old[31:35])
                if ab and ab != _old:
                    w["moveset"], old = F2.repack_abilities(w["moveset"], ab)
                    if m.get("abilities"): m["abilities"] = ab
                    w.setdefault("source", {})["abilities"] = ABIL_WHY % (F2.band_of(_B.engine_lbs(w.get("weight")), woman),)
                    w["source"].pop("weight_class", None)
                    line.append("abilities: %d of 55 changed (%s band)" % (sum(1 for x, y in zip(ab, old) if x != y), F2.band_of(_B.engine_lbs(w.get("weight")), woman)))
            json.dump(m, open(mp, "w"))
            line.append("%d own moves for the reference's (%s), %d slots kept it" % (len(own), " ".join("%d:%d->%d" % x[:3] for x in own), len(left)))
        how = TH.write_own(d, e["name"], e["code"])     # (reduxthemes: as --all writes it; never over his own)
        if not how.startswith("his own theme kept") or "theme" not in w.get("source", {}): w.setdefault("source", {})["theme"] = how
        if "music" not in w:                       # (2026-11-15, revfix: his entrance's music slot -- see REV_MUSIC_SLOT)
            w["music"] = REV_MUSIC_SLOT; w["source"]["music"] = MUSIC_WHY; line.append("music slot %d" % REV_MUSIC_SLOT)
        line.append("theme: " + how)
        if ph is not None and "his photo is made from his face picture" in w.get("about", ""): w["about"] = ABOUT_REST % (e["name"], e["code"])
        with open(wp, "w", encoding="utf-8", newline="\n") as f: json.dump(w, f, indent=1)
        report.append(" | ".join(line))
    open(os.path.join(a.out, "refresh_report.txt"), "w").write("\n".join(report) + "\n")
    print("\n".join(report)); print("to take out:", take_out)

# ---- (2026-11-13, movelib2) his moveset as library IDs ------------------------------------------------------------------
REC_FROM_ROM = {}      # (sid -> {slot: {byte: value}}: --records)
FROZEN_IDS = os.path.join(HERE, "handlers", "revenge", "legacy_ids.json")
FROZEN_LIBRARY = os.path.join(HERE, "handlers", "revenge", "library_ids.json")

def load_frozen_ids():
    """string id -> his frozen number ID ("0x038C"), handlers\\revenge\\legacy_ids.json (newhome 2026-11-17)"""
    if not os.path.exists(FROZEN_IDS): return {}
    return {k: v for k, v in json.load(open(FROZEN_IDS, encoding="utf-8")).items() if k != "about"}

def load_library(path):
    """string id -> {slot: library ID}, from the Revenge library's index (revlib.py), or {} if there is none -- with the
    frozen per-man index laid over it (newhome 2026-11-17, handlers\\revenge\\library_ids.json: the library IDs the
    installed folders play, for the library the game ships; a man there takes exactly his slots from it)"""
    lib = {}
    if path and os.path.exists(path):
        lib = {sid: {int(k): v for k, v in m["ids"].items()} for sid, m in json.load(open(path)).items()}
    if os.path.exists(FROZEN_LIBRARY):
        for sid, m in json.load(open(FROZEN_LIBRARY, encoding="utf-8")).items():
            if sid != "about": lib[sid] = {int(k): v for k, v in m["ids"].items()}
    return lib

def moves_json(sid, slots, ab, lib, fx=None):
    """moves.json: his 292 slots ([move, flag, flag, flag]; a slot that plays one of his own Revenge moves names its library
    move, "revenge:<pair>@<the No Mercy move it's played on>"), and his abilities"""
    ids = lib.get(sid, {})
    out = [list(s) for s in slots]
    for slot, mid in ids.items():
        if 0 <= slot < len(out): out[slot][0] = mid
    about = ("his moveset, slot by slot, as the handler mapped Revenge's list (gen/mkroster.py port_rev_moveset): [move, flag, "
             "flag, flag] per slot 0-291 -- a slot that plays his own Revenge move names it in the built-in Revenge library "
             "(\"revenge:<attacker file>_<defender file>@<the No Mercy move it's played on>\": %d of his slots); abilities 0-54"
             % len(ids))
    j = {"about": about, "slots": out, "abilities": ab}
    # (his fixes' entries past the 292 and record changes, as moves.json has them: exe arcade_wrestlers.cpp read_edits)
    mv = (fx or {}).get("moveset", {})
    if mv.get("extra"): j["extra"] = mv["extra"]
    if mv.get("records"): j["records"] = mv["records"]
    # (2026-11-13, movelib2: and Revenge's own move data's, where it maps -- his fixes' win)
    for slot, rec in REC_FROM_ROM.get(sid, {}).items(): j.setdefault("records", {}).setdefault(slot, rec)
    return j

def moves_only(a, lib):
    """--moves-only: each folder under <out>\\Revenge made before -- his moves.json's slots as library IDs (the No Mercy numbers
    it has kept where no library move goes), his "about" with the \\a fixed"""
    root = os.path.join(a.out, "Revenge")
    fx = G.fixes()
    n = 0
    for sid in sorted(os.listdir(root)):
        d = os.path.join(root, sid)
        mp, wp = os.path.join(d, "moves.json"), os.path.join(d, "wrestler.json")
        if not os.path.exists(mp): continue
        m = json.load(open(mp))
        # (from No Mercy's numbers: a slot a run before gave an ID goes back to the move it was played on)
        plain = [[int(s[0].split("@")[1].split("/")[0]) if isinstance(s[0], str) and "@" in s[0] else s[0]] + list(s[1:]) for s in m["slots"]]
        mj = moves_json(sid, plain, m.get("abilities", []), lib, fx.get(sid))
        json.dump(mj, open(mp, "w"))
        w = json.load(open(wp, encoding="utf-8"))
        if "\x07" in w.get("about", ""):
            w["about"] = w["about"].replace("\x07", "\\a")
            with open(wp, "w", encoding="utf-8", newline="\n") as f: json.dump(w, f, indent=1)
        n += 1
        print("%s: %d slots as library IDs" % (sid, len(lib.get(sid, {}))))
    print("%d folders' moves.json rewritten in %s" % (n, root))

# The 38 looks fields in the order the game unpacks a costume's packed look (func_8001AC44; mod parts.c fields[]): their
# offsets in the 0x3F-byte looks struct. A folder's "looks" lists each costume's values in this order. (2026-11-13, handlers1)
LOOK_OFFSETS = [0x00, 0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0F, 0x12, 0x13, 0x15, 0x18, 0x1B, 0x1E,
                0x1F, 0x21, 0x22, 0x24, 0x25, 0x27, 0x28, 0x2A, 0x2B, 0x2D, 0x2E, 0x30, 0x31, 0x32, 0x33, 0x36, 0x37, 0x39, 0x3C]
def looks_fields(un):          # an unpacked looks struct (0x3F bytes) -> its 38 fields' values, for "looks"
    return [un[o] for o in LOOK_OFFSETS]

# ---- his body (2026-11-13, revbody: handlers/revenge/body.py -- what Revenge keeps, and what No Mercy does with it) ------
def body_type_of(e, fx=None):
    """(No Mercy body type, why) for Revenge record e: his fixes' hand choice when they keep one, else the rule's"""
    from handlers.revenge import body as B
    if fx and fx.get("body_type") is not None:
        return fx["body_type"], fx.get("body_type_why", "his fixes' hand choice")
    return B.nm_body_type(e, e["body"])

def body_source(e, body, why):
    """what "source" says of his body: Revenge's (word, frame, scale), the No Mercy type it became and why, what that type
    picks (the belt mesh, the hands' class), and both games' size for him"""
    from handlers.revenge import body as B
    s = dict(e["body"])
    belt = B.NM_BELT[body]
    s.update(nm_body_type=body, nm_name=B.NM_NAMES.get(body, str(body)), why=why, nm_belt=belt,
             nm_hand_class=B.NM_HAND_CLASS[belt])
    h = e["height"] if e["height"] is not None else B.height_for_scale(e["body"]["scale"])
    s["nm_height_scale"] = B.nm_scale(h)   # (No Mercy's scale for his height: what he'd have without "size")
    s["nm_size"] = body_size(e)            # (2026-11-13, revbody: "size", and the scale it gives -- Revenge's, to 0.0003)
    s["nm_size_scale"] = B.scale_of_size(s["nm_size"])
    return s

def body_size(e):
    """his folder's "size": No Mercy's size code for Revenge's scale of him (body.py nm_size_for_scale)"""
    from handlers.revenge import body as B
    return B.nm_size_for_scale(e["body"]["scale"])

def body_height_for(e):
    from handlers.revenge import body as B
    return B.height_for_scale(e["body"]["scale"])

_base_looks = {}
def base_looks(ordinal):
    """the game's own Superstar of this ordinal: his four costumes' looks as numbers (the 38 fields) -- his appearance
    entries (the game's file 1) as the game unpacks them (gen/mkparts.py vapp, costume_src), checked to come back the
    same through the game's own packing and unpacking"""
    if ordinal not in _base_looks:
        import mkparts as P
        out = []
        for c in range(4):
            un = P.costume_src(P.vapp(ordinal, c))[1]
            assert P.van_unpack(P.van_pack(un)) == bytes(un), "ordinal %d costume %d: the packing changes it" % (ordinal, c)
            out.append(looks_fields(un))
        _base_looks[ordinal] = out
    return _base_looks[ordinal]

def G_costume0(f):
    import mkparts as P
    return P.COSTUME0[f["key"]]

if __name__ == "__main__":
    main()
