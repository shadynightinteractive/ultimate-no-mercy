// ri_wmport.cpp (phase3 p3wmport, 2026-11-19): mod\gen\wmport.py build_all natively -- see ri_wmport.h.
//
// Byte for byte the Python's, so its arithmetic exactly where it decides bytes: the files numbered in the order the Python
// first meets them (Files.one / Files.pair: a pair's palette first, then its picture), floats in the Python's order (the
// skin tones' sums and means, the brightness sort of the finger colours), int() truncating, min() keeping the first.
#include "ri_wmport.h"
#include "ri_capture.h"
#include "ri_images.h"
#include "ri_collide.h"
#include <algorithm>
#include <cmath>
#include <set>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace ri {

std::string crlf(const std::string& lf) {
    std::string c; c.reserve(lf.size() + lf.size() / 16);
    for (char ch : lf) { if (ch == '\n') c += '\r'; c += ch; }
    return c;
}

CollideFn native_collide(Roms& R, const Bytes* py_source) {
    return [&R, py_source](const Bytes& pack, const std::map<std::string, std::string>& hs) {
        auto get = [&](const char* n) { auto it = hs.find(n); if (it == hs.end()) fail("collide: no %s", n); return it->second; };
        // (arenasplit: a pack of one of the two source games has no stage sets of the game's ten -- no text of their headers)
        auto opt = [&](const char* n) { auto it = hs.find(n); return it == hs.end() ? std::string() : it->second; };
        collide::HeaderTexts h{ get("wmport_files.h"), opt("arena_raw_files.h"), opt("arena_stage_files.h"), get("arena_ring_files.h"), get("arena_files.h") };
        std::map<int, std::string> out;
        // (arenasplit 2026-11-24: each result under its arena's number -- 0..22 in order with both source games; with one, its
        //  arcade arenas' alone)
        for (auto& c : collide::collide_all(pack, h, R, py_source)) out[c.n] = collide::result_json(c);
        return out;
    };
}

WmportOut wmport_build(Roms& R, const RiData& D, const std::string& rev_anims_json, bool with_rma, const HdrFill* shape) {
    WmportIn in;
    in.shape = shape;
    in.rev_anims_json = rev_anims_json;
    in.with_rma = with_rma;
    in.collide = native_collide(R);
    return make_wmport(R, D, in);
}

const Bytes& WmCtx::image(const std::string& name) {
    auto it = images.find(name);
    if (it == images.end()) it = images.emplace(name, make_image(name, D.dir / "loadmaps", R)).first;
    return it->second;
}

