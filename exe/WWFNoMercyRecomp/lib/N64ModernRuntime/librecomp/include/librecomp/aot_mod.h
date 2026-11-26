// aot_mod.h: mods translated ahead of time and linked into the game ("the linked translation", Stage 1 step 1, 2026-11-11).
//
// A mod is MIPS code in a .nrm. The mod loader (librecomp mods.cpp) normally translates it as it loads it, with N64Recomp's
// live recompiler. tools/aot/aot_tool.cpp does the same translation at build time, with the same N64Recomp front end, into C
// (src/game/mod_aot/), and that C is compiled into the game. This header is the table that C hands to the loader: one entry
// for each mod translated (the arcade mod, and the three AkiMods the game ships with: separate files, a separate library),
// with its translated functions and the places the loader fills in for it (the imports, the game functions it calls, where
// its sections were loaded); and, for all of them together, the hook slots and the hooked game functions -- the same things
// the loader fills in for its own live translation.
//
// The hash guard, per mod: each entry carries the hash of the mod's two files it was made from (mod_syms.bin and
// mod_binary.bin, as they are inside the .nrm). The loader works out the same hash for each mod it loads and uses an entry
// only for a mod whose hash it has. Any other build of a mod, or any other mod, is translated by the live recompiler as
// before, beside the linked ones.
//
// (startup 2026-11-19) The mod's DATA the first start fills from the player's ROMs -- the built-in extras' tables
// (tools/firstrun/mkplan.py's EXTRAS holes: the ported Superstars, the arenas, No Mercy Plus's media, the 2K15 models, the
// extras' flag) -- differs from player to player with the ROMs they have. Those bytes are no instructions and nothing the
// translation reads (aot_tool checks it at every build and stops otherwise: no function's bytes, no GOT and no jump table
// -- with the word that ends it, as N64Recomp's own analysis finds the tables -- in them; and the whole mod translated again
// with them scrambled gives the very same C. Independent review: REVIEW_STARTUP_MASK in the project's private notes). So an entry may also carry those byte ranges
// (masks) and the hash of the two files with every masked byte taken as zero: the loader takes the entry for a mod with
// the same symbol file, the same image size and the same bytes everywhere else -- the same code, whatever the extras' data.
//
// Plain C, so the generated C, the tool and the loader all read this one file.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Raised when anything below changes shape: the loader refuses a table of another version (and translates live).
#define NM_AOT_VERSION 4u   // (3, firstrun 2026-11-17: nrm_hash_also; 4, startup 2026-11-19: the masked hash in its place)

