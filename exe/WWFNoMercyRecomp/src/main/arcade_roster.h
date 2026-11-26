// arcade_roster.h (startup 2026-11-19): the story roster (names, string IDs and game numbers) as the story compiler and the
// select's pages read it, and the one rule every wrestler reference follows when it carries both a game number and a
// string ID.
//
// The user, 2026-11-19: "Wrestler references also check the name, as a fallback to the ID" -- and "name" is the wrestler's
// STRING ID (his folder's name, or wrestler.json's "id" when it is a string: aki_man), NEVER the name on screen, which can
// change freely without changing anything. So a reference may carry
//   {"id": "aki_man", "game_number": "0x0379"}
// and resolves by the number when the wrestler with that number IS aki_man (the fast path, as before), else by the string ID
// (an import renumbered by a clash, a folder that got another number as it loaded ...). A string ID nobody has: by the
// number, as before. Every older form (a number, "0x0379", a string ID, a name) works as it did.
//
// (Why a header: arcade_pages.cpp used to declare Roster as a plain std::unordered_map while arcade_stories.cpp defined it
//  with three more maps -- one function, two return types: load_roster built the bigger one into the caller's smaller slot.
//  One definition now, here.)
#pragma once
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace arcade_stories {

// the roster: names -> Superstar IDs (_roster.json), its groups ("@GROUP:<NAME>": [IDs]) and the voices (_voices.json
// beside it: {ID: {field: [lines]}}, 0 the default; storyc.py voices())
struct Roster : std::unordered_map<std::string, int64_t> {
    std::map<std::string, std::vector<int64_t>> groups;
    std::map<int64_t, std::map<int, std::vector<std::string>>> voices;
    // (2026-11-13, removal) The stock Superstars removed from the game (his folder deleted: arcade_wrestlers.cpp), by game
    // number -> string ID. A list of men for a role passes him over; a story that names him by himself compiles as it is and
    // the mod casts his stand-in in his place when it runs (mod story.c -- the user's R8, 2026-11-13: a stand-in, never a
    // story greyed out). Empty when nobody is removed, and then every pack is the same bytes as without this.
    std::map<int64_t, std::string> removed;
};

// A string ID as the roster keys it: lower case, a space or '-' as '_' (as the select's pages and the stories have always
// matched one written "Rock On Austin").
inline std::string sid_key(const std::string& s) {
    std::string l;
    for (char c : s) l += (c == ' ' || c == '-') ? '_' : (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    return l;
}

// The game number of the wrestler with this string ID (as written, then as sid_key makes it), 0 if none.
inline int64_t number_of_sid(const Roster& R, const std::string& sid) {
    auto i = R.find("@ID:" + sid);
    if (i == R.end()) i = R.find("@ID:" + sid_key(sid));
    return i != R.end() && i->second > 0 && i->second < 0x1000 ? i->second : 0;
}

// (no2k15, 2026-11-23) A built-in Superstar taken out for good (mod gen/retired_ids.json: the two WWE 2K15 test slots,
// stone_cold_2k15 0x011A and the_rock_2k15 0x011B): _roster.json maps his string ID, his name and his old number ("0x011A")
// to his stand-in's game number, "@RETIRED:<key>" (gen/storyc.py --roster). A story that still names him plays the stand-in
// (the game's rule for a removed wrestler). By text (a string ID or name) only when nobody on the roster has it; by number
// only when no wrestler has that number now -- a folder that has it is his own wrestler. 0: not one. (gen/storyc.py
// retired_stand_in is the same.)
inline int64_t retired_stand_in(const Roster& R, const std::string& s) {
    for (const std::string& k : { s, sid_key(s) }) {
        auto i = R.find("@RETIRED:" + k);
        if (i != R.end()) return i->second;
    }
    std::string u;
    for (char c : s) u += (c >= 'a' && c <= 'z') ? char(c - 32) : c;
    const size_t a = u.find_first_not_of(" \t\n\r\f\v"), b = u.find_last_not_of(" \t\n\r\f\v");
    auto i = R.find("@RETIRED:" + (a == std::string::npos ? std::string() : u.substr(a, b - a + 1)));
    return i != R.end() ? i->second : 0;
}
inline int64_t retired_stand_in(const Roster& R, int64_t number) {
    if (number <= 0 || number >= 0x1000) return 0;
    char h[16];
    snprintf(h, sizeof h, "0x%04X", (unsigned)number);
    auto i = R.find("@RETIRED:" + std::string(h));
    if (i == R.end()) return 0;
    for (const auto& kv : R) if (kv.first.rfind("@ID:", 0) == 0 && kv.second == number) return 0;
    return i->second;
}

// THE RULE (startup 2026-11-19). number: what the reference's number resolves to (0: none); sid: its string ID ("" when it
// has none). Returns the wrestler's game number, 0 if neither finds one; note says what the fallback did (empty when the
// number held). A string ID belongs to one wrestler, so "the number's wrestler is <sid>" is "<sid>'s number is the number":
// the number holds when it is the string ID's own; when the string ID is someone else's, he is the one; only a string ID
// nobody has leaves the number to decide.
inline int64_t resolve_ref(const Roster& R, int64_t number, const std::string& sid, std::string& note) {
    note.clear();
    if (sid.empty()) return number;                                   // (no string ID: as before)
    const int64_t by_sid = number_of_sid(R, sid);
    char h[16];
    snprintf(h, sizeof h, "0x%04X", (unsigned)number);
    if (!by_sid) {
        note = "no wrestler has the string ID " + sid + (number > 0 ? ": by the game number " + std::string(h) : std::string());
        return number;
    }
    if (by_sid != number) note = (number > 0 ? "game number " + std::string(h) + " isn't " + sid : std::string("no game number")) +
                                 ": found by his string ID";
    return by_sid;
}

}  // namespace arcade_stories
