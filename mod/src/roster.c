// The roster, made larger: the new Superstars get places of their own in it.
//
// The game keeps its Superstars by ordinal: 1-64 the Superstars, 65-82 the Create-a-Wrestler slots,
// 83 a special one, 84-101 the Create-a-Wrestler edit buffers (IDs 0xB01-0xB12). Everything about
// one goes through the resident roster module (func_8000A638..func_8000BD18): ordinal <-> ID
// (func_8000AE38 / func_8000AE78), the costume records (func_8000B4B8) and movesets (func_8000B560),
// built from the ROM's per-ordinal tables (func_8000B834, func_8000B914, func_8000B9B0). Those
// functions are replaced here by ones that know ordinals 102 and up: the new Superstars (roster_ext.h,
// from No Mercy Plus V3), whose records and movesets live here.
//
// The Superstar Select's pages (0x800A9990: 12 x {9 x (u16 ID, u16 ordinal), u16 count, name}) are
// built at boot from the ROM (func_8000B004) and loaded from the save (func_800E30DC); the new ones
// are given the first free boxes of SUPERSTARS 6 / 7 (pages 8-9, empty in the game) after either.
// They live here now (roster_pages_tab, up to 32 of them), laid out the game's way or by select_pages.json.
//
// Nothing of them goes into the save: the pages go in without them (func_800E3020), and the game's
// functions that put one Superstar's data in the save leave them out. The save stays the game's.
#include "arcade.h"
#include "gamefn.h"
#include "roster_ext.h"
int variant_hidden(u16 id);
u16 variant_slot_heir(u16 id);          // (2026-11-14, families: variants.c -- a removed / hidden first one's place, its next one)
int variant_wrestler_hidden(u16 id);    // (... families.json "hidden": no box, never drawn)
int variants_any_hidden(void);
static u16 box_heir(u16 id) { const u16 e = variant_slot_heir(id); return e ? e : id; }   // (variants.c: a costume variant of another Superstar -- no box of his own)
s32 roster_id_to_ord(u32 id);
u32 roster_ord_to_id(s32 ord);
static void rivals_fill(void);    // (rostertags, 2026-11-21: roster tags, after roster_build_all)
int bigshow_is_on(void);      // (bigshow.c: Big Show has Benoit's box -- whose moveset is in it, moves_whose below)

#define ROSTER_TABLE  0x80047124u   // [ordinal 0..101] -> 4 costume structs (0x14 bytes)
#define ROSTER_RECS   0x80047E90u   // -> records, 0x4E bytes, [ordinal * 4 + costume] (ordinals 0..83)
#define ROSTER_MOVES  0x80047E94u   // -> movesets, 0xD8 bytes, [ordinal]
#define ROSTER_CAWREC 0x80047E98u   // -> the edit buffers' records (ordinals 84..101)
#define ROSTER_CAWMOV 0x80047E9Cu   // -> ... and movesets
#define ROSTER_APP    0x80047EA0u   // -> the appearance file (0x26 bytes per entry)
#define ROSTER_NAMES  0x80047EA4u   // -> the names file
#define PAGE_LISTS    0x80047784u   // 12 x { u8* ordinals, u32 name }
// The pages, moved here: the select screens' and page editor's code reads them here (gen/patches.py "the
// Superstar Select's pages"; func_8000A6DC / A758 / A7D8 below), and how many there are from roster_pages_n.
// The game's own: its twelve (0x800A9990 in the game) and one more, WCW (the Revenge Superstars, the user,
// 2026-10-30). Or <program folder>\select_pages.json's (the user, 2026-10-30: any number of pages, any number on
// each -- up to the select's 32 pages of nine; exe arcade_pages.cpp reads it): see "the pages from the file".
#define MAX_PAGES     32            // (the select's page list keeps a page in five bits)
#define DEF_PAGES     13
#define WCW_PAGE      12
u8 roster_pages_tab[MAX_PAGES * 0x38] __attribute__((aligned(8)));
s32 roster_pages_n = DEF_PAGES;     // the pages there are (the select screens' loops)
u8 roster_pages_flags[MAX_PAGES];   // PAGE_NO_RANDOM: not in the Rumble's random draw (logic.c)
s32 roster_rr_fill_mark;            // (gen/patches.py func_8013486C: the Rumble's field at a round's end)
#define PAGES         ((u32)roster_pages_tab)
#define PAGE_STRIDE   0x38
#define PAGE_COUNT    0x24
#define PAGE_NAME     0x26
#define SEL_PAGE      0x800A9586u   // the Superstar Select's page (s16)
#define N_PAGES       12            // (the game's: built from the ROM, kept in the save)
#define SAVE_IMAGE    0x800F6134u   // -> the save image (ovl_a's)
#define SAVE_PAGES    0x6C48        // 12 x 0x17: name (0x0E), 9 ordinals
#define REC_SIZE      0x4E
#define MOVES_SIZE    0xD8
#define DUMMY_REC     0x80069670u   // what the getters give when there's no roster
#define DUMMY_MOVES   0x800696C0u

// The new Superstars (roster_ext.h, ext 0-3), Chris Benoit as one of them (ext 4: with Big Show in
// his box, Benoit gets a place among the new ones: ID 0x205, his records and moves the ROM's) and the
// ones after him (ext 5..: roster_ext.h ext_late -- Jeff Jarrett, Droz, Mideon, The Blue Meanie, the
// Mean Street Posse, the 1999 Undertaker, Raven, William Regal, Tiger Ali Singh; their
// looks parts.c's costumes 16 on, four each; on the roster by mask bits ROSTER_EXT_JARRETT << i).
#define EXT_ALL (EXT_N + 1 + EXT_LATE_N)
// Room for wrestlers from folders after the built-in ones (see "wrestlers from folders" below). Ordinals are one byte,
// so this is every number left.
#define EXT_CAP (256 - EXT_ORD0)
#define FOLDER_MAX (EXT_CAP - EXT_ALL)
#define EXT_BENOIT EXT_N
#define EXT_LATE0 (EXT_N + 1)
#define EXT_MASK_ALL (0xFull | ((((u64)1 << EXT_LATE_N) - 1u) << 6))   // (ROSTER_EXT_JARRETT's bit 6 on; a u64: ports4)
#define ID_BENOIT_NEW 0x205
#define ORD_BENOIT 14
#define ID_BENOIT 0x201
static u8 ext_rec[EXT_CAP][4][REC_SIZE] __attribute__((aligned(8)));
static u8 ext_moves[EXT_CAP][MOVES_SIZE] __attribute__((aligned(8)));
static u64 ext_mask = 0xF;      // who's on the roster (bit k: ext k, Jarrett ROSTER_EXT_JARRETT; ext 4, Benoit: bigshow_is_on, 2026-11-19)
static int benoit_built;        // ext 4's records and moves made (from the ROM, at boot)

// ---- wrestlers from folders -------------------------------------------------------------------------
// The exe reads the game's wrestlers\*\wrestler.json (exe src/main/arcade_wrestlers.cpp) and these go on the roster after
// the built-in ones, as ext numbers from EXT_ALL up. Each starts as a copy of the wrestler he's based on. The copy is
// made in folder_finish, once the roster's records exist to copy from.
//
// The buffer the exe fills, byte by byte: the RTW_* offsets must match wrestler_get in arcade_wrestlers.cpp.
#define RTW_ID       0x00
#define RTW_BASED_ON 0x02
#define RTW_WEIGHT   0x04          // 0xFFFF: keep the copy's
#define RTW_HEIGHT   0x06          // This and the next two, 0xFF: keep the copy's
#define RTW_MUSIC    0x07
#define RTW_VIDEO    0x08
#define RTW_FLAGS    0x09          // Bits: 1 his own costumes, 2 his own moveset, 4 his own photo, 8 his own full-detail model,
                                   // 0x10 his costumes' looks as numbers (RTW_LOOKS), 0x20 no nickname (2026-11-13, handlers1).
#define RTW_HD       0x0A          // Which of the folder models, if he has one.
#define RTW_NAME     0x0B
#define RTW_SHORT    (RTW_NAME + 0x15)
#define RTW_NICK     (RTW_SHORT + 0x0B)
#define RTW_KEYS     (RTW_NICK + 0x11)
#define RTW_MOVES    (RTW_KEYS + 4 * 0x17)
#define RTW_PHOTO    (RTW_MOVES + MOVES_SIZE)
#define RTW_LOOKS    (RTW_PHOTO + 32 + 2048)   // (2026-11-13) 4 costumes x the 38 looks fields, in fields[] order (parts.c)
#define LOOK_FIELDS  38
#define RTW_SIZE     (RTW_LOOKS + 4 * LOOK_FIELDS)   // (2026-11-13, revbody) 1 if his folder gives a "size", then it (0-255)
#define RTW_BYTES    (RTW_SIZE + 2)
static u8 folder_w[FOLDER_MAX][RTW_BYTES] __attribute__((aligned(8)));
static int folder_n;               // How many seats there are (below): read once, by ext_total.
static int folder_read;
extern int plat_wrestler_count(void);
extern void plat_wrestler_get(int i, u32 dst);

// ---- staging seats (2026-11-13, seatsroot) ------------------------------------------------------------------------
// TEMPORARY: a shim over the game's one-byte roster ordinals. The root fix is Stage 2's native records (scratch\agents\
// stage2\PLAN.md, step 4: the wrestler and roster records), then Stage 3's wide fields, which give a wrestler an ID of any
// width and retire all of this.
//
// The user (2026-11-13): no limit at all on how many wrestlers there can be. The game knows a wrestler by a one-byte ordinal,
// and the ordinals left after the game's own (0-101) and the built-in ports' are FOLDER_MAX = 112. Until now each folder
// wrestler had one of them for good, so there could be 112 (0.81 kept the rest off the roster: seatable()). Now those
// ordinals are SEATS: a seat holds one folder wrestler at a time, and a wrestler gets one when the game asks for him -- by
// his game number (func_8000AE78, roster_id_to_ord), as the select's pages, the match, the Rumble's entrants, a story or the
// title records need him -- and gives it up only when nothing the game is using still names him (seat_free's pins). His
// ordinal, photo number (FOLDER_PHOTO(seat)) and looks marker (the seat in it) are the seat's while he has it. Every folder
// is still read by the exe; the mod fetches a wrestler's data (plat_wrestler_get) as he takes a seat.
//
// With no more folders than seats nothing of this changes anything: seat i holds folder i from the start, no seat is ever
// taken back, and every ordinal is what it was ("staging" stays 0). Only past FOLDER_MAX folders are seats handed out and
// taken back. How many can be in play at once is then 112 (the select's window of pages, the match, the Rumble's field, the
// champions): an "at once" limit, which the user's rule allows -- it bounds what the game holds, not what can exist.
#define SEATS FOLDER_MAX
static s32 seat_folder[SEATS];     // the folder (the exe's number for it) in each seat; -1: empty
static u32 seat_used[SEATS];       // seat_tick when the game last asked for him
static u8 seat_photo_dirty[SEATS]; // a new wrestler is in it: the game's sprite cache lets go of the seat's photo (menus)
static int folder_total;           // how many folders the exe read: any number
static int staging;                // more folders than seats: seats handed out as they're needed
static u32 seat_tick;              // roster_frame's count of frames
static int seat_busy = -1;         // the seat being filled now (never the one his base is given)
static int seat_probe;             // roster_probe: a check that mustn't take a seat (logic.c's Rumble draw)
static u32 seats_taken, seats_given_back, seats_refused;   // (for the log and the tests)
extern int plat_wrestler_find(u32 id);    // the exe: the folder with this game number, or -1
extern u32 plat_wrestler_id(int i);       // ... folder i's game number
extern void plat_seat_note(int kind, int seat, int folder, int other);   // ... its log, "[seats]" lines
#define SEAT_NOTE_STAGING 1         // (seat: seats, folder: folders)
#define SEAT_NOTE_TAKE    2         // (other: the folder that had it, or -1)
#define SEAT_NOTE_FULL    3         // (no seat free: every one is in use at once)
#define SEAT_NOTE_MATCH   4         // (taken while the roster's records are gone: built when the menus are back)
static void folder_finish(int k);
static int seat_take(int f);
static void seat_fill(int s, int f) {
    seat_folder[s] = f;
    seat_used[s] = seat_tick;
    plat_wrestler_get(f, (u32)folder_w[s]);
}

// How many wrestlers there are after the game's own, built-in and from folders: the built-in ones, then the seats.
static int ext_total(void) {
    if (!folder_read) {
        folder_read = 1;
        int n = plat_wrestler_count();
        folder_total = n > 0 ? n : 0;
        staging = folder_total > SEATS;
        for (int s = 0; s < SEATS; s++) seat_folder[s] = -1;
        // (as it always was: the first SEATS folders in the first seats, in folder order -- the exe's log has the count)
        folder_n = staging ? SEATS : folder_total;
        for (int i = 0; i < folder_n; i++) seat_fill(i, i);
        if (staging) plat_seat_note(SEAT_NOTE_STAGING, SEATS, folder_total, 0);
    }
    // (2026-11-14) folder_n is 0..FOLDER_MAX in the game -- only the lines above set it -- but nothing here said so where it is
    //  read, and every index made from the total (folder_w, ext_rec, ext_moves: each FOLDER_MAX / EXT_CAP long) relies on it:
    //  an index past their end is undefined in C, and the compiler took the ranges from that. Kept to them here, the same
    //  total for every value the game has, and a defined one for any other. Nothing changes in the game.
    return EXT_ALL + (folder_n < 0 ? 0 : folder_n > FOLDER_MAX ? FOLDER_MAX : folder_n);
}
static int ext_is_folder(int k) { return k >= EXT_ALL; }
static int seat_of_folder(int f) { for (int s = 0; s < folder_n; s++) if (seat_folder[s] == f) return s; return -1; }

// Whose moveset it is (2026-11-10). A moveset used to be known by its bytes, so every moveset had to differ from every
// other. The 2K15 Stone Cold (a test slot, removed since: no2k15) had stock Austin's moves, so two of his taunts were swapped
// just to make him different, and the swap put his standing taunt where his special taunt belongs. The user: "His taunts should match stock Austins."
// And of a rule that movesets must differ: "Wait, what, that sounds like a limit. We don't want limits." So a moveset
// is known by whose it is, and two may be the same byte for byte. How it's told: moves_whose, near the end of this file.
#define WHOSE_UNKNOWN 0             // Can't be told. (0, so a folder wrestler who isn't built yet is this.)
#define WHOSE_GAME    1             // One of the game's own Superstars: no cells and no move rows of ours.
#define WHOSE_ROW0    2             // WHOSE_ROW0 + r: the man of roster_ext.h's moveset row r (moveset_key, moveset_cells).
static u8 folder_whose[FOLDER_MAX]; // Each folder wrestler's moveset: whose it was when he was built (folder_finish).
static int moves_whose(u32 moves);
// (2026-11-13, movesets) A folder wrestler's moveset can be a list of slots (his folder's moves.json: any move in any
// slot), not only the packed bytes: moveset_unpack.h, and "movesets as slot lists" below. Whose list his moveset is, as
// a folder number + 1: his own, or his base's (a folder wrestler based on one with a list, with no moveset of his own);
// 0: none, the packed bytes as before (and what it is until ext_build_all makes them). The lists themselves stay in the
// exe (plat_wrestler_slots), so the mod keeps no room for them.
// (2026-11-13, final082: by seat, as everything here -- seatsroot's staging seats; the number kept is the EXE's folder
//  number + 1, which plat_wrestler_slots takes, and past the seats can be any, so not a byte)
static s32 folder_list[FOLDER_MAX];

// A photo of his own needs a photo number no one else has, and that's also how the model code knows him: the looks reach
// it as unpacked fields, and the wrestler's ID doesn't. The game's and the built-in ports' photos stop well short of the
// top of the 0-255 range, so folder wrestlers count down from 255. FOLDER_PHOTO_MIN is below every folder's number.
#define FOLDER_PHOTO(i) (255 - (i))
#define FOLDER_PHOTO_MIN (255 - (FOLDER_MAX - 1))
// The photo files' pictures (files.c), or 0 if this photo isn't a folder wrestler's.
// (2026-11-13, seatsroot: "folder number" in these is the SEAT -- the photo is the seat's while he has it; a seat no one
//  has is no one's photo)
const u8* roster_folder_photo(int photo) {
    ext_total();
    const int i = 255 - photo;
    if (i < 0 || i >= folder_n || seat_folder[i] < 0 || !(folder_w[i][RTW_FLAGS] & 4u)) return 0;
    return folder_w[i] + RTW_PHOTO;
}
// Which folder model the wrestler with this photo has, or -1 (parts.c).
int roster_folder_hd_of_photo(int photo) {
    ext_total();
    const int i = 255 - photo;
    if (i < 0 || i >= folder_n || seat_folder[i] < 0 || !(folder_w[i][RTW_FLAGS] & 8u)) return -1;
    return folder_w[i][RTW_HD];
}
// The folder wrestler with this photo: his number (his seat), or -1.
int roster_folder_of_photo(int photo) {
    ext_total();
    const int i = 255 - photo;
    return (i >= 0 && i < folder_n && seat_folder[i] >= 0 && (folder_w[i][RTW_FLAGS] & 4u)) ? i : -1;
}
// The folder (the exe's number) in a seat, or -1: for the exe's calls that name a folder wrestler by his folder, from a
// seat found in a looks marker or a photo (handlers1's model calls, parts.c parts_model_folder).
int roster_seat_folder(int seat) { return seat >= 0 && seat < folder_n ? seat_folder[seat] : -1; }
// The packed looks in a record are 23 bytes. For a wrestler with his own photo they're a marker instead: this is what
// marks it, then the folder number and costume, with the top bit of the last byte set as the built-in ports' keys have
// it (parts.c). parts_unpack finds his real looks from here.
// (2026-11-13, handlers1) A wrestler whose folder gives his looks as numbers ("looks", RTW_LOOKS) has the marker too,
// photo or not: those numbers are his looks, kept as they are (RTW_LOOKS in his buffer) -- not packed, so they may hold parts
// past the packing's widths and be the same as anyone's.
#define FOLDER_KEY_MARK0 0xF0u
#define FOLDER_KEY_MARK1 0x46u
#define FOLDER_KEY_LAST  0x80u
static u8 folder_real_key[FOLDER_MAX][4][0x17];
static int folder_has_looks(int i) { return i >= 0 && i < folder_n && seat_folder[i] >= 0 && (folder_w[i][RTW_FLAGS] & 0x10u); }
// His real packed looks, first costume, for the photo of the wrestler he's based on (parts.c).
const u8* roster_folder_real_key(int i) { return folder_real_key[i][0]; }
// If this is a folder wrestler's marker: his real packed looks for that costume, and his photo number. Otherwise 0 (and
// for one whose looks are numbers: roster_folder_looks).
const u8* roster_folder_key(u32 src, int* photo) {
    if (RD8(src) != FOLDER_KEY_MARK0 || RD8(src + 1u) != FOLDER_KEY_MARK1 || RD8(src + 0x16u) != FOLDER_KEY_LAST) return 0;
    const int i = RD8(src + 2u), c = RD8(src + 3u) & 3;
    if (i >= folder_n || seat_folder[i] < 0 || folder_has_looks(i)) return 0;
    *photo = FOLDER_PHOTO(i);
    return folder_real_key[i][c];
}
// (2026-11-13, handlers1) The 38 looks fields of folder wrestler i's costume c, as his folder gives them -- his "looks"
// numbers, else his packed looks unpacked -- into the looks struct at dst. The photo (0x3C) is the one in them: his base's,
// for one with his own photo (parts.c matches the base's whole-body models by it).
void parts_unpack(u32 src, u32 dst);
void parts_fields_put(const u8* values, u32 dst);
void roster_folder_base_looks(int i, int c, u32 dst) {
    if (folder_has_looks(i)) parts_fields_put(folder_w[i] + RTW_LOOKS + (c & 3) * LOOK_FIELDS, dst);
    else parts_unpack((u32)folder_real_key[i][c & 3], dst);
}
// (2026-11-13, revbody) Folder wrestler i's own size for his model ("size": the game's 0-255 size, 0.97 + 0.00047 x size
// as its scale), or -1: the size his height gives, as the game makes it (superstar_size). parts.c / skel.c put it on.
int roster_folder_size(int i) {
    return (i >= 0 && i < folder_n && seat_folder[i] >= 0 && folder_w[i][RTW_SIZE]) ? (int)folder_w[i][RTW_SIZE + 1] : -1;
}
// If this is the marker of a folder wrestler whose looks are numbers: those numbers into the looks struct at dst, with his
// own photo if he has one, and 1. Otherwise 0. (parts_unpack, func_8001AC44's)
int roster_folder_looks(u32 src, u32 dst) {
    if (RD8(src) != FOLDER_KEY_MARK0 || RD8(src + 1u) != FOLDER_KEY_MARK1 || RD8(src + 0x16u) != FOLDER_KEY_LAST) return 0;
    const int i = RD8(src + 2u), c = RD8(src + 3u) & 3;
    if (!folder_has_looks(i)) return 0;
    parts_fields_put(folder_w[i] + RTW_LOOKS + c * LOOK_FIELDS, dst);
    if (folder_w[i][RTW_FLAGS] & 4u) RD8(dst + 0x3C) = (u8)FOLDER_PHOTO(i);
    return 1;
}
static u32 folder_u16(int k, u32 at) { const u8* w = folder_w[k - EXT_ALL]; return (u32)w[at] << 8 | w[at + 1]; }

