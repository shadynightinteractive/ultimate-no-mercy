# (fablenames, 2026-11-14) Rule FR, autovars.py's rule (an m2c temp that holds exactly one named field read) redone so that
# every shape of the release's record macros is read by its own parameters, not by position. autovars' pattern took the second
# argument as the field whatever the macro, which is right for KIND(address, name) and KIND_AS(address, name, type) but names
# the wrong thing for the others (GAME_TABLE(wrestler_place_z, temp_s4) became wrestler_place_z_temp_s4, SOUND_CHANNEL_VIA(at,
# bend_track, script) became record_at_bend_track: the anchor, not the field read), and it never matched GAME_VAR(name).
# What is named: a temp_ or var_ written only by `t = MACRO(args)` (once, or the same text at every write: rulelib.value_writes) (one cast allowed; a whole statement or a step of a
# condition's comma expression, rulelib.assignments), MACRO one of the macros lift.fields_preamble defines, and used at least once. The name follows from the macro's own parameters:
#   KIND(address, name) / KIND_AS(address, name, type)  the field's name, with the holder's name in front when the holder is a
#                                                 named local that is not the first parameter (autovars' rule, unchanged:
#                                                 picture_width, other_record_move_index)
#   KIND_VIA[_AS](at, anchor, name ..)           the field's name: `at` points into the record at `anchor`, `name` is what is read
#   KIND_IN[_AS](array, bytes, name ..)          <kind>_<name>: field `name` of one record of the array (object_shape_holder)
#   KIND.._AT[_AS](.., index, name ..)           as above with _at_<index>, only for a constant index (y_at_1)
#   GAME_VAR(name)                               the variable's own name: the temp is the value read from it (the given names
#                                                 already do this: save_image, money, step_slot, editor_page)
#   GAME_VAR_AS(name, type)                      <name>_<type> for a number type (the given convention: hud_player_count_s8),
#                                                 <name> for a pointer type
#   GAME_TABLE[_AS] / GAME_ARRAY[_AS](name, bytes ..)   <name>_entry: one entry of the table `name`
# Duplicates take _2, _3 in order of the code. A name already held by a local of the function is continued (x_2 ...) only when
# that local is itself a read of the same thing by this rule's reading; otherwise the proposal is skipped. Names that clash with
# an identifier of the raw C++, a called function, a keyword or an m2c-looking name are skipped.
# Writes fieldread_edits.json for put.py ({func: {"vars+": {...}}}).
from paths import D, L, B, T, BASE, CANDS, CH
from rulelib import assignments, value_writes, no_other_write
import json, re, sys, os, collections
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'
import lift
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8')) if os.path.exists(CH) else {}
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
kinds, sources = lift.read_fields()
lift.field_param_kinds(cands, lift.with_copy_names(merged, cands), kinds, sources)
KW = lift.CPP_KEYWORDS | {'main', 'field', 'P', 'ctx', 'frame', 'game', 'NULL', 'rdram', 'lift_rdram', 'index', 'abs', 'min', 'max', 'exp', 'log', 'sqrt', 'sin', 'cos', 'floor', 'time', 'signal', 'remove', 'rename', 'free', 'select', 'link', 'read', 'write', 'open', 'close', 'clock', 'round', 'trunc', 'div', 'rand', 'pow', 'y0', 'y1', 'j0', 'j1', 'yn', 'jn', 'step', 'printf', 'malloc', 'memcpy', 'memset', 'strlen', 'abort', 'exit', 'fabs', 'atan2', 'std', 's8', 's16', 's32', 's64', 'u8', 'u16', 'u32', 'u64', 'f32', 'f64'}
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+|r\d+|f\d+)$')
# (names083, 2026-11-14) A variable with "count" above 1 in fields.json (camera_matrix: 16 floats) is an array: a read of it at
# its own address, GAME_VAR(name), is its element 0, named <name>_entry_0 -- <name> alone would say the whole array.
VAR_COUNT = {v['name']: int(v.get('count', 1)) for v in json.load(open(L + 'fields.json', encoding='utf8')).get('_variables', {}).values()}
MACROS = {}
for line in lift.fields_preamble(kinds, sources):
    m = re.match(r'#define (\w+)\(([^)]*)\) ', line)
    if m:
        MACROS[m.group(1)] = [p.strip() for p in m.group(2).split(',')]
GENERIC = r'his_record|wrestler|step|record|self|man|row|widget|actor|layer|screen|page'

def split_args(s):
    out, depth, cur = [], 0, ''
    for c in s:
        if c in '(<[':
            depth += 1
        elif c in ')>]':
            depth -= 1
            if depth < 0:
                return None
        if c == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += c
    if depth:
        return None
    out.append(cur.strip())
    return out

