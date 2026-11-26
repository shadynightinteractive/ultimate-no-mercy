// lumberjack.c -- the Lumberjack match: the competitors fight in the ring, lumberjacks stand round it on the floor, and a
// man who leaves the ring (or is thrown out) is set on by them and put back in. The lumberjacks are never in the match: a
// pin or a submission in the ring decides it, by the match type's own rules.
//
// New feature (Ultimate No Mercy) -- lumberjack, 2026-11-21; round 2 (lumberjack2, 2026-11-21: the user's answers to the
// design's nine questions). The design and the rules followed: docs\MATCH_SLOTS.md section 4 ("Lumberjack").
//
// Built on the per-slot match model (slots.c), not on the Royal Rumble: the lumberjacks are extra slots after the match
// type's competitors (match_slots_add_at, rule ringside, job MS_JOB_LUMBERJACK), each a real match slot with his own match
// record, model, computer player and collision. They are never a side of the match (+0xE9 0x10 clear), so the type's own
// decision never counts one of them: never pinned, never the winner or the loser. Three match types take them: the
// one-on-one (2 competitors), the triple threat (3) and the tag match played tornado -- all four legal at once (4).
//   lumberjack_on_records  the request, each time the menus build the match records (a Lumberjack match chosen);
//   lumberjack_setup       the match set up: each lumberjack's post is where slots.c placed him round the ring; the
//                          lumberjacks' intro set up (they wait out of sight);
//   lumberjack_lead_in     every step of the match loop, the lead-in too: the intro (the bell held for it);
//   lumberjack_step        after every step of the match: who is out, who goes for him, the roll back in, the brawls;
//   lumberjack_cpu         after the computer player has chosen a slot's buttons: a lumberjack's job, a computer-played
//                          competitor's way back in, the QA probes.
//
// THE PLAYER'S MAN (lumberjack4, 2026-11-21 -- the user played build 13 and called it unplayable: "I can't attack my
// opponent outside the ring. I can't attack anymore"): this code never writes a state, a target, buttons or a plan for a
// competitor a controller drives (NM_SLOT_HUMAN). Build 13 did -- his target was set to the nearest lumberjack while they
// were on him (his strikes went to them), and the roll back in put him in the game's walk to the ring (126) and re-sent it
// every 2 s, a state that takes no attack. Now the lumberjacks set on him; the roller grabs him, carries him to the apron
// in the hold and throws him in -- the game's own computer-player plan for it, set on the roller's own record (the user:
// "The CPU typically will grapple the player, walk (while in grapple) towards the ring, and then roll them in"). If the hold ends on the floor
// all the same, they stand off a few seconds and he goes back in himself. A computer-played competitor gets the same
// throw; failing it, his own computer player's buttons climb him in (the game's climb). Once back in the ring nothing of
// his is touched any more.
#include "arcade.h"
#include "slots.h"
#include "gamefn.h"

// (lumberjack5) ovl_e compute_body_part_radii(wrestler): his body parts' radii from his model's bounds -- the strike and grab
// tests read them (0x80188950 + 0x70 a slot). The game runs it in state 0 and the Rumble's walk-in (163); for a lumberjack
// it ran before his model was there, and his radii stayed 0.
GAMEFN(void, func_80120560, 0x80120560u, (u32 wrestler));

void removal_s(const char* s); void removal_num(s32 v); void removal_say(void); void removal_who(u32 id);   // (the game's log: roster.c)
// (logic.c: the lumberjacks from the select's pages -- at random, wrestlers only -- or the player's own picks)
int arcade_draw_lumberjacks(u16* ids, u8* looks, int n, const u16* not_these, int n_not);
int arcade_picked_lumberjacks(u16* ids, u8* looks, int from, int n);
int arcade_not_a_wrestler(u16 id);
u32 variant_base_of(u16 id);

#define LJ_RECORD(s)    (SIXA_80159990 + 0x358u * (u32)(s))   // the wrestlers' records (offsets as slots.c's)
#define LJ_STRUCK_BY    0x00u    // s32: the slot of the man whose strike last landed on him (apply_strike_to_target)
#define LJ_TARGET       0x08u    // s32: the slot he is after
#define LJ_RADII        0x80188950u   // f32 x 28 a slot (0x70): the body parts' radii the strike / grab tests read
#define LJ_TEAM         0x0Cu    // s32: his side
#define LJ_STATE        0x14u    // s32: what he is doing
#define LJ_PREV         0x1Cu    // s32: the state before it
#define LJ_AREA         0x82u    // u16: & 0x7FF -- 1 the ring, 2 the apron, 4 the floor outside
#define LJ_RESULT       0x94u    // u16: & 1 out of the match
#define LJ_ID           0xA0u    // s16: the Superstar (-1 nobody)
#define LJ_STAMINA      0xAAu    // s16
#define LJ_STAMINA_MAX  0xACu    // s16: his full stamina (apply_move_damage takes damage / 4 off it)
#define LJ_SPIRIT       0xAEu    // s16: 0-100 (100: SPECIAL)
#define LJ_PUNISHED     0xBEu    // s16: punishment_count (add_to_stamina; his time down)
#define LJ_HURT_PART    0xE7u    // s8: the body part the hurt walk uses
#define LJ_BODY         0x2C4u   // f32 x 5: the body parts' health (50 whole)
#define LJ_ROLE         0xE9u    // u8: & 0x10 a side of the match (the decision's)
#define LJ_X            0xFCu
#define LJ_Y            0x100u
#define LJ_Z            0x104u
#define LJ_FACING       0x10Cu   // s32 0..4095: he faces (sin f, cos f)
#define LJ_LAST_X       0x120u   // the last steps' positions (12 bytes each, to +0x15C): a teleport writes them too
#define LJ_LAST_END     0x15Cu
#define LJ_AI_CURRENT   0x8015A6F0u   // the computer player's record while it decides a slot (+0x0C held, +0x0E pressed)
#define LJ_AI_STICK_X   0x12u         // ... s8: its analog stick, x (read_slot_controls hands it on; past 0x28 either way: a taunt)
#define LJ_AI_STICK_Y   0x13u         // ... s8: y
#define LJ_MATCH_STATE  0x801591FCu   // s32: the match loop's state (func_80114514): 0 set-up, 1 the lead-in, 2 the match
#define LJ_LEAD_IN      0x801591E0u   // s32: the lead-in's count: 30 down to -1, then the bell (0x8016B598 | 1)
#define LJ_VENUE        0x80159628u   // s32: the area in play (0-9 a ring venue)
#define LJ_MATCH_RECS   0x80097FE0u   // 0x114 a slot: the match records; +0xBC the packed abilities
#define LJ_RULES        0x800A9950u   // the live rules: 0x40 a tag match's four all in the ring (tornado)
#define LJ_RULE_ROWS    0x800AA044u   // u8[12]: the RULES page's choices; row 10, a tag match's: 5 tornado

// the wrestler states this job uses (names.json)
#define ST_STANDING     1
#define ST_WALKING      2
#define ST_RUNNING      3
#define LJ_GRAB_FLAGS   0x78u    // s32: the grabber's grab flags (pick_grab_outcome; pick_front_grapple_action reads them)
#define LJ_GRAB_BEHIND  0x80u    // ... & 0x80: the grab was from behind (the man faced away)
#define ST_TAUNT        27       // taunting (wrestler_state_27: the taunt from the moveset)
#define ST_GRAPPLER     81       // holding a man in the front grapple
#define ST_GRAPPLED     82       // held in it (the other man of the pair: his +0x08)
#define ST_CLIMB_IN     111      // climbing in (from the floor: onto the apron, then over the ropes with C-up held)
#define ST_OVER_TOP     112      // going over the top rope to the floor (wrestler_state_112_over_top_rope)
#define ST_SLIDE_IN     155      // under the bottom rope, in (file 0x2E4D) -- the game's own, from a run: a man in it is on
                                 //  his way in by himself (never set here since lumberjack4)
#define ST_WALK_IN      126      // wrestler_state_126_walk_to_ring_to_climb (likewise only recognised, never set)

// the buttons (the computer player's, as a controller's; the D-pad is a world direction: up +z, right +x -- gameplay.c)
#define B_A             0x8000u
#define B_B             0x4000u  // (a strike: the QA player's)
#define B_CUP           0x0008u  // C-up: through the ropes / climb in
#define B_CDOWN         0x0004u  // C-down: run
#define B_CRIGHT        0x0001u  // C-right: pick another man
#define B_R             0x0010u  // R: alone, in a grapple, the game's break-off (count_down_grapple_tie_up); the guard

#define LJ_SECOND       30                // steps a second
// WHEN they roll a man back in (lumberjack4, the user: "we do need them to not fight the match participants indefinitely,
// they should know to throw them in more often (but after getting some attacks in)"): a short beating first -- this many
// of their hits landed on him out there, or this long out, whichever comes first -- then the roll-in; and when an attempt
// fails (no grip, or the hold broken) they try again soon, without another beating. The HOW is the game's own (the
// computer player's plan for it: LJ_PLAN_THROW).
#define LJ_BEAT_HITS    3                 // their hits landed on him outside before they roll him in
#define LJ_BEAT_TIME    (4 * LJ_SECOND)   // ... or his time out of the ring, whichever first (well inside the referee's count)
#define LJ_RETRY        10                // a failed attempt -- no grip, the hold broken, the grapple gone into another move:
                                          //  the next one starts this soon (a third of a second: at once, without a new beating)
#define LJ_ROLLERS      1                 // lumberjacks who go for the grab at once (pick_rollers keeps the nearest on it). One:
                                          //  with two, both got hold of him and the game made it its own double-team move
                                          //  (131 / 133) instead of the walk to the ring (b31)
#define LJ_GRAB_MAX     (4 * LJ_SECOND)   // the roller's time to get hold of him, close to him and him up (then another attempt,
                                          //  LJ_RETRY later)
#define LJ_LATE         (13 * LJ_SECOND)  // a computer-played man out this long: sent in at once, whatever the roller is at (the
                                          //  count must never reach him: b3's lumberjack_match was won by count-out)
#define LJ_WALK_MAX     (8 * LJ_SECOND)   // the roller's way to his man (him up), at most (then another attempt)
#define LJ_TRY_MAX      (12 * LJ_SECOND)  // an attempt to get hold of him, at most in all (him down included)
#define LJ_HOLD         10                // steps the hold shows before the roller's move ends it
#define LJ_HOLD_MAX     (8 * LJ_SECOND)   // the roll's hold, at most (then on as if it had ended)
#define LJ_ROLL_NEAR    150.0f            // the roller farther than this from his man: our walk brings him over (then his own)
#define LJ_DOWN_WAIT    95.0f             // (ljtwitch2) the roller by his man lying waits once this near (the man's body keeps him
#define LJ_DOWN_WALK    125.0f            //  60-100 off) and walks up again only once he is this far
#define LJ_GRAB_NEAR    70.0f             // the roller this near his man (him on his feet): the game's grapple plan is his
#define LJ_PLAN_GRAB    1                 // the game's own computer-player plan for grabbing his man: ai_plan_1_grapple (holds A
                                          //  -- the CPU's held buttons -- and answers the man's own grab or strike as it does)
#define LJ_PLAN_THROW   103               // the game's own computer-player plan for it (the user, 2026-11-21: "The CPU typically
                                          //  will grapple the player, walk (while in grapple) towards the ring, and then roll
                                          //  them in"): ai_plan_103_whip_with_stick -- in a grapple outside it turns to the
                                          //  ring's middle, walks the man in the grapple, lines up and presses C-up with the
                                          //  side's stick; the game's throw (pair 15 / 16) lands him in the ring. It decides
                                          //  where, as for its own computer players: no distance of ours.
#define LJ_GRIP_MAX     (4 * LJ_SECOND)   // ANY lumberjack holding a man this long ends it with his own grapple move (A):
                                          //  lumberjack4 -- in b13's triple threat two competitors were held for good by
                                          //  lumberjacks and one was carried off through the walls in the hold
#define LJ_SEND_MAX     (12 * LJ_SECOND)  // a computer-played man sent back in: his buttons are ours this long at most
#define LJ_NEAR         140.0f            // a swarming lumberjack this close is "on him"
#define LJ_REACH        55.0f             // close enough for the roller's grapple
#define LJ_AT_POST      28.0f             // close enough to his post
#define LJ_HOME_LEAVE   80.0f             // ... and once there, pushed this far off it before he walks back (lumberjack3)
#define LJ_ROPES        200.0f            // the ring's half size where wrestlers stop (gameplay.c RR_EDGE)
#define LJ_APRON        310.0f            // the apron's edge
#define LJ_SIDE_ALONG   180.0f            // a man sent back in climbs in along a side within this of its middle (clear of the
                                          //  ring posts and the steps at the corners)
#define LJ_RING_BOX     325.0f            // the ring and its apron, as a walk round them sees it (from close to the ring)
#define LJ_STEPS_BOX    395.0f            // ... and with the ring steps at its corners, from out at the posts' square
#define LJ_POST_H       400.0f            // the posts: a square this far out from the middle, clear of the ring steps (b5's walk-
                                          //  out: at 360 the posts by the corners were walked to over the steps and never reached)
#define LJ_CORNER       470.0f            // the walk round the ring back to a post goes by this square, out past the posts (its
                                          //  corners past the ring steps)
#define LJ_BRAWL_WIN    (10 * LJ_SECOND)  // the brawls' window: each window a 10% chance of one
#define LJ_BRAWL_FIGHT  (10 * LJ_SECOND)  // a brawl lasts this long once the two have met
#define LJ_BRAWL_MAX    (20 * LJ_SECOND)  // ... and this long at most
// the intro (lumberjack5, the user, b33: "The lumberjack entrances is too long. Can we show a side angle of the ramp (not the
// stage entrance) where we seem them walking down the ramp in one shot? One after the other."): one camera at the side of
// the ramp, the lumberjacks walking down it one after another through the shot; each one past the shot waits out of
// sight; then all of them are put at their posts as the camera cuts back to the game's, and the bell. (lumberjack4's
// one-by-one close shots at the stage took 17.5 s for twelve.)
#define LJ_RAMP_FOOT    530.0f            // the aisle's mouth by the ring (arena_ringstage's XRS_MOUTH_Z): the ramp runs up from here
#define LJ_RAMP_MID     420.0f            // the middle of the stretch of ramp the shot shows: this far up from its foot (the
                                          //  posts by the ring stay out of the frame)
#define LJ_RAMP_HALF    300.0f            // the stretch, each way from its middle: a man sets off just above it (out of the
                                          //  frame) and is gone once past its bottom end
#define LJ_RAMP_CAM_X   330.0f            // the camera: this far to the side of the ramp (x),
#define LJ_RAMP_CAM_UP  300.0f            //  this high above it -- above No Mercy's crowd signs by the aisle (b2 at 150 and b4 at 200:
                                          //  a sign filled the shot on SmackDown, then RAW; b3 tried 440 / 420 out: signs again),
#define LJ_RAMP_CAM_IN  110.0f            //  a little toward the ring of the middle (they walk toward it), looking at the ramp's
#define LJ_RAMP_LOOK_UP 75.0f             //  middle this high (their chests)
#define LJ_RAMP_LOOK_IN 20.0f             //  and this far up the ramp of it
#define LJ_RAMP_GAP     90.0f             // the next one sets off once the last one is this far down the ramp from the start
#define LJ_RAMP_CLOSE   70.0f             // a walker this close behind the man ahead stands a step (men walk at their own pace)
#define LJ_RAMP_RESUME  80.0f             // ... and walks on once the man ahead is this much further off (ljtwitch: a wait of near a
                                          //  second, not a stutter -- at 30 the stands were a third of a second, b-t5's lumberjack_p1)
#define LJ_RAMP_FIRST   15                // steps of the shot before the first one is in it (the game's fade into the match)
#define LJ_RAMP_MAX     (14 * LJ_SECOND)  // the intro at most (then every one to his post, wherever he is)
#define LJ_MODEL_SIZE   0x804B1A20u       // s16 a model slot: its size (slot_model_scale: 0.97 + it x 0.00047; superstar_size)
#define LJ_LANE         36.0f             // (ljround8) the intro's two files: this far either side of the aisle's middle line
#define LJ_PACE_MAX     7.0f              // (ljround8) the fastest walk seen down the ramp (round 7 / 8's traces: 5.2, 5.9, 6.7 a step)
#define LJ_PARK_Z       6000.0f           // where the lumberjacks wait, out of sight, before their turn and after it

enum { LJ_POST, LJ_SWARM, LJ_EXIT, LJ_ROLL, LJ_BRAWL, LJ_PARKED, LJ_INTRO, LJ_GONE };   // (LJ_GONE: walked through the intro's shot)
// a lumberjack: his job (mode), his man (prey), turned to the ring once home (faced), home at his post (home), steps in his
// job (t), steps holding a man (grip), when he met his brawl's rival (met), the slot he watches at his post (across: the
// lumberjack across the ring, who stands still -- lumberjack4), his post (px, pz), and the QA stillness measure's last step
typedef struct { u8 mode, prey, faced, home, last_st, across; u16 t, met, grip; s16 last_df; s32 last_f; float last_x, last_z; float px, pz;
                 // (lumberjack5, QA trace qa_lj_trace: what his computer player chose before our buttons, ours, the step it ran;
                 //  what the trace saw of him last -- state, animation file, facing, place)
                 u16 ai_h, ai_p, our_h, our_p; u32 cpu_t; s32 tr_st, tr_anim, tr_f, tr_tg; float tr_x, tr_z;
                 u8 iwalk, said_ai;   // (the intro: walking down the ramp this step, or standing a step)
                 u16 wr, last_dpad;   // (ljtwitch, QA: what this code wrote to him this step -- LJW_* bits; our last D-pad)
                 // (ljtwitch: our walk pressed into something -- the steps pressed, the place they began, the steps he now
                 //  stands before trying again; the computer player's plan when it chose his buttons, for the trace)
                 u8 bt, bwait; float bx, bz; s32 ai_plan;
                 // (ljtwitch: his walk goes straight in to his post, not by the walkway -- the walkway was blocked; the
                 //  corner of the walk round the ring he last reached, +1 -- 0 none; his walk home blocked so many times
                 //  running; the roller near his man -- his own computer player closes; the roller going round to the
                 //  man's front)
                 u8 direct, corner, blocks, rnear, rside, rwait;   // (rwait: the roller waiting by his man lying, ljtwitch2)
                 // (ljrole, ROUND 7: the role -- the job and man last decided for (seen: a new one is decided at once), the plan
                 //  last given his computer player, his walk leg (on, start place and step, steps), his pace a step, the roller on
                 //  the far side of his man from the ring)
                 u16 seen; s32 given; u8 leg_on, outer; u16 leg_n; u32 leg_t; float leg_x, leg_z, pace;
                 // (ljround8: the hold he has a competitor in -- the competitor + 1, and what was decided for it: 1 the throw
                 //  in, 2 a grapple move; his hold from behind -- the competitor + 1; the intro: his stops, standing now,
                 //  walked yet, his model size and his place the last step)
                 u8 hold_k, hdec, rear_k, rear_in, istops, istand, iwalked, mv, hd, stk, sk_up, sk_k, idle_run, ostand; s16 sk_sta; s16 isize; float iz, iwd; u16 iws; } Lumberjack;
// (ljtwitch, QA) this code's writes to a man, for the per-step trace (qa_lj_trace & 4): a bit each this step
#define LJW_TGT_CPU   0x001u   // his target changed before his state's step (lumberjack_cpu_job)
#define LJW_TGT_STEP  0x002u   // his target changed after it (lumberjack_step)
#define LJW_FACING    0x004u   // his facing set (the turn to the ring on arrival)
#define LJW_STICK     0x008u   // his computer player's stick zeroed (it was not)
#define LJW_PLAN      0x010u   // a plan given to his computer player
#define LJW_COMP_TGT  0x020u   // (a competitor) his target changed by this code
#define LJW_STATE     0x040u   // a state set
#define LJW_PLACE     0x080u   // placed (moved) by this code
#define LJW_BUTTONS   0x100u   // his computer player's buttons replaced by others
#define LJW_DPAD_FLIP 0x200u   // our D-pad this step points another way than last step's (both held)
static Lumberjack lj[NM_SLOTS_CAP];
// a competitor: his time out, the swarm's time on him, the roll back in (0 none, 1 the roller getting hold of him, 2 the
// hold and the throw, 3 -- a computer-played man only, the last resort -- on his way back in by his own computer player's
// buttons), the lumberjacks standing off him before another attempt (rest), their hits landed on him out there (hits),
// beaten already (ready: the next attempt needs no beating), escaping by himself, the QA probe
typedef struct { u16 out, beat, rt, rest, walk, tries_t; u8 was_out, roll, roller, roller2, coming, esc, probe, threw, said, hits, ready, behind; u16 probe_t; s16 stamina;
                 u8 sside, smid, tdec; } Competitor;   // (ljtwitch: sent back in -- the side he goes in by, 1 x along it / 2 z; in its
                                                       //  middle stretch. ROUND 9 tdec: this roll's throw decided, kept through the turn
                                                       //  from behind and the grab again: cleared once the roll is over)
static Competitor lc[4];

int lj_count = 12;               // F2 > LUMBERJACK MATCH > LUMBERJACKS (logic.c keeps it with the settings)
int lj_pick;                     // ... PICKED: 0 at random, 1 the player picks them on the Superstar Select
int lj_variant;                  // ... MATCH: 0 one on one, 1 triple threat, 2 tornado tag
// (ljround8, the user 2026-11-23: "How do I select lumberjacks?" -- the menus' own choices for this match (logic.c: Multi
//  Play > Lumberjack Match, its MATCH page's LUMBERJACKS and PICKED rows and the type), over F2's for this request only;
//  -1 F2's (the defaults). Cleared with the request.)
int lj_m_count = -1, lj_m_pick = -1, lj_m_variant = -1;
static int eff_count(void) { return lj_m_count >= 2 ? lj_m_count : lj_count; }
static int eff_pick(void) { return lj_m_pick >= 0 ? lj_m_pick : lj_pick; }
void lumberjack_menu_set(int count, int pick, int variant) { lj_m_count = count; lj_m_pick = pick; lj_m_variant = variant; }
int lj_live;                     // this match is a Lumberjack match, set up
int qa_lj_out;                   // QA: every this many seconds (from 15 s) the last competitor walks out of the ring
int qa_lj_esc;                   // QA: every other walk-out he climbs back in by himself 3 s later (the lumberjacks let him)
int qa_lj_brawl = -1;            // QA: the brawls' chance a window, percent (-1: the match's own, 10)
int qa_lj_p1;                    // QA (lumberjack4): player 1's controller pressed as a player would (lumberjack_qa_pad)
int qa_lj_trace;                 // QA (lumberjack5): the game's log traces 1 the lumberjacks at their posts, 2 player 1's strikes
int qa_lj_behind;                // QA (lumberjack5): the roller grabs his man FROM BEHIND (to prove the turn to the front)
int qa_lj_set;                   // QA (ljtwitch): 1 -- the twelve lumberjacks of the user's 2026-11-23 match, not a draw
// (the user's match, 2026-11-23, where he still saw the twitching: ten of the twelve imported men)
static const u16 lj_user_set[12] = { 0x0108, 0x03BC, 0x0370, 0x0382, 0x0609, 0x0603, 0x0902, 0x0384, 0x060C, 0x010B, 0x0102, 0x0202 };
int qa_lj_cam[4];                // QA (lumberjack5): the intro's camera x, height, toward the ring, the walkers' gap (0: the match's own)
float lumberjack_cam[7];         // the intro's camera: on, eye x y z, the point looked at x y z (announce.c qa_camera_mul)
static int lj_armed, lj_qa_n, lj_drawn, lj_n, lj_c, lj_kind;
static u16 lj_ids[NM_SLOTS_CAP], lj_pair[4];
static u8 lj_looks[NM_SLOTS_CAP], lj_engaged[NM_SLOTS_CAP];
static s16 lj_brawl_sta[NM_SLOTS_CAP];
typedef struct { s16 sta, max, spirit, punished; s8 hurt; u8 ok; u32 body[5]; s32 att; } LjHealth;   // (att: +0x00, the last striker)
static LjHealth lj_health[NM_SLOTS_CAP];   // (lumberjack3: each lumberjack after the last step -- no damage among them)
static u32 q_outs, q_thrown, q_sends, q_back, q_engaged, q_ring_steps, q_side, q_probes, q_hits, q_max_out, q_present;
static u32 q_grabbed, q_sent_in, q_rolled_in, q_esc, q_esc_back, q_esc_hits, q_esc_probes;
static u32 q_brawl_rolls, q_brawls, q_brawl_bad, q_brawl_hits, q_rival_pairs, q_drawn_bad, q_caption;
static u32 q_released, q_arrived, q_min_d, q_intro_steps, q_cam, q_held, q_intro_done, q_intro_on, q_walk_min;
// (lumberjack3: the hits among lumberjacks / what they would have done and was put back; the competitors' hits on them and
//  those that hurt; the lowest lumberjack spirit at the bell)
static u32 q_lj_hits, q_lj_undone, q_comp_hits, q_comp_dmg, q_spirit_bell, q_bell_seen;
static u32 q_post_steps, q_post_flips, q_face_rev, q_post_moves;   // (lumberjack3: at the post, still -- lj_l, lj_m)
static u32 q_post_busy, q_post_taunts;   // (lumberjack5: at the post, steps not standing; taunts begun)
static u32 q_post_pushed;                  // (ROUND 9: at the post, steps he was pushed a little by a man within 90 -- not his own moves)
// (lumberjack4: player 1's hits on his opponent in the ring, outside, in the ring after a roll-in, in the ring after he was
//  out and grabbed; the times the lumberjacks grabbed him; a competitor at ring height off the ring (steps), out of the
//  arena (steps); a watched man's target changed by the game (the twitch's cause); slot 0 driven by a controller seen)
static u32 q_p1_in, q_p1_out, q_p1_after_roll, q_float, q_away, q_tgt_taken, q_p1_human;
static u32 q_p1_lj_hits, tr_lines, tr_p1_last, lj_qa_pad_last;   // (lumberjack5: player 1's hits that hurt a lumberjack, kept; the trace's lines; the QA player's last buttons)
static u32 q_behind, q_behind_cup, q_turned, q_behind_lost, q_radii;   // (lumberjack5: roll-in holds from behind let go; steps a roller holding from behind had C-up -- never)
static u32 q_behind_cup_game;              // (ROUND 9: C-up from behind in a hold the game's own move was decided for -- its plan 112, the shoulder lift: the game's)
static u32 q_thrown, q_throws, q_p1_outs, q_p1_thrown, q_p1_long;   // (the roll-in throw: men thrown into the ring by it; the game's plan for it given to a roller)
// (ljtwitch, QA: the jitter measure -- every man each step, by what he is doing (JC_*): his turns that reverse within a
//  third of a second (any size), his animation file switching back to the one before within 2/3 s, the file restarted
//  before its end, his state switching back likewise, his step reversing his last one's way (any size). The user, 11-23:
//  "There's still some twitching" / "Its not just when they're at posts" / "Check when they're idle outside the ring,
//  thats where I notice most" -- the posts-only measure (above) passed while it went on)
enum { JC_IDLE_POST, JC_IDLE_AWAY, JC_WALK, JC_SWARM, JC_ROLL, JC_BRAWL, JC_EXIT, JC_BUSY, JC_INTRO, JC_COMP_OUT, JC_N };
enum { JE_STEPS, JE_TURN, JE_ANIM, JE_RESTART, JE_STATE, JE_POS, JE_N };
static u32 q_jit[JC_N][JE_N];
typedef struct { s32 f, anim, anim_prev, st, st_prev, fr, left; float x, z, mx, mz; s8 fsign; u8 ok; u16 fsince, anim_t, st_t, m_t; s16 lastdf; } Jit;
#define LJ_JIT_TURN     3                 // a turn reversed counts when both turns are this big at least (0.26 degrees; a unit
                                          //  either way as the standing file loops is not a twitch: scratch jit.py counts all)
static Jit lj_jit[NM_SLOTS_CAP];
static u32 tr_full;                        // (the per-step trace's lines this match)
static u32 q_blocked, q_second_walks;      // (ljtwitch: walks that pressed on the spot; the second's walk turned to the approach)
// (ljrole, QA: the role's steps, plans given, walk legs given, the game's attack chooser asked, its choices put aside (into the
//  ring / onto the apron / the ropes), steps a roller pressed R (the turn to the front -- the role's one button), throws
//  refused (his back to the ring), throw plans a non-roller's grapple chose that were put aside; the role's steps OUTSIDE a
//  Lumberjack match -- never: the "doesn't leak" proof, in the log at every Lumberjack match's end)
static u32 q_role_steps, q_plans, q_legs, q_chooser, q_vetoed, q_r_steps, q_throw_refused, q_throw_vetoed;
// (ljround8, QA: the user 2026-11-23 -- "They don't attack enough. They attack the player more than the CPU", "60% ...
//  throw them into the ring, ... 40% ... a grapple move", the escape loop, the intro's stops)
static u32 q_run_hits[2], q_run_eng[2];   // (ROUND 9, QA lj_y: competitors 0 and 1 over the whole run -- never reset: each run is a game of its own)
static u32 q_c_hits[4], q_c_out[4], q_c_eng[4];   // per competitor: the lumberjacks' hits on him out there, his steps out on
                                                  //  the floor, the lumberjack-steps on him (in the swarm or rolling him, within 140)
