// The game's files, with more of them: IDs from 0x4C03 (the ROM's table ends at 0x4C02) are the
// arcade's own. The game reads every file through func_80003BD0 (its place and size) and
// func_80003C94 (read it, unpacking it if it's packed); both are replaced here by ones that know the
// new IDs and otherwise do just what the game's do.
//
// Photos: the game has 100 (0-99): a "TEX" file for the Superstar Select (0x45B3 + photo) and a
// picture / palette pair (0x4AAE + 2 photo / 0x4AAD + 2 photo). Photos 100-127 are new files here (FILE_PHOTO_*),
// and photos 128-255 (2026-11-01, ports4: the photo read as an unsigned byte -- it was a signed one) a second
// block (FILE_PHOTO2_*); the game's code that names a photo's files asks for them (gen/patches.py).
#include "arcade.h"
#include "gamefn.h"
#include "bigshow_photo.h"
#include "jarrett_photo.h"
#include "droz_photo.h"
#include "mideon_photo.h"
#include "meanie_photo.h"
#include "photos_nm.h"
#include "rev_photos.h"
#include "wmport_files.h"
#include "rev_move_anims.h"

#define FILES_N       0x4C03u   // the ROM's files are 1..0x4C02
#define FILES_END     0x7FFFu   // ... the arcade's from 0x4C03 (gen/patches.py FILES_END)
#define PHOTO_EXT0    100
#define PHOTO_EXT_N   28
#define FILE_PHOTO_TEX(p)  (0x4C03u + (u32)((p) - PHOTO_EXT0))          // 0x4C03..0x4C1E
#define FILE_PHOTO_TLUT(p) (0x4C1Fu + 2u * (u32)((p) - PHOTO_EXT0))     // 0x4C1F..
#define FILE_PHOTO_CI4(p)  (0x4C20u + 2u * (u32)((p) - PHOTO_EXT0))     // ..0x4C56
// photos 128-255 (ports4, 2026-11-01: a photo read unsigned, gen/patches.py photo_file): a second block
#define PHOTO_EXT2    128
#define PHOTO_EXT2_N  128
#define FILE_PHOTO2_TEX(p)  (0x7C00u + (u32)((p) - PHOTO_EXT2))         // 0x7C00..0x7C7F
#define FILE_PHOTO2_TLUT(p) (0x7C80u + 2u * (u32)((p) - PHOTO_EXT2))    // 0x7C80..
#define FILE_PHOTO2_CI4(p)  (0x7C81u + 2u * (u32)((p) - PHOTO_EXT2))    // ..0x7D7F
#define PHOTO_BIG_SHOW 100
#define PHOTO_JARRETT  101            // (gen/mkparts.py's costumes for him name it)
#define PHOTO_DROZ     102            // (... and for these)
#define PHOTO_MIDEON   103
#define PHOTO_MEANIE   104
// (phase3 2026-11-19) the 18 Revenge ports' photo numbers (ours, as gen/mkparts.py numbers them): the ROM-rendered
// rev_photos.h (tools/romrender/mkportphotos.py, and the first start's native twin) gives only the pictures; the old
// screenshot-made header defined these too
#ifndef PHOTO_BRET
#define PHOTO_BRET         112
#define PHOTO_NEIDHART     113
#define PHOTO_HOGAN        114
#define PHOTO_NASH         115
#define PHOTO_HALL         116
#define PHOTO_SAVAGE       117
#define PHOTO_HENNIG       118
#define PHOTO_GIANT        119
#define PHOTO_STING        120
#define PHOTO_LUGER        121
#define PHOTO_GOLDBERG     122
#define PHOTO_RICKSTEINER  123
#define PHOTO_SCOTTSTEINER 124
#define PHOTO_BOOKER       125
#define PHOTO_STEVIERAY    126
#define PHOTO_REY          127
#define PHOTO_PIPER        128
#define PHOTO_BISCHOFF     129
#endif

