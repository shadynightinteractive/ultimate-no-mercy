// [arcade] Custom stories for the No Mercy Arcade mod: the exe's own story compiler. The Custom Story menu
// lists <program folder>\stories\*.json (not test_* / _*), each compiled here at run time into a "story pack"
// (the game's Story Mode data + the mod's: story.c runs it). A faithful port of mod\gen\storyc.py (Pack.build()
// + check()): the same JSON, the same packs byte for byte (big-endian). The roster (names -> Superstar IDs)
// is <program folder>\stories\_roster.json, made by `storyc.py --roster` (build_mod.ps1).
// The mod calls these through RECOMP_IMPORT (recomp_arcade_story_*): strings and pack bytes go into the
// game's memory a byte at a time (MEM_B: the game's byte order, as the mod reads a u8 array).
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <cmath>

#include "json/json.hpp"
#include "arcade_roster.h"

#ifndef ARCADE_STORIES_TEST
#include "arcade_host.h"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#endif

#ifndef ARCADE_STORIES_TEST
#include <functional>
void arcade_wrestlers_each_name(const std::function<void(const std::string&, int)>& use);    // (arcade_wrestlers.cpp)
void arcade_wrestlers_each_id(const std::function<void(const std::string&, int)>& use);      // (arcade_wrestlers.cpp)
void arcade_arenas_each_name(int first, const std::function<void(const std::string&, int)>& use);    // (arcade_arenas.cpp)
void arcade_wrestlers_each_removed(const std::function<void(const std::string&, int)>& use);   // (arcade_wrestlers.cpp, 2026-11-13)
#endif

namespace arcade_stories {

using json = nlohmann::ordered_json;     // (the file's key order: "nodes" and "cast" depend on it, as in storyc)
// the roster: arcade_roster.h (startup 2026-11-19: one definition for the story compiler and the select's pages)

struct StoryError : std::runtime_error { using std::runtime_error::runtime_error; };
[[noreturn]] static void fail(const std::string& s) { throw StoryError(s); }

constexpr int64_t TEXT0 = 0xE000;
constexpr int64_t MAGIC = 0x4E4D5350;
constexpr int C_VAR = 1, C_CHOICE = 2, C_CHANCE = 3, C_ROLE_IS = 4;
constexpr int S_SET = 1, S_ADD = 2, S_ROLE_COPY = 3, S_ROLE_SET = 4;
// (2026-11-02, the story upgrade: storyc.py's) more terms, a condition as a postfix program (the branch's flags & 1)
constexpr int C_GAME = 5, C_HOW = 6, C_ROLE_IN = 7, C_AND = 0x10, C_OR = 0x11, C_NOT = 0x12, COND_DEPTH = 16;
constexpr int S_STORE = 5, VARS_MAX = 32;
static const std::vector<std::pair<std::string, int>> HOW = { {"pinfall", 1}, {"pin", 1}, {"submission", 2},
    {"tap out", 2}, {"dq", 3}, {"disqualification", 3}, {"countout", 4}, {"count-out", 4}, {"count out", 4},
    {"tko", 5}, {"ladder", 6}, {"escape", 7}, {"draw", 8}, {"decision", 9} };
static const std::map<std::string, int> VOICE_FIELDS = { {"nickname", 1}, {"catchphrase", 2}, {"signoff", 3},
    {"finisher", 4}, {"insult", 5} };
static const std::vector<std::pair<std::string, int>> VOICE_KEYS = { {"nickname", 1}, {"catchphrases", 2},
    {"signoff", 3}, {"finisher", 4}, {"insult", 5} };
static const std::map<int, int> VOICE_MAX = { {1, 20}, {2, 40}, {3, 40}, {4, 24}, {5, 16} };
constexpr int STORY_LINE_MAX = 48, OPTION_MAX = 28, NAME_LEN = 10;   // (what fits on the screen: storyc.py)
static const std::map<std::string, int> LINES = { {"say", 3}, {"page", 7}, {"requirement", 3}, {"ask", 5} };
constexpr int X_RECAST = 1, X_GROUPDEF = 2, X_GROUPMEM = 3, X_VARIANT = 4, X_VOICE = 5, X_SCENEOP = 6, X_MSCRIPT = 7;
constexpr int SO_COND = 1, SO_SET = 2, SO_STORE = 3, SO_TRON = 4, SO_CAM = 5;
constexpr int COMMENTARY_SHOT = 67;      // the ring shot that frames JR and King at their table (storyc.py)

static const std::map<std::string, int> BELTS = { {"world", 0}, {"tag", 1}, {"ic", 2}, {"intercontinental", 2},
    {"european", 3}, {"hardcore", 4}, {"lightheavy", 5}, {"light heavyweight", 5}, {"womens", 6}, {"women's", 6} };
static const std::map<std::string, std::pair<int, int>> TYPES = { {"single", {0, 2}}, {"tag", {1, 4}},
    {"triple", {2, 3}}, {"handicap", {3, 3}}, {"fourway", {13, 4}}, {"rumble", {9, 36}}, {"single_runin", {14, 4}},
    {"cage", {4, 2}}, {"ladder", {5, 2}}, {"ironman", {17, 2}}, {"handicap_runin", {18, 4}} };
// a match's rules: node +E, the engine's rule preset, by match type (storyc.py RULES; the first the type's default)
using RuleList = std::vector<std::pair<std::string, int>>;
static const RuleList R_SINGLE = { {"normal", 0x0F}, {"no dq", 0x19}, {"hardcore", 0x11}, {"submission", 0x13},
    {"anywhere", 0x15}, {"street fight", 0x16}, {"pinfall only", 0x17}, {"first blood", 0x18} };
static const RuleList R_HANDICAP = { {"normal", 0x25}, {"hardcore", 0x26} };
static const std::map<std::string, RuleList> RULES = { {"single", R_SINGLE}, {"single_runin", R_SINGLE},
    {"tag", { {"normal", 0x1A}, {"tornado", 0x1B}, {"no dq", 0x1E}, {"hardcore", 0x20} }},
    {"triple", { {"normal", 0x23}, {"hardcore", 0x24} }}, {"handicap", R_HANDICAP}, {"handicap_runin", R_HANDICAP},
    {"fourway", { {"normal", 0x28}, {"no dq", 0x2A} }}, {"rumble", { {"normal", 0x29} }}, {"cage", { {"normal", 0x2B} }},
    {"ladder", { {"normal", 0x2B} }}, {"ironman", { {"normal", 0x2C} }} };
// a match held backstage: node +A's high nibble (storyc.py LOCATIONS)
static const std::map<std::string, int> LOCATIONS = { {"ring", 0x50}, {"pool hall", 0x90}, {"parking lot", 0xA0},
    {"boiler room", 0xB0} };
// ---- 3D scenes: storyc.py's tables (PLACES, SPOTS, DOORS, ANIMS, WALKS, CLOSE, PAIR, SHOT1..3, EFFECTS) ----
static const std::map<std::string, int> PLACES = { {"hall", 1}, {"hallway", 1}, {"locker room", 2}, {"dressing room", 2},
    {"boiler room", 3}, {"pool hall", 4}, {"sports lounge", 4}, {"bar", 4}, {"parking lot", 5}, {"garage", 5},
    {"stage", 6}, {"entrance", 6}, {"ramp", 6}, {"ring", 7} };
static const std::map<int, std::vector<int>> SPOTS = { {1, {59, 60, 62, 64, 65, 63}}, {2, {38, 39, 40, 44, 45, 46, 47, 48}},
    {3, {77, 79, 76, 78}}, {4, {51, 52, 49, 50, 53, 54}}, {5, {66, 67, 69, 68, 70, 72}}, {6, {33, 34, 35, 36, 37}},
    {7, {3, 4, 9, 1, 2, 5, 6, 7, 8, 10}} };
static const std::map<int, std::vector<int>> DOORS = { {1, {55, 57}}, {5, {70, 72, 74}} };
static const std::map<int, int> DOORCAM = { {55, 77}, {57, 83}, {70, 112}, {72, 114}, {74, 113} };
static const std::map<std::string, int> ANIMS = { {"stand", 2}, {"mic", 3}, {"talk", 4}, {"entrance", 0},
    {"tag entrance", 26}, {"walk", 10}, {"run", 22}, {"shout", 5}, {"point", 6}, {"interview", 7}, {"shrug", 8},
    {"rub face", 9}, {"turn away", 11}, {"drink", 13}, {"laugh", 14}, {"punch", 15}, {"fist up", 16}, {"arms up", 17},
    {"hurt", 23}, {"hold out mic", 27}, {"swing", 28}, {"fall", 29}, {"down", 30}, {"get up", 33}, {"bring it", 41},
    // (2026-11-02) his own taunts (storyc.py: story.c points 18..21 / 24 at his moveset's 227..230 / 235)
    {"taunt", 18}, {"taunt 1", 18}, {"taunt 2", 19}, {"taunt 3", 20}, {"taunt 4", 21}, {"win taunt", 24} };
static bool is_taunt(int a) { return (a >= 18 && a <= 21) || a == 24; }
constexpr int A_STAND = 2, A_MIC = 3, A_TALK = 4, A_ENTRANCE = 0, A_TAG_ENTRANCE = 26;
static bool is_walk(int a) { return a == 10 || a == 22; }
static const std::map<int, int> CLOSE = { {59, 79}, {56, 79}, {60, 81}, {62, 84}, {64, 107}, {65, 106}, {38, 89},
    {39, 93}, {44, 94}, {42, 93}, {77, 119}, {79, 118}, {51, 99}, {52, 101}, {50, 97}, {53, 103}, {66, 111}, {71, 111},
    {67, 109}, {70, 115}, {0, 73}, {33, 73}, {34, 75}, {37, 75}, {3, 67}, {4, 65}, {1, 59}, {2, 60}, {5, 71}, {7, 67},
    {28, 61} };
static const std::map<std::pair<int, int>, int> PAIR = { {{59, 60}, 78}, {{56, 60}, 78}, {{64, 65}, 106}, {{38, 39}, 87},
    {{45, 47}, 96}, {{77, 79}, 118}, {{51, 52}, 101}, {{66, 67}, 117}, {{67, 71}, 117}, {{33, 34}, 76}, {{3, 4}, 58} };
static const std::map<int, int> SHOT1 = { {1, 82}, {2, 94}, {3, 120}, {4, 101}, {5, 108}, {6, 74}, {7, 63} };
static const std::map<int, int> SHOT2 = { {1, 82}, {2, 86}, {3, 118}, {4, 101}, {5, 108}, {6, 76}, {7, 57} };
static const std::map<int, int> SHOT3 = { {1, 82}, {2, 94}, {3, 120}, {4, 98}, {5, 108}, {6, 74}, {7, 63} };
constexpr int EFFECT_MUSIC = 7, EFFECT_QUIET = 8;
// a match's arena: node +A's low nibble -- the venue byte (0..9), as Exhibition numbers them (storyc.py ARENAS)
static const char* const ARENAS[10] = { "raw is war", "no mercy", "smackdown", "king of the ring", "summerslam",
    "survivor series", "royal rumble", "wrestlemania", "backlash", "armageddon" };
static const std::map<std::string, int> RUNIN = { {"opponents", 0x0002}, {"player", 0x0003} };
// (a run-in: the slot his side, the byte when -- storyc.py RUNIN_AT; a match's script: storyc.py MS_* / MA_* / END_HOW)
static const std::map<int64_t, int> RUNIN_AT = { {15, 1}, {30, 2}, {35, 3}, {40, 4}, {45, 5}, {60, 6}, {70, 7}, {90, 8},
    {120, 9}, {180, 10} };
static const std::map<std::string, int> END_HOW = { {"pinfall", 1}, {"pin", 1}, {"submission", 2}, {"tko", 3}, {"dq", 4},
    {"countout", 5}, {"count-out", 5}, {"count out", 5}, {"ladder", 6}, {"escape", 7} };
static const std::map<std::string, int> GAME = { {"win", 0x1}, {"lose", 0x2}, {"notlose", 0x4} };

// ---- Python's ways, for the JSON values ------------------------------------------------------------
static std::string repr(const json& j) { return j.dump(); }

static std::vector<uint32_t> utf8_decode(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = (uint8_t)s[i];
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || i + n >= s.size()) { out.push_back(0xFFFD); i++; continue; }
        uint32_t cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
        bool ok = true;
        for (int k = 1; k <= n; k++) {
            if (i + k >= s.size() || ((uint8_t)s[i + k] & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
        }
        if (!ok) { out.push_back(0xFFFD); i++; continue; }
        out.push_back(cp);
        i += n + 1;
    }
    return out;
}

static std::string utf8_encode(uint32_t cp) {
    std::string s;
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    return s;
}

// str.upper() (ASCII and Latin-1: the roster's names)
static std::string py_upper(const std::string& s) {
    std::string out;
    for (uint32_t c : utf8_decode(s)) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 32;
        else if (c == 0xFF) c = 0x178;
        else if (c == 0xB5) c = 0x39C;
        else if (c == 0xDF) { out += "SS"; continue; }
        out += utf8_encode(c);
    }
    return out;
}

