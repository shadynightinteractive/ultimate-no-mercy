"""Each slot's farthest point from the ring's middle in a QA run (state.csv), and how many samples someone spent at the
ring's height (y 80..100) more than 500 out in x or z -- the Royal Rumble's runaway walkers of 2026-11-06 (a QA hook's
artifact, logic.c qa_rr_out).
    python qa\\farthest.py RUN_DIR"""
import csv
import math
import sys

d = sys.argv[1]
rows = list(csv.DictReader(open(d + '/state.csv')))
for s in ('s0', 's1', 's2', 's3', 's4', 's5'):
    mx, at, high = 0, None, 0
    for r in rows:
        try:
            x, y, z = float(r[s + '_x']), float(r[s + '_y']), float(r[s + '_z'])
        except (KeyError, ValueError):
            continue
        if not (math.isfinite(x) and math.isfinite(z)):
            continue
        if 80 <= y <= 100 and max(abs(x), abs(z)) > 500:
            high += 1
        if max(abs(x), abs(z)) > mx:
            mx, at = max(abs(x), abs(z)), (int(r['ms']) // 1000, r[s + '_id'], round(x), round(y), round(z))
    print('  %s farthest %d %s; samples out past 500 at ring height: %d' % (s, mx, at, high))
