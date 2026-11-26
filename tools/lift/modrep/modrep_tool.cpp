// modrep_tool.cpp: the mod's replacement functions, translated the way the game runs them, for tools/lift to convert.
//
//     modrep_tool <mod_syms.bin> <mod_binary.bin> <game symbols: syms/dump.toml> <out folder>
//
// The mod (mod/build/mod_six: what RecompModTool made of it) replaces some of the game's functions with copies of its own. In
// the game, the mod loader translates the mod's MIPS as it loads it (N64Recomp's live recompiler) and points each replaced
// function at its copy. This does the same translation, with the same translator's front end, into C (N64Recomp's C generator,
// as OfflineModRecomp in N64Recomp does for a whole mod; this is that tool cut down to the replacements): the same
// register-level C that RecompiledFuncs has for the game's own functions, with each instruction's MIPS in a comment. tools/lift
// reads it beside RecompiledFuncs and converts it as it converts any function, with this as the original the tester compares
// against.
//
// Writes into the out folder:
//   modrep_originals.c   each replacement's translation, as `RECOMP_FUNC void modrep_<the game function's name>(...)`. In it,
//                        a call to another of the mod's replacements calls the game function it replaces (which, in the game,
//                        leads to that replacement); a call to any other of the mod's own functions is `modfn_<its address>`;
//                        a call to an import (a function the port provides) has the import's name. The mod's own addresses
//                        stay RELOC_HI16 / RELOC_LO16 (section, offset), as the translator writes them (tools/lift/modrep.py
//                        deals with those).
//   modrep_index.json    for each replacement: its name, the game function it replaces (name, section ROM address,
//                        address), the copy's address and size in the mod, whether it is a forced replacement, whether the
//                        mod also hooks that function, and its hash (lift_modrep_hash.h).
#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <span>
#include <unordered_set>
#include <vector>

#include "recompiler/context.h"
#include "rabbitizer.hpp"
#include "lift_modrep_hash.h"

static std::vector<uint8_t> read_file(const std::filesystem::path& path, bool& found) {
    std::vector<uint8_t> ret;
    found = false;
    std::ifstream file{path, std::ios::binary};
    if (file.good()) {
        file.seekg(0, std::ios::end);
        ret.resize(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(ret.data()), ret.size());
        found = true;
    }
    return ret;
}

static std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%08X", v);
    return b;
}

