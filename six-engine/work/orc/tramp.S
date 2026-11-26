# The oracle's logical-frame counter (both games, the same code): osRecvMesg's entry counts the main thread's
# waits for its next frame -- on 0x80060B20 (func_800030F4, the menus) and 0x80063368 (func_80005818, the
# controller read each frame) -- in the word at 0x807FFFE0. (t0/t1 are free at a function's entry.)
    .set noreorder
    .set noat
    .text
    .globl tramp
tramp:
    lui   $t0, 0x8005
    lw    $t0, 0x37D0($t0)        # __osRunningThread
    lui   $t1, 0x8009
    addiu $t1, $t1, -0x6FC0       # 0x80089040, the main thread
    bne   $t0, $t1, 2f
    lui   $t0, 0x8006
    addiu $t0, $t0, 0x0B20
    beq   $a0, $t0, 1f
    lui   $t0, 0x8006
    addiu $t0, $t0, 0x3368
    bne   $a0, $t0, 2f
    nop
1:  lui   $t0, 0x8080
    lw    $t1, -0x20($t0)
    addiu $t1, $t1, 1
    sw    $t1, -0x20($t0)
2:  addiu $sp, $sp, -0x28
    j     0x80036CD8
    sw    $s0, 0x10($sp)
