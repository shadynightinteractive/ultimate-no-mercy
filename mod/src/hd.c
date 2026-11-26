// hd.c: a model drawn whole, past the game's meshes (SKEL_WM_HD; only a model with that flag ever comes here).
//
// (no2k15, 2026-11-23) Whose: a wrestler folder's own full-detail model (hd.bin + hd.json + pictures.pak: exe
// src/main/arcade_wrestlers.cpp read_hd, tools/wrestler_folder.py), up to HD_MODELS_MAX of them. The two built in -- the
// WWE 2K15 Stone Cold and Rock test slots (0x011A, 0x011B), whose models were third-party rips (hd.bin, hd_rock.bin,
// hd_index.h, hd_index_rock.h) -- are removed: the user, 2026-11-14, "the HD / 2K15 models can be REMOVED from what ships
// (they were only for testing)". The machinery below is kept whole, so a player can build and load his own: model k here is
// the k-th folder's (parts.c puts k in the descriptor's SKEL_WM_HD_MODEL bits).
//
// The game's mesh is at most 127 points and 127 triangles a part, in whole units, with no depth buffer. This model is 25,000+ points, so it's drawn by its own lists, one a bone:
//  - The game builds and animates the model as usual (a small stand-in of it, parts.pkl). Each frame, at func_80002730 (the graphics thread takes the frame; the main thread's waiting, the
//    bones are that frame's), each bone's draw item is pointed at a list of ours: item +34 the list, no vertices of the game's (+30, +2C), no joined items (+8). func_800028B4 forces the item's
//    matrix and calls it. Everything else about the item (its sort key from the bone's centre) stays the game's.
//  - His skeleton, not No Mercy's: a bone keeps its rotation from the animation, but its place is where its parent carries HIS joint (hd.py; his hd.json's joints): No Mercy's elbow isn't where his is. For this function's run
//    too, and only the draw's matrices: the game's own idea of where its bones are (hits, grabs) isn't touched.
//  - Points in 1/16ths of a unit (s16): the bone's matrix is scaled by 1/16 for this function's run (hd_frame_done puts it back before the main thread runs).
//  - Points shared between bones (the elbow, the knee ...: hd.py's weights) are worked out here each frame in the bone's own frame: its inverse x the others'. The rest never change.
//  - Its own depth test: the announcers' buffer (tron.c ann_zclear), in the lists' wrappers. The hard camera (a picture of another size) gets the wrapper without it: hd_zdl_orig.
//  - Its lists and points are past the first 8 MB (mod memory), which the renderer reaches in its extended mode (gEXSetRDRAMExtended), switched on and off in the wrapper, which is below 8 MB
//    because the game's call of an item's list is a physical address.
#include "arcade.h"
#include "gamefn.h"
#include "skel.h"
typedef struct { u32 dl, dln, vtx, vtxn, bl, bln; } HdBone;
typedef struct { u8 bone, parent; float x, y, z; } HdJoint;
#define HD_JOINTS 15
#define HD_SUB 16.0f

#define F32(a) (*(volatile float*)(a))
#define BONES(slot) (SIXA_80091CC0 + (u32)(slot) * 0x1810u)
#define DESC(slot) (SIXA_800A8980 + (u32)(slot) * 0x270u)
#define NODE(bones, k) ((bones) + (u32)(k) * 0xDCu)
#define HD_SLOTS 6
#define HD_STUBS 0x806CC000u            // (expansion RAM, empty in a match: 0x806CC000..0x806E8100)
#define HD_STUB_BYTES 72u
#define HD_STUB(s, k, noz) (HD_STUBS + ((u32)(s) * 28u + (u32)(k)) * 2u * HD_STUB_BYTES + ((noz) ? HD_STUB_BYTES : 0u))
#define HD_BONE_N 28
extern int plat_hd_blend(u32 bones, u32 table, u32 vtx, u32 mats);   // (the exe's, native: src/main/arcade_hd.cpp)
extern void plat_hd_hand(u32 args);
extern void plat_hd_time(int what);
extern void plat_view_hd_pack(int man, u32 addr);
static int qa_hd_clock = -1;            // (QA: the stopwatch, in the log)
// Each man's pack (data/austin2k15/hd_pack.py's layout: tools/wrestler_folder.py writes it into a folder's hd.json). Which: his
// descriptor's flags, SKEL_WM_HD_MODEL (parts.c). A slot's copies are as big as the biggest man's
typedef struct { int pack; u32 vtx_off, vtx_bytes, dl_off, dl_bytes, bl_off, ha_off, hand_r, hand_l; const HdBone* bones; const HdJoint* joints; u32 desc_off, desc_bytes, idx_off; } HdModel;
// The men: the wrestler folders' own, up to the 16 the descriptor's 4 bits can name (none built in since no2k15).
#define HD_MODELS_MAX 16
static HdModel hd_models[HD_MODELS_MAX];
static int hd_models_n;

