// ri_wmport.h (phase3 p3wmport, 2026-11-19): the mod's pack wmport.bin and its headers' text, made natively -- the C++ twin
// of mod\gen\wmport.py build_all (run twice by the developer's build with mod\gen\arenas_collide.py between: the walls'
// headers read the collision results the first pack's stage sets give; the pack itself doesn't depend on them).
//
// The pack holds, numbered from WMP_FILE0 (0x5000) in the order the Python makes them (each file once: Files):
//   - WrestleMania 2000's two ported Superstars (Jeff Jarrett, Mean Street Posse's Meanie): their four costumes' meshes,
//     pictures and palettes as WM2000's own Superstar Select builds them (ri_models' match images), WM2000's hands' pose
//     meshes and pictures (hands.c), their Titantron video frames;
//   - WCW/nWo Revenge's built-in ports (Bret Hart ... Bischoff and their second IDs): each costume's parts as Revenge's own
//     match loader leaves them (ri_models' Revenge captures), the hands made WM2000's (hands.c);
//   - No Mercy's hands and skin on every ported model (nm_skin: the user's call 0.35.12);
//   - the Revenge move animations' file numbers (with WMPORT_REV_ANIMS their bytes too: the "with_rma" pack revlib reads);
//   - the arcade's arenas (ri_wmarenas.cpp: Revenge's six, WrestleMania 2000's seven, No Mercy's own ten made detailed).
// The headers (mod src\*.h) are the tables the mod is compiled with: here their TEXT, exactly as the Python writes them
// (text mode on Windows: CRLF line ends), for the mod's extras holes (ri_hdrfill) and the developer's byte comparison.
#pragma once
#include "ri_akimodel.h"
#include "ri_models.h"
#include "ri_json.h"
#include "ri_hdrfill.h"
#include <map>
#include <optional>

