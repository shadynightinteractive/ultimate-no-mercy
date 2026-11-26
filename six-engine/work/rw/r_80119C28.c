/* func_80119C28 (ovl_e): the winner to show -- the result mask's first winner, or the first loser if that winner
 * has +0xE9 & 8 and the loser doesn't; -1 if none or if 4 or more won. In a tag match (0x22000) the winner's
 * partner (slot ^ 1) gets +0x94 |= 4 unless either has +0xE9 & 4. The original keeps the winners and losers in two
 * 6-word stack lists (5 cleared) filled from 4 slots; for n: n. */
#include "six.h"
extern u8 A_801596C0[];
REWRITE(80119C28, s32, (void))
{
    s32 win[SIX_NSLOTS + 1], lose[SIX_NSLOTS + 1], n = nslots(), nw = 0, nl = 0, i, r, p;
    for (i = 0; i < n + 1; i++) { win[i] = -1; lose[i] = -1; }
    for (i = 0; i < n; i++) {
        if (RES_WON(i)) win[nw++] = i;
        if (RES_LOST(i)) lose[nl++] = i;
    }
    if (nw <= 0 || nw >= 4) return -1;
    r = win[0];
    /* (no loser: the original reads slot -1's +0xE9, the byte before the wrestlers -- CPU AI slot 0 +0x61) */
    if ((V8(WR(win[0]) + 0xE9) & 8) && !((lose[0] < 0 ? V8(A_801596C0 + 0x61) : V8(WR(lose[0]) + 0xE9)) & 8)) r = lose[0];
    if ((V32(0x80097EC0) & 0x22000) && !(V8(WR(win[0]) + 0xE9) & 4)) {
        p = win[0] ^ 1;
        if (!(V8(WR(p) + 0xE9) & 4)) V16(WR(p) + 0x94) |= 4;
    }
    return r;
}
