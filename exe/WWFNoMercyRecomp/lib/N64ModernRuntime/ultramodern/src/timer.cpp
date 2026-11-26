#include <thread>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <variant>
#include <set>
#include "blockingconcurrentqueue.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/lockstep.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#endif

// Start time for the program
static std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
// Offset of the duration since program start used to calculate the value for osGetTime. 
static int64_t ostime_offset = 0;
// N64 CPU counter ticks per millisecond, at the game's own pace
constexpr uint64_t counter_per_ms = 46'875;
// [arcade] The game's clock, at a speed that can change while it runs: the tests' fast-forward (WCW_SPEED, qa.ps1).
// The clock adds up its own time: each reading is the ticks at the last change of speed plus the real time since then
// times the speed, so a change never makes it jump (the game's timers, its osGetCount and the VI thread's ticks all
// read it). Speed 1 is the game as it always was. ultramodern::set_speed_multiplier changes it (main.cpp: the menus
// at one speed, a match at another).
static std::mutex clock_mutex;
static std::chrono::high_resolution_clock::time_point clock_base_real = start_time;   // (the real time of the last change)
static uint64_t clock_base_ticks = 0;                                                     // (the game's ticks then)
static std::atomic<uint32_t> clock_speed{ [] {
    const char* s = getenv("WCW_SPEED");
    long v = s ? strtol(s, nullptr, 10) : 1;
    return (uint32_t)(v < 1 ? 1 : v > 16 ? 16 : v);
}() };

struct OSTimer {
    PTR(OSTimer) unused1;
    PTR(OSTimer) unused2;
    OSTime interval;
    OSTime timestamp;
    PTR(OSMesgQueue) mq;
    OSMesg msg;
};

struct AddTimerAction {
    PTR(OSTimer) timer;
};

struct RemoveTimerAction {
    PTR(OSTimer) timer;
};

// [wcw fix] Ends the timer thread at shutdown (aki-roadmap#3): it reads OSTimers in rdram, so it
// has to stop before rdram is released. It used to be detached and run forever.
struct ExitTimerAction {};

// [arcade] The clock's speed changed: the wait for the next timer is worked out again (nothing else to do).
struct SpeedChangedAction {};

using Action = std::variant<AddTimerAction, RemoveTimerAction, ExitTimerAction, SpeedChangedAction>;

struct {
    std::thread thread;
    moodycamel::BlockingConcurrentQueue<Action> action_queue{};
} timer_context;

// (real time -> the game's ticks at a speed: more accurate than a floating point timer, and only overflows after years)
static uint64_t duration_to_ticks(std::chrono::high_resolution_clock::duration duration, uint32_t speed) {
    int64_t delta_micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    if (delta_micros < 0) delta_micros = 0;
    return ((uint64_t)delta_micros * counter_per_ms * speed) / 1000;
}

// The game's ticks now (its osGetCount / osGetTime, its timers), on the clock above.
// [replay 2026-11-14] In lockstep the lockstep clock instead (ultramodern/lockstep.hpp): it moves only when the game waits.
uint64_t time_now() {
    if (ultramodern::lockstep::on()) return ultramodern::lockstep::now();
    std::lock_guard lock{ clock_mutex };
    return clock_base_ticks + duration_to_ticks(std::chrono::high_resolution_clock::now() - clock_base_real, clock_speed.load());
}

// The real time at which the game's clock reads `ticks`, at the speed it runs now (a wait for a timer or a VI tick).
std::chrono::high_resolution_clock::time_point ticks_to_timepoint(uint64_t ticks) {
    std::lock_guard lock{ clock_mutex };
    const uint64_t rate = counter_per_ms * clock_speed.load();   // (ticks a real millisecond)
    if (ticks >= clock_base_ticks)
        return clock_base_real + std::chrono::microseconds((ticks - clock_base_ticks) * 1000 / rate);
    return clock_base_real - std::chrono::microseconds((clock_base_ticks - ticks) * 1000 / rate);
}

