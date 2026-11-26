// ri_place.cpp (phase3, 2026-11-19): imported folders moved in under the user's rules (ri_place.h). place_batch and the fresh
// ranges: startup 2026-11-19.
#include "ri_place.h"
#include "ri_json.h"
#include <algorithm>

namespace ri {

static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

static bool parse_number(const std::string& s, uint32_t& v) {
    if (s.size() < 3 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return false;
    char* end = nullptr;
    unsigned long x = strtoul(s.c_str() + 2, &end, 16);
    if (!end || *end) return false;
    v = (uint32_t)x;
    return true;
}

void Taken::scan(const fs::path& root) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().filename() != "wrestler.json") continue;
        Bytes b;
        if (!read_file_if(it->path(), b)) continue;
        std::string id;
        try {
            auto j = ojson::parse(std::string(b.begin(), b.end()));
            if (j.is_object() && j.contains("id") && j["id"].is_string()) id = j["id"].get<std::string>();
        } catch (...) { continue; }
        // (no "id": his ID is his folder's name in small letters -- the wrestlers README)
        if (id.empty()) id = lower(it->path().parent_path().filename().string());
        ids.insert(lower(id));
        uint32_t v;
        if (parse_number(id, v)) numbers.insert(v);
    }
}

std::string Taken::fresh_number(uint32_t from) {
    uint32_t v = from;
    while (numbers.count(v) || ids.count(lower(strf("0x%04X", v)))) v++;
    numbers.insert(v);
    std::string s = strf("0x%04X", v);
    ids.insert(lower(s));
    return s;
}

// ---- fresh numbers (startup 2026-11-19) ----------------------------------------------------------------------------------
// Every importer's OWN numbers -- what each pins in its wrestler.json, frozen so saves, presets, stories and QA keep naming
// the same man. A fresh number never lands in any of these, even for an importer whose ROM isn't there yet (a ROM added
// later brings its men with their own numbers; one given away now would make a second clash then). A new source-game
// handler that pins numbers adds its range HERE (and it must stay out of FRESH below).
struct NumRange { uint32_t lo, hi; const char* who; };
static const NumRange IMPORTER_NUMBERS[] = {
    { 0x0340, 0x038D, "Revenge (handlers\\revenge\\legacy_ids.json, frozen)" },
    { 0x0390, 0x0390, "Goldust (No Mercy Plus V3 + Badd Blood)" },
    { 0x0391, 0x0391, "Vader (Badd Blood)" },
    { 0x03A0, 0x03EB, "WrestleMania 2000 (0x03A0 + its roster index 0 .. 0x4B: ri_wmimport.cpp)" },
};
// Where fresh numbers come from, in this order. The rule: first the lowest free number ABOVE every importer's range
// (0x03EC .. 0x03FF: the importers' own group 3, so a renumbered import stays beside his batch); past that the upper halves
// of the wrestlers' groups 4, 5, 6, 0, 1, 2 (0xN80 .. 0xNFF: the built-ins sit at 0xN01 .. 0xN1B and are frozen for good,
// the user's D1). Never group 7 (0x7xx counts as a woman for the titles: mod logic_v3.c), 8 / 9 (the game's managers and
// officials), 0xA / 0xB (its Create-a-Wrestlers), 0xC00 on (0xC00 is a special record in the game: mod roster.c
// roster_build_record) and never 0x1000 or more (the game's IDs are 12 bits: arcade_wrestlers.cpp, arcade_pages.cpp).
// The game's own loader gives a folder with no number one in groups 0-6 too (arcade_wrestlers.cpp free_id), so these are
// numbers the game already plays folder wrestlers under.
static const NumRange FRESH[] = {
    { 0x03EC, 0x03FF, "above every importer's" },
    { 0x0480, 0x04FF, "group 4's upper half" }, { 0x0580, 0x05FF, "group 5's upper half" },
    { 0x0680, 0x06FF, "group 6's upper half" }, { 0x0080, 0x00FF, "group 0's upper half" },
    { 0x0180, 0x01FF, "group 1's upper half" }, { 0x0280, 0x02FF, "group 2's upper half" },
};
static bool importer_owns(uint32_t v) {
    for (const NumRange& r : IMPORTER_NUMBERS) if (v >= r.lo && v <= r.hi) return true;
    return false;
}

