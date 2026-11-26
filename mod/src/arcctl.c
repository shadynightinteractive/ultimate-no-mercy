// ARCADE controls (operator menu: CONTROLS -> CONTROL MODE): No Mercy on an 8-way joystick and six
// buttons. The cabinet's buttons arrive as the N64 pad's (the controller set-up puts them there):
//     STRIKE = B   GRAPPLE = A   RUN = C-down   DEFEND = R   ACTION = C-left   SPECIAL = C-right
// and in a match each player's pad is turned into the game's own controls before the game reads it:
//   - the joystick moves (the d-pad; a stick that reports as analog is turned into the 8 directions);
//   - STRIKE, GRAPPLE: the game's B and A, as they are (nothing waits on them: the grapple moves, the
//     joystick + STRIKE / GRAPPLE in a tie-up, and A + B together are the game's own);
//   - RUN: the game's C-down;
//   - DEFEND: R and L together (blocks strikes, reverses grapples, whichever comes);
//   - ACTION, by where he is: by a man down, a tap pins him (L), held picks him up (R held); at the
//     ropes, on the apron or outside by the ring, through the ropes (C-up); else C-left (a weapon, or
//     turning the man around);
//   - SPECIAL: the analog stick flicked the joystick's way (up when it's let be): a taunt, or in a
//     grapple with SPECIAL lit, the finisher -- the game's own;
//   - FOCUS = DEFEND + ACTION together, the moment both are down (nothing waits: each is the game's
//     till then): a tap, the next one, nearest to furthest; held with a direction, the one that way
//     on the screen. The game's own changing of focus (on whoever attacks him) is left as it is.
//     Whoever he's on gets a small arrow over his head in the player's colour, fading out once the
//     choosing stops.
// Menus and everything outside a match get the pad as it is.
//
// Two halves: arcctl_port (the controller hook, the pad as it's read: whole numbers only) and
// arcctl_frame (the arcade's frame: where everyone is -- distances, the screen -- and the choosing).
#include "arcade.h"

int arc_controls;                        // 0 regular (the game's), 1 arcade (config "controls")

#define WR_BASE  (NM_WRESTLERS - 8u)
#define WR(s)    (WR_BASE + (u32)(s) * NM_WRESTLER_SIZE)
#define W_TARGET 0x08
#define W_ID     0xA0
#define W_AREA   0x82                    // u16 & 0x7FF: 1 in the ring, 2 on the apron, else outside
#define W_FLAGS  0x8A                    // u8: 0x40 lying on the mat
#define W_OUT    0x94                    // u16 & 1: out of the match
#define W_SPECIAL 0xEE                   // u8 & 6: SPECIAL
#define W_X      0x120
#define W_Y      0x124
#define W_Z      0x128

#define PICKUP_VI   14                   // ACTION held this long by a man down: picks him up
#define FOCUS_TAP_VI 24                  // FOCUS let go within this, no direction used: a tap
#define ARROW_VI    180                  // the arrow's time after the last choosing (3 s)
#define ARROW_FADE  36

enum { ACT_NONE, ACT_PIN, ACT_ROPES, ACT_CLEFT };
enum { FR_NONE, FR_TAP, FR_DIR };
typedef struct {
    u16 prev;                // the pad last time (cabinet buttons)
    u8 chord;                // FOCUS: DEFEND / ACTION do nothing more till both are let go
    u8 synth;                // SPECIAL: the stick's flick being played, its step, its way
    u8 synth_t;
    s8 synth_x, synth_y;
    u8 act;                  // ACTION's meaning since it was pressed
    u32 act_vi;
    u8 pin_out;              // the pin's L, for a moment after the tap
    u8 focus_held, focus_dir_used;
    u32 focus_vi;
    u16 last_dirs;
    u8 req;                  // a choosing for the frame to do (FR_), the direction for FR_DIR
    u16 req_dirs;
    // the frame's: where he is
    s8 slot;                 // the match slot this pad drives (-1 none)
    u8 down_close, down_first, ropes;
    // FOCUS's round, the arrow
    u8 session, order_n, order_i;
    s8 order[8];
    s32 last_target;
    u32 arrow_vi;            // when the arrow was last asked for (0 none)
} Pad;
static Pad pads[4];
u32 dbg_arc_last[4];         // tests: what went to the game (buttons | stick x << 16 | stick y << 24)
u32 dbg_arc_ctx[4];              // tests: the frame's view (slot, down_close, down_first, ropes)
u32 dbg_arc_taps, dbg_arc_tapinfo[8];  // tests: the taps done; the last one's (session, n, i), order

