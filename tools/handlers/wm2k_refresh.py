#!/usr/bin/env python3
"""wm2k_refresh.py (2026-11-15, revfix): the WrestleMania 2000 folders brought up to the user's rule -- faithful to the source
game, the universal mechanics No Mercy's -- in place (everything else, his ID first, as it is):

  - his STANCE (ability 1) WrestleMania 2000's own: gen/mkroster.py port_wm_moveset takes abilities 0-2 from his reference
    (the No Mercy man he is based on) -- right for the weight class (WrestleMania 2000's ability 2 means otherwise: 0 for
    Austin, 1 for the cruiserweights), wrong for the stance: Chaz, Droz, Gangrel, Meat, Mideon and Thrasher stood as Kane (3,
    the big men's) where WrestleMania 2000 has them stand as themselves; its ability 1 is No Mercy's (3 the big men, 4 the
    women: Kane, The Undertaker, Test, Big Show 3; Debra, Terri, Stephanie, the Hos 4);
  - his WINNING TAUNT (slot 235) standing, never a turnbuckle one (handlers\\revenge\\fixes2.py win_pose, the same rule as
    for Revenge's men).
  The weight class stays the reference's -- No Mercy's own value for the same man (his based_on) -- and his special flags are
  WrestleMania 2000's own already (port_wm_moveset takes each slot's flags from his WrestleMania 2000 moveset).

    python wm2k_refresh.py <wrestlers folder>          (its "WrestleMania 2000" collection)
"""
import glob, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from handlers import DEV
sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
from handlers.revenge import fixes2 as F2

def main():
    import wm2k, mkroster as K
    root = os.path.join(sys.argv[1], "WrestleMania 2000")
    R = {e["k"]: e for e in wm2k.roster()}
    fx = {os.path.basename(p)[:-5]: json.load(open(p)) for p in glob.glob(os.path.join(HERE, "handlers", "wm2000", "wrestlers", "*.json"))}
    report = []
    for sid in sorted(os.listdir(root)):
        d = os.path.join(root, sid); wp, mp = os.path.join(d, "wrestler.json"), os.path.join(d, "moves.json")
        k = fx.get(sid, {}).get("wm2000", {}).get("roster_index")
        if k is None or not os.path.exists(wp): continue
        w = json.load(open(wp, encoding="utf-8"))
        e = R[k]
        _, wab = wm2k.unpack_moves(wm2k.wfile(e["moves"]), wm2k.wfile(e["abil"]))
        b = bytes.fromhex(w["moveset"]); _, ab = K.unpack(K.EV, K.VAN_UNPACK, b[:0xB8], b[0xB8:])
        line = [sid]
        if ab[1] != wab[1]:
            nab = list(ab); nab[1] = wab[1]
            w["moveset"], old = F2.repack_abilities(w["moveset"], nab)
            w.setdefault("source", {})["stance"] = ("ability 1 (his stance) %d, WrestleMania 2000's own for him -- was %d, his "
                                                    "reference's (2026-11-15, wm2k_refresh.py)" % (wab[1], ab[1]))
            line.append("stance %d -> %d" % (ab[1], wab[1]))
        if os.path.exists(mp):
            m = json.load(open(mp))
            if line[1:] and m.get("abilities"): m["abilities"][1] = wab[1]
            m["slots"], wpose = F2.win_pose(m["slots"])
            if wpose:
                w.setdefault("source", {})["win_pose"] = ("winning taunt (slot 235): %s was a turnbuckle taunt (No Mercy has it only "
                                                          "in slot 232) -- now his own standing taunt, %s (2026-11-15)" % wpose)
                line.append("win pose %s -> %s" % wpose)
            json.dump(m, open(mp, "w"))
        # (newhome 2026-11-17: "weight_from" -- womenfix's, written after this refresh had run on the installed folders -- stays
        #  last, so a fresh import followed by this refresh writes the installed bytes)
        if "weight_from" in w.get("source", {}): w["source"]["weight_from"] = w["source"].pop("weight_from")
        with open(wp, "w", encoding="utf-8", newline="\n") as f: json.dump(w, f, indent=1)
        report.append(" | ".join(line))
    print("\n".join(report))

if __name__ == "__main__":
    main()
