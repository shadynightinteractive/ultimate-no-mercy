// slots.c -- the per-slot match model: more than four wrestlers present in ANY match type (one on one, tag, triple
// threat, handicap, cage, ladder, special and story matches), up to the engine's 32 slots (NM_SLOTS_CAP).
//
// Changed for new features (Ultimate No Mercy) -- any32, 2026-11-18. The design: docs\MATCH_SLOTS.md.
//
// What the engine already had (the six-man / 32-slot engine, six-engine\work\spec.py with SIX_SLOTS=32): every per-slot
// and per-model array of the match overlay and the main segment sized for 32, the HUD past four, the decision's lists,
// the result bits of slots 4 on, the contact and target lists, the draw lists and the record pool. A match runs as many
// slots as its count ($gp = rw_match_count, read once as the match overlay starts). What stopped a non-Rumble match at
// four was not room but three things only the Royal Rumble did:
//   1. the count: only the Rumble's set-up (rw_rumble_fill) raised rw_match_count past 4;
//   2. the match records (0x80097FE0 + 0x114 x n: the moveset, name and looks the match unpacks for slot n; 40 records
//      of room): build_match_records (func_80136CC8) builds 0..3 from the slots (a singles match 0..1, slots 2 and 3
//      emptied), the Rumble every entrant -- so a non-Rumble slot 4+ unpacked an empty moveset and the first step
//      crashed in walk_step_length (the Lumberjack prototype's probes);
//   3. the slots' roles: the match type's set-up (func_80115000) makes every filled slot a side of the match (singles:
//      +0xE9 0x10, the target slot ^ 1), so an extra man would be a competitor and his fall would decide the match.
// This file does those three for any match type, after the type's own set-up, and leaves everything else to the type:
// who is legal and how the match ends stay the match type's own (its decision, rw/r_80117438.c, reads +0xE9 0x10).
// From the Royal Rumble it REUSES (not hosts on): the record build by entrant (func_8000BAD8 into record n, as
// build_match_records' Rumble branch does), the slot fill of slots 4 on (as rw_rumble_fill: the Superstar, look, the
// computer's controller, level) and the engine's 32-slot tables. Nothing here runs unless a match asked for extra slots
// (match_slots_add): every match with four or fewer is the game's own, byte for byte (docs\MATCH_SLOTS.md, the proof).
#include "arcade.h"
#include "gamefn.h"
#include "modding.h"     // (RECOMP_HOOK_RETURN: build_match_records)
#include "slots.h"
void removal_s(const char* s); void removal_num(s32 v); void removal_say(void); void removal_who(u32 id);   // (the game's log: roster.c)

GAMEFN(void, func_8000BAD8, 0x8000BAD8u, (u32 id, u32 costume, u32 record));   // a Superstar's match record (moves, name, looks)

#define MS_RECORDS        SIXA_80159990          // the wrestlers' records, 0x358 each (offsets below from the record)
#define MS_RECORD_SIZE    0x358u
#define MS_TARGET         0x08u   // s32: the slot he is after
#define MS_TEAM           0x0Cu   // s32: his side (the singles set-up gives slot | 2)
#define MS_STATE          0x14u   // s32: what he is doing
#define MS_AREA           0x82u   // u16: 1 ring, 2 apron, & 4 the floor outside
#define MS_PINNED         0x8Au   // u16: & 0x400 pinned (states 69 / 75: what the referee counts)
#define MS_RESULT         0x94u   // u16: & 8 pinned for three, & 0x200 lost, & 1 out of the match
#define MS_ID             0xA0u   // s16: the Superstar (-1 nobody)
#define MS_STAMINA        0xAAu   // s16
#define MS_ROLE           0xE9u   // u8: & 8 the computer's, & 0x10 a side of the match (the decision's), & 4 waiting outside, & 2 held back
#define MS_POS_X          0xFCu
#define MS_POS_Y          0x100u
#define MS_POS_Z          0x104u
#define MS_FACING         0x10Cu  // s32 0..4095: he faces (sin f, cos f)
#define MS_LAST_X         0x120u  // the last steps' positions (12 bytes each, to +0x15C): a teleport writes them too
#define MS_LAST_END       0x15Cu
#define MS_MATCH_RECORDS  0x80097FE0u   // 40 x 0x114: the match records (build_match_records zeroes 0x2B20)
#define MS_MATCH_RECORD   0x114u
#define MS_RECORDS_ROOM   40
#define MS_ENTRANT_IDS    0x800BD4D0u   // u16 a record: its Superstar (find_superstar_record reads them: names, looks)
#define MS_ENTRANT_LOOKS  0x800A9358u   // u8 a record: its look
#define MS_SLOT_EXTRAS    0x8051BFC0u   // 8 bytes a slot (room for 32): what set_up_wrestler_slot (func_80114DBC) starts him with --
                                        //  +0 stamina and its cap, +1..+5 the body parts' health, +7 the hurt walk's part