static const ExtWrestler* ext_w(int k) { return k >= EXT_LATE0 ? &ext_late[k - EXT_LATE0] : &ext_wrestlers[k]; }
static u16 ext_id(int k) { return ext_is_folder(k) ? (u16)folder_u16(k, RTW_ID) : k == EXT_BENOIT ? ID_BENOIT_NEW : ext_w(k)->id; }
// (2026-11-14, revfix) A built-in Superstar a folder replaces ("replaces" in its wrestler.json: the exe gives that folder
// wrestler the built-in's own number) is not built: the number is the folder's, and ext_of_id finds his seat. Asked of the
// exe once per built-in (the folders don't change while the game runs).
static s8 ext_rep[EXT_ALL];          // 0 not asked yet, 1 replaced, -1 not
static int ext_replaced(int k) {
    if (k < 0 || k >= EXT_ALL) return 0;
    if (!ext_rep[k]) { const u16 id = k == EXT_BENOIT ? ID_BENOIT_NEW : ext_w(k)->id; ext_rep[k] = id && plat_wrestler_find(id) >= 0 ? 1 : -1; }
    return ext_rep[k] > 0;
}
static int ext_wanted(int k) {
    if (ext_is_folder(k)) return seat_folder[k - EXT_ALL] >= 0;     // (Having the folder is what puts him on the roster; his seat, in it now.)
    if (ext_replaced(k)) return 0;                                 // (2026-11-14, revfix: his folder is he)
    if (k >= EXT_LATE0) return ((ext_mask >> 6) >> (k - EXT_LATE0)) & 1u;
    if (k == EXT_BENOIT) return bigshow_is_on() && benoit_built;   // (2026-11-19: among the new ones while Big Show has his ordinal)
    return (ext_mask >> k) & 1u;
}
static const u8* ext_moveset(int k) { return moveset_key[k >= EXT_LATE0 ? MOVESET_LATE0 + (k - EXT_LATE0) : k]; }
static int ext_media;           // No Mercy Plus's themes and videos are on
static int ext_built;           // records made for this media setting (bit 1: valid)

static int ext_index(s32 ord) {
    u32 k = (u32)(ord - EXT_ORD0);
    if (!(k < (u32)ext_total() && ext_wanted((int)k))) return -1;
    if (ext_is_folder((int)k)) seat_used[k - EXT_ALL] = seat_tick;     // (asked for: his seat stays his a while)
    return (int)k;
}
// (2026-11-13, seatsroot) A wrestler from a folder with no seat now gets one here: this is how the game asks for him
// (func_8000AE78, by his game number). roster_probe holds that back for a check that only asks "is he someone".
static int ext_of_id(u32 id) {
    for (int k = 0, n = ext_total(); k < n; k++)
        if (ext_id(k) == id && ext_wanted(k)) { if (ext_is_folder(k)) seat_used[k - EXT_ALL] = seat_tick; return k; }
    if (!staging || !id || id >= 0x1000u) return -1;
    const int f = plat_wrestler_find(id);
    if (f < 0) return -1;
    if (seat_probe) return EXT_ALL;     // (some seat's number: the check only needs one past the game's)
    const int s = seat_take(f);
    return s >= 0 ? EXT_ALL + s : -1;
}
int roster_is_ext_ord(s32 ord) { return ext_index(ord) >= 0; }
int roster_is_ext_id(u32 id) { return ext_of_id(id) >= 0; }
// Is this the game number of a folder wrestler (seated or not)? Takes no seat. (2026-11-13, seatsroot)
static int folder_id_of(u32 id) {
    if (!id || id >= 0x1000u) return -1;
    if (!staging) { for (int s = 0; s < folder_n; s++) if (folder_u16(EXT_ALL + s, RTW_ID) == id) return s; return -1; }
    return plat_wrestler_find(id);
}
// More folder wrestlers than seats: the select's pages are a window, the Rumble draws from every page (logic.c).
int roster_staging(void) { ext_total(); return staging; }
// While on, a folder wrestler without a seat is given none by func_8000AE78 (a check of the game's -- the Rumble's
// eligibility, func_80134830 -- asks for his ordinal only to see it isn't one of its own hidden ones). (logic.c)
void roster_probe(int on) { seat_probe = on; }
// A folder wrestler by the exe's number for his folder: his game number (no seat taken) -- QA's Rumble of folder wrestlers.
u16 roster_folder_id_at(u32 r) {
    ext_total();
    if (!folder_total) return 0;
    const int f = (int)(r % (u32)folder_total);
    if (!staging) return (u16)folder_u16(EXT_ALL + f, RTW_ID);
    return (u16)plat_wrestler_id(f);
}
int roster_is_folder_id(u32 id) { ext_total(); return folder_id_of(id) >= 0; }
// (2026-11-14, families) On the roster now and not removed -- a family's wrestler in play (variants.c) -- asked without
// giving a folder wrestler a seat (only one the game then shows takes one).
int roster_id_removed(u32 id);
int roster_id_on(u32 id) {
    const int probe = seat_probe;
    seat_probe = 1;
    const s32 o = roster_id_to_ord(id);
    seat_probe = probe;
    return o > 0 && !roster_id_removed(id);
}
int roster_ord_is_new(s32 ord) { return ord >= EXT_ORD0; }   // (on the roster or not)
static int ext_ord(s32 o) { return o >= EXT_ORD0 && o < EXT_ORD0 + ext_total(); }

// ---- removed stock Superstars (2026-11-13, removal) --------------------------------------------------------------------
// The user, 2026-11-13: every wrestler is a folder, the game's own too; deleting a stock man's folder REMOVES him from the
// game; the importer brings him back (nm_import.py --only <id>: his folder written again -- the only way back, the user's R17).
// The exe says who is removed (arcade_wrestlers.cpp: a man his collection's stock_roster.json lists, whose folder is gone).
// Removed means he can't be picked, drawn or cast:
//   - his box is off the Superstar Select (the game's pages and select_pages.json's: rm_boxes_off, box_ord), and the save's
//     pages keep it where it was (roster_pages_to_save), so the page editor's saved order comes back with him;
//   - Random (roster_random_ord), the attract's demo picks (logic.c) and the Royal Rumble's draws (the pages) pass him over;
//   - his stand-in (roster_stand_in: the man after him on his select page) plays wherever something still asks for him:
//     Story Mode's own tables (bigshow.c), the roles of any story and the parts of a custom story (story.c), an
//     exhibition preset (save.c), Regular's attract demos (logic.c) -- the user's R6, R8, R9, R10 (2026-11-13);
//   - a title he holds is vacated at once (save.c: empty, no champion, until someone wins it; his return doesn't give it
//     back, the user's R3);
//   - other men's ties to him are emptied while he is away (rm_relations, the user's R13);
//   - a costume variant of his (variants.c: Undertaker 1999 on The Undertaker's box) goes with him: removed too (R14).
// Nothing of him is deleted: the game builds his records and moveset from its own tables as ever, and the save keeps what
// names him -- former titles, presets (him in them, not the stand-in), the story in progress, his edited costumes (save.c
// says each in the log). So nothing breaks, and with his folder back everything but a vacated title is as it was.
// With nobody removed none of this runs: rm_n is 0 and every path below is the one before.
extern int plat_removed_count(void);
extern int plat_removed_get(int i);
extern void plat_removal_text(const char* text);
#define RM_MAX 0x54
static u16 rm_id[RM_MAX];
static int rm_n = -1;               // how many (-1: not asked yet)
static int rm_qa = -1;              // qa_removal: 1 the QA set-up (save.c), 2 the QA dump of pages, titles, roles, slots
static void rm_read(void) {
    if (rm_n >= 0) return;
    rm_n = 0;
    const int n = plat_removed_count();
    for (int i = 0; i < n && rm_n < RM_MAX; i++) { const int id = plat_removed_get(i); if (id > 0 && id < 0x1000) rm_id[rm_n++] = (u16)id; }
    rm_qa = (int)plat_config("qa_removal", 0);
}
int roster_removed_n(void) { rm_read(); return rm_n; }
int roster_removed_qa(void) { rm_read(); return rm_qa; }
static int rm_listed(u32 id) { for (int k = 0; k < rm_n; k++) if (rm_id[k] == (u16)id) return 1; return 0; }
// Removed: one the exe says. (2026-11-13 R14 had a removed man's costume variants go with him; 2026-11-14 the user: his
// family stays, headed by the next one -- variants.c.)
int roster_id_removed(u32 id) {
    rm_read();
    if (rm_n <= 0) return 0;
    if (rm_listed(id)) return 1;
    // (2026-11-14, the user: a removed head's family members in folders of their own are NOT removed with him -- the family
    //  stays in his box, headed by the next one (variants.c variant_slot_heir); folders nested in his go, being inside it.)
    return 0;
}
// A box's man is removed: by his ID -- and ordinal 14, Benoit's own box, while it is Benoit's (Big Show off): his folder's
// number is 0x205, the one he has in this game (bigshow.c).
static int box_removed(u16 id, s32 o) {
    if (roster_removed_n() <= 0) return 0;   // (removal2: asks the exe first -- rm_n alone was -1 at the boot's first layout, so he was on it)
    if (roster_id_removed(id)) return 1;
    // (2026-11-14, from the native mod's triage: roster_id_removed gives 0 or 1, and the compiled code hands its result back as it
    //  is where `&&` promises 0 or 1 for any result -- the C now says what is done. The same result for every value.)
    return o == ORD_BENOIT && !bigshow_is_on() ? roster_id_removed(ID_BENOIT_NEW) : 0;
}
// The lines for the log, built here (no printf in the mod): "@" + four hex digits is a game number, which the exe writes
// as his string ID.
static char rm_line[320];
static int rm_len;
void removal_s(const char* s) { while (*s && rm_len < (int)sizeof rm_line - 1) rm_line[rm_len++] = *s++; rm_line[rm_len] = 0; }
void removal_who(u32 id) {
    char h[6] = { '@', 0, 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) { const u32 d = (id >> (12 - 4 * i)) & 15u; h[1 + i] = (char)(d < 10 ? '0' + d : 'A' + d - 10); }
    removal_s(h);
}
void removal_num(s32 v) {
    char b[12];
    int n = 0;
    u32 u = v < 0 ? (u32)-v : (u32)v;
    do { b[n++] = (char)('0' + u % 10u); u /= 10u; } while (u && n < 11);
    if (v < 0) removal_s("-");
    while (n) { char c[2] = { b[--n], 0 }; removal_s(c); }
}
void removal_say(void) { plat_removal_text(rm_line); rm_len = 0; rm_line[0] = 0; }
// the Superstar Select's Random (ovl_c func_8010A9E8): an ordinal from a random number, the game's
// 0-64 (0 is drawn again) and the new Superstars' when they're on (gen/patches.py)
s32 roster_random_ord(s32 r) {
    { extern u16 variant_qa_random; if (variant_qa_random) return roster_id_to_ord(variant_qa_random); }   // (QA: variants.c)
    s32 on[EXT_CAP], m = 0;
    for (int k = 0, n_ext = ext_total(); k < n_ext; k++) if (ext_wanted(k) && !(staging && ext_is_folder(k))) on[m++] = k;
    // (2026-11-13, seatsroot: staging, every folder wrestler can be drawn, seated or not -- the one drawn takes a seat.
    //  Otherwise every folder is seated and in on[], as before.)
    const s32 more = staging ? folder_total : 0;
    // (2026-11-14, families: a hidden wrestler is never drawn either -- variants.c variant_wrestler_hidden)
    if (roster_removed_n() > 0 || variants_any_hidden()) {   // (2026-11-13, removal: the draw among those on the roster; 0 stays in it, drawn again)
        s32 st[65], ns = 0, mm = 0;
        for (s32 o = 0; o < 65; o++)
            if (!o || (!box_removed((u16)roster_ord_to_id(o), o) && !variant_wrestler_hidden((u16)roster_ord_to_id(o)))) st[ns++] = o;
        for (s32 i = 0; i < m; i++) if (!roster_id_removed(ext_id(on[i])) && !variant_wrestler_hidden(ext_id(on[i]))) on[mm++] = on[i];
        s32 k = r % (ns + mm + more);
        if (k < 0) k += ns + mm + more;
        if (k >= ns + mm) {        // (staging: any folder wrestler -- seatsroot)
            const int f = k - ns - mm;
            if (roster_id_removed(plat_wrestler_id(f)) || variant_wrestler_hidden((u16)plat_wrestler_id(f))) return 0;
            const int s = seat_take(f);
            return s >= 0 ? EXT_ORD0 + EXT_ALL + s : 1;
        }
        return k >= ns ? EXT_ORD0 + on[k - ns] : st[k];
    }
    s32 n = 65 + m + more;
    s32 k = r % n;
    if (k < 0) k += n;
    if (k >= 65 + m) { const int s = seat_take(k - 65 - m); return s >= 0 ? EXT_ORD0 + EXT_ALL + s : 1; }
    if (k >= 65) return EXT_ORD0 + on[k - 65];
    return k;
}

static u32 table_struct(s32 ord, s32 c) { return RD32(ROSTER_TABLE + 16u * (u32)ord + 4u * (u32)c); }

// ---- ordinal <-> ID -----------------------------------------------------------------------------
// func_8000AE38
u32 roster_ord_to_id(s32 ord) {
    if ((u32)(ord - 0x54) < 0x12) return (u32)(ord + 0xAAD);
    if ((u32)(ord - 1) < 0x53) return RD16(table_struct(ord, 0));
    int k = ext_index(ord);
    return k >= 0 ? ext_id(k) : 0;
}
// func_8000AE78
s32 roster_id_to_ord(u32 id) {
    if ((id & 0xF00u) == 0xB00u) return (s32)id - 0xAAD;
    for (s32 o = 0; o < 0x54; o++) if (RD16(table_struct(o, 0)) == id) return o;
    int k = ext_of_id(id);
    return k >= 0 ? EXT_ORD0 + k : 0;
}

// func_8000AAF4(id, costume): a Superstar's entrance song (his music slot's); func_8000AB64: its effects.
// Music slots EXT_SLOT0 on are media.c's (more music slots: the new Superstars').
s32 media_slot_song(u32 slot); int media_slot_fx(u32 slot, u32* fx); void media_entrance_tables(void);
s32 media_user_song(u32 id);
u32 roster_record(s32 ord, s32 c);
// His entry in the match's list (0x800BD4D0: an ID per entry, 0x800A9358: its costume), whose record at 0x80097FE0 holds
// his song once the select's records are gone (in a match). 2026-11-11: the game's lookup, func_8000A690, matches the
// costume too, and was called here with the ID alone -- so it compared against whatever was left in a register, missed,
// and gave entry 0: a Rumble run-in played the first entrant's theme (QA: HBK and Andre came in to the folder wrestler's
// theme.bin). Now his costume is passed, and if that still misses (a costume the list holds differently), he's found by
// ID alone: a run-in plays his own theme on purpose, never somebody else's.
static u32 match_entry(u32 id, s32 c) {
    u32 i = func_8000A690(id, (u32)c);
    if (RD16(0x800BD4D0u + 2u * i) == (u16)id) return i;
    for (u32 k = 0; k < 0x65u; k++) if (RD16(0x800BD4D0u + 2u * k) == (u16)id) return k;
    return i;       // (not in the list at all: the game's own answer, entry 0, as it always was)
}
s32 roster_entrance_song(u32 id, s32 c) {
    s32 us = media_user_song(id);                // (a music file for him: its theme; his slot's effects)
    if (us >= 0) return us;
    if (!RD32(ROSTER_RECS)) return (s8)RD8(0x80097FE0u + 0x114u * match_entry(id, c));
    u32 slot = RD8(roster_record(roster_id_to_ord(id), c) + 0x4Bu);
    s32 s = media_slot_song(slot);
    return s >= 0 ? s : (s16)RD16(0x800F50D0u + 12u * slot);
}
// Changed for new features (Ultimate No Mercy): the stagescale agent, 2026-11-20. Bits 8-15 of the effects are the colour of
// the light his close shots are lit with -- the whole close scene and him (0: white; Hogan's purple, Jarrett's green: the rig,
// vanilla No Mercy, those bits cleared -> white light, the rest of his entrance's effects, its flares, as they were). WCW/nWo
// Revenge and WrestleMania 2000 light their entrance stages white (the user, 2026-11-20: "ensure the closeup stages look
// identical to how they look in Revenge"): in the mod's arenas from them (arena_current() 10 on, a folder arena by the one
// it plays as) his colour is taken off; in No Mercy's own arenas, his own.
#define FX_COLOUR 0xFF00u
extern int arena_current(void);
u32 roster_entrance_fx(u32 id, s32 c) {
    media_entrance_tables();
    u32 slot = RD8(roster_record(roster_id_to_ord(id), c) + 0x4Bu), fx;
    if (!media_slot_fx(slot, &fx)) fx = RD32(0x800F50D4u + 12u * slot);
    return arena_current() >= 10 ? fx & ~FX_COLOUR : fx;
}

// ---- the getters ---------------------------------------------------------------------------------
// func_8000B4B8(ordinal, costume)
u32 roster_record(s32 ord, s32 c) {
    u32 caw = RD32(ROSTER_CAWREC);
    if ((u32)(ord - 0x54) < 0x12 && caw) return caw + (u32)(((ord - 0x54) * 4 + (c & 3)) * REC_SIZE);
    int k = ext_index(ord);
    if (k >= 0) return (u32)ext_rec[k][c & 3];
    u32 base = RD32(ROSTER_RECS);
    if (ord < 1 || !base || ord >= 0x55) return DUMMY_REC;
    return base + (u32)((ord * 4 + (c & 3)) * REC_SIZE);
}
// func_8000B560(ordinal)
u32 roster_moves(s32 ord) {
    u32 caw = RD32(ROSTER_CAWMOV);
    if ((u32)(ord - 0x54) < 0x12 && caw) return caw + (u32)((ord - 0x54) * MOVES_SIZE);
    int k = ext_index(ord);
    if (k >= 0) return (u32)ext_moves[k];
    u32 base = RD32(ROSTER_MOVES);
    if (ord < 1 || !base || ord >= 0x55) return DUMMY_MOVES;
    return base + (u32)(ord * MOVES_SIZE);
}