// a photo's pictures (64 x 64 CI4 and its 16-color palette) by photo
// (a table, not a switch: the PC port's live recompiler can't size a jump table of a mod's)
typedef struct { int photo; const u8 *tlut, *ci4; } ExtPhoto;
static const ExtPhoto ext_photos[] = {
    { PHOTO_BIG_SHOW, bs_photo_tlut, bs_photo_ci4 }, { PHOTO_JARRETT, jj_photo_tlut, jj_photo_ci4 },
    { PHOTO_DROZ, dz_photo_tlut, dz_photo_ci4 }, { PHOTO_MIDEON, md_photo_tlut, md_photo_ci4 },
    { PHOTO_MEANIE, bm_photo_tlut, bm_photo_ci4 },
    { PHOTO_BRET, bret_photo_tlut, bret_photo_ci4 }, { PHOTO_NEIDHART, neidhart_photo_tlut, neidhart_photo_ci4 },   // (Revenge's)
    { PHOTO_HOGAN, hogan_photo_tlut, hogan_photo_ci4 }, { PHOTO_NASH, nash_photo_tlut, nash_photo_ci4 },
    { PHOTO_HALL, hall_photo_tlut, hall_photo_ci4 },
    { PHOTO_SAVAGE, savage_photo_tlut, savage_photo_ci4 }, { PHOTO_HENNIG, hennig_photo_tlut, hennig_photo_ci4 },
    { PHOTO_GIANT, giant_photo_tlut, giant_photo_ci4 },
    { PHOTO_STING, sting_photo_tlut, sting_photo_ci4 },
    { PHOTO_LUGER, luger_photo_tlut, luger_photo_ci4 },
    { PHOTO_GOLDBERG, goldberg_photo_tlut, goldberg_photo_ci4 },
    { PHOTO_RICKSTEINER, ricksteiner_photo_tlut, ricksteiner_photo_ci4 },
    { PHOTO_SCOTTSTEINER, scottsteiner_photo_tlut, scottsteiner_photo_ci4 },
    { PHOTO_BOOKER, booker_photo_tlut, booker_photo_ci4 },
    { PHOTO_STEVIERAY, stevieray_photo_tlut, stevieray_photo_ci4 },
    { PHOTO_REY, rey_photo_tlut, rey_photo_ci4 },
    { PHOTO_PIPER, piper_photo_tlut, piper_photo_ci4 },
    { PHOTO_BISCHOFF, bischoff_photo_tlut, bischoff_photo_ci4 },
};
// (A folder wrestler's photo: its palette, then its picture. roster.c)
const u8* roster_folder_photo(int photo);
static const u8* photo_tlut(int p) {
    { const u8* mine = roster_folder_photo(p); if (mine) return mine; }
    for (u32 i = 0; i < sizeof ext_photos / sizeof ext_photos[0]; i++) if (ext_photos[i].photo == p) return ext_photos[i].tlut;
    for (int i = 0; i < NM_PHOTOS_N; i++) if (nm_photos[i].photo == p) return nm_photos[i].tlut;
    return 0;
}
static const u8* photo_ci4(int p) {
    { const u8* mine = roster_folder_photo(p); if (mine) return mine + 32; }
    for (u32 i = 0; i < sizeof ext_photos / sizeof ext_photos[0]; i++) if (ext_photos[i].photo == p) return ext_photos[i].ci4;
    for (int i = 0; i < NM_PHOTOS_N; i++) if (nm_photos[i].photo == p) return nm_photos[i].ci4;
    return 0;
}
static const u8 tex_header[16] = { 'T', 'E', 'X', 0, 0x00, 0x40, 0x00, 0x40, 0x04, 0x02, 0x00, 0x10, 0, 0, 0, 0 };
static const u8 ci4_header[8] = { 0x3F, 0x3F, 0x00, 0x20, 0x00, 0x00, 0x06, 0x06 };