// A model from a folder, laid out to match wrestler_hd_meta in exe src/main/arcade_wrestlers.cpp.
typedef struct {
    u32 vtx_off, vtx_bytes, dl_off, dl_bytes, bl_off, ha_off, hand_r, hand_l, desc_off, desc_bytes, idx_off;
    HdBone bones[28];
    HdJoint joints[HD_JOINTS];
} HdFolderMeta;
static HdFolderMeta hd_folder_meta[HD_MODELS_MAX];
extern int plat_wrestler_hd_count(void);
extern void plat_wrestler_hd_meta(int i, u32 dst);
extern u32 plat_reserve(u32 bytes);

// Each slot's working copies of a man's points, lists and batches. They have to fit the biggest man there is, and that's
// only known once the folders are read, so they're set aside then, in memory after the content packs.
static u32 hd_vtx_at[HD_SLOTS], hd_dl_at[HD_SLOTS], hd_desc_at[HD_SLOTS];
#define hd_vtx(s)  ((u8*)hd_vtx_at[s])
#define hd_dl(s)   ((u8*)hd_dl_at[s])
#define hd_desc(s) ((u32*)hd_desc_at[s])
static void hd_models_ready(void) {
    static int done;
    if (done) return;
    done = 1;
    int n = plat_wrestler_hd_count();
    if (n > HD_MODELS_MAX) n = HD_MODELS_MAX;
    for (int i = 0; i < n; i++) {
        HdFolderMeta* f = &hd_folder_meta[i];
        plat_wrestler_hd_meta(i, (u32)f);
        HdModel* M = &hd_models[i];
        M->pack = PACK_COUNT + i;
        M->vtx_off = f->vtx_off; M->vtx_bytes = f->vtx_bytes; M->dl_off = f->dl_off; M->dl_bytes = f->dl_bytes;
        M->bl_off = f->bl_off; M->ha_off = f->ha_off; M->hand_r = f->hand_r; M->hand_l = f->hand_l;
        M->bones = f->bones; M->joints = f->joints;
        M->desc_off = f->desc_off; M->desc_bytes = f->desc_bytes; M->idx_off = f->idx_off;
    }
    hd_models_n = n;
    if (!n) return;        // (nobody's: no working copies -- a descriptor that asks for a model is then drawn as the game's, hd_frame)
    u32 vtx_max = 0, dl_max = 0, desc_max = 0;
    for (int i = 0; i < hd_models_n; i++) {
        if (hd_models[i].vtx_bytes > vtx_max) vtx_max = hd_models[i].vtx_bytes;
        if (hd_models[i].dl_bytes > dl_max) dl_max = hd_models[i].dl_bytes;
        if (hd_models[i].desc_bytes > desc_max) desc_max = hd_models[i].desc_bytes;
    }
    for (int s = 0; s < HD_SLOTS; s++) {
        hd_vtx_at[s] = plat_reserve(vtx_max);
        hd_dl_at[s] = plat_reserve(dl_max);
        hd_desc_at[s] = plat_reserve(desc_max);
    }
}
static u32 hd_slot_desc[HD_SLOTS];      // (built for)
static const HdModel* hd_slot_model[HD_SLOTS];
static u8 hd_on[HD_SLOTS];
static int hd_any;
static float hd_saved[HD_SLOTS][HD_BONE_N][32];
u32 dbg_hd_frames, dbg_hd_blends;
static s16 hd_shown[HD_SLOTS][2];