std::string Taken::fresh_import_number() {
    for (const NumRange& r : FRESH)
        for (uint32_t v = r.lo; v <= r.hi; v++) {
            if (importer_owns(v)) continue;           // (never, as the tables stand: kept so a range added above is never given)
            const std::string s = strf("0x%04X", v);
            if (numbers.count(v) || ids.count(lower(s))) continue;
            numbers.insert(v);
            ids.insert(lower(s));
            return s;
        }
    return std::string();
}

bool Taken::holds(const std::string& id) const {
    uint32_t v;
    return ids.count(lower(id)) || (parse_number(id, v) && numbers.count(v));
}

void Taken::reserve(const std::string& id) {
    ids.insert(lower(id));
    uint32_t v;
    if (parse_number(id, v)) numbers.insert(v);
}

// The folder's place under wrestlers_root\collection: his own name, else <name>_<source>, else <name>_<source>~2, ~3 ...
static std::string free_folder_name(const fs::path& dir, const std::string& name, const std::string& source_tag) {
    std::error_code ec;
    std::string use = name;
    if (fs::exists(dir / use, ec)) {
        use = name + "_" + source_tag;
        for (int k = 2; fs::exists(dir / use, ec); k++) use = name + "_" + source_tag + "~" + std::to_string(k);
    }
    return use;
}

// Writes the new "id" into the built folder's wrestler.json, as the importer writes it (indent 1, as Python's
// json.dump(indent=1)), keeping a last newline if it had one.
static void rewrite_id(const fs::path& wj, const std::string& new_id) {
    Bytes b;
    if (!read_file_if(wj, b)) return;
    ojson j = ojson::parse(std::string(b.begin(), b.end()));
    j["id"] = new_id;
    PyDump h; h.indent = 1; h.ensure_ascii = true;
    std::string text = py_dumps(j, h);
    if (!b.empty() && b.back() == '\n') text += '\n';
    write_file(wj, text);
}

// Moves the built folder in as dir\use and says in the report what changed (folder name and/or ID).
static PlaceResult move_in(const fs::path& built, const fs::path& dir, const std::string& collection, const std::string& name,
                           const std::string& use, const std::string& old_id, const std::string& new_id,
                           std::vector<std::string>& report) {
    PlaceResult r;
    std::error_code ec;
    fs::rename(built, dir / use, ec);
    if (ec) {      // (another drive, or held: copy, then remove the temporary one)
        ec.clear();
        fs::copy(built, dir / use, fs::copy_options::recursive, ec);
        if (ec) { r.note = "can't move " + built.string() + ": " + ec.message(); return r; }
        fs::remove_all(built, ec);
    }
    r.placed = true; r.where = dir / use;
    if (use != name || !new_id.empty()) {
        std::string line = collection + "\\" + name + ": ";
        if (use != name) line += "a folder of that name is there already (yours: left as it is) -> imported as " + use;
        if (!new_id.empty()) line += std::string(use != name ? "; " : "") + "his ID " + old_id + " is taken -> " + new_id;
        // (startup 2026-11-19) the string ID he goes by: what finds him wherever a roster, story or save names him with it
        // (a game number there is only checked against it -- the user: references fall back to the string ID)
        if (!new_id.empty() && new_id.rfind("0x", 0) == 0) line += " (his string ID: " + lower(use) + ")";
        report.push_back(line);
        r.note = line;
    }
    return r;
}

