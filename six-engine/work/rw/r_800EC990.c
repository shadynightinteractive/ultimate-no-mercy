/* func_800EC990 (ovl_e): hit-stat matrix t (0..2) counts: [a][a] and [a][b] + 1 (unless 0xFFFF), a = w's model,
 * b = other's. (STAT: the engine's 6 x 6 matrices.) */
#include "six.h"
REWRITE(800EC990, void, (u8 *w, u8 *other, s32 t))
{
    s32 a, b;
    if (t != 0 && t != 1 && t != 2) return;
    a = *(volatile s16 *)(w + 0x9E);
    if (STAT(t, a, a) <= 0xFFFE) STAT(t, a, a) = STAT(t, a, a) + 1;
    a = *(volatile s16 *)(w + 0x9E); b = *(volatile s16 *)(other + 0x9E);
    if (STAT(t, a, b) <= 0xFFFE) STAT(t, a, b) = STAT(t, a, b) + 1;
}