static s32 q_c_att[4]; static u16 q_c_quiet[4];   // (his +0x00 last step; steps since his last hit taken -- a hit is a drop
                                                  //  after 10 quiet steps)
static u32 q_hold_throw, q_hold_move;      // a lumberjack's hold of a competitor from the front: the throw in / a grapple move
static u32 q_rear_grabs, q_rear_in, q_loops;   // holds of a competitor from behind; of those, on a man heading back in; out
                                               //  spells with two or more of those ending in his escape (the user's loop)
static u32 q_rear_esc, q_rear_esc_in, q_rear_turned;   // (ljround8) holds from behind that ended with the man free, not turned
                                               //  to face the lumberjack (he got out: the elbow); of those, on a man heading in; turned by R
static u8 q_c_rear_in[4];                  // (this out spell's holds from behind on him heading in)
static float q_c_rd[4][16];                // (his distance out from the ring, the last 16 steps)
static u32 q_istops, q_istop_max, q_iwalkers;  // the intro: walkers' stops (standing again after walking), the most by one, walkers
#define LJ_STRIKE_NEAR  85.0f             // (ljround8) the role's attack from this near; farther, a walk leg up to him first
static u32 q_dec_throw, q_dec_move;        // (ljround8, QA: the holds' decisions -- the throw, a grapple move; ROUND 9: every hold, front or behind)
static u32 lj_dec_t, lj_dec_m;             // (ljround8: this match's hold decisions so far -- throws, grapple moves)
static u32 q_passive_vetoed;                // (ljround8: the game's passive choices turned to attacks)
static u32 q_mv_out, q_mv_apron, q_mv_far, q_mv_rear;   // (ROUND 9, QA lj_x: lumberjacks' grapple moves on a man out there; by the apron's edge; by the barricade / aisle; from behind)
// (ljround10, QA) what a lumberjack on a man (the swarm's, the roller's, within LJ_NEAR) spends his steps on -- the log's
// "on a man" line at a match's end, a share each: striking | grabbing (80) | holding (81) | in a move (85 / 93 / 95) | on his
// feet: on a walk leg (plan 60) | waiting (55) | a strike given (6, 22) not yet begun | a grab given | the game's other plans |
// down, hit, staggered (anything else); and on his feet within 85 of the man with no attack under way
enum { EC_STRIKE, EC_GRAB, EC_HOLD, EC_MOVE, EC_LEG, EC_WAIT, EC_STRIKE_GIVEN, EC_GRAB_GIVEN, EC_OTHER_PLAN, EC_DOWN, EC_N };
static u32 q_eng_cls[EC_N], q_eng_near_idle, q_eng_man_down, q_eng_man_held;
static u32 q_out_d[3], q_hold_d[3];
static u32 q_strikes, q_steps_last, q_vi_first, q_vi_last;
static u32 q_mate_way;
static u32 q_held_blow, q_struck_other;                    // (ljround10, QA: attacks held a moment -- the man reeling or in a move)
static u32 q_engage_reach, q_raised_whips, q_raised_holds, q_idle_steps, q_idle_spells, q_whipped_down;   // (ljround10, QA lj_z)
static u32 q_sk[3][2];                     // (ljround10, QA: his strikes on a man up | down (lying, rising) | else -- missed, landed)                     // (ljround10, QA: attacks put off for a fellow lumberjack in the way -- to his own side first)   // (ljround10, QA: strikes begun on a man out there; the match's steps and the game's frame count)        // (ljround10, QA: competitor-steps out / lumberjacks' holds begun, the man under 370 out | 370-430 | 430+)
static const char* const ec_name[EC_N] = { "striking", "grabbing", "holding", "in a move", "walk leg", "waiting", "strike given",
                                           "grab given", "the game's other plans", "down / hit" };
static u32 lj_role_outside;                // (never reset: role steps asked for outside a Lumberjack match -- refused, counted)
static int lj_role_said;
static u8 lj_float_run[4];
static s16 lj_p1_opp_sta;
static u32 lj_rand;
static int lj_intro, lj_intro_t, lj_hold_on, lj_brawl_t, lj_rule10 = -1;

static u32 rec(int s) { return LJ_RECORD(s); }
float ringstage_ground_at(float x, float z);   // (arena_ringstage.c: the floor's height at a place, by the arena's map)
// a competitor a controller drives: this code never writes his state, target, buttons or plan (lumberjack4)
static int human(int k) { return k >= 0 && k < 4 && RD8(NM_SLOT_HUMAN + (u32)k) != 0; }
// standing, walking or running: free -- in no move, no hold, not down. Only then is a man's target (+0x08) ever written
// here: in a hold or a move together it is the other man of the pair, and b13 broke pairs by writing it (lumberjack4)
static int free_state(s32 st) { return st == 1 || st == 2 || st == 3; }
static float fabs_f(float v) { return v < 0.0f ? -v : v; }
static float wx(int s) { return *(volatile float*)(rec(s) + LJ_X); }
static float wz(int s) { return *(volatile float*)(rec(s) + LJ_Z); }
static int area(int s) { return RD16(rec(s) + LJ_AREA) & 0x7FF; }
static int on_floor(int s) { return (area(s) & 4) != 0; }
// (ljround10) on the ramp or the stage: the ground under him raised (the arena's map: arena_ringstage.c), past the aisle's
// mouth -- where the roll-in is the game's whip toward the ring, not a walk all the way down with the man held
float ringstage_ground_at(float x, float z);
#define LJ_RAISED       15.0f
static int raised(int s) {
    // (r6's lumberjack_stage_raw: RAW's aisle is floor-high all the way to its stage -- the man 1410 up it on ground 0, and the
    //  throw in walked him back from there. Up the aisle past its mouth counts as much as raised ground)
    return ringstage_ground_at(wx(s), wz(s)) > LJ_RAISED || (wz(s) > LJ_RAMP_FOOT + 100.0f && fabs_f(wx(s)) < 400.0f);
}
// (ljround10, the user 2026-11-24: "Lumberjacks shouldn't be idle when a wrestler is out of the ring in striking distance
// ever") a lumberjack this near a competitor out there engages him: an attack or a grab -- whatever his job was
#define LJ_ENGAGE       100.0f
static int present(int s) { return RDS16(rec(s) + LJ_ID) != -1 && !(RD16(rec(s) + LJ_RESULT) & 1); }
static s32 state_of(int s) { return RDS32(rec(s) + LJ_STATE); }
static float dist2(int a, int b);
static float dist1(int a, int b) { return fabs_f(wx(a) - wx(b)) + fabs_f(wz(a) - wz(b)); }
// his distance out from the ring, as the apron sees it (the larger of |x|, |z|)
static float ring_d(int s) { float x = fabs_f(wx(s)), z = fabs_f(wz(s)); return x > z ? x : z; }
static u32 rnd(void) { lj_rand = lj_rand * 1103515245u + 12345u; return lj_rand >> 8; }   // (its own: the game's numbers stay the game's)
static float sqrt_f(float v) {                      // (Newton's: no library here)
    if (v <= 0.0f) return 0.0f;
    float r = v > 1.0f ? v : 1.0f;
    for (int i = 0; i < 20; i++) r = 0.5f * (r + v / r);
    return r;
}
static u16 dpad_to(float dx, float dz) {            // (gameplay.c's: the eight ways)
    float ax = fabs_f(dx), az = fabs_f(dz);
    u16 b = 0;
    if (az > ax * 0.414f) b |= dz > 0.0f ? 0x0800 : 0x0400;
    if (ax > az * 0.414f) b |= dx > 0.0f ? 0x0100 : 0x0200;
    return b;
}
static s32 facing_to(float dx, float dz);
// his facing off a way (0..2048)
static s32 off_way(int s, s32 way) { s32 d = ((RDS32(rec(s) + LJ_FACING) - way + 2048) & 4095) - 2048; return d < 0 ? -d : d; }
static s32 facing_to(float dx, float dz) {           // (slots.c's ms_facing: 0..4095 with (sin f, cos f) along (dx, dz))
    float ax = fabs_f(dx), az = fabs_f(dz);
    if (ax < 1e-3f && az < 1e-3f) return 0;
    float t = ax < az ? ax / az : az / ax;
    float a = t * (0.7853982f + 0.2730f * (1.0f - t));
    if (ax > az) a = 1.5707963f - a;
    if (dz < 0) a = 3.1415927f - a;
    if (dx < 0) a = -a;
    return (s32)(a * (4096.0f / 6.2831853f)) & 4095;
}
// the way out of the ring from where he is: (nx, nz) toward the nearest ropes, and how far in from them he is
static float way_out(float x, float z, float* nx, float* nz) {
    if (fabs_f(x) >= fabs_f(z)) { *nx = x >= 0.0f ? 1.0f : -1.0f; *nz = 0.0f; return LJ_ROPES - fabs_f(x); }
    *nx = 0.0f; *nz = z >= 0.0f ? 1.0f : -1.0f; return LJ_ROPES - fabs_f(z);
}
// a straight walk from (x, z) to (tx, tz) that would cross the ring or its apron -- and, from out at the posts' square,
// the ring steps at its corners (from close to the ring, at the apron: the ring and apron only)
// (ljtwitch: the steps are at the corners only -- the whole square out to LJ_STEPS_BOX counted as steps, so a man by the
//  apron in the middle of a side could not be walked to from the posts: the walk went to a corner, and at it the next
//  corner and back -- a roller turning left and right every step at the corner for seconds, the trace's roll-in)
#define LJ_STEPS_CORNER 290.0f            // the ring steps: where both |x| and |z| are past this (and inside LJ_STEPS_BOX) --
                                          //  (ljtwitch2: 250 took in the floor by the apron 50 along from each corner: a roller 60
                                          //  from his man lying there was sent round by the corner, away from him, and back)
#define LJ_STEPS_NEAR   160.0f            // a walk this short is never sent round for the steps (ljtwitch2: walked straight,
                                          //  the man is beside them; blocked, unblocked stands him)
// (ljtwitch2: the ring's box is drawn no further out than the walker himself and the point he walks to, less a little, and
//  never inside the apron's edge -- the box is 325, the apron 310, and a man on the floor hugging the apron stood INSIDE
//  it: from there every walk "crossed the ring", the walk went to a corner, a step out of the box it went straight again,
//  a step in it crossed again -- the D-pad turning between two ways every step or two. The merged build's
//  lumberjack_twitch, match 2: a roller by the apron after his man lying a few steps off, 15 jitter events a second)
#define LJ_BOX_MARGIN   8.0f
static int crosses_ring(float x, float z, float tx, float tz) {
    int out = !(fabs_f(x) < LJ_STEPS_BOX && fabs_f(z) < LJ_STEPS_BOX) && fabs_f(tx - x) + fabs_f(tz - z) > LJ_STEPS_NEAR;
    float own = fabs_f(x) > fabs_f(z) ? fabs_f(x) : fabs_f(z), to = fabs_f(tx) > fabs_f(tz) ? fabs_f(tx) : fabs_f(tz);
    float rb = LJ_RING_BOX;
    if (own - LJ_BOX_MARGIN < rb) rb = own - LJ_BOX_MARGIN;
    if (to - LJ_BOX_MARGIN < rb) rb = to - LJ_BOX_MARGIN;
    if (rb < LJ_APRON - 10.0f) rb = LJ_APRON - 10.0f;
    for (int k = 1; k < 8; k++) {
        float u = fabs_f(x + (tx - x) * (float)k / 8.0f), v = fabs_f(z + (tz - z) * (float)k / 8.0f);
        if (u < rb && v < rb) return 1;
        if (out && u < LJ_STEPS_BOX && v < LJ_STEPS_BOX && u > LJ_STEPS_CORNER && v > LJ_STEPS_CORNER) return 1;
    }
    return 0;
}
// the next point of a walk on the floor to (tx, tz): the target itself, or the corner of the square to go round by. A post
// is walked to along the walkway (LJ_CORNER out) and stepped in to from the walkway's point outside it, so a man on his
// way never passes close by another already standing at his post.
// (ljtwitch, the user 11-23: "Check when they're idle outside the ring, thats where I notice most". The step in from the
//  walkway's point to the post is 70 long, but a man counted as on it only within 30 of either end: a step or two in from
//  the walkway's point he was sent back out to it, then in again -- the D-pad turned round every step, the walk files
//  switching back and forth, a lumberjack stuck twitching by his post for ten seconds and more (the trace: 236 man-seconds
//  of 1287 in one match, 3460 walk-file switch-backs). Now the whole stretch from the walkway to the post and a little
//  either side of it is the step in: anywhere on it he walks straight to the post.)
#define LJ_LEG_SIDE     45.0f             // the step in to a post: this far either side of the line from the walkway's point,
#define LJ_LEG_IN       40.0f             //  and from this far on the ring's side of the post to as far past the walkway
static int on_post_leg(float x, float z, float tx, float tz) {
    float ax = fabs_f(tx), az = fabs_f(tz);
    if (ax >= az) {                                       // (a post on a side across x: the step in runs along x)
        if ((x > 0.0f) != (tx > 0.0f) || fabs_f(z - tz) > LJ_LEG_SIDE) return 0;
        return fabs_f(x) > LJ_POST_H - LJ_LEG_IN && fabs_f(x) < LJ_CORNER + LJ_LEG_IN;
    }
    if ((z > 0.0f) != (tz > 0.0f) || fabs_f(x - tx) > LJ_LEG_SIDE) return 0;
    return fabs_f(z) > LJ_POST_H - LJ_LEG_IN && fabs_f(z) < LJ_CORNER + LJ_LEG_IN;
}
static void next_point(int s, float x, float z, float tx, float tz, float* nx, float* nz) {
    *nx = tx; *nz = tz;
    float ax = fabs_f(tx), az = fabs_f(tz);
    Lumberjack* L = &lj[s];
    if (!L->direct && (ax > LJ_POST_H - 1.0f || az > LJ_POST_H - 1.0f) && fabs_f(x - tx) + fabs_f(z - tz) > 30.0f && !on_post_leg(x, z, tx, tz)) {
        float wxp = ax >= az ? (tx > 0.0f ? LJ_CORNER : -LJ_CORNER) : tx, wzp = ax >= az ? tz : (tz > 0.0f ? LJ_CORNER : -LJ_CORNER);
        // (not yet on the step in: to the walkway's point -- round by the corners if need be)
        tx = wxp; tz = wzp; *nx = tx; *nz = tz;
    }
    if (!crosses_ring(x, z, tx, tz)) return;
    float best = 1e9f; int keep = -1;
    for (int k = 0; k < 4; k++) {
        float qx = (k & 1) ? LJ_CORNER : -LJ_CORNER, qz = (k & 2) ? LJ_CORNER : -LJ_CORNER, dq = fabs_f(qx - x) + fabs_f(qz - z);
        if (dq < 40.0f) { L->corner = (u8)(k + 1); continue; }    // (the corner he is at: on to the next -- b5's walkers stood at it)
        // (ljtwitch: nor back to it while still near it -- a step off it, it was the nearest again: the walk turned back to
        //  it, and on, every step)
        if (crosses_ring(x, z, qx, qz)) continue;
        if (L->corner == k + 1 && dq < 160.0f) { keep = k; continue; }
        float d = fabs_f(qx - x) + fabs_f(qz - z) + fabs_f(tx - qx) + fabs_f(tz - qz);
        if (d < best) { best = d; *nx = qx; *nz = qz; }
    }
    if (best >= 1e9f && keep >= 0) { *nx = (keep & 1) ? LJ_CORNER : -LJ_CORNER; *nz = (keep & 2) ? LJ_CORNER : -LJ_CORNER; }   // (no other way)
}
static void place(int s, float x, float z, s32 facing) {
    u32 r = rec(s);
    *(volatile float*)(r + LJ_X) = x; *(volatile float*)(r + LJ_Y) = 0.0f; *(volatile float*)(r + LJ_Z) = z;
    for (u32 a = LJ_LAST_X; a + 8u <= LJ_LAST_END; a += 12u) { *(volatile float*)(r + a) = x; *(volatile float*)(r + a + 4u) = 0.0f; *(volatile float*)(r + a + 8u) = z; }
    RD16(r + LJ_AREA) = 4;
    RDS32(r + LJ_FACING) = facing;
    lj[s].wr |= LJW_PLACE;
}

// ---- rivals: the game's own -- each moveset's "Rival 1", "Rival 2", "Rival 3" (the Create-a-Wrestler ALLY/ENEMY page,
// ally_enemy_page_run func_801119AC; abilities 31-33, 12-bit Superstar IDs at bits 149, 161, 173 of the packed abilities)
// and "Accompanied by" (34, bits 185: whom the game walks out with him and sends in as his run-in). Read from the match
// records (record s +0xBC: the packed abilities, as the match unpacks them), so every slot's -- the lumberjacks' too.
static u32 tie(int s, int k) {
    u32 a = LJ_MATCH_RECS + 0x114u * (u32)s + 0xBCu, pos = 149u + 12u * (u32)k, v = 0;
    for (u32 i = 0; i < 12; i++, pos++) v = v << 1 | ((RD8(a + (pos >> 3)) >> (7 - (pos & 7))) & 1u);
    return v;
}
static u32 man_of(u32 id) { u32 b = variant_base_of((u16)id); return b ? b : id; }   // (a version of a man: the man)
// rivals (lumberjack3): roster-tags' shared rule, roster.c roster_rivals -- wrestlers\rivals.json (Revenge's real feuds,
// WrestleMania 2000's own Rival 1-3) for the imports, else No Mercy's own Rival 1-3 (either naming the other)
int roster_rivals(u16 a, u16 b);
static int rivals(int a, int b) { return roster_rivals((u16)RDS16(rec(a) + LJ_ID), (u16)RDS16(rec(b) + LJ_ID)); }

// ---- the request: a Lumberjack match chosen (the MATCH page's row, logic.c), or a QA match ---------------------------
void lumberjack_arm(int on) { lj_armed = on; lj_qa_n = 0; if (!on) { lj_drawn = 0; lj_m_count = lj_m_pick = lj_m_variant = -1; } }
void lumberjack_arm_qa(int n) { lj_armed = n > 0; lj_qa_n = n; lj_drawn = 0; }
int lumberjack_armed(void) { return lj_armed; }
int lumberjack_variant(void) { int v = lj_m_variant >= 0 ? lj_m_variant : lj_variant; return v < 0 || v > 2 ? 0 : v; }
// the competitors a Lumberjack match of this kind has: one on one 2, triple threat 3, tornado tag 4
int lumberjack_competitors(void) { static const int c[3] = { 2, 3, 4 }; return c[lumberjack_variant()]; }
// the picks the Superstar Select is to take (logic.c, while the select is up): the competitors, then -- the player
// picking them -- the lumberjacks; 0: the type's own
int lumberjack_select_picks(void) {
    if (!lj_armed || lj_qa_n || eff_pick() != 1) return 0;
    int n = eff_count() < 1 ? 1 : eff_count(), c = lumberjack_competitors();
    if (c + n > 48) n = 48 - c;          // (the select's 50 records)
    if (c + n > NM_SLOTS_CAP) n = NM_SLOTS_CAP - c;
    return c + n;
}
// The RULES page's tag row is the player's: a Lumberjack tornado tag match has it "tornado" while the match flags are
// made (func_80136784, logic.c's arcade_rr_finalize_pre / _post round it), then it is put back as the player left it.
void lumberjack_finalize_pre(void) {
    lj_rule10 = -1;
    if (!lj_armed || lj_qa_n || lumberjack_variant() != 2) return;
    lj_rule10 = RD8(LJ_RULE_ROWS + 10);
    RD8(LJ_RULE_ROWS + 10) = 5;
}
void lumberjack_finalize_post(void) {
    if (lj_rule10 >= 0) RD8(LJ_RULE_ROWS + 10) = (u8)lj_rule10;
    lj_rule10 = -1;
}
// The match card's caption (the game's text ids by the match's kind, story.c story_card_text): "Lumberjack Match" for
// Single Match, and the triple threat's and tornado tag's, while a Lumberjack match is asked for. 0: the game's own.
u32 lumberjack_caption(s32 id) {
    if (!lj_armed) return 0;
    int v = lj_qa_n ? (lj_c == 3 ? 1 : lj_c == 4 ? 2 : 0) : lumberjack_variant();
    if (id == 0xF0F0 && v == 0) { q_caption++; return (u32)"Lumberjack Match"; }
    if (id == 0xF0F2 && v == 1) { q_caption++; return (u32)"Lumberjack Triple Threat"; }
    if (id == 0xF0F1 && v == 2) { q_caption++; return (u32)"Lumberjack Tornado Tag"; }
    return 0;
}
// The match records have been built (slots.c's return hook on build_match_records; the menus build them as the select
// ends and again as they leave for the match): a match chosen as a Lumberjack match asks for its lumberjacks -- the same
// ones while the competitors are the same (a rematch keeps them), drawn (or taken from the picks) again for others. 1: asked.
int lumberjack_on_records(void) {
    if (!lj_armed) return 0;
    u32 f = RD32(NM_MATCH_FLAGS);
    // (not a Rumble 0x4000, cage 0x10000, handicap 0x20000, ladder 0x100000; a three-way 0x8000, a tag match 0x2000 -- tornado)
    if (f & 0x134000u) return 0;
    int c = (f & 0x8000u) ? 3 : (f & 0x2000u) ? 4 : 2;
    if (!lj_qa_n && c != lumberjack_competitors()) return 0;        // (another match than the one asked for)
    u16 comp[4];
    for (int k = 0; k < c; k++) { s32 id = RDS32(NM_SLOT_WRESTLER + 4u * (u32)k); if (id <= 0) return 0; comp[k] = (u16)id; }
    int same = lj_drawn && lj_c == c;
    for (int k = 0; k < c && same; k++) same = lj_pair[k] == comp[k];
    if (!same) {
        int n = lj_qa_n > 0 ? lj_qa_n : eff_count();
        if (n < 1) n = 1;
        if (n > NM_SLOTS_CAP - c) n = NM_SLOTS_CAP - c;
        lj_kind = (!lj_qa_n && eff_pick() == 1) ? 1 : 0;
        lj_n = lj_kind ? arcade_picked_lumberjacks(lj_ids, lj_looks, c, n) : arcade_draw_lumberjacks(lj_ids, lj_looks, n, comp, c);
        if (lj_qa_n && qa_lj_set == 1)                               // (QA, ljtwitch: the user's twelve)
            for (lj_n = 0; lj_n < n && lj_n < 12; lj_n++) { lj_ids[lj_n] = lj_user_set[lj_n]; lj_looks[lj_n] = 0; }
        // the front end walks slot k + 2's man out with competitor k when he is k's "Accompanied by" (func_80109A34): a
        // lumberjack in slot 2 or 3 is never that one (swapped with a later lumberjack)
        for (int s = c; s < 4 && s - c < lj_n; s++) {
            int k = s - 2;
            if (k < 0 || k >= c) continue;
            u32 partner = tie(k, 3);
            if (!partner || man_of(lj_ids[s - c]) != man_of(partner)) continue;
            for (int j = lj_n - 1; j > s - c; j--) {
                if (man_of(lj_ids[j]) == man_of(partner)) continue;
                u16 t = lj_ids[j]; lj_ids[j] = lj_ids[s - c]; lj_ids[s - c] = t;
                u8 l = lj_looks[j]; lj_looks[j] = lj_looks[s - c]; lj_looks[s - c] = l;
                break;
            }
        }
        for (int k = 0; k < 4; k++) lj_pair[k] = k < c ? comp[k] : 0;
        lj_c = c; lj_drawn = 1;
        q_drawn_bad = 0;
        for (int i = 0; i < lj_n; i++) if (arcade_not_a_wrestler(lj_ids[i])) q_drawn_bad++;
        removal_s("lumberjack: a Lumberjack match (match flags "); removal_num((s32)f); removal_s(", competitors "); removal_num(c);
        removal_s(lj_kind ? "): lumberjacks picked " : "): lumberjacks drawn "); removal_num(lj_n); removal_s(" of "); removal_num(n);
        removal_s(", not wrestlers among them "); removal_num((s32)q_drawn_bad); removal_say();
    }
    if (lj_n <= 0) return 0;
    if (c == 4) RD32(LJ_RULES) |= 0x40u;                            // (tornado: all four in the ring)
    match_slots_add_at(c, lj_n, lj_ids, lj_looks, MS_RULE_RINGSIDE, 0, MS_JOB_LUMBERJACK);
    return 1;
}

