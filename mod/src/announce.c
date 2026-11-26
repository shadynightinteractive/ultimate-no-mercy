// Jim Ross and Jerry Lawler at the announce table, in every match: No Mercy's own models of them (Superstar
// 0x904: costume 0 JR, costume 1 King), built outside the match's slots and seated behind the table.
//
// The table (func_8001DB4C sets it up with the arena, func_8001DF38 every frame): four pieces at
// 0x8007AC40 (0xDC each), state 0x8007D78C (0 standing, 1 breaking, 2 fading, 3 gone); none in a cage.
//
// Their models: the game builds a model only into a match slot's arrays (func_800140B4). A slot is
// borrowed for it -- every per-model array of it (six/spec.py MAIN: gen/mkannsave.py) kept, the model
// loaded, its bones and parts copied out to the announcer's own (heap) and the pointers between them
// moved there, the slot's arrays put back. What the model has on the heap (meshes, pictures, its mesh
// pool) is then the announcer's, freed when the next scene's are made. Its shadow (a model of its own the
// load makes too) is freed straight away and its draw entries taken out of the frame's draw list.
//
// Their poses: No Mercy's (ann_poses.h, from its animations: announce/mkposes.py), turned into the bones'
// matrices the way func_80012BE0 does it for a wrestler, every frame; the root placed at the seat.
#include "arcade.h"
#include "gamefn.h"
#include "ann_slot.h"
#include "ann_poses.h"
#include "ann_chair.h"

GAMEFN(void, func_8000C548, 0x8000C548u, (u32 bone, u32 part, s32 kind, u32 x, u32 y, u32 z));   // a bone set up
GAMEFN(void, func_80001684, 0x80001684u, (u32 bone, s32 n));               // ... its draw object
GAMEFN(void, func_8001089C, 0x8001089Cu, (u32 pool, u32 vertices, u32 commands));
GAMEFN(u32,  func_8000A900, 0x8000A900u, (s32 id, s32 costume));             // a Superstar's looks (packed)
GAMEFN(void, func_8001AC44, 0x8001AC44u, (u32 src, u32 dst));                // ... unpacked
GAMEFN(s32,  func_8000AA24, 0x8000AA24u, (s32 id, s32 costume));             // ... his size
GAMEFN(void, func_800148F0, 0x800148F0u, (s32 slot, u32 looks, s32 size));   // the model made from the looks
GAMEFN(void, func_80016464, 0x80016464u, (u32 desc, s32 slot, s32 phase));   // ... its pictures
GAMEFN(void, func_8000C58C, 0x8000C58Cu, (u32 bone, u32 part, u32 pool, s32 mesh, u32 parent, u32 child, u32 next));
GAMEFN(void, func_8000CA18, 0x8000CA18u, (u32 part, s32 tex, s32 pal));       // a part's picture
GAMEFN(void, func_8001DB4C, 0x8001DB4Cu, (void));                           // the table set up
GAMEFN(void, func_8001DF38, 0x8001DF38u, (void));                           // the table, every frame
GAMEFN(void, func_80002268, 0x80002268u, (u32 bone));                       // a bone tree's world matrices

#define F32(a) (*(volatile float*)(a))
#define ANN_ID 0x904
u32 dbg_ann_id = ANN_ID;   // tests: another Superstar
#define ANN_SLOT 0                       // the slot borrowed for the build
#define BONES_SZ 0x1810u
#define PARTS_SZ 0x5B0u
#define BONES(k) (SIXA_80091CC0 + (u32)(k) * BONES_SZ)
#define PARTS(k) (SIXA_800A4050 + (u32)(k) * PARTS_SZ)
#define POOL(k)  (SIXA_80070D20 + (u32)(k) * 0x10u)
#define DRAW_LIST  SIXA_80060BA0         // u32[]: the frame's draw objects (stock 0x80060BA0; moved with the main arrays
                                         //  past six slots -- six_addrs.h)
#define DRAW_COUNT 0x800619B2u           // s16
#define TABLE      0x8007AC40u           // its pieces (bones), 4 x 0xDC
#define TABLE_STATE 0x8007D78Cu
#define MATCH_RECS 0x80097FE0u          // the match's Superstars' records, 0x114 each (looks at +0xFC)
#define MATCH_REC_IDS 0x800BD4D0u       // u16: their IDs
#define MATCH_REC_COSTUMES 0x800A9358u  // u8: their costumes
#define RING_OBJ   0x800785C4u           // the ring's draw object (its view depth at +0x28)

typedef struct {
    u32 bones, parts;          // heap: 28 x 0xDC, 28 x 0x34
    u32 pool[4];               // the mesh pool (func_8001089C's record)
    float base[16];            // the model's own scale matrix (0x80069F38 of its slot)
    u8 cls;                    // its body class (0x8006A268)
    u8 hidden[28];             // the bones the model doesn't draw
    float r0[28];              // their draw objects' depth bias (+0x24) as the model has it
    u32 chair_bones, chair_parts;   // heap: his chair, 2 pieces (seat and back, legs)
    u32 pics[2];               // (QA) his head's (part 20's) picture and palette, as the load made them (+0x20, +0x24)
    float good[20][16];        // his bones' last local matrices that were numbers (a pose that isn't: these instead)
    int have_good;
} Announcer;
// their costume records' looks (+0x31) and size (No Mercy's roster: ordinal 64, costumes 0 / 1). Only the first
// ANN_LOOKS_N bytes are the looks (what func_8000BAD8 copies to a match record's +0xFC); the rest is the record's
// +0x48.. (weight 0x01F5, height, music, video) and must NOT go into a match record: +0xFC + 0x1D runs 5 bytes into
// the next record (slot 1's: its height, size and weight became F5 25 13 00 -> 4864 "pounds", a super heavyweight in
// ovl_e func_80114CC4, and every lift on him the struggling body slam / suplex, entries 159 / 161; 2026-10-31)
#define ANN_LOOKS_N 0x17
_Static_assert(0xFC + ANN_LOOKS_N <= 0x114, "the looks stay inside match record 0");
static const u8 ann_looks[2][0x1D] = {
    { 0x06, 0x41, 0x00, 0x74, 0x00, 0x28, 0x00, 0x64, 0x36, 0xAA, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x09, 0x00, 0x00, 0x00, 0x33, 0x01, 0xF5, 0x24, 0x13, 0x00, 0x00 },
    { 0x05, 0x30, 0x00, 0xA8, 0x00, 0x00, 0x00, 0x44, 0x36, 0x9A, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x06, 0x00, 0x00, 0x00, 0x32, 0x01, 0xF5, 0x25, 0x13, 0x00, 0x00 },
};
static const u8 ann_scale[2] = { 0x00, 0x6A };   // (func_8000A9A8 of the records' heights)
static Announcer ann[2];

// (QA: the frame's draw-list entries that are the announcers' -- an entry's +4 is its bone, in an announcer's own block)
u32 ann_listed(void) {
    u32 n = 0; s16 c = RDS16(DRAW_COUNT);
    for (s16 i = 0; i < c && i < 8192; i++) {
        u32 d = RD32(DRAW_LIST + (u32)i * 4);
        if (!GAME_PTR(d)) continue;
        u32 bone = RD32(d + 4);
        for (int k = 0; k < 2; k++)
            if (ann[k].bones && bone >= ann[k].bones && bone < ann[k].bones + BONES_SZ) n++;
    }
    return n;
}
static int ann_built, ann_no_table, ann_broken, ann_frames;
int ann_qa_stand;                          // QA (qa_ann_stand): on their feet from the start, as with the table broken
static float ann_stand[2];                 // 0 seated .. 1 standing
static float ann_dodge[2];                 // how far each has stepped aside (along the table)
static int ann_dodge_t[2];
static s32 ann_flinch[2];                  // frames into a flinch (0: none)
static float ann_talk_w[2], ann_flinch_w[2], ann_look[2];
static s32 ann_talk_t[2], ann_talk_side[2], ann_cool[2];
static u32 rnd_state = 0x1234567u;
static u32 rnd(u32 n) { rnd_state = rnd_state * 1103515245u + 12345u; return (rnd_state >> 16) % n; }
u32 dbg_ann_builds, dbg_ann_fail, dbg_ann_frames, dbg_ann_draw[2];
u32 dbg_ann_flag = 0xFF;
u32 dbg_ann_f9 = 0xFF, dbg_ann_f9_was;
float dbg_ann_eye[3];
float dbg_ann_r;    // tests: added to the bones' sort depth   // tests: the bones' draw flag (0xFF: as the table)
float dbg_ann_xyz[4] = { -55.0f, 55.0f, -637.0f, 0.0f };   // tests: JR's x, King's x, their z, yaw (up to the table, their
                                                            //  chairs clear of the barricade, z -690: 2026-10-27, they sat on its
                                                            //  line at -692, the chairs half in it, their arms out to the table)
