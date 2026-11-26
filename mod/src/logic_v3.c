// WWF No Mercy Arcade (v0.3): the real game, run as a coin-op.
//
//   ATTRACT  title + CPU demo matches; INSERT COIN / PRESS START. Nothing reaches the game.
//   PLAY     coin + Start: the game's own menus, trimmed to one page with two items:
//              Versus (the game's Exhibition) and Story Mode (its Championship).
//            Only the players who paid have working controllers. Player 2 joins any time with
//            a coin + Start. A player who loses a match gets CONTINUE? 10..0 over the game; coin +
//            Start carries on right where the game is. When nobody continues: GAME OVER, back
//            to the title.
//
// Also: the entrances skip the full-screen Titantron video (the music keeps playing), the
// stage walk gets a low camera looking up at the wrestler, and in matches wrestlers get up
// sooner after being knocked down.
#include "arcade.h"
#include "roster.h"

#define SIG_INTRO_ADDR 0x80106AB0u
#define SIG_INTRO      0x3C028016u
#define SIG_TITLE_ADDR 0x80111158u
#define SIG_TITLE      0x27BDFFB8u

// menu screens (0x800457A4)
#define SCR_TITLE 0
#define SCR_TRANS_A 1
#define SCR_MULTIPLAY 2
#define SCR_SINGLEPLAY 3
#define SCR_COMMISSIONER 4
#define SCR_PLAYERS 6
#define SCR_CHAMPIONSHIP 22  // the title list
#define SCR_STORY_CARD 23    // chapter card
#define SCR_STORY_SELECT 26  // superstar select (also Exhibition's first select)
#define SCR_STORY_SELECT2 27
#define NM_SELECT_HOVER 0x80076264u // u16: wrestler shown at the superstar select (under the cursor)
#define NM_STORY_BELT   0x800A9579u // u8: the championship picked (0 World Heavyweight, 6 Women's)
#define BELT_HEAVYWEIGHT 0
#define BELT_WOMENS 6
#define SCR_RESULTS 29
#define SCR_RULES 21
#define SCR_TRANS_B 41
#define SCR_MATCH 42

// The main menu pages' item tables: screen n uses NM_MENU_TABLE + (n - 2) * 8 = { items*, u8 count }.
// Each item is 22 bytes: x, y (s16), ?, u8 up, u8 down, ..., u16 label id, ..., u16 action.
#define NM_MENU_TABLE      0x8014DFD8u
#define NM_MULTIPLAY_ITEMS 0x8014BEE0u
#define ITEM_SIZE 22

enum { ATTRACT, PLAY, GAMEOVER };
enum { DRV_NONE, DRV_ENTER, DRV_ROUTE, DRV_TO_HUB, DRV_TO_TITLE };
enum { GM_NONE, GM_STORY, GM_VERSUS };

static int phase = ATTRACT;
static int phase_frames;
static u32 frame_no;
static int credits;
static int booted;
static int intro_frames;
static u32 last_vi;
static int credit_flash;     // frames left showing CREDITS after a coin

// players: 0 = P1 (controller 1), 1 = P2 (controller 2)
static int active[2];
static int port_player[2] = { 0, 1 }; // which player is the game's controller 1 / 2
static int need_p2;
static int route_mode;          // where DRV_ROUTE takes the players
static int game_mode;           // what they're playing: GM_STORY / GM_VERSUS
static int hub_pending;         // go back to the main menu (and on) once the current match is over
static int challenger_frames;           // frames left of the "2P INSERT COIN" hint
static int cont_player = -1; // player on the CONTINUE? countdown
static int cont_frames;
static int lost_pending[2];  // lost and waiting for their turn on the countdown
static int match_armed;      // all slots were in: a new match is running
static int match_lost[2];    // this match's loss was already counted

// operator settings (F2)
static int op_open, op_cursor;
static int free_play, continue_secs, attract_demos, fast_getup, selftest, selftest_done;
static int settings_loaded;

// input
static u16 pad_prev[4], pad_latch[4];
static u8 key_prev[256 / 8], key_latch[256 / 8];
static const u8 watched_keys[] = { SC_1, SC_2, SC_5, SC_6, SC_F2, SC_UP, SC_DOWN, SC_LEFT, SC_RIGHT, SC_RETURN };

// menu driver (runs in the controller hook)
int drv_goal;
int dbg_line;
#define DRV_SET(v) do { drv_goal = (v); dbg_line = __LINE__; } while (0)
static s16 drv_screen;
static s32 drv_mode;
static int drv_stable;
static int drv_hold;
static u16 drv_button;
static int drv_step;
static int drv_wait;

