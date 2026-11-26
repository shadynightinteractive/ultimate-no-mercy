# Test-rig trampolines. The harness patches:
#   0x800012B0 (frame task submit, func_800012B0) first two instructions -> j frame_tramp; nop
#   0x800346AC (jr $ra of osContGetReadData, func_80034618)              -> j pad_tramp
# and the blob is loaded at 0x80700000 with entry table at its start.
    .set noreorder
    .set noat
    .section .text.start, "ax"
    .globl _start
_start:
    j frame_tramp          # 0x80700000
    nop
    j pad_tramp            # 0x80700008
    nop
    j demo_tramp           # 0x80700010
    nop
    j page_tramp           # 0x80700018
    nop
    j sprites_tramp        # 0x80700020
    nop
    j rrframe_tramp        # 0x80700028
    nop
    j setup_tramp          # 0x80700030
    nop
    j hud_tramp            # 0x80700038
    nop
    j finalize_tramp       # 0x80700040
    nop
    j step_tramp           # 0x80700048
    nop
    j packrec_tramp        # 0x80700050
    nop
    j packmoves_tramp      # 0x80700058
    nop
    j storypart_tramp      # 0x80700060
    nop
    j storyrival_tramp     # 0x80700068
    nop
    j storypick_tramp      # 0x80700070
    nop
    j storyrumble_tramp    # 0x80700078
    nop
    j fileload_tramp       # 0x80700080
    nop
    j themes_audio_dma     # 0x80700088 (func_8002B0C0 replaced: a plain C call)
    nop
    j media_video_select   # 0x80700090 (unused: func_8010C998 is a fn_patch now)
    nop
    j entfx_tramp          # 0x80700098
    nop
    j unpack_tramp         # 0x807000A0
    nop
    j matchmoves_tramp     # 0x807000A8
    nop
    j ovlret_tramp         # 0x807000B0
    nop

frame_tramp:
    addiu $sp, $sp, -0x60
    sw $ra, 0x5C($sp)
    sw $a0, 0x58($sp)
    sw $a1, 0x54($sp)
    sw $a2, 0x50($sp)
    sw $a3, 0x4C($sp)
    sw $v0, 0x48($sp)
    sw $v1, 0x44($sp)
    sw $at, 0x40($sp)
    sw $t0, 0x3C($sp)
    sw $t1, 0x38($sp)
    sw $t2, 0x34($sp)
    sw $t3, 0x30($sp)
    sw $t4, 0x2C($sp)
    sw $t5, 0x28($sp)
    sw $t6, 0x24($sp)
    sw $t7, 0x20($sp)
    sw $t8, 0x1C($sp)
    sw $t9, 0x18($sp)
    jal test_frame_hook
    nop
    lw $ra, 0x5C($sp)
    lw $a0, 0x58($sp)
    lw $a1, 0x54($sp)
    lw $a2, 0x50($sp)
    lw $a3, 0x4C($sp)
    lw $v0, 0x48($sp)
    lw $v1, 0x44($sp)
    lw $at, 0x40($sp)
    lw $t0, 0x3C($sp)
    lw $t1, 0x38($sp)
    lw $t2, 0x34($sp)
    lw $t3, 0x30($sp)
    lw $t4, 0x2C($sp)
    lw $t5, 0x28($sp)
    lw $t6, 0x24($sp)
    lw $t7, 0x20($sp)
    lw $t8, 0x1C($sp)
    lw $t9, 0x18($sp)
    addiu $sp, $sp, 0x60
    # displaced instructions of func_800012B0
    addiu $sp, $sp, -0x20
    sw $ra, 0x18($sp)
    lui $at, 0x8000
    ori $at, $at, 0x12B8
    jr $at
    nop

