// The arenas: the game's ten (venues 0-9), then the arcade's (gen/arenas.py ARENAS: WCW/nWo Revenge's Monday Nitro,
// Halloween Havoc, Bash at the Beach, WrestleMania 2000's RAW IS WAR ...) -- arena 10 + k is the arcade's k.
//
// Each arena is its own (mod/NOTES.md "Arenas restructured", 2026-11-01; the user: "Each arena is its own separate
// arena"). The game itself has ten ring venues (10 and up are its backstage scenes), so an arcade arena plays with the
// venue byte XARENA_BASE -- for the game's code paths only ("a ring venue"): everything the arena builders read per
// arena comes from the arena's own data, found through the globals below, which the patched builders read
// (gen/patches.py ARENA_LUIS / the stage set / the table / the collision):
// - arena_lists: the base of the arena's data block in the game's arena-data layout (0x80050000 + o: the ring, crowd,
//   truss, aprons, the per-venue entries, the ropes' colour). The game's arenas: 0x80050000, their own data, read in
//   place and never written. An arcade arena: its own block (xblock[k], filled once: arena_ring.c / arena_entrance.c),
//   every per-venue entry in it its own.
// - arena_close: the same for the entrances' close scene (its place 0x690 + 16 i, its tables 0x2280..): the arena's
//   block while its close-ups (scene index XARENA_BASE) are built, else the game's.
// - arena_set_mesh / _pal / _n / _z, arena_set_parts[ARENA_SET_MAX]: the stage set (the game's array 0x8007B8A0 had room
//   for 15), arena_set_built the parts made (what's freed).
// - arena_no_table: no announce table and no steps (the game's own no-table way: rules 0x100000 / the cage).
// - arena_zone_q, arena_floor_zones, arena_zone_fix[8]: the collision -- the arena's own walls (arena_ring.c), 0 / the
//   game's for the game's arenas and an arena with the table.
// They're set as an arena or a scene is built (func_8001B220's and func_8001B3E4's entry: arena_take / arena_scene,
// from arena_current()), and stay as they are until the next.
#if __has_include("modding.h")
#include "modding.h"
#define ARENA_HOOKS 1
#endif
#include "arcade.h"
#include "arena_files.h"
#include "arena_raw_files.h"
#include "arena_stage_files.h"
#include "arena_table_files.h"
#include "arena_walls_files.h"
// an arcade arena's own (arena_ring.c, arena_entrance.c)
void arena_ring_fill(u8* b, int k);
void arena_entrance_fill(u8* b, int k);
int arena_ring_set(int k, u32* mesh, u32* pal);
int arena_ring_table(int k);
void ringstage_take(int on);          // (arena_ringstage.c: RAW IS WAR's ring and stage one area)
void ringstage_rumble_start(int venue, int arcade);   // (arena_ringstage.c: where a Royal Rumble's entrants come in)

#define G_LISTS      0x80050000u   // the game's arena data (data/arenaport/NM_ARENAS.md)
#define G_SET_MESHES 0x800507FCu   // ptr [v]
#define G_SET_PALS   0x80050824u   // ptr [v]
#define G_SET_COUNT  0x8005084Cu   // s16 [v]
#define O_TRUSS_PAL  0x78Cu        // ptr [v]: the light truss and the two banners' palettes
#define ARENA_SET_MAX 64           // (the stage set's parts: the game's array had 15)
#define BLOCK_SIZE   0x2890u       // (0x80050000 .. 0x80052890: every offset the patched code reads)
#define F1500        0x44BB8000u   // 1500.0f: No Mercy's sets stand about the set's node moved 1500 along z
_Static_assert(XARENA_SET_MAX <= ARENA_SET_MAX, "gen/arenas.py SET_MAX: the arena's parts array");
_Static_assert(XRAW_SET_N <= ARENA_SET_MAX, "gen/arenas_raw.py: the arena's parts array");

