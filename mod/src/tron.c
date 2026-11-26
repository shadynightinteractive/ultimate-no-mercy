// The Titantron: the arena's big screen shows the entering Superstar's video during the entrances (from his music's
// start to its end), and in the match a hard camera's picture of it -- the TV wrestling shot: from the ring's side, a little above it.
//
// The screen's picture is one image (TRON_W x TRON_H RGBA16). In the match the hard camera draws the scene into it
// each frame, after the frame's own scene (see "the hard camera" below); RT64 has the image as its own colour image
// and samples it whole (at its own resolution), so the screen's picture is sharp. During the entrances the image is
// the video's frame, drawn by the CPU. (2026-10-27: first a second camera made by turning the frame's commands --
// the Superstars came and went; then the TV's own picture made small -- the user asked for a hard camera.)
//
// The arena's own screen is made to show the image: the screen's panels (their materials, in the arena's data) are
// found in the frame's list and their texture loads replaced by ones of the image (the panels' texture coordinates
// made to lay it once across the whole screen).
#include "arcade.h"

#define TRON_IMG   0x806C0000u   // the screen's picture in the match (expansion RAM past the frame buffers, 0x80670800:
#define TRON_IMG_BYTES 0x4000u   //  nothing of the game's or the six-man engine's there), then the video's
#define TRON_W     64
#define TRON_H     32
#define TRON_MASKED (TRON_IMG + TRON_IMG_BYTES)   // the picture cut to a screen's own shape (TP_MASK: mask_compose)
#define TRON_VIDEO (TRON_IMG + 2u * TRON_IMG_BYTES)
#define TRON_FRAME 0x806E8100u   // the video's frames read (a TEX: 16-byte header, 256-colour palette, 64 x 64 CI8),
#define TRON_FRAME_B 0x806EA000u //  two: the one shown, the next (read on the main thread while the other's drawn)
#define PHYS(a) ((a) & 0x1FFFFFFFu)
#define F32(a)  (*(volatile float*)(a))

int tron_live = 1, tron_screens = 1;       // the operator's options (logic.c)
u32 dbg_tron[16];
u32 dbg_tron_any;   // tests: the screen's mode (1 the match, 2 the video) whatever's on

// ---- the frame's runs of the scene -------------------------------------------------------------------
// A frame's scene is drawn in runs (func_800028B4(&gfx, slot, first group) each: the groups 0x80061D40[k] from
// `first` while they share a camera); the frame's picture is its colour image (func_80002BB4's).
#define NM_GROUPS     SIXA_80061D40  // u32[]: the groups' first items (stock 0x80061D40; six_addrs.h: moved past six slots)
#define NM_GROUP_N    0x800619B4u  // s16: how many
static u32 scene_head;                     // where this run's commands begin in the task's list
static s32 layer_first, layer_next, layer_slot;
static s32 layer_n;                        // [tronflicker 2026-11-21] (the groups this frame: the run reaching it is the scene's last)
static u32 frame_pic;                      // the frame's picture
static s32 last_first, last_first_was;     // the frame's last run (its first group): this frame's so far, the last frame's (tests: dbg_tron[7])

static u32 group_cam(s32 k) {
    u32 it = RD32(NM_GROUPS + 4u * (u32)k);
    return (GAME_PTR(it)) ? RD32(it) : 0xFFFFFFFFu;
}
void tron_frame_begin(u32 picture) {
    if (GAME_PTR(picture)) frame_pic = RD32(picture);
}
// JR and King's own depth buffer (announce.c ann_zwrap): cleared, and made the depth image, where the frame's scene
// starts -- the frame's size (480 x 240, 16-bit), past the screen's video memory in the expansion RAM's empty part
#define ANN_ZBUF 0x80700000u
u32 dbg_zclears, dbg_zfill;
static void ann_zclear(u32 gfxp) {
    extern int ann_zbuf_wanted(void);
    extern int hd_zbuf_wanted(void);
    if ((!ann_zbuf_wanted() && !hd_zbuf_wanted()) || !frame_pic || !GAME_PTR(gfxp)) return;
    u32 o = RD32(gfxp);
    if (!GAME_PTR(o)) return;
    // The game's fill colour, put back after ours: it sets it once and fills the background with it later (Austin's
    // entrance close-up in the Rumble arena: the background filled with our 0xFFFC, white -- 255, 255, 247 -- inside
    // the game's scissor, now and then; the user, 2026-10-29). Its last one in the list so far (a 16-bit fill's word
    // is the colour twice); black, as the game's, if none.
    u32 fill = 0x00010001u;
    for (u32 p = o - 8u, n = 0; p >= 0x80000400u && n < 4096; p -= 8u, n++) {
        if ((RD32(p) >> 24) != 0xF7u) continue;
        u32 w = RD32(p + 4);
        if ((w >> 16) == (w & 0xFFFFu)) { fill = w; break; }
    }
    dbg_zfill = fill;
#define EMIT(a, b) do { RD32(o) = (a); RD32(o + 4) = (b); o += 8; } while (0)
    // (the depth image named first, then filled through the colour image, as games clear one: the renderer takes that
    // for a depth clear)
    EMIT(0xE7000000u, 0);
    EMIT(0xFE000000u, PHYS(ANN_ZBUF));                                 // the depth image
    EMIT(0xFF1001DFu, PHYS(ANN_ZBUF));                                 // the buffer as the picture: filled far
    EMIT(0xEF300000u, 0);
    EMIT(0xF7000000u, 0xFFFCFFFCu);
    EMIT(0xF6000000u | (479u * 4u << 12) | (239u * 4u), 0);
    EMIT(0xE7000000u, 0);
    EMIT(0xFF1001DFu, PHYS(frame_pic));                                // the frame's picture back
    EMIT(0xF7000000u, fill);                                           // and its fill colour
    EMIT(0xDE000000u, 0x01000010u);                                    // the game's viewport, scissor, modes
#undef EMIT
    RD32(gfxp) = o;
    dbg_zclears++;
}
void tron_scene_begin(u32 gfxp, s32 slot, s32 first) {
    scene_head = (GAME_PTR(gfxp)) ? RD32(gfxp) : 0;
    s32 n = RDS16(NM_GROUP_N);
    layer_first = first;
    layer_slot = slot;
    layer_n = n;
    if (first == 0) { last_first_was = last_first; last_first = 0; ann_zclear(gfxp); }   // (a new frame)
    if (first > last_first) last_first = first;
    // Where this run ends, as func_800028B4 ends it (exe arcade_scene.cpp build: the groups from `first` while their first
    // item is of the first one's kind, up to the groups' count). [tronflicker 2026-11-21] (the user, 2026-11-21: "the
    // titantron is clipping to black again every couple of seconds briefly". This stopped at group 64; a match has
    // hundreds of groups -- one a chain of items, up to 900 -- so it was never the run's real end. Now it is, and the run
    // that reaches the count is the scene's last: tron_scene_end)
    layer_next = first + 1;
    u32 cam = group_cam(first);
    while (layer_next < n && group_cam(layer_next) == cam) layer_next++;
}

// ---- the arena's screen: its panels made to show the picture ---------------------------------------
// A panel: a material of the arena's (a list: the texture's palette loaded, the CI texture, its tile, then a
// branch to the panel's triangles), found by its texture (its CI texels' 2 KB folded) among the arena's for the
// venue. The material is written over with a load of the whole picture (RGBA16, TRON_W x TRON_H) and a call of
// the triangles; the triangles' vertices (loaded before it) get texture coordinates from where they are on the
// screen (its extent in the model, tron_panels.h): the picture once across all of it. The arena's own
// coordinates mirror a quarter of its picture into every part of the screen -- kept so, ours came out in flipped
// tiles (2026-10-27). The originals (material, coordinates) kept to put back -- and to draw the arena's own
// picture of the screen into ours (panels_draw): its steel truss frame around what we show (the user, 2026-10-28;
// a quad of ours over the arena's picture, in its place in the scene, broke the stage in the entrances' close
// shots: the vertex slots and matrix there aren't the screen's).
typedef struct { u8 venue; s16 gx0, gx1, gy0, gy1; u32 sig0, sig1; u8 framed; } TronPanel;   // (framed: its picture has the frame)
#define TRON_PANEL_EXT 1           // (tron_panels_all.h: the rows that need what's below -- the fold over the texture's
                                   //  own size, the extent check, 32 vertices, twin and animated pieces)
#ifndef TRON_SIDE_SCREENS
#define TRON_SIDE_SCREENS 0        // (the second screens: WrestleMania's sides, Backlash's lower one)
#endif
#define XRAW_SCREEN_ONLY
#include "arena_raw_files.h"        // (RAW IS WAR's screen in its match set: gen/arenas_raw.py)
#define XSTAGE_SCREEN_ONLY
#include "arena_stage_files.h"      // (the other arenas' screens in their match sets: gen/arenas_stages.py)
#include "arena_screen_files.h"     // (the arenas' new screens: Bash's board, Survivor Series' Titantron)
#include "tron_panels_all.h"
#define TP_FRAMED 1
#define PATCH_MAX 12
#define PATCH_CMDS 16
#define PATCH_VTX 32               // (SmackDown's oval close-up screen: 31; WrestleMania's side screens: 18)
#define PANEL_COPY 0x806B9000u   // each panel's (PANEL_COPY_BYTES): its vertices as the arena has them; +0x200 the
#define PANEL_COPY_BYTES 0x400u  //  matrix that lays the screen flat on our picture, as its picture lies on the screen;
                                 //  +0x240 its triangles' list (no culling)