static std::string py_lower(const std::string& s) {
    std::string out;
    for (uint32_t c : utf8_decode(s)) {
        if (c >= 'A' && c <= 'Z') c += 32;
        else if (c >= 0xC0 && c <= 0xDE && c != 0xD7) c += 32;
        out += utf8_encode(c);
    }
    return out;
}

// t.encode("latin1", "replace")
static std::string latin1(const std::string& s) {
    std::string out;
    for (uint32_t c : utf8_decode(s)) out += c <= 0xFF ? (char)c : '?';
    return out;
}

static bool truthy(const json& j) {
    switch (j.type()) {
    case json::value_t::null: return false;
    case json::value_t::boolean: return j.get<bool>();
    case json::value_t::number_integer: return j.get<int64_t>() != 0;
    case json::value_t::number_unsigned: return j.get<uint64_t>() != 0;
    case json::value_t::number_float: return j.get<double>() != 0.0;
    case json::value_t::string: return !j.get_ref<const std::string&>().empty();
    case json::value_t::array: case json::value_t::object: return !j.empty();
    default: return false;
    }
}

// iterating a value (for x in v / list(v)): a list's items, a string's letters, a dict's keys
static std::vector<json> py_iter(const json& j) {
    std::vector<json> out;
    if (j.is_array()) for (const json& x : j) out.push_back(x);
    else if (j.is_string()) for (uint32_t c : utf8_decode(j.get_ref<const std::string&>())) out.push_back(utf8_encode(c));
    else if (j.is_object()) for (auto it = j.begin(); it != j.end(); ++it) out.push_back(it.key());
    else fail(repr(j) + " isn't a list");
    return out;
}

// d.get(key, default): d a dict
static json get(const json& d, const char* key, const json& def) {
    if (!d.is_object()) fail(repr(d) + " isn't a dict");
    auto it = d.find(key);
    return it == d.end() ? def : *it;
}

static bool is_ws(char c) { return c == ' ' || (c >= '\t' && c <= '\r') || (c >= 0x1C && c <= 0x1F); }
static std::string strip(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && is_ws(s[a])) a++;
    while (b > a && is_ws(s[b - 1])) b--;
    return s.substr(a, b - a);
}

// digits (a run of 0-9, maybe with single '_' between) -> a number (saturating: too big for any field anyway)
static int64_t digits_value(const std::string& d) {
    int64_t v = 0;
    for (char c : d) if (c != '_') v = v > (int64_t)1e15 ? v : v * 10 + (c - '0');
    return v;
}

// int(v)
static int64_t py_int(const json& j) {
    if (j.is_boolean()) return j.get<bool>() ? 1 : 0;
    if (j.is_number_integer() && !j.is_number_unsigned()) return j.get<int64_t>();
    if (j.is_number_unsigned()) {
        uint64_t u = j.get<uint64_t>();
        return u > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)u;
    }
    if (j.is_number_float()) {
        double d = j.get<double>();
        if (!std::isfinite(d)) fail("int(" + repr(j) + ")");
        d = std::trunc(d);
        return d > 9e18 ? INT64_MAX : d < -9e18 ? INT64_MIN : (int64_t)d;
    }
    if (j.is_string()) {
        std::string s = strip(j.get_ref<const std::string&>());
        size_t i = 0;
        bool neg = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
        std::string d = s.substr(i);
        bool ok = !d.empty() && d.front() != '_' && d.back() != '_';
        for (size_t k = 0; ok && k < d.size(); k++)
            if (!(d[k] >= '0' && d[k] <= '9') && !(d[k] == '_' && d[k - 1] != '_')) ok = false;
        if (!ok) fail("invalid literal for int(): " + repr(j));
        int64_t v = digits_value(d);
        return neg ? -v : v;
    }
    fail("int(" + repr(j) + ")");
}

// a value struct.pack takes for an integer field: an int (or a bool)
static int64_t strict_int(const json& j) {
    if (j.is_boolean() || j.is_number_integer()) return py_int(j);
    fail("struct: " + repr(j) + " isn't an integer");
}

// ---- struct.pack (big-endian), range-checked as struct does ---------------------------------------------
using Bytes = std::vector<uint8_t>;
struct Out {
    Bytes b;
    Out& B(int64_t v) { if (v < 0 || v > 0xFF) fail("struct: " + std::to_string(v) + " out of range (B)"); b.push_back((uint8_t)v); return *this; }
    Out& H(int64_t v) { if (v < 0 || v > 0xFFFF) fail("struct: " + std::to_string(v) + " out of range (H)"); return raw16((uint16_t)v); }
    Out& h(int64_t v) { if (v < -0x8000 || v > 0x7FFF) fail("struct: " + std::to_string(v) + " out of range (h)"); return raw16((uint16_t)(int16_t)v); }
    Out& I(int64_t v) { if (v < 0 || v > 0xFFFFFFFFll) fail("struct: " + std::to_string(v) + " out of range (I)"); return raw32((uint32_t)v); }
    Out& i(int64_t v) { if (v < INT32_MIN || v > INT32_MAX) fail("struct: " + std::to_string(v) + " out of range (i)"); return raw32((uint32_t)(int32_t)v); }
    Out& x(int n) { while (n-- > 0) b.push_back(0); return *this; }
    Out& raw16(uint16_t v) { b.push_back(v >> 8); b.push_back(v & 0xFF); return *this; }
    Out& raw32(uint32_t v) { raw16(v >> 16); return raw16(v & 0xFFFF); }
};

// ---- 3D scenes: storyc.py's Scene and its helpers (num, texts, steps_of, cast_list, place7, spots, place_of, anim_of,
// wait_of, shot_for) ---------------------------------------------------------------------------------------------
static bool has(const json& d, const char* k) { return d.is_object() && d.contains(k); }
static bool is_int(const json& v) { return v.is_number_integer(); }   // (not a bool, not a float: Python's int)
static int num(const json& v, int64_t lo, int64_t hi, const std::string& what) {
    if (!is_int(v)) fail(what + " " + repr(v) + ": " + std::to_string(lo) + " to " + std::to_string(hi));
    int64_t x = py_int(v);
    if (x < lo || x > hi) fail(what + " " + repr(v) + ": " + std::to_string(lo) + " to " + std::to_string(hi));
    return (int)x;
}
static std::vector<json> texts(const json& v) { if (v.is_array()) return py_iter(v); return { v }; }
static std::vector<json> steps_of(const json& sc) {
    if (sc.is_array()) return py_iter(sc);
    if (sc.is_object()) {
        json head = json::object();
        for (const char* k : { "place", "cast", "fade", "camera" }) if (sc.contains(k)) head[k] = sc.at(k);
        std::vector<json> out;
        if (head.contains("place")) out.push_back(head);
        for (const json& x : py_iter(get(sc, "steps", json::array()))) out.push_back(x);
        return out;
    }
    fail("a scene " + repr(sc) + " isn't a list or a dict");
}
static std::vector<json> cast_list(const json& c) {
    if (!c.is_array()) fail("cast " + repr(c) + " isn't a list");
    std::vector<json> out;
    for (const json& x : c) {
        if (x.is_object()) out.push_back(x);
        else { json d = json::object(); d["who"] = x; out.push_back(d); }
    }
    return out;
}
static int place7(int pl) { return pl == 8 ? 7 : pl; }
static const std::vector<int>& spots(int pl) {
    auto it = SPOTS.find(place7(pl));
    if (it == SPOTS.end()) fail("a step before any place");
    return it->second;
}
static int place_of(const json& v) {
    if (is_int(v)) { int64_t x = py_int(v); if (x >= 1 && x <= 8) return (int)x; }
    if (v.is_string()) { auto it = PLACES.find(py_lower(v.get<std::string>())); if (it != PLACES.end()) return it->second; }
    std::string names;
    for (auto& kv : PLACES) names += (names.empty() ? "" : ", ") + kv.first;
    fail("place " + repr(v) + " (one of " + names + ")");
}
static int anim_of(const json& v) {
    if (is_int(v)) { int64_t x = py_int(v); if (x >= 0 && x <= 60) return (int)x; }
    if (v.is_string()) { auto it = ANIMS.find(py_lower(v.get<std::string>())); if (it != ANIMS.end()) return it->second; }
    std::string names;
    for (auto& kv : ANIMS) names += (names.empty() ? "" : ", ") + kv.first;
    fail("do " + repr(v) + " (one of " + names + ")");
}
static int wait_of(const json& v, int dflt) {     // (v null: not given)
    if (v.is_null()) return dflt;
    if (v.is_boolean()) return v.get<bool>() ? 0xFF : 0;
    return num(v, 0, 254, "wait (frames, or true: till it's done)");
}
static int shot_for(int pl, const std::vector<int>& marks) {
    int p = place7(pl);
    if (marks.size() == 1) { auto it = CLOSE.find(marks[0]); return it != CLOSE.end() ? it->second : SHOT1.at(p); }
    if (marks.size() == 2) {
        auto it = PAIR.find({ std::min(marks[0], marks[1]), std::max(marks[0], marks[1]) });
        return it != PAIR.end() ? it->second : SHOT2.at(p);
    }
    return SHOT3.at(p);
}
// "choice\s*(==|=)?\s*(\d+)$" -> the number (-1: not that)
static int64_t choice_term(const std::string& term) {
    if (term.compare(0, 6, "choice") != 0) return -1;
    size_t i = 6;
    while (i < term.size() && is_ws(term[i])) i++;
    if (term.compare(i, 2, "==") == 0) i += 2; else if (term.compare(i, 1, "=") == 0) i += 1;
    while (i < term.size() && is_ws(term[i])) i++;
    size_t a = i;
    while (i < term.size() && term[i] >= '0' && term[i] <= '9') i++;
    if (i == a || i != term.size()) return -1;
    return digits_value(term.substr(a, i - a));
}

// "1:30" / "90" -> seconds (storyc.py clock_of)
static int64_t clock_of(const json& t) {
    if (t.is_string()) {
        const std::string& s = t.get_ref<const std::string&>();
        size_t c = s.find(':');
        auto digits = [](const std::string& d) { if (d.empty()) return false; for (char ch : d) if (ch < '0' || ch > '9') return false; return true; };
        if (c != std::string::npos) {
            std::string m = s.substr(0, c), sec = s.substr(c + 1);
            if (digits(m) && m.size() <= 3 && digits(sec) && sec.size() == 2 && digits_value(sec) < 60) return digits_value(m) * 60 + digits_value(sec);
        } else if (digits(s) && s.size() <= 4) return digits_value(s);
    }
    fail("time " + repr(t) + ": M:SS or seconds");
}

// "teams" -> the match type (its slots in out): storyc.py teams_of
static json teams_of(const json& m, std::vector<json>& out) {
    if (m.contains("slots")) fail("a match has slots or teams, not both");
    const json& tm = m.at("teams");
    bool ok = tm.is_array();
    if (ok) for (const json& t : tm) ok = ok && t.is_array();
    if (!ok) fail("teams " + repr(tm) + ": a list of teams, each a list");
    std::vector<size_t> sh;
    for (const json& t : tm) sh.push_back(t.size());
    std::string t;
    if (sh == std::vector<size_t>{ 1, 1 }) t = "single";
    else if (sh == std::vector<size_t>{ 2, 2 }) t = "tag";
    else if (sh == std::vector<size_t>{ 1, 1, 1 }) t = "triple";
    else if (sh == std::vector<size_t>{ 1, 1, 1, 1 }) t = "fourway";
    else if (sh == std::vector<size_t>{ 1, 2 } || sh == std::vector<size_t>{ 2, 1 }) t = "handicap";
    else fail("teams " + repr(tm) + ": 1 on 1, 2 on 2, 1 on 2, 2 on 1, 1 on 1 on 1 or four of one");
    json ty = get(m, "type", t);
    if (ty != t && !(t == "single" && (ty == "cage" || ty == "ladder" || ty == "ironman")))
        fail("teams " + repr(tm) + " make a " + t + " match, not " + repr(ty));
    if (sh == std::vector<size_t>{ 1, 2 }) { for (const json& x : tm[1]) out.push_back(x); out.push_back(tm[0][0]); }
    else for (const json& team : tm) for (const json& x : team) out.push_back(x);
    return ty;
}

// ---- the compiler (storyc.py's Pack) ----------------------------------------------------------------
struct Pack {
    const json& st;
    const Roster& R;
    std::vector<std::string> strings;
    std::vector<Bytes> parts, scenes, cmds, branches, conds, sets;
    std::map<std::string, int64_t> vars;
    std::vector<int64_t> var_init;
    std::map<std::string, int64_t> roles = { {"player", 0}, {"partner", 1}, {"champion", 2}, {"champion2", 3} };
    int64_t role_init[8] = {};
    std::vector<std::pair<int64_t, int64_t>> xarena;   // (node, arena) for the arenas of ours (storyc.py xarenas)
    std::map<int, std::vector<Bytes>> ext = { {X_RECAST, {}}, {X_GROUPDEF, {}}, {X_GROUPMEM, {}}, {X_VARIANT, {}},
                                              {X_VOICE, {}}, {X_SCENEOP, {}}, {X_MSCRIPT, {}} };
    std::map<std::string, int64_t> gidx;
    std::set<int> voice_used;

