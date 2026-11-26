// The save stays the game's: the new Superstars never go into it.
//
// The game keeps its save as an image in RAM (*0x80062C20, ovl_a's *0x800F6134), changes it as things
// happen and writes it out whole through func_80004EE8, the one way it's ever written. The Superstars'
// own data (their records, movesets, the Superstar Select's pages) never goes into the image at all
// (roster.c). What can still name one is where the game keeps a Superstar's ordinal: the titles'
// champions, the exhibition presets and the story in progress. Those stay in the image as the game
// has them (the game reads them back from it), and only what goes out is without the new Superstars:
//   a title's champions: those who are new Superstars are left out (the ones before them move up);
//   a preset with one in it: an unused preset (as a new save has them);
//   a story in progress with one in it: no story in progress (as a new save has);
//   a story of ours (story.c) in progress: what was in progress before it.
// func_80004EE8 calls save_sanitize before it writes and save_restore after (gen/patches.py).
//
// The sidecar (2026-11-13, seatsroot). The save keeps a champion as a 7-bit ordinal (func_800E37D4), so no ordinal of ours
// past 127 -- every folder wrestler's seat (142 on since no2k15, 2026-11-23; 144 before), and the built-in ports' from 128 -- can be in it at all: it went in cut
// to 7 bits, which names somebody else (ordinal 150 came back as 22), or was taken out as one of ours. Now a title whose
// champion (or partner) is one of ours goes out without that current reign, and the champion is written beside the save
// instead, by string ID (a folder's; a built-in port's game number), with a check of the title's bytes as they went out
// (the exe: <save>.ours.json, arcade_wrestlers.cpp sidecar_put). When the save is read back (roster_pages_from_save), each
// title whose bytes are still those gets its champion back -- a seat given him if need be. A save changed without the
// mod (other bytes) keeps what it has. The game's image is unchanged apart from the reign it could never hold.
// TEMPORARY over the game's save format (7-bit ordinals); the root fix is Stage 2's native records and a save codec of
// our own (stage2\PLAN.md). Not in it yet, and still the game's 7-bit format: a past reign of ours in the title history
// (cut as before), the exhibition presets (emptied, as before) and the story in progress (dropped, as before).
#include "arcade.h"
#include "gamefn.h"     // (2026-11-13, removal: func_80004EE8 for the QA set-up)

#define SAVE_IMAGE_PTR 0x80062C20u
#define TITLES         0x7018    // 7 x 0x26: 6 x 6 bytes (champions, the current first), u8
#define TITLE_SIZE     0x26
#define PRESETS        0x6E84    // 15 x 0xE: u8 (0xFF: unused), 9 bytes of bits, 4 bytes
#define PRESET_SIZE    0xE
#define STORY          0x6F90    // 0x88: +1 0xFF: none; +8: 16 x { u8 ordinal, u8 ordinal, u8 }
#define KEPT0          0x6E84    // the part of the image kept aside while it's written
#define KEPT_END       (TITLES + 7 * TITLE_SIZE)

static u8 kept[KEPT_END - KEPT0];
static u32 kept_image;
u32 dbg_save_writes, dbg_save_cleaned;

static int is_new(u32 ord) { return roster_ord_is_new((s32)ord); }

// the game's bit packing (func_800082A8 / func_80008350): from bit 0 of the first byte up
static u32 bits(u32 src, u32* pos, u32 n) {
    u32 v = 0;
    for (u32 i = 0; i < n; i++, (*pos)++) v |= (u32)((RD8(src + (*pos >> 3)) >> (*pos & 7)) & 1u) << i;
    return v;
}