#define MS_SLOT_LEVEL     SIXA_8009ED10 // u16 a slot: the computer's level
#define MS_COUNT          SIX_MATCH_COUNT   // rw_match_count: the next match's slots (read once as the match starts)
#define MS_CUR_COUNT      SIX_CUR_COUNT     // rw_cur_count: this match's slots ($gp)
#define MS_PIN_MAN        0x80159204u   // s32: the man the referee is counting (-1 none; run_pin_count)
#define MS_PIN_TIMER      0x80189750u   // 12 a slot: the count's clock {minutes, seconds = the count, sub-steps}
#define MS_AI_CURRENT     0x8015A6F0u   // the computer player's record while it decides a slot (+0x0C held, +0x0E pressed)
#define MS_MATCH_FRAMES   0x801532C0u   // u16: 0xFFFF outside a match

#define MS_RD_F(a)        (*(volatile float*)(a))

// The model, per slot (docs\MATCH_SLOTS.md "The per-slot model"): a role, a side, and a state. The match type fills the
// competitors' (its own slots 0..3 as the game sets them up); the extra slots are filled here. Kept general for the
// match types to come (Survivor Series 3v3..8v8: a side per slot, legal / waiting / eliminated, corners for N partners).
// (the roles, states, rules and jobs: slots.h)
typedef struct {
    u8 role[NM_SLOTS_CAP];     // MS_EMPTY / MS_COMPETITOR / MS_EXTRA
    u8 side[NM_SLOTS_CAP];     // which side he is on (the extras: each his own, 8 + slot)
    u8 state[NM_SLOTS_CAP];    // MS_LEGAL / MS_WAITING / MS_ELIMINATED
    s16 stamina0[NM_SLOTS_CAP];
    u8 acted[NM_SLOTS_CAP], hit[NM_SLOTS_CAP], grappled[NM_SLOTS_CAP];
    int competitors, extras, slots, rule, wait_steps, steps, retarget, job;
    u32 random, refused, eliminated, ai_calls;
} MatchSlots;
static MatchSlots ms;
static int ms_pending, ms_pending_rule, ms_pending_wait, ms_pending_competitors, ms_pending_job;   // (asked for the next match)
int ms_live;                                   // this match has extra slots (the step and the CPU hook look at it)

static u32 ms_rec(int s) { return MS_RECORDS + MS_RECORD_SIZE * (u32)s; }
static u32 ms_rand(void) { ms.random = ms.random * 1103515245u + 12345u; return ms.random >> 8; }
static int ms_present(int s) { return RDS16(ms_rec(s) + MS_ID) != -1; }

// The extra slots for the next match: n wrestlers (ids / looks, id 0 none) into the slots after the match type's own
// competitors, their match records built, the count raised. Call it once the match type's slots are filled, before the
// match overlay starts; the request is kept and done again each time the match records are built (build_match_records
// empties them and a singles match's slots 2 and 3: the menus build them once more as they leave for the match), until
// the match's set-up takes it (match_slots_setup). rule: MS_RULE_*; wait: steps (30 a second) the extras stand first.
// Returns the slots the match will have (its count), or 0 when nothing was asked or there is no room.
static u16 rq_id[NM_SLOTS_CAP]; static u8 rq_look[NM_SLOTS_CAP]; static int rq_n, rq_base;