#define TRON_MX    6             // what we show, in from the picture's edges (percent of its width, height): the
#define TRON_MY    10            //  arena's truss and a little of the dark border inside it showing (measured off it)
static struct { u32 mat; u32 orig[PATCH_CMDS * 2]; u32 cmd[PATCH_CMDS * 2]; u32 n; u32 settimg; u32 vtx, nv, v0, geo; u8 framed, applied; const TronPanel* pp; u32 st[PATCH_VTX], rgba[PATCH_VTX]; u32 pin_node, pin_dl; } patch[PATCH_MAX];
// The screen shows our picture only once it's been drawn (a pass of ours in a frame before): until then the panels
// are found and kept as they are (2026-10-28: the switch from the arena's picture to a video flashed the last one).
static int img_passes, img_ready;
// Which model of the screen the frame drew (its panels' materials met in its lists): the far one's picture has the
// steel truss painted round it (ours then gets the frame and what we show inside it); the close one (the entrances'
// shots) has a truss of its own round it, in the stage (ours then is all picture, to the truss). The last frame's
// decides this one's (a cut between them: one frame).
static u32 seen_mask, seen_last;

static int n_patch;
static u32 shown_img;                       // the picture the panels show
static u32 seg_base[16];

static u32 dl_addr(u32 a) {                 // a list's address (segmented or physical) -> RDRAM
    u32 sg = (a >> 24) & 15;
    if ((a >> 24) < 0x10 && sg) a = seg_base[sg] + (a & 0x00FFFFFFu);
    a = 0x80000000u | PHYS(a);
    return a;
}
static void mtx_fixed(const float* m, u32 dst) {   // guMtxF2L's
    for (u32 i = 0; i < 16; i += 2) {
        float fa = m[i] * 65536.0f, fb = m[i + 1] * 65536.0f;
        if (fa > 2.1e9f) fa = 2.1e9f; else if (fa < -2.1e9f) fa = -2.1e9f;
        if (fb > 2.1e9f) fb = 2.1e9f; else if (fb < -2.1e9f) fb = -2.1e9f;
        u32 a = (u32)(s32)fa, b = (u32)(s32)fb;
        RD32(dst + 2u * i) = (a & 0xFFFF0000u) | (b >> 16);
        RD32(dst + 0x20u + 2u * i) = (a << 16) | (b & 0xFFFFu);
    }
}
static int patched(u32 mat) {
    for (int i = 0; i < n_patch; i++) if (patch[i].mat == mat) return i;
    return -1;
}
static int panel_ours(int i) { return RD32(patch[i].settimg) == (0xFD100000u | ((u32)TRON_W - 1)); }   // (else the arena's gone)
static int panel_masked(int i) { return (patch[i].pp->framed & TP_MASK) != 0; }
static u32 panel_img(int i, u32 img) { return panel_masked(i) ? TRON_MASKED : img; }   // (the picture it shows)
static int panel_alive(int i) {             // (the material ours, or still the arena's: the arena not gone)
    return patch[i].applied ? panel_ours(i) : RD32(patch[i].mat) == patch[i].orig[0] && RD32(patch[i].mat + 4) == patch[i].orig[1];
}
static void unpatch_all(void) {
    for (int i = 0; i < n_patch; i++) {
        if (!patch[i].applied || !panel_ours(i)) continue;            // (not ours; the arena's gone: nothing to put back)
        u32 m = patch[i].mat;
        for (u32 k = 0; k < patch[i].n * 2; k++) RD32(m + 4u * k) = patch[i].orig[k];
        // Changed for new features (Ultimate No Mercy) -- any32, 2026-11-18: a vertex is put back only while it is still
        // ours (its colour the white panel_apply wrote): with the match over its memory may be the menus' already --
        // a 12-man match's Titantron vertices were the menus' rows, and putting them back crashed the menus
        // (give_page_row_objects, any32_singles12 / dbg12)
        for (u32 v = 0; v < patch[i].nv; v++) {
            if (RD32(patch[i].vtx + 16u * v + 12u) != 0xFFFFFFFFu) continue;
            RD32(patch[i].vtx + 16u * v + 8u) = patch[i].st[v]; RD32(patch[i].vtx + 16u * v + 12u) = patch[i].rgba[v];
        }
    }
    n_patch = 0;
}
static void panel_apply(int i);
// Changed for new features (Ultimate No Mercy) -- any32, 2026-11-18: a match with extra slots is over (slots.c
// match_slots_after, as the menus' overlay comes back): the arena's memory the panels were patched in is the menus'
// now, so the patches are forgotten, not put back (putting them back wrote over the menus' page rows: any32_singles12).
void tron_forget_patches(void) { n_patch = 0; }
// An animated piece of the entrances' close-up scene (func_8001FFA8: its node's list, +0x10, set to frame k's every few
// frames -- each frame its own list: its vertices, then a call of its own material): the frame whose material this is,
// held while the panel shows ours (tron_hold_anim). Nodes and frame lists by the scene's index (s16 0x80083FDE):
// piece 1 node 0x800836D8, frames u32 0x800837B8[k], count u16 0x80052738[i]; piece 2 0x80083B78, 0x80083C58,
// 0x800527CC. (Rig, 2026-10-31: SmackDown's, Survivor Series', WrestleMania's, Backlash's frames call the materials.)
static void anim_find(int i, u32 mat) {
    s32 idx = RDS16(0x80083FDEu);
    if (idx < 0 || idx > 14) return;
    static const u32 node[2] = { 0x800836D8u, 0x80083B78u }, frames[2] = { 0x800837B8u, 0x80083C58u };
    static const u32 count[2] = { 0x80052738u, 0x800527CCu };
    for (u32 p = 0; p < 2; p++) {
        u32 n = RD16(count[p] + 2u * (u32)idx);
        for (u32 k = 0; k < n && k < 32; k++) {
            u32 f = RD32(frames[p] + 4u * k);
            if (!GAME_PTR(f)) continue;
            for (u32 q = 0; q < 8; q++) {
                u32 w0 = RD32(f + 8u * q);
                if (w0 >> 24 == 0xDF) break;
                if (w0 >> 24 == 0xDE && dl_addr(RD32(f + 8u * q + 4)) == mat) { patch[i].pin_node = node[p]; patch[i].pin_dl = f; return; }
            }
        }
    }
}
// the animated pieces found held at their frame while ours shows (as the graphics thread takes the frame: before
// func_800025B0 reads the nodes' lists)
static void tron_hold_anim(void) {
    for (int i = 0; i < n_patch; i++)
        if (patch[i].pin_node && patch[i].applied && panel_ours(i)) RD32(patch[i].pin_node + 0x10u) = patch[i].pin_dl;
}
// the panel's material at mat, the vertices loaded before it (the command vw0: nv at vtx): made to show the picture
static void panel_patch(const TronPanel* pp, u32 mat, u32 vw0, u32 vtx, u32 nv) {
    if (n_patch >= PATCH_MAX || nv == 0 || nv > PATCH_VTX || pp->gx1 == pp->gx0 || pp->gy1 == pp->gy0) return;
    u32 v0 = ((vw0 >> 1) & 0x7Fu) - nv;                                // (the slots they're in)
    if (v0 + nv > 32u) return;
    // the material: its commands up to the branch to its triangles
    u32 n = 0, geo = 0;
    for (; n < PATCH_CMDS; n++) {
        u32 w0 = RD32(mat + 8u * n), w1 = RD32(mat + 8u * n + 4);
        if (w0 >> 24 == 0xDE && ((w0 >> 16) & 0xFF) == 1) { geo = w1; n++; break; }
        if (w0 >> 24 == 0xDF) return;
    }
    if (!geo || n < 12) return;
    int i = n_patch++;
    patch[i].mat = mat; patch[i].n = n; patch[i].vtx = vtx; patch[i].nv = nv; patch[i].v0 = v0; patch[i].framed = pp->framed & TP_FRAMED;
    patch[i].pp = pp; patch[i].applied = 0;
    patch[i].pin_node = 0; patch[i].pin_dl = 0;
    if (pp->framed & TP_ANIM) {                                        // (its frames each match a row: the first found
        anim_find(i, mat);                                             //  is the one held)
        if (!patch[i].pin_node) { n_patch--; return; }                // (not the close scene's animated piece: the
                                                                       //  match's set has its first frame too, 2026-10-31
                                                                       //  -- Survivor Series' disc: no screen in the match)
        for (int k = 0; k < i; k++) if (patch[k].pin_node == patch[i].pin_node) { n_patch--; return; }
    }
    seen_mask |= 1u << i;
    // its triangles' list, for drawing it into the picture: its culling taken out (laid flat it'd go: the far
    // screen's list turns on back-face culling itself -- the frame came out black in the match, 2026-10-28)
    u32 gc = PANEL_COPY + PANEL_COPY_BYTES * (u32)i + 0x240u, ga = dl_addr(geo);
    for (u32 k = 0; k < 56u; k++) {
        u32 w0 = RD32(ga + 8u * k), w1 = RD32(ga + 8u * k + 4), op = w0 >> 24;
        if (op == 0xD9) { w0 = 0; w1 = 0; }                           // (geometry mode: no-op)
        else if (op == 0xDE || op == 0x01 || op == 0xFD) w1 = PHYS(dl_addr(w1));
        RD32(gc + 8u * k) = w0; RD32(gc + 8u * k + 4) = w1;
        if (op == 0xDF || (op == 0xDE && ((w0 >> 16) & 0xFF) == 1)) break;
        if (k == 55u) { RD32(gc + 8u * k) = 0xDF000000u; RD32(gc + 8u * k + 4) = 0; }
    }
    patch[i].geo = PHYS(gc);
    for (u32 k = 0; k < n * 2; k++) patch[i].orig[k] = RD32(mat + 4u * k);
    for (u32 k = 0; k < n; k++) {                                     // (its commands, their addresses made whole:
        u32 w0 = patch[i].orig[2 * k], w1 = patch[i].orig[2 * k + 1], op = w0 >> 24;   //  drawn later, the segments
        if (op == 0xFD || op == 0xDE || op == 0x01 || op == 0xDA || op == 0xDC) w1 = PHYS(dl_addr(w1));   //  moved on)
        patch[i].cmd[2 * k] = w0; patch[i].cmd[2 * k + 1] = w1;
    }
    // its vertices as they are, and the matrix that lays them flat on our picture as ours will lie on them
    u32 c = PANEL_COPY + PANEL_COPY_BYTES * (u32)i;
    for (u32 k = 0; k < nv * 4u; k++) RD32(c + 4u * k) = RD32(vtx + 4u * k);
    float w = (float)(pp->gx1 - pp->gx0), h = (float)(pp->gy1 - pp->gy0);
    float m[16] = { -2.0f / w, 0, 0, 0,  0, 2.0f / h, 0, 0,  0, 0, 0, 0,
                    (float)(pp->gx1 + pp->gx0) / w, -(float)(pp->gy1 + pp->gy0) / h, 0, 1.0f };
    mtx_fixed(m, c + 0x200u);
    if (img_ready) panel_apply(i);
}
// the panel made to show our picture (its S / T, its material)
static void panel_apply(int i) {
    const TronPanel* pp = patch[i].pp;
    u32 vtx = patch[i].vtx, nv = patch[i].nv, mat = patch[i].mat, n = patch[i].n;
    u32 geo = patch[i].orig[2 * (n - 1) + 1];
    patch[i].applied = 1;
    // each vertex's S / T (10.5) from where it is on the screen: x across (gx0 .. gx1: TRON_W .. 0), y down
    // (gy1 .. gy0: 0 .. TRON_H) -- as the arena's own picture runs on its screen up close
    for (u32 v = 0; v < nv; v++) {
        u32 a = vtx + 16u * v;
        patch[i].st[v] = RD32(a + 8u); patch[i].rgba[v] = RD32(a + 12u);
        s32 x = RDS16(a), y = RDS16(a + 2);
        if ((pp->framed & TP_TWIN) && x < 0) x += pp->gx0 + pp->gx1;     // (the copy at -x: as the one at +x)
        s32 ss = (s32)(pp->gx1 - x) * (s32)TRON_W * 32 / (s32)(pp->gx1 - pp->gx0);
        s32 tt = (s32)(pp->gy1 - y) * (s32)TRON_H * 32 / (s32)(pp->gy1 - pp->gy0);
        if (ss < 0) ss = 0; if (ss > TRON_W * 32) ss = TRON_W * 32;
        if (tt < 0) tt = 0; if (tt > TRON_H * 32) tt = TRON_H * 32;
        RD32(a + 8u) = ((u32)(u16)ss << 16) | (u32)(u16)tt;
        RD32(a + 12u) = 0xFFFFFFFFu;                                    // (white: the picture as it is, no glow)
    }
    u32 line = (TRON_W * 2u + 7u) / 8u;
    u32 o = mat;
#define PW(a, b) do { RD32(o) = (a); RD32(o + 4) = (b); o += 8; } while (0)
    PW(0xE3001001u, 0x00000000u);                                   // no palette
    PW(0xD7000002u, 0xFFFFFFFFu);                                   // the texture's scale: 1 (S / T are texels)
    patch[i].settimg = o;
    PW(0xFD100000u | ((u32)TRON_W - 1), PHYS(panel_img(i, shown_img ? shown_img : TRON_IMG)));
    PW(0xF5100000u | (line << 9), 0x07000000u);                     // load tile
    PW(0xE6000000u, 0);
    PW(0xF4000000u, 0x07000000u | ((u32)(TRON_W - 1) << 14) | ((u32)(TRON_H - 1) << 2));   // the whole picture
    PW(0xE7000000u, 0);
    PW(0xF5100000u | (line << 9), 0x00080200u);                      // render tile 0: RGBA16, clamped
    PW(0xF2000000u, ((u32)(TRON_W - 1) * 4u << 12) | ((u32)(TRON_H - 1) * 4u));
    PW(0xDE000000u, geo);                                           // the triangles (a call)
    PW(0xDF000000u, 0);
    while (o < mat + 8u * n) PW(0, 0);                              // (no-ops)
#undef PW
    dbg_tron[13]++;
}
// the arena's own picture of the screen drawn into ours (the viewport: all of it), each panel as it lies on the
// screen -- its material, its vertices as they were (their own S / T), laid flat, its triangles
static int tron_framed = 1;
static u32 panels_draw(u32 o, u32 end, int whole) {   // (whole: the screen in view's whole picture; else the far one's frame)
#define EMIT(a, b) do { RD32(o) = (a); RD32(o + 4) = (b); o += 8; } while (0)
    for (int i = 0; i < n_patch; i++) {
        int want = whole ? patch[i].framed == (u8)tron_framed : patch[i].framed;
        if (!panel_alive(i) || !want || o + 8u * (patch[i].n + 12u) > end) continue;
        u32 c = PANEL_COPY + PANEL_COPY_BYTES * (u32)i;
        EMIT(0xE7000000u, 0);
        for (u32 k = 0; k + 1 < patch[i].n; k++) EMIT(patch[i].cmd[2 * k], patch[i].cmd[2 * k + 1]);
        EMIT(0xD9FEF9FFu, 0);                                       // no culling (laid flat it's mirrored), no fog
        EMIT(0xE200001Cu, 0x0F0A4000u);                             // opaque
        EMIT(0xFCFFFFFFu, 0xFFFCF279u);                             // the texel as it is
        EMIT(0xDC38000Eu, PHYS(c + 0x200u));                        // laid flat
        EMIT(0xDB0C0000u, 0x00010000u);
        EMIT(0x01000000u | (patch[i].nv << 12) | ((patch[i].v0 + patch[i].nv) << 1), PHYS(c));
        EMIT(0xDE000000u, patch[i].geo);
    }
#undef EMIT
    return o;
}
u32 dbg_mats[128][8], dbg_mat_n;
// the vertices loaded for the material all on the row's screen (within 8): three arenas' screens are one colour all
// over (their textures fold to 0 / 0, 0xEEEEEE00 / 0), and RAW IS WAR's close-up screen has the far one's picture
static int panel_inside(const TronPanel* pp, u32 vtx, u32 nv) {
    if (nv == 0 || nv > PATCH_VTX) return 0;
    for (u32 v = 0; v < nv; v++) {
        s32 x = RDS16(vtx + 16u * v), y = RDS16(vtx + 16u * v + 2);
        if ((pp->framed & TP_TWIN) && x < 0) x = -x;
        if (x < pp->gx0 - 8 || x > pp->gx1 + 8 || y < pp->gy0 - 8 || y > pp->gy1 + 8) return 0;
    }
    return 1;
}
static u32 seen_mat[256]; static int seen_n, seen_at;   // (materials looked at already)
static int checked(u32 mat) {
    for (int i = 0; i < seen_n; i++) if (seen_mat[i] == mat) return 1;
    seen_mat[seen_at] = mat; seen_at = (seen_at + 1) & 255; if (seen_n < 256) seen_n++;
    return 0;
}
// a material (a list called from the arena's): one of the venue's panels? (its CI texture's 2 KB folded)
static void material_check(u32 mat, u32 vw0, u32 vtx, u32 nv, u32 venue) {
    if (!GAME_PTR(mat)) return;
    { int k = patched(mat); if (k >= 0) { seen_mask |= 1u << k; return; } }   // (drawn this frame)
    for (u32 q = 0; q < 12; q++) {
        u32 c0 = RD32(mat + 8u * q), c1 = RD32(mat + 8u * q + 4);
        if (c0 >> 24 == 0xDE || c0 >> 24 == 0xDF) return;
        if (c0 >> 24 != 0xFD || ((c0 >> 21) & 7) != 2) continue;    // (a CI texture's image)
        u32 tex = dl_addr(c1), words = 512;
        for (u32 r = q + 1; r < q + 6 && r < PATCH_CMDS; r++) {          // (its load block: what's loaded -- a 64 x 32
            u32 l0 = RD32(mat + 8u * r), l1 = RD32(mat + 8u * r + 4);  //  CI4 is 1 KB; 2 KB folded ran into what
            if (l0 >> 24 != 0xF3) continue;                             //  follows it in the heap)
            u32 siz = (c0 >> 19) & 3, texels = ((l1 >> 12) & 0xFFFu) + 1u;
            u32 bytes = siz == 3 ? texels * 4u : (texels << siz) >> 1;
            if (bytes / 4u < words) words = bytes / 4u;
            break;
        }
        if (!GAME_PTR(tex)) return;
        if (checked(mat)) return;
        u32 a = 0, b = 0;
        for (u32 i = 0; i < words; i++) { u32 w = RD32(tex + 4u * i); a += w; b = ((b << 1) | (b >> 31)) ^ w; }
        if (dbg_mat_n < 128) {             // (tests: the materials looked at)
            u32* d = dbg_mats[dbg_mat_n++];
            d[0] = mat; d[1] = tex; d[2] = vtx; d[3] = nv; d[4] = a; d[5] = b;
            d[6] = nv ? RD32(vtx) : 0; d[7] = nv ? RD32(vtx + 4) : 0;
        }
        for (u32 i = 0; i < sizeof(tron_panels) / sizeof(tron_panels[0]); i++) {
            const TronPanel* pp = &tron_panels[i];
            if ((pp->venue == venue || pp->venue == 0xFE) && pp->sig0 == a && pp->sig1 == b && panel_inside(pp, vtx, nv)) {
                panel_patch(pp, mat, vw0, vtx, nv); break;
            }
        }
        return;
    }
}
// a list's commands (the layer's, then the lists it calls, one level down): vertices loaded, materials called
static void list_scan(u32 p, u32 end, int depth, u32 venue) {
    u32 vtx = 0, nv = 0, vw0 = 0;
    for (u32 n = 0; n < 4096 && (end ? p < end : 1); n++, p += 8) {
        if (!GAME_PTR(p)) return;
        u32 w0 = RD32(p), w1 = RD32(p + 4), op = w0 >> 24;
        if (op == 0xDF) return;
        if (op == 0xDB && ((w0 >> 16) & 0xFF) == 0x06) { if (depth == 0) seg_base[((w0 & 0xFFFF) >> 2) & 15] = w1 & 0x1FFFFFFFu; continue; }
        if (op == 0x01) { nv = (w0 >> 12) & 0xFF; vtx = dl_addr(w1); vw0 = w0; if (vtx > GAME_RAM_TOP - 16u * 64u) nv = 0; continue; }
        if (op != 0xDE) continue;
        u32 t = dl_addr(w1);
        material_check(t, vw0, vtx, nv, venue);
        if (depth == 0 && ((w0 >> 16) & 0xFF) == 0) list_scan(t, 0, 1, venue);
        if (((w0 >> 16) & 0xFF) != 0) return;               // (a branch: the list ends there)
    }
}
static s32 story_v;                         // (a story's Titantron shot: tron_story below)
static s16 story_a = -1;
// the layer's commands looked through for the venue's panels
static void panels_find(u32 end) {
    for (int k = 0; k < 16; k++) seg_base[k] = 0;
    extern int arena_current(void);
    // (a story's Titantron shot: the node's arena -- arena_current is the arcade's pick, not the story's: QA 2026-11-02 story_tron_arenas, no video on
    //  NO MERCY's, KING OF THE RING's, BACKLASH's screens, it looked for RAW's)
    list_scan(scene_head, end, 0, story_v > 0 && story_a >= 0 ? (u32)story_a : (u32)arena_current());   // (0-9 the game's; 10.. the arcade's: arena.c)
}
// the panels' picture this frame
static void panels_show(u32 img) {
    shown_img = img;
    for (int i = 0; i < n_patch; i++) {
        if (!patch[i].applied && panel_alive(i)) { if (img_ready) panel_apply(i); continue; }
        if (patch[i].applied && panel_ours(i)) RD32(patch[i].settimg + 4) = PHYS(panel_img(i, img));
        else { patch[i] = patch[--n_patch]; i--; }                   // (written over: the arena's gone)
    }
}
// the picture's inner part (what we show: the hard camera, the video), in quarter pixels
#define IN_X0 ((u32)TRON_W * 4u * TRON_MX / 100u)
#define IN_X1 ((u32)TRON_W * 4u - IN_X0)
#define IN_Y0 ((u32)TRON_H * 4u * TRON_MY / 100u)
#define IN_Y1 ((u32)TRON_H * 4u - IN_Y0)

