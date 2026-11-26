#!/usr/bin/env python3
"""The gameplay A/B's verdicts (modwhole, 2026-11-14; the user's decision "B now, C later").

A native function the random tester has not proved counts as GAMEPLAY-VERIFIED only if
  (1) it compiles natively under the same flags (it is in mod_native.json's "gameplay": mod_native.py release --whole), and
  (2) in native-on vs translated runs of the same exe it is logged as running ("[native] <name> (<address>) in use (gameplay
      tier)") and EVERY recorded scenario gives the same results both ways -- each scenario PASS both ways, check for check --
      and, once the replay tool is there, the same per-frame structure hashes over the recorded matches (replay_verdict);
  (3) one that never runs in any scenario stays translated (not counted) until a scenario covers it.
The proved tier (mod_native.json's "functions", both seeds) is counted apart, never mixed in.

    python whole_ab.py <ab_queue.log> <mod_native.json> [--tag T ...] [--proved <names file>] [--out <file.json>]
Reads ab_queue.log (ab_queue.ps1's lines) and each run's ab_<tag>_<mode>_<scenario>.txt beside it (qa.ps1's output, UTF-16).
Prints the verdicts; --out writes them as JSON (gameplay_verified, ran_but_unverified, never_ran, scenarios).

The replay hook (agent "replay", due 2026-11-15): NM_REPLAY_COMPARE = a command with {a} and {b} for the two runs' folders,
exit 0 = the same per-frame hashes, 1 = different, anything else = not comparable; without it, two runs that both have
frame_hashes.txt are compared line by line. No hashes: the verdict says "hashes pending" -- the QA tier only.
"""
import json, os, re, subprocess, sys

LINE = re.compile(r'^(\S+) (\S+) (\S+) (\S+) : (.*?) ; (\d+) s ; native in use: (\d+) \((.*?)\) ; (.*)$')


def read_text(path):
    raw = open(path, 'rb').read()
    for enc in ('utf-16', 'utf-8'):
        try:
            t = raw.decode(enc)
            if 'RESULT' in t or enc == 'utf-8':
                return t
        except UnicodeError:
            pass
    return raw.decode('latin-1')


def checks_of(path):
    """(RESULT, [each PASS / FAIL check line]) of one qa.ps1 output."""
    if not os.path.exists(path):
        return None, []
    t = read_text(path)
    res = next((l.strip() for l in reversed(t.splitlines()) if l.startswith('RESULT')), None)
    checks = [l.strip() for l in t.splitlines() if re.match(r'^(PASS|FAIL)\s', l)]
    return res, checks


# (2026-11-15) Not compared: "ram" (all of memory as the PC holds it: the native code's own stack slots differ by design) and "host"
# (a digest of each host export's index and v0 AFTER the call: a void export leaves v0 as its caller had it, which native and
# translated callers leave differently -- in the first hash A/B, versus, "host" differed at every other retrace while "host_calls",
# the pictures, the pad reads and all nine engine structures agreed at all 4,764). Proposed to agent "replay": hash v0 only for an
# export with a result.
SKIP_COLUMNS = ('ram', 'host')


def hash_rows(path):
    """{retrace: (structure hashes...)} of a replay's hashes.csv (agent "replay"'s nm_replay.cpp format: '#' header lines, a
    column line, then `retrace, pictures, pad_reads, host_calls, host, <one column per structure>, ram`), without the "ram"
    column -- all of memory as the PC holds it, a same-build check: the native code's own stack slots differ there by design."""
    cols, rows = None, {}
    for line in open(path, encoding='utf8'):
        line = line.rstrip('\n')
        if line.startswith('#'):
            continue
        v = line.split(',')
        if cols is None:
            cols = v
            continue
        if len(v) == len(cols):
            rows[int(v[0])] = tuple(x for c, x in zip(cols, v) if c not in SKIP_COLUMNS)
    return cols, rows


