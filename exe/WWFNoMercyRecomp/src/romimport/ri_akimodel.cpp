// ri_akimodel.cpp (phase3 p3folders, 2026-11-19): the AKI handlers' shared model core -- see ri_akimodel.h.
//
// Float arithmetic here is Python's (IEEE double, the same operations in the same order: the build has no float
// contraction), and the maths functions are the C runtime's that CPython calls (pow, atan2, sqrt), so the skin tests and
// the hand palette come out as the Python's to the bit. math.hypot is CPython 3.10's own (vector_norm), not the C one.
#include "ri_akimodel.h"
#include "ri_json.h"
#include "ri_arenascan.h"
#include "rr_core.h"
#include <algorithm>
#include <cmath>

namespace ri {

// ==== handlers\aki\model.py =================================================================================================
uint32_t Pool::one(const Bytes& b) {
    auto it = by_.find(b);
    if (it != by_.end()) return it->second;
    uint32_t v = POOL0 + (uint32_t)data.size();
    by_[b] = v; data.push_back(b);
    return v;
}

std::pair<uint32_t, uint32_t> Pool::pair(const Bytes& tex, const Bytes& pal) {
    auto key = std::make_pair(tex, pal);
    auto it = by_pair_.find(key);
    uint32_t q;
    if (it != by_pair_.end()) q = it->second;
    else {
        q = POOL0 + (uint32_t)data.size();
        by_pair_[key] = q; data.push_back(pal); data.push_back(tex); pairs.insert(q);
    }
    return { q + 1, q };
}

const std::vector<int>& model_fields() {
    static std::vector<int> f = [] {
        std::vector<int> out;
        for (int k = 1; k < 28; k++) { int o = part_offset(k); out.push_back(o); out.push_back(o + 2); out.push_back(o + 4); }
        return out;
    }();
    return f;
}

size_t model_write(const fs::path& folder, const std::vector<Bytes>& descs, const Pool& pool) {
    std::set<uint32_t> used, keep;
    for (auto& d : descs)
        for (int o : model_fields()) { uint32_t v = be16(d, o); if (pool.ours(v)) used.insert(v); }
    for (uint32_t v : used) {                   // (a pair stays a pair: both or neither)
        keep.insert(v);
        if (pool.pairs.count(v)) keep.insert(v + 1);
        if (pool.pairs.count(v - 1)) keep.insert(v - 1);
    }
    std::vector<uint32_t> order(keep.begin(), keep.end());
    std::map<uint32_t, int> local;
    for (size_t i = 0; i < order.size(); i++) local[order[i]] = (int)i;
    fs::create_directories(folder / "model" / "files");
    ojson names = ojson::array();
    for (uint32_t v : order) {
        const Bytes& b = pool.get(v);
        std::string n = "files/" + sha1_hex(b).substr(0, 16) + ".bin";
        write_new_file(folder / "model" / fs::path(n), b);
        names.push_back(n);
    }
    ojson cos = ojson::array();
    for (size_t c = 0; c < descs.size(); c++) {
        Bytes d = descs[c];
        ojson fields = ojson::array();
        for (int o : model_fields()) {
            uint32_t v = be16(d, o);
            if (pool.ours(v)) { fields.push_back(ojson::array({ o, local[v] })); put_be16(d.data() + o, 0); }
        }
        write_new_file(folder / "model" / strf("c%zu.bin", c), d);
        ojson co = ojson::object();
        co["descriptor"] = strf("c%zu.bin", c); co["keep"] = ojson::array({ 0x254 }); co["fields"] = fields;
        cos.push_back(co);
    }
    ojson j = ojson::object();
    j["format"] = 1;
    j["about"] = "his own low-detail model, one descriptor per costume (handlers/aki/model.py has the format)";
    j["files"] = names; j["costumes"] = cos;
    // (json.dump into a file opened "w": Python's text mode on Windows, CRLF line ends; no newline at the end)
    write_new_file(folder / "model" / "model.json", json_file_text(j, 1));
    return order.size();
}

// ==== colour maths (Python's) ===============================================================================================
// Python's float % (the result takes the divisor's sign)
static double pymod(double x, double y) {
    double m = std::fmod(x, y);
    if (m != 0.0) { if ((y < 0) != (m < 0)) m += y; }
    else m = std::copysign(0.0, y);
    return m;
}

// CPython 3.10 mathmodule.c vector_norm (math.hypot): the sum of squares carried in double-double, then corrected
static double py_hypot2(double a, double b) {
    double vec[2] = { std::fabs(a), std::fabs(b) };
    double max = std::max(vec[0], vec[1]);
    if (std::isinf(max)) return max;
    if (std::isnan(a) || std::isnan(b)) return NAN;
    if (max == 0.0) return max;
    const double T27 = 134217729.0;
    double x, csum = 1.0, oldcsum, frac = 0.0, scale, t, hi, lo, h;
    int max_e;
    std::frexp(max, &max_e);
    if (max_e < -1023) fail("hypot: subnormal values aren't expected here");
    scale = std::ldexp(1.0, -max_e);
    for (int i = 0; i < 2; i++) {
        x = vec[i] * scale;
        t = x * T27; hi = t - (t - x); lo = x - hi;
        x = hi * hi; oldcsum = csum; csum += x; frac += (oldcsum - csum) + x;
        x = 2.0 * hi * lo; oldcsum = csum; csum += x; frac += (oldcsum - csum) + x;
        x = lo * lo; oldcsum = csum; csum += x; frac += (oldcsum - csum) + x;
    }
    h = std::sqrt(csum - 1.0 + frac);
    x = h; t = x * T27; hi = t - (t - x); lo = x - hi;
    x = -hi * hi; oldcsum = csum; csum += x; frac += (oldcsum - csum) + x;
    x = -2.0 * hi * lo; oldcsum = csum; csum += x; frac += (oldcsum - csum) + x;
    x = -lo * lo; oldcsum = csum; csum += x; frac += (oldcsum - csum) + x;
    x = csum - 1.0 + frac;
    return (h + x / (2.0 * h)) / scale;
}

// colorsys.rgb_to_hsv
static void rgb_to_hsv(double r, double g, double b, double& h, double& s, double& v) {
    double maxc = std::max({ r, g, b }), minc = std::min({ r, g, b });
    v = maxc;
    if (minc == maxc) { h = 0.0; s = 0.0; return; }
    s = (maxc - minc) / maxc;
    double rc = (maxc - r) / (maxc - minc), gc = (maxc - g) / (maxc - minc), bc = (maxc - b) / (maxc - minc);
    if (r == maxc) h = bc - gc;
    else if (g == maxc) h = 2.0 + rc - bc;
    else h = 4.0 + gc - rc;
    h = pymod(h / 6.0, 1.0);
}

typedef std::array<double, 3> C3d;

std::array<int, 3> rgb5551(uint32_t v) { return { (int)((v >> 11) << 3), (int)(((v >> 6) & 31) << 3), (int)(((v >> 1) & 31) << 3) }; }

std::vector<int> tex_pixels(const Bytes& tex) {             // wmport._pix
    int w = tex[0] + 1, h = tex[1] + 1;
    std::vector<int> out;
    if (tex[2] == 1) { for (int i = 0; i < w * h && 8 + i < (int)tex.size(); i++) out.push_back(tex[8 + i]); return out; }
    for (int i = 0; i < w * h / 2 && 8 + i < (int)tex.size(); i++) { out.push_back(tex[8 + i] >> 4); out.push_back(tex[8 + i] & 15); }
    return out;
}

bool wm_skin(double r, double g, double b) {                // wmport._skin (and skintone.W_skin: the same test)
    double hh, ss, vv;
    rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0, hh, ss, vv);
    return (hh < 50.0 / 360 || hh > 345.0 / 360) && 0.15 <= ss && ss <= 0.78 && vv >= 0.22 && r > b + 25;
}

