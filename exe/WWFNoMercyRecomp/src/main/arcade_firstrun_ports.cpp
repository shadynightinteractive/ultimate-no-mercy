// The first start's ports (arcade_firstrun_ports.h): the mod's tables our Python generators computed from the game's
// data, computed again here, in C++, from the player's ROM -- byte for byte the build's (firstrun, 2026-11-17).
//
//   port "walls":     xw0_rec .. xw9_rec, the game's ten arenas' collision records (mod/gen/arenas_walls.py emit_arena)
//   port "ringstage": xrs0_cells .. xrs9_cells and xrs_arenas' first ten entries (arenas_walls.py write_ringstage)
//   port "labels":    the menu lettering (arcade_firstrun_labels.cpp port_labels)
//
// Both walls and ringstage come from one computation: each arena's walkable ground, walls and map, worked out from the
// models of its stage set (mod/gen/arenas_collide.py result_of: src/romimport/ri_collide.cpp), then laid out as the overlay's records and the running
// game's maps (arenas_walls.py arena_set). Each step below names the Python function it ports.
//
// Byte for byte means Python's arithmetic exactly: float64 in the same order, // as floor division (on floats too),
// round() as banker's rounding, int() truncating, min() / max() keeping the FIRST extreme, and the normals written as
// "%.6f" and read back as a float literal. Nothing here may be fused (a*b+c in one rounding): the pragma below, and the
// exe compiles this file without -ffast-math.
//
// The stage sets' models: most of them are the mod's own made files (ids 0x5000 and up: the pack wmport.bin), the rest
// No Mercy's own files; both come through rom.file(id) (see run_port).
#include "arcade_firstrun_ports.h"

#include "../romimport/ri_collide.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <utility>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace firstrun {
namespace {

constexpr uint32_t IMAGE_BASE = 0x81000000u;   // the image holds address A at A - IMAGE_BASE

// ---- arenas_collide.py: the computation ----------------------------------------------------------------------------

// Python's arithmetic (py_floordiv, py_idiv, py_round, py_min / py_max, int_hypot), the models' triangles (add_tris) and
// result_of -> Collide: src/romimport/ri_collide.cpp, shared with the ROM importer's 23 arenas (p3collide, 2026-11-19).
using namespace ri::collide;

// Python's "%.6f" % v read back as a C float literal (how the walls' normals went through the header): the six-decimal
// text (std::to_chars: exact, as printf), then the float nearest that decimal (ties to even), as clang reads a literal.
float six_decimals_as_float(double v) {
    char text[64];
    auto res = std::to_chars(text, text + sizeof(text), v, std::chars_format::fixed, 6);
    *res.ptr = 0;
    // the text as an integer count of millionths, and its sign ("-0.000000" keeps its sign: -0.0f)
    bool negative = text[0] == '-';
    long long millionths = 0;
    for (const char* p = text; *p; p++)
        if (*p >= '0' && *p <= '9') millionths = millionths * 10 + (*p - '0');
    // the nearest float to millionths / 1e6: a candidate and its neighbours, each one's distance worked out exactly (a
    // float times 1e6 is exact in a double: 24 + 20 bits)
    float best = float(double(millionths) / 1e6);
    float candidates[3] = {std::nextafter(best, 0.0f), best, std::nextafter(best, 2.0f)};
    double best_error = -1.0;
    for (float c : candidates) {
        double error = std::fabs(double(c) * 1e6 - double(millionths));
        uint32_t bits;
        std::memcpy(&bits, &c, 4);
        uint32_t best_bits;
        std::memcpy(&best_bits, &best, 4);
        if (best_error < 0 || error < best_error || (error == best_error && (bits & 1) == 0 && (best_bits & 1) != 0)) {
            best = c;
            best_error = error;
        }
    }
    return negative ? -best : best;
}

// ---- reading and writing big-endian bytes ------------------------------------------------------------------------------

uint16_t read_u16(std::span<const uint8_t> b, size_t at) { return uint16_t(b[at] << 8 | b[at + 1]); }
int16_t read_s16(std::span<const uint8_t> b, size_t at) { return int16_t(read_u16(b, at)); }
void write_u16(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v >> 8);
    b[at + 1] = uint8_t(v);
}
void write_u32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v >> 24);
    b[at + 1] = uint8_t(v >> 16);
    b[at + 2] = uint8_t(v >> 8);
    b[at + 3] = uint8_t(v);
}