// ---- read by the patched game functions (their names in gen/patches.py) -------------------------------------------
u32 arena_lists = G_LISTS, arena_close = G_LISTS;
u32 arena_set_mesh, arena_set_pal, arena_set_z = F1500;
s32 arena_set_n, arena_set_built, arena_no_table;
u32 arena_zone_q, arena_floor_zones;
// New feature (Ultimate No Mercy): the CPU's look ahead (func_8012D82C, a CPU walking to a spot: ai_plan_39_walk_to_spot)
// outside the ring -- the arena's own walls, every span of its four quadrants' lists (walls_take); 0: the game's own list
// (0x8014DA2C's row 0, 0x80147C44: its barricade 10..21 and apron). The game's has record 10 across the aisle at z 800, the old
// door to the stage area, and the old aisle's sides: on a stage that is one area with the ring, a CPU going up to his man past
// z 800 saw a wall there that isn't, turned aside 300 and back toward the ring, and came again -- shaking at the ramp's foot
// (the stages agent, 2026-11-20).
#define LOOK_N 32
static u32 look_list[LOOK_N];
u32 arena_zone_look;
// (step 4b: the zone lists the code names by address, the table top's record, the table-break relink -- the arena's own)
u32 arena_zone_top = 0x80147E20u, arena_zone_t28 = 0x80147E28u, arena_zone_t30 = 0x80147E30u, arena_zone_aisle = 0x80147408u;
u32 arena_zone_plane = 0x80147B7Cu, arena_rec_top = 0x80147E38u;
u32 arena_brk_at = 0x80147214u, arena_brk_val = 0x80147218u, arena_brk_zone = 0x8014CACCu;
static u32 brk_dummy;
u32 arena_zone_fix[8] = { 0x80147BB4u, 0x80147BC4u, 0x80147BD4u, 0x80147BE4u, 0x80147BF4u, 0x80147C04u, 0x80147C14u, 0x80147C24u };
static const u32 game_zone_fix[8] = { 0x80147BB4u, 0x80147BC4u, 0x80147BD4u, 0x80147BE4u, 0x80147BF4u, 0x80147C04u, 0x80147C14u, 0x80147C24u };
u8 arena_set_parts[ARENA_SET_MAX * 0x34] __attribute__((aligned(16)));
// RAW IS WAR's two fist screens animate, as the entrances' close scene's do (its pieces 0xAA5 / 0xAA6: a frame list 0x800526FC[0] /
// 0x80052790[0] of palette file numbers, count u16 0x80052738[0] / 0x800527CC[0], a frame every u8 0x80052828[0]): the part's own picture and
// palette, found in its record by their first words, take the frame's (xraw_fist_*: gen/arenas_raw.py). The set is the match's and the story's.
int arena_current(void);
static u32 fist_part_field(u32 part, u32 first) {
    for (u32 o = 0; o < 0x34u; o += 4u) {
        u32 p = RD32(part + o);
        if (GAME_PTR(p) && !(p & 3u) && RD32(p) == first) return p;
    }
    return 0;
}
void arena_raw_anim(void) {
    static s8 was[2] = { -1, -1 };
    if (arena_current() != 0 || arena_set_mesh != (u32)xraw_set_mesh || (s32)arena_set_built <= XRAW_FIST_R) { was[0] = was[1] = -1; return; }
    u32 rate = RD8(0x80052828u), t = RD32(NM_VI_COUNT) / 2u;
    if (!rate) rate = 4;
    static const u32 lists[2] = { 0x800526FCu, 0x80052790u }, counts[2] = { 0x80052738u, 0x800527CCu };
    static const u16 base[2] = { 0xE44, 0xE4C }, idx[2] = { XRAW_FIST_L, XRAW_FIST_R };
    for (u32 s = 0; s < 2; s++) {
        u32 n = RD16(counts[s]), l = RD32(lists[s]);
        if (!n || n > 32 || !GAME_PTR(l)) continue;
        l = RD32(l);
        if (!GAME_PTR(l)) continue;
        u32 id = RD16(l + 2u * ((t / rate) % n));            // (the frame's palette file)
        if (id < base[s] || id >= (u32)base[s] + 8u) { id = RD32(l + 4u * ((t / rate) % n)); }
        if (id < base[s] || id >= (u32)base[s] + 8u) continue;
        s32 k = (s32)(id - base[s]) / 2 + (s ? 4 : 0);
        if (was[s] == (s8)k) continue;
        u32 part = (u32)arena_set_parts + 0x34u * idx[s];
        u32 tex = fist_part_field(part, 0x1F3F0010u), pal = fist_part_field(part, 0xFFFF0843u);
        if (!tex || !pal) continue;
        for (u32 b = 0; b < 1032u; b++) RD8(tex + b) = xraw_fist_tex[k][b];
        for (u32 b = 0; b < 32u; b++) RD8(pal + b) = xraw_fist_pal[k][b];
        was[s] = (s8)k;
    }
}
u32 dbg_arena_builds;              // tests: arena builds taken (arena_take)
// what the game reads by the venue byte outside the arena data (step 4, gen/arenas.py write_tables): the nameplate font
// (func_8000A470: u16 [v % 10]), ovl_b's close-shot bytes (func_80109F88: u8 [3 v]) -- the game's tables for its arenas,
// an arcade arena's own (every venue's slot its own value) while it's in play (arena_tabs); the announce table / steps
// lists (func_8001DB4C, 8001DF38, 8001E488, 8001B0B8: 0x8005004C.., 0x800500B8..) -- each arena's own table block
u32 arena_font_tab = 0x80045980u, arena_close_tab = 0x801257B8u, arena_table = G_LISTS;
// (step 5: ovl_b's entrance effect sets -- func_8011D2F8's table 0x80132CC0 --, ovl_c's VS-screen pieces -- func_80116194's
//  0x801497FC[v] -- and func_8011649C's two tables 0x80149D68 / 0x80149E80 [row][v]: the arena's own while it's in play)
u32 arena_fx_tab = 0x80132CC0u, arena_vs_ptrs = 0x801497FCu, arena_vs_a = 0x80149D68u, arena_vs_b = 0x80149E80u;
static u16 xfont10[XARENA_N][10]; static u8 xclose30[XARENA_N][30]; static int xtabs_made;
static u32 xvs_ptr10[XARENA_N][10]; static u8 xvs_own[XARENA_N][0x24] __attribute__((aligned(4)));
static u32 xvs_a70[XARENA_N][70], xvs_b70[XARENA_N][70];
int arena_current(void); int arena_count(void);
static void arena_tabs(void) {
    if (!xtabs_made) {
        for (int k = 0; k < XARENA_N; k++) for (int v = 0; v < 10; v++) {
            xfont10[k][v] = xarena_font[k];
            for (int c = 0; c < 3; c++) xclose30[k][3 * v + c] = xarena_close_bytes[k][c];
            xvs_ptr10[k][v] = (u32)xvs_own[k];
            for (int r = 0; r < 7; r++) { xvs_a70[k][10 * r + v] = xvs_rows_a[r]; xvs_b70[k][10 * r + v] = xvs_rows_b[r]; }
        }
        for (int k = 0; k < XARENA_N; k++) for (int b = 0; b < 0x24; b++) xvs_own[k][b] = xvs_struct[b];
        xtabs_made = 1;
    }
    int a = arena_current();
    if (a >= 10 && a < arena_count()) {
        int k = a - 10;
        arena_font_tab = (u32)xfont10[k]; arena_close_tab = (u32)xclose30[k]; arena_fx_tab = (u32)xfx_tab[k];
        arena_vs_ptrs = (u32)xvs_ptr10[k]; arena_vs_a = (u32)xvs_a70[k]; arena_vs_b = (u32)xvs_b70[k];
    } else {
        arena_font_tab = 0x80045980u; arena_close_tab = 0x801257B8u; arena_fx_tab = 0x80132CC0u;
        arena_vs_ptrs = 0x801497FCu; arena_vs_a = 0x80149D68u; arena_vs_b = 0x80149E80u;
    }
}

