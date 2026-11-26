from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os
names = json.load(open(BASE, encoding='utf8'))
if os.path.exists(CH):
    names.update(json.load(open(CH, encoding='utf8')))
cands = json.load(open(CANDS))
g = json.load(open(T + 'graph.json'))
raw = '-raw' in sys.argv
short = int(os.environ.get('ABOUT', '160'))
def nm(f):
    e = names.get(f)
    return (e['name'] + ('?' if e.get('guess') else '')) if e else '-'
def ab(f):
    e = names.get(f)
    return (e['about'].replace('\n', ' ')[:short]) if e else ''
for f in [a for a in sys.argv[1:] if not a.startswith('-')]:
    e = names.get(f, {})
    d = g.get(f, {'size': '?', 'callers': [], 'callees': []})
    print('=' * 100)
    print('%s  %s  size %s  guess %s' % (f, e.get('name'), d['size'], e.get('guess')))
    print('ABOUT:', e.get('about'))
    print('VARS:', json.dumps(e.get('vars', {})))
    print('CALLERS (%d):' % len(d['callers']))
    for c in d['callers'][:int(os.environ.get('CAL', '14'))]:
        print('   %s %s: %s' % (c, nm(c), ab(c)))
    print('CALLEES:')
    for c in d['callees']:
        print('   %s %s: %s' % (c, nm(c), ab(c)))
    if 'cpp' not in cands.get(f, {}):
        import glob
        for p in glob.glob(D + 'exe/WWFNoMercyRecomp/RecompiledFuncs/funcs_*.c'):
            t = open(p, encoding='utf8', errors='replace').read()
            i = t.find('RECOMP_FUNC void %s(' % f)
            if i >= 0:
                cpp = t[i:t.index(chr(10) + ';}', i) + 3]; break
        else:
            cpp = '(not found)'
        cpp = chr(10).join(l for l in cpp.split(chr(10)) if l.strip() and not l.strip().startswith('// 0x'))
    else:
        cpp = cands[f]['cpp']
    if not raw and 'cpp' in cands.get(f, {}):
        sw = dict(e.get('vars', {}))
        if sw:
            cpp = re.sub(r'(?<!::)\b(' + '|'.join(re.escape(k) for k in sorted(sw, key=len, reverse=True)) + r')\b', lambda m: sw[m.group(1)], cpp)
        cpp = re.sub(r'::(func_[0-9A-F]{8})', lambda m: '::' + m.group(1) + '<' + nm(m.group(1)) + '>', cpp)
    lim = int(os.environ.get('LINES', '120'))
    lines = cpp.split('\n')
    if os.environ.get('FULL') != '1':
        lines = [l for l in lines if not re.match(r'\s+(?:s8|s16|s32|u8|u16|u32|f32|f64|s64|u64|P<.*>|Ref<.*>) \w+;$', l) and 'frame.keep' not in l and 'LiftAbi' not in l and not re.match(r'\s+lift_out_(?!v0)\w+ = [^;]*;$', l)]
    print('\n'.join(lines[:lim]) + ('\n... (%d more lines)' % (len(lines) - lim) if len(lines) > lim else ''))
