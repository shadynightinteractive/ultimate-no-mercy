#!/usr/bin/env python3
"""m5_analyse.py <candidates.json> <report tag> [<report tag> ...]: a run's verdicts (modnative5, 2026-11-13; after modnative4's
analyse_run.py). Each native candidate's line from the reports (a later tag's line replaces an earlier one's), judged at the full
bar -- no difference, every instruction reached, 200,000 plain tests -- the instructions it stands for (its own, and those of the
internal functions run through it, rule (5)), the negative controls, and the rest by their first difference.
Writes proved_<last tag>.txt (the proved candidates) and verdicts_<last tag>.json."""
import json, os, sys, collections

HERE = os.path.dirname(os.path.abspath(__file__))
REPORTS = os.path.join(HERE, '..', 'lift', 'fuzz')


def verdict(f):
    if len(f) < 8:
        return 'DIFFERENT' if len(f) > 3 and f[3] != '0' else 'no line: ' + ' '.join(f[1:])
    diffs, mask, n, plain = int(f[3]), int(f[4], 16), int(f[5]), int(f[7])
    if diffs:
        return 'DIFFERENT'
    if bin(mask).count('1') == n and plain >= 200000:
        return 'PROVED'
    return 'short: reached %d/%d, %d plain' % (bin(mask).count('1'), n, plain)


def main():
    cands = json.load(open(sys.argv[1]))
    inv = json.load(open(os.path.join(HERE, os.environ.get('NM_INVENTORY', 'inventory.json'))))
    lines = {}
    for tag in sys.argv[2:]:
        p = os.path.join(REPORTS, 'report_%s.txt' % tag)
        for l in open(p):
            f = l.split()
            if f:
                lines[f[0]] = f
    tag = sys.argv[-1]
    nat = sorted(n for n in lines if n.startswith('modrep_nat_'))
    out, proved, funcs = {}, [], set()
    for n in nat:
        v = verdict(lines[n])
        out[n] = {'verdict': v, 'line': ' '.join(lines[n][1:])}
        if v == 'PROVED':
            proved.append(n)
            a = n[len('modrep_nat_'):]
            funcs.add(a)
            for t in cands.get(n, {}).get('native', {}).get('thru', []):
                funcs.add(t[len('modrep_natthru_'):])
    ins = sum(int(inv[a]['instructions']) for a in funcs)
    print('run %s: %d candidates with a line; PROVED %d candidates = %d of the mod\'s functions (with the %d run through callers), %d instructions of %d' % (
        ','.join(sys.argv[2:]), len(nat), len(proved), len(funcs), len(funcs) - len(proved), ins, sum(int(r['instructions']) for r in inv.values())))
    by = collections.Counter()
    for n in nat:
        v = out[n]['verdict']
        if v != 'PROVED':
            by[v.split(':')[0]] += 1
    print('not proved:', dict(by))
    for n in nat:
        if out[n]['verdict'] != 'PROVED':
            a = n[len('modrep_nat_'):]
            print('  %s %-28s %5d  %s  %s' % (a, inv[a]['name'], int(inv[a]['instructions']), out[n]['verdict'], lines[n][6] if len(lines[n]) > 6 else ''))
    negs = sorted((n for n in lines if n.startswith('modrep_natneg')), key=lambda n: int(n[13:].split('_')[0]))
    print('negative controls: %d, DIFFERENT: %d' % (len(negs), sum(1 for n in negs if verdict(lines[n]) == 'DIFFERENT')))
    for n in negs:
        print('  %-28s %s %s of %s %s' % (n, verdict(lines[n]), lines[n][3], lines[n][1], lines[n][6] if len(lines[n]) > 6 else ''))
    open(os.path.join(HERE, 'proved_%s.txt' % tag), 'w').write(' '.join(proved))
    json.dump(out, open(os.path.join(HERE, 'verdicts_%s.json' % tag), 'w'), indent=0)


if __name__ == '__main__':
    main()
