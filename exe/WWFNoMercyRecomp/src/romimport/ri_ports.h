// ri_ports.h (phase3 p3ports, 2026-11-19): the mod's game-data tables for its BUILT-IN PORTS -- the Superstars the arcade adds
// to No Mercy's roster from the other games: No Mercy Plus V3's (Funaki, Gangrel, the Headbangers, Droz, Mideon, the Mean Street
// Posse, the 1999 Undertaker, Raven, Regal, Tiger Ali Singh, Big Show's moveset), WrestleMania 2000's (Jeff Jarrett, The Blue
// Meanie) and WCW/nWo Revenge's (Bret Hart .. Eric Bischoff, their second IDs) -- made natively from the player's ROMs, the
// C++ twin of the chain's steps
//   revanims    mod\gen\revanims.py   -> data\gen_data\rev_anims.json (wmport.bin's input) + movex_data.h
//   mkroster    mod\gen\mkroster.py   -> mod\src\roster_ext.h
// (and of what they call: data\revport\revmoves.py, mod\gen\revbanks.py, wm2k.py, wmanim.py through ri_wmmoves' MkRosterBase).
//
// The texts are written as the Python writes them (text mode on Windows: CR LF line ends), byte for byte.
// Our data (tools\romimport, shipped as firstrun\romimport): loadmaps\ (the RAM images), names_index.json (the move names),
// frozen\rev_moveset_picks.json "positions" (revmoves.slot_positions: where each No Mercy slot sits in a Revenge list) and
// frozen\rev_port_picks.json (the FREEZE RULE: the ports' feature-distance choices -- revanims_freeze.py makes and checks it).
#pragma once
#include "ri_roms.h"
#include <memory>

namespace ri {

struct HdrFill;

class Ports {
public:
    // R: the player's ROMs; ri_data_dir: tools\romimport (firstrun\romimport in the game).
    Ports(Roms& R, const fs::path& ri_data_dir);
    ~Ports();

    // gen/revanims.py: the Revenge ports' stand-in moves played with their own Revenge animations. Needs No Mercy, Revenge
    // and WrestleMania 2000 (mkroster's machinery reads WrestleMania 2000's tables at its start). Error when one is missing.
    struct RevAnims {
        std::string json;        // data\gen_data\rev_anims.json (CR LF text)
        std::string movex_h;     // data\gen_data\movex_data.h (CR LF text)
        std::vector<std::string> log;   // what the Python prints (the blocks, the counts)
    };
    RevAnims revanims();

    // gen/mkroster.py: mod\src\roster_ext.h (CR LF text) -- the added Superstars' records and their packed movesets. No Mercy
    // Plus V3's rows (its four, Big Show's moveset, its men's records) when the player has it, else zeros (set_shape below).
    std::string roster_ext_h();

    // gen/mkparts.py: parts_ext.h (CR LF text) and parts.pkl's values (as JSON: keys / uns / nm_uns as hex, files {nm+: ours}).
    struct Parts { std::string parts_ext_h, parts_pkl_json; };
    Parts mkparts();

    // gen/mkovr.py: parts_ovr.h (CR LF text). wm_models: wmport's models (ri_wmport WmportOut::models = wmport_models.pkl).
    // the mod's move_lib.h (a lost producer: tools/handlers revlib.py's head over movelib.py's port lists; LF text) from
    // movelib_revenge.bin (ri_movelib) and rev_anims.json (revanims above).
    std::string move_lib_h(const Bytes& movelib_revenge_bin, const std::string& rev_anims_json);

    std::string parts_ovr_h(const std::vector<std::pair<std::pair<std::string, int>, Bytes>>& wm_models);

    // (nmplus 2026-11-19) No Mercy Plus V3 is a source game of its own. Without it, roster_ext_h / mkparts / parts_ovr_h write its
    // men's rows as zeros of the build's shape -- the layouts of firstrun\hdrfill.bin, given here -- so the others' rows keep
    // their places and bytes (an error if it is needed and not given).
    void set_shape(const HdrFill* layouts);

    struct Impl;
private:
    std::unique_ptr<Impl> p_;
};

}  // namespace ri
