"""The instant replay's per-slot bits for any count of wrestlers -- the 32-slot work, 2026-11-07.

The replay's word 0x80162A7C (F) has a bit per slot for "this slot's move is replay-worthy" (rw_tab_DB60: 0x4000..
0x20000 for 0..3, 0x200000 / 0x400000 for 4, 5) and one for "snap its animation" (rw_tab_DB78). About 20 move
functions clear a slot's worthy bit (`F &= ~DB60[slot]`), func_800E5890 sets it (`F |= DB60[slot]`), and the recorder
(rw/r_8013092C.c) tests it. The tables had six entries: slots 6 on read the next tables' words as bits -- 0x02000100,
0x04000300 ... -- and set and cleared F's own 0x100 / 0x200 (the replay's state) at random: the 16-man probe's
wrestlers ended up walking off the ring at its height after the replays began (all 16 thousands of units out).
Now, for more than six slots (F has no room for 32 + 32 bits):
  - slots 0..5 keep their bits; 6 on all have 0x10000000 ("one of 6 on is worthy": no reader of F uses it -- each
    function that touches F checked for the masks it builds), and which of them in the engine's rw_rp_worthy (set
    by rw_rp_set_a0 at func_800E5890's one set; the recorder takes it as empty once the game has cleared 0x10000000,
    as the sequence lock does);
  - the snap bits: none for 6 on (their replay pose isn't snapped at its first frame -- the replay is step 4's);
  - the replay camera's slot field (rw_tab_DB70) and the camera event's (rw_tab_6350): (slot + 1) << 8 for every slot
    (the camera reads bits 8..15: up to 255).
The six-slot build is unchanged.
(The build_rom part -- step_replay, run after the tables move -- was put in by hand after this script's first try:
its stub entry expected the moved table's address before the move.)"""
import os

W = os.path.dirname(os.path.abspath(__file__))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)


def slots_list(fn, per_line=8):
    v = [fn(i) for i in range(32)]
    return ',\n    '.join(', '.join(x for x in v[i:i + per_line]) for i in range(0, 32, per_line))


edit(os.path.join(W, 'rw', 'r_tables.c'), [
    ("const u32 rw_tab_6350[6] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600 };",
     "#if SIX_NSLOTS == 6\nconst u32 rw_tab_6350[6] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600 };\n#else\n"
     "/* (every slot's, 2026-11-07: patch_replay_bits.py) */\nconst u32 rw_tab_6350[32] = {\n    "
     + slots_list(lambda i: '0x%X' % ((i + 1) << 8)) + " };\n#endif"),
    ("const u32 rw_tab_DB60[6] = { 0x4000, 0x8000, 0x10000, 0x20000, 0x200000, 0x400000 };\n"
     "const u16 rw_tab_DB70[6] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600 };\n"
     "const u32 rw_tab_DB78[6] = { 0x10, 0x20, 0x40, 0x80, 0x800000, 0x1000000 };",
     "#if SIX_NSLOTS == 6\n"
     "const u32 rw_tab_DB60[6] = { 0x4000, 0x8000, 0x10000, 0x20000, 0x200000, 0x400000 };\n"
     "const u16 rw_tab_DB70[6] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600 };\n"
     "const u32 rw_tab_DB78[6] = { 0x10, 0x20, 0x40, 0x80, 0x800000, 0x1000000 };\n"
     "#else\n"
     "/* (every slot's, 2026-11-07: patch_replay_bits.py -- 6 on share 0x10000000, which of them in rw_rp_worthy; no snap) */\n"
     "const u32 rw_tab_DB60[32] = { 0x4000, 0x8000, 0x10000, 0x20000, 0x200000, 0x400000, [6 ... 31] = 0x10000000 };\n"
     "const u16 rw_tab_DB70[32] = {\n    " + slots_list(lambda i: '0x%X' % ((i + 1) << 8)) + " };\n"
     "const u32 rw_tab_DB78[32] = { 0x10, 0x20, 0x40, 0x80, 0x800000, 0x1000000 };\n"
     "#endif"),
])
edit(os.path.join(W, 'rw', 'r_8013092C.c'), [
    ("extern const u32 rw_tab_DB60[6], rw_tab_DB78[6];\nextern const u16 rw_tab_DB70[6];",
     "extern const u32 rw_tab_DB60[], rw_tab_DB78[];\nextern const u16 rw_tab_DB70[];\n"
     "extern u32 rw_rp_worthy;   /* slots 6 on: which are replay-worthy (they share F's 0x10000000; rw/stubs.S rw_rp_set_a0) */"),
    ("    if (LOCKS) return;\n    if (!(F & 0x100) && V8(0x800A946A) == 0) {",
     "    if (!(F & 0x10000000)) rw_rp_worthy = 0;   /* (the game cleared 6 on's shared bit: none of them now) */\n"
     "    if (LOCKS) return;\n    if (!(F & 0x100) && V8(0x800A946A) == 0) {"),
    ("                if (!(f & rw_tab_DB60[i])) continue;",
     "                if (!(f & rw_tab_DB60[i])) continue;\n"
     "                if (i >= 6 && !(rw_rp_worthy & (1u << i))) continue;   /* (6 on: this one's own) */"),
])
edit(os.path.join(W, 'rw', 'stubs.S'), [
    ('''    .data
    .globl rw_seq_lock''', '''# func_800E5890's `F |= DB60[slot]` (patch_replay_bits.py): was `addu a0, a0, v0; lw a0, 0x4A30(a0)` (a0 the
# table's upper half, v0 = slot * 4) -> a0 = rw_tab_DB60[slot], and for slots 6 on (who share F's 0x10000000) their
# own bit in rw_rp_worthy. v1 holds F there: only a0 and at change (t0 kept on the stack).
    .globl rw_rp_set_a0
rw_rp_set_a0:
    lui   $at, %hi(rw_tab_DB60)
    addu  $at, $at, $v0
    lw    $a0, %lo(rw_tab_DB60)($at)
    sltiu $at, $v0, 24
    bnez  $at, 1f
    nop
    addiu $sp, $sp, -8
    sw    $t0, 0($sp)
    srl   $t0, $v0, 2
    addiu $at, $zero, 1
    sllv  $t0, $at, $t0
    lui   $at, %hi(rw_rp_worthy)
    lw    $at, %lo(rw_rp_worthy)($at)
    or    $t0, $t0, $at
    lui   $at, %hi(rw_rp_worthy)
    sw    $t0, %lo(rw_rp_worthy)($at)
    lw    $t0, 0($sp)
    addiu $sp, $sp, 8
1:  jr    $ra
    nop

    .data
    .globl rw_rp_worthy
rw_rp_worthy:                  # the replay-worthy among slots 6 on, a bit each (F has 0x10000000 for them)
    .word 0
    .globl rw_seq_lock'''),
])
edit(os.path.join(W, 'build_rom.py'), [
    ("    ('ovl_e', 0x8011E0EC, (0x00031827, 0x00431024), 'rw_seq_release'),\n",
     "    ('ovl_e', 0x8011E0EC, (0x00031827, 0x00431024), 'rw_seq_release'),\n"
     "    # func_800E5890's `F |= DB60[slot]`: `addu a0, a0, v0; lw a0, 0x4A30(a0)` -> rw_rp_set_a0 (patch_replay_bits.py)\n"
     "    ('ovl_e', 0x800E5BD0, (0x00822021, 0x8C844A30), 'rw_rp_set_a0'),\n"),
])
print('applied')
