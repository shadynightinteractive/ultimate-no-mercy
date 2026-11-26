// Moves done as the Superstar did them in the game he comes from (WCW/nWo Revenge's Bret Hart: his front
// special is a slam that goes straight into the Sharpshooter). No Mercy has no such move, so one is made of
// parts: a move's record and animation in one of his moveset's entries are replaced, and the entries it
// goes on to (one after another) added -- per wrestler, while the match unpacks his moveset.
//
// How the game keeps a wrestler's moves (ovl_e func_8013CBAC, six-man engine addresses): per slot 548
// entries -- the moveset's 292 then continuations -- each a move (MX_MOVES), its record's number
// (MX_RECIX), a copy of the record (MX_RECS, 36 bytes; the word at +0x20 its flags). An entry whose record
// has a continuation (flags & 0x1E00) gets the next continuation entry, 292 + n (MX_NEXT[entry] = n), for
// its move + 1. In the match, a move that goes on (flags bit 0) goes to entry 292 + MX_NEXT[the entry it's
// in] (func_80129128, func_80137044 -> func_800EC698) -- for any entry, the continuations' too, so a move
// can go on more than once. The animations are the move's (func_80130878: the move's attacker's file from
// the file 0x1F0 table, the defender's the next); an entry here can have its own.
//
// Which entries play what (2026-11-13, movelib): a move of another game is a LIBRARY MOVE, named by an ID the same for
// whoever has it ("revenge:126c_1270": Revenge's animation pair, and the hold it goes on to). The built-in libraries are
// made from each source game's ROM by its handler (move_lib.h: Revenge's), and a wrestler folder can add moves of its own
// (moves\movex.json + anims\, "own:<name>": the exe reads them and gives their animations game file numbers while they're
// in a match). A wrestler's moveset says which entry plays which library move: a folder wrestler's slot list (his
// moves.json, roster.c / moveset_unpack.h: each entry's move ID), a built-in port's list in move_lib.h (port_moves). Before,
// the rows were found by the port's ext_late index (MoveX.late, movex_data.h), so only the built-in ports could have one,
// and a wrestler had room for 96 (MX_PER); now every entry of the 548 can have its animation (mx_file).
// (2026-11-13, movelib2) The library is the whole of Revenge's move list (every code with an animation pair in any of its
// Superstars' lists, and the holds they go on to), made from the user's ROM by the Revenge handler into a content file the
// exe reads (movelib_revenge.bin). Its animations get game file numbers only while a match plays them, from the pool the
// folder models' and the folders' own moves' come from -- not fixed numbers in the mod's pack (rev_move_anims.h: a copy per
// port, 378 pairs of which 125 differ) -- so this asks the exe for a library move as for a folder's own (lib_get).
#include "arcade.h"
#include "gamefn.h"

#ifdef NM_SIX
#define MX_MOVES  SIXA_8008FA70   // [slot][548] u16
#define MX_RECIX  SIXA_8009FF90   // [slot][548] u16
#define MX_NEXT   SIXA_800A5720   // [slot][548] u8
#define MX_RECS   SIXA_800AA090   // [slot][548][36]
#define MX_ON 1
#else
#define MX_ON 0                 // (the stock engine's tables aren't mapped here: nothing done)
#define MX_MOVES 0u
#define MX_RECIX 0u
#define MX_NEXT 0u
#define MX_RECS 0u
#endif
#define MX_E 548
#define MX_BASE 292
#define NM_ANIMS  0x8015A700u   // s16 per move: its attacker's animation file - 0x3322 (file 0x1F0)
#define NM_RECIX  0x80162A90u   // s16 per move: its record's number (file 0x1F1)
#define NM_RECBUF 0x8016B344u   // -> the records (file 0x1EF), while the match is set up
#define MOVES16(s, e) (MX_MOVES + (u32)(s) * (MX_E * 2u) + 2u * (u32)(e))
#define RECIX16(s, e) (MX_RECIX + (u32)(s) * (MX_E * 2u) + 2u * (u32)(e))
#define NEXT8(s, e)   (MX_NEXT + (u32)(s) * MX_E + (u32)(e))
#define REC(s, e)     (MX_RECS + (u32)(s) * (MX_E * 36u) + 36u * (u32)(e))