// ---- building them --------------------------------------------------------------------------------
static void put_str(u32 dst, const char* s, int size) {
    int i = 0;
    for (; s[i] && i < size - 1; i++) RD8(dst + (u32)i) = (u8)s[i];
    for (; i < size; i++) RD8(dst + (u32)i) = 0;
}
static void ext_build(int k, int c, u32 rec) {
    const ExtWrestler* w = ext_w(k);
    put_str(rec + 0x00, w->name, 0x15);
    put_str(rec + 0x15, w->short_name, 0x0B);
    put_str(rec + 0x20, w->nick, 0x11);
    const u8* key = parts_costume_key(k >= EXT_LATE0 ? k - 1 : k, c);   // the looks (parts.c: its new body parts)
    for (int i = 0; i < 0x17; i++) RD8(rec + 0x31 + (u32)i) = key[i];
    RD16(rec + 0x48) = w->weight;
    RD8(rec + 0x4A) = w->height;
    RD8(rec + 0x4B) = w->music[ext_media ? 1 : 0];
    RD8(rec + 0x4C) = w->video[ext_media ? 1 : 0];
}
// Benoit among the new ones: his records and moves as the game has them (bigshow.c keeps them when
// Big Show takes his box)
u32 bigshow_benoit(const u8** recs, const u8** moves);
static u32 benoit_ver_built;
static void benoit_build(void) {
    const u8 *recs, *moves;
    u32 v = bigshow_benoit(&recs, &moves);
    if (!v || v == benoit_ver_built) return;
    for (int c = 0; c < 4; c++) for (int i = 0; i < REC_SIZE; i++) ext_rec[EXT_BENOIT][c][i] = recs[c * REC_SIZE + i];
    for (int i = 0; i < MOVES_SIZE; i++) ext_moves[EXT_BENOIT][i] = moves[i];
    benoit_ver_built = v;
    benoit_built = 1;
}
// A wrestler from a folder: a copy of the one he's based on, then whatever his folder overrides.
void roster_build_record(u32 id, s32 c, u32 rec);
void roster_load_moves(u32 id, u32 buf);
static void folder_finish(int k) {
    const u8* w = folder_w[k - EXT_ALL];
    const s32 base = roster_id_to_ord(folder_u16(k, RTW_BASED_ON));
    if (base <= 0) return;
    // (2026-11-13, seatsroot: a seat taken in a match -- staging -- finds the roster's records gone; a base of the game's own
    //  is then made from the ROM's tables, as func_8000B834 / func_8000B914 make them, if their files are there)
    const int from_rom = base < 0x54 && !RD32(ROSTER_RECS) && GAME_PTR(RD32(ROSTER_APP)) && GAME_PTR(RD32(ROSTER_NAMES));
    for (int c = 0; c < 4; c++) {
        const u32 from = roster_record(base, c);
        u8* rec = ext_rec[k][c];
        if (from_rom) roster_build_record(folder_u16(k, RTW_BASED_ON), c, (u32)rec);
        else for (int i = 0; i < REC_SIZE; i++) rec[i] = RD8(from + (u32)i);
        if (w[RTW_NAME]) put_str((u32)rec + 0x00, (const char*)w + RTW_NAME, 0x15);
        if (w[RTW_SHORT]) put_str((u32)rec + 0x15, (const char*)w + RTW_SHORT, 0x0B);
        if (w[RTW_NICK] || (w[RTW_FLAGS] & 0x20u)) put_str((u32)rec + 0x20, (const char*)w + RTW_NICK, 0x11);   // ("nick": null -- none: 2026-11-13)
        if (w[RTW_FLAGS] & 1u) for (int i = 0; i < 0x17; i++) rec[0x31 + i] = w[RTW_KEYS + c * 0x17 + i];
        if (w[RTW_FLAGS] & 0x14u) {    // His own photo, or looks as numbers: keep his real looks aside, and put the marker in the record.
            const int f = k - EXT_ALL;
            for (int i = 0; i < 0x17; i++) { folder_real_key[f][c][i] = rec[0x31 + i]; rec[0x31 + i] = 0; }
            rec[0x31 + 0] = FOLDER_KEY_MARK0; rec[0x31 + 1] = FOLDER_KEY_MARK1; rec[0x31 + 2] = (u8)f; rec[0x31 + 3] = (u8)c; rec[0x31 + 0x16] = FOLDER_KEY_LAST;
        }
        if (folder_u16(k, RTW_WEIGHT) != 0xFFFFu) RD16((u32)rec + 0x48) = (u16)folder_u16(k, RTW_WEIGHT);
        if (w[RTW_HEIGHT] != 0xFF) rec[0x4A] = w[RTW_HEIGHT];
        if (w[RTW_MUSIC] != 0xFF) rec[0x4B] = w[RTW_MUSIC];
        if (w[RTW_VIDEO] != 0xFF) rec[0x4C] = w[RTW_VIDEO];
        {   // A video in his folder beats a number in his file.
            extern int media_folder_video(u32 id);
            const int own = media_folder_video(folder_u16(k, RTW_ID));
            if (own >= 0) rec[0x4C] = (u8)own;
        }
    }
    const u32 moves = roster_moves(base);
    for (int i = 0; i < MOVES_SIZE; i++) ext_moves[k][i] = (w[RTW_FLAGS] & 2u) ? w[RTW_MOVES + i] : RD8(moves + (u32)i);
    if (from_rom && !(w[RTW_FLAGS] & 2u)) roster_load_moves(folder_u16(k, RTW_BASED_ON), (u32)ext_moves[k]);   // (as his records: seatsroot)
    // (2026-11-10) Whose moveset he has now. A copy of the moveset of the wrestler he's based on is that man's, and is
    // told the way his is. A moveset from his own folder arrives as bytes alone, with nothing to say whose packing it
    // is: it stays unknown, and is matched by its bytes when the game unpacks it (cells_on).
    folder_whose[k - EXT_ALL] = (u8)((w[RTW_FLAGS] & 2u) ? WHOSE_UNKNOWN : moves_whose(moves));
}
// (2026-11-13, movesets) Whose slot list each folder wrestler's moveset is (folder_list). His own if his folder has one.
// Otherwise, with no packed moveset of his own either, he has his base's moveset -- so his base's list, when his base is a
// folder wrestler with one (followed down a chain of such bases, as far as there are folders).
extern int plat_wrestler_slots(int i, u32 dst);
// (2026-11-13, final082: per seat -- s is a seat, the exe is asked by the folder in it, seat_folder; seat_take makes a seat's
//  again when a new wrestler takes it after the roster's records were built)
static void folder_list_set(int s) {
    const int f0 = s >= 0 && s < folder_n ? seat_folder[s] : -1;
    if (f0 < 0) { if (s >= 0 && s < folder_n) folder_list[s] = 0; return; }
    folder_list[s] = plat_wrestler_slots(f0, 0) ? f0 + 1 : 0;
    int at = s;
    for (int depth = 0; !folder_list[s] && depth < folder_n; depth++) {
        if (folder_w[at][RTW_FLAGS] & 2u) break;                     // (his own packed moveset: that's his)
        const int k = ext_index(roster_id_to_ord(folder_u16(EXT_ALL + at, RTW_BASED_ON)));
        if (k < 0 || !ext_is_folder(k)) break;                         // (based on one of the game's or a built-in port)
        at = k - EXT_ALL;
        const int fa = seat_folder[at];
        if (fa >= 0 && plat_wrestler_slots(fa, 0)) folder_list[s] = fa + 1;
    }
}
static void folder_lists(void) {
    for (int s = 0; s < folder_n; s++) folder_list[s] = 0;
    for (int s = 0; s < folder_n; s++) folder_list_set(s);
}
static void rm_relations(u32 id, u32 moves);   // (2026-11-13, removal: below, by the moveset loader)
static void ext_build_all(void) {
    for (int k = 0; k < EXT_ALL; k++) {
        if (k == EXT_BENOIT) continue;
        for (int c = 0; c < 4; c++) ext_build(k, c, (u32)ext_rec[k][c]);
        for (int i = 0; i < MOVES_SIZE; i++) ext_moves[k][i] = ext_moveset(k)[i];
    }
    for (int k = EXT_ALL, n = ext_total(); k < n; k++) if (ext_wanted(k)) folder_finish(k);   // (an empty seat: no one to build)
    folder_lists();
    ext_built = 2 | ext_media;
    if (roster_removed_n() > 0)              // (2026-11-13, removal: the added Superstars' ties too -- below, rm_relations)
        for (int k = 0, n = ext_total(); k < n; k++) rm_relations(ext_id(k), (u32)ext_moves[k]);
}
// ---- stock folders (2026-11-13, nmhandler) --------------------------------------------------------------------------
// Every stock Superstar is a folder too (the user, 2026-11-13: "We gotta do the folder thing with No Mercy wrestlers too").
// The No Mercy handler (tools\handlers\nm_import.py) writes one for each of the game's 64 from the user's own
// ROM -- his record, his costumes' looks (the game builds his model from them, out of its own parts), his moveset, his
// photos -- and the exe hands this mod each folder that has data (exe arcade_wrestlers.cpp, "stock folders"). Here the game
// still builds his record and moveset from its own tables first, exactly as before -- that is the fallback, so a missing
// folder, or a field a folder leaves out, is the built-in's -- and then puts each field his folder gives over it. Nothing
// else changes: the records are where the game keeps them, and the save, Create-a-Wrestler's edits of a stock man and
// everything that reads a record see what they always saw. The handler's folders are the built-in's byte for byte
// (tools\handlers\p0_nomercy.py), so with them the game plays exactly as it does without.
//
// QA (option qa_stock_twin): each record, moveset, model and photo the folder gives is compared with the built-in's at the
// moment the game builds it, and the result written to the exe's log ("[wrestlers] stock twin ...").
//
// The buffer the exe fills (exe stock_get): his game number, flags, then per costume STK_C_SIZE bytes, then the moveset.
#define STK_ID       0x00
#define STK_FLAGS    0x02          // 1: his moveset
#define STK_COSTUME  0x04
#define STK_C_SIZE   0x60
#define STK_C_FLAGS  0x00          // u16: what this costume's folder gives -- 1 name, 2 short name, 4 nickname, 8 height,
#define STK_C_NAME   0x02          //      0x10 weight, 0x20 music, 0x40 video, 0x80 looks
#define STK_C_SHORT  (STK_C_NAME + 0x15)
#define STK_C_NICK   (STK_C_SHORT + 0x0B)
#define STK_C_HEIGHT (STK_C_NICK + 0x11)
#define STK_C_WEIGHT (STK_C_HEIGHT + 1)
#define STK_C_MUSIC  (STK_C_WEIGHT + 2)
#define STK_C_VIDEO  (STK_C_MUSIC + 1)
#define STK_C_LOOKS  (STK_C_VIDEO + 1)
#define STK_MOVES    (STK_COSTUME + 4 * STK_C_SIZE)
#define STK_BYTES    (STK_MOVES + MOVES_SIZE)
// One per ordinal the game builds from its own tables (0-83): the stock roster's own size, not a limit on wrestlers (a new
// one is a folder wrestler above).
#define STOCK_MAX    0x54
static u8 stock_w[STOCK_MAX][STK_BYTES] __attribute__((aligned(8)));
static s8 stock_of_ord[STOCK_MAX];  // ordinal -> his stock folder, -1: none
static u8 stock_exe_i[STOCK_MAX];   // his stock folder -> the exe's number for it (for the notes)
static int stock_n = -1;            // how many (-1: not read yet)
static int stock_twin = -1;         // qa_stock_twin
static u8 stock_said[STOCK_MAX];    // (what the log has been told once: bit c costume c's record, 0x10 the moveset)
static u8 stock_looks_said[STOCK_MAX];   // (bit c: costume c's looks that the packing cut)
extern int plat_stock_count(void);
extern void plat_stock_get(int i, u32 dst);
extern int plat_stock_photo(int i, int c, u32 dst);
extern void plat_stock_note(int i, int what, int c, int a, u32 b);
GAMEFN(void, func_80018A3C, 0x80018A3Cu, (u32 looks, u32 model));
int stock_looks_offset(int i);       // a costume's model from its looks (patched: parts.c)
void parts_unpack(u32 src, u32 dst);
// The 38 looks fields (a folder's "looks", in parts.c fields[] order) -> the 0x26-byte appearance entry func_8000B60C packs:
// which entry byte each field is (func_8000B60C's copy, exe src/game/lifted.cpp pack_looks_into_record).
static const u8 stock_app_of_field[38] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x0C,
    0x0D, 0x11, 0x12, 0x13, 0x14, 0x0F, 0x10, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x0E, 0x23, 0x24, 0x0B, 0x25 };
#define PHOTO_FIELD 37             // (the last field: the looks' photo number, 0x3C)

// The ordinal the game builds a stock man at, from his number: the game's own table (ordinals 1-83). Benoit is 0x205 in
// this game (Big Show has the ROM's 0x201 box: bigshow.c), and his record is still built at the ROM's ordinal 14.
static s32 stock_ord_of(u32 id) {
    if (id == ID_BENOIT_NEW) return ORD_BENOIT;
    for (s32 o = 1; o < STOCK_MAX; o++) if (RD16(table_struct(o, 0)) == id) return o;
    return 0;
}
static void stock_read(void) {
    if (stock_n >= 0) return;
    stock_n = 0;
    for (int o = 0; o < STOCK_MAX; o++) stock_of_ord[o] = -1;
    const int n = plat_stock_count();
    for (int i = 0; i < n && stock_n < STOCK_MAX; i++) {
        u8* w = stock_w[stock_n];
        plat_stock_get(i, (u32)w);
        const u32 id = (u32)w[STK_ID] << 8 | w[STK_ID + 1];
        const s32 o = stock_ord_of(id);
        if (o <= 0 || stock_of_ord[o] >= 0) { plat_stock_note(i, 6, 0, 0, id); continue; }
        stock_of_ord[o] = (s8)stock_n;
        stock_exe_i[stock_n] = (u8)i;
        stock_n++;
    }
    stock_twin = (int)plat_config("qa_stock_twin", 0);
}
static int stock_index(s32 ord) { stock_read(); return (ord > 0 && ord < STOCK_MAX) ? stock_of_ord[ord] : -1; }
static int stock_exe(int k) { return stock_exe_i[k]; }
static int stock_has_moves(int k) { return stock_w[k][STK_FLAGS + 1] & 1u; }
static u32 stock_c_flags(int k, int c) { const u8* s = stock_w[k] + STK_COSTUME + c * STK_C_SIZE; return (u32)s[0] << 8 | s[1]; }

// His folder's fields over the record the game just built at rec, costume c: each the way the game's builder writes it --
// the names zeroed and copied (func_8000B780), the looks packed by the game's own func_8000B60C from an appearance entry made
// of his 38 fields, the size, music and video bytes. Byte 0x4D, which the game's builder never writes, stays.
static void stock_apply(int k, int c, u32 rec) {
    const u8* s = stock_w[k] + STK_COSTUME + c * STK_C_SIZE;
    const u32 f = stock_c_flags(k, c);
    if (f & 1u) put_str(rec + 0x00, (const char*)s + STK_C_NAME, 0x15);
    if (f & 2u) put_str(rec + 0x15, (const char*)s + STK_C_SHORT, 0x0B);
    if (f & 4u) put_str(rec + 0x20, (const char*)s + STK_C_NICK, 0x11);
    if (f & 0x80u) {
        static u8 app[0x28] __attribute__((aligned(8)));
        for (int i = 0; i < 38; i++) app[stock_app_of_field[i]] = s[STK_C_LOOKS + i];
        func_8000B60C(rec, (u32)app);
        // A value past what the game's packed looks hold is cut by its packing: said in the log, never silently (the folder
        // wrestlers' looks-as-numbers keep such values: handlers1; a stock folder needing them is the next step).
        static u8 un[0x40] __attribute__((aligned(8)));
        parts_unpack(rec + 0x31, (u32)un);
        if (!(stock_looks_said[k] & (1u << c)))
            for (int i = 0; i < 38; i++)
                if (un[stock_looks_offset(i)] != s[STK_C_LOOKS + i]) { stock_looks_said[k] |= (u8)(1u << c); plat_stock_note(stock_exe(k), 5, c, i, s[STK_C_LOOKS + i]); break; }
    }
    if (f & 8u) RD8(rec + 0x4A) = s[STK_C_HEIGHT];
    if (f & 0x10u) RD16(rec + 0x48) = (u16)((u32)s[STK_C_WEIGHT] << 8 | s[STK_C_WEIGHT + 1]);
    if (f & 0x20u) RD8(rec + 0x4B) = s[STK_C_MUSIC];
    if (f & 0x40u) RD8(rec + 0x4C) = s[STK_C_VIDEO];
}
// QA: the record his folder made against the one the game built from its tables (rom), the models the game builds from
// each one's looks, the music slot. Once per costume.
static u8 stock_rom_rec[REC_SIZE] __attribute__((aligned(8)));
static u8 stock_model_a[0x270] __attribute__((aligned(8))), stock_model_b[0x270] __attribute__((aligned(8)));
static u8 stock_un_a[0x40] __attribute__((aligned(8))), stock_un_b[0x40] __attribute__((aligned(8)));
static void stock_twin_record(int k, int c, u32 rec) {
    if (stock_said[k] & (1u << c)) return;
    stock_said[k] |= (u8)(1u << c);
    int n = 0, first = 0;
    for (int i = 0; i < REC_SIZE; i++) if (RD8(rec + (u32)i) != stock_rom_rec[i]) { if (!n) first = i; n++; }
    plat_stock_note(stock_exe(k), 1, c, n, (u32)first);
    plat_stock_note(stock_exe(k), 7, c, RD8(rec + 0x4Bu) != stock_rom_rec[0x4B], RD8(rec + 0x4Bu));
    // His model: the game's model builder on the looks the game unpacks from each record (the descriptor names his parts'
    // files by number: the built-in parts both ways).
    parts_unpack((u32)stock_rom_rec + 0x31u, (u32)stock_un_a);
    parts_unpack(rec + 0x31u, (u32)stock_un_b);
    for (int i = 0; i < 0x270; i++) { stock_model_a[i] = 0; stock_model_b[i] = 0; }
    func_80018A3C((u32)stock_un_a, (u32)stock_model_a);
    func_80018A3C((u32)stock_un_b, (u32)stock_model_b);
    n = 0;
    for (int i = 0; i < 0x270; i++) n += stock_model_a[i] != stock_model_b[i];
    plat_stock_note(stock_exe(k), 3, c, n, 0);
}

