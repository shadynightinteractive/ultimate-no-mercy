// ri_redux.h (phase3, 2026-11-19): Revenge Redux's entrance themes put into the wrestler folders when the player adds the
// Redux patch -- the C++ twin of tools\romimport\reduxthemes.py apply(), under the user's rules (2026-11-18):
//  * once per new input: content\firstrun.json "redux_themes" = { input: the Redux ROM's SHA-1, state, wrote: {folder:
//    SHA-256 of the theme written} }; the same input again: nothing done, nothing written; a stop half way: "failed", again;
//  * existing content always wins: a folder's theme is replaced only when it is still the importer's (Revenge's own 0x214)
//    or the one this step wrote, as the record says; the player's own theme, or a theme he took out, is never touched;
//  * no Redux patch: nothing written.
#pragma once
#include "ri_roms.h"
#include "json/json.hpp"
#include <functional>
#include <optional>

namespace ri {

struct ReduxResult { int done = 0, same = 0, kept = 0; bool ran = false; std::vector<std::string> log; };
// rec: the "redux_themes" object of content\firstrun.json (read and updated here); save: called to write the record out
// (at the start, as "failed", and at the end, as "done").
ReduxResult redux_themes_apply(const fs::path& wrestlers, Roms& R, nlohmann::ordered_json& rec,
                               const std::function<void()>& save);

}  // namespace ri
