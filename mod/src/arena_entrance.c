// The entrances' close shots in the arcade's arenas (data/arenaport/NM_ENTRANCE.md; gen/arenas_entrance.py).
//
// The close shot (entrance steps 5 / 11: the Superstar on the stage, the camera close) is not the arena: at entrance
// step 1 the entrance overlay builds "other scene" venue + 10 (func_8010B778 -> func_8001B3E4(venue + 10) ->
// func_8001F2D8, later entrances func_80020B58), and for the close shot hides the arena (func_8001E7A4(0)) and shows
// that scene (func_8002173C(1)). Its pieces come from per-scene tables indexed by i = scene - 10 = the venue: the main
// piece list (0x80052280 list A, 0x800522BC list B -- the same for us --, palettes 0x800522F8, count 0x80052334), the
// other groups (extra list 0x80052354 by its pointer, animated pieces 0x80052738 / 0x800527CC, single models 0x80052518,
// more 0x800525B0: their counts), and its place (0x80050690 + 16 i: x, y, z, the turn).
//
// Each arcade arena's close scene is its own (mod/NOTES.md "Arenas restructured", 2026-11-01): those tables in its own
// data block (arena.c), filled here once -- every index 0..9 its own scene, the other groups empty -- and read by the
// patched scene code through arena_close while that arena's close-ups are built (arena.c arena_on_scene). The game's
// tables are never written. (Until 2026-11-01 scene index XARENA_BASE's entries were written over and put back.)
#include "arcade.h"
#include "arena_files.h"
#include "arena_entrance_files.h"

#define O_CLOSE_MESH_A 0x2280u   // ptr[]
#define O_CLOSE_MESH_B 0x22BCu   // ptr[]
#define O_CLOSE_PAL    0x22F8u   // ptr[]: their palettes (the texture: + 1)
#define O_CLOSE_N      0x2334u   // s16[]
#define O_CLOSE_PLACE  0x0690u   // float[4] per scene: x, y, z, the turn

void arena_entrance_fill(u8* b, int k) {         // k: the arcade's arena (0..XARENA_N - 1)
    if (k < 0 || k >= XARENA_N) return;
    for (u32 i = 0; i < 10; i++) {
        *(u32*)(b + O_CLOSE_MESH_A + 4u * i) = (u32)xclose_mesh[k];
        *(u32*)(b + O_CLOSE_MESH_B + 4u * i) = (u32)xclose_mesh[k];
        *(u32*)(b + O_CLOSE_PAL + 4u * i) = (u32)xclose_pal[k];
        *(s16*)(b + O_CLOSE_N + 2u * i) = xclose_n[k];
        for (u32 c = 0; c < 4; c++) *(float*)(b + O_CLOSE_PLACE + 16u * i + 4u * c) = xclose_place[k][c];
    }
    // (the extra list's pointer, the animated / single models' counts, their rates and biases: 0, the block's own zeros)
}
