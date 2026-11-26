// A model's skeleton: func_80015D8C (a model's descriptor -> its bones and parts), in C, so that models
// that come from WrestleMania 2000 and WCW/nWo Revenge are put together the way their own game puts
// them together. (Revenge's models are WM2000's kind -- the same parts, joins and formats; its
// 8000BDD0 differs from WM2000's 800166B4 only in the few lines SKEL_WM_REV marks.)
//
// The two games read meshes, textures and vertex groups the same way (NM func_8000C1A8 = WM 8000B4EC,
// NM 8000BEB0 = WM 8000B1F4, NM 8000C58C = WM 8000B94C, NM 8000D284 = WM 8000C7F0); they differ in how a
// model's parts are joined. A part's mesh has up to three vertex groups (its header's byte 3: hi nibble =
// the first group's size, lo nibble = the second's): the first drawn with one neighbour bone's matrix,
// the second with another's, the rest with the part's own -- and which neighbours those are is this
// function's choice. No Mercy cut the chest and the upper arms in two (parts 22-24, children of 1-3), so
// its joins differ from WM2000's (WM 800166B4), and a WM2000 mesh joined No Mercy's way names bones a
// WM2000 model doesn't have: the RSP draws with no matrix and the game hangs. A model whose descriptor
// says it is WM2000's (SKEL_MAGIC) is joined WM2000's way; every other model No Mercy's, exactly as the
// game's own function does (gen/checkskel.py runs both on many descriptors and compares).
//
// No Mercy's arrays per model (six-man engine: moved, six_addrs.h): bones 28 x 0xDC, parts 28 x 0x34.
// Bones 1-19 the body, 20 the head (a child of 5), 21 a second head piece (a child of 20), 22-24 the
// split chest and arms (No Mercy's), 25-27 attachments (a hat, ...). WM2000's are 25 bones: 22-24 its
// attachments. A WM2000 model here: its attachments in 25-27, 21-24 empty.
#include "arcade.h"
#include "gamefn.h"
#include "skel.h"

GAMEFN(void, func_8000C58C, 0x8000C58Cu, (u32 bone, u32 part, u32 pool, s32 mesh, u32 parent, u32 child, u32 next));
GAMEFN(void, func_8000D284, 0x8000D284u, (u32 bone, u32 part, u32 first, u32 second));
GAMEFN(void, func_80015D54, 0x80015D54u, (u32 bone));
GAMEFN(s32,  func_80018510, 0x80018510u, (s32 code, s32 field));

#define BONES(slot) (SIXA_80091CC0 + (u32)(slot) * 0x1810u)
#define PARTS(slot) (SIXA_800A4050 + (u32)(slot) * 0x5B0u)
#define POOL(slot)  (SIXA_80070D20 + (u32)(slot) * 0x10u)
#define JOINTS(slot) (SIXA_80076BC0 + (u32)(slot) * 0x40u)
#define BONE(k) (bones + (u32)(k) * 0xDCu)
#define PART(k) (parts + (u32)(k) * 0x34u)
#define OBJ(k)  RD32(BONE(k) + 0x14u)          // the bone's draw object (its matrix and offsets)
#define F32(a)  (*(volatile float*)(a))

// the expression system's parts (eyes, mouth): their vertex count and mesh, kept to put back
static void joints(u32 parts, s32 slot) {
    for (u32 i = 0; i < 4; i++) {
        s32 k = RDS8(0x800484E0u + i);
        u32 src = PART(k), dst = JOINTS(slot) + i * 0x10u;
        RD8(dst + 2) = RD8(src + 2);
        RD8(dst + 3) = RD8(src + 3);
        RD32(dst + 4) = RD32(src + 0x14);
    }
}

// the body: parts 1-19, each a child of the root, in a row
static void body(u32 desc, u32 bones, u32 parts, u32 pool) {
    RD32(bones + 4) = BONE(1);
    for (u32 k = 1; k < 20; k++) {
        s16 mesh = RDS16(desc + k * 8);
        if (mesh) func_8000C58C(BONE(k), PART(k), pool, mesh, bones, 0, BONE(k + 1));
    }
    RD32(BONE(19) + 8) = 0;
}