static u32 vi_now(void) { return RD32(NM_VI_COUNT); }
// in the match, and there: not out, not a Rumble entrant still to come (his entrance, 0xA3, or not loaded)
static int w_in(int s) {
    u32 w = WR(s);
    return RDS16(w + W_ID) != -1 && !(RD16(w + W_OUT) & 1) && RDS32(w + 0x14) != 0xA3 && !(RD8(w + 0xF1) & 1);
}
static int w_area(int s) { return RD16(WR(s) + W_AREA) & 0x7FF; }
static int w_lying(int s) { return (RD8(WR(s) + W_FLAGS) & 0x40) != 0; }

static int slot_of_port(int port) {
    for (int s = 0, n = nm_slots(); s < n; s++)
        if (RDS32(NM_SLOT_PAD + 4u * (u32)s) == port + 1 && w_in(s)) return s;
    return -1;
}

void arcctl_reset(void) {
    for (int i = 0; i < 4; i++) {
        u8* b = (u8*)&pads[i];
        for (u32 k = 0; k < sizeof(Pad); k++) b[k] = 0;
        pads[i].slot = -1; pads[i].last_target = -1;
    }
}

// ---- the controller hook ------------------------------------------------------------------------------
static u16 stick_dirs(s8 x, s8 y) {
    u16 b = 0;
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax < 28 && ay < 28) return 0;
    if (ay * 5 > ax * 2) b |= y > 0 ? BTN_DU : BTN_DD;
    if (ax * 5 > ay * 2) b |= x > 0 ? BTN_DR : BTN_DL;
    return b;
}
// SPECIAL: the stick flicked (held a few reads, then let go)
#define FLICK_N 4

// ACTION's meaning, from where he is when it's pressed
static void act_begin(Pad* p, u32 at) {
    p->act_vi = at;
    p->act = p->down_close ? ACT_PIN : p->ropes ? ACT_ROPES : ACT_CLEFT;
}