static u32 rng_state = 0x1234567u;
static u32 rng(void) {
    rng_state = rng_state * 1103515245u + 12345u + frame_no;
    return rng_state >> 8;
}

static void set_phase(int p) { phase = p; phase_frames = 0; }

static void load_settings(void) {
    free_play = (int)plat_config("free_play", 0);
    continue_secs = (int)plat_config("continue_secs", 10);
    if (continue_secs < 5 || continue_secs > 30) continue_secs = 10;
    attract_demos = (int)plat_config("attract_demos", 1) != 0;
    fast_getup = (int)plat_config("fast_getup", 1) != 0;
    selftest = (int)plat_config("selftest", 0) != 0;
    settings_loaded = 1;
}

// ---- game state helpers --------------------------------------------------------------------
static int title_loaded(void) { return RD32(SIG_TITLE_ADDR) == SIG_TITLE; }
static int on_title(void) { return title_loaded() && RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == SCR_TITLE; }
static int intro_running(void) {
    return RD32(SIG_INTRO_ADDR) == SIG_INTRO && RDS32(NM_MODE) == 0 && RDS32(NM_INTRO_SCENE) == 0 &&
           RDS32(NM_INTRO_SKIPPABLE) != 0;
}
static int in_menus(void) { return title_loaded() && RDS32(NM_MODE) == 0; }
static u8* menu_cursor(int screen) { return (u8*)(NM_MENU_RECORDS + (u32)screen * NM_MENU_STRIDE + 5); }
static int match_running(void) { return RDS32(NM_MODE) == -2 && RD16(NM_MATCH_FRAMES) != 0xFFFF; }
static int nobody_playing(void) { return !active[0] && !active[1]; }

static int blink(int period) { return (frame_no / (u32)period) & 1; }
static int pay(void) {
    if (free_play) return 1;
    if (credits <= 0) return 0;
    credits--;
    credit_flash = 180;
    return 1;
}
static int can_start(void) { return free_play || credits > 0; }

// ---- the main menu: one page, Versus + Story Mode ----------------------------------------------
// The page's item table lives in the title overlay; it's patched before the page is built (the
// item count is read then), every frame while the overlay is loaded.
static void patch_menu_table(void) {
    if (!title_loaded()) return;
    if (RD32(NM_MENU_TABLE) != NM_MULTIPLAY_ITEMS) return;
    RD8(NM_MENU_TABLE + 4) = 2;                       // Exhibition, Royal Rumble (relabelled)
    RD8(NM_MULTIPLAY_ITEMS + 6) = 1;                  // item 0: up / down -> item 1
    RD8(NM_MULTIPLAY_ITEMS + 7) = 1;
    RD8(NM_MULTIPLAY_ITEMS + ITEM_SIZE + 6) = 0;      // item 1: up / down -> item 0
    RD8(NM_MULTIPLAY_ITEMS + ITEM_SIZE + 7) = 0;
}

// The labels are CI4 textures (grey-ramp palette) the page loads into RDRAM; found by their
// checksum among the textures the frame's display list loads, then rewritten in place.
#include "menu_tex.h"
#define CK_EXHIBITION   0x1bbd1847u  // 96 x 32, 0x600 bytes
#define CK_ROYALRUMBLE  0xcd91d9e9u  // 128 x 32, 0x800 bytes
#define CK_SINGLEPLAY   0x95a844beu  // page indicators: blanked
#define CK_COMMISSIONER 0x612d4f09u
#define CK_ICON_A       0x47c173c1u  // the L / R button icons beside them (16 x 16, 0x80 bytes)
#define CK_ICON_B       0x5f3c33a5u

static u32 checksum(u32 a, u32 n) {
    u32 s = 0, x = 0;
    for (u32 i = 0; i < n; i += 4) {
        u32 w = RD32(a + i);
        s += w;
        x = (x << 5) | (x >> 27);
        x ^= w;
    }
    return s ^ x;
}

static void copy_tex(u32 dst, const unsigned char* src, u32 n) {
    for (u32 i = 0; i < n; i += 4)
        RD32(dst + i) = ((u32)src[i] << 24) | ((u32)src[i + 1] << 16) | ((u32)src[i + 2] << 8) | src[i + 3];
}
static void clear_tex(u32 dst, u32 n) { for (u32 i = 0; i < n; i += 4) RD32(dst + i) = 0; }

