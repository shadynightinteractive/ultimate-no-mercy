// Every arena: the ring and its stage one fightable area (data/ringstage/RINGSTAGE.md, data/stagecollide/COLLISION.md; the user, 2026-11-02: "Combine the in-ring ring area
// and the closeup stage area ... No transitions"; later: "every stage playable on the closeup ramp without transition, even the Revenge and WM 2000 ones. Use collision detection based on the
// models"). Each arena's walls and floors are its own data, made from its models (gen/arenas_collide.py, gen/arenas_walls.py: src/arena_walls_files.h). What code can't get from data, here: the game
// keeps a man's y at 0 or 90 by writing those numbers (16 + writes of +0x100), so his y stays the game's and his pose is lifted by the ground under him (func_8012F9CC, one gateway: every bone,
// the camera and the shadow follow); the count-out as in the stage area (none) past the old door line; the door at the back of the stage; the CPU's way round.
// With none taken (the ring alone) every function here is what the game did.
#include "arcade.h"
#include "arena_ringstage_files.h"

#define RS_AREA    0x80159628u   // s32: the area in play (0-9 a ring venue, 10-19 a stage area, 20.. backstage)
#define RS_SELF    0x80159674u   // float x, y, z: the CPU's own place (func_800F03F0, from +0x120)
#define RS_OPP     0x8016B9C0u   // float x, y, z: his opponent's
#define F32(a)     (*(volatile float*)(a))
#define MAN(s)     (SIXA_80159990 + 0x358u * (u32)(s))
// (lumberjack2, 2026-11-21: every slot the engine has -- the Lumberjack match's walk-out brings men 6 .. 31 down a raised
//  stage and ramp too; with 6 they walked at y 0 inside it. An empty slot's +0xA0 is -1, as before.)
#define RS_SLOTS   NM_SLOTS_CAP
#define RS_RATE    8.0f          // (a pose's lift moves at most this per step: two men in a hold on the ramp, a throw over its edge)

s32 ringstage_on;
float ringstage_h;                                 // (func_8012F9CC: the lift of this call)
float arena_count_z = 1e9f;                        // func_801166B8: a man past this z isn't counted
u32 arena_man_z0 = SIXA_80159990 + 0x104u;         // (the man table's +0x104: func_801166B8's copy adds its offset)
u32 arena_arrive = 0x8014638Cu;                    // func_80115000: the arrival spots (s16 x, z, facing by index)
s32 arena_ai_area;                                 // func_800F6154 (the CPU's "back to the ring"): the area, as he's to see it (ringstage_ai)
s32 dbg_ringstage[4];                              // tests: lifts asked, door refused, ai steers, replay lifts
float h_now[RS_SLOTS];                             // the lift of each man's pose (func_80142B28 / func_80141508: a weapon's height against his)

static inline float abs_f(float v) { return v < 0.0f ? -v : v; }
static const XrsArena* ra;                         // this arena's map (src/arena_ringstage_files.h)
static inline int cell_at(int i, int k) { return (i < 0 || k < 0 || i >= ra->w || k >= ra->h) ? 255 : ra->cells[k * ra->w + i]; }
// the ground under a place: the map's heights, between its cells' middles (cells not walkable don't count: a man thrown past an edge stands at the edge's height)
static float ground(float x, float z) {
    if (!ra || z < (float)ra->z0) return 0.0f;
    float fx = (x - (float)ra->x0) / (float)XRS_GRID - 0.5f, fz = (z - (float)ra->z0) / (float)XRS_GRID - 0.5f;
    int i = (int)(fx + 1000.0f) - 1000, k = (int)(fz + 1000.0f) - 1000;
    float tx = fx - (float)i, tz = fz - (float)k, sum = 0.0f, w = 0.0f;
    for (int b = 0; b < 2; b++) for (int a = 0; a < 2; a++) {
        int h = cell_at(i + a, k + b);
        if (h == 255) continue;
        float q = (a ? tx : 1.0f - tx) * (b ? tz : 1.0f - tz);
        sum += q * (float)h; w += q;
    }
    if (w > 0.001f) return 2.0f * sum / w;
    // (off the map: the nearest cell within two)
    int ci = (int)((x - (float)ra->x0) / (float)XRS_GRID), ck = (int)((z - (float)ra->z0) / (float)XRS_GRID);
    for (int r = 1; r <= 2; r++) for (int b = -r; b <= r; b++) for (int a = -r; a <= r; a++) {
        int h = cell_at(ci + a, ck + b);
        if (h != 255) return 2.0f * (float)h;
    }
    return 0.0f;
}
static int active(void) { return ringstage_on && RDS32(RS_AREA) < 10; }
int ringstage_front(void) { return ra ? ra->front : 2305; }   // (QA: how far up the walker goes)
// the floor's height at a place, by this arena's map (0: no map) -- the Lumberjack intro's camera on the stage (lumberjack.c)
float ringstage_ground_at(float x, float z) { return active() ? ground(x, z) : 0.0f; }

static s16 arrive_spots[84] __attribute__((aligned(4)));
void ringstage_take(int a) {                       // (arena.c arena_take: every ring build: the arena, or < 0 the ring alone)
    ra = (a >= 0 && a < XRS_N && xrs_arenas[a].cells) ? &xrs_arenas[a] : 0;
    ringstage_on = ra != 0;
    arena_count_z = ra ? (float)XRS_COUNT_Z : 1e9f;
    if (ra) {
        for (u32 i = 0; i < 84u; i++) arrive_spots[i] = xrs_spots[i];
        arrive_spots[0] = ra->ax; arrive_spots[1] = ra->az; arrive_spots[2] = 2048;
    }
    arena_arrive = ra ? (u32)arrive_spots : 0x8014638Cu;
    for (int i = 0; i < RS_SLOTS; i++) h_now[i] = 0.0f;
}

// Where a Royal Rumble's entrants come in (2026-11-15, the user's playtest: "in a Nitro Royal Rumble entrants first appear behind the
// Nitro stage logo, then phase through it"). In a ring venue func_8011D848 puts each at x 0, z = the s16 at 0x801465D0[venue] (ovl_e's
// data; 2600 for venue 7, the venue an arcade arena plays as), facing the ring, and walks him in along x = 0 with no wall test. The game
// draws its stage set behind everything, so on the N64 an entrant behind a piece of the set showed in front of it all the same; drawn
// with depth (the exe's renderer, since 2026-11-09) he is hidden behind it until he walks through it -- Nitro's 3D stage ends at z 2128
// with its logo board standing at z 2214 (x +-84, y 34..202), on his line. So an arcade arena's entrants start at its own arrival spot
// (xrs az: on x 0, 80 in front of the back of its walkable stage -- Nitro's 2050, inside the gate), and the game's venues at the game's
// own. The table is checked for the game's other values first: at 0x801465D0 another overlay has code.
#define RR_START_Z 0x801465D0u
static const s16 rr_start_rom[10] = { 2450, 2550, 2800, 2250, 2300, 2530, 2350, 2600, 2600, 2170 };
void ringstage_rumble_start(int venue, int arcade) {     // (arena.c arena_take: every ring build, after ringstage_take)
    if (venue < 0 || venue >= 10) return;
    for (int v = 0; v < 10; v++) if (v != venue && RDS16(RR_START_Z + 2u * (u32)v) != rr_start_rom[v]) return;
    s16 z = rr_start_rom[venue];
    if (arcade && ra && ra->ax == 0 && ra->az > 0 && ra->az < z) z = ra->az;
    RDS16(RR_START_Z + 2u * (u32)venue) = z;
}

