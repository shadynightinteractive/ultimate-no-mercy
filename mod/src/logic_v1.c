// WWF No Mercy Arcade: Stone Cold vs The Rock.
//
// Flow:  ATTRACT (title + Austin-vs-Rock demo matches, INSERT COIN)
//          -> coin + start -> SELECT (pick Austin or The Rock, 10 s)
//          -> LAUNCH (the game's attract demo is started and turned into a real match)
//          -> MATCH (player 2 can press start to take over the CPU wrestler)
//          -> RESULT -> CONTINUE (10 s countdown, coin + start = rematch) or GAME OVER.
//
// The game never sees the controllers outside a match: the mod reads them and drives the
// game itself, so its menus never open.
#include "arcade.h"

// ---- overlay signatures (first word of a function, to know which overlay is loaded) ----
#define SIG_INTRO_ADDR 0x80106AB0u
#define SIG_INTRO      0x3C028016u
#define SIG_TITLE_ADDR 0x80111158u
#define SIG_TITLE      0x27BDFFB8u

enum { ATTRACT, SELECT, LAUNCH, MATCH, RESULT, CONTINUE, GAMEOVER, CHALLENGER };

static const char* const ARENA_NAMES[] = { "RANDOM", "RAW IS WAR", "NO MERCY", "SMACKDOWN", "KING OF THE RING",
    "SUMMERSLAM", "SURVIVOR SERIES", "ROYAL RUMBLE", "WRESTLEMANIA", "BACKLASH", "ARMAGEDDON" };

static int phase = ATTRACT;
static int phase_frames;     // frames in the current phase
static u32 frame_no;
static int credits;
static int booted;           // intro skipped once
static int prev_mode;
static u32 last_vi;
static int intro_frames;     // attract: how long the game's intro movie has played

// players: index 0 = P1 (controller 1), 1 = P2 (controller 2)
static int active[2];        // playing in the current game
static int pick[2];          // 0 = Austin, 1 = Rock
static int confirmed[2];
static int slot_of[2];       // match slot the player drives (0/1), -1 = none
static int loser_player;     // RESULT: player index who lost (-1: CPU lost)
static int winner_player;
static int cont_player;      // CONTINUE: who may continue
static int match_decided;
static int launch_pending;   // demo about to start should become the real match
static int launch_converted;
static int demo_set_seen;    // the demo setup ran for this demo (poll fallback)
static int attract_venue;

// operator settings (F2), defaults from the mod config
static int op_open, op_cursor;
static int free_play, continue_secs, arena_sel, attract_demos;
static int settings_loaded;

// input
static u16 pad_raw[4], pad_prev[4], pad_latch[4];
static s8 stick_x[4];
static int stick_dir_prev[4];
static u8 key_prev[256 / 8];

static u32 rng_state = 0x1234567u;
static u32 rng(void) {
    rng_state = rng_state * 1103515245u + 12345u + frame_no;
    return rng_state >> 8;
}

static void set_phase(int p) { phase = p; phase_frames = 0; }

// Keyboard: polled from the controller hook (runs every game frame, even while the game isn't
// drawing), latched, and taken by the frame logic.
static const u8 watched_keys[] = { SC_1, SC_2, SC_5, SC_6, SC_F2, SC_UP, SC_DOWN, SC_LEFT, SC_RIGHT, SC_RETURN };
static u8 key_latch[256 / 8];

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

static void load_settings(void) {
    free_play = (int)plat_config("free_play", 0);
    continue_secs = (int)plat_config("continue_secs", 10);
    if (continue_secs < 5 || continue_secs > 30) continue_secs = 10;
    arena_sel = (int)plat_config("arena", 0);
    if (arena_sel < 0 || arena_sel > 10) arena_sel = 0;
    attract_demos = (int)plat_config("attract_demos", 1) != 0;
    settings_loaded = 1;
}

static int lost_armed;       // saw both loser flags clear in this match

