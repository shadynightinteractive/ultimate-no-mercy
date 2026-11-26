#include <thread>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <variant>
#include <unordered_map>
#include <utility>
#include <mutex>
#include <queue>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#include "blockingconcurrentqueue.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/extensions.h"
#include "ultramodern/perf.hpp"

#include "ultramodern/rsp.hpp"
#include "ultramodern/renderer_context.hpp"
#include "ultramodern/lockstep.hpp"

static ultramodern::events::callbacks_t events_callbacks{};

void ultramodern::events::set_callbacks(const ultramodern::events::callbacks_t& callbacks) {
    events_callbacks = callbacks;
}

struct SpTaskAction {
    OSTask task;
};

struct ScreenUpdateAction {
    double wcw_enqueue_ms; // [wcw] DIAGNOSTIC: set at VI-thread enqueue for latency measurement
    ultramodern::renderer::ViRegs regs;
};

struct UpdateConfigAction {
};

struct DummyWorkloadAction {
    int32_t fb_address;
};

using Action = std::variant<SpTaskAction, ScreenUpdateAction, UpdateConfigAction, DummyWorkloadAction>;

struct ViState {
    const OSViMode* mode;
    PTR(void) framebuffer;
    PTR(OSMesg) mq;
    OSMesg msg;
    uint32_t state;
    uint32_t control;
    int retrace_count = 1;
};

#define VI_STATE_BLACK 0x20
#define VI_STATE_REPEATLINE 0x40

static struct {
    struct {
        std::thread thread;
        int cur_state;
        int field;
        ViState states[2];
        ultramodern::renderer::ViRegs regs;
        ultramodern::renderer::ViRegs update_screen_regs;

        ViState* get_next_state() {
            return &states[cur_state ^ 1];
        }
        ViState* get_cur_state() {
            return &states[cur_state];
        }
        void update_vi() {
            ViState* next_state = get_next_state();
            const OSViMode* next_mode = next_state->mode;
            if (next_mode == nullptr) {
                static bool warned = false;
                if (!warned) { warned = true; fprintf(stderr, "[wcw][vi] update_vi: next_state->mode is NULL (cur_state=%d field=%d game_started=%d) - skipping\n", cur_state, field, (int)ultramodern::is_game_started()); }
                return;
            }
            const OSViCommonRegs* common_regs = &next_mode->comRegs;
            const OSViFieldRegs* field_regs = &next_mode->fldRegs[field];
            PTR(void) framebuffer = osVirtualToPhysical(next_state->framebuffer);
            PTR(void) origin = framebuffer + field_regs->origin;

            // Process the VI state flags.
            uint32_t hStart = common_regs->hStart;
            if (next_state->state & VI_STATE_BLACK) {
                hStart = 0;
            }

            uint32_t yScale = field_regs->yScale;
            if (next_state->state & VI_STATE_REPEATLINE) {
                yScale = 0;
                origin = framebuffer;
            }

            // TODO implement osViFade

            // Update VI registers.
            regs.VI_ORIGIN_REG = origin;
            regs.VI_WIDTH_REG = common_regs->width;
            regs.VI_TIMING_REG = common_regs->burst;
            regs.VI_V_SYNC_REG = common_regs->vSync;
            regs.VI_H_SYNC_REG = common_regs->hSync;
            regs.VI_LEAP_REG = common_regs->leap;
            regs.VI_H_START_REG = hStart;
            regs.VI_V_START_REG = field_regs->vStart; // TODO implement osViExtendVStart
            regs.VI_V_BURST_REG = field_regs->vBurst;
            regs.VI_INTR_REG = field_regs->vIntr;
            regs.VI_X_SCALE_REG = common_regs->xScale; // TODO implement osViSetXScale
            regs.VI_Y_SCALE_REG = yScale; // TODO implement osViSetYScale
            regs.VI_STATUS_REG = next_state->control;
            
            // Swap VI states.
            cur_state ^= 1;
            *get_next_state() = *get_cur_state();
        }
    } vi;
    struct {
        std::thread gfx_thread;
        std::thread task_thread;
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } sp;
    struct {
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } dp;
    struct {
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } ai;
    struct {
        PTR(OSMesgQueue) mq = NULLPTR;
        OSMesg msg = (OSMesg)0;
    } si;
    // The same message queue may be used for multiple events, so share a mutex for all of them
    std::mutex message_mutex;
    uint8_t* rdram;
    moodycamel::BlockingConcurrentQueue<Action> action_queue{};
    moodycamel::BlockingConcurrentQueue<OSTask*> sp_task_queue{};
    moodycamel::ConcurrentQueue<OSThread*> deleted_threads{};
} events_context{};

ultramodern::renderer::ViRegs* ultramodern::renderer::get_vi_regs() {
    return &events_context.vi.update_screen_regs;
}