// ---- what the screen shows -------------------------------------------------------------------------
// The entering Superstar's video while the entrances run (while his music plays); the match itself in the match;
// else the arena's own screen.
s32 tron_v;                                // the video picked
static int vid_k, vid_left, vid_ready;
// An entrance's video plays while his theme does: from the music's start (func_800037EC) to its stop or fade
// (func_800037BC) -- not by the entrance's steps (2026-10-27: the video came and went with the steps, not the music)
static int music_on;
// 2026-11-10: what the exe plays natively for the man entering (NATIVE_* bits, arcade.h), from his theme or video file
// in a common format. Asked as his music starts, called off as it stops. A native video is drawn by the exe in place
// of the frame video_pass draws: the mod's part is to keep that pass going (tron_mode) with a black frame (tron_main).
static int native_bits;
// 2026-11-12: his theme file as a song in the ring (a Rumble run-in's, the winner's: media.c), sound only. Kept apart from
// native_bits, which the entrance screen's end calls off (tron_mode); this one stops with the game's song (music_off).
static int ring_song_bits;
s32 tron_stop_fade;
static void music_off(void) {
    if (native_bits || ring_song_bits) { plat_entrance_media((u32)tron_stop_fade, 0, 0); native_bits = 0; ring_song_bits = 0; }
    tron_stop_fade = 0;
    music_on = 0;
}
void tron_native_cancel(void) {
    if (native_bits || ring_song_bits) { plat_entrance_media(0, 0, 0); native_bits = 0; ring_song_bits = 0; }
}
int tron_ring_song(u32 id) {
    ring_song_bits = plat_entrance_media(id & 0xFFFFu, 1, 1) & NATIVE_THEME;
    return ring_song_bits;
}
int tron_music(int on) {
    int ent = RDS16(NM_SCREEN) == 42 && RDS32(NM_ENTRANCE_STEP) >= 1 && RDS32(NM_ENTRANCE_STEP) <= 29;
    if (on && ent) {                                  // his video (whose music this is: the one entering), from its start
        // (2026-11-02, the user: no video of his own, none at all -- it kept the last one picked: the opponent's, or
        // the last man's of an earlier match, for the moment before the entrance's step picks again)
        s32 v = arcade_entrant_video();
        tron_v = v > 0 ? v : 0;
        music_on = 1; vid_k = -1; vid_left = 0; vid_ready = 0;
        s32 who = RDS32(0x80133B94u);                 // (the entrant, as arcade_entrant_video reads him)
        native_bits = (who >= 0 && who < 4) ? plat_entrance_media(RD32(SIXA_800A4038 + 4u * (u32)who) & 0xFFFFu, 0, 1) : 0;
        return native_bits;
    }
    else if (!on) music_off();
    return 0;
}
void tron_video_picked(s32 v) { if (v >= 0 && v != tron_v) { tron_v = v; vid_k = -1; vid_left = 0; vid_ready = 0; } }
#define TM_NONE 0
#define TM_LIVE 1
#define TM_VIDEO 2
// A story scene's Titantron shot (2026-11-02, the user: "a closeup of the titan tron in the arena, not just a full screen
// titantron"): the man's video on the arena's own screen while the shot lasts (story.c SO_TRON). 0: off.
void tron_story(s32 v) {
    story_v = v;
    if (v <= 0) story_a = -1;
    if (v > 0) { tron_v = v; vid_k = -1; vid_left = 0; vid_ready = 0; }
}
int tron_story_on(void) { return tron_screens && story_v > 0; }
// The arena's screen, in the world (the stage set's node sits 1500 on: data/storyup/tron/project.py, screen_z.py): its
// middle and width -> 1. None (no screen of its own): 0.
static const struct { s16 arena, x, y, z, w; } tron_place[] = {
    { 0, 0, 710, 2695, 1160 },     // RAW IS WAR (its set at full size)
    { 1, 0, 508, 2656, 1024 },     // NO MERCY
    { 2, -990, 450, 2434, 500 },   // SMACKDOWN: the oval sign
    { 3, 0, 1240, 2652, 720 },     // KING OF THE RING
    // (SURVIVOR SERIES: none yet -- its disc is the arena's own logo, the user, 2026-11-01, its Titantron above the stage to come: the full-screen cut, as SUMMERSLAM)
    { 7, 776, 200, 2380, 496 },    // WRESTLEMANIA: the right side screen
    { 8, 0, 620, 2716, 600 },      // BACKLASH
    { 12, 277, 135, 1375, 254 },   // BASH AT THE BEACH: its picture board under the hut
    { 13, 0, 682, 1880, 808 },     // RAW IS WAR WM 2000
    { 17, 0, 682, 1880, 808 },     // SUNDAY NIGHT HEAT
};
void tron_story_arena(int a) { story_a = a; }
int tron_screen_place(int a, float* xyz, float* width) {
    for (u32 k = 0; k < sizeof tron_place / sizeof tron_place[0]; k++) {
        if (tron_place[k].arena != a) continue;
        xyz[0] = tron_place[k].x; xyz[1] = tron_place[k].y; xyz[2] = tron_place[k].z; *width = tron_place[k].w;
        return 1;
    }
    return 0;
}
static int tron_mode(void) {
    s32 mode = RDS32(NM_MODE);
    if (dbg_tron_any) return (int)dbg_tron_any;
    if (tron_story_on()) return TM_VIDEO;
    s32 st = RDS32(NM_ENTRANCE_STEP);
    if ((mode == 0 || mode == -2) && RDS16(NM_SCREEN) == 42 && st >= 1 && st <= 29)   // the entrances: the Superstar's Titantron video, never the camera
        return (tron_screens && (tron_v > 0 || (native_bits & NATIVE_VIDEO)) && music_on && (u32)RDS32(0x80133B94u) < 4u) ? TM_VIDEO : TM_NONE;   // (2026-11-02: only while someone's entering -- after the last man the game picks a video of its own: an unrelated man on the screen for a moment)
    if (native_bits) music_off(); else music_on = 0;    // (2026-11-10: out of the entrances: the exe's theme and video stop too)
    if (mode == -2 || mode == -6) return tron_live ? TM_LIVE : TM_NONE;
    return TM_NONE;
}
// QA (state.csv's bs_step in the entrances): the video picked | the screen's mode << 16 | his music on << 20 | the entrant << 24
u32 tron_qa(void) { return ((u32)tron_v & 0xFFFFu) | (u32)tron_mode() << 16 | (u32)music_on << 20 | ((u32)RDS32(0x80133B94u) & 0xFu) << 24; }
static int vid_front;                      // (the frame shown: TRON_FRAME, TRON_FRAME_B)
static u32 vid_buf_at(int k) { return k ? TRON_FRAME_B : TRON_FRAME; }
void tron_main(void) {                     // (the main thread's, each frame: its file reads are its own)
    if (tron_mode() != TM_VIDEO) return;
    // 2026-11-10: a native video (the exe's): no frames of the game's are read. The frame video_pass draws is a black one
    // (palette: 256 x black, texels: 0), which the exe's renderer replaces with the decoded picture (arcade_view.cpp).
    if ((native_bits & NATIVE_VIDEO) && music_on && !tron_story_on()) {
        if (!vid_ready) {
            for (u32 i = 0; i < 256u; i++) RD16(TRON_FRAME + 16u + 2u * i) = 0x0001u;
            for (u32 i = 0; i < 4096u; i += 4u) RD32(TRON_FRAME + 16u + 512u + i) = 0;
            vid_front = 0; vid_ready = 1;
        }
        return;
    }
    if (vid_left > 0) { vid_left--; return; }
    int t = 0;
    u32 back = vid_buf_at(vid_front ^ 1);
    int r = media_tron_frame(tron_v, vid_k + 1, back, 1, &t);
    if (r != 1) { vid_k = -1; r = media_tron_frame(tron_v, 0, back, 1, &t); if (r != 1) return; }
    vid_k++;
    vid_left = t > 0 ? t : 4;
    vid_front ^= 1;                                   // (shown from the next pass on, whole)
    vid_ready = 1;
}