static void menu_texture(u32 addr) {
    if (addr < 0x80000400u || addr >= 0x80780000u) return;
    if (addr & 3) return;
    u32 c6 = checksum(addr, 0x600);
    if (c6 == CK_EXHIBITION) { copy_tex(addr, tex_versus, 0x600); return; }
    u32 c8 = checksum(addr, 0x800);
    if (c8 == CK_ROYALRUMBLE) { copy_tex(addr, tex_story, 0x800); return; }
    if (c8 == CK_SINGLEPLAY || c8 == CK_COMMISSIONER) { clear_tex(addr, 0x800); return; }
    u32 c1 = checksum(addr, 0x80);
    if (c1 == CK_ICON_A || c1 == CK_ICON_B) clear_tex(addr, 0x80);
}

// ---- going to a menu screen the game's way -------------------------------------------------
// The menus' main loop (func_80111158) changes screen by running that screen's set-up (its own
// table, 0x80154918) and then storing the screen number. Doing just the store leaves the screen
// unbuilt, so this does both.
static void menu_goto(int screen) {
    plat_menu_init(screen, NM_MENU_RECORDS + (u32)screen * NM_MENU_STRIDE);
    RDS16(NM_SCREEN) = (s16)screen;
}
int dbg_goto;
// Selecting a menu item the game's way: its action word goes through the menus' own action
// interpreter (func_8010F0A8), which sets up whatever the choice means (game mode, title, ...)
// and returns the next screen; then that screen is built and shown.
static int menu_select(int screen, int item_index, int action) {
    u32 record = NM_MENU_RECORDS + (u32)screen * NM_MENU_STRIDE;
    u32 item = RD32(record + 0x14) + (u32)item_index * 0x88u;
    int next = plat_menu_action(action, record, item);
    RDS16(0x8015B7C8u) = 1;                 // the menus' "screen changed" flag (func_80110C20 sets it)
    if (next >= 0 && next < 0x40) menu_goto(next);
    return next;
}
int dbg_sel_screen, dbg_sel_item, dbg_sel_action, dbg_sel_result;

// ---- menu driver -----------------------------------------------------------------------------
static void drv_press(u16 b) { drv_button = b; drv_hold = 6; } // the menus read the pads every few frames

static void menu_driver(void) {
    s16 scr = RDS16(NM_SCREEN);
    s32 mode = RDS32(NM_MODE);
    if (scr == drv_screen && mode == drv_mode) drv_stable++; else drv_stable = 0;
    drv_screen = scr; drv_mode = mode;
    if (drv_hold > 0) { drv_hold--; RD16(NM_PADS) = drv_button; return; }
    if (drv_goal == DRV_NONE) return;

    if (drv_goal == DRV_ENTER) {             // coin + start in attract: on to the menus
        if (mode == -6) { if (frame_no & 4) RD16(NM_PADS) = BTN_START; return; } // end the demo
        if (!title_loaded() || mode != 0) return;
        if (scr != SCR_TITLE) { DRV_SET(DRV_NONE); return; }
        if (drv_stable >= 8 && (drv_stable % 10) == 8) drv_press(BTN_START);
        return;
    }
    if (drv_goal == DRV_ROUTE) {             // the main menu: on to Story Mode (1 player) or Versus (2)
        if (!in_menus()) return;
        if (scr != SCR_MULTIPLAY && scr != SCR_SINGLEPLAY && scr != SCR_COMMISSIONER && scr != SCR_TRANS_B &&
            scr != SCR_CHAMPIONSHIP) {
            DRV_SET(DRV_NONE); return;           // arrived: Exhibition's first page / the superstar select
        }
        if (drv_stable < 20 || (drv_stable % 12) != 8) return;
        if (route_mode == GM_VERSUS) {           // Multi Play page, Exhibition
            if (scr == SCR_SINGLEPLAY || scr == SCR_COMMISSIONER) drv_press(BTN_DL);
            else if (scr == SCR_CHAMPIONSHIP) drv_press(BTN_B);
            else if (scr == SCR_MULTIPLAY) drv_press(*menu_cursor(SCR_MULTIPLAY) != 0 ? BTN_DU : BTN_A);
        } else {                                 // Single Play page, Championship
            if (scr == SCR_MULTIPLAY) drv_press(BTN_DR);
            else if (scr == SCR_COMMISSIONER) drv_press(BTN_DL);
            else if (scr == SCR_SINGLEPLAY) drv_press(*menu_cursor(SCR_SINGLEPLAY) != 0 ? BTN_DU : BTN_A);
            // the title: World Heavyweight here; a woman is switched to the Women's title at the select
            else if (scr == SCR_CHAMPIONSHIP) drv_press(*menu_cursor(SCR_CHAMPIONSHIP) != 0 ? BTN_DU : BTN_A);
        }
        return;
    }
    if (drv_goal == DRV_TO_HUB) {            // back out of the menus to the main menu
        if (!in_menus() || scr == SCR_MATCH) return;
        if (scr == SCR_MULTIPLAY || scr == SCR_SINGLEPLAY || scr == SCR_COMMISSIONER || scr == SCR_CHAMPIONSHIP) { DRV_SET(DRV_NONE); return; }
        if (drv_stable < 20 || (drv_stable % 20) != 8) return;
        drv_press(scr == SCR_RESULTS ? BTN_A : BTN_B); // results: Quit; anywhere else: back
        if (++drv_step > 60) DRV_SET(DRV_NONE);
        return;
    }
    if (drv_goal == DRV_TO_TITLE) {
        if (mode == -2) {
            if (match_running() && RDS32(NM_SLOT_ELIM_TIME) < 0 && RDS32(NM_SLOT_ELIM_TIME + 4) < 0) {
                // still a match on: pause > QUIT MATCH > YES
                if (drv_stable < 10) return;
                if (++drv_wait < 14) return;
                drv_wait = 0;
                static const u16 seq[] = { BTN_START, BTN_DD, BTN_DD, BTN_A, BTN_DU, BTN_A };
                if (drv_step < 6) drv_press(seq[drv_step++]);
                else if (++drv_step > 6 + 25) drv_step = 0;
                return;
            }
            if ((frame_no & 31) == 0) drv_press(BTN_A); // replays and the finish: move along
            return;
        }
        if (mode == -6) { if (frame_no & 4) RD16(NM_PADS) = BTN_START; return; }
        if (!title_loaded() || mode != 0) {
            if (mode == 0 && drv_stable > 60 && (drv_stable % 30) == 0) drv_press(BTN_A); // story scenes
            return;
        }
        if (scr == SCR_TITLE) { DRV_SET(DRV_NONE); return; }
        if (drv_stable < 16) return;
        if (scr != SCR_TRANS_A && scr != SCR_TRANS_B) RDS16(NM_SCREEN) = SCR_TITLE;
        return;
    }
}