// ---- never a match with an empty record (lumberjack3, 2026-11-21) ----------------------------------------------------
// A match record is built only for a Superstar this install's roster has (func_8000BAD8 asks func_8000AE78 first). A story,
// a saved setting, a select page, a random draw or a QA scenario can still name one it doesn't -- the built-in 1999
// Undertaker (0x000A) where WrestleMania 2000's Undertaker folder replaces him -- and an empty record unpacks an empty
// moveset: the crash in walk_step_length the moment he walks (b6-b8's Lumberjack tornado). So: a competitor's slot whose
// record stays empty gets a stand-in -- his stock counterpart (the man a version is of), else the first Superstar of the
// roster not in the match -- and an extra slot whose record stays empty is left out. Each logged.
u32 variant_base_of(u16 id); u32 roster_ord_to_id(s32 ord);
static u32 ms_record(int n) { return MS_MATCH_RECORDS + MS_MATCH_RECORD * (u32)n; }
static int ms_record_ok(int n) { return RD8(ms_record(n) + 0xDCu) != 0; }   // (+0xDC his name: the build's)
static int ms_build(int n, u32 id, u32 look) {
    RD16(MS_ENTRANT_IDS + 2u * (u32)n) = (u16)id;
    RD8(MS_ENTRANT_LOOKS + (u32)n) = (u8)look;
    func_8000BAD8(id, look, ms_record(n));
    return ms_record_ok(n);
}
static int ms_in_match(u32 id, int upto, int not_n) {
    for (int j = 0; j < upto && j < NM_SLOTS_CAP; j++) if (j != not_n && (u32)RDS32(NM_SLOT_WRESTLER + 4u * (u32)j) == id) return 1;
    return 0;
}
// competitors 0 .. upto-1 (the slots holding a Superstar): an empty record built again, else a stand-in
static void ms_fix_competitors(int upto) {
    for (int c = 0; c < upto && c < 4; c++) {
        s32 id = RDS32(NM_SLOT_WRESTLER + 4u * (u32)c);
        if (id <= 0 || ms_record_ok(c)) continue;
        u32 look = (u32)RDS32(NM_SLOT_COSTUME + 4u * (u32)c) & 3u;
        if (ms_build(c, (u32)id, look)) continue;
        u32 got = 0;
        u32 base = variant_base_of((u16)id);
        if (base && base != (u32)id && !ms_in_match(base, 4, c) && ms_build(c, base, 0)) got = base;
        for (s32 o = 1; !got && o < 0x54; o++) {
            u32 t = roster_ord_to_id(o);
            if (t && t < 0x1000u && !ms_in_match(t, 4, c) && ms_build(c, t, 0)) got = t;
        }
        removal_s("slots: WARNING slot "); removal_num(c); removal_s("'s Superstar "); removal_num(id);
        removal_s(" is not on this install's roster (no match record can be built for him): ");
        if (got) {
            RDS32(NM_SLOT_WRESTLER + 4u * (u32)c) = (s32)got; RDS32(NM_SLOT_COSTUME + 4u * (u32)c) = 0;
            removal_s("stand-in "); removal_num((s32)got);
        } else removal_s("NO stand-in found");
        removal_say();
    }
}

