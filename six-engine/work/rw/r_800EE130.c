/* func_800EE130 (ovl_e): two slot masks in one u16 -- wrestler state +0x14 == 0xC4, and +0x14 == 0xBD with +0x18 == 3.
 * The original: bits 4-7 and bits 0-3 (its consumer tests 0x11 << slot). Here, for any count: bits 6-11 and 0-5
 * (the consumer's 0x41 << slot: build_rom.MASK_IMMS). */
#include "six.h"
REWRITE(800EE130, u16, (void))
{
    s32 n = nslots(), i;
    u32 m = 0;
    for (i = 0; i < n; i++) {
        u8 *w = WR(i);
        if (*(volatile s16 *)(w + 0xA0) == -1) continue;
        if (V32(w + 0x14) == 0xC4) m |= 0x40u << i;
        if (V32(w + 0x14) == 0xBD && V32(w + 0x18) == 3) m |= 1u << i;
    }
    return (u16)m;
}