static C3d lab(const C3d& c) {                               // skintone._lab
    auto lin = [](double u) { u /= 255.0; return u <= 0.04045 ? u / 12.92 : std::pow((u + 0.055) / 1.055, 2.4); };
    double r = lin(c[0]), g = lin(c[1]), b = lin(c[2]);
    double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
    double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    auto f = [](double t) { return t > 0.008856 ? std::pow(t, 1.0 / 3) : 7.787 * t + 16.0 / 116; };
    return { 116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z)) };
}

static void lch(const C3d& c, double& L, double& C, double& h) {
    C3d l = lab(c);
    L = l[0]; C = py_hypot2(l[1], l[2]);
    h = std::atan2(l[2], l[1]) * (180.0 / 3.141592653589793);      // (math.degrees: x * radToDeg)
}

static double hsv_hue(const C3d& c) {
    double h, s, v;
    rgb_to_hsv(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, h, s, v);
    h *= 360.0;
    return h > 345.0 ? h - 360.0 : h;
}

static constexpr double SKIN_HUE0 = 5.0, SKIN_HUE1 = 25.0, SKIN_DH = 12.0, SKIN_C = 0.4, SKIN_DL0 = 45.0, SKIN_DL1 = 35.0;
static constexpr double DEEP_DH = 6.0, FACE_DARKER = 25.0, FACE_FAR = 15.0;
static constexpr int FACE_PIX = 150, WRIST_PIX = 200;

