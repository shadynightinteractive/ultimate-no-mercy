/* func_8013F820 (ovl_e): 0 if a wrestler in the same group (+0xCC == the argument's) is in a state that blocks
 * (+0x14 == 0x18 with +0x18 < 5, 0x0A, 0x1C, 0x74, or +0xEC & 8), else 1. The original's end pointer is
 * slot 0 + 4 * 0x358; for n wrestlers: n. */
#include "six.h"
REWRITE(8013F820, s32, (u8 *a))
{
    s32 n = nslots(), i, st;
    for (i = 0; i < n; i++) {
        u8 *w = WR(i);
        if (*(volatile s16 *)(w + 0xA0) == -1 || *(volatile s16 *)(w + 0xCC) != *(volatile s16 *)(a + 0xCC)) continue;
        st = V32(w + 0x14);
        if (st == 0x18 && V32(w + 0x18) < 5) return 0;
        st = V32(w + 0x14);
        if (st == 0x0A || st == 0x1C || st == 0x74) return 0;
        if (V8(w + 0xEC) & 8) return 0;
    }
    return 1;
}
