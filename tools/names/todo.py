# (namesprep, 2026-11-14) The work lists, with your changes applied: (1) the entries still marked "guess": true, (2) the
# converted, named functions by how many m2c locals (temp_*, var_*, arg*, sp*, phi_*) are still unnamed, with their size and
# callers, (3) the abouts that are mostly a list of calls. Writes names/todo.txt (needs names/tools/graph.json: build.py).
from paths import D, L, B, T, BASE, CANDS, CH
import json, re
names = json.load(open(BASE, encoding='utf8'))
names.update(json.load(open(CH, encoding='utf8')))
cands = json.load(open(CANDS))
g = json.load(open(T + 'graph.json'))
M2C = re.compile(r'\b(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)\b')
conv = {n for n, v in cands.items() if 'cpp' in v and not n.startswith('modrep_')}
out = []
gs = sorted((n for n, v in names.items() if v.get('guess')), key=lambda n: (n not in conv, -len(g.get(n, {}).get('callers', []))))
out.append('== 1. "guess": true -- %d entries (%d of them converted; the mod\'s converted copies, modrep_func_X, count)' % (len(gs), sum(1 for n in gs if 'cpp' in cands.get(n, {}))))
out.append('function            name                                     converted size callers  about (start)')
for n in gs:
    out.append('%-19s %-40s %-9s %4s %7d  %s' % (n, names[n]['name'], 'yes' if n in conv else ('mod copy' if 'cpp' in cands.get(n, {}) else 'NO'), cands.get(n, {}).get('size', '-'),
                                               len(g.get(n, {}).get('callers', [])), names[n]['about'][:110].replace('\n', ' ')))
rows = []
for n in conv & set(names):
    ids = set(M2C.findall(cands[n]['cpp']))
    un = ids - set(names[n].get('vars', {}))
    if un:
        rows.append((len(un), len(ids), int(cands[n]['size']), len(g.get(n, {}).get('callers', [])), n))
out.append('')
out.append('== 2. unnamed m2c locals in named converted functions: %d in %d functions' % (sum(r[0] for r in rows), len(rows)))
out.append('   biggest first (unnamed / all m2c locals, size in instructions, callers):')
for un, al, size, callers, n in sorted(rows, reverse=True)[:300]:
    out.append('%-19s %-40s %4d / %-4d size %5d callers %3d' % (n, names[n]['name'], un, al, size, callers))
out.append('   most called first (callers, then unnamed):')
for un, al, size, callers, n in sorted(rows, key=lambda r: (-r[3], -r[0]))[:150]:
    out.append('%-19s %-40s %4d / %-4d size %5d callers %3d' % (n, names[n]['name'], un, al, size, callers))
# 3. abouts that only list calls: short, and most of their words are other functions' names
fn_names = {v['name'] for v in names.values()}
thin = []
for n in sorted(conv & set(names)):
    a = names[n]['about']
    words = re.findall(r'[A-Za-z_]\w*', a)
    calls = [w for w in words if w in fn_names or re.fullmatch(r'func_[0-9A-F]{8}\w*', w)]
    if words and (re.match(r'(?i)\s*(just )?calls\b', a) or (len(calls) >= 3 and len(calls) * 4 >= len(words))):
        thin.append(n)
out.append('')
out.append('== 3. abouts that look like a list of calls: %d (a heuristic: check each)' % len(thin))
for n in thin:
    out.append('%-19s %-40s %s' % (n, names[n]['name'], names[n]['about'][:140].replace('\n', ' ')))
open(B + 'todo.txt', 'w', encoding='utf8', newline='\n').write('\n'.join(out) + '\n')
print('\n'.join(l for l in out if l.startswith('==')))