    Pack(const json& story, const Roster& roster) : st(story), R(roster) {
        json cast = get(story, "cast", json::object());
        if (!cast.is_object()) fail("cast: " + repr(cast) + " isn't a dict");
        // (a title on the line: the engine moves the old champion to role 4 when the belt changes hands -- the
        // story's own roles from 5 then; storyc.py)
        int r0 = 4;
        json nodes_j = get(story, "nodes", json::object());
        if (nodes_j.is_object())
            for (auto it = nodes_j.begin(); it != nodes_j.end(); ++it)
                if (has(it.value(), "match") && has(it.value().at("match"), "title")) r0 = 5;
        int i = 0;
        for (auto it = cast.begin(); it != cast.end(); ++it, i++) {
            if (r0 + i > 7) fail("at most " + std::to_string(8 - r0) + " cast roles");
            roles[it.key()] = r0 + i;
            if (it.value().is_array()) {     // a recast: the first of these who isn't the player (or another role's man)
                const json& l = it.value();
                if (l.empty()) fail("cast " + json(it.key()).dump() + ": an empty list");
                std::vector<int64_t> ids;
                listing = true;
                for (const json& x : l) ids.push_back(who(x));
                listing = false;
                for (int64_t c : ids) if (!c || (c & 0x8000)) fail("cast " + json(it.key()).dump() + ": a list of Superstars (no roles)");
                // (2026-11-13, removal) A removed man in the list is passed over, as if the story hadn't named him: the next
                // stands in. If the list is only removed men it stays as it is: the mod casts the first one's stand-in.
                if (!R.removed.empty()) {
                    std::vector<int64_t> on;
                    for (int64_t c : ids) if (!R.removed.count(c)) on.push_back(c);
                    if (on.empty()) stand_in(ids[0]);
                    else ids = on;
                }
                if (ids.size() > 8) fail("cast " + json(it.key()).dump() + ": 8 Superstars at most");
                role_init[r0 + i] = ids[0];
                if (ids.size() > 1) for (int64_t c : ids) ext[X_RECAST].push_back(Out().B(r0 + i).B(0).H(c).b);
            } else role_init[r0 + i] = who(it.value());
        }
        json vs = get(story, "vars", json::object());
        if (!vs.is_object()) fail("vars: " + repr(vs) + " isn't a dict");
        for (auto it = vs.begin(); it != vs.end(); ++it) {
            vars[it.key()] = (int64_t)var_init.size();
            var_init.push_back(py_int(it.value()));
        }
        if (var_init.size() > (size_t)VARS_MAX) fail("at most " + std::to_string(VARS_MAX) + " variables");
    }

