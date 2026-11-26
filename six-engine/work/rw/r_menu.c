/* The menus' side of a six-man match: who stands in slots 4 and 5, and the count the match overlay runs with
 * (rw_match_count, read once by rw_run_overlay when ovl_e is entered). */
#include "six.h"
extern s32 rw_match_count;
extern u8 A_800A4038[], A_800A8958[], A_800A6028[], A_8009ED10[];
/* wrestlers in the ring at once in a Royal Rumble: 6, or the stock 4 */
u8 rw_rumble_ring = 6;
#define ROSTER(i)        V16(0x800BD4D0 + (i) * 2)    /* the Rumble's entrants in order (wrestler ids) */
#define ROSTER_COST(i)   V8(0x800A9358 + (i))         /* their costumes */
#define ROSTER_LEVEL(i)  V8(0x800A98E0 + (i))         /* their computer level */
#define ENTRANTS         (*(volatile s16 *)0x800A958C)
#define MENU_FLAGS       V32(0x800A957C)            /* the match type's set-up bits (0x200 Royal Rumble) */
#define MENU_CONTEXT     V8(0x800A9578)             /* 1 Multi Play, 7 one pick then the match (the arcade's), 2 and 8
                                                       the Championship modes' (their title and result screens are
                                                       the stock four slots') */
/* func_8013486C (ovl_c), the Rumble's setup, its `jr ra` becomes `j rw_rumble_fill`: it has put the menu's four in slots 0..3
 * and entrants 0..3 and drawn the rest of the roster. With six in the ring and at least six entrants, entrants 4
 * and 5 take slots 4 and 5 at the start -- as func_801199F0 brings an entrant into a slot in the match: the id,
 * the costume, the computer's (pad 0) at the entrant's level. Only a Multi Play / arcade Royal Rumble: func_8013486C
 * also sets up the Championship mode's 40-man Rumble (func_8013AA48). */
void rw_rumble_fill(void)
{
    /* (in the ring: rw_rumble_ring, as far as the entrants and the engine's slots go; fewer than six is the stock
     *  four -- as it was when this was six or four, 2026-11-06) */
    s32 n = rw_rumble_ring, i;
    if (n > ENTRANTS) n = ENTRANTS;
    if (n > SIX_NSLOTS) n = SIX_NSLOTS;
    if (n < 6 || !(MENU_FLAGS & 0x200) || (MENU_FLAGS & 0x50000) || !(MENU_CONTEXT == 1 || MENU_CONTEXT == 7)) n = 4;
    for (i = 4; i < n; i++) {
        *(volatile s32 *)(A_800A4038 + i * 4) = ROSTER(i);
        *(volatile s32 *)(A_800A8958 + i * 4) = ROSTER_COST(i);
        *(volatile s32 *)(A_800A6028 + i * 4) = 0;
        *(volatile u16 *)(A_8009ED10 + i * 2) = ROSTER_LEVEL(i);
    }
    rw_match_count = n;
}
