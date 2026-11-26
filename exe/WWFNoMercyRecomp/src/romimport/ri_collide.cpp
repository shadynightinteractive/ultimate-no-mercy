// ri_collide.cpp (phase3 p3collide, 2026-11-19): mod/gen/arenas_collide.py's computation -- an arena's walkable ground, its
// walls and the running game's map from its stage set's and crowd's triangles (ri_collide.h says what and why). Written
// first for the first start's ten arenas (firstrun, 2026-11-17: src/main/arcade_firstrun_ports.cpp), moved here so the
// ROM importer's 23 use the same code. Each function names the Python one it ports.
//
// Nothing here may be fused (a*b+c in one rounding: Python rounds each step): the pragma below; the builds don't use
// -ffast-math. This file needs nothing else of the importer (the first start's port test builds it on its own).
#include "ri_collide.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <map>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace ri {
namespace collide {

// ---- Python's arithmetic, exactly --------------------------------------------------------------------------------------

// Python's a // b on floats (CPython float_floor_div: fmod, then the quotient corrected to the floor).
double py_floordiv(double vx, double wx) {
    double mod = std::fmod(vx, wx);
    double div = (vx - mod) / wx;
    if (mod != 0.0) {
        if ((wx < 0) != (mod < 0)) {
            mod += wx;
            div -= 1.0;
        }
    }
    double floordiv;
    if (div != 0.0) {
        floordiv = std::floor(div);
        if (div - floordiv > 0.5) floordiv += 1.0;
    } else {
        floordiv = std::copysign(0.0, vx / wx);
    }
    return floordiv;
}

// int(a // b) for floats (the cell a coordinate falls in).
int floor_index(double a, double b) { return int(py_floordiv(a, b)); }

// Python's a // b on ints.
long long py_idiv(long long a, long long b) {
    long long q = a / b;
    if (a % b != 0 && ((a < 0) != (b < 0))) q--;
    return q;
}

// Python's round(x) (no digits): to the nearest, halves to even (CPython float___round___impl).
long long py_round(double x) {
    double r = std::round(x);
    if (std::fabs(x - r) == 0.5) r = 2.0 * std::round(x / 2.0);
    return (long long)r;
}

// Python's math.hypot of two ints: the square root of the exact sum of squares (the correctly rounded length).
double int_hypot(long long dx, long long dz) { return std::sqrt(double(dx * dx + dz * dz)); }

namespace {

int s8(uint8_t b) { return b > 127 ? int(b) - 256 : int(b); }

}  // namespace

// ---- arenas_collide.py: the models (mesh, Models.tris) -----------------------------------------------------------------

// arenas_collide.py mesh + Models.tris for one file: its triangles, z moved by dz; with zmin, only those reaching it.
bool add_tris(const std::vector<uint8_t>& d, int dz, std::optional<int> zmin, std::vector<Tri>& out) {
    if (d.size() < 8) return false;
    const int scale = (d[0] & 0x7F) + 1, n_points = d[1] & 0x7F, n_tris = d[2];
    const int off[3] = {s8(d[4]) * 16, s8(d[5]) * 16, s8(d[6]) * 16};
    const size_t tris_at = 8 + 8 * size_t(n_points);
    if (d.size() < tris_at + 3 * size_t(n_tris)) return false;
    std::vector<Vec3> points(n_points);
    for (int i = 0; i < n_points; i++) {
        for (int k = 0; k < 3; k++) points[i][k] = double((s8(d[8 + 8 * i + k]) + off[k]) * scale);
        points[i][2] += dz;
    }
    for (int i = 0; i < n_tris; i++) {
        const int a = d[tris_at + 3 * i], b = d[tris_at + 3 * i + 1], c = d[tris_at + 3 * i + 2];
        if (std::max({a, b, c}) >= n_points) continue;
        if (zmin && std::max({points[a][2], points[b][2], points[c][2]}) < *zmin) continue;
        out.push_back({points[a], points[b], points[c]});
    }
    return true;
}

namespace {

// ---- arenas_collide.py: geometry ---------------------------------------------------------------------------------------

// arenas_collide.py norm: the y of a model triangle's unit normal (its points are whole numbers: the products exact).
double normal_y(const Tri& t) {
    const long long ux = (long long)(t[1][0] - t[0][0]), uy = (long long)(t[1][1] - t[0][1]), uz = (long long)(t[1][2] - t[0][2]);
    const long long vx = (long long)(t[2][0] - t[0][0]), vy = (long long)(t[2][1] - t[0][1]), vz = (long long)(t[2][2] - t[0][2]);
    const long long nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    // (Python's exact integer sum of squares, then made a float)
    const __int128 sum = (__int128)nx * nx + (__int128)ny * ny + (__int128)nz * nz;
    double length = std::sqrt(double(sum));
    if (length == 0.0) length = 1.0;
    return double(ny) / length;
}

// arenas_collide.py cells_of: the cells an x-z polygon passes through.
std::vector<IPt> cells_of(const std::vector<Pt2>& poly, int zmin, int nx, int nz) {
    std::vector<IPt> out;
    double zlow = poly[0][1], zhigh = poly[0][1];
    for (const Pt2& p : poly) {
        zlow = py_min(zlow, p[1]);
        zhigh = py_max(zhigh, p[1]);
    }
    const int k0 = floor_index(zlow - zmin, CELL), k1 = floor_index(zhigh - zmin, CELL);
    const size_t n = poly.size();
    for (int k = std::max(0, k0); k <= std::min(nz - 1, k1); k++) {
        const double zlo = zmin + k * CELL, zhi = zmin + (k + 1) * CELL;
        std::vector<double> xs;
        for (size_t j = 0; j < n; j++) {
            const Pt2& a = poly[j];
            const Pt2& b = poly[(j + 1) % n];
            if ((a[1] < zlo && b[1] < zlo) || (a[1] > zhi && b[1] > zhi)) continue;
            if (a[1] == b[1]) {
                xs.push_back(a[0]);
                xs.push_back(b[0]);
                continue;
            }
            const double t0 = py_max(0.0, py_min(1.0, (zlo - a[1]) / (b[1] - a[1])));
            const double t1 = py_max(0.0, py_min(1.0, (zhi - a[1]) / (b[1] - a[1])));
            xs.push_back(a[0] + t0 * (b[0] - a[0]));
            xs.push_back(a[0] + t1 * (b[0] - a[0]));
        }
        if (xs.empty()) continue;
        double xlow = xs[0], xhigh = xs[0];
        for (double x : xs) {
            xlow = py_min(xlow, x);
            xhigh = py_max(xhigh, x);
        }
        const int i0 = floor_index(xlow - X0, CELL), i1 = floor_index(xhigh - X0, CELL);
        for (int i = std::max(0, i0); i <= std::min(nx - 1, i1); i++) out.push_back({i, k});
    }
    return out;
}

// A triangle seen from above.
std::vector<Pt2> footprint(const Tri& t) { return {{t[0][0], t[0][2]}, {t[1][0], t[1][2]}, {t[2][0], t[2][2]}}; }

// arenas_collide.py slab: the part of a triangle between heights y0 and y1, seen from above (empty: none).
std::vector<Pt2> slab(const Tri& tri, double y0, double y1) {
    std::vector<Vec3> p(tri.begin(), tri.end());
    for (int pass = 0; pass < 2; pass++) {
        const double lim = pass == 0 ? y0 : y1;
        auto keep = [&](double v) { return pass == 0 ? v >= y0 : v <= y1; };
        std::vector<Vec3> q;
        for (size_t i = 0; i < p.size(); i++) {
            const Vec3& a = p[i];
            const Vec3& b = p[(i + 1) % p.size()];
            const bool ka = keep(a[1]), kb = keep(b[1]);
            if (ka) q.push_back(a);
            if (ka != kb) {
                const double t = (lim - a[1]) / (b[1] - a[1]);
                q.push_back({a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2])});
            }
        }
        p = q;
        if (p.empty()) return {};
    }
    std::vector<Pt2> out;
    for (const Vec3& v : p) out.push_back({v[0], v[2]});
    return out;
}

