// [arcade] The Superstar Select's pages from a file: <program folder>\select_pages.json (the user, 2026-10-30:
// "any number of pages ... the number of wrestlers on each page modifiable ... the game should not break").
// Read here once, checked, and handed to the mod as a small "pages pack" (roster.c lays the select out from it;
// without the file, or with nothing good in it, the game's own pages as before). The pack, bytes:
//   'P' 'G' 1 <pages>  <flags: 1 = hide the Superstars the pages leave out>  0 0 0
//   then each page: name[16] (NUL-padded, at most 15 letters)  <flags: 1 = not in the Rumble's random draw>
//                   <count 0..9>  count x ID (big-endian u16)
// A page of more than nine goes on over pages of its own ("NAME 2", ...); at most 32 pages (the select's page
// list keeps a page in five bits); a Superstar twice: the first place only. Superstars by string ID ("the_rock", a folder's
// name; caw_1 .. caw_18 the Create-a-Wrestler boxes), or number ("0x0105"); a name ("THE ROCK", the story roster:
// <program folder>\stories\_roster.json) is still read for older files (startup 2026-11-19: the game's own file uses IDs). What's wrong is said in the
// log ([pages] ...) and left out; the mod still checks every ID against the roster as it is.
// (startup 2026-11-19) Or both a string ID and a game number, {"id": "aki_man", "game_number": "0x0379"}: by the number when
// that wrestler is aki_man, else by the string ID (the user: references fall back to the string ID; arcade_roster.h).
// (2026-11-19, roster -- the user: the game's own wrestlers, then ALL the Revenge imports together, then the WrestleMania
// 2000 imports, then everything else) An entry {"game": "WCW/nWo Revenge"} is every folder wrestler imported from that game
// -- his wrestler.json "source": {"game": ...}, the name compared without regard to capitals -- in folder order, each one
// not on a page before; with nobody from it (its ROM not there), nothing. Whoever no page names (Vader, Goldust, other mods'
// or packs' wrestlers) comes after them all ("unlisted": "append": the mod's MORE pages).
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <functional>

#include "json/json.hpp"
#include "arcade_roster.h"      // (startup 2026-11-19: the Roster as arcade_stories.cpp defines it -- this file had its own)

#ifndef ARCADE_PAGES_TEST       // (startup 2026-11-19: built alone for the references' test, main/test/arcade_refs_test.cpp)
#include "arcade_host.h"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#endif

namespace arcade_stories {
Roster load_roster(const std::filesystem::path& path, std::string& err);
}
#ifndef ARCADE_PAGES_TEST
void arcade_wrestlers_each_source(const std::function<void(const std::string&, int)>& use);   // (arcade_wrestlers.cpp)
#else
static void arcade_wrestlers_each_source(const std::function<void(const std::string&, int)>&) {}   // (built alone: no folder wrestlers)
#endif