static u8 xblock[XARENA_N][BLOCK_SIZE] __attribute__((aligned(16)));
static u8 xblock_made[XARENA_N];
static u8* block(int k) {
    if (!xblock_made[k]) {
        arena_ring_fill(xblock[k], k);
        arena_entrance_fill(xblock[k], k);
        for (u32 v = 0; v < 10; v++) *(u32*)(xblock[k] + O_TRUSS_PAL + 4u * v) = (u32)xarena_truss_pal[k];
        xblock_made[k] = 1;
    }
    return xblock[k];
}

static int a_pick = -1;              // the arena chosen (-1: the game's venue, as it is)
static int a_in;                     // the arcade's arena in play (0: the game's; 1.. the arcade's k + 1)
static int left_menus;                // (the game left the menus since the arena was chosen)

// ---- arenas from folders ----------------------------------------------------------------------------------
// An arena from a folder (exe src/main/arcade_arenas.cpp) comes after the built-in ones in the arena numbers. All the game's
// code and ours see arena_current(), which answers with the arena it's based on: so every table and rule is that arena's
// while it plays. Two things make it its own: its name and label on the ARENA page, and arena_file below, which gives its
// files in place of the game's while it's in play.
typedef struct { s32 based_on; u32 has_label; char name[28]; } FolderArena;   // (Filled by the exe: arena_get.)
#define FOLDER_ARENAS_MAX (256 - 10 - XARENA_N)      // (pg_list's entries are a byte each.)
#define FOLDER_LABEL_FILE0 0x7000u                    // Free file IDs, one per folder arena, for their labels.
extern int plat_arena_folder_count(void);
extern void plat_arena_folder_get(int i, u32 dst);
extern u32 plat_arena_folder_file(int i, u32 id, u32 size_at);
static FolderArena folder_arenas[FOLDER_ARENAS_MAX];
static int folder_arenas_n = -1;
static int folder_arena_count(void) {
    if (folder_arenas_n < 0) {
        int n = plat_arena_folder_count();
        if (n > FOLDER_ARENAS_MAX) n = FOLDER_ARENAS_MAX;
        for (int i = 0; i < n; i++) {
            plat_arena_folder_get(i, (u32)&folder_arenas[i]);
            // One based on a number that isn't a built-in added arena would read outside every table: treat it as the first.
            if (folder_arenas[i].based_on < 10 || folder_arenas[i].based_on >= 10 + XARENA_N) folder_arenas[i].based_on = 10;
        }
        folder_arenas_n = n;
    }
    // (2026-11-14) 0..FOLDER_ARENAS_MAX in the game (the lines above); kept to it where it is read, so that no arena number
    //  made from it runs past folder_arenas or past an int -- both undefined in C, and the compiler took its ranges from them.
    //  Nothing changes in the game.
    return folder_arenas_n < FOLDER_ARENAS_MAX ? folder_arenas_n : FOLDER_ARENAS_MAX;
}
// The built-in arena an arena number plays as: itself, unless it's from a folder.
static int arena_plays_as(int a) {
    // (2026-11-14: the folder arena's number counted in unsigned, which wraps where an int's overflow is undefined -- the compiler
    //  turned `a - 23 >= 0` into `a >= 23`, which an a near the int's least value makes differ. The same i for every arena.)
    const int i = (int)((u32)a - (10u + XARENA_N));
    return i >= 0 && i < folder_arena_count() ? folder_arenas[i].based_on : a;
}
// The file `file` (an added one) for the arena in play, or its label for the ARENA page: its size, and the bytes put in
// dest if there is one. 0: not this arena's, use the game's.
u32 arena_file(u32 file, u32 dest) {
    int i = -1;
    u32 id = file;
    if (file >= FOLDER_LABEL_FILE0 && file < FOLDER_LABEL_FILE0 + FOLDER_ARENAS_MAX) { i = (int)(file - FOLDER_LABEL_FILE0); id = 0xFFFFu; }
    else if (a_in > XARENA_N) i = a_in - 1 - XARENA_N;
    if (i < 0 || i >= folder_arena_count()) return 0;
    u32 size = 0;
    const u32 from = plat_arena_folder_file(i, id, (u32)&size);
    if (!from) return 0;
    if (dest) for (u32 k = 0; k < size; k++) RD8(dest + k) = RD8(from + k);
    return size;
}