int ann_free;                   // (on their feet without a table: drawn by the ring's side, as a Superstar: tron.c)
#define ANN_TABLE_BACK (-612.0f)      // the table's back edge (moved up: -687 + ANN_TABLE_UP): a camera behind it, behind the table

static void copy(u32 d, u32 s, u32 n) { for (u32 i = 0; i < n; i++) RD8(d + i) = RD8(s + i); }
static u32 arr_at(u32 stock, u32 k) {
    for (u32 i = 0; i < ANN_SLOT_ARRAYS; i++)
        if (ann_slot_arrays[i].stock == stock) return ann_slot_arrays[i].at + k * ann_slot_arrays[i].size;
    return 0;
}

// ---- the depth test between them ----------------------------------------------------------------------
// The game draws no depth buffer at all: every piece of everyone in the order of its distance (its centre's). Close
// up from the side, a piece of the far one came after a piece of the near one -- JR's arm through King's chest (the
// user's F6 camera, 2026-10-28 evening). So the two of them draw with a depth buffer of their own (ANN_ZBUF, the
// frame's size, cleared where the frame's scene starts: tron.c tron_scene_begin): each piece's list copied with its
// render mode's depth compare and update on (the piece's own E2 render mode) and the RSP's depth on, called, then
// its render mode as it was. Nothing else writes or tests depth: the table, the chairs, the ring, the Superstars
// keep the game's order with them, as before.
#define ANN_ZDL       0x806F0000u      // the pieces' lists (expansion RAM, empty in a match: 0x806D0000..0x8077FFFF)
#define ANN_ZDL_SLOT  0x140u           // (40 commands each)
#define ANN_ZDL_CMDS  32u
int ann_zbuf_on = 1;                   // (tron.c clears the buffer while it's 1 and they're built)
static u32 zdl_orig[2][28];
u32 dbg_ann_zwraps;
u32 ann_zdl_orig(u32 dl) {             // (the hard camera's picture: their lists as the game has them)
    if (dl < ANN_ZDL || dl >= ANN_ZDL + 56u * ANN_ZDL_SLOT) return dl;
    u32 k = (dl - ANN_ZDL) / ANN_ZDL_SLOT;
    return zdl_orig[k / 28u][k % 28u] ? zdl_orig[k / 28u][k % 28u] : dl;
}
int ann_zbuf_wanted(void) { return ann_built && ann_zbuf_on; }
static void ann_zwrap(int who, u32 b, u32 d) {
    u32 w = ANN_ZDL + ((u32)who * 28u + b) * ANN_ZDL_SLOT;
    u32 dl = RD32(d + 0x34);
    if (!ann_zbuf_on) { if (dl == w && zdl_orig[who][b]) RD32(d + 0x34) = zdl_orig[who][b]; return; }
    if (dl == w) return;                                             // (made already, the game's list unchanged)
    if (!GAME_PTR(dl)) return;
    // the piece's own list: its state (render mode, combiner, texture), then a branch to its triangles
    u32 n = 0, rm = 0, o = w;
    for (; n < ANN_ZDL_CMDS; n++) {
        u32 w0 = RD32(dl + 8u * n), w1 = RD32(dl + 8u * n + 4u), op = w0 >> 24;
        if (op == 0xDF) break;
        if (op == 0xE2 && w0 == 0xE200001Cu) { rm = w1; w1 |= 0x30u; }      // (Z_CMP | Z_UPD)
        if (op == 0xDE && ((w0 >> 16) & 0xFF) == 1) {                     // its branch: called, then put back
            RD32(o) = 0xD9FFFFFFu; RD32(o + 4) = 0x00000001u; o += 8;    // G_ZBUFFER
            RD32(o) = 0xDE000000u; RD32(o + 4) = w1; o += 8;
            RD32(o) = 0xD9FFFFFEu; RD32(o + 4) = 0; o += 8;
            if (rm) { RD32(o) = 0xE200001Cu; RD32(o + 4) = rm; o += 8; }
            RD32(o) = 0xDF000000u; RD32(o + 4) = 0;
            zdl_orig[who][b] = dl;
            RD32(d + 0x34) = w;
            dbg_ann_zwraps++;
            return;
        }
        if (op == 0xDE || op == 0x01 || op == 0xDA || op == 0xDC || op == 0xFD) {
            if (op != 0xDE && op != 0xFD) return;                      // (not the shape looked for: left as it is)
        }
        RD32(o) = w0; RD32(o + 4) = w1; o += 8;
    }
}

// ---- the models -----------------------------------------------------------------------------------
// what the model has on the heap (func_8000D3F8 without its draw object: that one is the last scene's)
static void free_part(u32 part) {
    if (!RD8(part)) return;
    RD8(part) = 0;
    if (RD32(part + 0x10)) { func_8000093C(RD32(part + 0x10)); }
    else {
        if (RD32(part + 0x14) && !RD32(part + 0x30)) func_8000093C(RD32(part + 0x14));
        if (RD32(part + 0x18) && !RD32(part + 0x30)) func_8000093C(RD32(part + 0x18));
        if (RD32(part + 0x1C) && !RD32(part + 0x30)) func_8000093C(RD32(part + 0x1C));
    }
    RD32(part + 0x10) = RD32(part + 0x14) = RD32(part + 0x18) = RD32(part + 0x1C) = 0;
    if (RD32(part + 0x20)) func_8000093C(RD32(part + 0x20));
    if (RD32(part + 0x24)) func_8000093C(RD32(part + 0x24));
    RD32(part + 0x20) = RD32(part + 0x24) = 0;
}
static void free_chair(Announcer* a);
static void teardown(Announcer* a) {
    free_chair(a);
    if (!a->bones) return;
    for (u32 b = 0; b < 28; b++) {
        u32 bone = a->bones + b * 0xDC;
        if (RD32(bone + 0x10)) func_8000093C(RD32(bone + 0x10));
        free_part(a->parts + b * 0x34);
    }
    if (a->pool[0]) func_8000093C(a->pool[0]);
    if (a->pool[2]) func_8000093C(a->pool[2]);
    func_8000093C(a->bones);
    func_8000093C(a->parts);
    a->bones = a->parts = 0;
    a->pool[0] = a->pool[1] = a->pool[2] = a->pool[3] = 0;
}

// func_800140B4 (a slot's model loaded) without its shadow model and with the hat left on: in a match the
// game takes the headgear off (0x80091CB4: desc +0x254 & 0x80, skel.c leaves the hat out); JR keeps his
#define HATS_OFF 0x80091CB4u
static void load_model(u32 k, s32 id, s32 costume) {
    static u32 looks[0x68 / 4];
    RD16(arr_at(0x80076264u, k)) = (u16)id;
    RD16(arr_at(0x80076270u, k)) = (u16)id;
    RD8(arr_at(0x8007626Cu, k)) = (u8)costume;
    for (u32 b = 0; b < 28; b++) {
        func_8000C548(BONES(k) + b * 0xDC, PARTS(k) + b * 0x34, 4, 0, 0, 0);
        func_80001684(BONES(k) + b * 0xDC, 0);
    }
    func_8001089C(POOL(k), 900, 0x514);
    RD16(arr_at(0x80070D12u, k)) = (u16)costume;
    func_8001AC44(func_8000A900(id, costume), (u32)looks);
    if (dbg_ann_f9 != 0xFF) RD8((u32)looks + 9) = (u8)dbg_ann_f9;   // (tests: field 9; the costume's own kept)
    dbg_ann_f9_was = RD8((u32)looks + 9);
    RD8(HATS_OFF) = 0;
    RD8(0x800A8970u) = RD32(0x801594D8u) != 0;
    func_800148F0((s32)k, (u32)looks, (s32)(s16)func_8000AA24(id, costume));
    RD8(HATS_OFF) = 1;
    u32 h = arr_at(0x80076CF8u, k);
    for (u32 i = 0; i < 2; i++) if (RD32(h + i * 4)) { func_8000093C(RD32(h + i * 4)); RD32(h + i * 4) = 0; }
    func_80016464(arr_at(0x800A8980u, k), (s32)k, 0);
    func_80016464(arr_at(0x800A8980u, k), (s32)k, 1);
    RD8(arr_at(0x80076260u, k)) = 1;
}

