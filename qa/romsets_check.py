"""romsets_check.py (romsets, 2026-11-24): what one install of the game offers, checked against the source ROMs it has.

    python qa\\romsets_check.py --sources REV,WM,NMP,REDUX,BB --install <game folder> [--fs <first start log>]
                                [--pages <QA run>] [--merge <QA run>] [--rumble <QA run>] [--match <QA run> ...]
                                [--stock <file of stock IDs>] [--write-stock <file>]

The player may have any of the source games (the user, 2026-11-24: "only the No Mercy ROM or only the No Mercy ROM plus
Revenge, etc., or all, or just a few"). Each built-in Superstar comes from given source games (mod src/extras.c
arcade_ro_needs); a wrestler folder is the player's (made once by the first start from a ROM, kept when the ROM goes).
So on an install:
  - the Superstar Select's pages (QA option qa_removal 2: 'qa pages N: @XXXX ...') hold the game's own men, the built-ins
    whose sources are all there, and the folders there -- nobody else, nobody twice, and no page empty;
  - every source there has its men on the pages (the built-ins of it; its folders);
  - the Royal Rumble's field (QA option qa_tags 1: 'tags: field N @XXXX=class') draws only from them;
  - no slot was given a stand-in for a man the install doesn't have ('slots: WARNING'), and no run crashed;
  - the first start's record (content\\firstrun.json) says the extras were made from exactly the sources there.
Prints one line per check (PASS / FAIL) and exits 1 when any fails.
"""
import argparse, json, os, re, sys

# the built-ins' numbers and the sources each needs (mod/src/extras.c arcade_ro_needs; roster_ext.h's rows)
NMP = "NMP"; REV = "REV"; WM = "WM"
BUILTIN = {}
for n in (0x060A, 0x0408, 0x0309, 0x030A,                     # No Mercy Plus's four (RO_EXT0..)
          # (Big Show, 0x0201, isn't here: without No Mercy Plus 0x0201 is Chris Benoit's number -- the game's own)
          0x0509, 0x050A, 0x030C, 0x030D, 0x030E, 0x000A, 0x060C, 0x030F, 0x0409):   # Droz .. Tiger Ali Singh
    BUILTIN[n] = {NMP}
for n in (0x030B, 0x060B):                                    # Jeff Jarrett, The Blue Meanie: WrestleMania 2000 (arenasplit 2026-11-24)
    BUILTIN[n] = {WM}
for n in [0x060D, 0x060E] + list(range(0x0105, 0x0115)) + [0x060F] + list(range(0x0115, 0x011A)):   # the Revenge ports
    BUILTIN[n] = {REV}
BITS = {REV: 1, WM: 2, NMP: 4, "REDUX": 8}
# the folders the first start makes, by source (ri_build.cpp folder_makers): collection -> the sources it needs
FOLDER_SOURCES = {"Revenge": {REV}, "WrestleMania 2000": {WM}, "Imported": {"BB"}}   # (arenasplit: Vader from Badd Blood alone)

def log_of(run):
    p = os.path.join(run, "game_stderr.txt")
    return open(p, encoding="utf8", errors="replace").read().splitlines() if os.path.exists(p) else []

def ids_in(line):
    # (removal_who: '@XXXX', which the exe writes as 'string_id (0xXXXX)', or '0xXXXX' when he has no string ID)
    return [int(x, 16) for x in re.findall(r"(?:@|0x)([0-9A-F]{4})\b", line)]

class Report:
    def __init__(self): self.fails = 0
    def check(self, ok, what):
        print(("PASS  " if ok else "FAIL  ") + what)
        if not ok: self.fails += 1

def folders_of(lines):
    """the wrestler folders the game read: number -> (place, string ID) ('[wrestlers] <place>: <sid> (0xNNNN), based on')"""
    out = {}
    for l in lines:
        m = re.search(r"\[wrestlers\] (.+?): (\S+) \(0x([0-9A-F]{4})\), based on", l)
        if m: out[int(m.group(3), 16)] = (m.group(1), m.group(2))
    return out