// While our converted match runs, keep it a real match: the game sets the attract-demo bit again
// when it switches to demo mode, and gives demos a 1-minute limit at match init.
static void enforce_real_match(void) {
    if (!launch_converted || (phase != MATCH && phase != RESULT)) return;
    if (phase == RESULT && phase_frames > 20 * 60) return; // being ended
    if (RDS32(NM_MODE) != -6) return;
    u32 f = RD32(NM_MATCH_FLAGS);
    if (f & NM_FLAG_DEMO) RD32(NM_MATCH_FLAGS) = f & ~NM_FLAG_DEMO;
    if (RDS8(NM_TIME_LIMIT) == 7) RD8(NM_TIME_LIMIT) = 0;
}

// ---- controllers ------------------------------------------------------------------------
// Called right after the game read the controllers into NM_PADS: remember them for the
// arcade logic and decide what the game gets to see.
void arcade_pad_hook(void) {
    poll_keys();
    enforce_real_match();
    for (int i = 0; i < 4; i++) {
        u32 p = NM_PADS + 6u * (u32)i;
        u16 b = RD16(p);
        pad_raw[i] = b;
        pad_latch[i] |= (u16)(b & ~pad_prev[i]);
        pad_prev[i] = b;
        stick_x[i] = RDS8(p + 2);

        int pass = 0;
        if (phase == MATCH || phase == RESULT) {
            for (int pl = 0; pl < 2; pl++)
                if (active[pl] && slot_of[pl] >= 0 && i == pl) pass = 1;
        }
        if (op_open) pass = 0;
        if (!pass) {
            RD16(p) = 0;
            RD8(p + 2) = 0;
            RD8(p + 3) = 0;
        } else {
            RD16(p) = (u16)(b & ~BTN_START); // no pausing in the arcade
        }
    }
    if (RDS32(NM_MODE) == -6) {
        // End the running match/demo: launching while an attract demo plays, a challenger
        // stopping the match, or a result that never returns to the title. A demo ends on any
        // button, so mark the match as a demo again and press Start for the game.
        int ending = (phase == LAUNCH && !launch_converted) || phase == CHALLENGER ||
                     (phase == RESULT && phase_frames > 20 * 60);
        if (ending) {
            if (phase != LAUNCH) RD32(NM_MATCH_FLAGS) |= 0x02000000u;
            RD16(NM_PADS) = (frame_no & 4) ? BTN_START : 0;
        } else if (phase == RESULT && phase_frames > 240 && (frame_no & 8)) {
            // after a pinfall/submission the game loops a replay until a button
            RD16(NM_PADS) = BTN_A;
        }
    }
}

static u16 take_pressed(int i) {
    u16 v = pad_latch[i];
    pad_latch[i] = 0;
    return v;
}

// -1 / +1 on a new left/right (d-pad or stick), else 0
static int horizontal_edge(int i, u16 pressed) {
    int dir = 0;
    if (pressed & BTN_DL) return -1;
    if (pressed & BTN_DR) return 1;
    if (stick_x[i] < -40) dir = -1; else if (stick_x[i] > 40) dir = 1;
    int edge = (dir != 0 && dir != stick_dir_prev[i]) ? dir : 0;
    stick_dir_prev[i] = dir;
    return edge;
}

// ---- game control -----------------------------------------------------------------------
static int title_loaded(void) { return RD32(SIG_TITLE_ADDR) == SIG_TITLE; }
static int on_title(void) { return title_loaded() && RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == 0; }
static int intro_running(void) {
    return RD32(SIG_INTRO_ADDR) == SIG_INTRO && RDS32(NM_MODE) == 0 && RDS32(NM_INTRO_SCENE) == 0 &&
           RDS32(NM_INTRO_SKIPPABLE) != 0;
}

static void patch_demo_table(u16 w0, u16 w1) {
    for (int r = NM_DEMO_SINGLES_FIRST; r < NM_DEMO_SINGLES_END; r++) {
        u32 a = NM_DEMO_TABLE + 10u * (u32)r;
        RD16(a) = w0; RD16(a + 2) = w1; RD16(a + 4) = 0; RD16(a + 6) = 0;
    }
}

static int pick_venue(void) {
    if (arena_sel > 0) return arena_sel - 1;
    return (int)(rng() % 10u);
}

