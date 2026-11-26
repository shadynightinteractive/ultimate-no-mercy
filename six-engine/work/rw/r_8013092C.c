/* func_8013092C: the instant replay's recorder, each match frame -- one 12-byte sample per wrestler and per prop
 * into 600-frame rings (func_801308FC set them up), then: is a replay due (a wrestler's replay-worthy move,
 * 0x8014DB60 bits, lands on a legal opponent), and its start (playback cursor, camera word, "snap" bits). For n
 * wrestlers: n rings and n slots' bits (rw/r_tables.c: rw_tab_DB60 / _DB70 / _DB78; slots 4 and 5 get bits
 * 0x200000/0x400000 and 0x800000/0x1000000 -- never used by anything else in 0x80162A7C), and the lock of
 * 0x8016B650 (0xF, one bit per model) has models 4 and 5 at 0x1000/0x2000 (rw_tab_65C8): 0x300F. */
#include "six.h"
extern u8 A_801593D8[];
extern const u32 rw_tab_DB60[], rw_tab_DB78[];
extern const u16 rw_tab_DB70[];
extern u32 rw_rp_worthy;   /* slots 6 on: which are replay-worthy (they share F's 0x10000000; rw/stubs.S rw_rp_set_a0) */
#define F       V32(0x80162A7C)
#define LOCKS   (V16(0x8016B650) & 0x300F)
#define S32(a)  (*(volatile s32 *)(a))
#define F32(a)  (*(volatile float *)(a))
#define REC     V32(0x801593E8)            /* the wrestlers' rings */
#define PROPREC V32(0x801593EC)            /* the props' */
#define POS     S32(0x801593F8)            /* the record cursor 0..599 */
#define RSLOT   S32(0x801593F0)            /* the replay's slot */
#define CUR     S32(0x80159404)            /* the playback cursor */
REWRITE(8013092C, void, (void))
{
    s32 n = nslots(), i, arena = S32(0x80159628), alive;
    u32 snap;
    if (!(F & 4)) {
        u8 fl = V8(0x8016B928);
        for (i = 0; i < n; i++) {
            volatile u8 *w = WR(i);
            volatile u16 *s = (volatile u16 *)(REC + i * 0x1C20 + POS * 12);
            s[5] = 0;
            if (*(volatile s16 *)(w + 0xA0) == -1) {
                s[4] = 0xFFFF;
            } else {
                s32 fr, ang; u16 a, fx, mk = 0;
                s[4] = *(volatile u16 *)(w + 0xA6);
                fr = *(volatile s16 *)(w + 0xA4) - 1;
                s[5] = (fr < 0 ? 0 : fr) & 0x3FF;
                s[0] = (s16)(s32)((double)F32(w + 0xFC) * 10.0);
                s[1] = (s16)(s32)((double)F32(w + 0x100) * 10.0);
                s[2] = (s16)(s32)((double)F32(w + 0x104) * 10.0);
                ang = S32(w + 0x10C);
                a = (u16)(ang / 4) & 0x3FF;
                s[3] = a;
                fx = *(volatile u16 *)(w + 0x8E);
                mk = (fx & 2) ? 0x8000 : 0;
                if (fx & 4) mk = 0x4000;
                if (fx & 8) mk = 0x2000;
                if (fx & 0x10) mk = 0x1000;
                if (fx & 0x20) mk = 0x800;
                s[3] = a | mk;
                switch (*(volatile u32 *)(A_801593D8 + 4 * i)) {
                case 2: case 3: mk = 0x8000; break;
                case 6: mk = 0x4000; break;
                default: mk = 0;
                }
                s[5] |= mk;
            }
            if (i == 0) {                  /* (the arena's effects ride on slot 0's samples) */
                if (arena < 10) {
                    if (fl & 4) s[5] |= 0x2000;
                    if (fl & 1) s[5] |= 0x1000;
                    if (fl & 2) s[5] |= 0x800;
                } else if (arena == 0x15) {
                    if (fl & 8) s[5] |= 0x2000;
                } else if (arena == 0x18) {
                    if (fl & 0x10) s[5] |= 0x2000;
                    if (fl & 0x20) s[5] |= 0x1000;
                    if (fl & 0x40) s[5] |= 0x800;
                }
            }
        }
        for (i = 0; i < 7; i++) {
            volatile u8 *p = (volatile u8 *)(0x8016AE10 + i * 0xB4);
            volatile u8 *s = (volatile u8 *)(PROPREC + i * 0x1C20 + POS * 12);
            u8 b = (u8)V32(p + 8) & 7;
            s[0] = b;
            if (i == 6 && (*(volatile u16 *)(p + 0x20) & 0x8000)) s[0] = b | 0x80;
            s[1] = (u8)V32(p + 4);
            *(volatile s16 *)(s + 2) = (s16)(s32)((double)F32(p + 0x44) * 10.0);
            *(volatile s16 *)(s + 4) = (s16)(s32)((double)F32(p + 0x48) * 10.0);
            *(volatile s16 *)(s + 6) = (s16)(s32)((double)F32(p + 0x4C) * 10.0);
            s[8]  = (u8)(s32)((double)S32(p + 0x50) * 0.0625);
            s[9]  = (u8)(s32)((double)S32(p + 0x54) * 0.0625);
            s[10] = (u8)(s32)((double)S32(p + 0x58) * 0.0625);
            s[11] = (u8)(s32)((double)S32(p + 0x5C) * 0.0625);
        }
        POS = POS + 1;
        if (POS >= 600) { POS = 0; F |= 0x80000; }
    }
    if (!(F & 0x10000000)) rw_rp_worthy = 0;   /* (the game cleared 6 on's shared bit: none of them now) */
    if (LOCKS) return;
    if (!(F & 0x100) && V8(0x800A946A) == 0) {
        u32 mf, f = F;
        alive = 0;
        for (i = 0; i < n; i++)
            if (*(volatile s16 *)(WR(i) + 0xA0) != -1) alive += (V8(WR(i) + 0xE9) & 0x10) != 0;
        mf = V32(0x80097EC0);
        if ((mf & 0x1000) || ((mf & 0xC000) && alive < 3)) {
            for (i = 0; i < n; i++) {
                volatile u8 *w = WR(i);
                if (*(volatile s16 *)(w + 0xA0) == -1 || !(V8(w + 0xE9) & 0x10)) continue;
                if (!(V8(WR(S32(w + 8)) + 0xE9) & 0x10)) continue;
                if (*(volatile s16 *)(w + 0xA4) != V8(w + 0x302) + 0xF) continue;
                if (!(f & rw_tab_DB60[i])) continue;
                if (i >= 6 && !(rw_rp_worthy & (1u << i))) continue;   /* (6 on: this one's own) */
                RSLOT = i;
                S32(0x801593F4) = S32(w + 8);
                F = rw_tab_DB60[i] | (f & 0x80000) | 1;
                S32(0x801593FC) = 2;
                S32(0x80159400) = -15;
                S32(0x80159414) = 1;
                CUR = POS;
                S32(0x8015940C) = POS;
                break;
            }
        }
    }
    if (LOCKS || !(F & 1)) return;
    snap = 0;
    for (i = 0; i < n; i++) snap |= rw_tab_DB78[i];
    F = (F & 0xFFEFFFFE) | 0x0A | snap;           /* (stock 0xFA: 0x0A and the four slots' 0x10..0x80) */
    CUR = CUR + S32(0x80159400);
    if (CUR >= 600) CUR = CUR - 600;
    if (CUR < 0) {
        if (F & 0x80000) CUR = CUR + 600;
        else CUR = 0;
    }
    if (F & 0x100) {
        u32 base = RSLOT * 0x1C20 + REC;
        if ((*(volatile u16 *)(base + CUR * 12 + 10) & 0x3FF) >= 2) {
            do {
                s32 c = CUR - 1;
                if (c < 0) {
                    if (!(F & 0x80000)) { CUR = 0; break; }
                    c = CUR + 599;
                }
                CUR = c;
            } while ((*(volatile u16 *)(base + CUR * 12 + 10) & 0x3FF) >= 2);
        }
    }
    S32(0x80159418) = 0;
    S32(0x80159408) = CUR;
    S32(0x80159410) = (s32)(s16)rw_tab_DB70[RSLOT] | 0xDE;
}