    // A Superstar from a story: his string ID ("stone_cold", a folder's name), else a name (the stories before IDs), else
    // "0x0340". 0 if none of those. (2026-11-09)
    int64_t lookup(const std::string& s) const {
        auto i = R.find("@ID:" + s);
        if (i != R.end()) return i->second;
        {   // (an ID written in capitals or with spaces, "Rock On Austin": as the folder scan makes IDs, 2026-11-12)
            std::string l;
            for (char c : s) l += (c == ' ' || c == '-') ? '_' : (char)tolower((uint8_t)c);
            i = R.find("@ID:" + l);
            if (i != R.end()) return i->second;
        }
        auto x = R.find(py_upper(s));
        if (x != R.end()) return x->second;
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            char* end = nullptr;
            const long v = strtol(s.c_str() + 2, &end, 16);
            if (end && *end == 0 && v > 0 && v < 0x1000) {
                if (const int64_t r = retired_stand_in(R, int64_t(v))) { retired_said(s, r); return r; }   // (no2k15)
                return v;
            }
        }
        // (no2k15, 2026-11-23) one taken out for good (mod gen/retired_ids.json): his stand-in plays (arcade_roster.h)
        if (const int64_t r = retired_stand_in(R, s)) { retired_said(s, r); return r; }
        return 0;
    }
    // (no2k15) what a story names of a Superstar taken out for good, once each, in the log
    mutable std::set<std::string> retired_seen;
    void retired_said(const std::string& what, int64_t stand_in) const {
        if (!retired_seen.insert(what).second) return;
        fprintf(stderr, "[stories] %s: taken out of the game (mod gen/retired_ids.json) -- his stand-in 0x%04X plays his parts\n",
                what.c_str(), (unsigned)stand_in);
    }
    std::string nearest(const std::string& s) const {     // the IDs most like it, for the message
        std::vector<std::pair<int, std::string>> d;
        for (auto& kv : R) {
            if (kv.first.rfind("@ID:", 0) != 0) continue;
            const std::string id = kv.first.substr(4);
            std::vector<int> row(id.size() + 1);
            for (size_t j = 0; j <= id.size(); j++) row[j] = (int)j;
            for (size_t i = 1; i <= s.size(); i++) {
                int prev = row[0]; row[0] = (int)i;
                for (size_t j = 1; j <= id.size(); j++) { int t = row[j]; row[j] = std::min({ row[j] + 1, row[j - 1] + 1, prev + (tolower((uint8_t)s[i - 1]) != id[j - 1]) }); prev = t; }
            }
            d.push_back({ row[id.size()], id });
        }
        std::sort(d.begin(), d.end());
        std::string out;
        for (size_t k = 0; k < d.size() && k < 4; k++) out += (k ? ", " : "") + d[k].second;
        return out;
    }
    // (startup 2026-11-19) A Superstar named by both his string ID and his game number, {"id": "aki_man", "game_number":
    // "0x0379"}: by the number when that wrestler is aki_man, else by the string ID (arcade_roster.h resolve_ref; never by
    // the name on screen). 0 if neither finds anyone. What the fallback did goes in the log.
    int64_t ref_object(const json& w) const {
        int64_t number = 0;
        if (w.contains("game_number")) {
            const json& g = w.at("game_number");
            if (g.is_number_integer()) number = g.get<int64_t>();
            else if (g.is_string()) {
                const std::string& t = g.get_ref<const std::string&>();
                char* end = nullptr;
                const long v = t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X') ? strtol(t.c_str() + 2, &end, 16) : -1;
                if (end && *end == 0) number = v;
            }
            if (number <= 0 || number >= 0x1000) fail("game_number " + repr(g) + " isn't a game number (0x1 .. 0xFFF)");
        }
        const std::string sid = w.contains("id") && w.at("id").is_string() ? w.at("id").get<std::string>() : std::string();
        if (!number && sid.empty()) fail(repr(w) + ": a Superstar needs \"id\" (his string ID) and/or \"game_number\"");
        std::string note;
        int64_t id = resolve_ref(R, number, sid, note);
        if (!note.empty()) fprintf(stderr, "[stories] %s: %s\n", repr(w).c_str(), note.c_str());
        // (no2k15, 2026-11-23) one taken out for good: his stand-in -- by the string ID when nobody has it, else by the number
        // when nobody has that (gen/storyc.py ref_object the same)
        if (sid.empty() || !number_of_sid(R, sid)) {
            int64_t r = sid.empty() ? 0 : retired_stand_in(R, sid);
            if (!r) r = retired_stand_in(R, number);
            if (r) { retired_said(repr(w), r); id = r; }
        }
        return id;
    }
    int64_t star(const json& w) {          // a Superstar by ID or name (not a role) -> his ID
        if (w.is_object()) {               // (startup 2026-11-19: string ID + game number)
            if (int64_t id = ref_object(w)) return id;
            fail(repr(w) + " isn't a Superstar on the roster");
        }
        if (w.is_string() && !roles.count(w.get<std::string>())) {
            if (int64_t id = lookup(w.get<std::string>())) return id;
            fail(repr(w) + " isn't a Superstar on the roster (closest: " + nearest(w.get<std::string>()) + ")");
        }
        fail(repr(w) + " isn't a Superstar on the roster");
    }

    int64_t group(const std::string& name) {   // a group ("dx") -> its index in the pack (its members put in once)
        std::string g = py_upper(strip(name));
        auto it = R.groups.find(g);
        if (it == R.groups.end()) {
            std::string names;
            for (auto& kv : R.groups) names += (names.empty() ? "" : ", ") + py_lower(kv.first);
            fail("no group " + json(name).dump() + " (" + names + ")");
        }
        if (!gidx.count(g)) {
            gidx[g] = (int64_t)ext[X_GROUPDEF].size();
            ext[X_GROUPDEF].push_back(Out().H((int64_t)ext[X_GROUPMEM].size()).H((int64_t)it->second.size()).b);
            for (int64_t i : it->second) ext[X_GROUPMEM].push_back(Out().H(i).H(0).b);
        }
        return gidx[g];
    }

    int64_t sceneop(int kind, int64_t first, int64_t n, int64_t a) {   // -> the 0x7F command's param
        if (ext[X_SCENEOP].size() >= 0x1000) fail("too many scene tests / sets");
        ext[X_SCENEOP].push_back(Out().B(kind).B(n).H(first).H(a).H(0).b);
        return 0xF000 | (int64_t)(ext[X_SCENEOP].size() - 1);
    }

    // a {...} in text: {kind, who, field}: kind 0 a name, 1 a voice token (storyc.py token)
    struct Tok { int kind; std::string who; int field; };
    static Tok token(const std::string& inner) {
        size_t d = inner.rfind('.');
        if (d != std::string::npos) {
            std::string w = inner.substr(0, d), f = py_lower(strip(inner.substr(d + 1)));
            auto v = VOICE_FIELDS.find(f);
            if (v != VOICE_FIELDS.end()) return { 1, strip(w), v->second };
            if (f == "name") return { 0, strip(w), 0 };
        }
        return { 0, inner, 0 };
    }
    std::string voice(const std::string& w, int field) {   // a voice token's bytes (ESC ...)
        voice_used.insert(field);
        auto r = roles.find(w);
        if (r != roles.end()) return "\x1b" + utf8_encode(0x80 | field) + utf8_encode(0x80 | (uint32_t)r->second);
        int64_t i = star(json(w));
        return "\x1b" + utf8_encode(0xC0 | field) + utf8_encode(0x80 | (uint32_t)(i >> 6)) + utf8_encode(0x80 | (uint32_t)(i & 63));
    }
    // a text's fit (LINES, STORY_LINE_MAX: a {name} counts NAME_LEN letters, a voice token its field's longest)
    void fit(const json& tj, const std::string& what) {
        if (!tj.is_string()) fail("text " + repr(tj) + " isn't a string");
        const std::string& t = tj.get_ref<const std::string&>();
        std::string u;
        for (size_t i = 0; i < t.size();) {
            if (t[i] == '{') {
                size_t j = i + 1;
                while (j < t.size() && t[j] != '{' && t[j] != '}') j++;
                if (j < t.size() && t[j] == '}' && j > i + 1) {
                    Tok k = token(t.substr(i + 1, j - i - 1));
                    if (k.kind == 1) {
                        u += std::string(VOICE_MAX.at(k.field), 'X');
                        if (k.field == 3) u += "\n" + std::string(VOICE_MAX.at(3), 'X');
                    } else u += std::string(NAME_LEN, 'X');
                    i = j + 1;
                    continue;
                }
            }
            u += t[i++];
        }
        std::vector<std::string> ls;
        for (size_t a = 0;;) {
            size_t b = u.find('\n', a);
            ls.push_back(u.substr(a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        int nl = LINES.at(what);
        if ((int)ls.size() > nl) fail(repr(tj) + ": " + std::to_string(nl) + " lines at most here");
        for (const std::string& l : ls)
            if ((int)utf8_decode(l).size() > STORY_LINE_MAX)
                fail(repr(tj) + ": " + std::to_string(STORY_LINE_MAX) + " characters a line at most (a {name} counts " +
                     std::to_string(NAME_LEN) + ", a voice token its longest)");
    }
    // a line of text, or one per player: {"THE ROCK": "...", "group:dx": "...", "default": "..."} -> its text id
    int64_t vtext(const json& t, std::vector<int64_t>& cast, const std::string& what) {
        if (t.is_object()) {
            if (!t.contains("default")) fail("lines by Superstar need a \"default\"");
            int64_t tid = vtext(t.at("default"), cast, what);
            for (auto it = t.begin(); it != t.end(); ++it) {
                const std::string& k = it.key();
                if (k == "default") continue;
                int64_t key = py_lower(k).rfind("group:", 0) == 0 ? (0x8000 | group(k.substr(6))) : star(json(k));
                if (!it.value().is_string()) fail("text " + repr(it.value()) + " isn't a string");
                int64_t vid = vtext(it.value(), cast, what);
                ext[X_VARIANT].push_back(Out().H(tid - TEXT0).H(key).H(vid - TEXT0).H(0).b);
            }
            return tid;
        }
        fit(t, what);
        return text(json(names(t.get<std::string>(), cast)));
    }

    int64_t text(const json& t) {
        if (!t.is_string()) fail("text " + repr(t) + " isn't a string");
        strings.push_back(t.get<std::string>());
        return TEXT0 + (int64_t)strings.size() - 1;
    }

    int64_t who(const json& w) {          // a slot's part code
        if (w.is_null()) return 0;
        if (w.is_object()) {               // (startup 2026-11-19: string ID + game number, ref_object)
            if (int64_t id = ref_object(w)) { if (!listing && R.removed.count(id)) stand_in(id); return id; }
            fail("unknown Superstar: " + repr(w));
        }
        if (!w.is_string()) fail("unknown Superstar or role: " + repr(w));
        const std::string& s = w.get_ref<const std::string&>();
        if (s.empty() || s == "-") return 0;
        auto r = roles.find(s);
        if (r != roles.end()) return 0x8000 + r->second;
        if (int64_t id = lookup(s)) { if (!listing && R.removed.count(id)) stand_in(id); return id; }
        fail("unknown Superstar or role: " + repr(w) + " (closest: " + nearest(s) + ")");
    }
    // (2026-11-13, removal) A Superstar the story puts in a role, a match or a scene by himself who is removed: the story is
    // compiled as it is, naming him, and the mod gives his places to his stand-in when it runs (mod story.c: the man after
    // him on his select page). Only noted here, for the log. Lines keyed by him, "role is" tests and voice tokens naming him
    // aren't changed: they just never come up.
    bool listing = false;                // (in a cast list: its own rule, above)
    std::vector<std::string> standins;   // the removed men it names, in the order named (the log's line)
    void stand_in(int64_t id) {
        const auto r = R.removed.find(id);
        const std::string n = r != R.removed.end() ? r->second : std::to_string(id);
        if (std::find(standins.begin(), standins.end(), n) == standins.end()) standins.push_back(n);
    }

    int64_t part(std::vector<int64_t> codes, int64_t extra = 0) {
        codes.resize(5, 0);
        Out o;
        for (int64_t c : codes) o.H(c);
        o.H(extra);
        parts.push_back(o.b);
        return (int64_t)parts.size();     // (index from 1)
    }

    // names in text: {player}, {rival}, {KANE} ... -> the engine's %cNN (re.sub(r"\{([^{}]+)\}", ...))
    std::string names(const std::string& text, std::vector<int64_t>& cast) {
        std::string out;
        size_t i = 0;
        while (i < text.size()) {
            if (text[i] == '{') {
                size_t j = i + 1;
                while (j < text.size() && text[j] != '{' && text[j] != '}') j++;
                if (j < text.size() && text[j] == '}' && j > i + 1) {
                    Tok k = token(text.substr(i + 1, j - i - 1));
                    if (k.kind == 1) { out += voice(k.who, k.field); i = j + 1; continue; }
                    int64_t code = who(json(k.who));
                    auto it = std::find(cast.begin(), cast.end(), code);
                    if (it == cast.end()) { cast.push_back(code); it = cast.end() - 1; }
                    char sz[32];
                    snprintf(sz, sizeof sz, "%%c%02d", (int)(it - cast.begin()) + 1);
                    out += sz;
                    i = j + 1;
                    continue;
                }
            }
            out += text[i++];
        }
        return out;
    }

    static bool has_names(const std::string& text) {
        for (size_t i = 0; i < text.size(); i++) {
            if (text[i] != '{') continue;
            size_t j = i + 1;
            while (j < text.size() && text[j] != '{' && text[j] != '}') j++;
            if (j < text.size() && text[j] == '}' && j > i + 1) return true;
        }
        return false;
    }

    // a scene: narration pages; "also": texts named with the scene's cast (returned converted)
    int64_t narration(const json& pages, std::vector<std::string>* also = nullptr) {
        if (!truthy(pages)) return 0;
        int64_t pos = (int64_t)cmds.size();
        std::vector<int64_t> cast;
        cmds.push_back(Out().B(0x10).B(0x00).H(0x0002).b);
        for (const json& p : py_iter(pages)) {
            int64_t t = vtext(p, cast, "page");
            cmds.push_back(Out().B(0x32).B(0xFF).H(t).b);
        }
        if (also) for (std::string& t : *also) t = names(t, cast);
        cmds.push_back(Out().B(0x00).B(0xFF).H(0x0000).b);
        int64_t first = 0;
        for (int64_t c : cast) {
            int64_t k = part({ c });
            if (!first) first = k;
        }
        if (cast.size() > 8) fail("a scene names 8 Superstars at most");
        scenes.push_back(Out().H(first).H(pos).B((int64_t)cast.size()).B(5).b);
        return (int64_t)scenes.size();
    }


    // ---- a 3D scene (storyc.py's class Scene) ----
    struct Scene {
        std::vector<int64_t> cast;
        std::vector<Bytes> out;
        int place = 0, real = 0, speaker = -1;
        std::map<int, int> on, doing;
        bool dirty = false;
    };

    int actor(Scene& sc, const json& w, const std::string& ctx) {
        int64_t code = who(w);
        if (!code) fail(ctx + ": who is " + repr(w) + "?");
        auto it = std::find(sc.cast.begin(), sc.cast.end(), code);
        if (it == sc.cast.end()) {
            if (sc.cast.size() >= 4) fail("a scene shows 4 Superstars at most");
            sc.cast.push_back(code);
            it = sc.cast.end() - 1;
        }
        return (int)(it - sc.cast.begin());
    }

    void gather(Scene& sc, const std::vector<json>& steps) {
        for (const json& st : steps) {
            if (!st.is_object()) fail("a scene step " + repr(st) + " isn't a dict");
            if (st.contains("place"))
                for (const json& c : cast_list(get(st, "cast", json::array()))) actor(sc, get(c, "who", nullptr), "cast");
            for (const char* k : { "who", "with" }) if (st.contains(k)) actor(sc, st.at(k), "a step");
            if (st.contains("on")) {
                const json& on = st.at("on");
                if (on.is_array()) { for (const json& w : on) actor(sc, w, "camera"); }
                else actor(sc, on, "camera");
            }
            if (st.contains("if")) {
                gather(sc, steps_of(get(st, "then", json::array())));
                gather(sc, steps_of(get(st, "else", json::array())));
            }
        }
    }

    static void cmd(Scene& sc, int64_t b0, int64_t b1, int64_t p) { sc.out.push_back(Out().B(b0).B(b1).H(p).b); }
    void line(Scene& sc, int op, const json& t) {
        cmd(sc, op, 0xFF, vtext(t, sc.cast, op == 0x32 ? "page" : "say"));
    }

    void restore(Scene& sc) {
        if (!sc.real) fail("a scene step before any place");
        cmd(sc, 0x10 | sc.real, 0x00, 0x0002);
        for (auto& kv : sc.on) cmd(sc, 0x20 | kv.first, 0x00, (kv.second << 8) | sc.doing[kv.first]);
        sc.place = sc.real; sc.dirty = false; sc.speaker = -1;
    }
    void need3d(Scene& sc) { if (sc.place == 0 || sc.dirty) restore(sc); }

    int mark_of(Scene& sc, const json& st, int a) {     // (-1: none)
        if (has(st, "mark")) return num(st.at("mark"), 0, 98, "mark");
        if (has(st, "at")) {
            const std::vector<int>& sp = spots(sc.place);
            return sp[num(st.at("at"), 1, (int64_t)sp.size(), "at (this place's spots)") - 1];
        }
        auto it = sc.on.find(a);
        return it != sc.on.end() ? it->second : -1;
    }

    static void camera(Scene& sc, const std::vector<int>& acts, int shot) {
        int mask = 0;
        for (int a : acts) mask |= 1 << a;
        cmd(sc, 0x60 | mask, 0x00, shot);
    }
    int shot(Scene& sc, const json& v, const std::vector<int>& acts) {
        std::vector<int> marks;
        for (int a : acts) { auto it = sc.on.find(a); if (it != sc.on.end()) marks.push_back(it->second); }
        if ((v.is_boolean() && v.get<bool>()) || v == "auto") return shot_for(sc.place, marks);
        if (is_int(v) && py_int(v) >= 0 && py_int(v) <= 0xFFFF) return (int)py_int(v);
        if (v == "close") { if (marks.empty()) marks.push_back(-1); marks.resize(1); return shot_for(sc.place, marks); }
        if (v == "two") { marks.push_back(-1); marks.push_back(-1); marks.resize(2); return shot_for(sc.place, marks); }
        if (v == "wide") return SHOT3.at(place7(sc.place));
        fail("camera " + repr(v) + ": close, two, wide or a number");
    }

    void frame(Scene& sc, int a, const json& st) {   // a speaker: the camera on him, him talking, the one before standing
        if (a < 0 || !sc.on.count(a)) return;
        int b = sc.speaker;
        if (b >= 0 && b != a && sc.on.count(b) && (sc.doing[b] == A_MIC || sc.doing[b] == A_TALK)) {
            sc.doing[b] = A_STAND;
            cmd(sc, 0x20 | b, 0x00, (sc.on[b] << 8) | A_STAND);
        }
        json cam = get(st, "camera", true);
        if (!(cam.is_boolean() && !cam.get<bool>())) camera(sc, { a }, shot(sc, cam, { a }));
        int an = st.contains("do") ? anim_of(st.at("do")) : (sc.place == 6 || sc.place == 7 || sc.place == 8) ? A_MIC : A_TALK;
        sc.doing[a] = an;
        cmd(sc, 0x20 | a, 0x00, (sc.on[a] << 8) | an);
        sc.speaker = a;
    }

    void steps(Scene& sc, const std::vector<json>& list) {
        for (size_t k = 0; k < list.size(); k++) {
            try { step(sc, list[k]); }
            catch (const StoryError& e) { fail("step " + std::to_string(k + 1) + ": " + e.what()); }
        }
    }

    void step(Scene& sc, const json& st) {
        if (!st.is_object()) fail(repr(st) + " isn't a step (a dict)");
        {
            if (st.contains("place")) {
                int pl = place_of(st.at("place"));
                cmd(sc, 0x10 | pl, 0x00, num(get(st, "fade", 2), 0, 3, "fade"));
                sc.place = sc.real = pl; sc.on.clear(); sc.doing.clear(); sc.speaker = -1; sc.dirty = false;
                std::vector<int> acts;
                std::vector<json> cl = cast_list(get(st, "cast", json::array()));
                for (size_t i = 0; i < cl.size(); i++) {
                    const json& c = cl[i];
                    int a = actor(sc, get(c, "who", nullptr), "cast");
                    int m = mark_of(sc, c, -1);
                    if (m < 0) {
                        const std::vector<int>& sp = spots(pl);
                        if (i >= sp.size()) fail("cast: say where (\"at\")");
                        m = sp[i];
                    }
                    int an = anim_of(get(c, "do", "stand"));
                    cmd(sc, 0x20 | a, wait_of(get(c, "wait", nullptr), 0), (m << 8) | an);
                    sc.on[a] = m; sc.doing[a] = an; acts.push_back(a);
                }
                json cam = get(st, "camera", true);
                if (!acts.empty() && !(cam.is_boolean() && !cam.get<bool>())) camera(sc, acts, shot(sc, cam, acts));
            } else if (st.contains("narrate")) {
                if (sc.place != 0 || sc.dirty) cmd(sc, 0x10, 0x00, 0x0002);
                sc.place = 0; sc.dirty = false;
                for (const json& t : texts(st.at("narrate"))) line(sc, 0x32, t);
            } else if (st.contains("tron")) {   // a man on the Titantron (storyc.py): effect 4, his video, the bubble over it
                if (!st.contains("say")) fail("tron: what he says (\"say\")");
                int64_t code = who(st.at("tron"));
                if (!code) fail("tron: who is " + repr(st.at("tron")) + "?");
                auto it = std::find(sc.cast.begin(), sc.cast.end(), code);
                if (it == sc.cast.end()) { sc.cast.push_back(code); it = sc.cast.end() - 1; }
                int64_t k = it - sc.cast.begin();
                if (sc.place == 0) restore(sc);
                if (sc.place != 6 && sc.place != 7 && sc.place != 8)    // (the fallback: never a reason to refuse the story)
                    fprintf(stderr, "[stories] warning: tron %s: no Titantron backstage (ring or stage only): the bubble alone\n", repr(st.at("tron")).c_str());
                bool arena = sc.place == 6 || sc.place == 7 || sc.place == 8;
                for (const json& t : texts(st.at("say"))) {
                    if (arena) {
                        cmd(sc, 0x70, 0x01, sceneop(SO_TRON, 0, 0, k | (sc.place == 6 ? 0x40 : 0)));
                        cmd(sc, 0x54, 0x00, 0);
                    }
                    line(sc, 0x30, t);
                }
                if (arena) {
                    cmd(sc, 0x70, 0x00, sceneop(SO_TRON, 0, 0, 0x80));
                    std::vector<int> acts;
                    for (auto& kv : sc.on) acts.push_back(kv.first);
                    if (!acts.empty()) camera(sc, acts, shot(sc, true, acts));
                }
            } else if (st.contains("commentary")) {   // JR / King at the announce table (storyc.py)
                const json& c = st.at("commentary");
                std::string cl = c.is_string() ? py_lower(c.get<std::string>()) : "";
                if (cl != "jr" && cl != "king") fail("commentary " + repr(c) + ": \"jr\" or \"king\"");
                if (!st.contains("say")) fail("commentary: what he says (\"say\")");
                if (sc.real != 7) fail("commentary: in the ring (place \"ring\"), where the announce table is");
                if (sc.place == 0 || sc.dirty) restore(sc);
                std::string pre = cl == "jr" ? "JR: " : "KING: ";
                std::vector<int> acts;
                for (auto& kv : sc.on) acts.push_back(kv.first);
                cmd(sc, 0x70, 0x01, sceneop(SO_CAM, 0, 0, 0));
                camera(sc, acts, COMMENTARY_SHOT);
                for (json t : texts(st.at("say"))) {
                    if (t.is_string()) t = pre + t.get<std::string>();
                    else if (t.is_object()) for (auto it = t.begin(); it != t.end(); ++it) if (it.value().is_string()) it.value() = pre + it.value().get<std::string>();
                    line(sc, 0x30, t);
                }
                if (!acts.empty()) camera(sc, acts, shot(sc, true, acts));
            } else if (st.contains("say")) {
                int a = st.contains("who") ? actor(sc, st.at("who"), "say") : -1;
                if (a >= 0) need3d(sc);
                frame(sc, a, st);
                for (const json& t : texts(st.at("say"))) line(sc, 0x30, t);
            } else if (st.contains("ask")) {
                json opts = get(st, "options", nullptr);
                if (!opts.is_array() || opts.size() < 2 || opts.size() > 4) fail("ask: 2 to 4 options");
                const json& q = st.at("ask");
                bool ok = q.is_string();
                for (const json& o : opts) ok = ok && o.is_string();
                if (!ok) fail("ask: text");
                int a = st.contains("who") ? actor(sc, st.at("who"), "ask") : -1;
                if (a >= 0) need3d(sc);
                frame(sc, a, st);
                std::string all = q.get<std::string>();
                for (const json& o : opts) {
                    if (o.get<std::string>().find('\n') != std::string::npos) fail("ask: an option is one line");
                    all += "\n" + o.get<std::string>();
                }
                fit(json(all), "ask");
                cmd(sc, 0x40 | (int64_t)opts.size(), 0x00, text(json(names(all, sc.cast))));
                if (st.contains("store")) {     // the answer kept in a variable (1, 2 ...): a scene op after the question
                    const json& v = st.at("store");
                    if (!v.is_string() || !vars.count(v.get<std::string>())) fail("store " + repr(v) + ": a variable (\"vars\")");
                    cmd(sc, 0x70, 0x00, sceneop(SO_STORE, 0, 0, vars.at(v.get<std::string>())));
                }
            } else if (st.contains("if")) {
                const json& c = st.at("if");
                int64_t n = c.is_string() ? choice_term(strip(c.get<std::string>())) : -1;
                if (n == 0) fail("if: choices count from 1");
                std::pair<const char*, int> ops[2] = { { "then", 0x79 }, { "else", 0x78 } };
                if (n >= 1) n -= 1;      // (as before: the engine's own choice test)
                else {                   // any condition: a scene op of ours (story.c story_scene_cond)
                    if (!c.is_string()) fail("if " + repr(c) + ": a condition");
                    std::vector<Bytes> cs;
                    bool prog = false;
                    cond(c, cs, prog, true);
                    n = sceneop(SO_COND, (int64_t)conds.size(), (int64_t)cs.size(), 0);
                    conds.insert(conds.end(), cs.begin(), cs.end());
                    ops[0].second = 0x71;
                    ops[1].second = st.contains("then") ? 0x74 : 0x70;
                }
                std::vector<Bytes> keep_out = std::move(sc.out);
                int k_place = sc.place, k_real = sc.real;
                std::map<int, int> k_on = sc.on, k_doing = sc.doing;
                bool k_dirty = sc.dirty, changed = false;
                for (auto& kv : ops) {
                    if (!st.contains(kv.first)) continue;
                    sc.out.clear(); sc.place = k_place; sc.real = k_real; sc.on = k_on; sc.doing = k_doing; sc.dirty = k_dirty;
                    sc.speaker = -1;
                    steps(sc, steps_of(st.at(kv.first)));
                    changed = changed || sc.place != k_place || sc.real != k_real || sc.on != k_on;   // (a place, or men moved)
                    if (sc.out.size() > 255) fail("if: 255 commands at most");
                    keep_out.push_back(Out().B(kv.second).B((int64_t)sc.out.size()).H(n).b);
                    keep_out.insert(keep_out.end(), sc.out.begin(), sc.out.end());
                }
                sc.out = std::move(keep_out);
                sc.place = k_place; sc.real = k_real; sc.on = k_on; sc.doing = k_doing; sc.dirty = k_dirty;
                sc.speaker = -1;
                if (changed) sc.dirty = true;
            } else if (st.contains("set")) {     // variables / roles changed in the scene (a branch's "set")
                std::vector<Bytes> ss = setops(st.at("set"));
                cmd(sc, 0x70, 0x00, sceneop(SO_SET, (int64_t)sets.size(), (int64_t)ss.size(), 0));
                sets.insert(sets.end(), ss.begin(), ss.end());
            } else if (st.contains("who")) {
                need3d(sc);
                int a = actor(sc, st.at("who"), "a step");
                int an = anim_of(get(st, "do", "stand"));
                if (is_walk(an)) {
                    int m;
                    if (st.contains("mark")) m = num(st.at("mark"), 0, 97, "mark");
                    else {
                        auto d = DOORS.find(place7(sc.place));
                        if (d == DOORS.end()) fail("walk: this place has no door (\"mark\" for one)");
                        m = d->second[num(get(st, "door", 1), 1, (int64_t)d->second.size(), "door") - 1];
                    }
                    json cam = get(st, "camera", true);
                    if (!(cam.is_boolean() && !cam.get<bool>()) && DOORCAM.count(m)) camera(sc, { a }, DOORCAM.at(m));
                    cmd(sc, 0x20 | a, wait_of(get(st, "wait", nullptr), 60), (m << 8) | an);
                    sc.on[a] = m + 1; sc.doing[a] = A_STAND;
                } else if (an == A_ENTRANCE || an == A_TAG_ENTRANCE) {
                    if (sc.place != 6) fail("an entrance is made on the stage");
                    if (an == A_TAG_ENTRANCE && (!st.contains("with") || actor(sc, st.at("with"), "with") != a + 1))
                        fail("a tag entrance: \"with\" his partner, the next man in the scene's cast");
                    cmd(sc, 0x20 | a, wait_of(get(st, "wait", nullptr), 0xFF), an);
                    sc.on.erase(a); sc.doing.erase(a);
                } else {
                    int m = mark_of(sc, st, a);
                    if (m < 0) fail(repr(st.at("who")) + " isn't on screen: say where (\"at\")");
                    cmd(sc, 0x20 | a, wait_of(get(st, "wait", nullptr), is_taunt(an) ? 120 : 0), (m << 8) | an);
                    sc.on[a] = m; sc.doing[a] = an;
                }
                if (sc.speaker == a) sc.speaker = -1;
            } else if (st.contains("camera")) {
                need3d(sc);
                std::vector<int> acts;
                if (st.contains("on")) {
                    const json& on = st.at("on");
                    if (on.is_array()) { for (const json& w : on) acts.push_back(actor(sc, w, "camera")); }
                    else acts.push_back(actor(sc, on, "camera"));
                } else for (auto& kv : sc.on) acts.push_back(kv.first);
                if (acts.empty()) fail("camera: on whom?");
                camera(sc, acts, shot(sc, st.at("camera"), acts));
            } else if (st.contains("music")) {
                const json& v = st.at("music");
                if (v != "on" && v != "off") fail("music: \"on\" or \"off\"");
                cmd(sc, 0x50 | (v == "on" ? EFFECT_MUSIC : EFFECT_QUIET), 0x00, 0);
            } else if (st.contains("wait")) {
                cmd(sc, 0x53, num(st.at("wait"), 1, 254, "wait (frames)"), 0);
            } else if (st.contains("effect")) {
                cmd(sc, 0x50 | num(st.at("effect"), 0, 10, "effect"), 0x00, 0);
            } else if (st.contains("raw")) {
                const json& r = st.at("raw");
                bool ok = r.is_string() && r.get_ref<const std::string&>().size() == 8;
                if (ok) for (char ch : r.get_ref<const std::string&>()) ok = ok && isxdigit((uint8_t)ch);
                if (!ok) fail("raw " + repr(r) + ": 8 hex digits");
                uint32_t v = (uint32_t)strtoul(r.get_ref<const std::string&>().c_str(), nullptr, 16);
                sc.out.push_back(Out().raw32(v).b);
            } else {
                fail(repr(st) + ": a step is place, who, say, tron, commentary, ask, narrate, if, set, camera, music, wait, effect or raw");
            }
        }
    }

    // storyc.py's Pack.scene3d: the pages, then the scene; one scene, one cast; "also" named with it
    int64_t scene3d(const json& pages, const json& scd, std::vector<std::string>* also = nullptr) {
        Scene sc;
        std::vector<json> list = steps_of(scd);
        gather(sc, list);
        int64_t pos = (int64_t)cmds.size();
        if (truthy(pages)) {
            cmd(sc, 0x10, 0x00, 0x0002);
            for (const json& p : py_iter(pages)) line(sc, 0x32, p);
        }
        steps(sc, list);
        cmd(sc, 0x00, 0xFF, 0x0000);
        if (also) for (std::string& t : *also) t = names(t, sc.cast);
        if (sc.cast.size() > 8) fail("a scene names 8 Superstars at most");
        cmds.insert(cmds.end(), sc.out.begin(), sc.out.end());
        int64_t first = 0;
        for (int64_t c : sc.cast) {
            int64_t k = part({ c });
            if (!first) first = k;
        }
        scenes.push_back(Out().H(first).H(pos).B((int64_t)sc.cast.size()).B(5).b);
        return (int64_t)scenes.size();
    }

    // a condition (storyc.py cond): terms with and / or / not and ( ). With "and" alone -> (the game's mask, [our
    // terms], all to hold); otherwise (or program) a postfix program in the conds -> (0, [records], prog = true)
    struct CTok { std::string kind, text; };
    static std::vector<CTok> cond_tokens(const std::string& expr) {
        std::vector<CTok> toks;
        std::vector<std::string> words;
        auto flush = [&]() {
            if (words.empty()) return;
            std::string t;
            for (size_t k = 0; k < words.size(); k++) t += (k ? " " : "") + words[k];
            toks.push_back({ "t", t });
            words.clear();
        };
        auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        size_t i = 0, n = expr.size();
        while (i < n) {
            char c = expr[i];
            if (sp(c)) { i++; continue; }
            if (c == '(' || c == ')') { flush(); toks.push_back({ std::string(1, c), std::string(1, c) }); i++; continue; }
            size_t j = i;
            while (j < n && !sp(expr[j]) && expr[j] != '(' && expr[j] != ')') j++;
            std::string w = expr.substr(i, j - i);
            i = j;
            std::string lw = py_lower(w);
            if (lw == "and" || lw == "or" || lw == "not") { flush(); toks.push_back({ lw, w }); }
            else words.push_back(w);
        }
        flush();
        return toks;
    }
    static Bytes rec(int kind, int op = 0, int64_t a = 0, int64_t b = 0) { return Out().B(kind).B(op).H(a).i(b).b; }
    // one term -> the game's mask, and records (all to hold)
    int64_t term(const std::string& term, std::vector<Bytes>& out) {
        std::string low = py_lower(term);
        auto g = GAME.find(low);
        if (g != GAME.end()) return g->second;
        static const std::pair<const char*, int> pre[3] = { { "win by ", 0x1 }, { "lose by ", 0x2 }, { "ended by ", 0 } };
        for (auto& p : pre) {
            if (low.rfind(p.first, 0) != 0) continue;
            std::string h = strip(low.substr(strlen(p.first)));
            for (auto& kv : HOW) if (kv.first == h) { out.push_back(rec(C_HOW, 0, 0, kv.second)); return p.second; }
            std::string names;
            for (auto& kv : HOW) names += (names.empty() ? "" : ", ") + kv.first;
            fail(json(term).dump() + ": by " + names);
        }
        auto skip_ws = [](const std::string& t, size_t& i) { while (i < t.size() && is_ws(t[i])) i++; };
        auto take_digits = [](const std::string& t, size_t& i) {
            size_t a = i;
            while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++;
            return t.substr(a, i - a);
        };
        size_t i;
        int64_t ch = choice_term(low);       // choice\s*(==|=)?\s*(\d+)$
        if (ch >= 0) {
            if (ch < 1) fail(json(term).dump() + ": choices count from 1");
            out.push_back(rec(C_CHOICE, 0, 0, ch - 1));
            return 0;
        }
        if (low.compare(0, 6, "chance") == 0) {   // chance\s+(\d+)%?$
            i = 6;
            size_t a = i;
            skip_ws(low, i);
            if (i > a) {
                std::string d = take_digits(low, i);
                if (i < low.size() && low[i] == '%') i++;
                if (!d.empty() && i == low.size()) { out.push_back(rec(C_CHANCE, 0, 0, digits_value(d))); return 0; }
            }
        }
        {   // (\w+)\s*(==|!=|<=|>=|<|>)\s*(-?\d+)$, the name a variable
            i = 0;
            while (i < term.size() && (isalnum((uint8_t)term[i]) || term[i] == '_' || (uint8_t)term[i] >= 0x80)) i++;
            std::string name = term.substr(0, i);
            skip_ws(term, i);
            static const char* const ops[] = { "==", "!=", "<=", ">=", "<", ">" };
            static const int opv[] = { 0, 1, 3, 5, 2, 4 };
            int op = -1;
            for (int k = 0; k < 6 && op < 0 && !name.empty(); k++)
                if (term.compare(i, strlen(ops[k]), ops[k]) == 0) { op = opv[k]; i += strlen(ops[k]); }
            if (op >= 0) {
                skip_ws(term, i);
                bool neg = i < term.size() && term[i] == '-';
                if (neg) i++;
                std::string d = take_digits(term, i);
                auto v = vars.find(name);
                if (!d.empty() && i == term.size() && v != vars.end()) {
                    int64_t n = digits_value(d);
                    out.push_back(rec(C_VAR, op, v->second, neg ? -n : n));
                    return 0;
                }
            }
        }
        for (int is = 1; is >= 0; is--) {      // "<role> is <Superstar>", then "<role> in <group>"
            size_t k = low.find(is ? " is " : " in ");
            if (k == std::string::npos || k == 0) continue;
            std::string r = strip(term.substr(0, k)), w = strip(term.substr(k + 4));
            auto ro = roles.find(r);
            if (ro == roles.end()) fail(json(term).dump() + ": " + json(r).dump() + " isn't a role (player, partner, a cast role)");
            if (is) out.push_back(rec(C_ROLE_IS, 0, ro->second, star(json(w))));
            else out.push_back(rec(C_ROLE_IN, 0, ro->second, group(w)));
            return 0;
        }
        fail("can't read the condition " + json(term).dump());
    }
    typedef std::pair<std::vector<Bytes>, int> Prog;   // (records, stack depth)
    int64_t cond(const json& expr_j, std::vector<Bytes>& out, bool& prog, bool program = false) {
        if (!expr_j.is_string()) fail("can't read the condition " + repr(expr_j));
        const std::string& expr = expr_j.get_ref<const std::string&>();
        std::vector<CTok> toks = cond_tokens(expr);
        prog = false;
        if (toks.empty()) fail("can't read the condition " + json(expr).dump());
        bool plain = !program;
        for (auto& t : toks) if (t.kind == "or" || t.kind == "not" || t.kind == "(" || t.kind == ")") plain = false;
        if (plain) {
            bool ok = toks.size() % 2 == 1;
            for (size_t k = 0; k < toks.size(); k++) if (toks[k].kind != (k % 2 == 0 ? "t" : "and")) ok = false;
            if (!ok) fail("can't read the condition " + json(expr).dump());
            int64_t mask = 0;
            for (size_t k = 0; k < toks.size(); k += 2) mask |= term(toks[k].text, out);
            return mask;
        }
        size_t pos = 0;
        auto peek = [&]() -> std::string { return pos < toks.size() ? toks[pos].kind : std::string(); };
        std::function<Prog()> unary, conj, disj;
        auto binary = [&](std::function<Prog()>& sub, const char* op, int code) {
            Prog r = sub();
            while (peek() == op) {
                pos++;
                Prog r2 = sub();
                r.first.insert(r.first.end(), r2.first.begin(), r2.first.end());
                r.first.push_back(rec(code));
                r.second = std::max(r.second, r2.second + 1);
            }
            return r;
        };
        unary = [&]() -> Prog {
            std::string k = peek();
            if (k == "not") { pos++; Prog r = unary(); r.first.push_back(rec(C_NOT)); return r; }
            if (k == "(") {
                pos++;
                Prog r = disj();
                if (peek() != ")") fail(json(expr).dump() + ": a ( without its )");
                pos++;
                return r;
            }
            if (k == "t") {
                std::string t = toks[pos++].text;
                std::vector<Bytes> o;
                int64_t m = term(t, o);
                std::vector<Bytes> r;
                if (m) r.push_back(rec(C_GAME, 0, 0, m));
                r.insert(r.end(), o.begin(), o.end());
                size_t n = r.size();
                for (size_t q = 1; q < n; q++) r.push_back(rec(C_AND));
                return { r, (int)std::min<size_t>(n, 2) };
            }
            fail(json(expr).dump() + ": a term missing" + (k.empty() ? std::string(" at the end") : " before " + json(toks[pos].text).dump()));
        };
        conj = [&]() { return binary(unary, "and", C_AND); };
        disj = [&]() { return binary(conj, "or", C_OR); };
        Prog r = disj();
        if (pos != toks.size()) fail(json(expr).dump() + ": " + json(toks[pos].text).dump() + " out of place");
        if (r.second > COND_DEPTH || r.first.size() > 255) fail(json(expr).dump() + ": too long");
        out.insert(out.end(), r.first.begin(), r.first.end());
        prog = true;
        return 0;
    }

    std::vector<Bytes> setops(const json& d) {
        if (!d.is_object()) fail("set: " + repr(d) + " isn't a dict");
        std::vector<Bytes> out;
        for (auto it = d.begin(); it != d.end(); ++it) {
            const std::string& k = it.key();
            const json& v = it.value();
            auto r = roles.find(k);
            if (r != roles.end()) {      // a role takes another's Superstar ("rival": "partner") or a named one
                auto rv = v.is_string() ? roles.find(v.get<std::string>()) : roles.end();
                if (rv != roles.end()) out.push_back(Out().B(S_ROLE_COPY).B(r->second).h(rv->second).b);
                else out.push_back(Out().B(S_ROLE_SET).B(r->second).h(who(v)).b);
                continue;
            }
            auto var = vars.find(k);
            if (var == vars.end()) fail("unknown variable " + json(k).dump());
            if (v.is_string() && (v.get_ref<const std::string&>().empty() || v.get_ref<const std::string&>()[0] == '+' ||
                                  v.get_ref<const std::string&>()[0] == '-'))
                out.push_back(Out().B(S_ADD).B(var->second).h(py_int(v)).b);
            else out.push_back(Out().B(S_SET).B(var->second).h(py_int(v)).b);
        }
        return out;
    }

    Bytes node(const std::string& n, const json& d, std::map<std::string, int64_t>& idx) {   // a node -> its 18 bytes
            if (!d.is_object()) fail("isn't a node (a dict)");
            int64_t title = text(get(d, "chapter", ""));
            json reqtext_j = get(d, "requirement", "");
            if (!reqtext_j.is_string()) fail("requirement " + repr(reqtext_j) + " isn't a string");
            fit(reqtext_j, "requirement");
            std::vector<std::string> also = { reqtext_j.get<std::string>() };
            int64_t scene;
            if (d.contains("scene")) scene = scene3d(get(d, "pages", json::array()), d.at("scene"), &also);
            else {
                if (has_names(also[0]) && !truthy(get(d, "pages", nullptr)))
                    fail("a requirement that names Superstars needs pages or a scene before the match (the names come from them)");
                scene = narration(get(d, "pages", json::array()), &also);
            }
            const std::string& reqtext = also[0];
            if (!d.contains("match")) fail("no match");
            const json& m = d.at("match");
            if (!m.is_object()) fail("match " + repr(m) + " isn't a dict");
            json type_j = get(m, "type", "single");
            std::vector<json> tslots;
            if (m.contains("teams")) type_j = teams_of(m, tslots);
            if (!type_j.is_string() || !TYPES.count(type_j.get<std::string>())) fail("match type " + repr(type_j));
            const std::string tname = type_j.get<std::string>();
            int typ = TYPES.at(tname).first;
            size_t nslots = (size_t)TYPES.at(tname).second;
            std::vector<json> slots = m.contains("teams") ? tslots : py_iter(get(m, "slots", json::array({ "player", "-" })));
            json runins = get(m, "runins", json::array());
            std::map<size_t, int64_t> rslot;      // (slot -> its part's low byte: when he comes; storyc.py)
            if (truthy(runins)) {
                // singles: one a side (type 14: slot 2 the player's man, slot 3 the opponents'); handicap: one (type 18)
                bool ok = runins.is_array();
                if (ok) for (const json& r : runins) ok = ok && r.is_object() && r.contains("who");
                if (!ok) fail("runins: a list of {\"who\": ...}");
                if (typ == 0 && runins.size() <= 2) {
                    typ = TYPES.at("single_runin").first; nslots = (size_t)TYPES.at("single_runin").second;
                    slots.resize(std::min<size_t>(slots.size(), 2));
                } else if (typ == 3 && runins.size() == 1) {
                    typ = TYPES.at("handicap_runin").first; nslots = (size_t)TYPES.at("handicap_runin").second;
                    slots.resize(std::min<size_t>(slots.size(), 3));
                } else fail("run-ins: two in a singles match, one in a handicap match");
                std::map<size_t, json> who_at;
                for (const json& r : runins) {
                    json side = get(r, "helps", "opponents");
                    if (!side.is_string() || !RUNIN.count(side.get<std::string>())) fail("run-in helps " + repr(side) + ": player or opponents");
                    if (typ == 18 && side != "opponents") fail("a handicap match's run-in helps the opponents");
                    size_t i = side == "opponents" ? 3 : 2;
                    if (rslot.count(i)) fail("run-ins: one for each side");
                    int64_t at = RUNIN.at(side.get<std::string>());
                    if (r.contains("at")) {
                        const json& t = r.at("at");
                        int64_t sec = is_int(t) ? py_int(t) : clock_of(t);
                        auto c = RUNIN_AT.find(sec);
                        if (c == RUNIN_AT.end()) fail("run-in at " + repr(t) + ": one of 0:15, 0:30, 0:35, 0:40, 0:45, 1:00, 1:10, 1:30, 2:00, 3:00");
                        at = c->second;
                    }
                    rslot[i] = at; who_at[i] = r.at("who");
                }
                for (size_t i = slots.size(); i < nslots; i++) slots.push_back(who_at.count(i) ? who_at[i] : json("-"));
            }
            if (slots.size() != nslots && typ != 9)
                fail("a " + repr(get(m, "type", nullptr)) + " match takes " + std::to_string(nslots));
            // a slot: a role / Superstar, or a list of them (the engine takes the first that can be in the match)
            auto cands = [&](const json& w) {
                std::vector<int64_t> cs;
                if (w.is_array()) { for (const json& x : w) if (int64_t c = who(x)) cs.push_back(c); }
                else if (int64_t c = who(w)) cs.push_back(c);
                return cs;
            };
            int64_t p0 = -1;
            for (size_t i = 0; i < slots.size(); i++) {
                int64_t extra = rslot.count(i) ? rslot[i] : 0;
                std::vector<int64_t> cs = cands(slots[i]);
                if (cs.size() > 5) fail("5 candidates a slot at most");
                int64_t k = part(cs, extra);
                if (p0 < 0) p0 = k;
            }
            if (p0 < 0) fail("no slots");
            {
                int k = 0;
                json sj = get(m, "script", json::array());
                if (!sj.is_array() || sj.size() > 16) fail("script: a list of 16 triggers at most");
                for (const json& tr : py_iter(sj)) {
                    k++;
                    try { trigger(tr, idx[n], typ, rslot); }
                    catch (const StoryError& e) { fail("script " + std::to_string(k) + ": " + e.what()); }
                }
            }
            int64_t req = text(reqtext);
            int64_t b0 = (int64_t)branches.size();
            int bi = 0;
            for (const json& b : py_iter(get(d, "branches", json::array()))) {
                bi++;
                try { branch(b, idx); }
                catch (const StoryError& e) { fail("branch " + std::to_string(bi) + ": " + e.what()); }
            }
            if ((int64_t)branches.size() == b0 || branches.back()[0] || branches.back()[1] || branches.back()[2] ||
                branches.back()[3] || branches.back()[6])
                branches.push_back(Out().I(0).H(0).B(0).B(0).h(0).H(0xFFFF).H(0).H(0).B(0).B(0).x(2).b);  // (the default: the end)
            std::map<std::string, json> nd = { {"a", 0x50}, {"b", 0x02}, {"c", 0x00}, {"e", 0x0F}, {"f", 0x00}, {"flags", 0x0008} };
            if (d.contains("retries")) nd["f"] = (strict_int(nd["f"]) & 0xF0) | (py_int(d.at("retries")) & 0xF);
            // the rules (node +E): the type's own unless named
            auto rl = RULES.find(tname);
            if (m.contains("rules")) {
                const json& rj = m.at("rules");
                std::string rn = py_lower(rj.is_string() ? rj.get<std::string>() : repr(rj));
                int e = -1;
                if (rl != RULES.end()) for (auto& kv : rl->second) if (kv.first == rn) { e = kv.second; break; }
                if (e < 0) fail("rules " + repr(rj) + " (not one for " + tname + ")");
                nd["e"] = e;
            } else if (rl != RULES.end()) nd["e"] = rl->second[0].second;
            // a title on the line (node +C: the belt + 1)
            if (m.contains("title")) {
                const json& tj = m.at("title");
                std::string tb = py_lower(tj.is_string() ? tj.get<std::string>() : repr(tj));
                if (!BELTS.count(tb)) fail("title " + repr(tj) + ": a championship");
                nd["c"] = (strict_int(nd["c"]) & 0xF0) | (BELTS.at(tb) + 1);
            }
            // a match backstage (node +A bits 4-7)
            if (m.contains("location")) {
                const json& lj = m.at("location");
                std::string ln = py_lower(lj.is_string() ? lj.get<std::string>() : repr(lj));
                if (!LOCATIONS.count(ln)) fail("location " + repr(lj) + ": ring, pool hall, parking lot or boiler room");
                nd["a"] = (strict_int(nd["a"]) & 0x0F) | LOCATIONS.at(ln);
            }
            if (m.contains("arena")) {
                const json& aj = m.at("arena");
                std::string an = aj.is_string() ? aj.get<std::string>() : repr(aj);
                for (char& ch : an) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                int ai = -1;
                for (int k = 0; k < 10; k++) if (an == ARENAS[k]) ai = k;
                // (the arenas of ours: _roster.json's "@ARENA:<NAME>" -> 10 + k, played in venue "@ARENA_BASE"; the
                // nibble the base, the arena in the pack's arena list)
                std::string names = "raw is war, no mercy, smackdown, king of the ring, summerslam, survivor series, royal rumble, wrestlemania, backlash, armageddon";
                for (auto& kv : R) if (kv.first.rfind("@ARENA:", 0) == 0) names += ", " + py_lower(kv.first.substr(7));
                auto xa = R.find("@ARENA:" + py_upper(an));
                auto xb = R.find("@ARENA_BASE");
                if (ai < 0 && xa != R.end() && xb != R.end()) {
                    xarena.push_back({ idx[n], xa->second });
                    ai = (int)xb->second;
                }
                if (ai < 0) fail("arena " + repr(aj) + " (one of " + names + ")");
                nd["a"] = (strict_int(nd["a"]) & 0xF0) | ai;
            }
            json raw = get(d, "raw", json::object());
            if (raw.is_object()) {
                for (auto it = raw.begin(); it != raw.end(); ++it) nd[it.key()] = it.value();
            } else if (raw.is_array()) {      // (dict.update() takes pairs too)
                for (const json& pr : raw) {
                    std::vector<json> kv = py_iter(pr);
                    if (kv.size() != 2) fail("raw " + repr(raw));
                    if (kv[0].is_string()) nd[kv[0].get<std::string>()] = kv[1];
                }
            } else fail("raw " + repr(raw) + " isn't a dict");
            return Out().H(title).H(scene).H(req).H(p0).H(b0).B(strict_int(nd["a"])).B(strict_int(nd["b"]))
                        .B(strict_int(nd["c"])).B(typ).B(strict_int(nd["e"])).B(strict_int(nd["f"]))
                        .H(strict_int(nd["flags"])).b;
    }

    // a match's trigger (storyc.py trigger): -> X_MSCRIPT (u16 node, u8 when, u8 its role, u16 its number, u8 do, u8 a1..a3)
    void trigger(const json& tr, int64_t node, int typ, std::map<size_t, int64_t>& rslot) {
        if (!tr.is_object() || !tr.contains("when") || !tr.contains("do")) fail(repr(tr) + ": {\"when\": ..., \"do\": ...}");
        const json& wj = tr.at("when");
        if (!wj.is_string()) fail("when " + repr(wj));
        std::string w1;                       // " ".join(w.split())
        {
            const std::string& w = wj.get_ref<const std::string&>();
            size_t i = 0;
            while (i < w.size()) {
                while (i < w.size() && is_ws(w[i])) i++;
                size_t j = i;
                while (j < w.size() && !is_ws(w[j])) j++;
                if (j > i) w1 += (w1.empty() ? "" : " ") + w.substr(i, j - i);
                i = j;
            }
        }
        std::string low = py_lower(w1);
        auto role = [&](const json& x) -> int64_t {
            json y = x.is_string() ? json(strip(x.get<std::string>())) : x;
            if (!y.is_string() || !roles.count(y.get<std::string>())) fail(repr(y) + ": a role (player, a cast role) in this match");
            return roles.at(y.get<std::string>());
        };
        auto ends = [&](const char* e) { size_t n = strlen(e); return low.size() >= n && low.compare(low.size() - n, n, e) == 0; };
        int64_t ck = 0, cr = 0, cv = 0;
        if (low.rfind("time ", 0) == 0) { ck = 1; cv = clock_of(json(low.substr(5))); }
        else if (low.rfind("health ", 0) == 0) {
            // health (.+?) ?< ?(\d+)%?$
            size_t lt = w1.rfind('<');
            bool ok = lt != std::string::npos && lt > 7;
            std::string who, num;
            if (ok) {
                who = w1.substr(7, lt - 7);
                if (!who.empty() && who.back() == ' ') who.pop_back();
                size_t i = lt + 1;
                if (i < w1.size() && w1[i] == ' ') i++;
                size_t a = i;
                while (i < w1.size() && w1[i] >= '0' && w1[i] <= '9') i++;
                num = w1.substr(a, i - a);
                if (i < w1.size() && w1[i] == '%') i++;
                ok = !who.empty() && !num.empty() && i == w1.size() && who.find('<') == std::string::npos;
            }
            int64_t pct = ok ? digits_value(num) : 0;
            if (!ok || pct < 1 || pct > 99) fail("when " + repr(wj) + ": \"health <role> < 30%\"");
            ck = 2; cr = role(json(who)); cv = pct;
        }
        else if (ends(" outside")) { ck = 3; cr = role(json(w1.substr(0, w1.size() - 8))); }
        else if (ends(" in a hold")) { ck = 4; cr = role(json(w1.substr(0, w1.size() - 10))); }
        else if (low.rfind("finisher by ", 0) == 0) { ck = 5; cr = role(json(w1.substr(12))); }
        else fail("when " + repr(wj) + ": time M:SS, health <role> < N%, <role> outside, <role> in a hold, finisher by <role>");
        const json& d = tr.at("do");
        int64_t ak = 0, a1 = 0, a2 = 0, a3 = 0;
        if (d == "end") {
            a1 = role(get(tr, "winner", nullptr));
            if (tr.contains("loser")) a2 = role(tr.at("loser"));
            else if (typ == 0 || typ == 4 || typ == 5 || typ == 14 || typ == 17) a2 = 0xFF;   // (one on one: the other man)
            else fail("end: \"loser\" (this match has more than two)");
            json h = get(tr, "how", "pinfall");
            auto e = h.is_string() ? END_HOW.find(h.get<std::string>()) : END_HOW.end();
            if (e == END_HOW.end()) fail("end: how " + repr(h) + " (pinfall, pin, submission, tko, dq, countout, count-out, count out, ladder, escape)");
            ak = 1; a3 = e->second;
        } else if (d == "runin") {
            json side = get(tr, "side", rslot.count(3) ? "opponents" : "player");
            if (!side.is_string() || !RUNIN.count(side.get<std::string>()) || !rslot.count(side == "opponents" ? 3 : 2))
                fail("runin: the match's run-in for " + repr(side) + " (\"runins\")");
            ak = 2; a1 = side == "opponents" ? 1 : 0;
        } else if (d == "no dq") ak = 3;
        else if (d == "no countout") ak = 4;
        else fail("do " + repr(d) + ": end, runin, no dq or no countout");
        ext[X_MSCRIPT].push_back(Out().H(node).B(ck).B(cr).H(cv).B(ak).B(a1).B(a2).B(a3).H(0).b);
    }

    void branch(const json& b, std::map<std::string, int64_t>& idx) {
        if (!b.is_object()) fail(repr(b) + " isn't a branch (a dict)");
        int64_t mask = 0;
        std::vector<Bytes> cs;
        bool prog = false;
        if (b.contains("if")) mask = cond(b.at("if"), cs, prog);
        std::vector<Bytes> ss = setops(get(b, "set", json::object()));
        json nxt_j = get(b, "next", "end");
        int64_t nxt;
        if (nxt_j == "end") nxt = 0xFFFF;
        else if (nxt_j == "gameover") nxt = 0;
        else if (nxt_j.is_string() && idx.count(nxt_j.get<std::string>())) nxt = idx[nxt_j.get<std::string>()];
        else fail("a branch goes to " + repr(nxt_j) + ", which isn't a node");
        int64_t after = b.contains("scene") ? scene3d(get(b, "pages", json::array()), b.at("scene"))
                                            : narration(get(b, "pages", json::array()));
        int64_t money = py_int(get(b, "money", 0));
        branches.push_back(Out().I(mask).H((int64_t)conds.size()).B((int64_t)cs.size()).B(0).h(money).H(nxt)
                                .H(after).H((int64_t)sets.size()).B((int64_t)ss.size()).B(prog ? 1 : 0).x(2).b);
        conds.insert(conds.end(), cs.begin(), cs.end());
        sets.insert(sets.end(), ss.begin(), ss.end());
    }

    // the voices the story's tokens use: for each field used, every Superstar's (ID order; 0 the default)
    void voice_tables() {
        for (int f : voice_used)
            for (auto& v : R.voices) {
                auto it = v.second.find(f);
                if (it == v.second.end() || it->second.empty()) continue;
                int64_t first = (int64_t)strings.size();
                for (const std::string& t : it->second) strings.push_back(t);
                ext[X_VOICE].push_back(Out().H(v.first).B(f).B((int64_t)it->second.size()).H(first).H(0).b);
            }
    }

    Bytes build(int belt) {
        const json& nodes_j = st.at("nodes");
        std::vector<std::string> order;
        for (auto it = nodes_j.begin(); it != nodes_j.end(); ++it) order.push_back(it.key());
        std::map<std::string, int64_t> idx;
        for (size_t i = 0; i < order.size(); i++) idx[order[i]] = (int64_t)i + 1;   // node indexes from 1, in the file's order
        // chapters: a run of nodes with the same chapter title
        std::vector<int64_t> ch_first;
        json last = nullptr;
        for (const std::string& n : order) {
            json t = get(nodes_j.at(n), "chapter", last);
            if (t != last) { ch_first.push_back(idx[n]); last = t; }
        }
        if (ch_first.size() > 11) fail("at most 11 chapters");
        std::vector<Bytes> nodes;
        for (const std::string& n : order) {
            try { nodes.push_back(node(n, nodes_j.at(n), idx)); }
            catch (const StoryError& e) { fail("node " + json(n).dump() + ": " + e.what()); }
        }
        voice_tables();
        if (order.empty()) fail("no nodes");
        json start_j = get(st, "start", order[0]);
        if (!start_j.is_string() || !idx.count(start_j.get<std::string>())) fail("start " + repr(start_j) + " isn't a node");
        int64_t start = idx[start_j.get<std::string>()];
        std::vector<int64_t> chapters = { start, start };
        chapters.insert(chapters.end(), ch_first.begin(), ch_first.end());
        for (size_t k = ch_first.size(); k < 12; k++) chapters.push_back((int64_t)order.size() + 1);
        Out out;
        out.I(MAGIC).H(1).H(belt).H(start).H((int64_t)nodes.size()).H((int64_t)parts.size()).H((int64_t)scenes.size())
           .H((int64_t)cmds.size()).H((int64_t)branches.size()).H((int64_t)conds.size()).H((int64_t)sets.size())
           .H((int64_t)strings.size()).H((int64_t)var_init.size());
        for (size_t k = 0; k < 14; k++) out.H(chapters[k]);
        int64_t nsecs = 0;
        for (auto& kv : ext) if (!kv.second.empty()) nsecs++;
        out.H((int64_t)xarena.size()).H(nsecs).x(4);   // (+0x38: the arena list's length; +0x3A the extension sections')
        auto put = [&](const std::vector<Bytes>& chunks) {
            for (const Bytes& c : chunks) out.b.insert(out.b.end(), c.begin(), c.end());
            while (out.b.size() % 4) out.b.push_back(0);
        };
        put(nodes); put(parts); put(scenes); put(cmds); put(branches); put(conds); put(sets);
        std::vector<Bytes> vi;
        for (int64_t v : var_init) vi.push_back(Out().i(v).b);
        put(vi);
        for (int64_t r : role_init) out.H(r);
        for (auto& xa : xarena) out.H(xa.first).H(xa.second);
        for (auto& kv : ext) {               // (the extension sections: kind, count, records)
            if (kv.second.empty()) continue;
            out.H(kv.first).H((int64_t)kv.second.size());
            for (const Bytes& r : kv.second) out.b.insert(out.b.end(), r.begin(), r.end());
        }
        std::string blob;
        for (const std::string& t : strings) {
            out.H((int64_t)blob.size());
            blob += latin1(t);
            blob += '\0';
        }
        out.b.insert(out.b.end(), blob.begin(), blob.end());
        while (out.b.size() % 4) out.b.push_back(0);
        return out.b;
    }
};

// storyc.py's check(): -> the championship (BELTS)
static int check(const json& st) {
    if (!st.is_object()) fail("not a story (a JSON object)");
    if (!st.contains("nodes")) fail("no \"nodes\"");
    json ch = get(st, "championship", "world");
    std::string names;
    for (auto& b : BELTS) names += (names.empty() ? "" : ", ") + b.first;
    if (!ch.is_string() || !BELTS.count(py_lower(ch.get<std::string>())))
        fail("championship " + repr(ch) + ": one of " + names);
    int belt = BELTS.at(py_lower(ch.get<std::string>()));
    const json& nodes = st.at("nodes");
    if (!nodes.is_object()) fail("\"nodes\" isn't a dict");
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        const json& d = it.value();
        if (!d.is_object() || !d.contains("match")) fail("node " + json(it.key()).dump() + " has no match");
        for (const json& b : py_iter(get(d, "branches", json::array()))) {
            json nx = get(b, "next", "end");
            if (nx != "end" && nx != "gameover" && !(nx.is_string() && nodes.contains(nx.get<std::string>())))
                fail("node " + json(it.key()).dump() + ": a branch goes to " + repr(nx) + ", which isn't a node");
        }
    }
    json menu = get(st, "menu", "Custom Story");
    if (!menu.is_string()) fail("menu " + repr(menu) + " isn't a string");
    if (utf8_decode(menu.get<std::string>()).size() > 24) fail("menu " + repr(menu) + ": 24 letters at most");
    return belt;
}

struct Story {
    std::string file, title, menu;
    int belt = 0;                         // (7: a story that doesn't compile -- on the list as "ERROR: <file>", its reason)
    Bytes pack;
    std::string error;
    std::vector<std::string> standins;    // (2026-11-13, removal: the removed men it names -- stand-ins play them, mod story.c)
};

// a story file -> its pack (false: err says why)
bool compile_file(const std::filesystem::path& path, const Roster& roster, Story& out, std::string& err) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) fail("can't read it");
        std::stringstream ss;
        ss << in.rdbuf();
        json st;
        try { st = json::parse(ss.str()); }
        catch (const json::exception& e) { fail(std::string("JSON: ") + e.what()); }
        int belt = check(st);
        Pack p(st, roster);
        out.pack = p.build(belt);
        out.belt = belt;
        out.file = std::string(reinterpret_cast<const char*>(path.filename().u8string().c_str()));
        json title = get(st, "title", "");
        out.title = title.is_string() ? title.get<std::string>() : title.dump();
        out.menu = get(st, "menu", "Custom Story").get<std::string>();
        out.standins = p.standins;       // (2026-11-13, removal: said in the log by the caller; the story plays with stand-ins)
        return true;
    } catch (const StoryError& e) {
        err = e.what();
    } catch (const std::exception& e) {
        err = e.what();
    }
    return false;
}

