// aot_tool.cpp: mods translated ahead of time, as C that links into the game (Stage 1 step 1, 2026-11-11).
//
//     aot_tool <game symbols: syms/dump.toml> <the game's ROM> <out folder> { <tag> <mod_syms.bin> <mod_binary.bin> }...
//
// One or more mods, each named by a tag (its .nrm's file name without the extension) with the two files from inside its .nrm.
// In the game, the mod loader (librecomp mods.cpp) translates each mod's MIPS as it loads it, with N64Recomp's live
// recompiler, and translates each game function the mods hook again from the ROM with the hook calls put in. This tool does
// both at build time, with the same N64Recomp front end (recompile_function_custom: the instruction decoding, the delay
// slots, the jump tables, the relocations are the very same code), writing C with N64Recomp's own C generator -- the
// generator the game's RecompiledFuncs came from. So the mods' code becomes register-level C over MEM_*, like the rest of
// the game, and none of it is generated at run time.
//
// Why not OfflineModRecomp or modrep_tool as they are: OfflineModRecomp writes a DLL's source (exported globals a DLL loader
// fills in), cannot put hook calls in (the loader refuses a mod whose replaced functions are hooked, OfflineModHooked), and
// does nothing for hooked game functions; modrep_tool renames and splits its output for tools/lift to read, not to link.
// This is OfflineModRecomp's method -- parse_mod_symbols as the loader does, then the C generator -- with those three things
// added. The contexts are set up exactly as mods.cpp sets them up (init_mod_code for a mod; build_regen_list and
// context_from_regenerated_list for the hooked game functions), so the front end sees what it sees in the game.
//
// What the translation calls, and how each is bound when the loader takes a mod's entry (librecomp/aot_mod.h). <mod> is
// nm_aot_<tag>:
//   another of the mod's functions     by name, directly (the live recompiler calls it directly too)
//   a game function                    <mod>_game_funcs[k]: the loader fills it with the game function's entry
//   an import (the port's function,    <mod>_imports[i]: the loader fills it, by the mod's import number
//     or another mod's)
//   a jalr                             LOOKUP_FUNC, the game's own lookup by address (the loader registers the mod's functions)
//   a hook, in a hooked function       nm_aot_run_hook(rdram, ctx, nm_aot_hook_slot[k]): at the function's entry and before
//                                      each return, where the live generator puts them (emit_function_start / emit_return).
//                                      A hook slot is a place in the game, whichever mods hook it: one list for all the mods
//   the mod's own addresses            RELOC_HI16 / RELOC_LO16 over <mod>_section_addresses: where the loader put the section
//
// Writes into the out folder (src/game/mod_aot in the game's source):
//   mod_aot.h              what all the translated C is written against
//   mod_aot_<tag>.h        one mod's declarations
//   mod_aot_<tag>_<n>.c    its functions, about 12,000 instructions a file
//   mod_aot_hooked.c       the hooked game functions
//   mod_aot_table.c        the table the loader reads (nm_aot_table): an entry per mod, each with the hash of its two files
//   mod_aot_report.txt     what was translated, for a person
//
// (startup 2026-11-19) NM_AOT_MASK="<tag>=<mask file>;...": byte ranges of a mod's image that hold only DATA a player's first
// start fills from their ROMs (tools/aot/aot.py: tools/firstrun/mkplan.py's EXTRAS holes). Each line of the file is
// "<offset> <size>" (hex with 0x, or decimal), in order, not overlapping. The entry then also carries those masks and the
// hash with their bytes taken as zero (aot_mod.h nm_aot_hash_masked), so the loader takes it for that build with any data
// there. What the front end reads from a mod's image is each function's words (parse_mod_symbols) and each jump table's
// words, up to and including the word that ends its scan (N64Recomp analysis.cpp; a mod has no GOT). This tool refuses a
// mask -- and so the build -- unless all of these hold:
//   1. no byte of any of the mod's functions is in a mask (the functions' bytes as the symbol file places them), no section
//      has a GOT, and no byte of any jump table N64Recomp's own analyze_function finds -- its entries and the word after
//      them, which ends the table -- is in a mask (reviewmask R1: a table's end read from a mask could lengthen the table);
//   2. the mod translated again with every masked byte scrambled (a fixed pseudo-random fill) gives the very same C, function
//      for function, and the same game functions called (a second, independent look: one sample, so check 1 is the proof).
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <span>
#include <unordered_set>
#include <vector>

#include "recompiler/context.h"
#include "recompiler/generator.h"
#include "rabbitizer.hpp"
#include "librecomp/aot_mod.h"
#include "analysis.h"            // (startup: N64Recomp's src -- its jump-table finder, analyze_function; aot.py adds the folder)

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

static std::string hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%08X", v);
    return b;
}

// A hook's place: the game function (its section's ROM address, its address), at its entry or its return.
struct HookPlace {
    uint32_t section_vrom, vram;
    bool at_return;
    bool operator==(const HookPlace&) const = default;
};

// What the generator adds to the C generator's output, for the functions of one context (a mod, or the hooked game functions).
struct Bindings {
    // The game's symbols (dump.toml): for a game section's ROM address and where it starts, and names for the comments.
    const N64Recomp::Context* game = nullptr;
    // The mod's name in the C: nm_aot_<tag> (its tables are <prefix>_imports and so on).
    std::string prefix;
    // Function number in the context being translated -> index in the hook places, for its entry and for its return.
    std::unordered_map<size_t, size_t> entry_hooks, return_hooks;
    // The game functions called so far: (section ROM address, address) -> its number in <prefix>_game_funcs.
    std::map<std::pair<uint32_t, uint32_t>, size_t> game_funcs;
    std::vector<NmAotGameFunc> game_func_list;
    bool failed = false;
};