void arcctl_port(int port, u16 in, s8 sx, s8 sy, u16* ob, u8* ox, u8* oy) {
    if (port < 0 || port > 3) return;
    Pad* p = &pads[port];
    int me = p->slot;
    if (me < 0 || me != slot_of_port(port)) { p->prev = in; return; }   // (not his: as it is)
    u32 now = vi_now();
    u16 edge = (u16)(in & ~p->prev);
    p->prev = in;
    u16 out = (u16)(in & (BTN_START | BTN_Z | BTN_L | BTN_CU));   // (anything else a pad has: as it is)
    s8 outx = 0, outy = 0;
    u16 dirs = (u16)((in & (BTN_DU | BTN_DD | BTN_DL | BTN_DR)) | stick_dirs(sx, sy));
    const u16 DA = BTN_R | BTN_CL;           // DEFEND, ACTION

    // STRIKE (B), GRAPPLE (A): as they are
    out |= (u16)(in & (BTN_A | BTN_B));

    // SPECIAL (C-right): the stick flicked the joystick's way (up when it's let be)
    if (edge & BTN_CR) {
        s8 fx = (s8)((dirs & BTN_DR) ? 1 : (dirs & BTN_DL) ? -1 : 0);
        s8 fy = (s8)((dirs & BTN_DU) ? 1 : (dirs & BTN_DD) ? -1 : 0);
        if (!fx && !fy) fy = 1;
        s8 m = (fx && fy) ? 56 : 80;
        p->synth = 1; p->synth_t = 0; p->synth_x = (s8)(fx * m); p->synth_y = (s8)(fy * m);
    }

    // FOCUS: DEFEND + ACTION -- at once, the moment both are down (each goes to the game as it is till
    // then; nothing more from either till both are let go)
    if (p->chord && !(in & DA)) p->chord = 0;
    if (!p->chord && (in & DA) == DA && (edge & DA)) {
        p->chord = 1; p->act = ACT_NONE; p->pin_out = 0;
        p->focus_held = 1; p->focus_dir_used = 0; p->focus_vi = now; p->last_dirs = dirs;
    }
    if (p->focus_held) {
        u16 newd = (u16)(dirs & ~p->last_dirs);
        if (dirs && (newd || (!p->focus_dir_used && now - p->focus_vi > 6))) {
            if (newd || !p->focus_dir_used) { p->req = FR_DIR; p->req_dirs = dirs; }
            p->focus_dir_used = 1;
        }
        p->last_dirs = dirs;
        if ((in & DA) != DA) {
            p->focus_held = 0;
            if (!p->focus_dir_used && now - p->focus_vi <= FOCUS_TAP_VI) p->req = FR_TAP;
        }
        dirs = 0;                                            // (choosing: he stands)
    }
    out |= dirs;

    // RUN
    out |= (u16)(in & BTN_CD);

    // DEFEND (R): R and L together (by a man down, nearer than any other: nothing -- R and L are his
    // pick-up and pin there, ACTION's)
    if ((in & BTN_R) && !p->down_first && !p->chord) out |= BTN_R | BTN_L;

    // ACTION
    if ((edge & BTN_CL) && !p->chord) act_begin(p, now);
    if (p->act == ACT_PIN) {
        if (in & BTN_CL) { if (now - p->act_vi >= PICKUP_VI) out |= BTN_R; }   // held: pick him up
        else { if (now - p->act_vi < PICKUP_VI) p->pin_out = 4; p->act = ACT_NONE; }
    } else if (p->act == ACT_ROPES) { if (in & BTN_CL) out |= BTN_CU; else p->act = ACT_NONE; }
    else if (p->act == ACT_CLEFT) { if (in & BTN_CL) out |= BTN_CL; else p->act = ACT_NONE; }
    if (p->pin_out) { out |= BTN_L; p->pin_out--; }

    if (p->synth) {
        if (p->synth_t < FLICK_N - 1) { outx = p->synth_x; outy = p->synth_y; }
        if (++p->synth_t >= FLICK_N) p->synth = 0;
    }
    *ob = out; *ox = (u8)outx; *oy = (u8)outy;
    dbg_arc_last[port] = (u32)out | ((u32)(u8)outx << 16) | ((u32)(u8)outy << 24);
}

// ---- the frame: where everyone is, the choosing ---------------------------------------------------------
static float fabsf_(float v) { return v < 0 ? -v : v; }
static float wx(int s) { return *(volatile float*)(WR(s) + W_X); }
static float wy(int s) { return *(volatile float*)(WR(s) + W_Y); }
static float wz(int s) { return *(volatile float*)(WR(s) + W_Z); }
static float dist2(int a, int b) { float dx = wx(a) - wx(b), dz = wz(a) - wz(b); return dx * dx + dz * dz; }

// the opponents (in the match, not him), nearest first
static int opponents(int me, s8* out) {
    int n = 0;
    float d[8];
    for (int s = 0, m = nm_slots(); s < m && n < 8; s++) {
        if (s == me || !w_in(s)) continue;
        float ds = dist2(me, s);
        int i = n++;
        while (i > 0 && d[i - 1] > ds) { out[i] = out[i - 1]; d[i] = d[i - 1]; i--; }
        out[i] = (s8)s; d[i] = ds;
    }
    return n;
}

