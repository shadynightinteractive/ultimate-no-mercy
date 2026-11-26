/* aki_unpack.c (portable, 2026-11-15): every file of an AKI game's file table, unpacked, in one go -- nmrebuild.py's AkiFiles
 * in C, for speed (Python's LZSS takes minutes for a whole table). The same steps as tools\handlers\handlers\aki\romtable.py:
 * a table of big-endian u32 offsets from a base (bit 0: LZSS-packed, a u32 unpacked size first), AKI's LZSS (a 4 KB ring from
 * 0xFEE, flag bytes low bit first, 12-bit place and 4-bit length + 3), a "size" over 4 MB meaning the file is stored as it is.
 *
 *   aki_unpack <rom .z64> <table offset> <entries> <data base> <out.bin> <out.idx>
 *
 * out.bin: the files one after another; out.idx: per file (1 .. entries-1) its offset (u64), length (u32), packed (u32), little
 * endian. Holds no game data: it reads the player's ROM. Build: clang -O2 aki_unpack.c -o aki_unpack(.exe). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static size_t lzss(const uint8_t* src, size_t ls, uint8_t* out, size_t n) {
    uint8_t ring[0x1000]; memset(ring, 0, sizeof ring);
    size_t r = 0xFEE, i = 4, o = 0; unsigned flags = 0;
    while (o < n && i < ls) {
        flags >>= 1;
        if (!(flags & 0x100)) { flags = src[i++] | 0xFF00u; }
        if (flags & 1) {
            if (i >= ls) break;
            uint8_t c = src[i++]; out[o++] = c; ring[r] = c; r = (r + 1) & 0xFFF;
        } else {
            if (i + 1 >= ls) break;
            unsigned lo = src[i], hi = src[i + 1]; i += 2;
            unsigned pos = lo | ((hi & 0xF0u) << 4), len = (hi & 0xFu) + 3;
            for (unsigned k = 0; k < len; k++) {
                uint8_t c = ring[(pos + k) & 0xFFF];
                if (o < n) out[o++] = c;
                ring[r] = c; r = (r + 1) & 0xFFF;
            }
        }
    }
    return n;      /* (Python's makes a buffer of n bytes: what isn't reached stays zero, as calloc's) */
}

int main(int argc, char** argv) {
    if (argc != 7) { fprintf(stderr, "usage: aki_unpack <rom> <table offset> <entries> <base> <out.bin> <out.idx>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb"); if (!f) { fprintf(stderr, "aki_unpack: can't read the ROM\n"); return 1; }
    fseek(f, 0, SEEK_END); long rs = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* rom = malloc((size_t)rs); if (!rom || fread(rom, 1, (size_t)rs, f) != (size_t)rs) { fprintf(stderr, "aki_unpack: read failed\n"); return 1; }
    fclose(f);
    const uint32_t tab = (uint32_t)strtoul(argv[2], 0, 0), entries = (uint32_t)strtoul(argv[3], 0, 0), base = (uint32_t)strtoul(argv[4], 0, 0);
    if ((uint64_t)tab + 4ull * entries > (uint64_t)rs) { fprintf(stderr, "aki_unpack: the table is past the ROM's end\n"); return 1; }
    FILE* ob = fopen(argv[5], "wb"); FILE* oi = fopen(argv[6], "wb");
    if (!ob || !oi) { fprintf(stderr, "aki_unpack: can't write\n"); return 1; }
    uint64_t at = 0; uint8_t* buf = 0; size_t cap = 0;
    for (uint32_t i = 1; i < entries; i++) {
        const uint32_t t0 = be32(rom + tab + 4 * (i - 1)), t1 = be32(rom + tab + 4 * i);
        uint64_t a = (uint64_t)(t0 & ~1u) + base, b = (uint64_t)(t1 & ~1u) + base;
        if (a > (uint64_t)rs) a = (uint64_t)rs;
        if (b > (uint64_t)rs) b = (uint64_t)rs;
        const uint8_t* raw = rom + a; size_t rl = b > a ? (size_t)(b - a) : 0;   /* (Python's slice: empty when b <= a) */
        const uint8_t* data = raw; size_t dl = rl; uint32_t packed = t0 & 1u;
        if (packed && rl >= 4) {
            uint32_t dn = be32(raw);
            if (dn <= 0x400000u) {
                if (dn > cap) { cap = dn; buf = realloc(buf, cap ? cap : 1); }
                memset(buf, 0, dn);
                lzss(raw, rl, buf, dn); data = buf; dl = dn;
            }
        }
        fwrite(data, 1, dl, ob);
        uint32_t len32 = (uint32_t)dl;
        fwrite(&at, 8, 1, oi); fwrite(&len32, 4, 1, oi); fwrite(&packed, 4, 1, oi);
        at += dl;
    }
    fclose(ob); fclose(oi);
    return 0;
}
