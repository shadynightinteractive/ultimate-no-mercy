#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <deque>

#include "blockingconcurrentqueue.h"

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/lockstep.hpp"

// The game's message queues. They live in its memory, and only a game thread (one at a time) touches them. Everything
// else (the retrace, a finished sound task, a loaded block) posts here, and the game thread that's running delivers.
//
// A completion arriving at a full queue is kept and tried again until there's room ("retry"): these things finish
// instantly here where on an N64 they took time, so the game hasn't had the chance it was built around to empty the
// queue. A retrace or a sound buffer's end, with a queue full, is simply missed, as on an N64.
struct QueuedMessage {
    PTR(OSMesgQueue) mq;
    OSMesg mesg;
    bool jam;
    bool retry;
};

static moodycamel::BlockingConcurrentQueue<QueuedMessage> external_messages{};
static std::deque<QueuedMessage> waiting;       // (those waiting for room, oldest first: the running game thread's)
static size_t waiting_said = 32;

void ultramodern::enqueue_external_message(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, bool requeue_if_blocked) {
    // [replay 2026-11-14] In lockstep only the game's own threads post here (in their own order, which is the game's); a
    // message from any other thread waits for the idle thread's turn, and is counted (ultramodern/lockstep.hpp).
    if (ultramodern::lockstep::on() && !ultramodern::is_game_thread() && mq != NULLPTR) {
        ultramodern::lockstep::post_foreign(mq, msg, jam, requeue_if_blocked);
        return;
    }
    external_messages.enqueue({ mq, msg, jam, requeue_if_blocked });
}

void ultramodern::wake_external_message_waiters(int count) {
    for (int i = 0; i < count; i++) {
        external_messages.enqueue({ NULLPTR, NULLPTR, false, false });
    }
}

static bool do_send(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, bool jam, bool block);

static void deliver(RDRAM_ARG const QueuedMessage& m) {
    if (m.mq == NULLPTR) {                      // (nobody's asked for this event yet, or it's only a wake-up)
        return;
    }
    if (!do_send(PASS_RDRAM m.mq, m.mesg, m.jam, false) && m.retry) {
        waiting.push_back(m);
        if (waiting.size() >= waiting_said) {   // (nothing should ever pile up: say so if it does)
            fprintf(stderr, "[message] %zu completions waiting for room (the latest for queue 0x%08X)\n", waiting.size(), (unsigned)(uint32_t)m.mq);
            waiting_said *= 2;
        }
    }
}

static void deliver_waiting(RDRAM_ARG1) {
    for (size_t n = waiting.size(); n > 0; n--) {
        const QueuedMessage m = waiting.front();
        waiting.pop_front();
        deliver(PASS_RDRAM m);
    }
}

static void deliver_new(RDRAM_ARG1) {
    ultramodern::park_game_thread_if_quitting();
    deliver_waiting(PASS_RDRAM1);
    QueuedMessage m;
    while (external_messages.try_dequeue(m)) {
        deliver(PASS_RDRAM m);
    }
}

// [replay 2026-11-14] (lockstep.cpp: what the game's threads have posted, handed over at the idle thread's turn)
void ultramodern::deliver_external_messages(RDRAM_ARG1) {
    deliver_new(PASS_RDRAM1);
}

void ultramodern::wait_for_external_message(RDRAM_ARG1) {
    // [replay 2026-11-14] In lockstep nothing from outside comes to wait for: the wait is the clock's turn to move
    if (ultramodern::lockstep::on()) { ultramodern::lockstep::idle(PASS_RDRAM1); return; }
    deliver_waiting(PASS_RDRAM1);
    QueuedMessage m;
    if (waiting.empty()) {
        external_messages.wait_dequeue(m);
        ultramodern::park_game_thread_if_quitting();
        deliver(PASS_RDRAM m);
    }
    else if (external_messages.wait_dequeue_timed(m, std::chrono::microseconds{ 16000 })) {
        ultramodern::park_game_thread_if_quitting();
        deliver(PASS_RDRAM m);
    }
    ultramodern::park_game_thread_if_quitting();
}

