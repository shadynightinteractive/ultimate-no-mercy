#!/usr/bin/env python3
"""Which of the mod's data is PRIVATE: read and written by the mod's own code only (2026-11-12, scratch/agents/modglue).

The user chose (memory mod-functions-decisions.md) that, while a native mod function is proved, the mod's private data lives
in PC memory, not at its N64 address. That needs a new tester rule (fuzz_main.cpp, "The mod's private data"), and the rule
needs to know which data is private. This says, symbol by symbol, from the build the tester translates:

  mod.map (mod/build/mod_six)    every data symbol of the mod: its address, size, and output section (.rodata*, .data, .bss)
  modrep_modfuncs.c              the mod's own functions as N64Recomp translates them: every instruction that makes a place in
  modrep_originals.c             the mod's data carries RELOC_HI16 / RELOC_LO16(section, offset), with the MIPS in a comment
  mod.elf's data relocations     (llvm-readelf -r): R_MIPS_32 words in the mod's data that hold the address of another symbol

A symbol is private only when ALL of these hold (anything else stays at its N64 address and is compared as game memory is
today, so a symbol wrongly left out of "private" can only make the proof stricter, never looser):
  1. it is in the mod's data (.rodata*, .data, .bss), not a jump table (.rodata.jt_*: the asm copies' tables, game code reads them);
  2. it is reached at least once, and every reach is from the mod's own C -- a modfn_ function or a C patch's copy (RECOMP_PATCH) --
     never from an edited copy of the game's MIPS (mod.S: game code, which may hand the address on);
  3. every reach is a load or a store through %lo (the instruction carrying RELOC_LO16 is lb/lbu/lh/lhu/lw/lwl/lwr/lwc1/ldc1 or
     sb/sh/sw/swl/swr/swc1/sdc1): the value is used, the address never is. An addiu with %lo makes the address itself, which can
     then be stored, passed to a game function or the port, or returned: the symbol escapes, so it is not private;
  4. no word in the mod's data holds its address (no R_MIPS_32 relocation points into it);
  5. its layout is known: every access is of one width (1, 2, 4 or 8 bytes; not lwl / lwr / swl / swr) at a whole number of
     that width from its start, and its size is a whole number of them. The tester turns each such field between the N64's
     byte order and the PC's, so a wrong layout would be a wrong comparison; one it can't tell stays in game memory.
A symbol no instruction names (reached, if at all, through a neighbour's address) fails 2.

    python tools/lift/private_data.py      writes private_data.json beside it (lift.py's tester reads it) and prints the counts
"""
import json, os, re, subprocess, sys
from collections import defaultdict

LIFT = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.abspath(os.path.join(LIFT, '..', '..'))
OUT = os.path.join(LIFT, 'modrep', 'out')
BUILD = os.environ.get('LIFT_MOD_BUILD') or os.path.join(DEV, 'mod', 'build', 'mod_six')   # (LIFT_MOD_BUILD: another build, as modrep.py takes it -- modnative5, 2026-11-13)
READELF = os.path.join(DEV, 'toolchain', 'llvm-mingw', 'bin', 'llvm-readelf.exe')
LOADS = {'lb', 'lbu', 'lh', 'lhu', 'lw', 'lwl', 'lwr', 'lwc1', 'ldc1'}
# (Each access's width, for the symbol's layout: the tester turns each field between the N64's byte order and the PC's, so
#  it must know the fields. lwl / lwr / swl / swr, half a word each, give no layout.)
WIDTH = {'lb': 1, 'lbu': 1, 'sb': 1, 'lh': 2, 'lhu': 2, 'sh': 2, 'lw': 4, 'sw': 4, 'lwc1': 4, 'swc1': 4, 'ldc1': 8, 'sdc1': 8}
STORES = {'sb', 'sh', 'sw', 'swl', 'swr', 'swc1', 'sdc1'}


def data_symbols():
    """[(address, size, name, output section, object)] for the symbols in the mod's data sections, from mod.map."""
    out, section, obj = [], None, None
    lines = open(os.path.join(BUILD, 'mod.map'), encoding='utf8', errors='replace').read().split('\n')
    for i, line in enumerate(lines):
        m = re.match(r'^([0-9a-f]{8}) [0-9a-f]{8} +([0-9a-f]+) +(\d+) (\S.*)$', line)
        if m:                                   # an output section
            section = m.group(4).strip()
            continue
        m = re.match(r'^([0-9a-f]{8}) [0-9a-f]{8} +([0-9a-f]+) +(\d+) {9}(\S.*)$', line)
        if m:                                   # an input section
            obj = m.group(4).strip()
            continue
        m = re.match(r'^([0-9a-f]{8}) [0-9a-f]{8} +([0-9a-f]+) +(\d+) {17}(\S+)\s*$', line)
        if m and section and (section.startswith('.rodata') or section in ('.data', '.bss', '.sdata', '.sbss')):
            out.append([int(m.group(1), 16), int(m.group(2), 16), m.group(4), section, obj])
    # (A symbol's size: the map gives each symbol's own size where the compiler set one; where it is 0, up to the next one.)
    out.sort()
    for k, s in enumerate(out):
        if s[1] == 0 and k + 1 < len(out):
            s[1] = out[k + 1][0] - s[0]
    return [tuple(s) for s in out]


INS = re.compile(r'^\s*// 0x([0-9A-F]{8}): (\S+)\s*(.*)$')
FUNC = re.compile(r'^RECOMP_FUNC void (\w+)\(')
LO = re.compile(r'RELOC_LO16\((\d+), (0X[0-9A-F]+|\d+)\)')


