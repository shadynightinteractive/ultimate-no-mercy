"""page_items' stores by source line (modnative5, 2026-11-13: rule (6), the compound literals' padding): each instruction's line
from the mod's debug build (dwarf/logic.elf, llvm-symbolizer), each instruction from Stage 1's original (same code, -g changes
none of page_items': dwarf_same)."""
import json, os, re, subprocess, sys
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HERE)
import prove_gen as pg
T = os.path.join(pg.DEV, 'toolchain', 'llvm-mingw', 'bin')
inv = json.load(open(os.path.join(HERE, 'inventory.json')))
a = next(k for k, r in inv.items() if r['name'] == 'page_items')
n_ins = int(inv[a]['instructions'])
elf = os.path.join(HERE, 'dwarf', 'logic.elf')
nm = subprocess.run([os.path.join(T, 'llvm-nm.exe'), elf], capture_output=True, text=True).stdout
start = int(re.search(r'^([0-9a-f]+) [tT] page_items$', nm, re.M).group(1), 16)
q = ''.join('0x%X\n' % (start + 4 * k) for k in range(n_ins))
out = subprocess.run([os.path.join(T, 'llvm-symbolizer.exe'), '--obj=' + elf, '--no-inlines'], input=q, capture_output=True, text=True).stdout
blocks = [b for b in out.strip().split('\n\n')]
lines = [int(re.search(r':(\d+):\d+$', b.split('\n')[-1]).group(1)) if re.search(r':(\d+):\d+$', b.split('\n')[-1]) else 0 for b in blocks]
text = pg.original_text(a)
ins = {}
for m in re.finditer(r'// 0x([0-9A-F]{8}): (\w+) +(.*)$', text, re.M):
    ins.setdefault(int(m.group(1), 16), (m.group(2), m.group(3).strip()))
base = int(a, 16)
by_line = {}
for k in range(n_ins):
    mn, ops = ins.get(base + 4 * k, ('?', ''))
    by_line.setdefault(lines[k], []).append((k, mn, ops))
json.dump({str(l): v for l, v in by_line.items()}, open(os.path.join(HERE, 'dev', 'page_sites.json'), 'w'), indent=0)
for l in sorted(by_line)[:400]:
    st = [(mn, ops) for k, mn, ops in by_line[l] if mn in ('sw', 'sh', 'sb')]
    print(l, st)
