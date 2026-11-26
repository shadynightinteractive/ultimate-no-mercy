// ri_wmvenues.cpp (phase3 p3wmvenues, 2026-11-19): WrestleMania 2000's seven venues as arcade arenas -- the C++ twin of
// mod\gen\arenas_wm.py's data side (see ri_wmvenues.h). Every function below names the Python it mirrors; the arithmetic is
// the Python's (doubles where it has floats, its round() half to even, int() toward zero, // floor, min / max keeping the
// first of equals, sorted() stable), so the meshes and pictures are the same bytes.
//
// One thing is not computed here but frozen (the FREEZE rule): painter()'s drawing order of the stage set's and the close
// scene's pieces -- a heuristic ordering from coarse numpy depth renders -- read from tools\romimport\frozen\
// wm_painter_order.json (tools\romimport\native\wm_painter_freeze.py makes and checks it against the Python).
#include "ri_wmvenues.h"
#include "ri_wmmesh.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace ri {
namespace {

typedef std::array<double, 3> P3;
typedef std::array<double, 2> UV;
typedef std::array<int, 3> Tri;

// ---- arenas_wm.py's constants ---------------------------------------------------------------------------------------
constexpr int WM_LOOK = 0x80;                 // mesh byte 1 bit 7: No Mercy's combiner FC15FE2B (the picture as it is)
constexpr int SET_MAX = 64;                   // src/arena.c ARENA_SET_MAX
constexpr double ALT_GREY = 200;              // the alternate form's points: colour 200
constexpr int UV_MAX = 1023 - 64;             // texels a mesh can hold
constexpr int LIT = 255;
constexpr int TRUSS_TEX = 1;                  // the truss picture (its back faces left out: facing())
constexpr double SET_SCALE = 0.3;
constexpr double DECK_FRONT_SET = -1000.0;    // the set's deck front: the walkway cut here
constexpr double CLOSE_POSE_Z = -100.0;       // WM2000's close shots: the Superstars at z ~-100
// Changed for new features (Ultimate No Mercy): the stagescale agent, 2026-11-20 (arenas_wm.py NM_POSE_Z): WM2000's pose spot at
// No Mercy's (its Superstar poses at z -515 of the close scene, not the origin: the set stood 515 too far behind him); the
// grate deck no longer carried on toward the camera, its front skirt no longer left out (WrestleMania 2000's own as they are)
constexpr double NM_POSE_Z = -515.0;
const double CAGE = 250.0 / 280.0;            // No Mercy's cage ring: +-250
constexpr bool TABLE = true;                  // No Mercy's announce table kept: the -z side moved out (table_side)
const double TABLE_Z0 = -470.0, TABLE_Z1 = -620.0, TABLE_K = 70.0 / 60.0;
constexpr int TABLE_CROWD_SHIFT = -170;

// WM2000 RAW IS WAR's lists (the same in all seven venues: the rig's tables)
const int MAT[2] = { 0x5C2, 0x5C0 };
const int FLOOR[2][2] = { { 0x5C4, 0x647 }, { 0x5C3, 0x649 } };
const int BARR_A[3] = { 0x5BB, 0x5BA, 0x5BC }, BARR_B[3] = { 0x5BF, 0x5BD, 0x5BE }, BARR_PAL = 0x645;
const int FRONT_ROW_M[2] = { 0x58B, 0x58C }, FRONT_ROW_A = 0x58D, FRONT_ROW_B = 0x58F;
const int FRONT_ROW_TURNS[3] = { 270, 180, 90 };
const std::set<int> WALLS = { 0x64D, 0x64F }, TIERS = { 0x625, 0x627, 0x629, 0x62B };
const std::set<int> BACK_ROW = { 0x5DC, 0x5DD, 0x5DE, 0x5DF, 0x5F7, 0x5F8, 0x5F9, 0x5FA, 0x60A, 0x60B, 0x60C, 0x60D, 0x619, 0x61A, 0x61B, 0x61C };
const std::set<int> BARS = { 0x5D5, 0x5F2, 0x603, 0x614 };
const std::set<int> MID_ROW = { 0x5E0, 0x5E1, 0x5E2, 0x5E3, 0x5FB, 0x5FC, 0x5FD, 0x5FE, 0x60E, 0x60F, 0x610, 0x611, 0x61D, 0x61E, 0x61F, 0x620 };
const std::set<int> FRONT_SIGNS = { 0x5D8, 0x5D9, 0x5DA, 0x5DB, 0x5F3, 0x5F4, 0x5F5, 0x5F6, 0x606, 0x607, 0x608, 0x609, 0x615, 0x616, 0x617, 0x618 };
const P3 EYES[6] = { { 0, 300, -1100 }, { 800, 300, -800 }, { -800, 300, -800 }, { 0, 300, 0 }, { 500, 250, 300 }, { -500, 250, 300 } };
const std::set<int> CLOSE_SKIP = {};           // (stagescale 2026-11-20: none -- was { 29, 30 }, the deck's front skirt)
// GROUPS: (list, its places, two frames' pictures)
const char* const GROUP_NAMES[5] = { "stands", "g1", "g2", "g3", "g4" };

// VENUES: what differs per venue (match = (scale, y, z) of the 3D scene in the match; keep = WM2000's own match-stage
// pieces kept (mesh, picture, z cut or none); logo = the page photo's box picture; raw_set = scenes 0 / 1's RAW IS WAR set)
struct Keep { int mesh, pal, zcut; bool cut; };
struct VenueCfg { const char* name; double ms, dy, dz; Keep keep; int logo; bool raw_set; };
const VenueCfg VENUES[7] = {
    { "RAW IS WAR WM 2000",       1.0, 187.0, 2100.0, { 0x6FD, 0x655, 1830, true }, 0x65F, true },
    { "SUNDAY NIGHT HEAT",        1.0, 187.0, 2100.0, { 0x6FD, 0x655, 1830, true }, 0x669, true },
    { "ROYAL RUMBLE WM 2000",     1.0, 0.0, 1957.0, { 0x6CF, 0x66D, 0, false }, 0x67F, false },
    { "KING OF THE RING WM 2000", 1.0, 0.0, 1957.0, { 0x6C5, 0x66D, 0, false }, 0x673, false },
    { "SURVIVOR SERIES WM 2000",  1.0, 0.0, 1535.0, { 0x6E7, 0x6A9, 0, false }, 0x6AD, false },
    { "WRESTLEMANIA WM 2000",     0.8, 0.0, 1578.0, { 0x6F1, 0x6B9, 0, false }, 0x6C1, false },
    { "SUMMERSLAM WM 2000",       1.0, 0.0, 1700.0, { 0x6DC, 0x68D, 0, false }, 0x68B, false },
};

int s8(uint8_t b) { return b > 127 ? (int)b - 256 : (int)b; }
int64_t floordiv(int64_t a, int64_t b) { int64_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }

// ---- encode / fits (arenas_wm.encode: the plain form, else the alternate 16-bit-texel form) ------------------------------
std::optional<Bytes> encode_opt(const Mesh& m) {
    if (m.pts.empty() || m.pts.size() > 127 || m.tris.size() > 127) return std::nullopt;   // (Mesh.file's assert)
    if (auto b = m.file_opt()) return b;
    for (auto& t : m.uv) if (!(0 <= t[0] && t[0] < 1024 && 0 <= t[1] && t[1] < 1024)) return std::nullopt;
    const auto& pts = m.pts;
    int sc = 1, off[3] = {};
    std::vector<P3> q(pts.size());
    bool ok = false;
    for (sc = 1; sc < 129; sc++) {
        for (int k = 0; k < 3; k++) {
            double lo = pts[0][k], hi = pts[0][k];
            for (auto& p : pts) { if (p[k] < lo) lo = p[k]; if (p[k] > hi) hi = p[k]; }
            off[k] = (int)py_round_half_even((lo + hi) / 2 / sc / 16);
        }
        bool fit = true;
        for (int k = 0; k < 3; k++) if (off[k] < -128 || off[k] > 127) fit = false;
        for (size_t i = 0; i < pts.size() && fit; i++)
            for (int k = 0; k < 3; k++) {
                double c = pts[i][k] / sc - off[k] * 16;
                q[i][k] = c;
                if (!(c >= -128 && c <= 127 && c == std::trunc(c))) { fit = false; break; }
            }
        if (fit) { ok = true; break; }
    }
    if (!ok) return std::nullopt;                                                          // ("no scale fits")
    Bytes out = { (uint8_t)(0x80 | (sc - 1)), (uint8_t)(m.b1 | (int)pts.size()), (uint8_t)m.tris.size(), (uint8_t)m.b3,
                  (uint8_t)(off[0] & 0xFF), (uint8_t)(off[1] & 0xFF), (uint8_t)(off[2] & 0xFF), 0 };
    for (size_t i = 0; i < pts.size(); i++) {
        for (int k = 0; k < 3; k++) out.push_back((uint8_t)((int64_t)q[i][k] & 0xFF));
        out.push_back(0);
        out.push_back((uint8_t)(m.uv[i][0] >> 8)); out.push_back((uint8_t)m.uv[i][0]);
        out.push_back((uint8_t)(m.uv[i][1] >> 8)); out.push_back((uint8_t)m.uv[i][1]);
    }
    for (auto& t : m.tris) { out.push_back((uint8_t)t[0]); out.push_back((uint8_t)t[1]); out.push_back((uint8_t)t[2]); }
    if (out.size() & 1) out.push_back(0);
    return out;
}
Bytes encode(const Mesh& m) {
    auto b = encode_opt(m);
    if (!b) fail("arenas_wm: a mesh no No Mercy mesh file holds (%zu points, %zu triangles)", m.pts.size(), m.tris.size());
    return *b;
}
bool is_alt(const Mesh& m) { return (encode(m)[0] & 0x80) != 0; }

// merge_greedy: meshes with one key put together in order while the result still encodes
template <class K> std::vector<std::pair<K, Mesh>> merge_greedy(const std::vector<std::pair<K, Mesh>>& items) {
    std::vector<std::pair<K, Mesh>> out;
    for (auto& [key, m] : items) {
        bool put = false;
        for (auto& g : out) {
            if (!(g.first == key)) continue;
            Mesh j = g.second + m;                              // (the Python adds before fits(): one flags or it stops)
            if (encode_opt(j)) { g.second = std::move(j); put = true; break; }
        }
        if (!put) out.push_back({ key, m });
    }
    return out;
}

// empty(): EMPTY with WM_LOOK
Mesh empty_mesh() {
    Mesh m;
    m.pts = { P3{ 0, 10, 0 }, P3{ 0, 10, 0 }, P3{ 0, 10, 0 } };
    m.uv = { { 0, 0 }, { 0, 0 }, { 0, 0 } };
    m.rgb = { { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 } };
    m.tris = { Tri{ 0, 1, 2 } };
    m.b1 = WM_LOOK; m.b3 = 0;
    return m;
}

template <class F> Mesh map_pts(const Mesh& m, F f) { Mesh o = m; for (auto& p : o.pts) p = f(p); return o; }

// roty: turned about y by a multiple of 90 degrees ((x, z) -> (z, -x) at 90)
Mesh roty(const Mesh& m, int deg) { return m.rot((int)((floordiv(deg, 90) % 4 + 4) % 4)); }

// caged: the ring's pieces drawn at the cage ring's size (on the 10s)
Mesh caged(const Mesh& m) {
    return map_pts(m, [](const P3& p) {
        if (std::fabs(p[0]) < 400 && std::fabs(p[2]) < 400)
            return P3{ py_round_half_even(p[0] * CAGE / 10.0) * 10, p[1], py_round_half_even(p[2] * CAGE / 10.0) * 10 };
        return p;
    });
}

// table_side: the -z side moved out to No Mercy's table lines
Mesh table_side(const Mesh& m) {
    if (!TABLE) return m;
    return map_pts(m, [](const P3& q) {
        if (q[2] <= TABLE_Z0) return P3{ q[0], q[1], py_round_half_even(TABLE_Z1 + (q[2] - TABLE_Z0) * TABLE_K) };
        return q;
    });
}

// brighter: a picture's palette colours x 255 / 200 (an alternate-form mesh's 200 grey made 255 again)
WmPic brighter(const WmPic& pic) {
    const double k = 255.0 / ALT_GREY;
    WmPic o = pic;
    for (size_t i = 0; i + 1 < o.pal.size(); i += 2) {
        int c = be16(o.pal, i), ch[3], n = 0;
        for (int sh : { 11, 6, 1 }) ch[n++] = std::min(31, (int)py_round_half_even(((c >> sh) & 31) * k));
        put_be16(&o.pal[i], ch[0] << 11 | ch[1] << 6 | ch[2] << 1 | (c & 1));
    }
    return o;
}

std::vector<Bytes> entries(const Bytes& d) {
    uint32_t n = be32(d, 0);
    std::vector<size_t> offs;
    for (uint32_t i = 0; i < n; i++) offs.push_back(be32(d, 4 + 4 * i));
    offs.push_back(d.size());
    std::vector<Bytes> out;
    for (uint32_t i = 0; i < n; i++) out.emplace_back(d.begin() + offs[i], d.begin() + offs[i + 1]);
    return out;
}

// a scene model as models() gives it (the Python's dict)
struct Tex { int entry, pal; bool ci8; bool operator==(const Tex& o) const { return entry == o.entry && pal == o.pal && ci8 == o.ci8; } };
struct Model {
    int k = 0;
    std::vector<P3> verts; std::vector<UV> st; std::vector<int> alpha; std::vector<Tri> tris;
    Tex tex{}; int w = 0, h = 0, cms = 0, cmt = 0;
};

double depth_of(const std::vector<P3>& verts) {       // depth(): the mean z, the far first (-sum / n)
    double s = 0; for (auto& p : verts) s += p[2];
    return -s / (double)verts.size();
}

// prep_uv: texels moved by whole repeats to start at 0; a model spanning more than a mesh holds cut into clusters
double uv_base(const std::vector<double>& vals, int per) {
    double lo = vals[0]; for (double v : vals) if (v < lo) lo = v;
    return std::floor(lo / per + 1e-6) * per;
}
Model prep_uv(Model m) {
    int pu = m.w * ((m.cms & 1) ? 2 : 1), pv = m.h * ((m.cmt & 1) ? 2 : 1);
    std::vector<double> su, sv;
    for (auto& t : m.st) { su.push_back(t[0]); sv.push_back(t[1]); }
    double bu = uv_base(su, pu), bv = uv_base(sv, pv);
    std::vector<UV> st;
    double mu = -INFINITY, mv = -INFINITY;
    for (auto& t : m.st) { st.push_back({ t[0] - bu, t[1] - bv }); mu = std::max(mu, t[0] - bu); mv = std::max(mv, t[1] - bv); }
    if (mu <= UV_MAX && mv <= UV_MAX) { m.st = st; return m; }
    // triangles spanning more than a mesh holds halved along their longest edge in texels until they fit
    std::vector<P3> P = m.verts; std::vector<UV> Q = m.st; std::vector<int> Al = m.alpha;
    auto span = [&](const Tri& t) {
        double best = 0; bool first = true;
        for (int k = 0; k < 2; k++) {
            double hi = Q[t[0]][k], lo = Q[t[0]][k];
            for (int i : t) { hi = std::max(hi, Q[i][k]); lo = std::min(lo, Q[i][k]); }
            if (first || hi - lo > best) best = hi - lo;
            first = false;
        }
        return best;
    };
    std::vector<Tri> todo = m.tris, tris;
    while (!todo.empty()) {
        Tri t = todo.back(); todo.pop_back();
        if (span(t) <= UV_MAX - 64) { tris.push_back(t); continue; }
        static const int E[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
        int e = 0; double eb = 0;
        for (int c = 0; c < 3; c++) {                 // (max(): the first of the longest)
            double l = std::max(std::fabs(Q[t[E[c][0]]][0] - Q[t[E[c][1]]][0]), std::fabs(Q[t[E[c][0]]][1] - Q[t[E[c][1]]][1]));
            if (c == 0 || l > eb) { e = c; eb = l; }
        }
        int i = t[E[e][0]], j = t[E[e][1]], o = t[3 - E[e][0] - E[e][1]];
        int n = (int)P.size();
        P.push_back({ (P[i][0] + P[j][0]) / 2.0, (P[i][1] + P[j][1]) / 2.0, (P[i][2] + P[j][2]) / 2.0 });
        Q.push_back({ (Q[i][0] + Q[j][0]) / 2.0, (Q[i][1] + Q[j][1]) / 2.0 });
        Al.push_back((Al[i] + Al[j]) / 2);            // (alphas are >= 0: // is /)
        todo.push_back({ i, n, o }); todo.push_back({ n, j, o });
    }
    // the triangles in clusters, each moved by its own whole repeats and within UV_MAX
    struct Cl { std::vector<Tri> t; std::set<int> p; };
    std::vector<Cl> clusters;
    auto fits = [&](const std::set<int>& pts) {
        std::vector<double> a, b;
        for (int i : pts) { a.push_back(Q[i][0]); b.push_back(Q[i][1]); }
        return (*std::max_element(a.begin(), a.end()) - uv_base(a, pu) <= UV_MAX) && (*std::max_element(b.begin(), b.end()) - uv_base(b, pv) <= UV_MAX);
    };
    for (auto& t : tris) {
        bool put = false;
        for (auto& c : clusters) {
            std::set<int> u = c.p; u.insert(t.begin(), t.end());
            if (fits(u)) { c.t.push_back(t); c.p = std::move(u); put = true; break; }
        }
        if (!put) clusters.push_back({ { t }, std::set<int>(t.begin(), t.end()) });
    }
    Model r = m;
    r.verts.clear(); r.st.clear(); r.alpha.clear(); r.tris.clear();
    for (auto& c : clusters) {
        std::vector<double> a, b;
        for (int i : c.p) { a.push_back(Q[i][0]); b.push_back(Q[i][1]); }
        double cu = uv_base(a, pu), cv = uv_base(b, pv);
        std::map<int, int> at;
        for (int i : c.p) {                           // (sorted: a std::set)
            at[i] = (int)r.verts.size(); r.verts.push_back(P[i]); r.st.push_back({ Q[i][0] - cu, Q[i][1] - cv }); r.alpha.push_back(Al[i]);
        }
        for (auto& t : c.t) r.tris.push_back({ at[t[0]], at[t[1]], at[t[2]] });
    }
    return r;
}

// set_mesh's clip: (points, texels) -> (points, texels) per model
struct Clip { int kind = 0; double y_min = 0; };      // 0 none, 1 cut_walkway, 2 cut_skirt(y_min)

void cut_walkway(std::vector<P3>& V, std::vector<UV>& ST) {
    double zmin = V[0][2]; for (auto& p : V) zmin = std::min(zmin, p[2]);
    if (zmin >= DECK_FRONT_SET) return;
    std::vector<P3> ov = V; std::vector<UV> ot = ST;
    for (size_t i = 0; i < V.size(); i++) {
        const P3& p = V[i];
        if (p[2] >= DECK_FRONT_SET) continue;
        int j = -1;
        for (size_t k = 0; k < V.size(); k++)
            if (V[k][0] == p[0] && V[k][2] > p[2] && (j < 0 || V[k][2] < V[j][2])) j = (int)k;
        if (j < 0) fail("arenas_wm cut_walkway: a point with nothing behind it");
        double t = (DECK_FRONT_SET - p[2]) / (double)(V[j][2] - p[2]);
        ov[i] = { p[0], p[1] + t * (V[j][1] - p[1]), DECK_FRONT_SET };
        ot[i] = { ST[i][0] + t * (ST[j][0] - ST[i][0]), ST[i][1] + t * (ST[j][1] - ST[i][1]) };
    }
    V = ov; ST = ot;
}

// set_mesh: models (one picture) as one No Mercy mesh at scale x set + move, on the grid the points need
Mesh set_mesh(const std::vector<Model>& ms, const P3& move, Clip clip, double scale, int screen, bool look = false, int lit = LIT) {
    Mesh M;
    std::vector<UV> uv;
    for (auto& m : ms) {
        std::vector<P3> V = m.verts; std::vector<UV> ST = m.st;
        if (clip.kind == 1) cut_walkway(V, ST);
        else if (clip.kind == 2) for (auto& p : V) p[1] = (p[1] >= clip.y_min) ? p[1] : clip.y_min;   // (max(y, y_min): y on a tie)
        int n = (int)M.pts.size();
        for (auto& p : V) M.pts.push_back({ scale * p[0] + move[0], scale * p[1] + move[1], scale * p[2] + move[2] });
        for (auto& t : ST) uv.push_back(t);
        for (int a : m.alpha) { uint8_t c = (uint8_t)((m.k == screen ? 255 : a) * lit / 255); M.rgb.push_back({ c, c, c }); }
        for (auto& t : m.tris) M.tris.push_back({ t[0] + n, t[1] + n, t[2] + n });
    }
    for (auto& t : uv) {
        int64_t a = (int64_t)py_round_half_even(t[0]), b = (int64_t)py_round_half_even(t[1]);
        M.uv.push_back({ std::min<int64_t>(std::max<int64_t>(a, 0), UV_MAX), std::min<int64_t>(std::max<int64_t>(b, 0), UV_MAX) });
    }
    double span = 0, mid = 0;
    for (int k = 0; k < 3; k++) {
        double lo = M.pts[0][k], hi = M.pts[0][k];
        for (auto& p : M.pts) { lo = std::min(lo, p[k]); hi = std::max(hi, p[k]); }
        double s = hi - lo, md = std::fabs(hi + lo) / 2.0;
        if (k == 0 || s > span) span = s;
        if (k == 0 || md > mid) mid = md;
    }
    double sc = std::max({ 1.0, std::ceil(span / 230.0), std::ceil(mid / 1900.0) });
    for (auto& p : M.pts) for (int k = 0; k < 3; k++) p[k] = py_round_half_even(p[k] / sc) * sc;
    M.b1 = look ? WM_LOOK : 0; M.b3 = 0;
    return M;
}

// a cross product's sign against an eye: facing()'s side() / _front_sign()'s test, in the Python's order
double side_of(const P3& a, const P3& b, const P3& c, const P3& e) {
    double n0 = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
    double n1 = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
    double n2 = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    double s = 0; s += n0 * (e[0] - a[0]); s += n1 * (e[1] - a[1]); s += n2 * (e[2] - a[2]);
    return s;
}
P3 placed(const P3& q, double scale, const P3& move) { return { scale * q[0] + move[0], scale * q[1] + move[1], scale * q[2] + move[2] }; }

// a 3 x 3 least-squares fit (close_parts' np.linalg.lstsq of the deck's texels on [x, z, 1]) by Householder QR
std::array<UV, 3> lstsq3(const std::vector<std::array<double, 3>>& A0, const std::vector<UV>& B0) {
    size_t n = A0.size();
    if (n < 3) fail("arenas_wm: a deck of %zu points", n);
    std::vector<std::array<double, 3>> A = A0; std::vector<UV> B = B0;
    for (int c = 0; c < 3; c++) {
        double nr = 0; for (size_t i = c; i < n; i++) nr += A[i][c] * A[i][c];
        nr = std::sqrt(nr);
        if (nr == 0) fail("arenas_wm: a degenerate deck");
        double alpha = A[c][c] > 0 ? -nr : nr;
        std::vector<double> v(n, 0.0);
        for (size_t i = c; i < n; i++) v[i] = A[i][c];
        v[c] -= alpha;
        double vv = 0; for (size_t i = c; i < n; i++) vv += v[i] * v[i];
        if (vv == 0) continue;
        for (int j = c; j < 3; j++) {
            double d = 0; for (size_t i = c; i < n; i++) d += v[i] * A[i][j];
            d = 2 * d / vv; for (size_t i = c; i < n; i++) A[i][j] -= d * v[i];
        }
        for (int j = 0; j < 2; j++) {
            double d = 0; for (size_t i = c; i < n; i++) d += v[i] * B[i][j];
            d = 2 * d / vv; for (size_t i = c; i < n; i++) B[i][j] -= d * v[i];
        }
    }
    std::array<UV, 3> X{};
    for (int j = 0; j < 2; j++)
        for (int c = 2; c >= 0; c--) {
            double s = B[c][j];
            for (int k = c + 1; k < 3; k++) s -= A[c][k] * X[k][j];
            X[c][j] = s / A[c][c];
        }
    return X;
}

// ---- one venue: venue() + models() + stage_parts() + close_parts() + ring_lists() ------------------------------------
struct Venue {
    int v; const VenueCfg* c; const RomFiles& wm; const ojson& sc;
    std::vector<std::pair<int, int>> aprons;
    struct Group { std::vector<int> ms, pa, pb; };
    std::vector<Group> crowd;
    double sc_scale = 0, oz = 0, match_scale = 0;
    P3 match_move{}, close_move{};
    int screen = -1;                      // screen_model (-1: None)
    std::set<int> mirror, close_skip;
    std::vector<Model> models;
    int front_sign = 1;
    std::vector<Bytes> tex_entries;

    Venue(int v_, const RomFiles& wm_, const ojson& scenes, const ojson& venues)
        : v(v_), c(&VENUES[v_]), wm(wm_), sc(scenes.at("scenes").at(v_)) {
        const ojson& T = venues.at(std::to_string(v));
        auto h = [](const ojson& xs) { std::vector<int> o; for (auto& x : xs) o.push_back(x.get<int>()); return o; };
        auto am = h(T.at("apron").at("mesh")), ap = h(T.at("apron").at("pal"));
        for (size_t i = 0; i < std::min(am.size(), ap.size()); i++) if (ap[i]) aprons.push_back({ am[i], ap[i] });
        int g = 0;
        for (auto& G : T.at("crowd")) {
            auto ms = h(G.at("mesh")), pa = h(G.at("palA")), pb = h(G.at("palB"));
            Group k;
            for (size_t i = 0; i < ms.size(); i++)
                if (pa[i] && (g != 2 || (ms[i] >= 0x603 && ms[i] < 0x612))) { k.ms.push_back(ms[i]); k.pa.push_back(pa[i]); k.pb.push_back(pb[i]); }
            crowd.push_back(k); g++;
        }
        std::string raw = sc.at("raw52").get<std::string>();
        auto hx = [&](size_t at) { return (int)std::stoul(raw.substr(2 * at, 2), nullptr, 16); };
        int s32 = hx(32) << 8 | hx(33), s34 = hx(34) << 8 | hx(35);
        sc_scale = SET_SCALE * (s32 / 2048.0); oz = SET_SCALE * s34;
        match_scale = sc_scale * c->ms; match_move = { 0.0, c->dy, c->ms * oz + c->dz };
        close_move = { 0.0, 0.0, oz - CLOSE_POSE_Z + NM_POSE_Z };
        for (auto& n : sc.at("nodes")) if (n.at("flags").get<int>() & 0x100) { screen = n.at("first").get<int>(); break; }
        for (auto& n : sc.at("nodes"))
            if (n.at("flags").get<int>() & 0x2000)
                for (int k = n.at("first").get<int>(); k < n.at("first").get<int>() + n.at("count").get<int>(); k++) mirror.insert(k);
        if (c->raw_set) close_skip = CLOSE_SKIP;
        tex_entries = entries(wm(sc.at("tex_file").get<uint32_t>()));
        make_models();
    }

    const Bytes& wfile(int i) const { return wm((uint32_t)i); }
    WmPic pic_of(int p) const { return { wfile(p + 1), wfile(p) }; }      // _pic(palette file)
    WmPic pic_pair(int t, int p) const { return { wfile(t), wfile(p) }; } // _pic((texture, palette))

    // wm_mesh: a WM2000 arena mesh file (WM_LOOK: colour 255), its texels moved by whole repeats (rebase)
    Mesh wm_mesh(int f, int pal = 0, bool look = true) const {
        const Bytes& d = wfile(f);
        bool alt = d[0] & 0x80; int sc_ = (d[0] & 0x7F) + 1, nv = d[1] & 0x7F, nt = d[2];
        int off[3] = { s8(d[4]) * 16, s8(d[5]) * 16, s8(d[6]) * 16 };
        int pu = (d[7] >> 4) * 16, pv = (d[7] & 15) * 16;
        Mesh m;
        for (int i = 0; i < nv; i++) {
            size_t o = 8 + 8 * (size_t)i;
            m.pts.push_back({ (double)((s8(d[o]) + off[0]) * sc_), (double)((s8(d[o + 1]) + off[1]) * sc_), (double)((s8(d[o + 2]) + off[2]) * sc_) });
            if (alt) { m.uv.push_back({ be16(d, o + 4), be16(d, o + 6) }); m.rgb.push_back({ 255, 255, 255 }); }
            else {
                m.uv.push_back({ (int64_t)d[o + 3] + pu, (int64_t)d[o + 4] + pv });
                m.rgb.push_back(look ? std::array<uint8_t, 3>{ 255, 255, 255 } : std::array<uint8_t, 3>{ d[o + 5], d[o + 6], d[o + 7] });
            }
        }
        size_t o = 8 + 8 * (size_t)nv;
        for (int i = 0; i < nt; i++) m.tris.push_back({ d[o + 3 * i], d[o + 3 * i + 1], d[o + 3 * i + 2] });
        if (pal) {                                    // rebase(uv, *tex_size(pal))
            const Bytes& t = wfile(pal + 1);
            int64_t w = t[0] + 1, hh = t[1] + 1; int cms = t[4], cmt = t[5];
            for (int k = 0; k < 2; k++) {
                int fl = k ? cmt : cms; int64_t size = k ? hh : w;
                if (fl & 2) continue;
                int64_t per = size * ((fl & 1) ? 2 : 1), lo = m.uv[0][k];
                for (auto& x : m.uv) lo = std::min(lo, x[k]);
                int64_t b = floordiv(lo, per) * per;
                for (auto& x : m.uv) x[k] -= b;
            }
        }
        m.b1 = look ? WM_LOOK : 0; m.b3 = d[3];
        return m;
    }

    // models(): the scene's models in node order (sprites left out), each turned and placed as its node, prep_uv'd;
    // the mirrored nodes' models twice (k + 1000: x mirrored, the winding turned)
    void make_models() {
        std::vector<Bytes> geo = entries(wfile(sc.at("geo_file").get<int>()));
        for (auto& nd : sc.at("nodes")) {
            if (nd.at("flags").get<int>() & 0x8000) continue;
            int first = nd.at("first").get<int>(), count = nd.at("count").get<int>(), deg = nd.at("ry_deg").get<int>();
            double pos[3] = { nd.at("pos")[0].get<double>(), nd.at("pos")[1].get<double>(), nd.at("pos")[2].get<double>() };
            for (int k = first; k < first + count; k++) {
                const ojson& m = sc.at("models").at(k);
                const Bytes& vb = geo.at(m.at("vtx_entry").get<int>()); const Bytes& tb = geo.at(m.at("tri_entry").get<int>());
                Model it; it.k = k;
                std::vector<P3> V; std::vector<Tri> T;
                for (int v_ = 0; v_ < m.at("n_verts").get<int>(); v_++) {
                    size_t o = 16 * (size_t)v_;
                    double x = (int16_t)be16(vb, o), y = (int16_t)be16(vb, o + 2), z = (int16_t)be16(vb, o + 4);
                    double s = (int16_t)be16(vb, o + 8), t = (int16_t)be16(vb, o + 10);
                    int a = vb[o + 15];
                    if (deg) {                         // rot_y (math.cos / sin of math.radians)
                        double r = deg * (3.141592653589793 / 180.0), cs = std::cos(r), sn = std::sin(r);
                        double nx = x * cs + z * sn, nz = -x * sn + z * cs; x = nx; z = nz;
                    }
                    V.push_back({ x + pos[0], y + pos[1], z + pos[2] }); it.st.push_back({ s / 32.0, t / 32.0 }); it.alpha.push_back(a);
                }
                std::vector<std::pair<int, int>> starts; int acc = 0;
                for (auto& b : m.at("batches")) { starts.push_back({ b[1].get<int>(), acc }); acc += b[0].get<int>(); }
                int cur = 0;
                for (int ti = 0; ti < m.at("n_tris").get<int>(); ti++) {
                    for (auto& [at, o] : starts) if (at == ti) cur = o;
                    int a_ = tb[4 * ti], b_ = tb[4 * ti + 1], c_ = tb[4 * ti + 2], rot = tb[4 * ti + 3];
                    Tri tri = rot == 0 ? Tri{ a_, b_, c_ } : rot == 1 ? Tri{ b_, c_, a_ } : rot == 2 ? Tri{ c_, a_, b_ } : Tri{ a_, b_, c_ };
                    T.push_back({ cur + tri[0], cur + tri[1], cur + tri[2] });
                }
                it.verts = V; it.tris = T;
                it.tex = { m.at("tex_entry").get<int>(), m.at("pal_entry").get<int>(), m.at("ci8").get<bool>() };
                it.w = m.at("width").get<int>(); it.h = m.at("height").get<int>(); it.cms = m.at("cms").get<int>(); it.cmt = m.at("cmt").get<int>();
                Model item = prep_uv(it);
                models.push_back(item);
                if (mirror.count(k)) {
                    // (the Python's mirror copies the node's own points and triangles -- V, T before prep_uv -- with the
                    // prepared item's texels and alphas)
                    Model mm = item; mm.k = k + 1000;
                    mm.verts.clear(); for (auto& p : V) mm.verts.push_back({ -p[0], p[1], p[2] });
                    mm.tris.clear(); for (auto& t : T) mm.tris.push_back({ t[0], t[2], t[1] });
                    models.push_back(mm);
                }
            }
        }
    }

    // set_picture: the scene's picture entry with No Mercy's header (w-1, h-1; the wrap of the first model using it)
    WmPic set_picture(const Tex& tex) const {
        Bytes t = tex_entries.at(tex.entry);
        int w = 1 << t[6], h = 1 << t[7];
        if (w - 1 > 255 || h - 1 > 255) fail("arenas_wm set_picture: a %dx%d picture", w, h);
        t[0] = (uint8_t)(w - 1); t[1] = (uint8_t)(h - 1);
        const ojson* mm = nullptr;
        for (auto& m : sc.at("models")) if (m.at("tex_entry").get<int>() == tex.entry && m.at("pal_entry").get<int>() == tex.pal) { mm = &m; break; }
        if (!mm) fail("arenas_wm set_picture: no model uses picture %d", tex.entry);
        t[4] = (uint8_t)mm->at("cms").get<int>(); t[5] = (uint8_t)mm->at("cmt").get<int>();
        const Bytes& p = tex_entries.at(tex.pal);
        Bytes pal(p.begin(), p.begin() + std::min<size_t>(p.size(), tex.ci8 ? 512 : 32));
        return { t, pal };
    }
    WmPic picture_for(const Mesh& m, const WmPic& pic) const { return (is_alt(m) && !(m.b1 & WM_LOOK)) ? brighter(pic) : pic; }

    // facing(): only the triangles some match camera sees from the front
    Model facing(const Model& m, const P3& move, double scale) const {
        std::vector<Tri> keep;
        for (auto& t : m.tris) {
            P3 a = placed(m.verts[t[0]], scale, move), b = placed(m.verts[t[1]], scale, move), c_ = placed(m.verts[t[2]], scale, move);
            bool any = false;
            for (auto& e : EYES) if (side_of(a, b, c_, e) * front_sign > 0) { any = true; break; }
            if (any) keep.push_back(t);
        }
        std::set<int> used; for (auto& t : keep) used.insert(t.begin(), t.end());
        std::map<int, int> at; int n = 0; for (int i : used) at[i] = n++;
        Model o = m; o.verts.clear(); o.st.clear(); o.alpha.clear(); o.tris.clear();
        for (int i : used) { o.verts.push_back(m.verts[i]); o.st.push_back(m.st[i]); o.alpha.push_back(m.alpha[i]); }
        for (auto& t : keep) o.tris.push_back({ at[t[0]], at[t[1]], at[t[2]] });
        return o;
    }
    void set_front_sign() {                          // _front_sign(): the screen faces the ring
        const Model* m = nullptr;
        for (auto& x : models) if (x.k == screen) { m = &x; break; }
        if (!m) fail("arenas_wm: no screen model");
        const Tri& t = m->tris.at(0);
        P3 a = placed(m->verts[t[0]], match_scale, match_move), b = placed(m->verts[t[1]], match_scale, match_move), c_ = placed(m->verts[t[2]], match_scale, match_move);
        front_sign = side_of(a, b, c_, EYES[0]) > 0 ? 1 : -1;
    }

    struct Part { std::string name; Mesh m; WmPic pic; };

    // stage_parts (before painter's order): the 3D set merged by picture, the screen its own part; the kept pieces
    std::vector<Part> stage_unordered() {
        double SC = match_scale; P3 MV = match_move;
        std::vector<Model> ms;
        if (c->raw_set) {
            set_front_sign();
            for (auto& m : models) if (m.k != 30 && m.k != 1030) ms.push_back(m.tex.entry == TRUSS_TEX ? facing(m, MV, SC) : m);
        } else ms = models;
        struct G { Tex tex; std::vector<Model> g; };
        std::vector<G> groups;
        for (auto& m : ms) {
            G* put = nullptr;
            for (auto& g : groups) {
                if (!(g.tex == m.tex) || m.k == screen) continue;
                bool has_scr = false; size_t nv = 0, nt = 0;
                for (auto& x : g.g) { if (x.k == screen) has_scr = true; nv += x.verts.size(); nt += x.tris.size(); }
                if (has_scr || nv + m.verts.size() > 127 || nt + m.tris.size() > 127) continue;
                put = &g; break;
            }
            if (put) put->g.push_back(m); else groups.push_back({ m.tex, { m } });
        }
        std::vector<std::pair<double, size_t>> key;
        for (size_t i = 0; i < groups.size(); i++) {
            std::vector<P3> all; for (auto& m : groups[i].g) all.insert(all.end(), m.verts.begin(), m.verts.end());
            key.push_back({ depth_of(all), i });
        }
        std::stable_sort(key.begin(), key.end(), [](auto& a, auto& b) { return a.first < b.first; });
        double skirt_floor = -MV[1] / SC;
        std::vector<Part> out;
        for (auto& [d, gi] : key) {
            auto& g = groups[gi];
            Clip clip;
            if (c->raw_set) for (auto& m : g.g) if (m.k == 19) clip.kind = 1;
            if (c->raw_set) for (auto& m : g.g) if (m.k % 1000 == 29) { clip.kind = 2; clip.y_min = skirt_floor; }
            std::string name = (screen >= 0 && g.g[0].k == screen) ? "screen" : strf("set t%d", g.tex.entry);
            Mesh mm = set_mesh(g.g, MV, clip, SC, screen);
            out.push_back({ name, mm, picture_for(mm, set_picture(g.tex)) });
        }
        const Keep& k = c->keep;
        Mesh r = roty(wm_mesh(k.mesh, k.pal), 180);
        if (k.cut) {
            std::vector<Tri> keep;
            for (auto& t : r.tris) { double z = std::min({ r.pts[t[0]][2], r.pts[t[1]][2], r.pts[t[2]][2] }); if (z < k.zcut) keep.push_back(t); }
            r.tris = keep;
        }
        out.push_back({ c->raw_set ? "ramp" : "aisle", r, pic_of(k.pal) });
        if ((int)out.size() > SET_MAX) fail("arenas_wm: %zu stage parts", out.size());
        return out;
    }

    // close_parts (before painter's order): every model (far first) at the close scale + move
    std::vector<Part> close_unordered() {
        P3 CM = close_move; double SC = sc_scale;
        std::vector<std::pair<double, size_t>> key;
        for (size_t i = 0; i < models.size(); i++) key.push_back({ depth_of(models[i].verts), i });
        std::stable_sort(key.begin(), key.end(), [](auto& a, auto& b) { return a.first < b.first; });
        std::vector<Part> out;
        for (auto& [d, mi] : key) {
            const Model& m = models[mi];
            if (close_skip.count(m.k) || close_skip.count(m.k - 1000)) continue;
            Mesh mm = set_mesh({ m }, CM, {}, SC, screen);
            out.push_back({ strf("m%d", m.k), mm, picture_for(mm, set_picture(m.tex)) });
        }
        return out;
    }
};

std::vector<int> frozen_order(const ojson& fr, int v, const char* which, size_t n) {
    const ojson& e = fr.at("venues").at(std::to_string(v)).at(which);
    if (e.at("n").get<size_t>() != n) fail("wm_painter_order.json: venue %d %s has %zu pieces, the frozen order %zu", v, which, n, e.at("n").get<size_t>());
    std::vector<int> o; std::vector<bool> seen(n, false);
    for (auto& x : e.at("order")) {
        int i = x.get<int>();
        if (i < 0 || (size_t)i >= n || seen[i]) fail("wm_painter_order.json: venue %d %s: not an order of its pieces", v, which);
        seen[i] = true; o.push_back(i);
    }
    if (o.size() != n) fail("wm_painter_order.json: venue %d %s: not an order of its pieces", v, which);
    return o;
}

std::array<int, 4> extent(const Mesh& m) {            // tron_rows' ext(): the mesh read back from its file (the plain form)
    Mesh q = is_alt(m) ? m : Mesh::from_file(encode(m));
    double x0 = q.pts[0][0], x1 = x0, y0 = q.pts[0][1], y1 = y0;
    for (auto& p : q.pts) { x0 = std::min(x0, p[0]); x1 = std::max(x1, p[0]); y0 = std::min(y0, p[1]); y1 = std::max(y1, p[1]); }
    return { (int)x0, (int)x1, (int)y0, (int)y1 };
}

}  // namespace

WmVenue wm_venue(int v, const RomFiles& wm, const ojson& scenes, const ojson& venues, const fs::path& frozen_dir) {
    if (v < 0 || v > 6) fail("WrestleMania 2000 has venues 0..6, not %d", v);
    Venue Vn(v, wm, scenes, venues);
    Bytes ft = read_file(frozen_dir / "wm_painter_order.json");
    ojson fr = ojson::parse(std::string(ft.begin(), ft.end()));
    WmVenue W; W.v = v; W.name = Vn.c->name; W.has_screen = Vn.screen >= 0;

    // the match's stage set and the close scene, in the frozen drawing order
    auto st0 = Vn.stage_unordered();
    std::vector<Venue::Part> st;
    for (int i : frozen_order(fr, v, "stage", st0.size())) st.push_back(st0[i]);
    auto cl0 = Vn.close_unordered();
    std::vector<Venue::Part> cl;
    for (int i : frozen_order(fr, v, "close", cl0.size())) cl.push_back(cl0[i]);
    for (auto& p : st) W.stage.push_back({ p.name, encode(p.m), p.pic });
    for (auto& p : cl) W.close.push_back({ p.name, encode(p.m), p.pic });

    // ring_lists
    Mesh A = Vn.wm_mesh(MAT[0], MAT[1]);
    W.A = { "", encode(A), Vn.pic_of(MAT[1]) }; W.A_cage = encode(caged(A));
    for (auto& [m, p] : Vn.aprons) { Mesh x = Vn.wm_mesh(m, p); W.apron.push_back({ "", encode(x), Vn.pic_of(p) }); W.apron_cage.push_back(encode(caged(x))); }
    for (auto& f : FLOOR) { Mesh x = table_side(roty(Vn.wm_mesh(f[0], f[1]), 180)); W.floor.push_back({ "", encode(x), Vn.pic_of(f[1]) }); W.floor_cage.push_back(encode(caged(x))); }
    std::vector<Mesh> bar;
    for (int c = 0; c < 4; c++) {
        std::vector<Mesh> a, b;
        for (int f : BARR_A) if (!(c == 0 && f == 0x5BB)) a.push_back(roty(Vn.wm_mesh(f, BARR_PAL), 90 * c));
        for (int f : BARR_B) b.push_back(roty(Vn.wm_mesh(f, BARR_PAL), 90 * c));
        bar.push_back(table_side(join(a))); bar.push_back(table_side(join(b)));
    }
    for (auto& m : bar) W.barricade.push_back({ "", encode(m), Vn.pic_of(BARR_PAL) });
    WmPic hook{ Bytes{ 15, 15, 0, 16, 0, 0, 4, 4 }, Bytes(32, 0) };
    hook.tex.resize(8 + 128, 0x11); hook.pal[2] = 0x10; hook.pal[3] = 0x85;          // ("hook": 16 x 16 of 0E0E0E)
    std::vector<std::pair<Mesh, WmPic>> corner = { { Vn.wm_mesh(0x57E, 0x57F), Vn.pic_pair(0x580, 0x57F) },
                                                  { Vn.wm_mesh(0x581, 0x582), Vn.pic_pair(0x583, 0x582) },
                                                  { Vn.wm_mesh(0x5B9), hook } };
    for (auto& [m, p] : corner) {
        W.corner.push_back({ "", encode(m), p });
        W.corner_cage.push_back(encode(map_pts(m, [](const P3& q) { return P3{ q[0] - 28, q[1], q[2] + 28 }; })));
    }
    // the crowd: pieces by tier, merged by picture pair
    typedef std::pair<int, int> AB;
    std::map<std::string, std::vector<std::pair<AB, Mesh>>> tiers;
    for (size_t g = 0; g < Vn.crowd.size(); g++) {
        auto& G = Vn.crowd[g];
        for (size_t i = 0; i < G.ms.size(); i++) {
            int f = G.ms[i], a = G.pa[i], b = G.pb[i];
            Mesh m = roty(Vn.wm_mesh(f, a), 180);
            if (TABLE && g == 0 && !WALLS.count(a) && !TIERS.count(a))
                m = map_pts(m, [](const P3& q) { return P3{ q[0], q[1], q[2] + TABLE_CROWD_SHIFT }; });
            const char* t = WALLS.count(a) ? "walls" : TIERS.count(a) ? "tiers" : BACK_ROW.count(f) ? "back" : BARS.count(f) ? "bars"
                          : MID_ROW.count(f) ? "mid" : FRONT_SIGNS.count(f) ? "front" : nullptr;
            if (!t) fail("arenas_wm: crowd mesh 0x%x of no tier", f);
            tiers[t].push_back({ { a, b }, m });
        }
    }
    std::vector<std::pair<AB, Mesh>> hands;
    for (int r : FRONT_ROW_TURNS) {
        Mesh h = roty(join({ Vn.wm_mesh(FRONT_ROW_M[0], FRONT_ROW_A), Vn.wm_mesh(FRONT_ROW_M[1], FRONT_ROW_A) }), r);
        double s = 0; for (auto& q : h.pts) s += q[2];
        if (TABLE && s < 0) h = map_pts(h, [](const P3& q) { return P3{ q[0], q[1], q[2] + TABLE_CROWD_SHIFT }; });
        hands.push_back({ { FRONT_ROW_A, FRONT_ROW_B }, h });
    }
    std::map<std::string, std::vector<std::pair<AB, Mesh>>> M;
    for (const char* k : { "walls", "tiers", "back", "bars", "mid", "front" }) M[k] = merge_greedy(tiers[k]);
    auto add = [&](int g, const std::vector<std::pair<AB, Mesh>>& xs, bool one) {
        for (auto& [ab, m] : xs) {
            if (one && ab.first != ab.second) fail("arenas_wm: a static crowd list with two frames");
            W.groups[g].push_back({ encode(m), Vn.pic_of(ab.first), Vn.pic_of(one ? ab.first : ab.second) });
        }
    };
    add(0, M["walls"], true); add(0, M["tiers"], true);
    add(1, M["back"], false);
    add(2, M["bars"], false); add(2, M["mid"], false);
    add(3, M["front"], false);
    add(4, merge_greedy(hands), false);
    W.empty = encode(empty_mesh());

    // rr_pieces: the stage set, the crowd groups (frame a), floor, aprons, mat, barricades, the corners turned to 4 sides
    auto rr = [&](const Bytes& mb, const WmPic& p, int turns) { W.rr.push_back({ mb, p.tex, p.pal, turns }); };
    for (auto& p : W.stage) rr(p.mesh, p.pic, 0);
    for (int g = 0; g < 5; g++) for (auto& it : W.groups[g]) rr(it.mesh, it.a, 0);
    for (auto& p : W.floor) rr(p.mesh, p.pic, 0);
    for (auto& p : W.apron) rr(p.mesh, p.pic, 0);
    rr(W.A.mesh, W.A.pic, 0);
    for (auto& p : W.barricade) rr(p.mesh, p.pic, 0);
    for (int r = 0; r < 4; r++) for (auto& p : W.corner) rr(p.mesh, p.pic, r);
    W.logo_tex = Vn.wfile(Vn.c->logo + 1); W.logo_pal = Vn.wfile(Vn.c->logo);

    // the Titantron rows' numbers: the screen's extent in both scenes, its picture's fold
    if (W.has_screen) {
        const Venue::Part* sm = nullptr; const Venue::Part* cm = nullptr;
        for (auto& p : st) if (p.name == "screen") { sm = &p; break; }
        for (auto& p : cl) if (p.name == strf("m%d", Vn.screen)) { cm = &p; break; }
        if (!sm || !cm) fail("arenas_wm: venue %d's screen not found", v);
        auto e1 = extent(sm->m), e2 = extent(cm->m);
        for (int i = 0; i < 4; i++) { W.tron_match[i] = e1[i]; W.tron_close[i] = e2[i]; }
        Bytes b(2048, 0);                              // fold(): the picture's first 2048 bytes after its header, zero-padded
        for (size_t i = 0; i < 2048 && 8 + i < sm->pic.tex.size(); i++) b[i] = sm->pic.tex[8 + i];
        uint32_t a = 0, x = 0;
        for (size_t i = 0; i < 2048; i += 4) { uint32_t w = be32(b, i); a += w; x = ((x << 1) | (x >> 31)) ^ w; }
        W.sig[0] = a; W.sig[1] = x;
    }
    return W;
}

std::vector<std::string> wm_tron_rows(const WmVenue& V, int k) {
    if (!V.has_screen) return {};
    const int* m = V.tron_match; const int* c = V.tron_close;
    return { strf("    { %d, %d, %d, %d, %d, 0x%08Xu, 0x%08Xu, 0 },   // %s: match -- WM2000's 3D set's big screen (its %s picture)",
                  k, m[0], m[1], m[2], m[3], V.sig[0], V.sig[1], V.name.c_str(), V.v == 0 ? "RAW IS WAR" : "own"),
             strf("    { %d, %d, %d, %d, %d, 0x%08Xu, 0x%08Xu, 0 },   //   ... close-up: the same screen in the close scene",
                  k, c[0], c[1], c[2], c[3], V.sig[0], V.sig[1]) };
}

}  // namespace ri
