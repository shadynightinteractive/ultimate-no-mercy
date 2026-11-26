// ri_imports2.h (phase3 p3folders, 2026-11-19): the "Imported" collection -- the C++ twin of tools\handlers\imports2.py:
//   vader     WWF WarZone - Badd Blood's Vader (a WrestleMania 2000 mod: the WrestleMania 2000 handler's steps on its ROM --
//             tools\handlers\handlers\baddblood\game.py), his model as Badd Blood builds it in a match (ri_models
//             "baddblood"), photo rendered from it, theme and Titantron from the ROM (ri_wmmedia), looks Viscera's plain ones.
//   goldust   No Mercy Plus V3's Goldust parts: not ported yet.
// NOT HERE YET: Vader's moveset (baddblood\game.py moveset = mkroster.py port_wm_moveset on Badd Blood's files, pack_plain):
// it plugs in through VaderMoveset.
#pragma once
#include "ri_wmimport.h"
#include <functional>

namespace ri {

struct VaderMoves { std::string moves_json, packed_hex; int fell = 0; };
typedef std::function<std::optional<VaderMoves>(Roms&, const WmRecord&, int reference_ordinal)> VaderMoveset;

// <out>\vader\ (out: the "Imported" collection folder). Error when the Badd Blood ROM isn't there.
void vader_folder(Roms& R, const RiData& D, const fs::path& out, const VaderMoveset& moveset = nullptr);

}  // namespace ri
