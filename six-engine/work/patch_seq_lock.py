"""The sequence lock (0x8016B650, u16) for any count of wrestlers -- the 32-slot work, 2026-11-06.

func_8011D848 runs one wrestler's sequence at a time: it takes the lock with the wrestler's bit (rw_tab_65C8[model])
-- `xori v0, a0, 0x300F; and v0, v1, v0`: someone else holds it? -- and gives it back with `nor v1, zero, v1;
and v0, v0, v1`. The table had six bits (0..3 their own, 4 and 5 0x1000 / 0x2000); a seventh wrestler on read past
it, his "someone else holds it" stayed true, his sequence never ran, and a 10-count went on to 11: banner -1, a crash
(the first 10-man Rumble probe). Now:
  - wrestlers 0..3 keep their own bits, as they were;
  - wrestlers 4 on all have the game's word's bit 0x1000 -- "one of 4 on holds it" -- and their own bit in a 32-bit
    word of the engine's, rw_seq_lock, which says who (set only when he gets it: rw/stubs.S was corrected by hand
    after the second probe deadlocked -- this script's first version set it even while he waited);
  - rw_seq_take / rw_seq_release do the two sites' work: for 0..3 the game's own instructions, for 4 on the wide word
    too. When the game clears its word (a reset, a new match: 0xCFF0, 0, ...) the 0x1000 bit goes with it, and the wide
    word is taken as empty at the next take -- nothing else needs to know about it. The replay's 0x300F still sees
    "someone holds it" (0x1000)."""
import os

W = os.path.dirname(os.path.abspath(__file__))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)


edit(os.path.join(W, 'rw', 'r_tables.c'), [
    ('const u16 rw_tab_65C8[6] = { 1, 2, 4, 8, 0x1000, 0x2000 };',
     '/* (2026-11-06: wrestlers 4 on all 0x1000, "one of 4 on holds it" -- which of them is the engine\'s wide word,\n'
     ' *  rw_seq_lock: rw/stubs.S rw_seq_take / rw_seq_release) */\n'
     'const u16 rw_tab_65C8[SIX_NSLOTS] = { 1, 2, 4, 8, [4 ... SIX_NSLOTS - 1] = 0x1000 };'),
])
edit(os.path.join(W, 'rw', 'stubs.S'), [
    ('''    .data
    .globl rw_last_count''', '''# The sequence lock for any count of wrestlers (build_rom STUBS2; patch_seq_lock.py has the story). s1 the wrestler
# (+0x9E his model); a model's bit is rw_tab_65C8's: 1, 2, 4, 8 for 0..3, 0x1000 ("one of 4 on") for 4 on.
# func_8011D848's take: was `xori v0, a0, 0x300F; and v0, v1, v0` -- a0 his bit, v1 the game's word: v0 = the others
# holding it. For 4 on: the others of 0..3 (and 0x2000, as before), and any other of 4 on in rw_seq_lock -- which counts
# only while the game's word still has 0x1000 (cleared, it was a reset); his own wide bit set (the game sets 0x1000 in
# the delay slot after, `or v1, v1, a0`, as for anyone). Only at and v0 change (t0, t1 kept on the stack).
    .globl rw_seq_take
rw_seq_take:
    andi  $at, $a0, 0x1000
    bnez  $at, 1f
    nop
    xori  $v0, $a0, 0x300F
    jr    $ra
    and   $v0, $v1, $v0
1:  addiu $sp, $sp, -8
    sw    $t0, 0($sp)
    sw    $t1, 4($sp)
    lh    $t0, 0x9E($s1)
    addiu $v0, $zero, 1
    sllv  $t0, $v0, $t0
    lui   $at, %hi(rw_seq_lock)
    andi  $v0, $v1, 0x1000
    bnez  $v0, 2f
    lw    $t1, %lo(rw_seq_lock)($at)
    move  $t1, $zero
2:  nor   $v0, $zero, $t0
    and   $v0, $t1, $v0
    or    $t1, $t1, $t0
    sw    $t1, %lo(rw_seq_lock)($at)
    andi  $at, $v1, 0x200F
    or    $v0, $v0, $at
    lw    $t0, 0($sp)
    lw    $t1, 4($sp)
    jr    $ra
    addiu $sp, $sp, 8
# func_8011D848's give-back: was `nor v1, zero, v1; and v0, v0, v1` -- v1 his bit, v0 the game's word: v0 without his
# bit. For 4 on: his wide bit cleared, and 0x1000 taken out of v0 only when no other of 4 on holds it. v1 = ~his bit,
# as before. Only at, v0 and v1 change.
    .globl rw_seq_release
rw_seq_release:
    andi  $at, $v1, 0x1000
    bnez  $at, 1f
    nop
    nor   $v1, $zero, $v1
    jr    $ra
    and   $v0, $v0, $v1
1:  addiu $sp, $sp, -8
    sw    $t0, 0($sp)
    sw    $t1, 4($sp)
    lh    $t0, 0x9E($s1)
    addiu $at, $zero, 1
    sllv  $t0, $at, $t0
    lui   $at, %hi(rw_seq_lock)
    lw    $t1, %lo(rw_seq_lock)($at)
    nor   $t0, $zero, $t0
    and   $t1, $t1, $t0
    sw    $t1, %lo(rw_seq_lock)($at)
    nor   $v1, $zero, $v1
    bnez  $t1, 2f
    nop
    and   $v0, $v0, $v1
2:  lw    $t0, 0($sp)
    lw    $t1, 4($sp)
    jr    $ra
    addiu $sp, $sp, 8

    .data
    .globl rw_seq_lock
rw_seq_lock:                   # the sequence lock's wrestlers of 4 on, a bit each (the game's word has 0x1000 for them)
    .word 0
    .globl rw_last_count'''),
])
edit(os.path.join(W, 'build_rom.py'), [
    ('''    ('ovl_c', 0x80138D6C, (0x00009021, 0x3C048015), 'rw_colrec_fill_s'),
]''', '''    ('ovl_c', 0x80138D6C, (0x00009021, 0x3C048015), 'rw_colrec_fill_s'),
    # func_8011D848, the sequence lock for any count (2026-11-06; rw/stubs.S rw_seq_take / rw_seq_release): its take,
    # `xori v0, a0, 0x300F; and v0, v1, v0`, and its give-back, `nor v1, zero, v1; and v0, v0, v1`
    ('ovl_e', 0x8011D8B8, (0x3882300F, 0x00621024), 'rw_seq_take'),
    ('ovl_e', 0x8011E0EC, (0x00031827, 0x00431024), 'rw_seq_release'),
]'''),
])
print('applied')