def replay_verdict(dir_a, dir_b):
    """'same' / 'different (...)' / 'pending' for two runs' replay hashes (ab_queue.ps1 -Replay: <dir>/hashes.csv): every retrace
    both have, every column but "ram". NM_REPLAY_COMPARE = a command with {a} and {b} (the two hashes.csv) instead: exit 0 the
    same, 1 different, else not comparable -- e.g. agent "replay"'s tools/replay_diff.py, which also names the member."""
    fa, fb = [os.path.join(d, 'hashes.csv') if d else '' for d in (dir_a, dir_b)]
    if not (fa and fb and os.path.exists(fa) and os.path.exists(fb)):
        return 'pending'
    cmd = os.environ.get('NM_REPLAY_COMPARE')
    if cmd:
        p = subprocess.run(cmd.format(a=fa, b=fb), shell=True, capture_output=True, text=True)
        return {0: 'same', 1: 'different'}.get(p.returncode, 'pending')
    ca, ra = hash_rows(fa)
    cb, rb = hash_rows(fb)
    if ca != cb or not ra or not rb:
        return 'pending'
    both = sorted(set(ra) & set(rb))
    bad = next((r for r in both if ra[r] != rb[r]), None)
    if bad is None:
        return 'same' if both else 'pending'
    cols = [c for c in ca if c not in SKIP_COLUMNS]
    which = [c for c, x, y in zip(cols, ra[bad], rb[bad]) if x != y]
    return 'different (retrace %d: %s)' % (bad, ', '.join(which[:4]))


NATIVE_MODE = 'native'


