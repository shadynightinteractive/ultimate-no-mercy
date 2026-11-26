/* func_800DBA88 (ovl_e): the music for the result: from the last winner's roster id and costume (0x16 if none or
 * out of range; 10 in a mode 0x100000 match that slot 0 didn't win). The original scans result bits 0-3; for n:
 * RES_WON(0..n-1). */
#include "six.h"
s32 func_80013F8C(s32 slot);
s32 func_80013FA4(s32 slot);
s32 func_8000AAF4(s32 id, s32 costume);
REWRITE(800DBA88, s16, (void))
{
    s32 n = nslots(), i, idx = -1, id = 0, cos = 0, r;
    if ((V32(0x800A9950) & 0x100000) && !(RESULT & 1)) return 10;
    for (i = 0; i < n; i++) if (RES_WON(i)) idx = i;
    if ((s16)idx >= 0) { id = func_80013F8C((s16)idx); cos = func_80013FA4((s16)idx); }
    if ((s16)id <= 0) return 0x16;
    r = func_8000AAF4((s16)id, (s16)cos);
    if ((u16)(r - 0x17) < 0x39) return (s16)r;
    return 0x16;
}
