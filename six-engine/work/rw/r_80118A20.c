/* func_80118A20 (ovl_e): the match result. win / lose: up to 3 slots each, -1 = none (the lists stop at the first
 * -1); flags: the result kind (0x800A10EC's flags). Sets the elimination times, the result mask (winners, losers:
 * RES_SET_WIN / RES_SET_LOSE -- slots 4 and 5 in RES6), the winners' +0x94 |= 4 and the losers' |= 1, the per-slot
 * result bytes and flags for the menus. The original clears +0x94 bit 2 of slots 0-3 unrolled and walks 4 slots at
 * the end; for n: n. */
#include "six.h"
extern u8 A_8009FE58[], A_8009FDF4[];
void func_80119328(void);
void func_80119C20(void);
s32 func_801199F0(s32 slot);
void func_800E12F8(u32 r);
void rw_colrec_fill(void);
/* 0x8009FE68 |= per slot (0x80146580: 4, 8, 0x10, 0x20) when a player had it; slots 4 and 5 are always the
 * computer's (rw_rumble_fill: pad 0), and the word has no bits for them */
static const u32 human_bit[SIX_NSLOTS] = { 4, 8, 0x10, 0x20 };   /* (slots 4 on: 0) */
static void setbit128(u32 v)
{
    volatile u32 *set = (volatile u32 *)(v < 0x41 ? 0x80159228 : 0x80159230);
    u32 sh = (v < 0x41 ? 0x40 : 0x80) - v;
    if (sh & 0x20) set[0] |= 1u << (sh & 31);
    else set[1] |= 1u << (sh & 31);
}
REWRITE(80118A20, void, (s32 *win, s32 *lose, u32 flags, s32 replay))
{
    s32 n = nslots(), k, i, s, sumw = 0, cntw = 0, suml = 0, cntl = 0, avgw, avgl;
    s32 *p;
    if (replay) func_80119328();
    for (k = 0; k < 3; k++)
        if (lose[k] != -1) V32(A_8009FE58 + 4 * lose[k]) = V32(0x8016B930) * 60 + V32(0x8016B934);
    *(volatile s16 *)0x800A8784 = 0;
    *(volatile s16 *)0x800A84C0 = (s16)V32(0x8016B930);
    *(volatile s16 *)0x800A84C2 = (s16)V32(0x8016B934);
    *(volatile u16 *)0x800A84C4 = (lose[0] == -1 || !(flags & 0x6000)) ? 0xFFFF : V16(WR(lose[0]) + 0xD6);
    V32(0x80162A84) = win[0];
    RESULT = flags; RES6 = 0;
    if ((V32(0x80097EC0) & 0x22000) && win[0] != -1 && lose[0] != -1)
        RESULT = (V8(WR(win[0]) + 0xE9) & 0x20) ? flags | 0x100 : flags | 0x200;
    for (i = 0; i < n; i++) V16(WR(i) + 0x94) &= 0xFFFB;
    for (k = 0, p = win; k < 3; k++) {
        if (*p == -1) continue;
        RES_SET_WIN(*p);
        V16(WR(*p) + 0x94) |= 4;
        sumw += *(volatile s16 *)(WR(*p) + 0xAA);
        cntw++; p++;
    }
    avgw = cntw ? sumw / cntw : sumw;
    for (k = 0, p = lose; k < 3; k++) {
        if (*p == -1) continue;
        s = *p;
        RES_SET_LOSE(s);
        V16(WR(s) + 0x94) |= 1;
        if ((V32(0x80097EC0) & 0x204000) == 0x204000) V8(WR(s) + 0xE9) |= 8;
        if ((V32(0x80097EC0) & 0x4000) && (V32(0x800A9950) & 0x100000) && (V16(WR(0) + 0x94) & 4))
            setbit128(V8(WR(*p) + 0xF8));
        if (func_801199F0(*p)) V8(WR(*p) + 0xF1) |= 1;
        else V8(WR(*p) + 0xE9) &= 0xEF;
        suml += *(volatile s16 *)(WR(*p) + 0xAA);
        cntl++; p++;
    }
    avgl = cntl ? suml / cntl : suml;
    V32(0x800A84C8 + 12 * *(volatile s16 *)0x800A8784) = RESULT;
    func_80119C20();
    V32(0x80159208) = avgw - avgl;
    if (V32(0x80159208) < 0) V32(0x80159208) = 0;
    V32(0x80159208) = (s32)((double)V32(0x80159208) * *(volatile double *)0x80150A88);
    if (V32(0x80097EC0) & 0x1000) {
        if (V8(WR(0) + 0xF1) & 8) V32(0x8009FE68) |= 1;
        if (V8(WR(1) + 0xF1) & 8) V32(0x8009FE68) |= 2;
    }
    if (flags & 0x400) {
        V32(0x800A10E0) = V32(0x80159214) - 1;
        V32(0x800AA068) = V32(0x80159228); V32(0x800AA06C) = V32(0x8015922C);
        V32(0x800AA070) = V32(0x80159230); V32(0x800AA074) = V32(0x80159234);
        V8(0x8009FDF2) = (u8)V32(0x8016B930);
        V8(0x8009FDF3) = (u8)V32(0x8016B934);
        V8(0x8009FDF1) = (u8)V32(0x80159628);
        for (i = 0; i < n; i++) {
            if (*(volatile s16 *)(WR(i) + 0xA0) == -1) continue;
            V8(A_8009FDF4 + i) = (u8)V16(WR(i) + 0xAA);
            if (V8(WR(i) + 0xF3) & 2) V32(0x8009FE68) |= human_bit[i];
        }
    }
    *(volatile s16 *)0x800A8784 = *(volatile s16 *)0x800A8784 + 1;
    func_800E12F8(RESULT);
    rw_colrec_fill();      /* (the results table's columns: ready before ovl_c draws them -- rw/r_results.c) */
}
