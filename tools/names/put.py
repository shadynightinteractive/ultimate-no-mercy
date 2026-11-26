# Merge edits (JSON on stdin: {func: {name?, about?, guess?, vars?: full, "vars+"?: additions}}) into changes.json as full entries.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, sys, re, os
names = json.load(open(BASE, encoding='utf8'))
cands = json.load(open(CANDS))
ch = json.load(open(CH, encoding='utf8')) if os.path.exists(CH) else {}
edits = json.loads(sys.stdin.read())
KW = set('alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register return short signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while xor main'.split())
RES = {'field', 'P', 'ctx', 'frame', 'game', 'NULL', 'rdram', 'lift_rdram', 's8', 's16', 's32', 's64', 'u8', 'u16', 'u32', 'u64', 'f32', 'f64', 'std', 'index', 'abs', 'min', 'max', 'exp', 'log', 'sqrt', 'sin', 'cos', 'floor', 'time', 'signal', 'remove', 'rename', 'printf', 'free', 'malloc', 'memcpy', 'memset', 'strlen', 'abort', 'exit', 'select', 'link', 'read', 'write', 'open', 'close', 'clock', 'round', 'trunc', 'div', 'rand', 'pow', 'fabs', 'atan2', }
bad = False
for f, e in edits.items():
    if f not in cands or 'cpp' not in cands[f]:
        print('NOT CONVERTED', f); bad = True; continue
    base = dict(ch.get(f) or names.get(f) or {'name': None, 'about': None, 'guess': True, 'vars': {}})
    base['vars'] = dict(base.get('vars', {}))
    for k in ('name', 'about', 'guess'):
        if k in e: base[k] = e[k]
    if 'about+' in e: base['about'] = base['about'].rstrip() + ' ' + e['about+']
    if 'vars' in e: base['vars'] = dict(e['vars'])
    for k, v in e.get('vars+', {}).items(): base['vars'][k] = v
    for k in e.get('vars-', []): base['vars'].pop(k, None)
    cpp = cands[f]['cpp']
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', re.sub(r'//.*', '', cpp)))
    vals = list(base['vars'].values())
    orig_vars = (names.get(f) or {}).get('vars', {})
    for k, v in base['vars'].items():
        if orig_vars.get(k) == v and list(base['vars'].values()).count(v) == 1: continue
        if k not in ids: print(f, 'var key not in cpp:', k); bad = True
        if not re.fullmatch(r'[a-z_][a-z0-9_]*', v) or v in KW or v in RES: print(f, 'bad var name', v); bad = True
        if v in ids and v != k: print(f, 'var name already an identifier in cpp:', v); bad = True
        if vals.count(v) > 1: print(f, 'duplicate var name', v); bad = True
        if v.startswith('lift_') or v.startswith('m2c') or re.fullmatch(r'(sp[0-9A-F]+|var_\w+|temp_\w+|phi_\w+|arg\d+|r\d+|f\d+)', v): print(f, 'reserved-looking name', v); bad = True
    if (base['name'] != (names.get(f) or {}).get('name')) and (not base['name'] or not re.fullmatch(r'[a-z_][a-z0-9_]*', base['name']) or base['name'] in KW): print(f, 'bad name', base['name']); bad = True
    if base['name'] in ids and base['name'] != f: print(f, 'function name collides with cpp identifier', base['name']); bad = True
    if base['name'] in base['vars'].values(): print(f, 'function name equals a var'); bad = True
    others = {n for n, v in names.items() if v['name'] == base['name'] and n != f} | {n for n, v in ch.items() if v['name'] == base['name'] and n != f}
    if others: print(f, 'name already used by', others); bad = True
    if not base['about']: print(f, 'no about'); bad = True
    if bad: continue
    ch[f] = {'about': base['about'], 'guess': bool(base['guess']), 'name': base['name'], 'vars': dict(sorted(base['vars'].items()))}
if bad:
    print('NOT WRITTEN'); sys.exit(1)
json.dump(dict(sorted(ch.items())), open(CH, 'w', encoding='utf8', newline='\n'), indent=1, ensure_ascii=False)
open(CH, 'a').write('\n')
print('ok', len(edits), 'total', len(ch))
