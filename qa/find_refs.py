"""Which translated functions build an address in a range (lui + an offset: a load, a store or an addiu), and in which
file -- for finding who uses a table.
    python qa\\find_refs.py 800F5E70 800F5E80"""
import glob
import os
import re
import sys

lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'exe', 'WWFNoMercyRecomp', 'RecompiledFuncs')
pat_lui = re.compile(r'// 0x([0-9A-F]{8}): lui\s+\$(\w+), (0x[0-9A-F]+)')
pat_mem = re.compile(r'// 0x([0-9A-F]{8}): (\w+)\s+\$(\w+), (-?0x[0-9A-F]+|-?\d+)\(\$(\w+)\)')
pat_imm = re.compile(r'// 0x([0-9A-F]{8}): (?:addiu|ori)\s+\$(\w+), \$(\w+), (-?0x[0-9A-F]+|-?\d+)')


def num(t):
    return int(t, 16) if 'x' in t else int(t)


hits = set()
for f in glob.glob(os.path.join(root, '*.c')):
    cur = None
    lines = open(f, encoding='utf-8', errors='replace').read().splitlines()
    for i, l in enumerate(lines):
        m = re.match(r'RECOMP_FUNC void (\w+)\(', l)
        if m:
            cur = m.group(1)
            continue
        m = pat_lui.search(l)
        if not m:
            continue
        reg, h = m.group(2), int(m.group(3), 16)
        for l2 in lines[i + 1:i + 16]:
            u = pat_mem.search(l2)
            off = None
            if u and u.group(5) == reg:
                off = num(u.group(4))
            else:
                u = pat_imm.search(l2)
                if u and u.group(3) == reg:
                    off = num(u.group(4))
            if off is None:
                continue
            a = ((h << 16) + off) & 0xFFFFFFFF
            if lo <= a < hi:
                hits.add((cur, os.path.basename(f), hex(a)))
            break
for h in sorted(hits):
    print(*h)