// func_8012F9CC (a man's pose): the lift for this man -> added to the y it hands func_80012130 and taken off what comes back
float ringstage_lift(u32 man) {
    if (!active()) return 0.0f;
    s32 s = RDS16(man + 0x9Eu);
    if (s < 0 || s >= RS_SLOTS) return 0.0f;
    if (RDS32(man + 0x14u) == 163 && ra->own_y) return 0.0f;   // (a Rumble's entrant coming down the ramp: func_8011D848 gives him its real y itself, where the game has one: RAW's, SmackDown's)
    float x = F32(man + 0xFCu), z = F32(man + 0x104u);
    // two men holding each other stand on one height: their middle's (+0x80 & 1: in a move together, +0x8 the other man)
    if (RD16(man + 0x80u) & 1u) {
        s32 o = RDS32(man + 0x8u);
        if (o >= 0 && o < RS_SLOTS && o != s) {
            float oz = F32(MAN(o) + 0x104u);
            if (abs_f(oz - z) < 300.0f) { z = 0.5f * (z + oz); x = 0.5f * (x + F32(MAN(o) + 0xFCu)); }
        }
    }
    float want = ground(x, z), d = want - h_now[s];
    if (d > 60.0f || d < -60.0f) h_now[s] = want;   // (put somewhere else: a new area, a scenario)
    else if (d > RS_RATE) h_now[s] += RS_RATE;
    else if (d < -RS_RATE) h_now[s] -= RS_RATE;
    else h_now[s] = want;
    dbg_ringstage[0]++;
    return h_now[s];
}
// func_8013140C (the replay's pose): the recorded place, the same lift
void ringstage_replay_lift(float* pos) {
    if (!active()) return;
    pos[1] += ground(pos[0], pos[2]);
    dbg_ringstage[3]++;
}

// func_800E1E60 -> func_800E2344 (the frame's camera). The user, 2026-11-02, on the fights on the stage: "look at the old version and try to copy
// the camera angle ... a little tighter on the wrestler and a little more angled down ... a little jerky as we move onto the stage".
// The old stage area's camera is func_800E30D8, the area-10 case of its switch (eye = the men's height + 100 + a share of their spread, 400 +
// a share of it back from the nearest man; looking at their middle): the stock game goes there because func_800DAAE4 turns the camera's mode
// 0x80159074 on for any area past 9 (the rig, data/ringstage/scripts/camroute.py; the ring's two men go to func_800E38A4 instead).
// Here the ring's camera runs as ever on its own state, then func_800E30D8, told it's area 10, on a state of its own, and the frame's camera is
// the one blended into the other by how far up the ramp the camera's men are: nothing to jump, wherever they are. The ring's
// special shots (func_800E4B60; the game's func_800E2344 sets 0x80158EA0, or its code past 0x20) are the ring's, whole.
#define CAM_AREA     0x800A8968u
#define CAM_STATE    0x80159024u         // (6 floats now, 6 floats wanted: eye, at)
#define CAM_COPY     0x80159058u         // (6 floats)
#define CAM_FINAL    0x80158FF4u         // (6 floats: func_800E2284)
#define CAM_SHOT     0x801594D2u
#define CAM_MODE     0x80159074u
#define CAM_SPECIAL  0x80158EA0u
#define CAM_BLEND_Z  500.0f              // (the blend: over this much of the way, ending
#define CAM_BLEND_BACK 200.0f            //  this far short of the stage's front: their spread is in that number)
#define CAM_BLEND_LOW 900.0f             // (and never starting nearer the ring than this)
#define CAM_BLEND_STEP 0.02f             // (and no faster than this a frame: a man thrown)
extern void func_800E2344(s32 a);
extern void func_800E30D8(void);
static float cam_deck[18], cam_w;
static int cam_deck_on;
float dbg_cam_w; u32 dbg_cam[2];         // (QA, mode 64: bs_grabs the frame's eye z | looked-at z << 16, rr_put_out / rr_kept_out their y)
static inline void cam_copy_in(const float* v) {
    for (u32 k = 0; k < 12u; k++) F32(CAM_STATE + 4u * k) = v[k];
    for (u32 k = 0; k < 6u; k++) F32(CAM_COPY + 4u * k) = v[12u + k];
}
static inline void cam_copy_out(float* v) {
    for (u32 k = 0; k < 12u; k++) v[k] = F32(CAM_STATE + 4u * k);
    for (u32 k = 0; k < 6u; k++) v[12u + k] = F32(CAM_COPY + 4u * k);
}
// The stage area's camera sits 400 and more back from the men; where the stage starts right behind the crowd's rows (SURVIVOR SERIES, KING OF THE RING ..: z 1470, the rows to 1420) that's in among the
// crowd's boards, the picture all boards (QA 2026-11-02 crash_survivor_series). No crowd in the old stage area. Here: as high as it takes to see the men over the rows, where its line to them crosses them.
#define CAM_AISLE_CLEAR 180.0f           // (the aisle's gap between the rows)
#define CAM_OVER_MAX 900.0f
static void cam_over_crowd(float* v) {   // (eye x y z, at x y z)
    float cz = (float)ra->crowd_z, ez = v[2], az = v[5];
    if (cz <= 0.0f || ez >= cz || az <= cz + 10.0f) return;
    float t = (cz - ez) / (az - ez), xr = abs_f(v[0] + t * (v[3] - v[0]));
    float k = (xr - (CAM_AISLE_CLEAR - 60.0f)) / 60.0f;      // (in the aisle's gap: as it was; coming to the rows: by degrees)
    if (k <= 0.0f) return;
    if (k > 1.0f) k = 1.0f;
    float need = v[4] + ((float)ra->crowd_y + 20.0f - v[4]) * (az - ez) / (az - cz);
    if (need > CAM_OVER_MAX) need = CAM_OVER_MAX;
    if (need > v[1]) v[1] += (need - v[1]) * k;
}
static void ringstage_cam_step(s32 a) {
    if (!active()) { cam_deck_on = 0; cam_w = 0.0f; func_800E2344(a); return; }
    // (all the way on the stage: its camera mode too, so that the game's special shots are as few as the stage area's -- func_800E2344 takes
    //  them from code 0x20 there, from 0x14 in the ring's mode: QA 2026-11-02_114912, the camera thrown about the deck)
    s16 mode_was = RDS16(CAM_MODE);
    if (cam_w >= 0.999f) RDS16(CAM_MODE) = 1;
    func_800E2344(a);
    RDS16(CAM_MODE) = mode_was;
    if ((a & 0xFFFF) != 0) return;       // (nothing computed: the frame's camera stands)
    float ring_state[18], ring_final[6], deck_final[6];
    for (u32 k = 0; k < 6u; k++) ring_final[k] = F32(CAM_FINAL + 4u * k);
    cam_copy_out(ring_state);
    int first = !cam_deck_on;
    if (first) { for (u32 k = 0; k < 18u; k++) cam_deck[k] = ring_state[k]; cam_deck_on = 1; }
    cam_copy_in(cam_deck);
    s16 was = RDS16(CAM_AREA);
    RDS16(CAM_AREA) = 10;
    func_800E30D8();
    if (first) {                         // (arrived here: on it, not coming to it)
        for (u32 k = 0; k < 6u; k++) F32(CAM_STATE + 4u * k) = F32(CAM_STATE + 24u + 4u * k);
        func_800E30D8();
    }
    RDS16(CAM_AREA) = was;
    cam_copy_out(cam_deck);
    for (u32 k = 0; k < 6u; k++) deck_final[k] = F32(CAM_FINAL + 4u * k);
    float deck_z_seen = deck_final[2];
    cam_over_crowd(deck_final);
    // how far up the camera's men are: where the stage camera's own eye is (400 + a share of their spread short of the nearest of the men it
    // frames -- its test of who they are, a tag's partner on the apron not one): the far from the ring, the more of it
    float z0 = (float)ra->front - CAM_BLEND_BACK - CAM_BLEND_Z;
    if (z0 < CAM_BLEND_LOW) z0 = CAM_BLEND_LOW;
    float t = (deck_z_seen + 400.0f - z0) / CAM_BLEND_Z;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    t = t * t * (3.0f - 2.0f * t);
    if (first) cam_w = t;
    cam_copy_in(ring_state);
    float d = t - cam_w;
    cam_w = d > CAM_BLEND_STEP ? cam_w + CAM_BLEND_STEP : d < -CAM_BLEND_STEP ? cam_w - CAM_BLEND_STEP : t;
    dbg_cam_w = cam_w;
    float w = ((RD16(CAM_SHOT) & 0x3Fu) >= 0x20u || RDS16(CAM_SPECIAL) == 1) ? 0.0f : cam_w;
    for (u32 k = 0; k < 6u; k++) F32(CAM_FINAL + 4u * k) = ring_final[k] + (deck_final[k] - ring_final[k]) * w;
    dbg_cam[0] = (u32)(u16)(s16)F32(CAM_FINAL + 8u) | (u32)(u16)(s16)F32(CAM_FINAL + 20u) << 16;
    dbg_cam[1] = (u32)(u16)(s16)F32(CAM_FINAL + 4u) | (u32)(u16)(s16)F32(CAM_FINAL + 16u) << 16;
}
// (guestref, 2026-11-22) The frame's camera step, as func_800E1E60 calls it (gen/patches.py): a Guest Referee match with
// a computer referee is framed as the one-on-one between its competitors -- guestref.c, which says why (the game's own
// keep-everyone shot for the whole match, the referee included, is what looked off).
u32 guestref_camera_enter(void); void guestref_camera_leave(u32 taken);
void ringstage_cam_call(s32 a) {
    u32 taken = guestref_camera_enter();
    ringstage_cam_step(a);
    guestref_camera_leave(taken);
}

