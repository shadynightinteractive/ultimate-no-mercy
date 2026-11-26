// ri_models.h (phase3, 2026-11-19): the source games' wrestler models, built by the source games' own code from the ROMs --
// the C++ twin of tools\romimport\wmmodels.py (WrestleMania 2000, Badd Blood) and revmodels.py (WCW/nWo Revenge).
//
// A model is what a game's own loader leaves in RAM when it loads a wrestler into a match slot: his descriptor, his parts'
// meshes, pictures and palettes, his scale. The importer and the pack builder read them from those RAM images (as they
// read the emulator rig's captures before). The games' code runs in ri_mips on images made from the ROM (ri_images); the
// plan of which men, costumes and slots each image holds is our data (tools\romimport\frozen\*.json).
#pragma once
#include "ri_images.h"
#include <functional>

namespace ri {

// Where the shipped data lives: the folder with loadmaps\ and frozen\ (tools\romimport in the source tree).
struct RiData { fs::path dir; };

// ---- WrestleMania 2000 / Badd Blood -----------------------------------------------------------------------------------
// Each output image, by its path in the data folder (wmmodels.py writes the same paths): the 8 wmport captures
// (wmport\cap\k<k>_c<c>\ram.bin), the 60 rig captures (handlers_rig\wm2000\cap\all_c*_ram.bin) and Badd Blood's Vader
// (handlers_rig\imports2\cap\vader_c<c>_ram.bin). `want` picks: "wmport", "rig", "baddblood".
void wm_models(Roms& R, const RiData& D, const std::string& want,
               const std::function<void(const std::string& rel, const Bytes& ram)>& out);

// ---- WCW/nWo Revenge ----------------------------------------------------------------------------------------------------
// The match image the Revenge loads run on (loadmaps\rev_bm.json + the boot's costume table + an empty object pool).
Bytes rev_match_image(Roms& R, const RiData& D);
// The costume table's 42-byte entry for (code, costume), as the game's 0x8002B7E0 finds it.
Bytes rev_entry(const Bytes& img, uint32_t code, uint32_t costume);
// One part-loader call the load made: (2, a0, a1, a2) as revmodels.py logs it.
struct RevCall { uint32_t a0, a1, a2; };
struct RevLoad { Bytes ram; std::vector<RevCall> calls; };
struct RevMan { uint32_t code, costume, attire, mask; };
RevLoad rev_load(const Bytes& img, const RomFiles& F, const std::vector<RevMan>& men);
// The ports' captures (frozen\revenge_caps.json "tags"): out(rel path of "<tag>_ram.bin", load) for each.
void rev_port_models(Roms& R, const RiData& D, const std::function<void(const std::string& rel, const RevLoad& load)>& out);

}  // namespace ri
