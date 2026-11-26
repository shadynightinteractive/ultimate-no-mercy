// WWF No Mercy Arcade.
//
// Flow:  ATTRACT (title + CPU demo matches, INSERT COIN)
//          -> coin + start -> SELECT (4 x 6 portrait grid, 15 s)
//          -> VS (our VS screen; underneath, the game's own exhibition menus are driven to a
//             singles match and our picks are put in at its VS screen)
//          -> ENTRANCE (the game's entrances, minus the full-screen titantron video)
//          -> MATCH -> RESULT -> next opponent (ladder) / CONTINUE / CHAMPION / GAME OVER.
// Player 2 can press Start any time: HERE COMES A NEW CHALLENGER (the match is quit and a
// 1P vs 2P match starts). The game never sees the players' controllers outside a match.
#include "arcade.h"
#include "roster.h"

#define SIG_INTRO_ADDR 0x80106AB0u
#define SIG_INTRO      0x3C028016u
#define SIG_TITLE_ADDR 0x80111158u
#define SIG_TITLE      0x27BDFFB8u

// menu screens (0x800457A4)
#define SCR_TITLE 0
#define SCR_MULTIPLAY 2
#define SCR_ARENA 5
#define SCR_PLAYERS 6
#define SCR_MATCHTYPE 7
#define SCR_RULES 21
#define SCR_VS 25
#define SCR_SELECT1 26
#define SCR_SELECT2 27
#define SCR_RESULTS 29
#define SCR_MATCH 42
#define PLAYERS_1P_VS_2P 0
#define PLAYERS_1P_VS_CPU 1

enum { ATTRACT, SELECT, VS, ENTRANCE, MATCH, RESULT, CONTINUE, GAMEOVER, CHALLENGER };
enum { DRV_NONE, DRV_TO_PICK, DRV_START_MATCH, DRV_TO_TITLE, DRV_QUIT_MATCH };

// the exhibition arena list (its cursor order); 0 = random
#define N_ARENAS 8
static const char* const ARENA_NAMES[] = { "RANDOM", "RAW IS WAR", "NO MERCY", "SMACKDOWN", "KING OF THE RING",
    "SUMMERSLAM", "SURVIVOR SERIES", "ROYAL RUMBLE", "WRESTLEMANIA" };

static int phase = ATTRACT;
static int phase_frames;
static u32 frame_no;
static int credits;
static int booted;
static int intro_frames;
static u32 last_vi;

// players: 0 = P1 (controller 1), 1 = P2 (controller 2)
static int active[2];
static int pick[2];          // roster index
static int confirmed[2];
static int opponent;         // CPU's roster index (solo match)
static int ladder_wins;      // matches won in a row by the solo player
static int solo;             // which player plays a solo match against the CPU
static int versus;           // this match is 1P vs 2P
static int lost_player;      // RESULT: -1 = the CPU lost
static int won_player;
static int cont_player;
static int lost_armed;
static int vs_applied;
static int match_venue;

// operator settings (F2)
static int op_open, op_cursor;
static int free_play, continue_secs, arena_sel, attract_demos, ladder_len;
static int settings_loaded;

// input
static u16 pad_prev[4], pad_latch[4];
static s8 stick_x[4], stick_y[4];
static int stick_dir_prev[4][2];
static u8 key_prev[256 / 8], key_latch[256 / 8];
static const u8 watched_keys[] = { SC_1, SC_2, SC_5, SC_6, SC_F2, SC_UP, SC_DOWN, SC_LEFT, SC_RIGHT, SC_RETURN };

// menu driver (runs in the controller hook)
static int drv_goal;
static s16 drv_screen;
static s32 drv_mode;
static int drv_stable;
static int drv_hold;        // polls left to hold drv_button
static u16 drv_button;
static int drv_step;        // DRV_QUIT_MATCH sequence position
static int drv_players;     // players screen cursor to choose
static int drv_arena;       // arena cursor to choose
static int drv_picks_ready; // select screens may be confirmed
static int drv_at_vs;       // reached the game's VS screen

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
    arena_sel = (int)plat_config("arena", 0);
    if (arena_sel < 0 || arena_sel > N_ARENAS) arena_sel = 0;
    attract_demos = (int)plat_config("attract_demos", 1) != 0;
    ladder_len = (int)plat_config("ladder", 5);
    if (ladder_len < 1 || ladder_len > 10) ladder_len = 5;
    settings_loaded = 1;
}

