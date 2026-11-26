// ri_json.cpp (phase3, 2026-11-19): Python's json.dumps, byte for byte (ri_json.h).
#include "ri_json.h"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace ri {

// repr() of a Python float: the shortest digits that read back as the same double (the same digits C++'s shortest
// to_chars gives), written as Python's float_repr_style 'short' writes them: with the decimal point at decpt (the value
// = 0.d1d2... x 10^decpt), fixed notation when -4 < decpt <= 16 (always a digit after the point), else d.ddde+XX.
std::string py_float_repr(double x) {
    if (std::isnan(x)) return "NaN";                      // (json.dumps' words for them)
    if (std::isinf(x)) return x > 0 ? "Infinity" : "-Infinity";
    if (x == 0) return std::signbit(x) ? "-0.0" : "0.0";
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);
    std::string s(buf, r.ptr);                            // e.g. "-1.2345e-05"
    bool neg = s[0] == '-';
    if (neg) s.erase(0, 1);
    size_t e = s.find('e');
    std::string mant = s.substr(0, e);
    int exp10 = atoi(s.c_str() + e + 1);
    std::string digits;
    for (char c : mant) if (c != '.') digits += c;
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    int decpt = exp10 + 1;
    std::string out;
    if (decpt > -4 && decpt <= 16) {
        if (decpt <= 0) out = "0." + std::string(-decpt, '0') + digits;
        else if ((size_t)decpt >= digits.size()) out = digits + std::string(decpt - digits.size(), '0') + ".0";
        else out = digits.substr(0, decpt) + "." + digits.substr(decpt);
    } else {
        out = digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        int ex = decpt - 1;
        char eb[16]; snprintf(eb, sizeof eb, "e%c%02d", ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
        out += eb;
    }
    return neg ? "-" + out : out;
}

static void put_string(std::string& o, const std::string& s, bool ascii) {
    o += '"';
    for (size_t i = 0; i < s.size();) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; size_t n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6 && i + 1 < s.size()) { cp = (c & 0x1F) << 6 | (s[i + 1] & 0x3F); n = 2; }
        else if ((c >> 4) == 14 && i + 2 < s.size()) { cp = (c & 0x0F) << 12 | (s[i + 1] & 0x3F) << 6 | (s[i + 2] & 0x3F); n = 3; }
        else if (i + 3 < s.size()) { cp = (c & 0x07) << 18 | (s[i + 1] & 0x3F) << 12 | (s[i + 2] & 0x3F) << 6 | (s[i + 3] & 0x3F); n = 4; }
        else { cp = c; n = 1; }
        char b[16];
        switch (cp) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        case '\b': o += "\\b"; break;
        case '\f': o += "\\f"; break;
        default:
            if (cp < 0x20 || (ascii && cp >= 0x7F)) {
                if (cp < 0x7F || cp < 0x10000) { snprintf(b, sizeof b, "\\u%04x", cp); o += b; }
                else {
                    uint32_t v = cp - 0x10000;
                    snprintf(b, sizeof b, "\\u%04x\\u%04x", 0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF)); o += b;
                }
            } else o.append(s, i, n);
        }
        i += n;
    }
    o += '"';
}

static void dump(std::string& o, const ojson& v, const PyDump& h, int level) {
    const char* isep = h.item_sep ? h.item_sep : (h.indent >= 0 ? "," : ", ");
    auto newline = [&](int lv) { o += '\n'; o.append((size_t)(h.indent * lv), ' '); };
    switch (v.type()) {
    case ojson::value_t::null: o += "null"; break;
    case ojson::value_t::boolean: o += v.get<bool>() ? "true" : "false"; break;
    case ojson::value_t::number_integer: o += std::to_string(v.get<int64_t>()); break;
    case ojson::value_t::number_unsigned: o += std::to_string(v.get<uint64_t>()); break;
    case ojson::value_t::number_float: o += py_float_repr(v.get<double>()); break;
    case ojson::value_t::string: put_string(o, v.get_ref<const std::string&>(), h.ensure_ascii); break;
    case ojson::value_t::array: {
        if (v.empty()) { o += "[]"; break; }
        o += '[';
        bool first = true;
        for (auto& x : v) {
            if (!first) o += isep;
            first = false;
            if (h.indent >= 0) newline(level + 1);
            dump(o, x, h, level + 1);
        }
        if (h.indent >= 0) newline(level);
        o += ']';
        break;
    }
    case ojson::value_t::object: {
        if (v.empty()) { o += "{}"; break; }
        o += '{';
        std::vector<std::pair<const std::string*, const ojson*>> items;
        for (auto it = v.begin(); it != v.end(); ++it) items.push_back({ &it.key(), &it.value() });
        if (h.sort_keys) std::stable_sort(items.begin(), items.end(), [](auto& a, auto& b) { return *a.first < *b.first; });
        bool first = true;
        for (auto& [k, x] : items) {
            if (!first) o += isep;
            first = false;
            if (h.indent >= 0) newline(level + 1);
            put_string(o, *k, h.ensure_ascii);
            o += h.key_sep;
            dump(o, *x, h, level + 1);
        }
        if (h.indent >= 0) newline(level);
        o += '}';
        break;
    }
    default: o += "null"; break;
    }
}

std::string py_dumps(const ojson& v, const PyDump& how) {
    std::string o;
    dump(o, v, how, 0);
    return o;
}

}  // namespace ri
