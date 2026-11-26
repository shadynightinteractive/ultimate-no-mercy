/* func_80117438 (ovl_e): the match's decision -- who won (kind: 0x800000, 0x80000 or 0x8000; arg: 1 = forced),
 * calling func_80118A20(winners, losers, result flags, replay). The original sorts the wrestlers into four 4-int
 * stack lists (A, B: sides; C: none; D: per-slot keys) for 4 slots; here n + 1 each. Line by line the original's
 * logic (the asm's order of tests and stores), the lists' room aside. */
#include "six.h"
extern u8 A_80159218[];
void func_80118A20(s32 *win, s32 *lose, u32 flags, s32 replay);
s32 func_8012E040(s32 n);
#define TEAM(i) V32(A_80159218 + 4 * (i))
#define PRESENT(i) (*(volatile s16 *)(WR(i) + 0xA0) != -1)
#define E9(i) V8(WR(i) + 0xE9)
#define F94(i) V16(WR(i) + 0x94)
#define F82(i) V16(WR(i) + 0x82)
#define SCORE(i) (*(volatile s16 *)(WR(i) + 0xAA))
static const u32 teamA_bit[SIX_NSLOTS] = { 0x10, 0x20, 0x40, 0x80 };   /* 0x8014636C (tag matches: 4 slots; the rest 0) */
typedef unsigned long long u64;
REWRITE(80117438, s32, (u32 kind, s32 arg))
{
    s32 A[SIX_NSLOTS + 1], B[SIX_NSLOTS + 1], C[SIX_NSLOTS + 1], D[SIX_NSLOTS + 1];   /* (n + 1 each: room for every slot) */
    s32 n = nslots(), i, k, j;
    u32 MF = V32(0x80097EC0);
    s32 cntA = 0, cntB = 0, appB = 0;             /* t1, s0, sp+0x64 */
    s32 sumA = 0, sumB = 0;                       /* sp+0x5C, fp: scores */
    u64 diagA = 0, diagB = 0;                     /* t4:t5, t2:t3: stat matrix 0's [i][i] */
    s32 teamA = 0, teamB = 0;                     /* s7, s6 */
    s32 best = 0, maxs, mins, minslot = 1;        /* s5, sp+0x74, sp+0x7C, sp+0x6C */
    u32 bestdiag = 0;                             /* sp+0x84 */
    s32 *pa, *pd;                                 /* sp+0xCC (A appends), sp+0xDC (A[i] / D[i]) */
    maxs = SCORE(0); mins = SCORE(1);
    for (i = 0; i < n + 1; i++) { A[i] = -1; B[i] = -1; C[i] = -1; D[i] = 0; }

    if (kind == 0x800000) {
        for (i = 0; i < n; i++)
            if (PRESENT(i) && !(E9(i) & 4) && (E9(i) & 0x10) && !(F94(i) & 0x200)) break;
        if (i == n) {
            for (i = 0, k = 0; i < n; i++) {
                if (!PRESENT(i)) continue;
                if (E9(i) & 4) A[0] = i;
                else if (E9(i) & 0x10) B[k++] = i;
            }
            func_80118A20(A, B, kind | 0x10400, 0);
            return 1;
        }
        if (MF & 0x1000) {
            A[0] = 0; B[0] = 1;
            if (F94(0) & 0x200) func_80118A20(B, A, kind | 0x10400, 0);
            else func_80118A20(A, B, kind | 0x10400, 0);
        }
        if (!(V32(0x80097EC0) & 0x28000)) return 1;
        for (i = 0; i < n; i++)
            if (PRESENT(i) && (F94(i) & 0x200)) {
                B[0] = i; A[0] = V32(WR(i) + 0x08);
                func_80118A20(A, B, kind | 0x10400, 0);
                return 1;
            }
        return 1;
    }

    pa = A; pd = A;
    {
        u32 m_c000 = MF & 0xC000, m_8000 = MF & 0x8000, m_1000 = MF & 0x1000, m_22000 = MF & 0x22000;
        u32 x_80000 = V32(0x800A9950) & 0x80000;
        for (i = 0; i < n; i++, pd++) {
            if (!PRESENT(i) || !(E9(i) & 0x10)) continue;
            if (m_c000) {
                if ((F82(i) & 3) == 0) {
                    if ((V8(WR(i) + 0xF1) & 1) || V32(WR(i) + 0x14) == 0xA3) {
                        if (arg != 1) return 0;
                    } else B[cntB++] = i;
                } else A[cntA++] = i;
                if (m_8000) {
                    if (x_80000) { pd[0] = i; D[pd - A] = TEAM(i); }
                    else {
                        s32 v = SCORE(i);
                        if (v == maxs && (s32)bestdiag < (s32)STAT(0, i, i)) { bestdiag = STAT(0, i, i); best = i; }
                        v = SCORE(i);
                        if (maxs < v) { maxs = v; best = i; }
                        if (v < mins) { mins = v; minslot = i; }
                    }
                }
            }
            if (m_1000) {
                if (A[0] == -1) {
                    A[0] = i; sumA = SCORE(i); diagA = STAT(0, i, i); teamA = TEAM(i); cntA += (F82(i) & 3) == 0;
                } else {
                    B[0] = i; sumB = SCORE(i); diagB = STAT(0, i, i); teamB = TEAM(i); cntB += (F82(i) & 3) == 0;
                }
            }
            if (m_22000) {
                if (MF & teamA_bit[i]) {
                    *pa++ = i; sumA += SCORE(i); diagA += STAT(0, i, i); teamA += TEAM(i); cntA += (F82(i) & 3) == 0;
                } else {
                    B[appB++] = i; sumB += SCORE(i); diagB += STAT(0, i, i); teamB += TEAM(i); cntB += (F82(i) & 3) == 0;
                }
            }
        }
    }

    if (kind == 0x80000) {
        MF = V32(0x80097EC0);
        if (MF & 0x8000) {
            if (cntA == 0) { func_80118A20(C, C, 0x81400, 0); return 1; }
            if (cntA == 1) { func_80118A20(A, B, 0x80400, 0); return 1; }
            func_80118A20(C, B, kind, 0); return 0;
        }
        if (MF & 0x4000) {
            if (arg == 1 || cntA == 1) { func_80118A20(A, B, 0x80400, 0); return 1; }
            if (cntA == 0) { func_80118A20(C, C, 0x81400, 0); return 1; }
            func_80118A20(C, B, kind, 0); return 0;
        }
        if (MF & 0x1000) {
            if (cntA == cntB) func_80118A20(C, C, 0x101400, 1);
            else if (cntA < cntB) func_80118A20(A, B, 0x80400, 1);
            else func_80118A20(B, A, 0x80400, 1);
            return 1;
        }
        if (MF & 0x22000) {
            if (cntA != 0 && cntB != 0) { func_80118A20(C, C, 0x101400, 1); return 1; }
            if (cntA == 0) func_80118A20(A, B, 0x80400, 1);
            if (cntB != 0) return 1;
            func_80118A20(B, A, 0x80400, 1);
            return 1;
        }
        return 1;
    }
    if (kind != 0x8000) return 1;

    MF = V32(0x80097EC0);
    if (MF & 0x4000) { func_80118A20(C, C, 0x9400, 1); return 1; }
    if (!(MF & 0x200000)) {
        if (!(V32(0x800A9950) & 0x80000)) { func_80118A20(C, C, 0x9400, 1); return 1; }
    } else if (!(V32(0x800A9950) & 0x80000)) {
        /* (0x200000, the per-wrestler scores) */
        if (MF & 0x8000) {
            A[1] = -1; B[1] = -1; A[0] = best; B[0] = minslot;
            sumA = SCORE(best); sumB = SCORE(minslot);
            diagA = STAT(0, best, best); diagB = STAT(0, minslot, minslot);
        }
        {
            s32 bwins;
            if (sumA != sumB) bwins = sumA < sumB;
            else if (diagA != diagB) bwins = diagA < diagB;
            else bwins = func_8012E040(500) != 0;
            if (bwins) func_80118A20(B, A, 0x8C00, 1);
            else func_80118A20(A, B, 0x8C00, 1);
        }
        return 1;
    }
    /* (0x800A9950 & 0x80000: by team score) */
    if (V32(0x80097EC0) & 0x8000) {
        for (i = 0; i < n - 2; i++)
            for (j = i + 1; j < n - 1; j++)
                if (D[i] < D[j]) { s32 t = D[j]; D[j] = D[i]; D[i] = t; t = A[j]; A[j] = A[i]; A[i] = t; }
        teamA = D[0]; teamB = D[1];
        B[0] = A[1]; A[1] = -1;
    }
    if (teamA == teamB) func_80118A20(C, C, kind | 0x1400, 1);
    else if (teamA < teamB) func_80118A20(B, A, kind | 0x1000400, 1);
    else func_80118A20(A, B, kind | 0x1000400, 1);
    return 1;
}