namespace {

using json = nlohmann::ordered_json;

constexpr int MAX_PAGES = 32, PER_PAGE = 9, NAME_LEN = 15;

std::string upper(const std::string& s) {
    std::string o;
    for (char c : s) o += (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    return o;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}
// a page's name as the select can show it: capitals, digits and a little punctuation; at most 15
std::string page_name(const std::string& s) {
    std::string o;
    for (char c : upper(trim(s))) {
        bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '-' || c == '&' ||
                  c == '!' || c == '\'' || c == '/';
        o += ok ? c : ' ';
    }
    o = trim(o);
    if ((int)o.size() > NAME_LEN) o = trim(o.substr(0, NAME_LEN));
    return o.empty() ? std::string("PAGE") : o;
}

struct Page { std::string name; uint8_t flags = 0; std::vector<uint16_t> ids; };

// a Superstar: a name of the roster, "0x...", or a number -> his ID (0: none)
int who(const json& w, const arcade_stories::Roster& R, std::string& why) {
    // (startup 2026-11-19) Both his string ID and his game number, {"id": "aki_man", "game_number": "0x0379"}: by the number
    // when that wrestler is aki_man, else by the string ID -- the user's fallback (arcade_roster.h resolve_ref). Never by the
    // name on screen. What the fallback did is in the log.
    if (w.is_object()) {
        int number = 0;
        if (w.contains("game_number")) {
            number = who(w["game_number"], R, why);
            if (!number) return 0;
        }
        const std::string sid = w.contains("id") && w["id"].is_string() ? trim(w["id"].get<std::string>()) : std::string();
        if (!number && sid.empty()) { why = w.dump() + " needs \"id\" (his string ID) and/or \"game_number\""; return 0; }
        std::string note;
        const int64_t id = arcade_stories::resolve_ref(R, number, sid, note);
        if (!note.empty()) fprintf(stderr, "[pages] %s: %s\n", w.dump().c_str(), note.c_str());
        if (id > 0 && id < 0x1000) return (int)id;
        why = "no Superstar with the string ID " + sid;
        return 0;
    }
    if (w.is_number_integer()) {
        int64_t v = w.get<int64_t>();
        if (v > 0 && v < 0x1000) return (int)v;
        why = w.dump() + " isn't an ID (1 .. 0xFFF)";
        return 0;
    }
    if (!w.is_string()) { why = w.dump() + " isn't a name or an ID"; return 0; }
    std::string s = trim(w.get<std::string>());
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        char* end = nullptr;
        long v = strtol(s.c_str() + 2, &end, 16);
        if (end && *end == 0 && v > 0 && v < 0x1000) return (int)v;
        why = "\"" + s + "\" isn't an ID (0x1 .. 0xFFF)";
        return 0;
    }
    // (2026-11-12) A string ID first -- a built-in's ("steve_austin", the mod's gen/roster_ids.json) or a folder's ("rock_on_austin",
    // "rock_on_austin~2" for the second of two alike) -- then a name, as before.
    {
        std::string l;
        for (char c : s) l += (c == ' ' || c == '-') ? '_' : (char)tolower((uint8_t)c);
        auto id = R.find("@ID:" + s);
        if (id == R.end()) id = R.find("@ID:" + l);
        if (id != R.end() && id->second > 0 && id->second < 0x1000) return (int)id->second;
        // (startup 2026-11-19) the Create-a-Wrestler boxes by their own string IDs, caw_1 .. caw_18 (the game's EDIT 1 / 2:
        //  shown once made). No built-in or folder has them; a folder named so is found above, as his own.
        if (l.rfind("caw_", 0) == 0) {
            const int k = atoi(l.c_str() + 4);
            if (k >= 1 && k <= 18 && l == "caw_" + std::to_string(k)) return 0xA00 + k;
        }
    }
    // LEGACY (startup 2026-11-19, the user: references by folder / string ID, never the name on screen): the names below and
    // the roster's names are still read, so a player's old select_pages.json keeps working; the game's own file names
    // everyone by string ID (undertaker_1999, chris_benoit, caw_1 ...).
    std::string u = upper(s);
    // (names the story roster hasn't: the 1999 Undertaker -- "UNDERTAKER" is 2000's --, Chris Benoit, the Create-a-Wrestler
    // boxes, CAW 1 .. CAW 18, as on the game's EDIT 1 / 2: shown once made)
    if (u == "UNDERTAKER 1999" || u == "UNDERTAKER '99") return 0x000A;
    if (u == "CHRIS BENOIT" || u == "BENOIT") return 0x0205;   // (his own box, on with the operator menu's Benoit)
    if (u.rfind("CAW ", 0) == 0) {
        int k = atoi(u.c_str() + 4);
        if (k >= 1 && k <= 18 && u == "CAW " + std::to_string(k)) return 0xA00 + k;
    }
    auto it = R.find(u);
    if (it != R.end() && it->second > 0 && it->second < 0x1000) return (int)it->second;
    why = "no Superstar called \"" + s + "\" (" + (std::filesystem::path("stories") / "_roster.json").string() + " has the names)";   // [deck 2026-11-13] the name with this system's folder separator (stories/_roster.json on Linux; on Windows the same text as before)
    return 0;
}

std::vector<uint8_t> build(const std::filesystem::path& file, const std::filesystem::path& roster_file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    json j;
    try { j = json::parse(ss.str(), nullptr, true, true); }   // (comments allowed)
    catch (const json::exception& e) { fprintf(stderr, "[pages] select_pages.json: %s -- the game's own pages\n", e.what()); return {}; }
    if (!j.is_object() || !j.contains("pages") || !j["pages"].is_array()) {
        fprintf(stderr, "[pages] select_pages.json: no \"pages\" list -- the game's own pages\n");
        return {};
    }
    std::string err;
    arcade_stories::Roster R = arcade_stories::load_roster(roster_file, err);
    if (!err.empty()) fprintf(stderr, "[pages] %s: %s (Superstars by ID only)\n", (std::filesystem::path("stories") / "_roster.json").string().c_str(), err.c_str());   // [deck 2026-11-13] the name with this system's folder separator (stories/_roster.json on Linux; on Windows the same text as before)

    uint8_t flags = 0;
    if (j.contains("unlisted")) {
        std::string u = j["unlisted"].is_string() ? upper(j["unlisted"].get<std::string>()) : "";
        if (u == "HIDE") flags |= 1;
        else if (u != "APPEND") fprintf(stderr, "[pages] \"unlisted\": %s isn't \"append\" or \"hide\" -- append\n", j["unlisted"].dump().c_str());
    }
    std::vector<Page> pages;
    std::set<int> seen;
    int n = 0;
    for (const json& p : j["pages"]) {
        n++;
        if (!p.is_object()) { fprintf(stderr, "[pages] page %d isn't {\"name\": ..., \"wrestlers\": [...]}: left out\n", n); continue; }
        std::string name = page_name(p.contains("name") && p["name"].is_string() ? p["name"].get<std::string>() : "PAGE " + std::to_string(n));
        uint8_t pf = (p.contains("rumble") && p["rumble"].is_boolean() && !p["rumble"].get<bool>()) ? 1 : 0;
        std::vector<uint16_t> ids;
        if (p.contains("wrestlers") && p["wrestlers"].is_array()) {
            for (const json& w : p["wrestlers"]) {
                if (w.is_object() && w.contains("game") && w["game"].is_string()) {   // (2026-11-19, roster: a game's imports)
                    const std::string game = upper(trim(w["game"].get<std::string>()));
                    int got = 0;
                    arcade_wrestlers_each_source([&](const std::string& from, int id) {
                        if (game.empty() || upper(trim(from)) != game || id <= 0 || id >= 0x1000) return;
                        if (seen.insert(id).second) { ids.push_back((uint16_t)id); got++; }
                    });
                    fprintf(stderr, "[pages] \"%s\": %d imported from %s\n", name.c_str(), got, w["game"].dump().c_str());
                    continue;
                }
                std::string why;
                int id = who(w, R, why);
                if (!id) { fprintf(stderr, "[pages] \"%s\": %s: left out\n", name.c_str(), why.c_str()); continue; }
                if (!seen.insert(id).second) { fprintf(stderr, "[pages] \"%s\": %s is on an earlier page: left out here\n", name.c_str(), w.dump().c_str()); continue; }
                ids.push_back((uint16_t)id);
            }
        } else fprintf(stderr, "[pages] \"%s\": no \"wrestlers\" list\n", name.c_str());
        if (ids.empty()) { fprintf(stderr, "[pages] \"%s\": nobody on it: left out\n", name.c_str()); continue; }
        for (size_t k = 0; k < ids.size(); k += PER_PAGE) {   // (more than nine: the rest on pages of their own)
            Page pg;
            // (romsets 2026-11-24) the pages after a group's first marked as its own (0x80, the mod's PAGE_CONT): the mod flows the
            //  group's boxes on, nine to a page, past whoever has no box (merged into a family, hidden, removed, not on this install)
            pg.flags = uint8_t(pf | (k ? 0x80 : 0));
            if (k == 0) pg.name = name;
            else {
                std::string suf = " " + std::to_string(k / PER_PAGE + 1);
                pg.name = trim(name.substr(0, std::min(name.size(), (size_t)(NAME_LEN - suf.size())))) + suf;
            }
            pg.ids.assign(ids.begin() + k, ids.begin() + std::min(ids.size(), k + PER_PAGE));
            pages.push_back(pg);
        }
    }
    if ((int)pages.size() > MAX_PAGES) {
        fprintf(stderr, "[pages] %d pages: the select takes %d -- the rest left out\n", (int)pages.size(), MAX_PAGES);
        pages.resize(MAX_PAGES);
    }
    if (pages.empty()) { fprintf(stderr, "[pages] select_pages.json: no page with anyone on it -- the game's own pages\n"); return {}; }
    std::vector<uint8_t> b = { 'P', 'G', 1, (uint8_t)pages.size(), flags, 0, 0, 0 };
    for (const Page& pg : pages) {
        for (int i = 0; i < 16; i++) b.push_back(i < (int)pg.name.size() ? (uint8_t)pg.name[i] : 0);
        b.push_back(pg.flags);
        b.push_back((uint8_t)pg.ids.size());
        for (uint16_t id : pg.ids) { b.push_back(id >> 8); b.push_back(id & 0xFF); }
    }
    fprintf(stderr, "[pages] select_pages.json: %d pages, %d Superstars%s\n", (int)pages.size(), (int)seen.size(),
            (flags & 1) ? " (the rest hidden)" : "");
    return b;
}

#ifndef ARCADE_PAGES_TEST
std::mutex pages_mutex;
std::vector<uint8_t> pack;
bool loaded = false;

// recomp_arcade_pages(dst, maxlen) -> the pages pack's size (copied to dst); 0: no file, nothing good in it, or too big
void pages_get(uint8_t* rdram, recomp_context* ctx) {
    gpr dst = ctx->r4;
    int maxlen = _arg<1, int>(rdram, ctx);
    std::lock_guard<std::mutex> lock(pages_mutex);
    if (!loaded) {
        auto dir = arcade::program_folder();
        pack = build(dir / "select_pages.json", dir / "stories" / "_roster.json");
        loaded = true;
    }
    if (pack.empty() || (int)pack.size() > maxlen) { _return(ctx, 0); return; }
    for (int k = 0; k < (int)pack.size(); k++) MEM_B(k, dst) = pack[k];
    _return(ctx, (int)pack.size());
}

#endif

} // namespace

#ifndef ARCADE_PAGES_TEST
void arcade_pages_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_pages", pages_get);
}
#endif