// ---- the sidecar (2026-11-13, seatsroot): see the top -----------------------------------------------------------------
#define TITLE_RECS 0x800A3EB0u      // the titles as the game has them: 7 x 0x34, +2 holder, +3 partner (ordinals), +4 / +5 costumes
#define SC_MAX     14               // 7 titles x champion and partner
int roster_folder_of_ord(u32 ord);
s32 roster_ord_of_folder(int f);
u32 roster_ord_to_id(s32 ord);
s32 roster_id_to_ord(u32 id);
extern void plat_sidecar_put(u32 buf, int len);
extern int plat_sidecar_get(u32 buf, int max);
static u8 ours_now[7];              // this write: the title's champion or partner is one of ours (bit 0 / bit 1)
static u8 title_out[7][TITLE_SIZE]; // ... and that title's bytes as they went out (kept in the image after the write too)
static u8 sc_buf[4 + 12 * SC_MAX] __attribute__((aligned(8)));
static int sc_due;                  // the save was just read: the sidecar to be put back (save_sidecar_frame)
u32 dbg_sidecar_out, dbg_sidecar_in;
static u32 title_check(u32 tb) { u32 c = 0x5343u; for (u32 j = 0; j < TITLE_SIZE; j++) c = c * 31u + RD8(tb + j); return c; }
// One of ours (an ordinal past the game's): the folder (or -1) and game number to keep him by.
static int ours(u32 ord) { return ord >= 0x66u && ord <= 0xFFu; }

static void clean_titles(u32 img) {
    for (u32 t = 0; t < 7; t++) {
        u32 tb = img + TITLES + t * TITLE_SIZE;
        u8 e[6][6];
        int n = 0;
        for (u32 i = 0; i < 6; i++) {
            u32 src = tb + 6 * i, pos = 0;
            u32 a = bits(src, &pos, 7); bits(src, &pos, 2);       // (func_800E37D4: champion, costume,
            u32 b = bits(src, &pos, 7);                            //  partner, ...)
            if (i == 0 && ours_now[t]) { dbg_save_cleaned++; continue; }   // (the reign of ours: the sidecar has it)
            if (is_new(a) || is_new(b)) { dbg_save_cleaned++; continue; }
            for (u32 j = 0; j < 6; j++) e[n][j] = RD8(src + j);
            n++;
        }
        for (int i = 0; i < 6; i++)
            for (u32 j = 0; j < 6; j++) RD8(tb + 6u * (u32)i + j) = i < n ? e[i][j] : 0;
    }
}
static void clean_presets(u32 img) {
    for (u32 p = 0; p < 15; p++) {
        u32 pb = img + PRESETS + p * PRESET_SIZE;
        if (RD8(pb) == 0xFF) continue;
        u32 src = pb + 1, pos = 0, any = 0;
        u32 type = bits(src, &pos, 5);                            // (func_800E40B8)
        for (int i = 0; i < 4; i++) { if (is_new(bits(src, &pos, 7))) any = 1; bits(src, &pos, 2); }
        bits(src, &pos, 6); bits(src, &pos, 6);
        u32 f = bits(src, &pos, 4), last = bits(src, &pos, 8);
        if (type == 2 && f != 5 && is_new(last)) any = 1;         // (then the last is an ordinal too)
        if (!any) continue;
        RD8(pb) = 0xFF;
        for (u32 j = 1; j < PRESET_SIZE; j++) RD8(pb + j) = 0;
        dbg_save_cleaned++;
    }
}
static void clean_story(u32 img) {
    u32 sb = img + STORY;
    if (RD8(sb + 1) == 0xFF) return;
    int any = 0;
    for (u32 i = 0; i < 16; i++) if (is_new(RD8(sb + 8 + 3 * i)) || is_new(RD8(sb + 9 + 3 * i))) any = 1;
    if (!any) return;
    for (u32 j = 0; j < 0x88; j++) RD8(sb + j) = 0;                // (func_800E3F28)
    RD8(sb + 1) = 0xFF;
    dbg_save_cleaned++;
}