// func_800E1E60 (the frame's men): the height of the man's shadow, past z 800 the game's func_8011E128 -- its ray finds nothing under a
// man on RAW's deck in the ring's area (QA ringstage_fight: the shadows on the aisle floor under the deck). Here: the ground he's lifted by
extern float func_8011E128(u32 slot);
float ringstage_shadow(u32 slot) {
    if (active() && slot < RS_SLOTS) return h_now[slot];
    // (everywhere else the game's test, moved here from the patched branch: its z 0x800A1144 past 800)
    return F32(0x800A1144u + 0xB24u * slot) > 800.0f ? func_8011E128(slot) : 0.0f;
}

// func_80014494 -> func_80015540 (a man's shadow, whoever asks): y its height (the ground he's lifted by in RAW's one area)
extern void func_80015540(u32 rec, s32 slot, float ring_y, float y);
static u32 dbg_sh_calls, dbg_sh_last;     // (QA, mode bit 32: the shadow's calls and the last height given)
void ringstage_shadow_call(u32 rec, s32 slot, float ring_y, float y) {
    // (func_80015540 takes the ring's height for a floor it finds raised -- the deck: the shadow at 90, under the men. On the ramp and deck, both his)
    if (active() && slot >= 0 && slot < RS_SLOTS) { y = h_now[slot]; if (y > 0.0f) ring_y = y; }
    dbg_sh_calls++; dbg_sh_last = (u32)(s32)y | (u32)(slot & 15) << 12;
    func_80015540(rec, slot, ring_y, y);
}

// func_80110D7C: a whipped man at a door wall (kind 0x200): -> 0 he hits it as a wall (the game's own bounce, state 0x15: a door wall has none of its own). Going through takes everyone to the next
// room, so in RAW the door at the back of the stage opens only when someone else is on the stage too -- a whip from the ring doesn't
// run a man the whole aisle and out of the arena
int ringstage_door_open(u32 man) {
    if (!active()) return 1;
    for (int s = 0; s < 6; s++) {                   // (the six it always looked at: lumberjack2 left this as it was)
        u32 m = MAN(s);
        if (m == man || RDS16(m + 0xA0u) == -1) continue;
        if (F32(m + 0x104u) >= (float)ra->front) return 1;
    }
    dbg_ringstage[1]++;
    return 0;
}

// New feature (Ultimate No Mercy): the stages agent, 2026-11-20. func_800EDCF4 (a man through a door: the next area, both men
// to state 190) -> the six-man engine's own copy, rw_800EDCF4, then: from backstage, the door code 0x2000 (the hallway's way
// out, record 108) is the start venue's stage area, start + 10 -- the old stage view, one more door from the ring. Where the
// ring's world backstage was entered from has its stage one area with it (it has a map), the way out is that ring's world:
// the area asked again (func_800DA2F0); func_80115000 then puts the men at the map's arrival spot, by the door at the back of the stage
// (ringstage_take). Before this the game's own func_800EDCF4 was patched for it, but in the six-man build that function is a
// tail call to rw_800EDCF4 and the edit was in its dead rest: it never ran.
#define RS_AREA_PREV  0x8016B348u   // s32: the area the change is from (rw_800EDCF4 sets it)
#define RS_AREA_NEXT  0x80159670u   // s32: the area it goes to
#define RS_START      0x8016B340u   // s32: the match's start venue
extern s32 rw_800EDCF4(u32 man, u32 partner);
extern void func_800DA2F0(s32 area);
extern int arena_ringstage_of(int v);
u32 dbg_area_back;                  // (the times the way back was put right)
// The ring's world to go back to is the one backstage was entered from (ring_from), not the start venue: in the QA's matches
// 0x8016B340 held 2 whatever the arena, and the way back from RAW IS WAR's hallway went to SMACKDOWN's ring (QA stagedoor_*
// 2026-11-20: areas 0 > 20 > 2, 4 > 20 > 2 ..). The start venue only when backstage was never entered from a ring (-1).
static s32 ring_from = -1;
s32 ringstage_area_change(u32 man, u32 partner) {
    s32 r = rw_800EDCF4(man, partner);
    s32 prev = RDS32(RS_AREA_PREV), next = RDS32(RS_AREA_NEXT);
    if (prev >= 0 && prev < 10 && next >= 20) ring_from = prev;
    s32 back = ring_from >= 0 ? ring_from : RDS32(RS_START), a = arena_ringstage_of(back);
    if (prev >= 20 && next >= 10 && next < 20 && back >= 0 && back < 10 && a >= 0 && a < XRS_N && xrs_arenas[a].cells) {
        RDS32(RS_AREA_NEXT) = back;
        func_800DA2F0(back);
        dbg_area_back++;
    }
    return r;
}

