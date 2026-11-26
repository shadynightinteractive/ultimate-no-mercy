/* func_80117F28 (ovl_e): the match's per-frame wrestler update, in round-robin order from 0x801591F8.
 * The original wraps with `& 3`; here `% n` (n = the wrestler count). Mode 0 clears the state histories. */
#include "six.h"
extern u8 A_8016B5A0[];                 /* state history, 16 shorts per slot */
extern u8 A_8009ED20[];                 /* slot human flag, a byte per slot */
#define RR_START V32(0x801591F8)
#define H88(i) V16(WR(i) + 0x88)
void func_80118368(s32 slot);
void func_8012FEDC(void);
void func_80140A84(void);
u32 func_8012EA9C(void *a, void *b);
void func_800EBA84(void *w, u32 v, s32 a, s32 b);
void func_80142620(void);
void func_801442F8(void);
void func_8012FA78(void);
void func_8013092C(void);

REWRITE(80117F28, void, (s32 mode))
{
    s32 n = nslots(), s, k;
    if (mode == 0) {
        RR_START = 0;
        for (s = 0; s < n; s++) {
            volatile s16 *h = (volatile s16 *)(A_8016B5A0 + s * 0x20);
            for (k = 0xF; k >= 0; k--) h[k] = 0;
        }
        return;
    }
    if (mode != 1) return;
    for (s = 0; s < n; s++) H88(s) &= 0xFFED;
    func_8012FEDC();
    s = RR_START;
    do {
        u16 f = H88(s);
        if ((f & 4) && !(f & 2)) {
            func_80118368(s);
            for (k = 0; k < 2; k++) {
                s32 o = V32(WR(s) + 0x30 + k * 4);
                if (!(H88(o) & 2) && o != -1) func_80118368(o);
            }
        }
        s = (s + 1) % n;
    } while (s != RR_START);
    do {
        if (!(H88(s) & 2)) func_80118368(s);
        s = (s + 1) % n;
    } while (s != RR_START);
    do {
        if (H88(s) & 1) func_80118368(s);
        s = (s + 1) % n;
    } while (s != RR_START);
    if (V8(0x8016B598) & 8) func_80140A84();
    s = RR_START;
    do {
        if (H88(s) & 1) func_80118368(s);
        s = (s + 1) % n;
    } while (s != RR_START);
    RR_START = (s + 1) % n;
    for (s = 0; s < n; s++) {
        if (V16(WR(s) + 0x8A) & 0x200) {
            s32 o = V32(WR(s) + 0x08);
            u32 r = func_8012EA9C(WR(s) + 0x114 + 0xC, WR(o) + 0x114 + 0xC);
            func_800EBA84(WR(s), r, 0x155, 0);
        }
    }
    func_80142620();
    func_801442F8();
    for (s = 0; s < n; s++) A_8009ED20[s] = (V8(WR(s) + 0xE9) & 8) ? 0 : 1;
    func_8012FA78();
    func_8013092C();
    for (s = 0; s < n; s++) {
        s32 v = V32(WR(s) + 0x14);
        volatile s16 *h = (volatile s16 *)(A_8016B5A0 + s * 0x20);
        if (v != 0x30 && v != 1 && (v != h[0] || (H88(s) & 0x10))) {
            for (k = 0xF; k > 0; k--) h[k] = h[k - 1];
            h[0] = (s16)V32(WR(s) + 0x14);
        }
    }
}