// N64Recomp's C generator, with the calls the loader has to bind written through the mod's tables, and the hook calls put
// where the live generator puts them. Everything else is passed straight to the C generator. (CGenerator is final and
// inherits Generator privately, so it is held, not derived from.)
class AotGenerator final : public N64Recomp::Generator {
public:
    AotGenerator(std::ostream& out, Bindings& bindings) : out(out), c(out), bindings(bindings) {}

    void emit_function_start(const std::string& function_name, size_t func_index) const final {
        c.emit_function_start(function_name, func_index);
        // (live_generator.cpp emit_function_start: the entry hook runs first thing, with the context as the caller left it)
        auto hook = bindings.entry_hooks.find(func_index);
        if (hook != bindings.entry_hooks.end()) {
            out << "    nm_aot_run_hook(rdram, ctx, nm_aot_hook_slot[" << hook->second << "]);\n";
        }
    }
    void emit_return(const N64Recomp::Context& context, size_t func_index) const final {
        // (live_generator.cpp emit_return: the return hook runs before every return of the function)
        auto hook = bindings.return_hooks.find(func_index);
        if (hook != bindings.return_hooks.end()) {
            out << "{ nm_aot_run_hook(rdram, ctx, nm_aot_hook_slot[" << hook->second << "]); ";
            c.emit_return(context, func_index);
            out << "    }\n";
        } else {
            c.emit_return(context, func_index);
        }
    }
    void emit_function_call_reference_symbol(const N64Recomp::Context& context, uint16_t section_index, size_t symbol_index,
                                             uint32_t target_section_offset) const final {
        if (section_index == N64Recomp::SectionImport) {
            out << bindings.prefix << "_imports[" << symbol_index << "](rdram, ctx);   // " << context.import_symbols[symbol_index].base.name << "\n";
            return;
        }
        // A game function. The mod's relocations number the game's sections as dump.toml lists them (parse_mod_symbols was
        // given that numbering); the loader may number them otherwise, so the table names the section by its ROM address.
        if (section_index >= bindings.game->sections.size()) {
            std::fprintf(stderr, "a call into game section %u, which the game's symbols don't have\n", section_index);
            bindings.failed = true;
            return;
        }
        const N64Recomp::Section& section = bindings.game->sections[section_index];
        const uint32_t vram = section.ram_addr + target_section_offset;
        auto [at, added] = bindings.game_funcs.try_emplace({section.rom_addr, vram}, bindings.game_func_list.size());
        if (added) bindings.game_func_list.push_back(NmAotGameFunc{section.rom_addr, vram});
        size_t named = bindings.game->find_function_by_vram_section(vram, section_index);
        out << bindings.prefix << "_game_funcs[" << at->second << "](rdram, ctx);   // "
            << (named == (size_t)-1 ? std::string("?") : bindings.game->functions[named].name) << "\n";
    }
    void emit_trigger_event(uint32_t event_index) const final {
        out << "nm_aot_trigger_event(rdram, ctx, " << bindings.prefix << "_base_event_index + " << event_index << ");\n";
    }

    // The rest: the C generator's, unchanged.
    void process_binary_op(const N64Recomp::BinaryOp& op, const N64Recomp::InstructionContext& ctx) const final { c.process_binary_op(op, ctx); }
    void process_unary_op(const N64Recomp::UnaryOp& op, const N64Recomp::InstructionContext& ctx) const final { c.process_unary_op(op, ctx); }
    void process_store_op(const N64Recomp::StoreOp& op, const N64Recomp::InstructionContext& ctx) const final { c.process_store_op(op, ctx); }
    void emit_function_end() const final { c.emit_function_end(); }
    void emit_function_call_lookup(uint32_t addr) const final { c.emit_function_call_lookup(addr); }
    void emit_function_call_by_register(int reg) const final { c.emit_function_call_by_register(reg); }
    void emit_function_call(const N64Recomp::Context& context, size_t function_index) const final { c.emit_function_call(context, function_index); }
    void emit_named_function_call(const std::string& function_name) const final { c.emit_named_function_call(function_name); }
    void emit_goto(const std::string& target) const final { c.emit_goto(target); }
    void emit_label(const std::string& label_name) const final { c.emit_label(label_name); }
    void emit_jtbl_addend_declaration(const N64Recomp::JumpTable& jtbl, int reg) const final { c.emit_jtbl_addend_declaration(jtbl, reg); }
    void emit_branch_condition(const N64Recomp::ConditionalBranchOp& op, const N64Recomp::InstructionContext& ctx) const final { c.emit_branch_condition(op, ctx); }
    void emit_branch_close() const final { c.emit_branch_close(); }
    void emit_switch(const N64Recomp::Context& context, const N64Recomp::JumpTable& jtbl, int reg) const final { c.emit_switch(context, jtbl, reg); }
    void emit_case(int case_index, const std::string& target_label) const final { c.emit_case(case_index, target_label); }
    void emit_switch_error(uint32_t instr_vram, uint32_t jtbl_vram) const final { c.emit_switch_error(instr_vram, jtbl_vram); }
    void emit_switch_close() const final { c.emit_switch_close(); }
    void emit_check_fr(int fpr) const final { c.emit_check_fr(fpr); }
    void emit_check_nan(int fpr, bool is_double) const final { c.emit_check_nan(fpr, is_double); }
    void emit_cop0_status_read(int reg) const final { c.emit_cop0_status_read(reg); }
    void emit_cop0_status_write(int reg) const final { c.emit_cop0_status_write(reg); }
    void emit_cop1_cs_read(int reg) const final { c.emit_cop1_cs_read(reg); }
    void emit_cop1_cs_write(int reg) const final { c.emit_cop1_cs_write(reg); }
    void emit_muldiv(N64Recomp::InstrId instr_id, int reg1, int reg2) const final { c.emit_muldiv(instr_id, reg1, reg2); }
    void emit_syscall(uint32_t instr_vram) const final { c.emit_syscall(instr_vram); }
    void emit_do_break(uint32_t instr_vram) const final { c.emit_do_break(instr_vram); }
    void emit_pause_self() const final { c.emit_pause_self(); }
    void emit_comment(const std::string& comment) const final { c.emit_comment(comment); }

private:
    std::ostream& out;
    N64Recomp::CGenerator c;
    Bindings& bindings;
};