namespace {

// ---- RAM images: what the Python reads with struct from a match / select image ------------------------------------
uint32_t r32(const Bytes& ram, uint32_t a) { return be32(ram, a & 0x7FFFFF); }
uint32_t r16(const Bytes& ram, uint32_t a) { return be16(ram, a & 0x7FFFFF); }

// CPython 3.10's iteration order of a set of small non-negative ints built by adding `values` in order (setobject.c:
// set_add_entry's linear probes and perturbation, set_table_resize at fill * 5 >= mask * 3 to used * 4, re-inserted in
// table order). rev_fingers sorts such a set by brightness: equal brightnesses keep this order (sorted() is stable).
std::vector<int> py_int_set_order(const std::vector<int>& values) {
    const size_t LINEAR_PROBES = 9;
    std::vector<int> table(8, -1);            // -1: empty (no deletions here, so no dummies)
    size_t fill = 0;
    auto insert_clean = [&](std::vector<int>& t, int key) {
        size_t mask = t.size() - 1, perturb = (size_t)key, i = (size_t)key & mask;
        while (true) {
            if (t[i] < 0) { t[i] = key; return; }
            if (i + LINEAR_PROBES <= mask)
                for (size_t j = 1; j <= LINEAR_PROBES; j++) if (t[i + j] < 0) { t[i + j] = key; return; }
            perturb >>= 5;
            i = (i * 5 + 1 + perturb) & mask;
        }
    };
    for (int key : values) {
        size_t mask = table.size() - 1, perturb = (size_t)key, i = (size_t)key & mask;
        bool found = false; size_t at = 0;
        while (true) {
            size_t probes = (i + LINEAR_PROBES <= mask) ? LINEAR_PROBES : 0, e = i;
            bool stop = false;
            while (true) {
                if (table[e] < 0) { at = e; stop = true; break; }
                if (table[e] == key) { found = true; stop = true; break; }
                e++;
                if (probes-- == 0) break;
            }
            if (stop) break;
            perturb >>= 5;
            i = (i * 5 + 1 + perturb) & mask;
        }
        if (found) continue;
        table[at] = key; fill++;
        if (fill * 5 < mask * 3) continue;
        size_t used = fill, newsize = 8;
        while (newsize <= used * 4) newsize <<= 1;
        std::vector<int> nt(newsize, -1);
        for (int k : table) if (k >= 0) insert_clean(nt, k);
        table.swap(nt);
    }
    std::vector<int> out;
    for (int k : table) if (k >= 0) out.push_back(k);
    return out;
}

// ---- constants (gen/wmport.py) ------------------------------------------------------------------------------------
constexpr uint32_t SKEL_WM_HANDS = 0x262, SKEL_WM_FLAGS = 0x264, SKEL_MAGIC_AT = 0x268, SKEL_MAGIC_WM = 0x574D324B;
constexpr uint32_t SKEL_WM_REV = 0x80000000u, SKEL_WM_NMHANDS = 0x40000000u, SKEL_WM_OWNHANDS = 0x10000000u;
// (c2hands 2026-11-19) The pack's row where the Revenge move animations start (rev_move_anims.h RMA_FIRST - WMP_FILE0): the
// mod is compiled with that number (files.c reads those files as animations), so a pack made without WrestleMania 2000 -- its
// two Superstars, hands and videos left out, every file after them sooner -- keeps the animations there: empty files before
// them up to this row. The full set's pack has them exactly here (checked when it is made: a change to the pack's order is
// a change to this number and to the mod's build).
constexpr size_t RMA_ROW = 0x74B;
constexpr int VIDEO_PORT0 = 58, TV_CARRIERS = 72;
constexpr uint32_t FRAME_SIZE = 4624, NM_PAL_SIZE = 0x200;
constexpr int REV_TRON_RETIRED = 144;
struct Ported { const char* name; int k; };
const Ported PORTED[] = { { "jarrett", 29 }, { "meanie", 41 } };
const int NM_HAND_K[4] = { 9, 10, 17, 18 };
const uint32_t NM_HAND_MESH[4] = { 0x1112, 0x1113, 0x1119, 0x111A };
constexpr uint32_t NM_HAND_PIC_T = 0x296E, NM_HAND_PIC_P = 0x296D;
bool is_hand(int k) { return k == 9 || k == 10 || k == 17 || k == 18; }
// NM_SKIN_PIC: No Mercy's bare-skin pictures (texture, palette) per part
bool nm_skin_pic(int k, uint32_t& t, uint32_t& p) {
    static const std::map<int, std::pair<uint32_t, uint32_t>> M = {
        { 1, { 0x2962, 0x2961 } }, { 2, { 0x2960, 0x295F } }, { 3, { 0x295E, 0x295D } }, { 6, { 0x2972, 0x2971 } },
        { 7, { 0x2970, 0x296F } }, { 8, { 0x2974, 0x2973 } }, { 11, { 0x296C, 0x296B } }, { 12, { 0x296A, 0x2969 } },
        { 13, { 0x2972, 0x2971 } }, { 14, { 0x2970, 0x296F } }, { 15, { 0x2974, 0x2973 } }, { 16, { 0x296C, 0x296B } },
        { 19, { 0x296A, 0x2969 } } };
    auto it = M.find(k); if (it == M.end()) return false;
    t = it->second.first; p = it->second.second; return true;
}
bool skin_keep(int k) { return k == 4 || k == 5 || k == 20; }

// REV_PORTED: (name, its four costumes' captures (tag, slot)), then revbanks.BANKS' second IDs (one capture each, base)
struct RevPort { const char* name; std::vector<std::pair<const char*, int>> caps; const char* base; };
const std::vector<RevPort>& rev_ported() {
    static const std::vector<RevPort> P = {
        { "brethart", { { "L12", 0 }, { "at_bn1", 0 }, { "at_bn2", 0 }, { "at_bn3", 0 } }, nullptr },
        { "neidhart", { { "L12", 1 }, { "at_bn1", 1 }, { "at_bn2", 1 }, { "at_bn3", 1 } }, nullptr },
        { "hogan", { { "hh_c0", 0 }, { "at_hh1", 0 }, { "at_hh2", 0 }, { "at_hh3", 0 } }, nullptr },
        { "nash", { { "hh_c0", 1 }, { "at_hh1", 1 }, { "at_hh2", 1 }, { "at_hh3", 1 } }, nullptr },
        { "hall", { { "ho_c0", 0 }, { "at_ho1", 0 }, { "at_ho2", 0 }, { "at_ho3", 0 } }, nullptr },
        { "savage", { { "sv_c0", 0 }, { "at_sv1", 0 }, { "at_sv2", 0 }, { "at_sv3", 0 } }, nullptr },
        { "hennig", { { "sv_c0", 1 }, { "at_sv1", 1 }, { "at_sv2", 1 }, { "at_sv3", 1 } }, nullptr },
        { "giant", { { "gi_c0", 0 }, { "at_gi1", 0 }, { "at_gi2", 0 }, { "at_gi3", 0 } }, nullptr },
        { "sting", { { "sl_c0", 0 }, { "at_sl1", 0 }, { "at_sl2", 0 }, { "at_sl3", 0 } }, nullptr },
        { "luger", { { "sl_c0", 1 }, { "at_sl1", 1 }, { "at_sl2", 1 }, { "at_sl3", 1 } }, nullptr },
        { "goldberg", { { "gp_c0", 0 }, { "at_gp1", 0 }, { "at_gp2", 0 }, { "at_gp3", 0 } }, nullptr },
        { "ricksteiner", { { "st_c0", 0 }, { "at_st1", 0 }, { "at_st2", 0 }, { "at_st3", 0 } }, nullptr },
        { "scottsteiner", { { "st_c0", 1 }, { "at_st1", 1 }, { "at_st2", 1 }, { "at_st3", 1 } }, nullptr },
        { "booker", { { "hz_c0", 0 }, { "at_hz1", 0 }, { "at_hz2", 0 }, { "at_hz3", 0 } }, nullptr },
        { "stevieray", { { "hz_c0", 1 }, { "at_hz1", 1 }, { "at_hz2", 1 }, { "at_hz3", 1 } }, nullptr },
        { "rey", { { "rb_c0", 0 }, { "at_rb1", 0 }, { "at_rb2", 0 }, { "at_rb3", 0 } }, nullptr },
        { "piper", { { "gp_c0", 1 }, { "at_gp1", 1 }, { "at_gp2", 1 }, { "at_gp3", 1 } }, nullptr },
        { "bischoff", { { "rb_c0", 1 }, { "at_rb1", 1 }, { "at_rb2", 1 }, { "at_rb3", 1 } }, nullptr },
        // gen/revbanks.py BANKS
        { "brethart_b", { { "bret_c1", 0 } }, "brethart" }, { "savage_b", { { "sv_c3", 0 } }, "savage" },
        { "scottsteiner_b", { { "st_c2", 1 } }, "scottsteiner" }, { "booker_b", { { "hz_c1", 0 } }, "booker" },
        { "stevieray_b", { { "hz_c1", 1 } }, "stevieray" }, { "rey_b", { { "rb_c1", 0 } }, "rey" } };
    return P;
}
bool in_rev_ported(const std::string& who) {
    for (auto& p : rev_ported()) if (who == p.name) return true;
    return false;
}

// ---- pictures (wmport.py ci8 / mirror / rev_fingers) -----------------------------------------------------------------
std::pair<Bytes, Bytes> ci8(const Bytes& tex, const Bytes& pal) {
    if (tex[2] == 1) return { tex, pal };
    int w = tex[0] + 1, h = tex[1] + 1;
    Bytes t(tex.begin(), tex.begin() + 2);
    t.push_back(1); t.push_back(0);
    t.insert(t.end(), tex.begin() + 4, tex.begin() + 8);
    for (int i = 0; i < w * h / 2; i++) { uint8_t b = tex[8 + i]; t.push_back(b >> 4); t.push_back(b & 15); }
    Bytes p(pal.begin(), pal.begin() + std::min<size_t>(512, pal.size()));
    p.resize(512, 0);
    return { t, p };
}
Bytes mirror(const Bytes& tex) {
    int w = tex[0] + 1, h = tex[1] + 1;
    Bytes out(tex.begin(), tex.begin() + 8);
    for (int y = 0; y < h; y++)
        for (int x = w - 1; x >= 0; x--) {
            size_t i = 8 + (size_t)y * w + x;
            if (i < tex.size()) out.push_back(tex[i]);       // (Python slices: a short picture gives what there is)
        }
    return out;
}
struct C3i { int r, g, b; };
std::vector<C3i> colors(const Bytes& pal) {
    std::vector<C3i> out;
    for (size_t i = 0; i < pal.size() / 2; i++) {
        uint32_t v = be16(pal, 2 * i);
        out.push_back({ (int)((v >> 11) << 3), (int)(((v >> 6) & 31) << 3), (int)(((v >> 1) & 31) << 3) });
    }
    return out;
}
double lum(const C3i& c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; }
Bytes rev_fingers(const Bytes& wm_tex, const Bytes& wm_pal, const Bytes& palm_tex, const Bytes& palm_pal) {
    int w = wm_tex[0] + 1, h = wm_tex[1] + 1;
    std::vector<int> wpx;
    for (int i = 0; i < w * h && 8 + i < (int)wm_tex.size(); i++) wpx.push_back(wm_tex[8 + i]);
    auto wc = colors(wm_pal), pc = colors(palm_pal);
    int pw = palm_tex[0] + 1, ph = palm_tex[1] + 1;
    struct PL { double l; C3i c; };
    std::vector<PL> palm;
    for (int i = 0; i < pw * ph && 8 + i < (int)palm_tex.size(); i++) { const C3i& c = pc[palm_tex[8 + i]]; palm.push_back({ lum(c), c }); }
    std::sort(palm.begin(), palm.end(), [](const PL& a, const PL& b) {
        if (a.l != b.l) return a.l < b.l;
        if (a.c.r != b.c.r) return a.c.r < b.c.r;
        if (a.c.g != b.c.g) return a.c.g < b.c.g;
        return a.c.b < b.c.b; });
    std::vector<int> order = py_int_set_order(wpx);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return lum(wc[a]) < lum(wc[b]); });
    std::map<int, int> count;
    for (int i : order) count[i] = 0;
    for (int i : wpx) count[i]++;
    Bytes np = wm_pal;
    int seen = 0;
    for (int i : order) {
        double mid = (seen + count[i] / 2.0) / (double)wpx.size(); seen += count[i];
        size_t at = std::min(palm.size() - 1, (size_t)(int64_t)(mid * (double)palm.size()));
        const C3i& c = palm[at].c;
        put_be16(&np[2 * i], ((c.r >> 3) << 11) | ((c.g >> 3) << 6) | ((c.b >> 3) << 1) | 1);
    }
    return np;
}

