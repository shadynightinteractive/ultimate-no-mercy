/* The six-man engine's rewritten functions: a game function whose slot handling can't be changed by an immediate
 * (a round-robin `& 3`, a 4-entry stack list, a mask with flags above the slot bits ...), written again in C for
 * any wrestler count. The same source builds
 *   - the oracle's test blob (rw/build.sh: MIPS code at 0x80780000, each game function's first two instructions
 *     `j rw_<addr>; nop`, applied in RAM by oracle3.py), and
 *   - the PC port's mod (RECOMP_PATCH on the game function).
 * Moved arrays are A_<old base> (linker symbols at their place in the build's layout: syms_<layout>.ld); game
 * functions are called by their address names. The match's wrestler count is $gp (see loops.apply_gp). */
#ifndef SIX_H
#define SIX_H
typedef unsigned char u8; typedef signed char s8; typedef unsigned short u16; typedef short s16;
typedef unsigned int u32; typedef int s32;
/* the slots the engine has room for (spec.py NSLOTS, given by rw/build.py): the size of its own per-slot arrays and
 * loops that run outside a match (where $gp is 4) -- a match's own count is still nslots() */
#ifndef SIX_NSLOTS
#define SIX_NSLOTS 6
#endif
static inline s32 nslots(void) { s32 n; __asm__ volatile("move %0, $gp" : "=r"(n)); return n; }
#define V32(a) (*(volatile s32 *)(a))
#define V16(a) (*(volatile u16 *)(a))
#define V8(a)  (*(volatile u8 *)(a))
#ifdef SIX_RECOMP
#define REWRITE(addr, ret, args) RECOMP_PATCH ret func_##addr args
#else
#define REWRITE(addr, ret, args) ret rw_##addr args
#endif
/* the wrestler structs (0x358 each) */
extern u8 A_80159990[];
#define WR(i) (A_80159990 + (i) * 0x358)
/* the match result mask 0x800A10EC (u32): winners bits 0-3 (0x8014637C {1,2,4,8}), losers bits 4-7 (0x80146384
 * {0x10..0x80}), flags from 0x100 up (0x100, 0x200, 0x400, 0x800, 0x1000-0x8000, 0x10000-0x680000, and 0x1000000 /
 * 0x2000000 from the menus: ovl_c func_8013843C). Slots from 4 on have their own two words (spec EXTRA_OVLE result6,
 * zeroed with ovl_e's BSS; cleared with the mask, func_80118A20): RES6, the winners, bit (slot - 4), and RES6L, the
 * losers, the same bits -- room for slots 4..35. (Until 2026-11-06 one word: winners bits 0-1, losers bits 4-5, which
 * had room for six slots only; the arcade's media.c reads the winners' word, which is where it was.) */
extern u8 X_result6[];
#define RES6  V32(X_result6)
#define RES6L V32(X_result6 + 4)
#define RES_WON(i)  ((i) < 4 ? (RESULT & (1u << (i))) != 0 : (RES6 & (1u << ((i) - 4))) != 0)
#define RES_LOST(i) ((i) < 4 ? (RESULT & (0x10u << (i))) != 0 : (RES6L & (1u << ((i) - 4))) != 0)
#define RES_SET_WIN(i)  do { if ((i) < 4) RESULT |= 1u << (i); else RES6 |= 1u << ((i) - 4); } while (0)
#define RES_SET_LOSE(i) do { if ((i) < 4) RESULT |= 0x10u << (i); else RES6L |= 1u << ((i) - 4); } while (0)
#define RESULT V32(0x800A10EC)
/* the hit-stat matrices (spec EXTRA_OVLE statmat): t 0..2, row a, column b (models), SIX_NSLOTS x SIX_NSLOTS each */
extern u8 X_statmat[];
#define STAT(t, a, b) (*(volatile u16 *)(X_statmat + ((t) * SIX_NSLOTS * SIX_NSLOTS + (a) * SIX_NSLOTS + (b)) * 2))
/* the HUD with more than four wrestlers (rw/r_hud.c): the panels at the four corners and the top and bottom centre;
 * the entrant count and the clock side by side in the strip above the top row (x, y in the HUD's 480 x 240) */
#define RW_HUD6_TOP_Y 0.0f
#define RW_COUNT6_X 203          /* the units digit (the stock 243: 215..259 -> 175..219) */
#define RW_CLOCK6_DX 29          /* the stock 204..276 -> 233..305: the pair centred on 240 */
#endif