// Title screen: keep the attract sequence on Austin-vs-Rock singles demos; when launching,
// start the demo right away (it becomes the real match).
static void drive_title(void) {
    if (!on_title()) return;
    s32 idle = RDS32(NM_TITLE_IDLE);
    u8 counter = RD8(NM_ATTRACT_COUNTER);
    if (phase == LAUNCH) {
        u16 p1w = pick[0] ? WR_ROCK : WR_AUSTIN;
        u16 p2w = pick[0] ? WR_AUSTIN : WR_ROCK;
        if (!active[0] && active[1]) { p1w = pick[1] ? WR_ROCK : WR_AUSTIN; p2w = pick[1] ? WR_AUSTIN : WR_ROCK; }
        patch_demo_table(p1w, p2w);
        if (idle >= 0) RDS32(NM_TITLE_IDLE) = NM_TITLE_IDLE_LIMIT - 1;
        RD8(NM_ATTRACT_COUNTER) = (u8)(NM_DEMO_SET_SINGLES * 2);
        launch_pending = 1;
        return;
    }
    // selecting / continuing / results: hold the title (no demo starts underneath)
    if (phase != ATTRACT) {
        if (idle > 600) RDS32(NM_TITLE_IDLE) = 600;
        launch_pending = 0;
        return;
    }
    // attract: Austin vs Rock (random sides), title shown ~5 s between demos
    if (rng() & 1) patch_demo_table(WR_AUSTIN, WR_ROCK); else patch_demo_table(WR_ROCK, WR_AUSTIN);
    if (!(counter & 1)) RD8(NM_ATTRACT_COUNTER) = (u8)(NM_DEMO_SET_SINGLES * 2);
    if (!attract_demos) {
        if (idle > 600) RDS32(NM_TITLE_IDLE) = 600; // never reaches the demo
    } else if (idle >= 0 && idle < NM_TITLE_IDLE_LIMIT - 150) {
        RDS32(NM_TITLE_IDLE) = NM_TITLE_IDLE_LIMIT - 150;
    }
    launch_pending = 0;
}

// After the game picked the demo match (hook on func_800EF478's return, or polled).
void arcade_demo_setup_done(void) {
    demo_set_seen = 1;
    if (launch_pending && phase == LAUNCH) {
        // The demo becomes a real match: controllers on the players' wrestlers, HUD, no demo limit.
        u32 flags = RD32(NM_MATCH_FLAGS) & ~(NM_FLAG_DEMO | 3u);
        int first = active[0] ? 0 : 1; // the player whose pick is in slot 0
        slot_of[0] = slot_of[1] = -1;
        slot_of[first] = 0;
        RDS32(NM_SLOT_PAD + 0) = first + 1;
        int other = first ^ 1;
        if (active[other]) {
            slot_of[other] = 1;
            RDS32(NM_SLOT_PAD + 4) = other + 1;
        } else {
            RDS32(NM_SLOT_PAD + 4) = 0;
            flags |= 2u; // slot 1 is CPU
        }
        RD32(NM_MATCH_FLAGS) = flags;
        RD8(NM_VENUE) = (u8)pick_venue();
        launch_pending = 0;
        launch_converted = 1;
        match_decided = 0;
        lost_armed = 0;
        set_phase(MATCH);
    } else {
        attract_venue = (int)(rng() % 10u);
        RD8(NM_VENUE) = (u8)attract_venue;
    }
}

static int pay(void) {
    if (free_play) return 1;
    if (credits <= 0) return 0;
    credits--;
    return 1;
}

static int can_start(void) { return free_play || credits > 0; }

// ---- drawing helpers -----------------------------------------------------------------------
static int blink(int period) { return (frame_no / (u32)period) & 1; }

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

static const char* wname(int p) { return p ? "THE ROCK" : "STONE COLD"; }