// ---- the intro (lumberjack5) ---------------------------------------------------------------------------------------
// The competitors' own entrances are the front end's, before the match; the lumberjacks are match slots, there from the
// match's set-up. So before the bell -- the lead-in held -- they walk down the ramp one after another through ONE shot from
// the side of the ramp: each sets off from just above the stretch the shot shows (out of the frame), walks down it by his
// own stick (x 0, the aisle's middle line, toward the ring), and once past its bottom end waits out of sight. The next one
// sets off when the last is LJ_RAMP_GAP down from the start, and a man who closes on the one ahead (men walk at their own
// pace: the moveset's walk) stands a step, so nobody walks into anybody. When the last one is half-way down the shot, the
// camera cuts back to the game's, every lumberjack is at his post, and the bell rings. (lumberjack4 showed them one by one
// at the stage, 17.5 s for twelve: "too long", the user, b33.)
// The stretch shown: from the ramp's foot (by the ring) up LJ_RAMP_MID -- every arena's ramp runs up the aisle from there
// (z +), its height the arena's map (ringstage_ground_at: RAW's stands at ring height, others at the floor's).
static float ramp_mid_z(void) { return LJ_RAMP_FOOT + LJ_RAMP_MID; }
static float ramp_top_z(void) { return ramp_mid_z() + LJ_RAMP_HALF + 60.0f; }   // (where each sets off: out of the frame)
static float ramp_end_z(void) { return ramp_mid_z() - LJ_RAMP_HALF - 40.0f; }   // (past it: out of the frame)
// (ljrole) the intro's ANCHOR: one lumberjack waiting out of sight stands, hidden, at the ramp's foot (z LJ_ANCHOR_Z: below
// every walker, 70 below where a walker is hidden, and up the aisle from its mouth, 530); the walker with nobody ahead of
// him has him for his man. A man walking by his stick faces his man: so he faces down the ramp; and the fight-on-stage
// steering (arena_ringstage.c ringstage_ai, run inside the computer player's step) walks -- and, lined up, RUNS -- a man on
// the ramp whose man is by the ring (r1: the first walkers' man was player 1: they ran down, 1.6 jitter events a second;
// r2: a waiting man up behind the stage -- walked down backwards, facing him); a man up the aisle it leaves alone. So the
// walk is the computer player's own plan (71, the run-in's walk down the aisle), at a walk, facing the ring. (The hide
// point moved 20 up the ramp from round 5's for the anchor's room: still past the shot's bottom edge.)
#define LJ_ANCHOR_Z     540.0f
static int lj_anchor = -1;
// (placed there once. His own man: one already through the shot, parked off the map for good, or -- before anyone is --
//  himself: the stage steering then leaves him standing. r5: his man a lumberjack still to set off, who then walked down the
//  ramp: the hidden anchor walked UP toward him and stood in the walkers' way, a jam of stops and starts, 3 events a second)
static void anchor_put(int a, int c, int n) {
    place(a, 0.0f, LJ_ANCHOR_Z, 2048);
    int t = a;
    for (int o = c; o < n; o++) if (o != a && match_slots_is_extra(o) && present(o) && lj[o].mode == LJ_GONE) t = o;
    RDS32(rec(a) + LJ_TARGET) = t;
}
// the shot: from the side (x +), a little toward the ring, above the ramp, looking at the stretch's middle
static void ramp_camera(void) {
    float zm = ramp_mid_z(), y = ringstage_ground_at(0.0f, zm);
    float cx = qa_lj_cam[0] ? (float)qa_lj_cam[0] : LJ_RAMP_CAM_X, cu = qa_lj_cam[1] ? (float)qa_lj_cam[1] : LJ_RAMP_CAM_UP;
    float ci = qa_lj_cam[2] ? (float)qa_lj_cam[2] : LJ_RAMP_CAM_IN;   // (QA: qa_lj_camx / camy / camin try other places)
    lumberjack_cam[1] = cx;   lumberjack_cam[2] = y + cu; lumberjack_cam[3] = zm - ci;
    lumberjack_cam[4] = 0.0f; lumberjack_cam[5] = y + LJ_RAMP_LOOK_UP; lumberjack_cam[6] = zm + LJ_RAMP_LOOK_IN;
    lumberjack_cam[0] = 1.0f;
    q_cam++;
}
// a lumberjack to his post: there at once, facing the ring, shown, home (the intro's end of him, off camera)
static void to_post(int s) {
    Lumberjack* L = &lj[s];
    place(s, L->px, L->pz, facing_to(-L->px, -L->pz));
    plat_game_call(GF_PLAYER_SHOW, (u32)s, 1, 0);
    L->mode = LJ_POST; L->home = 1; L->faced = 1; L->t = 0;
    q_arrived++;
}
// out of sight, each in a place of his own, far behind the stage (before his turn, and after it)
static void park(int s) {
    place(s, -1500.0f + 100.0f * (float)s, ramp_top_z() + LJ_PARK_Z, 2048);
    plat_game_call(GF_PLAYER_SHOW, (u32)s, 0, 0);
}
static void intro_setup(int c, int n) {
    lj_intro = 0; lj_intro_t = 0; lj_hold_on = 0; lumberjack_cam[0] = 0.0f;
    s32 venue = RDS32(LJ_VENUE);
    if (venue < 0 || venue > 9 || RDS32(LJ_MATCH_STATE) != 0) return;   // (a ring venue, the match not yet under way)
    lj_anchor = -1;
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s)) continue;
        lj[s].mode = LJ_PARKED; lj[s].t = 0; lj[s].iwalk = 0;
        park(s);
        lj_anchor = s;                                               // (the last to be released: the anchor till then)
    }
    if (lj_anchor >= 0) anchor_put(lj_anchor, c, n);                // (hidden, standing at the ramp's foot)
    lj_intro = 1; q_intro_on = 1;
}
// the intro's end: the game's camera back, every one at his post, full spirit at the bell
static void intro_end(int c, int n) {
    lumberjack_cam[0] = 0.0f;
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s)) continue;
        if (lj[s].mode == LJ_PARKED || lj[s].mode == LJ_INTRO || lj[s].mode == LJ_GONE) to_post(s);
        RDS16(rec(s) + LJ_SPIRIT) = 100;
    }
    // (ljtwitch: each one already facing the man he watches, his target that man -- the intro left the walkers' target on
    //  player 1, so at the bell every lumberjack turned 30 degrees one way and at once back: the trace, two steps each)
    // (ljrole: a one-time set-up, as each is put at his post out of sight of the camera -- not a steering: from here on his
    //  computer player's plan (the role's watch) keeps him there, and the game turns him to the man he watches)
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s) || lj[s].mode != LJ_POST) continue;
        int a = lj[s].across;
        if (a == s || a >= n || !match_slots_is_extra(a) || !present(a)) continue;
        RDS32(rec(s) + LJ_TARGET) = a;
        RDS32(rec(s) + LJ_FACING) = facing_to(lj[a].px - lj[s].px, lj[a].pz - lj[s].pz);
    }
    for (int a = c; a < n; a++) {                                    // (the closest two at their posts: QA)
        if (!match_slots_is_extra(a) || !present(a)) continue;
        for (int b = a + 1; b < n; b++) {
            if (!match_slots_is_extra(b) || !present(b)) continue;
            float dx = wx(a) - wx(b), dz = wz(a) - wz(b);
            u32 d = (u32)sqrt_f(dx * dx + dz * dz);
            if (!q_min_d || d < q_min_d) q_min_d = d ? d : 1;
        }
    }
    if (!q_walk_min) q_walk_min = 0xFFFFu;                          // (never two walking at once: nobody near anybody)
    lj_intro = 0; q_intro_done = 1;
    removal_s("lumberjack: the intro done in "); removal_num(lj_intro_t); removal_s(" steps, down the ramp "); removal_num((s32)q_released);
    removal_s(", at their posts "); removal_num((s32)q_arrived); removal_s(", the closest two on the ramp "); removal_num((s32)q_walk_min);
    removal_s(", at their posts "); removal_num((s32)q_min_d); removal_say();
}
// every step of the match loop (slots.c: before its own, which waits for the match proper): the intro, the bell held
static void lead_in_body(void) {
    int n = match_slots_count(), c = match_slots_competitors();
    s32 ms = RDS32(LJ_MATCH_STATE);
    if (ms == 0) return;
    if (ms != 1) { lj_intro = 0; lumberjack_cam[0] = 0.0f; return; }   // (the match is on: no more intro)
    if (!lj_hold_on && RDS32(LJ_LEAD_IN) > 27) return;             // (the game's own first steps of it: 30 and 28 do things)
    lj_hold_on = 1;
    lj_intro_t++; q_intro_steps = (u32)lj_intro_t;
    ramp_camera();
    if (lj_intro_t > LJ_RAMP_MAX) { intro_end(c, n); return; }
    RDS32(LJ_LEAD_IN) = 27; q_held++;                               // (the bell waits)
    if (lj_intro_t <= LJ_RAMP_FIRST) return;
    // the walkers: past the shot -> out of sight; the newest (the highest up the ramp) found for the next start
    float end = ramp_end_z();
    int left = 0, newest = -1;
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s)) continue;
        Lumberjack* L = &lj[s];
        if (L->mode == LJ_PARKED) left++;
        if (L->mode != LJ_INTRO) continue;
        L->t++;
        // (ljrole: his target is player 1's slot from his release (below) -- a man standing a step faces down the ramp, to the
        //  ring; his walk is his computer player's plan 71, the run-in's walk down the aisle -- lumberjack_role)
        if (wz(s) < end) {
            // (ljround8, QA: the log -- each walker's model size, pace and stops)
            removal_s("lumberjack: the intro walker "); removal_num(s); removal_s(" "); removal_who((u16)RDS16(rec(s) + LJ_ID));
            removal_s(" model size "); removal_num(RDS16(LJ_MODEL_SIZE + 2u * (u32)s)); removal_s(", pace x10 "); removal_num(L->t ? (s32)(10.0f * (ramp_top_z() - wz(s)) / (float)L->t) : 0); removal_s(", walking pace x10 "); removal_num(L->iws ? (s32)(10.0f * L->iwd / (float)L->iws) : 0);
            removal_s(", stops "); removal_num(L->istops); removal_say();
            L->mode = LJ_GONE; L->iwalk = 0; park(s); continue;
        }
        if (newest < 0 || L->t < lj[newest].t) newest = s;
    }
    // each walker's gap to the man ahead of him (the nearest walker below him): closer than LJ_RAMP_CLOSE, he stands this
    // step; and the closest two walkers (QA: never into each other)
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s) || lj[s].mode != LJ_INTRO) continue;
        float ahead = 1e9f; int ah = -1;
        for (int o = c; o < n; o++) {
            if (o == s || !match_slots_is_extra(o) || !present(o) || lj[o].mode != LJ_INTRO) continue;
            float dz = wz(s) - wz(o), dx = wx(s) - wx(o), d = sqrt_f(dx * dx + dz * dz);
            if (dz > 0.0f && d < ahead && fabs_f(dx) < LJ_LANE) { ahead = d; ah = o; }   // (ljround8: the man ahead in his own file)
            if (o > s && (!q_walk_min || (u32)d < q_walk_min)) q_walk_min = (u32)d ? (u32)d : 1u;
        }
        // (ljtwitch: on again only once the man ahead is LJ_RAMP_RESUME further off -- with one threshold the walker caught
        //  up, stood a step, walked a step: the trace's intro, a walk and a stand every 8 steps, 61 switch-backs)
        lj[s].iwalk = lj[s].iwalk ? ahead > LJ_RAMP_CLOSE : ahead > LJ_RAMP_CLOSE + LJ_RAMP_RESUME;
        {   // (ljround8, QA: the user, "they seem to pause in the aisle, and the CPU behind them has to stop behind them too" --
            // a stop: standing again (under 2 a step) once he has walked)
            float z = wz(s);
            if (lj[s].iz - z > 2.0f) { if (!lj[s].iwalked) { lj[s].iwalked = 1; q_iwalkers++; } lj[s].istand = 0; if (lj[s].iz - z < 20.0f) { lj[s].iwd += lj[s].iz - z; lj[s].iws++; } }
            else if (lj[s].iwalked && !lj[s].istand) { lj[s].istand = 1; if (lj[s].istops < 255) lj[s].istops++; q_istops++; if (lj[s].istops > q_istop_max) q_istop_max = lj[s].istops; }
            lj[s].iz = z;
        }
        // (ljrole: his man is the intro's anchor (LJ_ANCHOR_Z) -- set when that changes: a walker faces him, down the ramp,
        //  and the fight-on-stage steering leaves him to his own plan)
        //  (r3: the walker ahead for his man, 90 off, swayed his facing a few units each way with every stride -- 1.5 jitter
        //  events a second; the anchor, far below and standing still, does not)
        {   int want = lj_anchor >= 0 && lj_anchor != s ? lj_anchor : ah >= 0 ? ah : 0;
            if (free_state(state_of(s)) && RDS32(rec(s) + LJ_TARGET) != want) { RDS32(rec(s) + LJ_TARGET) = want; lj[s].wr |= LJW_TGT_STEP; } }
    }
    // the next one sets off when the newest is LJ_RAMP_GAP down from the start (or none is walking)
    // (ljround8, the user: "they seem to pause in the aisle, and the CPU behind them has to stop behind them too, then they
    //  continue. They should all just walk" -- men walk at their own pace (round 7's trace: 6.0 and 6.7 a step; round 8's:
    //  5.2, 5.9, 6.7), so a faster man set off 90 behind a slower one closed on him and stood (LJ_RAMP_CLOSE, then 80 more
    //  before walking on), and the men behind him in turn. The stand-a-step stays only as the guard; the stops are counted.)
    //  (r3: set off biggest first -- the model's size is not the pace (Big Show 5.2 a step, Chyna 6.0, D-Lo Brown 6.7): the
    //  stops stayed. So: two files, LJ_LANE either side of the middle line, the men set off into them in turn -- one after
    //  another down the ramp, LJ_RAMP_GAP apart as before -- and a man sets off only once the man ahead IN HIS FILE is far
    //  enough down that not even the fastest walk could close on him before he is out of the shot: his own pace measured
    //  (5 a step until he has walked 4 steps), the gap needed LJ_RAMP_CLOSE + 10 + (LJ_PACE_MAX - his pace) x his steps
    //  left. A faster man in the other file passes a slower one 72 to the side; nobody stands.)
    int lane = (int)(q_released & 1);
    float lane_x = lane ? LJ_LANE : -LJ_LANE;
    int prev = -1;                                          // (the newest man walking in that file)
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s) || lj[s].mode != LJ_INTRO || fabs_f(wx(s) - lane_x) > LJ_LANE) continue;
        if (prev < 0 || lj[s].t < lj[prev].t) prev = s;
    }
    int file_clear = 1;
    if (prev >= 0) {
        float v = lj[prev].iws >= 4 ? lj[prev].iwd / (float)lj[prev].iws : 5.0f, left_z = wz(prev) - ramp_end_z();
        if (v < 1.0f) v = 1.0f;
        float need = LJ_RAMP_CLOSE + 10.0f + (LJ_PACE_MAX > v ? (LJ_PACE_MAX - v) * (left_z > 0.0f ? left_z : 0.0f) / v : 0.0f);
        file_clear = ramp_top_z() - wz(prev) >= need;
    }
    int next = -1;
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s) || lj[s].mode != LJ_PARKED || (s == lj_anchor && left > 1)) continue;
        next = s; break;
    }
    if (left && next >= 0 && file_clear && (newest < 0 || ramp_top_z() - wz(newest) >= (qa_lj_cam[3] ? (float)qa_lj_cam[3] : LJ_RAMP_GAP))) {
        for (int s = next; s < n; s++) {
            if (!match_slots_is_extra(s) || !present(s) || lj[s].mode != LJ_PARKED) continue;
            if (s == lj_anchor) {                                    // (the anchor sets off: the last one gone takes his place)
                lj_anchor = -1;
                for (int o = c; o < n; o++) if (o != s && match_slots_is_extra(o) && present(o) && lj[o].mode == LJ_GONE) lj_anchor = o;
                if (lj_anchor >= 0) anchor_put(lj_anchor, c, n);
            }
            place(s, lane_x, ramp_top_z(), 2048);                    // (facing down the ramp: -z; ljround8: in his file)
            plat_game_call(GF_PLAYER_SHOW, (u32)s, 1, 0);
            plat_game_call(GF_SET_STATE, rec(s), ST_STANDING, 0); lj[s].wr |= LJW_STATE;
            RDS32(rec(s) + LJ_TARGET) = 0;
            lj[s].mode = LJ_INTRO; lj[s].t = 0; lj[s].iwalk = 1; q_released++;
            newest = s; left--;
            break;
        }
    }
    // all set off, and the last one half-way down the shot: the cut, the posts, the bell
    if (!left && (newest < 0 || wz(newest) < ramp_mid_z())) intro_end(c, n);
}
static void qa_jitter(int c, int n, int intro_t);
void lumberjack_lead_in(void) {
    if (!lj_live || !lj_intro) return;
    lead_in_body();
    if (lj_intro) qa_jitter(match_slots_competitors(), match_slots_count(), lj_intro_t);   // (QA: the intro's walk too, ljtwitch)
}

// ---- the match ------------------------------------------------------------------------------------------------------
void lumberjack_setup(void) {
    int n = match_slots_count(), c = match_slots_competitors();
    for (int s = 0; s < NM_SLOTS_CAP; s++) {
        lj[s].mode = LJ_POST; lj[s].prey = 0; lj[s].faced = 0; lj[s].t = 0; lj[s].met = 0; lj[s].grip = 0; lj[s].across = (u8)s; lj_engaged[s] = 0; lj[s].said_ai = 0;
        lj[s].px = s < n ? wx(s) : 0.0f; lj[s].pz = s < n ? wz(s) : 0.0f;
        lj_brawl_sta[s] = s < n ? RDS16(rec(s) + LJ_STAMINA) : 0;
    }
    // the posts: evenly round the square LJ_POST_H out (slots.c's are 360 out: by the corners that is on the ring steps), facing in
    {   int m = 0, i = 0;
        for (int s = c; s < n; s++) if (match_slots_is_extra(s) && present(s)) m++;
        for (int s = c; s < n && m > 0; s++) {
            if (!match_slots_is_extra(s) || !present(s)) continue;
            const float h = LJ_POST_H, u = (8.0f * h) * ((float)i++ + 0.5f) / (float)m;
            float x, z;
            if (u < 2 * h) { x = -h + u; z = -h; } else if (u < 4 * h) { x = h; z = -h + (u - 2 * h); }
            else if (u < 6 * h) { x = h - (u - 4 * h); z = h; } else { x = -h; z = h - (u - 6 * h); }
            lj[s].px = x; lj[s].pz = z;
            place(s, x, z, facing_to(-x, -z));
            RDS16(rec(s) + LJ_SPIRIT) = 100;                         // (lumberjack3, the user: full spirit from the start)
        }
    }
    // the man each one watches at his post: the lumberjack whose post is nearest the point across the ring from his own
    // (lumberjack4: the stand-still fix -- the game turns a standing man to his target (+0x08) every step, exactly, and with
    // a competitor there he followed every shuffle of the fight in the ring: b13 measured 0.8 reversed turns a second at
    // each post, the "shaking". The man across stands still, so the watcher does too, facing the ring. With no one across,
    // the nearest competitor, as before.)
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s)) continue;
        int best = -1; float bd = 1e9f;
        for (int o = c; o < n; o++) {
            if (o == s || !match_slots_is_extra(o) || !present(o)) continue;
            float d = fabs_f(lj[o].px + lj[s].px) + fabs_f(lj[o].pz + lj[s].pz);
            if (d < bd) { bd = d; best = o; }
        }
        lj[s].across = (u8)(best >= 0 ? best : s);
    }
    for (int k = 0; k < 4; k++) {
        Competitor* m = &lc[k];
        m->out = m->beat = m->rt = m->rest = m->walk = m->tries_t = 0; m->roller2 = 0xFF; m->was_out = m->roll = m->roller = m->coming = m->esc = m->probe = m->threw = m->hits = m->ready = 0; m->probe_t = 0;
        m->stamina = k < c ? RDS16(rec(k) + LJ_STAMINA) : 0;
    }
    q_outs = q_thrown = q_sends = q_back = q_engaged = q_ring_steps = q_side = q_probes = q_hits = q_max_out = q_present = 0;
    q_grabbed = q_sent_in = q_rolled_in = q_esc = q_esc_back = q_esc_hits = q_esc_probes = 0;
    q_brawl_rolls = q_brawls = q_brawl_bad = q_brawl_hits = q_rival_pairs = 0;
    q_released = q_arrived = q_min_d = q_intro_steps = q_cam = q_held = q_intro_done = q_intro_on = q_walk_min = 0;
    q_lj_hits = q_lj_undone = q_comp_hits = q_comp_dmg = 0; q_spirit_bell = 0xFF; q_bell_seen = 0;
    q_post_steps = q_post_flips = q_face_rev = q_post_moves = q_post_busy = q_post_taunts = q_post_pushed = 0;
    q_p1_in = q_p1_out = q_p1_after_roll = q_float = q_away = q_tgt_taken = q_p1_human = 0;
    q_thrown = q_throws = q_p1_outs = q_p1_thrown = q_p1_long = 0;
    q_p1_lj_hits = 0; tr_lines = 0; tr_p1_last = 0; q_behind = q_behind_cup = q_turned = q_behind_lost = q_radii = 0; q_behind_cup_game = 0;
    lj_p1_opp_sta = -1;
    for (int i = 0; i < JC_N; i++) for (int e = 0; e < JE_N; e++) q_jit[i][e] = 0;
    for (int s = 0; s < NM_SLOTS_CAP; s++) { lj_jit[s].ok = 0; lj[s].wr = 0; lj[s].last_dpad = 0; lj[s].bt = lj[s].bwait = 0; lj[s].direct = lj[s].corner = lj[s].blocks = lj[s].rnear = lj[s].rside = lj[s].rwait = 0; }
    q_blocked = q_second_walks = 0;
    q_role_steps = q_plans = q_legs = q_chooser = q_vetoed = q_r_steps = q_throw_refused = q_throw_vetoed = 0; lj_role_said = 0;
    for (int k = 0; k < 4; k++) { q_c_hits[k] = q_c_out[k] = q_c_eng[k] = 0; q_c_att[k] = k < c ? RDS32(rec(k) + LJ_STRUCK_BY) : -1; q_c_quiet[k] = 100; q_c_rear_in[k] = 0;
                                  for (int i = 0; i < 16; i++) q_c_rd[k][i] = 0.0f; }
    q_hold_throw = q_hold_move = q_rear_grabs = q_rear_in = q_loops = q_rear_esc = q_rear_esc_in = q_rear_turned = 0; q_istops = q_istop_max = q_iwalkers = 0; q_passive_vetoed = 0; q_mv_out = q_mv_apron = q_mv_far = q_mv_rear = 0;
    for (int i = 0; i < EC_N; i++) q_eng_cls[i] = 0;
    q_eng_near_idle = q_eng_man_down = q_eng_man_held = 0;
    for (int i = 0; i < 3; i++) q_out_d[i] = q_hold_d[i] = 0;
    q_strikes = 0; q_mate_way = 0; q_held_blow = 0; q_struck_other = 0; q_engage_reach = q_raised_whips = q_raised_holds = q_idle_steps = q_idle_spells = q_whipped_down = 0; for (int i = 0; i < 3; i++) q_sk[i][0] = q_sk[i][1] = 0; q_steps_last = 0; q_vi_first = q_vi_last = 0xFFFFFFFFu; lj_dec_t = lj_dec_m = 0; q_dec_throw = q_dec_move = 0;
    for (int s = 0; s < NM_SLOTS_CAP; s++) lj[s].idle_run = 0;   // (ljround10, the review of b143c55: the idle-in-reach spell begun afresh each match)
    for (int s = 0; s < NM_SLOTS_CAP; s++) { lj[s].hold_k = lj[s].hdec = lj[s].rear_k = lj[s].rear_in = lj[s].istops = lj[s].istand = lj[s].iwalked = 0; lj[s].isize = 0; lj[s].iz = 0.0f; lj[s].iwd = 0.0f; lj[s].iws = 0; }
    for (int s = 0; s < NM_SLOTS_CAP; s++) { lj[s].seen = 0xFFFF; lj[s].given = -1; lj[s].leg_on = lj[s].outer = 0; lj[s].leg_n = 0; lj[s].leg_t = 0; lj[s].pace = 0.0f; }
    tr_full = 0;
    for (int k = 0; k < 4; k++) lj_float_run[k] = 0;
    for (int s = 0; s < NM_SLOTS_CAP; s++) lj_health[s].ok = 0;
    for (int s = 0; s < NM_SLOTS_CAP; s++) { lj[s].home = 0; lj[s].last_st = 0; lj[s].last_df = 0; lj[s].last_f = 0; lj[s].last_x = lj[s].last_z = 0.0f; }
    lj_rand = 0x6C1E5EEDu; lj_brawl_t = 0;
    for (int s = c; s < n; s++) if (match_slots_is_extra(s) && present(s)) q_present++;
    for (int a = c; a < n; a++) for (int b = a + 1; b < n; b++)
        if (match_slots_is_extra(a) && match_slots_is_extra(b) && present(a) && present(b) && rivals(a, b)) q_rival_pairs++;
    lj_live = 1;
    removal_s("lumberjack: the match set up: competitors "); removal_num(c); removal_s(", lumberjacks "); removal_num((s32)q_present);
    removal_s(", rival pairs among them "); removal_num((s32)q_rival_pairs); removal_say();
    for (int k = 0; k < c; k++) {                                    // (the log: the competitors' records and movesets -- lumberjack3:
        u32 mi = RD32(rec(k) + 0x2E4u);                              //  b6/b7's tornado crashed on an empty record 3)
        removal_s("lumberjack: competitor "); removal_num(k); removal_s(" "); removal_who((u16)RDS16(rec(k) + LJ_ID));
        removal_s(", match record "); removal_s(RD8(LJ_MATCH_RECS + 0x114u * (u32)k + 0xDCu) ? "built" : "EMPTY");
        removal_s(", walk row "); removal_num((mi >= 0x80000000u && mi < 0x80800000u) ? (s32)RD8(mi) : -1); removal_say();
    }
    for (int s = c; s < n; s++) {                                    // (the log: each one's rivals, the game's data)
        if (!match_slots_is_extra(s) || !present(s)) continue;
        removal_s("lumberjack: slot "); removal_num(s); removal_s(" "); removal_who((u16)RDS16(rec(s) + LJ_ID));
        if (qa_lj_trace) { removal_s(", +0x80 "); removal_num(RD16(rec(s) + 0x80)); removal_s(" +0xF6 "); removal_num(RD8(rec(s) + 0xF6)); removal_s(" +0xE9 "); removal_num(RD8(rec(s) + 0xE9)); removal_s(" +0xF1 "); removal_num(RD8(rec(s) + 0xF1)); }
        removal_s(", rivals");
        for (int k = 0; k < 3; k++) { u32 t = tie(s, k); removal_s(" "); if (t) removal_who(t); else removal_s("-"); }
        removal_say();
    }
    intro_setup(c, n);
}
static void jit_say(void);
void lumberjack_after(void) {
    if (lj_live) {                                   // (the log: the hits among lumberjacks, put back; the competitors' on them)
        removal_s("lumberjack: the match over: lumberjack-on-lumberjack hits "); removal_num((s32)q_lj_hits); removal_s(" (damage put back "); removal_num((s32)q_lj_undone);
        removal_s("), the competitors' hits on lumberjacks "); removal_num((s32)q_comp_hits); removal_s(" (hurt "); removal_num((s32)q_comp_dmg); removal_s(")"); removal_say();
        removal_s("lumberjack: at the posts: lumberjack-seconds "); removal_num((s32)(q_post_steps / 30u)); removal_s(", steps not standing "); removal_num((s32)q_post_busy);
        removal_s(", taunts "); removal_num((s32)q_post_taunts); removal_s(", moves "); removal_num((s32)q_post_moves); removal_s(" (pushed by a man beside him "); removal_num((s32)q_post_pushed); removal_s(")"); removal_s(", state changes "); removal_num((s32)q_post_flips);
        removal_s(", reversed turns "); removal_num((s32)q_face_rev); removal_s("; player 1's hits on lumberjacks "); removal_num((s32)q_p1_lj_hits); removal_s(" (radii computed "); removal_num((s32)q_radii); removal_s(")");
        removal_s("; roll-in holds from behind "); removal_num((s32)q_behind); removal_s(", turned to the front "); removal_num((s32)q_turned); removal_s(", ended "); removal_num((s32)q_behind_lost); removal_s(", C-up from behind with the throw decided "); removal_num((s32)q_behind_cup); removal_s(" (the game's own moves' "); removal_num((s32)q_behind_cup_game); removal_s(")"); removal_say();
        jit_say();                                   // (ljtwitch: the jitter measure, every phase)
        {   // (ljround8: the aggression measure -- per competitor, the lumberjacks' hits on him out there, his time out, the
            //  lumberjack-time on him; the holds' throw / grapple-move decisions; holds from behind, on a man heading in, loops;
            //  the intro's stops)
            int c = match_slots_competitors(); if (c > 4) c = 4;
            for (int k = 0; k < c; k++) {
                removal_s("lumberjack: aggression -- competitor "); removal_num(k); removal_s(human(k) ? " (a controller's)" : " (the computer's)");
                removal_s(": lumberjacks' hits on him outside "); removal_num((s32)q_c_hits[k]); removal_s(", his time out (s) "); removal_num((s32)(q_c_out[k] / 30u));
                removal_s(", lumberjack-seconds on him "); removal_num((s32)(q_c_eng[k] / 30u));
                removal_s(", hits a minute out x10 "); removal_num(q_c_out[k] ? (s32)(q_c_hits[k] * 18000u / q_c_out[k]) : 0);
                removal_s(", hits a lumberjack-minute x10 "); removal_num(q_c_eng[k] ? (s32)(q_c_hits[k] * 18000u / q_c_eng[k]) : 0); removal_say();
            }
            {   u32 all = 0; for (int i = 0; i < EC_N; i++) all += q_eng_cls[i];
                removal_s("lumberjack: on a man (lumberjack-steps "); removal_num((s32)all); removal_s("), per 1000:");
                for (int i = 0; i < EC_N; i++) { removal_s(" "); removal_s(ec_name[i]); removal_s(" "); removal_num(all ? (s32)(q_eng_cls[i] * 1000u / all) : 0); removal_s(","); }
                removal_s(" on his feet within 85 with no attack under way "); removal_num(all ? (s32)(q_eng_near_idle * 1000u / all) : 0);
                removal_s("; the man held "); removal_num(all ? (s32)(q_eng_man_held * 1000u / all) : 0); removal_s(", down / in a move / hit "); removal_num(all ? (s32)(q_eng_man_down * 1000u / all) : 0);
                removal_say();
                removal_s("lumberjack: on a man (continued)");
                removal_s("; the men out (s) by how far out: under 370 "); removal_num((s32)(q_out_d[0] / 30u)); removal_s(", 370-430 "); removal_num((s32)(q_out_d[1] / 30u)); removal_s(", 430+ "); removal_num((s32)(q_out_d[2] / 30u));
                removal_s("; idle in reach (spells / s) "); removal_num((s32)q_idle_spells); removal_s(" / "); removal_num((s32)(q_idle_steps / 30u));
                removal_s(", whips on the ramp / stage "); removal_num((s32)q_raised_whips); removal_s(", put on a man in reach "); removal_num((s32)q_engage_reach);
                removal_say(); removal_s("lumberjack: on a man (continued 2)");
                removal_s("; strikes begun on him "); removal_num((s32)q_strikes); removal_s(", attacks put off for a mate in the way "); removal_num((s32)q_mate_way); removal_s(" / for the man reeling or in a move "); removal_num((s32)q_held_blow);
                removal_s(" (landed / of: the man up "); removal_num((s32)q_sk[0][1]); removal_s("/"); removal_num((s32)(q_sk[0][0] + q_sk[0][1]));
                removal_s(", down "); removal_num((s32)q_sk[1][1]); removal_s("/"); removal_num((s32)(q_sk[1][0] + q_sk[1][1]));
                removal_s(", else "); removal_num((s32)q_sk[2][1]); removal_s("/"); removal_num((s32)(q_sk[2][0] + q_sk[2][1])); removal_s(")");
                removal_s("; the match's steps "); removal_num((s32)q_steps_last); removal_s(" over the game's frames "); removal_num((s32)(q_vi_last - q_vi_first));
                removal_s("; holds begun there "); removal_num((s32)q_hold_d[0]); removal_s(" / "); removal_num((s32)q_hold_d[1]); removal_s(" / "); removal_num((s32)q_hold_d[2]);
                removal_say();
            }
            removal_s("lumberjack: holds of a competitor from the front -- the throw in "); removal_num((s32)q_hold_throw); removal_s(", a grapple move "); removal_num((s32)q_hold_move);
            removal_s(" (decisions, front or behind: the throw "); removal_num((s32)q_dec_throw); removal_s(" / a move "); removal_num((s32)q_dec_move); removal_s(")");
            removal_s("; from behind "); removal_num((s32)q_rear_grabs); removal_s(", on a man heading back in "); removal_num((s32)q_rear_in); removal_s(", turned to face by R "); removal_num((s32)q_rear_turned);
            removal_s(", got out of "); removal_num((s32)q_rear_esc); removal_s(" (heading in "); removal_num((s32)q_rear_esc_in); removal_s("), escape loops "); removal_num((s32)q_loops);
            removal_s("; the intro's stops "); removal_num((s32)q_istops); removal_s(" (most by one "); removal_num((s32)q_istop_max); removal_s(", walkers "); removal_num((s32)q_iwalkers); removal_s(")");
            removal_s("; the game's passive choices made attacks "); removal_num((s32)q_passive_vetoed); removal_say();
        }
    }
    lj_live = 0; lj_intro = 0; lumberjack_cam[0] = 0.0f;
}