// A stock man's photo: if a stock folder gives photo p (the photo number in a costume's looks), the photo's three files --
// its TEX (0x45B3 + p), palette (0x4AAD + 2p) and picture (0x4AAE + 2p) -- are his folder's, laid out as the game's (the
// same headers as files.c's folder photos). The game's own file is read first (the fallback; and for the QA twin). Returns
// 1 if dest now holds the folder's file.
static const u8 stock_tex_header[16] = { 'T', 'E', 'X', 0, 0x00, 0x40, 0x00, 0x40, 0x04, 0x02, 0x00, 0x10, 0, 0, 0, 0 };
static const u8 stock_ci4_header[8] = { 0x3F, 0x3F, 0x00, 0x20, 0x00, 0x00, 0x06, 0x06 };
static u8 stock_photo_buf[32 + 2048] __attribute__((aligned(8)));
static u8 stock_photo_said[128];
int roster_stock_photo_file(u32 file, u32 dest) {
    stock_read();
    if (!stock_n || file < 0x45B3u || file >= 0x4AAEu + 2u * 100u) return 0;
    int p, part;
    if (file < 0x45B3u + 100u) { p = (int)(file - 0x45B3u); part = 0; }
    else if (file >= 0x4AADu) { p = (int)((file - 0x4AADu) >> 1); part = 1 + (int)((file - 0x4AADu) & 1u); }
    else return 0;
    for (int k = 0; k < stock_n; k++)
        for (int c = 0; c < 4; c++) {
            if (!(stock_c_flags(k, c) & 0x80u) || stock_w[k][STK_COSTUME + c * STK_C_SIZE + STK_C_LOOKS + PHOTO_FIELD] != p) continue;
            if (!plat_stock_photo(stock_exe(k), c, (u32)stock_photo_buf)) return 0;
            const u8* head = part == 0 ? stock_tex_header : part == 2 ? stock_ci4_header : 0;
            const u32 nh = part == 0 ? 16u : part == 2 ? 8u : 0u;
            const u32 from = part == 2 ? 32u : 0u, len = part == 0 ? 2080u : part == 1 ? 32u : 2048u;
            int n = 0;
            for (u32 i = 0; i < nh; i++) { n += RD8(dest + i) != head[i]; RD8(dest + i) = head[i]; }
            for (u32 i = 0; i < len; i++) { n += RD8(dest + nh + i) != stock_photo_buf[from + i]; RD8(dest + nh + i) = stock_photo_buf[from + i]; }
            if (stock_twin && p < 128 && !(stock_photo_said[p] & (1u << part))) {
                stock_photo_said[p] |= (u8)(1u << part);
                plat_stock_note(stock_exe(k), 4, p, n, (u32)part);
            }
            return 1;
        }
    return 0;
}
// QA: every photo a stock folder gives, read once through the game's own file reader (func_80003C94, as the select screen
// reads it), so each one's three files go through roster_stock_photo_file's check at start-up, not only those a test shows.
static void stock_twin_photos(void) {
    static int done;
    if (done || stock_n <= 0 || !stock_twin) return;
    done = 1;
    u8 info[0x10] __attribute__((aligned(8)));
    const u32 buf = func_800007D4(2096 + 16);
    if (!buf) return;
    for (int k = 0; k < stock_n; k++)
        for (int c = 0; c < 4; c++) {
            if (!(stock_c_flags(k, c) & 0x80u)) continue;
            const u32 p = stock_w[k][STK_COSTUME + c * STK_C_SIZE + STK_C_LOOKS + PHOTO_FIELD];
            if (p >= 100u || stock_photo_said[p] == 7u) continue;
            func_80003C94((u32)info, 0x45B3u + p, buf);
            func_80003C94((u32)info, 0x4AADu + 2u * p, buf);
            func_80003C94((u32)info, 0x4AAEu + 2u * p, buf);
        }
    func_8000093C(buf);
}

// ---- staging seats: given and taken back (2026-11-13, seatsroot; TEMPORARY, see "staging seats" above) ---------------
// Where the game keeps a wrestler while it is using him: a seat whose wrestler is named in any of these is never taken
// back. (Each is a place the game reads an ID or an ordinal from later; anything not listed here that the game keeps
// longer than a few seconds would be a bug in this list -- the seat's "last asked for" time is the second guard.)
//   - the select's pages (roster_pages_tab), while the menus have the roster (in a match they are put right again when
//     the menus come back: seats_pages_heal);
//   - the select's picks (0x800A95A0: 50 x 12 bytes, the Rumble's included), the match's slots (NM_SLOT_WRESTLER) and
//     its list of entrants (0x800BD4D0: 101 x ID | costume << 12 -- the Rumble's whole field, whose records it copies);
//   - the champions: the 7 title records (0x800A3EB0 + 0x34 t: +2 holder, +3 partner, ordinals);
//   - the story in progress (the save image's +0x6F90 block, 16 x two ordinals).
// The save stores ordinals in 7 bits, so no seat's ordinal (142 on since no2k15; 144 before) can be in it: the save sidecar keeps ours (save.c).
#define SEL_PICKS       0x800A95A0u   // 50 x 12 bytes: u16 Superstar, u8 costume, u8 human (logic.c NM_SEL_REC)
#define SEL_PICKS_N     50
#define SEAT_MATCH_IDS   0x800BD4D0u  // u16 a place: Superstar (logic.c NM_RR_LIST; movesets' MATCH_IDS further down, the same)
#define SEAT_MATCH_IDS_N 101
#define TITLE_RECS      0x800A3EB0u   // 7 x 0x34 (tools\lift\fields.json TitleRecord)
#define SAVE_IMAGE_PTR  0x80062C20u
#define SAVE_STORY      0x6F90u
#define SEAT_RECENT     300u          // frames (5 s): a seat asked for this recently is taken back only if nothing else is free
static u8 seat_pin[SEATS];
static void pin_id(u32 id) {
    id &= 0x0FFFu;
    if (!id) return;
    for (int s = 0; s < folder_n; s++) if (seat_folder[s] >= 0 && folder_u16(EXT_ALL + s, RTW_ID) == id) seat_pin[s] = 1;
}
static void pin_ord(u32 o) {
    const u32 s = o - (u32)(EXT_ORD0 + EXT_ALL);
    if (s < (u32)folder_n) seat_pin[s] = 1;
}
static void pins_scan(void) {
    for (int s = 0; s < SEATS; s++) seat_pin[s] = 0;
    if (seat_busy >= 0) seat_pin[seat_busy] = 1;
    if (RD32(ROSTER_RECS))
        for (int p = 0; p < roster_pages_n && p < MAX_PAGES; p++) {
            const u32 pg = PAGES + (u32)(p * PAGE_STRIDE);
            for (u32 i = 0; i < 9 && i < RD16(pg + PAGE_COUNT); i++) pin_ord(RD16(pg + 4u * i + 2));
        }
    for (u32 i = 0; i < SEL_PICKS_N; i++) pin_id(RD16(SEL_PICKS + 12u * i));
    for (u32 s = 0; s < NM_SLOTS_CAP; s++) pin_id(RD32(NM_SLOT_WRESTLER + 4u * s));
    for (u32 i = 0; i < SEAT_MATCH_IDS_N; i++) pin_id(RD16(SEAT_MATCH_IDS + 2u * i));
    for (u32 t = 0; t < 7; t++) { pin_ord(RD8(TITLE_RECS + 0x34u * t + 2)); pin_ord(RD8(TITLE_RECS + 0x34u * t + 3)); }
    const u32 img = RD32(SAVE_IMAGE_PTR);
    if (GAME_PTR(img) && RD8(img + SAVE_STORY + 1) != 0xFF)
        for (u32 i = 0; i < 16; i++) { pin_ord(RD8(img + SAVE_STORY + 8 + 3 * i)); pin_ord(RD8(img + SAVE_STORY + 9 + 3 * i)); }
}
// A seat for a new wrestler: an empty one, else the one asked for longest ago that nothing names (first those not asked
// for in the last SEAT_RECENT frames). -1: every seat is named by something in play at once.
static int seat_free(void) {
    for (int s = 0; s < folder_n; s++) if (seat_folder[s] < 0 && s != seat_busy) return s;
    pins_scan();
    for (int pass = 0; pass < 2; pass++) {
        int best = -1;
        for (int s = 0; s < folder_n; s++) {
            if (seat_pin[s]) continue;
            if (!pass && seat_tick - seat_used[s] < SEAT_RECENT) continue;
            if (best < 0 || (s32)(seat_used[s] - seat_used[best]) < 0) best = s;
        }
        if (best >= 0) return best;
    }
    return -1;
}
// Folder f into a seat (his own if he has one): his data from the exe, his records made as folder_finish makes every folder
// wrestler's. -1: no seat free (said in the log, once a while).
static int seat_take(int f) {
    ext_total();
    if (f < 0 || f >= folder_total) return -1;
    int s = seat_of_folder(f);
    if (s >= 0) { seat_used[s] = seat_tick; return s; }
    s = seat_free();
    if (s < 0) { if (!(seats_refused++ & 63)) plat_seat_note(SEAT_NOTE_FULL, -1, f, (int)seats_refused); return -1; }
    const int was = seat_folder[s];
    if (was >= 0) seats_given_back++;

    seats_taken++;
    seat_fill(s, f);
    seat_photo_dirty[s] = 1;
    plat_seat_note(SEAT_NOTE_TAKE, s, f, was);
    if (ext_built) {           // (his records: as soon as the roster's are there to copy from -- in a match, when it's over)
        if (!RD32(ROSTER_RECS)) plat_seat_note(SEAT_NOTE_MATCH, s, f, 0);
        const int busy = seat_busy;
        seat_busy = s;         // (his base may need a seat too: never this one)
        folder_finish(EXT_ALL + s);
        folder_list_set(s);    // (his moveset's slot list, movesets': 2026-11-13, final082)
        seat_busy = busy;
    }
    return s;
}
// The select's pages, back in the menus: a box whose wrestler lost his seat in a match (the pages aren't held then) gets
// one again, and its ordinal is put right. (Each box: one look at its seat; a search only for those that moved.)
static void seats_pages_heal(void) {
    for (int p = 0; p < roster_pages_n && p < MAX_PAGES; p++) {
        const u32 pg = PAGES + (u32)(p * PAGE_STRIDE);
        for (u32 i = 0; i < 9 && i < RD16(pg + PAGE_COUNT); i++) {
            const u32 e = pg + 4u * i, id = RD16(e), s = RD16(e + 2) - (u32)(EXT_ORD0 + EXT_ALL);
            if (s < (u32)folder_n && seat_folder[s] >= 0 && folder_u16(EXT_ALL + (int)s, RTW_ID) == id) continue;
            if (s >= (u32)folder_n && RD16(e + 2) < EXT_ORD0 + EXT_ALL) continue;     // (not a seat's: the game's, a built-in's)
            const int t = seat_take(plat_wrestler_find(id));
            if (t >= 0) RD16(e + 2) = (u16)(EXT_ORD0 + EXT_ALL + t);
        }
    }
}
// func_8000B834(id, costume, record): a costume record from the ROM's tables
void roster_build_record(u32 id, s32 c, u32 rec) {
    s32 ord = id ? roster_id_to_ord(id) : 0;
    if (id == 0xC00u) { func_80036A30(rec, 0x15); return; }
    int k = ext_index(ord);
    if (k == EXT_BENOIT || ext_is_folder(k)) { for (int i = 0; i < REC_SIZE; i++) RD8(rec + (u32)i) = ext_rec[k][c & 3][i]; return; }
    if (k >= 0) { ext_build(k, c & 3, rec); return; }
    u32 s = table_struct(ord, c);
    func_8000B60C(rec, RD32(ROSTER_APP) + 0x26u * RD16(s + 0xE));
    func_8000B780(rec, RD16(s + 0x10));
    RD8(rec + 0x4A) = RD8(s + 6);
    RD16(rec + 0x48) = RD16(s + 8);
    RD8(rec + 0x4B) = RD8(s + 4);
    RD8(rec + 0x4C) = RD8(s + 5);
    // (2026-11-13, nmhandler) A stock man with a folder: his folder's fields over the built-in's just built.
    const int sk = stock_index(ord);
    if (sk >= 0) {
        if (stock_twin) for (int i = 0; i < REC_SIZE; i++) stock_rom_rec[i] = RD8(rec + (u32)i);
        stock_apply(sk, c & 3, rec);
        if (stock_twin) stock_twin_record(sk, c & 3, rec);
    }
}
// (2026-11-13, removal) A moveset's four relations -- the abilities' fields 37-40, four 12-bit IDs at bits 149.. of the
// packed abilities (0xB8 on), most significant bit first (read_ability_fields' widths; checked on every stock moveset
// against the game's own unpack: the handler's moves.json): the men he is tied to, whom the game's run-ins and partners
// come from (The Rock's: Triple H, Big Show, The Undertaker). A removed man is taken out (0, as an empty one), said once
// each; the moveset as built here is the roster's in memory only -- a stock man's moveset never goes into the save.
static u32 rel_get(u32 a, u32 pos) { u32 v = 0; for (u32 i = 0; i < 12; i++, pos++) v = v << 1 | ((RD8(a + (pos >> 3)) >> (7 - (pos & 7))) & 1u); return v; }
static void rel_clear(u32 a, u32 pos) { for (u32 i = 0; i < 12; i++, pos++) RD8(a + (pos >> 3)) &= (u8)~(1u << (7 - (pos & 7))); }
static void rm_relations(u32 id, u32 moves) {
    static u8 said[0x54][4];
    for (u32 k = 0; k < 4; k++) {
        const u32 pos = 149u + 12u * k, r = rel_get(moves + 0xB8u, pos);
        if (!r || !box_removed((u16)r, roster_id_to_ord(r))) continue;
        rel_clear(moves + 0xB8u, pos);
        const s32 o = roster_id_to_ord(id);
        if (o > 0 && o < 0x54 && !said[o][k]) {
            said[o][k] = 1;
            removal_s("relations: "); removal_who(r); removal_s(" taken out of "); removal_who(id);
            removal_s("'s four ties (his run-ins and partners come from them): place "); removal_num((s32)k + 1); removal_say();
        }
    }
}
// func_8000B914(id, moveset): a moveset from the ROM (its moves file and abilities file)
void roster_load_moves(u32 id, u32 buf) {
    u8 info[2][0x10] __attribute__((aligned(8)));
    func_80036A30(buf, MOVES_SIZE);
    if (!id) return;
    s32 ord = roster_id_to_ord(id);
    int k = ext_index(ord);
    if (k >= 0) { for (int i = 0; i < MOVES_SIZE; i++) RD8(buf + (u32)i) = (k == EXT_BENOIT || ext_is_folder(k)) ? ext_moves[k][i] : ext_moveset(k)[i]; return; }
    u32 s = table_struct(ord, 0);
    func_80003C94((u32)info[0], RD16(s + 0xA), buf);
    func_80003C94((u32)info[1], RD16(s + 0xC), buf + 0xB8);
    // (2026-11-13, nmhandler) A stock man whose folder gives his moveset: the folder's 216 bytes over the ROM's.
    const int sk = stock_index(ord);
    if (sk >= 0 && stock_has_moves(sk)) {
        const u8* m = stock_w[sk] + STK_MOVES;
        int n = 0;
        for (int i = 0; i < MOVES_SIZE; i++) { n += RD8(buf + (u32)i) != m[i]; RD8(buf + (u32)i) = m[i]; }
        if (stock_twin && !(stock_said[sk] & 0x10u)) { stock_said[sk] |= 0x10u; plat_stock_note(stock_exe(sk), 2, 0, n, 0); }
    }
    if (roster_removed_n() > 0) rm_relations(id, buf);   // (2026-11-13, removal: no run-ins or partners from a removed man)
}
// func_8000B9B0: the whole roster from the ROM
void roster_build_all(void) {
    func_8000B168();
    u32 recs = RD32(ROSTER_RECS), moves = RD32(ROSTER_MOVES);
    for (s32 o = 0; o < 0x54; o++) {
        u32 id = roster_ord_to_id(o);
        for (s32 c = 0; c < 4; c++) roster_build_record(id, c, recs + (u32)((o * 4 + c) * REC_SIZE));
        roster_load_moves(id, moves + (u32)(o * MOVES_SIZE));
    }
    func_8000B1C4();
    ext_build_all();
    stock_twin_photos();       // (2026-11-13, nmhandler: QA only)
    rivals_fill();             // (rostertags, 2026-11-21: the game's own Rival 1-3, for a match to ask -- below)
}

// ---- roster tags: who is what, who is whose rival (New feature (Ultimate No Mercy) -- rostertags, 2026-11-21) --------------
// The user (2026-11-21): the imported wrestlers carry what they are -- women, announcers, managers -- and every random pool
// (the Royal Rumble's draw, a Lumberjack match's lumberjacks, any other random fill) takes the same rule from it, for the game's
// own men, the built-in ports and the folders alike; and rivals for the imports, from each game's own data or its real feuds.
//
// CLASS: a folder's wrestler.json "class" (exe arcade_wrestlers.cpp reads it: recomp_arcade_wrestler_class); the importers
// write it from each source game's own data (Revenge: its groups 8 managers / 9 valets; WrestleMania 2000: its women's
// records, its officials' group, its non-wrestlers' height code -- wrestler.json "class_from" says which). A man with no
// "class" -- No Mercy's own, the built-in ports, an older folder -- is what his number says, exactly the rule the Rumble always
// had (rr_not_a_wrestler, logic.c): 0x7xx the women's pages (but Chyna, 0x701, one of the Superstars), 0x8xx the women's
// second page, 0x903 Earl Hebner the referee, 0x904 Jim Ross the announcer; Mr. McMahon and Shane (0x901, 0x902) owners who
// wrestle (in the Rumble's field as ever). So nothing changes for anyone without a "class".
// The numbers must match the exe's CLASS_* (arcade_wrestlers.cpp).
#define CLASS_WRESTLER  1
#define CLASS_WOMAN     2
#define CLASS_VALET     3
#define CLASS_MANAGER   4
#define CLASS_ANNOUNCER 5
#define CLASS_REFEREE   6
#define CLASS_OFFICIAL  7
#define CLASS_AUTHORITY 8
extern u32 plat_wrestler_class(u32 id);
extern int plat_rivals(u32 a, u32 b);
u32 variant_base_of(u16 id);
// what he is (CLASS_*): his folder's "class", else his number's
int roster_class(u16 id) {
    id &= 0x0FFFu;
    if (!id) return 0;
    const u32 c = plat_wrestler_class(id);
    if (c >= CLASS_WRESTLER && c <= CLASS_AUTHORITY) return (int)c;
    const u16 g = id >> 8;
    if (g == 7) return id == 0x701 ? CLASS_WRESTLER : CLASS_WOMAN;
    if (g == 8) return CLASS_WOMAN;
    if (id == 0x901 || id == 0x902) return CLASS_AUTHORITY;
    if (id == 0x903) return CLASS_REFEREE;
    if (id == 0x904) return CLASS_ANNOUNCER;
    return CLASS_WRESTLER;
}
// THE rule of every random pool: never drawn at random -- a woman, a valet, a manager, an announcer, a referee, an official.
// A wrestler or an owner who wrestles can be. (The Rumble asks it with its NO WOMEN / ANNOUNCERS on; a Lumberjack match's
// lumberjacks always. Whom the player picks is never asked.)
int roster_not_drawn(u16 id) {
    const int c = roster_class(id);
    return !(c == CLASS_WRESTLER || c == CLASS_AUTHORITY);
}
// RIVALS: either says so --
//   - a rivals.json (the wrestlers folder's, each collection's: exe rivals_read), which can also say a pair is NOT; or
//   - the game's own "Rival 1-3" (each moveset's abilities 31-33, twelve-bit Superstar IDs at bits 149, 161, 173 of the
//     packed abilities -- rel_get above), for the men whose movesets are No Mercy's own: its 64, its built-in additions (No
//     Mercy Plus's, Benoit) and a stock folder's. Not the built-in Revenge ports' (mkroster.py put their allies
//     there) nor WrestleMania 2000's two (none there): their rivals are their game's rivals.json. Not a folder wrestler's
//     (the imports have none there; rivals.json is theirs).
// Read from the roster as the menus build it (rivals_fill, at the end of roster_build_all), so a match can ask without the
// roster's records: by man (a variant is his family's head too).
#define RV_N 256
static u16 rv_id[RV_N], rv_of[RV_N][3];
static int rv_n;
static int rv_port(u16 id) { return (id >= 0x0105 && id <= 0x0119) || (id >= 0x060D && id <= 0x060F) || id == 0x030B || id == 0x060B; }
static void rv_add(u16 id, u32 moves) {
    if (!id || id >= 0x1000 || rv_port(id) || rv_n >= RV_N) return;
    for (int i = 0; i < rv_n; i++) if (rv_id[i] == id) return;
    rv_id[rv_n] = id;
    for (u32 k = 0; k < 3; k++) rv_of[rv_n][k] = (u16)rel_get(moves + 0xB8u, 149u + 12u * k);
    rv_n++;
}
static void rivals_fill(void) {
    rv_n = 0;
    const u32 moves = RD32(ROSTER_MOVES);
    if (moves) for (s32 o = 1; o < 0x54; o++) rv_add((u16)roster_ord_to_id(o), moves + (u32)(o * MOVES_SIZE));
    for (int k = 0; k < EXT_ALL; k++) rv_add(ext_id(k), (u32)(k == EXT_BENOIT ? ext_moves[k] : ext_moveset(k)));
}
static u16 rv_man(u16 id) { const u16 b = (u16)variant_base_of(id); return b ? b : id; }
static int rv_names(u16 a, u16 b) {        // (a's own Rival 1-3 name b, as a man)
    for (int i = 0; i < rv_n; i++) {
        if (rv_id[i] != a) continue;
        for (int k = 0; k < 3; k++) if (rv_of[i][k] && rv_man(rv_of[i][k]) == rv_man(b)) return 1;
    }
    return 0;
}
// are a and b rivals? (game numbers; 1 yes)
int roster_rivals(u16 a, u16 b) {
    a &= 0x0FFFu; b &= 0x0FFFu;
    if (!a || !b || rv_man(a) == rv_man(b)) return 0;
    const u16 xa[2] = { a, rv_man(a) }, xb[2] = { b, rv_man(b) };
    int not_them = 0;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            if ((i && xa[1] == xa[0]) || (j && xb[1] == xb[0])) continue;
            const int r = plat_rivals(xa[i], xb[j]);
            if (r > 0) return 1;
            if (r < 0) not_them = 1;
        }
    if (not_them) return 0;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) if (rv_names(xa[i], xb[j]) || rv_names(xb[j], xa[i])) return 1;
    return 0;
}

