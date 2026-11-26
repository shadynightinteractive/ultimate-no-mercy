// ri_revimport.cpp (phase3 p3revfolders, 2026-11-19): WCW/nWo Revenge's never-ported Superstars as wrestler folders -- see
// ri_revimport.h. Each part names the Python it mirrors (tools\handlers\aki_import.py, handlers\revenge\*.py, mod\gen\wmport.py,
// tools\romimport\revmodels.py).
#include "ri_revimport.h"
#include "ri_images.h"
#include "ri_data.h"
#include "ri_themes.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace ri {

static ojson load_json(const fs::path& p) { Bytes b = read_file(p); return ojson::parse(std::string(b.begin(), b.end())); }

// ---- text: Revenge's latin-1 as Python's str ---------------------------------------------------------------------------------
// The handler decodes Revenge's names as latin-1 and works on code points (strip, upper); the folder's JSON escapes them.
typedef std::vector<uint32_t> U32s;
static U32s latin1(const std::string& s) { U32s o; for (unsigned char c : s) o.push_back(c); return o; }
static std::string utf8(const U32s& u) {
    std::string o;
    for (uint32_t c : u) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); }
        else { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); }
    }
    return o;
}
static U32s from_utf8(const std::string& s) {           // (the record's names, back to code points)
    U32s o;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        if (c < 0x80) { o.push_back(c); i++; }
        else if (c < 0xE0) { o.push_back(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)); i += 2; }
        else { o.push_back(((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F)); i += 3; }
    }
    return o;
}
static bool py_space(uint32_t c) { return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0; }
static U32s py_strip(const U32s& s) {                     // str.strip()
    size_t a = 0, b = s.size();
    while (a < b && py_space(s[a])) a++;
    while (b > a && py_space(s[b - 1])) b--;
    return U32s(s.begin() + a, s.begin() + b);
}
static U32s py_upper(const U32s& s) {                     // str.upper() over latin-1's code points
    U32s o;
    for (uint32_t c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) o.push_back(c - 0x20);
        else if (c == 0xDF) { o.push_back('S'); o.push_back('S'); }   // (sharp s: "SS")
        else if (c == 0xFF) o.push_back(0x178);
        else if (c == 0xB5) o.push_back(0x39C);                      // (micro sign: Greek capital mu)
        else o.push_back(c);
    }
    return o;
}
static U32s head(const U32s& s, size_t n) { return U32s(s.begin(), s.begin() + std::min(n, s.size())); }

// game.py roster's name marks: "<S>cott{.}< Steiner>" -- <...> letters of the short name, {...} put in the short name only.
//   name  = re.sub(r"\{[^}]*\}", "", full).replace("<", "").replace(">", "").strip()
//   short = "".join(a or b for a, b in re.findall(r"<([^>]*)>|\{([^}]*)\}", full)).strip() or name
static void rev_names(const U32s& full, U32s& name, U32s& shrt) {
    U32s n1;
    for (size_t i = 0; i < full.size();) {
        if (full[i] == '{') {
            size_t j = i + 1; while (j < full.size() && full[j] != '}') j++;
            if (j < full.size()) { i = j + 1; continue; }
        }
        n1.push_back(full[i]); i++;
    }
    U32s n2; for (uint32_t c : n1) if (c != '<' && c != '>') n2.push_back(c);
    name = py_strip(n2);
    U32s s;
    for (size_t i = 0; i < full.size();) {
        uint32_t close = full[i] == '<' ? '>' : full[i] == '{' ? '}' : 0;
        if (close) {
            size_t j = i + 1; while (j < full.size() && full[j] != close) j++;
            if (j < full.size()) { s.insert(s.end(), full.begin() + i + 1, full.begin() + j); i = j + 1; continue; }
        }
        i++;
    }
    shrt = py_strip(s);
    if (shrt.empty()) shrt = name;
}

std::string rev_slug(const std::string& name) {          // re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
    std::string o; bool run = false;
    for (unsigned char c : name) {
        char l = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
        // (a latin-1 letter's lower case is past ASCII: not [a-z0-9] either way)
        if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) { o += l; run = false; }
        else if (!run) { o += '_'; run = true; }
    }
    size_t a = o.find_first_not_of('_'), b = o.find_last_not_of('_');
    return a == std::string::npos ? "" : o.substr(a, b - a + 1);
}