extern "C" void osSetEventMesg(RDRAM_ARG OSEvent event_id, PTR(OSMesgQueue) mq_, OSMesg msg) {
    std::lock_guard lock{ events_context.message_mutex };
    fprintf(stderr, "[wcw][osSetEventMesg] event=%d mq=0x%X msg=0x%X\n", (int)event_id, (unsigned)mq_, (unsigned)msg);

    switch (event_id) {
        case OS_EVENT_SP:
            events_context.sp.msg = msg;
            events_context.sp.mq = mq_;
            break;
        case OS_EVENT_DP:
            events_context.dp.msg = msg;
            events_context.dp.mq = mq_;
            break;
        case OS_EVENT_AI:
            events_context.ai.msg = msg;
            events_context.ai.mq = mq_;
            break;
        case OS_EVENT_SI:
            events_context.si.msg = msg;
            events_context.si.mq = mq_;
    }
}

extern "C" void osViSetEvent(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, u32 retrace_count) {
    std::lock_guard lock{ events_context.message_mutex };
    // [wcw fix] the retrace event registration is global on real hardware (latest osViSetEvent
    // wins), but the ViStates here are double-buffered and never copy to each other — writing
    // only next_state leaves the stale registration live on the other parity. WCW re-registers
    // its retrace (new mq/msg/rate) at the end of its attract sequence; the stale parity then
    // starved the new one via the shared remaining_retraces counter and the game deadlocked.
    ViState* next_state = events_context.vi.get_next_state();
    next_state->mq = mq_;
    next_state->msg = msg;
    next_state->retrace_count = retrace_count;
    ViState* cur_state = events_context.vi.get_cur_state();
    cur_state->mq = mq_;
    cur_state->msg = msg;
    cur_state->retrace_count = retrace_count;
    fprintf(stderr, "[wcw][osViSetEvent] mq=0x%X msg=0x%X retrace_count=%u\n",
        (unsigned)(uint32_t)mq_, (unsigned)(uint32_t)msg, retrace_count);
}

uint64_t total_vis = 0;


extern std::atomic_bool exited;
extern moodycamel::LightweightSemaphore graphics_shutdown_ready;
// [qa12x 2026-11-17] The VI thread's retraces given, and those it let go by (woken too late for them): the tests' pace line
// (main.cpp WCW_QA_PERF) reads them. Counting only.
std::atomic<uint64_t> wcw_vi_given{ 0 }, wcw_vi_skipped{ 0 };

void set_dummy_vi(bool odd);

