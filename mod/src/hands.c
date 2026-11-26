// The hands: func_80016D2C, every frame, in C -- No Mercy's for its models, WrestleMania 2000's for its.
//
// A model's hands (parts 17 18 9 10, palm and fingers of each) are drawn in the pose its animation names
// (0x80076588[slot], 0-19): per hand piece a table gives the pose's mesh, and func_8000DEC4 copies that
// mesh's vertices into the piece and draws it with the model's own hand picture (func_80017190 keeps a
// copy of each). The meshes are the game's, loaded once (func_8001692C), shared by every model; pose 0
// of No Mercy's is the model's own hand. WrestleMania 2000 has its own meshes (bigger hands: its models'
// hand pictures are 64 x 32, No Mercy's 32 x 16), tables and hand kinds (its 80016F3C loads them,
// 800173AC every frame, 8001770C keeps the pictures): a model of it gets those (loaded from its files
// with No Mercy's own loaders, the first time one is made).
#include "arcade.h"
#include "gamefn.h"
#include "skel.h"
#include "wmport_files.h"

GAMEFN(void, func_8000DEC4, 0x8000DEC4u, (u32 state, u32 bone, u32 src, u32 dst));
GAMEFN(void, func_8000E014, 0x8000E014u, (u32 bone, u32 part, u32 joint));
GAMEFN(void, func_8000CAF8, 0x8000CAF8u, (u32 part, s32 tex, s32 pal, s32 at, s32 n, s32 shift, s32 z));
GAMEFN(void, func_8001089C, 0x8001089Cu, (u32 pool, u32 vertices, u32 commands));
GAMEFN(void, func_8000D514, 0x8000D514u, (u32 entry, u32 pool, s32 file));
GAMEFN(void, func_8000D688, 0x8000D688u, (u32 entry, s32 tex, s32 pal));

#define BONES(slot) (SIXA_80091CC0 + (u32)(slot) * 0x1810u)
#define PARTS(slot) (SIXA_800A4050 + (u32)(slot) * 0x5B0u)
#define DESC(slot)  (SIXA_800A8980 + (u32)(slot) * 0x270u)
#define POSE(slot)  (SIXA_80076588 + (u32)(slot))            // s8: the hands' pose (the animation's)
#define SHOWN(slot) (SIXA_80076590 + (u32)(slot) * 4u)       // s8[4]: each piece's pose drawn (0xFF: none yet)
#define DRAW(slot)  (SIXA_800765A0 + (u32)(slot) * 0x90u)    // [4] x 0x24: each piece's vertex buffers, lists
#define PICS(slot)  (SIXA_800767E0 + (u32)(slot) * 0x50u)    // [4] x 0x14: each piece's picture (kept)
#define JOINTS(slot) (SIXA_80076BC0 + (u32)(slot) * 0x40u)   // [4] x 0x10: each piece's own mesh (skel.c)
#define KIND(slot)  (SIXA_80070D0A + (u32)(slot) * 2u)       // s16: the wrestler's hand kind
#define BONE(k) (BONES(slot) + (u32)(k) * 0xDCu)
#define PART(k) (PARTS(slot) + (u32)(k) * 0x34u)

// ---- WrestleMania 2000's: loaded the first time one of its models is ---------------------------------
static u32 wmh_ready;
#define AL __attribute__((aligned(8)))
static u32 wmh_pool[4] AL;                                  // (func_8001089C's: vertices, commands)
static u8 wmh_odd[2][11][16] AL, wmh_even[2][3][4][16] AL;  // the pose meshes (func_8000D514's entries)
static u8 wmh_pics[NM_SLOTS_CAP][4][0x14] AL;               // each slot's hand pictures (64 x 32)
static u8 wmh_pics_ready[NM_SLOTS_CAP];                     // ... loaded for that slot (wm_pics_load)
static u8 wmh_special[4][0x14] AL;                          // pose 0x13's pictures
// WM2000's tables. Which pose mesh each hand piece takes in each of the 20 poses is WrestleMania 2000's own table
// (its RAM 0x80041CD0..0x80041D20, ROM 0x428D0): wmport_files.h's wmh_pose[piece][pose], made from the player's
// WrestleMania 2000 ROM (the importer: ri_wmport; the developer's gen/wmport.py) -- not written here (handsrom 2026-11-25,
// the user: our source holds none of a game's data). Without that ROM it is zero and never read (wm_hands_there).
static const s8 wm_side[4] = { 1, 1, 0, 0 }, wm_bone[4] = { 0x11, 0x12, 9, 10 }, wm_last[4] = { 3, 10, 3, 10 };