// FNV-1a, 64-bit, one byte at a time (as lift_modrep_hash.h): simple, and the same on every compiler. Start with
// NM_AOT_HASH_START; feed the result back in as `hash` to go on with more bytes.
#define NM_AOT_HASH_START 0xCBF29CE484222325ull
static inline uint64_t nm_aot_hash(uint64_t hash, const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

// (startup 2026-11-19) A byte range of a mod's image (mod_binary.bin) left out of the masked hash: its offset and length.
typedef struct NmAotMask {
    uint32_t offset;
    uint32_t size;
} NmAotMask;

// nm_aot_hash over `size` bytes with every byte inside one of the masks (sorted by offset, not overlapping) taken as zero:
// the same as nm_aot_hash over a copy with those bytes zero.
static inline uint64_t nm_aot_hash_masked(uint64_t hash, const void* data, size_t size, const NmAotMask* masks, size_t num_masks) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t m = 0;
    for (size_t i = 0; i < size; i++) {
        while (m < num_masks && (size_t)masks[m].offset + masks[m].size <= i) m++;
        hash ^= (m < num_masks && i >= masks[m].offset) ? 0u : bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

// A game function, named the way that is the same for the tool and the loader: its section's ROM address, and its address.
typedef struct NmAotGameFunc {
    uint32_t section_vrom;
    uint32_t vram;
} NmAotGameFunc;

// One of the mod's hooks' places (RECOMP_HOOK / RECOMP_HOOK_RETURN): the game function, and its entry or its return. Several
// of the mod's functions may hook the same place; that is one entry here, as it is one "hook slot" in the loader.
typedef struct NmAotHook {
    uint32_t section_vrom;
    uint32_t vram;
    uint32_t at_return;
} NmAotHook;

// A game function the mod hooks but does not replace, translated again from the game's ROM with the hook calls in it (what
// the loader's regenerate_with_hooks does with the live recompiler). The loader uses it only when the function's bytes in the
// ROM it holds have this size and hash.
typedef struct NmAotHookedFunc {
    uint32_t section_vrom;
    uint32_t vram;
    uint32_t size;           // bytes of code
    uint64_t rom_hash;       // nm_aot_hash of those bytes as they are in the ROM
    recomp_func_t* func;
    int32_t entry_hook;      // index in `hooks`, or -1: no hook at its entry
    int32_t return_hook;     // the same for its return
} NmAotHookedFunc;

// A hook call inside one of a mod's own copies of a game function (the mod replaces the function, and something hooks it):
// which of the mod's functions, at its entry or its return, and which hook place it calls. The loader takes the entry only if
// these are exactly the hooks it would put in that mod's copies itself.
typedef struct NmAotCopyHook {
    uint32_t func_index;
    uint32_t at_return;
    uint32_t hook;           // index in the table's `hooks`
} NmAotCopyHook;

// One mod.
typedef struct NmAotMod {
    const char* tag;                     // the .nrm's file name without the extension: for the log
    uint64_t nrm_hash;                   // of mod_syms.bin's bytes, then mod_binary.bin's
    // (startup 2026-11-19, in place of firstrun's nrm_hash_also) The same build with other data in its masked ranges -- the
    // release's mod as a player's first start makes it, with or without the built-in extras: nm_aot_hash of mod_syms.bin, then
    // nm_aot_hash_masked of mod_binary.bin; only for an image of binary_size bytes. num_masks 0: none (nrm_hash alone).
    uint64_t nrm_hash_masked;
    uint32_t binary_size;
    size_t num_masks;
    const NmAotMask* masks;
    // The mod's functions, in the order of its symbol file (the loader's function numbers), and each one's address as linked.
    size_t num_functions;
    recomp_func_t* const* functions;
    const uint32_t* function_vrams;
    // Where the loader put the mod's sections (RELOC_HI16 / RELOC_LO16 in the translation read this).
    size_t num_sections;
    int32_t* section_addresses;
    // The functions the port (or another mod) provides to the mod, by the mod's import number.
    size_t num_imports;
    recomp_func_t** imports;
    // The game functions the mod calls.
    size_t num_game_funcs;
    const NmAotGameFunc* game_func_targets;
    recomp_func_t** game_funcs;
    // The hook calls in its copies of game functions.
    size_t num_copy_hooks;
    const NmAotCopyHook* copy_hooks;
    uint32_t* base_event_index;
} NmAotMod;

typedef struct NmAotTable {
    uint32_t version;                    // NM_AOT_VERSION
    size_t num_mods;
    const NmAotMod* mods;
    // Every hook place of the mods here, and the loader's slot number for each ((size_t)-1: the mods loaded have no such hook).
    // A slot is a place, not a mod: whichever mods hook it, the call is the same.
    size_t num_hooks;
    const NmAotHook* hooks;
    size_t* hook_slot;
    // The hooked game functions.
    size_t num_hooked;
    const NmAotHookedFunc* hooked;
    // The loader's own functions the translation calls through.
    void (**run_hook)(uint8_t* rdram, recomp_context* ctx, size_t hook_slot);
    void (**trigger_event)(uint8_t* rdram, recomp_context* ctx, uint32_t event);
} NmAotTable;

// Defined by the generated src/game/mod_aot/mod_aot_table.c. (A game built without it has none: the loader's weak one.)
const NmAotTable* nm_aot_table(void);

#ifdef __cplusplus
}
#endif
