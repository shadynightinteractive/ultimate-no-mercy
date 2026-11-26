// ri_json.h (phase3, 2026-11-19): JSON written exactly as Python's json.dumps writes it.
//
// The importer's files (wrestler.json, moves.json, model.json, the arenas' scene files ...) are the same bytes as the Python
// tools write, so the text must be too: Python's number forms (an integer as it is; a float as repr() gives it -- the
// shortest digits that read back the same, "1.0", "0.0001", "1e-05", "1e+16"), its escapes (ensure_ascii: \uXXXX for
// anything past ASCII), its spacing (", " and ": ", or with an indent a new line per item), and the keys in the order they
// were put in (nlohmann::ordered_json keeps it).
#pragma once
#include <string>
#include "json/json.hpp"

namespace ri {

using ojson = nlohmann::ordered_json;

struct PyDump {
    int indent = -1;                  // -1: None (one line); 0 or more: json.dumps(indent=n)
    bool ensure_ascii = true;
    const char* item_sep = nullptr;   // nullptr: Python's default (", " without an indent, "," with one)
    const char* key_sep = ": ";
    bool sort_keys = false;
};

std::string py_dumps(const ojson& v, const PyDump& how = {});
std::string py_float_repr(double x);     // repr(x) for a Python float

}  // namespace ri
