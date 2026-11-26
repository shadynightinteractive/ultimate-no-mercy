#!/usr/bin/env python3
# capture_all.py (handlers, 2026-11-12): the Revenge handler's model captures for every Superstar not ported yet -- the
# rig (the libretro core, data/revport's way) runs the user's Revenge ROM from the tag match's Decision screen (h11), each
# slot's model code and costume poked in (as ports2 put Mr. Perfect in, and attires each costume), into the match
# (A, 1500 frames); his model is then read as Revenge built it (gen/wmport.py rev_capture). Two men a run.
# Writes cap/auto.pkl: {(code, costume): {part: (mesh file, picture, palette)}}, kept as it goes (a rerun skips what's in).
# The RAM images aren't kept (8 MB each). Below normal priority, one emulator at a time.
import os, pickle, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from handlers import DEV, RIG     # (2026-11-13, final082: no absolute path; the captures go to the rig folder)
sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
from handlers.revenge import game as G
import wmport as W
STORE = os.path.join(RIG, "cap", "auto.pkl")
store = pickle.load(open(STORE, "rb")) if os.path.exists(STORE) else {}
ported = {int(x["revenge"]["code"], 16) for x in G.fixes().values()}
codes = [e["code"] for e in G.roster() if e["code"] not in ported and e["moveset_file"]]
only = [int(x, 16) for x in sys.argv[1:]]
if only: codes = [c for c in codes if c in only]
todo = [(c, k) for k in range(4) for c in codes if (c, k) not in store]
print(len(codes), "men,", len(todo), "costumes to capture", flush=True)
while todo:
    pair = todo[:2]; todo = todo[2:]
    (a, ka), (b, kb) = pair[0], pair[-1]
    tag = "x_%03x_%d_%03x_%d" % (a, ka, b, kb)
    steps = ["p:h11", "poke:8007e8b0:%x" % a, "poke:8007e8b4:%x" % b, "poke16:8007ef7e:%x" % a, "poke16:8007ef8e:%x" % b,
             "poke8:8007ef80:%x" % ka, "poke8:8007ef90:%x" % kb, "poke:8007e930:%x" % ka, "poke:8007e934:%x" % kb,
             "w2", "A", "w1500", "w30", "save:" + tag]
    r = subprocess.run([sys.executable, "rv.py"] + steps, cwd=HERE, env=dict(os.environ, HOOK3="1", NOSTATE="1"),
                       capture_output=True, text=True, creationflags=0x00004000)    # (BELOW_NORMAL_PRIORITY_CLASS)
    ram_p, calls_p = os.path.join(RIG, "cap", tag + "_ram.bin"), os.path.join(RIG, "cap", tag + "_calls.pkl")
    if not os.path.exists(ram_p):
        print(tag, "FAILED", r.stderr[-300:], flush=True); continue
    ram, calls = open(ram_p, "rb").read(), pickle.load(open(calls_p, "rb"))
    loads = [(a0, a1 & 0xFFFF) for t, a0, a1, a2 in calls if t & 0xFF == 3]
    for slot, (code, k) in enumerate(((a, ka), (b, kb))):
        if (slot, code) not in loads: print(tag, "slot %d: his model wasn't loaded (%s)" % (slot, loads[-4:]), flush=True); continue
        try:
            got = W.rev_capture(ram, calls, slot)
        except Exception as ex:
            print(tag, "slot %d: %s" % (slot, ex), flush=True); continue
        store[(code, k)] = got
        print(tag, "slot %d: 0x%03X costume %d, %d parts" % (slot, code, k, len(got)), flush=True)
    pickle.dump(store, open(STORE, "wb"))
    os.remove(ram_p); os.remove(calls_p)
print("done:", len(store), "costumes in", STORE, flush=True)
