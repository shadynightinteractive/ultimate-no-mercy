"""The frame display lists for any count of wrestlers -- the 32-slot work, 2026-11-06.

With five or more wrestlers the frame's display list goes to the engine's two buffers (rw_dltab, rw_dlbase_*), 0x8000
each, after the main arrays below FB0. What a frame uses: 0x20D4 bytes with 6, about 0x5A84-0x60BC with 10 -- about
0x900-0xA00 a wrestler: 0x8000 runs out at about 13. For more than six slots they move out of the main arrays' room
to the expansion RAM that's empty in a match (the mod's notes: 0x806D0000..0x8077FFFF; the announcers' depth image is
at 0x80700000..0x80725800), at 0x80730000, spec.DLBUF_SIZE each: 0x1000 + 0xA00 a slot, never under 0x8000 (0x15000
for 32: the pair ends 0x8075A000, under the engine at 0x80780000). The scheduler task's size field ((end - start) / 8
in 16 signed bits) takes up to 0x3FFF8.
Taken out of the main arrays' room, the arrays' start (spec.EXP) would rise 64 KB -- and the converted code
(lifted.cpp) has their places written in (a 64 KB shift crashed the title earlier the same day): spec.EXP_PIN keeps
it at 0x804B0000 for these layouts, the 64 KB left as room for the arrays to grow. The six-slot build is unchanged."""
import os

W = os.path.dirname(os.path.abspath(__file__))
D = os.path.dirname(os.path.dirname(W))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n' if path.endswith('.py') or path.endswith('.S') else '').write(s)


edit(os.path.join(W, 'spec.py'), [
    ("EXTRA_MAIN = [('dlbuf', 2 * 0x8000)]",
     "EXTRA_MAIN = [('dlbuf', 2 * 0x8000)]\n"
     "DLBUF_SIZE = 0x8000   # (each of the two; past six slots they're elsewhere and bigger: below, patch_dlbuf.py)"),
    ("    EXP = (FB0 - _need(MAIN, EXTRA_MAIN, NSLOTS, 'main')) & ~0xFFFF",
     "    # the frame display lists, past six slots: out of the main arrays' room, to the expansion RAM that's empty in a\n"
     "    # match, 0x1000 + 0xA00 a slot each (about 0x900-0xA00 a wrestler is used), never under 0x8000 (patch_dlbuf.py)\n"
     "    EXTRA_MAIN = []\n"
     "    DLBUF = 0x80730000\n"
     "    DLBUF_SIZE = max(0x8000, (0x1000 + 0xA00 * NSLOTS + 0xFFF) & ~0xFFF)\n"
     "    assert DLBUF + 2 * DLBUF_SIZE <= 0x80780000, 'the frame lists would reach the engine'\n"
     "    # the main arrays' start, kept where the converted code (lifted.cpp) has them: it may go down for more room (and\n"
     "    # then the conversion must be run again -- port/install.py check_lifted stops the install), not up\n"
     "    EXP_PIN = {32: 0x804B0000}\n"
     "    EXP = (FB0 - _need(MAIN, EXTRA_MAIN, NSLOTS, 'main')) & ~0xFFFF\n"
     "    EXP = min(EXP, EXP_PIN.get(NSLOTS, EXP))"),
])
edit(os.path.join(W, 'build_rom.py'), [
    ("EXTRA.update(spec.blocks(0x80700000, 0x80780000, spec.EXTRA_MAIN) if ORACLE else spec.blocks(_MAIN_END, spec.FB0, spec.EXTRA_MAIN))",
     "EXTRA.update(spec.blocks(0x80700000, 0x80780000, spec.EXTRA_MAIN) if ORACLE else spec.blocks(_MAIN_END, spec.FB0, spec.EXTRA_MAIN))\n"
     "# (the frame display lists: in the main arrays' room for six, at spec.DLBUF past it -- patch_dlbuf.py; X_dlbuf2 the second)\n"
     "if 'dlbuf' not in EXTRA: EXTRA['dlbuf'] = spec.DLBUF\n"
     "EXTRA['dlbuf2'] = EXTRA['dlbuf'] + spec.DLBUF_SIZE"),
])
edit(os.path.join(W, 'rw', 'stubs.S'), [
    ('    .word X_dlbuf, X_dlbuf + 0x8000', '    .word X_dlbuf, X_dlbuf2        # (spec.DLBUF_SIZE apart: 0x8000 for six, more past it)'),
])
edit(os.path.join(W, 'port', 'gen_arcade_six.py'), [
    ("      '#define SIX_DLTAB 0x%08Xu           // rw_dltab: u32[2], a six-man frame\\'s display lists (0x8000 each)' % syms['rw_dltab'],",
     "      '#define SIX_DLTAB 0x%08Xu           // rw_dltab: u32[2], a six-man frame\\'s display lists' % syms['rw_dltab'],\n"
     "      '#define SIX_DLBUF_SIZE 0x%Xu        // each of them (spec.DLBUF_SIZE)' % build_rom.spec.DLBUF_SIZE,"),
])
edit(os.path.join(D, 'mod', 'src', 'draw.c'), [
    ("    // a six-man match builds its frames in the engine's own bigger lists (rw_dltab, 0x8000 each)\n"
     "    if (RDS32(SIX_CUR_COUNT) >= 5) { head = RD32(SIX_DLTAB + 4u * (u32)idx); room = 0x8000u; }",
     "    // a six-man match builds its frames in the engine's own bigger lists (rw_dltab, SIX_DLBUF_SIZE each)\n"
     "    if (RDS32(SIX_CUR_COUNT) >= 5) { head = RD32(SIX_DLTAB + 4u * (u32)idx); room = SIX_DLBUF_SIZE; }"),
])
print('applied')
