// Match commentary: Jim Ross & Jerry Lawler (WWE '13's Attitude Era lines, played by the exe:
// arcade_commentary.cpp). This side watches the match the way a commentator does and says what
// happened (plat_comm_event); the exe's director picks a line for it, or lets it pass.
//
// What it watches, each frame of a match (wrestler struct offsets from its start):
//   +0x14 state (0x4f down on the mat), +0x08 the one he's after, +0xA0 id (-1 empty slot),
//   +0xAA how much he has left (drops as he's hurt), +0x30A his move (number: / 10 = the move,
//   comm_moves.h's class of it), +0xEE 4 = SPECIAL, +0x94 0x10 = outside the ring, 1 = eliminated;
//   0x80159204 the wrestler being pinned (-1 none), 0x80153A84 the referee's count on screen (1-3),
//   0x801594D0 the result is up (the match is over), 0x800A10EC its winners (bits 0-3).
// A hit: someone's +0xAA drops. The one hurting him is the one after him who is in the middle of a move
// (or, one on one, the other man); what's said is by the move's class and how much it took out of him
// (summed while it keeps dropping): a big one a big move, a finisher by name or with SPECIAL lit.
#include "arcade.h"
#include "comm_moves.h"

enum {   // keep in step with the exe's commentary/director.h
    EV_NONE = 0, EV_MATCH_START, EV_LOCKUP, EV_STRIKE, EV_STRIKE_BIG, EV_GRAPPLE, EV_BIG_MOVE, EV_KNOCKDOWN,
    EV_REVERSAL, EV_SPECIAL, EV_FINISHER, EV_PIN, EV_COUNT, EV_KICKOUT_1, EV_KICKOUT_2, EV_PINFALL,
    EV_SUBMISSION, EV_ROPE_BREAK, EV_ESCAPE, EV_TAP_OUT, EV_OUTSIDE, EV_BACK_IN, EV_OUTSIDE_HIT, EV_TOP_ROPE,
    EV_AERIAL, EV_AERIAL_MISS, EV_WEAPON, EV_WEAPON_HIT, EV_TABLE, EV_BLOOD, EV_TAG, EV_SAVE, EV_TAUNT,
    EV_RUN_IN, EV_DOMINATE, EV_COMEBACK, EV_TIRED, EV_ELIMINATION, EV_RUMBLE_ENTRY, EV_CAGE, EV_LADDER,
    EV_WIN, EV_DQ, EV_ENTRANCE, EV_SIGN_OFF, EV_FILLER, EV_DANGER
};

#define W_STATE   0x14
#define W_TARGET  0x08
#define W_ID      0xA0
#define W_HP      0xAA
#define W_MOVE    0x30A     // the move he's doing (u16: the game's move number)
#define W_FLAGS94 0x94
#define W_EE      0xEE
#define ST_DOWN   0x4f
#define ST_STAND  0x01
#define NM_PINNED     0x80159204u
#define NM_REF_COUNT  0x80153A84u
#define NM_RESULT_UP  0x801594D0u
#define NM_RESULT     0x800A10ECu
#define NM_HUD_MSG    0x80158E90u
#define MSG_ROPE_BREAK 0x801448BCu

#define WR_BASE (NM_WRESTLERS - 8u)
int comm_speaker;   // 0 quiet, 1 a line on (whose not known), 2 Jim Ross's, 3 Jerry Lawler's
static u32 wr(int s) { return WR_BASE + (u32)s * NM_WRESTLER_SIZE; }

typedef struct {
    s16 id;
    s16 hp;
    s32 state;
    u8 special, outside, gone, danger;
    u16 last_move;        // his move (+0x30A: the last one he did; set as it starts) and when it changed
    s32 move_at;
    s16 aerial_wait;      // frames left for his top-rope move to land (0: none on)
    u8 aerial_hit;
    // a hit being taken: from whom, with what, how much so far, frames since the last drop
    s8 by;
    u8 cls, open, quiet;
    u16 move;
    s16 dmg;
    s16 down_wait;        // frames to wait for him to go down after a hit
    u8 down_cls;
    // who's on top: damage dealt / taken lately (fading: 15/16 a second), when he was last beaten down
    // (taking it with nothing back), his hits since, when he was last said to be in control
    s16 dealt, took;
    s32 beaten_at, dom_at;
    u8 hits_back;
} Seen;