// ---- removed stock Superstars in the save (2026-11-13, removal; the user's decisions of 2026-11-13) --------------------
// A stock man whose folder is deleted is removed from the game (roster.c "removed"). What the save names of him:
//   - a title he holds (or holds with a partner: the Tag Team title) is VACATED at once (the user's R3, 2026-11-13: EMPTY,
//     no champion, until someone wins it -- not the champion before him): in the game's own title table an empty reign
//     goes in front of his, so his is a former one (R4: history isn't rewritten), and the game packs the titles and writes
//     the save then and there (func_800E37D4). His return doesn't give it back.
//   - an exhibition preset that names him plays his STAND-IN (R6: roster_stand_in, the man after him on his select page):
//     the game's preset table in memory gets the stand-in, so choosing it puts the stand-in in the match; what goes out to
//     the save still names him (save_sanitize: put back while that preset is as we left it), so it is his again once he's
//     back. (A type-2 preset's last byte, an ordinal too, isn't changed.)
//   - the story in progress (R7) and his edited costumes: kept as they are (Story Mode casts stand-ins: bigshow.c, story.c).
// Each is said in the log ("[removal] save: ..."). The game's tables in memory (ovl_a's code reads and writes them):
#define RT_TITLES      0x800A3EB0u  // 7 x 0x34: 6 x 8 bytes (the current champion first): u16, u8 champion ordinal, u8
#define RT_TITLE_SIZE  0x34u        //   partner's (the Tag Team title's), u8 / u8 their costumes, u16 (func_800E37D4 packs it)
#define RT_PRESETS     0x800BD5A8u  // 15 x 0x22: u8 (0xFF unused), u8, 4 x s16 Superstar ID, 4 x u8 costume, u8 type, ...
#define RT_PRESET_SIZE 0x22u        //   (func_800E430C fills it from the save -- for the presets page, not at load --,
                                    //    func_800E4218 packs it back; the stand-in goes in each frame of the menus)
#define OVL_SAVE_IMAGE 0x800F6134u  // (ovl_a's pointer to the save image)
GAMEFN(void, func_800E37D4, 0x800E37D4u, (void));             // the titles into the save image, and the save written
GAMEFN(void, func_800E40B8, 0x800E40B8u, (u32 dst, u32 src)); // a preset into the save image's bits (QA set-up)
GAMEFN(void, func_800E430C, 0x800E430Cu, (u32 names));        // the presets from the save image into RT_PRESETS (QA)
// (ovl_a loaded: the menus' code, whose functions these are -- their first two words)
// (the menus running: their main loop's code, ovl_c -- logic.c title_loaded; at boot ovl_a's code is there before the menus
//  are, and a save written then didn't reach the disk: 2026-11-13, removal2's first set-up run)
static int menus_running(void) { return RD32(0x80111158u) == 0x27BDFFB8u; }
static int titles_code_here(void) { return RD32(0x800E37D4u) == 0x3C02800Fu && RD32(0x800E37D8u) == 0x8C426134u; }
static int preset_code_here(void) { return RD32(0x800E40B8u) == 0x27BDFFC8u && RD32(0x800E40BCu) == 0xAFB30024u &&
                                          RD32(0x800E430Cu) == 0x27BDFFD8u && RD32(0x800E4310u) == 0xAFB10014u; }
int roster_removed_n(void);
int roster_removed_qa(void);
int roster_id_removed(u32 id);
u16 roster_stand_in(u32 id, int nth);
u32 roster_ord_to_id(s32 ord);
s32 roster_id_to_ord(u32 id);
void removal_s(const char* s); void removal_who(u32 id); void removal_num(s32 v); void removal_say(void);
// (relprep2 2026-11-24) The title names are an array of their own, in .rodata, not string literals: the linker packs the
//  literals of every file together, and there "World Heavyweight" had landed just before two arena names of the extras
//  (HALLOWEEN HAVOC, STARRCADE) that the release ships as zeros (firstrun/mod_binary.holes) -- "Heavyweight" and those
//  zeros, 32 bytes, were the same bytes as a place in No Mercy Plus V3. The same names, read the same way (only the log
//  uses them).
static const char title_name[7][18] = { "World Heavyweight", "Tag Team", "Intercontinental", "European", "Hardcore",
                                        "Light Heavyweight", "Women's" };
static u32 ord_id(u32 ord) { return ord ? roster_ord_to_id((s32)ord) : 0; }
static int named_removed(u32 ord) { return ord && roster_id_removed(ord_id(ord)); }
static void put_bits(u32 src, u32 pos, u32 n, u32 v) {     // (the game's packing, as bits() reads it)
    for (u32 i = 0; i < n; i++, pos++) {
        const u32 at = src + (pos >> 3), m = 1u << (pos & 7);
        RD8(at) = (u8)((v >> i) & 1u ? RD8(at) | m : RD8(at) & ~m);
    }
}
static u32 rt_entry(u32 t, u32 i) { return RT_TITLES + t * RT_TITLE_SIZE + 8u * i; }

