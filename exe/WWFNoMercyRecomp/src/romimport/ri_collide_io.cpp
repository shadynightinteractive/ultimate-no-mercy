// ri_collide_io.cpp (phase3 p3collide, 2026-11-19): mod/gen/arenas_collide.py around its computation -- which models each
// arena's stage set and crowd are (arena_list, from the mod's generated headers), their bytes (Models), the result's hash
// (key_of), and data/stagecollide/result/<n>.json's text both ways (main's json.dump; arenas_walls.py's json.load).
// The computation itself is ri_collide.cpp (ri_collide.h says what it all is).
#include "ri_collide.h"

#include <cctype>
#include <cstring>

#include "ri_core.h"
#include "ri_json.h"
#include "ri_roms.h"

namespace ri {
namespace collide {
namespace {

// ---- reading the headers as the Python's regular expressions do -------------------------------------------------------

// re.findall(r"0x[0-9A-Fa-f]+|\d+", s), each int(x, 0) (arenas_collide.py nums).
std::vector<int> nums(const std::string& s) {
    std::vector<int> out;
    size_t i = 0;
    while (i < s.size()) {
        if (s.compare(i, 2, "0x") == 0 && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 2])) {
            size_t j = i + 2;
            while (j < s.size() && std::isxdigit((unsigned char)s[j])) j++;
            out.push_back(int(std::stol(s.substr(i + 2, j - i - 2), nullptr, 16)));
            i = j;
        } else if (std::isdigit((unsigned char)s[i])) {
            size_t j = i;
            while (j < s.size() && std::isdigit((unsigned char)s[j])) j++;
            // (int(x, 0) refuses a decimal with a leading 0 other than "0"s: the Python would stop)
            const std::string d = s.substr(i, j - i);
            if (d.size() > 1 && d[0] == '0' && d.find_first_not_of('0') != std::string::npos) fail("arenas_collide: number %s", d.c_str());
            out.push_back(int(std::stol(d)));
            i = j;
        } else {
            i++;
        }
    }
    return out;
}

// One part of a declaration's head, as the Python's patterns have them after the name.
enum class Dim { Any, Digits, Literal };
struct Part {
    Dim kind;
    const char* text;   // for Literal
};

// re.search(<name> <dims> " = \{" "(.*?)" <end>, t, re.S).group(1): the first place `name` is followed by its dims
// ("[...]" each: [^\]]* for Any, \d+ for Digits, the text for Literal) and " = {", then the text up to the first `end`.
// Fails (as the Python's .group on None would) if there's none.
std::string decl_body(const std::string& t, const std::string& name, const std::vector<Part>& dims, const char* end) {
    size_t from = 0;
    while (true) {
        const size_t at = t.find(name, from);
        if (at == std::string::npos) fail("arenas_collide: no %s in the header", name.c_str());
        from = at + 1;
        size_t p = at + name.size();
        bool ok = true;
        for (const Part& d : dims) {
            if (p >= t.size() || t[p] != '[') { ok = false; break; }
            p++;
            if (d.kind == Dim::Any) {
                while (p < t.size() && t[p] != ']') p++;
            } else if (d.kind == Dim::Digits) {
                const size_t q = p;
                while (p < t.size() && std::isdigit((unsigned char)t[p])) p++;
                if (p == q) { ok = false; break; }
            } else {
                if (t.compare(p, std::strlen(d.text), d.text) != 0) { ok = false; break; }
                p += std::strlen(d.text);
            }
            if (p >= t.size() || t[p] != ']') { ok = false; break; }
            p++;
        }
        if (!ok || t.compare(p, 4, " = {") != 0) continue;
        p += 4;
        const size_t e = t.find(end, p);
        if (e == std::string::npos) continue;
        return t.substr(p, e - p);
    }
}

// re.findall(r"\{([^{}]*)\}", s): every innermost brace pair's text.
std::vector<std::string> brace_groups(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '{') {
            size_t j = i + 1;
            while (j < s.size() && s[j] != '{' && s[j] != '}') j++;
            if (j < s.size() && s[j] == '}') {
                out.push_back(s.substr(i + 1, j - i - 1));
                i = j + 1;
                continue;
            }
        }
        i++;
    }
    return out;
}