// ---- body.py -----------------------------------------------------------------------------------------------------------------
static constexpr uint32_t SCALE_TABLE = 0x800318C0, BODY_WORDS = 0x80031998, FRAME_ATTIRES = 0x8003673C;
static const int NM_BELT[] = { 0, 0, 1, 1, 2, 2, 3, 4, 1, 5, 1, 0, 2, 6, 6, 7, 7, 8, 9, 9, 9, 10, 1, 1 };   // 0x80048A48[body type]
static const int NM_HAND_CLASS[] = { 0, 1, 1, 2, 3, 2, 4, 4, 4, 4, 4 };                                     // 0x80047FE8[belt]
static constexpr int WOMEN_GROUP = 9, NM_WOMAN = 15, NM_WOMAN_LOOKS = 51;
static constexpr int NM_WOMAN_HEIGHT = 36, NM_WOMAN_WEIGHT = 500, NM_STAFF_HEIGHT = 37, NM_STAFF_WEIGHT = 501;
static std::string nm_name(int t) {
    static const char* N[] = { "Skinny 1", "Skinny 2", "Medium 1", "Medium 2", "Thick 1", "Thick 2", "Fat 1", "Fat 2" };
    if (t >= 0 && t < 8) return N[t];
    if (t == 15) return "a woman's";
    return std::to_string(t);
}
static double nm_scale(int height) {                      // body.py nm_scale
    int h = std::max(0, std::min(height, 35));
    int size = h * 255 / 36;
    return py_round(0.97 + size * 0.00047, 4);
}
static int nm_size_for_scale(double s) { return (int)std::max<long long>(0, std::min<long long>(255, py_round_int((s - 0.97) / 0.00047))); }
static double scale_of_size(int size) { return py_round(0.97 + size * 0.00047, 4); }
static int height_for_scale(double scale) {               // min(range(36), key=lambda h: (abs(nm_scale(h) - scale), h))
    int best = 0; double bd = 0;
    for (int h = 0; h < 36; h++) { double d = std::fabs(nm_scale(h) - scale); if (h == 0 || d < bd) { bd = d; best = h; } }
    return best;
}
// nm_body_type(e, b) -> (No Mercy body type, why)
static std::pair<int, std::string> nm_body_type(const RevRecord& e) {
    std::optional<int> lbs; if (e.weight) lbs = *e.weight + 100;
    if ((int)(e.code >> 8) == WOMEN_GROUP) return { NM_WOMAN, "Revenge's valets (group 9): No Mercy's women's type" };
    int f = e.body.frame;
    if (f == 0) {
        if (lbs && *lbs >= 180) return { 1, strf("Revenge's small frame (0), %d lbs: Skinny 2", *lbs) };
        return { 0, "Revenge's small frame (0)" + (lbs ? strf(", %d lbs", *lbs) : std::string()) + ": Skinny 1" };
    }
    if (f == 3) {
        if (lbs && *lbs >= 330) return { 7, strf("Revenge's heavy frame (3), %d lbs: Fat 2", *lbs) };
        return { 6, "Revenge's heavy frame (3)" + (lbs ? strf(", %d lbs", *lbs) : std::string()) + ": Fat 1" };
    }
    if (!lbs) return { 3, strf("Revenge's frame %d, no weight on his record: Medium 2 (the standard build)", f) };
    static const int STEPS[3][2] = { { 230, 2 }, { 265, 3 }, { 275, 4 } };
    for (auto& s : STEPS) if (*lbs < s[0]) return { s[1], strf("Revenge's frame %d, %d lbs: %s", f, *lbs, nm_name(s[1]).c_str()) };
    return { 5, strf("Revenge's frame %d, %d lbs: Thick 2", f, *lbs) };
}

// ---- game.py roster ----------------------------------------------------------------------------------------------------------
static constexpr const char* CLEAN_SHA1 = "e1711a2511394b9357b5f1ac8ca5cc17bd674836";   // game.py check_clean: Revenge (USA)

std::vector<RevRecord> rev_roster(Roms& R, const RiData& D) {
    auto rom = R.rom("revenge");
    if (!rom) fail("the Revenge ROM isn't there");
    // (check_clean: the handler's tables are the clean USA game's; the boot image is made from that ROM, so it is its RAM)
    if (sha1_hex(*rom) != CLEAN_SHA1) fail("the Revenge ROM isn't the clean WCW/nWo Revenge (USA) this handler's tables are known for");
    Bytes r = make_image("rev_boot", D.dir / "loadmaps", R);      // (game.py BOOT_RAM: v_boot_ram.bin)
    auto r32 = [&](uint32_t a) { return be32(r, a & 0x7FFFFF); };
    auto r16 = [&](uint32_t a) { return (uint32_t)be16(r, a & 0x7FFFFF); };
    auto cstr = [&](uint32_t a) {                                 // 40 bytes up to the first 0, latin-1
        std::string s; size_t o = a & 0x7FFFFF;
        for (size_t i = 0; i < 40 && r[o + i]; i++) s += (char)r[o + i];
        return s;
    };
    RevMovesets ms = rev_movesets(R);
    std::map<std::pair<int, int>, int> sets;
    for (auto& row : ms.rows) sets[{ row.group, row.row }] = row.file;   // ({(g, row): f}: the last wins)
    float scales[8];
    for (int i = 0; i < 8; i++) { uint32_t v = r32(SCALE_TABLE + 4 * i); std::memcpy(&scales[i], &v, 4); }
    int attires[5];
    for (int i = 0; i < 5; i++) attires[i] = sx16(r16(FRAME_ATTIRES + 2 * i));
    uint32_t table = r32(0x8003A594);
    std::vector<RevRecord> out;
    for (uint32_t g = 0; g < 11; g++) {
        uint32_t p = r32(0x8003A4F8 + 8 * g), n = r16(0x8003A4FC + 8 * g);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t rec = r32(p + 4 * i);
            RevRecord e;
            e.code = r16(rec);
            e.index = r[(rec & 0x7FFFFF) + 2];
            if (e.code >> 8 >= 10) continue;                       // (groups 10..: managers and the like)
            U32s nm, sh;
            rev_names(latin1(cstr(r32(rec + 8))), nm, sh);
            e.name = utf8(nm); e.short_name = utf8(sh);
            // re.match(r"(\d+)'(\d+)") / re.match(r"(\d+)") at the text's start
            std::string ht = cstr(r32(rec + 12)), wt = cstr(r32(rec + 16));
            auto digits = [](const std::string& s, size_t& at, long long& v) {
                size_t b = at; v = 0;
                while (at < s.size() && s[at] >= '0' && s[at] <= '9') { v = v * 10 + (s[at] - '0'); at++; if (v > 1000000000) v = 1000000000; }
                return at > b;
            };
            size_t at = 0; long long a1, a2;
            if (digits(ht, at, a1) && at < ht.size() && ht[at] == '\'' && (++at, digits(ht, at, a2))) {
                long long hv = (a1 - 5) * 12 + a2;
                if (0 <= hv && hv <= 255) e.height = (int)hv;      // (managers and the like: 0'0" -- none)
            }
            at = 0;
            if (digits(wt, at, a1)) { long long wv = a1 - 100; if (wv >= 0) e.weight = (int)wv; }
            for (int c = 0; c < 4; c++) {
                size_t o = (table & 0x7FFFFF) + 42 * (4 * e.index + c);
                e.costumes[c] = { r[o + 36], r[o + 37], strf("%02x%02x%02x", r[o + 38], r[o + 39], r[o + 40]) };
            }
            auto ms_it = sets.find({ (int)(e.code >> 8), (int)(e.code & 0xFF) - 1 });
            if (ms_it != sets.end()) e.moveset_file = ms_it->second;
            // body.py Tables.body: the body word (frame bits 0-6, scale class bits 12-14)
            uint32_t bp = r32(BODY_WORDS + 4 * (e.code >> 8));
            uint32_t w = r16(bp + 2 * ((e.code & 0xFF) - 1));
            e.body.word = w; e.body.frame = w & 0x7F; e.body.scale_class = (w & 0x7000) >> 12;
            if (e.body.frame < 5) e.body.frame_attires = attires[e.body.frame];
            e.body.scale = py_round((double)scales[e.body.scale_class], 4);
            out.push_back(std::move(e));
        }
    }
    return out;
}