// ---- nor a Royal Rumble (rumblecrash, 2026-11-22) ---------------------------------------------------------------------
// The Royal Rumble builds a match record for every entrant on its list (build_match_records' Rumble branch: entrants 0 ..
// the list's count - 1, from the list 0x800BD4D0 and the looks 0x800A9358 -- record n is entrant n's), and its slots take
// their men from that list as they come in. So the guard above, which looks at the slots, never saw a Rumble: an entrant
// this install's roster lacks got an empty record, and the moment he walked, walk_step_length read through it. Seen in QA
// music_rumble_late (2026-11-22): No Mercy Plus's THRASHER (0x030A), named by a test's list of ports, on an install made
// without No Mercy Plus -- slot 1 crashed about a second after the bell. A player's own draws ask the roster first
// (rumble_eligible); this is for anything else that names an entrant (a test, a story, a list kept from before). The same
// rule as the other match types: a stand-in -- his stock counterpart, else the first Superstar of the roster a Rumble may
// draw who is not on the list -- in his place on the list and in any match slot that holds him; each logged. With every
// entrant on the roster it finds nothing and writes nothing.
#define MS_ENTRANT_COUNT  0x800457ACu   // s32: how many entrants the list holds (entrant_list_count)
#define MS_SETUP_FLAGS    0x800A957Cu   // u32: the match type's set-up bits (0x200: a Royal Rumble -- its records are the list's)
static int ms_on_list(u32 id, int count, int not_n) {
    for (int j = 0; j < count; j++) if (j != not_n && RD16(MS_ENTRANT_IDS + 2u * (u32)j) == (u16)id) return 1;
    return 0;
}
static void ms_fix_rumble_entrants(void) {
    int count = RDS32(MS_ENTRANT_COUNT);
    if (count > MS_RECORDS_ROOM) count = MS_RECORDS_ROOM;          // (the records' room: build_match_records empties 40)
    for (int n = 0; n < count; n++) {
        const u32 id = RD16(MS_ENTRANT_IDS + 2u * (u32)n);
        const u32 look = RD8(MS_ENTRANT_LOOKS + (u32)n);
        if (id == 0 || id >= 0x1000u || ms_record_ok(n)) continue;  // (0 / 0xFFFF: no entrant; a record: he is on the roster)
        u32 got = 0;
        const u32 base = variant_base_of((u16)id);
        if (base && base != id && !ms_on_list(base, count, n) && ms_build(n, base, 0)) got = base;
        for (s32 o = 1; !got && o < 0x54; o++) {
            const u32 t = roster_ord_to_id(o);
            if (t && t < 0x1000u && !ms_on_list(t, count, n) && plat_game_call(GF_RR_ELIGIBLE, t, 0, 0) && ms_build(n, t, 0)) got = t;
        }
        removal_s("slots: WARNING Royal Rumble entrant "); removal_num(n); removal_s(", "); removal_who(id);
        removal_s(", is not on this install's roster (no match record can be built for him): ");
        if (got) {
            // (the match slots already holding him -- the Rumble's first men are in theirs before the bell)
            for (int s = 0; s < NM_SLOTS_CAP; s++)
                if ((u32)RDS32(NM_SLOT_WRESTLER + 4u * (u32)s) == id) { RDS32(NM_SLOT_WRESTLER + 4u * (u32)s) = (s32)got; RDS32(NM_SLOT_COSTUME + 4u * (u32)s) = 0; }
            removal_s("stand-in "); removal_who(got);
        } else {
            RD16(MS_ENTRANT_IDS + 2u * (u32)n) = (u16)id; RD8(MS_ENTRANT_LOOKS + (u32)n) = (u8)look;   // (as it was: the tries wrote theirs)
            removal_s("NO stand-in found");
        }
        removal_say();
    }
}
static int ms_apply(void) {
    ms_pending = 0;
    if (rq_n <= 0) return 0;
    if (RD32(NM_MATCH_FLAGS) & 0x4000u) return 0;                 // (the Royal Rumble has its own entrants)
    int base = rq_base;                                            // the type's competitors (counted when asked)
    if (base < 1) return 0;
    int n = rq_n;
    if (base + n > NM_SLOTS_CAP) n = NM_SLOTS_CAP - base;
    if (base + n > MS_RECORDS_ROOM) n = MS_RECORDS_ROOM - base;
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (!rq_id[i]) continue;
        int s = base + k++;
        u32 id = rq_id[i], look = rq_look[i];
        // the slot (as rw_rumble_fill fills a Rumble's slots 4 on): the Superstar, his look, the computer's, his level
        RDS32(NM_SLOT_WRESTLER + 4u * (u32)s) = (s32)id;
        RDS32(NM_SLOT_COSTUME + 4u * (u32)s) = (s32)look;
        RDS32(NM_SLOT_PAD + 4u * (u32)s) = 0;
        RD16(MS_SLOT_LEVEL + 2u * (u32)s) = RD16(MS_SLOT_LEVEL);  // (slot 0's level: the match's computer level)
        // his start (lumberjack3, 2026-11-21): the slot's 8 bytes as the game's reset gives them (func_80109238: stamina 0xFF,
        // the five body parts 50, nothing hurt) -- the menus only ever fill slots 0..3's, so an extra slot started with
        // whatever those bytes held: stamina 5..240, every body part 0 (the hurt walk: "the lumberjacks look hurt")
        {   u32 e = MS_SLOT_EXTRAS + 8u * (u32)s;
            RD8(e) = 0xFF;
            for (u32 b = 1; b <= 5; b++) RD8(e + b) = 0x32;
            RD8(e + 6) = 0; RD8(e + 7) = 0;
        }
        if (s < 4) RD32(NM_MATCH_FLAGS) |= 1u << s;               // (slots 2, 3: the match flags' computer bits)
        // his match record (as build_match_records builds a Rumble entrant's): the moveset the match unpacks for slot s,
        // the name and the looks its lookups find by his ID in the list
        if (!ms_build(s, id, look)) {                             // (not on this roster: left out -- lumberjack3)
            removal_s("slots: WARNING extra Superstar "); removal_num((s32)id); removal_s(" is not on this install's roster (no match record): left out"); removal_say();
            RDS32(NM_SLOT_WRESTLER + 4u * (u32)s) = -1; RDS32(NM_SLOT_PAD + 4u * (u32)s) = -1; RDS32(NM_SLOT_COSTUME + 4u * (u32)s) = 0;
            RD16(MS_ENTRANT_IDS + 2u * (u32)s) = 0xFFFF; RD8(MS_ENTRANT_LOOKS + (u32)s) = 0xFF;
            if (s < 4) RD32(NM_MATCH_FLAGS) &= ~(1u << s);
            k--;
        }
    }
    if (!k) return 0;
    ms_fix_competitors(base);                                      // (an empty competitor record: lumberjack3, above)
    int slots = base + k;
    RDS32(MS_COUNT) = slots < 4 ? 4 : slots;                       // (the engine's count: never below the stock four)
    ms_pending = k; ms_pending_competitors = base;
    removal_s("slots: extra slots asked: competitors "); removal_num(base); removal_s(", extras "); removal_num(k); removal_s(", count "); removal_num(RDS32(MS_COUNT)); removal_say();
    return slots;
}
// match_slots_add_at: the same with the type's competitors given (base: the first extra slot) and the extras' job -- a
// one-on-one's slots 2 and 3 can still hold an earlier tag match's men, whom counting the filled slots would take for the
// type's own (New feature (Ultimate No Mercy) -- lumberjack, 2026-11-21).
int match_slots_add_at(int base, int n, const u16* ids, const u8* looks, int rule, int wait, int job) {
    ms_live = 0; ms_pending_competitors = 0;
    if (n > NM_SLOTS_CAP) n = NM_SLOTS_CAP;
    rq_n = n < 0 ? 0 : n;
    for (int i = 0; i < rq_n; i++) { rq_id[i] = ids[i]; rq_look[i] = looks ? looks[i] : 0; }
    ms_pending_rule = rule; ms_pending_wait = wait; ms_pending_job = job;
    rq_base = base;
    return ms_apply();
}
int match_slots_add(int n, const u16* ids, const u8* looks, int rule, int wait) {
    int base = 0;                                                  // the type's competitors: its filled slots 0..3
    for (int s = 0; s < 4; s++) if (RDS32(NM_SLOT_WRESTLER + 4u * (u32)s) > 0) base = s + 1;
    return match_slots_add_at(base, n, ids, looks, rule, wait, MS_JOB_NONE);
}
// The match records have been built (build_match_records, func_80136CC8: the menus, the title's demo set-up): a
// request for extra slots is done again over them. Nothing at all without one.
RECOMP_HOOK_RETURN("func_80136CC8") void match_slots_on_records(void) {
    // (lumberjack3: any match but a Rumble -- whose records are its entrants' -- never starts with an empty competitor
    //  record; with every Superstar on the roster this finds nothing and writes nothing)
    if (!(RD32(NM_MATCH_FLAGS) & 0x4000u)) ms_fix_competitors(4);
    if (RD32(MS_SETUP_FLAGS) & 0x200u) ms_fix_rumble_entrants();   // (a Royal Rumble's records are its list's: rumblecrash, above)
    if (lumberjack_on_records()) return;                           // (a Lumberjack match asks for its own: lumberjack.c)
    if (rq_n > 0) ms_apply();
}