int arena_count(void) { return 10 + XARENA_N + folder_arena_count(); }
// (firstrun 2026-11-17) Is arena a one that can be played? The arcade's own (10 .. 10 + XARENA_N - 1) only with the built-in
// extras (extras.c): without them their tables are empty. Their numbers stay theirs either way, so a folder's arena keeps its
// number.
// (arenasplit 2026-11-24) The arcade's arena 10 + k: only when its source game is in (phase3: its source games -- each is its
// own game's now, extras.c), and only when its row was filled (its label file, read as the mod's image has it, not as the
// compiler knew it), so an arena is never offered with its tables empty.
static int xarena_there(int k) {
    return arcade_extras_has(arcade_arena_needs(k)) && *(volatile const u16*)&xarena_label_file[k] != 0;
}
static int arena_there(int a) {
    if (a < 0 || a >= arena_count()) return 0;
    if (a < 10) return 1;
    // (arenasplit) an arena from a folder plays as the built-in one it is based on: there when that one is
    if (a >= 10 + XARENA_N) return xarena_there(arena_plays_as(a) - 10);
    return xarena_there(a - 10);
}
static int xarenas_all_there(void) {
    for (int k = 0; k < XARENA_N; k++) if (!arena_there(10 + k)) return 0;
    return 1;
}
// An arena drawn at random from r: any arena there is. With all the built-in extras, r % arena_count() as it always was.
int arena_random(u32 r) {
    if (xarenas_all_there()) return (int)(r % (u32)arena_count());
    if (!arcade_extras()) {
        const int i = (int)(r % (u32)(10 + folder_arena_count()));
        return i < 10 ? i : i + XARENA_N;
    }
    // (phase3: some of the arcade's arenas only) the n-th of the arenas there
    int n = 0;
    for (int a = 0; a < arena_count(); a++) n += arena_there(a);
    int want = (int)(r % (u32)n);
    for (int a = 0; a < arena_count(); a++) if (arena_there(a) && want-- == 0) return a;
    return 0;
}
// the game's venue byte arena a plays with
int arena_venue(int a) { return a >= 10 && a < arena_count() ? XARENA_BASE : (a >= 0 && a < 10 ? a : 0); }
// the arena for the next match (the venue written by whoever chose: arena_venue(a))
void arena_choose(int a) {
    a_pick = arena_there(a) ? a : -1; left_menus = 0;   // (firstrun: arena_there -- with the extras, a >= 0 && a < arena_count() as before)
    a_in = a_pick >= 10 ? a_pick - 9 : 0;
    arena_tabs();
}
int arena_picked(void) { return a_pick; }
// the arena in play: 0-9 the game's (its venue), 10.. the arcade's
int arena_current(void) { return a_in ? arena_plays_as((int)(9u + (u32)a_in)) : (int)RD8(NM_VENUE); }   // (2026-11-14: unsigned, as above)
const char* arena_name(int a) {
    if (a < 10 || a >= arena_count()) return 0;
    return a < 10 + XARENA_N ? xarena_names[a - 10] : folder_arenas[a - (10 + XARENA_N)].name;
}
int arena_has_table(void) { return !arena_no_table; }    // (announce.c: JR and King only at a table)

