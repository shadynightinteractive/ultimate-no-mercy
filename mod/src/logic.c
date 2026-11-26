// WWF No Mercy Arcade (v0.4): the real game, run as a coin-op.
//
//   ATTRACT   title, intro movie and CPU demo matches; INSERT COIN / PRESS START.
//   PLAY      coin + Start, one player: Story Mode's superstar select (a man gets the World
//             Heavyweight Championship story, a woman the Women's). Two players in: Versus, the
//             game's Exhibition menu. Player 2 joins any time ("HERE COMES A NEW CHALLENGER!");
//             a Versus player who drops out leaves the other one in Story Mode. Losing a match:
//             CONTINUE? 10..0; coin + Start carries on where the game is.
//   GAMEOVER  nobody continued: the match ends and the game goes back to its title.
//
// Nothing here presses buttons for the game or covers the screen. Getting somewhere is done
// with the game's own code, the way its menus do it:
//   - a menu choice runs through the menus' action interpreter (func_8010F0A8) and the screen
//     change runs that screen's own set-up (a copy of func_80111158's switch, menu_change);
//   - a match is left through the match loop's own exit (what QUIT MATCH does), and the menus
//     are told which screen to open on afterwards (0x800457A6);
//   - a demo match is ended with its own fade-out (what Start does in a demo).
//
// Also: the entrances can skip the full-screen Titantron video (option; the music keeps
// playing), the stage walk gets a low camera looking up at the wrestler, and wrestlers get up
// sooner.
#if __has_include("modding.h")
#include "modding.h"     // (RECOMP_HOOK: the QA rules hook below)
#define LOGIC_HOOKS 1
#endif
#include "arcade.h"
#include "roster.h"
#ifdef ARCADE_TEST
#define entrance_video(id, c) (((u32 (*)(u32, u32))0x8000BBD0u)((id), (c)))
#else
extern u32 func_8000BBD0(u32 id, u32 costume);
#define entrance_video(id, c) func_8000BBD0((id), (c))
#endif

#define SIG_INTRO_ADDR 0x80106AB0u
#define SIG_INTRO      0x3C028016u
#define SIG_TITLE_ADDR 0x80111158u   // the menus' main loop (ovl_c)
#define SIG_TITLE      0x27BDFFB8u

// menu screens (0x800457A4)
#define SCR_TITLE 0
#define SCR_TRANS_A 1
#define SCR_MULTIPLAY 2      // main menu pages
#define SCR_SINGLEPLAY 3
#define SCR_COMMISSIONER 4
#define SCR_ARENA 5          // Exhibition: ARENA
#define SCR_PLAYERS 6        // Exhibition: PLAYER (1P vs 2P, ...)
#define SCR_EXHIBITION 7     // Exhibition: MATCH (match type)
#define SCR_RULES 21
#define SCR_CHAMPIONSHIP 22  // the title list
#define SCR_STORY_CARD 23    // chapter card / story pages
#define SCR_VS 25
#define SCR_SELECT 26        // superstar select (two halves)
#define SCR_SELECT2 27
#define SCR_RESULTS 29
#define SCR_MATCH_RESULTS 28  // a Multi Play match's results table (Next / Rematch)
#define ACT_RESULTS_NEXT 0x464
#define SCR_TRANS_B 41
#define SCR_MATCH 42

// menu actions (the item tables' action words)
#define ACT_EXHIBITION   0x0402   // Multi Play page, item 0
#define ACT_CHAMPIONSHIP 0x040C   // Single Play page, item 0
#define ACT_TITLE_FIRST  0x0427   // championship list: the first title (World Heavyweight)

#define SFX_BELL    0x0001        // the ring bell (in a match's sound bank; func_800173C0)
#define SFX_IMPACT  0x0413        // a deep hit (the menus' and story's sound bank)

#define NM_SELECT_HOVER SIXA_80076264 // u16: wrestler shown at the superstar select (under the cursor)
#define NM_CHAMP_CONTEXT 0x800A9578u // u8: 8 = Championship (Story Mode) set up
#define CONTEXT_CHAMPIONSHIP 8
#define NM_STORY_BELT   0x800A9579u // u8: the championship played for (0 World Heavyweight, 6 Women's)
#define BELT_HEAVYWEIGHT 0
#define BELT_WOMENS 6

#define COL_PROMPT 0xFFE040FFu   // INSERT COIN / PRESS START / CONTINUE?

enum { ATTRACT, PLAY, GAMEOVER };
enum { GM_NONE, GM_STORY, GM_VERSUS };
enum { G_NONE, G_STORY, G_VERSUS, G_TITLE };   // where the arcade is taking the game

static int phase = ATTRACT;
static int phase_frames;
static u32 frame_no;
// INSERT COIN flashes, hard on / off like a coin-op's: 0.6 s lit, 0.4 s dark
static int coin_lit(void) { return (frame_no % 60u) < 36u; }
static int credits;
static int booted;
static u32 last_vi;
static int credit_flash;     // frames left showing CREDITS after a coin

// players: 0 = P1 (controller 1), 1 = P2 (controller 2)
static int active[2];
static int port_player[2] = { 0, 1 }; // which player is the game's controller 1 / 2
static int game_mode;           // what they're playing: GM_STORY / GM_VERSUS
static int challenger_frames;   // frames left of "HERE COMES A NEW CHALLENGER!"
static int cont_player = -1;    // player on the CONTINUE? countdown
static int cont_frames;
static int lost_pending[2];     // lost and waiting for their turn on the countdown
s8 slot_owner[4];        // the player driving each match slot (-1: CPU)
static int match_armed;         // all slots were in: a new match is running
static int power_start_ok(int pl);
static int match_lost[2];       // this match's loss was already counted

// routing
int goal;                       // G_*
static int goal_frames;
static int goal_wait;           // frames before a running match is left (the challenger banner)
static int leave_asked;         // the match was asked to end
static int leave_asked_at;      // (goal_frames then: asked again if the match is still on a second later)
static int banner_paused;       // the challenger's banner holds the match still (the game's pause flag, ours to let go)
static int leave_fast;          // a challenger: no wait, no fade-in left to finish
static int sfx_pending = -1;    // a sound to play from the controller hook (the game's main loop)
static s16 scr_prev;
static int scr_stable;          // frames the menu screen hasn't changed
static int card_confirmed;
static int card_tally;
int dbg_no_card_skip;
int dbg_no_rules_skip;
int dbg_card_next = -99;
int dbg_rules_wait = 2;

// operator settings (F2)
static int op_open;
static int free_play, continue_secs, attract_demos, fast_getup, titantron, selftest, selftest_done;
// QA scenarios (the hidden qa_* options, set only by Development\qa.ps1 in its own copy of the game; see
// "QA scenarios" below): 0 everywhere else
static int qa_cam_from;
int dbg_list_label;               // (QA: the stories' page shows a picture of the game's on its rows)
static int qa_op, qa_op_at;      // QA: an operator-menu action (qa_op: ACT_ id + 1) that many match seconds in
static int qa_pic_walk;          // QA (qa_op 20): along the PICTURE page, a value at a time, as the menu's left and right set them: the shape, CRT, all of the screen and back, then SAVE AND EXIT
static int qa_bind_row;          // QA (qa_op 22 / 23): 1.. a row of the CONTROLLER / KEYBOARD BUTTONS page whose binding is asked for (a capture started)
static int qa_on, qa_w[4], qa_c[4], qa_arena, qa_ctl[4], qa_special, qa_matches;
static int qa_demo_set = 1;      // QA: the attract demo set a match is made from -- its format (see qa_set_men)
static int qa_rules;             // QA: the game's rule preset for it (0x800F5E78 + 12 e; 0: the demo's own)
static u32 qa_setup_flags;       // QA: its set-up flags outright (0x800A957C; 0: the demo set's)
static int qa_camprobe;           // QA (guestref): the match camera in state.csv's lj_a .. lj_e (qa_camera_probe)
static int qa_arena2, qa_walls, qa_ringstage;
static int qa_fslot, qa_ffrom, qa_fto, qa_fas, qa_fcycle, qa_fsub;   // a move forced: slot qa_fslot - 1's entries qa_ffrom..qa_fto -> qa_fas
static int qa_over_wait = 2;   // (QA: seconds after a match ends before the A presses -- the winner's taunt played out first)
static u32 qa_forced;                             // (how many times)
static int qa_bs;                                  // qa_bsuplex: Bret's back suplex from a back grapple, scripted (below)
static int qa_story;            // QA: a story of ours (bits: 1 the CPU plays the player's matches in it, 2 the arcade drives to it)
static int qa_versus;           // QA: one player's Versus, driven (qa_versus_frame): the CPU plays his Superstar too after that many s
static int qa_versus_p2;        // (... 1: player 2 comes in 10 s into its match: the challenge, played, then single player's again)
static int qa_variant;          // QA: costume variants (variants.c; bits: 1 a tour of the Type arrows before player 1's first pick,
                                //  2 the challenge's picks qa_w1 / qa_w2 on each player's own controller, 4 the Rumble's list in
                                //  state.csv) -- state.csv bs_tries / bs_grabs / bs_step: see qa_state
static int qv_tour;             // (QA qa_variant & 1: presses of the Type arrows' tour still to make: auto_pick_c --
                                //  qa_variant_tour C-rights (8 unset: a family of 8), then C-left, C-right; 2026-11-14)
static int qv_random;           // (QA qa_variant & 8: Random's presses (C-up) still to make before player 1's pick; Random
                                //  draws the Undertaker every time -- variants.c variant_qa_random -- and one of his family)
// costume variants (variants.c): one Superstar in another's box, reached with the Type arrows
u32 variant_base_of(u16 id);     // (a u32 since 2026-11-14: see variants.c)
int variant_same_man(u16 a, u16 b);
int variant_family_n(u16 id);           // (2026-11-14, families: a family's wrestlers in play, any number -- 0: none)
u16 variant_family_at(u16 id, int j);
int variant_shown_look(u16 id, int look);   // (2026-11-14, families: look, or his first not hidden if it is)
u16 variant_box_for(u16 id, int look);       // (... the box holding his variant look; -1: any)
u16 variant_cursor_box(void);                // (... the select's cursor box's own Superstar, 0 not known)
u16 variant_pick_member(u16 id, u32 r);
void variant_select_frame(u32 rec);
static u16 qa_pad_bits[2];      // (QA: the game's controller 1's / 2's player's buttons held this way qa_pad_frames more reads)
static int qa_pad_frames[2];
static void qa_pad(u16 bits) { qa_pad_bits[0] = bits; qa_pad_frames[0] = 5; }
static s8 qa_stick_x;            // (QA: controller 1's stick x held this way qa_stick_frames more reads -- arcade_pad_hook)
static int qa_stick_frames;
static void qa_pad2(u16 bits) { qa_pad_bits[1] = bits; qa_pad_frames[1] = 5; }
enum { QV_TITLE, QV_LIST, QV_DOWN, QV_ROW, QV_MATCH, QV_ARENA, QV_RULES, QV_PICK1, QV_PICK2, QV_FIGHT, QV_AFTER, QV_BACK, QV_LIST2, QV_DONE };
static int qv_step, qv_wait, qv_cursor_down = -1;   // (qa_versus_frame: its step; where DOWN from Survival took the cursor)
static int qv_round, qv_p2_start;                    // (... 1: the challenge's match; player 2's Start asked for)
static int qa_table;                                 // QA: the CPU run at the announce table (qa_table_input)
static int qa_headgrab;                              // QA: the CPU grapples the downed man (qa_headgrab_input)
static int qa_pspecial;                              // QA: slot 0's player does his front special (qa_pspecial_pad)
static void qa_pspecial_pad(void);
static void qa_ringstage_pad(void);
static int qa_rr_ports, qa_rr_out, qa_p2, qa_p2_after_op;
static int qa_rr_folders;        // QA (2026-11-13, handlers1; seatsroot the same): the Rumble's entrants all wrestlers from folders
static int qa_rr_first;          // QA (rumblecrash, 2026-11-22): the first CPU entrant named outright, unchecked -- a Superstar this
                                 //  install hasn't, to prove slots.c's empty-record guard for the Royal Rumble
static int qa_rr_out_after;      // QA: qa_rr_out's eliminations from this many game seconds into the match on
static int qa_rr_ring;           // QA: a Royal Rumble's ring size (rw_rumble_ring; 0: the engine's own)
// Changed for new features (Ultimate No Mercy) -- any32, 2026-11-18: a QA match with extra wrestlers present (slots.c)
static int qa_extras, qa_extras_rule, qa_extras_wait;   // QA: how many extra slots, their rule (0 ringside, 1 elimination), seconds they stand first
// (New feature (Ultimate No Mercy) -- lumberjack, 2026-11-21: the Lumberjack match, lumberjack.c -- F2's count, the MATCH page's row, QA)
extern int lj_count, lj_live, qa_lj_out;
void lumberjack_arm(int on); void lumberjack_arm_qa(int n); int lumberjack_armed(void); int lumberjack_on_records(void);
void lumberjack_qa(u32 q); u32 lumberjack_qa_word(void);
// (lumberjack2, 2026-11-21: F2's PICKED (random / the player's picks) and MATCH (one on one, triple threat, tornado tag);
//  the Superstar Select's picks, the RULES page's tornado round the match flags, the walk-out's and the rest's QA words)
extern int lj_pick, lj_variant, qa_lj_esc, qa_lj_brawl, qa_lj_p1, qa_lj_trace;
extern int qa_gr_p1; extern u32 qa_gr_words[4]; void guestref_qa_pad(u32 pad);   // (guestref.c: player 1 in a Guest Referee match, QA)
void lumberjack_qa_pad(u32 pad);
int lumberjack_select_picks(void); int lumberjack_competitors(void); int lumberjack_variant(void);
void lumberjack_finalize_pre(void); void lumberjack_finalize_post(void); void lumberjack_qa2(u32 q);
static int qa_lj_w[4];           // QA: the lumberjacks the Regular driver picks first on the select (0: any box free)
static int qa_lumberjacks;       // QA: a Lumberjack match with this many lumberjacks (the QA match is the one-on-one)
static int qa_lj_menu;           // QA (ljround8): the Regular driver's type on the Lumberjack MATCH page (0 single, 1 tag, 2 triple;
static int qa_lj_walk;           //  ljmenu10: + 16, the page walked first -- every row, Exhibition's page and back: qa_lj_walk its step)
static int qa_outfight, qa_of_a, qa_of_b = 1, qa_of_stage;   // QA (ljround9): the same fight outside, any match (qa_outfight_step)
static int qa_area_trace;          // QA (ljround9): the place bits traced in the game's log (qa_area_trace_step)
static int qa_void_off;          // QA: ring_void_guard off (2026-11-15: to see a move leave men in the void at all)
static int qa_rr_ring_first;     // QA: ... the first match's, when it differs (2026-11-15, gameplayfix: a small Rumble, then a
                                 //  big one -- what crashed the hands, crash reports 2026-11-15; 0: qa_rr_ring for every match)
static int qa_ring(void);        // (the ring size the QA scenario asks for this match; 0: none)
static u32 qa_watch[2];           // QA: words for the exe's write-watch, chosen by the scenario (0: none)
static int qa_modes;             // QA: state.csv's bs_tries / bs_grabs / bs_step carry the two modes' state (qa_state)
static int qa_ctlmode;           // QA (ctrlsave): state.csv's bs_tries / bs_grabs / bs_step carry CONTROL MODE as read, set and saved (qa_state)
static u32 dbg_ctl_loaded;       // (QA: "controls" as the last settings load read it, + 1; 0 not read yet)
static u32 dbg_ctl_saved;        // (QA: "controls" read back from the settings after the last save, + 1; 0 none yet)
static u32 dbg_ctl_saves;        // (QA: the settings' saves)
static int ctl_migrated;         // (QA: the start-up's migration put CONTROL MODE back to N64, settings_migrate)
static int qa_reg;               // QA: Regular's own menus driven (qa_regular_frame; bits: 1 the title's Start to the main
                                 //  menu, 2 F2 opened there a while, 4 an Exhibition match through the game's own pages)
enum { QR_BOOT, QR_TITLE, QR_MAIN, QR_F2, QR_MULTI, QR_MATCHPAGE, QR_PLAYERS, QR_ARENA, QR_RULES, QR_PICK1, QR_PICK2, QR_FIGHT, QR_AFTER, QR_DONE,
       QR_SP_PAGE, QR_SP_LIST, QR_SP_OPEN, QR_SP_PICK, QR_SP_SELECT, QR_SP_PAGES, QR_RESET, QR_RR_PAGES, QR_OPT_PAGE, QR_OPT_WAIT };
static int qr_step, qr_wait;     // (qa_regular_frame's step, and frames to wait before the next)
static int qa_reg_right;         // (QA, qa_reg 256: RIGHT presses on the Rumble's NUMBER page -- qa_regular_rumble)
static int qa_reg_order;         // (QA, qa_reg 256: the ORDER page's row to take, 1 the first shown; 0: Random -- 2026-11-15)
static int qa_number_way;        // (QA: the NUMBER page's ways tried so far -- qa_state)
static int unlock_all = 1;     // everything the SmackDown Mall sells and the championships unlock
static int rumble_ai = 1;       // the CPU goes for eliminations in a Royal Rumble (gameplay.c)
// The coin's sound: one of the game's own (func_800173C0's sound numbers, the same in the menus and
// in a match). 1 (the default) is the scatter of metallic pings, like coins dropping.
static int coin_sound = 1;
int ann_on = 1;                                     // JR and King at the announce table (announce.c)
int pic_window = 0, pic_shape = 0, pic_smooth = 1, pic_limit = 0, pic_crt = 0, pic_hdr = 0;   // the PICTURE page: the renderer's
static void picture_send(void) {   // (to the renderer, when one's changed: at once, as the operator goes along the page)
    static u32 sent = 0xFFFFFFFFu;
    u32 v = (u32)pic_window | (u32)pic_shape << 1 | (u32)pic_smooth << 4 | (u32)pic_limit << 5 | (u32)pic_crt << 6 | (u32)pic_hdr << 8;
    if (v != sent) { sent = v; plat_picture(v); }
}
int host_volume = 100, host_rumble = 25, host_deadzone = 5;     // the SOUND & FEEL page: the exe's
static void host_send(void) { static s32 a = -1, b = -1, c = -1; if (a != host_volume || b != host_rumble || c != host_deadzone) { a = host_volume; b = host_rumble; c = host_deadzone; plat_host(a, b, c); } }
int comm_on = 1, comm_volume = 80, comm_talk = 2;   // match commentary (commentary.c): on, 0-100, how much they talk 0-3
int comm_lines = 0;                                // ... whose lines: 0 WWE '13's, 1 both games', 2 Legends of WrestleMania's
int comm_splice = 1;                               // the new spliced calls (JR's lead-in + a name: "What a move by" ... "Kurt Angle"), 0 off
// (2026-11-19, roster -- the user: JR's trained AI voice was dropped, so there are no AI lines: the AI VOICE LINES option is gone.
//  A value an older version saved, "commentary_ai", is not in mod.toml any more and is not read.)
static const s16 coin_sfx[] = { -1, 149, 132, 156, 142 };
#define COIN_SOUNDS 4
static int rumble_no_women = 1;  // the Royal Rumble's field: no women, no announcer or referee
static int rumble_one_each = 1;  // ... and one of each man: of two versions of a Superstar (the Undertaker, his 1999 look), one
static int pad_swap;             // players 1 and 2 trade controllers
static int custom_story = 1;     // the championship list's Custom Story row (0: none): the game folder's stories\*.json
// New feature (Ultimate No Mercy) -- automerge, 2026-11-23 (the user: "the option in F2 settings to auto merge wrestlers into
// the same select spot as variants"): F2 > GAME SETTINGS > MERGE SAME WRESTLERS, option "auto_merge" (OFF to start with). The
// exe reads it once, at the start-up, and makes the roster editor's automatic families then (exe arcade_wrestlers.cpp
// "MERGE SAME WRESTLERS"): so, as GAME MODE, a change counts from the next start-up. merge_next is the row (the option as
// it will be); merge_running the option as this run read it (latched at the first load), for the note under the page.
static int merge_next;
static int merge_running = -1;
static int rumble_size = 15;     // single-player Royal Rumble: Superstars in it (2..40)
static int rumble_interval = 30; // ... seconds between entrants (10..120)
static int rumble_ring = 6;      // ... in the ring at once (4..NM_SLOTS_CAP; 6, the engine's own)
static int rumble_start4;        // ... a game-menu Rumble starts with four in the ring (two, as the real one, by default: W10)
static int menu_idle_secs = 90;  // Arcade: seconds left alone at a menu before the cabinet goes back to its attract (0 never: W11)
static int menu_idle_frames;     // (... how long nobody has touched anything, in the menus)
static int reg_pressed;          // (anything newly pressed on a controller since regular_route last looked -- W8)
static int arcade_speed = 1;     // matches at the arcade's pace (arcade_match_step) ...
static int speed_boost = 15;     // ... this much faster than the game's own, in percent (5..30)
static int settings_loaded;
// The per-mode toggles (W9, 2026-11-07; the user's spec: each mode has its own value, F2 shows and changes the running
// mode's). Default ON in Arcade / OFF in Regular: GAME SPEED (arcade_speed above), FAST GET UP (fast_getup), the
// Rumble's continue, abbreviated menus, the RULES page's over-the-top preset for a Rumble, the low stage-walk camera.
// Default ON in both: splash screens, attract. (mode_key: which option holds the running mode's value.)
static int rr_cont_on = 1;       // RUMBLE CONTINUE: an eliminated player is held and can come back in (Arcade: a credit)
static int short_menus = 1;      // ABBREVIATED MENUS: the Story shortcuts (chapter card, RULES, VS card, results' Next, pages)
static int splash_on = 1;        // SPLASH SCREENS: the boot's logos and intro movie (off: straight to the title, at boot)
static int attract_on = 1;       // ATTRACT: the title's movie / demo loop (off: the title waits)
static int rules_preset_on = 1;  // RUMBLE RULES PRESET: a Royal Rumble's RULES page opens over the top only
static int stage_cam_on = 1;     // LOW ENTRANCE CAMERA: the low camera on the stage walk
static int title_seen;           // (the title has come up once since power-on: SPLASH SCREENS only acts before)
// The roster's options (Big Show, the new Superstars, No Mercy Plus's themes and videos): the operator's, and the
// ones the game has (they change between games, never during one: arcade_roster_opt).
static const char* const roster_opt_key[ROSTER_OPTS] = { "big_show", "nmplus_media", "funaki", "gangrel", "mosh", "thrasher", "benoit", "jarrett",
                                                          "droz", "mideon", "meanie", "petegas", "rodney", "joeyabs", "taker99",
                                                          "raven", "regal", "tiger", "brethart", "neidhart", "hogan", "nash", "hall", "savage", "hennig", "giant", "sting", "luger", "goldberg", "ricksteiner", "scottsteiner", "booker", "stevieray", "rey", "piper", "bischoff",
                                                          "brethart", "savage", "scottsteiner", "booker", "stevieray", "rey" };   // (the second IDs: their base's key, RO_BANK0)
// (no2k15, 2026-11-23: "stonecold15" / "rock15", the two 2K15 test slots, removed; a settings file that still has their keys
//  keeps them, unread)
static const u8 roster_opt_def[ROSTER_OPTS] = { 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static int roster_opt[ROSTER_OPTS], roster_live[ROSTER_OPTS], roster_opt_loaded;

// input
static u16 pad_prev[4], pad_latch[4];
static s8 pad_stick[2][2];      // the controllers' sticks as read (the operator menu's directions)
static int op_paused;           // the operator menu paused the match
static u16 held_back[2];        // buttons the arcade took (a row it answers): kept from the game until let go
static u8 key_prev[256 / 8], key_latch[256 / 8];
static u8 pad_coin_prev[2], pad_coin_latch[2];
static const u8 watched_keys[] = { SC_1, SC_2, SC_5, SC_6, SC_F2, SC_UP, SC_DOWN, SC_LEFT, SC_RIGHT, SC_RETURN };

static u32 rng_state = 0x1234567u;
// Each draw also takes in the retrace count and the game's own random number (0x80053780, which
// everything the game does random -- the CPU's every move in the demos and matches -- moves on):
// the moment of every draw counts, so nothing repeats from one power-on to the next.
#define NM_GAME_RAND 0x80053780u
static u32 rng(void) {
    rng_state = rng_state * 1103515245u + 12345u + frame_no + RD32(NM_VI_COUNT) * 2654435761u + RD32(NM_GAME_RAND);
    return rng_state >> 8;
}

// ---- the arcade's log, for crash / freeze reports ---------------------------------------------------
// The exe writes the game's RAM into a report when the game crashes or stops making frames
// (crash_reports\); this is what the arcade was doing: its state each frame, and its last 64 events.
#define FLIGHT        0x807F2000u   // 'ARCL', then the state (u32s), then the events from +0x80
#define FLIGHT_EVENTS 64
static u32 flight_n;
void flight_event(u32 code, u32 a, u32 b) {
    u32 e = FLIGHT + 0x80u + (flight_n % FLIGHT_EVENTS) * 16u;
    RD32(e) = frame_no; RD32(e + 4) = code; RD32(e + 8) = a; RD32(e + 12) = b;
    flight_n++;
    RD32(FLIGHT + 0x7C) = flight_n;
}
static void set_phase(int p) { flight_event(FL_PHASE, (u32)p, (u32)phase); phase = p; phase_frames = 0; }
// Adds room to the game's heap. Runs once the game has set the heap up, then logs how it's going about every 30 seconds.
//
// The game's allocator is func_800007D4 and its free is func_8000093C. It keeps a list of free blocks in address order
// and hands out the smallest block that fits. Each block has a 16-byte header (next, size in 16-byte units, 0, a magic
// number). Its only region was 0x80176000-0x80500000: 3.5 MB, all the room there has ever been for models and a match's
// data. To add a region we make it look like one big block the game allocated, and pass it to the game's own free, which
// puts it in the list. Because the smallest fit wins, things keep going where they always went, and a higher region
// only gets a block when nothing lower can hold it. Nothing about the game's use of the heap changes until it's needed.
//
// WCW_HEAP_TEST (QA) pushes allocations into the new regions by using up the room below them at the start:
//   1: everything above 16 MB (harsher than real use: code that packs addresses into 24 bits breaks)
//   2: only blocks of 32 KB or more go above 16 MB (what large models would do in real use)
//   3: everything into the middle region
// Known not to survive 1 as of 2026-11-05: the select screen's driver and a story's entrance (func_80010314). Modes 2
// and 3 are what real use can reach. logic.c header_versus also searches 0x80180000-0x80200000 for the menu's title.
extern int plat_heap_test(void);
extern void plat_heap_note(u32 low_free, u32 high_free, u32 largest_low);
extern u32 func_800007D4(u32 size);
extern void func_8000093C(u32 p);

// Hands [at, at + units x 16) to the game's heap, by way of its own free.
static void heap_add_region(u32 at, u32 units) {
    RD32(at) = 0; RD32(at + 4) = units; RD32(at + 8) = 0; RD32(at + 12) = 0xCAFE4ABEu;
    RD32(0x800A945Cu)++;         // The allocator's count of blocks given out: free takes one off.
    func_8000093C(at + 16);
}

// The largest free block below `top`, in units.
static u32 heap_largest_below(u32 top) {
    u32 largest = 0, first = RD32(0x800435F0u), p = first, n = 0;
    do { if (p < top && RD32(p + 4) > largest) largest = RD32(p + 4); p = RD32(p); } while (p != first && ++n < 4000);
    return largest;
}

// Allocates from the free blocks below `top`, and never frees it, until the largest one left is under `keep` units.
static void heap_use_up(u32 top, u32 keep) {
    for (int i = 0; i < 1024; i++) {
        const u32 largest = heap_largest_below(top);
        if (largest <= keep + 1u) break;
        func_800007D4((largest - keep - 1u) * 16u);
    }
}

static int heap_regions_in;
void arcade_heap_service(void) {
    static u32 tick;
    const u32 freep_at = 0x800435F0u;
    if (!RD32(freep_at)) return;     // The game hasn't made its first allocation yet.
    if (!heap_regions_in) {
        heap_regions_in = 1;
        const int test = plat_heap_test();
        if (test == 3) heap_use_up(HEAP_MID, 1u);
        heap_add_region(HEAP_MID, HEAP_MID_UNITS);
        heap_add_region(HEAP_HIGH, HEAP_HIGH_UNITS);
        if (test == 1) heap_use_up(HEAP_HIGH, 1u);
        if (test == 2) heap_use_up(HEAP_HIGH, 0x800u);   // (0x800 units: 32 KB)
    }
    if (++tick % 900u == 1u) {
        u32 low = 0, high = 0, big = 0, p = RD32(freep_at), first = p, n = 0;
        do { const u32 u = RD32(p + 4); if (p >= HEAP_HIGH) high += u; else { low += u; if (u > big) big = u; } p = RD32(p); } while (p != first && ++n < 4000);
        plat_heap_note(low, high, big);
    }
}

// More of the game's state each frame (for freeze reports): the draw list, the heap, the match's six
// slots (+0x44..+0x6C), and the last 128 frames' (+0x480: match frames, draw count | pool, heap's
// largest free block | free blocks, the slots' states) -- what led up to it.
#define FLIGHT_HIST 0x480u
#define FLIGHT_HIST_N 128
static u32 flight_hist_n;
static void flight_more(void) {
    u32 draws = RD16(0x800619B2u), pool = RD16(0x800619B0u);
    u32 largest = 0, blocks = 0;
    u32 freep = RD32(0x800435F0u), p = freep;
    if (GAME_PTR(p)) {
        do {
            u32 sz = RD32(p + 4) * 16u;
            if (sz > largest) largest = sz;
            blocks++;
            p = RD32(p);
        } while (p != freep && blocks < 400 && GAME_PTR(p));
    }
    RD32(FLIGHT + 0x44) = draws << 16 | pool;
    RD32(FLIGHT + 0x48) = (largest >> 10) << 16 | blocks;
    RD32(FLIGHT + 0x4C) = RD16(NM_MATCH_FRAMES);
    u32 st = 0;
    for (u32 s = 0; s < 6; s++) {
        u32 w = NM_WRESTLERS - 8u + s * NM_WRESTLER_SIZE;
        RD32(FLIGHT + 0x50 + 4 * s) = (u32)RD16(w + 0xA0) << 16 | (RD32(w + 0x14) & 0xFFFFu);
        st |= (RD32(w + 0x14) & 0xFFu) << (4 * s);       // (low nibbles only, for the history)
    }
    u32 h = FLIGHT + FLIGHT_HIST + (flight_hist_n % FLIGHT_HIST_N) * 16u;
    RD32(h) = frame_no << 16 | (RD16(NM_MATCH_FRAMES) & 0xFFFFu);
    RD32(h + 4) = draws << 16 | pool;
    RD32(h + 8) = (largest >> 10) << 16 | blocks;
    RD32(h + 12) = st;
    flight_hist_n++;
    RD32(FLIGHT + 0x68) = flight_hist_n;
}

// Settings that changed their factory value: applied once to a machine set up before (settings_version).
#define SETTINGS_VERSION 6   // 2: Titantron videos off; the new Superstars one by one, Chris Benoit; 3: Jeff Jarrett
                             // and The Blue Meanie off; 4: Jeff Jarrett and The Blue Meanie on (WrestleMania 2000's
                             // whole models); 5: the two modes -- game_mode from the old "enabled" (settings_migrate);
                             // 6: CONTROL MODE back to N64 once (ctrlsave: what versions 1..5 stored was never the player's)

// ---- the game's two modes: Regular / Arcade (2026-11-07) ----------------------------------------------------------
// The user's spec (Development\scratch\modes_plan.md): Regular is No Mercy's own title, menus and attract with all
// of the mod's enhancements; Arcade is today's coin-op. The mode is chosen in F2 and takes effect at the next
// start-up, so it is read once and kept for the whole run (nm_mode) -- every reader asks nm_mode(), whichever thread
// or hook comes first: the frame task (gfx thread), the controller read (main loop) and an overlay's loading can each
// be the first, and each goes through the same guarded function, which runs the settings' migration before reading the
// mode. EXIT WITHOUT SAVING re-reads the settings (load_settings) but never the running mode.
static volatile int mode_latched;   // nm_mode has read the mode (set last: the mode is in place before it says so)
static volatile int mode_running = MODE_REGULAR;   // the mode this run plays in
static int mode_next = MODE_REGULAR;   // F2's GAME MODE: the option as it will be at the next start-up
static int mode_sv_seen = -1;       // (QA: settings_version as the migration found it)
static int mode_migrated;           // (QA: what the migration did to game_mode: 0 nothing, 1 set Regular, 2 set Arcade)

// The settings' one-time changes, each applied once to a machine that has run an older version (settings_version):
// the factory values that changed (2..4, as load_settings did them until 0.62), then the modes (5). For the modes:
//   - settings_version 0: a new install, or one that only ever ran with the arcade off (with "enabled" off the mod
//     never loaded its settings, so it never wrote a version): game_mode is left as it is (mod.toml's default,
//     Regular, unless the operator set it).
//   - 1..4: an install that ran the arcade's settings: it keeps playing as it did -- "enabled" on means Arcade,
//     off means Regular (the user, 2026-11-07: existing installs keep their config).
// Runs once per start-up, from nm_mode. If two threads both get here before either has latched the mode, both make the
// same writes from the same reading: harmless.
static void settings_migrate(void) {
    int sv = (int)plat_config("settings_version", 0);
    mode_sv_seen = sv;
    if (sv < 2) {
        int ext = (int)plat_config("new_superstars", 1) != 0;
        plat_config_set("titantron", 0);
        for (int k = RO_EXT0; k < RO_EXT0 + 4; k++) plat_config_set(roster_opt_key[k], (u32)ext);
        plat_config_set("benoit", 0);
    }
    if (sv < 3) { plat_config_set("jarrett", 0); plat_config_set("meanie", 0); }
    if (sv < 4) { plat_config_set("jarrett", 1); plat_config_set("meanie", 1); }
    if (sv >= 1 && sv < 5) {
        int arcade_was_on = (int)plat_config("enabled", 1) != 0;
        plat_config_set("game_mode", arcade_was_on ? MODE_ARCADE : MODE_REGULAR);   // (an Enum: its index)
        mode_migrated = arcade_was_on ? 2 : 1;
    }
    // (6, ctrlsave 2026-11-25: up to settings version 5 the "controls" option was read and written as the continue
    //  timer -- hooks_recomp.c plat_config took any key starting "con" for continue_secs -- so every save stored
    //  6-BUTTON, whatever the player had picked: what such a machine holds is not his choice. It goes back to the
    //  factory N64 once; from here on the player's choice is kept. A new install, 0, has none stored: mod.toml's N64.)
    if (sv >= 1 && sv < 6) { plat_config_set("controls", 0); ctl_migrated = 1; }
    if (sv < SETTINGS_VERSION) plat_config_set("settings_version", SETTINGS_VERSION);
}
// The running mode: read the first time it is asked for (after the migration), the same answer every time after.
int nm_mode(void) {
    if (!mode_latched) {
        settings_migrate();
        int m = (int)plat_config("game_mode", MODE_REGULAR) == MODE_ARCADE ? MODE_ARCADE : MODE_REGULAR;
        mode_running = m;
        mode_next = m;
        mode_latched = 1;
    }
    return mode_running;
}
int nm_arcade(void) { return nm_mode() == MODE_ARCADE; }

// The option that holds the running mode's value of a per-mode toggle (W9): F2 reads and writes only that one, the
// other mode's is kept as it is.
static const char* mode_key(const char* arcade_key, const char* regular_key) { return nm_arcade() ? arcade_key : regular_key; }
// (2026-11-14, revfix -- the user: wrestlers are handled by their folders and families.json, not by switches) The roster's
// options are no longer the operator's: every one is ON -- every added Superstar in, Big Show in Benoit's box AND Chris
// Benoit among the added ones (before, Benoit was off by default: no one is left out now; leaving a man out is data, his
// line in a families.json "hidden" list, or his folder taken out). No Mercy Plus's themes and videos stay the operator's.
// A value an older version kept in the settings is not read; the log names each that was off.
static void roster_opts_load(void) {
    nm_mode();   // (the migration first: it may change the roster's options -- the controller read can ask before the settings load)
    void removal_s(const char* s); void removal_say(void);
    for (int k = 0; k < ROSTER_OPTS; k++) {
        if (k == RO_MEDIA) {           // (No Mercy Plus's themes and videos: not a wrestler -- still the operator's, SOUND page)
            roster_opt[k] = roster_live[k] = plat_config(roster_opt_key[k], roster_opt_def[k]) != 0;
            continue;
        }
        roster_opt[k] = roster_live[k] = 1;
        if (k >= RO_BANK0) continue;                          // (the second IDs: their base's key, said once there)
        if (plat_config(roster_opt_key[k], roster_opt_def[k]) == 0) {
            removal_s("roster option \""); removal_s(roster_opt_key[k]);
            removal_s("\" was off: no longer read (the operator menu has no wrestler switches; every added Superstar is in) -- "
                      "to leave him out, name him in a families.json \"hidden\" list, or take his folder out");
            removal_say();
        }
    }
    roster_opt_loaded = 1;
}
static void load_settings(void) {
    nm_mode();                                   // (the one-time migration, and the mode latched: settings_migrate)
    // F2's GAME MODE row shows the option as saved (EXIT WITHOUT SAVING comes back here: an unsaved change dropped);
    // the running mode stays what it was at start-up
    mode_next = (int)plat_config("game_mode", MODE_REGULAR) == MODE_ARCADE ? MODE_ARCADE : MODE_REGULAR;
    roster_opts_load();
    free_play = (int)plat_config("free_play", 0);
    continue_secs = (int)plat_config("continue_secs", 10);
    if (continue_secs < 5 || continue_secs > 30) continue_secs = 10;
    attract_demos = (int)plat_config("attract_demos", 1) != 0;
    fast_getup = (int)plat_config(mode_key("fast_getup", "fast_getup_regular"), 1) != 0;   // (per mode: W9)
    rr_cont_on = (int)plat_config(mode_key("rumble_continue_arcade", "rumble_continue_regular"), 1) != 0;
    short_menus = (int)plat_config(mode_key("short_menus_arcade", "short_menus_regular"), 1) != 0;
    splash_on = (int)plat_config(mode_key("splash_arcade", "splash_regular"), 1) != 0;
    attract_on = (int)plat_config(mode_key("attract_arcade", "attract_regular"), 1) != 0;
    rules_preset_on = (int)plat_config(mode_key("rumble_rules_arcade", "rumble_rules_regular"), 1) != 0;
    stage_cam_on = (int)plat_config(mode_key("stage_cam_arcade", "stage_cam_regular"), 1) != 0;
    unlock_all = (int)plat_config("unlock_all", 1) != 0;
    rumble_ai = (int)plat_config("rumble_ai", 1) != 0;
    coin_sound = (int)plat_config("coin_sound", 1);
    if (coin_sound < 0 || coin_sound > COIN_SOUNDS) coin_sound = 1;
    titantron = (int)plat_config("titantron", 0) != 0;
    tron_screens = (int)plat_config("titantron_screens", 1) != 0;
    tron_live = (int)plat_config("titantron_live", 1) != 0;
    pic_window = (int)plat_config("picture_window", 0); pic_shape = (int)plat_config("picture_shape", 0); pic_smooth = (int)plat_config("picture_smooth", 1) != 0;
    pic_limit = (int)plat_config("picture_limit", 0); pic_crt = (int)plat_config("picture_crt", 0); pic_hdr = (int)plat_config("picture_hdr", 0) != 0;
    picture_send();
    host_volume = (int)plat_config("main_volume", 100); host_rumble = (int)plat_config("rumble_strength", 25); host_deadzone = (int)plat_config("stick_deadzone", 5);
    host_send();
    rumble_no_women = (int)plat_config("rumble_no_women", 1) != 0;
    rumble_one_each = (int)plat_config("rumble_one_each", 1) != 0;
    custom_story = plat_config("custom_story", 1) != 0;
    merge_next = (int)plat_config("auto_merge", 0) != 0;               // (automerge: F2's MERGE SAME WRESTLERS, as saved)
    if (merge_running < 0) merge_running = merge_next;               // (what this run's families were made with: the start-up's)
    int swap = (int)plat_config("pad_swap", 0) != 0;
    if (swap != pad_swap) { plat_pad_swap(); pad_swap = swap; }
    selftest = (int)plat_config("selftest", 0) != 0;
    rumble_size = (int)plat_config("rumble_entrants", 15);
    if (rumble_size < 2 || rumble_size > 40) rumble_size = 15;
    lj_count = (int)plat_config("lumberjacks", 12);                // (lumberjack: F2 > LUMBERJACK MATCH; 12 since lumberjack2)
    if (lj_count < 2 || lj_count > NM_SLOTS_CAP - 2) lj_count = 12;
    lj_pick = (int)plat_config("lumberjack_pick", 0) != 0;            // (lumberjack2: RANDOM / SELECT)
    lj_variant = (int)plat_config("lumberjack_match", 0);             // (lumberjack2: ONE ON ONE / TRIPLE THREAT / TORNADO TAG)
    if (lj_variant < 0 || lj_variant > 2) lj_variant = 0;
    rumble_interval = (int)plat_config("rumble_interval", 30);
    if (rumble_interval < 10 || rumble_interval > 120) rumble_interval = 30;
    rumble_start4 = (int)plat_config("rumble_start4", 0) != 0;   // (W10: a game-menu Rumble's start, 2 or 4 in the ring)
    menu_idle_secs = (int)plat_config("menu_idle_secs", 90);     // (W11)
    if (menu_idle_secs < 0 || menu_idle_secs > 300) menu_idle_secs = 90;
    menu_idle_secs -= menu_idle_secs % 30;
    rumble_ring = (int)plat_config("rumble_ring", 6);
    if (rumble_ring < 4 || rumble_ring > NM_SLOTS_CAP) rumble_ring = NM_SLOTS_CAP < 6 ? NM_SLOTS_CAP : 6;
    arcade_speed = (int)plat_config(mode_key("arcade_speed", "regular_speed"), 1) != 0;   // (GAME SPEED, per mode: W9)
    speed_boost = (int)plat_config("speed_boost", 15);
    if (speed_boost < 5 || speed_boost > 30) speed_boost = 15;
    speed_boost -= speed_boost % 5;
    comm_on = (int)plat_config("commentary", 1) != 0;
    ann_on = (int)plat_config("announcers", 1) != 0;
    {   // CONTROL MODE: 0 N64 (mod.toml's default, a new install's), 1 6-BUTTON
        u32 c = plat_config("controls", 0);
        arc_controls = c != 0;
        dbg_ctl_loaded = c + 1u;
    }
    qa_ctlmode = (int)plat_config("qa_ctlmode", 0) != 0;
    comm_volume = (int)plat_config("commentary_volume", 80);
    if (comm_volume < 0 || comm_volume > 100) comm_volume = 80;
    comm_volume -= comm_volume % 10;
    comm_talk = (int)plat_config("commentary_talk", 2);
    if (comm_talk < 0 || comm_talk > 3) comm_talk = 2;
    // (2026-11-14, revfix -- the user dropped Legends of WrestleMania: no commentary-source choice; LINES FROM is gone from the
    //  operator menu and mod.toml. Always WWE '13's lines, the old default: the pack's WWE '13 rows only (Hollywood Hogan's ten
    //  clips among them), never its Legends rows, which the user's pack still holds. A value an older version kept -- "Both"
    //  or "Legends" -- is not read. The pack itself is as it was.)
    comm_lines = 0;
    comm_splice = (int)plat_config("commentary_splice", 1) != 0;
    qa_on = (int)plat_config("qa_mode", 0);           // 1 a singles match, 2 a Royal Rumble
    qa_w[0] = (int)plat_config("qa_w1", 0); qa_w[1] = (int)plat_config("qa_w2", 0);
    qa_w[2] = (int)plat_config("qa_w3", 0); qa_w[3] = (int)plat_config("qa_w4", 0);
    qa_rules = (int)plat_config("qa_rules", 0) & 0x3F;
    qa_setup_flags = (u32)plat_config("qa_setup_flags", 0);
    qa_demo_set = (int)plat_config("qa_demo_set", 1); if (qa_demo_set < 0 || qa_demo_set > 8 || qa_demo_set == 5) qa_demo_set = 1;
    qa_c[0] = (int)plat_config("qa_c1", 0) & 3; qa_c[1] = (int)plat_config("qa_c2", 0) & 3;
    qa_c[2] = (int)plat_config("qa_c3", 0) & 3; qa_c[3] = (int)plat_config("qa_c4", 0) & 3;
    qa_arena = (int)plat_config("qa_arena", 0);
    qa_arena2 = (int)plat_config("qa_arena2", 0);       // (QA: the arena of the second match on; 0 qa_arena's)
    qa_ringstage = (int)plat_config("qa_ringstage", 0);  // (QA: state.csv bs_* = the area, slot 0's and 1's lift; 2: the men put in the aisle: arena_ringstage.c)
    qa_walls = (int)plat_config("qa_walls", 0);         // (QA: state.csv bs_* = the arena's walls, arena_ring.c)
    qa_ctl[0] = (int)plat_config("qa_ctl1", 0); qa_ctl[1] = (int)plat_config("qa_ctl2", 0);
    qa_ctl[2] = (int)plat_config("qa_ctl3", 0); qa_ctl[3] = (int)plat_config("qa_ctl4", 0);
    qa_special = (int)plat_config("qa_special", 0);
    qa_over_wait = (int)plat_config("qa_over_wait", 2);
    qa_matches = (int)plat_config("qa_matches", 0);
    qa_fslot = qa_on ? (int)plat_config("qa_fslot", 0) : 0;
    qa_bs = qa_on == 2 ? (int)plat_config("qa_bsuplex", 0) : 0;
    qa_rr_ports = qa_on == 2 ? (int)plat_config("qa_rr_ports", 0) : 0;
    qa_rr_folders = qa_on == 2 ? (int)plat_config("qa_rr_folders", 0) : 0;
    qa_rr_first = qa_on == 2 ? (int)plat_config("qa_rr_first", 0) : 0;
    qa_rr_out = qa_on == 2 ? (int)plat_config("qa_rr_out", 0) : 0;
    qa_rr_out_after = qa_on == 2 ? (int)plat_config("qa_rr_out_after", 0) : 0;
    qa_rr_ring = qa_on == 2 ? (int)plat_config("qa_rr_ring", 0) : 0;
    qa_extras = qa_on == 1 ? (int)plat_config("qa_extras", 0) : 0;   // (any32: slots.c)
    qa_extras_rule = (int)plat_config("qa_extras_rule", 0);
    qa_extras_wait = (int)plat_config("qa_extras_wait", 0);
    qa_lumberjacks = qa_on == 1 ? (int)plat_config("qa_lumberjacks", 0) : 0;   // (lumberjack: lumberjack.c)
    // (lumberjack2: read whatever the QA mode -- the menu drivers, qa_reg / qa_versus, run with qa_on 0, as qa_reg itself is
    //  read; every default is 0 = nothing. b5's select run lost its picks and its brawl rate to "qa_on ? ...")
    qa_lj_out = (int)plat_config("qa_lj_out", 0);
    qa_lj_esc = (int)plat_config("qa_lj_esc", 0);
    qa_lj_brawl = (int)plat_config("qa_lj_brawl", 0) - 1;               // (0: the match's own 10%; 1 + percent)
    qa_lj_p1 = (int)plat_config("qa_lj_p1", 0);
    qa_lj_menu = (int)plat_config("qa_lj_menu", 0);                      // (ljround8: the Regular driver's Lumberjack type)                          // (lumberjack4: player 1 played at his controller)
    qa_lj_trace = (int)plat_config("qa_lj_trace", 0);                    // (lumberjack5: the game's log traces, lumberjack.c qa_trace)
    { extern int qa_lj_behind; qa_lj_behind = (int)plat_config("qa_lj_behind", 0); }   // (lumberjack5: grabs from behind, QA)
    { extern int qa_lj_set; qa_lj_set = (int)plat_config("qa_lj_set", 0); }             // (ljtwitch: the user's 11-23 twelve, QA)
    { extern int qa_lj_cam[4]; static const char* const k[4] = { "qa_lj_camx", "qa_lj_camy", "qa_lj_camin", "qa_lj_gap" };   // (lumberjack5: the intro's camera, tried)
      for (int i = 0; i < 4; i++) qa_lj_cam[i] = (int)plat_config(k[i], 0); }
    qa_camprobe = qa_on ? (int)plat_config("qa_camprobe", 0) : 0;        // (guestref: the match camera in lj_a .. lj_e)
    qa_gr_p1 = qa_on ? (int)plat_config("qa_gr_p1", 0) : 0;              // (guestref: player 1 played at his controller, guestref.c)
    for (int i = 0; i < 4; i++) { static const char* const k[4] = { "qa_lj_w1", "qa_lj_w2", "qa_lj_w3", "qa_lj_w4" }; qa_lj_w[i] = (int)plat_config(k[i], 0); }
    qa_void_off = qa_on ? (int)plat_config("qa_void_off", 0) : 0;
    qa_area_trace = qa_on ? (int)plat_config("qa_area_trace", 0) : 0;   // (ljround9: the game's log traces every man's place bits, qa_area_trace_step)
    qa_outfight = qa_on ? (int)plat_config("qa_outfight", 0) : 0; qa_of_a = (int)plat_config("qa_of_a", 0); qa_of_b = (int)plat_config("qa_of_b", 1); qa_of_stage = (int)plat_config("qa_of_stage", 0);   // (ljround9: qa_outfight_step)
    qa_rr_ring_first = qa_on == 2 ? (int)plat_config("qa_rr_ring_first", 0) : 0;
    qa_watch[0] = qa_on == 2 ? (u32)plat_config("qa_watch1", 0) : 0;
    qa_watch[1] = qa_on == 2 ? (u32)plat_config("qa_watch2", 0) : 0;
    qa_table = qa_on == 1 ? (int)plat_config("qa_table", 0) : 0;
    qa_headgrab = qa_on == 1 ? (int)plat_config("qa_headgrab", 0) : 0;
    qa_pspecial = qa_on == 1 ? (int)plat_config("qa_pspecial", 0) : 0;
    { extern int ann_old_back_plane; ann_old_back_plane = qa_table == 2; }   // (QA: the table as it was, for the bug)
    qa_p2 = (int)plat_config("qa_p2", 0);
    qa_p2_after_op = (int)plat_config("qa_p2_after_op", 0);
    qa_story = (int)plat_config("qa_story", 0);
    qa_versus = qa_on ? 0 : (int)plat_config("qa_versus", 0);
    qa_versus_p2 = qa_versus ? (int)plat_config("qa_versus_p2", 0) : 0;
    qa_variant = (int)plat_config("qa_variant", 0);
    if (qa_variant & 1) { const int t = (int)plat_config("qa_variant_tour", 0); qv_tour = (t > 0 ? t : 8) + 2; }   // (2026-11-14, families)
    if (qa_variant & 8) { extern u16 variant_qa_random; variant_qa_random = 0x0003; qv_random = 8; }
    { extern u32 dbg_tron_any; dbg_tron_any = qa_on ? plat_config("qa_tron", 0) : 0; }   // (tron.c: the screen's mode forced)
    { extern int ann_qa_stand; ann_qa_stand = qa_on ? (int)plat_config("qa_ann_stand", 0) : 0; }   // (2: as with the table broken)
    { extern int ann_qa_nan; ann_qa_nan = qa_on ? (int)plat_config("qa_ann_nan", 0) != 0 : 0; }
    {   // a scenario's camera (announce.c qa_camera_mul): on, then the eye and the point looked at, each option
        // the coordinate + 3000 (the options are unsigned)
        extern float qa_cam[7];
        static const char* const k[7] = { "qa_cam", "qa_cam_ex", "qa_cam_ey", "qa_cam_ez", "qa_cam_ax", "qa_cam_ay", "qa_cam_az" };
        qa_cam[0] = qa_on && plat_config(k[0], 0) ? 1.0f : 0.0f;
        qa_cam_from = qa_on ? (int)plat_config(k[0], 0) - 1 : 0;   // (more than 1: on from that many seconds into the match --
                                                                   //  the game's own cameras till then, tron.c's sided pieces)
        for (int i = 1; i < 7; i++) qa_cam[i] = (float)((s32)plat_config(k[i], 3000) - 3000);
    }
    qa_op = qa_on || qa_story ? (int)plat_config("qa_op", 0) : 0; qa_op_at = (int)plat_config("qa_op_at", 0);
    qa_bind_row = qa_on ? (int)plat_config("qa_bind_row", 0) : 0;
    dbg_list_label = qa_on ? (int)plat_config("qa_list_dbg", 0) : 0;
    { extern int ann_zbuf_on; ann_zbuf_on = !(qa_on && plat_config("qa_no_zbuf", 0)); }   // (QA: JR / King's depth buffer off)
    qa_ffrom = (int)plat_config("qa_ffrom", 0); qa_fto = (int)plat_config("qa_fto", 0); qa_fas = (int)plat_config("qa_fas", 0); qa_fcycle = (int)plat_config("qa_fcycle", 0); qa_fsub = (int)plat_config("qa_fsub", 0);
    qa_modes = (int)plat_config("qa_modes", 0) != 0;   // (QA: the modes' state in state.csv's bs_* columns -- qa_state)
    qa_reg = (int)plat_config("qa_reg", 0);            // (QA: Regular's own menus driven -- qa_regular_frame)
    qa_reg_right = (int)plat_config("qa_reg_right", 0); // (QA: RIGHT presses on the Rumble's NUMBER page -- qa_regular_rumble)
    qa_reg_order = (int)plat_config("qa_reg_order", 0);  // (QA: the ORDER page's row, 1.. the rows shown; 0 Random)
    settings_loaded = 1;
}

// The operator's settings are kept: the next power-on starts with them (the mod's options, plat_config_set).
static void save_settings(void) {
    plat_config_set("game_mode", (u32)mode_next);   // (F2's GAME MODE: from the next start-up -- the running mode stays)
    plat_config_set("free_play", (u32)free_play);
    plat_config_set("continue_secs", (u32)continue_secs);
    plat_config_set("attract_demos", (u32)attract_demos);
    plat_config_set(mode_key("fast_getup", "fast_getup_regular"), (u32)fast_getup);     // (the running mode's: W9)
    plat_config_set(mode_key("rumble_continue_arcade", "rumble_continue_regular"), (u32)rr_cont_on);
    plat_config_set(mode_key("short_menus_arcade", "short_menus_regular"), (u32)short_menus);
    plat_config_set(mode_key("splash_arcade", "splash_regular"), (u32)splash_on);
    plat_config_set(mode_key("attract_arcade", "attract_regular"), (u32)attract_on);
    plat_config_set(mode_key("rumble_rules_arcade", "rumble_rules_regular"), (u32)rules_preset_on);
    plat_config_set(mode_key("stage_cam_arcade", "stage_cam_regular"), (u32)stage_cam_on);
    plat_config_set("unlock_all", (u32)unlock_all);
    plat_config_set("rumble_ai", (u32)rumble_ai);
    plat_config_set("coin_sound", (u32)coin_sound);
    plat_config_set("titantron", (u32)titantron);
    plat_config_set("titantron_screens", (u32)tron_screens);
    plat_config_set("titantron_live", (u32)tron_live);
    plat_config_set("picture_window", (u32)pic_window); plat_config_set("picture_shape", (u32)pic_shape); plat_config_set("picture_smooth", (u32)pic_smooth);
    plat_config_set("main_volume", (u32)host_volume); plat_config_set("rumble_strength", (u32)host_rumble); plat_config_set("stick_deadzone", (u32)host_deadzone);
    plat_config_set("picture_limit", (u32)pic_limit); plat_config_set("picture_crt", (u32)pic_crt); plat_config_set("picture_hdr", (u32)pic_hdr);
    plat_config_set("rumble_entrants", (u32)rumble_size);
    plat_config_set("lumberjacks", (u32)lj_count);
    plat_config_set("lumberjack_pick", (u32)lj_pick); plat_config_set("lumberjack_match", (u32)lj_variant);
    plat_config_set("rumble_interval", (u32)rumble_interval);
    plat_config_set("rumble_ring", (u32)rumble_ring);
    plat_config_set("rumble_start4", (u32)rumble_start4);
    plat_config_set("menu_idle_secs", (u32)menu_idle_secs);
    plat_config_set(mode_key("arcade_speed", "regular_speed"), (u32)arcade_speed);
    plat_config_set("speed_boost", (u32)speed_boost);
    plat_config_set("rumble_no_women", (u32)rumble_no_women);
    plat_config_set("custom_story", (u32)custom_story);
    plat_config_set("auto_merge", (u32)merge_next);   // (automerge: MERGE SAME WRESTLERS -- from the next start-up, as GAME MODE)
    plat_config_set("pad_swap", (u32)pad_swap);
    plat_config_set("commentary", (u32)comm_on);
    plat_config_set("announcers", (u32)ann_on);
    plat_config_set("controls", (u32)arc_controls);
    dbg_ctl_saved = plat_config("controls", 9) + 1u; dbg_ctl_saves++;   // (QA: what the settings hold now)
    plat_config_set("commentary_volume", (u32)comm_volume);
    plat_config_set("commentary_talk", (u32)comm_talk);
    plat_config_set("commentary_splice", (u32)comm_splice);
    // (2026-11-14, revfix: the roster's options are not the operator's any more -- not written, an old value stays unread;
    //  but No Mercy Plus's themes and videos, which is no wrestler's)
    plat_config_set(roster_opt_key[RO_MEDIA], (u32)roster_opt[RO_MEDIA]);
}
// The operator menu's RESTORE FACTORY SETTINGS (GAME MODE is left as it is: restoring the settings shouldn't turn a
// cabinet into the home game at its next start-up, or the other way round -- it is chosen on its own row)
static void factory_settings(void) {
    host_volume = 100; host_rumble = 25; host_deadzone = 5;
    pic_window = 0; pic_shape = 0; pic_smooth = 1; pic_limit = 0; pic_crt = 0; pic_hdr = 0;
    free_play = 0; continue_secs = 10; attract_demos = 1; fast_getup = 1; titantron = 0; tron_screens = 1; tron_live = 1; unlock_all = 1; coin_sound = 1; rumble_ai = 1;
    rumble_size = 15; rumble_interval = 30; rumble_ring = NM_SLOTS_CAP < 6 ? NM_SLOTS_CAP : 6; arcade_speed = 1; speed_boost = 15; rumble_no_women = 1; rumble_one_each = 1;
    lj_count = 12; lj_pick = 0; lj_variant = 0;
    custom_story = 1;
    merge_next = 0;                                   // (automerge: MERGE SAME WRESTLERS off, the game's own boxes)
    comm_on = 1; comm_volume = 80; comm_talk = 2; comm_lines = 0; comm_splice = 1; ann_on = 1; arc_controls = 0;
    if (pad_swap) { plat_pad_swap(); pad_swap = 0; }
    for (int k = 0; k < ROSTER_OPTS; k++) roster_opt[k] = k == RO_MEDIA ? roster_opt_def[k] : 1;   // (2026-11-14, revfix: all in)
    // (the per-mode toggles: the running mode's factory values -- ON in Arcade, OFF in Regular; splash and attract ON)
    int arc = nm_arcade();
    arcade_speed = arc; fast_getup = arc; rr_cont_on = arc; short_menus = arc; rules_preset_on = arc; stage_cam_on = arc;
    splash_on = 1; attract_on = 1;
    rumble_start4 = 0; menu_idle_secs = 90;
}

// ---- game state helpers --------------------------------------------------------------------
static int title_loaded(void) { return RD32(SIG_TITLE_ADDR) == SIG_TITLE; }
static int in_menus(void) { return title_loaded() && RDS32(NM_MODE) == 0; }
static int on_title(void) { return in_menus() && RDS16(NM_SCREEN) == SCR_TITLE; }
static int intro_running(void) {
    return RD32(SIG_INTRO_ADDR) == SIG_INTRO && RDS32(NM_MODE) == 0 && RDS32(NM_INTRO_SCENE) == 0 &&
           RDS32(NM_INTRO_SKIPPABLE) != 0;
}
static int match_running(void) { return RDS32(NM_MODE) == -2 && RD16(NM_MATCH_FRAMES) != 0xFFFF; }
static int nobody_playing(void) { return !active[0] && !active[1]; }
static int active_any(void) { return active[0] || active[1]; }

// A roster option as the game has it now: the operator's from when nobody is playing and the operator menu is
// closed (a game in progress keeps its roster).
static const u8 ro_bank_base[ROSTER_BANKS_N] = { RO_BRET, RO_SAVAGE, RO_SSTEINER, RO_BOOKER, RO_STEVIE, RO_REY };
// Between games (W7, 2026-11-07): Arcade -- nobody has paid; Regular, which has no paying players -- the title or a main
// menu page (Multi Play, Single Play, Options) is up: never in a select, a match or the pages leading to one.
static int between_games(void) {
    if (nm_arcade()) return nobody_playing();
    if (!in_menus()) return 0;
    s16 scr = RDS16(NM_SCREEN);
    return scr == SCR_TITLE || scr == SCR_MULTIPLAY || scr == SCR_SINGLEPLAY || scr == SCR_COMMISSIONER;
}
int arcade_roster_opt(int i) {
    if (i >= RO_BANK0 && i < RO_BANK0 + ROSTER_BANKS_N) i = ro_bank_base[i - RO_BANK0];   // (a second ID: his base's option)
    if (!roster_opt_loaded) roster_opts_load();
    if (!op_open && between_games()) roster_live[i] = roster_opt[i];
    // (firstrun 2026-11-17) Without the built-in extras (extras.c: a first start that had only the No Mercy ROM) none of them
    // is in, whatever the options say: no added Superstar, no No Mercy Plus themes and videos -- the game's own roster. With
    // them (every build of the mod) the options as they always were.
    // (phase3 2026-11-19) an added Superstar whose source game the player hasn't: not in, whatever the options say.
    // (nmplus 2026-11-19) One that needs no other game is in with any ROMs, the extras or none. (content2 2026-11-19: Big
    // Show needs No Mercy Plus again -- extras.c -- so without it he is off: Benoit's box, the game's own Story Mode.)
    const u32 need = arcade_ro_needs(i);
    if (need && !arcade_extras_has(need)) return 0;   // (2026-11-19, roster: RO_BENOIT is never asked -- Benoit is always in)
    return roster_live[i];
}
// The roster as the game has it, each frame (the controller read; with the arcade off: the game's own):
// Big Show in Benoit's box; the new Superstars, each on or off; Chris Benoit -- in his own box
// without Big Show, among the new Superstars with him (roster.c ext 4; since 2026-11-19 always in: no option, his box
// never taken off -- a player leaves him out by families.json "hidden" or select_pages.json); No Mercy Plus's themes
// and videos.
void arcade_roster_service(int on) {
    int bs = on && arcade_roster_opt(RO_BIG_SHOW), media = on && arcade_roster_opt(RO_MEDIA);
    u64 mask = 0;   // (a u64: one bit per late Superstar from bit 6 -- 29 of them since ports4)
    for (int k = 0; k < 4; k++) if (on && arcade_roster_opt(RO_EXT0 + k)) mask |= 1u << k;
    for (int k = 0; k < ROSTER_LATE_N; k++) if (on && arcade_roster_opt(RO_JARRETT + k)) mask |= (u64)ROSTER_EXT_JARRETT << k;
    bigshow_frame(bs);
    media_frame(on, bs, media, (u32)(mask >> 6) & 0xFu);   // (media.c reads the first four's: CT_SONG_LATE_N; ROSTER_EXT_JARRETT is bit 6)
    roster_frame(mask, media);
    { void roster_menus_frame(void); if (in_menus()) roster_menus_frame(); }   // (staging seats: the select's window, 2026-11-13)
}
static int is_select(s16 scr) { return scr == SCR_SELECT || scr == SCR_SELECT2; }
static int is_main_page(s16 scr) {
    return scr == SCR_TITLE || scr == SCR_MULTIPLAY || scr == SCR_SINGLEPLAY || scr == SCR_COMMISSIONER ||
           scr == SCR_CHAMPIONSHIP;
}

static int pay(void) {
    if (free_play) return 1;
    if (credits <= 0) return 0;
    credits--;
    credit_flash = 180;
    return 1;
}
static int can_start(void) { return free_play || credits > 0; }

// ---- the menus' own code ---------------------------------------------------------------------
static u32 menu_record(int screen) { return NM_MENU_RECORDS + (u32)screen * NM_MENU_STRIDE; }

// A screen change exactly as the menus' main loop makes it (func_80111158, 0x80112134..0x801122F4):
// the new screen's set-up from the loop's switch (table 0x80154918), then the screen number.
// The pages that need wrestlers loaded (the superstar select 26 / 27, the match card 25, the story's
// chapter card 23, and 8) aren't switched to directly: as the main loop does when a choice leads
// there (0x80111E8C..0x80111F30, 0x801120D4), the page is readied, the sequencer's loading step
// (0x71: it loads the 3D wrestlers, shows screen 41, then opens the page) is given the page to open.
#define NM_PAGE_PENDING  0x801581C0u   // s16: the page the loading step opens
#define SEQ_LOAD_PAGE    0x71
#define SFX_MENU_CONFIRM 0x402
int dbg_no_load_route;
static int menus_loading(void) {
    if (dbg_no_load_route) return 0;
    s32 st = RDS32(NM_SCENE_STEP); return st == SEQ_LOAD_PAGE || st == SEQ_LOAD_PAGE + 1; }
static void menu_change(int next) {
    s16 cur = RDS16(NM_SCREEN);
    if (next < 0 || next == cur) return;
    if (!dbg_no_load_route && RDS16(NM_SCREEN_CHANGED) != 0 && (next == 8 || next == 23 || next == 25 || next == 26 || next == 27)) {
        if (next == 25) plat_game_call(GF_INIT_25, 1, 0, 0);
        if (next == 26 || next == 27) plat_game_call(GF_SELECT_INIT, (u32)next, 1, 0);
        RDS16(NM_PAGE_PENDING) = (s16)next;
        RDS32(NM_SCENE_STEP) = SEQ_LOAD_PAGE;
        if (sfx_pending < 0) sfx_pending = SFX_MENU_CONFIRM;   // (played from the controller hook)
        return;
    }
    u32 rec_cur = menu_record(cur), rec_next = menu_record(next);
    if (RDS16(NM_SCREEN_CHANGED) != 0 && (u32)(next - 2) < 0x29u) {
        switch (next) {
        case 2: case 3: case 4:
            plat_game_call(GF_PAGE_INIT, (u32)next, 0, 0);
            RD16(0x8015832Cu) = 0;
            break;
        case 7: plat_game_call(GF_EXH_INIT, 0, 0, 0); break;
        case 5:
            plat_game_call(GF_INIT_5, 0, 0, 0);
            RD16(rec_cur + 0xA) = 0;
            RD32(0x80106470u) = 0;
            break;
        case 14: plat_game_call(GF_INIT_14, 0, 0, 0); break;
        case 26: case 27: plat_game_call(GF_SELECT_INIT, (u32)next, 0, 0); break;
        case 17: {
            plat_game_call(GF_INIT_17, 0, 0, 0);
            u32 v = RD8(0x800A946Cu);
            RD32(0x80158340u) = v;
            plat_game_call(GF_80003714, v == 0 ? 1u : 0u, 0, 0);
            break;
        }
        case 37: {
            u32 items = RD32(rec_next + 0x14);
            if (RDS16(0x800BD5AAu) == 0) RD16(items + 0x1E2) |= 1;
            else RD16(items + 0x1E2) &= 0xFFF0;
            break;
        }
        case 21: plat_game_call(GF_INIT_21, 0, 0, 0); break;
        case 20: plat_game_call(GF_INIT_20, 0, 0, 0); break;
        case 33: plat_game_call(GF_INIT_33, 0, 0, 0); break;
        case 34: plat_game_call(GF_INIT_34, 0, 0, 0); break;
        case 25:
            plat_game_call(GF_INIT_25, 1, 0, 0);
            RD32(0x800A10D4u) = 0;
            break;
        case 22: plat_game_call(GF_INIT_22, 0, 0, 0); break;
        case 42: RD16(0x8015B694u) = 0; break;
        default: break;
        }
    }
    RDS16(NM_SCREEN) = (s16)next;
}

// Choosing a menu item as the menus do when it's confirmed (func_80110C20): its action word goes
// through the action interpreter, which sets up what the choice means and names the next screen.
static int menu_choose(int screen, int item_index, int action) {
    u32 rec = menu_record(screen);
    u32 items = RD32(rec + 0x14);
    if (!GAME_PTR(items)) {     // that page was never built: build it
        if (screen >= 2 && screen <= 4) plat_game_call(GF_PAGE_INIT, (u32)screen, 0, 0);
        items = RD32(rec + 0x14);
        if (!GAME_PTR(items)) return -1;
    }
    u32 item = items + (u32)item_index * 0x88u;
    int next = plat_game_call(GF_ACTION, (u32)action, rec, item);
    RDS16(NM_SCREEN_CHANGED) = 1;
    if (next >= 0 && next < 0x40) menu_change(next);
    return next;
}

// An item of the screen showing (index < 0: the one under the cursor), confirmed with the action
// its definition carries.
// (a row's definition is the game's, or -- the story's chapter card's, moved for the championship list's Versus row,
// champ_versus_defs -- in the mod's memory, 0x81000000 on)
static int def_ptr(u32 p) { return (GAME_PTR(p)) || 0; }
static int menu_confirm(int screen, int index) {
    u32 rec = menu_record(screen);
    u32 items = RD32(rec + 0x14);
    if (!GAME_PTR(items)) return -1;
    int cursor = index < 0 ? RD8(rec + 5) : index;
    u32 def = RD32(items + (u32)cursor * 0x88u + 0x64);
    if (!def_ptr(def)) return -1;
    return menu_choose(screen, cursor, RD16(def + 0x12));
}

// The page's item that does an action (the first one), chosen.
static int menu_choose_action(int screen, int action) {
    u32 items = RD32(menu_record(screen) + 0x14);
    if (!GAME_PTR(items)) return -1;
    for (int i = 0; i < 16; i++) {
        u32 def = RD32(items + (u32)i * 0x88u + 0x64);
        if (!def_ptr(def)) return -1;
        if (RD16(def + 0x12) == (u16)action) return menu_choose(screen, i, action);
    }
    return -1;
}

int dbg_sel_screen, dbg_sel_item, dbg_sel_action, dbg_sel_result;
u32 dbg_rr_next;                 // tests: the Rumble results table's automatic Next (QA_STATE + 0x240)
u32 dbg_op_done;                 // tests: QUIT MATCH / RESET TO TITLE done

int dbg_keep_voice;



// ---- the Exhibition (Versus) pages: no BELT tab --------------------------------------------------------
// The tab bars (MATCH / PLAYER / ARENA / RULES / BELT, and the other modes' sets) are a table in the
// menus' data: 0x34-byte entries { char name[0x1C]; s32 ?; s32 x; s32 screen; ... }. The BELT
// (title match) tab is moved off the bar before a page builds it.
#define NM_TAB_TABLE      0x80149900u
#define NM_TAB_TABLE_END  0x80149E00u
// (Arcade only, 2026-11-07: Regular keeps the game's title matches -- the BELT tab stays on every bar there)
static void tabs_without_belt(void) {
    if (!nm_arcade() || !title_loaded()) return;
    for (u32 a = NM_TAB_TABLE; a < NM_TAB_TABLE_END; a += 4) {
        if (RD32(a) != 0x42454C54u || RD8(a + 4) != 0) continue;           // "BELT\0"
        if (RDS32(a + 0x24) != 12) continue;                               // (its page: 12)
        RDS32(a + 0x20) = 0x400;                                           // x: off the bar
    }
}

// ---- single-player Royal Rumble ----------------------------------------------------------------------
// The championship list has an eighth row, hidden (the Game Boy Color link-up, label 0x8517,
// action 0x428), in the free spot under Light Heavyweight. The arcade shows it as "Royal Rumble"
// (the Multi Play page's label, 0x8502). Choosing it sets the game's Royal Rumble up the way an
// arcade operator would have it (rr_open): no ORDER / NUMBER / PLAYER / ARENA / RULES pages, the
// Superstar Select with the player's pick only, and the match straight after it.
#define CHAMP_FREE_ROW     7
#define CHAMP_FREE_DEF     0x8014C06Eu   // that row's definition: label id +0xA, action +0x12
#define LABEL_GBC          0x8517
#define LABEL_ROYAL_RUMBLE 0x8502
#define ACT_GBC            0x0428
#define ACT_ROYAL_RUMBLE   0x0404
#define CONTEXT_RUMBLE     1
static u16 inject_bits;                  // a press for the game's controller 1 (from the pad hook)
static int inject_frames;
static void inject_press(u16 bits) { inject_bits = bits; inject_frames = 3; }

static int list_on;
// (Arcade only, 2026-11-07: Regular plays the Rumble from Multi Play, the GBC row stays the game's hidden row)
static void champ_rumble_slot(void) {
    if (!nm_arcade() || !in_menus() || RDS16(NM_SCREEN) != SCR_CHAMPIONSHIP || list_on) return;
    u32 d = CHAMP_FREE_DEF;
    if (RD16(d + 0xA) == LABEL_GBC) RD16(d + 0xA) = LABEL_ROYAL_RUMBLE;   // (its action stays: the arcade takes the row)
    if (RD16(d + 0xA) != LABEL_ROYAL_RUMBLE) return;
    u32 items = RD32(menu_record(SCR_CHAMPIONSHIP) + 0x14);
    if (!GAME_PTR(items)) return;
    u32 row = items + CHAMP_FREE_ROW * 0x88u;
    if (RD32(row + 0x64) != d) return;
    RD16(row + 0x60) = LABEL_ROYAL_RUMBLE;
    if (RD8(row + 0x4B)) { RD8(row + 0x4B) = 0; RD8(row + 0x56) = 0xFF; RD8(row + 0x57) = 0x80; }
}
// ---- Survival: a row of the championship list -------------------------------------------------------
// The game's Survival (Single Play's second item: four in the ring, a new CPU Superstar in for each
// one put out, until the player is) gets the championship list's ninth place, under Royal Rumble.
// The list's "Complete" box (how much of Story Mode the save has done) is made that row: its
// definition (0x16 bytes: x, y, 0x0400, up / down / left / right, label, 0, style, -1, action) is
// changed while the menus' data is loaded, before the page is built. Choosing it starts Survival
// the menus' own way (Single Play's item), its ARENA and RULES pages go by at once, then the
// Superstar Select and the match.
#define CHAMP_SURV_ROW   9
#define CHAMP_SURV_DEF   0x8014C09Au   // the "Complete" box's definition
#define CHAMP_TAG_DEF    0x8014BFEAu   // Tag Team's (row 1: above it, Royal Rumble's wrapped round)
#define LABEL_COMPLETE   0x8519
#define LABEL_SURVIVAL   0x850A        // (Single Play's label)
#define ACT_SURVIVAL     0x0403
#define SINGLEPLAY_SURVIVAL 1
static int sv_open_pending;              // the player chose the Survival row (from the pad hook)
static int sv_setup;                     // its ARENA / RULES pages are on their way
// (Arcade only, 2026-11-07: Regular plays Survival from Single Play, the Complete box stays the game's. Until now this
// ran at every overlay load even with the arcade off: a disabled install's list had a dead Survival row, ACT_GBC.)
static void champ_survival_defs(void) {
    if (!nm_arcade() || !title_loaded()) return;
    u32 d = CHAMP_SURV_DEF;
    if (RD16(d + 0xA) != LABEL_COMPLETE || (RD16(CHAMP_FREE_DEF + 0xA) != LABEL_GBC && RD16(CHAMP_FREE_DEF + 0xA) != LABEL_ROYAL_RUMBLE)) return;
    RDS16(d + 0) = 0x46; RDS16(d + 2) = 0x30;          // under Royal Rumble
    RD8(d + 6) = 7; RD8(d + 7) = 1; RD8(d + 8) = 6; RD8(d + 9) = 6;
    RD16(d + 0xA) = LABEL_SURVIVAL;
    RD8(d + 0xE) = 0x12;                               // (a short row's style, as Hardcore's)
    RD16(d + 0x12) = ACT_GBC;                          // (the arcade takes the row, as Royal Rumble's)
    RD8(CHAMP_FREE_DEF + 7) = CHAMP_SURV_ROW;          // Royal Rumble: down to it
    RD8(CHAMP_TAG_DEF + 6) = CHAMP_SURV_ROW;           // Tag Team: up to it
}
static void survival_open(void);
// ---- a story of ours: a row of the championship list (story.c, mod\stories\*.json) -----------------
// The list's "Belt" box (row 8, under Women's: it shows the belt of the row the cursor is on) is made a
// row of its own, "Custom Story" (the operator's GAME SETTINGS > CUSTOM STORY: which story built into the
// mod; OFF leaves the box as it is). Its label is a text id the game never asks for (story.c gives the
// story's menu name for it). Choosing it starts the story (story_start: its nodes, scenes and text over
// the engine's, for the championship it runs as) and takes that championship's row the menus' own way --
// the cursor put on it and A -- so the Superstar Select and everything after are the game's Story Mode.
// The game's own seven stories are untouched: with no story of ours running, story.c's four patches are
// the game's own code.
#define CHAMP_CUSTOM_ROW  8
#define CHAMP_CUSTOM_DEF  0x8014C084u   // the "Belt" box's definition
#define CHAMP_WORLD_DEF   0x8014BFD4u   // World Heavyweight's (row 0: up from it, to ours)
#define CHAMP_WOMENS_DEF  0x8014C058u   // Women's (row 6: down from it, to ours)
#define LABEL_BELT        0x8518
// The row is a picture, as the list's own (2026-10-28 evening, the user: as if it was always there): "Custom Story"
// in the labels' own lettering (gen/labeltex.py: letters cut from the ROM's labels; FILE_LABEL_CUSTOM, files.c), under
// the label id of the Game Boy Color championship -- the arcade's list has none (its row is the Royal Rumble's) --
// so the game fades it, lights it orange under the cursor and puts its glow bar behind it as it does World's.
// Choosing it opens the stories' list (list_open: on this page), its rows pictures of the stories' menu names (the
// game's own lettering, made when the list opens: storylist_make), the stories the game folder's stories\*.json.
#define LABEL_CUSTOM      0x8517
#define LABEL_TABLE       0x80147F80u   // { u16 file, u16 variant } by a picture label's low byte
#define LABEL_ENTRY(l)    (LABEL_TABLE + 4u * ((u32)(l) & 0xFFu))
#define FILE_LABEL_CUSTOM 0x4C57u
#define FILE_LABEL_VERSUS 0x4C58u       // (the Versus row's: champ_versus_defs)
#define FILE_STORY_LABEL0 0x4E00u       // one a story (its place in the folder's list): never a stale picture in the
#define STORY_LABELS_N    32            //  game's file cache
#define LIST_ROWS         8
#define LIST_LABEL0       0x8525        // (the PPV / King of the Ring MATCH pages' match types: their entries lent)
#define CHAMP_HARDCORE_DEF 0x8014C02Cu
static int custom_open_pending;          // the player chose the row (from the pad hook)
static int custom_launch;                // its championship's row being taken (frames)
static int sp_custom;                    // single player is a story of ours
static int list_top, list_pick_pending = -1, list_back_pending;
// (W10b: the Rumble NUMBER page's three captions -- pictures made like the stories' rows, served as these files)
#define FILE_NP_CAP0  (FILE_STORY_LABEL0 + STORY_LABELS_N)
static u8 np_cap_tex[4][2096] __attribute__((aligned(8)));   // (rumblemenu 2026-11-25: a fourth, NUMBER's own -- FILE_NP_CAP_NUM)
// (lumberjack, 2026-11-21: the Exhibition MATCH page's Lumberjack Match row -- its picture, made like the captions)
#define FILE_LJ_LABEL (FILE_NP_CAP0 + 3u)
// (ljround8: the Lumberjack MATCH page's two settings rows, LUMBERJACKS and PICKED -- ljmenu10: their pictures are the
//  labels' own lettering, label_tex.h label_lj_count / label_lj_pick, as the page's settings are now: storylist_file)
#define FILE_LJ_OPT0  (FILE_NP_CAP0 + 4u)
// (rumblemenu, 2026-11-25: the NUMBER page's own caption, "Number of Superstars" / "Number of Teams", as a picture like the
//  other three, so all four light up the same way: after the Lumberjack pages' files)
#define FILE_NP_CAP_NUM (FILE_NP_CAP0 + 6u)
static int lj_mc = 12, lj_mp;              // the page's settings: how many lumberjacks (2..30), picked (0 random, 1 select)
static u8 lj_label_ram[2096] __attribute__((aligned(8)));   // (ljround8: the row's picture, served from here as rounds 1-7's was)
static u32 dbg_lj_label_reads, dbg_lj_label_sizes;           // (QA: the game's reads of it, its size asked)
static u16 list_saved[LIST_ROWS][2];
static int list_saved_ok;
// Regular's layout (W5, 2026-11-07): the same place, the Belt box (row 8) under Women's, but no Survival row to hang
// off -- World (row 0) up -> it, Women's (row 6) down -> it, it up -> Women's, down -> World, left / right -> itself (the
// right column has no row beside it: the Complete box, row 9, can't be chosen). Its label can't be the GBC row's
// (0x8517: in Regular row 7 is still the game's hidden GBC row, which would carry the picture if the link-up test ever
// passed) and the game has no unused picture label (0x8518..0x851B are the boxes', drawn by their own code; 0x8531 on
// isn't the label table any more): it lends 0x852F, a match type of the PPV / King of the Ring MATCH pages -- pages
// that are never up together with the championship list -- while the list is up (custom_label_lend), as the stories'
// list lends 0x8525..0x852C.
#define LABEL_CUSTOM_REG  0x852F
static u16 custom_lend_was[2];           // (0x852F's entry as the game has it, while ours is lent)
static int custom_lent;
static void champ_custom_defs_regular(void) {
    if (!title_loaded() || custom_story <= 0 || story_count() <= 0) return;
    u32 d = CHAMP_CUSTOM_DEF;
    if (RD16(d + 0xA) != LABEL_BELT) return;            // (done already: until the overlay comes back from the ROM)
    RDS16(d + 0) = RDS16(CHAMP_WOMENS_DEF + 0);
    RDS16(d + 2) = (s16)(RDS16(CHAMP_WOMENS_DEF + 2) + (RDS16(CHAMP_WOMENS_DEF + 2) - RDS16(CHAMP_HARDCORE_DEF + 2)));
    RD8(d + 6) = 6; RD8(d + 7) = 0; RD8(d + 8) = CHAMP_CUSTOM_ROW; RD8(d + 9) = CHAMP_CUSTOM_ROW;
    RD16(d + 0xA) = LABEL_CUSTOM_REG;
    RD8(d + 0xE) = RD8(CHAMP_WOMENS_DEF + 0xE);
    RD16(d + 0x12) = ACT_GBC;                          // (taken by the mod -- regular_pads -- before the game sees it)
    RD8(CHAMP_WOMENS_DEF + 7) = CHAMP_CUSTOM_ROW;      // Women's: down to it
    RD8(CHAMP_WORLD_DEF + 6) = CHAMP_CUSTOM_ROW;       // World Heavyweight: up to it
}
// Each frame in Regular: the row's picture lent while the championship list is up, given back as soon as it isn't.
static void custom_label_lend(void) {
    int up = in_menus() && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP && RD16(CHAMP_CUSTOM_DEF + 0xA) == LABEL_CUSTOM_REG;
    if (up && !custom_lent) {
        custom_lend_was[0] = RD16(LABEL_ENTRY(LABEL_CUSTOM_REG)); custom_lend_was[1] = RD16(LABEL_ENTRY(LABEL_CUSTOM_REG) + 2);
        custom_lent = 1;
    }
    if (up) {
        RD16(LABEL_ENTRY(LABEL_CUSTOM_REG)) = FILE_LABEL_CUSTOM;
        RD16(LABEL_ENTRY(LABEL_CUSTOM_REG) + 2) = RD16(LABEL_ENTRY(0x8516) + 2);   // (Women's quads)
    } else if (custom_lent) {
        if (title_loaded()) { RD16(LABEL_ENTRY(LABEL_CUSTOM_REG)) = custom_lend_was[0]; RD16(LABEL_ENTRY(LABEL_CUSTOM_REG) + 2) = custom_lend_was[1]; }
        custom_lent = 0;
    }
}
// Arcade's layout: under the Survival row's links (below).
static void champ_custom_defs(void) {
    if (!title_loaded()) return;
    if (!nm_arcade()) { champ_custom_defs_regular(); return; }
    if (custom_story <= 0 || story_count() <= 0) return;
    u32 d = CHAMP_CUSTOM_DEF;
    if (RD16(d + 0xA) != LABEL_BELT || RD16(CHAMP_SURV_DEF + 0xA) != LABEL_SURVIVAL) return;   // (after Survival's)
    // under Women's, as far below it as it is below Hardcore (the left column's own spacing), its style and size
    RDS16(d + 0) = RDS16(CHAMP_WOMENS_DEF + 0);
    RDS16(d + 2) = (s16)(RDS16(CHAMP_WOMENS_DEF + 2) + (RDS16(CHAMP_WOMENS_DEF + 2) - RDS16(CHAMP_HARDCORE_DEF + 2)));
    RD8(d + 6) = 6; RD8(d + 7) = 0; RD8(d + 8) = CHAMP_SURV_ROW; RD8(d + 9) = CHAMP_SURV_ROW;
    RD16(d + 0xA) = LABEL_CUSTOM;
    RD8(d + 0xE) = RD8(CHAMP_WOMENS_DEF + 0xE);
    RD16(d + 0x12) = ACT_GBC;                          // (the arcade takes the row)
    RD16(LABEL_ENTRY(LABEL_CUSTOM)) = FILE_LABEL_CUSTOM;
    RD16(LABEL_ENTRY(LABEL_CUSTOM) + 2) = RD16(LABEL_ENTRY(0x8516) + 2);   // (Women's quads)
    RD8(CHAMP_WOMENS_DEF + 7) = CHAMP_CUSTOM_ROW;      // Women's: down to it
    RD8(CHAMP_WORLD_DEF + 6) = CHAMP_CUSTOM_ROW;       // World Heavyweight: up to it
    RD8(CHAMP_SURV_DEF + 8) = CHAMP_CUSTOM_ROW;        // Survival: left / right to it
    RD8(CHAMP_SURV_DEF + 9) = CHAMP_CUSTOM_ROW;
}
static int custom_row_on(void) {   // (the row there: Arcade's label, or Regular's lent one)
    return RD16(CHAMP_CUSTOM_DEF + 0xA) == (nm_arcade() ? LABEL_CUSTOM : LABEL_CUSTOM_REG) && custom_story > 0 && story_count() > 0;
}
static void inject_press(u16 bits);

// A story's picture: its menu name in the menus' own lettering -- the condensed one of their words ("Continue",
// "Decision": the game's rasterizer, func_80009F6C, in the menus' style 0x11) -- set on a label of the game's (128 x
// 32 CI4, the grey ramp of World's palette: index = brightness, 1 the dark outline round the letters), its letters
// as high as the labels' (19 px at most) and squeezed to fit their width, centred where theirs sit.
#include "label_tex.h"   // (ljround8: label_lumberjack too -- a table made from the ROM: the package ships it zeroed, the first start makes
                         //  it again from the player's ROM, exe arcade_firstrun_labels.cpp make_lumberjack)
#define STORY_TEXT_STYLE 0x11u
static u8 story_tex[STORY_LABELS_N][2096] __attribute__((aligned(8)));
static u8 story_tex_ok[STORY_LABELS_N];
u32 dbg_story_tex;
static u8 story_g[32][128];
static int i4_at(u32 tx, int stride, int x, int y) {
    u8 b = RD8(tx + (u32)(y * (stride / 2) + x / 2));
    return (x & 1) ? (b & 15) : (b >> 4);
}
// A story that doesn't compile (story_belt 7: "ERROR: <file>" on the list) shows why when it's picked: the list's
// rows are its reason, a line each (2026-11-02); A or B goes back to the stories.
static int list_why = -1, why_n;
static char why_line[8][26];
static const char* list_text(int i) {
    if (list_why < 0) return story_title(i);
    return i >= 0 && i < why_n ? why_line[i] : "";
}
static int list_count(void) { return list_why >= 0 ? why_n : story_count(); }
static void why_split(const char* e) {         // (the reason in lines of 24 letters at most, broken at spaces; 8 lines)
    why_n = 0;
    int p = 0, n = 0;
    while (e[n]) n++;
    while (p < n && why_n < 8) {
        while (p < n && e[p] == ' ') p++;
        if (p >= n) break;
        int q = p + 24 < n ? p + 24 : n;
        if (q < n) { int b = q; while (b > p && e[b] != ' ') b--; if (b > p) q = b; }
        int k = 0;
        for (int j = p; j < q && k < 25; j++) why_line[why_n][k++] = e[j];
        why_line[why_n][k] = 0;
        why_n++;
        p = q;
    }
    if (p < n && why_n == 8) { int k = 0; while (why_line[7][k]) k++; if (k > 21) k = 21; why_line[7][k++] = '.'; why_line[7][k++] = '.'; why_line[7][k++] = '.'; why_line[7][k] = 0; }
}
// A label picture of a line of text (W10b: made out of storylist_make, which it still serves): "TEX", 128 x 32, CI4, the
// labels' palette; the text in the menus' condensed lettering (the game's rasterizer, func_80009F6C, style 0x11), as high
// as the labels' letters and squeezed to fit, centred, a dark outline round it. 0: the game's text maker had no room
// this time (to be tried again).
static int label_text_draw(u8* t, u32 r);
static int label_text_render(u8* t, const char* text) {
    for (u32 k = 0; k < 48; k++) t[k] = label_custom_story[k];
    for (u32 k = 48; k < 2096; k++) t[k] = 0;
    u32 r = plat_game_text(text, STORY_TEXT_STYLE);
    if (!GAME_PTR(r)) return 0;
    label_text_draw(t, r);
    plat_game_text_free(r);
    return 1;
}
static void storylist_make(int i) {
    if (i < 0 || i >= STORY_LABELS_N || story_tex_ok[i]) return;
    u8* t = story_tex[i];
    for (u32 k = 0; k < 48; k++) t[k] = label_custom_story[k];       // ("TEX", 128 x 32, CI4; the palette)
    for (u32 k = 48; k < 2096; k++) t[k] = 0;
    // (2026-11-02: the game's text maker can be out of room for a moment; a row given up on stayed blank for good -- the list's
    // sixth story never showed. Tried again each frame; the sprite cache lets go of the blank it may have taken.)
    static u8 tried[STORY_LABELS_N];
    u32 r = plat_game_text(list_text(i), STORY_TEXT_STYLE);
    if (!GAME_PTR(r)) { tried[i] = 1; return; }
    story_tex_ok[i] = 1;
    if (tried[i]) { tried[i] = 0; plat_game_call(GF_SPRITE_RELEASE, FILE_STORY_LABEL0 + (u32)i, 0, 0); }
    label_text_draw(t, r);
    plat_game_text_free(r);
}
// (the text's letters from the rasterizer's record r into the label picture t -- the part storylist_make had inline)
static int label_text_draw(u8* t, u32 r) {
    u32 tx = RD32(r + 4);
    int stride = RD16(r + 0xC), h = RD16(r + 0xE), w = RD16(r + 0x10);
    if (GAME_PTR(tx) && w > 0 && h > 0 && stride >= w) {
        int x0 = w, x1 = -1, y0 = h, y1 = -1;                         // (the letters' own extent)
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
            if (i4_at(tx, stride, x, y) >= 3) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        if (x1 >= x0 && y1 >= y0) {
            int sw = x1 - x0 + 1, sh = y1 - y0 + 1;
            int fh = sh > 19 ? 19 : sh, fw = sw * fh / sh;
            if (fw > 118) fw = 118;
            int ox = (128 - fw) / 2, oy = 16 - fh / 2;
            for (int y = 0; y < 32; y++) for (int x = 0; x < 128; x++) story_g[y][x] = 0;
            for (int y = 0; y < fh; y++)
                for (int x = 0; x < fw; x++) {
                    int v = i4_at(tx, stride, x0 + x * sw / fw, y0 + y * sh / fh);
                    story_g[oy + y][ox + x] = (u8)(v < 3 ? 0 : v);
                }
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 128; x++) {
                    if (story_g[y][x]) continue;
                    int near = 0;
                    for (int dy = -1; dy <= 1 && !near; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            int yy = y + dy, xx = x + dx;
                            if (yy >= 0 && yy < 32 && xx >= 0 && xx < 128 && story_g[yy][xx] > 1) { near = 1; break; }
                        }
                    if (near) story_g[y][x] = 1;
                }
            for (int y = 0; y < 32; y++)
                for (int x = 0; x < 128; x += 2) t[48 + y * 64 + x / 2] = (u8)(story_g[y][x] << 4 | story_g[y][x + 1]);
            dbg_story_tex++;
        }
    }
    return 1;
}
// files.c: our label files (0: not one)
u32 storylist_file(u32 file, u32 dest) {
    if (file == FILE_LABEL_CUSTOM) {
        if (dest) for (u32 k = 0; k < sizeof label_custom_story; k++) RD8(dest + k) = label_custom_story[k];
        return sizeof label_custom_story;
    }
    if (file == FILE_LABEL_VERSUS) {
        if (dest) for (u32 k = 0; k < sizeof label_versus; k++) RD8(dest + k) = label_versus[k];
        return sizeof label_versus;
    }
    if (file >= FILE_STORY_LABEL0 && file < FILE_STORY_LABEL0 + STORY_LABELS_N) {
        u32 i = file - FILE_STORY_LABEL0;
        if (dest) for (u32 k = 0; k < 2096; k++) RD8(dest + k) = story_tex[i][k];
        return 2096;
    }
    if (file >= FILE_NP_CAP0 && file < FILE_NP_CAP0 + 3u) {          // (W10b: the NUMBER page's captions)
        u32 i = file - FILE_NP_CAP0;
        if (dest) for (u32 k = 0; k < 2096; k++) RD8(dest + k) = np_cap_tex[i][k];
        return 2096;
    }
    if (file == FILE_NP_CAP_NUM) {                                    // (rumblemenu: NUMBER's caption, the fourth)
        if (dest) for (u32 k = 0; k < 2096; k++) RD8(dest + k) = np_cap_tex[3][k];
        return 2096;
    }
    if (file == FILE_LJ_LABEL) {                                      // (lumberjack: the row -- ljround8: in the labels' own lettering)
        if (!lj_label_ram[0]) for (u32 k = 0; k < sizeof label_lumberjack; k++) lj_label_ram[k] = label_lumberjack[k];
        if (dest) { for (u32 k = 0; k < sizeof lj_label_ram; k++) RD8(dest + k) = lj_label_ram[k]; dbg_lj_label_reads++; }
        else dbg_lj_label_sizes++;
        return sizeof lj_label_ram;
    }
    if (file >= FILE_LJ_OPT0 && file < FILE_LJ_OPT0 + 2u) {          // (ljround8: the Lumberjack MATCH page's settings rows --
        // ljmenu10: the picture of the setting as it is now, in the labels' lettering: "Lumberjacks: N" (N 2..30), "Picked:
        // Random" / "Picked: Select")
        int n = lj_mc < 2 ? 2 : lj_mc > 30 ? 30 : lj_mc;
        const unsigned char* src = file == FILE_LJ_OPT0 ? label_lj_count + 2096u * (u32)(n - 2) : label_lj_pick + 2096u * (u32)(lj_mp & 1);
        if (dest) for (u32 k = 0; k < 2096; k++) RD8(dest + k) = src[k];
        return 2096;
    }
    return 0;
}
// The list is the championship page itself (the game's page 12 doesn't draw its rows when the arcade puts it up
// without its tab): while it's open, its rows 0..7 -- its own two-column grid, World / Tag / IC / European ... --
// show the stories list_top.. in reading order (their own pictures under the labels 0x8525.., lent), the rest of
// its rows hidden, the cursor's links the page's own (hidden rows are passed over); each row faded in again as a
// new page's are. B puts the championships back (faded in the same way); A starts the story on the row.
// (eleven rows with Versus's, champ_versus_defs; ten as the ROM has the page)
#define LIST_PAGE_ROWS_MAX 11
#define NM_PAGE_TABLE      0x8014DFC8u   // { def*, u8 count } a screen: what the menus build each page's rows from
#define PAGE_ENTRY(s)      (NM_PAGE_TABLE + 8u * (u32)(s))
static int champ_rows(void) {
    int n = RD8(PAGE_ENTRY(SCR_CHAMPIONSHIP) + 4);
    return n > LIST_PAGE_ROWS_MAX ? LIST_PAGE_ROWS_MAX : n;
}
#define LIST_PAGE_ROWS champ_rows()
static u16 list_was_label[LIST_PAGE_ROWS_MAX], list_was_flags[LIST_PAGE_ROWS_MAX];
static u8 list_was_cursor;
static u32 champ_items(void) {
    u32 items = RD32(menu_record(SCR_CHAMPIONSHIP) + 0x14);
    return (GAME_PTR(items)) ? items : 0;
}
static void list_rows(void) {
    u32 items = champ_items();
    if (!items) return;
    int n = list_count() - list_top;
    if (n > LIST_ROWS) n = LIST_ROWS;
    for (int k = 0; k < LIST_PAGE_ROWS; k++) {
        u32 row = items + (u32)k * 0x88u;
        if (k < n) {
            int i = list_top + k;
            storylist_make(i);
            RD16(LABEL_ENTRY(LIST_LABEL0 + k)) = (u16)(FILE_STORY_LABEL0 + (u32)i);
            RD16(LABEL_ENTRY(LIST_LABEL0 + k) + 2) = 9;                 // (the 128-px quads)
            RD16(row + 0x60) = dbg_list_label == 1 ? 0x8510 : (u16)(LIST_LABEL0 + k);
            RD16(row + 0x4A) &= 0xFFF0u;
        } else RD16(row + 0x4A) |= 1u;
    }
}
static void list_fade_in(void) {                // (each row from nothing, as a page that comes up)
    u32 items = champ_items();
    if (items) for (int k = 0; k < LIST_PAGE_ROWS; k++) RD8(items + (u32)k * 0x88u + 0x56) = 0;
}
static void list_close(void) {
    if (!list_on) return;
    list_on = 0;
    if (list_why >= 0) { list_why = -1; for (int k = 0; k < STORY_LABELS_N; k++) story_tex_ok[k] = 0; }
    u32 items = champ_items();
    if (items)
        for (int k = 0; k < LIST_PAGE_ROWS; k++) {
            u32 row = items + (u32)k * 0x88u;
            RD16(row + 0x60) = list_was_label[k]; RD16(row + 0x4A) = list_was_flags[k];
        }
    if (list_saved_ok)
        for (int k = 0; k < LIST_ROWS; k++) { RD16(LABEL_ENTRY(LIST_LABEL0 + k)) = list_saved[k][0]; RD16(LABEL_ENTRY(LIST_LABEL0 + k) + 2) = list_saved[k][1]; }
    list_saved_ok = 0;
}
static void list_open(void) {
    story_rescan();
    for (int i = 0; i < STORY_LABELS_N; i++) story_tex_ok[i] = 0;    // (the folder read again: made again)
    u32 items = champ_items();
    if (story_count() <= 0 || !items) return;
    for (int k = 0; k < LIST_PAGE_ROWS; k++) {
        u32 row = items + (u32)k * 0x88u;
        list_was_label[k] = RD16(row + 0x60); list_was_flags[k] = RD16(row + 0x4A);
    }
    for (int k = 0; k < LIST_ROWS; k++) { list_saved[k][0] = RD16(LABEL_ENTRY(LIST_LABEL0 + k)); list_saved[k][1] = RD16(LABEL_ENTRY(LIST_LABEL0 + k) + 2); }
    list_saved_ok = 1;
    list_was_cursor = RD8(menu_record(SCR_CHAMPIONSHIP) + 5);
    list_top = 0; list_on = 1;
    list_rows();
    list_fade_in();
    RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = 0;
    if (sfx_pending < 0) sfx_pending = SFX_MENU_CONFIRM;
}
static void why_show(int i) {                   // (i: the story; -1 the stories again)
    static char e[200];
    int was = list_why;
    if (i >= 0) { story_error(i, e, (int)sizeof e); why_split(e); if (!why_n) return; }
    list_why = i;
    // (the rows' pictures made again, and the game's sprite cache lets go of the old ones: it would go on drawing them)
    for (int k = 0; k < STORY_LABELS_N; k++) { story_tex_ok[k] = 0; plat_game_call(GF_SPRITE_RELEASE, FILE_STORY_LABEL0 + (u32)k, 0, 0); }
    list_top = i >= 0 ? 0 : (was / LIST_ROWS) * LIST_ROWS;
    list_rows();
    list_fade_in();
    RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = (u8)(i >= 0 ? 0 : was - list_top);
}
static void list_back(void) {
    if (list_why >= 0) { why_show(-1); return; }
    list_close();
    list_fade_in();
    RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = list_was_cursor;
}
// a story chosen: started, then its championship's row taken as A on it would be (from this page: the list not
// seen again) -- the game's own Superstar Select and new story (func_800ED28C) follow
static void custom_open(int i) {
    if (list_why >= 0) { why_show(-1); return; }
    if (story_belt(i) == 7) { why_show(i); return; }
    if (!story_start_builtin(i)) return;
    sp_custom = 1;
    list_close();
    RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = (u8)story_belt(i);
    menu_confirm(SCR_CHAMPIONSHIP, story_belt(i));
    custom_launch = 1;
}
static void custom_frame(void) {
    if (list_on) {
        s16 sc = in_menus() ? RDS16(NM_SCREEN) : -1;
        if (sc == SCR_CHAMPIONSHIP) list_rows();
        else if (!menus_loading()) list_close();
    }
    if (list_back_pending) { list_back_pending = 0; list_back(); }
    if (list_pick_pending >= 0) { int i = list_pick_pending; list_pick_pending = -1; custom_open(i); }
    if (!custom_launch) return;
    s16 scr = in_menus() ? RDS16(NM_SCREEN) : -1;
    if (scr != SCR_CHAMPIONSHIP) { custom_launch = 0; return; }   // (the row taken: on to the select)
    if (++custom_launch > 400) { custom_launch = 0; story_stop(); sp_custom = 0; }   // (never taken)
}
// ---- one player's Versus: a row of the championship list ---------------------------------------------
// (2026-10-31, the user: "a mode in Single Player menu where I can select a Versus style match ... myself and a
// computer opponent ... as if I was playing Versus") The list gets an eleventh row, "Versus" -- a picture in the labels'
// own lettering (gen/labeltex.py: V from "Pay Per View", e from "Guest Referee", the rest as Custom Story's; the
// game's own "Exhibition" picture is the Multi Play page's name for it, but the arcade calls two players' matches
// Versus, its MULTI PLAY header reads "versus", and that's what the user asked for) -- under Survival in the right-hand
// column: the arcade's own ways to play, Royal Rumble, Survival, Versus, one under another. Choosing it takes one
// player to the Exhibition menu Versus has (route G_VERSUS: MATCH, PLAYER, ARENA, RULES, then the Superstar Select);
// its PLAYER page is re-dealt with the line-ups of player 1 and the CPU (players_page: 1P vs CPU and the like), so the
// player picks his Superstar and the CPU's at the select, as the game's own 1P vs CPU exhibition has it. It stays single
// player (game_mode GM_STORY: a challenger comes in the Story way; but the RULES page isn't passed by, nor anything
// else Story Mode's): sp_versus says the menus are Versus's (versus_menus) and that single player goes back to them --
// after a match, a challenge, a continue; B on its MATCH page leaves it for the list, the cursor on the row again.
// The eleventh row, the game's own way: the page table (0x8014DFC8: { defs, u8 count } a screen) says 11 for the list,
// its definitions running on into the eleventh place of 0x16 bytes, 0x8014C0B0 -- the story chapter card's (screen 23,
// three rows), which are moved into the mod's memory first (card_defs; the card's table entry pointed there: nothing in
// ovl_c names them but the table, the ROM has no other pointer to them, and the game reads them through the rows built
// from it). The rows come from a pool of 512 (0x11000 bytes, allocated as the menus start): the menus have 386, one
// more is room. Done while ovl_c's data is as the ROM has it, before the pages are built (arcade_overlay_loaded); the
// card's entry still at 0x8014C0B0 is the sign it isn't done yet.
#define CHAMP_ROWS_ROM     10
#define CHAMP_VERSUS_ROW   10
#define CHAMP_VERSUS_DEF   0x8014C0B0u   // (where the chapter card's first definition was)
#define CARD_DEFS_N        3
#define LABEL_VERSUS       0x852D        // (a match type of the PPV MATCH page's -- a page the arcade never shows -- its entry lent)
static u8 card_defs[CARD_DEFS_N * 0x16] __attribute__((aligned(4)));
static int sp_versus;                    // single player is a Versus of his own (the row chosen): the Exhibition menu is his
static int vs_open_pending, vs_back_pending;   // (the row chosen / B on its MATCH page: from the pad hook)
static int vs_cursor_pending;            // (back on the list: the cursor on the row)
// (Arcade only, 2026-11-07: Regular's list keeps its ten rows and the chapter card its own definitions)
static void champ_versus_defs(void) {
    if (!nm_arcade() || !title_loaded()) return;
    if (RD32(PAGE_ENTRY(SCR_STORY_CARD)) != CHAMP_VERSUS_DEF || RD8(PAGE_ENTRY(SCR_CHAMPIONSHIP) + 4) != CHAMP_ROWS_ROM) return;
    if (RD16(CHAMP_SURV_DEF + 0xA) != LABEL_SURVIVAL) return;          // (after Survival's)
    for (u32 k = 0; k < sizeof card_defs; k++) card_defs[k] = RD8(CHAMP_VERSUS_DEF + k);
    RD32(PAGE_ENTRY(SCR_STORY_CARD)) = (u32)card_defs;
    u32 d = CHAMP_VERSUS_DEF;
    for (u32 k = 0; k < 0x16; k++) RD8(d + k) = RD8(CHAMP_SURV_DEF + k);   // (Survival's, then made ours)
    // under Survival, as far below it as it is below Royal Rumble (the right-hand column's own spacing)
    RDS16(d + 2) = (s16)(RDS16(CHAMP_SURV_DEF + 2) + (RDS16(CHAMP_SURV_DEF + 2) - RDS16(CHAMP_FREE_DEF + 2)));
    RD8(d + 6) = CHAMP_SURV_ROW; RD8(d + 7) = 1;                       // up Survival, down round to Tag Team (as Survival did)
    RD8(d + 8) = 6; RD8(d + 9) = 6;                                    // left / right: the left column's last (versus_links)
    RD16(d + 0xA) = LABEL_VERSUS;
    RD8(d + 0xE) = RD8(CHAMP_WOMENS_DEF + 0xE);                        // (a 128-px picture's style, as World's / Women's)
    RD16(d + 0x12) = ACT_GBC;                                          // (the arcade takes the row)
    RD16(LABEL_ENTRY(LABEL_VERSUS)) = FILE_LABEL_VERSUS;
    RD16(LABEL_ENTRY(LABEL_VERSUS) + 2) = RD16(LABEL_ENTRY(0x8516) + 2);   // (Women's quads)
    RD8(CHAMP_SURV_DEF + 7) = CHAMP_VERSUS_ROW;                        // Survival: down to it
    RD8(CHAMP_TAG_DEF + 6) = CHAMP_VERSUS_ROW;                         // Tag Team: up to it
    RD8(PAGE_ENTRY(SCR_CHAMPIONSHIP) + 4) = CHAMP_ROWS_ROM + 1;
}
static int champ_versus_on(void) {
    return title_loaded() && RD8(PAGE_ENTRY(SCR_CHAMPIONSHIP) + 4) > CHAMP_VERSUS_ROW && RD16(CHAMP_VERSUS_DEF + 0xA) == LABEL_VERSUS;
}
// left / right from it: the row beside it in the left column, Custom Story's -- or, the operator's CUSTOM STORIES off
// (the Belt box there, not a row), Women's (the cursor's own links: read from the definition as it moves)
static void versus_links(void) {
    if (!nm_arcade() || !champ_versus_on()) return;   // (Arcade only: the row is the arcade's)
    u8 to = custom_row_on() ? CHAMP_CUSTOM_ROW : 6;
    RD8(CHAMP_VERSUS_DEF + 8) = to; RD8(CHAMP_VERSUS_DEF + 9) = to;
}
static int versus_menus(void);
// An overlay was loaded: the menus' data is as the ROM has it, before they build pages from it.
// The arcade's rows (Survival, Custom Story in its arcade layout, Versus, Ladder Match) only in Arcade -- this hook was
// never gated, so until 2026-11-07 an install with the arcade off got them on the game's own pages (the plan's latent
// bug; discovery D3 saw an 11-row list with "enabled" off). The story runner's patches are both modes'. nm_arcade()
// here may be the run's first reader of the mode: it latches it (after the settings' migration) before any page is built.
extern int dbg_passive;
static void exh_ladder_defs(void);
static void multiplay_lj_defs(void);   // (ljround8: Regular, Multi Play's Lumberjack Match row)
static void lj_page_data(int on);      // (ljmenu10: the Lumberjack MATCH page's own rows, while it is asked for)
static int lj_menu;
static void number_page_defs(void);
void arcade_overlay_loaded(void) {
    { extern void match_slots_after(void); match_slots_after(); }   // (any32: after a match with extra slots, the menus' slots as the type left them -- slots.c)
    if (!dbg_passive && nm_arcade()) { champ_survival_defs(); champ_custom_defs(); champ_versus_defs(); versus_links(); exh_ladder_defs(); }
    if (!dbg_passive && !nm_arcade()) champ_custom_defs();   // (Regular's own layout of the Custom Story row, W5)
    if (!dbg_passive && !nm_arcade()) multiplay_lj_defs();   // (ljround8: Multi Play's Lumberjack Match row, Regular)
    if (!dbg_passive && !nm_arcade()) lj_page_data(lj_menu); // (ljmenu10: the ROM's data again -- the Lumberjack page's rows put back while asked for)
    if (!dbg_passive) number_page_defs();                    // (the Rumble's NUMBER page's own rows, both modes: W10b)
    story_install();
}

// ---- Versus: Ladder Match among the match types ------------------------------------------------------
// The Exhibition MATCH page lists Single / Tag / Triple Threat / Handicap / Cage; its sixth row
// (unused there) is shown as "Ladder Match" (Multi Play's label). Choosing it goes on as the game's
// own Ladder Match (Multi Play's item: its MATCH page, Single / Tag / Triple Threat / Handicap ladder
// matches, then PLAYER, ARENA, RULES and the Superstar Select). B there goes back to Exhibition.
#define EXH_LADDER_ROW   5
#define LABEL_LADDER     0x8506
#define LABEL_CAGE       0x8520
#define ACT_LADDER       0x040E
#define MULTIPLAY_LADDER 5
static int ladder_open_pending;
static u16 ob_taken(u16 e) { return (u16)(e & (BTN_A | BTN_START)); }
static int exh_is_exhibition(void) {             // the MATCH page as Exhibition has it (not Ladder Match's)
    u32 items = RD32(menu_record(SCR_EXHIBITION) + 0x14);
    if (!GAME_PTR(items)) return 0;
    u32 cage = items + 4u * 0x88u;           // (Ladder Match's has Cage Match hidden)
    return RD16(cage + 0x60) == LABEL_CAGE && RD8(cage + 0x4B) == 0;
}
static int exh_rows_in(void) {                   // the MATCH page has rows to show
    u32 items = RD32(menu_record(SCR_EXHIBITION) + 0x14);
    if (!GAME_PTR(items)) return 0;
    for (u32 i = 0; i < 6; i++) if (RD8(items + i * 0x88u + 0x4B) == 0) return 1;
    return 0;
}
// Versus's MATCH page come up with no rows (got to without its set-up): set up as the game does (GF_EXH_INIT)
static int exh_fix_frames;
static void exh_rows_fix(void) {
    if (!nm_arcade()) return;                          // (Arcade only: a page the arcade's routing reaches)
    if (!in_menus() || RDS16(NM_SCREEN) != SCR_EXHIBITION || menus_loading() || exh_rows_in()) { exh_fix_frames = 0; return; }
    if (++exh_fix_frames < 10) return;                                 // (a page coming up has none for a moment)
    plat_game_call(GF_EXH_INIT, 0, 0, 0);
    RD8(menu_record(SCR_EXHIBITION) + 5) = 0;
    exh_fix_frames = 0;
}
// As the game's own rows (2026-10-28 evening, the user: "as if it was always there"): Multi Play's label 0x8506 is
// drawn at its rows' size here too -- its table entry's variant 9 (the 128-px quads Single / Triple / Handicap have;
// with variant 0 it took this page's style-2 quads: ~12% narrower, 20% shorter, squashed; Multi Play's own row, style
// 5, has the same quads as variant 9), unhidden the game's way (its fade and light the game's), and out of the
// left / right chains (this page's defs are the PPV grid's: Single left went to it, natively Cage).
#define LADDER_LABEL_ENTRY 0x80147F98u  // 0x80147F80 + 4 x 6: { u16 file, u16 variant }
#define EXH_DEF(k) (0x8014CAB4u + 0x16u * (u32)(k))
static void exh_ladder_defs(void) {
    if (!nm_arcade() || !title_loaded()) return;       // (Arcade only, 2026-11-07: Regular has Ladder Match on Multi Play)
    if (RD16(LADDER_LABEL_ENTRY) != 0x448C) return;            // (ovl_c's data as the ROM has it)
    RD16(LADDER_LABEL_ENTRY + 2) = 9;
    if (RD8(EXH_DEF(11) + 8) != EXH_LADDER_ROW) return;
    RD8(EXH_DEF(11) + 8) = 10; RD8(EXH_DEF(10) + 9) = 11;
    RD8(EXH_DEF(5) + 8) = EXH_LADDER_ROW; RD8(EXH_DEF(5) + 9) = EXH_LADDER_ROW;
}
static u16 ladder_row_flags;             // the row as the page had it (label, hidden)
static u16 ladder_row_label;
static u32 ladder_row_at;
static void exh_ladder_slot(void) {
    if (!nm_arcade() || !in_menus() || RDS16(NM_SCREEN) != SCR_EXHIBITION) return;   // (Arcade only, as its definitions)
    u32 items = RD32(menu_record(SCR_EXHIBITION) + 0x14);
    if (!GAME_PTR(items)) return;
    u32 row = items + EXH_LADDER_ROW * 0x88u, r4 = items + 4u * 0x88u, r3 = items + 3u * 0x88u;
    if (!exh_is_exhibition()) {                  // Ladder Match's own page: the row as it was
        if (ladder_row_at == row && RD16(row + 0x60) == LABEL_LADDER) {
            RD16(row + 0x60) = ladder_row_label; RD16(row + 0x4A) = ladder_row_flags;
        }
        return;
    }
    if (RD16(row + 0x60) != LABEL_LADDER) {
        ladder_row_at = row;
        ladder_row_label = RD16(row + 0x60); ladder_row_flags = RD16(row + 0x4A);
    }
    RD16(row + 0x60) = LABEL_LADDER;
    // under Cage Match, as far below it as it is below Handicap Match (x, y: floats at +0x40, +0x44)
    union { float f; u32 u; } x4, y4, y3, y5;
    x4.u = RD32(r4 + 0x40); y4.u = RD32(r4 + 0x44); y3.u = RD32(r3 + 0x44);
    y5.f = y4.f + (y4.f - y3.f);
    RD32(row + 0x40) = x4.u; RD32(row + 0x44) = y5.u;
    if (RD16(row + 0x4A) & 0xF) RD16(row + 0x4A) &= 0xFFF0u;   // (the game's own unhide: it fades in with the rest)
}
// ---- the Lumberjack Match row (Regular until ljround8: the Exhibition MATCH page's sixth row; Arcade: Versus's seventh) --
// New feature (Ultimate No Mercy) -- lumberjack, 2026-11-21 (docs\MATCH_SLOTS.md section 4, lumberjack.c). The game's own
// Exhibition MATCH page (Multi Play > Exhibition) lists Single / Tag / Triple Threat / Handicap / Cage; its sixth row,
// unused there (Arcade's Versus shows Ladder Match on it), is shown as "Lumberjack Match" under Cage Match, as far below
// it as Cage is below Handicap: its picture in the game's own menu lettering (label_text_render, as the NUMBER page's
// captions), on the label 0x852E -- a match type of the PPV / King of the Ring MATCH pages, never up with this one -- lent
// while the page is up (as Regular's Custom Story label, 0x852F), the row's own label and flags put back as soon as it
// isn't. Choosing it is choosing Single Match (its press goes to Single Match's row: the game's own action, then PLAYER,
// ARENA, RULES, the Superstar Select, the match card) with the Lumberjack match asked for (lumberjack_arm): the match
// records' hook adds the lumberjacks. No data of the menus is changed (the PPV page shares these rows' definitions): the
// row is kept out of the left / right chains by the cursor itself -- LEFT / RIGHT that would land on it go on past it,
// as the page would without it; from it they stay. Choosing another row, or leaving for Multi Play, ends the request.
// (lumberjack2, 2026-11-21, the user: "Arcade mode: the option goes right after Ladder Match") Arcade's MATCH page (Versus)
// has Ladder Match on the sixth row: there the row is the seventh, under Ladder Match (the page has 17 rows; DOWN from
// Ladder Match goes to it by the page's own links). Its press goes to the row of F2's LUMBERJACK MATCH > MATCH: Single
// Match, Triple Threat or Tag Match (played tornado: lumberjack.c), and the select, the card and the match are that type's.
// (ljround8, 2026-11-23, the user after playing: "How do we select triple threat? Tornado Tag? How do I select lumberjacks?
//  In the menu it looks odd, text is too small ... its ok if Lumberjack match goes on the previous screen (with Ladder match
//  in regular mode). In arcade it would just stay in Versus.") REGULAR: the row is on Multi Play's page now, under Ironman
//  Match, as Ladder Match is there (multiplay_lj_defs / lj_regular_frame / lj_regular_pad below); the Exhibition page has
//  its five rows as the game made them. ARCADE: as before, the seventh row of Versus's MATCH page. Both: the row's picture
//  is "Lumberjack Match" in the labels' own lettering (gen/labeltex.py: letters cut from the ROM's labels), not the text
//  maker's condensed letters ("too small", "odd").
#define EXH_LJ_ROW 6u
#define LABEL_LJ   0x852E
static int lj_lent, lj_row_shown, lj_cur_was = -1, lj_lr;
static u32 dbg_lj_nav;                     // (QA, qa_reg 16384: where LEFT, RIGHT and DOWN left the cursor, 4 bits each)
static u16 lj_lend_was[2], lj_row_label, lj_row_flags;
static u32 lj_row_at;
// ---- Regular: Multi Play's Lumberjack Match row and its MATCH page (ljround8) ------------------------------------------
// The row: Multi Play's page (screen 2) is built from the page table (0x8014DFC8: { defs, u8 count } a screen) -- as the
// NUMBER page's and the championship list's rows are added (number_page_defs, champ_versus_defs), its definitions are
// copied into the mod's memory with one more after them: the bottom row's (Ironman Match), as far below it as it is below
// the row above it, its label 0x852E lent while the page is up, its action Exhibition's (0x0402: the Exhibition MATCH page
// comes up, the game's own way, and the column beside the list shows it). DOWN from Ironman Match goes to it, and from it
// round to the top row, as Ironman Match's did; UP the other way. Done while ovl_c's data is as the ROM has it.
// Its MATCH page, the Lumberjack MATCH page (lj_menu: the row chosen): the game's MATCH page (screen 7) built as the game
// builds each of its MATCH pages -- Exhibition's, Ladder Match's, Guest Referee's, Pay Per View's -- from a list of its
// own. The page's set-up (init_exhibition_page, func_8012D720, when the screen comes up) takes the row list of the menus'
// context (Exhibition's: context 0, 0x8014FE08 -- s8 row kinds, -1 ends), gives each row in turn its kind's label and
// help (0x8014FDB0: { u16 label, u16 help } a kind) and hides the rest. While the Lumberjack match is asked for, that list
// is the Lumberjack page's (lj_page_data): Single Match (one on one), Tag Match (a tornado tag: all four in the ring),
// Triple Threat Match, then the match's two settings -- LUMBERJACKS 2..30 and PICKED Random / Select, from F2's (the
// defaults; F2 keeps its own), LEFT / RIGHT (or A) to change them -- as the kinds whose labels are 0x8525 / 0x8526 (Ladder
// Match's two ladder types: a page never up with this one), those two labels' pictures ours while it is asked for: the
// settings as they are, in the labels' own lettering (label_tex.h label_lj_count / label_lj_pick; storylist_file). So
// the page comes up with its own rows from its first picture -- nothing of Exhibition's is drawn and then written over
// (ljmenu10, 2026-11-24, the user: "The options ... briefly flash to show 'Cage' 'handicap' ... Make sure you are not
// overlaying. It should be a proper menu"). The list and the pictures are given back the moment the request ends (A on
// another row of Multi Play, or another main page), before Exhibition's page can be built. A on a type: that match, as
// the game's own row (PLAYER, ARENA, RULES, the Superstar Select, the card), with the lumberjacks asked for
// (lumberjack_menu_set + arm). B goes back to Multi Play, the game's own way.
#define MP_DEFS_MAX 16
static u8 mp_defs[MP_DEFS_MAX * 0x16] __attribute__((aligned(4)));
static int mp_lj_row = -1;                 // the row (its index on Multi Play's page), -1 none
static u16 mp_lj_variant = 9;              // (the bottom row's label quads)
static int lj_menu;                        // the Lumberjack MATCH page is up / on its way (the row chosen; declared above too)
static u32 dbg_lj_reg;                     // (QA: the Regular flow -- rr_kept_out with qa_reg 16384)
void lumberjack_menu_set(int count, int pick, int variant);
extern int lj_count, lj_pick;
static void multiplay_lj_defs(void) {
    if (nm_arcade() || !title_loaded()) return;
    u32 e = PAGE_ENTRY(SCR_MULTIPLAY), defs = RD32(e);
    int n = RD8(e + 4);
    if (defs == (u32)mp_defs) return;                              // (done: until the overlay comes back from the ROM)
    mp_lj_row = -1;
    if (!GAME_PTR(defs) || n < 3 || n >= MP_DEFS_MAX) return;
    // the list's bottom row and the one above it: the lowest two of its picture rows (label 0x85xx) at the bottom one's x
    int b = -1, u = -1;
    for (int i = 0; i < n; i++) {
        u32 d = defs + 0x16u * (u32)i;
        if ((RD16(d + 0xA) & 0xFF00u) != 0x8500u) continue;
        if (b < 0 || RDS16(d + 2) > RDS16(defs + 0x16u * (u32)b + 2)) b = i;
    }
    if (b < 0) return;
    u32 bd = defs + 0x16u * (u32)b;
    for (int i = 0; i < n; i++) {
        u32 d = defs + 0x16u * (u32)i;
        if (i == b || (RD16(d + 0xA) & 0xFF00u) != 0x8500u || RDS16(d + 0) != RDS16(bd + 0) || RDS16(d + 2) >= RDS16(bd + 2)) continue;
        if (u < 0 || RDS16(d + 2) > RDS16(defs + 0x16u * (u32)u + 2)) u = i;
    }
    if (u < 0) return;
    for (u32 k = 0; k < 0x16u * (u32)n; k++) mp_defs[k] = RD8(defs + k);
    u32 m = (u32)mp_defs, nd = m + 0x16u * (u32)n, mb = m + 0x16u * (u32)b;
    for (u32 k = 0; k < 0x16u; k++) RD8(nd + k) = RD8(mb + k);
    RDS16(nd + 2) = (s16)(RDS16(mb + 2) + (RDS16(mb + 2) - RDS16(m + 0x16u * (u32)u + 2)));
    u8 wrap = RD8(mb + 7);
    RD8(nd + 6) = (u8)b; RD8(nd + 7) = wrap;                        // (up: Ironman Match; down: round, as his did)
    if (RD8(mb + 8) == (u8)b) RD8(nd + 8) = (u8)n;                  // (left / right: itself, as his own pointed at himself)
    if (RD8(mb + 9) == (u8)b) RD8(nd + 9) = (u8)n;
    RD16(nd + 0xA) = LABEL_LJ;
    RD16(nd + 0x12) = ACT_EXHIBITION;
    RD8(mb + 7) = (u8)n;                                            // (Ironman Match: down to it)
    for (int i = 0; i < n; i++) if (i != b && RD8(m + 0x16u * (u32)i + 6) == (u8)b && (u8)i == wrap) RD8(m + 0x16u * (u32)i + 6) = (u8)n;   // (the top row: up to it)
    mp_lj_variant = 9;                                              // (the 128-px quads, as the MATCH pages' 128-px rows; r6 took
                                                                    //  Ironman Match's entry's and the picture never showed)
    RD32(e) = m; RD8(e + 4) = (u8)(n + 1);
    mp_lj_row = n;
}
// The page's own rows (ljmenu10). Their labels: Single / Tag / Triple Threat Match the game's own; the two settings rows
// 0x8525 / 0x8526, the kinds 9 / 10 of Ladder Match's MATCH page ("Ladder: Single" / "Ladder: Tag", menu context 3 -- a
// page never up with this one: A on its row of Multi Play ends the request first), their pictures the settings' while the
// request lasts. (Every kind's help is 0: a row of the page does what its label says, set_match_type_from_row.)
#define LJ_OPT_LABEL0   0x8525
#define EXH_KIND_TABLE  0x8014FDB0u        // { u16 label, u16 help } a row kind: what init_exhibition_page gives a row of it
#define EXH_KINDS_N     22                 // (0x851C .. 0x8531; the first list starts after them)
#define EXH_ROWS_CTX0   0x8014FE08u        // Exhibition's row list (menu context 0): s8 kinds, -1 ends -- 8 bytes, the next list's after
#define LJ_PAGE_ROWS    5
static const u16 lj_page_label[LJ_PAGE_ROWS] = { 0x851C, 0x851D, 0x851E, LJ_OPT_LABEL0, LJ_OPT_LABEL0 + 1 };
static const u16 exh_page_label[LJ_PAGE_ROWS] = { 0x851C, 0x851D, 0x851E, 0x851F, 0x8520 };   // (Exhibition's, as the ROM has it)
static s8 lj_rows_was[8];                  // Exhibition's list, as it was before ours (lj_rows_saved: kept)
static u16 lj_opt_lend_was[2][2];          // the two labels' entries, as they were before ours (lj_opt_saved: kept)
static int lj_rows_saved, lj_opt_saved;
static void lj_opt_show(int k);
static u32 dbg_lj_flash;                   // (QA: frames of the page, asked for, with a row on it that is not the page's)
static int exh_kind_of(u16 label) {
    for (int k = 0; k < EXH_KINDS_N; k++) if (RD16(EXH_KIND_TABLE + 4u * (u32)k) == label) return k;
    return -1;
}
// Exhibition's row list names these five labels in order, and ends there
static int exh_rows_are(const u16* labels) {
    for (int i = 0; i < LJ_PAGE_ROWS; i++) {
        int k = RDS8(EXH_ROWS_CTX0 + (u32)i);
        if (k < 0 || k >= EXH_KINDS_N || RD16(EXH_KIND_TABLE + 4u * (u32)k) != labels[i]) return 0;
    }
    return RDS8(EXH_ROWS_CTX0 + LJ_PAGE_ROWS) < 0;
}
// The page's data while the Lumberjack match is asked for (on), or Exhibition's again (off): the row list the page is
// built from, and the settings rows' pictures. Read from the data itself each time (an overlay loaded again has the ROM's,
// and ours is put back before any page is built: arcade_overlay_loaded). Regular only; only ovl_c's data, as the ROM has it.
static void lj_page_data(int on) {
    if (nm_arcade() || !title_loaded()) return;
    if (on && exh_rows_are(exh_page_label)) {
        int kind[LJ_PAGE_ROWS];
        for (int i = 0; i < LJ_PAGE_ROWS; i++) if ((kind[i] = exh_kind_of(lj_page_label[i])) < 0) return;
        for (u32 i = 0; i < 8; i++) lj_rows_was[i] = RDS8(EXH_ROWS_CTX0 + i);
        lj_rows_saved = 1;
        for (int i = 0; i < LJ_PAGE_ROWS; i++) RDS8(EXH_ROWS_CTX0 + (u32)i) = (s8)kind[i];
        RDS8(EXH_ROWS_CTX0 + LJ_PAGE_ROWS) = -1;
    } else if (!on && exh_rows_are(lj_page_label) && lj_rows_saved) {
        for (u32 i = 0; i < 8; i++) RDS8(EXH_ROWS_CTX0 + i) = lj_rows_was[i];
    }
    int lent = RD16(LABEL_ENTRY(LJ_OPT_LABEL0)) == (u16)FILE_LJ_OPT0;
    if (on && !lent) {
        for (int k = 0; k < 2; k++) { lj_opt_lend_was[k][0] = RD16(LABEL_ENTRY(LJ_OPT_LABEL0 + k)); lj_opt_lend_was[k][1] = RD16(LABEL_ENTRY(LJ_OPT_LABEL0 + k) + 2); }
        lj_opt_saved = 1;
        for (int k = 0; k < 2; k++) {
            RD16(LABEL_ENTRY(LJ_OPT_LABEL0 + k)) = (u16)(FILE_LJ_OPT0 + (u32)k); RD16(LABEL_ENTRY(LJ_OPT_LABEL0 + k) + 2) = 9;   // (the 128-px quads, as Single Match's)
            lj_opt_show(k);                                                                     // (the settings as they are now)
        }
    } else if (!on && lent && lj_opt_saved)
        for (int k = 0; k < 2; k++) { RD16(LABEL_ENTRY(LJ_OPT_LABEL0 + k)) = lj_opt_lend_was[k][0]; RD16(LABEL_ENTRY(LJ_OPT_LABEL0 + k) + 2) = lj_opt_lend_was[k][1]; }
}
// (ljmenu10) a settings row's picture as the setting is now, put straight into the copy the game draws from: the menus' sprite
// cache (0x80104D30, 128 entries of { file, picture, keep count, flags -- bit 0: still to be read }). Letting the sprite go
// instead (GF_SPRITE_RELEASE) had the game read the file again a frame later -- one frame of the row's glow with no picture
// at each change (frame-by-frame shots, run 2026-11-24_145835, 169.133 s). Not in the cache (or not read yet): the game
// reads the file as it is now. Kept but not a 128 x 32 picture as served: let go, as before.
#define SPRITE_CACHE 0x80104D30u
static void lj_opt_show(int k) {
    u32 file = FILE_LJ_OPT0 + (u32)k;
    for (u32 e = SPRITE_CACHE; e < SPRITE_CACHE + 0x80u * 0x10u; e += 0x10u) {
        if (RD32(e) != file) continue;
        u32 pic = RD32(e + 4);
        if (RD32(e + 0xC) & 1u) return;
        if (GAME_PTR(pic) && RD32(pic) == 0x54455800u && RD16(pic + 4) == 128 && RD16(pic + 6) == 32) storylist_file(file, pic);
        else plat_game_call(GF_SPRITE_RELEASE, file, 0, 0);
        return;
    }
}
// the Lumberjack MATCH page is up: screen 7, asked for, built with the page's rows
static int lj_page_up(void) {
    if (!in_menus() || RDS16(NM_SCREEN) != SCR_EXHIBITION || !lj_menu) return 0;
    u32 items = RD32(menu_record(SCR_EXHIBITION) + 0x14);
    if (!GAME_PTR(items)) return 0;
    for (int i = 0; i < LJ_PAGE_ROWS; i++) if (RD16(items + (u32)i * 0x88u + 0x60) != lj_page_label[i]) return 0;
    return RD8(items + 4u * 0x88u + 0x4B) == 0;
}
static int lj_mp_away;                     // (Regular: Multi Play's Pay Per View / King of the Ring chosen -- 0x852E is theirs till Multi Play)
// every frame (Regular): the row's label lent on Multi Play's page; the Lumberjack MATCH page's rows while it is up
static void lj_regular_frame(void) {
    int scr = in_menus() ? RDS16(NM_SCREEN) : -1;
    int mp_up = scr == SCR_MULTIPLAY && mp_lj_row >= 0 && RD32(PAGE_ENTRY(SCR_MULTIPLAY)) == (u32)mp_defs;
    int page = lj_page_up();
    // (r7-r9: lent only while Multi Play's page was up, the row showed no picture at all -- the main pages are built (and
    //  their pictures taken) before the screen says so. Now 0x852E is ours in Regular all the time the menus are not on the
    //  way to, or on, a Pay Per View or King of the Ring MATCH page -- the pages whose match type it is: from the press on
    //  those rows of Multi Play (lj_regular_pad) till Multi Play's page is up again)
    if (mp_up && !menus_loading()) lj_mp_away = 0;
    int lend = mp_lj_row >= 0 && RD32(PAGE_ENTRY(SCR_MULTIPLAY)) == (u32)mp_defs && in_menus() && !lj_mp_away &&
               (scr != SCR_EXHIBITION || page || exh_is_exhibition());
    if (lend || page) {
        if (!lj_lent) {
            lj_lend_was[0] = RD16(LABEL_ENTRY(LABEL_LJ)); lj_lend_was[1] = RD16(LABEL_ENTRY(LABEL_LJ) + 2); lj_lent = 1;
            plat_game_call(GF_SPRITE_RELEASE, FILE_LJ_LABEL, 0, 0);   // (as rounds 1-7 did after making the picture: the
        }                                                             //  sprite cache lets go of whatever it took for the file)
        RD16(LABEL_ENTRY(LABEL_LJ)) = (u16)FILE_LJ_LABEL; RD16(LABEL_ENTRY(LABEL_LJ) + 2) = mp_lj_variant;
    } else if (lj_lent) {
        if (title_loaded()) { RD16(LABEL_ENTRY(LABEL_LJ)) = lj_lend_was[0]; RD16(LABEL_ENTRY(LABEL_LJ) + 2) = lj_lend_was[1]; }
        lj_lent = 0;
    }
    if (mp_up) {                                                    // (the row's own picture, on the built row)
        u32 mitems = RD32(menu_record(SCR_MULTIPLAY) + 0x14);
        if (GAME_PTR(mitems) && RD32(mitems + (u32)mp_lj_row * 0x88u + 0x64) == (u32)mp_defs + 0x16u * (u32)mp_lj_row) {
            u32 row = mitems + (u32)mp_lj_row * 0x88u, up = RD32(row + 0x64) ? mitems + (u32)RD8((u32)mp_defs + 0x16u * (u32)mp_lj_row + 6) * 0x88u : 0;
            RD16(row + 0x60) = LABEL_LJ;
            // (r8: the row came up with no picture at all -- its fade (+0x56) stayed 0: the page fades in the rows it was
            //  built with by their own count. It follows the row above's fade, and has the game's own light, as Arcade's
            //  Royal Rumble row is unhidden: champ_rumble_slot)
            if (up && RD8(row + 0x56) < RD8(up + 0x56)) RD8(row + 0x56) = RD8(up + 0x56);
            if (!RD8(row + 0x57)) RD8(row + 0x57) = 0x80;
            if (RD16(row + 0x4A) & 0xFu) RD16(row + 0x4A) &= 0xFFF0u;
        }
    }
    if (mp_up && !menus_loading() && RD8(menu_record(SCR_MULTIPLAY) + 5) != (u8)mp_lj_row) lj_menu = 0;   // (another row: no request)
    if (scr == SCR_SINGLEPLAY || scr == SCR_COMMISSIONER || scr == SCR_TITLE || scr == SCR_CHAMPIONSHIP) lj_menu = 0;   // (another main page)
    if (in_menus() && (scr == SCR_MULTIPLAY || scr == SCR_TITLE || scr == SCR_CHAMPIONSHIP) && lumberjack_armed()) lumberjack_arm(0);
    lj_page_data(lj_menu);               // (the page's own rows while asked for -- set at the press already: lj_regular_pad)
    // (QA: a frame of screen 7 with a row shown that is not its page's, counted -- the page asked for and a row on it not one
    //  of its five (Exhibition's Handicap or Cage, say), or Exhibition's page (context 0) not asked for with a settings row's
    //  label on it, or any page not asked for drawing those labels with our pictures still lent -- and the settings rows'
    //  pictures there, made from the player's ROM by the first start)
    u32 items = scr == SCR_EXHIBITION ? RD32(menu_record(SCR_EXHIBITION) + 0x14) : 0;
    if (GAME_PTR(items))
        for (int i = 0; i < 17; i++) {
            u32 row = items + (u32)i * 0x88u;
            u16 lb = RD16(row + 0x60);
            if ((RD16(row + 0x4A) & 0xFu) || !RD8(row + 0x56)) continue;              // (hidden, or not faded in at all)
            int ours = lb == LJ_OPT_LABEL0 || lb == LJ_OPT_LABEL0 + 1;
            if (lj_menu ? (i >= LJ_PAGE_ROWS || lb != lj_page_label[i])
                        : ours && (RD8(0x800A9578u) == 0 || RD16(LABEL_ENTRY(LJ_OPT_LABEL0)) == (u16)FILE_LJ_OPT0)) { dbg_lj_flash++; break; }
        }
    {   static int said; extern void removal_s(const char*); extern void removal_num(s32); extern void removal_say(void);
        if (page && !said) {
            said = 1;
            removal_s("lumberjack: menus -- the row's picture read "); removal_num((s32)dbg_lj_label_reads); removal_s(" times, its size asked "); removal_num((s32)dbg_lj_label_sizes);
            removal_s("; the MATCH page built with its own rows (context "); removal_num((s32)RD8(0x800A9578u));   /* (NM_SEL_CTX) */ removal_s(", settings rows' help ");
            removal_num((s32)RD16(items + 3u * 0x88u + 0x5A)); removal_s(" / "); removal_num((s32)RD16(items + 4u * 0x88u + 0x5A)); removal_s(")"); removal_say();
        }
    }
    {   static u32 flash_said; extern void removal_s(const char*); extern void removal_num(s32); extern void removal_say(void);
        if (dbg_lj_flash != flash_said) { flash_said = dbg_lj_flash; removal_s("lumberjack: menus -- a frame of the Lumberjack MATCH page with a row not its own (screen 7, frames so far "); removal_num((s32)dbg_lj_flash); removal_s(")"); removal_say(); }
    }
    int pics = 0;                        // (the settings rows' pictures not blank: the first start made them)
    for (u32 k = 48; k < 2096 && !pics; k++) pics = label_lj_count[k] != 0;
    // (bits 0-14 reach the QA state, rr_kept_out: 6 a frame with a row not its page's ever seen, 7 the settings rows' pictures made)
    dbg_lj_reg = (u32)(mp_lj_row & 0xF) | (u32)(lj_menu & 1) << 4 | (u32)page << 5 | (u32)(dbg_lj_flash != 0) << 6 | (u32)pics << 7 |
                 (u32)(lj_mc & 0x3F) << 8 | (u32)(lj_mp & 1) << 14 |
                 (u32)(scr == SCR_MULTIPLAY ? RD8(menu_record(SCR_MULTIPLAY) + 5) & 0xF : 0xF) << 16 |
                 (u32)(page ? RD8(menu_record(SCR_EXHIBITION) + 5) & 0xF : 0xF) << 20 | (u32)(lumberjack_armed() & 1) << 24 |
                 (u32)(RD16(LABEL_ENTRY(LABEL_LJ)) == (u16)FILE_LJ_LABEL) << 25;
}
static u8 lj_stick_was;
static u16 reg_held[4];
// Regular's controllers (regular_pads): A on Multi Play's row; the Lumberjack MATCH page's LEFT / RIGHT and A
static void lj_regular_pad(u16* ob, u16* oe, u8* ox, u16 any) {
    int scr = RDS16(NM_SCREEN);
    if (scr == SCR_MULTIPLAY && mp_lj_row >= 0 && (any & (BTN_A | BTN_START))) {
        int on = RD8(menu_record(SCR_MULTIPLAY) + 5) == (u8)mp_lj_row;
        {   // (Pay Per View, King of the Ring: their MATCH pages' match types use 0x852E -- given back before they are built)
            u32 d = RD32(RD32(menu_record(SCR_MULTIPLAY) + 0x14) + (u32)RD8(menu_record(SCR_MULTIPLAY) + 5) * 0x88u + 0x64);
            u16 lb = GAME_PTR(d) || d == (u32)mp_defs + 0x16u * (u32)mp_lj_row ? RD16(d + 0xA) : 0;
            if (lb == 0x8503 || lb == 0x8504) {
                lj_mp_away = 1;
                if (lj_lent && title_loaded()) { RD16(LABEL_ENTRY(LABEL_LJ)) = lj_lend_was[0]; RD16(LABEL_ENTRY(LABEL_LJ) + 2) = lj_lend_was[1]; lj_lent = 0; }
            }
        }
        if (on && !lj_menu) { lj_mc = lj_count < 2 ? 2 : lj_count > NM_SLOTS_CAP - 2 ? NM_SLOTS_CAP - 2 : lj_count; lj_mp = lj_pick & 1; }
        lj_menu = on;
        lj_page_data(on);                // (ljmenu10: before the game sees the press -- the page it opens is built from it)
        if (on) for (int k = 0; k < 2; k++) lj_opt_show(k);   // (the settings as they are now)
        return;
    }
    if (!lj_page_up()) return;
    u32 rec = menu_record(SCR_EXHIBITION);
    int cur = RD8(rec + 5);
    // LEFT / RIGHT: the page's own left / right links are the PPV grid's (they lead off the three types) -- taken here: on a
    // settings row they change it, elsewhere nothing
    int dir = 0, stick = 0;
    for (int i = 0; i < 4; i++) {
        if (oe[i] & BTN_DL) dir = -1;
        if (oe[i] & BTN_DR) dir = 1;
        s8 sx = (s8)ox[i];
        if (sx < -40) stick = -1; else if (sx > 40) stick = 1;
        ob[i] &= (u16)~(BTN_DL | BTN_DR); oe[i] &= (u16)~(BTN_DL | BTN_DR); ox[i] = 0;
    }
    if (stick && !lj_stick_was) dir = stick;
    lj_stick_was = (u8)(stick != 0);
    RD16(NM_PRESSED) &= (u16)~(BTN_DL | BTN_DR);
    int ab = (any & (BTN_A | BTN_START)) != 0;
    if (cur == 3 || cur == 4) {
        if (ab) {                                                   // (A on a setting: the next value; the game never sees it)
            for (int i = 0; i < 4; i++) { reg_held[i] |= (u16)(oe[i] & (BTN_A | BTN_START)); ob[i] &= (u16)~(BTN_A | BTN_START); }
            RD16(NM_PRESSED) &= (u16)~(BTN_A | BTN_START);
            if (!dir) dir = 1;
        }
        // (a new value: its picture, in the copy the game draws from: lj_opt_show)
        if (dir && cur == 3) { lj_mc += dir; if (lj_mc < 2) lj_mc = NM_SLOTS_CAP - 2; if (lj_mc > NM_SLOTS_CAP - 2) lj_mc = 2; lj_opt_show(0); }
        if (dir && cur == 4) { lj_mp ^= 1; lj_opt_show(1); }
        return;
    }
    if (ab && cur <= 2) {                // (a type: Single 0 -> one on one, Tag 1 -> tornado tag, Triple Threat 2 -> triple threat)
        static const s8 variant_of_row[3] = { 0, 2, 1 };
        lumberjack_arm(1);
        lumberjack_menu_set(lj_mc, lj_mp, variant_of_row[cur]);
    }
}
// ---- Arcade: the seventh row of Versus's MATCH page (lumberjack2) ---------------------------------------------------------
static void exh_lumberjack_slot(void) {
    {   // (lumberjack2: picked by the player -- the Superstar Select takes the competitors, then N more picks: the lumberjacks)
        int want = lumberjack_select_picks();
        if (want && in_menus() && is_select(RDS16(NM_SCREEN)) && !menus_loading() && RDS16(0x800A9582u) == lumberjack_competitors())
            RDS16(0x800A9582u) = (s16)want;                       // (NM_SEL_PICKS: the picks the select asks for)
    }
    if (!nm_arcade()) { lj_regular_frame(); return; }
    int up = in_menus() && RDS16(NM_SCREEN) == SCR_EXHIBITION && exh_is_exhibition();
    if (up && !lj_lent) {
        lj_lend_was[0] = RD16(LABEL_ENTRY(LABEL_LJ)); lj_lend_was[1] = RD16(LABEL_ENTRY(LABEL_LJ) + 2); lj_lent = 1;
        plat_game_call(GF_SPRITE_RELEASE, FILE_LJ_LABEL, 0, 0);       // (ljround8: as rounds 1-7 did after making the picture)
    }
    if (up) { RD16(LABEL_ENTRY(LABEL_LJ)) = (u16)FILE_LJ_LABEL; RD16(LABEL_ENTRY(LABEL_LJ) + 2) = 9; }   // (the 128-px quads)
    else if (lj_lent) {
        if (title_loaded()) { RD16(LABEL_ENTRY(LABEL_LJ)) = lj_lend_was[0]; RD16(LABEL_ENTRY(LABEL_LJ) + 2) = lj_lend_was[1]; }
        lj_lent = 0;
    }
    u32 items = in_menus() ? RD32(menu_record(SCR_EXHIBITION) + 0x14) : 0;
    if (!up) {
        if (GAME_PTR(items) && lj_row_at == items + EXH_LJ_ROW * 0x88u && RD16(lj_row_at + 0x60) == LABEL_LJ) {
            RD16(lj_row_at + 0x60) = lj_row_label; RD16(lj_row_at + 0x4A) = lj_row_flags;   // (the row as the page had it)
        }
        lj_row_shown = 0; lj_cur_was = -1;
        if (in_menus() && (RDS16(NM_SCREEN) == SCR_MULTIPLAY || RDS16(NM_SCREEN) == SCR_TITLE || RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP) && lumberjack_armed()) lumberjack_arm(0);
        return;
    }
    if (!GAME_PTR(items)) return;
    u32 row = items + EXH_LJ_ROW * 0x88u, r4 = items + 4u * 0x88u, r3 = items + 3u * 0x88u;
    if (RD16(row + 0x60) != LABEL_LJ) { lj_row_at = row; lj_row_label = RD16(row + 0x60); lj_row_flags = RD16(row + 0x4A); }
    RD16(row + 0x60) = LABEL_LJ;
    union { float f; u32 u; } x4, y4, y3, y5;                    // (under Ladder Match, itself under Cage: x, y floats at +0x40, +0x44)
    x4.u = RD32(r4 + 0x40); y4.u = RD32(r4 + 0x44); y3.u = RD32(r3 + 0x44);
    y5.f = y4.f + (y4.f - y3.f) * 2.0f;
    RD32(row + 0x40) = x4.u; RD32(row + 0x44) = y5.u;
    if (RD16(row + 0x4A) & 0xF) RD16(row + 0x4A) &= 0xFFF0u;    // (the game's own unhide: it fades in with the rest)
    lj_row_shown = 1;
}
// The page's controllers (Regular, regular_pads): the row's cursor chain and its choosing (above)
static void exh_lumberjack_pad(const u16* raw, u16 any) {
    u32 rec = menu_record(SCR_EXHIBITION), items = RD32(rec + 0x14);
    if (!GAME_PTR(items)) return;
    int cur = RD8(rec + 5);
    if (lj_lr && lj_cur_was >= 0 && lj_cur_was != EXH_LJ_ROW && cur == EXH_LJ_ROW) {
        // LEFT / RIGHT landed on the row: on past it, along its own links, to the next row shown (the page without it)
        u32 d = RD32(items + EXH_LJ_ROW * 0x88u + 0x64);
        int to = lj_cur_was;
        for (int t = 0; t < 16 && GAME_PTR(d); t++) {
            int next = RD8(d + (lj_lr == 1 ? 8u : 9u));
            if (next < 0 || next >= 16) break;
            if (next != EXH_LJ_ROW && RD8(items + (u32)next * 0x88u + 0x4B) == 0) { to = next; break; }
            d = RD32(items + (u32)next * 0x88u + 0x64);
        }
        RD8(rec + 5) = (u8)to;
    } else if (lj_lr && lj_cur_was == EXH_LJ_ROW && cur != EXH_LJ_ROW) RD8(rec + 5) = EXH_LJ_ROW;   // (from it: it stays)
    cur = RD8(rec + 5);
    int lr = 0;
    for (int i = 0; i < 4; i++) {
        s8 sx = RDS8(NM_PADS + 6u * (u32)i + 2u);
        if ((raw[i] & BTN_DL) || sx < -40) lr = 1;
        if ((raw[i] & BTN_DR) || sx > 40) lr = 2;
    }
    lj_lr = lr; lj_cur_was = cur;
    if (any & (BTN_A | BTN_START)) {
        if (cur == (int)EXH_LJ_ROW) {   // (the match's own row takes the press: Single 0, Tag 1, Triple Threat 2)
            static const u8 type_row[3] = { 0, 2, 1 };
            int to = type_row[lumberjack_variant()];
            RD8(rec + 5) = (u8)to; lumberjack_arm(1); lj_cur_was = to;
        }
        else lumberjack_arm(0);
    }
}

enum { RR_OFF, RR_SELECT, RR_START, RR_LIVE };
static int rr_mode;                      // where the arcade's Royal Rumble is
static int rr_open_pending;              // the player chose the Rumble row (from the pad hook)
static void rr_open(int rebuild);

// ---- challenges: the winner takes over single player ------------------------------------------------
// Like a fighting game: a challenger stops single player (Story Mode or the Royal Rumble) for a
// Versus match; whoever wins it carries on in single player, where it was, as the Superstar they
// won with; the loser is out (a coin and Start challenges again). If neither player loses the
// match (a team win, a time-out) the player who was challenged keeps their place.
enum { SP_NONE, SP_STORY, SP_RUMBLE, SP_SURVIVAL };
#define NM_SLOT_WRESTLER SIXA_800A4038    // s32[4]: the wrestler in each match slot
static int sp_kind;            // what single player is in the middle of (from its Superstar Select)
static int sp_belt;            // its championship (Story Mode)
static u16 sp_pick;            // the Superstar single player plays as
static u8 sp_costume;          // (and the costume, when the Rumble's select picked it)
static int challenge_on, challenge_played, challenger = -1;
static int challenge_old;       // the match (or its entrances) on when the challenger came: not the challenge match
static int resume_pending;     // the winner goes back to where single player was
static int pick_wait, pick_steps;

// The Superstar Select's pages (roster_pages_n x { 9 x (u16 ID, u16 ordinal), u16 count at +0x24, name }; R goes
// to the next one shown, L to the one before): roster.c's, since the WCW page (2026-10-30) -- not the game's
// 0x800A9990; how many, the game's thirteen or select_pages.json's
#define NM_SEL_PAGES  ((u32)roster_pages_tab)
#define SEL_PAGE_SIZE 0x38u
#define SEL_PAGES_N   roster_pages_n
static int sel_find(u16 id, int* page, int* cell) {
    if (id == 0) return 0;
    for (int p = 0; p < SEL_PAGES_N; p++) {
        u32 pg = NM_SEL_PAGES + SEL_PAGE_SIZE * (u32)p;
        u32 n = RD16(pg + 0x24);
        if (n > 9) n = 9;
        for (u32 c = 0; c < n; c++)
            if (RD16(pg + 4u * c) == id) { *page = p; *cell = (int)c; return 1; }
    }
    return 0;
}
// The Superstar under the select's cursor: its box's row (+0x62; variants.c makes it a variant's), else the game's hover
#define NM_SEL_COSTUME 0x800A9588u    // s16: the costume under the cursor (Type - 1)
static u16 sel_hover(void) {
    s16 scr = RDS16(NM_SCREEN);
    u32 rec = menu_record(scr), items = RD32(rec + 0x14);
    s32 cur = RDS16(rec + 4);
    if (is_select(scr) && GAME_PTR(items) && cur >= 0 && cur < 9) {
        u16 id = RD16(items + 0x88u * (u32)cur + 0x62u);
        if (id && id < 0x1000) return id;
    }
    return RD16(NM_SELECT_HOVER);
}
// Moves the select's cursor to a Superstar, a press at a time, and picks them -- a costume variant (variants.c) in his
// base's box, the Type arrows (C-right) to him; want_c >= 0: that costume too. pl: whose controller (-1 the game's
// controller 1, inject_press; 0 / 1 player 1's / 2's, qa_pad / qa_pad2). 1: picked; 0: on the way; -1: not there (the
// player picks).
static u32 dbg_pick_presses;   // (QA qa_variant: state.csv "forced" = the last press | how many << 16 | the cursor << 24)
static void pick_press(int pl, u16 b) { dbg_pick_presses = (dbg_pick_presses & 0xFF0000u) + 0x10000u + b; if (pl < 0) inject_press(b); else if (pl == 0) qa_pad(b); else qa_pad2(b); }
int roster_staging(void);   // (roster.c: more folder wrestlers than seats)
static int auto_pick_c(u16 want, int want_c, int pl) {
    if (inject_frames > 0 || qa_pad_frames[0] > 0 || qa_pad_frames[1] > 0) return 0;
    if (pick_wait > 0) { pick_wait--; return 0; }
    u16 h = sel_hover();
    // (2026-11-14, families: the box is the one holding that variant -- a partial family's member has a box of his own for
    //  the variants it doesn't list; the cursor's box is the page's entry for it)
    u16 hb = variant_base_of(h);
    u16 box = variant_box_for(want, want_c), hbox = variant_cursor_box();
    if (!hbox) hbox = hb ? hb : h;
    int tp, tc, cp, cc;
    // (2026-11-13, seatsroot: with the select's pages a window on more of them -- roster.c staging -- a box on a page not in
    //  the window now is reached the way a player reaches it, R page after page: the window follows; as many steps as pages)
    {   if (roster_staging() && !sel_find(box, &tp, &tc) && sel_find(hbox, &cp, &cc) && ++pick_steps <= 600) { pick_press(pl, BTN_R); pick_wait = 8; return 0; } }
    if (!sel_find(box, &tp, &tc) || !sel_find(hbox, &cp, &cc) || ++pick_steps > (roster_staging() ? 600 : 80)) return -1;
    if (hbox == box) {
        // (QA: a tour first -- C-right round the whole family and once more, then C-left, C-right; a shot each)
        if (qv_tour > 0) { pick_press(pl, qv_tour == 2 ? BTN_CL : BTN_CR); qv_tour--; pick_steps--; pick_wait = 75; return 0; }
        if (h == want && (want_c < 0 || RDS16(NM_SEL_COSTUME) == want_c)) { pick_press(pl, BTN_A); pick_steps = 0; pick_wait = 20; return 1; }
        pick_press(pl, BTN_CR);                           // (the Type arrows: on to him / his costume)
        pick_wait = qa_variant ? 75 : 20;
        return 0;
    }
    u16 b;
    if (cp != tp) b = BTN_R;                              // (round the pages shown, to its page)
    else if (cc / 3 != tc / 3) b = (tc / 3 > cc / 3) ? BTN_DD : BTN_DU;
    else b = (tc % 3 > cc % 3) ? BTN_DR : BTN_DL;
    pick_press(pl, b);
    pick_wait = 8;
    return 0;
}
static int auto_pick(u16 want) { return auto_pick_c(want, -1, -1); }

static u16 slot_wrestler_of(int pl) {
    for (int s = 0; s < 4; s++) {
        if (slot_owner[s] != pl) continue;
        u32 id = RD32(NM_SLOT_WRESTLER + 4u * (u32)s) & 0xFFFFu;
        if (id != 0 && id < 0x1000) return (u16)id;
    }
    return 0;
}

static void survival_open(void) {
    sp_kind = SP_SURVIVAL; sv_setup = 1;
    menu_choose(SCR_SINGLEPLAY, SINGLEPLAY_SURVIVAL, ACT_SURVIVAL);
}

// The challenge is decided: the loser is out, the winner carries on in single player.
static void challenge_decided(int loser) {
    int winner = loser ^ 1;
    if (winner != challenger || !active[winner]) winner = challenger ^ 1, loser = challenger;
    u16 w = slot_wrestler_of(winner);
    if (w && winner == challenger) { sp_pick = w; sp_costume = 0; }
    active[loser] = 0;
    lost_pending[loser] = 0;
    port_player[0] = winner; port_player[1] = loser;
    challenge_on = 0; challenge_played = 0; challenger = -1;
    if (sp_kind != SP_NONE) resume_pending = 1;
}

static void rumble_capture(void);
// The winner back where single player was: its championship on the list, then their Superstar at
// the select; Story Mode picks up from its chapter card (Continue). The Royal Rumble is set up again
// from where it stood (rr_open(1)), straight from the list.
static int resume_list_wait;
static void resume_single(void) {
    if (!resume_pending || phase != PLAY || game_mode != GM_STORY || goal != G_NONE || cont_player >= 0) return;
    if (!in_menus() || menus_loading() || inject_frames > 0) return;
    s16 scr = RDS16(NM_SCREEN);
    if (scr == SCR_CHAMPIONSHIP) {
        if (scr_stable < 20) { resume_list_wait = 0; return; }
        if (sp_kind == SP_RUMBLE) { resume_pending = 0; rr_open(1); return; }
        if (sp_kind == SP_SURVIVAL) { survival_open(); return; }       // (then the Superstar, below)
        RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = (u8)sp_belt;
        if (++resume_list_wait < 20) return;              // (the cursor on it a moment)
        resume_list_wait = -100;
        pick_steps = 0; pick_wait = 30;
        inject_press(BTN_A);
        return;
    }
    if (is_select(scr) && (sp_kind == SP_STORY || sp_kind == SP_SURVIVAL)) {
        if (scr_stable < 30) return;
        int r = auto_pick(sp_pick);
        if (r != 0) resume_pending = 0;                  // (picked, or the player picks)
    }
}

// ---- the single-player Royal Rumble ------------------------------------------------------------------
// What the arcade's Rumble is, in the game's own terms:
//  - the set-up: the game's Royal Rumble (action 0x404) with what its pages would answer for an
//    operator's game: ORDER Random, NUMBER the operator's size, PLAYER 1P against the CPU, ARENA the
//    Royal Rumble's, its RULES' defaults (the game's own preset, func_80134140). The Superstar
//    Select is opened in the game's one-pick context (7): one box, the player's, and right after the
//    pick the game sets the match up (func_80135728, func_80136784) and goes to it (screen 42) --
//    no versus card, so who comes out is a surprise.
//  - two at the bell: the player and the first entrant. The other two ring slots are empty the way
//    a singles match leaves them: no Superstar (-1, which the match's AI, camera, physics and
//    entrances skip), nobody on the line-up (the entrance scenes stop at the first such slot).
//  - one more every interval (the operator's, 30 s by default), into an empty slot, with the
//    game's own Rumble entrance (the stage shot, the walk down; func_8011D400 starts it, action 0xA3
//    loads the Superstar). With four in, the one called waits for the next elimination.
//  - an eliminated CPU Superstar leaves: once he lands outside, his slot is emptied the same way.
#define NM_SEL_CTX      0x800A9578u   // u8: the menus' context (1 Multi Play, 7 one pick then the match, 8 Story)
#define NM_SEL_FLAGS    0x800A957Cu   // u32: the match type's set-up bits (0x200 Royal Rumble, 0x20000 random order)
#define NM_SEL_PICKS    0x800A9582u   // s16: picks the Superstar Select asks for
#define NM_SEL_COUNT    0x800A958Cu   // s16: Royal Rumble entrants (NUMBER)
#define NM_SEL_REC      0x800A95A0u   // 12-byte picks: u16 Superstar, u8 costume, u8 human
#define NM_SEL_REC_SIZE 12u
#define NM_SEL_ARENA    0x800A946Fu   // u8: ARENA row
#define NM_RR_COUNT_SRC 0x800457ACu   // s32: entrants the match takes (the select's completion sets it)
#define CTX_ONE_PICK    7
#define SEL_FLAGS_RUMBLE 0x00020200u
#define ARENA_ROYAL_RUMBLE 6
extern int arena_count(void); extern int arena_venue(int a); extern void arena_choose(int a); extern int arena_random(u32 r);   // (arena.c)
#define NM_RR_LIST     0x800BD4D0u    // u16 per entrant: Superstar (id | costume << 8 as the select has it)
#define NM_RR_TABLE_A  0x800A9358u    // u8 per entrant: costume
#define NM_RR_TABLE_B  0x800A98E0u    // u8 per entrant
#define NM_RR_COUNT    0x80159210u    // s32: entrants in this match
#define NM_RR_NEXT     0x80159214u    // s32: the next entrant (list index)
#define NM_WRESTLER    (SIXA_80159990 + 8u)
#define WRESTLER_SIZE  0x358u
#define NM_TB          SIXA_80159990    // the wrestler structs as the game's code points at them (W - 8)
#define NM_CAM_IDS     SIXA_8016B920    // s16[4]: the Superstar each slot shows the camera (-1: none)
#define NM_MATCH_MINUTES 0x8016B930u
#define NM_MATCH_SECONDS 0x8016B934u
#define NM_MATCH_FLAGS_RUMBLE 0x4000u
#define NM_RR_RULES    0x800A9950u
#define NM_RR_FIELD3DD8 (SIXA_800A3DC0 + 0x18u)   // s32 per slot (stride 0x1C): the entrant's list index
#define NM_RR_SLOT_B   SIXA_8009ED10    // s16 per slot: the entrant's table B entry
#define RR_MAX         48
#define NM_RR_SPOTS    SIX_RR_SPOTS    // s16 x, z per slot: where func_80115E54 puts a Royal Rumble's four at the bell
#define W(s)           (NM_WRESTLER + WRESTLER_SIZE * (u32)(s))
// wrestler struct fields (W): +0x00 the slot he faces, +0x0C phase, +0x7A ring (4: outside),
// +0xF4 / +0xF8 / +0xFC position (float x, y, z), +0x104 the way he faces (0..4095),
// +0x8C bit 0 out of the match, +0x98 Superstar (-1: none), +0xD0 / +0xE0 / +0xF0 the entrant on
// the way (Superstar, costume, list index), +0xE1 bit 4 in the Rumble, +0xE9 bit 0 an entrant is
// coming to the slot.
static int rr_called;          // entrants the clock has called since the bell
static int rr_due;             // called, not sent to a slot yet
static s32 rr_last_call;       // match second of the last call (the bell: 0)
static u16 rr_model[NM_SLOTS_CAP];        // an emptied slot: the Superstar its engine model still is
static int rr_rebuild;         // the Royal Rumble being set up again after a challenge
static u16 rr_rest[RR_MAX];    // (a challenge: who's still in, then everyone still to come)
static u8 rr_rest_a[RR_MAX], rr_rest_b[RR_MAX];
static int rr_rest_n, rr_kept;  // (rr_kept: how many of them were in the ring)
static int rr_next_cap;        // (... and how many had come in when it stopped)
int rr_num_offset;             // a list place -> the entrant's number (+ 1): after a challenge, the list starts again
static int rr_time0;           // (seconds of the wait for the next entrant already run)
static int rr_player_out;      // the player has been eliminated (the Rumble goes on; a continue puts him back)
static int rr_restore_due;            // frames until he's put back (free play: no countdown)
static int rr_walk_in;                // frames the computer still has him (getting back in)
static int rr_walk_slot = -1;
static u32 rr_walk_rule;              // (the count-out rule, off while he gets back in)
int dbg_rr_interval;           // tests: seconds between entrants (0: the operator's)
// ---- the F2 Rumble settings in the game's own Royal Rumble (W10, 2026-11-07) ----------------------------------------
// The user's spec: the game's own Rumble screens open with F2's values; a change made there is for that match only and
// is never written back to F2. So each time Multi Play's Royal Rumble is chosen (the action 0x404 through the menus'
// action interpreter: arcade_menu_action_done) the match gets its own copy of F2's Rumble values (gm_*), and everything
// from there on -- the NUMBER page, the match's set-up, its clock -- reads the copy.
//   gm_size      SUPERSTARS IN IT (the game's NUMBER)        gm_ring      IN THE RING AT ONCE (rw_rumble_ring)
//   gm_interval  TIME BETWEEN ENTRANTS                        gm_start4    the Rumble starts with four, not two
// gm_on: the copy is for the Rumble being set up now (cleared when the menus are left for another match type).
// gm_live: the match running is such a Rumble, on the arcade's clock (its entrants called by TIME BETWEEN ENTRANTS).
static int gm_on, gm_live, gm_size = 15, gm_ring = 6, gm_interval = 30, gm_start4, gm_page_index = -1;
static int rr_interval(void) {
    if (dbg_rr_interval > 0) return dbg_rr_interval;
    return gm_live ? gm_interval : rumble_interval;
}
static s32 rr_clock(void) { return RDS32(NM_MATCH_MINUTES) * 60 + RDS32(NM_MATCH_SECONDS); }
static int rr_live(void) {
    return rr_mode == RR_LIVE && RDS32(NM_MODE) == -2 && (RD32(NM_MATCH_FLAGS) & NM_MATCH_FLAGS_RUMBLE);
}
static int rr_absent(int s) { return RDS16(W(s) + 0x98) == -1; }
static int rr_cpu(int s) { return RDS32(NM_SLOT_PAD + 4u * (u32)s) == 0; }

// A Superstar off the select's pages (the CPU who starts against the player): not the player's. The
// SUPERSTARS pages (1-5, and 6-7 with the new Superstars), not the ladies' or the hidden ones.
// The same man twice: two versions of one Superstar -- a base and his costume variants (variants.c's table: the game's
// Undertaker 0x003 and No Mercy Plus's, his 1999 look, 0x00A) -- with rumble_one_each, a Royal Rumble has one of them
static int rr_same_man(u16 a, u16 b) {
    if (!rumble_one_each || a == b) return a == b;
    return variant_same_man(a, b);
}
// (QA, qa_rr_ports: the Superstars brought over from other games -- their models made as the match goes on have
// packed palettes, skel.c pal_group_guard)
static const u16 qa_ports[] = { 0x030B, 0x060E, 0x060B, 0x0509, 0x050A, 0x030C, 0x030D, 0x030E, 0x060C,
                                0x030F, 0x0409, 0x060A, 0x0408, 0x0309, 0x030A, 0x000A, 0x060D, 0x0105, 0x0106, 0x0107 };
#define QA_PORTS_N (int)(sizeof(qa_ports) / sizeof(qa_ports[0]))
static int qa_is_port(u16 id) { for (int i = 0; i < QA_PORTS_N; i++) if (qa_ports[i] == id) return 1; return 0; }
int roster_is_folder_id(u32 id);
u16 roster_folder_id_at(u32 r);
int roster_not_drawn(u16 id);       // (roster.c: the random pools' one rule -- rostertags, 2026-11-21)
int roster_class(u16 id);
int roster_rivals(u16 a, u16 b);
static u16 rr_random_superstar(u16 not_this) {
    if (qa_on == 2 && qa_rr_first > 0 && qa_rr_first < 0x1000) return (u16)qa_rr_first;   // (QA: rumblecrash's guard test)
    if (qa_on == 2 && qa_rr_folders)     // (QA, qa_rr_folders: a wrestler from a folder, any of them -- 2026-11-13)
        for (int tries = 0; tries < 8; tries++) {
            u16 id = roster_folder_id_at(rng());
            if (id && !rr_same_man(id, not_this)) return id;
        }
    // (rumblecrash, 2026-11-22: only a port this install has -- the same roster check as every other draw here
    //  (GF_RR_ELIGIBLE). The list is fixed, and since the source games became separate (2026-11-19) some of it is
    //  only there with its game: No Mercy Plus's THRASHER, drawn on an install without No Mercy Plus, had an empty
    //  match record and crashed music_rumble_late at the bell)
    if (qa_on == 2 && qa_rr_ports)
        for (int tries = 0; tries < 8; tries++) {
            u16 id = qa_ports[rng() % (u32)QA_PORTS_N];
            if (!rr_same_man(id, not_this) && plat_game_call(GF_RR_ELIGIBLE, id, 0, 0)) return id;
        }
    // (a page of the draw's -- not the ladies', the hidden ones', Create-a-Wrestler's: PAGE_NO_RANDOM -- at random,
    // one of it at random, one the game lets into its field)
    for (int tries = 0; tries < 64; tries++) {
        u32 p = rng() % (u32)(SEL_PAGES_N > 0 ? SEL_PAGES_N : 1);
        if (roster_pages_flags[p] & PAGE_NO_RANDOM) continue;
        u32 pg = NM_SEL_PAGES + SEL_PAGE_SIZE * p;
        u32 n = RD16(pg + 0x24);
        if (n == 0 || n > 9) continue;
        u16 id = RD16(pg + 4u * (rng() % n));
        if (id && id < 0x1000) id = variant_pick_member(id, rng());   // (a box's family: its costume variants have none of their own)
        if (id && id < 0x1000 && rumble_no_women && roster_not_drawn(id)) continue;   // (rostertags, 2026-11-21: an import's page has no
                                                                                      //  PAGE_NO_RANDOM: the same rule as the field's)
        if (id && id < 0x1000 && !rr_same_man(id, not_this) && plat_game_call(GF_RR_ELIGIBLE, id, 0, 0)) return id;
    }
    return not_this == 0x001 ? 0x002 : 0x001;
}

// The operator's Royal Rumble, set up as its pages leave it: the game's own Royal Rumble choice,
// then what ORDER Random, NUMBER, PLAYER 1P vs CPU, ARENA Royal Rumble and its default RULES write
// (read off the game's pages: the set-up bits, the picks, the line-up, the arena, the rules and the
// match flags and venue they lead to), then the Superstar Select's context.
// RULES as the page leaves them (a byte per choice; 0 is a row's first choice), with
//  - COUNT OUT on Lose (byte 2; 1 Hardcore, 2 No): thrown out of the ring is out of the Rumble --
//    the game's own over-the-top elimination (func_80135EE8 makes it the match rule 0x400);
//  - TIME COUNT on Yes (byte 11): each entrant's entrance (action 0xA3) starts with the game's
//    countdown, :10 down to :00 in the HUD with the buzzer at :00 (match rule 0x40000; without it
//    func_8011D400 starts the entrance past the countdown, at step 4).
// The rest: PIN No, SUBMISSION No, TKO No, ROPE BREAK No, DQ No, BLOODSHED Yes -- over the top
// rope is the only way out, as in the real Rumble (the game's own preset has PIN, SUBMISSION and
// ROPE BREAK on Yes: rr_rules_defaults puts them on No wherever a Royal Rumble's rules are preset).
// The rows (bytes 3..8) and the match rule bits func_80135EE8 makes of a row on Yes: PIN 0x20,
// SUBMISSION 0x10, TKO 0x1, ROPE BREAK 0x2, DQ 0x2000, BLOODSHED 0x800.
#define RULE_ROW_COUNT_OUT  2
#define RULE_ROW_PIN        3
#define RULE_ROW_SUBMISSION 4
#define RULE_ROW_ROPE_BREAK 6
#define NM_RULE_ROWS        0x800AA044u   // u8[12]: the RULES page's choices (0: a row's first, Yes)
static const u8 rr_rules_rumble[12] = { 0x0D, 0x05, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 };
// func_80134140 (the RULES page's preset for the match type chosen) has run: a Royal Rumble's is
// without pins, submissions and rope breaks (the page can still turn them on).
void arcade_rules_preset_done(void) {
    if (!rules_preset_on) return;                     // (the running mode's toggle, W9: on in Arcade, off in Regular by default)
    if (!(RD32(NM_SEL_FLAGS) & 0x200u)) return;      // (the set-up bits: Royal Rumble)
    RD8(NM_RULE_ROWS + RULE_ROW_COUNT_OUT) = 0;      // COUNT OUT: Lose -- out of the ring is out
    RD8(NM_RULE_ROWS + RULE_ROW_PIN) = 1;
    RD8(NM_RULE_ROWS + RULE_ROW_SUBMISSION) = 1;
    RD8(NM_RULE_ROWS + RULE_ROW_ROPE_BREAK) = 1;
}
// The Royal Rumble's default RULES (rumblemenu 2026-11-25, the user: Count Out Lose; Pin, Submission, TKO, Rope Break, DQ
// No; Bloodshed Yes; Time Count Yes). The game keeps each match type's rules in the save: 14 packed slots (save image
// +0x6E28, 4 bytes each), the Royal Rumble's slot 13. The RULES page opens on the slot (load_rules_for_context ->
// apply_rules_preset(0x4B + 13)) and the rules played are written back to it (apply_match_type_rules(1)), so the slot is
// the player's own Rumble rules from then on. A new save fills the slots from the ROM's presets 0..13 (fill_save_with_
// defaults, func_800E45A0, each through load_rules_preset(e, its own rows) then pack_rules; preset 13: Count Out No, Pin
// Yes, Submission Yes, TKO No, Rope Break Yes, DQ No, Bloodshed Yes, Time Count No). So it is there that slot 13 gets these
// instead: load_rules_preset's return hook (qa_hook_preset_done, below), when the preset is 13 and the rows are not the
// RULES page's -- fill_save_with_defaults' own, the one caller that passes others -- puts them in those rows, and the game
// packs them into the new save. The ROM's preset 13 itself is left alone (stories and demos read it into the page's rows).
// A save that already exists keeps its slot as it is: a player's own Rumble rules (or the game's old defaults, if they
// were never changed) still win.
// (The first try, 408a091, hooked fill_save_with_defaults' return: never reached. That function is converted (lifted.cpp)
//  with no hook places of its own, and its converted caller create_new_save calls game::fill_save_with_defaults directly
//  -- the built exe: call 0x1401a1ba0, not the entry 0x1401a3000 the mod loader patches. load_rules_preset is converted
//  WITH its hook places (lifted_changed.inc: the QA rules hook below has had them), so its hooks run on every path.)
// (2026-11-25, the user: Time Count was meant to be ON -- byte 11, 0 = Yes: each entrant's countdown, see above)
static const u8 rr_rules_default[12] = { 0x0D, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 };
#define RULES_SLOT_RUMBLE 13
u32 dbg_rumble_rules_default;           // (QA: a new save's Rumble slot given them -- state.csv on the RULES page)
#define RR_COUNTDOWN 10                // seconds of an entrance's countdown (TIME COUNT)
static void rr_configure(void) {
    u32 rec = menu_record(SCR_CHAMPIONSHIP);
    u32 items = RD32(rec + 0x14);
    plat_game_call(GF_ACTION, ACT_ROYAL_RUMBLE, rec, items + CHAMP_FREE_ROW * 0x88u);   // Royal Rumble
    plat_game_call(GF_RULES_PRESET, 1, 0, 0);                                           // its rules
    RD32(NM_SEL_FLAGS) = SEL_FLAGS_RUMBLE;                                              // ORDER: Random
    plat_game_call(GF_SETTINGS_SLOT, 2, 0, 0);          // (the menus remember each match type's choices)
    RD16(NM_SEL_FLAGS + 4) = 0x0318;                                                    // (the pages visited)
    RDS16(NM_SEL_PICKS) = 4;
    // NUMBER: the operator's size. The ring slots that start empty take two places in the order
    // the game makes (the picks come first in it); they're taken out again (rr_finalize_post).
    RDS16(NM_SEL_COUNT) = (s16)(rumble_size + 2);
#ifdef NM_SIX
    // IN THE RING AT ONCE: the engine's rw_rumble_ring, read by the set-up (rw_rumble_fill); a QA scenario's own wins
    RD8(SIX_RUMBLE_RING) = (u8)(qa_ring() ? qa_ring() : rumble_ring);
#endif
    for (u32 r = 0; r < 4; r++) {                                                       // PLAYER: 1P vs CPU
        u32 p = NM_SEL_REC + r * NM_SEL_REC_SIZE;
        RD16(p) = 0xFFFF; RD8(p + 2) = 0xFF; RD8(p + 3) = r == 0;
    }
    int qa_ar = qa_on == 2 && qa_arena >= 1 && qa_arena <= arena_count();   // (QA: the scenario's arena -- ours too, arena.c)
    arena_choose(qa_ar ? qa_arena - 1 : -1);
    RD8(NM_SEL_ARENA) = qa_ar ? (u8)arena_venue(qa_arena - 1) : (u8)ARENA_ROYAL_RUMBLE;   // ARENA
    for (u32 i = 0; i < 12; i++) RD8(NM_RULE_ROWS + i) = rr_rules_rumble[i];            // RULES
    RD32(NM_RR_RULES) = 0x00040C00u;                                                    // (what they make)
    RD32(SIXA_800AA078 + 0xCu) = 0xFFFFFFFFu;
    RD8(0x800A3E31u) = 0xFF; RD8(0x800A9461u) = 0xFF;
    // the choices as the pages record them: ORDER, PLAYER, ARENA, RULES, -, NUMBER
    // (QA: a scenario's arena instead -- the Titantron's entrances at RAW IS WAR, 2026-10-28)
    u8 venue = qa_ar ? (u8)arena_venue(qa_arena - 1) : (u8)ARENA_ROYAL_RUMBLE;
    RD8(0x80158154u) = 0; RD8(0x80158155u) = 3; RD8(0x80158156u) = venue; RD8(0x80158157u) = 1;
    RD8(0x80158158u) = 0xFF; RD8(0x80158159u) = (u8)(rumble_size + 2);
    RD32(NM_MATCH_FLAGS) = NM_MATCH_FLAGS_RUMBLE | 0xFu;                                // the match flags and
    RD8(NM_VENUE - 1) = 5; RD8(NM_VENUE) = venue;                                       // venue they lead to
    RD8(NM_SEL_CTX) = CTX_ONE_PICK;
}
static void rr_open(int rebuild) {
    rr_configure();
    gm_live = 0; gm_on = 0;              // (the arcade's own Rumble: F2's values, not a game-menu match's copy -- W10; after
                                         //  rr_configure, whose own Royal Rumble action would have taken a copy)
    rr_rebuild = rebuild;
    rr_player_out = 0;
    rr_mode = RR_SELECT;
    RDS16(NM_SCREEN_CHANGED) = 1;
    if (!rebuild) { menu_change(SCR_SELECT); return; }
    // after a challenge the winner goes on as who they won with: no select, the match at once
    u32 p = NM_SEL_REC;
    RD16(p) = sp_pick; RD8(p + 2) = sp_costume;
    plat_game_call(GF_SELECT_DONE, 1, 0, 0);
    plat_game_call(GF_FINALIZE, 0, 0, 0);
    menu_change(SCR_MATCH);
}

// The menus' action interpreter (func_8010F0A8) has run an action (W10). 0x404 is Multi Play's Royal Rumble: it has
// just reset the pages' remembered choices (func_8010D874(0): NUMBER's, 0x80158159, to -1). The match gets its copy of
// F2's Rumble values, and the game's own NUMBER page and Start's skip-ahead (func_8010CE74, which reads 0x80158159
// before anything else) start from F2's SUPERSTARS IN IT: written as the page's remembered NUMBER (a byte, 4..40 there).
#define ACT_RUMBLE_MP     0x0404
#define NM_PAGES_NUMBER   0x80158159u   // s8: the NUMBER page's remembered count (-1: none)
void arcade_menu_action_done(s32 action) {
    if (action != ACT_RUMBLE_MP) return;
    gm_on = 1; gm_live = 0;
    gm_size = rumble_size; gm_ring = rumble_ring; gm_interval = rumble_interval; gm_start4 = rumble_start4;
    int n = gm_size < 4 ? 4 : gm_size > 40 ? 40 : gm_size;
    RDS8(NM_PAGES_NUMBER) = (s8)n;
    gm_page_index = n >> 2;               // (where the page's widget starts: a different index at its confirm is a change)
}
// The NUMBER page is about to be set up (func_8012E934), or Start skips ahead past it (func_8010CE74) (W10): both read
// the remembered NUMBER first -- and ORDER's confirm resets it (func_8010D874(2 / 4) when its row differs from the one
// remembered: the first run, 2026-11-07, showed the page at 40, the match type's record, not F2's 12). So the match's
// own count goes in just before either reads it.
u32 dbg_number_setups;                 // tests: calls (QA: whether the game calls these each frame on the page)
void arcade_number_setup(void) {
    dbg_number_setups++;
    if (!gm_on || !(RD32(NM_SEL_FLAGS) & 0x200u)) return;
    int n = gm_size < 4 ? 4 : gm_size > 40 ? 40 : gm_size;
    RDS8(NM_PAGES_NUMBER) = (s8)n;
    gm_page_index = n >> 2;
}
// ---- the NUMBER page counting by one (W10b, 2026-11-07) ----
// The page (screen 9) is one number widget -- a type-5 row: the menus' loop (func_80111158, L_80111544) steps its
// +0x4C by one with UP / DOWN (the menus' repeat word 0x80106640, 0x800 / 0x400) while its +0x74 is 0, then puts it into
// +0x4E between the min / max nibbles of +0x4A (bits 12-15 / 8-11) -- or as it is when they're 0xF (L_801115D4). The big
// number (func_8011ED80) is +0x4E x mult + offset, both from the row's label: bits 4-7 the multiplier (0 = 1), bits 0-3
// the offset -- the game's Rumble label 0x840 shows the index x 4 (4..40: the page's steps of four, min 1 max 10).
// For ORDER Random (the field is drawn, the players don't pick it) the page is made to count by one over F2's own range,
// 2..40, natively: label 0x800 (the value as it is), limits 0xF (no clamping: a 4-bit max can't hold 40), the value
// itself in +0x4C / +0x4D / +0x4E, kept within 2..40 by number_page_frame (round from the ends, as the game's own
// nibbles wrap), read back at the confirm. The other ORDERs, whose players pick every entrant, keep the game's steps.
#define NUMBER_MIN 2
#define NUMBER_MAX 40
static int number_by_one;                // the page is set up counting by one (this Rumble's ORDER is Random)
static int np_page_index_was[4];         // (the new rows' values as the page opened: a different one at the confirm is a change)
static u32 number_row(void) {            // (the page's widget row: screen 9's first)
    u32 items = RD32(menu_record(9) + 0x14);
    return GAME_PTR(items) ? items : 0;
}
// ---- the Rumble's own rows on the NUMBER page (W10b, phase 3) ----
// The F2 Rumble settings the game's screen lacks, as rows of its own NUMBER page -- the champ_versus_defs way: the page
// table (0x8014DFC8: { defs, u8 count } a screen) says 8 for screen 9, its definitions moved into the mod's memory with
// six more after the game's two (the rows are built from the table as the menus start: the pool has room). Each new
// setting is a widget row like NUMBER's own (a type-5 row: UP / DOWN steps it, the game's big number shows it through
// its label's multiplier and offset) under a caption of its own: a picture of its name in the menus' lettering, made the
// way the stories' list makes its rows (label_text_render), its picture label lent while the page is up. LEFT / RIGHT
// go from one setting to the next (UP / DOWN are the values'). A on any of them is the page's own confirm (0x41C).
//   rows 0, 1  NUMBER (the game's widget, counting by one for ORDER Random) and its caption (the game's)
//   rows 2, 3  IN THE RING AT ONCE  4..32         (any ORDER but the teams one, whose ring is its four teams -- 2026-11-15:
//              the order the players pick has its ring filled by the mod, gm_fill_ring; it was ORDER Random only)
//   rows 4, 5  TIME BETWEEN ENTRANTS 10..95 s, x5 (the big number has two digits: F2's 100..120 show as 95 and the match
//              keeps F2's own unless the page is changed)
//   rows 6, 7  STARTS WITH 2 or 4 (x2 + 2)
//   row 8      NUMBER's caption as a picture like the other three ("Number of Superstars", "Number of Teams" for the teams
//              ORDER), in row 1's place while the rows are up -- row 1, the game's text caption, hidden then (rumblemenu)
// The other match type with a NUMBER tab (counting 3..16) gets the page as the game has it: the new rows hidden. The rows
// are up for every ORDER of a game-menu Rumble (the user, 2026-11-15: "should work for every Rumble choice"); only ORDER
// Random's NUMBER counts by one (the others' players pick every entrant: the game's steps of four).
// Which setting LEFT / RIGHT is on (rumblemenu 2026-11-25, the user: "nothing shows which one is selected"): its caption lit
// as the game lights a picture row under the cursor -- the ORDER page's Random, the championship list's titles: the
// row's +0x57 at 0, which the picture rows' drawer (func_80123588) draws in the lit colour with the pulsing frame round
// it; the others at 0x80, as the game leaves a row that isn't. The game lights only a row the cursor is on (or one with
// the 0x10 draw flag), and a caption is never the cursor's row -- it is linked to its number (def +0x10), and a linked
// row's update (func_801079B4) always puts it unlit -- so the page's update hook lights the current setting's caption
// after that update has run (np_caption_light).
#define NP_ROWS      9
#define NP_PAIRS     8                    // (rows 0..7: the four settings, widget and caption; row 8 NUMBER's picture caption)
#define NP_CAP_NUM_ROW 8
#define NP_DEF       0x16u
#define NP_DEFS_ROM  0x8014CCA0u          // (the page's two definitions in ovl_c's data)
static u8 np_defs[NP_ROWS * NP_DEF] __attribute__((aligned(4)));
// (each setting's place: x, y -- from run 193858's shot, in the page's own units: 1 ~ 4.2 px of a 960-px picture; a
//  number's top ~ 304 px + 4.06 y; the game's text caption at the row's own y draws ~80 px above its number; a picture
//  caption ~205 px lower than a text row at the same y -- so the pictures go 33 units above their widgets)
// (2026-11-15, the user's playtest: "the Rumble options page's labels overlap the title and the tab bar". At y -30 the top row's
//  numbers (~217 px tall) covered the tab bar (~170..222 px) and its captions sat on the title: the game's "Number of Superstars"
//  over "royal rumble", ours over the tabs -- QA 2026-11-15_094928 shot 125.0s shows it too. Now everything below the tab bar: the
//  top row's numbers from ~300 px (y -1), the bottom row's from ~584 (y 69, ending ~809, clear of START / Decide); the game's
//  text caption (65 px tall, ~128 px above its number at its widget's own y) put 13 lower, ~10 px above its number as ours are.)
// (2026-11-25, rumblemenu -- the user: the bottom row's "30" and "5" hang below the panel. At y 69 the bottom numbers ended
//  ~780 px, ~45 px into the dark bar the page's START / Decision sits on (its top edge ~735 px); the stock page has one
//  number, up top, and no clipping of its own. Now the bottom row is 13 units up (y 56: its numbers ~518..725 px, inside the
//  panel), the top row 3 up (y -4: ~267..455 px, its caption still under the tab bar) and the picture captions 3 units
//  nearer their numbers (-30), so each caption keeps clear of the number above it.)
static const s16 np_xy[4][2] = { { -76, -4 }, { 76, -4 }, { -76, 56 }, { 76, 56 } };
#define NP_CAP_DY_TEXT 13                  // (the game's caption: from its widget's y, down to sit just over its number)
#define NP_CAP_DY_PIC  (-30)               // (ours, pictures)
static const u16 np_label[4] = { 0x0800, 0x0800, 0x0850, 0x0822 };   // (x1, x1, x5, x2 + 2)
static const s8 np_min[4] = { NUMBER_MIN, 4, 2, 0 }, np_max[4] = { NUMBER_MAX, NM_SLOTS_CAP, 19, 1 };
#define NP_CAP_LABEL0 0x8528               // (captions' picture labels lent on the page: PPV / KotR MATCH types, never up with it)
static const char* const np_caption[4] = { "In the Ring at Once", "Entry Time (sec)", "Starts With", "Number of Superstars" };
static u8 np_cap_ok[4];
static u16 np_lend_was[4][2];
static int np_row1_hidden;                // (the game's caption row hidden by us -- shown again on a page without the rows)
static int np_cap_num_teams;              // (NUMBER's caption as made: 1 "Number of Teams")
static int np_lent, np_rows_on;           // (captions lent; the page has the new rows -- the table was patched)
static int np_rows_up;                    // (... and they're up: this page is a game-menu Rumble's, any ORDER)
static int label_text_render(u8* t, const char* text);
// An overlay was loaded (both modes): while ovl_c's data is as the ROM has it, the page table says 8 rows for screen 9.
static void number_page_defs(void) {
    if (!title_loaded()) return;
    if (RD32(PAGE_ENTRY(9)) != NP_DEFS_ROM || RD8(PAGE_ENTRY(9) + 4) != 2) { np_rows_on = RD32(PAGE_ENTRY(9)) == (u32)np_defs; return; }
    for (u32 k = 0; k < 2 * NP_DEF; k++) np_defs[k] = RD8(NP_DEFS_ROM + k);
    for (int r = 2; r < NP_ROWS; r++)                       // (a widget like row 0, a caption like row 1; row 8 a caption)
        for (u32 k = 0; k < NP_DEF; k++) np_defs[(u32)r * NP_DEF + k] = np_defs[(u32)(r < NP_PAIRS ? (r & 1) : 1) * NP_DEF + k];
    // the caption rows take a picture row's style -- the championship list's Women's row, a 128-px picture label
    for (int r = 3; r < NP_ROWS; r += 2) { np_defs[(u32)r * NP_DEF + 0xE] = RD8(CHAMP_WOMENS_DEF + 0xE); np_defs[(u32)r * NP_DEF + 0xF] = RD8(CHAMP_WOMENS_DEF + 0xF); }
    np_defs[(u32)NP_CAP_NUM_ROW * NP_DEF + 0xE] = RD8(CHAMP_WOMENS_DEF + 0xE); np_defs[(u32)NP_CAP_NUM_ROW * NP_DEF + 0xF] = RD8(CHAMP_WOMENS_DEF + 0xF);
    for (int g = 0; g < 4; g++) {
        u32 w = (u32)(2 * g) * NP_DEF, c = w + NP_DEF;
        *(s16*)&np_defs[w + 0] = np_xy[g][0]; *(s16*)&np_defs[w + 2] = np_xy[g][1];
        *(s16*)&np_defs[c + 0] = np_xy[g][0]; *(s16*)&np_defs[c + 2] = (s16)(np_xy[g][1] + (g ? NP_CAP_DY_PIC : NP_CAP_DY_TEXT));
        // links: up / down the widget's own (the values'); left / right round the four settings
        np_defs[w + 6] = (u8)(2 * g); np_defs[w + 7] = (u8)(2 * g);
        np_defs[w + 8] = (u8)(2 * ((g + 3) & 3)); np_defs[w + 9] = (u8)(2 * ((g + 1) & 3));
        np_defs[c + 6] = np_defs[c + 7] = np_defs[c + 8] = np_defs[c + 9] = (u8)(2 * g + 1);
        if (g) { *(u16*)&np_defs[c + 0xA] = (u16)(NP_CAP_LABEL0 + g - 1); *(u16*)&np_defs[c + 0x12] = 0xFFFF; }
        *(s16*)&np_defs[c + 0x10] = (s16)(2 * g);        // (each caption linked to its own number: its wobble -- row 1 is the game's, to 0)
    }
    {   // row 8: NUMBER's picture caption, where the other pictures sit over their numbers; linked to row 0
        u32 c = (u32)NP_CAP_NUM_ROW * NP_DEF;
        *(s16*)&np_defs[c + 0] = np_xy[0][0]; *(s16*)&np_defs[c + 2] = (s16)(np_xy[0][1] + NP_CAP_DY_PIC);
        np_defs[c + 6] = np_defs[c + 7] = np_defs[c + 8] = np_defs[c + 9] = (u8)NP_CAP_NUM_ROW;
        *(u16*)&np_defs[c + 0xA] = (u16)(NP_CAP_LABEL0 + 3); *(u16*)&np_defs[c + 0x12] = 0xFFFF;
        *(s16*)&np_defs[c + 0x10] = 0;
    }
    RD32(PAGE_ENTRY(9)) = (u32)np_defs;
    RD8(PAGE_ENTRY(9) + 4) = NP_ROWS;
    np_rows_on = 1;
}
static u32 np_row(int r) { u32 it = number_row(); return it ? it + (u32)r * 0x88u : 0; }
// the captions' pictures, made once each (the game's text maker can be out of room a moment: tried again next frame)
static u32 np_cap_file(int k) { return k < 3 ? FILE_NP_CAP0 + (u32)k : FILE_NP_CAP_NUM; }
static void np_captions_make(void) {
    int teams = (RD32(NM_SEL_FLAGS) & 0x10000u) != 0;          // (NUMBER's caption: the teams ORDER counts teams)
    if (np_cap_ok[3] && np_cap_num_teams != teams) np_cap_ok[3] = 0;
    for (int k = 0; k < 4; k++)
        if (!np_cap_ok[k] && label_text_render(np_cap_tex[k], k == 3 && teams ? "Number of Teams" : np_caption[k])) {
            np_cap_ok[k] = 1;
            if (k == 3) np_cap_num_teams = teams;
            plat_game_call(GF_SPRITE_RELEASE, np_cap_file(k), 0, 0);
        }
}
// the captions' labels lent while the page is up, given back as soon as it isn't (as Regular's Custom Story label)
static void np_captions_lend(int up) {
    if (up && !np_lent) {
        for (int k = 0; k < 4; k++) { np_lend_was[k][0] = RD16(LABEL_ENTRY(NP_CAP_LABEL0 + k)); np_lend_was[k][1] = RD16(LABEL_ENTRY(NP_CAP_LABEL0 + k) + 2); }
        np_lent = 1;
    }
    if (up) for (int k = 0; k < 4; k++) { RD16(LABEL_ENTRY(NP_CAP_LABEL0 + k)) = (u16)np_cap_file(k); RD16(LABEL_ENTRY(NP_CAP_LABEL0 + k) + 2) = 9; }
    else if (np_lent) {
        if (title_loaded()) for (int k = 0; k < 4; k++) { RD16(LABEL_ENTRY(NP_CAP_LABEL0 + k)) = np_lend_was[k][0]; RD16(LABEL_ENTRY(NP_CAP_LABEL0 + k) + 2) = np_lend_was[k][1]; }
        np_lent = 0;
    }
}
static void number_row_put(u32 row, int v, int show_from) {
    RD8(row + 0x4C) = (u8)v; RD8(row + 0x4D) = (u8)show_from; RD8(row + 0x4E) = (u8)v;
    RDS16(row + 0x62) = (s16)v; RDS16(row + 0x50) = (s16)(v << 7);
}
// func_8012E934 (the page's set-up) has returned: the widget made to count by one, the match's count in it.
void arcade_number_setup_done(void) {
    number_by_one = 0; np_rows_up = 0;
    // (the new rows only for a game-menu Rumble -- hidden on the other NUMBER pages, the game's own as they are)
    if (np_rows_on) for (int r = 2; r < NP_ROWS; r++) { u32 x = np_row(r); if (x) RD8(x + 0x4B) = 1; }
    if (np_rows_on && np_row1_hidden) { u32 x = np_row(1); if (x) RD8(x + 0x4B) = 0; np_row1_hidden = 0; }   // (the game's caption back)
    if (!gm_on || !(RD32(NM_SEL_FLAGS) & 0x200u)) return;
    u32 row = number_row();
    if (!row) return;
    int v = gm_size < NUMBER_MIN ? NUMBER_MIN : gm_size > NUMBER_MAX ? NUMBER_MAX : gm_size;
    if (RD32(NM_SEL_FLAGS) & 0x20000u) {             // (ORDER Random: NUMBER counts by one)
        RD16(row + 0x60) = 0x0800;                                   // (x 1 + 0: the value as it is)
        RD16(row + 0x4A) = (u16)((RD16(row + 0x4A) & 0x00FFu) | 0xF000u);   // (no min / max: the page's own code steps it)
        number_row_put(row, v, v);
        number_by_one = 1;
    }
    if (!np_rows_on) return;
    // (the teams ORDER, 0x10000: one slot a team, its men coming in turn -- its ring is its four teams, no row for it)
    int teams = (RD32(NM_SEL_FLAGS) & 0x10000u) != 0;
    for (int r = teams ? 4 : 2; r < NP_ROWS; r++) { u32 x = np_row(r); if (x) RD8(x + 0x4B) = 0; }
    { u32 x = np_row(1); if (x && !RD8(x + 0x4B)) { RD8(x + 0x4B) = 1; np_row1_hidden = 1; } }   // (NUMBER's caption: ours, row 8)
    { u32 x = np_row(NP_CAP_NUM_ROW); if (x) RD16(x + 0x60) = (u16)(NP_CAP_LABEL0 + 3); }
    np_rows_up = 1;
    // the new rows (phase 3): each widget its value from the match's copy, as the game's own set-up does row 0 (its
    // limits as row 0's, its label the value's format); each caption its lent picture
    int iv = gm_interval / 5; if (iv > 19) iv = 19; if (iv < 2) iv = 2;
    int val[4] = { v, gm_ring, iv, gm_start4 ? 1 : 0 };
    for (int g = 1; g < 4; g++) {
        u32 w = np_row(2 * g), c = np_row(2 * g + 1);
        if (!w || !c) continue;
        int x = val[g] < np_min[g] ? np_min[g] : val[g] > np_max[g] ? np_max[g] : val[g];
        RD16(w + 0x60) = np_label[g];
        RD16(w + 0x4A) = (u16)((RD16(row + 0x4A) & 0x00FFu) | 0xF000u);
        // (as the game's set-up leaves its own widget: the fields the menus' loop and the drawer read besides the value --
        //  +0x52 the step's direction, +0x54 the drawer's colour source, +0x5A the row's help, +0x74 the value's source;
        //  the loop steps a type-5 row only with +0x74 and +0x52 at 0 -- the first run's new rows took no UP / DOWN)
        RD16(w + 0x52) = RD16(row + 0x52); RD16(w + 0x54) = RD16(row + 0x54);
        RD16(w + 0x5A) = RD16(row + 0x5A); RD16(w + 0x74) = RD16(row + 0x74);
        number_row_put(w, x, x);
        RD16(c + 0x60) = (u16)(NP_CAP_LABEL0 + g - 1);
    }
    np_page_index_was[0] = v; np_page_index_was[1] = val[1]; np_page_index_was[2] = iv; np_page_index_was[3] = val[3];
}
// The game has two "no limits" conventions for a widget row and they can't both hold at once: the menus' loop takes
// +0x4A & 0xFF00 == 0xF000 (min 15, max 0) as none (L_801115D4), while the rows' own per-frame update
// (func_801079B4, L_801082B0 -- called for each row by func_80108C54) takes min == max as none and otherwise clamps
// +0x4E into min..max -- with 0xF000 it put every value to 15 (runs 191433..192719). So for this page's widget rows,
// while that update runs the limits read min == max (0); after it, 0xF000 again for the loop.
static u32 row_update_at, np_cap_update_at;
static int np_cap_update_widget;
void arcade_row_update(u32 row) {
    row_update_at = 0; np_cap_update_at = 0;
    if (!(number_by_one || np_rows_up) || !in_menus() || RDS16(NM_SCREEN) != 9) return;
    if (np_rows_up) {                     // (a caption: lit after the update when its setting is the cursor's -- np_caption_light)
        static const u8 caps[4] = { NP_CAP_NUM_ROW, 3, 5, 7 };   // (the four captions: NUMBER's, then IN THE RING's, ENTRY TIME's, STARTS WITH's)
        for (int g = 0; g < 4; g++) if (row == np_row(caps[g])) { np_cap_update_at = row; np_cap_update_widget = 2 * g; return; }
    }
    int r = -1;
    for (int k = number_by_one ? 0 : 2; k < (np_rows_up ? NP_PAIRS : 1); k += 2) if (row == np_row(k)) r = k;
    if (r < 0) return;
    RD16(row + 0x4A) = (u16)(RD16(row + 0x4A) & 0x00FFu);
    row_update_at = row;
}
// (np_caption_light: the update has put the caption unlit, +0x57 0x80, as for any linked row; the current setting's is lit,
//  0 -- what the update gives a row under the cursor -- so the drawer draws it in the lit colour with its pulsing frame)
static void np_caption_light(void) {
    u32 c = np_cap_update_at;
    np_cap_update_at = 0;
    if (!c || (RD8(c + 0x4B) & 1u)) return;
    int cur = RD8(menu_record(9) + 5);
    if (cur == np_cap_update_widget && RD8(c + 0x56) == 0xFF) RD8(c + 0x57) = 0;
}
void arcade_row_update_done(void) {
    if (np_cap_update_at) np_caption_light();
    if (!row_update_at) return;
    RD16(row_update_at + 0x4A) = (u16)((RD16(row_update_at + 0x4A) & 0x00FFu) | 0xF000u);
    row_update_at = 0;
}
// Each frame (both modes, the universal part of arcade_frame): on the page counting by one, the value kept within
// 2..40 -- past 40 round to 2, below 2 round to 40, as the game's own min / max wrap; the number shows from where it was.
static void number_page_frame(int hold_limits) {
    int up = np_rows_up && in_menus() && RDS16(NM_SCREEN) == 9;
    if (!hold_limits) { if (up) np_captions_make(); np_captions_lend(up); }   // (the captions: from the frame task)
    if (!number_by_one && !np_rows_up) return;
    if (!in_menus() || RDS16(NM_SCREEN) != 9) return;
    u32 row = number_row();
    if (!row) return;
    // (the limits held at "none" each frame: something of the page's own ORs the game's 0x1A00 -- min 1, max 10 -- back in
    //  after its set-up, which with ours made min 15 / max 10 and sent every value to 15, run 191433)
    // (only from the controller read, the game's own thread, before the menus' loop: not from the frame task, which could
    //  land in the middle of the row's update -- arcade_row_update)
    for (int g = number_by_one ? 0 : 1; g < (np_rows_up ? 4 : 1); g++) {   // (row 0 the game's own unless by one)
        u32 w = np_row(2 * g);
        if (!w) continue;
        if (hold_limits) { RD16(w + 0x4A) = (u16)((RD16(w + 0x4A) & 0x00FFu) | 0xF000u); RD16(w + 0x60) = np_label[g]; }
        int v = RDS8(w + 0x4C);
        if (v > np_max[g]) number_row_put(w, np_min[g], RDS8(w + 0x4D));
        else if (v < np_min[g]) number_row_put(w, np_max[g], RDS8(w + 0x4D));
    }
}
// The NUMBER page's confirm (func_8012E9F0) has run (W10): it made NM_SEL_COUNT the widget's index x 4. Unchanged on
// the page, the match keeps F2's own count (any 2..40, not just steps of four); changed there, that is the match's.
void arcade_number_confirm_done(void) {
    if (!gm_on) return;
    if (number_by_one) {                 // (W10b: the page's own value, by one -- the game's confirm made it x 4)
        u32 row = number_row();
        if (row) {
            int v = RDS8(row + 0x4E);
            if (v >= NUMBER_MIN && v <= NUMBER_MAX) gm_size = v;
        }
        RDS16(NM_SEL_COUNT) = (s16)(gm_size < 4 ? 4 : gm_size);   // (the four picks start it: at least four in the order)
        RDS8(NM_PAGES_NUMBER) = (s8)gm_size;                         // (the page's memory: as it was left)
    } else {
        s32 n = RDS16(NM_SEL_COUNT);
        if ((n >> 2) != gm_page_index) gm_size = n;
    }
    if (np_rows_up) {                    // (the new rows, any ORDER: a value changed on the page is the match's)
        for (int g = 1; g < 4; g++) {
            u32 w = np_row(2 * g);
            if (!w || RD8(w + 0x4B)) continue;   // (hidden: the teams ORDER's ring)
            int v = RDS8(w + 0x4E);
            if (v < np_min[g] || v > np_max[g] || v == np_page_index_was[g]) continue;
            if (g == 1) gm_ring = v; else if (g == 2) gm_interval = v * 5; else gm_start4 = v != 0;
        }
    }
}
// The game-menu Rumble's start (W10, the user 2026-11-07: "starts 1 on 1 by default (two at the bell, then one per
// interval); a toggle makes it start with 4"): the slots past the start (CPU ones -- a human is never left out of the
// bell) are emptied as the arcade's own start has them (no Superstar, no controller: rr_absent), their men back to
// coming in order (the game's order list has the slots' men first, so the next one called is the first of them). The
// clock (arcade_rr_frame, TIME BETWEEN ENTRANTS) calls the rest. ORDER Random only gets the field drawn again with NO
// WOMEN / ANNOUNCERS and ONE UNDERTAKER (the players didn't choose who's in it), the four at the start kept.
static int rr_draw_field_from(int from, int w);
// IN THE RING AT ONCE for the ORDER whose players pick every entrant (match kind 0x40000; 2026-11-15, the user: the
// Rumble's options for every choice, not only Random). The six-man engine's end of the Rumble's set-up (rw_rumble_fill,
// six-engine rw/r_menu.c) fills slots 4 on only for a drawn field and leaves this ORDER at the stock four. But its order
// list is made the same way -- the picks in order (func_8013486C copies every pick into 0x800BD4D0), the first four in
// slots 0..3 -- so its ring is filled here as rw_rumble_fill fills a drawn one: entrants 4 .. n-1 into slots 4 .. n-1
// (the Superstar, costume, the computer's controller, the entrant's level) and the match's count n (rw_match_count,
// which the match overlay reads as it starts); gm_start then empties those past the start as for any ORDER. Fewer than
// six in the ring stays four, as the engine has it. Not the teams ORDER (0x10000): one slot a team, its men in turn.
#ifdef NM_SIX
#define RR_ENTRANT_COSTUME 0x800A9358u     // u8 per entrant (rw_rumble_fill's ROSTER_COST)
#define RR_ENTRANT_LEVEL   0x800A98E0u     // u8 per entrant: the computer's level (ROSTER_LEVEL)
static void gm_fill_ring(void) {
    u32 kind = RD32(NM_SEL_FLAGS);
    if (!(kind & 0x200u) || !(kind & 0x40000u) || (kind & 0x30000u)) return;
    s32 n = RD8(SIX_RUMBLE_RING), entrants = RDS16(NM_SEL_COUNT);
    if (n > entrants) n = entrants;
    if (n > NM_SLOTS_CAP) n = NM_SLOTS_CAP;
    if (n < 6 || RDS32(SIX_MATCH_COUNT) >= n) return;
    for (s32 i = 4; i < n; i++) {
        RDS32(NM_SLOT_WRESTLER + 4u * (u32)i) = RD16(NM_RR_LIST + 2u * (u32)i);
        RDS32(SIXA_800A8958 + 4u * (u32)i) = RD8(RR_ENTRANT_COSTUME + (u32)i);
        RDS32(NM_SLOT_PAD + 4u * (u32)i) = 0;
        RD16(SIXA_8009ED10 + 2u * (u32)i) = RD8(RR_ENTRANT_LEVEL + (u32)i);
    }
    RDS32(SIX_MATCH_COUNT) = n;
}
#else
static void gm_fill_ring(void) {}
#endif
static void gm_start(void) {
    int slots = RDS32(SIX_MATCH_COUNT);
    if (slots < 4) slots = 4;
    if (slots > NM_SLOTS_CAP) slots = NM_SLOTS_CAP;
    int keep = gm_start4 ? 4 : 2;
    if (keep > gm_size && gm_size >= NUMBER_MIN) keep = gm_size;   // (never more at the bell than there are in it)
    for (int s = 0; s < 4; s++) { s32 pad = RDS32(NM_SLOT_PAD + 4u * (u32)s); if (pad > 0 && s + 1 > keep) keep = s + 1; }
    for (int s = keep; s < slots; s++) {
        if (RDS32(NM_SLOT_PAD + 4u * (u32)s) > 0) continue;
        RDS32(NM_SLOT_WRESTLER + 4u * (u32)s) = -1;
        RDS32(NM_SLOT_PAD + 4u * (u32)s) = -1;
        if (s < 4) RD16(NM_SEL_REC + (u32)s * NM_SEL_REC_SIZE) = 0xFFFF;
    }
}
// The menus set the match up from the picks (func_80136784): the order is made from the picks and
// the roster at random (func_8013486C), the slots from the picks.
void arcade_rr_finalize_pre(void) {
    lumberjack_finalize_pre();                   // (lumberjack2: a Lumberjack tornado tag: the RULES page's tag row tornado, for now)
    // (W10) a Royal Rumble from the game's own menus with the match's copy of F2's values: IN THE RING AT ONCE for this
    // match (written each time: the engine's byte otherwise keeps whatever the last Rumble left -- discovery D2), and for
    // ORDER Random SUPERSTARS IN IT (four at least: the four picks start it)
    if (rr_mode == RR_OFF && gm_on && RD8(NM_SEL_CTX) == CONTEXT_RUMBLE && (RD32(NM_SEL_FLAGS) & 0x200u)) {
#ifdef NM_SIX
        RD8(SIX_RUMBLE_RING) = (u8)(qa_ring() ? qa_ring() : gm_ring);
#endif
        if (RD32(NM_SEL_FLAGS) & 0x20000u) RDS16(NM_SEL_COUNT) = (s16)(gm_size < 4 ? 4 : gm_size > 40 ? 40 : gm_size);
        // (2 or 3, W10b: the game's order is made of four at least -- the picks -- and cut to that many after it, below)
        gm_live = 2;                                // (the post below takes it on)
        return;
    }
    if (rr_mode != RR_SELECT) return;
    u16 me = RD16(NM_SEL_REC);
    RD8(NM_SEL_REC + 3) = 1;
    if (!rr_rebuild && me != 0xFFFF && me < 0x1000) { sp_pick = me; sp_costume = RD8(NM_SEL_REC + 2); }   // (who single player goes on as)
    if (rr_rebuild) {
        for (int k = 0; k < rr_kept && k < 3; k++) {
            u32 p = NM_SEL_REC + (u32)(k + 1) * NM_SEL_REC_SIZE;
            RD16(p) = rr_rest[k]; RD8(p + 2) = rr_rest_a[k]; RD8(p + 3) = 0;
        }
        return;
    }
    u32 p = NM_SEL_REC + NM_SEL_REC_SIZE;
    RD16(p) = rr_random_superstar(me); RD8(p + 2) = 0; RD8(p + 3) = 0;
}
// The game's random field (func_8013486C) takes one Superstar from each select page in turn (page 1,
// page 2, ... the ladies, the others, the hidden ones, page 1 again), so who comes when follows the
// pages. The arcade draws the whole field again from the same Superstars (the pages', those the game
// lets into its field), in a shuffled order: after the player and the first entrant, anyone can come
// out next. Costumes as the game picks them (1 or 2). -> the entrants in the list
#define NM_SEL_PAGE_TABLE (NM_SEL_PAGES + 0x24u)   // the select's pages: u16 how many, 0x38 bytes each (roster.c's)
#define SEL_PAGE_SLOTS ((u32)SEL_PAGES_N)   // (the game's field went through its 12; now every page there is)
// The women (the ladies' page, Linda McMahon, Mae Young, the Kat, Ho: IDs 0x702.. 0x7xx and 0x8xx --
// Chyna, 0x701, is one of the Superstars), Earl Hebner and Jim Ross (0x903, 0x904): kept out of the
// Rumble's field with the operator's NO WOMEN / ANNOUNCERS IN THE RUMBLE.
// Changed for new features (Ultimate No Mercy) -- rostertags, 2026-11-21 (the user: "We need to specify Revenge and WM2000
// wrestlers as women or announcers and include them in that setting"): the one rule every random pool takes, roster.c
// roster_not_drawn -- a folder's "class" (the importers write it: Revenge's valets and managers, WrestleMania 2000's women,
// announcers, officials, managers), else the numbers above, as before. So the setting keeps the imports' women and
// announcers out too (the Lumberjack draw always: lj_drawing).
static int rr_not_a_wrestler(u16 id) {
    return roster_not_drawn(id);
}
// (from: the places kept as they are -- the arcade's 2, the player and the first entrant; a game-menu Rumble's 4, the
// four picks -- nobody drawn the same man as one of them)
// (2026-11-13, seatsroot) The draw's pool holds 128; it used to stop there, so with more Superstars on the pages than that
// the ones after the 128th (in page order) were never drawn. Past 128 now each further one replaces one of the pool at
// random (a reservoir: each of the n has the same 128-in-n chance), so anyone on any page can be drawn; up to 128 nothing
// changes, not even the random numbers used. The pool is what the field is drawn from, never more than the field's 101
// places can take: an "at once" size, not a count of who can be in it. With the select's pages a window (roster.c
// staging), the boxes come from every page of the whole layout (roster_boxes_to_rumble), not the window's.
int roster_boxes_to_rumble(void);
void roster_probe(int on);
static u16 rr_pool[128], rr_pool_keep[4];
static int rr_pool_n, rr_pool_past, rr_from;
static int lj_drawing;                          // (the pool drawn for a Lumberjack match's lumberjacks: arcade_draw_lumberjacks)
void arcade_rr_offer(u16 box) {
    if (box == 0 || box >= 0x1000) return;
    int nf = variant_family_n(box);             // (the box's family too: variants.c -- none of the others has a box;
    if (!nf) nf = 1;                            //  2026-11-14, families: any number of them)
    for (int k = 0; k < nf; k++) {
        u16 id = variant_family_at(box, k);
        int kept = 0;
        for (int k = 0; k < rr_from && k < 4 && !kept; k++) kept = rr_same_man(id, rr_pool_keep[k]);
        if (kept) continue;
        if ((rumble_no_women || lj_drawing) && rr_not_a_wrestler(id)) continue;   // (lumberjacks: wrestlers only, below)
        { int arcade_not_a_wrestler(u16 id); if (lj_drawing && arcade_not_a_wrestler(id)) continue; }   // (lumberjack2: imports' women too)
        roster_probe(1);                         // (the check asks his ordinal: no seat for that -- roster.c)
        const int ok = plat_game_call(GF_RR_ELIGIBLE, id, 0, 0);
        roster_probe(0);
        if (!ok) continue;
        if (qa_on == 2 && qa_rr_ports && !qa_is_port(id)) continue;
        if (qa_on == 2 && qa_rr_folders && !roster_is_folder_id(id)) continue;
        int dup = 0;
        for (int j = 0; j < rr_pool_n && !dup; j++) dup = rr_same_man(rr_pool[j], id);
        if (dup) continue;
        if (rr_pool_n < 128) { rr_pool[rr_pool_n++] = id; continue; }
        const u32 j = rng() % (u32)(128 + ++rr_pool_past);
        if (j < 128) rr_pool[j] = id;
    }
}
static int rr_draw_field_from(int from, int w) {
    u16* pool = rr_pool;
    rr_pool_n = 0; rr_pool_past = 0; rr_from = from;
    for (int k = 0; k < from && k < 4; k++) rr_pool_keep[k] = RD16(NM_RR_LIST + 2u * (u32)k) & 0x0FFFu;
    if (!roster_boxes_to_rumble())
        for (u32 pg = 0; pg < SEL_PAGE_SLOTS; pg++) {
            u32 count = RD16(NM_SEL_PAGE_TABLE + pg * 0x38u);
            if (count > 9) continue;
            for (u32 i = 0; i < count; i++) arcade_rr_offer((u16)plat_game_call(GF_PAGE_MEMBER, pg, i, 0));
        }
    int n = rr_pool_n;
    if (n == 0 || w <= from) return w;
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rng() % (u32)(i + 1));
        u16 t = pool[i]; pool[i] = pool[j]; pool[j] = t;
    }
    if (w > n + from) w = n + from;
    for (int k = from; k < w; k++) {
        RD16(NM_RR_LIST + 2u * (u32)k) = pool[k - from];
        RD8(NM_RR_TABLE_A + (u32)k) = (u8)variant_shown_look(pool[k - from], (int)(rng() & 1u));   // (2026-11-14: never a hidden variant)
    }
    return w;
}
static int rr_draw_field(int w) { return rr_draw_field_from(2, w); }
// New feature (Ultimate No Mercy) -- lumberjack, 2026-11-21: a Lumberjack match's lumberjacks (lumberjack.c), n of them at random from the same
// pool the Royal Rumble's random field is drawn from -- the Superstar Select's pages, the Superstars the game lets into a
// random field, one of a man's versions -- never the two competitors (nor another version of them), never the managers,
// announcers or the women's page (rr_not_a_wrestler, whatever the Rumble's NO WOMEN / ANNOUNCERS says), each in a look the
// select shows. Only the pool is the Rumble's: nothing is written to its entrant list. Too few on the pages (a test's
// title match): the core roster's in turn. -> how many drawn.
// (lumberjack2, 2026-11-21: the competitors are 2, 3 or 4 -- a one on one, a triple threat, a tornado tag -- none of them drawn)
int arcade_draw_lumberjacks(u16* ids, u8* looks, int n, const u16* not_these, int n_not) {
    if (n_not > 4) n_not = 4;
    rr_pool_n = 0; rr_pool_past = 0; rr_from = n_not;
    for (int k = 0; k < 4; k++) rr_pool_keep[k] = k < n_not ? (u16)(not_these[k] & 0x0FFFu) : 0;
    lj_drawing = 1;
    if (!roster_boxes_to_rumble())
        for (u32 pg = 0; pg < SEL_PAGE_SLOTS; pg++) {
            u32 count = RD16(NM_SEL_PAGE_TABLE + pg * 0x38u);
            if (count > 9) continue;
            for (u32 i = 0; i < count; i++) arcade_rr_offer((u16)plat_game_call(GF_PAGE_MEMBER, pg, i, 0));
        }
    lj_drawing = 0;
    int m = rr_pool_n;
    for (int i = m - 1; i > 0; i--) { int j = (int)(rng() % (u32)(i + 1)); u16 t = rr_pool[i]; rr_pool[i] = rr_pool[j]; rr_pool[j] = t; }
    int k = 0;
    for (int i = 0; i < m && k < n; i++) { ids[k] = rr_pool[i]; looks[k] = (u8)variant_shown_look(rr_pool[i], (int)(rng() & 1u)); k++; }
    for (int i = 0; i < 4 * ROSTER_N && k < n; i++) {           // (too few: the core roster, then its second looks)
        u16 id = roster_id[i % ROSTER_N]; int used = 0;
        for (int c = 0; c < n_not && !used; c++) used = rr_same_man(id, rr_pool_keep[c]);
        for (int j = 0; j < k && !used; j++) used = ids[j] == id && looks[j] == (u8)(i / ROSTER_N);
        if (used || arcade_not_a_wrestler(id)) continue;
        ids[k] = id; looks[k] = (u8)(i / ROSTER_N); k++;
    }
    return k;
}
// (lumberjack2) The lumberjacks the player picked on the Superstar Select, after the competitors' picks (the select took
// competitors + N picks: lumberjack_select_picks, below) -- whoever he picked, women and announcers too. -> how many
int arcade_picked_lumberjacks(u16* ids, u8* looks, int from, int n) {
    int k = 0;
    for (int i = from; i < 50 && k < n; i++) {
        u16 id = RD16(NM_SEL_REC + (u32)i * NM_SEL_REC_SIZE);
        if (id == 0 || id >= 0x1000) continue;
        ids[k] = id; looks[k] = RD8(NM_SEL_REC + (u32)i * NM_SEL_REC_SIZE + 2u); k++;
    }
    return k;
}
// (lumberjack2) Not a lumberjack drawn at random: the women and the announcers / the referee (the user, 2026-11-21). Since
// lumberjack3 (main's roster-tags merged in) the random pools' one rule, roster.c roster_not_drawn: a folder's "class" (the
// importers write it -- Revenge's valets and managers, WrestleMania 2000's women, announcers, officials), else the IDs the
// Rumble keeps out. (It was the IDs and the women's weight 500, which let Revenge's Ming Chee through.)
int arcade_not_a_wrestler(u16 id) {
    return roster_not_drawn(id);     // (lumberjack3: roster-tags' one rule for every random pool -- a folder's "class", else the IDs)
}
// (2026-11-13, removal2) The Royal Rumble's field as drawn, with someone removed (roster.c): said in the log -- a removed man
// in it would be a WARNING (the draw is from the pages, which don't have him); QA qa_removal 2: every entrant, 8 a line.
static void rm_rumble_field_say(int w) {
    extern int roster_removed_n(void); extern int roster_removed_qa(void); extern int roster_id_removed(u32 id);
    extern void removal_s(const char* s); extern void removal_who(u32 id); extern void removal_num(s32 v); extern void removal_say(void);
    if (roster_removed_n() <= 0 && !(roster_removed_qa() & 2)) return;
    int bad = 0;
    for (int i = 0; i < w && i < 100; i++) {
        const u16 id = RD16(NM_RR_LIST + 2u * (u32)i) & 0x0FFFu;
        if (roster_id_removed(id)) bad++;
        if ((roster_removed_qa() & 2) && i % 8 == 0) { removal_s("qa rumble field "); removal_num(i + 1); removal_s("-:"); }
        if (roster_removed_qa() & 2) { removal_s(" "); removal_who(id); if (i % 8 == 7 || i == w - 1) removal_say(); }
    }
    if (bad) { removal_s("WARNING: the Royal Rumble's field has "); removal_num(bad); removal_s(" removed men"); }
    else { removal_s("rumble: none of the field's "); removal_num(w); removal_s(" entrants is removed"); }
    removal_say();
}
// (rostertags, 2026-11-21) QA, option qa_tags (1): the random pools' rule and the rivals, in the log ([removal] lines) -- once a
// run, after the Royal Rumble's field is drawn: each entrant with his class; then 20 more draws of the whole pool (the field's
// own way, nothing written) and 20 Lumberjack draws of 30, each man checked (a woman, valet, manager, announcer, referee or
// official among them is a "BAD" line); every rival pair among the field; and set pairs (stock, ports, folders).
static const char* const rt_class_names[] = { "none", "wrestler", "woman", "valet", "manager", "announcer", "referee", "official", "authority" };
static void rt_qa_say_who(u16 id) {
    extern void removal_s(const char* s); extern void removal_who(u32 id);
    const int c = roster_class(id);
    removal_who(id); removal_s("="); removal_s(rt_class_names[c >= 0 && c <= 8 ? c : 0]);
}
static void rt_qa_field(int w) {
    extern void removal_s(const char* s); extern void removal_num(s32 v); extern void removal_say(void);
    static int done;
    if (done || !plat_config("qa_tags", 0)) return;
    done = 1;
    removal_s("tags: NO WOMEN / ANNOUNCERS "); removal_num(rumble_no_women); removal_s(", the field "); removal_num(w); removal_say();
    int bad = 0;
    for (int i = 0; i < w && i < 100; i++) {
        const u16 id = RD16(NM_RR_LIST + 2u * (u32)i) & 0x0FFFu;
        removal_s("tags: field "); removal_num(i + 1); removal_s(" "); rt_qa_say_who(id);
        if (i >= 2 && rumble_no_women && roster_not_drawn(id)) { removal_s(" BAD"); bad++; }
        removal_say();
    }
    // the draws again (the pool as the field takes it: rr_draw_field_from's offer, from every page), nothing written
    int draws = 0, men = 0, imports = 0;
    for (int d = 0; d < 20; d++) {
        rr_pool_n = 0; rr_pool_past = 0; rr_from = 0;
        if (!roster_boxes_to_rumble())
            for (u32 pg = 0; pg < SEL_PAGE_SLOTS; pg++) {
                u32 count = RD16(NM_SEL_PAGE_TABLE + pg * 0x38u);
                if (count > 9) continue;
                for (u32 i = 0; i < count; i++) arcade_rr_offer((u16)plat_game_call(GF_PAGE_MEMBER, pg, i, 0));
            }
        for (int i = 0; i < rr_pool_n; i++) {
            men++; imports += rr_pool[i] >= 0x340 && rr_pool[i] < 0x400;
            if (rumble_no_women && roster_not_drawn(rr_pool[i])) { removal_s("tags: pool BAD "); rt_qa_say_who(rr_pool[i]); removal_say(); bad++; }
        }
        draws++;
    }
    removal_s("tags: rumble pool draws "); removal_num(draws); removal_s(", men "); removal_num(men); removal_s(" (imports 0x340-0x3FF "); removal_num(imports); removal_s(")"); removal_say();
    int lj_men = 0, lj_imports = 0;
    for (int d = 0; d < 20; d++) {
        u16 ids[30]; u8 looks[30];
        static const u16 not_these[2] = { 0x001, 0x002 };   // (lumberjack2's signature: the competitors as a list)
        const int n = arcade_draw_lumberjacks(ids, looks, 30, not_these, 2);
        for (int i = 0; i < n; i++) {
            lj_men++; lj_imports += ids[i] >= 0x340 && ids[i] < 0x400;
            if (roster_not_drawn(ids[i])) { removal_s("tags: lumberjack BAD "); rt_qa_say_who(ids[i]); removal_say(); bad++; }
        }
    }
    removal_s("tags: lumberjack draws 20, men "); removal_num(lj_men); removal_s(" (imports "); removal_num(lj_imports); removal_s(")"); removal_say();
    // the not-drawn among every import (0x340-0x3FF: the frozen Revenge and WrestleMania 2000 numbers) -- the eyeball list
    for (u16 id = 0x340; id < 0x400; id++) {
        const int c = roster_class(id);
        if (c == 1 || !roster_is_folder_id(id)) continue;
        removal_s("tags: not drawn "); rt_qa_say_who(id); removal_say();
    }
    // rivals: every pair among the field; then set pairs (1 expected / 0 expected)
    int pairs = 0;
    for (int i = 0; i < w && i < 100; i++)
        for (int j = i + 1; j < w && j < 100; j++) {
            const u16 a = RD16(NM_RR_LIST + 2u * (u32)i) & 0x0FFFu, b = RD16(NM_RR_LIST + 2u * (u32)j) & 0x0FFFu;
            if (!roster_rivals(a, b)) continue;
            removal_s("tags: field rivals "); rt_qa_say_who(a); removal_s(" - "); rt_qa_say_who(b); removal_say(); pairs++;
        }
    static const u16 probe[][3] = {
        { 0x001, 0x101, 1 },   // The Rock - Triple H: No Mercy's own Rival 1-3
        { 0x101, 0x001, 1 },
        { 0x10D, 0x105, 1 },   // Goldberg - Hollywood Hogan: Revenge's rivals.json (two built-in ports)
        { 0x10B, 0x105, 1 },   // Sting - Hollywood Hogan
        { 0x035C, 0x0365, 1 }, // Raven - Saturn: Revenge's rivals.json (two Revenge folders)
        { 0x0362, 0x10D, 1 },  // Diamond Dallas Page (a folder) - Goldberg (a port)
        { 0x03A1, 0x03A2, 1 }, // Steve Austin - The Rock, WrestleMania 2000's own Rival 1-3 (its rivals.json)
        { 0x03C4, 0x03C5, 1 }, // Edge - Gangrel (WrestleMania 2000)
        { 0x0105, 0x0106, 1 }, // Hogan - Nash: Revenge's rivals.json (1998)
        { 0x001, 0x03A1, 0 },  // The Rock - WrestleMania 2000's Austin: no (each game's within its own)
        { 0x03A1, 0x03E9, 0 }, // Austin - Chyna (WrestleMania 2000): no
        { 0x0105, 0x0107, 0 }, // Hogan - Hall: allies in the port's ties, not rivals
    };
    for (u32 k = 0; k < sizeof probe / sizeof probe[0]; k++) {
        const int r = roster_rivals(probe[k][0], probe[k][1]);
        const int want = (int)probe[k][2];
        removal_s("tags: rivals? "); rt_qa_say_who(probe[k][0]); removal_s(" - "); rt_qa_say_who(probe[k][1]);
        removal_s(r ? ": yes" : ": no"); removal_s(r == want ? " (as expected)" : " UNEXPECTED"); removal_say();
        bad += r != want;
    }
    removal_s("tags: done, rival pairs in the field "); removal_num(pairs); removal_s(", bad "); removal_num(bad); removal_say();
}
void arcade_rr_finalize_post(void) {
    lumberjack_finalize_post();                  // (lumberjack2: ... and back as the player left it)
    if (gm_live == 2 && rr_mode == RR_OFF) {     // (W10: the game-menu Rumble -- see arcade_rr_finalize_pre, gm_start)
        if ((RD32(NM_SEL_FLAGS) & 0x20000u) && (rumble_no_women || rumble_one_each)) {
            int w = RDS32(NM_RR_COUNT_SRC);
            if (w > 4 && w <= 100) rr_draw_field_from(4, w);
        }
        // (W10b: SUPERSTARS IN IT 2 or 3 with ORDER Random: the order the game made (four, its picks) cut to that many;
        //  the start below never keeps more in the ring than that)
        if ((RD32(NM_SEL_FLAGS) & 0x20000u) && gm_size >= NUMBER_MIN && gm_size < 4 && RDS32(NM_RR_COUNT_SRC) > gm_size) {
            RDS32(NM_RR_COUNT_SRC) = gm_size;
            for (int i = gm_size; i < 101; i++) RD16(NM_RR_LIST + 2u * (u32)i) = 0xFFFF;
        }
        gm_fill_ring();
        gm_start();
        rr_rebuild = 0; rr_num_offset = 0;
        gm_live = 1;
        rr_mode = RR_START;
        return;
    }
    if (rr_mode != RR_SELECT) return;
    s32 n = RDS16(NM_SEL_COUNT);
    if (n < 1 || n > 100) n = 1;
    // the order without the empty picks' places
    int w = 0;
    for (int i = 0; i < n; i++) {
        u16 id = RD16(NM_RR_LIST + 2u * (u32)i);
        if (i < 4 && id == 0xFFFF) continue;
        RD16(NM_RR_LIST + 2u * (u32)w) = id;
        RD8(NM_RR_TABLE_A + (u32)w) = RD8(NM_RR_TABLE_A + (u32)i);
        RD8(NM_RR_TABLE_B + (u32)w) = RD8(NM_RR_TABLE_B + (u32)i);
        w++;
    }
    if (rr_rebuild) {
        // after a challenge: the winner, who was in the ring, then everyone still to come, in order
        w = 1 + rr_kept;
        for (int i = rr_kept; i < rr_rest_n && w < 100; i++, w++) {
            RD16(NM_RR_LIST + 2u * (u32)w) = rr_rest[i];
            RD8(NM_RR_TABLE_A + (u32)w) = rr_rest_a[i];
            RD8(NM_RR_TABLE_B + (u32)w) = rr_rest_b[i];
        }
        if (w < 2) w = 2;
        // (the entrants still to come keep their numbers: the list's first to come, 1 + rr_kept, is
        // the one who'd have come next)
        rr_num_offset = rr_next_cap - (1 + rr_kept);
    } else {
        rr_num_offset = 0;
        if (w > rumble_size) w = rumble_size;
        w = rr_draw_field(w);
    }
    for (int i = w; i < 101; i++) RD16(NM_RR_LIST + 2u * (u32)i) = 0xFFFF;
    RDS32(NM_RR_COUNT_SRC) = w;
    rm_rumble_field_say(w);          // (2026-11-13, removal2: the field said, with someone removed or QA)
    rt_qa_field(w);                  // (rostertags, 2026-11-21: QA qa_tags only)
    // Everyone's match record (moves, attributes, the AI's), from the list: the step a Multi Play
    // match takes on the loading screen (func_80111158), which skips it for a one-pick select.
    plat_game_call(GF_BUILD_RECORDS, 0, 0, 0);
    // the empty slots: nobody on the line-up (their entrances aren't played)
    for (u32 s = 0; s < 4; s++)
        if (RD16(NM_SEL_REC + s * NM_SEL_REC_SIZE) == 0xFFFF) RDS32(NM_SLOT_PAD + 4u * s) = -1;
#ifdef NM_SIX
    // six or more in the ring: the six-man engine's Rumble set-up (rw_rumble_fill) put entrants 4 on in slots 4 on;
    // the arcade's starts them empty like 2 and 3 (they come in on the clock). (Six or more since 2026-11-06, the
    // 32-slot work: it was six exactly.)
    if (RDS32(SIX_MATCH_COUNT) >= 6)
        for (u32 s = 4; s < (u32)RDS32(SIX_MATCH_COUNT) && s < NM_SLOTS_CAP; s++) { RDS32(NM_SLOT_WRESTLER + 4u * s) = -1; RDS32(NM_SLOT_PAD + 4u * s) = -1; }
#endif
    rr_mode = RR_START;
}

// The match's set-up is done (func_80115000; it puts four in the ring, the next entrant the fifth,
// and everyone facing the one across): the Rumble as it starts here.
void arcade_rr_match_setup(void) {
    { extern void match_slots_setup(void); match_slots_setup(); }   // (any32: the extra slots' roles, any match type -- slots.c)
    // (a game-menu Rumble whose start emptied slots -- gm_start, W10 -- always gets the empty slots' set-up below: the
    //  game's own HUD read an empty slot as a Superstar and crashed when it didn't, 2026-11-07)
    if (rr_mode != RR_START && !(gm_live == 1 && rr_mode == RR_OFF)) return;
    if (!(RD32(NM_MATCH_FLAGS) & NM_MATCH_FLAGS_RUMBLE)) { rr_mode = RR_OFF; return; }
    int in[NM_SLOTS_CAP], n = 0, slots = nm_slots();
    for (int s = 0; s < slots; s++) if (!rr_absent(s)) in[n++] = s;
    RDS32(NM_RR_NEXT) = n;
    for (int k = 0; k < n; k++) RDS32(W(in[k])) = in[(k + 1) % n];
    // Two at the bell stand as a one-on-one match has them: in corners diagonally across the ring.
    // The player keeps his Rumble corner (top left in the match camera); the first entrant takes
    // the one across from him, the Rumble's corner for the fourth slot (the lower right
    // turnbuckle). Each turns to face the other the way func_80115000 does (func_8012EA9C).
    if (n == 2 && in[0] == 0 && in[1] == 1) {
        *(volatile float*)(W(1) + 0xF4) = (float)RDS16(NM_RR_SPOTS + 3u * 4u);
        *(volatile float*)(W(1) + 0xFC) = (float)RDS16(NM_RR_SPOTS + 3u * 4u + 2u);
        for (int k = 0; k < 2; k++)
            RDS32(W(k) + 0x104) = plat_game_call(GF_FACING, W(k) + 0xF4, W(k ^ 1) + 0xF4, 0);
    }
    for (int s = 0; s < slots; s++) {
        if (!rr_absent(s)) continue;
        RD8(W(s) + 0xE1) &= (u8)~0x10;   // not in the Rumble yet
        RD16(W(s) + 0x8C) |= 1;          // (out, for the match-over count)
        // whoever comes is the CPU's: the slot's pad, the match's CPU bit for it and the wrestler's
        // CPU flag (func_80118368 runs the AI, func_800F03F0, only for a wrestler with it), as
        // func_80115000 gives a CPU slot that starts the match in the ring
        RDS32(NM_SLOT_PAD + 4u * (u32)s) = 0;
        if (s < 4) RD32(NM_MATCH_FLAGS) |= 1u << s;   // (slots 4, 5: the computer's always; bits 4.. are others')
        RD8(W(s) + 0xE1) |= 0x08;
        rr_model[s] = 1;                 // the engine's stand-in model for a slot nobody's in
    }
    RD8(NM_SEL_CTX) = CONTEXT_RUMBLE;    // (the match is the Multi Play Royal Rumble's from here)
    rr_called = 0; rr_due = 0;
    // (an entrant called comes out after his countdown: the first is called that much before the
    // entry time, so one comes out every entry time from the bell)
    rr_last_call = (rr_rebuild ? -rr_time0 : 0) - RR_COUNTDOWN;
    rr_rebuild = 0;
    rr_player_out = 0;
    rr_restore_due = 0; rr_walk_in = 0; rr_walk_slot = -1; rr_walk_rule = 0;
    rr_mode = RR_LIVE;
}

// func_801199F0, the game's: who goes to a Royal Rumble slot next (an elimination asks it for
// the eliminated one's replacement). In the arcade's Rumble the answer is only yes when the clock
// has called an entrant.
u32 dbg_rr_kept_out;   // tests: eliminated men kept out, no entrant for their slot
s32 arcade_rr_entrant(s32 slot) {
    u32 flags = RD32(NM_MATCH_FLAGS);
    u32 w = W(slot);
    if (flags & 0x4000u) {
        int over = 0;
        if (((RD32(NM_RR_RULES) & 0x100000u) || (flags & 0x400000u)) && (RD16(W(0) + 0x8C) & 1)) over = 1;
        if (!over) {
            if (RD32(NM_MATCH_FLAGS) & 0x04000000u) {
                int i = RD8(w + 0xF0) + 1;
                int end = RD8(w + 0xF1);
                if (!(i < end)) return 0;
                if (i == end - 1) RD8(w + 0xE9) |= 4;
                RD8(w + 0xF0) = (u8)i;
                RD16(w + 0xD0) = RD16(NM_RR_LIST + 2u * (u32)i);
                RD8(w + 0xE0) = RD8(NM_RR_TABLE_A + (u32)i);
                RDS32(NM_RR_FIELD3DD8 + 0x1Cu * (u32)slot) = i;
                return 1;
            }
            s32 next = RDS32(NM_RR_NEXT);
            int arcade = rr_live();
            // (the arcade's: an entrant the clock has called, and never into the player's slot)
            if (next < RDS32(NM_RR_COUNT) && (!arcade || (rr_due > 0 && rr_cpu(slot)))) {
                RD8(w + 0xF0) = (u8)next;
                RD16(w + 0xD0) = RD16(NM_RR_LIST + 2u * (u32)next);
                RD8(w + 0xE0) = RD8(NM_RR_TABLE_A + (u32)next);
                RDS32(NM_RR_NEXT) = next + 1;
                RDS16(NM_RR_SLOT_B + 2u * (u32)slot) = (s16)RD8(NM_RR_TABLE_B + (u32)next);
                RDS32(NM_RR_FIELD3DD8 + 0x1Cu * (u32)slot) = next;
                if (arcade) {
                    rr_due--;
                    RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)slot) = -1;   // (in, not eliminated when the man he replaces was)
                }
                return 1;
            }
        }
    }
    RD8(w + 0xF0) = 0;
    RD16(w + 0xD0) = 0;
    RD8(w + 0xE0) = 0;
    // No entrant for an eliminated CPU Superstar's slot (none called yet, or none left): the game had already put the
    // slot back in the Rumble for the one to come -- with none, its eliminated man was back in the match, brawling on
    // with everyone (and, the field run out, for good: the Royal Rumble's "possessed" end, 2026-10-27). He's out again:
    // he leaves (arcade_rr_frame), and the slot takes the next entrant called.
    if (rr_live() && rr_cpu(slot) && RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)slot) >= 0) {
        RD8(w + 0xE1) &= (u8)~0x10u;
        RD16(w + 0x8C) |= 1u;
        dbg_rr_kept_out++;
    }
    return 0;
}

// (an eliminated Superstar nobody has been sent to replace)
static int rr_out(int s) {
    u32 w = W(s);
    return !rr_absent(s) && !(RD8(w + 0xE1) & 0x10) && (RD16(w + 0x8C) & 1) && !(RD8(w + 0xE9) & 1);
}
// (... a CPU one: his slot takes the next entrant; the player's slot never does)
static int rr_out_cpu(int s) { return rr_out(s) && rr_cpu(s); }
// (in the ring: in the Rumble and not out)
static int rr_in(int s) {
    u32 w = W(s);
    return !rr_absent(s) && (RD8(w + 0xE1) & 0x10) && !(RD16(w + 0x8C) & 1);
}
// ---- a man left at the mat's height past the apron (2026-11-15, gameplayfix: the playtest's item 5) ----------------------
// The game keeps a man marked in the ring or on the apron (+0x82, here W + 0x7A: 1 / 2) at the mat's height, 90, wherever
// he is. A move that carries him past the ropes and the apron without the game's own fall leaves him standing on nothing
// at that height outside the ring: Chris Jericho's tornado DDT, done the wrong way round, left him and Lex Luger there,
// Luger stuck a while (playtest 2026-11-15, shot 6) -- as qa_rr_out's runaway walkers were, 2026-11-06. (Which Jericho's
// move does it -- No Mercy's, WrestleMania 2000's or Revenge's import -- is for the QA scenarios to say: gameplayfix
// STATE.md.) Here, every match step: a man so placed for 30 steps while free (standing, walking, running, down, rising)
// is put down on the floor where he is -- height 0 and marked outside (4), as qa_rr_out puts a man out; the game's own
// count-out and the brawl outside go on from there. A man on the table (0x40), a ladder or the cage is never so marked.
#define VOID_EDGE  300.0f   // (past the apron's edge, 280: the ring list's apron walls)
#define VOID_STEPS 30
static u8 void_steps[NM_SLOTS_CAP];
u32 dbg_void_drops;                      // tests: men put down
static int void_free_state(s32 st) { return st == 1 || st == 2 || st == 3 || st == 0x31 || st == 0x32; }
// QA (ljround9, qa_area_trace): every man's place bits (+0x82) as they change, any match -- the game's log, "areatr": the
// slot, the bits before and after, his state (+0x14) and the one before (+0x1C), his animation file (+0xA6) and frame (+0xA4),
// the file's end level (+0x2EE) and mark frame (+0x2FC: read_slot_controls sets the place from the end level there), his
// place, his grab flags (+0x78), and the same of his man (+0x08). With 2 also each step a man is at the mat's height (y > 60)
// past the apron (300 out) with the ring's or the apron's bits (the float). Reads only.
static u16 qa_area_was[NM_SLOTS_CAP];
static s16 qa_area_an[NM_SLOTS_CAP];
static void qa_area_trace_step(void) {
    extern void removal_s(const char* s); extern void removal_num(s32 v); extern void removal_say(void);
    extern int match_slots_count(void);
    if (!qa_area_trace || RD16(NM_MATCH_FRAMES) == 0xFFFFu) return;
    int n = match_slots_count(); if (n < nm_slots()) n = nm_slots(); if (n > NM_SLOTS_CAP) n = NM_SLOTS_CAP;
    for (int s = 0; s < n; s++) {
        u32 r = W(s) - 8u;
        if (RDS16(r + 0xA0) == -1) { qa_area_was[s] = 0xFFFFu; continue; }
        u16 a = RD16(r + 0x82);
        float x = *(volatile float*)(r + 0xFC), y = *(volatile float*)(r + 0x100), z = *(volatile float*)(r + 0x104);
        {   // (a grapple move's files as each starts: the attacker 85 / the man 86 -- the file, its end level and mark frame)
            s16 an = RDS16(r + 0xA6); s32 st = RDS32(r + 0x14);
            if (an != qa_area_an[s] && (st == 85 || st == 86)) {
                removal_s("areatr move f "); removal_num((s32)RD16(NM_MATCH_FRAMES)); removal_s(" s "); removal_num(s); removal_s(" st "); removal_num(st);
                removal_s(" an "); removal_num(an); removal_s(" el "); removal_num(RD8(r + 0x2EE)); removal_s(" mk "); removal_num(RD8(r + 0x2FC));
                removal_s(" mv "); removal_num(RDS16(r + 0x30A)); removal_s(" en "); removal_num(RDS16(r + 0x308)); removal_s(" ow "); removal_num(RDS8(r + 0x304));
                removal_s(" id "); removal_num(RDS16(r + 0xA0)); { extern u32 dbg_place_kept; removal_s(" kept "); removal_num((s32)dbg_place_kept); }
                removal_s(" a "); removal_num((s32)a); removal_s(" x "); removal_num((s32)x); removal_s(" y "); removal_num((s32)y); removal_s(" z "); removal_num((s32)z); removal_say();
            }
            qa_area_an[s] = an;
        }
        float ax = x < 0.0f ? -x : x, az = z < 0.0f ? -z : z, d = ax > az ? ax : az;
        int fl = (qa_area_trace & 2) && y > 60.0f && d > 300.0f && (a & 3);
        if (a == qa_area_was[s] && !fl) continue;
        s32 t = RDS32(r + 0x08); u32 tr = (t >= 0 && t < NM_SLOTS_CAP) ? W(t) - 8u : r;
        removal_s(fl && a == qa_area_was[s] ? "areatr float f " : "areatr f "); removal_num((s32)RD16(NM_MATCH_FRAMES));
        removal_s(" s "); removal_num(s); removal_s(" a "); removal_num(qa_area_was[s] == 0xFFFFu ? -1 : (s32)qa_area_was[s]); removal_s(" > "); removal_num((s32)a);
        removal_s(" st "); removal_num(RDS32(r + 0x14)); removal_s(" pv "); removal_num(RDS32(r + 0x1C));
        removal_s(" an "); removal_num(RDS16(r + 0xA6)); removal_s(" fr "); removal_num(RDS16(r + 0xA4));
        removal_s(" el "); removal_num(RD8(r + 0x2EE)); removal_s(" mk "); removal_num(RD8(r + 0x2FC));
        removal_s(" x "); removal_num((s32)x); removal_s(" y "); removal_num((s32)y); removal_s(" z "); removal_num((s32)z);
        removal_s(" gf "); removal_num(RDS32(r + 0x78));
        removal_s(" | tg "); removal_num(t); removal_s(" st "); removal_num(RDS32(tr + 0x14)); removal_s(" an "); removal_num(RDS16(tr + 0xA6));
        removal_s(" el "); removal_num(RD8(tr + 0x2EE)); removal_s(" a "); removal_num((s32)RD16(tr + 0x82)); removal_s(" gf "); removal_num(RDS32(tr + 0x78));
        removal_s(" x "); removal_num((s32)*(volatile float*)(tr + 0xFC)); removal_s(" z "); removal_num((s32)*(volatile float*)(tr + 0x104));
        removal_say();
        qa_area_was[s] = a;
    }
}
// QA (ljround9, qa_outfight = a period in match frames): the same fight outside, in any match -- every period, with slots
// qa_of_a and qa_of_b both on their feet (standing / walking), the two are put on the floor at the next of six spots, 55
// apart along the way out from the ring, facing each other, their man each other: by the barricade (the man nearer it) and
// by the apron's edge (the man nearer it) on the south side, the aisle's foot, the same two on the west side, a corner.
// Their computer players fight from there (with qa_fslot, the attacker's grapple moves forced to one entry: the same move).
static u32 qa_of_n;
static void qa_outfight_step(void) {
    if (!qa_outfight || RD16(NM_MATCH_FRAMES) == 0xFFFFu || RDS32(0x801591FCu) != 2) return;
    u32 mf = RD16(NM_MATCH_FRAMES);
    static u32 last_p = 0xFFFFFFFFu;               // (the period last put: a step is more than one frame, so by period, not frame)
    u32 per = mf / (u32)qa_outfight;
    if (mf < 300u || per == last_p) return;
    u32 ra = W(qa_of_a) - 8u, rb = W(qa_of_b) - 8u;
    s32 sa = RDS32(ra + 0x14), sb = RDS32(rb + 0x14);
    if (RDS16(ra + 0xA0) == -1 || RDS16(rb + 0xA0) == -1 || (!qa_of_stage && sa != 1 && sa != 2) || (sb != 1 && sb != 2)) return;
    //                           A x,   A z,   B x,   B z, A faces, B faces
    static const s16 spot[6][6] = { {   40, -400,   40, -455, 2048,    0 },     // (south: the man by the barricade)
                                    {   40, -400,   40, -345,    0, 2048 },     // (south: the man by the apron's edge)
                                    {  150,  470,  150,  525,    0, 2048 },     // (the aisle's foot)
                                    { -400,   60, -455,   60, 3072, 1024 },     // (west: by the barricade)
                                    { -400,   60, -345,   60, 1024, 3072 },     // (west: by the apron's edge)
                                    {  400, -400,  439, -439, 1536, 3584 } };   // (a corner, the man farther out)
    const s16* p = spot[qa_of_n % 6u];
    if (qa_of_stage) {   // (ljround10: the second man alone, put on the ramp's middle or on the stage's front, by turns; facing the ring)
        extern int ringstage_front(void); extern float ringstage_ground_at(float x, float z);
        float front = (float)ringstage_front(), z = (qa_of_n & 1) ? front + 80.0f : 530.0f + 0.5f * (front - 530.0f), y = ringstage_ground_at(0.0f, z);
        *(volatile float*)(rb + 0xFC) = 0.0f; *(volatile float*)(rb + 0x100) = 0.0f; *(volatile float*)(rb + 0x104) = z;
        for (u32 q = 0x120; q + 8u <= 0x15C; q += 12u) { *(volatile float*)(rb + q) = 0.0f; *(volatile float*)(rb + q + 4u) = 0.0f; *(volatile float*)(rb + q + 8u) = z; }
        RD16(rb + 0x82) = (u16)((RD16(rb + 0x82) & 0xF800u) | 4u);
        RDS32(rb + 0x10C) = 2048;
        extern void removal_s(const char* s); extern void removal_num(s32 v); extern void removal_say(void);
        removal_s("areatr put f "); removal_num((s32)mf); removal_s(qa_of_n & 1 ? " the stage" : " the ramp"); removal_s(" z "); removal_num((s32)z);
        removal_s(" ground "); removal_num((s32)y); removal_s(" slot "); removal_num(qa_of_b); removal_say();
        qa_of_n++; last_p = per;
        return;
    }
    for (int k = 0; k < 2; k++) {
        u32 r = k ? rb : ra;
        float x = (float)p[k ? 2 : 0], z = (float)p[k ? 3 : 1];
        *(volatile float*)(r + 0xFC) = x; *(volatile float*)(r + 0x100) = 0.0f; *(volatile float*)(r + 0x104) = z;
        for (u32 q = 0x120; q + 8u <= 0x15C; q += 12u) { *(volatile float*)(r + q) = x; *(volatile float*)(r + q + 4u) = 0.0f; *(volatile float*)(r + q + 8u) = z; }
        RD16(r + 0x82) = (u16)((RD16(r + 0x82) & 0xF800u) | 4u);
        RDS32(r + 0x10C) = p[k ? 5 : 4];
        RDS32(r + 0x08) = k ? qa_of_a : qa_of_b;
    }
    extern void removal_s(const char* s); extern void removal_num(s32 v); extern void removal_say(void);
    removal_s("areatr put f "); removal_num((s32)mf); removal_s(" spot "); removal_num((s32)(qa_of_n % 6u)); removal_s(" slots "); removal_num(qa_of_a); removal_s(" "); removal_num(qa_of_b); removal_say();
    qa_of_n++; last_p = per;
}
void ring_void_guard(void) {
    qa_area_trace_step();
    qa_outfight_step();
    if (RD16(NM_MATCH_FRAMES) == 0xFFFFu || qa_void_off) return;
    for (int s = 0, n = nm_slots(); s < n; s++) {
        u32 w = W(s);
        float x = *(volatile float*)(w + 0xF4), y = *(volatile float*)(w + 0xF8), z = *(volatile float*)(w + 0xFC);
        u16 area = RD16(w + 0x7A) & 0x7FFu;
        int off = !rr_absent(s) && (area == 1 || area == 2) && y > 60.0f &&
                  (x > VOID_EDGE || x < -VOID_EDGE || z > VOID_EDGE || z < -VOID_EDGE) && void_free_state(RDS32(w + 0x0C));
        if (!off) { void_steps[s] = 0; continue; }
        if (++void_steps[s] < VOID_STEPS) continue;
        void_steps[s] = 0;
        *(volatile float*)(w + 0xF8) = 0.0f; *(volatile float*)(w + 0x11C) = 0.0f;
        for (u32 k = 0x124; k <= 0x154; k += 12) *(volatile float*)(w + k + 4) = 0.0f;
        RD16(w + 0x7A) = 4;
        dbg_void_drops++;
    }
}
// He's gone: the slot is emptied as a singles match has it (no Superstar, the camera not on it,
// the engine not drawing the model); whoever faced him turns to someone else.
static void rr_leave(int s) {
    u32 w = W(s);
    rr_model[s] = RD16(w + 0x98);
    RDS16(w + 0x98) = -1;
    RDS16(NM_CAM_IDS + 2u * (u32)s) = -1;
    plat_game_call(GF_PLAYER_SHOW, (u32)s, 0, 0);
}
// Back to an empty slot: the entrant's (the game's entrance takes it from here: the Superstar
// waits outside like an eliminated one, func_8011D400 sees him on the way and starts the walk).
static void rr_return(int s) {
    u32 w = W(s);
    RD16(w + 0x98) = rr_model[s];
    RDS32(w + 0x0C) = 1; RDS32(w + 0x10) = 0;
    RD16(w + 0x7A) = 4;
    RD16(w + 0x8C) |= 1;
}

// Full power: the spirit meter full (SPECIAL, as func_8011D05C makes it at 100), stamina and its cap
// (+0xAA / +0xAC, real offsets) as a fresh entrant's, the five body parts' health (+0x2C4..) 50 each,
// the knock-down count (+0xBE, func_8013FEF4's) none. (W: the struct - 8)
static void full_power(int s) {
    u32 w = W(s);
    RDS16(w + 0xA2) = 255; RDS16(w + 0xA4) = 255;
    for (u32 k = 0; k < 5; k++) *(volatile float*)(w + 0x2BCu + 4u * k) = 50.0f;
    RDS16(w + 0xB6) = 0;
    RD8(w + 0xE6) &= (u8)~1u;                    // (DANGER over)
    if (RD8(w + 0xE1) & 1) RDS16(w + 0xA6) = 99;  // (as func_8011D05C: no SPECIAL for these)
    else { RDS16(w + 0xA6) = 100; RDS16(w + 0xA8) = 600; RD8(w + 0xE6) |= 2; }
}
u32 dbg_power_fills;                   // tests: Start's refills
// The player's slot (the arcade's Rumble: the one slot a controller drives)
static int rr_player_slot(void) {
    for (int s = 0, slots = nm_slots(); s < slots; s++) if (!rr_absent(s) && !rr_cpu(s)) return s;
    return -1;
}
// While the player is out with a continue to come (or free play's moment), his slot counts as in for
// the match's own "is it over?" (func_8011723C): the Rumble isn't over round him.
static int rr_holding(void) { return rr_live() && rr_player_out; }
// He's back: in the Rumble again (out of the match no more, the clock's elimination time gone, in
// the HUD's count), at full power. Down in the ring (pinned), he gets up the game's own way; outside,
// the computer takes him back in under the ropes (rr_walk_in_step) and hands him back.
#define WALK_IN_FRAMES (60 * 12)
u32 dbg_back_in;                       // tests: times he was put back
u32 qa_put_outs;                       // (QA, qa_reg 8192: times the driver gave the player his controller back, out)
static void rr_walk_in_step(void);
static int rr_back_slot = -1;           // (Regular, phase 3: the player put back -- his own slot; -1: the first, as Arcade's)
static void rr_back_in(void) {
    flight_event(FL_BACK_IN, (u32)rr_walk_slot, 0);
    int s = rr_back_slot >= 0 ? rr_back_slot : rr_player_slot();
    rr_back_slot = -1;
    rr_player_out = 0;
    for (int pl = 0; pl < 2; pl++) match_lost[pl] = 0;
    if (s < 0) return;
    u32 w = W(s);
    RD16(w + 0x8C) &= (u16)~0x33u;         // (out, out and standing, and any elimination still to be counted)
    RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)s) = -1;
    RD8(w + 0xE1) |= 0x10;
    full_power(s);
    dbg_back_in++;
    // Till he's on his feet in the ring (rr_walk_in_step): the Rumble's count-out rule -- 0x400:
    // standing outside is out, func_800E5EB8 -- off, or wherever he lands outside (where he is now,
    // or thrown out of a move he was in) would have him out again at once (over the top still is
    // out, a move's); and while outside, the computer drives him back in under the ropes.
    rr_walk_rule = RD32(NM_RR_RULES) & 0x400u;
    RD32(NM_RR_RULES) &= ~0x400u;
    rr_walk_slot = s; rr_walk_in = WALK_IN_FRAMES;
    rr_walk_in_step();
}
static void rr_walk_cpu(int s, int on) {
    u32 w = W(s);
    if (on) { RD8(w + 0xE1) |= 0x08; if (s < 4) RD32(NM_MATCH_FLAGS) |= 1u << s; }
    else { RD8(w + 0xE1) &= (u8)~0x08; if (s < 4) RD32(NM_MATCH_FLAGS) &= ~(1u << s); }
}
static void rr_walk_in_step(void) {
    if (rr_walk_slot < 0) return;
    u32 w = W(rr_walk_slot);
    int outside = (RD16(w + 0x7A) & 4) != 0;
    // on his feet in the ring, out again (over the top), or out of time: his controller, and the rule
    if (--rr_walk_in <= 0 || (RD16(w + 0x8C) & 1) || (!outside && RDS32(w + 0x0C) == 1)) {
        rr_walk_cpu(rr_walk_slot, 0);
        RD32(NM_RR_RULES) |= rr_walk_rule; rr_walk_rule = 0;
        rr_walk_slot = -1; rr_walk_in = 0;
        return;
    }
    rr_walk_cpu(rr_walk_slot, outside);
}
// func_8011723C (ovl_e), the game's: slot is eliminated (out of the match, +0x94 bit 0) -> 1: the
// match is over. For the arcade's Rumble the player's slot, while held (rr_holding), counts as in.
s32 arcade_elim_check(s32 slot) {
    u32 flags = RD32(NM_MATCH_FLAGS);
    // (RUMBLE CONTINUE off, W9: nothing held -- the player's elimination is the game's own)
    // (Regular has no paying players: a human slot is held whenever the continue is on -- W9/W10)
    int slots = nm_slots(), hold = rr_cont_on && (rr_holding() || (rr_live() && !rr_cpu(slot) && (nm_arcade() ? active_any() : 1)));
    RD16(W(slot) + 0x8C) |= 1;
    flight_event(FL_ELIM, (u32)slot, (u32)hold | (u32)RD16(W(slot) + 0x98) << 16);
    if (flags & 0x3B000u) return 1;
    if (!(flags & 0x4000u)) return 0;
    int b = (RD32(NM_RR_RULES) & 0x100000u) || (flags & 0x400000u);
    if (b && slot == 0) return 1;
    int in = 0;
    if (!(flags & 0x04000000u)) {
        if (RDS32(NM_RR_NEXT) < RDS32(NM_RR_COUNT)) return 0;
        for (int s = 0; s < slots; s++) in += !(RD16(W(s) + 0x8C) & 1) || (hold && !rr_cpu(s) && !rr_absent(s));
    } else {
        int k = 0;
        for (int s = 0; s < slots; s++) k += (RD8(W(s) + 0xE9) & 4) != 0;
        if (k < slots - 1) return 0;
        for (int s = 0; s < slots; s++)
            if (!(RD16(W(s) + 0x8C) & 1) || (RD8(W(s) + 0xE9) & 1) || RDS32(W(s) + 0x0C) == 0xA3 ||
                (hold && !rr_cpu(s) && !rr_absent(s))) in++;
        if (!(RD8(W(slot) + 0xE9) & 4)) return 0;
    }
    if (in < 2) flight_event(FL_MATCH_OVER, (u32)slot, (u32)in);
    return in < 2;
}

// Each frame of the match, before the game's Rumble entrance manager (func_8011D400) runs.
void arcade_rr_frame(void) {
    if (!rr_live()) {
        if (rr_mode == RR_LIVE && RDS32(NM_MODE) != -2) rr_mode = RR_OFF;
        return;
    }
    // the player is out: where the Rumble stands, kept up to date for a continue
    if (rr_player_out) rumble_capture();
    s32 t = rr_clock();
    int in = 0, coming = 0, slots = nm_slots();
    for (int s = 0; s < slots; s++) { if (rr_in(s)) in++; if (!rr_absent(s) && (RD8(W(s) + 0xE9) & 1)) coming++; }
    // the clock calls the next entrant; and anyone left alone in the ring doesn't wait for him
    if (RDS32(NM_RR_NEXT) + rr_due < RDS32(NM_RR_COUNT) &&
        (t - rr_last_call >= rr_interval() || (in <= 1 && rr_due == 0 && coming == 0))) {
        rr_called++; rr_due++;
        rr_last_call = t;
    }
    // an entrant called goes to a free slot: one whose Superstar was eliminated, or an empty one
    while (rr_due > 0) {
        int pick = -1;
        for (int s = 0; s < slots && pick < 0; s++) if (rr_out_cpu(s)) pick = s;
        for (int s = 0; s < slots && pick < 0; s++) if (rr_absent(s) && rr_cpu(s)) pick = s;
        if (pick < 0) break;                        // (the ring full: the next elimination takes him)
        if (rr_absent(pick)) rr_return(pick);
        // (in the Rumble only if an entrant is given: a slot marked so with none -- the player out, his continue
        // still to come -- was its eliminated Superstar back in the match, the Rumble's end the eliminated
        // brawling on with everyone, 2026-10-27; and the entrant's in, not eliminated when the one he replaces was)
        u8 e1 = RD8(W(pick) + 0xE1);
        RD8(W(pick) + 0xE1) = e1 | 0x10;
        if (!arcade_rr_entrant(pick)) { RD8(W(pick) + 0xE1) = e1; break; }
        RD8(W(pick) + 0xE9) |= 1;
        RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)pick) = -1;
    }
    // the player's continue (or free play's moment) is through: back in the Rumble where he is
    if (rr_restore_due > 0 && --rr_restore_due == 0) rr_back_in();
    rr_walk_in_step();
    // an eliminated Superstar who has landed outside, with nobody coming for his slot, leaves --
    // not the player: he stays where he is while his CONTINUE? countdown runs (his slot is held, the
    // Rumble goes on round him), and is back in it at full power with a continue
    for (int s = 0; s < slots; s++) {
        if (!rr_out(s) || !rr_cpu(s)) continue;
        s32 ph = RDS32(W(s) + 0x0C);
        if ((ph == 1 || ph == 0x31) && (RD16(W(s) + 0x7A) & 4)) rr_leave(s);
    }
    // nobody faces an empty slot
    for (int s = 0; s < slots; s++) {
        if (rr_absent(s)) continue;
        s32 tg = RDS32(W(s));
        if (tg >= 0 && tg < slots && (tg == s || rr_absent(tg))) plat_game_call(GF_RETARGET, NM_TB + WRESTLER_SIZE * (u32)s, 1, 0);
    }
}

// The match HUD (func_800DEDE0) has updated: an empty slot has no panel (its frame, bar and name:
// three of the HUD's sprite objects, each pointing at its instance, whose flag 0x80 hides it), nor
// has a slot whose entrant is on the way but not loaded yet (it would show who was there before).
static const u32 rr_hud_obj[3] = { SIXA_80154668, SIXA_801556F0, SIXA_80155B50 };
#define HUD_OBJ_SIZE 0xDCu
static void rr_hud_meters(void);
void arcade_rr_hud(void) {
    { extern void match_slots_hud(void); match_slots_hud(); }   // (any32: an extra slot has no panel -- slots.c)
    rr_hud_meters();                              // (any Royal Rumble with more than six in the ring, both modes)
    if (!rr_live()) return;
    for (int s = 0, slots = nm_slots(); s < slots; s++) {
        u32 w = W(s);
        u16 who = RD16(w + 0xD0);
        int unloaded = ((RD8(w + 0xE9) & 1) || RDS32(w + 0x0C) == 0xA3) &&
                       (RD16(w + 0x98) != who || RD16(NM_CAM_IDS + 2u * (u32)s) != who);
        if (!rr_absent(s) && !unloaded) continue;
        for (int k = 0; k < 3; k++) {
            u32 inst = RD32(rr_hud_obj[k] + HUD_OBJ_SIZE * (u32)s + 0x14);
            if (GAME_PTR(inst)) RD32(inst) |= 0x80u;
        }
    }
}

// ---- The power meters of a Royal Rumble with more than six in the ring (2026-11-15, gameplayfix; the user's spec) ----
// The engine gives every slot a panel (meter, frame, name) at the place its HUD record's byte 0 names (& 7) in six-entry
// tables (six-engine rw/r_hud.c: 0 top left, 1 bottom left, 2 top right, 3 bottom right, 4 top centre, 5 bottom centre),
// and slot k's byte is k: past six they read past the tables -- the uneven grid of up to nine panels over the action, and
// slots 8 and 9 on top of 0 and 1 (playtest 2026-11-15, shots 6 and 8). The user: at most 8 meters; the others in the ring
// get none; when a wrestler with a meter is eliminated, the next one in the queue (by entry order) takes it; never more
// over the action than before. Here: at most RR_METERS meters (6: the places the engine has -- two more need wider
// tables in the engine, see gameplayfix NOTES), each on a place of its own; a player's man always has one; the rest go
// by the Rumble's entry order (the order list's index, +0xF0). A meter given to a slot moves the slot's place (byte 0)
// and puts its three still parts there the way the HUD's set-up does (place_player_hud_parts, func_800DEAB0); the moving
// ones (the bar) follow byte 0 by themselves every frame. A slot without one has its panel hidden as arcade_rr_hud hides
// an empty slot's (its frame, bar and name: rr_hud_obj). Six or fewer in the match: untouched, the engine's own HUD.
#ifdef NM_SIX
#define RR_METERS        6
#define HUD_REC          0x8016B9F0u      // u8 place, ..., +8 / +0xA the place's x / y (0xC a slot; stock 0x80154638)
#define HUD_LAYOUT       0x8016DE70u      // u8 a slot: the panel's picture set (0x14: "pick again")
#define HUD_BONE_SIZE    0xDCu
#define HUD_PART_SIZE    0x34u
#define HUD_T_B78        0x807847D8u      // rw_hud_B78.. (s16 x 6 each; the blob's tables, place_player_hud_parts reads them)
#define HUD_T_BD0        0x807847F0u
#define HUD_T_BD8        0x807847FCu
#define HUD_T_BE0        0x80784808u
#define HUD_T_BE8        0x80784814u
extern void func_800DC85C(u32 bone, u32 part, u32 sprite);   // place_hud_quad_points (ovl_e)
static s8 rrm_place[NM_SLOTS_CAP];        // each slot's meter place (-1: none)
static u16 rrm_who[NM_SLOTS_CAP];         // ... and the Superstar it was given to (a new entrant in the slot: a new choice)
static int rrm_on;
static struct { s16 x, y; float ax, ay, sx, sy; u8 r, g, b, a; } rrm_sprite __attribute__((aligned(8)));
static s16 hud_t(u32 table, int place) { return RDS16(table + 2u * (u32)place); }
// (the blob's tables where this build expects them: rw/r_hud.c's values -- the left / right / centre x 48 / 335 / 192 and the
//  rows' y 20 / 203. A six-man engine whose blob has moved them: no meters of ours, the engine's HUD as it is.)
static int hud_tables_here(void) {
    static const s16 x[6] = { 48, 48, 335, 335, 192, 192 }, y[6] = { 20, 203, 20, 203, 20, 203 };
    for (int p = 0; p < 6; p++) if (hud_t(HUD_T_BD0, p) != x[p] || hud_t(HUD_T_BD8, p) != y[p]) return 0;
    return 1;
}
// the slot's three still parts at place p, as place_player_hud_parts puts slot p's
static void rrm_put(int s, int p) {
    u32 rec = HUD_REC + 0xCu * (u32)s, bone = HUD_BONE_SIZE * (u32)s, part = HUD_PART_SIZE * (u32)s;
    RD8(rec) = (u8)((RD8(rec) & 0xC0u) | (u32)p);
    RDS16(rec + 8) = hud_t(HUD_T_BD0, p);
    RDS16(rec + 0xA) = hud_t(HUD_T_BD8, p);
    rrm_sprite.ax = rrm_sprite.ay = 0.0f;
    rrm_sprite.r = rrm_sprite.g = rrm_sprite.b = rrm_sprite.a = 0xFF;
    rrm_sprite.x = hud_t(HUD_T_BD0, p); rrm_sprite.y = hud_t(HUD_T_BD8, p); rrm_sprite.sx = 1.0f; rrm_sprite.sy = 1.0f;
    func_800DC85C(SIXA_80154668 + bone, 0x8016D7F0u + part, (u32)&rrm_sprite);
    rrm_sprite.x = hud_t(HUD_T_BE0, p); rrm_sprite.y = hud_t(HUD_T_BE8, p); rrm_sprite.sx = 0.95f; rrm_sprite.sy = 0.6f;
    func_800DC85C(SIXA_80155B50 + bone, 0x80176F10u + part, (u32)&rrm_sprite);
    RD8(HUD_LAYOUT + (u32)s) = 0x14;      // (its pictures picked again: a right-hand place has its own)
    rrm_sprite.x = hud_t(HUD_T_B78, p); rrm_sprite.y = hud_t(HUD_T_BD8, p); rrm_sprite.sx = 2.0f; rrm_sprite.sy = 1.0f;
    func_800DC85C(0x80170E90u + bone, 0x80172A10u + part, (u32)&rrm_sprite);
}
// a slot that may have a meter: someone there, loaded, not eliminated
static int rrm_wants(int s) {
    u32 w = W(s);
    if (rr_absent(s) || rr_out(s)) return 0;
    u16 who = RD16(w + 0xD0);
    if (((RD8(w + 0xE9) & 1) || RDS32(w + 0x0C) == 0xA3) && (RD16(w + 0x98) != who || RD16(NM_CAM_IDS + 2u * (u32)s) != who)) return 0;
    return 1;
}
// who comes first for a free meter: a player's man, then the earliest in the Rumble's order
static int rrm_before(int a, int b) {
    int ha = !rr_cpu(a), hb = !rr_cpu(b);
    if (ha != hb) return ha;
    return RD8(W(a) + 0xF0) < RD8(W(b) + 0xF0);
}
static void rr_hud_meters(void) {
    int slots = nm_slots();
    int live = RDS32(NM_MODE) == -2 && (RD32(NM_MATCH_FLAGS) & NM_MATCH_FLAGS_RUMBLE) && RD16(NM_MATCH_FRAMES) != 0xFFFF &&
               slots > NM_SLOTS_MAX && hud_tables_here();
    if (!live) { rrm_on = 0; return; }
    if (!rrm_on) {                        // (a new match: the set-up has put slot k at place k)
        for (int s = 0; s < NM_SLOTS_CAP; s++) { rrm_place[s] = -1; rrm_who[s] = 0; }
        rrm_on = 1;
    }
    u8 used[RR_METERS] = { 0 };
    // a meter is given back when its man is eliminated or gone, or another man has the slot
    for (int s = 0; s < slots; s++) {
        int p = rrm_place[s];
        if (p < 0) continue;
        if (!rrm_wants(s) || RD16(W(s) + 0xD0) != rrm_who[s]) { rrm_place[s] = -1; continue; }
        used[p] = 1;
        if ((RD8(HUD_REC + 0xCu * (u32)s) & 7u) != (u32)p) rrm_put(s, p);   // (the HUD set up again: put back)
    }
    for (;;) {                            // free meters to those waiting, the first in the queue first
        int best = -1;
        for (int s = 0; s < slots; s++)
            if (rrm_place[s] < 0 && rrm_wants(s) && (best < 0 || rrm_before(s, best))) best = s;
        if (best < 0) break;
        int p = 0;
        while (p < RR_METERS && used[p]) p++;
        if (p == RR_METERS) {             // none free: a player's man takes the last-in CPU man's
            if (rr_cpu(best)) break;
            int last = -1;
            for (int s = 0; s < slots; s++)
                if (rrm_place[s] >= 0 && rr_cpu(s) && (last < 0 || rrm_before(last, s))) last = s;
            if (last < 0) break;
            p = rrm_place[last];
            rrm_place[last] = -1;
        }
        used[p] = 1;
        rrm_place[best] = (s8)p;
        rrm_who[best] = RD16(W(best) + 0xD0);
        rrm_put(best, p);
    }
    // the rest: no panel at all
    for (int s = 0; s < slots; s++) {
        if (rrm_place[s] >= 0) continue;
        for (int k = 0; k < 3; k++) {
            u32 inst = RD32(rr_hud_obj[k] + HUD_BONE_SIZE * (u32)s + 0x14);
            if (GAME_PTR(inst)) RD32(inst) |= 0x80u;
        }
    }
}
#else
static void rr_hud_meters(void) {}
#endif

// A challenge stops the Royal Rumble: what it stands at, for the winner to go on with
// (rr_open(1)): the CPU Superstars in the ring, then everyone still to come, in order.
static void rr_keep(int li, int n) {
    if (li < 0 || li >= n || rr_rest_n >= RR_MAX) return;
    u16 id = RD16(NM_RR_LIST + 2u * (u32)li);
    if (id == 0xFFFF || id == 0) return;
    rr_rest[rr_rest_n] = id;
    rr_rest_a[rr_rest_n] = RD8(NM_RR_TABLE_A + (u32)li);
    rr_rest_b[rr_rest_n] = RD8(NM_RR_TABLE_B + (u32)li);
    rr_rest_n++;
}
static void rumble_capture(void) {
    s32 n = RDS32(NM_RR_COUNT), next = RDS32(NM_RR_NEXT);
    if (n <= 0 || n > 100 || next < 0 || next > n) return;
    rr_rest_n = 0;
    for (int s = 0, slots = nm_slots(); s < slots; s++)
        if (rr_cpu(s) && (rr_in(s) || (!rr_absent(s) && (RD8(W(s) + 0xE9) & 1)))) {
            int li = (RD8(W(s) + 0xE9) & 1) ? RD8(W(s) + 0xF0) : RD8(W(s) + 0xF0);
            rr_keep(li, n);
        }
    rr_kept = rr_rest_n > 3 ? 3 : rr_rest_n;
    rr_next_cap = next;
    for (int i = next; i < n; i++) rr_keep(i, n);
    s32 waited = rr_clock() - rr_last_call;
    rr_time0 = waited < 0 ? 0 : waited;
}

// ---- Versus PLAYER page: two players, both in -----------------------------------------------------
// The page has eight rows; each carries the option it shows (+0x61, one numbering for every match
// type) and a hidden flag (+0x4B). Singles: 0 1P vs 2P, 1 1P vs CPU, 2 CPU vs 1P, 3 Watch.
// Triple Threat 4..10: 1P/2P/3P, 1P/2P/CPU, 1P/CPU/2P, 1P/CPU/CPU, CPU/1P/CPU, CPU/CPU/1P, Watch.
// Handicap 11..16: 1P&2P/3P, 1P&2P/CPU, 1P&CPU/2P, 1P&CPU/CPU, CPU&CPU/1P, Watch. Tag 17..24:
// 1P&2P/3P&4P, 1P&2P/3P&CPU, 1P&2P/CPU&CPU, CPU&CPU/1P&2P, 1P&CPU/2P&CPU, 1P&CPU/CPU&CPU,
// CPU&CPU/1P&CPU, Watch. The arcade keeps the ones with 1P and 2P and nobody else's controller:
// the rows are re-dealt with those, the rest hidden. When one line-up is all there is (1P vs 2P,
// 1P vs 2P vs CPU), the page chooses it itself and the menus go on to ARENA (going back from ARENA
// goes on to MATCH).
#define PLAYER_ROWS 8
#define ROW_SIZE    0x88
// (one of each line-up: a mirror or re-ordering of the same sides isn't offered twice)
static int versus_option(int id) { return id == 0 || id == 5 || id == 12 || id == 13 || id == 19 || id == 21; }
// One player's Versus (the championship list's row): his against the CPU -- 1P vs CPU, 1P / CPU / CPU, 1P & CPU vs CPU,
// CPU & CPU vs 1P (the two sides of a handicap match), 1P & CPU vs CPU & CPU. (Watch, and the CPU-first mirrors, not.)
static int solo_option(int id) { return id == 1 || id == 7 || id == 14 || id == 15 || id == 22; }
static int vs_solo(void) { return sp_versus && game_mode == GM_STORY; }
static int players_from;          // the screen before the PLAYER page
static int players_done;
static int players_auto;          // (the page is one line-up: the arcade passes it, the players' A / B kept off it)
int dbg_no_players;
static void players_restore(void);
static void players_page(void) {
    if (dbg_no_players || !nm_arcade()) return;   // (Arcade only, 2026-11-07: Regular's PLAYER page keeps all its line-ups)
    if (!in_menus()) { players_done = 0; return; }
    s16 scr = RDS16(NM_SCREEN);
    if (scr != SCR_PLAYERS) { players_done = 0; players_auto = 0; players_from = scr; return; }
    u32 rec = menu_record(SCR_PLAYERS), items = RD32(rec + 0x14);
    if (!GAME_PTR(items)) return;
    int ids[PLAYER_ROWS], n = 0, keep[PLAYER_ROWS], k = 0;
    for (int i = 0; i < PLAYER_ROWS; i++) {
        u32 row = items + (u32)i * ROW_SIZE;
        if (RD8(row + 0x4B) || RD8(row + 0x61) == 0xFF) continue;
        ids[n++] = RD8(row + 0x61);
    }
    if (n == 0) return;
    int solo = vs_solo();
    for (int i = 0; i < n; i++) if (solo ? solo_option(ids[i]) : versus_option(ids[i])) keep[k++] = ids[i];
    if (k == 0) return;                               // (not a page this knows: left alone)
    if (k != n) {
        int cur = RD8(rec + 5), cur_id = cur < PLAYER_ROWS ? RD8(items + (u32)cur * ROW_SIZE + 0x61) : -1;
        for (int i = 0; i < PLAYER_ROWS; i++) {
            u32 row = items + (u32)i * ROW_SIZE;
            if (i < k) {
                RD8(row + 0x61) = (u8)keep[i];
            } else {
                RD8(row + 0x4B) = 1; RD8(row + 0x56) = 0; RD8(row + 0x60) = 0xFF; RD8(row + 0x61) = 0xFF;
            }
        }
        RD8(rec + 5) = 0;
        for (int i = 0; i < k; i++) if (keep[i] == cur_id) RD8(rec + 5) = (u8)i;
    }
    players_auto = k == 1;
    if (k == 1 && !players_done && scr_stable >= 2 && !menus_loading() && inject_frames == 0) {
        players_done = 1;
        if (players_from == SCR_ARENA) menu_change(SCR_EXHIBITION);   // (L from ARENA: on back to MATCH)
        else { players_restore(); menu_confirm(SCR_PLAYERS, -1); }     // 1P vs 2P: on to ARENA
        // (the page's own rows back first, the cursor on the line-up's: the game takes the choice by its row -- one
        // player's 1P vs CPU, re-dealt onto row 0, went as row 0's 1P vs 2P, and the select waited for player 2's pick)
    }
}

// The game takes the choice by its row (the cursor), so when a player confirms, the page gets its
// own rows back, the cursor on the row of the option chosen; the menus move on to the next page in
// the same frame, so the restored page is never drawn.
static const u8 option_first[4] = { 0, 4, 11, 17 }, option_last[4] = { 3, 10, 16, 24 };
static void players_restore(void) {
    if (!nm_arcade()) return;                      // (Arcade only: the page is re-dealt only there)
    u32 rec = menu_record(SCR_PLAYERS), items = RD32(rec + 0x14);
    if (!GAME_PTR(items)) return;
    int cur = RD8(rec + 5);
    if (cur >= PLAYER_ROWS) return;
    int id = RD8(items + (u32)cur * ROW_SIZE + 0x61);
    int g = -1;
    for (int i = 0; i < 4; i++) if (id >= option_first[i] && id <= option_last[i]) g = i;
    if (g < 0) return;
    for (int i = 0; i < PLAYER_ROWS; i++) {
        u32 row = items + (u32)i * ROW_SIZE;
        int o = option_first[g] + i;
        if (o <= option_last[g]) { RD8(row + 0x4B) = 0; RD8(row + 0x56) = 0xFF; RD8(row + 0x60) = 7; RD8(row + 0x61) = (u8)o; }
        else { RD8(row + 0x4B) = 1; RD8(row + 0x56) = 0; RD8(row + 0x60) = 0xFF; RD8(row + 0x61) = 0xFF; }
    }
    RD8(rec + 5) = (u8)(id - option_first[g]);
}

// ---- the Multi Play sub-header reads "versus" -----------------------------------------------------
// The red italic "exhibition" under MULTI PLAY is a CI4 texture the menus load into their heap. The
// same lettering re-cut from the game's other sub-headers ("pay per view", "guest referee") spells
// "versus"; it is written over the loaded texture, so the game draws its own header with it.
#include "versus_tex.h"
#define HDR_WORDS (sizeof(hdr_versus) / 4)
static u32 hdr_word(const unsigned char* b, u32 i) {
    return ((u32)b[i * 4] << 24) | ((u32)b[i * 4 + 1] << 16) | ((u32)b[i * 4 + 2] << 8) | b[i * 4 + 3];
}
static int hdr_is(u32 a, const unsigned char* b) {
    if (RD32(a) != hdr_word(b, 0) || RD32(a + 0x40) != hdr_word(b, 0x10)) return 0;
    for (u32 i = 0; i < HDR_WORDS; i++) if (RD32(a + i * 4) != hdr_word(b, i)) return 0;
    return 1;
}
static u32 hdr_addr = 0x801C0800u;
static int hdr_scan_screen = -1;
static void header_versus(void) {
    if (!nm_arcade()) return;                      // (Arcade only, 2026-11-07: Regular's sub-header stays "exhibition")
    if (!title_loaded() || RDS32(NM_MODE) != 0) { hdr_scan_screen = -1; return; }
    int scr = RDS16(NM_SCREEN);
    if (!hdr_is(hdr_addr, hdr_exhibition)) {
        if (hdr_is(hdr_addr, hdr_versus) || scr == hdr_scan_screen) return;
        hdr_scan_screen = scr;                     // once per page: look for it in the menus' heap
        u32 a;
        for (a = 0x80180000u + SIX_HEAP_SHIFT; a < 0x80200000u + SIX_HEAP_SHIFT; a += 8)   // (in the heap: it moves with it)
            if (RD32(a) == hdr_word(hdr_exhibition, 0) && hdr_is(a, hdr_exhibition)) break;
        if (a >= 0x80200000u) return;
        hdr_addr = a;
    }
    for (u32 i = 0; i < HDR_WORDS; i++) RD32(hdr_addr + i * 4) = hdr_word(hdr_versus, i);
}

// ---- routing: taking the game where the players are going -----------------------------------------
u32 dbg_leave_retries, dbg_menu_cam;
static void set_goal(int g, int wait) {
    flight_event(FL_GOAL, (u32)g, (u32)wait);
    if (banner_paused && match_running()) RDS16(NM_MATCH_PAUSED) = 0;
    banner_paused = 0;
    goal = g; goal_frames = 0; goal_wait = wait; leave_asked = 0; leave_fast = 0;
}

static int goal_return_screen(void) {
    return goal == G_TITLE ? SCR_TITLE : goal == G_STORY ? SCR_SINGLEPLAY : SCR_MULTIPLAY;
}

// The menus' camera (ovl_a: eye 0x80104490, looked at 0x8010449C, where it's going 0x801044A8, and the page's place
// 0x80104478 / 0x80104540; the page 0x801044D4): each page has its place, and the menus glide the camera there as
// pages change. From an attract demo (coin + Start) the menus come straight onto Single Play and the camera stays at
// its far place (0, 200, -1280): the page's 3D scene came out small (2026-10-28). On Single Play and the
// championship list, still there, it's put at the page's own (as from the title: (100, 50, 520), looking 500 on).
// And Versus's MATCH page, put up by the arcade from the championship list (a challenger), draws its rows at Multi
// Play's place (750, 0, 720), page 13: the camera left at the championship's (or the far place) showed the page with
// nothing on it -- after a story and a reset, when Multi Play's page wasn't built afresh on the way (2026-10-29).
#define MENU_CAM 0x80104478u
static void menu_cam_put(float x, float y, float z, u32 page) {
    static const u32 at[4] = { 0x80104478u, 0x80104490u, 0x801044A8u, 0x80104540u };
    for (int i = 0; i < 4; i++) {
        volatile float* p = (volatile float*)at[i];
        p[0] = x; p[1] = y; p[2] = z;
    }
    volatile float* l = (volatile float*)0x8010449Cu;
    l[0] = x; l[1] = y; l[2] = z + 500.0f;
    RD32(0x801044D4u) = page;
    dbg_menu_cam++;
}
static void menu_cam_fix(void) {
    if (!nm_arcade() || !in_menus()) return;      // (Arcade only: for the arcade's direct routing)
    s16 scr = RDS16(NM_SCREEN);
    volatile float* e = (volatile float*)0x80104490u;
    int far = e[0] == 0.0f && e[1] == 200.0f && e[2] == -1280.0f;
    int champ = e[0] == 100.0f && e[1] == 50.0f && e[2] == 520.0f;
    if ((scr == SCR_SINGLEPLAY || scr == SCR_CHAMPIONSHIP) && far) menu_cam_put(100.0f, 50.0f, 520.0f, 14);
    if (scr == SCR_EXHIBITION && !menus_loading() && (far || champ)) {
        menu_cam_put(750.0f, 0.0f, 720.0f, 13);
        if (exh_is_exhibition()) {                    // (its rows were put where the camera was: put again)
            plat_game_call(GF_EXH_INIT, 0, 0, 0);
            RD8(menu_record(SCR_EXHIBITION) + 5) = 0;
        }
    }
}
// (the arcade taking the menus to Versus: the camera at Multi Play's place first, where the page is built)
static void menu_cam_multiplay(void) {
    if (!nm_arcade()) return;                      // (Arcade only)
    volatile float* e = (volatile float*)0x80104490u;
    if (e[0] != 750.0f || e[1] != 0.0f || e[2] != 720.0f) menu_cam_put(750.0f, 0.0f, 720.0f, 13);
}
static void route(int dt) {
    menu_cam_fix();
    if (goal == G_NONE) return;
    goal_frames += dt;
    if (goal_wait > 0) goal_wait -= dt;
    if (leave_fast && !match_running()) goal_wait = 0;      // nothing to hold still: on at once
    if (goal_frames > 60 * 60) { goal = G_NONE; return; }   // never got there: leave it to the players
    s32 mode = RDS32(NM_MODE);

    if (mode == -6) {
        // a demo: its own fade-out (what Start does in a demo), once the match loop runs and its
        // opening fade-in is over (two fades at once never finish)
        u16 f = RD16(NM_MATCH_FRAMES);
        if (f != 0xFFFF && RD8(NM_MATCH_FADING_IN) == 0) RD8(NM_FADE_FLAGS) |= 0x80;
        return;
    }
    if (intro_running()) { RDS32(NM_MODE) = 1; return; }              // the intro movie: skipped
    if (!in_menus()) {
        // an entrance (the match is loaded, its entrance steps run first): straight to their end
        s32 step = RDS32(NM_SCENE_STEP);
        if (leave_fast && mode == 0 && RDS16(NM_SCREEN) == SCR_MATCH && step > 0 && step < 30)
            RDS32(NM_SCENE_STEP) = 30;                   // the entrances' last step: the match starts
        // in a match: it ends through its own exit, and the menus open where we're going
        if (match_running() && leave_fast && goal_wait <= 0 && !leave_asked && RD8(NM_MATCH_FADING_IN))
            RD8(NM_MATCH_FADING_IN) = 0;                 // a challenger doesn't wait for the fade-in
        // (the banner's hold let go first: asked while the game's pause flag was up, the quit was dropped as the pause
        // ended and the Rumble went on under HERE COMES A NEW CHALLENGER, 2026-10-29; and asked again if a second on
        // the match still runs, nothing leaving)
        if (goal_wait <= 0 && banner_paused) { if (match_running()) RDS16(NM_MATCH_PAUSED) = 0; banner_paused = 0; }
        if (leave_asked && match_running() && goal_frames - leave_asked_at > 60 && RDS16(NM_MATCH_LEAVE) == 0 &&
            RD8(NM_MATCH_DONE) == 0) { leave_asked = 0; dbg_leave_retries++; }
        if (match_running() && !leave_asked && goal_wait <= 0 && RDS16(NM_MATCH_LEAVE) == 0 &&
            RD8(NM_MATCH_DONE) == 0 && RD8(NM_MATCH_FADING_IN) == 0) {
            RD8(NM_MATCH_QUIT) = 1;
            RDS16(NM_MATCH_LEAVE) = leave_fast ? 1 : 60;
            leave_asked = 1; leave_asked_at = goal_frames;
        }
        if (leave_asked || mode != -2) RDS16(NM_RETURN_SCREEN) = (s16)goal_return_screen();
        return;
    }

    s16 scr = RDS16(NM_SCREEN);
    if (scr == SCR_TRANS_A || scr == SCR_TRANS_B || scr == SCR_MATCH) return;
    switch (goal) {
    case G_TITLE:
        if (scr == SCR_TITLE) { goal = G_NONE; return; }
        RDS16(NM_RETURN_SCREEN) = SCR_TITLE;      // the menus start over on their title
        RDS32(NM_MODE) = -1;
        return;
    case G_STORY:
        if (menus_loading()) return;                     // (a page is being loaded: wait for it)
        // (ABBREVIATED MENUS off, phase 3: a game starts on the game's own main menu -- its first page, Multi Play, as
        //  Start on the title opens it -- and the players go where they like from there; the arcade's routing stops)
        if (!short_menus) {
            if (scr == SCR_TITLE) { RDS16(NM_SCREEN_CHANGED) = 1; menu_change(SCR_MULTIPLAY); }
            else goal = G_NONE;
            return;
        }
        // from the title by way of the main menu's first page, the way Start on the title goes:
        // its set-up (GF_PAGE_INIT 2) is where the menus' wrestler models are readied (the
        // superstar select's model, the match card's torsos)
        int fresh = scr == SCR_TITLE;
        if (fresh) { RDS16(NM_SCREEN_CHANGED) = 1; menu_change(SCR_MULTIPLAY); scr = RDS16(NM_SCREEN); }
        // Story Mode starts on the championship list: the player picks the title to go for
        if (scr == SCR_CHAMPIONSHIP || is_select(scr)) { goal = G_NONE; return; }
        // (from anywhere else -- Multi Play's page, where a Royal Rumble's results' Next leaves the game -- the way from
        // the title: Multi Play's page and Single Play's built afresh where they stand, the camera at Single Play's
        // place. Single Play's page left as it was built before the match, the championship list came up with no
        // rows (the user, after winning a Rumble, 2026-10-30; QA rumble_win_list) -- as Versus's MATCH page did)
        if (!fresh) {
            RDS16(NM_SCREEN_CHANGED) = 1; menu_change(SCR_MULTIPLAY);
            plat_game_call(GF_PAGE_INIT, SCR_SINGLEPLAY, 0, 0);
            menu_cam_put(100.0f, 50.0f, 520.0f, 14);
        }
        menu_choose(SCR_SINGLEPLAY, 0, ACT_CHAMPIONSHIP);
        return;
    case G_VERSUS:
        if (!short_menus) {                              // (ABBREVIATED MENUS off: as G_STORY's, the main menu)
            if (scr == SCR_TITLE) { RDS16(NM_SCREEN_CHANGED) = 1; menu_change(SCR_MULTIPLAY); }
            else goal = G_NONE;
            return;
        }
        // Versus is the Exhibition menu (match type, players, arena, rules, then the Superstar
        // Select), chosen the menus' own way; its announcer call ("Exhibition!") isn't wanted
        if (scr == SCR_EXHIBITION) { goal = G_NONE; return; }
        // (Multi Play's page built again where it stands, as the game builds it going there (menu_change): left as it
        // was built going by from the title to Story Mode, the camera at its far place then, the MATCH page's rows
        // were drawn behind the camera -- the page empty, after a reset or a game over, 2026-10-29)
        menu_cam_multiplay();
        if (scr != SCR_MULTIPLAY) { plat_game_call(GF_PAGE_INIT, SCR_MULTIPLAY, 0, 0); RD16(0x8015832Cu) = 0; }
        if (menu_choose(SCR_MULTIPLAY, 0, ACT_EXHIBITION) >= 0 && !dbg_keep_voice) RDS32(NM_MENU_SOUND) = -1;
        return;
    }
}

// One player's Versus (the championship list's row): Versus's menus are his. Opened the way the arcade takes two players
// to Versus (route G_VERSUS -- from the championship list as a challenger's is: QA versus_after_story_reset), left for
// the list by B on its MATCH page the way Story Mode is gone back to (route G_STORY: from anywhere but the title, Multi
// Play's and Single Play's pages built afresh and the camera at Single Play's place, then the championship).
static int versus_menus(void) { return game_mode == GM_VERSUS || vs_solo(); }
static void vs_open(void) {
    sp_versus = 1; sp_kind = SP_NONE;
    set_goal(G_VERSUS, 0);
    if (sfx_pending < 0) sfx_pending = SFX_MENU_CONFIRM;
}
static void vs_back(void) {
    sp_versus = 0;
    vs_cursor_pending = 1;
    set_goal(G_STORY, 0);
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

// After the game read the controllers (NM_PADS): remember them and decide what the game sees. Both modes: the settings,
// the announcers, the game's lettering, the Titantron, QA's presses, the sounds asked for, the keys, F2 pausing a
// match. Then Regular (2026-11-07): every controller as it is (regular_pads). Arcade: the paying players'
// controllers (the player who started is the game's controller 1), nothing while the arcade's own things are up.
int dbg_sfx = -1;
int dbg_inject, dbg_inject_frames;
int dbg_passive;                  // tests: the plain game (the mod does nothing)
u32 dbg_call_addr, dbg_call_args[4];
s32 dbg_call_ret;
static void pads_out(u16* ob, u8* ox, u8* oy);
static void regular_pads(const u16* raw, const u16* edge);
void arcade_pad_hook(void) {
    if (!settings_loaded) load_settings();
    announce_frame();
    draw_gtext_service();
    tron_main();
#ifdef ARCADE_TEST
    if (dbg_call_addr) { u32 a = dbg_call_addr; dbg_call_addr = 0;
        dbg_call_ret = ((s32 (*)(u32, u32, u32, u32))a)(dbg_call_args[0], dbg_call_args[1], dbg_call_args[2], dbg_call_args[3]); }
#endif
    if (dbg_passive) return;
    // (W10b: the NUMBER page's limits held before the menus' loop steps it this frame -- the controller read comes first)
    number_page_frame(1);
    // (QA, qa_versus: a press on player 1's controller, as the controller read it -- the arcade's handling and the game's
    // both see it as a player's)
    for (int q = 0; q < 2; q++)
        if (qa_pad_frames[q] > 0) { RD16(NM_PADS + 6u * (u32)port_player[q]) |= qa_pad_bits[q]; qa_pad_frames[q]--; }
    if (qa_stick_frames > 0) { RD8(NM_PADS + 2u) = (u8)qa_stick_x; qa_stick_frames--; }   // (QA: controller 1's stick)
    if (qa_pspecial) qa_pspecial_pad();   // (QA: player 1's controller pressed as a player would -- a grab, his special)
    if (qa_lj_p1) lumberjack_qa_pad(NM_PADS + 6u * (u32)port_player[0]);   // (QA: player 1 in a Lumberjack match, lumberjack.c)
    if (qa_gr_p1) guestref_qa_pad(NM_PADS + 6u * (u32)port_player[0]);     // (QA: player 1 in a Guest Referee match, guestref.c)
    if (qa_ringstage & 4) qa_ringstage_pad();   // (QA: slot 0's player walks up to the stage, fights there, whips)
    if (dbg_sfx >= 0) { plat_sound((u32)dbg_sfx); dbg_sfx = -1; }
    if (sfx_pending >= 0) { plat_sound((u32)sfx_pending); sfx_pending = -1; }
    // a challenger stops the match where it is: the match loop's own pause state (no pause menu:
    // nobody paused) holds every wrestler, the clock and the camera
    if (goal == G_VERSUS && leave_fast && goal_wait > 0 && match_running()) { RDS16(NM_MATCH_PAUSED) = 1; banner_paused = 1; }
    poll_keys();
    u16 raw[4], edge[4];
    for (int i = 0; i < 4; i++) {
        u32 p = NM_PADS + 6u * (u32)i;
        raw[i] = RD16(p);
        edge[i] = (u16)(raw[i] & ~pad_prev[i]);
        pad_latch[i] |= edge[i];
        pad_prev[i] = raw[i];
        if (i < 2) { pad_stick[i][0] = RDS8(p + 2); pad_stick[i][1] = RDS8(p + 3); }
    }
    // (W11: anything newly pressed, on any controller -- the keyboard is controller 1 -- and the menus aren't idle)
    if (edge[0] | edge[1] | edge[2] | edge[3]) { menu_idle_frames = 0; reg_pressed = 1; }
    // the controllers' BACK / SELECT: the player's coin
    for (int i = 0; i < 2; i++) {
        int c = (plat_pad_buttons(i) & 1) != 0;
        if (c && !pad_coin_prev[i]) pad_coin_latch[i] = 1;
        pad_coin_prev[i] = (u8)c;
    }
    // the operator menu holds a match where it is (the match loop's own pause state, no pause menu)
    if (op_open && match_running()) { RDS16(NM_MATCH_PAUSED) = 1; op_paused = 1; }
    // Regular (2026-11-07): the game sees every controller as it is -- its own Start, pause menu and players 1-4 --
    // and none of the arcade's filtering below (paid players only, Start held back, the arcade's rows)
    if (!nm_arcade()) { regular_pads(raw, edge); return; }
    int pass = (phase == PLAY || qa_on) && !op_open && cont_player < 0 && goal == G_NONE;
    u16 ob[4] = { 0, 0, 0, 0 }, oe[4] = { 0, 0, 0, 0 };
    u8 ox[4] = { 0, 0, 0, 0 }, oy[4] = { 0, 0, 0, 0 };
    if (pass) {
        for (int port = 0; port < 2; port++) {
            int pl = port_player[port];
            if (!active[pl]) continue;
            u32 src = NM_PADS + 6u * (u32)pl;
            ob[port] = raw[pl]; oe[port] = edge[pl]; ox[port] = RD8(src + 2); oy[port] = RD8(src + 3);
            held_back[port] &= raw[pl];
            // No pause in the arcade: in a match the game never sees Start (its pause menu). Start
            // is the arcade's there -- a credit's power refill, a continue, a challenger joining.
            if (match_running()) held_back[port] |= (u16)(raw[pl] & BTN_START);
            ob[port] &= (u16)~held_back[port];
            // ARCADE controls: the six buttons -> the game's (arcctl.c)
            if (arc_controls && match_running() && !RDS16(NM_MATCH_PAUSED))
                arcctl_port(port, ob[port], (s8)ox[port], (s8)oy[port], &ob[port], &ox[port], &oy[port]);
        }
    }
    if (pass && in_menus()) {
        s16 scr = RDS16(NM_SCREEN);
        // no backing out past where the arcade put them: the championship list in Story Mode,
        // the Exhibition menu in Versus (Ladder Match's MATCH page goes back to it: route)
        // (... also while the Versus MATCH page is still coming up -- its rows not in yet, it isn't known for
        // Exhibition's: a second B then went on out to the main menu and the page came back empty, 2026-10-28)
        // (ABBREVIATED MENUS off: B goes back the game's own way -- the players came in by the main menu)
        if (short_menus && ((game_mode == GM_STORY && scr == SCR_CHAMPIONSHIP) ||
            (versus_menus() && scr == SCR_EXHIBITION && (exh_is_exhibition() || menus_loading() || !exh_rows_in()))))
            for (int i = 0; i < 4; i++) ob[i] &= (u16)~BTN_B;
        // (one player's Versus: B there takes him back to the championship list, the cursor on its row -- vs_back)
        if (vs_solo() && scr == SCR_EXHIBITION && exh_is_exhibition() && !menus_loading() && exh_rows_in() && (oe[0] & BTN_B)) {
            held_back[0] |= BTN_B;
            RD16(NM_PRESSED) &= (u16)~BTN_B;
            vs_back_pending = 1;
        }
        // Versus's pages after MATCH (PLAYER, ARENA, RULES): B goes back a page -- the tab before, L. (The game's B
        // there leaves the Exhibition menu for the main menu: the arcade put them back on MATCH, which came up with
        // nothing on it -- "back, back and pick again: the menu disappears", the user, 2026-10-28.)
        if (versus_menus() && (scr == SCR_PLAYERS || scr == SCR_ARENA || scr == SCR_RULES)) {
            int was = 0;
            for (int i = 0; i < 4; i++) {
                if (ob[i] & BTN_B) ob[i] = (u16)((ob[i] & ~BTN_B) | BTN_L);
                if (oe[i] & BTN_B) was = 1;
            }
            if (was || (RD16(NM_PRESSED) & BTN_B)) RD16(NM_PRESSED) = (u16)((RD16(NM_PRESSED) & ~BTN_B) | BTN_L);
        }
        // any ARENA page (Versus's, Survival's, the Rumble's ... -- W6, 2026-11-07; Versus's only before): its window of
        // ten rows on the arenas (arena.c)
        if (scr == SCR_ARENA && !menus_loading()) { extern void arena_page_pad(u16* ob, u16* oe, int ports); arena_page_pad(ob, oe, 2); }
        // the Versus PLAYER page with one line-up goes by by itself (players_page): no presses of theirs on it
        if (versus_menus() && scr == SCR_PLAYERS && players_auto)
            for (int i = 0; i < 4; i++) ob[i] &= (u16)~(BTN_A | BTN_B | BTN_START);
        // Survival's ARENA / RULES pages go by by themselves: no backing out of them
        if (game_mode == GM_STORY && sv_setup && (scr == SCR_ARENA || scr == SCR_RULES))
            for (int i = 0; i < 4; i++) ob[i] &= (u16)~BTN_B;
        // the Survival row of the championship list
        if (game_mode == GM_STORY && scr == SCR_CHAMPIONSHIP && !challenge_on && !menus_loading() &&
            RD8(menu_record(SCR_CHAMPIONSHIP) + 5) == CHAMP_SURV_ROW && RD16(CHAMP_SURV_DEF + 0xA) == LABEL_SURVIVAL &&
            (oe[0] & (BTN_A | BTN_START))) {
            ob[0] &= (u16)~(BTN_A | BTN_START); held_back[0] |= ob_taken(oe[0]);
            sv_open_pending = 1;
        }
        // the Versus row of the championship list: one player's own Versus (vs_open, between frames)
        if (game_mode == GM_STORY && scr == SCR_CHAMPIONSHIP && !challenge_on && !menus_loading() && !list_on && !custom_launch &&
            RD8(menu_record(SCR_CHAMPIONSHIP) + 5) == CHAMP_VERSUS_ROW && champ_versus_on() && (oe[0] & (BTN_A | BTN_START))) {
            ob[0] &= (u16)~(BTN_A | BTN_START); held_back[0] |= ob_taken(oe[0]);
            RD16(NM_PRESSED) &= (u16)~(BTN_A | BTN_START);
            vs_open_pending = 1;
        }
        // a story of ours: its row of the championship list (while its championship's row is taken, no presses)
        if (scr == SCR_CHAMPIONSHIP && custom_launch) for (int i = 0; i < 4; i++) ob[i] = 0;
        if (game_mode == GM_STORY && scr == SCR_CHAMPIONSHIP && !challenge_on && !menus_loading() && !custom_launch &&
            RD8(menu_record(SCR_CHAMPIONSHIP) + 5) == CHAMP_CUSTOM_ROW && custom_row_on() &&
            (oe[0] & (BTN_A | BTN_START))) {
            ob[0] &= (u16)~(BTN_A | BTN_START); held_back[0] |= ob_taken(oe[0]);
            custom_open_pending = 1;
        }
        // (lumberjack2) Versus: the Lumberjack Match row of the MATCH page, under Ladder Match (exh_lumberjack_pad)
        if (versus_menus() && scr == SCR_EXHIBITION && !menus_loading() && lj_row_shown) exh_lumberjack_pad(ob, (u16)(oe[0] | oe[1]));
        // Versus: the Ladder Match row of the MATCH page
        if (versus_menus() && scr == SCR_EXHIBITION && !menus_loading() && exh_is_exhibition() &&
            RD8(menu_record(SCR_EXHIBITION) + 5) == EXH_LADDER_ROW && ((oe[0] | oe[1]) & (BTN_A | BTN_START))) {
            for (int i = 0; i < 2; i++) { ob[i] &= (u16)~(BTN_A | BTN_START); held_back[i] |= ob_taken(oe[i]); }
            ladder_open_pending = 1;
        }
        // the stories' list (list_open, on the championship page): the arcade answers its rows and B
        if (game_mode == GM_STORY && scr == SCR_CHAMPIONSHIP && list_on && !menus_loading()) {
            int cur = RD8(menu_record(SCR_CHAMPIONSHIP) + 5);
            if (oe[0] & (BTN_A | BTN_START)) { list_pick_pending = list_top + cur; }
            else if (oe[0] & BTN_B) list_back_pending = 1;
            else if ((oe[0] & BTN_R) && list_top + LIST_ROWS < list_count()) { list_top += LIST_ROWS; RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = 0; list_fade_in(); }
            else if ((oe[0] & BTN_L) && list_top > 0) { list_top -= LIST_ROWS; RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = 0; list_fade_in(); }
            held_back[0] |= (u16)(oe[0] & (BTN_A | BTN_START | BTN_B | BTN_L | BTN_R));
            for (int i = 0; i < 4; i++) ob[i] &= (u16)~(BTN_A | BTN_START | BTN_B | BTN_L | BTN_R | BTN_Z);
            oe[0] &= (u16)~(BTN_A | BTN_START | BTN_B | BTN_L | BTN_R);   // (the championship rows' own answers: not now)
        }
        // the Royal Rumble row of the championship list: the arcade's Rumble (rr_open, between frames)
        if (game_mode == GM_STORY && scr == SCR_CHAMPIONSHIP && !challenge_on && !menus_loading() &&
            RD8(menu_record(SCR_CHAMPIONSHIP) + 5) == CHAMP_FREE_ROW && RD16(CHAMP_FREE_DEF + 0xA) == LABEL_ROYAL_RUMBLE &&
            (oe[0] & (BTN_A | BTN_START))) {
            ob[0] &= (u16)~(BTN_A | BTN_START); held_back[0] |= ob_taken(oe[0]);
            rr_open_pending = 1;
        }
        // single player's Superstar (Story Mode, or the Royal Rumble)
        // (not one player's Versus: nothing of single player's to carry on with after a challenge but its menus)
        if (game_mode == GM_STORY && is_select(scr) && (oe[0] & BTN_A) && !challenge_on && !sp_versus) {
            u16 h = sel_hover();
            if (h && h < 0x1000) sp_pick = h;
            sp_kind = rr_mode == RR_SELECT ? SP_RUMBLE : (sp_kind == SP_SURVIVAL && sv_setup) ? SP_SURVIVAL : SP_STORY;
            sv_setup = 0;
            if (sp_kind == SP_STORY) sp_belt = RD8(menu_record(SCR_CHAMPIONSHIP) + 5);
        }
        // the Rumble's select: no backing out (there's nothing behind it but the list)
        if (game_mode == GM_STORY && is_select(scr) && rr_mode == RR_SELECT)
            for (int i = 0; i < 4; i++) ob[i] &= (u16)~BTN_B;
        // Versus PLAYER page: the option confirmed is the one on the row (see players_page)
        if (scr == SCR_PLAYERS && ((oe[0] | oe[1]) & (BTN_A | BTN_START))) players_restore();
    }
    pads_out(ob, ox, oy);
}

// The end of the controller read, both modes (moved out of arcade_pad_hook as it was, 2026-11-07): the presses the
// mod makes for the game's controller 1 (a test's, dbg_inject; the arcade's or a QA driver's answer to a page,
// inject_press) joined to what controller 1 has, the controllers written back as the game will read them, and the
// entrances' full-screen video skipped when FULL SCREEN TITANTRONS is off (the wrestler walks out to his music).
static void pads_out(u16* ob, u8* ox, u8* oy) {
    if (dbg_inject_frames > 0) { if (dbg_inject_frames == 4) RD16(NM_PRESSED) |= (u16)dbg_inject; dbg_inject_frames--; ob[0] |= (u16)dbg_inject; }
    // the arcade's answer to a page (single-player Royal Rumble set-up): one press, controller 1
    if (inject_frames > 0) { if (inject_frames == 3) RD16(NM_PRESSED) |= inject_bits; inject_frames--; ob[0] |= inject_bits; }
    for (int i = 0; i < 4; i++) {
        u32 p = NM_PADS + 6u * (u32)i;
        RD16(p) = ob[i]; RD8(p + 2) = ox[i]; RD8(p + 3) = oy[i];
    }
    // entrances: with Titantron videos off, skip the full-screen video (the wrestler walks out to
    // their music)
    // (2026-11-18, valetfix: since then the game skips the video step itself, entrance_video_step below, and the stage
    //  step that follows is set up by the game's own code. This is left for a video step reached with the option on
    //  and turned off while it plays: the step is written over, as before -- its walk paths are not set up then.)
    if (!titantron && RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == SCR_MATCH) {
        s32 st = RDS32(NM_ENTRANCE_STEP);
        if (st == 4 || st == 10) {
            // (the step skipped is where the game picks the entering Superstar's video: picked here for
            // the arena's Titantron -- func_8000BBD0(id, costume), the entrant 0x80133B94's)
            s32 who = RDS32(0x80133B94u);
            if (who >= 0 && who < 4) tron_video_picked((s32)entrance_video(RD32(SIXA_800A4038 + 4u * (u32)who), RD32(SIXA_800A8958 + 4u * (u32)who)));
            RDS32(NM_ENTRANCE_STEP) = st + 1;
        }
    }
}

// The entrance's next step picked (ovl_b func_80109F88, gen/patches.py): its test of the video step (steps 4 / 10 /
// 16 / 22) asks func_8010C998(v) for the entering Superstar's video v -- none (0), and the game goes straight on to the
// stage step and sets that step up, as it does for a Superstar with no video. With FULL SCREEN TITANTRONS off the
// answer is "none": the video is still picked (media_video_select: the arena's Titantron screen plays it on the stage),
// but not played full screen.
// (2026-11-18, valetfix: the user -- "When a wrestler comes out with a valet ... the valet and the wrestler come out in the
//  exact same spot and they like clip into each other". The video step used to be cut by writing the stage step over it
//  (pads_out, above), so the stage step's own set-up never ran: func_80109F88's case for it -- the close scene, its
//  camera shot and pick_entrance_walk_path (0x8010990C), which gives the entrant walk path 0 (2 with entrance style
//  0x2000) and his valet path 1, 50 to his side and 180 behind (ovl_b's paths 0x8012E038). The paths kept
//  what earlier steps had left in them, and the valet walked out in the wrestler's own spot, inside him (QA
//  valet_entrance: GODFATHER with HO at RAW IS WAR, the Arcade's low stage camera). The game's own way of going past the video step sets the paths, as the original game does with a Superstar who
//  has no video.)
s32 entrance_video_step(s32 v) {
    s32 list = media_video_select(v);
    return titantron ? list : 0;
}

// Regular mode's controllers (2026-11-07): the game reads all four as they are -- no paying players, its own Start
// (and so its pause menu), the keyboard as controller 1 (the exe's default profile: Return is Start). The mod still
// has: F2 open takes every controller from the game (the menu is driven with them, nothing reaches the match or
// the menus underneath); ARCADE controls (F2 CONTROL MODE) turn controllers 1 and 2's six buttons into the game's
// in a match, as in Arcade; and the presses the mod makes for controller 1 (QA, pads_out).
// Also, on the championship list (W5): A / Start on the Custom Story row opens the stories' list instead of the row's
// own action (the GBC link-up's), and while the list is up its rows, B and L / R are the mod's -- as in Arcade; the
// buttons taken are kept from the game until let go (reg_held: a held A would otherwise reach the game as a new press
// the frame after). And any ARENA page (W6): its window past ten rows (arena.c).
static void regular_pads(const u16* raw, const u16* edge) {
    u16 ob[4], oe[4]; u8 ox[4], oy[4];
    for (int i = 0; i < 4; i++) {
        u32 p = NM_PADS + 6u * (u32)i;
        ob[i] = raw[i]; oe[i] = edge[i]; ox[i] = RD8(p + 2); oy[i] = RD8(p + 3);
        reg_held[i] &= raw[i];
        if (op_open) { ob[i] = 0; oe[i] = 0; ox[i] = 0; oy[i] = 0; }
    }
    if (!op_open && in_menus() && !menus_loading()) {
        s16 scr = RDS16(NM_SCREEN);
        u32 champ = menu_record(SCR_CHAMPIONSHIP);
        u16 any = (u16)(oe[0] | oe[1] | oe[2] | oe[3]);
        if (scr == SCR_CHAMPIONSHIP && custom_launch) for (int i = 0; i < 4; i++) ob[i] = 0;
        else if (scr == SCR_CHAMPIONSHIP && list_on) {
            int cur = RD8(champ + 5);
            if (any & (BTN_A | BTN_START)) list_pick_pending = list_top + cur;
            else if (any & BTN_B) list_back_pending = 1;
            else if ((any & BTN_R) && list_top + LIST_ROWS < list_count()) { list_top += LIST_ROWS; RD8(champ + 5) = 0; list_fade_in(); }
            else if ((any & BTN_L) && list_top > 0) { list_top -= LIST_ROWS; RD8(champ + 5) = 0; list_fade_in(); }
            for (int i = 0; i < 4; i++) { reg_held[i] |= (u16)(oe[i] & (BTN_A | BTN_START | BTN_B | BTN_L | BTN_R | BTN_Z)); ob[i] &= (u16)~(BTN_A | BTN_START | BTN_B | BTN_L | BTN_R | BTN_Z); }
            RD16(NM_PRESSED) &= (u16)~(BTN_A | BTN_START | BTN_B | BTN_L | BTN_R);
        } else if (scr == SCR_CHAMPIONSHIP && RD8(champ + 5) == CHAMP_CUSTOM_ROW && custom_row_on() && (any & (BTN_A | BTN_START))) {
            for (int i = 0; i < 4; i++) { reg_held[i] |= (u16)(oe[i] & (BTN_A | BTN_START)); ob[i] &= (u16)~(BTN_A | BTN_START); }
            RD16(NM_PRESSED) &= (u16)~(BTN_A | BTN_START);
            custom_open_pending = 1;
        }
        if (scr == SCR_ARENA) { extern void arena_page_pad(u16* ob, u16* oe, int ports); arena_page_pad(ob, oe, 4); }
        if (scr == SCR_MULTIPLAY || scr == SCR_EXHIBITION) lj_regular_pad(ob, oe, ox, any);   // (ljround8: Multi Play's Lumberjack row, its MATCH page)
    }
    for (int i = 0; i < 4; i++) ob[i] &= (u16)~reg_held[i];
    if (!op_open && arc_controls && match_running() && !RDS16(NM_MATCH_PAUSED))
        for (int port = 0; port < 2; port++)
            arcctl_port(port, ob[port], (s8)ox[port], (s8)oy[port], &ob[port], &ox[port], &oy[port]);
    pads_out(ob, ox, oy);
}

// the Superstar entering now's Titantron video (the entrant 0x80133B94's, func_8000BBD0(id, costume)); -1 none --
// tron.c picks it as his music starts (2026-10-27: picked at the entrance's video step, the screen played the one
// before him with the next one's music)
s32 arcade_entrant_video(void) {
    s32 who = RDS32(0x80133B94u);
    if (who < 0 || who >= 4) return -1;
    return (s32)entrance_video(RD32(SIXA_800A4038 + 4u * (u32)who), RD32(SIXA_800A8958 + 4u * (u32)who));
}

// ---- title / attract -------------------------------------------------------------------------
// ---- the intro movie: no disclaimer --------------------------------------------------------------
// The intro is a script (ovl_b func_80118390: *0x8016CDB8 the entry playing, 0x1C bytes each, timed
// by the frames since the entry began, (0x8015CCE0 - 0x8015CD10) / 2). Its first entry shows the
// legal page, then crossfades into THQ, JAKKS, Asmik Ace, AKI and the WWF logo by that time. The
// legal page is a 480x240 IA4 picture decompressed into 0x801BB7F0: it is cleared, and the entry's
// time starts just before the THQ logo fades in -- out of black, as the legal page did.
#define NM_INTRO_ENTRY      0x8016CDB8u
#define NM_INTRO_FIRST      0x8013046Cu
#define NM_INTRO_CLOCK      0x8015CCE0u
#define NM_INTRO_START      0x8015CD10u
#define NM_LEGAL_PAGE       0x801BB7F0u
#define LEGAL_PAGE_WORDS    (480u * 240u / 2u / 4u)
#define LEGAL_PAGE_HASH     0xB53E5BCFu
#define INTRO_THQ_FADE      96          // (entry time when the legal page starts fading out)
static int intro_legal_done;
static u32 legal_page_hash(void) {
    u32 h = 0;
    for (u32 i = 0; i < LEGAL_PAGE_WORDS; i += 57) h = h * 31u + RD32(NM_LEGAL_PAGE + i * 4u);
    return h;
}
static void intro_service(void) {
    if (RD32(SIG_INTRO_ADDR) != SIG_INTRO || RD32(NM_INTRO_ENTRY) != NM_INTRO_FIRST ||
        RD32(NM_INTRO_FIRST) != 0x00000000u || RD32(NM_INTRO_FIRST + 4) != 0x01860101u) {
        intro_legal_done = 0;
        return;
    }
    s32 t = (RDS32(NM_INTRO_CLOCK) - RDS32(NM_INTRO_START)) / 2;
    if (t < 0 || t >= INTRO_THQ_FADE) return;
    if (!intro_legal_done) {
        if (legal_page_hash() != LEGAL_PAGE_HASH) return;      // (not decompressed yet)
        for (u32 i = 0; i < LEGAL_PAGE_WORDS; i++) RD32(NM_LEGAL_PAGE + i * 4u) = 0;
        intro_legal_done = 1;
    }
    // a moment of black first: longer at power-on, while the screen comes up
    s32 from = booted ? INTRO_THQ_FADE - 24 : INTRO_THQ_FADE - 56;
    if (t < from) RDS32(NM_INTRO_START) = RDS32(NM_INTRO_CLOCK) - 2 * from;
}

static void patch_demo_table(u16 w0, u16 w1) {
    for (int r = NM_DEMO_SINGLES_FIRST; r < NM_DEMO_SINGLES_END; r++) {
        u32 a = NM_DEMO_TABLE + 10u * (u32)r;
        RD16(a) = w0; RD16(a + 2) = w1; RD16(a + 4) = 0; RD16(a + 6) = 0;
    }
}

static int demo_a, demo_b;
// (2026-11-13, removal) Regular's attract plays the game's own demo table (33 records of four men, The Rock in some): a
// removed man -- his folder deleted, roster.c -- is in none of them; his stand-in (roster.c roster_stand_in, one not already
// in that record) takes his place, his costume kept. Done while the menus are loaded (the table is theirs, loaded again
// from the ROM with them); nobody removed, nothing done.
#define NM_DEMO_RECORDS 33
static void demo_table_removed(void) {
    extern int roster_removed_n(void); extern int roster_id_removed(u32 id); extern u16 roster_stand_in(u32 id, int nth);
    if (roster_removed_n() <= 0 || !title_loaded()) return;
    for (u32 r = 0; r < NM_DEMO_RECORDS; r++) {
        const u32 a = NM_DEMO_TABLE + 10u * r;
        for (u32 k = 0; k < 4; k++) {
            const u16 v = RD16(a + 2u * k), id = v & 0x0FFFu;
            if (!id || !roster_id_removed(id)) continue;
            u16 s = 0;
            for (int nth = 0; nth < 8 && !s; nth++) {
                const u16 c = roster_stand_in(id, nth);
                if (!c) break;
                int there = 0;
                for (u32 j = 0; j < 4; j++) if ((RD16(a + 2u * j) & 0x0FFFu) == c) there = 1;
                if (!there) s = c;
            }
            if (s) RD16(a + 2u * k) = (u16)((v & 0xF000u) | s);
        }
    }
}
static void drive_title(void) {
    if (!nm_arcade()) return;                      // (Arcade only: Regular's attract is the game's own)
    if (!attract_on) return;                       // (ATTRACT off, W9: the title waits -- attract_hold)
    if (!on_title() || phase != ATTRACT || goal != G_NONE) return;
    s32 idle = RDS32(NM_TITLE_IDLE);
    if (idle >= 0 && idle < 20) {
        demo_a = (int)(rng() % ROSTER_N);
        int r = (int)(rng() % (ROSTER_N - 1));
        demo_b = r >= demo_a ? r + 1 : r;
        // (2026-11-13, removal: a removed man -- his folder deleted, roster.c -- is in no demo: the next on the list instead)
        extern int roster_removed_n(void); extern int roster_id_removed(u32 id);
        if (roster_removed_n() > 0)
            for (int t = 0; t < ROSTER_N && (roster_id_removed(roster_id[demo_a]) || roster_id_removed(roster_id[demo_b]) || demo_a == demo_b); t++) {
                if (roster_id_removed(roster_id[demo_a])) demo_a = (demo_a + 1) % ROSTER_N;
                else demo_b = (demo_b + 1) % ROSTER_N;
            }
    }
    patch_demo_table(roster_id[demo_a], roster_id[demo_b]);
    // the game's attract counter alternates: odd the intro movie, even a demo match (always a
    // singles match here); without demos, always the movie
    u8 counter = RD8(NM_ATTRACT_COUNTER);
    if (!(counter & 1)) RD8(NM_ATTRACT_COUNTER) = attract_demos ? (u8)(NM_DEMO_SET_SINGLES * 2) : (u8)(counter | 1);
    if (idle >= 0 && idle < NM_TITLE_IDLE_LIMIT - 300) RDS32(NM_TITLE_IDLE) = NM_TITLE_IDLE_LIMIT - 300;
}

u32 dbg_demo_venue = 0xFF;   // tests: the attract demo's venue
static void qa_demo_setup(void);
void arcade_demo_setup_done(void) {
    if (qa_on == 1) { qa_demo_setup(); return; }
    // (any arena, ours too: arena.c)
    if (phase == ATTRACT) {
        int a = dbg_demo_venue < 0xFF ? (int)dbg_demo_venue : arena_random(rng());   // (firstrun: any arena there is -- arena.c)
        arena_choose(a); RD8(NM_VENUE) = (u8)arena_venue(a);
    }
}

// ---- attract demo matches: no DEMO label ---------------------------------------------------------
// The match HUD's mode label (REPLAY in a match's replays) is built at the start of a match as a
// small display list (HUD object 0x801556B8: +0x10 its quad, +0x1C the list that draws it); an
// attract demo loads the DEMO picture into it and shows it all along. INSERT COIN says it's a demo:
// the label's list ends before it draws anything.
#define NM_HUD_LABEL       0x801556B8u
#define NM_HUD_TABLE_DEMO  0x80144B70u   // u16[4]: the HUD pictures a demo loads (label: 0x38C)
static void demo_label_off(void) {
    if (!nm_arcade()) return;                      // (Arcade only: Regular's demos say DEMO, as the game's)
    if (RDS32(NM_MODE) != -6 || !(RD32(NM_MATCH_FLAGS) & NM_FLAG_ATTRACT)) return;
    if (RD32(NM_HUD_TABLE_DEMO) != 0x038E038Cu || RD16(NM_MATCH_FRAMES) == 0xFFFF) return;
    u32 quad = RD32(NM_HUD_LABEL + 0x10), list = RD32(NM_HUD_LABEL + 0x1C);
    if (!GAME_PTR(quad) || !GAME_PTR(list)) return;
    // the label's own list: ... G_VTX (its quad) ... G_TRI2 ... G_ENDDL
    if (RD32(list) == 0xDF000000u) return;                              // (done already)
    int found = 0;
    for (int i = 0; i < 8; i++)
        if ((RD32(list + 8u * (u32)i) >> 24) == 0x01 && (RD32(list + 8u * (u32)i + 4) & 0xFFFFFF) == (quad & 0xFFFFFF)) found = 1;
    if (!found) return;
    RD32(list) = 0xDF000000u;
    RD32(list + 4) = 0;
}

// ---- losing and continuing -----------------------------------------------------------------------
// The player continues the Royal Rumble he was eliminated from: the Rumble never stopped (his slot
// was held, rr_holding), he's back in it where he is, at full power (rr_back_in, after wait frames).
static void rr_continue(int wait) {
    rr_restore_due = wait > 0 ? wait : 1;
}
// A player has lost when every match slot their controller drives has been eliminated. Who drives
// which slot is taken when the match starts (everyone in).
static void watch_match(void) {
    if (!match_running() || RD16(NM_MATCH_FRAMES) < 10) { match_armed = 0; return; }
    if (!match_armed) {
        int all_in = 1;
        for (int s = 0; s < 4; s++) if (RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)s) >= 0) all_in = 0;
        if (!all_in) return;
        match_armed = 1; match_lost[0] = match_lost[1] = 0;
        // (not the match the challenger stopped: a Rumble's slots fill again as it goes on -- everyone in, armed again --
        // and the Rumble being left counted as the challenge played, player 2 out at once, 2026-10-29)
        if (challenge_on && game_mode == GM_VERSUS && !challenge_old) challenge_played = 1;
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
            if (challenge_on) { challenge_decided(pl); return; }
            if (rr_cont_on && game_mode == GM_STORY && sp_kind == SP_RUMBLE && rr_mode == RR_LIVE) {   // (W9: RUMBLE CONTINUE; off: a normal loss)
                rr_player_out = 1;
                flight_event(FL_PLAYER_OUT, (u32)pl, (u32)free_play);
                rumble_capture();
                if (free_play) { rr_continue(90); continue; }     // (no countdown: back in after a moment)
            }
            if (!free_play) lost_pending[pl] = 1;
        }
    }
}

// The player's slots still in the match (all of them while held out for a continue are not)
static int power_start_ok(int pl) {
    if (free_play || credits <= 0 || !active[pl] || cont_player == pl || challenger_frames > 0 || goal != G_NONE) return 0;
    if (!match_running() || !match_armed || match_lost[pl] || RD8(NM_MATCH_DONE) || RDS16(NM_MATCH_PAUSED)) return 0;
    for (int s = 0; s < 4; s++)
        if (slot_owner[s] == pl && RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)s) < 0) return 1;
    return 0;
}
static void power_fill(int pl) {
    for (int s = 0; s < 4; s++)
        if (slot_owner[s] == pl && RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)s) < 0) full_power(s);
    dbg_power_fills++;
}

// Arcade (W11, 2026-11-07, the user's spec: "return to the attract loop after a while idle at a menu", cabinet
// behaviour): in a game (PLAY), at a menu -- not in a match or its entrances, not a page loading, not with F2 open, not
// while CONTINUE? or a challenger's banner runs, not while the arcade is taking the game somewhere -- nobody touching
// anything for MENU IDLE seconds (0: never) ends the game quietly, credits kept, as RESET TO TITLE does: back to the
// attract loop.
static void start_game_over(void);
static int quiet_over;                // (declared again below with the operator menu: a GAME OVER with no words)
u32 dbg_menu_idle;                    // tests: games ended so
static void menu_idle(int dt) {
    int menus = in_menus() && !menus_loading() && RDS16(NM_SCREEN) != SCR_MATCH;
    if (!menus || menu_idle_secs <= 0 || op_open || cont_player >= 0 || challenger_frames > 0 || goal != G_NONE || challenge_on) {
        menu_idle_frames = 0; return;
    }
    menu_idle_frames += dt;
    if (menu_idle_frames < menu_idle_secs * 60) return;
    menu_idle_frames = 0;
    dbg_menu_idle++;
    challenge_on = challenge_played = 0; challenger = -1; resume_pending = 0;
    sp_kind = SP_NONE; sp_custom = 0; custom_launch = 0;
    rr_player_out = 0; rr_restore_due = 0;
    start_game_over();
    quiet_over = 1;
}
static void start_game_over(void) {
    game_mode = GM_NONE;
    sp_versus = 0; vs_cursor_pending = 0;
    active[0] = active[1] = 0;
    cont_player = -1;
    lost_pending[0] = lost_pending[1] = 0;
    challenger_frames = 0;
    set_goal(G_TITLE, 0);
    set_phase(GAMEOVER);
}

static const char* const credit_words[10] = {
    "CREDITS 0", "CREDIT 1", "CREDITS 2", "CREDITS 3", "CREDITS 4",
    "CREDITS 5", "CREDITS 6", "CREDITS 7", "CREDITS 8", "CREDITS 9",
};
static const char* credits_word(void) { return free_play ? "FREE PLAY" : credit_words[credits < 0 ? 0 : credits > 9 ? 9 : credits]; }
static const char* const number_words[31] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
    "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30",
};

// The arcade's words in the game's lettering. On the title they go into the menus' own sprite queue
// (arcade_menu_sprites hands them over just before the menus draw it): the title's frame builds
// more than one picture, and drawing after it doesn't hold still there. Elsewhere they're drawn
// on top of the frame (draw_gtext). (cx: centre; y: top)
#define MAX_SAY 6
typedef struct { const char* s; u32 style; s16 x, y; } Say;
static Say says[MAX_SAY];
static int n_says;
static int says_on_title(void) { return title_loaded() && RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == SCR_TITLE; }
static void say_text(const char* s, u32 style, int cx, int y, u32 rgba) {
    if (says_on_title()) {
        if (n_says < MAX_SAY) {
            int h = ((style & GT_BIG) ? 25 : 15) + ((style & GT_GLOW) ? 16 : 0);
            Say* t = &says[n_says++];
            t->s = s; t->style = style; t->x = (s16)cx; t->y = (s16)(y + h / 2 - 5);
        }
        return;
    }
    draw_gtext(s, style, cx, y, rgba);
}

// over the game: the credits, top centre (the match clock is at the bottom). With more than four in
// the ring the top strip has the entrant count and the clock (the six-man HUD): the credits go
// under the top row's centre panel then, or below the CONTINUE? countdown while it runs.
static void draw_credits(int row) {
    (void)row;
    int y = 4;
    if (match_running() && nm_slots() > 4) y = cont_player >= 0 ? 182 : 50;
    say_text(credits_word(), GT_GLOW, 240, y, 0xFFFFFFFFu);
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
    if (start[pl] && pay()) {
        flight_event(FL_CONTINUE, (u32)pl, (u32)rr_player_out);
        cont_player = -1;
        if (rr_player_out && sp_kind == SP_RUMBLE) rr_continue(0);
        return;
    }
    if (secs < 0) {
        active[pl] = 0;
        cont_player = -1;
        if (nobody_playing() && !lost_pending[0] && !lost_pending[1]) start_game_over();
        return;
    }
    // in the game's lettering, over the game
    draw_gtext(pl == 0 ? "PLAYER 1" : "PLAYER 2", GT_GLOW, 240, 52, pl == 0 ? 0xFF5040FFu : 0x60D0FFFFu);
    draw_gtext("CONTINUE?", GT_BIG | GT_GLOW, 240, 70, 0xFFE040FFu);
    draw_gtext(number_words[secs > 30 ? 30 : secs], GT_BIG | GT_GLOW, 240, 104, 0xFFFFFFFFu);
    if (can_start() || coin_lit()) draw_gtext(can_start() ? "PRESS START" : "INSERT COIN", GT_BIG | GT_GLOW, 240, 146, COL_PROMPT);
    draw_credits(28);
}

// ---- operator menu (F2) -----------------------------------------------------------------------------
// Laid out as the other arcade ports have theirs (Terrordrome's): a page of its own (the game's frame
// replaced, draw_menu_frame), the main list leading to the pages of settings; UP / DOWN choose,
// LEFT / RIGHT (or RETURN) change, ESC goes back; the controllers' d-pad, A and B work too. F2 again,
// or SAVE AND EXIT, keeps the settings; EXIT WITHOUT SAVING puts them back as they were.
enum { MI_VAL, MI_PAGE, MI_ACT, MI_INFO_PAD, MI_BIND };
enum { ACT_SAVE_EXIT, ACT_EXIT, ACT_DEFAULTS, ACT_QUIT, ACT_BACK, ACT_PADSETUP1, ACT_PADSETUP2,
       ACT_QUIT_MATCH, ACT_RESET, ACT_BIND_RESET };
static int quiet_over;           // GAME OVER from RESET GAME: no "GAME OVER" words
// (2026-11-19: PG_KEYS / PG_PADKEYS / PG_P1KEYS..PG_P4KEYS, every N64 input bindable: the keyboard, the controllers'
//  default, each player's own -- reached from the CONTROLS page, not the main list)
enum { PG_MAIN, PG_GAME, PG_COIN, PG_RUMBLE, PG_WRESTLERS, PG_CONTROLS, PG_TEST, PG_COMM, PG_PICTURE, PG_SOUND,
       PG_KEYS, PG_PADKEYS, PG_P1KEYS, PG_P2KEYS, PG_P3KEYS, PG_P4KEYS, PG_LUMBERJACK, PG_COUNT };
enum { VF_PLAIN, VF_SEC, VF_SPEED };   // how a number shows: 10, 10 SEC, +15% / OFF
typedef struct { const char* label; u8 type, fmt; int* p; s16 min, max, step; const char* const* names; s16 arg; } MI;
static const char* const off_on[] = { "OFF", "ON" };
static const char* const coin_sound_names[] = { "OFF", "1", "2", "3", "4" };
static const char* const comm_talk_names[] = { "LIGHT", "NORMAL", "HEAVY", "NONSTOP" };
static const char* const pic_window_names[] = { "WINDOW", "FULL SCREEN" };
static const char* const pic_shape_names[] = { "4:3", "16:9", "16:10", "ULTRA WIDE", "3:4", "9:16" };
static const char* const pic_limit_names[] = { "THE SCREEN'S", "NO LIMIT" };
static const char* const pic_crt_names[] = { "OFF", "SCANLINES", "FULL" };
// CONTROL MODE's names (the user, 2026-11-07: "N64" / "6-BUTTON" -- "REGULAR" / "ARCADE" are the game modes' now). The
// option itself still stores "Regular" / "Arcade" (mod.toml: the config file keeps an Enum by its string, so renaming
// those would quietly turn every saved six-button set-up back to the N64's).
static const char* const control_names[] = { "N64", "6-BUTTON" };
static const char* const mode_names[] = { "REGULAR", "ARCADE" };
static const char* const lj_pick_names[] = { "RANDOM", "SELECT" };   // (lumberjack2: F2 > LUMBERJACK MATCH)
static const char* const lj_variant_names[] = { "ONE ON ONE", "TRIPLE THREAT", "TORNADO TAG" };
static const char* const start_names[] = { "2 (1 ON 1)", "4" };   // (W10: STARTS WITH)   // (GAME MODE: MODE_REGULAR, MODE_ARCADE)
static const char* const page_title[PG_COUNT] = { "OPERATOR MENU", "GAME SETTINGS", "COIN / CREDIT SETTINGS",
    "ROYAL RUMBLE", "WRESTLER OPTIONS", "CONTROLS", "INPUT TEST", "COMMENTARY", "PICTURE", "SOUND & FEEL",
    "KEYBOARD (P1)", "ALL CONTROLLERS", "P1 CONTROLLER", "P2 CONTROLLER", "P3 CONTROLLER", "P4 CONTROLLER", "LUMBERJACK MATCH" };
static int menu_page, menu_sel, menu_top, nav_last, nav_repeat, speed_pct;
static int menu_note_frames;
static const char* menu_note;
// controller setup: each control in turn (ESC cancels, SPACE skips one)
#define WIZ_N 15
static int wiz_player, wiz_step, wiz_released, wiz_space_last;
static char wiz[WIZ_N][16] __attribute__((aligned(4)));
static const char* const wiz_name[WIZ_N] = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "Z", "L", "R",
    "C-UP", "C-DOWN", "C-LEFT", "C-RIGHT", "START", "COIN" };
static const char* const wiz_hint[WIZ_N] = {
    "JOYSTICK / D-PAD UP", "JOYSTICK / D-PAD DOWN", "JOYSTICK / D-PAD LEFT", "JOYSTICK / D-PAD RIGHT",
    "THE N64 PAD'S A BUTTON", "THE N64 PAD'S B BUTTON", "THE N64 PAD'S Z TRIGGER", "THE N64 PAD'S L BUTTON",
    "THE N64 PAD'S R BUTTON", "THE N64 PAD'S C-UP BUTTON", "THE N64 PAD'S C-DOWN BUTTON",
    "THE N64 PAD'S C-LEFT BUTTON", "THE N64 PAD'S C-RIGHT BUTTON", "START BUTTON  (JOIN / START GAME)",
    "COIN / SELECT BUTTON  (INSERT CREDIT)" };
// ARCADE controls: the cabinet's six buttons, each put on the N64 button the arcade reads it as
#define WIZ_ARC_N 12
static const s8 wiz_arc_at[WIZ_ARC_N] = { 0, 1, 2, 3, 5, 4, 10, 8, 11, 12, 13, 14 };
static const char* const wiz_arc_name[WIZ_ARC_N] = { "UP", "DOWN", "LEFT", "RIGHT", "STRIKE", "GRAPPLE", "RUN",
    "DEFEND", "ACTION", "SPECIAL", "START", "COIN" };
static const char* const wiz_arc_hint[WIZ_ARC_N] = {
    "JOYSTICK UP", "JOYSTICK DOWN", "JOYSTICK LEFT", "JOYSTICK RIGHT",
    "PUNCHES AND KICKS", "TIE-UPS AND THROWS", "RUN / CLIMB / SLIDE IN",
    "BLOCK STRIKES, REVERSE GRAPPLES  (WITH ACTION: FOCUS)", "PIN / PICK UP / ROPES / WEAPON  (WITH DEFEND: FOCUS)", "TAUNT  (SPECIAL LIT, IN A GRAPPLE: FINISHER)",
    "START BUTTON  (JOIN / START GAME)", "COIN / SELECT BUTTON  (INSERT CREDIT)" };
static int wiz_steps(void) { return arc_controls ? WIZ_ARC_N : WIZ_N; }
static int wiz_slot(int step) { return arc_controls ? wiz_arc_at[step] : step; }
static const char* wiz_step_name(int step) { return arc_controls ? wiz_arc_name[step] : wiz_name[step]; }
static const char* wiz_step_hint(int step) { return arc_controls ? wiz_arc_hint[step] : wiz_hint[step]; }
#define SC_SPACE 44
#define SC_ESC 41
#define SC_BACKSPACE 42
#define SC_DELETE 76
// The N64 controls, each one bindable (2026-11-19; the exe's arcade_pads.cpp keeps them, in controls_n64.json): the rows
// of the KEYBOARD BUTTONS (P1) and CONTROLLER BUTTONS pages, in the exe's order. SPECIAL: while held, the analog stick
// pushed the way the d-pad is held (up with no direction) -- the specials and taunts the game has on the stick.
// (2026-11-20, controls -- the user: "I still don't see the analog stick map-able in settings": its four rows were the
//  page's 15th-18th, under the 14 the page shows, named "STICK UP" ..., on pages called "... BUTTONS". Now the pages are
//  "... BUTTONS & STICK", the stick's rows come first, and a row for the whole stick (BIND_STICK, the exe's row 19): on a
//  controllers' page LEFT / RIGHT puts the N64 stick on the pad's left / right stick, ENTER takes the stick moved; on the
//  keyboard's ENTER asks for its four keys in turn. MORE ABOVE / MORE BELOW where a page goes on past what's shown.)
#define BIND_ROWS 19
#define BIND_STICK_UP 14
#define BIND_STICK 19
static const char* const bind_label[BIND_ROWS + 1] = { "A", "B", "Z", "L", "R", "START", "C-UP", "C-DOWN", "C-LEFT", "C-RIGHT",
    "D-PAD UP (MOVE)", "D-PAD DOWN (MOVE)", "D-PAD LEFT (MOVE)", "D-PAD RIGHT (MOVE)", "ANALOG STICK UP", "ANALOG STICK DOWN",
    "ANALOG STICK LEFT", "ANALOG STICK RIGHT", "SPECIAL (STICK PUSH)", "ANALOG STICK" };
// the rows as the pages list them: the stick first (whole, its four ways, SPECIAL), then the buttons, then the d-pad
#define BIND_LIST (BIND_ROWS + 1)
static const s8 bind_order[BIND_LIST] = { BIND_STICK, 14, 15, 16, 17, 18, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
static int bind_pos(int row) { for (int i = 0; i < BIND_LIST; i++) if (bind_order[i] == row) return i; return 0; }
static int bind_on, bind_dev, bind_row, bind_del_last;   // a capture on (the exe's bind_poll each frame), its device and row
static int bind_chain;           // the keyboard's whole stick: the four rows asked for in turn (bind_row 14..17)
static int bind_last;            // (QA: the last capture's end -- 1 set and saved, 2 cancelled; state.csv's bs_grabs >> 24)
// the buttons' pages: the exe's set -- 0 the keyboard, 1 the controllers' default, 2..5 players 1..4's own
static int bind_page(int page) { return page >= PG_KEYS && page <= PG_P4KEYS; }
static int bind_page_dev(void) { return menu_page - PG_KEYS; }

static int page_items(int page, MI* a) {
    int n = 0;
#define V(l, ptr, mn, mx, st, nm, f) a[n++] = (MI){ l, MI_VAL, f, ptr, mn, mx, st, nm, 0 }
#define PG(l, pg) a[n++] = (MI){ l, MI_PAGE, 0, 0, 0, 0, 0, 0, pg }
#define ACT(l, id) a[n++] = (MI){ l, MI_ACT, 0, 0, 0, 0, 0, 0, id }
    // (2026-11-07, the two modes: the menu is the same in both, but in Regular the coin-op's own settings and actions
    // are left out -- the credits page, the continue timer, the attract demos' option, the arcade's pace and FAST GET UP
    // (both Arcade-only until their Regular toggles come, the plan's W9), QUIT MATCH and RESET TO TITLE (they route the
    // arcade's way: Regular's are the plan's W8). The GAME MODE row is on top of GAME SETTINGS in both.)
    int arc = nm_arcade();
    switch (page) {
    case PG_MAIN:
        PG("GAME SETTINGS", PG_GAME);
        PG("COMMENTARY", PG_COMM);
        PG("PICTURE", PG_PICTURE);
        PG("SOUND & FEEL", PG_SOUND);
        if (arc) PG("COIN / CREDIT SETTINGS", PG_COIN);
        PG("ROYAL RUMBLE", PG_RUMBLE);
        PG("LUMBERJACK MATCH", PG_LUMBERJACK);      // (New feature (Ultimate No Mercy) -- lumberjack, 2026-11-21: lumberjack.c)
        // (2026-11-14, revfix: no WRESTLER OPTIONS page -- the wrestlers are their folders and families.json; its last line,
        //  NO WOMEN / ANNOUNCERS IN RUMBLE, is on the ROYAL RUMBLE page)
        PG("CONTROLS", PG_CONTROLS);
        PG("INPUT TEST", PG_TEST);
        ACT("QUIT MATCH", ACT_QUIT_MATCH);          // (Regular's: W8, regular_route)
        ACT("RESET TO TITLE", ACT_RESET);
        ACT("RESTORE FACTORY SETTINGS", ACT_DEFAULTS);
        ACT("SAVE AND EXIT", ACT_SAVE_EXIT);
        ACT("EXIT WITHOUT SAVING", ACT_EXIT);
        ACT("QUIT GAME", ACT_QUIT);
        return n;
    case PG_GAME:
        V("GAME MODE", &mode_next, MODE_REGULAR, MODE_ARCADE, 1, mode_names, VF_PLAIN);   // (from the next start-up: menu_draw's note)
        // (the CONTINUE TIMER: Arcade's continues; in Regular the Rumble's continue counts down too, when it's on)
        if (arc || rr_cont_on) V("CONTINUE TIMER", &continue_secs, 5, 30, 5, 0, VF_SEC);
        // (W9: these are the running mode's values -- each mode keeps its own)
        V("GAME SPEED", &speed_pct, 0, 30, 5, 0, VF_SPEED);
        V("FAST GET UP", &fast_getup, 0, 1, 1, off_on, VF_PLAIN);
        V("ABBREVIATED MENUS", &short_menus, 0, 1, 1, off_on, VF_PLAIN);
        V("LOW ENTRANCE CAMERA", &stage_cam_on, 0, 1, 1, off_on, VF_PLAIN);
        V("SPLASH SCREENS", &splash_on, 0, 1, 1, off_on, VF_PLAIN);
        V("ATTRACT", &attract_on, 0, 1, 1, off_on, VF_PLAIN);
        if (arc) V("ATTRACT DEMOS", &attract_demos, 0, 1, 1, off_on, VF_PLAIN);
        if (arc) V("MENU IDLE (0: NEVER)", &menu_idle_secs, 0, 300, 30, 0, VF_SEC);   // (W11: idle at a menu -> the attract)
        V("UNLOCK EVERYTHING", &unlock_all, 0, 1, 1, off_on, VF_PLAIN);
        V("FULL SCREEN TITANTRONS", &titantron, 0, 1, 1, off_on, VF_PLAIN);
        V("TITANTRON VIDEOS", &tron_screens, 0, 1, 1, off_on, VF_PLAIN);
        V("TITANTRON LIVE CAMERA", &tron_live, 0, 1, 1, off_on, VF_PLAIN);
        V("NO MERCY PLUS THEMES/VIDEOS", &roster_opt[RO_MEDIA], 0, 1, 1, off_on, VF_PLAIN);
        V("CUSTOM STORIES", &custom_story, 0, 1, 1, off_on, VF_PLAIN);
        V("MERGE SAME WRESTLERS", &merge_next, 0, 1, 1, off_on, VF_PLAIN);   // (automerge: from the next start-up: menu_draw's note)
        break;
    case PG_SOUND:
        V("MAIN VOLUME", &host_volume, 0, 100, 5, 0, VF_PLAIN);
        V("RUMBLE STRENGTH", &host_rumble, 0, 100, 5, 0, VF_PLAIN);
        V("STICK DEAD ZONE", &host_deadzone, 0, 30, 1, 0, VF_PLAIN);
        break;
    case PG_PICTURE:
        V("DISPLAY", &pic_window, 0, 1, 1, pic_window_names, VF_PLAIN);
        V("SHAPE", &pic_shape, 0, 5, 1, pic_shape_names, VF_PLAIN);
        V("SMOOTH MOTION", &pic_smooth, 0, 1, 1, off_on, VF_PLAIN);
        V("FRAME RATE", &pic_limit, 0, 1, 1, pic_limit_names, VF_PLAIN);
        V("CRT", &pic_crt, 0, 2, 1, pic_crt_names, VF_PLAIN);
        V("HDR", &pic_hdr, 0, 1, 1, off_on, VF_PLAIN);
        break;
    case PG_COMM:
        V("COMMENTARY (JR & KING)", &comm_on, 0, 1, 1, off_on, VF_PLAIN);
        V("VOLUME", &comm_volume, 0, 100, 10, 0, VF_PLAIN);
        V("AMOUNT", &comm_talk, 0, 3, 1, comm_talk_names, VF_PLAIN);
        V("NEW SPLICED COMMENTARY", &comm_splice, 0, 1, 1, off_on, VF_PLAIN);
        V("JR & KING AT THE TABLE", &ann_on, 0, 1, 1, off_on, VF_PLAIN);
        break;
    case PG_COIN:
        V("FREE PLAY", &free_play, 0, 1, 1, off_on, VF_PLAIN);
        V("COIN SOUND", &coin_sound, 0, COIN_SOUNDS, 1, coin_sound_names, VF_PLAIN);
        break;
    case PG_RUMBLE:
        V("SUPERSTARS IN IT", &rumble_size, 2, 40, 1, 0, VF_PLAIN);
        V("TIME BETWEEN ENTRANTS", &rumble_interval, 10, 120, 5, 0, VF_SEC);
        V("IN THE RING AT ONCE", &rumble_ring, NM_SLOTS_CAP < 4 ? NM_SLOTS_CAP : 4, NM_SLOTS_CAP, 1, 0, VF_PLAIN);
        V("STARTS WITH", &rumble_start4, 0, 1, 1, start_names, VF_PLAIN);   // (W10: a game-menu Rumble: 2 at the bell, or 4)
        V("CPU GOES FOR ELIMINATIONS", &rumble_ai, 0, 1, 1, off_on, VF_PLAIN);
        V("RUMBLE CONTINUE", &rr_cont_on, 0, 1, 1, off_on, VF_PLAIN);                  // (W9: per mode)
        V("OVER THE TOP ONLY (RULES)", &rules_preset_on, 0, 1, 1, off_on, VF_PLAIN);   // (W9: per mode -- the RULES page's preset)
        V("NO WOMEN / ANNOUNCERS", &rumble_no_women, 0, 1, 1, off_on, VF_PLAIN);
        break;
    case PG_WRESTLERS:     // (2026-11-14, revfix: the page is gone from the menu -- see PG("ROYAL RUMBLE") above)
        break;
    case PG_LUMBERJACK:    // (lumberjack: how many stand round the ring -- drawn at random from the Superstar Select's pages)
        V("LUMBERJACKS", &lj_count, 2, NM_SLOTS_CAP - 2, 1, 0, VF_PLAIN);
        V("PICKED", &lj_pick, 0, 1, 1, lj_pick_names, VF_PLAIN);              // (lumberjack2: at random, or on the select)
        V("MATCH", &lj_variant, 0, 2, 1, lj_variant_names, VF_PLAIN);         // (lumberjack2: the Lumberjack Match row's match)
        break;
    case PG_CONTROLS:
        V("CONTROL MODE", &arc_controls, 0, 1, 1, control_names, VF_PLAIN);
        a[n++] = (MI){ "P1 CONTROLLER", MI_INFO_PAD, 0, 0, 0, 0, 0, 0, 0 };
        a[n++] = (MI){ "P2 CONTROLLER", MI_INFO_PAD, 0, 0, 0, 0, 0, 0, 1 };
        ACT("P1 CONTROLLER SETUP", ACT_PADSETUP1);
        ACT("P2 CONTROLLER SETUP", ACT_PADSETUP2);
        V("SWAP CONTROLLERS 1/2", &pad_swap, 0, 1, 1, off_on, VF_PLAIN);
        PG("KEYBOARD (P1): BUTTONS & STICK", PG_KEYS);       // (2026-11-19: every N64 input, both control modes)
        PG("ALL CONTROLLERS: BUTTONS & STICK", PG_PADKEYS);  // (the default: every player's row he hasn't set himself)
        PG("P1 CONTROLLER: BUTTONS & STICK", PG_P1KEYS);
        PG("P2 CONTROLLER: BUTTONS & STICK", PG_P2KEYS);
        PG("P3 CONTROLLER: BUTTONS & STICK", PG_P3KEYS);
        PG("P4 CONTROLLER: BUTTONS & STICK", PG_P4KEYS);
        break;
    case PG_KEYS:
    case PG_PADKEYS:
        for (int i = 0; i < BIND_LIST; i++) a[n++] = (MI){ bind_label[bind_order[i]], MI_BIND, 0, 0, 0, 0, 0, 0, bind_order[i] };
        ACT("RESET TO DEFAULTS", ACT_BIND_RESET);
        break;
    case PG_P1KEYS:
    case PG_P2KEYS:
    case PG_P3KEYS:
    case PG_P4KEYS:
        for (int i = 0; i < BIND_LIST; i++) a[n++] = (MI){ bind_label[bind_order[i]], MI_BIND, 0, 0, 0, 0, 0, 0, bind_order[i] };
        ACT("USE THE DEFAULT (ALL ROWS)", ACT_BIND_RESET);
        break;
    case PG_TEST:
        break;
    }
    ACT("BACK", ACT_BACK);
    return n;
#undef V
#undef PG
#undef ACT
}

// navigation: bit 0 up, 1 down, 2 left, 3 right, 4 ok, 5 back
static int menu_nav_raw(void) {
    int n = 0;
    if (plat_key_down(SC_UP)) n |= 1;
    if (plat_key_down(SC_DOWN)) n |= 2;
    if (plat_key_down(SC_LEFT)) n |= 4;
    if (plat_key_down(SC_RIGHT)) n |= 8;
    if (plat_key_down(SC_RETURN)) n |= 16;
    if (plat_key_down(SC_ESC) || plat_key_down(SC_BACKSPACE)) n |= 32;
    for (int pl = 0; pl < 2; pl++) {
        u16 b = pad_prev[pl];
        if ((b & BTN_DU) || pad_stick[pl][1] > 40) n |= 1;
        if ((b & BTN_DD) || pad_stick[pl][1] < -40) n |= 2;
        if ((b & BTN_DL) || pad_stick[pl][0] < -40) n |= 4;
        if ((b & BTN_DR) || pad_stick[pl][0] > 40) n |= 8;
        if (b & (BTN_A | BTN_START)) n |= 16;
        if (b & BTN_B) n |= 32;
    }
    return n;
}
static int menu_nav(void) {
    int n = menu_nav_raw(), e = n & ~nav_last;
    if (n && n == nav_last) {
        if (++nav_repeat > 30 && nav_repeat % 6 == 0) e |= n & 15;    // (the directions repeat when held)
    } else nav_repeat = 0;
    nav_last = n;
    return e;
}

static int op_pause_was;          // (Regular: the match's pause flag as it was when F2 opened -- the game's pause menu)
static void menu_open(void) {
    op_open = 1;
    op_pause_was = match_running() ? RDS16(NM_MATCH_PAUSED) : 0;
    menu_page = PG_MAIN; menu_sel = 0; menu_top = 0;
    wiz_player = 0;
    bind_on = 0;                        // (a capture left when the menu closed: not picked up again)
    if (!roster_opt_loaded) roster_opts_load();
    speed_pct = arcade_speed ? speed_boost : 0;
    nav_last = menu_nav_raw();          // (what's held now doesn't count)
}
static void menu_close(void) {
    op_open = 0;
    wiz_player = 0;
    // (the match let go: in Arcade, which has no pause menu, always; in Regular, put back as it was -- if the player
    //  had the game's own pause menu up when F2 opened, it is still up, 2026-11-07 -- the plan's risk 6)
    if (op_paused) { RDS16(NM_MATCH_PAUSED) = nm_arcade() ? 0 : (s16)op_pause_was; op_paused = 0; }
}
static void menu_note_show(const char* s) { menu_note = s; menu_note_frames = 120; }

// QUIT MATCH (the operator menu, 2026-10-28 evening, the user): a match on, or its entrances, not ending already and
// nothing else routing the game -- left the way a challenger leaves one (route(): the entrance skipped, the match
// ended the game's own way, the menus: the championship list, or Versus's Exhibition menu)
static int reg_goal;                     // (Regular, W8: 1 the match left, the game's own way back; 2 the title; 3 a story's match
                                         //  left for Single Play, the story let go -- not a loss, no retry; 4 the title, watched)
static int match_quittable(void) {
    if (!nm_arcade()) {                  // (Regular: any match on, or its entrances, the mod not leaving one already)
        if (reg_goal) return 0;
        if (match_running()) return RD8(NM_MATCH_DONE) == 0;
        s32 st = RDS32(NM_SCENE_STEP);
        return !in_menus() && RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == SCR_MATCH && st > 0 && st < 30;
    }
    if (phase != PLAY || goal != G_NONE) return 0;
    if (match_running()) return RD8(NM_MATCH_DONE) == 0;
    s32 step = RDS32(NM_SCENE_STEP);
    return !in_menus() && RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == SCR_MATCH && step > 0 && step < 30;
}
static void menu_action(int act) {
    switch (act) {
    case ACT_QUIT_MATCH:
        if (!nm_arcade()) {             // (Regular, W8: the match's own exit; the menus open where the game itself goes)
            if (!match_quittable()) { menu_note_show("NO MATCH TO QUIT"); break; }
            save_settings();
            op_pause_was = 0;           // (the match let go unpaused: a quit asked while paused is dropped as it ends)
            menu_close();
            // (a story's match -- the game's Story Mode, context 8, or a story of ours: the game's own way back goes through
            //  the results and the chapter card, where it's a lost match and a retry; QUIT MATCH leaves the story instead:
            //  Single Play's page, the story of ours let go there -- phase 2 run 190019 saw the story go on to a retry)
            reg_goal = (RD8(NM_CHAMP_CONTEXT) == CONTEXT_CHAMPIONSHIP || story_active()) ? 3 : 1;
            dbg_op_done++;
            break;
        }
        if (!match_quittable()) {
            if (phase == ATTRACT && RDS32(NM_MODE) == -6) { menu_action(ACT_RESET); break; }   // (a demo: the title)
            menu_note_show("NO MATCH TO QUIT");
            break;
        }
        save_settings();
        menu_close();                   // (the match let go first)
        rr_player_out = 0; rr_restore_due = 0;   // (a Rumble's continue mustn't put him back in a match that's gone)
        set_goal(versus_menus() ? G_VERSUS : G_STORY, 0);   // (one player's Versus: its menus again)
        leave_fast = 1;                 // (after set_goal, which clears it): the entrances skipped, no wait
        dbg_op_done++;
        break;
    case ACT_RESET:                     // RESET TO TITLE: as a cabinet's reset (the credits kept)
        if (!nm_arcade()) {             // (Regular, W8: the menus start over on their title; a match left first)
            save_settings();
            op_pause_was = 0;
            menu_close();
            sp_custom = 0; custom_launch = 0;
            story_stop();               // (a story of ours let go at once -- not left running on under the menus' restart)
            reg_goal = 2; reg_pressed = 0;
            dbg_op_done++;
            break;
        }
        save_settings();
        menu_close();
        challenge_on = challenge_played = 0; challenger = -1; resume_pending = 0;
        sp_kind = SP_NONE; sp_custom = 0; custom_launch = 0;
        rr_player_out = 0; rr_restore_due = 0;
        start_game_over();
        quiet_over = 1;
        dbg_op_done++;
        break;
    case ACT_BACK:
        if (menu_page == PG_MAIN) { menu_action(ACT_EXIT); break; }
        if (bind_page(menu_page)) {     // (the buttons' pages: back to their lines on CONTROLS, from 6 on)
            menu_sel = 6 + menu_page - PG_KEYS;
            menu_page = PG_CONTROLS; menu_top = 0;
            break;
        }
        menu_sel = menu_page - 1;       // (back to the page's line in the main list)
        menu_page = PG_MAIN; menu_top = 0;
        break;
    case ACT_SAVE_EXIT:
        save_settings();
        menu_close();
        break;
    case ACT_EXIT:
        load_settings();                // (the settings as they were)
        roster_opts_load();
        menu_close();
        break;
    case ACT_DEFAULTS:
        factory_settings();
        speed_pct = arcade_speed ? speed_boost : 0;
        menu_note_show("FACTORY SETTINGS: SAVE AND EXIT TO KEEP THEM");
        break;
    case ACT_QUIT:
        save_settings();
        plat_quit();
        break;
    case ACT_BIND_RESET:                // (the buttons' pages: that device's factory bindings, saved at once)
        plat_bind_reset(bind_page_dev());
        menu_note_show(bind_page_dev() >= 2 ? "THE DEFAULT'S BUTTONS, EVERY ROW (SAVED)" : "THE FACTORY BUTTONS (SAVED)");
        break;
    case ACT_PADSETUP1:
    case ACT_PADSETUP2: {
        int pl = act == ACT_PADSETUP1 ? 0 : 1;
        char nm[4];
        if (!plat_pad_name(pl, nm, sizeof nm)) { menu_note_show(pl ? "NO CONTROLLER FOR PLAYER 2" : "NO CONTROLLER FOR PLAYER 1"); break; }
        for (int k = 0; k < WIZ_N; k++) wiz[k][0] = 0;
        wiz_player = pl + 1; wiz_step = 0; wiz_released = 0; wiz_space_last = 1;
        plat_pad_learn_start(pl);
        break;
    }
    }
}

// a binding asked for (A / ENTER on a row of the buttons' pages): the exe waits for the next key, or button / stick
static void bind_begin(int dev, int row) {
    bind_chain = 0;
    if (row == BIND_STICK && dev == 0) { bind_chain = 1; row = BIND_STICK_UP; }   // (the keyboard's whole stick: key by key)
    int r = plat_bind_start(dev, row);
    if (r == 1) { bind_on = 1; bind_dev = dev; bind_row = row; return; }
    menu_note_show(r == 2 ? "THAT CONTROLLER HAS NO LAYOUT: CONTROLLER SETUP FIRST"
                          : dev >= 2 ? "NO CONTROLLER FOR THIS PLAYER" : "NO CONTROLLER PLUGGED IN");
}

// one frame of the menu (the arcade's game logic waits)
static void menu_tick(void) {
    MI a[48];
    int n = page_items(menu_page, a);
    if (menu_note_frames > 0) menu_note_frames--;
    if (bind_on) {                      // (a capture: no navigation till it's done; what's held then doesn't count)
        int r = plat_bind_poll();
        if (r == 1 && bind_chain && bind_row < BIND_STICK_UP + 3 && plat_bind_start(bind_dev, bind_row + 1) == 1) {
            bind_row++;                 // (the keyboard's whole stick: the next way's key)
            return;
        }
        if (r) {
            bind_on = 0; bind_last = r; bind_chain = 0;
            menu_note_show(r == 1 ? "SET AND SAVED" : "NOT CHANGED");
            nav_last = menu_nav_raw();
        }
        return;
    }
    if (wiz_player) {
        int pl = wiz_player - 1;
        char el[16];
        int held = plat_pad_learn_read(pl, el, sizeof el), space = plat_key_down(SC_SPACE);
        if (plat_key_down(SC_ESC)) { wiz_player = 0; nav_last = menu_nav_raw(); return; }
        if (!wiz_released) { wiz_released = !held && !space; wiz_space_last = space; return; }
        if (space && !wiz_space_last) { wiz[wiz_slot(wiz_step)][0] = 0; wiz_step++; wiz_released = 0; }
        else if (held && el[0]) {
            int i = 0, at = wiz_slot(wiz_step);
            for (; el[i] && i < 15; i++) wiz[at][i] = el[i];
            wiz[at][i] = 0;
            wiz_step++; wiz_released = 0;
        }
        wiz_space_last = space;
        if (wiz_step >= wiz_steps()) {
            int any = 0;
            for (int k = 0; k < WIZ_N; k++) if (wiz[k][0]) any = 1;
            if (any) menu_note_show(plat_pad_learn_apply(pl, &wiz[0][0]) ? "CONTROLLER SET UP" : "THE CONTROLLER DIDN'T TAKE IT");
            else menu_note_show("NOTHING PRESSED: THE CONTROLLER IS AS IT WAS");
            wiz_player = 0;
            nav_last = menu_nav_raw();
        }
        return;
    }
    int nav = menu_nav();
    if (menu_page == PG_TEST) {
        // every control is being tested: ESC, or 1P and 2P START together, goes back
        int st1 = plat_key_down(SC_1) || (pad_prev[0] & BTN_START), st2 = plat_key_down(SC_2) || (pad_prev[1] & BTN_START);
        if (plat_key_down(SC_ESC) || (st1 && st2)) { if (!(nav_last & 0x80)) menu_action(ACT_BACK); nav_last |= 0x80; }
        return;
    }
    if (menu_sel >= n) menu_sel = n - 1;
    if (nav & 1) menu_sel = (menu_sel + n - 1) % n;
    if (nav & 2) menu_sel = (menu_sel + 1) % n;
    if (nav & 32) { menu_action(ACT_BACK); return; }
    MI* it = &a[menu_sel];
    {   // DELETE: the selected button's binding cleared (the buttons' pages)
        int del = plat_key_down(SC_DELETE);
        if (del && !bind_del_last && it->type == MI_BIND) { plat_bind_clear(bind_page_dev(), it->arg); menu_note_show("CLEARED (SAVED)"); }
        bind_del_last = del;
    }
    if (it->type == MI_BIND && (nav & 16)) { bind_begin(bind_page_dev(), it->arg); return; }
    if (it->type == MI_BIND && it->arg == BIND_STICK && bind_page_dev() >= 1 && (nav & (4 | 8))) {   // (LEFT / RIGHT: which stick)
        if (plat_bind_stick(bind_page_dev(), (nav & 8) ? 1 : 0)) menu_note_show((nav & 8) ? "THE RIGHT STICK (SAVED)" : "THE LEFT STICK (SAVED)");
        return;
    }
    if (it->type == MI_VAL && (nav & (4 | 8 | 16))) {
        int v = *it->p + ((nav & 4) ? -it->step : it->step);
        if (v < it->min) v = (nav & 16) ? it->min : it->max;
        if (v > it->max) v = it->min;
        *it->p = v;
        if (it->p == &speed_pct) { arcade_speed = v > 0; if (v > 0) speed_boost = v; }
        if (it->p == &pad_swap) plat_pad_swap();
        if (it->p == &coin_sound && coin_sound > 0) sfx_pending = coin_sfx[coin_sound];   // heard as it's picked
    } else if (nav & 16) {
        if (it->type == MI_PAGE) { menu_page = it->arg; menu_sel = 0; menu_top = 0; }
        else if (it->type == MI_ACT) menu_action(it->arg);
    }
}

// colours: the 5-bit ones of the other ports' menus
#define MC(r, g, b) ((u32)((r) * 255 / 31) << 24 | (u32)((g) * 255 / 31) << 16 | (u32)((b) * 255 / 31) << 8 | 0xFFu)
static void num_text(char* out, int v, int fmt) {
    if (fmt == VF_SPEED && v <= 0) { out[0] = 'O'; out[1] = 'F'; out[2] = 'F'; out[3] = 0; return; }
    int k = 0;
    if (fmt == VF_SPEED) out[k++] = '+';
    str_num(out + k, v);
    k = str_len(out);
    const char* tail = fmt == VF_SEC ? " SEC" : fmt == VF_SPEED ? "%" : "";
    for (int i = 0; tail[i]; i++) out[k++] = tail[i];
    out[k] = 0;
}
static void str_cat(char* d, const char* s, int max) {
    int k = str_len(d);
    for (int i = 0; s[i] && k < max - 1; i++) d[k++] = s[i];
    d[k] = 0;
}

static void menu_draw_test(void) {
    static const char* const lab[WIZ_N] = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "Z", "L", "R",
        "C-U", "C-D", "C-L", "C-R", "START", "COIN" };
    static const u16 bit[13] = { BTN_DU, BTN_DD, BTN_DL, BTN_DR, BTN_A, BTN_B, BTN_Z, BTN_L, BTN_R,
        BTN_CU, BTN_CD, BTN_CL, BTN_CR };
    char sz[64];
    for (int pl = 0; pl < 2; pl++) {
        u16 b = pad_prev[pl];
        int st[WIZ_N];
        for (int k = 0; k < 13; k++) st[k] = (b & bit[k]) != 0;
        st[0] |= pad_stick[pl][1] > 40; st[1] |= pad_stick[pl][1] < -40;
        st[2] |= pad_stick[pl][0] < -40; st[3] |= pad_stick[pl][0] > 40;
        st[13] = (b & BTN_START) || plat_key_down(pl ? SC_2 : SC_1);
        st[14] = (plat_pad_buttons(pl) & 1) || plat_key_down(pl ? SC_6 : SC_5);
        int y = 70 + pl * 170;
        draw_ptext(pl ? "PLAYER 2" : "PLAYER 1", 40, y, 2, MC(31, 31, 31));
        sz[0] = 0; str_cat(sz, "PAD: ", 64);
        char nm[40];
        plat_pad_name(pl, nm, sizeof nm);
        str_cat(sz, nm, 46);
        draw_ptext(sz, 40, y + 24, 1, MC(20, 20, 20));
        for (int j = 0; j < wiz_steps(); j++) {        // (ARCADE controls: the cabinet's names)
            int k = wiz_slot(j);
            const char* l = arc_controls ? wiz_arc_name[j] : lab[k];
            int x = 40 + (j % 8) * 72, yy = y + 46 + (j / 8) * 34;
            draw_fill(x - 4, yy - 6, 66, 28, st[k] ? MC(31, 24, 0) : MC(4, 4, 6));
            int sz2 = str_len(l) > 3 ? 1 : 2;
            int c = st[k] ? 0 : 20;
            draw_ptext(l, x + (58 - ptext_width(l, sz2)) / 2, yy + (sz2 == 1 ? 4 : 0), sz2, MC(c, c, c));
        }
    }
    draw_ptext("KEYBOARD: 5 / 6 COIN   1 / 2 START   F2 OPERATOR MENU", -1, 420, 1, MC(20, 20, 20));
    draw_ptext("ESC OR 1P+2P START: BACK", -1, 450, 2, MC(31, 31, 31));
}

static void menu_draw(void) {
    MI a[48];
    int n = page_items(menu_page, a), rows = 16;
    char val[64];
    draw_fill(0, 0, 640, 480, MC(0, 0, 3));
    if (bind_on) {                      // (a binding asked for: what to press)
        draw_ptext(page_title[PG_KEYS + bind_dev], -1, 30, 2, MC(31, 24, 0));
        if (bind_dev >= 2) draw_ptext("ON THIS PLAYER'S OWN CONTROLLER", -1, 236, 1, MC(24, 28, 31));
        draw_ptext(bind_row == BIND_STICK ? "MOVE THE STICK TO USE AS" : bind_dev ? "PRESS A BUTTON OR MOVE A STICK FOR" : "PRESS A KEY FOR",
                   -1, 130, 2, MC(24, 24, 24));
        draw_ptext(bind_row == BIND_STICK ? "THE N64 ANALOG STICK" : bind_label[bind_row], -1, 172, 3, MC(31, 31, 0));
        if (bind_chain) {
            char sz[32];
            sz[0] = 0; str_cat(sz, "THE STICK'S KEYS: ", 32);
            int k = str_len(sz);
            sz[k++] = (char)('1' + bind_row - BIND_STICK_UP); sz[k] = 0;
            str_cat(sz, " OF 4", 32);
            draw_ptext(sz, -1, 210, 1, MC(24, 28, 31));
        }
        plat_bind_name(bind_dev, bind_row, val, 48);
        draw_ptext("NOW:", -1, 260, 1, MC(20, 20, 20));
        draw_ptext(val, -1, 276, 1, MC(20, 28, 20));
        if (bind_row == BIND_ROWS - 1) draw_ptext("HELD: THE STICK PUSHED THE D-PAD'S WAY (UP WITH NONE)", -1, 310, 1, MC(24, 28, 31));
        if (bind_row == BIND_STICK) draw_ptext("THE LEFT STICK OR THE RIGHT: ITS FOUR WAYS AT ONCE", -1, 310, 1, MC(24, 28, 31));
        draw_ptext("ESC: CANCEL", -1, 440, 2, MC(24, 24, 24));
        return;
    }
    if (bind_page(menu_page)) rows = 14;   // (room for the page's notes under the list)
    if (wiz_player) {
        char sz[64];
        sz[0] = 0; str_cat(sz, wiz_player == 1 ? "PLAYER 1 CONTROLLER SETUP" : "PLAYER 2 CONTROLLER SETUP", 64);
        draw_ptext(sz, -1, 30, 2, MC(31, 24, 0));
        plat_pad_name(wiz_player - 1, val, 45);
        draw_ptext(val, -1, 70, 1, MC(20, 20, 20));
        int k = wiz_step < wiz_steps() ? wiz_step : wiz_steps() - 1;
        draw_ptext("PRESS", -1, 130, 3, MC(24, 24, 24));
        draw_ptext(wiz_step_name(k), -1, 168, 5, MC(31, 31, 0));
        draw_ptext(wiz_step_hint(k), -1, 226, 1, MC(24, 28, 31));
        for (int j = 0; j < wiz_step && j < wiz_steps(); j++) {
            int at = wiz_slot(j);
            sz[0] = 0; str_cat(sz, wiz_step_name(j), 64); str_cat(sz, ": ", 64); str_cat(sz, wiz[at][0] ? wiz[at] : "-", 64);
            draw_ptext(sz, 40 + (j / 8) * 290, 270 + (j % 8) * 20, 1, MC(20, 28, 20));
        }
        draw_ptext("SPACE: SKIP THIS ONE   ESC: CANCEL", -1, 440, 2, MC(24, 24, 24));
        return;
    }
    draw_ptext(page_title[menu_page], -1, 14, 3, MC(31, 24, 0));
    draw_fill(40, 44, 560, 2, MC(31, 24, 0));
    if (menu_page == PG_TEST) { menu_draw_test(); return; }
    if (menu_sel < menu_top) menu_top = menu_sel;
    if (menu_sel >= menu_top + rows) menu_top = menu_sel - rows + 1;
    for (int i = menu_top; i < n && i < menu_top + rows; i++) {
        MI* it = &a[i];
        int sel = i == menu_sel, y = 58 + (i - menu_top) * 22, bflags = 0;
        if (sel) draw_fill(24, y - 3, 592, 20, MC(6, 4, 0));
        int g = sel ? 31 : 22;
        val[0] = 0;
        if (it->type == MI_VAL) {
            if (it->names && *it->p >= it->min && *it->p <= it->max) str_cat(val, it->names[*it->p], 64);
            else num_text(val, *it->p, it->fmt);
        } else if (it->type == MI_INFO_PAD) {
            plat_pad_name(it->arg, val, 23);
        } else if (it->type == MI_BIND) {
            bflags = plat_bind_name(bind_page_dev(), it->arg, val, 48);
        }
        int lsize = 2;
        if (it->type == MI_INFO_PAD || it->type == MI_BIND) {   // (a long controller name / two bindings: the value small)
            draw_ptext(it->label, 40, y, 2, MC(g, g, g));
            // (a player's page: his own rows yellow as everywhere, the default's grey)
            if (it->type == MI_BIND && bind_page_dev() >= 2 && !(bflags & 2)) { if (val[0]) draw_ptext(val, 600 - ptext_width(val, 1), y + 4, 1, MC(15, 18, 22)); }
            else if (val[0]) draw_ptext(val, 600 - ptext_width(val, 1), y + 4, 1, MC(31, sel ? 31 : 24, sel ? 10 : 0));
            continue;
        }
        draw_ptext(it->label, 40, y, lsize, MC(g, g, g));
        if (val[0]) draw_ptext(val, 600 - ptext_width(val, 2), y, 2, MC(31, sel ? 31 : 24, sel ? 10 : 0));
    }
    // (2026-11-20: a page longer than the screen says so -- the stick's rows were missed under the 14 shown)
    if (menu_top > 0) draw_ptext("MORE ABOVE", 600 - ptext_width("MORE ABOVE", 1), 48, 1, MC(31, 24, 0));
    if (menu_top + rows < n) draw_ptext("MORE BELOW", 600 - ptext_width("MORE BELOW", 1), 58 + rows * 22 - 2, 1, MC(31, 24, 0));
    // (GAME MODE: latched at start-up -- a change is saved for the next one; said brighter while it differs)
    if (menu_page == PG_RUMBLE)
        draw_ptext("THE GAME'S OWN ROYAL RUMBLE OPENS WITH THESE (A MATCH'S CHANGES STAY ITS OWN)", -1, 388, 1, MC(16, 16, 16));
    if (menu_page == PG_GAME || menu_page == PG_RUMBLE)
        draw_ptext(nm_arcade() ? "THESE ARE ARCADE MODE'S VALUES" : "THESE ARE REGULAR MODE'S VALUES", -1, 404, 1, MC(16, 16, 16));
    if (menu_page == PG_GAME) {
        // (automerge: MERGE SAME WRESTLERS is read at the start-up too -- the same note, brighter while either differs)
        const int mode_changed = mode_next != nm_mode(), merge_changed = merge_running >= 0 && merge_next != merge_running;
        draw_ptext(mode_changed && merge_changed ? "GAME MODE, MERGE SAME WRESTLERS CHANGED: TAKE EFFECT AT THE NEXT START-UP"
                   : mode_changed ? "GAME MODE CHANGED: TAKES EFFECT AT THE NEXT START-UP"
                   : merge_changed ? "MERGE SAME WRESTLERS CHANGED: TAKES EFFECT AT THE NEXT START-UP"
                   : "GAME MODE, MERGE SAME WRESTLERS: TAKE EFFECT AT THE NEXT START-UP", -1, 420, 1,
                   mode_changed || merge_changed ? MC(31, 31, 10) : MC(16, 16, 16));
        // (and what the row does, while it is the one chosen)
        if (menu_sel >= 0 && menu_sel < n && a[menu_sel].p == &merge_next)
            draw_ptext("SAME WRESTLER FROM EACH GAME IN ONE BOX: THE TYPE ARROWS STEP TO HIS VARIANTS", -1, 436, 1, MC(20, 20, 20));
    }
    if (menu_page == PG_CONTROLS) {
        draw_ptext("KNOWN CONTROLLERS WORK AS THEY ARE (GAMECONTROLLERDB.TXT).", -1, 396, 1, MC(16, 16, 16));
        draw_ptext("ANY OTHER: CONTROLLER SETUP, THEN PRESS EACH CONTROL AS ASKED.", -1, 410, 1, MC(16, 16, 16));
    }
    if (bind_page(menu_page)) {
        draw_ptext(menu_page == PG_KEYS ? "PLAYER 1'S KEYS. SAVED AS SOON AS SET (CONTROLS_N64.JSON)."
                   : menu_page == PG_PADKEYS ? "THE DEFAULT FOR EVERY PLAYER'S PAD. SAVED AS SOON AS SET."
                   : "HIS OWN PAD. YELLOW: HIS OWN ROWS, GREY: THE DEFAULT'S (ALL).", -1, 380, 1, MC(16, 16, 16));
        draw_ptext("SPECIAL: HELD, THE STICK PUSHED THE D-PAD'S WAY (UP WITH NONE)", -1, 394, 1, MC(16, 16, 16));
        if (arc_controls) draw_ptext("6-BUTTON: THE STICK MOVES HIM; CABINET SPECIAL = C-RIGHT", -1, 408, 1, MC(16, 16, 16));
        if (menu_note_frames > 0 && menu_note) draw_ptext(menu_note, -1, 434, 1, MC(31, 31, 10));
        int whole = menu_sel < n && a[menu_sel].type == MI_BIND && a[menu_sel].arg == BIND_STICK;
        draw_ptext(whole && bind_page_dev() >= 1 ? "LEFT/RIGHT: LEFT OR RIGHT STICK  ENTER: MOVE ONE  ESC: BACK"
                   : whole ? "ENTER: ITS FOUR KEYS IN TURN  DELETE: CLEAR  ESC: BACK"
                   : "UP/DOWN: SELECT  ENTER: SET  DELETE: CLEAR  ESC: BACK", -1, 456, 1, MC(24, 24, 24));
        return;
    }
    if (menu_note_frames > 0 && menu_note) draw_ptext(menu_note, -1, 434, 1, MC(31, 31, 10));
    draw_ptext("UP/DOWN: SELECT  LEFT/RIGHT: CHANGE  ESC: BACK", -1, 456, 1, MC(24, 24, 24));
}

// The chapter card (championship, money, Continue / Save) skipped -- the abbreviated menus' Story shortcut, both modes
// (Arcade's own since 0.2; Regular's with its ABBREVIATED MENUS on, phase 3). While the screen is fully black between
// the superstar select fading out and the card fading in: its Continue, the sequencer step Continue leads to (41), and
// that step's exit as the step makes it after its fade-out (ovl_a func_800EA774): on to the story scene, the card never
// drawn. After a match the card comes up with the engine at phase 7 (0x80097ED6): the money won being counted up
// (func_800EF190, A finishes it), then phase 8 plays the scene after the match (0x80097EE8). Skipped at phase 7, the
// step went to the scene the last one loaded (the one before the match) and back round to the results, for ever: a story
// of ours waits for the count (A, as a player's), then goes on as from any card. (Seen in a story of ours, QA
// 2026-10-28.) Called each frame the card is up; card_confirmed is cleared by the caller off the card.
static void story_card_skip(s16 scr, s32 step) {
    if (!card_confirmed && scr_stable >= 4 && RD16(0x80097ED6u) == 7 && story_active()) {
        if (inject_frames == 0 && ++card_tally % 20 == 0) inject_press(BTN_A);
    } else if (!card_confirmed && scr_stable >= 4) {
        dbg_card_next = menu_confirm(scr, 0);
        card_confirmed = 1;
        if (step != 44) RDS32(NM_SCENE_STEP) = 41;   // where Continue takes the sequencer
        RDS32(NM_MODE) = -8;
        RDS16(NM_RETURN_SCREEN) = SCR_STORY_CARD;
        RD32(0x800A9444u) = 0;
        scr_stable = 0;
    }
}

// ---- the match's pace ---------------------------------------------------------------------------------
// The match loop (func_800D9AB0) reads the controllers, runs one step of the match's logic
// (func_80114514) and draws, once a frame: 30 frames a second at most (the graphics thread waits for
// two retraces a frame). Everything in a match is timed in those steps -- moves, the AI, the clock,
// the Rumble's countdown -- so the game is as fast as its frames. The arcade's pace gives a match
// more steps than frames: every 100 / boost frames the match takes a second step before the frame is
// drawn (+15%: about one frame in seven). The pictures and their pace stay the game's: the logic
// works out the wrestlers' bones itself (func_80012130), and the graphics thread takes the frame's
// matrices as it receives the frame (func_80002730), before the match loop goes on. The music and
// the crowd play at their own pace.
#define NM_MATCH_STATE  0x801591FCu   // s32: func_80114514's state (2: the match is on)
#define NM_MATCH_SCENE  0x80162A7Cu   // bit 1: a scene runs in the match's place (func_8013140C)
#define NM_PAD_STATE    SIXA_8009EDA0   // per controller, 0xC each: +4 held, +6 newly pressed, +8 let go (func_80005818)
int arcade_fast_getup(void) { return fast_getup; }
int arcade_rumble_ai_on(void) { return rumble_ai; }
static int step_acc;                  // percent of a step owed
static int step_extra;                // (the extra step is running)
u32 dbg_match_steps, dbg_match_extra; // tests: the match loop's steps, and the extra ones
static int match_on(void) {
    s32 m = RDS32(NM_MODE);
    return (m == -2 || m == -6) && RDS32(NM_MATCH_STATE) == 2 && !(RD32(NM_MATCH_SCENE) & 2) &&
           RD8(NM_MATCH_DONE) == 0 && RDS16(NM_MATCH_PAUSED) == 0;
}
void arcade_match_step(void) {
    if (match_on()) { extern void movex_chain_step(void); movex_chain_step(); }   // (after every step, the extra ones too)
    { extern int ms_live; extern void match_slots_step(void); if (ms_live) match_slots_step(); }   // (any32: slots.c)
    if (step_extra) return;
    if (!match_on()) { step_acc = 0; return; }
    dbg_match_steps++;
    // (GAME SPEED: the running mode's -- W9: on by default in Arcade, off in Regular)
    if (!arcade_speed) { step_acc = 0; return; }
    step_acc += speed_boost;
    if (step_acc < 100) return;
    step_acc -= 100;
    // The second step sees the controllers as they're held: nothing newly pressed or let go (the
    // first step had those), so a press is one press however many steps its frame has. The rest of
    // the frame gets them back (the match loop reads Start for its pause menu after the logic).
    u16 edges[4][2];
    for (u32 i = 0; i < 4; i++) {
        edges[i][0] = RD16(NM_PAD_STATE + 0xCu * i + 6u); RD16(NM_PAD_STATE + 0xCu * i + 6u) = 0;
        edges[i][1] = RD16(NM_PAD_STATE + 0xCu * i + 8u); RD16(NM_PAD_STATE + 0xCu * i + 8u) = 0;
    }
    step_extra = 1;
    dbg_match_extra++;
    plat_game_call(GF_MATCH_STEP, 0, 0, 0);
    step_extra = 0;
    for (u32 i = 0; i < 4; i++) {
        RD16(NM_PAD_STATE + 0xCu * i + 6u) = edges[i][0];
        RD16(NM_PAD_STATE + 0xCu * i + 8u) = edges[i][1];
    }
}

// ---- story pages ------------------------------------------------------------------------------------
// Called each time a story page (ovl_a's func_800E8A14) waits for a confirm. Like an arcade
// cutscene, a page moves on by itself after a few seconds (A / B still move it on sooner).
static u32 page_last;
static int page_frames;
int arcade_story_page(void) {
    if (dbg_passive) return 0;
    if (nm_arcade()) {
        if (goal == G_VERSUS && leave_fast && goal_wait <= 0) return 1;     // a challenger: on, at once
        if (!short_menus) return 0;                                         // (ABBREVIATED MENUS off: the player's A, W9)
        if (phase != PLAY || game_mode != GM_STORY || cont_player >= 0 || goal != G_NONE) return 0;
    } else if (!short_menus) return 0;             // (Regular: the pages wait for the player unless ABBREVIATED MENUS, W9)
    if (frame_no - page_last > 6) page_frames = 0;
    page_last = frame_no;
    if (++page_frames < 330) return 0;
    page_frames = 0;
    return 1;
}


// ---- the arcade's words in the game's own lettering ---------------------------------------------
// The menus draw a sprite queue (ovl_a func_800DD5A4 requests, ovl_c func_80117A84 draws them):
// a sprite requested in a frame shows (fading in at the speed in its flags' low bits) and fades
// out once it isn't requested any more. A string pointer as the sprite's id is rasterized by the
// game in its menu font. The title's PRESS START is sprite 15 (requested by the title itself).
#define SPR_PRESS_START 0x1000000Fu
#define SPR_CENTER      0x0C00u        // centred on x, y
#define SPR_FADE_SOFT   0x0002u        // fades in and out over about half a second
#define SPR_FADE_NONE   0x0001u
#define NM_SPRITE_FADES 0x8010581Cu    // -> 0x60 x { request*, s16 alpha, u16 flags, s32 requested }

static void sprite_hide(u32 id) {
    u32 t = RD32(NM_SPRITE_FADES);
    if (!GAME_PTR(t)) return;
    for (int i = 0; i < 0x60; i++) {
        u32 e = t + (u32)i * 12u;
        u32 req = RD32(e);
        if (!GAME_PTR(req)) continue;
        if (RD32(req + 8) != id) continue;
        RDS16(e + 4) = 0;     // alpha
        RD32(e + 8) = 0;      // not requested this frame
    }
}




// (QA, phase 3: the title's PRESS START -- the menus' loop requests sprite 15, func_800DD5A4(0xF, 240, 200, 0, 0xC01),
//  each frame the title's idle count isn't negative; the queue (func_80117A84) draws each sprite it has with its alpha,
//  func_80117714, then takes a fade step off it. Counted here a second at a time: requests, draws, the highest alpha.)
static int qa_press_start_ok;
static u32 qa_ps_req, qa_ps_draws, qa_ps_alpha, qa_ps_shown, qa_ps_frame;
// (and its own blink: the queue's entry 15 is drawn by func_80117630, which pulses its own counter at 0x8014A5AC
//  1..40 and back -- direction flag 0x8014A5B0 -- and draws picture 0x458B through func_80114294 at count x 8 alpha
//  (255 from 33): the lowest and highest count seen in the second)
static u32 qa_ps_cmin = 0xFFFFFFFFu, qa_ps_cmax;
void arcade_qa_sprite_request(u32 id) { if (qa_modes && (id == 0xF || id == SPR_PRESS_START)) qa_ps_req++; }
void arcade_qa_sprite_draw(u32 req, s32 alpha) {
    if (!qa_modes || !GAME_PTR(req) || RD32(req + 8) != SPR_PRESS_START) return;
    qa_ps_draws++;
    if ((u32)alpha > qa_ps_alpha) qa_ps_alpha = (u32)alpha & 0xFFu;
    u32 c = RD32(0x8014A5ACu);
    if (c < qa_ps_cmin) qa_ps_cmin = c;
    if (c > qa_ps_cmax) qa_ps_cmax = c;
}
static void qa_press_start_second(void) {
    if (!qa_modes) return;
    if (frame_no - qa_ps_frame < 60) return;
    qa_ps_frame = frame_no;
    // (the blink draws picture file 0x458B, which it gets from the menus' sprite cache -- func_800DF3BC: 128 entries of
    //  0x10 at 0x80104D30, { id, data, uses, flags }, id -1 free; bit 0 of the flags: its file still to load -- and draws
    //  nothing while that gives 0: an id with no entry takes a free one, none free -> never. So: the free entries, and
    //  0x458B's entry -- 1 has one | its load still pending << 1 | its data there << 2 | its uses (up to 31) << 3)
    u32 free_n = 0, pic = 0;
    for (u32 e = 0x80104D30u; e < 0x80104D30u + 128u * 0x10u; e += 0x10u) {
        u32 id = RD32(e);
        if (id == 0xFFFFFFFFu) free_n++;
        else if (id == 0x458Bu) pic = 1u | (RD32(e + 0xC) & 1u) << 1 | (u32)(RD32(e + 4) != 0) << 2 | (RD32(e + 8) > 31u ? 31u : RD32(e + 8)) << 3;
    }
    // (rr_next: the free entries | 0x458B's entry << 8 | its draws << 16 | the blink's highest count << 24 -- phase 3:
    //  the queue's own alpha, 255 every frame, said nothing of what showed)
    qa_ps_shown = (free_n & 0xFFu) | pic << 8 | (qa_ps_draws & 0xFFu) << 16 | (qa_ps_cmax & 0xFFu) << 24;
    qa_press_start_ok = 1;
    qa_ps_req = qa_ps_draws = qa_ps_alpha = 0; qa_ps_cmin = 0xFFFFFFFFu; qa_ps_cmax = 0;
}

extern int dbg_passive;
void arcade_menu_sprites(void) {
    if (dbg_passive || !nm_arcade()) return;       // (Arcade only: Regular's title keeps its own PRESS START)
    if (!title_loaded() || RDS32(NM_MODE) != 0) return;
    // attract mode says INSERT COIN / PRESS START itself, the same everywhere (arcade_frame): the
    // title's own blinking PRESS START is left out
    if (phase == ATTRACT && RDS16(NM_SCREEN) == SCR_TITLE) sprite_hide(SPR_PRESS_START);
    if (RDS16(NM_SCREEN) == SCR_TITLE)
        for (int i = 0; i < n_says; i++)
            plat_sprite_request((u32)says[i].s, says[i].x, says[i].y, says[i].style, SPR_CENTER | SPR_FADE_NONE);
}

// Everything unlocked: the hidden Superstars, every SmackDown Mall item (attire, moves, the rest)
// and what the championships unlock are one bit array of the game's (0x800BD910, 25 bytes; ovl_a
// func_800D9A10 sets a bit, func_800D9A5C tests one, func_800D9DA8 -- never called -- sets them
// all). The save keeps its own copy (image + 0x6B67) and copies it back over the game's when a save
// is loaded or made, so both are kept at all-on: the game's own flags, as a game with everything
// bought has them (a vanilla game reads the save the same way).
#define NM_UNLOCKS      0x800BD910u
#define NM_UNLOCK_BYTES 25u
#define SAVE_IMAGE_PTR  0x80062C20u
#define SAVE_UNLOCKS    0x6B67u
static void unlock_everything(void) {
    if (!unlock_all) return;
    for (u32 i = 0; i < NM_UNLOCK_BYTES; i++) RD8(NM_UNLOCKS + i) = 0xFF;
    if (!in_menus()) return;                         // (the save image: only where the game keeps it, the menus)
    u32 img = RD32(SAVE_IMAGE_PTR);
    if (GAME_PTR(img))
        for (u32 i = 0; i < NM_UNLOCK_BYTES; i++) RD8(img + SAVE_UNLOCKS + i) = 0xFF;
}

// ---- main per-frame logic --------------------------------------------------------------------------
// (LOW ENTRANCE CAMERA, the running mode's toggle -- W9: on in Arcade, off in Regular by default. Arcade: a paid game's
// entrances or a QA bench's; Regular: any real match's -- not an attract demo's)
int arcade_entrance_cam(void) {
    if (!stage_cam_on) return 0;
    if (nm_arcade()) return phase == PLAY || qa_on;
    return qa_on || !(RD32(NM_MATCH_FLAGS) & NM_FLAG_ATTRACT);
}

// ---- QA scenarios (Development\qa.ps1) ------------------------------------------------------------------
// With the hidden option qa_mode on, the arcade is a test bench: no coins, no attract loop. The title's demo
// match is set up as asked -- qa_w1 / qa_w2 the Superstars (0: anyone), qa_c1 / qa_c2 their costumes,
// qa_arena (1..10; 0: any) -- and made a real match the way the first arcade did it: qa_ctl1 / qa_ctl2 the
// controller on each slot (0: the CPU), the HUD, no time limit. qa_special (bit per slot) fills SPECIAL at
// the bell. After qa_matches matches (0: no end) the game quits. What it's doing goes into QA_STATE every
// frame; the exe writes it out (WCW_QA_STATE).
#define QA_STATE 0x807F3000u   // 'QAST', then the state (u32s); per slot 0x40 bytes from +0x40
enum { QA_TITLE, QA_DEMO, QA_MATCH };
static int qa_stage, qa_done, qa_special_done, qa_last_frames, qa_quit_in, qa_over_frames;
__attribute__((unused)) static int qa_ring(void) {
    int n = (qa_done == 0 && qa_rr_ring_first) ? qa_rr_ring_first : qa_rr_ring;
    return (n >= 4 && n <= NM_SLOTS_CAP) ? n : 0;
}
static u16 qa_a, qa_b;           // this match's Superstars (the first two)
// The demo sets (func_800EF478, by NM_ATTRACT_COUNTER / 2): their records in the demo table (0x80102AE4) and the
// format's men -- 0 tag (0x42), 1 singles, 2 singles 0x11, 3 singles, 4 handicap 0x48, 5 the Rumble (not one of
// these), 6 tag (0x2), 7 triple threat (0x4), 8 handicap (0x8)
static const u8 qa_set_first[9] = { 0, 3, 11, 16, 17, 18, 19, 27, 30 }, qa_set_end[9] = { 3, 11, 16, 17, 18, 19, 27, 30, 33 };
static const u8 qa_set_men[9] = { 4, 2, 2, 2, 3, 0, 4, 3, 3 };
static u16 qa_men[4];            // this match's Superstars, every one of the format's
static u16 qa_pick(int k, u16 other) {
    if (qa_w[k] > 0 && qa_w[k] < 0x1000) return (u16)qa_w[k];
    u16 id;
    do id = roster_id[rng() % ROSTER_N]; while (id == other);
    return id;
}
// The game's load_rules_preset, func_800E2B44(e, dst) -- e's 12 bytes from 0x800F5E78 + 12e (the title overlay's data) to
// dst, or from e 0x4B on a save slot's rules. The hook's start notes which and where; its return:
//  - a new save's Royal Rumble slot (e 13 into rows that are not the RULES page's: fill_save_with_defaults' own) gets the
//    mod's Rumble defaults (rr_rules_default, above -- rumblemenu 2026-11-25);
//  - a QA match's rules (qa_rules, the scenario's "rules"): the demo puts a preset of its own into the RULES page's rows;
//    when a scenario asked for rules, the scenario's preset goes there in their place (the same table, the same step).
#ifdef LOGIC_HOOKS
static u32 qa_preset_dst;
static s32 rules_preset_e;
RECOMP_HOOK("func_800E2B44") void qa_hook_preset(s32 e, u32 dst) { qa_preset_dst = dst; rules_preset_e = e; }
RECOMP_HOOK_RETURN("func_800E2B44") void qa_hook_preset_done(void) {
    if (rules_preset_e == RULES_SLOT_RUMBLE && (qa_preset_dst | 0x80000000u) != NM_RULE_ROWS && GAME_PTR(qa_preset_dst)) {
        for (u32 i = 0; i < 12; i++) RD8(qa_preset_dst + i) = rr_rules_default[i];
        dbg_rumble_rules_default++;
        return;
    }
    if (qa_on != 1 || qa_rules <= 0 || (qa_preset_dst | 0x80000000u) != NM_RULE_ROWS) return;
    for (u32 i = 0; i < 12; i++) RD8(NM_RULE_ROWS + i) = RD8(0x800F5E78u + 12u * (u32)qa_rules + i);
}
// (and its set-up flags outright, when a scenario gives them: after the demo routine has written its set's)
RECOMP_HOOK_RETURN("func_800EF478") void qa_hook_demo_flags(void) {
    if (qa_on == 1 && qa_setup_flags) RD32(0x800A957Cu) = qa_setup_flags;
    // (guestref, 2026-11-22: a Guest Referee kind -- 0x80 with 1 single, 4 triple threat, 8 handicap, as the menus' MATCH rows
    //  give it -- needs the match's flags the menus' match start builds from the kind (set_match_flags_from_kind,
    //  func_80135EE8): the demo routine built them already, from its own set's kind. Its format bits are put right here
    //  the way that function makes them for a 0x80 kind -- 0x80000 the referee match, with 0x1400 / 0x8800 / 0x20840 by
    //  the format -- the computer-player bits (0xF) and the demo's own (the top byte) kept. The rules are the same as the
    //  demo set's: the function's rules part treats these kinds alike. QA only: nothing here outside a QA match.)
    if (qa_on == 1 && (qa_setup_flags & 0x80u)) {
        u32 f = RD32(NM_MATCH_FLAGS) & 0xFF00000Fu;
        f |= 0x80000u | ((qa_setup_flags & 1u) ? 0x1400u : (qa_setup_flags & 4u) ? 0x8800u : (qa_setup_flags & 8u) ? 0x20840u : 0u);
        RD32(NM_MATCH_FLAGS) = f;
    }
}
#endif
static void qa_demo_setup(void) {
    // (as logic_v1's: the demo becomes a real match)
    // (guestref, 2026-11-22: every one of the format's slots' computer bit cleared first, not only the first two's -- a
    //  controller on slot 2 or 3 (a guest referee, a triple threat's third) was left the computer's: its bit stayed set)
    u32 flags = RD32(NM_MATCH_FLAGS) & ~(NM_FLAG_DEMO | ((1u << qa_set_men[qa_demo_set]) - 1u) | 3u);
    for (int s = 0; s < qa_set_men[qa_demo_set]; s++) {   // (the format's slots: 2, 3 or 4)
        int c = qa_ctl[s] == 1 || qa_ctl[s] == 2 ? qa_ctl[s] : 0;
        RDS32(NM_SLOT_PAD + 4u * (u32)s) = c;
        if (!c) flags |= 1u << s;
    }
    RD32(NM_MATCH_FLAGS) = flags;
    int qa_ar_n = qa_done >= 1 && qa_arena2 >= 1 ? qa_arena2 : qa_arena;   // (the second match on: qa_arena2's)
    if (qa_ar_n >= 1 && qa_ar_n <= arena_count()) { arena_choose(qa_ar_n - 1); RD8(NM_VENUE) = (u8)arena_venue(qa_ar_n - 1); }
    else { int a = arena_random(rng()); arena_choose(a); RD8(NM_VENUE) = (u8)arena_venue(a); }   // (firstrun: arena.c arena_random)
    // the controllers reach the game as a player's would (the controller hook's pass)
    active[0] = qa_ctl[0] == 1 || qa_ctl[1] == 1 || qa_ctl[2] == 1 || qa_ctl[3] == 1;
    active[1] = qa_ctl[0] == 2 || qa_ctl[1] == 2 || qa_ctl[2] == 2 || qa_ctl[3] == 2;
    port_player[0] = 0; port_player[1] = 1;
    qa_stage = QA_MATCH; qa_special_done = 0;
    // Changed for new features (Ultimate No Mercy) -- any32, 2026-11-18: QA extra wrestlers present in this match
    // (slots.c match_slots_add): the core roster in turn, past the four the demo set has, then again in their second look
    if (qa_extras > 0) {
        u16 ids[NM_SLOTS_CAP]; u8 looks[NM_SLOTS_CAP]; int n = 0;
        for (int k = 0; k < 4 * ROSTER_N && n < qa_extras && n < NM_SLOTS_CAP; k++) {
            u16 id = roster_id[k % ROSTER_N]; int used = 0;
            for (int j = 0; j < 4; j++) if (k < ROSTER_N && j < qa_set_men[qa_demo_set] && qa_men[j] == id) used = 1;
            if (used) continue;
            ids[n] = id; looks[n] = (u8)(k / ROSTER_N); n++;
        }
        extern int match_slots_add(int n, const u16* ids, const u8* looks, int rule, int wait);
        match_slots_add(n, ids, looks, qa_extras_rule, qa_extras_wait * 30);
    }
    // New feature (Ultimate No Mercy) -- lumberjack, 2026-11-21: a QA Lumberjack match (qa_lumberjacks): the one-on-one
    // with its lumberjacks drawn as the menus' would be (lumberjack.c; the records' hook asks again over each build)
    if (qa_lumberjacks > 0) { lumberjack_arm_qa(qa_lumberjacks); lumberjack_on_records(); }
}
// A wrestler going into a moveset entry (func_800EC698's start, gen/patches.py): a QA scenario's forced move (slot
// qa_fslot - 1's entries qa_ffrom..qa_fto taken as qa_fas -- the rig's test_ec698, in the real game); otherwise the
// entry as it is
// qa_bsuplex (a Royal Rumble): Bret's back suplex from a back grapple, the way the user's Rumbles broke (2026-10-27),
// over and over, the game's own way. The CPU fights for him. Every few seconds, him and a man in the ring both standing
// (the man dazed if there's one: he can't turn round), he's put right behind that man facing as he faces -- a back
// grapple's place: the two 55 apart, one facing (a front one is 73 apart, face to face; a man faces (sin f, cos f),
// f = facing * 2 pi / 4096) -- and the CPU grabs him itself. Whenever he holds a man from behind (him 80, the man 34 /
// 35, near, facing as he does), the move the CPU picks is his entry 73, the Falling Back Drop (his Revenge animation).
// (Pressing A for him, his controller taken from the CPU and given back, didn't grab: tried, 2026-10-27.)
#define QA_BS_ENTRY 73
#define QA_F32(a) (*(volatile float*)(a))
static int qa_bs_wait = 300, qa_bs_slot = -1, qa_bs_held;
static u32 qa_bs_tries, qa_bs_grabs, qa_rr_outs;
static float qa_sin(float a) {   // (radians, |a| <= 2 pi)
    const float pi = 3.14159265f;
    while (a > pi) a -= 2.0f * pi;
    while (a < -pi) a += 2.0f * pi;
    if (a > pi * 0.5f) a = pi - a; else if (a < -pi * 0.5f) a = -pi - a;
    float a2 = a * a;
    return a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
}
static int qa_fbad(u32 a) { return (RD32(a) & 0x7F800000u) == 0x7F800000u; }   // (not a number)
// standing (1, 3), or for the man grabbed staggered too (34, 35: the states the user's Rumbles had them in, just
// before Bret's back suplex), in the ring (inside the ropes: |x|, |z| < 205; 0, 0 is an entrant not in yet)
static int qa_bs_free(u32 w, int dazed_ok) {
    u32 st = RD32(w + 0x14) & 0xFFFFu;
    if (qa_fbad(w + 0xF4) || qa_fbad(w + 0xFC)) return 0;
    float x = QA_F32(w + 0xF4), z = QA_F32(w + 0xFC);
    int ok = st == 1 || st == 3 || (dazed_ok && (st == 34 || st == 35));
    return ok && x > -205.0f && x < 205.0f && z > -205.0f && z < 205.0f && (x != 0.0f || z != 0.0f);
}
// slot s holding a man from behind: him 80, a man 34 / 35 within 70, facing within 30 degrees of him
static int qa_bs_back_grapple(int s) {
    u32 w = W(s);
    if ((RD32(w + 0x14) & 0xFFFFu) != 80 || qa_fbad(w + 0xF4) || qa_fbad(w + 0xFC)) return 0;
    for (int v = 0; v < nm_slots(); v++) {
        u32 vw = W(v), vst = RD32(vw + 0x14) & 0xFFFFu;
        if (v == s || (vst != 34 && vst != 35) || qa_fbad(vw + 0xF4) || qa_fbad(vw + 0xFC)) continue;
        float dx = QA_F32(vw + 0xF4) - QA_F32(w + 0xF4), dz = QA_F32(vw + 0xFC) - QA_F32(w + 0xFC);
        s32 df = (RDS32(vw + 0x104) - RDS32(w + 0x104)) & 4095;
        if (dx * dx + dz * dz < 70.0f * 70.0f && (df < 341 || df > 4096 - 341)) return 1;
    }
    return 0;
}
static void qa_bs_frame(int s) {
    u32 w = W(s);
    qa_bs_slot = s;
    int held = qa_bs_back_grapple(s);
    if (held && !qa_bs_held) qa_bs_grabs++;
    qa_bs_held = held;
    if (--qa_bs_wait > 0 || !qa_bs_free(w, 0)) return;
    // (a dazed man first, then anyone standing)
    for (int pass = 0; pass < 2; pass++) for (int v = 0; v < nm_slots(); v++) {
        u32 vw = W(v), vst = RD32(vw + 0x14) & 0xFFFFu;
        if (v == s || RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)v) != -1 || !qa_bs_free(vw, 1)) continue;
        if (pass == 0 && vst != 34 && vst != 35) continue;
        s32 fc = RDS32(vw + 0x104);
        float f = (float)fc * (6.2831853f / 4096.0f), sn = qa_sin(f), cs = qa_sin(f + 1.5707963f);
        float x = QA_F32(vw + 0xF4) - 55.0f * sn, z = QA_F32(vw + 0xFC) - 55.0f * cs;
        // (where he is, and the game's other copies of it -- +0x118 the last frame's, +0x124.. five before that:
        // left as they were he slid there over a few frames)
        QA_F32(w + 0xF4) = x; QA_F32(w + 0xFC) = z;
        QA_F32(w + 0x118) = x; QA_F32(w + 0x120) = z;
        for (u32 k = 0x124; k <= 0x154; k += 12) { QA_F32(w + k) = x; QA_F32(w + k + 8) = z; }
        RDS32(w + 0x104) = fc;
        qa_bs_tries++; qa_bs_wait = 120;
        return;
    }
    qa_bs_wait = 20;
}

// qa_table (QA singles): the user's announce-table climb (2026-10-30: running up onto the table at its side, by JR and
// King, left him up at its height, walking through every wall, the crowd, the ring). Every 10 s slot 0 (the CPU's), standing,
// is put on the floor beside the table -- at the announcers' depth (z -650) or at its front (z -500), from the right or
// the left in turn -- facing it, and runs at it (C-up, the d-pad toward it) for 45 frames; the CPU's own after.
// Called from the computer player's hook (gameplay.c arcade_rumble_ai): 1 -- held / pressed are his buttons.
// qa_headgrab (QA singles, the user, 2026-10-30: "a CPU player ONLY goes for that downed man at the head"): slot 0 (the
// CPU's) is put at the head of slot 1 when he lies on the mat (states 49 / 50), facing as he does, and grapples (A) -- once
// at a time; with SPECIAL kept full (qa_special 5) the game's pick there is his ground special
// (qa_fsub 4 counts it: "forced" entry 155)
static int qa_headgrab_input(u16* held, u16* pressed) {
    static int t, wait = 120;
    static float px, py, pz; static s32 pf;
    u32 me = W(0), v = W(1);
    u32 st = RD32(me + 0x14) & 0xFFFFu, vst = RD32(v + 0x14) & 0xFFFFu;
    if (t > 0) {
        // (held at the spot a moment -- the computer player's walk, begun before he was put there, carried him off
        // -- then a press of A, standing still)
        t--;
        if ((vst != 49 && vst != 50) || (st != 1 && st != 2 && st != 3)) { t = 0; return 0; }
        QA_F32(me + 0xF4) = px; QA_F32(me + 0xF8) = py; QA_F32(me + 0xFC) = pz;
        QA_F32(me + 0x118) = px; QA_F32(me + 0x11C) = py; QA_F32(me + 0x120) = pz;
        for (u32 b = 0x124; b <= 0x154; b += 12) { QA_F32(me + b) = px; QA_F32(me + b + 4) = py; QA_F32(me + b + 8) = pz; }
        RDS32(me + 0x104) = pf;
        u16 bt = qa_headgrab == 2 ? BTN_B : BTN_A;          // (2: a strike at his head, B)
        if (t == 2 || t == 1) { *held = bt; if (t == 2) *pressed = bt; }
        return 1;
    }
    if (--wait > 0) return 0;
    if ((vst != 49 && vst != 50) || (st != 1 && st != 2 && st != 3)) return 0;
    if (qa_fbad(v + 0xF4) || qa_fbad(v + 0xF8) || qa_fbad(v + 0xFC)) return 0;
    s32 f = RDS32(v + 0x104) & 4095;
    float a = (float)f * (6.2831853f / 4096.0f), sn = qa_sin(a), cs = qa_sin(a + 1.5707963f);
    // (at his head: where the user's own grapples there stood him -- 130 back along the way the man faces, facing
    // as he does; 55 away, on top of him, the A picked him up)
    px = QA_F32(v + 0xF4) - 110.0f * sn; py = QA_F32(v + 0xF8); pz = QA_F32(v + 0xFC) - 110.0f * cs; pf = f;
    t = 24; wait = 150;
    return 0;
}
// qa_pspecial (QA singles; the user, 2026-11-01, playing Bret: "He drops the opponent and does the sharpshooter and is
// still misaligned"): slot 0 is a player's (control pad1), and his controller is pressed as a player's is -- at the
// controller read (arcade_pad_hook, before the arcade's controls and the game see it): with SPECIAL up (qa_special 5)
// and the man near and in front of him, a tap of A (the grab), then his special -- & 3 == 1: the stick pushed (the
// game's own controls), 2: C-right (the arcade's SPECIAL: option controls Arcade), 3: no presses (a CPU's own specials:
// state.csv only). Not near in 6 s: he's put 70 in front of the man, facing him (where a grab puts him; the man to the
// ring's middle first when that's outside the ropes). + 16: a camera of its own on the two, round them every 2 s. + 32:
// the man (slot 1) in DANGER.
// state.csv (qa_state): bs_tries = slot 0's animation file | slot 1's << 16; bs_grabs = slot 0's nearest hand piece to
// the man's nearest bone, x 10 | that bone << 16 | the driver's step << 24; bs_step = slot 0's moveset entry | grabs
// tried << 16; rr_put_out = the roots' distance x 10, rr_kept_out = slot 1's facing - slot 0's - 2048 (0: face to face)
static int qps_step, qps_t = 300, qps_wait, qps_tries;
static void qps_put(u32 w, float x, float y, float z, s32 f) {
    QA_F32(w + 0xF4) = x; QA_F32(w + 0xF8) = y; QA_F32(w + 0xFC) = z;
    QA_F32(w + 0x118) = x; QA_F32(w + 0x11C) = y; QA_F32(w + 0x120) = z;
    for (u32 b = 0x124; b <= 0x154; b += 12) { QA_F32(w + b) = x; QA_F32(w + b + 4) = y; QA_F32(w + b + 8) = z; }
    RDS32(w + 0x104) = f;
}
// qa_ringstage (RAW's ring and stage, the player's own path, at the controller read): bit 4 slot 0's player walks: to the middle of
// the aisle's side, out through the ropes (C-up), runs up the aisle and the ramp to the deck. 8: on the deck, grabs and strikes.
// 16: there, whips too. With 2 (the men put in the aisle every 15 s of match): A, then C-down and up -- the whip up the aisle.
extern int ringstage_front(void);
static void qa_ringstage_pad(void) {
    u16 mf = RD16(NM_MATCH_FRAMES);
    s32 m = RDS32(NM_MODE);
    if ((m != -6 && m != -2) || mf == 0xFFFF || mf < 60 || nm_slots() < 2) return;
    u32 me = W(0), pad = NM_PADS + 6u * (u32)port_player[0];
    float x = *(volatile float*)(me + 0xF4u), y = *(volatile float*)(me + 0xF8u), z = *(volatile float*)(me + 0xFCu);
    u16 b = 0;
    int sx = 0, sy = 0;
    if (qa_ringstage & 2) {
        int t = mf % 900;
        if (t >= 320 && t < 332) b |= BTN_A;
        if (t >= 392 && t < 410) b |= BTN_CD;
        if (t >= 540 && t < 780) b |= BTN_DU | BTN_CD;
    } else if (z < (float)ringstage_front() + 60.0f) {
        b |= BTN_DU;
        if (x > 30.0f) b |= BTN_DL; else if (x < -30.0f) b |= BTN_DR;
        if (y >= 60.0f) { if (z > 175.0f && mf % 90 < 10) b |= BTN_CU; }
        else if (z > 300.0f) b |= BTN_CD;
    } else if (qa_ringstage & 8) {
        int t = mf % 240;
        if (t < 10) b |= BTN_A;
        if (t >= 60 && t < 70) b |= BTN_B;
        if ((qa_ringstage & 16) && t >= 120 && t < 130) b |= BTN_A;
        if ((qa_ringstage & 16) && t >= 180 && t < 195) b |= BTN_CD;
    }
    RD16(pad) |= b;
    if (sx) RD8(pad + 2u) = (u8)(s8)sx;
    if (sy) RD8(pad + 3u) = (u8)(s8)sy;
}
static void qa_pspecial_pad(void) {
    u16 mf = RD16(NM_MATCH_FRAMES);
    if (qa_stage != QA_MATCH || RDS32(NM_MODE) != -6 || mf == 0xFFFF || nm_slots() < 2) return;
    u32 me = W(0), v = W(1), pad = NM_PADS + 6u * (u32)port_player[0];
    if (qa_fbad(me + 0xF4) || qa_fbad(me + 0xFC) || qa_fbad(v + 0xF4) || qa_fbad(v + 0xFC)) return;
    float mx = QA_F32(me + 0xF4), mz = QA_F32(me + 0xFC), vx = QA_F32(v + 0xF4), vz = QA_F32(v + 0xFC);
    if (qa_pspecial & 16) {   // (the camera: on the two, from one of three sides, every 2 s the next)
        extern float qa_cam[7];
        float a = (float)((mf / 120u) % 3u) * 2.0943951f + 0.5f, cx = (mx + vx) * 0.5f, cz = (mz + vz) * 0.5f;
        float y = qa_fbad(v + 0xF8) ? 0.0f : QA_F32(v + 0xF8);
        qa_cam[0] = 1.0f;
        qa_cam[1] = cx + 210.0f * qa_sin(a); qa_cam[2] = y + 150.0f; qa_cam[3] = cz + 210.0f * qa_sin(a + 1.5707963f);
        qa_cam[4] = cx; qa_cam[5] = y + 25.0f; qa_cam[6] = cz;
    }
    // (+ 32: the man kept in DANGER at stamina 0 -- the CPU's Hall in 2026-11-01_104430, where the special first looked
    //  wrong. DANGER as func_8011D05C makes it when spirit runs out: spirit 0, +0xEE | 1, its timer 300)
    if (qa_pspecial & 32) {
        RDS16(v + 0xA2) = 0;
        if (!(RD8(v + 0xE6) & 1)) { RDS16(v + 0xA6) = 0; RDS16(v + 0xA8) = 300; RD8(v + 0xE6) |= 1; }
    }
    if ((qa_pspecial & 64) && mf >= 240) {     // (+ 64: every 2.5 s, him standing free, the stick pushed: up, down, left, right in turn: his four standing taunts. The hands' pose: state.csv bs_grabs)
        static int wait = 60, push, n;
        u32 st0 = RD32(me + 0x14) & 0xFFFFu;
        if (push > 0) { push--; RD16(pad) |= BTN_CR; RD8(pad + 2) = (u8)(s8)(n == 2 ? -80 : n == 3 ? 80 : 0); RD8(pad + 3) = (u8)(s8)(n == 0 ? 80 : n == 1 ? -80 : 0); if (!push) { n = (n + 1) & 3; wait = 150; } }
        else if (wait > 0) wait--;
        else if (st0 <= 3u) push = 8;      // (0: standing idle)
    }
    int mode = qa_pspecial & 3;
    if (mode == 3 || mf < 240) return;
    u32 st = RD32(me + 0x14) & 0xFFFFu, vst = RD32(v + 0x14) & 0xFFFFu;
    u16 b = 0; s8 sy = 0;
    if (qps_t > 0) qps_t--;
    switch (qps_step) {
    case 0: {   // (waiting: both standing, SPECIAL up)
        if (qps_t > 0) break;
        int up = (RD8(NM_TB + 0xEE) & 6u) != 0;
        if ((st != 1 && st != 2 && st != 3) || (vst != 1 && vst != 2 && vst != 3 && vst != 34 && vst != 35) || !up) { qps_wait = 0; break; }
        float dx = vx - mx, dz = vz - mz;
        s32 f = RDS32(me + 0x104) & 4095;
        float a = (float)f * (6.2831853f / 4096.0f);
        float ahead = dx * qa_sin(a) + dz * qa_sin(a + 1.5707963f);   // (along the way he faces)
        if (dx * dx + dz * dz < 85.0f * 85.0f && ahead > 0.0f && ahead * ahead > 0.5f * (dx * dx + dz * dz)) {   // (within 45 degrees)
            qps_step = 1; qps_t = 4; qps_tries++; break;
        }
        if (++qps_wait < 360) break;
        // (put in front of him, facing him: the man to the middle first if that's outside the ropes)
        s32 fv = RDS32(v + 0x104) & 4095;
        float av = (float)fv * (6.2831853f / 4096.0f), sn = qa_sin(av), cs = qa_sin(av + 1.5707963f);
        float y = qa_fbad(v + 0xF8) ? QA_F32(me + 0xF8) : QA_F32(v + 0xF8);
        if (vx + 70.0f * sn < -170.0f || vx + 70.0f * sn > 170.0f || vz + 70.0f * cs < -170.0f || vz + 70.0f * cs > 170.0f) {
            vx = 0.0f; vz = 0.0f; qps_put(v, vx, y, vz, fv);
        }
        qps_put(me, vx + 70.0f * sn, y, vz + 70.0f * cs, (fv + 2048) & 4095);
        qa_forced++; qps_wait = 0; qps_t = 8;   // (a moment there, then the check again)
        break; }
    case 1: b = BTN_A; if (qps_t == 0) { qps_step = 2; qps_t = 45; } break;          // (a tap of A: the grab)
    case 2: if (st == 80 || st == 81) { qps_step = 3; qps_t = 10; }                   // (holding him)
            else if (qps_t == 0) { qps_step = 0; qps_t = 120; } break;
    case 3: if (qps_t == 0) { qps_step = 4; qps_t = 5; } break;
    case 4: if (mode == 2) b = BTN_CR; else sy = 80;                                    // (the special)
            if (qps_t == 0) { qps_step = 0; qps_t = 900; } break;                       // (15 s: the move played out)
    }
    if (b) RD16(pad) |= b;
    if (sy) RD8(pad + 3) = (u8)sy;
}
static void qa_pspecial_state(void) {
    u32 m0 = (u16)RDS16(W(0) + 0x9E), m1 = (u16)RDS16(W(1) + 0x9E);
    RD32(QA_STATE + 0x220) = m0 | m1 << 16;
    // (slot 0's hand pieces -- 9 10 17 18: palm and fingers -- to the man's bones 1-19, their world places, bone + 0x8C)
    u32 b0 = SIXA_80091CC0, b1 = SIXA_80091CC0 + 0x1810u;
    static const u8 hand[4] = { 9, 10, 17, 18 };
    float best = 1e9f; u32 bi = 0;
    for (u32 i = 0; i < 4; i++) {
        u32 h = b0 + hand[i] * 0xDCu;
        if (qa_fbad(h + 0x8C) || qa_fbad(h + 0x90) || qa_fbad(h + 0x94)) continue;
        for (u32 k = 1; k < 20; k++) {
            u32 q = b1 + k * 0xDCu;
            if (qa_fbad(q + 0x8C) || qa_fbad(q + 0x90) || qa_fbad(q + 0x94)) continue;
            float dx = QA_F32(q + 0x8C) - QA_F32(h + 0x8C), dy = QA_F32(q + 0x90) - QA_F32(h + 0x90), dz = QA_F32(q + 0x94) - QA_F32(h + 0x94);
            float d = dx * dx + dy * dy + dz * dz;
            if (d < best) { best = d; bi = k; }
        }
    }
    float d = 0.0f;
    if (best < 1e8f) { d = best; float r = d > 1.0f ? d * 0.5f : 1.0f; for (int i = 0; i < 12; i++) r = 0.5f * (r + d / r); d = r; }
    u32 dh = best < 1e8f ? (u32)(d * 10.0f) : 0xFFFFu;
    RD32(QA_STATE + 0x224) = (dh > 0xFFFFu ? 0xFFFFu : dh) | bi << 16 | (u32)qps_step << 24;
    RD32(QA_STATE + 0x228) = (u16)RDS16(W(0) + 0x300) | (u32)qps_tries << 16;
    if (qa_pspecial & 64) { extern u32 hd_dbg_pose(void); RD32(QA_STATE + 0x224) = hd_dbg_pose(); }
    if (!qa_fbad(W(0) + 0xF4) && !qa_fbad(W(1) + 0xF4) && !qa_fbad(W(0) + 0xFC) && !qa_fbad(W(1) + 0xFC)) {
        float dx = QA_F32(W(1) + 0xF4) - QA_F32(W(0) + 0xF4), dz = QA_F32(W(1) + 0xFC) - QA_F32(W(0) + 0xFC), q = dx * dx + dz * dz, r = q > 1.0f ? q * 0.5f : 1.0f;
        for (int i = 0; i < 12; i++) r = 0.5f * (r + q / r);
        s32 rel = ((RDS32(W(1) + 0x104) - RDS32(W(0) + 0x104) - 2048) & 4095); if (rel > 2048) rel -= 4096;
        RD32(QA_STATE + 0x230) = ((u32)(r * 10.0f) & 0xFFFFu) | (u32)(u16)(s16)rel << 16;
    }
}
int qa_table_input(s32 slot, u16* held, u16* pressed) {
    static int t, k;
    if (qa_headgrab && slot == 0 && (RDS32(NM_MODE) == -2 || RDS32(NM_MODE) == -6) && RD16(NM_MATCH_FRAMES) != 0xFFFF && RD16(NM_MATCH_FRAMES) >= 120)
        return qa_headgrab_input(held, pressed);
    // (a QA singles match is a demo made a real one: mode -6)
    if (!qa_table || slot != 0 || (RDS32(NM_MODE) != -2 && RDS32(NM_MODE) != -6) || RD16(NM_MATCH_FRAMES) == 0xFFFF || RD16(NM_MATCH_FRAMES) < 120) return 0;
    u32 w = W(0);
    if (t > 0) {
        t--;
        *held = k & 1 ? BTN_DR : BTN_DL;                      // (toward the table: from the right, left)
        if (t == 44) *pressed = BTN_CU;
        *held |= BTN_CU;
        return 1;
    }
    static int wait = 300;
    u32 st = RD32(w + 0x14) & 0xFFFFu;
    if (--wait > 0 || (st != 1 && st != 2 && st != 3) || qa_fbad(w + 0xF4) || qa_fbad(w + 0xFC)) return 0;
    k++;
    float x = k & 1 ? -190.0f : 190.0f, z = (k >> 1) & 1 ? -500.0f : -650.0f;
    s32 fc = k & 1 ? 1024 : 3072;                             // (facing (sin f, cos f): +x 1024, -x 3072)
    // (on the floor: y 0 and the area outside -- +0x7A 4; moved from the ring with only x, z, he walked on at the
    // ring's height, 90, everywhere)
    QA_F32(w + 0xF4) = x; QA_F32(w + 0xF8) = 0.0f; QA_F32(w + 0xFC) = z;
    QA_F32(w + 0x118) = x; QA_F32(w + 0x11C) = 0.0f; QA_F32(w + 0x120) = z;
    for (u32 a = 0x124; a <= 0x154; a += 12) { QA_F32(w + a) = x; QA_F32(w + a + 4) = 0.0f; QA_F32(w + a + 8) = z; }
    RD16(w + 0x7A) = 4;
    RDS32(w + 0x104) = fc;
    qa_forced++; t = 45; wait = 200;     // (the computer player's hook isn't every frame: ~10 s)
    return 0;
}

static u32 qa_last_entry;          // QA (qa_headgrab): slot 0's last moveset entry taken | how many << 16 (state.csv bs_step)
// Hollywood Hogan's leg drop (movex.c movex_legdrop_entry; data\legdrop\LEGDROP.md): his ground strike (209 face up, 210
// face down: what B at a lying man takes, the player's or the CPU's) with SPECIAL up (+0xEE 2 lit / 4 SPECIAL!) and him at
// the man's head -> his leg-drop entry, a finisher: the game's strike plays it, the man's reaction by the damage (31 and
// more: its big one, legs up, as from the People's Elbow -- and its replay), the damage from the record (x1.2 with
// SPECIAL!). SPECIAL used up (as func_8013F7F0 would: spirit 50), and the man kept down (his timer +0xB4). (TB offsets:
// w is the game's pointer, W - 8.)
#define LEGDROP_DOWN 300                       // game frames he stays down after it (~10 s)
static u32 legdrop_man(u32 w) {                // the man lying nearest him (state 49 / 50, or down: +0x80 & 8), within 140
    u32 best = 0; float bd = 140.0f * 140.0f;
    for (int i = 0; i < nm_slots(); i++) {
        u32 v = NM_TB + (u32)i * WRESTLER_SIZE;
        if (v == w || RDS16(v + 0xA0) == -1) continue;
        u32 st = RD32(v + 0x14) & 0xFFFFu;
        if (st != 49 && st != 50 && !(RD16(v + 0x80) & 8)) continue;
        if (qa_fbad(v + 0x120) || qa_fbad(v + 0x128)) continue;
        float dx = QA_F32(v + 0x120) - QA_F32(w + 0x120), dz = QA_F32(v + 0x128) - QA_F32(w + 0x128), d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = v; }
    }
    return best;
}
static int legdrop_at_head(u32 w, u32 v) {     // the game's own rule (func_80136240), as a dot product
    float ux = QA_F32(v + 0x120) - QA_F32(w + 0x120), uz = QA_F32(v + 0x128) - QA_F32(w + 0x128);
    float a = (float)(RDS32(v + 0x10C) & 4095) * (6.2831853f / 4096.0f);
    float dot = ux * qa_sin(a) + uz * qa_sin(a + 1.5707963f);
    return (RD16(v + 0x8A) & 1) ? dot < 0.0f : dot > 0.0f;   // (face down: the other end)
}
u32 dbg_legdrops;                               // tests: leg drops taken
static s32 move_entry(u32 w, s32 owner, s32 entry);
// (then a move that goes on into a ground grapple: movex.c movex_chain_pick -- Bret's front special, the slam -> his
// ground Sharpshooter -- on the entry as it comes out)
s32 qa_move_entry(u32 w, s32 owner, s32 entry) {
    extern s32 movex_chain_pick(u32 w, s32 owner, s32 entry);
    return movex_chain_pick(w, (s8)owner, move_entry(w, owner, entry));
}
static s32 move_entry(u32 w, s32 owner, s32 entry) {
    {   extern s32 movex_legdrop_entry(s32 s);
        s32 le = movex_legdrop_entry((s8)owner);
        if (le > 0 && ((s16)entry == 209 || (s16)entry == 210) && (RD8(w + 0xEE) & 6u)) {
            u32 v = legdrop_man(w);
            if (v && legdrop_at_head(w, v)) {
                RD8(w + 0xEE) &= (u8)~6u; RDS16(w + 0xAE) = 50; RDS16(w + 0xB0) = 120;   // SPECIAL used up
                if (RDS16(v + 0xB4) < LEGDROP_DOWN) RDS16(v + 0xB4) = LEGDROP_DOWN;      // he stays down
                dbg_legdrops++;
                return le;
            }
        }
    }
    if (qa_headgrab && (s8)owner == 0) qa_last_entry = ((qa_last_entry >> 16) + 1u) << 16 | ((u32)entry & 0xFFFFu);
    if (qa_bs && (s8)owner == qa_bs_slot && (s16)entry < 292 && qa_bs_back_grapple(qa_bs_slot)) { qa_forced++; return QA_BS_ENTRY; }
    if (qa_fslot > 0 && (s8)owner == qa_fslot - 1 && (s16)entry >= qa_ffrom && (s16)entry <= qa_fto) {
        // (qa_fcycle: a ported Superstar's moves with Revenge animations of their own -- ext_late[qa_fcycle]'s, movex.c --
        // each in turn, one a forced move: every one of his checked whole in a run)
        // (only from a grab that holds a man -- him 80 / 81, a man within 80: the game picks the grapple's move on the
        // grab. Picked standing, Nash's jackknife never started; on a grab that missed (the man 88 away, walking) it
        // played with nobody held -- the man dropped 130 away. Held, the two were 57 .. 76 apart; 2026-10-30)
        u32 me = W(qa_fslot - 1), st = RD32(me + 0x14) & 0xFFFFu;
        int grab = st == 80 || st == 81, near = 0;
        for (int v = 0; v < nm_slots() && !near; v++) {
            u32 vw = W(v);
            if (vw == me || qa_fbad(vw + 0xF4) || qa_fbad(vw + 0xFC)) continue;
            float dx = QA_F32(vw + 0xF4) - QA_F32(me + 0xF4), dz = QA_F32(vw + 0xFC) - QA_F32(me + 0xFC);
            near = dx * dx + dz * dz < 80.0f * 80.0f;
        }
        if (qa_fcycle > 0) {
            if (!grab || !near) return entry;
            extern s32 movex_qa_entry(s32 late, s32 k);
            s32 e = movex_qa_entry(qa_fcycle, (s32)qa_forced); qa_forced++; return e >= 0 ? e : entry;
        }
        // (qa_fsub: his holds, each in turn, forced where the game picks a move of the same place -- a hold on the mat
        // for a move on the mat, a standing one for a grab that holds a man (movex.c movex_qa_subs / _same_place); state
        // "forced": how many | the last one's entry << 16. Bret's Sharpshooter: the man feet away, the user, 2026-10-30)
        if (qa_fsub) {
            extern s32 movex_qa_subs(s32 s, s16* out, s32 max, int specials); extern int movex_qa_same_place(s32 a, s32 b);
            static s16 subs[64]; static s32 nsub = -1, next;   // (the first 24 in QA_STATE + 0x348..0x377)
            if (nsub < 0) {
                extern s32 movex_qa_own(s32 s, s16* out, s32 max);
                if (qa_fsub == 4) { subs[0] = (s16)qa_fas; nsub = 1; }                    // (4: the one entry qa_fas only)
                else nsub = qa_fsub == 3 ? movex_qa_own(qa_fslot - 1, subs, 64)           // (3: his moves with animations of his own)
                                         : movex_qa_subs(qa_fslot - 1, subs, 64, qa_fsub == 2);   // (2: his specials only)
                RD32(QA_STATE + 0x344) = (u32)nsub;
                for (s32 k = 0; k < nsub && k < 24; k++) RD16(QA_STATE + 0x348u + 2u * (u32)k) = (u16)subs[k];
            }
            if (nsub <= 0 || (grab && !near) || (s16)entry >= 292) return entry;
            for (s32 k = 0; k < nsub; k++) {
                s32 t = subs[(next + k) % nsub];
                if (t != (s16)entry && !movex_qa_same_place((s16)entry, t)) continue;
                next = (next + k + 1) % nsub;
                qa_forced = ((qa_forced & 0xFFFFu) + 1u) | (u32)t << 16;
                return t;
            }
            return entry;
        }
        qa_forced++;
        return qa_fas;
    }
    return entry;
}
// QA: the moves the first two slots go into, as they change (match frame, slot, the move +0x30A, its moveset entry
// +0x308, whose moveset +0x304 -- offsets from the game's pointer, W(s) - 8: W(s) + 0x302 / 0x300 / 0x2FC; until
// 2026-10-31 this read W(s) + 0x30A, the record pointer's low half, so the ring never held a move): a ring of 256 at
// QA_STATE + 0x400 (the count at + 0x3FC) -- the struggling slam's moves looked for (2026-10-28)
static void qa_move_log(void) {
    static s16 last[2] = { -2, -2 };
    u16 mf = RD16(NM_MATCH_FRAMES);
    if (mf == 0xFFFF) return;
    for (int s = 0; s < 2; s++) {
        s16 m = RDS16(W(s) + 0x302);
        if (m == last[s]) continue;
        last[s] = m;
        u32 n = RD32(QA_STATE + 0x3FC), at = QA_STATE + 0x400u + 8u * (n & 255u);
        RD32(at) = (u32)mf << 16 | (u32)s << 8 | (RD8(W(s) + 0x2FC) & 0xFFu);
        RD32(at + 4) = (u32)(u16)m << 16 | (u16)RDS16(W(s) + 0x300);
        RD32(QA_STATE + 0x3FC) = n + 1;
    }
}
// QA (qa_camprobe, guestref 2026-11-22): the match camera as the frame gets it, in state.csv's lj_a .. lj_e (words no other
// QA writes outside a Lumberjack match) -- the final camera (0x80158FF4: eye x y z, then the point looked at, as
// func_800E2284 hands it to the frame; arena_ringstage.c CAM_FINAL), each to a whole number:
//   lj_a  eye x | eye z << 16      lj_b  eye y | looked-at y << 16      lj_c  looked-at x | looked-at z << 16
//   lj_d  the camera's mode (0x80159074) | its shot (0x801594D2) << 16
//   lj_e  the special-shot flag (0x80158EA0) | the referee's pin count on screen (0x80153A84) << 16
//   lj_f, lj_g  with qa_gr_p1, player 1's numbers (guestref.c qa_gr_words): lj_f his hits in the ring | outside << 8 | steps
//         a man was pinned while he refereed << 16; lj_g steps he played | steps he was out of the ring << 16; lj_h the
//         refereeing player's target | the man covering << 8 | his state << 16; lj_i his target switches at a pin | his
//         count presses << 16
static void qa_camera_probe(u32 q) {
    s16 c[6];
    for (u32 k = 0; k < 6u; k++) {
        float v = QA_F32(0x80158FF4u + 4u * k);
        c[k] = (v == v && v > -32000.0f && v < 32000.0f) ? (s16)v : (s16)0x7FFF;   // (not a number, or out of range: 0x7FFF)
    }
    RD32(q + 0x00) = (u32)(u16)c[0] | (u32)(u16)c[2] << 16;
    RD32(q + 0x04) = (u32)(u16)c[1] | (u32)(u16)c[4] << 16;
    RD32(q + 0x08) = (u32)(u16)c[3] | (u32)(u16)c[5] << 16;
    RD32(q + 0x0C) = (u32)RD16(0x80159074u) | (u32)RD16(0x801594D2u) << 16;
    RD32(q + 0x10) = (u32)RD16(0x80158EA0u) | (u32)RD16(0x80153A84u) << 16;
    RD32(q + 0x14) = qa_gr_words[0];
    RD32(q + 0x18) = qa_gr_words[1];
    RD32(q + 0x1C) = qa_gr_words[2];
    RD32(q + 0x20) = qa_gr_words[3];
}
static void qa_state(void) {
    if (qa_on) qa_move_log();
    RD32(QA_STATE) = 0x51415354u;   // 'QAST'
    RD32(QA_STATE + 0x1C0) = qa_forced;
    // (the exe's write-watch, WCW_WATCH_MOD: each of the first four slots' part 1 palette + 0xE0 -- in the Royal Rumble
    // freeze, 2026-10-27, 32 bytes written there ran over Jeff Jarrett's display list's end; a ported model's palettes
    // are packed, its lists right after them)
    // (four debug registers, six men: the packed ones first -- a display list within 0x100 of the palette)
    // (from two seconds into a match: the models' own loading before that isn't what's looked for)
    if (qa_on) {
        u32 at[4] = { 0, 0, 0, 0 }, k = 0;
        // (the scenario's own first: offsets into RAM, 0x400..0x7FFFFF -- an option's number is read as a signed 32-bit
        //  one, so a full 0x80xxxxxx address came back as 0x80000000)
        for (int q = 0; q < 2; q++) if (qa_watch[q] >= 0x400u && qa_watch[q] < 0x800000u) at[k++] = (0x80000000u | qa_watch[q]) & ~3u;
        u16 mf = RD16(NM_MATCH_FRAMES);
        if (mf != 0xFFFF && mf > 120)
        for (int pass = 0; pass < 2; pass++)
            for (int s = 0; s < nm_slots() && k < 4; s++) {
                u32 part = SIXA_800A4050 + (u32)s * 0x5B0u + 1u * 0x34u, pal = RD32(part + 0x24), geom = RD32(part + 0x1C);
                u16 id = RD16(W(s) + 0x98);   // (a man in: not an entrant still loading, 1, nor none -- their parts' pointers are the last model's)
                if (id == 0x0001u || id == 0xFFFFu || id == 0) continue;
                if (pal < 0x80000400u || pal >= 0x807F0000u) continue;
                int packed = geom > pal && geom - pal < 0x100u;
                if (packed != (pass == 0)) continue;
                at[k++] = pal + 0xE0u;
            }
        RD32(0x807F2F10u) = 0x57544348u;   // 'WTCH'
        for (u32 i = 0; i < 4; i++) RD32(0x807F2F14u + 4u * i) = at[i];
    }
    { extern u32 dbg_pal_skipped, dbg_pal_calls, dbg_pal_wm, dbg_pal_room; RD32(QA_STATE + 0x22C) = (dbg_pal_wm & 0xFF) | (dbg_pal_room & 0xFF) << 8 | dbg_pal_calls << 16; }   // (skel.c pal_group_guard: groups with no room)
    { extern u32 dbg_rr_kept_out; RD32(QA_STATE + 0x230) = qa_rr_outs | dbg_rr_kept_out << 16; }   // (QA: men put outside; men kept out, no entrant)
    // (lumberjack: a Lumberjack match's numbers in rr_put_out / rr_kept_out -- lumberjack_qa_word; the Regular driver's
    //  Lumberjack row: where its presses left the cursor | the row shown << 12 | the match asked for << 13 | the cursor << 16)
    if (lj_live) RD32(QA_STATE + 0x230) = lumberjack_qa_word();
    else if ((qa_reg & 16384) && !nm_arcade())       // (ljround8, Regular: dbg_lj_nav | the flow, lj_regular_frame's -- Multi
        RD32(QA_STATE + 0x230) = (dbg_lj_nav & 0xFFFFu) | ((dbg_lj_reg & 0x7FFFu) | (u32)(lumberjack_armed() & 1) << 15) << 16;   //  Play's row, the page up, count, picked -- | armed)
    else if (qa_reg & 16384)
        RD32(QA_STATE + 0x230) = dbg_lj_nav | (u32)lj_row_shown << 12 | (u32)lumberjack_armed() << 13 |
            (in_menus() && RDS16(NM_SCREEN) == SCR_EXHIBITION ? (u32)RD8(menu_record(SCR_EXHIBITION) + 5) : 0xFFu) << 16;
    {   // (QA, the 32-slot work: across every slot in play -- the per-slot block above has six -- how many hold a Superstar,
        //  and how many of those are in the ring: in the match, +0x8C bit 0 clear, and not outside, +0x7A bit 4 clear)
        u32 present = 0, in_ring = 0;
        for (int s = 0; s < nm_slots(); s++) {
            u32 w = W(s); u16 id = RD16(w + 0x98);
            if (id == 0xFFFFu || id == 0) continue;
            present++;
            if (!(RD16(w + 0x8C) & 1u) && !(RD16(w + 0x7A) & 4u)) in_ring++;
        }
        RD32(QA_STATE + 0x238) = present | in_ring << 8;
    }
    { extern u32 ann_listed(void); RD32(QA_STATE + 0x23C) = ann_listed(); }
    { extern u32 dbg_rr_next; RD32(QA_STATE + 0x240) = dbg_rr_next; }   // (QA: the Rumble results table's Next: what it gave | tries << 16)   // (QA: the announcers' draw-list entries)
    if (!(lj_live || lumberjack_armed())) { extern u32 dbg_tron[16]; for (u32 i = 0; i < 16; i++) RD32(QA_STATE + 0x280u + 4u * i) = dbg_tron[i]; }   // (tron.c's counters, +0x280..; ljround8: not in a Lumberjack match -- lj_p .. lj_w are there, and a row read between the two writes showed tron's)
    // (not with qa_modes: its own values go there below -- the exe reads the state while the frame writes it, and a row
    //  caught between the two writes showed the modes' word as 0, 2026-11-07)
    if (!qa_modes && !qa_ctlmode) { RD32(QA_STATE + 0x220) = qa_bs_tries; RD32(QA_STATE + 0x224) = qa_bs_grabs; RD32(QA_STATE + 0x228) = (u32)qa_bs_held; }
    if (qa_story) story_qa(QA_STATE + 0x220u);   // (QA a story of ours: its roles, scene ops, how the match ended, variables -- story.c)
    if (qa_fsub && nm_slots() >= 2) {   // (QA qa_fsub: state.csv bs_tries / bs_grabs = the animation file each of the first two
                                        //  plays (the game's +0xA6) -- a hold's ending is its file + 2 / + 4 / + 6, 2026-11-01)
        RD32(QA_STATE + 0x220) = RD16(W(0) + 0x9E); RD32(QA_STATE + 0x224) = RD16(W(1) + 0x9E);
    }
    if (qa_special && !qa_table && !qa_bs) {   // (QA: state.csv bs_tries / bs_grabs -- slot 2's and slot 1's body health, the
        float h[2] = { 0, 0 };                    //  five parts' sum x 10: what a move took out of him)
        for (int s = 0; s < 2; s++) for (u32 k = 0; k < 5; k++) { float v = *(volatile float*)(W(s) + 0x2BCu + 4u * k); if (v == v && v > -1e6f && v < 1e6f) h[s] += v; }
        RD32(QA_STATE + 0x220) = (u32)(s32)(h[1] * 10.0f); RD32(QA_STATE + 0x224) = (u32)(s32)(h[0] * 10.0f);
        RD32(QA_STATE + 0x228) = qa_headgrab ? qa_last_entry : dbg_legdrops;   // (bs_step: or Hogan's leg drops taken)
    }
    if (qa_pspecial && nm_slots() >= 2) qa_pspecial_state();   // (QA qa_pspecial: the special's numbers, above)
    { extern int ms_live; extern void match_slots_qa(u32 q); if (qa_extras && ms_live) match_slots_qa(QA_STATE + 0x220u); }   // (any32: the extras' numbers, slots.c)
    if (qa_lumberjacks && lj_live) lumberjack_qa(QA_STATE + 0x220u);   // (lumberjack: the lumberjacks' numbers, lumberjack.c)
    if (lj_live || lumberjack_armed()) lumberjack_qa2(QA_STATE + 0x244u);   // (lumberjack2: lj_a .. lj_h, lumberjack.c)
    else if (qa_camprobe) qa_camera_probe(QA_STATE + 0x244u);             // (guestref: the match camera in lj_a .. lj_e)
    if (qa_ringstage) {   // (QA RAW's ring and stage: bs_tries the area in play, bs_grabs slot 0's lift | slot 1's << 16, bs_step door refused | steers << 16)
        extern void ringstage_qa(u32 q, int mode);
        ringstage_qa(QA_STATE + 0x220u, qa_ringstage);
    }
    if (qa_walls) {   // (QA qa_walls: state.csv bs_tries / bs_grabs / bs_step -- the walls the match's collision uses: arena_ring.c)
        extern void arena_ring_qa(u32 q);
        arena_ring_qa(QA_STATE + 0x220u);
    }
    if (qa_table) {   // (QA table_climb: state.csv bs_tries / bs_grabs / bs_step -- the table's walls moved, its top's corners'
                      //  z (first | third << 16, as u16), the high back wall's ... the top's address)
        extern u32 dbg_ann_walls, dbg_ann_top_at;
        u32 t = dbg_ann_top_at;
        RD32(QA_STATE + 0x220) = dbg_ann_walls;
        RD32(QA_STATE + 0x224) = t ? (u32)(u16)RDS16(t + 4u) | (u32)(u16)RDS16(t + 16u) << 16 : 0;
        { extern u32 dbg_ann_kept; RD32(QA_STATE + 0x228) = dbg_ann_kept; }
    }
    if (qa_versus) {  // (QA qa_versus: state.csv bs_tries / bs_grabs / bs_step -- the list's cursor + 100 x its rows; sp_versus |
                      //  game_mode << 4 | goal << 8 | challenge_on << 12; the driver's step | where DOWN from Survival went << 8)
        RD32(QA_STATE + 0x220) = (u32)RD8(menu_record(SCR_CHAMPIONSHIP) + 5) + 100u * (u32)RD8(PAGE_ENTRY(SCR_CHAMPIONSHIP) + 4);
        RD32(QA_STATE + 0x224) = (u32)sp_versus | (u32)game_mode << 4 | (u32)goal << 8 | (u32)challenge_on << 12;
        RD32(QA_STATE + 0x228) = (u32)qv_step | (u32)(qv_cursor_down & 0xFF) << 8 | (u32)qv_round << 16 | (u32)(active[0] | active[1] << 1) << 20;
    }
    if (qa_variant) { // (QA qa_variant: state.csv bs_tries = on the Superstar Select, the Superstar under the cursor | its costume
                      //  << 16 | 1 << 24 (0 elsewhere); bs_grabs / bs_step = the select's first two picks, ID | costume << 16;
                      //  & 4: bs_step = where 0x00A and 0x003 are in the Royal Rumble's list (+ 1, 0: not in it) | its size << 16)
        s16 scr = RDS16(NM_SCREEN);
        int sel = in_menus() && is_select(scr) && !menus_loading();
        RD32(QA_STATE + 0x220) = sel ? (u32)sel_hover() | (u32)(RDS16(NM_SEL_COSTUME) & 0xFF) << 16 | 1u << 24 : 0;
        RD32(QA_STATE + 0x224) = (u32)RD16(NM_SEL_REC) | (u32)RD8(NM_SEL_REC + 2) << 16;
        RD32(QA_STATE + 0x1C0) = (dbg_pick_presses & 0xFFFFFFu) | (u32)(RDS16(menu_record(scr) + 4) & 0xFF) << 24;
        RD32(QA_STATE + 0x228) = (u32)RD16(NM_SEL_REC + NM_SEL_REC_SIZE) | (u32)RD8(NM_SEL_REC + NM_SEL_REC_SIZE + 2) << 16 |
                                 (RD32(0x8015B80Cu) & 0xFFu) << 24;   // (| the select's rules byte << 24: 0x80 a Superstar once only)
        if (qa_variant & 4) {
            s32 n = RDS32(NM_RR_COUNT_SRC);
            u32 at_a = 0, at_3 = 0;
            for (s32 i = 0; i < n && i < 100; i++) {
                u16 id = RD16(NM_RR_LIST + 2u * (u32)i) & 0x0FFFu;
                if (id == 0x00A && !at_a) at_a = (u32)i + 1;
                if (id == 0x003 && !at_3) at_3 = (u32)i + 1;
            }
            RD32(QA_STATE + 0x228) = at_a | at_3 << 8 | (u32)(n & 0xFF) << 16;
        }
    }
    if (qa_modes) {   // (QA, the two modes, 2026-11-07: state.csv's bs_* columns -- after every other use of them)
        // bs_tries: the running mode | F2's GAME MODE << 1 | the option as saved << 2 | what the migration did << 4 (1 set
        // Regular, 2 set Arcade) | settings_version as the migration found it << 8 | as it is now << 16 | F2 open << 24 |
        // the arcade's phase << 28 (0 ATTRACT -- all along in Regular)
        u32 cfg_mode = plat_config("game_mode", 0) & 1u, cfg_sv = plat_config("settings_version", 0) & 0xFFu;
        RD32(QA_STATE + 0x220) = (u32)nm_mode() | (u32)mode_next << 1 | cfg_mode << 2 | (u32)mode_migrated << 4 |
                                 ((u32)mode_sv_seen & 0xFFu) << 8 | cfg_sv << 16 | (u32)op_open << 24 | (u32)phase << 28;
        // bs_grabs: the Regular driver's step (qa_regular_frame) | the F2 page << 8 | the match's pause flag << 16
        RD32(QA_STATE + 0x224) = (u32)qr_step | (u32)menu_page << 8 | (u32)(RDS16(NM_MATCH_PAUSED) & 0xFF) << 16 |
                                 (u32)(bind_last & 0x7F) << 24;   // (2026-11-19: a binding's capture, 1 set / 2 cancelled; 0 none)
        // bs_step: the arcade's menu changes, as found in the menus' data (W4: none of them in Regular) -- the menus'
        // data loaded << 15; the championship list's rows (10 the game's, 11 with the arcade's Versus); the BELT tab
        // moved off a bar << 8; the Survival row << 9; Custom Story's << 10; the GBC row as Royal Rumble << 11;
        // Ladder Match's label patched << 12
        u32 w4 = 0;
        if (title_loaded()) {
            w4 = 1u << 15 | RD8(PAGE_ENTRY(SCR_CHAMPIONSHIP) + 4);
            for (u32 a = NM_TAB_TABLE; a < NM_TAB_TABLE_END; a += 4)
                if (RD32(a) == 0x42454C54u && RD8(a + 4) == 0 && RDS32(a + 0x24) == 12 && RDS32(a + 0x20) == 0x400) w4 |= 1u << 8;
            if (RD16(CHAMP_SURV_DEF + 0xA) == LABEL_SURVIVAL) w4 |= 1u << 9;
            if (RD16(CHAMP_CUSTOM_DEF + 0xA) == LABEL_CUSTOM) w4 |= 1u << 10;
            if (RD16(CHAMP_FREE_DEF + 0xA) == LABEL_ROYAL_RUMBLE) w4 |= 1u << 11;
            if (RD16(LADDER_LABEL_ENTRY + 2) == 9) w4 |= 1u << 12;
        }
        RD32(QA_STATE + 0x228) = w4;
        // (W10, QA) on the NUMBER page (screen 9): bs_step = its widget's +0x4C | +0x4D << 8 | +0x4E << 16 | the ways tried << 24
        //  (4 bits) | arcade_number_setup's calls so far << 28 (4 bits)
        if (in_menus() && RDS16(NM_SCREEN) == 9) {
            u32 items = RD32(menu_record(9) + 0x14);
            if (GAME_PTR(items)) RD32(QA_STATE + 0x228) = (u32)RD8(items + 0x4C) | (u32)RD8(items + 0x4D) << 8 | (u32)RD8(items + 0x4E) << 16 | (u32)(qa_number_way & 0xF) << 24 | (dbg_number_setups & 0xFu) << 28;
            // (and rr_next = the row's +0x4A | its label +0x60 << 16 -- what limits and format the page has)
            // (and rr_next = the cursor | rows 2, 4, 6's values (+0x4E) << 8, << 16, << 24 -- phase 3, the page's own rows)
            if (GAME_PTR(items)) RD32(QA_STATE + 0x240) = (u32)RD8(menu_record(9) + 5) | (u32)RD8(items + 2u * 0x88u + 0x4E) << 8 |
                                                          (u32)RD8(items + 4u * 0x88u + 0x4E) << 16 | (u32)RD8(items + 6u * 0x88u + 0x4E) << 24;
            // (and, the rows' test only -- qa_reg 2048 -- bs_tries = row 0's +0x74 | +0x5A << 8, row 2's << 16 / << 24)
            if (GAME_PTR(items) && (qa_reg & 2048)) RD32(QA_STATE + 0x220) = (RD16(items + 0x74) & 0xFFu) | (RD16(items + 0x5A) & 0xFFu) << 8 |
                                                          (RD16(items + 0x88u + 0x88u + 0x74) & 0xFFu) << 16 | (RD16(items + 2u * 0x88u + 0x5A) & 0xFFu) << 24;
            // (rumblemenu, 2026-11-25 -- without qa_reg 2048: bs_tries = the four captions' lit bytes (+0x57: 0 lit, 0x80 not):
            //  NUMBER's (row 8) | IN THE RING's (3) << 8 | ENTRY TIME's (5) << 16 | STARTS WITH's (7) << 24)
            else if (GAME_PTR(items) && np_rows_up) RD32(QA_STATE + 0x220) = (u32)RD8(items + 8u * 0x88u + 0x57) | (u32)RD8(items + 3u * 0x88u + 0x57) << 8 |
                                                          (u32)RD8(items + 5u * 0x88u + 0x57) << 16 | (u32)RD8(items + 7u * 0x88u + 0x57) << 24;
        }
        // (rumblemenu, 2026-11-25) on the RULES page (screen 21): bs_step = the page's choices (0x80091CA8) rows 2..9, 4 bits
        //  each from bit 0 (2 COUNT OUT, 3 PIN, 4 SUBMISSION, 5 TKO, 6 ROPE BREAK, 7 DQ, 8 BLOODSHED, 9); rr_next = row 10 | row 11
        //  (TIME COUNT) << 4 | a new save's Rumble slot given the mod's defaults, times (dbg_rumble_rules_default) << 8
        if (in_menus() && RDS16(NM_SCREEN) == SCR_RULES) {
            u32 w = 0;
            for (u32 i = 0; i < 8; i++) w |= (u32)(RD8(0x80091CA8u + 2u + i) & 0xFu) << (4u * i);
            RD32(QA_STATE + 0x228) = w;
            RD32(QA_STATE + 0x240) = (u32)(RD8(0x80091CA8u + 10u) & 0xFu) | (u32)(RD8(0x80091CA8u + 11u) & 0xFu) << 4 | (dbg_rumble_rules_default & 0xFFu) << 8;
            // (and bs_tries = the page's own rows 0..7 (*0x80159D0C) as shown, +0x4E 4 bits each -- the choice each row draws)
            u32 pr = RD32(0x80159D0Cu), sh = 0;
            if (GAME_PTR(pr)) for (u32 i = 0; i < 8; i++) sh |= (u32)(RD8(pr + i * 0x88u + 0x4E) & 0xFu) << (4u * i);
            RD32(QA_STATE + 0x220) = sh;
        }
        // (W10) in a match (mode -2): bs_step = the Rumble's entrants (NM_RR_COUNT) | IN THE RING AT ONCE as the engine
        // has it (rw_rumble_ring) << 8 | the clock's seconds between entrants << 16 | gm_live << 24 | rr_mode << 28; and
        // bs_grabs' bits 16.. = F2's SUPERSTARS IN IT as saved (a match's change must never reach it)
        if (RDS32(NM_MODE) == -2) {
            u32 ring = 0;
#ifdef NM_SIX
            ring = RD8(SIX_RUMBLE_RING);
#endif
            RD32(QA_STATE + 0x228) = ((u32)RDS32(NM_RR_COUNT) & 0xFFu) | ring << 8 | ((u32)rr_interval() & 0xFFu) << 16 |
                                     (u32)gm_live << 24 | (u32)rr_mode << 28;
        }
        // (bits 24-30 kept: a binding's capture, 2026-11-19 -- see qa_modes above; 0 unless one was asked for)
        RD32(QA_STATE + 0x224) = (RD32(QA_STATE + 0x224) & 0xFF00FFFFu) | (plat_config("rumble_entrants", 0) & 0xFFu) << 16;
        // (W8, QA: on the title, bs_grabs bits 16.. = the screen the menus are told to open on, NM_RETURN_SCREEN, + 1)
        if (on_title()) RD32(QA_STATE + 0x224) = (RD32(QA_STATE + 0x224) & 0xFF00FFFFu) | ((u32)(RDS16(NM_RETURN_SCREEN) + 1) & 0xFFu) << 16;
        // (the title's own PRESS START, sprite 15: is it in the menus' sprite queue, and how lit -- rr_next = 1 found |
        //  its alpha << 8 | the title's idle count << 16; on the title only. W11:
        //  in Arcade, in a game, its frames idle at a menu in the low half instead)
        if (on_title()) {
            u32 t = RD32(NM_SPRITE_FADES), found = 0, alpha = 0, req = 0, n = 0;
            if (GAME_PTR(t))
                for (int i = 0; i < 0x60; i++) {
                    u32 e = t + (u32)i * 12u, r = RD32(e);
                    if (!GAME_PTR(r)) continue;
                    n++;
                    if (RD32(r + 8) == SPR_PRESS_START) { found = 1; alpha = (u32)RDS16(e + 4) & 0xFFu; req = RD32(e + 8) ? 1u : 0u; }
                }
            // (and the title's idle count, NM_TITLE_IDLE, << 16: the menus' loop requests PRESS START -- sprite 15 at 240,
            //  200 -- each frame the count isn't negative, counting it up as it does: func_80111158 0x80111740)
            (void)req; (void)n;
            RD32(QA_STATE + 0x240) = found | alpha << 8 | ((u32)RDS32(NM_TITLE_IDLE) & 0xFFFFu) << 16;
            // (phase 3: what was actually asked for and drawn -- rr_next = requests of sprite 15 this second (& 0xFF) | the
            //  highest alpha it was drawn at this second << 8 | its draws this second << 16; once a second, reset)
            if (qa_press_start_ok) RD32(QA_STATE + 0x240) = qa_ps_shown;
        } else if (nm_arcade()) RD32(QA_STATE + 0x240) = (u32)menu_idle_frames | dbg_menu_idle << 24;
        // (Regular, a game-menu Rumble on -- phase 3, its continue: rr_next = times put back | held out << 8 | the driver's
        //  hand-backs, out (qa_reg 8192) << 16 | 0xC0 << 24)
        else if (gm_live == 1 && RDS32(NM_MODE) == -2) { extern u32 qa_put_outs; RD32(QA_STATE + 0x240) = (dbg_back_in & 0xFFu) | (u32)rr_player_out << 8 | (qa_put_outs & 0xFFu) << 16 | 0xC0u << 24; }
        // (Regular, the chapter card -- phase 3, ABBREVIATED MENUS: rr_next = short_menus | story_active << 1 | the skip done
        //  << 2 | the menus' code loaded << 3 | NM_CHAMP_CONTEXT << 8 | the card's state (0x80097ED6) << 16 | 0xCA << 24)
        else if (RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == SCR_STORY_CARD)
            RD32(QA_STATE + 0x240) = (u32)short_menus | (u32)(story_active() != 0) << 1 | (u32)card_confirmed << 2 | (u32)title_loaded() << 3 |
                                     (u32)RD8(NM_CHAMP_CONTEXT) << 8 | (RD16(0x80097ED6u) & 0xFFu) << 16 | 0xCAu << 24;
        // (Regular, off the title -- W7: rr_next = F2's BIG SHOW | the game's live roster's << 8 | 0xB5 << 24)
        else if (!(in_menus() && RDS16(NM_SCREEN) == 9)) RD32(QA_STATE + 0x240) = (u32)roster_opt[RO_BIG_SHOW] | (u32)roster_live[RO_BIG_SHOW] << 8 | 0xB5u << 24;
    }
    if (qa_ctlmode) {   // (QA ctrlsave: CONTROL MODE kept from one start to the next -- qa\scenarios\ctl_mode_*)
        // bs_tries = CONTROL MODE (0 N64, 1 6-BUTTON) | F2 open << 1 | the settings loaded << 2 | the migration put it back
        //            to N64 << 3 | F2's page << 8 | its row << 16
        // bs_grabs = "controls" as the settings load read it + 1 (& 0xFF; 0 not read) | as read back after the last save + 1
        //            << 8 | the saves << 16
        // bs_step  = in a match, slot 0 (player 1): how far he went from where he stood at match frame 60 (the larger of x
        //            and z, whole units, & 0xFFFF) | the match frames player 1's controller was pressed or pushed << 16
        static float x0, z0; static int have0; static u32 went, pressed;
        u16 mf = RD16(NM_MATCH_FRAMES);
        if (match_running() && mf != 0xFFFF && mf > 60 && nm_slots() >= 1) {
            float x = *(volatile float*)(W(0) + 0xF4), z = *(volatile float*)(W(0) + 0xFC);
            if (x == x && z == z) {
                if (!have0) { x0 = x; z0 = z; have0 = 1; }
                float dx = x > x0 ? x - x0 : x0 - x, dz = z > z0 ? z - z0 : z0 - z, d = dx > dz ? dx : dz;
                if (d < 65535.0f && (u32)d > went) went = (u32)d;
            }
            u32 p = NM_PADS + 6u * (u32)port_player[0];
            s8 sx = RDS8(p + 2u), sy = RDS8(p + 3u);
            if (RD16(p) || sx > 20 || sx < -20 || sy > 20 || sy < -20) pressed++;
        }
        RD32(QA_STATE + 0x220) = (u32)arc_controls | (u32)op_open << 1 | (u32)settings_loaded << 2 | (u32)ctl_migrated << 3 |
                                 ((u32)menu_page & 0xFFu) << 8 | ((u32)menu_sel & 0xFFu) << 16;
        RD32(QA_STATE + 0x224) = (dbg_ctl_loaded & 0xFFu) | (dbg_ctl_saved & 0xFFu) << 8 | (dbg_ctl_saves & 0xFFFFu) << 16;
        RD32(QA_STATE + 0x228) = (went & 0xFFFFu) | (pressed > 0xFFFFu ? 0xFFFFu : pressed) << 16;
    }
    announce_qa(QA_STATE + 0x1C4);                     // JR's, King's: bones, root x y z (+0x1C4.., +0x1D4..)
    {   // the last bad node itself, and the parent it named (a garbage parent: the tree's memory overwritten)
        u32 n = RD32(FLIGHT + 0x70);
        RD32(QA_STATE + 0x1E4) = n;
        RD32(QA_STATE + 0x1E8) = (GAME_PTR(n)) ? RD32(n) : 0;
    }
    {   // (the stories' page, tests: on, top, its rows' label / flags / alpha, the pictures made)
        u32 items = RD32(menu_record(SCR_CHAMPIONSHIP) + 0x14);
        RD32(QA_STATE + 0x300) = (u32)list_on | (u32)list_top << 8 | (u32)story_count() << 16;
        RD32(QA_STATE + 0x304) = items; RD32(QA_STATE + 0x308) = dbg_story_tex;
        for (u32 k = 0; k < 11 && GAME_PTR(items); k++) {
            u32 row = items + k * 0x88u;
            RD32(QA_STATE + 0x30C + 8u * k) = (u32)RD16(row + 0x60) << 16 | RD16(row + 0x4A);
            RD32(QA_STATE + 0x310 + 8u * k) = (u32)RD8(row + 0x56) << 24 | (u32)RD8(row + 0x57) << 16 | (u32)RD16(LABEL_ENTRY(LIST_LABEL0 + (int)k));
        }
    }
    // (the route, tests: where it's taking the game and the match's leaving -- a challenger's Rumble that went on, 2026-10-29)
    RD32(QA_STATE + 0x380) = (u32)goal | (u32)leave_fast << 8 | (u32)leave_asked << 16 | (u32)game_mode << 24;
    RD32(QA_STATE + 0x384) = (u32)goal_wait; RD32(QA_STATE + 0x388) = (u32)goal_frames;
    RD32(QA_STATE + 0x38C) = (u32)RD8(NM_MATCH_QUIT) | (u32)RD8(NM_MATCH_DONE) << 8 | (u32)RD8(NM_MATCH_FADING_IN) << 16 | (u32)(cont_player & 0xFF) << 24;
    RD32(QA_STATE + 0x390) = (u32)(u16)RDS16(NM_MATCH_LEAVE) | (u32)(u16)RDS16(NM_MATCH_PAUSED) << 16;
    RD32(QA_STATE + 0x394) = (u32)phase | (u32)rr_mode << 8 | (u32)lost_pending[0] << 16 | (u32)lost_pending[1] << 24;
    RD32(QA_STATE + 0x398) = (u32)RDS32(NM_MATCH_STATE); RD32(QA_STATE + 0x39C) = RD32(NM_MATCH_SCENE);
    RD32(QA_STATE + 0x3A0) = dbg_leave_retries; RD32(QA_STATE + 0x3A4) = dbg_menu_cam;
    { extern u32 dbg_zfill; RD32(QA_STATE + 0x3A8) = dbg_zfill; }   // (tron.c ann_zclear: the game's fill colour put back)
    { extern u32 dbg_jk_sounds; RD32(QA_STATE + 0x340) = dbg_jk_sounds; }   // (movex.c: Nash's jackknife's sounds read; + 0x3AC..0x3CF is media.c's sound log)
    RD32(QA_STATE + 0x3E0) = (u32)roster_pages_n;      // (the select's pages, where they are: the mod's memory is
    RD32(QA_STATE + 0x3E4) = (u32)roster_pages_tab;    //  placed when it's loaded -- qa\pages_dump.py)
    RD32(QA_STATE + 0x3E8) = (u32)roster_pages_flags;
    RD32(QA_STATE + 0x04) = frame_no;
    RD32(QA_STATE + 0x08) = (u32)qa_stage;
    RD32(QA_STATE + 0x0C) = (u32)qa_done;
    if (qa_story) { extern u32 dbg_story_branch; RD32(QA_STATE + 0x0C) = story_active() ? 10000u + RD16(0x80097ED4u) * 100u + (RD16(0x80097ED6u) & 0xFFu) : 0u; }
    RD32(QA_STATE + 0x10) = RD32(NM_MODE);
    RD32(QA_STATE + 0x14) = (u32)RDS16(NM_SCREEN);
    RD32(QA_STATE + 0x18) = RD16(NM_MATCH_FRAMES);
    RD32(QA_STATE + 0x1C) = RD32(NM_MATCH_STATE);
    RD32(QA_STATE + 0x20) = RD32(NM_ENTRANCE_STEP);
    { extern int arena_current(void); RD32(QA_STATE + 0x24) = (u32)arena_current(); }   // (the venue; an arena of ours 10 + k: arena.c)
    RD32(QA_STATE + 0x28) = RD32(NM_MATCH_FLAGS);
    RD32(QA_STATE + 0x2C) = (u32)rr_clock();
    if (!qa_story && RDS16(NM_SCREEN) == 42 && RDS32(NM_ENTRANCE_STEP) >= 1 && RDS32(NM_ENTRANCE_STEP) <= 29) { extern u32 tron_qa(void); RD32(QA_STATE + 0x230) = tron_qa(); }   // (the Titantron in the entrances: state.csv's rr_put_out = the video picked, rr_kept_out = its mode | music << 4 | entrant << 8)
    if (qa_story && rr_mode == RR_OFF) { extern u32 dbg_story_branch; RD32(QA_STATE + 0x2C) = dbg_story_branch; }
    int n = nm_slots();
    RD32(QA_STATE + 0x30) = (u32)n;
    // the scene's matrices that went bad (gameplay.c scene_world): how many, whose the last was, its frame
    RD32(QA_STATE + 0x34) = RD32(FLIGHT + 0x6C);
    RD32(QA_STATE + 0x38) = announce_owner(RD32(FLIGHT + 0x70));
    RD32(QA_STATE + 0x3C) = RD32(FLIGHT + 0x74);
    for (int s = 0; s < NM_SLOTS_MAX; s++) {
        u32 q = QA_STATE + 0x40u + 0x40u * (u32)s, w = W(s);
        if (s >= n) { for (u32 k = 0; k < 0x40; k += 4) RD32(q + k) = 0; continue; }
        RD32(q + 0x00) = RD16(w + 0x98) | (RD32(NM_SLOT_COSTUME + 4u * (u32)s) & 0xFu) << 16;   // Superstar, costume
        RD32(q + 0x04) = RD32(NM_SLOT_PAD + 4u * (u32)s);                       // controller (0: CPU)
        RD32(q + 0x08) = RD32(w + 0xF4); RD32(q + 0x0C) = RD32(w + 0xF8); RD32(q + 0x10) = RD32(w + 0xFC);   // x y z
        RD32(q + 0x14) = (u32)RD16(w + 0xA6) | (u32)RD16(w + 0xA2) << 16;      // spirit (100: SPECIAL), stamina
        // (2026-11-12, agent "names4", noted by final081: W(s) is the wrestler record + 8, so W + 0x14 is the record's +0x1C, his
        //  PREVIOUS state, and W + 0x0C its +0x14, his state (tools/lift/fields.json Wrestler). state.csv calls the first
        //  "s<k>_state" and the second "s<k>_phase": the labels are kept, because 37 scenarios' checks (s0_phase == 72 in
        //  blocks_hogan, s1_state == 3 ...) were written against these values as they are, and their verdicts mean that.)
        RD32(q + 0x18) = (u32)RD16(w + 0x302) << 16 | (RD32(w + 0x14) & 0xFFFFu);  // the move he is in (the game's +0x30A = W + 0x302), "state" (his previous state)
        RD32(q + 0x1C) = RD32(w + 0x0C);                                         // "phase" (his state)
        RD32(q + 0x20) = (u32)RD16(w + 0x8C) | (u32)RD8(w + 0xE1) << 16 | (u32)RD8(w + 0x7A) << 24;
        RD32(q + 0x24) = RD32(NM_SLOT_ELIM_TIME + 4u * (u32)s);                 // eliminated at (-1: in)
        RD32(q + 0x28) = RD32(w + 0x104);                                        // facing
    }
}
// qa_mode 2: the single-player Royal Rumble, the arcade's own way (the championship list's Royal Rumble row,
// the Superstar picked on its select: qa_w1), free play. qa_ctl1 0: once the bell has gone the CPU fights for
// the player's Superstar (his slot's controller off and its CPU bit on, as an entrant's). The arcade's own
// logic runs it all; this only makes the choices a player would.
static void qa_rumble(void) {
    free_play = 1;
#ifdef NM_SIX
    if (qa_ring()) RD8(SIX_RUMBLE_RING) = (u8)qa_ring();   // (before the set-up reads it)
#endif
    if (phase == ATTRACT && booted && on_title()) {
        active[0] = 1; active[1] = 0; port_player[0] = 0; port_player[1] = 1;
        game_mode = GM_STORY; match_armed = 0;
        set_goal(G_STORY, 0);
        set_phase(PLAY);
        return;
    }
    if (phase != PLAY) return;
    s16 scr = in_menus() ? RDS16(NM_SCREEN) : -1;
    if (scr == SCR_CHAMPIONSHIP && goal == G_NONE && !menus_loading() && scr_stable > 30 && rr_mode == RR_OFF && !rr_open_pending) {
        if (qa_last_frames > 0) {                                 // a Rumble over: back on the list
            qa_done++; qa_last_frames = 0; qa_stage = QA_TITLE;
            if (qa_matches > 0 && qa_done >= qa_matches) { if (!qa_quit_in) qa_quit_in = 900; return; }   // (15 s on the list: its shots)
        }
        rr_open_pending = 1;                                      // (the Royal Rumble row, as A on it)
    }
    if (is_select(scr) && rr_mode == RR_SELECT && qa_w[0] > 0 && qa_w[0] < 0x1000) auto_pick((u16)qa_w[0]);
    // (the results table after the Rumble: Next, as the player would -- a test waited there until its time ran out,
    //  never back on the list where it counts the match as done, 2026-11-06. Two seconds on it first: its shots.)
    if (scr == SCR_MATCH_RESULTS && qa_last_frames > 0 && scr_stable > 120 && inject_frames == 0 && (frame_no % 60u) == 0)
        inject_press(BTN_A);
    if (match_running() && rr_live()) {
        u16 fr = RD16(NM_MATCH_FRAMES);
        if (fr != 0xFFFF) qa_last_frames = fr;
        qa_stage = QA_MATCH;
        // (rumblecrash, 2026-11-22: with player 1 at his controller -- qa_ctl1 1, a played Rumble -- his man stays his, and
        //  qa_rr_out still puts the CPU's men out so the Rumble reaches its decision in a test's time: the put-out below
        //  passes over every controller's slot. With qa_ctl1 0 all of this is as it was.)
        if ((qa_ctl[0] == 0 || qa_rr_out > 0) && fr != 0xFFFF && fr > 60) {
            // (his slot as found while he was still the player's: once the CPU fights for him he isn't, to
            // rr_player_slot)
            static int qa_pslot = -1;
            int s = -1;
            if (qa_ctl[0] == 0) {
                s = rr_player_slot();
                if (s >= 0) qa_pslot = s; else s = qa_pslot;
                if (s >= 0) { RDS32(NM_SLOT_PAD + 4u * (u32)s) = 0; RD8(W(s) + 0xE1) |= 0x08; }
                if (s >= 0 && qa_bs) qa_bs_frame(s);
            }
            // (QA, qa_rr_out: eliminations without waiting for the CPU's -- a CPU Superstar standing in the ring put
            // outside it, where the Rumble's count-out rule has him out: the game's own elimination)
            static int qa_out_wait;
            if (qa_rr_out > 0 && RD16(NM_MATCH_FRAMES) != 0xFFFFu && RD16(NM_MATCH_FRAMES) >= (u32)qa_rr_out_after * 60u
                && ++qa_out_wait >= qa_rr_out * 60) {
                for (int v = 0; v < nm_slots(); v++) {
                    u32 vw = W(v);
                    if (v == s || RDS32(NM_SLOT_PAD + 4u * (u32)v) != 0 || RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)v) != -1 || !qa_bs_free(vw, 0)) continue;
                    // (on the floor past the apron -- z ±330, height 0, +0x7A "outside" (4) -- where a man thrown out
                    // lands. Until 2026-11-06 he was put at z ±260 and left at the ring's height, 90, still marked in
                    // the ring: the game went on treating him so, and he walked off along that line out through the
                    // barricade and the crowd -- in the six-slot game as in the 32-slot: a test artifact, seen by the
                    // user in a QA window. +0xF4/+0xF8/+0xFC his place, +0x118.. its last copy, +0x124..+0x154 his
                    // joints: x, y, z each.)
                    float z = QA_F32(vw + 0xFC) < 0.0f ? -330.0f : 330.0f;
                    QA_F32(vw + 0xFC) = z; QA_F32(vw + 0x120) = z;
                    QA_F32(vw + 0xF8) = 0.0f; QA_F32(vw + 0x11C) = 0.0f;
                    for (u32 k = 0x124; k <= 0x154; k += 12) { QA_F32(vw + k + 8) = z; QA_F32(vw + k + 4) = 0.0f; }
                    RD16(vw + 0x7A) = 4;   // (as the floor-brawl hook above does, which met the same walk at height 90)
                    qa_out_wait = 0; qa_rr_outs++;
                    break;
                }
            }
        }
    }
}
// qa_story (QA mode off: the arcade as it is): 4 (with 2) -- the game's own World Heavyweight story instead of
// ours (the game's own, to compare); 2 -- in free play, one player started on the title, the cursor put
// on the championship list's Custom Story row and (2 s later) the row chosen, as a player's A would, once; the
// Superstar Select left to the pads (or qa_w1 picked); back on the list after it, the game quits. 1 -- in a
// story of ours, the CPU plays the player's Superstar from the bell (his slot's controller off, its CPU bit on),
// so its matches end by themselves (and A for the replay after). state.csv: "matches" = 10000 + the node * 100 + the engine's phase while a
// story of ours runs; "clock" = the branch it took last (story.c), -1 none yet.
static void qa_op_frame(void);
static void qa_story_frame(void) {
    static int opened, left, wait, over, page_a, again;
    if (!qa_story) return;
    // (qa_p2_after_op: after the operator action's reset, a second game started the same way -- nothing more driven
    // in it but player 2's Start)
    if ((qa_story & 2) && qa_p2_after_op && dbg_op_done && !again && phase == ATTRACT && booted && on_title()) { opened = 0; again = 1; }
    if (again && opened) return;
    if ((qa_story & 2) && phase == ATTRACT && booted && on_title() && !opened) {
        active[0] = 1; active[1] = 0; port_player[0] = 0; port_player[1] = 1;
        game_mode = GM_STORY; match_armed = 0;
        set_goal(G_STORY, 0);
        set_phase(PLAY);
        if (again) opened = 1;
        return;
    }
    if (phase != PLAY) return;
    s16 scr = in_menus() ? RDS16(NM_SCREEN) : -1;
    if (opened && scr != SCR_CHAMPIONSHIP) left = 1;
    if ((qa_story & 2) && scr == SCR_CHAMPIONSHIP && goal == G_NONE && !menus_loading() && !custom_launch && !story_active()) {
        if (opened) { if (left && scr_stable > 120 && !qa_quit_in && !qa_p2_after_op) qa_quit_in = 120; }
        else if (scr_stable > 90 && ((qa_story & 4) || custom_row_on())) {
            RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = (qa_story & 4) ? 0 : CHAMP_CUSTOM_ROW;   // (4: World Heavyweight)
            if (++wait > 120) { opened = 1; if (qa_story & 4) inject_press(BTN_A); else custom_open_pending = 1; }
        }
    }
    { static int lw; if ((qa_story & 2) && scr == SCR_CHAMPIONSHIP && list_on && !menus_loading()) { if (++lw == 150 && !(qa_story & 8)) list_pick_pending = list_top; } else lw = 0; }   // (8: left on it)
    if ((qa_story & 2) && is_select(scr) && (story_active() || (qa_story & 4)) && qa_w[0] > 0 && qa_w[0] < 0x1000) auto_pick((u16)qa_w[0]);
    int ours = story_active() || (qa_story & 4);
    if ((qa_story & 1) && ours && match_running() && RD16(NM_MATCH_FRAMES) != 0xFFFF && RD16(NM_MATCH_FRAMES) > 30)
        for (int sl = 0; sl < 4; sl++) {
            s32 pad = RDS32(NM_SLOT_PAD + 4u * (u32)sl);
            if (pad == 1 || pad == 2) { RDS32(NM_SLOT_PAD + 4u * (u32)sl) = 0; RD8(W(sl) + 0xE1) |= 0x08; }
        }
    // (and its replay, which loops until a player's A: A every second, after two)
    if ((qa_story & 1) && ours && match_running() && RDS32(NM_MATCH_STATE) == 4) {
        if (++over >= 120 && over % 60 == 0 && inject_frames == 0) inject_press(BTN_A);
    } else over = 0;
    // (and the story's pages that wait for one: the match's results (Next), anything else that stays up 10 s --
    // the rules and the match card go on by themselves; a chapter card waits for Continue: on it 10 s, A)
    if ((qa_story & 1) && ours && in_menus() && !menus_loading() && scr != SCR_RULES &&
        scr != SCR_VS && !is_select(scr) && scr != SCR_CHAMPIONSHIP && scr != SCR_MATCH &&
        scr_stable > (scr == SCR_RESULTS ? 180 : 600)) {
        if (++page_a % 40 == 0 && inject_frames == 0) inject_press(BTN_A);
    } else page_a = 0;
    qa_op_frame();                                   // (an operator action in a story's match, qa_op)
    if (qa_quit_in > 0 && --qa_quit_in == 0) plat_quit();
}
// qa_versus (QA mode off: the arcade as it is; free play): one player's Versus, driven the way a player would -- presses
// on player 1's controller as the controller read them (qa_pad), so the arcade's own handling sees them. One player
// started on the title; on the championship list the cursor put on Survival, then DOWN (to the Versus row by its own
// link) and A; on Versus's MATCH page A (Single Match: the PLAYER page passed by itself, 1P vs CPU), A on ARENA, Start on
// RULES; at the Superstar Select qa_w1 for the player and qa_w2 for the CPU (auto_pick); the match card's A; in the match
// the CPU plays the player's Superstar too after qa_versus seconds (his slot's controller off, its CPU bit on), so it
// ends by itself; A through its replay; Next on the results. Back on the MATCH page 10 s, B; on the list 10 s, the game
// quits. state.csv: bs_tries = the list's cursor + 100 x its rows, bs_grabs = sp_versus | game_mode << 4 | goal << 8,
// bs_step = the driver's step (below).
static void qa_versus_frame(void) {
    static int over, quit_in;
    if (!qa_versus) return;
    free_play = 1;
    if (quit_in > 0 && --quit_in == 0) plat_quit();
    if (qv_step == QV_TITLE) {
        if (phase == ATTRACT && booted && on_title()) {
            active[0] = 1; active[1] = 0; port_player[0] = 0; port_player[1] = 1;
            game_mode = GM_STORY; match_armed = 0;
            set_goal(G_STORY, 0);
            set_phase(PLAY);
            qv_step = QV_LIST;
        }
        return;
    }
    if (phase != PLAY || qa_pad_frames[0] > 0 || qa_pad_frames[1] > 0 || inject_frames > 0) return;
    if (qv_wait > 0) { qv_wait--; return; }
    s16 scr = in_menus() ? RDS16(NM_SCREEN) : -1;
    int still = goal == G_NONE && !menus_loading();
    u32 champ = menu_record(SCR_CHAMPIONSHIP);
    switch (qv_step) {
    case QV_LIST:     // (on the list a moment: its shots)
        if (scr == SCR_CHAMPIONSHIP && still && scr_stable > 150) { RD8(champ + 5) = CHAMP_SURV_ROW; qv_step = QV_DOWN; qv_wait = 60; }
        break;
    case QV_DOWN:
        qa_pad(BTN_DD); qv_step = QV_ROW; qv_wait = 90;
        break;
    case QV_ROW:      // (the cursor where DOWN from Survival took it: the Versus row)
        if (qv_cursor_down < 0) qv_cursor_down = RD8(champ + 5);
        if (RD8(champ + 5) != CHAMP_VERSUS_ROW) RD8(champ + 5) = CHAMP_VERSUS_ROW;
        qa_pad(BTN_A); qv_step = QV_MATCH; qv_wait = 60;
        break;
    case QV_MATCH:
        // (qa_reg 16384, lumberjack2: Arcade's Lumberjack Match row -- the cursor on Ladder Match, DOWN (the page's own link),
        //  where it landed kept (dbg_lj_nav bits 8-11), then A on it)
        if ((qa_reg & 16384) && scr == SCR_EXHIBITION && still && exh_rows_in() && scr_stable > 180 && lj_row_shown) {
            static int lk;
            u32 rec = menu_record(SCR_EXHIBITION);
            if (lk == 0) { RD8(rec + 5) = EXH_LADDER_ROW; lk = 1; qv_wait = 30; }
            else if (lk == 1) { qa_pad(BTN_DD); lk = 2; qv_wait = 40; }
            else if (lk == 2) { dbg_lj_nav |= (u32)(RD8(rec + 5) & 0xF) << 8; lk = 3; qv_wait = 120; }
            else { RD8(rec + 5) = (u8)EXH_LJ_ROW; qa_pad(BTN_A); lk = 0; qv_step = QV_ARENA; qv_wait = 60; }
            break;
        }
        if (scr == SCR_EXHIBITION && still && exh_rows_in() && scr_stable > 180) { RD8(menu_record(SCR_EXHIBITION) + 5) = 0; qa_pad(BTN_A); qv_step = QV_ARENA; qv_wait = 60; }
        break;
    case QV_ARENA:
        if (scr == SCR_ARENA && still && scr_stable > 120) { qa_pad(BTN_A); qv_step = QV_RULES; qv_wait = 60; }
        break;
    case QV_RULES:
        if (scr == SCR_RULES && still && scr_stable > 120) { qa_pad(BTN_START); qv_step = QV_PICK1; qv_wait = 60; }
        break;
    case QV_PICK1:    // (the player's box, then the CPU's -- both picked on controller 1, the game's 1P vs CPU)
        if (qv_round == 1) { qv_step = QV_PICK2; break; }
        if (is_select(scr) && still && scr_stable > 60 && qv_random > 0) { qa_pad(BTN_CU); qv_random--; qv_wait = 90; break; }   // (qa_variant & 8)
        if (is_select(scr) && still && scr_stable > 60 && auto_pick_c((u16)qa_w[0], qa_variant ? qa_c[0] : -1, -1) == 1) { qv_step = QV_PICK2; qv_wait = 60; }
        break;
    case QV_PICK2:
        if (is_select(scr) && still && qv_round == 1 && (qa_variant & 2)) {   // (QA qa_variant & 2: the challenge's picks, each on
            static int r1_pl;                                                 //  his own controller: qa_w1 then qa_w2, costumes too)
            if (r1_pl < 2 && auto_pick_c((u16)qa_w[r1_pl], qa_c[r1_pl], r1_pl) == 1) r1_pl++;
        }
        else if (is_select(scr) && still && qv_round == 1) { static int t; if (++t % 2) qa_pad(BTN_A); else qa_pad2(BTN_A); qv_wait = 45; }   // (the challenge: each picks)
        else if (is_select(scr) && still) auto_pick_c((u16)qa_w[1], qa_variant ? qa_c[1] : -1, -1);
        else if (scr == SCR_VS && still && scr_stable > 180) inject_press(BTN_A);
        else if (match_running()) { qv_step = QV_FIGHT; over = 0; }
        break;
    case QV_FIGHT:
        // (qa_versus_p2: player 2's Start 10 s into single player's match -- the challenger)
        if (qa_versus_p2 && qv_round == 0 && !active[1] && match_running() && RD16(NM_MATCH_FRAMES) != 0xFFFF && RD16(NM_MATCH_FRAMES) > 600) qv_p2_start = 1;
        if (match_running() && RD16(NM_MATCH_FRAMES) != 0xFFFF && RD16(NM_MATCH_FRAMES) > (u32)qa_versus * 60u && !(qa_versus_p2 && qv_round == 0))
            for (int sl = 0; sl < 4; sl++) {
                s32 pad = RDS32(NM_SLOT_PAD + 4u * (u32)sl);
                if (pad == 1 || pad == 2) { RDS32(NM_SLOT_PAD + 4u * (u32)sl) = 0; RD8(W(sl) + 0xE1) |= 0x08; }
            }
        // (its replay loops until a player's A: A every second, after two)
        if (match_running() && RDS32(NM_MATCH_STATE) == 4) { if (++over >= 120 && over % 60 == 0) qa_pad(BTN_A); }
        else over = 0;
        if (in_menus() && scr != SCR_MATCH && !match_running()) qv_step = QV_AFTER;
        break;
    case QV_AFTER:    // (the results: Next; then the MATCH page again)
        if ((scr == SCR_MATCH_RESULTS || scr == SCR_RESULTS) && still && scr_stable > 180) {   // (a singles' Next / Rematch: 29)
            u32 rec = menu_record(scr), items = RD32(rec + 0x14);
            for (int i = 0; i < 8 && GAME_PTR(items); i++) {
                u32 def = RD32(items + (u32)i * 0x88u + 0x64);
                if (def_ptr(def) && RD16(def + 0x12) == ACT_RESULTS_NEXT) RD8(rec + 5) = (u8)i;
            }
            qa_pad(BTN_A); qv_wait = 120;
        } else if (scr == SCR_EXHIBITION && still && challenge_on && qv_round == 0 && scr_stable > 120) { qv_round = 1; qv_step = QV_MATCH; }
        else if (scr == SCR_EXHIBITION && still && !challenge_on && scr_stable > 600) { qa_pad(BTN_B); qv_step = QV_BACK; qv_wait = 60; }
        else if (match_running() && RDS32(NM_MATCH_STATE) == 4 && scr_stable > 120) qa_pad(BTN_A);
        break;
    case QV_BACK:
        if (scr == SCR_CHAMPIONSHIP && still) qv_step = QV_LIST2;
        break;
    case QV_LIST2:
        if (scr == SCR_CHAMPIONSHIP && still && scr_stable > 600) { qv_step = QV_DONE; quit_in = 60; }
        break;
    }
}
// qa_reg (QA, Regular mode -- 2026-11-07, the two modes' phase 1): Regular's own title and menus driven the way a player
// would, each press keyed to the screen it is meant for (the plain game's start-up time moves by tens of seconds from
// run to run, so presses on a clock miss -- discovery's QA notes). Presses go to the game's controller 1 as the
// controller read gives it (inject_press / qa_pad, pads_out): this rig's own controller holds player 1, so a scripted
// virtual pad couldn't press Start on the title. Bits:
//   1  the boot left to play as the game's (legal page, logos, intro movie); 2 s after the title is up, Start; the
//      main menu (the Rumble Pak notice first) -- Start again every 5 s while still on the title;
//   2  on the main menu 3 s, F2 opened on GAME SETTINGS for 5 s, then EXIT WITHOUT SAVING (its settings read again:
//      the running mode must stay as latched);
//   4  an Exhibition singles match through the game's own pages: Multi Play's Exhibition, MATCH Single Match, PLAYER
//      1P vs CPU, ARENA as the page has it, RULES Start (on past BELT), the Superstar Select qa_w1 for the player then
//      qa_w2 for the CPU (auto_pick), the match card's A; 20 s into the match the CPU plays the player's Superstar too
//      (his controller off, his CPU bit on) so it ends by itself; A through its replay; Next on the results;
//   8  once done, the game quits 10 s later.
// 32768 (with 4) Multi Play's Guest Referee instead of Exhibition (guestref, 2026-11-22): its MATCH page's first row, Single
//      Match (kind 0x81: the two and a guest referee), the PLAYER page's row with controller 1 on slot 0 and the computer
//      on slot 1 and on the referee (slot 2), the select's picks as 4's then the box along; the rest as 4 -- 20 s in, the
//      CPU plays the player's man too, so all three are the computer's.
//  16  (instead of 4) a story of ours from the championship list (W5): Single Play, Championship, the Custom Story row
//      (UP from World Heavyweight -- the row's own link), A; on the stories' list A (the first); the select qa_w1; the
//      story's pages A every 5 s; its match as 4's (the CPU plays him after 20 s, A through the replay);
//  32  in the match (4 or 16), 20 s in: F2's QUIT MATCH (W8) instead of the CPU playing it out;
//  64  done: 3 s later F2's RESET TO TITLE (W8); done again on the title;
// 128  on the ARENA page (4): DOWN twelve times before A -- past the page's ten rows, to one of the mod's arenas (W6).
// state.csv (with qa_modes): bs_grabs = the step below (qa_state).
// (lumberjack2, 2026-11-21) The select's second pick, then any more it asks for, as a player would make them on controller 1:
// a triple threat's third and a tag match's third and fourth (qa_w3, qa_w4), then a Lumberjack match's lumberjacks picked
// by the player (qa_lj_w1..4, then the box under the cursor, or the next one along if that one's taken). 1: busy here.
static int sel_picked(void) {                    // (the picks made: the select's records with a Superstar)
    int n = 0;
    for (u32 i = 0; i < 50; i++) { u16 id = RD16(NM_SEL_REC + i * NM_SEL_REC_SIZE); if (id && id < 0x1000) n++; }
    return n;
}
static int qr_pick_more(void) {
    static int stage, tries, had;
    if (stage == 0) { if (auto_pick((u16)qa_w[1]) == 1) { stage = 1; tries = 0; had = -1; qr_wait = 60; } return 1; }
    // (the game's own count of picks placed, s32 picks_placed 0x800BD948: the select's 50 records keep older picks past it --
    //  b5's select run counted those and took every pick as "any box")
    int made = RDS32(0x800BD948u);
    if (made < 0 || made > 50) made = sel_picked();
    if (made != had) { had = made; tries = 0; pick_steps = 0; }   // (a new pick: auto_pick's own count of presses from 0)
    int e = made - 2, want = 0;                  // (the extra pick's place: 0 the first after the two)
    int comp_extra = lumberjack_armed() ? lumberjack_competitors() - 2 : 0;
    if (e < 0) e = 0;
    if (e < comp_extra) want = qa_w[2 + e];
    else if (e - comp_extra < 4) want = qa_lj_w[e - comp_extra];
    if (want > 0) { int r = auto_pick((u16)want); if (r >= 0) return 1; }   // (-1: not on the pages -- any box, below)
    if (++tries % 60 == 1) inject_press(BTN_DR);                           // (the next box along ...)
    else if (tries % 60 == 30) inject_press(BTN_A);                        // (... picked)
    return 1;
}
static void qa_regular_rumble(void);
static void qa_regular_frame(void) {
    static int over, quit_in, f2_frames;
    if (quit_in > 0 && --quit_in == 0) plat_quit();
    if (qa_pad_frames[0] > 0 || inject_frames > 0) return;      // (the last press still going)
    if (qr_wait > 0) { qr_wait--; return; }
    s16 scr = in_menus() ? RDS16(NM_SCREEN) : -1;
    int still = !menus_loading();
    int main_page = scr == SCR_MULTIPLAY || scr == SCR_SINGLEPLAY || scr == SCR_COMMISSIONER;
    switch (qr_step) {
    case QR_BOOT:
        if (on_title() && scr_stable > 120) { inject_press(BTN_START); qr_step = QR_TITLE; qr_wait = 60; }
        break;
    case QR_TITLE:
        if (main_page) qr_step = QR_MAIN;
        else if (on_title() && scr_stable > 300) { inject_press(BTN_START); qr_wait = 60; }
        break;
    case QR_MAIN:
        if (!main_page || !still || scr_stable < 180) break;
        if (qa_reg & 2) { menu_open(); menu_page = PG_GAME; menu_sel = 0; menu_top = 0; f2_frames = 0; qr_step = QR_F2; }
        else qr_step = QR_MULTI;
        break;
    case QR_F2:
        if (!op_open) { qr_step = QR_MULTI; break; }
        if (++f2_frames >= 300) { menu_action(ACT_EXIT); qr_step = QR_MULTI; qr_wait = 120; }
        break;
    case QR_MULTI:
        if (qa_reg & 1024) { qr_step = QR_OPT_PAGE; break; }          // (W8: RESET TO TITLE from the OPTIONS page)
        if (qa_reg & 16) { qr_step = QR_SP_PAGE; break; }
        if (qa_reg & 256) { qr_step = QR_RR_PAGES; break; }             // (a Rumble from Multi Play: W10, qa_regular_rumble)
        if (!(qa_reg & 4)) { qr_step = QR_DONE; break; }
        // (16384, ljround8: the Lumberjack Match row on Multi Play as a player gets to it -- the cursor on Ironman Match (a
        //  moment: its shot, the row below it not lit), DOWN (the page's own link; where it landed: dbg_lj_nav bits 0-3), a
        //  moment on it (its shot), then A)
        if ((qa_reg & 16384) && scr == SCR_MULTIPLAY && still && scr_stable > 60 && mp_lj_row >= 0) {
            static int mk;
            u32 rec = menu_record(SCR_MULTIPLAY);
            switch (mk) {
            case 0: RD8(rec + 5) = RD8((u32)mp_defs + 0x16u * (u32)mp_lj_row + 6); mk = 1; qr_wait = 200; break;
            case 1: qa_pad(BTN_DD); mk = 2; qr_wait = 60; break;
            case 2: dbg_lj_nav = (dbg_lj_nav & ~0xFu) | (RD8(rec + 5) & 0xFu); mk = 3; qr_wait = 200; break;
            default: mk = 0; qa_pad(BTN_A); qr_step = QR_MATCHPAGE; qr_wait = 60; break;
            }
            break;
        }
        if (scr == SCR_MULTIPLAY && still && scr_stable > 60) {
            // (Exhibition; with 32768 -- guestref, 2026-11-22 -- Guest Referee, Multi Play's fifth item: its sub-headers
            //  run exhibition / royal rumble / pay per view / king of the ring / guest referee / ladder match / ironman
            //  match, mod/NOTES.md; its MATCH page is the menus' context 4, where Single Match makes the kind 0x81)
            RD8(menu_record(SCR_MULTIPLAY) + 5) = (qa_reg & 32768) ? 4 : 0;
            inject_press(BTN_A); qr_step = QR_MATCHPAGE; qr_wait = 60;
        } else if (scr == SCR_SINGLEPLAY && still && scr_stable > 60) { qa_pad(BTN_DL); qr_wait = 90; }   // (Multi Play: the page before)
        break;
    case QR_MATCHPAGE:
        // (ljmenu10, qa_lj_menu + 16: first the page walked, for its pictures frame by frame -- the cursor DOWN over every row
        //  and UP again, B back to Multi Play, Exhibition's own page opened (its rows the game's again: the page's list given
        //  back) and left, the Lumberjack row chosen again -- then on as below)
        if ((qa_reg & 16384) && (qa_lj_menu & 16) && qa_lj_walk < 13) {
            if (!still) break;
            switch (qa_lj_walk) {
            case 0: if (scr == SCR_EXHIBITION && lj_menu && scr_stable > 60) { RD8(menu_record(SCR_EXHIBITION) + 5) = 0; qa_lj_walk = 1; qr_wait = 30; } break;
            case 1: case 2: case 3: case 4: qa_pad(BTN_DD); qa_lj_walk++; qr_wait = 30; break;
            case 5: case 6: case 7: case 8: qa_pad(BTN_DU); qa_lj_walk++; qr_wait = 30; break;
            case 9: qa_pad(BTN_B); qa_lj_walk = 10; qr_wait = 60; break;
            case 10: if (scr == SCR_MULTIPLAY && scr_stable > 60) { RD8(menu_record(SCR_MULTIPLAY) + 5) = 0; qa_pad(BTN_A); qa_lj_walk = 11; qr_wait = 60; } break;
            case 11: if (scr == SCR_EXHIBITION && scr_stable > 90) { qa_pad(BTN_B); qa_lj_walk = 12; qr_wait = 60; } break;
            default: if (scr == SCR_MULTIPLAY && scr_stable > 60) { RD8(menu_record(SCR_MULTIPLAY) + 5) = (u8)mp_lj_row; qa_pad(BTN_A); qa_lj_walk = 13; qr_wait = 60; } break;
            }
            break;
        }
        // (16384, ljround8: the Lumberjack MATCH page -- the cursor on LUMBERJACKS, RIGHT (13: dbg_lj_nav bits 4-9), LEFT (12:
        //  bits 10-15); on PICKED, RIGHT (Select; its shot) and A (Random again: A changes a setting, the game never sees it);
        //  then A on the type qa_lj_menu names: 0 Single Match, 1 Tag Match, 2 Triple Threat Match)
        if ((qa_reg & 16384) && scr == SCR_EXHIBITION && still && scr_stable > 120 && lj_menu) {
            static int lk;
            u32 rec = menu_record(SCR_EXHIBITION);
            switch (lk) {
            case 0: RD8(rec + 5) = 3; lk = 1; qr_wait = 90; break;
            case 1: qa_pad(BTN_DR); lk = 2; qr_wait = 40; break;
            case 2: dbg_lj_nav = (dbg_lj_nav & ~0x3F0u) | (u32)(lj_mc & 0x3F) << 4; lk = 3; qr_wait = 60; break;
            case 3: qa_pad(BTN_DL); lk = 4; qr_wait = 40; break;
            case 4: dbg_lj_nav = (dbg_lj_nav & ~0xFC00u) | (u32)(lj_mc & 0x3F) << 10; RD8(rec + 5) = 4; lk = 5; qr_wait = 40; break;
            case 5: qa_pad(BTN_DR); lk = 6; qr_wait = 150; break;
            case 6: qa_pad(BTN_A); lk = 7; qr_wait = 90; break;
            default: { int t = (qa_lj_menu & 15) > 2 ? 0 : qa_lj_menu & 15; RD8(rec + 5) = (u8)t; lk = 8; qr_wait = 90; break; }
            case 8: qa_pad(BTN_A); lk = 0; qr_step = QR_PLAYERS; qr_wait = 60; break;
            }
            break;
        }
        if (scr == SCR_EXHIBITION && still && scr_stable > 120) {
            RD8(menu_record(SCR_EXHIBITION) + 5) = 0;             // (Single Match)
            inject_press(BTN_A); qr_step = QR_PLAYERS; qr_wait = 60;
        }
        break;
    case QR_PLAYERS:
        if (scr == SCR_PLAYERS && still && scr_stable > 120) {
            u32 rec = menu_record(SCR_PLAYERS), items = RD32(rec + 0x14);
            for (int i = 0; i < PLAYER_ROWS && GAME_PTR(items); i++)   // (the row of 1P vs CPU, option 1: see players_page)
                if (!RD8(items + (u32)i * ROW_SIZE + 0x4B) && RD8(items + (u32)i * ROW_SIZE + 0x61) == 1) RD8(rec + 5) = (u8)i;
            // (guestref, 32768: the Guest Referee single's rows are other options -- the row whose option gives controller 1
            //  to slot 0 and the computer to slot 1 and to the referee, slot 2: the option's six bytes at 0x8014FA0C + 6 x
            //  option, bytes +3 / +4 / +2 for slots 0 / 1 / 2 in a 0x81 match -- set_slot_controllers_from_player_option, func_801353F8, read)
            if (qa_reg & 32768)
                for (int i = 0; i < PLAYER_ROWS && GAME_PTR(items); i++) {
                    if (RD8(items + (u32)i * ROW_SIZE + 0x4B)) continue;
                    u32 e = 0x8014FA0Cu + 6u * (u32)RD8(items + (u32)i * ROW_SIZE + 0x61);
                    if (RD8(e + 3) == 1 && RD8(e + 4) == 0 && RD8(e + 2) == 0) { RD8(rec + 5) = (u8)i; break; }
                }
            inject_press(BTN_A); qr_step = QR_ARENA; qr_wait = 60;
        }
        break;
    case QR_ARENA: {
        static int downs;
        if (scr == SCR_ARENA && still && scr_stable > 120) {
            if ((qa_reg & 128) && downs < 12) { qa_pad(BTN_DD); downs++; qr_wait = 40; break; }
            // (A on the controller, as the player's: the ARENA page's window takes the row from the controller's press --
            //  arena_page_pad -- an injected one isn't seen there, and the game took its own row's arena, 2026-11-07)
            qa_pad(BTN_A); qr_step = QR_RULES; qr_wait = 60;
        }
        break;
    }
    case QR_RULES:     // (Start: on past the BELT tab to the select, the remaining choices as they are)
        if ((scr == SCR_RULES || scr == 12) && still && scr_stable > 120) { inject_press(BTN_START); qr_wait = 90; }
        else if (is_select(scr)) qr_step = QR_PICK1;
        break;
    case QR_PICK1: {   // (both picked on controller 1: the game's 1P vs CPU)
        // (4096, W7: on the select first, F2 opened, BIG SHOW turned off, SAVE AND EXIT -- the game in a select: its
        //  roster must keep him until the menus are back between games)
        static int roster_done;
        if ((qa_reg & 4096) && !roster_done && is_select(scr) && still && scr_stable > 60) {
            roster_done = 1; menu_open(); roster_opt[RO_BIG_SHOW] = 0; menu_action(ACT_SAVE_EXIT); qr_wait = 120; break;
        }
        if (is_select(scr) && still && scr_stable > 60 && auto_pick((u16)qa_w[0]) == 1) { qr_step = QR_PICK2; qr_wait = 60; }
        break;
    }
    case QR_PICK2:
        if (is_select(scr) && still) { if (qr_pick_more()) break; }
        else if (scr == SCR_VS && still && scr_stable > 180) inject_press(BTN_A);
        else if (match_running()) { qr_step = QR_FIGHT; over = 0; }
        break;
    case QR_FIGHT: {
        static int quit_asked;
        if ((qa_reg & 32) && !quit_asked && match_running() && RD16(NM_MATCH_FRAMES) != 0xFFFF && RD16(NM_MATCH_FRAMES) > 20u * 60u) {
            quit_asked = 1; menu_open(); menu_action(ACT_QUIT_MATCH);     // (W8: F2's QUIT MATCH, as the operator would)
        }
        if (!(qa_reg & 32) && match_running() && RD16(NM_MATCH_FRAMES) != 0xFFFF && RD16(NM_MATCH_FRAMES) > 20u * 60u)
            for (int sl = 0; sl < 4; sl++) {
                s32 pad = RDS32(NM_SLOT_PAD + 4u * (u32)sl);
                if (pad == 1 || pad == 2) { RDS32(NM_SLOT_PAD + 4u * (u32)sl) = 0; RD8(W(sl) + 0xE1) |= 0x08; }
            }
        if (match_running() && RDS32(NM_MATCH_STATE) == 4) { if (++over >= 120 && over % 60 == 0) inject_press(BTN_A); }   // (its replay)
        else over = 0;
        if (in_menus() && scr != SCR_MATCH && !match_running()) qr_step = QR_AFTER;
        break;
    }
    case QR_AFTER:     // (the results: Next -- then wherever the game goes, 10 s, and done)
        if ((scr == SCR_MATCH_RESULTS || scr == SCR_RESULTS) && still && scr_stable > 180) {
            u32 rec = menu_record(scr), items = RD32(rec + 0x14);
            for (int i = 0; i < 8 && GAME_PTR(items); i++) {
                u32 def = RD32(items + (u32)i * 0x88u + 0x64);
                if (def_ptr(def) && RD16(def + 0x12) == ACT_RESULTS_NEXT) RD8(rec + 5) = (u8)i;
            }
            inject_press(BTN_A); qr_wait = 120;
        } else if (in_menus() && scr != SCR_MATCH_RESULTS && scr != SCR_RESULTS && scr != SCR_MATCH && still && scr_stable > 600) {
            qr_step = QR_DONE;
        } else if (match_running() && RDS32(NM_MATCH_STATE) == 4 && scr_stable > 120) inject_press(BTN_A);
        break;
    case QR_DONE: {
        static int reset_done;
        if ((qa_reg & 64) && !reset_done && in_menus() && still && scr_stable > 180) {
            reset_done = 1; menu_open(); menu_action(ACT_RESET); qr_step = QR_RESET; break;   // (W8: F2's RESET TO TITLE)
        }
        if ((qa_reg & 8) && !quit_in) quit_in = 600;
        break;
    }
    case QR_RESET:     // (until the title is up again -- 1024: and 20 s on it, to see it stay)
        if (on_title() && scr_stable > ((qa_reg & 1024) ? 1200 : 60)) qr_step = QR_DONE;
        break;
    case QR_SP_PAGE:   // (Multi Play -> Single Play: RIGHT; there, Championship -- its first item -- A)
        if (scr == SCR_MULTIPLAY && still && scr_stable > 60) { qa_pad(BTN_DR); qr_wait = 90; }
        else if (scr == SCR_SINGLEPLAY && still && scr_stable > 60) {
            RD8(menu_record(SCR_SINGLEPLAY) + 5) = 0; inject_press(BTN_A); qr_step = QR_SP_LIST; qr_wait = 60;
        }
        break;
    case QR_SP_LIST:   // (the championship list: UP from World Heavyweight, the Custom Story row's own link)
        if (scr == SCR_CHAMPIONSHIP && still && scr_stable > 120) {
            RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = 0; qa_pad(BTN_DU); qr_step = QR_SP_OPEN; qr_wait = 90;
        }
        break;
    case QR_SP_OPEN:   // (on the row: A -- a press on the controller, seen by the mod's own row handling)
        if (scr == SCR_CHAMPIONSHIP && still) { qa_pad(BTN_A); qr_step = QR_SP_PICK; qr_wait = 150; }
        break;
    case QR_SP_PICK:   // (the stories' list up: A on its first -- with 512, on "Retry Test", the game-over test's story)
        if (scr == SCR_CHAMPIONSHIP && list_on && still) {
            if (qa_reg & 512)
                for (int i = 0; i < list_count(); i++) {
                    const char* t = story_title(i);
                    if (t && t[0] == 'R' && t[1] == 'e' && t[2] == 't' && t[3] == 'r' && t[4] == 'y') {
                        list_top = (i / LIST_ROWS) * LIST_ROWS; list_rows(); RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = (u8)(i - list_top);
                    }
                }
            qa_pad(BTN_A); qr_step = QR_SP_SELECT; qr_wait = 60;
        }
        break;
    case QR_SP_SELECT:
        if (is_select(scr) && still && scr_stable > 60 && auto_pick((u16)qa_w[0]) == 1) { qr_step = QR_SP_PAGES; qr_wait = 60; }
        break;
    case QR_SP_PAGES:  // (the story's own pages wait for A: one every 5 s; its RULES Start)
        if (qa_reg & 512) {            // (W12: the story played to its end -- the matches its script ends; A through
            static int t2;             //  everything else, every 5 s; done on the title, the story over)
            if (on_title() && !story_active()) { qr_step = QR_DONE; break; }
            if (match_running() && RDS32(NM_MATCH_STATE) == 4) { if (++t2 % 60 == 0) inject_press(BTN_A); break; }
            if (!match_running() && !is_select(scr) && scr != SCR_CHAMPIONSHIP && ++t2 % 300 == 0) inject_press((t2 / 300) & 1 ? BTN_A : BTN_START);
            break;
        }
        if (match_running()) { qr_step = QR_FIGHT; over = 0; break; }
        // (the story's scenes are pages too, out of the menus: a press every 5 s wherever it is, but the select)
        // (A, then Start the next time: a story's RULES page -- "Rules are not selectable" -- goes on with A, an
        //  Exhibition's with Start; 2026-11-07, the first run waited on it with Start)
        { static int t; if (!is_select(scr) && scr != SCR_CHAMPIONSHIP && ++t % 300 == 0) inject_press((t / 300) & 1 ? BTN_A : BTN_START); }
        break;
    case QR_RR_PAGES:
        qa_regular_rumble();
        break;
    case QR_OPT_PAGE:  // (1024: the main menu's third page -- SmackDown Mall / Options -- RIGHT twice; Options, A)
        if ((scr == SCR_MULTIPLAY || scr == SCR_SINGLEPLAY) && still && scr_stable > 60) { qa_pad(BTN_DR); qr_wait = 90; }
        else if (scr == SCR_COMMISSIONER && still && scr_stable > 60) { RD8(menu_record(SCR_COMMISSIONER) + 5) = 1; inject_press(BTN_A); qr_step = QR_OPT_WAIT; qr_wait = 60; }
        break;
    case QR_OPT_WAIT:  // (on the OPTIONS page 3 s: F2's RESET TO TITLE; then 20 s to see it stay on the title, done)
        if (scr == 18 && still && scr_stable > 180) { menu_open(); menu_action(ACT_RESET); qr_step = QR_RESET; }
        break;
    }
}
// qa_reg 256 (W10): a Royal Rumble from Regular's own Multi Play menu, as a player would: Royal Rumble (Multi Play's
// second item), A; ORDER: the Random row (label 0x8522), A; NUMBER: RIGHT qa_reg_right times (0: F2's count as it
// comes -- the per-match change test uses 2), then A; PLAYER and on: Start (the pages' skip-ahead, the remaining
// choices as they are); the Superstar Select: qa_w1 for the player, then A on whoever is under the cursor for any other
// pick it asks for; in the match the CPU plays the player's Superstar from 10 s (so the Rumble goes on by itself).
static void qa_regular_rumble(void) {
    static int rs, presses, picked;
    s16 scr = in_menus() ? RDS16(NM_SCREEN) : -1;
    int still = !menus_loading();
    switch (rs) {
    case 0:   // Multi Play: Royal Rumble
        if (scr == SCR_MULTIPLAY && still && scr_stable > 60) { RD8(menu_record(SCR_MULTIPLAY) + 5) = 1; inject_press(BTN_A); rs = 1; qr_wait = 90; }
        break;
    case 1: { // ORDER (screen 7, the Rumble's rows): Random
        if (scr != SCR_EXHIBITION || !still || scr_stable < 120) break;
        u32 rec = menu_record(SCR_EXHIBITION), items = RD32(rec + 0x14);
        for (int i = 0, shown = 0; i < 17 && GAME_PTR(items); i++) {
            if (RD8(items + (u32)i * 0x88u + 0x4B)) continue;
            shown++;
            if (qa_reg_order > 0 ? shown == qa_reg_order : RD16(items + (u32)i * 0x88u + 0x60) == 0x8522) RD8(rec + 5) = (u8)i;
        }
        inject_press(BTN_A); rs = 2; qr_wait = 90;
        break;
    }
    case 2:   // NUMBER (screen 9): RIGHT qa_reg_right times, then A
        if (scr != 9 || !still || scr_stable < 120) break;
        // (UP on the controller, as a player's: the widget is a type-5 row, which steps with UP / DOWN -- the menus' loop,
        //  L_80111544 -- not LEFT / RIGHT, which the first runs pressed, 2026-11-07; a controller's press reaches the
        //  menus' repeat word 0x80106640 the game's own way)
        if (presses < qa_reg_right) { qa_pad(BTN_DU); presses++; qa_number_way = presses; qr_wait = 40; break; }
        // (2048, W10b: the page's own rows -- NUMBER UP x3, RIGHT to IN THE RING, UP x2, RIGHT to ENTRY TIME, DOWN x2,
        //  RIGHT to STARTS WITH, UP: from F2's 12 / 6 / 30 / 2 to 15 / 8 / 20 / 4)
        if (qa_reg & 2048) {
            static const u16 seq[] = { BTN_DU, BTN_DU, BTN_DU, BTN_DR, BTN_DU, BTN_DU, BTN_DR, BTN_DD, BTN_DD, BTN_DR, BTN_DU };
            static int k;
            if (k < (int)(sizeof seq / sizeof seq[0])) { qa_pad(seq[k++]); qa_number_way = k; qr_wait = 40; break; }
        }
        // (65536, rumblemenu 2026-11-25: which setting is current -- its caption lit -- as LEFT / RIGHT go round, and an even
        //  and an odd value of NUMBER and IN THE RING: 2 s on each step for its shots. UP on NUMBER, RIGHT to IN THE RING, UP
        //  x2, RIGHT to ENTRY TIME, RIGHT to STARTS WITH, RIGHT round to NUMBER, LEFT back to STARTS WITH, RIGHT to NUMBER)
        if (qa_reg & 65536) {
            static const u16 seq2[] = { BTN_DU, BTN_DR, BTN_DU, BTN_DU, BTN_DR, BTN_DR, BTN_DR, BTN_DL, BTN_DR };
            static int k2, first;
            if (!first) { first = 1; qr_wait = 120; break; }          // (as it opens: NUMBER's caption lit)
            if (k2 < (int)(sizeof seq2 / sizeof seq2[0])) { qa_pad(seq2[k2++]); qa_number_way = k2; qr_wait = 120; break; }
        }
        qr_wait = 120; rs = 3;                    // (a moment on it as it ends up: its shot)
        break;
    case 3:
        if (scr == 9 && still) { inject_press(BTN_A); rs = 4; qr_wait = 90; }
        break;
    case 4:   // PLAYER on: Start, until the select
        // (65536: PLAYER and ARENA with A, as they come, to the RULES page -- its rows as the page opens them; the run stays there)
        if (qa_reg & 65536) {
            if (scr == SCR_RULES) { rs = 7; break; }
            if (in_menus() && still && scr_stable > 120 && scr != 9) { inject_press(BTN_A); qr_wait = 90; }
            break;
        }
        if (is_select(scr)) { rs = 5; break; }
        if (in_menus() && still && scr_stable > 120 && scr != 9) { inject_press(BTN_START); qr_wait = 90; }
        break;
    case 5:   // the select (then the match card: A after 3 s, as a player's)
        if (scr == SCR_VS && still && scr_stable > 180) { inject_press(BTN_A); qr_wait = 120; break; }
        if (!is_select(scr)) { if (match_running() || (!in_menus() && RDS32(NM_MODE) == 0)) rs = 6; break; }
        if (!still || scr_stable < 60) break;
        if (!picked) { if (auto_pick((u16)qa_w[0]) == 1) picked = 1; qr_wait = 30; }
        else { static int k; if (++k & 1) qa_pad(BTN_DR); else inject_press(BTN_A); qr_wait = 60; }   // (another box each pick)
        break;
    case 6:   // the match: the CPU plays the player from 10 s
        // (8192, phase 3: the player kept on his controller -- the CPU throws him out sooner or later -- and on his
        //  CONTINUE? (RUMBLE CONTINUE on) Start, 2 s into it, as a player's)
        if (qa_reg & 8192) {
            // (the CPU plays him from 10 s, as below -- a player standing still is never thrown out (run 200733: 20
            //  minutes of game), and putting him on the floor isn't out under the game's own rules (no COUNT OUT; runs
            //  204228..215151) -- and once the game has him out, his controller is his again, as a player's is when he's
            //  thrown out: his CONTINUE? and its countdown, then Start every 2 s, as a player's)
            static int t, my_slot = -1, my_pad;
            extern u32 qa_put_outs;
            if (match_running() && RD16(NM_MATCH_FRAMES) != 0xFFFF && RD16(NM_MATCH_FRAMES) > 10u * 60u && my_slot < 0)
                for (int sl = 0; sl < nm_slots(); sl++) {
                    s32 pad = RDS32(NM_SLOT_PAD + 4u * (u32)sl);
                    if (pad >= 1 && pad <= 4) { my_slot = sl; my_pad = pad; RDS32(NM_SLOT_PAD + 4u * (u32)sl) = 0; RD8(W(sl) + 0xE1) |= 0x08; break; }
                }
            if (my_slot >= 0 && RDS32(NM_SLOT_PAD + 4u * (u32)my_slot) == 0 && RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)my_slot) >= 0) {
                RDS32(NM_SLOT_PAD + 4u * (u32)my_slot) = my_pad; RD8(W(my_slot) + 0xE1) &= (u8)~0x08u;   // (out: his own again)
                qa_put_outs++;
            }
            if (rr_player_out && match_running() && ++t % 120 == 0) qa_pad(BTN_START);
            break;
        }
        if (match_running() && RD16(NM_MATCH_FRAMES) != 0xFFFF && RD16(NM_MATCH_FRAMES) > 10u * 60u)
            for (int sl = 0; sl < nm_slots(); sl++) {
                s32 pad = RDS32(NM_SLOT_PAD + 4u * (u32)sl);
                if (pad >= 1 && pad <= 4) { RDS32(NM_SLOT_PAD + 4u * (u32)sl) = 0; RD8(W(sl) + 0xE1) |= 0x08; }
            }
        break;
    }
}
static void qa_op_frame(void) {
    if (qa_op > 0 && match_running()) {
        u16 mf = RD16(NM_MATCH_FRAMES);
        if (mf != 0xFFFF && mf > (u32)qa_op_at * 60u) {
            if (qa_op == 20) { menu_open(); menu_page = PG_PICTURE; menu_sel = 1; qa_pic_walk = 1; }     // (QA: the PICTURE page: see qa_pic_step)
            else if (qa_op == 21) { menu_open(); menu_page = PG_CONTROLS; menu_sel = 7; }   // (QA: the CONTROLS page, for a picture)
            else if (qa_op >= 22 && qa_op <= 27) {     // (QA: a buttons page -- 22 CONTROLLER BUTTONS (ALL), 23 KEYBOARD, 24..27
                                                       //  P1..P4 CONTROLLER BUTTONS; qa_bind_row: a capture of that row)
                menu_open(); menu_page = qa_op == 22 ? PG_PADKEYS : qa_op == 23 ? PG_KEYS : PG_P1KEYS + qa_op - 24; menu_sel = 0;
                if (qa_bind_row > 0 && qa_bind_row <= BIND_ROWS + 1) { menu_sel = bind_pos(qa_bind_row - 1); bind_begin(bind_page_dev(), qa_bind_row - 1); }
            }
            else { menu_open(); menu_action(qa_op - 1); }
            qa_op = 0;
        }
    }
}
static void qa_frame(void) {
    qa_op_frame();
    if (qa_cam_from > 0) {
        extern float qa_cam[7];
        u16 mf = RD16(NM_MATCH_FRAMES);
        qa_cam[0] = mf != 0xFFFF && mf > (u32)qa_cam_from * 60u ? 1.0f : 0.0f;
    }
    // (the intro movie skipped: singles, the menus' own leave-intro; the Royal Rumble -- the arcade's logic running
    // alongside, its legal page taken out -- Start, a player's way: that one with both left the intro stuck)
    if (intro_running()) {
        // (the Royal Rumble: once the legal page is gone -- the arcade's intro_service rewrites it, and leaving the
        // intro while it does left it stuck -- the same leave-intro, not a minute of the movie)
        if (qa_on == 1 || RD32(NM_INTRO_ENTRY) != NM_INTRO_FIRST) RDS32(NM_MODE) = 1;
        else if (inject_frames == 0 && (frame_no % 30u) == 0) inject_press(BTN_START);
    }
    // (on the title, no attract demo: the Rumble is asked for at once)
    if (qa_on == 2 && qa_stage != QA_MATCH && on_title()) RDS32(NM_TITLE_IDLE) = 0;
    s32 mode = RDS32(NM_MODE);
    if (qa_stage == QA_MATCH) {
        if (mode == -6) {
            // keep it a real match: the game sets the demo bit again when it switches to demo mode, and
            // gives demos a minute (logic_v1's enforce_real_match)
            u32 f = RD32(NM_MATCH_FLAGS);
            if (f & NM_FLAG_DEMO) RD32(NM_MATCH_FLAGS) = f & ~NM_FLAG_DEMO;
            if (RDS8(NM_TIME_LIMIT) == 7) RD8(NM_TIME_LIMIT) = 0;
            u16 fr = RD16(NM_MATCH_FRAMES);
            if (fr != 0xFFFF) qa_last_frames = fr;
            // (here the mode is -6: match_running() never was -- the meter wasn't filled in a QA singles match.
            // qa_special 4: filled again every 5 s, his specials the game's own pick whenever they can be)
            if (fr != 0xFFFF && fr >= 10 && (!qa_special_done || ((qa_special & 4) && fr % 300 == 0))) {
                for (int s = 0; s < 2; s++) if (qa_special & (1 << s)) full_power(s);
                qa_special_done = 1;
            }
            // the match over: its replay loops until A (a player's press) -- A every second, after two
            if (RDS32(NM_MATCH_STATE) == 4) {
                if (++qa_over_frames >= 60 * qa_over_wait && qa_over_frames >= 120 && qa_over_frames % 60 == 0 && inject_frames == 0) inject_press(BTN_A);
            } else qa_over_frames = 0;
        } else if (on_title() && qa_last_frames > 0) {             // the match is over: back on the title
            qa_done++; qa_last_frames = 0; qa_stage = QA_TITLE;
            if (qa_matches > 0 && qa_done >= qa_matches) qa_quit_in = 60;   // (a second: the state gets written out)
        }
    }
    if (qa_quit_in > 0 && --qa_quit_in == 0) plat_quit();
    if (qa_on != 1) return;
    if (qa_stage != QA_MATCH && on_title()) {
        // the next demo is this one, now (asked again every frame on the title: the table is the title
        // overlay's, and the attract counter mustn't pick the intro movie)
        const int men = qa_set_men[qa_demo_set];
        if (qa_stage == QA_TITLE) {
            for (int k = 0; k < 4; k++) {   // (each a different Superstar: one the scenario names, or anyone left)
                u16 id = 0;
                if (k < men) for (int tries = 0; tries < 50; tries++) {
                    id = qa_pick(k, 0); int dup = 0;
                    for (int j = 0; j < k; j++) if (qa_men[j] == id) dup = 1;
                    if (!dup || (qa_w[k] > 0 && qa_w[k] < 0x1000)) break;
                }
                qa_men[k] = id;
            }
            qa_a = qa_men[0]; qa_b = qa_men[1]; qa_stage = QA_DEMO;
        }
        for (int r = qa_set_first[qa_demo_set]; r < qa_set_end[qa_demo_set]; r++) {
            u32 a = NM_DEMO_TABLE + 10u * (u32)r;
            for (int k = 0; k < 4; k++) RD16(a + 2u * (u32)k) = k < men ? (u16)(qa_men[k] | qa_c[k] << 12) : 0;
        }
        RD8(NM_ATTRACT_COUNTER) = (u8)(qa_demo_set * 2);
        s32 idle = RDS32(NM_TITLE_IDLE);
        if (idle >= 0 && idle < NM_TITLE_IDLE_LIMIT - 2) RDS32(NM_TITLE_IDLE) = NM_TITLE_IDLE_LIMIT - 2;
    }
}

// The operator menu (F2) for this frame, in both modes: F2 opens it (or saves and closes it); while it is open it is
// the whole frame -- its page drawn over the game, the game's own picture held (draw_menu_frame) -- and the caller
// returns. (Moved out of arcade_frame as it was, 2026-11-07, so Regular's frame has the same F2; QA's qa_pic_walk too.)
// Returns 1 when the menu took the frame (flushed already), 0 when it is closed.
static int operator_menu_frame(u32 task) {
    if (key_edge(SC_F2) && !wiz_player && !bind_on) {   /* (roster 2026-11-19: not while a binding waits for its key) */ if (op_open) menu_action(ACT_SAVE_EXIT); else menu_open(); }
    if (op_open && qa_pic_walk) {
        static int t; t++;
        if (t == 240) pic_shape = 1;
        if (t == 480) { menu_sel = 4; pic_crt = 2; }
        if (t == 720) { menu_sel = 0; pic_window = 1; }
        if (t == 960) pic_window = 0;
        if (t == 1200) { qa_pic_walk = 0; picture_send(); menu_action(ACT_SAVE_EXIT); }
    }
    if (op_open) { menu_tick(); picture_send(); host_send(); if (op_open) { menu_draw(); draw_menu_frame(); } draw_flush(task); return 1; }
    return 0;
}

// SPLASH SCREENS and ATTRACT (W9), both modes, each frame.
//  - SPLASH SCREENS off: at power-on, the intro (legal page, logos, movie) left the menus' own way the moment it can be
//    (NM_MODE 1, what the QA bench does to leave it) -- straight to the title. Only before the title's first showing:
//    the attract's own intro movie later is the attract's.
//  - ATTRACT off: on the title, the game's idle count held at 0, so neither its movie nor a demo ever starts; the
//    title waits (Arcade: INSERT COIN on it). Not under a QA bench, whose demo match is started from the title.
static void splash_attract_service(void) {
    if (on_title()) title_seen = 1;
    if (!splash_on && !title_seen && intro_running()) RDS32(NM_MODE) = 1;
    if (!attract_on && !qa_on && on_title()) {
        s32 idle = RDS32(NM_TITLE_IDLE);
        if (idle > 0) RDS32(NM_TITLE_IDLE) = 0;
    }
}

// Menu screen bookkeeping, both modes: how many frames the menus' screen has stayed the same (scr_stable; -1 out of
// the menus) -- the arcade's routing and QA's drivers wait on it.
static void screen_watch(int dt) {
    s16 scr = in_menus() ? RDS16(NM_SCREEN) : -1;
    if (scr == scr_prev) scr_stable += dt; else scr_stable = 0;
    scr_prev = scr;
}

// Regular mode's part of the frame (2026-11-07), after the universal part of arcade_frame: the rest of the mod's
// enhancements that live in the frame -- everything unlocked, the costume variants at the Superstar Select -- then
// QA's benches and drivers and the operator menu. Nothing of the coin-op: the game's own title, menus, attract and
// pause menu run as they are. (Still to come in Regular, the plan's phase 2: the Custom Story row on the championship
// list (W5), the ARENA page's window past ten rows on any ARENA page (W6), roster changes between games (W7), F2's
// QUIT MATCH / RESET TO TITLE (W8), the toggles (W9), the F2 Rumble settings in the game's own Rumble (W10).)
// ABBREVIATED MENUS in Regular (W9; off by default): Story Mode's pages that need nothing of the player go by -- the
// chapter card skipped as Arcade's is (story_card_skip, phase 3), the RULES page (fixed by the story) at once, the match
// card after 5 s (A sooner) -- and the story's own pages move on by themselves (arcade_story_page). Story Mode is the
// menus' context 8 (NM_CHAMP_CONTEXT, set as a championship is chosen).
static void regular_short_menus(void) {
    if (!short_menus || !in_menus() || RD8(NM_CHAMP_CONTEXT) != CONTEXT_CHAMPIONSHIP) { card_confirmed = 0; return; }
    s16 scr = RDS16(NM_SCREEN);
    if (scr != SCR_STORY_CARD) card_confirmed = 0;
    if (scr == SCR_STORY_CARD && !dbg_no_card_skip) { story_card_skip(scr, RDS32(NM_SCENE_STEP)); return; }
    if (menus_loading()) return;
    if ((scr == SCR_RULES && scr_stable > 2) || (scr == SCR_VS && scr_stable > 300)) { menu_confirm(scr, -1); scr_stable = 0; }
}
// F2's QUIT MATCH and RESET TO TITLE in Regular (W8, 2026-11-07), each frame: the match left through its own exit, the
// way the game's pause menu's QUIT MATCH and the arcade's routing leave one (NM_MATCH_QUIT, then NM_MATCH_LEAVE frames of
// fade) -- its entrances first run to their end (step 30: the match starts), a fade-in first finished. QUIT MATCH leaves
// the screen the menus open on as the game set it for the match (the game's own way back); RESET TO TITLE sets the
// title (NM_RETURN_SCREEN), and from the menus starts them over there (NM_MODE -1, as the arcade's G_TITLE). An attract
// demo is ended with its own fade-out. Given up after a minute.
static int reg_goal_frames, reg_leave_asked, reg_settle;
static void regular_route(int dt) {
    if (!reg_goal) { reg_goal_frames = 0; reg_leave_asked = 0; return; }
    reg_goal_frames += dt;
    if (reg_goal_frames > 60 * 60) { reg_goal = 0; return; }
    s32 mode = RDS32(NM_MODE);
    if (mode == -6) {                                 // (a demo: its own fade-out, its fade-in over first)
        if (RD16(NM_MATCH_FRAMES) != 0xFFFF && RD8(NM_MATCH_FADING_IN) == 0) RD8(NM_FADE_FLAGS) |= 0x80;
        return;
    }
    if (!in_menus()) {
        s32 step = RDS32(NM_SCENE_STEP);
        if (mode == 0 && RDS16(NM_SCREEN) == SCR_MATCH && step > 0 && step < 30) RDS32(NM_SCENE_STEP) = 30;
        if (match_running() && !reg_leave_asked && RDS16(NM_MATCH_LEAVE) == 0 && RD8(NM_MATCH_DONE) == 0 &&
            RD8(NM_MATCH_FADING_IN) == 0) {
            if (RDS16(NM_MATCH_PAUSED)) RDS16(NM_MATCH_PAUSED) = 0;
            RD8(NM_MATCH_QUIT) = 1;
            RDS16(NM_MATCH_LEAVE) = 60;
            reg_leave_asked = 1;
        }
        if (reg_goal == 2 && (reg_leave_asked || mode != -2)) RDS16(NM_RETURN_SCREEN) = SCR_TITLE;
        if (reg_goal == 3 && (reg_leave_asked || mode != -2)) RDS16(NM_RETURN_SCREEN) = SCR_SINGLEPLAY;
        return;
    }
    s16 scr = RDS16(NM_SCREEN);
    if (scr == SCR_TRANS_A || scr == SCR_TRANS_B || scr == SCR_MATCH || menus_loading()) return;
    if (reg_goal == 3) {                                               // (out of the story's match: the story let go)
        story_stop(); sp_custom = 0; custom_launch = 0;
        RD8(NM_CHAMP_CONTEXT) = 0;
        reg_goal = 0; return;
    }
    if (reg_goal == 1) { reg_goal = 0; return; }                       // (back in the menus)
    // (RESET: on the title -- then 5 s of watching it: the game's own flow under the menus -- a story's, from its chapter
    //  card's OPTIONS, phase 2 run 190019 -- took the menus off the title again by itself, to the main menu's Options
    //  page; leaving the title with nobody pressing anything in that time, they're started over on it again. A press
    //  is the player's: the watch ends.)
    if (reg_goal == 4) {
        if (reg_pressed || --reg_settle <= 0) { reg_goal = 0; return; }
        if (scr == SCR_TITLE) return;
    } else if (scr == SCR_TITLE) { reg_goal = 4; reg_settle = 300; reg_pressed = 0; return; }
    RDS16(NM_RETURN_SCREEN) = SCR_TITLE;                               // (the menus start over on their title)
    RDS32(NM_MODE) = -1;
}
// The game-menu Rumble in Regular (W10), each frame: its state let go once the match is (the arcade's own frame does
// this for Arcade); the match copy of F2's values let go when the menus go to another match type (a Rumble chosen
// again takes a fresh copy). And RUMBLE CONTINUE in Regular (W9, off by default): the player eliminated is held (his
// slot counts as in: arcade_elim_check), CONTINUE? and the countdown (CONTINUE TIMER) over the match; his Start (no
// credits in Regular) puts him back in where he is at full power (rr_back_in, the arcade's); the countdown run out, he's
// out and the Rumble goes on to its end. (One player: the first slot a controller drives.)
static int reg_cont[NM_SLOTS_CAP];      // (frames each human slot's CONTINUE? has run; 0: none)
// (the Rumble set up -- RR_START -- is on its way to the match through the match card and the loading screens: only a
//  page of the menus' own past those lets it go; the first run let it go on the match card, 2026-11-07)
static int rr_on_way(s16 scr) { return scr == SCR_MATCH || scr == SCR_VS || scr == SCR_TRANS_A || scr == SCR_TRANS_B || is_select(scr); }
static void regular_rumble(void) {
    if ((rr_mode == RR_START || rr_mode == RR_LIVE) && in_menus() && !rr_on_way(RDS16(NM_SCREEN))) rr_mode = RR_OFF;
    if (rr_mode == RR_OFF && !match_running()) gm_live = 0;
    if (gm_on && in_menus() && !(RD32(NM_SEL_FLAGS) & 0x200u) && RDS16(NM_SCREEN) == SCR_MULTIPLAY) gm_on = 0;
    if (!rr_live() || !rr_cont_on) { for (int k = 0; k < NM_SLOTS_CAP; k++) reg_cont[k] = 0; return; }
    // (phase 3: each player his own -- every slot a controller drives: eliminated, his countdown starts; his Start puts him
    //  back in his own slot; while any countdown runs, the players' slots count as in -- rr_player_out, arcade_elim_check)
    int counting = 0, shown = 0;
    static u16 st_prev[4];
    u16 st_now[4];
    for (int c = 0; c < 4; c++) st_now[c] = RD16(NM_PADS + 6u * (u32)c);
    for (int s = 0, slots = nm_slots(); s < slots && s < NM_SLOTS_CAP; s++) {
        s32 pad = RDS32(NM_SLOT_PAD + 4u * (u32)s);
        if (rr_absent(s) || pad < 1 || pad > 4) { reg_cont[s] = 0; continue; }
        if (!reg_cont[s]) {
            if (RDS32(NM_SLOT_ELIM_TIME + 4u * (u32)s) >= 0 && !(RD8(W(s) + 0xE1) & 0x10)) reg_cont[s] = 1;   // (out, not back yet)
            else continue;
        }
        if (reg_cont[s] < 0) continue;                       // (his countdown ran out: out for good)
        int secs = continue_secs - reg_cont[s]++ / 60;
        int start = (st_now[pad - 1] & BTN_START) && !(st_prev[pad - 1] & BTN_START);
        if (start && reg_cont[s] > 30 && rr_restore_due == 0) { rr_back_slot = s; rr_continue(0); reg_cont[s] = 0; continue; }
        if (secs < 0) { reg_cont[s] = -1; continue; }
        counting = 1;
        // (each player's countdown where his own words go: player 1 left, player 2 right, 3 and 4 below them)
        int x = (pad - 1) & 1 ? 360 : 120, y = (pad - 1) & 2 ? 150 : 60;
        static const char* const who[4] = { "PLAYER 1", "PLAYER 2", "PLAYER 3", "PLAYER 4" };
        draw_gtext(who[pad - 1], GT_GLOW, x, y, 0xFFFFFFFFu);
        draw_gtext("CONTINUE?", GT_BIG | GT_GLOW, x, y + 16, 0xFFE040FFu);
        draw_gtext(number_words[secs > 30 ? 30 : secs], GT_BIG | GT_GLOW, x, y + 48, 0xFFFFFFFFu);
        shown++;
    }
    for (int c = 0; c < 4; c++) st_prev[c] = st_now[c];
    if (shown) draw_gtext("PRESS START", GT_GLOW, 240, 200, COL_PROMPT);
    rr_player_out = counting || rr_restore_due > 0;
}
static void qa_regular_frame(void);
static void regular_frame(u32 task, int dt) {
    n_says = 0;
    unlock_everything();
    if (in_menus() && is_select(RDS16(NM_SCREEN)) && !menus_loading())   // (costume variants: variants.c)
        variant_select_frame(menu_record(RDS16(NM_SCREEN)));
    if (dbg_sel_action) {
        dbg_sel_result = dbg_sel_action < 0 ? menu_confirm(dbg_sel_screen, dbg_sel_item) : menu_choose(dbg_sel_screen, dbg_sel_item, dbg_sel_action);
        dbg_sel_action = 0;
    }
    screen_watch(dt);
    if (on_title() || intro_running()) booted = 1;
    splash_attract_service();                                  // (W9: SPLASH SCREENS / ATTRACT)
    regular_short_menus();                                     // (W9: ABBREVIATED MENUS in Regular)
    // the Custom Story row and the stories' list on the championship list (W5): the same list and launch as Arcade's
    champ_custom_defs();
    custom_label_lend();
    exh_lumberjack_slot();                                     // (lumberjack: the Exhibition MATCH page's row)
    if (custom_open_pending) {
        custom_open_pending = 0;
        if (in_menus() && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP && !menus_loading()) list_open();
    }
    custom_frame();
    if (in_menus() && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP && !list_on && !custom_launch && !custom_open_pending) sp_custom = 0;
    regular_route(dt);                                         // (W8: F2's QUIT MATCH / RESET TO TITLE, Regular's way)
    regular_rumble();                                          // (W10: the game-menu Rumble's state; W9: its continue)
    if (qa_on == 1) { qa_frame(); draw_flush(task); return; }   // (a QA singles match: the bench works in both modes)
    if (qa_reg) qa_regular_frame();                            // (QA: Regular's own menus, driven)
    if (operator_menu_frame(task)) return;
    draw_flush(task);
}

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
    demo_table_removed();             // (2026-11-13, removal: the game's own demo table without a removed man, both modes)
    // (talk: the amount, | 0x10 Legends' lines, | 0x20 not WWE '13's; never 0x40 -- the AI voice's lines, dropped 2026-11-19)
    commentary_settings(comm_on, comm_volume, comm_talk | (comm_lines ? 0x10 : 0) | (comm_lines == 2 ? 0x20 : 0) | (comm_splice ? 0 : 0x80));
    commentary_frame();
    story_frame(sp_custom && phase == PLAY && (challenge_on || resume_pending));
    { extern void arena_raw_anim(void); arena_raw_anim(); }   // (RAW IS WAR's fist screens: arena.c)
    // (a story of ours: "gameover", no retries left -- the engine's phase 10. Arcade: GAME OVER, the attract. Regular
    // (W12, 2026-11-07): back to the title the way RESET TO TITLE goes (regular_route: the match left, the menus started
    // over on the title), where the story runner lets the story go (story_frame) -- as a game over is in the game.)
    if (story_active() && RD16(0x80097ED6u) == 10) {
        if (nm_arcade()) { if (phase == PLAY) start_game_over(); }
        else if (!reg_goal) reg_goal = 2;
    }
    // a story's match in an arena of ours (storyc.py: the node's venue the base, the arena in the pack's list): chosen
    // while the menus lead up to it; one of ours chosen so, and the next node's none, put back
    {   extern int story_xarena(void); extern int arena_picked(void);
        static int by_story;
        if (story_active() && in_menus()) {
            int a = story_xarena();
            if (a >= 10) { if (arena_picked() != a) arena_choose(a); by_story = 1; }
            else if (by_story) { arena_choose(-1); by_story = 0; }
        } else if (!story_active()) by_story = 0;
    }
    number_page_frame(0);                 // (W10b: the Rumble's NUMBER page counting by one, both modes)
    qa_press_start_second();              // (QA: the title's PRESS START, counted a second at a time)
    announce_table_move();
    { extern void announce_table_guard(void); announce_table_guard(); }   // (up on the table: kept on it)
    announce_watch();
    arcctl_frame();
    draw_begin();
    RD32(FLIGHT) = 0x4152434Cu;   // 'ARCL'
    // (where the packs are: a QA run's exe makes them read-only -- in a Royal Rumble, 2026-10-27, something wrote 3.9 MB
    // of zeros from the start of the arcade's memory on, its numbers with it: the next write there is a crash report
    // naming what did it)
    plat_guard(FLIGHT + 0xF00u);
    RD32(FLIGHT + 0x04) = frame_no;  RD32(FLIGHT + 0x08) = (u32)phase;   RD32(FLIGHT + 0x0C) = (u32)rr_mode;
    RD32(FLIGHT + 0x10) = (u32)rr_player_out; RD32(FLIGHT + 0x14) = (u32)cont_player; RD32(FLIGHT + 0x18) = (u32)goal;
    RD32(FLIGHT + 0x1C) = RD32(NM_MODE); RD32(FLIGHT + 0x20) = (u32)RDS16(NM_SCREEN); RD32(FLIGHT + 0x24) = (u32)game_mode;
    RD32(FLIGHT + 0x28) = (u32)sp_kind; RD32(FLIGHT + 0x2C) = (u32)credits; RD32(FLIGHT + 0x30) = (u32)op_open;
    RD32(FLIGHT + 0x34) = (u32)rr_walk_in; RD32(FLIGHT + 0x38) = (u32)match_lost[0] | (u32)match_lost[1] << 8;
    RD32(FLIGHT + 0x3C) = (u32)lost_pending[0] | (u32)lost_pending[1] << 8; RD32(FLIGHT + 0x40) = (u32)active[0] | (u32)active[1] << 8;
    flight_more();
    { extern void arena_frame(void); arena_frame(); }   // (Revenge's arenas in the base arena's place: arena.c)
    { extern void arena_page_frame(int menus_on, int on_page);   // (any ARENA page past ten rows, both modes -- W6: arena.c)
      arena_page_frame(title_loaded(), in_menus() && RDS16(NM_SCREEN) == SCR_ARENA && !menus_loading() && scr_stable > 1); }
    qa_state();   // (every frame, QA scenario or not: the exe writes it out only for a QA run, WCW_QA_STATE)
    if (dbg_passive) {
        if (dbg_sel_action) {
            dbg_sel_result = dbg_sel_action < 0 ? menu_confirm(dbg_sel_screen, dbg_sel_item) : menu_choose(dbg_sel_screen, dbg_sel_item, dbg_sel_action);
            dbg_sel_action = 0;
        }
        draw_flush(task); return;
    }
    // The seam between the two modes (2026-11-07): everything above is both modes' (settings, commentary, the story
    // runner, the announce table, ARCADE controls' frame, the arenas, QA's state); Regular has its own short frame
    // (regular_frame: the rest of what's universal, F2) and never reaches the coin-op below.
    if (!nm_arcade()) { regular_frame(task, dt); return; }
    n_says = 0;
    unlock_everything();
    tabs_without_belt();
    champ_survival_defs();
    champ_custom_defs();
    versus_links();                       // (champ_versus_defs only as the overlay loads: its rows' number is the build's)
    exh_ladder_slot();
    exh_lumberjack_slot();                                     // (lumberjack2: Arcade's MATCH page's row, under Ladder Match)
    if (custom_open_pending) {
        custom_open_pending = 0;
        if (in_menus() && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP && !menus_loading()) list_open();
    }
    custom_frame();
    if (sv_open_pending) {
        sv_open_pending = 0;
        if (in_menus() && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP && !menus_loading()) survival_open();
    }
    if (vs_open_pending) {
        vs_open_pending = 0;
        if (in_menus() && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP && !menus_loading() && game_mode == GM_STORY && goal == G_NONE) vs_open();
    }
    if (vs_back_pending) {
        vs_back_pending = 0;
        if (in_menus() && RDS16(NM_SCREEN) == SCR_EXHIBITION && !menus_loading() && vs_solo() && goal == G_NONE) vs_back();
    }
    if (ladder_open_pending) {
        ladder_open_pending = 0;
        if (in_menus() && RDS16(NM_SCREEN) == SCR_EXHIBITION && !menus_loading() && versus_menus()) {
            int next = menu_choose(SCR_MULTIPLAY, MULTIPLAY_LADDER, ACT_LADDER);
            if (next == SCR_EXHIBITION) {          // (the same page: set up again, as Ladder Match's)
                plat_game_call(GF_EXH_INIT, 0, 0, 0);
                RD8(menu_record(SCR_EXHIBITION) + 5) = 0;
            }
            if (next >= 0 && !dbg_keep_voice) RDS32(NM_MENU_SOUND) = -1;
        }
    }
    if (versus_menus()) exh_rows_fix();
    header_versus();
    players_page();
    if (in_menus() && is_select(RDS16(NM_SCREEN)) && !menus_loading())   // (costume variants: variants.c)
        variant_select_frame(menu_record(RDS16(NM_SCREEN)));
    champ_rumble_slot();
    if (rr_open_pending) {
        rr_open_pending = 0;
        if (in_menus() && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP && !menus_loading()) rr_open(0);
    }
    if (rr_mode == RR_SELECT && in_menus() && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP && !menus_loading() && scr_stable > 30) rr_mode = RR_OFF;
    // (a game-menu Rumble -- W10 -- has a match card and loading screens on its way: those don't let it go)
    if ((rr_mode == RR_START || rr_mode == RR_LIVE) && in_menus() && RDS16(NM_SCREEN) != SCR_MATCH &&
        !(gm_live && (RDS16(NM_SCREEN) == SCR_VS || RDS16(NM_SCREEN) == SCR_TRANS_A || RDS16(NM_SCREEN) == SCR_TRANS_B))) rr_mode = RR_OFF;
    // The Rumble's first two make their entrances before the bell, as the real one has it. One set
    // up again (a challenge's winner, the CPU Superstars still in the ring) is a match already on:
    // its entrances are skipped the game's own way (Start during the first stage shot).
    if (rr_mode == RR_START && rr_rebuild && RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == SCR_MATCH &&
        RDS32(NM_ENTRANCE_STEP) == 5 && inject_frames == 0) inject_press(BTN_START);

    if (dbg_sel_action) {
        dbg_sel_result = dbg_sel_action < 0 ? menu_confirm(dbg_sel_screen, dbg_sel_item) : menu_choose(dbg_sel_screen, dbg_sel_item, dbg_sel_action);
        dbg_sel_action = 0;
    }
    if (qa_on == 1) { qa_frame(); draw_flush(task); return; }   // a QA singles match: no coins, menus or attract loop
    if (qa_on == 2) { qa_rumble(); qa_frame(); }               // a QA Royal Rumble: the arcade's own way, driven
    if (!qa_on) qa_story_frame();                              // (QA: a story of ours, driven)
    if (!qa_on) qa_versus_frame();                             // (QA: one player's Versus, driven)

    int coin = 0;
    for (int i = 0; i < 2; i++) {
        int c = key_edge(i ? SC_6 : SC_5) || pad_coin_latch[i];
        pad_coin_latch[i] = 0;
        if (c && !op_open) { if (credits < 9) credits++; coin = 1; }
    }
    if (coin) {
        flight_event(FL_COIN, (u32)credits, 0);
        credit_flash = 180;
        if (coin_sound > 0) sfx_pending = coin_sfx[coin_sound];   // (played from the controller hook)
    }
    int kstart1 = key_edge(SC_1), kstart2 = key_edge(SC_2);
    if (operator_menu_frame(task)) return;     // (F2: open, it is the whole frame)

    u16 pr[2] = { take_pressed(0), take_pressed(1) };
    int start[2] = { kstart1 || (pr[0] & BTN_START), kstart2 || (pr[1] & BTN_START) };
    // (QA, qa_p2: player 2's Start that many frames into a game -- a scripted second controller's doesn't reach the
    // game's controller 2; qa_p2_after_op: counted only once qa_op is done)
    { static int qa_p2_frames; int q = active[0] ? 1 : 0; if (phase == PLAY && qa_p2 > 0 && !active[q] && (!qa_p2_after_op || dbg_op_done) && ++qa_p2_frames == qa_p2) start[q] = 1; }   // (the other player)
    if (qv_p2_start) { qv_p2_start = 0; if (!active[1]) start[1] = 1; }   // (QA qa_versus_p2: the challenger)

    // the intro movie plays in full, at boot and in attract (Start skips it, as in the game)
    intro_service();
    if (on_title() || intro_running()) booted = 1;
    splash_attract_service();                 // (W9: SPLASH SCREENS / ATTRACT)

    screen_watch(dt);                         // (menu screen bookkeeping)

    switch (phase) {
    case ATTRACT: {
        demo_label_off();
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
                game_mode = GM_STORY;
                match_armed = 0;
                set_goal(G_STORY, 0);
                set_phase(PLAY);
                break;
            }
        }
        if (phase != ATTRACT) break;
        // INSERT COIN flashing, the way a coin-op asks for one; PRESS START once there is a credit.
        // One line in the game's lettering, the same on the intro, the title and the demo matches.
        if (can_start()) say_text("PRESS START", GT_BIG | GT_GLOW, 240, 178, 0xFFFFFFFFu);
        else if (coin_lit()) say_text("INSERT COIN", GT_BIG | GT_GLOW, 240, 178, 0xFFFFFFFFu);
        draw_credits(28);
        break;
    }
    case PLAY: {
        // a player joins: coin + Start on their controller (or key 1 / 2)
        for (int pl = 0; pl < 2; pl++)
            if (!active[pl] && start[pl] && cont_player != pl && pay()) {
                active[pl] = 1;
                if (game_mode == GM_STORY) {
                    // HERE COMES A NEW CHALLENGER! at once, whatever the story is doing: the bell
                    // in a match or an entrance, a deep hit elsewhere
                    challenger_frames = 150;
                    challenge_on = 1; challenge_played = 0; challenger = pl; resume_pending = 0;
                    challenge_old = RDS32(NM_MODE) == -2 || RDS16(NM_SCREEN) == SCR_MATCH;
                    if (sp_kind == SP_RUMBLE && match_running()) rumble_capture();
                    sfx_pending = (RDS32(NM_MODE) == -2 || RDS16(NM_SCREEN) == SCR_MATCH) ? SFX_BELL : SFX_IMPACT;
                }
            }
        // Start with a credit (not free play) during the player's match: his power meter fills up,
        // as on an arcade machine (one credit; the controller's Start doesn't pause then: the pad hook)
        for (int pl = 0; pl < 2; pl++)
            if (start[pl] && power_start_ok(pl) && pay()) power_fill(pl);
        watch_match();
        menu_idle(dt);                       // (W11: left alone at a menu -> back to the attract)
        // (... then nothing more of a game this frame: the routing below would take the game back to Story Mode's list
        //  -- no players, game mode NONE -> STORY -- over the title the game over asked for; 2026-11-07)
        if (phase != PLAY) break;
        // a challenge match over with neither player beaten: the one challenged keeps their place
        if (challenge_old && in_menus() && RDS16(NM_SCREEN) != SCR_MATCH) challenge_old = 0;   // (that match left)
        if (challenge_on && challenge_played && !match_running() && in_menus()) challenge_decided(challenger);
        route(dt);
        resume_single();
        // back on the championship list with nothing to pick up: single player starts over
        if (!resume_pending && game_mode == GM_STORY && in_menus() && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP) {
            sp_kind = SP_NONE;
            if (!custom_launch && !custom_open_pending) sp_custom = 0;
            if (goal == G_NONE && !vs_open_pending) sp_versus = 0;     // (one player's Versus left: on the list again)
            if (vs_cursor_pending && goal == G_NONE && !menus_loading()) {
                vs_cursor_pending = 0;
                if (champ_versus_on()) RD8(menu_record(SCR_CHAMPIONSHIP) + 5) = CHAMP_VERSUS_ROW;
            }
        }

        // where the players are going: one player Story Mode, two Versus
        if (cont_player < 0 && !lost_pending[0] && !lost_pending[1]) {
            int want = (active[0] && active[1]) ? GM_VERSUS : GM_STORY;
            if (want != game_mode) {
                game_mode = want;
                // a challenger stops the story match after the announcement; a player dropping out
                // of Versus ends the match there (one player's Versus: back to its menus, where single player was --
                // the challenge's winner carries on there)
                set_goal(want == GM_VERSUS || sp_versus ? G_VERSUS : G_STORY, want == GM_VERSUS ? 150 : 0);
                if (want == GM_VERSUS) leave_fast = 1;
            } else if (short_menus && goal == G_NONE && in_menus() && scr_stable > 2 && is_main_page(RDS16(NM_SCREEN)) &&   // (off: the main menu is theirs)
                       !(game_mode == GM_STORY && RDS16(NM_SCREEN) == SCR_CHAMPIONSHIP)) {   // (Story starts there)
                // the game went back to its main menu or title (a story ended, results: Quit, ...; one player's
                // Versus: its menus, as two players')
                set_goal(versus_menus() ? G_VERSUS : G_STORY, 0);
            }
        }
        // Story Mode: the chapter card is skipped, the rules (fixed in the story) go by at once,
        // the match card after a moment (A moves it on sooner) -- not one player's Versus: its pages are Versus's
        if (short_menus && game_mode == GM_STORY && !sp_versus && goal == G_NONE && cont_player < 0 && in_menus()) {   // (W9: ABBREVIATED MENUS)
            s16 scr = RDS16(NM_SCREEN);
            s32 step = RDS32(NM_SCENE_STEP);
            if (scr != SCR_STORY_CARD) card_confirmed = 0;
            if (scr == SCR_STORY_CARD && !dbg_no_card_skip) {
                story_card_skip(scr, step);   // (the chapter card skipped: story_card_skip, shared with Regular's)
            } else if (scr == SCR_MATCH_RESULTS && sp_kind == SP_RUMBLE && !menus_loading() && scr_stable > 2) {
                // after the Royal Rumble, its Multi Play results table: its Next, at once (to the list)
                // (QA: what the game's Next gave, and how many times -- the table that wouldn't go, 2026-11-07)
                { extern u32 dbg_rr_next; int nx = menu_choose_action(scr, ACT_RESULTS_NEXT);
                  dbg_rr_next = ((u32)nx & 0xFFFFu) | (((dbg_rr_next >> 16) + 1u) << 16); }
                scr_stable = 0;
            } else if (scr == SCR_ARENA && sv_setup && !menus_loading() && scr_stable > 2) {
                menu_confirm(scr, -1);           // Survival's ARENA: the one the page has
                scr_stable = 0;
            } else if (!menus_loading() && ((scr == SCR_RULES && scr_stable > dbg_rules_wait && !dbg_no_rules_skip) || (scr == SCR_VS && scr_stable > 300))) {
                menu_confirm(scr, -1);
                scr_stable = 0;
            }
        }

        if (challenger_frames > 0 || (goal == G_VERSUS && leave_fast && !vs_solo())) {   // (one player's Versus' QUIT MATCH: no challenger)
            if (challenger_frames > 0) challenger_frames -= dt;
            draw_gtext("HERE COMES A", GT_GLOW, 240, 84, 0xFFE040FFu);
            draw_gtext("NEW CHALLENGER!", GT_BIG | GT_GLOW, 240, 100, 0xFF4030FFu);
        }
        continue_screen(pr, start);
        if (phase != PLAY) break;
        // the other player's invitation to join (INSERT COIN flashing), the credits after a coin
        if (cont_player < 0 && challenger_frames <= 0 && goal == G_NONE && phase == PLAY) {
            for (int pl = 0; pl < 2; pl++) {
                if (active[pl] || (!can_start() && !coin_lit())) continue;
                const char* w = can_start() ? (pl == 0 ? "1P PRESS START" : "2P PRESS START")
                                            : (pl == 0 ? "1P INSERT COIN" : "2P INSERT COIN");
                draw_gtext(w, GT_GLOW, pl == 0 ? 72 : 408, 4, pl == 0 ? 0xFF6050FFu : 0x70D8FFFFu);
            }
            if (credit_flash > 0) draw_credits(28);
        }
        break;
    }
    case GAMEOVER: {
        route(dt);
        if (!on_title() && !quiet_over) draw_gtext("GAME OVER", GT_BIG | GT_GLOW, 240, 96, 0xFF4030FFu);
        draw_credits(28);
        if (goal == G_NONE && on_title()) { quiet_over = 0; set_phase(ATTRACT); }
        if (phase_frames > 60 * 60) { goal = G_NONE; quiet_over = 0; set_phase(ATTRACT); } // safety
        break;
    }
    }

    // ARCADE controls: the arrow over whoever each player is on
    if (arc_controls && phase == PLAY && match_running() && !op_open && cont_player < 0)
        for (int port = 0; port < 2; port++) {
            int pl = port_player[port];
            if (active[pl]) arcctl_draw(port, pl == 0 ? 0xFF5040u : 0x60D0FFu);
        }
    drive_title();
    draw_flush(task);
}