void vi_thread_func() {
    ultramodern::set_native_thread_name("VI Thread");
    // This thread should be prioritized over every other thread in the application, as it's what allows
    // the game to generate new audio and gfx lists.
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::Critical);
    using namespace std::chrono_literals;

    // [replay 2026-11-14] In lockstep the retraces come from the idle thread's turn (lockstep_retrace_step below): this
    // thread only waits for the end.
    if (ultramodern::lockstep::on()) {
        while (!exited) std::this_thread::sleep_for(10ms);
        return;
    }

    int remaining_retraces = 1;

    while (!exited) {
        // Determine the next VI time (more accurate than adding 16ms each VI interrupt)
        // [arcade] on the game's clock (timer.cpp), which can run fast in the tests: a VI every 1/60 s of the game's time
        constexpr uint64_t ticks_per_vi = 46'875'000ull / 60;
        auto next = ultramodern::game_ticks_to_time(total_vis * ticks_per_vi);
        //if (next > std::chrono::high_resolution_clock::now()) {
        //    printf("Sleeping for %" PRIu64 " us to get from %" PRIu64 " us to %" PRIu64 " us \n",
        //        (next - std::chrono::high_resolution_clock::now()) / 1us,
        //        (std::chrono::high_resolution_clock::now() - events_context.start) / 1us,
        //        (next - events_context.start) / 1us);
        //} else {
        //    printf("No need to sleep\n");
        //}
        // Detect if there's more than a second to wait and wait a fixed amount instead for the next VI if so, as that usually means the system clock went back in time.
        if (std::chrono::floor<std::chrono::seconds>(next - std::chrono::high_resolution_clock::now()) > 1s) {
            // printf("Skipping the next VI wait\n");
            next = std::chrono::high_resolution_clock::now();
        }
        ultramodern::sleep_until(next);
        // Calculate how many VIs have passed (on the game's clock)
        uint64_t new_total_vis = ultramodern::game_ticks_now() / ticks_per_vi + 1;
        if (new_total_vis > total_vis + 1) {
            //printf("Skipped % " PRId64 " frames in VI interupt thread!\n", new_total_vis - total_vis - 1);
            // [qa12x 2026-11-17] (counted for the tests' pace line, WCW_QA_PERF; and in fast-forward with WCW_VI_CATCH_UP=1,
            //  the retraces that went by while this thread slept are each still given, one after another, as long as it is
            //  less than a quarter of a second behind: Windows' Sleep wakes in whole milliseconds, a retrace at 12x is every
            //  1.4 ms, and dropping the ones in between held a 12x match to about 7x. Players never set either: nothing changes.)
            static const bool catch_up = [] { const char* e = getenv("WCW_VI_CATCH_UP"); return e && e[0] == '1'; }();
            const uint32_t speed = ultramodern::get_speed_multiplier();
            if (catch_up && speed > 1 && new_total_vis - total_vis <= 15ull * speed) {
                new_total_vis = total_vis + 1;
            } else {
                wcw_vi_skipped.fetch_add(new_total_vis - total_vis - 1, std::memory_order_relaxed);
            }
        }
        wcw_vi_given.fetch_add(1, std::memory_order_relaxed);
        total_vis = new_total_vis;

        // [arcade] (fast-forward: this tick shows on the screen, one in n -- see below)
        static uint64_t ticks_since_start = 0;
        const bool screen_tick = (ticks_since_start++ % ultramodern::get_speed_multiplier()) == 0;
        // If the game hasn't started yet, set a dummy VI mode and origin.
        if (!ultramodern::is_game_started() && screen_tick) {
            static bool odd = false;
            set_dummy_vi(odd);
            odd = !odd;

            events_context.action_queue.enqueue(DummyWorkloadAction{events_context.vi.get_next_state()->framebuffer});
        }

        // Queue a screen update for the graphics thread with the current VI register state.
        // Doing this before the VI update is equivalent to updating the screen after the previous frame's scanout finished.
        // [arcade] In fast-forward (WCW_SPEED n > 1) only every n-th tick: the screen still gets 60 a second. With one per
        // tick the graphics thread got n times what a 60 Hz present can take, its queue grew without end, the window
        // stopped answering and the tests' pictures came out black (2026-11-06).
        if (screen_tick)
        events_context.action_queue.enqueue(ScreenUpdateAction{
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0,
            events_context.vi.regs });

        // Update VI registers and swap VI modes.
        {   // The same lock as osViSwapBuffer. Without it a swap landing between the states' flip and the copy in update_vi
            // went into the state the copy then overwrites: the swap was lost, and the game's frame task (func_800012B0)
            // waited for ever to get its buffer back: the "1% freeze" of 2026-10-27 to 11-04.
            std::lock_guard lock{ events_context.message_mutex };
            events_context.vi.update_vi();
        }



        // If the game has started, handle sending VI and AI events.
        if (ultramodern::is_game_started()) {
            remaining_retraces--;
            
            std::lock_guard lock{ events_context.message_mutex };
            ViState* cur_state = events_context.vi.get_cur_state();
            // [wcw fix] was `== 0` with an unclamped reload: if cur_state->retrace_count ever reads
            // 0 once (e.g. mid VI-state swap during a mode change / osViBlack fade), the counter
            // reloads to 0, goes negative, and `== 0` never fires again — retrace events stop
            // permanently and every game thread eventually starves (frozen game, live process).
            // WCW hit this deterministically at the end of its attract sequence.
            if (remaining_retraces <= 0) {
                if (cur_state->mq != NULLPTR) {
                    // Send a message to the VI queue, and do not set it to be requeued if the queue was full.
                    // The worst case scenario is that the game misses a VI message and has to wait a little longer for the next.
                    ultramodern::enqueue_external_message(cur_state->mq, cur_state->msg, false, false);
                }
                remaining_retraces = (cur_state->retrace_count > 0) ? cur_state->retrace_count : 1;
            }
            if (events_context.ai.mq != NULLPTR) {
                // Send a message to the VI queue, and do not set it to be requeued if the queue was full for the same reason as the VI message above.
                ultramodern::enqueue_external_message(events_context.ai.mq, events_context.ai.msg, false, false);
            }
        }

        if (events_callbacks.vi_callback != nullptr) {
            events_callbacks.vi_callback();
        }
    }
}