// ---- operator menu (F2) ----------------------------------------------------------------------
static void operator_menu(void) {
    // keyboard arrows, or controller 1 d-pad
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
        if (op_cursor == 2) arena_sel = (arena_sel + d + 11) % 11;
        if (op_cursor == 3) attract_demos ^= 1;
    }
    if (ok && op_cursor == 4) op_open = 0;

    char num[4];
    draw_box(4, 5, 32, 19, 0x000000D8u);
    draw_text_center(6, COL_YELLOW, "OPERATOR SETTINGS");
    const char* labels[5] = { "FREE PLAY", "CONTINUE TIMER", "ARENA", "ATTRACT DEMOS", "EXIT" };
    for (int i = 0; i < 5; i++) {
        int row = 9 + i * 2;
        u32 c = (i == op_cursor) ? COL_YELLOW : COL_WHITE;
        if (i == op_cursor) draw_text(5, row, COL_YELLOW, ">");
        draw_text(7, row, c, labels[i]);
        if (i == 0) draw_text(24, row, c, free_play ? "ON" : "OFF");
        if (i == 1) { str_num(num, continue_secs); draw_text(24, row, c, num); draw_text(27, row, c, "SEC"); }
        if (i == 2) draw_text(19, row, c, ARENA_NAMES[arena_sel]);
        if (i == 3) draw_text(24, row, c, attract_demos ? "ON" : "OFF");
    }
    draw_text_center(21, COL_GREY, "UP/DOWN SELECT  LEFT/RIGHT CHANGE");
    draw_text_center(22, COL_GREY, "F2 CLOSE");
}

