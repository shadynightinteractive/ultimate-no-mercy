# (names4) classify the unnamed m2c locals by the shape of their assignments (to find the next deterministic rule)
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, collections, sys
n = json.load(open(BASE, encoding='utf8')); n.update(json.load(open(CH, encoding='utf8')))
c = json.load(open(CANDS))
M2C = re.compile(r'\b(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)\b')
cls = collections.Counter(); ex = collections.defaultdict(list)
for f, e in n.items():
    if f not in c or 'cpp' not in c[f] or f.startswith('modrep_'): continue
    code = re.sub(r'//.*', '', c[f]['cpp']); vars_ = e.get('vars', {})
    for t in sorted(set(M2C.findall(code)) - set(vars_)):
        if t.startswith('arg'): k = 'arg'
        elif t.startswith('sp'): k = 'sp'
        else:
            ws = re.findall(r'(?<![\w.>])%s = ([^;\n]+);' % re.escape(t), code)
            if not ws: k = 'nowrite'
            else:
                w = ws[0]
                if re.fullmatch(r'\w+ & (0x[0-9A-F]+|\d+)', w): k = 'mask'
                elif re.fullmatch(r'\w+ [+-] \d+', w): k = 'incdec'
                elif re.fullmatch(r'\w+ [<>]=? \w+', w): k = 'compare'
                elif re.fullmatch(r'\w+ (<<|>>) \w+', w): k = 'shift'
                elif re.fullmatch(r'\w+ \* \w+', w): k = 'mul'
                elif re.fullmatch(r'-?(0x[0-9A-F]+|\d+)U?', w): k = 'const'
                elif re.fullmatch(r'\(?[\w ]*\)? ?M2C_FIELD\(.*\)', w): k = 'field'
                elif re.fullmatch(r'\*\(P<[^)]*\) ?\)?0x[0-9A-F]+', w) or re.fullmatch(r'\*\(P<.*>\s*\)\s*0x[0-9A-F]+', w): k = 'global'
                elif 'lift_call' in w: k = 'call'
                elif re.fullmatch(r'[\w.]+', w): k = 'copy'
                elif len(ws) > 1: k = 'multi'
                else: k = 'other'
            if t.startswith('temp_f') or t.startswith('var_f'): k = 'f_' + k
        cls[k] += 1
        if len(ex[k]) < 4: ex[k].append((f, t, (re.findall(r'(?<![\w.>])%s = ([^;\n]+);' % re.escape(t), code) or [''])[0][:80]))
for k, v in cls.most_common(): print(k, v, ex[k][:3])
