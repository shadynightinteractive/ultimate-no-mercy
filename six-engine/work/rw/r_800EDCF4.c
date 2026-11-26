/* func_800EDCF4 (ovl_e): start of a sequence (a run-in, a team move...): the sequence slot list 0x80159608 (moved:
 * one int per slot) = [the wrestler | 0x100 or 0x200, the partner | 0x400, the others by distance]. The original
 * clears 4 entries and sorts up to 3 others into a 14-float stack list starting at sp+0x10 -- index 0 isn't used, so
 * with 6 its entries would reach the saved registers; for n wrestlers: n entries. */
#include "six.h"
extern u8 A_80159608[];
#define SEQ(i) V32(A_80159608 + 4 * (i))
float func_8012EA74(void *a, void *b);
void func_800DA2F0(s32 a);
void func_800EB5E0(void *w, s32 a);
REWRITE(800EDCF4, s32, (u8 *w, u8 *partner))
{
    float d[SIX_NSLOTS + 2];   /* (index 0 unused, then up to n - 1 others: room for every slot, 2026-11-06) */
    s32 n = nslots(), k, first, cnt, i, j;
    volatile s16 *tab;
    s16 code;
    if (V32(0x80097EC0) & 0x100000) return 0;
    if (V32(0x800A9950) & 0x200000) return 0;
    V16(w + 0x8C) |= 0x8000;
    V32(0x8016B348) = V32(0x80159628);
    tab = (volatile s16 *)0x80145140;
    if (tab[0] == -1) return 0;
    for (k = 0; tab[2 * k] != -1; k++) {
        if (tab[2 * k] != *(volatile s16 *)(*(u8 *volatile *)(w + 0x348) + 0x24)) continue;
        code = tab[2 * k + 1];
        if (code & 0x8000) V32(0x80159670) = V32(0x80159628) + 10;
        else if (code & 0x4000) V32(0x80159670) = V32(0x80159628) - 10;
        else if (code & 0x2000) V32(0x80159670) = V32(0x8016B340) + 10;
        else V32(0x80159670) = code & 0xFF;
        func_800DA2F0(V32(0x80159670));
        func_800EB5E0(w, 0xBE);
        if (code & 0x1000) {
            V8(0x8016B598) |= 4;
            *(volatile s16 *)(w + 0xD0) = V32(w + 0x1C) == 3 ? 6 : 4;
        } else *(volatile s16 *)(w + 0xD0) = 10000;
        if (partner) {
            func_800EB5E0(partner, 0xBE);
            V16(partner + 0xD0) = V16(w + 0xD0);
        }
        for (i = n - 1; i >= 0; i--) SEQ(i) = -1;
        if (V32(w + 0x1C) == 3) {
            SEQ(0) = (s16)(V16(w + 0x9E) | 0x100);
            first = 1;
            V16(w + 0x84) |= 1;
        } else {
            SEQ(0) = (s16)(V16(w + 0x9E) | 0x200);
            SEQ(1) = (s16)(V16(partner + 0x9E) | 0x400);
            first = 2;
        }
        cnt = 0;
        for (i = 0; i < n; i++)
            if (*(volatile s16 *)(WR(i) + 0xA0) != -1 && (u32)i != (SEQ(0) & 0xFF) && (u32)i != (SEQ(1) & 0xFF)) {
                SEQ(first + cnt) = i;
                d[first + cnt] = func_8012EA74(w + 0xFC, WR(i) + 0xFC);
                cnt++;
            }
        for (i = first; i < first + cnt - 1; i++)
            for (j = i + 1; j < first + cnt; j++)
                if (d[j] < d[i]) {
                    float t = d[j]; s32 s;
                    d[j] = d[i]; d[i] = t;
                    s = SEQ(j); SEQ(j) = SEQ(i); SEQ(i) = s;
                }
        return 1;
    }
    return 0;
}