// No Mercy's own (func_80015D8C)
static void setup_nm(u32 desc, s32 slot) {
    u32 bones = BONES(slot), parts = PARTS(slot), pool = POOL(slot);
    body(desc, bones, parts, pool);
    for (u32 k = 22; k < 25; k++) {                     // the split chest and arms
        RD8(BONE(k) + 0x18) = RD8(BONE(k - 21) + 0x18);
        func_8000C58C(BONE(k), PART(k), pool, RDS16(desc + 0xB0 + (k - 22) * 8), BONE(k - 21), 0, 0);
        RD32(BONE(k - 21) + 4) = BONE(k);
    }
    if (RDS16(desc + 0xA0)) {                           // the head
        RD8(BONE(20) + 0x18) = RD8(BONE(5) + 0x18);
        func_8000C58C(BONE(20), PART(20), pool, RDS16(desc + 0xA0), BONE(5), 0, 0);
        RD32(BONE(5) + 4) = BONE(20);
    } else {
        func_80015D54(BONE(20));
        RD32(BONE(5) + 4) = 0;
    }
    if (RDS16(desc + 0xA8)) {
        RD8(BONE(21) + 0x18) = RD8(BONE(20) + 0x18);
        func_8000C58C(BONE(21), PART(21), pool, RDS16(desc + 0xA8), BONE(20), 0, 0);
        RD32(BONE(20) + 4) = BONE(21);
    } else {
        func_80015D54(BONE(21));
        RD32(BONE(20) + 4) = 0;
    }
    for (u32 i = 0; i < 3; i++) func_80015D54(BONE(25 + i));
    for (u32 i = 0; i < 3; i++) {                       // attachments: code nibbles = parent, joins, slot
        s16 code = RDS16(desc + 0x25A + i * 2);
        if (!code) continue;
        s16 at = (s16)func_80018510(code, 3), parent = (s16)func_80018510(code, 0);
        if ((RD8(desc + 0x254) & 0x80) && !at) continue;
        u32 b = BONE(25 + at), p = PART(25 + at);
        func_8000C58C(b, p, pool, RDS16(desc + 0xC8 + at * 8), BONE(parent), 0, 0);
        RD32(b + 8) = RD32(BONE(parent) + 4);
        RD32(BONE(parent) + 4) = b;
        if ((s16)func_80018510(code, 1) || (s16)func_80018510(code, 2)) {
            s16 j1 = (s16)func_80018510(code, 1), j2 = (s16)func_80018510(code, 2);
            func_8000D284(b, p, BONE(j1), BONE(j2));
        }
        F32(OBJ(25 + at) + 0x24) = -20.0f;
    }
    func_8000D284(BONE(2), PART(2), BONE(1), BONE(3));
    func_8000D284(BONE(23), PART(23), BONE(1), BONE(3));
    func_8000D284(BONE(6), PART(6), BONE(8), BONE(7));
    func_8000D284(BONE(13), PART(13), BONE(15), BONE(14));
    func_8000D284(BONE(11), PART(11), BONE(9), BONE(12));
    func_8000D284(BONE(16), PART(16), BONE(17), BONE(19));
    func_8000D284(BONE(1), PART(1), 0, BONE(14));
    func_8000D284(BONE(22), PART(22), BONE(7), BONE(14));
    func_8000D284(BONE(5), PART(5), 0, BONE(3));
    func_8000D284(BONE(3), PART(3), 0, BONE(5));
    F32(OBJ(12) + 0x10) = F32(OBJ(12) + 0x10) - 5.0f;
    F32(OBJ(19) + 0x10) = F32(OBJ(19) + 0x10) - 5.0f;
    F32(OBJ(20) + 0x24) = -5.0f;
    F32(OBJ(5) + 0x24) = 0.0f;
    F32(OBJ(21) + 0x24) = -15.0f;
    F32(OBJ(7) + 0x24) = -5.0f;
    F32(OBJ(14) + 0x24) = -5.0f;
    F32(OBJ(6) + 0x24) = -7.0f;
    F32(OBJ(13) + 0x24) = -7.0f;
    F32(OBJ(8) + 0x24) = 7.0f;
    F32(OBJ(15) + 0x24) = 7.0f;
    F32(OBJ(26) + 0x24) = -5.0f;
    F32(OBJ(27) + 0x24) = -7.0f;
    joints(parts, slot);
}

