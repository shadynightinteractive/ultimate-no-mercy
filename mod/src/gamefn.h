// The game's own functions that the shared C code calls directly. The mod links them by their
// recompiled names; the test blob calls them at their N64 addresses.
#ifndef GAMEFN_H
#define GAMEFN_H
#include "arcade.h"

#ifdef ARCADE_TEST
#define GAMEFN(ret, sym, addr, params) static ret (*const sym) params = (ret (*) params)(addr)
#else
#define GAMEFN(ret, sym, addr, params) extern ret sym params
#endif

// resident
GAMEFN(u32,  func_800007D4, 0x800007D4u, (u32 size));                 // heap alloc
GAMEFN(void, func_80003C94, 0x80003C94u, (u32 info, u32 file, u32 dest)); // read a file from the ROM
GAMEFN(void, func_8000062C, 0x8000062Cu, (u32 rom, u32 dest, u32 len));  // PI DMA
GAMEFN(void, func_80003D64, 0x80003D64u, (u32 src, u32 dest));        // unpack (LZSS)
GAMEFN(void, func_8000093C, 0x8000093Cu, (u32 p));                    // heap free
GAMEFN(void, func_80004EE8, 0x80004EE8u, (void));                     // write the save image out
GAMEFN(void, func_80007F94, 0x80007F94u, (u32 dst, u32 src));         // strcpy
GAMEFN(void, func_800083BC, 0x800083BCu, (u32 dst, u32 src, u32 n));  // (page name, to the save)
GAMEFN(void, func_80008468, 0x80008468u, (u32 dst, u32 src, u32 n));  // (page name, from the save)
GAMEFN(void, func_800037BC, 0x800037BCu, (u32 a));                    // the music's sound stopped
GAMEFN(void, func_80027D38, 0x80027D38u, (u32 bank));                  // the sound engine's bank
GAMEFN(u32,  func_80027494, 0x80027494u, (s32 snd));                   // a sound of it played -> its handle
GAMEFN(u32,  func_8000A690, 0x8000A690u, (u32 id, u32 costume));     // (no roster: a Superstar's entry in the match's list, by ID and costume; 0 if he isn't in it)
GAMEFN(void, func_8000AFD4, 0x8000AFD4u, (u32 dst, u32 name));        // a string of the names file
GAMEFN(u32,  func_8000BBD0, 0x8000BBD0u, (u32 id, u32 costume));   // a Superstar's entrance video
GAMEFN(void, func_8000B168, 0x8000B168u, (void));                     // appearance + names files in
GAMEFN(void, func_8000B1C4, 0x8000B1C4u, (void));                     // ... and out
GAMEFN(void, func_8000B60C, 0x8000B60Cu, (u32 rec, u32 look));       // looks -> a record
GAMEFN(void, func_8000B780, 0x8000B780u, (u32 rec, u32 name));        // names -> a record
GAMEFN(void, func_80036710, 0x80036710u, (u32 src, u32 dst, u32 n));  // bcopy
GAMEFN(void, func_80001E08, 0x80001E08u, (u32 a, u32 b, u32 out));   // 4x4 float matrix product
GAMEFN(void, func_80036A30, 0x80036A30u, (u32 dst, u32 n));           // bzero
GAMEFN(void, func_80034310, 0x80034310u, (u32 p, u32 n));             // osInvalDCache
GAMEFN(u32,  osCartRomInit, 0x8002EE40u, (void));                     // (libultra: the PC port's own, by these names)
GAMEFN(s32,  osEPiStartDma, 0x8002EDA0u, (u32 handle, u32 mb, s32 dir));
GAMEFN(s32,  osRecvMesg, 0x80036CD0u, (u32 q, u32 msg, s32 block));
GAMEFN(s32,  osSendMesg, 0x80036E00u, (u32 q, u32 msg, s32 block));
// ovl_a (the menus' and the story's)
GAMEFN(void, func_800E299C, 0x800E299Cu, (u32 dst, u32 rec));         // a costume record -> the save
GAMEFN(void, func_800E2AC0, 0x800E2AC0u, (u32 dst, u32 moves));       // a moveset -> the save
GAMEFN(void, func_800E347C, 0x800E347Cu, (void));
GAMEFN(void, func_800E31C4, 0x800E31C4u, (u32 id));
GAMEFN(void, func_800E450C, 0x800E450Cu, (u32 stats));
#endif
