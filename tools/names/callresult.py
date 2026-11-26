# (fablenames, 2026-11-14) Rule CR, beside autocall.py: an m2c temp or var written only by (once, or the same text at every write: rulelib.value_writes)
# `t = lift_call<T>(ctx, ::callee)(...)` (one cast allowed; a whole statement or a step of a condition's comma expression,
# rulelib.assignments), and read at least once, holds what that call returned. autocall
# names such a temp after the callee only when the callee's name reads as a value (find_x -> x, angle_to); for a callee named by
# what it does (set_, start_, ai_set_plan ..) it gives nothing. Here the temp is named as autocall would name it when it gives a name, else <callee>_result, which says only that:
# the value `callee` returned. The callee must have a confirmed name (guess false); an OS function called by its own name
# (osVirtualToPhysical_recomp) gives os_virtual_to_physical_result. Calls through a pointer or a lookup are left.
# _2, _3 in order of the code; names over 40 characters and clashes are skipped. Raw C++. Writes callresult_edits.json.
import os, re, collections
from rulelib import *
names, ch, merged, cands = load()
# autocall.py's own reading of a callee's name (find_x -> x, its MAP: chance_in_1000 -> roll_won ..), so that a result reached
# only here (written inside a condition) gets the name autocall gives the same call written as a statement.
_src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'autocall.py'), encoding='utf8').read()   # (names083: beside this file; T is the outputs folder in tools/names)
_ns = {}
exec(_src[_src.index('GETTER = '):_src.index('out = {}')], _ns)
var_from = _ns['var_from']
out = {}; stats = collections.Counter()
def snake(n):
    n = re.sub(r'_recomp$', '', n).lstrip('_')
    return re.sub(r'(?<=[a-z0-9])(?=[A-Z])', '_', n).lower()
for f, info, raw in functions(merged, cands):
    vars_ = info.get('vars', {})
    wanted = []
    for t, (rhs, pos) in value_writes(raw).items():
        m = re.fullmatch(r'%slift_call<[\w<> ]+>\(ctx, ::(\w+)\)\((.*)\)' % CAST, rhs)
        if not m or t in vars_:
            continue
        callee, args = m.groups()
        depth = 0; ok = True
        for c in args:
            if c == '(': depth += 1
            elif c == ')':
                depth -= 1
                if depth < 0: ok = False; break
        if not ok or depth:
            continue
        if callee in merged:
            if merged[callee].get('guess'):
                stats['callee a guess'] += 1; continue
            cn = merged[callee]['name']
        elif re.match(r'_*os[A-Z]\w*_recomp$', callee):
            cn = snake(callee)
        else:
            stats['callee unnamed'] += 1; continue
        if not no_other_write(t, raw, pos) or read_count(t, raw) < 1:
            continue
        wanted.append((t, (var_from(cn) if callee in merged else None) or cn + '_result', pos[0]))
    hand_out(f, info, raw, wanted, out, stats, continue_numbering=True)
save(out, stats, 'callresult_edits.json')
