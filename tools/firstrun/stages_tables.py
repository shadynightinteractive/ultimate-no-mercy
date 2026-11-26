#!/usr/bin/env python3
"""stages_tables.py: the stage sets' tables for the exe's first start, written from the project's own Python source
(firstrun, 2026-11-17).

  python tools/firstrun/stages_tables.py      -> exe/WWFNoMercyRecomp/src/main/arcade_firstrun_stages_tables.inc

mod/gen/arenas_stages.py makes venues 1-9's stage sets from its SPEC (arenas_stages_spec.py: the transforms, what is kept,
cut, dropped, packed -- our decisions), the close scenes' lists (arenas_stages_tab.py: which of the game's files each close
scene draws, where) and NEW (the arena's own new pieces). arcade_firstrun_stages.cpp makes them again in C++ from the same
tables: this writes SPEC and NEW as C++ data, so the two never disagree. Run it after changing either; the .inc is tracked
(it holds our settings and the file numbers they name, no game data).
[stagelists 2026-11-19] The close scenes' lists are the game's own tables: not written here any more (the .inc held them as
CLOSE_SCENES until 2026-11-19) -- both arenas_stages_tab.py and arcade_firstrun_stages.cpp (close_scenes) read them from the
No Mercy ROM.
"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(SRC, 'mod', 'gen'))
import arenas_stages as A                      # noqa: E402  (SPEC filled by arenas_stages_spec on import)

OUT = os.path.join(SRC, 'exe', 'WWFNoMercyRecomp', 'src', 'main', 'arcade_firstrun_stages_tables.inc')


def pcs(lst): return '{ %s }' % ', '.join('{ 0x%X, 0x%X }' % (f, p) for f, p in lst)
def ints(lst): return '{ %s }' % ', '.join(str(x) for x in lst)
def num(x): return repr(float(x))


def main():
    L = ['// Written by tools/firstrun/stages_tables.py from mod/gen/arenas_stages_spec.py and arenas_stages.py NEW (the close',
         '// scenes are read from the ROM: arcade_firstrun_stages.cpp close_scenes): do not edit -- change those and run it again.',
         '// (arcade_firstrun_stages.cpp includes it.)',
         '']
    L.append('const Spec SPECS[] = {')
    for v in sorted(A.SPEC):
        sp = A.SPEC[v]
        keeps = []
        for mesh, pal, where in sp.get('keep', ()):
            ms = pcs(mesh) if isinstance(mesh, tuple) else '{}'
            single = -1 if isinstance(mesh, tuple) else mesh
            if where == 'start': wk, wf = 0, 0
            elif where == 'end': wk, wf = 1, 0
            elif isinstance(where, tuple): wk, wf = 2, where[1]
            else: wk, wf = 3, where
            keeps.append('{ %s, %d, %d, %d, 0x%X }' % (ms, single, -1 if pal is None else pal, wk, wf))
        clips = []
        for ids, axis, op, at in sp.get('clip', ()):
            clips.append('{ %s, %s, %d, %s, %s }' % ('{}' if ids == 'all' else ints(sorted(ids)), 'true' if ids == 'all' else 'false',
                                                     axis, 'true' if op == '>=' else 'false', num(at)))
        cos = sp.get('cos', {})
        cos_ids = ['{ 0x%X, %s }' % (k, num(x)) for k, x in cos.items() if k != 'all']
        packs = []
        for W, H, cells, exact in sp.get('pack', ()):
            assert exact is None, 'a pack with its own palette: not ported'
            packs.append('{ %d, %d, { %s } }' % (W, H, ', '.join('{ 0x%X, %d, %d, %d, %d }' % c for c in cells)))
        for k in sp:
            assert k in ('s', 'd', 'z1', 'keep', 'drop', 'clip', 'cos', 'order', 'alone', 'bands', 'band_pals', 'pack', 'screen'), \
                'SPEC[%d] has %r, which the C++ port does not do yet' % (v, k)
        L.append('    { %d, %s, { %s }, %d, { %s }, %s, { %s }, { %s }, %s, { %s }, %s, %s, %s, { %s } },' % (
            v, num(sp['s']), ', '.join(num(x) for x in sp['d']), sp['z1'], ', '.join(keeps), ints(sorted(sp.get('drop', ()))),
            ', '.join(clips), ', '.join(cos_ids), num(cos['all']) if 'all' in cos else '-1.0',
            ', '.join('{ 0x%X, 0x%X }' % kv for kv in sp.get('order', {}).items()), ints(sorted(sp.get('alone', ()))),
            ints(sp.get('bands', ())), ints(sorted(sp.get('band_pals', ()))), ', '.join(packs)))
    L.append('};')
    L.append('const NewPieces NEW_PIECES[] = {')
    for v, n in sorted(A.NEW.items()):
        L.append('    { %d, %s, %d, %d, 0x%X, 0x%X, %d },' % (v, ints(n['screen']), n['z'], n['beam'], n['screen_pic'], n['truss_pic'], n['grey']))
    L.append('};')
    L.append('constexpr int NEW_SCREEN = 0x%X;' % A.NEW_SCREEN)
    L.append('constexpr double COS_MIN = %s;' % num(A.COS_MIN))
    open(OUT, 'w', newline='\n').write('\n'.join(L) + '\n')
    print('written', OUT)


if __name__ == '__main__':
    main()