// ---- no damage among lumberjacks (lumberjack3, the user 2026-11-21) --------------------------------------------------
// A lumberjack's move on another lumberjack -- a brawl, or two of them in the swarm round a man -- plays as the game plays
// it (the move, the bump, the sell), but it does him no damage. The game's damage step (apply_move_damage, func_80112074)
// takes the attacker from the man hit's own target (its callers pass his step's target record). So after each step: a
// lumberjack whose damage went up with another lumberjack the nearest man to him, and no competitor close (120), gets back
// what he had -- stamina and its cap, the five body parts, the hurt walk's part, his time down, his spirit; a competitor's
// hit stands. (lumberjack4: by the nearest man, not his target -- at his post a lumberjack's target is now the lumberjack
// across the ring, and a competitor's punch there must still hurt. A hook on apply_move_damage itself never ran: the
// converted move states call it directly, and it has no compiled hook place -- b10's brawls, 0 calls seen.)
// the strike states: find_strike_target's callers (lifted.cpp: the strikes' hit test)
static int striking(s32 st) {                   // (the strike states: find_strike_target's callers)
    return st == 7 || st == 8 || st == 26 || (st >= 31 && st <= 44) || st == 97 || st == 98 || st == 191 || st == 203;
}
static void health_keep(int s) {
    LjHealth* h = &lj_health[s]; u32 r = rec(s);
    h->sta = RDS16(r + LJ_STAMINA); h->max = RDS16(r + LJ_STAMINA_MAX); h->spirit = RDS16(r + LJ_SPIRIT);
    h->punished = RDS16(r + LJ_PUNISHED); h->hurt = (s8)RD8(r + LJ_HURT_PART); h->att = RDS32(r + LJ_STRUCK_BY);
    for (u32 i = 0; i < 5; i++) h->body[i] = RD32(r + LJ_BODY + 4u * i);
    h->ok = 1;
}
static void health_check(int s, int c) {
    LjHealth* h = &lj_health[s]; u32 r = rec(s);
    if (!h->ok) return;
    int hurt = RDS16(r + LJ_STAMINA) < h->sta || RDS16(r + LJ_STAMINA_MAX) < h->max;
    for (u32 i = 0; i < 5; i++) { union { u32 u; float f; } a, b; a.u = RD32(r + LJ_BODY + 4u * i); b.u = h->body[i]; if (a.f < b.f) hurt = 1; }
    if (!hurt) return;
    int by = -1, n = match_slots_count(); float bd = 1e9f, comp = 1e9f;
    for (int o = 0; o < n; o++) {
        if (o == s || !present(o)) continue;
        float d = dist1(o, s);
        if (d < bd) { bd = d; by = o; }
        if (o < c && d < comp) comp = d;
    }
    // who did it (lumberjack5): a strike that landed this step names its striker in the man's +0x00 (apply_strike_to_target);
    // a move's other man is his target (set_pair_states); else the nearest man. (lumberjack3/4 took the nearest man alone:
    // in a crowd of lumberjacks round the player one of them was often nearer, and the player's own hit was put back.)
    s32 att = RDS32(r + LJ_STRUCK_BY), tg = RDS32(r + LJ_TARGET);
    int who = by;
    if (att != h->att && att >= 0 && att < n) who = att;
    else if (tg >= 0 && tg < n && tg != s && present(tg) && dist1(tg, s) < 200.0f) who = tg;
    int lumberjack = who >= c && match_slots_is_extra(who);
    if ((qa_lj_trace & 2) && present(0) && dist1(0, s) < 200.0f) {
        removal_s("ljhurt t "); removal_num(match_slots_steps()); removal_s(" lj "); removal_num(s); removal_s(" by "); removal_num(who);
        removal_s(" (struck by "); removal_num(att); removal_s(", his target "); removal_num(tg); removal_s(", nearest "); removal_num(by); removal_s(" at "); removal_num((s32)bd);
        removal_s(") p1 st "); removal_num(state_of(0)); removal_s(" at "); removal_num((s32)dist1(0, s));
        removal_s(lumberjack ? " -> PUT BACK" : " -> kept"); removal_say();
    }
    if (lumberjack) {
        q_lj_hits++; q_lj_undone++;
        RDS16(r + LJ_STAMINA) = h->sta; RDS16(r + LJ_STAMINA_MAX) = h->max; RDS16(r + LJ_SPIRIT) = h->spirit;
        RDS16(r + LJ_PUNISHED) = h->punished; RD8(r + LJ_HURT_PART) = (u8)h->hurt;
        for (u32 i = 0; i < 5; i++) RD32(r + LJ_BODY + 4u * i) = h->body[i];
    } else if (who >= 0 && who < c) {
        q_comp_hits++; q_comp_dmg++;
        if (who == 0 && human(0)) q_p1_lj_hits++;             // (QA: player 1's own hits on a lumberjack, kept)
    }
}

// the nearest lumberjack free to go (at or on the way to his post, on the floor) to competitor k; -1 none
static int nearest_free(int k, int c, int n) {
    int best = -1; float bd = 1e9f;
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s) || lj[s].mode != LJ_POST || !on_floor(s)) continue;
        float d = dist1(s, k);
        if (d < bd) { bd = d; best = s; }
    }
    return best;
}
// The rollers for competitor k (LJ_ROLLERS): the nearest lumberjacks on the floor who are free, on him already (the swarm),
// or his rollers already -- so an attempt that failed is followed at once by the next with the men who are there, not by
// men called from their posts (b30: every retry released the roller and picked one walking back to his post, 400-800 off,
// and no approach was ever finished). Those no longer picked go back to their posts. Returns the roller, -1 none.
static int pick_rollers(int k, int c, int n) {
    Competitor* m = &lc[k];
    int a = -1, b = -1; float da = 1e9f, db = 1e9f;
    for (int t = c; t < n; t++) {
        if (!match_slots_is_extra(t) || !present(t) || !on_floor(t)) continue;
        int ok = lj[t].mode == LJ_POST || ((lj[t].mode == LJ_SWARM || lj[t].mode == LJ_ROLL) && lj[t].prey == k);
        if (!ok) continue;
        float d = dist1(t, k);
        if (d < da) { b = a; db = da; a = t; da = d; } else if (d < db) { b = t; db = d; }
    }
    for (int t = c; t < n; t++)                       // (the rollers no longer picked: back to their posts)
        if (lj[t].mode == LJ_ROLL && lj[t].prey == k && t != a && t != b) lj[t].mode = LJ_POST;
    if (LJ_ROLLERS < 2) b = -1;
    if (a >= 0) { lj[a].mode = LJ_ROLL; lj[a].prey = (u8)k; if (!lj_engaged[a]) { lj_engaged[a] = 1; q_engaged++; } }
    if (b >= 0) { lj[b].mode = LJ_ROLL; lj[b].prey = (u8)k; if (!lj_engaged[b]) { lj_engaged[b] = 1; q_engaged++; } }
    m->roller = (u8)(a >= 0 ? a : 0); m->roller2 = (u8)(b >= 0 ? b : 0xFF);
    return a;
}
// a competitor on his way back in by himself: climbing in, sliding in, on the apron, or walking / running at it close to it,
// facing in -- the lumberjacks never stop that (real lumberjacks put a man back in; they never keep him out)
static int escaping(int k) {
    s32 st = state_of(k);
    if (st == ST_CLIMB_IN || st == ST_SLIDE_IN || st == ST_WALK_IN || (area(k) & 2)) return 1;
    // (on his way, walking or running: standing still by the apron is not -- b22: the player stood there facing in and the
    //  lumberjacks let him be 20 s)
    if (st != ST_WALKING && st != ST_RUNNING) return 0;
    float x = wx(k), z = wz(k), edge = (fabs_f(x) > fabs_f(z) ? fabs_f(x) : fabs_f(z)) - LJ_APRON;
    if (edge > 45.0f) return 0;
    s32 f = RDS32(rec(k) + LJ_FACING), in = facing_to(-x, -z), d = (f - in) & 4095;
    return d < 600 || d > 4096 - 600;
}
static int opponent_of(int k, int c) {          // in the ring: his man is a competitor (the nearest of the other side)
    int best = k ^ 1; float bd = 1e9f;
    for (int j = 0; j < c; j++) {
        if (j == k || !present(j)) continue;
        if (c == 4 && RDS32(rec(j) + LJ_TEAM) == RDS32(rec(k) + LJ_TEAM)) continue;
        float d = dist1(j, k);
        if (d < bd) { bd = d; best = j; }
    }
    return best;
}
// the man a lumberjack's target (+0x08) is to be: a swarming or rolling lumberjack's is his man, a brawling one's his
// rival; at his post (or on the way back to it) the lumberjack across the ring (lumberjack4: he stands still, so the
// watcher does -- the game turns a standing man to his target every step), or with nobody across the nearest competitor
// (ljtwitch: the man watched must stand still -- at his post. The game turns a standing man to his target every step, so a
//  watcher followed every step of his man across the ring once that one left his post: the trace's posts, 3432 turns
//  reversed, all but 33 of them while the man watched was off his post. When he leaves it the watcher takes another
//  lumberjack who is at his post, the nearest to the point across the ring, and keeps him while he stays there.)
static int still_at_post(int o) {
    return o < match_slots_count() && match_slots_is_extra(o) && present(o) && lj[o].mode == LJ_POST && lj[o].home && state_of(o) == ST_STANDING;
}
// A computer-played competitor sent back in: to the middle stretch of the side he is nearest (out past the ring steps
// while he goes along it), then into it with C-up held -- the game's own climb in from the floor (111: onto the apron,
// on over the ropes with C-up held), the way the QA escape probe has climbed in since round 2. Only his computer
// player's buttons (lumberjack_cpu runs for no man a controller drives); once he is in, nothing more.
// (lumberjack4: rounds 2-3 put him in the game's walk to the ring (126) and at its climb switched him to the slide under
//  the bottom rope (155): the slide takes its way in from the wall record he touches (+0x348), mid-climb not the one in
//  front of him, and in b13's triple threat it carried Triple H along the apron at ring height and he walked off it at
//  that height, floating -- the user's "my opponent went off the ring while staying at ring level")
static void send_in(int k, u32 ai) {
    Competitor* m = &lc[k];
    float x = wx(k), z = wz(k), ax = fabs_f(x), az = fabs_f(z);
    // (ljtwitch: his side kept once chosen, while he is no more than 40 the other side's way -- by a corner, |x| and |z|
    //  about equal, it was picked afresh each step; and the side's middle stretch entered within LJ_SIDE_ALONG and left only
    //  40 past it -- b-t4's trace: the opponent turned between up and left every step or two at x 180, the stretch's edge)
    if (!m->sside || (m->sside == 1 && ax > az + 40.0f) || (m->sside == 2 && az > ax + 40.0f)) { m->sside = ax < az ? 1 : 2; m->smid = 0; }
    int along_x = m->sside == 1;                         // (his side: x along it on the near / far sides, z on the others)
    float across = along_x ? az : ax, along = along_x ? x : z, sg = (along_x ? z : x) >= 0.0f ? 1.0f : -1.0f;
    u16 in = along_x ? (sg > 0.0f ? 0x0400 : 0x0800) : (sg > 0.0f ? 0x0200 : 0x0100);   // (the D-pad into the ring)
    u16 held = 0, pressed = 0;
    if (fabs_f(along) <= LJ_SIDE_ALONG) m->smid = 1; else if (fabs_f(along) > LJ_SIDE_ALONG + 40.0f) m->smid = 0;
    if ((area(k) & 4) && !m->smid) {
        // along the side to its middle stretch, kept out past the steps (420) on the way
        float t_al = along > 0.0f ? LJ_SIDE_ALONG - 20.0f : -(LJ_SIDE_ALONG - 20.0f), t_ac = sg * (across > 420.0f ? across : 420.0f);
        float tx = along_x ? t_al : t_ac, tz = along_x ? t_ac : t_al;
        held = dpad_to(tx - x, tz - z);
    } else if ((area(k) & 4) && across > LJ_APRON + 40.0f) {
        held = in;                                        // (in to the apron)
    } else if (!(area(k) & 6)) {
        // (ljtwitch: neither the floor by the ring nor the apron -- out past the barricade, in the aisle: toward the ring,
        //  no C-up. It used to take this for "at the apron" and climb with C-up wherever he was: lumberjack_p1's one float
        //  in ~29 runs began a climb (111) at z -509, far past the apron -- by its place, this is the likely way in)
        held = dpad_to(-x, -z);
    } else {
        held = (u16)(in | B_CUP);                         // (at the apron, on it: in, C-up held -- the climb, over the ropes)
        pressed = (match_slots_steps() & 7) == 0 ? B_CUP : 0;
    }
    RD16(ai + 0x0C) = held;
    RD16(ai + 0x0E) = pressed;
}
// QA (lumberjack4), each step of the match: player 1's hits on his opponent -- in the ring, outside, in the ring after a
// roll-in, in the ring after the lumberjacks had him outside -- and any competitor at ring height off the ring (more than
// half a second running: the user's floating man) or out of the arena
static void qa_watch(int c) {
    if (human(0)) q_p1_human = 1;
    if (c >= 2 && present(0)) {
        int o = opponent_of(0, c);
        s16 v = RDS16(rec(o) + LJ_STAMINA);
        if (lj_p1_opp_sta >= 0 && v < lj_p1_opp_sta && present(o)) {
            int by = -1; float bd = 1e9f;                // (the hit was his: he is the nearest man to the opponent, close)
            for (int s = 0; s < match_slots_count(); s++) { if (s == o || !present(s)) continue; float d = dist1(s, o); if (d < bd) { bd = d; by = s; } }
            // (his hit: he is the nearest man to the opponent, or close to him and striking or in a move with him -- in a crowd
            //  of lumberjacks outside one of them is often the nearer: b18 counted no hit outside)
            s32 p1st = state_of(0);
            int attacking = p1st == 34 || p1st == 35 || p1st == 36 || p1st == 81 || p1st == 85 || p1st == 89 || p1st == 97 || p1st == 98 || p1st == 160;
            if ((by == 0 && bd < 130.0f) || (attacking && dist1(0, o) < 130.0f)) {
                if ((area(0) & 1) && (area(o) & 1)) {
                    q_p1_in++;
                    if (q_rolled_in) q_p1_after_roll++;
                } else if (on_floor(0) && on_floor(o)) q_p1_out++;
            }
        }
        lj_p1_opp_sta = v;
    }
    for (int k = 0; k < c && k < 4; k++) {
        if (!present(k)) { lj_float_run[k] = 0; continue; }
        // (ljround10, the user 2026-11-25: "Both must be out" -- a float step needs BOTH his recorded place (+0xFC / +0x104, the
        //  record's height +0x100 at ring height above the ground there: the old test) AND his drawn body point (+0x120 / +0x128,
        //  the middle of his model's bones, which the game's own wall and reach tests use) off the ring in the same band, the
        //  ground looked up at the body's x / z; the height stays the record's. The tornado partner's record went 360 out over
        //  the ropes while his drawn body stayed at the downed man in the ring (r30's frames): not a float. A man thrown or
        //  diving over the ropes has his body out while his record is still in: not a float either)
        float y = *(volatile float*)(rec(k) + LJ_Y);
        float x = fabs_f(wx(k)), z = wz(k), az = fabs_f(z);
        float edge = x > az ? x : az, side = x > az ? az : x;
        // (above the ground there -- the arena's map: RAW's ramp in the aisle stands at ring height, and a man on it is not
        //  floating: b24's "float" was The Rock on the ramp at (-59, 90, 548))
        float ground = ringstage_ground_at(wx(k), z);
        float bxs = *(volatile float*)(rec(k) + 0x120), bz = *(volatile float*)(rec(k) + 0x128), bx = fabs_f(bxs), abz = fabs_f(bz);
        float bedge = bx > abz ? bx : abz, bside = bx > abz ? abz : bx, bground = ringstage_ground_at(bxs, bz);
        int rec_out = edge > 345.0f && edge < 700.0f && side < 250.0f && y > ground + 40.0f;
        int body_out = bedge > 345.0f && bedge < 700.0f && bside < 250.0f && y > bground + 40.0f;
        if (rec_out && body_out) {
            if (lj_float_run[k] < 255) lj_float_run[k]++;
            if (lj_float_run[k] == 15) q_float += 15; else if (lj_float_run[k] > 15) q_float++;
            if ((lj_float_run[k] == 1 || lj_float_run[k] == 15) && q_float < 200u) {   // (ljround10, QA: the float's man -- his state, file,
                //  his computer player's plan and buttons as it chose them and as they went out, his target; the tornado tag's partner
                //  running past the ropes, r20 / r21)
                Lumberjack* F = &lj[k];
                removal_s("lumberjack: FLOAT step "); removal_num(lj_float_run[k]); removal_s(" competitor "); removal_num(k);
                removal_s(" state "); removal_num(state_of(k)); removal_s(" was "); removal_num(RDS32(rec(k) + 0x1C)); removal_s(" file "); removal_num(RDS16(rec(k) + 0xA6));
                removal_s(" frame "); removal_num(RDS16(rec(k) + 0xA4)); removal_s(" at "); removal_num((s32)wx(k)); removal_s(", "); removal_num((s32)y); removal_s(", "); removal_num((s32)z);
                removal_s(" place "); removal_num(RD16(rec(k) + LJ_AREA)); removal_s(" human "); removal_num(human(k));
                removal_s(" plan "); removal_num(F->ai_plan); removal_s(" buttons "); removal_num(F->ai_h); removal_s(" / "); removal_num(F->ai_p);
                removal_s(" -> "); removal_num(F->our_h); removal_s(" / "); removal_num(F->our_p); removal_s(" cpu step "); removal_num((s32)F->cpu_t);
                removal_s(" now "); removal_num(match_slots_steps()); removal_s(" roll "); removal_num(lc[k].roll); removal_s(" probe "); removal_num(lc[k].probe);
                removal_s(" target "); removal_num(RDS32(rec(k) + LJ_TARGET));
                {   // (who is near him: the nearest other man, how far, his state)
                    int nb = -1; float nd = 1e9f;
                    for (int o = 0; o < match_slots_count(); o++) { if (o == k || !present(o)) continue; float d = dist1(o, k); if (d < nd) { nd = d; nb = o; } }
                    if (nb >= 0) { removal_s(" nearest "); removal_num(nb); removal_s(" at "); removal_num((s32)nd); removal_s(" state "); removal_num(state_of(nb)); removal_s(" ("); removal_num((s32)wx(nb)); removal_s(", "); removal_num((s32)wz(nb)); removal_s(")"); }
                }
                removal_say();
            }
        } else lj_float_run[k] = 0;
        if (fabs_f(wx(k)) > 1100.0f || wz(k) < -1100.0f || wz(k) > 3300.0f) q_away++;   // (out of the arena: his record's place, as before)
    }
}
// QA (lumberjack5, qa_lj_trace): the game's log, to see what moves at the posts and where player 1's strikes go.
//   & 1  each lumberjack home at his post: a line whenever his state, animation file, facing or place changes (600 lines a
//        match at most), and every step of the first one for 5 s from 20 s in -- with what his computer player chose, what
//        went out after our job's buttons, and the step that last ran (the user, b33: "the lumberjacks twitching in place";
//        the reversed-turn count saw 0.003 a second)
//   & 2  player 1's strikes: each one started, his target and the nearest lumberjack (how far, how far off his facing, the
//        lumberjack's +0x80 / +0xF6 bits the game's hit test reads); each hurt on a lumberjack near player 1 (health_check)
static void tr_num(const char* k, s32 v) { removal_s(k); removal_num(v); }
static void qa_trace(int c, int n) {
    int t = match_slots_steps();
    if (qa_lj_trace & 1) {
        for (int s = c; s < n; s++) {
            if (!match_slots_is_extra(s) || !present(s)) continue;
            Lumberjack* L = &lj[s]; u32 r = rec(s);
            s32 st = state_of(s), anim = RD16(r + 0xA6), f = RDS32(r + LJ_FACING), tg = RDS32(r + LJ_TARGET);
            float x = wx(s), z = wz(s);
            int home = L->mode == LJ_POST && L->home && L->faced;
            int every = s == c && t >= 20 * LJ_SECOND && t < 25 * LJ_SECOND;
            int changed = st != L->tr_st || anim != L->tr_anim || f != L->tr_f || tg != L->tr_tg || fabs_f(x - L->tr_x) + fabs_f(z - L->tr_z) > 0.05f;
            if (home && (every || (changed && tr_lines < 600))) {
                if (!every) tr_lines++;
                removal_s("ljtrace "); tr_num("t ", t); tr_num(" s ", s); tr_num(" st ", st); tr_num(" ph ", RDS32(r + 0x18));
                tr_num(" anim ", anim); tr_num(" fr ", RDS16(r + 0xA4)); tr_num(" left ", RDS16(r + 0xA2));
                tr_num(" f ", f); tr_num(" df ", ((f - L->tr_f + 2048) & 4095) - 2048);
                tr_num(" x10 ", (s32)(x * 10.0f)); tr_num(" z10 ", (s32)(z * 10.0f));
                tr_num(" dx100 ", (s32)((x - L->tr_x) * 100.0f)); tr_num(" dz100 ", (s32)((z - L->tr_z) * 100.0f));
                tr_num(" tg ", tg); tr_num(" ai ", L->ai_h); tr_num("/", L->ai_p); tr_num(" out ", L->our_h); tr_num("/", L->our_p);
                tr_num(" ran ", (s32)L->cpu_t); tr_num(" f80 ", RD16(r + 0x80)); removal_say();
            }
            L->tr_st = st; L->tr_anim = anim; L->tr_f = f; L->tr_tg = tg; L->tr_x = x; L->tr_z = z;
        }
    }
    if ((qa_lj_trace & 2) && present(0)) {
        s32 st = state_of(0);
        if (striking(st) && !striking((s32)tr_p1_last)) {
            int best = -1; float bd = 1e9f;
            for (int s = c; s < n; s++) if (match_slots_is_extra(s) && present(s)) { float d = dist1(s, 0); if (d < bd) { bd = d; best = s; } }
            s32 tg = RDS32(rec(0) + LJ_TARGET), f0 = RDS32(rec(0) + LJ_FACING);
            removal_s("ljstrike "); tr_num("t ", t); tr_num(" p1 st ", st); tr_num(" area ", area(0)); tr_num(" his target ", tg);
            if (tg >= 0 && tg < n) { tr_num(" at ", (s32)dist1(tg, 0)); tr_num(" off his facing ", ((facing_to(wx(tg) - wx(0), wz(tg) - wz(0)) - f0 + 2048) & 4095) - 2048); }
            if (best >= 0) {
                u32 r = rec(best);
                s32 f = RDS32(rec(0) + LJ_FACING), to = facing_to(wx(best) - wx(0), wz(best) - wz(0));
                tr_num(" nearest lj ", best); tr_num(" at ", (s32)bd); tr_num(" off his facing ", ((to - f + 2048) & 4095) - 2048);
                tr_num(" lj st ", state_of(best)); tr_num(" f80 ", RD16(r + 0x80)); tr_num(" f6 ", RD8(r + 0xF6)); tr_num(" area ", area(best));
                // (the hit test's own inputs: his body point 0 (+0x114) against his place (+0xFC), his radius (0x80188950-block,
                //  +0x70 a slot) -- and player 1's)
                for (int w = 0; w < 2; w++) {
                    int q = w ? 0 : best; u32 rq = rec(q);
                    removal_s(w ? " | p1" : " | lj"); tr_num(" bp0x ", (s32)*(volatile float*)(rq + 0x114)); tr_num(" bp0y ", (s32)*(volatile float*)(rq + 0x118));
                    tr_num(" bp0z ", (s32)*(volatile float*)(rq + 0x11C)); tr_num(" px ", (s32)wx(q)); tr_num(" pz ", (s32)wz(q));
                    tr_num(" r0x100 ", (s32)(100.0f * *(volatile float*)(LJ_RADII + 0x70u * (u32)q))); tr_num(" r5x100 ", (s32)(100.0f * *(volatile float*)(LJ_RADII + 0x70u * (u32)q + 20u)));
                }
            }
            removal_say();
        }
        tr_p1_last = (u32)st;
        // (each step player 1 is on the floor outside: his state, his target and how far, how far his facing is off it, the
        //  target's state and +0x80, the bot's buttons this step -- 900 lines a match at most)
        if (on_floor(0) && tr_lines < 1500) {
            s32 tg = RDS32(rec(0) + LJ_TARGET);
            tr_lines++;
            removal_s("ljp1 "); tr_num("t ", t); tr_num(" st ", st); tr_num(" ph ", RDS32(rec(0) + 0x18)); tr_num(" tg ", tg);
            if (tg >= 0 && tg < n) {
                tr_num(" at ", (s32)dist1(tg, 0)); tr_num(" off ", ((facing_to(wx(tg) - wx(0), wz(tg) - wz(0)) - RDS32(rec(0) + LJ_FACING) + 2048) & 4095) - 2048);
                tr_num(" tgst ", state_of(tg)); tr_num(" tgf80 ", RD16(rec(tg) + 0x80)); tr_num(" tgsta ", RDS16(rec(tg) + LJ_STAMINA));
                if (tg >= c) { tr_num(" tgmode ", lj[tg].mode); tr_num(" tgai ", lj[tg].ai_h); tr_num("/", lj[tg].ai_p); tr_num(" tgout ", lj[tg].our_h); tr_num("/", lj[tg].our_p);
                               tr_num(" tgx10 ", (s32)(wx(tg) * 10.0f)); tr_num(" tgz10 ", (s32)(wz(tg) * 10.0f)); }
                tr_num(" x10 ", (s32)(wx(0) * 10.0f)); tr_num(" z10 ", (s32)(wz(0) * 10.0f));
            }
            tr_num(" pad ", (s32)lj_qa_pad_last); removal_say();
        }
    }
}

