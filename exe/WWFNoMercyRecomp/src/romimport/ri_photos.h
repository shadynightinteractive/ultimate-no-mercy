// ri_photos.h (phase3 p3photos, 2026-11-19): the mod's PHOTO headers (and tv_carriers.h) made natively from the player's
// ROMs -- the Superstar Select photos of the built-in ports and the table of No Mercy's Titantron frame files. The texts
// are exactly the reference files' bytes (LF line ends: the references are LF), for the mod's extras holes (ri_hdrfill)
// and the developer's byte comparison (tools\romimport\native\photos_check.py).
//
// port_photo_headers -- the C++ twin of tools\romrender\mkportphotos.py:
//   rev_photos.h     the 18 Revenge ports' photos, RENDERED (ri_akimodel photo_from_model) from each one's costume-1 model
//                    as gen/wmport.py builds it (ri_wmport port_photo_models)
//   jarrett_photo.h  Jeff Jarrett's (WrestleMania 2000's man 29), the same way
//   meanie_photo.h   The Blue Meanie's (WrestleMania 2000's man 41)
//   Needs No Mercy (Rev 1: the renderer reads its files where a model names one of them) and WrestleMania 2000; rev_photos.h
//   needs WCW/nWo Revenge too. Without No Mercy or WrestleMania 2000: none of the three; without Revenge: the two
//   WrestleMania 2000 ones only.
//
// nm_photo_headers -- the "lost producers" (headers no chain step makes; their rules found again, see ri_photos.cpp):
//   photos_nm.h      No Mercy Plus V3's photos for the seven No Mercy Plus Superstars after the Meanie (mod\gen\mkphotos.py)
//   mideon_photo.h   No Mercy Plus V3's photo of Mideon      } its Superstar Select photo files: the same rule as
//   droz_photo.h     No Mercy Plus V3's photo of Droz        } mkphotos.py's, for its ordinals 79, 75 and 5
//   bigshow_photo.h  No Mercy Plus V3's photo of Big Show    }
//   tv_carriers.h    No Mercy's own Titantron frame files the videos' frames are shown in place of, and 32 bytes of each
//                    one's palette
//   The four photo headers need No Mercy Plus V3 (No Mercy v1.0 + its patch: its files and its RAM image at the Superstar
//   Select, loadmaps\nm_pick.json); tv_carriers.h needs No Mercy (Rev 1). A header whose source ROM is absent is left out.
//
// Each function returns the (file name, text) pairs it can make, in the order above; the caller fills the holes of the
// ones left out with zero (the mod's extras: that source's Superstars aren't offered).
#pragma once
#include "ri_models.h"
#include <string>
#include <utility>
#include <vector>

namespace ri {

std::vector<std::pair<std::string, std::string>> port_photo_headers(Roms& R, const RiData& D);
std::vector<std::pair<std::string, std::string>> nm_photo_headers(Roms& R, const RiData& D);

}  // namespace ri
