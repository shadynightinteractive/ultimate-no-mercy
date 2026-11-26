// ri_wmimport.h (phase3 p3folders, 2026-11-19): WWF WrestleMania 2000's roster as standard wrestler folders -- the C++ twin
// of tools\handlers\wm2k_import.py (one(): every Superstar but the two the game has built in, Jeff Jarrett and The Blue
// Meanie, whose folders the Python makes and then sets aside) followed by tools\handlers\wm2k_refresh.py, with the
// WrestleMania 2000 handler's steps (tools\handlers\handlers\wm2000\game.py).
//
// Per man (his fixes: tools\handlers\handlers\wm2000\wrestlers\<string id>.json, our data): his model as WrestleMania 2000
// builds it in a match (ri_models' rig loads, read by ri_capture), made a No Mercy model with No Mercy's hands in his own
// skin (ri_akimodel); his photo rendered from it; his name, size, body type, looks, moveset, theme and Titantron from the ROM.
#pragma once
#include "ri_akimodel.h"
#include "ri_capture.h"
#include "ri_wmroster.h"

namespace ri {

// Where the importer's own data is (all ours: numbers, names, choices):
struct WmImportData {
    RiData ri;                    // tools\romimport (loadmaps\, frozen\)
    fs::path fixes_dir;           // tools\handlers\handlers\wm2000\wrestlers (<sid>.json)
    fs::path roster_ids;          // mod\gen\roster_ids.json (string ID -> game number: "based_on")
};

// One man's model: his four costumes' descriptors (files numbered by the pool) and the pool.
struct WmModel { std::vector<Bytes> descs; Pool pool; double scales[4]; };

// The rig captures read once (ri_models wm_models "rig"): (roster index, costume) -> his capture and his scale.
struct WmCaps {
    struct One { WmCapture cap; double scale; };
    std::map<std::pair<int, int>, One> by;
    std::map<std::pair<int, int>, std::pair<std::string, int>> index;   // (k, c) -> (capture file, slot): the folder notes it
};
WmCaps wm_rig_caps(Roms& R, const RiData& D);

// The WrestleMania 2000 handler's model steps on any WrestleMania 2000-engine game (wm2000\game.py build_model_caps;
// baddblood\game.py build_model with wm_rules false: no women's hands, no waiting for another costume's skin): his four
// costumes' captures (him in each), each one's hands' kinds table, his record (the looks' first byte picks the kind).
WmModel wm_like_model(const RomFiles& files, const RomFiles& nm, const std::vector<const WmCapture*>& caps,
                      const std::vector<const uint8_t*>& kinds, const WmRecord& e, const std::string& sid, bool wm_rules);
// build_model_caps: his four costumes from his captures, No Mercy's hands in his skin, wrists fitted, palettes padded.
WmModel wm_build_model(Roms& R, const WmCaps& caps, const WmRecord& e, const std::string& sid);

// aki_import.py base_looks(ordinal): No Mercy's Superstar's four costumes' looks (van: the "van_sel" image); imports2.py
// plain_looks(body, photo, costume): the plain costume an import with a model of his own gets
std::vector<std::vector<int>> nm_base_looks(const Bytes& van, const RomFiles& nm, int ord);
std::vector<int> nm_plain_looks(int body, int photo, int c);

// The whole collection: <out>\<sid>\ for every man (only: those string IDs, empty = all). Pieces not ported yet are
// skipped and said on stdout.
void wm2000_folders(Roms& R, const WmImportData& D, const fs::path& out, const std::vector<std::string>& only = {});

}  // namespace ri