// ---- their chairs ---------------------------------------------------------------------------------
// Two meshes of the mod's own (ann_chair.h, announce/mkchair.py) served as files past the game's
// (files.c), drawn with the table's pictures (its dark top, its grey metal)
#define ANN_FILE_CHAIR 0xFFF0u
u32 announce_file(u32 file, u32 dest) {
    const u8* m; u32 n;
    if (file == ANN_FILE_CHAIR) { m = ann_chair_top; n = sizeof(ann_chair_top); }
    else if (file == ANN_FILE_CHAIR + 1) { m = ann_chair_legs; n = sizeof(ann_chair_legs); }
    else return 0;
    if (dest) for (u32 i = 0; i < n; i++) RD8(dest + i) = m[i];
    return n;
}
static void free_part(u32 part);
static int build_chair(Announcer* a) {
    u32 bones = func_800007D4(2 * 0xDC), parts = func_800007D4(2 * 0x34);
    if (!bones || !parts) { if (bones) func_8000093C(bones); if (parts) func_8000093C(parts); return 0; }
    for (u32 j = 0; j < 2; j++) {
        u32 b = bones + j * 0xDC, p = parts + j * 0x34;
        for (u32 q = 0; q < 0xDC; q += 4) RD32(b + q) = 0;
        func_8000C548(b, p, 0, 0, 0, 0);
        func_8000C58C(b, p, 0, (s32)(ANN_FILE_CHAIR + j), 0, 0, 0);
        func_8000CA18(p, j ? 0x7B4 : 0x7B6, j ? 0x7B3 : 0x7B5);
    }
    a->chair_bones = bones; a->chair_parts = parts;
    return 1;
}
static void free_chair(Announcer* a) {
    if (!a->chair_bones) return;
    for (u32 j = 0; j < 2; j++) {
        u32 b = a->chair_bones + j * 0xDC;
        if (RD32(b + 0x10)) func_8000093C(RD32(b + 0x10));
        free_part(a->chair_parts + j * 0x34);
    }
    func_8000093C(a->chair_bones); func_8000093C(a->chair_parts);
    a->chair_bones = a->chair_parts = 0;
}

static u32 moved(u32 p, u32 from, u32 size, u32 to) { return (p >= from && p < from + size) ? p - from + to : p; }

// the heap's free bytes (func_800007D4's free list: {next, size in 16s}, from *0x800435F0)
static u32 heap_free(void) {
    u32 p0 = RD32(0x800435F0u), p = p0, tot = 0;
    for (int i = 0; p && i < 4096; i++) {
        tot += RD32(p + 4) * 16u;
        p = RD32(p);
        if (p == p0) break;
    }
    return tot;
}
#define ANN_HEAP_NEED 0x60000u      // a model's pieces with room to spare (the match's own come first)

static int build(Announcer* a, s32 costume) {
    u32 k = ANN_SLOT, tot = 0;
    if (heap_free() < ANN_HEAP_NEED) { dbg_ann_fail++; return 0; }
    for (u32 i = 0; i < ANN_SLOT_ARRAYS; i++) tot += ann_slot_arrays[i].size;
    u32 save = func_800007D4(tot);
    u32 bones = func_800007D4(BONES_SZ), parts = func_800007D4(PARTS_SZ);
    if (!save || !bones || !parts) {
        if (save) func_8000093C(save);
        if (bones) func_8000093C(bones);
        if (parts) func_8000093C(parts);
        dbg_ann_fail++;
        return 0;
    }
    u32 o = 0;
    for (u32 i = 0; i < ANN_SLOT_ARRAYS; i++) {
        u32 at = ann_slot_arrays[i].at + k * ann_slot_arrays[i].size;
        copy(save + o, at, ann_slot_arrays[i].size); o += ann_slot_arrays[i].size;
    }
    // what the load frees or reuses: the slot's mesh pool, its two scratch buffers
    RD32(POOL(k)) = 0; RD32(POOL(k) + 8) = 0;
    u32 handles = arr_at(0x80076CF8u, k);
    RD32(handles) = 0; RD32(handles + 4) = 0;
    s16 n0 = RDS16(DRAW_COUNT);
    // the match's records (func_8000A690 finds a Superstar's by ID and costume): the first one, his for now
    static u8 rec_keep[0x114];
    copy((u32)rec_keep, MATCH_RECS, 0x114);
    u16 id_keep = RD16(MATCH_REC_IDS); u8 c_keep = RD8(MATCH_REC_COSTUMES);
    RD16(MATCH_REC_IDS) = (u16)dbg_ann_id; RD8(MATCH_REC_COSTUMES) = (u8)costume;
    copy(MATCH_RECS + 0xFC, (u32)ann_looks[costume], ANN_LOOKS_N);   // (+0xFC..+0x112: within record 0)
    RD8(MATCH_RECS + 1) = ann_scale[costume];

    load_model(k, (s32)dbg_ann_id, costume);

    copy(MATCH_RECS, (u32)rec_keep, 0x114);
    RD16(MATCH_REC_IDS) = id_keep; RD8(MATCH_REC_COSTUMES) = c_keep;

    // the bones and parts, moved to the announcer's own
    u32 B = BONES(k), P = PARTS(k);
    copy(bones, B, BONES_SZ);
    copy(parts, P, PARTS_SZ);
    for (u32 b = 0; b < 28; b++) {
        u32 bone = bones + b * 0xDC;
        for (u32 w = 0; w < 3; w++) RD32(bone + w * 4) = moved(RD32(bone + w * 4), B, BONES_SZ, bones);
        for (u32 d = RD32(bone + 0x14), n = 0; d && n < 8; d = RD32(d + 8), n++)
            RD32(d + 4) = moved(RD32(d + 4), B, BONES_SZ, bones);
        a->hidden[b] = (b >= 25 && RD32(bone) && RD32(bone + 0xC)) ? 0 : (RD8(bone + 0x18) & 0x80);   // (as the load left them; a hat shown)
        a->r0[b] = RD32(bone + 0x14) ? F32(RD32(bone + 0x14) + 0x24) : 0.0f;
    }
    // the parts' joining pieces (the chin to the neck, the chest to the waist...: +0x28 / +0x2C, drawn on
    // the bone's own) point at the slot's bones as well
    for (u32 b = 0; b < 28; b++)
        for (u32 off = 0x28; off <= 0x2C; off += 4)
            for (u32 d = RD32(parts + b * 0x34 + off), n = 0; GAME_PTR(d) && n < 8; d = RD32(d + 8), n++)
                RD32(d + 4) = moved(RD32(d + 4), B, BONES_SZ, bones);
    // the neck piece (4): drawn over the chin as a white patch on these two (the head and chest meet
    // without it)
    a->hidden[4] = 0x80; RD8(bones + 4 * 0xDC + 0x18) = 0x80;
    for (u32 i = 0; i < 4; i++) a->pool[i] = RD32(POOL(k) + i * 4);
    for (u32 i = 0; i < 2; i++) a->pics[i] = RD32(parts + 20u * 0x34u + 0x20u + i * 4u);   // (the head's picture, palette)
    u32 st = arr_at(0x80069F20u, k);
    for (u32 i = 0; i < 16; i++) a->base[i] = F32(st + 0x18 + i * 4);
    a->cls = RD8(arr_at(0x8006A268u, k));
    // the frame's draw list: the load's entries kept only for the announcer's bones (the shadow's out)
    s16 n = RDS16(DRAW_COUNT), w = n0;
    for (s16 i = n0; i < n; i++) {
        u32 d = RD32(DRAW_LIST + (u32)i * 4), bone = RD32(d + 4);
        if (bone >= bones && bone < bones + BONES_SZ) RD32(DRAW_LIST + (u32)(w++) * 4) = d;
    }
    RD16(DRAW_COUNT) = (u16)w;
    // the slot as it was
    o = 0;
    for (u32 i = 0; i < ANN_SLOT_ARRAYS; i++) {
        u32 at = ann_slot_arrays[i].at + k * ann_slot_arrays[i].size;
        copy(at, save + o, ann_slot_arrays[i].size); o += ann_slot_arrays[i].size;
    }
    func_8000093C(save);
    a->bones = bones; a->parts = parts;
    return 1;
}

// The table's set-up: the announcers made anew with the scene (the last scene's freed)
// ---- the table, moved up toward the ring (ANN_TABLE_UP) so they sit at it -------------------------------
// Its pieces' places are the game's data (0x800500B8: x y z per piece), read when the table is made; what
// a Superstar bumps into is the match overlay's collision quads (s16 corners {x, y, z} x 4, then the
// plane): the table's walls (x = +-113, z -690..-530, up to y 100, and above its top to 1000), its top
// (y 90) and its four bounding planes (x = 113, x = -113, z = -530, z = -690, one after the other).
// Those that are the table's (no other quad has x = +-113 on all four corners) move with it.
#define ANN_TABLE_UP 75          // (20 left no room for them and their chairs before the barricade, 2026-10-27; they sit 25 from
                                //  its back edge: closer, it cuts across their upper arms -- no depth test)