// The titles in memory: each whose champion (or champion's partner) is removed is vacated -- the reigns move down one
// (the oldest of the six drops off, as when the game crowns a new champion) and the current one is empty: no champion.
// Returns how many titles were vacated. (An empty current reign names nobody, so it is never vacated twice.)
static int vacate_titles(void) {
    int n = 0;
    for (u32 t = 0; t < 7; t++) {
        const u32 e = rt_entry(t, 0), a = RD8(e + 2), b = RD8(e + 3);
        if (!named_removed(a) && !named_removed(b)) continue;
        for (int i = 5; i > 0; i--) for (u32 j = 0; j < 8; j++) RD8(rt_entry(t, (u32)i) + j) = RD8(rt_entry(t, (u32)i - 1) + j);
        for (u32 j = 0; j < 8; j++) RD8(e + j) = 0;
        removal_s("save: the "); removal_s(title_name[t]); removal_s(" title: "); removal_who(ord_id(a));
        if (b) { removal_s(" & "); removal_who(ord_id(b)); }
        removal_s(" held it -- VACATED: no champion until someone wins it (his reign kept among the former ones)");
        removal_say();
        n++;
    }
    return n;
}

// The presets in memory: a removed man's place goes to his stand-in (one not in that preset already); what was there is
// kept (rm_preset) so the save still gets him (presets_back_out).
static struct { u8 used; u16 was, now; u8 after[RT_PRESET_SIZE]; } rm_preset[15][4];
static void presets_stand_in(void) {
    for (u32 p = 0; p < 15; p++) {
        const u32 pb = RT_PRESETS + p * RT_PRESET_SIZE;
        if (RD8(pb) == 0xFF) continue;
        for (u32 k = 0; k < 4; k++) {
            const u16 id = RD16(pb + 2u + 2u * k);
            if (!id || id >= 0x1000u || !roster_id_removed(id)) continue;
            u16 s = 0;
            for (int nth = 0; nth < 8 && !s; nth++) {
                const u16 c = roster_stand_in(id, nth);
                if (!c) break;
                int there = 0;
                for (u32 j = 0; j < 4; j++) if (RD16(pb + 2u + 2u * j) == c) there = 1;
                if (!there) s = c;
            }
            if (!s) continue;
            RD16(pb + 2u + 2u * k) = s;
            rm_preset[p][k].used = 1; rm_preset[p][k].was = id; rm_preset[p][k].now = s;
            for (u32 j = 0; j < 4; j++)                // (each kept place of this preset: the preset as it is now)
                if (rm_preset[p][j].used) for (u32 b = 0; b < RT_PRESET_SIZE; b++) rm_preset[p][j].after[b] = RD8(pb + b);
            removal_s("save: exhibition preset "); removal_num((s32)p + 1); removal_s(": "); removal_who(id); removal_s(" is removed -- ");
            removal_who(s); removal_s(" stands in (place "); removal_num((s32)k + 1); removal_s("; the save keeps "); removal_who(id); removal_s(")");
            removal_say();
        }
    }
}
// What goes out (the image, while it's written): a preset still as we left it has him back in his place.
static void presets_back_out(u32 img) {
    for (u32 p = 0; p < 15; p++)
        for (u32 k = 0; k < 4; k++) {
            if (!rm_preset[p][k].used) continue;
            const u32 pb = RT_PRESETS + p * RT_PRESET_SIZE;
            int same = 1;
            for (u32 b = 0; b < RT_PRESET_SIZE && same; b++) same = RD8(pb + b) == rm_preset[p][k].after[b];
            if (!same) { rm_preset[p][k].used = 0; continue; }       // (the player changed it: his own now)
            const u32 src = img + PRESETS + p * PRESET_SIZE + 1, pos = 5u + 9u * k;
            u32 at = pos;
            if (bits(src, &at, 7) == (u32)roster_id_to_ord(rm_preset[p][k].now)) put_bits(src, pos, 7, (u32)roster_id_to_ord(rm_preset[p][k].was));
        }
}