// _voices.json (storyc.py voices(): the same leniency -- what isn't right is left out; a file that isn't JSON: none)
static void load_voices(const std::filesystem::path& path, Roster& r) {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return;
        std::stringstream ss;
        ss << in.rdbuf();
        json j = json::parse(ss.str());
        if (!j.is_object()) return;
        for (auto it = j.begin(); it != j.end(); ++it) {
            const json& d = it.value();
            if (!d.is_object()) continue;
            int64_t vid = 0;
            if (it.key() != "default") {
                auto i = d.find("id");
                if (i == d.end() || !i->is_string()) continue;
                const std::string& s = i->get_ref<const std::string&>();
                if (s.size() < 3 || s.size() > 6 || s[0] != '0' || s[1] != 'x') continue;
                bool ok = true;
                for (size_t k = 2; k < s.size(); k++) ok = ok && isxdigit((uint8_t)s[k]);
                if (!ok) continue;
                vid = strtol(s.c_str() + 2, nullptr, 16);
                if (!(vid > 0 && vid < 0x1000)) continue;
            }
            std::map<int, std::vector<std::string>> f;
            for (auto& kv : VOICE_KEYS) {
                auto x = d.find(kv.first);
                std::vector<std::string> v;
                if (x != d.end()) {
                    if (x->is_string()) v.push_back(x->get<std::string>());
                    else if (x->is_array()) for (const json& e : *x) if (e.is_string()) v.push_back(e.get<std::string>());
                }
                if (v.size() > 15) v.resize(15);
                if (!v.empty()) f[kv.second] = v;
            }
            r.voices[vid] = f;
        }
    } catch (const std::exception&) { r.voices.clear(); }
}

