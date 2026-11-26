# (namesprep, 2026-11-14) Self-check of the offline folder: each function in 0.82's released lifted.cpp, rendered here offline
# (fielded_cpp + named_cpp with the given names.json, as lift.py release_text does), must appear in lifted.cpp exactly.
# Usage: python release_equal.py [0|1]    (LIFT_MODREP: 1, the default, as the release ran; 0 as the names tools run)
from paths import D as F
import json, os, re, sys
mode = sys.argv[1] if len(sys.argv) > 1 else '1'
os.environ['LIFT_MODREP'] = mode
sys.path.insert(0, F + 'tools/lift')
import lift
cands = json.load(open(F + 'tools/lift/candidates.json'))
names = lift.with_copy_names(json.load(open(F + 'tools/lift/names.json', encoding='utf8')), cands)
text = open(F + 'exe/WWFNoMercyRecomp/src/game/lifted.cpp', encoding='utf8').read()
released = re.findall(r'(?m)^extern "C" (?:__attribute__\(\(noinline\)\) )?void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{$', text)
kinds, sources = lift.read_fields()
lift.field_param_kinds(cands, names, kinds, sources)
same = differ = 0
bad = []
for f in released:
    v = dict(cands[f], cpp=lift.fielded_cpp(f, cands[f]['cpp'], names.get(f), kinds, sources))
    out = lift.named_cpp(f, v, names.get(f))
    if out in text:
        same += 1
    else:
        differ += 1; bad.append(f)
print('LIFT_MODREP=%s: released functions %d, rendered offline exactly as released %d, different %d %s' % (
    mode, len(released), same, differ, bad[:20]))