// ---- No Mercy's hands and skin (wmport.py nm_skin) -----------------------------------------------------------------
Bytes bake(const Bytes& pal, int row) {             // _bake: palette row `row` copied over the first (a Python slice: may be short)
    Bytes b = pal;
    size_t a = std::min(pal.size(), (size_t)(0x20 * row)), e = std::min(pal.size(), (size_t)(0x20 * row + 0x20));
    Bytes r(pal.begin() + a, pal.begin() + e);
    size_t first = std::min<size_t>(0x20, b.size());
    b.erase(b.begin(), b.begin() + first);
    b.insert(b.begin(), r.begin(), r.end());
    return b;
}
std::vector<int> pix(const Bytes& tex) { return tex_pixels(tex); }
bool skin(const C3i& c) { return wm_skin(c.r, c.g, c.b); }
std::array<double, 3> tone_avg(WmCtx& C, int row) {
    int64_t acc[3] = { 0, 0, 0 }; int64_t n = 0;
    const uint32_t pics[3][2] = { { 0x296C, 0x296B }, { 0x296A, 0x2969 }, { 0x296E, 0x296D } };
    for (auto& tp : pics) {
        const Bytes& tex = C.nmf(tp[0]); Bytes pal = bake(C.nmf(tp[1]), row);
        auto cs = colors(Bytes(pal.begin(), pal.begin() + 32));
        for (int i : pix(tex)) { acc[0] += cs[i].r; acc[1] += cs[i].g; acc[2] += cs[i].b; n++; }
    }
    return { (double)acc[0] / n, (double)acc[1] / n, (double)acc[2] / n };
}
bool opaque(const Bytes& tex, const Bytes& pal) {
    size_t n = pal.size() / 2;
    for (int i : pix(tex)) if (!((size_t)i < n && (be16(pal, 2 * i) & 1))) return false;
    return true;
}
struct SkinState { std::map<std::string, std::array<double, 3>> tone; std::map<std::string, std::vector<int>> bare; };
Bytes nm_skin(WmCtx& C, const Bytes& model, const std::string& name, SkinState& skins, const std::string& who) {
    Bytes d = model;
    auto data = [&](uint32_t i) -> const Bytes& { return i >= WMP_FILE0 ? C.F.data[i - WMP_FILE0] : C.nmf(i); };
    struct P { Bytes tex, pal; std::vector<C3i> cs; double frac; std::array<double, 3> avg; int n; };
    std::map<int, P> parts;                       // (k ascending: the Python dict's order)
    for (int k = 1; k < 20; k++) {
        uint32_t t = be16(d, 8 * k + 2), q = be16(d, 8 * k + 4);
        if (!t || skin_keep(k)) continue;
        P p; p.tex = data(t); p.pal = data(q); p.cs = colors(p.pal);
        auto px = pix(p.tex);
        std::vector<int> sk;
        for (int i : px) if ((size_t)i < p.cs.size() && skin(p.cs[i])) sk.push_back(i);
        p.frac = (double)sk.size() / (double)std::max<size_t>(1, px.size());
        for (int j = 0; j < 3; j++) {
            int64_t s = 0;
            for (int i : sk) s += j == 0 ? p.cs[i].r : j == 1 ? p.cs[i].g : p.cs[i].b;
            p.avg[j] = (double)s / (double)std::max<size_t>(1, sk.size());
        }
        p.n = (int)sk.size();
        parts[k] = std::move(p);
    }
    std::vector<int> full, src;
    for (auto& [k, p] : parts) if (p.frac >= 0.95 && !is_hand(k)) full.push_back(k);
    if (!full.empty()) src = full;
    else for (auto& [k, p] : parts) if (p.frac >= 0.5) src.push_back(k);
    std::optional<std::array<double, 3>> keep;
    if (in_rev_ported(who)) {
        auto b = skins.bare.find(who);
        if (b == skins.bare.end()) skins.bare[who] = py_int_set_order(full);   // (set(full): kept in its own order)
        else {
            std::vector<int> bare;
            for (int k : b->second) { auto it = parts.find(k); if (it != parts.end() && it->second.frac >= 0.95) bare.push_back(k); }
            if (!bare.empty()) src = bare;
            else if (skins.tone.count(who)) keep = skins.tone[who];
        }
    }
    int64_t tot = 0;
    for (int k : src) tot += parts[k].n;
    std::array<double, 3> have;
    if (keep) have = *keep;
    else if (tot) {
        for (int j = 0; j < 3; j++) {
            double s = 0.0; bool first = true;            // (sum() of floats: 0 + a + b ..., left to right)
            for (int k : src) { double v = parts[k].avg[j] * parts[k].n; s = first ? 0.0 + v : s + v; first = false; }
            have[j] = s / (double)tot;
        }
        if (!skins.tone.count(who)) skins.tone[who] = have;
    } else if (skins.tone.count(who)) have = skins.tone[who];
    else fail("%s: no bare skin in any costume so far (its skin tone unknown)", name.c_str());
    std::array<double, 3> tones[8];
    for (int r = 0; r < 8; r++) tones[r] = tone_avg(C, r);
    int row = 0; double best = 0;
    for (int r = 0; r < 8; r++) {
        double s = 0.0;
        for (int j = 0; j < 3; j++) { double e = tones[r][j] - have[j]; s += e * e; }
        if (r == 0 || s < best) { best = s; row = r; }
    }
    const auto& tone = tones[row];
    double gain[3];
    for (int j = 0; j < 3; j++) gain[j] = tone[j] / std::max(1.0, have[j]);
    for (int h = 0; h < 4; h++) {                                         // the hands
        auto tq = C.F.pair(C.nmf(NM_HAND_PIC_T), bake(C.nmf(NM_HAND_PIC_P), row));
        put_be16(&d[8 * NM_HAND_K[h]], NM_HAND_MESH[h]); put_be16(&d[8 * NM_HAND_K[h] + 2], tq.first); put_be16(&d[8 * NM_HAND_K[h] + 4], tq.second);
    }
    for (auto& [k, p] : parts) {
        if (is_hand(k) || p.frac < 0.3) continue;
        uint32_t m = be16(d, 8 * k);
        uint32_t nt, np;
        std::pair<uint32_t, uint32_t> tq;
        if (p.frac >= 0.95 && nm_skin_pic(k, nt, np) && C.nmf(nt)[0] == p.tex[0] && C.nmf(nt)[1] == p.tex[1] &&
            opaque(C.nmf(nt), bake(C.nmf(np), row)))
            tq = C.F.pair(C.nmf(nt), bake(C.nmf(np), row));
        else {
            Bytes pal = p.pal;
            for (size_t i = 0; i < p.cs.size(); i++) {
                if (!skin(p.cs[i])) continue;
                uint32_t v = be16(pal, 2 * i);
                int c3[3] = { p.cs[i].r, p.cs[i].g, p.cs[i].b }, o[3];
                for (int j = 0; j < 3; j++) o[j] = std::min(255, (int)(int64_t)(c3[j] * gain[j]));
                put_be16(&pal[2 * i], ((o[0] >> 3) << 11) | ((o[1] >> 3) << 6) | ((o[2] >> 3) << 1) | (v & 1));
            }
            tq = C.F.pair(p.tex, pal);
        }
        put_be16(&d[8 * k], m); put_be16(&d[8 * k + 2], tq.first); put_be16(&d[8 * k + 4], tq.second);
    }
    put_be32(&d[SKEL_WM_FLAGS], be32(d, SKEL_WM_FLAGS) | SKEL_WM_NMHANDS);
    return d;
}