// the arena's own walls in (gen/arenas_walls.py: every arena's, the game's ten and the arcade's) -- the table's side of
// the barricade linked back as it is with the table and its top's floor zone put back (a break in the last match
// relinked / cleared them: func_8012DCC0 case 2, arena_brk_*)
static void walls_take(int a) {
    if (a < 0 || a >= XWALLS_N) return;
    const XWalls* w = xwalls[a];
    arena_zone_q = (u32)w->zq; arena_floor_zones = w->fz;
    for (int i = 0; i < 8; i++) arena_zone_fix[i] = w->fix[i];
    arena_zone_top = w->top; arena_zone_t28 = w->t28; arena_zone_t30 = w->t30; arena_zone_aisle = w->aisle;
    arena_zone_plane = w->plane; arena_rec_top = w->rec_top;
    *(u32*)w->brk_at = w->linked;
    if (w->brk_zone) *(u32*)w->brk_zone = w->brk_span;
    arena_brk_at = w->brk_at; arena_brk_val = w->brk_val; arena_brk_zone = w->brk_zone ? w->brk_zone : (u32)&brk_dummy;
    // New feature (Ultimate No Mercy): the CPU's look ahead, the same walls (arena_zone_look above): each span once
    u32 n = 0;
    for (int q = 0; q < 4; q++)
        for (const u32* l = (const u32*)w->zq[q]; *l && n < LOOK_N - 1; l++) {
            u32 k = 0;
            while (k < n && look_list[k] != *l) k++;
            if (k == n) look_list[n++] = *l;
        }
    look_list[n] = 0;
    arena_zone_look = n ? (u32)look_list : 0;
}
// the ring arena of venue v about to be built (func_8001B220 -> 8001B548, or the ring made again in place at the
// entrances' end: func_8001B3E4 -> 8001C62C / 8001B548): its data
// Changed for new features (Ultimate No Mercy): the stages agent, 2026-11-20 -- the ring-and-stage map of venue v's ring
// world, by itself (arena_ringstage.c ringstage_area_change asks it of the start venue, from backstage)
int arena_ringstage_of(int v) {
    int a = arena_current(), k = a >= 10 && v == XARENA_BASE ? a - 10 : -1;
    return k >= 0 && k < XARENA_N ? 10 + k : v >= 0 && v < 10 ? v : -1;
}
static void arena_take(int v) {
    int a = arena_current(), k = a >= 10 && v == XARENA_BASE ? a - 10 : -1;
    dbg_arena_builds++;
    ringstage_take(arena_ringstage_of(v));   // (each arena's ring and stage one area)
    ringstage_rumble_start(XARENA_BASE, k >= 0 && k < XARENA_N);                   // (2026-11-15: an arcade arena's entrants from its own back)
    if (k >= 0 && k < XARENA_N) {
        arena_lists = (u32)block(k); arena_table = (u32)xtable_block[10 + k];
        u32 m = 0, p = 0;
        arena_set_n = arena_ring_set(k, &m, &p); arena_set_mesh = m; arena_set_pal = p;
        arena_set_z = 0;                          // (Revenge's pieces where Revenge has them: the node not moved)
        arena_no_table = !arena_ring_table(k);
        walls_take(10 + k);
        return;
    }
    arena_lists = G_LISTS;
    arena_table = v >= 0 && v < 10 ? (u32)xtable_block[v] : G_LISTS;
    arena_set_z = F1500; arena_no_table = 0;
    if (v >= 0 && v < 10) walls_take(v);
    else {                                            // (venue 30, the ring alone: the game's own zones)
        arena_zone_q = 0; arena_floor_zones = 0; arena_zone_look = 0;
        for (int i = 0; i < 8; i++) arena_zone_fix[i] = game_zone_fix[i];
        arena_zone_top = 0x80147E20u; arena_zone_t28 = 0x80147E28u; arena_zone_t30 = 0x80147E30u; arena_zone_aisle = 0x80147408u;
        arena_zone_plane = 0x80147B7Cu; arena_rec_top = 0x80147E38u;
        arena_brk_at = 0x80147214u; arena_brk_val = 0x80147218u; arena_brk_zone = 0x8014CACCu;
    }
    // its stage set: the mod's for RAW IS WAR and No Mercy's other arenas (their close sets at the old sets' place:
    // gen/arenas_raw.py, gen/arenas_stages.py), else the game's own entry (read, as the game does)
    if (v == 0) { arena_set_mesh = (u32)xraw_set_mesh; arena_set_pal = (u32)xraw_set_pal; arena_set_n = XRAW_SET_N; }
    else if (v > 0 && v < XSTAGE_N && xstage_mesh[v]) {
        arena_set_mesh = (u32)xstage_mesh[v]; arena_set_pal = (u32)xstage_pal[v]; arena_set_n = xstage_n[v];
    } else {
        u32 vv = (u32)(v & 0xFF);
        arena_set_mesh = RD32(G_SET_MESHES + 4u * vv); arena_set_pal = RD32(G_SET_PALS + 4u * vv); arena_set_n = RDS16(G_SET_COUNT + 2u * vv);
    }
}
// A game arena's close scene with pieces of its own besides the game's (gen/arenas_stages.py NEW: Survivor Series'
// Titantron, in its extra list): its own close block -- its scene index's entries its own close scene's (the game's
// lists of its pieces, read: the main list, its animated pieces, single models, place, rates), the extra list the
// arena's new pieces. Only that index is read from it (arena_scene). The game's tables are never written.
#define O_CLOSE_EXTRA     0x2354u   // ptr[]: the extra list's meshes; 0x2390 ptr[] palettes; 0x23CC s16[] count
#define O_CLOSE_EXTRA_PAL 0x2390u
#define O_CLOSE_EXTRA_N   0x23CCu
#define O_CLOSE_EXTRA_OFF 0x23ECu   // float[3] per scene (12 bytes)
static const struct { u16 o; u8 size; } close_entries[] = {   // (every table the close scene's code reads by index)
    { 0x2280, 4 }, { 0x22BC, 4 }, { 0x22F8, 4 }, { 0x2334, 2 }, { 0x2354, 4 }, { 0x2390, 4 }, { 0x23CC, 2 },
    { 0x24A0, 4 }, { 0x24DC, 4 }, { 0x2518, 2 }, { 0x2538, 4 }, { 0x2574, 4 }, { 0x25B0, 2 }, { 0x2684, 4 },
    { 0x26C0, 4 }, { 0x26FC, 4 }, { 0x2738, 2 }, { 0x2758, 4 }, { 0x2790, 4 }, { 0x27CC, 2 }, { 0x27EC, 4 }, { 0x2828, 1 } };
