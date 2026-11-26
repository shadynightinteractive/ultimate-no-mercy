// Test-rig platform layer: the harness writes the keyboard state into RDRAM and the
// trampolines in tramp.S call into the shared arcade code.
#include "arcade.h"

#define TEST_KEYS 0x807F8000u   // u8[256], 1 = held (SDL scancodes)
#define TEST_CFG  0x807F8100u   // u32[16] overrides; 0xFFFFFFFF = use the default

int plat_key_down(int scancode) {
    if (scancode < 0 || scancode > 255) return 0;
    return RD8(TEST_KEYS + (u32)scancode) != 0;
}

static int cfg_index(const char* k) {
    // tiny fixed table: must match the harness
    static const char* names[] = { "free_play", "continue_secs", "arena", "attract_demos", "fast_getup", "selftest", "rumble_entrants", "rumble_interval", "arcade_speed", "speed_boost", "big_show", "titantron", "nmplus_media", "new_superstars", "unlock_all", "coin_sound", 0 };   // (rumble_ai: the default)
    for (int i = 0; names[i]; i++) {
        const char* a = names[i]; const char* b = k;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;
}

u32 plat_config(const char* key, u32 fallback) {
    int i = cfg_index(key);
    if (i < 0) return fallback;
    u32 v = RD32(TEST_CFG + 4u * (u32)i);
    return v == 0xFFFFFFFFu ? fallback : v;
}

void plat_config_set(const char* key, u32 value) {
    int i = cfg_index(key);
    if (i >= 0) RD32(TEST_CFG + 4u * (u32)i) = value;
}

// ovl_a's demo setup (func_800EF478) is reloaded with the overlay: patch its return each time.
#define DEMO_SETUP_SIG_ADDR 0x800EF478u
#define DEMO_SETUP_SIG      0x27BDFF90u
#define DEMO_SETUP_JR       0x800EF750u   // jr $ra (delay slot: addiu $sp, $sp, 0x70)
#define J_DEMO_TRAMP        0x081C0004u   // j 0x80700010

// ovl_a's story page wait (func_800E8A14): patched to call the arcade first.
#define PAGE_WAIT_ADDR 0x800E8A14u
#define PAGE_WAIT_SIG0 0x27BDFFE8u    // addiu $sp, $sp, -0x18
#define PAGE_WAIT_SIG1 0x24020001u    // addiu $v0, $zero, 1
#define J_PAGE_TRAMP   0x081C0006u    // j 0x80700018

// ovl_c's sprite draw (func_80117A84): patched to call the arcade first.
#define SPRITES_ADDR 0x80117A84u
#define SPRITES_SIG0 0x27BDFFD0u      // addiu $sp, $sp, -0x30
#define SPRITES_SIG1 0xAFB40028u      // sw $s4, 0x28($sp)
#define J_SPRITES_TRAMP 0x081C0008u   // j 0x80700020

// The single-player Royal Rumble's hooks: a function's first two instructions -> j <tramp>; nop
// (or, func_801199F0, j to the arcade's version of it). Overlays come back from the ROM when they
// load, so the patches are checked again each frame and each controller read.
typedef struct { u32 addr, sig0, sig1, jump; } TestPatch;
static TestPatch rr_patches[] = {
    { 0x801199F0u, 0x00803821u, 0x3C048009u, 0 },            // ovl_e: who goes to a Rumble slot -> arcade_rr_entrant
    { 0x8011D400u, 0x3C028009u, 0x8C427EC0u, 0x081C000Au },  // ovl_e: the Rumble's entrance manager (0x80700028)
    { 0x80115000u, 0x3C028017u, 0x9042B598u, 0x081C000Cu },  // ovl_e: the match's set-up (0x80700030)
    { 0x800DEDE0u, 0x3C038016u, 0x94638E98u, 0x081C000Eu },  // ovl_e: the HUD update (0x80700038)
    { 0x80136784u, 0x27BDFFE0u, 0xAFBF0018u, 0x081C0010u },  // ovl_c: the match set up from the menus (0x80700040)
    { 0x800D9DFCu, 0x0C045145u, 0x00000000u, 0x0C1C0012u },  // ovl_e: the match loop's jal func_80114514 -> jal 0x80700048
    // Big Show (ovl_a)
    { 0x800E299Cu, 0x27BDFFD8u, 0xAFB00018u, 0x081C0014u },  // the save packs a costume record (0x80700050)
    { 0x800E2AC0u, 0x27BDFFE0u, 0xAFB10014u, 0x081C0016u },  // the save packs a moveset (0x80700058)
    { 0x800EC554u, 0x3C038009u, 0x94637EECu, 0x081C0018u },  // Story Mode: a part's wrestler (0x80700060)
    { 0x800EBF70u, 0x00041040u, 0x00441021u, 0x081C001Au },  // Story Mode: a challenger (0x80700068)
    { 0x800EC268u, 0x27BDFF90u, 0x3084000Fu, 0x081C001Cu },  // Story Mode: a random pick (0x80700070)
    { 0x800EE3ECu, 0x27BDFFC8u, 0x00803021u, 0x081C001Eu },  // Story Mode: a Royal Rumble's entrants (0x80700078)
    { 0x8002B0C0u, 0x27BDFFE8u, 0xAFB00010u, 0x081C0022u },  // (resident) the audio DMA callback, replaced (0x80700088)
    { 0x80005EFCu, 0x27BDFFE0u, 0xAFB00010u, 0x081C0028u },  // (resident) a moveset is unpacked (0x807000A0)
    { 0x8013CBACu, 0x27BDFFA0u, 0xAFB20040u, 0x081C002Au },  // ovl_e: the match unpacks a moveset (0x807000A8)
};
// The roster (roster.c): functions replaced outright (first two instructions -> j <C function>)
extern u32 roster_ord_to_id(s32); extern s32 roster_id_to_ord(u32); extern u32 roster_record(s32, s32);
extern u32 roster_moves(s32); extern void roster_build_record(u32, s32, u32); extern void roster_load_moves(u32, u32);
extern void roster_build_all(void); extern void roster_build_pages(void); extern void roster_pages_from_save(void);
extern void roster_pages_to_save(void); extern void roster_save_one(u32); extern void roster_pack_one(u32);
extern void roster_copy_saved(u32); extern void roster_stats_alloc(void);
extern void files_info(u32, u32); extern u32 files_read(u32, u32, u32); extern s32 roster_entrance_song(u32, s32); extern u32 roster_entrance_fx(u32, s32); extern s32 media_play_sound(s32);
#include "skel.h"
typedef struct { u32 addr, sig0, sig1; void* fn; } FnPatch;
#ifdef ARCADE_MOVEX
// func_800EC698(wrestler, the owner's slot, entry), in C -- with, for tests, entries dbg_ec_from..dbg_ec_to
// of slot dbg_ec_slot's moveset taken as entry dbg_ec_as (a computer player made to do his special)
s32 dbg_ec_slot = -1, dbg_ec_from, dbg_ec_to, dbg_ec_as; u32 dbg_ec_hits;
static void test_ec698(u32 w, s32 o, s32 e) {
    o = (s8)o; e = (s16)e;
    if (o == dbg_ec_slot && e >= dbg_ec_from && e <= dbg_ec_to) { e = dbg_ec_as; dbg_ec_hits++; }
    RD16(w + 0x30E) = RD16(w + 0x30C);
    RD16(w + 0x306) = (RDS16(w + 0x308) == e) ? (u16)(RD16(w + 0x306) + 1) : 1;
    RD8(w + 0x304) = (u8)o;
    u32 rec = 0x8051E2A0u + 19728u * (u32)o + 36u * (u32)e;
    RD32(w + 0x310) = rec;
    RD16(w + 0x308) = (u16)e;
    RD16(w + 0x30A) = RD16(0x80509A80u + 1096u * (u32)o + 2u * (u32)e);
    RD16(w + 0x30C) = RD16(0x805146F0u + 1096u * (u32)o + 2u * (u32)e);
    if (RD32(rec + 0x20) & 0x8000u) RD16(w + 0x8C) |= 1; else RD16(w + 0x8C) &= (u16)~1u;
    u32 t = RD32(w + 0x2E4);
    if (RDS16(w + 0x308) == RDS16(t + 0x1C) || RDS16(w + 0x308) == RDS16(t + 0x20)) RD16(w + 0x8C) |= 2; else RD16(w + 0x8C) &= (u16)~2u;
    if (RDS16(w + 0x308) == 0x46 || RDS16(w + 0x308) == 0x51) RD16(w + 0x8C) |= 4; else RD16(w + 0x8C) &= (u16)~4u;
}
#endif
u32 dbg_video_calls[32], dbg_video_n;
static s32 test_video_select(s32 v) { dbg_video_calls[dbg_video_n++ & 31] = (u32)v; return media_video_select(v); }
static s32 test_down_anim(u32 w) { return arcade_down_anim(w, 1); }
#include "patches.inc"
static FnPatch fn_patches[] = {
    { 0x8000AE38u, 0x2482FFACu, 0x2C420012u, (void*)roster_ord_to_id },
    { 0x8000AE78u, 0x30830F00u, 0x24020B00u, (void*)roster_id_to_ord },
    { 0x8000B4B8u, 0x2483FFACu, 0x2C620012u, (void*)roster_record },
    { 0x8000AAF4u, 0x3C028004u, 0x8C427E90u, (void*)roster_entrance_song },   // music slots (roster.c, media.c)
    { 0x8000AB64u, 0x27BDFFE8u, 0xAFBF0010u, (void*)roster_entrance_fx },
    { 0x800037ECu, 0x3C028004u, 0x84423626u, (void*)media_play_sound },
    { 0x8000B560u, 0x2482FFACu, 0x2C420012u, (void*)roster_moves },
    { 0x8000B834u, 0x27BDFFE0u, 0xAFB00010u, (void*)roster_build_record },
    { 0x8000B914u, 0x27BDFFC0u, 0xAFB00030u, (void*)roster_load_moves },
    { 0x8000B9B0u, 0x27BDFFD8u, 0xAFBF0024u, (void*)roster_build_all },
    { 0x8000B004u, 0x27BDFFD8u, 0xAFB00010u, (void*)roster_build_pages },
    { 0x800E30DCu, 0x27BDFFD0u, 0xAFB40020u, (void*)roster_pages_from_save },   // ovl_a
    { 0x800E3020u, 0x3C02800Fu, 0x8C426134u, (void*)roster_pages_to_save },
    { 0x800E31C4u, 0x27BDFFD8u, 0xAFB3001Cu, (void*)roster_save_one },
    { 0x800E3330u, 0x27BDFFD8u, 0xAFB3001Cu, (void*)roster_pack_one },
    { 0x800E5A44u, 0x27BDFFE0u, 0xAFB10014u, (void*)roster_copy_saved },
    { 0x800E4450u, 0x3C02800Fu, 0x8C42612Cu, (void*)roster_stats_alloc },
    { 0x80003BD0u, 0x30A2FFFFu, 0x2C430001u, (void*)files_info },   // the game's files (files.c)
    { 0x80003C94u, 0x27BDFFD0u, 0xAFB20028u, (void*)files_read },
    { 0x8001AC44u, 0x27BDFFE0u, 0x00801021u, (void*)parts_unpack },   // body parts (parts.c)
    { 0x80015D8Cu, 0x27BDFF90u, 0xA7A5002Eu, (void*)skel_setup },     // a model's skeleton (skel.c)
    { 0x80014F5Cu, 0x27BDFFD8u, 0xAFB20018u, (void*)skel_visible },   // ... which of its parts are drawn
    { 0x80014E44u, 0x00803821u, 0x24060001u, (void*)skel_show },      // ... shown whole (the menus)
    { 0x800DD740u, 0x27BDFFE8u, 0x00802821u, (void*)story_card_text },  // story text (story.c): ovl_a
    { 0x8010CC38u, 0x27BDFFE8u, 0xAFBF0010u, (void*)story_scene_text }, // ... ovl_b's scenes
    { 0x800EE1F0u, 0x3C028009u, 0x8C427F14u, (void*)story_branch },     // ... the branch after a match
    { 0x800ED130u, 0x000410C0u, 0x00441021u, (void*)story_set_node },   // ... the current node
    { 0x80016464u, 0x27BDFF88u, 0xAFB40060u, (void*)skel_textures },  // ... its textures
    { 0x80016D2Cu, 0x3C028005u, 0x8C4284ECu, (void*)hands_frame },    // ... its hands (hands.c)
    { 0x80002268u, 0x27BDFFE8u, 0xAFB00010u, (void*)scene_world },    // the scene's world matrices, checked (gameplay.c)
#ifdef ARCADE_MOVEX
    { 0x80130878u, 0x8482030Au, 0x00021040u, (void*)movex_anim },     // ovl_e: the animation of the move he's in (movex.c)
    { 0x800EC698u, 0x00803821u, 0x94E2030Cu, (void*)test_ec698 },     // a wrestler goes into a moveset entry (tests: redirected)
    { 0x80011A54u, 0x3C028007u, 0x8C42A220u, (void*)anim_frames },     // an animation's header and record (movex.c)
    { 0x80011A78u, 0x3C028007u, 0x8C42A220u, (void*)anim_speed },
    { 0x80011AC0u, 0x3C028007u, 0x8C42A220u, (void*)anim_endpose },
    { 0x80011AE8u, 0x00042400u, 0x00042403u, (void*)anim_pose_now },
    { 0x80011BD8u, 0x27BDFFE0u, 0xAFB10014u, (void*)anim_details },
    { 0x80011D30u, 0x3C05FFF9u, 0x34A58D7Cu, (void*)anim_recpart },
    { 0x80011340u, 0x27BDFFD0u, 0x00041C00u, (void*)anim_stream },
#endif
    { 0x8010C998u, 0x0481003Au, 0x27BDFFF8u, (void*)test_video_select },   // ovl: the entrance's video (media.c)
    { 0x8013FEF4u, 0x94820080u, 0x30420010u, (void*)test_down_anim },
    { 0x8011723Cu, 0x00803021u, 0x3C020003u, (void*)arcade_elim_check },   // ovl_e: a slot is eliminated -> the match is over   // ovl_e: a knocked-down wrestler's lying animation
    ASMPATCHES      // gen/patches.py: the game's functions with a few instructions changed
};
void test_moves_pre(u32 m, u32 s, u32 a, u32 o) { roster_moves_pre(m, s, o); }
void test_moves_post(void) { roster_match_moves_post(); }   // (the menus' unpacking too: movex does nothing there)
u32 dbg_mm[8][2], dbg_mm_n;
void test_match_moves_pre(u32 slot, u32 m, u32 bits) { dbg_mm[dbg_mm_n & 7][0] = slot; dbg_mm[dbg_mm_n++ & 7][1] = m; roster_match_moves_pre((s32)slot, m, bits); }
// func_800EE3EC's stand-in for the player (addiu $a1, $zero, 0x201 in a delay slot): bigshow_stand_in
#define RUMBLE_STAND_IN 0x800EE480u
#define RUMBLE_STAND_IN_SIG 0x24050000u
void test_file_pre(u32 file, u32 dest) { media_file_pre(file & 0xFFFFu, dest); }   // (unused: files.c reads them)
void test_file_post(void) { media_file_post(); }
// The content pack: in the test ROM at 0x02000000 (the rig's ROM is the game's with it appended), read
// from the cartridge a word at a time (uncached).
// WrestleMania 2000's Superstars' files: in the test ROM at WMPORT_ROM (the rig's ROM is the game's with
// them put there: harness/rig.py)
#define WMPORT_ROM 0x03E00000u   // (past the content pack: test/mkrom.py)
u32 dbg_pack_copy;   // tests: 1 = the pack's animations copied as the PC does
void plat_guard(u32 at) { (void)at; }   // (the test ROM's packs are on the cartridge)
int plat_entrance_media(u32 id, u32 variant, int on) { (void)id; (void)variant; (void)on; return 0; }   // (2026-11-11: no native media in the rig)
int plat_wrestler_slots(int i, u32 dst) { (void)i; (void)dst; return 0; }   // (2026-11-13, movesets: no folder wrestlers in the rig)
void plat_moves_note(u32 what, u32 id, u32 a, u32 b) { (void)what; (void)id; (void)a; (void)b; }
// (2026-11-13, movelib: no folder moves in the rig; its library count is the mod's own)
int plat_move_own(u32 id, u32 out) { (void)id; (void)out; return 0; }
u32 plat_move_lib_n(void) { extern u32 movex_lib_n(void); return movex_lib_n(); }
// (2026-11-13, movelib2: the library is the exe's content file now -- none in the rig: no library moves there)
u32 plat_move_lib_stamp(void) { return 0; }
int plat_wrestler_edit(int i, int k, u32 dst) { (void)i; (void)k; (void)dst; return 0; }
int plat_anim_meta(u32 file, u32 dst) { (void)file; (void)dst; return 0; }
u32 plat_anim_rom(u32 file) { (void)file; return 0; }
void plat_anim_read(u32 rom, u32 dst, u32 len) { (void)rom; (void)dst; (void)len; }
void plat_movex_where(u32 files, u32 slots_entries, u32 players, u32 players_stride) { (void)files; (void)slots_entries; (void)players; (void)players_stride; }
u32 plat_wmport_rom(u32 off) { return dbg_pack_copy ? 0 : WMPORT_ROM + off; }
void plat_wmport_read(u32 off, u32 dst, u32 len) {
    u32 a = 0xB0000000u + WMPORT_ROM + off;
    for (u32 i = 0; i < len; i++) {
        u32 w = *(volatile u32*)((a + i) & ~3u);
        RD8(dst + i) = (u8)(w >> (8 * (3 - ((a + i) & 3))));
    }
}
void plat_content_read(u32 off, u32 dst, u32 len) {
    u32 a = 0xB2000000u + off;
    for (u32 i = 0; i < len; i++) {
        u32 w = *(volatile u32*)((a + i) & ~3u);
        RD8(dst + i) = (u8)(w >> (8 * (3 - ((a + i) & 3))));
    }
}
// func_80002FA4's calls of func_80002BB4 (the frame's list starts) and func_800028B4 (the scene):
// jal -> these, which call the game's own and then the arcade (as the runtime's hooks do).
#define FRAME_LIST_JAL 0x80002FC8u
#define SCENE_JAL      0x80002FF4u
typedef u32 (*fn_bb4)(u32, s32);
typedef s32 (*fn_28b4)(u32, s32, s32);
static u32 test_frame_list(u32 picture, s32 clear) {
    dl_big_begin();
    draw_frame_begin(picture);
    tron_frame_begin(picture);
    return ((fn_bb4)0x80002BB4u)(picture, clear);
}
static s32 test_scene(u32 gfxp, s32 a1, s32 a2) {
    tron_scene_begin(gfxp, a1, a2);
    s32 r = ((fn_28b4)0x800028B4u)(gfxp, a1, a2);
    tron_scene_end(gfxp);
    draw_scene_end(gfxp);
    return r;
}
#define FRAME_END_JAL 0x80003020u   // (func_80002FA4's call of func_80002DE4: the list ends, the task sent)
typedef void (*fn_de4)(u32, u32, s32, s32);
static void test_frame_end(u32 a0, u32 head, s32 a2, s32 a3) {
    dl_big_end();
    ((fn_de4)0x80002DE4u)(a0, head, a2, a3);
    dl_big_done();
}
#define JAL(f) (0x0C000000u | (((u32)(f) >> 2) & 0x03FFFFFFu))
// ovl_e func_80118368's call of the computer player's step (func_800F03F0): then the arcade's Rumble AI
#define CPU_JAL 0x801186A8u
static void test_cpu(s32 slot) { ((void (*)(s32))0x800F03F0u)(slot); arcade_rumble_ai(slot); }
void test_apply_patches(void) {
    if (RD32(FRAME_LIST_JAL) == 0x0C000AEDu) RD32(FRAME_LIST_JAL) = JAL(test_frame_list);
    if (RD32(SCENE_JAL) == 0x0C000A2Du) RD32(SCENE_JAL) = JAL(test_scene);
    if (RD32(FRAME_END_JAL) == 0x0C000B79u) RD32(FRAME_END_JAL) = JAL(test_frame_end);
    if (RD32(CPU_JAL) == 0x0C03C0FCu) RD32(CPU_JAL) = JAL(test_cpu);
    if (RD32(0x800007A4u) == 0x03E00008u) RD32(0x800007A4u) = 0x081C002Cu;   // j 0x807000B0 (overlay loaded)
    rr_patches[0].jump = 0x08000000u | (((u32)arcade_rr_entrant >> 2) & 0x03FFFFFFu);
    for (u32 i = 0; i < sizeof(rr_patches) / sizeof(rr_patches[0]); i++) {
        TestPatch* p = &rr_patches[i];
        if (RD32(p->addr) == p->sig0 && RD32(p->addr + 4) == p->sig1) { RD32(p->addr) = p->jump; RD32(p->addr + 4) = 0; }
    }
    for (u32 i = 0; i < sizeof(fn_patches) / sizeof(fn_patches[0]); i++) {
        FnPatch* p = &fn_patches[i];
        if (RD32(p->addr) == p->sig0 && RD32(p->addr + 4) == p->sig1) {
            RD32(p->addr) = 0x08000000u | (((u32)p->fn >> 2) & 0x03FFFFFFu); RD32(p->addr + 4) = 0;
        }
    }
    if (RD32(0x800EE3ECu) == 0x081C001Eu && (RD32(RUMBLE_STAND_IN) & 0xFFFF0000u) == RUMBLE_STAND_IN_SIG)
        RD32(RUMBLE_STAND_IN) = RUMBLE_STAND_IN_SIG | bigshow_stand_in();
}

u32 dbg_ring[64]; u32 dbg_ring_i;
u32 dbg_fcall_addr, dbg_fcall_arg; s32 dbg_fcall_ret;   // tests: a game function called at the frame hook
void test_frame_hook(u32 task) {
    { u32 i = (dbg_ring_i++ & 15) * 4; dbg_ring[i] = task; dbg_ring[i+1] = RD32(task + 0xC); dbg_ring[i+2] = RD32(task + 0x40); dbg_ring[i+3] = RD32(0x80037E30) ; }
    if (RD32(DEMO_SETUP_SIG_ADDR) == DEMO_SETUP_SIG && RD32(DEMO_SETUP_JR) == 0x03E00008u)
        RD32(DEMO_SETUP_JR) = J_DEMO_TRAMP;
    if (RD32(SPRITES_ADDR) == SPRITES_SIG0 && RD32(SPRITES_ADDR + 4) == SPRITES_SIG1) {
        RD32(SPRITES_ADDR) = J_SPRITES_TRAMP;
        RD32(SPRITES_ADDR + 4) = 0;
    }
    if (RD32(PAGE_WAIT_ADDR) == PAGE_WAIT_SIG0 && RD32(PAGE_WAIT_ADDR + 4) == PAGE_WAIT_SIG1) {
        RD32(PAGE_WAIT_ADDR) = J_PAGE_TRAMP;
        RD32(PAGE_WAIT_ADDR + 4) = 0;
    }
    test_apply_patches();
    { extern void dbg_texlog_frame(u32); dbg_texlog_frame(task); }
    if (dbg_fcall_addr) { u32 a = dbg_fcall_addr; dbg_fcall_addr = 0; dbg_fcall_ret = ((s32 (*)(u32))a)(dbg_fcall_arg); }
    arcade_frame(task);
}

void test_step_hook(void) { arcade_match_step(); }

void test_page_hook(void) {
    if (arcade_story_page()) RD16(NM_PRESSED) |= BTN_A;
}


// Camera: the harness points func_80001580's call of the 4x4 multiply (0x80001590) here.
typedef void (*mtx_mul_fn)(float*, float*, float*);
float dbg_zoom[3];   // tests: S, Tx, Ty: clip x' = S x + Tx w (a picture of part of the screen)
float dbg_rot[4];    // tests: the world turned by sin, cos (dbg_rot[0..1]) about the vertical through x, z (2, 3)
float dbg_view[16], dbg_proj[16];
void test_camera_mul(float* view, float* proj, float* out) {
    for (int i = 0; i < 16; i++) { dbg_view[i] = view[i]; dbg_proj[i] = proj[i]; }
    ((mtx_mul_fn)0x80001E08u)(view, proj, out);
    arcade_camera(proj, out);
    if (dbg_rot[0] != 0.0f || dbg_rot[1] != 0.0f) {
        // v' = v x M, M = T(-p) Ry T(p): x' = c x + s z + px - c px - s pz, z' = -s x + c z + pz + s px - c pz
        float sn = dbg_rot[0], cs = dbg_rot[1], px = dbg_rot[2], pz = dbg_rot[3];
        float m[16] = { cs, 0, -sn, 0,  0, 1, 0, 0,  sn, 0, cs, 0,
                        px - cs * px - sn * pz, 0, pz + sn * px - cs * pz, 1 };
        float t[16];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
            float a = 0; for (int k = 0; k < 4; k++) a += m[i * 4 + k] * out[k * 4 + j];
            t[i * 4 + j] = a;
        }
        for (int i = 0; i < 16; i++) out[i] = t[i];
    }
    if (dbg_zoom[0] != 0.0f)
        for (int i = 0; i < 4; i++) {
            float w = out[i * 4 + 3];
            out[i * 4 + 0] = dbg_zoom[0] * out[i * 4 + 0] + dbg_zoom[1] * w;
            out[i * 4 + 1] = dbg_zoom[0] * out[i * 4 + 1] + dbg_zoom[2] * w;
        }
}