// One function of a context, translated into text. False (with why on stderr) if the front end refuses it, or if it needs
// something this tool doesn't bind.
static bool translate(const N64Recomp::Context& context, size_t func_index, Bindings& bindings, std::string& text) {
    std::ostringstream out;
    AotGenerator generator{out, bindings};
    std::vector<std::vector<uint32_t>> static_funcs(context.sections.size());
    if (!N64Recomp::recompile_function_custom(generator, context, func_index, out, static_funcs, true) || bindings.failed) {
        std::fprintf(stderr, "can't translate %s\n", context.functions[func_index].name.c_str());
        return false;
    }
    // (A call to an address with no function symbol would make the front end ask for a new "static" function, which nothing
    //  here would write. The live recompiler has no such thing either.)
    for (const auto& list : static_funcs) {
        if (!list.empty()) {
            std::fprintf(stderr, "%s calls an address with no function there (%08X)\n", context.functions[func_index].name.c_str(), list[0]);
            return false;
        }
    }
    text = out.str();
    // (The mod's own addresses are RELOC_HI16 / RELOC_LO16; one against a game section would be REF_RELOC_*, which the game's
    //  sections never need here: they don't move. Said now, not as a compiler error later.)
    if (text.find("REF_RELOC_") != std::string::npos) {
        std::fprintf(stderr, "%s has a relocation against a game section, which this tool doesn't bind\n", context.functions[func_index].name.c_str());
        return false;
    }
    return true;
}

// One mod: what was read, and what came of it.
struct Mod {
    std::string tag, prefix;             // aki_camera, nm_aot_aki_camera
    uint64_t hash = 0;                   // of its two files (librecomp/aot_mod.h)
    std::vector<uint8_t> syms;           // (startup) its symbol file, as read
    std::vector<uint8_t> binary;
    std::vector<NmAotMask> masks;        // (startup) its masked ranges (NM_AOT_MASK), none if empty
    uint64_t hash_masked = 0;            // the hash with them taken as zero
    N64Recomp::Context context;
    Bindings bindings;
    std::unordered_map<size_t, std::string> replaced_name;   // its function number -> the game function it replaces
    std::vector<NmAotCopyHook> copy_hooks;
    std::vector<std::string> text;       // each function's translation
    size_t words = 0, files = 0;
};

