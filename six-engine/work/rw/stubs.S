# The six-man engine's register stubs: a two-instruction site in a game function becomes `jal rw_X` with the
# site's first instruction in its delay slot; the stub does what the second one did, for any wrestler count
# ($gp), touching nothing but its result (and $at), and returns. (Sites: build_rom.STUBS.)
    .set noreorder
    .set noat
    .text

# x = (x >= n) ? x - n : x   -- the `(i + 1) & 3` round-robin wrap, for n wrestlers
    .globl rw_wrap_s1_v0
rw_wrap_s1_v0:                 # s1 = wrap(v0)
    sltu  $at, $v0, $gp
    bnez  $at, 1f
    move  $s1, $v0
    subu  $s1, $v0, $gp
1:  jr    $ra
    nop

    .globl rw_wrap_v0_v0
rw_wrap_v0_v0:                 # v0 = wrap(v0)
    sltu  $at, $v0, $gp
    bnez  $at, 1f
    nop
    subu  $v0, $v0, $gp
1:  jr    $ra
    nop

# t1 = a1 + n * 0x20           -- the end of a 0x20-a-slot table
    .globl rw_t1_a1_n20
rw_t1_a1_n20:
    sll   $t1, $gp, 5
    jr    $ra
    addu  $t1, $a1, $t1

# s1 = the HUD's per-slot name texture buffers (func_800DDB10; spec.EXTRA_OVLE namebuf)
    .globl rw_namebuf_s1
rw_namebuf_s1:
    lui   $s1, %hi(X_namebuf)
    jr    $ra
    addiu $s1, $s1, %lo(X_namebuf)

# The frame display list's base: the stock one is the frame block 0x8005D8E0[cur] (its first 12000 bytes; the
# scheduler task follows); with more than 4 wrestlers the engine's dlbuf halves, rw_dltab[cur] (spec.EXTRA_MAIN).
#   func_80002BB4: after the delay slot s2 = 0x80060000 + cur * 4, v0 = cur * 4; then was `lw s2, -0x2720(s2)`
# (the frame is built on the graphics thread, func_80003138: not $gp -- the main thread's count in rw_cur_count)
    .globl rw_dlbase_s2
rw_dlbase_s2:
    lui   $at, %hi(rw_cur_count)
    lw    $at, %lo(rw_cur_count)($at)
    slti  $at, $at, 5
    bnez  $at, 1f
    lw    $s2, -0x2720($s2)
    lui   $at, %hi(rw_dltab)
    addu  $at, $at, $v0
    lw    $s2, %lo(rw_dltab)($at)
1:  jr    $ra
    nop
#   func_80002DE4: v1 = &0x8005D8E0[cur]; was `lw v0, 0(v1)` (twice: the task's data pointer and size)
    .globl rw_dlbase_v0_v1
rw_dlbase_v0_v1:
    lui   $at, %hi(rw_cur_count)
    lw    $at, %lo(rw_cur_count)($at)
    slti  $at, $at, 5
    bnez  $at, 1f
    lw    $v0, 0($v1)
    lui   $at, %hi(rw_dltab - 0x8005D8E0)
    addu  $at, $at, $v1
    lw    $v0, %lo(rw_dltab - 0x8005D8E0)($at)
1:  jr    $ra
    nop

# func_80120D98: was `sw v0, 0(s2); addiu s2, s2, 4` (append a prop to the list at s5) -- only below 10 entries
    .globl rw_prop_append
rw_prop_append:
    subu  $at, $s2, $s5
    slti  $at, $at, 40
    beqz  $at, 1f
    nop
    sw    $v0, 0($s2)
    addiu $s2, $s2, 4
1:  jr    $ra
    nop

# func_800E2344: v0 = 1 if controller 1 has one of slots 1..n-1 (0x800A6028, the per-slot pads), else 0; v1 = 1
# (as the original left it)
    .globl rw_p1_other_v0
rw_p1_other_v0:
    lui   $at, %hi(A_800A6028 + 4)
    addiu $at, $at, %lo(A_800A6028 + 4)
    sll   $v1, $gp, 2
    lui   $v0, %hi(A_800A6028)
    addiu $v0, $v0, %lo(A_800A6028)
    addu  $v1, $v1, $v0                 # &pad[n]
1:  sltu  $v0, $at, $v1
    beqz  $v0, 2f
    nop
    lw    $v0, 0($at)
    addiu $at, $at, 4
    xori  $v0, $v0, 1
    bnez  $v0, 1b
    nop
    addiu $v1, $zero, 1
    jr    $ra
    addiu $v0, $zero, 1
2:  addiu $v1, $zero, 1
    jr    $ra
    addu  $v0, $zero, $zero

# func_800EBD2C: before its `c.lt.d f0, f2` (a prop near enough joins the contact list): with the list (s4
# entries) full at 10, f0 = f2 -- not nearer
    .globl rw_contact_cap