// [replay 2026-11-14] A retrace in lockstep: what vi_thread_func does at each tick after its wait, step for step, done
// from the idle thread's turn (lockstep.cpp idle) on the lockstep clock. Its messages go to the game's own queue of
// posted messages (this is a game thread), handed over in this order right after.
void ultramodern::lockstep::retrace_step(RDRAM_ARG1) {
    static int remaining_retraces = 1;
    static uint64_t ticks_since_start = 0;
    total_vis = ultramodern::lockstep::retraces();
    const bool screen_tick = (ticks_since_start++ % ultramodern::get_speed_multiplier()) == 0;
    if (!ultramodern::is_game_started() && screen_tick) {
        static bool odd = false;
        set_dummy_vi(odd);
        odd = !odd;
        events_context.action_queue.enqueue(DummyWorkloadAction{events_context.vi.get_next_state()->framebuffer});
    }
    if (screen_tick)
        events_context.action_queue.enqueue(ScreenUpdateAction{
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0,
            events_context.vi.regs });
    {
        std::lock_guard lock{ events_context.message_mutex };
        events_context.vi.update_vi();
    }
    if (ultramodern::is_game_started()) {
        remaining_retraces--;
        std::lock_guard lock{ events_context.message_mutex };
        ViState* cur_state = events_context.vi.get_cur_state();
        if (remaining_retraces <= 0) {
            if (cur_state->mq != NULLPTR) {
                ultramodern::enqueue_external_message(cur_state->mq, cur_state->msg, false, false);
            }
            remaining_retraces = (cur_state->retrace_count > 0) ? cur_state->retrace_count : 1;
        }
        if (events_context.ai.mq != NULLPTR) {
            ultramodern::enqueue_external_message(events_context.ai.mq, events_context.ai.msg, false, false);
        }
    }
    if (events_callbacks.vi_callback != nullptr) {
        events_callbacks.vi_callback();
    }
}

// [wcw] DIAGNOSTIC counters: completion-message accounting (find where SP/DP dones get lost).

void sp_complete() {
    uint8_t* rdram = events_context.rdram;
    std::lock_guard lock{ events_context.message_mutex };
    ultramodern::enqueue_external_message(events_context.sp.mq, events_context.sp.msg, false, true);
}

void dp_complete() {
    uint8_t* rdram = events_context.rdram;
    std::lock_guard lock{ events_context.message_mutex };
    ultramodern::enqueue_external_message(events_context.dp.mq, events_context.dp.msg, false, true);
}

void task_thread_func(uint8_t* rdram, moodycamel::LightweightSemaphore* thread_ready) {
    ultramodern::set_native_thread_name("SP Task Thread");
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::Normal);

    // Notify the caller thread that this thread is ready.
    thread_ready->signal();

    while (true) {
        // Wait until an RSP task has been sent
        OSTask* task;
        events_context.sp_task_queue.wait_dequeue(task);

        if (task == nullptr) {
            return;
        }

        // [wcw2k] DIAGNOSTIC (WCW2K_TASKWATCH=1): WM2000's corruption family (TCB wipes,
        // music-slot garbage, thread-6 stack slots — float debris) lives in the audio
        // heap around 0x80080000-0x80084000. The RSP task thread runs CONCURRENTLY with
        // the cooperative game threads, so scheduler-touch watchers mis-attribute its
        // writes. Snapshot the hot windows around each task run to convict or acquit
        // the recompiled audio ucode directly.
        static const bool taskwatch = getenv("WCW2K_TASKWATCH") != nullptr;
        static uint32_t snap[0x60 / 4];
        static uint32_t tasknum = 0;
        constexpr uint32_t tcb_off = 0x807C0, frame_off = 0x83900;
        if (taskwatch) {
            memcpy(snap, rdram + tcb_off, 0x30);
            memcpy(snap + 0x30 / 4, rdram + frame_off, 0x30);
        }
        tasknum++;

        const bool perf_on = ultramodern::perf::enabled();
        const int64_t perf_start = perf_on ? ultramodern::perf::now_ns() : 0;
        if (!ultramodern::rsp::run_task(PASS_RDRAM task)) {
            fprintf(stderr, "Failed to execute task type: %" PRIu32 "\n", task->t.type);
            ULTRAMODERN_QUICK_EXIT();
        }

        if (perf_on) { // [wcw perf] WCW_PERF_LOG
            ultramodern::perf::phase((task->t.type == M_AUDTASK) ? "rsp_audio" : "rsp_task", perf_start, ultramodern::perf::now_ns());
        }

        if (taskwatch) {
            const uint32_t* now_tcb = (const uint32_t*)(rdram + tcb_off);
            const uint32_t* now_frm = (const uint32_t*)(rdram + frame_off);
            for (uint32_t i = 0; i < 0x30 / 4; i++) {
                if (now_tcb[i] != snap[i]) {
                    fprintf(stderr, "[wcw2k][taskwatch] task#%u type=%u CHANGED 0x%08X: %08X -> %08X\n",
                        tasknum, (unsigned)task->t.type, 0x80000000u + tcb_off + i * 4, snap[i], now_tcb[i]);
                }
                if (now_frm[i] != snap[0x30 / 4 + i]) {
                    fprintf(stderr, "[wcw2k][taskwatch] task#%u type=%u CHANGED 0x%08X: %08X -> %08X\n",
                        tasknum, (unsigned)task->t.type, 0x80000000u + frame_off + i * 4, snap[0x30 / 4 + i], now_frm[i]);
                }
            }
        }

        // Tell the game that the RSP has completed
        sp_complete();
    }
}

