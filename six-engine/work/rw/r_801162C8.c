/* func_801162C8 (ovl_e): a byte, bit i = slot i present with +0xE9 & 0x10 set and & 8 clear. The original walks
 * slots 3..0 (byte offset 3 * 0x358 down); for n wrestlers: n-1..0 (bits 0-5 fit the byte). */
#include "six.h"
REWRITE(801162C8, u8, (void))
{
    s32 i;
    u32 m = 0;
    for (i = nslots() - 1; i >= 0; i--) {
        u8 *w = WR(i);
        m <<= 1;
        if (*(volatile s16 *)(w + 0xA0) != -1 && !(V8(w + 0xE9) & 8) && (V8(w + 0xE9) & 0x10)) m |= 1;
    }
    return (u8)m;
}
