# (names3) Compact reading view: fielded + named C++, noise lines dropped, calls shown by name, callers listed.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift, pickle
names = json.load(open(BASE, encoding='utf8'))
if os.path.exists(CH): names.update(json.load(open(CH, encoding='utf8')))
cands = json.load(open(CANDS))
g = json.load(open(B + 'tools/graph.json'))
kinds, sources = lift.read_fields()
alln = lift.with_copy_names(names, cands)
PK = B + 'tools/fpk.pickle'
if os.path.exists(PK):
    lift.FIELD_PARAMS.update(pickle.load(open(PK, 'rb'))[0]); lift.FIELD_RETURNS.update(pickle.load(open(PK, 'rb'))[1])
else:
    lift.field_param_kinds(cands, alln, kinds, sources)
    pickle.dump((dict(lift.FIELD_PARAMS), dict(lift.FIELD_RETURNS)), open(PK, 'wb'))
W = int(os.environ.get('W', '170')); LIM = int(os.environ.get('LINES', '90')); AB = int(os.environ.get('ABOUT', '2000'))
def nm(f):
    e = names.get(f); return (e['name'] + ('?' if e.get('guess') else '')) if e else f
for f in sys.argv[1:]:
    e = names.get(f, {}); v = cands[f]; d = g.get(f, {'callers': [], 'size': v.get('size')})
    print('=' * 60); print('%s %s size %s guess %s' % (f, e.get('name'), v.get('size'), e.get('guess')))
    print('ABOUT:', e.get('about', '')[:AB])
    print('CALLERS:', ', '.join(nm(c) for c in d['callers'][:10]) + (' +%d' % (len(d['callers']) - 10) if len(d['callers']) > 10 else ''))
    fc = lift.fielded_cpp(f, v['cpp'], alln.get(f), kinds, sources)
    out = lift.named_cpp(f, dict(v, cpp=fc), alln.get(f))
    out = re.sub(r'lift_call<[\w<> ]+>\(ctx, ::(func_\w+)\)', lambda m: nm(m.group(1)), out)
    out = re.sub(r'\b(func_[0-9A-F]{8}(?:_ovl_\w)?)(?=\()', lambda m: nm(m.group(1)), out)
    lines = []
    for l in out.split('\n'):
        s = l.strip()
        if not s or re.match(r'(?:const )?(?:s8|s16|s32|u8|u16|u32|f32|f64|s64|u64|P<.*>|Ref<.*>|M2C_UNK) \w+;$', s) or 'LiftFrame' in s or 'frame.keep' in s or re.match(r'lift_out_\w+ = [^;]*;$', s) or s == '':
            continue
        l = re.sub(r'^((?:    )+)', lambda m: ' ' * (len(m.group(1)) // 4), l)
        lines.append(l[:W])
    print('\n'.join(lines[:LIM]) + ('\n...(%d more)' % (len(lines) - LIM) if len(lines) > LIM else ''))
