# (fablenames, 2026-11-14) Rule X2, rules X (exprvars.py) and M (maskvars.py) carried on to the operands and operators they
# leave: an m2c temp or var written only by (once, or the same text at every write: rulelib.value_writes) `t = X op C` (a statement or a comma-expression step) (casts allowed on X and on the whole), C a
# constant, and read at least once, is named by the expression's own words. X is ONE named value:
#     a named local or parameter (x for x_2 beside an x)       GAME_VAR(name) / GAME_VAR_AS(name, type): name
#     KIND(address, name) / KIND_AS(address, name, type): the field's name (rules X and M take only locals / only `&`)
# and the name is, as rules X and M write them (constants in decimal):
#     X + c, X - c (c 1..16)  x_plus_c, x_minus_c        X / c  x_over_c        X % c  x_mod_c
#     X * c                   x_times_c                  X << c (c 1..12) x_times_<2^c> (a left shift multiplies)
#     X & one bit             x_bit<n>                   X & 0xFF x_low_byte    X & 0xFFFF x_low_half
#     X & (2^n - 1)           x_low_n_bits
# Shapes rules X and M already name (their temps are named by now) are not reached again; `>>` and masks that are not the low
# bits or one bit are left: their meaning is not in the expression. _2, _3 in order of the code; clashes and names over 40
# characters are skipped. The release's fielded C++. Writes exprs2_edits.json.
import re, collections
from rulelib import *
import lift
names, ch, merged, cands = load()
fielded = fielder(merged, cands)
kinds, sources = lift.read_fields()
PLAIN = set()
for line in lift.fields_preamble(kinds, sources):
    m = re.match(r'#define (\w+)\(([^)]*)\) ', line)
    if m and [p.strip() for p in m.group(2).split(',')][:2] == ['address', 'name']:
        PLAIN.add(m.group(1))
out = {}; stats = collections.Counter()
RHS = re.compile(r'%s\(?%s(\w+|[A-Z][A-Z0-9_]*\([^()]*\))\)? (\+|-|\*|/|%%|<<|&) (0x[0-9A-F]+|\d+)U?' % (CAST, CAST))
for f, info, raw in functions(merged, cands):
    vars_ = info.get('vars', {})
    fc = fielded(f, info)
    wanted = []
    for t, (rhs, pos) in value_writes(fc).items():
        m = RHS.fullmatch(rhs)
        if not m or t in vars_:
            continue
        x, op, c = m.groups()
        c = int(c, 0)
        mm = re.fullmatch(r'([A-Z][A-Z0-9_]*)\(([^()]*)\)', x)
        if mm:
            args = [a.strip() for a in mm.group(2).split(',')]
            if mm.group(1) in ('GAME_VAR', 'GAME_VAR_AS'):
                xn = args[0]
            elif mm.group(1) in PLAIN and len(args) >= 2:
                xn = args[1]
            else:
                continue
        elif x in vars_:
            xn = root_name(vars_[x], set(vars_.values()))
        else:
            continue
        if not re.fullmatch(r'[a-z_][a-z0-9_]*', xn):
            continue
        if op in '+-':
            if not 1 <= c <= 16: continue
            nm = '%s_%s_%d' % (xn, 'plus' if op == '+' else 'minus', c)
        elif op == '*':
            nm = '%s_times_%d' % (xn, c)
        elif op == '<<':
            if not 1 <= c <= 12: continue
            nm = '%s_times_%d' % (xn, 1 << c)
        elif op == '/':
            nm = '%s_over_%d' % (xn, c)
        elif op == '%':
            nm = '%s_mod_%d' % (xn, c)
        else:
            if c == 0xFF: nm = xn + '_low_byte'
            elif c == 0xFFFF: nm = xn + '_low_half'
            elif c and c & (c - 1) == 0: nm = '%s_bit%d' % (xn, c.bit_length() - 1)
            elif c and c & (c + 1) == 0: nm = '%s_low_%d_bits' % (xn, c.bit_length())
            else: continue
        if not no_other_write(t, fc, pos) or read_count(t, fc) < 1:
            continue
        wanted.append((t, nm, pos[0]))
    hand_out(f, info, raw, wanted, out, stats, continue_numbering=True)
save(out, stats, 'exprs2_edits.json')