// ---- the Superstar Select's pages ---------------------------------------------------------------
static u32 page(int p) { return PAGES + (u32)(p * PAGE_STRIDE); }
static void page_name_set(u32 pg, const char* s) {
    for (u32 i = 0; i < 0x12; i++) RD8(pg + PAGE_NAME + i) = *s ? (u8)*s++ : 0;
}
// WCW's page: the Superstars from WCW/nWo Revenge
static int wcw_id(u16 id) { return id == 0x060D || id == 0x060E || id == 0x060F || (id >= 0x0105 && id <= 0x0119); }   // (0x010A: The Giant; 0x010B-0x0114: ports4, Sting .. Bischoff; 0x060F, 0x0115-0x0119: the second IDs, gen/revbanks.py)
static void wcw_page(void) { page_name_set(page(WCW_PAGE), "REVENGE"); }
// the game's pages' flags: the ladies, the hidden ones and Create-a-Wrestler's out of the Rumble's random draw
static void default_flags(void) {
    for (int p = 0; p < MAX_PAGES; p++)
        roster_pages_flags[p] = (p >= 5 && p <= 7) || p == 10 || p == 11 ? PAGE_NO_RANDOM : 0;
}

static void box_remove(u32 pg, int i) {
    for (int j = i; j < 8; j++) RD32(pg + 4u * (u32)j) = RD32(pg + 4u * (u32)(j + 1));
    RD32(pg + 4u * 8u) = 0;
    if (RD16(pg + PAGE_COUNT)) RD16(pg + PAGE_COUNT)--;
}
// (2026-11-13, removal) The removed men's boxes off the game's pages (the page closes up), each kept as it was -- its page,
// its place on it, his ordinal -- for the save, which gets the pages as if he had never gone (roster_pages_to_save). Kept
// afresh each time the pages are laid out from the game's twelve (default_layout).
#define RM_BOXES 64
static struct { u8 page, box, ord, put; u16 id; } rm_box[RM_BOXES];
static int rm_boxes;
static u8 rm_box_said[RM_MAX];      // (the log has been told of his box)
static void rm_boxes_off(void) {
    if (roster_removed_n() <= 0 && !variants_any_hidden()) return;     // (2026-11-14, families: and the hidden ones' boxes)
    for (int p = 0; p < N_PAGES; p++) {
        u32 pg = page(p);
        for (int i = 8; i >= 0; i--) {            // (from the end: a box taken off moves no box before it)
            if (i >= (int)RD16(pg + PAGE_COUNT)) continue;
            const u16 id = RD16(pg + 4u * (u32)i);
            const s32 o = (s32)RD16(pg + 4u * (u32)i + 2);
            if (!id || o <= 0 || o >= 0x54 || !(box_removed(id, o) || variant_wrestler_hidden(id)) || rm_boxes >= RM_BOXES) continue;
            box_remove(pg, i);
            rm_box[rm_boxes].page = (u8)p; rm_box[rm_boxes].box = (u8)i; rm_box[rm_boxes].ord = (u8)o; rm_box[rm_boxes].id = id;
            rm_boxes++;
            for (int k = 0; k < rm_n; k++)
                if ((rm_id[k] == id || (o == ORD_BENOIT && rm_id[k] == ID_BENOIT_NEW)) && !rm_box_said[k]) {   // (box 14: Benoit's own, 0x0205's man)
                    rm_box_said[k] = 1;
                    removal_s("pages: "); removal_who(rm_id[k]); removal_s(" is off the Superstar Select (his box was page "); removal_num(p + 1);
                    removal_s(", box "); removal_num(i + 1); removal_s("); the save keeps the box where it was"); removal_say();
                }
        }
    }
}
static void benoit_swap(void);       // (2026-11-19, roster: below, after the game's own twelve -- BASE)
static int benoit_into_own_box(u16 id, int k);
static int benoit_swapped(void);
// the new Superstars into the pages' free boxes (SUPERSTARS 6, 7, then any Superstars page), or out
// of them when they're off
static void place_ext(void) {
    for (int p = 0; p < DEF_PAGES; p++) {
        u32 pg = page(p);
        for (int i = 0; i < 9;) {
            s32 o = (s32)RD16(pg + 4u * (u32)i + 2);
            if (ext_ord(o) && (!ext_wanted(o - EXT_ORD0) || variant_hidden(RD16(pg + 4u * (u32)i)) || roster_id_removed(RD16(pg + 4u * (u32)i)) ||
                               variant_wrestler_hidden(RD16(pg + 4u * (u32)i)))) box_remove(pg, i);
            else i++;
        }
    }
    static const int order[] = { 8, 9, 0, 1, 2, 3, 4, 5, 6, 7 };
    for (int k = 0, n_ext = ext_total(); k < n_ext; k++) {
        if (!ext_wanted(k)) continue;
        u16 id = ext_id(k);
        if (variant_hidden(id)) continue;   // (in his base's box: variants.c)
        if (variant_wrestler_hidden(id)) continue;   // (2026-11-14, families: hidden -- families.json "hidden")
        if (roster_id_removed(id)) continue;   // (2026-11-13, removal: Benoit as 0x205 with his folder gone)
        int there = 0;
        for (int p = 0; p < DEF_PAGES && !there; p++)
            for (int i = 0; i < 9; i++) if (RD16(page(p) + 4u * (u32)i) == id) { there = 1; break; }
        if (there) continue;
        if (k == EXT_BENOIT && benoit_into_own_box(id, k)) continue;   // (2026-11-19, roster: his own box, Big Show to HBK's page)
        for (int j = wcw_id(id) ? -1 : 0; j < 10; j++) {   // (WCW's: their own page first)
            u32 pg = page(j < 0 ? WCW_PAGE : order[j]);
            int i = 0;
            while (i < 9 && RD16(pg + 4u * (u32)i)) i++;
            if (i == 9) continue;
            RD16(pg + 4u * (u32)i) = id;
            RD16(pg + 4u * (u32)i + 2) = (u16)(EXT_ORD0 + k);
            RD16(pg + PAGE_COUNT) = (u16)(RD16(pg + PAGE_COUNT) + 1);
            break;
        }
    }
    benoit_swap();
    rm_boxes_off();
}

// ---- the pages from the file ---------------------------------------------------------------------
// <program folder>\select_pages.json, as exe arcade_pages.cpp packs it ('P' 'G' 1 <pages> <flags: 1 hide the
// rest> 0 0 0, then each page: name[16], flags, count, count x big-endian ID), laid out with the Superstars there
// are: anyone off the roster (the operator's, Benoit's box) or not a Superstar at all left out, a page with
// nobody left on it not made; then (unless "unlisted": "hide") everyone the file leaves out, on pages of their
// own at the end (MORE, MORE 2 ...: the game's twelve's, in their order, then the new Superstars, then Create-a-
// Wrestler's boxes -- shown once made, as on EDIT 1 / 2 -- so it's those the 32 pages leave out first). Nobody
// on any page: the game's own pages. The game's twelve (the ROM's, or the save's) are kept as they were: the
// save gets those.
#define PACK_MAX (8 + MAX_PAGES * 36)
static u8 jp_pack[PACK_MAX] __attribute__((aligned(8)));
static int jp_len = -1;              // the pack's size (-1: not asked yet; 0: none)
static int jp_on;                    // the pages are the file's
static int pages_built;              // (the game has made its pages: at boot)
static u8 base_tab[N_PAGES * PAGE_STRIDE] __attribute__((aligned(8)));   // the game's twelve
#define BASE ((u32)base_tab)

// (2026-11-19, roster -- the user: Chris Benoit back in his own box on the Superstar Select, Big Show in the box Benoit had
// among the new Superstars; Story Mode as before, bigshow.c.) With Big Show in Benoit's place (ordinal 14) and Benoit among the
// new ones (ext 4, 0x205), the two boxes change places: Benoit's ext box goes where ordinal 14's box is on the game's own twelve
// (BASE: the ROM's pages, or the save's), and ordinal 14 -- Big Show -- into the first free box of Shawn Michaels' page (the
// user, round 2: the page with HBK and Ken Shamrock, HIDDEN 2, has a free sixth box; the page keeps its name and its flags),
// Benoit's box among the new ones closing up; with no room there, into the box place_ext gave Benoit. Done once per layout:
// after it, ordinal 14's box is no longer at that place, so a second place_ext (the roster's options changing) leaves them be.
// The save gets the game's own pages back: roster_pages_to_save writes ordinal 14 where Benoit's box is and leaves Big Show's
// out, as it leaves every new Superstar out (benoit_swapped).
static int box_at(int p, int i) { return (int)RD16(page(p) + 4u * (u32)i + 2); }
static int benoit_swapped(void) {
    if (!bigshow_is_on()) return 0;
    for (int p = 0; p < DEF_PAGES; p++)
        for (int i = 0; i < 9 && i < (int)RD16(page(p) + PAGE_COUNT); i++) if (box_at(p, i) == EXT_ORD0 + EXT_BENOIT) return 1;
    return 0;
}
// Shawn Michaels' page (0x0009) with a free box, not page `not_p`: -1 none
static int hbk_page(int not_p) {
    for (int p = 0; p < DEF_PAGES; p++)
        for (int i = 0; i < 9 && i < (int)RD16(page(p) + PAGE_COUNT); i++)
            if (RD16(page(p) + 4u * (u32)i) == 0x0009u) return RD16(page(p) + PAGE_COUNT) < 9 && p != not_p ? p : -1;
    return -1;
}
// Ordinal 14's box at its own place (BASE), Big Show's: -> page << 8 | box, -1 not there
static int bigshow_own_box(void) {
    for (int p = 0; p < DEF_PAGES; p++)
        for (int i = 0; i < 9 && i < (int)RD16(page(p) + PAGE_COUNT); i++)
            if (box_at(p, i) == ORD_BENOIT)
                return p < N_PAGES && (int)RD16(BASE + (u32)p * PAGE_STRIDE + 4u * (u32)i + 2) == ORD_BENOIT ? p << 8 | i : -1;
    return -1;
}
// (place_ext, as it places Benoit -- ext 4 -- before the free boxes are taken by the ones after him) Benoit straight into
// ordinal 14's own box, Big Show into the first free box of Shawn Michaels' page. 0: not done (Big Show off, his box moved
// already, or no room on that page -- then Benoit takes a free box as before and benoit_swap below does the rest).
static int benoit_into_own_box(u16 id, int k) {
    if (!bigshow_is_on()) return 0;
    const int b = bigshow_own_box(), hp = hbk_page(-1);
    if (b < 0 || hp < 0) return 0;
    const u32 a = page(b >> 8) + 4u * (u32)(b & 0xFF), t = RD32(a);
    RD16(a) = id;
    RD16(a + 2) = (u16)(EXT_ORD0 + k);
    const u32 m = RD16(page(hp) + PAGE_COUNT);
    RD32(page(hp) + 4u * m) = t;
    RD16(page(hp) + PAGE_COUNT) = (u16)(m + 1);
    return 1;
}
static void benoit_swap(void) {
    if (!bigshow_is_on()) return;
    int bp = -1, bi = 0, sp = -1, si = 0;
    for (int p = 0; p < DEF_PAGES; p++)
        for (int i = 0; i < 9 && i < (int)RD16(page(p) + PAGE_COUNT); i++) {
            if (box_at(p, i) == EXT_ORD0 + EXT_BENOIT && bp < 0) { bp = p; bi = i; }
            if (box_at(p, i) == ORD_BENOIT && sp < 0) { sp = p; si = i; }
        }
    if (bp < 0 || sp < 0 || sp >= N_PAGES) return;
    if ((int)RD16(BASE + (u32)sp * PAGE_STRIDE + 4u * (u32)si + 2) != ORD_BENOIT) return;   // (Big Show's box not at Benoit's own place: done)
    const u32 a = page(sp) + 4u * (u32)si, b = page(bp) + 4u * (u32)bi, t = RD32(a);
    const int hp = hbk_page(bp);     // (Shawn Michaels' page with a free box -- not Benoit's new one)
    RD32(a) = RD32(b);
    if (hp < 0) { RD32(b) = t; return; }          // (no room there: Big Show in the box Benoit had)
    box_remove(page(bp), bi);
    const u32 m = RD16(page(hp) + PAGE_COUNT);
    RD32(page(hp) + 4u * m) = t;
    RD16(page(hp) + PAGE_COUNT) = (u16)(m + 1);
}

static int jp_have(void) {
    if (jp_len < 0) {
        jp_len = plat_pages_pack(jp_pack, PACK_MAX);
        if (jp_len < 8 || jp_pack[0] != 'P' || jp_pack[1] != 'G' || jp_pack[2] != 1 || jp_pack[3] == 0) jp_len = 0;
    }
    return jp_len > 0;
}
int roster_pages_from_file(void) { return jp_have(); }     // (2026-11-14, families: variants.c, a family's kept place)
// a Superstar who can have a box: on the roster as it is, not an edit buffer, not Benoit's box while it's off, not
// another's costume variant (variants.c: he's in his base's box -- a page that named him closes up)
static s32 box_ord(u16 id) {
    {   // (2026-11-14, families) A family's place whose first wrestler is removed or hidden: the box is the next one's there
        // (box_heir: the ID the box gets) -- his ordinal, though as a variant he has no box of his own.
        const u16 e = variant_slot_heir(id);
        if (e) { const s32 o = roster_id_to_ord(e); return o > 0 && o <= 0xFF && !(o >= 0x54 && o < EXT_ORD0) ? o : 0; }
    }
    if (variant_wrestler_hidden(id)) return 0;      // (2026-11-14, families: hidden -- the page closes up)
    if (id == 0 || id >= 0x1000 || (id & 0xF00u) == 0xB00u || variant_hidden(id)) return 0;
    s32 o = roster_id_to_ord(id);
    if (o <= 0 || o > 0xFF || (o >= 0x54 && o < EXT_ORD0)) return 0;
    if (box_removed(id, o)) {               // (2026-11-13, removal: his folder gone -- a page that named him closes up)
        for (int k = 0; k < rm_n; k++)
            if ((rm_id[k] == id || (o == ORD_BENOIT && rm_id[k] == ID_BENOIT_NEW)) && !rm_box_said[k]) {   // (box 14: Benoit's own, 0x0205's man)
                rm_box_said[k] = 1;
                removal_s("pages: "); removal_who(rm_id[k]); removal_s(" is off the Superstar Select (select_pages.json's pages close up;"
                                                                   " the save's pages are the game's twelve, kept as they were)");
                removal_say();
            }
        return 0;
    }
    return o;
}
// (2026-11-19, roster) The file's CHRIS BENOIT (0x205: his number among the new Superstars, while Big Show has his ordinal) is
// Benoit wherever he is: with Big Show off, his own ordinal 14's number, 0x201 -- so his box is where the file puts him either
// way, and BIG SHOW (0x201) further on, then Benoit too, isn't placed twice (placed[]).
static u16 file_id(u16 id) { return id == ID_BENOIT_NEW && !bigshow_is_on() ? (u16)ID_BENOIT : id; }
static void box_put(u32 pg, u16 id, s32 o) {
    u32 m = RD16(pg + PAGE_COUNT);
    RD16(pg + 4u * m) = id;
    RD16(pg + 4u * m + 2) = (u16)o;
    RD16(pg + PAGE_COUNT) = (u16)(m + 1);
}
static int json_cut;                 // (json_layout ran out of the select's 32 pages: someone left out -- the window then)
// (romsets 2026-11-24, the user: "the select pages for WM2000 and Revenge should be grouped correctly, with as many wrestler
// thumbnails as a page allows ... like auto arrange apps on a phone") One page of select_pages.json is one GROUP: the exe
// sends it as pages of nine, the ones after its first marked PAGE_CONT (arcade_pages.cpp). Whoever has no box here -- a
// wrestler merged into another's box (MERGE SAME WRESTLERS, a family), hidden, removed, a built-in whose source game isn't
// there -- used to leave his page short (WM 2000's pages with 2 or 3 boxes once its men were merged into No Mercy's). Now
// a group's boxes flow on in order, nine to a page, and a page left with nobody isn't made: NAME, NAME 2, NAME 3 ... as many as
// its boxes fill. Groups keep their order and their own pages (a group never fills another's page).
#define PAGE_CONT 0x80u              // (the pack's page flags: a page of the same group as the one before it)
static void group_page_name(char* out, const char* base, int part) {   // NAME, then "NAME 2" ... (the exe's rule: 15 letters)
    int n = 0;
    while (n < 16 && base[n]) n++;
    if (part <= 1) { for (int i = 0; i < 17; i++) out[i] = i < n ? base[i] : 0; return; }
    char suf[5]; int s = 0;
    suf[s++] = ' ';
    if (part >= 10) suf[s++] = (char)('0' + part / 10 % 10);
    suf[s++] = (char)('0' + part % 10);
    int keep = n < 15 - s ? n : 15 - s;
    while (keep > 0 && base[keep - 1] == ' ') keep--;
    int o = 0;
    for (int i = 0; i < keep; i++) out[o++] = base[i];
    for (int i = 0; i < s; i++) out[o++] = suf[i];
    while (o < 17) out[o++] = 0;
}
static int json_layout(void) {
    static u8 placed[256];
    json_cut = 0;
    for (int i = 0; i < 256; i++) placed[i] = 0;
    for (u32 i = 0; i < MAX_PAGES * PAGE_STRIDE; i += 4) RD32(PAGES + i) = 0;
    for (int p = 0; p < MAX_PAGES; p++) roster_pages_flags[p] = 0;
    int n = 0, at = 8;
    char base[17] = { 0 };
    u8 gflags = 0;
    int part = 0;                         // (pages of the group made so far)
    u32 pg = 0;                           // (the group's page being filled; 0: none yet)
    for (int k = 0; k < jp_pack[3] && at + 18 <= jp_len; k++) {
        const u8* h = jp_pack + at;
        int cnt = h[17];
        at += 18;
        if (!(h[16] & PAGE_CONT) || k == 0) {   // (a new group: its name and flags)
            for (int i = 0; i < 16; i++) base[i] = (char)h[i];
            base[16] = 0;
            gflags = h[16] & PAGE_NO_RANDOM;
            part = 0; pg = 0;
        }
        for (int i = 0; i < cnt && at + 2 <= jp_len; i++, at += 2) {
            u16 id = file_id((u16)(jp_pack[at] << 8 | jp_pack[at + 1]));
            s32 o = box_ord(id);
            if (!o || placed[o]) continue;
            if (!pg || RD16(pg + PAGE_COUNT) >= 9) {
                if (n >= MAX_PAGES) { json_cut = 1; continue; }   // (seatsroot: more of the file's pages than fit)
                pg = page(n);
                char name[17];
                group_page_name(name, base, ++part);
                page_name_set(pg, name);
                roster_pages_flags[n++] = gflags;
            }
            placed[o] = 1;
            box_put(pg, box_heir(id), o);
        }
    }
    if (!(jp_pack[4] & 1)) {
        u32 pg = 0;
        int more = 0;
        const int all_boxes = N_PAGES * 9 + ext_total();
        for (int src = 0; src < 2 * all_boxes; src++) {   // (the Superstars, then Create-a-Wrestler's boxes)
            int caw_pass = src >= all_boxes;
            u16 id;
            int at_ = caw_pass ? src - all_boxes : src;
            if (at_ < N_PAGES * 9) {
                u32 b = BASE + (u32)(at_ / 9) * PAGE_STRIDE;
                if (at_ % 9 >= (int)RD16(b + PAGE_COUNT)) continue;
                id = RD16(b + 4u * (u32)(at_ % 9));
            } else {
                int k = at_ - N_PAGES * 9;
                if (!ext_wanted(k)) continue;
                id = ext_id(k);
            }
            if (((id & 0xF00u) == 0xA00u) != caw_pass) continue;
            s32 o = box_ord(id);
            if (!o || placed[o]) continue;
            if (!pg || RD16(pg + PAGE_COUNT) >= 9) {
                if (n >= MAX_PAGES) { json_cut = 1; break; }
                pg = page(n);
                roster_pages_flags[n++] = PAGE_NO_RANDOM;
                char name[8] = "MORE";
                if (++more > 1) {
                    int c = 4;
                    name[c++] = ' ';
                    if (more >= 10) name[c++] = (char)('0' + more / 10);
                    name[c++] = (char)('0' + more % 10);
                    name[c] = 0;
                }
                page_name_set(pg, name);
            }
            placed[o] = 1;
            box_put(pg, box_heir(id), o);
        }
    }
    if (!n) return 0;
    roster_pages_n = n;
    return 1;
}
// ---- the whole layout, and the select's pages a window on it (2026-11-13, seatsroot; TEMPORARY) ----------------------
// json_layout's pages (the file's, then MORE, MORE 2 ... for everyone it leaves out) can be any number: past the seats
// (staging), and past the 32 pages the select keeps (a page in five bits: MAX_PAGES), which json_layout cut -- the
// Create-a-Wrestlers first, then whoever came last. Each box on the select's table also needs its wrestler seated. So when
// the whole layout doesn't fit, the table holds ring_w pages of it at a time, round the one shown: 7 when staging (7 x 9
// boxes seated at once), 31 when every folder has a seat anyway. The game steps L / R through its table and wraps round
// it; slot j holds page g + d of the whole layout, d = (j - shown + half) mod ring_w - half, so the slot the game steps
// into always holds the next page (or the one before) of the whole layout, and wrapping at the table's ends goes on
// through it. Each time the page shown changes, the slots furthest behind are filled with the pages coming up
// (ring_follow); the page shown is never written while it's shown. Its boxes' wrestlers take seats as they're written.
// When the whole layout fits (32 pages or fewer, no staging), json_layout lays it out as it always did.
//   The whole layout is made again whenever a page is wanted (glayout_run: one pass over the file's pages and everyone,
// no list of them kept -- the mod's memory has no room for a list of every wrestler, and there's no count to size it by).
// What stays the game's here: the page list (func_801378B8) shows the ring_w pages there are now; the game's own Rumble
// field (func_8013486C) and the first entrant drawn at random go through them; the page editor (ovl_d) keeps its twelve.
// Root fix: Stage 2's native records, then the select's page index wide (Stage 3), when the table can hold every page.
#define RING_W_STAGING 7         // (staging: 7 x 9 boxes seated at once, of the 112 seats -- the rest for the match, the Rumble's
                                  //  field, the champions)
