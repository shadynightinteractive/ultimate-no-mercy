// An arcade arena's own ringside (gen/arenas_ring.py, data/arenaport/RINGSIDE.md; mod/NOTES.md "Arenas restructured",
// 2026-11-01): the ring's mat / logo / aprons / corners, the floor, the barricades, the crowd, the ropes' colour, its
// stage set (brightened), whether it has No Mercy's announce table and steps -- and its own walls.
//
// Each arcade arena's data block (arena.c, the game's arena-data layout: the offsets the game's builders read at
// 0x80050000 + o) is filled here once, from the generated lists: the patched builders (gen/patches.py ARENA_LUIS) read it
// through arena_lists while that arena is built. Nothing of the game's is written: the game's own arenas read their
// own data in place. (Until 2026-11-01 the shared lists were written over for the build and put back, the collision
// records moved in the match overlay and put back: the user's RAW after a Revenge arena, PENDING (bulk).)
//
// The walls: every arena's own (gen/arenas_walls.py, step 4b; arena.c walls_take).
#include "arcade.h"
#include "arena_ring_files.h"

extern int arena_current(void);      // arena.c: 0-9 the game's, 10 + k the arcade's k
#define F32(a) (*(volatile float*)(a))

// ---- the block: the game's arena-data offsets (data/arenaport/NM_ARENAS.md, RINGSIDE.md) -----------------------------
#define O_RING_A      0x040u   // u16 mesh; +4 the cage match's; +8 the palette
#define O_FLOOR       0x064u   // u16 x 2; the cage's x 2 at +4; palettes x 2 at +8
#define O_BARRICADE   0x070u   // u16 x 10; palettes x 10 at 0x084
#define O_BARR_PAL    0x084u
#define O_CORNER      0x0A0u   // u16 x 3; the cage's x 3 at +8; palettes x 3 at +0x10
#define O_STANDS      0x0F8u   // u16 x 15; palettes x 15 at 0x118
#define O_STANDS_PAL  0x118u
#define O_G1          0x1BCu   // u16 x 3;  its two palette lists (the crowd's two frames): ptr x 2 at 0x1D4
#define O_G1_PALS     0x1D4u
#define O_G2          0x1DCu   // u16 x 9;  0x218
#define O_G2_PALS     0x218u
#define O_G3          0x220u   // u16 x 22; 0x2A4
#define O_G3_PALS     0x2A4u
#define O_G4          0x2ACu   // u16 x 22; 0x330
#define O_G4_PALS     0x330u
#define O_UPPER       0x160u   // ptr [v]: meshes; 0x188 [v] palettes; u8 0x1B0 [v] count
#define O_UPPER_PAL   0x188u
#define O_UPPER_N     0x1B0u
#define O_TRUSS       0x780u   // u16 x 5 (palettes per venue: 0x78C[v], arena.c)
#define O_APRON       0x7B4u   // u16 x 8; the cage's x 8 at 0x7C4; palettes ptr [v] 0x7D4
#define O_APRON_CAGE  0x7C4u
#define O_APRON_PAL   0x7D4u
#define O_ROPE_SET    0x2854u  // u8 [v]: the ropes' colour set s; its R G B: 0x2860 / 6C / 78 + 4 s
#define O_ROPE_R      0x2860u
#define BH(b, o) (*(u16*)((b) + (o)))
#define BW(b, o) (*(u32*)((b) + (o)))
static void bhn(u8* b, u32 o, const u16* v, int n) { for (int i = 0; i < n; i++) BH(b, o + 2u * (u32)i) = v[i]; }