// WrestleMania 2000's (its 800166B4): a nibble of an attachment's code -> a bone (WM 80017840, its table
// 0x80047144 = No Mercy's 0x8004FDE8)
static s32 wm_field(s32 field, s32 code) {
    return field < 3 ? RDS8(0x8004FDE8u + (((s32)(s16)code >> (field * 4)) & 0xF)) : 0;
}
static u32 wm_bone(u32 bones, s32 field, s32 code) {
    s32 k = (s16)wm_field(field, code);
    if (k == 2) k = 1;
    return k ? BONE(k) : 0;
}

static void setup_wm(u32 desc, s32 slot) {
    u32 bones = BONES(slot), parts = PARTS(slot), pool = POOL(slot);
    body(desc, bones, parts, pool);
    // No Mercy's split chest and arms (22-24): no parts here, but the bones are kept where No Mercy has
    // them (children of 1-3, at their place): the match reads every body bone's place (func_80012130's
    // contact points, bones 1-22: the ring's edges, the barricade, ...), and a bone left out of the
    // skeleton stays at the ring's centre.
    for (u32 k = 22; k < 25; k++) {
        u32 b = BONE(k), parent = BONE(k - 21);
        func_80015D54(b);
        for (u32 i = 0; i < 16; i++) F32(b + 0x1C + i * 4) = (i % 5 == 0) ? 1.0f : 0.0f;   // (identity)
        RD32(b) = parent;
        RD32(b + 8) = RD32(parent + 4);
        RD32(parent + 4) = b;
    }
    if (RDS16(desc + 0xA0)) {
        RD8(BONE(20) + 0x18) = RD8(BONE(5) + 0x18);
        func_8000C58C(BONE(20), PART(20), pool, RDS16(desc + 0xA0), BONE(5), 0, 0);
        RD32(BONE(5) + 4) = BONE(20);
    } else {
        func_80015D54(BONE(20));
        RD32(BONE(5) + 4) = 0;
    }
    func_80015D54(BONE(21));                                  // (no second head piece)
    RD32(BONE(20) + 4) = 0;
    for (u32 i = 0; i < 3; i++) {                             // attachments i: bone 25 + i (WM2000's 22 + i)
        s16 code = RDS16(desc + SKEL_WM_ATTACH + i * 2);
        u32 b = BONE(25 + i), p = PART(25 + i);
        u32 parent = wm_bone(bones, 0, code);
        if (!parent) { func_80015D54(b); continue; }
        func_8000C58C(b, p, pool, RDS16(desc + 0xC8 + i * 8), parent, 0, 0);
        RD32(b + 8) = RD32(parent + 4);
        RD32(parent + 4) = b;
        u32 j1 = wm_bone(bones, 2, code), j2 = wm_bone(bones, 1, code);
        func_8000D284(b, p, j1, j2);
        F32(OBJ(25 + i) + 0x24) = (float)(s16)(((s32)(s16)code >> 12) * 2);   // (WM 8001787C)
        s32 from = (s16)wm_field((s32)i, RDS16(desc + SKEL_WM_PLACE));       // (WM 8001788C)
        if (from) {
            u32 o = OBJ(25 + i), f = OBJ(from);
            RD32(o + 0x0C) = RD32(f + 0x0C);
            RD32(o + 0x10) = RD32(f + 0x10);
            RD32(o + 0x14) = RD32(f + 0x14);
        }
    }
    func_8000D284(BONE(2), PART(2), BONE(1), BONE(3));
    func_8000D284(BONE(4), PART(4), BONE(3), BONE(5));
    func_8000D284(BONE(7), PART(7), BONE(6), BONE(1));
    func_8000D284(BONE(14), PART(14), BONE(13), BONE(1));
    func_8000D284(BONE(11), PART(11), BONE(9), BONE(12));
    func_8000D284(BONE(16), PART(16), BONE(17), BONE(19));
    func_8000D284(BONE(1), PART(1), BONE(7), BONE(14));
    F32(OBJ(12) + 0x10) = F32(OBJ(12) + 0x10) - 5.0f;
    F32(OBJ(19) + 0x10) = F32(OBJ(19) + 0x10) - 5.0f;
    u32 flags = RD32(desc + SKEL_WM_FLAGS);
    if (flags & SKEL_WM_REV) {                                // Revenge's (its 8000BDD0): no head offsets
        RD32(OBJ(3) + 0x14) = RD32(OBJ(4) + 0x14);
        F32(OBJ(20) + 0x24) = 0.0f;                           // (none left from the slot's last model)
        F32(OBJ(4) + 0x24) = 0.0f;
        F32(OBJ(5) + 0x24) = 0.0f;
    } else {
        RD32(OBJ(3) + 0x14) = RD32(OBJ(5) + 0x14);
        if (flags & 0x20) {
            F32(OBJ(20) + 0x24) = 0.0f;
            F32(OBJ(4) + 0x24) = -4.0f;
            F32(OBJ(5) + 0x24) = 0.0f;
        } else if (flags & 4) {
            F32(OBJ(20) + 0x24) = -3.0f;
            F32(OBJ(5) + 0x24) = 2.0f;
        } else {
            F32(OBJ(20) + 0x24) = 3.0f;
            F32(OBJ(5) + 0x24) = -4.0f;
        }
    }
    F32(OBJ(7) + 0x24) = -5.0f;
    F32(OBJ(14) + 0x24) = -5.0f;
    F32(OBJ(6) + 0x24) = -7.0f;
    F32(OBJ(13) + 0x24) = -7.0f;
    F32(OBJ(8) + 0x24) = 7.0f;
    F32(OBJ(15) + 0x24) = 7.0f;
    joints(parts, slot);
}