std::atomic_uint32_t display_refresh_rate = 60;
std::atomic<float> resolution_scale = 1.0f;

uint32_t ultramodern::get_target_framerate(uint32_t original) {
    auto& config = ultramodern::renderer::get_graphics_config();

    switch (config.rr_option) {
        case ultramodern::renderer::RefreshRate::Original:
        default:
            return original;
        case ultramodern::renderer::RefreshRate::Manual:
            return config.rr_manual_value;
        case ultramodern::renderer::RefreshRate::Display:
            return display_refresh_rate.load();
    }
}

uint32_t ultramodern::get_display_refresh_rate() {
    return display_refresh_rate.load();
}

float ultramodern::get_resolution_scale() {
    return resolution_scale.load();
}

void ultramodern::trigger_config_action() {
    events_context.action_queue.enqueue(UpdateConfigAction{});
}

std::atomic<ultramodern::renderer::SetupResult> renderer_setup_result = ultramodern::renderer::SetupResult::Success;
std::atomic<ultramodern::renderer::GraphicsApi> renderer_chosen_api = ultramodern::renderer::GraphicsApi::Auto;

void gfx_thread_func(uint8_t* rdram, moodycamel::LightweightSemaphore* thread_ready, ultramodern::renderer::WindowHandle window_handle) {
    bool enabled_instant_present = false;
    using namespace std::chrono_literals;

    ultramodern::set_native_thread_name("Gfx Thread");
    ultramodern::set_native_thread_priority(ultramodern::ThreadPriority::Normal);

    auto old_config = ultramodern::renderer::get_graphics_config();

    auto renderer_context = ultramodern::renderer::create_render_context(rdram, window_handle, ultramodern::renderer::get_graphics_config().developer_mode);

    renderer_chosen_api.store(renderer_context->get_chosen_api());
    if (!renderer_context->valid()) {
        renderer_setup_result.store(renderer_context->get_setup_result());
        // Notify the caller thread that this thread is ready.
        thread_ready->signal();
        return;
    }

    if (events_callbacks.gfx_init_callback != nullptr) {
        events_callbacks.gfx_init_callback();
    }

    ultramodern::rsp::init();

    // Notify the caller thread that this thread is ready.
    thread_ready->signal();

    while (!exited) {
        // Try to pull an action from the queue
        Action first_action;
        if (events_context.action_queue.wait_dequeue_timed(first_action, 1ms)) {
            // [wcw fix] Drain the whole queue and drop STALE ScreenUpdateActions (keep only the
            // newest). Each ScreenUpdateAction is a snapshot of the VI regs at one 60Hz tick; a
            // standing backlog in this queue (SpTask display-list processing shares the thread)
            // otherwise delays scanout by that backlog — measured at 5-6 VIs (~90ms), enough that
            // the presented framebuffer overlapped the game re-clearing/redrawing it for its NEXT
            // frame -> 1 of 3 presents was pure black (constant visible flicker). Presenting only
            // the freshest VI snapshot bounds scanout latency to ~1 action regardless of backlog.
            thread_local std::vector<Action> action_batch;
            action_batch.clear();
            action_batch.push_back(std::move(first_action));
            {
                Action extra;
                while (events_context.action_queue.try_dequeue(extra)) {
                    action_batch.push_back(std::move(extra));
                }
            }
            size_t last_screen_update = SIZE_MAX;
            for (size_t i = 0; i < action_batch.size(); i++) {
                if (std::get_if<ScreenUpdateAction>(&action_batch[i]) != nullptr) {
                    last_screen_update = i;
                }
            }
            for (size_t action_index = 0; action_index < action_batch.size(); action_index++) {
                Action &action = action_batch[action_index];
                if ((std::get_if<ScreenUpdateAction>(&action) != nullptr) && (action_index != last_screen_update)) {
                    continue; // stale screen update — a newer VI snapshot is in this batch
                }
            // Determine the action type and act on it
            if (const auto* task_action = std::get_if<SpTaskAction>(&action)) {
                // [wcw fix] sp_complete() used to be signaled HERE, before send_dl — telling the
                // game the RSP consumed its inputs before RT64 actually read them. On hardware,
                // SP-done is the license to rewrite RSP inputs, and the AKI engine takes it: its
                // pose pass rewrites the double-buffered bone matrices as soon as SP-done arrives,
                // which can race this thread's DL interpretation and feed torn/stale
                // gSPForceMatrix loads (WWF No Mercy's per-wrestler matrices are the exposed
                // surface; static-matrix menus are immune). Signal sp_complete() AFTER send_dl
                // instead — the hardware contract — so inputs are fully ingested before the game
                // may rewrite them. (The old comment predicted exactly this: "If this causes
                // issues then the logic can be replaced with responding to yield requests.")
                // NOTE: this is a correctness hardening of the SP-input contract, NOT the cause
                // of the No Mercy exploded-wrestler bug — that was a recompiler miscompile
                // (misplaced self-entry label, fixed by tools/fix_selfentry.py). This reorder is
                // the defensive other half; kept because the old ordering is simply wrong on HW.
                ultramodern::measure_input_latency();

                PTR(u64) displaylist = task_action->task.t.data_ptr;
                ultramodern::extensions::on_displaylist_submitted(displaylist);

                [[maybe_unused]] auto renderer_start = std::chrono::high_resolution_clock::now();
                const bool perf_on = ultramodern::perf::enabled();
                const int64_t perf_start = perf_on ? ultramodern::perf::now_ns() : 0;
                renderer_context->send_dl(&task_action->task);
                if (perf_on) { // [wcw perf] WCW_PERF_LOG
                    ultramodern::perf::phase("dl_submit", perf_start, ultramodern::perf::now_ns());
                }
                sp_complete(); // [wcw fix] see above: RSP inputs are consumed only now.
                [[maybe_unused]] auto renderer_end = std::chrono::high_resolution_clock::now();

                dp_complete();
                // TODO hook the parsed event up to the actual parsing point when a callback is added to RT64.
                ultramodern::extensions::on_displaylist_parsed(displaylist);
                ultramodern::extensions::on_displaylist_completed(displaylist);
                // printf("Renderer ProcessDList time: %d us\n", static_cast<u32>(std::chrono::duration_cast<std::chrono::microseconds>(renderer_end - renderer_start).count()));
            }
            else if (const auto* screen_update_action = std::get_if<ScreenUpdateAction>(&action)) {
                events_context.vi.update_screen_regs = screen_update_action->regs;
                renderer_context->update_screen();
                display_refresh_rate = renderer_context->get_display_framerate();
                resolution_scale = renderer_context->get_resolution_scale();
            }
            else if (const auto* config_action = std::get_if<UpdateConfigAction>(&action)) {
                (void)config_action;
                auto new_config = ultramodern::renderer::get_graphics_config();
                if (renderer_context->update_config(old_config, new_config)) {
                    old_config = new_config;
                }
            }
            else if (const auto* dummy_workload_action = std::get_if<DummyWorkloadAction>(&action)) {
                renderer_context->send_dummy_workload(dummy_workload_action->fb_address);
            }
            } // [wcw fix] end of drained-batch loop
        }
    }

    graphics_shutdown_ready.wait();
    renderer_context->shutdown();
}

