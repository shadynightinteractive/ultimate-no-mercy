#include "ultramodern/ultramodern.hpp"
#include "ultramodern/perf.hpp"

void ultramodern::schedule_running_thread(RDRAM_ARG PTR(OSThread) t_) {
    thread_queue_insert(PASS_RDRAM running_queue, t_);
    TO_PTR(OSThread, t_)->state = OSThreadState::QUEUED;
}

// (Hand over to anything of a higher priority that can run)
void ultramodern::check_running_queue(RDRAM_ARG1) {
    ultramodern::park_game_thread_if_quitting();
    if (thread_queue_empty(PASS_RDRAM running_queue)) {
        return;
    }
    PTR(OSThread) next_ = thread_queue_peek(PASS_RDRAM running_queue);
    OSThread* next = TO_PTR(OSThread, next_);
    OSThread* self = TO_PTR(OSThread, this_thread());
    if (next->priority > self->priority) {
        thread_queue_pop(PASS_RDRAM running_queue);
        schedule_running_thread(PASS_RDRAM this_thread());
        resume_thread_and_wait(PASS_RDRAM next);
    }
}

extern "C" void pause_self(RDRAM_ARG1) {
    for (;;) {
        const bool perf_on = ultramodern::perf::enabled();
        const int64_t perf_start = perf_on ? ultramodern::perf::now_ns() : 0;
        ultramodern::wait_for_external_message(PASS_RDRAM1);
        if (perf_on) {
            ultramodern::perf::add_game_idle(ultramodern::perf::now_ns() - perf_start);
        }
        ultramodern::check_running_queue(PASS_RDRAM1);
    }
}

extern "C" void yield_self(RDRAM_ARG1) {
    ultramodern::wait_for_external_message(PASS_RDRAM1);
    ultramodern::check_running_queue(PASS_RDRAM1);
}

extern "C" void yield_self_1ms(RDRAM_ARG1) {
    ultramodern::wait_for_external_message_timed(PASS_RDRAM1, 1);
    ultramodern::check_running_queue(PASS_RDRAM1);
}
