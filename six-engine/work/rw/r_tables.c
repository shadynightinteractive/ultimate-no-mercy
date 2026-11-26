/* Per-slot data tables of ovl_e that hold 4 entries, as 6 (build_rom.SLOT_TABLES moves the code's references here;
 * entries 0..3 are the stock ones). */
#include "six.h"
/* func_80115000, match start: match flags (0x80097EC0) & tab[slot] -> the wrestler's +0xE9 bits.
 * bits 0..3: the slot is the computer's (+0xE9 |= 8) -- slots 4 and 5 always are (no controller);
 * bits 4..7: (+0xE9 |= 0x20), bits 8..11: starts outside / waiting (+0xE9 |= 4) -- never for 4 and 5 */
/* (slots 4 on all alike: SIX_NSLOTS entries, 2026-11-06) */
const u32 rw_tab_6440[SIX_NSLOTS] = { 1, 2, 4, 8, [4 ... SIX_NSLOTS - 1] = 0xFFFFFFFF };
const u32 rw_tab_636C[SIX_NSLOTS] = { 0x10, 0x20, 0x40, 0x80 };
const u32 rw_tab_6450[SIX_NSLOTS] = { 0x100, 0x200, 0x400, 0x800 };
/* func_80115000: a wrestler's first target (+0x08) in modes 0xC000, [mode 0x4000 ? 8 : 0 + slot]
 * (build_rom.SEQS): mode 0x8000 (three-way) the stock 0x80146470 {1, 2, 0, 0}; Royal Rumble slot ^ 3 for 0..3,
 * the pair 4 <-> 5 */
/* (2026-11-06: halves of RW_OPP_HALF, a power of two the slots fit in -- 8 for up to eight, the six-man table as it
 *  was; build_rom's shift of 0x4000 makes the second half's offset -- and in the Rumble's half, slots 4 on in pairs,
 *  4 <-> 5, 6 <-> 7, ...) */
#define RW_OPP_HALF (SIX_NSLOTS <= 8 ? 8 : SIX_NSLOTS <= 16 ? 16 : 32)
const s8 rw_opp[2 * RW_OPP_HALF] = { 1, 2, 0, [RW_OPP_HALF] = 3, 2, 1, 0,
#define P(k) [RW_OPP_HALF + 2 * (k)] = 2 * (k) + 1, [RW_OPP_HALF + 2 * (k) + 1] = 2 * (k)
    P(2), P(3),
#if RW_OPP_HALF > 8
    P(4), P(5), P(6), P(7),
#endif
#if RW_OPP_HALF > 16
    P(8), P(9), P(10), P(11), P(12), P(13), P(14), P(15),
#endif
#undef P
};
/* func_80115E54, start positions (x, z) in modes 0xC000 by model: the four corners, and the middles of the two
 * sides between them (z = +-180) for 4 and 5. Then, for bigger matches (2026-11-06): the middles of the other two
 * sides, the points halfway in, the rest of the 90-step grid, the middle, and spots between -- 32 in all, the first six
 * as they were. (The arcade's NM_RR_SPOTS reads it by slot too.) */
const s16 rw_tab_6558[64] = { -180, 180,  -180, -180,  180, 180,  180, -180,  0, 180,  0, -180,
    -180, 0,  180, 0,
    -90, 90,  90, -90,  90, 90,  -90, -90,
    -90, 180,  90, -180,  90, 180,  -90, -180,
    -180, 90,  180, -90,  180, 90,  -180, -90,
    0, 0,  0, 90,  0, -90,  90, 0,  -90, 0,
    -45, 135,  45, -135,  135, 45,  -135, -45,  45, 135,  -45, -135,  135, -45 };
/* (slot + 1) << 8: the slot field (0x700) of the camera's event word 0x801594D2 (func_80114514, func_8011D848) */
#if SIX_NSLOTS == 6
const u32 rw_tab_6350[6] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600 };
#else
/* (every slot's, 2026-11-07: patch_replay_bits.py) */
const u32 rw_tab_6350[32] = {
    0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x800,
    0x900, 0xA00, 0xB00, 0xC00, 0xD00, 0xE00, 0xF00, 0x1000,
    0x1100, 0x1200, 0x1300, 0x1400, 0x1500, 0x1600, 0x1700, 0x1800,
    0x1900, 0x1A00, 0x1B00, 0x1C00, 0x1D00, 0x1E00, 0x1F00, 0x2000 };
#endif
/* the instant replay (0x80162A7C bits; rw/r_8013092C.c): a slot's move is replay-worthy (0x8014DB60), the replay's
 * camera word slot field (0x8014DB70: (slot + 1) << 8, u16), snap the slot's animation at the first playback
 * frame (0x8014DB78). Slots 4, 5: bits 21..24, which nothing else in the word uses. */
#if SIX_NSLOTS == 6
const u32 rw_tab_DB60[6] = { 0x4000, 0x8000, 0x10000, 0x20000, 0x200000, 0x400000 };
const u16 rw_tab_DB70[6] = { 0x100, 0x200, 0x300, 0x400, 0x500, 0x600 };
const u32 rw_tab_DB78[6] = { 0x10, 0x20, 0x40, 0x80, 0x800000, 0x1000000 };
#else
/* (every slot's, 2026-11-07: patch_replay_bits.py -- 6 on share 0x10000000, which of them in rw_rp_worthy; no snap) */
const u32 rw_tab_DB60[32] = { 0x4000, 0x8000, 0x10000, 0x20000, 0x200000, 0x400000, [6 ... 31] = 0x10000000 };
const u16 rw_tab_DB70[32] = {
    0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700, 0x800,
    0x900, 0xA00, 0xB00, 0xC00, 0xD00, 0xE00, 0xF00, 0x1000,
    0x1100, 0x1200, 0x1300, 0x1400, 0x1500, 0x1600, 0x1700, 0x1800,
    0x1900, 0x1A00, 0x1B00, 0x1C00, 0x1D00, 0x1E00, 0x1F00, 0x2000 };
const u32 rw_tab_DB78[32] = { 0x10, 0x20, 0x40, 0x80, 0x800000, 0x1000000 };
#endif
/* 0x8016B650's lock (func_8011D848: one wrestler at a time runs its sequence), by model: 1 << model for 0..3,
 * then 0x1000/0x2000 (bits 4..11 are the tag swaps and flags) */
/* (2026-11-06: wrestlers 4 on all 0x1000, "one of 4 on holds it" -- which of them is the engine's wide word,
 *  rw_seq_lock: rw/stubs.S rw_seq_take / rw_seq_release) */
/* func_800EBD2C's contact list, off func_8012C474's stack (patch_contacts.py): max(n - 1, 10) entries of 0x28 and
 * the owner's after them (8-aligned: it holds floats) */
#define RW_CONTACT_CAP (SIX_NSLOTS - 1 > 10 ? SIX_NSLOTS - 1 : 10)
u8 rw_contacts[(RW_CONTACT_CAP + 1) * 0x28] __attribute__((aligned(8)));

const u16 rw_tab_65C8[SIX_NSLOTS] = { 1, 2, 4, 8, [4 ... SIX_NSLOTS - 1] = 0x1000 };
