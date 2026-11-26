// The first start's ports: the mod's tables our generators COMPUTED from the game's data, computed again here from the
// player's ROM (firstrun, 2026-11-17; arcade_firstrun.cpp runs them, tools/firstrun/mkplan.py says which tables each makes).
//
// A table copied from the ROM (with bytes of ours between) is a plan's copy; a table our Python generators worked out from the
// game's files -- the game's ten arenas' collision walls and ring-and-stage maps (mod/gen/arenas_collide.py, arenas_walls.py),
// menu lettering cut and set again (mod/gen/labeltex.py) -- is game data in another form, so it can't be shipped either: it is
// made by the same computation, here, in C++, and must come out byte for byte as the build's (the plan's SHA-256 of the whole
// image checks it).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace firstrun {

// What the player's ROM gives.
struct Rom {
    std::span<const uint8_t> rom;    // WWF No Mercy (USA) (Rev 1), in its own (.z64, big-endian) byte order
    std::span<const uint8_t> six;    // the same made the six-man game's (main.cpp six_engine_init's words applied)
    std::function<const std::vector<uint8_t>&(uint32_t)> file;   // file i (1 .. 19458) of its file table, unpacked
};

// A table a port makes: its symbol in the mod, its link address (the image holds it at address - 0x81000000), its size.
struct Table {
    std::string name;
    uint32_t address;
    uint32_t size;
};

// Makes every table in `mine` (all of one port's) into `image` (the mod's image). The plan's copies are in it already, so a
// port may read the tables made that way (e.g. the stage sets' file lists): `all` names every table the plan fills (a part
// of one -- xrs_arenas' first ten entries -- under its symbol's name and address). False, and why, if it can't.
bool run_port(const std::string& port, const Rom& rom, const std::vector<Table>& mine, const std::vector<Table>& all,
              std::vector<uint8_t>& image, std::string& why);

// The game's ten arenas' stage sets: the files the mod's pack holds from 0x5000 on that mod/gen/arenas_raw.py and
// arenas_stages.py make from No Mercy's own files, made again here (arcade_firstrun_stages.cpp): file id -> bytes.
bool make_stage_files(const Rom& rom, const std::vector<Table>& all, const std::vector<uint8_t>& image,
                      std::map<uint32_t, std::vector<uint8_t>>& files, std::string& why);

// The stage sets' rows for the ROM importer's pack builder (arcade_firstrun_stages.cpp stage_rows): venue 0 RAW IS WAR
// (arenas_raw.py parts), 1-9 (arenas_stages.py parts; close_new: new_pieces). mesh: the made mesh file (empty for the
// game's own mesh game_mesh); pal: the game's palette, or -1 and the made picture (pic_tex, pic_pal).
struct StageRow {
    std::string name;
    std::vector<uint8_t> mesh;
    int game_mesh = -1, pal = -1, new_id = -1;
    std::vector<uint8_t> pic_tex, pic_pal;
};
bool stage_rows(const Rom& rom, int v, std::vector<StageRow>& rows, std::vector<StageRow>& close_new, std::string& why);

}  // namespace firstrun
