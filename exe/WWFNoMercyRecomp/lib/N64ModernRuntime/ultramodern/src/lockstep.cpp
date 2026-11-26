// [replay 2026-11-14] Lockstep: the game's clock and its events, moved on only when the game is waiting (see
// ultramodern/lockstep.hpp for why and how). This file is the clock, the idle thread's turn, and the pacing.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/lockstep.hpp"

namespace {

bool env_flag(const char* name, bool otherwise) {
    const char* v = getenv(name);
    if (v == nullptr || v[0] == '\0') return otherwise;
    return !(v[0] == '0' && v[1] == '\0');
}

// (read once: the run is in lockstep or not from its first moment)
const bool lockstep_on = [] {
    return env_flag("NM_LOCKSTEP", false) || getenv("NM_REPLAY_RECORD") != nullptr || getenv("NM_REPLAY_PLAY") != nullptr;
}();
// (a replay or the no-graphics mode runs as fast as it can; a recording made live keeps the wall clock's pace)
const bool lockstep_paced = [] {
    const bool fast = getenv("NM_REPLAY_PLAY") != nullptr || env_flag("NM_HEADLESS", false);
    return env_flag("NM_LOCKSTEP_PACE", !fast);
}();

std::atomic<uint64_t> clock_ticks{ 0 };        // the game's clock; only the idle thread moves it
std::atomic<uint64_t> retrace_count{ 0 };
std::atomic<uint64_t> foreign_count{ 0 };
ultramodern::lockstep::callbacks_t callbacks{};

// Messages from outside the game's threads. There should be none in lockstep (every source is on a game thread or at
// the idle turn); any that come are handed over at the next idle turn and counted, since their order is the PC's.
struct Foreign { PTR(OSMesgQueue) mq; OSMesg msg; bool jam; bool retry; };
std::mutex foreign_mutex;
std::deque<Foreign> foreign;

// The pacing: the game's clock against the wall clock, from a base that's moved whenever the speed changes or the game
// has fallen behind (it is never made to catch up: in lockstep a slow stretch is slow motion, not skipped frames).
struct Pace {
    bool started = false;
    uint32_t speed = 1;
    uint64_t base_ticks = 0;
    std::chrono::steady_clock::time_point base_wall{};
} pace;

void pace_to(uint64_t ticks) {
    if (!lockstep_paced) return;
    const uint32_t speed = ultramodern::get_speed_multiplier();
    const auto wall = std::chrono::steady_clock::now();
    if (!pace.started || speed != pace.speed) {
        pace.started = true; pace.speed = speed; pace.base_ticks = clock_ticks.load(); pace.base_wall = wall;
    }
    const uint64_t ahead_us = (ticks - pace.base_ticks) * 1000ull / (46'875ull * speed);
    const auto due = pace.base_wall + std::chrono::microseconds(ahead_us);
    if (due > wall) {
        std::this_thread::sleep_until(due);
    } else if (wall - due > std::chrono::milliseconds(250)) {
        pace.base_ticks = ticks; pace.base_wall = wall;     // (behind by a quarter of a second: go on from here)
    }
}

// A watch on the clock: in lockstep the game stops dead if a thread keeps running without ever waiting (a loop polling
// for a message that only the idle turn would bring). Said in the log; nothing is done about it.
void start_watch() {
    std::thread([] {
        uint64_t last = clock_ticks.load(); int still = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            const uint64_t now = clock_ticks.load();
            if (now == last) {
                if (++still == 2) fprintf(stderr, "[lockstep] the game's clock has not moved for 10 s (no idle turn: a game thread busy, or the game stopped) at retrace %llu\n", (unsigned long long)retrace_count.load());
            } else {
                still = 0;
            }
            last = now;
        }
    }).detach();
}

}  // namespace

namespace ultramodern::lockstep {

bool on() { return lockstep_on; }
bool paced() { return lockstep_paced; }
uint64_t now() { return clock_ticks.load(std::memory_order_relaxed); }
uint64_t retraces() { return retrace_count.load(std::memory_order_relaxed); }
uint64_t foreign_messages() { return foreign_count.load(); }
void set_callbacks(const callbacks_t& c) { callbacks = c; }

// (the host's own values in the game's memory: noted as threads are made, read by a hash between)
static std::mutex host_bytes_mutex;
static std::vector<HostBytes> host_bytes_list;
void note_host_bytes(uint32_t n64_address, uint32_t bytes) {
    std::lock_guard lock{ host_bytes_mutex };
    host_bytes_list.push_back({ n64_address - 0x80000000u, bytes });
    std::sort(host_bytes_list.begin(), host_bytes_list.end(), [](const HostBytes& a, const HostBytes& b) { return a.offset < b.offset; });
}
const HostBytes* host_bytes(size_t* count) {
    std::lock_guard lock{ host_bytes_mutex };
    *count = host_bytes_list.size();
    return host_bytes_list.data();
}

void post_foreign(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, bool retry) {
    {
        std::lock_guard lock{ foreign_mutex };
        foreign.push_back({ mq, msg, jam, retry });
    }
    const uint64_t n = ++foreign_count;
    if (n <= 8 || (n & (n - 1)) == 0)
        fprintf(stderr, "[lockstep] a message from outside the game's threads (queue 0x%08X): %llu so far -- the replay may not repeat\n", (unsigned)(uint32_t)mq, (unsigned long long)n);
}

// The idle thread's turn: every other game thread is waiting. First whatever the game's threads posted; if that woke
// one, it runs (pause_self's check_running_queue). Only when nothing can run does the clock move, to the next retrace
// or timer, whichever is first, and that event's messages are handed over.
void idle(RDRAM_ARG1) {
    static bool first = true;
    if (first) {
        first = false;
        fprintf(stderr, "[lockstep] on: the game's clock moves only when the game waits (%s)\n", lockstep_paced ? "paced to the wall clock" : "as fast as it goes");
        start_watch();
    }
    ultramodern::park_game_thread_if_quitting();
    ultramodern::deliver_external_messages(PASS_RDRAM1);
    if (!ultramodern::thread_queue_empty(PASS_RDRAM ultramodern::running_queue)) return;
    {
        std::deque<Foreign> got;
        { std::lock_guard lock{ foreign_mutex }; got.swap(foreign); }
        for (const Foreign& f : got) ultramodern::enqueue_external_message(f.mq, f.msg, f.jam, f.retry);
        if (!got.empty()) {
            ultramodern::deliver_external_messages(PASS_RDRAM1);
            if (!ultramodern::thread_queue_empty(PASS_RDRAM ultramodern::running_queue)) return;
        }
    }
    const uint64_t clock = clock_ticks.load();
    const uint64_t next_vi = (retrace_count.load() + 1) * TICKS_PER_VI;
    uint64_t target = next_vi, timer = 0;
    if (next_timer(PASS_RDRAM &timer) && timer < next_vi) target = timer < clock ? clock : timer;
    pace_to(target);
    clock_ticks.store(target);
    fire_timers(PASS_RDRAM target);
    if (target == next_vi) {
        const uint64_t n = retrace_count.fetch_add(1) + 1;
        if (callbacks.on_retrace != nullptr) callbacks.on_retrace(PASS_RDRAM n);
        retrace_step(PASS_RDRAM1);
    }
    ultramodern::deliver_external_messages(PASS_RDRAM1);
}

}  // namespace ultramodern::lockstep
