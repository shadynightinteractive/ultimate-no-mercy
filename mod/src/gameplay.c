// Gameplay touches: the entrance camera and getting up sooner.
#include "arcade.h"
#include "gamefn.h"

// ---- entrance camera ------------------------------------------------------------------------------
// The game builds its camera with guLookAtF(view, eye, at, up) and then
// func_80001580(gfx, proj, view): CAMERA_MATRIX = view x proj (row vectors). During the stage walk
// (entrance steps 5 and 11) the camera is rebuilt here: low in front of the wrestler, looking up.
// The entering wrestler's model matrix sits at NM_ENTRANT_MTX (translation in row 3).
typedef float Mtx4[4][4];

static float sqrt_f(float x) {
    if (x <= 0.0f) return 0.0f;
    float r = x > 1.0f ? x * 0.5f : 1.0f;
    for (int i = 0; i < 20; i++) r = 0.5f * (r + x / r);
    return r;
}

static void look_at(Mtx4 out, const float from[3], const float at[3]) {
    float f[3] = { at[0] - from[0], at[1] - from[1], at[2] - from[2] };
    float len = sqrt_f(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len < 1e-3f) len = 1e-3f;
    for (int k = 0; k < 3; k++) f[k] /= len;
    float r[3] = { -f[2], 0.0f, f[0] };
    len = sqrt_f(r[0] * r[0] + r[2] * r[2]);
    if (len < 1e-3f) len = 1e-3f;
    r[0] /= len; r[2] /= len;
    const float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) out[i][j] = (i == j) ? 1.0f : 0.0f;
    for (int k = 0; k < 3; k++) { out[k][0] = r[k]; out[k][1] = u[k]; out[k][2] = -f[k]; }
    out[3][0] = -(from[0] * r[0] + from[1] * r[1] + from[2] * r[2]);
    out[3][1] = -(from[0] * u[0] + from[1] * u[1] + from[2] * u[2]);
    out[3][2] = (from[0] * f[0] + from[1] * f[1] + from[2] * f[2]);
}

static int cam_frames;     // frames into this stage walk
static s32 cam_step;
static float cam_eye[3], cam_at[3];

void arcade_camera(const float* proj, float* out) {
    s32 step = RDS32(NM_ENTRANCE_STEP);
    int stage = RDS32(NM_MODE) == 0 && RDS16(NM_SCREEN) == 42 && (step == 5 || step == 11) && arcade_entrance_cam();
    if (!stage) { cam_step = -1; return; }
    volatile float* m = (volatile float*)NM_ENTRANT_MTX;
    float wx = m[12], wy = m[13], wz = m[14];
    if (!(wy > -20.0f && wy < 20.0f) || !(wx > -600.0f && wx < 600.0f) || !(wz > -2500.0f && wz < 600.0f)) return;
    if (step != cam_step) { cam_step = step; cam_frames = 0; }
    cam_frames++;
    // they walk towards -z, out from the stage: the camera waits ahead of them, near the floor,
    // a little off to one side, and keeps its distance as they come
    float side = step == 5 ? 55.0f : -55.0f;
    float eye[3] = { wx + side, wy + 14.0f, wz - 245.0f };
    float at[3] = { wx, wy + 112.0f, wz };
    if (cam_frames <= 1) {
        for (int k = 0; k < 3; k++) { cam_eye[k] = eye[k]; cam_at[k] = at[k]; }
    } else {
        for (int k = 0; k < 3; k++) { cam_eye[k] += (eye[k] - cam_eye[k]) * 0.25f; cam_at[k] += (at[k] - cam_at[k]) * 0.25f; }
    }
    Mtx4 view, p;
    look_at(view, cam_eye, cam_at);
    for (int i = 0; i < 16; i++) p[i / 4][i % 4] = proj[i];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            out[i * 4 + j] = view[i][0] * p[0][j] + view[i][1] * p[1][j] + view[i][2] * p[2][j] + view[i][3] * p[3][j];
}

