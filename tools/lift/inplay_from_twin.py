#!/usr/bin/env python3
"""The in-play twin check's results, as the data lift.py's notes are written from (2026-11-18, agent twinnotes).

The twin build (lift_twin.cpp, made by `lift.py twin` and the caller-twin harness) runs the game with each converted function
compared against its translated original in real play. Each run leaves, beside its QA report:
  twin_report.txt               name calls checked different reached_mask instructions first_difference   (one line a function)
  twin_report.txt.callers.txt   name given_up held_up yielded waited waited_different raw_only upper_only (caller twin only)
  twin_report.txt.untracked.txt name checks_given_up_for_an_untracked_writer first_page
  twin_report.txt.run.txt       lockstep 1 / foreign_messages 0  (a run counts only with both)
"checked" is the number of COMPLETED checks: a check given up (caps, the time limit, an untracked writer), held up or yielded
to another check never reaches it. The functions are of three kinds, from the twin build's sets file (make_twin2.py's
twin_sets json): "callers" (the caller twin: the original runs for real, the converted body on the side with each call
matched and the callee's recorded effects replayed), "twin" (the leaf twin: functions that call nothing, both run) and
"counted" (counted only, never compared).

This writes one JSON file, committed with the project as the evidence the notes rest on (no game data in it: function names,
counts, scenario names and hashes):
  {"set": ..., "build": "0.84", "twin_exe_md5": ..., "lifted_md5": ..., "runs": [...], "converted_functions": 3147,
   "functions": {name: {"twin": "caller" | "leaf", "n": completed checks, "scenarios": [...], "untracked": M,
                        "raw_only": K, "waited": W, "body": the function's 0.84 body hash}}}
Only the functions with at least one completed check and no difference are in "functions"; game_main never is (its one
check a run is at start-up and is always given up: the independent re-check's rule). "body" is lift.py's inplay_body_hash
of the function's candidates.json entry -- the converted C++ and how it is called -- so a note is written only while the
function is converted exactly as it was when it was compared (lift.py drops it otherwise).

Which runs count: the scenario runs, never the controls (a control run's name has "_c" and a digit, or ends in _poke: their
results are poked on purpose); each must be a lockstep run with no message from outside the game's threads, not marked
INVALID, not killed, exit 0 (the review's conditions). Any run short of that stops this script: nothing is written. Each run's
QA verdict is recorded beside (qa_results) but is not a condition: see qa_result.

    python tools/lift/inplay_from_twin.py --runs <the twin's runs folder> --pattern "tw_*_ct11" --sets <twin_sets json>
        --lifted <the lifted.cpp the twin build was made from> --candidates <that release's candidates.json>
        --exe-md5 <the twin exe's md5> --set caller_twin_v11 --build 0.84 --out tools/lift/inplay/caller_twin_v11_084.json
        --label "tw_modes_reg_rumble_ct11=modes_reg_rumble (match not reached)"
(That run's QA report is FAIL: the Rumble match never started in its time under the slow twin build. Its checks are kept --
real comparisons of the functions that ran, meeting the review's conditions -- with a label that does not suggest a Rumble.)
(The runs and the sets file are the twin's own outputs, kept outside the repository; this file records what was read.)
"""
import argparse
import glob
import hashlib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lift      # (inplay_body_hash: the same hash lift.py checks at each release)

# A control run (poked on purpose): its name has "_c" + a digit (+ a letter), or ends in _poke -- the twin's ct_summary.py rule.
CONTROL = re.compile(r'_c\d+[a-z]?(_|$)|_poke$')
# The function whose one check a run is the start-up's, always given up: never a note (the independent re-check, section 4).
NEVER = {'game_main'}


def md5(path):
    return hashlib.md5(open(path, 'rb').read()).hexdigest()


def columns(path, width):
    """name -> its numbers, from a twin side file (every line: a name and `width` whole numbers)."""
    out = {}
    if os.path.exists(path):
        for line in open(path):
            f = line.split()
            if f:
                out[f[0]] = [int(x) for x in f[1:width + 1]] + [0] * (width + 1 - len(f))
    return out