// (ljtwitch, QA) what a man is doing this step, for the jitter measure (JC_*); -1 not measured (a competitor in the ring,
// on the apron; a lumberjack parked out of sight)
static int jit_class(int s, int c) {
    s32 st = state_of(s);
    if (s < c) return on_floor(s) ? JC_COMP_OUT : -1;
    Lumberjack* L = &lj[s];
    if (L->mode == LJ_PARKED || L->mode == LJ_GONE) return -1;
    if (L->mode == LJ_INTRO) return JC_INTRO;
    if (!free_state(st)) return JC_BUSY;
    switch (L->mode) {
    case LJ_SWARM: return JC_SWARM;
    case LJ_ROLL: return JC_ROLL;
    case LJ_BRAWL: return JC_BRAWL;
    case LJ_EXIT: return JC_EXIT;
    default: break;
    }
    if (L->home && L->faced) return JC_IDLE_POST;
    // (a lumberjack walks home in the standing state -- walk_by_stick; walking (2) / running (3) in no job is the game's:
    //  a whip, a run -- with the busy ones)
    return st == ST_STANDING ? JC_IDLE_AWAY : JC_WALK;
}
// (ljtwitch, QA) each step: the jitter measure for every man (q_jit), and with qa_lj_trace & 4 a line a man a step in the
// game's log ("ljf": what he is doing, his state, animation, facing, travel angle, place, target, buttons, our writes)
static void qa_jitter(int c, int n, int intro_t) {
    int t = match_slots_steps();
    for (int s = 0; s < n && s < NM_SLOTS_CAP; s++) {
        if (!present(s) || (s >= c && !match_slots_is_extra(s))) continue;
        Jit* J = &lj_jit[s]; Lumberjack* L = &lj[s]; u32 r = rec(s);
        s32 st = state_of(s), anim = RD16(r + 0xA6), fr = RDS16(r + 0xA4), left = RDS16(r + 0xA2), f = RDS32(r + LJ_FACING);
        float x = wx(s), z = wz(s);
        int k = jit_class(s, c);
        if (J->ok && k >= 0) {
            u32* q = q_jit[k];
            q[JE_STEPS]++;
            s32 df = ((f - J->f + 2048) & 4095) - 2048;
            if (df) {
                s8 sg = df > 0 ? 1 : -1;
                s32 a0 = df < 0 ? -df : df, a1 = J->lastdf < 0 ? -J->lastdf : J->lastdf;
                if (J->fsign == -sg && J->fsince <= 10 && a0 >= LJ_JIT_TURN && a1 >= LJ_JIT_TURN) q[JE_TURN]++;
                J->fsign = sg; J->fsince = 0; J->lastdf = (s16)df;
            } else if (J->fsince < 0xFFFF) J->fsince++;
            if (anim != J->anim) {
                if (anim == J->anim_prev && J->anim_t <= 20) q[JE_ANIM]++;
                J->anim_prev = J->anim; J->anim_t = 0;
            } else {
                if (J->anim_t < 0xFFFF) J->anim_t++;
                if (fr < J->fr && J->left > 1) q[JE_RESTART]++;       // (the same file back to an earlier frame before its end)
            }
            if (st != J->st) {
                if (st == J->st_prev && J->st_t <= 20) q[JE_STATE]++;
                J->st_prev = J->st; J->st_t = 0;
            } else if (J->st_t < 0xFFFF) J->st_t++;
            float mx = x - J->x, mz = z - J->z;
            if (fabs_f(mx) + fabs_f(mz) > 0.05f) {
                if (J->m_t <= 10 && mx * J->mx + mz * J->mz < 0.0f) q[JE_POS]++;
                J->mx = mx; J->mz = mz; J->m_t = 0;
            } else if (J->m_t < 0xFFFF) J->m_t++;
        } else if (!J->ok) { J->anim_prev = anim; J->st_prev = st; J->fsign = 0; J->fsince = J->anim_t = J->st_t = J->m_t = 0xFFFF; J->mx = J->mz = 0.0f; }
        if (k < 0) J->fsince = J->anim_t = J->st_t = J->m_t = 0xFFFF;     // (a gap: nothing carried over it)
        if ((qa_lj_trace & 4) && (k >= 0 || s < c) && tr_full < 250000u) {   // (ljround8: the competitors in the ring too, k -1)
            tr_full++;
            removal_s("ljf "); tr_num("t ", t); tr_num(" i ", intro_t); tr_num(" s ", s); tr_num(" k ", k); tr_num(" m ", s < c ? -1 : L->mode);
            tr_num(" st ", st); tr_num(" ph ", RDS32(r + 0x18)); tr_num(" an ", anim); tr_num(" fr ", fr); tr_num(" lf ", left);
            tr_num(" f ", f); tr_num(" ta ", RDS32(r + 0x24)); tr_num(" x ", (s32)(x * 10.0f)); tr_num(" z ", (s32)(z * 10.0f));
            tr_num(" y ", (s32)(*(volatile float*)(r + LJ_Y) * 10.0f)); tr_num(" gy ", (s32)(ringstage_ground_at(x, z) * 10.0f));   // (ljround8: height, the ground's there)
            tr_num(" tg ", RDS32(r + LJ_TARGET)); tr_num(" ai ", L->ai_h); tr_num(" o ", L->our_h); tr_num(" p ", L->our_p); tr_num(" pl ", L->ai_plan);
            tr_num(" w ", L->wr); tr_num(" h ", s < c ? -1 : L->home * 2 + L->faced); tr_num(" a ", area(s)); removal_say();
        }
        J->f = f; J->anim = anim; J->fr = fr; J->left = left; J->st = st; J->x = x; J->z = z; J->ok = 1;
        L->wr = 0;
    }
}
static void jit_say(void) {
    removal_s("lumberjack: walk legs blocked (little headway) "); removal_num((s32)q_blocked); removal_say();
    // (ljrole: the role's numbers -- its decisions, never a step's steering)
    removal_s("lumberjack: the role: lumberjack steps "); removal_num((s32)q_role_steps); removal_s(", plans given "); removal_num((s32)q_plans);
    removal_s(" (walk legs "); removal_num((s32)q_legs); removal_s("), the game's attack choice asked "); removal_num((s32)q_chooser);
    removal_s(" (its plan put aside "); removal_num((s32)q_vetoed); removal_s("), steps a roller pressed R (from behind) "); removal_num((s32)q_r_steps);
    removal_s(", throws refused (his back to the ring) "); removal_num((s32)q_throw_refused); removal_s(", a non-roller's throw put aside "); removal_num((s32)q_throw_vetoed);
    removal_s("; role calls outside a Lumberjack match (never) "); removal_num((s32)lj_role_outside); removal_say();
    static const char* const nm[JC_N] = { "idle at post", "idle away from post (standing, or walking back)", "running / whipped, no job", "swarm", "roll-in", "brawl", "leaving ring", "busy (down / moves)", "intro walk", "competitor out" };
    for (int k = 0; k < JC_N; k++) {
        u32* q = q_jit[k];
        if (!q[JE_STEPS]) continue;
        removal_s("lumberjack: jitter, "); removal_s(nm[k]); tr_num(": man-seconds ", (s32)(q[JE_STEPS] / 30u));
        tr_num(", turn reversals ", (s32)q[JE_TURN]); tr_num(", animation switch-backs ", (s32)q[JE_ANIM]); tr_num(", restarts ", (s32)q[JE_RESTART]);
        tr_num(", state switch-backs ", (s32)q[JE_STATE]); tr_num(", step reversals ", (s32)q[JE_POS]); removal_say();
    }
}
// (QA) the jitter events of some classes together, and their man-seconds: v | secs << 16 -- lj_p .. lj_s
static u32 jit_word(int k0, int k1) {
    u32 ev = 0, st = 0;
    for (int k = k0; k <= k1; k++) { st += q_jit[k][JE_STEPS]; for (int e = JE_TURN; e < JE_N; e++) ev += q_jit[k][e]; }
    st /= 30u;
    return (ev > 0xFFFFu ? 0xFFFFu : ev) | (st > 0xFFFFu ? 0xFFFFu : st) << 16;
}
void lumberjack_step(void) {
    if (!lj_live || lj_intro) return;
    int n = match_slots_count(), c = match_slots_competitors();
    if (n <= 0 || c <= 0) return;
    {   u32 mf = RD16(NM_MATCH_FRAMES);           // (ljround10, QA: the match's steps over the game's own frame count -- the pacing)
        if (mf != 0xFFFFu) { if (q_vi_first == 0xFFFFFFFFu) q_vi_first = mf; q_vi_last = mf; q_steps_last++; } }
    // (lumberjack5, the user: "I can't seem to land striking attacks on lumberjacks" -- b13's trace of the hit test: every
    //  lumberjack's body-part radii were 0 (the player's 13.5): the game computes them in state 0 / the Rumble's walk-in
    //  (compute_body_part_radii), which for a lumberjack ran before his model was built. A strike lands when a striking point
    //  comes within the two men's radii, so none ever reached a lumberjack -- nor a grab. Computed here, the game's own
    //  function, once his model's bounds are there: each step while his part 5 is still 0)
    for (int s = c; s < n; s++)
        if (match_slots_is_extra(s) && present(s) && *(volatile float*)(LJ_RADII + 0x70u * (u32)s + 20u) == 0.0f) { func_80120560(rec(s)); q_radii++; }
    for (int s = c; s < n; s++) if (match_slots_is_extra(s) && present(s)) health_check(s, c);   // (no damage among them: above)
    if (!q_bell_seen) {                              // (QA: the lumberjacks' lowest spirit at the match's first step)
        q_bell_seen = 1;
        for (int s = c; s < n; s++) if (match_slots_is_extra(s) && present(s)) { s32 v = RDS16(rec(s) + LJ_SPIRIT); if ((u32)v < q_spirit_bell) q_spirit_bell = (u32)(v < 0 ? 0 : v); }
    }
    if (c > 4) c = 4;
    qa_watch(c);
    // the competitors: out of the ring (on the floor), how long, whether the swarm has had him, rolled back in, escaping
    for (int k = 0; k < c; k++) {
        Competitor* m = &lc[k];
        u32 r = rec(k);
        if (!present(k)) continue;
        s32 st = state_of(k);
        int player = human(k);                       // (his state, target, buttons: never ours)
        int out = on_floor(k);
        if (out && !m->was_out) {
            q_outs++;
            if (st == ST_OVER_TOP || RDS32(r + LJ_PREV) == ST_OVER_TOP) q_thrown++;   // (thrown over the top, not walked out)
            m->out = 0; m->beat = 0; m->roll = 0; m->rt = 0; m->hits = 0; m->ready = 0; m->rest = 0;
            q_c_rear_in[k] = 0;                      // (ljround8, QA: a new spell out)
            if (k == 0 && player) q_p1_outs++;
        }
        if (!out && m->was_out) {                    // (off the floor: on his way in -- onto the apron, over the ropes)
            m->coming = m->roll >= 2 ? 2 : 1;           // (thrown in from the hold, sent in by us, or back in by himself)
            if (m->roll == 2) {
                q_thrown++;
                if (k == 0 && player) q_p1_thrown++;
                removal_s("lumberjack: the roll-in: competitor "); removal_num(k); removal_s(player ? " (the player's)" : " (the computer's)");
                removal_s(" thrown in from the hold -- his state "); removal_num(st); removal_s(", the roller's "); removal_num(state_of(m->roller));
                removal_s(", the plan given "); removal_num(m->threw); removal_say();
            }
            if (m->roll < 3) m->roll = 0;
            m->out = 0; m->beat = 0;
        }
        if (m->coming && (area(k) & 1)) {            // (back in the ring: from here on nothing of his is ours)
            q_back++;
            if (m->coming == 2) q_rolled_in++; else q_esc_back++;
            m->coming = 0; m->roll = 0;
        }
        if (out) m->coming = 0;
        m->was_out = (u8)out;
        if (!m->roll) m->tdec = 0;                   // (ROUND 9: no roll, no throw decided -- the next hold decides afresh)
        if (m->rest) m->rest--;
        s16 sta = RDS16(r + LJ_STAMINA);
        int esc = out && !m->roll && escaping(k);
        if (esc && !m->esc) q_esc++;
        if (esc && sta < m->stamina) q_esc_hits++;   // (hit on his way back in: QA, never wanted)
        m->esc = (u8)esc;
        if (out) {
            if (m->out < 0xFFFF) m->out++;
            if (m->out > q_max_out) q_max_out = m->out;
            int on = 0;
            for (int s = c; s < n; s++) if ((lj[s].mode == LJ_SWARM || lj[s].mode == LJ_ROLL) && lj[s].prey == k && dist1(s, k) < LJ_NEAR) on = 1;
            if (on && m->beat < 0xFFFF) m->beat++;
            // (ljrole, ROUND 7: out there a computer-played competitor is his own computer player's, the stock game's -- the
            //  game makes whoever hits him his man, and he fights back as it chooses. Rounds 4-6 wrote his target here every
            //  step: the nearest lumberjack on him, then his roller -- gone with the steering. A grab from behind is turned
            //  to the front by the roller's R, the user's way.)
            if (on && sta < m->stamina) { q_hits++; if (m->hits < 255) m->hits++; }   // (hit while they were on him)
            if (k == 0 && player && m->out == 20 * LJ_SECOND) q_p1_long++;            // (QA: the player out 20 s: too long)
            // enough of it (LJ_BEAT_HITS hits or LJ_BEAT_TIME out; beaten already after a failed attempt): the roller gets
            // hold of him (the game's own front grapple: A) and is given the game's plan for throwing him in (lumberjack_cpu)
            if (!m->roll && !esc && !m->rest && (m->ready || m->hits >= LJ_BEAT_HITS || m->out >= LJ_BEAT_TIME)) {
                if (pick_rollers(k, c, n) >= 0) { m->roll = 1; m->rt = 0; m->walk = 0; m->tries_t = 0; m->said = 0; m->threw = 0; q_sends++; }
            }
            if (!player && m->roll < 2 && !esc && m->out > LJ_LATE && free_state(st) && !raised(k)) { m->roll = 3; m->rt = 0; m->sside = m->smid = 0; q_sent_in++; }
            if (m->roll == 3) { if (m->rt < 0xFFFF) m->rt++; if (m->rt > LJ_SEND_MAX) m->roll = 0; }
            else if (m->roll) {
                int s = m->roller;
                // (the grab's time counts only with the roller at him and him up to be grabbed -- b21: after a failed attempt the
                //  next roller came from his post, and the time ran out on his way there, attempt after attempt; his way there
                //  has its own limit, LJ_WALK_MAX)
                if (m->roll != 1 || (free_state(st) && dist1(s, k) < 2.0f * LJ_REACH)) { if (m->rt < 0xFFFF) m->rt++; }
                else if (free_state(st) && m->walk < 0xFFFF) m->walk++;   // (his way there: only while the man is up)
                if (m->roll == 1 && m->tries_t < 0xFFFF) m->tries_t++;
                // (the pair: the roller holding (81), he held (82), the two together -- whoever else the roller may have got)
                int held = state_of(s) == ST_GRAPPLER && st == ST_GRAPPLED && dist1(s, k) < 120.0f;
                // (lumberjack5, the user: "when they grapple from behind, try to roll the opponent into the ring from the back
                //  grapple. This actually makes them try to put the opponent on their shoulders ... They should know that if
                //  they're in a back grapple, they need to flip the opponent to a front grapple first"; and: "I believe it's the
                //  same button to pick them up from the ground". From behind (the grabber's +0x78 & 0x80) C-up is the shoulder
                //  lift for a top-rope team move (129 / 130), not the throw. So a hold from behind is not yet the roll's: the
                //  roller turns his man round with the pick-up button, R (lumberjack_cpu): the game's turn lets him go facing the
                //  roller, who grabs him again at once from the front, and the roll goes on.)
                int holding = held, behind = holding && (RDS32(rec(s) + LJ_GRAB_FLAGS) & LJ_GRAB_BEHIND) != 0;
                if (behind) {
                    if (!m->behind) {
                        q_behind++;
                        removal_s("lumberjack: the roll-in: competitor "); removal_num(k); removal_s(" held FROM BEHIND by "); removal_num(s);
                        removal_s(" (grab flags "); removal_num(RDS32(rec(s) + LJ_GRAB_FLAGS)); removal_s("): turning him to the front -- R, the pick-up button"); removal_say();
                    }
                    m->behind = 1; held = 0;
                } else if (m->behind) {
                    s32 df = ((RDS32(rec(k) + LJ_FACING) - RDS32(rec(s) + LJ_FACING) + 2048) & 4095) - 2048;
                    // (b15: R from behind is the game's turn -- the tie-up's break-off plays the man round to face the roller
                    //  and lets him go (count_down_grapple_tie_up: R alone); facing him, he is grabbed again at once, from
                    //  the front. Counted turned when the pair is still held from the front, or the man now faces him)
                    int faced = holding || df > 1536 || df < -1536;
                    if (faced) q_turned++; else q_behind_lost++;
                    removal_s("lumberjack: the roll-in: competitor "); removal_num(k);
                    removal_s(holding ? " TURNED TO THE FRONT in the hold by " : faced ? " TURNED ROUND TO FACE (R; let go, to be grabbed again from the front): the roller "
                                                                                       : " -- the hold from behind ended (taken again from the front): the roller ");
                    removal_num(s); removal_s(" (grab flags "); removal_num(RDS32(rec(s) + LJ_GRAB_FLAGS)); removal_s(", states "); removal_num(st);
                    removal_s(" / "); removal_num(state_of(s)); removal_s(", facings apart "); removal_num(df < 0 ? -df : df); removal_s(" of 2048 face to face)"); removal_say();
                    m->behind = 0;
                }
                if (held && m->roller2 != 0xFF) {                  // (the roller has him: the second steps back)
                    if (lj[m->roller2].mode == LJ_ROLL && lj[m->roller2].prey == k) lj[m->roller2].mode = LJ_POST;
                    m->roller2 = 0xFF;
                }
                if (m->roll == 1 && !held && st == ST_GRAPPLED) {   // (another lumberjack has hold of him: he is the roller now)
                    for (int t = c; t < n; t++) {
                        if (t == s || !match_slots_is_extra(t) || !present(t) || state_of(t) != ST_GRAPPLER || dist1(t, k) >= 120.0f) continue;
                        if (lj[s].mode == LJ_ROLL && lj[s].prey == k) lj[s].mode = LJ_POST;
                        if (m->roller2 != 0xFF && m->roller2 != t && lj[m->roller2].mode == LJ_ROLL && lj[m->roller2].prey == k) lj[m->roller2].mode = LJ_POST;
                        lj[t].mode = LJ_ROLL; lj[t].prey = (u8)k; m->roller = (u8)t; m->roller2 = 0xFF; s = t;
                        held = !(RDS32(rec(t) + LJ_GRAB_FLAGS) & LJ_GRAB_BEHIND); holding = 1;   // (from behind: turned first, as above)
                        if (!held && !m->behind) { m->behind = 1; q_behind++; }
                        if (!lj_engaged[t]) { lj_engaged[t] = 1; q_engaged++; }
                        break;
                    }
                }
                int done = 0;                                // (the lumberjacks have had him: on to the last part)
                if (m->roll == 1) {
                    if (held) {
                        m->roll = 2; m->rt = 0; m->threw = 0; q_grabbed++;
                        removal_s("lumberjack: the roll-in: competitor "); removal_num(k); removal_s(" held by "); removal_num(s);
                        removal_s(", his grab flags (+0x78) "); removal_num(RDS32(rec(s) + 0x78)); removal_s((RDS32(rec(s) + 0x78) & 0x80) ? " -- FROM BEHIND" : " -- from the front"); removal_say();
                    } else if (m->rt > LJ_GRAB_MAX || m->walk > LJ_WALK_MAX || m->tries_t > LJ_TRY_MAX) {
                        done = 1;
                        if (!m->said) {                  // (the log: why no grip -- once a roll)
                            m->said = 1;
                            removal_s("lumberjack: the roll-in: competitor "); removal_num(k); removal_s(" -- no grip: ");
                            removal_s(m->rt > LJ_GRAB_MAX ? "the grab's time out" : m->walk > LJ_WALK_MAX ? "the way there too long" : "the attempt too long");
                            removal_s(", the roller ");
                            removal_num(s); removal_s(" state "); removal_num(state_of(s)); removal_s(" at "); removal_num((s32)dist1(s, k));
                            removal_s(", grab plans given "); removal_num(m->threw); removal_s(", his state "); removal_num(st);
                            removal_s(" at ("); removal_num((s32)wx(k)); removal_s(", "); removal_num((s32)wz(k)); removal_s(")"); removal_say();
                        }
                    }
                } else if (m->roll == 2) {
                    // (the throw itself -- the game's pair 15 / 16 -- is waited out: he leaves the floor at its end, into the
                    //  ring. b18 ended the roll at its first step, with him still on the floor, and missed the throw)
                    int throwing = st == 16 || state_of(s) == 15;
                    if ((!holding && !throwing && m->rt > LJ_HOLD) || m->rt > LJ_HOLD_MAX + (throwing ? 3 * LJ_SECOND : 0)) done = 1;
                    if (done && !m->said) {
                        m->said = 1;
                        removal_s("lumberjack: the roll-in: competitor "); removal_num(k); removal_s(" -- the hold ended on the floor: his state ");
                        removal_num(st); removal_s(", the roller's "); removal_num(state_of(s)); removal_s(", the plan given "); removal_num(m->threw);
                        removal_s(", at ("); removal_num((s32)wx(k)); removal_s(", "); removal_num((s32)wz(k)); removal_s(")"); removal_say();
                    }
                }
                if (done) {                                // (failed: the next attempt at once, with the men who are there)
                    m->ready = 1;
                    if (pick_rollers(k, c, n) >= 0) { m->roll = 1; m->rt = 0; m->walk = 0; m->tries_t = 0; m->said = 0; m->threw = 0; q_sends++; }
                    else { m->roll = 0; m->rest = LJ_RETRY; }
                }
                // (the last resort, a computer-played man only: out LJ_LATE all the same -- his own climb in)
                if (!player && m->roll < 3 && m->out > LJ_LATE && free_state(st) && !raised(k)) { m->roll = 3; m->rt = 0; m->sside = m->smid = 0; q_sent_in++; }
            }
        } else if ((area(k) & 1) && !player && free_state(st)) {
            // in the ring, a computer-played man's man is a competitor: a lumberjack picked from inside is put back to him
            // (ljrole: kept -- a write only when the game has made a lumberjack his man, once each time: back in the ring
            //  with a lumberjack outside his man, the stock chooser takes him out to that man again (plan 36: a man who is not
            //  taking part, ai_choose_in_ring_target_outside), and the match would never end -- "never fight forever")
            s32 tg = RDS32(r + LJ_TARGET);
            if (match_slots_is_extra(tg)) { RDS32(r + LJ_TARGET) = opponent_of(k, c); lj[k].wr |= LJW_COMP_TGT; }
        }
        // (ljround8, QA: "They don't attack enough. They attack the player more than the CPU") the lumberjacks' hits on him out
        // there -- a drop in his stamina after 10 quiet steps, by a lumberjack: the striker a strike names (+0x00, new this
        // step), else the other man of his move (his target), else the nearest lumberjack within 150; his steps out; the
        // lumberjack-steps on him (the swarm or his roller, within LJ_NEAR)
        {   s32 att = RDS32(r + LJ_STRUCK_BY);
            if (q_c_quiet[k] < 0xFFFF) q_c_quiet[k]++;
            if (out) {
                q_c_out[k]++;
                { float d = ring_d(k); q_out_d[d < 370.0f ? 0 : d < 430.0f ? 1 : 2]++; }   // (ljround10, QA: where he is out there)
                for (int s = c; s < n; s++) if ((lj[s].mode == LJ_SWARM || lj[s].mode == LJ_ROLL) && lj[s].prey == k && present(s) && dist1(s, k) < LJ_NEAR) {
                    q_c_eng[k]++; if (k < 2) q_run_eng[k]++;
                    s32 sst = state_of(s), pl = lj[s].ai_plan; int cl;
                    if (striking(sst) && !lj[s].stk) {           // (a strike begun on him: the man up or down, his stamina then)
                        q_strikes++;
                        lj[s].sk_up = (u8)(free_state(st) ? 1 : (st == 49 || st == 50) ? 2 : 3); lj[s].sk_sta = RDS16(r + LJ_STAMINA); lj[s].sk_k = (u8)k;
                    }
                    if (!striking(sst) && lj[s].stk && lj[s].sk_k == k) {   // (over: did it land -- his stamina down)
                        int c3 = lj[s].sk_up - 1; if (c3 >= 0 && c3 < 3) { q_sk[c3][RDS16(r + LJ_STAMINA) < lj[s].sk_sta ? 1 : 0]++; }
                    }
                    lj[s].stk = (u8)striking(sst);
                    if (striking(sst)) cl = EC_STRIKE;
                    else if (sst == 80) cl = EC_GRAB;
                    else if (sst == ST_GRAPPLER) cl = EC_HOLD;
                    else if (sst == 85 || sst == 93 || sst == 95) cl = EC_MOVE;
                    else if (free_state(sst)) {
                        cl = pl == 60 ? EC_LEG : pl == 55 ? EC_WAIT : (pl == 6 || pl == 22) ? EC_STRIKE_GIVEN :
                             (pl == 1 || pl == 2 || pl == 16 || pl == 17 || pl == 18 || pl == 43 || pl == 19) ? EC_GRAB_GIVEN : EC_OTHER_PLAN;
                        if (dist1(s, k) < 85.0f && (cl == EC_LEG || cl == EC_WAIT || cl == EC_OTHER_PLAN)) q_eng_near_idle++;
                    } else cl = EC_DOWN;
                    q_eng_cls[cl]++;
                    if (!free_state(st) && !striking(st)) { if (st == ST_GRAPPLED) q_eng_man_held++; else q_eng_man_down++; }
                }
                if (sta < m->stamina) {
                    int who = -1;
                    if (att != q_c_att[k] && att >= 0 && att < n) who = att;
                    else { s32 tg = RDS32(r + LJ_TARGET); if (tg >= c && tg < n && present(tg) && dist1(tg, k) < 200.0f) who = tg; }
                    if (who < 0) { float bd = 150.0f; for (int s = c; s < n; s++) if (match_slots_is_extra(s) && present(s) && dist1(s, k) < bd) { bd = dist1(s, k); who = s; } }
                    if (who >= c && match_slots_is_extra(who) && q_c_quiet[k] >= 10) { q_c_hits[k]++; if (k < 2) q_run_hits[k]++; }
                    q_c_quiet[k] = 0;
                }
            }
            q_c_att[k] = att;
            for (int i = 15; i > 0; i--) q_c_rd[k][i] = q_c_rd[k][i - 1];
            q_c_rd[k][0] = ring_d(k);
        }
        m->stamina = sta;
    }
    // anyone out: the lumberjacks' job comes first -- a brawl among them ends
    int anyone_out = 0;
    for (int k = 0; k < c; k++) if (present(k) && on_floor(k)) anyone_out = 1;
    // the lumberjacks: never in the ring or on the apron; the swarm lets go of a man back in, let go, gone or escaping
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s)) continue;
        Lumberjack* L = &lj[s];
        if (RD8(rec(s) + LJ_ROLE) & 0x10) q_side = 1;                              // (never a side of the match: QA)
        {   // (ROUND 9, QA: lj_x) his grapple moves on a competitor out on the floor -- the game's move (85, the man in 86) as it
            // starts, both men on the floor: all, those with the man by the apron's edge (under 370 out), by the barricade or
            // the aisle (430 out or more), and those done from behind (a rear grapple's move) -- the user's "regular back
            // grapple functionality", "CPUs can do whatever moves they want"
            s32 tg = RDS32(rec(s) + LJ_TARGET);
            int mv = state_of(s) == 85 && tg >= 0 && tg < c && state_of(tg) == 86;
            if (mv && !L->mv && on_floor(s) && on_floor(tg)) {
                q_mv_out++;
                if (ring_d(tg) < 370.0f) q_mv_apron++;
                if (ring_d(tg) >= 430.0f) q_mv_far++;
                if (RDS32(rec(s) + LJ_GRAB_FLAGS) & LJ_GRAB_BEHIND) q_mv_rear++;
            }
            L->mv = (u8)mv;
            // (ljround10, QA) his holds of a competitor out there as they start, by where the man is
            int hd = state_of(s) == ST_GRAPPLER && tg >= 0 && tg < c && on_floor(tg);
            if (hd && !L->hd) { float d = ring_d(tg); q_hold_d[d < 370.0f ? 0 : d < 430.0f ? 1 : 2]++; }
            L->hd = (u8)hd;
        }
        {   // (ljround8, QA) his hold of a competitor from behind, once a hold; on a man heading back in -- his distance out
            // down 15 or more over the half second before -- the user's loop ("CPU player tries to get into the ring, Lumberjack
            // tries to back grapple, CPU player knocks back with his elbow to escape the grapple, repeat"); a spell out with two
            // of those is a loop
            s32 tg = RDS32(rec(s) + LJ_TARGET);
            int rk = 0;
            if (state_of(s) == ST_GRAPPLER && tg >= 0 && tg < c && state_of(tg) == ST_GRAPPLED && dist1(s, tg) < 120.0f &&
                (RDS32(rec(s) + LJ_GRAB_FLAGS) & LJ_GRAB_BEHIND)) rk = tg + 1;
            if (L->rear_k && rk != L->rear_k && !qa_lj_behind) {   // (the hold from behind over: turned to face him, or got out)
                int k = L->rear_k - 1;
                s32 df = ((RDS32(rec(k) + LJ_FACING) - RDS32(rec(s) + LJ_FACING) + 2048) & 4095) - 2048;
                int faced = df > 1536 || df < -1536 || (state_of(s) == ST_GRAPPLER && state_of(k) == ST_GRAPPLED);
                if (faced) q_rear_turned++;
                else if (free_state(state_of(k)) || state_of(k) == ST_CLIMB_IN) {
                    q_rear_esc++;
                    if (L->rear_in) { q_rear_esc_in++; if (q_c_rear_in[k] < 255 && ++q_c_rear_in[k] == 2) q_loops++; }
                    removal_s("lumberjack: competitor "); removal_num(k); removal_s(" GOT OUT of a hold from behind by "); removal_num(s);
                    removal_s(L->rear_in ? " (he was heading back in)" : ""); removal_s(", his state "); removal_num(state_of(k)); removal_s(", this spell's "); removal_num(q_c_rear_in[k]); removal_say();
                }
            }
            if (rk && L->rear_k != rk && !qa_lj_behind) {
                int k = rk - 1;
                q_rear_grabs++;
                L->rear_in = q_c_rd[k][15] - q_c_rd[k][1] >= 15.0f;
                if (L->rear_in) q_rear_in++;
                removal_s("lumberjack: a hold FROM BEHIND on competitor "); removal_num(k); removal_s(L->rear_in ? " heading back in" : ""); removal_s(" (out ");
                removal_num((s32)q_c_rd[k][15]); removal_s(" -> "); removal_num((s32)q_c_rd[k][1]); removal_s(") by "); removal_num(s);
                removal_s(", his job "); removal_num(L->mode); removal_s(", the plan given "); removal_num(L->given); removal_s(" -- R, turned to face"); removal_say();
            }
            L->rear_k = (u8)rk;
            // (ljround8, QA) his hold of a competitor from the front: at its end, the throw in (plan 103 given in it) or not
            int hk = 0;
            if (state_of(s) == ST_GRAPPLER && tg >= 0 && tg < c && state_of(tg) == ST_GRAPPLED && dist1(s, tg) < 120.0f &&
                !(RDS32(rec(s) + LJ_GRAB_FLAGS) & LJ_GRAB_BEHIND)) hk = tg + 1;
            if (L->hold_k && hk != L->hold_k) { if (L->hdec & 1) q_hold_throw++; else q_hold_move++; L->hdec = 0; }
            if (!hk && !L->hold_k && state_of(s) != ST_GRAPPLER) L->hdec &= 1;   // (bit 8: R pressed in this hold from behind)   // (no hold: the next one decides afresh)
            L->hold_k = (u8)hk;
        }
        if (state_of(s) == ST_GRAPPLER && !(L->mode == LJ_ROLL && L->prey < 4 && lc[L->prey].roll == 2)) { if (L->grip < 0xFFFF) L->grip++; }
        else L->grip = 0;                                                         // (holding a man -- not the roll's hold: how long)
        // (in the ring or on the apron: out at once. QA counts the steps he is there on his feet -- able to leave: a lumberjack
        //  thrown in by a competitor, with the game's own throw, lies there a while first, b32's triple threat)
        // (ljround8: by his place too -- r5's trace: a lumberjack the player threw over the top lay and rose on the floor 300-420
        //  out with the game's area still 1, the ring's; the leave-the-ring plan walked him about the floor for 5 s, turning
        //  each step -- 21 jitter events a second. Within the apron's footprint only)
        if ((area(s) & 3) && ring_d(s) < LJ_APRON + 30.0f) { if (L->mode != LJ_EXIT) L->mode = LJ_EXIT; if (free_state(state_of(s))) q_ring_steps++; continue; }
        if (L->mode == LJ_EXIT) L->mode = LJ_POST;
        // (ljtwitch: off on a job, he is not at his post -- home and faced stayed set through a roll-in or a brawl, and back
        //  in POST far from the post he counted as at it, and was watched as standing still there)
        if (L->mode != LJ_POST) { L->home = 0; L->faced = 0; L->direct = 0; L->corner = 0; L->blocks = 0; }
        if (L->mode == LJ_BRAWL && anyone_out) L->mode = LJ_POST;
        if (L->mode == LJ_SWARM) {
            int k = L->prey;
            int near = present(k) && dist2(s, k) <= LJ_ENGAGE;   // (ljround10: in reach, he stays on him -- only the roll's hold lets go)
            if (k >= c || !present(k) || !on_floor(k) || lc[k].roll == 2 || ((lc[k].roll >= 2 || lc[k].esc || lc[k].rest) && !near)) L->mode = LJ_POST;   // (ljround8: on him through the roller's grab, roll 1)
        }
        if (L->mode == LJ_ROLL) {
            int k = L->prey;
            if (k >= c || !present(k) || !on_floor(k) || lc[k].roll == 0 || (lc[k].roller != s && lc[k].roller2 != s)) L->mode = LJ_POST;
        }
    }
    // each man out, not being thrown in nor escaping: the nearest lumberjacks go for him (2, or 3 with ten or more) --
    // (ljround8: "They don't attack enough") on him through the roller's grab too. (r3 sent 3 / 4: four round one man
    // walked into each other, the swarm's jitter 6 events a second)
    // (ljround10: r10 tried 4 with ten or more -- more of them waiting round him and hitting each other (19-33 a match), fewer
    //  hits on him: back to 3)
    int want = (n - c) >= 10 ? 3 : 2;
    // (ljround10) first: any lumberjack in reach of a competitor out there -- at his post, on his way back to it, leaving the
    // ring's footprint -- is on him (r5: a lumberjack stood looking at a computer-played man stuck walking on the ramp, the
    // roll's "sent back in" (roll 3) keeping the swarm off; the user: never idle in striking distance). Not while the roll's
    // own hold / throw is under way (roll 2: the game's move in progress), nor a lumberjack on another job of his own
    for (int k = 0; k < c; k++) {
        if (!present(k) || !on_floor(k) || lc[k].roll == 2) continue;
        for (int s = c; s < n; s++) {
            Lumberjack* L = &lj[s];
            if (!match_slots_is_extra(s) || !present(s) || (area(s) & 3)) continue;
            // (on his post, home, a brawl; or on the swarm of another man not in his reach -- r6: a swarm lumberjack walking past
            //  a man stuck up the aisle, 60 from him, on his way to the other competitor)
            int other = L->mode == LJ_SWARM && L->prey != k && (L->prey >= c || !present(L->prey) || dist2(s, L->prey) > LJ_ENGAGE);
            if (L->mode != LJ_POST && L->mode != LJ_BRAWL && !other) continue;
            if (dist2(s, k) > LJ_ENGAGE || crosses_ring(wx(s), wz(s), wx(k), wz(k))) continue;
            if (L->mode == LJ_BRAWL) { if (lj[L->prey].mode == LJ_BRAWL && lj[L->prey].prey == s) lj[L->prey].mode = LJ_POST; }
            L->mode = LJ_SWARM; L->prey = (u8)k; L->faced = 0; q_engage_reach++;
            if (!lj_engaged[s]) { lj_engaged[s] = 1; q_engaged++; }
        }
    }
    for (int k = 0; k < c; k++) {
        if (!present(k) || !on_floor(k) || lc[k].roll >= 2 || lc[k].esc || lc[k].rest) continue;
        int have = 0;
        for (int s = c; s < n; s++) if (lj[s].mode == LJ_SWARM && lj[s].prey == k) have++;
        while (have < want) {
            int s = nearest_free(k, c, n);
            if (s < 0) break;
            lj[s].mode = LJ_SWARM; lj[s].prey = (u8)k; lj[s].faced = 0; have++;
            if (!lj_engaged[s]) { lj_engaged[s] = 1; q_engaged++; }
        }
    }
    // (ljround10, QA lj_z) a lumberjack idle in striking distance of a competitor out there: on his feet within 85 of him
    // (not in the roll's hold, not held), and not on him -- another job, or his plan the wait (55) or none. A spell of 60
    // steps (2 s) is counted -- the user 2026-11-24: "Half a second is ok.. I just meant extended idle" (was 15 steps, half
    // a second, from the user's first "never")
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s)) continue;
        Lumberjack* L = &lj[s];
        int idle = 0, who = -1;
        if (free_state(state_of(s)) && !(area(s) & 3))
            for (int k = 0; k < c && !idle; k++) {
                s32 kst = state_of(k);
                if (!present(k) || !on_floor(k) || lc[k].roll == 2 || dist2(s, k) > LJ_STRIKE_NEAR) continue;
                if (!free_state(kst) && kst != 49) continue;   // (reeling from a blow, coming up, in a move, held: in progress)
                int on = (L->mode == LJ_SWARM || L->mode == LJ_ROLL) && L->prey == k;
                if (!on || L->ai_plan == 55 || L->ai_plan == 0) { idle = 1; who = k; }
            }
        if (!idle) { L->idle_run = 0; continue; }
        q_idle_steps++;
        if (L->idle_run < 255) L->idle_run++;
        if (L->idle_run == 60) {
            q_idle_spells++;
            if (q_idle_spells <= 10u) {
                removal_s("lumberjack: IDLE IN REACH -- lumberjack "); removal_num(s); removal_s(" job "); removal_num(L->mode); removal_s(" man "); removal_num(L->prey);
                removal_s(" plan "); removal_num(L->ai_plan); removal_s(" state "); removal_num(state_of(s)); removal_s(", competitor "); removal_num(who);
                removal_s(" state "); removal_num(state_of(who)); removal_s(" roll "); removal_num(lc[who].roll); removal_s(" at "); removal_num((s32)dist2(s, who));
                removal_s(" ("); removal_num((s32)wx(who)); removal_s(", "); removal_num((s32)wz(who)); removal_s(")"); removal_say();
            }
        }
    }
    // the brawls: lumberjacks fight among themselves -- only rivals (the game's own Rival 1-3), a 10% chance each 10 s
    // window with nobody out, for that window
    for (int s = c; s < n; s++) {                    // (a brawl: on till 10 s after they met, 20 s at most)
        if (lj[s].mode != LJ_BRAWL) continue;
        Lumberjack* L = &lj[s];
        if (L->t < 0xFFFF) L->t++;
        if (!L->met && dist1(s, L->prey) < LJ_NEAR) L->met = (u16)L->t;
        if ((L->met && L->t - L->met > LJ_BRAWL_FIGHT) || L->t > LJ_BRAWL_MAX || lj[L->prey].mode != LJ_BRAWL) L->mode = LJ_POST;
    }
    int brawling = 0;
    for (int s = c; s < n; s++) if (lj[s].mode == LJ_BRAWL) brawling = 1;
    if (++lj_brawl_t >= LJ_BRAWL_WIN) {
        lj_brawl_t = 0;
        int pct = qa_lj_brawl >= 0 ? qa_lj_brawl : 10;
        if (!anyone_out && !brawling) {
            q_brawl_rolls++;
            if ((int)(rnd() % 100u) < pct) {
                int pa[64], pb[64], np = 0;
                for (int a = c; a < n; a++) for (int b = a + 1; b < n && np < 64; b++) {
                    if (!match_slots_is_extra(a) || !match_slots_is_extra(b) || !present(a) || !present(b)) continue;
                    if (lj[a].mode != LJ_POST || lj[b].mode != LJ_POST || !on_floor(a) || !on_floor(b)) continue;
                    if (rivals(a, b)) { pa[np] = a; pb[np] = b; np++; }
                }
                if (np) {
                    int i = (int)(rnd() % (u32)np);                  // (a pair at random; then the closest pair, half the time:
                    if (rnd() & 1) { float bd = 1e9f; for (int j = 0; j < np; j++) { float d = dist1(pa[j], pb[j]); if (d < bd) { bd = d; i = j; } } }   //  neighbours fall out more)
                    int a = pa[i], b = pb[i];
                    lj[a].mode = LJ_BRAWL; lj[a].prey = (u8)b; lj[b].mode = LJ_BRAWL; lj[b].prey = (u8)a;
                    lj[a].t = lj[b].t = 0; lj[a].met = lj[b].met = 0;
                    q_brawls++;
                    if (!rivals(a, b)) q_brawl_bad++;
                    removal_s("lumberjack: a brawl -- "); removal_who((u16)RDS16(rec(a) + LJ_ID)); removal_s(" and "); removal_who((u16)RDS16(rec(b) + LJ_ID)); removal_say();
                }
            }
        }
    }
    // QA: at his post, is he still? -- each step a lumberjack is home in POST: a step in any state but standing (lumberjack5:
    // a taunt, a run -- what the user saw twitching; b1's trace), a taunt begun, a move over 1 unit; (lumberjack3) a state
    // change, a turn that reverses the last one (both over 8 / 4096) -- the measure that saw 0.003 a second and missed it
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s)) continue;
        Lumberjack* L = &lj[s];
        s32 st = state_of(s), f = RDS32(rec(s) + LJ_FACING); float x = wx(s), z = wz(s);
        if (L->mode == LJ_POST && L->home && L->faced) {
            q_post_steps++;
            if (st != L->last_st) q_post_flips++;
            if (st != ST_STANDING) q_post_busy++;
            if (st == ST_TAUNT && L->last_st != ST_TAUNT) q_post_taunts++;
            s32 df = ((f - L->last_f + 2048) & 4095) - 2048;
            if ((df > 8 && L->last_df < -8) || (df < -8 && L->last_df > 8)) q_face_rev++;
            float mvd = fabs_f(x - L->last_x) + fabs_f(z - L->last_z);
            // (ROUND 9: what moved him -- r6's trace, lumberjack_p1_triple: each match's first 12 "moves" were the intro's end
            //  putting each at his post (400-670 in a step: a placement, not a move), and the rest a man standing next to him
            //  (61-84 away: the game's keeping two men apart) pushing him a step at a time -- counted apart, as pushes; his own
            //  moves, with nobody within 90 of him, are what the stillness check counts)
            int pushed = 0;
            if (mvd > 1.0f && mvd <= 50.0f) for (int j = 0; j < n; j++) if (j != s && present(j) && dist2(j, s) < 90.0f) pushed = 1;
            if (mvd > 1.0f && mvd <= 50.0f && pushed) q_post_pushed++;
            if (mvd > 1.0f && mvd <= 50.0f && !pushed) {
                q_post_moves++;
                if ((qa_lj_trace & 8) && q_post_moves <= 60u) {   // (ROUND 9, QA: who moved him -- the nearest man and what he is doing)
                    int o = -1; float od = 1e9f;
                    for (int j = 0; j < n; j++) if (j != s && present(j) && dist1(j, s) < od) { od = dist1(j, s); o = j; }
                    removal_s("ljpost s "); removal_num(s); removal_s(" moved "); removal_num((s32)(10.0f * (fabs_f(x - L->last_x) + fabs_f(z - L->last_z))));
                    removal_s(" x10, st "); removal_num(st); removal_s(" at ("); removal_num((s32)x); removal_s(", "); removal_num((s32)z);
                    removal_s("), the nearest "); removal_num(o); removal_s(" at "); removal_num((s32)od); removal_s(" st "); removal_num(o >= 0 ? state_of(o) : -1);
                    removal_s(" job "); removal_num(o >= c ? lj[o].mode : -1); removal_say();
                }
            }
            L->last_df = (s16)df;
        } else L->last_df = 0;
        L->last_st = (u8)st; L->last_f = f; L->last_x = x; L->last_z = z;
    }
    // (QA: the brawls' hits)
    // (ljrole, ROUND 7: no more targets written here each step (lj_target, rounds 4-6), no facing turned for a roller
    //  waiting by his man (ljtwitch2), none for the first steps after the intro (ljtwitch): a lumberjack's man is set by his
    //  role when it decides a job -- lumberjack_role, at his computer player's choice -- and the game turns him)
    for (int s = c; s < n; s++) {
        if (!match_slots_is_extra(s) || !present(s)) continue;
        s16 v = RDS16(rec(s) + LJ_STAMINA);
        if (lj[s].mode == LJ_BRAWL && v < lj_brawl_sta[s]) q_brawl_hits++;
        lj_brawl_sta[s] = v;
    }
    if (qa_lj_trace) qa_trace(c, n);                                                           // (QA: the trace, lumberjack5)
    qa_jitter(c, n, 0);                                                                         // (QA: the jitter measure, ljtwitch)
    for (int s = c; s < n; s++) if (match_slots_is_extra(s) && present(s)) health_keep(s);      // (what the next step is checked against)
}