int main(int argc, const char** argv) {
    if (argc != 5) {
        std::printf("Usage: %s <mod symbol file> <mod binary file> <game symbols file> <out folder>\n", argv[0]);
        return EXIT_FAILURE;
    }
    bool found;
    std::vector<uint8_t> symbol_data = read_file(argv[1], found);
    if (!found) { std::fprintf(stderr, "can't open the mod's symbol file\n"); return EXIT_FAILURE; }
    std::vector<uint8_t> rom_data = read_file(argv[2], found);
    if (!found) { std::fprintf(stderr, "can't open the mod's binary\n"); return EXIT_FAILURE; }
    std::span<const char> symbol_span{reinterpret_cast<const char*>(symbol_data.data()), symbol_data.size()};

    // The game's sections and functions (syms/dump.toml), as the mod loader has them.
    std::vector<uint8_t> no_rom{};
    N64Recomp::Context game{};
    if (!N64Recomp::Context::from_symbol_file(argv[3], std::move(no_rom), game, false)) {
        std::fprintf(stderr, "can't read the game's symbol file\n");
        return EXIT_FAILURE;
    }
    std::unordered_map<uint32_t, uint16_t> sections_by_vrom{};
    for (uint16_t i = 0; i < game.sections.size(); i++) sections_by_vrom[game.sections[i].rom_addr] = i;

    // The mod, read as the loader reads it (parse_mod_symbols), then given the game's symbols, as OfflineModRecomp does.
    N64Recomp::Context mod;
    N64Recomp::ModSymbolsError error = N64Recomp::parse_mod_symbols(symbol_span, rom_data, sections_by_vrom, mod);
    if (error != N64Recomp::ModSymbolsError::Good) {
        std::fprintf(stderr, "can't read the mod's symbols: %d\n", (int)error);
        return EXIT_FAILURE;
    }
    mod.import_reference_context(game);
    // (OfflineModRecomp's: each call to a game function gets that function's symbol, so the C names it.)
    std::unordered_map<uint32_t, std::vector<size_t>> reference_by_vram{};
    for (size_t i = 0; i < mod.num_regular_reference_symbols(); i++) {
        const auto& sym = mod.get_regular_reference_symbol(i);
        if (sym.section_index != N64Recomp::SectionAbsolute) {
            reference_by_vram[mod.get_reference_section_vram(sym.section_index) + sym.section_offset].push_back(i);
        }
    }
    for (auto& section : mod.sections) {
        for (auto& reloc : section.relocs) {
            if (reloc.type == N64Recomp::RelocType::R_MIPS_26 && reloc.reference_symbol && mod.is_regular_reference_section(reloc.target_section)) {
                uint32_t target = mod.get_reference_section_vram(reloc.target_section) + reloc.target_section_offset;
                bool named = false;
                for (size_t i : reference_by_vram[target]) {
                    if (mod.get_reference_symbol(reloc.target_section, i).section_index == reloc.target_section) {
                        reloc.symbol_index = i;
                        named = true;
                        break;
                    }
                }
                if (!named) {
                    std::fprintf(stderr, "a call to %08X in game section %d has no game function there\n", target, reloc.target_section);
                    return EXIT_FAILURE;
                }
            }
        }
    }
    mod.rom = std::move(rom_data);

    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBal = false;

    // Each replacement's game function, by its name in the game's symbols.
    std::unordered_map<size_t, std::string> replaced_name{};
    for (const auto& r : mod.replacements) {
        auto section = sections_by_vrom.find(r.original_section_vrom);
        size_t index = section == sections_by_vrom.end() ? (size_t)-1 : game.find_function_by_vram_section(r.original_vram, section->second);
        if (index == (size_t)-1) {
            std::fprintf(stderr, "a replacement of %08X (section %08X) matches no game function\n", r.original_vram, r.original_section_vrom);
            return EXIT_FAILURE;
        }
        replaced_name[r.func_index] = game.functions[index].name;
    }
    // Names, before translating, so the calls come out named: a replacement calls by the game function's name (the call reaches
    // it through that function in the game); the mod's other functions by their address.
    for (size_t i = 0; i < mod.functions.size(); i++) {
        auto it = replaced_name.find(i);
        mod.functions[i].name = it != replaced_name.end() ? it->second : "modfn_" + hex(mod.functions[i].vram).substr(2);
    }

    std::filesystem::path out = argv[4];
    std::filesystem::create_directories(out);
    std::ofstream originals{out / "modrep_originals.c"};
    originals << "// modrep_originals.c: the mod's replacement functions, translated as the mod loader translates them. WRITTEN BY\n"
                 "// tools/lift/modrep/modrep_tool.cpp from the built mod: read by tools/lift, never compiled into the game.\n\n";
    std::ofstream index{out / "modrep_index.json"};
    index << "[\n";
    std::vector<std::vector<uint32_t>> static_funcs(mod.sections.size());
    bool first = true;
    for (const auto& r : mod.replacements) {
        const std::string game_name = replaced_name[r.func_index];
        std::ostringstream text;
        if (!N64Recomp::recompile_function(mod, r.func_index, text, static_funcs, true)) {
            std::fprintf(stderr, "can't translate the mod's copy of %s\n", game_name.c_str());
            return EXIT_FAILURE;
        }
        // The translation names the function as the game function; here it gets its own name, so it sits beside the game's.
        std::string t = text.str();
        const std::string head = "RECOMP_FUNC void " + game_name + "(";
        size_t at = t.find(head);
        if (at == std::string::npos) { std::fprintf(stderr, "no head in the translation of %s\n", game_name.c_str()); return EXIT_FAILURE; }
        t.replace(at, head.size(), "RECOMP_FUNC void modrep_" + game_name + "(");
        originals << t << "\n";

        bool hooked_entry = false, hooked_return = false;
        for (const auto& hook : mod.hooks) {
            if (hook.original_section_vrom == r.original_section_vrom && hook.original_vram == r.original_vram) {
                ((hook.flags & N64Recomp::HookFlags::AtReturn) == N64Recomp::HookFlags::AtReturn ? hooked_return : hooked_entry) = true;
            }
        }
        const auto& f = mod.functions[r.func_index];
        uint64_t hash = lift_modrep::replacement_hash(mod, r.func_index, [&](uint16_t s) { return (uint64_t)mod.get_reference_section_rom(s); });
        // The imports it calls (functions the port provides to the mod), by name.
        std::string imports;
        for (const auto& reloc : mod.sections[f.section_index].relocs) {
            if (reloc.address >= f.vram && reloc.address < f.vram + 4 * f.words.size() && reloc.target_section == N64Recomp::SectionImport &&
                reloc.symbol_index < mod.import_symbols.size()) {
                imports += std::string(imports.empty() ? "" : ", ") + "\"" + mod.import_symbols[reloc.symbol_index].base.name + "\"";
            }
        }
        char line[1024];
        std::snprintf(line, sizeof(line),
                      "%s{\"name\": \"modrep_%s\", \"replaces\": \"%s\", \"section_vrom\": %u, \"vram\": %u, \"mod_vram\": %u, \"words\": %zu, "
                      "\"force\": %s, \"hooked_entry\": %s, \"hooked_return\": %s, \"hash\": \"%016" PRIX64 "\", \"imports\": [",
                      first ? "" : ",\n", game_name.c_str(), game_name.c_str(), r.original_section_vrom, r.original_vram, f.vram,
                      f.words.size(), (r.flags & N64Recomp::ReplacementFlags::Force) == N64Recomp::ReplacementFlags(0) ? "false" : "true",
                      hooked_entry ? "true" : "false", hooked_return ? "true" : "false", hash);
        index << line << imports << "]}";
        first = false;
    }
    index << "\n]\n";
    // The mod's hooks (RECOMP_HOOK, RECOMP_HOOK_RETURN): the game function hooked, at its entry or its return, and the mod's
    // function that runs there (modfn_<address>). For reading only: what such a hook reads, should hooked functions be
    // converted some day.
    std::ofstream hooks{out / "modrep_hooks.json"};
    hooks << "[\n";
    for (size_t i = 0; i < mod.hooks.size(); i++) {
        const auto& hook = mod.hooks[i];
        auto section = sections_by_vrom.find(hook.original_section_vrom);
        size_t index_in_game = section == sections_by_vrom.end() ? (size_t)-1 : game.find_function_by_vram_section(hook.original_vram, section->second);
        hooks << (i ? ",\n" : "") << "{\"hooks\": \"" << (index_in_game == (size_t)-1 ? std::string("?") : game.functions[index_in_game].name)
              << "\", \"at_return\": " << ((hook.flags & N64Recomp::HookFlags::AtReturn) == N64Recomp::HookFlags::AtReturn ? "true" : "false")
              << ", \"function\": \"" << mod.functions[hook.func_index].name << "\"}";
    }
    hooks << "\n]\n";
    // The mod's other functions, translated the same way (modfn_<address>), so tools/lift/analyse.py can see what a replacement
    // that calls one of them really reads: they are never converted.
    std::ofstream others{out / "modrep_modfuncs.c"};
    others << "// modrep_modfuncs.c: the mod's own functions that replace nothing, translated as the mod loader translates them. WRITTEN\n"
              "// BY tools/lift/modrep/modrep_tool.cpp: for tools/lift's analysis only (what a replacement calling one reads).\n\n";
    for (size_t i = 0; i < mod.functions.size(); i++) {
        if (replaced_name.contains(i) || mod.functions[i].words.empty()) continue;
        if (!N64Recomp::recompile_function(mod, i, others, static_funcs, true)) {
            std::fprintf(stderr, "can't translate the mod's function %s\n", mod.functions[i].name.c_str());
            return EXIT_FAILURE;
        }
        others << "\n";
    }
    // The mod's sections' addresses as linked, for RELOC_HI16 / RELOC_LO16 (section number, offset) in the translations.
    std::ofstream sections{out / "modrep_sections.json"};
    // (Each as [address, size with its bss], the bss being the zeroed space the loader puts straight after the section.)
    sections << "[";
    for (size_t i = 0; i < mod.sections.size(); i++) {
        sections << (i ? ", " : "") << "[" << mod.sections[i].ram_addr << ", " << (mod.sections[i].size + mod.sections[i].bss_size) << "]";
    }
    sections << "]\n";
    std::printf("%zu replacements\n", mod.replacements.size());
    return EXIT_SUCCESS;
}
