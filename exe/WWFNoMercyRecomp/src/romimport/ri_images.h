// ri_images.h (phase3, 2026-11-19): RAM images made from the ROMs -- the C++ twin of tools\romimport\images.py (build),
// banks.py (place) and the images' extras of make_data.py.
//
// Several of the importer's steps read a source game's tables, code and loaded files out of an 8 MB RAM image, as the game
// has them in RAM at some screen. Nearly all of those bytes are the game's own, put in RAM by its loader; a LOAD MAP (our
// data: numbers only, tools\romimport\loadmaps\<name>.json) says where:
//   [ram address, "rom", rom offset, length]   or   [ram address, "file", file number, offset in the file, length]
// plus a few "words" the game writes at run time (each one named in the map with its reason). Some images then get what the
// game builds at run time, made by the game's own code (ri_mips) or as its code does: a music bank relocated (banks), No
// Mercy's movesets and costume records at its Superstar Select (van_sel), Revenge's costume table at boot (rev_boot).
#pragma once
#include "ri_roms.h"

namespace ri {

constexpr uint32_t IMAGE_SIZE = 0x800000;

// The image of a load map (its JSON text), from the game's ROM: Error if the ROM isn't there.
Bytes image_from_map(const std::string& map_json, Roms& R);

// A game's music bank as its sound start-up leaves it in RAM (banks.py place): -> (B, C), the header's and the ctl's address.
struct BankAt { uint32_t B, C; };
BankAt bank_place(Bytes& img, const Bytes& rom, const std::string& game);

// The named image (make_data.py IMAGES: the map's name in loadmaps\), with its extras, as make_data.py writes it.
// maps_dir: the folder of the load maps.
Bytes make_image(const std::string& name, const fs::path& maps_dir, Roms& R);
extern const char* const IMAGE_NAMES[];      // every image make_data.py makes, null-terminated
const char* image_rel_path(const std::string& name);   // where make_data.py puts it in the data folder (for the checks)

// Revenge's costume table as its boot leaves it, put into an image of Revenge's RAM (make_data.py rev_boot_extra).
void rev_boot_extra(Bytes& img);

// No Mercy Plus V3's RAM with its music bank (make_data.py: scratch\tmp\nmbank.pkl's "ram", "B", "C"), from the nm_pick image.
struct NmBank { Bytes ram; uint32_t B, C; };
NmBank nmplus_bank(const Bytes& nm_pick_image, Roms& R);

}  // namespace ri