// ---- the hard camera: the screen's picture in the match ----------------------------------------------
// No Mercy draws without a depth buffer. The scene's items (0x80060BA0: bones and objects -- +0 a chain (1) or not
// (0), +4 its node, +8 the next of its chain, +0xC.. its centre, +0x24 its order bias, +0x2C/+0x2E/+0x30 its
// vertices (how many, the first slot, where), +0x34 its triangles, +0x38 its matrix for each of the frame's two
// slots) are sorted far to near for the camera (func_800022D8's key, func_80002400) and drawn so in groups
// (func_800025B0's eight passes, as group / kind; func_800028B4: each of a group's items' matrices forced and its
// vertices loaded, then their triangles). Every item's matrix is its node's world matrix (+0x5C) x the frame's view
// x projection (the node's +0x9C, made a fixed-point matrix by func_800025B0; the HUD's items: none) -- checked over
// a whole RAW match's items, 2026-10-27. So the hard camera's picture is the same items, each with world x the hard
// camera's, ordered, culled and grouped as the game does it for that camera: built as the graphics thread takes the
// frame (func_80002730, before the match goes on: the nodes are the frame's), drawn into the screen's picture after
// the frame's scene (its own viewport; the frame's picture and viewport put back after).
#define NM_ITEMS      SIXA_80060BA0  // u32[]: the scene's items, sorted (stock 0x80060BA0; six_addrs.h: moved past six slots)
#define NM_ITEM_N     0x800619B2u  // s16: how many
#define NM_ITEM_GROUP 0x80043618u  // u8[]: a chain's group, by its node's +0x18
#define NM_CULL_K     0x80055828u  // double: a chain is seen while |x| and |y| <= it x (z + 150) (func_800024F8)
#define NM_LIGHT_SET  0x80043610u  // s16: the prim / env colours' set (0x80062B68, 0x80062B88: 4 sets of 4)
#define HC_MTX   0x80680000u       // the hard camera's matrices, one block a slot (0x10000: HC_MTX_N)
#define HC_MTX_N 1024u
#define HC_DL    0x806A0000u       // its list, one a slot
#define HC_DL_BYTES 0xC000u
#define HC_VPORT 0x806B8000u       // its viewport: the screen's picture (all of it; +0x10 its inner part)
#define HC_VPORT_IN (HC_VPORT + 0x10u)
#define HC_ITEMS 640
static const float hc_eye[3] = { 820.0f, 420.0f, 0.0f }, hc_at[3] = { 0.0f, 150.0f, 0.0f };   // the ring's side, high, looking down (the user's TV picture; the near apron just in)
#define HC_COT   3.7320508f        // the lens: 30 degrees high (cot 15); TRON_W : TRON_H wide
#define HC_NEAR  16.0f             // as the game's (func_8000E098)
#define HC_FAR   8192.0f
static float hc_vp_all[16], hc_vp_in[16], *hc_vp = hc_vp_in;
static u32 hc_item_of[2][HC_MTX_N];        // (whose each of the hard camera's matrices is, for the native picture to know the same object from update to update: the items come in a different order each time)
extern void plat_view_tron(u32 vp, u32 items);
static int hc_made, hc_slot = -1;          // the slot whose list is ready (-1 none)
static u32 hc_pic_word;                    // (the list's command that puts the frame's picture back)
static struct { u32 item; float key; u8 group, kind; } hc_it[HC_ITEMS];
static const u8 hc_passes[8][2] = { {0,0}, {0,1}, {6,0}, {3,0}, {4,1}, {4,0}, {5,1}, {5,0} };   // func_80002730's