void timer_thread(RDRAM_ARG1) {
    ultramodern::set_native_thread_name("Timer Thread");
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::VeryHigh);

    // Lambda comparator function to keep the set ordered
    auto timer_sort = [PASS_RDRAM1](PTR(OSTimer) a_, PTR(OSTimer) b_) {
        OSTimer* a = TO_PTR(OSTimer, a_);
        OSTimer* b = TO_PTR(OSTimer, b_);

        // Order by timestamp if the timers have different timestamps
        if (a->timestamp != b->timestamp) {
            return a->timestamp < b->timestamp;
        }

        // If they have the exact same timestamp then order by address instead
        return a < b;
    };

    // Ordered set of timers that are currently active
    std::set<PTR(OSTimer), decltype(timer_sort)> active_timers{timer_sort};
    
    bool exit_requested = false;

    // Lambda to process a timer action to handle adding and removing timers
    auto process_timer_action = [&](const Action& action) {
        // Determine the action type and act on it
        if (const auto* add_action = std::get_if<AddTimerAction>(&action)) {
            active_timers.insert(add_action->timer);
        } else if (const auto* remove_action = std::get_if<RemoveTimerAction>(&action)) {
            active_timers.erase(remove_action->timer);
        } else if (std::get_if<ExitTimerAction>(&action)) {
            exit_requested = true;
        }
    };

    while (true) {
        // Empty the action queue
        Action cur_action;
        while (timer_context.action_queue.try_dequeue(cur_action)) {
            process_timer_action(cur_action);
        }

        // If there's no timer to act on, wait for one to come in from the action queue
        while (active_timers.empty() && !exit_requested) {
            timer_context.action_queue.wait_dequeue(cur_action);
            process_timer_action(cur_action);
        }

        if (exit_requested) {
            return;
        }

        // Get the timer that's closest to running out
        PTR(OSTimer) cur_timer_ = *active_timers.begin();
        OSTimer* cur_timer = TO_PTR(OSTimer, cur_timer_);

        // Remove the timer from the queue (it may get readded if waiting is interrupted)
        active_timers.erase(cur_timer_);

        // Determine how long to wait to reach the timer's timestamp
        auto wait_duration = ticks_to_timepoint(cur_timer->timestamp) - std::chrono::high_resolution_clock::now();

        // Wait for either the duration to complete or a new action to come through
        if (wait_duration.count() >= 0 && timer_context.action_queue.wait_dequeue_timed(cur_action, wait_duration)) {
            // Timer was interrupted by a new action 
            // Add the current timer back to the queue (done first in case the action is to remove this timer)
            active_timers.insert(cur_timer_);
            // Process the new action
            process_timer_action(cur_action);
            if (exit_requested) {
                return;
            }
        }
        else {
            // Waiting for the timer completed, so send the timer's message to its message queue
            ultramodern::enqueue_external_message(cur_timer->mq, cur_timer->msg, false, true);
            // If the timer has a specified interval then reload it with that value
            if (cur_timer->interval != 0) {
                cur_timer->timestamp = cur_timer->interval + time_now();
                active_timers.insert(cur_timer_);
            }
        }
    }
}

void ultramodern::init_timers(RDRAM_ARG1) {
    timer_context.thread = std::thread{ timer_thread, PASS_RDRAM1 };
}

void ultramodern::join_timer_thread() {
    if (timer_context.thread.joinable()) {
        timer_context.action_queue.enqueue(ExitTimerAction{});
        timer_context.thread.join();
    }
}

// [replay 2026-11-14] The timers in lockstep (ultramodern/lockstep.hpp): the same set, in the same order (time, then
// address) as the timer thread keeps, but looked at only at the idle thread's turn and fired there, on the lockstep
// clock. Only game threads touch it, one at a time, so it needs no lock. (The timer thread still runs, and is given
// nothing.)
static uint8_t* lockstep_rdram = nullptr;
namespace {
struct LockstepTimerOrder {
    bool operator()(PTR(OSTimer) a_, PTR(OSTimer) b_) const {
        uint8_t* rdram = lockstep_rdram;
        const OSTimer* a = TO_PTR(OSTimer, a_);
        const OSTimer* b = TO_PTR(OSTimer, b_);
        if (a->timestamp != b->timestamp) return a->timestamp < b->timestamp;
        return a_ < b_;
    }
};
std::set<PTR(OSTimer), LockstepTimerOrder> lockstep_timers;
}

void ultramodern::lockstep::add_timer(PTR(OSTimer) t) {
    // (osSetTimer has just set its time: a timer already in the set is taken out first, as its place may have changed --
    //  the set's order is by the time it had when it went in)
    for (auto it = lockstep_timers.begin(); it != lockstep_timers.end(); ++it) if (*it == t) { lockstep_timers.erase(it); break; }
    lockstep_timers.insert(t);
}
void ultramodern::lockstep::remove_timer(PTR(OSTimer) t) {
    for (auto it = lockstep_timers.begin(); it != lockstep_timers.end(); ++it) if (*it == t) { lockstep_timers.erase(it); break; }
}
bool ultramodern::lockstep::next_timer(RDRAM_ARG uint64_t* when) {
    if (lockstep_timers.empty()) return false;
    *when = TO_PTR(OSTimer, *lockstep_timers.begin())->timestamp;
    return true;
}
void ultramodern::lockstep::fire_timers(RDRAM_ARG uint64_t now) {
    while (!lockstep_timers.empty()) {
        const PTR(OSTimer) t_ = *lockstep_timers.begin();
        OSTimer* t = TO_PTR(OSTimer, t_);
        if (t->timestamp > now) break;
        lockstep_timers.erase(lockstep_timers.begin());
        // (as the timer thread: its message, and back in with its interval when it repeats)
        ultramodern::enqueue_external_message(t->mq, t->msg, false, true);
        if (t->interval != 0) {
            t->timestamp = t->interval + now;
            lockstep_timers.insert(t_);
        }
    }
}

