// lift_twin.h: what the generated proof build needs from lift_twin.cpp. See there for how a comparison works.
#pragma once
#ifdef ARCADE_LIFT_TWIN
#include <atomic>
#include <cstdio>
#include <string>
#include "recomp.h"
#include "lift_mem.h"

// One converted function's running score.
struct LiftTwinEntry {
    const char* name;
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> checked{0};
    std::atomic<uint64_t> different{0};
    char first_difference[64] = {0};
    u8* reached;       // Which of the original's instructions have run during a comparison, one byte each.
    int instructions;
    LiftTwinEntry(const char* n, u8* r, int count) : name(n), reached(r), instructions(count) {}
};

void lift_twin_side_begin(uint8_t* rdram);
void lift_twin_side_end();
void lift_twin_note_real_write(u32 address, u8 before, u8 after);
void lift_twin_compare(LiftTwinEntry& entry, const LiftTwinEntry* table, int count, u32 entry_sp, bool has_return, uint64_t side_return,
                       uint64_t real_return, bool registers_kept);

// The registers a converted function leaves alone, as the original found them. If the original ends with any of these changed, it
// isn't a whole function: something else is relying on it to do more than m2c shows (func_8002C57C was the tail end of another
// function, and restored the stack for it). The comparison can't otherwise see this, because the original is what really runs.
struct LiftKeptRegisters {
    gpr values[11];
    explicit LiftKeptRegisters(const recomp_context* c)
        : values{c->r16, c->r17, c->r18, c->r19, c->r20, c->r21, c->r22, c->r23, c->r29, c->r30, c->r31} {}
    bool same_as(const recomp_context* c) const {
        LiftKeptRegisters now(c);
        for (int i = 0; i < 11; i++) {
            if (values[i] != now.values[i]) return false;
        }
        return true;
    }
};

// Check every one of the first 2,000 calls, then one in 64.
inline bool lift_twin_should_check(LiftTwinEntry& e) {
    uint64_t n = e.calls.fetch_add(1);
    if (n == 0) { fprintf(stderr, "[lift] first call: %s\n", e.name); fflush(stderr); }    // (So a crash can be traced to its function.)
    return n < 2000 || (n & 63) == 0;
}

// The originals' memory access, redefined so what they write is noted. They read and write memory exactly as before.
template <class T> struct LiftRealMem {
    uint8_t* rdram;
    uint64_t address;     // As the game's code computes it, sign-extended: 0xFFFFFFFF80000000 and up.
    int flip;
    T* where() const { return (T*)(rdram + ((address ^ (uint64_t)flip) - 0xFFFFFFFF80000000ull)); }
    operator T() const { return *where(); }
    const LiftRealMem& operator=(T v) const {
        u32 a = (u32)address;
        // Note the bytes' old values, write, then note the new ones.
        u8 before[sizeof(T)];
        for (u32 i = 0; i < sizeof(T); i++) before[i] = rdram[((a + i) ^ 3) - 0x80000000u];
        *where() = v;
        for (u32 i = 0; i < sizeof(T); i++) lift_twin_note_real_write(a + i, before[i], rdram[((a + i) ^ 3) - 0x80000000u]);
        return *this;
    }
};
#endif