// arenas_collide.py height_at: a floor triangle's height over (x, z) (just off it: its nearest place on it).
std::optional<double> height_at(const Tri& tri, double x, double z) {
    const Vec3 &a = tri[0], &b = tri[1], &c = tri[2];
    const double d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2]);
    if (std::fabs(d) < 1e-6) return std::nullopt;
    double l0 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d;
    double l1 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d;
    double l2 = 1 - l0 - l1;
    if (l0 < -1e-3 || l1 < -1e-3 || l2 < -1e-3) {
        l0 = py_max(0.0, l0);
        l1 = py_max(0.0, l1);
        l2 = py_max(0.0, l2);
        const double s = l0 + l1 + l2;
        l0 = l0 / s;
        l1 = l1 / s;
        l2 = l2 / s;
    }
    return l0 * a[1] + l1 * b[1] + l2 * c[1];
}

// ---- arenas_collide.py solve: the walkable ground ----------------------------------------------------------------------

// The stage's cells (CELL units from x X0, z zmin): what solve works out for each.
struct Solved {
    int zmin = 0, nx = 0, nz = 0, front = 0;
    std::vector<std::vector<double>> floors;   // the floors' heights over each cell (in the models' order)
    std::vector<uint8_t> has_ground;           // reached from the aisle's mouth
    std::vector<double> ground;                // ... at this height
    std::vector<uint8_t> blocked, free_, walk;
    std::vector<Tri> steep;                    // the models' triangles that aren't floors