def main():
    log, man = sys.argv[1], json.load(open(sys.argv[2]))
    tags = [sys.argv[k + 1] for k in range(len(sys.argv) - 1) if sys.argv[k] == '--tag']
    out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else None
    # (--native-mode <mode>: the native side is the runs of that mode -- e.g. `a+b`, native with functions a and b off
    #  (ab_queue.ps1 -Modes a+b,translated), the gameplay tier without the ones a bisection found changing a scenario)
    global NATIVE_MODE
    NATIVE_MODE = sys.argv[sys.argv.index('--native-mode') + 1] if '--native-mode' in sys.argv else 'native'
    here = os.path.dirname(os.path.abspath(log))
    gameplay = dict(man.get('gameplay', {}))
    proved = dict(man.get('functions', {}))
    # (--proved <file>: the functions proved under both seeds by another run -- names modrep_nat_<A> or addresses -- when the A/B
    #  exe linked them in the gameplay tier: they are counted as PROVED, apart, never as gameplay-verified)
    if '--proved' in sys.argv:
        more = open(sys.argv[sys.argv.index('--proved') + 1]).read().split()
        for x in more:
            A = x[len('modrep_nat_'):] if x.startswith('modrep_nat_') else x
            if A in gameplay:
                proved[A] = gameplay.pop(A)
    runs = {}
    for l in open(log, encoding='utf8', errors='replace'):
        m = LINE.match(l.strip())
        if not m or (tags and m.group(2) not in tags):
            continue
        _, tag, mode, scn, res, secs, n, names, run = m.groups()
        res_full, checks = checks_of(os.path.join(here, 'ab_%s_%s_%s.txt' % (tag, mode, scn)))
        ran = [x for x in names.split() if x]
        runs[(scn, mode)] = {'tag': tag, 'result': res, 'checks': checks, 'run': run.strip(), 'seconds': int(secs),
                             'ran': ran}         # (the last run of a scenario in a mode counts)
    scenarios = {}
    for scn in sorted({s for s, _ in runs}):
        a, b = runs.get((scn, NATIVE_MODE)), runs.get((scn, 'translated'))
        if not a or not b:
            scenarios[scn] = {'verdict': 'incomplete', 'modes': sorted(m for s, m in runs if s == scn)}
            continue
        pa, pb = a['result'].startswith('RESULT: PASS'), b['result'].startswith('RESULT: PASS')
        # (a check's line, without what qa_tool.py adds about timing: the PASS/FAIL word and the check's own text)
        same_checks = [c.split('  [')[0] for c in a['checks']] == [c.split('  [')[0] for c in b['checks']]
        hd = os.path.join(here, 'ab_hashes')
        hashes = replay_verdict(os.path.join(hd, '%s_%s_%s' % (a['tag'], NATIVE_MODE, scn)), os.path.join(hd, '%s_translated_%s' % (b['tag'], scn)))
        v = 'same' if pa and pb and same_checks and not hashes.startswith('different') else 'DIFFERENT'
        if v == 'DIFFERENT' and not pa and not pb and same_checks and a['result'] == b['result']:
            # (a scenario that FAILs both ways, check for check the same -- broken on its own, not by the native code: it
            #  verifies nothing and is left out of the set, said apart; the functions it ran count from no other run of it)
            v = 'both-fail'
        scenarios[scn] = {'verdict': v, 'native': a['result'], 'translated': b['result'], 'checks_same': same_checks,
                          'hashes': hashes, 'native_run': a['run'], 'translated_run': b['run'],
                          'ran_gameplay': sorted({x.split('@')[1][:8] for x in a['ran'] if x.split('@')[1][:8] in gameplay}),
                          'ran_proved': sorted({x.split('@')[1][:8] for x in a['ran'] if x.split('@')[1][:8] in proved})}
    complete = [s for s, v in scenarios.items() if v['verdict'] not in ('incomplete', 'both-fail')]
    all_same = bool(complete) and all(scenarios[s]['verdict'] == 'same' for s in complete)
    ran_g = sorted({A for s in complete for A in scenarios[s]['ran_gameplay']})
    pending = any(scenarios[s]['hashes'] == 'pending' for s in complete)
    verified = ran_g if all_same else []
    size = lambda As: sum(gameplay[A].get('size', 0) for A in As if A in gameplay)
    result = {'all_scenarios_same': all_same, 'hashes': 'pending' if pending else 'compared',
              'gameplay_verified': {A: gameplay[A]['name'] for A in verified if A in gameplay},
              'ran_but_unverified': {A: gameplay[A]['name'] for A in ran_g if A in gameplay and A not in verified},
              'never_ran': {A: gameplay[A]['name'] for A in sorted(gameplay) if A not in ran_g},
              'proved_ran': sorted({A for s in complete for A in scenarios[s]['ran_proved']}),
              'scenarios': scenarios}
    print('scenarios: %d complete, %d the same both ways%s' % (len(complete), sum(scenarios[s]['verdict'] == 'same' for s in complete),
                                                            '' if all_same else ' -- NOT all: nothing is gameplay-verified until each differing one is explained'))
    for s in sorted(scenarios):
        v = scenarios[s]
        print('  %-26s %s' % (s, v['verdict'] if v['verdict'] == 'incomplete' else 'both-fail  (FAIL both ways, the same checks: left out)' if v['verdict'] == 'both-fail' else '%s  (native: %s | translated: %s | checks %s | hashes %s | gameplay-tier ran %d)' % (
            v['verdict'], v['native'][8:40], v['translated'][8:40], 'same' if v['checks_same'] else 'DIFFER', v['hashes'], len(v['ran_gameplay']))))
    psize = lambda As: sum(proved[A].get('size', 0) for A in As if A in proved)
    print('PROVED tier (counted apart): %d functions (%d instructions), %d of them ran in these scenarios (%d instructions)' % (
        len(proved), psize(proved), len(result['proved_ran']), psize(result['proved_ran'])))
    print('GAMEPLAY tier: %d functions linked; ran in a scenario: %d (%d instructions)' % (len(gameplay), len(ran_g), size(ran_g)))
    print('GAMEPLAY-VERIFIED%s: %d functions, %d instructions' % (' (QA tier; per-frame hashes pending)' if pending else '', len(verified), size(verified)))
    print('never ran (stay translated until a scenario covers them): %d functions, %d instructions' % (len(result['never_ran']), size(result['never_ran'])))
    if out:
        json.dump(result, open(out, 'w', newline='\n'), indent=1, sort_keys=True)


if __name__ == '__main__':
    main()
