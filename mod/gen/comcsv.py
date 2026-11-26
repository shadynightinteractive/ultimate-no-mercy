#!/usr/bin/env python3
"""The commentary as one spreadsheet (CSV): every line JR and King can say, what it's tied to, and a link to hear it.

  python mod\\gen\\comcsv.py export [game folder or commentary.pak]   -> data\\commentary_text\\commentary.csv (+ clips\\NNNN.ogg)
  python mod\\gen\\comcsv.py import                                   data\\commentary_text\\commentary.csv -> mod\\build\\commentary.pak
  (then .\\deploy.ps1 -Commentary puts the pack in the game)

The CSV's columns (one row per line the director can play; a clip can have several rows, one per moment):
  row       the row number at export (leave it; a copied row keeps it; a row you add: leave it empty)
  use       yes / no: no = the line is never said there (or delete the row)
  speaker   JR, King, "JR to King" / "King to JR" (said to the other man: he answers most of the time), either
  text      what's said (only a transcript: changing it doesn't change the audio; the developer's own, from
            data\\commentary_text\\wwe13_text.tsv -- packs made from the shipped recipe hold tags instead)
  event     the moment it's for (the list in commentary\\README.txt, and "when it plays" next to it)
  wrestler  a line about one Superstar (by name: "Kurt Angle"), or empty for anyone;
            "(name follows)": a lead-in ("A heat shot by") the director joins to a name clip
  is        doing it / taking it: the wrestler is the one doing the move, or the one it's done to
  for       men / women: a line only for male / female wrestlers (empty: anyone)
  move      the move it names (from the game's move list: commentary\\moves.txt); empty: any move
  repeat    yes: only when the move was just done again ("Another suplex!")
  arena     a line for where the match is: raw, smackdown, kotr, summerslam, survivor, wrestlemania, rumble (a Rumble in
            its arena), rrmatch (a Royal Rumble anywhere); empty: anywhere
  weight    how likely against the other lines that fit (1 usual, 0.5 half as often)
  source    WWE '13 / Legends of WrestleMania / AI voice (information only)
  clip      the audio clip (clips\\NNNN.ogg); seconds: its length;  play: a link to hear it
"""
import csv, io, os, sys, subprocess
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compak
from names import NAMES

DEV = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
# (commentary2 2026-11-18, the user's decision: no transcripts in the release, and the game's move names are ROM text:
#  the spreadsheet, its clips and the transcripts are the developer's own, in the private folder -- data\commentary_text,
#  linked in as data\ -- never in the repository)
WORK = os.path.join(DEV, "data", "commentary_text")
CSV = os.path.join(WORK, "commentary.csv")
ORIG = os.path.join(WORK, "_export_lines.tsv")     # the rows as exported (their splice data, by row number)
TEXT = os.path.join(WORK, "wwe13_text.tsv")        # the transcripts: clip, its SHA-1, event, what is said (make_tags.py)

# the director's events (exe\...\commentary\director.h), in its order, and when the mod reports each
EVENTS = [
    ("match_start", "the bell"), ("lockup", "a collar-and-elbow tie-up"),
    ("strike", "a punch / kick that lands"), ("strike_big", "a running or strong strike that lands"),
    ("grapple", "a weak grapple move"), ("big_move", "a strong grapple move (slam, suplex, driver)"),
    ("knockdown", "put down on the mat"), ("reversal", "a counter"), ("special", "SPECIAL! lights up"),
    ("finisher", "a special (finishing) move hits"), ("pin", "a cover"), ("count", "the referee's count"),
    ("kickout_1", "kicked out at one"), ("kickout_2", "kicked out at two (a near fall)"), ("pinfall", "the three count"),
    ("submission", "a submission hold applied"), ("rope_break", "out of a hold by the ropes"),
    ("escape", "out of a hold / pin some other way"), ("tap_out", "submits"), ("outside", "goes out to the floor"),
    ("back_in", "back in the ring"), ("outside_hit", "a move on the floor / into the steps / barricade"),
    ("top_rope", "climbing the turnbuckle"), ("aerial", "a move off the top that hits"),
    ("aerial_miss", "a move off the top that misses"), ("weapon", "a weapon picked up"), ("weapon_hit", "a weapon shot"),
    ("table", "through a table"), ("blood", "busted open"), ("tag", "a tag"),
    ("save", "a partner breaks up a pin / hold"), ("taunt", "showboating"), ("run_in", "someone comes down to interfere"),
    ("dominate", "one man well on top (from the damage numbers)"), ("comeback", "the one who was down fights back"),
    ("tired", "both slow, a long match"), ("elimination", "out of a Rumble / over the top"),
    ("rumble_entry", "a Rumble entrant comes out"), ("cage", "a cage / cell spot"), ("ladder", "a ladder spot"),
    ("win", "the match is over (the winner)"), ("dq", "disqualification / count out"),
    ("entrance", "walking out to the ring"), ("sign_off", "after the match"),
    ("filler", "nothing happening for a while"), ("danger", "in trouble (spirit low / taking a beating)"),
    ("name", "a Superstar's name on its own (joined after a lead-in line)"),
]
WHEN = dict(EVENTS)
SPEAKER = {"J": "JR", "K": "King", "j": "JR to King", "k": "King to JR", "M": "either", "-": ""}
SOURCE = {"0": "WWE '13", "1": "Legends of WrestleMania", "2": "AI voice"}
ARENAS = ["raw", "smackdown", "kotr", "summerslam", "survivor", "wrestlemania", "rumble", "rrmatch"]
LEAD = "(name follows)"
COLS = ["row", "use", "speaker", "text", "event", "when it plays", "wrestler", "is", "for", "move", "repeat", "arena",
        "weight", "source", "clip", "seconds", "play"]

