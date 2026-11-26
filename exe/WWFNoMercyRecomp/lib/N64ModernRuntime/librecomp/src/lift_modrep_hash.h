// lift_modrep_hash.h: one 64-bit number that stands for one of the mod's replacement functions, exactly as the built mod has it.
//
// The mod replaces some of the game's functions with copies of its own (mod/gen/asmpatch.py's edited copies, and the C ones that
// say RECOMP_PATCH). tools/lift can convert such a copy to C++ (tools/lift/modrep.py): the converted copy then has the mod's edits
// baked into it. If a later build of the mod changes that copy, the converted one is stale and must not run. This number is how
// that is noticed. tools/lift/modrep/modrep_tool.cpp works it out when a copy is converted (lifted.cpp keeps it), and the game's
// mod loader (librecomp mods.cpp, which has an identical copy of this file) works it out again from the mod it is loading: the
// converted copy is used only when the two agree. Otherwise the mod's own copy runs, as it always did.
//
// What goes in, for the function's instructions as they are in the mod's binary:
//   - how many instructions there are, and each one's 32 bits, with the bits a relocation fills in left out (they depend on where
//     things end up in memory, which changes whenever any other part of the mod grows or shrinks);
//   - each relocation: where it is in the function, its kind, and what it points at, in terms that don't move with the mod's layout:
//       a game function or address: the game section's ROM address and the offset in it;
//       another of the mod's replacements: the game function it replaces (section ROM address and address);
//       a place inside this same function: the offset from the function's start;
//       a list of addresses inside this same function (a jump table, in the mod's data): each entry's offset from the function's
//         start, as many as there are, read from the mod's binary;
//       an import (a function the port provides) or an event: its number, and the import's name;
//       anything else in the mod (its own data): which section, and the offset in it -- the converted code has that offset in it
//         (tools/lift/lift.py, lift_mod_address), so a change of layout makes the copy stale. (A copy that calls the mod's own code
//         is refused for conversion: why_not in tools/lift/modrep.py.)
// Header-only, so the tool and the loader compile the very same code.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include "recompiler/context.h"

