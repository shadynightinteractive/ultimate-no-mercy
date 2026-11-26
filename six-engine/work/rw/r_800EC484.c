/* func_800EC484 (ovl_e): pick the nearest other wrestler as the new target (+0x08). The original keeps the
 * candidates in a 4-entry stack list (3 others + one spare, cleared to -1); for n wrestlers: n entries. With more
 * than 4 it goes round them all in distance order (below). */
#include "six.h"
float func_8012EA74(void *a, void *b);
void func_800E144C(s32 a, s32 b);
REWRITE(800EC484, s32, (u8 *self, u32 flags))
{
    struct { s32 slot, unused; float d; } e[SIX_NSLOTS], t;
    s32 n = nslots(), k = 0, i, j;
    if (!(flags & 1)) return 0;
    for (i = 0; i < n; i++) e[i].slot = -1;
    for (i = 0; i < n; i++)
        if (*(volatile s16 *)(WR(i) + 0xA0) != -1 && i != *(volatile s16 *)(self + 0x9E)) {
            e[k].slot = i; e[k].d = func_8012EA74(self + 0x120, WR(i) + 0x120); k++;
        }
    for (i = 0; i < k - 1; i++)
        for (j = i + 1; j < k; j++)
            if (e[j].d < e[i].d) { t = e[i]; e[i] = e[j]; e[j] = t; }
    if (n > 4) {
        /* six: the next one out in distance order after the current target (wrapping round to the nearest), so
         * that pressing it again reaches every wrestler -- the original's "nearest but the current" only ever
         * swaps between the two nearest, which with 5 others can leave out the one the computer wants (its
         * brawl partner in func_800F0EF0: it presses this until its target is that one) */
        s32 cur = V32(self + 0x8), at = -1;
        for (i = 0; i < k; i++) if (e[i].slot == cur) at = i;
        for (j = 1; j <= k; j++) {
            i = (at + j) % k;
            if (at < 0) i = j - 1;
            if (e[i].slot == V32(self + 0xC) || e[i].slot == cur) continue;
            V32(self + 0x8) = e[i].slot;
            func_800E144C(*(volatile s8 *)(self + 0x9F), (s8)e[i].slot);
            return 1;
        }
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (e[i].slot == V32(self + 0xC)) e[i].slot = -1;
        if (e[i].slot == V32(self + 0x8)) e[i].slot = -1;
    }
    for (i = 0; i < n; i++)
        if (e[i].slot != -1) {
            V32(self + 0x8) = e[i].slot;
            func_800E144C(*(volatile s8 *)(self + 0x9F), (s8)e[i].slot);
            return 1;
        }
    return 0;
}
