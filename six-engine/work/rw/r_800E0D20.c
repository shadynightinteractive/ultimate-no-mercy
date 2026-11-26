/* func_800E0D20 (ovl_e, the Royal Rumble's entrant count: 3 digit objects, the first of the per-slot groups at
 * 0x80157F60, 0x294 each; the digits last drawn at 0x801589B0): the original draws it at the top centre (x 243 for
 * the units, 14 less for each digit after, y 18). With more than four wrestlers the panels take the top and bottom
 * centre (rw/r_hud.c), and the count goes up into the strip above the top row, beside the clock. */
#include "six.h"
void func_8000FA54(void *obj, void *spr, float x, float y);
s32 func_80131BA0(void);
extern u8 A_80157F60[], A_801589B0[];
#define DIG_OBJ(g, k) (A_80157F60 + (g) * 0x294 + (k) * 0xDC)
#define DIG_FLAGS(g, k) (**(volatile u32 **)(DIG_OBJ(g, k) + 0x14))
REWRITE(800E0D20, void, (s32 a0))
{
    s16 c = (s16)a0;
    s32 g, k, v, x, six = nslots() > 4;
    if (func_80131BA0() != 0 || V16(0x801594DC) == 0) {
        for (g = 0; g < nslots(); g++) for (k = 0; k < 3; k++) DIG_FLAGS(g, k) |= 0x80;
        return;
    }
    if (c < 0) { for (k = 0; k < 3; k++) DIG_FLAGS(0, k) |= 0x80; return; }
    v = c >= 1000 ? 999 : c;
    x = six ? RW_COUNT6_X : 243;
    for (k = 0; k < 3; k++, x -= 14) {
        s8 d = (s8)(v % 10);
        v /= 10;
        if (*(volatile s8 *)(A_801589B0 + k) == d) continue;
        *(volatile s8 *)(A_801589B0 + k) = d;
        func_8000FA54(DIG_OBJ(0, k), (u8 *)0x80153730 + d * 8, (float)x, six ? RW_HUD6_TOP_Y : 18.0f);
    }
    for (k = 0; k < 3; k++) DIG_FLAGS(0, k) &= ~0x80u;
}
