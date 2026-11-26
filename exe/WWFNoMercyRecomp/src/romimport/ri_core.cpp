// ri_core.cpp (phase3, 2026-11-19): files, hashes and messages for the ROM importer (ri_core.h).
#include "ri_core.h"
#include <cstdarg>
#include <cstdio>
#include <fstream>

namespace ri {

std::string strf(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    va_list b; va_copy(b, a);
    int n = vsnprintf(nullptr, 0, fmt, a); va_end(a);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) vsnprintf(s.data(), n + 1, fmt, b);
    va_end(b);
    return s;
}

void fail(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
    va_list b; va_copy(b, a);
    int n = vsnprintf(nullptr, 0, fmt, a); va_end(a);
    std::string s(n > 0 ? n : 0, '\0');
    if (n > 0) vsnprintf(s.data(), n + 1, fmt, b);
    va_end(b);
    throw Error(s);
}

// ---- files ----------------------------------------------------------------------------------------------------------
bool read_file_if(const fs::path& p, Bytes& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n < 0) return false;
    f.seekg(0);
    out.resize((size_t)n);
    if (n && !f.read((char*)out.data(), n)) return false;
    return true;
}

Bytes read_file(const fs::path& p) {
    Bytes b;
    if (!read_file_if(p, b)) fail("can't read %s", p.string().c_str());
    return b;
}

void write_file(const fs::path& p, const void* data, size_t n) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p; tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || (n && !f.write((const char*)data, (std::streamsize)n))) fail("can't write %s", tmp.string().c_str());
    }
    fs::rename(tmp, p, ec);
    if (ec) { fs::remove(tmp, ec); fail("can't write %s", p.string().c_str()); }
}

// A file written straight to its place (no temporary beside it): for files in a folder of the importer's own that is moved in
// whole when it is complete (content\_import_tmp\...) -- a stop half way leaves only that folder, which the next start clears.
void write_new_file(const fs::path& p, const void* data, size_t n) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::error_code ec;
        if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
        f.open(p, std::ios::binary | std::ios::trunc);
    }
    if (!f || (n && !f.write((const char*)data, (std::streamsize)n))) fail("can't write %s", p.string().c_str());
}

// ---- SHA-1 (FIPS 180-4) ---------------------------------------------------------------------------------------------
static inline uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

std::string sha1_hex(const uint8_t* p, size_t n) {
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    auto block = [&](const uint8_t* b) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = be32(b + 4 * i);
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999; }
            else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = bb ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(bb, 30); bb = a; a = t;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    };
    size_t full = n / 64 * 64;
    for (size_t i = 0; i < full; i += 64) block(p + i);
    uint8_t tail[128] = {};
    size_t r = n - full;
    memcpy(tail, p + full, r);
    tail[r] = 0x80;
    size_t tl = r + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
    block(tail);
    if (tl == 128) block(tail + 64);
    std::string s;
    for (int i = 0; i < 5; i++) s += strf("%08x", h[i]);
    return s;
}

// ---- MD5 (RFC 1321) -------------------------------------------------------------------------------------------------
std::string md5_hex(const uint8_t* p, size_t n) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
    static const int S[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9,
                               14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15,
                               21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
    uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    auto block = [&](const uint8_t* b) {
        uint32_t m[16];
        for (int i = 0; i < 16; i++) m[i] = b[4 * i] | b[4 * i + 1] << 8 | b[4 * i + 2] << 16 | (uint32_t)b[4 * i + 3] << 24;
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f; int g;
            if (i < 16) { f = (bb & c) | (~bb & d); g = i; }
            else if (i < 32) { f = (d & bb) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = bb ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (bb | ~d); g = (7 * i) % 16; }
            uint32_t t = d; d = c; c = bb;
            bb = bb + rol(a + f + K[i] + m[g], S[i]);
            a = t;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d;
    };
    size_t full = n / 64 * 64;
    for (size_t i = 0; i < full; i += 64) block(p + i);
    uint8_t tail[128] = {};
    size_t r = n - full;
    memcpy(tail, p + full, r);
    tail[r] = 0x80;
    size_t tl = r + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) tail[tl - 8 + i] = (uint8_t)(bits >> (8 * i));
    block(tail);
    if (tl == 128) block(tail + 64);
    std::string s;
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) s += strf("%02x", (h[i] >> (8 * k)) & 0xFF);
    return s;
}

// ---- SHA-256 (FIPS 180-4) -------------------------------------------------------------------------------------------
std::array<uint8_t, 32> sha256(const uint8_t* p, size_t n) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    auto ror = [](uint32_t x, int k) { return x >> k | x << (32 - k); };
    auto block = [&](const uint8_t* b) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = be32(b + 4 * i);
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    };
    size_t full = n / 64 * 64;
    for (size_t i = 0; i < full; i += 64) block(p + i);
    uint8_t tail[128] = {};
    size_t r = n - full;
    memcpy(tail, p + full, r);
    tail[r] = 0x80;
    size_t tl = r + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (uint8_t)(bits >> (8 * i));
    block(tail);
    if (tl == 128) block(tail + 64);
    std::array<uint8_t, 32> out;
    for (int i = 0; i < 8; i++) put_be32(out.data() + 4 * i, h[i]);
    return out;
}

std::string sha256_hex(const uint8_t* p, size_t n) {
    std::array<uint8_t, 32> d = sha256(p, n);
    std::string s;
    for (uint8_t c : d) s += strf("%02x", c);
    return s;
}

}  // namespace ri
