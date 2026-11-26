// ri_wmarenas.cpp (phase3 p3wmport, 2026-11-19): the arcade's arenas in the pack -- the C++ twins of mod\gen\arenas.py
// build (WCW/nWo Revenge's six arenas; WrestleMania 2000's seven through ri_wmvenues), arenas_stage3d.py (Revenge's 3D
// stages), arenas_ring.py (everything round the ring), arenas_entrance.py (the entrances' close scenes), arenas_raw.py and
// arenas_stages.py (No Mercy's own ten made detailed: their rows from src\main\arcade_firstrun_stages.cpp, which the first
// start already uses), the tables' header (write_tables / effects_and_vs), arenas_walls.py (the walls' headers from the
// collision results) and tools\romrender\rr_arena.py (the ARENA page photo and the VS backdrop, rendered by rr_core).
//
// Files go into the pack in the Python's order exactly (C.F: one / pair as the Python calls them -- watch which of a
// piece's mesh and picture it asks for first); the headers' text is the Python's line for line.
#include "ri_wmport.h"
#include "ri_wmmesh.h"
#include "ri_wmvenues.h"
#include "ri_arenascan.h"
#include "rr_core.h"
#include "../main/arcade_firstrun_ports.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace ri {
namespace {

uint32_t r32(const Bytes& ram, uint32_t a) { return be32(ram, a & 0x7FFFFF); }
uint32_t r16(const Bytes& ram, uint32_t a) { return be16(ram, a & 0x7FFFFF); }
std::string hx(uint32_t v) { return strf("0x%X", v); }
std::string hexlist(const std::vector<uint32_t>& v) { std::string s; for (size_t i = 0; i < v.size(); i++) s += (i ? ", " : "") + hx(v[i]); return s; }

// ---- a picture: ("pic", texture, palette) or a Revenge palette file (its texture the next) ----------------------------
struct Pic { bool own = false; int rev = 0; Bytes tex, pal; };
Pic rev_pic(int p) { Pic x; x.rev = p; return x; }
Pic own_pic(Bytes t, Bytes p) { Pic x; x.own = true; x.tex = std::move(t); x.pal = std::move(p); return x; }

// ---- Revenge's arena table (arenas.py table: its match RAM, the same image in all six) ---------------------------------
struct RevTable { std::vector<int> meshes, pals, ring, aisle; };

// ---- the arcade's arenas (arenas.py ARENAS: (name, Revenge's arena or "wm<v>") -- an arena's number is 10 + its row) ----
struct ArenaDef { const char* name; int rev; int wm; };     // rev: Revenge's arena (-1: none); wm: WM2000's venue (-1)
const ArenaDef ARENAS[] = {
    { "MONDAY NITRO", 0, -1 }, { "HALLOWEEN HAVOC", 4, -1 }, { "BASH AT THE BEACH", 3, -1 }, { "RAW IS WAR WM 2000", -1, 0 },
    { "SOULED OUT", 1, -1 }, { "SUPERBRAWL", 2, -1 }, { "STARRCADE", 5, -1 },
    { "SUNDAY NIGHT HEAT", -1, 1 }, { "ROYAL RUMBLE WM 2000", -1, 2 }, { "KING OF THE RING WM 2000", -1, 3 },
    { "SURVIVOR SERIES WM 2000", -1, 4 }, { "WRESTLEMANIA WM 2000", -1, 5 }, { "SUMMERSLAM WM 2000", -1, 6 } };
constexpr int N_ARENAS = 13;
constexpr int BASE = 7, SET_MAX = 64;
constexpr int AISLE_FLOOR = 0x2F5;
const char* label_of(int v) {
    switch (v) { case 0: return "MONDAY NITRO"; case 4: return "HALLOWEEN HAVOC"; case 3: return "BASH AT THE BEACH";
                 case 1: return "SOULED OUT"; case 2: return "SUPERBRAWL"; default: return "STARRCADE"; }
}
int banner_of(int v) { switch (v) { case 0: return 0x36B; case 4: return 0x3AA; case 3: return 0x3C4; case 1: return 0x35A; case 5: return 0x3D8; default: return 0x385; } }
int logo_of(int v) { switch (v) { case 0: return 0x36B; case 4: return 0x3AA; case 3: return 0x3C4; case 1: return 0x35A; case 2: return 0x38B; default: return 0x3D8; } }

// ---- the state of one build --------------------------------------------------------------------------------------------
struct A {
    WmCtx& C;
    ojson scenes;                                // Revenge's stage3d scenes.json (ri_arenascan)
    std::map<int, ojson> meshes;                 // per Revenge arena v: its 3D scene's models (meshes.json + EXTRA)
    ojson wm_scenes, wm_venues;
    std::map<int, WmVenue> venues;               // WrestleMania 2000's (ri_wmvenues), made once
    ojson labels;                                // frozen\arena_labels.json
    explicit A(WmCtx& c) : C(c) {}
    // (arenasplit 2026-11-24) Each arcade arena is its own source game's: Revenge's six from Revenge (and No Mercy's files),
    // WrestleMania 2000's seven from WrestleMania 2000 (and No Mercy's) -- nothing of the other game's in either (the only
    // files they share are our own: the white picture, the empty mesh, the labels' font). So with one of the two games its
    // arenas are made as with both, and the other's rows are zeros of the build's shape (C.shape: the layouts in
    // firstrun\hdrfill.bin), so every row keeps its place; with both, everything as always.
    bool full() const { return C.rev && C.wm; }
    bool here(int k) const { return ARENAS[k].wm >= 0 ? C.wm != nullptr : C.rev != nullptr; }
    const HdrFill& shape() const {
        if (!C.shape) fail("arenas: a source game isn't there and the build's layouts weren't given (firstrun\\hdrfill.bin)");
        return *C.shape;
    }
    // row `row` of `symbol` (`rows` rows of one size) as the build's number of zeros there
    std::string zero_row(const char* symbol, size_t row, size_t rows) const {
        const uint32_t size = hdr_symbol_size(shape(), symbol);
        if (!size || size % rows) fail("arenas: the build's %s isn't %zu rows", symbol, rows);
        const long n = hdr_leaves_in(shape(), symbol, (int64_t)(size / rows * row), (int64_t)(size / rows * (row + 1)));
        if (n < 0) fail("arenas: the build has no %s", symbol);
        return hdr_zero_group((size_t)n);
    }
    // the second size of `symbol` [rows][n] (elements `elem` bytes each), as the build has it
    size_t shape_cols(const char* symbol, size_t rows, size_t elem) const {
        const uint32_t size = hdr_symbol_size(shape(), symbol);
        if (!size || size % (rows * elem)) fail("arenas: the build's %s isn't %zu rows", symbol, rows);
        return size / (rows * elem);
    }
    const Bytes& rf(int f) { return C.revf((uint32_t)f); }
    RevTable table(int v) {
        const Bytes& ram = C.image("rev_arena");
        RevTable t;
        int n = sx8(ram[(0x80036CAC + v) & 0x7FFFFF]);
        for (int i = 0; i < n; i++) t.meshes.push_back(r16(ram, r32(ram, 0x80036CB4 + 4 * v) + 2 * i));
        for (int i = 0; i < n; i++) t.pals.push_back(r16(ram, r32(ram, 0x80036CCC + 4 * v) + 2 * i));
        for (int i = 0; i < 6; i++) t.ring.push_back(r16(ram, r32(ram, 0x80036C74 + 4 * v) + 2 * i));
        for (int i = 0; i < 3; i++) t.aisle.push_back(r16(ram, r32(ram, 0x80036C94 + 4 * v) + 2 * i));
        return t;
    }
    // arenas.pic_pair: F.pair of a picture -> (texture id, palette id)
    std::pair<uint32_t, uint32_t> pic_pair(const Pic& p) {
        if (p.own) return C.F.pair(p.tex, p.pal);
        return C.F.pair(rf(p.rev + 1), rf(p.rev));
    }
    const WmVenue& venue(int v) {
        auto it = venues.find(v);
        if (it == venues.end()) it = venues.emplace(v, wm_venue(v, *C.wm, wm_scenes, wm_venues, C.D.dir / "frozen")).first;
        return it->second;
    }
    Bytes label(const std::string& text) {
        if (!labels.contains(text)) fail("arena label \"%s\" not frozen", text.c_str());
        Bytes b(C.nmf(0x462B).begin(), C.nmf(0x462B).begin() + 48);
        std::string h = labels[text].get<std::string>();
        for (size_t i = 0; i + 1 < h.size(); i += 2) b.push_back((uint8_t)std::stoi(h.substr(i, 2), nullptr, 16));
        return b;
    }
};

// ==== arenas_stage3d.py: Revenge's 3D stages ==============================================================================
const int STAGE3D_PIC[6] = { 0x3F, 0x43, 0x45, 0x3D, 0x47, 0x41 };   // by Revenge's arena v: its scene's picture file
constexpr double S3_SCALE = 0.3, AISLE_END = 1230.0;
// Changed for new features (Ultimate No Mercy): the stagescale agent, 2026-11-20 (arenas_stage3d.py close_pt): the close scene
// placed as Revenge's own entrance places the set -- x 0.3 turned 2 degrees about y + (16.2, 0, -57.4), its pose spot moved
// onto No Mercy's (-515) -- not with Revenge's pose spot at the origin (CLOSE_Z 454, the set 515 too far behind the
// Superstar); the floor carried on to -1250; the colour the vertex alpha x 255 (Revenge's white light)
constexpr double CLOSE_C = 0.2998172640, CLOSE_S = 0.0104698490, CLOSE_TX = 16.2, CLOSE_TZ = -60.9, CLOSE_NEAR = -1250.0;
constexpr int CLOSE_GREY = 255;
// close_pt: a set point where the close scene has it (Python's evaluation order: ((C x) + (S z)) + TX, S3_SCALE y + 0.0,
// ((C z) - (S x)) + TZ)
std::array<double, 3> close_pt(double x, double y, double z) {
    return { CLOSE_C * x + CLOSE_S * z + CLOSE_TX, S3_SCALE * y + 0.0, CLOSE_C * z - CLOSE_S * x + CLOSE_TZ };
}
const std::vector<std::vector<int>> RING_PARTS_0 = { { 0 }, { 1, 3 }, { 2, 4 }, { 5, 6, 7, 8, 9, 10 }, { 11, 12, 13, 14, 15, 16 }, { 17 }, { 18 },
                                                     { 19, 20, 21 }, { 24, 25, 26 }, { 22, 27 }, { 23, 28 } };
std::vector<int> aisle_of(int v) { if (v == 3) return { 0x3B2, 0x3B3 }; if (v == 2) return { 0x382 }; return { 0x314 }; }
std::vector<int> extra_of(int v) { if (v == 3) return { 0, 13, 14, 15, 16 }; return {}; }
std::vector<int> board_of(int v) { if (v == 3) return { 13, 14, 15, 16 }; return {}; }

std::vector<Bytes> entries(const Bytes& d) {
    uint32_t n = be32(d, 0);
    std::vector<uint32_t> offs;
    for (uint32_t i = 0; i < n; i++) offs.push_back(be32(d, 4 + 4 * i));
    offs.push_back((uint32_t)d.size());
    std::vector<Bytes> out;
    for (uint32_t i = 0; i < n; i++) out.emplace_back(d.begin() + offs[i], d.begin() + offs[i + 1]);
    return out;
}

// extra_model: scene model k as a meshes.json mesh (its points as they are)
ojson extra_model(A& a, int v, int k) {
    const ojson& S = a.scenes["scenes"][v];
    const ojson& m = S["models"][k];
    auto geo = entries(a.rf(S["geo_file"].get<int>()));
    const Bytes& vb = geo[m["vtx_entry"].get<int>()];
    const Bytes& tb = geo[m["tri_entry"].get<int>()];
    ojson V = ojson::array(), UV = ojson::array(), RGBA = ojson::array(), T = ojson::array();
    int W = m["width"].get<int>(), H = m["height"].get<int>();
    for (int i = 0; i < m["n_verts"].get<int>(); i++) {
        const uint8_t* p = &vb[16 * i];
        V.push_back({ sx16(be16(p)), sx16(be16(p + 2)), sx16(be16(p + 4)) });
        UV.push_back({ sx16(be16(p + 8)) / 32.0 / W, sx16(be16(p + 10)) / 32.0 / H });
        RGBA.push_back({ p[12], p[13], p[14], p[15] });
    }
    std::vector<std::pair<int, int>> starts; int acc = 0;
    for (auto& b : m["batches"]) { starts.push_back({ b[1].get<int>(), acc }); acc += b[0].get<int>(); }
    int cur = 0;
    for (int ti = 0; ti < m["n_tris"].get<int>(); ti++) {
        for (auto& [at, off] : starts) if (at == ti) cur = off;
        int a_ = tb[4 * ti], b_ = tb[4 * ti + 1], c_ = tb[4 * ti + 2], rot = tb[4 * ti + 3];
        int t[3] = { a_, b_, c_ };
        if (rot == 1) { t[0] = b_; t[1] = c_; t[2] = a_; } else if (rot == 2) { t[0] = c_; t[1] = a_; t[2] = b_; }
        T.push_back({ cur + t[0], cur + t[1], cur + t[2] });
    }
    ojson out;
    out["name"] = strf("x_m%02d", k); out["node"] = -1; out["model"] = k;
    out["texture"] = strf("t%02d_p%02d.png", m["tex_entry"].get<int>(), m["pal_entry"].get<int>());
    out["tex_size"] = { W, H }; out["ci8"] = m["ci8"];
    out["wrap"] = { { "cms", m["cms"] }, { "masks", m["masks"] }, { "cmt", m["cmt"] }, { "maskt", m["maskt"] } };
    out["verts"] = V; out["uv"] = UV; out["rgba"] = RGBA; out["tris"] = T;
    return out;
}
const ojson& s3_models(A& a, int v) {
    auto it = a.meshes.find(v);
    if (it != a.meshes.end()) return it->second;
    ojson ms = rev_scene_meshes(v, a.scenes, *a.C.rev)["meshes"];
    for (int k : extra_of(v)) ms.push_back(extra_model(a, v, k));
    return a.meshes.emplace(v, ms).first->second;
}
double ring_z(A& a, int v) {
    const ojson& v0 = s3_models(a, v)[0]["verts"];
    double lo = v0[0][2].get<double>();
    for (auto& p : v0) lo = std::min(lo, p[2].get<double>());
    return AISLE_END - S3_SCALE * lo;
}
std::vector<std::vector<int>> ring_groups(A& a, int v) {
    if (v == 0) return RING_PARTS_0;
    const ojson& ms = s3_models(a, v);
    struct G { std::string tex; std::vector<int> ks; size_t nv, nt; };
    std::vector<G> groups;
    for (size_t k = 0; k < ms.size(); k++) {
        std::string tex = ms[k]["texture"].get<std::string>();
        size_t nv = ms[k]["verts"].size(), nt = ms[k]["tris"].size();
        G* g = nullptr;
        for (auto& x : groups) if (x.tex == tex && x.nv + nv <= 127 && x.nt + nt <= 127) { g = &x; break; }
        if (g) { g->ks.push_back((int)k); g->nv += nv; g->nt += nt; }
        else groups.push_back({ tex, { (int)k }, nv, nt });
    }
    std::vector<std::vector<int>> out;
    for (auto& g : groups) out.push_back(g.ks);
    return out;
}
Pic s3_picture(A& a, int v, const ojson& m) {
    auto e = entries(a.rf(STAGE3D_PIC[v]));
    int t = 0, p = 0;
    sscanf(m["texture"].get<std::string>().c_str(), "t%d_p%d", &t, &p);
    Bytes tex = e[t];
    int w = m["tex_size"][0].get<int>(), h = m["tex_size"][1].get<int>();
    if ((1 << tex[6]) != w || (1 << tex[7]) != h) fail("stage3d picture size");
    tex[0] = (uint8_t)(w - 1); tex[1] = (uint8_t)(h - 1);
    tex[4] = (uint8_t)m["wrap"]["cms"].get<int>(); tex[5] = (uint8_t)m["wrap"]["cmt"].get<int>();
    return own_pic(tex, e[p]);
}
double max_span(const std::vector<std::array<double, 3>>& pts) {
    double span = 0;
    for (int k = 0; k < 3; k++) {
        double lo = pts[0][k], hi = pts[0][k];
        for (auto& p : pts) { lo = std::min(lo, p[k]); hi = std::max(hi, p[k]); }
        span = std::max(span, hi - lo);
    }
    return span;
}
void on_grid(std::vector<std::array<double, 3>>& pts) {
    double sc = std::max(1.0, std::ceil(max_span(pts) / 230.0));
    for (auto& p : pts) for (int k = 0; k < 3; k++) p[k] = py_round_half_even(p[k] / sc) * sc;
}
// mesh(): the models idx as one No Mercy mesh at SCALE x set + move (close: close_pt instead), texels from 0, colour the
// vertex alpha x grey / 255
std::pair<Bytes, Pic> s3_mesh(A& a, int v, const std::vector<int>& idx, const std::array<double, 3>& move, int grey, bool close = false) {
    const ojson& ms = s3_models(a, v);
    Mesh M; std::vector<std::array<double, 2>> uvf;
    for (int k : idx) {
        const ojson& m = ms[k];
        int n = (int)M.pts.size();
        for (auto& p : m["verts"])
            M.pts.push_back(close ? close_pt(p[0].get<double>(), p[1].get<double>(), p[2].get<double>())
                                  : std::array<double, 3>{ S3_SCALE * p[0].get<double>() + move[0], S3_SCALE * p[1].get<double>() + move[1], S3_SCALE * p[2].get<double>() + move[2] });
        for (auto& c : m["uv"]) uvf.push_back({ c[0].get<double>(), c[1].get<double>() });
        for (size_t i = 0; i < m["verts"].size(); i++) { uint8_t g = (uint8_t)(m["rgba"][i][3].get<int>() * grey / 255); M.rgb.push_back({ g, g, g }); }
        for (auto& t : m["tris"]) M.tris.push_back({ t[0].get<int>() + n, t[1].get<int>() + n, t[2].get<int>() + n });
    }
    const ojson& m0 = ms[idx[0]];
    int w = m0["tex_size"][0].get<int>(), h = m0["tex_size"][1].get<int>();
    bool mir[2] = { m0["wrap"]["cms"].get<int>() != 0, m0["wrap"]["cmt"].get<int>() != 0 };
    for (int k : idx) if (ms[k]["texture"] != m0["texture"]) fail("stage3d: one picture a mesh");
    std::vector<int64_t> vals[2];
    for (int ax = 0; ax < 2; ax++) {
        int size = ax ? h : w; int64_t per = (int64_t)size * (mir[ax] ? 2 : 1);
        std::vector<double> t;
        for (auto& c : uvf) t.push_back(c[ax] * size);
        double lo = *std::min_element(t.begin(), t.end());
        double base = std::floor(lo / per + 1e-6) * per;
        for (double c : t) vals[ax].push_back(std::min<int64_t>(std::max<int64_t>((int64_t)py_round_half_even(c - base), 0), 255 + 240));
        int64_t mn = *std::min_element(vals[ax].begin(), vals[ax].end());
        int64_t cap = 255 + 16 * (mn / 16);                  // (mn >= 0: Python's // the same)
        for (auto& c : vals[ax]) c = std::min(c, cap);
    }
    on_grid(M.pts);
    for (size_t i = 0; i < M.pts.size(); i++) M.uv.push_back({ vals[0][i], vals[1][i] });
    return { M.file(), s3_picture(a, v, m0) };
}
// door_part (Souled Out's board in its box's mouth)
std::pair<Bytes, Pic> s3_door(const std::array<double, 3>& move, int grey, bool close = false) {
    const double q[4][3] = { { -600, 0, 0 }, { 600, 0, 0 }, { 600, 1125, 0 }, { -600, 1125, 0 } };
    Mesh M;
    for (auto& p : q) M.pts.push_back(close ? close_pt(p[0], p[1], p[2]) : std::array<double, 3>{ S3_SCALE * p[0] + move[0], S3_SCALE * p[1] + move[1], S3_SCALE * p[2] + move[2] });
    on_grid(M.pts);
    M.uv = { { 64, 64 }, { 0, 64 }, { 0, 0 }, { 64, 0 } };
    M.rgb.assign(4, { (uint8_t)grey, (uint8_t)grey, (uint8_t)grey });
    M.tris = { { 0, 2, 1 }, { 0, 3, 2 } };
    return { M.file(), rev_pic(0x358) };
}
// ring_parts: the match's stage set -- the old set's aisle floor, then the stage merged by picture, Souled Out's door
std::vector<std::pair<Bytes, Pic>> s3_ring_parts(A& a, int v) {
    RevTable t = a.table(v);
    std::map<int, int> pal;
    for (size_t i = 0; i < t.meshes.size(); i++) pal[t.meshes[i]] = t.pals[i];      // (dict(zip): the last wins)
    std::vector<Mesh> am;
    for (int m : aisle_of(v)) { Mesh x = Mesh::from_file(a.rf(m)); for (auto& c : x.rgb) c = { 255, 255, 255 }; am.push_back(x); }
    std::vector<std::pair<Bytes, Pic>> out;
    out.push_back({ join(am).file(), rev_pic(pal.at(aisle_of(v)[0])) });
    std::array<double, 3> move{ 0.0, 0.0, ring_z(a, v) };
    for (auto& idx : ring_groups(a, v)) out.push_back(s3_mesh(a, v, idx, move, 255));
    if (v == 1) out.push_back(s3_door(move, 255));
    return out;
}
// numpy.linalg.lstsq of [x z 1] -> uv: Householder QR (the floor's points lie exactly on a plane in uv)
std::array<std::array<double, 2>, 3> lstsq3(std::vector<std::array<double, 3>> Am, std::vector<std::array<double, 2>> Bm) {
    size_t n = Am.size();
    for (int j = 0; j < 3; j++) {
        double norm = 0; for (size_t i = j; i < n; i++) norm += Am[i][j] * Am[i][j];
        norm = std::sqrt(norm);
        double alpha = Am[j][j] > 0 ? -norm : norm;
        std::vector<double> vv(n, 0.0);
        for (size_t i = j; i < n; i++) vv[i] = Am[i][j];
        vv[j] -= alpha;
        double vn = 0; for (size_t i = j; i < n; i++) vn += vv[i] * vv[i];
        if (vn == 0) continue;
        for (int c = j; c < 3; c++) { double s = 0; for (size_t i = j; i < n; i++) s += vv[i] * Am[i][c]; s = 2 * s / vn; for (size_t i = j; i < n; i++) Am[i][c] -= s * vv[i]; }
        for (int c = 0; c < 2; c++) { double s = 0; for (size_t i = j; i < n; i++) s += vv[i] * Bm[i][c]; s = 2 * s / vn; for (size_t i = j; i < n; i++) Bm[i][c] -= s * vv[i]; }
    }
    std::array<std::array<double, 2>, 3> x{};
    for (int c = 0; c < 2; c++)
        for (int j = 2; j >= 0; j--) {
            double s = Bm[j][c];
            for (int k = j + 1; k < 3; k++) s -= Am[j][k] * x[k][c];
            x[j][c] = s / Am[j][j];
        }
    return x;
}
// close_parts: the close scene's pieces in Revenge's order, the floor carried on toward the camera
std::vector<std::pair<Bytes, Pic>> s3_close_parts(A& a, int v) {
    const ojson& ms = s3_models(a, v);
    std::array<double, 3> move{ 0.0, 0.0, 0.0 };              // (unused: close_pt)
    std::vector<std::pair<Bytes, Pic>> out;
    for (size_t k = 0; k < ms.size(); k++) {
        if (k == 0) {
            const ojson& f = ms[0];
            double near = f["verts"][0][2].get<double>();
            for (auto& p : f["verts"]) near = std::min(near, p[2].get<double>());
            double zn = (CLOSE_NEAR - CLOSE_TZ) / CLOSE_C;
            std::vector<std::array<double, 3>> Am; std::vector<std::array<double, 2>> Bm;
            for (size_t i = 0; i < f["verts"].size(); i++) {
                Am.push_back({ f["verts"][i][0].get<double>(), f["verts"][i][2].get<double>(), 1.0 });
                Bm.push_back({ f["uv"][i][0].get<double>(), f["uv"][i][1].get<double>() });
            }
            auto fit = lstsq3(Am, Bm);
            std::vector<size_t> nr;
            for (size_t i = 0; i < f["verts"].size(); i++) if (f["verts"][i][2].get<double>() == near) nr.push_back(i);
            if (nr.size() != 2) fail("stage3d: a floor with a straight near edge");
            std::vector<std::array<double, 3>> pts; std::vector<std::array<double, 2>> uvs;
            for (size_t i : nr) {
                double x = f["verts"][i][0].get<double>();
                uvs.push_back({ x * fit[0][0] + zn * fit[1][0] + 1.0 * fit[2][0], x * fit[0][1] + zn * fit[1][1] + 1.0 * fit[2][1] });
                pts.push_back(close_pt(x, 0, zn));
            }
            for (size_t i : nr) pts.push_back(close_pt(f["verts"][i][0].get<double>(), f["verts"][i][1].get<double>(), f["verts"][i][2].get<double>()));
            for (size_t i : nr) uvs.push_back({ f["uv"][i][0].get<double>(), f["uv"][i][1].get<double>() });
            // mesh_quad
            int w = f["tex_size"][0].get<int>(), h = f["tex_size"][1].get<int>();
            double per_u = 2.0 * w, per_v = 2.0 * h;
            std::vector<double> tu, tv;
            for (auto& c : uvs) { tu.push_back(c[0] * w); tv.push_back(c[1] * h); }
            double bu = std::floor(*std::min_element(tu.begin(), tu.end()) / per_u + 1e-6) * per_u;
            double bv = std::floor(*std::min_element(tv.begin(), tv.end()) / per_v + 1e-6) * per_v;
            Mesh M;
            for (size_t i = 0; i < 4; i++) M.uv.push_back({ (int64_t)py_round_half_even(tu[i] - bu), (int64_t)py_round_half_even(tv[i] - bv) });
            on_grid(pts);
            M.pts = pts;
            M.rgb.assign(4, { (uint8_t)CLOSE_GREY, (uint8_t)CLOSE_GREY, (uint8_t)CLOSE_GREY });
            for (auto t : std::vector<std::array<int, 3>>{ { 0, 2, 1 }, { 1, 2, 3 } }) {     // up_facing
                auto &pa = M.pts[t[0]], &pb = M.pts[t[1]], &pc = M.pts[t[2]];
                double cr = (pb[0] - pa[0]) * (pc[2] - pa[2]) - (pb[2] - pa[2]) * (pc[0] - pa[0]);
                M.tris.push_back(cr < 0 ? t : std::array<int, 3>{ t[0], t[2], t[1] });
            }
            out.push_back({ M.file(), s3_picture(a, v, f) });
        }
        out.push_back(s3_mesh(a, v, { (int)k }, move, CLOSE_GREY, true));
    }
    if (v == 1) out.push_back(s3_door(move, CLOSE_GREY, true));
    return out;
}
std::pair<uint32_t, uint32_t> fold(const Bytes& tex) {          // tron.c's picture fold (arenas_stage3d.fold)
    size_t n = std::min<size_t>(2048, tex.size() - 8);
    Bytes b(tex.begin() + 8, tex.end()); if (b.size() < 2048) b.resize(2048, 0); b.resize(n);
    uint32_t s = 0, x = 0;
    for (size_t i = 0; i + 4 <= b.size(); i += 4) { uint32_t w = be32(b, i); s += w; x = ((x << 1) | (x >> 31)) ^ w; }
    return { s, x };
}

// ==== arenas_ring.py: everything round the ring (Revenge's, the same in all its arenas) =====================================
Mesh EMPTY() {
    Mesh e; e.pts.assign(3, { 0, 10, 0 }); e.uv.assign(3, { 0, 0 }); e.rgb.assign(3, { 255, 255, 255 }); e.tris = { { 0, 1, 2 } };
    return e;
}
std::pair<Bytes, Bytes> white_pic() {
    Bytes t = { 15, 15, 0, 16, 0, 0, 4, 4 }; t.resize(8 + 128, 0x11);
    Bytes p(32, 0); put_be16(&p[2], 0xFFFF);
    return { t, p };
}
std::pair<Bytes, Bytes> signs_pic(A& a) {
    const Bytes &x = a.rf(0x30E), &y = a.rf(0x310);
    if (a.rf(0x30D) != a.rf(0x30F) || x[0] != 63 || x[1] != 31 || y[0] != 63 || y[1] != 31) fail("the barricade's signs");
    Bytes t = { 63, 63 };
    t.insert(t.end(), x.begin() + 2, x.begin() + 6);
    t.push_back(6); t.push_back(6);
    t.insert(t.end(), x.begin() + 8, x.begin() + 8 + 1024);
    t.insert(t.end(), y.begin() + 8, y.begin() + 8 + 1024);
    return { t, a.rf(0x30D) };
}
Mesh rev(A& a, const std::vector<int>& fs, int r = 0) {
    std::vector<Mesh> ms;
    for (int f : fs) ms.push_back(Mesh::from_file(a.rf(f)).rot(r).lit());
    return join(ms);
}
// a picture in the ring's lists: a Revenge palette, "white", "signs" or ("pic", ...)
struct RPic { int kind = 0; int rev = 0; Bytes tex, pal; };     // kind 0 Revenge, 1 white, 2 signs, 3 own
RPic rp(int p) { RPic x; x.rev = p; return x; }
RPic rp_white() { RPic x; x.kind = 1; return x; }
RPic rp_signs() { RPic x; x.kind = 2; return x; }

std::map<int, int> TIER() {
    std::map<int, int> T = { { 0x346, 1 }, { 0x347, 1 }, { 0x342, 2 }, { 0x343, 2 }, { 0x344, 2 }, { 0x345, 2 }, { 0x33F, 3 }, { 0x340, 3 },
                             { 0x341, 3 }, { 0x321, 3 }, { 0x322, 3 } };
    for (int f : { 0x337, 0x338, 0x339, 0x33A, 0x33B, 0x33C, 0x33D, 0x33E, 0x31F, 0x320, 0x323, 0x324 }) T[f] = 4;
    for (int f : { 0x305, 0x306 }) T[f] = 5;
    return T;
}
std::vector<int> L16(A& a, uint32_t at, int n) {
    const Bytes& ram = a.C.image("rev_arena");
    std::vector<int> out;
    for (int i = 0; i < n; i++) out.push_back(r16(ram, at + 2 * i));
    return out;
}
struct CrowdItem { Mesh m; int a, b; };
const char* GROUP_NAMES[5] = { "stands", "g1", "g2", "g3", "g4" };
const int GROUP_N[5] = { 15, 3, 9, 22, 22 };
const bool GROUP_TWO[5] = { false, true, true, true, true };
void crowd_lists_rev(A& a, std::vector<CrowdItem> (&lists)[5]) {
    auto Tier = TIER();
    auto SIDE_M = L16(a, 0x80036768, 19), SIDE_A = L16(a, 0x80036790, 19), SIDE_B = L16(a, 0x800367B8, 19);
    auto FRONT_M = L16(a, 0x80036750, 3), FRONT_A = L16(a, 0x80036758, 3), FRONT_B = L16(a, 0x80036760, 3);
    const std::vector<int> STAGE_CROWD = { 0x321, 0x322, 0x31F, 0x320, 0x323, 0x324, 0x347, 0x344, 0x345, 0x341, 0x33B, 0x33E };
    struct G { int tier, side; std::vector<int> fs; int a, b; };
    std::vector<G> out;
    auto add = [&](int side, const std::vector<int>& fs, const std::vector<int>& pa, const std::vector<int>& pb) {
        for (size_t i = 0; i < fs.size() && i < pa.size() && i < pb.size(); i++) {
            int f = fs[i];
            if (f == 0x2F4 || f == 0x312) continue;
            G* g = nullptr;
            for (auto& x : out) if (x.tier == Tier.at(f) && x.side == side && x.a == pa[i] && x.b == pb[i]) { g = &x; break; }
            if (g) g->fs.push_back(f); else out.push_back({ Tier.at(f), side, { f }, pa[i], pb[i] });
        }
    };
    RevTable st = a.table(0);
    std::map<int, int> stA, stB;
    for (size_t i = 0; i < st.meshes.size(); i++) stA[st.meshes[i]] = st.pals[i];
    const Bytes& ram = a.C.image("rev_arena");
    auto B = L16(a, be32(ram, 0x36CE4), (int)st.meshes.size());
    for (size_t i = 0; i < st.meshes.size(); i++) stB[st.meshes[i]] = B[i];
    std::vector<int> sa, sb;
    for (int f : STAGE_CROWD) { sa.push_back(stA.at(f)); sb.push_back(stB.at(f)); }
    add(0, STAGE_CROWD, sa, sb);
    for (int s : { 1, 2, 3 }) { add(s, SIDE_M, SIDE_A, SIDE_B); add(s, FRONT_M, FRONT_A, FRONT_B); }
    std::stable_sort(out.begin(), out.end(), [](const G& x, const G& y) { return x.tier != y.tier ? x.tier < y.tier : x.side < y.side; });
    for (auto& g : out) {
        int grp;
        if (g.tier == 1 && g.side != 0) grp = 0;
        else if (g.tier == 2 && g.side != 0) grp = 1;
        else if (g.tier <= 3) grp = 2;
        else grp = g.tier == 4 ? 3 : 4;
        if (grp == 0 && g.a != g.b) fail("the stands have one palette list");
        lists[grp].push_back({ rev(a, g.fs, g.side), g.a, g.b });
    }
    // crowd_ext: the stage-end corner moved back, both sides
    int back = (int)(ring_z(a, 0) - 1220);
    struct E { int tier, pa, pb; std::vector<Mesh> ms; };
    std::vector<E> groups;
    for (int f : { 0x347, 0x344, 0x345, 0x341, 0x33B, 0x33E }) {
        Mesh m = Mesh::from_file(a.rf(f)).lit();
        for (auto& p : m.pts) p[2] += back;
        Mesh mx = m;
        for (auto& p : mx.pts) p[0] = -p[0];
        for (auto& t : mx.tris) std::swap(t[1], t[2]);
        E* e = nullptr;
        for (auto& x : groups) if (x.tier == Tier.at(f) && x.pa == stA.at(f) && x.pb == stB.at(f)) { e = &x; break; }
        if (!e) { groups.push_back({ Tier.at(f), stA.at(f), stB.at(f), {} }); e = &groups.back(); }
        e->ms.push_back(m); e->ms.push_back(mx);
    }
    for (auto& e : groups) lists[e.tier <= 3 ? 2 : 3].push_back({ join(e.ms), e.pa, e.pb });
    for (int g = 0; g < 5; g++) if ((int)lists[g].size() > GROUP_N[g]) fail("crowd list %s too long", GROUP_NAMES[g]);
}
std::vector<std::pair<Mesh, RPic>> barricades_rev(A& a) {
    std::vector<std::pair<Mesh, RPic>> out;
    for (int side = 0; side < 4; side++) {
        Mesh lower = Mesh::from_file(a.rf(0x313));
        for (auto& t : lower.uv) t[1] += 32;
        std::vector<Mesh> sg = { Mesh::from_file(a.rf(0x311)), lower }, bars;
        if (side == 0) for (int f : { 0x319, 0x31A, 0x31B, 0x31C, 0x31D, 0x31E }) bars.push_back(Mesh::from_file(a.rf(f)));
        else {
            sg.push_back(Mesh::from_file(a.rf(0x303)));
            for (int f : { 0x312, 0x302, 0x304 }) bars.push_back(Mesh::from_file(a.rf(f)));
        }
        for (auto& m : sg) m = m.rot(side).lit();
        for (auto& m : bars) m = m.rot(side).lit();
        out.push_back({ join(sg), rp_signs() });
        out.push_back({ join(bars), rp(0x30B) });
    }
    return out;
}
double q10(double c) { return py_round_half_even(c / 10.0) * 10; }
constexpr double CAGE = 250.0 / 280.0;
std::vector<Mesh> floor_rev(A& a, bool cage) {
    Mesh m = Mesh::from_file(a.rf(0x2F5)).lit();
    if (cage) for (auto& p : m.pts) if (std::fabs(p[2]) < 400) { p[0] = q10(p[0] * CAGE); p[2] = q10(p[2] * CAGE); }
    return { m.rot(0) + m.rot(2), m.rot(1) + m.rot(3) };
}
std::pair<Mesh, std::vector<Mesh>> ring_rev(A& a, bool cage) {
    auto f = [&](Mesh m) { if (cage) for (auto& p : m.pts) { p[0] = q10(p[0] * CAGE); p[2] = q10(p[2] * CAGE); } return m; };
    std::vector<Mesh> ap;
    for (int x : { 0x2F8, 0x2F9, 0x2FA, 0x2FB }) ap.push_back(f(Mesh::from_file(a.rf(x)).lit()));
    Mesh mat = f(Mesh::from_file(a.rf(0x2F6)).lit()), logo = f(Mesh::from_file(a.rf(0x34F)).lit());
    for (auto& p : logo.pts) if (p[1] != 100) fail("Revenge's logo where it was");
    for (auto& p : mat.pts) if (p[1] != 90) fail("Revenge's mat where it was");
    for (auto& p : logo.pts) p[1] = 90;
    ap.push_back(mat); ap.push_back(EMPTY()); ap.push_back(EMPTY()); ap.push_back(EMPTY());
    return { logo, ap };
}
std::vector<Mesh> corner_rev(A& a, bool cage) {
    std::vector<Mesh> out;
    for (int x : { 0x2ED, 0x2F0, 0x2F3 }) {
        Mesh m = Mesh::from_file(a.rf(x)).lit();
        if (cage) for (auto& p : m.pts) { p[0] -= 28; p[2] += 28; }
        out.push_back(m);
    }
    return out;
}
// arenas_ring.stage: arenas.stage(v)'s pieces less the rails, BRIGHT
std::vector<std::pair<Bytes, Pic>> ring_stage(A& a, int v) {
    std::vector<std::pair<Bytes, Pic>> out;
    for (auto& [mb, p] : s3_ring_parts(a, v)) {
        if (!p.own && p.rev == 0x30B) continue;
        Bytes b = mb;
        int nv = b[1] & 0x7F;
        for (int i = 0; i < nv; i++) { uint8_t* c = &b[8 + 8 * i + 5]; if (c[0] == 0x49 && c[1] == 0x49 && c[2] == 0x49) c[0] = c[1] = c[2] = 0xFF; }
        out.push_back({ b, p });
    }
    return out;
}

// ==== tools\romrender\rr_arena.py: the ARENA page photo and the VS backdrop ===============================================
typedef rr::i64 i64;
i64 fdiv(i64 a, i64 b) { i64 q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
i64 rdiv(i64 a, i64 b) { return fdiv(2 * a + b, 2 * b); }
std::array<i64, 3> norm16(const std::array<i64, 3>& v) {
    i64 n = rr::isqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    std::array<i64, 3> o;
    for (int k = 0; k < 3; k++) o[k] = v[k] >= 0 ? fdiv(v[k] * 65536, n) : -fdiv(-v[k] * 65536, n);
    return o;
}
std::array<i64, 3> cross(const std::array<i64, 3>& a, const std::array<i64, 3>& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
rr::Camera look_at(std::array<i64, 3> eye, std::array<i64, 3> at, i64 focal16, i64 cx16, i64 cy16, i64 near) {
    auto f = norm16({ at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] });
    auto r = norm16(cross(f, { 0, 1, 0 }));
    auto u = cross(r, f);
    for (auto& c : u) c = c >= 0 ? c >> 16 : -((-c) >> 16);
    rr::Camera C;
    for (int k = 0; k < 3; k++) { C.eye[k] = eye[k]; C.right[k] = r[k]; C.up[k] = u[k]; C.fwd[k] = f[k]; }
    C.focal16 = focal16; C.cx16 = cx16; C.cy16 = cy16; C.near8 = near * 256;
    return C;
}
i64 focal_for(i64 h, i64 fov) {
    i64 a = fdiv(rdiv(fov * 65536, 360), 2);
    return fdiv(h * 8 * rr::isin(a + 0x4000), rr::isin(a));
}
int wrap_mode(int flag) { return (flag & 2) ? 2 : (flag & 1) ? 1 : 0; }
rr::Texture texture_from_tex(const Bytes& tex, const Bytes& pal) {
    rr::Texture t; t.w = tex[0] + 1; t.h = tex[1] + 1;
    size_t n = pal.size() / 2;
    for (i64 i = 0; i < t.w * t.h; i++) {
        size_t k = tex[2] == 1 ? tex[8 + i] : ((i & 1) == 0 ? tex[8 + (i >> 1)] >> 4 : tex[8 + (i >> 1)] & 15);
        t.texels.push_back(k < n ? be16(pal, 2 * k) : 0);
    }
    t.ws = wrap_mode(tex[4]); t.wt = wrap_mode(tex[5]);
    return t;
}
struct RrPiece { Bytes mesh, tex, pal; int turns; };
const i64 EYE[3] = { 680, 380, -820 }, AT[3] = { -200, 110, 420 };
// photo(): the arena drawn from one camera (rr_arena.scene + camera + add_sky), downsampled -> W x H RGBA
Bytes rr_photo(const std::vector<RrPiece>& pieces, int W, int H, const std::vector<std::pair<Bytes, Bytes>>* sky) {
    std::vector<rr::Texture> texs; std::vector<rr::Tri> tris;
    std::map<std::pair<Bytes, Bytes>, i64> tid;
    auto s8 = [](uint8_t b) { return b > 127 ? (int)b - 256 : (int)b; };
    for (auto& pc : pieces) {
        const Bytes& mb = pc.mesh;
        if (mb.size() < 8 || pc.tex.empty() || pc.pal.empty()) continue;
        auto key = std::make_pair(pc.tex, pc.pal);
        auto it = tid.find(key);
        if (it == tid.end()) { it = tid.emplace(key, (i64)texs.size()).first; texs.push_back(texture_from_tex(pc.tex, pc.pal)); }
        bool whole = mb[1] & 0x80, alt = mb[0] & 0x80;
        int sc = (mb[0] & 0x7F) + 1, nv = mb[1] & 0x7F, nt = mb[2];
        int off[3] = { s8(mb[4]) * 16, s8(mb[5]) * 16, s8(mb[6]) * 16 };
        int hu = mb[7] & 0xF0, hv = (mb[7] << 4) & 0xF0;
        std::vector<rr::Vtx> vs;
        for (int i = 0; i < nv; i++) {
            size_t o = 8 + 8 * (size_t)i;
            i64 x = (s8(mb[o]) + off[0]) * sc, y = (s8(mb[o + 1]) + off[1]) * sc, z = (s8(mb[o + 2]) + off[2]) * sc, s, t;
            if (alt) { s = ((i64)((mb[o + 4] << 8) | mb[o + 5]) << 5) + 8; t = ((i64)((mb[o + 6] << 8) | mb[o + 7]) << 5) + 8; }
            else { s = ((i64)(mb[o + 3] + hu) << 5) + 8; t = ((i64)(mb[o + 4] + hv) << 5) + 8; }
            for (int q = 0; q < (pc.turns & 3); q++) { i64 xx = x; x = z; z = -xx; }
            i64 c[3];
            if (whole) c[0] = c[1] = c[2] = 255;
            else if (alt) c[0] = c[1] = c[2] = 0xC8;
            else { c[0] = mb[o + 5]; c[1] = mb[o + 6]; c[2] = mb[o + 7]; }
            rr::Vtx v = { x, y, z, s, t, fdiv(c[0] * 256 * 17, 255 * 10), fdiv(c[1] * 256 * 17, 255 * 10), fdiv(c[2] * 256 * 17, 255 * 10) };
            vs.push_back(v);
        }
        size_t o = 8 + 8 * (size_t)nv;
        for (int i = 0; i < nt; i++) {
            int ta = mb[o + 3 * i] & 0x7F, tb = mb[o + 3 * i + 1] & 0x7F, tc = mb[o + 3 * i + 2] & 0x7F;
            if (ta < nv && tb < nv && tc < nv) { rr::Tri T; T.tex = it->second; T.flags = 1; T.v[0] = vs[ta]; T.v[1] = vs[tb]; T.v[2] = vs[tc]; tris.push_back(T); }
        }
    }
    if (sky) {                                                     // add_sky
        const i64 SKY_DIST = 5000, SKY_W = 2400, SKY_Y0 = -400, SKY_Y1 = 2600;
        i64 hx_ = AT[0] - EYE[0], hz = AT[2] - EYE[2];
        i64 L = rr::isqrt(hx_ * hx_ + hz * hz);
        i64 cx = EYE[0] + fdiv(hx_ * SKY_DIST, L), cz = EYE[2] + fdiv(hz * SKY_DIST, L);
        i64 rx = -hz, rz = hx_;
        i64 n = (i64)sky->size();
        for (i64 k = 0; k < n; k++) {
            rr::Texture t = texture_from_tex((*sky)[k].first, (*sky)[k].second);
            i64 ti = (i64)texs.size(); texs.push_back(t);
            i64 a_ = fdiv((2 * k - n) * SKY_W, 2), b_ = fdiv((2 * k + 2 - n) * SKY_W, 2);
            i64 xa = cx + fdiv(rx * a_, L), za = cz + fdiv(rz * a_, L), xb = cx + fdiv(rx * b_, L), zb = cz + fdiv(rz * b_, L);
            i64 s1 = t.w * 32 - 1, t1 = t.h * 32 - 1;
            rr::Vtx v0 = { xa, SKY_Y1, za, 0, 0, 256, 256, 256 }, v1 = { xb, SKY_Y1, zb, s1, 0, 256, 256, 256 },
                    v2 = { xb, SKY_Y0, zb, s1, t1, 256, 256, 256 }, v3 = { xa, SKY_Y0, za, 0, t1, 256, 256, 256 };
            rr::Tri T1; T1.tex = ti; T1.flags = 0; T1.v[0] = v0; T1.v[1] = v1; T1.v[2] = v2; tris.push_back(T1);
            rr::Tri T2; T2.tex = ti; T2.flags = 0; T2.v[0] = v0; T2.v[1] = v2; T2.v[2] = v3; tris.push_back(T2);
        }
    }
    rr::Camera cam = look_at({ EYE[0], EYE[1], EYE[2] }, { AT[0], AT[1], AT[2] }, focal_for(H, 48), (i64)W * 8, (i64)H * 8, 8);
    const i64 bg[4] = { 0, 0, 0, 255 };
    return rr::render(texs, tris, cam, bg, W, H, 3);
}
std::array<i64, 3> c555(i64 r, i64 g, i64 b) { return { rdiv(r * 31, 255), rdiv(g * 31, 255), rdiv(b * 31, 255) }; }
uint16_t p5551(const rr::C3& p) { return (uint16_t)((p[0] << 11) | (p[1] << 6) | (p[2] << 1) | 1); }
uint8_t c8(int c5) { return (uint8_t)((c5 << 3) | (c5 >> 2)); }
// page(): the photo (100 x 74) in a white frame, the logo in a framed box -> 128 x 96 RGBA
Bytes rr_page(const Bytes& photo, const Bytes& logo_tex, const Bytes& logo_pal) {
    const int W = 128;
    Bytes img(128 * 96 * 4, 0);
    auto fill = [&](int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b, uint8_t a_) {
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { uint8_t* p = &img[4 * (y * W + x)]; p[0] = r; p[1] = g; p[2] = b; p[3] = a_; }
    };
    fill(0, 0, 102, 76, 255, 255, 255, 255);
    for (int y = 0; y < 74; y++) for (int x = 0; x < 100; x++) memcpy(&img[4 * ((y + 1) * W + x + 1)], &photo[4 * (y * 100 + x)], 4);
    fill(70, 44, 126, 96, 255, 255, 255, 255); fill(71, 45, 125, 95, 0, 0, 0, 255);
    rr::Texture lt = texture_from_tex(logo_tex, logo_pal);
    Bytes lpx;
    for (auto c : lt.texels) { lpx.push_back(c8((c >> 11) & 31)); lpx.push_back(c8((c >> 6) & 31)); lpx.push_back(c8((c >> 1) & 31)); lpx.push_back((c & 1) ? 255 : 0); }
    i64 lw = lt.w, lh = lt.h;
    i64 h = lw == lh ? 48 : std::max<i64>(1, rdiv(52 * lh, lw));
    i64 y0 = lw == lh ? 46 : 46 + fdiv(48 - h, 2);
    Bytes sm = rr::resample(lpx, lw, lh, 52, h);
    for (i64 y = 0; y < h; y++)
        for (int x = 0; x < 52; x++) {
            uint8_t* d = &img[4 * ((y0 + y) * W + 72 + x)];
            const uint8_t* s = &sm[4 * (y * 52 + x)];
            i64 a_ = s[3];
            if (a_ == 0) continue;
            if (a_ == 255) { memcpy(d, s, 4); continue; }
            i64 oa = a_ + rdiv(d[3] * (255 - a_), 255);
            if (oa == 0) { d[0] = d[1] = d[2] = d[3] = 0; continue; }
            uint8_t o[4];
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)rdiv(s[k] * a_ * 255 + d[k] * d[3] * (255 - a_), oa * 255);
            o[3] = (uint8_t)oa;
            memcpy(d, o, 4);
        }
    return img;
}
std::vector<Bytes> rr_page_tiles(const Bytes& img, const Bytes& head) {
    std::vector<Bytes> out;
    for (int k = 0; k < 6; k++) {
        int x0 = 64 * (k / 3), y0 = 32 * (k % 3);
        std::vector<size_t> op; std::vector<rr::C3> cols;
        std::vector<const uint8_t*> tile;
        for (int y = 0; y < 32; y++) for (int x = 0; x < 64; x++) tile.push_back(&img[4 * ((y0 + y) * 128 + x0 + x)]);
        for (size_t i = 0; i < tile.size(); i++) if (tile[i][3] >= 128) { op.push_back(i); auto c = c555(tile[i][0], tile[i][1], tile[i][2]); cols.push_back({ c[0], c[1], c[2] }); }
        std::vector<rr::C3> pal; std::vector<i64> idx;
        rr::quantise(cols, 255, pal, idx);
        Bytes ci(tile.size(), 0);
        for (size_t j = 0; j < op.size(); j++) ci[op[j]] = (uint8_t)(idx[j] + 1);
        Bytes f(head.begin(), head.begin() + 16);
        std::vector<uint16_t> tl = { 0 };
        for (auto& p : pal) tl.push_back(p5551(p));
        tl.resize(256, 0);
        for (auto c : tl) { f.push_back((uint8_t)(c >> 8)); f.push_back((uint8_t)c); }
        f.insert(f.end(), ci.begin(), ci.end());
        out.push_back(f);
    }
    return out;
}
std::vector<Bytes> rr_backdrop_tiles(const Bytes& px) {
    std::vector<rr::C3> cols;
    for (size_t i = 0; i < px.size() / 4; i++) { auto c = c555(px[4 * i], px[4 * i + 1], px[4 * i + 2]); cols.push_back({ c[0], c[1], c[2] }); }
    std::vector<rr::C3> pal; std::vector<i64> idx;
    rr::quantise(cols, 16, pal, idx);
    Bytes tl;
    for (size_t i = 0; i < 16; i++) { uint16_t c = i < pal.size() ? p5551(pal[i]) : 0; tl.push_back((uint8_t)(c >> 8)); tl.push_back((uint8_t)c); }
    std::vector<Bytes> out;
    for (int t = 0; t < 40; t++) {
        int x0 = 64 * (t % 5), y0 = 30 * (t / 5);
        Bytes f = t == 0 ? tl : Bytes();
        for (int y = 0; y < 30; y++) for (int x = 0; x < 64; x += 2)
            f.push_back((uint8_t)((idx[(y0 + y) * 320 + x0 + x] << 4) | idx[(y0 + y) * 320 + x0 + x + 1]));
        out.push_back(f);
    }
    return out;
}
std::pair<Bytes, Bytes> rpic_bytes(A& a, const RPic& p) {
    if (p.kind == 3) return { p.tex, p.pal };
    if (p.kind == 1) return white_pic();
    if (p.kind == 2) return signs_pic(a);
    return { a.rf(p.rev + 1), a.rf(p.rev) };
}
// arenas.py rr_pieces(v) (arenas_ring.scene) for a Revenge arena
std::vector<RrPiece> rev_rr_pieces(A& a, int v) {
    RevTable t = a.table(v);
    std::vector<RrPiece> out;
    auto add = [&](const Mesh& m, const RPic& p) {
        auto mb = m.file_opt();
        if (!mb) return;
        auto tq = rpic_bytes(a, p);
        out.push_back({ *mb, tq.first, tq.second, 0 });
    };
    for (auto& [mb, p] : ring_stage(a, v)) {
        RPic q; if (p.own) { q.kind = 3; q.tex = p.tex; q.pal = p.pal; } else q.rev = p.rev;
        add(Mesh::from_file(mb), q);
    }
    std::vector<CrowdItem> cl[5]; crowd_lists_rev(a, cl);
    for (int g = 0; g < 5; g++) for (auto& c : cl[g]) add(c.m, rp(c.a));
    for (auto& m : floor_rev(a, false)) add(m, rp(t.aisle[0]));
    auto rg = ring_rev(a, false);
    for (int i = 0; i < 5; i++) add(rg.second[i], rp(i < 4 ? t.ring[2] : t.ring[0]));
    if (t.ring[1]) add(rg.first, rp(t.ring[1]));
    for (auto& [m, p] : barricades_rev(a)) add(m, p);
    auto cn = corner_rev(a, false);
    for (int r = 0; r < 4; r++) { add(cn[0].rot(r), rp(0x2EE)); add(cn[1].rot(r), rp(0x2F1)); add(cn[2].rot(r), rp_white()); }
    return out;
}
std::vector<Bytes> photo_tiles(A& a, const std::vector<RrPiece>& ps, const std::vector<std::pair<Bytes, Bytes>>* sky,
                               const Bytes& logo_tex, const Bytes& logo_pal, std::vector<Bytes>& backdrop) {
    Bytes small = rr_photo(ps, 100, 74, sky);
    auto tiles = rr_page_tiles(rr_page(small, logo_tex, logo_pal), a.C.nmf(0x4641));
    backdrop = rr_backdrop_tiles(rr_photo(ps, 320, 240, sky));
    return tiles;
}
Bytes apron_half(const Bytes& t, bool right) {
    int w = t[0] + 1, h = t[1] + 1;
    if (w != 128 || h != 32 || be16(t, 2) != 16) fail("a CI4 128 x 32 apron");
    Bytes out(t.begin(), t.begin() + 8);
    auto nib = [&](int i) { return (t[8 + (i >> 1)] >> ((i & 1) ? 0 : 4)) & 15; };
    for (int y = 0; y < h; y++) {
        std::vector<int> row;
        for (int x = 0; x < w; x++) row.push_back(nib(y * w + (right ? 64 : 0) + x / 2));
        for (int x = 0; x < w; x += 2) out.push_back((uint8_t)(row[x] << 4 | row[x + 1]));
    }
    return out;
}
uint32_t first_of(Pool& F, const std::vector<Bytes>& files) {
    uint32_t first = WMP_FILE0 + (uint32_t)F.data.size();
    for (auto& f : files) F.data.push_back(f);
    return first;
}

}  // namespace

// (continued: the rows, the ring lists' build, the close scenes, No Mercy's own ten, the tables, the walls)
#include "ri_wmarenas2.inc"

}  // namespace ri