#define RING_W_ALL (MAX_PAGES - 1) // (every folder seated: as many as the select can keep, less one -- an odd number, the shown in
                                  //  the middle)
static int ring_w = RING_W_STAGING, ring_half = RING_W_STAGING / 2;
static int ring_on;                 // the table is a window (staging with the file's pages)
static int ring_total;              // pages in the whole layout (glayout_run counts them)
static int ring_page[MAX_PAGES];    // the page of the whole layout each slot holds
static int ring_shown = -1;         // the slot shown when the window was last filled
static u8 gp_page[PAGE_STRIDE] __attribute__((aligned(8)));   // a page as glayout_run makes it (IDs, no ordinals yet)
static u8 gp_flags;
static u16 fp_folder[MAX_PAGES * 9];   // folder wrestlers the file's pages have placed (none twice)
static int fp_n;
static int fp_has(u16 id) { for (int i = 0; i < fp_n; i++) if (fp_folder[i] == id) return 1; return 0; }
// A wrestler who can have a box, without taking a seat: a folder wrestler not hidden as another's variant, or box_ord's.
static int box_ok(u16 id) {
    { const u16 e = variant_slot_heir(id); if (e) return folder_id_of(e) >= 0 || box_ord(id) != 0; }   // (2026-11-14, families)
    if (variant_wrestler_hidden(id)) return 0;
    if (folder_id_of(id) >= 0) return !variant_hidden(id);
    return box_ord(id) != 0;
}
static void gp_start(const char* name, u8 flags) {
    for (u32 i = 0; i < PAGE_STRIDE; i += 4) *(u32*)&gp_page[i] = 0;
    page_name_set((u32)gp_page, name);
    gp_flags = flags;
}
static int gp_count(void) { return *(u16*)&gp_page[PAGE_COUNT]; }
static void gp_put(u16 id) { int m = gp_count(); *(u16*)&gp_page[4 * m] = id; *(u16*)&gp_page[PAGE_COUNT] = (u16)(m + 1); }
// The whole layout, page by page, each page made in gp_page and handed to gl_emit (gl_mode: what's done with it).
// -> how many pages there are.
static int gl_mode;                 // 0 count them; 1 the page ring_want_k into the table (ring_put); 2 each box to the Rumble
static int ring_want_k = -1;
static void ring_put(int k);
void arcade_rr_offer(u16 id);       // (logic.c: the Rumble's draw from every page)
static void gl_emit(int k);
static int glayout_run(void) {
    static u8 placed[256];
    for (int i = 0; i < 256; i++) placed[i] = 0;
    fp_n = 0;
    int n = 0, at = 8;
    // the file's pages, as json_layout lays them out: a group's boxes flowing on nine to a page (romsets: PAGE_CONT)
    char base[17] = { 0 }, name[17];
    u8 gflags = 0;
    int part = 0, open = 0;
    for (int k = 0; k < jp_pack[3] && at + 18 <= jp_len; k++) {
        const u8* h = jp_pack + at;
        int cnt = h[17];
        at += 18;
        if (!(h[16] & PAGE_CONT) || k == 0) {   // (a new group: the page of the one before, if any, out first)
            if (open && gp_count()) { gl_emit(n); n++; }
            open = 0;
            for (int i = 0; i < 16; i++) base[i] = (char)h[i];
            base[16] = 0;
            gflags = h[16] & PAGE_NO_RANDOM;
            part = 0;
        }
        for (int i = 0; i < cnt && at + 2 <= jp_len; i++, at += 2) {
            u16 id = file_id((u16)(jp_pack[at] << 8 | jp_pack[at + 1]));
            if (!box_ok(id)) continue;
            const u16 raw = id;
            id = box_heir(raw);           // (2026-11-14, families)
            if (folder_id_of(id) >= 0) { if (fp_has(id) || fp_n >= MAX_PAGES * 9) continue; }
            else { s32 o = box_ord(raw); if (placed[o]) continue; }
            if (open && gp_count() >= 9) { gl_emit(n); n++; open = 0; }
            if (!open) { group_page_name(name, base, ++part); gp_start(name, gflags); open = 1; }
            if (folder_id_of(id) >= 0) fp_folder[fp_n++] = id;
            else placed[box_ord(raw)] = 1;
            gp_put(id);
        }
    }
    if (open && gp_count()) { gl_emit(n); n++; }
    if (!(jp_pack[4] & 1)) {        // MORE, MORE 2 ...: the game's twelve's, the built-in ports, every folder; then CAW's
        int more = 0, open = 0;
        const int base_n = N_PAGES * 9, all_boxes = base_n + EXT_ALL + folder_total;
        for (int src = 0; src < 2 * all_boxes; src++) {
            const int caw_pass = src >= all_boxes, at_ = caw_pass ? src - all_boxes : src;
            u16 id;
            if (at_ < base_n) {
                u32 b = BASE + (u32)(at_ / 9) * PAGE_STRIDE;
                if (at_ % 9 >= (int)RD16(b + PAGE_COUNT)) continue;
                id = RD16(b + 4u * (u32)(at_ % 9));
            } else if (at_ < base_n + EXT_ALL) {
                const int k = at_ - base_n;
                if (!ext_wanted(k)) continue;
                id = ext_id(k);
            } else id = (u16)plat_wrestler_id(at_ - base_n - EXT_ALL);
            if (((id & 0xF00u) == 0xA00u) != caw_pass) continue;
            if (!box_ok(id)) continue;
            const u16 raw = id;
            id = box_heir(raw);           // (2026-11-14, families)
            if (folder_id_of(id) >= 0) { if (fp_has(id)) continue; }
            else { s32 o = box_ord(raw); if (placed[o]) continue; placed[o] = 1; }
            if (!open || gp_count() >= 9) {
                if (open) { gl_emit(n); n++; }
                char name[12] = "MORE";
                if (++more > 1) {          // (MORE 2 ... MORE 1000 ...: any number of digits)
                    char d[8]; int nd = 0;
                    for (int v = more; v; v /= 10) d[nd++] = (char)('0' + v % 10);
                    int c = 4;
                    name[c++] = ' ';
                    while (nd) name[c++] = d[--nd];
                    name[c] = 0;
                }
                gp_start(name, PAGE_NO_RANDOM);
                open = 1;
            }
            gp_put(id);
        }
        if (open && gp_count()) { gl_emit(n); n++; }
    }
    return n;
}
// gp_page into the table's slot j: each box's ordinal, a folder wrestler seated now (a box nobody can be seated for is
// left out, and the log says so: seat_take).
static int ring_slot_now;
static void ring_put(int k) {
    const u32 pg = page(ring_slot_now);
    for (u32 i = 0; i < PAGE_STRIDE; i += 4) RD32(pg + i) = 0;
    for (u32 i = 0; i < 0x12; i++) RD8(pg + PAGE_NAME + i) = gp_page[PAGE_NAME + i];
    for (int i = 0; i < gp_count(); i++) {
        const u16 id = *(u16*)&gp_page[4 * i];
        const int f = folder_id_of(id);
        s32 o;
        if (f >= 0) { const int s = seat_take(f); if (s < 0) continue; o = EXT_ORD0 + EXT_ALL + s; }
        else o = box_ord(id);
        if (!o) o = roster_id_to_ord(id);     // (2026-11-14, families: a family's next one at a removed / hidden first one's place)
        box_put(pg, id, o);
    }
    roster_pages_flags[ring_slot_now] = gp_flags;
    ring_page[ring_slot_now] = k;
}
static void gl_emit(int k) {
    if (gl_mode == 1 && k == ring_want_k) ring_put(k);
    else if (gl_mode == 2) for (int i = 0; i < gp_count(); i++) arcade_rr_offer(*(u16*)&gp_page[4 * i]);
}
static void ring_get(int j, int k) { ring_slot_now = j; ring_want_k = k; gl_mode = 1; glayout_run(); gl_mode = 0; }
// Every box of the whole layout to the Rumble's draw (logic.c rr_draw_field_from). 0: the select's table is the whole
// layout (no window): the draw reads it, as it always did.
int roster_boxes_to_rumble(void) {
    if (!ring_on) return 0;
    gl_mode = 2; glayout_run(); gl_mode = 0;
    return 1;
}
// Slot j: the page it should hold with the page shown in slot `shown` being page g.
static int ring_page_for(int j, int shown, int g) {
    const int d = (j - shown + ring_w + ring_half) % ring_w - ring_half;
    return ((g + d) % ring_total + ring_total) % ring_total;
}
static void ring_fill(int shown, int g) {
    for (int j = 0; j < ring_w; j++) {
        const int k = ring_page_for(j, shown, g);
        if (ring_page[j] == k) continue;
        ring_get(j, k);
    }
    ring_shown = shown;
}
// The file's pages and everyone as a window (layout's staging way). 0: not one page.
static int ring_layout(void) {
    gl_mode = 0;
    ring_total = glayout_run();
    if (!ring_total) return 0;
    for (u32 i = 0; i < MAX_PAGES * PAGE_STRIDE; i += 4) RD32(PAGES + i) = 0;
    for (int p = 0; p < MAX_PAGES; p++) roster_pages_flags[p] = 0;
    for (int j = 0; j < MAX_PAGES; j++) ring_page[j] = -1;
    ring_w = staging ? RING_W_STAGING : RING_W_ALL;
    ring_half = ring_w / 2;
    if (ring_total <= ring_w) {          // (few pages: all of them, in order)
        for (int k = 0; k < ring_total; k++) ring_get(k, k);
        roster_pages_n = ring_total;
        ring_on = 0;
        return 1;
    }
    roster_pages_n = ring_w;
    ring_on = 1;
    ring_fill(0, 0);
    plat_seat_note(5, ring_w, ring_total, 0);   // ("the pages: a window of ring_w on ring_total")
    return 1;
}
// Each frame in the menus: the page shown moved to another slot -- the window follows it.
static void ring_follow(void) {
    if (!ring_on) return;
    const s32 shown = RDS16(SEL_PAGE);
    if (shown < 0 || shown >= ring_w || shown == ring_shown) return;
    const int g = ring_page[shown];
    if (g < 0) return;
    ring_fill(shown, g);
}

// the game's own pages: its twelve as they were made, WCW's, the new Superstars in free boxes
static void default_layout(void) {
    func_80036A30(PAGES, MAX_PAGES * PAGE_STRIDE);
    for (u32 i = 0; i < N_PAGES * PAGE_STRIDE; i += 4) RD32(PAGES + i) = RD32(BASE + i);
    wcw_page();
    roster_pages_n = DEF_PAGES;
    default_flags();
    jp_on = 0;
    rm_boxes = 0;                    // (2026-11-13, removal: the game's twelve again, the removed men's boxes taken off anew)
    place_ext();
}
// the pages laid out: the file's if it lays anyone out, else the game's own (at boot, and when the roster changes)
static void layout(void) {
    ring_on = 0; ring_shown = -1;
    if (roster_staging() && jp_have() && ring_layout()) jp_on = 1;      // (2026-11-13, seatsroot: past the seats, a window on them all)
    else if (jp_have() && json_layout()) { jp_on = 1; if (json_cut) ring_layout(); }   // (seatsroot: more than 32 pages: a window too)
    else default_layout();
    pages_built = 1;
    if (RDS16(SEL_PAGE) >= roster_pages_n) RD16(SEL_PAGE) = 0;   // (the select's page: one there is)
    if (roster_removed_qa() & 2)                    // (2026-11-13, removal, QA: the pages as laid out, every box)
        for (int p = 0; p < roster_pages_n; p++) {
            removal_s("qa pages "); removal_num(p + 1); removal_s(":");
            for (int i = 0; i < (int)RD16(page(p) + PAGE_COUNT) && i < 9; i++) { removal_s(" "); removal_who(RD16(page(p) + 4u * (u32)i)); }
            removal_say();
        }
    static int rm_said_once;                        // (2026-11-13, removal: each removed man's variants and stand-in, once)
    u16 roster_stand_in(u32 id, int nth);
    if (roster_removed_n() > 0 && !rm_said_once && roster_stand_in(rm_id[0], 0)) {   // (once the game's own pages are there)
        rm_said_once = 1;
        for (int k = 0; k < rm_n; k++) {
            int variants_removed_with(u16 base);
            if (!variants_removed_with(rm_id[k])) { removal_s("variants: "); removal_who(rm_id[k]); removal_s(" heads no family (variants.c): nothing else goes with him"); removal_say(); }
            removal_s("stand-in: "); removal_who(rm_id[k]); removal_s(" -> "); removal_who(roster_stand_in(rm_id[k], 0));
            removal_s(" (the man after him on his select page), wherever something still asks for him"); removal_say();
        }
    }
}
// (2026-11-13, removal) Who stands in for removed man id -- in Story Mode's own tables (bigshow.c), a story's roles and parts
// (story.c), a preset (save.c), a demo (logic.c): the men after him
// in his box's page of the game's own twelve -- the same division, the way the game groups
// them -- then the pages after it, passing over anyone removed; nth: the nth such (0 the first), for a list that has the
// first already. 0 if there's nobody (or the pages aren't made yet).
u16 roster_stand_in(u32 id, int nth) {
    if (!pages_built) return 0;
    s32 o = roster_id_to_ord(id);
    if (id == ID_BENOIT_NEW) o = ORD_BENOIT;
    int p0 = -1, b0 = 0;
    for (int p = 0; p < N_PAGES && p0 < 0; p++)
        for (int i = 0; i < (int)RD16(BASE + (u32)p * PAGE_STRIDE + PAGE_COUNT) && i < 9; i++)
            if ((s32)RD16(BASE + (u32)p * PAGE_STRIDE + 4u * (u32)i + 2) == o) { p0 = p; b0 = i; break; }
    if (p0 < 0) { p0 = 0; b0 = -1; }
    for (int s = 0; s < N_PAGES; s++) {
        const u32 pg = BASE + (u32)((p0 + s) % N_PAGES) * PAGE_STRIDE;
        const int n = (int)RD16(pg + PAGE_COUNT) < 9 ? (int)RD16(pg + PAGE_COUNT) : 9;
        for (int j = 0; j < n; j++) {
            const int i = s == 0 ? (b0 + 1 + j) % n : j;
            const u16 c = RD16(pg + 4u * (u32)i);
            const s32 co = (s32)RD16(pg + 4u * (u32)i + 2);
            if (!c || co == o || co <= 0 || co > 64 || box_removed(c, co)) continue;   // (the game's own Superstars)
            if (nth-- == 0) return c;
        }
    }
    return 0;
}
static void base_keep(void) { for (u32 i = 0; i < N_PAGES * PAGE_STRIDE; i += 4) RD32(BASE + i) = RD32(PAGES + i); }