// ---- arenas_walls.py: the game's records and each arena's set ----------------------------------------------------------

// arenas_walls.py ovl_e: the match overlay's data in the six-man ROM (ROM 0x144BE0 at 0x800D9960).
size_t ovl_e_offset(uint32_t address) { return 0x144BE0u + address - 0x800D9960u; }

// arenas_walls.py REC: the game's records (their addresses in ovl_e), by id.
uint32_t game_rec_address(int id) {
    static const std::map<int, uint32_t> rec = {
        {10, 0x801470F8}, {11, 0x80147128}, {12, 0x80147158}, {13, 0x80147188}, {14, 0x801471B8}, {15, 0x801471E8},
        {16, 0x80147218}, {17, 0x80147248}, {18, 0x80147278}, {19, 0x801472A8}, {20, 0x801472D8}, {21, 0x80147308},
        {22, 0x80147390}, {23, 0x801473C0}, {24, 0x80147410}, {25, 0x80147440}, {26, 0x80147470}, {27, 0x801474A0},
        {28, 0x801474D0}, {55, 0x80147520}, {56, 0x80147550}, {0, 0x80147B38}, {78, 0x80147C50}, {79, 0x80147C80},
        {80, 0x80147CB0}, {81, 0x80147CE0}, {82, 0x80147E38}, {83, 0x80147D30}, {84, 0x80147D60}, {85, 0x80147D90},
        {86, 0x80147DC0}};
    auto it = rec.find(id);
    return it == rec.end() ? 0 : it->second;
}

// A collision record as arena_set builds it.
struct Rec {
    int id = 0, fl = 0, kind = 0;
    std::array<int, 12> c{};   // four corners (x, y, z)
};

// arenas_walls.py game_rec: record id as the game has it.
bool game_rec(std::span<const uint8_t> six, int id, Rec& r, std::string& why) {
    const uint32_t address = game_rec_address(id);
    const size_t o = ovl_e_offset(address);
    if (!address || o + 0x30 > six.size()) { why = "no game record " + std::to_string(id); return false; }
    for (int k = 0; k < 12; k++) r.c[k] = read_s16(six, o + 2 * k);
    r.id = id;
    r.fl = read_u16(six, o + 0x26);
    r.kind = read_u16(six, o + 0x28);
    if (read_u16(six, o + 0x24) != id) { why = "game record " + std::to_string(id) + " isn't there"; return false; }
    return true;
}