// Game functions (see arcade.h's GF_*): called at their N64 addresses.
static const u32 game_fn[] = {
    0x8012D2ECu, 0x8012D720u, 0x8012D614u, 0x8013DA18u, 0x80110FD0u, 0x80132EB4u, 0x80003714u,
    0x80133DF8u, 0x8013437Cu, 0x8014227Cu, 0x80143390u, 0x80135534u, 0x80133CD8u, 0x8010F0A8u,
    0x80134140u, 0x80135728u, 0x80136784u, 0x8010D874u, 0x80136CC8u,
    0, 0x80118A20u, 0x800EC484u, 0x8012EA9Cu, 0x80114514u, 0x800159FCu, 0x8000B60Cu,
    0x8000AEC8u, 0x80134830u, 0x8002B12Cu, 0x800DF5DCu,
    0x801202F4u, 0x800EB6D4u,
    0x800DD6F4u, 0x8010CC10u, 0x80138360u, 0x800ED958u, 0x800EDD84u, 0x800EDE48u, 0x800EE084u, 0x80017C64u,
};
s32 plat_game_call(int fn, u32 a0, u32 a1, u32 a2) {
    if (fn < 0 || fn >= (int)(sizeof(game_fn) / sizeof(game_fn[0]))) return -1;
    if (fn == GF_RR_ENTRANT) return arcade_rr_entrant((s32)a0);
    return ((s32 (*)(u32, u32, u32, u32))game_fn[fn])(a0, a1, a2, 0);
}