pad_tramp:
    addiu $sp, $sp, -0x60
    sw $ra, 0x5C($sp)
    sw $a0, 0x58($sp)
    sw $a1, 0x54($sp)
    sw $a2, 0x50($sp)
    sw $a3, 0x4C($sp)
    sw $v0, 0x48($sp)
    sw $v1, 0x44($sp)
    sw $at, 0x40($sp)
    sw $t0, 0x3C($sp)
    sw $t1, 0x38($sp)
    sw $t2, 0x34($sp)
    sw $t3, 0x30($sp)
    sw $t4, 0x2C($sp)
    sw $t5, 0x28($sp)
    sw $t6, 0x24($sp)
    sw $t7, 0x20($sp)
    sw $t8, 0x1C($sp)
    sw $t9, 0x18($sp)
    jal test_pad_hook
    nop
    lw $ra, 0x5C($sp)
    lw $a0, 0x58($sp)
    lw $a1, 0x54($sp)
    lw $a2, 0x50($sp)
    lw $a3, 0x4C($sp)
    lw $v0, 0x48($sp)
    lw $v1, 0x44($sp)
    lw $at, 0x40($sp)
    lw $t0, 0x3C($sp)
    lw $t1, 0x38($sp)
    lw $t2, 0x34($sp)
    lw $t3, 0x30($sp)
    lw $t4, 0x2C($sp)
    lw $t5, 0x28($sp)
    lw $t6, 0x24($sp)
    lw $t7, 0x20($sp)
    lw $t8, 0x1C($sp)
    lw $t9, 0x18($sp)
    addiu $sp, $sp, 0x60
    jr $ra
    nop

demo_tramp:
    addiu $sp, $sp, -0x60
    sw $ra, 0x5C($sp)
    sw $v0, 0x48($sp)
    sw $v1, 0x44($sp)
    sw $a0, 0x58($sp)
    sw $a1, 0x54($sp)
    sw $a2, 0x50($sp)
    sw $a3, 0x4C($sp)
    sw $at, 0x40($sp)
    sw $t0, 0x3C($sp)
    sw $t1, 0x38($sp)
    sw $t2, 0x34($sp)
    sw $t3, 0x30($sp)
    sw $t4, 0x2C($sp)
    sw $t5, 0x28($sp)
    sw $t6, 0x24($sp)
    sw $t7, 0x20($sp)
    sw $t8, 0x1C($sp)
    sw $t9, 0x18($sp)
    jal arcade_demo_setup_done
    nop
    lw $ra, 0x5C($sp)
    lw $v0, 0x48($sp)
    lw $v1, 0x44($sp)
    lw $a0, 0x58($sp)
    lw $a1, 0x54($sp)
    lw $a2, 0x50($sp)
    lw $a3, 0x4C($sp)
    lw $at, 0x40($sp)
    lw $t0, 0x3C($sp)
    lw $t1, 0x38($sp)
    lw $t2, 0x34($sp)
    lw $t3, 0x30($sp)
    lw $t4, 0x2C($sp)
    lw $t5, 0x28($sp)
    lw $t6, 0x24($sp)
    lw $t7, 0x20($sp)
    lw $t8, 0x1C($sp)
    lw $t9, 0x18($sp)
    addiu $sp, $sp, 0x60
    jr $ra
    nop

# ovl_a's story page wait (func_800E8A14): its first two instructions are patched to j page_tramp.
page_tramp:
    addiu $sp, $sp, -0x60
    sw $ra, 0x5C($sp)
    sw $a0, 0x58($sp)
    sw $a1, 0x54($sp)
    sw $a2, 0x50($sp)
    sw $a3, 0x4C($sp)
    sw $v0, 0x48($sp)
    sw $v1, 0x44($sp)
    sw $at, 0x40($sp)
    sw $t0, 0x3C($sp)
    sw $t1, 0x38($sp)
    sw $t2, 0x34($sp)
    sw $t3, 0x30($sp)
    sw $t4, 0x2C($sp)
    sw $t5, 0x28($sp)
    sw $t6, 0x24($sp)
    sw $t7, 0x20($sp)
    sw $t8, 0x1C($sp)
    sw $t9, 0x18($sp)
    jal test_page_hook
    nop
    lw $ra, 0x5C($sp)
    lw $a0, 0x58($sp)
    lw $a1, 0x54($sp)
    lw $a2, 0x50($sp)
    lw $a3, 0x4C($sp)
    lw $v0, 0x48($sp)
    lw $v1, 0x44($sp)
    lw $at, 0x40($sp)
    lw $t0, 0x3C($sp)
    lw $t1, 0x38($sp)
    lw $t2, 0x34($sp)
    lw $t3, 0x30($sp)
    lw $t4, 0x2C($sp)
    lw $t5, 0x28($sp)
    lw $t6, 0x24($sp)
    lw $t7, 0x20($sp)
    lw $t8, 0x1C($sp)
    lw $t9, 0x18($sp)
    addiu $sp, $sp, 0x60
    # displaced instructions of func_800E8A14
    addiu $sp, $sp, -0x18
    addiu $v0, $zero, 0x1
    lui $at, 0x800E
    ori $at, $at, 0x8A1C
    jr $at
    nop