Roster load_roster(const std::filesystem::path& path, std::string& err) {
    Roster r;
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) { err = "can't read it"; return r; }
        std::stringstream ss;
        ss << in.rdbuf();
        json j = json::parse(ss.str());
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_number_integer()) r[it.key()] = it.value().get<int64_t>();
            else if (it.key().rfind("@GROUP:", 0) == 0 && it.value().is_array()) {   // (a group: its IDs in order)
                std::vector<int64_t>& g = r.groups[it.key().substr(7)];
                for (const json& x : it.value()) if (x.is_number_integer()) g.push_back(x.get<int64_t>());
            }
        }
    } catch (const std::exception& e) {
        err = e.what();
    }
    load_voices(path.parent_path() / "_voices.json", r);
    // Wrestlers from folders can be named too, in stories and select_pages.json, by their name or short name in capitals.
#ifndef ARCADE_STORIES_TEST     // (Built alone to check it against the Python compiler, there are no folder wrestlers.)
    // (2026-11-09: a name already on the roster keeps its Superstar, so a folder wrestler named like another, or two folders
    //  alike, don't take a name from the one a story meant. Every one has his string ID too: "@ID:<id>", the folder's.)
    arcade_wrestlers_each_name([&](const std::string& name, int id) {
        auto [it, fresh] = r.emplace(name, id);
        if (!fresh && it->second != id) fprintf(stderr, "[stories] the name %s stays with 0x%04X; 0x%04X is named by his ID\n", name.c_str(), (int)it->second, id);
    });
    arcade_wrestlers_each_id([&](const std::string& sid, int id) { r["@ID:" + sid] = id; });
    arcade_wrestlers_each_removed([&](const std::string& sid, int id) { r.removed[id] = sid; });   // (2026-11-13, removal)
    // Arenas from folders too: their numbers carry on after the built-in ones _roster.json lists.
    {
        int built_in = 0;
        for (const auto& kv : r) if (kv.first.rfind("@ARENA:", 0) == 0) built_in++;
        arcade_arenas_each_name(10 + built_in, [&](const std::string& name, int number) { r["@ARENA:" + name] = number; });
    }
