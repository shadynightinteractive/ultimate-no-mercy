/* func_800DFED0 (ovl_e, the match clock: minutes a1, seconds a0, as "MM:SS" in 5 glyph objects at 0x801532E0,
 * 0xDC each, the glyphs at 0x80153730 by digit, 10 the colon): the original draws it at the bottom centre (x from
 * 0x80144B54, y 208). With more than four wrestlers the panels take the top and bottom centre (rw/r_hud.c), and the
 * clock goes up into the strip above the top row, beside the entrant count (rw/r_800E0D20.c). */
#include "six.h"
void func_8000FA54(void *obj, void *spr, float x, float y);
void func_800DDA6C(void);
extern u16 D_80144B54[];
#define CLOCK_OBJ(k) ((u8 *)0x801532E0 + (k) * 0xDC)
#define CLOCK_SEEN(k) (*(volatile s8 *)(0x801532C8 + (k)))
REWRITE(800DFED0, void, (s32 a0, s32 a1))
{
    s16 sec = (s16)a0, min = (s16)a1;
    s8 d[5];
    s32 k, six = nslots() > 4;
    if (min > 60) { min = 60; sec = 0; }
    if (**(volatile u32 **)(CLOCK_OBJ(0) + 0x14) & 0x80)
        for (k = 0; k < 5; k++) { CLOCK_SEEN(k) = -1; **(volatile u32 **)(CLOCK_OBJ(k) + 0x14) &= ~0x80u; }
    if (min == 0 && sec == 0 && V16(0x80154632) == 0) func_800DDA6C();
    d[0] = (min % 100) / 10; d[1] = min % 10; d[2] = 10; d[3] = (sec % 100) / 10; d[4] = sec % 10;
    for (k = 0; k < 5; k++) {
        if (d[k] == CLOCK_SEEN(k)) continue;
        CLOCK_SEEN(k) = d[k];
        func_8000FA54(CLOCK_OBJ(k), (u8 *)0x80153730 + d[k] * 8,
                      (float)(six ? D_80144B54[k] + RW_CLOCK6_DX : D_80144B54[k]), six ? RW_HUD6_TOP_Y : 208.0f);
    }
}