void ultramodern::wait_for_external_message_timed(RDRAM_ARG u32 millis) {
    QueuedMessage m;
    if (external_messages.wait_dequeue_timed(m, std::chrono::milliseconds{ millis })) {
        ultramodern::park_game_thread_if_quitting();
        deliver(PASS_RDRAM m);
    }
}

extern "C" void osCreateMesgQueue(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg, s32 count) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    mq->blocked_on_recv = NULLPTR;
    mq->blocked_on_send = NULLPTR;
    mq->msgCount = count;
    mq->msg = msg;
    mq->validCount = 0;
    mq->first = 0;
}

static void wake_one(RDRAM_ARG PTR(PTR(OSThread)) blocked) {
    if (!ultramodern::thread_queue_empty(PASS_RDRAM blocked)) {
        ultramodern::schedule_running_thread(PASS_RDRAM ultramodern::thread_queue_pop(PASS_RDRAM blocked));
    }
}

static bool do_send(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, bool jam, bool block) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    while (mq->validCount >= mq->msgCount) {
        if (!block) {
            return false;
        }
        ultramodern::thread_queue_insert(PASS_RDRAM GET_MEMBER(OSMesgQueue, mq_, blocked_on_send), ultramodern::this_thread());
        ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
    }
    if (jam) {
        mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
        TO_PTR(OSMesg, mq->msg)[mq->first] = msg;
    }
    else {
        TO_PTR(OSMesg, mq->msg)[(mq->first + mq->validCount) % mq->msgCount] = msg;
    }
    mq->validCount++;
    wake_one(PASS_RDRAM GET_MEMBER(OSMesgQueue, mq_, blocked_on_recv));
    return true;
}

static bool do_recv(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg_, bool block) {
    OSMesgQueue* mq = TO_PTR(OSMesgQueue, mq_);
    while (mq->validCount == 0) {
        if (!block) {
            return false;
        }
        ultramodern::thread_queue_insert(PASS_RDRAM GET_MEMBER(OSMesgQueue, mq_, blocked_on_recv), ultramodern::this_thread());
        ultramodern::run_next_thread_and_wait(PASS_RDRAM1);
    }
    if (msg_ != NULLPTR) {
        *TO_PTR(OSMesg, msg_) = TO_PTR(OSMesg, mq->msg)[mq->first];
    }
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;
    wake_one(PASS_RDRAM GET_MEMBER(OSMesgQueue, mq_, blocked_on_send));
    return true;
}

static s32 send_from_anywhere(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, s32 flags, bool jam) {
    if (!ultramodern::is_game_thread()) {
        ultramodern::enqueue_external_message(mq_, msg, jam, false);
        return 0;
    }
    deliver_new(PASS_RDRAM1);
    const bool sent = do_send(PASS_RDRAM mq_, msg, jam, flags == OS_MESG_BLOCK);
    ultramodern::check_running_queue(PASS_RDRAM1);
    return sent ? 0 : -1;
}

extern "C" s32 osSendMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, s32 flags) {
    return send_from_anywhere(PASS_RDRAM mq_, msg, flags, false);
}

extern "C" s32 osJamMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, OSMesg msg, s32 flags) {
    return send_from_anywhere(PASS_RDRAM mq_, msg, flags, true);
}

extern "C" s32 osRecvMesg(RDRAM_ARG PTR(OSMesgQueue) mq_, PTR(OSMesg) msg_, s32 flags) {
    assert(ultramodern::is_game_thread() && "RecvMesg not allowed outside of game threads.");
    deliver_new(PASS_RDRAM1);
    const bool received = do_recv(PASS_RDRAM mq_, msg_, flags == OS_MESG_BLOCK);
    ultramodern::check_running_queue(PASS_RDRAM1);
    return received ? 0 : -1;
}