// One slot's four hand pictures, loaded the first time that slot needs them. (2026-11-15, gameplayfix: they used to be
// loaded only for the slots the first match with such a model had -- six at least -- so a later Royal Rumble with more in
// the ring at once put one in slot 6 or past it, whose pictures were never loaded: wm_keep copied its hand palette to
// address 0 and the game crashed -- crash reports 2026-11-15 00:20 and 12:00 (20 and 10 in the ring; slots 8 and 6 held
// Revenge imports with WrestleMania 2000's hands, SKEL_WM_REV without SKEL_WM_NMHANDS).)
static void wm_pics_load(u32 slot) {
    if (slot >= NM_SLOTS_CAP || wmh_pics_ready[slot]) return;
    wmh_pics_ready[slot] = 1;
    for (u32 i = 0; i < 4; i++) func_8000D688((u32)wmh_pics[slot][i], wmh_pic_files[i][0], wmh_pic_files[i][1]);
}

void hands_wm_load(void) {
    if (wmh_ready) return;
    wmh_ready = 1;
    for (u32 i = 0; i < 4; i++) wmh_pool[i] = 0;              // (func_8001089C frees what one had)
    func_8001089C((u32)wmh_pool, 0x778, 0x3A0);
    for (u32 s = 0; s < 2; s++)
        for (u32 p = 0; p < 11; p++) func_8000D514((u32)wmh_odd[s][p], (u32)wmh_pool, wmh_odd_files[s][p]);
    for (u32 s = 0; s < 2; s++)
        for (u32 t = 0; t < 3; t++)
            for (u32 p = 0; p < 4; p++) func_8000D514((u32)wmh_even[s][t][p], (u32)wmh_pool, wmh_even_files[s][t][p]);
    // (the slots of the match now, in the order they always were; a slot past them gets its own when it needs them)
    for (u32 slot = 0; slot < (u32)nm_slot_span(); slot++) wm_pics_load(slot);
    for (u32 i = 0; i < 4; i++) func_8000D688((u32)wmh_special[i], wmh_special_files[i][0], wmh_special_files[i][1]);
}

// func_80017190(slot) for a WM2000 model (its 8001770C): its hand pictures kept, the pose drawn none yet.
// (The model is loaded on the loader's thread; the hands' files are loaded and its pictures kept on the
// match's, the next frame: hands_frame.)
static u8 wmh_keep[NM_SLOTS_CAP], wmh_kept[NM_SLOTS_CAP];
void hands_wm_keep(s32 slot) { if (slot >= 0 && slot < NM_SLOTS_CAP) { wmh_keep[slot] = 1; wmh_kept[slot] = 0; } }
static int wm_keep(s32 slot) {
    if (!RD32(0x800484ECu)) return 0;
    wm_pics_load((u32)slot);
    // (a picture with nowhere to keep it -- the loader out of room -- leaves the hands as they are, never a copy to 0)
    for (u32 i = 0; i < 4; i++) {
        u32 pic = (u32)wmh_pics[slot][i];
        if (!GAME_PTR(RD32(pic + 0xC)) || !GAME_PTR(RD32(pic + 0x10))) return 0;
    }
    for (u32 i = 0; i < 4; i++) {
        s32 k = RDS8(0x800484E0u + i);
        u32 pic = (u32)wmh_pics[slot][i];
        func_80036710(RD32(PART(k) + 0x20), RD32(pic + 0xC), RD32(pic + 4));
        func_80036710(RD32(PART(k) + 0x24), RD32(pic + 0x10), 0x200);
        RD8(POSE(slot)) = 0;
        RD8(SHOWN(slot) + i) = 0xFF;
    }
    return 1;
}

static void hands_wm(s32 slot) {
    u32 desc = DESC(slot);
    if (RD32(desc + SKEL_WM_FLAGS) & 4) return;
    s32 pose = RDS8(POSE(slot));
    s32 kind = RDS8(desc + SKEL_WM_HANDS);
    for (u32 i = 0; i < 4; i++) {
        u32 shown = SHOWN(slot) + i;
        if (RDS8(shown) == wm_last[i]) continue;
        s32 v = (pose >= 0 && pose < 20) ? wmh_pose[i][pose] : 0;
        if (v == RDS8(shown)) continue;
        RD8(shown) = (u8)v;
        u32 src = (i & 1) ? (u32)wmh_odd[wm_side[i]][v] : (u32)wmh_even[wm_side[i]][kind][v];
        u32 dst = pose == 0x13 ? (u32)wmh_special[i] : (u32)wmh_pics[slot][i];
        func_8000DEC4(DRAW(slot) + i * 0x24u, BONE(wm_bone[i]), src, dst);
    }
    if (pose == 0x13) func_8000CAF8(PART(16), wmh_decal_files[0], wmh_decal_files[1], 0xE0, 0x20, 0, 0);
}

