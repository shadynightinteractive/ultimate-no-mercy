"""opens_filter.py: the reads in an opens.py log that are not a game file (extracted or NM+ unpacked) or Python source."""
import re, sys
GAME = re.compile(r"(nmfiles\\f|rev_files|wm2k_files|nmplus\\f)\\[0-9a-f]+\.bin$")
for l in open(sys.argv[1], encoding="utf-8"):
    n, p = l[:6], l[7:].strip()
    if p.endswith(".py") or GAME.search(p): continue
    print(n, p.split("romimport\\")[-1])
cnt = {}
for l in open(sys.argv[1], encoding="utf-8"):
    m = GAME.search(l)
    if m: cnt[m.group(1)] = cnt.get(m.group(1), 0) + 1
print("game files:", cnt)