static __attribute__((unused)) void table_data_up(void) {
    static const u32 at[4] = { 0x800500C0u, 0x800500CCu, 0x800500D8u, 0x800500E4u };
    static const float was[4] = { -605.0f, -545.0f, -605.0f, -605.0f };
    for (int i = 0; i < 4; i++) if (F32(at[i]) == was[i]) F32(at[i]) = was[i] + (float)ANN_TABLE_UP;
}
int ann_old_back_plane;        // QA (qa_table 2): its back plane left where it was (the climb the user found)
u32 dbg_ann_walls, dbg_ann_top_at;   // tests: (and the table's top's record, once found)             // tests: the table's collision quads moved (10)
static int wall_quad(u32 p) {
    for (u32 q = 0; q < 4; q++) {
        s16 x = RDS16(p + q * 6), y = RDS16(p + q * 6 + 2), z = RDS16(p + q * 6 + 4);
        if ((x != 113 && x != -113) || y < 0 || y > 1000 || (z != -530 && z != -690)) return 0;
    }
    return 1;
}
static int plane_quad(u32 p, s16 xs, s16 zs) {      // xs: x on all four (zs = +-1000), or zs: z on all four
    for (u32 q = 0; q < 4; q++) {
        s16 x = RDS16(p + q * 6), z = RDS16(p + q * 6 + 4);
        if (xs ? (x != xs || (z != 1000 && z != -1000)) : ((x != 1000 && x != -1000) || z != zs)) return 0;
    }
    return 1;
}
static __attribute__((unused)) void table_walls_up(void) {
    static const s16 sig[12] = { 113, 100, -690, 113, 100, -530, 113, 0, -530, 113, 0, -690 };
    u32 at = 0;
    for (u32 a = 0x80140000u; a < 0x80150000u && !at; a += 2) {
        if (RDS16(a) != 113 || RDS16(a + 2) != 100) continue;
        int k = 2;
        for (; k < 12; k++) if (RDS16(a + (u32)k * 2) != sig[k]) break;
        if (k == 12) at = a;
    }
    if (!at) return;                                  // (moved already, or not this overlay)
    u32 n = 0;
    for (u32 p = at - 0x100; p < at + 0xC00; p += 2) {
        // only its front moves: its back stays on the barricade's line (z -690), so the table is one solid
        // block from the barricade to its new front -- no gap behind it for a Superstar to walk into, where
        // the announcers sit
        // ... and up on it (the walls from its top up, y 90..1000), a Superstar stays clear of the
        // announcers' side: its back wall up there just in front of them (the table's back edge + 12)
        // ... and its top (the floor up there, y 90 at all four corners) ends at that wall too: left reaching back to
        // -690, a run at the table's side by JR / King (z -690 .. -600) landed him on its top behind the wall, where
        // the walls over the top at the sides (-600 .. -455) don't reach -- he walked off at the table's height and
        // through everything, the crowd, the ring (the user, 2026-10-30; QA table_climb: record 82, the top)
        if (wall_quad(p)) {
            int high = 0, top = 1;
            for (u32 q = 0; q < 4; q++) { s16 y = RDS16(p + q * 6 + 2); if (y == 1000) high = 1; if (y != 90) top = 0; }
            if (top) dbg_ann_top_at = p;
            if (ann_old_back_plane) top = 0;
            for (u32 q = 0; q < 4; q++) {
                s16 z = RDS16(p + q * 6 + 4);
                if (z == -530) RDS16(p + q * 6 + 4) = -530 + ANN_TABLE_UP;
                else if (z == -690 && (high || top)) RDS16(p + q * 6 + 4) = -690 + ANN_TABLE_UP + 15;
            }
            n++; p += 22; continue;
        }
        if (plane_quad(p, 113, 0)) {
            if (plane_quad(p + 0x60, 0, -530)) { for (u32 q = 0; q < 4; q++) RDS16(p + 0x60 + q * 6 + 4) = -530 + ANN_TABLE_UP; n++; }
            // ... and its back plane on the line of the wall over its top (the announcers' side of it not the table's):
            // left at -690, a run at the table's side between the two (by JR / King) took him up onto its top
            // behind that wall, where no wall over the top stands at the sides -- up at the table's height he walked
            // off it and through everything, the crowd, the ring (the user, 2026-10-30; QA table_climb)
            if (!ann_old_back_plane && plane_quad(p + 0x90, 0, -690)) { for (u32 q = 0; q < 4; q++) RDS16(p + 0x90 + q * 6 + 4) = -690 + ANN_TABLE_UP + 15; n++; }
        }
    }
    dbg_ann_walls = n;
}
// Up on the table, kept on it (every frame of a match with the table standing). The game's climb puts a Superstar on
// the table's top where he climbs: from its side by JR and King (z -690 .. -600) that was behind the wall over the top
// in front of them, from its side at the front just past the walls at its sides (x 126 against 113) -- outside the
// walls that keep a man up there on the table, and up at its height he walked off it and through everything, the
// crowd, the ring (the user, 2026-10-30: "players should be able to climb on from the sides, just not onto JR/King or
// where it's glitching"). Climbing up, standing, walking or running up there (states 111, 1, 2, 3, 27: not a move -- a dive off it goes
// on its way) he's kept inside those walls: x within +-105, z -592 .. -463.
#define TOP_X   105.0f
#define TOP_Z0  (-690.0f + (float)ANN_TABLE_UP + 15.0f + 8.0f)
#define TOP_Z1  (-530.0f + (float)ANN_TABLE_UP - 8.0f)
// (2026-11-14) A byte: it is only ever 0 or 1, and the mod's compiler kept it as one byte though the C said int -- so the
//  C says what memory holds, and a reader of the C (or the native build) stores the byte the game stores. Same values.
static u8 walls_done;
static int fbad(float f);
u32 dbg_ann_kept;               // tests: times a Superstar up there was put back inside
static void wr_put(u32 w, float x, float z) {
    F32(w + 0xF4) = x; F32(w + 0xFC) = z; F32(w + 0x118) = x; F32(w + 0x120) = z;
    for (u32 a = 0x124; a <= 0x154; a += 12) { F32(w + a) = x; F32(w + a + 8) = z; }
}
void announce_table_guard(void) {
    if (ann_no_table || ann_old_back_plane || !walls_done || RD8(TABLE_STATE) != 0 || RD16(NM_MATCH_FRAMES) == 0xFFFFu) return;
    if (RDS32(NM_MODE) != -2 && RDS32(NM_MODE) != -6) return;
    for (u32 sl = 0; sl < (u32)nm_slot_span(); sl++) {
        u32 w = NM_WRESTLERS + sl * NM_WRESTLER_SIZE;
        if (RDS16(w - 8 + 0xA0) == -1) continue;
        float x = F32(w + 0xF4), y = F32(w + 0xF8), z = F32(w + 0xFC);
        if (fbad(x) || fbad(y) || fbad(z)) continue;
        u32 st = RD32(w + 0x14) & 0xFFFFu;
        if (st != 1 && st != 2 && st != 3 && st != 27 && st != 111) continue;   // (111: the climb up itself -- he lands in front of them)
        if (y < 80.0f || y > 100.0f || x > TOP_X + 80.0f || x < -TOP_X - 80.0f || z < -720.0f || z > -400.0f) continue;
        float nx = x > TOP_X ? TOP_X : x < -TOP_X ? -TOP_X : x;
        float nz = z < TOP_Z0 ? TOP_Z0 : z > TOP_Z1 ? TOP_Z1 : z;
        if (nx != x || nz != z) { wr_put(w, nx, nz); dbg_ann_kept++; }
    }
}
// before the table is made (arcade_frame, every frame: the data is the game's, the walls the overlay's)
void announce_table_move(void) {
    { extern int arena_has_table(void); if (!arena_has_table()) { walls_done = 0; return; } }   // (an arena without the table:
                                                    //  its own walls, nothing of the table's to move -- arena.c)
    // (the table's places: each arena's own table data, the 75 built in -- gen/arenas.py write_tables, arena.c arena_table;
    //  until 2026-11-01 table_data_up wrote them into the game's 0x800500C0..)
    if (RDS32(NM_MODE) != -2 && RDS32(NM_MODE) != -6) { walls_done = 0; return; }
    if (!walls_done) {          // (the table's walls: the arena's own, the 75 built in -- gen/arenas_walls.py; until 2026-11-01
        extern u32 arena_rec_top;  //  table_walls_up moved the overlay's records each match)
        dbg_ann_top_at = arena_rec_top; walls_done = 1;
    }
}

