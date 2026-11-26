// Movesets as lists of slots, put straight into what the game unpacks (2026-11-13, movesets).
//
// Why: the game keeps a moveset packed (0xD8 bytes: 0xB8 of moves, 0x20 of abilities). Each slot of the packing can
// only say "list L, cell i" for one of its own 4 lists of moves (0x80045500), so a move none of those lists holds
// can't be said at all. 53 of the 157 slots have no room in their lists, and importing Revenge's other 57 wrestlers
// put the reference wrestler's move in 162 of their slots without a word. The game never plays a packed moveset,
// though: it unpacks it first, into tables of plain 16-bit moves -- in the menus (func_80005EFC -> func_800075DC, 292
// entries of 6 bytes) and for the match (ovl_e func_8013CBAC, 548 entries a wrestler slot). A wrestler whose moveset
// comes as a list of slots (his folder's moves.json) has those tables made from his list here, the same way the game
// makes them from the packed bytes, so any move can be in any slot and nothing is shared between wrestlers.
// roster.c says which movesets have a list (roster_moves_pre / roster_match_moves_pre); everyone else is untouched:
// the game's own code unpacks his packed bytes, as before.
//
// A slot list: one entry for each of the 292 entries the menus' unpack makes (gen/mkroster.py unpack's "slots", the
// handlers' moves.json "slots"), 8 bytes each (all numbers high byte first):
//   +0 u16  the move as the game's tables hold it: a No Mercy move number (its record, its animations);
//   +2 u8   its three flags (bit 0, 1, 2: the unpack's bytes 2, 3, 4 -- bit 2 marks the special); bit 7 (ML_KEEP): his
//           moves.json named a move the exe doesn't know, so this entry keeps what his packed moveset gives (the exe's
//           log names each one -- nothing is put in its place quietly);
//   +3 u8   0;
//   +4 u32  the move's ID (2026-11-13, the user: moves and animations of every AKI game may be built in, a library per
//           source game, and folders may add their own; no fixed caps): 0 for one of No Mercy's own moves, else the exe's
//           number for a move of a library or of his folder ("revenge:58", "own:sharpshooter"), whose animations and
//           record the move rows will take from it (the next step, LIMITS.md #3: movex.c's rows from data). +0 is then the
//           No Mercy move the entry is built on. Nothing here reads it yet.
// The entries the game's layout names are the moveset as
// the game knows it (157 of the 292; 159 in layout kind 1). The others the game fills itself in the match -- 13 every
// wrestler has, 95 made from another entry's move, 27 left 0 (scratch\agents\movesets\reconcile.py) -- and the menus
// leave them 0; a list may still give one a move of its own, for the match (mu_match_build). 0 there: the game's own.
#ifndef MOVESET_UNPACK_H
#define MOVESET_UNPACK_H

#define ML_ENTRIES 292
#define ML_STRIDE  8
#define ML_BYTES   (ML_ENTRIES * ML_STRIDE)
#define ML_KEEP    0x80u
static u32 ml_move(const u8* l, s32 e) { return (u32)l[ML_STRIDE * e] << 8 | l[ML_STRIDE * e + 1]; }
static u32 ml_flags(const u8* l, s32 e) { return l[ML_STRIDE * e + 2]; }
static int ml_given(const u8* l, s32 e) { return !(l[ML_STRIDE * e + 2] & ML_KEEP); }