// ---- game state helpers --------------------------------------------------------------------
static int title_loaded(void) { return RD32(SIG_TITLE_ADDR) == SIG_TITLE; }
static int on_title(void) { return title_loaded() && RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == SCR_TITLE; }
static int intro_running(void) {
    return RD32(SIG_INTRO_ADDR) == SIG_INTRO && RDS32(NM_MODE) == 0 && RDS32(NM_INTRO_SCENE) == 0 &&
           RDS32(NM_INTRO_SKIPPABLE) != 0;
}
__attribute__((unused)) static int in_menus_unused(void) { return title_loaded() && RDS32(NM_MODE) == 0; }
static u8* menu_cursor(int screen) { return (u8*)(NM_MENU_RECORDS + (u32)screen * NM_MENU_STRIDE + 5); }

static int pick_venue(void) { return arena_sel > 0 ? arena_sel - 1 : (int)(rng() % N_ARENAS); }

static int random_opponent(int not_this) {
    int r = (int)(rng() % (ROSTER_N - 1));
    return r >= not_this ? r + 1 : r;
}

// Put a roster wrestler in a match slot, the way the game's own slot setup does.
static void set_slot_wrestler(int slot, int roster_index) {
    u32 id = roster_id[roster_index];
    RDS32(NM_SLOT_WRESTLER + 4u * (u32)slot) = (s32)(id & 0xFFF);
    RD16(NM_SLOT_ID16 + 2u * (u32)slot) = (u16)(id & 0xFFF);
    RDS32(NM_SLOT_COSTUME + 4u * (u32)slot) = (s32)((id >> 12) & 0xF);
    RD8(NM_SLOT_COSTUME_B + (u32)slot) = (u8)((id >> 12) & 0xF);
}

// who is in match slot 0 / 1 (player index, or -1 for the CPU)
static int slot_player(int slot) {
    if (versus) return slot; // P1 = controller 1 = slot 0, P2 = slot 1
    return slot == 0 ? solo : -1;
}

// ---- menu driver -----------------------------------------------------------------------------
static int drv_both; // also press on controller 2 (player 2's superstar select in 1P vs 2P)
static void drv_press(u16 b) { drv_button = b; drv_hold = 3; drv_both = 0; }

static void drv_seek(int screen, int want) {
    int cur = *menu_cursor(screen);
    drv_press(cur < want ? BTN_DD : cur > want ? BTN_DU : BTN_A);
}

