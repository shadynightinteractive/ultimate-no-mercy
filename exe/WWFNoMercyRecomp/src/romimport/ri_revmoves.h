// ri_revmoves.h (phase3 p3folders / p3wmmoves, 2026-11-19): a never-ported WCW/nWo Revenge man's moveset as his folder has it
// -- the C++ twin of the moveset parts of the chain's two Revenge steps (tools\handlers\import_all.ps1):
//     aki_import.py revenge --all --rest-only --legacy-ids 0x0340 --records <frozen\records_damage.json>
//         G.moveset (gen/mkroster.py port_rev_moveset), G.pack_plain, moves_json (the library IDs: handlers\revenge\
//         library_ids.json over revlib.py's index; --records: REC_FROM_ROM), fixes2.py own_slots / no_struggle / win_pose /
//         band_abilities / repack_abilities, the texts OWN_ABOUT, SLAM_ABOUT, WIN_WHY, ABIL_WHY
//     aki_import.py revenge --refresh
//         refresh()'s moveset part: own_slots / no_struggle / win_pose again, band_abilities by the folder's weight
// Its pieces plug into ri_revimport (RevMovesetFn): moves.json, wrestler.json "moveset", "source"."win_pose",
// "source"."abilities".
//
// Inputs: the No Mercy and Revenge ROMs (their files; the No Mercy RAM image "van_sel" and Revenge's "rev_bm", by ri_images),
// content\movelib_revenge.bin as ri_movelib makes it (no_struggle finds his slam's library ID in it), and our data:
//   tools\romimport\frozen\rev_moveset_picks.json   the heuristic choices of port_rev_moveset (FREEZE RULE: revmoves_freeze.py)
//   tools\romimport\frozen\records_damage.json      --records (movelib2's record changes)
//   tools\handlers\handlers\revenge\library_ids.json, library_index.json, nm_band_abilities.json
// NOT HERE: revlib.py's own index (work\revenge_library\rows_all.json): library_ids.json is laid over it for every one of
// the 52 men (aki_import.py load_library), so it decides no byte of their folders.
#pragma once
#include "ri_revimport.h"
#include <memory>

namespace ri {

class RevMoves {
public:
    // ri_data_dir: tools\romimport (loadmaps\, frozen\); rev_handler_dir: tools\handlers\handlers\revenge.
    RevMoves(Roms& R, const fs::path& ri_data_dir, const fs::path& rev_handler_dir);
    ~RevMoves();
    // His moveset's pieces (RevMovesetFn): nullopt when he has no moveset file.
    std::optional<RevMovesetPiece> man(const RevManInfo& m);
    // As a RevMovesetFn for revenge_folders (this object must outlive the call).
    RevMovesetFn fn() { return [this](const RevManInfo& m) { return man(m); }; }
    struct Impl;
private:
    std::unique_ptr<Impl> p_;
};

}  // namespace ri