void announce_scene(void) {
    teardown(&ann[0]); teardown(&ann[1]);
    ann_built = 0; ann_broken = 0; ann_frames = 0;
    ann_stand[0] = ann_stand[1] = 0.0f; ann_dodge[0] = ann_dodge[1] = 0.0f; ann_dodge_t[0] = ann_dodge_t[1] = 0;
    for (int i = 0; i < 2; i++) { ann_flinch[i] = 0; ann_flinch_w[i] = ann_talk_w[i] = ann_look[i] = 0.0f; ann_talk_t[i] = -(s32)(60 + rnd(200)); ann_cool[i] = 0; }
    ann_no_table = (RD32(0x800A9950u) & 0x100000u) || (RD32(NM_MATCH_FLAGS) & 0x10000u);
    { extern int arena_has_table(void); if (!arena_has_table()) return; }   // (an arena without the announce table: no JR
                                                                             //  and King -- arena.c, the arena's own)
    if (!ann_on) return;
    if (!build(&ann[0], 0)) return;
    if (!build(&ann[1], 1)) { teardown(&ann[0]); return; }
    build_chair(&ann[0]); build_chair(&ann[1]);
    ann_built = 1;
    dbg_ann_builds++;
}

// ---- poses ----------------------------------------------------------------------------------------
// Not a number, or infinite: told by its bits. (The PC port's recompiled float compares are ordered ones:
// with a NaN, `a > b` is true where the N64's is false, and `x != x` is never true -- a loop on a NaN never
// ended: the Royal Rumble freezes and crashes, 0.35.8. Every float that comes in from outside is checked.)
static int fbad(float f) { union { float f; u32 u; } v; v.f = f; return (v.u & 0x7F800000u) == 0x7F800000u; }
static float wrapf(float a) {
    if (fbad(a) || a > 1.0e6f || a < -1.0e6f) return 0.0f;
    for (int n = 0; a > 180.0f && n < 8000; n++) a -= 360.0f;
    for (int n = 0; a < -180.0f && n < 8000; n++) a += 360.0f;
    return a;
}
static float sin_d(float deg) {           // sine of degrees (range-reduced, odd polynomial)
    float x = wrapf(deg);
    if (x > 90.0f) x = 180.0f - x; else if (x < -90.0f) x = -180.0f - x;
    x *= 0.017453292f;
    float x2 = x * x;
    return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f * (1.0f - x2 / 72.0f))));
}
static float cos_d(float deg) { return sin_d(deg + 90.0f); }
static void mtx_rot(float* m, float rx, float ry, float rz) {     // func_80010E50
    float s1 = sin_d(rx), c2 = cos_d(rx), s3 = sin_d(ry), c4 = cos_d(ry), s5 = sin_d(rz), c6 = cos_d(rz);
    m[0] = c6 * c4; m[1] = s5 * c2 + c6 * s3 * s1; m[2] = s5 * s1 - c6 * s3 * c2; m[3] = 0;
    m[4] = -s5 * c4; m[5] = c6 * c2 - s5 * s3 * s1; m[6] = c6 * s1 + s5 * s3 * c2; m[7] = 0;
    m[8] = s3; m[9] = -c4 * s1; m[10] = c4 * c2; m[11] = 0;
    m[12] = m[13] = m[14] = 0; m[15] = 1;
}
static void mtx_mul(const float* a, const float* b, float* out) {  // out = a x b (row vectors)
    float t[16];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
        float s = 0; for (int q = 0; q < 4; q++) s += a[i * 4 + q] * b[q * 4 + j];
        t[i * 4 + j] = s;
    }
    for (int i = 0; i < 16; i++) out[i] = t[i];
}
static void mtx_id(float* m) { for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.0f : 0.0f; }

static AnnPose cur;   // the pose being drawn
static void blend(const AnnPose* a, const AnnPose* b, float t, AnnPose* out) {
    if (fbad(t)) t = 0.0f;
    for (int k = 0; k < 20; k++) for (int c = 0; c < 3; c++) {
        out->r[k][c] = a->r[k][c] + wrapf(b->r[k][c] - a->r[k][c]) * t;
        out->p[k][c] = a->p[k][c] + (b->p[k][c] - a->p[k][c]) * t;
    }
    out->lift = a->lift + (b->lift - a->lift) * t;
}

// the bones' local matrices for a pose (func_80012BE0's way), the root at x, y, z turned yaw
static void pose_bones(Announcer* a, const AnnPose* p, float x, float y, float z, float yaw) {
    static float M[20][16];
    for (u32 i = 1; i < 20; i++) {
        s32 b = RDS8(0x80047EF8u + i), par = RDS8(0x80047F0Cu + b);
        float ox = (float)RDS8(0x80047ED0u + b * 2), oy = (float)RDS8(0x80047ED1u + b * 2);
        float* m = M[b];
        mtx_rot(m, p->r[b][0], p->r[b][1], p->r[b][2]);
        if (par) {
            float dx = ox - (float)RDS8(0x80047ED0u + par * 2), dy = oy - (float)RDS8(0x80047ED1u + par * 2);
            float* q = M[par];
            m[12] = q[0] * dx + q[4] * dy + q[12];
            m[13] = q[1] * dx + q[5] * dy + q[13];
            m[14] = q[2] * dx + q[6] * dy + q[14];
        } else { m[12] = p->p[b][0]; m[13] = p->p[b][1]; m[14] = p->p[b][2]; }
        float t[16];
        s32 f = RDS8(0x80047F40u + b);
        if (f == -1) { mtx_id(t); t[12] = -ox; t[13] = -oy; }
        else {
            u32 c = (u32)a->cls * 4u;
            mtx_id(t);
            t[0] = F32(0x80047F54u + c); t[5] = F32(0x80047F68u + c); t[10] = F32(0x80047F7Cu + c);
            t[12] = F32(RD32(0x80047FE0u + (u32)f * 4) + c) - ox;
            t[13] = F32(0x80047FB8u + c) - oy;
            t[14] = F32(0x80047FCCu + c);
        }
        mtx_mul(t, m, (float*)(a->bones + (u32)b * 0xDC + 0x1C));
    }
    float r[16];
    float s = sin_d(yaw), c = cos_d(yaw);                       // func_80010FD4
    mtx_id(r); r[0] = c; r[2] = -s; r[8] = s; r[10] = c;
    r[12] = x; r[13] = y + p->lift * a->base[5]; r[14] = z;
    mtx_mul(a->base, r, (float*)(a->bones + 0x1C));
    // (their world matrices: pose_kept, once the pose is known to be numbers)
}

// A pose that came out not a number (seen in the Royal Rumble, 2026-10-27: every bone of both of them, every frame
// from then on -- the scene's safety net, its list of models full, put them at the world's origin: JR and King one
// on the other in the middle of the ring, frozen, the game slowing down): their last good one instead, so they
// stay where they are. The first time, what went into it is kept at ANN_DIAG for a glitch report's RAM ('ADIA').
#define ANN_DIAG 0x807F3240u
static int ann_diag_done;
static int mtx_badf(const float* m) { for (int i = 0; i < 16; i++) if (fbad(m[i])) return 1; return 0; }
static void diag_f(u32 k, float v) { union { float f; u32 u; } x; x.f = v; RD32(ANN_DIAG + 4u * k) = x.u; }
int ann_qa_nan;   // QA (qa_ann_nan): King's pose made not a number from 5 s into a match on (the fallback tested)
static void pose_kept(Announcer* a, int i) {
    if (ann_qa_nan && i == 1 && RD16(NM_MATCH_FRAMES) != 0xFFFFu && RD16(NM_MATCH_FRAMES) > 150) {
        union { float f; u32 u; } q; q.u = 0x7FC00000u;
        F32(a->bones + 0x1C) = q.f; F32(a->bones + 0xDC + 0x1C) = q.f;
    }
    int bad = 0;
    for (u32 b = 0; b < 20 && !bad; b++) bad = mtx_badf((float*)(a->bones + b * 0xDC + 0x1C));
    if (!bad) {
        for (u32 b = 0; b < 20; b++) for (u32 k = 0; k < 16; k++) a->good[b][k] = F32(a->bones + b * 0xDC + 0x1C + 4 * k);
        a->have_good = 1;
        func_80002268(a->bones);
        return;
    }
    if (!ann_diag_done) {
        ann_diag_done = 1;
        RD32(ANN_DIAG) = 0x41444941u;   // 'ADIA'
        RD32(ANN_DIAG + 4) = RD32(NM_VI_COUNT); RD32(ANN_DIAG + 8) = (u32)i;
        diag_f(3, dbg_ann_xyz[3]); diag_f(4, sin_d(dbg_ann_xyz[3])); diag_f(5, cos_d(dbg_ann_xyz[3]));
        diag_f(6, sin_d(30.0f)); diag_f(7, 30.0f * 0.017453292f);
        diag_f(8, ann_stand[i]); diag_f(9, ann_look[i]); diag_f(10, ann_talk_w[i]); diag_f(11, ann_flinch_w[i]);
        diag_f(12, cur.r[1][0]); diag_f(13, cur.p[0][1]); diag_f(14, a->base[0]); diag_f(15, a->base[5]);
        diag_f(16, F32(a->bones + 0x1C)); diag_f(17, F32(a->bones + 0xDC + 0x1C));
        for (u32 k = 0; k < 6; k++) diag_f(18 + k, F32(0x80047F54u + 4u * k));
        // (where the arcade's own data is while it runs -- not the link map's address -- to read it in the RAM dump)
        RD32(ANN_DIAG + 24u * 4u) = (u32)a; RD32(ANN_DIAG + 25u * 4u) = (u32)&cur; RD32(ANN_DIAG + 26u * 4u) = (u32)dbg_ann_xyz;
        RD32(ANN_DIAG + 27u * 4u) = a->cls; RD32(ANN_DIAG + 28u * 4u) = (u32)ann_stand;
    }
    RD32(ANN_DIAG + 0x80) = RD32(ANN_DIAG + 0x80) + 1;   // (how many times since; past the diagnosis's 29 words)
    if (a->have_good)
        for (u32 b = 0; b < 20; b++) for (u32 k = 0; k < 16; k++) F32(a->bones + b * 0xDC + 0x1C + 4 * k) = a->good[b][k];
    else    // (none yet: hidden, not drawn wrong, till there is one)
        for (u32 b = 0; b < 28; b++) RD8(a->bones + b * 0xDC + 0x18) = 0x80;
    if (a->have_good) func_80002268(a->bones);
}

