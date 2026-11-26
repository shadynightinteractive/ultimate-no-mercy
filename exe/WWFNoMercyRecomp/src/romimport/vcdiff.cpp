// vcdiff.cpp (reduxthemes, 2026-11-18): a VCDIFF (xdelta3) patch applied in memory -- the C++ twin of
// tools\handlers\handlers\aki\vcdiff.py, giving the same bytes (tools\romimport\native\vcdiff_check.py proves it on the
// player's own patches: No Mercy Plus V3 and WCW/nWo Revenge Redux). It moves into the game's first-launch builder in Phase 3
// (the player supplies a mod as an xdelta against his clean ROM; the ROM is never copied, the patch never run).
//
// RFC 3284 with xdelta3's one addition (an Adler-32 of each target window, checked when present). Not handled, with a plain
// message rather than a wrong ROM: secondary compression (xdelta3 -S), a patch's own code table, a compressed window.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vcdiff {

typedef std::vector<uint8_t> Bytes;
enum { NOOP = 0, ADD = 1, RUN = 2, COPY = 3 };
struct Code { uint8_t type1, size1, mode1, type2, size2, mode2; };

// the default code table (RFC 3284 section 5.6), built as vcdiff.py builds it
static std::vector<Code> default_table() {
    std::vector<Code> t;
    t.push_back({ RUN, 0, 0, NOOP, 0, 0 });
    t.push_back({ ADD, 0, 0, NOOP, 0, 0 });
    for (int s = 1; s < 18; s++) t.push_back({ ADD, (uint8_t)s, 0, NOOP, 0, 0 });
    for (int m = 0; m < 9; m++) {
        t.push_back({ COPY, 0, (uint8_t)m, NOOP, 0, 0 });
        for (int s = 4; s < 19; s++) t.push_back({ COPY, (uint8_t)s, (uint8_t)m, NOOP, 0, 0 });
    }
    for (int m = 0; m < 6; m++)
        for (int a = 1; a < 5; a++)
            for (int c = 4; c < 7; c++) t.push_back({ ADD, (uint8_t)a, 0, COPY, (uint8_t)c, (uint8_t)m });
    for (int m = 6; m < 9; m++)
        for (int a = 1; a < 5; a++) t.push_back({ ADD, (uint8_t)a, 0, COPY, 4, (uint8_t)m });
    for (int m = 0; m < 9; m++) t.push_back({ COPY, 4, (uint8_t)m, ADD, 1, 0 });
    return t;   // 256 entries
}

static uint32_t adler32(const uint8_t* p, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; i++) { a = (a + p[i]) % 65521u; b = (b + a) % 65521u; }
    return b << 16 | a;
}

struct Reader {
    const uint8_t* p; size_t n, i = 0; bool bad = false;
    Reader(const uint8_t* p_, size_t n_) : p(p_), n(n_) {}
    uint8_t byte() { if (i >= n) { bad = true; return 0; } return p[i++]; }
    uint64_t varint() {
        uint64_t v = 0;
        for (int k = 0; k < 10; k++) { uint8_t c = byte(); v = v << 7 | (c & 0x7F); if (!(c & 0x80) || bad) return v; }
        bad = true; return 0;
    }
};