#define VI_CTRL_TYPE_16             0x00002
#define VI_CTRL_TYPE_32             0x00003
#define VI_CTRL_GAMMA_DITHER_ON     0x00004
#define VI_CTRL_GAMMA_ON            0x00008
#define VI_CTRL_DIVOT_ON            0x00010
#define VI_CTRL_SERRATE_ON          0x00040
#define VI_CTRL_ANTIALIAS_MASK      0x00300
#define VI_CTRL_ANTIALIAS_MODE_1    0x00100
#define VI_CTRL_ANTIALIAS_MODE_2    0x00200
#define VI_CTRL_ANTIALIAS_MODE_3    0x00300
#define VI_CTRL_PIXEL_ADV_MASK      0x01000
#define VI_CTRL_PIXEL_ADV_1         0x01000
#define VI_CTRL_PIXEL_ADV_2         0x02000
#define VI_CTRL_PIXEL_ADV_3         0x03000
#define VI_CTRL_DITHER_FILTER_ON    0x10000

// [relclean 2026-11-19] The stand-in mode is libultra's NTSC LAN1 mode, the N64 SDK's data (every AKI ROM holds the same 80
// bytes): it is no longer built here from its numbers but read from the player's ROM at start-up, before this thread starts
// (main.cpp arcade_vi_mode_or_quit -> set_dummy_vi_mode; arcade_vi_mode.h says where it is in each ROM). Without it the
// program stops here rather than show a mode of zeros.
static OSViMode dummy_mode{};
static std::atomic_bool dummy_mode_set{ false };

void ultramodern::set_dummy_vi_mode(const OSViMode& mode) {
    dummy_mode = mode;
    dummy_mode_set.store(true);
}

void set_dummy_vi(bool odd) {
    if (!dummy_mode_set.load()) {
        fprintf(stderr, "[vi] the stand-in video mode was never read from the ROM (set_dummy_vi_mode)\n"); fflush(stderr);
        abort();
    }
    ViState* next_state = events_context.vi.get_next_state();
    next_state->mode = &dummy_mode;
    next_state->control = next_state->mode->comRegs.ctrl;
    // Set up a dummy framebuffer.
    next_state->framebuffer = 0x80700000;
    if (odd) {
        next_state->framebuffer += 0x25800;
    }
}

