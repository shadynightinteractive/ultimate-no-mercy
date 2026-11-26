/* func_800EC940 (ovl_e): clears model a's row in the three hit-stat matrices (stock 4 x 4 at 0x8009FDF8.., here the
 * engine's 6 x 6: STAT) -- n columns. */
#include "six.h"
REWRITE(800EC940, void, (s32 a))
{
    s32 n = nslots(), b;
    for (b = 0; b < n; b++) { STAT(0, a, b) = 0; STAT(1, a, b) = 0; STAT(2, a, b) = 0; }
}