rw_contact_cap:
    slti  $at, $s4, 10
    bnez  $at, 1f
    nop
    mov.d $f0, $f2
1:  jr    $ra
    nop

# func_8013140C: was `ori v0, v0, 0xF8` -- with the snap bits of slots 4 and 5 when they're in (rw_tab_DB78)
    .globl rw_snap_v0
rw_snap_v0:
    ori   $v0, $v0, 0xF8
    slti  $at, $gp, 5
    bnez  $at, 1f
    lui   $at, 0x0080          # slot 4
    or    $v0, $v0, $at
    slti  $at, $gp, 6
    bnez  $at, 1f
    lui   $at, 0x0100          # slot 5
    or    $v0, $v0, $at
1:  jr    $ra
    nop

# Loop bounds that are the count times a stride: the count ($gp) times K, worked out (they were halfword tables for
# counts 0..7, so a count of 8 or more read past them -- the 32-slot work); the site's register, nothing else touched
# but $at (and the result register, set at the end anyway)
    .globl rw_sltu_v0_v1_k8
rw_sltu_v0_v1_k8:              # v0 = v1 < n * 8 (unsigned)
    sll   $at, $gp, 3
    jr    $ra
    sltu  $v0, $v1, $at
    .globl rw_slt_v0_a0_k358
rw_slt_v0_a0_k358:             # v0 = a0 < n * 0x358 (0x358 = 1024 - 128 - 32 - 8)
    sll   $v0, $gp, 10
    sll   $at, $gp, 7
    subu  $v0, $v0, $at
    sll   $at, $gp, 5
    subu  $v0, $v0, $at
    sll   $at, $gp, 3
    subu  $v0, $v0, $at
    jr    $ra
    slt   $v0, $a0, $v0

# The wrestler count ($gp) of the main thread, the one that runs the game (menus and matches; no other thread runs
# a slot loop): every overlay is entered by func_800007AC's `jal 0x800D9960`, which becomes `jal rw_run_overlay`.
# The match overlay (ovl_e, ROM 0x144BE0; a0 = the overlay's ROM start) runs with rw_match_count wrestlers (4, or 6
# for a six-man match: set by the menus), everything else with 4; and 4 again once it returns.
    .globl rw_run_overlay
rw_run_overlay:                # (no stack frame: the game keeps stack pointers -- 0x800A8788 -- that would see it;
    lui   $at, %hi(rw_ovl_ra)  #  the main thread is the only caller and overlays don't nest, so $ra waits in a word)
    sw    $ra, %lo(rw_ovl_ra)($at)
    addiu $gp, $zero, 4
    lui   $at, 0x0014
    ori   $at, $at, 0x4BE0
    bne   $a0, $at, 1f
    nop
    lui   $at, %hi(rw_match_count)
    lw    $gp, %lo(rw_match_count)($at)
    addiu $ra, $zero, 4                     # (read once: the next match is 4 unless the menus say otherwise)
    sw    $ra, %lo(rw_match_count)($at)
    lui   $at, %hi(rw_last_count)
    sw    $gp, %lo(rw_last_count)($at)      # (the last match's count, for the results table: rw/r_results.c)
1:  lui   $at, %hi(rw_cur_count)
    sw    $gp, %lo(rw_cur_count)($at)       # (the count for the other threads' use)
    jal   0x800D9960
    nop
    addiu $gp, $zero, 4
    lui   $at, %hi(rw_cur_count)
    sw    $gp, %lo(rw_cur_count)($at)
    lui   $at, %hi(rw_ovl_ra)
    lw    $ra, %lo(rw_ovl_ra)($at)
    jr    $ra
    nop

# func_80138D10 (ovl_c, the results table's values), its `move s2, zero; lui a0, 0x8015` -> `jal here; nop`: fill
# rw_colrec (rw/r_results.c) first; nothing caller-saved is live there
    .globl rw_colrec_fill_s
rw_colrec_fill_s:
    addiu $sp, $sp, -0x20
    sw    $ra, 0x1C($sp)
    jal   rw_colrec_fill
    nop
    lw    $ra, 0x1C($sp)
    addiu $sp, $sp, 0x20
    move  $s2, $zero
    jr    $ra
    lui   $a0, 0x8015