// [wcw] DIAGNOSTIC: per-second game-frame counter, reported in the [wcw][health] line
// (mesgqueue.cpp). osViSwapBuffer fires once per rendered game frame, so a drop in this
// rate is a direct measure of "the game slowed down" regardless of host present rate.
std::atomic_int wcw_viswaps{0};
std::atomic_int wcw_viswaps_total{0};   // [arcade] every game frame since boot (the freeze watchdog, crash_report.cpp)

extern "C" void osViSwapBuffer(RDRAM_ARG PTR(void) frameBufPtr) {
    std::lock_guard lock{ events_context.message_mutex };
    wcw_viswaps.fetch_add(1);
    wcw_viswaps_total.fetch_add(1);
    if (ultramodern::perf::enabled()) { // [wcw perf] WCW_PERF_LOG: game_frame / game_busy
        ultramodern::perf::game_frame_boundary();
    }
    // [wcw] DIAGNOSTIC: log swaps — if this never fires, VI scans out nothing (black screen)
    // regardless of what the RDP rendered.
    { static int n = 0; if (n++ < 30) fprintf(stderr, "[wcw][viswap#%d] fb=0x%08X\n", n, (unsigned)frameBufPtr); }
    events_context.vi.get_next_state()->framebuffer = frameBufPtr;
}

extern "C" void osViSetMode(RDRAM_ARG PTR(OSViMode) mode_) {
    std::lock_guard lock{ events_context.message_mutex };
    OSViMode* mode = TO_PTR(OSViMode, mode_);
    ViState* next_state = events_context.vi.get_next_state();
    next_state->mode = mode;
    next_state->control = next_state->mode->comRegs.ctrl;
}

#define OS_VI_GAMMA_ON          0x0001
#define OS_VI_GAMMA_OFF         0x0002
#define OS_VI_GAMMA_DITHER_ON   0x0004
#define OS_VI_GAMMA_DITHER_OFF  0x0008
#define OS_VI_DIVOT_ON          0x0010
#define OS_VI_DIVOT_OFF         0x0020
#define OS_VI_DITHER_FILTER_ON  0x0040
#define OS_VI_DITHER_FILTER_OFF 0x0080

extern "C" void osViSetSpecialFeatures(uint32_t func) {
    std::lock_guard lock{ events_context.message_mutex };
    ViState* next_state = events_context.vi.get_next_state();
    uint32_t* control_out = &next_state->control;
    if ((func & OS_VI_GAMMA_ON) != 0) {
        *control_out |= VI_CTRL_GAMMA_ON;
    }

    if ((func & OS_VI_GAMMA_OFF) != 0) {
        *control_out &= ~VI_CTRL_GAMMA_ON;
    }

    if ((func & OS_VI_GAMMA_DITHER_ON) != 0) {
        *control_out |= VI_CTRL_GAMMA_DITHER_ON;
    }

    if ((func & OS_VI_GAMMA_DITHER_OFF) != 0) {
        *control_out &= ~VI_CTRL_GAMMA_DITHER_ON;
    }

    if ((func & OS_VI_DIVOT_ON) != 0) {
        *control_out |= VI_CTRL_DIVOT_ON;
    }

    if ((func & OS_VI_DIVOT_OFF) != 0) {
        *control_out &= ~VI_CTRL_DIVOT_ON;
    }

    if ((func & OS_VI_DITHER_FILTER_ON) != 0) {
        *control_out |= VI_CTRL_DITHER_FILTER_ON;
        *control_out &= ~VI_CTRL_ANTIALIAS_MASK;
    }

    if ((func & OS_VI_DITHER_FILTER_OFF) != 0) {
        *control_out &= ~VI_CTRL_DITHER_FILTER_ON;
        *control_out |= next_state->mode->comRegs.ctrl & VI_CTRL_ANTIALIAS_MASK;
    }
}

extern "C" void osViBlack(uint8_t active) {
    std::lock_guard lock{ events_context.message_mutex };
    ViState* next_state = events_context.vi.get_next_state();
    uint32_t* state_out = &next_state->state;
    if (active) {
        *state_out |= VI_STATE_BLACK;
    } else {
        *state_out &= ~VI_STATE_BLACK;
    }
}

extern "C" void osViRepeatLine(uint8_t active) {
    std::lock_guard lock{ events_context.message_mutex };
    ViState* next_state = events_context.vi.get_next_state();
    uint32_t* state_out = &next_state->state;
    if (active) {
        *state_out |= VI_STATE_REPEATLINE;
    } else {
        *state_out &= ~VI_STATE_REPEATLINE;
    }
}

extern "C" void osViSetXScale(float scale) {
    if (scale != 1.0f) {
        assert(false);
    }
}

