// ri_wmmoves.h (phase3 p3folders / p3wmmoves, 2026-11-19): WrestleMania 2000's men's movesets in No Mercy's moves, and the
// WrestleMania 2000 move library -- the C++ twin of
//   tools\handlers\wmlib.py "lib"           -> movelib_wm2000.bin, move_lib_wm2000.json, rows_wm2000.json (Library, write)
//   tools\handlers\wm2k_import.py one()     -> a man's moves.json and his packed moveset (wrestler.json "moveset",
//                                              "source"."moveset_fallbacks")
//   tools\handlers\wm2k_refresh.py          -> the same, brought up to the refresh's rules (his stance WrestleMania 2000's,
//                                              his winning taunt a standing one), with its two notes
// and of what they call: handlers\wm2000\game.py (use_rom_files, moveset_of, place_own), mod\gen\mkroster.py
// (port_wm_moveset, stand_in, unpack / pack by No Mercy's own code in ri_mips, CAN), mod\gen\wm2k.py (unpack_moves by
// WrestleMania 2000's own code, its move names), mod\gen\wmanim.py (nm_anim, nm_move: a move found by its very animations),
// handlers\baddblood\game.py pack_moveset (= revenge\game.py pack_plain), handlers\revenge\fixes2.py (win_pose,
// repack_abilities).
//
// Inputs: the WrestleMania 2000 and No Mercy ROMs (their files, the No Mercy RAM images "van_sel" and "wm2k_ram" made by
// ri_images from our load maps), the move names (names_index.json, ri_data), and the WrestleMania 2000 handler's per-man
// fixes (tools\handlers\handlers\wm2000\wrestlers\<string id>.json: his roster index, his reference ordinal). No frozen
// table: every choice the Python makes is made here the same way (a Python set's iteration order included: PySet below).
#pragma once
#include "ri_roms.h"
#include "ri_wmroster.h"
#include <map>
#include <memory>
#include <optional>

namespace ri {

// One man's moveset, as his folder has it after wm2k_import.py one() and wm2k_refresh.py.
struct WmMoves {
    std::string moves_json;                    // moves.json, the file's bytes (json.dump: one line, no newline at the end)
    std::string packed_hex;                    // wrestler.json "moveset": the packed 0xD8 bytes as lower-case hex
    int fell = 0;                              // wrestler.json "source"."moveset_fallbacks"
    std::optional<std::string> stance_note;    // wrestler.json "source"."stance" (the refresh changed ability 1), or none
    std::optional<std::string> win_pose_note;  // wrestler.json "source"."win_pose" (the refresh changed slot 235), or none
};

class WmMoveLib {
public:
    // R: the player's ROMs (WrestleMania 2000 and No Mercy needed: Error otherwise). ri_data_dir: tools\romimport (its
    // loadmaps\ and names_index.json). wm_wrestlers_dir: tools\handlers\handlers\wm2000\wrestlers (the 58 men's fixes).
    // Builds the library from all 58 movesets (wmlib.py Library.__init__), as wm2k_import.py does once per run.
    WmMoveLib(Roms& R, const fs::path& ri_data_dir, const fs::path& wm_wrestlers_dir);
    ~WmMoveLib();

    // The chain step "wmlib": <out>\movelib_wm2000.bin, move_lib_wm2000.json, rows_wm2000.json (wmlib.py build + write).
    // Independent of man() calls (it counts the slots on a pass of its own, as wmlib.py's own process does).
    void write_lib(const fs::path& out);

    // One man (one of the 56 that have no "legacy_game_id"): e = his roster record (ri_wmroster), reference_ordinal = his
    // fixes' "moveset"."reference_ordinal".
    WmMoves man(const std::string& sid, const WmRecord& e, int reference_ordinal);

    // The 58 men's string IDs by roster index, and whether each is one of the two built-in ports (skipped by man()).
    struct Who { std::string sid; int roster_index, reference_ordinal; bool ported; };
    const std::vector<Who>& who() const;