// re.findall(r'"([^"]+)"', s): the quoted names.
std::vector<std::string> quoted(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '"') {
            const size_t j = s.find('"', i + 1);
            if (j == std::string::npos) break;
            if (j > i + 1) {
                out.push_back(s.substr(i + 1, j - i - 1));
                i = j + 1;
                continue;
            }
        }
        i++;
    }
    return out;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// One (\d+|0x[0-9A-Fa-f]+) at p, as the regular expression takes it before what must follow it (comma_next: a ',', else
// whitespace then '}'): the decimal run if that follows it, else 0x and hex digits. -1 if neither (p moved past it).
long long pair_number(const std::string& s, size_t& p, bool comma_next) {
    auto after_ok = [&](size_t q) {
        if (comma_next) return q < s.size() && s[q] == ',';
        while (q < s.size() && is_space(s[q])) q++;
        return q < s.size() && s[q] == '}';
    };
    size_t q = p;
    while (q < s.size() && std::isdigit((unsigned char)s[q])) q++;
    if (q > p && after_ok(q)) {
        const long long v = std::stoll(s.substr(p, q - p));
        p = q;
        return v;
    }
    if (s.compare(p, 2, "0x") == 0) {
        q = p + 2;
        while (q < s.size() && std::isxdigit((unsigned char)s[q])) q++;
        if (q > p + 2 && after_ok(q)) {
            const long long v = std::stoll(s.substr(p + 2, q - p - 2), nullptr, 16);
            p = q;
            return v;
        }
    }
    return -1;
}

// Models.__init__'s table: re.search(r"wmp_files\[WMP_FILES_N\]\[2\] = \{.*?\n(.*?)\n\};", wf, re.S), then every
// re.findall(r"\{\s*(\d+|0x...),\s*(\d+|0x...)\s*\}") in it.
std::vector<std::pair<long long, long long>> wmp_files(const std::string& wf) {
    const std::string head = "wmp_files[WMP_FILES_N][2] = {";
    const size_t at = wf.find(head);
    if (at == std::string::npos) fail("arenas_collide: no wmp_files in wmport_files.h");
    const size_t nl = wf.find('\n', at + head.size());
    if (nl == std::string::npos) fail("arenas_collide: wmp_files has no rows");
    const size_t end = wf.find("\n};", nl + 1);
    if (end == std::string::npos) fail("arenas_collide: wmp_files has no end");
    const std::string body = wf.substr(nl + 1, end - nl - 1);
    std::vector<std::pair<long long, long long>> out;
    size_t i = 0;
    while (i < body.size()) {
        if (body[i] == '{') {
            size_t p = i + 1;
            while (p < body.size() && is_space(body[p])) p++;
            const long long a = pair_number(body, p, true);
            if (a >= 0) {
                p++;   // (the comma)
                while (p < body.size() && is_space(body[p])) p++;
                const long long b = pair_number(body, p, false);
                if (b >= 0) {
                    while (p < body.size() && is_space(body[p])) p++;
                    out.push_back({a, b});
                    i = p + 1;   // (past the '}')
                    continue;
                }
            }
        }
        i++;
    }
    return out;
}

const char* const GAME_NAMES[10] = {"RAW IS WAR", "NO MERCY", "SMACKDOWN", "KING OF THE RING", "SUMMERSLAM", "SURVIVOR SERIES",
                                    "ROYAL RUMBLE", "WRESTLEMANIA", "BACKLASH", "ARMAGEDDON"};

std::vector<int> head_of(const std::vector<int>& v, int n) {   // v[:n]
    if (n < 0) n = std::max(0, int(v.size()) + n);
    return std::vector<int>(v.begin(), v.begin() + std::min<size_t>(v.size(), size_t(n)));
}

}  // namespace

// ---- arenas_collide.py arena_list --------------------------------------------------------------------------------------