// ---- every frame (after the table's) ----------------------------------------------------------------
// What they do: sit (a hand up now and then as they talk), heads turned to the action; a Superstar
// close by (on the floor at the table) or the table going: a flinch; the table broken (or none, in a
// cage): on their feet, the same. Drawn as the table is (its group, sorted by depth): from the ring's
// side their legs under the table are sorted behind it (the table drawn over them) and their forearms
// before it (on it); from behind them, as they are. Right in front of the camera (it behind their
// backs, close): not drawn.
static float atan2_d(float y, float x) {     // degrees
    if (fbad(x) || fbad(y)) return 0.0f;
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax < 1e-6f && ay < 1e-6f) return 0.0f;
    float t = ax > ay ? ay / ax : ax / ay, t2 = t * t;
    float r = t * (57.2958f - t2 * (18.65f - t2 * 5.55f));   // atan(t) degrees, |t| <= 1
    if (ay > ax) r = 90.0f - r;
    if (x < 0) r = 180.0f - r;
    return y < 0 ? -r : r;
}
static void ramp(float* v, float to, float step) {
    if (fbad(*v)) *v = 0.0f;
    if (fbad(to)) return;
    if (*v < to) { *v += step; if (*v > to) *v = to; } else if (*v > to) { *v -= step; if (*v < to) *v = to; }
}
static AnnPose tmp1, tmp2;
u8 dbg_ann_ngrp;               // tests: force their group from behind
u8 dbg_ann_grp[2];             // tests: the group they're drawn in from behind (0: as props), the table's << 4
// (a bone's place is its joint: the hips' for most of the body, so it says little about where its piece is)
static int leg_bone(u32 b) { return b == 6 || b == 7 || b == 8 || b == 13 || b == 14 || b == 15; }
static int fore_bone(u32 b) { return b == 9 || b == 10 || b == 11 || b == 16 || b == 17 || b == 18; }