    struct Impl;
private:
    std::unique_ptr<Impl> p_;
};

// ---- Badd Blood (WWF WarZone - Badd Blood, a WrestleMania 2000 mod: the same engine, its own files) ------------------------
// handlers\baddblood\game.py moveset (mkroster.py port_wm_moveset on Badd Blood's moves, abilities, move names, animation
// index and animations -- WrestleMania 2000's own code unpacking them, its other files' fingerprints WrestleMania 2000's, as
// wmanim.py's hashes.pkl has them; override {}, no place_own, no library, no refresh) + pack_moveset (pack_plain) +
// tools\handlers\imports2.py moves_json. (game.py's remap check -- tools\handlers\work\baddblood\remap.json: every animation
// Badd Blood changed is read from Badd Blood itself -- is a developer check of the Python's, true here by construction.)
struct WmPortedMoveset { std::string moves_json, packed_hex; int fell = 0; };
WmPortedMoveset baddblood_moveset(Roms& R, const fs::path& ri_data_dir, const WmRecord& e, int reference_ordinal,
                                  const std::string& key, const std::string& about);
extern const char* const VADER_MOVES_ABOUT;      // (imports2.py vader's moves.json "about")
extern const char* const GOLDUST_MOVES_ABOUT;    // (imports2.py goldust's)

// ---- gen/mkroster.py's module-level machinery, shared with the built-in ports' tables (ri_ports) -------------------------
// No Mercy at its Superstar Select (EV: its own moveset unpacking / packing, its packing's value lists = LAYOUT / VALS / CAN,
// its movesets = van_sets), the move names (names.pkl), WrestleMania 2000's moves by their very animations (wmanim.py nm_move)
// and by its own unpacking (wm2k.py unpack_moves), mkroster.py stand_in and port_wm_moveset -- the very code the
// WrestleMania 2000 folders use (WmMoveLib), so the two can never drift apart.
std::string mkroster_norm(const std::string& latin1);   // (mkroster.py norm: lower-case letters and digits only)
class Emu;
struct MoveSlot { int m, f2, f3, f4; bool operator==(const MoveSlot& o) const { return m == o.m && f2 == o.f2 && f3 == o.f3 && f4 == o.f4; } };
typedef std::pair<std::vector<MoveSlot>, std::vector<int>> MovesetUnpacked;   // 292 slots, the abilities
class MkRosterBase {
public:
    // Needs the No Mercy ROM (Error otherwise); WrestleMania 2000's for its parts (wm_nm_move, wm_unpack, port_wm_moveset: c2hands
    // 2026-11-19, without it they mustn't be asked for). ri_data_dir: tools/romimport (loadmaps, names_index.json).
    MkRosterBase(Roms& R, const fs::path& ri_data_dir);
    ~MkRosterBase();
    Emu& ev();                                          // (mkroster.py EV: its memory is the game's tables while it runs)
    const Bytes& van_set(int o) const;                  // van_sets[o]: 0xD8 bytes (empty: None -- ordinal 14)
    MovesetUnpacked nm_unpack(const uint8_t* moves, const uint8_t* params);   // (unpack(EV, VAN_UNPACK, ...))
    Bytes nm_pack(const std::vector<MoveSlot>& slots, const std::vector<int>& ab);   // (pack: 0xB8 + 0x20 bytes)
    MovesetUnpacked van_unpacked(int o);
    const std::vector<std::pair<int, std::vector<int>>>& layout() const;      // (LAYOUT: slot, its four lists)
    const std::vector<std::vector<int>>& vals() const;                        // (VALS: each list's values)
    bool can_has(int slot) const;                       // (slot in CAN)
    bool in_can(int slot, int v) const;                 // (v in CAN[slot]; False when the slot isn't in CAN)
    std::vector<int> can_order(int slot) const;         // (CAN[slot] in the Python set's own iteration order)
    const std::vector<int>& can_kind(int slot) const;   // (sorted(x for x in CAN[slot] if x not in (0, 0xFFFF)))
    // mkroster.py stand_in(slot, name, ref): ref / the result INT_MIN for Python's None. name: Latin-1.
    int stand_in(int slot, const std::string& name, int ref);
    std::vector<int> wm_nm_move(int m);                 // (wmanim.py nm_move(m)[0])
    MovesetUnpacked wm_unpack(int mv_file, int ab_file);  // (wm2k.py unpack_moves on WrestleMania 2000's files)
    // mkroster.py port_wm_moveset(key, mv_file, ab_file, ref_ord, {}, report)
    void port_wm_moveset(const std::string& key, int mv_file, int ab_file, int ref_ord, std::vector<std::string>& report,
                         std::vector<MoveSlot>& out, std::vector<int>& ab);
    const std::map<int, std::string>& vnames() const;   // (names.pkl's No Mercy names, Latin-1)
    std::string vname(int m) const;                     // (vnames.get(m, ""))
    struct Impl;
private:
    std::unique_ptr<WmMoveLib::Impl> p_;
};

}  // namespace ri