static Seen sn[NM_SLOTS_CAP];
static int was_match, started, bell_said, result_said, pin_slot = -1, pin_max, n_slots;
static u32 last_msg;
static int frames_in;
static u32 rr_said[4];                // Rumble entrants announced (by list place)
static u8 rr_walk[NM_SLOTS_CAP];      // an entrant announced, not out yet (the slot shows a stand-in through his countdown)
static int place;                     // where the match is: the venue + 1, | 0x80 a Royal Rumble
extern int rr_num_offset;
#define W_RR_INDEX 0xF8               // (u8: his place in the Rumble's list of entrants)
#define W_RR_ENTRANT 0xD8             // (u16: the Superstar on his way to the slot)
static int last_sub_frames = -1000;
static int tired_at;
static u32 vi_start, vi_fade;
int dbg_comm_on = 1;                  // tests

static int female(s16 id) { int g = (id >> 8) & 0xFF; return g == 7 || g == 8; }
static void say_move(int ev, int a, int b, int arg, u16 move) {
    int fa = a >= 0 && a < n_slots ? sn[a].id : -1, fb = b >= 0 && b < n_slots ? sn[b].id : -1;
    u32 m = move == 0xFFFF ? 0xFFFFu : (u32)(move / 10);
    plat_comm_event(ev, fa, fb, (int)((u32)(arg & 0xFF) | ((fa >= 0 && female((s16)fa)) ? 0x100u : 0u) | (m << 16)));
}
static void say(int ev, int a, int b, int arg) { say_move(ev, a, b, arg, 0xFFFF); }
static int attacking(s32 st) {
    return st != ST_STAND && st != 0 && st != 0x30 && st != 0x2d && st != ST_DOWN && st != 0x31;
}
static int who_hurt(int v) {
    int best = -1;
    for (int s = 0; s < n_slots; s++) {
        if (s == v || sn[s].id < 0 || sn[s].gone) continue;
        u32 o = wr(s);
        int after = RDS32(o + W_TARGET) == v;
        if (after && attacking(RDS32(o + W_STATE))) return s;
        if (after && best < 0) best = s;
    }
    if (best < 0 && n_slots == 2) best = v ^ 1;
    return best;
}
// a move as it's said: Hogan's ground special is the Russian Neck Drop's move played with his leg drop (gen/mkroster.py,
// gen/revanims.py NM_OWN) -- the leg drop's (7980: its class, King's "Hogan hits the leg drop")
static u16 said_move(u32 o, u16 m) { (void)o; return m; }
static int move_class(u16 m) { return m == 0xFFFF || m / 10 >= COMM_MOVES_N ? MC_NONE : comm_move_class[m / 10]; }

static void hit_done(int v) {
    Seen* x = &sn[v];
    x->open = 0;
    int a = x->by, d = x->dmg, c = x->cls;
    if (a < 0) return;
    int special = sn[a].special;
    int ev;
    if (c == MC_FINISHER || (special && d >= 18 && (c == MC_BIG_MOVE || c == MC_SUBMISSION || c == MC_AERIAL || c == MC_DROP)))
        ev = EV_FINISHER;
    else if (c == MC_SUBMISSION) ev = EV_SUBMISSION;
    else if (c == MC_AERIAL) ev = EV_AERIAL;
    else if (c == MC_REVERSAL) ev = EV_REVERSAL;
    else if (sn[v].outside || sn[a].outside || c == MC_OUTSIDE) ev = d >= 5 ? EV_OUTSIDE_HIT : EV_STRIKE;
    else if (c == MC_BIG_MOVE || d >= 14) ev = EV_BIG_MOVE;
    else if (c == MC_STRIKE_BIG || c == MC_DROP) ev = EV_STRIKE_BIG;
    else if (c == MC_GRAPPLE) ev = d >= 8 ? EV_BIG_MOVE : EV_GRAPPLE;
    else ev = EV_STRIKE;
    if (sn[a].aerial_wait > 0) { sn[a].aerial_hit = 1; sn[a].aerial_wait = 0; if (c != MC_FINISHER) ev = EV_AERIAL; }
    say_move(ev, a, v, d > 255 ? 255 : d, x->move);     // (arg: the damage, for the logs)
    if (ev == EV_SUBMISSION) last_sub_frames = frames_in;
    // the run of the match (after the move's own line: this one waits for it): one man taking it to the other, or the one who was getting beaten fighting back
    Seen* A = &sn[a];
    A->dealt += (s16)d; x->took += (s16)d; x->hits_back = 0;
    // (in retraces, 60 a second)
    s32 now = (s32)(RD32(NM_VI_COUNT) - vi_start);
    if (x->took >= 30 && x->dealt <= 4) x->beaten_at = now;
    if (now - A->beaten_at < 1200 && ++A->hits_back >= 3) {
        say(EV_COMEBACK, a, v, 0); A->beaten_at = -100000; A->hits_back = 0;
    } else if (A->dealt >= 30 && A->took <= 4 && now - A->dom_at > 900) {
        say(EV_DOMINATE, a, v, 0); A->dom_at = now;
    }
    // a strike that puts him down: a knockdown line, if he goes down soon
    x->down_wait = (ev == EV_STRIKE || ev == EV_STRIKE_BIG) ? 40 : 0;
}