// func_80014F5C(slot), every frame: which of a model's parts are drawn (a bone's +0x18: 0x80 = not
// drawn; func_800151A0 sets one by where its bone is). No Mercy's never draws part 4 (a WM2000 model's
// back of the head) and draws its split chest and arms with parts 1-3; WrestleMania 2000's (its
// 80014F60) does neither.
GAMEFN(void, func_800151A0, 0x800151A0u, (u32 bone, float x, float y, float z));
#define D64(a) (*(volatile double*)(a))
static float nm_fabs(float v) { return (double)v < 0.0 ? -v : v; }     // (func_8000567C)
u32 dbg_skel_hide[6];
void skel_visible(s32 slot) {
    slot = (s16)slot;
    u32 bones = BONES(slot);
    if (RD8(BONE(1) + 0x18) & 0x80) return;
    u32 at = SIXA_800A1130 + (u32)slot * 0xB24u + 0xD0u;       // each bone's place (x, y, z), 0x7C apart
    int each = 1;
    if (RD32(0x80097EC0u) & 0x10000u) {
        if ((double)F32(at + 4) >= D64(0x80055C50u)) {
            if (D64(0x80055C58u) < (double)nm_fabs(F32(at + 8))) each = 0;
            else if (D64(0x80055C60u) < (double)nm_fabs(F32(at))) each = 0;
        }
    }
    if (each) {
        for (u32 k = 1; k < 20; k++) {
            u32 r = at + (k - 1) * 0x7Cu;
            func_800151A0(BONE(k), -F32(r), F32(r + 4), F32(r + 8));
        }
    } else {
        u8 v = (double)F32(at + 8) < D64(0x80055C68u) ? 5 : 0;
        for (u32 k = 1; k < 20; k++) RD8(BONE(k) + 0x18) = v;
    }
    if (skel_origin(SIXA_800A8980 + (u32)slot * 0x270u) == SKEL_WM) {
        RD8(BONE(20) + 0x18) = RD8(BONE(5) + 0x18);
        RD8(BONE(21) + 0x18) = RD8(BONE(5) + 0x18);
    } else {
        u8 f1 = RD8(BONE(1) + 0x18);
        RD8(BONE(4) + 0x18) = 0x80;
        RD8(BONE(22) + 0x18) = f1;
        RD8(BONE(23) + 0x18) = RD8(BONE(2) + 0x18);
        RD8(BONE(24) + 0x18) = RD8(BONE(3) + 0x18);
        RD8(BONE(20) + 0x18) = RD8(BONE(5) + 0x18);
        RD8(BONE(21) + 0x18) = RD8(BONE(5) + 0x18);
    }
    for (u32 i = 0; i < 3; i++) {
        u32 b = BONE(25 + i), parent = RD32(b);
        if (parent) RD8(b + 0x18) = RD8(parent + 0x18);
    }
    if (slot >= 0 && slot < 6 && dbg_skel_hide[slot])          // (tests: parts hidden by bit)
        for (u32 k = 1; k < 28; k++) if (dbg_skel_hide[slot] & (1u << k)) RD8(BONE(k) + 0x18) = 0x80;
}