void announce_table_frame(void) {
    if (!ann_built) return;
    ann_frames++;
    dbg_ann_frames++;
    u8 st = RD8(TABLE_STATE);
    int was_broken = ann_broken;
    if (!ann_no_table && st == 1) ann_broken = 1;                  // (broken: it goes through 1, its pieces flying)
    int hide = !ann_no_table && st >= 2 && !ann_broken;           // (the entrances: no table, no them)
    int standing = ann_no_table || ann_broken || ann_qa_stand;
    // the action: where the Superstars are (on screen: x mirrored), who's on the floor by the table
    float tx = 0, tz = 0; int n = 0;
    float wx[NM_SLOTS_CAP], wz[NM_SLOTS_CAP], wy[NM_SLOTS_CAP]; int wn = 0;
    if (RD16(NM_MATCH_FRAMES) != 0xFFFFu)
        for (u32 sl = 0; sl < (u32)nm_slot_span(); sl++) {
            u32 w = NM_WRESTLERS + sl * NM_WRESTLER_SIZE;
            if (RDS16(w - 8 + 0xA0) == -1) continue;
            float x = -F32(w + 0xF4), y = F32(w + 0xF8), z = F32(w + 0xFC);
            if (fbad(x) || fbad(y) || fbad(z) || x > 3000.0f || x < -3000.0f || z > 3000.0f || z < -3000.0f) continue;
            wx[wn] = x; wy[wn] = y; wz[wn] = z; wn++;
            tx += x; tz += z; n++;
        }
    if (n) { tx /= (float)n; tz /= (float)n; } else { tx = 0; tz = 0; }
    // the camera's eye (where its matrix -- the game's, or the free camera's -- sends x, y and w to 0)
    // and the table's pieces' places in the draw order
    float eye[3];
    {
        u32 m = NM_CAMERA_MATRIX;
        float A[3][3], c[3];
        const int col[3] = { 0, 1, 3 };
        for (int r = 0; r < 3; r++) { for (int q = 0; q < 3; q++) A[r][q] = F32(m + (u32)(q * 16 + col[r] * 4)); c[r] = -F32(m + (u32)(48 + col[r] * 4)); }
        float det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
        if (fbad(det) || (det > -1e-12f && det < 1e-12f)) { eye[0] = 0; eye[1] = 300; eye[2] = -600; }
        else for (int q = 0; q < 3; q++) {
            float M2[3][3];
            for (int r = 0; r < 3; r++) for (int t = 0; t < 3; t++) M2[r][t] = t == q ? c[r] : A[r][t];
            eye[q] = (M2[0][0] * (M2[1][1] * M2[2][2] - M2[1][2] * M2[2][1]) - M2[0][1] * (M2[1][0] * M2[2][2] - M2[1][2] * M2[2][0]) + M2[0][2] * (M2[1][0] * M2[2][1] - M2[1][1] * M2[2][0])) / det;
        }
        if (fbad(eye[0]) || fbad(eye[1]) || fbad(eye[2])) { eye[0] = 0; eye[1] = 300; eye[2] = -600; }
    }
    float tkmax = 0.0f, tkmin = 0.0f;
    for (u32 k = 0; k < 4; k++) {
        u32 d = RD32(TABLE + k * 0xDC + 0x14);
        if (!d) continue;
        float key = F32(d + 0x28);
        if (fbad(key)) continue;
        if (k == 0 || key > tkmax) tkmax = key;
        if (k == 0 || key < tkmin) tkmin = key;
    }
    dbg_ann_eye[0] = eye[0]; dbg_ann_eye[1] = eye[1]; dbg_ann_eye[2] = eye[2];
    for (int i = 0; i < 2; i++) {
        Announcer* a = &ann[i];
        float ax = dbg_ann_xyz[i], az = dbg_ann_xyz[2];
        // on their feet
        if (standing) ramp(&ann_stand[i], 1.0f, 0.06f);
        // a flinch: someone on the floor close by, the table going
        int near = 0;
        for (int k = 0; k < wn; k++) {
            float dx = wx[k] - ax, dz = wz[k] - az;
            if (wy[k] < 60.0f && dx < 95.0f && dx > -95.0f && dz < 200.0f && dz > -90.0f) near = 1;   // (at the table, by him)
        }
        if (ann_cool[i] > 0) ann_cool[i]--;
        if (((near && ann_cool[i] == 0) || (ann_broken && !was_broken)) && ann_flinch[i] == 0) { ann_flinch[i] = 1; ann_cool[i] = 70; }
        if (ann_flinch[i]) { ann_flinch[i]++; if (ann_flinch[i] > 34) ann_flinch[i] = 0; }
        ramp(&ann_flinch_w[i], ann_flinch[i] && ann_flinch[i] < 24 ? 1.0f : 0.0f, ann_flinch[i] ? 0.2f : 0.08f);
        // talking: a hand up now and then; while one of them has a line on (the commentary), him
        int spk = comm_speaker >= 2 ? comm_speaker - 2 : -1;
        if (spk == i && ann_talk_t[i] < 12) { if (ann_talk_t[i] <= 0) ann_talk_side[i] = (s32)rnd(2); ann_talk_t[i] = 12; }
        if (spk >= 0 && spk != i && ann_talk_t[i] > 12) ann_talk_t[i] = 12;
        if (ann_talk_t[i] > 0) ann_talk_t[i]--;
        else if (ann_talk_t[i] == 0) ann_talk_t[i] = -(s32)(150 + rnd(300));     // quiet for a while
        if (ann_talk_t[i] < 0 && ++ann_talk_t[i] == 0) { ann_talk_t[i] = (s32)(60 + rnd(70)); ann_talk_side[i] = (s32)rnd(2); }
        ramp(&ann_talk_w[i], ann_talk_t[i] > 8 ? 1.0f : 0.0f, 0.07f);
        // looking at the action (or, talking, at the other one now and then)
        float look = atan2_d(tx - ax, tz - az);
        if (!n) look = 0;
        if (ann_talk_t[i] > 0 && ann_talk_side[i]) look = i ? -55.0f : 55.0f;
        if (look > 55.0f) look = 55.0f; else if (look < -55.0f) look = -55.0f;
        ramp(&ann_look[i], look, 3.0f);
        // the pose
        float sw = ann_stand[i];
        blend(&ANN_POSE_SIT, &ANN_POSE_STAND, sw, &cur);
        if (ann_talk_w[i] > 0.0f) {
            blend(ann_talk_side[i] ? &ANN_POSE_SIT_TALK2 : &ANN_POSE_SIT_TALK, ann_talk_side[i] ? &ANN_POSE_STAND_TALK2 : &ANN_POSE_STAND_TALK, sw, &tmp1);
            blend(&cur, &tmp1, ann_talk_w[i], &cur);
        }
        float lw = ann_look[i] < 0 ? -ann_look[i] / 55.0f : ann_look[i] / 55.0f;
        blend(ann_look[i] < 0 ? &ANN_POSE_SIT_LOOK_L : &ANN_POSE_SIT_LOOK_R, ann_look[i] < 0 ? &ANN_POSE_STAND_LOOK_L : &ANN_POSE_STAND_LOOK_R, sw, &tmp1);
        blend(&ANN_POSE_SIT, &ANN_POSE_STAND, sw, &tmp2);
        blend(&tmp2, &tmp1, lw, &tmp2);
        for (int b = 4; b <= 5; b++) for (int c = 0; c < 3; c++) cur.r[b][c] = tmp2.r[b][c];
        if (ann_flinch_w[i] > 0.0f) {
            blend(&ANN_POSE_SIT_FLINCH, &ANN_POSE_STAND_FLINCH, sw, &tmp1);
            blend(&cur, &tmp1, ann_flinch_w[i], &cur);
        }
        pose_bones(a, &cur, ax, 0.0f, az + 26.0f * sw, dbg_ann_xyz[3]);   // (up: a step in front of the chair)
        pose_kept(a, i);
        // drawn: as the table (group 0, the camera's distance sorting it, no depth test between them):
        // each piece of them drawn before the table or after it, by where the camera is
        u8 flag = 0;
        u32 hb = a->bones + 5 * 0xDC;
        float hx = F32(hb + 0x8C) - eye[0], hy = F32(hb + 0x90) - eye[1], hz = F32(hb + 0x94) - eye[2];
        if (hide) flag = 0x80;
        else if (hx * hx + hy * hy + hz * hz < 45.0f * 45.0f) flag = 0x80;     // (the camera inside his head)
        if (dbg_ann_flag != 0xFF) flag = (u8)dbg_ann_flag;
        dbg_ann_draw[i] = flag;
        int table_in = !ann_no_table && st == 0 && tkmax > 0.0f && ann_qa_stand != 2;   // (QA 2: as with it broken)
        int seated = table_in && sw < 0.5f;
        u8 tgroup = (u8)(RD8(TABLE + 0x18) & 0x7F);
        float ey = eye[1], ez = eye[2];
        // the camera behind them (their backs to it): they're the nearest of everything there -- drawn after the
        // props (the ring, the table) and with the Superstars (group 4: they're drawn after all the props, so a
        // prop's place in the order can't put him in front of one); with the table in the see-through group
        // (5, drawn after the Superstars), in that one, after it
        int cam_behind = ez < ANN_TABLE_BACK;
        u8 near_group = (table_in && tgroup == 5) ? 5 : 4;
        if (dbg_ann_ngrp) near_group = dbg_ann_ngrp;
        // no table (broken; a cage): on their feet by the barricade, as a Superstar outside the ring there is drawn --
        // the group of the ring's side they're by (0x80084620: in front of the ring, 5, if that side faces the
        // camera; else among it, sorted, 0), the Superstars there in the same one, sorted with them. (In group 4,
        // a man by them was drawn over them from behind the barricade -- he was in 5 -- 2026-10-28.)
        u8 free_group = (u8)RD32(0x80084620u + 4u * (u32)((ax + az < 0 ? 2 : 0) | (ax - az < 0 ? 1 : 0)));
        ann_free = !table_in;
        dbg_ann_grp[i] = (u8)(cam_behind ? near_group : 0) | (u8)(tgroup << 4);
        // Each piece before the table or after it -- moved there all together, his pieces on one side by one amount:
        // their own order (by the camera's distance: no depth test between them) kept. (Each moved to the very same
        // place -- the table's +/- 2048 -- their order among themselves was chance: his arms, chest and head through
        // each other, flickering, 2026-10-27.)
        u8 bef[28]; float k0s[28];
        float kmin_b = 1e30f, kmax_a = -1e30f;
        for (u32 b = 0; b < 28; b++) {
            u32 bone = a->bones + b * 0xDC, d = RD32(bone + 0x14);
            bef[b] = 2;
            if (!d || !table_in) continue;
            int before;    // drawn before the table (it over him where they cross)
            if (cam_behind) before = 0;                                                     // in front of it
            else if (!seated) before = 1;                                                   // on his feet behind it
            else if (fore_bone(b)) before = !(ey > 92.0f);                                  // on it
            else if (leg_bone(b)) before = 1;                                               // under it
            else before = 1;                                                                // behind it
            float k0 = F32(d + 0x28) - F32(d + 0x24) * 1024.0f;   // his place in the order without his bias
            if (fbad(k0)) k0 = 0.0f;
            k0 += a->r0[b] * 1024.0f;                              // (with his model's own bias: his parts' order)
            bef[b] = (u8)before; k0s[b] = k0;
            if (before && k0 < kmin_b) kmin_b = k0;
            if (!before && k0 > kmax_a) kmax_a = k0;
        }
        float shift_b = kmin_b < tkmax + 2048.0f ? (tkmax + 2048.0f - kmin_b) / 1024.0f : 0.0f;   // (all of them past the table)
        float shift_a = kmax_a > tkmin - 2048.0f ? (tkmin - 2048.0f - kmax_a) / 1024.0f : 0.0f;
        float far_k = tkmax + 2048.0f, near_k = tkmin - 2048.0f;   // (his furthest and nearest, for his chair)
        for (u32 b = 0; b < 28; b++) {
            u32 bone = a->bones + b * 0xDC, d = RD32(bone + 0x14);
            if (!d) continue;
            if (!a->hidden[b]) RD8(bone + 0x18) = flag;
            float r = a->r0[b];
            if (bef[b] != 2) {
                r = a->r0[b] + (bef[b] ? shift_b : shift_a);
                if (r > 900.0f) r = 900.0f; else if (r < -900.0f) r = -900.0f;
                float k = k0s[b] - a->r0[b] * 1024.0f + r * 1024.0f;
                if (k > far_k) far_k = k;
                if (k < near_k) near_k = k;
            }
            if (flag == 0 && !a->hidden[b]) {
                if (!table_in) RD8(bone + 0x18) = free_group;
                else if (cam_behind) RD8(bone + 0x18) = near_group;
            }
            F32(d + 0x24) = r;
            ann_zwrap(i, b, d);
        }
        // his chair: where he sits (none without a table: a cage)
        if (a->chair_bones) {
            float cs = cos_d(dbg_ann_xyz[3]), sn = sin_d(dbg_ann_xyz[3]);
            u8 cflag = ann_no_table ? 0x80 : flag;
            for (u32 j = 0; j < 2; j++) {
                u32 b = a->chair_bones + j * 0xDC, d = RD32(b + 0x14);
                float* m = (float*)(b + 0x1C);
                mtx_id(m); m[0] = cs; m[2] = -sn; m[8] = sn; m[10] = cs; m[12] = ax; m[14] = az;
                if (mtx_badf(m)) { mtx_id(m); m[12] = ax; m[14] = az; }   // (a turn not a number: square to the ring)
                func_80002268(b);
                if (!d) continue;
                RD8(b + 0x18) = cflag;
                float r = 0.0f;
                if (!ann_no_table && st == 0 && tkmax > 0.0f) {
                    // behind it, as they are -- and a step past him: from the front the chair is behind him, drawn
                    // before his furthest piece; from behind, between him and the camera, after his nearest. (With the
                    // same place in the order as his -- the table's +/- 2048 -- which of them came last was chance: the
                    // chair's back over half of JR's and King's chests, seen from the front, 2026-10-27.)
                    int before = !cam_behind;
                    float k0 = F32(d + 0x28) - F32(d + 0x24) * 1024.0f;
                    // (just past him -- no more: a step of the table's size put the chair before the barricade behind
                    // it, drawn over by it, seen from the side, 2026-10-27)
                    if (before && k0 < far_k + 32.0f) r = (far_k + 32.0f - k0) / 1024.0f;
                    else if (!before && k0 > near_k - 32.0f) r = (near_k - 32.0f - k0) / 1024.0f;
                    if (r > 900.0f) r = 900.0f; else if (r < -900.0f) r = -900.0f;
                }
                if (cam_behind && cflag == 0) RD8(b + 0x18) = near_group;
                F32(d + 0x24) = r;
            }
        }
    }
}

