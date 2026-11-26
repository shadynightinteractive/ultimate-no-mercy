# (fablenames, 2026-11-14) Rule RF: an m2c temp or var written only by (once, or the same text at every write: rulelib.value_writes) `t = M2C_FIELD(base, P<T>, OFF)` (one cast
# allowed; a whole statement or a step of a condition's comma expression, rulelib.assignments) that the release's fielded C++ still shows as M2C_FIELD -- fields.json has no name for that place -- where
# base is a NAMED local or parameter and OFF a constant not below 0, and read at least once, holds the unnamed value at that
# offset of `base`. The name says exactly that and no more (PROMPT: `value_at_0x2c` beats a wrong `health`), in the shape the
# given hand names already use (bracket_entry_byte_2, header_word_4, part_option_byte_1):
#     <base>_<width>_<offset in hex>      width by T: byte (s8 u8), half (s16 u16), word (s32 u32 and pointers), float (f32),
#                                         double (f64)                  wrestler_word_2c, row_byte_3, his_record_float_44
# base's own _2.. counter is dropped when the function has the name without it (x_2 beside x: the same kind of thing). _2, _3 in order of the code: the same place read again.
# Also for the global places: `t = *(P<T> )0xADDRESS;` that the fielded C++ leaves as a number (no GAME_VAR for it) gives
# <width>_<address in hex>: word_800a945c, float_80062b60. Clashes and names over 40 characters are skipped.
# And `t = *p` (one cast allowed), p a named local or parameter: what p points at, named <p>_value.
# Writes rawfield_edits.json.
import re, collections
from rulelib import *
names, ch, merged, cands = load()
fielded = fielder(merged, cands)
WIDTH = {'s8': 'byte', 'u8': 'byte', 's16': 'half', 'u16': 'half', 's32': 'word', 'u32': 'word', 'f32': 'float', 'f64': 'double'}
def width(ty):
    ty = ty.strip()
    return WIDTH.get(ty) or ('word' if ty.startswith('P<') else None)
out = {}; stats = collections.Counter()
for f, info, raw in functions(merged, cands):
    vars_ = info.get('vars', {})
    fc = fielded(f, info)
    wanted = []
    for t, (rhs, pos) in value_writes(fc).items():
        if t in vars_:
            continue
        m = re.fullmatch(r'%sM2C_FIELD\((\w+), P<([\w<> ]+)> ?, (0x[0-9A-F]+|\d+)\)' % CAST, rhs)
        if m:
            base, ty, off = m.groups()
            if base not in vars_ or not width(ty):
                continue
            nm = '%s_%s_%x' % (root_name(vars_[base], set(vars_.values())), width(ty), int(off, 0))
        elif re.fullmatch(r'%s\*(\w+)' % CAST, rhs):
            base = re.fullmatch(r'%s\*(\w+)' % CAST, rhs).group(1)
            if base not in vars_:
                continue
            nm = root_name(vars_[base], set(vars_.values())) + '_value'
        else:
            m = re.fullmatch(r'%s\*\(P<([\w<> ]+)> ?\) ?0x(80[0-9A-F]{6})' % CAST, rhs)
            if not m or not width(m.group(1)):
                continue
            nm = '%s_%s' % (width(m.group(1)), m.group(2).lower())
        if not no_other_write(t, fc, pos) or read_count(t, fc) < 1:
            continue
        wanted.append((t, nm, pos[0]))
    hand_out(f, info, raw, wanted, out, stats, continue_numbering=True)
save(out, stats, 'rawfield_edits.json')