// ---- the handler's own data ----------------------------------------------------------------------------------------------------
// the 24 ported men's fixes (wrestlers\*.json): their string IDs and model codes (the --all run leaves them out)
static void ported(const RevImportData& D, std::set<std::string>& sids, std::set<uint32_t>& codes) {
    for (auto& f : fs::directory_iterator(D.handler_dir / "wrestlers")) {
        if (f.path().extension() != ".json") continue;
        ojson j = load_json(f.path());
        sids.insert(j["string_id"].get<std::string>());
        codes.insert((uint32_t)std::stoul(j["revenge"]["code"].get<std::string>(), nullptr, 16));
    }
}

// ---- revmodels.py auto_plan + "auto" -------------------------------------------------------------------------------------------
RevAutoCaps rev_auto_caps(Roms& R, const RevImportData& D, const std::vector<RevRecord>& roster, const std::set<uint32_t>& want) {
    std::set<std::string> psids; std::set<uint32_t> pcodes;
    ported(D, psids, pcodes);
    std::vector<uint32_t> codes;
    for (auto& e : roster) if (!pcodes.count(e.code) && e.moveset_file && *e.moveset_file) codes.push_back(e.code);
    std::vector<std::pair<uint32_t, int>> plan;                   // costume-major
    for (int k = 0; k < 4; k++) for (uint32_t c : codes) plan.push_back({ c, k });
    // the stand-ins: a code with no model of its own loads another man (what the rig's capture held in that slot)
    std::map<uint32_t, std::pair<uint32_t, int>> sub;
    {
        ojson J = load_json(D.ri.dir / "frozen" / "revenge_caps.json")["auto_stand_ins"];
        for (auto it = J.begin(); it != J.end(); ++it)
            if (it.key() != "about") sub[(uint32_t)std::stoul(it.key())] = { it.value()[0].get<uint32_t>(), it.value()[1].get<int>() };
    }
    const RomFiles& F = *R.files("revenge");
    Bytes img = rev_match_image(R, D.ri);
    RevAutoCaps store;
    for (size_t i = 0; i < plan.size(); i += 2) {                  // (two a run: slots 0 and 1, as capture_all.py)
        std::vector<RevMan> men;
        size_t n = std::min<size_t>(2, plan.size() - i);
        bool wanted = want.empty();
        for (size_t s = 0; s < n; s++) wanted = wanted || want.count(plan[i + s].first);
        if (!wanted) continue;
        for (size_t s = 0; s < n; s++) {
            auto [code, k] = plan[i + s];
            auto st = sub.find(code);
            uint32_t lc = st != sub.end() ? st->second.first : code;
            int lk = st != sub.end() ? st->second.second : k;
            Bytes e = rev_entry(img, lc, (uint32_t)lk);
            men.push_back({ lc, (uint32_t)lk, e[37], e[36] });
        }
        RevLoad L = rev_load(img, F, men);
        for (size_t s = 0; s < n; s++) store[plan[i + s]] = rev_capture(L.ram, L.calls, (int)s);
    }
    return store;
}

// ---- wmport.py: Revenge's hands as WrestleMania 2000's (ci8, mirror, rev_fingers) -----------------------------------------------
// ci8: a CI4 picture as CI8, its palette made 0x200 bytes (WM2000's hands are 8 bits a pixel); already 8 bits: as it is
static void ci8(Bytes& tex, Bytes& pal) {
    if (tex[2] == 1) return;
    int w = tex[0] + 1, h = tex[1] + 1;
    Bytes t(tex.begin(), tex.begin() + 8);
    t[2] = 1; t[3] = 0;
    size_t end = std::min(tex.size(), (size_t)(8 + w * h / 2));
    for (size_t i = 8; i < end; i++) { t.push_back(tex[i] >> 4); t.push_back(tex[i] & 15); }
    Bytes p(pal.begin(), pal.begin() + std::min<size_t>(pal.size(), 512));
    p.resize(512, 0);
    tex = std::move(t); pal = std::move(p);
}
// mirror: a CI8 picture flipped left to right
static Bytes mirror(const Bytes& tex) {
    size_t w = tex[0] + 1, h = tex[1] + 1;
    Bytes o(tex.begin(), tex.begin() + 8);
    size_t n = tex.size() - 8;
    for (size_t y = 0; y < h; y++) {
        size_t a = std::min(n, y * w), b = std::min(n, (y + 1) * w);
        for (size_t x = b; x > a; x--) o.push_back(tex[8 + x - 1]);
    }
    return o;
}
static std::vector<std::array<int, 3>> colours(const Bytes& pal) {     // wmport._colors
    std::vector<std::array<int, 3>> o;
    for (size_t i = 0; i + 1 < pal.size(); i += 2) { uint32_t v = be16(pal, i); o.push_back({ (int)((v >> 11) << 3), (int)(((v >> 6) & 31) << 3), (int)(((v >> 1) & 31) << 3) }); }
    return o;
}
static double lum(const std::array<int, 3>& c) { return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]; }
// rev_fingers: WrestleMania 2000's finger picture (CI8), its colours the palm's, light for light -> the new palette
static Bytes rev_fingers(const Bytes& wm_tex, const Bytes& wm_pal, const Bytes& palm_tex, const Bytes& palm_pal) {
    size_t w = wm_tex[0] + 1, h = wm_tex[1] + 1;
    Bytes wpx(wm_tex.begin() + 8, wm_tex.begin() + std::min(wm_tex.size(), 8 + w * h));
    auto wc = colours(wm_pal), pc = colours(palm_pal);
    size_t pw = palm_tex[0] + 1, ph = palm_tex[1] + 1;
    std::vector<std::pair<double, std::array<int, 3>>> palm;     // sorted((lum, colour) for each palm pixel)
    for (size_t i = 8; i < std::min(palm_tex.size(), 8 + pw * ph); i++) palm.push_back({ lum(pc.at(palm_tex[i])), pc.at(palm_tex[i]) });
    std::sort(palm.begin(), palm.end());
    // order = sorted(set(wpx), key=lum): a stable sort of the set's iteration order. CPython iterates a set of small ints in
    // ascending order while they are under its table's size (8 at least); past that, with two equal brightnesses, the order
    // would be the set's own -- not mirrored here: such a picture is refused, not guessed at.
    std::map<int, long> count;
    for (uint8_t i : wpx) count[i]++;
    std::vector<int> order;
    for (auto& [i, n] : count) order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return lum(wc.at(a)) < lum(wc.at(b)); });
    bool small = count.empty() || count.rbegin()->first < 8;
    for (size_t i = 1; i < order.size(); i++)
        if (!small && lum(wc[order[i]]) == lum(wc[order[i - 1]]))
            fail("rev_fingers: two of WrestleMania 2000's finger colours are equally bright: the Python's set order decides");
    Bytes newpal = wm_pal;
    long seen = 0;
    for (int i : order) {
        double mid = ((double)seen + (double)count[i] / 2.0) / (double)wpx.size(); seen += count[i];
        long long at = (long long)(mid * (double)palm.size());
        auto& c = palm[(size_t)std::min<long long>((long long)palm.size() - 1, at)].second;
        put_be16(&newpal[2 * i], (uint32_t)(((c[0] >> 3) << 11) | ((c[1] >> 3) << 6) | ((c[2] >> 3) << 1) | 1));
    }
    return newpal;
}