    bool inside(int i, int k) const { return i >= 0 && i < nx && k >= 0 && k < nz; }
    size_t at(int i, int k) const { return size_t(k) * nx + i; }
    bool is_floor(int i, int k) const { return inside(i, k) && !floors[at(i, k)].empty(); }
    bool is_ground(int i, int k) const { return inside(i, k) && has_ground[at(i, k)]; }
    bool is_blocked(int i, int k) const { return inside(i, k) && blocked[at(i, k)]; }
    bool is_free(int i, int k) const { return inside(i, k) && free_[at(i, k)]; }
    bool is_walk(int i, int k) const { return inside(i, k) && walk[at(i, k)]; }
};

const int DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// arenas_collide.py flood: the cells reached from start, a step at a time, where ok(cell) (marked into `seen`).
template <typename Ok>
void flood(IPt start, Ok ok, std::vector<uint8_t>& seen, const Solved& s) {
    seen[s.at(start.first, start.second)] = 1;
    std::vector<IPt> todo = {start};
    while (!todo.empty()) {
        const IPt c = todo.back();
        todo.pop_back();
        for (const auto& d : DIRS) {
            const IPt e = {c.first + d[0], c.second + d[1]};
            if (ok(e) && !seen[s.at(e.first, e.second)]) {   // (ok only passes cells inside)
                seen[s.at(e.first, e.second)] = 1;
                todo.push_back(e);
            }
        }
    }
}

// The first of `values` nearest `to` (Python: min(values, key=lambda f: abs(f - to))).
double nearest(const std::vector<double>& values, double to) {
    double best = values[0];
    for (double f : values)
        if (std::fabs(f - to) < std::fabs(best - to)) best = f;
    return best;
}

