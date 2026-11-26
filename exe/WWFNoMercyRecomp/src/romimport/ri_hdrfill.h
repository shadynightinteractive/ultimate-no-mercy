// ri_hdrfill.h (phase3 p3hdrfill, 2026-11-19): the mod's EXTRAS holes filled from its game-data headers' TEXT, without a
// compiler.
//
// The release ships the mod with its game-data tables zeroed (the holes: tools\firstrun\mkplan.py). The extras' tables are
// what the mod's generated headers (mod\src\roster_ext.h, parts_ovr.h, content_index.h, ...) define; the importer makes
// those headers' text from the player's ROMs (the C++ twins of the Python generators: the same text, byte for byte). What
// turns a header's text into the table's bytes is the compiler's work, written down once by the developer's
// tools\firstrun\hdrlayout.py as firstrun\hdrfill.bin: for each hole, the symbol's leaves (its initializer's numbers,
// strings and addresses, in text order) and where each goes (offset, size, kind). hdrlayout.py checked every hole against
// the build: the same text gives the build's bytes exactly.
//
// So the fill is: find the symbol's initializer in the header ("NAME[..] = {" at brace depth 0), split it into leaves
// (commas and braces outside parentheses; comments skipped), evaluate the leaves the layout names (integers -- decimal, hex,
// octal, u/l suffixes --, floats -- an f suffix is single --, char and string literals, casts to the basic types, the
// header's own #defines, + - * / % << >> & | ^ ~ ! and parentheses) and write them big-endian. An address leaf (a string, a
// symbol's address) is the link's: since hdrfill.bin version 2 (relclean 2026-11-19) the fill takes its word from the mod's
// base image -- firstrun\mod_binary.holes with plan.bin's literal bytes (the link's words of every extras hole among them)
// -- and hdrfill.bin says only where it is. A STRING hole (relclean) is a string one of a table's pointers points at,
// written from that table's string leaves. A COMPUTED hole (arcade_extras_in: the extras' source bits) is the caller's.
#pragma once
#include "ri_core.h"
#include <array>
#include <functional>
#include <map>

namespace ri {

// ---- hdrfill.bin (hdrlayout.py's file head says the format) --------------------------------------------------------
enum HdrKind : uint8_t { HK_INT = 1, HK_F32 = 2, HK_F64 = 3, HK_STR = 4, HK_PTR = 5 };
enum : uint32_t { HF_COMPUTED = 1, HF_STRING = 2 };

struct HdrRun {                 // leaves leaf0 .. leaf0 + count - 1 at off0 + k * stride (from the symbol's start)
    uint8_t kind = 0;
    uint32_t size = 0;          // INT 1/2/4/8, F32 4, F64 8, STR the char array's length, PTR 4
    uint32_t leaf0 = 0, count = 0, off0 = 0, stride = 0;
};

struct HdrHole {
    uint32_t start = 0, size = 0;   // in the mod's image
    int32_t sym_at = 0;             // the symbol's start relative to the hole (0, or -N: the hole starts N bytes into it)
    uint32_t sym_size = 0, flags = 0;
    std::string header, symbol;     // the header (or the mod's source file) whose text defines it, and its name
    std::vector<HdrRun> runs;
};

struct HdrFill {
    uint32_t image_size = 0;
    std::vector<HdrHole> holes;
    Bytes link;                     // the mod's base image (load_hdrfill): where a PTR op's word is read; empty: none given
};

HdrFill read_hdrfill(const Bytes& b);          // the layouts alone (Error if it isn't hdrfill.bin)
// The layouts and the words they point with: hdrfill.bin and, beside it, mod_binary.holes and plan.bin (the package's
// firstrun\ folder) -- the holed image with the plan's literal bytes laid on it (its own SHA-256 and size checked). Error
// if any of the three is missing or they aren't one build's.
HdrFill load_hdrfill(const std::filesystem::path& hdrfill_bin);

// ---- the fill -------------------------------------------------------------------------------------------------------
// text(name): a header's (or source file's) whole text, as the generator writes it (Error if it can't be had).
// computed(hole): a COMPUTED hole's bytes (hole.size of them).
using HeaderText = std::function<std::string(const std::string& name)>;
using ComputedHole = std::function<Bytes(const HdrHole& hole)>;

// Every hole's bytes, in hdrfill.bin's order (Error, naming the hole and the leaf, on what it can't read).
// (nmplus 2026-11-19) missing_zero: the fill for a first start without one of the source games -- a hole whose symbol its
// header's text doesn't define is zero, and so is a leaf past the text's last one (and a STRING hole's leaf that is no
// string: the row of a game that isn't there); every other rule as above. A PTR op needs f.link (load_hdrfill).
std::vector<Bytes> fill_holes(const HdrFill& f, const HeaderText& text, const ComputedHole& computed, bool missing_zero = false);

// (nmplus 2026-11-19) The shape of the build's text, as the layouts keep it: how many leaves of `symbol` lie at its byte
// offsets [from, to) (-1: no such symbol), and its size (0: none). A maker writes a row whose source game isn't there as that
// many zeros (hdr_zero_group: "{ 0, ... }", "{ }" for none), so every row after it keeps its leaves' places.
long hdr_leaves_in(const HdrFill& f, const std::string& symbol, int64_t from, int64_t to);
uint32_t hdr_symbol_size(const HdrFill& f, const std::string& symbol);
std::string hdr_zero_group(size_t n);

// modextra.bin as mkplan.py writes it: "UNMXTRA1", the payload's SHA-256, u32 n, then n x (u32 start, u32 len, bytes).
Bytes modextra_bin(const HdrFill& f, const std::vector<Bytes>& holes);

// One header's text read for the fill (exposed for checks): its tokens, its object-like #defines, its definitions.
struct HdrText {
    std::string s;                                  // the text with comments and preprocessor lines blanked
    std::vector<std::pair<uint32_t, uint32_t>> tok;  // tokens: (offset, length) in s
    std::map<std::string, std::string> defines;     // object-like #define NAME body (a function-like one: "\x01")
    std::map<std::string, uint32_t> defs;           // NAME -> its initializer's first token (the first definition)
    explicit HdrText(const std::string& text);
    std::string t(size_t i) const { return s.substr(tok[i].first, tok[i].second); }
    // the initializer's leaves from token i: [first token, end token) of each, in text order
    std::vector<std::pair<uint32_t, uint32_t>> leaves(uint32_t i) const;
};

}  // namespace ri