sprites_tramp:
    addiu $sp, $sp, -0x60
    sw $ra, 0x5C($sp)
    sw $a0, 0x58($sp)
    sw $a1, 0x54($sp)
    sw $a2, 0x50($sp)
    sw $a3, 0x4C($sp)
    sw $v0, 0x48($sp)
    sw $v1, 0x44($sp)
    sw $at, 0x40($sp)
    sw $t0, 0x3C($sp)
    sw $t1, 0x38($sp)
    sw $t2, 0x34($sp)
    sw $t3, 0x30($sp)
    sw $t4, 0x2C($sp)
    sw $t5, 0x28($sp)
    sw $t6, 0x24($sp)
    sw $t7, 0x20($sp)
    sw $t8, 0x1C($sp)
    sw $t9, 0x18($sp)
    jal arcade_menu_sprites
    nop
    lw $ra, 0x5C($sp)
    lw $a0, 0x58($sp)
    lw $a1, 0x54($sp)
    lw $a2, 0x50($sp)
    lw $a3, 0x4C($sp)
    lw $v0, 0x48($sp)
    lw $v1, 0x44($sp)
    lw $at, 0x40($sp)
    lw $t0, 0x3C($sp)
    lw $t1, 0x38($sp)
    lw $t2, 0x34($sp)
    lw $t3, 0x30($sp)
    lw $t4, 0x2C($sp)
    lw $t5, 0x28($sp)
    lw $t6, 0x24($sp)
    lw $t7, 0x20($sp)
    lw $t8, 0x1C($sp)
    lw $t9, 0x18($sp)
    addiu $sp, $sp, 0x60
    # displaced instructions of func_80117A84
    addiu $sp, $sp, -0x30
    sw $s4, 0x28($sp)
    lui $at, 0x8011
    ori $at, $at, 0x7A8C
    jr $at
    nop


# ---- the single-player Royal Rumble's hooks -------------------------------------------------------
# Each function's first two instructions are patched to j <name>_tramp (hooks_test.c). A "pre" hook
# runs, then the function goes on; a "wrap" calls the function (its two displaced instructions,
# then the rest of it) and runs the hook after it returns.
.macro SAVE
    addiu $sp, $sp, -0x60
    sw $ra, 0x5C($sp)
    sw $a0, 0x58($sp)
    sw $a1, 0x54($sp)
    sw $a2, 0x50($sp)
    sw $a3, 0x4C($sp)
    sw $v0, 0x48($sp)
    sw $v1, 0x44($sp)
    sw $at, 0x40($sp)
    sw $t0, 0x3C($sp)
    sw $t1, 0x38($sp)
    sw $t2, 0x34($sp)
    sw $t3, 0x30($sp)
    sw $t4, 0x2C($sp)
    sw $t5, 0x28($sp)
    sw $t6, 0x24($sp)
    sw $t7, 0x20($sp)
    sw $t8, 0x1C($sp)
    sw $t9, 0x18($sp)
.endm
.macro RESTORE
    lw $ra, 0x5C($sp)
    lw $a0, 0x58($sp)
    lw $a1, 0x54($sp)
    lw $a2, 0x50($sp)
    lw $a3, 0x4C($sp)
    lw $v0, 0x48($sp)
    lw $v1, 0x44($sp)
    lw $at, 0x40($sp)
    lw $t0, 0x3C($sp)
    lw $t1, 0x38($sp)
    lw $t2, 0x34($sp)
    lw $t3, 0x30($sp)
    lw $t4, 0x2C($sp)
    lw $t5, 0x28($sp)
    lw $t6, 0x24($sp)
    lw $t7, 0x20($sp)
    lw $t8, 0x1C($sp)
    lw $t9, 0x18($sp)
    addiu $sp, $sp, 0x60
.endm

# func_8011D400 (pre): arcade_rr_frame
rrframe_tramp:
    SAVE
    jal arcade_rr_frame
    nop
    RESTORE
    lui $v0, 0x8009
    lw $v0, 0x7EC0($v0)
    lui $at, 0x8011
    ori $at, $at, 0xD408
    jr $at
    nop

