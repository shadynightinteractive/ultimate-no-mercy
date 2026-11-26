// [commentary2 2026-11-18] The commentary chooses the same lines from a pack whose index has the TAGS (the shipped recipe)
// as from the same pack with the TEXT: two Directors, one per pack, fed the same long scripted stream of match events
// (every event, both wrestlers' ids from the index, moves, the counts 1-3, Rumble entrants; the same clock and seed),
// each line started as the game does (start + its name chain); the line, its chained name and the speaker compared
// at every step.   director_same_test <pack with text> <pack with tags> [events]
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../../director.h"

static std::vector<uint8_t> slurp(const char* p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string s = ss.str();
    return std::vector<uint8_t>(s.begin(), s.end());
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: director_same_test <pack with text> <pack with tags> [events]\n"); return 2; }
    const std::vector<uint8_t> a = slurp(argv[1]), b = slurp(argv[2]);
    comm::Director A, B;
    if (!A.load(a.data(), a.size()) || !B.load(b.data(), b.size())) { std::fprintf(stderr, "a pack didn't load\n"); return 3; }
    if (A.clips() != B.clips()) { std::printf("DIFFERENT number of lines: %zu / %zu\n", A.clips(), B.clips()); return 1; }
    const long events = argc > 3 ? std::atol(argv[3]) : 200000;
    // wrestler ids the index names (hex, column 3), so the per-wrestler and name lines are reached
    std::set<int> ids;
    {
        const std::string s((const char*)a.data() + (a[16] | a[17] << 8 | a[18] << 16), (size_t)(a[20] | a[21] << 8 | a[22] << 16));
        std::istringstream in(s);
        std::string l;
        while (std::getline(in, l) && l != "#moves") {
            size_t t1 = l.find('\t'), t2 = l.find('\t', t1 + 1), t3 = l.find('\t', t2 + 1);
            std::string w = l.substr(t2 + 1, t3 - t2 - 1);
            if (!w.empty() && w[0] == 'v') w = w.substr(1);
            if (w != "-" && w != "*") ids.insert((int)std::strtol(w.c_str(), nullptr, 16));
        }
    }
    const std::vector<int> wid(ids.begin(), ids.end());
    uint32_t r = 12345;
    auto rnd = [&]() { r = r * 1103515245u + 12345u; return (r >> 8) & 0xFFFFFF; };
    for (comm::Director* d : { &A, &B }) { d->seed(7); d->config(true, 3, 1, true); d->reset(); }
    uint64_t now = 0;
    long said = 0, chained = 0, counts = 0, diffs = 0;
    for (long i = 0; i < events; i++) {
        now += 200 + rnd() % 3000;
        if (i % 4000 == 0) { A.reset(); B.reset(); }
        const int ev = 1 + (int)(rnd() % (comm::EV_COUNT_ - 1));
        const int wa = wid[rnd() % wid.size()], wb = wid[rnd() % wid.size()];
        const int arg = ev == comm::EV_COUNT ? 1 + (int)(rnd() % 3) : (int)(rnd() % 40);
        const bool female = rnd() % 8 == 0;
        const int move = rnd() % 3 ? (int)(rnd() % 1460) : -1;
        int ca, cb;
        if (rnd() % 5 == 0) { ca = A.tick(now, true); cb = B.tick(now, true); }
        else { ca = A.event(ev, wa, wb, arg, female, now, move); cb = B.event(ev, wa, wb, arg, female, now, move); }
        const int na = ca >= 0 ? A.chain() : -2, nb = cb >= 0 ? B.chain() : -2;
        if (ca != cb || na != nb || (ca >= 0 && A.chain_gain() != B.chain_gain())) {
            if (diffs++ < 10) std::printf("step %ld: event %d -> %d / %d (name %d / %d)\n", i, ev, ca, cb, na, nb);
        }
        if (ca >= 0) {
            said++; chained += na >= 0; counts += ev == comm::EV_COUNT;
            A.started(ca, now); B.started(cb, now);
            if (rnd() % 3 == 0) { A.stopped(); B.stopped(); }
        }
    }
    std::printf("%ld events: %ld lines said (%ld with a name chained, %ld on counts), %ld different\n", events, said, chained, counts, diffs);
    std::printf("%s\n", diffs ? "DIFFERENT" : "THE SAME lines, names and gains at every step");
    return diffs ? 1 : 0;
}
