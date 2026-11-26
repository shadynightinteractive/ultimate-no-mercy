// ri_hdrfill.cpp (phase3 p3hdrfill, 2026-11-19; relclean 2026-11-19: pointer words from the base image, STRING holes): the
// extras holes filled from the headers' text (ri_hdrfill.h). The text
// rules are tools\firstrun\hdrlayout.py's (its file head): this is their C++ twin, and hdrfill_check.py runs both on the same
// headers and compares the bytes with the build's modextra.bin.
#include "ri_hdrfill.h"
#include <cctype>
#include <charconv>
#include <memory>
#include <set>

namespace ri {

// =====================================================================================================================
// hdrfill.bin
// =====================================================================================================================
namespace {
struct In {         // little-endian reader; a read past the end is an Error
    const Bytes& b; size_t at = 0;
    void need(size_t n) { if (at + n > b.size()) fail("hdrfill.bin is cut short"); }
    uint32_t u32() { need(4); uint32_t v = b[at] | b[at + 1] << 8 | b[at + 2] << 16 | (uint32_t)b[at + 3] << 24; at += 4; return v; }
    uint8_t u8() { need(1); return b[at++]; }
    std::string str() { uint8_t n = u8(); need(n); std::string s(b.begin() + at, b.begin() + at + n); at += n; return s; }
};
}  // namespace

HdrFill read_hdrfill(const Bytes& b) {
    if (b.size() < 20 || memcmp(b.data(), "UNMHFIL1", 8)) fail("not hdrfill.bin");
    In r{ b, 8 };
    if (r.u32() != 2) fail("hdrfill.bin: a version this game doesn't know");    // (relclean 2026-11-19: 2, no PTR values)
    HdrFill f;
    f.image_size = r.u32();
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; i++) {
        HdrHole h;
        h.start = r.u32(); h.size = r.u32(); h.sym_at = (int32_t)r.u32(); h.sym_size = r.u32(); h.flags = r.u32();
        h.header = r.str(); h.symbol = r.str();
        if (uint64_t(h.start) + h.size > f.image_size) fail("hdrfill.bin: hole %s past the image", h.symbol.c_str());
        const uint32_t nr = r.u32();
        for (uint32_t k = 0; k < nr; k++) {
            HdrRun u;
            u.kind = r.u8(); r.u8(); r.u8(); r.u8();
            u.size = r.u32(); u.leaf0 = r.u32(); u.count = r.u32(); u.off0 = r.u32(); u.stride = r.u32();
            if (u.kind < HK_INT || u.kind > HK_PTR) fail("hdrfill.bin: an op of kind %u", u.kind);
            h.runs.push_back(std::move(u));
        }
        f.holes.push_back(std::move(h));
    }
    if (r.at != b.size()) fail("hdrfill.bin: bytes after its end");
    return f;
}

// (relclean 2026-11-19) The words a PTR op writes: the link's, which the mod's base image already holds -- plan.bin's literal
// bytes (op kind 2: the link's words of every base and extras hole, mkplan.py extras_link_words) laid on mod_binary.holes. The
// plan's other ops (copies from the ROM, zeros) fill only base holes, which no extras op reads, so they are skipped here.
// plan.bin's layout: exe src\main\arcade_firstrun.cpp read_plan (and tools\firstrun\mkplan.py, which writes it).
HdrFill load_hdrfill(const fs::path& hdrfill_bin) {
    HdrFill f = read_hdrfill(read_file(hdrfill_bin));
    const fs::path dir = hdrfill_bin.parent_path();
    Bytes img = read_file(dir / "mod_binary.holes");
    const Bytes plan = read_file(dir / "plan.bin");
    struct P { const Bytes& b; size_t at = 0; void need(size_t n) { if (at + n > b.size()) fail("firstrun\\plan.bin is cut short"); }
               uint32_t u32() { need(4); uint32_t v = b[at] | b[at + 1] << 8 | b[at + 2] << 16 | (uint32_t)b[at + 3] << 24; at += 4; return v; } } r{ plan };
    if (plan.size() < 8 || memcmp(plan.data(), "UNMPLAN1", 8)) fail("firstrun\\plan.bin isn't a plan");
    r.at = 8;
    if (r.u32() != 1) fail("firstrun\\plan.bin: a version this game doesn't know");
    const uint32_t image_size = r.u32(); r.u32();                                  // (the image's size, the ROM's)
    r.need(4 * 32);
    const uint8_t* holed_sha = plan.data() + r.at + 32; r.at += 4 * 32;            // (ROM, holed, base, full)
    const std::array<uint8_t, 32> img_sha = sha256(img.data(), img.size());
    if (image_size != f.image_size || img.size() != image_size || memcmp(img_sha.data(), holed_sha, 32))
        fail("firstrun\\hdrfill.bin, mod_binary.holes and plan.bin aren't one build's");
    r.at += size_t(r.u32()) * (32 + 32 + 8);                                       // (the packs)
    r.at += size_t(r.u32()) * (4 + 4 + 4 + 32);                                    // (the holes)
    r.at += size_t(r.u32()) * (4 + 4 + 16 + 32);                                   // (the ports)
    r.u32();                                                                       // (the ops' count)
    while (r.at < plan.size()) {
        r.need(1); const uint8_t kind = plan[r.at++];
        const uint32_t dst = r.u32();
        if (kind == 1) { r.need(12); r.at += 12; continue; }                       // (a copy: src, off, len)
        const uint32_t len = r.u32();
        if (kind == 3) continue;                                                   // (zeros)
        if (kind != 2) fail("firstrun\\plan.bin: an op it shouldn't have");
        r.need(len);
        if (uint64_t(dst) + len > img.size()) fail("firstrun\\plan.bin: our bytes out of place");
        memcpy(img.data() + dst, plan.data() + r.at, len); r.at += len;
    }
    f.link = std::move(img);
    return f;
}

