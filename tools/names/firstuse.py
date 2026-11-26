# (names5) For hand naming: each unnamed m2c local of a function with its first few uses in the named, fielded C++.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift, pickle
names = json.load(open(BASE, encoding='utf8')); names.update(json.load(open(CH, encoding='utf8')))
cands = json.load(open(CANDS))
kinds, sources = lift.read_fields()
alln = lift.with_copy_names(names, cands)
fp = pickle.load(open(B + 'tools/fpk.pickle', 'rb')); lift.FIELD_PARAMS.update(fp[0]); lift.FIELD_RETURNS.update(fp[1])
N = int(os.environ.get('USES', '3'))
for f in sys.argv[1:]:
    v = cands[f]
    out = lift.named_cpp(f, dict(v, cpp=lift.fielded_cpp(f, v['cpp'], alln.get(f), kinds, sources)), alln.get(f))
    lines = [l.strip() for l in out.split('\n')]
    ids = sorted(set(re.findall(r'\b((?:var|temp)_\w+|sp[0-9A-F]+)\b', out)) - {'temp_ret'})
    print('==', f, names[f]['name'], len(ids))
    for t in ids:
        us = [l for l in lines if re.search(r'\b%s\b' % t, l) and not re.match(r'(?:const )?[\w<>* ]+ %s;$' % t, l) and 'frame.at' not in l]
        print(' %-12s | %s' % (t, ' || '.join(u[:90] for u in us[:N])))