// ---- handlers\aki\facefit.py (model_fixes.json): a face mesh lengthened downward -------------------------------------------------
static int s8(int v) { return v > 127 ? v - 256 : v; }
static std::vector<std::array<int, 3>> verts(const Bytes& m, int& sc) {
    sc = (m[0] & 0x7F) + 1; int nv = m[1] & 0x7F;
    int off[3] = { s8(m[4]) * 16, s8(m[5]) * 16, s8(m[6]) * 16 };
    std::vector<std::array<int, 3>> V;
    for (int j = 0; j < nv; j++) { std::array<int, 3> v; for (int q = 0; q < 3; q++) v[q] = (s8(m[8 + 8 * j + q]) + off[q]) * sc; V.push_back(v); }
    return V;
}
static Bytes face_lengthen(const Bytes& face, const std::set<std::array<int, 3>>& keep, double factor, int pivot_y, int& moved) {
    int sc; auto V = verts(face, sc);
    int y0 = V[0][1], y1 = V[0][1];
    for (auto& v : V) { y0 = std::min(y0, v[1]); y1 = std::max(y1, v[1]); }
    double extra = (factor - 1.0) * (double)(y1 - y0);
    Bytes d = face; moved = 0;
    for (size_t j = 0; j < V.size(); j++) {
        auto& v = V[j];
        if (keep.count(v) || v[1] >= pivot_y) continue;
        double t = std::min(1.0, (double)(pivot_y - v[1]) / (double)(pivot_y - y0));
        long long dy = -py_round_int(extra * t / sc) * sc;
        if (!dy) continue;
        long long b = s8(d[8 + 8 * j + 1]) + dy / sc;            // (dy a multiple of sc: // exact)
        if (b < -128 || b > 127) fail("face_lengthen: vertex %zu out of the mesh's range", j);
        d[8 + 8 * j + 1] = (uint8_t)(b & 0xFF); moved++;
    }
    return d;
}
static std::vector<Bytes> face_fixes(const std::vector<Bytes>& descs, Pool& pool, const ojson& fx, const std::string& key,
                                     std::vector<std::string>& report) {
    if (!fx.contains(key) || !fx[key].contains("face_lengthen")) return descs;
    const ojson& fl = fx[key]["face_lengthen"];
    int k = fl.contains("part") ? fl["part"].get<int>() : 5;
    std::vector<int> keep_parts = { 4, 20 };
    if (fl.contains("keep_parts")) keep_parts = fl["keep_parts"].get<std::vector<int>>();
    double factor = fl["factor"].get<double>(); int pivot = fl["pivot_y"].get<int>();
    std::vector<Bytes> out;
    for (size_t c = 0; c < descs.size(); c++) {
        Bytes d = descs[c];
        uint32_t m = be16(d, 8 * k);
        if (!pool.ours(m)) { out.push_back(d); continue; }
        std::set<std::array<int, 3>> keep;
        for (int o : keep_parts) { uint32_t n = be16(d, 8 * o); int sc; if (pool.ours(n)) for (auto& v : verts(pool.get(n), sc)) keep.insert(v); }
        int moved;
        Bytes nw = face_lengthen(pool.get(m), keep, factor, pivot, moved);
        put_be16(&d[8 * k], pool.one(nw));
        report.push_back(strf("%s %zu: face (part %d) lengthened below y %d: %d vertices moved", key.c_str(), c, k, pivot, moved));
        out.push_back(d);
    }
    return out;
}

// ---- game.py build_model (a man with captures: twin False) -------------------------------------------------------------------
static constexpr uint32_t SKEL_WM_HANDS = 0x262, SKEL_WM_FLAGS = 0x264, SKEL_MAGIC_AT = 0x268;
static constexpr uint32_t SKEL_WM_REV = 0x80000000, SKEL_MAGIC_WM = 0x574D324B, NM_PAL_SIZE = 0x200;
static constexpr uint32_t SKEL_WM_NMHANDS = 0x40000000, SKEL_WM_OWNHANDS = 0x10000000;   // (mod src/skel.h)

