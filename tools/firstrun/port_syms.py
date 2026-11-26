"""port_syms.py: the symbols port_test.cpp needs, read from the mod's build (firstrun, 2026-11-17).

    python tools/firstrun/port_syms.py [mod/build/mod_six/mod.elf] [out.txt (default mod/build/mod_six/port_syms.txt)]

Writes one line per symbol, "name address size" (hex address, decimal size): the walls and ringstage ports' own tables
(xw0_rec .. xw9_rec, xrs0_cells .. xrs9_cells, xrs_arenas) and the stage sets' file lists they read (xraw_set_*,
xstage*_mesh / _pal, xstage_n, xstage5_close_*)."""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import modelf  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
WANT = re.compile(r'^(xw\d_rec|xrs\d_cells|xrs_arenas|xraw_set_mesh|xraw_set_pal|xstage\d?_mesh|xstage\d_pal|xstage_n|xstage5_close_\w+)$')


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'mod', 'build', 'mod_six', 'mod.elf')
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'mod', 'build', 'mod_six', 'port_syms.txt')
    b, secs, syms = modelf.read_elf(elf)
    seen = {}
    for s in syms:
        if WANT.match(s['name']) and s['alloc']:
            assert s['name'] not in seen or seen[s['name']] == (s['addr'], s['size']), 'two symbols named ' + s['name']
            seen[s['name']] = (s['addr'], s['size'])
    lines = ['%s %08X %d' % (n, a, z) for n, (a, z) in sorted(seen.items(), key=lambda kv: kv[1][0])]
    open(out, 'w').write('\n'.join(lines) + '\n')
    print('%d symbols -> %s' % (len(lines), out))


if __name__ == '__main__':
    main()