int hd_zbuf_wanted(void) { return hd_any; }
// (QA's state.csv: slot 0's hand pose as the game has it, | what's shown of each hand: right << 8, left << 16)
u32 hd_dbg_pose(void) { return (u32)(u8)RDS8(SIXA_80076588) | (u32)(u8)hd_shown[0][0] << 8 | (u32)(u8)hd_shown[0][1] << 16; }
u32 hd_zdl_orig(u32 dl) {               // (the hard camera's: the wrapper with no depth)
    if (dl < HD_STUBS || dl >= HD_STUBS + HD_SLOTS * 28u * 2u * HD_STUB_BYTES) return dl;
    return dl + HD_STUB_BYTES;
}

static void stub(u32 o, u32 list, int z) {
#define E(a, b) do { RD32(o) = (a); RD32(o + 4) = (b); o += 8; } while (0)
    E(0xE0525464u, 0x10000064u);        // (RT64's extended commands on)
    E(0x6400002Cu, 1u);                 // gEXSetRDRAMExtended(1)
    if (z) E(0xD9FFFFFFu, 1u);          // G_ZBUFFER
    E(0xE200001Cu, z ? 0x0F0A7038u : 0x0F0A7008u);
    E(0xDE000000u, list);
    if (z) E(0xD9FFFFFEu, 0u);
    E(0xE200001Cu, 0x0F0A7008u);
    E(0x6400002Cu, 0u);
    E(0xDF000000u, 0u);
#undef E
}
static void build(int s, const HdModel* M) {
    plat_view_hd_pack((int)(M - hd_models), plat_pack(M->pack));     // (the native window: these stand-ins are this man's HD pictures)
    const u8* p = (const u8*)plat_pack(M->pack) + M->vtx_off;
    u32* d = (u32*)hd_vtx(s);
    for (u32 i = 0; i < M->vtx_bytes / 4u; i++) d[i] = ((const u32*)p)[i];
    const u32* t = (const u32*)((const u8*)plat_pack(M->pack) + M->dl_off);
    u32* l = (u32*)hd_dl(s);
    for (u32 i = 0; i < M->dl_bytes / 8u; i++) {
        u32 w0 = t[2u * i], w1 = t[2u * i + 1u], op = w0 >> 24;
        if (w0 == 0x64000034u) w1 += (u32)hd_desc(s);     // (G_EX_DRAWBATCH_V1)
        else if (op == 0xFDu) w1 += plat_pack(M->pack);
        l[2u * i] = w0; l[2u * i + 1u] = w1;
    }
    {
        const u32* dt = (const u32*)((const u8*)plat_pack(M->pack) + M->desc_off);
        for (u32 i = 0; i < M->desc_bytes / 16u; i++) {
            hd_desc(s)[4u * i] = dt[4u * i] + (u32)hd_vtx(s);
            hd_desc(s)[4u * i + 1u] = dt[4u * i + 1u];
            hd_desc(s)[4u * i + 2u] = dt[4u * i + 2u] + plat_pack(M->pack) + M->idx_off;
            hd_desc(s)[4u * i + 3u] = dt[4u * i + 3u];
        }
    }
    for (int k = 0; k < HD_BONE_N; k++) {
        u32 list = M->bones[k].dln ? (u32)hd_dl(s) + M->bones[k].dl : 0;
        if (!list) continue;
        stub(HD_STUB(s, k, 0), list, 1);
        stub(HD_STUB(s, k, 1), list, 0);      // (the Titantron's hard camera: him whole. The user: no lower quality on it)
    }
}

// the 3 x 3's inverse (rows; a point's row vector x it)
static void inv3(const float* m, float* o) {
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (det == 0.0f) det = 1.0f;
    float q = 1.0f / det;
    o[0] = A * q; o[1] = (c * h - b * i) * q; o[2] = (b * f - c * e) * q;
    o[3] = B * q; o[4] = (a * i - c * g) * q; o[5] = (c * d - a * f) * q;
    o[6] = C * q; o[7] = (b * g - a * h) * q; o[8] = (a * e - b * d) * q;
}
static float hd_m[HD_BONE_N][16];