// ---- no slow get-ups ------------------------------------------------------------------------------
// A wrestler knocked down (state 0x4f, ovl_e func_8013FBB8) lies for the animation func_8013FEF4
// picks, then gets up with the same get-up (0x2f7d). The base animation is 0x2f79 (+0x8a & 1:
// 0x2f7e; +0x80 & 0x10: 0x2fbc / 0x2fd5) and the level on top of it is how hurt he is (+0xbe,
// +0xaa): 0 the quick one (about 32 steps of the match's logic), 2 and 3 the long, slow ones (81 and
// more). The arcade (FAST GET UP, on by default) keeps him at level 0 -- the game's own quick
// animation all the way, nothing cut short -- unless someone with SPECIAL up (+0xee & 6) has him as
// their target (+0x08): then the slow one, the opening for the finisher, as the game has it.
// This is func_8013FEF4 itself, with that rule (hooks: RECOMP_PATCH / the test rig's jump).
#define WR_BASE (NM_WRESTLERS - 8u)          // (the struct itself; the arcade's offsets are from +8)
static int special_on(u32 w) {
    u32 me = (w - WR_BASE) / NM_WRESTLER_SIZE;
    for (int s = 0, slots = nm_slots(); s < slots; s++) {
        u32 o = WR_BASE + (u32)s * NM_WRESTLER_SIZE;
        if ((u32)s == me || RDS16(o + 0xA0) == -1) continue;
        if ((RD8(o + 0xEE) & 6) && RDS32(o + 0x08) == (s32)me) return 1;
    }
    return 0;
}
u32 dbg_down_level[2];                    // tests: the last stock level, and the one used
s32 arcade_down_anim(u32 w, int on) {
    u32 face_down = RD16(w + 0x80) & 0x10, alt = RD16(w + 0x8A) & 1;
    s32 base = face_down ? (alt ? 0x2fd5 : 0x2fbc) : (alt ? 0x2f7e : 0x2f79), level = 0;
    if (RD8(w + 0xEE) & 4) {
        if (!face_down) base = alt ? 0x2f87 : 0x2f86;
        return base;
    }
    if (RDS32(w + 0x14) != 0x4f) return base;
    s16 times = RDS16(w + 0xBE), hp = RDS16(w + 0xAA);
    if (times >= 0x1e) level = face_down ? 2 : 3;
    else if (times >= 0x16) level = 2;
    else if (times >= 0xf && hp >= 0x40) level = 1;
    else level = hp >= 0x80 ? 0 : hp >= 0x40 ? 1 : 2;
    dbg_down_level[0] = (u32)level;
    if (level && on && arcade_fast_getup() && !special_on(w)) level = 0;
    dbg_down_level[1] = (u32)level;
    return (s16)(base + level);
}

// ---- the Royal Rumble: the computer goes for eliminations -----------------------------------------
// Only what the computer wants changes: nothing about how anyone gets thrown out. The game's computer
// player decides a slot's buttons for the step in ovl_e func_800F03F0(slot) and leaves them in the
// slot's AI record (*0x8015A6F0 while it runs: +0x0C buttons held, +0x0E pressed; the dpad is a world
// direction there: up +z, down -z, right +x, left -x), which func_80118368 then gives the wrestler as
// his controller, and the wrestler he's after is his target (+0x08, as the C-right button sets it).
// After the game has decided, for a computer wrestler in the ring (arcade_rumble_ai):
//   - he goes after the one most worn down (the lowest spirit, +0xAE), so the game's own fighting
//     wears that man down;
//   - a man in DANGER (the spirit meter in the blue: +0xEE bit 0) is the one to throw out: at the ropes,
//     he runs at him and hits him (C-up, B: the game's own running attack, which takes a man over the
//     top rope when the ropes are at his back); on the apron, he goes to the ropes in front of him, ties
//     up with him through them and throws him (A, A: the game's own way off the apron); in the middle
//     of the ring, the game's own fighting, at him;
//   - a hold wins nothing in a Rumble (submissions off): he lets go of it (R) after a little punishment.
// Slots a controller drives (the player, even while the arcade has the computer walk him back into
// the ring) are left alone.
#define RR_EDGE     200.0f             // the ropes: the ring's half-size where wrestlers stop
#define RR_NEAR     45.0f              // "at the ropes"
#define RR_RUNUP    120.0f             // the run starts this far straight in from him
#define RR_HIT2     (85.0f * 85.0f)    // B when the runner is this close
#define RR_HOLD     30                 // steps of a hold before letting go (submissions off)
#define RR_FOCUS    45                 // steps between choosing whom to go after
#define NM_AI_CUR   0x8015A6F0u        // -> the AI record of the slot being decided
#define NM_RULES    0x800A9950u        // 0x10 submissions, 0x20 pins, 0x400 out of the ring = out
#define W_DANGER    0x01               // +0xEE: the spirit meter in DANGER