static bool near_skin(const C3d& c, const C3d& own) {
    double L, C, h, L0, C0, h0;
    lch(c, L, C, h); lch(own, L0, C0, h0);
    return std::fabs(pymod(h - h0 + 180.0, 360.0) - 180.0) <= SKIN_DH && C >= SKIN_C * C0 && L0 - SKIN_DL0 <= L && L <= L0 + SKIN_DL1;
}

static bool deep_skin(const C3d& c, const C3d& own) {
    if (wm_skin(c[0], c[1], c[2])) return true;
    double hh, ss, vv;
    rgb_to_hsv(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, hh, ss, vv);
    double h = hsv_hue(c), h0 = hsv_hue(own);
    return ss > 0.78 && vv >= 0.15 && c[0] > c[2] + 25 && SKIN_HUE0 <= h && h < SKIN_HUE1 && std::fabs(h - h0) <= DEEP_DH;
}

static bool far_(const C3d& a, const C3d& b) {
    C3d la = lab(a), lb = lab(b);
    double d0 = 0.5 * (la[0] - lb[0]), d1 = la[1] - lb[1], d2 = la[2] - lb[2];
    return std::sqrt(d0 * d0 + d1 * d1 + d2 * d2) > FACE_FAR;
}
static bool darker(const C3d& body, const C3d& face) { return lab(face)[0] - lab(body)[0] > FACE_DARKER; }

static uint32_t pack5551(const C3d& c, uint32_t alpha) {       // skintone._pack
    auto q = [](double x) { return (uint32_t)std::min(31, std::max(0, (int)(x / 8.0 + 0.5))); };
    return (q(c[0]) << 11) | (q(c[1]) << 6) | (q(c[2]) << 1) | alpha;
}

static Bytes bake(const Bytes& pal, int row) {               // wmport._bake: palette row `row` copied to the top
    Bytes b = pal;
    std::memcpy(b.data(), pal.data() + 0x20 * row, 0x20);
    return b;
}

// ==== handlers\aki\skintone.py ================================================================================================
static constexpr uint32_t SKEL_WM_FLAGS = 0x264, SKEL_WM_NMHANDS = 0x40000000;
static const int SKIN_KEEP[3] = { 4, 5, 20 };                  // wmport.SKIN_KEEP: the head, its own

const HandKit& man_kit() {        // wmport.NM_HAND_MESH / NM_HAND_PIC: No Mercy's builder's hands for a man of body type 3
    static HandKit k{ { { 9, 0x1112 }, { 10, 0x1113 }, { 17, 0x1119 }, { 18, 0x111A } }, 0x296E, 0x296D, false };
    return k;
}
const HandKit& woman_kit() {      // skintone.WOMAN_KIT: No Mercy's women's palms and picture, the straight finger meshes
    static HandKit k{ { { 9, 0x118A }, { 10, 0x145D }, { 17, 0x1191 }, { 18, 0x145E } }, 0x2A96, 0x2A95, true };
    return k;
}

namespace {
struct Pic { std::vector<std::array<int, 3>> cs; std::vector<int> px; };
struct Measure { double frac; long n; C3d avg; };
}