def check_run(folder):
    """Why a run can't count, or None. (lockstep, foreign messages, INVALID, killed / exit code)"""
    report = os.path.join(folder, 'twin_report.txt')
    if not os.path.exists(report):
        return 'no twin_report.txt'
    run = {}
    if os.path.exists(report + '.run.txt'):
        run = {l.split()[0]: int(l.split()[1]) for l in open(report + '.run.txt') if len(l.split()) == 2}
    if run.get('lockstep') != 1 or run.get('foreign_messages') != 0:
        return 'not lockstep, or messages from outside the game (%s)' % run
    if os.path.exists(os.path.join(folder, 'INVALID.txt')):
        return 'marked INVALID'
    info = json.load(open(os.path.join(folder, 'run.json'))) if os.path.exists(os.path.join(folder, 'run.json')) else {}
    if info.get('killed') or info.get('exit_code', 0) != 0:
        return 'killed or a non-zero exit'
    return None


def qa_result(folder):
    """The run's QA verdict (its report.txt's RESULT line): recorded, not a condition. The twin's comparisons are valid for what
    ran whatever the scenario's own expectations found (a twin run is slow: a scenario can run out of its time before the part it
    checks); the verdict says how far the scenario got."""
    qa = os.path.join(folder, 'report.txt')
    found = [l.split(':', 1)[1].strip() for l in open(qa, encoding='utf8', errors='replace') if l.startswith('RESULT:')] if os.path.exists(qa) else []
    return found[-1] if found else 'none'