enum { RP_NONE, RP_RUN_UP, RP_RUN, RP_APRON, RP_APRON_GRAB };
typedef struct { s8 prey; u8 phase, t, cool, focus, hold; } RRPlan;
static RRPlan rr_plan[NM_SLOTS_CAP];
u8 dbg_rr_phase[NM_SLOTS_CAP];                 // tests
int dbg_no_rumble_ai;                          // tests: the game's own computer player only
u32 dbg_rr_runs, dbg_rr_aprons;

static float fabs_f(float v) { return v < 0.0f ? -v : v; }
static float wx(u32 w) { return *(volatile float*)(w + 0x120); }
static float wz(u32 w) { return *(volatile float*)(w + 0x128); }
static int w_in(u32 w) { return RDS16(w + 0xA0) != -1 && !(RD16(w + 0x94) & 1); }
static int w_ring(u32 w) { return (RD16(w + 0x82) & 0x7FF) == 1; }
static int w_apron(u32 w) { return (RD16(w + 0x82) & 0x7FF) == 2; }
static int w_danger(u32 w) { return (RD8(w + 0xEE) & W_DANGER) != 0; }
static u32 wr(int s) { return WR_BASE + (u32)s * NM_WRESTLER_SIZE; }
static u16 dpad_to(float dx, float dz) {
    float ax = fabs_f(dx), az = fabs_f(dz);
    u16 b = 0;
    if (az > ax * 0.414f) b |= dz > 0.0f ? 0x0800 : 0x0400;
    if (ax > az * 0.414f) b |= dx > 0.0f ? 0x0100 : 0x0200;
    return b;
}
// the ropes nearest him: (nx, nz) pointing out of the ring, and how far in from them he is
static float rope_of(u32 w, float* nx, float* nz) {
    float x = wx(w), z = wz(w);
    if (fabs_f(x) >= fabs_f(z)) { *nx = x >= 0.0f ? 1.0f : -1.0f; *nz = 0.0f; return RR_EDGE - fabs_f(x); }
    *nx = 0.0f; *nz = z >= 0.0f ? 1.0f : -1.0f; return RR_EDGE - fabs_f(z);
}
static float dist1(u32 a, u32 b) { return fabs_f(wx(a) - wx(b)) + fabs_f(wz(a) - wz(b)); }
// whom to go after: a man in DANGER (on the apron first, then the nearest), else the lowest spirit;
// *danger: whether it's a man in DANGER
static int rr_choose(int slot, u32 me, int* danger) {
    int best = -1, best_d = -1;
    float bs = 1e9f, bd = 1e9f;
    for (int o = 0, n = nm_slots(); o < n; o++) {
        u32 w = wr(o);
        if (o == slot || !w_in(w) || (!w_ring(w) && !w_apron(w))) continue;
        float d = dist1(w, me);
        if (w_danger(w)) {
            float k = w_apron(w) ? d * 0.5f : d;
            if (k < bd) { bd = k; best_d = o; }
        } else if (w_ring(w)) {
            float k = (float)RD16(w + 0xAE) * 6.0f + d;   // (the lowest spirit; nearer ones first)
            if (k < bs) { bs = k; best = o; }
        }
    }
    *danger = best_d >= 0;
    return best_d >= 0 ? best_d : best;
}

