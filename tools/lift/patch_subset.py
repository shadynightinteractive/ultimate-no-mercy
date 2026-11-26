"""lift.py: LIFT_SUBSET=<file of function names> -- candidates and fuzz for just those functions, merged into the
existing candidates.json and fuzz report (everything else kept as it is). For working on a few functions (the
camera's, 2026-11-06) in minutes, not the whole multi-hour run."""
import os

p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lift.py')
s = open(p, encoding='utf-8').read()


def sub(a, b):
    global s
    assert s.count(a) == 1, a[:70]
    s = s.replace(a, b)


sub("TEST_WORKERS = max(1, int(os.environ.get('LIFT_WORKERS', '6')))",
    "TEST_WORKERS = max(1, int(os.environ.get('LIFT_WORKERS', '6')))\n"
    "# LIFT_SUBSET=<a file of function names>: candidates and fuzz for those only, merged into what's there (2026-11-06)\n"
    "def subset_names():\n"
    "    path = os.environ.get('LIFT_SUBSET')\n"
    "    return set(open(path).read().split()) if path else None")
sub("""    plain = []
    for name, v in index.items():
        if v['size'] == 0:
            continue""", """    plain = []
    only = subset_names()
    for name, v in index.items():
        if v['size'] == 0 or (only is not None and name not in only):
            continue""")
sub("""    json.dump(results, open(CANDIDATES, 'w'), indent=0)
    total = sum(v['size'] for v in index.values())""", """    if only is not None and os.path.exists(CANDIDATES):   # (a subset: merged into the full list)
        merged = json.load(open(CANDIDATES))
        merged.update(results)
        results = merged
    json.dump(results, open(CANDIDATES, 'w'), indent=0)
    total = sum(v['size'] for v in index.values())""")
sub("""def run_fuzz(tests):
    cands = json.load(open(CANDIDATES))
    names = sorted(n for n, v in cands.items() if 'cpp' in v)""", """def run_fuzz(tests):
    cands = json.load(open(CANDIDATES))
    names = sorted(n for n, v in cands.items() if 'cpp' in v)
    only = subset_names()
    if only is not None:
        names = [n for n in names if n in only]""")
sub("""    open(FUZZ_REPORT, 'w').write(''.join(lines))
    print('tested %d functions""", """    if only is not None and os.path.exists(FUZZ_REPORT):   # (a subset: its lines replace theirs, the rest kept)
        kept = [l for l in open(FUZZ_REPORT).readlines() if l.split() and l.split()[0] not in only]
        lines = kept + lines
    open(FUZZ_REPORT, 'w').write(''.join(lines))
    print('tested %d functions""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print('applied')