std::vector<ArenaInput> arena_list(const HeaderTexts& h, const Bytes& nomercy_rom) {
    // the game's crowd and barricades: arenas.py main_data -- main at 0x80000450 from ROM 0x1050
    auto main_u16 = [&](uint32_t o, int n) {
        std::vector<int> out;
        const size_t at = 0x1050u + (0x80050000u + o) - 0x80000450u;
        if (at + 2 * size_t(n) > nomercy_rom.size()) fail("arenas_collide: the No Mercy ROM is short");
        for (int i = 0; i < n; i++) out.push_back(be16(nomercy_rom, at + 2 * i));
        return out;
    };
    std::vector<int> game_crowd;
    const std::pair<uint32_t, int> lists[] = {{0x70, 10}, {0xF8, 15}, {0x1BC, 3}, {0x1DC, 9}, {0x220, 22}, {0x2AC, 22}};
    for (const auto& [o, n] : lists) {
        const std::vector<int> part = main_u16(o, n);
        game_crowd.insert(game_crowd.end(), part.begin(), part.end());
    }
    std::vector<ArenaInput> out;
    // (arenasplit 2026-11-24) A pack made from one of WCW/nWo Revenge and WrestleMania 2000 has no stage sets of the game's ten
    // (the game reads those from content\wmport_base.bin) and so no text of arena_raw_files.h / arena_stage_files.h: the
    // arcade's arenas alone, then. With the texts, as always.
    if (!h.arena_raw_files.empty()) {
    // RAW IS WAR: xraw_set_mesh, whole
    out.push_back({0, GAME_NAMES[0], 1500, nums(decl_body(h.arena_raw_files, "xraw_set_mesh", {{Dim::Literal, "XRAW_SET_N"}}, "}")), game_crowd});
    // the other nine: xstageV_mesh[:xstage_n[v]]
    const std::vector<int> stage_n = nums(decl_body(h.arena_stage_files, "xstage_n", {{Dim::Any, nullptr}}, "}"));
    for (int v = 1; v < 10; v++) {
        if (size_t(v) >= stage_n.size()) fail("arenas_collide: xstage_n is short");
        const std::vector<int> ids = nums(decl_body(h.arena_stage_files, "xstage" + std::to_string(v) + "_mesh", {{Dim::Any, nullptr}}, "}"));
        out.push_back({v, GAME_NAMES[v], 1500, head_of(ids, stage_n[v]), game_crowd});
    }
    }
    // the arcade's: xring_set_mesh[k][:xring_set_n[k]], the crowd the barricade's, the stands' and g1 .. g4's rows k
    auto ring = [&](const char* name, size_t k) {
        const std::string body = decl_body(h.arena_ring_files, std::string("xring_") + name, {{Dim::Literal, "XRING_N"}, {Dim::Digits, nullptr}}, "};");
        const std::vector<std::string> rows = brace_groups(body);
        if (k >= rows.size()) fail("arenas_collide: xring_%s has no row %d", name, int(k));
        return nums(rows[k]);
    };
    const std::vector<int> set_n = nums(decl_body(h.arena_ring_files, "xring_set_n", {{Dim::Literal, "XRING_N"}}, "}"));
    const std::vector<std::string> names = quoted(decl_body(h.arena_files, "xarena_names", {{Dim::Literal, "XARENA_N"}}, "}"));
    for (size_t k = 0; k < names.size(); k++) {
        std::vector<int> crowd;
        for (const char* x : {"barricade_mesh", "stands_mesh", "g1_mesh", "g2_mesh", "g3_mesh", "g4_mesh"}) {
            const std::vector<int> row = ring(x, k);
            crowd.insert(crowd.end(), row.begin(), row.end());
        }
        if (k >= set_n.size()) fail("arenas_collide: xring_set_n is short");
        out.push_back({10 + int(k), names[k], 0, head_of(ring("set_mesh", k), set_n[k]), crowd});
    }
    return out;
}

// ---- arenas_collide.py Models ------------------------------------------------------------------------------------------

Models::Models(const Bytes& pack, const std::string& wmport_files_h, FileOf nm_file)
    : pack_(pack), files_(wmp_files(wmport_files_h)), nm_file_(std::move(nm_file)) {}