// arenas_collide.py solve.
bool solve(const std::vector<Tri>& set_tris, const std::vector<Tri>& crowd_tris, Solved& s) {
    if (set_tris.empty()) return false;
    double zmax_model = set_tris[0][0][2];
    for (const Tri& t : set_tris)
        for (const Vec3& p : t) zmax_model = py_max(zmax_model, p[2]);
    const double zmax = zmax_model + 2 * CELL;
    const int zmin = MOUTH_Z;
    s.zmin = zmin;
    s.nx = (X1 - X0) / CELL;
    s.nz = floor_index(zmax - zmin, CELL) + 1;
    const size_t n_cells = size_t(s.nx) * s.nz;
    s.floors.assign(n_cells, {});
    s.has_ground.assign(n_cells, 0);
    s.ground.assign(n_cells, 0.0);
    s.blocked.assign(n_cells, 0);
    s.free_.assign(n_cells, 0);
    s.walk.assign(n_cells, 0);
    s.steep.clear();
    // the floors: up-facing triangles' heights over the cells they pass through; the rest are steep
    for (const Tri& t : set_tris) {
        if (std::fabs(normal_y(t)) >= UP) {
            for (const IPt& c : cells_of(footprint(t), zmin, s.nx, s.nz)) {
                const auto h = height_at(t, X0 + (c.first + .5) * CELL, zmin + (c.second + .5) * CELL);
                if (h) s.floors[s.at(c.first, c.second)].push_back(*h);
            }
        } else {
            s.steep.push_back(t);
        }
    }
    // the aisle: the game's own floor up the middle, wherever the models have no floor, till they've had one a while
    const int cx = (0 - X0) / CELL, half = AISLE_X / CELL;
    double y = 0.0;
    int run = 0;
    for (int k = 0; k < s.nz; k++) {
        std::vector<double> have;
        for (double f : s.floors[s.at(cx, k)])
            if (std::fabs(f - y) <= STEP) have.push_back(f);
        if (!have.empty()) {
            y = nearest(have, y);
            run += 1;
            if (run > 40) break;
        } else {
            run = 0;
            for (int i = cx - half; i < cx + half; i++) s.floors[s.at(i, k)].push_back(y);
        }
    }
    // the ground: flooded from the aisle's mouth, a step of at most STEP between neighbours (depth first, as Python's)
    const IPt start = {cx, 0};
    s.has_ground[s.at(cx, 0)] = 1;
    s.ground[s.at(cx, 0)] = 0.0;
    std::vector<IPt> todo = {start};
    while (!todo.empty()) {
        const IPt c = todo.back();
        todo.pop_back();
        const double h = s.ground[s.at(c.first, c.second)];
        for (const auto& d : DIRS) {
            const int ei = c.first + d[0], ek = c.second + d[1];
            if (s.is_ground(ei, ek) || !s.is_floor(ei, ek)) continue;
            std::vector<double> near;
            for (double f : s.floors[s.at(ei, ek)])
                if (std::fabs(f - h) <= STEP) near.push_back(f);
            if (!near.empty()) {
                s.has_ground[s.at(ei, ek)] = 1;
                s.ground[s.at(ei, ek)] = nearest(near, h);
                todo.push_back({ei, ek});
            }
        }
    }
    // what's in a man's way: a steep triangle through his body over the ground, a top low over it
    for (const Tri& t : s.steep) {
        for (const IPt& c : cells_of(footprint(t), zmin, s.nx, s.nz)) {
            if (!s.is_ground(c.first, c.second)) continue;
            const double g = s.ground[s.at(c.first, c.second)];
            const std::vector<Pt2> cut = slab(t, g + BODY0, g + BODY1);
            if (cut.empty()) continue;
            for (const IPt& e : cells_of(cut, zmin, s.nx, s.nz))
                if (e == c) {
                    s.blocked[s.at(c.first, c.second)] = 1;
                    break;
                }
        }
    }
    for (size_t a = 0; a < n_cells; a++) {
        if (!s.has_ground[a]) continue;
        const double g = s.ground[a];
        for (double f : s.floors[a])
            if (g + STEP < f && f < g + LOW_TOP) s.blocked[a] = 1;
    }
    // the crowd and barricades: their whole footprint
    for (const Tri& t : crowd_tris)
        for (const IPt& c : cells_of(footprint(t), zmin, s.nx, s.nz))
            if (s.is_ground(c.first, c.second)) s.blocked[s.at(c.first, c.second)] = 1;
    // the aisle between the barricades, to the old door's line: the game's
    for (int k = 0; k < (DOOR_LINE - zmin) / CELL + 1; k++) {
        for (int i = cx - half; i < cx + half; i++) {
            s.blocked[s.at(i, k)] = 0;
            if (!s.has_ground[s.at(i, k)]) {
                s.has_ground[s.at(i, k)] = 1;
                s.ground[s.at(i, k)] = 0.0;
            }
        }
    }
    // the aisle's own sides: up to the stage's front, nobody off it
    auto x_of = [](int i) { return X0 + i * CELL; };
    int front = DOOR_LINE;
    const int lo = cx - half - 1, hi = cx + half;
    int gap = 0;
    for (int k = (DOOR_LINE - zmin) / CELL; k < s.nz; k++) {
        bool closed_l = false, closed_r = false;
        for (int j = 0; j < 3; j++) {
            if (s.is_blocked(lo - j, k) || !s.is_ground(lo - j, k)) closed_l = true;
            if (s.is_blocked(hi + j, k) || !s.is_ground(hi + j, k)) closed_r = true;
        }
        if (closed_l && closed_r) {
            gap = 0;
            front = zmin + (k + 1) * CELL;
        } else {
            gap += 1;
            if (gap > 4) break;
        }
    }
    s.front = front;
    // the set's last model is the stage's back
    double zback_model = zmax;
    if (!s.steep.empty()) {
        bool first = true;
        for (const Tri& t : s.steep) {
            const double m = py_max(py_max(t[0][2], t[1][2]), t[2][2]);
            zback_model = first ? m : py_max(zback_model, m);
            first = false;
        }
    }
    const double zback = zback_model + BACK_MARGIN;
    for (int k = 0; k < s.nz; k++)
        for (int i = 0; i < s.nx; i++) {
            const size_t a = s.at(i, k);
            s.free_[a] = s.has_ground[a] && !s.blocked[a] && zmin + (k + 1) * CELL <= zback &&
                         (zmin + k * CELL >= front || (x_of(i) >= -AISLE_X && x_of(i) + CELL <= AISLE_X));
        }
    // room for a man: the ROOM x ROOM blocks of free cells that reach the aisle's mouth, block by block
    std::vector<uint8_t> fit(n_cells, 0), seen_fit(n_cells, 0);
    for (int k = 0; k < s.nz; k++)
        for (int i = 0; i < s.nx; i++) {
            bool all = true;
            for (int a = 0; a < ROOM && all; a++)
                for (int b = 0; b < ROOM && all; b++) all = s.is_free(i + a, k + b);
            fit[s.at(i, k)] = all;
        }
    auto in_fit = [&](IPt e) { return s.inside(e.first, e.second) && fit[s.at(e.first, e.second)]; };
    for (int i = 0; i < s.nx; i++)   // (the seeds: blocks on the mouth's row; the flood's union doesn't depend on order)
        if (fit[s.at(i, 0)] && !seen_fit[s.at(i, 0)]) flood({i, 0}, in_fit, seen_fit, s);
    for (int k = 0; k < s.nz; k++)
        for (int i = 0; i < s.nx; i++)
            if (seen_fit[s.at(i, k)])
                for (int a = 0; a < ROOM; a++)
                    for (int b = 0; b < ROOM; b++) s.walk[s.at(i + a, k + b)] = 1;
    if (!s.walk[s.at(start.first, start.second)]) {
        s.walk.assign(n_cells, 0);
        flood(start, [&](IPt e) { return s.is_free(e.first, e.second); }, s.walk, s);
    }
    return true;
}