// What the save names of a removed man (the log; QA qa_removal 2: every title and preset too). From roster.c at the save's
// load, from save_sanitize at each write, from save_removal_frame once the menus are up.
void save_removal_report(void) {
    const u32 img = RD32(SAVE_IMAGE_PTR);
    if (!GAME_PTR(img) || (roster_removed_n() <= 0 && !roster_removed_qa())) return;
    for (u32 t = 0; t < 7; t++) {
        if (roster_removed_qa() & 2) {
            removal_s("qa titles "); removal_s(title_name[t]); removal_s(":");
            for (u32 i = 0; i < 6; i++) {
                const u32 a = RD8(rt_entry(t, i) + 2), b = RD8(rt_entry(t, i) + 3);
                removal_s(" "); removal_who(ord_id(a)); if (b) { removal_s("&"); removal_who(ord_id(b)); }
            }
            removal_say();
        }
        for (u32 i = 1; i < 6; i++)
            for (u32 k = 0; k < 2; k++) {
                const u32 o = RD8(rt_entry(t, i) + 2 + k);
                if (!named_removed(o)) continue;
                removal_s("save: the "); removal_s(title_name[t]); removal_s(" title: "); removal_who(ord_id(o));
                removal_s(" is among its former champions -- kept"); removal_say();
            }
    }
    if (roster_removed_qa() & 2)
        for (u32 p = 0; p < 15; p++) {
            const u32 pb = RT_PRESETS + p * RT_PRESET_SIZE;
            if (RD8(pb) == 0xFF) continue;
            removal_s("qa preset "); removal_num((s32)p + 1); removal_s(":");
            for (u32 k = 0; k < 4; k++) { removal_s(" "); removal_who(RD16(pb + 2u + 2u * k)); }
            u32 src = img + PRESETS + p * PRESET_SIZE + 1, pos = 5;
            removal_s(" (the save image:");
            for (u32 k = 0; k < 4; k++) { const u32 o = bits(src, &pos, 7); bits(src, &pos, 2); removal_s(" "); removal_who(ord_id(o)); }
            removal_s(")"); removal_say();
        }
    {
        const u32 sb = img + STORY;
        if (RD8(sb + 1) != 0xFF)
            for (u32 i = 0; i < 16; i++)
                for (u32 k = 0; k < 2; k++)
                    if (named_removed(RD8(sb + 8 + 3 * i + k))) {
                        removal_s("save: the Story Mode story in progress names "); removal_who(ord_id(RD8(sb + 8 + 3 * i + k)));
                        removal_s(" -- kept (its matches cast stand-ins)"); removal_say();
                        i = 16; break;
                    }
    }
}

