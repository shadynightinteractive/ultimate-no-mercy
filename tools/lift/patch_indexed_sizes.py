"""lift.py restore_lost_sizes: an indexed fixed table too. m2c wrote func_800E30D8's `lh v0, -0x6730(at)` (at = 0x80190000
+ the slot * 2) as *(0x801898D0 + (var_v1 * 2)) -- a number dereferenced, which doesn't compile ("does not compile",
2026-11-06). The instruction still says the size: a register made by adding an index to a known address is followed
as that table, and the read's size goes onto the expression: *(s16 *)(0x801898D0 + (var_v1 * 2))."""
import os

p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lift.py')
s = open(p, encoding='utf-8').read()


def sub(a, b):
    global s
    assert s.count(a) == 1, a[:70]
    s = s.replace(a, b)


sub("""    if '*(void *)0x' not in c and not re.search(r'(?<![\\w)])\\*0x[0-9A-Fa-f]{8}', c):
        return c
    known = {}
    values = {}""", """    if '*(void *)0x' not in c and not re.search(r'(?<![\\w)])\\*0x[0-9A-Fa-f]{8}', c) and not re.search(r'\\*\\(0x[0-9A-Fa-f]{8} \\+ ', c):
        return c
    known = {}
    values = {}
    indexed = {}   # (a register holding a known address plus an index: the table's address)""")
sub("""        if mn == 'addiu' and len(regs) == 2 and regs[1] in values:
            values[regs[0]] = (values[regs[1]] + int(ops.split(',')[-1], 0)) & 0xFFFFFFFF
            continue""", """        if mn == 'addiu' and len(regs) == 2 and regs[1] in values:
            values[regs[0]] = (values[regs[1]] + int(ops.split(',')[-1], 0)) & 0xFFFFFFFF
            continue
        if mn == 'addu' and len(regs) == 3 and (regs[1] in values) != (regs[2] in values):
            indexed[regs[0]] = values[regs[1] if regs[1] in values else regs[2]]
            values.pop(regs[0], None)
            continue""")
sub("""        if mn in SIZES and place and place.group(2) in values:
            address = (values[place.group(2)] + int(place.group(1), 0)) & 0xFFFFFFFF""",
    """        if mn in SIZES and place and (place.group(2) in values or place.group(2) in indexed):
            address = ((values[place.group(2)] if place.group(2) in values else indexed[place.group(2)]) + int(place.group(1), 0)) & 0xFFFFFFFF""")
sub("""        for r in uses_defs(mn, ops)[1]:
            values.pop(r, None)
    def one(m):""", """        for r in uses_defs(mn, ops)[1]:
            values.pop(r, None)
            indexed.pop(r, None)
    def one(m):""")
sub("""    c = re.sub(r'\\*\\(void \\*\\)(0x[0-9A-Fa-f]+)', one, c)
    return re.sub(r'(?<![\\w)])\\*(0x[0-9A-Fa-f]{8})\\b', one, c)""", """    c = re.sub(r'\\*\\(void \\*\\)(0x[0-9A-Fa-f]+)', one, c)
    def table(m):   # (*(0xADDR + index) -> *(T *)(0xADDR + index))
        t = known.get(int(m.group(1), 16))
        return '*(%s *)(%s + ' % (t, m.group(1)) if t else m.group(0)
    c = re.sub(r'\\*\\((0x[0-9A-Fa-f]{8}) \\+ ', table, c)
    return re.sub(r'(?<![\\w)])\\*(0x[0-9A-Fa-f]{8})\\b', one, c)""")
open(p, 'w', encoding='utf-8', newline='').write(s)
print('applied')