// ---- arenas_collide.py: the walls --------------------------------------------------------------------------------------

using Edges = std::map<IPt, std::vector<IPt>>;   // (sorted as Python's sorted(edges): by x, then z)

// arenas_collide.py edges_of: the walkable area's edge, each piece with the walkable side on its right.
// (Python goes through the set `walk` in CPython's hash order, which orders each corner's list; here row by row. The
// order could only matter where trace takes the first of two -- it refuses then -- or two turn alike: it refuses that too.
// The developer check shuffled every list: the Python's 23 results didn't change.)
Edges edges_of(const Solved& s) {
    Edges out;
    for (int k = 0; k < s.nz; k++)
        for (int i = 0; i < s.nx; i++) {
            if (!s.walk[s.at(i, k)]) continue;
            const int x = X0 + i * CELL, z = s.zmin + k * CELL;
            if (!s.is_walk(i, k - 1) && k > 0) out[{x + CELL, z}].push_back({x, z});              // (the bottom)
            if (!s.is_walk(i, k + 1)) out[{x, z + CELL}].push_back({x + CELL, z + CELL});        // (the top)
            if (!s.is_walk(i - 1, k)) out[{x, z}].push_back({x, z + CELL});                      // (the left)
            if (!s.is_walk(i + 1, k)) out[{x + CELL, z + CELL}].push_back({x + CELL, z});        // (the right)
        }
    return out;
}

