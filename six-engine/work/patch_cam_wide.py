"""The camera's view of the wrestlers for any count -- the 32-slot work, 2026-11-06.

func_80116130's u16 mask has the six-slot layout: bits 0-5 slots in view, 0x40 << slot out of play, flags 0x1000 /
0x2000. With 7 or more, slot 6 in view reads as "slot 0 out of play", slot 6 out of play as a flag: the first 10-man
Rumble probes' camera framed a garbled set and ended up on the empty ring floor. Its readers all test a slot as
`(mask >> slot) & 1`, which a 32-bit set does for 32 slots, so:
  - func_80116130 keeps its u16 (the HUD, func_800DEDE0, and the tag-type matches' paths read it, six at most) and
    also keeps two 32-bit sets, rw_cam_view (in play and in view) and rw_cam_play (not out of play: what the old
    `(m ^ 0xFC0) >> 6` gave -- an empty slot counts, the loops skip it on 0x801898D0);
  - func_800E2344 (the per-frame camera) and func_800E38A4 read rw_cam_view for their per-slot tests (stubs
    rw_cam_view_a1 / rw_cam_view_s5, and the two `andi x, a1, 0xFFFF` copies made whole);
  - func_800E28C0 / func_800E30D8 (the framing): which set they frame is chosen as before -- in play when match flag
    0x4000 or rule 0x100000, else in view, all when none -- from the wide sets (rw_cam_sel_a3 / rw_cam_sel_a0); the
    tag-type matches (match flags & 0x01C82000: func_800EE594's own mask) keep the old value, as before;
  - func_800E2344 lists the in-view slots on its stack at sp+0x10, room for 8 before its saved registers (sp+0x20):
    its frame grows by 0x40 (0x48 -> 0x88; room for 40), the list staying where it is.
For six or fewer every slot's bit is what it was, so the camera does what it did."""
import os

W = os.path.dirname(os.path.abspath(__file__))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)


edit(os.path.join(W, 'rw', 'r_80116130.c'), [
    (' * the count, as the original\'s `(m ^ 0x3C0) >> 6` with 4). The consumers\' masks: build_rom.MASK_IMMS. */',
     ' * the count, as the original\'s `(m ^ 0x3C0) >> 6` with 4). The consumers\' masks: build_rom.MASK_IMMS.\n'
     ' * Past six (2026-11-06, patch_cam_wide.py): the u16 has slots 0-5 only (the HUD and the tag-type matches read it), and\n'
     ' * the camera\'s per-slot readers take two 32-bit sets kept here: rw_cam_view (in play, in view) and rw_cam_play (not\n'
     ' * out of play -- an empty slot counts, as in the old `(m ^ 0xFC0) >> 6`). */'),
    ('REWRITE(80116130, u16, (void))\n{\n    s32 n = nslots(), i;\n    u32 m = 0;\n',
     'u32 rw_cam_view, rw_cam_play;\n\nREWRITE(80116130, u16, (void))\n{\n    s32 n = nslots(), i;\n    u32 m = 0, view = 0, play = 0;\n'),
    ('        if (*(volatile s16 *)(w + 0xA0) == -1) continue;\n'
     '        if (V16(w + 0x94) & 1) { m |= 0x40u << i; continue; }\n'
     '        if (!(V8(w + 0xE9) & 0x10)) continue;\n'
     '        m |= 1u << i;\n',
     '        if (*(volatile s16 *)(w + 0xA0) == -1) { play |= 1u << i; continue; }\n'
     '        if (V16(w + 0x94) & 1) { if (i < 6) m |= 0x40u << i; continue; }\n'
     '        play |= 1u << i;\n'
     '        if (!(V8(w + 0xE9) & 0x10)) continue;\n'
     '        view |= 1u << i;\n'
     '        if (i < 6) m |= 1u << i;\n'),
    ('    for (; i < 6; i++) m |= 0x40u << i;\n    return (u16)m;',
     '    for (; i < 6; i++) m |= 0x40u << i;\n    rw_cam_view = view;\n    rw_cam_play = play;\n    return (u16)m;'),
])

SEL = '''    lui   $v0, 0x8009
    lw    $v0, 0x7EC0($v0)          # the match flags
    lui   $v1, 0x01C8
    ori   $v1, $v1, 0x2000
    and   $v1, $v0, $v1
    beqz  $v1, 1f
    nop
    jr    $ra                       # a tag-type match: func_800EE594's mask, as before
    andi  OUT, $a0, 0xFFFF
1:  andi  $v1, $v0, 0x4000
    bnez  $v1, 2f
    lui   $v1, 0x800B
    lw    $v1, -0x66B0($v1)         # the rules (0x800A9950)
    lui   $at, 0x10
    and   $v1, $v1, $at
    bnez  $v1, 2f
    nop
    lui   $at, %hi(rw_cam_view)
    b     3f
    lw    OUT, %lo(rw_cam_view)($at)
2:  lui   $at, %hi(rw_cam_play)
    lw    OUT, %lo(rw_cam_play)($at)
3:  bnez  OUT, 4f
    nop
    addiu OUT, $zero, -1            # none: all
4:  jr    $ra
    nop
'''
edit(os.path.join(W, 'rw', 'stubs.S'), [
    ('''    .data
    .globl rw_seq_lock''', '''# The camera's per-slot sets for any count (patch_cam_wide.py has the story; rw_cam_view / rw_cam_play from
# rw/r_80116130.c, kept by func_80116130 each call).
# func_800E2344, after its func_80116130: was `addu a1, v0, zero; addu s0, zero, zero` -> a1 = the in-view set
    .globl rw_cam_view_a1
rw_cam_view_a1:
    lui   $at, %hi(rw_cam_view)
    lw    $a1, %lo(rw_cam_view)($at)
    jr    $ra
    move  $s0, $zero
# func_800E38A4, after its func_80116130: was `andi s5, v0, 0xFFFF` -> s5 = the in-view set
    .globl rw_cam_view_s5
rw_cam_view_s5:
    lui   $at, %hi(rw_cam_view)
    jr    $ra
    lw    $s5, %lo(rw_cam_view)($at)
# func_800E28C0's framing set: was `andi a3, a0, 0xFFFF` (a0 the set it chose: in play when match flag 0x4000 or
# rule 0x100000, else in view; none -> all) -> a3 the same choice from the wide sets. at, v0, v1 are free there.
    .globl rw_cam_sel_a3
rw_cam_sel_a3:
''' + SEL.replace('OUT', '$a3') + '''# func_800E30D8's, the same: was `andi a0, a0, 0xFFFF` -> a0. at, v0, v1 are free there.
    .globl rw_cam_sel_a0
rw_cam_sel_a0:
''' + SEL.replace('OUT', '$a0') + '''
    .data
    .globl rw_seq_lock'''),
])