// =====================================================================================================================
// The text: comments and preprocessor lines away, tokens, defines, the definitions at depth 0
// =====================================================================================================================
namespace {
inline bool is_id0(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
inline bool is_digit(char c) { return c >= '0' && c <= '9'; }
inline bool is_word(char c) { return is_id0(c) || is_digit(c); }
inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// (hdrlayout.py strip_text) a comment becomes one space; a string or char literal is copied whole (to its quote or the line's
// end), so a // inside one stays
std::string strip_comments(const std::string& t) {
    std::string o; o.reserve(t.size());
    size_t i = 0, n = t.size();
    while (i < n) {
        char c = t[i];
        if (c == '/' && i + 1 < n && t[i + 1] == '*') {
            size_t j = t.find("*/", i + 2); j = j == std::string::npos ? n : j + 2;
            o += ' '; i = j; continue;
        }
        if (c == '/' && i + 1 < n && t[i + 1] == '/') {
            size_t j = t.find('\n', i); j = j == std::string::npos ? n : j;
            o += ' '; i = j; continue;
        }
        if (c == '"' || c == '\'') {
            size_t j = i + 1;
            while (j < n && t[j] != c && t[j] != '\n') j += t[j] == '\\' ? 2 : 1;
            size_t e = std::min(j + 1, n);
            o.append(t, i, e - i); i = e; continue;
        }
        o += c; i++;
    }
    return o;
}

// (hdrlayout.py TOKEN) a string, a char literal, a name, a number (a C pp-number), << >>, or one character
size_t token_len(const std::string& s, size_t i) {
    const size_t n = s.size();
    char c = s[i];
    if (c == '"' || c == '\'') {      // (closed on its line: else the quote alone, as the Python's regex)
        size_t j = i + 1;
        while (j < n && s[j] != c && s[j] != '\n') {
            if (s[j] == '\\') { if (j + 1 >= n || s[j + 1] == '\n') break; j += 2; }
            else j++;
        }
        if (j < n && s[j] == c) return j + 1 - i;
        return 1;
    }
    if (is_id0(c)) { size_t j = i + 1; while (j < n && is_word(s[j])) j++; return j - i; }
    if (is_digit(c) || (c == '.' && i + 1 < n && is_digit(s[i + 1]))) {
        size_t j = c == '.' ? i + 2 : i + 1;
        while (j < n) {
            char d = s[j];
            if ((d == 'e' || d == 'E' || d == 'p' || d == 'P') && j + 1 < n && (s[j + 1] == '+' || s[j + 1] == '-')) { j += 2; continue; }
            if (is_word(d) || d == '.') { j++; continue; }
            break;
        }
        return j - i;
    }
    if ((c == '<' || c == '>') && i + 1 < n && s[i + 1] == c) return 2;
    return 1;
}
}  // namespace

HdrText::HdrText(const std::string& text) {
    s = strip_comments(text);
    // preprocessor lines: an object-like #define kept, the line blanked
    for (size_t ls = 0; ls < s.size();) {
        size_t le = s.find('\n', ls); if (le == std::string::npos) le = s.size();
        size_t k = ls; while (k < le && is_space(s[k])) k++;
        if (k < le && s[k] == '#') {
            size_t p = k + 1; while (p < le && is_space(s[p])) p++;
            if (s.compare(p, 6, "define") == 0 && p + 6 < le && is_space(s[p + 6])) {
                p += 6; while (p < le && is_space(s[p])) p++;
                size_t q = p; while (q < le && is_word(s[q])) q++;
                if (q > p && is_id0(s[p])) {
                    std::string name = s.substr(p, q - p);
                    if (q < le && s[q] == '(') defines[name] = "\x01";      // (function-like: not a value)
                    else {
                        size_t a = q, b = le;
                        while (a < b && is_space(s[a])) a++;
                        while (b > a && is_space(s[b - 1])) b--;
                        defines[name] = s.substr(a, b - a);
                    }
                }
            }
            for (size_t x = ls; x < le; x++) s[x] = ' ';
        }
        ls = le + 1;
    }
    for (size_t i = 0; i < s.size();) {
        if (is_space(s[i])) { i++; continue; }
        size_t n = token_len(s, i);
        tok.emplace_back((uint32_t)i, (uint32_t)n); i += n;
    }
    // (hdrlayout.py index_definitions) NAME [dims] [__attribute__((..))] = at depth 0
    int depth = 0;
    const size_t N = tok.size();
    auto is = [&](size_t i, const char* x) { return i < N && tok[i].second == strlen(x) && s.compare(tok[i].first, tok[i].second, x) == 0; };
    for (size_t i = 0; i < N; i++) {
        char c = s[tok[i].first];
        if (tok[i].second == 1 && (c == '{' || c == '(' || c == '[')) { depth++; continue; }
        if (tok[i].second == 1 && (c == '}' || c == ')' || c == ']')) { depth--; continue; }
        if (depth != 0 || !is_id0(c) || is(i, "__attribute__")) continue;
        size_t j = i + 1;
        while (is(j, "[")) {
            int d = 0;
            while (j < N) { d += is(j, "["); d -= is(j, "]"); j++; if (!d) break; }
        }
        while (is(j, "__attribute__")) {
            j++; int d = 0;
            while (j < N) { d += is(j, "("); d -= is(j, ")"); j++; if (!d) break; }
        }
        if (is(j, "=") && !is(j + 1, "=")) defs.emplace(t(i), (uint32_t)(j + 1));    // (the first one: hdrlayout.py checked it is the only one)
    }
}

std::vector<std::pair<uint32_t, uint32_t>> HdrText::leaves(uint32_t i) const {
    // (hdrlayout.py split_initializer, flattened: the layout already says where each leaf goes)
    std::vector<std::pair<uint32_t, uint32_t>> out;
    const uint32_t N = (uint32_t)tok.size();
    auto ch = [&](uint32_t k) -> char { return k < N && tok[k].second == 1 ? s[tok[k].first] : 0; };
    auto scalar = [&](uint32_t k, char stop1, char stop2) {   // a leaf: up to a stop at parenthesis depth 0
        uint32_t j = k; int d = 0;
        while (j < N) {
            char c = ch(j);
            if (d == 0 && c && (c == stop1 || c == stop2)) break;
            if (c == '(' || c == '[') d++;
            else if (c == ')' || c == ']') d--;
            else if (c == '{' || c == '}') fail("a brace inside a leaf");
            j++;
        }
        return j;
    };
    if (ch(i) != '{') {
        uint32_t j = scalar(i, ',', ';');
        if (j == i) fail("an empty initializer");
        out.emplace_back(i, j);
        return out;
    }
    int depth = 0; uint32_t k = i;
    do {
        if (k >= N) fail("an initializer not closed");
        char c = ch(k);
        if (c == '{') { depth++; k++; continue; }
        if (c == '}') {
            depth--; k++;
            if (depth > 0) {
                if (ch(k) == ',') k++;
                else if (ch(k) != '}') fail("after a group: %s", k < N ? t(k).c_str() : "the end");
            }
            continue;
        }
        uint32_t j = scalar(k, ',', '}');
        if (j == k) fail("an empty leaf (two commas)");
        out.emplace_back(k, j);
        k = j;
        if (ch(k) == ',') k++;
        else if (ch(k) != '}') fail("after a leaf: %s", k < N ? t(k).c_str() : "the end");
    } while (depth > 0);
    return out;
}

// =====================================================================================================================
// A leaf's value (hdrlayout.py Eval): C's integer types (int, unsigned, long long, unsigned long long: MIPS o32's long is 32
// bits) and float / double, the usual arithmetic conversions, wrap-around
// =====================================================================================================================
namespace {
enum VT { I32, U32, I64, U64, F32, F64 };
struct Val { VT t = I32; uint64_t u = 0; double f = 0; };     // an integer's bits (sign-extended to 64) or a float's value
inline bool is_f(VT t) { return t == F32 || t == F64; }
inline int width(VT t) { return t == I32 || t == U32 ? 32 : 64; }
inline bool is_signed(VT t) { return t == I32 || t == I64; }

uint64_t wrap(uint64_t v, VT t) {
    if (width(t) == 32) return is_signed(t) ? (uint64_t)(int64_t)(int32_t)(uint32_t)v : (uint64_t)(uint32_t)v;
    return v;
}
double as_double(const Val& v) {
    if (is_f(v.t)) return v.f;
    return is_signed(v.t) ? (double)(int64_t)v.u : (double)v.u;
}
float as_float(const Val& v) {        // (an integer straight to single: one rounding, as C converts)
    if (is_f(v.t)) return (float)v.f;
    return is_signed(v.t) ? (float)(int64_t)v.u : (float)v.u;
}
int64_t trunc_int(const Val& v) { return is_f(v.t) ? (int64_t)v.f : (int64_t)v.u; }    // (a float toward zero)

VT usual(VT a, VT b) {
    if (a == b) return a;
    if (is_signed(a) == is_signed(b)) return width(a) >= width(b) ? a : b;
    VT u = is_signed(a) ? b : a, s = is_signed(a) ? a : b;
    return width(u) >= width(s) ? u : s;
}

struct CastT { bool fl; int size; bool sgn; };
bool cast_type(const std::vector<std::string>& w, bool ptr, CastT& ct) {
    if (ptr) { ct = { false, 4, false }; return true; }          // (a pointer: an address, 32 bits)
    std::string k; for (auto& x : w) k += (k.empty() ? "" : " ") + x;
    static const std::map<std::string, CastT> T = {
        { "u8", { false, 1, false } }, { "s8", { false, 1, true } }, { "u16", { false, 2, false } }, { "s16", { false, 2, true } },
        { "u32", { false, 4, false } }, { "s32", { false, 4, true } }, { "u64", { false, 8, false } }, { "s64", { false, 8, true } },
        { "f32", { true, 4, true } }, { "f64", { true, 8, true } }, { "float", { true, 4, true } }, { "double", { true, 8, true } },
        { "char", { false, 1, true } }, { "signed char", { false, 1, true } }, { "unsigned char", { false, 1, false } },
        { "short", { false, 2, true } }, { "unsigned short", { false, 2, false } }, { "int", { false, 4, true } },
        { "signed", { false, 4, true } }, { "unsigned", { false, 4, false } }, { "unsigned int", { false, 4, false } },
        { "long", { false, 4, true } }, { "unsigned long", { false, 4, false } }, { "long long", { false, 8, true } },
        { "unsigned long long", { false, 8, false } } };
    auto it = T.find(k);
    if (it == T.end()) return false;
    ct = it->second; return true;
}

Val convert(const Val& v, const CastT& c) {
    Val r;
    if (c.fl) {
        if (c.size == 8) { r.t = F64; r.f = as_double(v); }
        else { r.t = F32; r.f = as_float(v); }
        return r;
    }
    uint64_t x = (uint64_t)trunc_int(v);
    const int bits = 8 * c.size;
    if (bits < 64) {
        x &= (uint64_t(1) << bits) - 1;
        if (c.sgn && (x >> (bits - 1)) & 1) x |= ~((uint64_t(1) << bits) - 1);
    }
    r.t = c.size == 8 ? (c.sgn ? I64 : U64) : (c.sgn || c.size < 4 ? I32 : U32);
    r.u = wrap(x, r.t);
    return r;
}

// (C's escapes; the text is ASCII / Latin-1)
std::string unescape(const std::string& b) {
    std::string o;
    for (size_t i = 0; i < b.size();) {
        char c = b[i];
        if (c != '\\') { o += c; i++; continue; }
        if (i + 1 >= b.size()) fail("an escape at a literal's end");
        char e = b[i + 1];
        if (e == 'x') {
            size_t j = i + 2; unsigned v = 0;
            while (j < b.size() && isxdigit((unsigned char)b[j])) { v = v * 16 + (unsigned)(isdigit((unsigned char)b[j]) ? b[j] - '0' : (tolower(b[j]) - 'a' + 10)); j++; }
            o += (char)(v & 0xFF); i = j;
        } else if (e >= '0' && e <= '7') {
            size_t j = i + 1; unsigned v = 0;
            while (j < b.size() && j < i + 4 && b[j] >= '0' && b[j] <= '7') { v = v * 8 + (unsigned)(b[j] - '0'); j++; }
            o += (char)(v & 0xFF); i = j;
        } else {
            static const char* from = "ntr0\\'\"abfv?"; static const char to[] = { 10, 9, 13, 0, 92, 39, 34, 7, 8, 12, 11, 63 };
            const char* p = strchr(from, e);
            if (!p || !e) fail("an escape: \\%c", e);
            o += to[p - from]; i += 2;
        }
    }
    return o;
}

Val int_literal(const std::string& t) {
    size_t i = 0; int base = 10;
    if (t.size() > 1 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) { base = 16; i = 2; }
    else if (t[0] == '0') base = 8;
    uint64_t v = 0; size_t d0 = i;
    for (; i < t.size(); i++) {
        char c = t[i]; int d;
        if (is_digit(c)) d = c - '0';
        else if (base == 16 && isxdigit((unsigned char)c)) d = tolower(c) - 'a' + 10;
        else break;
        if (d >= base) fail("a number: %s", t.c_str());
        if (v > (UINT64_MAX - d) / base) fail("a number too big: %s", t.c_str());
        v = v * base + d;
    }
    if (i == d0 && base == 16) fail("a number: %s", t.c_str());
    std::string suf;
    for (; i < t.size(); i++) suf += (char)tolower(t[i]);
    if (suf != "" && suf != "u" && suf != "l" && suf != "ul" && suf != "lu" && suf != "ll" && suf != "ull" && suf != "llu")
        fail("a number suffix: %s", t.c_str());
    const bool dec = base == 10, u = suf.find('u') != std::string::npos, ll = suf.find("ll") != std::string::npos;
    std::vector<VT> order;                  // (C99 6.4.4.1, with long = int)
    if (!u && !ll) { order.push_back(I32); if (!dec) order.push_back(U32); }
    if (u && !ll) order.push_back(U32);
    if (!u) { order.push_back(I64); if (!dec) order.push_back(U64); }
    order.push_back(U64);
    for (VT ty : order) {
        uint64_t lim = ty == I32 ? 0x80000000ull : ty == U32 ? 0x100000000ull : ty == I64 ? 0x8000000000000000ull : 0;
        if (lim == 0 || v < lim) { Val r; r.t = ty; r.u = v; return r; }
    }
    fail("a number too big: %s", t.c_str());
}

struct Eval {
    const HdrText& H;
    std::vector<std::string> t; size_t i = 0; int depth = 0;
    Eval(const HdrText& h) : H(h) {}