Bytes nm_hands(const Bytes& model, Pool& pool, const RomFiles& nm, Skins& skins, const std::string& who, const HandKit& kit,
               bool rev_ported) {
    Bytes d = model;
    auto data = [&](uint32_t i) -> const Bytes& { return i >= POOL0 ? pool.get(i) : nm(i); };
    auto colours = [](const Bytes& pal) {
        std::vector<std::array<int, 3>> cs;
        for (size_t i = 0; i + 1 < pal.size(); i += 2) cs.push_back(rgb5551(be16(pal, i)));
        return cs;
    };
    std::map<int, Pic> pics;                                     // (k in order: the Python's dict is filled 1..19)
    for (int k = 1; k < 20; k++) {
        uint32_t t = be16(d, 8 * k + 2), q = be16(d, 8 * k + 4);
        if (!t || k == 4 || k == 5) continue;
        pics[k] = Pic{ colours(data(q)), tex_pixels(data(t)) };
    }
    typedef std::function<bool(const C3d&)> Test;
    auto measure = [&](const Test& test) {
        std::map<int, Measure> out;
        for (auto& [k, p] : pics) {
            std::vector<int> ok(p.cs.size());
            for (size_t i = 0; i < p.cs.size(); i++) ok[i] = test({ (double)p.cs[i][0], (double)p.cs[i][1], (double)p.cs[i][2] });
            long n = 0, sum[3] = { 0, 0, 0 };
            for (int i : p.px)
                if (i < (int)p.cs.size() && ok[i]) { n++; for (int j = 0; j < 3; j++) sum[j] += p.cs[i][j]; }
            Measure m;
            m.frac = (double)n / (double)std::max<size_t>(1, p.px.size()); m.n = n;
            for (int j = 0; j < 3; j++) m.avg[j] = (double)sum[j] / (double)std::max<long>(1, n);
            out[k] = m;
        }
        return out;
    };
    auto is_hand = [&](int k) { return k == 9 || k == 10 || k == 17 || k == 18; };   // (wmport.NM_HAND_MESH's parts)
    auto src_of = [&](const std::map<int, Measure>& parts, std::vector<int>* full_out) {
        std::vector<int> full, half;
        for (auto& [k, p] : parts) { if (p.frac >= 0.95 && !is_hand(k)) full.push_back(k); if (p.frac >= 0.5) half.push_back(k); }
        if (full_out) *full_out = full;
        return full.empty() ? half : full;
    };
    auto weighted = [&](const std::map<int, Measure>& parts, const std::vector<int>& src, long& tot) {
        tot = 0; for (int k : src) tot += parts.at(k).n;
        C3d m{};
        for (int j = 0; j < 3; j++) {
            double s = 0; for (int k : src) s += parts.at(k).avg[j] * (double)parts.at(k).n;
            m[j] = s / (double)tot;
        }
        return m;
    };
    auto mean_of = [&](const std::map<int, Measure>& parts) -> std::optional<C3d> {
        long tot; std::vector<int> src = src_of(parts, nullptr);
        C3d m = weighted(parts, src, tot);
        if (!tot) return std::nullopt;
        return m;
    };
    Test first = [](const C3d& c) { if (!wm_skin(c[0], c[1], c[2])) return false; double h = hsv_hue(c); return SKIN_HUE0 <= h && h < SKIN_HUE1; };
    std::optional<C3d> own = mean_of(measure(first));
    std::optional<C3d> face;
    {
        double acc[3] = { 0, 0, 0 }; long n = 0;
        for (int k : SKIN_KEEP) {
            uint32_t t = be16(d, 8 * k + 2), q = be16(d, 8 * k + 4);
            if (!t) continue;
            auto cs = colours(data(q));
            for (int i : tex_pixels(data(t)))
                if (i < (int)cs.size() && first({ (double)cs[i][0], (double)cs[i][1], (double)cs[i][2] })) {
                    for (int j = 0; j < 3; j++) acc[j] += cs[i][j];
                    n++;
                }
        }
        if (n >= FACE_PIX) face = C3d{ acc[0] / n, acc[1] / n, acc[2] / n };
    }
    bool from_face = false;
    std::optional<C3d> ref = face;
    if (!ref && skins.tone.count(who)) ref = skins.tone[who];
    if (ref && own && darker(*own, *ref)) { own = ref; from_face = true; }
    if (!own && skins.tone.count(who)) own = skins.tone[who];
    if (!own) own = mean_of(measure([](const C3d& c) { return wm_skin(c[0], c[1], c[2]); }));
    if (!own) throw NoSkin(who + ": no bare skin in any costume so far (its skin tone unknown)");
    C3d o = *own;
    auto parts = measure([&](const C3d& c) { return wm_skin(c[0], c[1], c[2]) && near_skin(c, o); });
    std::vector<int> full;
    std::vector<int> src = src_of(parts, &full);
    std::optional<C3d> keep;
    if (rev_ported) {
        if (!skins.bare.count(who)) skins.bare[who] = std::set<int>(full.begin(), full.end());
        else {
            std::vector<int> bare;
            for (int k : skins.bare[who]) if (parts.count(k) && parts[k].frac >= 0.95) bare.push_back(k);
            if (!bare.empty()) src = bare;
            else if (skins.tone.count(who)) keep = skins.tone[who];
        }
    }
    long tot;
    C3d wmean = weighted(parts, src, tot);
    C3d have;
    if (keep) have = *keep;
    else if (tot) { have = wmean; if (!skins.tone.count(who)) skins.tone[who] = have; }
    else if (skins.tone.count(who) && face && far_(*face, skins.tone[who])) have = *face;
    else if (skins.tone.count(who)) have = skins.tone[who];
    else if (from_face) { have = o; skins.tone[who] = have; }
    else throw NoSkin(who + ": no bare skin in any costume so far (its skin tone unknown)");
    // the hands: the skin at his wrists (his forearms', parts 11 and 16, when they show enough of it), else his skin's
    auto wparts = measure([&](const C3d& c) { return deep_skin(c, o) && near_skin(c, o); });
    std::vector<int> fore;
    for (int k : { 11, 16 }) if (wparts.count(k) && wparts[k].n >= WRIST_PIX) fore.push_back(k);
    C3d target = have;
    if (!fore.empty()) {
        long n = 0; for (int k : fore) n += wparts[k].n;
        C3d wrist;
        for (int j = 0; j < 3; j++) { double s = 0; for (int k : fore) s += wparts[k].avg[j] * (double)wparts[k].n; wrist[j] = s / (double)n; }
        if (!far_(wrist, have)) target = wrist;
    }
    // hand_in_skin: No Mercy's hand picture in the palette row nearest the target, each colour times target / that row's mean
    const Bytes& pic = nm(kit.pic);
    std::vector<int> px = tex_pixels(pic);
    C3d means[8];
    for (int r = 0; r < 8; r++) {
        auto cs = colours(bake(nm(kit.pal), r));
        long s[3] = { 0, 0, 0 };
        for (int i : px) for (int j = 0; j < 3; j++) s[j] += cs.at(i)[j];
        for (int j = 0; j < 3; j++) means[r][j] = (double)s[j] / (double)px.size();
    }
    int row = 0; double best = 0;
    for (int r = 0; r < 8; r++) {
        double e = 0;
        for (int j = 0; j < 3; j++) { double x = means[r][j] - target[j]; e += x * x; }
        if (r == 0 || e < best) { best = e; row = r; }
    }
    C3d gain;
    for (int j = 0; j < 3; j++) gain[j] = target[j] / std::max(1.0, means[row][j]);
    Bytes pal = bake(nm(kit.pal), row);
    for (int i = 0; i < 16; i++) {
        uint32_t v = be16(pal, 2 * i);
        auto c = rgb5551(v);
        C3d x;
        for (int j = 0; j < 3; j++) x[j] = std::min(255.0, c[j] * gain[j]);
        put_be16(pal.data() + 2 * i, pack5551(x, v & 1));
    }
    auto [t, q] = pool.pair(pic, pal);
    for (auto& [k, mesh] : kit.mesh) { put_be16(d.data() + 8 * k, mesh); put_be16(d.data() + 8 * k + 2, t); put_be16(d.data() + 8 * k + 4, q); }
    put_be32(d.data() + SKEL_WM_FLAGS, be32(d, SKEL_WM_FLAGS) | SKEL_WM_NMHANDS);
    return d;
}