// func_8000A6DC(id) / func_8000A758(id): a Superstar's page and box (page << 8 | box; -1: none), from the first
// page on / from the last back; func_8000A7D8(page, box): the box (the game's empty one if there's none)
s32 roster_page_of(u32 id, int back) {
    for (int k = 0; k < roster_pages_n; k++) {
        int p = back ? roster_pages_n - 1 - k : k;
        u32 pg = page(p);
        for (int i = 0; i < (int)RD16(pg + PAGE_COUNT) && i < 9; i++)
            if (RD16(pg + 4u * (u32)i) == (u16)id) return p << 8 | i;
    }
    return -1;
}
u32 roster_page_box(u32 p, s32 i) {
    if (p >= (u32)roster_pages_n || i < 0 || i >= (s32)RD16(page((int)p) + PAGE_COUNT) || i >= 9) return 0x800459A0u;
    return page((int)p) + 4u * (u32)i;
}
// func_8000B004: the pages from the ROM's lists, and the ten event names
void roster_build_pages(void) {
    func_80036A30(PAGES, MAX_PAGES * PAGE_STRIDE);
    for (int p = 0; p < N_PAGES; p++) {
        u32 pg = page(p), list = RD32(PAGE_LISTS + 8u * (u32)p);
        func_8000AFD4(pg + PAGE_NAME, RD32(PAGE_LISTS + 8u * (u32)p + 4));
        int i = 0;
        for (; i < 9; i++) {
            u32 o = RD8(list + (u32)i);
            if (!o) break;
            u32 s = table_struct((s32)o, 0);
            RD16(pg + 4u * (u32)i + 2) = RD16(s + 2);
            RD16(pg + 4u * (u32)i) = RD16(s);
        }
        RD16(pg + PAGE_COUNT) = (u16)i;
        for (; i < 9; i++) RD32(pg + 4u * (u32)i) = 0;
    }
    u32 names = RD32(ROSTER_NAMES);
    for (u32 i = 0; i < 10; i++) {
        u32 n = RD16(0x8004709Cu + 2u * i);
        func_80007F94(0x80069798u + 17u * i, names + RD16(names + 2u * n));
    }
    base_keep();
    layout();
}
// func_800E30DC: the pages from the save
void roster_pages_from_save(void) {
    u32 sv = RD32(SAVE_IMAGE) + SAVE_PAGES;
    func_80036A30(PAGES, MAX_PAGES * PAGE_STRIDE);
    for (int p = 0; p < N_PAGES; p++, sv += 0x17) {
        u32 pg = page(p);
        func_80008468(pg + PAGE_NAME, sv, 0x10);
        RD16(pg + PAGE_COUNT) = 0;
        for (u32 i = 0; i < 9; i++) {
            u32 o = RD8(sv + 0xE + i), e = pg + 4u * i;
            if (o) {
                RD16(e + 2) = (u16)o;
                RD16(e) = (u16)roster_ord_to_id((s32)o);
                RD16(pg + PAGE_COUNT) = (u16)(RD16(pg + PAGE_COUNT) + 1);
            } else RD32(e) = 0;
        }
    }
    base_keep();
    layout();
    { void save_removal_report(void); save_removal_report(); }   // (2026-11-13, removal: what the save names of a removed man)
    { void save_sidecar_loaded(void); save_sidecar_loaded(); }   // (the save was read: its sidecar next frame, save.c -- 2026-11-13)
}
// (2026-11-13, removal) Page p's removed boxes into the save's list at sv, in the places they had: each whose place has come
// (its box number no more than the list's length so far), or with rest set every one left.
static void rm_boxes_back(int p, u32 sv, u32* n, int rest) {
    for (u32 b = 0; b < 9; b++)                    // (in their places' order)
        for (int k = 0; k < rm_boxes; k++) {
            if (rm_box[k].page != p || rm_box[k].box != b || rm_box[k].put) continue;
            if ((!rest && b > *n) || *n >= 9) return;
            RD8(sv + 0xE + (*n)++) = rm_box[k].ord;
            rm_box[k].put = 1;                     // (not twice in this write)
        }
}
// func_800E3020: the pages into the save -- the game's Superstars only, in their order (with the file's pages
// up, the game's twelve as they were: the save stays the game's)
void roster_pages_to_save(void) {
    u32 sv = RD32(SAVE_IMAGE) + SAVE_PAGES;
    func_80036A30(sv, 0x114);
    for (int k = 0; k < rm_boxes; k++) rm_box[k].put = 0;
    const int sw = !jp_on && benoit_swapped();     // (2026-11-19, roster: Benoit in his own box, Big Show in the one he had)
    for (int p = 0; p < N_PAGES; p++, sv += 0x17) {
        u32 pg = jp_on ? BASE + (u32)p * PAGE_STRIDE : page(p);
        func_800083BC(sv, pg + PAGE_NAME, 0x10);
        u32 n = 0;
        for (u32 i = 0; i < 9; i++) {
            if (!jp_on) rm_boxes_back(p, sv, &n, 0);   // (2026-11-13, removal: a removed man's box where it was)
            u32 o = RD16(pg + 4u * i + 2) & 0xFFu;
            if (sw && o == ORD_BENOIT) continue;                         // (Big Show's box: a new Superstar's, not the save's)
            if (sw && o == (u32)(EXT_ORD0 + EXT_BENOIT)) o = ORD_BENOIT;  // (Benoit's box: the game's ordinal 14, Benoit as it has him)
            if (!o || ext_ord((s32)o)) continue;
            if (n < 9) RD8(sv + 0xE + n++) = (u8)o;
        }
        if (!jp_on) rm_boxes_back(p, sv, &n, 1);
    }
}

// ---- the save: one Superstar's data -------------------------------------------------------------
// func_800E31C4(id): a Superstar's records (and a Create-a-Wrestler's moveset) into the save image,
// and the save written; an edit buffer's into the edit area instead.
static void save_one(u32 id, int write) {
    s32 ord = roster_id_to_ord(id);
    if (ext_index(ord) >= 0 || ext_ord(ord)) return;   // not ours to save
    if (ord < 0x54) {
        u32 dst = RD32(SAVE_IMAGE) + (u32)(ord * 0x114 - 0x114);
        for (s32 c = 0; c < 4; c++) func_800E299C(dst + 0x44u * (u32)c, roster_record(ord, c));
        if (ord >= 0x41) func_800E2AC0(RD32(SAVE_IMAGE) + (u32)(ord * 0xDC + 0x2314), roster_moves(roster_id_to_ord(id)));
        if (write) func_80004EE8();
    } else {
        u32 buf = RD32(0x80104BD0u);
        u32 dst = buf + (u32)(ord * 0x114 - 0x5A90);
        for (s32 c = 0; c < 4; c++) func_800E299C(dst + 0x44u * (u32)c, roster_record(ord, c));
        func_800E2AC0(buf + (u32)(ord * 0xDC - 0x34C8), roster_moves(roster_id_to_ord(id)));
    }
}
void roster_save_one(u32 id) { save_one(id, 1); }         // func_800E31C4
// func_800E3330: the same, the save not written; Superstars (below 0x41) without a moveset
void roster_pack_one(u32 id) {
    s32 ord = roster_id_to_ord(id);
    if (ord >= 0x54 || ord >= 0x41) { save_one(id, 0); return; }
    if (ext_index(ord) >= 0) return;
    u32 dst = RD32(SAVE_IMAGE) + (u32)(ord * 0x114 - 0x114);
    for (s32 c = 0; c < 4; c++) func_800E299C(dst + 0x44u * (u32)c, roster_record(ord, c));
}
// func_800E5A44(id): the special slot's saved data (83: an edit) copied over a Superstar's in the
// save image, the roster rebuilt from it and the Superstar saved
void roster_copy_saved(u32 id) {
    s32 ord = roster_id_to_ord(id);
    if (ord >= EXT_ORD0) return;                            // the new ones have nothing in the save
    u32 sv = RD32(SAVE_IMAGE);
    func_80036710(sv + 0x5868, sv + (u32)(ord * 0x114 - 0x114), 0x114);
    sv = RD32(SAVE_IMAGE);
    func_80036710(sv + 0x6A68, sv + (u32)(ord * 0xDC + 0x2314), 0xDC);
    func_80036A30(RD32(SAVE_IMAGE) + 0x5868, 0x12);
    func_800E347C();
    func_800E31C4(id);
}

// ---- the menus' records of who did what (ovl_a): one 10-byte entry per ordinal --------------------
// func_800E4450: allocated for ordinals 0..101 in the game; here for the new ones too
#define N_STATS (EXT_ORD0 + EXT_CAP)
void roster_stats_alloc(void) {
    if (!RD32(0x800F612Cu)) {
        RD32(0x800F612Cu) = func_800007D4(N_STATS * 10);
        RD32(0x800F6130u) = func_800007D4(N_STATS);
    }
    func_800E450C(RD32(0x800F612Cu));
    for (u32 i = 0; i < N_STATS; i++) RD8(RD32(0x800F6130u) + i) = (u8)i;
}

// ---- movesets as the game unpacks them --------------------------------------------------------------
// A moveset is packed: per slot an index into one of the slot's lists of moves (0x80045500: list ->
// u16 moves). No Mercy Plus put some moves in slots whose lists here don't have them. The new
// Superstars' and Big Show's movesets (roster_ext.h) are packed with those slots reading cells of
// their lists that hold nothing and that no moveset of the game reads; while the game unpacks one of
// them -- the menus (func_80005EFC) or the match (ovl_e func_8013CBAC) -- those cells hold its moves.
#define MOVE_LISTS 0x80045500u
static int cells_k = -1;
static u16 cells_was[MOVESET_CELLS];
static int same(u32 a, const u8* b, int n) { for (int i = 0; i < n; i++) if (RD8(a + (u32)i) != b[i]) return 0; return 1; }
static int ram_ptr(u32 p) { return (GAME_PTR(p)) || 0; }

// ---- whose moveset it is (2026-11-10) ---------------------------------------------------------------
// The cells above, and movex.c's move rows, belong to a man. Which man's moveset the game is unpacking is told by whose
// it is, not by what's in it, so two wrestlers may have the same moveset byte for byte (the user: "two Stone Cold Steve
// Austins that are exactly alike"). In order:
//
//  1. By where the moveset lives (moves_whose). The roster hands the game a pointer into its own storage (roster_moves),
//     and each place there is one man's:
//      - an extra wrestler's place here (ext_moves): his row of roster_ext.h. A second ID (gen/revbanks.py) is the same
//        man as his base, so his row is his base's (ext_late_moveset): the same cells, and the base's move rows. Benoit
//        among the extras is the game's Benoit;
//      - a folder wrestler's place here: whose it was when he was built (folder_finish, folder_whose);
//      - the game's Superstars (ordinals 1-64 of its own table): the game's own, nothing of ours. Benoit's box is Big
//        Show's while Big Show has it (bigshow.c).
//  2. In the match only, when the moveset is somewhere else: by the wrestler in the slot (slot_whose). If the bytes
//     are a copy of that wrestler's moveset in the roster, it is his, and is told as in 1.
//  3. Only when neither tells -- by its bytes, as before: the first row of roster_ext.h with the same bytes. That is:
//     a Create-a-Wrestler's moveset (his slot, 65-82, or his edit buffer) and the special slot 83, which can hold a copy
//     of anyone's (a clone, an edit in progress); a folder wrestler's own moveset, or one based on such a man; and any
//     copy the game makes outside the roster. Rows with the same bytes have the same cells unless gen/mkroster.py
//     printed a warning; a second ID's is found as his base's, as it always was.
u32 dbg_whose[2];   // tests: the last moveset unpacked -- how it was told (1 where it lives, 2 the match's slot, 3 its
                    // bytes; 0: not ours to tell, or no row has its bytes), and its row + 1 (0: the game's own / none)
// Extra wrestler k's moveset (his place in ext_moves): whose it is.
static int ext_whose(int k) {
    if (k == EXT_BENOIT) return WHOSE_GAME;
    if (ext_is_folder(k)) return folder_whose[k - EXT_ALL];
    if (k >= EXT_LATE0) return WHOSE_ROW0 + ext_late_moveset[k - EXT_LATE0];
    return WHOSE_ROW0 + k;
}
// Whose the moveset at this address is, from the address alone. Only a pointer to the start of a man's moveset counts.
static int moves_whose(u32 moves) {
    u32 off = moves - (u32)ext_moves;                  // (below ext_moves this wraps round to a large number)
    if (off < (u32)sizeof ext_moves) return off % MOVES_SIZE ? WHOSE_UNKNOWN : ext_whose((int)(off / MOVES_SIZE));
    u32 base = RD32(ROSTER_MOVES);
    if (!ram_ptr(base)) return WHOSE_UNKNOWN;
    off = moves - base;
    if (off >= 0x54u * MOVES_SIZE || off % MOVES_SIZE) return WHOSE_UNKNOWN;
    s32 ord = (s32)(off / MOVES_SIZE);
    if (ord == ORD_BENOIT && bigshow_is_on()) return WHOSE_ROW0 + MOVESET_BIG_SHOW;
    return (ord >= 1 && ord <= 64) ? WHOSE_GAME : WHOSE_UNKNOWN;   // (65 on: Create-a-Wrestler's, the special slot)
}
// The match's: the wrestler in the slot (the game's own table of them, as logic.c slot_wrestler_of reads it). The moveset
// is his only if it is a copy of his in the roster, all 0xD8 bytes. That check asks "is this a copy of that man's", not
// "whose bytes are these", so it is right for two men with the same moveset too.
static int slot_whose(s32 slot, u32 moves) {
    if (slot < 0 || slot >= NM_SLOTS_CAP) return WHOSE_UNKNOWN;
    u32 id = RD32(NM_SLOT_WRESTLER + 4u * (u32)slot) & 0xFFFFu;
    if (id == 0 || id >= 0x1000u) return WHOSE_UNKNOWN;
    s32 ord = roster_id_to_ord(id);
    if (ord <= 0) return WHOSE_UNKNOWN;
    u32 his = roster_moves(ord);
    if (his == moves || !ram_ptr(his) || !same(moves, (const u8*)his, MOVES_SIZE)) return WHOSE_UNKNOWN;
    return moves_whose(his);
}
// Row k's cells into the game's lists (cells_off puts back what was there).
static void cells_put(int k) {
    // (2026-11-14) k is a row of moveset_cells in the game (a WHOSE_ row, or the search below); said here, where the rows are
    //  indexed, so that a k past them is no read past the tables -- undefined in C, which the compiler took its ranges from.
    //  Nothing changes in the game.
    if (k < 0 || k >= MOVESETS_N) return;
    for (int i = 0; i < moveset_ncells[k]; i++) {
        const MoveCell* c = &moveset_cells[k][i];
        u32 at = RD32(MOVE_LISTS + 4u * c->list) + 2u * c->cell;
        cells_was[i] = RD16(at); RD16(at) = c->move;
    }
    cells_k = k;
}
// The game is about to unpack this moveset (slot: the match's wrestler slot; -1 in the menus, which have none).
static void cells_on(u32 moves, s32 slot) {
    cells_k = -1;
    dbg_whose[0] = dbg_whose[1] = 0;
    if (!ram_ptr(moves)) return;
    int how = 1, w = moves_whose(moves);
    if (w == WHOSE_UNKNOWN && slot >= 0) { how = 2; w = slot_whose(slot, moves); }
    if (w == WHOSE_GAME) { dbg_whose[0] = (u32)how; return; }
    if (w >= WHOSE_ROW0) { cells_put(w - WHOSE_ROW0); dbg_whose[0] = (u32)how; dbg_whose[1] = (u32)cells_k + 1u; return; }
    // Whose it is can't be told (3 above): by its bytes, the first row that has them.
    for (int k = 0; k < MOVESETS_N; k++) {
        if (!same(moves, moveset_key[k], 0xB8)) continue;
        cells_put(k);
        dbg_whose[0] = 3; dbg_whose[1] = (u32)k + 1u;
        return;
    }
}
static void cells_off(void) {
    int k = cells_k;
    // (2026-11-14: cells_k is -1 or a row cells_put took, in the game; a value past the rows is left alone, as -1 is, instead of
    //  reading past the tables -- undefined in C. Nothing changes in the game.)
    if (k < 0 || k >= MOVESETS_N) return;
    for (int i = moveset_ncells[k] - 1; i >= 0; i--) {
        const MoveCell* c = &moveset_cells[k][i];
        RD16(RD32(MOVE_LISTS + 4u * c->list) + 2u * c->cell) = cells_was[i];
    }
    cells_k = -1;
}
// ---- movesets as slot lists (2026-11-13, movesets) -------------------------------------------------------------------
// A folder wrestler with a moves.json has his moveset as a list of slots (moveset_unpack.h): whatever the game unpacks
// for him is made from that list, so any move can be in any slot -- no cells, no pool shared with anyone, nothing that
// depends on who else is on the roster. His packed bytes (his "moveset", else his base's) are still what the roster
// keeps, and the game's unpack still runs over them first; then its result is made over from his list. Everyone else --
// the game's own Superstars, the built-in ports with their cells, a folder wrestler without a list -- is left as it was.
//
// Where the packed form is still all there is (the root fix for each: his slot list kept beside the save, by his string
// ID -- the save sidecar, scratch\agents\limits\LIMITS.md row 19 -- and the editor's choice lists past the game's):
//  - Create-a-Wrestler: the editor shows and saves a moveset as the game's packing (each slot a pick from its own
//    lists), so a moveset copied into an edit keeps only the moves the lists have.
//  - The special slot 83 and any copy outside the roster: told by its bytes alone, so it gets the packed bytes' moves.
//  - The save: folder wrestlers' movesets never go in it (save_one: not ours to save), so nothing is lost there.
#include "moveset_unpack.h"
static u8 list_buf[ML_BYTES] __attribute__((aligned(8)));
#define MATCH_LIST 0x80097FE0u   // the match's list: each entrant's record, 0x114 bytes (announce.c MATCH_RECS)
#define MATCH_IDS  0x800BD4D0u   // ... and his ID (u16; the costume in the top bits: logic.c NM_RR_LIST)
// The wrestler a match moveset is: the match's list entry it's the copy in, else the wrestler slot's (0: neither says).
static u32 match_id(u32 moves, s32 slot) {
    const u32 off = moves - (MATCH_LIST + 4u);
    const u32 id = (off < 0x65u * 0x114u && off % 0x114u == 0) ? RD16(MATCH_IDS + 2u * (off / 0x114u)) & 0x0FFFu : 0;
    if (id || slot < 0 || slot >= NM_SLOTS_CAP) return id;
    return RD32(NM_SLOT_WRESTLER + 4u * (u32)slot) & 0xFFFFu;
}
// The folder wrestler whose slot list this moveset is (his folder number), or -1. Known by whose it is, never by its
// bytes: by where it lives (his place in ext_moves), and in the match by the wrestler in the slot when it's a copy of his
// moveset in the roster (the match's list holds copies). Into list_buf when there is one.
static int list_of(u32 moves, s32 slot) {
    if (!ram_ptr(moves)) return -1;
    int k = -1;
    const u32 off = moves - (u32)ext_moves;
    if (off < (u32)sizeof ext_moves) {
        if (off % MOVES_SIZE == 0) k = (int)(off / MOVES_SIZE);
    } else if (slot >= 0 && slot < NM_SLOTS_CAP) {
        // (the match's moveset is his record's copy in the match's list -- 0x80097FE0, 0x114 bytes an entrant, the moveset
        //  at +4 -- whose entrant's ID is in 0x800BD4D0; else the wrestler the slot holds. A Rumble's entrant is unpacked
        //  before the slot says who he is (its ID reads 0xFFFF then), so the entry comes first.)
        const u32 id = match_id(moves, slot);
        const int j = (id && id < 0x1000u) ? ext_index(roster_id_to_ord(id)) : -1;
        if (j >= 0 && same(moves, ext_moves[j], MOVES_SIZE)) k = j;
    }
    if (k < 0 || k >= ext_total() || !ext_is_folder(k) || !folder_list[k - EXT_ALL]) return -1;
    const int f = folder_list[k - EXT_ALL] - 1;
    return plat_wrestler_slots(f, (u32)list_buf) ? f : -1;
}
u32 dbg_moves_list[4];   // tests: lists put in by the menus, lists put in by the match, the last folder, its slot
// QA (option qa_moves_twin): every moveset the match unpacks the game's way is also made from a list -- the game's own
// menus' unpack of it (func_800075DC, with the same cells) -- in a wrestler slot no match uses, and the two compared byte
// for byte; written to the exe's log ("[moves] twin ..."). And a folder wrestler's list: how many of his slots it changed
// from what his packed bytes gave. Off in play.
// (The match's part is the six-man engine's, which ships: the stock-engine test build, -NoSix, keeps the packed bytes'
//  moves in a match -- its table addresses aren't proved here -- and has the list in the menus only.)
#ifdef NM_SIX
GAMEFN(void, func_800075DC, 0x800075DCu, (u32 moves, u32 slots, u32 abilities));   // (the menus' unpack of the moves)
extern void plat_moves_note(u32 what, u32 id, u32 a, u32 b);
#define TWIN_SLOT (NM_SLOTS_CAP - 1)
static int twin_on = -1;
static u8 twin_list[ML_BYTES] __attribute__((aligned(8)));
static u8 twin_slots[6 * ML_ENTRIES] __attribute__((aligned(8)));
static u8 twin_bits[0x40] __attribute__((aligned(8)));
static s16 twin_kind[2] __attribute__((aligned(8)));
static int twin_ready;
#endif