// A library move (move_lib.h's, one per ID; a folder's own the exe gives the same way, movex_post): the attacker's animation
// file (the defender's the next; 0: the move's own), the hold it goes on to -- its loop's attacker file, the endings at
// + 2 / + 4 / + 6 (0: none; played only on a move that goes on) -- and `ground`, the ground grapple the move goes on to when
// it ends with the man down: its attacker into the game's own ground grapple on him, and its pick there this entry (0:
// none; below: Bret's front special). The No Mercy move it is played on is the entry's (its record: damage, timing).
// (2026-11-20, revmoves) `n`: how many animations the move has from `file` on, one after another (0: 2, its pair). A No
// Mercy move can ask for more than its pair -- a turnbuckle dive five (its flight, landing, miss ...: file + 0 .. 4), a
// double team three (the lead's, the partner's and the man's: + 0 .. 2) -- and a library move that brings fewer gives
// those of the No Mercy move it is played on (movex_anim).
typedef struct { u16 file; u16 then_file; u16 ground; u16 n; } LibMove;
// A built-in port's entry that plays a library move: the entry, the library move's number (its ID's: 1 = move_lib[0]), and
// the move in the entry's place (0: his moveset's; else a move that ends, with its record -- a port's move whose Revenge
// animation ends where the slot's goes on).
typedef struct { u16 entry; u16 id; u16 move; } PortMove;
// Bret Hart's front special (entry 70), made as Revenge makes it (2026-11-01 afternoon): two moves. Revenge's slam
// (code 58) is No Mercy's 1880, "Sidewalk Slam / Submission", animation and all (14296 / 14297 byte for byte); its
// Revenge record names the move after it, code 200 -- his ground Sharpshooter (No Mercy's 16770 -> 16771, his entry
// 157), begun fresh, the two placed as for a ground move. So: entry 70 is 1880 as the game has it (its own files and
// record: 0x2000, the slam's damage at its end | 0x8000, the special as the game marks it), a move that ends -- the
// man down, lying face up (14297's end pose 20). Then the game's own ground grapple on him (movex_chain_step below:
// func_8013E870, the A press at a lying man -- state 70: the game puts him at the man's head or legs, func_80136240,
// and picks the move there, func_80136D90: at the legs of a face-up man 240, with SPECIAL! (+0xEE & 4, his moveset's
// 0x4000) 157). Its pick at the legs is 157 (the special's own Sharpshooter, as Revenge's 58 -> 200, SPECIAL! or not
// by then); at the head or face down the game's own. From there the game's Sharpshooter: 157 (0xA581: & 0x1E00) on
// to the hold 16771 (16352 ..), its endings + 2 / 4 / 6 (func_80137044 phase 1); each file's record its own (the
// turnover's sounds at 26 / 46 / 94 / 104).
// 1880's record has the game's own way on, too: +4 (func_8012F660 -> func_800ECEEC: no continuation and +4 set ->
// that entry, both men's files at once, states 95 / 96) = 240, the ground grapple at the legs -- played where the
// slam leaves them, not placed (a move after a move goes on in the same frame: NOTES 2026-11-01, the ports' holds --
// 2680's last key is 2681's first). Here 0 in his slot's copy: the ground grapple's placing instead. (His 72, also
// 1880, keeps the game's.)
// Sting's front special the same (ports4, 2026-11-01): Revenge's code 58 is Bret's byte for byte, record and all.
// Both are now the library move "revenge:slam_sharpshooter" (move_lib.h, made by hand there: { 0, 0, 157 }), on their
// entry 70 in their lists -- so a folder wrestler can have it too.
// (Before: the slam and the turnover were one joined file, gen/revanims.py NM_JOIN -- the turnover from the slam's
// places, the man's root jumping ~190 at the seam, all of it with the slam's record: the turnover silent -- then
// 16771 as 1880's continuation, flags 0x82581 as the Rolling Leg Lock's 950.)
#include "move_lib.h"   // (the Revenge handler's, movelib2's revlib.py: the library's count and stamp, its moves made by hand,
                        //  the built-in ports' lists)
#define LATE_HOGAN 13
// Hollywood Hogan's leg drop, his finisher (the user, 2026-10-30 / 31: "as in Revenge it is with B ... the opponent gets
// hit and stays laying"; data\legdrop\LEGDROP.md): an entry of his own past the game's and movex's continuations --
// No Mercy's Leg Drop 7980 (its animation his Revenge drop byte for byte) with a finisher's record: byte 6 (the blow's
// base damage) 40 as the Stunner's / Tombstone's (7980's 3), 7 0, 8 -15, 0x12 / 0x13 5 / 2 (head / body), flagged
// special. logic.c qa_move_entry takes it in place of his ground strike (209 / 210) at a man's head with SPECIAL up.
// (Still the built-in port's own, by his ext_late index: an entry past the moveset's, which a slot list can't name yet.)
#define LEGDROP_MOVE 7980
static s16 legdrop_entry[NM_SLOTS_CAP];             // 0: none
s32 movex_legdrop_entry(s32 s) { return (s >= 0 && s < NM_SLOTS_CAP) ? legdrop_entry[s] : 0; }

// Each wrestler slot's entries' own animations: the attacker's file (the defender's the next), 0 the move's own -- for any
// of the 548 entries, so a moveset can have as many as it has entries (movex_anim). The first one put on an entry stays.
static u16 mx_file[NM_SLOTS_CAP][MX_E];
static u8 mx_n[NM_SLOTS_CAP][MX_E];     // (2026-11-20, revmoves: how many animations from mx_file on are the entry's own)
static u8 mx_any[NM_SLOTS_CAP];         // (any at all: movex_anim's quick way out)
static s32 mx_slot = -1, mx_set = -1;
u32 dbg_movex[4];   // tests: slot, ext_late index (-2: a folder's list), library moves put in, last continuation number
static void put_file(s32 s, s32 e, u16 f, u32 n) {
    if (!f || e < 0 || e >= MX_E || mx_file[s][e]) return;
    mx_file[s][e] = f; mx_n[s][e] = (u8)(n < 2u ? 2u : n > 255u ? 255u : n); mx_any[s] = 1;
}
// QA (logic.c qa_move_entry, qa_fcycle): ext_late[late]'s k-th move made over with an animation of its own (each animation
// once), in his list's order, round again past the last; -1: none
// (a grapple's moves only, entries below 140: the reversals (148, 168) and the slide (265, 266) can't be forced from a
// grapple -- their files the same for every ported Superstar)
s32 movex_qa_entry(s32 late, s32 k) {
    s32 e[32], n = 0;
    if (late < 0 || late >= PORT_LATE_N) return -1;
    for (int i = port_moves_at[late]; i < port_moves_at[late + 1] && n < 32; i++) {
        const u16 id = port_moves[i].id;     // (2026-11-13, movelib2: by the library move -- one pair each -- as by its file before)
        if (id <= MOVELIB_HAND_N || port_moves[i].entry >= 140) continue;
        int seen = 0;
        for (int j = port_moves_at[late]; j < i && !seen; j++) seen = port_moves[j].id == id;
        if (!seen) e[n++] = port_moves[i].entry;
    }
    return n ? e[(k < 0 ? 0 : k) % n] : -1;
}