// WrestleMania 2000's finger pictures, Jeff Jarrett's (its roster index 29, costume 0, slot 0: wmport.PORTED[0]): parts 10 and 18
struct WmFingers { std::map<int, std::pair<Bytes, Bytes>> by_k; };
static const WmFingers& wm_fingers(Roms& R, const RiData& D) {
    static WmFingers W; static bool done = false;
    if (done) return W;
    bool got = false;
    wm_models(R, D, "wmport", [&](const std::string& rel, const Bytes& ram) {
        if (rel != "wmport/cap/k29_c0/ram.bin") return;
        for (auto& p : wm_capture(ram, 0).parts)
            if (p.tex && p.pal) W.by_k[p.k] = { *p.tex, *p.pal };
        got = true;
    });
    if (!got || !W.by_k.count(10) || !W.by_k.count(18)) fail("WrestleMania 2000's finger pictures (Jeff Jarrett's capture) weren't made");
    done = true;
    return W;
}

RevModel rev_build_model(Roms& R, const RevImportData& D, const std::string& sid, const std::map<int, RevPart>* got[4],
                         bool woman, std::vector<std::string>& report) {
    const RomFiles& rf = *R.files("revenge");
    const RomFiles& nm = *R.files("nomercy");
    // (c2hands 2026-11-19, the user: "Revenge uses its own hands when WM2000 is missing (today's look when both are there)")
    // With WrestleMania 2000: his hands' pictures made for its pose meshes (hands.c), as always. Without it: Revenge's own --
    // his palms' and fingers' pictures as Revenge captured them, drawn on his own hand meshes as modelled (SKEL_WM_OWNHANDS
    // below, for a model that keeps them: one with No Mercy's hands has them replaced, as always).
    const WmFingers* wm = R.files("wm2000") ? &wm_fingers(R, D.ri) : nullptr;
    RevModel M;
    for (int c = 0; c < 4; c++) {
        Bytes d(0x270, 0);
        for (auto& [k, part] : *got[c]) {
            uint32_t t = 0, q = 0;
            bool has = part.tex && !part.tex->empty();
            if (has) {
                Bytes tex = *part.tex, pal = part.pal ? *part.pal : Bytes();
                if (wm && (k == 9 || k == 10 || k == 17 || k == 18)) {  // (his hands: WM2000's meshes' pictures, hands.c)
                    ci8(tex, pal);
                    if (k == 9 || k == 17) tex = mirror(tex);
                    if (k == 10 || k == 18) {                           // (the fingers: WM2000's picture in his palm's colours)
                        auto palm = got[c]->find(k - 1);
                        if (palm == got[c]->end() || !palm->second.tex) fail("%s costume %d: fingers with no palm picture", sid.c_str(), c);
                        Bytes pt = *palm->second.tex, pp = palm->second.pal ? *palm->second.pal : Bytes();
                        ci8(pt, pp);
                        auto& ref = wm->by_k.at(k);
                        tex = ref.first; pal = rev_fingers(ref.first, ref.second, pt, pp);
                    }
                }
                std::tie(t, q) = M.pool.pair(tex, pal);                 // (the pair first, then the mesh: the Python's order)
            }
            uint32_t m = M.pool.one(rf(part.mesh));
            put_be16(&d[8 * k], m); put_be16(&d[8 * k + 2], t); put_be16(&d[8 * k + 4], q);
        }
        put_be32(&d[SKEL_WM_FLAGS], SKEL_WM_REV);
        d[SKEL_WM_HANDS] = 0;
        put_be32(&d[SKEL_MAGIC_AT], SKEL_MAGIC_WM);
        M.descs.push_back(d);
    }
    // No Mercy's hands in his own skin (skintone.nm_hands), his wrists onto them (handfit), his model fixes (facefit); with no
    // bare skin anywhere to go by (the Python's SystemExit): Revenge's own skin and hands kept, nothing else done.
    Skins skins;
    try {
        std::vector<Bytes> h;
        for (int c = 0; c < 4; c++) h.push_back(nm_hands(M.descs[c], M.pool, nm, skins, sid, woman ? woman_kit() : man_kit()));
        h = fit_wrists(h, M.pool, nm);
        static ojson mfx; static bool loaded = false;
        if (!loaded) {
            fs::path p = D.handler_dir / "model_fixes.json";
            mfx = fs::exists(p) && load_json(p).contains("wrestlers") ? load_json(p)["wrestlers"] : ojson::object();
            loaded = true;
        }
        M.descs = face_fixes(h, M.pool, mfx, sid, report);
    } catch (const NoSkin& ex) {
        report.push_back(std::string(sid) + ": " + ex.what() + " -- Revenge's own skin and hands kept");
    }
    // (c2hands) without WrestleMania 2000, a costume that keeps Revenge's hands keeps them as modelled (hands.c own_hands)
    if (!wm)
        for (auto& d : M.descs)
            if (!(be32(d, SKEL_WM_FLAGS) & SKEL_WM_NMHANDS)) put_be32(&d[SKEL_WM_FLAGS], be32(d, SKEL_WM_FLAGS) | SKEL_WM_OWNHANDS);
    // No Mercy's palettes are 0x200 bytes a part (gen/wmport.py nm_palettes)
    std::set<uint32_t> pals;
    for (auto& d : M.descs)
        for (int k = 1; k < 28; k++) { uint32_t q = be16(d, part_offset(k) + 4); if (M.pool.ours(q)) pals.insert(q); }
    for (uint32_t q : pals) { Bytes& b = M.pool.data[q - POOL0]; if (b.size() < NM_PAL_SIZE) b.resize(NM_PAL_SIZE, 0); }
    return M;
}

