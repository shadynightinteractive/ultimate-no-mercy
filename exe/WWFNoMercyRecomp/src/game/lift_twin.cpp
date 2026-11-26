// lift_twin.cpp: the proof that a converted function does what the original did. Only in a build with ARCADE_LIFT_TWIN.
//
// For a call in real play, the converted function runs first, "on the side": it reads game memory, but what it writes goes to
// a list. Then the original runs for real, with its writes noted too. The two must end up the same: every byte either wrote
// has the same final value, and the same value comes back. Each function is checked on every one of its first 2,000 calls,
// then one call in 64, so the game stays playable.
//
// Not compared, on purpose: the function's own stack frame (the original's saved registers and locals, which the converted
// function keeps in C++ locals) and the argument slots it may spill to. tools/lift/lift.py separately checks, across the whole
// program, that no caller relies on a scratch register the converted function no longer sets.
//
// A report goes to the file named by the environment variable WCW_LIFT_TWIN, rewritten every 5 seconds.
#ifdef ARCADE_LIFT_TWIN
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "lift_twin.h"

uint8_t* lift_rdram;

// What one call wrote, for the thread running it. Only one call at a time per thread: these functions call nothing.
struct Written { u8 before, after; };
static thread_local std::unordered_map<u32, u8> side_writes;
static thread_local std::unordered_map<u32, Written> real_writes;
static thread_local bool on_the_side = false;

static inline u8 memory_byte(u32 a) { return lift_rdram[(a ^ 3) - 0x80000000u]; }

u8 lift_side_read8(u32 a) {
    if (on_the_side) {
        auto it = side_writes.find(a);
        if (it != side_writes.end()) return it->second;
    }
    return memory_byte(a);
}

void lift_side_write8(u32 a, u8 v) {
    if (on_the_side) side_writes[a] = v;
    else lift_rdram[(a ^ 3) - 0x80000000u] = v;
}

void lift_twin_note_real_write(u32 a, u8 before, u8 after) {
    auto it = real_writes.find(a);
    if (it == real_writes.end()) real_writes.emplace(a, Written{before, after});
    else it->second.after = after;
}

void lift_twin_side_begin(uint8_t* rdram) {
    lift_rdram = rdram;
    side_writes.clear();
    real_writes.clear();
    on_the_side = true;
}

void lift_twin_side_end() { on_the_side = false; }

// Writes every function's counts. Called now and then from whichever thread is finishing a comparison.
static void write_report(const LiftTwinEntry* table, int count) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    const char* path = std::getenv("WCW_LIFT_TWIN");
    if (path == nullptr) return;
    std::string temp = std::string(path) + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "w");
    if (f == nullptr) return;
    for (int i = 0; i < count; i++) {
        const LiftTwinEntry& e = table[i];
        // Name, calls, comparisons, differences, the instructions reached (a hex mask, so reports can be added up), how many
        // instructions there are, and the first difference seen.
        std::string mask;
        for (int bit = e.instructions - 1 - ((e.instructions - 1) % 4); bit >= 0; bit -= 4) {
            int nibble = 0;
            for (int j = 0; j < 4 && bit + j < e.instructions; j++) nibble |= (e.reached[bit + j] ? 1 : 0) << j;
            mask += "0123456789abcdef"[nibble];
        }
        std::fprintf(f, "%s %llu %llu %llu %s %d %s\n", e.name, (unsigned long long)e.calls.load(), (unsigned long long)e.checked.load(),
                     (unsigned long long)e.different.load(), mask.c_str(), e.instructions, e.first_difference[0] ? e.first_difference : "-");
    }
    std::fclose(f);
    std::remove(path);
    std::rename(temp.c_str(), path);
}

void lift_twin_compare(LiftTwinEntry& e, const LiftTwinEntry* table, int count, u32 entry_sp, bool has_return, uint64_t side_return,
                       uint64_t real_return, bool registers_kept) {
    e.checked++;
    u32 frame_low = entry_sp - 0x4000, frame_high = entry_sp + 0x10;     // Its frame, and the four argument slots.
    bool same = registers_kept;
    u32 where = 0;
    u8 got = 0, wanted = 0;
    const char* what = registers_kept ? "memory" : "kept-registers";
    for (const auto& [address, w] : real_writes) {
        if (!same) break;
        if (address >= frame_low && address < frame_high) continue;
        auto it = side_writes.find(address);
        u8 side = it != side_writes.end() ? it->second : w.before;
        if (side != w.after) {
            same = false;
            where = address;
            got = side;
            wanted = w.after;
            break;
        }
    }
    if (same) {
        for (const auto& [address, value] : side_writes) {
            if (address >= frame_low && address < frame_high) continue;
            if (real_writes.find(address) == real_writes.end() && value != memory_byte(address)) {
                same = false;
                where = address;
                got = value;
                wanted = memory_byte(address);
                break;
            }
        }
    }
    if (same && has_return && (u32)side_return != (u32)real_return) {
        same = false;
        what = "return";
        where = 0;
        got = (u8)side_return;
        wanted = (u8)real_return;
    }
    if (!same && e.different.fetch_add(1) == 0) {
        std::snprintf(e.first_difference, sizeof(e.first_difference), "%s@%08X:converted=%02X,original=%02X", what, where, got, wanted);
    }
    static std::atomic<long long> next_report{0};
    long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    long long due = next_report.load();
    if (now >= due && next_report.compare_exchange_strong(due, now + 5)) write_report(table, count);
}
#endif