// QA (logic.c qa_move_entry, qa_fsub): slot s's holds -- the entries whose move, or a move it goes on to, is a
// submission (its record's 0x0400: Bret's special, a slam going on to the Sharpshooter, counts) -- into out[], how many
// (and a move that goes on into a ground grapple, a library move's `ground`: Bret's special -> 157)
static u32 rec_flags(s32 s, s32 e);
static s16 chain_from[NM_SLOTS_CAP], chain_to[NM_SLOTS_CAP];   // (a library move's `ground`, per slot: below)
s32 movex_qa_subs(s32 s, s16* out, s32 max, int specials) {   // (specials: only the moveset's specials, 0x8000)
    s32 n = 0;
    if (!MX_ON || s < 0 || s >= NM_SLOTS_CAP) return 0;
    for (s32 e = 0; e < MX_BASE && n < max; e++) {
        if (RD16(MOVES16(s, e)) == 0 || RD16(MOVES16(s, e)) == 0xFFFFu) continue;
        s32 c = e, hold = 0;
        for (int k = 0; k < 4 && !hold; k++) {
            u32 f = rec_flags(s, c);
            hold = (f & 0x0400u) != 0;
            if (!(f & 0x1E00u) && c == chain_from[s] && chain_to[s]) { c = chain_to[s]; continue; }
            if (!(f & 0x1E00u)) break;
            c = MX_BASE + RD8(NEXT8(s, c));
        }
        if (hold && (!specials || (rec_flags(s, e) & 0x8000u))) out[n++] = (s16)e;
    }
    return n;
}
// ... (qa_fsub 3) slot s's moves played with animations of their own (entries below 292), in the moveset's order
s32 movex_qa_own(s32 s, s16* out, s32 max) {
    s32 n = 0;
    if (s < 0 || s >= NM_SLOTS_CAP || !mx_any[s]) return 0;
    for (s32 e = 0; e < MX_BASE && n < max; e++) if (mx_file[s][e]) out[n++] = (s16)e;
    return n;
}
// ... and whether entries a and b are picked in the same place (standing, behind, on the mat ...): the game's
// moveset layout (0x800442DC -> { s16 entry, s16 lists[4] } ..., -1: gen/mkroster.py slot_lists) gives an entry
// the lists of moves it may hold; the same place, a list in common
#define NM_MOVE_LAYOUT 0x800442DCu
static int layout_lists(s32 e, s16* l) {
    u32 p = RD32(NM_MOVE_LAYOUT);
    if (!GAME_PTR(p)) return 0;
    for (int i = 0; i < 400; i++, p += 10) {
        s16 s = RDS16(p);
        if (s == -1) break;
        if (s != e) continue;
        for (int k = 0; k < 4; k++) l[k] = RDS16(p + 2u + 2u * (u32)k);
        return 1;
    }
    return 0;
}
// (QA, 2026-11-13, movesteps) An entry the game makes from another entry's move -- ovl_e's derived table (moveset_unpack.h
// MU_DERIVED: { s16 entry, s16 to, s16 step }, a grapple's variants entry + 1 .. entry + 4) -- isn't in the layout: it is
// picked where its base entry is, so qa_fsub can force a move's later steps too (a folder's library move's variants).
#define MX_DERIVED 0x8014DD34u
static s32 qa_base_of(s32 e) {
    for (u32 t = MX_DERIVED; RDS16(t) != -1; t += 6u) {
        const s32 b = RDS16(t), to = RDS16(t + 2u);
        if (to > 0 ? e == to : (e > b && e <= b + (to < -1 ? -to : 1))) return b;
    }
    return e;
}
int movex_qa_same_place(s32 a, s32 b) {
    s16 la[4], lb[4];
    a = qa_base_of(a); b = qa_base_of(b);
    if (!layout_lists(a, la) || !layout_lists(b, lb)) return 0;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) if (la[i] && la[i] == lb[j]) return 1;   // (list 0: the empty one, 155 entries have it)
    return 0;
}

// the match unpacks slot's moveset: ext_late[late]'s (-1: another's)
void movex_pre(s32 slot, s32 late) {
    mx_slot = slot; mx_set = late;
    if (slot < 0 || slot >= NM_SLOTS_CAP) return;
    legdrop_entry[slot] = 0; chain_from[slot] = chain_to[slot] = 0;
    for (s32 e = 0; e < MX_E; e++) { mx_file[slot][e] = 0; mx_n[slot][e] = 0; }
    mx_any[slot] = 0;
}

static u32 rec_flags(s32 s, s32 e) { return RD32(REC(s, e) + 0x20u); }

// Library move x on entry e of slot s, as the move rows always were (one row: its move in the entry's place, its ground
// grapple, its animation, then its hold's).
static void lib_put(s32 s, s32 e, const LibMove* x, u32 move, u32 buf) {
    if (move) {   // (the move in its place, with its record; the entry's special mark kept, as the game marks it)
        u32 special = rec_flags(s, e) & 0x8000u;
        s32 r = RDS16(NM_RECIX + 2u * move);
        RD16(MOVES16(s, e)) = (u16)move;
        RD16(RECIX16(s, e)) = (u16)r;
        for (u32 b = 0; b < 36; b += 4) RD32(REC(s, e) + b) = RD32(buf + 36u * (u32)r + b);
        RD32(REC(s, e) + 0x20u) |= special;
    }
    // (into a ground grapple when it ends: a move that ends -- no continuation, and record +4, the game's own way on
    //  in place, 0 -- and the slot's chain, movex_chain_step)
    if (x->ground && !(rec_flags(s, e) & 0x1E00u)) {
        RD16(REC(s, e) + 4u) = 0;
        chain_from[s] = (s16)e; chain_to[s] = (s16)x->ground;
    }
    // Its own animation (the ports' moves with their Revenge animations, gen/revanims.py). A move that goes on (record &
    // 0x1E00: a hold's grab -> the hold, a suplex -> its pin) goes to the game's continuation entry, 292 + MX_NEXT[entry],
    // whose animation is played over and over and ended with its file + 2 / + 4 / + 6 (ovl_e func_80137044 /
    // func_80137364): that entry plays the library move's hold (then_file: the loop's pair, then the three endings' --
    // not the stand-in's No Mercy hold, the Giant's sleeper turned neck wrench, the user 2026-11-01). Where Revenge's move
    // ends and the slot's goes on, the entry's move is one that ends (`move` above; the game's continuation entry left
    // unused).
    put_file(s, e, x->file, x->n);
    if (x->then_file && (rec_flags(s, e) & 0x1E00u)) put_file(s, MX_BASE + RD8(NEXT8(s, e)), x->then_file, 8u);   // (the hold's block: 8)
    dbg_movex[2]++;
}