// Each frame (roster.c roster_frame, with someone removed or QA on): in the menus, once the game's code and tables are
// there, the presets' stand-ins put and the titles vacated (the save then written by the game's own function).
// QA (qa_removal 1, the set-up, with nobody removed): THE ROCK made the World Heavyweight champion after KANE, THE ROCK and
// MANKIND the Tag Team champions after the DUDLEYS, exhibition preset 1 THE ROCK vs STEVE AUSTIN, and the save written.
static int menus_frames;           // (frames in the menus in a row: the save is loaded and the tables are filled by then)
static int sc_vacate_write;        // (no2k15: a sidecar title vacated, the save to be written -- save_sidecar_frame, below)
static void qa_new_champion(u32 t, u32 a, u32 b) {
    for (int i = 5; i > 0; i--) for (u32 j = 0; j < 8; j++) RD8(rt_entry(t, (u32)i) + j) = RD8(rt_entry(t, (u32)i - 1) + j);
    for (u32 j = 0; j < 8; j++) RD8(rt_entry(t, 0) + j) = 0;
    RD8(rt_entry(t, 0) + 2) = (u8)a; RD8(rt_entry(t, 0) + 3) = (u8)b;
}
static void qa_setup(void) {
    const u32 img = RD32(OVL_SAVE_IMAGE);
    qa_new_champion(0, (u32)roster_id_to_ord(0x0004), 0);                                     // KANE, then
    qa_new_champion(0, (u32)roster_id_to_ord(0x0001), 0);                                     // THE ROCK
    qa_new_champion(1, (u32)roster_id_to_ord(0x0504), (u32)roster_id_to_ord(0x0503));          // the DUDLEYS, then
    qa_new_champion(1, (u32)roster_id_to_ord(0x0001), (u32)roster_id_to_ord(0x0006));          // THE ROCK & MANKIND
    const u32 pb = RT_PRESETS;                                                                  // preset 1: a singles match
    for (u32 b = 0; b < RT_PRESET_SIZE; b++) RD8(pb + b) = 0;
    RD8(pb) = 1; RD16(pb + 2) = 0x0001; RD16(pb + 4) = 0x0002;
    RD8(img + PRESETS) = 1;
    func_800E40B8(img + PRESETS + 1, pb);
    removal_s("qa set-up: THE ROCK the World Heavyweight champion (after KANE), THE ROCK & MANKIND the Tag Team champions"
              " (after the DUDLEYS), preset 1 THE ROCK vs STEVE AUSTIN; the save written");
    removal_say();
    func_800E37D4();                 // (the titles packed and the save written: the game's own way)
    save_removal_report();
}
void save_removal_frame(void) {
    const u16 frames = RD16(NM_MATCH_FRAMES);
    // (not the match frames: on the title they read 256, in the menus 0 or 0xFFFF -- NM_MODE 0 and the menus' loop say it)
    const int menus = RDS32(NM_MODE) == 0 && menus_running() && titles_code_here() && preset_code_here() &&
                      GAME_PTR(RD32(OVL_SAVE_IMAGE)) && RD32(OVL_SAVE_IMAGE) == RD32(SAVE_IMAGE_PTR);
    static int qa_done, said, diag;
    if (roster_removed_qa() && !said && !qa_done && ++diag % 600 == 0) {   // (QA: every 10 s till it's done, what it waits for)
        removal_s("qa waiting for the menus' code: mode "); removal_num(RDS32(NM_MODE)); removal_s(", match frames "); removal_num(frames);
        removal_s(", menus "); removal_num(menus_running()); removal_s(", titles code "); removal_num(titles_code_here()); removal_s(", preset code "); removal_num(preset_code_here());
        removal_s(", the save image "); removal_num(GAME_PTR(RD32(OVL_SAVE_IMAGE)) && RD32(OVL_SAVE_IMAGE) == RD32(SAVE_IMAGE_PTR));
        removal_s(", menus frames "); removal_num(menus_frames); removal_say();
    }
    if (!menus) { menus_frames = 0; return; }
    if (++menus_frames < 120) return;             // (two seconds of the menus: the save is in since boot)
    if ((roster_removed_qa() & 1) && !qa_done && roster_removed_n() <= 0) { qa_done = 1; qa_setup(); return; }
    // (QA qa_removal 2: the presets read from the save as the menus' presets page does -- the game fills RT_PRESETS only
    //  then, not at load -- so the log shows what a preset puts in the match; names into a buffer of ours)
    static u8 qa_names[0x40] __attribute__((aligned(8)));
    if ((roster_removed_qa() & 2) && !said) func_800E430C((u32)qa_names);
    if (roster_removed_n() > 0) {
        presets_stand_in();
        if (vacate_titles()) {
            removal_s("save: written with the titles vacated (the game's own titles-and-save function)"); removal_say();
            func_800E37D4();
        }
    }
    if (sc_vacate_write) {           // (no2k15: a title the sidecar's retired champion held, vacated -- save_sidecar_frame)
        sc_vacate_write = 0;
        removal_s("save: written with the titles vacated (the game's own titles-and-save function)"); removal_say();
        func_800E37D4();
    }
    if (!said) { said = 1; save_removal_report(); }   // (once: what the save names of him -- QA: every title -- the tables in)
}