uint32_t ultramodern::get_speed_multiplier() {
    return clock_speed.load();
}

// [arcade] The clock's speed from now on (1..16): the game's time goes on from where it is, at the new rate.
void ultramodern::set_speed_multiplier(uint32_t speed) {
    speed = speed < 1 ? 1 : speed > 16 ? 16 : speed;
    {
        std::lock_guard lock{ clock_mutex };
        if (speed == clock_speed.load()) return;
        const auto now = std::chrono::high_resolution_clock::now();
        clock_base_ticks += duration_to_ticks(now - clock_base_real, clock_speed.load());
        clock_base_real = now;
        clock_speed.store(speed);
    }
    timer_context.action_queue.enqueue(SpeedChangedAction{});
}

// [arcade] The game's clock for the VI thread (events.cpp): its ticks now, and when it will read a given tick.
uint64_t ultramodern::game_ticks_now() { return time_now(); }
std::chrono::high_resolution_clock::time_point ultramodern::game_ticks_to_time(uint64_t ticks) { return ticks_to_timepoint(ticks); }

std::chrono::high_resolution_clock::time_point ultramodern::get_start() {
    return start_time;
}

std::chrono::high_resolution_clock::duration ultramodern::time_since_start() {
    return std::chrono::high_resolution_clock::now() - start_time;
}

extern "C" u32 osGetCount() {
    uint64_t total_count = time_now();

    // Allow for overflows, which is how osGetCount behaves
    return (uint32_t)total_count;
}

extern "C" void osSetCount(u32 count) {
    assert(false);
}

extern "C" OSTime osGetTime() {
    uint64_t total_count = time_now() - ostime_offset;

    return total_count;
}

extern "C" void osSetTime(OSTime t) {
    ostime_offset = time_now() - t;
}

extern "C" int osSetTimer(RDRAM_ARG PTR(OSTimer) t_, OSTime countdown, OSTime interval, PTR(OSMesgQueue) mq, OSMesg msg) {
    OSTimer* t = TO_PTR(OSTimer, t_);

    // Determine the time when this timer will trigger off
    if (countdown == 0) {
        // Set the timestamp based on the interval
        t->timestamp = interval + time_now();
    } else {
        t->timestamp = countdown + time_now();
    }
    t->interval = interval;
    t->mq = mq;
    t->msg = msg;

    // [wcw] identify who registers periodic timers (they keep firing into full queues post-freeze)
    {
        static int n = 0;
        if (n++ < 30) fprintf(stderr, "[wcw][osSetTimer] t=0x%X countdown=%llu interval=%llu mq=0x%X msg=0x%X\n",
            (unsigned)(uint32_t)t_, (unsigned long long)countdown, (unsigned long long)interval,
            (unsigned)(uint32_t)mq, (unsigned)(uint32_t)msg);
    }

    if (ultramodern::lockstep::on()) { lockstep_rdram = rdram; ultramodern::lockstep::add_timer(t_); return 0; }   // [replay 2026-11-14]
    timer_context.action_queue.enqueue(AddTimerAction{ t_ });

    return 0;
}

extern "C" int osStopTimer(RDRAM_ARG PTR(OSTimer) t_) {
    if (ultramodern::lockstep::on()) { ultramodern::lockstep::remove_timer(t_); return 0; }   // [replay 2026-11-14]
    timer_context.action_queue.enqueue(RemoveTimerAction{ t_ });

    // TODO don't blindly return 0 here; requires some response from the timer thread to know what the returned value was
    return 0;
}

#ifdef _WIN32

// The implementations of std::chrono::sleep_until and sleep_for were affected by changing the system clock backwards in older versions
// of Microsoft's STL. This was fixed as of Visual Studio 2022 17.9, but to be safe ultramodern uses Win32 Sleep directly.
void ultramodern::sleep_milliseconds(uint32_t millis) {
    Sleep(millis);
}

void ultramodern::sleep_until(const std::chrono::high_resolution_clock::time_point& time_point) {
    auto time_now = std::chrono::high_resolution_clock::now();
    if (time_point > time_now) {
        long long delta_ms = std::chrono::ceil<std::chrono::milliseconds>(time_point - time_now).count();
        // printf("Sleeping %lld %d ms\n", delta_ms, (uint32_t)delta_ms);
        Sleep(delta_ms);
    }
}

#else

void ultramodern::sleep_milliseconds(uint32_t millis) {
    std::this_thread::sleep_for(std::chrono::milliseconds{millis});
}

void ultramodern::sleep_until(const std::chrono::high_resolution_clock::time_point& time_point) {
    std::this_thread::sleep_until(time_point);
}

#endif