#endif
    return r;
}

// the stories folder: *.json (not test_* / _*), by file name -> the good ones
std::vector<Story> scan_folder(const std::filesystem::path& folder) {
    std::vector<Story> out;
    std::string err;
    Roster roster = load_roster(folder / "_roster.json", err);
    if (!err.empty()) fprintf(stderr, "[stories] _roster.json: %s (no Superstars by name)\n", err.c_str());
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto& e : std::filesystem::directory_iterator(folder, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::u8string ext = e.path().extension().u8string();
        std::string x(reinterpret_cast<const char*>(ext.c_str()));
        for (char& c : x) c = (char)tolower((uint8_t)c);
        if (x != ".json") continue;
        std::u8string name = e.path().filename().u8string();
        if (name.rfind(u8"test_", 0) == 0 || name.rfind(u8"_", 0) == 0 || name.rfind(u8".", 0) == 0 || name == u8"story.schema.json") continue;
        files.push_back(e.path());
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.filename().u8string() < b.filename().u8string(); });
    for (const auto& f : files) {
        Story s;
        std::string name(reinterpret_cast<const char*>(f.filename().u8string().c_str()));
        if (compile_file(f, roster, s, err)) {
            fprintf(stderr, "[stories] %s -> %d bytes (%s)\n", name.c_str(), (int)s.pack.size(), s.title.c_str());
            for (const std::string& m : s.standins)   // (2026-11-13, removal)
                fprintf(stderr, "[stories] %s names %s, removed from the game (his folder is gone): his stand-in plays his parts\n", name.c_str(), m.c_str());
            out.push_back(std::move(s));
        } else {
            fprintf(stderr, "[stories] %s: %s\n", name.c_str(), err.c_str());
            // (2026-11-02: it stays on the list, its reason shown when it's picked -- story.c / logic.c)
            Story b;
            b.file = name;
            std::string stem(reinterpret_cast<const char*>(f.stem().u8string().c_str()));
            b.menu = "ERROR: " + stem;
            std::vector<uint32_t> cps = utf8_decode(b.menu);
            if (cps.size() > 24) { b.menu.clear(); for (size_t k = 0; k < 24; k++) b.menu += utf8_encode(cps[k]); }
            b.belt = 7;
            b.error = name + ": " + err;
            out.push_back(std::move(b));
        }
    }
    return out;
}

} // namespace arcade_stories