// A move by its ID's number: a library move (made by hand: move_hand, move_lib.h; else the exe's, from the library's content
// file) or a folder's own (past the built-in libraries': his moves\movex.json), its animations' game file numbers given by the
// exe for this match (exe arcade_wrestlers.cpp); none if it can't (then the log says why, and the entry plays its own No
// Mercy move).
extern int plat_move_own(u32 id, u32 out);
extern u32 plat_move_lib_n(void);
extern u32 plat_move_lib_stamp(void);
extern void plat_moves_note(u32 what, u32 id, u32 a, u32 b);
extern void plat_movex_where(u32 files, u32 slots_entries, u32 players, u32 players_stride);
static u16 own_row[4] __attribute__((aligned(8)));
static int lib_checked;   // 1: the exe's libraries are this mod's (the same numbers); -1: they aren't -- no library moves
u32 movex_lib_n(void) { return MOVELIB_N; }
static u32 jk_file;       // (QA: the number Nash's jackknife's man's side has this match: anim_recpart)
// Whether the exe's library is this mod's (the same count and stamp: move_lib.h was made from the same library); said once.
static int lib_ok(void) {
    if (!lib_checked) {
        const u32 n = plat_move_lib_n(), st = plat_move_lib_stamp();
        lib_checked = (n == MOVELIB_N && st == MOVELIB_STAMP) ? 1 : -1;
        if (lib_checked < 0) plat_moves_note(7, st, MOVELIB_N, n);   // (an exe and a mod of different builds, or no library file)
    }
    return lib_checked > 0;
}
// Move id as a LibMove into *x: 1 if it can be played.
static int lib_get(u32 id, LibMove* x) {
    if (id && id <= MOVELIB_HAND_N) { *x = move_hand[id - 1]; return 1; }
    if (!id || !lib_ok()) return 0;
    static int where_said;
    if (!where_said) { where_said = 1; plat_movex_where((u32)mx_file, (u32)NM_SLOTS_CAP << 16 | MX_E, SIXA_80069F20, (u32)NM_SLOTS_CAP << 16 | 192u); }
    if (!plat_move_own(id, (u32)own_row)) return 0;
    x->file = own_row[0]; x->then_file = own_row[1]; x->ground = own_row[2]; x->n = own_row[3];   // (n: 2026-11-20, revmoves)
    if (id == MOVELIB_NASH_JK && x->file) jk_file = x->file + 1u;
    return 1;
}
// Move id on entry e of slot s (with `move` in its place, 0: the entry's), if it can be had (the first time, the exe is told
// where the numbers it gives can be held: every slot's mx_file and the game's animation players, 192 bytes each, the file
// at +4). Returns the move as put (file 0 and no ground: nothing put).
static LibMove lib_on(s32 s, s32 e, u32 id, u32 move, u32 buf) {
    LibMove x = { 0, 0, 0, 0 };
    if (lib_get(id, &x)) lib_put(s, e, &x, move, buf);
    return x;
}
// (QA, qa_moves_twin) A list's move with an animation of its own but no hold, on a No Mercy move that goes on: after it the
// game plays the No Mercy move's continuation with its own animation. Sometimes meant (the ports' Revenge suplexes, then No
// Mercy's German Suplex Pin's bridge: gen/revanims.py), sometimes not (a grab of one game and a hold of another, the man out
// of place: the user, 2026-11-01, "Revenge wrestlers holds are glitching") -- so said in the log for the one making a list.
static void hold_check(s32 s, s32 e, u32 id, const LibMove* x) {
    if (x->file && !x->then_file && (rec_flags(s, e) & 0x1E00u)) plat_moves_note(10, id, (u32)s << 16 | (u32)e, RD16(MOVES16(s, e)));
}

