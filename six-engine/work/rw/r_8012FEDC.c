/* func_8012FEDC (ovl_e): each wrestler's bone positions from its model (model i = wrestler i): bones 1..19 (stride
 * 0x7C from model bones +0x54) to wrestler +0x120.., and +0x120 = the middle of bones 1 and 23. The original runs a
 * byte offset to 4 * 0xB24; for n wrestlers: n. */
#include "six.h"
extern u8 A_800A1130[];
REWRITE(8012FEDC, void, (void))
{
    s32 n = nslots(), i, j;
    double h = *(volatile double *)0x80151188;
    for (i = 0; i < n; i++) {
        volatile float *b = (volatile float *)(A_800A1130 + i * 0xB24 + 0x54);
        volatile float *w = (volatile float *)(WR(i) + 0x114);
        for (j = 1; j < 0x14; j++) {
            w[3 * j] = b[0x1F * j]; w[3 * j + 1] = b[0x1F * j + 1]; w[3 * j + 2] = b[0x1F * j + 2];
        }
        for (j = 0; j < 3; j++) w[3 + j] = (float)((double)(b[0x1F + j] + b[0x2AA + j]) * h);
    }
}