def reading(macro, args, vars_, first_param, pf):
    """(candidate name, what is read) or None"""
    ps = MACROS.get(macro)
    if not ps or len(ps) != len(args):
        return None
    a = dict(zip(ps, args))
    if macro.startswith('GAME_'):
        nm = a.get('name')
        if not nm or not re.fullmatch(r'[a-z_][a-z0-9_]*', nm):
            return None
        if VAR_COUNT.get(nm, 1) > 1 and macro in ('GAME_VAR', 'GAME_VAR_AS'):
            return nm + '_entry_0', ('table', nm)   # (names083: element 0 of an array variable)
        if macro == 'GAME_VAR':
            return nm, ('var', nm)
        if macro == 'GAME_VAR_AS':
            ty = a['type']
            return (nm + '_' + ty if re.fullmatch(r'[suf]\d+', ty) else nm), ('var', nm, ty)
        return nm + '_entry', ('table', nm)
    fld = a.get('name')
    if not fld or not re.fullmatch(r'[a-z_][a-z0-9_]*', fld):
        return None
    root = macro
    for p, suf in (('type', '_AS'), ('index', '_AT'), ('array', '_IN'), ('anchor', '_VIA')):
        if p in a:
            if not root.endswith(suf):
                return None
            root = root[:-len(suf)]
    tail = ''
    if 'index' in a:
        if not re.fullmatch(r'\d+|0x[0-9A-Fa-f]+', a['index']):
            return None
        tail = '_at_%d' % int(a['index'], 0)
    if 'array' in a:
        pre = root.lower()
        return (fld if fld.startswith(pre) else pre + '_' + fld) + tail, ('in', root, fld, tail)
    if 'anchor' in a:
        return fld + tail, ('via', root, fld, tail)
    base = a.get('address')
    if base is None or not re.fullmatch(r'\w+', base):
        return None
    def rootname(b, depth=0):
        if b in vars_:
            return vars_[b]
        if b in pf and depth < 4:
            return pf[b]
        return None if M2C.match(b) else b
    r = rootname(base)
    if base == first_param or r is None or re.fullmatch(GENERIC, r):
        cand = fld
    else:
        pre = re.sub(r'_record$', '', r)
        cand = fld if pre in ('his',) or fld.startswith(pre) else '%s_%s' % (pre, fld)
    return cand + tail, ('field', root, fld, tail, cand)

RHS = re.compile(r'(?:\((?:s8|s16|s32|u8|u16|u32|f32|f64|P<[\w<> ]+>) ?\) ?)?([A-Z][A-Z0-9_]*)\((.*)\)')
out = {}
stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'):
        continue
    info = merged[f]
    cpp = cands[f]['cpp']
    fc = lift.fielded_cpp(f, cpp, info, kinds, sources)
    vars_ = dict(info.get('vars', {}))
    raw_ids = set(re.findall(r'\b[A-Za-z_]\w*\b', re.sub(r'//.*', '', cpp)))
    called = set(re.findall(r'\b([A-Za-z_]\w*)\s*[(<]', fc))
    first_param = None
    mp = re.match(r"[^\n(]*?\b%s\(([^)\n]*)\)" % re.escape(f), cpp)
    if mp:
        ps = [p.split()[-1] for p in mp.group(1).split(',') if p.strip() and not p.strip().startswith('recomp_context')]
        first_param = ps[0] if ps else None
    found = []
    for t, (rhs, pos) in value_writes(re.sub(r'//.*', '', fc)).items():
        m = RHS.fullmatch(rhs)
        if not m:
            continue
        macro, argtext = m.groups()
        args = split_args(argtext)
        if args is None:
            continue
        if not no_other_write(t, fc, pos):
            continue
        if len(re.findall(r'\b%s\b' % t, fc)) < 2 + len(re.findall(r'(?<![\w.>])%s = ' % t, fc)):   # declaration, the write(s) and at least one use
            continue
        found.append((t, macro, args, pos[0]))
    pf = {}
    for t, macro, args, pos in found:
        ps = MACROS.get(macro)
        if ps and ps[:2] == ['address', 'name'] and len(args) == len(ps):
            pf[t] = args[1]
    held = {}      # name -> what the local holding it reads (None: not a read by this rule)
    for t, macro, args, pos in found:
        if t in vars_:
            r = reading(macro, args, vars_, first_param, pf)
            held[vars_[t]] = r[0] if r else None
    used = set(vars_.values()) | {info['name']}
    wanted = []
    for t, macro, args, pos in found:
        if t in vars_:
            continue
        r = reading(macro, args, vars_, first_param, pf)
        if r:
            wanted.append((t, r[0], pos))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: x[2]):
        while True:
            seen[cand] += 1
            name_ = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
            if name_ in used and held.get(name_) == cand:
                continue           # the same thing read earlier and named so: take the next number
            break
        if (name_ in KW or name_ in raw_ids or name_ in called or name_ in used or M2C.match(name_) or name_.startswith('lift_')
                or name_.startswith('m2c') or not re.fullmatch(r'[a-z_][a-z0-9_]*', name_) or len(name_) > 40):
            stats['skipped'] += 1
            continue
        used.add(name_)
        out.setdefault(f, {})[t] = name_
        stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(T + 'fieldread_edits.json', 'w'), indent=0)
print(stats, len(out))