// ==== handlers\aki\handfit.py ================================================================================================
static int s8(int v) { return v > 127 ? v - 256 : v; }

static std::vector<std::array<int, 3>> mesh_verts(const Bytes& m) {
    int sc = (m[0] & 0x7F) + 1, nv = m[1] & 0x7F;
    int off[3] = { s8(m[4]) * 16, s8(m[5]) * 16, s8(m[6]) * 16 };
    std::vector<std::array<int, 3>> out;
    for (int j = 0; j < nv; j++) {
        std::array<int, 3> v;
        for (int q = 0; q < 3; q++) v[q] = (s8(m[8 + 8 * j + q]) + off[q]) * sc;
        out.push_back(v);
    }
    return out;
}

std::vector<Bytes> fit_wrists(const std::vector<Bytes>& descs, Pool& pool, const RomFiles& nm) {
    static const int PAIRS[2][2] = { { 11, 9 }, { 16, 17 } };   // (forearm, palm): right, left
    std::vector<Bytes> out;
    for (auto d : descs) {
        if (be32(d, SKEL_WM_FLAGS) & SKEL_WM_NMHANDS) {
            for (auto& pr : PAIRS) {
                uint32_t fm = be16(d, 8 * pr[0]), pm = be16(d, 8 * pr[1]);
                if (!fm || !pm || !pool.ours(fm)) continue;
                Bytes fore = pool.get(fm);
                const Bytes& palm = pool.ours(pm) ? pool.get(pm) : nm(pm);
                // gap(): the palm's top ring centre less the forearm's wrist group centre, rounded
                int g1 = fore[3] >> 4, g2 = fore[3] & 15;
                if (!g2) continue;
                auto vf = mesh_verts(fore), vp = mesh_verts(palm);
                int top_y = INT32_MIN;
                for (auto& v : vp) top_y = std::max(top_y, v[1]);
                std::vector<std::array<int, 3>> top, w(vf.begin() + g1, vf.begin() + g1 + g2);
                for (auto& v : vp) if (v[1] >= top_y - 2) top.push_back(v);
                auto mean = [](const std::vector<std::array<int, 3>>& vs, int q) {
                    long long s = 0; for (auto& v : vs) s += v[q]; return (double)s / (double)vs.size();
                };
                int dx = (int)py_round_int(mean(top, 0) - mean(w, 0)), dz = (int)py_round_int(mean(top, 2) - mean(w, 2));
                if (std::max(std::abs(dx), std::abs(dz)) < 2) continue;      // (MIN_GAP)
                // moved(): the wrist group by (dx, dz), when the format can hold it
                Bytes nw = fore;
                int sc = (nw[0] & 0x7F) + 1;
                bool ok = dx % sc == 0 && dz % sc == 0;
                for (int j = g1; ok && j < g1 + g2; j++) {
                    int o = 8 + 8 * j;
                    for (auto [qq, dv] : { std::pair<int, int>{ 0, dx }, std::pair<int, int>{ 2, dz } }) {
                        int v = s8(nw[o + qq]) + dv / sc;
                        if (v < -128 || v > 127) { ok = false; break; }
                        nw[o + qq] = (uint8_t)(v & 0xFF);
                    }
                }
                if (!ok) continue;
                put_be16(d.data() + 8 * pr[0], pool.one(nw));
            }
        }
        out.push_back(std::move(d));
    }
    return out;
}