// QA (qa_lj_out): the last competitor walks out of the ring -- to the nearest ropes, out through them (C-up) -- every
// qa_lj_out seconds from 15 s in, as a man who leaves the ring would; the lumberjacks' answer is what the test looks at.
// With qa_lj_esc every other one climbs back in by himself 3 s after he is out (to the nearest side, C-up held: the
// game's own climb), as a player would: the lumberjacks must let him.
static void lj_probe(int s, u32 ai) {
    Competitor* m = &lc[s];
    int t = match_slots_steps();
    if (!m->probe) {
        if (t < (15 + (int)q_probes * qa_lj_out) * LJ_SECOND || !(area(s) & 1) || state_of(s) != ST_STANDING) return;
        m->probe = 1; m->probe_t = 0; q_probes++;
    }
    if (m->probe == 1) {                             // (out)
        if (!on_floor(s)) {
            if (++m->probe_t > 8 * LJ_SECOND) { m->probe = 0; return; }
            if (!(area(s) & 1)) return;
            float nx, nz, d = way_out(wx(s), wz(s), &nx, &nz);
            RD16(ai + 0x0C) = dpad_to(nx, nz);
            RD16(ai + 0x0E) = d < 40.0f ? B_CUP : 0;
            return;
        }
        if (!(qa_lj_esc && (q_probes & 1))) { m->probe = 0; return; }
        m->probe = 2; m->probe_t = 0; q_esc_probes++;
    }
    // (back in by himself: 3 s out, then to the nearest side and up -- C-up held till he is in)
    if (++m->probe_t > 20 * LJ_SECOND || (area(s) & 1) || m->roll) { m->probe = 0; return; }
    if (m->probe_t < 3 * LJ_SECOND) return;
    float x = wx(s), z = wz(s);
    u16 held = fabs_f(x) >= fabs_f(z) ? (x > 0.0f ? 0x0200 : 0x0100) : (z > 0.0f ? 0x0400 : 0x0800);
    RD16(ai + 0x0C) = (u16)(held | B_CUP);
    RD16(ai + 0x0E) = (m->probe_t & 7) == 0 ? B_CUP : 0;
}
// ---- ROUND 7 (ljrole, 2026-11-23): the lumberjack, a ROLE of the game's own computer player -------------------------
// The user, 2026-11-23, after rounds 6 / 6b patched the steering's boundaries one by one: "What if you integrate it into the
// CPU AI, their role.. but only in Lumberjack matches so it doesn't leak out elsewhere", and: "Perhaps look at special
// referee mode? ... Also look wrestlers who interfere in matches. At a certain point they go outside the ring and don't
// return, but stay watching the match. Managers do that too."
// What the game has (lifted.cpp, read): its computer player's step (run_cpu_step, func_800F03F0) runs the slot's PLAN from
// the plan table 0x80145A10; with no plan (0) it chooses one -- ai_plan_0_choose -> ai_choose_by_place (func_800F0EF0). Two
// roles outside the competitors live in that choice: a man with +0xE9 bit 1 and no run-in under way (+0xF1 bit 1 clear) --
// the run-in man after his run-in -- and a man with +0xE9 bit 0 (placed outside: a manager, a valet) go to
// ai_choose_for_second_man (func_800FD828): on the floor up the aisle the walk down to ringside (plan 71, until z 530),
// by the ring the walk to a corner side by quadrant (plan 72), else NO plan: he stands, turned to his man (+0x08 set to his
// +0x0C), watching. The manager also fights back once punished enough (the AI +0x5E count, +0x80 bits 4 / 8) while his man
// is within 300, then stands off again. The guest referee has no computer role: no plan counts a pin (STATE_guestref.md).
// A lumberjack (+0xE9 8: neither bit) went to the competitors' choosers, which mostly sent him back to the ring for the
// count (plan 33, ai_try_back_to_ring) -- rounds 2-6 held that off by writing his buttons every step.
// Now the role decides WHAT and WHEN at that same point -- when his plan ends (0) or his job changes -- by giving his
// computer player one of the game's own plans; the game carries it out (walking, turning, facing, animation, collision):
//   watch at his post       plan 55 (wait, no buttons), his man the lumberjack across the ring (the second's watch);
//   walk back to his post   plan 60's walk (its step 1 holds +0x10 for +0x18 steps: the D-pad set once a leg), round the
//                           ring by the walkway's corners; arrived: the watch;
//   go to a man outside     plan 48 (the game's walk up to the target), round the ring by a walk leg when the way crosses it;
//   beat him                the game's own attack choice (ai_choose_by_target_situation, func_800F1EA4), its plans into the
//                           ring, onto the apron or the ropes put aside;
//   grab him                plan 1, from the far side of him from the ring (his roller walks there first);
//   pull him up             plan 19, when he lies;
//   throw him in            plan 103 (in the grapple: turned to the ring, walked to the apron, C-up) -- only with the roller
//                           facing the ring: with his back to it the game's throw lands the man OUTSIDE at ring height (the
//                           p1 float of 2026-11-23 16:22: held at (33,0,-341), thrown to (30,90,-588)); else a grapple
//                           move (plan 26) puts him down, and up again from the right side;
//   from behind             R, the pick-up button: the tie-up's break-off turns him to face the roller (the user's way) --
//                           the role's one button, pressed only while a roller holds his man from behind;
//   brawl with a rival      as the beating, at the rival;
//   out of the ring         plan 36 (the game's leave-the-ring), at a lumberjack outside;
//   the intro               plan 71 (the run-in's walk down the aisle), plan 55 to stand a step.
// THE GATE: lumberjack_cpu runs only from slots.c match_slots_cpu with a Lumberjack match's slots live (ms.job
// MS_JOB_LUMBERJACK) and this match set up as one (lj_live), and the role only for its lumberjacks (match_slots_is_extra,
// slot >= the competitors). Competitors and the player: the stock computer player (the one write kept: a computer-played
// competitor in the ring whose man the game made a lumberjack gets his opponent back -- once, when it happens).
// No per-step write of a lumberjack's D-pad, stick, facing or target: the target is set when a job is decided, the place
// and facing once at the intro's end (each put at his post out of sight); R while a roller holds from behind.
#define AI_PLAN         0x00u    // s32: the plan run (the plan table 0x80145A10)
#define AI_STEP         0x04u    // s32: its step
#define AI_SUB          0x08u    // s32: a second counter (ai_set_plan clears it)
#define AI_HELD         0x0Cu    // u16: buttons held this step
#define AI_PRESSED      0x0Eu    // u16: pressed this step
#define AI_BUILT        0x10u    // u16: a button word a plan builds and holds (plan 60: its walk's D-pad)
#define AI_COUNT        0x18u    // s32: a plan's step count (plan 60: the walk's steps left)
#define AI_TGT_SIT      0x48u    // s32: the target's situation code this step (ai_situation_code: 15 lying)
#define AI_TSTATE       0x56u    // s16: the target's state when the plan was set (ai_set_plan)
#define AI_FLAGS        0x80u    // u8: low bits 1 -- a new plan (ai_set_plan)
#define LJ_PLAN_SAME    0x8018A130u   // u16 a slot: run_cpu_step's steps with one plan (at 300 it clears it to 0 -- reset as a plan is given)
#define P_GRAB          1        // ai_plan_1_grapple
#define P_STRIKE        6        // ai_plan_6_strike
#define P_PULL_UP       19       // ai_plan_19_pull_up_lying_target (R by him)
#define P_MOVE          26       // ai_plan_26_grapple_move_input
#define P_LEAVE_RING    36       // ai_plan_36_leave_ring
#define P_APPROACH      48       // ai_plan_48_walk_up_to_target
#define P_WAIT          55       // ai_plan_55_wait_for_target: no buttons, up to 150 steps
#define P_WALK          60       // ai_plan_60_walk_back_along_apron: its step 1 holds +0x10 for +0x18 steps
#define P_WALK_DOWN     71       // ai_plan_71_walk_down_until_z: the D-pad's down (-z) held -- the run-in's walk to ringside
#define P_THROW         103      // ai_plan_103_whip_with_stick
#define LJ_PACE0        3.0f     // a walk's pace a step before his own is measured (world units)
#define LJ_LEG_MIN      12       // a walk leg's steps, at least (a turn back within 1/3 s is a twitch: never sooner)
#define LJ_LEG_MAX      45       // ... and at most (then the way is looked at again)
#define LJ_LEG_STUCK    10       // a leg this many steps old with under a third of his pace's headway: ended
#define LJ_LEG_REST     30       // ... and he stands this long before the next
#define LJ_HOME_NEAR    40.0f    // arrived at his post (walk legs end within a step or two of it)
#define LJ_FIGHT_NEAR   130.0f   // a man this near: the game's own attack choice; farther, the walk up to him
#define LJ_STOP_SHORT   55.0f    // a walk leg to a man ends this short of him (the game's walk up stops at 120: too far to grab)
#define LJ_THROW_FACE   0x2AA    // the throw's C-up: the roller facing at least this far off the ring's middle (60 degrees: out or
                                 //  across) --
#define LJ_THROW_OUT    10.0f    //  and no farther out than his man and this: the game's throw (pair 15 / 16) throws the man OVER
                                 //  the thrower (ljtwitch3's 17 good throws: the roller at the apron edge, 308-335 out, the man as
                                 //  far out or farther, the roller facing out or across; the 2026-11-23 16:22 float: the roller
                                 //  outside his man, facing the ring -- the man thrown outward, at ring height)
GAMEFN(void, func_800F1EA4, 0x800F1EA4u, (void));   // ai_choose_by_target_situation: the game's general attack choice
static int can_act(s32 st) { return st == ST_STANDING || st == ST_WALKING; }
static float dist2(int a, int b) { float dx = wx(a) - wx(b), dz = wz(a) - wz(b); return sqrt_f(dx * dx + dz * dz); }
// his computer player's plan, set as ai_set_plan sets one (the plan, its step and counter cleared, the target's state now,
// the new-plan bits), and run_cpu_step's same-plan count cleared (a plan given anew is a new plan)
static void give_plan(int s, u32 ai, s32 plan) {
    RDS32(ai + AI_PLAN) = plan; RDS32(ai + AI_STEP) = 0; RDS32(ai + AI_SUB) = 0;
    s32 tg = RDS32(rec(s) + LJ_TARGET);
    RDS16(ai + AI_TSTATE) = (s16)(tg >= 0 && tg < match_slots_count() ? state_of(tg) : 0);
    RD8(ai + AI_FLAGS) = (u8)((RD8(ai + AI_FLAGS) & 0xFC) | 1);
    RD16(LJ_PLAN_SAME + 2u * (u32)s) = 0;
    lj[s].given = plan; lj[s].leg_on = 0; lj[s].wr |= LJW_PLAN; q_plans++;
    if (plan == P_THROW) lj[s].hdec |= 1;          // (ljround8, QA: this hold went to the throw in)
}
// his man: set when a job is decided (only on his feet -- in a hold or a move +0x08 is his pair's other man, lumberjack4)
static void set_target(int s, int t) {
    if (t < 0 || t == s || t >= match_slots_count() || RDS32(rec(s) + LJ_TARGET) == t) return;
    RDS32(rec(s) + LJ_TARGET) = t; lj[s].wr |= LJW_TGT_CPU;
}
// a walk leg: plan 60 at its walking step -- the D-pad toward (tx, tz), one of the eight ways, held for as many steps as
// take him the way's length along it at his pace (LJ_LEG_MIN .. LJ_LEG_MAX); at its end the way is looked at again
static void give_leg_n(int s, u32 ai, float tx, float tz, int most) {
    Lumberjack* L = &lj[s];
    float x = wx(s), z = wz(s), dx = tx - x, dz = tz - z;
    u16 dp = dpad_to(dx, dz);
    if (!dp) { give_plan(s, ai, P_WAIT); return; }
    float ux = (dp & 0x0100) ? 1.0f : (dp & 0x0200) ? -1.0f : 0.0f, uz = (dp & 0x0800) ? 1.0f : (dp & 0x0400) ? -1.0f : 0.0f;
    if (ux != 0.0f && uz != 0.0f) { ux *= 0.7071f; uz *= 0.7071f; }
    float pace = L->pace > 0.5f ? L->pace : LJ_PACE0;
    int steps = (int)((dx * ux + dz * uz) / pace);
    if (steps < LJ_LEG_MIN) steps = LJ_LEG_MIN; else if (steps > most) steps = most;
    give_plan(s, ai, P_WALK);
    RDS32(ai + AI_STEP) = 1; RD16(ai + AI_BUILT) = dp; RDS32(ai + AI_COUNT) = steps;
    L->leg_on = 1; L->leg_n = (u16)steps; L->leg_t = (u32)match_slots_steps(); L->leg_x = x; L->leg_z = z;
    L->last_dpad = dp; q_legs++;
}
static void give_leg(int s, u32 ai, float tx, float tz) { give_leg_n(s, ai, tx, tz, LJ_LEG_MAX); }
#define LJ_CHASE_MAX    30       // a walk leg after a man: at most this long (r4: legs of up to 45 steps went on the way the man
                                 //  had been, past him or into him; r6: at 15 the turns of the walk each re-decision -- the swarm 3/s)