// arenas_collide.py trace: the open path from the aisle's mouth (-140, zmin) to (140, zmin), and the closed loops.
bool trace(Edges& edges, int zmin, std::vector<IPt>& path, std::vector<std::vector<IPt>>& loops, std::string& why) {
    bool order_matters = false;
    // take: the next corner from a (came: the one before): the only way on, or at a corner two cells meet at, the
    // sharpest right turn
    auto take = [&](IPt a, std::optional<IPt> came) -> std::optional<IPt> {
        auto it = edges.find(a);
        if (it == edges.end() || it->second.empty()) return std::nullopt;
        std::vector<IPt>& opts = it->second;
        if (opts.size() == 1 || !came) {
            if (opts.size() > 1) order_matters = true;   // (Python's set order would choose: see edges_of)
            const IPt b = opts.front();
            opts.erase(opts.begin());
            return b;
        }
        const long long dx = a.first - came->first, dz = a.second - came->second;
        auto turn = [&](IPt b) { return dx * (b.second - a.second) - dz * (b.first - a.first); };   // (< 0: right)
        size_t best = 0;
        for (size_t j = 1; j < opts.size(); j++)
            if (turn(opts[j]) < turn(opts[best])) best = j;
        for (size_t j = 0; j < opts.size(); j++)   // (two that turn alike: Python's min takes the first in set order)
            if (j != best && turn(opts[j]) == turn(opts[best])) order_matters = true;
        const IPt b = opts[best];
        opts.erase(opts.begin() + best);
        return b;
    };
    path = {{-AISLE_X, zmin}};
    std::optional<IPt> came;
    IPt a = path[0];
    while (true) {
        const auto b = take(a, came);
        if (!b) { why = "the edge stops"; return false; }
        path.push_back(*b);
        came = a;
        a = *b;
        if (a == IPt{AISLE_X, zmin}) break;
        if (path.size() >= 100000) { why = "the edge goes on and on"; return false; }
    }
    loops.clear();
    for (auto& entry : edges) {
        const IPt start = entry.first;
        while (!entry.second.empty()) {
            std::vector<IPt> lp = {start};
            std::optional<IPt> came_l;
            IPt c = start;
            while (true) {
                const auto b = take(c, came_l);
                if (!b) { why = "a loop stops"; return false; }
                if (*b == start) break;
                lp.push_back(*b);
                came_l = c;
                c = *b;
            }
            loops.push_back(lp);
        }
    }
    if (order_matters) { why = "a wall corner where Python's set order decides (none in the 23 arenas)"; return false; }
    return true;
}

// arenas_collide.py dp: Douglas-Peucker -- a line's corners kept where it strays more than tol.
std::vector<IPt> dp(const std::vector<IPt>& pts, double tol) {
    if (pts.size() < 3) return pts;
    const IPt a = pts.front(), b = pts.back();
    const long long dx = b.first - a.first, dz = b.second - a.second;
    const double l = int_hypot(dx, dz);
    double far = -1.0;
    size_t at = 0;
    for (size_t i = 1; i + 1 < pts.size(); i++) {
        const IPt p = pts[i];
        const double d = l != 0.0 ? double(std::abs((p.first - a.first) * dz - (p.second - a.second) * dx)) / l
                                  : int_hypot(p.first - a.first, p.second - a.second);
        if (d > far) {
            far = d;
            at = i;
        }
    }
    if (far <= tol) return {a, b};
    std::vector<IPt> one = dp(std::vector<IPt>(pts.begin(), pts.begin() + at + 1), tol);
    std::vector<IPt> two = dp(std::vector<IPt>(pts.begin() + at, pts.end()), tol);
    one.pop_back();
    one.insert(one.end(), two.begin(), two.end());
    return one;
}

// arenas_collide.py simplify_loop: a closed loop cut at the corner furthest from its first, each half simplified.
std::vector<IPt> simplify_loop(const std::vector<IPt>& lp, double tol) {
    size_t a = 0;
    long long far = -1;
    for (size_t i = 0; i < lp.size(); i++) {
        const long long ddx = lp[i].first - lp[0].first, ddz = lp[i].second - lp[0].second;
        if (ddx * ddx + ddz * ddz > far) {
            far = ddx * ddx + ddz * ddz;
            a = i;
        }
    }
    std::vector<IPt> one = dp(std::vector<IPt>(lp.begin(), lp.begin() + a + 1), tol);
    std::vector<IPt> second(lp.begin() + a, lp.end());
    second.push_back(lp[0]);
    std::vector<IPt> two = dp(second, tol);
    one.pop_back();
    two.pop_back();
    one.insert(one.end(), two.begin(), two.end());
    return one;
}