void arcade_rumble_ai(s32 slot) {
    if (slot < 0 || slot >= nm_slots()) return;
    RRPlan* p = &rr_plan[slot];
    u32 me = wr(slot), ai = RD32(NM_AI_CUR), rules = RD32(NM_RULES);
    dbg_rr_phase[slot] = p->phase;
    { extern int qa_table_input(s32 slot, u16* held, u16* pressed);   // (QA: the announce table run at, logic.c)
      u16 h = 0, pr = 0;
      if (GAME_PTR(ai) && qa_table_input(slot, &h, &pr)) { RD16(ai + 0x0C) = h; RD16(ai + 0x0E) = pr; return; } }
    if (dbg_no_rumble_ai || !arcade_rumble_ai_on()) return;
    if (!(RD32(NM_MATCH_FLAGS) & 0x4000u) || !(rules & 0x400u) || RDS32(NM_SLOT_PAD + 4u * (u32)slot) != 0 ||
        !GAME_PTR(ai) || !w_in(me) || !w_ring(me)) { p->phase = RP_NONE; return; }
    s32 st = RDS32(me + 0x14);
    if (p->cool) p->cool--;
    if ((st == 0x44 || st == 0x48 || st == 0x4a) && !(rules & 0x10u)) {   // his hold: R lets go
        if (++p->hold > RR_HOLD) { RD16(ai + 0x0C) = 0x0010; RD16(ai + 0x0E) = 0x0010; }   // (ovl_e func_80113474 / func_80137044)
        return;
    }
    p->hold = 0;
    // whom he's after (when he's free to choose)
    if (p->phase == RP_NONE && st == 1 && ++p->focus >= RR_FOCUS) {
        p->focus = 0;
        int danger, o = rr_choose(slot, me, &danger);
        if (o >= 0) {
            RD32(me + 0x08) = (u32)o;
            if (danger && !p->cool) { p->prey = (s8)o; p->phase = w_apron(wr(o)) ? RP_APRON : RP_RUN_UP; p->t = 0; }
        }
    }
    if (p->phase == RP_NONE) return;
    u32 v = wr(p->prey);
    if (!w_in(v) || !w_danger(v) || (!w_ring(v) && !w_apron(v))) { p->phase = RP_NONE; return; }
    float nx, nz, edge = rope_of(v, &nx, &nz);
    float vx = wx(v), vz = wz(v), mx = wx(me), mz = wz(me), dx = vx - mx, dz = vz - mz;
    float d2 = dx * dx + dz * dz;
    u16 held = 0, pressed = 0;
    switch (p->phase) {
    case RP_RUN_UP: {                                            // to the spot straight in from him
        if (st != 1) return;                                     // (busy: the game's own till he's free)
        if (w_apron(v)) { p->phase = RP_APRON; p->t = 0; return; }
        if (edge > RR_NEAR || ++p->t > 240) { p->phase = RP_NONE; p->cool = 30; return; }   // (the game's own fighting)
        float ex = vx - nx * RR_RUNUP - mx, ez = vz - nz * RR_RUNUP - mz;
        if (fabs_f(ex) + fabs_f(ez) > 22.0f) { held = dpad_to(ex, ez); break; }
        RD32(me + 0x08) = (u32)p->prey;
        pressed = 0x0008; held = dpad_to(dx, dz);                // C-up: run at him
        p->phase = RP_RUN; p->t = 0;
        dbg_rr_runs++;
        break;
    }
    case RP_RUN:
        if (++p->t > 90 || (st != 0x22 && p->t > 6)) { p->phase = RP_NONE; p->cool = 45; return; }
        held = dpad_to(dx, dz);
        if (d2 < RR_HIT2) { held |= 0x4000; pressed = 0x4000; } // B: the running attack
        break;
    case RP_APRON: {                                             // he's on the apron: to the ropes in front of him
        if (!w_apron(v)) { p->phase = RP_NONE; return; }
        if (st != 1) return;
        if (++p->t > 240) { p->phase = RP_NONE; p->cool = 45; return; }
        float ix = vx, iz = vz;
        if (nx != 0.0f) ix = nx * (RR_EDGE - 14.0f); else iz = nz * (RR_EDGE - 14.0f);
        float ex = ix - mx, ez = iz - mz;
        if (fabs_f(ex) + fabs_f(ez) > 16.0f) { held = dpad_to(ex, ez); break; }
        RD32(me + 0x08) = (u32)p->prey;
        held = 0x8000; pressed = 0x8000;                         // A: tie up with him through the ropes
        p->phase = RP_APRON_GRAB; p->t = 0;
        dbg_rr_aprons++;
        break;
    }
    case RP_APRON_GRAB:
        if (++p->t > 90) { p->phase = RP_NONE; p->cool = 45; return; }
        if (st == 0x51 && RD32(me + 0x18) == 2) { pressed = 0x8000; held = 0x8000; break; }   // A: off the apron
        if (st == 1 && p->t > 30) p->phase = RP_NONE;
        return;
    }
    RD16(ai + 0x0C) = held;
    RD16(ai + 0x0E) = pressed;
}