u32 plat_text_sprite(const char* s, u32 style) {
    return ((u32 (*)(u32, u32))0x800DF3BCu)((u32)s, style);
}
void plat_sprite_request(u32 id, s32 x, s32 y, u32 style, u32 flags) {
    ((void (*)(u32, s32, s32, u32, u32))0x800DD5A4u)(id, x, y, style, flags);
}
u32 plat_game_text(const char* s, u32 style) { return ((u32 (*)(u32, u32))0x80009F6Cu)((u32)s, style); }
void plat_game_text_free(u32 rec) { ((void (*)(u32))0x8000A1BCu)(rec); }
void plat_sound(u32 id) { ((u32 (*)(u32))0x800173C0u)(id); }

// Controllers (the exe's on the PC): none in the rig, but a learnt layout can be checked
char dbg_pad_learn[2][16]; int dbg_pad_held[2];
u32 dbg_pad_bits[2]; char dbg_pad_applied[15 * 16]; int dbg_pad_apply_n, dbg_pad_swaps, dbg_quit;
int plat_pad_name(int player, char* buf, int len) {
    const char* n = dbg_pad_held[player & 1] >= 0 ? "TEST PAD" : "NONE";
    int i = 0;
    for (; n[i] && i < len - 1; i++) buf[i] = n[i];
    buf[i] = 0;
    return dbg_pad_held[player & 1] >= 0 ? 2 : 0;
}
u32 plat_pad_buttons(int player) { return dbg_pad_bits[player & 1]; }
int plat_pad_learn_start(int player) { return 1; }
int plat_pad_learn_read(int player, char* buf, int len) {
    int i = 0;
    if (dbg_pad_held[player & 1] > 0) for (; dbg_pad_learn[player & 1][i] && i < len - 1; i++) buf[i] = dbg_pad_learn[player & 1][i];
    if (len > 0) buf[i] = 0;
    return dbg_pad_held[player & 1] > 0;
}
int plat_pad_learn_apply(int player, char* el) { for (int i = 0; i < 15 * 16; i++) dbg_pad_applied[i] = el[i]; dbg_pad_apply_n++; return 1; }
void plat_pad_swap(void) { dbg_pad_swaps++; }
void plat_quit(void) { dbg_quit++; }
// the N64 controls' bindings (the exe's on the PC): none in the rig -- every row "NONE", a capture cancelled at once
int dbg_bind_starts, dbg_bind_clears, dbg_bind_resets;
int plat_bind_name(int dev, int row, char* buf, int len) {
    const char* n = "NONE";
    int i = 0;
    for (; n[i] && i < len - 1; i++) buf[i] = n[i];
    if (len > 0) buf[i] = 0;
    return 0;
}
int plat_bind_start(int dev, int row) { dbg_bind_starts++; return dev == 0 ? 1 : 0; }
int plat_bind_poll(void) { return 2; }
void plat_bind_clear(int dev, int row) { dbg_bind_clears++; }
void plat_bind_reset(int dev) { dbg_bind_resets++; }
int plat_bind_stick(int dev, int which) { return 0; }

void story_test_frame(void);
void test_pad_hook(void) {
    test_apply_patches();
    story_test_frame();
    arcade_roster_service(1);
    arcade_pad_hook();
}

// Commentary: the rig keeps what the mod says (the harness reads dbg_comm_log; sim plays it)
typedef struct { u32 vi; s16 ev, a, b, pad; s32 arg; } CommLog;
CommLog dbg_comm_log[1024];
u32 dbg_comm_n, dbg_comm_cfg;
void plat_comm_event(int ev, int a, int b, int arg) {
    CommLog* e = &dbg_comm_log[dbg_comm_n++ & 1023];
    e->vi = RD32(NM_VI_COUNT); e->ev = (s16)ev; e->a = (s16)a; e->b = (s16)b; e->arg = arg;
}
int plat_comm_tick(int in_match) { return 0; }
void plat_comm_config(int on, int volume, int talk) { dbg_comm_cfg = (u32)on | (u32)volume << 8 | (u32)talk << 16; }
void plat_comm_reset(void) { CommLog* e = &dbg_comm_log[dbg_comm_n++ & 1023]; e->vi = RD32(NM_VI_COUNT); e->ev = -1; e->a = e->b = e->arg = 0; }
int plat_comm_available(void) { return 1; }
