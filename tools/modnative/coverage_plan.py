#!/usr/bin/env python3
"""The gameplay tier's coverage (modwhole, 2026-11-14): which of the mod's functions no QA scenario ran, and which scenarios would
run the most of them.

    python coverage_plan.py <ab_queue.log> <mod_native.json> <inventory.json> <qa scenarios folder> [--tag T ...] [--out <json>]

1. RAN: every "[native] <name> (<address>) in use" of every run in ab_queue.log (any mode that ran native, any tag given).
2. NEVER RAN: the mod's functions (inventory.json: all of them, native or not) no run printed -- by file, biggest first. A function
   not native in that build can't print: it is listed apart ("not native in the build": its caller's runs say nothing of it).
3. MEASURED: scenarios run native-on for coverage only (a tag with only the native mode): the functions each ran that no A/B
   scenario did -- the gain it would bring to the A/B set, counted in instructions.
4. PROPOSED (not yet run): each other scenario of the qa folder, scored by the never-ran functions whose name's words are in the
   scenario's name or "about" text (a guess, to choose which to measure; only the measured gain counts).
"""
import collections, json, os, re, sys

LINE = re.compile(r'^(\S+) (\S+) (\S+) (\S+) : (.*?) ; (\d+) s ; native in use: (\d+) \((.*?)\) ; (.*)$')
STOP = {'the', 'a', 'of', 'on', 'qa', 'frame', 'pre', 'post', 'get', 'set', 'put', 'is', 'to', 'in', 'at', 'from', 'nm', 'plat',
        'arcade', 'mod', 'func', 'do', 'one', 'all', 'and', 'by', 'it', 'its', 'for', 'no', 'not', 'n', 'k', 'x', 'id'}


def main():
    log, man, inv, scdir = sys.argv[1:5]
    tags = [sys.argv[k + 1] for k in range(len(sys.argv) - 1) if sys.argv[k] == '--tag']
    out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else None
    man, inv = json.load(open(man)), json.load(open(inv))
    native = set(man.get('functions', {})) | set(man.get('gameplay', {}))
    runs = collections.defaultdict(set)       # (tag, scenario) -> addresses run native
    modes = collections.defaultdict(set)
    for l in open(log, encoding='utf8', errors='replace'):
        m = LINE.match(l.strip())
        if not m or (tags and m.group(2) not in tags):
            continue
        _, tag, mode, scn = m.group(1), m.group(2), m.group(3), m.group(4)
        modes[(tag, scn)].add(mode)
        if mode != 'translated':
            runs[(tag, scn)] |= {x.split('@')[1][:8] for x in m.group(8).split() if '@' in x}
    ab = {k for k in runs if 'translated' in modes[k]}
    ran_ab = set().union(*[runs[k] for k in ab]) if ab else set()
    measured = {k: runs[k] - ran_ab for k in runs if k not in ab}
    ran_any = set().union(*runs.values()) if runs else set()
    size = lambda As: sum(inv[A]['instructions'] for A in As if A in inv)
    never = {A: r for A, r in inv.items() if A not in ran_any}
    by_file = collections.defaultdict(list)
    for A, r in never.items():
        by_file[r['object']].append(A)
    print('the mod: %d functions, %d instructions' % (len(inv), size(inv)))
    print('ran native in an A/B scenario: %d functions, %d instructions' % (len(ran_ab), size(ran_ab)))
    print('ran native in any run (coverage runs too): %d functions, %d instructions' % (len(ran_any), size(ran_any)))
    print('never ran: %d functions, %d instructions (of them not native in the build: %d, %d instructions)' % (
        len(never), size(never), sum(1 for A in never if A not in native), size([A for A in never if A not in native])))
    for f, As in sorted(by_file.items(), key=lambda x: -size(x[1])):
        As.sort(key=lambda A: -inv[A]['instructions'])
        print('  %-16s %3d functions %6d instructions: %s' % (f, len(As), size(As), ', '.join('%s %d' % (inv[A]['name'], inv[A]['instructions']) for A in As[:8])))
    if measured:
        print('MEASURED coverage runs (native on): what each ran that no A/B scenario did')
        for (tag, scn), As in sorted(measured.items(), key=lambda x: -size(x[1])):
            print('  %-28s +%3d functions +%6d instructions (%s)' % (scn, len(As), size(As), ', '.join(sorted(inv[A]['name'] for A in As if A in inv)[:10])))
    # proposals: by the words of the never-ran functions' names
    words = collections.defaultdict(set)
    for A, r in never.items():
        for w in re.split(r'_+', r['name'].lower()):
            if len(w) > 2 and w not in STOP and not w.isdigit():
                words[w].add(A)
    tried = {s for _, s in runs}
    score = {}
    for f in os.listdir(scdir):
        if not f.endswith('.json') or f.startswith('_'):
            continue
        s = f[:-5]
        if s in tried:
            continue
        try:
            j = json.load(open(os.path.join(scdir, f), encoding='utf8'))
        except (ValueError, UnicodeError):
            continue
        text = (s + ' ' + str(j.get('about', '')) + ' ' + ' '.join(str(e.get('name', '')) for e in j.get('expect', []) if isinstance(e, dict))).lower()
        hit = set()
        for w, As in words.items():
            if re.search(r'\b%s' % re.escape(w), text):
                hit |= As
        if hit:
            score[s] = hit
    print('PROPOSED to measure (a guess from the names: the never-ran functions whose words the scenario names)')
    for s, As in sorted(score.items(), key=lambda x: -size(x[1]))[:25]:
        print('  %-30s %6d instructions (%s)' % (s, size(As), ', '.join(sorted(inv[A]['name'] for A in As)[:8])))
    if out:
        json.dump({'ran_ab': sorted(ran_ab), 'ran_any': sorted(ran_any), 'never': sorted(never),
                   'measured': {s: sorted(As) for (_, s), As in measured.items()},
                   'proposed': {s: sorted(As) for s, As in score.items()}}, open(out, 'w', newline='\n'), indent=1)


if __name__ == '__main__':
    main()