# The sequence lock for any count of wrestlers (build_rom STUBS2; patch_seq_lock.py has the story). s1 the wrestler
# (+0x9E his model); a model's bit is rw_tab_65C8's: 1, 2, 4, 8 for 0..3, 0x1000 ("one of 4 on") for 4 on.
# func_8011D848's take: was `xori v0, a0, 0x300F; and v0, v1, v0` -- a0 his bit, v1 the game's word: v0 = the others
# holding it. For 4 on: the others of 0..3 (and 0x2000, as before), and any other of 4 on in rw_seq_lock -- which counts
# only while the game's word still has 0x1000 (cleared, it was a reset). His own wide bit is set only when no one else
# holds it -- as the game stores its word (`or v1, v1, a0` in the delay slot, stored at 0x8011D8E0) only then. Only at and v0 change (t0, t1 kept on the stack).
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
    and   $v0, $t1, $v0        # the others of 4 on holding it
    or    $t1, $t1, $t0        # (the wide word with his bit)
    andi  $t0, $v1, 0x200F     # the others of 0..3 (and 0x2000)
    or    $v0, $v0, $t0
    bnez  $v0, 3f              # someone else holds it: the game marks him waiting (+0x90) and stores nothing -- nor
    nop                        # do we (setting his bit anyway left every waiting one of 4 on blocking the rest: a
    sw    $t1, %lo(rw_seq_lock)($at)   # deadlock, the second 10-man probe)
3:  lw    $t0, 0($sp)
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

# The camera's per-slot sets for any count (patch_cam_wide.py has the story; rw_cam_view / rw_cam_play from
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
    lui   $v0, 0x8009
    lw    $v0, 0x7EC0($v0)          # the match flags
    lui   $v1, 0x01C8
    ori   $v1, $v1, 0x2000
    and   $v1, $v0, $v1
    beqz  $v1, 1f
    nop
    jr    $ra                       # a tag-type match: func_800EE594's mask, as before
    andi  $a3, $a0, 0xFFFF
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
    lw    $a3, %lo(rw_cam_view)($at)
2:  lui   $at, %hi(rw_cam_play)
    lw    $a3, %lo(rw_cam_play)($at)
3:  bnez  $a3, 4f
    nop
    addiu $a3, $zero, -1            # none: all
4:  jr    $ra
    nop
# func_800E30D8's, the same: was `andi a0, a0, 0xFFFF` -> a0. at, v0, v1 are free there.
    .globl rw_cam_sel_a0
rw_cam_sel_a0:
    lui   $v0, 0x8009
    lw    $v0, 0x7EC0($v0)          # the match flags
    lui   $v1, 0x01C8
    ori   $v1, $v1, 0x2000
    and   $v1, $v0, $v1
    beqz  $v1, 1f
    nop
    jr    $ra                       # a tag-type match: func_800EE594's mask, as before
    andi  $a0, $a0, 0xFFFF
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
    lw    $a0, %lo(rw_cam_view)($at)
2:  lui   $at, %hi(rw_cam_play)
    lw    $a0, %lo(rw_cam_play)($at)
3:  bnez  $a0, 4f
    nop
    addiu $a0, $zero, -1            # none: all
4:  jr    $ra
    nop

# func_80120D98's target list for any count (patch_target_list.py): the function fills its first 11 entries with -1
# (`addiu s0, zero, 10; addiu v0, s5, 0x28` and its loop) and then writes the other wrestlers from the first, with no
# -1 of its own after them -- past 11 others the list had no end, and its caller read the stack beyond (the 16-slot
# probe's second crash, after the room was made). Its two list callers (func_80126384, func_8012069C: a3 = sp+0x10,
# their frames grown for it) call it through this: the list's entries 11 up to the match's count ($gp) set to -1
# first, then on into func_80120D98 with the caller's own return. Its other callers (func_80121080, func_801210C4: no
# list) call it directly, as before. Only at, v0, t0, t1 change (a call's to lose).
    .globl rw_tl_call
rw_tl_call:
    slti  $at, $gp, 12
    bnez  $at, 2f
    sll   $t1, $gp, 2
    addu  $t1, $t1, $a3              # the list's end: entry $gp
    addiu $t0, $a3, 44               # entry 11
    addiu $at, $zero, -1
1:  sw    $at, 0($t0)
    addiu $t0, $t0, 4
    sltu  $v0, $t0, $t1
    bnez  $v0, 1b
    nop
2:  j     0x80120D98
    nop

# func_8012C474: was `addiu s0, sp, 0x30` -- its contact list, the engine's rw_contacts (patch_contacts.py)
    .globl rw_contacts_s0
rw_contacts_s0:
    lui   $s0, %hi(rw_contacts)
    jr    $ra
    addiu $s0, $s0, %lo(rw_contacts)

# func_800E5890's `F |= DB60[slot]` (patch_replay_bits.py): was `addu a0, a0, v0; lw a0, 0x4A30(a0)` (a0 the
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
    .globl rw_seq_lock
rw_seq_lock:                   # the sequence lock's wrestlers of 4 on, a bit each (the game's word has 0x1000 for them)
    .word 0
    .globl rw_last_count
rw_last_count:
    .word 4
rw_dltab:
    .word X_dlbuf, X_dlbuf2        # (spec.DLBUF_SIZE apart: 0x8000 for six, more past it)
    .globl rw_match_count
rw_match_count:
    .word 4
rw_ovl_ra:
    .word 0
    .globl rw_cur_count
rw_cur_count:                  # the main thread's wrestler count ($gp) right now
    .word 4
