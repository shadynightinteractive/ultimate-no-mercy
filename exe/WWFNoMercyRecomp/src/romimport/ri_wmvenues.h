// ri_wmvenues.h (phase3 p3wmport, 2026-11-19): WrestleMania 2000's seven venues as arcade arenas -- the C++ twin of
// mod\gen\arenas_wm.py (its data side: no pack numbers here). gen/arenas.py, arenas_ring.py and arenas_entrance.py put
// these meshes and pictures into the pack in their own order (ri_wmarenas.cpp does that); this gives them, per venue:
// the match's stage set (stage_parts), the entrances' close scene (close_parts), everything round the ring (ring_lists),
// the pieces the ARENA page photo / VS backdrop are rendered from (rr_pieces) and the Titantron rows (tron_rows).
//
// Meshes are given as No Mercy mesh FILES (arenas_wm.encode: the plain form, or the alternate 16-bit-texel form), pictures
// as (texture file, palette file) bytes (arenas_wm._pic: "pic" tuples, WM2000 palette files + 1, "hook", "white").
#pragma once
#include "ri_roms.h"
#include "ri_json.h"
#include <optional>

namespace ri {

struct WmPic { Bytes tex, pal; };
struct WmPiece { std::string name; Bytes mesh; WmPic pic; };
// a crowd list item: its mesh and its two frames' pictures (a static list: the same picture twice)
struct WmCrowdItem { Bytes mesh; WmPic a, b; };

struct WmVenue {
    int v = 0;
    std::string name;                         // VENUES[v]["name"]
    bool has_screen = false;
    std::vector<WmPiece> stage;               // stage_parts(v): (name, encode(mesh), _pic(picture)) in drawing order
    std::vector<WmPiece> close;               // close_parts(v)
    // ring_lists(v), each mesh encoded; the cage's copies (caged() / the corner moved (-28, +28)) beside
    WmPiece A; Bytes A_cage;                  // list A (the mat): mesh, its picture; the cage's mesh
    std::vector<WmPiece> apron; std::vector<Bytes> apron_cage;
    std::vector<WmPiece> floor; std::vector<Bytes> floor_cage;
    std::vector<WmPiece> barricade;
    std::vector<WmPiece> corner; std::vector<Bytes> corner_cage;
    std::vector<WmCrowdItem> groups[5];       // GROUPS order: stands (one picture: a == b), g1, g2, g3, g4
    Bytes empty;                              // encode(empty())
    // the ARENA page's renders' pieces (rr_pieces): (mesh file, texture, palette, quarter turns)
    struct RrPiece { Bytes mesh, tex, pal; int turns; };
    std::vector<RrPiece> rr;
    Bytes logo_tex, logo_pal;                 // the page photo's logo box: wfile(logo + 1), wfile(logo)
    // tron_rows(k, v): the screen's extent in the match set's and the close scene's mesh, its picture's fold
    int tron_match[4] = {}, tron_close[4] = {}; uint32_t sig[2] = {};
};

// venue v (0..6) from the player's WrestleMania 2000 files; scenes / venues: wm3d_scenes.json / wm_venues.json's values
// (ri_arenascan wm3d_scenes / wm_venues); frozen_dir: tools\romimport\frozen (the painter's drawing orders, if frozen)
WmVenue wm_venue(int v, const RomFiles& wm, const ojson& scenes, const ojson& venues, const fs::path& frozen_dir);

// tron_rows' C text lines for the arena number k (gen/arenas_wm.py tron_rows), [] without a screen
std::vector<std::string> wm_tron_rows(const WmVenue& V, int k_arena);

}  // namespace ri