// func_80014E44(slot, flag): a model shown (or hidden) whole, as the menus show one (the Superstar select,
// the VS page: func_800159FC): every bone's +0x18 the flag, the head hidden without one, and its shadow's
// body bones (the table at 0x8004804C: which of them it has). No Mercy's hides part 4 here too (its own
// models have none to draw); a WrestleMania 2000 / Revenge model's part 4 is its neck: kept, as in a match.
#define SHADOW(slot) (SIXA_80071DA0 + (u32)(slot) * 0x1130u)
u32 dbg_skel_wmneck = 1;     // tests: 0 = hide a WM2000 / Revenge model's neck in the menus (as before)
void skel_show(s32 slot, s32 flag) {
    slot = (s16)slot;
    u8 f = (u8)flag;
    u32 bones = BONES(slot), sh = SHADOW(slot);
    for (u32 k = 1; k < 28; k++) RD8(BONE(k) + 0x18) = f;
    if (!RD32(BONE(20))) RD8(BONE(20) + 0x18) = 0x80;
    if (skel_origin(SIXA_800A8980 + (u32)slot * 0x270u) != SKEL_WM || !dbg_skel_wmneck) RD8(BONE(4) + 0x18) = 0x80;
    for (u32 k = 1; k < 20; k++)
        RD8(sh + k * 0xDCu + 0x18u) = RD8(0x8004804Cu + k) ? RD8(BONE(k) + 0x18) : 0x80;
}