// ---- controllers --------------------------------------------------------------------------------
static void poll_keys(void) {
    for (unsigned k = 0; k < sizeof(watched_keys); k++) {
        int sc = watched_keys[k];
        int down = plat_key_down(sc);
        int idx = sc >> 3, bit = 1 << (sc & 7);
        int was = (key_prev[idx] & bit) != 0;
        if (down) key_prev[idx] |= (u8)bit; else key_prev[idx] &= (u8)~bit;
        if (down && !was) key_latch[idx] |= (u8)bit;
    }
}

static int key_edge(int sc) {
    int idx = sc >> 3, bit = 1 << (sc & 7);
    int hit = (key_latch[idx] & bit) != 0;
    key_latch[idx] &= (u8)~bit;
    return hit;
}

static u16 take_pressed(int i) { u16 v = pad_latch[i]; pad_latch[i] = 0; return v; }

static int covering(void);

// After the game read the controllers (NM_PADS): remember them, decide what the game sees, and
// let the menu driver press its buttons.
void arcade_pad_hook(void) {
    if (!settings_loaded) load_settings();
    poll_keys();
    u16 raw[4], edge[4];
    for (int i = 0; i < 4; i++) {
        u32 p = NM_PADS + 6u * (u32)i;
        raw[i] = RD16(p);
        edge[i] = (u16)(raw[i] & ~pad_prev[i]);
        pad_latch[i] |= edge[i];
        pad_prev[i] = raw[i];
    }
    // what the game gets: the paying players' controllers, while nothing of ours is in the way.
    // The player who started is the game's controller 1, the other one its controller 2.
    int pass = phase == PLAY && !op_open && cont_player < 0 && drv_goal == DRV_NONE && !covering();
    u16 ob[4] = { 0, 0, 0, 0 }, oe[4] = { 0, 0, 0, 0 };
    u8 ox[4] = { 0, 0, 0, 0 }, oy[4] = { 0, 0, 0, 0 };
    if (pass) {
        for (int port = 0; port < 2; port++) {
            int pl = port_player[port];
            if (!active[pl]) continue;
            u32 src = NM_PADS + 6u * (u32)pl;
            ob[port] = raw[pl]; oe[port] = edge[pl]; ox[port] = RD8(src + 2); oy[port] = RD8(src + 3);
        }
    }
    if (pass && in_menus()) {
        s16 scr = RDS16(NM_SCREEN);
        for (int port = 0; port < 2; port++) {
            if (scr == SCR_PLAYERS && !(active[0] && active[1]) && *menu_cursor(SCR_PLAYERS) == 0 &&
                (oe[port] & (BTN_A | BTN_START))) {
                ob[port] &= (u16)~(BTN_A | BTN_START);   // 1P vs 2P needs player 2 in
                need_p2 = 150;
            }
        }
    }
    if (phase == PLAY && game_mode == GM_STORY && RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) != SCR_MATCH) {
        s16 scr = RDS16(NM_SCREEN);
        // story pages the arcade moves through: the chapter card, the text pages, the rules (fixed in
        // the story), the VS card. The superstar select is the player's.
        int story_page = scr != SCR_STORY_SELECT && scr != SCR_STORY_SELECT2 && scr != SCR_MULTIPLAY && scr != SCR_SINGLEPLAY &&
                         scr != SCR_COMMISSIONER && scr != SCR_CHAMPIONSHIP && scr != SCR_TRANS_A && scr != SCR_TRANS_B &&
                         scr != SCR_TITLE;
        if (!title_loaded()) story_page = 1;
        // superstar select: the championship follows the wrestler (women: the Women's title)
        // (set as they confirm their pick)
        if ((scr == SCR_STORY_SELECT || scr == SCR_STORY_SELECT2) && title_loaded() && ((oe[0] | oe[1]) & (BTN_A | BTN_START))) {
            u32 id = RD16(NM_SELECT_HOVER);
            if (id != 0 && id < 0x1000) RD8(NM_STORY_BELT) = (u8)((id & 0xF00) == 0x700 ? BELT_WOMENS : BELT_HEAVYWEIGHT);
        }
        // the chapter card (Continue / Save / Option): shown a moment, then on with the story
        // the chapter card (Continue / Save / Option) and the story's text pages: A moves on, nothing
        // else gets through, and after a few seconds it moves on by itself
        if ((scr == SCR_STORY_SELECT || scr == SCR_STORY_SELECT2) && title_loaded())
            for (int i = 0; i < 4; i++) ob[i] &= (u16)~BTN_B;          // no backing out to the title list
        if (story_page && cont_player < 0) {
            for (int i = 0; i < 4; i++) { ob[i] &= BTN_A; ox[i] = 0; oy[i] = 0; }
            int wait = scr == SCR_RULES ? 20 : 200;  // the rules page is hidden: straight on
            if (drv_goal == DRV_NONE && drv_hold == 0 && drv_screen == scr && drv_stable > wait && (drv_stable % 40) == (wait % 40))
                drv_press(BTN_A);
        }
    }
    for (int i = 0; i < 4; i++) {
        u32 p = NM_PADS + 6u * (u32)i;
        RD16(p) = ob[i]; RD8(p + 2) = ox[i]; RD8(p + 3) = oy[i];
    }
    // entrances: skip the full-screen Titantron video (the wrestler walks out to their music)
    if (RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == SCR_MATCH) {
        s32 st = RDS32(NM_ENTRANCE_STEP);
        if (st == 4 || st == 10) RDS32(NM_ENTRANCE_STEP) = st + 1;
    }
    menu_driver();
}