// the screen: where a wrestler is on it (640 x 480, the operator menu's units). The camera's matrix is
// view x projection, row vectors; the match's x runs the other way to the picture's.
static int to_screen(float x, float y, float z, float* sx, float* sy) {
    volatile float* m = (volatile float*)NM_CAMERA_MATRIX;
    float rx = -x;
    float cx = rx * m[0] + y * m[4] + z * m[8] + m[12];
    float cy = rx * m[1] + y * m[5] + z * m[9] + m[13];
    float cw = rx * m[3] + y * m[7] + z * m[11] + m[15];
    if (cw < 1.0f) return 0;
    *sx = (cx / cw * 0.5f + 0.5f) * 640.0f;
    *sy = (0.5f - cy / cw * 0.5f) * 480.0f;
    return 1;
}
float dbg_arc_head = 80.0f;          // tests: the arrow's height over him (standing; his y is his middle, the head top about +83)
static int head_on_screen(int s, float* sx, float* sy) {
    float up = w_lying(s) ? 30.0f : dbg_arc_head;
    return to_screen(wx(s), wy(s) + up, wz(s), sx, sy);
}

static void focus_set(Pad* p, int me, int t) {
    RDS32(WR(me) + W_TARGET) = t;
    p->last_target = t;
    p->arrow_vi = (vi_now() - 1u) | 1u;   // (never 0, never ahead of now)
}
static void focus_tap(Pad* p, int me) {
    if (!p->session) {                 // a fresh round: the order as they stand now
        p->order_n = (u8)opponents(me, p->order);
        p->session = 1;
        if (!p->order_n) return;
        s32 cur = RDS32(WR(me) + W_TARGET);
        p->order_i = (u8)((p->order[0] == cur && p->order_n > 1) ? 1 : 0);
    } else {
        if (!p->order_n) return;
        p->order_i = (u8)((p->order_i + 1) % p->order_n);
    }
    int t = p->order[p->order_i];
    if (!w_in(t)) { p->session = 0; return; }
    focus_set(p, me, t);
}
// the one in the joystick's direction on the screen
static void focus_dir(Pad* p, int me, u16 dirs) {
    float dx = (float)(((dirs & BTN_DR) != 0) - ((dirs & BTN_DL) != 0));
    float dy = (float)(((dirs & BTN_DD) != 0) - ((dirs & BTN_DU) != 0));   // (screen y runs down)
    if (dx == 0.0f && dy == 0.0f) return;
    float mx, my;
    if (!head_on_screen(me, &mx, &my)) return;
    s8 o[8];
    int n = opponents(me, o), best = -1;
    float bs = -2.0f;
    for (int i = 0; i < n; i++) {
        float sx, sy;
        if (!head_on_screen(o[i], &sx, &sy)) continue;
        float vx = sx - mx, vy = sy - my, len = vx * vx + vy * vy;
        if (len < 1.0f) continue;
        // how well it lines up (the cosine, squared with its sign), the nearer a little better
        float dot = vx * dx + vy * dy, dl = dx * dx + dy * dy;
        float c = dot * (dot < 0 ? -dot : dot) / (len * dl);
        c -= len * 0.0000004f;
        if (c > bs) { bs = c; best = o[i]; }
    }
    if (best >= 0 && bs > 0.1f) { focus_set(p, me, best); p->session = 0; }
}