#ifdef XSTAGE5_CLOSE_N
static u8 gclose5[BLOCK_SIZE] __attribute__((aligned(16)));
static int gclose5_made;
static u32 game_close_block(int v) {
    if (v != 5) return 0;
    u8* b = gclose5; const u32 i = 5;
    if (!gclose5_made) {
        for (u32 e = 0; e < sizeof close_entries / sizeof close_entries[0]; e++) {
            u32 o = close_entries[e].o + close_entries[e].size * i;
            for (u32 c = 0; c < close_entries[e].size; c++) b[o + c] = RD8(G_LISTS + o + c);
        }
        for (u32 c = 0; c < 16; c++) b[0x690u + 16u * i + c] = RD8(G_LISTS + 0x690u + 16u * i + c);   // (its place)
        *(u32*)(b + O_CLOSE_EXTRA + 4u * i) = (u32)xstage5_close_mesh;
        *(u32*)(b + O_CLOSE_EXTRA_PAL + 4u * i) = (u32)xstage5_close_pal;
        *(s16*)(b + O_CLOSE_EXTRA_N + 2u * i) = XSTAGE5_CLOSE_N;
        for (u32 c = 0; c < 3; c++) *(float*)(b + O_CLOSE_EXTRA_OFF + 12u * i + 4u * c) = 0.0f;
        gclose5_made = 1;
    }
    return (u32)b;
}
#else
static u32 game_close_block(int v) { (void)v; return 0; }
#endif
// an "other scene" (s >= 10: index s - 10) about to be built: the arena's own close scene for its venue's close-ups
// (an arcade arena: index XARENA_BASE while it's in play; a game arena with pieces of its own: its index), else the game's
static void arena_scene(int s) {
    int a = arena_current(), k = a >= 10 && s - 10 == XARENA_BASE ? a - 10 : -1;
    u32 g = a < 10 && s - 10 == a && RD8(NM_VENUE) == (u8)a ? game_close_block(a) : 0;
    arena_close = k >= 0 && k < XARENA_N ? (u32)block(k) : g ? g : G_LISTS;
}
void arena_on_build(s32 venue) {   // func_8001B220's entry (the arena's build: the venue, or a scene >= 10)
    s16 v = (s16)venue;
    if (v >= 10 && v != 30) arena_scene(v); else arena_take(v);
}
void arena_on_scene(s32 scene) {   // func_8001B3E4's entry (the entrances: the close scene, then the ring again)
    s16 s = (s16)scene;
    if (s >= 10) arena_scene(s); else arena_take(s);
}
#ifdef ARENA_HOOKS
RECOMP_HOOK("func_8001B220") void arena_hook_build(s32 venue) { arena_on_build(venue); }
RECOMP_HOOK("func_8001B3E4") void arena_hook_scene(s32 scene) { arena_on_scene(scene); }
#endif

