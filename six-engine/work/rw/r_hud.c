/* The HUD's per-corner tables (ovl_e data 0x80144B78..; index = the panel's corner, the HUD entry's byte 0): the
 * originals hold 4 corners -- 0 top left, 1 bottom left, 2 top right, 3 bottom right; these 6, the two more at the
 * centre of the top and bottom rows: 4 top, 5 bottom (laid out as a left-hand panel, moved right by 144 to the
 * middle of the 480-wide HUD: 192..288; y as the row's). The entrant count and the clock that the original has
 * there move up above the top row (rw/r_800E0D20.c, rw/r_800DFED0.c). The code indexes the tables with `& 7`
 * instead of `& 3` (build_rom.HUD_TABLES). */
#include "six.h"
#define MID 144
#define X4(a, b) a, a, b, b, (a) + MID, (a) + MID      /* positions */
#define XL(a, b) a, a, b, b, a, a                      /* a left panel's own values (offset, mirror) */
#define Y4(t, b) t, b, t, b, t, b
const s16 rw_hud_B78[6] = { X4(142, 276) };
const s16 rw_hud_B80[6] = { X4(163, 311) };
const s16 rw_hud_BD0[6] = { X4(48, 335) };
const s16 rw_hud_BD8[6] = { Y4(20, 203) };
const s16 rw_hud_BE0[6] = { X4(45, 332) };
const s16 rw_hud_BE8[6] = { Y4(35, 194) };
const s16 rw_hud_BF0[6] = { X4(106, 373) };
const s16 rw_hud_BF8[6] = { XL(29, -26) };
const u32 rw_hud_C00[6] = { XL(0, 0x3F800000) };
const s16 rw_hud_C10[6] = { Y4(28, 211) };
const s16 rw_hud_B90[6] = { Y4(34, 185) };   /* (func_800E0A0C: by panel = slot, in the corners' order) */
