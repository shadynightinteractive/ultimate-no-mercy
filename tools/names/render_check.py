from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os, collections
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
kinds, sources = lift.read_fields()
allnames = lift.with_copy_names(merged, cands)
lift.field_param_kinds(cands, allnames, kinds, sources)
bad = 0; typed_changed = []
oldkinds = {}
for f in sorted(ch):
    v = cands[f]
    fc = lift.fielded_cpp(f, v['cpp'], allnames.get(f), kinds, sources)
    out = lift.named_cpp(f, dict(v, cpp=fc), allnames.get(f))
    decls = re.findall(r'(?m)^    (?:const )?(?:s8|s16|s32|u8|u16|u32|f32|f64|s64|u64|P<[^;]*?>|Ref<[^;]*?>|M2C_UNK) (\w+)[;(]', out)
    d = [x for x, c in collections.Counter(decls).items() if c > 1]
    head = out.split('{', 1)[0]
    params = re.findall(r'(\w+)(?=[,)])', head)
    clash = set(params) & set(decls)
    if d or clash:
        bad += 1; print(f, 'duplicate declarations', d, clash)
    out_old = lift.named_cpp(f, dict(v, cpp=lift.fielded_cpp(f, v['cpp'], names.get(f), kinds, sources)), names.get(f)) if f in names else None
print('checked', len(ch), 'bad', bad)
