#include <cstdio>
#include <thread>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <stdexcept>
#include <atomic>
#include <vector>
#include <cassert>
#include <string>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "blockingconcurrentqueue.h"

#include "ultramodern/threads.hpp"
#include "ultramodern/lockstep.hpp"
#include <cstddef>

// Native APIs only used to set thread names for easier debugging
#ifdef _WIN32
#include <Windows.h>
#endif

static ultramodern::threads::callbacks_t threads_callbacks;

void ultramodern::threads::set_callbacks(const callbacks_t& callbacks) {
    threads_callbacks = callbacks;
}

std::string ultramodern::threads::get_game_thread_name(const OSThread* t) {
    if (threads_callbacks.get_game_thread_name == nullptr) {
        return "Game Thread " + std::to_string(t->id);
    }
    return threads_callbacks.get_game_thread_name(t);
}

extern "C" void bootproc();

thread_local bool is_main_thread = false;
// Whether this thread is part of the game (i.e. the start thread or one spawned by osCreateThread)
thread_local bool is_game_thread = false;
thread_local PTR(OSThread) thread_self = NULLPTR;

// [wcw fix] Shutdown ordering (aki-roadmap#3). recomp::start used to release rdram while game
// threads were still alive: the one running kept running, and the idle thread kept cycling
// undeliverable external messages through do_send, which reads the target queue in rdram — an
// access violation at rdram+<queue>+8 (validCount) when quitting mid-match. Now a game thread
// that reaches a scheduler point after quit() parks there for good, and rdram is only released
// once every live game thread has parked. Parked threads end with the process.
extern std::atomic_bool exited;
static std::atomic_int live_game_threads{0};
static std::atomic_int parked_game_threads{0};

void ultramodern::park_game_thread_if_quitting() {
    if (!is_game_thread || !exited.load()) {
        return;
    }
    parked_game_threads.fetch_add(1);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::hours(1));
    }
}

void ultramodern::set_main_thread() {
    ::is_game_thread = true;
    is_main_thread = true;
}

bool ultramodern::is_game_thread() {
    return ::is_game_thread;
}

#if 0
int main(int argc, char** argv) {
    ultramodern::set_main_thread();

    bootproc();
}
#endif

#if 1
void run_thread_function(uint8_t* rdram, uint64_t addr, uint64_t sp, uint64_t arg);
#else
#define run_thread_function(func, sp, arg) func(arg)
#endif

#if defined(_WIN32)
void ultramodern::set_native_thread_name(const std::string& name) {
    std::wstring wname{name.begin(), name.end()};

    HRESULT r;
    r = SetThreadDescription(
        GetCurrentThread(),
        wname.c_str()
    );
}

void ultramodern::set_native_thread_priority(ThreadPriority pri) {
    int nPriority = THREAD_PRIORITY_NORMAL;

    // Convert ThreadPriority to Win32 priority
    switch (pri) {
        case ThreadPriority::Low:
            nPriority = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        case ThreadPriority::Normal:
            nPriority = THREAD_PRIORITY_NORMAL;
            break;
        case ThreadPriority::High:
            nPriority = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        case ThreadPriority::VeryHigh:
            nPriority = THREAD_PRIORITY_HIGHEST;
            break;
        case ThreadPriority::Critical:
            nPriority = THREAD_PRIORITY_TIME_CRITICAL;
            break;
        default:
            throw std::runtime_error("Invalid thread priority!");
            break;
    }
    // [wcw fix] Apply the priority (this call was commented out, so it was computed but never set).
    SetThreadPriority(GetCurrentThread(), nPriority);
}
#elif defined(__linux__)
#include <sys/prctl.h>

void ultramodern::set_native_thread_name(const std::string& name) {
    if (name.length() > 15) {
        // Linux only accepts up to 16 characters including the null terminator for a thread name.
        debug_printf("[Thread] The thread name '%s' will be truncated to 15 characters", name.c_str());
    }

    prctl(PR_SET_NAME, name.c_str());
}