void arena_ring_fill(u8* b, int k) {             // k: the arcade's arena (gen/arenas.py ARENAS' order)
    if (k < 0 || k >= XRING_N) return;
    BH(b, O_RING_A) = xring_logo[k][0]; BH(b, O_RING_A + 4u) = xring_logo[k][1]; BH(b, O_RING_A + 8u) = xring_logo[k][2];
    bhn(b, O_APRON, xring_apron_mesh[k], 8); bhn(b, O_APRON_CAGE, xring_apron_mesh_cage[k], 8);
    bhn(b, O_FLOOR, xring_floor_mesh[k], 4); bhn(b, O_FLOOR + 8u, xring_floor_pal[k], 2);
    bhn(b, O_BARRICADE, xring_barricade_mesh[k], 10); bhn(b, O_BARR_PAL, xring_barricade_pal[k], 10);
    bhn(b, O_CORNER, xring_corner_mesh[k], 3); bhn(b, O_CORNER + 8u, xring_corner_mesh[k] + 3, 3); bhn(b, O_CORNER + 0x10u, xring_corner_pal[k], 3);
    bhn(b, O_STANDS, xring_stands_mesh[k], 15); bhn(b, O_STANDS_PAL, xring_stands_pal_a[k], 15);
    bhn(b, O_G1, xring_g1_mesh[k], 3);  BW(b, O_G1_PALS) = (u32)xring_g1_pal_a[k]; BW(b, O_G1_PALS + 4u) = (u32)xring_g1_pal_b[k];
    bhn(b, O_G2, xring_g2_mesh[k], 9);  BW(b, O_G2_PALS) = (u32)xring_g2_pal_a[k]; BW(b, O_G2_PALS + 4u) = (u32)xring_g2_pal_b[k];
    bhn(b, O_G3, xring_g3_mesh[k], 22); BW(b, O_G3_PALS) = (u32)xring_g3_pal_a[k]; BW(b, O_G3_PALS + 4u) = (u32)xring_g3_pal_b[k];
    bhn(b, O_G4, xring_g4_mesh[k], 22); BW(b, O_G4_PALS) = (u32)xring_g4_pal_a[k]; BW(b, O_G4_PALS + 4u) = (u32)xring_g4_pal_b[k];
    bhn(b, O_TRUSS, xring_truss_mesh[k], 5);
    // its per-venue entries: every venue's slot the arena's own (the venue byte, XARENA_BASE, picks nothing of a venue's)
    for (u32 v = 0; v < 10; v++) {
        BW(b, O_APRON_PAL + 4u * v) = (u32)xring_apron_pal[k];
        BW(b, O_UPPER + 4u * v) = (u32)xring_upper_mesh[k]; BW(b, O_UPPER_PAL + 4u * v) = (u32)xring_upper_pal[k];
        b[O_UPPER_N + v] = 1;
        b[O_ROPE_SET + v] = 0;                    // (its colour set 0: its own R G B below)
    }
    for (u32 c = 0; c < 3; c++) for (u32 s = 0; s < 3; s++) for (u32 i = 0; i < 4; i++) b[O_ROPE_R + 12u * c + 4u * s + i] = xring_rope_rgb[k][c][i];
}
int arena_ring_set(int k, u32* mesh, u32* pal) {   // its stage set (the build's: brightened, its floors in the ring lists)
    if (k < 0 || k >= XRING_N) return 0;
    *mesh = (u32)xring_set_mesh[k]; *pal = (u32)xring_set_pal[k];
    return xring_set_n[k];
}
int arena_ring_table(int k) { return k >= 0 && k < XRING_N && xring_table[k]; }

// ---- its walls: gen/arenas_walls.py (src/arena_walls_files.h, arena.c walls_take) -- every arena's own --------------------

// QA (logic.c qa_walls): the walls the match's collision really walks on the ring floor's -z quadrant -- the zone list
// of row 0 / quadrant 2 (the arena's own, or 0x8014D290 + 8: func_8012D58C's), its first span's records from first to
// last (+0x2C next): q[0] = the least z of the -z side's barricade (the record with id 24 or 16: u16, -690 0xFD4E the
// game's, -530 0xFDEE Revenge's) | the least z of the +x side's (id 14: -615 0xFD99, -455 0xFE39) << 16; q[1] = the least
// z of every record on that walk (u16) | records walked << 16; q[2] = arena_current() | 1 << 8 when the walls walked are
// the arena's own (arena.c's arena_zone_q) | the stage set's parts built << 16
static s16 rz(u32 r, int q) { return RDS16(r + (u32)q * 6u + 4u); }
void arena_ring_qa(u32 q) {
    extern u32 arena_zone_q; extern s32 arena_set_built;
    RD32(q) = 0; RD32(q + 4u) = 0;
    RD32(q + 8u) = (u32)arena_current() | (arena_zone_q ? 1u << 8 : 0u) | (u32)(arena_set_built & 0xFFFF) << 16;
    s32 mode = RDS32(NM_MODE);
    if (mode != -2 && mode != -6) return;
    u32 zl = arena_zone_q ? RD32(arena_zone_q + 8u) : RD32(0x8014D290u + 8u);
    if (zl < 0x80000400u || zl >= 0x88000000u) return;          // (the arcade's own walls are in the mod's memory, 0x83..)
    u32 sp = RD32(zl);
    if (sp < 0x80000400u) return;
    u32 r = RD32(sp), last = RD32(sp + 4u);
    int n = 0; s16 zmin = 0x7FFF, z24 = 0x7FFF, z14 = 0x7FFF;
    for (; n < 40 && r >= 0x80000400u; n++) {
        s16 m = 0x7FFF; u16 id = RD16(r + 0x24u);
        for (int c = 0; c < 4; c++) if (rz(r, c) < m) m = rz(r, c);
        if (m < zmin) zmin = m;
        if ((id == 24 || id == 16) && m < z24) z24 = m;
        if (id == 14 && m < z14) z14 = m;
        if (r == last) { n++; break; }
        r = RD32(r + 0x2Cu);
    }
    RD32(q) = (u32)(u16)z24 | (u32)(u16)z14 << 16;
    RD32(q + 4u) = (u32)(u16)zmin | (u32)n << 16;
}
