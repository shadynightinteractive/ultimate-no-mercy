#!/usr/bin/env python3
"""goldust_remap_freeze.py (phase3 p3goldust, 2026-11-19): the FREEZE RULE's table for Goldust's looks, and its developer check.

tools\\handlers\\imports2.py goldust takes No Mercy Plus V3's looks through the mod's remap (handlers\\aki\\modtables.py Remap,
loaded from tools\\handlers\\work\\nmplus\\remap.json, which tools\\handlers\\modcheck.py tables makes: handlers\\aki\\nmprobe.py
runs No Mercy's and No Mercy Plus's model builders on every value 0-255 of every looks field, ~20,000 builds). It asks the
remap two kinds of question (handlers\\customs\\nmplus.py):
  goldust_apps -> set_app_stock -> Remap.stock("parts.<field>", v)   "v (0: none) means here what it means in the game": the
                                                                     import stops if the mod changed it
  tame -> Remap.to_stock("parts.<field>", v)                         the game's number that builds what the mod's v does
                                                                     (none: the mod's own part)
No Mercy Plus V3 is a fixed patch (the importer knows its ROM by fingerprint), so the answers are constants. They are frozen,
part numbers only, in tools\\romimport\\frozen\\nmplus_goldust_remap.json:
  "mod_sha1"            the No Mercy Plus V3 ROM the remap was made for (the native code stops on another)
  "stock_meaning_ok"    {"parts.<field>": [values whose stock meaning Remap.stock allowed]}
  "to_stock"            {"parts.<field>": {"<value>": stock value, or null: no stock equivalent}}
The native code (exe\\...\\romimport\\ri_goldust.cpp) asks the table instead; a question not in it stops the import.

    python goldust_remap_freeze.py write <python worktree> <frozen json>   make the table: every question goldust's looks
        ask the remap (nmplus.goldust_apps + tame, as imports2.py goldust calls them), with the remap's answers
    python goldust_remap_freeze.py check <python worktree> <frozen json>   the developer check: (1) every question the Python
        asks has the table's answer from the real remap; (2) goldust_apps + tame with the remap replaced by the table give the
        same four costumes and the same four looks as with the real remap
Run with the reference toolchain's Python; it only reads the worktree (its tools\\handlers\\work and data links)."""
import json, os, sys
sys.dont_write_bytecode = True

def setup(wt):
    sys.path.insert(0, os.path.join(wt, "tools", "handlers"))
    from handlers.customs import nmplus as NP
    from handlers.aki import modtables as MTB
    from handlers.nomercy import game as NMG
    return NP, MTB, NMG

TEST_PHOTO_ORDINAL = 21          # (imports2.py goldust: stock("test"), No Mercy's Test)

def run(NP, NMG, nm, remap, photos):
    """imports2.py goldust's looks steps with `remap` -> (apps, looks)"""
    nm.remap = remap
    base, apps = NP.goldust_apps(nm)
    rep = []
    looks = [NMG.looks_of(NP.set_app(NP.tame(a, photos[c], remap, rep), hat_c=c)) for c, a in enumerate(apps)]
    return apps, looks

def test_photos(NMG):
    """stock("test")'s looks' photo field per costume: the game's own Test (ordinal 21), costume c's appearance's last byte"""
    g = NMG.NoMercy()
    return [g.appearance(g.costume_struct(TEST_PHOTO_ORDINAL, c)["appearance"])[0x25] for c in range(4)]

class Frozen:
    """modtables.Remap's two questions answered from the frozen table"""
    def __init__(self, d, MTB): self.d, self.MTB, self.used = d, MTB, []
    def stock(self, table, key, why=""):
        if key not in self.d["stock_meaning_ok"].get(table, []): raise self.MTB.ModTableChanged("frozen: %s %r" % (table, key))
        self.used.append((table, key, "stock meaning", True, why)); return key
    def to_stock(self, table, key, why=""):
        a = self.d["to_stock"].get(table, {})
        if str(key) not in a: raise SystemExit("frozen table has no %s %r" % (table, key))
        if a[str(key)] is None: raise self.MTB.ModTableChanged("frozen: %s %r is the mod's own" % (table, key))
        self.used.append((table, key, "to_stock", True, why)); return a[str(key)]
    def report(self): return []

def questions(NP, MTB, NMG):
    nm = NP.NMPlus(); real = nm.remap
    apps, looks = run(NP, NMG, nm, real, test_photos(NMG))
    stock_ok, to_stock = {}, {}
    for table, key, how, ok, why in real.used:
        if how == "stock meaning":
            if ok: stock_ok.setdefault(table, [])
            if ok and key not in stock_ok[table]: stock_ok[table].append(key)
        else:     # (to_stock: "same", or "stock equivalent <s>")
            s = key if how == "same" else (None if not ok else int(how.split()[-1]))
            to_stock.setdefault(table, {})[str(key)] = s
    return nm, real, apps, looks, stock_ok, to_stock

def main():
    mode, wt, path = sys.argv[1], sys.argv[2], sys.argv[3]
    NP, MTB, NMG = setup(wt)
    nm, real, apps, looks, stock_ok, to_stock = questions(NP, MTB, NMG)
    if mode == "write":
        d = {"about": "Goldust's looks' questions to No Mercy Plus V3's remap, frozen (FREEZE RULE): made by tools\\romimport\\"
                      "native\\goldust_remap_freeze.py write from tools\\handlers\\modcheck.py tables' remap (handlers\\aki\\"
                      "modtables.py Remap.stock / Remap.to_stock, as handlers\\customs\\nmplus.py goldust_apps and tame ask "
                      "them); checked by goldust_remap_freeze.py check. Part numbers only.",
             "mod_sha1": real.d["mod_sha1"], "stock_meaning_ok": stock_ok, "to_stock": to_stock}
        with open(path, "w", newline="\n") as f: json.dump(d, f, indent=1); f.write("\n")
        print("wrote %s: %d stock claims, %d to_stock answers" % (path, sum(len(v) for v in stock_ok.values()),
                                                                 sum(len(v) for v in to_stock.values())))
        return
    d = json.load(open(path))
    bad = 0
    if d["mod_sha1"] != real.d["mod_sha1"]: print("BAD mod_sha1"); bad += 1
    if d["stock_meaning_ok"] != stock_ok: print("BAD stock claims differ from the remap's"); bad += 1
    if d["to_stock"] != to_stock: print("BAD to_stock answers differ from the remap's"); bad += 1
    fz = Frozen(d, MTB)
    apps2, looks2 = run(NP, NMG, nm, fz, test_photos(NMG))
    if apps2 != apps: print("BAD costumes differ with the table"); bad += 1
    if looks2 != looks: print("BAD looks differ with the table"); bad += 1
    print("%s: %d questions (%d stock claims, %d to_stock); costumes %s; looks %s" % (
        "OK" if not bad else "FAILED", len(real.used), sum(len(v) for v in stock_ok.values()), sum(len(v) for v in to_stock.values()),
        "SAME" if apps2 == apps else "DIFFERENT", "SAME" if looks2 == looks else "DIFFERENT"))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
