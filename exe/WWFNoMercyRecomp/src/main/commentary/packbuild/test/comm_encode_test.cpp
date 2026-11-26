// [commentary2 2026-11-18] Test CLI for comm_encode: a raw s16le mono file -> the clip, as the game's builder makes it.
//   comm_encode_test <in.raw> <rate> <serial> <comment packet hex> <out.ogg> [notrim]
// tools\commentary\test_encode.py runs it beside FFmpeg + oggpk.replace_comment and compares the bytes.
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "../comm_encode.h"

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: comm_encode_test <in.raw> <rate> <serial> <comment hex> <out.ogg> [notrim]\n");
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) return 3;
    std::vector<float> x;
    int16_t s[4096];
    size_t got;
    while ((got = std::fread(s, 2, 4096, f)) > 0)
        for (size_t i = 0; i < got; i++) x.push_back((float)s[i] * (1.0f / 32768.0f));
    std::fclose(f);
    const int rate = std::atoi(argv[2]);
    const uint32_t serial = (uint32_t)std::strtoul(argv[3], nullptr, 10);
    std::vector<uint8_t> comment;
    const std::string hex = argv[4];
    for (size_t i = 0; i + 1 < hex.size(); i += 2) comment.push_back((uint8_t)std::stoul(hex.substr(i, 2), nullptr, 16));
    size_t first = 0, last = x.size();
    if (argc < 7) comm_build::trim_silence(x, rate, first, last);
    const std::vector<uint8_t> ogg = comm_build::encode_clip(x.data() + first, last - first, rate, comment, serial);
    if (ogg.empty()) return 4;
    FILE* o = std::fopen(argv[5], "wb");
    if (!o) return 5;
    std::fwrite(ogg.data(), 1, ogg.size(), o);
    std::fclose(o);
    std::printf("%zu samples kept of %zu (from %zu), %zu bytes\n", last - first, x.size(), first, ogg.size());
    return 0;
}