// The match's memory has been set up (set_up_match_memory, func_80113E30, as the match starts): a one-on-one makes its
// slots past 1 run-in men held back (+0xE9 = 2, +0xF1 = 2: func_80115000 then hides them until their run-in) or men
// placed outside (+0xE9 = 1), and a handicap match its slot 3. An extra slot is neither: those bits cleared, so the
// type's set-up places and sides him as any present man, and match_slots_setup gives him his role. Nothing without a
// request (ms_pending).
RECOMP_HOOK_RETURN("func_80113E30") void match_slots_on_memory(void) {
    if (!ms_pending) return;
    for (int s = ms_pending_competitors; s < ms_pending_competitors + ms_pending && s < NM_SLOTS_CAP; s++) {
        RD8(ms_rec(s) + MS_ROLE) &= (u8)~3u;
        RD8(ms_rec(s) + 0xF1u) &= (u8)~2u;
    }
}

// facing (0..4095) toward the direction (dx, dz): f with (sin f, cos f) along it
static s32 ms_facing(float dx, float dz) {
    float ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
    if (ax < 1e-3f && az < 1e-3f) return 0;
    float t = ax < az ? ax / az : az / ax;                        // atan(t), t in 0..1: within 0.005 rad
    float a = t * (0.7853982f + 0.2730f * (1.0f - t));
    if (ax > az) a = 1.5707963f - a;                              // angle from +z toward +x, 0..pi/2
    if (dz < 0) a = 3.1415927f - a;
    if (dx < 0) a = -a;
    s32 f = (s32)(a * (4096.0f / 6.2831853f));
    return f & 4095;
}
static void ms_place(int s, float x, float z) {
    u32 r = ms_rec(s);
    MS_RD_F(r + MS_POS_X) = x; MS_RD_F(r + MS_POS_Y) = 0.0f; MS_RD_F(r + MS_POS_Z) = z;
    for (u32 a = MS_LAST_X; a + 8u <= MS_LAST_END; a += 12u) { MS_RD_F(r + a) = x; MS_RD_F(r + a + 4u) = 0.0f; MS_RD_F(r + a + 8u) = z; }
    RD16(r + MS_AREA) = 4;                                         // (the floor outside)
    RDS32(r + MS_FACING) = ms_facing(-x, -z);                      // (facing the ring)
}
// a place on the square 360 out from the middle (the apron ends about 310), the extras evenly round it
static void ms_post(int i, int n, float* x, float* z) {
    const float h = 360.0f;
    float u = (8.0f * h) * ((float)i + 0.5f) / (float)n;           // (along the square's edge, from its corner -h, -h)
    if (u < 2 * h) { *x = -h + u; *z = -h; }
    else if (u < 4 * h) { *x = h; *z = -h + (u - 2 * h); }
    else if (u < 6 * h) { *x = h - (u - 4 * h); *z = h; }
    else { *x = -h; *z = h - (u - 6 * h); }
}
static int ms_pick_target(int s) {
    int c[NM_SLOTS_CAP], n = 0;
    for (int t = 0; t < ms.slots; t++)
        if (t != s && ms.role[t] != MS_EMPTY && ms.state[t] != MS_ELIMINATED && ms_present(t)) c[n++] = t;
    return n ? c[ms_rand() % (u32)n] : s;
}