static s32 last_step = -1;
void commentary_frame(void) {
    // the entrances: JR on each Superstar as he walks out (the stage shots: steps 5 and 11)
    s32 step = RDS16(NM_SCREEN) == 42 ? RDS32(NM_ENTRANCE_STEP) : -1;
    if (step != last_step && (step == 5 || step == 11) && dbg_comm_on) {
        s32 id = RDS32(NM_SLOT_WRESTLER + (step == 5 ? 0u : 4u));
        if (id > 0) plat_comm_event(EV_ENTRANCE, id, -1, (int)(0xFFFF0000u | (female((s16)id) ? 0x100u : 0u)));
    }
    last_step = step;
    int in_match = RDS32(NM_MODE) == -2 && RD16(NM_MATCH_FRAMES) != 0xFFFF;
    comm_speaker = plat_comm_tick(in_match && started && !result_said);   // (announce.c: who's talking)
    if (!in_match) {
        if (was_match) {
            // leaving: a match that ended gets a good night; one quit is just quiet
            if (result_said && dbg_comm_on) say(EV_SIGN_OFF, -1, -1, place); else plat_comm_reset();
            was_match = 0;
        }
        return;
    }
    n_slots = nm_slots();
    if (!was_match) {
        was_match = 1; started = 0; bell_said = 0; result_said = 0; pin_slot = -1; pin_max = 0; frames_in = 0;
        last_msg = 0; last_sub_frames = -1000;
        plat_comm_reset();
        for (int s = 0; s < NM_SLOTS_CAP; s++) { sn[s].id = -1; sn[s].open = 0; sn[s].down_wait = 0; }
        tired_at = 0; vi_start = vi_fade = RD32(NM_VI_COUNT);
        for (int s = 0; s < NM_SLOTS_CAP; s++) rr_walk[s] = 0;
    }
    if (!dbg_comm_on) return;
    frames_in++;
    // (slots as they are now: someone new in a Rumble, the empties)
    for (int s = 0; s < n_slots; s++) {
        u32 o = wr(s);
        s16 id = RDS16(o + W_ID);
        if (id != sn[s].id) {
            sn[s].id = id; sn[s].hp = RDS16(o + W_HP); sn[s].state = RDS32(o + W_STATE);
            sn[s].special = (RD8(o + W_EE) & 4) != 0; sn[s].outside = (RD8(o + W_FLAGS94 + 1) & 0x10) != 0;
            sn[s].gone = (RD8(o + W_FLAGS94 + 1) & 1) != 0; sn[s].open = 0; sn[s].down_wait = 0;
            sn[s].danger = (RD8(o + W_EE) & 1) != 0;
            sn[s].last_move = RD16(o + W_MOVE); sn[s].move_at = -1000; sn[s].aerial_wait = 0;
            sn[s].dealt = sn[s].took = 0; sn[s].beaten_at = sn[s].dom_at = -100000; sn[s].hits_back = 0;
            // a Rumble entrant: as his countdown starts the slot shows a stand-in (his number, from his place in
            // the list, once each), then him as he walks out (JR and King on him, by name if they've lines)
            if (id >= 0 && started && (RD32(NM_MATCH_FLAGS) & 0x4000u)) {
                int li = RD8(o + W_RR_INDEX);
                if (li >= 2 && li < 128 && !(rr_said[li >> 5] & (1u << (li & 31)))) {
                    rr_said[li >> 5] |= 1u << (li & 31);
                    int num = li + 1 + rr_num_offset;
                    say(EV_RUMBLE_ENTRY, -1, -1, num > 0 && num < 100 ? num : 0);
                    rr_walk[s] = 1;
                }
                if (rr_walk[s] && id == RDS16(o + W_RR_ENTRANT)) {
                    rr_walk[s] = 0;
                    plat_comm_event(EV_ENTRANCE, id, -1, (int)(0xFFFF0000u | (female(id) ? 0x100u : 0u)));
                }
            } else rr_walk[s] = 0;
        }
    }
    u16 mf = RD16(NM_MATCH_FRAMES);
    if (!started) {
        if (mf <= 8) return;
        started = 1;
    }
    if (!bell_said && mf > 20) {
        bell_said = 1;
        // (an arena of ours, arena.c, plays in a game's venue: no place named -- JR's lines name the game's ten)
        { extern int arena_current(void);
          place = (RD8(NM_VENUE) < 10 && arena_current() < 10 ? RD8(NM_VENUE) + 1 : 0) | ((RD32(NM_MATCH_FLAGS) & 0x4000u) ? 0x80 : 0); }
        // (the two at a Rumble's bell: numbers 1 and 2, not announced)
        rr_said[0] = 3; rr_said[1] = rr_said[2] = rr_said[3] = 0;
        say(EV_MATCH_START, 0, 1, place);
    }

    if (result_said) return;          // (the replays after it: nothing more)
    u32 vi = RD32(NM_VI_COUNT);
    if (vi - vi_fade >= 60) {
        vi_fade = vi;
        for (int s = 0; s < n_slots; s++) { sn[s].dealt = (s16)(sn[s].dealt * 15 / 16); sn[s].took = (s16)(sn[s].took * 15 / 16); }
    }
    // a long one: five minutes in (JR: "and five minutes have elapsed in this contest"), when it's gone quiet
    if ((s32)(vi - vi_start) > 18000 && !tired_at && n_slots == 2) {
        int quiet = 1;
        for (int s = 0; s < n_slots; s++) if (sn[s].open || sn[s].took > 8) quiet = 0;
        if (quiet || (s32)(vi - vi_start) > 19800) {        // (by 5:30 it's not five minutes any more)
            tired_at = (s32)(vi - vi_start);
            if (quiet) say(EV_TIRED, 0, 1, 0);
        }
    }
    for (int s = 0; s < n_slots; s++) {
        Seen* x = &sn[s];
        if (x->id < 0) continue;
        u32 o = wr(s);
        s32 st = RDS32(o + W_STATE);
        s16 hp = RDS16(o + W_HP);
        int special = (RD8(o + W_EE) & 4) != 0;
        int outside = (RD8(o + W_FLAGS94 + 1) & 0x10) != 0;   // (u16 at +0x94: its low byte)
        int gone = (RD8(o + W_FLAGS94 + 1) & 1) != 0;
        u16 mv = said_move(o, RD16(o + W_MOVE));
        if (mv != x->last_move) {
            x->last_move = mv; x->move_at = frames_in;
            int mc = move_class(mv);
            if (mc == MC_TAUNT) say(EV_TAUNT, s, -1, 0);                // (as a taunt starts)
            else if (mc == MC_AERIAL && !outside) {                     // up top: does it land?
                say(EV_TOP_ROPE, s, RDS32(o + W_TARGET), 0);
                x->aerial_wait = 200; x->aerial_hit = 0;
            } else if (mc == MC_FINISHER) say(EV_SPECIAL, s, RDS32(o + W_TARGET), 0);   // setting it up
        }
        if (x->aerial_wait > 0 && --x->aerial_wait == 0 && !x->aerial_hit && !x->outside) {
            int t = RDS32(o + W_TARGET);
            say(EV_AERIAL_MISS, s, t >= 0 && t < n_slots ? t : -1, 0);
        }
        if (gone && !x->gone && (RD32(NM_MATCH_FLAGS) & 0x4000u) && !RD16(NM_RESULT_UP)) say(EV_ELIMINATION, s, -1, 0);
        x->gone = gone;
        if (gone) { x->state = st; x->hp = hp; continue; }
        if (special && !x->special) say(EV_SPECIAL, s, -1, 0);
        int danger = (RD8(o + W_EE) & 1) != 0;
        if (danger && !x->danger) say(EV_DANGER, s, -1, 0);
        x->danger = (u8)danger;
        if (outside != x->outside) say(outside ? EV_OUTSIDE : EV_BACK_IN, s, -1, 0);
        // a hit (not while he's pinned: the count's the thing then)
        if (hp < x->hp && pin_slot != s) {
            int a = who_hurt(s);
            if (!x->open || a != x->by) {
                if (x->open) hit_done(s);
                x->open = 1; x->by = (s8)a; x->dmg = 0;
                // (the move's damage lands as it ends; a move set long ago isn't this one)
                x->move = (a >= 0 && frames_in - sn[a].move_at < 300) ? sn[a].last_move : 0xFFFF;
                x->cls = (u8)move_class(x->move);
            }
            x->dmg += x->hp - hp;
            x->quiet = 0;
        } else if (x->open && ++x->quiet > 24) {
            hit_done(s);
        }
        // down after a strike
        if (x->down_wait > 0) {
            x->down_wait--;
            if (st == ST_DOWN && x->state != ST_DOWN) { say(EV_KNOCKDOWN, who_hurt(s), s, 0); x->down_wait = 0; }
        }
        x->state = st; x->hp = hp; x->special = (u8)special; x->outside = (u8)outside;
    }

    // pins
    s32 p = RDS32(NM_PINNED);
    if (p >= 0 && p < n_slots) {
        if (pin_slot != p) { pin_slot = p; pin_max = 0; say(EV_PIN, who_hurt(p), p, 0); }
        s16 c = RDS16(NM_REF_COUNT);
        if (c > pin_max && c <= 3) { pin_max = c; say(EV_COUNT, who_hurt(p), p, c); }
    } else if (pin_slot >= 0) {
        int v = pin_slot, a = who_hurt(v);
        // (the three count and the result can come in the same frame as the pin ends, before the
        // count on screen says 3: the result, or him out of it, says it was)
        int over = RD16(NM_RESULT_UP) || (RD8(wr(v) + W_FLAGS94 + 1) & 1) || (RD32(NM_RESULT) & 0xFFu);
        if (pin_max >= 3 || over) { pin_max = 3; say(EV_PINFALL, a, v, 3); }
        else if (last_msg == MSG_ROPE_BREAK) say(EV_ROPE_BREAK, v, a, pin_max);
        else if (pin_max == 2) say(EV_KICKOUT_2, v, a, 2);
        else say(EV_KICKOUT_1, v, a, pin_max);
        if (pin_max >= 3) result_said = 1;
        pin_slot = -1; pin_max = 0;
    }
    u32 msg = RD32(NM_HUD_MSG);
    if (msg && msg != last_msg && msg == MSG_ROPE_BREAK && pin_slot < 0 && frames_in - last_sub_frames < 400)
        say(EV_ROPE_BREAK, -1, -1, 0);
    if (msg) last_msg = msg;

    // the result
    if (RD16(NM_RESULT_UP) && !result_said) {
        result_said = 1;
        u32 r = RD32(NM_RESULT);
        int w = -1;
        for (int s = 0; s < 4 && s < n_slots; s++) if (r & (1u << s)) { w = s; break; }
        say(frames_in - last_sub_frames < 120 ? EV_TAP_OUT : EV_WIN, w, -1, 0);
    }
}

// The operator's settings, given to the exe when they change
void commentary_settings(int on, int volume, int talk) {
    static int last = -1;
    int key = (on ? 1 : 0) | (volume << 1) | (talk << 9);
    if (key == last) return;
    last = key;
    plat_comm_config(on, volume, talk);
}