// ---- WrestleMania 2000's (wm2k.py roster / wmport.py wm_video) ------------------------------------------------------
struct WmRec { uint32_t video; uint32_t cost[4]; };
WmRec wm_roster(const Bytes& ram, int k) {
    uint32_t rec = r32(ram, 0x800411B0 + 4 * k);
    WmRec r;
    r.video = ram[(rec + 4 + 13) & 0x7FFFFF];             // (">HHHHHHBB" at rec + 4: music, video the last two bytes)
    for (int i = 0; i < 4; i++) r.cost[i] = r32(ram, rec + 0x14 + 4 * i);
    return r;
}

// ---- a Revenge port's descriptor (wmport.py rev_port_desc): build_all's loop and mkportphotos (port_photo_models) share it
// got: his capture (rev_capture of his tag and slot); his files go into F in the capture's part order, each part's (picture,
// palette) pair numbered before its mesh (the Python's evaluation order); the hands made WM2000's (hands.c): the palms'
// pictures CI8 and mirrored, the fingers WM2000's (wm_ref: Jarrett's costume 0) in his palm's colours. (c2hands 2026-11-19)
// wm_ref null -- no WrestleMania 2000: Revenge's own hands, his palms' and fingers' pictures as captured, drawn on his own hand
// meshes as modelled (SKEL_WM_OWNHANDS: make_wmport, for a model that keeps them).
Bytes rev_port_desc(const std::map<int, RevPart>& got, Pool& F, const std::map<int, std::pair<Bytes, Bytes>>* wm_ref,
                    const RomFiles& rev) {
    Bytes nm(0x270, 0);
    for (auto& [k, part] : got) {
        std::optional<Bytes> tex = part.tex, pal = part.pal;
        if (wm_ref && is_hand(k) && tex) {
            auto tp8 = ci8(*tex, *pal); tex = tp8.first; pal = tp8.second;
            if (k == 9 || k == 17) tex = mirror(*tex);
            if (k == 10 || k == 18) {
                auto& palm = got.at(k - 1);
                auto pp = ci8(*palm.tex, *palm.pal);
                auto& ref = wm_ref->at(k);
                tex = ref.first; pal = rev_fingers(ref.first, ref.second, pp.first, pp.second);
            }
        }
        std::pair<uint32_t, uint32_t> tq{ 0, 0 };
        if (tex) tq = F.pair(*tex, *pal);
        uint32_t m = F.one(rev(part.mesh));
        put_be16(&nm[8 * k], m); put_be16(&nm[8 * k + 2], tq.first); put_be16(&nm[8 * k + 4], tq.second);
    }
    put_be32(&nm[SKEL_WM_FLAGS], SKEL_WM_REV);
    nm[SKEL_WM_HANDS] = 0;
    put_be32(&nm[SKEL_MAGIC_AT], SKEL_MAGIC_WM);
    return nm;
}