static float hc_sqrt(float v) { if (v <= 0) return 0; float x = v > 1 ? v : 1; for (int i = 0; i < 16; i++) x = 0.5f * (x + v / x); return x; }
static void hc_camera(void) {              // view (guLookAtF's) x projection (guPerspectiveF's)
    float ex = hc_eye[0], ey = hc_eye[1], ez = hc_eye[2];
    float lx = hc_at[0] - ex, ly = hc_at[1] - ey, lz = hc_at[2] - ez, n = hc_sqrt(lx * lx + ly * ly + lz * lz);
    lx /= n; ly /= n; lz /= n;
    float rx = -lz, rz = lx; n = hc_sqrt(rx * rx + rz * rz); rx /= n; rz /= n;   // look x up (0, 1, 0)
    float ux = -rz * ly, uy = rz * lx - rx * lz, uz = rx * ly;
    float v[16] = { rx, ux, -lx, 0,  0, uy, -ly, 0,  rz, uz, -lz, 0,
                    -(rx * ex + rz * ez), -(ux * ex + uy * ey + uz * ez), lx * ex + ly * ey + lz * ez, 1 };
    for (int in = 0; in < 2; in++) {        // (as wide as what it fills: all of the picture, 2 : 1; its inner part, ~2.2 : 1)
        float p[16] = { 0 }, *vp = in ? hc_vp_in : hc_vp_all;
        p[0] = in ? HC_COT * (float)(IN_Y1 - IN_Y0) / (float)(IN_X1 - IN_X0) : HC_COT * TRON_H / TRON_W; p[5] = HC_COT;
        p[10] = (HC_NEAR + HC_FAR) / (HC_NEAR - HC_FAR); p[11] = -1.0f; p[14] = 2.0f * HC_NEAR * HC_FAR / (HC_NEAR - HC_FAR);
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) vp[r * 4 + c] = v[r * 4] * p[c] + v[r * 4 + 1] * p[4 + c] + v[r * 4 + 2] * p[8 + c] + v[r * 4 + 3] * p[12 + c];
    }
    RD32(HC_VPORT) = (TRON_W * 2u) << 16 | TRON_H * 2u; RD32(HC_VPORT + 4) = 511u << 16;   // scale, translate: the image
    RD32(HC_VPORT + 8) = (TRON_W * 2u) << 16 | TRON_H * 2u; RD32(HC_VPORT + 12) = 511u << 16;
    RD32(HC_VPORT_IN) = (IN_X1 - IN_X0) / 2u << 16 | (IN_Y1 - IN_Y0) / 2u; RD32(HC_VPORT_IN + 4) = 511u << 16;   // ... the inner part
    RD32(HC_VPORT_IN + 8) = (IN_X1 + IN_X0) / 2u << 16 | (IN_Y1 + IN_Y0) / 2u; RD32(HC_VPORT_IN + 12) = 511u << 16;
}
static void hc_mvp(u32 node, float* m) {   // the node's world x the hard camera's
    for (int r = 0; r < 4; r++) {
        u32 row = node + 0x5Cu + 16u * (u32)r;
        float a0 = F32(row), a1 = F32(row + 4), a2 = F32(row + 8), a3 = F32(row + 12);
        for (int c = 0; c < 4; c++) m[r * 4 + c] = a0 * hc_vp[c] + a1 * hc_vp[4 + c] + a2 * hc_vp[8 + c] + a3 * hc_vp[12 + c];
    }
}
// the picture's inner part made black, then the game's state for drawing the scene (its task's setup, func_80002BB4's)
static u32 inner_begin(u32 o) {
#define EMIT(a, b) do { RD32(o) = (a); RD32(o + 4) = (b); o += 8; } while (0)
    EMIT(0xE7000000u, 0);
    EMIT(0xED000000u | IN_X0 << 12 | IN_Y0, IN_X1 << 12 | IN_Y1);
    EMIT(0xEF300000u, 0);                                              // fill: black (the inner part's own
    EMIT(0xF7000000u, 0x00010001u);                                    //  rectangle, not the scissor's: RT64)
    EMIT(0xF6000000u | (IN_X1 - 4u) << 12 | (IN_Y1 - 4u), IN_X0 << 12 | IN_Y0);
    EMIT(0xE7000000u, 0);
    EMIT(0xDE000000u, 0x01000010u);
    EMIT(0xD7000002u, 0xFFFFFFFFu);
    EMIT(0xD9C0F9FAu, 0);
    EMIT(0xD9FFFFFFu, 0x00200404u);
    EMIT(0xE3000A01u, 0);
    EMIT(0xE3001001u, 0x8000u);
#undef EMIT
    return o;
}
// A Superstar's bone outside the ring is drawn in front of the ring (group 5) or among it, sorted, so the ring can
// hide him (group 0) by the side of the ring he's on: a side facing the camera or not (func_800151A0, the side's
// table 0x80084620 made by func_800231A4 from the ring's edges seen by the frame's camera -- the TV's). For the hard
// camera the sides are its own: else a man outside on the TV's near side was drawn over the ring in the screen's
// picture (the user, 2026-10-28).
#define NM_RING_EDGE_A 0x800528BCu  // the ring's four sides, each from A to B (x, y, z): +x, +z, -z, -x
#define NM_RING_EDGE_B 0x8005288Cu
static u8 hc_side[4];
static void hc_sides(const float* vp) {    // (func_8000214C's: the side's two ends left to right across the picture)
    for (u32 i = 0; i < 4; i++) {
        float sx[2];
        for (u32 e = 0; e < 2; e++) {
            u32 p = (e ? NM_RING_EDGE_B : NM_RING_EDGE_A) + 12u * i;
            float x = F32(p), y = F32(p + 4), z = F32(p + 8);
            float px = vp[0] * x + vp[4] * y + vp[8] * z + vp[12], pw = vp[2] * x + vp[6] * y + vp[10] * z + vp[14] + 1000.0f;
            sx[e] = px / pw;
        }
        hc_side[i] = sx[0] < sx[1] ? 5 : 0;
    }
}
// The arena's own pieces are sided the same way (the barricades, the crowd's near rows, the table: 0 / 5 by the TV's
// side table, func_80025F40 and the venue's): in the screen's picture the table and a barricade on the TV's near side
// were drawn over the crowd behind them (the user, 2026-10-28 evening). Which nodes are sided isn't in one table:
// a node seen both 0 and 5 (the TV's camera going round the ring) is one; it gets the hard camera's side of its
// centre. (Nodes always 0 or always 5 -- the stage, the far crowd -- are left as the game has them.)
#define SIDED_N 512
static u32 sided_node[SIDED_N]; static u8 sided_seen[SIDED_N];   // (1: seen 0, 2: seen 5)
static int sided_find(u32 node, u8 f) {
    u32 i = (node >> 2) * 2654435761u % SIDED_N;
    for (int k = 0; k < 16; k++, i = (i + 1) % SIDED_N) {
        if (sided_node[i] == node) { sided_seen[i] |= f == 5 ? 2 : 1; return sided_seen[i] == 3; }
        if (!sided_node[i]) { sided_node[i] = node; sided_seen[i] = f == 5 ? 2 : 1; return 0; }
    }
    return 0;
}
static int quadrant(float X, float Z) { return (X + Z < 0 ? 2 : 0) | (X - Z < 0 ? 1 : 0); }   // (the side table's order)