# func_80115000 (wrap): arcade_rr_match_setup after it
setup_tramp:
    addiu $sp, $sp, -0x18
    sw $ra, 0x14($sp)
    jal setup_orig
    nop
    SAVE
    jal arcade_rr_match_setup
    nop
    RESTORE
    lw $ra, 0x14($sp)
    jr $ra
    addiu $sp, $sp, 0x18
setup_orig:
    lui $v0, 0x8017
    lbu $v0, -0x4A68($v0)
    lui $at, 0x8011
    ori $at, $at, 0x5008
    jr $at
    nop

# func_800DEDE0 (wrap): arcade_rr_hud after it
hud_tramp:
    addiu $sp, $sp, -0x18
    sw $ra, 0x14($sp)
    jal hud_orig
    nop
    SAVE
    jal arcade_rr_hud
    nop
    RESTORE
    lw $ra, 0x14($sp)
    jr $ra
    addiu $sp, $sp, 0x18
hud_orig:
    lui $v1, 0x8016
    lhu $v1, -0x7168($v1)
    lui $at, 0x800D
    ori $at, $at, 0xEDE8
    jr $at
    nop

# func_80136784 (ovl_c; pre and post): arcade_rr_finalize_pre / _post around it
finalize_tramp:
    addiu $sp, $sp, -0x20
    sw $ra, 0x1C($sp)
    sw $a0, 0x18($sp)
    SAVE
    jal arcade_rr_finalize_pre
    nop
    RESTORE
    lw $a0, 0x18($sp)
    jal finalize_orig
    nop
    sw $v0, 0x10($sp)
    SAVE
    jal arcade_rr_finalize_post
    nop
    RESTORE
    lw $v0, 0x10($sp)
    lw $ra, 0x1C($sp)
    jr $ra
    addiu $sp, $sp, 0x20
finalize_orig:
    addiu $sp, $sp, -0x20
    sw $ra, 0x18($sp)
    lui $at, 0x8013
    ori $at, $at, 0x678C
    jr $at
    nop


# the match loop's step of the match's logic (its jal func_80114514 at 0x800D9DFC comes here)
step_tramp:
    addiu $sp, $sp, -0x18
    sw $ra, 0x14($sp)
    lui $at, 0x8011
    ori $at, $at, 0x4514
    jalr $at
    nop
    jal test_step_hook
    nop
    lw $ra, 0x14($sp)
    jr $ra
    addiu $sp, $sp, 0x18


# Big Show (bigshow.c). ovl_a func_800E299C(dest, record) (wrap): the save packs a costume record
# func_80005EFC(moves, slots, abilities, out) (wrap): a moveset is unpacked
unpack_tramp:
    addiu $sp, $sp, -0x20
    sw $ra, 0x1C($sp)
    SAVE
    jal test_moves_pre
    nop
    RESTORE
    jal unpack_orig
    nop
    SAVE
    jal test_moves_post
    nop
    RESTORE
    lw $ra, 0x1C($sp)
    jr $ra
    addiu $sp, $sp, 0x20
unpack_orig:
    addiu $sp, $sp, -0x20
    sw $s0, 0x10($sp)
    lui $at, 0x8000
    ori $at, $at, 0x5F04
    jr $at
    nop

# func_8000073C (an overlay is loaded): its jr $ra comes here -- the patches into the overlay at once
ovlret_tramp:
    SAVE
    jal test_apply_patches
    nop
    jal arcade_overlay_loaded
    nop
    RESTORE
    jr $ra
    nop

# ovl_e func_8013CBAC(slot, moves, ability bits) (wrap): the match unpacks a moveset
matchmoves_tramp:
    addiu $sp, $sp, -0x20
    sw $ra, 0x1C($sp)
    SAVE
    jal test_match_moves_pre
    nop
    RESTORE
    jal matchmoves_orig
    nop
    SAVE
    jal test_moves_post
    nop
    RESTORE
    lw $ra, 0x1C($sp)
    jr $ra
    addiu $sp, $sp, 0x20
matchmoves_orig:
    addiu $sp, $sp, -0x60
    sw $s2, 0x40($sp)
    lui $at, 0x8013
    ori $at, $at, 0xCBB4
    jr $at
    nop