namespace lift_modrep {

// FNV-1a, 64-bit, one byte at a time: simple, and the same on every compiler.
struct Hash {
    uint64_t value = 0xCBF29CE484222325ull;
    void byte(uint8_t b) { value ^= b; value *= 0x100000001B3ull; }
    void word(uint64_t v) { for (int i = 0; i < 8; i++) byte((uint8_t)(v >> (8 * i))); }
    void text(const std::string& s) { word(s.size()); for (char c : s) byte((uint8_t)c); }
};

// A word of the mod's binary, at a mod address in one of its sections (big-endian, as the N64 keeps it); false if it isn't there.
inline bool binary_word(const N64Recomp::Context& mod, const N64Recomp::Section& section, uint32_t address, uint32_t& out) {
    if (address < section.ram_addr || address + 4 > section.ram_addr + section.size) return false;
    size_t at = (size_t)section.rom_addr + (address - section.ram_addr);
    if (at + 4 > mod.rom.size()) return false;
    out = ((uint32_t)mod.rom[at] << 24) | ((uint32_t)mod.rom[at + 1] << 16) | ((uint32_t)mod.rom[at + 2] << 8) | (uint32_t)mod.rom[at + 3];
    return true;
}

// The replacement (if any) whose mod copy starts at this mod address.
inline const N64Recomp::FunctionReplacement* replacement_at(const N64Recomp::Context& mod, uint32_t vram) {
    auto found = mod.functions_by_vram.find(vram);
    if (found == mod.functions_by_vram.end()) return nullptr;
    for (size_t index : found->second) {
        for (const auto& r : mod.replacements) {
            if (r.func_index == index) return &r;
        }
    }
    return nullptr;
}

// A jump table at this mod address: the run of words there that are each relocated to an address inside [start, end) -- the
// function's own code -- as offsets from start. Empty if it isn't one.
inline std::vector<uint32_t> table_entries(const N64Recomp::Context& mod, const N64Recomp::Section& section, uint32_t target,
                                           uint32_t start, uint32_t end) {
    std::vector<uint32_t> out;
    for (uint32_t at = target;; at += 4) {
        bool relocated = false;
        for (const auto& t : section.relocs) {
            if (t.address == at && t.type == N64Recomp::RelocType::R_MIPS_32 && !t.reference_symbol) { relocated = true; break; }
        }
        uint32_t entry = 0;
        if (!relocated || !binary_word(mod, section, at, entry) || entry < start || entry >= end) break;
        out.push_back(entry - start);
    }
    return out;
}

// section_vrom: a game section's number (as the mod's relocations name it) -> its ROM address. The tool and the loader number the
// game's sections each their own way; the ROM address is the same for both.
template <class SectionVrom>
uint64_t replacement_hash(const N64Recomp::Context& mod, size_t func_index, SectionVrom section_vrom) {
    const N64Recomp::Function& func = mod.functions[func_index];
    const N64Recomp::Section& section = mod.sections[func.section_index];
    const uint32_t start = func.vram, end = func.vram + 4 * (uint32_t)func.words.size();
    std::vector<const N64Recomp::Reloc*> relocs;
    for (const auto& r : section.relocs) {
        if (r.address >= start && r.address < end) relocs.push_back(&r);
    }
    std::sort(relocs.begin(), relocs.end(), [](const N64Recomp::Reloc* a, const N64Recomp::Reloc* b) { return a->address < b->address; });

    Hash h;
    h.text("lift_modrep 1");
    h.word(func.words.size());
    for (size_t i = 0; i < func.words.size(); i++) {
        uint32_t w = 0;
        binary_word(mod, section, start + 4 * (uint32_t)i, w);
        for (const auto* r : relocs) {
            if (r->address != start + 4 * i) continue;
            if (r->type == N64Recomp::RelocType::R_MIPS_26) w &= ~0x03FFFFFFu;
            else if (r->type == N64Recomp::RelocType::R_MIPS_HI16 || r->type == N64Recomp::RelocType::R_MIPS_LO16) w &= ~0xFFFFu;
            else if (r->type == N64Recomp::RelocType::R_MIPS_32) w = 0;
        }
        h.word(w);
    }
    h.word(relocs.size());
    for (const auto* r : relocs) {
        h.word(r->address - start);
        h.word((uint64_t)r->type);
        if (r->target_section == N64Recomp::SectionImport) {
            h.byte('I');
            h.word(r->symbol_index);
            if (r->symbol_index < mod.import_symbols.size()) h.text(mod.import_symbols[r->symbol_index].base.name);
        } else if (r->target_section == N64Recomp::SectionEvent) {
            h.byte('E');
            h.word(r->symbol_index);
        } else if (r->target_section == N64Recomp::SectionAbsolute) {
            h.byte('A');
            h.word(r->target_section_offset);
        } else if (r->reference_symbol) {
            h.byte('G');
            h.word(section_vrom(r->target_section));
            h.word(r->target_section_offset);
        } else if (r->target_section < mod.sections.size()) {
            const N64Recomp::Section& target_section = mod.sections[r->target_section];
            const uint32_t target = target_section.ram_addr + r->target_section_offset;
            const N64Recomp::FunctionReplacement* other = r->type == N64Recomp::RelocType::R_MIPS_26 ? replacement_at(mod, target) : nullptr;
            if (other != nullptr) {
                h.byte('R');
                h.word(other->original_section_vrom);
                h.word(other->original_vram);
            } else if (target >= start && target < end) {
                h.byte('S');
                h.word(target - start);
            } else {
                std::vector<uint32_t> entries = table_entries(mod, target_section, target, start, end);
                if (!entries.empty() && r->type != N64Recomp::RelocType::R_MIPS_26) {
                    // A jump table: its entries, as offsets in this function. (Where the table is doesn't matter: the converted
                    // code's switch never reads it.)
                    h.byte('J');
                    h.word(entries.size());
                    for (uint32_t e : entries) h.word(e);
                } else {
                    // The mod's own data (or code): which section, and where in it. The converted code has that offset in it
                    // (tools/lift/lift.py, lift_mod_address), so when the mod's layout moves it, the copy is stale.
                    h.byte('D');
                    h.word(r->target_section);
                    h.word(r->target_section_offset);
                }
            }
        } else {
            h.byte('?');
        }
    }
    return h.value;
}

}  // namespace lift_modrep