// ---- a screen of its own shape (TP_MASK: SmackDown's oval sign) --------------------------------------------------
// The sign is a flat quad whose texture has see-through texels round the oval (or an oval fan whose texture trims its
// edge), and the arena draws it with a render mode that leaves see-through texels out (the stage set's list: combiner
// alpha = TEXEL0, render mode 0x0F0A7008 -- CVG_X_ALPHA, the texture edge). So the screen keeps its shape if our
// picture is see-through where the sign's own picture is: TRON_MASKED is our picture with its alpha taken from the
// sign's own -- cleared to nothing (alpha 0: a 16-bit picture's alpha bit is its coverage's), the panels' own
// pictures laid flat on it as they lie on the screen (panels_draw's way: their materials, vertices, triangles) with
// coverage x texel alpha written full (alpha 1 where the sign is, nothing where it's see-through), then our picture
// drawn over all of it keeping that alpha (CVG_DST_SAVE). The masked panels show TRON_MASKED (panel_img).
static int masked_alive(void) {
    for (int i = 0; i < n_patch; i++) if (panel_masked(i) && panel_alive(i)) return 1;
    return 0;
}
#define MASK_BYTES 0x300u           // (room a list keeps for it: the setup, a panel's commands, the copy)
static u32 mask_compose(u32 o, u32 end) {
    if (!masked_alive() || o + 8u * 40u > end) return o;
    u32 line = (TRON_W * 2u + 7u) / 8u;
#define EMIT(a, b) do { RD32(o) = (a); RD32(o + 4) = (b); o += 8; } while (0)
    EMIT(0xE7000000u, 0);
    EMIT(0xFF100000u | (TRON_W - 1), PHYS(TRON_MASKED));
    EMIT(0xED000000u, ((u32)TRON_W << 14) | ((u32)TRON_H << 2));
    EMIT(0xEF300000u, 0);                                              // fill: nothing (black, alpha 0)
    EMIT(0xF7000000u, 0);
    EMIT(0xF6000000u | ((TRON_W - 1) * 4u << 12) | ((TRON_H - 1) * 4u), 0);
    EMIT(0xE7000000u, 0);
    EMIT(0xDE000000u, 0x01000010u);                                    // the game's setup of a task's list
    EMIT(0xD7000002u, 0xFFFFFFFFu);                                    // ... and func_80002BB4's
    EMIT(0xD9C0F9FAu, 0);
    EMIT(0xD9FFFFFFu, 0x00200404u);
    EMIT(0xE3000A01u, 0);
    EMIT(0xE3001001u, 0x8000u);
    EMIT(0xDC080008u, PHYS(HC_VPORT));                                 // all of the picture
    EMIT(0xED000000u, ((u32)TRON_W << 14) | ((u32)TRON_H << 2));
    // the shape: each masked panel's own picture laid flat, written where its texels aren't see-through
    for (int i = 0; i < n_patch; i++) {
        if (!panel_masked(i) || !panel_alive(i) || o + 8u * (patch[i].n + 32u) > end) continue;
        u32 c = PANEL_COPY + PANEL_COPY_BYTES * (u32)i;
        EMIT(0xE7000000u, 0);
        for (u32 k = 0; k + 1 < patch[i].n; k++) EMIT(patch[i].cmd[2 * k], patch[i].cmd[2 * k + 1]);   // (its palette, texture, tiles)
        EMIT(0xD9FEF9FFu, 0);                                       // no culling (laid flat it's mirrored), no fog
        EMIT(0xE200001Cu, 0x0F0A7200u);                             // coverage x alpha (see-through: not drawn), written full
        EMIT(0xFCFFFFFFu, 0xFFFCF279u);                             // the texel as it is (its alpha)
        EMIT(0xDC38000Eu, PHYS(c + 0x200u));                        // laid flat
        EMIT(0xDB0C0000u, 0x00010000u);
        EMIT(0x01000000u | (patch[i].nv << 12) | ((patch[i].v0 + patch[i].nv) << 1), PHYS(c));
        EMIT(0xDE000000u, patch[i].geo);
    }
    // our picture over it, the alpha kept
    EMIT(0xE7000000u, 0);
    EMIT(0xEF000CF0u, 0x0F0A4300u);                                    // 1 cycle, no palette, point; opaque, CVG_DST_SAVE
    EMIT(0xFCFFFFFFu, 0xFFFCF279u);
    EMIT(0xFD100000u | ((u32)TRON_W - 1), PHYS(TRON_IMG));
    EMIT(0xF5100000u | (line << 9), 0x07000000u);
    EMIT(0xE6000000u, 0);
    EMIT(0xF4000000u, 0x07000000u | ((u32)(TRON_W - 1) << 14) | ((u32)(TRON_H - 1) << 2));
    EMIT(0xE7000000u, 0);
    EMIT(0xF5100000u | (line << 9), 0x00080200u);                       // tile 0: RGBA16, clamped
    EMIT(0xF2000000u, ((u32)(TRON_W - 1) * 4u << 12) | ((u32)(TRON_H - 1) * 4u));
    EMIT(0xE4000000u | ((u32)TRON_W * 4u) << 12 | ((u32)TRON_H * 4u), 0);   // texel for pixel
    EMIT(0xE1000000u, 0);
    EMIT(0xF1000000u, 0x04000400u);
    EMIT(0xE7000000u, 0);
#undef EMIT
    dbg_tron[14]++;
    return o;
}
static int tron_mode(void);
extern u32 ann_zdl_orig(u32 dl);
extern u32 hd_zdl_orig(u32 dl);
// the graphics thread takes the frame (its matrices' slot): the hard camera's list for it
void tron_hardcam_build(s32 slot) {
    hc_slot = -1;
    tron_hold_anim();
    if (slot < 0 || slot > 1 || tron_mode() != 1 || !(n_patch || dbg_tron_any)) return;
    if (!hc_made) { hc_camera(); hc_made = 1; }
    int framed = tron_framed;
    hc_vp = framed ? hc_vp_in : hc_vp_all;
    hc_sides(hc_vp);
    plat_view_tron((u32)hc_vp, (u32)hc_item_of);
    union { u32 w[2]; double d; } kd; kd.w[0] = RD32(NM_CULL_K); kd.w[1] = RD32(NM_CULL_K + 4);
    float K = (float)kd.d;
    // the items, culled and keyed for the hard camera, far to near (equal keys: as they were)
    s32 n = RDS16(NM_ITEM_N), m = 0;
    for (s32 i = 0; i < n && m < HC_ITEMS; i++) {
        u32 it = RD32(NM_ITEMS + 4u * (u32)i);
        if (!GAME_PTR(it)) continue;
        u32 node = RD32(it + 4), kind = RD32(it);
        if (!GAME_PTR(node) || kind > 1) continue;
        u8 f = RD8(node + 0x18);
        if ((f & 0x80) || RD8(node + 0x19)) continue;           // (hidden; the HUD's)
        extern int ann_free; extern u32 announce_owner(u32 node);
        if (kind == 1 && (f == 0 || f == 5) && ((node >= SIXA_80091CC0 && node < SIXA_80091CC0 + 0x1810u * NM_SLOTS_CAP) ||
                                                (ann_free && announce_owner(node) - 1u < 2u))) {   // (JR, King on their feet)
            float X = F32(node + 0x5C + 48), Z = F32(node + 0x5C + 56);   // (a Superstar's bone outside the ring:
            f = hc_side[quadrant(X, Z)];                                    //  the hard camera's side of it)
        } else if ((f == 0 || f == 5) && sided_find(node, f)) {         // (an arena piece the TV's camera sides)
            float cx = F32(it + 0xC), cy = F32(it + 0x10), cz = F32(it + 0x14), M[16];
            for (u32 r = 0; r < 16; r++) M[r] = F32(node + 0x5C + 4u * r);
            float X = cx * M[0] + cy * M[4] + cz * M[8] + M[12], Z = cx * M[2] + cy * M[6] + cz * M[10] + M[14];
            f = hc_side[quadrant(X, Z)];
            dbg_tron[12]++;
        }
        float mv[16]; hc_mvp(node, mv);
        float cx = F32(it + 0xC), cy = F32(it + 0x10), cz = F32(it + 0x14);
        float x = cx * mv[0] + cy * mv[4] + cz * mv[8] + mv[12];
        float y = cx * mv[1] + cy * mv[5] + cz * mv[9] + mv[13];
        float z = cx * mv[2] + cy * mv[6] + cz * mv[10] + mv[14];
        if (kind == 1) {
            float w = z + 150.0f;
            if (w <= 0 || x > K * w || -x > K * w || y > K * w || -y > K * w) continue;
        }
        float key = (F32(it + 0x24) + z) * 1024.0f + hc_sqrt(x * x + y * y);
        int k = m++;
        while (k > 0 && hc_it[k - 1].key < key) { hc_it[k] = hc_it[k - 1]; k--; }
        hc_it[k].item = it; hc_it[k].key = key; hc_it[k].kind = (u8)kind;
        hc_it[k].group = kind == 1 ? RD8(NM_ITEM_GROUP + f) : f;
    }
    // the list: the picture cleared, the game's state for a scene, the hard camera's viewport; the groups
    u32 o = HC_DL + HC_DL_BYTES * (u32)slot, start = o, end = o + HC_DL_BYTES - 64 - (masked_alive() ? MASK_BYTES : 0u);
    u32 mtx = HC_MTX + HC_MTX_N * 64u * (u32)slot, nm = 0;
#define EMIT(a, b) do { RD32(o) = (a); RD32(o + 4) = (b); o += 8; } while (0)
    EMIT(0xE7000000u, 0);
    EMIT(0xFF100000u | (TRON_W - 1), PHYS(TRON_IMG));
    EMIT(0xED000000u, ((u32)TRON_W << 14) | ((u32)TRON_H << 2));
    EMIT(0xEF300000u, 0);                                              // fill: black
    EMIT(0xF7000000u, 0x00010001u);
    EMIT(0xF6000000u | ((TRON_W - 1) * 4u << 12) | ((TRON_H - 1) * 4u), 0);
    EMIT(0xE7000000u, 0);
    EMIT(0xDE000000u, 0x01000010u);                                    // the game's setup of a task's list
    EMIT(0xD7000002u, 0xFFFFFFFFu);                                    // ... and func_80002BB4's
    EMIT(0xD9C0F9FAu, 0);
    EMIT(0xD9FFFFFFu, 0x00200404u);
    EMIT(0xE3000A01u, 0);
    EMIT(0xE3001001u, 0x8000u);
    EMIT(0xDC080008u, PHYS(HC_VPORT));                                 // the screen's picture
    EMIT(0xED000000u, ((u32)TRON_W << 14) | ((u32)TRON_H << 2));
    if (framed) {                                                      // (the far screen: its frame, ours inside)
        o = panels_draw(o, end, 0);                                    // ... the arena's own (its frame's truss)
        o = inner_begin(o);                                            // the inner part: black, the scene's state
        EMIT(0xDC080008u, PHYS(HC_VPORT_IN));
        EMIT(0xED000000u | IN_X0 << 12 | IN_Y0, IN_X1 << 12 | IN_Y1);
    }
    u32 groups = 0;
    for (int p = 0; p < 8; p++)
        for (int k = 0; k < m; k++) {
            if (hc_it[k].group != hc_passes[p][0] || hc_it[k].kind != hc_passes[p][1]) continue;
            u32 h = hc_it[k].item;
            // its items: each one's matrix forced, its vertices loaded (func_800028B4's)
            for (u32 it = h; GAME_PTR(it); it = RD32(it + 8)) {
                if (nm >= HC_MTX_N || o >= end) goto full;
                hc_item_of[slot][nm] = it;
                u32 a = mtx + 64u * nm++;
                float mv[16]; hc_mvp(RD32(it + 4), mv); mtx_fixed(mv, a);
                EMIT(0xDC38000Eu, PHYS(a));
                EMIT(0xDB0C0000u, 0x00010000u);
                u32 v = RD32(it + 0x30);
                if (!v) continue;
                u32 va = PHYS(v), cnt = (u32)RDS16(it + 0x2C), first = (u32)RDS16(it + 0x2E);
                while (cnt > 32u && o < end) { first += 32u; cnt -= 32u; EMIT(0x01020000u | ((first & 0x7Fu) << 1), va); va += 0x200u; }
                EMIT(0x01000000u | ((cnt & 0xFFu) << 12) | (((first + cnt) & 0x7Fu) << 1), va);
            }
            // their triangles (in the venue's light)
            for (u32 it = h; GAME_PTR(it); it = RD32(it + 8)) {
                if (o >= end) goto full;
                u32 node = RD32(it + 4), dl = hd_zdl_orig(ann_zdl_orig(RD32(it + 0x34)));   // (JR's, King's: the game's own)
                if (hc_it[k].kind == 0 && RD32(node + 0x10)) dl = RD32(node + 0x10);   // (func_800025B0's)
                if (!dl) continue;
                if (RD8(node + 0x19) == 0) {
                    u32 c = (RD8(node + 0x1A) & 0xFu) * 4u + (u32)RDS16(NM_LIGHT_SET) * 16u;
                    EMIT(0xFA000000u, RD32(0x80062B68u + c));
                    EMIT(0xFB000000u, RD32(0x80062B88u + c));
                }
                EMIT(0xDE000000u, PHYS(dl));
            }
            groups++;
        }
    if (0) {
full:   dbg_tron[10]++;                                                // (tests: the list or the matrices ran out)
    }
    o = mask_compose(o, start + HC_DL_BYTES - 64);                      // (a screen of its own shape: its picture)
    EMIT(0xE7000000u, 0);
    hc_pic_word = o + 4;
    EMIT(0xFF1001DFu, 0);                                              // the frame's picture (put in as it's called)
    EMIT(0xDE000000u, 0x01000010u);                                    // the game's viewport, scissor, modes
    EMIT(0xDF000000u, 0);
#undef EMIT
    hc_slot = slot;
    dbg_tron[2] = (u32)m; dbg_tron[3] = nm; dbg_tron[4] = groups; dbg_tron[5] = o - start;
    if (o - start > dbg_tron[8]) dbg_tron[8] = o - start;                // (tests: the most)
    if (nm > dbg_tron[9]) dbg_tron[9] = nm;
}
// called from the task's list where the frame's scene ends
static void hardcam_pass(u32 gfxp) {
    if (hc_slot < 0 || hc_slot != layer_slot || !frame_pic) return;
    RD32(hc_pic_word) = PHYS(frame_pic);
    u32 g = RD32(gfxp);
    RD32(g) = 0xDE000000u; RD32(g + 4) = PHYS(HC_DL + HC_DL_BYTES * (u32)hc_slot);
    RD32(gfxp) = g + 8;
    hc_slot = -1;
    img_passes++;
    dbg_tron[0]++;
}

