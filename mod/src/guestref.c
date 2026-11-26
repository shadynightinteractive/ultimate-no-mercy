// guestref.c -- the Guest Referee match (the menus' Guest Referee: set-up kind 0x80 with 1 single, 4 triple threat, 8
// handicap; match flag 0x80000), the game's own match type. Its men: the competitors, each a side of the match (+0xE9
// 0x10, "taking part"), and the guest referee, a man of his own slot who is no side of it (+0xE9 0x04 in the single's
// set-up, func_80115000: placed in the ring at (-80, 80), his target slot 0).
//
// Changed for new features (Ultimate No Mercy) -- guestref, 2026-11-22 (the user: "please QA guest referee mode. Camera
// is off").
//
// THE CAMERA. The game's frame camera (update_match_camera, func_800E2344) picks its shot by the match: for a one-on-one
// the two-man ring camera on the two competitors (func_800E38A4) and its special shots (func_800E4B60). With match flag
// 0x80000 -- the Guest Referee match, and only it -- it skips all of that (0x800E263C: the flag sends it straight to
// 0x800E2654) and keeps ONE shot for the whole match: the keep-everyone-in-view shot (func_800E28C0 in the ring), framing
// every man present, the referee too (slots_in_play_mask, for match flags & 0x01C82000). That is the stock game's own
// behaviour -- measured the same in the stock ROM in the rig (scratch\guestref\stockref.py) and in this port -- and it is
// what looks off: one high, far shot from the same side all match, pulled further back and up whenever the referee wanders
// (out of the ring, up the ramp of a fight-on stage), with the competitors small in it, never the closer angles a one-on-
// one has.
// So: while the frame's camera is worked out, a Guest Referee match the COMPUTER referees is shown as the match it is
// between the competitors -- the camera code sees the match flags without 0x80000 (guestref_camera_enter / _leave, round
// arena_ringstage.c ringstage_cam_call, the game's whole camera step): it then frames the men taking part (the referee is
// not one) with the same shots and the same fight-on-stage blend as any one-on-one (or, three and more taking part -- the
// triple threat's guest referee -- its keep-them-in-view shot on the competitors). The referee is in the picture when he
// is where the action is (counting a pin he is by the two men). Nothing else sees the flags changed: they are put back
// as soon as the camera step returns, and the match's rules, the count and the AI read them as ever.
// A Guest Referee match a PLAYER referees keeps the game's own camera, which keeps him in view: he has to see himself.
#include "arcade.h"

#define GR_RECORD(s)   (SIXA_80159990 + 0x358u * (u32)(s))   // the wrestlers' records (offsets as slots.c's)
#define GR_TARGET      0x08u    // s32: the slot he is after (his target)
#define GR_STATE       0x14u    // s32: what he is doing
#define GR_DOWN        0x80u    // u16: & 8 lying
#define GR_AREA        0x82u    // u16: & 0x7FF -- 1 the ring, 2 the apron, 4 the floor outside
#define GR_RESULT      0x94u    // u16: & 1 out of the match
#define GR_STAMINA     0xAAu    // s16: stamina (a hit takes some)
#define GR_ID          0xA0u    // s16: the Superstar (-1 nobody)
#define GR_ROLE        0xE9u    // u8: 0x10 a side of the match, 0x04 the guest referee (the single's set-up)
#define GR_X           0xFCu
#define GR_Z           0x104u
#define GR_SLOTS       4        // (the Guest Referee match's men: three in the single, four in the triple / handicap)
#define GR_FLAG        0x80000u // the match flags' referee-match bit (set_match_flags_from_kind for a 0x80 kind)
#define GR_ROPES       200.0f   // the ring's half size where wrestlers stop (gameplay.c RR_EDGE)
#define GR_APRON       310.0f   // the apron's edge
#define GR_SIDE_ALONG  180.0f   // a man climbs in along a side within this of its middle (clear of the posts)
#define B_A            0x8000u
#define B_B            0x4000u
#define B_CUP          0x0008u  // C-up: through the ropes / climb in
#define B_CLEFT        0x0002u  // C-left: the guest referee's count at a pin
#define B_CRIGHT       0x0001u  // C-right: the target switch
#define B_MASH         0xC030u  // A+B+L+R: struggling, as the computer player struggles