// ---- the scene's matrices, kept numbers -------------------------------------------------------------
// func_80002268(node): a model's (and every prop's) world matrices, down its tree -- each node's its own
// (+0x1C) times its parent's (+0x5C of the node at +0x00; none: its own), then its children (+0x04) and the
// nodes after it (+0x08). Here in C, the same, with each result checked: a matrix with a value that isn't a
// number, infinite or out of all reason (a part flung to the ends of the world) is replaced by its parent's
// (the part drawn where its parent is), or none at all. On the PC a NaN that reaches a model makes a
// part of it a giant (the matrices go to fixed point): the ray tracer's slowdowns, its card hanging, its
// memory running out in the Royal Rumble (0.35.10). Counted in the arcade's log (FLIGHT +0x6C..).
#define FLIGHT_BADMTX 0x807F206Cu   // count, the last node, the frame (logic.c's FLIGHT + 0x6C)
static int mtx_bad(u32 m) {
    for (u32 i = 0; i < 16; i++) {
        u32 u = RD32(m + 4 * i);
        if ((u & 0x7F800000u) == 0x7F800000u) return 1;            // NaN / infinite
        if ((u & 0x7F800000u) >= 0x49800000u) return 1;            // |v| >= 2^20 (the arena is a few thousand)
    }
    return 0;
}
// A model's root (a node with no parent) that went bad is put back where it last was (not at the world's
// origin: the identity there drew the whole model flat across the middle of the screen -- Jerry Lawler, the
// Royal Rumble, a back suplex), its last good matrix kept here. Room for every model and prop a scene has: with
// 32, the Royal Rumble's scene had more roots than that, a model's was pushed out before it went bad, and the
// identity drew it at the world's origin (JR and King in the middle of the ring, 2026-10-27).
#define ROOTS_N 256
static u32 root_node[ROOTS_N], root_mtx[ROOTS_N][16];
static int root_at;
static int root_find(u32 n) { for (int i = 0; i < ROOTS_N; i++) if (root_node[i] == n) return i; return -1; }
static void scene_tree(u32 n, int depth) {
    while (n && depth < 64) {
        u32 p = RD32(n);
        if (!p) func_80036710(n + 0x1C, n + 0x5C, 0x40);
        else func_80001E08(n + 0x1C, p + 0x5C, n + 0x5C);
        int bad = mtx_bad(n + 0x5C);
        if (!p && !bad) {                     // (a root that's fine: kept)
            int r = root_find(n);
            if (r < 0) { r = root_at; root_at = (root_at + 1) % ROOTS_N; root_node[r] = n; }
            for (u32 i = 0; i < 16; i++) root_mtx[r][i] = RD32(n + 0x5C + 4 * i);
        }
        if (bad) {
            int r = p ? -1 : root_find(n);
            if (r >= 0) for (u32 i = 0; i < 16; i++) { RD32(n + 0x5C + 4 * i) = root_mtx[r][i]; RD32(n + 0x1C + 4 * i) = root_mtx[r][i]; }
            else if (p && !mtx_bad(p + 0x5C)) func_80036710(p + 0x5C, n + 0x5C, 0x40);
            else for (u32 i = 0; i < 16; i++) RD32(n + 0x5C + 4 * i) = (i % 5 == 0) ? 0x3F800000u : 0u;
            RD32(FLIGHT_BADMTX) = RD32(FLIGHT_BADMTX) + 1; RD32(FLIGHT_BADMTX + 4) = n; RD32(FLIGHT_BADMTX + 8) = RD32(NM_VI_COUNT);
        }
        if (RD32(n + 4)) scene_tree(RD32(n + 4), depth + 1);
        n = RD32(n + 8);
    }
}
// The wrestlers' positions (+0xFC x, +0x100 y, +0x104 z), kept numbers in a match: a bad one put back to
// the last good (where the root's matrix comes from).
static u32 pos_good[6][3];
static void positions_keep(void) {
    s32 mode = RDS32(NM_MODE);
    if (mode != -2 && mode != -6) return;
    for (u32 sl = 0; sl < 6; sl++) {
        u32 w = SIXA_80159990 + 0x358u * sl;
        u16 id = RD16(w + 0xA0);
        if (!id || id == 0xFFFF) continue;
        int bad = 0;
        for (u32 k = 0; k < 3; k++) { u32 u = RD32(w + 0xFC + 4 * k); if ((u & 0x7F800000u) == 0x7F800000u || (u & 0x7F800000u) >= 0x49800000u) bad = 1; }
        if (bad) { if (pos_good[sl][0] | pos_good[sl][1] | pos_good[sl][2]) for (u32 k = 0; k < 3; k++) RD32(w + 0xFC + 4 * k) = pos_good[sl][k]; }
        else for (u32 k = 0; k < 3; k++) pos_good[sl][k] = RD32(w + 0xFC + 4 * k);
    }
}
void scene_world(u32 n) { positions_keep(); scene_tree(n, 0); }