# each Superstar's name in the CSV: the full one; the second of two with the same name (the '99 Undertaker) told apart
LABEL = {0x000A: "The Undertaker ('99)"}
def label(k): return LABEL.get(k, NAMES[k][0])

def name_of(hexid):
    k = int(hexid, 16)
    return label(k) if k in NAMES else "id " + hexid

def with_text(rows):
    """a pack made from the shipped recipe has TAGS where the text was (way:N / count:N / -): the developer's transcripts
    (TEXT, when it is there) put back, found by each clip's SHA-1; without the file the tags stay"""
    if not os.path.exists(TEXT): return rows
    import hashlib
    said = {}
    for l in open(TEXT, encoding="utf-8"):
        f = l.rstrip("\n").split("\t")
        if len(f) == 4 and not l.startswith("#"): said[f[1]] = f[3]
    sha = {}
    out = []
    for line in rows:
        f = line.split("\t")
        if f[0] not in sha:
            p = os.path.join(WORK, "clips", "%04d.ogg" % int(f[0]))
            sha[f[0]] = hashlib.sha1(open(p, "rb").read()).hexdigest() if os.path.exists(p) else ""
        t = f[10]
        if (t == "-" or t.startswith("way:") or t.startswith("count:")) and sha[f[0]] in said: f[10] = said[sha[f[0]]]
        out.append("\t".join(f))
    return out

def export(src):
    os.makedirs(WORK, exist_ok=True)
    compak.unpack(src, WORK)
    rows = open(os.path.join(WORK, "lines.tsv"), encoding="utf-8", newline="").read().splitlines()
    rows = with_text(rows)
    open(ORIG, "w", encoding="utf-8", newline="").write("\n".join(rows) + "\n")
    os.remove(os.path.join(WORK, "lines.tsv"))
    moves = open(os.path.join(WORK, "moves.tsv"), encoding="utf-8").read().splitlines()
    open(os.path.join(WORK, "moves.txt"), "w", encoding="utf-8").write(
        "\n".join(sorted({m.split("\t")[1] for m in moves if "\t" in m})) + "\n")
    with open(CSV, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(COLS)
        for n, line in enumerate(rows, 1):
            clip, ev, who, sex, ms, weight, mk, srcg, spk, splice, text = line.split("\t")
            victim = who.startswith("v")
            who = who[1:] if victim else who
            wname = "" if who == "-" else LEAD if who == "*" else name_of(who)
            move = repeat = arena = ""
            if mk.startswith("@"): arena = mk[1:]
            elif mk.startswith("+"): repeat, move = "yes", mk[1:]
            elif mk != "-": move = mk
            w.writerow([n, "yes", SPEAKER.get(spk, spk), text, ev, WHEN.get(ev, ""), wname,
                        ("taking it" if victim else "doing it") if wname else "",
                        {"M": "men", "F": "women"}.get(sex, ""), move, repeat, arena, weight,
                        SOURCE.get(srcg, srcg), clip, "%.1f" % (int(ms) / 1000),
                        '=HYPERLINK("clips\\%04d.ogg","play %04d")' % (int(clip), int(clip))])
    print("%d lines -> %s" % (len(rows), CSV))

def clip_ms(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path],
                         capture_output=True, text=True, check=True).stdout.strip()
    return int(float(out) * 1000)

