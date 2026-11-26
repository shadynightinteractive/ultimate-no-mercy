#!/usr/bin/env python3
# cc_all.py (handlers, 2026-11-14, revfix): Revenge's own Costume Change screen for every Superstar on its roster, his code
# poked in and "Default" pressed (his own attire and mask, as Revenge gives them) -- the picture his select photo is cropped
# from (handlers\revenge\fixes2.py photo), as the 24 ports' were (data\revport\ports4 cc_sheet.sh). The rig (rv.py here)
# from ports2's cc_hennig state (<RIG>\cap\cc_hennig.st -- copy data\revport\ports2\cap\cc_hennig.st there), where the
# cursor is on Mask; below it are as many colour rows as his costume has (0, 1 or 2), then Decision, RIGHT of it Default --
# so it tries 1, 2, 3 DOWNs and keeps the shot where Default was pressed: the name box is no longer the state's own ("Macho
# Man Randy Savage": <RIG>\cc_fail_template.png, the screen with no Default pressed) and the cursor is on Default. Writes
# <RIG>\cap\cc_<code>.png. One emulator at a time, below normal priority; a rerun skips codes already shot. (Savage himself,
# 0x202, can't be told from the state's own man this way: his photo stays his port's.)
#   python cc_all.py [code ...]
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from handlers import DEV, RIG
from handlers.revenge import game as G
import numpy as np
from PIL import Image
env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", NOSTATE="1")
FAIL = np.array(Image.open(os.path.join(RIG, "cc_fail_template.png")).convert("RGB")).astype(int)
def took(path):
    a = np.array(Image.open(path).convert("RGB")).astype(int)
    green = lambda x0, x1: (a[203:224, x0:x1, 1] - a[203:224, x0:x1, 0]).clip(0).mean()
    return np.abs(a[70:92, 55:310] - FAIL[70:92, 55:310]).mean() > 5 and green(160, 272) > green(48, 162)
codes = [int(x, 16) for x in sys.argv[1:]] or [e["code"] for e in G.roster()]
for c in codes:
    tag = "cc_%03x" % c
    if os.path.exists(os.path.join(RIG, "cap", tag + ".png")): continue
    for n in (1, 2, 3):
        tr = "%s_try%d" % (tag, n)
        steps = ["p:cc_hennig", "w10", "poke16:800566e0:%x" % c, "poke16:8006a2a4:%x" % c, "poke16:8007ef7e:%x" % c, "w30"]
        steps += ["DOWN", "w30"] * n + ["RIGHT", "w30", "A", "w120", "shot:" + tr]
        r = subprocess.run([sys.executable, "rv.py"] + steps, cwd=HERE, env=env, capture_output=True, text=True,
                           creationflags=0x00004000 if os.name == "nt" else 0)    # (BELOW_NORMAL_PRIORITY_CLASS)
        p = os.path.join(RIG, "cap", tr + ".png")
        if os.path.exists(p) and took(p):
            os.replace(p, os.path.join(RIG, "cap", tag + ".png")); print(tag, "ok (%d down)" % n, flush=True); break
    else:
        print(tag, "FAILED", r.stderr[-200:], flush=True)
print("done", flush=True)
