// ri_goldust.h (phase3 p3goldust, 2026-11-19): the "Imported" collection's Goldust -- the C++ twin of tools\handlers\imports2.py
// goldust(), the customs case:
//   looks + model  No Mercy Plus V3's Create-a-Wrestler Goldust parts (its SmackDown Mall's "Goldust 1" / "Goldust 2": ring
//                  attire, boots, face paint) on No Mercy Plus's own Test, his model the one No Mercy Plus's own builder makes
//                  of those looks (run in ri_mips on its Superstar Select RAM image, make_image "nm_pick"):
//                  tools\handlers\handlers\customs\nmplus.py (NMPlus, goldust_apps, build, tame, set_app)
//   photo          his face paint's picture made by the renderer's integer steps (handlers\aki\photo.py photo_from_picture_rr)
//   the rest       No Mercy Plus has no Goldust Superstar, so his name, size, moveset, theme and Titantron are Badd Blood's
//                  Goldust's (its roster's 32nd): ri_wmroster, ri_wmmoves baddblood_moveset, ri_wmmedia -- as Vader's
// The mod's remap (modcheck.py tables -> handlers\aki\modtables.py Remap): every answer goldust's looks ask of it is frozen
// in tools\romimport\frozen\nmplus_goldust_remap.json (see ri_goldust.cpp: why, and its developer check).
#pragma once
#include "ri_models.h"

namespace ri {

// <out>\goldust\ (out: the "Imported" collection folder). Needs the No Mercy, No Mercy Plus V3 and Badd Blood ROMs (Error
// otherwise: the caller checks they're there). D.dir: the importer's data folder (loadmaps\, frozen\).
void goldust_folder(Roms& R, const RiData& D, const fs::path& out);

}  // namespace ri