// His fingers: each toward its closed shape (256) or its straight one (-256), from as he's modelled (0), by the game's own hand poses. The game's table (func_80016D2C: 0x800484CC) names for each pose and hand a shape v. 0 is the model's own hand;
// v is the list's mesh v - 1 (0x80048400 as handposes.py draws it): the loaded entries' point counts at 0x80076A50 + v * 0x10, in RAM, are the list's moved along one (read as v = the list's, 2026-11-03, his two-finger taunt, pose 7, spread his hands open)
static const s16 hd_shape[11][5] = {         // thumb, index, middle, ring, little
    { 200, 200, 200, 200, 200 },              // 0  the hand's own in No Mercy: a loose fist
    { 0, -256, -256, -256, -256 },            // 1  open, fingers apart
    { 0, -256, -256, -256, -256 },            // 2  flat, fingers together
    { 256, 256, -256, 256, 256 },             // 3  the middle finger
    { 0, 200, -256, -256, -256 },             // 4  open, the first finger folded: a grab
    { 180, -256, 256, 256, 256 },             // 5  pointing
    { 256, 256, 256, 256, 256 },              // 6  a tight fist
    { 200, -256, 256, 256, 256 },             // 7  pointing, the thumb up
    { 120, -256, 170, 170, 170 },             // 8  pointing, loose
    { 256, -256, -256, 256, 256 },            // 9  two fingers
    { 180, -256, 200, 200, 200 },             // 10 one finger, loose
};
// (the fingers' shape of each pose for his left hand (17) and right (9), read from the game: handposes.py)
static const u8 hd_pose_l[20] = { 0, 8, 4, 5, 6, 1, 3, 3, 5, 2, 0, 9, 9, 7, 7, 2, 0, 1, 8, 0 };
static const u8 hd_pose_r[20] = { 0, 0, 4, 0, 0, 0, 0, 3, 5, 0, 9, 0, 9, 0, 7, 2, 8, 1, 8, 0 };
static void hd_hand(int s, int side, int shape) {     // (side 0: bone 9, his right; 1: bone 17)
    if (hd_shown[s][side] == shape) return;
    hd_shown[s][side] = (s16)shape;
    const s16* cl = hd_shape[shape];
    int k = side ? 17 : 9;
    const HdModel* M = hd_slot_model[s];
    u32 e = plat_pack(M->pack) + M->ha_off + (side ? M->hand_r * 20u : 0u), n = side ? M->hand_l : M->hand_r;
    u32 vb = (u32)hd_vtx(s) + M->bones[k].vtx;
    u32 tb = plat_pack(M->pack) + M->vtx_off + M->bones[k].vtx;
    static u32 args[5] __attribute__((aligned(8)));
    args[0] = e; args[1] = n; args[2] = vb; args[3] = tb; args[4] = (u32)cl;
    plat_hd_hand((u32)args);     // (the exe's, native)
}

static int qa_hd_cam = -1, hd_cam_t;
extern float qa_cam[7];
static float qa_sin(float a) {          // (|a| <= 2 pi)
    const float pi = 3.14159265f;
    while (a > pi) a -= 2.0f * pi;
    while (a < -pi) a += 2.0f * pi;
    float y = 1.27323954f * a + (a < 0.0f ? 0.40528473f : -0.40528473f) * a * a;
    return y;
}