// A folder wrestler's moves.json edits (2026-11-13, movelib2; exe arcade_wrestlers.cpp read_edits): his slots' records
// changed ("records": a move played harder, sooner, later than No Mercy's own), and entries of his own past the moveset's
// and the game's continuations ("extra": a move with its record -- and its library animation -- that the match reaches some
// way of the mod's: "use" 1, the leg drop, logic.c move_entry -- as Hollywood Hogan's leg drop is for the built-in port,
// below). `next` is how many continuation entries the game gave: an extra entry goes after them, as the leg drop does.
extern int plat_wrestler_edit(int i, int k, u32 dst);
s32 movex_list_folder = -1;     // (roster.c, before movex_post: the folder wrestler whose list this is; -1: none)
static u8 edit_buf[84] __attribute__((aligned(8)));
// Phase 0, before the list's library moves (2026-11-13, movelib2): a slot's "@<move>/<move>" -- the move put in its place,
// with its record (the entry's special mark kept), as a built-in port's row does (lib_put's `move`). Phase 1, after them: the
// record changes and the extra entries.
static void edits_put(s32 s, s32 f, s32 next, u32 buf, int note, int phase) {
    const int n = f >= 0 ? plat_wrestler_edit(f, 0, 0) : 0;
    for (int k = 0; k < n; k++) {
        plat_wrestler_edit(f, k, (u32)edit_buf);
        const u8* x = edit_buf;
        const u32 entry = (u32)x[0] << 8 | x[1], use = (u32)x[2] << 8 | x[3], move = (u32)x[4] << 8 | x[5];
        const u32 id = (u32)x[8] << 24 | (u32)x[9] << 16 | (u32)x[10] << 8 | x[11];
        s32 e = (s32)entry;
        if (entry < MX_BASE && move) {          // (a move in a slot's place: phase 0 only)
            if (phase != 0) continue;
            const u32 special = rec_flags(s, e) & 0x8000u;
            const s32 r = RDS16(NM_RECIX + 2u * move);
            RD16(MOVES16(s, e)) = (u16)move; RD16(RECIX16(s, e)) = (u16)r;
            for (u32 b = 0; b < 36; b += 4) RD32(REC(s, e) + b) = RD32(buf + 36u * (u32)r + b);
            RD32(REC(s, e) + 0x20u) |= special;
            if (note > 0) plat_moves_note(11, id, (u32)s << 16 | (u32)e, 2u << 16 | move);
            continue;
        }
        if (phase != 1) continue;
        if (entry >= MX_BASE) {                 // (a new entry: the move and its record, as the game copies one)
            if (next >= 256 || MX_BASE + next >= MX_E || !move) continue;
            e = MX_BASE + next++;
            const s32 r = RDS16(NM_RECIX + 2u * move);
            RD16(MOVES16(s, e)) = (u16)move; RD16(RECIX16(s, e)) = (u16)r;
            for (u32 b = 0; b < 36; b += 4) RD32(REC(s, e) + b) = RD32(buf + 36u * (u32)r + b);
            if (id) lib_on(s, e, id, 0, buf);
        }
        for (u32 b = 0; b < 36; b++) if (x[12 + b]) RD8(REC(s, e) + b) = x[48 + b];
        if (x[6]) RD32(REC(s, e) + 0x20u) |= 0x8000u;    // (special, as the game marks one)
        if (use == 1) legdrop_entry[s] = (s16)e;
        if (note > 0) plat_moves_note(11, id, (u32)s << 16 | (u32)e, use << 16 | RD16(MOVES16(s, e)));
    }
}