// WrestleMania 2000's fingers (wmport.py build_all's wm_ref): Jarrett's costume 0 parts' (picture, palette)
std::map<int, std::pair<Bytes, Bytes>> wm_fingers_ref(const Bytes& jarrett_c0) {
    std::map<int, std::pair<Bytes, Bytes>> wm_ref;
    for (auto& p : wm_capture(jarrett_c0, 0).parts) if (p.tex && p.pal) wm_ref[p.k] = { *p.tex, *p.pal };
    return wm_ref;
}

}  // namespace

// ---- tools\romrender\mkportphotos.py's models (see ri_wmport.h) --------------------------------------------------------
std::vector<PortModel> port_photo_models(Roms& R, const RiData& D) {
    const RomFiles* wm = R.files("wm2000");
    const RomFiles* rev = R.files("revenge");
    // (arenasplit 2026-11-24) either game: Revenge's ports with Revenge, Jarrett and the Meanie with WrestleMania 2000
    if (!rev && !wm) fail("portphotos: WCW/nWo Revenge or WrestleMania 2000 is needed");
    // (c2hands 2026-11-19) without WrestleMania 2000: the Revenge ports alone, with Revenge's own hands (rev_port_desc)
    std::map<std::string, Bytes> wcap;                       // WrestleMania 2000's Superstar Select captures (costume 0 used)
    if (wm) wm_models(R, D, "wmport", [&](const std::string& rel, const Bytes& ram) { wcap[rel] = ram; });
    auto cap = [&](int k) -> const Bytes& {
        auto it = wcap.find(strf("wmport/cap/k%d_c0/ram.bin", k));
        if (it == wcap.end()) fail("portphotos: no capture of WrestleMania 2000's %d costume 0", k);
        return it->second;
    };
    std::optional<std::map<int, std::pair<Bytes, Bytes>>> wm_ref;
    if (wm) wm_ref = wm_fingers_ref(cap(PORTED[0].k));
    std::map<std::string, RevLoad> rcap;                     // Revenge's match loads, by tag
    if (rev) rev_port_models(R, D, [&](const std::string& rel, const RevLoad& l) {
        std::string b = rel.substr(rel.find_last_of('/') + 1);
        rcap[b.substr(0, b.size() - 8)] = l;                 // ("<tag>_ram.bin")
    });
    std::vector<PortModel> out;
    // the Revenge ports, REV_PORTED's order -- not revbanks' second IDs (mkportphotos: `if n not in W.revbanks.BY`)
    if (rev) for (auto& P : rev_ported()) {
        if (P.base) continue;
        auto& cp = P.caps[0];
        auto it = rcap.find(cp.first);
        if (it == rcap.end()) fail("portphotos: no Revenge capture %s", cp.first);
        PortModel M{ P.name, {}, {} };                       // (W.Files() per man: his own numbering from 0x5000)
        M.desc = rev_port_desc(rev_capture(it->second.ram, it->second.calls, cp.second), M.files, wm_ref ? &*wm_ref : nullptr, *rev);
        out.push_back(std::move(M));
    }
    if (!wm) return out;
    // WrestleMania 2000's two (mkportphotos.wm_port_desc: build_all's PORTED loop but no hands' kind, the SKEL magic set;
    // a part's pair numbered before its mesh, as that function evaluates them)
    for (auto& P : PORTED) {
        PortModel M{ P.name, {}, {} };
        WmCapture w = wm_capture(cap(P.k), 0);
        Bytes nm = nm_descriptor(w.desc);
        for (int o = 8; o < 0xB0; o += 8) std::fill(nm.begin() + o, nm.begin() + o + 6, 0);
        for (int o = 0xC8; o < 0xE0; o += 8) std::fill(nm.begin() + o, nm.begin() + o + 6, 0);
        for (auto& p : w.parts) {
            int o = p.k <= 20 ? p.at : 0xC8 + 8 * (p.k - 22);
            std::pair<uint32_t, uint32_t> tq{ 0, 0 };
            if (p.tex) { if (!p.pal) fail("portphotos: a picture without its palette"); tq = M.files.pair(*p.tex, *p.pal); }
            uint32_t m = M.files.one((*wm)(p.mesh));
            put_be16(&nm[o], m); put_be16(&nm[o + 2], tq.first); put_be16(&nm[o + 4], tq.second);
        }
        put_be32(&nm[SKEL_MAGIC_AT], SKEL_MAGIC_WM);
        M.desc = std::move(nm);
        out.push_back(std::move(M));
    }
    return out;
}

