# The unreachable-line checker's other ways into each function (unreach.py): every jump, branch, call, built address and jump-table
# entry in the game's code that lands inside a function other than at its start, and every word of a running game's memory that
# could be such an address. They only make the checker more careful: an instruction reachable from any of them is never called dead.
# Written to unreach_roots.json, which lift.py reads (and which is part of the tester's cache key). Taken from
# scratch/lift_unreach/run.py, the version the review checked (2026-11-09).
#   python tools/lift/unreach_roots.py <state.rdram.bin from a QA run>
import collections, json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import to_asm, unreach
from mips import BRANCH

OUT = os.path.join(HERE, 'unreach_roots.json')


def snapshot_words(path):
    raw = open(path, 'rb').read()
    out = set()
    for i in range(0, len(raw) & ~3, 4):
        v = int.from_bytes(raw[i:i + 4], 'little')
        if 0x80000000 <= v < 0x80400000 and not v & 3:
            out.add(v)
    return out


def entries(fs, words):
    by_addr = collections.defaultdict(set)
    for n, f in fs.items():
        for a in f['ins']:
            by_addr[a].add(n)
    pieces_of = {}
    for n, f in fs.items():
        for p in f.get('pieces', [n]):
            pieces_of.setdefault(p, set()).add(n)
    roots = collections.defaultdict(set)
    starts = {min(f['ins']) for f in fs.values() if f['ins']}

    def enter(a, source, kind):
        if a in starts:
            return
        own = kind in ('jump', 'table') and source in fs and a in fs[source]['ins']
        for n in by_addr.get(a, ()):
            if n == source:
                continue
            if own and not fs[n]['ins'].keys() <= fs[source]['ins'].keys():
                continue
            if own and n in pieces_of.get(source, ()) and source in pieces_of.get(n, ()):
                continue
            if kind in ('jump', 'table') and not own and source in fs[n].get('pieces', []):
                continue
            roots[n].add(a)

    for g, f in fs.items():
        lui = {}
        for a in sorted(f['ins']):
            mn, ops = f['ins'][a]
            if mn in BRANCH or mn in ('j', 'b'):
                t = unreach.target(ops)
                if t is not None:
                    enter(t, g, 'jump')
            elif mn == 'jal':
                t = unreach.target(ops)
                if t is not None:
                    enter(t, g, 'call')
            r = unreach.REG.findall(ops)
            if mn == 'lui' and r:
                m = re.search(r',\s*(0[xX][0-9A-Fa-f]+|\d+)$', ops)
                if m:
                    lui[r[0]] = int(m.group(1), 0) << 16
            elif mn in ('addiu', 'ori') and len(r) == 2 and r[1] in lui:
                m = re.search(r',\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))$', ops)
                if m:
                    k = int(m.group(1), 0)
                    v = (lui[r[1]] + unreach.sext16(k)) if mn == 'addiu' else (lui[r[1]] | (k & 0xFFFF))
                    enter(v & 0xFFFFFFFF, g, 'built')
        for table in f['tables'].values():
            for t in table.values():
                enter(t, g, 'table')
    for v in words:
        enter(v, '', 'data')
    return roots


if __name__ == '__main__':
    fs = {n: f for n, f in to_asm.read_functions().items() if not n.startswith('modrep_')}
    roots = entries(fs, snapshot_words(sys.argv[1]))
    json.dump({'snapshot': os.path.basename(os.path.dirname(os.path.abspath(sys.argv[1]))),
               'roots': {n: sorted(v) for n, v in sorted(roots.items())}}, open(OUT, 'w', newline='\n'), indent=0)
    print('functions with other ways in:', len(roots), '->', OUT)
