// ri_place.h (phase3, 2026-11-19): imported wrestler folders put into the game's wrestlers\ folder by the user's two rules.
//
// The user (2026-11-18): what is in the game folder ALWAYS wins. An import never writes into or over an existing wrestler
// folder or any file in it (his theme, Titantron, photo ...). It is built in a temporary folder first and moved in whole,
// only under a name no folder has; when the name is taken it gets "<name>_<source>" (then "~2", "~3" ...), and when his ID is
// someone else's he gets a fresh one. Every rename is written to content\import_report.txt for the player.
// And: a folder the player deleted is not made again (the record of what was imported says it was; only
// content\reimport.txt makes it again).
#pragma once
#include "ri_core.h"
#include <set>

namespace ri {

// Every ID the game's wrestler folders hold now ("id" in each wrestler.json, numeric "0x...." or a string), and the
// folder names in use -- what an import must not take.
struct Taken {
    std::set<std::string> ids;           // as written, lower case
    std::set<uint32_t> numbers;          // the numeric ones
    void scan(const fs::path& wrestlers_root);
    std::string fresh_number(uint32_t from = 0x0340);   // the lowest free "0x...." from `from`, marked taken
    // (startup 2026-11-19) A fresh number for an import whose own one is taken: from the FRESH ranges (ri_place.cpp), which
    // no importer's own numbers are in -- so it never lands on a wrestler a later import (this start's or a ROM added later)
    // brings with his frozen number. Empty when every fresh number is used.
    std::string fresh_import_number();
    bool holds(const std::string& id) const;              // is this ID (a string or "0x....") someone's already?
    void reserve(const std::string& id);                  // marks it taken
};

struct PlaceResult { bool placed = false; fs::path where; std::string note; };

// Moves the built folder `built` (a temporary folder holding wrestler.json ...) into wrestlers_root\collection\<name>.
// source_tag: "revenge", "wm2000", ... for the renamed folder's suffix. report: lines appended for the player.
// (startup 2026-11-19) One importer's whole batch -- the folders its maker built under tmp, (collection, name) each -- put in
// with the user's rule of 2026-11-19: "only the clashing wrestler changes, and the rest of the batch keeps its numbers".
// Every ID of the batch is reserved first (before any folder is placed), so a fresh number can't be the batch's next man's
// own; only a wrestler whose ID an existing folder holds (or an earlier wrestler of the same batch) gets a fresh one, given
// after the whole batch. Then each is placed as place_folder places it. One result per made folder, in order.
std::vector<PlaceResult> place_batch(const fs::path& tmp, const std::vector<std::pair<std::string, std::string>>& made,
                                     const fs::path& wrestlers_root, const std::string& source_tag, Taken& taken,
                                     std::vector<std::string>& report);

PlaceResult place_folder(const fs::path& built, const fs::path& wrestlers_root, const std::string& collection,
                         const std::string& name, const std::string& source_tag, Taken& taken, std::vector<std::string>& report);

}  // namespace ri