// ---- main per-frame logic ---------------------------------------------------------------------
void arcade_frame(u32 task) {
    // timers run on the game's retrace counter: the game can submit more than one frame per retrace
    u32 vi = RD32(NM_VI_COUNT);
    int dt = (int)(vi - last_vi);
    if (dt < 0 || dt > 10) dt = 1;
    last_vi = vi;
    frame_no += (u32)dt;
    phase_frames += dt;
    if (!settings_loaded) load_settings();
    draw_begin();

    // cabinet keys
    if (key_edge(SC_5)) { if (credits < 9) credits++; }
    if (key_edge(SC_6)) { if (credits < 9) credits++; }
    int kstart1 = key_edge(SC_1), kstart2 = key_edge(SC_2);
    if (key_edge(SC_F2)) { op_open = !op_open; op_cursor = 0; }

    if (op_open) {
        operator_menu();
        draw_flush(task);
        return;
    }

    // the title's own PRESS START would read wrong under arcade text: cover it on the title
    if (on_title()) draw_box(13, 22, 14, 3, 0x000000FFu);

    u16 pr[2] = { take_pressed(0), take_pressed(1) };
    int start[2] = { kstart1 || (pr[0] & BTN_START), kstart2 || (pr[1] & BTN_START) };

    s32 mode = RDS32(NM_MODE);
    // Boot / intro: skip straight to the title the first time, and whenever someone starts.
    // (in attract the game's intro movie may play, but for 30 s at most)
    if (intro_running()) {
        intro_frames += dt;
        if (!booted || phase != ATTRACT || intro_frames > 30 * 60) RDS32(NM_MODE) = 1;
    } else if (RD32(SIG_INTRO_ADDR) != SIG_INTRO) {
        intro_frames = 0;
    }
    if (on_title()) booted = 1;

    enforce_real_match();
    // attract demo started: poll fallback for the demo-setup hook
    // (the hook runs a few frames before the game switches to demo mode; only if it didn't run
    // for this demo, do its work now)
    if (mode == -6 && prev_mode != -6) {
        if (!demo_set_seen) arcade_demo_setup_done();
        demo_set_seen = 0;
    }

    switch (phase) {
    case ATTRACT:
    case GAMEOVER: {
        if (phase == GAMEOVER && phase_frames >= 0) {
            active[0] = active[1] = 0;
            draw_box(11, 12, 18, 3, 0x000000C0u);
            draw_text_center(13, COL_RED, "GAME OVER");
            if (phase_frames > 150) set_phase(ATTRACT);
        }
        for (int pl = 0; pl < 2; pl++) {
            if (start[pl] && phase == ATTRACT && pay()) {
                active[0] = active[1] = 0;
                confirmed[0] = confirmed[1] = 0;
                active[pl] = 1;
                pick[pl] = pl; // P1 defaults to Austin, P2 to The Rock
                set_phase(SELECT);
                break;
            }
        }
        if (phase == ATTRACT) {
            if (mode == -6) draw_text_center(3, COL_YELLOW, "STONE COLD  VS  THE ROCK");
            int row = 25;
            if (on_title()) row = 23; // in the box over the game's PRESS START
            if (can_start()) { if (blink(20)) draw_text_center(row, COL_YELLOW, "PRESS START"); }
            else if (blink(30)) draw_text_center(row, COL_WHITE, "INSERT COIN");
        }
        draw_credits(28);
        break;
    }
    case SELECT: {
        int timer = 10 - phase_frames / 60;
        for (int pl = 0; pl < 2; pl++) {
            if (!active[pl] && start[pl] && pay()) { active[pl] = 1; pick[pl] = pick[pl ^ 1] ^ 1; confirmed[pl] = 0; }
            if (!active[pl] || confirmed[pl]) continue;
            int h = horizontal_edge(pl, pr[pl]);
            if (h) pick[pl] ^= 1;
            if ((pr[pl] & (BTN_A | BTN_START)) || (pl == 0 ? kstart1 : kstart2) || timer <= 0) confirmed[pl] = 1;
        }
        // two players: the second one to confirm gets the other wrestler
        if (active[0] && active[1] && confirmed[0] && confirmed[1] && pick[0] == pick[1]) pick[1] = pick[0] ^ 1;
        draw_box(3, 4, 34, 18, 0x000000B0u);
        draw_text_center(5, COL_YELLOW, "SELECT YOUR SUPERSTAR");
        char num[4]; str_num(num, timer < 0 ? 0 : timer);
        draw_text_center(7, COL_WHITE, num);
        for (int w = 0; w < 2; w++) {
            int col = w == 0 ? 6 : 24;
            draw_text(col, 11, COL_WHITE, w == 0 ? "STONE COLD" : " THE ROCK");
            draw_text(col, 12, COL_GREY, w == 0 ? "STEVE AUSTIN" : "");
            for (int pl = 0; pl < 2; pl++) {
                if (!active[pl] || pick[pl] != w) continue;
                u32 c = pl == 0 ? COL_RED : COL_CYAN;
                if (!confirmed[pl] && blink(8)) continue;
                draw_text(col + 2, 15 + pl, c, pl == 0 ? "< 1P >" : "< 2P >");
            }
        }
        if (!active[1] && can_start()) { if (blink(20)) draw_text_center(19, COL_CYAN, "2P PRESS START TO JOIN"); }
        else if (!active[0] && can_start()) { if (blink(20)) draw_text_center(19, COL_RED, "1P PRESS START TO JOIN"); }
        int all = 1;
        for (int pl = 0; pl < 2; pl++) if (active[pl] && !confirmed[pl]) all = 0;
        if (all && phase_frames > 20) {
            // 2P: P1's pick goes in slot 0. 1P: the player's pick goes in slot 0, the CPU gets the other.
            if (active[0] && active[1] && pick[0] == pick[1]) pick[1] ^= 1;
            launch_converted = 0;
            set_phase(LAUNCH);
        }
        draw_credits(28);
        break;
    }
    case LAUNCH: {
        // get the game to the title screen (skip intro / leave a running attract demo), then the
        // title code starts the demo right away and the demo hook turns it into the real match
        draw_box(12, 13, 16, 3, 0x000000C0u);
        draw_text_center(14, COL_YELLOW, "GET READY!");
        if (phase_frames > 60 * 30) set_phase(ATTRACT); // safety
        break;
    }
    case MATCH: {
        // decision: the game flags the loser's slot
        if (RD16(NM_MATCH_FRAMES) != 0xFFFF && phase_frames > 30) {
            int l0 = RDS32(NM_SLOT_ELIM_TIME) >= 0, l1 = RDS32(NM_SLOT_ELIM_TIME + 4) >= 0;
            if (!l0 && !l1) lost_armed = 1;
            if (lost_armed && l0 != l1) {
                int lost_slot = l0 ? 0 : 1;
                loser_player = -1; winner_player = -1;
                for (int pl = 0; pl < 2; pl++) {
                    if (!active[pl] || slot_of[pl] < 0) continue;
                    if (slot_of[pl] == lost_slot) loser_player = pl; else winner_player = pl;
                }
                set_phase(RESULT);
                break;
            }
        }
        if (mode != -6 && phase_frames > 60 && lost_armed) { // match ended without a decision we saw
            loser_player = active[0] ? 0 : 1; winner_player = -1;
            set_phase(RESULT);
            break;
        }
        // challenger: the free side presses start -> the match stops and a 2-player match starts
        for (int pl = 0; pl < 2; pl++) {
            if (!active[pl] && start[pl] && pay()) {
                active[pl] = 1;
                pick[pl] = pick[pl ^ 1] ^ 1;
                set_phase(CHALLENGER);
                break;
            }
        }
        if (phase != MATCH) break;
        for (int pl = 0; pl < 2; pl++) {
            if (!active[pl] && can_start() && blink(30)) draw_text(pl == 0 ? 1 : 22, 1, pl == 0 ? COL_RED : COL_CYAN, pl == 0 ? "1P PRESS START" : "2P PRESS START");
            else if (!active[pl] && !can_start() && blink(30)) draw_text(pl == 0 ? 1 : 26, 1, COL_WHITE, "INSERT COIN");
        }
        break;
    }
    case RESULT: {
        const char* msg = 0;
        if (winner_player >= 0) msg = winner_player == 0 ? "1P WINS!" : "2P WINS!";
        else msg = "YOU LOSE";
        draw_box(12, 12, 16, 3, 0x000000C0u);
        draw_text_center(13, winner_player >= 0 ? COL_YELLOW : COL_RED, msg);
        // the demo returns to the title by itself; wait for it
        if (mode == 0 && phase_frames > 90) {
            if (loser_player >= 0) {
                cont_player = loser_player;
                // the winner (if any) is out of the game unless the loser continues
                set_phase(CONTINUE);
            } else {
                // beat the CPU: the champion
                set_phase(GAMEOVER);
                phase = GAMEOVER;
                phase_frames = -240; // show the champion screen first
                winner_player = winner_player < 0 ? 0 : winner_player;
            }
        }
        break;
    }
    case CHALLENGER: {
        draw_box(4, 11, 32, 5, 0x000000D0u);
        if (blink(10)) draw_text_center(12, COL_YELLOW, "HERE COMES A");
        draw_text_center(14, COL_RED, "NEW CHALLENGER!");
        if (on_title() && phase_frames > 60) {
            launch_converted = 0;
            set_phase(LAUNCH);
        }
        if (phase_frames > 60 * 20) set_phase(ATTRACT); // safety
        break;
    }
    case CONTINUE: {
        int secs = continue_secs - phase_frames / 60;
        u16 any = pr[cont_player];
        if (any & (BTN_A | BTN_B)) phase_frames += 20; // tap to count down faster
        if (start[cont_player] && pay()) {
            // rematch: same players, the continuing player may switch wrestler
            for (int pl = 0; pl < 2; pl++) confirmed[pl] = 0;
            active[cont_player] = 1;
            set_phase(SELECT);
            phase_frames = 5 * 60; // 5 s to (re)pick
            break;
        }
        if (secs < 0) {
            active[cont_player] = 0;
            int other = cont_player ^ 1;
            if (active[other]) {
                // the winner stays on and faces the CPU
                confirmed[other] = 1;
                launch_converted = 0;
                set_phase(LAUNCH);
            } else {
                set_phase(GAMEOVER);
            }
            break;
        }
        char num[4]; str_num(num, secs);
        draw_box(10, 9, 20, 9, 0x000000C0u);
        draw_text_center(10, COL_WHITE, cont_player == 0 ? "PLAYER 1" : "PLAYER 2");
        draw_text_center(12, COL_YELLOW, "CONTINUE?");
        draw_text_center(14, COL_RED, num);
        if (can_start()) { if (blink(15)) draw_text_center(16, COL_WHITE, "PRESS START"); }
        else draw_text_center(16, COL_WHITE, "INSERT COIN");
        draw_credits(28);
        break;
    }
    }

    // champion screen (negative phase_frames in GAMEOVER)
    if (phase == GAMEOVER && phase_frames < 0) {
        draw_box(6, 9, 28, 9, 0x000000C0u);
        draw_text_center(10, COL_YELLOW, "CONGRATULATIONS!");
        draw_text_center(12, COL_WHITE, wname(pick[winner_player]));
        draw_text_center(14, COL_YELLOW, "IS YOUR CHAMPION");
    }

    drive_title();
    prev_mode = mode;
    draw_flush(task);
}
