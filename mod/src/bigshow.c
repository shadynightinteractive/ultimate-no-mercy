// Big Show, in Chris Benoit's place.
//
// The game was made with Big Show on its roster: his face is still among the Create-a-Wrestler
// faces (face 3, left out of the menu), his BIG SHOW tank top is still among the shirts, and the
// music and Titantron lists have empty slots where his were. He was taken out before release.
// His looks, height and photo are the ones No Mercy Plus V3 restores him with.
// The arcade puts him back where Benoit was: the box on the Superstar Select (ID 0x201), in every
// mode. In Story Mode he plays the parts the story gave Steven Richards, and Ken Shamrock plays
// Benoit's.
//
// None of it is saved: the working copies of the roster are changed in memory, and whenever the
// game packs Benoit's data for the save it is given Benoit's own (bigshow_pack_pre / _post), so the
// save file never has Big Show in it. Turn the option (or the arcade) off and Benoit is back.
#include "arcade.h"

// ---- the roster's working data (built at boot from the ROM, then from the save) -----------------
#define NM_ROSTER_RECS   0x80047E90u // u32 -> costume records, 0x4E bytes, [ordinal * 4 + costume]
#define NM_ROSTER_MOVES  0x80047E94u // u32 -> movesets, 0xD8 bytes (0xB8 moves, 0x20 abilities), [ordinal]
#define REC_SIZE    0x4E
#define MOVES_SIZE  0xD8
#define REC_NAME    0x00             // char[0x15] name
#define REC_SHORT   0x15             // char[0x0B] short name
#define REC_NICK    0x20             // char[0x11] nickname
#define REC_WEIGHT  0x48             // u16 pounds - 100
#define REC_HEIGHT  0x4A             // u8 inches over 5'0"
#define REC_MUSIC   0x4B             // u8 entrance music
#define REC_VIDEO   0x4C             // u8 Titantron video
#define ORD_BENOIT  14               // Benoit's place in the roster (wrestler ID 0x201)
#define ID_BENOIT   0x201
#define ID_RICHARDS 0x005
#define ID_SHAMROCK 0x308

#define BS_WEIGHT   500
#define BS_HEIGHT   28               // 7'4"
#define MUSIC_BIG_SHOW 2             // his music slot: its theme is put in by media.c
#define VIDEO_BIG_SHOW 5             // his Titantron slot (empty in the game): its video is put in by media.c

static const char bs_name[] = "BIG SHOW";