def do_import(out):
    orig = open(ORIG, encoding="utf-8").read().splitlines()
    ms_of, src_clip = {}, {}
    for line in orig:
        f = line.split("\t"); ms_of[f[0]] = f[4]; src_clip[f[0]] = f[7]
    by_name = {}
    for k in NAMES: by_name[label(k).lower()] = "%x" % k
    for k, v in NAMES.items():
        for nm in v: by_name.setdefault(nm.lower(), "%x" % k)
    spk_of = {v.lower(): k for k, v in SPEAKER.items() if v}
    src_of = {v.lower(): k for k, v in SOURCE.items()}
    nclips = len([x for x in os.listdir(os.path.join(WORK, "clips")) if x.endswith(".ogg")])
    rows, bad = [], []
    raw = open(CSV, "rb").read()
    try: body = raw.decode("utf-8-sig")
    except UnicodeDecodeError: body = raw.decode("cp1252")    # (Excel's plain "CSV (Comma delimited)")
    with io.StringIO(body, newline="") as f:
        for n, r in enumerate(csv.DictReader(f), 2):
            def err(msg): bad.append("line %d (row %s): %s" % (n, r.get("row", ""), msg))
            r = {k: (v or "").strip() for k, v in r.items() if k}
            if not any(r.values()): continue
            use = r.get("use", "yes").lower()
            if use in ("no", "n", "0", "off"): continue
            if use not in ("yes", "y", "1", "on", ""): err("use: yes or no, not %r" % use); continue
            ev = r["event"]
            if ev not in WHEN: err("no event %r" % ev); continue
            try: clip = int(float(r["clip"]))
            except ValueError: err("clip %r" % r["clip"]); continue
            if not 0 <= clip < nclips: err("no clip %d (clips\\ has 0..%d)" % (clip, nclips - 1)); continue
            wn, victim = r["wrestler"], r.get("is", "").lower().startswith("tak")
            if not wn: who = "-"
            elif wn == LEAD: who = "*"
            elif wn.lower().startswith("id "): who = wn[3:].lower()
            elif wn.lower() in by_name: who = by_name[wn.lower()]
            else: err("no Superstar called %r (names: mod\\gen\\names.py)" % wn); continue
            if victim and who != "-": who = "v" + who
            sex = {"men": "M", "women": "F", "": "-"}.get(r.get("for", "").lower())
            if sex is None: err("for: men, women or empty"); continue
            arena, move, rep = r.get("arena", "").lower(), r.get("move", "").lower(), r.get("repeat", "").lower() in ("yes", "y", "1")
            if arena and arena not in ARENAS: err("arena %r (one of %s)" % (arena, ", ".join(ARENAS))); continue
            if arena and (move or rep): err("a line is for an arena or a move, not both"); continue
            mk = "@" + arena if arena else ("+" if rep else "") + move if (move or rep) else "-"
            spk = spk_of.get(r.get("speaker", "").lower(), "-" if not r.get("speaker") else None)
            if spk is None: err("speaker %r (JR, King, JR to King, King to JR, either)" % r["speaker"]); continue
            try: weight = float(r.get("weight") or 1)
            except ValueError: err("weight %r" % r["weight"]); continue
            srcg = src_of.get(r.get("source", "").lower()) or src_clip.get(str(clip), "2")
            splice = "-"
            if r.get("row"):
                try: splice = orig[int(float(r["row"])) - 1].split("\t")[9]
                except (ValueError, IndexError): err("row %r: not one exported (leave it empty for a new row)" % r["row"]); continue
            ms = ms_of.get(str(clip)) or str(clip_ms(os.path.join(WORK, "clips", "%04d.ogg" % clip)))
            text = r.get("text", "").replace("\t", " ").replace("\n", " ")
            rows.append("\t".join([str(clip), ev, who, sex, ms, "%g" % weight, mk, srcg, spk, splice, text]))
    if bad:
        print("not packed -- fix these in commentary.csv first:"); [print("  " + b) for b in bad]; sys.exit(1)
    open(os.path.join(WORK, "lines.tsv"), "w", encoding="utf-8", newline="").write("\n".join(rows) + "\n")
    compak.pack(WORK, out)
    os.remove(os.path.join(WORK, "lines.tsv"))
    print("%d lines (of %d exported) -> %s   (.\\deploy.ps1 -Commentary puts it in the game)" % (len(rows), len(orig), out))

if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["export"]: export(a[1] if len(a) > 1 else (os.environ.get("UNM_GAME") or os.path.join(os.path.dirname(DEV), "game")))
    elif a[:1] == ["import"]: do_import(a[1] if len(a) > 1 else os.path.join(DEV, "mod", "build", "commentary.pak"))
    else: print(__doc__)