// a leg over (his plan no longer the walk): his pace measured; little headway is a walk blocked (counted toward giving up
// the way home -- ljtwitch's LJ_BLOCK_GIVE)
#define LJ_BLOCK_GIVE   3
static void leg_over(int s) {
    Lumberjack* L = &lj[s];
    L->leg_on = 0;
    u32 t = (u32)match_slots_steps() - L->leg_t;
    if (t < 6 || t > 200) return;
    float moved = sqrt_f((wx(s) - L->leg_x) * (wx(s) - L->leg_x) + (wz(s) - L->leg_z) * (wz(s) - L->leg_z)), pace = moved / (float)t;
    float was = L->pace > 0.5f ? L->pace : LJ_PACE0;
    if (pace < 0.3f * was) { q_blocked++; if (L->blocks < 255) L->blocks++; if (L->mode == LJ_POST) L->direct = 1; return; }
    L->blocks = 0;
    if (pace > 1.0f && pace < 8.0f) L->pace = 0.5f * was + 0.5f * pace;
}
// the next point of a walk to (tx, tz) that is not a post: there, or by the walkway's corner that clears the ring
static void round_ring(int s, float x, float z, float tx, float tz, float* nx, float* nz) {
    *nx = tx; *nz = tz;
    if (!crosses_ring(x, z, tx, tz)) return;
    Lumberjack* L = &lj[s];
    float best = 1e9f;
    for (int k = 0; k < 4; k++) {
        float qx = (k & 1) ? LJ_CORNER : -LJ_CORNER, qz = (k & 2) ? LJ_CORNER : -LJ_CORNER, dq = fabs_f(qx - x) + fabs_f(qz - z);
        if (dq < 40.0f) { L->corner = (u8)(k + 1); continue; }
        if (crosses_ring(x, z, qx, qz)) continue;
        float d = dq + fabs_f(tx - qx) + fabs_f(tz - qz);
        if (d < best) { best = d; *nx = qx; *nz = qz; }
    }
}
// the man a lumberjack at his post watches: the lumberjack across the ring while he stands at his post, else another who
// does (lumberjack4 / ljtwitch: the game turns a standing man to his target, so the man watched must stand still)
static int watch_target(int s, int c) {
    Lumberjack* L = &lj[s];
    if (!still_at_post(L->across)) {
        int best = -1; float bd = 1e9f;
        for (int o = c; o < match_slots_count(); o++) {
            if (o == s || !still_at_post(o)) continue;
            float d = fabs_f(lj[o].px + L->px) + fabs_f(lj[o].pz + L->pz);
            if (d < bd) { bd = d; best = o; }
        }
        if (best >= 0) L->across = (u8)best;
    }
    return L->across != s ? L->across : -1;
}
// the plans a lumberjack's fight on the floor never takes from the game's choice: into the ring or onto the apron, off the
// ropes or the turnbuckle, the weapon hunt, the second's walks, the cage and ladder -- the role's beating is on the floor
static int put_aside(s32 p) {
    switch (p) {
    case 4: case 5: case 33: case 34: case 35: case 36: case 51: case 57: case 58: case 60: case 62: case 63: case 64:
    case 65: case 66: case 67: case 71: case 72: case 87: case 88: case 89: case 90: case 91: case 92: case 101:
    case 102: case 103: case 104: case 113: case 116: case 118: return 1;
    case 32: case 48: return 1;   // (the wait-and-watch and the walk up, in a fight on the floor: a walk leg or a strike instead --
                                  //  r1's trace, the swarm's most jittery plans: standing turned to a moving man, the walk up's
                                  //  eight-way D-pad toward him every step)
    default: return p >= 121;
    }
}
static void go_to(int s, u32 ai, int k);
static int mate_in_way(int s, int k);
// (ljround8, the user 2026-11-23: "They don't attack enough ... They should be more aggressive in other words") the game's
// own choices that are no attack -- guarding, backing off, sidestepping, taunting, waiting, walking about, running off: on
// a competitor out there the lumberjack attacks instead
static int passive_plan(s32 p) {
    switch (p) {
    case 7: case 21: case 27: case 28: case 32: case 38: case 39: case 40: case 41: case 42: case 44: case 45: case 46: case 47:
    case 48: case 49: case 50: case 55: case 69: case 86: case 93: case 95: case 113: case 114: case 131: case 137: return 1;
    default: return 0;
    }
}
// the plans that take hold of the man (a grab, a grab of a runner, a timed grab, the grab and follow-up)
static int grab_plan(s32 p) { return p == P_GRAB || p == 2 || p == 16 || p == 17 || p == 18 || p == 43; }
// (ljround9, the user 2026-11-24: "all I wanted was just regular back grapple functionality ... if they're fighting a player
// on the outside, they might want to back grapple. That's just regular game behavior that needs to stay"; "CPUs can do
// whatever moves they want") round 8's gates on where and how a lumberjack may grab or do a move -- only from in front of the
// man, no grab or move with a man's place bits stale, grapple moves only 370-450 out and not on the aisle's side -- are gone:
// he grabs, from the front or behind, and does the game's own moves, anywhere, as any computer player. Why a move by the
// barricade, the aisle or the apron's edge left a man at ring height: movex.c keep_place (ROUND 9) -- an imported move's
// animation that ends on the mat, in any match.
// an attack on a competitor near him, the role's own: lying -- the game's strike at a man down (22); on his feet, now and
// then a grab, from wherever he stands (the hold's throw or grapple move follows: role_grapple), else a strike
// (ljround10) a man a blow can land on: on his feet, lying, coming up -- reeling from a blow (45), in a move, held: not
// (r8's trace: three quarters of the lumberjacks' strikes begun on a man out there were on a man reeling or in a move, and
// one in ten of those landed; swung round a reeling man, they landed on each other -- 24-42 lumberjack-on-lumberjack hits a match)
// (r10: coming up off the mat, 50, he can't be hit either -- 2 of 54 blows begun on a man lying or rising landed)
static int hittable(int k) { s32 st = state_of(k); return free_state(st) || st == 49; }
// (ljround10) his own man not to be had this moment, or a walk under way: another competitor out there in reach whom a blow
// can land on gets one meanwhile -- his target his for the blow, back to his own man once it is over (r11's trace: a
// swarm lumberjack waiting by his man reeling, the other competitor standing 58-79 from him; a roller's walk leg past him)
// (ljround10) a blow at another competitor (strike_other) still under way: his plan the strike, his target not his own man --
// left to the game while he swings or closes; standing there 20 steps with it (r14's stage_raw trace: a roller 47 from the
// other man, plan 6, on his feet 4 s -- the plan never swung) it is decided again
static int other_blow_on(int s, s32 plan, int fresh, int k) {
    Lumberjack* L = &lj[s];
    if (fresh || RDS32(rec(s) + LJ_TARGET) == k || (plan != P_STRIKE && plan != 22) || (L->given != P_STRIKE && L->given != 22)) { L->ostand = 0; return 0; }
    if (!free_state(state_of(s))) { L->ostand = 0; return 1; }
    if (++L->ostand < 20) return 1;
    L->ostand = 0; return 0;
}
static int strike_other(int s, u32 ai, int k) {
    int c = match_slots_competitors(); if (c > 4) c = 4;
    for (int j = 0; j < c; j++) {
        if (j == k || !present(j) || !on_floor(j) || !hittable(j) || lc[j].roll == 2 || dist2(s, j) > LJ_STRIKE_NEAR || mate_in_way(s, j)) continue;
        set_target(s, j); give_plan(s, ai, state_of(j) == 49 ? 22 : P_STRIKE); q_struck_other++;
        return 1;
    }
    return 0;
}
// (ljround10) the backstop's blow: his own man first, if a blow can land on him now, then another in reach
static int strike_any(int s, u32 ai, int k) {
    if (k >= 0 && k < match_slots_competitors() && k < 4 && present(k) && on_floor(k) && hittable(k) && lc[k].roll != 2 &&
        dist2(s, k) <= LJ_STRIKE_NEAR && !mate_in_way(s, k)) {
        set_target(s, k); give_plan(s, ai, state_of(k) == 49 ? 22 : P_STRIKE); return 1;
    }
    return strike_other(s, ai, k);
}
static void attack_now(int s, u32 ai, float d) {
    int k = lj[s].prey;
    if (d > LJ_STRIKE_NEAR) { go_to(s, ai, k); return; }
    if (k < match_slots_competitors() && !hittable(k)) { if (strike_other(s, ai, k)) return; q_held_blow++; give_plan(s, ai, P_WAIT); return; }   // (ljround10: the next blow once he can take it)
    if (mate_in_way(s, k)) { q_mate_way++; go_to(s, ai, k); return; }   // (ljround10: a fellow lumberjack in the way: his side first)
    if (RDS32(ai + AI_TGT_SIT) == 15) { give_plan(s, ai, 22); return; }
    if (free_state(state_of(k)) && d <= LJ_GRAB_NEAR + 10.0f && (rnd() % 100u) < 40u) { give_plan(s, ai, P_GRAB); return; }
    give_plan(s, ai, P_STRIKE);
}
// the game's own attack choice for his man (near), asked as ai_plan_0_choose would ask: what it picks is his plan from the
// next step (this step's buttons are left as they were: the choice is not acted on twice)
static void choose_attack(int s, u32 ai, float d) {
    if (lj[s].prey < match_slots_competitors() && !hittable(lj[s].prey)) { if (strike_other(s, ai, lj[s].prey)) return; q_held_blow++; give_plan(s, ai, P_WAIT); return; }   // (ljround10)
    if (lj[s].prey < match_slots_competitors() && mate_in_way(s, lj[s].prey)) { q_mate_way++; go_to(s, ai, lj[s].prey); return; }   // (ljround10)
    u16 h = RD16(ai + AI_HELD), p = RD16(ai + AI_PRESSED); u8 sx = RD8(ai + LJ_AI_STICK_X), sy = RD8(ai + LJ_AI_STICK_Y);
    RDS32(ai + AI_PLAN) = 0;
    func_800F1EA4(); q_chooser++;
    RD16(ai + AI_HELD) = h; RD16(ai + AI_PRESSED) = p; RD8(ai + LJ_AI_STICK_X) = sx; RD8(ai + LJ_AI_STICK_Y) = sy;
    s32 plan = RDS32(ai + AI_PLAN);
    int comp = lj[s].prey < match_slots_competitors();
    // (ljround8) on a competitor: no standing off -- the role's attack instead of the game's wait, guard, taunt or walk
    if (comp && (plan == 0 || put_aside(plan) || passive_plan(plan))) { if (plan) q_passive_vetoed++; RDS32(ai + AI_PLAN) = 0; attack_now(s, ai, d); return; }
    if (plan == 0 || put_aside(plan)) { if (plan) q_vetoed++; if (d > 100.0f) go_to(s, ai, lj[s].prey); else give_plan(s, ai, P_STRIKE); return; }
    lj[s].given = plan; lj[s].leg_on = 0; lj[s].wr |= LJW_PLAN; q_plans++;
    RD16(LJ_PLAN_SAME + 2u * (u32)s) = 0;
}
// to a man on the floor: a walk leg toward him, ending LJ_STOP_SHORT short of him, or round the ring when the way crosses it
// (r1: the game's walk up (plan 48) holds its eight-way D-pad toward him every step -- a zigzag, 3 jitter events a second)
// (ljround10) the lumberjacks on one competitor out there come at him from different sides: his place among them (by slot)
// picks his side -- the first from straight out of the ring, the next two a quarter-turn and more either way, a fourth from
// the ring's side when there is floor there. r2's trace (lumberjack_p1, lockstep): coming all from outside, in a file,
// their strikes landed on each other -- 50 lumberjack-on-lumberjack hits in a match against 23 on the man, and 31-36% of
// their steps on a man spent down or hit themselves
static int mate_rank(int s, int k) {
    int r = 0, n = match_slots_count(), c = match_slots_competitors();
    for (int t = c; t < n && t < s; t++) if ((lj[t].mode == LJ_SWARM || lj[t].mode == LJ_ROLL) && lj[t].prey == k && present(t)) r++;
    return r;
}
static void side_spot(int s, int k, float* nx, float* nz) {
    float mx = wx(k), mz = wz(k), ox, oz;
    float ax = fabs_f(mx), az = fabs_f(mz);                 // (straight out of the ring from him: the nearer side's normal)
    if (ax >= az) { ox = mx >= 0.0f ? 1.0f : -1.0f; oz = 0.0f; } else { ox = 0.0f; oz = mz >= 0.0f ? 1.0f : -1.0f; }
    static const float cs[4] = { 1.0f, -0.17f, -0.17f, -1.0f }, sn[4] = { 0.0f, 0.985f, -0.985f, 0.0f };   // (0, +-100, 180 degrees)
    int r = mate_rank(s, k) & 3;
    if (r == 3 && ring_d(k) < LJ_APRON + LJ_STOP_SHORT + 20.0f) r = (s & 1) ? 1 : 2;   // (no floor on the ring's side)
    float dx = ox * cs[r] - oz * sn[r], dz = ox * sn[r] + oz * cs[r];
    *nx = mx + dx * LJ_STOP_SHORT; *nz = mz + dz * LJ_STOP_SHORT;
}
// (ljround10) a fellow lumberjack between him and his man, in the way of his strike: within 45 of the line to the man and
// nearer than the man
static int mate_in_way(int s, int k) {
    float sx = wx(s), sz = wz(s), dx = wx(k) - sx, dz = wz(k) - sz, d = sqrt_f(dx * dx + dz * dz);
    if (d < 1.0f) return 0;
    int n = match_slots_count(), c = match_slots_competitors();
    for (int t = c; t < n; t++) {
        if (t == s || !present(t) || !match_slots_is_extra(t)) continue;
        float tx = wx(t) - sx, tz = wz(t) - sz, along = (tx * dx + tz * dz) / d;
        if (along <= 0.0f || along >= d) continue;
        float off = fabs_f(tx * dz - tz * dx) / d;
        if (off < 45.0f) return 1;
    }
    return 0;
}
static void go_to(int s, u32 ai, int k) {
    float x = wx(s), z = wz(s), nx, nz, mx = wx(k), mz = wz(k);
    round_ring(s, x, z, mx, mz, &nx, &nz);
    if (nx == mx && nz == mz) {
        if (k < match_slots_competitors() && (lj[s].mode == LJ_SWARM || lj[s].mode == LJ_ROLL) && on_floor(k)) side_spot(s, k, &nx, &nz);
        else {
            float dx = mx - x, dz = mz - z, d = sqrt_f(dx * dx + dz * dz);
            if (d > LJ_STOP_SHORT + 1.0f) { nx = mx - dx * LJ_STOP_SHORT / d; nz = mz - dz * LJ_STOP_SHORT / d; }
        }
    }
    give_leg_n(s, ai, nx, nz, LJ_CHASE_MAX);
}
// his facing off a way (0..2048)

// at his post or on his way back to it
static void role_post(int s, u32 ai, s32 plan, int fresh, int c) {
    Lumberjack* L = &lj[s];
    float x = wx(s), z = wz(s), d = fabs_f(L->px - x) + fabs_f(L->pz - z);
    if (L->home) { if (d > LJ_HOME_LEAVE) L->home = 0; }
    else if (d <= LJ_HOME_NEAR) { L->home = 1; L->direct = 0; L->corner = 0; L->blocks = 0; }
    if (L->home || L->blocks >= LJ_BLOCK_GIVE) {     // (there -- or his way home blocked again and again: he stays where he is)
        int a = watch_target(s, c);
        int changed = a >= 0 && RDS32(rec(s) + LJ_TARGET) != a;
        if (changed) set_target(s, a);
        if (fresh || changed || plan != P_WAIT) give_plan(s, ai, P_WAIT);
        L->faced = (u8)L->home;
        return;
    }
    L->faced = 0;
    if (!fresh && plan == P_WALK && L->given == P_WALK) return;   // (a leg on its way)
    if (L->bwait && plan == P_WAIT && !fresh) return;               // (a moment after a walk that made no headway)
    // (his man on his way home: the still lumberjack he will watch -- a stick walk faces the man, and with the man he had
    //  been after, moving, his face and his walk files swung each step: r7's lumberjack_twitch5 match 1, 4.7 events a second)
    { int a = watch_target(s, c); if (a >= 0) set_target(s, a); }
    float nx, nz; next_point(s, x, z, L->px, L->pz, &nx, &nz);
    give_leg(s, ai, nx, nz);
}
// the swarm's man, or a brawl's rival: to him, then the game's own fight
static void role_fight(int s, u32 ai, s32 plan, int fresh) {
    Lumberjack* L = &lj[s];
    int k = L->prey;
    if (k >= match_slots_count() || !present(k)) return;
    float d = dist2(s, k);
    if (other_blow_on(s, plan, fresh, k)) return;   // (a blow at another, under way)
    if (fresh || RDS32(rec(s) + LJ_TARGET) != k) {
        // (ljround10: his man again -- the game makes whoever hit him his target, a fellow lumberjack's stray blow too -- and
        //  at him at once when he is in reach, not a walk leg first: r3's trace, 30-119 lumberjack-on-lumberjack hits a match)
        int was_mate = !fresh;
        set_target(s, k);
        if (was_mate && k < match_slots_competitors() && d <= LJ_STRIKE_NEAR && !mate_in_way(s, k) && !crosses_ring(wx(s), wz(s), wx(k), wz(k))) { choose_attack(s, ai, d); return; }
        go_to(s, ai, k); return;
    }
    // (ljround10) arrived: a walk leg or the rest after one that brought him within reach of a competitor out there, his way
    // clear -- the attack now, not the rest of the leg (r2's trace: 19-20% of the lumberjacks' steps on a man on walk legs,
    // 6-10% waiting, 8-9% on their feet within 85 of him with no attack under way)
    int reach = k < match_slots_competitors() && d <= LJ_STRIKE_NEAR && !mate_in_way(s, k) && !crosses_ring(wx(s), wz(s), wx(k), wz(k));
    if (plan == P_WALK && L->given == P_WALK && !reach) { if (k < match_slots_competitors()) strike_other(s, ai, k); return; }  // (a leg round the ring, or up to him)
    if (L->bwait && plan == P_WAIT && !reach) return;            // (a moment after a walk that made no headway)
    if (plan == P_WAIT && k >= match_slots_competitors()) return; // (a brawl's wait: the game's)
    if (plan != 0 && plan != P_WALK && plan != P_WAIT && !put_aside(plan)) return; // (the game's fight goes on)
    if (d > LJ_FIGHT_NEAR || crosses_ring(wx(s), wz(s), wx(k), wz(k))) { go_to(s, ai, k); return; }
    choose_attack(s, ai, d);
}
// the roller (lumberjack4-6's roll-in: WHEN is lumberjack_step's): to his man (a walk leg, round the ring if need be), then
// the game's grab (plan 1); lying, the game's pull up (plan 19); else he waits by him (plan 55). The hold and the throw are
// role_grapple's: the game's throw-in walks the pair to the apron itself (plan 103), so no side of the man is chosen here
// (r1 sent the roller round to the far side of his man first: the geometry of the float, and the dance round him)
static void role_roll(int s, u32 ai, s32 plan, int fresh) {
    Lumberjack* L = &lj[s];
    int k = L->prey;
    if (k >= 4 || !present(k)) return;
    Competitor* m = &lc[k];
    if (other_blow_on(s, plan, fresh, k)) return;   // (a blow at another, under way)
    if (fresh || RDS32(rec(s) + LJ_TARGET) != k) { set_target(s, k); fresh = 1; }
    if (m->roll != 1) {                                             // (his man sent in by himself: he stands by --
        // (ljround10: in reach, at him all the same -- the user's "never idle in striking distance"; r6: the roller standing by a
        //  man stuck walking up the aisle, his walk in (roll 3) under way)
        if (dist2(s, k) <= LJ_STRIKE_NEAR && !mate_in_way(s, k) && hittable(k)) {
            if (plan != P_STRIKE || L->given != P_STRIKE) give_plan(s, ai, state_of(k) == 49 ? 22 : P_STRIKE);
            return;
        }
        if (plan != P_WAIT) give_plan(s, ai, P_WAIT); return;
    }
    if (!fresh && plan == P_WALK && L->given == P_WALK) { strike_other(s, ai, k); return; }   // (a leg on its way -- a blow at another in reach)
    if (L->bwait && plan == P_WAIT && !fresh) {                     // (a moment after a walk that made no headway --
        // ljround10, r14's SmackDown stage trace: the roller 79-84 from his man on the stage, his walk to him making no headway,
        // waiting there 1 s and more: in reach, a blow at him meanwhile; else at another in reach)
        if (dist2(s, k) <= LJ_STRIKE_NEAR && hittable(k) && !mate_in_way(s, k)) give_plan(s, ai, state_of(k) == 49 ? 22 : P_STRIKE);
        else strike_other(s, ai, k);
        return;
    }
    if (!fresh && (plan == P_GRAB || plan == P_PULL_UP) && plan == L->given) return;   // (the game at it)
    float d = dist2(s, k);
    if (d > LJ_ROLL_NEAR || crosses_ring(wx(s), wz(s), wx(k), wz(k))) { go_to(s, ai, k); return; }
    if (free_state(state_of(k))) {
        if (d <= LJ_GRAB_NEAR) {
            // (ljround9: the game's grab from wherever he stands -- from behind it is the game's rear grapple, as any
            //  computer player's; role_grapple then decides the hold: the throw, turned to the front first, or a move)
            give_plan(s, ai, P_GRAB); if (m->threw < 255) m->threw++; return;
        }
        go_to(s, ai, k); return;                                   // (up to him)
    }
    if (RDS32(ai + AI_TGT_SIT) == 15) { give_plan(s, ai, P_PULL_UP); return; }   // (lying: the game's pull up -- R by him)
    // (rising, reeling: ljround10 -- at him, not waiting by him: r6's trace, the roller standing (plan 55) within 46-72 of a man
    //  reeling or rising, the user's "never idle in striking distance". A strike while he can't be grabbed; the grab once he is up)
    if (d <= LJ_STRIKE_NEAR && !mate_in_way(s, k) && hittable(k)) { if (plan != P_STRIKE || fresh || L->given != P_STRIKE) give_plan(s, ai, P_STRIKE); return; }
    if (strike_other(s, ai, k)) return;   // (ljround10, r8: a roller standing by his man, the other man lying 63 from him)
    if (plan != P_WAIT || fresh) give_plan(s, ai, P_WAIT);           // (reeling, in a move, a mate in the way: he waits by him)
}
// his distance out from the ring, as the apron sees it (the larger of |x|, |z|)
// holding a man in a grapple (state 81)
#define LJ_THROW_PCT    60                // (ljround8, the user: "They should try to throw them into the ring 60% of the time, but
                                          //  about 40% of the time should try to do a grapple move to cause damage")
static void role_grapple(int s, u32 ai, s32 plan) {
    Lumberjack* L = &lj[s];
    int c = match_slots_competitors(); if (c > 4) c = 4;
    // (ljround8: the man held is his target -- in a hold +0x08 is the pair's other man -- not always the one the job was for:
    //  r26's trace, a swarm lumberjack after competitor 0 grabbed competitor 2 (the tornado tag's other CPU man, who had come
    //  for him), and none of the hold's rules saw it: the game's own move ran, by the ramp's foot (the float). The job takes
    //  the man held)
    {   s32 tg = RDS32(rec(s) + LJ_TARGET);
        if ((L->mode == LJ_SWARM || L->mode == LJ_ROLL) && tg >= 0 && tg < c && tg != L->prey && state_of(tg) == ST_GRAPPLED && dist1(s, tg) < 120.0f) {
            if (L->mode == LJ_ROLL) L->mode = LJ_SWARM;          // (a roller of another man: the roll is not this hold's)
            L->prey = (u8)tg; L->seen = (u16)(L->mode << 8 | tg);
        }
    }
    int k = L->prey;
    // (ROUND 9) the throw decided for a hold from behind: the turn first -- R over whatever the game's plan pressed (r6's
    //  trace: a swarm lumberjack's hold from behind, the throw decided, and the game's own plan 112 pressing C-up, the
    //  shoulder lift, the same step). The roll goes on as below once he holds him from the front
    //  (r7: not by the distance -- the hold's man is his target, +0x08; the pair slides together over the hold's first steps
    //  and turns round each other in R's turn, 120 apart and more: the same plan 112 pressed C-up then)
    s32 hk = RDS32(rec(s) + LJ_TARGET);
    if ((L->hdec & 2) && (RDS32(rec(s) + LJ_GRAB_FLAGS) & LJ_GRAB_BEHIND) && hk >= 0 && hk < c) {
        if (state_of(hk) == ST_GRAPPLED && !(L->mode == LJ_ROLL && lc[hk].roll == 2 && lc[hk].roller == s)) {   // (his roll, as below)
            Competitor* m = &lc[hk]; int n = match_slots_count();
            for (int t = c; t < n; t++) if (t != s && lj[t].mode == LJ_ROLL && lj[t].prey == hk) lj[t].mode = LJ_POST;
            L->mode = LJ_ROLL; L->prey = (u8)hk; L->seen = (u16)(LJ_ROLL << 8 | hk);
            if (m->roll == 0) q_sends++;
            if (m->roll != 2) { m->roll = 2; m->rt = 0; m->threw = 0; q_grabbed++; }
            m->roller = (u8)s; m->roller2 = 0xFF; m->tdec = 1;
        }
        RD16(ai + AI_HELD) = B_R;
        RD16(ai + AI_PRESSED) = (match_slots_steps() & 3) == 0 || !(L->hdec & 8) ? B_R : 0;
        L->hdec |= 8; L->wr |= LJW_BUTTONS; q_r_steps++;
        return;
    }
    // (ROUND 9) any lumberjack on a man (the swarm's, the roller's) holding a competitor -- from the front or from behind, as
    // the game's grab made it -- decides once a hold, at its start: the throw in (LJ_THROW_PCT) or the game's own grapple move
    // (plan 26, the game's choice: from behind its rear grapple moves). The throw makes him the man's roller (the roll's hold:
    // lumberjack_step waits it out and sees him in); held from behind, the throw turns him first (R, below: the user's rear ->
    // R -> front), and stays decided for this roll (tdec) through the turn and the grab again from the front. The move puts the
    // man down, and the next attempt takes him again. No place is ruled out: a move anywhere is the game's (round 8's move
    // zone and its let-go with R are gone, the user 2026-11-24).
    if ((L->mode == LJ_SWARM || L->mode == LJ_ROLL) && k < c && state_of(k) == ST_GRAPPLED && dist1(s, k) < 120.0f) {
        Competitor* m = &lc[k];
        if (!(L->hdec & 6)) {
            if (L->mode == LJ_ROLL && m->tdec && m->roll >= 1) L->hdec |= 2;   // (this roll's throw, decided before the turn)
            else {
                // (the split kept to it, not left to chance: a grapple move whenever the moves so far would stay within
                //  100 - LJ_THROW_PCT % with it -- throw, throw, move, throw, move ...; r17 / r18 drew it at random: 6 throws
                //  in 7 holds a match)
                int move = (lj_dec_m + 1u) * 100u <= (lj_dec_t + lj_dec_m + 1u) * (100u - LJ_THROW_PCT);
                if (move) { lj_dec_m++; q_dec_move++; } else { lj_dec_t++; q_dec_throw++; }
                L->hdec |= move ? 4 : 2;
            }
        }
        if ((L->hdec & 2) && !(L->mode == LJ_ROLL && m->roll == 2 && m->roller == s)) {
            int n = match_slots_count();
            for (int t = c; t < n; t++) if (t != s && lj[t].mode == LJ_ROLL && lj[t].prey == k) lj[t].mode = LJ_POST;
            L->mode = LJ_ROLL; L->prey = (u8)k; L->seen = (u16)(LJ_ROLL << 8 | k);
            if (m->roll == 0) q_sends++;
            if (m->roll != 2) { m->roll = 2; m->rt = 0; m->threw = 0; q_grabbed++; }
            m->roller = (u8)s; m->roller2 = 0xFF;
            if (!lj_engaged[s]) { lj_engaged[s] = 1; q_engaged++; }
        }
        if (L->hdec & 2) m->tdec = 1;
        if (L->hdec & 4) {                                      // (the grapple move: the game's own, the man down)
            if (plan == P_THROW || plan == 102) q_throw_vetoed++;
            if (plan != P_MOVE || L->given != P_MOVE) give_plan(s, ai, P_MOVE);
            return;
        }
    }
    int roller = L->mode == LJ_ROLL && k < 4 && lc[k].roll >= 1 && state_of(k) == ST_GRAPPLED && dist1(s, k) < 120.0f;
    if (!roller) {          // (a brawl lumberjack's grapple, or one from behind: the game's own move -- never its throw-in, the roller's)
        if (plan == P_THROW || plan == 102) { give_plan(s, ai, P_MOVE); q_throw_vetoed++; }
        return;
    }
    if (RDS32(rec(s) + LJ_GRAB_FLAGS) & LJ_GRAB_BEHIND) {
        // (lumberjack5, the user: "if they're in a back grapple, they need to flip the opponent to a front grapple first" --
        //  R, the pick-up button: the tie-up's break-off turns him round to face the roller and lets him go; grabbed again
        //  at once from the front. The role's one button: pressed only while he holds his man from behind, over what the
        //  grapple's own plan would press -- never C-up, the shoulder lift)
        RD16(ai + AI_HELD) = B_R;
        RD16(ai + AI_PRESSED) = (match_slots_steps() & 3) == 0 ? B_R : 0;
        L->wr |= LJW_BUTTONS; q_r_steps++;
        return;
    }
    Competitor* m = &lc[k];
    if (raised(k) || raised(s)) {
        // (ljround10, the user 2026-11-24: "On stage the lumberjack will try to grapple and bring the player to the ring. This
        //  is impossible as its too slow. When on ramp or on stage, they should rather try to irish whip them back to the ring
        //  area") the throw in, on the ramp or the stage, is the game's whip toward the ring: the stick toward the ring's middle
        //  and C-up in the hold -- what the game's own whip plan (103) presses once lined up, without its walk to the apron.
        //  The man runs off down toward the ring; the floor's roll-in takes over there
        if (plan != P_THROW || L->given != P_THROW) { give_plan(s, ai, P_THROW); if (m->threw < 255) m->threw++; if (m->threw == 1) q_throws++; }
        RD16(ai + AI_HELD) = (u16)(dpad_to(-wx(k), -wz(k)) | B_CUP);
        RD16(ai + AI_PRESSED) = (match_slots_steps() & 3) == 0 ? B_CUP : 0;
        L->wr |= LJW_BUTTONS;
        if (!(L->hdec & 16)) { L->hdec |= 16; q_raised_whips++; }
        return;
    }
    if (plan == P_THROW) {
        // (its C-up comes at step 3, after the walk to the apron and the line-up: only from the throw's own geometry -- the
        //  roller no farther out than his man (LJ_THROW_OUT) and facing out or across (LJ_THROW_FACE). Else no throw: a grapple
        //  move puts the man down, and the next attempt takes him again)
        if (RDS32(ai + AI_STEP) >= 3) {
            s32 face = off_way(s, facing_to(-wx(s), -wz(s)));
            if (ring_d(s) > ring_d(k) + LJ_THROW_OUT || face < LJ_THROW_FACE) {
                give_plan(s, ai, P_MOVE);
                q_throw_refused++;
                removal_s("lumberjack: the roll-in: the throw refused at its C-up -- roller "); removal_num(s); removal_s(" "); removal_num((s32)ring_d(s));
                removal_s(" out, his man "); removal_num((s32)ring_d(k)); removal_s(", facing "); removal_num(face); removal_s(" off the ring's middle (a grapple move instead)"); removal_say();
            }
        }
        return;
    }
    if (plan == P_MOVE && L->given == P_MOVE) return;
    if (m->rt > LJ_HOLD_MAX - 2 * LJ_SECOND) {   // (held too long: his own move ends it)
        give_plan(s, ai, P_MOVE);
        return;
    }
    give_plan(s, ai, P_THROW);                                   // (the game's throw-in: to the apron, lined up, C-up)
    if (m->threw < 255) m->threw++;
    if (m->threw == 1) q_throws++;
}
// out of the ring or off the apron: the game's leave-the-ring (plan 36) at a lumberjack outside
static void role_exit(int s, u32 ai, s32 plan, int fresh, int c) {
    int best = -1; float bd = 1e9f;
    for (int o = c; o < match_slots_count(); o++) {
        if (o == s || !match_slots_is_extra(o) || !present(o) || !on_floor(o)) continue;
        float d = dist2(o, s);
        if (d < bd) { bd = d; best = o; }
    }
    if (best >= 0 && (fresh || plan != P_LEAVE_RING)) set_target(s, best);
    if (fresh || plan != P_LEAVE_RING) give_plan(s, ai, P_LEAVE_RING);
}
// the role, after his computer player's step (its plan run, its buttons out): when his plan has ended or his job changed,
// the next plan -- from the next step it runs, the game's
static void lumberjack_role(s32 s, u32 ai) {
    Lumberjack* L = &lj[s];
    int c = match_slots_competitors();
    s32 st = state_of(s), plan = RDS32(ai + AI_PLAN);
    q_role_steps++;
    if (!lj_role_said) {
        lj_role_said = 1;
        removal_s("lumberjack: the role is on (a Lumberjack match; lumberjacks in slots "); removal_num(c); removal_s(" to "); removal_num(match_slots_count() - 1);
        removal_s("; role steps outside a Lumberjack match so far "); removal_num((s32)lj_role_outside); removal_s(")"); removal_say();
    }
    if (L->bwait) L->bwait--;
    if (L->leg_on && (plan != P_WALK || L->given != P_WALK)) leg_over(s);
    else if (L->leg_on && can_act(st) && (u32)match_slots_steps() - L->leg_t >= LJ_LEG_STUCK) {
        // (a leg that makes no headway -- into a wall, the barricade, a man -- ends at once, and he stands a moment: r7's
        //  twitch5 match 1, a walk home on the spot into the aisle's wall for 33 steps, the walk file playing)
        float mx = wx(s) - L->leg_x, mz = wz(s) - L->leg_z, t = (float)((u32)match_slots_steps() - L->leg_t);
        float pace = L->pace > 0.5f ? L->pace : LJ_PACE0;
        if (sqrt_f(mx * mx + mz * mz) < 0.3f * pace * t) { leg_over(s); L->bwait = LJ_LEG_REST; give_plan(s, ai, P_WAIT); return; }
    }
    u16 key = (u16)(L->mode << 8 | L->prey);
    int fresh = key != L->seen;
    if (L->mode == LJ_PARKED || L->mode == LJ_GONE) { L->seen = key; if (plan != P_WAIT) give_plan(s, ai, P_WAIT); return; }
    if (L->mode == LJ_INTRO) {      // (down the ramp: the run-in's walk; standing a step when close behind the man ahead)
        L->seen = key;
        s32 want = L->iwalk ? P_WALK_DOWN : P_WAIT;
        if (plan != want) give_plan(s, ai, want);
        return;
    }
    if (st == ST_GRAPPLER) { L->seen = key; role_grapple(s, ai, plan); return; }
    if (st == ST_RUNNING && (plan == 0 || plan == P_WAIT)) {   // (ljround10, r21 real time: a lumberjack running with no plan by a
        //  man out there, 2-4 s -- the game's own run, nothing asked of it: a hittable man in reach gets the blow, the running one)
        strike_any(s, ai, (L->mode == LJ_SWARM || L->mode == LJ_ROLL || L->mode == LJ_BRAWL) ? (int)L->prey : -1);
        return;
    }
    if (!can_act(st)) return;       // (down, hit, in a move, running, whipped: the game's own -- decided once he is on his feet)
    L->seen = key;
    switch (L->mode) {
    case LJ_SWARM: case LJ_BRAWL: role_fight(s, ai, plan, fresh); break;
    case LJ_ROLL: role_roll(s, ai, plan, fresh); break;
    case LJ_EXIT: role_exit(s, ai, plan, fresh, c); return;
    default: role_post(s, ai, plan, fresh, c); break;
    }
    // (ljround10, the user: "never idle in striking distance") whatever the job decided, a lumberjack left standing with no
    // attack -- the wait, or no plan -- and a competitor out there in reach whom a blow can land on: the blow (r19's twitch5:
    // a swarm lumberjack waiting by his man reeling, the other man standing 58 from him; a roller with no plan, the other
    // man lying 57 from him)
    s32 now = RDS32(ai + AI_PLAN);
    if ((now == P_WAIT || now == 0) && free_state(state_of(s)))
        strike_any(s, ai, (L->mode == LJ_SWARM || L->mode == LJ_ROLL || L->mode == LJ_BRAWL) ? (int)L->prey : -1);
}