WmportOut make_wmport(Roms& R, const RiData& D, const WmportIn& in) {
    WmCtx C{ R, D };
    C.nm = R.files("nomercy"); C.wm = R.files("wm2000"); C.rev = R.files("revenge");
    C.shape = in.shape;
    // (arenasplit 2026-11-24) No Mercy and either of WCW/nWo Revenge and WrestleMania 2000: each gives its own part of the pack
    if (!C.nm || (!C.rev && !C.wm)) fail("wmport: No Mercy (Rev 1) and WCW/nWo Revenge or WrestleMania 2000 are needed");
    // (c2hands 2026-11-19) Without WrestleMania 2000 the pack is Revenge's alone: no Jarrett / Meanie, no WM2000 hands or
    // videos; the Revenge ports with Revenge's own hands; the move animations at the build's row (RMA_ROW); Revenge's arenas
    // (arenasplit 2026-11-24: WrestleMania 2000's rows zero). Without Revenge (arenasplit) it is WrestleMania 2000's alone:
    // Jarrett, the Meanie, its hands, videos and arenas (Revenge's rows zero); no Revenge ports, no move animations. The
    // game's own arenas' stage sets -- the pack's last files, at the build's places -- come from content\wmport_base.bin,
    // which the game reads under this shorter pack (arcade_packs.cpp).
    C.nm_rom = *R.rom("nomercy");
    Pool& F = C.F;
    std::vector<std::pair<std::pair<std::string, int>, Bytes>> models;
    auto set_model = [&](const std::string& n, int c, const Bytes& b) {
        for (auto& m : models) if (m.first.first == n && m.first.second == c) { m.second = b; return; }
        models.push_back({ { n, c }, b });
    };

    // the ported Superstars' captures (WrestleMania 2000's Superstar Select, ri_models "wmport")
    std::map<std::string, Bytes> wcap;
    if (C.wm) wm_models(R, D, "wmport", [&](const std::string& rel, const Bytes& ram) { wcap[rel] = ram; });
    auto cap = [&](int k, int c) -> const Bytes& {
        auto it = wcap.find(strf("wmport/cap/k%d_c%d/ram.bin", k, c));
        if (it == wcap.end()) fail("wmport: no capture of WrestleMania 2000's %d costume %d", k, c);
        return it->second;
    };
    uint32_t odd[2][11] = {}, even[2][3][4] = {};
    std::pair<uint32_t, uint32_t> pics[4] = {}, special[4] = {}, decal{ 0, 0 };
    struct Video { std::string name; uint32_t wv; std::vector<std::pair<int, uint32_t>> lst; uint32_t back; std::vector<uint32_t> ids; };
    std::vector<Video> videos;
    std::vector<std::pair<std::string, int>> video_of;
    std::optional<std::map<int, std::pair<Bytes, Bytes>>> wm_ref;
    if (C.wm) {
        const Bytes& wram = C.image("wm2k_ram");
        const Bytes& match = C.image("wm_match");
        for (auto& P : PORTED)
            for (int c = 0; c < 4; c++) {
                WmCapture w = wm_capture(cap(P.k, c), 0);
                Bytes nm = nm_descriptor(w.desc);
                for (int o = 8; o < 0xB0; o += 8) std::fill(nm.begin() + o, nm.begin() + o + 6, 0);
                for (int o = 0xC8; o < 0xE0; o += 8) std::fill(nm.begin() + o, nm.begin() + o + 6, 0);
                for (auto& p : w.parts) {
                    int o = p.k <= 20 ? p.at : 0xC8 + 8 * (p.k - 22);
                    uint32_t m = F.one(C.wmf(p.mesh));
                    std::pair<uint32_t, uint32_t> tq{ 0, 0 };
                    if (p.tex) { if (!p.pal) fail("wmport: a picture without its palette"); tq = F.pair(*p.tex, *p.pal); }
                    put_be16(&nm[o], m); put_be16(&nm[o + 2], tq.first); put_be16(&nm[o + 4], tq.second);
                }
                WmRec rec = wm_roster(wram, P.k);
                uint8_t looks0 = wram[rec.cost[c] & 0x7FFFFF];
                nm[SKEL_WM_HANDS] = match[(0x41CC4 + looks0) & 0x7FFFFF];
                set_model(P.name, c, nm);
            }
        C.log.push_back(strf("part %zu wm2000 ported models (Jarrett, Meanie)", F.data.size()));
        // WrestleMania 2000's hands (hands.c): its tables in its match RAM
        for (int sd = 0; sd < 2; sd++)
            for (int p = 0; p < 11; p++) odd[sd][p] = F.one(C.wmf(r16(match, r32(match, 0x80041C5C + 4 * sd) + 2 * p)));
        for (int sd = 0; sd < 2; sd++)
            for (int t = 0; t < 3; t++)
                for (int p = 0; p < 4; p++) even[sd][t][p] = F.one(C.wmf(r16(match, r32(match, r32(match, 0x80041CAC + 4 * sd) + 4 * t) + 2 * p)));
        auto tp = [&](uint32_t i) { return F.pair(C.wmf(i + 1), C.wmf(i)); };
        for (int i = 0; i < 4; i++) pics[i] = tp(r16(match, 0x80041CB4 + 2 * i));
        for (int i = 0; i < 4; i++) special[i] = tp(r16(match, 0x80041CBC + 2 * i));
        decal = F.pair(C.wmf(0x122E), C.wmf(0x122D));
        C.log.push_back(strf("part %zu wm2000 hands", F.data.size()));
        // their videos (WrestleMania 2000's entrance table)
        const Bytes& entr = C.image("wm2k_entr");
        for (auto& P : PORTED) {
            uint32_t wv = wm_roster(wram, P.k).video;
            if (!wv) continue;
            std::vector<std::pair<uint32_t, uint32_t>> lst;
            uint32_t a = r32(entr, 0x80102070 + 12 * wv);
            while (r16(entr, a) != 0xFFFF) { lst.push_back({ r16(entr, a), r16(entr, a + 2) }); a += 4; if (lst.size() >= 256) fail("wm_video %u", wv); }
            uint32_t back = r16(entr, a + 2);
            std::vector<uint32_t> frames;
            for (auto& [f, t] : lst) if (std::find(frames.begin(), frames.end(), f) == frames.end()) frames.push_back(f);
            if ((int)frames.size() > TV_CARRIERS) fail("wmport: %s's video has %zu frames", P.name, frames.size());
            Video V{ P.name, wv, {}, back, {} };
            for (uint32_t f : frames) {
                const Bytes& d = C.wmf(f);
                if (d.size() != FRAME_SIZE || memcmp(d.data(), "TEX", 3)) fail("wmport: %s's frame 0x%X", P.name, f);
                V.ids.push_back(F.one(d));
            }
            for (auto& [f, t] : lst) V.lst.push_back({ (int)(std::find(frames.begin(), frames.end(), f) - frames.begin()), t });
            video_of.push_back({ P.name, VIDEO_PORT0 + (int)videos.size() });
            videos.push_back(V);
        }
        C.log.push_back(strf("part %zu wm2000 videos", F.data.size()));
        wm_ref = wm_fingers_ref(cap(PORTED[0].k, 0));           // WM2000's fingers: Jarrett's costume 0
    } else C.log.push_back("no WrestleMania 2000: its two Superstars, hands and videos left out; Revenge's own hands");
    for (int i = 0; i < REV_TRON_RETIRED; i++) F.data.push_back(Bytes());   // (Bret's / Neidhart's retired frames: empty)

    C.log.push_back(strf("part %zu retired Titantron frames (empty)", F.data.size()));
    // Revenge's built-in ports (its match loads, ri_models "revports") -- (arenasplit) only with Revenge
    std::map<std::string, RevLoad> rcap;
    if (C.rev) rev_port_models(R, D, [&](const std::string& rel, const RevLoad& l) {
        std::string b = rel.substr(rel.find_last_of('/') + 1);
        rcap[b.substr(0, b.size() - 8)] = l;                 // ("<tag>_ram.bin")
    });
    auto rev_cap = [&](const std::string& tag, int slot) {
        auto it = rcap.find(tag);
        if (it == rcap.end()) fail("wmport: no Revenge capture %s", tag.c_str());
        return rev_capture(it->second.ram, it->second.calls, slot);
    };
    if (C.rev) for (auto& P : rev_ported())
        for (int c = 0; c < 4; c++) {
            auto& cp = P.caps[c % P.caps.size()];
            set_model(P.name, c, rev_port_desc(rev_cap(cp.first, cp.second), F, wm_ref ? &*wm_ref : nullptr, *C.rev));
        }
    C.log.push_back(strf("part %zu revenge built-in ports", F.data.size()));
    // (no2k15, 2026-11-23) The two WWE 2K15 test slots' stand-in models (Goldberg's parts, No Mercy's hands: src/hd.c drew
    // their full-detail models over them) are removed with the slots. The one pair of files they were the first to make -- No
    // Mercy's hand picture, its palette baked (3) -- stays at its rows: the Revenge ports' No Mercy hands below use it, and so
    // every file after it keeps its number (wmport.bin and wmport_files.h as before; gen/wmport.py the same).
    F.pair(C.nmf(NM_HAND_PIC_T), bake(C.nmf(NM_HAND_PIC_P), 3));
    C.log.push_back(strf("part %zu nomercy hand picture (the ports' hands)", F.data.size()));
    // No Mercy's hands and skin on each (not REV_SKIN's: Revenge's own skin and hands)
    {
        SkinState skins;
        for (auto& m : models) {
            const std::string& n = m.first.first;
            if (n == "goldberg" || n == "nash") continue;
            std::string who = n;
            for (auto& P : rev_ported()) if (n == P.name && P.base) who = P.base;
            m.second = nm_skin(C, m.second, strf("%s %d", n.c_str(), m.first.second), skins, who);
        }
        // (c2hands) without WrestleMania 2000, a Revenge model that keeps Revenge's hands (REV_SKIN's) keeps them as modelled
        if (!C.wm)
            for (auto& m : models) {
                const uint32_t fl = be32(m.second, SKEL_WM_FLAGS);
                if ((fl & SKEL_WM_REV) && !(fl & SKEL_WM_NMHANDS)) put_be32(&m.second[SKEL_WM_FLAGS], fl | SKEL_WM_OWNHANDS);
            }
    }
    C.log.push_back(strf("part %zu nomercy hands / skin pictures", F.data.size()));
    // Revenge's moves' animations (movex.c): their numbers (and with_rma their bytes; else empty files)
    std::vector<std::pair<std::string, uint32_t>> rev_anims;
    if (!in.rev_anims_json.empty()) {
        // (c2hands) the animations at the build's row: the full set's files come exactly to it; without WrestleMania 2000, fewer
        // (empty files up to it)
        if (F.data.size() > RMA_ROW || (C.wm && F.data.size() != RMA_ROW))
            fail("wmport: the move animations would start at row 0x%zX, not the build's 0x%zX (RMA_ROW)", F.data.size(), RMA_ROW);
        while (F.data.size() < RMA_ROW) F.data.push_back(Bytes());
        ojson J = ojson::parse(in.rev_anims_json);
        for (auto& f : J["files"]) {
            std::string src = f.contains("src") && f["src"].is_string() ? f["src"].get<std::string>() : "";
            if (src == "nm" || src == "join") fail("wmport: rev_anims.json's %s pairs aren't ported", src.c_str());
            std::string name = f["name"].get<std::string>();
            uint32_t id = WMP_FILE0 + (uint32_t)F.data.size();
            bool had = false;
            for (auto& r : rev_anims) if (r.first == name) { r.second = id; had = true; }
            if (!had) rev_anims.push_back({ name, id });
            if (!in.with_rma) { F.data.push_back(Bytes()); F.data.push_back(Bytes()); continue; }
            int64_t fa = f["fa"].get<int64_t>(), fd = f["fd"].get<int64_t>();
            const RomFiles* src_files = fa > 0 ? C.rev : C.nm;
            Bytes da = (*src_files)((uint32_t)std::llabs(fa)), dd = (*src_files)((uint32_t)std::llabs(fd));
            if (f.contains("b0a") && !f["b0a"].is_null()) da[0] = (uint8_t)f["b0a"].get<int>();
            if (f.contains("b0d") && !f["b0d"].is_null()) dd[0] = (uint8_t)f["b0d"].get<int>();
            F.data.push_back(da); F.data.push_back(dd);
        }
    }
    C.log.push_back(strf("part %zu revenge move animations (empty unless with_rma)", F.data.size()));
    // the arenas (ri_wmarenas.cpp): (arenasplit 2026-11-24) each source game's own -- with one of the two, its arenas alone
    wm_arenas(C);
    // every palette a model names made No Mercy's size
    {
        std::set<uint32_t> pals;
        for (auto& m : models)
            for (int k = 1; k < 28; k++) {
                int o = k <= 20 ? 8 * k : 0xA8 + 8 * (k - 21);
                uint32_t q = be16(m.second, o + 4);
                if (q >= WMP_FILE0 && q - WMP_FILE0 < F.data.size()) pals.insert(q - WMP_FILE0);
            }
        for (uint32_t i : pals) if (F.data[i].size() < NM_PAL_SIZE) F.data[i].resize(NM_PAL_SIZE, 0);
    }
    WmportOut out;
    std::vector<std::pair<size_t, size_t>> offs;
    for (auto& b : F.data) {
        offs.push_back({ out.pack.size(), b.size() });
        out.pack.insert(out.pack.end(), b.begin(), b.end());
        out.pack.resize(out.pack.size() + ((8 - b.size() % 8) % 8), 0);
    }
    std::string L = "// generated by gen/wmport.py: WrestleMania 2000's Superstars' files (build/wmport.bin, in the mod)\n";
    L += strf("#define WMP_FILE0 0x%Xu\n#define WMP_FILES_N %zu\n#define WMP_PACK_SIZE 0x%zXu\n", WMP_FILE0, F.data.size(), out.pack.size());
    L += "static const u32 wmp_files[WMP_FILES_N][2] = {   // offset in the pack, size; file WMP_FILE0 + i\n";
    for (auto& o : offs) L += strf("    { 0x%06zX, %zu },\n", o.first, o.second);
    L += "};\n";
    {
        std::string H = "// generated by gen/wmport.py: Revenge's moves' animations (movex.c): the attacker's file (the defender's the next)\n";
        uint32_t first = 0;
        for (auto& r : rev_anims) first = first ? std::min(first, r.second) : r.second;
        H += strf("#define RMA_FIRST 0x%Xu   // (the pairs one after another from here, gen/revanims.py's order)\n", first);
        H += strf("#define RMA_FILES_N %zu\n", 2 * rev_anims.size());
        for (auto& r : rev_anims) H += strf("#define RMA_%s 0x%Xu\n", r.first.c_str(), r.second);
        C.header("rev_move_anims.h", H);
    }
    auto join_hex = [](const uint32_t* v, int n) { std::string s; for (int i = 0; i < n; i++) s += (i ? ", " : "") + strf("0x%X", v[i]); return s; };
    // (c2hands 2026-11-19) without WrestleMania 2000 its hands' and videos' tables aren't written: their holes are zero (the
    // fill's rule for a source game that isn't there), and the mod leaves them out (hands.c wm_hands_there; their videos)
    if (C.wm) {
        L += "// WrestleMania 2000's hands (hands.c): pose meshes [side][pose], [side][kind][pose]; pictures (tex, pal)\n";
        L += strf("static const u16 wmh_odd_files[2][11] = { { %s }, { %s } };\n", join_hex(odd[0], 11).c_str(), join_hex(odd[1], 11).c_str());
        L += "static const u16 wmh_even_files[2][3][4] = { ";
        for (int sd = 0; sd < 2; sd++) {
            L += sd ? ", { " : "{ ";
            for (int t = 0; t < 3; t++) L += (t ? ", { " : "{ ") + join_hex(even[sd][t], 4) + " }";
            L += " }";
        }
        L += " };\n";
        auto pairs4 = [](const std::pair<uint32_t, uint32_t>* p) { std::string s; for (int i = 0; i < 4; i++) s += (i ? ", " : "") + strf("{ 0x%X, 0x%X }", p[i].first, p[i].second); return s; };
        L += strf("static const u16 wmh_pic_files[4][2] = { %s };\n", pairs4(pics).c_str());
        L += strf("static const u16 wmh_special_files[4][2] = { %s };\n", pairs4(special).c_str());
        L += strf("static const u16 wmh_decal_files[2] = { 0x%X, 0x%X };\n", decal.first, decal.second);
        // (handsrom 2026-11-25) WrestleMania 2000's own table of its hands' poses -- the pose mesh each hand piece takes in each
        // of the 20 poses: rows at its RAM's 0x80041D0C, 0x80041CE4, 0x80041CF8, 0x80041CD0 (ROM 0x4290C, 0x428E4, 0x428F8,
        // 0x428D0; read from its RAM image, loadmaps\wm2k_ram.json). It was written in hands.c (wm_pose, a hole the importer
        // filled from "hands.c"'s text); now it is made here, with the other tables of its hands, so our source holds none of it.
        {
            const Bytes& img = C.image("wm2k_ram");
            L += "// WrestleMania 2000's hand poses (hands.c): the pose mesh of hand piece [i] in pose [p] (its RAM 0x80041D0C, 0x80041CE4, 0x80041CF8, 0x80041CD0)\n";
            L += "static const s8 wmh_pose[4][20] = { ";
            int row = 0;
            for (uint32_t a : { 0x41D0Cu, 0x41CE4u, 0x41CF8u, 0x41CD0u }) {
                L += row++ ? ", { " : "{ ";
                for (int k = 0; k < 20; k++) L += strf(k ? ", %d" : "%d", (int)(int8_t)img[a + k]);
                L += " }";
            }
            L += " };\n";
        }
        size_t vlen = 0, vfr = 0;
        for (auto& v : videos) { vlen = std::max(vlen, v.lst.size() + 1); vfr = std::max(vfr, v.ids.size()); }
        L += "// Their Titantron videos (media.c): video WMP_VIDEO0 + i: (frame k, time) ..., (0xFFFF, the entry to go back\n";
        L += "// to); frame k of it the file wmp_video_frames[i][k] (64 x 64 CI8 TEX)\n";
        L += strf("#define WMP_VIDEO0 %d\n#define WMP_VIDEOS_N %zu\n#define WMP_VIDEO_LEN %zu\n#define WMP_VIDEO_FRAMES %zu\n", VIDEO_PORT0, videos.size(), vlen, vfr);
        L += "static const u16 wmp_video[WMP_VIDEOS_N][WMP_VIDEO_LEN][2] = {\n";
        for (auto& v : videos) {
            std::string e;
            for (auto& [k, t] : v.lst) e += (e.empty() ? "" : ", ") + strf("{ %d, 0x%X }", k, t);
            e += (e.empty() ? "" : ", ") + strf("{ %d, 0x%X }", 0xFFFF, v.back);
            L += strf("    { %s },   // %s (WM2000's video %u)\n", e.c_str(), v.name.c_str(), v.wv);
        }
        L += "};\nstatic const u16 wmp_video_frames[WMP_VIDEOS_N][WMP_VIDEO_FRAMES] = {\n";
        for (auto& v : videos) L += strf("    { %s },   // %s\n", join_hex(v.ids.data(), (int)v.ids.size()).c_str(), v.name.c_str());
        L += "};\n";
    }
    C.header("wmport_files.h", L);
    // the walls (gen/arenas_walls.py, from gen/arenas_collide.py's results on this pack's stage sets)
    std::map<int, std::string> res = in.collide_results;
    if (in.collide) {                             // (the stage sets' collisions: the arenas' -- arenasplit: whichever are made)
        std::map<std::string, std::string> hs;
        for (auto& h : C.headers) hs[h.first] = h.second;
        res = in.collide(out.pack, hs);
    }
    if (!res.empty()) wm_walls_headers(C, res);
    out.collide_json = res;
    for (auto& h : C.headers) out.headers.push_back({ h.first, crlf(h.second) });
    out.models = models;
    out.videos = video_of;
    out.log = C.log;
    return out;
}

}  // namespace ri