// The entrances' video: the arena's picture of the screen, then the video's frame (64 x 64 CI8) in its
// inner part -- called from the task's list where the frame's scene ends, as the hard camera's
#define VID_DL 0x806BC000u          // (two: this frame's, the last's; VID_DL_BYTES each)
#define VID_DL_BYTES 0x1000u
static int vid_buf;
static void video_pass(u32 gfxp) {
    if (!frame_pic) return;
    vid_buf ^= 1;
    u32 o = VID_DL + VID_DL_BYTES * (u32)vid_buf, start = o, end = o + VID_DL_BYTES - 256;
#define EMIT(a, b) do { RD32(o) = (a); RD32(o + 4) = (b); o += 8; } while (0)
    EMIT(0xE7000000u, 0);
    EMIT(0xFF100000u | (TRON_W - 1), PHYS(TRON_IMG));
    EMIT(0xED000000u, ((u32)TRON_W << 14) | ((u32)TRON_H << 2));
    EMIT(0xEF300000u, 0);
    EMIT(0xF7000000u, 0x00010001u);
    EMIT(0xF6000000u | ((TRON_W - 1) * 4u << 12) | ((TRON_H - 1) * 4u), 0);
    EMIT(0xE7000000u, 0);
    EMIT(0xDE000000u, 0x01000010u);
    EMIT(0xD7000002u, 0xFFFFFFFFu);
    EMIT(0xD9C0F9FAu, 0);
    EMIT(0xD9FFFFFFu, 0x00200404u);
    EMIT(0xE3000A01u, 0);
    EMIT(0xE3001001u, 0x8000u);
    if (!hc_made) { hc_camera(); hc_made = 1; }                        // (the viewports)
    EMIT(0xDC080008u, PHYS(HC_VPORT));
    EMIT(0xED000000u, ((u32)TRON_W << 14) | ((u32)TRON_H << 2));
    int framed = tron_framed;
    u32 x0 = framed ? IN_X0 : 0, x1 = framed ? IN_X1 : (u32)TRON_W * 4u, y0 = framed ? IN_Y0 : 0, y1 = framed ? IN_Y1 : (u32)TRON_H * 4u;
    if (!vid_ready) o = panels_draw(o, end, 1);                         // (no frame of it yet: the arena's own picture)
    else if (framed) { o = panels_draw(o, end, 0); o = inner_begin(o); }
    if (vid_ready) {
        // the video's frame onto the inner part as it is: 64 x 64 CI8 through its palette (TMEM's upper half), in
        // two halves of 64 x 32 (2 KB, the lower half's room) -- RT64 draws it at its own resolution, filtered.
        // (Until 2026-10-28 the CPU made it 64 x 32, every other row dropped: "horrible", the user.)
        u32 fr = vid_buf_at(vid_front), pal = fr + 16u, tex = fr + 16u + 512u, ym = (y0 + y1) / 2u;
        EMIT(0xE7000000u, 0);
        EMIT(0xEF00ACF0u, 0x0F0A4000u);                                // 1 cycle, a palette (RGBA16), bilinear; opaque
        EMIT(0xFCFFFFFFu, 0xFFFCF279u);                                // the texel as it is
        EMIT(0xFD100000u, PHYS(pal));                                  // the palette: 256 colours into TMEM 0x800
        EMIT(0xE8000000u, 0);
        EMIT(0xF5000100u, 0x07000000u);
        EMIT(0xE6000000u, 0);
        EMIT(0xF0000000u, 0x073FC000u);
        EMIT(0xE7000000u, 0);
        for (u32 h = 0; h < 2; h++) {
            u32 ya = h ? ym : y0, yb = h ? y1 : ym;
            EMIT(0xFD48003Fu, PHYS(tex + h * 2048u));                  // its half: CI8, 64 wide
            EMIT(0xF5481000u, 0x07000000u);
            EMIT(0xE6000000u, 0);
            EMIT(0xF4000000u, 0x07000000u | (63u << 14) | (31u << 2));
            EMIT(0xE7000000u, 0);
            EMIT(0xF5481000u, 0x00080200u);                            // render tile 0: CI8, palette 0, clamped
            EMIT(0xF2000000u, (63u * 4u) << 12 | (31u * 4u));
            EMIT(0xE4000000u | x1 << 12 | yb, x0 << 12 | ya);
            EMIT(0xE1000000u, 0);
            EMIT(0xF1000000u, (64u * 4096u / (x1 - x0)) << 16 | (32u * 4096u / (yb - ya)));   // (5.10)
        }
    }
    o = mask_compose(o, start + VID_DL_BYTES - 64);                     // (a screen of its own shape: its picture)
    EMIT(0xE7000000u, 0);
    EMIT(0xFF1001DFu, PHYS(frame_pic));                                // the frame's picture back
    EMIT(0xDE000000u, 0x01000010u);                                    // the game's viewport, scissor, modes
    EMIT(0xDF000000u, 0);
#undef EMIT
    (void)start;
    u32 g = RD32(gfxp);
    RD32(g) = 0xDE000000u; RD32(g + 4) = PHYS(VID_DL + VID_DL_BYTES * (u32)vid_buf);
    RD32(gfxp) = g + 8;
    img_passes++;
    dbg_tron[11]++;
}
void tron_scene_end(u32 gfxp) {
    if (!GAME_PTR(gfxp)) return;
    u32 end = RD32(gfxp);
    dbg_tron[1]++;
    int m = tron_mode();
    dbg_tron[6] = frame_pic; dbg_tron[7] = (u32)m | (u32)n_patch << 8 | (u32)layer_first << 16 | (u32)last_first_was << 24;   // (tests)
    if (m == TM_NONE) { if (n_patch) unpatch_all(); seen_n = 0; scene_head = 0; img_passes = 0; img_ready = 0; return; }
    if (layer_first == 0) {
        if (img_passes > 0) img_ready = 1;                             // (drawn in a frame before this one)
        panels_show(TRON_IMG);
        seen_last = seen_mask; seen_mask = 0;
        if (seen_last) {
            int far = 0;
            for (int i = 0; i < n_patch; i++) if ((seen_last >> i & 1) && patch[i].framed) far = 1;
            tron_framed = far;
        }
    }
    panels_find(end);
    // the scene's last run -- the one reaching the groups' end (a frame's runs go from group 0 until one does): the hard
    // camera's picture, for the screen (shown the next frame)
    // [tronflicker 2026-11-21] (it was the run starting where the LAST frame's last run started. Any item seen or culled
    // before that run -- the TV camera turning, a man stepping out to the near side -- moves where the last run starts,
    // so the guess missed: no camera pass that frame. Over 6 frames in a row and the renderer shows the screen's video
    // memory, which nothing writes in the native picture: the screen went black. See NOTES.md, 2026-11-21 tronflicker)
    const int last_run = layer_next >= layer_n;
    if (m == TM_LIVE && last_run) hardcam_pass(gfxp);
    if (m == TM_VIDEO && last_run) video_pass(gfxp);
    scene_head = 0;
}
