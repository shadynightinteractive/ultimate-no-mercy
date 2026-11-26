/* func_80018258 (main): clears the 4 controller ports' rumble bytes (0x80076568, 4 each) and, per slot, the human
 * flag 0x8009ED20 and model byte 0x800A10CC. The original does both in one 4-pass loop; ports stay 4, slots n. */
#include "six.h"
extern u8 A_8009ED20[], A_800A10CC[];
REWRITE(80018258, void, (void))
{
    s32 n = nslots(), i;
    for (i = 0; i < 4; i++) {
        V8(0x80076568 + 4 * i) = 0; V8(0x80076569 + 4 * i) = 0; V8(0x8007656A + 4 * i) = 0; V8(0x8007656B + 4 * i) = 0;
    }
    for (i = 0; i < n; i++) { V8(A_8009ED20 + i) = 0; V8(A_800A10CC + i) = 0; }
}