// The mod's own files (not an arena folder's, nor a folder wrestler's model's): file -> its size and a writer (0: not one
// of ours). (2026-11-13, filepool: apart from ext_file below, so files_pool_send can ask it which numbers are taken.)
static u32 ext_file_own(u32 file, u32 dest) {
    { u32 n = announce_file(file, dest); if (n) return n; }   // the announcers' chairs (announce.c)
    { extern u32 storylist_file(u32 file, u32 dest); u32 n = storylist_file(file, dest); if (n) return n; }   // Custom Story's labels (logic.c)
    { int p = -1, part = 0;   // (a photo's file: 0 its TEX, 1 its palette, 2 its picture -- photos 100-127, then 128-255)
      if (file >= FILE_PHOTO_TEX(PHOTO_EXT0) && file < FILE_PHOTO_TEX(PHOTO_EXT0 + PHOTO_EXT_N)) p = PHOTO_EXT0 + (int)(file - FILE_PHOTO_TEX(PHOTO_EXT0));
      else if (file >= FILE_PHOTO_TLUT(PHOTO_EXT0) && file < FILE_PHOTO_TLUT(PHOTO_EXT0 + PHOTO_EXT_N)) {
          u32 k = file - FILE_PHOTO_TLUT(PHOTO_EXT0); p = PHOTO_EXT0 + (int)(k >> 1); part = 1 + (int)(k & 1); }
      else if (file >= FILE_PHOTO2_TEX(PHOTO_EXT2) && file < FILE_PHOTO2_TEX(PHOTO_EXT2 + PHOTO_EXT2_N)) p = PHOTO_EXT2 + (int)(file - FILE_PHOTO2_TEX(PHOTO_EXT2));
      else if (file >= FILE_PHOTO2_TLUT(PHOTO_EXT2) && file < FILE_PHOTO2_TLUT(PHOTO_EXT2 + PHOTO_EXT2_N)) {
          u32 k = file - FILE_PHOTO2_TLUT(PHOTO_EXT2); p = PHOTO_EXT2 + (int)(k >> 1); part = 1 + (int)(k & 1); }
      if (p >= 0) {
        if (!photo_tlut(p)) return 0;
        if (part == 0) {
            if (dest) {
                for (u32 i = 0; i < 16; i++) RD8(dest + i) = tex_header[i];
                for (u32 i = 0; i < 32; i++) RD8(dest + 16 + i) = photo_tlut(p)[i];
                for (u32 i = 0; i < 2048; i++) RD8(dest + 48 + i) = photo_ci4(p)[i];
            }
            return 2096;
        }
        if (part == 1) {   // the palette
            if (dest) for (u32 i = 0; i < 32; i++) RD8(dest + i) = photo_tlut(p)[i];
            return 32;
        }
        if (dest) {
            for (u32 i = 0; i < 8; i++) RD8(dest + i) = ci4_header[i];
            for (u32 i = 0; i < 2048; i++) RD8(dest + 8 + i) = photo_ci4(p)[i];
        }
        return 2056;
      }
    }
    if (file >= WMP_FILE0 && file < WMP_FILE0 + WMP_FILES_N) {   // WrestleMania 2000's Superstars' (gen/wmport.py)
        u32 i = file - WMP_FILE0;
        if (dest) plat_wmport_read(wmp_files[i][0], dest, wmp_files[i][1]);
        return wmp_files[i][1];
    }
    return parts_file(file, dest);    // the new body parts' (parts.c)
}

// ---- the numbers no file has, for folder wrestlers' models (2026-11-13, filepool) -------------------------------------
// The exe gives a folder wrestler's model files game numbers only while the game loads that model (exe
// arcade_wrestlers.cpp: "game file numbers, held only while a model loads"), from every number below 0x8000 that no other
// file has. This file map is the one place that knows which those are: the blocks between the files above, each number
// asked of them once more (files_pool_send) so a block can never take a file that's there.
//   Left out: 0x4C03-0x4DFF (photos 100-127, two list labels, No Mercy Plus's body parts -- whose tables name numbers
// that aren't all in the lists here: a palette's picture is its number + 1), the WrestleMania 2000 / Revenge pack, the
// folder arenas' labels (arena.c FOLDER_LABEL_FILE0 0x7000 + FOLDER_ARENAS_MAX 233), photos 128-255 and FILES_END itself.
// The story list's labels and the Rumble captions (logic.c FILE_STORY_LABEL0 0x4E00, 32 + 3) are in the first block and
// taken out by the check.
#define POOL_RANGES_MAX 64
static const u32 pool_blocks[][2] = {
    { 0x4E00u, WMP_FILE0 },
    { WMP_FILE0 + WMP_FILES_N, 0x7000u },
    { 0x7000u + 233u, FILE_PHOTO2_TEX(PHOTO_EXT2) },
    { FILE_PHOTO2_CI4(PHOTO_EXT2 + PHOTO_EXT2_N - 1) + 1u, FILES_END },
};
static int files_in_pool(u32 file) {
    for (u32 b = 0; b < sizeof pool_blocks / sizeof pool_blocks[0]; b++)
        if (file >= pool_blocks[b][0] && file < pool_blocks[b][1]) return 1;
    return 0;
}
static u32 pool_ranges[2 * POOL_RANGES_MAX] __attribute__((aligned(8)));
u32 dbg_pool_taken;           // (numbers of the blocks that a file already has: the check took them out)
extern void plat_wrestler_file_pool(u32 ranges, u32 count);
extern u32 arena_file(u32 file, u32 dest);
// Tells the exe the pool: the blocks, less every number a file of the mod's or an arena folder's has. Once, before the
// first folder model (parts.c).
void files_pool_send(void) {
    u32 n = 0;
    dbg_pool_taken = 0;
    for (u32 b = 0; b < sizeof pool_blocks / sizeof pool_blocks[0]; b++) {
        u32 at = pool_blocks[b][0];
        const u32 end = pool_blocks[b][1];
        while (at < end) {
            while (at < end && (ext_file_own(at, 0) || arena_file(at, 0))) { at++; dbg_pool_taken++; }
            u32 to = at;
            while (to < end && !ext_file_own(to, 0) && !arena_file(to, 0)) to++;
            if (to > at && n < POOL_RANGES_MAX) { pool_ranges[2 * n] = at; pool_ranges[2 * n + 1] = to; n++; }
            at = to;
        }
    }
    plat_wrestler_file_pool((u32)pool_ranges, n);
}