int main(int argc, const char** argv) {
    if (argc < 7 || (argc - 4) % 3 != 0) {
        std::printf("Usage: %s <game symbols file> <game ROM> <out folder> { <tag> <mod symbol file> <mod binary file> }...\n", argv[0]);
        return EXIT_FAILURE;
    }
    bool found;
    std::vector<uint8_t> rom = read_file(argv[2], found);
    if (!found) { std::fprintf(stderr, "can't open the game's ROM\n"); return EXIT_FAILURE; }

    // The game's sections and functions (syms/dump.toml), with each function's instructions from the ROM.
    N64Recomp::Context game{};
    {
        std::vector<uint8_t> rom_copy = rom;
        if (!N64Recomp::Context::from_symbol_file(argv[1], std::move(rom_copy), game, false)) {
            std::fprintf(stderr, "can't read the game's symbol file\n");
            return EXIT_FAILURE;
        }
    }
    std::unordered_map<uint32_t, uint16_t> sections_by_vrom{};
    for (uint16_t i = 0; i < game.sections.size(); i++) sections_by_vrom[game.sections[i].rom_addr] = i;
    auto game_function = [&](uint32_t section_vrom, uint32_t vram) -> size_t {
        auto section = sections_by_vrom.find(section_vrom);
        return section == sections_by_vrom.end() ? (size_t)-1 : game.find_function_by_vram_section(vram, section->second);
    };

    RabbitizerConfig_Cfg.pseudos.pseudoMove = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBeqz = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBnez = false;
    RabbitizerConfig_Cfg.pseudos.pseudoNot = false;
    RabbitizerConfig_Cfg.pseudos.pseudoBal = false;

    // ---- Read every mod, as the loader does (mods.cpp init_mod_code) ----
    std::vector<std::unique_ptr<Mod>> mods;
    for (int arg = 4; arg < argc; arg += 3) {
        auto mod = std::make_unique<Mod>();
        mod->tag = argv[arg];
        mod->prefix = "nm_aot_" + mod->tag;
        for (char ch : mod->tag) {
            if (!(std::isalnum((unsigned char)ch) || ch == '_')) { std::fprintf(stderr, "%s: a tag is letters, digits and _\n", argv[arg]); return EXIT_FAILURE; }
        }
        std::vector<uint8_t> symbol_data = read_file(argv[arg + 1], found);
        if (!found) { std::fprintf(stderr, "%s: can't open the mod's symbol file\n", argv[arg]); return EXIT_FAILURE; }
        mod->binary = read_file(argv[arg + 2], found);
        if (!found) { std::fprintf(stderr, "%s: can't open the mod's binary\n", argv[arg]); return EXIT_FAILURE; }
        // The hash guard: the two files as the loader reads them out of the .nrm, syms first.
        mod->hash = nm_aot_hash(nm_aot_hash(NM_AOT_HASH_START, symbol_data.data(), symbol_data.size()), mod->binary.data(), mod->binary.size());
        mod->syms = symbol_data;
        std::span<const char> symbol_span{reinterpret_cast<const char*>(symbol_data.data()), symbol_data.size()};
        N64Recomp::ModSymbolsError error = N64Recomp::parse_mod_symbols(symbol_span, mod->binary, sections_by_vrom, mod->context);
        if (error != N64Recomp::ModSymbolsError::Good) {
            std::fprintf(stderr, "%s: can't read the mod's symbols: %d\n", argv[arg], (int)error);
            return EXIT_FAILURE;
        }
        mod->context.set_all_reference_sections_relocatable();
        mod->context.skip_validating_reference_symbols = true;
        mod->context.rom = mod->binary;
        mod->bindings.game = &game;
        mod->bindings.prefix = mod->prefix;
        mods.push_back(std::move(mod));
    }

    // (startup 2026-11-19) The masks (NM_AOT_MASK), read and checked against the image's size and each other; the hash with them.
    if (const char* env = std::getenv("NM_AOT_MASK")) {
        const std::string list = env;
        size_t at = 0;
        while (at < list.size()) {
            size_t end = list.find(';', at);
            if (end == std::string::npos) end = list.size();
            const std::string item = list.substr(at, end - at);
            at = end + 1;
            if (item.empty()) continue;
            const size_t eq = item.find('=');
            if (eq == std::string::npos) { std::fprintf(stderr, "NM_AOT_MASK: \"%s\" isn't <tag>=<file>\n", item.c_str()); return EXIT_FAILURE; }
            const std::string tag = item.substr(0, eq);
            auto mod = std::find_if(mods.begin(), mods.end(), [&](const auto& m) { return m->tag == tag; });
            if (mod == mods.end()) { std::fprintf(stderr, "NM_AOT_MASK: no mod %s\n", tag.c_str()); return EXIT_FAILURE; }
            if (!(*mod)->masks.empty()) { std::fprintf(stderr, "NM_AOT_MASK: %s named twice\n", tag.c_str()); return EXIT_FAILURE; }
            std::ifstream file{item.substr(eq + 1)};
            if (!file.good()) { std::fprintf(stderr, "NM_AOT_MASK: can't open %s\n", item.substr(eq + 1).c_str()); return EXIT_FAILURE; }
            std::vector<std::string> tokens;
            for (std::string t; file >> t;) tokens.push_back(t);
            if (tokens.size() % 2) { std::fprintf(stderr, "%s: its mask file has something after its last whole range\n", tag.c_str()); return EXIT_FAILURE; }
            uint64_t last_end = 0;
            for (size_t k = 0; k < tokens.size(); k += 2) {
                const std::string& a = tokens[k];
                const std::string& b = tokens[k + 1];
                const uint64_t offset = std::stoull(a, nullptr, 0), size = std::stoull(b, nullptr, 0);
                if (size == 0 || offset < last_end || offset + size > (*mod)->binary.size()) {
                    std::fprintf(stderr, "%s: mask %s %s is empty, out of order, overlaps the one before or runs past the image\n", tag.c_str(), a.c_str(), b.c_str());
                    return EXIT_FAILURE;
                }
                (*mod)->masks.push_back(NmAotMask{(uint32_t)offset, (uint32_t)size});
                last_end = offset + size;
            }
            if ((*mod)->masks.empty()) { std::fprintf(stderr, "%s: its mask file has no ranges\n", tag.c_str()); return EXIT_FAILURE; }
            (*mod)->hash_masked = nm_aot_hash_masked(nm_aot_hash(NM_AOT_HASH_START, (*mod)->syms.data(), (*mod)->syms.size()),
                                                     (*mod)->binary.data(), (*mod)->binary.size(), (*mod)->masks.data(), (*mod)->masks.size());
            // check 1: no function's bytes in a mask
            for (const auto& f : (*mod)->context.functions) {
                const uint64_t f_from = f.rom, f_to = f.rom + 4 * (uint64_t)f.words.size();
                for (const NmAotMask& m : (*mod)->masks) {
                    if (f_from < (uint64_t)m.offset + m.size && m.offset < f_to) {
                        std::fprintf(stderr, "%s: mask 0x%X+0x%X holds code (the function at %08X, image 0x%llX..0x%llX): refused\n",
                                     tag.c_str(), m.offset, m.size, f.vram, (unsigned long long)f_from, (unsigned long long)f_to);
                        return EXIT_FAILURE;
                    }
                }
            }
            // check 1, continued (reviewmask R1): no GOT, and no jump table -- found as the front end finds it, its entries and
            // the word that ends it -- in a mask
            const N64Recomp::Context& context = (*mod)->context;
            for (const auto& section : context.sections) {
                if (section.got_ram_addr.has_value()) { std::fprintf(stderr, "%s: a section with a GOT: masks refused\n", tag.c_str()); return EXIT_FAILURE; }
            }
            size_t tables = 0;
            for (const auto& f : context.functions) {
                std::vector<rabbitizer::InstructionCpu> instructions;
                instructions.reserve(f.words.size());
                uint32_t vram = f.vram;
                for (uint32_t word : f.words) { instructions.emplace_back(__builtin_bswap32(word), vram); vram += 4; }
                N64Recomp::FunctionStats stats{};
                if (!N64Recomp::analyze_function(context, f, instructions, stats)) {
                    std::fprintf(stderr, "%s: the function at %08X can't be analysed: masks refused\n", tag.c_str(), f.vram);
                    return EXIT_FAILURE;
                }
                for (const auto& jt : stats.jump_tables) {
                    tables++;
                    const uint64_t j_from = jt.rom, j_to = jt.rom + 4 * ((uint64_t)jt.entries.size() + 1);
                    for (const NmAotMask& m : (*mod)->masks) {
                        if (j_from < (uint64_t)m.offset + m.size && m.offset < j_to) {
                            std::fprintf(stderr, "%s: mask 0x%X+0x%X holds a jump table of the function at %08X (image 0x%llX..0x%llX): refused\n",
                                         tag.c_str(), m.offset, m.size, f.vram, (unsigned long long)j_from, (unsigned long long)j_to);
                            return EXIT_FAILURE;
                        }
                    }
                }
            }
            std::printf("%s: no function and none of its %zu jump tables (with each one's end word) in a mask\n", tag.c_str(), tables);
        }
    }

    // Every hook place of these mods, each once, in the order the mods list them. (A place is one "hook slot" in the loader,
    // however many functions of however many mods hook it.)
    std::vector<HookPlace> hooks;
    auto hook_index = [&](const HookPlace& place) -> int {
        auto it = std::find(hooks.begin(), hooks.end(), place);
        return it == hooks.end() ? -1 : (int)(it - hooks.begin());
    };
    size_t hook_count = 0;
    for (const auto& mod : mods) {
        for (const auto& hook : mod->context.hooks) {
            HookPlace place{hook.original_section_vrom, hook.original_vram, (hook.flags & N64Recomp::HookFlags::AtReturn) == N64Recomp::HookFlags::AtReturn};
            if (hook_index(place) < 0) hooks.push_back(place);
            hook_count++;
        }
    }

    // ---- Each mod's functions ----
    std::unordered_set<uint64_t> replaced_places;   // (section ROM address, address) of every game function a mod replaces
    for (auto& mod : mods) {
        N64Recomp::Context& context = mod->context;
        for (const auto& r : context.replacements) {
            size_t index = game_function(r.original_section_vrom, r.original_vram);
            if (index == (size_t)-1) {
                std::fprintf(stderr, "%s: a replacement of %08X (section %08X) matches no game function\n", mod->tag.c_str(), r.original_vram, r.original_section_vrom);
                return EXIT_FAILURE;
            }
            mod->replaced_name[r.func_index] = game.functions[index].name;
            replaced_places.insert(((uint64_t)r.original_section_vrom << 32) | r.original_vram);
            // A replaced function that something hooks (this mod or another) has the hook calls in the mod's copy of it
            // (mods.cpp load_mod_code). The loader checks, when it takes this mod's entry, that these are the hooks there are.
            int entry = hook_index({r.original_section_vrom, r.original_vram, false});
            int at_return = hook_index({r.original_section_vrom, r.original_vram, true});
            if (entry >= 0) { mod->bindings.entry_hooks[r.func_index] = entry; mod->copy_hooks.push_back({(uint32_t)r.func_index, 0, (uint32_t)entry}); }
            if (at_return >= 0) { mod->bindings.return_hooks[r.func_index] = at_return; mod->copy_hooks.push_back({(uint32_t)r.func_index, 1, (uint32_t)at_return}); }
        }
        // Names, before translating, so the calls between the mod's functions come out named: by the address as linked.
        std::unordered_set<std::string> names;
        for (auto& f : context.functions) {
            f.name = mod->prefix + "_" + hex8(f.vram);
            if (!names.insert(f.name).second) {
                std::fprintf(stderr, "%s: two functions at %08X\n", mod->tag.c_str(), f.vram);
                return EXIT_FAILURE;
            }
        }
        mod->text.resize(context.functions.size());
        for (size_t i = 0; i < context.functions.size(); i++) {
            if (!translate(context, i, mod->bindings, mod->text[i])) return EXIT_FAILURE;
            mod->words += context.functions[i].words.size();
        }
        // (startup 2026-11-19) check 2: with masks, the whole mod read and translated again from its image with every masked byte
        // scrambled must give the same C and call the same game functions -- or the mask holds something the translation reads
        if (!mod->masks.empty()) {
            std::vector<uint8_t> scrambled = mod->binary;
            uint64_t x = 0x9E3779B97F4A7C15ull;
            for (const NmAotMask& m : mod->masks) {
                for (uint32_t i = 0; i < m.size; i++) {
                    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
                    uint8_t v = (uint8_t)(x >> 24);
                    if (v == mod->binary[m.offset + i]) v ^= 0xA5;        // (every byte changed)
                    scrambled[m.offset + i] = v;
                }
            }
            N64Recomp::Context again{};
            std::span<const char> symbol_span{reinterpret_cast<const char*>(mod->syms.data()), mod->syms.size()};
            if (N64Recomp::parse_mod_symbols(symbol_span, scrambled, sections_by_vrom, again) != N64Recomp::ModSymbolsError::Good ||
                again.functions.size() != context.functions.size()) {
                std::fprintf(stderr, "%s: its symbols don't read the same with the masked bytes scrambled: mask refused\n", mod->tag.c_str());
                return EXIT_FAILURE;
            }
            again.set_all_reference_sections_relocatable();
            again.skip_validating_reference_symbols = true;
            again.rom = scrambled;
            for (size_t i = 0; i < again.functions.size(); i++) again.functions[i].name = context.functions[i].name;
            Bindings bindings_again;
            bindings_again.game = &game;
            bindings_again.prefix = mod->prefix;
            bindings_again.entry_hooks = mod->bindings.entry_hooks;
            bindings_again.return_hooks = mod->bindings.return_hooks;
            for (size_t i = 0; i < again.functions.size(); i++) {
                std::string text;
                if (again.functions[i].words != context.functions[i].words || !translate(again, i, bindings_again, text) || text != mod->text[i]) {
                    std::fprintf(stderr, "%s: the function at %08X translates otherwise with the masked bytes scrambled: mask refused\n",
                                 mod->tag.c_str(), context.functions[i].vram);
                    return EXIT_FAILURE;
                }
            }
            if (bindings_again.game_func_list.size() != mod->bindings.game_func_list.size() ||
                !std::equal(bindings_again.game_func_list.begin(), bindings_again.game_func_list.end(), mod->bindings.game_func_list.begin(),
                            [](const NmAotGameFunc& a, const NmAotGameFunc& b) { return a.section_vrom == b.section_vrom && a.vram == b.vram; })) {
                std::fprintf(stderr, "%s: the game functions it calls differ with the masked bytes scrambled: mask refused\n", mod->tag.c_str());
                return EXIT_FAILURE;
            }
            uint64_t masked_bytes = 0;
            for (const NmAotMask& m : mod->masks) masked_bytes += m.size;
            std::printf("%s: %zu masks, %llu bytes of data: no code in them, and the same C for all %zu functions with them scrambled\n",
                        mod->tag.c_str(), mod->masks.size(), (unsigned long long)masked_bytes, context.functions.size());
        }
    }

    // ---- The game functions the mods hook and none of them replaces ----
    // The context the loader makes for them (mods.cpp build_regen_list<false> and context_from_regenerated_list): only the
    // hooked functions, in sections that keep only their ROM and RAM addresses, every call a lookup by address.
    struct Hooked { uint32_t section_vrom, vram, size; uint64_t rom_hash; int entry, at_return; std::string name, game_name; };
    std::vector<Hooked> hooked;
    for (const HookPlace& place : hooks) {
        bool listed = false;
        for (const Hooked& h : hooked) listed |= h.section_vrom == place.section_vrom && h.vram == place.vram;
        if (listed || replaced_places.contains(((uint64_t)place.section_vrom << 32) | place.vram)) continue;
        size_t index = game_function(place.section_vrom, place.vram);
        if (index == (size_t)-1) {
            std::fprintf(stderr, "a hook on %08X (section %08X) matches no game function\n", place.vram, place.section_vrom);
            return EXIT_FAILURE;
        }
        const N64Recomp::Function& f = game.functions[index];
        Hooked h{place.section_vrom, place.vram, (uint32_t)(4 * f.words.size()), 0,
                 hook_index({place.section_vrom, place.vram, false}), hook_index({place.section_vrom, place.vram, true}),
                 "nm_aot_hooked_" + hex8(place.section_vrom) + "_" + hex8(place.vram), f.name};
        h.rom_hash = nm_aot_hash(NM_AOT_HASH_START, rom.data() + f.rom, h.size);
        hooked.push_back(h);
    }
    std::sort(hooked.begin(), hooked.end(), [](const Hooked& a, const Hooked& b) {
        return a.section_vrom == b.section_vrom ? a.vram < b.vram : a.section_vrom < b.section_vrom;
    });
    N64Recomp::Context regen{};
    regen.rom = rom;
    Bindings regen_bindings;
    regen_bindings.game = &game;
    for (size_t i = 0; i < hooked.size(); i++) {
        const Hooked& h = hooked[i];
        if (regen.sections.empty() || regen.sections.back().rom_addr != h.section_vrom) {
            N64Recomp::Section& section = regen.sections.emplace_back();
            section.rom_addr = h.section_vrom;
            section.ram_addr = game.sections[sections_by_vrom[h.section_vrom]].ram_addr;
            section.size = 0;
            section.bss_size = 0;
            section.name = "patch_section_" + std::to_string(regen.sections.size() - 1);
            section.bss_section_index = 0;
            section.executable = true;
            // (The loader: relocatable if the game's section has relocations. None of this game's has; the loader checks
            //  that again before it uses one of these.)
            section.relocatable = false;
            section.has_mips32_relocs = false;
            regen.section_functions.emplace_back();
        }
        N64Recomp::Section& section = regen.sections.back();
        N64Recomp::Function& f = regen.functions.emplace_back();
        f.vram = h.vram;
        f.rom = section.rom_addr + (h.vram - section.ram_addr);
        f.name = h.name;
        f.section_index = (uint16_t)(regen.sections.size() - 1);
        f.ignored = f.reimplemented = f.stubbed = false;
        const uint32_t* words = reinterpret_cast<const uint32_t*>(rom.data() + f.rom);
        f.words.assign(words, words + h.size / 4);
        section.function_addrs.push_back(0);   // (the loader's are left zero too)
        regen.section_functions.back().push_back(i);
        regen.functions_by_vram[f.vram].push_back(i);
        if (h.entry >= 0) regen_bindings.entry_hooks[i] = h.entry;
        if (h.at_return >= 0) regen_bindings.return_hooks[i] = h.at_return;
    }
    regen.set_all_reference_sections_relocatable();
    regen.use_lookup_for_all_function_calls = true;
    std::vector<std::string> hooked_text(hooked.size());
    for (size_t i = 0; i < hooked.size(); i++) {
        if (!translate(regen, i, regen_bindings, hooked_text[i])) return EXIT_FAILURE;
    }
    if (!regen_bindings.game_func_list.empty()) {
        // (Can't happen: with every call a lookup, the front end names no game function.)
        std::fprintf(stderr, "a hooked game function calls a game function by name\n");
        return EXIT_FAILURE;
    }

    // ---- Write it out ----
    std::filesystem::path out = argv[3];
    std::filesystem::create_directories(out);
    for (const auto& old : std::filesystem::directory_iterator(out)) {
        // (a smaller mod makes fewer files: none of the last run's may stay to be compiled)
        if (old.path().filename().string().starts_with("mod_aot")) std::filesystem::remove(old.path());
    }
    const char* made = "// WRITTEN BY tools/aot/aot_tool.cpp from the built mods (tools/aot/aot.py). Do not edit: build the mod, run the tool.\n";

    {
        std::ofstream header{out / "mod_aot.h"};
        header << "// mod_aot.h: what all the mods' translations (mod_aot_*.c) are written against.\n" << made
               << "#pragma once\n#include \"librecomp/aot_mod.h\"\n\n"
                  "// The places the mod loader fills in for all the linked mods together (mod_aot_table.c has them).\n"
                  "extern size_t nm_aot_hook_slot[];\n"
                  "extern void (*nm_aot_run_hook)(uint8_t* rdram, recomp_context* ctx, size_t hook_slot);\n"
                  "extern void (*nm_aot_trigger_event)(uint8_t* rdram, recomp_context* ctx, uint32_t event);\n\n";
        for (const auto& h : hooked) header << "void " << h.name << "(uint8_t* rdram, recomp_context* ctx);\n";
    }
    for (auto& mod : mods) {
        std::ofstream header{out / ("mod_aot_" + mod->tag + ".h")};
        header << "// mod_aot_" << mod->tag << ".h: what " << mod->tag << "'s translation is written against.\n" << made
               << "#pragma once\n#include \"mod_aot.h\"\n\n"
                  "// The places the mod loader fills in when it takes this mod's entry (mod_aot_table.c has them).\n"
               << "extern int32_t " << mod->prefix << "_section_addresses[];\n"
               << "extern recomp_func_t* " << mod->prefix << "_imports[];\n"
               << "extern recomp_func_t* " << mod->prefix << "_game_funcs[];\n"
               << "extern uint32_t " << mod->prefix << "_base_event_index;\n\n"
                  "// In a mod's code, a relocated address is in the mod's own sections, where the loader put them (recomp.h's\n"
                  "// macros are for the game's sections).\n"
                  "#undef RELOC_HI16\n#undef RELOC_LO16\n"
               << "#define RELOC_HI16(section_index, offset) HI16(" << mod->prefix << "_section_addresses[section_index] + (offset))\n"
               << "#define RELOC_LO16(section_index, offset) LO16(" << mod->prefix << "_section_addresses[section_index] + (offset))\n\n";
        for (const auto& f : mod->context.functions) header << "void " << f.name << "(uint8_t* rdram, recomp_context* ctx);\n";
        header.close();

        // The mod's functions, in files of about 12,000 instructions (the compiler's time goes up faster than a file's size).
        std::ofstream file;
        size_t words_in_file = 0;
        for (size_t i = 0; i < mod->context.functions.size(); i++) {
            if (!file.is_open() || words_in_file > 12000) {
                if (file.is_open()) file.close();
                file.open(out / ("mod_aot_" + mod->tag + "_" + std::to_string(mod->files++) + ".c"));
                file << "// " << mod->tag << "'s functions, translated ahead of time.\n" << made << "#include \"mod_aot_" << mod->tag << ".h\"\n\n";
                words_in_file = 0;
            }
            auto replaced = mod->replaced_name.find(i);
            if (replaced != mod->replaced_name.end()) file << "// The mod's copy of " << replaced->second << ".\n";
            file << mod->text[i] << "\n";
            words_in_file += mod->context.functions[i].words.size();
        }
    }
    {
        std::ofstream file{out / "mod_aot_hooked.c"};
        file << "// The game functions the mods hook, translated again from the game's ROM with the hook calls in (as the mod loader's\n"
                "// regenerate_with_hooks does with the live recompiler).\n" << made << "#include \"mod_aot.h\"\n\n";
        for (size_t i = 0; i < hooked.size(); i++) file << "// " << hooked[i].game_name << ", hooked.\n" << hooked_text[i] << "\n";
    }

    std::ofstream table{out / "mod_aot_table.c"};
    table << "// mod_aot_table.c: the mods' linked translations, as the mod loader reads them (librecomp/aot_mod.h).\n" << made;
    for (const auto& mod : mods) table << "#include \"mod_aot_" << mod->tag << ".h\"\n";
    table << "\nsize_t nm_aot_hook_slot[" << std::max<size_t>(1, hooks.size()) << "] = {0};\n"
             "void (*nm_aot_run_hook)(uint8_t* rdram, recomp_context* ctx, size_t hook_slot) = 0;\n"
             "void (*nm_aot_trigger_event)(uint8_t* rdram, recomp_context* ctx, uint32_t event) = 0;\n\n";
    for (const auto& mod : mods) {
        const N64Recomp::Context& context = mod->context;
        const std::string& p = mod->prefix;
        const size_t num_imports = context.import_symbols.size(), num_game = mod->bindings.game_func_list.size();
        table << "// ---- " << mod->tag << " ----\n";
        table << "int32_t " << p << "_section_addresses[" << std::max<size_t>(1, context.sections.size()) << "] = {0};\n";
        table << "recomp_func_t* " << p << "_imports[" << std::max<size_t>(1, num_imports) << "] = {0};\n";
        table << "recomp_func_t* " << p << "_game_funcs[" << std::max<size_t>(1, num_game) << "] = {0};\n";
        table << "uint32_t " << p << "_base_event_index = 0;\n";
        table << "static recomp_func_t* const " << p << "_functions[" << std::max<size_t>(1, context.functions.size()) << "] = {\n";
        for (const auto& f : context.functions) table << "    " << f.name << ",\n";
        table << "};\nstatic const uint32_t " << p << "_function_vrams[" << std::max<size_t>(1, context.functions.size()) << "] = {\n";
        for (const auto& f : context.functions) table << "    0x" << hex8(f.vram) << "u,\n";
        table << "};\nstatic const NmAotGameFunc " << p << "_game_func_targets[" << std::max<size_t>(1, num_game) << "] = {\n";
        for (const auto& g : mod->bindings.game_func_list) {
            size_t named = game_function(g.section_vrom, g.vram);
            table << "    {0x" << hex8(g.section_vrom) << "u, 0x" << hex8(g.vram) << "u},   // "
                  << (named == (size_t)-1 ? std::string("?") : game.functions[named].name) << "\n";
        }
        table << "};\nstatic const NmAotCopyHook " << p << "_copy_hooks[" << std::max<size_t>(1, mod->copy_hooks.size()) << "] = {\n";
        for (const auto& c : mod->copy_hooks) table << "    {" << c.func_index << ", " << c.at_return << ", " << c.hook << "},   // in the mod's copy of " << mod->replaced_name[c.func_index] << "\n";
        table << "};\n";
        if (!mod->masks.empty()) {
            // (startup 2026-11-19) the image's masked ranges: data a player's first start fills (aot_mod.h)
            table << "static const NmAotMask " << p << "_masks[" << mod->masks.size() << "] = {\n";
            for (const NmAotMask& m : mod->masks) table << "    {0x" << hex8(m.offset) << "u, 0x" << hex8(m.size) << "u},\n";
            table << "};\n";
        }
        table << "\n";
    }
    table << "static const NmAotMod mods[" << mods.size() << "] = {\n";
    for (const auto& mod : mods) {
        const std::string& p = mod->prefix;
        char hash_text[32];
        std::snprintf(hash_text, sizeof(hash_text), "0x%016" PRIX64 "ull", mod->hash);
        // (startup 2026-11-19, in place of firstrun's NM_AOT_ALSO) the masks and the masked hash, when the mod has masks
        char masked_text[32];
        std::snprintf(masked_text, sizeof(masked_text), "0x%016" PRIX64 "ull", mod->hash_masked);
        table << "    {\n        .tag = \"" << mod->tag << "\", .nrm_hash = " << hash_text << ",\n"
              << "        .nrm_hash_masked = " << (mod->masks.empty() ? std::string("0") : std::string(masked_text))
              << ", .binary_size = " << mod->binary.size() << ", .num_masks = " << mod->masks.size()
              << ", .masks = " << (mod->masks.empty() ? std::string("0") : p + "_masks") << ",\n"
              << "        .num_functions = " << mod->context.functions.size() << ", .functions = " << p << "_functions, .function_vrams = " << p << "_function_vrams,\n"
              << "        .num_sections = " << mod->context.sections.size() << ", .section_addresses = " << p << "_section_addresses,\n"
              << "        .num_imports = " << mod->context.import_symbols.size() << ", .imports = " << p << "_imports,\n"
              << "        .num_game_funcs = " << mod->bindings.game_func_list.size() << ", .game_func_targets = " << p << "_game_func_targets, .game_funcs = " << p << "_game_funcs,\n"
              << "        .num_copy_hooks = " << mod->copy_hooks.size() << ", .copy_hooks = " << p << "_copy_hooks,\n"
              << "        .base_event_index = &" << p << "_base_event_index,\n    },\n";
    }
    table << "};\nstatic const NmAotHook hooks[" << std::max<size_t>(1, hooks.size()) << "] = {\n";
    for (const auto& h : hooks) table << "    {0x" << hex8(h.section_vrom) << "u, 0x" << hex8(h.vram) << "u, " << (h.at_return ? 1 : 0) << "},\n";
    table << "};\nstatic const NmAotHookedFunc hooked[" << std::max<size_t>(1, hooked.size()) << "] = {\n";
    for (const auto& h : hooked) {
        char line[256];
        std::snprintf(line, sizeof(line), "    {0x%08Xu, 0x%08Xu, %u, 0x%016" PRIX64 "ull, %s, %d, %d},   // %s\n",
                      h.section_vrom, h.vram, h.size, h.rom_hash, h.name.c_str(), h.entry, h.at_return, h.game_name.c_str());
        table << line;
    }
    table << "};\n\nstatic const NmAotTable table = {\n"
          << "    .version = NM_AOT_VERSION,\n"
          << "    .num_mods = " << mods.size() << ", .mods = mods,\n"
          << "    .num_hooks = " << hooks.size() << ", .hooks = hooks, .hook_slot = nm_aot_hook_slot,\n"
          << "    .num_hooked = " << hooked.size() << ", .hooked = hooked,\n"
          << "    .run_hook = &nm_aot_run_hook, .trigger_event = &nm_aot_trigger_event,\n"
          << "};\nconst NmAotTable* nm_aot_table(void) { return &table; }\n";

    size_t hooked_words = 0;
    for (const auto& h : hooked) hooked_words += h.size / 4;
    std::ofstream report{out / "mod_aot_report.txt"};
    std::string summary;
    char text[1024];
    for (const auto& mod : mods) {
        // (aot.py --check reads the "hash" lines)
        std::snprintf(text, sizeof(text),
                      "%s: hash %016" PRIX64 "; %zu functions (%zu instructions), %zu replace a game function, %zu hook calls in those; "
                      "%zu imports, %zu game functions called, %zu hooks; %zu file(s)\n",
                      mod->tag.c_str(), mod->hash, mod->context.functions.size(), mod->words, mod->context.replacements.size(),
                      mod->copy_hooks.size(), mod->context.import_symbols.size(), mod->bindings.game_func_list.size(),
                      mod->context.hooks.size(), mod->files);
        summary += text;
        if (!mod->masks.empty()) {
            uint64_t masked_bytes = 0;
            for (const NmAotMask& m : mod->masks) masked_bytes += m.size;
            std::snprintf(text, sizeof(text), "  (startup) masked hash %016" PRIX64 ": %zu ranges, %llu bytes of the extras' data (no code; the same C with them scrambled)\n",
                          mod->hash_masked, mod->masks.size(), (unsigned long long)masked_bytes);
            summary += text;
        }
    }
    std::snprintf(text, sizeof(text), "hook places: %zu (the mods' hooks: %zu); hooked game functions translated from the ROM: %zu (%zu instructions)\n",
                  hooks.size(), hook_count, hooked.size(), hooked_words);
    summary += text;
    report << summary;
    for (const auto& h : hooked) {
        report << "  hooked: " << h.game_name << " (section " << hex8(h.section_vrom) << ", " << h.size / 4 << " instructions)"
               << (h.entry >= 0 ? " entry" : "") << (h.at_return >= 0 ? " return" : "") << "\n";
    }
    std::fputs(summary.c_str(), stdout);
    return EXIT_SUCCESS;
}