// ---- title / attract -------------------------------------------------------------------------
static void patch_demo_table(u16 w0, u16 w1) {
    for (int r = NM_DEMO_SINGLES_FIRST; r < NM_DEMO_SINGLES_END; r++) {
        u32 a = NM_DEMO_TABLE + 10u * (u32)r;
        RD16(a) = w0; RD16(a + 2) = w1; RD16(a + 4) = 0; RD16(a + 6) = 0;
    }
}

static int demo_a, demo_b;
static void drive_title(void) {
    if (!on_title() || phase != ATTRACT) return;
    s32 idle = RDS32(NM_TITLE_IDLE);
    if (idle >= 0 && idle < 20) {
        demo_a = (int)(rng() % ROSTER_N);
        int r = (int)(rng() % (ROSTER_N - 1));
        demo_b = r >= demo_a ? r + 1 : r;
    }
    patch_demo_table(roster_id[demo_a], roster_id[demo_b]);
    u8 counter = RD8(NM_ATTRACT_COUNTER);
    if (!(counter & 1)) RD8(NM_ATTRACT_COUNTER) = (u8)(NM_DEMO_SET_SINGLES * 2);
    if (!attract_demos) { if (idle > 600) RDS32(NM_TITLE_IDLE) = 600; }
    else if (idle >= 0 && idle < NM_TITLE_IDLE_LIMIT - 300) RDS32(NM_TITLE_IDLE) = NM_TITLE_IDLE_LIMIT - 300;
}

void arcade_demo_setup_done(void) {
    if (phase == ATTRACT) RD8(NM_VENUE) = (u8)(rng() % 10u);
}