void hd_frame(s32 gslot) {
    if (qa_hd_clock < 0) qa_hd_clock = (int)plat_config("qa_hd_clock", 0);
    if (qa_hd_clock) plat_hd_time(0);
    hd_any = 0;
    for (int s = 0; s < HD_SLOTS; s++) {
        hd_on[s] = 0;
        u32 desc = DESC(s);
        if (RD32(desc + SKEL_MAGIC_AT) != SKEL_MAGIC_WM || !(RD32(desc + SKEL_WM_FLAGS) & SKEL_WM_HD)) { hd_slot_desc[s] = 0; continue; }
        u32 bones = BONES(s);
        u32 it1 = RD32(NODE(bones, 1) + 0x14u);
        if (!GAME_PTR(it1)) continue;     // (not built)
        {
            u32 mi = (RD32(desc + SKEL_WM_FLAGS) & SKEL_WM_HD_MODEL) >> SKEL_WM_HD_MODEL_AT;
            hd_models_ready();
            // (no2k15: no model with that number -- none loaded -- the game's own model is drawn, as for any descriptor without
            //  SKEL_WM_HD; before, the first built-in man stood in)
            if (mi >= (u32)hd_models_n) { hd_slot_desc[s] = 0; continue; }
            const HdModel* M_ = &hd_models[mi];
            if (hd_slot_desc[s] != desc || hd_slot_model[s] != M_) { build(s, M_); hd_slot_desc[s] = desc; hd_slot_model[s] = M_; hd_shown[s][0] = hd_shown[s][1] = -1; }
        }
        const HdModel* M = hd_slot_model[s];
        hd_on[s] = 1; hd_any = 1;
        // this frame's bones
        for (int k = 1; k < 20; k++) {
            u32 m = NODE(bones, k) + 0x5Cu;
            for (int i = 0; i < 16; i++) hd_m[k][i] = F32(m + 4u * (u32)i);
        }
        // his skeleton: each bone's place from its parent, by his joint. d: how far that moved it, in the bone's own frame (for the matrix with the camera's in it)
        static float hd_d[HD_BONE_N][3];
        for (int k = 0; k < HD_BONE_N; k++) hd_d[k][0] = hd_d[k][1] = hd_d[k][2] = 0.0f;
        for (int n = 0; n < HD_JOINTS; n++) {
            const HdJoint* J = &M->joints[n];
            float* m = hd_m[J->bone]; const float* p = hd_m[J->parent];
            float wx = J->x * p[0] + J->y * p[4] + J->z * p[8] + p[12];
            float wy = J->x * p[1] + J->y * p[5] + J->z * p[9] + p[13];
            float wz = J->x * p[2] + J->y * p[6] + J->z * p[10] + p[14];
            float tx = wx - (J->x * m[0] + J->y * m[4] + J->z * m[8]);
            float ty = wy - (J->x * m[1] + J->y * m[5] + J->z * m[9]);
            float tz = wz - (J->x * m[2] + J->y * m[6] + J->z * m[10]);
            float dx = tx - m[12], dy = ty - m[13], dz = tz - m[14], in[9];
            inv3(m, in);
            hd_d[J->bone][0] = dx * in[0] + dy * in[3] + dz * in[6];
            hd_d[J->bone][1] = dx * in[1] + dy * in[4] + dz * in[7];
            hd_d[J->bone][2] = dx * in[2] + dy * in[5] + dz * in[8];
            m[12] = tx; m[13] = ty; m[14] = tz;
        }
        // the points shared between bones, each in its bone's own frame (native: thousands of points; mod code is translated with no optimiser)
        if (qa_hd_clock) plat_hd_time(2);
        dbg_hd_blends += (u32)plat_hd_blend((u32)M->bones, plat_pack(M->pack) + M->bl_off, (u32)hd_vtx(s), (u32)hd_m);
        if (qa_hd_clock) plat_hd_time(3);
        // his fingers (a pose only when it changes)
        {
            int pose = RDS8(SIXA_80076588 + (u32)s);
            if (pose < 0 || pose >= 20) pose = 0;
            hd_hand(s, 0, hd_pose_r[pose]);
            hd_hand(s, 1, hd_pose_l[pose]);
        }
        // the draw items
        for (int k = 1; k < HD_BONE_N; k++) {
            u32 node = NODE(bones, k), it = RD32(node + 0x14u);
            if (!GAME_PTR(it)) continue;
            RD32(it + 8u) = 0;
            RDS16(it + 0x2Cu) = 0; RD32(it + 0x30u) = 0;
            RD32(it + 0x34u) = (k < 20 && M->bones[k].dln) ? HD_STUB(s, k, 0) : 0;
            RD32(node + 0x10u) = 0;
            // the scale, and the matrices it's put on kept to put back
            if (k < 20) {
                float* sv = hd_saved[s][k];
                for (int r = 0; r < 16; r++) { sv[r] = F32(node + 0x5Cu + 4u * (u32)r); sv[16 + r] = F32(node + 0x9Cu + 4u * (u32)r); }
                // (his skeleton's place: +5C the world's, +9C with the camera's: the move, in the bone's frame, through its rows)
                const float* d = hd_d[k];
                for (int c = 0; c < 4; c++) {
                    F32(node + 0x5Cu + 4u * (u32)(12 + c)) = c < 3 ? hd_m[k][12 + c] : sv[15];
                    F32(node + 0x9Cu + 4u * (u32)(12 + c)) = sv[16 + 12 + c] + d[0] * sv[16 + c] + d[1] * sv[16 + 4 + c] + d[2] * sv[16 + 8 + c];
                }
                for (int r = 0; r < 12; r++) {
                    F32(node + 0x5Cu + 4u * (u32)r) = sv[r] / HD_SUB;
                    F32(node + 0x9Cu + 4u * (u32)r) = sv[16 + r] / HD_SUB;
                }
            }
        }
    }
    if (hd_any) dbg_hd_frames++;
    if (qa_hd_clock) plat_hd_time(1);
    // (QA: a camera on slot 0: its head, or the whole man, from three sides)
    if (qa_hd_cam < 0) qa_hd_cam = (int)plat_config("qa_hd_cam", 0);
    if (qa_hd_cam > 0 && hd_on[0]) {
        hd_cam_t++;
        float a = (float)((hd_cam_t / 180) % 3) * 2.0943951f + 0.3f;
        const float* m = hd_m[qa_hd_cam == 1 ? 5 : 3];
        float py = qa_hd_cam == 1 ? 75.0f : qa_hd_cam == 3 ? 45.0f : 10.0f, d = qa_hd_cam == 1 ? 95.0f : qa_hd_cam == 3 ? 150.0f : 300.0f;      // (3: head to waist, the hands in the air)
        float cx = py * m[4] + m[12], cy = py * m[5] + m[13], cz = py * m[6] + m[14];
        qa_cam[0] = 1.0f;
        qa_cam[1] = cx + d * qa_sin(a); qa_cam[2] = cy + (qa_hd_cam == 1 ? 8.0f : 40.0f); qa_cam[3] = cz + d * qa_sin(a + 1.5707963f);
        if (qa_hd_cam == 3) {     // (3: in front of him, whichever way he faces, a little to one side and then the other: his chest's own forward)
            float sc = m[8] * m[8] + m[9] * m[9] + m[10] * m[10], side = (hd_cam_t / 180) & 1 ? 0.55f : -0.55f;
            float q = sc > 0.0f ? d / (sc * 1.0f) : 0.0f, r = m[0] * m[0] + m[1] * m[1] + m[2] * m[2];
            (void)r;
            float l = 1.0f;       // (the rows' length: the bones' scale; 1 here, as the game's)
            (void)l;
            qa_cam[1] = cx + q * (m[8] + side * m[0]); qa_cam[2] = cy + 12.0f; qa_cam[3] = cz + q * (m[10] + side * m[2]);
        }
        qa_cam[4] = cx; qa_cam[5] = cy; qa_cam[6] = cz;
    }
}
void hd_frame_done(void) {
    for (int s = 0; s < HD_SLOTS; s++) {
        if (!hd_on[s]) continue;
        u32 bones = BONES(s);
        for (int k = 1; k < 20; k++) {
            u32 node = NODE(bones, k);
            const float* sv = hd_saved[s][k];
            for (int r = 0; r < 16; r++) { F32(node + 0x5Cu + 4u * (u32)r) = sv[r]; F32(node + 0x9Cu + 4u * (u32)r) = sv[16 + r]; }
        }
    }
}
