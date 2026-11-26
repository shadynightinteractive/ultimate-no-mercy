// [replay 2026-11-14] Lockstep: the game run so that the same inputs always give the same game, frame for frame. A QA
// tool (the replay / no-graphics mode of memory_groundwork's DESIGN.md, proof P3); off unless asked for, and then nothing
// of a normal run changes.
//
// Why a normal run is not repeatable: the game's clock (osGetCount / osGetTime, its timers) is the PC's clock, and the
// retrace, the timers and the finished sound tasks arrive from other threads at moments the PC's load decides, and are
// handed to whichever game thread is next at a scheduling point. The game counts the 60 Hz frames that went by while it
// worked (wait_graphics_count_frames: osGetTime before and after) and moves the match on by that many, so a busier PC
// plays a different match.
//
// In lockstep the game's clock is a count kept here, and it moves only when every game thread is waiting (the idle
// thread's turn, pause_self): then it goes straight on to the next retrace or timer, and that event's messages are
// handed over there and then. Everything else that used to come from another thread happens on the game thread that
// asked for it: a sound task runs at once, in osSpTaskStartGo; a graphics task has nothing to do (the native renderer
// reads the frame's list as the game ends it), so its completions are posted at once. The sound driver's "how much is
// still queued" (osAiGetLength / osAiGetStatus) is worked out from this clock. What the game sees then depends only on
// what it is given -- the pads, which nm_replay records and plays back -- and not on the PC.
//
// The clock can be held to the wall clock (paced: for a person playing, or a recording made live) or run as fast as the
// PC allows (a replay, or the no-graphics mode). The game behaves the same either way: in lockstep it never skips a frame
// for want of time, it only takes longer.
//
// Turned on by NM_LOCKSTEP=1, or by asking nm_replay for a recording (NM_REPLAY_RECORD) or a replay (NM_REPLAY_PLAY).
// NM_LOCKSTEP_PACE=1 / 0: paced or not (default: paced, except in a replay or with NM_HEADLESS=1).
#pragma once
#include <cstddef>
#include <cstdint>
#include "ultramodern/ultra64.h"

namespace ultramodern::lockstep {

// N64 CPU counter ticks a second, and a retrace's worth of them (the VI thread's own figures, events.cpp)
constexpr uint64_t TICKS_PER_SECOND = 46'875'000ull;
constexpr uint64_t TICKS_PER_VI = TICKS_PER_SECOND / 60;

bool on();                        // lockstep for this run (read once, at the first ask)
bool paced();                     // the game's clock held to the wall clock (times the fast-forward speed)
uint64_t now();                   // the game's clock, in counter ticks
uint64_t retraces();              // retraces so far (the frame number the hashes are kept by)
uint64_t foreign_messages();      // messages posted from outside the game's threads: each one is a leak of the PC's timing

// What a QA tool is told (nm_replay). on_retrace runs on the idle thread at each retrace, before the retrace's messages
// are handed over: every game thread is waiting, so the game's memory holds still while it is read.
struct callbacks_t {
    void (*on_retrace)(uint8_t* rdram, uint64_t retrace) = nullptr;
};
void set_callbacks(const callbacks_t& callbacks);

// Places in the game's memory that hold the PC's own values, not the game's (the threads' host contexts, threads.cpp):
// [address, address + bytes) as the PC holds them, from the start of rdram. A hash of all the memory leaves them out.
struct HostBytes { uint32_t offset, bytes; };
void note_host_bytes(uint32_t n64_address, uint32_t bytes);
const HostBytes* host_bytes(size_t* count);

// ---- the runtime's side (ultramodern) ----
void idle(RDRAM_ARG1);                                                       // the idle thread's turn (scheduling.cpp pause_self)
void post_foreign(PTR(OSMesgQueue) mq, OSMesg msg, bool jam, bool retry);    // a message from another thread (mesgqueue.cpp)
// (events.cpp) the retrace's work -- the VI registers, the retrace and audio messages, the per-retrace callback -- done
// from the idle thread instead of the VI thread
void retrace_step(RDRAM_ARG1);
// (timer.cpp) the timers, kept here instead of the timer thread: the earliest one's time, and those due by `now` fired
bool next_timer(RDRAM_ARG uint64_t* when);
void fire_timers(RDRAM_ARG uint64_t now);
void add_timer(PTR(OSTimer) timer);
void remove_timer(PTR(OSTimer) timer);

}  // namespace ultramodern::lockstep

namespace ultramodern {
// (mesgqueue.cpp) hand over what the game's threads have posted: the idle thread's turn does it, in their order
void deliver_external_messages(RDRAM_ARG1);
}