// ---- the Versus ARENA page (menu screen 5, ovl_c; data/arenaport/ARENA_PAGE.md) past ten rows -------------------------
// Its ten rows are a window on the list (the arenas the game shows -- its unlocked ones, packed by func_8012D614 --
// then ours): row s is list[top + s], labelled 0x6500 + s, its label picture and photo the ovl_c tables' entry s (only
// func_80124698 reads them: written while the page is up, the game's own back when it's left). Down on the bottom
// row / up on the top one move the window while there's more that way (pg_links). The game
// takes a row as label & 0xF into NM_SEL_ARENA (A: then the page's memory 0x80158156; Start: on to the venue byte
// through func_80135EE8): the same frame that row is made the arena's venue in all of them, and the arena chosen.
#define PG_LABEL_FILES   0x80148044u   // u16[10]: a row's label picture by label & 0xFF
#define PG_PREVIEW_FILES 0x80148058u   // u16[10][6]: the photo's six tiles by the cursor row's label & 0xF
#define PG_BACKDROP      0x80148E08u   // u16: the VS / loading backdrop's first file, venue XARENA_BASE's (0x80148DE0[13 + 7])
#define PG_CURSOR        0x8015897Cu   // s16
#define PG_DEFS          0x8014C10Cu   // the rows' definitions (0x16 each: up link +6, down +7)
#define PG_ROWS          0x8015898Cu   // u32: the rows (0x88 each: +0x4A flags, +0x60 label)
#define PG_MEMORY        0x80158156u   // s8: the page's remembered choice
#define NM_SEL_ARENA     0x800A946Fu
#define NM_VENUE_COPY    0x800BD5A0u
#define PG_ROWS_N        10
static const u16 game_preview0[10] = { 0x4641, 0x4665, 0x463B, 0x466B, 0x4635, 0x464D, 0x4659, 0x4647, 0x4653, 0x465F };
#define GAME_LABEL0      0x462Bu
#define GAME_BACKDROP_WM 0x47A5u       // (0x468D + 0x28 x 7)

// The arenas the ARENA page can scroll through. Entries are bytes, so 256 is as many as this list can name.
static u8 pg_list[256]; static int pg_n, pg_top, pg_in, pg_cur_prev;
static int pg_confirm = -1;          // the row A / Start was pressed on (pad hook) -> the arena, this frame
static int pg_memory = -1;           // the arena last chosen on the page (venue 7 alone can't tell ours from WrestleMania)

// the top row's up link and the bottom one's down: themselves while the window can move that way (else the wrap)
static void pg_links(int ours) {
    RD8(PG_DEFS + 6u) = (u8)(ours && pg_top > 0 ? 0 : PG_ROWS_N - 1);
    RD8(PG_DEFS + 0x16u * (PG_ROWS_N - 1) + 7u) = (u8)(ours && pg_top + PG_ROWS_N < pg_n ? PG_ROWS_N - 1 : 0);
}
static void pg_tables(int ours) {
    for (int s = 0; s < PG_ROWS_N; s++) {
        int a = ours && pg_top + s < pg_n ? pg_list[pg_top + s] : s;
        // A folder arena shows its base's preview; its label is its own if it has one.
        const int base = arena_plays_as(a);
        u16 label = a < 10 ? (u16)(GAME_LABEL0 + a) : xarena_label_file[base - 10];
        if (base != a && folder_arenas[a - (10 + XARENA_N)].has_label) label = (u16)(FOLDER_LABEL_FILE0 + (u32)(a - (10 + XARENA_N)));
        u16 prev = a < 10 ? game_preview0[a] : xarena_preview_file[base - 10];
        RD16(PG_LABEL_FILES + 2u * s) = label;
        for (u32 k = 0; k < 6; k++) RD16(PG_PREVIEW_FILES + 12u * s + 2u * k) = (u16)(prev + k);
    }
}

