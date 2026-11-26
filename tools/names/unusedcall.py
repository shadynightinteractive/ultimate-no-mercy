# (fablenames, 2026-11-14) Rule U: an m2c temp that appears only in its declaration and in one assignment
# `t = lift_call<T>(ctx, callee)(...)` (one cast allowed; a whole statement or a step of a comma expression) -- no read anywhere, not even a lift_out mirror -- holds a call's
# return value that the function never looks at (m2c keeps the v0 of every call it cannot prove void). It is named unused
# (_2, _3 in order of the code), so the reader need not look for where it goes. Raw C++. Writes unused_edits.json.
import re, collections
from rulelib import *
names, ch, merged, cands = load()
out = {}; stats = collections.Counter()
for f, info, raw in functions(merged, cands):
    vars_ = info.get('vars', {})
    wanted = []
    for t, rhs, pos in assignments(raw):
        if not re.fullmatch(r'%slift_call<[\w<> ]+>\(ctx, .*\)' % CAST, rhs):
            continue
        if t in vars_ or len(re.findall(r'\b%s\b' % re.escape(t), raw)) != 2:
            continue
        if not re.search(r'(?m)^\s+(?:const )?[\w<>*, ]+ \*?%s;$' % re.escape(t), raw):
            continue
        wanted.append((t, 'unused', pos))
    hand_out(f, info, raw, wanted, out, stats, continue_numbering=True)
save(out, stats, 'unused_edits.json')