// ==== tools\romrender\rr_model.py ==============================================================================================
namespace {
typedef long long i64;
i64 fdiv(i64 a, i64 b) { i64 q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }
i64 rdiv(i64 a, i64 b) { return fdiv(2 * a + b, 2 * b); }
i64 icos(i64 a) { return rr::isin(a + 0x4000); }
i64 deg(i64 d) { return rdiv(d * 65536, 360); }
std::array<i64, 3> norm16(const std::array<i64, 3>& v) {
    i64 n = rr::isqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    std::array<i64, 3> o;
    for (int k = 0; k < 3; k++) o[k] = v[k] >= 0 ? fdiv(v[k] * 65536, n) : -fdiv(-v[k] * 65536, n);
    return o;
}
std::array<i64, 3> cross(const std::array<i64, 3>& a, const std::array<i64, 3>& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
struct Pt { i64 x, y, z, s, t, nx, ny, nz; };
struct Part { int k; std::vector<Pt> pts; std::vector<std::array<int, 3>> tris; bool nrm; rr::Texture tex; };

rr::Texture texture_from_tex(const Bytes& tex, const Bytes& pal0, int group) {
    i64 w = tex[0] + 1, h = tex[1] + 1;
    Bytes pal = pal0;
    if (group > 0 && pal.size() >= (size_t)(0x20 * (group + 1))) std::memcpy(pal.data(), pal0.data() + 0x20 * group, 0x20);
    size_t n = pal.size() / 2;
    auto wrap_mode = [](int f) -> i64 { return (f & 2) ? 2 : ((f & 1) ? 1 : 0); };
    rr::Texture t{ w, h, wrap_mode(tex[4]), wrap_mode(tex[5]), {} };
    for (i64 i = 0; i < w * h; i++) {
        size_t k;
        if (tex[2] == 1) k = tex.at(8 + i);
        else { uint8_t b = tex.at(8 + (i >> 1)); k = (i & 1) == 0 ? (b >> 4) : (b & 15); }
        t.texels.push_back(k < n ? (uint16_t)((pal[2 * k] << 8) | pal[2 * k + 1]) : 0);
    }
    return t;
}

std::vector<Part> model_parts(const Bytes& desc, const FieldGetter& get) {
    bool wm = be32(desc, 0x268) == 0x574D324B;                 // (drawn_parts: joined WrestleMania 2000's way)
    std::vector<int> ks;
    if (wm) { for (int k = 1; k <= 20; k++) ks.push_back(k); ks.insert(ks.end(), { 25, 26, 27 }); }
    else { for (int k = 1; k < 20; k++) if (k != 4) ks.push_back(k); ks.insert(ks.end(), { 20, 21, 22, 23, 24, 25, 26, 27 }); }
    // group_rows: the parts that get palette row g over row 0
    std::set<int> grp; int g = desc[0x255];
    if (!(g >= 128 || g == 0)) {
        if (wm) { for (int k = 1; k < 20; k++) if (k != 4 && k != 5) grp.insert(k); }
        else { for (int k = 1; k < 25; k++) grp.insert(k); if (desc[0x254] & 2) grp.erase(20); }
    } else g = 0;
    std::vector<Part> out;
    for (int k : ks) {
        int o = part_offset(k);
        auto m = get(o);
        if (!m || m->size() < 8) continue;
        auto tex = get(o + 2), pal = get(o + 4);
        if (!tex || tex->empty() || !pal || pal->empty()) continue;
        const Bytes& M = *m;
        Part p; p.k = k;
        bool alt = M[0] & 0x80; int sc = (M[0] & 0x7F) + 1, nv = M[1] & 0x7F, nt = M[2];
        int off[3] = { s8(M[4]) * 16, s8(M[5]) * 16, s8(M[6]) * 16 };
        int hu = M[7] & 0xF0, hv = (M[7] << 4) & 0xF0;
        for (int i = 0; i < nv; i++) {
            int o2 = 8 + 8 * i;
            Pt q;
            q.x = (s8(M.at(o2)) + off[0]) * sc; q.y = (s8(M.at(o2 + 1)) + off[1]) * sc; q.z = (s8(M.at(o2 + 2)) + off[2]) * sc;
            if (alt) {
                q.s = ((i64)((M[o2 + 4] << 8) | M[o2 + 5]) << 5) + 8; q.t = ((i64)((M[o2 + 6] << 8) | M[o2 + 7]) << 5) + 8;
                q.nx = q.ny = q.nz = 0;
            } else {
                q.s = ((i64)(M[o2 + 3] + hu) << 5) + 8; q.t = ((i64)(M[o2 + 4] + hv) << 5) + 8;
                q.nx = s8(M[o2 + 5]); q.ny = s8(M[o2 + 6]); q.nz = s8(M[o2 + 7]);
            }
            p.pts.push_back(q);
        }
        int o3 = 8 + 8 * nv;
        for (int i = 0; i < nt; i++) p.tris.push_back({ M.at(o3 + 3 * i) & 0x7F, M.at(o3 + 3 * i + 1) & 0x7F, M.at(o3 + 3 * i + 2) & 0x7F });
        p.nrm = !alt;
        p.tex = texture_from_tex(*tex, *pal, grp.count(k) ? g : 0);
        out.push_back(std::move(p));
    }
    return out;
}

constexpr i64 AMBIENT = 150, DIFFUSE = 150, DIST = 600;

bool bounds(const std::vector<Part>& parts, std::initializer_list<int> ks, i64 b[6]) {
    bool any = false;
    for (auto& p : parts) {
        if (std::find(ks.begin(), ks.end(), p.k) == ks.end()) continue;
        for (auto& q : p.pts) {
            if (!any) { b[0] = b[1] = q.x; b[2] = b[3] = q.y; b[4] = b[5] = q.z; any = true; }
            else {
                b[0] = std::min(b[0], q.x); b[1] = std::max(b[1], q.x); b[2] = std::min(b[2], q.y);
                b[3] = std::max(b[3], q.y); b[4] = std::min(b[4], q.z); b[5] = std::max(b[5], q.z);
            }
        }
    }
    return any;
}
}  // namespace

const char* const PHOTO_HOW = "rendered by tools\\romrender (our renderer) from his model as this folder has it (costume 1): "
                              "head and shoulders, turned 15 degrees, lit from the left";

Bytes photo_from_model(const Bytes& desc, const FieldGetter& get) {
    std::vector<Part> parts = model_parts(desc, get);
    const i64 W = 64, H = 64, SS = 4;
    // portrait_camera(parts, 64, 64, deg(YAW = 15), 7, 4, 14)
    i64 face[6], head[6];
    if (!bounds(parts, { 5 }, face) && !bounds(parts, { 5, 20, 21 }, face)) fail("photo: the model has no head");
    if (!bounds(parts, { 5, 20, 21 }, head)) std::memcpy(head, face, sizeof head);
    i64 top = head[3], chin = face[2];
    i64 hh = std::max<i64>(1, top - chin);
    i64 win = fdiv(hh * 7, 4);
    i64 cx = fdiv(face[0] + face[1], 2), cz = fdiv(face[4] + face[5], 2);
    i64 cy = top + fdiv(win, 14) - fdiv(win, 2);
    i64 turn = deg(15), sn = rr::isin(turn), cs = icos(turn);
    std::array<i64, 3> eye{ cx + fdiv(DIST * sn, 65536), cy, cz + fdiv(DIST * cs, 65536) }, at{ cx, cy, cz };
    i64 focal16 = fdiv(H * 16 * DIST, win);
    // look_at
    rr::Camera cam;
    auto f = norm16({ at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] });
    auto r = norm16(cross(f, { 0, 1, 0 }));
    auto u = cross(r, f);
    for (auto& c : u) c = c >= 0 ? (c >> 16) : -((-c) >> 16);
    for (int k = 0; k < 3; k++) { cam.eye[k] = eye[k]; cam.right[k] = r[k]; cam.up[k] = u[k]; cam.fwd[k] = f[k]; }
    cam.focal16 = focal16; cam.cx16 = W * 8; cam.cy16 = H * 8; cam.near8 = 8 * 256;
    // scene_of: the light from over the camera's left shoulder; each point's shade from its normal
    auto L = norm16({ -cam.right[0] * 10 + cam.up[0] * 12 - cam.fwd[0] * 13, -cam.right[1] * 10 + cam.up[1] * 12 - cam.fwd[1] * 13,
                      -cam.right[2] * 10 + cam.up[2] * 12 - cam.fwd[2] * 13 });
    std::vector<rr::Texture> texs;
    std::vector<rr::Tri> tris;
    for (auto& p : parts) {
        i64 ti = (i64)texs.size();
        texs.push_back(p.tex);
        std::vector<rr::Vtx> vs;
        for (auto& q : p.pts) {
            i64 sh = 200;
            if (p.nrm) {
                i64 nn = rr::isqrt(q.nx * q.nx + q.ny * q.ny + q.nz * q.nz);
                if (nn == 0) sh = AMBIENT + DIFFUSE / 2;
                else {
                    i64 dd = q.nx * L[0] + q.ny * L[1] + q.nz * L[2];
                    sh = dd <= 0 ? AMBIENT : AMBIENT + fdiv(DIFFUSE * dd, nn * 65536);
                }
            }
            vs.push_back({ q.x, q.y, q.z, q.s, q.t, sh, sh, sh });
        }
        for (auto& t : p.tris)
            if (t[0] < (int)vs.size() && t[1] < (int)vs.size() && t[2] < (int)vs.size())
                tris.push_back(rr::Tri{ ti, 1 /* CULL_BACK */, { vs[t[0]], vs[t[1]], vs[t[2]] } });
    }
    const i64 bg[4] = { 0, 0, 0, 0 };
    std::vector<uint8_t> px = rr::render(texs, tris, cam, bg, W, H, SS);
    // photo_bin: 15 colours by median cut in RGB555 over the opaque pixels, colour 0 clear
    std::vector<rr::C3> cols; std::vector<size_t> opaque;
    for (size_t i = 0; i < (size_t)(W * H); i++)
        if (px[4 * i + 3]) {
            opaque.push_back(i);
            cols.push_back({ rdiv(px[4 * i] * 31, 255), rdiv(px[4 * i + 1] * 31, 255), rdiv(px[4 * i + 2] * 31, 255) });
        }
    std::vector<rr::C3> pal; std::vector<i64> idx;
    rr::quantise(cols, 15, pal, idx);
    std::vector<int> ci(W * H, 0);
    for (size_t j = 0; j < opaque.size(); j++) ci[opaque[j]] = (int)idx[j] + 1;
    Bytes out(32 + 2048, 0);
    for (size_t i = 0; i < pal.size() && i < 15; i++)
        put_be16(out.data() + 2 * (i + 1), (uint32_t)((pal[i][0] << 11) | (pal[i][1] << 6) | (pal[i][2] << 1) | 1));
    for (size_t i = 0; i < ci.size(); i += 2) out[32 + i / 2] = (uint8_t)((ci[i] << 4) | ci[i + 1]);
    return out;
}