void ultramodern::set_native_thread_priority(ThreadPriority pri) {
    // TODO linux thread priority
    // printf("set_native_thread_priority unimplemented\n");
    // int nPriority = THREAD_PRIORITY_NORMAL;

    // // Convert ThreadPriority to Win32 priority
    // switch (pri) {
    //     case ThreadPriority::Low:
    //         nPriority = THREAD_PRIORITY_BELOW_NORMAL;
    //         break;
    //     case ThreadPriority::Normal:
    //         nPriority = THREAD_PRIORITY_NORMAL;
    //         break;
    //     case ThreadPriority::High:
    //         nPriority = THREAD_PRIORITY_ABOVE_NORMAL;
    //         break;
    //     case ThreadPriority::VeryHigh:
    //         nPriority = THREAD_PRIORITY_HIGHEST;
    //         break;
    //     case ThreadPriority::Critical:
    //         nPriority = THREAD_PRIORITY_TIME_CRITICAL;
    //         break;
    //     default:
    //         throw std::runtime_error("Invalid thread priority!");
    //         break;
    // }
}
#elif defined(__APPLE__)
void ultramodern::set_native_thread_name(const std::string& name) {
    if (name.length() > 15) {
        // Macs seem to only accept up to 16 characters including the null terminator for a thread name.
        debug_printf("[Thread] The thread name '%s' will be truncated to 15 characters", name.c_str());
    }

    pthread_setname_np(name.c_str());
}

void ultramodern::set_native_thread_priority(ThreadPriority pri) {}
#endif

// Game threads. One real thread each, one running at a time, the highest priority that can run: the game's code was
// written for one CPU, and leans on that. A thread hands over at the points it blocks or wakes something higher.
// (The game's made seven and never ends one: the Destroy and Stop paths are for the runtime's exports.)
static std::mutex all_mutex;
static std::vector<UltraThreadContext*> all_threads;       // (only so shutdown can wake them)

static void wait_for_resumed(RDRAM_ARG UltraThreadContext* context) {
    context->running.wait();
    ultramodern::park_game_thread_if_quitting();
    if (TO_PTR(OSThread, ultramodern::this_thread())->context != context) {
        throw ultramodern::thread_terminated{};
    }
}

static void run_next_thread(RDRAM_ARG1) {
    if (ultramodern::thread_queue_empty(PASS_RDRAM ultramodern::running_queue)) {
        throw std::runtime_error("No threads left to run!\n");
    }
    PTR(OSThread) next = ultramodern::thread_queue_pop(PASS_RDRAM ultramodern::running_queue);
    TO_PTR(OSThread, next)->context->running.signal();
}

void ultramodern::run_next_thread_and_wait(RDRAM_ARG1) {
    UltraThreadContext* context = TO_PTR(OSThread, thread_self)->context;
    run_next_thread(PASS_RDRAM1);
    wait_for_resumed(PASS_RDRAM context);
}

void ultramodern::resume_thread_and_wait(RDRAM_ARG OSThread* t) {
    UltraThreadContext* context = TO_PTR(OSThread, thread_self)->context;
    t->context->running.signal();
    wait_for_resumed(PASS_RDRAM context);
}

static void thread_main(RDRAM_ARG PTR(OSThread) self_, PTR(thread_func_t) entrypoint, PTR(void) arg, UltraThreadContext* context) {
    OSThread* self = TO_PTR(OSThread, self_);
    fprintf(stderr, "[thread] id=%d entry=0x%08X pri=%d struct=0x%08X\n", self->id, (unsigned)(uint32_t)entrypoint, self->priority, (unsigned)(uint32_t)self_);
    thread_self = self_;
    is_game_thread = true;
    live_game_threads.fetch_add(1);

    ultramodern::set_native_thread_name(ultramodern::threads::get_game_thread_name(self));
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::High);

    context->initialized.signal();

    try {
        wait_for_resumed(PASS_RDRAM context);
        run_thread_function(PASS_RDRAM entrypoint, self->sp, arg);
    } catch (ultramodern::thread_terminated&) {
    }

    {
        std::lock_guard lock{ all_mutex };
        all_threads.erase(std::remove(all_threads.begin(), all_threads.end(), context), all_threads.end());
    }
    if (self->context == context) {
        self->context = nullptr;
        run_next_thread(PASS_RDRAM1);
    }
    live_game_threads.fetch_sub(1);
    ultramodern::cleanup_thread(context);
}

extern "C" void osStartThread(RDRAM_ARG PTR(OSThread) t_) {
    OSThread* t = TO_PTR(OSThread, t_);
    if (thread_self) {
        ultramodern::schedule_running_thread(PASS_RDRAM t_);
        ultramodern::check_running_queue(PASS_RDRAM1);
    }
    else {                                                 // (the first, from the boot)
        t->state = OSThreadState::QUEUED;
        t->context->running.signal();
    }
}