// ---- losing and continuing -----------------------------------------------------------------------
// A player has lost when every match slot their controller drives has been eliminated. Who drives
// which slot is taken when the match starts (everyone in).
static s8 slot_owner[4];
static void watch_match(void) {
    if (!match_running() || RD16(NM_MATCH_FRAMES) < 10) { match_armed = 0; return; }
    if (!match_armed) {
        int all_in = 1;
        for (int s = 0; s < 4; s++) if (RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)s) >= 0) all_in = 0;
        if (!all_in) return;
        match_armed = 1; match_lost[0] = match_lost[1] = 0;
        for (int s = 0; s < 4; s++) {
            s32 pad = RDS32(NM_SLOT_PAD + 4u * (u32)s);
            slot_owner[s] = (s8)((pad == 1 || pad == 2) && RD8(NM_SLOT_HUMAN + (u32)s) != 0 ? port_player[pad - 1] : -1);
        }
        return;
    }
    for (int pl = 0; pl < 2; pl++) {
        if (!active[pl] || match_lost[pl]) continue;
        int owns = 0, out = 1;
        for (int s = 0; s < 4; s++) {
            if (slot_owner[s] != pl) continue;
            owns = 1;
            if (RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)s) < 0) out = 0;
        }
        if (owns && out) {
            match_lost[pl] = 1;
            if (!free_play) lost_pending[pl] = 1;
        }
    }
}

static void start_game_over(void) {
    game_mode = GM_NONE; hub_pending = 0;
    active[0] = active[1] = 0;
    cont_player = -1;
    lost_pending[0] = lost_pending[1] = 0;
    drv_goal = DRV_TO_TITLE; drv_step = 0; drv_wait = 0;
    set_phase(GAMEOVER);
}

static void draw_credits(int row) {
    char buf[24], num[4];
    if (free_play) { draw_text(30, row, COL_WHITE, "FREE PLAY"); return; }
    const char* lbl = credits == 1 ? "CREDIT " : "CREDITS ";
    int n = 0;
    for (const char* s = lbl; *s; s++) buf[n++] = *s;
    str_num(num, credits);
    for (const char* s = num; *s; s++) buf[n++] = *s;
    buf[n] = 0;
    draw_text(39 - n, row, COL_WHITE, buf);
}

static void cover_screen(void) { draw_rect(0, 0, 480, 240, 0x0A0A12FFu); }
static int covering(void) {
    if (phase == GAMEOVER) return 1;
    if (drv_goal == DRV_ROUTE) return 1;
    if (phase == PLAY && drv_goal == DRV_ENTER && RDS32(NM_MODE) != -6) return 1;   // the title, the Rumble Pak note
    if (phase == PLAY && game_mode == GM_NONE && in_menus()) return 1;               // on the way to the menus
    if (phase == PLAY && game_mode == GM_STORY && in_menus() && RDS16(NM_SCREEN) == SCR_RULES) return 1;
    return drv_goal == DRV_TO_HUB && in_menus() && RDS16(NM_SCREEN) != SCR_MATCH;
}

static void continue_screen(u16 pr[2], int start[2]) {
    if (cont_player < 0) {
        for (int pl = 0; pl < 2; pl++)
            if (lost_pending[pl]) { lost_pending[pl] = 0; cont_player = pl; cont_frames = 0; break; }
        if (cont_player < 0) return;
    }
    int pl = cont_player;
    int secs = continue_secs - cont_frames / 60;
    if (pr[pl] & (BTN_A | BTN_B)) cont_frames += 20;
    if (start[pl] && pay()) { cont_player = -1; return; }
    if (secs < 0) {
        active[pl] = 0;
        cont_player = -1;
        if (nobody_playing() && !lost_pending[0] && !lost_pending[1]) start_game_over();
        return;
    }
    char num[4]; str_num(num, secs);
    draw_box(10, 8, 20, 12, 0x000000D0u);
    draw_text_center(9, pl == 0 ? COL_RED : COL_CYAN, pl == 0 ? "PLAYER 1" : "PLAYER 2");
    draw_text_center(12, COL_YELLOW, "CONTINUE?");
    draw_text_center(15, COL_WHITE, num);
    if (can_start()) { if (blink(15)) draw_text_center(18, COL_WHITE, "PRESS START"); }
    else draw_text_center(18, COL_WHITE, "INSERT COIN");
    draw_credits(28);
}