// The game's tables (the same in the stock and six-man engines: resident data, and ovl_e's data).
#define MU_LAYOUTS  0x800442DCu   // -> each layout kind's slots (0, 1): { s16 entry, s16 lists[4] } ..., entry -1 ends
#define MU_LISTS    0x80045500u   // list number -> its moves (u16 each)
#define MU_RECIX    0x80162A90u   // s16 a move: its record's number (file 0x1F1)
#define MU_RECBUF   0x8016B344u   // -> the move records, 36 bytes each (file 0x1EF), while the match is set up
#define MU_FIXED    0x8014DCE0u   // ovl_e: { s16 entry, s16 list, s16 cell }: entries every wrestler has; entry -1 ends
#define MU_DERIVED  0x8014DD34u   // ovl_e: { s16 entry, s16 to, s16 step }: entries made from another entry's move
#define MU_BITS16   0x8014DE2Cu   // ovl_e: { s16 entry, s16 0, u32 bit }: the bit in the ability bits' +0x3C if he has it
#define MU_BITS32   0x8014DEB4u   // ovl_e: the same for +0x38
// The match's tables, per wrestler slot (six-man engine addresses; movex.c's MX_*): 548 entries, the moveset's 292
// and then the continuations. A move (u16), its record's number (u16), the continuation each entry goes on to (u8),
// and a copy of the record (36 bytes: +0x20 its flags).
#define MU_E        548
#define MU_BASE     292
#define MU_MOVE(s, e)  (SIXA_8008FA70 + (u32)(s) * (MU_E * 2u) + 2u * (u32)(e))
#define MU_RECIX_(s, e) (SIXA_8009FF90 + (u32)(s) * (MU_E * 2u) + 2u * (u32)(e))
#define MU_NEXT(s, e)  (SIXA_800A5720 + (u32)(s) * MU_E + (u32)(e))
#define MU_REC(s, e)   (SIXA_800AA090 + (u32)(s) * (MU_E * 36u) + 36u * (u32)(e))

// ---- the menus' unpack ---------------------------------------------------------------------------------------------
// func_800075DC(moves, slots, abilities out) has made `slots` from the packed bytes: each entry its layout names gets
// the move and flags of the list instead. The layout is the one the game used: by the unpacked ability 0.
static void mu_menu_put(const u8* l, u32 slots, u32 abil_out) {
    u32 p = RD32(MU_LAYOUTS + 4u * (u32)(s32)RDS16(abil_out));
    if (!GAME_PTR(p)) return;
    for (; RDS16(p) != -1; p += 10u) {
        const s32 e = RDS16(p);
        if (e < 0 || e >= ML_ENTRIES || !ml_given(l, e)) continue;   // (not given: the game's, from his packed bytes)
        const u32 at = slots + 6u * (u32)e, f = ml_flags(l, e);
        RD16(at) = (u16)ml_move(l, e);
        RD8(at + 2u) = (u8)(f & 1u);
        RD8(at + 3u) = (u8)(f >> 1 & 1u);
        RD8(at + 4u) = (u8)(f >> 2 & 1u);
    }
}