Bytes photo_from_pool(const Bytes& desc, const Pool& pool, const RomFiles& nm) {
    return photo_from_model(desc, [&](int o) -> std::optional<Bytes> {
        uint32_t v = be16(desc, o);
        if (!v) return std::nullopt;
        if (pool.ours(v)) return pool.get(v);
        const Bytes* b = nm.get(v);
        if (!b) return std::nullopt;
        return *b;
    });
}

// ==== Python number forms ======================================================================================================
long long py_round_int(double x) { return (long long)std::nearbyint(x); }   // (the default rounding: to nearest, ties to even)

double py_round(double x, int nd) {
    // CPython's float round(x, n): the exact decimal value rounded to n places, ties to even, read back. The C runtime's
    // printf gives the exact digits of the double (it has at most 1074 of them; 40 places are past any tie these values have
    // ... checked: a tie needs x * 10^n + 0.5 to be a whole number, and the digits below are exact either way).
    if (!std::isfinite(x)) return x;
    char buf[512];
    snprintf(buf, sizeof buf, "%.60f", std::fabs(x));
    std::string s = buf;
    size_t dot = s.find('.');
    std::string ip = s.substr(0, dot), fp = s.substr(dot + 1);
    std::string keep = ip + fp.substr(0, nd), rest = fp.substr(nd);
    bool up;
    if (rest[0] > '5') up = true;
    else if (rest[0] < '5') up = false;
    else {
        bool more = rest.find_first_not_of('0', 1) != std::string::npos;
        up = more || ((keep.back() - '0') & 1);
    }
    if (up) {
        int i = (int)keep.size() - 1;
        while (i >= 0 && keep[i] == '9') keep[i--] = '0';
        if (i >= 0) keep[i]++; else keep.insert(keep.begin(), '1');
    }
    std::string num = keep.substr(0, keep.size() - nd) + "." + keep.substr(keep.size() - nd);
    double v = std::strtod(num.c_str(), nullptr);
    return x < 0 ? -v : v;
}

}  // namespace ri