// His looks, costumes 1-4, in the game's appearance format (what the ROM's defaults use, 0x26
// bytes; func_8000B60C packs it into a record): the four No Mercy Plus V3 gives him (its appearance
// table, entries 260-263), all made of parts this game has: his face, short dark hair, goatee. Items are the Create-a-Wrestler menus' (colors:
// 1 black, 2 white, 3 red, 4 blue, 5 green, 6 yellow, 7 orange, 8 purple, 9 pink, A gold, B grey):
//  00 body  01 skin  02 lower body (03-04 colors)  05 upper body (06-07)  08 entrance costume (09-0A)
//  0C gloves (0D)  0E tattoo  0F wristbands (10)  11, 13 arm bands (12, 14)  15, 17 knee pads (16, 18)
//  19 boots (1A-1B)  1C head  1D face  1E hair  1F hair color  20 front hair  21 facial hair
//  22 mask / accessory  23 hat  24 hat color  25 photo
#define LOOK_SIZE 0x26
// His photo: a new one, photo 100 (files.c: photos 100-127 are the arcade's files). (The game's
// photo 43 is Gangrel's: the new Superstars have him.)
#define PHOTO_BIG_SHOW 100
#ifdef ARCADE_TEST
u8 bs_look[4][LOOK_SIZE] = {
#else
static const u8 bs_look[4][LOOK_SIZE] = {
#endif
    // 1: the BIG SHOW logo tank top (a shirt the game has and no one wears), black trunks
    { 0x05, 0x03, 0x0C, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x03, 0x03, 0x05,
      0x06, 0x09, 0x00, 0x00, 0x00, PHOTO_BIG_SHOW },
    // 2: the logo tank top and jeans
    { 0x05, 0x03, 0x40, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x03, 0x03, 0x05,
      0x06, 0x09, 0x00, 0x00, 0x00, PHOTO_BIG_SHOW },
    // 3: a referee's shirt and jeans
    { 0x05, 0x03, 0x40, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x03, 0x03, 0x05,
      0x06, 0x09, 0x00, 0x00, 0x00, PHOTO_BIG_SHOW },
    // 4: The Giant: long hair, black trunks
    { 0x05, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x03, 0x0A, 0x04,
      0x03, 0x09, 0x00, 0x00, 0x00, PHOTO_BIG_SHOW },
};

// His moves and abilities in the roster: No Mercy Plus V3's, packed (roster_ext.h, gen/mkroster.py);
// roster.c sees that the game unpacks them right.
#define bs_moves roster_bigshow_moveset()
static int bs_on;                       // Big Show is wanted (the option, with the arcade on)
#ifdef ARCADE_TEST
int dbg_bs_rebuild;                     // tests: build his records again (after bs_look changed)
#endif
static u8 benoit_rec[4][REC_SIZE];      // Benoit's records as the game last had them
static u8 benoit_moves[MOVES_SIZE];
static int have_rec, have_moves;        // (bit per costume)
static u32 benoit_ver;                  // (counts each time they're taken again)
// Benoit as the game has him (taken when Big Show took his box): for his place among the new ones
// (roster.c) -> 0 until both his records and moves are known; changes with them
u32 bigshow_benoit(const u8** recs, const u8** moves) {
    if (have_rec != 0xF || !have_moves) return 0;
    *recs = &benoit_rec[0][0];
    *moves = benoit_moves;
    return benoit_ver;
}
u32 dbg_bs_applied, dbg_bs_restored, dbg_bs_packs, dbg_bs_story;
int bigshow_is_on(void) { return bs_on; }   // (Big Show in Benoit's box, 0x201: variants.c's pairs on him)

static void copy_bytes(u32 dst, u32 src, int n) { for (int i = 0; i < n; i++) RD8(dst + (u32)i) = RD8(src + (u32)i); }
static int same_bytes(u32 a, u32 b, int n) { for (int i = 0; i < n; i++) if (RD8(a + (u32)i) != RD8(b + (u32)i)) return 0; return 1; }
static void put_str(u32 dst, const char* s, int size) {
    int i = 0;
    for (; s[i] && i < size - 1; i++) RD8(dst + (u32)i) = (u8)s[i];
    for (; i < size; i++) RD8(dst + (u32)i) = 0;
}

static int ram_ptr(u32 p) { return GAME_PTR(p); }
static u32 rec_addr(int c) {
    u32 base = RD32(NM_ROSTER_RECS);
    return ram_ptr(base) ? base + (u32)((ORD_BENOIT * 4 + c) * REC_SIZE) : 0;
}
static u32 moves_addr(void) {
    u32 base = RD32(NM_ROSTER_MOVES);
    return ram_ptr(base) ? base + (u32)(ORD_BENOIT * MOVES_SIZE) : 0;
}
// The record is Big Show's (by its name; whatever else the game did to it)
static int is_big_show(u32 rec) { return same_bytes(rec + REC_NAME, (u32)bs_name, (int)sizeof bs_name); }

static void build_record(u32 rec, int c) {
    put_str(rec + REC_NAME, bs_name, 0x15);
    put_str(rec + REC_SHORT, bs_name, 0x0B);
    put_str(rec + REC_NICK, "", 0x11);
    plat_game_call(GF_LOOK, rec, (u32)bs_look[c], 0);
    RD16(rec + REC_WEIGHT) = BS_WEIGHT - 100;
    RD8(rec + REC_HEIGHT) = BS_HEIGHT;
    RD8(rec + REC_MUSIC) = MUSIC_BIG_SHOW;
    RD8(rec + REC_VIDEO) = VIDEO_BIG_SHOW;
}

// Every frame (the game's thread, the controller read), whatever the arcade is doing: Big Show in
// Benoit's place while he's wanted, Benoit back when he isn't. The game builds the roster at boot
// and again whenever it loads the save; a record that isn't Big Show's has just been (re)built, so
// it's Benoit's as the game has him now, and it's kept for the save.
void bigshow_frame(int on) {
    bs_on = on;
    u32 m = moves_addr();
    if (!rec_addr(0) || !m) return;
#ifdef ARCADE_TEST
    { if (dbg_bs_rebuild) { dbg_bs_rebuild = 0; if (on) for (int c = 0; c < 4; c++) if (is_big_show(rec_addr(c))) build_record(rec_addr(c), c); } }
#endif
    int rebuilt = 0;
    for (int c = 0; c < 4; c++) {
        u32 r = rec_addr(c);
        int bs = is_big_show(r);
        if (on && !bs) {
            copy_bytes((u32)benoit_rec[c], r, REC_SIZE);
            have_rec |= 1 << c;
            benoit_ver++;
            build_record(r, c);
            rebuilt = 1;
        } else if (!on && bs && (have_rec & (1 << c))) {
            copy_bytes(r, (u32)benoit_rec[c], REC_SIZE);
            dbg_bs_restored++;
        }
    }
    if (rebuilt) {
        // the moves came with the roster: Benoit's, unless the game left Big Show's there
        if (!same_bytes(m, (u32)bs_moves, MOVES_SIZE)) { copy_bytes((u32)benoit_moves, m, MOVES_SIZE); have_moves = 1; benoit_ver++; }
        copy_bytes(m, (u32)bs_moves, MOVES_SIZE);
        dbg_bs_applied++;
    } else if (!on && have_moves && same_bytes(m, (u32)bs_moves, MOVES_SIZE)) {
        copy_bytes(m, (u32)benoit_moves, MOVES_SIZE);
    }
}

// ---- the save: Benoit's own data goes in ------------------------------------------------------------
// The game packs a wrestler for the save with func_800E299C(dest, record) (a costume record) and
// func_800E2AC0(dest, moveset). Packing Benoit's while Big Show has his place packs Benoit's own,
// and Big Show is put back right after.
static u32 swap_at;
static int swap_size;
static u8 swap_keep[MOVES_SIZE];
u32 dbg_bs_pack_calls;
void bigshow_pack_pre(int moves, u32 src) {
    swap_at = 0;
    dbg_bs_pack_calls++;
    u32 r0 = rec_addr(0), m = moves_addr();
    if (!r0 || !m) return;
    if (!moves) {
        for (int c = 0; c < 4; c++) {
            u32 r = rec_addr(c);
            if (src != r || !(have_rec & (1 << c)) || !is_big_show(r)) continue;
            copy_bytes((u32)swap_keep, r, REC_SIZE);
            copy_bytes(r, (u32)benoit_rec[c], REC_SIZE);
            swap_at = r; swap_size = REC_SIZE;
        }
    } else if (src == m && have_moves && is_big_show(r0)) {
        copy_bytes((u32)swap_keep, m, MOVES_SIZE);
        copy_bytes(m, (u32)benoit_moves, MOVES_SIZE);
        swap_at = m; swap_size = MOVES_SIZE;
    }
    if (swap_at) dbg_bs_packs++;
}
void bigshow_pack_post(void) {
    if (!swap_at) return;
    copy_bytes(swap_at, (u32)swap_keep, swap_size);
    swap_at = 0;
}

// ---- Story Mode: Big Show in Richards' parts, Shamrock in Benoit's ----------------------------------
// Story Mode's people come from tables in its code's data (ovl_a, loaded again from the ROM each time
// the menus or the story come back). Each is changed where it names Richards or Benoit, once per
// load: the change of Richards' first entry (ST_RIVALS + 2) tells whether this load has had it.
// What was changed is noted, so it can be put back when Big Show is turned off.
#define ST_CAST      0x800FBE68u  // 666 parts x 12 bytes: u16 candidates [5] (bit 15: a special code), u16
#define ST_CAST_N    666
#define ST_RIVALS    0x800F8BD4u  // 8 x 3 u16: a championship's challenger (and who stands in)
#define ST_RUMBLES   0x800F8CB0u  // u32[2] -> the Royal Rumbles' entrants (u16: bits 14-15 side), u8[2] counts
#define ST_POOLS     0x800F8D84u  // u32[5] -> u16 lists ending in 0: picked from at random
#define ST_POOLS_N   5
#define ST_SENTINEL  (ST_RIVALS + 2)
#define UNDO_MAX 1024                  // (2026-11-13, removal: room for a removed man's every part too; was 160)
static struct { u32 at; u16 was, now; } undo[UNDO_MAX];
static int n_undo;

// Who plays whom: with Big Show, he takes Richards' parts and Benoit's go to Benoit among the new
// Superstars (ID 0x205), or to Ken Shamrock when Benoit is off; without either, Shamrock has Benoit's.
#define ID_BENOIT_NEW 0x205
// (2026-11-13, removal) A removed stock man (his folder deleted: roster.c "removed") is cast in no part of the game's own
// stories: wherever the tables name him -- a part's candidates, a championship's challenger and those who stand in, a
// story Rumble's entrants, the pools drawn from -- his stand-in (roster_stand_in: the men after him on his select page)
// takes his place, one not already in that part, row or list where there is one. Mode bit 4; put back as Big Show's are.
int roster_removed_n(void);
int roster_id_removed(u32 id);
u16 roster_stand_in(u32 id, int nth);
void removal_s(const char* s); void removal_who(u32 id); void removal_num(s32 v); void removal_say(void);
static int story_mode(void) { return (bs_on ? 2 : 0) | 1 | (roster_removed_n() > 0 ? 4 : 0); }   // (1: the game's own; bit 0, Benoit wanted, always since 2026-11-19)
static u16 story_map(u16 id, int mode) {
    if (mode & 2) {
        if (id == ID_RICHARDS) return ID_BENOIT;               // (Big Show's box)
        if (id == ID_BENOIT) return (mode & 1) ? ID_BENOIT_NEW : ID_SHAMROCK;
    } else if (!(mode & 1) && id == ID_BENOIT) return ID_SHAMROCK;
    return id;
}
static int cur_mode = 1;
static void remap(u32 at) {
    u16 v = RD16(at), w = (u16)((v & 0xF000u) | story_map(v & 0x0FFFu, cur_mode));
    if (w == v || n_undo >= UNDO_MAX) return;
    RD16(at) = w;
    undo[n_undo].at = at; undo[n_undo].was = v; undo[n_undo].now = w;
    n_undo++;
}
static int ovl_ptr(u32 p) { return p >= 0x800D0000u && p < 0x80110000u; }
// (2026-11-13, removal) The entry at `at` if it names a removed man: his stand-in, the first not among the n entries of its
// group at `group` (a part's five, a row's three, a list), with the undo kept as remap's. Returns 1 if it put one.
static u16 rm_said[8];
static int rm_put(u32 at, u32 group, u32 n) {
    const u16 v = RD16(at), id = v & 0x0FFFu;
    if ((v & 0x8000u) || !id || !roster_id_removed(id) || n_undo >= UNDO_MAX) return 0;
    u16 s = 0;
    for (int nth = 0; nth < 16 && !s; nth++) {
        const u16 c = roster_stand_in(id, nth);
        if (!c) break;
        int there = 0;
        for (u32 i = 0; i < n; i++) if ((RD16(group + 2u * i) & 0x0FFFu) == c) there = 1;
        if (!there) s = c;
    }
    if (!s) s = roster_stand_in(id, 0);
    if (!s) return 0;
    const u16 w = (u16)((v & 0xF000u) | s);
    RD16(at) = w;
    undo[n_undo].at = at; undo[n_undo].was = v; undo[n_undo].now = w;
    n_undo++;
    for (int k = 0; k < 8; k++) {
        if (rm_said[k] == id) break;
        if (!rm_said[k]) {
            rm_said[k] = id;
            removal_s("Story Mode: "); removal_who(id); removal_s(" is removed: his parts in the game's own stories go to stand-ins (first ");
            removal_who(s); removal_s(")"); removal_say();
            break;
        }
    }
    return 1;
}
static void rm_story_tables(void) {
    int n = 0;
    for (u32 i = 0; i < ST_CAST_N; i++)
        for (u32 k = 0; k < 5; k++) n += rm_put(ST_CAST + i * 12u + k * 2u, ST_CAST + i * 12u, 5);
    for (u32 r = 0; r < 2; r++) {
        u32 list = RD32(ST_RUMBLES + 4u * r), m = RD8(ST_RUMBLES + 8u + r);
        if (!ovl_ptr(list)) continue;
        for (u32 i = 1; i < m; i++) if (RD16(list + 2u * i) != 0x8002u) n += rm_put(list + 2u * i, list, m);
    }
    for (u32 p = 0; p < ST_POOLS_N; p++) {
        u32 list = RD32(ST_POOLS + 4u * p), m = 0;
        if (!ovl_ptr(list)) continue;
        while (m < 64 && RD16(list + 2u * m)) m++;
        for (u32 i = 0; i < m; i++) n += rm_put(list + 2u * i, list, m);
    }
    for (u32 i = 0; i < 8 * 3; i++) n += rm_put(ST_RIVALS + 2u * i, ST_RIVALS + 2u * (i - i % 3), 3);
    static int said;
    if (!said) { said = 1; removal_s("Story Mode's tables: "); removal_num(n); removal_s(" places of removed men given to stand-ins"); removal_say(); }
}
// The tables are loaded again with the story's code: a sum of them tells whether they're as the arcade
// left them (and in which mode) or fresh from the ROM.
static u32 tables_sum(void) {
    u32 h = 0;
    for (u32 i = 0; i < ST_CAST_N * 12u; i += 4) h = h * 31u + RD32(ST_CAST + i);
    for (u32 i = 0; i < 8 * 3 * 2; i += 4) h = h * 31u + RD32(ST_RIVALS + i);
    return h;
}
static u32 sum_left;
static int left_mode = -1;       // the mode the tables were left in (-1: not seen)
// (the story's code calls it before it reads a table)
void bigshow_story_tables(void) {
    int mode = story_mode();
    u32 h = tables_sum();
    int ours = left_mode >= 0 && h == sum_left;
    if (ours && mode == left_mode) return;
    if (ours) {          // back to the game's own first
        for (int i = n_undo - 1; i >= 0; i--) if (RD16(undo[i].at) == undo[i].now) RD16(undo[i].at) = undo[i].was;
    }
    n_undo = 0;
    cur_mode = mode;
    if (mode != 1) {
        for (u32 i = 0; i < ST_CAST_N; i++)
            for (u32 k = 0; k < 5; k++) {
                u32 at = ST_CAST + i * 12u + k * 2u;
                if (!(RD16(at) & 0x8000u)) remap(at);
            }
        for (u32 r = 0; r < 2; r++) {
            u32 list = RD32(ST_RUMBLES + 4u * r), n = RD8(ST_RUMBLES + 8u + r);
            if (!ovl_ptr(list)) continue;
            for (u32 i = 1; i < n; i++) if (RD16(list + 2u * i) != 0x8002u) remap(list + 2u * i);   // (0: the player, 0x8002: the partner)
        }
        for (u32 p = 0; p < ST_POOLS_N; p++) {
            u32 list = RD32(ST_POOLS + 4u * p);
            if (!ovl_ptr(list)) continue;
            for (u32 i = 0; i < 64 && RD16(list + 2u * i); i++) remap(list + 2u * i);
        }
        for (u32 i = 0; i < 8 * 3; i++) remap(ST_RIVALS + 2u * i);
        if (mode & 4) rm_story_tables();       // (2026-11-13, removal: after Big Show's, on what they made)
        dbg_bs_story++;
    }
    sum_left = tables_sum();
    left_mode = mode;
}
// The story's Royal Rumbles (func_800EE3EC) put someone in for the player when the player is one of
// the entrants: Benoit, in the game. That's Ken Shamrock's part now.
u32 bigshow_stand_in(void) { return story_map(ID_BENOIT, story_mode()); }