// file -> its size and a writer (0: not one of ours). A number of the pool is a folder wrestler's model's while the game
// loads it, before anything else (2026-11-13, filepool: an arena folder's files.bin could otherwise name one).
static u32 ext_file(u32 file, u32 dest) {
    if (files_in_pool(file)) {
        extern u32 plat_wrestler_file(u32 file, u32 dest);
        u32 n = plat_wrestler_file(file, dest);
        if (n) return n;
    }
    { u32 n = arena_file(file, dest); if (n) return n; }   // an arena folder's (arena.c)
    return ext_file_own(file, dest);
}

// (2026-11-12, handlers) One of the added files by number, for parts.c's QA twin check: its size, and the file at dest.
u32 files_ext_read(u32 file, u32 dest) { return file >= FILES_N ? ext_file(file & 0xFFFFu, dest) : 0; }

// func_80003BD0(info, file): { ROM start, ROM end, size, packed size (0: not packed) }
void files_info(u32 info, u32 file) {
    file &= 0xFFFFu;
    if (file >= FILES_N) {
        u32 n = ext_file(file, 0);
        RD32(info) = 0; RD32(info + 4) = 0; RD32(info + 8) = n; RD32(info + 12) = 0;
        if (RMA_FILES_N && file >= RMA_FIRST && file < RMA_FIRST + RMA_FILES_N) {   // (an animation: played
            RD32(info) = ANIM_ROM + wmp_files[file - WMP_FILE0][0];                 // from its "ROM" place,
            RD32(info + 4) = RD32(info) + n;                                         // movex.c anim_stream)
        }
        if (n && files_in_pool(file)) {   // (2026-11-13, movelib: a move's animation -- a library's or a folder's own: its
            extern u32 plat_anim_rom(u32 file);         //  number and its "ROM" place the exe's, given for the match it's in;
                                                        //  movelib2: any number of the pool, files_pool_send)
            const u32 rom = plat_anim_rom(file);
            if (rom) { RD32(info) = rom; RD32(info + 4) = rom + n; }
        }
        return;
    }
    if (file == 0) { for (;;) { } }       // (the game's own answer to a file that isn't there)
    u32 e = RD32(0x80062C10u) + 4u * (file - 1);
    u32 start = RD32(e), end = RD32(e + 4);
    u32 size = RD32(RD32(0x80062C14u) + 4u * (file - 1));
    RD32(info) = (start & ~1u) + 0x1BD150u;
    RD32(info + 4) = (end & ~1u) + 0x1BD150u;
    RD32(info + 8) = size;
    RD32(info + 12) = (start & 1u) ? RD32(info + 4) - RD32(info) : 0;
}
// func_80003C94(info, file, dest): the file read into dest
u32 files_read(u32 info, u32 file, u32 dest) {
    u32 i[4];
    int file_is_rom = 0;
    file &= 0xFFFFu;
    media_file_pre(file, dest);
    if (file >= FILES_N) {
        u32 n = ext_file(file, dest);
        i[0] = 0; i[1] = 0; i[2] = n; i[3] = 0;
    } else {
        files_info((u32)i, file);
        file_is_rom = 1;
        if (i[3] > 0) {
            u32 buf = func_800007D4(i[3]);
            if (!buf) { i[0] = 0; i[1] = 0; i[2] = 0; i[3] = 0; }
            else {
                func_8000062C(i[0], buf, i[3]);
                func_80003D64(buf, dest);
                func_8000093C(buf);
            }
        } else func_8000062C(i[0], dest, i[2]);
    }
    // (2026-11-13, nmhandler) A stock man's photo from his folder, over the game's own file just read (roster.c).
    if (file_is_rom) { extern int roster_stock_photo_file(u32 file, u32 dest); roster_stock_photo_file(file, dest); }
    RD32(info) = i[0]; RD32(info + 4) = i[1]; RD32(info + 8) = i[2]; RD32(info + 12) = i[3];
    media_file_post();
    return info;
}