// func_80016464(descriptor, slot, phase): the parts' textures. Phase 0: each part's picture and
// palette (and the palette group the descriptor names made group 0); phase 1: the attachments', the second
// head piece's, the face drawn (func_80019D88), the hands' kept (func_80017190). A WrestleMania 2000
// model's pictures are whole already (gen/wmport.py: as WM2000 drew them) and it has no parts 21-24:
// its parts loaded as WM2000's 80016D0C does (phase 0: the body but for the head pieces 4, 5 and 20,
// and the attachments; phase 1: the head pieces).
GAMEFN(void, func_80013FD4, 0x80013FD4u, (u32 part, s32 tex, s32 pal, s32 slot, s32 k));
GAMEFN(void, func_8000CA18, 0x8000CA18u, (u32 part, s32 tex, s32 pal));
GAMEFN(void, func_8000CAF8, 0x8000CAF8u, (u32 part, s32 tex, s32 pal, s32 at, s32 n, s32 shift, s32 z));
GAMEFN(void, func_80019550, 0x80019550u, (s32 slot, u32 colors, s32 k, s32 from));
GAMEFN(void, func_80019D88, 0x80019D88u, (s32 slot, u32 desc));
GAMEFN(void, func_80017190, 0x80017190u, (s32 slot));
#define HANDLES(slot) (SIXA_80076CF8 + (u32)(slot) * 8u)
static void tex_wm(u32 desc, s32 slot, s32 phase, u32 parts) {
    if (phase == 0) {
        for (u32 k = 1; k < 20; k++) {
            if (k == 4 || k == 5) continue;
            s16 tex = RDS16(desc + k * 8 + 2), pal = RDS16(desc + k * 8 + 4);
            if (!tex) continue;
            func_80013FD4(PART(k), tex, pal, slot, (s32)k);
            if (RDS8(desc + 0x255) > 0)
                func_80036710(RD32(PART(k) + 0x24) + (u32)(RDS8(desc + 0x255) * 0x20), RD32(PART(k) + 0x24), 0x20);
        }
        for (u32 i = 0; i < 3; i++) {
            s16 tex = RDS16(desc + 0xCA + i * 8);
            if (tex) func_80013FD4(PART(25 + i), tex, RDS16(desc + 0xCC + i * 8), slot, (s32)(25 + i));
        }
        if (!(RD32(desc + SKEL_WM_FLAGS) & SKEL_WM_NMHANDS)) hands_wm_keep(slot);
        return;
    }
    if (RDS16(desc + 0x22)) func_80013FD4(PART(4), RDS16(desc + 0x22), RDS16(desc + 0x24), slot, 4);
    if (RDS16(desc + 0x2A)) func_8000CA18(PART(5), RDS16(desc + 0x2A), RDS16(desc + 0x2C));
    if (RDS16(desc + 0xA4)) func_8000CA18(PART(20), RDS16(desc + 0xA2), RDS16(desc + 0xA4));
    func_80019D88(slot, desc);
    if (RD32(desc + SKEL_WM_FLAGS) & SKEL_WM_NMHANDS) func_80017190(slot);   // (No Mercy's hands: kept as its own)
    for (u32 i = 0; i < 2; i++) {
        u32 h = HANDLES(slot) + i * 4;
        if (RD32(h)) { func_8000093C(RD32(h)); RD32(h) = 0; }
    }
}
void parts_size_put(s32 slot);
void skel_textures(u32 desc, s32 slot, s32 phase) {
    slot = (s16)slot; phase = (s16)phase;
    if (phase == 0) parts_size_put(slot);   // (2026-11-13, revbody: a folder wrestler's own size, over his height's: parts.c)
    u32 parts = PARTS(slot);
    if (skel_origin(desc) == SKEL_WM) { tex_wm(desc, slot, phase, parts); return; }
    if (phase == 0) {
        for (u32 k = 1; k < 25; k++) {
            if (RDS8(0x80048020u + k) < 0) continue;
            u32 part = PART(k);
            s32 tex = RDS16(desc + k * 8 + 2), pal = RDS16(desc + k * 8 + 4);
            if (k == 20 && RD16(desc + 0x258) && RD8(0x80097FD8u) == 3) {
                tex = RDS16(desc + 0x258); pal = RDS16(desc + 0xA4);
            }
            func_80013FD4(part, tex, pal, slot, (s32)k);
            s32 g = RDS8(desc + 0x255);
            if (g <= 0) continue;
            if (k == 20 && (RD8(desc + 0x254) & 2)) continue;
            func_80036710(RD32(part + 0x24) + (u32)(g << 5), RD32(part + 0x24), 0x20);
        }
        return;
    }
    u8 colors[4];
    for (u32 i = 0; i < 3; i++) {
        u32 d = desc + i * 8;
        if (!RDS16(d + 0xCA)) continue;
        colors[2] = RD8(d + 0xCE); colors[3] = RD8(d + 0xCF);
        func_8000CA18(PART(25 + i), RDS16(d + 0xCA), RDS16(d + 0xCC));
        func_80019550(slot, (u32)colors, (s32)(25 + i), 0);
    }
    s32 h = RDS16(desc + 0x190);
    if (h) { colors[2] = RD8(desc + 0x192); colors[3] = RD8(desc + 0x193); }
    else {
        h = RDS16(desc + 0x240);
        if (h) { colors[2] = RD8(desc + 0x242); colors[3] = RD8(desc + 0x243); }
    }
    if ((s16)h) {
        func_8000CA18(PART(21), (s16)(h + 1), (s16)h);
        func_80019550(slot, (u32)colors, 0x15, 0);
    }
    func_80019D88(slot, desc);
    func_80017190(slot);
    for (u32 i = 0; i < 2; i++) {
        u32 a = HANDLES(slot) + i * 4;
        if (RD32(a)) { func_8000093C(RD32(a)); RD32(a) = 0; }
    }
    if (RD8(desc + 0x254) & 2) {
        func_80013FD4(PART(20), RDS16(desc + 0xA2), RDS16(desc + 0xA4), slot, 20);
        return;
    }
    if (!RD16(desc + 0x258) || RD8(0x80097FD8u) != 3) return;
    for (u32 i = 0; i < 2; i++) RD32(HANDLES(slot) + i * 4) = func_800007D4(0x808);
    func_80036710(RD32(PART(20) + 0x20), RD32(HANDLES(slot) + 4), 0x808);
    u32 pal = func_800007D4(0x200);
    func_80036710(RD32(PART(20) + 0x24), pal, 0x200);
    func_8000CA18(PART(20), RDS16(desc + 0xA2), RDS16(desc + 0xA4));
    if (RDS16(desc + 0x250) > 0)
        func_8000CAF8(PART(20), RDS16(desc + 0x250), RDS16(desc + 0x252), 0x60, 0x20, RDS8(desc + 0x26C), 0);
    s32 v = RDS16(desc + 0x230);
    if (v) func_8000CAF8(PART(20), (s16)(v + 1), (s16)v, 0xA0, 0x20, RDS8(desc + 0x26D), 0);
    if (RDS16(desc + 0x24C) > 0) func_8000CAF8(PART(20), RDS16(desc + 0x24C), RDS16(desc + 0x24E), 0x80, 0x20, 0, 0);
    else if (RDS16(desc + 0x228)) func_8000CAF8(PART(20), RDS16(desc + 0x228), 0x1EB1, 0x80, 0x20, 0, 0);
    v = RDS16(desc + 0x18C);
    if (v) func_8000CAF8(PART(20), (s16)(v + 1), (s16)v, 0xE0, 0x20, 0, 0);
    v = RDS16(desc + 0x248);
    if (v) func_8000CAF8(PART(20), (s16)(v + 1), (s16)v, 0x20, 0x20, 0, 0);
    func_80036710(RD32(PART(20) + 0x20), RD32(HANDLES(slot)), 0x808);
    func_80036710(pal, RD32(PART(20) + 0x24), 0x200);
    func_8000093C(pal);
}

