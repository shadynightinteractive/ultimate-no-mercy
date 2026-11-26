"""A QA run's stalls: spans of more than 2 s (real time) in which the game's frame counter didn't move (state.csv).
    python qa\\stalls.py RUN_DIR [RUN_DIR ...]"""
import csv
import sys

for d in sys.argv[1:]:
    rows = list(csv.DictReader(open(d + '/state.csv')))
    out, st = [], None
    for r in rows:
        f, t = int(r['frame']), int(r['ms'])
        if st is None or f != st[0]:
            if st and t - st[1] > 2000:
                out.append((round(st[1] / 1000, 1), round((t - st[1]) / 1000, 1), st[0], st[2]))
            st = (f, t, r['screen'])
    print(d[-40:], 'rows', len(rows), 'stalls >2s (at s, for s, frame, screen):', out[:8])