FRAME = [  # func_800E2344's frame, 0x48 -> 0x88 (its saves +0x40; the list at sp+0x10 stays)
    (0x800E234C, 0x27BDFFB8, 0x27BDFF78, 'addiu sp, sp, -0x88'),
    (0x800E2350, 0xAFB50034, 0xAFB50074, 'sw s5'), (0x800E2358, 0xAFB20028, 0xAFB20068, 'sw s2'),
    (0x800E2360, 0xAFBF0038, 0xAFBF0078, 'sw ra'), (0x800E2364, 0xAFB40030, 0xAFB40070, 'sw s4'),
    (0x800E2368, 0xAFB3002C, 0xAFB3006C, 'sw s3'), (0x800E236C, 0xAFB10024, 0xAFB10064, 'sw s1'),
    (0x800E2370, 0xAFB00020, 0xAFB00060, 'sw s0'), (0x800E2374, 0xF7B40040, 0xF7B40080, 'sdc1 f20'),
    (0x800E2798, 0x8FBF0038, 0x8FBF0078, 'lw ra'), (0x800E279C, 0x8FB50034, 0x8FB50074, 'lw s5'),
    (0x800E27A0, 0x8FB40030, 0x8FB40070, 'lw s4'), (0x800E27A4, 0x8FB3002C, 0x8FB3006C, 'lw s3'),
    (0x800E27A8, 0x8FB20028, 0x8FB20068, 'lw s2'), (0x800E27AC, 0x8FB10024, 0x8FB10064, 'lw s1'),
    (0x800E27B0, 0x8FB00020, 0x8FB00060, 'lw s0'), (0x800E27B4, 0xD7B40040, 0xD7B40080, 'ldc1 f20'),
    (0x800E27BC, 0x27BD0048, 0x27BD0088, 'addiu sp, sp, 0x88'),
]
seqs = ('    # func_800E2344 / func_800E38A4 / func_800E28C0 / func_800E30D8: the camera\'s per-slot sets for any count\n'
        '    # (2026-11-06, patch_cam_wide.py): the in-view set\'s copies made whole (`andi x, a1, 0xFFFF` -> `addu x, a1, zero`)\n'
        "    ('ovl_e', 0x800E23FC, 0x30A6FFFF, 0x00A03021),     # addu a2, a1, zero\n"
        "    ('ovl_e', 0x800E2504, 0x30B3FFFF, 0x00A09821),     # addu s3, a1, zero\n"
        '    # func_800E2344\'s frame 0x48 -> 0x88: its in-view list (sp+0x10) had room for 8 before the saves\n'
        + ''.join("    ('ovl_e', 0x%08X, 0x%08X, 0x%08X),     # %s\n" % f for f in FRAME))
edit(os.path.join(W, 'build_rom.py'), [
    ('SEQS = [\n', 'SEQS = [\n' + seqs),
    ("    ('ovl_e', 0x8011E0EC, (0x00031827, 0x00431024), 'rw_seq_release'),\n]",
     "    ('ovl_e', 0x8011E0EC, (0x00031827, 0x00431024), 'rw_seq_release'),\n"
     "    # the camera's per-slot sets (2026-11-06, patch_cam_wide.py): func_800E2344's `addu a1, v0, zero; addu s0, zero, zero`\n"
     "    ('ovl_e', 0x800E23F4, (0x00402821, 0x00008021), 'rw_cam_view_a1'),\n]"),
    ("STUBS = [\n",
     "STUBS = [\n"
     "    # the camera's per-slot sets (2026-11-06, patch_cam_wide.py): func_800E38A4's `andi s5, v0, 0xFFFF`, and\n"
     "    # func_800E28C0's / func_800E30D8's framing set (`andi a3, a0, 0xFFFF` / `andi a0, a0, 0xFFFF`)\n"
     "    ('ovl_e', 0x800E3D10, (0x00008821, 0x3055FFFF), 'rw_cam_view_s5'),\n"
     "    ('ovl_e', 0x800E2984, (0x00002821, 0x3087FFFF), 'rw_cam_sel_a3'),\n"
     "    ('ovl_e', 0x800E31A0, (0x250898D0, 0x3084FFFF), 'rw_cam_sel_a0'),\n"),
])
print('applied')
