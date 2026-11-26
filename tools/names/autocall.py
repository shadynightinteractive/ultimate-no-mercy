# Proposes names for m2c temps that hold exactly one call's result, from the callee's (confirmed) name.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, sys, os, collections
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
new = set()
KW = set('alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register return short signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while xor main field P ctx frame game NULL rdram index abs min max exp log sqrt sin cos floor time signal remove rename free select link read write open close clock round trunc div rand pow y0 y1 j0 j1 yn jn step'.split())
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+)$')
GETTER = {'find': None, 'get': None, 'pick': None, 'read': None, 'compute': None, 'calc': None, 'choose': None}
VERBS = set('set start draw play make load free add clear update run apply copy store write init build place move push show hide give take release try do change put send wait reset spawn attach ai open close step check finish award stamp track mark fill pack unpack keep restore tick knock toggle switch select scroll refresh rebuild save print format zero turn walk measure snap advance restart begin end enter leave handle process fade slide count cancel confirm request remember forget drop hold shake split merge use answer sort swap stop pause resume skip bump flip ease spin roll pull drive tag decide react defend help'.split())
MAP = {'count_down_timer_mashing': 'got_out', 'count_down_ground_grapple_escape': 'escaped', 'chance_in_1000': 'roll_won', 'int_abs': 'magnitude', 'abs_float': 'magnitude', 'float_abs': 'magnitude', 'heap_alloc': 'block', 'al_heap_alloc': 'block', 'audio_alloc': 'block', 'random_0_to_999': 'random', 'next_random_number': 'random', 'menu_widget_pointer': 'widget', 'string_length': 'length', 'cont_ram_read': 'status', 'cont_ram_write': 'status', 'on_animation_ending': 'animation_ended', 'n_alloc_param': 'update', 'integer_atan': 'atan', 'flag_bit_set': 'flag_set', 'menu_context': 'context', 'menu_text_picture': 'picture', 'text_id_picture': 'picture', 'menu_text_picture_data': 'picture', 'superstar_roster_record': 'roster_record', 's64_to_float': None, 'short_vector_to_float': None, 'square_root': 'root'}
def var_from(callee_name):
    if callee_name in MAP:
        return MAP[callee_name]
    w = callee_name.split('_')
    if w[0] in GETTER and len(w) > 1:
        return '_'.join(w[1:])
    if w[0] == 'count' and len(w) > 1 and w[1] != 'down':   # (names5) count_down_x counts something down, it doesn't count x
        return '_'.join(w[1:]) + '_count'
    if w[0] in VERBS:
        return None
    return callee_name
out = {}
stats = collections.Counter()
for f in sorted(merged):
    if f in new or f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'):
        continue
    info = merged[f]; cpp = cands[f]['cpp']
    vars_ = dict(info.get('vars', {}))
    raw_ids = set(re.findall(r'\b[A-Za-z_]\w*\b', re.sub(r'//.*', '', cpp)))
    used = set(vars_.values()) | {info['name']}
    wanted = []
    for m in re.finditer(r'(?m)^\s+(temp_\w+) = (?:\([\w<> ]+\) ?)?lift_call<[\w<> ]+>\(ctx, ::(func_\w+)\)\(', cpp):
        t, callee = m.groups()
        if t in vars_ or callee not in merged or merged[callee].get('guess'):
            continue
        if len(re.findall(r'(?<![\w.>])%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % t, cpp)) != 1 or len(re.findall(r'\b%s\b' % t, cpp)) < 3:
            continue
        cand = var_from(merged[callee]['name'])
        if cand:
            wanted.append((t, cand, m.start()))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in KW or nm in raw_ids or nm in used or M2C.match(nm) or nm.startswith('lift_') or not re.fullmatch(r'[a-z_][a-z0-9_]*', nm) or len(nm) > 40:
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump(out, open(T + 'autocall.json', 'w'), indent=1)
print(stats, len(out))
c = collections.Counter(v.rstrip('_0123456789') for d in out.values() for v in d.values())
print(c.most_common(60))