// func_80005EFC(moves, slots, abilities, abilities out): the menus' unpack.
static int menu_list = -1;
static u32 menu_slots, menu_abil;
void roster_moves_pre(u32 moves, u32 slots, u32 abil_out) {
    cells_on(moves, -1);
    menu_list = list_of(moves, -1); menu_slots = slots; menu_abil = abil_out;
}
void roster_moves_post(void) {
    cells_off();
    if (menu_list >= 0) { mu_menu_put(list_buf, menu_slots, menu_abil); dbg_moves_list[0]++; dbg_moves_list[2] = (u32)menu_list; }
    menu_list = -1;
}
// ovl_e func_8013CBAC(slot, moves, ability bits): the match's. Its moves made over where the Superstar's own were
// (movex.c) after his list is in, so a folder wrestler's own move rows (when he has them) go on top of his list.
#ifdef NM_SIX
static int match_list = -1;
static s32 match_slot;
static u32 match_bits, match_b38, match_moves;
static u16 match_b3c;
#endif
#ifdef NM_SIX
static int sweeping;     // (QA: moves_sweep is calling the game's unpack itself -- the hooks leave it alone)
// twin_list: the list the game's own menus' unpack (func_800075DC) makes of this packed moveset, for layout kind `kind`, with
// whatever cells are on now.
static void twin_list_of(u32 moves, s32 kind) {
    twin_kind[0] = (s16)kind;
    func_800075DC(moves, (u32)twin_slots, (u32)twin_kind);
    for (int e = 0; e < ML_ENTRIES; e++) {   // (as a list: moveset_unpack.h's 8 bytes an entry, No Mercy's own moves)
        const u8* m = twin_slots + 6 * e;
        u8* t = twin_list + ML_STRIDE * e;
        for (int b = 0; b < ML_STRIDE; b++) t[b] = 0;
        t[0] = m[0]; t[1] = m[1];
        t[2] = (u8)((m[2] & 1u) | (m[3] & 1u) << 1 | (m[4] & 1u) << 2);
    }
}
#endif
#if __has_include("movex_twin.h")
static s32 twin_late = -1;      // (QA build: the late movex_pre was given for this unpack -- whose bytes his are)
#endif
void roster_match_moves_pre(s32 slot, u32 moves, u32 bits) {
#ifdef NM_SIX
    if (sweeping) return;
#endif
    cells_on(moves, slot);
#ifdef NM_SIX
    match_list = -1; twin_ready = 0;
    match_slot = slot; match_bits = bits; match_moves = moves;
    if (slot >= 0 && slot < TWIN_SLOT && GAME_PTR(bits)) {
        match_list = list_of(moves, slot);
        match_b38 = RD32(bits + 0x38u); match_b3c = RD16(bits + 0x3Cu);   // (the bits as they were: the game's unpack ORs into them)
        if (twin_on < 0) twin_on = (int)plat_config("qa_moves_twin", 0);
        if (twin_on && match_list < 0) {   // (the twin's list: the game's menus' unpack of the same bytes, the same cells on)
            twin_list_of(moves, (RD32(bits + 0x34u) & 2u) ? 1 : 0);
            twin_ready = 1;
        }
    }
#endif
    // (cells_k: the row of the man whose moveset it is -- a second ID's is his base's, so he gets his base's move rows)
    movex_pre(slot, cells_k >= MOVESET_LATE0 ? cells_k - MOVESET_LATE0 : -1);
#if __has_include("movex_twin.h")
    twin_late = cells_k >= MOVESET_LATE0 ? cells_k - MOVESET_LATE0 : -1;
#endif
}
#ifdef NM_SIX
// (QA) Slot t's four tables made a copy of slot s's (so bytes neither side writes -- records' 0x1B-0x1F -- start alike).
static const u32 tw_base[4] = { MU_MOVE(0, 0), MU_RECIX_(0, 0), MU_NEXT(0, 0), MU_REC(0, 0) };
static const u32 tw_size[4] = { MU_E * 2u, MU_E * 2u, MU_E, MU_E * 36u }, tw_at[4] = { 0, MU_E * 2u, MU_E * 4u, MU_E * 5u };
static void twin_copy(s32 s, s32 t) {
    for (int k = 0; k < 4; k++) for (u32 b = 0; b < tw_size[k]; b++) RD8(tw_base[k] + (u32)t * tw_size[k] + b) = RD8(tw_base[k] + (u32)s * tw_size[k] + b);
}
// ... and compared, with the two ability bits: bytes different << 16 | the first one's offset (0xFFFF none, 0xFFFE bits only).
static u32 twin_differ(s32 s, s32 t, u32 bits_s, u32 bits_t) {
    u32 differ = 0, first = 0xFFFFu;
    for (int k = 0; k < 4; k++)
        for (u32 b = 0; b < tw_size[k]; b++)
            if (RD8(tw_base[k] + (u32)s * tw_size[k] + b) != RD8(tw_base[k] + (u32)t * tw_size[k] + b)) { if (!differ++) first = tw_at[k] + b; }
    if (RD32(bits_s + 0x38u) != RD32(bits_t + 0x38u) || RD16(bits_s + 0x3Cu) != RD16(bits_t + 0x3Cu)) { differ++; if (first == 0xFFFFu) first = 0xFFFEu; }
    return differ << 16 | first;
}
// (QA) The slot's four tables and bits, as the game made them, against TWIN_SLOT's made from twin_list.
static void moves_twin(void) {
    const s32 s = match_slot, t = TWIN_SLOT;
    twin_copy(s, t);
    for (u32 b = 0; b < 0x40u; b++) twin_bits[b] = RD8(match_bits + b);
    RD32((u32)twin_bits + 0x38u) = match_b38; RD16((u32)twin_bits + 0x3Cu) = match_b3c;
    mu_match_build(t, twin_list, (u32)twin_bits);
    plat_moves_note(1, match_id(match_moves, s), (u32)s << 16 | (u32)twin_kind[0], twin_differ(s, t, match_bits, (u32)twin_bits));
}
// (QA) The same for every moveset on the roster, once, at the first match (when the match's code and tables are in): the
// game's own ordinals 1-83 and every extra wrestler -- the built-in ports with their cells, Benoit, folder wrestlers without
// a list -- each for both layout kinds. The game's own match unpack (func_8013CBAC, called here, into wrestler slot
// TWIN_SLOT - 1) against mu_match_build from the game's own menus' unpack (into TWIN_SLOT). So every moveset the roster
// has is proved in the game, not only the ones a match happens to draw.
GAMEFN(void, func_8013CBAC, 0x8013CBACu, (s32 slot, u32 moves, u32 bits));   // (ovl_e: the match's unpack)
static u8 sweep_bits[2][0x40] __attribute__((aligned(8)));
static int swept;
static void moves_sweep(void) {
    const s32 g = TWIN_SLOT - 1, t = TWIN_SLOT;
    u32 n = 0, bad = 0;
    for (int i = 1, all = 0x54 + ext_total(); i < all; i++) {
        const int k = i - 0x54;
        const u32 moves = k < 0 ? roster_moves(i) : (u32)ext_moves[k];
        if (k >= 0 && (!ext_wanted(k) || (ext_is_folder(k) && folder_list[k - EXT_ALL]))) continue;   // (a list's own: nothing to twin)
        if (!ram_ptr(moves)) continue;
        for (s32 kind = 0; kind < 2; kind++) {
            for (int j = 0; j < 2; j++) { for (u32 b = 0; b < 0x40u; b++) sweep_bits[j][b] = 0; sweep_bits[j][0x37] = (u8)(kind ? 2 : 0); }
            sweeping = 1;
            cells_on(moves, -1);                                   // (the cells of whose it is, as in the menus)
            twin_list_of(moves, kind);
            twin_copy(g, t);
            func_8013CBAC(g, moves, (u32)sweep_bits[0]);            // (the game's own, cells on)
            cells_off();
            sweeping = 0;
            mu_match_build(t, twin_list, (u32)sweep_bits[1]);       // (the list's, no cells: as for a folder wrestler)
            const u32 d = twin_differ(g, t, (u32)sweep_bits[0], (u32)sweep_bits[1]);
            n++;
            if (d >> 16) { bad++; plat_moves_note(3, k < 0 ? roster_ord_to_id(i) : ext_id(k), (u32)i << 16 | (u32)kind, d); }
        }
    }
    plat_moves_note(4, 0, n, bad);
}
#if __has_include("movex_twin.h")
#include "movex_twin_sweep.h"
#endif
#endif
void roster_match_moves_post(void) {
#ifdef NM_SIX
    if (sweeping) return;
#endif
    cells_off();
    const u8* ids = 0;   // (2026-11-13, movelib: his list, whose move IDs say which entries play library moves -- movex.c)
#ifdef NM_SIX
    if (match_list >= 0) {
        ids = list_buf;
        // (how many of the layout's slots his list changes from what his packed bytes gave: QA's count of slots won)
        u32 changed = 0;
        if (twin_on > 0) {
            for (u32 p = RD32(MU_LAYOUTS + ((RD32(match_bits + 0x34u) & 2u) ? 4u : 0u)); RDS16(p) != -1; p += 10u)
                changed += ml_given(list_buf, RDS16(p)) && RD16(MU_MOVE(match_slot, RDS16(p))) != ml_move(list_buf, RDS16(p));
        }
        RD32(match_bits + 0x38u) = match_b38; RD16(match_bits + 0x3Cu) = match_b3c;
        mu_match_build(match_slot, list_buf, match_bits);
        dbg_moves_list[1]++; dbg_moves_list[2] = (u32)match_list; dbg_moves_list[3] = (u32)match_slot;
        if (twin_on > 0) plat_moves_note(2, match_id(match_moves, match_slot), (u32)match_slot << 16 | (u32)match_list, changed);
        {   extern s32 movex_list_folder;      // (2026-11-13, movelib2: whose list -- movex.c puts in his moves.json's edits)
            movex_list_folder = match_list; }
    } else if (twin_ready) moves_twin();
    match_list = -1; twin_ready = 0;
#endif
    movex_post(ids);
#if defined(NM_SIX) && __has_include("movex_twin.h")
    if (ids && twin_on > 0) movex_twin_list(match_slot, match_moves, match_bits, match_b38, match_b3c, match_id(match_moves, match_slot));
#endif
#ifdef NM_SIX
    if (twin_on > 0 && !swept) {
        swept = 1; moves_sweep();
#if __has_include("movex_twin.h")
        movex_twin_sweep();   // (QA build only, scratch\agents\movelib: the move rows as they were against the library's)
#endif
    }
#endif
}
// Big Show's moveset, packed as the roster keeps it: No Mercy Plus's (roster_ext.h). (content2 2026-11-19: Big Show is there
// only with No Mercy Plus -- extras.c -- so there is no stand-in moveset any more.)
const u8* roster_bigshow_moveset(void) { return moveset_key[MOVESET_BIG_SHOW]; }

// ---- each frame -------------------------------------------------------------------------------------
// (2026-11-13, removal) The match's men, each time a slot takes someone (a Royal Rumble's entrants too): a removed man in a
// match means something still put him there -- said, never hidden (he plays as he was: his records are the game's). QA
// (qa_removal 2): every man who comes into a match, for comparing runs.
static void rm_match_watch(void) {
    static u16 last[NM_SLOTS_CAP];
    const u16 frames = RD16(NM_MATCH_FRAMES);
    if (frames == 0xFFFFu || frames < 30u) { if (frames == 0xFFFFu) for (int s = 0; s < NM_SLOTS_CAP; s++) last[s] = 0; return; }
    for (int s = 0, n = nm_slots(); s < n; s++) {
        const u16 id = (u16)(RD32(NM_SLOT_WRESTLER + 4u * (u32)s) & 0xFFFFu);
        if (id == last[s]) continue;
        last[s] = id;
        if (!id || id >= 0x1000u) continue;
        if (roster_id_removed(id)) { removal_s("WARNING: "); removal_who(id); removal_s(" is removed and is in the match (slot "); removal_num(s + 1); removal_s(")"); removal_say(); }
        else if (rm_qa & 2) { removal_s("qa match slot "); removal_num(s + 1); removal_s(": "); removal_who(id); removal_say(); }
    }
}
// on: the new Superstars are wanted; media: No Mercy Plus's themes and videos are on.
void save_sidecar_frame(void);
static void qa_seats_frame(void);
void roster_frame(u64 mask, int media) {
    seat_tick++;                // (staging seats: how long ago each was asked for)
    save_sidecar_frame();       // (the save just read: its sidecar's champions back -- save.c; every roster, staging or not)
    qa_seats_frame();
    u64 was = ext_mask;
    ext_mask = mask;
    ext_media = media;
    if ((mask & EXT_MASK_ALL) && (ext_built != (2 | media))) ext_build_all();
    {   // (2026-11-19, roster: Benoit among the new ones while Big Show has his ordinal -- no mask bit of his own now)
        static int bs_last = -1;
        const int bs = bigshow_is_on();
        if (bs) { int b = benoit_built; benoit_build(); if (benoit_built && !b) was = ~mask; }
        if (bs != bs_last) { if (bs_last >= 0) was = ~mask; bs_last = bs; }
    }
    if (was != mask && pages_built) {               // (the file's pages laid out again; the game's: the new ones in or out)
        if (jp_on) layout();
        else place_ext();
    }
    if (roster_removed_n() > 0 || (rm_qa & 2)) rm_match_watch();
    { int save_vacate_due(void);   // (no2k15: or a title a retired champion held, vacated from the sidecar -- save.c)
      if (roster_removed_n() > 0 || rm_qa > 0 || save_vacate_due()) { void save_removal_frame(void); save_removal_frame(); } }   // (save.c: the save written after a title is vacated; presets; QA)
}
// Each frame while the menus are up (logic.c arcade_roster_service; 2026-11-13, seatsroot -- staging only, TEMPORARY): the
// select's window of pages follows the page shown; boxes whose wrestler lost his seat in a match get it back; a seat with a
// new wrestler in it has its photo let go by the game's sprite cache (it would go on drawing the last one's -- its file
// numbers are the seat's: files.c FILE_PHOTO2_*).
#define SEAT_PHOTO_TEX(p)  (0x7C00u + (u32)((p) - 128))
#define SEAT_PHOTO_TLUT(p) (0x7C80u + 2u * (u32)((p) - 128))
void save_sidecar_frame(void);
static void qa_seats_frame(void);
void roster_menus_frame(void) {
    if (!pages_built || !RD32(ROSTER_RECS)) return;
    if (roster_staging()) seats_pages_heal();
    ring_follow();                  // (the window: staging, or more than 32 pages)
    if (!staging) return;
    for (int s = 0; s < folder_n; s++) {
        if (!seat_photo_dirty[s]) continue;
        seat_photo_dirty[s] = 0;
        // (the menus' record of who did what is by ordinal: the new man starts with none, not the last one's -- ovl_a's
        //  table, roster_stats_alloc; kept per wrestler only once the sidecar holds them too: the plan's)
        const u32 stats = RD32(0x800F612Cu);
        if (GAME_PTR(stats)) for (u32 i = 0; i < 10; i++) RD8(stats + 10u * (u32)(EXT_ORD0 + EXT_ALL + s) + i) = 0;
        const int p = FOLDER_PHOTO(s);
        plat_game_call(GF_SPRITE_RELEASE, SEAT_PHOTO_TEX(p), 0, 0);
        plat_game_call(GF_SPRITE_RELEASE, SEAT_PHOTO_TLUT(p), 0, 0);
        plat_game_call(GF_SPRITE_RELEASE, SEAT_PHOTO_TLUT(p) + 1u, 0, 0);
    }
}
// (tests and the log: seats taken, taken back, refused)
u32 roster_seat_counts(int which) { return which == 0 ? seats_taken : which == 1 ? seats_given_back : seats_refused; }
// The save sidecar's (save.c; 2026-11-13, seatsroot): the folder (the exe's number) of the wrestler with this ordinal, if
// it's a seat's, else -1; and a folder wrestler's ordinal, a seat given him if need be (0: none free).
int roster_folder_of_ord(u32 ord) {
    ext_total();
    const u32 s = ord - (u32)(EXT_ORD0 + EXT_ALL);
    return s < (u32)folder_n ? seat_folder[s] : -1;
}
s32 roster_ord_of_folder(int f) {
    ext_total();
    if (!staging) return f >= 0 && f < folder_n ? EXT_ORD0 + EXT_ALL + f : 0;
    const int s = seat_take(f);
    return s >= 0 ? EXT_ORD0 + EXT_ALL + s : 0;
}
// QA (2026-11-13, seatsroot: the save sidecar's scenarios). qa_seat_save 1: 200 frames after the save was read, folder qa_seat_folder is
// made the first title's champion (title record 0, costume 0) and the save written (func_80004EE8: the sidecar with it);
// 2: nothing changed. Both then log every title's champion ("[seats] title" lines: whose folder, or the game's ordinal).
extern u32 plat_config(const char* key, u32 fallback);
static void qa_seats_frame(void) {
    static int read, mode, folder, frames, done;
    if (!read) { read = 1; mode = (int)plat_config("qa_seat_save", 0); folder = (int)plat_config("qa_seat_folder", 0); }
    { extern int save_sidecar_read(void); if (!save_sidecar_read()) return; }   // (from the save's reading on: its titles are there)
    if (!mode || done || ++frames < 200) return;
    done = 1;
    if (mode == 1 && RD32(SAVE_IMAGE_PTR)) {
        const s32 o = roster_ord_of_folder(folder);
        if (o > 0) { RD8(TITLE_RECS + 2) = (u8)o; RD8(TITLE_RECS + 4) = 0; func_80004EE8(); }
    }
    for (int t = 0; t < 7; t++) {
        const u32 o = RD8(TITLE_RECS + 0x34u * (u32)t + 2);
        plat_seat_note(6, t, roster_folder_of_ord(o), (int)o);
    }
}