// func_800F03F0 (the CPU's think, his own place and his opponent's just taken): the CPU goes straight at his man, and the game has no way round anything (COLLISION.md section 5: a box 300
// wide stops him for good). With a way straight there on the map he's left to it. Otherwise, once he's done thinking (ringstage_ai_post), his controller is walked along the map's shortest way -- the
// game's own run and direction bits, so everything else about him is the game's.
static int way_on; static float way_x, way_z;
#define MAP_CELLS 4096
static s16 map_dist[MAP_CELLS]; static s16 map_q[MAP_CELLS];
static int map_goal = -1; static const XrsArena* map_of;
static void map_from(int gi, int gk) {             // (distances to the cell, by steps round what's not walkable)
    int w = ra->w, n = w * ra->h, goal = gk * w + gi;
    if (map_of == ra && map_goal == goal) return;
    map_of = ra; map_goal = goal;
    if (n > MAP_CELLS) return;
    for (int i = 0; i < n; i++) map_dist[i] = 0x7FFF;
    int head = 0, tail = 0;
    map_dist[goal] = 0; map_q[tail++] = (s16)goal;
    while (head < tail) {
        int c = map_q[head++], ck = c / w, ci = c - ck * w, d = map_dist[c] + 1;
        static const s8 dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
        for (int j = 0; j < 4; j++) {
            int i2 = ci + dx[j], k2 = ck + dz[j];
            if (cell_at(i2, k2) == 255) continue;
            int e = k2 * w + i2;
            if (map_dist[e] > d) { map_dist[e] = (s16)d; map_q[tail++] = (s16)e; }
        }
    }
}
static int cell_near(float x, float z, int* oi, int* ok) {   // (his cell, or the walkable one nearest it within 2: he's kept off the very edge)
    int ci = (int)((x - (float)ra->x0) / (float)XRS_GRID), ck = (int)((z - (float)ra->z0) / (float)XRS_GRID);
    for (int r = 0; r <= 2; r++) for (int b = -r; b <= r; b++) for (int a = -r; a <= r; a++) {
        if (cell_at(ci + a, ck + b) != 255) { *oi = ci + a; *ok = ck + b; return 1; }
    }
    return 0;
}
// New feature (Ultimate No Mercy): the stages agent, 2026-11-20. The map is 40-unit cells and the walls the men meet are traced
// at 20 (gen/arenas_collide.py): by the edges they disagree -- NITRO's map is walkable to x 160 by the aisle and all along
// z 1240..1280 at the stage's front, its walls stand at x 140 and z 1250. A CPU coming down from the stage was walked at a
// map cell behind the front's drop wall and stood there pressing into it (QA stagecpu_monday_nitro 2026-11-20_144931: 6 s at
// x 173, z 1280). So a line he's walked along must clear the walls too (the arena's own, arena.c arena_zone_look: every
// span of its four quadrants' lists) by WALL_GAP, as well as the map.
#define WALL_GAP 20.0f
extern u32 arena_zone_look;
static float seg_pt(float px, float pz, float ax, float az, float bx, float bz) {   // (the distance squared, point to segment)
    float vx = bx - ax, vz = bz - az, l = vx * vx + vz * vz, t = 0.0f;
    if (l > 0.0f) { t = ((px - ax) * vx + (pz - az) * vz) / l; t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t; }
    float qx = ax + t * vx - px, qz = az + t * vz - pz;
    return qx * qx + qz * qz;
}
static int seg_meet(float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz) {   // (do two segments cross?)
    float d1 = (bx - ax) * (cz - az) - (bz - az) * (cx - ax), d2 = (bx - ax) * (dz - az) - (bz - az) * (dx - ax);
    float d3 = (dx - cx) * (az - cz) - (dz - cz) * (ax - cx), d4 = (dx - cx) * (bz - cz) - (dz - cz) * (bx - cx);
    return ((d1 > 0.0f) != (d2 > 0.0f)) && ((d3 > 0.0f) != (d4 > 0.0f));
}
static int walls_clear(float x0, float z0, float x1, float z1, int from_too) {
    u32 l = arena_zone_look;
    if (!l) return 1;
    for (u32 s; (s = RD32(l)) != 0u; l += 4u) {
        u32 r = RD32(s), last = RD32(s + 4u);
        for (int n = 0; r && n < 512; n++) {
            float ax = (float)RDS16(r), az = (float)RDS16(r + 4u), bx = (float)RDS16(r + 6u), bz = (float)RDS16(r + 10u);
            if ((ax != bx || az != bz) &&
                (seg_meet(x0, z0, x1, z1, ax, az, bx, bz) || (from_too && seg_pt(x0, z0, ax, az, bx, bz) < WALL_GAP * WALL_GAP) ||
                 seg_pt(x1, z1, ax, az, bx, bz) < WALL_GAP * WALL_GAP || seg_pt(ax, az, x0, z0, x1, z1) < WALL_GAP * WALL_GAP ||
                 seg_pt(bx, bz, x0, z0, x1, z1) < WALL_GAP * WALL_GAP)) return 0;
            if (r == last) break;
            r = RD32(r + 0x2Cu);
        }
    }
    return 1;
}
static int clear_line(float x0, float z0, float x1, float z1) {   // (the map walkable all the way, and no wall in the way?)
    float dx = x1 - x0, dz = z1 - z0, l = abs_f(dx) > abs_f(dz) ? abs_f(dx) : abs_f(dz);
    int n = (int)(l / 20.0f) + 1;
    for (int j = 0; j <= n; j++) {
        float t = (float)j / (float)n;
        int i = (int)((x0 + t * dx - (float)ra->x0) / (float)XRS_GRID), k = (int)((z0 + t * dz - (float)ra->z0) / (float)XRS_GRID);
        if (cell_at(i, k) == 255) return 0;
    }
    return walls_clear(x0, z0, x1, z1, 1);
}
// (QA mode 1024, the stagestuck agent 2026-11-24: why this think left the CPU to the game or walked him -- the trace's "why")
enum { WHY_OFF, WHY_RING, WHY_NEAR, WHY_TO_MOUTH, WHY_CLEAR, WHY_CLEAR_UP, WHY_NOMAP, WHY_NOWAY, WHY_PATH, WHY_EDGE, WHY_EDGE8 };
static u8 ai_why;
void ringstage_ai(void) {
    arena_ai_area = RDS32(RS_AREA);
    way_on = 0;
    ai_why = WHY_OFF;
    if (!active()) return;
    float sx = F32(RS_SELF), sz = F32(RS_SELF + 8u), ox = F32(RS_OPP), oz = F32(RS_OPP + 8u);
    // his man past the old door line, where the count doesn't run, or him on the stage: as in the stage area, no going back to the ring for the count (func_800F6154)
    if (oz >= XRS_COUNT_Z || sz >= (float)ra->front) arena_ai_area = 10;
    int s_ring = sz < XRS_MOUTH_Z, o_ring = oz < XRS_MOUTH_Z;
    if (s_ring && o_ring) { ai_why = WHY_RING; return; }
    float dx = ox - sx, dz = oz - sz;
    if (!s_ring && !o_ring && dx * dx + dz * dz < XRS_NEAR * XRS_NEAR && clear_line(sx, sz, ox, oz)) { ai_why = WHY_NEAR; return; }
    if (s_ring) {                                   // by the ring, his man up the aisle: to its mouth, then up
        way_x = 0.0f; way_z = abs_f(sx) > XRS_AISLE_X - 30 ? XRS_MOUTH_Z - 40 : XRS_MOUTH_Z + 200;
        way_on = 1; ai_why = WHY_TO_MOUTH; return;
    }
    if (!o_ring && clear_line(sx, sz, ox, oz)) {
        // Changed for new features (Ultimate No Mercy): the stages agent, 2026-11-20. In the aisle short of the old door line
        // (z 800) with his man up past it, the game's own choice is a ring area's (it walked him back toward the ring), and
        // below the mouth this steering sent him up again: to and fro at z 530 for seconds (QA stagecpu_king_of_the_ring /
        // raw_is_war 2026-11-20: z 526..535 every think, plan 0x21). There: straight at his man, as the line is clear.
        ai_why = WHY_CLEAR;
        if (sz < (float)XRS_COUNT_Z && oz >= (float)XRS_COUNT_Z) { way_x = ox; way_z = oz; way_on = 1; ai_why = WHY_CLEAR_UP; }
        return;
    }
    // on the map, his man elsewhere on it or down by the ring (then the aisle's mouth): the shortest way, as far along it as he can see
    float gx = o_ring ? 0.0f : ox, gz = o_ring ? (float)XRS_MOUTH_Z + 10.0f : oz;
    int gi, gk, si, sk;
    ai_why = WHY_NOMAP;
    if (ra->w * ra->h > MAP_CELLS || !cell_near(gx, gz, &gi, &gk) || !cell_near(sx, sz, &si, &sk)) return;
    map_from(gi, gk);
    int w = ra->w, c = sk * w + si, d = map_dist[c];
    ai_why = WHY_NOWAY;
    if (d >= 0x7FFF) return;
    ai_why = WHY_PATH;
    int ci = si, ck = sk;
    float bx = sx, bz = sz;
    for (int step = 0; step < 16 && d > 0; step++) {
        static const s8 ax[4] = { 1, -1, 0, 0 }, az[4] = { 0, 0, 1, -1 };
        int bi = ci, bk = ck, bd = d;
        for (int j = 0; j < 4; j++) {
            int i2 = ci + ax[j], k2 = ck + az[j];
            if (cell_at(i2, k2) == 255) continue;
            int dd = map_dist[k2 * w + i2];
            if (dd < bd) { bd = dd; bi = i2; bk = k2; }
        }
        if (bd >= d) break;
        ci = bi; ck = bk; d = bd;
        float nx = (float)ra->x0 + ((float)ci + 0.5f) * (float)XRS_GRID, nz = (float)ra->z0 + ((float)ck + 0.5f) * (float)XRS_GRID;
        if (!clear_line(sx, sz, nx, nz)) break;
        bx = nx; bz = nz;
    }
    if (bx == sx && bz == sz) {                     // (hard by an edge: the next cell's middle)
        bx = (float)ra->x0 + ((float)ci + 0.5f) * (float)XRS_GRID; bz = (float)ra->z0 + ((float)ck + 0.5f) * (float)XRS_GRID;
        ai_why = WHY_EDGE;
        // New feature (Ultimate No Mercy): ... unless a wall is in the way (a corner of the map the walls don't share): then
        // the step of 60 of the eight that clears the walls and ends nearest the goal on the map (the stages agent, 2026-11-20)
        if (!walls_clear(sx, sz, bx, bz, 0)) {
            ai_why = WHY_EDGE8;
            static const float ux[8] = { 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f, 0.0f, 0.7071f };
            static const float uz[8] = { 0.0f, 0.7071f, 1.0f, 0.7071f, 0.0f, -0.7071f, -1.0f, -0.7071f };
            int best = 0x7FFF;
            for (int j = 0; j < 8; j++) {
                float px = sx + 60.0f * ux[j], pz = sz + 60.0f * uz[j];
                int pi = (int)((px - (float)ra->x0) / (float)XRS_GRID), pk = (int)((pz - (float)ra->z0) / (float)XRS_GRID);
                if (cell_at(pi, pk) == 255 || !walls_clear(sx, sz, px, pz, 0)) continue;
                int pd = map_dist[pk * w + pi];
                if (pd < best) { best = pd; bx = px; bz = pz; }
            }
        }
    }
    if (o_ring && d == 0) { bx = 0.0f; bz = (float)XRS_MOUTH_Z - 40.0f; }
    way_x = bx; way_z = bz;
    way_on = 1;
}
#define RS_AI_OBJ  0x8015A6F0u   // u32: this CPU's controller and plans; +0xC its buttons held, +0xE pressed (func_80118368 hands them on)
#define RS_AI_MAN  0x8016B8E8u   // u32: the man
static s32 qa_plan[2]; static u8 qa_way[2];       // (QA mode 256: the plan each of the first two CPUs runs, and whether he's walked the map's way)
// (QA mode 1024, the stagestuck agent 2026-11-24: each of the first four CPUs' last think -- why, the way he was walked, his
//  man's place as the think saw it, the plan -- for the trace of a man walking in place on the ramp or the stage)
#define QS_N 4
static u8 qs_why[QS_N], qs_way[QS_N]; static s16 qs_wx[QS_N], qs_wz[QS_N], qs_ox[QS_N], qs_oz[QS_N]; static s32 qs_plan[QS_N];
// Changed for new features (Ultimate No Mercy): the stagestuck agent, 2026-11-24. The user: "CPU wrestlers are still getting
// stuck on stages/ramps, walking in place". Where this steering leaves him to the game (his man near with nothing between,
// or straight up the aisle), the game's own plans hold a stick direction for a count of steps, picked by the RING: plan 33
// (ai_plan_33_34_run_off_ropes) walks away from the way he faces, to the ropes to run off them; plan 41
// (ai_plan_41_walk_about_target, "walk about" his man) more than 100 from the ring's middle with the way pointing outward
// turns it two eighths, else goes straight at his man 40% of the time, else two to six eighths off that way -- each held
// whatever is in front of him. In the ring the ropes are what that walk meets. Up the aisle the walls of the ramp and the
// stage are all round him and there are no ropes, so the way it picks often runs straight into a wall, and he walks on the
// spot against it until the count runs out (QA stagestuck_starrcade_cpu 2026-11-24_152029: plan 33 at (-347, 1279) held
// down into the stage front's drop wall for 6 s; stagestuck_nitro_roam: (119, 1279) the same). The game's walls push a man out along their normal, so a walk
// slantwise against a wall slides along it; only a walk into it head-on (or into a corner) goes nowhere. Such a direction,
// on the ramp or the stage, is turned here to the nearest of the eight that goes somewhere, turning toward his man first:
// the plan, its count and everything else are the game's.
#define STEP_LOOK  40.0f          // (how far ahead a step is looked at: a walk is 7 or so a step)
#define STEP_BODY  30.0f          // (a walking man is kept this far from a wall: COLLISION.md section 1)
static const s8 oct_x[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, oct_z[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
static int oct_of(u16 b) {        // (the d-pad's world way: 0x800 +z, 0x400 -z, 0x100 +x, 0x200 -x; -1 none)
    int x = (b & XRS_BTN_XPLUS) ? 1 : (b & XRS_BTN_XMINUS) ? -1 : 0, z = (b & 0x800u) ? 1 : (b & 0x400u) ? -1 : 0;
    for (int o = 0; o < 8; o++) if (oct_x[o] == x && oct_z[o] == z && (x || z)) return o;
    return -1;
}
static u16 oct_bits(int o) {
    return (u16)((oct_x[o] > 0 ? XRS_BTN_XPLUS : oct_x[o] < 0 ? XRS_BTN_XMINUS : 0) | (oct_z[o] > 0 ? 0x800u : oct_z[o] < 0 ? 0x400u : 0));
}
// (does a step that way from (x, z) go nowhere: against a wall's face head-on (less than half of it along the wall), or into
//  two walls that meet (a corner)? A wall only stops a man on its walkable side moving against it: they are one-sided)
static int step_blocked(float x, float z, int o) {
    float k = (oct_x[o] && oct_z[o]) ? 0.7071f : 1.0f, ux = k * (float)oct_x[o], uz = k * (float)oct_z[o];
    float ex = x + STEP_LOOK * ux, ez = z + STEP_LOOK * uz, tx0 = 0.0f, tz0 = 0.0f;
    int slides = 0;
    u32 l = arena_zone_look;
    if (!l) return 0;
    for (u32 sp; (sp = RD32(l)) != 0u; l += 4u) {
        u32 r = RD32(sp), last = RD32(sp + 4u);
        for (int n = 0; r && n < 512; n++) {
            float ax = (float)RDS16(r), az = (float)RDS16(r + 4u), bx = (float)RDS16(r + 6u), bz = (float)RDS16(r + 10u);
            float dx = bx - ax, dz = bz - az, nx = dz, nz = -dx, dd = dx * dx + dz * dz;
            if (dd > 0.0f && (x - ax) * nx + (z - az) * nz > 0.0f && ux * nx + uz * nz < 0.0f &&
                (seg_meet(x, z, ex, ez, ax, az, bx, bz) || seg_pt(ex, ez, ax, az, bx, bz) < STEP_BODY * STEP_BODY)) {
                float along = ux * dx + uz * dz;
                if (along * along < 0.25f * dd) return 1;                 // (head-on)
                float tl = along > 0.0f ? 1.0f : -1.0f;                   // (a slide along it: the way it goes)
                if (slides && (tx0 * dz - tz0 * dx) * (tx0 * dz - tz0 * dx) > 0.25f * dd * (tx0 * tx0 + tz0 * tz0)) return 1;   // (two walls not in line: a corner)
                if (!slides) { tx0 = tl * dx; tz0 = tl * dz; }
                slides++;
            }
            if (r == last) break;
            r = RD32(r + 0x2Cu);
        }
    }
    return 0;
}
u32 dbg_walk_turned;                // (QA: the steps a CPU's held way was turned off a wall)
// (the nearest of the eight to way o that a step goes somewhere along, turning first toward (tx, tz) -- the side that brings
//  him round to it; -1 none)
static int way_off_walls(float x, float z, int o, float tx, float tz) {
    int side = ((float)oct_z[o] * tx - (float)oct_x[o] * tz) > 0.0f ? 1 : -1;   // (+1: o -> o + 1, +z toward +x, turns toward it)
    for (int k = 1; k <= 4; k++) for (int j = 0; j < 2; j++) {
        if (k == 4 && j) break;
        int o2 = (o + (j ? -side : side) * k + 8) & 7;
        if (!step_blocked(x, z, o2)) return o2;
    }
    return -1;
}
static void walk_off_walls(u32 man, u32 ai) {
    if (!active() || RDS32(man + 0x14u) != 1 || !(RD16(man + 0x82u) & 4u)) return;   // (on his feet, free, on the floor)
    float sx = F32(RS_SELF), sz = F32(RS_SELF + 8u);
    if (sz < (float)XRS_MOUTH_Z) return;             // (by the ring: the game's own rule, as ever)
    u16 b = RD16(ai + 0xCu);
    int o = oct_of(b);
    if (o < 0 || !step_blocked(sx, sz, o)) return;
    int o2 = way_off_walls(sx, sz, o, F32(RS_OPP) - sx, F32(RS_OPP + 8u) - sz);   // (toward his man first)
    RD16(ai + 0xCu) = (u16)(b & (u16)~0x0F00u) | (o2 >= 0 ? oct_bits(o2) : 0u);  // (nowhere to go: he stands till the plan's count runs out)
    dbg_walk_turned++;
}
void ringstage_ai_post(void) {
    static u8 tick;
    {   // New feature (Ultimate No Mercy): QA, what the CPU thought (state.csv bs_grabs in mode 256)
        s32 sl = RDS16(RD32(RS_AI_MAN) + 0x9Eu);
        if (sl >= 0 && sl < 2) { qa_plan[sl] = RDS32(RD32(RS_AI_OBJ)); qa_way[sl] = (u8)way_on; }
        if (sl >= 0 && sl < QS_N) {
            qs_why[sl] = ai_why; qs_way[sl] = (u8)way_on; qs_plan[sl] = RDS32(RD32(RS_AI_OBJ));
            qs_wx[sl] = (s16)way_x; qs_wz[sl] = (s16)way_z; qs_ox[sl] = (s16)F32(RS_OPP); qs_oz[sl] = (s16)F32(RS_OPP + 8u);
        }
    }
    if (!way_on) { walk_off_walls(RD32(RS_AI_MAN), RD32(RS_AI_OBJ)); return; }
    u32 man = RD32(RS_AI_MAN), ai = RD32(RS_AI_OBJ);
    s32 st = RDS32(man + 0x14u);
    u16 where = RD16(man + 0x82u);
    if (st != 1 && st != 2 && st != 3) return;       // (on his feet and free)
    float sx = F32(RS_SELF), sz = F32(RS_SELF + 8u);
    u16 b = 0, pressed = 0;
    if (where & 3u) {                                // in the ring or on the apron: to the ropes on the aisle's side, through them and down (up + C-up: state 110)
        way_x = sx; way_z = 400.0f;
        if (sz > 185.0f) { b |= 8u; if (!(++tick & 7u)) pressed = 8u; }
    } else if (!(where & 4u)) return;
    float dx = way_x - sx, dz = way_z - sz, ax = abs_f(dx), az = abs_f(dz);
    if (where & 4u) {
        // (a run goes straight till it's let go: only when he's lined up, and not for the last of a stretch)
        if ((az > 300.0f && ax < 0.08f * az) || (ax > 300.0f && az < 0.08f * ax)) b |= 4u;
        else if (st == 3) { RD16(ai + 0xCu) = 0; RD16(ai + 0xEu) = 0; return; }
    }
    if (az > 0.41f * ax) b |= dz > 0.0f ? 0x800u : 0x400u;   // (up: z up; down: z down -- the game's 0x8014D258 table)
    if (ax > 0.41f * az) b |= dx > 0.0f ? XRS_BTN_XPLUS : XRS_BTN_XMINUS;
    // Changed for new features (Ultimate No Mercy): the stagestuck agent, 2026-11-24. The way he is walked is the nearest of
    // the eight to the line the map's way was checked along (clear_line), up to 22.5 degrees off it; by the end of a wall
    // that is the difference between passing it and walking into it head-on: QA stagestuck_nitro_cpu 2026-11-24_140524, a
    // CPU on NITRO's stage at (127, 1280) walked at (20, 900) -- the line clear past the end of the stage front's drop wall,
    // the way straight down (-z) into it -- on the spot for 9 seconds. Then the other of the two ways either side of the
    // line (it lies between them), or the nearest that goes somewhere.
    if (where & 4u) {
        int o = oct_of(b);
        if (o >= 0 && step_blocked(sx, sz, o)) {
            int o2 = ((float)oct_x[o] * dz - (float)oct_z[o] * dx) > 0.0f ? (o + 7) & 7 : (o + 1) & 7;   // (the line's other side)
            if (step_blocked(sx, sz, o2)) o2 = way_off_walls(sx, sz, o, dx, dz);
            b = (u16)(b & (u16)~0x0F04u) | (o2 >= 0 ? oct_bits(o2) : 0u);   // (not running: he isn't lined up now)
            dbg_walk_turned++;
        }
    }
    RD16(ai + 0xCu) = b; RD16(ai + 0xEu) = pressed;
    dbg_ringstage[2]++;
}

// QA (logic.c qa_ringstage): state.csv's bs_tries the area, bs_grabs the two lifts, bs_step door refused | CPU steers << 16.
// mode 2: every 15 s of match the first two men are put in the aisle by the barricade, on the ground, the first facing the ring and the
// second facing him -- a place the player's own walk gets to, so that the scenario's scripted A and C-down are a real whip up the aisle
static void qa_put(u32 s, float x, float z, u32 facing) {
    u32 m = MAN(s);
    F32(m + 0xFCu) = x; F32(m + 0x100u) = 0.0f; F32(m + 0x104u) = z;
    F32(m + 0x120u) = x; F32(m + 0x124u) = 0.0f; F32(m + 0x128u) = z;
    for (u32 b = 0x12Cu; b < 0x160u; b += 12u) { F32(m + b) = x; F32(m + b + 4u) = 0.0f; F32(m + b + 8u) = z; }
    RD32(m + 0x10Cu) = facing;
    RD16(m + 0x82u) = 4u;
}
// New feature (Ultimate No Mercy): QA modes 256 and 512 (the stages agent, 2026-11-20).
// (a walkable place on the map near (x, z): there, or the middle of the nearest walkable cell)
static void qa_spot(float x, float z, float* ox, float* oz) {
    int i, k;
    *ox = x; *oz = z;
    if (!ra || !cell_near(x, z, &i, &k)) return;
    int ci = (int)((x - (float)ra->x0) / (float)XRS_GRID), ck = (int)((z - (float)ra->z0) / (float)XRS_GRID);
    if (ci == i && ck == k) return;
    *ox = (float)ra->x0 + ((float)i + 0.5f) * (float)XRS_GRID; *oz = (float)ra->z0 + ((float)k + 0.5f) * (float)XRS_GRID;
}
// (the back of the stage across its middle, x = 0: the far edge of the map's walkable cells there -- where a stage's doorway
//  stands; mode 512's whip goes there whatever the walls' data says)
static float qa_back_mid(void) {
    if (!ra) return 2000.0f;
    int i = (int)((0.0f - (float)ra->x0) / (float)XRS_GRID), k = ra->h - 1;
    while (k > 0 && cell_at(i, k) == 255) k--;
    return (float)ra->z0 + (float)(k + 1) * (float)XRS_GRID;
}
static int qa_free(u32 s) { s32 st = RDS32(MAN(s) + 0x14u); return st == 1 || st == 2 || st == 3; }
// New feature (Ultimate No Mercy): QA mode 1024 (the stagestuck agent, 2026-11-24) -- walking in place on the ramp and the
// stage, measured on the match's clock (a tick: match_frames moves, 30 a second), for the first four men. A man is on the
// ramp or the stage past the aisle's mouth (z XRS_MOUTH_Z); he walks in place in a tick when he is on his feet and free to
// walk (state 1, standing -- a man walks in it while a direction is held -- or 2, round a ring post) with a direction held (the buttons read_slot_controls gives his man this step: 0x8017DD58 + 0x38 a slot, the d-pad
// 0x0F00) for more than QS_W ticks running and is less than QS_D from where he was QS_W ticks before. state.csv lj_a ..
// lj_r (no Lumberjack match: those words are free): lj_a the CPUs' ticks on the ramp / stage, lj_b of them walking in place,
// lj_c / lj_d the same for every man, lj_e the longest stretch in place | its slot << 16, lj_f stretches of 30 ticks or
// more, lj_g ticks seen, lj_h his walk's speed (units a tick x 10), lj_i .. lj_l each slot's ticks in place, lj_m .. lj_p
// each slot's on the ramp / stage, lj_q the CPUs' ticks in place with a direction held in states 1 .. 3 (a run too), lj_r now: the
// d-pads (4 bits a slot) | in place << 16 | on the ramp << 20. And to the game's log, a man in place 15 ticks running and
// every 90 more: "stagestuck:" his place, state, buttons, the CPU's last think (ringstage_ai: why, the way, his man), the
// walls he was last pushed off (+0x348, +0x34C).
#define QS_W 10
#define QS_D 12.0f
#define QS_HELD 0x8017DD58u
static float qs_px[QS_N][16], qs_pz[QS_N][16];
static u8 qs_run[QS_N], qs_runl[QS_N]; static u16 qs_in_run[QS_N]; static u8 qs_head; static u16 qs_mf = 0xFFFFu;
static u32 qs_on[QS_N], qs_in[QS_N], qs_cpu_on, qs_cpu_in, qs_cpu_loose, qs_long, qs_eps, qs_ticks, qs_walk_n, qs_walk_d10;
static int qs_put_at = -1;
void removal_s(const char* s); void removal_num(s32 v); void removal_say(void);
static void qs_wall(const char* k, u32 r) {
    removal_s(k);
    if (r < 0x80000000u || r >= 0x80800000u) { removal_s("-"); if (r) removal_num((s32)(r & 0x7FFFFFFFu)); return; }
    removal_num(RDS16(r + 0x24u)); removal_s("("); removal_num(RDS16(r)); removal_s(","); removal_num(RDS16(r + 4u));
    removal_s(".."); removal_num(RDS16(r + 6u)); removal_s(","); removal_num(RDS16(r + 10u)); removal_s(" fl ");
    removal_num(RD16(r + 0x26u)); removal_s(")");
}
static void qs_trace(int s, u32 m, int cpu, u16 held, s32 st) {
    removal_s("stagestuck: s"); removal_num(s); removal_s(cpu ? " cpu" : " pad"); removal_s(" mf "); removal_num(qs_mf);
    removal_s(" in place "); removal_num(qs_in_run[s]); removal_s(" at "); removal_num((s32)F32(m + 0xFCu)); removal_s(",");
    removal_num((s32)F32(m + 0x104u)); removal_s(" h "); removal_num((s32)h_now[s]); removal_s(" st "); removal_num(st);
    removal_s("/"); removal_num(RDS32(m + 0x18u)); removal_s(" place "); removal_num(RD16(m + 0x82u)); removal_s(" held ");
    removal_num(held); removal_s(" face "); removal_num(RDS32(m + 0x10Cu)); removal_s(" tgt "); removal_num(RDS32(m + 0x8u));
    if (cpu) {
        removal_s(" why "); removal_num(qs_why[s]); removal_s(" way "); removal_num(qs_way[s]); removal_s(" ");
        removal_num(qs_wx[s]); removal_s(","); removal_num(qs_wz[s]); removal_s(" opp "); removal_num(qs_ox[s]);
        removal_s(","); removal_num(qs_oz[s]); removal_s(" plan "); removal_num(qs_plan[s]);
    }
    qs_wall(" wall ", RD32(m + 0x348u)); qs_wall(" wall2 ", RD32(m + 0x34Cu));
    removal_s(" list "); removal_num((s32)RD32(m + 0x350u));
    removal_say();
}
static float qs_sqrt(float r) {
    if (r <= 0.0f) return 0.0f;
    float g = r > 1.0f ? r * 0.5f : 1.0f;
    for (int k = 0; k < 10; k++) g = 0.5f * (g + r / g);
    return g;
}
static void qs_measure(u32 lj, u16 mf) {
    if (mf != qs_mf && mf != 0xFFFFu) {
        qs_mf = mf; qs_ticks++; qs_head = (u8)((qs_head + 1u) & 15u);
        int n = nm_slots(); if (n > QS_N) n = QS_N;
        for (int s = 0; s < n; s++) {
            u32 m = MAN(s);
            float x = F32(m + 0xFCu), z = F32(m + 0x104u);
            qs_px[s][qs_head] = x; qs_pz[s][qs_head] = z;
            int on = RDS16(m + 0xA0u) != -1 && z >= (float)XRS_MOUTH_Z && z < 1e6f;
            int cpu = (RD8(m + 0xE9u) & 8u) != 0;
            s32 st = RDS32(m + 0x14u);
            u16 held = RD16(QS_HELD + 0x38u * (u32)s);
            int walk = on && (st == 1 || st == 2) && (held & 0x0F00u), loose = on && st >= 1 && st <= 3 && (held & 0x0F00u);
            if (on) { qs_on[s]++; if (cpu) qs_cpu_on++; }
            u32 old = (qs_head + 16u - QS_W) & 15u, prev = (qs_head + 15u) & 15u;
            float ddx = x - qs_px[s][old], ddz = z - qs_pz[s][old];
            int still = ddx * ddx + ddz * ddz < QS_D * QS_D;
            qs_runl[s] = loose ? (u8)(qs_runl[s] < 255 ? qs_runl[s] + 1 : 255) : 0;
            if (cpu && qs_runl[s] > QS_W && still) qs_cpu_loose++;
            if (!walk) { qs_run[s] = 0; qs_in_run[s] = 0; continue; }
            if (qs_run[s] < 255) qs_run[s]++;
            if (qs_run[s] >= 2) {
                float px = x - qs_px[s][prev], pz = z - qs_pz[s][prev], d2 = px * px + pz * pz;
                if (d2 < 400.0f) { qs_walk_n++; qs_walk_d10 += (u32)(10.0f * qs_sqrt(d2)); }
            }
            if (qs_run[s] <= QS_W || !still) { qs_in_run[s] = 0; continue; }
            qs_in[s]++; if (cpu) qs_cpu_in++;
            qs_in_run[s]++;
            if (qs_in_run[s] == 30) qs_eps++;
            if (qs_in_run[s] > (qs_long & 0xFFFFu)) qs_long = (u32)qs_in_run[s] | (u32)s << 16;
            if (qs_in_run[s] == 15 || (qs_in_run[s] > 15 && (qs_in_run[s] - 15) % 90 == 0)) qs_trace(s, m, cpu, held, st);
        }
    }
    u32 now = 0;
    for (int s = 0; s < QS_N && s < nm_slots(); s++) {
        u32 m = MAN(s);
        now |= (u32)((RD16(QS_HELD + 0x38u * (u32)s) >> 8) & 15u) << (4 * s);
        if (qs_in_run[s]) now |= 1u << (16 + s);
        if (F32(m + 0x104u) >= (float)XRS_MOUTH_Z) now |= 1u << (20 + s);
    }
    u32 w[18] = { qs_cpu_on, qs_cpu_in, qs_on[0] + qs_on[1] + qs_on[2] + qs_on[3], qs_in[0] + qs_in[1] + qs_in[2] + qs_in[3],
                  qs_long, qs_eps, qs_ticks, qs_walk_n ? qs_walk_d10 / qs_walk_n : 0u,
                  qs_in[0], qs_in[1], qs_in[2], qs_in[3], qs_on[0], qs_on[1], qs_on[2], qs_on[3], qs_cpu_loose, now };
    for (u32 k = 0; k < 18u; k++) RD32(lj + 4u * k) = w[k];
}
static u32 qa_door_in, qa_door_back, qa_door_ok, qa_door_wait, qa_door_bad;
static s32 qa_door_from = -1;                    // (the ring's area the hallway was entered from: the way back must come to it)
static s32 qa_area_was = -1;
static int qa_roam_at = -1;
void ringstage_qa(u32 q, int mode) {
    RD32(q) = (u32)RDS32(RS_AREA);
    RD32(q + 4u) = (u32)(s32)h_now[0] | (u32)(s32)h_now[1] << 16;
    RD32(q + 8u) = (u32)dbg_ringstage[1] | (u32)dbg_ringstage[2] << 16;
    if (mode & 32) RD32(q + 8u) = (dbg_sh_calls & 0xFFFFu) | dbg_sh_last << 16;
    if (mode & 64) {                                 // (the camera, QA: see dbg_cam)
        RD32(q + 4u) = dbg_cam[0];
        RD32(q + 0x10u) = dbg_cam[1];
    }
    u16 mf = RD16(NM_MATCH_FRAMES);
    s32 area = RDS32(RS_AREA);
    // mode 512, the door at the back of the stage and the way back. state.csv rr_put_out: the times the hallway (20) was reached
    // from the ring's world | the times the ring's world was back from backstage << 8; rr_kept_out: of those, the times both men
    // stood on the walkable map, up on the stage, in the ring's world they left, 90 frames after | the times backstage led to a stage area (10-19: the old
    // stage view) << 8
    if (mode & 512) {
        if (qa_area_was >= 0 && qa_area_was < 10 && area == 20) { qa_door_in++; qa_door_from = qa_area_was; }
        if (qa_area_was >= 20 && area < 10) { qa_door_back++; qa_door_wait = 90; }
        if (qa_area_was >= 20 && area >= 10 && area < 20) qa_door_bad++;
        qa_area_was = area;
        if (qa_door_wait && mf != 0xFFFFu && --qa_door_wait == 0 && area < 10 && ra) {
            int ok = 1;
            for (u32 s = 0; s < 2; s++) {
                float x = F32(MAN(s) + 0xFCu), z = F32(MAN(s) + 0x104u);
                int i = (int)((x - (float)ra->x0) / (float)XRS_GRID), k = (int)((z - (float)ra->z0) / (float)XRS_GRID);
                if (cell_at(i, k) == 255 || z < (float)ra->front - 150.0f || area != qa_door_from) ok = 0;
            }
            qa_door_ok += (u32)ok;
        }
        RD32(q + 0x10u) = (qa_door_in & 0xFFu) | (qa_door_back & 0xFFu) << 8 | (qa_door_ok & 0xFFu) << 16 | (qa_door_bad & 0xFFu) << 24;
    }
    if (mode & 256)                                  // (bs_grabs: slot 0's CPU plan | slot 1's << 8 | each walked the map's way: << 16, << 17)
        RD32(q + 4u) = ((u32)qa_plan[0] & 0xFFu) | ((u32)qa_plan[1] & 0xFFu) << 8 | (u32)qa_way[0] << 16 | (u32)qa_way[1] << 17;
    if ((mode & 1024) && ra && area < 10) qs_measure(q + 0x24u, mf);   // (state.csv lj_a ..: QA_STATE + 0x244)
    if (mf == 0xFFFFu) return;
    // mode 2048 (the stagestuck agent, 2026-11-24): a fight carried up onto the stage -- once a minute of match, when the
    // first two are both free and both back by the ring, they are put on the stage 100 apart, facing; then left to fight
    if ((mode & 2048) && ra && area < 10) {
        int period = (int)(mf / 1800u), t = (int)(mf % 1800u);
        if (t >= 150 && period != qs_put_at && qa_free(0) && qa_free(1) && F32(MAN(0) + 0x104u) < (float)XRS_MOUTH_Z &&
            F32(MAN(1) + 0x104u) < (float)XRS_MOUTH_Z) {
            float a[2], b[2];
            qa_spot(-50.0f, (float)ra->front + 120.0f, &a[0], &a[1]);
            qa_spot(50.0f, (float)ra->front + 120.0f, &b[0], &b[1]);
            qa_put(0, a[0], a[1], 1024u);
            qa_put(1, b[0], b[1], 3072u);
            qs_put_at = period;
        }
    }
    // mode 256: CPU against CPU walked up and down the ramp -- every 25 s of match (once both are free to walk) the two are put
    // apart, by turns: by the ring and on the stage, the other way round, at the aisle's end and at the back of the stage, the
    // other way round
    if ((mode & 256) && ra && area < 10) {
        int period = (int)(mf / 1500u), t = (int)(mf % 1500u);
        if (t >= 300 && t < 900 && period != qa_roam_at && qa_free(0) && qa_free(1)) {
            float ring[2], deck[2], aisle[2], back[2];
            qa_spot(0.0f, 420.0f, &ring[0], &ring[1]);
            qa_spot(0.0f, (float)ra->front + 120.0f, &deck[0], &deck[1]);
            qa_spot(0.0f, 680.0f, &aisle[0], &aisle[1]);
            qa_spot(0.0f, qa_back_mid() - 120.0f, &back[0], &back[1]);
            const float* p[4][2] = { { ring, deck }, { deck, ring }, { aisle, back }, { back, aisle } };
            int k = period & 3;
            qa_put(0, p[k][0][0], p[k][0][1], 0u);
            qa_put(1, p[k][1][0], p[k][1][1], 2048u);
            qa_roam_at = period;
        }
        return;
    }
    if (!(mode & 2) || mf % 900u != 300u) return;
    if ((mode & 512) && area == 20) {               // (the hallway: before its far wall, the way back -- the game's record 108, z -960, x +-180)
        qa_put(0, 0.0f, -710.0f, 0u);
        qa_put(1, 0.0f, -665.0f, 2048u);
        return;
    }
    if (area >= 10) return;
    float zs[2] = { 690.0f, 645.0f }, x_at = 0.0f;
    u32 fs[2] = { 2048u, 0u };
    if ((mode & 128) && ra) { zs[0] = (float)ra->qz - 250.0f; zs[1] = (float)ra->qz - 295.0f; x_at = (float)ra->qx; }   // (a whip at a wall of the models': a whip sends the man past the whipper)
    if ((mode & 512) && ra) { float b = qa_back_mid(); zs[0] = b - 250.0f; zs[1] = b - 295.0f; x_at = 0.0f; }   // (the back of the stage across its middle)
    for (u32 s = 0; s < 2; s++) qa_put(s, x_at, zs[s], fs[s]);
}
