# Two words of our own after gen/asmpatch.py's patched functions (relprep2, 2026-11-24).
#
# The patched copies of the game's functions (patches.o, the last of the .recomp_patch section) are game code, so the
# release ships them as zeros (firstrun/mod_binary.holes; the game's first start copies them back from the player's ROM:
# tools/firstrun/mkplan.py). Right after them came our next function's first two instructions, and those zeros with that
# standard prologue (addiu sp, sp, -24; sw ra, 20(sp)) were 32 bytes found in No Mercy Plus V3 as well. These two words,
# linked right after patches.o (build_mod.ps1), keep the zeros away from our code with bytes no game has.
#
# They are never run: nothing names them and the function before them returns first. Even run, they would change nothing:
# both write register $zero, which always reads 0 (lui / ori of the letters "UNM!").
    .set noreorder
    .section .recomp_patch,"axR",@progbits
    .align 2
    .word 0x3C00554E     # lui $zero, 0x554E   ("UN")
    .word 0x34004D21     # ori $zero, $zero, 0x4D21   ("M!")
