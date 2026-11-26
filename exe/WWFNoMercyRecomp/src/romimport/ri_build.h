// ri_build.h (phase3, 2026-11-19): the ROM importer's first-start step -- what the game builds from the other games' ROMs
// in roms\ (WCW/nWo Revenge, WrestleMania 2000, No Mercy Plus V3, Badd Blood, the Revenge Redux patch), natively.
//
// Called by the first-start builder (src\main\arcade_firstrun.cpp) before it makes the mod. Two kinds of output, two rules
// (PHASE3 design, the user's rules of 2026-11-18):
//  * the PLAYER'S CONTENT -- wrestler folders (their models, photos, themes, moves) -- made ONCE per source ROM, in a
//    temporary folder, then moved into wrestlers\<collection>\ only under a free name (ri_place: a clash -> a new name and a
//    fresh ID, said in content\import_report.txt); never made again, never written over; a folder the player removed stays
//    removed. content\reimport.txt makes them again (it is removed by the mod builder once everything is made).
//  * THE GAME'S OWN PACKS -- content\content.bin, wmport.bin, movelib_*.bin, modextra.bin (the mod's extras tables) -- made
//    from the SET of source ROMs there; made again (whole: a temporary file, then renamed) only when that set changes.
// What was done is recorded in content\firstrun.json under "romimport" (and "redux_themes"), so a start with nothing new
// does nothing and writes nothing.
#pragma once
#include "ri_roms.h"
#include "json/json.hpp"
#include <functional>
#include <map>

namespace ri {

// The sources the importer knows (one bit each in the mod's extras flag: mod src arcade.h EXTRAS_*).
// (no2k15, 2026-11-23: 16, SRC_HD2K15 -- the two WWE 2K15 test slots' model packs -- is gone with them)
enum : uint32_t { SRC_REVENGE = 1, SRC_WM2000 = 2, SRC_NMPLUS = 4, SRC_REDUX = 8 };
uint32_t sources_present(Roms& R);           // the bits of the source ROMs the player has (No Mercy itself not counted)

// (startup 2026-11-19) What the importer says as it goes, for the start-up status window (src/main/arcade_startup_status.h):
// a line of the list (Found .. Note), or its progress bar (Plan: `text` is the number of steps to add; Step: one done).
enum class Say { Found, Making, Added, Removed, Note, Plan, Step };
const char* game_title(const std::string& game);   // a source ROM's name for people: "nmplus_v3" -> "No Mercy Plus V3 ..."

struct BuildPaths {
    fs::path game;          // the game's folder
    fs::path roms;          // roms\ (the ROMs, any byte order or .zip; patches)
    fs::path content;       // content\ (packs, firstrun.json, import_report.txt)
    fs::path wrestlers;     // wrestlers\ (the player's wrestler folders)
    fs::path data;          // firstrun\romimport\ (our data: loadmaps\, frozen\, the handlers' tables)
    std::function<void(Say, const std::string&)> say = {};   // (startup) empty: nothing said (ri_cli)
};

struct BuildResult {
    uint32_t sources = 0;           // what is there
    uint32_t packs_built_for = 0;   // the set the packs in content\ are made from (as recorded)
    bool changed = false;           // something written this start
    std::vector<std::string> log;   // for the game's log ([romimport] lines)
};

// rec: content\firstrun.json as the first-start builder holds it (its "romimport" / "redux_themes" keys read and updated
// here); save: writes it out (called after each finished part, so a stop half way keeps what was done).
BuildResult romimport_build(const BuildPaths& P, nlohmann::json& rec, const std::function<void()>& save);

// The 11 headers' text (file name, CR LF text as the Python writes them) of the wmport.bin this start made (empty if it
// made none): wmport_files.h, rev_move_anims.h and the nine arena_*_files.h -- the mod's extras (ri_hdrfill) read them.
const std::vector<std::pair<std::string, std::string>>& wmport_headers_made();

// The mod's extras holes made from the player's ROMs (every generated header the holes read, then ri_hdrfill over
// firstrun/hdrfill.bin), as content/modextra.bin -- what the pack maker "modextra.bin" writes. texts: the headers' text it used
// (by name). The steps' state is kept between calls until made_reset() (the pack makers share it).
Bytes modextra_made(Roms& R, const BuildPaths& P, std::map<std::string, std::string>* texts);
// ... its two halves: the headers' text made from the ROMs (by name), and the fill of firstrun/hdrfill.bin's holes from texts
// (a hole whose header isn't among them: zero; arcade_extras_in: bits).
const std::map<std::string, std::string>& extras_texts(Roms& R, const BuildPaths& P);
struct HdrFill;
Bytes modextra_from_texts(const HdrFill& F, const std::map<std::string, std::string>& texts, uint32_t bits);
void made_reset();

}  // namespace ri