// The match's set-up is done (the type's func_80115000 has placed and sided its slots): the extra slots get their
// roles -- the computer's, not a side of the match (+0xE9 0x10 clear: the type's decision never counts them), a side of
// their own, a target -- and their places round the ring. Every match: a match with no extra slots clears the model.
void match_slots_setup(void) {
    ms_live = 0;
    if (!ms_pending) return;
    int count = RDS32(MS_CUR_COUNT);
    if (count < ms_pending_competitors + ms_pending) { ms_pending = 0; rq_n = 0; return; }   // (not the match it was asked for)
    for (int s = 0; s < NM_SLOTS_CAP; s++) { ms.role[s] = MS_EMPTY; ms.side[s] = 0; ms.state[s] = MS_LEGAL; ms.acted[s] = ms.hit[s] = ms.grappled[s] = 0; }
    ms.competitors = ms_pending_competitors; ms.extras = ms_pending; ms.slots = ms_pending_competitors + ms_pending;
    ms.rule = ms_pending_rule; ms.wait_steps = ms_pending_wait; ms.steps = 0; ms.retarget = 0; ms.job = ms_pending_job;
    ms.refused = ms.eliminated = ms.ai_calls = 0; ms.random = 0x2545F491u;
    ms_pending = 0; rq_n = 0;                                      // (the request is this match's: taken)
    for (int s = 0; s < ms.competitors; s++) if (ms_present(s)) { ms.role[s] = MS_COMPETITOR; ms.side[s] = (u8)RDS32(ms_rec(s) + MS_TEAM); }
    for (int i = 0; i < ms.extras; i++) {
        int s = ms.competitors + i; u32 r = ms_rec(s);
        if (!ms_present(s)) continue;
        ms.role[s] = MS_EXTRA; ms.side[s] = (u8)(8 + s);
        RD8(r + MS_ROLE) = (u8)((RD8(r + MS_ROLE) | 8u) & ~0x16u);  // (the computer's; not a side, not waiting, not held back)
        RDS32(r + MS_TEAM) = 8 + s;
        float x, z; ms_post(i, ms.extras, &x, &z); ms_place(s, x, z);
        ms.stamina0[s] = RDS16(r + MS_STAMINA);
    }
    for (int i = 0; i < ms.extras; i++) { int s = ms.competitors + i; if (ms.role[s] == MS_EXTRA) RDS32(ms_rec(s) + MS_TARGET) = ms_pick_target(s); }
    ms_live = 1;
    if (ms.job == MS_JOB_LUMBERJACK) lumberjack_setup();
    removal_s("slots: the match set up: count "); removal_num(count); removal_s(", competitors "); removal_num(ms.competitors); removal_s(", extras "); removal_num(ms.extras); removal_say();
}

