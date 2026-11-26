# (names5, 2026-11-13) Rule A3, beside names2's rule A and names4's A2: an m2c temp written once whose every read -- the ABI's
# `lift_out_<reg> = t;` mirrors left aside, which rules A / A2 counted as reads -- is a whole argument (a cast allowed) of calls
# to confirmed (guess false) functions, each at a parameter with the same name there, takes that name (the value handed over as
# `name`; _2, _3 in order of the code). Raw C++ with the entries' names. Names that clash with any identifier of the function,
# keywords and m2c-looking names are skipped. Writes arg3_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, collections
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
KW = set('alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register return short signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while xor main field P Ref ctx frame game NULL rdram index step'.split())
CALL = re.compile(r'(?:lift_call<[\w<> ]+>\(ctx, ::(func_\w+)\)|\b(func_[0-9A-F]{8}(?:_ovl_\w)?))\(')
def split_args(s, start):
    # s[start] is just after '('; returns (list of (arg_text, arg_start)), end index
    depth, cur, args, i, a0 = 0, '', [], start, start
    while i < len(s):
        c = s[i]
        if c in '([': depth += 1
        elif c in ')]':
            if depth == 0:
                args.append((cur.strip(), a0)); return args, i
            depth -= 1
        elif c == ',' and depth == 0:
            args.append((cur.strip(), a0)); cur = ''; a0 = i + 1; i += 1; continue
        cur += c; i += 1
    return args, i
out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; raw = re.sub(r'//.*', '', cands[f]['cpp'])
    vars_ = dict(info.get('vars', {}))
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw))
    used = set(vars_.values()) | {info['name']}
    lines = [l.strip() for l in raw.split('\n')]
    # every whole-argument use: temp -> list of param names
    uses = collections.defaultdict(list)
    for l in lines:
        for m in CALL.finditer(l):
            callee = m.group(1) or m.group(2)
            args, _ = split_args(l, m.end())
            e = merged.get(callee)
            for k, (a, _) in enumerate(args):
                mm = re.fullmatch(r'(?:\([\w<> ]+\) )?(temp_\w+)', a)
                if not mm: continue
                p = (e or {}).get('vars', {}).get('arg%d' % k) if e and not e.get('guess') else None
                uses[mm.group(1)].append(p)
    wanted = []
    for i, s in enumerate(lines):
        m = re.match(r'(temp_\w+) = [^;]*;$', s)
        if not m or m.group(1) in vars_ or m.group(1) not in uses: continue
        t = m.group(1)
        writes = [y for y in lines if re.match(r'%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % re.escape(t), y)]
        if len(writes) != 1: continue
        reads = sum(len(re.findall(r'(?<![\w.>])%s\b' % re.escape(t), y)) for y in lines
                    if not re.match(r'(?:const )?[\w<>* ]+ %s;$' % re.escape(t), y) and not re.match(r'lift_out_\w+ = (?:\([\w<> ]+\) )?%s;$' % re.escape(t), y)) - 1
        ps = uses[t]
        if reads != len(ps) or not ps or None in ps or len(set(ps)) != 1: stats['other_use'] += 1; continue
        p = ps[0]
        if M2C.match(p) or p in KW: continue
        wanted.append((t, re.sub(r'_\d+$', '', p), i))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        while nm in used or nm in ids:
            seen[cand] += 1; nm = '%s_%d' % (cand, seen[cand])
        if len(nm) > 40: continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/arg3_edits.json', 'w'), indent=0)
print(stats, len(out))
c = collections.Counter(re.sub(r'_\d+$', '', v) for d in out.values() for v in d.values())
print(c.most_common(30))
