# (names5) Shapes of the unnamed m2c locals on the raw C++: how often each is read / written, and the first write's form, to find
# the next deterministic rule. Prints counts by (writes, reads, form) and examples.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, collections, sys
n = json.load(open(BASE, encoding='utf8')); n.update(json.load(open(CH, encoding='utf8')))
c = json.load(open(CANDS))
M2C = re.compile(r'\b(var_\w+|temp_\w+|phi_\w+)\b')
cls = collections.Counter(); ex = collections.defaultdict(list)
def form(w):
    w = re.sub(r'\(\w+\) ', '', w)
    if re.fullmatch(r'lift_call<[^>]*>\(ctx, ::\w+\)\(.*\)', w) or re.fullmatch(r'func_\w+\(.*\)', w): return 'call'
    if re.fullmatch(r'M2C_FIELD\(.*\)', w): return 'field'
    if re.fullmatch(r'\*\(P<[^)]*\) ?\)?0x[0-9A-F]+', w): return 'global'
    if re.fullmatch(r'-?(0x[0-9A-F]+|\d+)U?|[\d.]+f', w): return 'const'
    if re.fullmatch(r'arg\d+', w): return 'arg'
    if re.fullmatch(r'(var|temp)_\w+', w): return 'm2c'
    if re.fullmatch(r'[a-z_]\w*', w): return 'named'
    if re.fullmatch(r'\w+ [-+] (0x[0-9A-F]+|\d+)', w): return 'step'
    if re.search(r'[<>=!]=?', w) and not re.search(r'<<|>>', w): return 'cmp'
    return 'expr'
for f, e in n.items():
    if f not in c or 'cpp' not in c[f] or f.startswith('modrep_'): continue
    code = re.sub(r'//.*', '', c[f]['cpp']); vars_ = e.get('vars', {})
    for t in sorted(set(M2C.findall(code)) - set(vars_)):
        ws = re.findall(r'(?m)^\s*%s = ([^;\n]+);' % re.escape(t), code)
        upd = len(re.findall(r'(?<![\w.>])%s (?:[-+*/&|^]|<<|>>)= ' % re.escape(t), code))
        allocc = len(re.findall(r'\b%s\b' % re.escape(t), code))
        decl = len(re.findall(r'(?m)^\s*(?:const )?[\w<>* ]+ %s;' % re.escape(t), code))
        reads = allocc - len(ws) - upd - decl
        fs = tuple(sorted(set(form(w) for w in ws)))
        k = (min(len(ws), 3), min(reads, 3), fs if len(fs) < 3 else ('many',))
        cls[k] += 1
        if len(ex[k]) < 3: ex[k].append((f, t, ws[0][:70] if ws else ''))
for k, v in cls.most_common(int(sys.argv[1]) if len(sys.argv) > 1 else 40): print(v, k, ex[k][:2])