// Every frame (the frame's start): scenes that hide the table without its every-frame function (the
// entrances' stage shots) hide them too
void announce_watch(void) {
    if (!ann_built || ann_no_table || ann_broken || !(RD8(TABLE + 0x18) & 0x80)) return;
    for (int i = 0; i < 2; i++)
        for (u32 b = 0; b < 28; b++) {
            u32 bone = ann[i].bones + b * 0xDC;
            if (RD32(bone + 0x14)) RD8(bone + 0x18) = 0x80;
        }
    for (int i = 0; i < 2; i++)
        if (ann[i].chair_bones) for (u32 j = 0; j < 2; j++) RD8(ann[i].chair_bones + j * 0xDC + 0x18) = 0x80;
}

// A camera of a test's own (cam: on, eye x y z, at x y z): the view made by it (guLookAtF's way)
static float sqrt_f(float v) { if (v <= 0) return 0; float x = v > 1 ? v : 1; for (int i = 0; i < 20; i++) x = 0.5f * (x + v / x); return x; }
static void look_view(const float* cam, float* view) {
    float ex = cam[1], ey = cam[2], ez = cam[3];
    float lx = cam[4] - ex, ly = cam[5] - ey, lz = cam[6] - ez, n = sqrt_f(lx * lx + ly * ly + lz * lz);
    if (n < 1e-3f) return;
    lx /= n; ly /= n; lz /= n;
    float rx = ly * 0 - lz * 1, ry = lz * 0 - lx * 0, rz = lx * 1 - ly * 0;           // look x up(0,1,0)
    n = sqrt_f(rx * rx + ry * ry + rz * rz);
    if (n < 1e-3f) return;
    rx /= n; ry /= n; rz /= n;
    float ux = ry * lz - rz * ly, uy = rz * lx - rx * lz, uz = rx * ly - ry * lx;
    float v[16] = { rx, ux, -lx, 0,  ry, uy, -ly, 0,  rz, uz, -lz, 0,
                    -(rx * ex + ry * ey + rz * ez), -(ux * ex + uy * ey + uz * ez), lx * ex + ly * ey + lz * ez, 1 };
    for (int i = 0; i < 16; i++) view[i] = v[i];
}
// QA (the qa_cam options, a scenario's "camera"): its camera put in where the game makes the frame's view x
// projection (func_80001580's call of func_80001E08, gen/patches.py); otherwise just that product
// (lumberjack4: and the Lumberjack match's intro camera, lumberjack.c lumberjack_cam -- in the shipped game too; a QA
//  camera comes first)
float qa_cam[7];
extern float lumberjack_cam[7];
void qa_camera_mul(float* view, float* proj, float* out) {
    if (qa_cam[0] != 0.0f) look_view(qa_cam, view);
    else if (lumberjack_cam[0] != 0.0f) look_view(lumberjack_cam, view);
    func_80001E08((u32)view, (u32)proj, (u32)out);
}
// JR's and King's middle (their roots, as announce_qa reads them) -> 1, or 0 (not built)
int announce_where(float* xyz) {
    if (!ann_built || !ann[0].bones || !ann[1].bones) return 0;
    for (u32 k = 0; k < 3; k++) {
        float a = *(volatile float*)(ann[0].bones + 0x5Cu + 48u + 4u * k), b = *(volatile float*)(ann[1].bones + 0x5Cu + 48u + 4u * k);
        if (!(a == a) || !(b == b) || a < -1e5f || a > 1e5f || b < -1e5f || b > 1e5f) return 0;
        xyz[k] = 0.5f * (a + b);
    }
    return 1;
}
#ifdef ARCADE_TEST
// the rig: the arena code's calls of the table's functions -> these (the game's, then the announcers')
static void ann_test_setup(void) { func_8001DB4C(); announce_scene(); }
static void ann_test_frame(void) { func_8001DF38(); announce_table_frame(); }
#define JAL(f) (0x0C000000u | (((u32)(f) >> 2) & 0x03FFFFFFu))
// the rig's camera (dbg_cam, as qa_cam)
float dbg_cam[7];
extern void test_camera_mul(float* view, float* proj, float* out);
static void ann_test_camera(float* view, float* proj, float* out) {
    if (dbg_cam[0] != 0.0f) look_view(dbg_cam, view);
    test_camera_mul(view, proj, out);
}
u32 dbg_ann_cmd;   // tests: 1 = the scene's announcers made now
void announce_frame(void) {
    if (dbg_ann_cmd == 1) { dbg_ann_cmd = 0; announce_scene(); }
    if (RD32(0x8001BD6Cu) == 0x0C0076D3u) RD32(0x8001BD6Cu) = JAL(ann_test_setup);
    if (RD32(0x8001C428u) == 0x0C0077CEu) RD32(0x8001C428u) = JAL(ann_test_frame);
    RD32(0x80001590u) = JAL(ann_test_camera);
}
#else
void announce_frame(void) {}
#endif

// QA (logic.c's QA state): whose a model node is -- 1 / 2 JR's / King's (their bones), 3 / 4 their chairs,
// 0x10 + k a match slot's bones, 0x20 the table's pieces; 0 anything else (props, the ring, ...)
u32 announce_owner(u32 node) {
    if (!node) return 0;
    for (int i = 0; i < 2; i++) {
        if (ann[i].bones && node >= ann[i].bones && node < ann[i].bones + BONES_SZ) return (u32)(1 + i);
        if (ann[i].chair_bones && node >= ann[i].chair_bones && node < ann[i].chair_bones + 2 * 0xDCu) return (u32)(3 + i);
    }
    for (u32 k = 0; k < (u32)nm_slot_span(); k++) if (node >= BONES(k) && node < BONES(k) + BONES_SZ) return 0x10u + k;
    if (node >= TABLE && node < TABLE + 4 * 0xDCu) return 0x20;
    return 0;
}

// QA (logic.c's QA state, 0x807F3000 + 0x1C4..): what JR and King are each frame -- their bones (heap; 0: not
// built), where their roots are in the world (their bone 0's matrix, its last row)
void announce_qa(u32 out) {
    for (u32 i = 0; i < 2; i++) {
        u32 b = ann[i].bones, q = out + i * 16u;
        RD32(q) = b;
        if (b) { RD32(q + 4) = RD32(b + 0x5C + 48); RD32(q + 8) = RD32(b + 0x5C + 52); RD32(q + 12) = RD32(b + 0x5C + 56); }
        else RD32(q + 4) = RD32(q + 8) = RD32(q + 12) = 0;
    }
    // (+0x28: the camera's eye z; +0x2C: their draw flags, JR's | King's << 8 -- 0x80: not drawn; 0xFFFF: not built)
    union { float f; u32 u; } ez; ez.f = dbg_ann_eye[2];
    RD32(out + 0x28) = ann_built ? ez.u : 0;
    RD32(out + 0x2C) = ann_built ? (dbg_ann_draw[0] & 0xFF) | (dbg_ann_draw[1] & 0xFF) << 8 : 0xFFFFu;
    // (+0x30..: King's head pictures -- where, and a checksum of their first 1 KB: one that changes was written over)
    for (u32 i = 0; i < 2; i++) {
        u32 p = ann_built ? ann[1].pics[i] : 0, sum = 0;
        if (p >= 0x80000400u && p < 0x807FFC00u) for (u32 w = 0; w < 256; w++) sum = sum * 31u + RD32(p + w * 4u);
        RD32(out + 0x30 + i * 8u) = p; RD32(out + 0x34 + i * 8u) = sum;
    }
    // (+0x40..: where King's body is drawn: his hips' (bone 1) and head's (bone 5) world places -- his root says
    // where he should be; these, where his body went)
    for (u32 k = 0; k < 2; k++) {
        u32 b = ann_built && ann[1].bones ? ann[1].bones + (k ? 5u : 1u) * 0xDCu : 0, q = out + 0x40 + k * 12u;
        // (a bone's matrix sits at its joint -- most at the hips; +0x8C.. is where its piece is in the world)
        if (b) { RD32(q) = RD32(b + 0x8C); RD32(q + 4) = RD32(b + 0x90); RD32(q + 8) = RD32(b + 0x94); }
        else RD32(q) = RD32(q + 4) = RD32(q + 8) = 0;
    }
}