void save_sanitize(void) {
    u32 img = RD32(SAVE_IMAGE_PTR);
    kept_image = img;
    if (!img) return;
    dbg_save_writes++;
    if (roster_removed_n() > 0 || (roster_removed_qa() & 2)) {   // (2026-11-13, removal: each write said, with what it keeps of him)
        removal_s("save: written (write "); removal_num((s32)dbg_save_writes); removal_s(")"); removal_say();
        save_removal_report();
    }
    for (u32 i = 0; i < sizeof(kept); i++) kept[i] = RD8(img + KEPT0 + i);
    if (roster_removed_n() > 0) presets_back_out(img);   // (2026-11-13, removal: a preset's stand-in goes out as the man he stands in for)
    for (u32 t = 0; t < 7; t++) {       // (the sidecar: whose titles are ours now, from the game's own title records)
        const u32 rec = TITLE_RECS + 0x34u * t;
        ours_now[t] = (u8)((ours(RD8(rec + 2)) ? 1 : 0) | (ours(RD8(rec + 3)) ? 2 : 0));
    }
    clean_titles(img);
    {   // the sidecar: each of ours as a champion, with his title's bytes as they go out
        int n = 0;
        for (u32 t = 0; t < 7; t++)
            for (u32 r = 0; r < 2; r++) {
                if (!(ours_now[t] & (1u << r))) continue;
                const u32 rec = TITLE_RECS + 0x34u * t, ord = RD8(rec + 2 + r);
                u8* e = sc_buf + 4 + 12 * n++;
                const int f = roster_folder_of_ord(ord);
                const u32 id = roster_ord_to_id((s32)ord), chk = title_check(img + TITLES + t * TITLE_SIZE);
                e[0] = 1; e[1] = (u8)t; e[2] = (u8)r; e[3] = RD8(rec + 4 + r);
                e[4] = (u8)((u32)f >> 24); e[5] = (u8)((u32)f >> 16); e[6] = (u8)((u32)f >> 8); e[7] = (u8)f;
                e[8] = (u8)(chk >> 24); e[9] = (u8)(chk >> 16); e[10] = (u8)(chk >> 8); e[11] = (u8)chk;
                if (f < 0) { e[4] = 0xFF; e[5] = 0xFE; e[6] = (u8)(id >> 8); e[7] = (u8)id; }   // (a built-in port: by game number)
            }
        for (u32 t = 0; t < 7; t++) if (ours_now[t]) for (u32 j = 0; j < TITLE_SIZE; j++) title_out[t][j] = RD8(img + TITLES + t * TITLE_SIZE + j);
        sc_buf[0] = 'S'; sc_buf[1] = 'C'; sc_buf[2] = 1; sc_buf[3] = (u8)n;
        plat_sidecar_put((u32)sc_buf, 4 + 12 * n);      // (none: the exe writes an empty one only if there was one)
        dbg_sidecar_out += (u32)n;
    }
    clean_presets(img);
    clean_story(img);
    story_save_swap(1);             // (a story of ours: what was in progress before it goes out, story.c)
}
void save_restore(void) {
    story_save_swap(0);
    u32 img = kept_image;
    if (!img) return;
    for (u32 i = 0; i < sizeof(kept); i++) RD8(img + KEPT0 + i) = kept[i];
    // (the sidecar: a title whose champion is ours stays in the image as it went out -- the reign the image can't hold
    //  left out -- so the image is the save's, and when the game makes its title records from it again (func_800E3664, as
    //  the menus come back) the sidecar's check still holds and gives the champion back. Before, the image kept his reign
    //  cut to 7 bits: somebody else's ordinal.)
    for (u32 t = 0; t < 7; t++) if (ours_now[t]) for (u32 j = 0; j < TITLE_SIZE; j++) RD8(img + TITLES + t * TITLE_SIZE + j) = title_out[t][j];
    kept_image = 0;
}