int qa_gr_p1;                    // QA (logic.c reads the option): 1 player 1 a competitor, 2 player 1 the guest referee, 4 player
                                 // 1 a competitor in any match (automerge, 2026-11-23: the plain one-on-one's playability check)
u32 qa_gr_words[4];              // QA (logic.c's state): [0] the hits player 1 landed in the ring (to 255) | outside << 8 |
                                 // steps a man was pinned while player 1 refereed << 16; [1] steps played | steps out << 16;
                                 // [2] the referee's target | the man covering << 8 | the referee's state << 16 (now);
                                 // [3] steps he switched target at a pin | steps he pressed for the count << 16
static s16 gr_last_stamina = -1;

static u32 rec(int s) { return GR_RECORD(s); }
static float fabs_f(float v) { return v < 0.0f ? -v : v; }
static float wx(int s) { return *(volatile float*)(rec(s) + GR_X); }
static float wz(int s) { return *(volatile float*)(rec(s) + GR_Z); }
static int area(int s) { return RD16(rec(s) + GR_AREA) & 0x7FF; }
static int present(int s) { return RDS16(rec(s) + GR_ID) != -1 && !(RD16(rec(s) + GR_RESULT) & 1); }
static s32 state_of(int s) { return RDS32(rec(s) + GR_STATE); }
static int free_state(s32 st) { return st == 1 || st == 2 || st == 3; }     // standing, walking, running
static int competitor(int s) { return present(s) && (RD8(rec(s) + GR_ROLE) & 0x10); }
static u16 dpad_to(float dx, float dz) {            // (gameplay.c's: the eight ways; up is +z, right +x)
    float ax = fabs_f(dx), az = fabs_f(dz);
    u16 b = 0;
    if (az > ax * 0.414f) b |= dz > 0.0f ? 0x0800 : 0x0400;
    if (ax > az * 0.414f) b |= dx > 0.0f ? 0x0100 : 0x0200;
    return b;
}
// the way out of the ring from where he is: (nx, nz) toward the nearest ropes, and how far in from them he is
static float way_out(float x, float z, float* nx, float* nz) {
    if (fabs_f(x) >= fabs_f(z)) { *nx = x >= 0.0f ? 1.0f : -1.0f; *nz = 0.0f; return GR_ROPES - fabs_f(x); }
    *nx = 0.0f; *nz = z >= 0.0f ? 1.0f : -1.0f; return GR_ROPES - fabs_f(z);
}
// from the floor back into the ring: to the middle of his side (round the posts), then in over the apron (C-up)
static u16 climb_in(int me, int t) {
    float x = wx(me), z = wz(me);
    int along_x = fabs_f(x) < fabs_f(z);
    float across = along_x ? fabs_f(z) : fabs_f(x), along = along_x ? x : z, sg = (along_x ? z : x) >= 0.0f ? 1.0f : -1.0f;
    u16 in = along_x ? (sg > 0.0f ? 0x0400 : 0x0800) : (sg > 0.0f ? 0x0200 : 0x0100);
    if (fabs_f(along) > GR_SIDE_ALONG) {
        float t_al = along > 0.0f ? GR_SIDE_ALONG - 20.0f : -(GR_SIDE_ALONG - 20.0f), t_ac = sg * (across > 420.0f ? across : 420.0f);
        return dpad_to((along_x ? t_al : t_ac) - x, (along_x ? t_ac : t_al) - z);
    }
    if (across > GR_APRON + 40.0f) return in;
    return (u16)(in | ((t & 15) < 13 ? B_CUP : 0));
}
static int my_slot(void) {                          // (the slot controller 1 drives)
    for (int s = 0; s < GR_SLOTS; s++) if (present(s) && RDS32(NM_SLOT_PAD + 4u * (u32)s) == 1) return s;
    return -1;
}