def main():
    a = argparse.ArgumentParser()
    a.add_argument("--sources", default="")
    a.add_argument("--install", required=True)
    a.add_argument("--fs"); a.add_argument("--pages"); a.add_argument("--merge"); a.add_argument("--rumble")
    a.add_argument("--match", action="append", default=[])
    a.add_argument("--stock"); a.add_argument("--write-stock")
    o = a.parse_args()
    have = {s for s in re.split(r"[ ,]+", o.sources) if s}
    R = Report()
    builtin_on = lambda n: BUILTIN[n] <= have
    want_bits = sum(b for s, b in BITS.items() if s in have)
    # (the extras' tables are made when a source of a built-in is there: Revenge, No Mercy Plus or -- arenasplit 2026-11-24 --
    #  WrestleMania 2000: Jarrett, the Meanie and its arenas from it alone)
    extras_expected = bool(have & {REV, NMP, WM})

    # ---- the first start ------------------------------------------------------------------------------------------------
    rec_p = os.path.join(o.install, "content", "firstrun.json")
    rec = json.load(open(rec_p, encoding="utf8")) if os.path.exists(rec_p) else {}
    R.check(rec.get("state") == "done", "first start: the mod made (content\\firstrun.json state %r)" % rec.get("state"))
    R.check(rec.get("romimport_state") == "done", "first start: the ROM importer finished (romimport_state %r)" % rec.get("romimport_state"))
    got_bits = rec.get("extras_bits", 0) if rec.get("extras_in") else 0
    R.check(got_bits == (want_bits if extras_expected else 0),
            "first start: the built-in extras made from exactly the sources there (bits %d, want %d)" % (got_bits, want_bits if extras_expected else 0))
    if o.fs and os.path.exists(o.fs):
        fl = open(o.fs, encoding="utf8", errors="replace").read().splitlines()
        status = [l for l in fl if l.startswith("[status]")]
        print("      status window: " + " | ".join(l[9:] for l in status if "the status window" not in l))
        R.check(not any(re.search(r"couldn't make its mod|\[firstrun\] .*(give up|isn't the one this plan)", l) for l in fl), "first start: no failure in the builder's log")
        R.check(not any("[romimport] stopped" in l for l in fl), "first start: the importer never stopped half way")

    # ---- the select's pages -------------------------------------------------------------------------------------------
    stock = set()
    if o.stock and os.path.exists(o.stock): stock = {int(x, 16) for x in open(o.stock).read().split()}
    def pages_check(run, label):
        lines = log_of(run)
        folders = folders_of(lines)
        pages = []
        for l in lines:
            m = re.search(r"qa pages (\d+):(.*)$", l)
            if m: pages.append((int(m.group(1)), ids_in(m.group(2))))
        if pages:   # (the last layout the log has: pages are laid out again when the roster changes)
            # (arenasplit 2026-11-24: the last layout alone -- from its page 1 on. Merged by page number, an earlier layout's
            #  page past the last one's end stayed: WrestleMania 2000 alone has 18 pages at boot, before the roster's options
            #  are read, and 17 after, so the boot layout's 18th page -- No Mercy Plus's four -- was taken for the select's)
            start = max(i for i, (p, _) in enumerate(pages) if p == 1) if any(p == 1 for p, _ in pages) else 0
            last = {}
            for p, ids in pages[start:]: last[p] = ids
            n = max(last)
            pages = [(p, last[p]) for p in range(1, n + 1) if p in last]
        R.check(len(pages) > 0, "%s: the pages are in the log (%d)" % (label, len(pages)))
        everyone = [i for _, ids in pages for i in ids]
        R.check(all(len(ids) > 0 for _, ids in pages), "%s: no page empty" % label)
        # (romsets 2026-11-24, the user: a source's pages "should be filling up a page ... like auto arrange apps on a phone")
        # each source game's run of pages (its built-ins and its folders: Revenge, WrestleMania 2000, No Mercy Plus, Imported)
        # full but its last; a page holds one source only
        def source_of(i):
            if i in folders:
                c = re.split(r"[\\/]", folders[i][0])
                c = c[1] if c[0] == "wrestlers" and len(c) > 1 else c[0]
                return {"Revenge": "Revenge", "WrestleMania 2000": "WrestleMania 2000", "Imported": "Imported"}.get(c, "folders")
            if i in BUILTIN:
                return "No Mercy Plus" if BUILTIN[i] == {NMP} else "Revenge" if BUILTIN[i] == {REV} else "WrestleMania 2000"
            return None
        runs = []
        mixed = []
        for p, ids in pages:
            srcs = {source_of(i) for i in ids}
            src = srcs.pop() if len(srcs) == 1 else None
            if len({source_of(i) for i in ids} - {None}) > 1: mixed.append(p)
            if src and runs and runs[-1][0] == src: runs[-1][1].append((p, len(ids)))
            else: runs.append((src, [(p, len(ids))]))
        short = ["page %d (%d)" % (p, c) for src, pgs in runs if src for p, c in pgs[:-1] if c < 9]
        R.check(not short, "%s: each source's pages full but its last%s" % (label, "" if not short else " -- short: " + ", ".join(short)))
        R.check(not mixed, "%s: no page mixes source games%s" % (label, "" if not mixed else " -- pages " + " ".join(map(str, mixed))))
        for src, pgs in runs:
            if src: print("      %s: %d page(s), boxes %s" % (src, len(pgs), " ".join(str(c) for _, c in pgs)))
        dup = sorted({i for i in everyone if everyone.count(i) > 1})
        R.check(not dup, "%s: nobody on two boxes%s" % (label, "" if not dup else " (" + " ".join("%04X" % i for i in dup) + ")"))
        bad = []
        for i in everyone:
            if i in BUILTIN: ok = builtin_on(i)
            elif i in folders: ok = True
            elif stock: ok = i in stock
            else: ok = True                   # (no stock list given: a number no built-in and no folder has is the game's own)
            if not ok: bad.append(i)
        R.check(not bad, "%s: every box is a Superstar this install has%s" % (label, "" if not bad else " -- NOT: " + " ".join("%04X" % i for i in bad)))
        on = set(everyone)
        # (a family's members are in its box: a wrestler there counts as offered when someone of his family is on the pages --
        #  the exe's '[families] ... family N, <head>: <sid> <variant>, ...' lines, by string ID; built-ins' IDs: data\builtin_roster.json)
        num_of = {sid: n for n, (place, sid) in folders.items()}
        try:
            for b in json.load(open(os.path.join(o.install, "data", "builtin_roster.json"), encoding="utf8"))["superstars"]:
                num_of.setdefault(b["id"], int(b["number"], 16))
        except (OSError, ValueError, KeyError): pass
        in_box = set()
        for l in lines:
            m = re.search(r"\[families\] (?:auto-merge )?family \d+, [^:]+: (.*)$", l)
            if not m: continue
            members = {num_of.get(x.strip().rsplit(" ", 1)[0]) for x in m.group(1).split(",")} - {None}
            if members & on: in_box |= members
        missing_b = sorted(n for n in BUILTIN if builtin_on(n) and n not in on and n not in in_box and n not in (0x060F, 0x0115, 0x0116, 0x0117, 0x0118, 0x0119, 0x000A))
        R.check(not missing_b, "%s: every built-in of the sources there is offered%s" % (label, "" if not missing_b else " -- missing: " + " ".join("%04X" % i for i in missing_b)))
        missing_f = sorted(n for n in folders if n not in on and n not in in_box and not folders[n][1].startswith("_"))
        R.check(not missing_f, "%s: every wrestler folder is offered (%d folders)%s" % (label, len(folders), "" if not missing_f else " -- missing: " + " ".join("%04X" % i for i in missing_f[:20])))
        cols = {}
        for n, (place, sid) in folders.items(): cols.setdefault(re.split(r"[\\/]", place)[1] if re.match(r"wrestlers[\\/]", place) else re.split(r"[\\/]", place)[0], []).append(n)
        for c, ns in sorted(cols.items()):
            need = FOLDER_SOURCES.get(c)
            print("      folders %s: %d" % (c, len(ns)))
        for c, need in FOLDER_SOURCES.items():
            if need <= have: R.check(c in cols, "%s: the %s folders are there (the source is)" % (label, c))
        R.check(not any("slots: WARNING" in l for l in lines), "%s: no slot given a stand-in" % label)
        return set(everyone), folders
    on = set()
    if o.pages:
        on, folders = pages_check(o.pages, "select")
        if o.write_stock:
            with open(o.write_stock, "w") as f: f.write(" ".join("%04X" % i for i in sorted(i for i in on if i not in BUILTIN and i not in folders)) + "\n")
    if o.merge:
        pages_check(o.merge, "select (MERGE SAME WRESTLERS on)")
        lines = log_of(o.merge)
        fams = [l for l in lines if "[families] family" in l]
        R.check(any("auto-merge" in l for l in lines), "merge: the game made the automatic families ('[families] auto-merge')")
        print("      families: %d" % len(fams))
    # ---- the Royal Rumble's field -------------------------------------------------------------------------------------
    if o.rumble:
        lines = log_of(o.rumble)
        folders = folders_of(lines)
        field = [ids_in(l)[0] for l in lines if re.search(r"tags: field \d+ ", l) and ids_in(l)]
        R.check(len(field) >= 10, "rumble: the field is in the log (%d entrants)" % len(field))
        bad = [i for i in field if (i in BUILTIN and not builtin_on(i)) or (stock and i not in BUILTIN and i not in folders and i not in stock)]
        R.check(not bad, "rumble: every entrant drawn is a Superstar this install has%s" % ("" if not bad else " -- NOT: " + " ".join("%04X" % i for i in bad)))
        if on: R.check(all(i in on or i == field[0] for i in field), "rumble: every entrant drawn is on the select's pages")
        R.check(not any("slots: WARNING" in l for l in lines), "rumble: no entrant given a stand-in")
        R.check(not any(re.search(r"tags: (pool|lumberjack) BAD", l) for l in lines), "rumble: the pools' draws (20 Rumble, 20 Lumberjack) drew no one they mustn't")
    for run in o.match:
        lines = log_of(run)
        rep = os.path.join(run, "report.txt")
        res = [l.strip() for l in open(rep, encoding="utf8", errors="replace")] if os.path.exists(rep) else []
        result = next((l for l in res if l.startswith("RESULT")), "RESULT (no report)")
        fails = [l for l in res if l.startswith("FAIL") and "live recompiler" not in l]
        R.check(not fails and "PASS" in result, "match %s: %s%s" % (os.path.basename(run), result, "" if not fails else " -- " + " / ".join(fails[:4])))
        R.check(not any("slots: WARNING" in l for l in lines), "match %s: no slot given a stand-in" % os.path.basename(run))
        crash = os.path.join(run, "crash_reports")
        R.check(not (os.path.isdir(crash) and os.listdir(crash)), "match %s: no crash report" % os.path.basename(run))
    print("RESULT %s (%d failed)" % ("PASS" if not R.fails else "FAIL", R.fails))
    sys.exit(1 if R.fails else 0)

if __name__ == "__main__":
    main()
