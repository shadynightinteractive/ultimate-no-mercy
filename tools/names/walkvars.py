# (names5, 2026-11-13) Rule W: an m2c var that walks a table -- written by one plain `v = INIT;` and otherwise only stepped by one
# constant (`v += C;`, C > 1, in a loop), and read only as a place (`M2C_FIELD(v, ..)`, `*v`, or inside a sum `(v + ...)` /
# `(... + v)`) or in the loop's compare -- holds where the walk is: with INIT 0 it is a byte offset (named offset), else a place
# (named <p>_at when INIT is a named local or parameter p, record_at otherwise); _2, _3 in order of the code. lift_out_<reg>
# mirrors don't count. Names that clash with any identifier of the function are skipped. Raw C++. Writes walk_edits.json.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, collections, sys
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; raw = re.sub(r'//.*', '', cands[f]['cpp'])
    vars_ = dict(info.get('vars', {}))
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw))
    used = set(vars_.values()) | {info['name']}
    lines = raw.split('\n')
    wanted = []
    for v in sorted(set(re.findall(r'\bvar_\w+\b', raw)) - set(vars_)):
        inits, steps, ok = [], set(), True
        for i, l in enumerate(lines):
            s = l.strip()
            if not re.search(r'(?<![\w.>])%s\b' % re.escape(v), s): continue
            if re.match(r'(?:const )?[\w<>* ]+ %s;$' % re.escape(v), s): continue
            m = re.match(r'%s = ([^;]*);$' % re.escape(v), s)
            if m and v not in m.group(1): inits.append((i, m.group(1))); continue
            m = re.match(r'%s \+= (0x[0-9A-F]+|\d+);$' % re.escape(v), s)
            if m: steps.add(int(m.group(1), 0)); continue
            if re.match(r'lift_out_\w+ = (?:\([\w<> ]+\) )?%s;$' % re.escape(v), s): continue
            for mm in re.finditer(r'(?<![\w.>])%s\b' % re.escape(v), s):
                before = s[:mm.start()].rstrip(); after = s[mm.end():].lstrip()
                if re.search(r'M2C_FIELD\($', before) and after.startswith(','): continue
                if before.endswith('*') and not re.search(r'[\w)\]] ?\*$', before): continue   # *v (not a product)
                if after.startswith('+ ') or before.endswith('+'): continue
                if re.match(r'(?:!=|==|<|>|<=|>=) ', after) or re.search(r'(?:!=|==|<|>|<=|>=)$', before): continue
                ok = False; break
            if not ok: break
        if not ok or len(inits) != 1 or len(steps) != 1 or min(steps) < 2:
            stats['no'] += 1; continue
        init = re.sub(r'^\((?:P<[^)]*>|\w+) ?\)', '', inits[0][1]).strip()
        if init == '0': cand = 'offset'
        elif re.fullmatch(r'[a-z]\w*', init) and not M2C.match(init): cand = re.sub(r'_\d+$', '', init) + '_at'
        elif re.fullmatch(r'\w+', init) and init in vars_ and not M2C.match(vars_[init]): cand = re.sub(r'_\d+$', '', vars_[init]) + '_at'
        else: cand = 'entry_at' if min(steps) <= 4 else 'record_at'   # (2- and 4-byte steps walk table entries)
        wanted.append((v, cand, inits[0][0]))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in ids or nm in used or len(nm) > 40:
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/walk_edits.json', 'w'), indent=0)
print(stats, len(out))
c = collections.Counter(v.rstrip('_0123456789') for d in out.values() for v in d.values())
print(c.most_common(30))