// ---- operator menu (F2) -----------------------------------------------------------------------------
static void operator_menu(void) {
    u16 pr = take_pressed(0);
    int up = key_edge(SC_UP) || (pr & BTN_DU), down = key_edge(SC_DOWN) || (pr & BTN_DD);
    int left = key_edge(SC_LEFT) || (pr & BTN_DL), right = key_edge(SC_RIGHT) || (pr & BTN_DR);
    int ok = key_edge(SC_RETURN) || (pr & (BTN_A | BTN_START));
    if (up) op_cursor = (op_cursor + 4) % 5;
    if (down) op_cursor = (op_cursor + 1) % 5;
    int d = right ? 1 : (left ? -1 : 0);
    if (d) {
        if (op_cursor == 0) free_play ^= 1;
        if (op_cursor == 1) { continue_secs += d * 5; if (continue_secs < 5) continue_secs = 30; if (continue_secs > 30) continue_secs = 5; }
        if (op_cursor == 2) attract_demos ^= 1;
        if (op_cursor == 3) fast_getup ^= 1;
    }
    if (ok && op_cursor == 4) op_open = 0;

    char num[4];
    draw_box(4, 5, 32, 18, 0x000000E0u);
    draw_text_center(6, COL_YELLOW, "OPERATOR SETTINGS");
    const char* labels[5] = { "FREE PLAY", "CONTINUE TIMER", "ATTRACT DEMOS", "FAST GET UP", "EXIT" };
    for (int i = 0; i < 5; i++) {
        int row = 9 + i * 2;
        u32 c = (i == op_cursor) ? COL_YELLOW : COL_WHITE;
        if (i == op_cursor) draw_text(5, row, COL_YELLOW, ">");
        draw_text(7, row, c, labels[i]);
        if (i == 0) draw_text(25, row, c, free_play ? "ON" : "OFF");
        if (i == 1) { str_num(num, continue_secs); draw_text(25, row, c, num); draw_text(28, row, c, "SEC"); }
        if (i == 2) draw_text(25, row, c, attract_demos ? "ON" : "OFF");
        if (i == 3) draw_text(25, row, c, fast_getup ? "ON" : "OFF");
    }
    draw_text_center(20, COL_GREY, "UP/DOWN SELECT  LEFT/RIGHT CHANGE");
    draw_text_center(21, COL_GREY, "F2 CLOSE");
}

// ---- main per-frame logic --------------------------------------------------------------------------
int arcade_entrance_cam(void) { return phase == PLAY; }

