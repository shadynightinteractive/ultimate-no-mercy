# Proposes names for m2c temps that hold exactly one named field read (as the release's fielded C++ shows it).
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os, collections
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8')) if os.path.exists(CH) else {}
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
new = set()
kinds, sources = lift.read_fields()
lift.field_param_kinds(cands, lift.with_copy_names(merged, cands), kinds, sources)
KW = lift.CPP_KEYWORDS | {'main', 'field', 'P', 'ctx', 'frame', 'game', 'NULL', 'rdram', 'index', 'abs', 'min', 'max', 'exp', 'log', 'sqrt', 'sin', 'cos', 'floor', 'time', 'signal', 'remove', 'rename', 'free', 'select', 'link', 'read', 'write', 'open', 'close', 'clock', 'round', 'trunc', 'div', 'rand', 'pow', 'y0', 'y1', 'j0', 'j1', 'yn', 'jn', 'step'}
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
fn_names = {v['name'] for v in merged.values()}
out = {}
stats = collections.Counter()
for f in sorted(merged):
    if f in new or f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'):
        continue
    info = merged[f]
    cpp = cands[f]['cpp']
    fc = lift.fielded_cpp(f, cpp, info, kinds, sources)
    vars_ = dict(info.get('vars', {}))
    raw_ids = set(re.findall(r'\b[A-Za-z_]\w*\b', re.sub(r'//.*', '', cpp)))
    called = set(re.findall(r'\b([A-Za-z_]\w*)\s*[(<]', fc))
    used = set(vars_.values()) | {info['name']}
    shown = lambda b: vars_.get(b, b)
    props = []
    for m in re.finditer(r'(?m)^\s+(temp_\w+) = (?:\((?:s8|s16|s32|u8|u16|u32|f32|f64|P<[\w<> ]+>) ?\) ?)?([A-Z][A-Z0-9_]*?)(?:_AS)?\((\w+), (\w+)(?:, [\w<> ]+)?\);$', fc):
        t, macro, base, fld = m.groups()
        if t in vars_ or macro == 'M2C_FIELD':
            continue
        if len(re.findall(r'(?<![\w.>])%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % t, fc)) != 1:
            continue
        if len(re.findall(r'\b%s\b' % t, fc)) < 3:   # declaration, assignment and at least one use
            continue
        props.append((t, base, fld, m.start()))
    first_param = None
    mp = re.match(r"[^\n(]*?\b%s\(([^)\n]*)\)" % re.escape(f), cpp)
    if mp:
        ps = [p.split()[-1] for p in mp.group(1).split(',') if p.strip() and not p.strip().startswith('recomp_context')]
        first_param = ps[0] if ps else None
    pf = {t: fld for t, base, fld, _ in props}
    def root(base, depth=0):
        # the readable name the base would have, or None
        if base in vars_:
            return vars_[base]
        if base in pf and depth < 4:
            return pf[base]
        return None if M2C.match(base) else base
    wanted = []
    for t, base, fld, pos in props:
        r = root(base)
        if base == first_param or r is None or re.fullmatch(r'his_record|wrestler|step|record|self|man|row|widget|actor|layer|screen|page', r or ''):
            cand = fld
        else:
            pre = re.sub(r'_record$', '', r)
            cand = fld if pre in ('his',) or fld.startswith(pre) else '%s_%s' % (pre, fld)
        wanted.append((t, cand, pos))
    def tnum(t):
        m = re.search(r'_(\d+)$', t)
        return (re.sub(r'_\d+$', '', t), int(m.group(1)) if m else 1)
    
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        name_ = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if (name_ in KW or name_ in raw_ids or name_ in called or name_ in used or M2C.match(name_) or name_.startswith('lift_')
                or name_.startswith('m2c') or not re.fullmatch(r'[a-z_][a-z0-9_]*', name_)):
            stats['skipped'] += 1
            continue
        used.add(name_)
        out.setdefault(f, {})[t] = name_
        stats['named'] += 1
json.dump(out, open(T + 'autovars.json', 'w'), indent=1)
print(stats, len(out))