std::vector<PlaceResult> place_batch(const fs::path& tmp, const std::vector<std::pair<std::string, std::string>>& made,
                                     const fs::path& root, const std::string& source_tag, Taken& taken,
                                     std::vector<std::string>& report) {
    struct One { std::string old_id, new_id; bool numeric = false, clash = false; };
    std::vector<One> v(made.size());
    // 1. every man's own ID, read before anything is placed
    for (size_t i = 0; i < made.size(); i++) {
        Bytes b;
        if (!read_file_if(tmp / made[i].first / made[i].second / "wrestler.json", b)) continue;
        try {
            const ojson j = ojson::parse(std::string(b.begin(), b.end()));
            if (j.is_object() && j.contains("id") && j["id"].is_string()) v[i].old_id = j["id"].get<std::string>();
        } catch (...) { continue; }      // (a file that isn't JSON: moved in as it is, its ID unread)
        uint32_t n;
        v[i].numeric = parse_number(v[i].old_id, n);
    }
    // 2. reserve the batch's own IDs: a man keeps his unless an existing folder (or an earlier man of this batch) has it
    for (One& o : v) {
        if (o.old_id.empty()) continue;
        if (taken.holds(o.old_id)) o.clash = true;
        else taken.reserve(o.old_id);
    }
    // 3. the clashing ones get fresh IDs, after the whole batch: a number from the fresh ranges, or a string ID from his
    //    folder's name (as place_folder does)
    std::vector<std::string> use(made.size());
    for (size_t i = 0; i < made.size(); i++) {
        use[i] = free_folder_name(root / made[i].first, made[i].second, source_tag);
        One& o = v[i];
        if (!o.clash) continue;
        if (o.numeric) {
            o.new_id = taken.fresh_import_number();
            if (o.new_id.empty()) report.push_back(made[i].first + "\\" + made[i].second + ": his ID " + o.old_id +
                                                   " is taken and no fresh number is left: his folder keeps it (the game gives him another as it loads)");
        } else {
            o.new_id = lower(use[i]);
            for (int k = 2; taken.ids.count(o.new_id); k++) o.new_id = lower(use[i]) + "~" + std::to_string(k);
            taken.ids.insert(o.new_id);
        }
    }
    // 4. place each
    std::vector<PlaceResult> out;
    for (size_t i = 0; i < made.size(); i++) {
        const auto& [collection, name] = made[i];
        std::error_code ec;
        fs::create_directories(root / collection, ec);
        const fs::path built = tmp / collection / name;
        if (!v[i].new_id.empty()) rewrite_id(built / "wrestler.json", v[i].new_id);
        out.push_back(move_in(built, root / collection, collection, name, use[i], v[i].old_id, v[i].new_id, report));
    }
    return out;
}

PlaceResult place_folder(const fs::path& built, const fs::path& root, const std::string& collection, const std::string& name,
                         const std::string& source_tag, Taken& taken, std::vector<std::string>& report) {
    PlaceResult r;
    std::error_code ec;
    const fs::path dir = root / collection;
    fs::create_directories(dir, ec);
    // a free folder name: his own, else <name>_<source>, else <name>_<source>~2, ~3 ...
    std::string use = name;
    if (fs::exists(dir / use, ec)) {
        use = name + "_" + source_tag;
        for (int k = 2; fs::exists(dir / use, ec); k++) use = name + "_" + source_tag + "~" + std::to_string(k);
    }
    // his ID: kept unless a folder has it already
    const fs::path wj = built / "wrestler.json";
    Bytes b;
    std::string old_id, new_id;
    if (read_file_if(wj, b)) {
        ojson j = ojson::parse(std::string(b.begin(), b.end()));
        if (j.is_object() && j.contains("id") && j["id"].is_string()) {
            old_id = j["id"].get<std::string>();
            uint32_t v;
            const bool numeric = parse_number(old_id, v);
            if (taken.ids.count(lower(old_id)) || (numeric && taken.numbers.count(v))) {
                // (startup 2026-11-19: a fresh number from the fresh ranges, as place_batch gives -- never the next free one
                //  after his, which another import may pin)
                new_id = numeric ? taken.fresh_import_number() : lower(use);
                if (numeric && new_id.empty()) new_id = taken.fresh_number(v);
                if (!numeric) {
                    for (int k = 2; taken.ids.count(new_id); k++) new_id = lower(use) + "~" + std::to_string(k);
                    taken.ids.insert(new_id);
                }
                j["id"] = new_id;
                // (written as the importer writes it: indent 1, as Python's json.dump(indent=1))
                PyDump h; h.indent = 1; h.ensure_ascii = true;
                std::string text = py_dumps(j, h);
                bool nl = !b.empty() && b.back() == '\n';
                if (nl) text += '\n';
                write_file(wj, text);
            } else {
                taken.ids.insert(lower(old_id));
                if (numeric) taken.numbers.insert(v);
            }
        }
    }
    fs::rename(built, dir / use, ec);
    if (ec) {      // (another drive, or held: copy, then remove the temporary one)
        ec.clear();
        fs::copy(built, dir / use, fs::copy_options::recursive, ec);
        if (ec) { r.note = "can't move " + built.string() + ": " + ec.message(); return r; }
        fs::remove_all(built, ec);
    }
    r.placed = true; r.where = dir / use;
    if (use != name || !new_id.empty()) {
        std::string line = collection + "\\" + name + ": ";
        if (use != name) line += "a folder of that name is there already (yours: left as it is) -> imported as " + use;
        if (!new_id.empty()) line += std::string(use != name ? "; " : "") + "his ID " + old_id + " is taken -> " + new_id;
        report.push_back(line);
        r.note = line;
    }
    return r;
}

}  // namespace ri