static void menu_driver(void) {
    s16 scr = RDS16(NM_SCREEN);
    s32 mode = RDS32(NM_MODE);
    if (scr == drv_screen && mode == drv_mode) drv_stable++; else drv_stable = 0;
    drv_screen = scr; drv_mode = mode;
    if (drv_hold > 0) { drv_hold--; RD16(NM_PADS) = drv_button; if (drv_both) RD16(NM_PADS + 6) = drv_button; return; }
    if (drv_goal == DRV_NONE) return;

    if (drv_goal == DRV_QUIT_MATCH) {
        if (mode != -2) { drv_goal = DRV_NONE; return; }
        static int quit_wait;
        if (drv_stable < 10) return;
        if (++quit_wait < 14) return;
        quit_wait = 0;
        // pause > QUIT MATCH > "QUIT MATCH? YES / NO" (cursor starts on NO)
        static const u16 seq[] = { BTN_START, BTN_DD, BTN_DD, BTN_A, BTN_DU, BTN_A };
        if (drv_step < 6) drv_press(seq[drv_step++]);
        else if (++drv_step > 6 + 25) drv_step = 0; // still in the match ~10 s later: try again
        return;
    }
    // an attract demo (not our match) is running: any button ends it
    if (mode == -6) { if (frame_no & 4) RD16(NM_PADS) = BTN_START; return; }
    if (!title_loaded() || mode != 0) return;
    if (drv_stable < 16 || (drv_stable % 12) != 4) return;

    if (drv_goal == DRV_TO_TITLE) {
        if (scr == SCR_TITLE) { drv_goal = DRV_NONE; return; }
        if (scr == SCR_RESULTS) { drv_press(BTN_A); return; }   // Quit
        if (scr != 1 && scr != 41) RDS16(NM_SCREEN) = SCR_TITLE;  // menus: straight back to the title
        return;
    }
    if (drv_goal == DRV_START_MATCH) {
        if (scr == SCR_VS) drv_press(BTN_A);
        return; // done when the entrances start (phase logic)
    }
    // DRV_TO_PICK: Exhibition > Single > players > arena > rules > select > select > VS
    switch (scr) {
    case SCR_TITLE: drv_press(BTN_START); break;
    case SCR_MULTIPLAY: drv_press(BTN_A); break;      // Exhibition
    case SCR_MATCHTYPE: drv_press(BTN_A); break;      // Single Match
    case SCR_PLAYERS: if (drv_picks_ready) drv_seek(SCR_PLAYERS, drv_players); break; // wait for the picks
    case SCR_ARENA: drv_seek(SCR_ARENA, drv_arena); break;
    case SCR_RULES: drv_press(BTN_START); break;
    case SCR_SELECT1: case SCR_SELECT2: if (drv_picks_ready) { drv_press(BTN_A); drv_both = 1; } break;
    case SCR_RESULTS: drv_press(BTN_A); break;        // Quit, back to the Multi Play menu
    case SCR_VS: drv_at_vs = 1; break;
    default: break;
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

// After the game read the controllers (NM_PADS): remember them, decide what the game sees, and
// let the menu driver press its buttons.
void arcade_pad_hook(void) {
    poll_keys();
    u16 raw[4];
    s8 rx[4], ry[4];
    for (int i = 0; i < 4; i++) {
        u32 p = NM_PADS + 6u * (u32)i;
        raw[i] = RD16(p); rx[i] = RDS8(p + 2); ry[i] = RDS8(p + 3);
        pad_latch[i] |= (u16)(raw[i] & ~pad_prev[i]);
        pad_prev[i] = raw[i];
        stick_x[i] = rx[i]; stick_y[i] = ry[i];
        RD16(p) = 0; RD8(p + 2) = 0; RD8(p + 3) = 0;
    }
    int playing = (phase == ENTRANCE || phase == MATCH || phase == RESULT) && !op_open && drv_goal != DRV_QUIT_MATCH;
    if (playing) {
        // game controller 1 = slot 0's player; controller 2 = player 2 in a versus match
        int src0 = versus ? 0 : solo;
        RD16(NM_PADS) = (u16)(raw[src0] & ~BTN_START); // no pausing in the arcade
        RD8(NM_PADS + 2) = (u8)rx[src0]; RD8(NM_PADS + 3) = (u8)ry[src0];
        if (versus) {
            RD16(NM_PADS + 6) = (u16)(raw[1] & ~BTN_START);
            RD8(NM_PADS + 8) = (u8)rx[1]; RD8(NM_PADS + 9) = (u8)ry[1];
        }
    }
    // entrances: skip the full-screen titantron video (the wrestler walks out to their music)
    if (phase == ENTRANCE) {
        s32 st = RDS32(NM_ENTRANCE_STEP);
        if (st == 4 || st == 10) RDS32(NM_ENTRANCE_STEP) = st + 1;
    }
    // after the decision the game shows the finish and a replay until a button: move it along
    if (phase == RESULT && phase_frames > 6 * 60 && RDS32(NM_MODE) == -2 && (frame_no & 16))
        RD16(NM_PADS) = BTN_A;
    menu_driver();
}

static u16 take_pressed(int i) { u16 v = pad_latch[i]; pad_latch[i] = 0; return v; }

// -1 / +1 on a new press (d-pad or stick) along an axis (0 = x, 1 = y; +1 = right / down)
static int axis_edge(int i, u16 pressed, int axis) {
    if (axis == 0) { if (pressed & BTN_DL) return -1; if (pressed & BTN_DR) return 1; }
    else { if (pressed & BTN_DU) return -1; if (pressed & BTN_DD) return 1; }
    int v = axis == 0 ? stick_x[i] : -stick_y[i];
    int dir = v < -40 ? -1 : v > 40 ? 1 : 0;
    int edge = (dir != 0 && dir != stick_dir_prev[i][axis]) ? dir : 0;
    stick_dir_prev[i][axis] = dir;
    return edge;
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
    if (!on_title()) return;
    s32 idle = RDS32(NM_TITLE_IDLE);
    if (phase != ATTRACT) { if (idle > 600) RDS32(NM_TITLE_IDLE) = 600; return; }
    if (idle >= 0 && idle < 20) { demo_a = (int)(rng() % ROSTER_N); demo_b = random_opponent(demo_a); }
    patch_demo_table(roster_id[demo_a], roster_id[demo_b]);
    u8 counter = RD8(NM_ATTRACT_COUNTER);
    if (!(counter & 1)) RD8(NM_ATTRACT_COUNTER) = (u8)(NM_DEMO_SET_SINGLES * 2);
    if (!attract_demos) { if (idle > 600) RDS32(NM_TITLE_IDLE) = 600; }
    else if (idle >= 0 && idle < NM_TITLE_IDLE_LIMIT - 300) RDS32(NM_TITLE_IDLE) = NM_TITLE_IDLE_LIMIT - 300;
}

void arcade_demo_setup_done(void) {
    if (phase == ATTRACT) RD8(NM_VENUE) = (u8)(rng() % 10u);
}

// ---- helpers for the screens ---------------------------------------------------------------------
static int blink(int period) { return (frame_no / (u32)period) & 1; }
static int pay(void) { if (free_play) return 1; if (credits <= 0) return 0; credits--; return 1; }
static int can_start(void) { return free_play || credits > 0; }

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

// Select grid: 6 columns x 4 rows of 56 x 34 portraits
#define GRID_X 58
#define GRID_Y 26
#define CELL_W 62
#define CELL_H 38
static void cell_pos(int i, int* x, int* y) { *x = GRID_X + (i % 6) * CELL_W; *y = GRID_Y + (i / 6) * CELL_H; }

static void start_select(int both_keep) {
    for (int pl = 0; pl < 2; pl++) confirmed[pl] = 0;
    drv_picks_ready = 0; drv_at_vs = 0; vs_applied = 0;
    drv_goal = DRV_TO_PICK;
    set_phase(SELECT);
    (void)both_keep;
}

static int keep_opponent;
static void begin_vs(void) {
    versus = active[0] && active[1];
    if (!versus) {
        solo = active[0] ? 0 : 1;
        // a continue is a rematch against the same opponent (unless you picked them)
        if (!keep_opponent || opponent == pick[solo]) opponent = random_opponent(pick[solo]);
    }
    keep_opponent = 0;
    drv_players = versus ? PLAYERS_1P_VS_2P : PLAYERS_1P_VS_CPU;
    match_venue = pick_venue();
    drv_arena = match_venue;
    drv_picks_ready = 1;
    drv_at_vs = 0; vs_applied = 0;
    if (drv_goal != DRV_TO_PICK) drv_goal = DRV_TO_PICK;
    set_phase(VS);
}

// ---- operator menu (F2) -----------------------------------------------------------------------------
static void operator_menu(void) {
    u16 pr = take_pressed(0);
    int up = key_edge(SC_UP) || (pr & BTN_DU), down = key_edge(SC_DOWN) || (pr & BTN_DD);
    int left = key_edge(SC_LEFT) || (pr & BTN_DL), right = key_edge(SC_RIGHT) || (pr & BTN_DR);
    int ok = key_edge(SC_RETURN) || (pr & (BTN_A | BTN_START));
    if (up) op_cursor = (op_cursor + 5) % 6;
    if (down) op_cursor = (op_cursor + 1) % 6;
    int d = right ? 1 : (left ? -1 : 0);
    if (d) {
        if (op_cursor == 0) free_play ^= 1;
        if (op_cursor == 1) { continue_secs += d * 5; if (continue_secs < 5) continue_secs = 30; if (continue_secs > 30) continue_secs = 5; }
        if (op_cursor == 2) arena_sel = (arena_sel + d + N_ARENAS + 1) % (N_ARENAS + 1);
        if (op_cursor == 3) attract_demos ^= 1;
        if (op_cursor == 4) { ladder_len += d; if (ladder_len < 1) ladder_len = 10; if (ladder_len > 10) ladder_len = 1; }
    }
    if (ok && op_cursor == 5) op_open = 0;

    char num[4];
    draw_box(4, 4, 32, 21, 0x000000E0u);
    draw_text_center(5, COL_YELLOW, "OPERATOR SETTINGS");
    const char* labels[6] = { "FREE PLAY", "CONTINUE TIMER", "ARENA", "ATTRACT DEMOS", "MATCHES TO WIN", "EXIT" };
    for (int i = 0; i < 6; i++) {
        int row = 8 + i * 2;
        u32 c = (i == op_cursor) ? COL_YELLOW : COL_WHITE;
        if (i == op_cursor) draw_text(5, row, COL_YELLOW, ">");
        draw_text(7, row, c, labels[i]);
        if (i == 0) draw_text(24, row, c, free_play ? "ON" : "OFF");
        if (i == 1) { str_num(num, continue_secs); draw_text(24, row, c, num); draw_text(27, row, c, "SEC"); }
        if (i == 2) draw_text(19, row, c, ARENA_NAMES[arena_sel]);
        if (i == 3) draw_text(24, row, c, attract_demos ? "ON" : "OFF");
        if (i == 4) { str_num(num, ladder_len); draw_text(24, row, c, num); }
    }
    draw_text_center(21, COL_GREY, "UP/DOWN SELECT  LEFT/RIGHT CHANGE");
    draw_text_center(23, COL_GREY, "F2 CLOSE");
}

// ---- screens ---------------------------------------------------------------------------------------
static void screen_select(u16 pr[2], int start[2]) {
    int timer = 15 - phase_frames / 60;
    for (int pl = 0; pl < 2; pl++) {
        if (!active[pl] && start[pl] && pay()) { active[pl] = 1; confirmed[pl] = 0; pick[pl] = pl ? 2 : 0; }
        if (!active[pl] || confirmed[pl]) continue;
        int dx = axis_edge(pl, pr[pl], 0), dy = axis_edge(pl, pr[pl], 1);
        int c = pick[pl] % 6, r = pick[pl] / 6;
        if (dx) c = (c + dx + 6) % 6;
        if (dy) r = (r + dy + 4) % 4;
        pick[pl] = r * 6 + c;
        if ((pr[pl] & (BTN_A | BTN_START)) || timer <= 0) confirmed[pl] = 1;
    }
    cover_screen();
    draw_text_center(0, COL_YELLOW, "SELECT YOUR SUPERSTAR");
    char num[4]; str_num(num, timer < 0 ? 0 : timer);
    draw_text(36, 0, COL_WHITE, num);
    for (int i = 0; i < ROSTER_N; i++) {
        int x, y; cell_pos(i, &x, &y);
        int hl = (active[0] && pick[0] == i) || (active[1] && pick[1] == i);
        draw_portrait(i, x, y, 1, hl ? 0xFFFFFFFFu : 0xA0A0A0FFu);
    }
    for (int pl = 0; pl < 2; pl++) {
        if (!active[pl]) continue;
        int x, y; cell_pos(pick[pl], &x, &y);
        u32 col = pl == 0 ? 0xFF3020FFu : 0x30A0FFFFu;
        if (confirmed[pl] || blink(6)) draw_frame(x, y, THUMB_W, THUMB_H, pl == 0 ? 3 : 2, col);
    }
    if (active[0]) draw_text(2, 23, COL_RED, roster_name[pick[0]]);
    if (active[1]) { const char* n = roster_name[pick[1]]; draw_text(38 - str_len(n), 23, COL_CYAN, n); }
    if (active[0]) draw_text(2, 25, COL_RED, confirmed[0] ? "1P OK!" : "1P");
    if (active[1]) draw_text(36, 25, COL_CYAN, confirmed[1] ? "2P OK!" : "2P");
    if (!active[1] && can_start() && blink(20)) draw_text_center(25, COL_CYAN, "2P PRESS START");
    if (!active[0] && can_start() && blink(20)) draw_text_center(25, COL_RED, "1P PRESS START");
    draw_credits(28);
    int all = 1;
    for (int pl = 0; pl < 2; pl++) if (active[pl] && !confirmed[pl]) all = 0;
    if (all && phase_frames > 30) begin_vs();
}

static void draw_vs(void) {
    cover_screen();
    int left = versus ? pick[0] : pick[solo], right = versus ? pick[1] : opponent;
    if (versus) draw_text_center(3, COL_YELLOW, "PLAYER 1  VS  PLAYER 2");
    else {
        char buf[24] = "MATCH ", num[4]; str_num(num, ladder_wins + 1);
        int n = 6; for (const char* s = num; *s; s++) buf[n++] = *s;
        buf[n++] = ' '; buf[n++] = 'O'; buf[n++] = 'F'; buf[n++] = ' ';
        str_num(num, ladder_len); for (const char* s = num; *s; s++) buf[n++] = *s;
        buf[n] = 0;
        draw_text_center(3, COL_YELLOW, buf);
    }
    draw_portrait(left, 40, 64, 2, 0xFFFFFFFFu);
    draw_portrait(right, 480 - 40 - THUMB_W * 2, 64, 2, 0xFFFFFFFFu);
    draw_frame(40, 64, THUMB_W * 2, THUMB_H * 2, 3, versus || solo == 0 ? 0xFF3020FFu : 0x30A0FFFFu);
    draw_frame(480 - 40 - THUMB_W * 2, 64, THUMB_W * 2, THUMB_H * 2, 3, versus ? 0x30A0FFFFu : 0xC0C0C0FFu);
    if (blink(10) || phase_frames > 90) draw_text_center(12, COL_RED, "VS");
    const char* ln = roster_name[left]; const char* rn = roster_name[right];
    int lc = 3 + (10 - str_len(ln) / 2); if (lc < 0) lc = 0;
    draw_text(lc, 19, COL_WHITE, ln);
    int rc = 27 - str_len(rn) / 2; if (rc + str_len(rn) > 40) rc = 40 - str_len(rn);
    draw_text(rc, 19, COL_WHITE, rn);
    draw_text_center(23, COL_GREY, ARENA_NAMES[match_venue + 1]);
    draw_credits(28);
}

// ---- main per-frame logic --------------------------------------------------------------------------
void arcade_frame(u32 task) {
    u32 vi = RD32(NM_VI_COUNT);
    int dt = (int)(vi - last_vi);
    if (dt < 0 || dt > 10) dt = 1;
    last_vi = vi;
    frame_no += (u32)dt;
    phase_frames += dt;
    if (!settings_loaded) load_settings();
    draw_begin();

    if (key_edge(SC_5)) { if (credits < 9) credits++; }
    if (key_edge(SC_6)) { if (credits < 9) credits++; }
    int kstart1 = key_edge(SC_1), kstart2 = key_edge(SC_2);
    if (key_edge(SC_F2)) { op_open = !op_open; op_cursor = 0; }
    if (op_open) { operator_menu(); draw_flush(task); return; }

    u16 pr[2] = { take_pressed(0), take_pressed(1) };
    int start[2] = { kstart1 || (pr[0] & BTN_START), kstart2 || (pr[1] & BTN_START) };
    s32 mode = RDS32(NM_MODE);
    s16 scr = RDS16(NM_SCREEN);

    // boot: skip the intro; in attract the game's intro movie may play, 30 s at most
    if (intro_running()) {
        intro_frames += dt;
        if (!booted || phase != ATTRACT || intro_frames > 30 * 60) RDS32(NM_MODE) = 1;
    } else if (RD32(SIG_INTRO_ADDR) != SIG_INTRO) {
        intro_frames = 0;
    }
    if (on_title()) booted = 1;
    if (on_title() && phase == ATTRACT) draw_box(13, 22, 14, 3, 0x000000FFu); // over the game's PRESS START

    switch (phase) {
    case ATTRACT:
    case GAMEOVER: {
        if (phase == GAMEOVER) {
            cover_screen();
            if (phase_frames < 0) {
                draw_text_center(10, COL_YELLOW, "CONGRATULATIONS!");
                draw_text_center(12, COL_WHITE, roster_name[pick[won_player < 0 ? 0 : won_player]]);
                draw_text_center(14, COL_YELLOW, "IS YOUR CHAMPION");
            } else {
                draw_text_center(13, COL_RED, "GAME OVER");
            }
            if (phase_frames > 150 && drv_goal == DRV_NONE && on_title()) { active[0] = active[1] = 0; set_phase(ATTRACT); }
            draw_credits(28);
            break;
        }
        for (int pl = 0; pl < 2; pl++) {
            if (start[pl] && pay()) {
                active[0] = active[1] = 0;
                active[pl] = 1;
                pick[pl] = pl ? 2 : 0;
                ladder_wins = 0;
                start_select(0);
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
    case SELECT:
        screen_select(pr, start);
        break;
    case VS: {
        draw_vs();
        if (drv_at_vs && !vs_applied && scr == SCR_VS) {
            set_slot_wrestler(0, versus ? pick[0] : pick[solo]);
            set_slot_wrestler(1, versus ? pick[1] : opponent);
            vs_applied = 1;
            phase_frames = phase_frames < 60 ? phase_frames : 60;
        }
        if (vs_applied && phase_frames > 180 && drv_goal == DRV_TO_PICK) drv_goal = DRV_START_MATCH;
        if (drv_goal == DRV_START_MATCH && scr == SCR_MATCH) { drv_goal = DRV_NONE; set_phase(ENTRANCE); }
        if (phase_frames > 60 * 60) { drv_goal = DRV_TO_TITLE; set_phase(GAMEOVER); } // safety
        break;
    }
    case ENTRANCE:
    case MATCH: {
        if (phase == ENTRANCE && mode == -2 && RD16(NM_MATCH_FRAMES) != 0xFFFF && RD16(NM_MATCH_FRAMES) > 10) {
            lost_armed = 0;
            set_phase(MATCH);
        }
        if (phase == MATCH && RD16(NM_MATCH_FRAMES) != 0xFFFF) {
            int l0 = RDS32(NM_SLOT_ELIM_TIME) >= 0, l1 = RDS32(NM_SLOT_ELIM_TIME + 4) >= 0;
            if (!l0 && !l1) lost_armed = 1;
            if (lost_armed && l0 != l1) {
                lost_player = slot_player(l0 ? 0 : 1);
                won_player = slot_player(l0 ? 1 : 0);
                set_phase(RESULT);
                break;
            }
        }
        if (phase == MATCH && mode == 0 && scr == SCR_RESULTS) { // ended without a pin/submission (time limit)
            lost_player = versus ? -2 : solo; won_player = -1;
            set_phase(RESULT);
            break;
        }
        // challenger
        for (int pl = 0; pl < 2; pl++) {
            if (!versus && !active[pl] && start[pl] && pay()) {
                active[pl] = 1; pick[pl] = pl ? 2 : 0;
                drv_goal = DRV_QUIT_MATCH; drv_step = 0;
                set_phase(CHALLENGER);
                break;
            }
        }
        if (phase != MATCH && phase != ENTRANCE) break;
        for (int pl = 0; pl < 2; pl++) {
            if (versus || active[pl]) continue;
            if (can_start()) { if (blink(30)) draw_text(pl == 0 ? 1 : 25, 1, pl == 0 ? COL_RED : COL_CYAN, pl == 0 ? "1P PRESS START" : "2P PRESS START"); }
            else if (blink(30)) draw_text(pl == 0 ? 1 : 28, 1, COL_WHITE, "INSERT COIN");
        }
        break;
    }
    case RESULT: {
        if (mode == -2) {
            const char* msg = won_player >= 0 ? (!versus ? "YOU WIN!" : won_player == 0 ? "1P WINS!" : "2P WINS!") : (versus ? "DRAW" : "YOU LOSE");
            draw_box(12, 3, 16, 3, 0x000000C0u);
            draw_text_center(4, won_player >= 0 ? COL_YELLOW : COL_RED, msg);
        }
        if (mode == 0 && scr == SCR_RESULTS) {
            cover_screen();
            if (!versus && lost_player < 0) {           // beat the CPU
                ladder_wins++;
                if (ladder_wins >= ladder_len) {
                    drv_goal = DRV_TO_TITLE;
                    set_phase(GAMEOVER); phase_frames = -300; // champion screen first
                } else {
                    confirmed[solo] = 1;
                    drv_goal = DRV_TO_PICK;
                    begin_vs();                         // next opponent
                }
            } else {
                cont_player = lost_player >= 0 ? lost_player : solo;
                if (versus && lost_player >= 0) { solo = lost_player ^ 1; } // the winner stays on
                set_phase(CONTINUE);
            }
        }
        if (phase_frames > 120 * 60) { drv_goal = DRV_TO_TITLE; set_phase(GAMEOVER); } // safety
        break;
    }
    case CONTINUE: {
        cover_screen();
        int secs = continue_secs - phase_frames / 60;
        if (pr[cont_player] & (BTN_A | BTN_B)) phase_frames += 20;
        if (start[cont_player] && pay()) {
            keep_opponent = !versus;
            ladder_wins = versus ? ladder_wins : ladder_wins; // a continue keeps the ladder position
            start_select(1);
            phase_frames = 5 * 60; // 10 s to (re)pick
            break;
        }
        if (secs < 0) {
            active[cont_player] = 0;
            int other = cont_player ^ 1;
            if (versus && active[other]) {              // the winner faces the CPU next
                solo = other; versus = 0;
                confirmed[other] = 1;
                begin_vs();
            } else {
                drv_goal = DRV_TO_TITLE;
                set_phase(GAMEOVER);
            }
            break;
        }
        char num[4]; str_num(num, secs);
        draw_text_center(9, COL_WHITE, cont_player == 0 ? "PLAYER 1" : "PLAYER 2");
        draw_portrait(pick[cont_player], 240 - THUMB_W, 92, 2, 0xFFFFFFFFu);
        draw_text_center(21, COL_YELLOW, "CONTINUE?");
        draw_text_center(23, COL_RED, num);
        if (can_start()) { if (blink(15)) draw_text_center(25, COL_WHITE, "PRESS START"); }
        else draw_text_center(25, COL_WHITE, "INSERT COIN");
        draw_credits(28);
        break;
    }
    case CHALLENGER: {
        cover_screen();
        if (blink(10)) draw_text_center(12, COL_YELLOW, "HERE COMES A");
        draw_text_center(14, COL_RED, "NEW CHALLENGER!");
        draw_credits(28);
        if (drv_goal == DRV_NONE && phase_frames > 90) start_select(1);
        if (phase_frames > 30 * 60) { drv_goal = DRV_TO_TITLE; set_phase(GAMEOVER); } // safety
        break;
    }
    }

    drive_title();
    draw_flush(task);
}