// source + patch -> target; false (and why) when the patch can't be applied or doesn't match the source
bool apply(const Bytes& source, const Bytes& patch, Bytes& out, std::string& err) {
    static const std::vector<Code> TABLE = default_table();
    out.clear();
    if (patch.size() < 5 || patch[0] != 0xD6 || patch[1] != 0xC3 || patch[2] != 0xC4) { err = "not a VCDIFF / xdelta3 patch"; return false; }
    Reader r(patch.data(), patch.size()); r.i = 4;
    uint8_t hi = r.byte();
    if (hi & 0x01) { err = "this patch uses secondary compression (xdelta3 -S): not handled"; return false; }
    if (hi & 0x02) { err = "this patch has its own code table: not handled"; return false; }
    if (hi & 0x04) { uint64_t n = r.varint(); r.i += (size_t)n; }       // (xdelta3's application header)
    while (r.i < r.n && !r.bad) {
        uint8_t wi = r.byte();
        const uint8_t* seg = nullptr; uint64_t slen = 0;
        if (wi & 0x03) {
            slen = r.varint(); uint64_t spos = r.varint();
            const Bytes& from = (wi & 0x01) ? source : out;
            if (spos + slen > from.size()) { err = "a window's source segment is past its end"; return false; }
            seg = from.data() + spos;
        }
        Bytes segcopy;                                                   // (from the target: out grows below)
        if (seg && !(wi & 0x01)) { segcopy.assign(seg, seg + slen); seg = segcopy.data(); }
        r.varint();                                                      // (the delta's length)
        uint64_t tlen = r.varint();
        uint8_t di = r.byte();
        if (di) { err = "this patch's window is compressed (xdelta3 -S): not handled"; return false; }
        uint64_t alen = r.varint(), ilen = r.varint(), clen = r.varint();
        bool has_ck = (wi & 0x04) != 0; uint32_t ck = 0;
        if (has_ck) for (int k = 0; k < 4; k++) ck = ck << 8 | r.byte();
        if (r.bad || r.i + alen + ilen + clen > r.n) { err = "the patch ends early"; return false; }
        const uint8_t* data = patch.data() + r.i; r.i += (size_t)alen;
        Reader inst(patch.data() + r.i, (size_t)ilen); r.i += (size_t)ilen;
        Reader addr(patch.data() + r.i, (size_t)clen); r.i += (size_t)clen;
        Bytes win; win.reserve((size_t)tlen);
        size_t dp = 0;
        uint64_t near_[4] = { 0, 0, 0, 0 }; int ni = 0;
        std::vector<uint64_t> same(3 * 256, 0);
        while (inst.i < inst.n) {
            const Code& c = TABLE[inst.byte()];
            const uint8_t types[2] = { c.type1, c.type2 }, sizes[2] = { c.size1, c.size2 }, modes[2] = { c.mode1, c.mode2 };
            for (int h = 0; h < 2; h++) {
                if (types[h] == NOOP) continue;
                uint64_t size = sizes[h] ? sizes[h] : inst.varint();
                if (types[h] == ADD) {
                    if (dp + size > alen) { err = "an ADD past the data"; return false; }
                    win.insert(win.end(), data + dp, data + dp + size); dp += (size_t)size;
                } else if (types[h] == RUN) {
                    if (dp >= alen) { err = "a RUN past the data"; return false; }
                    win.insert(win.end(), (size_t)size, data[dp]); dp += 1;
                } else {
                    uint64_t here = slen + win.size(), a;
                    int mode = modes[h];
                    if (mode == 0) a = addr.varint();
                    else if (mode == 1) a = here - addr.varint();
                    else if (mode < 6) a = near_[mode - 2] + addr.varint();
                    else a = same[(size_t)(mode - 6) * 256 + addr.byte()];
                    near_[ni] = a; ni = (ni + 1) % 4; same[(size_t)(a % (3 * 256))] = a;
                    if (a + size <= slen) win.insert(win.end(), seg + a, seg + a + size);
                    else                                                 // (from the window being made, maybe overlapping)
                        for (uint64_t k = 0; k < size; k++) {
                            uint64_t q = a + k;
                            if (q < slen) win.push_back(seg[q]);
                            else if (q - slen < win.size()) win.push_back(win[(size_t)(q - slen)]);
                            else { err = "a COPY from beyond what is made"; return false; }
                        }
                }
            }
            if (inst.bad || addr.bad) { err = "the patch's instructions end early"; return false; }
        }
        if (win.size() != tlen) { err = "a window came out the wrong size"; return false; }
        if (has_ck && adler32(win.data(), win.size()) != ck) { err = "a window's Adler-32 doesn't match: the patch isn't for this ROM"; return false; }
        out.insert(out.end(), win.begin(), win.end());
    }
    if (r.bad) { err = "the patch ends early"; return false; }
    return true;
}

}  // namespace vcdiff

#ifdef VCDIFF_MAIN
// vcdiff <source> <patch.xdelta> <out>: the patched file
static bool read_file(const char* p, vcdiff::Bytes& b) {
    FILE* f = fopen(p, "rb"); if (!f) return false;
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) b.insert(b.end(), buf, buf + n);
    fclose(f); return true;
}
int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "vcdiff <source> <patch> <out>\n"); return 2; }
    vcdiff::Bytes src, pat, out; std::string err;
    if (!read_file(argv[1], src) || !read_file(argv[2], pat)) { fprintf(stderr, "can't read the input\n"); return 1; }
    if (!vcdiff::apply(src, pat, out, err)) { fprintf(stderr, "vcdiff: %s\n", err.c_str()); return 1; }
    FILE* o = fopen(argv[3], "wb"); if (!o) return 1;
    fwrite(out.data(), 1, out.size(), o); fclose(o);
    printf("%zu bytes\n", out.size());
    return 0;
}
#endif
