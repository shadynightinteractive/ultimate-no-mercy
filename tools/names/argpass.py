# (fablenames, 2026-11-14) Rule A4, the parameters' side of rules A / A2 / A3 (autoarg.py, autoarg2.py, argvars.py): an unnamed
# parameter argN that the function never writes and never takes the address of, and whose every read -- the ABI's
# `lift_out_<reg> = argN;` mirrors left aside -- is a whole argument (a cast allowed) of calls to confirmed (guess false)
# functions, each at a parameter with the same name there, is only handed on as that: it takes that name (the callee's own
# _2.. counter dropped; _2, _3 here in order of the code). At least one such call. Raw C++ with the entries' names; clashes with
# any identifier of the function, keywords and m2c-looking names are skipped. Writes argpass_edits.json.
import re, collections
from rulelib import *
names, ch, merged, cands = load()
CALL = re.compile(r'lift_call<[\w<> ]+>\(ctx, ::((?:func|rw)_\w+)\)\(')
def split_args(s, start):
    depth, cur, args, i = 0, '', [], start
    while i < len(s):
        c = s[i]
        if c in '([': depth += 1
        elif c in ')]':
            if depth == 0:
                args.append(cur.strip()); return args
            depth -= 1
        elif c == ',' and depth == 0:
            args.append(cur.strip()); cur = ''; i += 1; continue
        cur += c; i += 1
    return args
out = {}; stats = collections.Counter()
for f, info, raw in functions(merged, cands):
    vars_ = info.get('vars', {})
    head, _, body = raw.partition('{')
    params = [p for p in re.findall(r'\b(arg\d+)\b', head) if p not in vars_]
    if not params:
        continue
    uses = collections.defaultdict(list)
    for m in CALL.finditer(body):
        e = merged.get(m.group(1))
        for k, a in enumerate(split_args(body, m.end())):
            mm = re.fullmatch(CAST + r'(arg\d+)', a)
            if mm:
                p = e['vars'].get('arg%d' % k) if e and not e.get('guess') else None
                uses[mm.group(1)].append(p)
    wanted = []
    for a in params:
        ps = uses.get(a)
        if not ps or None in ps or len(set(re.sub(r'_\d+$', '', p) for p in ps)) != 1:
            continue
        if write_count(a, body) != 0 or read_count(a, body) != len(ps):
            stats['other use'] += 1; continue
        wanted.append((a, re.sub(r'_\d+$', '', ps[0]), int(a[3:])))
    hand_out(f, info, raw, wanted, out, stats)
save(out, stats, 'argpass_edits.json')