Bytes Models::data(int id) const {
    if (id >= 0x5000) {
        // self.pack[o:o + n] (a slice: cut short at the pack's end)
        const size_t i = size_t(id - 0x5000);
        if (i >= files_.size()) fail("arenas_collide: model file 0x%X isn't in wmport_files.h", id);
        const size_t o = size_t(std::min<long long>(files_[i].first, (long long)pack_.size()));
        const size_t e = size_t(std::min<long long>(files_[i].first + files_[i].second, (long long)pack_.size()));
        return Bytes(pack_.begin() + o, pack_.begin() + std::max(o, e));
    }
    const Bytes* d = nm_file_ ? nm_file_(id) : nullptr;
    if (!d) fail("arenas_collide: No Mercy's file 0x%X isn't there", id);
    return *d;
}

std::vector<Tri> Models::tris(const std::vector<int>& ids, int dz, std::optional<int> zmin) const {
    std::vector<Tri> out;
    for (int id : ids)
        if (!add_tris(data(id), dz, zmin, out)) fail("arenas_collide: model file 0x%X is short", id);
    return out;
}

// ---- arenas_collide.py key_of, main ------------------------------------------------------------------------------------

std::string key_of(const Models& M, const ArenaInput& a, const Bytes* py_source) {
    if (!py_source) return "";
    const std::string params = strf("%d %d %d %d %d %d %d %d", CELL, GRID, STEP, BODY0, BODY1, LOW_TOP, ROOM, int(SIMPLIFY));
    Bytes all(params.begin(), params.end());
    all.insert(all.end(), py_source->begin(), py_source->end());
    for (const std::vector<int>* list : {&a.ids, &a.crowd})
        for (int id : *list) {
            const Bytes d = M.data(id);
            all.insert(all.end(), d.begin(), d.end());
        }
    const std::string dz = std::to_string(a.dz);   // str(dz)
    all.insert(all.end(), dz.begin(), dz.end());
    return sha1_hex(all);
}

Collide collide_arena(const Models& M, const ArenaInput& a, const Bytes* py_source) {
    const std::vector<Tri> set_tris = M.tris(a.ids, a.dz, std::nullopt);
    const std::vector<Tri> crowd_tris = M.tris(a.crowd, 0, 300);
    Collide c;
    std::string why;
    if (!result_of(set_tris, crowd_tris, c, why)) fail("arenas_collide: arena %d (%s): %s", a.n, a.name.c_str(), why.c_str());
    c.key = key_of(M, a, py_source);
    c.name = a.name;
    c.n = a.n;
    return c;
}

std::vector<Collide> collide_all(const Bytes& pack, const HeaderTexts& h, Roms& R, const Bytes* py_source) {
    auto rom = R.rom("nomercy");
    const RomFiles* files = R.files("nomercy");
    if (!rom || !files) fail("arenas_collide: no No Mercy ROM");
    Models M(pack, h.wmport_files, [files](int id) { return id >= 1 ? files->get(uint32_t(id)) : nullptr; });
    std::vector<Collide> out;
    for (const ArenaInput& a : arena_list(h, *rom)) {
        // (arenasplit) an arcade arena whose source game isn't there: its row zero in the headers, nothing to collide -- no
        //  result (every arena of a full pack has a stage set)
        if (a.ids.empty()) continue;
        out.push_back(collide_arena(M, a, py_source));
    }
    return out;
}

// ---- the result file ---------------------------------------------------------------------------------------------------

namespace {

ojson point_json(IPt p) { return ojson::array({p.first, p.second}); }   // (a tuple: a list)

ojson walls_json(const std::vector<Wall>& ws) {
    ojson out = ojson::array();
    for (const Wall& w : ws) {
        ojson d = ojson::object();
        d["a"] = point_json(w.a);
        d["b"] = point_json(w.b);
        d["crash"] = w.crash;
        out.push_back(d);
    }
    return out;
}

IPt point_of(const ojson& j) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number_integer() || !j[1].is_number_integer()) fail("stagecollide result: a point isn't [x, z]");
    return {j[0].get<int>(), j[1].get<int>()};
}

