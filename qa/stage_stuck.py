#!/usr/bin/env python3
"""Stuck and shaking CPU wrestlers in a QA run's state.csv (stages agent, 2026-11-20). A measure, not a pass condition:
it reads a run's rows and counts, for each CPU-controlled slot of the first two, the time he spends free to walk (his state
1 stand, 2 walk, 3 run) outside the ring, away from his man (more than NEAR apart), and making no progress.

  python qa/stage_stuck.py <run folder> [<run folder> ...] [--json out.json]

A WINDOW is WIN match frames (the match clock: match_frames). Each row starts one: the rows within WIN after it, all of
them free / outside / away, the window counted when it spans at least 0.9 WIN. In a counted window:
  - net   = how far he got from its first row to its last;
  - path  = the sum of his moves row to row (frame-to-frame jitter adds up here, standing still does not);
  - flips = how many times his direction of travel turned round (the dot product of two moves in a row < 0).
  STILL   : net < NET_MIN and path < PATH_SHAKE        (standing where he wants to walk)
  SHAKING : net < NET_MIN and path >= PATH_SHAKE        (moving to and fro and getting nowhere: the user's "shake")
Stuck frames = the match frames covered by STILL or SHAKING windows (a frame counted once). Also: the longest stuck
stretch, the episodes (stuck stretches with gaps < WIN between them joined), where they were (x, z), and how far up the
stage he got (max z). The CPU slots: s<k>_pad == 0 (no controller)."""
import csv, json, math, sys

WIN = 90            # match frames (3 s of the 30-a-second match clock)
NET_MIN = 30.0      # world units: less than this net in WIN is no progress
PATH_SHAKE = 60.0   # world units of path in WIN with no progress: shaking
NEAR = 160.0        # men closer than this are fighting, not walking
RING = 300.0        # |x| and |z| under this: the ring and its apron


def num(v):
    try:
        return int(v, 16) if isinstance(v, str) and v.startswith('0x') else float(v)
    except Exception:
        return None


def rows_of(path):
    out = []
    with open(path, newline='', errors='replace') as f:
        for r in csv.DictReader(f):
            mf = num(r.get('match_frames'))
            if num(r.get('stage')) != 2 or mf is None or mf in (0xFFFF, 0) or num(r.get('mode')) not in (-6, -2):
                continue
            out.append(r)
    return out


def scan(run):
    rows = rows_of(run + '/state.csv')
    res = {'run': run.replace('\\', '/').rstrip('/').split('/')[-1], 'rows': len(rows), 'slots': {}}
    if not rows:
        return res
    # the match clock: rows of one match (a new match restarts match_frames: split there)
    for k in (0, 1):
        o = 1 - k
        if num(rows[0].get('s%d_pad' % k)) != 0:
            continue
        pts = []
        for r in rows:
            x, z, ph = num(r['s%d_x' % k]), num(r['s%d_z' % k]), num(r['s%d_phase' % k])
            ox, oz = num(r['s%d_x' % o]), num(r['s%d_z' % o])
            mf = int(num(r['match_frames']))
            if None in (x, z, ph, ox, oz) or not all(map(math.isfinite, (x, z, ox, oz))):
                continue
            free = int(ph) in (1, 2, 3) and not (abs(x) < RING and abs(z) < RING) and math.hypot(x - ox, z - oz) > NEAR
            g = int(num(r.get('bs_grabs')) or 0)
            pts.append((mf, x, z, free, int(ph), (g >> (8 * k)) & 0xFF, (g >> (16 + k)) & 1))
        plans = {}
        stuck = set(); shaking = set(); still = set(); wins = 0; shake_w = 0; still_w = 0; where = []
        n = len(pts)
        j = 0
        for i in range(n):
            if not pts[i][3]:
                continue
            if j < i: j = i
            while j + 1 < n and pts[j + 1][0] - pts[i][0] <= WIN and pts[j + 1][0] >= pts[i][0]:
                j += 1
            seg = pts[i:j + 1]
            if len(seg) < 4 or seg[-1][0] - seg[0][0] < 0.9 * WIN or not all(p[3] for p in seg):
                continue
            wins += 1
            net = math.hypot(seg[-1][1] - seg[0][1], seg[-1][2] - seg[0][2])
            path = 0.0; flips = 0; prev = None
            for a, b in zip(seg, seg[1:]):
                dx, dz = b[1] - a[1], b[2] - a[2]
                path += math.hypot(dx, dz)
                if abs(dx) + abs(dz) > 1.0:
                    if prev and prev[0] * dx + prev[1] * dz < 0: flips += 1
                    prev = (dx, dz)
            if net < NET_MIN:
                frames = set(range(seg[0][0], seg[-1][0] + 1))
                stuck |= frames
                if path >= PATH_SHAKE:
                    shake_w += 1; shaking |= frames
                else:
                    still_w += 1; still |= frames
                where.append((seg[0][0], round(seg[0][1]), round(seg[0][2]), round(net), round(path), flips, seg[0][5], seg[0][6]))
                for p in seg: plans[p[5]] = plans.get(p[5], 0) + 1
        # episodes: stuck frames with gaps under WIN joined
        eps = []
        for f in sorted(stuck):
            if eps and f - eps[-1][1] <= WIN: eps[-1][1] = f
            else: eps.append([f, f])
        free_frames = 0
        for a, b in zip(pts, pts[1:]):
            if a[3] and b[3] and 0 <= b[0] - a[0] <= 10: free_frames += b[0] - a[0]
        res['slots'][k] = {
            'free_s': round(free_frames / 30.0, 1), 'windows': wins, 'still_windows': still_w, 'shaking_windows': shake_w,
            'stuck_s': round(len(stuck) / 30.0, 1), 'shaking_s': round(len(shaking) / 30.0, 1), 'still_s': round(len(still) / 30.0, 1),
            'episodes': len(eps), 'longest_s': round(max([b - a for a, b in eps] or [0]) / 30.0, 1),
            'max_z': round(max([p[2] for p in pts] or [0])),
            'where': [(a, b) for a, b in eps][:20],
            'examples': where[:12],
            'plans': sorted(plans.items(), key=lambda kv: -kv[1])[:6],   # (mode 256's bs_grabs: the CPU's plan in the stuck windows' rows)
        }
    return res


def main(argv):
    out = None
    if '--json' in argv:
        i = argv.index('--json'); out = argv[i + 1]; argv = argv[:i] + argv[i + 2:]
    allr = []
    for run in argv:
        r = scan(run)
        allr.append(r)
        line = '%-48s rows %5d' % (r['run'], r['rows'])
        for k, s in sorted(r['slots'].items()):
            line += ' | s%d free %5.1fs stuck %5.1fs (shaking %5.1fs, still %5.1fs) episodes %2d longest %4.1fs max_z %5d' % (
                k, s['free_s'], s['stuck_s'], s['shaking_s'], s['still_s'], s['episodes'], s['longest_s'], s['max_z'])
        print(line)
    if out:
        json.dump(allr, open(out, 'w'), indent=1)


if __name__ == '__main__':
    main(sys.argv[1:])
