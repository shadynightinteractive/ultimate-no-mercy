#!/usr/bin/env python3
"""scenario.py (modcheck, 2026-11-13): the QA scenarios the visual check runs in our game.

  python tool\scenario.py sel <name> <wrestler id> [<about>]   -> scenarios\<name>.json
A Superstar Select tour: the arcade as a player plays it (free play), one player's Versus (logic.c qa_versus): player 1's
pick is driven to HIM with a costume he hasn't (9), so the driver steps the Type arrows round all his costumes again and
again (qa_variant 1: the tour, then 75 frames a press, up to 80 presses; run 153032_mc_sel_gd: the CPU's pick, driven
the same way, wandered off his page) -- ONE run shows every costume of
his on the select screen many times over (the first time round a model may not have loaded yet: seen in run
151018_mc_dump), the same screen and framing as the source game's own select (No Mercy's family). state.csv bs_tries = the Superstar under the
cursor | his costume << 16 | 1 << 24 on the select (logic.c), which says which costume each shot shows."""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
def sel(name, wid, about=""):
    wid = int(wid, 0) if isinstance(wid, str) else wid
    s = {"about": "modcheck (2026-11-13): the visual check's render of 0x%04X on the Superstar Select, every costume (the Type "
                  "arrows' tour) %s" % (wid, about),
         "match": "arcade", "wrestlers": ["0x%04X" % wid, "0x0001"], "costumes": [9, 0], "matches": 0, "seconds": 240,
         "shots": 0.5,
         "options": {"free_play": True, "attract_demos": False, "qa_versus": 20, "qa_variant": 1},
         "expect": [{"name": "his box on the select, under the cursor", "when": "any",
                     "check": "(bs_tries & 0x1000000) != 0 and (bs_tries & 0xFFFF) == 0x%04X" % wid},
                    {"name": "no model's matrices ever went bad", "when": "always", "check": "bad_mtx == 0"}]}
    p = os.path.join(os.environ.get("NM_IMPORT_CHECK_DIR") or os.path.join(HERE, "work"), "scenarios", name + ".json")
    os.makedirs(os.path.dirname(p), exist_ok=True)
    json.dump(s, open(p, "w"), indent=1)
    return p
if __name__ == "__main__":
    if sys.argv[1] == "sel": print(sel(sys.argv[2], sys.argv[3], " ".join(sys.argv[4:])))