#ifndef ARCADE_STORIES_TEST
namespace {

std::mutex stories_mutex;
std::vector<arcade_stories::Story> stories;
bool scanned = false;

int rescan() {
    std::lock_guard<std::mutex> lock(stories_mutex);
    stories = arcade_stories::scan_folder(arcade::program_folder() / "stories");
    scanned = true;
    return (int)stories.size();
}

const arcade_stories::Story* story_at(int i) {
    if (!scanned) rescan();
    std::lock_guard<std::mutex> lock(stories_mutex);
    return i >= 0 && i < (int)stories.size() ? &stories[i] : nullptr;
}

// recomp_arcade_story_scan() -> the number of good stories (<program folder>\stories\*.json, compiled again)
void story_scan(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, rescan());
}

// recomp_arcade_story_menu(i, buf, len) -> story i's championship (0..6; its menu name in buf), -1 if no story i
void story_menu(uint8_t* rdram, recomp_context* ctx) {
    int i = _arg<0, int>(rdram, ctx);
    gpr buf = ctx->r5;
    int len = _arg<2, int>(rdram, ctx);
    const arcade_stories::Story* s = story_at(i);
    std::string name = s ? arcade_stories::latin1(s->menu) : "";
    if (len > 0) {
        int n = 0;
        for (; n < (int)name.size() && n < len - 1; n++) MEM_B(n, buf) = (uint8_t)name[n];
        MEM_B(n, buf) = 0;
    }
    _return(ctx, s ? s->belt : -1);
}

// recomp_arcade_story_error(i, buf, len) -> the length of story i's reason it doesn't compile (in buf; 0: it does)
void story_error(uint8_t* rdram, recomp_context* ctx) {
    int i = _arg<0, int>(rdram, ctx);
    gpr buf = ctx->r5;
    int len = _arg<2, int>(rdram, ctx);
    const arcade_stories::Story* s = story_at(i);
    std::string e = s ? arcade_stories::latin1(s->error) : "";
    int n = 0;
    if (len > 0) {
        for (; n < (int)e.size() && n < len - 1; n++) MEM_B(n, buf) = (uint8_t)e[n];
        MEM_B(n, buf) = 0;
    }
    _return(ctx, n);
}

// recomp_arcade_story_pack(i, dst, maxlen) -> the size of story i's pack (copied to dst), 0 if no story i or too big
void story_pack(uint8_t* rdram, recomp_context* ctx) {
    int i = _arg<0, int>(rdram, ctx);
    gpr dst = ctx->r5;
    int maxlen = _arg<2, int>(rdram, ctx);
    const arcade_stories::Story* s = story_at(i);
    if (!s || s->pack.empty() || (int)s->pack.size() > maxlen) { _return(ctx, 0); return; }
    for (int k = 0; k < (int)s->pack.size(); k++) MEM_B(k, dst) = s->pack[k];
    _return(ctx, (int)s->pack.size());
}

} // namespace

void arcade_stories_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_story_scan", story_scan);
    recomp::overlays::register_base_export("recomp_arcade_story_menu", story_menu);
    recomp::overlays::register_base_export("recomp_arcade_story_pack", story_pack);
    recomp::overlays::register_base_export("recomp_arcade_story_error", story_error);
}
#endif
