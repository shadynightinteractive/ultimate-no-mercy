#!/usr/bin/env python3
"""modcheck (2026-11-13): the checks every import from a mod runs, so a mod's changed tables can't slip through again.

  python tool\\modcheck.py tables --family nomercy --base <clean ROM> (--mod <patched ROM> | --patch <xdelta>) --out <dir>
                                  [--base-ram <RAM image> --mod-ram <RAM image>]
        the MOD TABLE DIFF: every table the importers read, stock vs mod -> <dir>\\remap.json (what handlers load:
        handlers\\aki\\modtables.py Remap) and <dir>\\tables.txt (the same, to read). Families: nomercy (No Mercy and its
        mods: the parts probe and the roster need each game's RAM image -- rig\\boot_ram.py makes one), wm2000 (WrestleMania
        2000 and its mods), revenge (no mods known: the file table only).
  The VISUAL CHECK is tool\\visual.py (per import: tool\\check_import.py); the TEXTURE PACK check is tool\\texcheck.py.
Nothing is written but <dir>; ROMs are read in place, a patch applied in memory.
"""
import argparse, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers.aki import modtables as MT
# (the machine is shared: this runs below normal priority, RULES.md)
if os.name == "nt":
    import ctypes; ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)

def summary(d):
    L = ["MOD TABLE DIFF (modcheck, 2026-11-13): %s" % d["how"],
         "family %s; base sha1 %s, mod sha1 %s%s" % (d["family"], d["base_sha1"][:12], d["mod_sha1"][:12],
                                                  "  -- IDENTICAL: no mod" if d["identical"] else ""), ""]
    for name, t in d["tables"].items():
        st = t.get("status")
        line = "%-16s %-8s %s" % (name, st.upper() if st != "same" else "same", t.get("about", ""))
        L.append(line)
        if name == "files":
            L.append("    stock table ROM 0x%X, %d files, data 0x%X; mod's 0x%X, %d files, data 0x%X%s; %d changed, %d added, "
                     "%d removed, %d moved here from another number" % (t["stock_table"][0], t["stock_table"][1] - 1,
                     t["stock_table"][2], t["mod_table"][0], t["mod_table"][1] - 1, t["mod_table"][2],
                     " (MOVED)" if t["moved_table"] else "", len(t["changed"]), t["added_count"], len(t["removed"]),
                     len(t["moved_from"])))
            continue
        if st in ("same", "unknown"): continue
        ch = t.get("changed", []); ad = t.get("added", []); rm = t.get("removed", [])
        L.append("    %d changed, %d added, %d removed" % (len(ch), len(ad), len(rm)))
        for k in (ch[:40] if name.startswith(("parts.", "caw_", "names", "roster")) else ch[:8]):
            s = t.get("stock", {}).get(str(k)); m = t.get("mod", {}).get(str(k))
            if name == "roster" and isinstance(s, dict):
                L.append("      %-6s %-20s -> %-20s %s" % (k, s.get("name"), m.get("name"), ",".join(t["fields_changed"][k])))
                continue
            extra = ""
            if "map_mod_to_stock" in t:
                ms = t["map_mod_to_stock"].get(str(k)); sm = t["map_stock_to_mod"].get(str(k))
                extra = "\n           (the mod's %s builds what stock %s; stock %s builds what the mod's %s)" % (
                    k, ms if ms is not None else "-- no stock number does", k, sm if sm is not None else "-- none of the mod's does")
            L.append("      %-4s stock: %s\n           mod:   %s%s" % (k, s, m, extra))
        if len(ch) > 40: L.append("      ... (%d more: remap.json)" % (len(ch) - 40))
        for k in ad[:12]: L.append("      + %s: %s" % (k, t.get("mod", {}).get(str(k))))
    return L

def cmd_tables(a):
    base, mod, how = MT.load_mod(a.base, a.mod, a.patch)
    ram = lambda p: open(p, "rb").read() if p else None
    os.makedirs(a.out, exist_ok=True)
    log = []
    def lg(s): print(s, flush=True); log.append(s)
    d = MT.diff(a.family, base, mod, ram(a.base_ram), ram(a.mod_ram), how=how, probe=not a.no_probe, log=lg)
    d["inputs"] = {"base": os.path.basename(a.base), "mod": a.mod and os.path.basename(a.mod),
                   "patch": a.patch and os.path.basename(a.patch), "base_ram": a.base_ram and os.path.basename(a.base_ram),
                   "mod_ram": a.mod_ram and os.path.basename(a.mod_ram)}
    json.dump(d, open(os.path.join(a.out, "remap.json"), "w"), indent=1)
    L = summary(d) + [""] + log
    open(os.path.join(a.out, "tables.txt"), "w", encoding="utf-8").write("\n".join(L) + "\n")
    print("\n".join(L[:60]))
    print("-> %s" % os.path.join(a.out, "remap.json"))

def main():
    p = argparse.ArgumentParser()
    sp = p.add_subparsers(dest="cmd", required=True)
    t = sp.add_parser("tables")
    t.add_argument("--family", required=True, choices=MT.FAMILIES)
    t.add_argument("--base", required=True); t.add_argument("--mod"); t.add_argument("--patch")
    t.add_argument("--base-ram"); t.add_argument("--mod-ram"); t.add_argument("--no-probe", action="store_true")
    t.add_argument("--out", required=True)
    a, rest = p.parse_known_args()
    if a.cmd == "tables":
        if not (a.mod or a.patch): p.error("--mod or --patch")
        cmd_tables(a)

if __name__ == "__main__":
    main()
