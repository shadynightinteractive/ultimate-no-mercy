// Commentary director test: sim <pak> <events.tsv> <out.wav> [seed] [talk]
// events.tsv: ms ev a b arg (as the mod sends them; ev -1 = a new match). Prints what's said, when;
// writes the commentary alone, as it'd be heard (mono, 22050 Hz).
#include "director.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <fstream>
#include <string>
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "sim pak events out.wav [seed] [talk]\n"); return 1; }
    std::vector<uint8_t> pak;
    for (int k = 0; ; k++) {
        std::string p = std::string(argv[1]) + (k ? "." + std::to_string(k) : "");
        std::ifstream in(p, std::ios::binary);
        if (!in) break;
        pak.insert(pak.end(), std::istreambuf_iterator<char>(in), {});
    }
    comm::Director d;
    if (!d.load(pak.data(), pak.size())) { fprintf(stderr, "bad pak\n"); return 1; }
    d.seed(argc > 4 ? atoi(argv[4]) : 7);
    { int tk = argc > 5 ? atoi(argv[5]) : 2; d.config(true, tk & 3, ((tk & 0x20) ? 0u : 1u) | ((tk & 0x10) ? 2u : 0u) | ((tk & 0x40) ? 4u : 0u), (tk & 0x80) == 0); }
    struct E { uint64_t ms; int ev, a, b, arg; };
    std::vector<E> ev;
    FILE* f = fopen(argv[2], "r");
    E e;
    long long argl;
    while (f && fscanf(f, "%lu %d %d %d %lld", &e.ms, &e.ev, &e.a, &e.b, &argl) == 5) { e.arg = (int)(uint32_t)argl; ev.push_back(e); }
    if (f) fclose(f);
    const int RATE = 22050;
    uint64_t end = ev.empty() ? 0 : ev.back().ms + 8000;
    std::vector<float> out((size_t)(end * RATE / 1000) + RATE, 0.0f);
    size_t i = 0;
    int said = 0, heard = 0;
    for (uint64_t t = 0; t <= end; t += 16) {
        auto play = [&](int c, const char* why) {
            if (c < 0) return;
            const comm::Clip& k = d.at(c);
            printf("%7.2f  %-12s %-5s %s%s%s\n", t / 1000.0, comm::event_name(k.ev), why, (k.src ? "[L] " : ""), k.speaker == '-' ? "?   " : (toupper(k.speaker) == 'K' ? (k.speaker == 'k' ? "King>" : "King ") : (k.speaker == 'j' ? "JR>  " : "JR   ")), k.text.c_str());
            said++;
            size_t at = (size_t)(t * RATE / 1000);
            auto mix = [&](int cc) {
                auto s = d.sound(cc);
                int ch, rate; short* pcm;
                int n = stb_vorbis_decode_memory(pak.data() + s.first, (int)s.second, &ch, &rate, &pcm);
                if (n > 0) {
                    for (size_t j = 0; j < (size_t)n * RATE / rate && at + j < out.size(); j++) out[at + j] += pcm[(j * rate / RATE) * ch] / 32768.0f;
                    at += (size_t)n * RATE / rate + RATE * comm::CHAIN_GAP_MS / 1000;
                    free(pcm);
                }
            };
            mix(c);
            if (d.chain() >= 0) { printf("%31s+ %s  (heat %+.2f / %+.2f)\n", "", d.at(d.chain()).text.c_str(), d.at(c).heat, d.at(d.chain()).heat); mix(d.chain()); }
            d.started(c, t);
        };
        while (i < ev.size() && ev[i].ms <= t) {
            const E& x = ev[i++];
            if (x.ev < 0) { d.reset(); printf("%7.2f  -- new match --\n", t / 1000.0); continue; }
            int mv = (x.arg >> 16) & 0xFFFF;
            printf("%7.2f  [%s a=%x b=%x n=%d move=%s]\n", t / 1000.0, comm::event_name(x.ev), x.a & 0xFFFF, x.b & 0xFFFF,
                   x.arg & 0xFF, mv == 0xFFFF ? "-" : d.move_name(mv).c_str());
            heard++;
            play(d.event(x.ev, x.a, x.b, x.arg & 0xFF, (x.arg & 0x100) != 0, t, mv == 0xFFFF ? -1 : mv), "");
        }
        play(d.tick(t, true), "tick");
    }
    printf("events %d, lines %d\n", heard, said);
    FILE* w = fopen(argv[3], "wb");
    uint32_t n = (uint32_t)out.size(), bytes = n * 2;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, w); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, w); };
    fwrite("RIFF", 1, 4, w); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, w); u32(16); u16(1); u16(1); u32(RATE); u32(RATE * 2); u16(2); u16(16);
    fwrite("data", 1, 4, w); u32(bytes);
    for (float v : out) { int s = (int)(v * 32767); s = s < -32768 ? -32768 : s > 32767 ? 32767 : s; u16((uint16_t)(int16_t)s); }
    fclose(w);
}
