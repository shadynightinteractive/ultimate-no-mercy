// [arcade] The first of the game's own functions rewritten native, by reading it: func_800028B4, which each frame turns the scene's draw items into the graphics commands that draw them.
//   s16 func_800028B4(u32* gfx, s16 slot, s16 first)
// The sorted list (0x80061D40, how many: 0x800619B4) is taken from `first` for as long as its items are of the first one's kind (item +0: 0 or 1). Each entry is a chain of items (+8). For the whole chain first: each item's matrix
// (its own, for this frame's slot: item + 0x38 + slot * 64) and its points; then, again along the chain, each item's light colours (unless its node's +0x19, the HUD's) and its list. Returns where it stopped.
// recomp_arcade_scene_check runs it beside the original and compares every word (QA, while it's being proved); recomp_arcade_scene_emit is it in the original's place.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "ultramodern/ultramodern.hpp"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "six_layout.h"   // (the sorted list moves with the engine's layout: SIX_80061D40)

namespace {

struct Mem {
    uint8_t* rdram;
    uint8_t* at(uint32_t a) const { return rdram + (a - 0x80000000u); }
    uint32_t u32(uint32_t a) const { return *reinterpret_cast<uint32_t*>(at(a)); }
    int16_t s16(uint32_t a) const { return *reinterpret_cast<int16_t*>(at(a ^ 2u)); }
    uint8_t u8(uint32_t a) const { return *at(a ^ 3u); }
    void w32(uint32_t a, uint32_t v) const { *reinterpret_cast<uint32_t*>(at(a)) = v; }
};

constexpr uint32_t ITEMS = SIX_80061D40, ITEM_N = 0x800619B4u, LIGHT_SET = 0x80043610u, LIGHT_A = 0x80062B68u, LIGHT_B = 0x80062B88u;

inline uint32_t physical(uint32_t a) { return a & 0x1FFFFFFFu; }       // (func_80034AE0: osVirtualToPhysical, all of this in KSEG0)

// (Out: put(w0, w1), one command)
template <class Out>
int32_t build(const Mem& m, int32_t slot, int32_t first, Out&& put) {
    const int16_t s = static_cast<int16_t>(first);
    uint32_t idx = static_cast<uint16_t>(s) | (s < 0 ? 0xFFFF0000u : 0u);
    uint32_t e = ITEMS + (static_cast<uint32_t>(static_cast<int32_t>(s)) << 2);
    const uint32_t kind = m.u32(m.u32(e));
    const uint32_t mtx_at = (static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(slot))) << 6) + 0x38u;
    while (idx < static_cast<uint32_t>(static_cast<int32_t>(m.s16(ITEM_N)))) {
        const uint32_t head = m.u32(e);
        if (m.u32(head) != kind) break;
        for (uint32_t it = head; it; it = m.u32(it + 8u)) {
            put(0xDC38000Eu, it + mtx_at - 0x80000000u);
            put(0xDB0C0000u, 0x00010000u);
            const uint32_t v = m.u32(it + 0x30u);
            if (!v) continue;
            uint32_t a = physical(v), cnt = static_cast<uint32_t>(static_cast<int32_t>(m.s16(it + 0x2Cu))), f = static_cast<uint32_t>(static_cast<int32_t>(m.s16(it + 0x2Eu)));
            while (cnt > 32u) {
                f += 32u; cnt -= 32u;
                put(0x01020000u | ((f & 0x7Fu) << 1), a);
                a += 0x200u;
            }
            put(0x01000000u | ((cnt & 0xFFu) << 12) | (((f + cnt) & 0x7Fu) << 1), a);
        }
        for (uint32_t it = head; it; it = m.u32(it + 8u)) {
            const uint32_t dl = m.u32(it + 0x34u);
            if (!dl) continue;
            const uint32_t node = m.u32(it + 4u);
            if (!m.u8(node + 0x19u)) {
                const uint32_t c = ((m.u8(node + 0x1Au) & 0xFu) << 2) + (static_cast<uint32_t>(static_cast<int32_t>(m.s16(LIGHT_SET))) << 4);
                put(0xFA000000u, m.u32(LIGHT_A + c));
                put(0xFB000000u, m.u32(LIGHT_B + c));
            }
            put(0xDE000000u, dl - 0x80000000u);
        }
        idx++; e += 4u;
    }
    return static_cast<int16_t>(idx);
}

// recomp_arcade_scene_emit(gfx, slot, first) -> where it stopped: in the original's place
void scene_emit(uint8_t* rdram, recomp_context* ctx) {
    const Mem m{ rdram };
    const uint32_t gfx = _arg<0, uint32_t>(rdram, ctx);
    const int32_t slot = _arg<1, int32_t>(rdram, ctx), first = _arg<2, int32_t>(rdram, ctx);
    uint32_t p = m.u32(gfx);
    const int32_t r = build(m, slot, first, [&](uint32_t a, uint32_t b) { m.w32(p, a); m.w32(p + 4u, b); p += 8u; });
    m.w32(gfx, p);
    ctx->r2 = r;
}

// recomp_arcade_scene_check(start, end, slot, first): what the original wrote from start to end against what this makes
uint64_t chk_calls, chk_words, chk_bad;
void scene_check(uint8_t* rdram, recomp_context* ctx) {
    const Mem m{ rdram };
    const uint32_t start = _arg<0, uint32_t>(rdram, ctx), end = _arg<1, uint32_t>(rdram, ctx);
    const int32_t slot = _arg<2, int32_t>(rdram, ctx), first = _arg<3, int32_t>(rdram, ctx);
    static std::vector<uint32_t> mine;
    mine.clear();
    build(m, slot, first, [&](uint32_t a, uint32_t b) { mine.push_back(a); mine.push_back(b); });
    const size_t theirs = (end - start) / 4u;
    bool bad = mine.size() != theirs;
    size_t where = 0;
    for (size_t i = 0; i < theirs && i < mine.size() && !bad; i++) if (mine[i] != m.u32(start + static_cast<uint32_t>(i) * 4u)) { bad = true; where = i; }
    chk_calls++; chk_words += theirs;
    if (bad) {
        chk_bad++;
        if (chk_bad <= 8) {
            fprintf(stderr, "[scene] DIFFERENT: call %llu slot %d first %d: %zu words, mine %zu; at word %zu: theirs %08X mine %08X\n", (unsigned long long)chk_calls, slot, first, theirs, mine.size(), where,
                    where < theirs ? m.u32(start + static_cast<uint32_t>(where) * 4u) : 0u, where < mine.size() ? mine[where] : 0u);
            fflush(stderr);
        }
    }
    if ((chk_calls % 2000u) == 0u) { fprintf(stderr, "[scene] %llu scene lists compared, %llu words, %llu different\n", (unsigned long long)chk_calls, (unsigned long long)chk_words, (unsigned long long)chk_bad); fflush(stderr); }
}

} // namespace

void arcade_scene_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_scene_emit", scene_emit);
    recomp::overlays::register_base_export("recomp_arcade_scene_check", scene_check);
}