void arcctl_frame(void) {
    u32 now = vi_now();
    for (int port = 0; port < 4; port++) {
        Pad* p = &pads[port];
        int me = (arc_controls && RDS32(NM_MODE) == -2 && RD16(NM_MATCH_FRAMES) != 0xFFFF) ? slot_of_port(port) : -1;
        if (me != p->slot) {
            p->slot = (s8)me; p->session = 0; p->chord = 0; p->act = 0; p->synth = 0;
            p->focus_held = 0; p->pin_out = 0;
            p->req = FR_NONE; p->arrow_vi = 0; p->last_target = me >= 0 ? RDS32(WR(me) + W_TARGET) : -1;
        }
        if (me < 0) continue;
        // where he is: a man down by him (and nearer than any on his feet), the ropes by him
        s8 o[8];
        int n = opponents(me, o), down = -1, standing = -1;
        for (int i = 0; i < n; i++) {
            if (w_lying(o[i])) { if (down < 0) down = o[i]; }
            else if (standing < 0) standing = o[i];
        }
        p->down_close = down >= 0 && dist2(me, down) < 75.0f * 75.0f && w_area(me) == w_area(down) && !w_lying(me);
        p->down_first = p->down_close && (standing < 0 || dist2(me, down) < dist2(me, standing));
        int area = w_area(me);
        float ax = fabsf_(wx(me)), az = fabsf_(wz(me)), edge_d = 200.0f - (ax > az ? ax : az);
        p->ropes = area == 2 || (area == 1 && edge_d < 40.0f) || (area != 1 && edge_d > -90.0f);
        dbg_arc_ctx[port] = (u32)(u8)me | (u32)p->down_close << 8 | (u32)p->down_first << 16 | (u32)p->ropes << 24;
        // the choosing asked for
        if (p->req == FR_TAP) {
            focus_tap(p, me); dbg_arc_taps++;
            dbg_arc_tapinfo[0] = p->session | (u32)p->order_n << 8 | (u32)p->order_i << 16;
            for (int k = 0; k < 4; k++) dbg_arc_tapinfo[1 + k] = (u32)(s32)p->order[k];
        }
        else if (p->req == FR_DIR) focus_dir(p, me, p->req_dirs);
        p->req = FR_NONE;
        if (p->arrow_vi && now - p->arrow_vi > ARROW_VI) { p->arrow_vi = 0; p->session = 0; }
        // the game changing it (on whoever attacks him): the arrow shows that one a moment too
        s32 tgt = RDS32(WR(me) + W_TARGET);
        if (tgt != p->last_target) {
            if (tgt >= 0 && tgt < nm_slots() && tgt != me && n > 1) p->arrow_vi = (now - 1u) | 1u;
            p->last_target = tgt;
            p->session = 0;
        }
    }
}

// ---- the arrow over whoever each player is on --------------------------------------------------------
u32 dbg_arc_draw[4];         // tests: why the arrow is (not) drawn: 1 none, 2 old, 3 no target, 4 off the screen, 9 drawn
void arcctl_draw(int port, u32 rgb) {
    if (port < 0 || port > 3) return;
    Pad* p = &pads[port];
    dbg_arc_draw[port] = 1;
    if (!p->arrow_vi || p->slot < 0) return;
    u32 age = vi_now() - p->arrow_vi;
    dbg_arc_draw[port] = 2;
    if (age > ARROW_VI) return;
    s32 t = RDS32(WR(p->slot) + W_TARGET);
    dbg_arc_draw[port] = 3;
    if (t < 0 || t >= nm_slots() || t == p->slot || !w_in(t)) return;
    float sx, sy;
    dbg_arc_draw[port] = 4;
    if (!head_on_screen(t, &sx, &sy)) return;
    if (sx < -20.0f || sx > 660.0f || sy < -20.0f || sy > 500.0f) return;
    dbg_arc_draw[port] = 9 | (u32)(s32)sx << 8 | (u32)(s32)sy << 20;
    u32 a = 230;
    if (age > ARROW_VI - ARROW_FADE) a = 230u * (ARROW_VI - age) / ARROW_FADE;
    int bob = age < 40 ? (int)((40 - age) / 8) : 0;        // (a small drop as it comes, settling)
    int x = (int)sx, y = (int)sy - 30 - bob;
    // a chevron pointing down: a dark edge, the colour, a lighter top
    const int W = 13, H = 12;
    for (int r = 0; r < H + 2; r++) {
        int hw = W + 1 - (r * (W + 1)) / (H + 1);
        if (hw < 1) hw = 1;
        draw_fill(x - hw, y + r - 1, hw * 2, 1, 0x10101000u | (a * 7 / 10));
    }
    for (int r = 0; r < H; r++) {
        int hw = W - (r * W) / H;
        if (hw < 2) break;
        u32 c = r < 2 ? ((rgb | 0x404040u) & 0xFFFFFFu) : rgb;
        draw_fill(x - hw + 1, y + r, (hw - 1) * 2, 1, (c << 8) | a);
    }
}