// the guest referee's slot: present, no side of the match (+0xE9 0x10 clear); -1 when there is none
static int referee_slot(void) {
    for (int s = 0; s < GR_SLOTS; s++) if (present(s) && !(RD8(rec(s) + GR_ROLE) & 0x10)) return s;
    return -1;
}
// round the frame's camera step (arena_ringstage.c ringstage_cam_call): a Guest Referee match with a computer referee seen
// by the camera code as the one-on-one (or triple threat) between its competitors -- the referee-match bit taken off the
// match flags for the while. Returns what was taken off, for guestref_camera_leave.
u32 guestref_camera_enter(void) {
    u32 flags = RD32(NM_MATCH_FLAGS);
    if (!(flags & GR_FLAG)) return 0;
    int r = referee_slot();
    if (r < 0 || RDS32(NM_SLOT_PAD + 4u * (u32)r) > 0) return 0;   // (a player referees: the game's own camera, him in it)
    RD32(NM_MATCH_FLAGS) = flags & ~GR_FLAG;
    return GR_FLAG;
}
void guestref_camera_leave(u32 taken) { RD32(NM_MATCH_FLAGS) |= taken; }

// player 1 a competitor: after the other competitor nearest him
static u16 play_competitor(int me, int t) {
    int o = -1; float best = 1e9f;
    for (int s = 0; s < GR_SLOTS; s++) {
        if (s == me || !competitor(s)) continue;
        float d = fabs_f(wx(s) - wx(me)) + fabs_f(wz(s) - wz(me));
        if (d < best) { best = d; o = s; }
    }
    if (o < 0) return 0;
    {   // (QA: his hits -- the opponent's stamina going down; in a one-on-one only player 1 is fighting him)
        s16 sta = RDS16(rec(o) + GR_STAMINA);
        if (gr_last_stamina >= 0 && sta < gr_last_stamina) {
            u32 in = qa_gr_words[0] & 0xFF, out = (qa_gr_words[0] >> 8) & 0xFF;
            if (area(o) & 1) { if (in < 255) in++; } else if (out < 255) out++;
            qa_gr_words[0] = (qa_gr_words[0] & 0xFFFF0000u) | in | out << 8;
        }
        gr_last_stamina = sta;
    }
    s32 st = state_of(me);
    float x = wx(me), z = wz(me), dx = wx(o) - x, dz = wz(o) - z;
    if (area(me) & 4) qa_gr_words[1] += 0x10000u;
    if (free_state(st)) {
        if ((area(o) & 4) && (area(me) & 1)) {               // (him out: to the nearest ropes, through them -- C-up)
            float nx, nz, d = way_out(x, z, &nx, &nz);
            return (u16)(dpad_to(nx, nz) | (d < 40.0f && (t & 7) < 2 ? B_CUP : 0));
        }
        if (area(me) & 2) {                                  // (on the apron: off it after him, or in)
            if (area(o) & 4) { float nx, nz; way_out(x, z, &nx, &nz); return (u16)(dpad_to(nx, nz) | ((t & 15) < 2 ? B_CUP : 0)); }
            return (u16)((t & 15) < 13 ? B_CUP : 0);
        }
        if ((area(me) & 4) && !(area(o) & 4)) return climb_in(me, t);   // (him in, me out: back in)
        if (fabs_f(dx) + fabs_f(dz) > 75.0f) return dpad_to(dx, dz);    // (after him)
        if (RD16(rec(o) + GR_DOWN) & 8) return (t % 20) < 2 ? B_A : 0;     // (him lying: the ground grapple, now and then)
        int k = (t / 10) % 4;
        return (t % 10) < 2 ? (k == 3 ? B_A : B_B) : 0;      // (mostly strikes, now and then a grapple)
    }
    if (st == 81) return (t % 10) < 2 ? B_A : 0;              // (his grapple: the move)
    if ((st == 46 || st == 49 || st == 67 || st == 69 || st == 71 || st == 73 || st == 75) && (t % 4) < 2) return B_MASH;
    return 0;
}