// arenas_collide.py area2: twice a loop's signed area.
long long area2(const std::vector<IPt>& lp) {
    long long sum = 0;
    for (size_t i = 0; i < lp.size(); i++) {
        const IPt p = lp[i], q = lp[(i + 1) % lp.size()];
        sum += (long long)p.first * q.second - (long long)q.first * p.second;
    }
    return sum;
}

// arenas_collide.py wall_kind: 1 a crash (a model behind the wall, in a man's way), 0 a drop.
int wall_kind(IPt a, IPt b, const Solved& s) {
    const long long dx = b.first - a.first, dz = b.second - a.second;
    double L = int_hypot(dx, dz);
    if (L == 0.0) L = 1.0;
    // behind the wall: left of its way
    const double bx = double(-dz) / L, bz = double(dx) / L;
    const int n = std::max(1, floor_index(L, CELL));
    int hits = 0;
    for (int j = 0; j < n; j++) {
        const double t = (j + .5) / n;
        const double x = a.first + t * dx, z = a.second + t * dz;
        // his side of it: the ground he's on
        const int ci = floor_index(x - bx * CELL * .7 - X0, CELL), ck = floor_index(z - bz * CELL * .7 - s.zmin, CELL);
        const double g = s.is_ground(ci, ck) ? s.ground[s.at(ci, ck)] : 0.0;
        bool found = false;
        for (int step = 1; step < BUMP + 2; step++) {
            const int i = floor_index(x + bx * step * CELL * .7 - X0, CELL), k = floor_index(z + bz * step * CELL * .7 - s.zmin, CELL);
            if (s.is_blocked(i, k)) {
                found = true;
                break;
            }
        }
        if (!found) {
            const double px = x + bx * BEHIND / 2.0, pz = z + bz * BEHIND / 2.0;
            for (const Tri& t3 : s.steep) {
                const double xmax = py_max(py_max(t3[0][0], t3[1][0]), t3[2][0]), xmin = py_min(py_min(t3[0][0], t3[1][0]), t3[2][0]);
                const double zmax = py_max(py_max(t3[0][2], t3[1][2]), t3[2][2]), zmin = py_min(py_min(t3[0][2], t3[1][2]), t3[2][2]);
                if (xmax < px - BEHIND || xmin > px + BEHIND || zmax < pz - BEHIND || zmin > pz + BEHIND) continue;
                const std::vector<Pt2> cut = slab(t3, g + BODY0, g + BODY1);
                if (cut.empty()) continue;
                // its part in a man's way close behind this bit of the wall, not in front of it
                for (const Pt2& q : cut) {
                    const double d = (q[0] - x) * bx + (q[1] - z) * bz;
                    if (0 <= d && d <= BEHIND && std::fabs((q[0] - x) * bz - (q[1] - z) * bx) <= std::max(CELL * 2, BEHIND)) {
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
        }
        if (found) hits += 1;
    }
    return hits * 3 >= n ? 1 : 0;
}

}  // namespace

// ---- arenas_collide.py result_of: an arena's walls and the running game's map ------------------------------------------

bool result_of(const std::vector<Tri>& set_tris, const std::vector<Tri>& crowd_tris, Collide& out, std::string& why) {
    Solved s;
    if (!solve(set_tris, crowd_tris, s)) { why = "no stage set"; return false; }
    Edges edges = edges_of(s);
    std::vector<IPt> path;
    std::vector<std::vector<IPt>> loops;
    if (!trace(edges, s.zmin, path, loops, why)) return false;
    path = dp(path, SIMPLIFY);
    std::vector<std::vector<IPt>> props;
    for (const auto& lp : loops) {
        std::vector<IPt> simple = simplify_loop(lp, SIMPLIFY);
        if (simple.size() < 3) simple = lp;   // (too small to simplify away: as it is)
        if (area2(simple) == 0) { why = "a prop's loop with no area"; return false; }
        props.push_back(simple);
    }
    out.path.clear();
    for (size_t i = 0; i + 1 < path.size(); i++) out.path.push_back({path[i], path[i + 1], wall_kind(path[i], path[i + 1], s)});
    out.props.clear();
    for (const auto& p : props) {
        std::vector<Wall> ws;
        for (size_t i = 0; i < p.size(); i++) ws.push_back({p[i], p[(i + 1) % p.size()], 1});
        out.props.push_back(ws);
    }
    // the running game's map
    int xmin = INT_MAX, xmax = INT_MIN, zmin_w = INT_MAX, zmax_w = INT_MIN;
    for (int k = 0; k < s.nz; k++)
        for (int i = 0; i < s.nx; i++)
            if (s.walk[s.at(i, k)]) {
                xmin = std::min(xmin, X0 + i * CELL);
                xmax = std::max(xmax, X0 + i * CELL);
                zmin_w = std::min(zmin_w, s.zmin + k * CELL);
                zmax_w = std::max(zmax_w, s.zmin + k * CELL);
            }
    if (xmin == INT_MAX) { why = "nowhere to walk"; return false; }
    const long long gx0 = py_idiv(xmin, GRID) * GRID, gx1 = (py_idiv(xmax, GRID) + 1) * GRID;
    const long long gz0 = py_idiv(zmin_w, GRID) * GRID, gz1 = (py_idiv(zmax_w, GRID) + 1) * GRID;
    out.gx0 = int(gx0);
    out.gz0 = int(gz0);
    out.gw = int(py_idiv(gx1 - gx0, GRID));
    out.gh = int(py_idiv(gz1 - gz0, GRID));
    out.cells.clear();
    for (int gk = 0; gk < out.gh; gk++)
        for (int gi = 0; gi < out.gw; gi++) {
            std::vector<double> hs;
            int n_cs = 0;
            for (int a = 0; a < GRID / CELL; a++)
                for (int b = 0; b < GRID / CELL; b++) {
                    const int ci = int(py_idiv(gx0 + gi * GRID + a * CELL - X0, CELL));
                    const int ck = int(py_idiv(gz0 + gk * GRID + b * CELL - s.zmin, CELL));
                    n_cs++;
                    if (s.is_walk(ci, ck)) hs.push_back(s.ground[s.at(ci, ck)]);
                }
            if (int(hs.size()) * 2 < n_cs) {
                out.cells.push_back(NO_CELL);
            } else {
                double sum = 0.0;   // (Python 3.10's sum(): left to right)
                for (double h : hs) sum += h;
                out.cells.push_back(py_round(sum / double(hs.size())));
            }
        }
    out.front = std::min(s.front, zmax_w + CELL - 100);   // (an aisle that ends at a doorway, nothing behind)
    out.bbox[0] = xmin;
    out.bbox[1] = xmax + CELL;
    out.bbox[2] = zmin_w;
    out.bbox[3] = zmax_w + CELL;
    // the crowd's rows by the aisle: how far back they come and how high
    bool any_row = false;
    long long crowd_z = 0, crowd_y = 0;
    for (const Tri& t : crowd_tris) {
        const double ymax = std::max({t[0][1], t[1][1], t[2][1]}), zmax = std::max({t[0][2], t[1][2], t[2][2]});
        const double xnear = std::min({std::fabs(t[0][0]), std::fabs(t[1][0]), std::fabs(t[2][0])});
        if (!(ymax <= 300 && zmax > DOOR_LINE && xnear <= 1200)) continue;
        for (const Vec3& v : t) {
            crowd_z = any_row ? std::max(crowd_z, (long long)v[2]) : (long long)v[2];
            crowd_y = any_row ? std::max(crowd_y, (long long)v[1]) : (long long)v[1];
            any_row = true;
        }
    }
    out.crowd_z = any_row ? crowd_z : 0;
    out.crowd_y = any_row ? crowd_y : 0;
    // the walkable ground's highest and lowest (Python floats: max / min over the set W -- its order can't matter: equal
    // heights are the same float, never 0.0 against -0.0), the walls' count, the walkable cells'
    bool first = true;
    int walk_cells = 0;
    for (int k = 0; k < s.nz; k++)
        for (int i = 0; i < s.nx; i++)
            if (s.walk[s.at(i, k)]) {
                const double g = s.ground[s.at(i, k)];
                out.ymax = first ? g : py_max(out.ymax, g);
                out.ymin = first ? g : py_min(out.ymin, g);
                first = false;
                walk_cells++;
            }
    out.walk_cells = walk_cells;
    out.n_walls = int(out.path.size());
    for (const auto& p : out.props) out.n_walls += int(p.size());
    return true;
}

}  // namespace collide
}  // namespace ri