int skel_origin(u32 desc) {
    u32 m = RD32(desc + SKEL_MAGIC_AT);
    return m == SKEL_MAGIC_WM ? SKEL_WM : SKEL_NM;
}

// func_80015D8C(descriptor, slot)
void skel_setup(u32 desc, s32 slot) {
    slot = (s16)slot;
    if (skel_origin(desc) == SKEL_WM) setup_wm(desc, slot);
    else setup_nm(desc, slot);
}

// func_8000CAF8(part, picture file, palette file, at, n, shift, z): a decal put into a part's picture -- its n colours
// into the part's palette from colour `at` on (2 * at bytes in, 2 * n bytes, up to 256 colours' end), its pixels into
// the picture as those colours. The face's and the skin's (func_800199CC, func_80019B38, func_80019C54, func_80019D88:
// gen/patches.py) come here, and go in only where the palette has the room: from it to the nearest thing of that
// model's after it. A model made as the match goes on (a Royal Rumble's entrant) has its palettes packed -- each its
// own colours only, the next thing right after -- and the skin's colours went over the end of one of its display lists
// (2026-10-27: Jeff Jarrett's chest, its list without its end, the renderer drawing it over and over, the game slowing
// to a stop; QA's write-watch named this). Made before a match, a model's palettes have the 256 colours' room: unchanged.
#define HEAP_MARK 0xCAFE4ABEu   // (the game's heap: each block's header ends with it)
u32 dbg_pal_skipped, dbg_pal_calls, dbg_pal_wm, dbg_pal_room;   // tests: decals not put in; all asked for; of them a ported model's; skipped for room
void pal_group_guard(u32 part, s32 tex, s32 pal, s32 at, s32 n, s32 shift, s32 z) {
    u32 p = RD32(part + 0x24);
    dbg_pal_calls++;
    u32 ring = 0x807F2C80u + 16u * (dbg_pal_calls % 40u);   // (QA: the last 40 calls, read from a RAM snapshot)
    RD32(ring) = part; RD32(ring + 4) = p; RD32(ring + 8) = (u32)at | (u32)n << 16; RD32(ring + 12) = 0xFFFFFFFFu;
    if (GAME_PTR(p) && at >= 0 && n > 0) {
        // (the palette's room: up to the heap's header of the block after it -- its pointer, size, 0, then 0xCAFE4ABE:
        // 16 colours' 0x20 bytes, 32's 0x40, 256's 0x200. Its own memory, whichever model's it is: a part of the model
        // being made can still name the palette of the one made there before -- now in the ring, its palettes packed:
        // the Rumble's breaks all came as an entrant was being made)
        u32 room = 0x200u;
        for (u32 off = 0; off < 0x210u; off += 4)
            if (RD32(p + off) == HEAP_MARK) { room = off >= 12u ? off - 12u : 0; break; }
        RD32(ring + 12) = room;
        u32 end = 2u * (u32)(at & 0xFF) + 2u * (u32)(n & 0xFF);
        if (end > 0x200u) end = 0x200u;
        if (end > room) { dbg_pal_room++; dbg_pal_skipped++; return; }
    }
    func_8000CAF8(part, tex, pal, at, n, shift, z);
}
