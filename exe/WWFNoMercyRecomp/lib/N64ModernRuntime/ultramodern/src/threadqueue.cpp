#include "ultramodern/ultramodern.hpp"

// A priority-ordered list of threads, linked through their structs. The running queue is the one that's ours alone.
static PTR(OSThread) running_queue_impl = NULLPTR;

static PTR(OSThread)* queue_to_ptr(RDRAM_ARG PTR(PTR(OSThread)) queue) {
    return queue == ultramodern::running_queue ? &running_queue_impl : TO_PTR(PTR(OSThread), queue);
}

void ultramodern::thread_queue_insert(RDRAM_ARG PTR(PTR(OSThread)) queue_, PTR(OSThread) toadd_) {
    PTR(OSThread)* cur = queue_to_ptr(PASS_RDRAM queue_);
    OSThread* toadd = TO_PTR(OSThread, toadd_);
    while (*cur != NULLPTR && TO_PTR(OSThread, *cur)->priority > toadd->priority) {
        cur = &TO_PTR(OSThread, *cur)->next;
    }
    toadd->next = *cur;
    toadd->queue = queue_;
    *cur = toadd_;
}

PTR(OSThread) ultramodern::thread_queue_pop(RDRAM_ARG PTR(PTR(OSThread)) queue_) {
    PTR(OSThread)* queue = queue_to_ptr(PASS_RDRAM queue_);
    PTR(OSThread) ret = *queue;
    if (ret == NULLPTR) {
        return NULLPTR;
    }
    *queue = TO_PTR(OSThread, ret)->next;
    TO_PTR(OSThread, ret)->queue = NULLPTR;
    return ret;
}

bool ultramodern::thread_queue_remove(RDRAM_ARG PTR(PTR(OSThread)) queue_, PTR(OSThread) t_) {
    PTR(OSThread)* cur = queue_to_ptr(PASS_RDRAM queue_);
    while (*cur != NULLPTR) {
        if (*cur == t_) {
            *cur = TO_PTR(OSThread, t_)->next;
            TO_PTR(OSThread, t_)->queue = NULLPTR;
            return true;
        }
        cur = &TO_PTR(OSThread, *cur)->next;
    }
    return false;
}

bool ultramodern::thread_queue_empty(RDRAM_ARG PTR(PTR(OSThread)) queue_) {
    return *queue_to_ptr(PASS_RDRAM queue_) == NULLPTR;
}

PTR(OSThread) ultramodern::thread_queue_peek(RDRAM_ARG PTR(PTR(OSThread)) queue_) {
    return *queue_to_ptr(PASS_RDRAM queue_);
}
