// ri_collide.h (phase3 p3collide, 2026-11-19): every arena's ramp and stage made walkable from its models -- the C++ twin of
// mod/gen/arenas_collide.py, byte for byte.
//
// For each arena (0 .. 9 the game's ten, 10 .. 22 the arcade's WrestleMania 2000 / Revenge ones) the stage set's triangles
// (the models the match draws there) and its crowd's are worked into:
//   - the walkable cells (CELL units) from the aisle's mouth up onto the stage, round what's in a man's way;
//   - the walls round them: one open path from (-140, 530) round the stage back to (140, 530), and a closed loop round each
//     prop; each wall a crash (a model behind it) or a drop (the floor's edge);
//   - the running game's map (GRID units: the ground's height, or not walkable), the walkable box, how far back and how
//     high the crowd by the aisle comes, the ground's lowest and highest.
// mod/gen/arenas_walls.py lays these out as the game's records (the first start's "walls" / "ringstage" ports for the ten:
// src/main/arcade_firstrun_ports.cpp uses the computation here).
//
// Two parts:
//   ri_collide.cpp     the computation (result_of and what it calls) -- no other part of the importer needed, so the
//                      first start's ports and their test (tools/firstrun/port_test.ps1) build it on its own;
//   ri_collide_io.cpp  arena_list (the models' file lists from the mod's headers), Models (the files' bytes: the pack
//                      wmport.bin and No Mercy's own), key_of, and the result file's JSON text (data/stagecollide/
//                      result/<n>.json, json.dump's exact text) both ways.
//
// Byte for byte means Python's arithmetic exactly: float64 in the same order, // as floor division (on floats too), round()
// as banker's rounding, int() truncating, min() / max() keeping the FIRST extreme. Nothing may be fused (a*b+c in one
// rounding): ri_collide.cpp says so with a pragma, and the builds don't use -ffast-math.
//
// Python's set order: edges_of goes through the set of walkable cells in CPython's hash order, which orders the wall
// pieces leaving each corner -- that order would only decide anything where a path or loop STARTS at a corner with two
// ways on (or two ways turn alike). Here the cells go row by row instead, and trace refuses (false, why) if such a corner
// ever comes up; for all 23 arenas none does (developer check: every edge list shuffled, the Python's results unchanged).
#pragma once
#include <array>
#include <climits>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ri {
class Roms;
namespace collide {

// ---- arenas_collide.py's constants -------------------------------------------------------------------------------------
constexpr int CELL = 20;                     // the walkable cells' size
constexpr int GRID = 40;                     // the running game's map's cells
constexpr int STEP = 24;                     // the rise a man steps up between neighbouring cells
constexpr int BODY0 = 12, BODY1 = 110;       // what a man runs into, above his feet
constexpr int LOW_TOP = 170;                 // a top lower than this over the floor: a box
constexpr double UP = 0.70;                  // a triangle's normal's y: a floor
constexpr int X0 = -2400, X1 = 2400;         // the cells' x range
constexpr int AISLE_X = 140, MOUTH_Z = 530, DOOR_LINE = 800;   // the game's aisle: x +-140; the barricade's line; the old door
constexpr int ROOM = 3;                      // cells: the narrowest a man goes through
constexpr double SIMPLIFY = 14.0;            // units: a wall may stray this far from the cells' staircase
constexpr int BUMP = 2;                      // cells: within this of a blocked cell, a wall is a crash
constexpr int BEHIND = 90;                   // units: a model this close behind a wall makes it a crash
constexpr int BACK_MARGIN = 60;              // units: how far behind the set's last model the floor still counts

// ---- Python's arithmetic, exactly --------------------------------------------------------------------------------------
double py_floordiv(double a, double b);      // a // b on floats (CPython float_floor_div)
int floor_index(double a, double b);         // int(a // b)
long long py_idiv(long long a, long long b); // a // b on ints
long long py_round(double x);                // round(x): halves to even
inline double py_min(double a, double b) { return b < a ? b : a; }   // min(a, b): the first unless the second is smaller
inline double py_max(double a, double b) { return b > a ? b : a; }
double int_hypot(long long dx, long long dz);   // math.hypot of two ints

// ---- the models' triangles ---------------------------------------------------------------------------------------------
using Vec3 = std::array<double, 3>;   // a point (x, y, z): whole numbers for a model's, or a cut's
using Tri = std::array<Vec3, 3>;
using Pt2 = std::array<double, 2>;    // a point seen from above (x, z)
using IPt = std::pair<int, int>;      // a wall's corner (x, z), or a cell (i, k)

// arenas_collide.py mesh + Models.tris for one model file: its triangles (z moved by dz; with zmin, only those reaching
// it) added to out. False if the file is too short for what its header says.
bool add_tris(const std::vector<uint8_t>& d, int dz, std::optional<int> zmin, std::vector<Tri>& out);

// ---- arenas_collide.py result_of ---------------------------------------------------------------------------------------
constexpr long long NO_CELL = LLONG_MIN;   // a map cell nobody walks on (Python's None)

struct Wall {
    IPt a, b;     // from a to b, the walkable side on the right
    int crash;    // 1 a crash (state 0x15), 0 a drop (teeter)
};

// One arena's result: the fields of data/stagecollide/result/<n>.json (in its key order).
struct Collide {
    std::vector<Wall> path;                 // "path": the open path round the stage
    std::vector<std::vector<Wall>> props;   // "props": a closed loop round each prop (all crashes)
    int front = 0;                          // "front": where the aisle's sides end (z)
    int gx0 = 0, gz0 = 0, gw = 0, gh = 0;   // "grid" x0, z0, w, h: the map's GRID cells from (gx0, gz0), gw x gh
    std::vector<long long> cells;           // "grid" cells, row by row: the ground's height (rounded), or NO_CELL
    int bbox[4] = {};                       // "bbox": the walkable cells' x0, x1, z0, z1
    long long crowd_z = 0, crowd_y = 0;     // "crowd_z", "crowd_y": the crowd by the aisle's back and top
    double ymax = 0.0, ymin = 0.0;          // "ymax", "ymin": the walkable ground's highest and lowest (Python floats)
    int n_walls = 0;                        // "n_walls": the path's walls and every prop's
    int walk_cells = 0;                     // "walk_cells": how many CELL cells are walkable
    std::string key;                        // "key": key_of's hash ("" when the .py's bytes weren't given: see key_of)
    std::string name;                       // "name": the arena's name
    int n = -1;                             // (not in the file) the arena's number, arena_list's n (arenasplit: the results
                                            //  are keyed by it, as an arcade arena without its source game has none)
    long long cell(int i, int k) const { return cells[size_t(k) * gw + i]; }
};

// arenas_collide.py result_of: the arena's result from its stage set's triangles and its crowd's (key and name left
// empty). False, and why, where the Python would stop (an assert) or where Python's set order would decide (see above).
bool result_of(const std::vector<Tri>& set_tris, const std::vector<Tri>& crowd_tris, Collide& out, std::string& why);

// ---- ri_collide_io.cpp: the arenas' inputs, the result files -----------------------------------------------------------

// The text of the mod's generated headers arena_list and Models read (mod/src/...).
struct HeaderTexts {
    std::string wmport_files;        // wmport_files.h: wmp_files, the pack's (offset, size) per file 0x5000 + i
    std::string arena_raw_files;     // arena_raw_files.h: xraw_set_mesh (RAW IS WAR's stage set)
    std::string arena_stage_files;   // arena_stage_files.h: xstage_n, xstage1_mesh .. xstage9_mesh
    std::string arena_ring_files;    // arena_ring_files.h: xring_set_n, xring_set_mesh, the crowd's xring_*_mesh
    std::string arena_files;         // arena_files.h: xarena_names
};

// arenas_collide.py arena_list: an arena's number, name, the stage set's z shift, its set's and crowd's model files.
struct ArenaInput {
    int n = 0;
    std::string name;
    int dz = 0;                      // 1500 for the game's ten (their stage sets sit 1500 further back), 0 for the rest
    std::vector<int> ids, crowd;     // model file ids (0x5000 and up: the pack's)
};
// The game's ten use the game's own crowd lists (arenas.py main_data: No Mercy's main at 0x80050000 + ...; the No Mercy
// ROM given, Rev 1 -- the same bytes there as the six-man ROM the Python reads); the rest the headers' lists.
std::vector<ArenaInput> arena_list(const HeaderTexts& h, const std::vector<uint8_t>& nomercy_rom);

// arenas_collide.py Models: model file i's bytes -- 0x5000 and up from the pack (wmport.bin, by wmport_files.h's table),
// below from No Mercy's own files (file i of the No Mercy ROM's file table, as data/extracted/nmfiles/f/<i>.bin holds).
class Models {
public:
    using FileOf = std::function<const std::vector<uint8_t>*(int id)>;   // No Mercy's file id (nullptr: no such file)
    Models(const std::vector<uint8_t>& pack, const std::string& wmport_files_h, FileOf nm_file);
    std::vector<uint8_t> data(int id) const;                              // ri::Error if it isn't there
    std::vector<Tri> tris(const std::vector<int>& ids, int dz, std::optional<int> zmin) const;
private:
    const std::vector<uint8_t>& pack_;
    std::vector<std::pair<long long, long long>> files_;   // (offset, size) per pack file
    FileOf nm_file_;
};

// arenas_collide.py key_of: SHA-1 of the parameters' text, arenas_collide.py's own bytes, the models' bytes and dz. The
// .py's bytes are the developer's (the check passes the file); without them (nullptr: the game, which never writes the
// result files and only needs the values) the key is "".
std::string key_of(const Models& M, const ArenaInput& a, const std::vector<uint8_t>* py_source);

// arenas_collide.py main for one arena: its triangles, result_of, the key and name. ri::Error if it can't.
Collide collide_arena(const Models& M, const ArenaInput& a, const std::vector<uint8_t>* py_source);

// All 23 (arena_list's order; arenasplit: those with a stage set -- each one's n its arena's): the pack, the headers, the No Mercy ROM's files from R ("nomercy").
std::vector<Collide> collide_all(const std::vector<uint8_t>& pack, const HeaderTexts& h, Roms& R,
                                 const std::vector<uint8_t>* py_source);

// The result file's text exactly as json.dump(keep, f) writes it (keep: result_of's dict without "_r", then "key", "name").
std::string result_json(const Collide& c);
// A result file's text read back (ri::Error if it isn't one).
Collide parse_result_json(const std::string& text);

}  // namespace collide
}  // namespace ri