extern "C" void osViSetYScale(float scale) {
    if (scale != 1.0f) {
        assert(false);
    }
}

extern "C" PTR(void) osViGetNextFramebuffer() {
    std::lock_guard lock{ events_context.message_mutex };
    return events_context.vi.get_next_state()->framebuffer;
}

extern "C" PTR(void) osViGetCurrentFramebuffer() {
    std::lock_guard lock{ events_context.message_mutex };
    return events_context.vi.get_cur_state()->framebuffer;
}

void ultramodern::submit_rsp_task(RDRAM_ARG PTR(OSTask) task_) {
    OSTask* task = TO_PTR(OSTask, task_);

    // [replay 2026-11-14] In lockstep a task is done here and now, on the game thread that started it, and its completions
    // are posted from here: their order then is the game's, not the other threads'. A sound task runs its microcode at
    // once (it reads the command list the game has just finished and writes the sound out, as on the task thread). A
    // graphics task has nothing to do: this game's renderer reads the frame's list as the game ends it
    // (arcade_view.cpp, recomp_arcade_view_frame) and its send_dl is empty, so the task thread's "done" after it is all
    // there is. (A renderer that did draw in send_dl would have to be called here.)
    if (ultramodern::lockstep::on()) {
        if (task->t.type == M_GFXTASK) {
            PTR(u64) displaylist = task->t.data_ptr;
            ultramodern::extensions::on_displaylist_submitted(displaylist);
            sp_complete();
            dp_complete();
            ultramodern::extensions::on_displaylist_parsed(displaylist);
            ultramodern::extensions::on_displaylist_completed(displaylist);
        } else {
            if (!ultramodern::rsp::run_task(PASS_RDRAM task)) {
                fprintf(stderr, "Failed to execute task type: %" PRIu32 "\n", task->t.type);
                ULTRAMODERN_QUICK_EXIT();
            }
            sp_complete();
        }
        return;
    }

    // Send gfx tasks to the graphics action queue
    if (task->t.type == M_GFXTASK) {
        events_context.action_queue.enqueue(SpTaskAction{ *task });
    }
    // Set all other tasks as the RSP task
    else {
        events_context.sp_task_queue.enqueue(task);
    }
}

void ultramodern::send_si_message() {
    ultramodern::enqueue_external_message(events_context.si.mq, events_context.si.msg, false, true);
}

void ultramodern::init_events(RDRAM_ARG ultramodern::renderer::WindowHandle window_handle) {
    moodycamel::LightweightSemaphore gfx_thread_ready;
    moodycamel::LightweightSemaphore task_thread_ready;
    events_context.rdram = rdram;
    events_context.sp.gfx_thread = std::thread{ gfx_thread_func, rdram, &gfx_thread_ready, window_handle };
    events_context.sp.task_thread = std::thread{ task_thread_func, rdram, &task_thread_ready };

    // Wait for the two sp threads to be ready before continuing to prevent the game from
    // running before we're able to handle RSP tasks.
    gfx_thread_ready.wait();
    task_thread_ready.wait();

    ultramodern::renderer::SetupResult setup_result = renderer_setup_result.load();
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        auto show_renderer_error = [](const std::string& msg) {
            std::string error_msg = "An error has been encountered on startup: " + msg;

            ultramodern::error_handling::message_box(error_msg.c_str());
        };

        const std::string driver_os_suffix = "\nPlease make sure your GPU drivers and your OS are up to date.";
        switch (setup_result) {
            case ultramodern::renderer::SetupResult::Success:
                break;
            case ultramodern::renderer::SetupResult::DynamicLibrariesNotFound:
                show_renderer_error("Failed to load dynamic libraries. Make sure the DLLs are next to the recomp executable.");
                break;
            case ultramodern::renderer::SetupResult::InvalidGraphicsAPI:
                show_renderer_error(ultramodern::renderer::get_graphics_api_name(renderer_chosen_api.load()) + " is not supported on this platform. Please select a different graphics API.");
                break;
            case ultramodern::renderer::SetupResult::GraphicsAPINotFound:
                show_renderer_error("Unable to initialize " + ultramodern::renderer::get_graphics_api_name(renderer_chosen_api.load()) + "." + driver_os_suffix);
                break;
            case ultramodern::renderer::SetupResult::GraphicsDeviceNotFound:
                show_renderer_error("Unable to find compatible graphics device." + driver_os_suffix);
                break;
        }
        throw std::runtime_error("Failed to initialize the renderer");
    }

    events_context.vi.thread = std::thread{ vi_thread_func };
}

void ultramodern::join_event_threads() {
    events_context.sp.gfx_thread.join();
    events_context.vi.thread.join();

    // Send a null RSP task to indicate that the RSP task thread should exit.
    events_context.sp_task_queue.enqueue(nullptr);
    events_context.sp.task_thread.join();
}