namespace ri {

constexpr uint32_t WMP_FILE0 = 0x5000;

// The arenas' collision results (mod\gen\arenas_collide.py result_of, as data\stagecollide\result\<n>.json holds them), one
// per arena 0..22 (the game's ten, then the arcade's): what the walls' headers are made from. The JSON text of each.
typedef std::function<std::map<int, std::string>(const Bytes& pack, const std::map<std::string, std::string>& headers)> CollideFn;

struct WmportIn {
    std::string rev_anims_json;   // data\gen_data\rev_anims.json (gen/revanims.py's output: an input of this step)
    bool with_rma = false;        // WMPORT_REV_ANIMS=1: the Revenge move animations' bytes in the pack (revlib's pack)
    CollideFn collide;            // the collision step (ri_collide); empty: no walls headers
    std::map<int, std::string> collide_results;   // ... or its results given (the developer's check), when collide is empty
    // (arenasplit 2026-11-24) the build's layouts (firstrun\hdrfill.bin): with one of WCW/nWo Revenge and WrestleMania 2000 the
    // other's arenas' rows are written as zeros of the build's shape (hdr_leaves_in), so the rows of the rest keep their places
    const HdrFill* shape = nullptr;
};

struct WmportOut {
    Bytes pack;                                              // build\wmport.bin
    std::vector<std::pair<std::string, std::string>> headers;   // (file name, text with CRLF line ends) in the Python's order
    std::vector<std::pair<std::pair<std::string, int>, Bytes>> models;   // wmport_models.pkl: (name, costume) -> descriptor
    std::vector<std::pair<std::string, int>> videos;         // wmport_videos.pkl: name -> video number
    std::map<int, std::string> collide_json;                 // the collision results used (data\stagecollide\result)
    std::vector<std::string> log;
};

// Everything from the player's ROMs (No Mercy Rev 1, WrestleMania 2000, WCW/nWo Revenge); D: tools\romimport's data
// (loadmaps, frozen). Error when No Mercy is missing, or both WCW/nWo Revenge and WrestleMania 2000. (c2hands 2026-11-19)
// Without WrestleMania 2000: Revenge's part alone -- the Revenge ports with Revenge's own hands, the move animations at the
// build's row, no Jarrett / Meanie, no WM2000 hands or videos (their tables' text not written: zero holes). (arenasplit
// 2026-11-24) Without Revenge: WrestleMania 2000's part alone -- Jarrett and the Meanie, its hands and videos, no Revenge
// ports or move animations. Either way each game's own arcade arenas, the other's rows zero in the build's shape
// (in.shape), and the pack ends before the stage sets of the game's ten (the game reads content\wmport_base.bin under it).
WmportOut make_wmport(Roms& R, const RiData& D, const WmportIn& in);

// The collision step done natively (ri_collide: gen/arenas_collide.py on the first pack's stage sets): the 23 results'
// JSON text. py_source: arenas_collide.py's bytes for the results' "key" (the developer's check); null: key "" (the game).
CollideFn native_collide(Roms& R, const Bytes* py_source = nullptr);

// The whole step as the game's first start runs it: make_wmport with the native collision step. rev_anims_json: the
// revanims step's output (an input here). The 11 headers' text is in the result's headers (for ri_hdrfill).
WmportOut wmport_build(Roms& R, const RiData& D, const std::string& rev_anims_json, bool with_rma = false, const HdrFill* shape = nullptr);

std::string crlf(const std::string& lf);       // LF -> CR LF (the Python's text mode on Windows)

// ---- the built-in ports' models one man at a time (tools\romrender\mkportphotos.py renders each one's select photo) ----
// Each port's costume 1 (index 0) as gen/wmport.py builds it, its files in a pool of his own (mkportphotos: W.Files() per
// man): the 18 Revenge ports in REV_PORTED's order (rev_port_desc; not revbanks' second IDs), then WrestleMania 2000's
// Jarrett and Meanie (mkportphotos.wm_port_desc). No Mercy's skin (nm_skin) is NOT on them: the photo is of the model
// before it, as the Python renders it. Needs WCW/nWo Revenge (Error without it); with WrestleMania 2000 the Revenge ports'
// fingers are WrestleMania 2000's and Jarrett and the Meanie follow, without it (c2hands) the Revenge ports alone with
// Revenge's own hands.
struct PortModel { std::string name; Bytes desc; Pool files; };
std::vector<PortModel> port_photo_models(Roms& R, const RiData& D);

// ---- shared with the arenas' part (ri_wmarenas.cpp) ------------------------------------------------------------------
struct WmCtx {
    Roms& R;
    const RiData& D;
    Pool F;                                     // the pack's files (gen/wmport.py Files)
    const RomFiles* nm = nullptr;               // No Mercy (Rev 1)
    const RomFiles* wm = nullptr;               // WrestleMania 2000
    const RomFiles* rev = nullptr;              // WCW/nWo Revenge
    Bytes nm_rom;                               // No Mercy's ROM (the six-man ROM's data where the Python reads it)
    std::map<std::string, Bytes> images;        // RAM images by name (make_image), made once
    std::vector<std::pair<std::string, std::string>> headers;   // (name, LF text) as written
    std::vector<std::string> log;
    const Bytes& image(const std::string& name);
    const Bytes& nmf(uint32_t i) const { return (*nm)(i); }
    const Bytes& wmf(uint32_t i) const { return (*wm)(i); }
    const Bytes& revf(uint32_t i) const { return (*rev)(i); }
    const HdrFill* shape = nullptr;             // (arenasplit: WmportIn::shape)
    void header(const std::string& name, const std::string& text) { headers.push_back({ name, text }); }
};

// gen/arenas.py build (with arenas_entrance, arenas_raw, arenas_stages; arenas_ring and arenas_wm inside): the arenas' files
// into C.F, their headers into C.headers -- all but the walls' (arenas_walls: wm_walls_headers, from the collision results).
// (arenasplit 2026-11-24) With one of C.rev / C.wm: that game's arenas alone, the other's rows zero in C.shape's shape, and
// not the game's ten's stage sets (their rows are the base's: content\wmport_base.bin); with both, as always.
void wm_arenas(WmCtx& C);
// gen/arenas_walls.py build: arena_walls_files.h and arena_ringstage_files.h from the collision results (JSON text per arena)
void wm_walls_headers(WmCtx& C, const std::map<int, std::string>& results);

}  // namespace ri