// The save was read (roster.c roster_pages_from_save, func_800E30DC: with the titles, func_800E3664): the sidecar is put
// back on the next frame (save_sidecar_frame, from roster_frame), when the game's title records are made from the image.
static int sc_read;                 // (the save has been read once: QA's crowning waits for it)
static int sc_len;                  // the sidecar as read (sc_buf), for the frames it's put back on
void save_sidecar_loaded(void) { sc_due = 1; sc_read = 1; }
int save_sidecar_read(void) { return sc_read; }
// (put back on each of 120 frames after the reading: the game makes its title records from the image in the same load,
//  before or after the pages -- whichever frame that is, the champion is ours once it's done; each time only while the
//  title's bytes in the image are still those that went out)
#define SC_FRAMES 120
// (no2k15, 2026-11-23) A sidecar champion the exe says was taken out of the game for good (FF FD <his old game number>: mod
// gen/retired_ids.json -- the two WWE 2K15 test slots, 0x011A and 0x011B, removed): his title is VACATED, the rule for a
// removed wrestler's title (vacate_titles above, the user's R3 of 2026-11-13) -- an empty reign in front, the reigns before
// it kept, no champion until someone wins it -- and the save is written so in the menus (save_removal_frame). His own reign
// can't be kept among the former ones: the save never held it (clean_titles), and he has no ordinal now.
static u8 sc_vacated;               // (the titles vacated so, a bit each: once per load)
int save_vacate_due(void) { return sc_vacate_write; }
// (each of the frames the sidecar is put back on: the game may make its title records from the image again in that time,
//  so it is done again whenever the current reign isn't the empty one -- the same result each time; said once)
static void sidecar_vacate(u32 t, u32 id) {
    const u32 rec = TITLE_RECS + 0x34u * t;
    if (RD8(rec + 2) || RD8(rec + 3)) {                   // (else vacated already -- or nobody held it before him: empty as it is)
        for (int i = 5; i > 0; i--) for (u32 j = 0; j < 8; j++) RD8(rec + 8u * (u32)i + j) = RD8(rec + 8u * (u32)i - 8u + j);
        for (u32 j = 0; j < 8; j++) RD8(rec + j) = 0;
        sc_vacate_write = 1;
    }
    if (sc_vacated & (1u << t)) return;
    sc_vacated |= (u8)(1u << t);
    sc_vacate_write = 1;                                  // (the save written once without him either way: its sidecar with it)
    removal_s("save: the "); removal_s(title_name[t]); removal_s(" title: "); removal_who(id);
    removal_s(" held it (the sidecar) and is taken out of the game -- VACATED: no champion until someone wins it");
    removal_say();
}
void save_sidecar_frame(void) {
    if (sc_due == 1) {
        sc_due = 2 + SC_FRAMES;
        sc_len = plat_sidecar_get((u32)sc_buf, (int)sizeof sc_buf);
        if (sc_len < 4 || sc_buf[0] != 'S' || sc_buf[1] != 'C' || sc_buf[2] != 1) sc_len = 0;
    }
    if (sc_due < 2) return;
    if (--sc_due == 1) sc_due = 0;
    const u32 img = RD32(SAVE_IMAGE_PTR);
    if (!img || !sc_len) return;
    for (int k = 0; k < sc_buf[3] && 4 + 12 * k + 12 <= sc_len; k++) {
        const u8* e = sc_buf + 4 + 12 * k;
        if (e[0] != 1 || e[1] >= 7 || e[2] >= 2) continue;
        const u32 chk = (u32)e[8] << 24 | (u32)e[9] << 16 | (u32)e[10] << 8 | e[11];
        if (title_check(img + TITLES + e[1] * TITLE_SIZE) != chk) continue;   // (the title changed since: the save's)
        if (e[4] == 0xFF && e[5] == 0xFD) {   // (no2k15: a champion taken out of the game for good -- the exe's sidecar_get)
            sidecar_vacate(e[1], (u32)e[6] << 8 | e[7]);
            continue;
        }
        s32 ord = 0;
        if (e[4] == 0xFF && e[5] == 0xFE) ord = roster_id_to_ord((u32)e[6] << 8 | e[7]);
        else ord = roster_ord_of_folder((int)((u32)e[4] << 24 | (u32)e[5] << 16 | (u32)e[6] << 8 | e[7]));
        if (ord <= 0) continue;
        const u32 rec = TITLE_RECS + 0x34u * e[1];
        if (RD8(rec + 2 + e[2]) == (u8)ord && RD8(rec + 4 + e[2]) == e[3]) continue;
        RD8(rec + 2 + e[2]) = (u8)ord;
        RD8(rec + 4 + e[2]) = e[3];
        dbg_sidecar_in++;
    }
}