/* func_800EDE48 (ovl_a, after a match: a challenge's condition on the player's hits): over the match's losers, the
 * player's row of hit-stat matrix 0 (flags & 0x400: matrix 2) -- the sum, or with !(flags & 0x800) the smallest --
 * against flags & 0x300 (0: any, 0x100: none, 0x200: under 5, 0x300: under 10) -> 1 met / -1 not. The stock matrices
 * at 0x8009FDF8 are the engine's SIX_NSLOTS x SIX_NSLOTS (STAT, left in ovl_e's BSS after the match: no overlay's BSS or the heap
 * reaches it) and the losers are RES_LOST (slots 4 and 5 in RES6). The original's player row: func_800ED820. */
#include "six.h"
s32 func_800ED820(void);
REWRITE(800EDE48, s32, (u32 flags))
{
    s32 row = func_800ED820(), t = (flags & 0x400) ? 2 : 0, i, v = 0;
    u32 sel = flags & 0x300;
    /* (every slot the engine has: after the match $gp is 4 again; a slot not in it is no loser) */
    for (i = 0; i < SIX_NSLOTS; i++) if (RES_LOST(i)) v += STAT(t, row, i);
    if (!(flags & 0x800))
        for (i = 0; i < SIX_NSLOTS; i++) if (RES_LOST(i) && (s32)STAT(t, row, i) < v) v = STAT(t, row, i);
    if (sel == 0x100) return v == 0 ? 1 : -1;
    if (sel == 0x200) return v < 5 ? 1 : -1;
    if (sel == 0x300) return v < 10 ? 1 : -1;
    return v != 0 ? 1 : -1;
}