packrec_tramp:
    addiu $sp, $sp, -0x20
    sw $ra, 0x1C($sp)
    sw $a0, 0x18($sp)
    sw $a1, 0x14($sp)
    SAVE
    addu $a0, $zero, $zero
    jal bigshow_pack_pre
    nop
    RESTORE
    lw $a0, 0x18($sp)
    lw $a1, 0x14($sp)
    jal packrec_orig
    nop
    SAVE
    jal bigshow_pack_post
    nop
    RESTORE
    lw $ra, 0x1C($sp)
    jr $ra
    addiu $sp, $sp, 0x20
packrec_orig:
    addiu $sp, $sp, -0x28
    sw $s0, 0x18($sp)
    lui $at, 0x800E
    ori $at, $at, 0x29A4
    jr $at
    nop

# ovl_a func_800E2AC0(dest, moveset) (wrap): the save packs a moveset
packmoves_tramp:
    addiu $sp, $sp, -0x20
    sw $ra, 0x1C($sp)
    sw $a0, 0x18($sp)
    sw $a1, 0x14($sp)
    SAVE
    addiu $a0, $zero, 1
    jal bigshow_pack_pre
    nop
    RESTORE
    lw $a0, 0x18($sp)
    lw $a1, 0x14($sp)
    jal packmoves_orig
    nop
    SAVE
    jal bigshow_pack_post
    nop
    RESTORE
    lw $ra, 0x1C($sp)
    jr $ra
    addiu $sp, $sp, 0x20
packmoves_orig:
    addiu $sp, $sp, -0x20
    sw $s1, 0x14($sp)
    lui $at, 0x800E
    ori $at, $at, 0x2AC8
    jr $at
    nop

# Story Mode's readers of its tables of people (pre): ovl_a func_800EC554, func_800EBF70,
# func_800EC268, func_800EE3EC
# func_8000AB64 (an entrance's effects): bigshow_entrance_tables first.
entfx_tramp:
    SAVE
    jal media_entrance_tables
    nop
    RESTORE
    addiu $sp, $sp, -0x18
    sw $ra, 0x10($sp)
    lui $at, 0x8000
    ori $at, $at, 0xAB6C
    jr $at
    nop

storypart_tramp:
    SAVE
    jal bigshow_story_tables
    nop
    RESTORE
    lui $v1, 0x8009
    lhu $v1, 0x7EEC($v1)
    lui $at, 0x800E
    ori $at, $at, 0xC55C
    jr $at
    nop
storyrival_tramp:
    SAVE
    jal bigshow_story_tables
    nop
    RESTORE
    sll $v0, $a0, 1
    addu $v0, $v0, $a0
    lui $at, 0x800E
    ori $at, $at, 0xBF78
    jr $at
    nop
storypick_tramp:
    SAVE
    jal bigshow_story_tables
    nop
    RESTORE
    addiu $sp, $sp, -0x70
    andi $a0, $a0, 0xF
    lui $at, 0x800E
    ori $at, $at, 0xC270
    jr $at
    nop
storyrumble_tramp:
    SAVE
    jal bigshow_story_tables
    nop
    RESTORE
    addiu $sp, $sp, -0x38
    addu $a2, $a0, $zero
    lui $at, 0x800E
    ori $at, $at, 0xE3F4
    jr $at
    nop

# func_80003C94(info, file, dest) (wrap): every file read from the ROM
fileload_tramp:
    addiu $sp, $sp, -0x20
    sw $ra, 0x1C($sp)
    sw $a0, 0x18($sp)
    sw $a1, 0x14($sp)
    sw $a2, 0x10($sp)
    SAVE
    addu $a0, $a1, $zero
    addu $a1, $a2, $zero
    jal test_file_pre
    nop
    RESTORE
    lw $a0, 0x18($sp)
    lw $a1, 0x14($sp)
    lw $a2, 0x10($sp)
    jal fileload_orig
    nop
    sw $v0, 0x18($sp)
    SAVE
    jal test_file_post
    nop
    RESTORE
    lw $v0, 0x18($sp)
    lw $ra, 0x1C($sp)
    jr $ra
    addiu $sp, $sp, 0x20
fileload_orig:
    addiu $sp, $sp, -0x30
    sw $s2, 0x28($sp)
    lui $at, 0x8000
    ori $at, $at, 0x3C9C
    jr $at
    nop