def text_reaches(sections, c_patch):
    """{address in the mod: [(function, instruction address, mnemonic, kind)]}: each %lo place an instruction makes, once per
    instruction (the translator writes a delay slot's instruction twice). kind: 'mod' (the mod's own C) or 'game' (an asm copy)."""
    found = defaultdict(list)
    for path in (os.path.join(OUT, 'modrep_modfuncs.c'), os.path.join(OUT, 'modrep_originals.c')):
        func, last, seen = None, None, set()
        for line in open(path, encoding='utf8', errors='replace'):
            m = FUNC.match(line)
            if m:
                func, last = m.group(1), None
                continue
            m = INS.match(line)
            if m:
                last = (int(m.group(1), 16), m.group(2))
                continue
            for r in LO.finditer(line):
                if last is None or (func, last[0]) in seen:
                    continue
                seen.add((func, last[0]))
                kind = 'mod' if func.startswith('modfn_') or func in c_patch else 'game'
                found[sections[int(r.group(1))][0] + int(r.group(2), 0)].append((func, last[0], last[1], kind))
    return found


def width_layout(start, size, reaches):
    """[width] * fields: the symbol's layout when every access is of one width at a place that width apart from its start
    and the size is a whole number of them; None otherwise (the layout would need the mod's types)."""
    widths = {WIDTH.get(mn) for _, _, mn, _, at in reaches}
    if len(widths) != 1 or None in widths:
        return None
    w = widths.pop()
    if size % w or any((at - start) % w for _, _, _, _, at in reaches):
        return None
    return [w] * (size // w)


def data_pointers(symbols):
    """The addresses that words in the mod's data hold (R_MIPS_32 relocations outside the code)."""
    elf = os.path.join(BUILD, 'mod.elf')
    image = open(elf, 'rb').read()
    heads = subprocess.run([READELF, '-S', '-W', elf], capture_output=True, text=True).stdout
    where = {}
    for m in re.finditer(r'\]\s+(\S+)\s+\w+\s+([0-9a-f]{8}) ([0-9a-f]{6,8}) ([0-9a-f]{6,8})', heads):
        where[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16))
    rel = subprocess.run([READELF, '-r', '-W', elf], capture_output=True, text=True).stdout
    held, current = set(), None
    for line in rel.split('\n'):
        m = re.match(r"^Relocation section '\.rel(\S+)'", line)
        if m:
            current = m.group(1)
            continue
        m = re.match(r'^([0-9a-f]{8})\s+[0-9a-f]+\s+R_MIPS_32\b', line)
        if m and current in where and not current.startswith('.text'):
            addr, off, _ = where[current]
            at = int(m.group(1), 16)
            word = int.from_bytes(image[off + at - addr: off + at - addr + 4], 'big')
            held.add(word)
    return held


def classify():
    sys.path.insert(0, LIFT)
    import modrep
    sections = json.load(open(os.path.join(OUT, 'modrep_sections.json')))
    c_patch = {n for n, e in modrep.index(OUT).items() if modrep.c_patch(e)}
    symbols = data_symbols()
    reaches = text_reaches(sections, c_patch)
    held = data_pointers(symbols)
    by_symbol = defaultdict(list)
    starts = [s[0] for s in symbols]
    import bisect
    for a, rs in reaches.items():
        k = bisect.bisect_right(starts, a) - 1
        if k >= 0 and a < symbols[k][0] + max(1, symbols[k][1]):
            by_symbol[k] += [r + (a,) for r in rs]
    held_by = defaultdict(int)
    for a in held:
        k = bisect.bisect_right(starts, a) - 1
        if k >= 0 and a < symbols[k][0] + max(1, symbols[k][1]):
            held_by[k] += 1
    out = []
    for k, (a, size, name, section, obj) in enumerate(symbols):
        rs = by_symbol.get(k, [])
        why = None
        layout = width_layout(a, size, rs) if rs else None
        if section.startswith('.rodata.jt'):
            why = 'a jump table'
        elif not rs:
            why = 'no instruction names it'
        elif any(kind == 'game' for _, _, _, kind, _ in rs):
            why = 'an edited copy of the game\'s code names it'
        elif any(mn not in LOADS | STORES for _, _, mn, _, _ in rs):
            why = 'its address is made (%s)' % sorted({mn for _, _, mn, _, _ in rs if mn not in LOADS | STORES})[0]
        elif held_by.get(k):
            why = 'a word in the mod\'s data holds its address'
        elif layout is None:
            why = "accessed at more than one width, or off its fields: its layout needs the mod's types"
        out.append({'name': name, 'address': '%08X' % a, 'size': size, 'section': section, 'object': obj,
                    'private': why is None, 'why_not': why, 'reaches': len(rs), 'widths': layout if why is None else None,
                    'written': any(mn in STORES for _, _, mn, _, _ in rs)})
    return out


if __name__ == '__main__':
    rows = classify()
    json.dump(rows, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'private_data.json'), 'w'), indent=0)
    from collections import Counter
    print('data symbols: %d, %d bytes' % (len(rows), sum(r['size'] for r in rows)))
    p = [r for r in rows if r['private']]
    print('private: %d, %d bytes (%d written by the mod, %d only read)' % (len(p), sum(r['size'] for r in p),
          sum(r['written'] for r in p), sum(not r['written'] for r in p)))
    for why, n in Counter(r['why_not'] for r in rows if not r['private']).most_common():
        print('  not private, %s: %d' % (why, n))