// Whether WrestleMania 2000's hands are in this copy of the mod (2026-11-19, nmplus). Their meshes and pictures are its files
// in the wmport pack, named by wmport_files.h's tables -- an extras hole, filled only when the first start built the extras
// from the player's WrestleMania 2000 ROM. Without them, a model with those hands (a WrestleMania 2000 or Revenge import's
// folder model: no SKEL_WM_NMHANDS) keeps the hands it was modelled with, unposed: the tables would name file 0, and the
// game's answer to a file that isn't there is to stop for good (files.c files_info) -- the Superstar Select froze the moment
// AKI Man (a Revenge import) was under the cursor, with Revenge + WM2000 + Redux and no No Mercy Plus (no extras at all).
// (The tables are read through a volatile: the compiler knows the build's values and would fold the test away.)
static int wm_hands_there(void) {
    return arcade_extras_has(EXTRAS_WM2000) && *(volatile const u16*)&wmh_odd_files[0][0] != 0 &&
           *(volatile const u16*)&wmh_pic_files[0][0] != 0;
}
// A Revenge model made without WrestleMania 2000 (2026-11-19, c2hands; the user: "Revenge uses its own hands when WM2000 is
// missing"): its hands' pictures are Revenge's own -- the palms as Revenge draws them, not turned round for WM2000's meshes,
// the fingers Revenge's own picture, not WM2000's in his colours (the importer: ri_revimport rev_build_model, ri_wmport
// rev_port_desc) -- so it is drawn with its own hand meshes as modelled, never WM2000's pose meshes, even when a WrestleMania
// 2000 ROM is added later (an imported folder is made once).
static int own_hands(u32 desc) {
    return (RD32(desc + SKEL_WM_FLAGS) & (SKEL_WM_REV | SKEL_WM_OWNHANDS)) == (SKEL_WM_REV | SKEL_WM_OWNHANDS);
}

// func_80016D2C()
void hands_frame(void) {
    if (!RD32(0x800484ECu)) return;
    for (s32 slot = 0; slot < NM_SLOTS_CAP; slot++) {
        if (!wmh_keep[slot]) continue;
        wmh_keep[slot] = 0;
        if (skel_origin(DESC(slot)) != SKEL_WM || (RD32(DESC(slot) + SKEL_WM_FLAGS) & SKEL_WM_NMHANDS)) continue;
        if (own_hands(DESC(slot)) || !wm_hands_there()) { wmh_kept[slot] = 0; continue; }   // (his own hands, as modelled: above)
        hands_wm_load();
        wmh_kept[slot] = (u8)wm_keep(slot);
    }
    for (s32 slot = 0; slot < 4; slot++) {
        u32 desc = DESC(slot);
        int nmh = 0;   // a WM2000 / Revenge model with No Mercy's hands: drawn as No Mercy's (their normal kind)
        if (skel_origin(desc) == SKEL_WM) {
            if (!(RD32(desc + SKEL_WM_FLAGS) & SKEL_WM_NMHANDS)) { if (wmh_kept[slot]) hands_wm(slot); continue; }
            nmh = 1;
        }
        if (RD8(desc + 0x254) & 8) continue;
        s32 pose = RDS8(POSE(slot));
        for (u32 i = 0; i < 4; i++) {
            u32 shown = SHOWN(slot) + i;
            s32 v = RDS8(RD32(0x800484CCu + i * 4) + (u32)pose);
            if (v == RDS8(shown)) continue;
            RD8(shown) = (u8)v;
            s32 k = RDS8(0x800484E0u + i);
            if (!v) {
                func_8000E014(BONE(k), PART(k), JOINTS(slot) + i * 0x10u);
                continue;
            }
            s32 side = RDS8(0x800484DCu + i);
            u32 src;
            if (i & 1) src = 0x80076A50u + (u32)(side * 0xB0) + (u32)(v * 0x10);
            else src = 0x800768D0u + (u32)(side * 0xA0) + (u32)(v * 0x50) + (u32)((nmh ? 0 : RDS8(0x80048470u + RDS16(KIND(slot)))) * 0x10);
            func_8000DEC4(DRAW(slot) + i * 0x24u, BONE(k), src, PICS(slot) + i * 0x14u);
        }
        if (pose != 0x13 || nmh) continue;
        s32 kind = RDS16(SIXA_80076264 + (u32)slot * 2u);
        if (kind != 6 && kind != 8) continue;
        func_8000CAF8(PART(16), 7, 6, 0xC0, 0x20, 0, 0);
        func_8000CAF8(PART(17), 9, 8, 0xC0, 0x20, 0, 0);
        func_8000CAF8(PART(18), 9, 8, 0xC0, 0x20, 0, 0);
        RD8(desc + 0x254) |= 8;
    }
}