// arenas_walls.py unit_normal: a record's unit normal (its corners whole numbers: the products exact).
std::array<double, 3> unit_normal(const std::array<int, 12>& c) {
    const long long a[3] = {c[3] - c[0], c[4] - c[1], c[5] - c[2]};
    const long long b[3] = {c[6] - c[0], c[7] - c[1], c[8] - c[2]};
    const long long n[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    const __int128 sum = (__int128)n[0] * n[0] + (__int128)n[1] * n[1] + (__int128)n[2] * n[2];
    const double l = std::sqrt(double(sum));
    if (l == 0.0) return {0.0, 0.0, 0.0};
    return {double(n[0]) / l, double(n[1]) / l, double(n[2]) / l};
}

// arenas_walls.py table_moved: announce.c table_walls_up's move (75 toward the ring), built in.
constexpr int TABLE_UP = 75;
Rec table_moved(const Rec& r) {
    Rec out = r;
    bool table_wall = true, high = false, top = true, all_front = true, all_back = true;
    for (int q = 0; q < 4; q++) {
        const int x = r.c[3 * q], y = r.c[3 * q + 1], z = r.c[3 * q + 2];
        if (!(std::abs(x) == 113 && 0 <= y && y <= 1000 && (z == -530 || z == -690))) table_wall = false;
        if (y == 1000) high = true;
        if (y != 90) top = false;
        if (z != -530) all_front = false;
        if (z != -690) all_back = false;
    }
    if (table_wall) {
        for (int q = 0; q < 4; q++) {
            if (out.c[3 * q + 2] == -530) out.c[3 * q + 2] = -530 + TABLE_UP;
            else if (out.c[3 * q + 2] == -690 && (high || top)) out.c[3 * q + 2] = -690 + TABLE_UP + 15;
        }
    }
    if (r.id == 85 && all_front)
        for (int q = 0; q < 4; q++) out.c[3 * q + 2] = -530 + TABLE_UP;
    if (r.id == 86 && all_back)
        for (int q = 0; q < 4; q++) out.c[3 * q + 2] = -690 + TABLE_UP + 15;
    return out;
}

// arenas_walls.py rs_wall: a wall of the stage (from a to b, the walkable side on its right), as a record.
bool rs_wall(int id, IPt a, IPt b, int fl_class, int kind, Rec& r, std::string& why) {
    const int dx = b.first - a.first, dz = b.second - a.second;
    if (!dx && !dz) { why = "a wall of no length"; return false; }
    const int nx = dz, nz = -dx;   // (the walkable side: its normal)
    int d;
    if (std::abs(nx) >= std::abs(nz)) d = nx > 0 ? 0x0200 : 0x0100;
    else d = nz > 0 ? 0x0400 : 0x0800;
    r.id = id;
    r.fl = d | fl_class;
    r.kind = kind;
    if (!(r.fl & 0x30F8)) { why = "a wall a whipped man runs on the spot at"; return false; }
    r.c = {a.first, 1000, a.second, b.first, 1000, b.second, b.first, 0, b.second, a.first, 0, a.second};
    const auto un = unit_normal(r.c);
    if (!(un[0] * nx + un[2] * nz > 0)) { why = "a wall facing away from the walkable side"; return false; }
    return true;
}

// arenas_walls.py rs_path_door: the path's walls, the door across the stage's middle in the wall furthest back there.
constexpr int DOOR_HALF = 90;
constexpr int DOOR_MIN = 80;    // (the least of the door across the middle: a whip there must meet it)
constexpr int DOOR_STEP = 40;   // (a step of the map's stairs the door goes on across)
constexpr int DOOR_NEAR_Z = 60; // (rs_crash_target: a wall this near the door's z is 'at the door'; Changed for new features (Ultimate No Mercy), was 30)
const int DOOR_ID[10] = {306, 357, 95, 288, 218, 266, 402, 340, 231, 373};
struct PathWall {
    IPt a, b;
    int crash, door;
};
// Python's a // b on ints (b > 0 here): the floor
inline long long door_floordiv(long long a, long long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
// Changed for new features (Ultimate No Mercy): the stages agent, 2026-11-20 -- the door where the game's ten have their
// doorway, the middle of the stage's back (arenas_walls.py rs_path_door says why; the rule before put RAW IS WAR's on a wing)
std::vector<PathWall> rs_path_door(const Collide& res, int n, std::optional<IPt>& door_at) {
    std::vector<PathWall> ws;
    for (const Wall& w : res.path) ws.push_back({w.a, w.b, w.crash, 0});
    door_at.reset();
    // a wall along x, way +x (the walkable side -z; a slant under 45 degrees too); a step of the stairs (across z, DOOR_STEP
    // or less); a wall's z at x
    auto along = [&](size_t k) {
        const long long dx = (long long)ws[k].b.first - ws[k].a.first, dz = (long long)ws[k].b.second - ws[k].a.second;
        return dx > 0 && std::llabs(dz) <= dx;
    };
    auto step = [&](size_t k) {
        const long long dz = std::llabs((long long)ws[k].b.second - ws[k].a.second);
        return ws[k].a.first == ws[k].b.first && dz > 0 && dz <= DOOR_STEP;
    };
    auto z_at = [&](size_t k, long long x) {
        const IPt a = ws[k].a, b = ws[k].b;
        return a.second + door_floordiv(((long long)b.second - a.second) * (x - a.first), (long long)b.first - a.first);
    };
    // the candidates: along x, not the first or last, reaching DOOR_MIN or more into x -DOOR_HALF..DOOR_HALF; key (z at the
    // reach's middle, the reach, -index): the greatest
    bool have = false;
    long long best_z = 0, best_w = 0, lo = 0, hi = 0;
    size_t i = 0;
    for (size_t k = 0; k < ws.size(); k++) {
        if (k == 0 || k == ws.size() - 1 || !along(k)) continue;
        const long long klo = std::max<long long>(ws[k].a.first, -DOOR_HALF), khi = std::min<long long>(ws[k].b.first, DOOR_HALF);
        if (khi - klo < DOOR_MIN) continue;
        const long long zm = z_at(k, door_floordiv(klo + khi, 2));
        if (!have || zm > best_z || (zm == best_z && (khi - klo > best_w || (khi - klo == best_w && k < i)))) {
            have = true;
            best_z = zm;
            best_w = khi - klo;
            lo = klo;
            hi = khi;
            i = k;
        }
    }
    if (!have) return ws;
    // short of the very middle at a step of the stairs: on across it to the next wall along x
    size_t j = i;
    while (lo > -DOOR_MIN / 2 && i >= 3 && step(i - 1) && along(i - 2)) { i -= 2; lo = std::max<long long>(ws[i].a.first, -DOOR_HALF); }
    while (hi < DOOR_MIN / 2 && j + 2 + 2 <= ws.size() && step(j + 1) && along(j + 2)) { j += 2; hi = std::min<long long>(ws[j].b.first, DOOR_HALF); }
    const IPt d0 = {(int)lo, (int)z_at(i, lo)}, d1 = {(int)hi, (int)z_at(j, hi)};
    const PathWall wi = ws[i], wj = ws[j];
    std::vector<PathWall> out(ws.begin(), ws.begin() + i);
    if (d0 != wi.a) out.push_back({wi.a, d0, wi.crash, 0});
    out.push_back({d0, d1, 1, DOOR_ID[n]});
    if (d1 != wj.b) out.push_back({d1, wj.b, wj.crash, 0});
    out.insert(out.end(), ws.begin() + j + 1, ws.end());
    door_at = IPt{(int)door_floordiv((long long)d0.first + d1.first, 2), (int)door_floordiv((long long)d0.second + d1.second, 2)};
    return out;
}

// arenas_walls.py rs_floors: the raised ground as floor records: strips of the map, those over each other put together.
struct FloorStrip {
    long long x0, x1, z0, z1, y0, y1;
};
std::vector<FloorStrip> rs_floors(const Collide& res) {
    std::vector<FloorStrip> strips;
    for (int k = 0; k < res.gh; k++) {
        int i = 0;
        while (i < res.gw) {
            const long long h = res.cell(i, k);
            if (h == NO_CELL || h < 8) {
                i += 1;
                continue;
            }
            int j = i;
            while (j + 1 < res.gw && res.cell(j + 1, k) != NO_CELL && std::abs(res.cell(j + 1, k) - h) <= 4) j += 1;
            strips.push_back({res.gx0 + i * (long long)GRID, res.gx0 + (j + 1) * (long long)GRID, res.gz0 + k * (long long)GRID,
                              res.gz0 + (k + 1) * (long long)GRID, h, h});
            i = j + 1;
        }
    }
    std::vector<FloorStrip> out;
    for (const FloorStrip& t : strips) {
        bool joined = false;
        for (FloorStrip& o : out) {
            if (o.x0 == t.x0 && o.x1 == t.x1 && o.z1 == t.z0) {
                const double n_ = double(o.z1 - o.z0) / GRID;
                const double slope = n_ != 0.0 ? double(o.y1 - o.y0) / n_ : 0.0;
                const double other = n_ != 0.0 ? double(o.y1) + slope : double(t.y0);
                if (std::fabs((double(t.y0) + slope) - other) <= 6 || (o.y1 == o.y0 && o.y0 == t.y0)) {
                    o.z1 = t.z1;
                    o.y1 = t.y1;
                    joined = true;
                    break;
                }
            }
        }
        if (!joined) out.push_back(t);
    }
    std::stable_sort(out.begin(), out.end(), [](const FloorStrip& a, const FloorStrip& b) {
        return -(a.x1 - a.x0) * (a.z1 - a.z0) < -(b.x1 - b.x0) * (b.z1 - b.z0);
    });
    if (out.size() > 24) out.resize(24);
    return out;
}

// arenas_walls.py rs_crash_target: (x, z, is it a prop) -- the middle of a crash wall facing the ring with 320 clear
// in front of it, a prop's if there is one.
std::array<int, 3> rs_crash_target(const Collide& res, std::optional<IPt> door_at) {
    auto clear = [&](double x, double z) {
        const int i = floor_index(x - res.gx0, GRID), k = floor_index(z - res.gz0, GRID);
        return 0 <= i && i < res.gw && 0 <= k && k < res.gh && res.cell(i, k) != NO_CELL;
    };
    bool have = false;
    int best_prop = 0;
    double best_x = 0;
    std::array<int, 3> best{};
    for (int prop : {1, 0}) {
        std::vector<Wall> ws;
        if (prop) {
            for (const auto& p : res.props) ws.insert(ws.end(), p.begin(), p.end());
        } else {
            ws = res.path;
        }
        for (const Wall& w : ws) {
            if (!w.crash) continue;
            const int ax = w.a.first, az = w.a.second, bx = w.b.first, bz = w.b.second;
            const int dx = bx - ax, dz = bz - az;
            const double L = std::sqrt(double(dx * dx + dz * dz));   // (Python: (dx*dx + dz*dz) ** .5)
            if (L < 40 || dz / L * dz / L > 0.25 || dx <= 0) continue;
            double mx = (ax + bx) / 2.0;
            const double mz = (az + bz) / 2.0;
            if (door_at && std::fabs(mz - door_at->second) < DOOR_NEAR_Z && std::fabs(mx - door_at->first) < DOOR_HALF + 50) {
                mx = door_at->first - (DOOR_HALF + 60);   // (beside the door, not at it)
                if (mx < std::min(ax, bx) + 20) mx = door_at->first + DOOR_HALF + 60;
                if (mx > std::max(ax, bx) - 20) continue;
            }
            bool all_clear = true;
            for (int d = 40; d < 340 && all_clear; d += 40)
                for (int o : {-40, 0, 40})
                    if (!clear(mx + o, mz - d)) {
                        all_clear = false;
                        break;
                    }
            if (!all_clear) continue;
            const double score_x = -std::fabs(mx);
            if (!have || prop > best_prop || (prop == best_prop && score_x > best_x)) {
                have = true;
                best_prop = prop;
                best_x = score_x;
                best = {int(mx), int(mz), prop};   // (Python's int(): toward zero)
            }
        }
    }
    if (!have) return {0, res.bbox[3], 0};
    return best;
}

// arenas_walls.py arena_set (table = 1: the game's ten arenas) with the ring-and-stage walls: the records in order,
// each one's next (by id), and where a man comes back from backstage.
struct ArenaSet {
    std::vector<Rec> recs;
    std::map<int, int> next;   // id -> the next record's id
    IPt arrive{};
    std::optional<IPt> door_at;
};

bool arena_set(std::span<const uint8_t> six, int n, const Collide& res, ArenaSet& out, std::string& why) {
    const int ids[] = {10, 11, 12, 13, 14, 15, 16, 24, 25, 26, 27, 28, 17, 18, 19, 20, 21, 22, 23, 55, 56, 0, 78, 79, 80, 81, 82, 83, 84, 85, 86};
    std::vector<Rec> recs;
    for (int id : ids) {
        Rec r;
        if (!game_rec(six, id, r, why)) return false;
        recs.push_back(table_moved(r));
    }
    std::vector<int> loop = {10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 17, 18, 19, 20, 21};
    // the path round the stage: 21 its first, 11 its last; the door its own id
    const std::vector<PathWall> ws = rs_path_door(res, n, out.door_at);
    std::vector<Rec> path_recs;
    int next_id = 600;
    for (size_t j = 0; j < ws.size(); j++) {
        Rec r;
        bool ok;
        if (ws[j].door) ok = rs_wall(ws[j].door, ws[j].a, ws[j].b, 0x2000, 0x0200, r, why);
        else if (ws[j].crash) ok = rs_wall(next_id, ws[j].a, ws[j].b, 0x0008, 0x0001, r, why);
        else ok = rs_wall(next_id, ws[j].a, ws[j].b, 0x2000, 0x0400, r, why);
        if (!ok) return false;
        if (j == 0) r.id = 21;
        if (j == ws.size() - 1) r.id = 11;
        if (!ws[j].door) next_id += 1;
        path_recs.push_back(r);
    }
    std::vector<Rec> kept;
    for (const Rec& r : recs)
        if (r.id != 10 && r.id != 21 && r.id != 11) kept.push_back(r);
    recs = kept;
    recs.insert(recs.end(), path_recs.begin(), path_recs.end());
    // the chain: 12 .. 20, then the path, whose last, 11, goes on to 12
    std::vector<int> chain;
    for (int x : loop)
        if (x != 10 && x != 21 && x != 11) chain.push_back(x);
    const size_t at12 = size_t(std::find(chain.begin(), chain.end(), 12) - chain.begin());
    std::rotate(chain.begin(), chain.begin() + at12, chain.end());
    for (const Rec& r : path_recs) chain.push_back(r.id);
    // the props: each a closed loop of crash walls
    std::vector<std::vector<int>> props;
    for (const auto& p : res.props) {
        std::vector<int> prop_ids;
        for (const Wall& w : p) {
            Rec r;
            if (!rs_wall(next_id, w.a, w.b, 0x0008, 0x0001, r, why)) return false;
            recs.push_back(r);
            prop_ids.push_back(next_id);
            next_id += 1;
        }
        props.push_back(prop_ids);
    }
    // the floor: the game's 55 to the first of the raised ground (or the set's end), the raised ground its own records
    const std::vector<FloorStrip> raised = rs_floors(res);
    long long z_end = 0;
    bool have_end = false;
    for (const FloorStrip& o : raised)
        if (std::abs(o.x0 + o.x1) <= 400) {
            z_end = have_end ? std::min(z_end, o.z0) : o.z0;
            have_end = true;
        }
    if (!have_end) z_end = res.bbox[3] + 200;
    Rec r55;
    if (!game_rec(six, 55, r55, why)) return false;
    for (Rec& r : recs)
        if (r.id == 55) {
            r = r55;
            for (int q = 0; q < 4; q++)
                if (r.c[3 * q + 2] == 800) r.c[3 * q + 2] = int(z_end);
        }
    std::vector<int> floor_ids;
    for (const FloorStrip& o : raised) {
        Rec r = r55;
        r.id = next_id;
        r.c = {int(o.x0), int(o.y1), int(o.z1), int(o.x1), int(o.y1), int(o.z1), int(o.x1), int(o.y0), int(o.z0), int(o.x0), int(o.y0), int(o.z0)};
        recs.push_back(r);
        floor_ids.push_back(next_id);
        next_id += 1;
    }
    // where he comes back from backstage: by the door, facing the ring
    out.arrive = out.door_at ? IPt{out.door_at->first, out.door_at->second - 60} : IPt{0, res.bbox[3] - 80};
    // the next links: each ring of ids round to its first
    auto link_ring = [&](const std::vector<int>& ring) {
        for (size_t i = 0; i < ring.size(); i++) out.next[ring[i]] = ring[(i + 1) % ring.size()];
    };
    link_ring(chain);
    out.next[16] = 17;   // (the straight -z side: linked in at 15 when the table breaks)
    std::vector<int> floor_ring = {55};
    floor_ring.insert(floor_ring.end(), floor_ids.begin(), floor_ids.end());
    floor_ring.push_back(56);
    for (const std::vector<int>& ring : std::vector<std::vector<int>>{{22, 23}, floor_ring, {0}, {78, 79, 80, 81}, {82}, {83, 84}, {85, 86}})
        link_ring(ring);
    for (const auto& prop_ids : props) link_ring(prop_ids);
    out.recs = recs;
    return true;
}

// ---- the ten arenas, from the ROM --------------------------------------------------------------------------------------

const Table* find_table(const std::vector<Table>& mine, const std::vector<Table>& all, const std::string& name) {
    for (const Table& t : mine)
        if (t.name == name) return &t;
    for (const Table& t : all)
        if (t.name == name) return &t;
    return nullptr;
}

// A table's bytes in the image (null if it isn't all there).
const uint8_t* image_at(const std::vector<uint8_t>& image, const Table& t) {
    if (t.address < IMAGE_BASE || size_t(t.address - IMAGE_BASE) + t.size > image.size()) return nullptr;
    return image.data() + (t.address - IMAGE_BASE);
}

struct Arena {
    Collide res;
    ArenaSet set;
};

// arenas_collide.py arena_list (n 0 .. 9) + main: each arena's stage set and crowd made into triangles and solved; then
// arenas_walls.py arena_set on the result.
bool ten_arenas(const Rom& rom, const std::vector<Table>& mine, const std::vector<Table>& all, const std::vector<uint8_t>& image,
                std::vector<Arena>& out, std::string& why) {
    // the crowd and barricades: the game's lists (arenas.py main_data: the six-man ROM's main at 0x80000450 from 0x1050)
    std::vector<int> crowd;
    const std::pair<uint32_t, int> lists[] = {{0x70, 10}, {0xF8, 15}, {0x1BC, 3}, {0x1DC, 9}, {0x220, 22}, {0x2AC, 22}};
    for (const auto& [o, count] : lists) {
        const size_t at = 0x1050u + (0x80050000u + o) - 0x80000450u;
        if (at + 2 * count > rom.six.size()) { why = "the six-man ROM is short"; return false; }
        for (int i = 0; i < count; i++) crowd.push_back(read_u16(rom.six, at + 2 * i));
    }
    auto model = [&](int id, int dz, std::optional<int> zmin, std::vector<Tri>& tris) {
        const std::vector<uint8_t>& d = rom.file(uint32_t(id));
        if (d.empty()) {
            char text[80];
            std::snprintf(text, sizeof(text), "model file 0x%X isn't there%s", id, id >= 0x5000 ? " (wmport.bin's)" : "");
            why = text;
            return false;
        }
        if (!add_tris(d, dz, zmin, tris)) {
            char text[80];
            std::snprintf(text, sizeof(text), "model file 0x%X is short", id);
            why = text;
            return false;
        }
        return true;
    };
    std::vector<Tri> crowd_tris;
    for (int id : crowd)
        if (!model(id, 0, 300, crowd_tris)) return false;
    // the stage sets' file lists (in the image already: the plan's copies): RAW's xraw_set_mesh, the rest xstageV_mesh[:xstage_n[v]]
    const Table* stage_n = find_table(mine, all, "xstage_n");
    const uint8_t* stage_n_bytes = stage_n ? image_at(image, *stage_n) : nullptr;
    if (!stage_n_bytes || stage_n->size < 20) { why = "no xstage_n"; return false; }
    out.assign(10, {});
    for (int n = 0; n < 10; n++) {
        const Table* list = find_table(mine, all, n == 0 ? "xraw_set_mesh" : "xstage" + std::to_string(n) + "_mesh");
        const uint8_t* bytes = list ? image_at(image, *list) : nullptr;
        if (!bytes) { why = "no stage set list for arena " + std::to_string(n); return false; }
        int count = int(list->size / 2);
        if (n > 0) count = std::min(count, int(read_s16({stage_n_bytes, 20}, 2 * n)));
        std::vector<Tri> set_tris;
        for (int i = 0; i < count; i++)
            if (!model(read_u16({bytes, list->size}, 2 * i), 1500, std::nullopt, set_tris)) return false;
        if (!result_of(set_tris, crowd_tris, out[n].res, why)) { why = "arena " + std::to_string(n) + ": " + why; return false; }
        if (!arena_set(rom.six, n, out[n].res, out[n].set, why)) { why = "arena " + std::to_string(n) + ": " + why; return false; }
    }
    return true;
}

// arenas_walls.py emit_arena: arena n's records as XWallRec (0x30 bytes: s16 c[12], float n[3], u16 id, fl, kind, pad,
// next) into the image.
bool write_walls(int n, const ArenaSet& set, const Table& t, std::vector<uint8_t>& image, std::string& why) {
    std::map<int, size_t> index;   // id -> its place (Python's {id: i}: a later one wins)
    for (size_t i = 0; i < set.recs.size(); i++) index[set.recs[i].id] = i;
    if (t.size != set.recs.size() * 0x30) { why = "xw" + std::to_string(n) + "_rec: " + std::to_string(set.recs.size()) + " records, the table holds " + std::to_string(t.size / 0x30); return false; }
    if (!image_at(image, t)) { why = "xw" + std::to_string(n) + "_rec is outside the image"; return false; }
    const size_t base = t.address - IMAGE_BASE;
    for (size_t i = 0; i < set.recs.size(); i++) {
        const Rec& r = set.recs[i];
        const size_t o = base + 0x30 * i;
        for (int k = 0; k < 12; k++) write_u16(image, o + 2 * k, uint16_t(int16_t(r.c[k])));
        const auto normal = unit_normal(r.c);
        for (int k = 0; k < 3; k++) {
            const float f = six_decimals_as_float(normal[k]);
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            write_u32(image, o + 0x18 + 4 * k, bits);
        }
        write_u16(image, o + 0x24, uint16_t(r.id));
        write_u16(image, o + 0x26, uint16_t(r.fl));
        write_u16(image, o + 0x28, uint16_t(r.kind));
        write_u16(image, o + 0x2A, 0);
        auto next = set.next.find(r.id);
        if (next == set.next.end() || !index.count(next->second)) { why = "record " + std::to_string(r.id) + " has no next"; return false; }
        write_u32(image, o + 0x2C, t.address + uint32_t(0x30 * index[next->second]));
    }
    return true;
}

// arenas_walls.py write_ringstage, rows 0 .. 9: each arena's map (xrsN_cells: height / 2, 255 not walkable) and its
// XrsArena entry (28 bytes: s16 x0, z0, w, h; u32 cells; s16 front, own_y, ax, az, qx, qz, crowd_z, crowd_y).
bool write_ringstage(const std::vector<Arena>& arenas, const std::vector<Table>& mine, const std::vector<Table>& all,
                     std::vector<uint8_t>& image, std::string& why) {
    const Table* entries = find_table(mine, all, "xrs_arenas");
    if (!entries || entries->size < 10 * 28 || !image_at(image, Table{"", entries->address, 10 * 28})) { why = "no xrs_arenas"; return false; }
    for (int n = 0; n < 10; n++) {
        const Collide& res = arenas[n].res;
        const std::string name = "xrs" + std::to_string(n) + "_cells";
        const Table* cells = find_table(mine, all, name);
        if (!cells || !image_at(image, *cells)) { why = "no " + name; return false; }
        if (cells->size != res.cells.size()) { why = name + ": " + std::to_string(res.cells.size()) + " cells, the table holds " + std::to_string(cells->size); return false; }
        const size_t at = cells->address - IMAGE_BASE;
        for (size_t i = 0; i < res.cells.size(); i++) {
            const long long h = res.cells[i];
            image[at + i] = uint8_t(h == NO_CELL ? 255 : std::max(0LL, std::min(254LL, py_idiv(h + 1, 2))));
        }
        const std::array<int, 3> target = rs_crash_target(res, arenas[n].set.door_at);
        const size_t e = entries->address - IMAGE_BASE + 28 * size_t(n);
        const int head[4] = {res.gx0, res.gz0, res.gw, res.gh};
        for (int k = 0; k < 4; k++) write_u16(image, e + 2 * k, uint16_t(int16_t(head[k])));
        write_u32(image, e + 8, cells->address);
        const long long tail[8] = {res.front, (n == 0 || n == 2) ? 1 : 0, arenas[n].set.arrive.first, arenas[n].set.arrive.second,
                                   target[0], target[1], res.crowd_z, res.crowd_y};
        for (int k = 0; k < 8; k++) write_u16(image, e + 12 + 2 * k, uint16_t(int16_t(tail[k])));
    }
    return true;
}

}  // namespace

bool port_labels(const Rom& rom, const std::vector<Table>& mine, std::vector<uint8_t>& image, std::string& why);

// The ports. walls and ringstage both work the ten arenas out (each port on its own: the first start runs each once).
// Their models come through rom.file(id): No Mercy's files (id < 0x5000) and the mod's made ones (0x5000 and up:
// wmport.bin's file id - 0x5000) -- an empty one stops the port.
bool run_port(const std::string& port, const Rom& rom, const std::vector<Table>& mine, const std::vector<Table>& all,
              std::vector<uint8_t>& image, std::string& why) {
    if (port == "labels") return port_labels(rom, mine, image, why);     // (arcade_firstrun_labels.cpp)
    if (port != "walls" && port != "ringstage") {
        why = "no port called " + port;
        return false;
    }
    std::vector<Arena> arenas;
    if (!ten_arenas(rom, mine, all, image, arenas, why)) return false;
    if (port == "walls") {
        for (int n = 0; n < 10; n++) {
            const Table* t = find_table(mine, all, "xw" + std::to_string(n) + "_rec");
            if (!t) { why = "no xw" + std::to_string(n) + "_rec"; return false; }
            if (!write_walls(n, arenas[n].set, *t, image, why)) return false;
        }
        return true;
    }
    return write_ringstage(arenas, mine, all, image, why);
}

}  // namespace firstrun