std::vector<Wall> walls_of(const ojson& j) {
    if (!j.is_array()) fail("stagecollide result: walls aren't a list");
    std::vector<Wall> out;
    for (const ojson& w : j) out.push_back({point_of(w.at("a")), point_of(w.at("b")), w.at("crash").get<int>()});
    return out;
}

}  // namespace

// The dict main writes: result_of's (its order: path, props, front, grid, bbox, crowd_z, crowd_y, ymax, ymin, n_walls,
// walk_cells), then key, name. ymax / ymin are floats (the ground's heights), everything else ints (None for a map cell
// nobody walks on).
std::string result_json(const Collide& c) {
    ojson d = ojson::object();
    d["path"] = walls_json(c.path);
    ojson props = ojson::array();
    for (const auto& p : c.props) props.push_back(walls_json(p));
    d["props"] = props;
    d["front"] = c.front;
    ojson grid = ojson::object();
    grid["x0"] = c.gx0;
    grid["z0"] = c.gz0;
    grid["w"] = c.gw;
    grid["h"] = c.gh;
    ojson rows = ojson::array();
    for (int k = 0; k < c.gh; k++) {
        ojson row = ojson::array();
        for (int i = 0; i < c.gw; i++) {
            const long long v = c.cell(i, k);
            row.push_back(v == NO_CELL ? ojson(nullptr) : ojson(v));
        }
        rows.push_back(row);
    }
    grid["cells"] = rows;
    d["grid"] = grid;
    d["bbox"] = ojson::array({c.bbox[0], c.bbox[1], c.bbox[2], c.bbox[3]});
    d["crowd_z"] = c.crowd_z;
    d["crowd_y"] = c.crowd_y;
    d["ymax"] = c.ymax;   // (a double: written as Python's float repr, "0.0")
    d["ymin"] = c.ymin;
    d["n_walls"] = c.n_walls;
    d["walk_cells"] = c.walk_cells;
    d["key"] = c.key;
    d["name"] = c.name;
    return py_dumps(d);
}

Collide parse_result_json(const std::string& text) {
    ojson d;
    try {
        d = ojson::parse(text);
    } catch (const std::exception& e) {
        fail("stagecollide result: not JSON (%s)", e.what());
    }
    Collide c;
    try {
        c.path = walls_of(d.at("path"));
        for (const ojson& p : d.at("props")) c.props.push_back(walls_of(p));
        c.front = d.at("front").get<int>();
        const ojson& g = d.at("grid");
        c.gx0 = g.at("x0").get<int>();
        c.gz0 = g.at("z0").get<int>();
        c.gw = g.at("w").get<int>();
        c.gh = g.at("h").get<int>();
        const ojson& rows = g.at("cells");
        if (!rows.is_array() || int(rows.size()) != c.gh) fail("stagecollide result: the map has %d rows, not %d", int(rows.size()), c.gh);
        for (const ojson& row : rows) {
            if (!row.is_array() || int(row.size()) != c.gw) fail("stagecollide result: a map row isn't %d cells", c.gw);
            for (const ojson& v : row) c.cells.push_back(v.is_null() ? NO_CELL : v.get<long long>());
        }
        const ojson& b = d.at("bbox");
        for (int i = 0; i < 4; i++) c.bbox[i] = b.at(i).get<int>();
        c.crowd_z = d.at("crowd_z").get<long long>();
        c.crowd_y = d.at("crowd_y").get<long long>();
        c.ymax = d.at("ymax").get<double>();
        c.ymin = d.at("ymin").get<double>();
        c.n_walls = d.at("n_walls").get<int>();
        c.walk_cells = d.at("walk_cells").get<int>();
        c.key = d.value("key", std::string());
        c.name = d.value("name", std::string());
    } catch (const Error&) {
        throw;
    } catch (const std::exception& e) {
        fail("stagecollide result: %s", e.what());
    }
    return c;
}

}  // namespace collide
}  // namespace ri