// ---- base looks (aki_import.py base_looks: mkparts.py vapp + costume_src) -------------------------------------------------------
// The appearance entry's 38 bytes (No Mercy's file 1) as the folder's 38 "looks" fields (the same table as ri_wmimport's).
static const int APP_TO_LOOKS[38][2] = {
    { 0x00, 0x00 }, { 0x01, 0x01 }, { 0x02, 0x03 }, { 0x03, 0x04 }, { 0x04, 0x05 }, { 0x05, 0x06 }, { 0x06, 0x07 }, { 0x07, 0x08 },
    { 0x08, 0x09 }, { 0x09, 0x0A }, { 0x0A, 0x0B }, { 0x1C, 0x0C }, { 0x1D, 0x0F }, { 0x1E, 0x12 }, { 0x1F, 0x13 }, { 0x20, 0x15 },
    { 0x21, 0x18 }, { 0x22, 0x1B }, { 0x0C, 0x1E }, { 0x0D, 0x1F }, { 0x11, 0x21 }, { 0x12, 0x22 }, { 0x13, 0x24 }, { 0x14, 0x25 },
    { 0x0F, 0x27 }, { 0x10, 0x28 }, { 0x15, 0x2A }, { 0x16, 0x2B }, { 0x17, 0x2D }, { 0x18, 0x2E }, { 0x19, 0x30 }, { 0x1A, 0x31 },
    { 0x1B, 0x32 }, { 0x0E, 0x33 }, { 0x23, 0x36 }, { 0x24, 0x37 }, { 0x0B, 0x39 }, { 0x25, 0x3C } };
static std::vector<int> base_looks_one(const uint8_t* app) {
    std::vector<std::pair<int, int>> by_off;
    for (auto& p : APP_TO_LOOKS) by_off.push_back({ p[1], p[0] });
    std::sort(by_off.begin(), by_off.end());
    std::vector<int> out;
    for (auto& [off, a] : by_off) {
        int v = app[a];
        // (mkparts.py costume_src: the extended parts' numbers taken back to the game's -- its NEW maps inverted)
        if (off == 3) { if (v == 131) v = 129; else if (v == 143) v = 130; else if (v == 151) v = 131; }
        else if (off == 6) { if (v == 107) v = 0x80; }
        else if (off == 0xF) { if (v == 82) v = 0x80; else if (v == 83) v = 0x81; else if (v == 132) v = 0x82; }
        out.push_back(v);
    }
    return out;
}
static std::vector<std::vector<int>> base_looks(const Bytes& van, const RomFiles& nm, int ord) {
    std::vector<std::vector<int>> out;
    const Bytes& app = nm(1);
    for (int c = 0; c < 4; c++) {
        uint32_t a = be16(van, (be32(van, (0x80047124 + 16 * ord + 4 * c) & 0x7FFFFF) + 0xE) & 0x7FFFFF);
        out.push_back(base_looks_one(app.data() + a * 0x26));
    }
    return out;
}

// ---- aki_import.py's words ---------------------------------------------------------------------------------------------------------
static const char* const ABOUT_FIRST =
    "%s from WCW/nWo Revenge, imported by handlers\\aki_import.py (prototype). NO MODEL OF HIS OWN YET: Revenge builds his model "
    "in a match, so the handler needs a capture of him (captures.json's recipe, model code 0x%03X); until then he looks like "
    "the man he's based on.";
static const char* const ABOUT_REST =
    "%s from WCW/nWo Revenge, imported by handlers\\aki_import.py (prototype): his model as Revenge builds it (captured from "
    "the user's ROM in the rig, model code 0x%03X), his name, size and moveset from the ROM, his photo from Revenge's own "
    "Costume Change screen, his entrance theme the one Revenge plays (theme.bin: Revenge has none per wrestler -- put a theme "
    "of your own in its place). Edit freely.";
static const char* const HW_WOMAN_WHY =
    "Revenge's record has no height or weight for her (0'0\", 0 lbs; her scale %.2f is her \"size\"): No Mercy's own women's "
    "codes, height %d and weight %d -- the select screen's text for a woman and the engine's woman's weight class (light), as "
    "Stephanie, Lita and Trish have (handlers\\revenge\\body.py)";
static const char* const HW_STAFF_WHY =
    "Revenge's record has no weight for him, a manager (his scale %.2f is his \"size\"): No Mercy's own non-wrestlers' codes, "
    "height %d and weight %d -- the select screen's text for one and the engine's weight class, as Mr. McMahon and Shane "
    "McMahon have (handlers\\revenge\\body.py)";
static const char* const HEIGHT_WHY =
    "Revenge's scale for him (%.2f: his record has no height) -- No Mercy's size for this height is the nearest to it";
static const char* const MUSIC_WHY =
    "music slot 64, Jim Neidhart's: Revenge's song 0x214 and the 24 ports' plain entrance lights -- not his base's (The Rock's "
    "/ Kane's) entrance; his theme.bin plays over it";
static constexpr int REV_MUSIC_SLOT = 64;

// (Python's "%.2f": printf's, correctly rounded)
static std::string f2(const char* fmt, double s, int a, int b) { return strf(fmt, s, a, b); }

// (rostertags, 2026-11-21) game.py wrestler_class: what he is, from Revenge's own groups (code >> 8): 8 its managers, 9 its valets
static std::pair<const char*, const char*> rev_class(uint32_t code) {
    const uint32_t g = code >> 8;
    if (g == 9) return { "valet", "Revenge's own group 9 (its model codes 0x9xx): its valets, who don't wrestle" };
    if (g == 8) return { "manager", "Revenge's own group 8 (its model codes 0x8xx): its managers, who don't wrestle" };
    return { "wrestler", "Revenge's own wrestlers' groups (its model codes 0x0xx-0x7xx)" };
}