// After the game's unpack (and a folder wrestler's slot list put in, roster.c): `ids` his list (moveset_unpack.h: an entry's
// move ID at +4), or 0 for a moveset with no list -- a built-in port's then plays his port_moves (and so does a list with no
// move ID in it whose packed bytes are a port's, below).
void movex_post(const u8* ids) {
    s32 s = mx_slot, set = mx_set;
    mx_slot = -1; mx_set = -1;
#ifndef ARCADE_MOVEX
    return;
#endif
    if (!MX_ON || s < 0 || s >= NM_SLOTS_CAP) return;
    if (!ids && set < 0) return;
    u32 buf = RD32(NM_RECBUF);
    if (!buf) return;
    // the continuations the game gave: one per entry with one, in order
    s32 next = 0;
    for (s32 e = 0; e < MX_BASE; e++) if (rec_flags(s, e) & 0x1E00u) next++;
    // (a list that names no library move at all -- made before the libraries, by an importer that wrote only No Mercy's
    //  numbers -- with a built-in port's packed bytes plays that port's rows, as such a list did before: so no one's moves
    //  change under him. A list with any move ID is his whole moveset.)
    if (ids && set >= 0) {
        int any = 0;
        for (s32 e = 0; e < MX_BASE && !any; e++) any = ids[8 * e + 4] | ids[8 * e + 5] | ids[8 * e + 6] | ids[8 * e + 7];
        if (!any) ids = 0;
    }
    if (ids) {
        // A folder wrestler's list: each entry with a move ID plays that move (its No Mercy move already in the entry: the
        // list's, mu_match_build). Only his list's -- not a port's whose moveset bytes his might share.
        static int note = -1;   // (QA, option qa_moves_twin: each move put in, to the exe's log)
        if (note < 0) note = (int)plat_config("qa_moves_twin", 0);
        edits_put(s, movex_list_folder, next, buf, note, 0);
        for (s32 e = 0; e < MX_BASE; e++) {
            const u8* p = ids + 8 * e + 4;
            const u32 id = (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
            if (!id) continue;
            const LibMove x = lib_on(s, e, id, 0, buf);
            if (note > 0) hold_check(s, e, id, &x);
            if (note > 0) plat_moves_note(8, id, (u32)s << 16 | (u32)e, (u32)mx_file[s][e] << 16 | RD16(MOVES16(s, e)));
        }
        edits_put(s, movex_list_folder, next, buf, note, 1);
        movex_list_folder = -1;
        dbg_movex[0] = (u32)s; dbg_movex[1] = (u32)-2;
        return;
    }
    if (set < PORT_LATE_N)
        for (int i = port_moves_at[set]; i < port_moves_at[set + 1]; i++)
            lib_on(s, port_moves[i].entry, port_moves[i].id, port_moves[i].move, buf);
    dbg_movex[0] = (u32)s; dbg_movex[1] = (u32)set; dbg_movex[3] = (u32)next;
    if (set == LATE_HOGAN && next < 256) {                 // (his leg drop: the entry, a finisher's record)
        s32 c = MX_BASE + next++, r = RDS16(NM_RECIX + 2u * LEGDROP_MOVE);
        RD16(MOVES16(s, c)) = LEGDROP_MOVE; RD16(RECIX16(s, c)) = (u16)r;
        for (u32 b = 0; b < 36; b += 4) RD32(REC(s, c) + b) = RD32(buf + 36u * (u32)r + b);
        RD8(REC(s, c) + 6u) = 40; RD8(REC(s, c) + 7u) = 0; RD8(REC(s, c) + 8u) = 0xF1;
        RD8(REC(s, c) + 0x12u) = 5; RD8(REC(s, c) + 0x13u) = 2;
        RD32(REC(s, c) + 0x20u) |= 0x8000u;
        legdrop_entry[s] = (s16)c;
    }
}
// (QA, exe arcade_wrestlers.cpp: where each slot's entries' animations are, so a folder's animation numbers are given to
//  others only once no slot names them)
u32 movex_files_at(void) { return (u32)mx_file; }

// A move that goes on into a ground grapple (a library move's `ground`: Bret's front special, the slam -> his ground Sharpshooter,
// as Revenge's code 58 -> 200). The game's own ground grapple, as for a press of A at a lying man: func_8013E870(him, the
// man, 0x8000) -- the standing state's (func_800E5EB8 -> func_8013E79C, the A press: input 0x8000) -- checks the man is
// down (+0x80 & 9 == 8), on the same side of the ropes (+0x82) and within 100 (func_800EB970), then states 70 / 71 with
// +0xEB 0 (no pick-up). State 70 (func_80136240) phase 1 puts him at the head or the legs by where he stands (the man's
// facing against the way to him: face up, the head when within 0x400 -- logic.c legdrop_at_head is the same rule) --
// 150 along the man's facing at the legs, 130 behind at the head (sitting / face down otherwise) -- walks and turns him
// there (func_80132374, func_800EBA84), kneels (0x2F88), and picks the move (phase 4, func_80136D90: 0x8014DC90 by head
// | sitting | face down -- 240 at a face-up man's legs, 157 for it with SPECIAL! and the moveset's 0x4000); then plays it
// (phase 5) and goes on as the move's record says (157: the hold, 72 / 73). Its pick at the legs here is `ground`
// (movex_chain_pick, from logic.c qa_move_entry: func_800EC698's start); at the head or face down, the game's own.
// After a front grapple the man faces him and his facing is his feet's way (lying face up), so the game puts him at the
// legs. The man not down, out of reach or gone, or him doing anything else first: no chain -- as the game would.
// (TB offsets: the game's wrestler pointer; +0x14 the state, +0x308 the moveset entry, +0x8 the other man's index,
// +0x9E his own)
#define MX_TB(i)        (SIXA_80159990 + 0x358u * (u32)(i))
#define CHAIN_WAIT      600       // steps the slam may take to end (it's ~160 frames)
#define CHAIN_TRIES     20        // steps standing, the man not yet down, before it's let go
static s16 chain_wait[NM_SLOTS_CAP];   // > 0: his move with a chain began (steps left)
static s8 chain_man[NM_SLOTS_CAP], chain_on[NM_SLOTS_CAP], chain_tries[NM_SLOTS_CAP];
u32 dbg_chain[4];   // tests: chains begun (the move picked), ground grapples started, picks made the chain's, let go
// func_800EC698(w, owner, entry)'s start: entry -> the entry he goes into
s32 movex_chain_pick(u32 w, s32 owner, s32 entry) {
    s32 i = RDS16(w + 0x9E);
    if (i < 0 || i >= NM_SLOTS_CAP || owner != i) return entry;
    if (chain_on[i]) {
        if (RD32(w + 0x14) != 70u) chain_on[i] = 0;
        else if (entry == 240) { chain_on[i] = 0; dbg_chain[2]++; return chain_to[i]; }   // (the legs, face up)
        else if (entry == 155 || entry == 156 || entry == 158 || (entry >= 236 && entry <= 250)) chain_on[i] = 0;
    }
    if (chain_from[i] && entry == chain_from[i]) {
        s32 m = (s32)RD32(w + 0x8);
        if (m >= 0 && m < nm_slots() && m != i) { chain_wait[i] = CHAIN_WAIT; chain_man[i] = (s8)m; chain_tries[i] = 0; dbg_chain[0]++; }
    }
    return entry;
}
// after each step of the match's logic (logic.c arcade_match_step): a move with a chain that's over -> the ground grapple
void movex_chain_step(void) {
    for (int i = 0; i < nm_slots(); i++) {
        if (chain_wait[i] <= 0) continue;
        u32 w = MX_TB(i), st = RD32(w + 0x14);
        if (RDS16(w + 0x308) != chain_from[i] || --chain_wait[i] <= 0) { chain_wait[i] = 0; dbg_chain[3]++; continue; }
        if (st != 1u && st != 2u) continue;          // (the move, then its end: standing again -- 1, or walking on, 2)
        u32 v = MX_TB(chain_man[i]);
        if (plat_game_call(GF_GROUND_GRAPPLE, w, v, 0x8000u)) { chain_wait[i] = 0; chain_on[i] = 1; dbg_chain[1]++; }
        else if (++chain_tries[i] >= CHAIN_TRIES) { chain_wait[i] = 0; dbg_chain[3]++; }
    }
}

// func_80130878(wrestler, 0 attacker / 1 defender): the animation file of the move he's in
// (2026-11-20, revmoves) `who` is the animation's place in the move: 0 / 1 the pair, and more for the moves with more -- a
// turnbuckle dive's landing 1, 2, its miss 3, its landing on the man 4 (wrestler_state_121_125_dive_from_turnbuckle); a
// double team's man 2 (wrestler_state_133_double_teamed_man); a turnaround's 3; a running grapple's 1 / 3. An entry with
// animations of its own has mx_n of them: past those it plays the No Mercy move's own (as the game would), never the number
// after its last -- which was no animation of this move (another move's, a model's file or none: the user, 2026-11-20,
// "Ultimo Dragon's top turnbuckle move on a downed opponent glitches"; double-teamed, "he ended up standing while the CPU
// opponents did their animation on air").
s32 movex_anim(u32 w, s32 who) {
    s32 o = RDS8(w + 0x304);
    if (o >= 0 && o < NM_SLOTS_CAP && mx_any[o]) {
        s32 e = RDS16(w + 0x308);
        if (e >= 0 && e < MX_E && mx_file[o][e] && who >= 0 && who < (s32)mx_n[o][e]) return (s32)mx_file[o][e] + who;
    }
    return RDS16(NM_ANIMS + (u32)(2 * (s32)RDS16(w + 0x30A))) + 0x3322 + who;
}

// An animation's header and record. The game keeps, for its animation files 0x2DD9.. (0x169B of them), each
// file's first 4 bytes (read at boot, func_800119A4: the end pose's kind << 10 | a speed, the frames) and a
// 36-byte record (file 0x2BF: when it's struck, sounds, ...), and reads them by the file's number -- the
// files brought over from Revenge (the library's, past the game's) have theirs from the exe (movelib2). The
// game's six readers, replaced: for the game's files they do just what the game's do.
// (2026-11-13, movelib) A folder's own animations have theirs in the exe (his moves\movex.json: each file's record; the
// header is the file's first 4 bytes), asked for by the game file number the exe gave the file for the match: copied into
// one of a few places here, each kept until seven more have been asked for (the game reads a header or record at once --
// func_80013860 / func_80130898 go through the record's sounds right after asking -- so a place is never still in use).
#define AH_TABLE 0x8006A220u   // -> the headers (4 bytes a file from 0x2DD9)
#define AR_TABLE 0x8006A224u   // -> the records (36 bytes a file from 0x2DD9)
#define AH_FILE0 0x2DD9
#define AH_FILES_N 0x169B      // (the game's animation files, 0x2DD9 ..)
// (2026-11-13, movelib2) Any number that isn't one of the game's animations is asked of the exe: the library's and the
// folders' animations have numbers of the pool (exe arcade_wrestlers.cpp), wherever in it they are.
extern int plat_anim_meta(u32 file, u32 dst);
static u8 own_meta[8][40] __attribute__((aligned(8)));
static u32 own_meta_n;
static u32 own_meta_of(s32 file) {
    if (file >= AH_FILE0 && file < AH_FILE0 + AH_FILES_N) return 0;
    u32 m = (u32)own_meta[own_meta_n++ & 7u];
    return plat_anim_meta((u32)file, m) ? m : 0;
}
static u32 anim_hdr(s32 file) {
    { u32 m = own_meta_of(file); if (m) return m; }
    return RD32(AH_TABLE) + 4u * (u32)(file - AH_FILE0);
}
static u32 anim_rec(s32 file) {
    { u32 m = own_meta_of(file); if (m) return m + 4u; }
    return RD32(AR_TABLE) + 36u * (u32)(file - AH_FILE0);
}
s32 anim_frames(s32 file) { return RDS16(anim_hdr((s16)file) + 2u); }                    // func_80011A54
float anim_speed(s32 file) { return (float)(s32)(RD16(anim_hdr((s16)file)) & 0x3FFu) * 0.3515625f; }   // (exact, as the game's doubles)   // func_80011A78
s32 anim_endpose(s32 file) { return RD16(anim_hdr((s16)file)) >> 10; }                    // func_80011AC0
u32 dbg_jk_sounds;   // (QA: the game reading the sounds of the man's side of Nash's jackknife -- its thud, 0x1D)
u32 anim_recpart(s32 file) { if (jk_file && (s16)file == (s16)jk_file) dbg_jk_sounds++; return anim_rec((s16)file) + 0x14u; }   // func_80011D30
// func_80011AE8(the animation player n): the kind of pose he's in, by the frame he's at
s32 anim_pose_now(s32 n) {
    u32 p = SIXA_80069F20 + 192u * (u32)(s32)(s16)n;
    u32 r = anim_rec(RDS16(p + 4u));
    s32 at = RD8(r + 0xDu);
    if (at != 0 && !(RDS16(p + 6u) < at)) return RD8(r + 0xEu);
    return RD8(r) >> 4;
}
// (ljround9, ROUND 9 -- the Lumberjack match's "man left lying at ring height off the ring", found by the place-bit trace,
// logic.c qa_area_trace_step) An animation's record says at its byte 0 (low half) the level it ends at: 1 / 7 the mat, 3
// the apron, 2 the floor, 0 where he is ("by place"). read_slot_controls (func_80118368) sets the man's place bits (+0x82)
// from it once the file reaches its mark frame (set_place_from_end_level, func_801300F0), and settle_height_after_animation
// (func_80130038) his height (90 for the mat). Every No Mercy grapple move begun on the floor ends "by place" (0: the
// traces, 55 moves outside -- a one-on-one's 6, a tornado tag's 15, a Lumberjack match's 34 -- every attacker and man
// file 0; the game's own that end on the mat are its moves from the apron, in the ring), so a move outside ends outside. The
// moves brought over from other games (the Revenge / WrestleMania 2000 libraries, a folder's own moves: numbers of the
// exe's pool) can carry a record that ends on the mat -- an animation No Mercy plays only in the ring (revenge:126c_1270,
// Lex Luger's / Bret Hart's front grapple slam that goes on into the Sharpshooter: its man's file ends at 1, mark frame
// 103; 32 of Revenge's 783 library moves end on the mat, 5 of WrestleMania 2000's 256). Done on the floor, the man's place
// became the ring's at frame 103 while he lay 500 out by the ramp's foot, the game then put him by the ring's rules --
// height 90, against the ring's walls at (-19, 90, 352) -- until the void guard (logic.c ring_void_guard) dropped him a
// second later. In any match: a Lumberjack match made it common (lumberjacks have SPECIAL from the bell and do the game's
// grapple moves outside all the time, and are drawn from the whole roster, the imported Superstars too).
// So such an animation -- the exe's pool numbers are only these moves' (the pair, its further roles, the holds it goes on
// to; own_meta_of) -- started for a man on the floor outside (+0x82 & 4), ends where he is, as No Mercy's own moves do:
// its end level read as 0. The game's own files are never changed (their details come from the game's tables, untouched); in the ring or on
// the apron the record is as it was (a move from the apron or from ringside keeps the end its record gives: the man is
// not on the floor). QA: qa_place_keep_off 1 turns it off (the before, for the proof).
static int mx_keep_off = -1;
u32 dbg_place_kept;                         // (QA: the times an imported move's end on the mat was read as "by place")
// (an animation whose record the exe gave: a library move's or a folder's own -- the pool's numbers; own_meta_of's copy)
static int own_record(u32 r) { return r >= (u32)own_meta && r < (u32)own_meta + (u32)sizeof own_meta; }
static void keep_place(u32 out) {
    u32 o = out - 0x2E8u - SIXA_80159990;
    if (out < SIXA_80159990 + 0x2E8u || o % 0x358u) return;    // (the details for a wrestler's record, +0x2E8.., only)
    u32 w = out - 0x2E8u;
    if (o / 0x358u >= NM_SLOTS_CAP) return;
    if (mx_keep_off < 0) mx_keep_off = (int)plat_config("qa_place_keep_off", 0);
    u8 lv = RD8(out + 6u);
    if (mx_keep_off || !(RD16(w + 0x82u) & 4u) || (RD16(w + 0x82u) & 3u) || (lv != 1u && lv != 3u && lv != 7u)) return;
    RD8(out + 6u) = 0;
    dbg_place_kept++;
}
// func_80011BD8(out, file): the animation's details for its player
void anim_details(u32 out, s32 file) {
    file = (s16)file;
    if (file < AH_FILE0) return;
    u32 h = anim_hdr(file), r = anim_rec(file);
    RD32(out) = (RD16(h) & 0x3FFu) << 2;
    RD8(out + 4u) = (u8)RD16(h + 2u);
    RD8(out + 7u) = (u8)(RD16(h) >> 10);
    RD8(out + 5u) = (u8)(RD8(r + 1u) >> 7);
    RD8(out + 6u) = (u8)(RD8(r) & 0xFu);
    RD8(out + 8u) = RD8(r + 3u); RD8(out + 9u) = RD8(r + 4u);
    RD8(out + 0xAu) = RD8(r + 0x12u); RD8(out + 0xBu) = RD8(r + 0x13u);
    for (u32 k = 0; k < 8; k++) RD8(out + 0xCu + k) = RD8(r + 5u + k);
    RD8(out + 0x14u) = RD8(r + 0x11u); RD8(out + 0x15u) = RD8(r + 0x10u);
    RD8(out + 0x16u) = RD8(r + 2u);
    RD8(out + 0x17u) = (u8)(RD8(r) >> 4);
    RD8(out + 0x18u) = RD8(r + 0xDu); RD8(out + 0x19u) = RD8(r + 0xEu); RD8(out + 0x1Au) = RD8(r + 0xFu);
    if (own_record(r)) keep_place(out);    // (ROUND 9: an imported move's animation on the floor ends where he is)
}

// func_80011340(the animation player n, priority): its next piece read. An animation is played as it comes
// off the cartridge, 0x62 bytes at a time (two buffers, an osEPiStartDma each, waited for one piece later); the
// pack's animations (files.c gives them ANIM_ROM + their place in the pack) come from the pack instead,
// their request answered at once; and so do a folder's own (2026-11-13, movelib: files.c gives them the place the exe
// gave them, from ANIM_OWN_ROM). Otherwise just what the game's does.
u32 dbg_as[8];   // tests: calls, player, its ROM place, left, buffer, waits; the pack's pieces, the last
void anim_stream(s32 n, s32 pri) {
    u32 p = SIXA_80069F20 + 192u * (u32)(s32)(s16)n;
    s32 left = (s32)RD32(p + 0x14u);
    if (left == 0) return;
    u32 b = RD8(p + 2u);
    s32 len = left < 0x62 ? left : 0x62;
    u32 buf = RD32(p + 8u + 4u * b);
    func_80034310(buf, (u32)len);
    u32 m = p + 0x88u + 24u * b, q = p + 0x58u + 24u * b, rom = RD32(p + 0x10u);
    RD32(m + 4u) = q;
    RD8(m + 2u) = (u8)pri;
    RD32(m + 0xCu) = rom;
    RD32(m + 0x10u) = (u32)len;
    RD32(m + 8u) = buf;
    dbg_as[0]++; dbg_as[1] = (u32)n; dbg_as[2] = rom; dbg_as[3] = (u32)left; dbg_as[4] = b; dbg_as[5] = RD8(p + 3u);
    const int own = rom >= ANIM_OWN_ROM && rom < ANIM_OWN_ROM + ANIM_OWN_ROM_SIZE;
    u32 pack = own ? 0u : (rom >= ANIM_ROM && rom < ANIM_ROM + 0x01000000u) ? plat_wmport_rom(rom - ANIM_ROM) : 1u;
    if (!pack) {   // (the PC's pack is in memory: copied, the request answered here; a folder's animation from the exe)
        dbg_as[6]++; dbg_as[7] = rom;
        if (own) plat_anim_read(rom, buf, (u32)len);
        else plat_wmport_read(rom - ANIM_ROM, buf, (u32)len);
        osSendMesg(q, m, 0);
    } else {      // (the cartridge's, or the test ROM's copy of the pack: requested of the PI as the game does)
        if (pack != 1u) RD32(m + 0xCu) = pack;
        while (osEPiStartDma(osCartRomInit(), m, 0) != 0) { }
    }
    if (RD8(p + 3u)) osRecvMesg(p + 0x58u + 24u * (b ^ 1u), 0, 1);
    RD8(p + 2u) = (u8)(b ^ 1u);
    RD8(p + 3u) = 1;
    RD32(p + 0x10u) = rom + (u32)len;
    RD32(p + 0x14u) = (u32)(RDS32(p + 0x14u) - len);
}

#if __has_include("movex_twin.h")
#include "movex_twin.h"   // (the QA build only, scratch\agents\movelib\twin: the move rows as they were, against the library's)
#endif
