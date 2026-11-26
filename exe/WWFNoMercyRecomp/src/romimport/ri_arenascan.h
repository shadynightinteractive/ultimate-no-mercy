// ri_arenascan.h (phase3, 2026-11-19): the source games' arena sets read from their RAM images and files -- the C++ twin
// of the research readers the arena port builds on: data\arenaport\stage3d\extract.py (WCW/nWo Revenge's 3D venue
// scenes: scenes.json, <scene>\meshes.json), data\wmarena\wm3d.py (WrestleMania 2000's entrance sets: wm3d_scenes.json)
// and data\wmarena\all\venues_json.py + wmtab.py (WrestleMania 2000's per-venue arena lists: wm_venues.json).
// Each returns the same JSON value the Python writes (ri_json writes it as the Python does).
#pragma once
#include "ri_roms.h"
#include "ri_json.h"

namespace ri {

extern const char* const REV_SCENE_NAMES[9];       // nitro, souledout, superbrawl, bash, havoc, starrcade, scene6..8

ojson rev_scenes(const Bytes& ent0_ram);                                   // scenes.json (from Revenge's ent0 image)
ojson rev_scene_meshes(int k, const ojson& scenes, const RomFiles& rev);  // <name>\meshes.json for scene k
ojson wm3d_scenes(const Bytes& close1_ram);                                // wm3d_scenes.json (WrestleMania 2000's close1 image)
ojson wm_venues(const Bytes& fly1_ram);                                    // wm_venues.json (WrestleMania 2000's fly1 image)

// The text as each file is written: json.dump(indent=n) in text mode on Windows (CRLF line ends) -- or one line.
std::string json_file_text(const ojson& v, int indent);

}  // namespace ri