// ---- the collection ------------------------------------------------------------------------------------------------------------
RevFoldersResult revenge_folders(Roms& R, const RevImportData& D, const fs::path& out, const std::vector<std::string>& only,
                                 const RevMovesetFn& moveset) {
    if (!R.rom("revenge")) fail("the Revenge ROM isn't there");
    if (!R.rom("nomercy")) fail("the No Mercy ROM isn't there");
    // (c2hands 2026-11-19: WrestleMania 2000 no longer needed -- without it, Revenge's own hands: rev_build_model)
    RevFoldersResult res;
    std::vector<RevRecord> roster = rev_roster(R, D.ri);
    std::set<std::string> psids; std::set<uint32_t> pcodes;
    ported(D, psids, pcodes);
    // the records Revenge draws another man for (fixes2.skipped: skip.json)
    std::map<uint32_t, std::pair<std::string, std::string>> skip;
    {
        ojson j = load_json(D.handler_dir / "skip.json")["codes"];
        for (auto it = j.begin(); it != j.end(); ++it)
            skip[(uint32_t)std::stoul(it.key(), nullptr, 16)] = { it.value()["name"].get<std::string>(), it.value()["shows"].get<std::string>() };
    }
    // the frozen number IDs (legacy_ids.json); a man not in it takes the next free number from 0x0340 up (--legacy-ids 0x0340)
    std::map<std::string, std::string> frozen; std::set<uint32_t> taken;
    {
        ojson j = load_json(D.handler_dir / "legacy_ids.json");
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it.key() != "about") { frozen[it.key()] = it.value().get<std::string>(); taken.insert((uint32_t)std::stoul(it.value().get<std::string>(), nullptr, 16)); }
    }
    uint32_t legacy = 0x0340;
    auto next_legacy = [&](const std::string& sid) {
        auto f = frozen.find(sid);
        if (f != frozen.end()) return f->second;
        while (taken.count(legacy)) legacy++;
        return strf("0x%04X", legacy++);
    };
    std::set<uint32_t> want;                                           // (only some men: only their runs)
    for (auto& e : roster) if (!only.empty() && std::find(only.begin(), only.end(), rev_slug(e.name)) != only.end()) want.insert(e.code);
    if (!only.empty() && want.empty()) fail("none of the string IDs asked for is on Revenge's roster");
    auto tc = std::chrono::steady_clock::now();
    RevAutoCaps store = rev_auto_caps(R, D, roster, want);
    res.log.push_back(strf("(the models: %zu loaded by Revenge's code, %.1f s)", store.size(),
                           std::chrono::duration<double>(std::chrono::steady_clock::now() - tc).count()));
    const RomFiles& nm = *R.files("nomercy");
    Bytes van = make_image("van_sel", D.ri.dir / "loadmaps", R);   // (mkroster.py EV: No Mercy's Superstar Select)
    auto ord_id = [&](int o) { return (uint32_t)be16(van, be32(van, (0x80047124 + 16 * o) & 0x7FFFFF) & 0x7FFFFF); };
    std::optional<ThemeFiles> theme;                                   // (theme.py theme_files: made once a run)
    std::map<int, ThemeFiles> redux_themes;                            // (theme.py redux_files: each song made once a run)
    std::set<std::string> made;
    double t_model = 0, t_photo = 0, t_write = 0;                      // (the run's time by step, for the log)
    auto now = [] { return std::chrono::steady_clock::now(); };
    auto secs = [](auto a, auto b) { return std::chrono::duration<double>(b - a).count(); };
    for (const RevRecord& e : roster) {
        std::string sid = rev_slug(e.name);
        if (pcodes.count(e.code) || psids.count(sid) || made.count(sid)) continue;
        if (!only.empty() && std::find(only.begin(), only.end(), sid) == only.end()) continue;
        auto sk = skip.find(e.code);
        if (sk != skip.end()) {
            res.log.push_back(strf("%s: code 0x%03X -- no folder: %s: Revenge shows %s", sid.c_str(), e.code, sk->second.first.c_str(), sk->second.second.c_str()));
            continue;
        }
        if (!e.moveset_file || !*e.moveset_file) { res.log.push_back(sid + ": no moveset file known -- skipped"); continue; }
        auto [body, body_why] = nm_body_type(e);
        bool woman = body == NM_WOMAN;
        int ref = (e.weight ? *e.weight : 0) >= 200 ? 4 : 1;          // (Kane for the big men, else The Rock)
        if (woman) ref = NM_WOMAN_LOOKS;                               // (a woman's: Stephanie McMahon)
        uint32_t ref_id = ord_id(ref);
        RevManInfo info{ &e, sid, ref, body };
        std::optional<RevMovesetPiece> mp;
        if (moveset) mp = moveset(info);
        // his model: Revenge's four costumes as its own code builds them; his photo rendered from it
        const std::map<int, RevPart>* got[4];
        bool all = true;
        for (int c = 0; c < 4; c++) { auto it = store.find({ e.code, c }); got[c] = it == store.end() ? nullptr : &it->second; all = all && got[c]; }
        std::vector<std::string> rep;
        std::optional<RevModel> M;
        Bytes photo;
        if (all) {
            auto t0 = now(); M = rev_build_model(R, D, sid, got, woman, rep);
            auto t1 = now(); photo = photo_from_pool(M->descs[0], M->pool, nm);
            t_model += secs(t0, t1); t_photo += secs(t1, now());
        }
        // ---- wrestler.json, key for key in the order the two runs leave them ----
        ojson w = ojson::object();
        w["about"] = strf(ABOUT_FIRST, e.name.c_str(), e.code);
        w["id"] = sid;
        // (e["name"].upper()[:20], e["short"].upper()[:10]: on the code points)
        w["name"] = utf8(head(py_upper(from_utf8(e.name)), 20)); w["short_name"] = utf8(head(py_upper(from_utf8(e.short_name)), 10));
        w["nick"] = nullptr;
        w["height"] = e.height ? ojson(*e.height) : ojson(nullptr);
        w["weight"] = e.weight ? ojson(*e.weight) : ojson(nullptr);
        w["based_on"] = strf("0x%04X", ref_id);                       // (--legacy-ids: a number; its place the string ID's)
        ojson s = ojson::object();
        s["game"] = "WCW/nWo Revenge"; s["handler"] = "revenge (prototype 2026-11-12)"; s["code"] = strf("0x%03X", e.code);
        s["moveset_file"] = strf("0x%X", *e.moveset_file);
        ojson cs = ojson::array();
        for (auto& c : e.costumes) { ojson o = ojson::object(); o["mask"] = c.mask; o["attire"] = c.attire; o["colours"] = c.colours; cs.push_back(o); }
        s["costumes"] = cs; s["model"] = "needs a capture";
        // body_source: Revenge's body, the No Mercy type it became and why, what that type picks, both games' size for him
        ojson b = ojson::object();
        b["word"] = strf("0x%04X", e.body.word); b["frame"] = e.body.frame;
        b["frame_attires"] = e.body.frame_attires ? ojson(*e.body.frame_attires) : ojson(nullptr);
        b["scale_class"] = e.body.scale_class; b["scale"] = e.body.scale;
        int belt = NM_BELT[body];
        b["nm_body_type"] = body; b["nm_name"] = nm_name(body); b["why"] = body_why; b["nm_belt"] = belt; b["nm_hand_class"] = NM_HAND_CLASS[belt];
        b["nm_height_scale"] = nm_scale(e.height ? *e.height : height_for_scale(e.body.scale));
        int size = nm_size_for_scale(e.body.scale);
        b["nm_size"] = size; b["nm_size_scale"] = scale_of_size(size);
        s["body"] = b;
        w["source"] = s;
        // his looks: his base's four costumes (a woman's: No Mercy's ordinal 51's) with his own body type
        ojson looks = ojson::array();
        for (auto l : base_looks(van, nm, woman ? NM_WOMAN_LOOKS : ref)) { l[0] = body; looks.push_back(l); }
        w["looks"] = looks;
        w["size"] = size;
        if (woman) w["source"]["looks_from"] = strf("No Mercy's ordinal %d (a woman: type 15)", NM_WOMAN_LOOKS);
        if (woman) {
            w["height"] = NM_WOMAN_HEIGHT; w["weight"] = NM_WOMAN_WEIGHT;
            w["source"]["height_from"] = f2(HW_WOMAN_WHY, e.body.scale, NM_WOMAN_HEIGHT, NM_WOMAN_WEIGHT);
        } else if (!e.weight) {
            w["height"] = NM_STAFF_HEIGHT; w["weight"] = NM_STAFF_WEIGHT;
            w["source"]["height_from"] = f2(HW_STAFF_WHY, e.body.scale, NM_STAFF_HEIGHT, NM_STAFF_WEIGHT);
        }
        if (w["height"].is_null()) {
            w["height"] = height_for_scale(e.body.scale);
            w["source"]["height_from"] = strf(HEIGHT_WHY, e.body.scale);
        }
        for (const char* k : { "height", "weight" }) if (w[k].is_null()) w.erase(k);
        if (mp && mp->moveset) w["moveset"] = *mp->moveset;          // (the moveset's piece: the packed moveset)
        w["id"] = next_legacy(sid); w["string_id"] = sid; w["based_on"] = strf("0x%04X", ref_id);
        if (M) {
            w["about"] = strf(ABOUT_REST, e.name.c_str(), e.code);
            w["source"]["photo"] = PHOTO_HOW;
            w["model"] = "model/model.json"; w["source"]["model"] = "rig/capture_all.py captures";
        }
        if (mp && mp->win_pose) w["source"]["win_pose"] = *mp->win_pose;   // (the moveset's pieces: its notes)
        if (mp && mp->abilities) w["source"]["abilities"] = *mp->abilities;
        // ---- the folder ----
        fs::path d = out / sid;
        fs::create_directories(d);
        auto tw = now();
        if (M) { write_new_file(d / "photo.bin", photo); model_write(d, M->descs, M->pool); }
        t_write += secs(tw, now());
        if (mp && mp->moves_json) write_new_file(d / "moves.json", *mp->moves_json);
        // theme.py write: Revenge's own entrance song 0x214 -- unless the folder has a theme already (theme.*: never written over)
        std::vector<std::string> have;
        for (auto& f : fs::directory_iterator(d)) {
            std::string n = f.path().filename().string(), l = n;
            for (auto& ch : l) ch = (char)std::tolower((unsigned char)ch);
            if (l.rfind("theme.", 0) == 0) have.push_back(n);
        }
        std::string how;
        if (!have.empty()) {
            std::sort(have.begin(), have.end());
            how = "his own theme kept (";
            for (size_t i = 0; i < have.size(); i++) how += (i ? ", " : "") + have[i];
            how += ")";
        } else {
            // theme.py write_own: Revenge Redux's song for him when the player has the Redux patch and Redux gives him one (a
            // built-in port by his name -- reduxmusic.py of_name -- any other man by his model code), else Revenge's own 0x214
            std::optional<int> song;
            if (auto rx = R.rom("revenge_redux")) {
                bool port = false;
                for (auto& p : redux_ports()) if (norm_name(p.name) == norm_name(e.name)) port = true;
                song = port ? redux_song_of(*rx, e.name) : redux_song_for(*rx, e.code);
            }
            if (song) {
                auto it = redux_themes.find(*song);
                if (it == redux_themes.end()) it = redux_themes.emplace(*song, redux_theme_files(*R.rom("revenge_redux"), *song)).first;
                write_new_file(d / "theme.bin", it->second.bin); write_new_file(d / "theme.meta", it->second.meta);
                how = it->second.about;
            } else {
                if (!theme) theme = revenge_theme_files(*R.rom("revenge"), 0x214);
                write_new_file(d / "theme.bin", theme->bin); write_new_file(d / "theme.meta", theme->meta);
                how = theme->about;
            }
        }
        w["source"]["theme"] = how;
        w["music"] = REV_MUSIC_SLOT; w["source"]["music"] = MUSIC_WHY;
        // (a woman's or a non-wrestler's No Mercy weight code last, where the installed folders have it)
        if (!e.weight && w.contains("weight")) { ojson v = w["weight"]; w.erase("weight"); w["weight"] = v; }
        { auto c = rev_class(e.code); w["class"] = c.first; w["class_from"] = c.second; }   // (rostertags, 2026-11-21: what he is -- last)
        write_new_file(d / "wrestler.json", py_dumps(w, PyDump{ 1 }));      // (json.dump(indent=1), "\n" line ends: --refresh's write)
        made.insert(sid);
        res.made.push_back(sid);
        std::string line = strf("%s: code 0x%03X, model %s, photo, theme", sid.c_str(), e.code, M ? "his own (Revenge's code)" : "NONE");
        for (auto& r : rep) line += " | " + r;
        res.log.push_back(line);
    }
    res.log.push_back(strf("(time: models %.1f s, photos %.1f s, model files %.1f s)", t_model, t_photo, t_write));
    return res;
}

}  // namespace ri