void arcade_frame(u32 task) {
    u32 vi = RD32(NM_VI_COUNT);
    int dt = (int)(vi - last_vi);
    if (dt < 0 || dt > 10) dt = 1;
    last_vi = vi;
    frame_no += (u32)dt;
    phase_frames += dt;
    if (cont_player >= 0) cont_frames += dt;
    if (credit_flash > 0) credit_flash -= dt;
    if (!settings_loaded) load_settings();
    draw_begin();

    patch_menu_table();
    if (dbg_goto) { menu_goto(dbg_goto); dbg_goto = 0; }
    if (dbg_sel_action) { dbg_sel_result = menu_select(dbg_sel_screen, dbg_sel_item, dbg_sel_action); dbg_sel_action = 0; }
    if (in_menus() && RDS16(NM_SCREEN) != SCR_TITLE) draw_scan_textures(task, menu_texture);

    int coin = 0;
    if (key_edge(SC_5)) { if (credits < 9) credits++; coin = 1; }
    if (key_edge(SC_6)) { if (credits < 9) credits++; coin = 1; }
    if (coin) credit_flash = 180;
    int kstart1 = key_edge(SC_1), kstart2 = key_edge(SC_2);
    if (key_edge(SC_F2)) { op_open = !op_open; op_cursor = 0; }
    if (op_open) { operator_menu(); draw_flush(task); return; }

    u16 pr[2] = { take_pressed(0), take_pressed(1) };
    int start[2] = { kstart1 || (pr[0] & BTN_START), kstart2 || (pr[1] & BTN_START) };
    s32 mode = RDS32(NM_MODE);

    // boot: skip the intro; in attract the game's intro movie may play, 30 s at most
    if (intro_running()) {
        intro_frames += dt;
        if (!booted || phase != ATTRACT || intro_frames > 45 * 60) RDS32(NM_MODE) = 1;
    } else if (RD32(SIG_INTRO_ADDR) != SIG_INTRO) {
        intro_frames = 0;
    }
    if (on_title()) booted = 1;

    switch (phase) {
    case ATTRACT: {
        if (on_title()) draw_box(13, 22, 14, 3, 0x000000FFu); // over the game's PRESS START
        // self test (capture runs): a coin and Start a few seconds after the first title
        if (selftest && !selftest_done && on_title() && phase_frames > 180) {
            selftest_done = 1;
            credits++;
            start[0] = 1;
        }
        for (int pl = 0; pl < 2; pl++) {
            if (start[pl] && booted && pay()) {
                active[0] = active[1] = 0;
                active[pl] = 1;
                port_player[0] = pl; port_player[1] = pl ^ 1;
                game_mode = GM_NONE; hub_pending = 0;
                match_armed = 0;
                drv_goal = DRV_ENTER; drv_stable = 0;
                set_phase(PLAY);
                break;
            }
        }
        if (phase != ATTRACT) break;
        if (mode == -6) {
            char buf[48]; int n = 0;
            for (const char* s = roster_name[demo_a]; *s; s++) buf[n++] = *s;
            buf[n++] = ' '; buf[n++] = 'V'; buf[n++] = 'S'; buf[n++] = ' ';
            for (const char* s = roster_name[demo_b]; *s && n < 43; s++) buf[n++] = *s;
            buf[n] = 0;
            draw_text_center(2, COL_YELLOW, buf);
        }
        int row = on_title() ? 23 : 25;
        if (can_start()) { if (blink(20)) draw_text_center(row, COL_YELLOW, "PRESS START"); }
        else if (blink(30)) draw_text_center(row, COL_WHITE, "INSERT COIN");
        draw_credits(28);
        break;
    }
    case PLAY: {
        // a player joins: coin + Start on their controller (or key 1 / 2)
        for (int pl = 0; pl < 2; pl++)
            if (!active[pl] && start[pl] && cont_player != pl && pay()) {
                active[pl] = 1;
                if (game_mode == GM_STORY) { challenger_frames = 240; hub_pending = 1; }
            }
        watch_match();
        if (fast_getup && match_running()) arcade_getup_step();
        {
            int want = (active[0] && active[1]) ? GM_VERSUS : GM_STORY;
            if (game_mode != GM_NONE && game_mode != want && cont_player < 0) hub_pending = 1; // someone left / joined
            s16 scr = RDS16(NM_SCREEN);
            if (drv_goal == DRV_NONE && in_menus() && drv_screen == scr) {
                if (scr == SCR_MULTIPLAY || scr == SCR_SINGLEPLAY || scr == SCR_COMMISSIONER || scr == SCR_CHAMPIONSHIP) {
                    route_mode = game_mode = want; hub_pending = 0;
                    DRV_SET(DRV_ROUTE); drv_stable = 0;
                } else if (hub_pending && scr != SCR_MATCH && scr != SCR_TITLE && cont_player < 0) {
                    hub_pending = 0; drv_step = 0;
                    DRV_SET(DRV_TO_HUB); drv_stable = 0;
                }
            }
        }
        if (covering()) {
            cover_screen();
            if (drv_goal == DRV_ROUTE || drv_goal == DRV_ENTER || game_mode == GM_NONE) {
                int vs = drv_goal == DRV_ROUTE ? route_mode == GM_VERSUS : (active[0] && active[1]);
                draw_text_center(13, COL_YELLOW, vs ? "VERSUS" : "STORY MODE");
            }
        }
        if (challenger_frames > 0) {
            challenger_frames -= dt;
            draw_box(8, 11, 24, 5, 0x000000D0u);
            if (blink(10)) draw_text_center(12, COL_YELLOW, "HERE COMES A");
            draw_text_center(14, COL_RED, "NEW CHALLENGER!");
        }
        continue_screen(pr, start);
        if (phase != PLAY) break;
        // the other player's invitation to join
        if (cont_player < 0) {
            for (int pl = 0; pl < 2; pl++) {
                if (active[pl] || !in_menus()) continue;
                if (blink(30)) {
                    int row = pl == 0 ? 28 : 1;
                    if (can_start()) draw_text(pl == 0 ? 1 : 25, row, pl == 0 ? COL_RED : COL_CYAN, pl == 0 ? "1P PRESS START" : "2P PRESS START");
                    else draw_text(pl == 0 ? 1 : 25, row, COL_WHITE, pl == 0 ? "1P INSERT COIN" : "2P INSERT COIN");
                }
            }
            if (credit_flash > 0) draw_credits(3);
            if (need_p2 > 0) {
                int o = port_player[1];
                need_p2 -= dt;
                if (blink(10)) draw_text_center(26, o ? COL_CYAN : COL_RED, can_start() ? (o ? "2P PRESS START" : "1P PRESS START") : (o ? "2P INSERT COIN" : "1P INSERT COIN"));
            }
        }
        break;
    }
    case GAMEOVER: {
        cover_screen();
        draw_text_center(13, COL_RED, "GAME OVER");
        draw_credits(28);
        if (drv_goal == DRV_NONE && on_title() && phase_frames > 120) {
            RDS32(NM_TITLE_IDLE) = NM_TITLE_IDLE_LIMIT - 120; // a demo soon: it brings the title back whole
            set_phase(ATTRACT);
        }
        if (phase_frames > 60 * 60) { DRV_SET(DRV_NONE); set_phase(ATTRACT); } // safety
        break;
    }
    }

    drive_title();
    draw_flush(task);
}
