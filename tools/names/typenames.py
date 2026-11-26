# (names5, 2026-11-13) Finds local names that newvals / maskvars made from a global's type instead of its name (GAME_VAR_AS(x, u16)
# read as field "u16": new_u16, x_u16_bit3 ...). With -fix: drops them from changes.json (when the base had them, the entry goes
# back to the base's vars without them) so the fixed rules can name them again.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys
base = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(base); merged.update(ch)
T = r'(?:u8|s8|u16|s16|u32|s32|f32|f64)'
BAD = re.compile(r'^new_%s(?:_\d+)?$|^(?:\w+_)?%s_(?:bit\d+|low_byte|low_half)(?:_\d+)?$' % (T, T))
hits = [(f, k, v) for f, e in merged.items() for k, v in e.get('vars', {}).items() if BAD.match(v)]
for h in hits: print(*h, 'base' if base.get(h[0], {}).get('vars', {}).get(h[1]) == h[2] else 'new')
print(len(hits))
if '-fix' in sys.argv:
    for f, k, v in hits:
        e = ch.get(f) or json.loads(json.dumps(base[f]))
        e['vars'].pop(k, None)
        ch[f] = e
    json.dump(dict(sorted(ch.items())), open(CH, 'w', encoding='utf8', newline='\n'), indent=1, ensure_ascii=False)
    open(CH, 'a').write('\n')
    print('fixed')
