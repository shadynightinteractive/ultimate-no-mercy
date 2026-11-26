/* func_801308FC: the instant replay's start -- flags cleared, the sample buffers in the block func_80113E30
 * allocates (0x8016B57C, 0x2BF20 bytes): one 0x1C20 ring of 600 samples per wrestler, then the props' 7. With n
 * wrestlers the props' rings start after n of them (stock: 4 -> +0x7080; 6 + 7 rings = 0x16DA0 still fit). */
#include "six.h"
REWRITE(801308FC, void, (void))
{
    u32 base = V32(0x8016B57C);
    V32(0x80162A7C) = 0;
    V32(0x801593F8) = 0;
    V32(0x801593E8) = base;
    V32(0x801593EC) = base + nslots() * 0x1C20;
}
