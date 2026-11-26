/* The results table after a match (ovl_c, scr 28): 4 columns, each from one of the per-slot records 0x800A3DC0
 * (0x1C bytes: +0/+1 shown, +2.. the counts, +0x18 the entrant). The table's readers take rw_colrec (build_rom
 * SEQS) -- here the records 0..3, or after a six-man Royal Rumble: the winner, then the three who lasted longest
 * (the latest elimination times 0x8009FE58). rw_col_result is func_8010AED8 (winner 1 / loser 2 / none 3) for the
 * slot a column shows. */
#include "six.h"
extern u8 A_800A3DC0[], A_8009FE58[], A_800A4038[];
extern s32 rw_last_count;
s32 func_8010AED8(s32 col);
u8 rw_colrec[4 * 0x1C] __attribute__((aligned(4)));   /* (word reads: +0x18 the entrant) */
static s8 colmap[4] = { 0, 1, 2, 3 };
static s8 mapped;
#define MENU_FLAGS V32(0x800A957C)
static s32 any_winner(void)
{
    s32 i;
    for (i = 0; i < SIX_NSLOTS; i++) if (RES_WON(i)) return 1;
    return 0;
}
void rw_colrec_fill(void)
{
    s32 i, j, k, n = 0, order[SIX_NSLOTS];
    mapped = rw_last_count >= 6 && (MENU_FLAGS & 0x200);
    if (mapped) {
        for (i = 0; i < rw_last_count && i < SIX_NSLOTS; i++) if (V32(A_800A4038 + 4 * i) > 0) order[n++] = i;  /* the slots with a wrestler */
        /* the winner first, then the latest eliminated first (a slot still in has time -1: after the winner) */
        for (i = 0; i < n; i++)
            for (j = i + 1; j < n; j++) {
                s32 a = order[i], b = order[j], wa = RES_WON(a), wb = RES_WON(b);
                s32 ta = V32(A_8009FE58 + 4 * a), tb = V32(A_8009FE58 + 4 * b);
                if (wb > wa || (wb == wa && tb > ta)) { order[i] = b; order[j] = a; }
            }
        for (i = 0; i < 4; i++) colmap[i] = i < n ? order[i] : -1;
    } else
        for (i = 0; i < 4; i++) colmap[i] = i;
    for (i = 0; i < 4; i++)
        for (k = 0; k < 0x1C; k++)
            rw_colrec[i * 0x1C + k] = colmap[i] < 0 ? 0 : V8(A_800A3DC0 + colmap[i] * 0x1C + k);
}
s32 rw_col_result(s32 col)
{
    s32 s;
    if (!mapped) return func_8010AED8(col);
    s = colmap[col & 3];
    if (!any_winner()) return 3;
    return s >= 0 && RES_WON(s) ? 1 : 2;
}