    Val value(std::vector<std::string> toks, int d = 0) {
        std::swap(t, toks); size_t si = i; i = 0; int sd = depth; depth = d;
        Val v = expr(0);
        if (i != t.size()) fail("a leaf: %s ...", t.empty() ? "" : t[0].c_str());
        std::swap(t, toks); i = si; depth = sd;
        return v;
    }
    const std::string* peek() const { return i < t.size() ? &t[i] : nullptr; }
    static int prec(const std::string& op) {
        if (op == "|") return 1; if (op == "^") return 2; if (op == "&") return 3; if (op == "<<" || op == ">>") return 5;
        if (op == "+" || op == "-") return 6; if (op == "*" || op == "/" || op == "%") return 7; return 0;
    }
    Val expr(int p) {
        Val a = unary();
        for (;;) {
            const std::string* op = peek();
            int q = op ? prec(*op) : 0;
            if (!q || q <= p) return a;
            std::string o = *op; i++;
            Val b = expr(q);
            a = binary(o, a, b);
        }
    }
    bool try_cast(CastT& ct) {        // at '(': a type name and ')'
        static const char* W[] = { "const", "volatile", "unsigned", "signed", "char", "short", "int", "long", "float", "double",
                                   "u8", "s8", "u16", "s16", "u32", "s32", "u64", "s64", "f32", "f64", "void", nullptr };
        size_t j = i + 1; std::vector<std::string> words;
        for (; j < t.size(); j++) {
            bool k = false;
            for (int w = 0; W[w]; w++) if (t[j] == W[w]) k = true;
            if (!k) break;
            if (t[j] != "const" && t[j] != "volatile") words.push_back(t[j]);
        }
        bool ptr = false;
        while (j < t.size() && t[j] == "*") { ptr = true; j++; }
        if (words.empty() || j >= t.size() || t[j] != ")") return false;
        if (!cast_type(words, ptr, ct)) fail("a cast: (%s)", words[0].c_str());
        i = j + 1; return true;
    }
    Val unary() {
        const std::string* tp = peek();
        if (!tp) fail("a leaf cut short");
        const std::string tk = *tp;
        if (tk == "(") {
            CastT ct;
            if (try_cast(ct)) return convert(unary(), ct);
            i++; Val v = expr(0);
            if (!peek() || *peek() != ")") fail("a ( not closed");
            i++; return v;
        }
        if (tk == "&") fail("an address in a leaf that is not one");
        if (tk == "-" || tk == "+" || tk == "~" || tk == "!") {
            i++; Val v = unary();
            if (tk == "+") return v;
            if (tk == "!") { Val r; r.t = I32; r.u = is_f(v.t) ? v.f == 0 : v.u == 0; return r; }
            if (is_f(v.t)) { if (tk == "~") fail("~ on a float"); v.f = -v.f; return v; }
            v.u = wrap(tk == "-" ? 0 - v.u : ~v.u, v.t); return v;
        }
        i++;
        if (tk[0] == '\'') {
            std::string b = unescape(tk.substr(1, tk.size() - 2));
            if (b.size() != 1) fail("a char literal: %s", tk.c_str());
            Val r; r.t = I32; r.u = (uint64_t)(int64_t)(int8_t)(uint8_t)b[0]; return r;     // (char is signed on MIPS)
        }
        if (is_digit(tk[0]) || tk[0] == '.') {
            // an integer: 0x... (a hex float is not read), or digits and only u / l suffix letters
            if (tk.size() > 1 && tk[0] == '0' && (tk[1] == 'x' || tk[1] == 'X')) {
                if (tk.find_first_of("pP") != std::string::npos) fail("a hex float: %s", tk.c_str());
                return int_literal(tk);
            }
            size_t nd = 0; while (nd < tk.size() && is_digit(tk[nd])) nd++;
            bool intsuf = nd > 0;
            for (size_t q = nd; q < tk.size(); q++) if (!strchr("uUlL", tk[q])) intsuf = false;
            if (intsuf) return int_literal(tk);
            // a float: digits [. digits] [e[+-]digits] [f|F]
            std::string body = tk; char suf = 0;
            if (!body.empty() && strchr("fFlL", body.back())) { suf = body.back(); body.pop_back(); }
            size_t k = 0; bool digits = false;
            while (k < body.size() && is_digit(body[k])) { k++; digits = true; }
            if (k < body.size() && body[k] == '.') { k++; while (k < body.size() && is_digit(body[k])) { k++; digits = true; } }
            if (k < body.size() && (body[k] == 'e' || body[k] == 'E')) {
                k++; if (k < body.size() && (body[k] == '+' || body[k] == '-')) k++;
                size_t e0 = k; while (k < body.size() && is_digit(body[k])) k++;
                if (k == e0) fail("a number: %s", tk.c_str());
            }
            if (!digits || k != body.size()) fail("a number: %s", tk.c_str());
            if (suf == 'l' || suf == 'L') fail("a long double: %s", tk.c_str());
            Val r;
            if (suf) {      // (single: straight from the decimal, one rounding, as clang reads it)
                float f = 0; auto res = std::from_chars(body.data(), body.data() + body.size(), f);
                if (res.ec != std::errc() || res.ptr != body.data() + body.size()) fail("a number: %s", tk.c_str());
                r.t = F32; r.f = f;
            } else {
                double d = 0; auto res = std::from_chars(body.data(), body.data() + body.size(), d);
                if (res.ec != std::errc() || res.ptr != body.data() + body.size()) fail("a number: %s", tk.c_str());
                r.t = F64; r.f = d;
            }
            return r;
        }
        if (is_id0(tk[0])) {
            auto it = H.defines.find(tk);
            if (it == H.defines.end()) fail("a name that is not one of the header's #defines: %s", tk.c_str());
            if (it->second == "\x01") fail("a function-like macro in a leaf: %s", tk.c_str());
            if (depth > 32) fail("a define that refers to itself: %s", tk.c_str());
            HdrText sub(it->second);                     // (its body's tokens)
            std::vector<std::string> bt; for (size_t k = 0; k < sub.tok.size(); k++) bt.push_back(sub.t(k));
            Eval e(H); return e.value(bt, depth + 1);
        }
        fail("in a leaf: %s", tk.c_str());
    }
    Val binary(const std::string& op, const Val& a, const Val& b) {
        if (is_f(a.t) || is_f(b.t)) {
            if (op != "+" && op != "-" && op != "*" && op != "/") fail("%s on a float", op.c_str());
            const VT ty = (a.t == F64 || b.t == F64) ? F64 : F32;     // (an int with a float: the float's type)
            double x = as_double(a), y = as_double(b);
            double r = op == "+" ? x + y : op == "-" ? x - y : op == "*" ? x * y : x / y;
            Val v; v.t = ty; v.f = ty == F32 ? (double)(float)r : r; return v;
        }
        if (op == "<<" || op == ">>") {
            const VT ty = a.t; const int64_t y = (int64_t)b.u;     // (the left operand's type; b's value, sign-extended)
            if (y < 0 || y >= width(ty)) fail("a shift by %lld", (long long)y);
            Val v; v.t = ty;
            if (op == "<<") v.u = wrap(a.u << y, ty);
            else v.u = is_signed(ty) ? wrap((uint64_t)((int64_t)a.u >> y), ty) : wrap((width(ty) == 32 ? (a.u & 0xFFFFFFFFull) : a.u) >> y, ty);
            return v;
        }
        VT ty = usual(a.t, b.t);
        uint64_t x = wrap(a.u, ty), y = wrap(b.u, ty), r;
        if (op == "/" || op == "%") {
            if (y == 0) fail("a division by zero");
            if (is_signed(ty)) { int64_t X = (int64_t)x, Y = (int64_t)y; r = (uint64_t)(op == "/" ? X / Y : X % Y); }
            else { uint64_t X = width(ty) == 32 ? x & 0xFFFFFFFFull : x, Y = width(ty) == 32 ? y & 0xFFFFFFFFull : y; r = op == "/" ? X / Y : X % Y; }
        } else if (op == "+") r = x + y; else if (op == "-") r = x - y; else if (op == "*") r = x * y;
        else if (op == "&") r = x & y; else if (op == "|") r = x | y; else r = x ^ y;
        Val v; v.t = ty; v.u = wrap(r, ty); return v;
    }
};

// one scalar written big-endian at p (size bytes), as C converts the value to the field's type
void put_scalar(uint8_t* p, uint8_t kind, uint32_t size, const Val& v) {
    if (kind == HK_INT) {
        uint64_t x = is_f(v.t) ? (uint64_t)trunc_int(v) : v.u;
        for (uint32_t k = 0; k < size; k++) p[k] = (uint8_t)(x >> (8 * (size - 1 - k)));
    } else if (kind == HK_F32) {
        float f = v.t == F32 ? (float)v.f : as_float(v);
        uint32_t w; memcpy(&w, &f, 4); put_be32(p, w);
    } else {
        double d = as_double(v); uint64_t w; memcpy(&w, &d, 8);
        put_be32(p, (uint32_t)(w >> 32)); put_be32(p + 4, (uint32_t)w);
    }
}
}  // namespace

// =====================================================================================================================
// The fill
// =====================================================================================================================
std::vector<Bytes> fill_holes(const HdrFill& f, const HeaderText& text, const ComputedHole& computed, bool missing_zero) {
    std::map<std::string, std::unique_ptr<HdrText>> texts;     // (each header read once: parts_ovr.h is 1.5 MB)
    std::map<std::pair<std::string, std::string>, std::vector<std::pair<uint32_t, uint32_t>>> leaves_of;   // (a table's leaves, once)
    std::vector<Bytes> out;
    for (const HdrHole& h : f.holes) {
        if (h.flags & HF_COMPUTED) {
            Bytes b = computed(h);
            if (b.size() != h.size) fail("%s: the computed hole's bytes are %zu, not %u", h.symbol.c_str(), b.size(), h.size);
            out.push_back(std::move(b)); continue;
        }
        auto& T = texts[h.header];
        if (!T) T = std::make_unique<HdrText>(text(h.header));
        auto d = T->defs.find(h.symbol);
        if (d == T->defs.end()) {
            if (missing_zero) { out.push_back(Bytes(h.size, 0)); continue; }   // (its source game isn't there: zero)
            fail("%s: %s isn't defined in its text", h.header.c_str(), h.symbol.c_str());
        }
        auto lk = leaves_of.find({ h.header, h.symbol });
        if (lk == leaves_of.end()) {
            try { lk = leaves_of.emplace(std::make_pair(h.header, h.symbol), T->leaves(d->second)).first; }
            catch (const Error& e) { fail("%s %s: %s", h.header.c_str(), h.symbol.c_str(), e.what()); }
        }
        const std::vector<std::pair<uint32_t, uint32_t>>& leaves = lk->second;
        Bytes b(h.size, 0);
        // (an op's byte at the symbol's offset o is the hole's byte sym_at + o: kept only inside the hole)
        auto put = [&](int64_t at, const uint8_t* p, size_t n) {
            for (size_t k = 0; k < n; k++) if (at + (int64_t)k >= 0 && at + (int64_t)k < (int64_t)h.size) b[at + k] = p[k];
        };
        Eval ev(*T);
        for (const HdrRun& r : h.runs) {
            for (uint32_t k = 0; k < r.count; k++) {
                const int64_t at = (int64_t)h.sym_at + r.off0 + (int64_t)k * r.stride;
                const uint32_t li = r.leaf0 + k;
                if (r.kind == HK_PTR) {
                    // (relclean 2026-11-19) the link's word, from the mod's base image at this place (a pointer word lies whole in
                    //  its hole: hdrlayout.py checked; a zero there is no link word -- the image isn't this build's)
                    const int64_t img_at = (int64_t)h.start + at;
                    if (f.link.size() != f.image_size) fail("%s %s: a pointer, and the mod's base image wasn't given (load_hdrfill)", h.header.c_str(), h.symbol.c_str());
                    if (at < 0 || at + 4 > (int64_t)h.size || img_at % 4) fail("%s %s: a pointer word across its hole's edge", h.header.c_str(), h.symbol.c_str());
                    const uint8_t* w = f.link.data() + img_at;
                    if (!(w[0] | w[1] | w[2] | w[3])) fail("%s %s: the base image has no link word at 0x%llX", h.header.c_str(), h.symbol.c_str(), (unsigned long long)img_at);
                    put(at, w, 4); continue;
                }
                if (li >= leaves.size()) {
                    if (missing_zero) continue;                       // (rows past the text's: zero)
                    fail("%s %s: leaf %u of %zu", h.header.c_str(), h.symbol.c_str(), li, leaves.size());
                }
                std::vector<std::string> lt;
                for (uint32_t q = leaves[li].first; q < leaves[li].second; q++) lt.push_back(T->t(q));
                try {
                    if (r.kind == HK_STR) {           // (a string filling a char array: adjacent literals joined, zeros after)
                        // (a STRING hole whose table row is a game's that isn't there: its leaf is 0, not a string -- zero)
                        if ((h.flags & HF_STRING) && missing_zero && (lt.empty() || lt[0].size() < 2 || lt[0][0] != '"')) continue;
                        std::string s;
                        for (auto& x : lt) { if (x.size() < 2 || x[0] != '"') fail("not a string: %s", x.c_str()); s += unescape(x.substr(1, x.size() - 2)); }
                        if (s.size() > r.size) fail("a string longer than its array (%zu > %u)", s.size(), r.size);
                        // (a STRING hole is what a pointer points at: its zero must fit too -- the build's string and its zero)
                        if ((h.flags & HF_STRING) && s.size() >= r.size) fail("a string longer than the build's (%zu, room for %u)", s.size(), r.size - 1);
                        put(at, (const uint8_t*)s.data(), s.size());
                        continue;
                    }
                    uint8_t w[8] = {};
                    put_scalar(w, r.kind, r.size, ev.value(lt));
                    put(at, w, r.size);
                } catch (const Error& e) {
                    fail("%s %s, leaf %u (%s%s): %s", h.header.c_str(), h.symbol.c_str(), li, lt.empty() ? "" : lt[0].c_str(), lt.size() > 1 ? " ..." : "", e.what());
                }
            }
        }
        out.push_back(std::move(b));
    }
    return out;
}

long hdr_leaves_in(const HdrFill& f, const std::string& symbol, int64_t from, int64_t to) {
    std::set<uint32_t> leaves;
    bool any = false;
    for (const HdrHole& h : f.holes) {
        if (h.symbol != symbol || (h.flags & HF_STRING)) continue;      // (a STRING hole: its leaves are the table's own)
        any = true;
        for (const HdrRun& r : h.runs)
            for (uint32_t k = 0; k < r.count; k++) {
                const int64_t at = (int64_t)r.off0 + (int64_t)k * r.stride;
                if (at >= from && at < to) leaves.insert(r.leaf0 + k);
            }
    }
    return any ? (long)leaves.size() : -1;
}
uint32_t hdr_symbol_size(const HdrFill& f, const std::string& symbol) {
    for (const HdrHole& h : f.holes) if (h.symbol == symbol && !(h.flags & HF_STRING)) return h.sym_size;
    return 0;
}
std::string hdr_zero_group(size_t n) {
    if (!n) return "{ }";
    std::string s = "{ 0";
    for (size_t i = 1; i < n; i++) s += ", 0";
    return s + " }";
}

Bytes modextra_bin(const HdrFill& f, const std::vector<Bytes>& holes) {
    Bytes payload;
    auto le = [&](uint32_t v) { for (int k = 0; k < 4; k++) payload.push_back((uint8_t)(v >> (8 * k))); };
    for (size_t i = 0; i < f.holes.size(); i++) {
        le(f.holes[i].start); le((uint32_t)holes[i].size());
        payload.insert(payload.end(), holes[i].begin(), holes[i].end());
    }
    Bytes x = { 'U', 'N', 'M', 'X', 'T', 'R', 'A', '1' };
    auto h = sha256(payload.data(), payload.size());
    x.insert(x.end(), h.begin(), h.end());
    const uint32_t n = (uint32_t)f.holes.size();
    for (int k = 0; k < 4; k++) x.push_back((uint8_t)(n >> (8 * k)));
    x.insert(x.end(), payload.begin(), payload.end());
    return x;
}

}  // namespace ri
