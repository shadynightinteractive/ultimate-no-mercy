# capture.py (imports2, 2026-11-13): Badd Blood's Superstar k in costume c in a match, as data/wmport/wmcap.py does it for
# WrestleMania 2000 (the same code): from the Superstar Select (cap\<state>.st, the cursor on him), the costume the model
# loader makes forced to c (0x800146CC: lui $a1, c), picked for both slots, into the match; -> cap\<tag>_c<c>_ram.bin + shots
import os, sys, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
state, tag, c = sys.argv[1], sys.argv[2], int(sys.argv[3])
steps = ["poke:800146CC:%X" % (0x3C050000 | c), "reload", "A", "w60", "A", "w60", "A", "w120", "shot:%s_c%d_load" % (tag, c)]
for t in range(6):
    for i in range(4): steps.append("START" if t in (2, 4) and i == 0 else "A")
    steps.append("shot:%s_c%d_%02d" % (tag, c, t))
steps.append("save:%s_c%d" % (tag, c))
subprocess.check_call([sys.executable, os.path.join(HERE, "bb.py"), state] + steps)