// ---- the match's unpack --------------------------------------------------------------------------------------------
// Entry e of slot s: the move, its record's number and its record, as the game sets one. The move is read as a signed
// 16-bit number (the game's lh), and so is the record number, so the tables are indexed exactly as the game does.
static void mu_entry(s32 s, s32 e, s32 move, u32 recs) {
    RD16(MU_MOVE(s, e)) = (u16)move;
    const s32 r = RDS16(MU_RECIX + 2u * (u32)move);
    RD16(MU_RECIX_(s, e)) = (u16)r;
    const u32 from = recs + 36u * (u32)r, to = MU_REC(s, e);
    for (u32 b = 0; b < 36u; b += 4u) RD32(to + b) = RD32(from + b);
}
// Whether the layout at p names entry e.
static int mu_in_layout(u32 p, s32 e) {
    for (; RDS16(p) != -1; p += 10u) if (RDS16(p) == e) return 1;
    return 0;
}
// The match's tables for slot s from the list, all of what ovl_e func_8013CBAC(slot, moves, bits) does, step for step,
// with each layout entry's move taken from the list where the game reads it from the packed bytes. `bits` is the
// wrestler's ability bits (+0x34 bit 2 picks layout kind 1; +0x38 and +0x3C get bits OR'ed in): the caller gives them
// as they were before the game's unpack ran.
static void mu_match_build(s32 s, const u8* l, u32 bits) {
    const u32 recs = RD32(MU_RECBUF);
    // (an entry the list doesn't give keeps the move the game's unpack of his packed bytes just put there, and its
    //  special mark: the record's 0x8000 then is the record's own | the packed special, so OR'ing it back is exact)
    static u16 kept[ML_ENTRIES];   // (static, not on the stack: the game thread's stack is small)
    static u8 special[ML_ENTRIES];
    for (s32 e = 0; e < ML_ENTRIES; e++) {
        const int given = ml_given(l, e);
        kept[e] = given ? (u16)ml_move(l, e) : RD16(MU_MOVE(s, e));
        special[e] = given ? (u8)(ml_flags(l, e) & 4u) : (u8)((RD32(MU_REC(s, e) + 0x20u) & 0x8000u) ? 4u : 0u);
    }
    // Every entry cleared as the game clears it: the move, record number and continuation, and the record's bytes
    // 0-0x1A and its flags word (bytes 0x1B-0x1F are left as they were, as the game leaves them).
    for (s32 e = 0; e < MU_E; e++) {
        const u32 r = MU_REC(s, e);
        for (u32 b = 0; b < 0x1Bu; b++) RD8(r + b) = 0;
        RD32(r + 0x20u) = 0;
        RD8(MU_NEXT(s, e)) = 0;
        RD16(MU_MOVE(s, e)) = 0;
        RD16(MU_RECIX_(s, e)) = 0;
    }
    // The layout's entries, from the list (the special flag: bit 2 -> the record's 0x8000).
    const u32 p0 = RD32(MU_LAYOUTS + ((RD32(bits + 0x34u) & 2u) ? 4u : 0u));
    for (u32 p = p0; RDS16(p) != -1; p += 10u) {
        const s32 e = RDS16(p);
        mu_entry(s, e, (s16)kept[e], recs);
        if (special[e]) RD32(MU_REC(s, e) + 0x20u) |= 0x8000u;
    }
    // The entries every wrestler has, from the game's lists.
    for (u32 t = MU_FIXED; RDS16(t) != -1; t += 6u) {
        const u32 list = RD32(MU_LISTS + 4u * (u32)(s32)RDS16(t + 2u));
        mu_entry(s, RDS16(t), RDS16(list + 2u * (u32)(s32)RDS16(t + 4u)), recs);
    }
    // Entries made from another entry's move: one entry `to`, its move + step; or (to <= 0) the entries after it, each
    // the one before + step, max(1, -to) of them.
    for (u32 t = MU_DERIVED; RDS16(t) != -1; t += 6u) {
        const s32 e = RDS16(t), to = RDS16(t + 2u), step = RDS16(t + 4u);
        s32 m = RDS16(MU_MOVE(s, e));
        if (to > 0) { mu_entry(s, to, m + step, recs); continue; }
        s32 k = to, at = e;
        do { m += step; mu_entry(s, ++at, m, recs); } while (++k < 0);
    }
    // Not the game's (the one step it hasn't): an entry the layout doesn't name -- one every wrestler has, or one made
    // from another's move -- given a move in the list (not 0) has that move instead. The game's packing can't say such
    // a thing, so a list made from a packed moveset (0 there, as the game's menus' unpack leaves them) changes nothing
    // here; a list can set, say, one counter of a strike on its own, as no packed moveset could.
    for (s32 e = 0; e < ML_ENTRIES; e++) {
        if (!ml_given(l, e) || !ml_move(l, e) || mu_in_layout(p0, e)) continue;
        mu_entry(s, e, (s16)ml_move(l, e), recs);
        if (ml_flags(l, e) & 4u) RD32(MU_REC(s, e) + 0x20u) |= 0x8000u;
    }
    // The continuations: each of the 292 whose record goes on (flags & 0x1E00) gets the next one, its move + 1.
    s32 n = 0;
    for (s32 e = 0; e < MU_BASE; e++) {
        if (!(RD32(MU_REC(s, e) + 0x20u) & 0x1E00u)) continue;
        RD8(MU_NEXT(s, e)) = (u8)n;
        mu_entry(s, MU_BASE + n, RDS16(MU_MOVE(s, e)) + 1, recs);
        if (++n >= 256) break;
    }
    // The ability bits his moves give.
    for (u32 t = MU_BITS16; RDS16(t) != -1; t += 8u)
        if (RDS16(MU_MOVE(s, RDS16(t))) != 0) RD16(bits + 0x3Cu) = (u16)(RD16(bits + 0x3Cu) | RD32(t + 4u));
    for (u32 t = MU_BITS32; RDS16(t) != -1; t += 8u)
        if (RDS16(MU_MOVE(s, RDS16(t))) != 0) RD32(bits + 0x38u) |= RD32(t + 4u);
    if (RD32(MU_REC(s, 235) + 0x20u) & 0x08u) RD32(bits + 0x38u) |= 1u;
    if (RD32(MU_REC(s, 265) + 0x20u) & 0x20u) RD32(bits + 0x38u) |= 4u;
    if (RD32(MU_REC(s, 266) + 0x20u) & 0x20u) RD32(bits + 0x38u) |= 8u;
}
#endif