// every frame: menus_on -- ovl_c is in (the title's signature); on_page -- the ARENA page up and set up
void arena_page_frame(int menus_on, int on_page) {
    if (!menus_on) { pg_in = 0; return; }        // (the overlay is gone: its data comes back from the ROM with it)
    // the VS / loading screens' backdrop: ours while one of ours is chosen (they show venue XARENA_BASE's)
    RD16(PG_BACKDROP) = a_pick >= 10 ? xarena_backdrop_file[arena_plays_as(a_pick) - 10] : GAME_BACKDROP_WM;
    if (pg_confirm >= 0) {                        // (the game took the row as an arena this frame: it's the venue)
        int s = pg_confirm; pg_confirm = -1;      // (A leaves the page that frame: whatever page is up now)
        if (pg_top + s < pg_n) {
            int a = pg_list[pg_top + s];
            u8 v = (u8)arena_venue(a);
            RD8(NM_SEL_ARENA) = v; RDS8(PG_MEMORY) = (s8)v;
            if (RD8(NM_VENUE) == (u8)s) RD8(NM_VENUE) = v;
            if (RD8(NM_VENUE_COPY) == (u8)s) RD8(NM_VENUE_COPY) = v;
            arena_choose(a); pg_memory = a;
        }
    }
    if (!on_page) {
        if (pg_in) { pg_tables(0); pg_links(0); pg_in = 0; }
        return;
    }
    u32 rows = RD32(PG_ROWS);
    if (!GAME_PTR(rows)) return;
    if (!pg_in) {
        // the list: the rows the game showed (its arenas as it has them), then ours
        pg_n = 0;
        for (u32 r = 0; r < PG_ROWS_N; r++) {
            u32 row = rows + 0x88u * r;
            if (!(RD16(row + 0x4A) & 1) && (RD16(row + 0x60) & 0xFF00u) == 0x6500u) pg_list[pg_n++] = (u8)(RD16(row + 0x60) & 0xF);
        }
        for (int k = 0; k < XARENA_N && pg_n < (int)sizeof pg_list; k++) if (arena_there(10 + xarena_page_order[k])) pg_list[pg_n++] = (u8)(10 + xarena_page_order[k]);   // (gen/arenas.py: WM 2000's, then Revenge's; firstrun: only with the built-in extras)
        for (int k = 0; k < folder_arena_count() && pg_n < (int)sizeof pg_list; k++) pg_list[pg_n++] = (u8)(10 + XARENA_N + k);   // (Arenas from folders, after the rest.)
        // the cursor on the arena chosen last (ours by the mod's memory, the game's by its venue)
        int want = pg_memory >= 0 ? pg_memory : RDS8(PG_MEMORY) >= 0 ? RDS8(PG_MEMORY) : RD8(NM_SEL_ARENA);
        int at = 0;
        for (int i = 0; i < pg_n; i++) if (pg_list[i] == want) at = i;
        pg_top = at >= PG_ROWS_N ? at - (PG_ROWS_N - 1) : 0;
        for (u32 s = 0; s < PG_ROWS_N; s++) {
            u32 row = rows + 0x88u * s;
            if ((int)s < pg_n) { RD16(row + 0x60) = (u16)(0x6500u + s); RD16(row + 0x4A) &= 0xFFF0u; }
            else RD16(row + 0x4A) |= 1u;
        }
        RDS16(PG_CURSOR) = (s16)(at - pg_top); pg_cur_prev = at - pg_top;
        pg_in = 1;
    }
    // the window. The cursor follows the rows' links (func_8010BE34: the page's definitions, 0x16 each, up +6, down +7;
    // it moves by func_8010C71C, so it can't just be written): while there's more of the list below the bottom row its
    // down link is itself and down there moves the window; the top row's up link the same; at the list's ends the
    // game's own wrap (bottom -> top row, top -> bottom), the window going to the other end with it
    int cur = RDS16(PG_CURSOR);
    u16 pr = RD16(NM_PRESSED);
    if (pg_n > PG_ROWS_N) {
        if (pg_cur_prev == PG_ROWS_N - 1 && cur == 0) pg_top = 0;                       // (wrapped down)
        else if (pg_cur_prev == 0 && cur == PG_ROWS_N - 1) pg_top = pg_n - PG_ROWS_N;   // (wrapped up)
        // (on the edge row already: the press that brought the cursor there isn't a scroll too)
        else if (cur == PG_ROWS_N - 1 && pg_cur_prev == cur && (pr & BTN_DD) && pg_top + PG_ROWS_N < pg_n) pg_top++;
        else if (cur == 0 && pg_cur_prev == cur && (pr & BTN_DU) && pg_top > 0) pg_top--;
    }
    pg_links(pg_n > PG_ROWS_N);
    pg_cur_prev = cur;
    pg_tables(1);
}

// the pad hook on the ARENA page: the row A / Start is pressed on (the game takes it this frame)
void arena_page_pad(u16* ob, u16* oe, int ports) {
    if (!pg_in) return;
    u16 pr = RD16(NM_PRESSED);
    for (int i = 0; i < ports; i++) pr |= oe[i];
    if (pr & (BTN_A | BTN_START)) pg_confirm = RDS16(PG_CURSOR);
}

void arena_frame(void) {
    // (back in the menus or at the title after the match it was chosen for: the next one's chosen again)
    if (RDS32(NM_MODE) != 0) left_menus = 1;
    else if (left_menus) { a_pick = -1; a_in = 0; left_menus = 0; }
    arena_tabs();
}