// player 1 the guest referee: near the competitors, by a pin when there is one -- and there, his count. The game's count
// for the guest referee (try_tag_partner_save, func_8013F2B4, read): C-left HELD by the +0xE9 0x04 man, his target (+0x08)
// the man COVERING (68 / 74: +0x80 & 0xC0 -- the pinned man has not that mark), within 200 of him, sends him to 166, the
// count on the HUD. His target is slot 0 from the set-up; C-right (the target switch) moves it to the other man.
static u16 play_referee(int me, int t) {
    float tx = 0.0f, tz = 0.0f; int n = 0, pinned = -1, cover = -1;
    for (int s = 0; s < GR_SLOTS; s++) {
        if (s == me || !competitor(s)) continue;
        tx += wx(s); tz += wz(s); n++;
        s32 st = state_of(s);
        if (st == 69 || st == 75) pinned = s;
        if (st == 68 || st == 74) cover = s;
    }
    if (!n) return 0;
    if (pinned >= 0) { tx = wx(pinned); tz = wz(pinned); qa_gr_words[0] += 0x10000u; }
    else { tx /= (float)n; tz /= (float)n; }
    // (in his count, 166: C-left pressed again and again -- the count waits in its phase 2 for a press, and a press from
    //  its beat's frame 5 queues the next beat: one beat a press, three for the fall)
    if (state_of(me) == 166) { qa_gr_words[3] += 0x10000u; return (t & 1) ? B_CLEFT : 0; }
    if (!free_state(state_of(me))) return 0;
    float dx = tx - wx(me), dz = tz - wz(me), d = fabs_f(dx) + fabs_f(dz);
    if ((area(me) & 4) && pinned >= 0 && !(area(pinned) & 4)) return climb_in(me, t);
    qa_gr_words[2] = (u32)(RDS32(rec(me) + GR_TARGET) & 0xFF) | (u32)(cover & 0xFF) << 8 | (u32)(state_of(me) & 0xFF) << 16;
    if (cover >= 0) {
        float cx = wx(cover) - wx(me), cz = wz(cover) - wz(me);
        if (fabs_f(cx) + fabs_f(cz) > 150.0f) return dpad_to(cx, cz);                 // (to the man covering)
        if (RDS32(rec(me) + GR_TARGET) != cover) { qa_gr_words[3] += 1; return (t % 30) < 2 ? B_CRIGHT : 0; }   // (the target switch)
        qa_gr_words[3] += 0x10000u;
        return (t & 1) ? B_CLEFT : 0;                                                 // (C-left, pressed again and again: the count)
    }
    return d > 110.0f ? dpad_to(dx, dz) : 0;
}

// (QA, automerge 2026-11-23: qa_gr_p1 4 -- player 1 a competitor in ANY match, not only a Guest Referee one: the same
//  player at his controller, for the playability check of a plain one-on-one)
void guestref_qa_pad(u32 pad) {
    if (!qa_gr_p1 || (!(RD32(NM_MATCH_FLAGS) & GR_FLAG) && qa_gr_p1 != 4)) return;
    u16 mf = RD16(NM_MATCH_FRAMES);
    if (mf == 0xFFFF) return;
    int me = my_slot();
    if (me < 0) return;
    qa_gr_words[1] = (qa_gr_words[1] & 0xFFFF0000u) | ((qa_gr_words[1] + 1u) & 0xFFFFu);
    u16 b = (qa_gr_p1 == 2) ? play_referee(me, (int)mf) : play_competitor(me, (int)mf);
    RD16(pad) |= b;
}
