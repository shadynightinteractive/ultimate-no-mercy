# (fablenames, 2026-11-14) What the fablenames rules share: loading the names with the changes on top, the tests every rule makes
# on an m2c local (written once, how often read, the ABI's lift_out mirrors left aside) and the one way names are handed out
# (in order of the code, _2 _3 .., nothing that clashes). Not a rule itself: each rule file says what it names.
from paths import D, L, B, T, BASE, CANDS, CH
import json, re, sys, os, collections
sys.path.insert(0, L)
os.environ['LIFT_MODREP'] = '0'

KW = set('alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register return short signed sizeof static struct switch template this throw true try typedef typeid typename union unsigned using virtual void volatile while xor main'.split())
RES = {'field', 'P', 'Ref', 'ctx', 'frame', 'game', 'NULL', 'rdram', 'lift_rdram', 's8', 's16', 's32', 's64', 'u8', 'u16', 'u32', 'u64', 'f32', 'f64', 'std', 'index', 'abs', 'min', 'max', 'exp', 'log', 'sqrt', 'sin', 'cos', 'floor', 'time', 'signal', 'remove', 'rename', 'printf', 'free', 'malloc', 'memcpy', 'memset', 'strlen', 'abort', 'exit', 'select', 'link', 'read', 'write', 'open', 'close', 'clock', 'round', 'trunc', 'div', 'rand', 'pow', 'fabs', 'atan2', 'y0', 'y1', 'j0', 'j1', 'yn', 'jn', 'step'}
M2C = re.compile(r'(sp[0-9A-F]+(?:_\w+)?|var_\w+|temp_\w+|phi_\w+|arg\d+|r\d+|f\d+)$')
CAST = r'(?:\((?:s8|s16|s32|s64|u8|u16|u32|u64|f32|f64|P<[\w<> ]+>) ?\) ?)?'


def load():
    names = json.load(open(BASE, encoding='utf8'))
    ch = json.load(open(CH, encoding='utf8')) if os.path.exists(CH) else {}
    merged = dict(names); merged.update(ch)
    cands = json.load(open(CANDS))
    return names, ch, merged, cands


def functions(merged, cands):
    for f in sorted(merged):
        if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'):
            continue
        yield f, merged[f], re.sub(r'//.*', '', cands[f]['cpp'])


def fielder(merged, cands):
    """f, info, raw -> the release's fielded C++ (comments dropped)"""
    import lift
    kinds, sources = lift.read_fields()
    lift.field_param_kinds(cands, lift.with_copy_names(merged, cands), kinds, sources)
    return lambda f, info: re.sub(r'//.*', '', lift.fielded_cpp(f, cands[f]['cpp'], info, kinds, sources))


def write_count(t, code):
    return len(re.findall(r'(?<![\w.>])%s\s*(?:[-+*/%%&|^]|<<|>>)?=(?!=)' % re.escape(t), code)) + len(re.findall(r'(?:\+\+|--)%s\b|\b%s(?:\+\+|--)|&%s\b' % ((re.escape(t),) * 3), code))


def read_count(t, code):
    """uses of t that are not its declaration, the left side of a write or a lift_out mirror (`lift_out_v0 = t`, the ABI's
    copy of a register, as a statement or as a step of a comma expression)"""
    e = re.escape(t)
    total = len(re.findall(r'(?<![\w.>])%s\b' % e, code))
    decls = len(re.findall(r'(?m)^\s*(?:const )?[\w<>*, ]+ \*?%s;$' % e, code))
    writes = len(re.findall(r'(?<![\w.>])%s\s*(?:[-+*/%%&|^]|<<|>>)?=(?!=)' % e, code))
    mirrors = len(re.findall(r'lift_out_\w+ = %s%s(?=\s*[,;)])' % (CAST, e), code))
    return total - decls - writes - mirrors


def assignments(code):
    """every plain assignment `local = value` of an m2c temp_ / var_ in the code: as a whole statement (`t = value;`) or as one
    step of a comma expression inside a condition (`if ((a == 0) || (t = value, lift_out_v0 = t, (t == 0)))`), which is how m2c
    writes a value computed on the way through a test. Yields (local, value text, position)."""
    for m in re.finditer(r'(?<![\w.>])((?:temp|var)_\w+) = ', code):
        i = m.end(); depth = 0
        while i < len(code):
            c = code[i]
            if c in '([':
                depth += 1
            elif c in ')]':
                if depth == 0:
                    break
                depth -= 1
            elif c in ',;' and depth == 0:
                break
            elif c == '\n':
                break
            i += 1
        yield m.group(1), code[m.end():i].strip(), m.start()


def value_writes(code):
    """{local: (value text, position of the first write)} for the m2c temp_ / var_ whose every write is a plain assignment of
    the SAME expression: written once, or -- m2c's var_ set on each branch -- the same text at each write. A name taken from
    the expression's own words then holds at every write. (A compound write, ++/-- or an address taken rules the local out.)"""
    seen = {}
    for t, rhs, pos in assignments(code):
        seen.setdefault(t, []).append((rhs, pos))
    out = {}
    for t, ws in seen.items():
        if len(set(r for r, _ in ws)) == 1:
            out[t] = (ws[0][0], (ws[0][1], len(ws)))
    return out


def no_other_write(t, code, pos_n):
    """the second half of value_writes' test, left to the rule to ask once its pattern has matched (it is the slow part):
    the local has no write besides the plain ones value_writes counted"""
    return write_count(t, code) == pos_n[1]


def root_name(name, all_names):
    """x for x_2, x_3 .. when x itself is a name of the function (the counter convention); else the name as it is
    (record_half_4 or x_minus_1 end in a number that is part of the name)"""
    m = re.fullmatch(r'(.+)_(\d+)', name)
    return m.group(1) if m and int(m.group(2)) >= 2 and m.group(1) in all_names else name


def hand_out(f, info, raw, wanted, out, stats, continue_numbering=False, maxlen=40, extra_ids=()):
    """wanted: [(m2c local, candidate name, position in the code)]"""
    vars_ = info.get('vars', {})
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw)) | set(extra_ids)
    used = set(vars_.values()) | {info['name']} | set(out.get(f, {}).values())
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: x[2]):
        if t in vars_ or t in out.get(f, {}):
            continue
        while True:
            seen[cand] += 1
            nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
            if continue_numbering and nm in used:
                continue
            break
        if (nm in KW or nm in RES or nm in ids or nm in used or M2C.match(nm) or nm.startswith('lift_') or nm.startswith('m2c')
                or not re.fullmatch(r'[a-z_][a-z0-9_]*', nm) or len(nm) > maxlen):
            stats['skipped'] += 1
            continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1


def save(out, stats, filename):
    json.dump({f: {'vars+': v} for f, v in out.items()}, open(T + filename, 'w'), indent=0)
    c = collections.Counter(re.sub(r'_\d+$', '', v) for d in out.values() for v in d.values())
    print(dict(stats), len(out), 'functions')
    print(c.most_common(25))
