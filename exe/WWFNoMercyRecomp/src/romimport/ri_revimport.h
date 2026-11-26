// ri_revimport.h (phase3 p3revfolders, 2026-11-19): WCW/nWo Revenge's never-ported Superstars as standard wrestler folders --
// the C++ twin of the chain's two Revenge steps (tools\handlers\import_all.ps1):
//     aki_import.py revenge --all --rest-only --legacy-ids 0x0340 --records <frozen\records_damage.json>
//     aki_import.py revenge --refresh
// with the Revenge handler's parts (tools\handlers\handlers\revenge\game.py roster / slug / build_model, body.py, fixes2.py
// skipped, theme.py write) and the models Revenge's own code builds (tools\romimport\revmodels.py auto_plan + its "auto" loads:
// ri_models rev_load, ri_capture rev_capture).
//
// Per man (every Revenge Superstar who is not one of the 24 built-in ports, has a moveset file, and isn't a record Revenge
// draws another man for): his model as Revenge builds it in a match (four costumes), made a No Mercy model with No Mercy's
// hands in his own skin (ri_akimodel); his photo rendered from it; his record (name, height, weight, costumes), his body (No
// Mercy body type and size by the rule) and his looks; Revenge's own entrance theme (0x214) as his folder theme.
//
// NOT HERE: the moveset (moves.json, the packed "moveset", the notes "source"."win_pose" and "source"."abilities") -- ported
// separately; its results plug in through RevMovesetFn (a struct of optional pieces put where the Python puts them).
#pragma once
#include "ri_akimodel.h"
#include "ri_capture.h"
#include "ri_json.h"
#include <functional>
#include <optional>

namespace ri {

// Where the importer's own data is (all ours: numbers, names, choices):
struct RevImportData {
    RiData ri;                    // tools\romimport (loadmaps\, frozen\revenge_caps.json)
    fs::path handler_dir;         // tools\handlers\handlers\revenge (wrestlers\*.json, legacy_ids.json, skip.json, model_fixes.json)
};

// ---- game.py roster(): Revenge's Superstars as its boot builds them ------------------------------------------------------
struct RevCostume { int mask, attire; std::string colours; };          // (the costume table's bytes 36, 37, 38-40 as hex)
struct RevBody { uint32_t word; int frame; std::optional<int> frame_attires; int scale_class; double scale; };   // body.py Tables.body
struct RevRecord {
    uint32_t code;                // the model code its loader takes (group << 8 | row + 1)
    int index;                    // the select index (the costume table's row)
    std::string name, short_name; // UTF-8 (Revenge's text is latin-1), Revenge's marks taken out
    std::optional<int> height;    // inches over 5'0" (none: 0'0" on the record)
    std::optional<int> weight;    // lbs - 100 (none: 0 lbs)
    RevCostume costumes[4];
    std::optional<int> moveset_file;   // his files f .. f + 2 (rev_movesets: by (group, row))
    RevBody body;
};
std::vector<RevRecord> rev_roster(Roms& R, const RiData& D);
std::string rev_slug(const std::string& name);                      // game.py slug: the folder's string ID

// ---- the models (revmodels.py auto_plan + "auto") ------------------------------------------------------------------------
// {(code, costume): {part: (mesh file, picture, palette)}} for every Superstar not ported with a moveset file, each costume,
// loaded two a run (costume-major) as the rig's capture_all.py did; the managers with no model of their own load the frozen
// stand-ins (frozen\revenge_caps.json "auto_stand_ins").
typedef std::map<std::pair<uint32_t, int>, std::map<int, RevPart>> RevAutoCaps;
// want: only the runs holding these codes (empty: all; a run's second slot depends on its first, so runs are kept whole).
RevAutoCaps rev_auto_caps(Roms& R, const RevImportData& D, const std::vector<RevRecord>& roster, const std::set<uint32_t>& want = {});

// game.py build_model for a man with captures ("got"): his four costumes' descriptors, files numbered by his own pool.
// woman: No Mercy's women's hands. report: lines as the Python's report gets them (the skin and face steps' outcome).
struct RevModel { std::vector<Bytes> descs; Pool pool; };
RevModel rev_build_model(Roms& R, const RevImportData& D, const std::string& sid, const std::map<int, RevPart>* got[4],
                         bool woman, std::vector<std::string>& report);

// ---- the moveset's pieces (ported elsewhere): what the folder takes from them ------------------------------------------------
struct RevManInfo {
    const RevRecord* e;
    std::string sid;
    int reference_ordinal;        // 4 Kane (300 lbs and up), 1 The Rock, 51 Stephanie McMahon for a woman
    int body_type;                // No Mercy's body type (15: a woman)
};
struct RevMovesetPiece {
    std::optional<std::string> moveset;     // wrestler.json "moveset": the packed moveset's hex (absent: packing failed)
    std::optional<std::string> win_pose;    // "source"."win_pose" (fixes2.win_pose's note), when he had one
    std::optional<std::string> abilities;   // "source"."abilities" (ABIL_WHY), when his abilities were repacked
    std::optional<std::string> moves_json;  // moves.json's text as the folder has it (absent: no moves.json)
};
typedef std::function<std::optional<RevMovesetPiece>(const RevManInfo&)> RevMovesetFn;

// ---- the collection -----------------------------------------------------------------------------------------------------------
// <out>\<sid>\ for every man (only: those string IDs, empty = all): model\, photo.bin, theme.bin / theme.meta, wrestler.json
// (+ moves.json and the moveset's fields when moveset gives them). Lines for an import report go to log.
struct RevFoldersResult { std::vector<std::string> made, log; };
RevFoldersResult revenge_folders(Roms& R, const RevImportData& D, const fs::path& out, const std::vector<std::string>& only = {},
                                 const RevMovesetFn& moveset = nullptr);

}  // namespace ri