// An overlay has been loaded (func_8000073C's return): after a match with extra slots -- the menus' overlay coming back
// -- the menus get the match type's own slots again: the extra slots emptied as the type's set-up leaves an unused slot
// (no Superstar, no controller), their records' IDs out of the list, the computer bits of slots 2 and 3 the type's.
// The post-match menus read slots 0..3 as the match's players: a singles match's slots 2 and 3 left filled crashed them
// (give_page_row_objects, any32_singles12 / elim12, 2026-11-18).
void match_slots_after(void) {
    if (!ms_live) return;
    if (RD16(MS_MATCH_FRAMES) != 0xFFFF) return;
    for (int s = ms.competitors; s < ms.slots && s < NM_SLOTS_CAP; s++) {
        if (ms.role[s] != MS_EXTRA) continue;
        RDS32(NM_SLOT_WRESTLER + 4u * (u32)s) = -1;
        RDS32(NM_SLOT_PAD + 4u * (u32)s) = -1;
        RDS32(NM_SLOT_COSTUME + 4u * (u32)s) = 0;
        if (s < MS_RECORDS_ROOM) { RD16(MS_ENTRANT_IDS + 2u * (u32)s) = 0xFFFF; RD8(MS_ENTRANT_LOOKS + (u32)s) = 0xFF; }
        if (s < 4) RD32(NM_MATCH_FLAGS) &= ~(1u << s);
    }
    { extern void tron_forget_patches(void); tron_forget_patches(); }   // (the arena's memory is the menus' now: tron.c)
    if (ms.job == MS_JOB_LUMBERJACK) lumberjack_after();
    ms_live = 0;
    removal_s("slots: the match is over: the extra slots emptied"); removal_say();
}

// The computer player has decided slot s's buttons (ovl_e func_800F03F0's return): an extra waiting at his place holds
// still; an eliminated one stays out of it. Otherwise the game's own choice stands (the extras' moves are the AI's).
void match_slots_cpu(s32 s) {
    if (ms_live && ms.job == MS_JOB_LUMBERJACK) { lumberjack_cpu(s); return; }   // (every slot: lumberjack.c)
    if (!ms_live || s < 0 || s >= ms.slots || ms.role[s] != MS_EXTRA) return;
    u32 ai = RD32(MS_AI_CURRENT);
    if (ai < 0x80000000u || ai >= 0x80800000u) return;
    if (ms.steps < ms.wait_steps || ms.state[s] == MS_ELIMINATED) { RD16(ai + 0x0C) = 0; RD16(ai + 0x0E) = 0; return; }
    ms.ai_calls++;
    if (RD16(ai + 0x0E) || RD16(ai + 0x0C)) ms.acted[s] = 1;
}