extern "C" void osCreateThread(RDRAM_ARG PTR(OSThread) t_, OSId id, PTR(thread_func_t) entrypoint, PTR(void) arg, PTR(void) sp, OSPri pri) {
    OSThread* t = TO_PTR(OSThread, t_);
    t->next = NULLPTR;
    t->queue = NULLPTR;
    t->priority = pri;
    t->id = id;
    t->state = OSThreadState::STOPPED;
    t->sp = sp - 0x10;                                     // (the first stack frame)

    UltraThreadContext* context = new UltraThreadContext{};
    t->context = context;
    // [replay 2026-11-14] (a PC address kept in the game's memory: different every run, so a hash of all the memory
    //  leaves it out -- nm_replay.cpp's "ram")
    if (ultramodern::lockstep::on()) ultramodern::lockstep::note_host_bytes((uint32_t)t_ + (uint32_t)offsetof(OSThread, context), (uint32_t)sizeof(t->context));
    {
        std::lock_guard lock{ all_mutex };
        all_threads.push_back(context);
    }
    context->host_thread = std::thread{ thread_main, PASS_RDRAM t_, entrypoint, arg, context };
    context->initialized.wait();
}

extern "C" void osStopThread(RDRAM_ARG PTR(OSThread) t_) {
    if (t_ == NULLPTR || t_ == thread_self) {
        ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
    }
    else {
        assert(false);
    }
}

extern "C" void osDestroyThread(RDRAM_ARG PTR(OSThread) t_) {
    if (t_ == NULLPTR || t_ == thread_self) {
        throw ultramodern::thread_terminated{};
    }
    OSThread* t = TO_PTR(OSThread, t_);
    if (t->state != OSThreadState::STOPPED && t->queue != NULLPTR) {
        ultramodern::thread_queue_remove(PASS_RDRAM t->queue, t_);
    }
    UltraThreadContext* context = t->context;
    t->context = nullptr;
    if (context != nullptr) {
        context->running.signal();                         // (it wakes, sees it's not its struct's, and ends)
    }
}

extern "C" void osSetThreadPri(RDRAM_ARG PTR(OSThread) t_, OSPri pri) {
    if (t_ == NULLPTR) {
        t_ = thread_self;
    }
    OSThread* t = TO_PTR(OSThread, t_);
    if (t->priority == pri) {
        return;
    }
    t->priority = pri;
    if (t_ != thread_self && t->state != OSThreadState::STOPPED && t->queue != NULLPTR) {
        PTR(PTR(OSThread)) queue = t->queue;               // (back in at its new place)
        ultramodern::thread_queue_remove(PASS_RDRAM queue, t_);
        ultramodern::thread_queue_insert(PASS_RDRAM queue, t_);
    }
    ultramodern::check_running_queue(PASS_RDRAM1);
}

extern "C" OSPri osGetThreadPri(RDRAM_ARG PTR(OSThread) t) {
    return TO_PTR(OSThread, t == NULLPTR ? thread_self : t)->priority;
}

extern "C" OSId osGetThreadId(RDRAM_ARG PTR(OSThread) t) {
    return TO_PTR(OSThread, t == NULLPTR ? thread_self : t)->id;
}

PTR(OSThread) ultramodern::this_thread() {
    return thread_self;
}

static std::thread thread_cleaner_thread;
static moodycamel::BlockingConcurrentQueue<UltraThreadContext*> deleted_threads{};

static void thread_cleaner_func() {
    using namespace std::chrono_literals;
    while (!exited) {
        UltraThreadContext* to_delete;
        if (deleted_threads.wait_dequeue_timed(to_delete, 10ms)) {
            to_delete->host_thread.join();
            delete to_delete;
        }
    }
}

void ultramodern::init_thread_cleanup() {
    thread_cleaner_thread = std::thread{ thread_cleaner_func };
}

void ultramodern::cleanup_thread(UltraThreadContext* context) {
    deleted_threads.enqueue(context);
}

void ultramodern::join_thread_cleaner_thread() {
    thread_cleaner_thread.join();
}

// Shutdown: every game thread stops at its next scheduling point; this wakes the blocked ones so they get there. Game
// memory can only be released when it returns true.
bool ultramodern::park_game_threads(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        {
            std::lock_guard lock{ all_mutex };
            for (UltraThreadContext* context : all_threads) {
                context->running.signal();
            }
        }
        ultramodern::wake_external_message_waiters(live_game_threads.load());
        const int live = live_game_threads.load(), parked = parked_game_threads.load();
        if (parked >= live) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            fprintf(stderr, "[thread] shutdown: %d of %d game threads stopped; keeping game memory\n", parked, live);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