def scenario_of(run_name, pattern):
    """tw_smoke_singles_ct11 -> smoke_singles (the pattern's fixed head and tail taken off)."""
    head, _, tail = pattern.partition('*')
    return run_name[len(head):len(run_name) - len(tail)] if tail else run_name[len(head):]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--runs', required=True)
    ap.add_argument('--pattern', required=True)
    ap.add_argument('--sets', required=True)
    ap.add_argument('--lifted', required=True)
    ap.add_argument('--candidates', required=True)
    ap.add_argument('--exe-md5', required=True)
    ap.add_argument('--set', required=True)
    ap.add_argument('--build', required=True)
    ap.add_argument('--out', required=True)
    # (a run whose scenario did not get to the part its name says, e.g. --label "tw_modes_reg_rumble_ct11=modes_reg_rumble (match
    #  not reached)": the name the notes give its scenario. Its checks still count; the label keeps the note from suggesting more.)
    ap.add_argument('--label', action='append', default=[])
    args = ap.parse_args()
    labels = dict(x.split('=', 1) for x in args.label)
    name_of = lambda folder: labels.get(os.path.basename(folder), scenario_of(os.path.basename(folder), args.pattern))

    sets = json.load(open(args.sets))
    callers, leaves = set(sets['callers']), set(sets['twin'])
    # (The twin build must have been made from this lifted.cpp, and the hashes from the candidates of that same release.)
    if md5(args.lifted) != sets['live_md5']:
        raise SystemExit('the lifted.cpp given (%s) is not the one the twin build was made from (%s)' % (md5(args.lifted), sets['live_md5']))
    cands = json.load(open(args.candidates))
    released = set(re.findall(r'^extern "C" (?:__attribute__\(\(noinline\)\) )?void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{',
                              open(args.lifted, encoding='utf8').read(), re.M))
    if released != callers | leaves | set(sets['counted']):
        raise SystemExit("the twin build's sets are not the lifted.cpp given's functions")

    folders = sorted(f for f in glob.glob(os.path.join(args.runs, args.pattern)) if os.path.isdir(f))
    runs = [f for f in folders if not CONTROL.search(os.path.basename(f))]
    if not runs:
        raise SystemExit('no scenario runs match %s' % args.pattern)
    bad = [(os.path.basename(f), check_run(f)) for f in runs]
    bad = [(n, why) for n, why in bad if why]
    if bad:
        raise SystemExit('runs that cannot count -- nothing written:\n' + '\n'.join('  %s: %s' % b for b in bad))

    total = {}
    for folder in runs:
        scenario = name_of(folder)
        report = os.path.join(folder, 'twin_report.txt')
        extra = columns(report + '.callers.txt', 7)      # given_up held_up yielded waited waited_different raw_only upper_only
        untracked = columns(report + '.untracked.txt', 1)  # checks given up (its third column, the first page, not read)
        for line in open(report):
            f = line.split()
            if len(f) < 7:
                continue
            name, checked, different = f[0], int(f[2]), int(f[3])
            s = total.setdefault(name, {'n': 0, 'different': 0, 'scenarios': [], 'untracked': 0, 'raw_only': 0, 'waited': 0, 'upper_only': 0})
            s['n'] += checked
            s['different'] += different
            if checked:
                s['scenarios'].append(scenario)
            e = extra.get(name, [0] * 7)
            s['waited'] += e[3]
            s['raw_only'] += e[5]
            s['upper_only'] += e[6]
            s['untracked'] += untracked.get(name, [0])[0]

    differing = sorted(n for n, s in total.items() if s['different'])
    functions = {}
    for name in sorted(total):
        s = total[name]
        if name in NEVER or not s['n'] or s['different'] or name not in callers | leaves:
            continue
        kind = 'caller' if name in callers else 'leaf'
        entry = {'twin': kind, 'n': s['n'], 'scenarios': s['scenarios'], 'body': lift.inplay_body_hash(cands[name])}
        if kind == 'caller':
            # (the suffixes the note adds where they apply: the independent re-check, section 5)
            entry.update({k: s[k] for k in ('untracked', 'raw_only', 'waited') if s[k]})
            if s['upper_only']:
                raise SystemExit('%s: upper-word-only mismatches -- the approved wording has no suffix for those' % name)
        functions[name] = entry

    out = {'about': 'In-play twin results the notes in lifted.cpp are written from (tools/lift/inplay_from_twin.py; lift.py inplay_notes). '
                    'A note is written only while the function\'s body hash ("body", lift.py inplay_body_hash) is the same.',
           'set': args.set, 'build': args.build, 'twin_exe_md5': args.exe_md5.lower(), 'lifted_md5': sets['live_md5'],
           'runs': [os.path.basename(f) for f in runs],
           'qa_results': {os.path.basename(f): qa_result(f) for f in runs},
           'scenarios': [name_of(f) for f in runs],
           'converted_functions': len(released),
           'callers_in_the_twin': len(callers), 'leaves_in_the_twin': len(leaves), 'counted_only': len(sets['counted']),
           'differing_functions': differing,
           'functions': functions}
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    # (one function a line, so a later run's changes read as a diff)
    head = json.dumps({k: v for k, v in out.items() if k != 'functions'}, indent=1)
    body = ',\n'.join('  %s: %s' % (json.dumps(n), json.dumps(e)) for n, e in functions.items())
    with open(args.out, 'w', newline='\n') as f:
        f.write(head[:-2] + ',\n "functions": {\n' + body + '\n }\n}\n')
    if json.load(open(args.out)) != out:
        raise SystemExit('the file written does not read back as written')
    c = [e for e in functions.values() if e['twin'] == 'caller']
    l = [e for e in functions.values() if e['twin'] == 'leaf']
    print('%d of %d converted functions compared in play (caller twin %d, leaf twin %d), %d differing; %d caller checks in %d scenarios -> %s' % (
        len(functions), len(released), len(c), len(l), len(differing), sum(e['n'] for e in c), len(runs), args.out))
    for k in ('untracked', 'raw_only', 'waited'):
        print('  with %s: %d functions' % (k, sum(1 for e in c if e.get(k))))


if __name__ == '__main__':
    main()
