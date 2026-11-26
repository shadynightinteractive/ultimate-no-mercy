# (names4, 2026-11-12) Condensed facts of a state / plan handler from the release's fielded + named C++: calls with
# their arguments, the animation files started, state changes, fields read with masks, phases compared / set.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os, collections
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift, pickle
names = json.load(open(BASE, encoding='utf8'))
if os.path.exists(CH): names.update(json.load(open(CH, encoding='utf8')))
cands = json.load(open(CANDS))
kinds, sources = lift.read_fields()
alln = lift.with_copy_names(names, cands)
PK = B + 'tools/fpk.pickle'
lift.FIELD_PARAMS.update(pickle.load(open(PK, 'rb'))[0]); lift.FIELD_RETURNS.update(pickle.load(open(PK, 'rb'))[1])
def nm(f):
    e = names.get(f); return (e['name'] + ('?' if e.get('guess') else '')) if e else f
def text(f):
    v = cands[f]
    fc = lift.fielded_cpp(f, v['cpp'], alln.get(f), kinds, sources)
    out = lift.named_cpp(f, dict(v, cpp=fc), alln.get(f))
    return re.sub(r'lift_call<[\w<> ]+>\(ctx, ::(func_\w+)\)', lambda m: nm(m.group(1)), out)
SKIPC = {'STEP_AS', 'WRESTLER', 'WRESTLER_AS', 'M2C_FIELD', 'frame', 'keep', 'AI_RECORD', 'AI_RECORD_AS', 'if', 'while', 'for', 'return', 'switch', 'AI', 'AI_AS', 'sizeof'}
def facts(f):
    t = text(f); body = t.split('{', 1)[1]
    assigns = collections.defaultdict(list)
    for m in re.finditer(r'\b(\w+) = ([^;\n]+);', body): assigns[m.group(1)].append(m.group(2))
    def res(a, d=0):
        a = a.strip()
        a = re.sub(r'^\(\w+\) ', '', a)
        if d < 3 and re.fullmatch(r'\w+', a) and a in assigns and not re.fullmatch(r'0x[0-9A-F]+|\d+', a):
            return '|'.join(sorted(set(res(x, d + 1) for x in assigns[a])))
        return a
    calls = []
    for m in re.finditer(r'\b([a-z_]\w*\??)\(([^;\n{}]*?)\);', body):
        c = m.group(1)
        if c in SKIPC: continue
        args = [res(x) for x in re.split(r',\s*', m.group(2))] if m.group(2) else []
        args = [re.sub(r'STEP_AS\(step, (\w+), \w+\)', r'\1', a) for a in args]
        calls.append('%s(%s)' % (c, ', '.join(a if len(a) < 40 else a[:37] + '..' for a in args)))
    seen = {c.split('(')[0] for c in calls}
    for m in re.finditer(r'([a-z_]\w*\??)\(', body):
        if m.group(1) not in SKIPC and m.group(1) not in seen and m.group(1) not in ('ai_set_plan',): calls.append(m.group(1) + '(..)'); seen.add(m.group(1))
    cc = collections.Counter(calls)
    flds = collections.Counter()
    for m in re.finditer(r'(STEP_AS|STEP|WRESTLER|WRESTLER_AS|AI_RECORD|AI_RECORD_AS)\((\w+), (\w+)', body): flds[m.group(1).split('_')[0] + '.' + m.group(3)] += 1
    for m in re.finditer(r'M2C_FIELD\((\w+), [^,]+, (0x[0-9A-F]+|\d+)\)', body): flds['%s+%s' % (m.group(1), m.group(2))] += 1
    masks = sorted(set(re.findall(r'& (0x[0-9A-F]+|\d+)', body)), key=lambda x: int(x, 0))
    cmps = collections.defaultdict(set)
    for m in re.finditer(r'\b(\w+) ([!=<>]=?) (-?0x[0-9A-F]+|-?\d+)\b', body):
        cmps[m.group(1)].add(m.group(2) + m.group(3))
    sets = collections.Counter()
    for m in re.finditer(r'((?:WRESTLER|AI_RECORD|STEP)(?:_AS)?\(\w+, (\w+)(?:, [\w<> ]+)?\)|M2C_FIELD\(\w+, [^,]+, \w+\)) = ([^;]+);', body):
        sets['%s=%s' % (m.group(2) or m.group(1), res(m.group(3))[:40])] += 1
    return t, cc, flds, masks, cmps, sets
if __name__ == '__main__':
    for f in sys.argv[1:]:
        t, cc, flds, masks, cmps, sets = facts(f)
        e = names[f]
        print('=' * 50); print(f, e['name'], cands[f].get('size'))
        print('CALLS:', '; '.join(('%s x%d' % (k, v) if v > 1 else k) for k, v in cc.items()))
        print('FIELDS:', ' '.join(('%s x%d' % (k, v) if v > 1 else k) for k, v in flds.most_common()))
        print('SETS:', '; '.join(('%s x%d' % (k, v) if v > 1 else k) for k, v in sets.items()))
        print('MASKS:', ' '.join(masks))
        print('CMPS:', '; '.join('%s %s' % (k, ','.join(sorted(v))) for k, v in cmps.items() if not k.startswith(('temp_v0', 'lift_'))))