// After every step of the match: the referee and the extras (the match type's rule), the targets, the QA counters.
void match_slots_step(void) {
    if (!ms_live) return;
    if (ms.job == MS_JOB_LUMBERJACK) lumberjack_lead_in();      // (lumberjack2: before the bell too -- the walk-out, lumberjack.c)
    if (RD16(MS_MATCH_FRAMES) == 0xFFFF) return;              // (outside the match: match_slots_after ends it)
    ms.steps++;
    // the referee: a count on an extra is the type's rule's
    s32 man = RDS32(MS_PIN_MAN);
    if (man >= 0 && man < ms.slots && ms.role[man] == MS_EXTRA) {
        u32 t = MS_PIN_TIMER + 12u * (u32)man;
        int stop = 0;
        if (ms.rule == MS_RULE_RINGSIDE || ms.state[man] == MS_ELIMINATED) { stop = 1; if (RDS32(t + 8) == 1 && RDS32(t + 4) == 0) ms.refused++; }
        else if (RDS32(t + 4) >= 2 && RDS32(t + 8) >= 28) {        // (elimination: his three -- before the game's own three,
            stop = 1; ms.state[man] = MS_ELIMINATED; ms.eliminated++;   //  which the type's decision would take as its fall)
            RD8(ms_rec(man) + MS_ROLE) &= (u8)~0x10u;
        }
        if (stop) { RDS32(MS_PIN_MAN) = -1; RDS32(t) = 0; RDS32(t + 4) = 0; RDS32(t + 8) = 0; }
    }
    if (ms.job == MS_JOB_LUMBERJACK) { lumberjack_step(); return; }   // (the lumberjacks' targets are their job's)
    // the targets: every few seconds, or when his man is out, an extra goes after another man present
    if (++ms.retarget >= 240) ms.retarget = 0;
    for (int s = ms.competitors; s < ms.slots; s++) {
        if (ms.role[s] != MS_EXTRA) continue;
        u32 r = ms_rec(s);
        s32 tg = RDS32(r + MS_TARGET);
        if (tg < 0 || tg >= ms.slots || tg == s || ms.state[tg] == MS_ELIMINATED || (ms.retarget == (s * 7) % 240 && ms.steps > ms.wait_steps))
            RDS32(r + MS_TARGET) = ms_pick_target(s);
        s16 st = RDS16(r + MS_STAMINA);
        if (st < ms.stamina0[s]) ms.hit[s] = 1;
        s32 state = RDS32(r + MS_STATE);
        if (state == 80 || state == 81) ms.grappled[s] = 1;
    }
}

// The match HUD has updated (func_800DEDE0's return, before the Rumble's own): the HUD stays the match type's -- an
// extra slot has no panel (its frame, bar and name hidden as an empty Rumble slot's are: logic.c arcade_rr_hud). A tag
// match's panel count is the engine's count, so without this its extras would get panels past the six places.
void match_slots_hud(void) {
    static const u32 obj[3] = { SIXA_80154668, SIXA_801556F0, SIXA_80155B50 };
    if (!ms_live) return;
    for (int s = ms.competitors; s < ms.slots; s++) {
        if (ms.role[s] != MS_EXTRA) continue;
        for (int k = 0; k < 3; k++) {
            u32 inst = RD32(obj[k] + 0xDCu * (u32)s + 0x14u);
            if (inst >= 0x80000400u && inst < 0x80800000u) RD32(inst) |= 0x80u;
        }
    }
}

// QA: the extras' numbers for state.csv's spare columns (bs_tries / bs_grabs / bs_step: logic.c writes them when a
// scenario has extra slots): bs_tries extras present | acted << 8 | hit << 16 | grappled << 24; bs_grabs the referee's
// counts refused | extras eliminated << 16; bs_step the engine's count ($gp) | competitors << 8 | rule << 16 | joined in << 24
void match_slots_qa(u32 q) {
    u32 present = 0, acted = 0, hit = 0, grappled = 0;
    for (int s = ms.competitors; s < ms.slots; s++) {
        if (ms.role[s] != MS_EXTRA || !ms_present(s)) continue;
        present++; acted += ms.acted[s]; hit += ms.hit[s]; grappled += ms.grappled[s];
    }
    RD32(q + 0) = present | acted << 8 | hit << 16 | grappled << 24;    // bs_tries
    RD32(q + 4) = (ms.refused & 0xFFFF) | ms.eliminated << 16;          // bs_grabs
    RD32(q + 8) = (u32)RDS32(MS_CUR_COUNT) | (u32)ms.competitors << 8 | (u32)ms.rule << 16 | (ms.steps >= ms.wait_steps ? 1u : 0u) << 24;   // bs_step
}

// The model as the jobs read it (lumberjack.c): the slots in use, the type's competitors, a slot's role, the steps so far.
int match_slots_count(void) { return ms_live ? ms.slots : 0; }
int match_slots_competitors(void) { return ms.competitors; }
int match_slots_is_extra(int s) { return ms_live && s >= 0 && s < ms.slots && ms.role[s] == MS_EXTRA; }
int match_slots_steps(void) { return ms.steps; }