static void lumberjack_cpu_job(s32 s, u32 ai);
// (the computer player's step for slot s done: a lumberjack's role, a computer-played competitor's way back in -- and for
//  the QA trace, what it chose and what went out)
void lumberjack_cpu(s32 s) {
    if (s < 0 || s >= NM_SLOTS_CAP) return;
    if (!lj_live) { lj_role_outside++; return; }   // (the gate: never outside a Lumberjack match set up -- counted, QA)
    u32 ai = RD32(LJ_AI_CURRENT);
    if (ai < 0x80000000u || ai >= 0x80800000u) return;
    Lumberjack* L = &lj[s];
    L->ai_h = RD16(ai + 0x0C); L->ai_p = RD16(ai + 0x0E); L->cpu_t = (u32)match_slots_steps() + 1u; L->ai_plan = RDS32(ai + 0x00);
    if ((qa_lj_trace & 2) && !L->said_ai) {       // (QA trace: each computer player's level and skill chance, once a match)
        L->said_ai = 1;
        removal_s("ljai slot "); removal_num(s); removal_s(" level (+0x1C) "); removal_num(RDS32(ai + 0x1C)); removal_s(" skill (+0x20) "); removal_num(RDS32(ai + 0x20)); removal_say();
    }
    if (s >= match_slots_competitors() && match_slots_is_extra(s)) lumberjack_role(s, ai);   // (a lumberjack: his role)
    else lumberjack_cpu_job(s, ai);                                                         // (a computer-played competitor)
    L->our_h = RD16(ai + 0x0C); L->our_p = RD16(ai + 0x0E);
    if (L->our_h != L->ai_h || L->our_p != L->ai_p) L->wr |= LJW_BUTTONS;
    if (s >= match_slots_competitors() && state_of(s) == ST_GRAPPLER && (RDS32(rec(s) + LJ_GRAB_FLAGS) & LJ_GRAB_BEHIND) && ((L->our_h | L->our_p) & B_CUP)) {
        // (QA: from behind C-up lifts the man onto the shoulders. ROUND 9: with the throw in decided for the hold the role turns
        //  him with R first -- never C-up (lj_o's count); in a hold left to the game's own move (the 40%, or no job) it is the
        //  game's choice -- its plan 112, the shoulder lift -- counted apart, in the log)
        if (L->hdec & 4 || !(L->hdec & 2)) q_behind_cup_game++; else q_behind_cup++;
        if (q_behind_cup + q_behind_cup_game <= 20u) {   // (ROUND 9, QA: whose C-up -- the plan, his job and the hold's decision)
            removal_s("lumberjack: C-UP FROM BEHIND by "); removal_num(s); removal_s(" job "); removal_num(L->mode); removal_s(" man "); removal_num(L->prey);
            removal_s(" plan "); removal_num(RDS32(ai + 0x00)); removal_s(" step "); removal_num(RDS32(ai + 0x04)); removal_s(" given "); removal_num(L->given);
            removal_s(" hdec "); removal_num(L->hdec); removal_s(" his plan before "); removal_num(L->ai_plan); removal_s(" buttons "); removal_num(L->ai_h); removal_s(" / "); removal_num(L->ai_p);
            removal_s(" -> "); removal_num(L->our_h); removal_s(" / "); removal_num(L->our_p); removal_s(" target "); removal_num(RDS32(rec(s) + LJ_TARGET)); removal_s(" grab flags "); removal_num(RDS32(rec(s) + LJ_GRAB_FLAGS));
            { s32 tg = RDS32(rec(s) + LJ_TARGET); if (tg >= 0 && tg < match_slots_count()) { removal_s(" his state "); removal_num(state_of(tg)); removal_s(" at "); removal_num((s32)dist1(s, tg)); } }
            removal_say();
        }
    }
}
static void lumberjack_cpu_job(s32 s, u32 ai) {
    int c = match_slots_competitors();
    if (s >= c) return;
    // a competitor the computer plays (this hook runs for no man a controller drives)
    if (lj_intro || human(s)) return;
    if (s < 4 && lc[s].roll == 3) { send_in(s, ai); return; }     // (sent back in: his way in -- the last resort, 13 s out)
    if (qa_lj_out > 0 && s == c - 1) lj_probe(s, ai);              // (QA: the walk-out probe)
}

// QA (qa_lj_p1, lumberjack4): player 1 -- slot 0, a controller's man -- pressed at his controller as a player would, so
// the match is played as the user played it: after his opponent wherever he is (out through the ropes after him, back in
// over the apron when he is back in), striking (B) and grappling (A, then A for the move) when close. With qa_lj_p1 & 2
// he also goes out by himself every 50 s from 20 s in and stays out there -- the lumberjacks must bring him back in (a
// trip ends once he is back in the ring, or after 25 s). Called at the controller
// read (logic.c arcade_pad_hook) with his controller's buttons word; the game sees it as the player's own.
// QA: player 1's focus is not on an opponent -- on a lumberjack, himself, or (tornado) his partner
static int off_opponents(int c) {
    s32 tg = RDS32(rec(0) + LJ_TARGET);
    if (tg <= 0 || tg >= c) return 1;
    return c == 4 && RDS32(rec(tg) + LJ_TEAM) == RDS32(rec(0) + LJ_TEAM);
}
void lumberjack_qa_pad(u32 pad) {
    if (!qa_lj_p1 || !lj_live || lj_intro) return;
    int c = match_slots_competitors();
    if (c < 2 || !present(0)) return;
    int t = match_slots_steps(), o = opponent_of(0, c);
    s32 st = state_of(0);
    float x = wx(0), z = wz(0), ox = wx(o), oz = wz(o);
    // (a trip: 0 not yet out, 1 out on the floor, 2 over -- back in the ring; each window starts a new one)
    static int trip_win = -1, trip_state;
    int win = t >= 20 * LJ_SECOND ? (t - 20 * LJ_SECOND) / (50 * LJ_SECOND) : -1;
    if (win != trip_win) { trip_win = win; trip_state = 0; }
    int in_window = (qa_lj_p1 & 2) && win >= 0 && (t - 20 * LJ_SECOND) % (50 * LJ_SECOND) < 25 * LJ_SECOND;
    if (in_window && trip_state == 0 && on_floor(0)) trip_state = 1;
    if (in_window && trip_state == 1 && (area(0) & 1)) trip_state = 2;
    int trip = in_window && trip_state != 2;
    int want_out = trip || on_floor(o);
    u16 b = 0;
    if (free_state(st)) {
        if (want_out && (area(0) & 1)) {                       // (out: to the nearest ropes, through them -- C-up)
            float nx, nz, d = way_out(x, z, &nx, &nz);
            b = dpad_to(nx, nz);
            if (d < 40.0f && (t & 7) < 2) b |= B_CUP;
        } else if (want_out && (area(0) & 2)) {                // (on the apron: off it, outwards -- as a lumberjack gets off)
            float nx, nz; way_out(x, z, &nx, &nz);
            b = dpad_to(nx, nz);
            if ((t & 15) < 2) b |= B_CUP;
        } else if (!want_out && !(area(0) & 1) && !(area(o) & 4)) {   // (in: to the side's middle, then up and over -- C-up)
            int along_x = fabs_f(x) < fabs_f(z);
            float across = along_x ? fabs_f(z) : fabs_f(x), along = along_x ? x : z, sg = (along_x ? z : x) >= 0.0f ? 1.0f : -1.0f;
            u16 in = along_x ? (sg > 0.0f ? 0x0400 : 0x0800) : (sg > 0.0f ? 0x0200 : 0x0100);
            if ((area(0) & 4) && fabs_f(along) > LJ_SIDE_ALONG) {
                float t_al = along > 0.0f ? LJ_SIDE_ALONG - 20.0f : -(LJ_SIDE_ALONG - 20.0f), t_ac = sg * (across > 420.0f ? across : 420.0f);
                b = dpad_to((along_x ? t_al : t_ac) - x, (along_x ? t_ac : t_al) - z);
            } else if ((area(0) & 4) && across > LJ_APRON + 40.0f) b = in;
            else { b = in; if ((t & 15) < 13) b |= B_CUP; }
        } else if (trip && (qa_lj_p1 & 4)) {
            // (lumberjack5: out by himself he fights the lumberjacks, as the user tried: "I can't seem to land striking attacks
            //  on lumberjacks". No Mercy's man always faces his target (+0x24: the standing state turns him to it; a strike
            //  turns him to it too), so he strikes the lumberjack who is his target -- the game makes the one who hits him his
            //  target -- and, with none near, puts his focus on one the game's own way, C-right (retarget_nearest_wrestler:
            //  every man in the match, lumberjacks too). Then to him, and B)
            s32 tg = RDS32(rec(0) + LJ_TARGET);
            int on_lj = match_slots_is_extra(tg) && present(tg) && on_floor(tg) && dist1(tg, 0) < 250.0f;
            if ((qa_lj_p1 & 8) && on_lj && lj[tg].mode != LJ_POST) on_lj = 0;   // (QA 8: only a lumberjack at his post -- the hit itself, no defence)
            if (!on_lj) b = (t & 3) == 0 ? B_CRIGHT : 0;      // (a tap every 4 steps: the focus goes round the men)
            else if (dist1(tg, 0) > 80.0f) b = dpad_to(wx(tg) - x, wz(tg) - z);   // (close: as with his opponent -- two men never stand
                                                                                 //  much closer than 60-75: b7's bot walked at 45 for good)
            else if ((t % 6) < 2) b = B_B;
        } else if (trip) {
            // (out by himself: he stands there -- the lumberjacks come. ljround10: every other trip he first walks on out to the
            //  barricade's foot (470 out), as a player takes the fight there -- r3's trace, the player's time out mostly by the
            //  apron's edge: few holds, and fewer moves, by the barricade, whose check then rarely had a case)
            b = 0;
            if ((trip_win & 1) && on_floor(0) && ring_d(0) < 465.0f) {
                float ax = fabs_f(x), az = fabs_f(z);
                b = ax >= az ? (x >= 0.0f ? 0x0100 : 0x0200) : (z >= 0.0f ? 0x0800 : 0x0400);   // (straight out: right / left, up / down)
            }
        } else if (off_opponents(c)) {
            b = (t & 3) == 0 ? B_CRIGHT : 0;                   // (lumberjack5: his focus back on his opponent first -- C-right, as a
                                                               //  player would. No Mercy keeps a man's focus on whoever last hit or
                                                               //  held him: after a lumberjack's throw-in it is that lumberjack, and
                                                               //  his blows go that way -- b16's triple threat: 0 hits outside)
        } else {                                               // (with him: after him, then strike and grab)
            float dx = ox - x, dz = oz - z;
            if (fabs_f(dx) + fabs_f(dz) > 75.0f) b = dpad_to(dx, dz);
            else {
                int k = (t / 10) % 4;
                if ((t % 10) < 2) b = k == 3 ? B_A : B_B;      // (mostly strikes, now and then a grapple)
            }
        }
    } else if (st == ST_GRAPPLER && (t % 10) < 2) b = B_A;      // (his grapple: the move)
    else if ((st == 46 || st == 49 || st == 67 || st == 69 || st == 71 || st == 73 || st == 75) && (t % 4) < 2)
        b = 0xC030u;            // (pinned, in a hold, down: he struggles, as a player mashes -- A+B+L+R, the computer player's own
                                //  struggle (0xC030). b14's first run: never struggling,
                                //  pinned in 25 s. Not in a front grapple: b16's mashing broke every lumberjack's hold at once)
    else if ((area(0) & 2) && !want_out) { b = 0; if ((t & 15) < 13) b |= B_CUP; }
    RD16(pad) |= b;
    lj_qa_pad_last = b;
}

// QA: state.csv's bs_tries / bs_grabs / bs_step (a QA match with qa_lumberjacks) --
//   bs_tries  lumberjacks present | ever went for a man << 8 | steps a lumberjack was in the ring / on the apron on his feet,
//             able to leave (to 255; lumberjack4: not while down there, thrown in) << 16 |
//             a lumberjack ever a side of the match << 24
//   bs_grabs  times a competitor was out | of those, thrown over the top << 8 | roll-ins started << 16 | back in the ring << 24
//   bs_step   the engine's count | competitors << 8 | QA walk-outs << 16 | hits taken out there (to 255) << 24
// and (any Lumberjack match, both modes) lumberjack_qa_word for rr_put_out / rr_kept_out: outs | roll-ins << 8 |
// back in << 16 | (side seen | in the ring over 5 s << 1) << 24; lumberjack_qa2 for lj_a .. lj_h (below).
static u32 b8(u32 v) { return v > 255u ? 255u : v; }
void lumberjack_qa(u32 q) {
    u32 ring = b8(q_ring_steps), hits = b8(q_hits);
    RD32(q + 0) = (q_present & 0xFF) | (q_engaged & 0xFF) << 8 | ring << 16 | (q_side & 1) << 24;
    RD32(q + 4) = (q_outs & 0xFF) | (q_thrown & 0xFF) << 8 | (q_sends & 0xFF) << 16 | (q_back & 0xFF) << 24;
    RD32(q + 8) = ((u32)match_slots_count() & 0xFF) | ((u32)match_slots_competitors() & 0xFF) << 8 | (q_probes & 0xFF) << 16 | hits << 24;
}
u32 lumberjack_qa_word(void) {
    u32 bad = (q_side & 1) | (q_ring_steps > 5u * LJ_SECOND ? 2u : 0u);
    return (q_outs & 0xFF) | (q_sends & 0xFF) << 8 | (q_back & 0xFF) << 16 | bad << 24;
}
//   lj_a  the intro: set off down the ramp | at their posts << 8 | (lumberjack5) the closest two lumberjacks walking down
//         the ramp (world units; 0 none walked) << 16
//   lj_b  the intro: set up | done << 1 | steps the bell was held (/30) << 8 | the intro's camera on (/30) << 16 | its steps (/30) << 24
//   lj_c  the roll-ins: started | the roller got hold << 8 | a computer-played man sent back in (his own buttons, the game's
//         climb in; lumberjack4 -- before, slid in under the rope) << 16 | rolled back in the ring << 24
//   lj_d  by himself: escapes seen | back in that way << 8 | hits taken on the way (to 255) << 16 | QA escape probes << 24
//   lj_e  the brawls: windows rolled | brawls << 8 | brawls not between rivals << 16 | hits in them (to 255) << 24
//   lj_f  the lumberjacks: drawn or picked | not wrestlers among them << 8 | picked by the player << 16 | rival pairs << 24
//   lj_g  the card's caption given | the variant << 16 (0 single, 1 triple, 2 tornado) | armed << 24
//   lj_h  the settings: count | picked << 8 | variant << 16 | competitors << 24
void lumberjack_qa2(u32 q) {
    RD32(q + 0x00) = b8(q_released) | b8(q_arrived) << 8 | (q_walk_min > 0xFFFFu ? 0xFFFFu : q_walk_min) << 16;
    RD32(q + 0x04) = (q_intro_on & 1) | (q_intro_done & 1) << 1 | b8(q_held / 30u) << 8 | b8(q_cam / 30u) << 16 | b8(q_intro_steps / 30u) << 24;
    RD32(q + 0x08) = b8(q_sends) | b8(q_grabbed) << 8 | b8(q_sent_in) << 16 | b8(q_rolled_in) << 24;
    RD32(q + 0x0C) = b8(q_esc) | b8(q_esc_back) << 8 | b8(q_esc_hits) << 16 | b8(q_esc_probes) << 24;
    RD32(q + 0x10) = b8(q_brawl_rolls) | b8(q_brawls) << 8 | b8(q_brawl_bad) << 16 | b8(q_brawl_hits) << 24;
    RD32(q + 0x14) = b8((u32)lj_n) | b8(q_drawn_bad) << 8 | (u32)(lj_kind & 1) << 16 | b8(q_rival_pairs) << 24;
    RD32(q + 0x18) = (q_caption > 0xFFFFu ? 0xFFFFu : q_caption) | (u32)(lj_qa_n ? (lj_c == 3 ? 1 : lj_c == 4 ? 2 : 0) : lumberjack_variant()) << 16 | (u32)(lj_armed & 1) << 24;
    RD32(q + 0x1C) = b8((u32)eff_count()) | (u32)(eff_pick() & 1) << 8 | (u32)lumberjack_variant() << 16 | b8((u32)lj_c) << 24;
    // (lumberjack3)
    //   lj_i  lumberjack-on-lumberjack hits | of those, the damage the game did that was put back << 8 | the competitors' hits
    //         on lumberjacks that hurt (to 255) << 16 | the lumberjacks' lowest spirit at the bell (0xFF not yet) << 24
    //   lj_j  the first two lumberjacks' Superstars (slots c, c + 1) | lj_k the next two
    RD32(q + 0x20) = b8(q_lj_hits) | b8(q_lj_undone) << 8 | b8(q_comp_dmg) << 16 | (q_spirit_bell & 0xFF) << 24;
    {   u32 v[4];
        for (int k = 0; k < 4; k++) { int s = lj_c + k; v[k] = (lj_live && s < match_slots_count()) ? (u32)(u16)RDS16(rec(s) + LJ_ID) : 0; }
        RD32(q + 0x24) = v[0] | v[1] << 16; RD32(q + 0x28) = v[2] | v[3] << 16;
    }
    //   lj_l  (lumberjack5) at the post: steps not standing (a taunt, a run: the twitching; to 0xFFFF) | taunts begun there
    //         (to 15) << 16 | roll-in holds from behind turned to the front with R (to 15) << 20 | player 1's own hits on
    //         lumberjacks that hurt them (kept: to 255) << 24
    //         (lumberjack3's state changes / reversed turns: the game's log, "the match over")
    //   lj_m  at the post: lumberjack-seconds there | moves over 1 unit in a step << 16 (to 0xFFFF)
    RD32(q + 0x2C) = (q_post_busy > 0xFFFFu ? 0xFFFFu : q_post_busy) | (q_post_taunts > 15u ? 15u : q_post_taunts) << 16 |
                     (q_turned > 15u ? 15u : q_turned) << 20 | b8(q_p1_lj_hits) << 24;
    RD32(q + 0x30) = (q_post_steps / 30u > 0xFFFFu ? 0xFFFFu : q_post_steps / 30u) | (q_post_moves > 0xFFFFu ? 0xFFFFu : q_post_moves) << 16;
    // (lumberjack4: playability)
    //   lj_n  player 1's hits on his opponent: in the ring | outside << 8 | in the ring after a roll-in (his or his
    //         opponent's) << 16 (each to 255) | his spells on the floor outside (to 15) << 24 | of those, ended by the
    //         lumberjacks' roll-in (to 15) << 28
    //   lj_o  a competitor at ring height off the ring, steps (to 255) | out of the arena, steps (to 255) << 8 | (lumberjack5)
    //         steps a roller holding his man from behind had C-up (the shoulder lift; to 15) << 16 | roll-in holds from behind
    //         let go to be taken again from the front (to 15) << 20 | men thrown into the ring by the roll-in
    //         (to 7) << 24 | player 1's spells outside that lasted 20 s (to 15) << 27 | slot 0 driven by a controller << 31
    RD32(q + 0x34) = b8(q_p1_in) | b8(q_p1_out) << 8 | b8(q_p1_after_roll) << 16 | (q_p1_outs > 15u ? 15u : q_p1_outs) << 24 |
                     (q_p1_thrown > 15u ? 15u : q_p1_thrown) << 28;
    RD32(q + 0x38) = b8(q_float) | b8(q_away) << 8 | (q_behind_cup > 15u ? 15u : q_behind_cup) << 16 | (q_behind > 15u ? 15u : q_behind) << 20 |
                     (q_thrown > 7u ? 7u : q_thrown) << 24 |
                     (q_p1_long > 15u ? 15u : q_p1_long) << 27 | (q_p1_human & 1u) << 31;
    // (ljtwitch: the jitter measure -- each: the events (turns reversed within 1/3 s, animation files switched back within
    //  2/3 s or restarted before their end, states switched back within 2/3 s, steps reversed; to 0xFFFF) | the man-seconds
    //  measured << 16)
    //   lj_p  lumberjacks idle outside the ring: standing at the post, or anywhere else on the floor in no job, or walking
    //         back to the post (the user's "when they're idle outside the ring, thats where I notice most")
    //   lj_q  the intro's walk down the ramp
    //   lj_r  lumberjacks on a man (the swarm, the roll-in's roller), brawling, leaving the ring; down / in a move / whipped
    //   lj_s  the competitors out on the floor
    RD32(q + 0x3C) = jit_word(JC_IDLE_POST, JC_IDLE_AWAY);
    RD32(q + 0x40) = jit_word(JC_INTRO, JC_INTRO);
    RD32(q + 0x44) = jit_word(JC_WALK, JC_BUSY);
    RD32(q + 0x48) = jit_word(JC_COMP_OUT, JC_COMP_OUT);
    // (ljround8)
    //   lj_t  competitor 0: the lumberjacks' hits on him out there (to 255) | his seconds out on the floor (to 4095) << 8 |
    //         the lumberjack-seconds on him (to 4095) << 20
    //   lj_u  competitor 1: the same
    //   lj_v  holds of a competitor from the front where either could be done (the move zone): the throw in decided | a grapple
    //         move decided << 8 (the holds out of the zone, the throw forced: the log's "decisions") | holds from behind on a
    //         man heading back in that he got out of (not turned to face: the elbow) << 16 | spells out with two of those -- the
    //         escape loop << 24 (each to 255)
    //   lj_w  the intro: walkers' stops | the most stops by one walker << 8 | walkers << 16 | holds of a competitor from behind << 24
    for (int k = 0; k < 2; k++) RD32(q + 0x4C + 4u * (u32)k) = b8(q_c_hits[k]) | (q_c_out[k] / 30u > 4095u ? 4095u : q_c_out[k] / 30u) << 8 |
                                                               (q_c_eng[k] / 30u > 4095u ? 4095u : q_c_eng[k] / 30u) << 20;
    RD32(q + 0x54) = b8(q_dec_throw) | b8(q_dec_move) << 8 | b8(q_rear_esc_in) << 16 | b8(q_loops) << 24;
    RD32(q + 0x58) = b8(q_istops) | b8(q_istop_max) << 8 | b8(q_iwalkers) << 16 | b8(q_rear_grabs) << 24;
    // (ROUND 9)
    //   lj_x  the lumberjacks' grapple moves on a competitor out on the floor | of those, the man by the apron's edge (under 370
    //         out) << 8 | by the barricade or in the aisle (430 out or more) << 16 | done from behind << 24 (each to 255)
    RD32(q + 0x5C) = b8(q_mv_out) | b8(q_mv_apron) << 8 | b8(q_mv_far) << 16 | b8(q_mv_rear) << 24;
    //   lj_y  the aggression over the whole run (the lumberjacks' hits on him out there | lumberjack-seconds on him / 4 << 8):
    //         competitor 0 | competitor 1 << 16 (each to 255) -- ROUND 9: one match's handful of hits on the CPU (r8's
    //         lumberjack_twitch5 match 5: 4 against the player's 21) is no measure of "the CPU as much as the player"
    //   lj_z  (ljround10) lumberjacks idle in striking distance of a competitor out there: spells of 2 s | seconds in
    //         all << 8 | roll-in whips on the ramp / stage << 16 | lumberjacks put on a man for being in reach << 24
    RD32(q + 0x64) = b8(q_idle_spells) | b8(q_idle_steps / 30u) << 8 | b8(q_raised_whips) << 16 | b8(q_engage_reach) << 24;
    RD32(q + 0x60) = b8(q_run_hits[0]) | b8(q_run_eng[0] / 120u) << 8 | b8(q_run_hits[1]) << 16 | b8(q_run_eng[1] / 120u) << 24;
}
