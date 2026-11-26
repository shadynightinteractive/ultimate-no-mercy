// fuzz_main.cpp: tests a converted game function against its original on random inputs, away from the game.
//
//     fuzz.exe <function> <tests>
//
// The comparison in real play (src/game/lift_twin.cpp) sees the values the tests happen to reach. This reaches rare ones, and paths
// the game may never take in a test. One test:
//   1. Random registers, parameters and memory: small numbers, edge cases, pointers, floats. Every register a function shouldn't read
//      gets rubbish, so one that does read it shows up.
//   2. The original runs, with its writes noted. What it did is kept, then undone.
//   3. The converted function runs from the same state. Its writes are noted and undone.
//   4. The two must have changed the same bytes to the same values and given the same result. The original must also have left the
//      stack and the registers it has to keep as they were: if it didn't, it isn't a whole function, and converting it is wrong.
//
// The function's own stack frame isn't compared: that's where the original keeps locals and saved registers, which the converted one
// keeps in C++ variables. What callers do afterwards isn't tested here either: lift.py checks that against the whole program.
//
// A function it calls is a stand-in here (fuzz_stand_in). The real one can't run outside the game, so the stand-in behaves as badly as
// the calling rules allow: it notes what it was given, then returns a random result, scrambles every register it's allowed to change,
// and sometimes changes memory. Each call depends only on the test and how many calls came before, so the original and the converted
// function get the same treatment and must make the same calls, with the same parameters, in the same order.
//
// Every 32-bit address is real memory here, so a wild pointer can't crash the test. It reads zeros, as the two versions both would.
//
// A function part 1 (those random tests) didn't wholly reach gets part 2: tests steered from the ones that reached something new,
// changing what the original's path read. See Steering. It adds tests and changes nothing else: the same comparison, and the same rule
// that an instruction counts only when a compared test ran it. (FUZZ_NO_STEER: part 1 alone. FUZZ_STEER_MS: part 2's time.)
// Part 3 is boundary inputs (compare_word). A function parts 1 to 3 still didn't wholly reach gets part 4: more steered tests,
// aimed at the branches that lead somewhere no test has been, with stand-ins that can do more (see Part 4 in Steering).
// (FUZZ_NO_PART4: without it. FUZZ_PART4_MS: its time. FUZZ_DEBUG4: what it kept and which branches it couldn't turn.)
// A function parts 1 to 4 still didn't wholly reach gets part 5 (2026-11-12): part 4's tests with aliases between its own
// pointers and loop bounds made small (see Part 5 in Steering). (FUZZ_NO_PART5: without it. FUZZ_PART5_MS: its time.)
//
// Prints one line in the same form as a report from the proof build, so tools/lift/lift.py can add them up:
//     name tests compared differences reached-mask instructions first-difference part-1-compared
// (compared: parts 1 to 3. part-1-compared: part 1's alone, the plain random tests; the last column, which proof-build reports don't
// have. Part 4 only reaches: what it compares is not in either count, though a difference it finds is.)
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#include "fuzz.h"

uint8_t* lift_rdram;

static const u32 ARENA = 0x80100000u;        // Where random pointers point.
static const u32 ARENA_BYTES = 0x1000;
static const u32 STACK = 0x80600000u;        // The stack pointer each test starts with.
static const u64 MEMORY_BYTES = 0x100100000ull;
static const u32 FILLED_START = 0x80000000u; // The part of memory given random contents at the start.
static const u32 FILLED_BYTES = 0x400000;
static const u32 MOST_ACCESSES = 20000;      // More memory accesses than this in one call: a loop random data has made endless.
u32 lift_fuzz_access_limit = MOST_ACCESSES;     // (Part 2 raises it for a second chance: see run_both. Counted inline too: fuzz.h, FuzzMem.)
static u32& access_limit = lift_fuzz_access_limit;
// (FUZZ_MS: a longer limit, for the long-run pass over functions that were stopped short of the full count -- lift.py
//  too-few, night.ps1. 2026-11-08)
static const ULONGLONG MOST_MILLISECONDS = std::getenv("FUZZ_MS") ? (ULONGLONG)std::atoll(std::getenv("FUZZ_MS")) : 40000;   // How long one function gets, however many tests that comes to.
// (2026-11-10) Every time limit here is this process's own CPU time, not the clock on the wall. With the wall clock, a function
// tested while the PC was busy (another job, the user's programs) got fewer tests in its time and could end short of the full
// count -- "too few tests", then the long pass -- for no reason of its own. On CPU time each function gets the same work done
// however busy the PC is, so lift.py can run the testers at low priority and let everything else on the PC go first.
static ULONGLONG fuzz_clock_ms() {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return GetTickCount64();
    const ULONGLONG k = ((ULONGLONG)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    const ULONGLONG u = ((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime;
    return (k + u) / 10000;     // (100-nanosecond units)
}

struct TooLong {};
struct NotAGameState {};       // (a NaN into the original's float arithmetic, in part 2: see NAN_CHECK in fuzz.h)
bool lift_fuzz_nan_stops = false;
bool lift_fuzz_nan_noted = false;      // (the original's last run had a NaN operand: fuzz.h)
// (2026-11-13, final082. TESTER CHANGE) A NaN the original MADE from numbers sets the test aside too (fuzz.h FUZZ_NAN_MADE);
// FUZZ_NAN_MADE=0 turns that off, for measuring. nan_made_set_aside (below) counts the tests set aside for it, to stderr.
bool lift_fuzz_nan_made_on = !(std::getenv("FUZZ_NAN_MADE") && std::getenv("FUZZ_NAN_MADE")[0] == '0');
bool lift_fuzz_nan_made_seen = false;
void lift_fuzz_nan_seen() { throw NotAGameState{}; }
static bool steering_now = false;     // Part 2 is running.
static bool steering_now_flag() { return steering_now; }
// (2026-11-11, scratch/reach_overnight) Each addition to the steering made then has a switch, for looking at what each one
// does: FUZZ_NO_<name> turns that one off, FUZZ_NO_REACH_1111 all of them -- which makes every test exactly what it was before
// them (they only ever add random draws while they are on). Parts 1 and 3 don't use any of them.
static bool reach_off(const char* name) {
    if (std::getenv("FUZZ_NO_REACH_1111") != nullptr) return true;
    return std::getenv(name) != nullptr;
}
static const bool use_shared_fill = !reach_off("FUZZ_NO_SHARED_FILL");     // (steer_stand_in: a frame local both versions keep)
static const bool use_echo = !reach_off("FUZZ_NO_ECHO");                   // (steer_stand_in: read back an earlier call's buffer)
static const bool use_filled_sources = !reach_off("FUZZ_NO_FILLED_SOURCES");   // (frame_fill_of, learn4, source_tweak)
static const bool use_shift = !reach_off("FUZZ_NO_SHIFT");                 // (mutate4: shifts)
static const bool use_slt_aim = !reach_off("FUZZ_NO_SLT_AIM");             // (mutate4: slt_want)
static const bool use_below = !reach_off("FUZZ_NO_BELOW");                 // (mutate4: any value below a small bound)
static const bool use_rare_too = !reach_off("FUZZ_NO_RARE_TOO");           // (mutate4: rare beside stuck)
// (2026-11-12, agent "steer": part 5, after part 4 -- see Part 5 in Steering. Each of its additions has a switch too, and
//  FUZZ_NO_PART5 leaves the whole part out, which makes every test exactly what it was before it.)
static const bool use_alias = !reach_off("FUZZ_NO_ALIAS");                 // (part 5: alias_tweak5)
static const bool use_shrink = !reach_off("FUZZ_NO_SHRINK");               // (part 5: shrink5)
static const bool use_constant_offsets = !reach_off("FUZZ_NO_CONSTANT_OFFSETS");   // (part 5: mutate4, an input register plus a constant)
struct Undo { uint64_t offset; u8 before; };

// A jump table indexed past its end. The translated original stops there; on the N64 it would jump somewhere random. Random inputs can
// do this where the game never does, so such a test is skipped, the same as one that ran away.
extern "C" void switch_error(const char* func, uint32_t vram, uint32_t jtbl) { throw TooLong{}; }
// What the functions it calls were given, in order: the two versions must match.
// (2026-11-15, modnative7, from the review of 0.84 (reviewnm084): a TESTER CORRECTNESS FIX, stricter.) The list of calls and
// what they were given, and -- new -- which of its entries are one of the function's own words (note_stack_word), kept by
// POSITION. Until 0.83 an own word was told by bit 62 of the entry's value, and settle_own_words dropped the low 32 bits of
// every entry with that bit set: an entry that is a 64-bit sum or digest (memory_sum at a system call, a port call's digest,
// a private object's hash) has bit 62 set half the time, so half of those entries were compared on their high bits only. Now
// only the entries note_stack_word marks are settled; every other entry is compared whole.
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
// (The argument compare, FUZZ_ARGS_STRICT=1, 2026-11-16, agent argcheck: see "The argument compare" further on.) What one
// stand-in call was given: the argument registers a0 a1 a2 a3 f12 f13 f14 f15 (32 bits each), the 16 words from its
// $sp + 0x10, and -- in the original's run -- which of them the original changed or wrote since the call before.
struct ArgCall {
    u32 which;          // (the stand-in's number, as in the list of calls)
    u32 skip;           // (bit i: register i the callee is proved never to read -- readcheck.py)
    u32 stack;          // (bit j: stack word j the callee may read)
    u32 changed;        // (bit i: register i changed since the last stand-in returned, or since entry; the original's run)
    u32 written;        // (bit j: stack word j written by the function's own code since then; the original's run)
    u32 regs[8];
    u32 stack_words[16];
};
#endif
struct Trace {
    std::vector<uint64_t> v;
    std::vector<uint32_t> own;      // (positions in v of the function's own words)
    void push_back(uint64_t x) { v.push_back(x); }
    void push_own(uint64_t x) { own.push_back((uint32_t)v.size()); v.push_back(x); }
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    std::vector<ArgCall> args;      // (each stand-in call's arguments: the argument compare. Not part of ==)
    void clear() { v.clear(); own.clear(); args.clear(); }
#else
    void clear() { v.clear(); own.clear(); }
#endif
    size_t size() const { return v.size(); }
    uint64_t& operator[](size_t i) { return v[i]; }
    const uint64_t& operator[](size_t i) const { return v[i]; }
    bool operator==(const Trace& o) const { return v == o.v; }
    bool operator!=(const Trace& o) const { return v != o.v; }
};
static Trace call_trace;
// The runtime's lookup of a function by address (LOOKUP_FUNC), here a recording stand-in for each call into an overlay (lift.py,
// "Calls into overlays", 2026-11-07). Both versions look the address up the same way -- the original's LOOKUP_FUNC(0x800D9AB0),
// the converted lift_lookup<0x800D9AB0u> -- and get the stand-in run_fuzz wrote for it (fuzz_overlay_lookup), which notes the
// address and what it was given in the list of calls, so a wrong address or a missing call is a difference. Any other address
// -- in the tester, only a wrong one can be: no original looks it up -- comes to fuzz_overlay_unknown_address, which notes it
// too, as itself, with every parameter register: a difference as well, never a skipped test. Where fuzz_overlay_lookup gives
// nothing (the independence test's generated file, lift.py run_independence), a lookup skips the test, as every lookup did
// before (only calls through a pointer reached one then, and those are refused).
static void fuzz_overlay_unknown(uint8_t* rdram, recomp_context* ctx) { fuzz_stand_in(0x7F000000u, 0x3F, 0, ctx); }
recomp_func_t* fuzz_overlay_unknown_address(int32_t vram) {
    call_trace.push_back(0x7F000000u);
    call_trace.push_back((u32)vram);
    return fuzz_overlay_unknown;
}
recomp_func_t* get_function(int32_t vram) {
    if (recomp_func_t* found = fuzz_overlay_lookup(vram)) return found;
    throw TooLong{};
}

// Where a test goes when the game would have stopped (2026-11-07): the N64's "break" after a division by zero (or of the most
// negative number by -1), which the translated original calls do_break for -- in the game that prints and exits -- and the PC's
// own trap on such a division, which ends the game there just as surely. A converted function has m2c's plain division in place
// of the compiler's checks and their break (lift.py REFUSED), so it stops on the division itself, where the original stops on
// the break just after it. Both versions stopping is then the same ending, and what they did up to there is compared as for any
// other test (run_version, main). Only while a version runs (stop_armed): otherwise, as before, do_break skips the test and a
// division trap is a crash.
//
// The stop is a C++ exception (Stopped), caught in run_version, as TooLong is (2026-11-08). Until then it was a
// __builtin_longjmp back to a __builtin_setjmp in run_version, and on 64-bit Windows clang's pair gets one thing wrong: the
// longjmp puts back the frame pointer as the address setjmp noted (__builtin_frame_address), which is not the value run_version's
// own code expects in rbp (here 0x80 below it). Its way out then reloaded the saved float registers xmm6-xmm15 -- which the
// calling rules say a function must give back -- from the wrong place, below the stack. The functions that called run_version got
// garbage in them: main keeps zero in xmm6 to clear a vector with, cleared one with garbage, and freeing it ended the tester with
// a heap-corruption crash (0xC0000374). That is what 44 converted functions with a division showed as "crashed" in
// fuzz/report.txt (2026-11-08) -- every one of them the tester, after a test where both versions stopped. An exception goes back
// through each frame by its own unwinding rules, which give every register back as it was.
struct Stopped {};
// ---- A test of a function that never returns (2026-11-12, scratch/agents/neverran; the rule approved by the user 2026-11-12) ----
// A thread's body (func_80000E3C, the VI manager's loop) waits for a message, handles it and waits again, for ever: no test of
// it ever ends, so in both versions every test runs past the access limit and none is compared. For a function with no return
// instruction (FuzzFunction.never_returns, lift.py never_returns), a test ends at the K-th call (K from 1 to 64, from the
// test's seed): the stand-in notes the call -- its parameters, and for a system function the memory as it is then -- and stops
// the run there, as stop_here does after a division. The two versions are then compared as for any stopped test: the calls they
// made, and the memory outside the frame. Both were given the same results by the same K-1 calls before it, so they must have
// done the same by the K-th. Every function with a return instruction is tested exactly as before. (FUZZ_NO_CUT: never.)
static long cut_call = 0;
static const bool cut_allowed = std::getenv("FUZZ_NO_CUT") == nullptr;
static inline void maybe_cut(u32 calls) { if (cut_call != 0 && calls + 1 == (u32)cut_call) throw Stopped{}; }
static volatile bool stop_armed = false;
__attribute__((noinline, noreturn)) static void stop_here() {
    stop_armed = false;
    throw Stopped{};
}
void do_break(uint32_t vram) {
    if (stop_armed) stop_here();
    throw TooLong{};
}
// From the division trap: the thread carries on in stop_here as if the function that divided had just made a call to it from where
// its own caller called it. The dividing function's frame is taken off first (RtlVirtualUnwind, with its unwinding rules -- or, a
// function with no frame of its own, by taking its return address off the stack), so the context is its caller's just after that
// call came back, and stop_here gets a frame there as a real call would make it: the return address pushed, the stack aligned as
// the rules want on the way into a function. The exception it throws then unwinds from that caller on, as any exception from a
// call it made would. (The dividing function's own clean-ups are skipped: a converted function's LiftFrame only puts back the stack
// pointer of a context that's thrown away. Faking a call from the dividing instruction itself would leave the stack misaligned in
// a function with no frame.)
static LONG WINAPI stop_on_division_trap(EXCEPTION_POINTERS* e) {
    DWORD code = e->ExceptionRecord->ExceptionCode;
    if (!stop_armed || (code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW)) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = e->ContextRecord;
    DWORD64 image = 0;
    if (PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(c->Rip, &image, nullptr)) {
        void* handler_data = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, c->Rip, entry, c, &handler_data, &frame, nullptr);
    } else {
        c->Rip = *(DWORD64*)c->Rsp;         // (a function with no unwinding rules has no frame: its return address is on top)
        c->Rsp += 8;
    }
    c->Rsp -= 8;
    *(DWORD64*)c->Rsp = c->Rip;             // (the "call": its return address, where the caller would have carried on)
    c->Rip = (DWORD64)&stop_here;
    return EXCEPTION_CONTINUE_EXECUTION;
}
// The list of what a run wrote, with what each byte held before its first write. Each byte is on it once (2026-11-09: scratch/lift_fast).
// Before, every write to every byte added an entry and collect_and_undo sorted them all out at the end; a loop that writes the same
// few words 20,000 times spent most of its test there. `seen` says, a bit per byte of a 4 KB page, which bytes are on the list already;
// a page's bits are only allocated while it is in use (a wild pointer can name any page), and collect_and_undo reads the bits back in
// address order -- exactly the sorted list it made before.
static std::vector<Undo> undo_list;
u32 lift_fuzz_accesses;
static u32& accesses = lift_fuzz_accesses;
void lift_fuzz_too_long() { throw TooLong{}; }
struct SeenPage { uint64_t bits[64]; };
static const u32 SEEN_PAGES = (u32)(MEMORY_BYTES >> 12) + 1;
static SeenPage* seen_table[SEEN_PAGES];      // (zeros until a page is written: 8 MB of address space, only what's touched is real)
static std::vector<SeenPage*> seen_spare;
static std::vector<u32> seen_used;
static inline bool mark_written(uint64_t offset) {
    SeenPage*& p = seen_table[offset >> 12];
    if (p == nullptr) {
        if (seen_spare.empty()) p = (SeenPage*)std::calloc(1, sizeof(SeenPage));
        else { p = seen_spare.back(); seen_spare.pop_back(); }
        seen_used.push_back((u32)(offset >> 12));
    }
    uint64_t& word = p->bits[(offset >> 6) & 63];
    const uint64_t bit = 1ull << (offset & 63);
    if (word & bit) return false;
    word |= bit;
    return true;
}
static inline bool was_written(uint64_t offset) {
    const SeenPage* p = seen_table[offset >> 12];
    return p != nullptr && (p->bits[(offset >> 6) & 63] >> (offset & 63) & 1);
}

void lift_fuzz_count_access() {
    if (++accesses > access_limit) throw TooLong{};
}

// ---- The memory as it is at a call to a system function (2026-11-07, scratch/lift_sys) ----
// A system function reads game memory the caller filled in -- the message queue, the OSIoMesg of a DMA, the display list of an RSP
// task -- so the converted caller has to have made the same writes by the time it calls. The final comparison can't show that (a
// store moved past the call ends the same), and a stand-in only notes what its parameters point to on the stack. So for a function
// that calls one (FuzzFunction.system_calls) the tester keeps a running sum over every byte written outside the caller's frame:
// hash(offset, value now) - hash(offset, value before the run), which is the same in both runs exactly when the same bytes hold
// the same values (barring a 64-bit collision), whatever the order of the writes. fuzz_system_stand_in notes it at each call.
// (fuzz_port_call works the same out from the list of writes at each call; this keeps it as it goes, since a system function can
// be called in a loop -- osVirtualToPhysical once per display-list entry.) The frame (as collect_and_undo leaves it out) isn't in
// it: the original keeps saved registers and spills there, the converted one keeps those in C++ variables; what a parameter points
// to there is noted by the stand-in itself.
static bool track_memory = false;
static const FuzzFunction* under_test = nullptr;    // (the one being tested: its fixed addresses, for fuzz_system_stand_in)
static uint64_t memory_sum = 0;
static uint64_t pending_offset = 0;    // The bytes the last write noted, about to be written: added to the sum at the next note.
static u32 pending_bytes = 0;
// (2026-11-10) Where the memory that is the function's own ends: its frame, the four words above it the calling rules keep
// for a0-a3, and -- new -- the words its stack parameters came in, one per parameter after the fourth. Those are the
// callee's too: a caller stores them afresh for every call and reads none back. GCC keeps a stack parameter the function
// changes in that word (func_8013B0D8: `sw $t4, 0xB8($sp)` with a 0xA8 frame, its 5th parameter divided and stored back),
// where the converted function changes its C++ parameter, so comparing the word showed a difference no caller can see
// (memory-count@80600010). Until now only the first four words were left out.
static uint64_t own_memory_end() {
    static const FuzzFunction* of = nullptr;
    static uint64_t end = (STACK + 0x10u) - 0x80000000u;
    if (of != under_test && under_test != nullptr) {
        of = under_test;
        const size_t parameters = std::strlen(under_test->params);
        end = (STACK + 4u * (u32)(parameters < 4 ? 4 : parameters)) - 0x80000000u;
    }
    return end;
}
static const uint64_t TRACK_FRAME_LOW = (STACK - 0x4000u) - 0x80000000u;
#define TRACK_FRAME_HIGH own_memory_end()

static inline uint64_t byte_hash(uint64_t offset, u8 value) {
    uint64_t x = ((offset << 8) | value) * 0x9E3779B97F4A7C15ull;
    x ^= x >> 31;
    x *= 0xBF58476D1CE4E5B9ull;
    return x ^ (x >> 29);
}

static void settle_pending() {
    for (u32 i = 0; i < pending_bytes; i++) {
        uint64_t o = pending_offset + i;
        if (o < TRACK_FRAME_LOW || o >= TRACK_FRAME_HIGH) memory_sum += byte_hash(o, lift_rdram[o]);
    }
    pending_bytes = 0;
}

// Device registers (2026-11-10: see FuzzMem in fuzz.h). How many reads of one this run has made; back to 0 at the start of
// each run, so the original and the converted function are given the same sequence.
static u32 device_reads = 0;
static u32 device_reads_run = 0;     // (which run this is: the per-register counts start again with each)

// At the start of each run (the original's, then the converted's): nothing written yet.
static void reset_memory_sum() {
    memory_sum = 0;
    pending_bytes = 0;
    device_reads = 0;
    device_reads_run++;
}

// (2026-11-11, steering only) The words of the 16 KB under the tester's stack that the CONVERTED function itself has written
// in some run (not a stand-in's writes): its locals that live in game memory -- an array whose address it passes, which it
// keeps there as the original does (lift_call.h, LiftFrame's frame.at) -- and the slots its frame.keep stores saved registers
// in. A word the original writes and the converted function never does is a spill or the like, which the converted function
// holds in a C++ variable instead. See steer_stand_in's fill.
static uint64_t converted_frame_words[0x4000 / 4 / 64];
static bool running_converted = false, in_stand_in = false;
// (2026-11-13, agent ce80) And the words above that stack pointer that are the function's own (own_memory_end: a0-a3's home
// slots and its stack parameters' words) that the converted function has written, in this test's run or an earlier one. A
// converted function writes one only when lift.py keeps a parameter in game memory because the function hands on its address
// (parameter_slots, `&arg2`: it stores the home slots there as the original does), or when it has no frame and passes stack
// parameters of its own there. Every other converted function keeps all its parameters in C++ variables, so a word the
// original writes back (func_8011CE80's 5th parameter, `sw $t2, 0xB0($sp)`) still holds the value it came in with in the
// converted run. See note_stack_word. (A bit per word from STACK.)
static uint64_t converted_own_words = 0;
static inline void note_converted_frame_write(uint64_t offset, u32 bytes) {
    const uint64_t low = (STACK - 0x4000u) - 0x80000000u, high = STACK - 0x80000000u;
    for (uint64_t o = offset & ~3ull; o < offset + bytes; o += 4) {
        if (o >= low && o < high) converted_frame_words[(o - low) >> 8] |= 1ull << (((o - low) >> 2) & 63);
        else if (o >= high && o < own_memory_end() && (o - high) / 4 < 64) converted_own_words |= 1ull << ((o - high) / 4);
    }
}
static inline bool converted_wrote_frame_word(u32 address) {
    const u32 low = STACK - 0x4000u;
    if (address < low || address >= STACK) return false;
    const u32 w = (address - low) >> 2;
    return (converted_frame_words[w >> 6] >> (w & 63)) & 1;
}

// (2026-11-12, part 5 only) The game addresses the original's own code wrote (not a stand-in's writes), in order: where its
// stores landed, for alias_tweak5. Only while part 5 notes an original's run; nothing else sees them.
static bool noting_writes = false;
static std::vector<u32> original_writes;
static bool running_original_flag();
// ---- The write cut (2026-11-15, scratch/agents/rules084: a PROPOSAL, "find another way" for code only a run past the access
//      limit reaches; nothing counts from it until an independent review: REVIEW_WRITECUT.txt. TESTER CHANGE. APPROVED by
//      reviewr084 2026-11-15; the 0.84 night run sets LIFT_WRITE_CUT=1) ----
// For a function lift.py names (fuzz_write_cut; none unless LIFT_WRITE_CUT=1), a test both versions run past the limit is
// not simply set aside: both are run again from the same start and each is stopped just before its K-th write outside its
// own frame -- K the number of such writes both made before the limit -- and the two are then compared as any stopped test
// is: the calls made and the memory written. The game code a version runs counts its writes (not a stand-in's).
__attribute__((weak)) bool fuzz_write_cut(const char* function) { (void)function; return false; }
static bool write_cut_here = false;      // (the function under test is one of them)
static long write_cut_at = 0;            // (0: no cut; else stop just before the write_cut_at-th write)
static long nonframe_writes = 0;         // (the running version's game-code writes outside its frame so far)
static bool write_cut_hit = false;
struct Stopped;
[[noreturn]] static void write_cut_stop();
static inline void count_nonframe_write(uint64_t offset);
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
static void args_note_write(uint64_t offset, u32 bytes);     // (the argument compare: see there)
#endif
void lift_fuzz_note_write(uint64_t offset, u32 bytes) {
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    args_note_write(offset, bytes);
#endif
    count_nonframe_write(offset);
    lift_fuzz_count_access();
    if (noting_writes && !in_stand_in && running_original_flag() && original_writes.size() < 4096) {
        // (FuzzMem's offset has the byte order's flip in it -- 3 for a byte, 2 for a halfword: see fuzz.h -- so the address
        //  the game's code computed is the offset flipped back.)
        const u32 flip = bytes == 1 ? 3u : bytes == 2 ? 2u : 0u;
        original_writes.push_back(((u32)offset ^ flip) + 0x80000000u);
    }
    if (running_converted && !in_stand_in) note_converted_frame_write(offset, bytes);
    for (u32 i = 0; i < bytes; i++) {
        if (mark_written(offset + i)) undo_list.push_back({offset + i, lift_rdram[offset + i]});
    }
    if (track_memory) {
        // (Every write notes itself just before it's made: the bytes' old values come out of the sum now, their new ones go in
        // at the next note, or when a stand-in reads the sum.)
        settle_pending();
        for (u32 i = 0; i < bytes; i++) {
            uint64_t o = offset + i;
            if (o < TRACK_FRAME_LOW || o >= TRACK_FRAME_HIGH) memory_sum -= byte_hash(o, lift_rdram[o]);
        }
        pending_offset = offset;
        pending_bytes = bytes;
    }
}

static uint64_t test_seed;

// The value a device register gives on this run's n-th read of any of them: from the test's seed, the register and n only.
// Half the time 0 ("not busy", "nothing pending": what ends a wait), otherwise a small number or any word, so a wait is
// entered about as often as not and ends after a few turns.
// Only for the device pages the function under test names itself (main fills device_pages from its constants: the
// 0xA4xx0000 its `lui` loads): see FuzzMem in fuzz.h for why. FUZZ_NO_DEVICE: none, so every read is plain memory, as it was.
static std::vector<u32> device_pages;
static void note_device_pages(const FuzzFunction& f) {
    if (std::getenv("FUZZ_NO_DEVICE") != nullptr) return;
    for (int i = 0; i < f.constant_count; i++) {
        if ((f.constants[i] >> 24) == 0xA4 && (f.constants[i] & 0xFFFF) == 0) device_pages.push_back(f.constants[i]);
    }
}
bool lift_fuzz_device_named(u32 offset) {
    // (An address a little below a page's start is written `lui page + 1; offset -N`, so the page above counts too.)
    const u32 page = (offset + 0x80000000u) & 0xFFFF0000u;
    for (u32 named : device_pages) {
        if (named == page || named == page + 0x10000u) return true;
    }
    return false;
}
u32 lift_fuzz_device_read(u32 offset) {
    // (2026-11-12, proposed by the agent "different", approved by the user 2026-11-12) The n-th read of THIS register, not of
    // any: two reads of different registers may come in either order and still get the same values; two reads of one register
    // must still be in the same order. func_800384B0 reads a random pointer's fields 4 and 8, which the tester's dictionary can
    // make the VI's page; the converted code reads them the other way round (m2c passes field 4 straight to the call).
    // (FUZZ_NO_DEVICE_ORDER: numbered over all registers, as before.)
    static const bool per_register = std::getenv("FUZZ_NO_DEVICE_ORDER") == nullptr;
    static u32 seen_at[64], seen_count[64];
    static u32 seen_run = 0, seen_used = 0;
    if (seen_run != device_reads_run) { seen_run = device_reads_run; seen_used = 0; }
    u32 n = 0, i = 0;
    for (; i < seen_used && seen_at[i] != (offset & ~3u); i++) {}
    if (i == seen_used && seen_used < 64) { seen_at[seen_used] = offset & ~3u; seen_count[seen_used++] = 0; }
    if (!per_register) n = device_reads++;
    else if (i < 64) n = seen_count[i]++;
    else n = 0x10000u + device_reads++;
    uint64_t x = test_seed ^ ((uint64_t)(offset & ~3u) << 32) ^ ((uint64_t)n * 0x9E3779B97F4A7C15ull);
    x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 29; x *= 0x94D049BB133111EBull; x ^= x >> 32;
    if (x & 1) return 0;
    return (x & 2) ? (u32)(x >> 8) & 0x3F : (u32)(x >> 32);
}
static u32 calls_so_far;

// A small, repeatable random number generator: the same tests every time.
static uint64_t random_state = 0x9E3779B97F4A7C15ull;
static u32 random32() {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return (u32)(random_state >> 16);
}

// Where a called function's stand-in starts drawing its results from: one state per call, from the test's seed and the call's
// number, so the original and the converted function get the same results. Parts 1 to 3 use the seed XOR the number times a
// constant. xorshift is linear, so the draws of different calls in one test were tied together: in 200,000 tests no two of the
// first four calls ever both returned 0 (scratch/lift_cover/reasons/standin_corr.py), and a function that goes on only when four
// calls in a row say no (func_8003A050) was never reached. A test of part 4's own (mixed_calls, see Part 4) mixes the two through
// splitmix64's finalizer instead, a nonlinear hash: each call's draws are then as good as independent of every other call's. Parts
// 1 to 3 keep the old way exactly, so what they test and prove is unchanged.
static bool mixed_calls = false;
static uint64_t call_seed(u32 number) {
    if (!mixed_calls) return (test_seed ^ ((uint64_t)number * 0x9E3779B97F4A7C15ull)) | 1;
    uint64_t z = test_seed + (uint64_t)number * 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (z ^ (z >> 31)) | 1;
}

static u32 random_pointer(u32 align) { return ARENA + (random32() % (ARENA_BYTES - 0x40)) / align * align; }

// A pool of scratch-register values for the ordinary stand-ins (2026-11-09, scratch/lift_fast). Most of a call-heavy function's test was
// fuzz_stand_in drawing 54 random values per call for registers no correct caller reads afterwards: a stand-in called 12,000 times in a
// run was 80% of the test (func_80023E9C). Those registers' values only have to be rubbish, a different rubbish each call. So they come
// from 4096 values of each kind, drawn once as random_word and random_float_bits draw them, taken from a place the call's own random
// numbers choose. What a caller may legitimately read -- v0, v1, f0, f1 -- is still drawn fresh every call, as before.
static const u32 POOL_MASK = 4095;
static u32 word_pool[POOL_MASK + 1], float_pool[POOL_MASK + 1];

static u32 random_float_bits() {
    static const float common[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 100.0f, -100.0f, 1e-20f, 1e20f, 3.14159f};
    u32 bits;
    float v;
    switch (random32() % 4) {
    case 0: v = common[random32() % (sizeof(common) / sizeof(common[0]))]; break;
    case 1: v = (float)(s32)(random32() % 2001) - 1000.0f; break;
    case 2: v = (float)random32() / 4294967296.0f; break;
    default: { u32 r = random32(); std::memcpy(&v, &r, 4); break; }
    }
    std::memcpy(&bits, &v, 4);
    return bits;
}

// A double's 64 bits, drawn as random_float_bits draws a float's (TESTER CHANGE for 0.81, 2026-11-12, agent "refused2"; double
// parameters approved by the user 16:3x): a double parameter, f12:f13 or f14:f15 ('d' in a function's params). Only a function
// with one draws it, so no other function's tests change. A number, never NaN or infinity, as random_number_bits for a float
// handed over in a word: libm's frexp loop in func_800326EC halves an infinity for ever, and keeps its count in a C++ local,
// so the converted run never reaches the access limit that stops the original's (one test hung the tester, 2026-11-12); a
// NaN operand's test is set aside anyway.
// (FUZZ_NO_DOUBLE: as before 0.81 -- a 'd' parameter drawn as a float in the even register's low word, no f13/f15 noted at a
//  call, no 64-bit result compared. For measuring this change alone, never for a proof. final081, 2026-11-12)
static const bool use_double = std::getenv("FUZZ_NO_DOUBLE") == nullptr;
static uint64_t random_double_any();
static uint64_t random_double_bits() {
    for (;;) {
        const uint64_t bits = random_double_any();
        if (((bits >> 52) & 0x7FF) != 0x7FF) return bits;
    }
}
static uint64_t random_double_any() {
    static const double common[] = {0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 100.0, -100.0, 1e-20, 1e20, 3.141592653589793};
    double v;
    uint64_t bits;
    switch (random32() % 4) {
    case 0: v = common[random32() % (sizeof(common) / sizeof(common[0]))]; break;
    case 1: v = (double)(s32)(random32() % 2001) - 1000.0; break;
    case 2: v = (double)random32() / 4294967296.0; break;
    default: bits = (uint64_t)random32() << 32; bits |= random32(); std::memcpy(&v, &bits, 8); break;
    }
    std::memcpy(&bits, &v, 8);
    return bits;
}

// A float parameter's bits that are a number: not NaN or infinity. The N64 stops on a NaN operand (an unimplemented-operation
// exception) and the translated code asserts there are none (NAN_CHECK), so which NaN comes out is no part of the game. On x86 it
// depends on the order the compiler puts an addition's operands in, which the original and the converted needn't share
// (func_8011CCAC, 2026-11-07).
static u32 random_number_bits() {
    for (;;) {
        u32 bits = random_float_bits();
        if (((bits >> 23) & 0xFF) != 0xFF) return bits;
    }
}

// The numbers the function under test has in its own instructions (2026-11-07). A switch over message numbers compares a parameter
// with 0x642, 0x619, -0x510E...: random words almost never hit one, so most of such a function was never reached and it couldn't be
// proved (func_8013A0A4: 378 of 515 instructions). Drawing on them sometimes reaches those paths. It only adds inputs: the comparison
// and the rule that every instruction must have run are the same.
static const u32* dictionary = nullptr;
static int dictionary_count = 0;

// What the function's compares compare with, and the numbers either side (2026-11-07): K-1, K and K+1, where a compare's outcome
// changes (lift.py fuzz_compare_constants). Only those show whether the converted code compares the same value the same way:
// func_8014434C compared the value from after a branch's delay slot (s0 - 1 where the N64 compares s0 with 4), which differs only
// when s0 is 4 or 5. The dictionary has the constants too, but among every immediate the function has, and with a neighbour only
// sometimes. Part 3 draws on these, for one word in four of a test's start (boundary_start: memory, registers and parameters, not
// what the stand-ins hand back), each whole, in either halfword or in every byte, as dictionary_word does.
static const u32* compare_dictionary = nullptr;
static int compare_count = 0;
static bool boundary_start = false;     // (part 3's make_start)
static bool in_part3 = false;

static u32 compare_word() {
    u32 c = compare_dictionary[random32() % compare_count];
    switch (random32() % 4) {
    case 0: case 1: return c;
    case 2: return (c << 16) | (c & 0xFFFF);          // (in either half, for a halfword read from it)
    default: return (c & 0xFF) * 0x01010101u;          // (in every byte)
    }
}

static u32 dictionary_word() {
    u32 c = dictionary[random32() % dictionary_count];
    switch (random32() % 6) {
    case 0: case 1: return c;
    case 2: return c + 1;
    case 3: return c - 1;
    case 4: return (c << 16) | (c & 0xFFFF);          // (in either half, for a halfword read from it)
    default: return (c & 0xFF) * 0x01010101u;          // (in every byte)
    }
}

// A value for an integer register or a word of memory, in the kinds real game data is made of.
static u32 random_word() {
    if (boundary_start && compare_count != 0 && random32() % 4 == 0) return compare_word();    // (part 3 only: see compare_word)
    if (dictionary_count != 0 && random32() % 8 == 0) return dictionary_word();
    u32 r = random32();
    switch (r % 16) {
    case 0: case 1: case 2: case 3: return 0;
    case 4: case 5: case 6: return random32() % 256;
    case 7: return (u32)-(s32)(random32() % 256);
    case 8: return random32() % 65536;
    case 9: case 10: case 11: return random_pointer(4);
    case 12: return random_float_bits();
    case 13: {
        static const u32 edges[] = {1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000};
        return edges[random32() % (sizeof(edges) / sizeof(edges[0]))];
    }
    default: return random32();
    }
}

static inline u32& word_at(u32 a) { return *(u32*)(lift_rdram + (a - 0x80000000u)); }

// Memory is reserved, not committed: a page only becomes real when something touches it. (Committing all 4 GB up front charged 4 GB
// against Windows' memory limit per tester, and with four running beside the game that ran the computer short, 2026-11-05.) Pages
// a wild pointer touched are given back now and then: they only ever held zeros, which is what they come back as.
static const u64 KEPT_BYTES = 0x01000000;      // The first 16 MB stay: the filled part, the arena, the stack.
static std::vector<uint8_t*> touched_pages;
// The mod's sections, where the tester puts them, for a test of one of the mod's copies (tools/lift/modrep.py): given random
// contents at the start and kept, as the first 16 MB are, so what a copy reads from the mod's data is random and not zeros.
// (With zeros there, an edit that was lost, or a place left as it was linked, read the same zeros as the copy and showed nothing:
// 14 of 57 lost edits went unseen, 2026-11-09.) Off (0 bytes) for every other function, whose tests stay exactly as they were.
static u64 mod_fill_start = 0, mod_fill_end = 0;
__attribute__((weak)) void fuzz_mod_region(u32* start, u32* bytes) { *start = 0; *bytes = 0; }   // (lift.py writes the real one)

static LONG WINAPI commit_on_touch(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    uint8_t* where = (uint8_t*)e->ExceptionRecord->ExceptionInformation[1];
    if (where < lift_rdram || where >= lift_rdram + MEMORY_BYTES) return EXCEPTION_CONTINUE_SEARCH;
    uint8_t* page = lift_rdram + ((where - lift_rdram) & ~(u64)0xFFFF);
    if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE) == nullptr) return EXCEPTION_CONTINUE_SEARCH;
    if ((u64)(page - lift_rdram) >= KEPT_BYTES && !((u64)(page - lift_rdram) >= mod_fill_start && (u64)(page - lift_rdram) < mod_fill_end)) touched_pages.push_back(page);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void give_back_touched_pages() {
    for (uint8_t* page : touched_pages) VirtualFree(page, 0x10000, MEM_DECOMMIT);
    touched_pages.clear();
}

// A change part 2 makes to a test's start (see Steering): a word of memory, a parameter register, or the result the stand-in for
// the call-th function called hands back (kind CALL_V0, CALL_V1, CALL_F0; `where` is the call's number, from 0), or a word it writes
// through a pointer it was given (CALL_OUT; `where` is the call's number << 8 | the parameter's << 5 | the word's).
// Part 4 adds two (see Part 4): CALL_WRITE, a word of memory the call-th stand-in writes before it returns (`at`: the word's offset
// in memory), and CALL_REG, a register the call-th stand-in hands back changed (`at`: the register's bit, as abi_out's: GPR n is
// n, float register n is 32 + n), for abi.py's functions only. And CALL_OUT's parameter can be 4 to 7 there: the 1st to 4th
// parameter passed on the stack.
enum TweakKind : u8 { MEMORY_WORD, REGISTER, FLOAT_REGISTER, CALL_V0, CALL_V1, CALL_F0, CALL_OUT, CALL_WRITE, CALL_REG };
struct Tweak { u8 kind; u32 where; u32 value; u32 at = 0; };
static const std::vector<Tweak>* result_tweaks = nullptr;    // The test's tweaks, while part 2 runs one.
// (2026-11-09) Part 1 too: about half its tests have the called functions fill in what they're given a pointer to, as part 2's do (see
// steer_stand_in). Without it a function part 1 wholly reached had every callee-filled local tested only as what the frame held,
// usually 0: `lb` read as `lbu` from one (func_8000AB9C) or a `trunc.w.s` of one stored as a float (func_800EB970) showed no
// difference. Which tests fill comes from a generator of its own, so part 1's starts are the same ones in the same order as before.
// (FUZZ_NO_PART1_FILLS: without it.)
static bool part1_fills = false;
static bool part1_fill_coin() {
    static uint64_t state = 0x9E3779B97F4A7C15ull;
    static const bool off = std::getenv("FUZZ_NO_PART1_FILLS") != nullptr;
    state ^= state << 13; state ^= state >> 7; state ^= state << 17;
    return !off && ((state >> 32) & 1);
}
// ---- A function none of whose plain tests is ever compared (2026-11-12, scratch/agents/neverran) ----
// A start re-randomizes only the arena, the words near each fixed address the function names (randomize_memory) and its
// stack parameters. The rest of the first 4 MB keeps what it was given once, before the first test. Most functions never
// notice. For some, that fixed memory loses every test the same way:
//   - func_80022CEC reads 36 floats from 0x80084410 on, but its only constant there is 0x80080000 (a `lui`), so the floats
//     are the same in every test, and one of them is a NaN: every test was set aside (nan_set_aside). The same for
//     func_800DCC9C and func_80120560;
//   - func_8001EB18 walks twelve records it doesn't name past the first, and one has a count byte that is negative, which
//     its unsigned loop takes for four billion: every test ran past the access limit.
// And a loop with a fixed count longer than the limit (func_800033B8 clears 2 x 57,600 words) runs past it in every test, in
// both versions, so nothing is compared.
// So for a function none of whose first FALLBACK_AFTER part-1 tests was compared (fallback):
//   - every word of the filled memory its original has read in part 1 (fresh_words: learned from the first test on, kept
//     from part 1's end) is given a fresh random word in every later start, as the arena is (randomize_fresh_words);
//   - an original's NaN operand stops it in part 1 too: the test is set aside either way (run_original);
//   - a test where both versions ran past the access limit is run again with 16 times the room, and the one that still does
//     with 64 times, as part 4 does for some of its tests (run_both, long_retry).
// Neither changes how a test is compared, only which tests can be. A function any of whose first FALLBACK_AFTER tests was
// compared is tested exactly as before: the same starts, the same limits. (FUZZ_NO_FALLBACK: never.)
static const long FALLBACK_AFTER = 256;
static bool fallback = false;                   // (the function is in this mode: see above)
// (Proposed 2026-11-12 by agent "tail", APPROVED by the user 2026-11-12. FUZZ_NO_FALLBACK64: never.) And a function still with no test
// compared after 2 x FALLBACK_AFTER part-1 tests in fallback (fallback64): a test where BOTH versions ran past 16 times the
// limit is run again, the original first, with 64 times; the converted one too when the original finished there. Then they
// are compared as any test is. func_80144CC4 runs a fixed 22 x 5 x 24 x 24 = 63,360 calls: about 586,000 accesses in the
// original (four spills and four reloads a call) and 333,000 in the converted function (the four spills only) -- both past
// 16 x 20,000, both under 64 x, so no test of it was ever compared (0 of 59,991). The 64x step already exists for a test
// where only ONE version ran past 16x; this gives it to the test where both did. A function with any test compared by then
// (func_8001EB18: its count bytes make most tests truly endless) is tested exactly as before, so its runaway tests cost no more.
static bool fallback64 = false;
static bool fresh_learning = false;             // (part 1, until it ends or a test is compared: note the original's reads)
static std::vector<u32> fresh_words;            // (offsets into memory, as FuzzMem's: the words given fresh contents)
static std::unordered_map<u32, bool> fresh_seen;    // (the same, to add each once)
static const size_t MOST_FRESH_WORDS = 1 << 16;
static bool running_original = false;                        // Which version is running (part 2's stand-ins decide in the original's).
static bool running_original_flag() { return running_original; }
static std::vector<std::vector<std::pair<u32, u32>>> call_outputs;   // What each call wrote through its pointers, by call number.
// (2026-11-11, part 4 only) And of the words in the caller's frame, which stand-in filled each last, through which parameter
// and which word from where it pointed, as CALL_OUT's `where` has them (call << 8 | parameter << 5 | word), and with what: so
// part 4 can take such a word as a source of a comparison, and change it through that stand-in (see learn4, frame_fill_of).
static std::unordered_map<u32, std::pair<u32, u32>> frame_fills;
// (2026-11-11, steering only) The pointers into the caller's frame or the random memory that the calls so far in this run
// were given, latest last: for a stand-in that reads back what an earlier call was given (steer_stand_in, `echo`).
static std::vector<u32> buffers_given;

// ---- What part 4 notes of the original's run (see Part 4, below) ----
// In order, everything the original's path took a value from: each word of memory it read (its value then, and how many calls
// had been made before it), and each call's results as it returned. And each comparison its branches and set-less-thans made,
// equal sides or not, with where it came in that order: the reads and results just before a comparison are, most often, what
// it compares (a field loaded, maybe added to, then compared), so that is what part 4 changes to turn the comparison the other
// way. Only while part 4 runs a test's original; nothing here is seen by parts 1 to 3.
static bool in_part4 = false;
enum EventKind : u8 { READ_EVENT, RESULT_EVENT };
struct Event4 { u8 kind; u32 where; u32 value; u32 calls; u32 filled = 0; };    // (READ_EVENT: where = the word's offset; RESULT_EVENT: the call's number)
// (filled, 2026-11-11: a READ_EVENT of a word in the caller's frame that a stand-in filled, still holding what it put there --
//  CALL_OUT's `where` + 1, the call << 8 | parameter << 5 | word -- or 0. See frame_fill_of.)
// (event: how many events came before it; site, op, taken, fall: which comparison, as FUZZ_BRANCH_AT in fuzz.h gives them)
struct Compare4 { u32 a, b; u32 event; u32 site; int op, taken, fall; double x = 0, y = 0; };   // (x, y: a float comparison's values)
struct CallResult4 { uint64_t may_write; u32 regs[64]; };       // (a call's registers as it returned, by abi_out's bits)
static std::vector<Event4> events4;
static std::vector<Compare4> compares4;
static std::vector<CallResult4> results4;
static u32 read_word_at(uint64_t offset) { return *(u32*)(lift_rdram + offset); }

// The words the original reads, while part 2 notes them (FuzzMem in fuzz.h): what its path depended on, so what to change.
bool lift_fuzz_noting_reads = false;
std::vector<u32> lift_fuzz_reads;
// (2026-11-11) Which stand-in filled this word of the caller's frame, if one did and it still holds what that one put there:
// CALL_OUT's `where` + 1, or 0. The latest call's fill first. Part 4 only, in the original's run.
static u32 frame_fill_of(u32 offset, u32 value) {
    const u32 address = offset + 0x80000000u;
    if (address < STACK - 0x4000u || address >= STACK || frame_fills.empty()) return 0;
    const auto it = frame_fills.find(address);
    return it != frame_fills.end() && it->second.second == value ? it->second.first + 1 : 0;
}
// (2026-11-12, fallback) The words of the filled memory a part-1 original just read -- the first 4 MB, and for one of the
// mod's copies the mod's sections (main) -- added to fresh_words: each once, and not the arena's, which every start makes
// fresh anyway.
static bool in_mod_fill(u32 o) { return o >= mod_fill_start && o < mod_fill_end; }
// (2026-11-12, scratch/agents/modnative1) The mod's read-only image, where a test of a native mod function has it (main,
// fuzz_mod_image): it is the game's constant data, so no part of the tester makes it fresh or steers it -- its reads are not
// noted for that. Empty (0, 0) for every other function.
#if FUZZ_MOD_NATIVE     // (2026-11-14, fastnight: the mod's native functions only -- fuzz.h FUZZ_MOD_NATIVE)
static u64 mod_image_start = 0, mod_image_end = 0;
__attribute__((weak)) const char* fuzz_mod_image(const char* function, u32* start, u32* bytes) { (void)function; *start = 0; *bytes = 0; return nullptr; }
#endif
static void learn_fresh_words() {
    for (u32 o : lift_fuzz_reads) {
        if (fresh_words.size() >= MOST_FRESH_WORDS) break;
        if (o >= FILLED_BYTES && !in_mod_fill(o)) continue;
        if (o >= ARENA - 0x80000000u && o < ARENA - 0x80000000u + ARENA_BYTES) continue;
        if (fresh_seen.emplace(o, true).second) fresh_words.push_back(o);
    }
}
void lift_fuzz_note_read(uint64_t offset) {
#if FUZZ_MOD_NATIVE
    if (offset >= mod_image_start && offset < mod_image_end) return;     // (the mod's read-only image: see mod_image_start)
#endif
    if (lift_fuzz_reads.size() < 8192) lift_fuzz_reads.push_back((u32)offset & ~3u);
    // (Part 4: the word as it is now, which is what this read is reading.)
    if (in_part4 && running_original && events4.size() < 16384) {
        const u32 value = read_word_at(offset & ~3ull);
        events4.push_back({READ_EVENT, (u32)offset & ~3u, value, calls_so_far, frame_fill_of((u32)offset & ~3u, value)});
    }
}
// The originals' 64-bit loads (fuzz.h): read as recomp.h's load_doubleword does, the low word first, and in part 4 noted as two
// word reads (high word first, as it is in memory), so part 4 can find a double the original compared (float_tweak4). Outside
// part 4 exactly as before: not noted, not counted.
uint64_t fuzz_load_doubleword(uint8_t* rdram, gpr offset, gpr reg) {
    const gpr address = offset + reg;
    // (2026-11-10) Each word's place kept to 32 bits, as FuzzMem::offset keeps every other access of the original's and
    // lift_read32 the converted code's. These two weren't: a pointer just under 0x80000000 -- the random words' edge case
    // 0x7FFFFFFF -- put the original's low word past the top of memory, at 0x100000003, which reads zeros, while the
    // converted function's ldc1 wrapped round to 0x80000003 and read the random bytes there. func_800DEF28 (float-return,
    // 0x80000000 against 0) and func_8003F150 (the double it tests against 0.0) differed for that alone.
    const uint64_t low = (uint32_t)*(int32_t*)(rdram + (uint32_t)((uint32_t)address + 4u - 0x80000000u));
    const uint64_t high = (uint32_t)*(int32_t*)(rdram + (uint32_t)((uint32_t)address - 0x80000000u));
    if (lift_fuzz_noting_reads && in_part4) {
        lift_fuzz_note_read((u32)((u32)address - 0x80000000u));
        lift_fuzz_note_read((u32)((u32)address + 4 - 0x80000000u));
    }
    return low | high << 32;
}
// And the two sides of each comparison its branches and set-less-than instructions made (lift.py puts FUZZ_COMPARE before each),
// so part 2 can find where a side came from and put the other side there (as Redqueen's "input-to-state" does).
std::vector<std::pair<u32, u32>> lift_fuzz_compares;
// (2026-11-12, part 5 only) How many comparisons the original's run has made. A loop with no memory access in it never
// meets the access limit: func_8000CEEC's rows loop runs 1 << a byte times with nothing in it when a row is 1 pixel wide,
// 9 seconds of one test, and part 5's 40 seconds went in 8 such tests. So in part 5 an original that has made 64 times as
// many comparisons as the access limit allows accesses is stopped there and its test set aside, as one fed a NaN is (the
// converted function isn't run): one of part 5's own added tests, never compared -- and the next test is it made small
// (shrink5). Parts 1 to 4 run every test as they did.
static u32 compares_made = 0;
static bool compares_capped = false, compares_cut = false;
static void count_compare() {
    if (++compares_made > 64 * MOST_ACCESSES && compares_capped && running_original_flag()) {
        compares_cut = true;
        throw NotAGameState{};
    }
}
void lift_fuzz_note_compare(u32 a, u32 b, u32 site, int op, int taken, int fall) {
    count_compare();
    if (in_part4 && running_original && compares4.size() < 4096) compares4.push_back({a, b, (u32)events4.size(), site, op, taken, fall});
    if (a != b && lift_fuzz_compares.size() < 1024) lift_fuzz_compares.push_back({a, b});
}

static u32 lift_fuzz_float_bits(double x) {
    const float v = (float)x;
    u32 bits;
    std::memcpy(&bits, &v, 4);
    return bits;
}
// A float comparison (FUZZ_FLOAT_COMPARE in fuzz.h): noted as the others are, its sides as single floats' bits, and as they are.
void lift_fuzz_note_float_compare(double a, double b, u32 site, int op) {
    count_compare();
    if (in_part4 && running_original && compares4.size() < 4096) {
        compares4.push_back({lift_fuzz_float_bits(a), lift_fuzz_float_bits(b), (u32)events4.size(), site, op, -1, -1});
        compares4.back().x = a;
        compares4.back().y = b;
    }
}

// The scratch registers a stand-in hands back changed (fuzz_stand_in's: at, v0-t7, t8, t9 and f0-f19), by abi_out's bits. Part 4's
// CALL_REG only ever changes one of these, and only one the real callee may change: never a register a callee keeps.
static const uint64_t STAND_IN_SCRATCH = 0x000FFFFF0300FFFEull;
static u32* abi_register(recomp_context* c, int n);

// Part 2's additions to a stand-in, at the end of its call, before the random state is put back (so what it draws depends only on
// the test and the call's number, as the rest does): the results part 2 chose for this call, and the writes through the pointers
// it was given. `given`: its parameter registers as they came in. `may_write`: the registers the callee may change (abi.py's
// mask form; all of them for an ordinary stand-in), so a chosen result never lands in a register the real callee leaves alone.
// `stack_words`: how many parameters it takes on the stack (part 4 only: see below).
// (2026-11-10) An address in the frame the function under test is about to make (below the stack pointer it starts with) is
// never part of a start, nor a result a stand-in hands back. Nothing can hold such an address before the call: the frame
// doesn't exist yet. Part 2 made them all the same -- input_to_state and new_value's pool copy the sides of the original's
// own compares, and a loop over a local compares with `sp + 0x30` (func_80038DD0: `slt $v0, $a0, $a2`, a2 the end of its
// buffer). Planted in the word the function passes its callee, the stand-in then "filled" the function's saved s0 through
// it, and the calls' lists compared frame words that are spills in one version and nothing in the other (func_8003B528).
// A tweak whose value is one is left out where it would be applied; every other tweak is as it was.
// (2026-11-13, agent b0d8) Nor an address in the words just above that stack pointer that are the function's own: the four the
// calling rules keep for a0-a3 and the ones its stack parameters came in (own_memory_end). The caller stores those afresh for
// this one call and keeps nothing of its own there, so no pointer the game holds before the call points at them. Part 2 planted
// one, 0x80600000 (the stack pointer the test starts with), in func_8013B0D8's 7th parameter (and its 5th, 9th and 11th): a
// pointer its callee func_80143070 is given, so the stand-in noted the words there -- among them the 5th parameter's, which
// the original divides and stores back (`sw $t4, 0xB8($sp)`) and the converted function keeps in its C++ parameter. That
// showed calls@61:converted=80600000,original=7FA00000 (the parameter as it came, and divided by -1), a difference no game
// caller can see, in 1 test of 450,000 (0.79 and 0.80's reach pass, seed 2).
static bool own_frame_address(u32 value) { return value >= STACK - 0x4000u && value < (u32)(own_memory_end() + 0x80000000u); }

static bool is_kept_slot(u32 address);     // (below, beside kept_slots)
// (2026-11-11) A word of the caller's frame its own code wrote that a steered stand-in may fill all the same: one the converted
// function wrote too in an earlier run (converted_frame_words) -- a local both versions keep in game memory, such as the
// array whose address the caller passes, which a real callee given that address may fill -- and no saved register's slot.
// A spill or anything else only the original keeps in memory stays out, as before: the converted function holds it in a C++
// variable, and a stand-in changing it would only show the tester's difference. Parts 2 and 4 only (steering_now): part 1
// fills exactly as it did. (func_80038BD0 fills a 32-byte local with 0xFE and calls on to have it filled: its whole second
// half needs the callee to have changed it, which no test could give it.)
static bool steering_now_flag();
static bool shared_frame_local(u32 a) { return use_shared_fill && steering_now_flag() && converted_wrote_frame_word(a) && !is_kept_slot(a); }

// (2026-11-14, approved by the user: the private-object guard -- see "The private-object guard" below) A word a stand-in writes
// for its callee, through the guard.
static void stand_in_store(u32 a, u32 value);
static void steer_stand_in(u32 parameter_mask, const u32 given[4], u32 stack_words, u32 sp, recomp_context* c, uint64_t may_write) {
    gpr* r = &c->r0;
    if (result_tweaks != nullptr) {
        // A result part 2 chose for this call (both versions get it: it goes by the call's number, as everything here does).
        for (const Tweak& w : *result_tweaks) {
            if (w.where != calls_so_far - 1) continue;
            if (w.kind != CALL_F0 && own_frame_address(w.value)) continue;     // (see own_frame_address)
            if (w.kind == CALL_V0 && (may_write >> 2 & 1)) r[2] = (gpr)(int64_t)(s32)w.value;
            else if (w.kind == CALL_V1 && (may_write >> 3 & 1)) r[3] = (gpr)(int64_t)(s32)w.value;
            else if (w.kind == CALL_F0 && (may_write >> 32 & 1)) c->f0.u32l = w.value;
            else if (w.kind == CALL_REG && w.at < 64 && ((may_write & STAND_IN_SCRATCH) >> w.at & 1)) {
                // (Part 4, abi.py's functions: a result in a register other than v0, v1 and f0, which their callers read. A whole
                //  GPR is sign-extended, as the N64 keeps a 32-bit value.)
                if (w.at < 32) r[w.at] = (gpr)(int64_t)(s32)w.value;
                else *abi_register(c, (int)w.at) = w.value;
            }
        }
    }
    if (result_tweaks != nullptr || part1_fills) {
        // Part 2, and half of part 1's tests (part1_fills): a called function that fills in what it's given a pointer to. A function that passes a local's address or a
        // structure to fill reads the result after the call, and a stand-in that never wrote there left that path unreached
        // (func_8000C8CC: a size func_80003BD0 writes into its frame). For each parameter that points into the caller's frame (between
        // the stand-in's stack pointer and the top of the stack) or into the random memory, sometimes, up to 32 words from it get
        // random values (or values part 2 chose, CALL_OUT). Never a word the test has written already (but the first: see below):
        // that's a saved register or something the caller put there, which a real callee wouldn't touch. Which words and what goes in them is worked out in the
        // original's run and repeated exactly in the converted one's (by the call's number), so both get the same callee.
        const u32 call = calls_so_far - 1;
        if (running_original) {
            if (call_outputs.size() <= call) call_outputs.resize(call + 1);
            std::vector<std::pair<u32, u32>>& out = call_outputs[call];
            out.clear();
            // (Parameter i, holding the pointer given_pointer.)
            auto fill = [&](u32 i, u32 given_pointer) {
                const u32 p = given_pointer & ~3u;
                const bool in_frame = p >= sp && p < STACK, in_arena = p >= ARENA && p < ARENA + ARENA_BYTES;
                if ((!in_frame && !in_arena) || random32() % 2 == 0) return;
                // (2026-11-11, parts 2 and 4 only) A quarter of the time the words are those at a pointer an earlier call
                // was given, as they are now: a callee that reads back what another was handed to write. A routine that
                // writes a block to a device, reads it back into another local and compares the two byte by byte
                // (func_8003B528: 32 bytes) goes on only when they match, which random words never do. Like every fill,
                // it is worked out here in the original's run and repeated exactly in the converted one's.
                u32 echo = 0;
                if (use_echo && steering_now && !buffers_given.empty() && random32() % 4 == 0) {
                    for (size_t b = buffers_given.size(); b-- > 0 && echo == 0;) {
                        if (buffers_given[b] != p) echo = buffers_given[b];
                    }
                }
                const u32 echo_end = echo >= ARENA && echo < ARENA + ARENA_BYTES ? ARENA + ARENA_BYTES : STACK;
                // (32 words: func_800078B8's callee fills a structure whose flags it tests 0x34 bytes in.)
                for (u32 k = 0; k < 32; k++) {
                    const u32 a = p + 4 * k;
                    if (a >= (in_frame ? STACK : ARENA + ARENA_BYTES)) break;
                    const uint64_t o = a - 0x80000000u;
                    const bool written = was_written(o) || was_written(o + 1) || was_written(o + 2) || was_written(o + 3);
                    // (Except the very word the pointer points at: a caller that sets a result to 0 and passes its address for
                    //  the callee to fill (func_8002DD00) has written it, and it is no saved register or spilled value, which
                    //  nobody passes the address of.)
                    if (written && k != 0 && !(in_frame && shared_frame_local(a))) continue;
                    u32 v = echo != 0 && echo + 4 * k < echo_end ? word_at(echo + 4 * k) : random_word();
                    if (result_tweaks != nullptr) for (const Tweak& w : *result_tweaks) {
                        if (w.kind == CALL_OUT && w.where == ((call << 8) | (i << 5) | k) && !own_frame_address(w.value)) v = w.value;
                    }
                    out.push_back({a, v});
                    if (use_filled_sources && in_part4 && in_frame) frame_fills[a] = {(call << 8) | (i << 5) | k, v};
                }
            };
            for (u32 i = 0; i < 4; i++) {
                if (parameter_mask & (1u << i)) fill(i, given[i]);
            }
            for (u32 i = 0; i < 4 && steering_now && use_echo; i++) {
                const u32 p = given[i] & ~3u;
                if ((parameter_mask & (1u << i)) && ((p >= sp && p < STACK) || (p >= ARENA && p < ARENA + ARENA_BYTES))) buffers_given.push_back(p);
            }
            // Part 4 only: a pointer passed on the stack is filled in the same way (func_8003A050 passes the address of a local
            // as its callee's 5th parameter, sw $s4, 0x10($sp), and goes on only when the callee has set it to 1). The callee's
            // stack parameters are at its stack pointer + 0x10 on, as the calling rules have them.
            if (in_part4) {
                for (u32 s = 0; s < stack_words && s < 4; s++) fill(4 + s, word_at(sp + 0x10 + 4 * s));
            }
        }
        if (call < call_outputs.size()) {
            for (const std::pair<u32, u32>& w : call_outputs[call]) {
                stand_in_store(w.first, w.second);
            }
        }
        if (in_part4 && result_tweaks != nullptr) {
            // Part 4 only: a word of memory the callee changes (CALL_WRITE), one the original's path reads after the call -- a
            // global it set before the call and tests after it (func_80136CC8 sets 0x8053BCA8 to -1, calls func_8013697C, and goes
            // on only when that changed it), a flag in a structure. A real callee may write anywhere outside its caller's frame,
            // and a converted function must read such a word again after the call, as the original does. Never the stack (the
            // caller's frame and what is above it: saved registers, locals, its own parameters), unless the stand-in itself is
            // filling it through a pointer it was given (just above), as a real callee filling a local would. The same tweak
            // goes to both versions, by the call's number.
            const uint64_t stack_low = (STACK - 0x4000u) - 0x80000000u, stack_high = (STACK + 0x60u) - 0x80000000u;
            for (const Tweak& w : *result_tweaks) {
                if (w.kind != CALL_WRITE || w.where != call || own_frame_address(w.value)) continue;
                const uint64_t o = w.at & ~3u;
                if (o >= stack_low && o < stack_high) {
                    bool passed = false;
                    if (call < call_outputs.size()) {
                        for (const std::pair<u32, u32>& p : call_outputs[call]) passed = passed || p.first - 0x80000000u == o;
                    }
                    if (!passed) continue;
                }
                stand_in_store((u32)o + 0x80000000u, w.value);
            }
        }
    }
}

// Part 4 notes each call's results as it returns (only in the original's run, while it notes reads): `may_write`, the registers
// this callee may hand back changed, and every register's value.
static void note_result4(recomp_context* c, uint64_t may_write) {
    if (!(in_part4 && running_original && lift_fuzz_noting_reads)) return;
    const u32 call = calls_so_far - 1;
    if (events4.size() < 16384) events4.push_back({RESULT_EVENT, call, 0, calls_so_far});
    if (call < 256) {
        if (results4.size() <= call) results4.resize(call + 1);
        results4[call].may_write = may_write;
        for (int b = 0; b < 64; b++) results4[call].regs[b] = *abi_register(c, b);
    }
}

// (2026-11-13, agent ce80. TESTER CHANGE) One word of the stack a stand-in's parameter points into, noted in the list of calls
// (the stand-ins' note_word). A word that is the function's own -- its home slots and stack parameters' words (own_memory_end)
// -- goes in with a mark (OWN_WORD) and its place. run_both compares those words' values as before, unless the converted
// function has never written any of its own words (converted_own_words, as both runs of the test have left it), that is, it
// keeps all its parameters in C++ variables: then only the mark and place are compared (settle_own_words). Every other word
// is noted as before.
// Why: no pointer the game holds points there (see own_frame_address), and a function makes one only by taking a parameter's
// address, which lift.py then keeps in its slot (parameter_slots) and the converted function writes there at its start, as
// the original does -- so such a function's own words are all still compared. But a number a function computes can look like
// such a pointer. func_8011CE80, given a display-list pointer of 2 (a steered tweak of the word its 1st parameter points to),
// wrote its 0x01008060 command at address 2 (a store the N64 itself would refuse, unaligned), so the word at address 0 became
// 0x80600000 -- the tester's stack pointer -- and, its 4th parameter being 0, it read that word and passed it on as
// func_8011BF68_ovl_c's a2. The 8 words noted there held its 5th parameter's, which the original adds 2 to each pass and
// stores back (`sw $t2, 0xB0($sp)`) and the converted function keeps in `arg4`: calls@BA:converted=A8,original=B0, in 1 test
// of 450,000 (0.81's reach pass, seed 3), a difference no game caller can see. (The memory compared at the end has left those
// words out since 0.81: own_memory_end.)
static const uint64_t OWN_WORD = 1ull << 62;
static inline void note_stack_word(u32 a) {
    if (a >= STACK && a < (u32)(own_memory_end() + 0x80000000u))
        call_trace.push_own(OWN_WORD | ((uint64_t)((a - STACK) / 4) << 32) | word_at(a));    // (marked by position: Trace)
    else
        call_trace.push_back(word_at(a));
}
// (Before the two lists are compared, for a converted function that keeps none of its own words in memory: each of those
//  words keeps its mark and place only.)
static void settle_own_words(Trace& trace) {
    if (converted_own_words != 0) return;
    for (uint32_t i : trace.own) trace.v[i] &= ~0xFFFFFFFFull;    // (only the entries note_stack_word marked: see Trace)
}

#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
// ---- The argument compare (FUZZ_ARGS_STRICT=1; 2026-11-16, agent argcheck. TESTER CHANGE, approved by the user 2026-11-16 with an
//      independent review before anything counts from it: scratch/agents/argcheck/REVIEW_ARGCHECK.txt) ----
// Each stand-in notes only the parameter registers its callee's analysis.json `use` lists. When that list is short -- 0.83's
// analysis took `sw $s5, 0x18($sp)`, a stack parameter, for a saved register's save, so func_80117714 "read" a0-a2 -- a converted
// caller that drops the register is compared on nothing that shows it (21 converted callers, scratch/agents/reviewpt2). So, at
// every stand-in call, every argument register (a0-a3, f12, f13, f14, f15) whose value the ORIGINAL changed since the stand-in
// before returned (since entry, for the first call) is compared with what the converted function had there, and so is every
// stack-parameter word ($sp + 0x10 on) the original's own code wrote since then. Skipped only: a register the strict read-set
// checker (readcheck.py, its raw reads, no independence) proves the callee never reads, and a stack word it proves the callee
// never reads (for a callee it can't see: the words below the caller's own locals, readcheck.py outgoing_words). The value is
// compared, not its use: a difference is named args@<call>:<register> (the call counted from 0, in hex as the others).
static const char* const ARG_NAMES[8] = {"a0", "a1", "a2", "a3", "f12", "f13", "f14", "f15"};
static u32 args_next_skip = 0, args_next_stack = 0xFFFFFFFFu;     // (fuzz_args_next: for the next stand-in only)
static bool args_next_given = false;
static uint64_t args_last[8];          // (the argument registers as the last stand-in left them, or as at entry)
// (Per stack word from STACK - 0x4000: the args_epoch when the function's own code last wrote it. args_epoch moves on at each
//  run's start and after each recorded call -- only there, not at a port call or a hook, which make no record (reviewac G2)
//  -- so "written since the call before" is stamp == args_epoch, and a stamp from an earlier run never matches: no clear.)
static uint64_t args_stamp[(0x4000u + 0x60u) / 4];
static uint64_t args_epoch = 0;
static u32 args_outgoing = 16;         // (the function under test's outgoing words: fuzz_args_outgoing)
__attribute__((weak)) u32 fuzz_args_outgoing(const char* function) { (void)function; return 16; }
void fuzz_args_next(u32 skip, u32 stack) { args_next_skip = skip; args_next_stack = stack; args_next_given = true; }
static inline void args_registers(const recomp_context* c, uint64_t* out) {
    for (int i = 0; i < 4; i++) out[i] = (uint64_t)(&c->r0)[4 + i];
    out[4] = c->f12.u32l; out[5] = c->f12.u32h; out[6] = c->f14.u32l; out[7] = c->f14.u32h;
}
// At the start of each run (run_version): nothing written yet, and the registers as the function is given them.
static void args_begin(const FuzzFunction& f, const recomp_context* c) {
    args_epoch++;
    args_registers(c, args_last);
    args_next_given = false;
    args_outgoing = fuzz_args_outgoing(f.name);
    if (args_outgoing > 16) args_outgoing = 16;
}
static void args_note_write(uint64_t offset, u32 bytes) {
    if (in_stand_in || !running_original_flag()) return;      // (the original's own code only: not a stand-in's writes)
    const uint64_t low = (STACK - 0x4000u) - 0x80000000u, high = (STACK + 0x60u) - 0x80000000u;
    for (uint64_t o = offset & ~3ull; o < offset + bytes; o += 4) {
        if (o >= low && o < high) args_stamp[(o - low) / 4] = args_epoch;
    }
}
// At a stand-in's start, before it notes anything: what it was given, and (the original's run) what changed since the last one.
static void args_enter(u32 which, const recomp_context* c) {
    ArgCall a;
    std::memset(&a, 0, sizeof(a));
    a.which = which;
    a.skip = args_next_given ? args_next_skip : 0;
    u32 stack = args_next_given ? args_next_stack : 0xFFFFFFFFu;
    if (stack == 0xFFFFFFFFu) stack = args_outgoing >= 16 ? 0xFFFFu : (1u << args_outgoing) - 1;
    a.stack = stack & 0xFFFFu;
    args_next_given = false;
    uint64_t now[8];
    args_registers(c, now);
    for (int i = 0; i < 8; i++) {
        a.regs[i] = (u32)now[i];
        if (now[i] != args_last[i]) a.changed |= 1u << i;
    }
    const u32 sp = (u32)c->r29;
    for (u32 j = 0; j < 16; j++) {
        const u32 at = sp + 0x10 + 4 * j;
        a.stack_words[j] = word_at(at);
        if (at >= STACK - 0x4000u && at < STACK + 0x60u && args_stamp[(at - (STACK - 0x4000u)) / 4] == args_epoch) a.written |= 1u << j;
    }
    call_trace.args.push_back(a);
    args_epoch++;
}
// At a stand-in's end: the registers as it leaves them, for the next call's "changed".
static inline void args_leave(const recomp_context* c) { args_registers(c, args_last); }
#endif

void fuzz_stand_in(u32 which, u32 parameter_mask, u32 stack_words, recomp_context* c) {
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    args_enter(which, c);
#endif
    lift_fuzz_count_access();
    in_stand_in = true;      // (its writes aren't the converted function's own: note_converted_frame_write)
    // Its stack pointer, and what it reads.
    u32 sp = (u32)c->r29;
    call_trace.push_back(which);
    call_trace.push_back(sp);
    const u32 stack_low = STACK - 0x4000, stack_high = STACK + 0x60;
    auto note_word = [&](u32 v) {
        call_trace.push_back(v);
        if (v >= stack_low && v < stack_high) {
            // A pointer into the stack: what's there has to match too, since that's a local being passed.
            for (u32 a = v & ~3u; a < (v & ~3u) + 0x20 && a < stack_high; a += 4) note_stack_word(a);    // (see there)
        }
    };
    gpr* r = &c->r0;
    const u32 given[4] = {(u32)r[4], (u32)r[5], (u32)r[6], (u32)r[7]};    // (before they're scrambled, below)
    for (int i = 0; i < 4; i++) {
        if (parameter_mask & (1u << i)) note_word((u32)r[4 + i]);
    }
    if (parameter_mask & 0x10) call_trace.push_back(c->f12.u32l);
    if (parameter_mask & 0x20) call_trace.push_back(c->f14.u32l);
    if (use_double && (parameter_mask & 0x40)) call_trace.push_back(c->f12.u32h);      // (f13 and f15, a double parameter's high words: lift.py
    if (use_double && (parameter_mask & 0x80)) call_trace.push_back(c->f14.u32h);      //  stand_in_double_bits. TESTER CHANGE, "refused2")
    for (u32 i = 0; i < stack_words; i++) note_word(word_at(sp + 0x10 + 4 * i));

    // The same results for both versions: they only depend on the test and how many calls came before.
    maybe_cut(calls_so_far);      // (a function that never returns: see cut_call)
    uint64_t kept_state = random_state;
    random_state = call_seed(calls_so_far + 1);
    calls_so_far++;
    for (int i = 0; i < 20; i++) random32();
    r[2] = (gpr)(int64_t)(s32)random_word();
    r[3] = (gpr)(int64_t)(s32)random_word();
    c->f0.u32l = random_float_bits();     // (f0 and f1: a call's float result, drawn fresh as v0 and v1 are)
    c->f0.u32h = random_float_bits();
    c->f1.u32l = random_float_bits();
    c->f1.u32h = random_float_bits();
    // (Scratch registers: rubbish from the pools, above. One place in each pool per call, each register its own entry.)
    {
        u32 w = random32(), g = random32();
        for (int i : {1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25}) r[i] = (gpr)(int64_t)(s32)word_pool[w++ & POOL_MASK];
        for (int i = 2; i < 20; i++) {
            fpr* x = &c->f0 + i;
            x->u32l = float_pool[g++ & POOL_MASK];
            x->u32h = float_pool[g++ & POOL_MASK];
        }
    }
    c->hi = random32();
    c->lo = random32();
    if (random32() % 4 == 0) {
        // Changes something in memory, as most functions do.
        u32 a = random_pointer(4);
        u32 value = random_word();
        stand_in_store(a, value);
    }
    steer_stand_in(parameter_mask, given, stack_words, sp, c, ~0ull);
    note_result4(c, ~0ull);
    random_state = kept_state;
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    args_leave(c);
#endif
    in_stand_in = false;
}

#if FUZZ_MOD_NATIVE     // (2026-11-14, fastnight: from here to the #else, the mod's native functions only -- fuzz.h FUZZ_MOD_NATIVE)
// ---- The mod's private data in PC memory (2026-11-12, scratch/agents/modglue; its exact form CONFIRMED by the user 2026-11-12) ----
// The user chose that, while one of the mod's functions is proved native, the mod's PRIVATE data lives in PC memory -- as the
// native mod keeps it -- and not at its N64 address. Private means only the mod's own C ever reads or writes it
// (scratch/agents/modglue/private_data.py, from mod.map and the mod's translated code: reached only by loads and stores through
// %lo, its address never made, never named by game code or held in the mod's data). The original, the mod's translated copy,
// still keeps it in game memory at its mod.map address. So, for a function lift.py gives a table of private symbols
// (fuzz_private_table; none does yet, and with none every test is exactly as before):
//   1. Same start: before the converted run, each symbol's host object is set from the bytes the original started from, field
//      by field (each scalar of the N64 layout, its width from the table, turned to the PC's byte order).
//   2. The N64 copy is out of bounds for the converted run: its bytes there are changed to other random bytes (start XOR a
//      non-zero word) for that run, so a converted read of the old place reads wrong values, and ANY converted write there is a
//      difference ("private-n64"), whatever it writes.
//   3. Same end, compared as memory is today: after the run the N64 copy is put back as it started, and each host field that
//      changed is written back to its N64 place in the N64 layout, as a noted write. collect_and_undo then gives the bytes the
//      converted function changed in game memory and in the private data together, compared byte for byte with the original's
//      changes, as for any other memory: a missed write, an extra one, a wrong value or width are all differences.
//   4. In step with the mod's other functions: at each call to a stand-in for one of the mod's own functions
//      (fuzz_mod_stand_in), a hash of all the private data as it is then, in the N64 layout, is noted in the list of calls --
//      the original's from game memory, the converted's from its host objects -- so a write moved past such a call (which the
//      real callee might read) is a difference. And that stand-in may change a private field, as a real callee may: the same
//      field and value on both sides (from the call's own seed), into game memory for the original, into the host object for
//      the converted function, which must then read it again as the original does.
// What it never hides: every byte of game memory outside the table is compared exactly as before; a private symbol the
// converted function treats as game memory is caught by 2; the converted side's private data comes only from the table (the
// test build defines the mod's private variables from it, so one missing from it doesn't link); and a symbol wrongly left out
// of the table stays at its N64 address and is compared as game memory, which is only stricter.
// (FuzzPrivate, one symbol of the table: fuzz.h)
__attribute__((weak)) const FuzzPrivate* fuzz_private_table(const char* function, int* count) { (void)function; *count = 0; return nullptr; }
static const FuzzPrivate* private_table = nullptr;
static int private_count = 0;
static std::vector<u32> private_start;          // each symbol's start words as the original started (game memory's own form)
static bool private_n64_touched = false;        // the converted run wrote the N64 copy (2)
static u32 private_n64_where = 0;

// One scalar of a symbol, read from or written to game memory in the N64 layout (the translator's: a word as it is, a half
// at address ^ 2, a byte at address ^ 3, a doubleword as two words, the high one first).
static uint64_t n64_field(u32 a, u32 w) {
    switch (w) {
    case 1: return lift_rdram[(a ^ 3) - 0x80000000u];
    case 2: return *(u16*)(lift_rdram + ((a ^ 2) - 0x80000000u));
    case 4: return word_at(a);
    default: return ((uint64_t)word_at(a) << 32) | word_at(a + 4);
    }
}
static void set_n64_field(u32 a, u32 w, uint64_t v, bool noted) {
    switch (w) {
    case 1: if (noted) lift_fuzz_note_write((a ^ 3) - 0x80000000u, 1); lift_rdram[(a ^ 3) - 0x80000000u] = (u8)v; break;
    case 2: if (noted) lift_fuzz_note_write((a ^ 2) - 0x80000000u, 2); *(u16*)(lift_rdram + ((a ^ 2) - 0x80000000u)) = (u16)v; break;
    case 4: if (noted) lift_fuzz_note_write(a - 0x80000000u, 4); word_at(a) = (u32)v; break;
    default: set_n64_field(a, 4, v >> 32, noted); set_n64_field(a + 4, 4, (u32)v, noted); break;
    }
}
static uint64_t host_field(const u8* p, u32 w) { uint64_t v = 0; std::memcpy(&v, p, w); return v; }   // (the PC is little-endian)
static void set_host_field(u8* p, u32 w, uint64_t v) { std::memcpy(p, &v, w); }

// The private data's hash as it is now, in the N64 layout: from game memory (the original's run) or the host objects (4).
static uint64_t private_hash(bool host) {
    uint64_t h = 0x84222325CBF29CE4ull;
    for (int s = 0; s < private_count; s++) {
        const FuzzPrivate& p = private_table[s];
        u32 at = 0;
        for (u32 k = 0; k < p.width_count; k++) {
            const u32 w = p.widths[k];
            const uint64_t v = host ? host_field(p.host + at, w) : n64_field(p.address + at, w);
            h = (h ^ v ^ ((uint64_t)(p.address + at) << 40)) * 0x100000001B3ull;
            at += w;
        }
    }
    return h;
}

// 1 and 2, before the converted run (memory is as the test started: the original's writes are undone by then).
static void private_before_converted() {
    if (private_count == 0) return;
    private_n64_touched = false;
    private_start.clear();
    for (int s = 0; s < private_count; s++) {
        const FuzzPrivate& p = private_table[s];
        u32 at = 0;
        for (u32 k = 0; k < p.width_count; k++) {
            set_host_field(p.host + at, p.widths[k], n64_field(p.address + at, p.widths[k]));
            at += p.widths[k];
        }
        for (u32 a = p.address & ~3u; a < p.address + p.bytes; a += 4) {
            private_start.push_back(word_at(a));
            u32 x;
            do { x = random32(); } while (x == 0);
            word_at(a) ^= x;     // (not noted: the tester's own doing, put back in private_after_converted)
        }
    }
}

// 3, after it: the N64 copy as it started, a converted write to it noted as a difference, then the host objects written
// back as noted writes wherever they changed.
static void private_after_converted() {
    if (private_count == 0) return;
    size_t i = 0;
    for (int s = 0; s < private_count; s++) {
        const FuzzPrivate& p = private_table[s];
        for (u32 a = p.address & ~3u; a < p.address + p.bytes; a += 4) {
            for (u32 b = 0; b < 4; b++) {
                if (!private_n64_touched && was_written(a - 0x80000000u + b)) { private_n64_touched = true; private_n64_where = a + b; }
            }
            word_at(a) = private_start[i++];
        }
    }
    // (A byte the converted run wrote there has its "before" on the undo list as it was changed in 2: collect_and_undo would
    //  put that back. Such a test is a difference anyway; private_restore puts the start back after it.)
    // (2026-11-13, scratch/agents/modnative3, TESTER CHANGE: these writes are the tester's own, after the converted run has
    //  ended, outside run_version's try. They went through the access count, so a converted run that finished just under the
    //  access limit threw TooLong here, where nothing catches it: the tester ended (libc++abi terminate, 0xC0000409) --
    //  arcade_rr_finalize_post, story_begin, apply_sets and tron_scene_begin "crashed". Now they don't count, as the original's
    //  run has no such writes to count; the count is put back as the run left it. Every write is still noted as before.)
    const u32 accesses_of_the_run = accesses;
    accesses = 0;
    for (int s = 0; s < private_count; s++) {
        const FuzzPrivate& p = private_table[s];
        u32 at = 0;
        for (u32 k = 0; k < p.width_count; k++) {
            const u32 w = p.widths[k];
            const uint64_t v = host_field(p.host + at, w);
            if (v != n64_field(p.address + at, w)) set_n64_field(p.address + at, w, v, true);
            at += w;
        }
    }
    accesses = accesses_of_the_run;
}
// ---- The user's rules of 2026-11-13, for the mod's own functions made native (scratch/agents/modnative5) ----
// Each applies only to a function lift.py gives rules (fuzz_native_rules: the native candidates, prove_gen.py's), so every
// other function's test is exactly as before; each has a switch that turns it off (FUZZ_NO_NM_<name>).
//   (1) NARROW: a parameter the C declares u8 / s8 / u16 / s16 / bool is, as the calling rules have the caller leave it, extended
//       from its width as its type says -- the mod's compiler relies on it (variant_base_of compares a0 whole). Before each test
//       runs (run_both: every part's starts), each such parameter's register or stack word is extended so.
//   (2) SHRUNK: a private byte the mod's compiler shrank a variable to (FuzzPrivate.zero_or_one) holds 0 or 1 at the start of
//       each test, and a stand-in that changes it writes 0 or 1 (fuzz_mod_stand_in).
//   (3) FAST: a function of the mod compiler's fast convention gets its parameters' kinds by register (randomize_registers),
//       and LOOPS: each pass of a loop -- the original's backward jumps, the native code's loop conditions -- counts against a
//       budget of 64 times the access limit; past it the run has run long (TooLong), as past the access limit: a test both
//       versions run long in is not compared, one where only one does is "ran-long" as always. (The fast stand-ins have no
//       switch here: without them prove_gen.py makes no such candidate, NM_NO_FAST.)
//   (4) OUTSIDE: the native code indexed one of the mod's arrays past its end (nm_native.h NArr, from mod.map's size) and the
//       place it reached is inside a private object's N64 copy -- which the native side never uses, keeping that object in PC
//       memory -- and the original's run reached that same word too: the original reached outside an object into a
//       neighbouring private object's N64 copy. Only then is the test set aside (not compared; counted, outside_set_aside),
//       as the user's rule allows, and another runs in its place (main: until `tests` are compared). Any other test with an
//       index past an array's end is compared as always.
__attribute__((weak)) const FuzzNativeRules* fuzz_native_rules(const char* function) { (void)function; return nullptr; }
static const FuzzNativeRules* native_rules = nullptr;
static const bool use_nm_narrow = std::getenv("FUZZ_NO_NM_NARROW") == nullptr;
static const bool use_nm_shrunk = std::getenv("FUZZ_NO_NM_SHRUNK") == nullptr;
static const bool use_nm_fast = std::getenv("FUZZ_NO_NM_FAST") == nullptr;
static const bool use_nm_loops = std::getenv("FUZZ_NO_NM_LOOPS") == nullptr;
static const bool use_nm_outside = std::getenv("FUZZ_NO_NM_OUTSIDE") == nullptr;
// (2026-11-15, modnative7: the ALIGNED structure-pointer rule -- APPROVED by the user 2026-11-15 00:40 for 0.84, approved with
// fixes by the review (reviewnm084); FUZZ_NO_NM_ALIGNED=1 for the behaviour before it. Native-mod candidates only.) A test's
// start never gives a pointer parameter of kind 'h' or 'w' an address the N64 would fault on: a value a tweak gives it
// (steering's and parts 4 and 5's, apply) is rounded down to its alignment, as the plain draws already are (random_pointer(2),
// random_pointer(4)). The kind is the C pointee's alignment (prove_gen.py pointer_kinds, lift.py param_kinds): void*, char*, a
// structure of bytes only are 'b', and keep every address. page_items, given MI* a = 9 by steering (a small count), wrote its
// fields where the original's translated `sw` at an unaligned address scrambles them; on the N64 that `sw` raises an address
// error, and both its callers pass a local array. A difference on aligned data still counts: nothing else changes.
static const bool use_nm_aligned = std::getenv("FUZZ_NO_NM_ALIGNED") == nullptr;
static u32 nm_keep_alignment(const FuzzFunction& f, int parameter, u32 v) {
    if (!use_nm_aligned || native_rules == nullptr || parameter < 0 || parameter >= (int)std::strlen(f.params)) return v;
    const char k = f.params[parameter];
    return k == 'w' ? (v & ~3u) : k == 'h' ? (v & ~1u) : v;
}
static u32 loop_passes = 0;
void lift_fuzz_loop_tick() {
    if (native_rules == nullptr || !native_rules->loop_budget || !use_nm_loops) return;
    if (++loop_passes > 64 * access_limit) throw TooLong{};
}
bool lift_fuzz_watching_mod = false;
static std::unordered_map<u32, bool> original_touched;     // (4) the word offsets the original's run read or wrote
void lift_fuzz_note_mod_access(uint64_t offset) {
    if (in_stand_in || original_touched.size() >= 65536) return;
    original_touched[(u32)offset & ~3u] = true;
}
static std::vector<std::pair<u32, u32>> outside_noted;      // (4) where the native code's run indexed past an array's end
void lift_fuzz_note_outside(u32 address, u32 bytes) {
    if (running_converted && !in_stand_in && outside_noted.size() < 64) outside_noted.push_back({address, bytes});
}
static long outside_set_aside = 0;
// (4) Whether the last pair of runs is such a test.
static bool outside_into_private() {
    if (native_rules == nullptr || !use_nm_outside || outside_noted.empty()) return false;
    for (const auto& o : outside_noted) {
        for (u32 a = o.first & ~3u; a < o.first + o.second; a += 4) {
            bool in_private = false;
            for (int s = 0; s < private_count && !in_private; s++) {
                const FuzzPrivate& p = private_table[s];
                in_private = a + 4 > p.address && a < p.address + p.bytes;
            }
            if (in_private && original_touched.count(a - 0x80000000u)) return true;
        }
    }
    return false;
}
// (2026-11-14, modnative7: the PROPOSED rule NM_RULE_WRITES) One of the values the mod's writes can give p (FuzzPrivate.values).
static uint64_t written_value(const FuzzPrivate& p) {
    if (p.value_kind == 2) {
        const uint64_t span = (uint64_t)(p.values[1] - p.values[0]) + 1;
        const uint64_t r = ((uint64_t)random32() << 32) | random32();
        return (uint64_t)(p.values[0] + (int64_t)(span == 0 ? r : r % span));
    }
    return (uint64_t)p.values[random32() % p.value_count];
}
// (2026-11-14, modnative7: F4 of the rule's review) Whether the field's bits are one of p's values: as the width's unsigned
// number or its signed one (the bound lists -1 for a byte holding 0xFF).
static bool inside_written(const FuzzPrivate& p, uint64_t bits) {
    const u32 w = p.widths[0];
    const uint64_t mask = w >= 8 ? ~0ull : (1ull << (8 * w)) - 1;
    const int64_t u = (int64_t)(bits & mask);
    const int64_t s = w >= 8 ? u : (u & (1ll << (8 * w - 1))) ? u - (1ll << (8 * w)) : u;
    if (p.value_kind == 2) return (u >= p.values[0] && u <= p.values[1]) || (s >= p.values[0] && s <= p.values[1]);
    for (u32 k = 0; k < p.value_count; k++)
        if (p.values[k] == u || p.values[k] == s) return true;
    return false;
}
static void guard_inputs(const recomp_context& start);
// (1) and (2), on a test's start: run_both, before either version runs.
static void native_start(const recomp_context& start) {
    guard_inputs(start);     // (the private-object guard, B: see below)
    if (native_rules != nullptr && use_nm_narrow) {
        for (int i = 0; i < native_rules->narrow_count; i++) {
            const FuzzNarrow& n = native_rules->narrow[i];
            u32 v = n.where < 32 ? (u32)(&start.r0)[n.where] : word_at(STACK + (u32)(n.where - 0x100));
            switch (n.kind) {
            case 'b': v = (u8)v; break;
            case 'B': v = (u32)(s32)(s8)v; break;
            case 'h': v = (u16)v; break;
            case 'H': v = (u32)(s32)(s16)v; break;
            case '1': v &= 1; break;
            }
            if (n.where < 32) const_cast<gpr&>((&start.r0)[n.where]) = (gpr)(int64_t)(s32)v;
            else word_at(STACK + (u32)(n.where - 0x100)) = v;
        }
    }
    if (use_nm_shrunk) {
        for (int s = 0; s < private_count; s++) {
            const FuzzPrivate& p = private_table[s];
            if (p.zero_or_one) lift_rdram[(p.address ^ 3) - 0x80000000u] &= 1;
        }
    }
    // (2026-11-14, modnative7: the PROPOSED rule NM_RULE_WRITES -- a private scalar starts at a value the mod's writes can give
    //  it. Only where lift.py wrote values, which it does only with that switch.)
    //  (F4 of the rule's review, 2026-11-14: a start value already inside the bound -- one steering chose, or a kept start's -- is
    //  kept; only one outside it is replaced by a drawn value)
    for (int s = 0; s < private_count; s++) {
        const FuzzPrivate& p = private_table[s];
        if (p.values != nullptr && p.width_count == 1 && !inside_written(p, n64_field(p.address, p.widths[0])))
            set_n64_field(p.address, p.widths[0], written_value(p), false);
    }
}

// ---- The private-object guard (2026-11-14, scratch/agents/modnative7: APPROVED by the user 2026-11-14; FUZZ_NO_NM_PRIVATE_GUARD=1
// for the behaviour before it) -- native-mod candidates with private data only ----
// A private object's address never leaves the mod's own code, so no real callee can write it and no real caller can hand it
// over. The tester could do both: a stand-in's random write, its fill of a buffer a parameter points at, a part-4 call write --
// and a test input pointing into the object. In the original's run that changed the private (its N64 copy is what the original
// reads); in the native run it landed on the scrambled N64 copy the native code never reads -- so the two runs saw different
// private state, and a result could depend on the sequence of tests.
//   A (mirror): a stand-in's write is made as before, except each byte of it inside a private object's N64 copy: in the
//     native run that byte goes to the host object's same byte instead (the field's byte order turned as the host keeps it).
//     Both runs then see the same private state; the end comparison is as before.
//   B (redraw): before either run, a0-a3 and the first stack words, where they point into a private object's N64 copy, are
//     drawn again (another pointer into the test's memory), for both runs alike.
static const bool use_nm_guard = std::getenv("FUZZ_NO_NM_PRIVATE_GUARD") == nullptr;
static long guard_mirrored = 0, guard_redrawn = 0;
static bool guard_on() { return use_nm_guard && native_rules != nullptr && private_count != 0; }
static int private_at(u32 x) {
    for (int s = 0; s < private_count; s++) {
        const FuzzPrivate& p = private_table[s];
        if (x >= p.address && x < p.address + p.bytes) return s;
    }
    return -1;
}
// The host object's byte that holds N64 byte x of private s (x inside it): its field's start and width, the byte's place in the
// field counted from the most significant end (N64) turned to the least significant (the PC).
static u8* host_byte(int s, u32 x) {
    const FuzzPrivate& p = private_table[s];
    u32 at = 0;
    for (u32 k = 0; k < p.width_count; k++) {
        const u32 w = p.widths[k];
        if (x < p.address + at + w) return p.host + at + (w - 1 - (x - p.address - at));
        at += w;
    }
    return nullptr;
}
static void stand_in_store(u32 a, u32 value) {
    if (!guard_on() || (private_at(a) < 0 && private_at(a + 1) < 0 && private_at(a + 2) < 0 && private_at(a + 3) < 0)) {
        lift_fuzz_note_write(a - 0x80000000u, 4);
        word_at(a) = value;
        return;
    }
    for (u32 k = 0; k < 4; k++) {
        const u32 x = a + k;
        const u8 b = (u8)(value >> (24 - 8 * k));      // (game memory: the word's first address its most significant byte)
        const int s = private_at(x);
        if (s >= 0 && running_converted) {
            if (u8* h = host_byte(s, x)) { *h = b; guard_mirrored++; }
        } else {
            lift_fuzz_note_write((x ^ 3) - 0x80000000u, 1);
            lift_rdram[(x ^ 3) - 0x80000000u] = b;
        }
    }
}
static void guard_inputs(const recomp_context& start) {
    if (!guard_on()) return;
    for (int r = 4; r <= 7; r++) {
        u32 v = (u32)(&start.r0)[r];
        if (private_at(v) < 0) continue;
        do { v = random_pointer(4); } while (private_at(v) >= 0);
        const_cast<gpr&>((&start.r0)[r]) = (gpr)(int64_t)(s32)v;
        guard_redrawn++;
    }
    for (u32 a = STACK + 0x10; a < STACK + 0x30; a += 4) {
        u32 v = word_at(a);
        if (private_at(v) < 0) continue;
        do { v = random_pointer(4); } while (private_at(v) >= 0);
        word_at(a) = v;
        guard_redrawn++;
    }
}

static void private_restore() {      // (after collect_and_undo: the N64 copy exactly as the test started)
    if (private_count == 0) return;
    size_t i = 0;
    for (int s = 0; s < private_count; s++) {
        const FuzzPrivate& p = private_table[s];
        for (u32 a = p.address & ~3u; a < p.address + p.bytes; a += 4) word_at(a) = private_start[i++];
    }
}

// 4: the stand-in for one of the mod's own functions (lift.py writes it for a modfn_ callee when the function under test has
// a private table; otherwise that callee gets fuzz_stand_in, as every callee did). fuzz_stand_in's own draws come first and
// are as they were; this one's come from a seed of its own.
void fuzz_mod_stand_in(u32 which, u32 parameter_mask, u32 stack_words, recomp_context* c) {
    if (private_count != 0) call_trace.push_back(private_hash(running_converted));
    fuzz_stand_in(which, parameter_mask, stack_words, c);
    if (private_count == 0) return;
    const uint64_t kept_state = random_state;
    random_state = call_seed(calls_so_far) ^ 0x5851F42D4C957F2Dull;
    if (random_state == 0) random_state = 1;
    if (random32() % 4 == 0) {
        const FuzzPrivate& p = private_table[random32() % (u32)private_count];
        const u32 k = random32() % p.width_count;
        u32 at = 0;
        for (u32 j = 0; j < k; j++) at += p.widths[j];
        const uint64_t v = ((uint64_t)random_word() << 32) | random_word();
        uint64_t value = p.widths[k] == 8 ? v : v & ((1ull << (8 * p.widths[k])) - 1);
        if (p.zero_or_one && use_nm_shrunk) value &= 1;      // (the user's rule (2), 2026-11-13: a shrunk byte holds 0 or 1)
        if (p.values != nullptr && p.width_count == 1) value = written_value(p);   // (PROPOSED NM_RULE_WRITES, modnative7: only with it)
        if (running_converted) set_host_field(p.host + at, p.widths[k], value);
        else set_n64_field(p.address + at, p.widths[k], value, true);
    }
    random_state = kept_state;
}
#else   // FUZZ_MOD_NATIVE 0 (2026-11-14, fastnight): no function of this build has a private table or native rules, so each of
        // these does what the code above does then -- nothing -- and fuzz_mod_stand_in is fuzz_stand_in exactly.
static inline void private_before_converted() {}
static inline void private_after_converted() {}
static inline void private_restore() {}
static inline void native_start(const recomp_context&) {}
void fuzz_mod_stand_in(u32 which, u32 parameter_mask, u32 stack_words, recomp_context* c) { fuzz_stand_in(which, parameter_mask, stack_words, c); }
static inline u32 nm_keep_alignment(const FuzzFunction&, int, u32 v) { return v; }   // (merge084: as with no native rules)
static void stand_in_store(u32 a, u32 value) { lift_fuzz_note_write(a - 0x80000000u, 4); word_at(a) = value; }   // (merge084b: no private table in this build -- the guard is off: its first branch)
#endif  // FUZZ_MOD_NATIVE
// ---- The step limit (2026-11-14, scratch/agents/rules084; approved by the user 2026-11-14 for 0.84 with an independent review
//      before anything counts from it: REVIEW_STEPLIMIT.txt. TESTER CHANGE) ----
// A loop that makes no memory access never meets the access limit: on random data some run for ever (an angle wrapped by
// subtracting 2pi from a huge number, a digit count that wraps round, a step of 0), and the whole tester process ran until
// lift.py's wall limit killed it -- the function got no report line at all (rest100's MAP group F: 12 functions). Now both
// versions count their loop passes (fuzz.h lift_fuzz_step) and one past 64 times the access limit has run long (TooLong):
// treated exactly as a run past the access limit is. Both ran long: not compared (set aside, counted below). One ran long and
// the other finished: the second chance (16 times the limits) and then "ran-long", a difference, as always. Neither ran long:
// compared as any test, so a difference before the limit is a difference. Every function with no loop is tested as before.
//   OFF unless FUZZ_STEP_LIMIT=1 (lift.py sets it with LIFT_STEP_LIMIT=1): without it the steps never stop a run, exactly as
//   before.
//   The user's rule is strict: a test is set aside for the step limit only when BOTH versions ran past the STEP limit. One
//   past the step limit and the other past the access limit ("mixed") is a difference ("step-mixed"), always.
//   (merge084b, 2026-11-15: the independent review (scratch/agents/reviewr084) APPROVED the step limit in this strict form
//   only; rules084's proposal to set a mixed test aside is removed with its code. The 0.84 night run sets LIFT_STEP_LIMIT=1.)
u32 lift_fuzz_steps = 0;
u32 lift_fuzz_step_limit = 0xFFFFFFFFu;
static const bool use_step_limit = std::getenv("FUZZ_STEP_LIMIT") != nullptr && std::getenv("FUZZ_STEP_LIMIT")[0] == '1';
static bool step_stopped = false;       // (the last run was stopped by the step limit, not by the access limit)
static bool original_stepped = false, converted_stepped = false;
static long steps_set_aside_both = 0;   // tests set aside: both versions past the step limit
static long steps_mixed_different = 0;  // tests one version ran past the step limit, the other past the access limit: differences ("step-mixed")
void lift_fuzz_step_stop() {
    step_stopped = true;
    throw TooLong{};
}
static void start_steps() {
    lift_fuzz_steps = 0;
    step_stopped = false;
    lift_fuzz_step_limit = use_step_limit ? 64u * access_limit : 0xFFFFFFFFu;
    nonframe_writes = 0;        // (the write cut, 2026-11-15: counted from each run's start)
    write_cut_hit = false;
}
// (The write cut, 2026-11-15: see "The write cut". Only the game code a version runs counts -- not a stand-in's writes, not
//  the tester's own; and only outside the function's own frame, which the original fills with spills the converted code keeps
//  in variables. The cut comes just before the K-th such write is made, so both versions have made the same first K - 1.)
[[noreturn]] static void write_cut_stop() {
    write_cut_hit = true;
    throw Stopped{};
}
static inline void count_nonframe_write(uint64_t offset) {
    if (!write_cut_here || in_stand_in || !(running_original || running_converted)) return;
    if (offset >= TRACK_FRAME_LOW && offset < TRACK_FRAME_HIGH) return;
    if (++nonframe_writes == write_cut_at) write_cut_stop();
}
static long write_cut_tests = 0;         // (tests compared at a write cut: reach only, as part 4's -- not in the report's counts)
static u32 original_limit_used = 0, converted_limit_used = 0;
static long original_writes_made = 0, converted_writes_made = 0;

// In place of a system function, the runtime's own version the translated code calls (osRecvMesg_recomp and the rest: lift.py
// stand_in_line, system_reads.model, 2026-11-07). Its source is C++ that touches only the registers it reads and the result ones it
// writes, so this does the same: the result registers get random values, every other register keeps its value. (fuzz_stand_in
// scrambles all the scratch ones, as compiled MIPS may; here that would hide a converted caller reading a register the runtime's
// version leaves alone where the original reads what it put there itself.) What it reads is noted -- which one, its stack
// pointer, its parameters as fuzz_stand_in notes them, and the memory outside the caller's frame as it is now -- and the memory it
// writes through its parameters gets random bytes, so a caller reading them is tested on new values; plus, sometimes, the random
// write any stand-in may make, and one at the function's own fixed addresses (for what it writes elsewhere, or another thread
// writes while it waits: system_reads.MEMORY's notes). Part 2 steers it as it does fuzz_stand_in, with its results only.
#if FUZZ_MOD_NATIVE
// (the user's rule (3), 2026-11-13: see fuzz.h) The registers past a0-a3 a fast-convention callee is given, then as any of the
// mod's functions' stand-ins.
void fuzz_mod_stand_in_fast(u32 which, u32 parameter_mask, uint64_t extra, recomp_context* c) {
    for (int b = 0; b < 64; b++) {
        if (extra >> b & 1) call_trace.push_back(*abi_register(c, b));
    }
    fuzz_mod_stand_in(which, parameter_mask, 0, c);
}
#endif

void fuzz_system_stand_in(u32 which, u32 parameter_mask, u32 stack_words, u32 result_mask, const FuzzRegion* regions, recomp_context* c) {
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    args_enter(which, c);
#endif
    lift_fuzz_count_access();
    in_stand_in = true;
    const u32 sp = (u32)c->r29;
    call_trace.push_back(which);
    call_trace.push_back(sp);
    const u32 stack_low = STACK - 0x4000, stack_high = STACK + 0x60;
    auto note_word = [&](u32 v) {      // (as fuzz_stand_in's: a pointer into the stack has what's there noted too)
        call_trace.push_back(v);
        if (v >= stack_low && v < stack_high) {
            for (u32 a = v & ~3u; a < (v & ~3u) + 0x20 && a < stack_high; a += 4) note_stack_word(a);    // (see there)
        }
    };
    gpr* r = &c->r0;
    const u32 given[4] = {(u32)r[4], (u32)r[5], (u32)r[6], (u32)r[7]};
    for (int i = 0; i < 4; i++) {
        if (parameter_mask & (1u << i)) note_word((u32)r[4 + i]);
    }
    if (parameter_mask & 0x10) call_trace.push_back(c->f12.u32l);
    if (parameter_mask & 0x20) call_trace.push_back(c->f14.u32l);
    if (use_double && (parameter_mask & 0x40)) call_trace.push_back(c->f12.u32h);      // (f13 and f15, a double parameter's high words: lift.py
    if (use_double && (parameter_mask & 0x80)) call_trace.push_back(c->f14.u32h);      //  stand_in_double_bits. TESTER CHANGE, "refused2")
    for (u32 i = 0; i < stack_words; i++) note_word(word_at(sp + 0x10 + 4 * i));
    if (track_memory) {
        settle_pending();
        call_trace.push_back(memory_sum);
    }
    maybe_cut(calls_so_far);      // (a function that never returns: see cut_call)
    uint64_t kept_state = random_state;
    random_state = call_seed(calls_so_far + 1);
    calls_so_far++;
    for (int i = 0; i < 20; i++) random32();
    if (result_mask & 1) r[2] = (gpr)(int64_t)(s32)random_word();
    if (result_mask & 2) r[3] = (gpr)(int64_t)(s32)random_word();
    if (result_mask & 4) c->f0.u32l = random_float_bits();
    for (const FuzzRegion* g = regions; g->parameter >= 0; g++) {
        u32 at = g->parameter < 4 ? given[g->parameter] : word_at(sp + 0x10 + 4 * (g->parameter - 4));
        if (at == 0) continue;      // (A null pointer: the runtime's versions write nothing there, osRecvMesg's message.)
        for (u32 i = 0; i < g->bytes; i++) {
            // Byte by byte, each where the game's byte order puts it (as lift_write8 does), so only the region's own bytes change.
            uint64_t o = (u32)(((at + i) ^ 3) - 0x80000000u);
            lift_fuzz_note_write(o, 1);
            lift_rdram[o] = (u8)random32();
        }
    }
    if (random32() % 4 == 0) {
        u32 a = random_pointer(4);
        u32 value = random_word();
        stand_in_store(a, value);
    }
    // And sometimes a word at (or beside, as randomize_memory fills them) one of the fixed addresses the function under test names.
    // A system function can wait for another thread (osRecvMesg) or wake one (osSendMesg), which can change any of the game's
    // memory in the meantime, so a converted caller that read such a global before the call where the original reads it after
    // has to show up; the random write above only lands in the arena, never on the function's own globals.
    if (under_test != nullptr && under_test->addresses[0] != 0 && random32() % 4 == 0) {
        int count = 0;
        while (under_test->addresses[count] != 0) count++;
        u32 a = (under_test->addresses[random32() % count] & ~3u) - 0x20 + 4 * (random32() % 0x19);
        u32 value = random_word();
        stand_in_store(a, value);
    }
    // (Part 2's chosen results go only where the runtime's version writes one: bit 2 v0, bit 3 v1, bit 32 f0, as abi.py's masks.)
    const uint64_t may_write = ((result_mask & 1) ? 1ull << 2 : 0) | ((result_mask & 2) ? 1ull << 3 : 0) | ((result_mask & 4) ? 1ull << 32 : 0);
    steer_stand_in(parameter_mask, given, stack_words, sp, c, may_write);
    note_result4(c, may_write);
    random_state = kept_state;
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    args_leave(c);
#endif
    in_stand_in = false;
}

// A port function called by code the port added to a translated function (see fuzz.h). Both versions must call the same ones,
// with the same values, in the same order relative to every other call: a missing addition, one in the wrong place, or one passed
// something different shows as a different call list. (Its number is marked with the top bit, apart from the game functions'.)
u32 fuzz_port_call(u32 which, const uint64_t* words, int count) {
    lift_fuzz_count_access();
    call_trace.push_back(0x80000000u | which);
    for (int i = 0; i < count; i++) call_trace.push_back(words[i]);
    // And the memory the function has changed so far, outside its own frame: a hook usually reads what the code around it wrote
    // (a matrix, a display list), so one moved past a store differs here even when it is given the same values. (Only for these
    // calls: at a call, the converted code has made the same stores as the original -- m2c moves no store across a call.)
    const uint64_t frame_low = (STACK - 0x4000u) - 0x80000000u, frame_high = own_memory_end();
    std::vector<Undo> written;      // (each byte written so far, with what it held before the first write: stable_sort keeps that first)
    for (const Undo& u : undo_list) {
        if (u.offset < frame_low || u.offset >= frame_high) written.push_back(u);
    }
    std::stable_sort(written.begin(), written.end(), [](const Undo& x, const Undo& y) { return x.offset < y.offset; });
    uint64_t digest = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < written.size(); i++) {
        if (i > 0 && written[i].offset == written[i - 1].offset) continue;
        if (lift_rdram[written[i].offset] == written[i].before) continue;     // (written back as it was: no change)
        digest = (digest ^ written[i].offset) * 0x100000001B3ull;
        digest = (digest ^ lift_rdram[written[i].offset]) * 0x100000001B3ull;
    }
    call_trace.push_back(digest);
    maybe_cut(calls_so_far);      // (a function that never returns: see cut_call)
    uint64_t kept_state = random_state;
    random_state = call_seed(calls_so_far + 1);
    calls_so_far++;
    for (int i = 0; i < 20; i++) random32();
    u32 result = random_word();
    random_state = kept_state;
    return result;
}

// ---- For functions that don't keep to the calling rules (tools/lift/abi.py, 2026-11-07) ----
// Such a function's converted C++ keeps some extra registers in step in the context: ones a caller reads after it returns, and
// ones the functions it calls read beyond their parameters. The test checks those: the stand-ins record what each callee reads
// there, and the registers are compared at the end (FuzzFunction's abi_out). The stand-ins also have to behave like the real
// callee towards them: one that scrambled every scratch register would overwrite a register the real callee leaves alone, and
// hide a converted function that didn't keep it as the original does. So for these functions only (abi_mode), a stand-in
// changes only the registers the real function may change (lift.py works that out from its instructions and everything it
// calls, abi.py may_write): always those it surely writes on every way through (abi.py must_write: a call's result in v0, which
// a caller relies on), each of the others changed or left as it was at random. For every other function nothing is different.
static bool abi_mode = false;

// A register's 32 bits by its bit in those masks: n < 32 is GPR n, 32 + n float register n (an odd one is the top half of the
// even one's pair, the N64's 32-bit float mode, as register_named below has it).
static u32* abi_register(recomp_context* c, int n) {
    if (n < 32) return (u32*)(&c->r0 + n);
    n -= 32;
    fpr* x = &c->f0 + (n & ~1);
    return (n & 1) ? &x->u32h : &x->u32l;
}

void fuzz_stand_in_abi(u32 which, u32 parameter_mask, u32 stack_words, uint64_t extra_reads, uint64_t may_write, uint64_t must_write,
                       recomp_context* c) {
    if (!abi_mode) {
        fuzz_stand_in(which, parameter_mask, stack_words, c);
        return;
    }
    // What it was given, as fuzz_stand_in notes it...
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    args_enter(which, c);
#endif
    lift_fuzz_count_access();
    in_stand_in = true;
    u32 sp = (u32)c->r29;
    call_trace.push_back(which);
    call_trace.push_back(sp);
    const u32 stack_low = STACK - 0x4000, stack_high = STACK + 0x60;
    auto note_word = [&](u32 v) {
        call_trace.push_back(v);
        if (v >= stack_low && v < stack_high) {
            for (u32 a = v & ~3u; a < (v & ~3u) + 0x20 && a < stack_high; a += 4) note_stack_word(a);    // (see there)
        }
    };
    gpr* r = &c->r0;
    const u32 given[4] = {(u32)r[4], (u32)r[5], (u32)r[6], (u32)r[7]};    // (before they're changed, below: steer_stand_in)
    for (int i = 0; i < 4; i++) {
        if (parameter_mask & (1u << i)) note_word((u32)r[4 + i]);
    }
    if (parameter_mask & 0x10) call_trace.push_back(c->f12.u32l);
    if (parameter_mask & 0x20) call_trace.push_back(c->f14.u32l);
    if (use_double && (parameter_mask & 0x40)) call_trace.push_back(c->f12.u32h);      // (f13 and f15, a double parameter's high words: lift.py
    if (use_double && (parameter_mask & 0x80)) call_trace.push_back(c->f14.u32h);      //  stand_in_double_bits. TESTER CHANGE, "refused2")
    for (u32 i = 0; i < stack_words; i++) note_word(word_at(sp + 0x10 + 4 * i));
    // ...and the registers it reads beyond its parameters: what the caller left there has to match too.
    for (int b = 0; b < 64; b++) {
        if (extra_reads >> b & 1) call_trace.push_back(*abi_register(c, b));
    }

    // Results that depend only on the test and how many calls came before, as in fuzz_stand_in; but only the registers it may
    // change are changed: those it surely changes always (abi.py must_write: a call's result in v0, say), the others each one
    // or not at random.
    maybe_cut(calls_so_far);      // (a function that never returns: see cut_call)
    uint64_t kept_state = random_state;
    random_state = call_seed(calls_so_far + 1);
    calls_so_far++;
    for (int i = 0; i < 20; i++) random32();
    for (int i : {2, 3, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25}) {
        u32 v = random_word();
        bool change = (random32() & 1) != 0;
        if ((must_write >> i & 1) || ((may_write >> i & 1) && change)) r[i] = (gpr)(int64_t)(s32)v;
    }
    for (int i = 0; i < 20; i++) {
        fpr* x = &c->f0 + i;
        u32 low = random_float_bits(), high = random_float_bits(), coin = random32();
        if (i & 1) {
            x->u32l = low;      // (the odd entries aren't registers in the 32-bit float mode: as fuzz_stand_in has them)
            x->u32h = high;
            continue;
        }
        if ((must_write >> (32 + i) & 1) || ((may_write >> (32 + i) & 1) && (coin & 1))) x->u32l = low;
        if ((must_write >> (33 + i) & 1) || ((may_write >> (33 + i) & 1) && (coin & 2))) x->u32h = high;
    }
    c->hi = random32();
    c->lo = random32();
    // (2026-11-11, trial 5) A port helper that hands back a saved register (lift.py helpers_writing_saved_registers:
    // rw_cam_view_s5 sets s5): its masks have that register's bit, and the stand-in changes it as it does a scratch one,
    // always or at random. Both versions call the same stand-in, so a converted caller that went on with the s5 it had
    // differs from the original, which goes on with this one. No other stand-in's masks have a saved register's bit, and
    // without one nothing is drawn here: every other call is exactly as before.
    const uint64_t SAVED = 0x0000000040FF0000ull;       // (s0-s7 and fp)
    if ((may_write | must_write) & SAVED) {
        for (int i : {16, 17, 18, 19, 20, 21, 22, 23, 30}) {
            if (!((may_write | must_write) >> i & 1)) continue;
            u32 v = random_word();
            bool change = (random32() & 1) != 0;
            if ((must_write >> i & 1) || change) r[i] = (gpr)(int64_t)(s32)v;
        }
    }
    if (random32() % 4 == 0) {
        u32 a = random_pointer(4);
        u32 value = random_word();
        stand_in_store(a, value);
    }
    steer_stand_in(parameter_mask, given, stack_words, sp, c, may_write | must_write);
    note_result4(c, may_write | must_write);
    random_state = kept_state;
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    args_leave(c);
#endif
    in_stand_in = false;
}

// ---- A mod's hook (2026-11-12, scratch/agents/modhooks; the "hook" stand-in, approved by the user 2026-11-12) ----
// A function the mods hook is tested as the loader runs it: its translated original with a call of the hook place's slot at its
// entry, before its frame is made, and/or at its returns, after the frame is released and the result set (Stage 1's
// mod_aot_hooked.c; lift.py's hooks.py checks the original is that). The converted function makes the same calls in its outer
// wrapper. Both call this at each place. What a hook can do to the function is all here:
//   - it reads registers: the ones the hook functions at that place read (analyse.py over the mod's functions; every register
//     where another mod hooks the place too, whose functions aren't analysed) are noted, so a converted function that leaves one
//     other than the original does differs -- sp always, since a hook's frame is below it;
//   - it can change memory: sometimes a random word, as every stand-in may, and sometimes one at (or beside) the fixed addresses
//     the function under test names, as fuzz_system_stand_in does, since that is what a hook mostly does (arcade_on_page_wait
//     sets a button in the pressed mask before func_800E8A14 reads it); and part 4 can steer such a write as for any call;
//   - it changes no register: the loader saves the context and puts it back after each hook (run_hook). So nothing is drawn for
//     a register, and steer_stand_in is given none it may change.
// It counts as a call (its place in the list of calls, cut_call, the seed of what it draws), so both versions must make their
// hook calls at the same point among their calls: the entry hook first, the return hook last.
void fuzz_hook_stand_in(u32 which, uint64_t reads, recomp_context* c) {
    // (FUZZ_NO_HOOK: no hook stand-in at all -- both versions run as if no hook waited at the place. For measuring this change
    //  alone, never for a proof. final081, 2026-11-12)
    static const bool off = std::getenv("FUZZ_NO_HOOK") != nullptr;
    if (off) return;
    lift_fuzz_count_access();
    in_stand_in = true;
    const u32 sp = (u32)c->r29;
    call_trace.push_back(which);
    for (int b = 0; b < 64; b++) {
        if (reads >> b & 1) call_trace.push_back(*abi_register(c, b));
    }
    gpr* r = &c->r0;
    const u32 given[4] = {(u32)r[4], (u32)r[5], (u32)r[6], (u32)r[7]};
    maybe_cut(calls_so_far);      // (a function that never returns: see cut_call)
    uint64_t kept_state = random_state;
    random_state = call_seed(calls_so_far + 1);
    calls_so_far++;
    for (int i = 0; i < 20; i++) random32();
    if (random32() % 4 == 0) {
        u32 a = random_pointer(4);
        u32 value = random_word();
        stand_in_store(a, value);
    }
    if (under_test != nullptr && under_test->addresses[0] != 0 && random32() % 4 == 0) {
        int count = 0;
        while (under_test->addresses[count] != 0) count++;
        u32 a = (under_test->addresses[random32() % count] & ~3u) - 0x20 + 4 * (random32() % 0x19);
        u32 value = random_word();
        stand_in_store(a, value);
    }
    steer_stand_in(0, given, 0, sp, c, 0);       // (memory only: no parameter it fills, no register it may change)
    note_result4(c, 0);
    random_state = kept_state;
    in_stand_in = false;
}

static void randomize_memory(const FuzzFunction& f) {
    for (u32 a = ARENA; a < ARENA + ARENA_BYTES; a += 4) word_at(a) = random_word();
    for (const u32* p = f.addresses; *p != 0; p++) {
        // The word it names, and its neighbours, since code often indexes from a fixed address.
        u32 base = *p & ~3u;
        for (u32 a = base - 0x20; a <= base + 0x40; a += 4) word_at(a) = random_word();
    }
    for (u32 a = STACK; a < STACK + 0x60; a += 4) word_at(a) = random_word();    // Arguments on the stack, and whatever's beside them.
}

// (2026-11-12) A function in fallback only: the filled memory its original reads, fresh each test (see fallback). Last in a
// start (make_start), so the registers and the stand-ins' seed a random state makes don't depend on how many words there are.
static void randomize_fresh_words() {
    if (!fallback) return;
    for (u32 o : fresh_words) {
        // (In the mod's sections, a word as main first gives them: most often a pointer, and never 0.)
        u32 v;
        if (in_mod_fill(o)) { do { v = (random32() % 8) ? random_pointer(4) : random_word(); } while (v == 0); }
        else v = random_word();
        word_at(o + 0x80000000u) = v;
    }
}

static void randomize_registers(recomp_context* c, const FuzzFunction& f) {
    gpr* r = &c->r0;
    r[0] = 0;
    for (int i = 1; i < 32; i++) r[i] = (gpr)(int64_t)(s32)random_word();
    for (int i = 0; i < 32; i++) {
        fpr* x = &c->f0 + i;
        x->u32l = random_float_bits();
        x->u32h = random_float_bits();
    }
    c->hi = random32();
    c->lo = random32();
    c->r29 = (gpr)(int64_t)(s32)STACK;
    c->r31 = (gpr)(int64_t)(s32)0x80001234;
    c->f_odd = &c->f0.u32h;
    c->status_reg = 0;
    c->mips3_float_mode = 0;
    // The parameters, as the kinds the function expects.
    const char* kinds = f.params;
    for (int i = 0; kinds[i] != 0; i++) {
        u32 v = 0;
        switch (kinds[i]) {
        case 'b': v = random32() % 32 == 0 ? 0 : random_pointer(1); break;
        case 'h': v = random32() % 32 == 0 ? 0 : random_pointer(2); break;
        case 'w': v = random32() % 32 == 0 ? 0 : random_pointer(4); break;
        case 'f': v = 0; break;
        case 'd': v = 0; break;     // (a double: below)
        case 'g': v = random_number_bits(); break;    // (a float that comes in an integer register or stack slot)
        default: v = random_word(); break;
        }
        if (f.port_pointers && (kinds[i] == 'b' || kinds[i] == 'h' || kinds[i] == 'w') && random32() % 8 == 0) {
            // (Only for a function with the port's additions, fuzz.h: one of its fixed game addresses instead.)
            int count = 0;
            for (const u32* p = f.addresses; *p != 0; p++) count++;
            if (count != 0) v = f.addresses[random32() % count];
        }
        if (kinds[i] == 'f') {
            if (i < 2) (&c->f0 + 12 + 2 * i)->u32l = random_float_bits();
        } else if (kinds[i] == 'd') {
            // (A double, the first or second parameter -- in f12:f13 or f14:f15 -- as a float there would be: the even register's
            //  64 bits. TESTER CHANGE for 0.81, 2026-11-12, agent "refused2")
            if (i < 2 && use_double) (&c->f0 + 12 + 2 * i)->u64 = random_double_bits();
            else if (i < 2) (&c->f0 + 12 + 2 * i)->u32l = random_float_bits();     // (FUZZ_NO_DOUBLE: as a float, as before)
        } else if (i < 4) {
            r[4 + i] = (gpr)(int64_t)(s32)v;
        } else {
            word_at(STACK + 4 * i) = v;
        }
    }
#if FUZZ_MOD_NATIVE
    // (The user's rule (3), 2026-11-13: a native function of the mod compiler's fast convention -- its parameters by register.
    //  Nothing for any other function: its draws are as before.)
    if (native_rules != nullptr && use_nm_fast) {
        for (int i = 0; i < native_rules->fast_count; i++) {
            const FuzzNarrow& p = native_rules->fast[i];
            const u32 v = p.kind == 'f' ? random_float_bits() : p.kind == 'w' ? (random32() % 32 == 0 ? 0 : random_pointer(4)) : random_word();
            if (p.where < 32) r[p.where] = (gpr)(int64_t)(s32)v;
            else if (p.where < 64) (&c->f0 + (p.where - 32))->u32l = v;
            else word_at(STACK + (u32)(p.where - 0x100)) = v;
        }
    }
#endif
}

// What a call changed, ignoring its own frame: sorted (offset, final value), the bytes that ended up different. Undoes it all.
struct Change { uint64_t offset; u8 value; };
static std::vector<Change> collect_and_undo() {
    // (The bytes written, in address order, each once: `seen`, above. Kept from run to run, so their room is only ever made once.)
    static std::vector<uint64_t> offsets;
    static std::vector<u8> finals;
    offsets.clear();
    finals.clear();
    std::sort(seen_used.begin(), seen_used.end());
    for (u32 page : seen_used) {
        SeenPage*& p = seen_table[page];
        for (u32 w = 0; w < 64; w++) {
            for (uint64_t word = p->bits[w]; word != 0; word &= word - 1) {
                const uint64_t o = ((uint64_t)page << 12) + w * 64 + __builtin_ctzll(word);
                offsets.push_back(o);
                finals.push_back(lift_rdram[o]);
            }
            p->bits[w] = 0;
        }
        seen_spare.push_back(p);
        p = nullptr;
    }
    seen_used.clear();
    for (const Undo& u : undo_list) lift_rdram[u.offset] = u.before;
    undo_list.clear();
    const uint64_t frame_low = (STACK - 0x4000u) - 0x80000000u, frame_high = own_memory_end();   // (see own_memory_end)
    std::vector<Change> changes;
    for (size_t i = 0; i < offsets.size(); i++) {
        // (A byte's address within a word is flipped; the frame's bounds are whole words, so that doesn't matter here.)
        if (offsets[i] >= frame_low && offsets[i] < frame_high) continue;
        if (finals[i] != lift_rdram[offsets[i]]) changes.push_back({offsets[i], finals[i]});
    }
    return changes;
}

static bool kept_registers_same(const recomp_context& before, const recomp_context& after) {
    // (And f20-f31, both halves of each even register: lift.py counts them as kept when the original saves and restores them,
    // kept_floats, so the original has to really give them back. Added 2026-11-07.)
    for (int i = 20; i < 32; i += 2) {
        const fpr& x = (&before.f0)[i];
        const fpr& y = (&after.f0)[i];
        if (x.u32l != y.u32l || x.u32h != y.u32h) return false;
    }
    return before.r16 == after.r16 && before.r17 == after.r17 && before.r18 == after.r18 && before.r19 == after.r19 &&
           before.r20 == after.r20 && before.r21 == after.r21 && before.r22 == after.r22 && before.r23 == after.r23 &&
           before.r29 == after.r29 && before.r30 == after.r30 && before.r31 == after.r31;
}

// ---- An original whose own frame was overwritten (2026-11-10) ----
// "kept-registers@0:converted=0,original=0" says the ORIGINAL came back with a saved register changed (it has no numbers:
// the report's two zeros are just that). Of the 43 functions DIFFERENT in 0.73.0, 17 said so, and in every one looked into
// the register was not changed by the function's code: it was saved in the frame as it came in, the slot was overwritten,
// and the epilogue loaded what was there. What overwrote it:
//   - a stand-in "filling" through a frame address part 2 had planted in the start (func_80038DD0: input_to_state copied
//     `sp + 0x30`, one side of the function's own loop compare, into the word it passes its callee);
//   - the function's own loop over a local array, its count a random word or a random callee's result (rw_800EDCF4: nine
//     floats over s0-s7, fp and ra; func_80143628: 254 entries of 8 bytes in a 0x198 frame, which also zeroed the stack
//     parameter it reads again);
//   - a store to a table of the game's that lies just under this tester's stack, with a random index (func_800025B0:
//     0x805EF4A0[n], n a random halfword).
// None is a game state: the game would have returned to a random address. And the converted function cannot match it by
// construction -- it keeps those registers in C++ variables and never loads them back -- so no conversion could pass.
//
// So such a test is set aside, as a NaN operand's is, and another runs in its place (main). Only when the frame shows it:
// every saved register that came back changed must have a slot (the converted function's own frame.keep says where: the
// same offsets, lift.py saved_slots), and the original's frame, as its run left it, must hold in that slot exactly what
// came back. An original that changes a register it never saved, or saves one and gives back something else -- half a
// function, a wrong calling rule: what this check is for -- has no such slot or no such value, and is DIFFERENT as before.
static std::vector<std::pair<u32, int>> kept_slots;     // (address, register: GPR n is n, float register n 32 + n) -- the converted run's
static std::vector<u8> unkept_frame;                     // The original's frame at the end of its run, if it didn't keep its registers.
void lift_fuzz_note_keep(u32 address, int reg) { kept_slots.push_back({address, reg}); }
static bool is_kept_slot(u32 address) {
    for (const std::pair<u32, int>& k : kept_slots) {
        const u32 bytes = k.second >= 32 ? 8 : 4;
        if (address + 4 > k.first && address < k.first + bytes) return true;
    }
    return false;
}
static const u32 UNKEPT_LOW = STACK - 0x4000u;
static void note_unkept_frame(bool kept) {
    unkept_frame.clear();
    if (!kept) unkept_frame.assign(lift_rdram + (UNKEPT_LOW - 0x80000000u), lift_rdram + (STACK - 0x80000000u));
}
static bool frame_was_overwritten(const recomp_context& before, const recomp_context& after) {
    if (unkept_frame.empty() || before.r29 != after.r29) return false;
    auto slot_word = [&](int reg, u32* word) {
        for (const std::pair<u32, int>& k : kept_slots) {
            if (k.second != reg || k.first < UNKEPT_LOW || k.first + 4 > STACK) continue;
            std::memcpy(word, unkept_frame.data() + (k.first - UNKEPT_LOW), 4);
            return true;
        }
        return false;
    };
    // (2026-11-10, the user's rule: "Skip only if it would crash". A test is set aside only when the return address is among
    //  the registers that came back from the overwritten frame: in the game the original would then jump to whatever was
    //  written there, so the input can't be a game state. An overrun that spoils only s-registers or floats, which the game
    //  could survive, stays a difference.)
    bool return_address_lost = false;
    for (int i : {16, 17, 18, 19, 20, 21, 22, 23, 30, 31}) {
        if ((&before.r0)[i] == (&after.r0)[i]) continue;
        u32 word;
        // (The epilogue's lw sign-extends the word into the register.)
        if (!slot_word(i, &word) || (gpr)(int64_t)(s32)word != (&after.r0)[i]) return false;
        if (i == 31) return_address_lost = true;
    }
    for (int i = 20; i < 32; i += 2) {
        const fpr& x = (&before.f0)[i];
        const fpr& y = (&after.f0)[i];
        if (x.u32l == y.u32l && x.u32h == y.u32h) continue;
        // (sdc1 / ldc1: the pair's high word first, as keep_double stores it.)
        u32 high, low;
        if (!slot_word(32 + i, &high)) return false;
        bool found = false;
        for (const std::pair<u32, int>& k : kept_slots) {
            if (k.second == 32 + i && k.first >= UNKEPT_LOW && k.first + 8 <= STACK) {
                std::memcpy(&low, unkept_frame.data() + (k.first + 4 - UNKEPT_LOW), 4);
                found = true;
                break;
            }
        }
        if (!found || high != y.u32h || low != y.u32l) return false;
    }
    return return_address_lost;
}

static bool floats_same(u32 a, u32 b) {
    if (a == b) return true;
    float x, y;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    return std::isnan(x) && std::isnan(y);    // Which NaN an operation produces isn't something the game relies on.
}

// A register by its MIPS name, in the context.
static u32* register_named(recomp_context* c, const char* name) {
    static const char* const names[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                                          "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
    for (int i = 1; i < 32; i++) {
        if (std::strcmp(names[i], name) == 0) return (u32*)(&c->r0 + i);    // (The low half: these hold 32-bit values.)
    }
    if (name[0] == 'f' && name[1] >= '0' && name[1] <= '9') {
        int i = std::atoi(name + 1);
        fpr* x = &c->f0 + (i & ~1);
        return (i & 1) ? &x->u32h : &x->u32l;    // (Odd float registers are the top half of the pair, in the N64's 32-bit mode.)
    }
    return nullptr;
}

// Whether an original function's behaviour depends on a register it reads before setting (on some path). Runs it twice from the same
// random state, with only that register different, and compares everything a caller could see: memory, the calls it makes, and the
// results in v0, v1, f0 and f1. The instructions that read the register have to have run, or it proves nothing.
//
//     fuzz.exe --independent <function> <register> <tests> <instruction numbers that read it, comma-separated>
//
// Prints: name register tests compared differences read-instructions-reached(1/0) first-difference
static int independence(const FuzzFunction& f, const char* reg, long tests, const char* needed_list) {
    std::vector<int> needed;
    for (const char* p = needed_list; *p;) {
        needed.push_back(std::atoi(p));
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    long compared = 0, different = 0;
    std::string first = "-";
    const ULONGLONG started = fuzz_clock_ms();
    long tests_run = 0;
    for (long t = 0; t < tests; t++) {
        if ((t & 255) == 0 && t >= 256 && fuzz_clock_ms() - started > MOST_MILLISECONDS) break;
        if ((t & 1023) == 0 || touched_pages.size() > 256) give_back_touched_pages();
        tests_run++;
        randomize_memory(f);
        recomp_context start;
        std::memset(&start, 0, sizeof(start));
        randomize_registers(&start, f);
        test_seed = ((uint64_t)random32() << 32) | random32();
        u32 other = random_word();
        if (other == *register_named(&start, reg)) other ^= 0x5A5A5A5Au;

        recomp_context runs[2] = {start, start};
        if (reg[0] == 'f' && reg[1] >= '0' && reg[1] <= '9') *register_named(&runs[1], reg) = other;
        else *(gpr*)register_named(&runs[1], reg) = (gpr)(int64_t)(s32)other;    // (A whole register, sign-extended as the N64 keeps it.)
        std::vector<Change> changes[2];
        Trace traces[2];
        bool finished[2];
        std::vector<u8> reached_now(f.instructions + 1, 0);
        for (int k = 0; k < 2; k++) {
            runs[k].f_odd = &runs[k].f0.u32h;
            accesses = 0;
            calls_so_far = 0;
            call_trace.clear();
            reset_memory_sum();
            finished[k] = true;
            try {
                f.original(lift_rdram, &runs[k], reached_now.data());
            } catch (const TooLong&) {
                finished[k] = false;
            }
            changes[k] = collect_and_undo();
            traces[k] = call_trace;
        }
        if (!finished[0] || !finished[1]) continue;
        compared++;
        for (int i = 0; i < f.instructions; i++) f.reached[i] |= reached_now[i];
        const char* what = nullptr;
        if (traces[0] != traces[1]) what = "calls";
        else if (changes[0].size() != changes[1].size()) what = "memory-count";
        else {
            for (size_t i = 0; i < changes[0].size() && !what; i++) {
                if (changes[0][i].offset != changes[1][i].offset || changes[0][i].value != changes[1][i].value) what = "memory";
            }
        }
        if (!what && ((u32)runs[0].r2 != (u32)runs[1].r2 || (u32)runs[0].r3 != (u32)runs[1].r3)) what = "v0-v1";
        if (!what && (!floats_same(runs[0].f0.u32l, runs[1].f0.u32l) || !floats_same(runs[0].f0.u32h, runs[1].f0.u32h))) what = "f0-f1";
        if (what && ++different == 1) first = what;
    }
    bool all_read = true;
    for (int i : needed) {
        if (i < 0 || i >= f.instructions || !f.reached[i]) all_read = false;
    }
    std::printf("%s %s %ld %ld %ld %d %s\n", f.name, reg, tests_run, compared, different, all_read ? 1 : 0, first.c_str());
    return 0;
}

// ---- One test, and what it found ----

// The tests' totals and the first difference, in the report's form.
static long compared = 0, different = 0;
static std::string first = "-";
static bool show_differences = false;
static bool first_shown_trace = true;
static const recomp_context* showing = nullptr;
static const std::vector<Change>* showing_real = nullptr;
static const std::vector<Change>* showing_side = nullptr;

static void note(const char* what, uint64_t where, u32 got, u32 wanted) {
    if (++different == 1) {
        char text[120];
        std::snprintf(text, sizeof(text), "%s@%llX:converted=%X,original=%X", what, (unsigned long long)where, got, wanted);
        first = text;
        if (show_differences && showing != nullptr) {
            std::printf("%s\n a0=%08X a1=%08X a2=%08X a3=%08X f12=%08X f14=%08X slots:", text, (u32)showing->r4, (u32)showing->r5, (u32)showing->r6,
                        (u32)showing->r7, showing->f12.u32l, showing->f14.u32l);
            for (u32 a = STACK + 0x10; a < STACK + 0x30; a += 4) std::printf(" %08X", word_at(a));
            std::printf("\n original wrote:");
            for (const Change& c : *showing_real) std::printf(" %08llX=%02X", (unsigned long long)(c.offset + 0x80000000ull) ^ 3, c.value);
            std::printf("\n converted wrote:");
            for (const Change& c : *showing_side) std::printf(" %08llX=%02X", (unsigned long long)(c.offset + 0x80000000ull) ^ 3, c.value);
            std::printf("\n");
        }
    }
}

// A test's starting state, made from the random generator's state: memory, then the registers and parameters, then the seed the
// stand-ins draw their results from. The same random state always makes the same start (what the originals and stand-ins write is
// undone after every test, so the memory outside what's randomized here never changes).
static void make_start(const FuzzFunction& f, recomp_context* start) {
    randomize_memory(f);
    std::memset(start, 0, sizeof(*start));
    randomize_registers(start, f);
    test_seed = ((uint64_t)random32() << 32) | random32();
    cut_call = f.never_returns && cut_allowed ? (long)(1 + (test_seed >> 40) % 64) : 0;    // (see cut_call: from the seed, so no other draw changes)
    randomize_fresh_words();
}

// How a run of one version ended: it returned, it ran away (TooLong: the test is skipped if both do), or it stopped where the
// game would have (stop_here). Or, in part 2, the original fed a NaN into its float arithmetic (NAN_CHECK in fuzz.h): no game
// state does that, so the test is skipped.
enum Ending { FINISHED, TOO_LONG, STOPPED, NOT_A_GAME_STATE };

// Runs the original (reached given) or the converted function (reached null). A stop (Stopped, from stop_here) comes back here;
// what the version wrote up to then stays noted in undo_list, as for any other run.
__attribute__((noinline)) static Ending run_version(const FuzzFunction& f, recomp_context* c, u8* reached) {
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    args_begin(f, c);
#endif
    stop_armed = true;
    try {
        if (reached != nullptr) f.original(lift_rdram, c, reached);
        else f.converted(lift_rdram, c);
    } catch (const Stopped&) {
        stop_armed = false;
        return STOPPED;
    } catch (const TooLong&) {
        stop_armed = false;
        return TOO_LONG;
    } catch (const NotAGameState&) {
        stop_armed = false;
        return NOT_A_GAME_STATE;
    }
    stop_armed = false;
    return FINISHED;
}

// How a test came out: not compared (both versions ran away, or the start was no game state), the two ended differently (one ran
// away or stopped and the other didn't: a difference), or compared.
enum Outcome { NOT_COMPARED, ENDED_DIFFERENTLY, COMPARED };
static u32 calls_by_original;      // How many functions the original called in the last test (the stand-ins' results part 2 can set).
static long nan_set_aside;         // How many tests run_both set aside for a NaN operand in the original (see there).
static long nan_made_set_aside;    // (2026-11-13) of those, how many for a NaN the original made itself (FUZZ_NAN_MADE)
static bool long_retry = false;    // (part 4, some of its tests: both versions ran past the limit -- run them again with more. run_both)
static bool original_long = false; // (the original's last run ran past the access limit, or needed more room to finish. Part 5's shrink5)

static bool original_saw_nan = false;      // (the last original run stopped on a NaN: NOT_A_GAME_STATE. Part 4 mends such a start)
static Ending run_original(const FuzzFunction& f, recomp_context* real, std::vector<u8>& reached_now) {
    real->f_odd = &real->f0.u32h;
    accesses = 0;
#if FUZZ_MOD_NATIVE
    loop_passes = 0;        // (the user's rules (3) and (4), 2026-11-13: native candidates only)
    original_touched.clear();
    lift_fuzz_watching_mod = native_rules != nullptr && use_nm_outside;
#endif
    start_steps();          // (the step limit, 2026-11-14: rules084)
    calls_so_far = 0;
    call_trace.clear();
    reset_memory_sum();
    call_outputs.clear();
    frame_fills.clear();
    buffers_given.clear();
    in_stand_in = false;
    running_original = true;
    lift_fuzz_nan_noted = false;
    lift_fuzz_nan_made_seen = false;
    lift_fuzz_nan_stops = steering_now || in_part3 || part1_fills;     // (part 3's starts aren't all game states either, nor a test whose locals a stand-in filled with random words)
    // (fallback, 2026-11-12: a part-1 test with a NaN operand is set aside however far it runs, so it stops there, saving the
    //  rest of the original and the whole converted run. func_80022CEC had 49 tests of 50 so; nothing else changes.)
    if (fallback) lift_fuzz_nan_stops = true;
    // (fallback, 2026-11-12: part 1's original notes what it reads, for fresh_words. Part 2 notes its own reads already.)
    const bool learning = fresh_learning && !lift_fuzz_noting_reads;
    if (learning) {
        lift_fuzz_reads.clear();
        lift_fuzz_noting_reads = true;
    }
    compares_made = 0;
    compares_cut = false;
    const Ending e = run_version(f, real, reached_now.data());
    original_stepped = e == TOO_LONG && step_stopped;
    original_limit_used = access_limit;          // (the write cut: this run's limit and its writes, to run it again to a cut)
    original_writes_made = nonframe_writes;
#if FUZZ_MOD_NATIVE
    lift_fuzz_watching_mod = false;
#endif
    // (Or made more comparisons than that, a loop with no memory access in it: see count_compare.)
    original_long = e == TOO_LONG || accesses > MOST_ACCESSES || compares_made > MOST_ACCESSES || compares_cut;
    if (learning) {
        lift_fuzz_noting_reads = false;
        learn_fresh_words();
        lift_fuzz_reads.clear();
        lift_fuzz_compares.clear();
    }
    running_original = lift_fuzz_nan_stops = false;
    original_saw_nan = e == NOT_A_GAME_STATE && !compares_cut;     // (count_compare's stop is no NaN)
    return e;
}

static Ending run_converted(const FuzzFunction& f, recomp_context* side) {
    side->f_odd = &side->f0.u32h;
    accesses = 0;
#if FUZZ_MOD_NATIVE
    loop_passes = 0;        // (the user's rules (3) and (4), 2026-11-13: native candidates only)
    outside_noted.clear();
#endif
    start_steps();          // (the step limit, 2026-11-14: rules084)
    calls_so_far = 0;
    call_trace.clear();
    reset_memory_sum();
    kept_slots.clear();      // (its frame.keep calls say where the saved registers are: frame_was_overwritten)
    running_converted = true;     // (note_converted_frame_write)
    in_stand_in = false;
    private_before_converted();     // (the mod's private data in PC memory: only with a private table, see there)
    const Ending e = run_version(f, side, nullptr);
    converted_stepped = e == TOO_LONG && step_stopped;
    converted_limit_used = access_limit;
    converted_writes_made = nonframe_writes;
    running_converted = in_stand_in = false;
    private_after_converted();
    return e;
}

#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
// (The argument compare: see there.) The first argument the original changed or wrote before a call that the converted
// function had otherwise there, the callee not proved never to read it. Checked before the list of calls (run_both), up to
// the first call where the two called different stand-ins.
static bool args_differ(const Trace& original, const Trace& converted) {
    const size_t n = std::min(original.args.size(), converted.args.size());
    for (size_t k = 0; k < n; k++) {
        const ArgCall& o = original.args[k];
        const ArgCall& c = converted.args[k];
        if (o.which != c.which) return false;      // (another callee from here on: the list of calls shows that)
        for (int i = 0; i < 8; i++) {
            if (!((o.changed & ~o.skip) >> i & 1) || o.regs[i] == c.regs[i]) continue;
            if (++different == 1) {
                char text[120];
                std::snprintf(text, sizeof(text), "args@%zX:%s:converted=%X,original=%X", k, ARG_NAMES[i], c.regs[i], o.regs[i]);
                first = text;
            }
            return true;
        }
        for (int j = 0; j < 16; j++) {
            if (!((o.written & o.stack) >> j & 1) || o.stack_words[j] == c.stack_words[j]) continue;
            if (++different == 1) {
                char text[120];
                std::snprintf(text, sizeof(text), "args@%zX:sp+0x%X:converted=%X,original=%X", k, 0x10 + 4 * j, c.stack_words[j], o.stack_words[j]);
                first = text;
            }
            return true;
        }
    }
    return false;
}
#endif

// The original, then the converted function, from the same start, compared as the comment at the top says. reached_now gets how
// many times each of the original's instructions ran, and f.reached what any compared test reached.
//
// second_chance: when one version ran past the limit on memory accesses and the other didn't, the one that didn't
// finish runs again with 16 times the limit. The two needn't make the same number of accesses (the converted one keeps values in
// variables the original reloads), so steered inputs that take a loop up to the limit would otherwise show a "difference" that is
// only the count. One that still doesn't finish is a real difference, and noted as part 1 notes it.
static Outcome run_both(const FuzzFunction& f, const recomp_context& start, std::vector<u8>& reached_now, bool second_chance) {
    // (2026-11-12, proposed by the agent "different", approved by the user 2026-11-12) gp, as the game has it. In this build gp is
    // no free register: the six-man engine keeps the match's wrestler count in it (six-engine/work/kb/context.md, SIX_LOOPS:
    // 173 slot loops end at `slt/sltu rs, rt, $gp`), set only by rw_run_overlay -- 4, or rw_match_count when the match
    // overlay starts, which rw_rumble_fill makes 4 or 6 (SIX_NSLOTS) -- and read, never set, by everything else. Drawn as any
    // word, gp ran those loops hundreds of times past the six slots: func_800E2344 filled a 40-entry local array past its
    // end into its saved registers, which no game state can do. Now whatever a part chose for it becomes a count from 0 to
    // 32, the same for both versions: the engine's capacity (spec.py: room for 32 slots, SIX_SLOTS) and below. Only 4 and 6
    // happen today, but the engine's code is written for any count up to its capacity, and a narrower range (tried: 4 to 6)
    // left code for the larger counts unreached in eight functions (func_801260A4: 103 of 184).
    // (A value already 0 to 32 stays as it is, so a start run twice -- a second chance, a kept start -- runs the same.
    //  FUZZ_NO_GP_COUNT: gp any word, as before.)
    static const bool gp_count = std::getenv("FUZZ_NO_GP_COUNT") == nullptr;
    if (gp_count && (u32)start.r28 > 32u) const_cast<recomp_context&>(start).r28 = (gpr)(int64_t)(s32)((u32)start.r28 % 33u);
    native_start(start);        // (the user's rules (1) and (2) of 2026-11-13: native candidates only)
    recomp_context real = start;
    access_limit = MOST_ACCESSES;
    Ending original_ending = run_original(f, &real, reached_now);
    note_unkept_frame(original_ending != FINISHED || kept_registers_same(start, real));    // (before collect_and_undo puts memory back)
    bool nan_operand = lift_fuzz_nan_noted;
    calls_by_original = calls_so_far;
    lift_fuzz_noting_reads = false;      // (part 2 notes the original's reads only)
    std::vector<Change> real_changes = collect_and_undo();
    if (original_ending == NOT_A_GAME_STATE) return NOT_COMPARED;     // (nothing to compare)
    Trace real_trace = call_trace;

    recomp_context side = start;
    Ending converted_ending = run_converted(f, &side);
    std::vector<Change> side_changes = collect_and_undo();
    private_restore();

    if (second_chance && (original_ending == TOO_LONG) != (converted_ending == TOO_LONG)) {
        access_limit = MOST_ACCESSES * 16;
        if (original_ending == TOO_LONG) {
            real = start;
            std::fill(reached_now.begin(), reached_now.end(), 0);
            Trace converted_trace = call_trace;   // (run_original clears call_trace: without this the comparison of calls
            original_ending = run_original(f, &real, reached_now); //  below held the original's calls against themselves. Found 2026-11-08)
            note_unkept_frame(original_ending != FINISHED || kept_registers_same(start, real));
            nan_operand = lift_fuzz_nan_noted;
            calls_by_original = calls_so_far;
            real_changes = collect_and_undo();
            real_trace = call_trace;
            call_trace = converted_trace;
            // (2026-11-10) And the converted function again, after it. What a stand-in writes through the pointers it is given
            // (steer_stand_in's call_outputs) is worked out in the original's run and repeated in the converted one's -- and
            // the converted one ran while call_outputs held only the calls the original made before it was stopped. For every
            // call after that it was given a callee that filled nothing, where the original, run again here, got one that
            // did: a difference in the calls that is only the tester's (func_800DBE48: the frame word at sp + 0x28 its
            // callees are given, 0x39 against 8, in 4 of 30,000 tests, none of them without the fills). Run again, it gets
            // the same callees as the original's whole run.
            if (original_ending != NOT_A_GAME_STATE && original_ending != TOO_LONG) {
                side = start;
                converted_ending = run_converted(f, &side);
                side_changes = collect_and_undo();
                private_restore();
            }
        } else {
            side = start;
            converted_ending = run_converted(f, &side);
            side_changes = collect_and_undo();
            private_restore();
        }
        access_limit = MOST_ACCESSES;
        if (original_ending == NOT_A_GAME_STATE) return NOT_COMPARED;
    }
    if ((long_retry || fallback) && original_ending == TOO_LONG && converted_ending == TOO_LONG) {
        // (Part 4, now and then: both ran past the limit. A loop with a fixed count of 5,787 passes -- func_800119A4, about
        //  52,000 accesses -- is no endless loop, but at the usual limit no test of it was ever compared. Both run again with 16
        //  times the room, and one that still runs past it while the other finished gets 64 times (the second chance's rule:
        //  the two needn't make the same number of accesses). Then they are compared as any test is, a difference included.)
        // (2026-11-12) And every such test of a function in fallback, parts 1 to 3 too: see fallback. The NaN operand that
        //  counts is the longer run's, not the shorter one's (nan_operand).
        access_limit = MOST_ACCESSES * 16;
        real = start;
        std::fill(reached_now.begin(), reached_now.end(), 0);
        original_ending = run_original(f, &real, reached_now);
        note_unkept_frame(original_ending != FINISHED || kept_registers_same(start, real));
        nan_operand = lift_fuzz_nan_noted;
        calls_by_original = calls_so_far;
        real_changes = collect_and_undo();
        real_trace = call_trace;
        if (original_ending != NOT_A_GAME_STATE) {
            side = start;
            converted_ending = run_converted(f, &side);
            side_changes = collect_and_undo();
            private_restore();
            if (fallback64 && original_ending == TOO_LONG && converted_ending == TOO_LONG) {
                // (fallback64, approved 2026-11-12: see there. Both again at 64x, the original first; the converted one only
                //  when the original finished, since one that still runs past it makes the test not compared either way.)
                access_limit = MOST_ACCESSES * 64;
                real = start;
                std::fill(reached_now.begin(), reached_now.end(), 0);
                original_ending = run_original(f, &real, reached_now);
                note_unkept_frame(original_ending != FINISHED || kept_registers_same(start, real));
                nan_operand = lift_fuzz_nan_noted;
                calls_by_original = calls_so_far;
                real_changes = collect_and_undo();
                real_trace = call_trace;
                if (original_ending != NOT_A_GAME_STATE && original_ending != TOO_LONG) {
                    side = start;
                    converted_ending = run_converted(f, &side);
                    side_changes = collect_and_undo();
                    private_restore();
                }
            } else if ((original_ending == TOO_LONG) != (converted_ending == TOO_LONG)) {
                access_limit = MOST_ACCESSES * 64;
                if (original_ending == TOO_LONG) {
                    real = start;
                    std::fill(reached_now.begin(), reached_now.end(), 0);
                    Trace converted_trace = call_trace;   // (as the second chance above: run_original clears call_trace)
                    original_ending = run_original(f, &real, reached_now);
                    note_unkept_frame(original_ending != FINISHED || kept_registers_same(start, real));
                    nan_operand = lift_fuzz_nan_noted;
                    calls_by_original = calls_so_far;
                    real_changes = collect_and_undo();
                    real_trace = call_trace;
                    call_trace = converted_trace;
                    // (fallback: the converted function again too, for the reason the second chance above gives -- what
                    //  the callees fill in comes from the original's run, which only now made all its calls. 2026-11-12)
                    if (fallback && original_ending != NOT_A_GAME_STATE && original_ending != TOO_LONG) {
                        side = start;
                        converted_ending = run_converted(f, &side);
                        side_changes = collect_and_undo();
                        private_restore();
                    }
                } else {
                    side = start;
                    converted_ending = run_converted(f, &side);
                    side_changes = collect_and_undo();
                    private_restore();
                }
            }
        }
        access_limit = MOST_ACCESSES;
        if (original_ending == NOT_A_GAME_STATE) return NOT_COMPARED;
    }
    // (Part 1: an original that had a NaN operand ran on to the end, as before, but no game state gives it one -- the N64 stops
    //  there -- and which NaN x86 hands back depends on the order clang put the operands in: not compared, as parts 2 and 3
    //  already don't, where it stopped the original (fuzz.h). main runs one more part-1 test for each, so part 1 compares as
    //  many as before.)
    if (nan_operand) {
        nan_set_aside++;
        if (lift_fuzz_nan_made_seen) nan_made_set_aside++;
        return NOT_COMPARED;
    }
    // (2026-11-10) Nor one whose original came back with saved registers that are its overwritten frame: see
    // frame_was_overwritten. Counted with the NaN ones, so part 1 still compares as many tests as it was asked for.
    if (frame_was_overwritten(start, real)) {
        nan_set_aside++;
        return NOT_COMPARED;
    }
#if FUZZ_MOD_NATIVE
    // (The user's rule (4) of 2026-11-13: the original reached outside an object into a neighbouring private object's N64 copy,
    //  and the native code indexed past that array's end to the same place. See outside_into_private.)
    if (outside_into_private()) {
        outside_set_aside++;
        return NOT_COMPARED;
    }
#endif
    showing = &start;
    showing_real = &real_changes;
    showing_side = &side_changes;

    const bool original_finished = original_ending != TOO_LONG, converted_finished = converted_ending != TOO_LONG;
    bool cut_compare = false;       // (the write cut: a test compared at its cut, not counted as compared -- see below)
    if (!original_finished && !converted_finished) {
        // (The step limit, 2026-11-14: a test it stopped either version of is counted, so the report's reader can see how many.
        //  One stopped by steps and the other by accesses is a difference, always -- see there.)
        if (original_stepped && converted_stepped) {
            steps_set_aside_both++;
        } else if (original_stepped || converted_stepped) {
            steps_mixed_different++;
            note("step-mixed", 0, converted_stepped, original_stepped);
            compared++;
            return ENDED_DIFFERENTLY;
        }
        // (The write cut, 2026-11-15 -- see there. Both ran away; for a function it is on for, each made K or more writes
        //  outside its frame before its limit, so run again from the same start with the same limits each stops just before
        //  its K-th, and what they did up to there -- the first K - 1 writes, every call -- is compared below as for any
        //  stopped test. Reach only: the report's tests and compared columns don't count it, as part 4's don't.)
        if (!write_cut_here || original_writes_made < 2 || converted_writes_made < 2) return NOT_COMPARED;
        const u32 limit_o = original_limit_used, limit_c = converted_limit_used;
        write_cut_at = std::min(original_writes_made, converted_writes_made);
        access_limit = limit_o;
        real = start;
        std::fill(reached_now.begin(), reached_now.end(), 0);
        original_ending = run_original(f, &real, reached_now);
        const bool original_cut = original_ending == STOPPED && write_cut_hit;
        nan_operand = lift_fuzz_nan_noted;
        calls_by_original = calls_so_far;
        real_changes = collect_and_undo();
        real_trace = call_trace;
        access_limit = limit_c;
        side = start;
        converted_ending = run_converted(f, &side);
        const bool converted_cut = converted_ending == STOPPED && write_cut_hit;
        side_changes = collect_and_undo();
        private_restore();
        write_cut_at = 0;
        access_limit = MOST_ACCESSES;
        // (Both runs repeat the ones before up to the cut, so both stop there; anything else -- a NaN operand on the way,
        //  a run that did not repeat -- is not compared, as before.)
        if (!original_cut || !converted_cut || nan_operand || original_ending == NOT_A_GAME_STATE) return NOT_COMPARED;
        cut_compare = true;
        write_cut_tests++;
    }
    if (original_finished != converted_finished) {
        note("ran-long", 0, converted_finished, original_finished);
        compared++;
        return ENDED_DIFFERENTLY;
    }
    if (original_ending != converted_ending) {
        // (One stopped -- a division the game can't go on from, see stop_here -- where the other didn't.)
        note("stopped", 0, converted_ending == STOPPED, original_ending == STOPPED);
        compared++;
        return ENDED_DIFFERENTLY;
    }
    if (!cut_compare) compared++;       // (a write-cut test: what it reaches and any difference count, not the test itself)
    for (int i = 0; i < f.instructions; i++) f.reached[i] |= reached_now[i];

    bool same = true;
    settle_own_words(real_trace);       // (the function's own words: see note_stack_word)
    settle_own_words(call_trace);
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT
    // (The argument compare first, when it is on: a dropped argument shows by its own name, args@<call>:<register>, even
    //  where the list of calls differs too -- so its controls show it finds a drop by itself. See args_differ.)
    if (args_differ(real_trace, call_trace)) {
        same = false;
    } else
#endif
    if (real_trace != call_trace) {
        same = false;
        size_t i = 0;
        while (i < real_trace.size() && i < call_trace.size() && real_trace[i] == call_trace[i]) i++;
        note("calls", i, i < call_trace.size() ? (u32)call_trace[i] : 0xFFFFFFFF, i < real_trace.size() ? (u32)real_trace[i] : 0xFFFFFFFF);
        if (show_differences && first_shown_trace) {
            first_shown_trace = false;
            size_t from = i > 12 ? i - 12 : 0;
            std::printf("trace around %zu (of %zu original, %zu converted):\n original:", i, real_trace.size(), call_trace.size());
            for (size_t k = from; k < i + 8 && k < real_trace.size(); k++) std::printf(" %08X", (u32)real_trace[k]);
            std::printf("\n converted:");
            for (size_t k = from; k < i + 8 && k < call_trace.size(); k++) std::printf(" %08X", (u32)call_trace[k]);
            std::printf("\n");
        }
    } else if (real_changes.size() != side_changes.size()) {
        same = false;
        size_t i = 0;
        while (i < real_changes.size() && i < side_changes.size() && real_changes[i].offset == side_changes[i].offset) i++;
        uint64_t where = i < real_changes.size() ? real_changes[i].offset : side_changes[i].offset;
        note("memory-count", where + 0x80000000ull, (u32)side_changes.size(), (u32)real_changes.size());
    } else {
        for (size_t i = 0; i < real_changes.size(); i++) {
            if (real_changes[i].offset != side_changes[i].offset || real_changes[i].value != side_changes[i].value) {
                same = false;
                note("memory", real_changes[i].offset + 0x80000000ull, side_changes[i].value, real_changes[i].value);
                break;
            }
        }
    }
#if FUZZ_MOD_NATIVE
    if (same && private_n64_touched) {
        // (The mod's private data: the converted run wrote its N64 place, which it must leave alone. See private_before_converted.)
        same = false;
        note("private-n64", private_n64_where, 0, 0);
    }
#endif
    // (Stopped part-way, neither has a result or has given back its registers: memory and calls are what there is.)
    const bool stopped = original_ending == STOPPED;
    if (same && !stopped && f.result == 1 && (u32)side.r2 != (u32)real.r2) {
        same = false;
        note("return", 0, (u32)side.r2, (u32)real.r2);
    }
    if (same && !stopped && f.result == 2 && !floats_same(side.f0.u32l, real.f0.u32l)) {
        same = false;
        note("float-return", 0, side.f0.u32l, real.f0.u32l);
    }
    // (A double result, f0:f1: its whole 64 bits, NaNs as for a float -- either NaN is the same. TESTER CHANGE for 0.81,
    //  2026-11-12, agent "refused2", approved by the user 16:3x. Noted with the word that differs: the low, f0, else the high.)
    if (use_double && same && !stopped && f.result == 3 && side.f0.u64 != real.f0.u64 && !(std::isnan(side.f0.d) && std::isnan(real.f0.d))) {
        same = false;
        const bool low = side.f0.u32l != real.f0.u32l;
        note("double-return", low ? 0 : 1, low ? side.f0.u32l : side.f0.u32h, low ? real.f0.u32l : real.f0.u32h);
    }
    // (abi.py's functions: the registers it keeps in step, as the original left them -- not where both stopped part-way.)
    for (int b = 0; b < 64 && same && !stopped && f.abi_out; b++) {
        if (!(f.abi_out >> b & 1)) continue;
        u32 got = *abi_register(&side, b), wanted = *abi_register(&real, b);
        if (b < 32 ? got != wanted : !floats_same(got, wanted)) {
            same = false;
            note("abi-register", b, got, wanted);
        }
    }
    // (2026-11-11, the user's decision.) An original that came back with saved registers changed -- it overwrote its own save
    // slots, the return address not among them (that test is set aside before here: frame_was_overwritten) -- is compared with
    // the converted function, which now puts its saved registers back from the same slots as the original's epilogue does
    // (lift_call.h LiftFrame): the original's registers against the converted's, the same registers, the same values. A converted
    // function that kept a register the original didn't, or lost one it kept, still differs. These checks are the last ones.
    if (same && !stopped && !in_part3 && !kept_registers_same(start, real)) {
        if (!kept_registers_same(real, side)) { same = false; note("kept-registers", 0, 0, 0); }
        return COMPARED;
    }
    if (same && !stopped && !kept_registers_same(start, real)) {
        // (Part 3: an original that doesn't give back its saved registers on a boundary input has overrun its own frame -- a
        //  count from memory used as a loop bound over a local array -- which the game never gives it, or it would crash. Not a
        //  difference in the conversion: part 1 still checks every function keeps them on ordinary inputs.)
        if (in_part3) return COMPARED;
        same = false;
        note("kept-registers", 0, 0, 0);
    }
    if (same && !stopped && !kept_registers_same(start, side)) {
        same = false;
        note("converted-kept-registers", 0, 0, 0);
    }
    return COMPARED;
}

// ---- Part 2: steered inputs (2026-11-07) ----
//
// Random inputs miss what needs several things at once: a pointer that leads to a structure whose flag is set and whose count is
// small, a table entry that matches a parameter, 96 list entries none of which is free, a called function's result that equals one
// particular number. About 200 functions showed no difference but couldn't be proved for that (2026-11-07: 5,107 instructions never
// reached). So for a function part 1 didn't wholly reach, part 2 steers, as coverage-guided testers do:
//
//   - Every test that reached something new is kept as a starting point: an instruction never reached before, or one reached a new
//     number of times (roughly: 1, 2, 3, 4-5, 6-7, 8-11, ...), which is how a loop that runs longer shows it's getting somewhere.
//   - A new test takes a kept start and changes a few of the things the original's path depended on: words of memory it READ (noted
//     as it ran: so the pointers it followed, the fields and table entries it looked at), its parameters, and what the functions it
//     called returned. The new values are the kinds that pass a check: the function's own constants (the dictionary), zero, one more
//     or less, a bit flipped, a value read elsewhere in the same test (so two things can be made equal), a pointer to memory that
//     holds something, a number as a float; or every word that held one value is given another (so all 96 entries of a list stop
//     being free at once).
//   - It runs and is compared exactly as a part 1 test is. What it reached counts the same way.
//
// This only adds tests. Part 1 is unchanged, the comparison is the same, and an instruction counts as reached only when a compared
// test ran it, so a function proved this way has every instruction run in a test the two versions agreed on, and none disagreed.
// (Tweak and its kinds are declared next to fuzz_stand_in, which applies the CALL_ ones.)

struct Kept {
    uint64_t base;                  // The random state the start is made from (make_start).
    std::vector<Tweak> tweaks;      // Then these, in order.
    std::vector<u32> reads;         // Memory words the original read (offsets into rdram, whole words), outside its own frame.
    u32 calls = 0;                  // How many functions it called.
    bool known = false;             // Whether reads and calls are known (part 1's starts are run once more to learn them).
    std::vector<u32> reached;       // Which instructions its test reached, a bit each.
    std::vector<std::pair<u32, u32>> compares;   // The comparisons its original made that came out unequal (FUZZ_COMPARE).
    // Part 4's (see Part 4): whether its stand-ins draw independent results (mixed_calls), whether what follows is known yet, and
    // what part 4 noted of its original's run -- each comparison with up to 4 of the values that came just before it (its likely
    // sources), the words read after a call (outside the stack: what a stand-in could change for it), and the calls' results.
    struct Compare { u32 a, b; u32 site; int op, taken, fall; u32 count; Event4 sources[5]; double x = 0, y = 0; u32 event = 0; };
    // (event, 2026-11-11: where in the run's events it came, as Compare4's: a branch and the set-less-than whose result it tests
    //  come at the same place.)
    struct Result { u32 call; u32 bit; u32 value; };
    bool mixed = false;
    bool known4 = false;
    // Part 5's (2026-11-12): whether its original's writes are known yet, and where they landed (game addresses, once each).
    bool known5 = false;
    std::vector<u32> writes;
    std::vector<Compare> compares4;
    std::vector<Event4> after_calls;
    std::vector<Result> results;
};

static const u64 FRAME_LOW = (STACK - 0x4000u) - 0x80000000u, FRAME_HIGH = STACK - 0x80000000u;
static const size_t MOST_KEPT = 8192, MOST_TWEAKS = 4096;

// The bucket a number of runs falls in, as a bit: 1, 2, 3, 4-5, 6-7, 8-11, 12-15, ... 192-255 (counts stop at 255).
static u32 count_bucket(u32 c) {
    if (c < 4) return 1u << c;
    int k = 31 - __builtin_clz(c);
    return 1u << (4 + 2 * (k - 2) + ((c >> (k - 1)) & 1));
}

struct Steering {
    const FuzzFunction& f;
    std::vector<u32> buckets_seen;      // For each instruction, the buckets of run counts some compared test has had.
    std::vector<Kept> kept;
    std::vector<size_t> gainers;        // The kept starts that reached an instruction no test had reached before.
    // Part 4: for each comparison in the code (FUZZ_COMPARE_KIND's site), which ways it has come out in part 4's compared tests,
    // a bit each (outcome4). One that has never gone one way guards something no test has reached, so part 4 aims at it.
    std::unordered_map<u32, u8> site_seen;
    // And the branches some kept start comes to, as part 4 noted it (learn4): a test that comes to a branch with a way still
    // unreached, which no kept start comes to, is kept even if it reached nothing new itself -- it is the only way there.
    std::unordered_map<u32, bool> site_in_kept;
    std::vector<std::pair<uint64_t, u32>> restore;     // The memory a test's tweaks changed, and what was there.
    bool five = false;                  // run4 is running part 5 (see Part 5), not part 4.
    u32 alias_to = 0;                   // (part 5: where the child's alias aims its store, or 0: alias_tweak5, run4)
    long tests_now = 0, last_gain = 0;  // Part 2's tests so far, and the one that last reached a new instruction.

    std::vector<u32> hits;              // For each instruction, how many compared tests have reached it.
    std::vector<int> rarest;            // The reached instructions fewest tests have reached (worked out now and then).

    explicit Steering(const FuzzFunction& fn) : f(fn), buckets_seen(fn.instructions, 0), hits(fn.instructions, 0) {}

    // A kept start to work from. Half the time one that reaches one of the instructions fewest tests have reached: that's where
    // the edge of what's been reached is, and the unreached paths branch off near it (as FairFuzz chooses). Otherwise half the
    // time one that first reached something, or any.
    size_t choose() {
        if (!rarest.empty() && random32() % 2) {
            const int target = rarest[random32() % rarest.size()];
            std::vector<size_t> with;
            for (size_t i = 0; i < kept.size(); i++) {
                if (kept[i].reached.size() * 32 > (size_t)target && (kept[i].reached[target / 32] >> (target % 32)) & 1) with.push_back(i);
            }
            if (!with.empty()) return with[random32() % with.size()];
        }
        return !gainers.empty() && random32() % 2 ? gainers[random32() % gainers.size()] : random32() % kept.size();
    }

    void find_rarest() {
        std::vector<std::pair<u32, int>> order;
        for (int i = 0; i < f.instructions; i++) {
            if (hits[i]) order.push_back({hits[i], i});
        }
        std::sort(order.begin(), order.end());
        rarest.clear();
        for (size_t i = 0; i < order.size() && i < 8; i++) rarest.push_back(order[i].second);
    }

    bool all_reached() const {
        for (int i = 0; i < f.instructions; i++) {
            if (!f.reached[i]) return false;
        }
        return true;
    }

    // A compared test: keeps its start if it reached something new.
    // (force: part 4's, keep it even if it reached nothing new -- see run4.)
    void consider(uint64_t base, std::vector<Tweak> tweaks, const std::vector<u8>& reached_now, std::vector<u32>* reads, u32 calls,
                  const std::vector<std::pair<u32, u32>>* compares = nullptr, bool force = false, bool counted = true) {
        bool fresh = false, gained = false;
        // (counted: a compared test, whose reach counts here. Part 5 also keeps some tests that weren't compared -- see run4 --
        //  only as starts to steer from: those leave the counts as they were.)
        for (int i = 0; i < f.instructions && counted; i++) {
            if (!reached_now[i]) continue;
            hits[i]++;
            u32 b = count_bucket(reached_now[i]);
            if (buckets_seen[i] & b) continue;
            if (buckets_seen[i] == 0) gained = true;
            buckets_seen[i] |= b;
            fresh = true;
        }
        if (gained) last_gain = tests_now;
        // (Part 4 has room for 4,096 starts of its own beyond part 2's.)
        if ((!fresh && !force) || kept.size() >= MOST_KEPT + (in_part4 ? 4096 : 0) || tweaks.size() > MOST_TWEAKS) return;
        Kept k;
        k.base = base;
        k.tweaks = std::move(tweaks);
        k.reached.assign((f.instructions + 31) / 32, 0);
        for (int i = 0; i < f.instructions; i++) {
            if (reached_now[i]) k.reached[i / 32] |= 1u << (i % 32);
        }
        if (reads != nullptr) {
            std::sort(reads->begin(), reads->end());
            reads->erase(std::unique(reads->begin(), reads->end()), reads->end());
            for (u32 o : *reads) {
                if (o < FRAME_LOW || o >= FRAME_HIGH) k.reads.push_back(o);
            }
            if (k.reads.size() > 2048) k.reads.resize(2048);
            k.calls = calls;
            k.known = true;
            if (compares != nullptr) k.compares = unique_compares(*compares);
        }
        if (gained) gainers.push_back(kept.size());
        kept.push_back(std::move(k));
    }

    u32& memory_word(u32 offset) { return *(u32*)(lift_rdram + offset); }

    static std::vector<std::pair<u32, u32>> unique_compares(std::vector<std::pair<u32, u32>> c) {
        std::sort(c.begin(), c.end());
        c.erase(std::unique(c.begin(), c.end()), c.end());
        if (c.size() > 256) c.resize(256);
        return c;
    }

    // Input-to-state: takes a comparison the parent's test made, a == b or a < b that came out one way, finds where one side came
    // from (a word it read, or a parameter, holding it whole, or a halfword or byte of one holding it), and puts the other side
    // there (or one more or less, for a less-than). A branch that compares something read with something else read, or with a
    // number worked out from the inputs, is passed the other way like that, where a random value almost never would be.
    bool input_to_state(Kept& child, const Kept& parent, recomp_context* start, const std::vector<u32>& params) {
        const std::pair<u32, u32>& c = parent.compares[random32() % parent.compares.size()];
        u32 from = c.first, to = c.second;
        if (random32() % 2) std::swap(from, to);
        if (random32() % 3 == 0) to += random32() % 2 ? 1 : -1;
        // (Sometimes a side is a value read plus or minus a little: `addiu v0, a3, -1; sltiu v0, v0, 9` compares one less than
        //  what was read. Then a word that holds `from` plus that little gets `to` plus it.)
        if (random32() % 4 == 0) {
            const s32 d = (s32)(random32() % 17) - 8;
            from += d;
            to += d;
        }
        std::vector<Tweak> found;
        auto look = [&](u32 v, u8 kind, u32 where) {
            if (v == from) found.push_back({kind, where, to});
            if ((v & 0xFFFF) == (from & 0xFFFF) && (from < 0x10000 || from >= 0xFFFF8000u)) found.push_back({kind, where, (v & 0xFFFF0000u) | (to & 0xFFFF)});
            if ((v >> 16) == (from & 0xFFFF) && (from < 0x10000 || from >= 0xFFFF8000u)) found.push_back({kind, where, (v & 0xFFFF) | (to << 16)});
            if (from < 0x100 || from >= 0xFFFFFF80u) {
                for (int b = 0; b < 32; b += 8) {
                    if (((v >> b) & 0xFF) == (from & 0xFF)) found.push_back({kind, where, (v & ~(0xFFu << b)) | ((to & 0xFF) << b)});
                }
            }
        };
        for (u32 o : parent.reads) look(memory_word(o), MEMORY_WORD, o);
        for (u32 p : params) {
            if (p < 32) look((u32)(&start->r0)[p], REGISTER, p);
        }
        if (found.empty()) return false;
        Tweak w = found[random32() % found.size()];
        apply(w, start);
        child.tweaks.push_back(w);
        return true;
    }

    // Makes a kept start again, its tweaks applied (the memory ones noted in `restore`, to be undone).
    void make(const Kept& k, recomp_context* start) {
        random_state = k.base;
        make_start(f, start);
        for (const Tweak& w : k.tweaks) apply(w, start);
    }

    void apply(const Tweak& w, recomp_context* start) {
        gpr* r = &start->r0;
        if (w.kind != FLOAT_REGISTER && own_frame_address(w.value)) return;     // (see own_frame_address: not a start the game can make)
        switch (w.kind) {
        case MEMORY_WORD: {
            // (the aligned rule: a stack parameter's word, STACK + 4 * i for the 5th parameter on, keeps its kind's alignment)
            const u32 sp = STACK - 0x80000000u;
            const u32 v = w.where >= sp + 16 && w.where < sp + 64 && (w.where - sp) % 4 == 0 ? nm_keep_alignment(f, (int)((w.where - sp) / 4), w.value) : w.value;
            restore.push_back({w.where, memory_word(w.where)}); memory_word(w.where) = v; break;
        }
        case REGISTER: r[w.where] = (gpr)(int64_t)(s32)(w.where >= 4 && w.where <= 7 ? nm_keep_alignment(f, (int)w.where - 4, w.value) : w.value); break;
        case FLOAT_REGISTER: (&start->f0 + w.where)->u32l = w.value; break;
        default: break;    // (a called function's result: fuzz_stand_in puts it in)
        }
    }

    void undo_memory() {
        for (size_t i = restore.size(); i-- > 0;) memory_word((u32)restore[i].first) = restore[i].second;
        restore.clear();
    }

    // A value that might pass a check `old` failed. `pool`: values the test read, to copy one. Half the time the whole word
    // changes; otherwise one halfword or one byte of it, since the function may read only that much of it (a flag byte, a count in
    // a halfword), and the rest is left as it was.
    u32 new_value(u32 old, const std::vector<u32>& pool) {
        const u32 lane = random32() % 4;
        const int bits = lane < 2 ? 32 : lane == 2 ? 16 : 8;
        const int shift = bits == 32 ? 0 : bits * (random32() % (32 / bits));
        const u32 mask = bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1) << shift;
        const u32 was = (old & mask) >> shift;
        u32 v;
        switch (random32() % (bits == 32 ? 14 : 11)) {
        case 0: v = random_word(); break;
        case 1: case 2: v = dictionary_count ? dictionary_word() : random_word(); break;
        case 3: v = 0; break;
        case 4: v = random32() % 16; break;                        // (a small count, index or kind)
        case 5: v = was + 1 + random32() % 4; break;
        case 6: v = was - 1 - random32() % 4; break;
        case 7: v = was ^ (1u << (random32() % bits)); break;
        case 8: v = pool.empty() ? random_word() : pool[random32() % pool.size()]; break;
        case 9: v = (u32)-1; break;
        case 10: v = random32() % 2 ? 1u << (bits - 1) : (1u << (bits - 1)) - 1; break;    // (the sign bit, or the largest)
        case 11: v = random_pointer(4); break;
        case 12: {
            // A fixed address the function names, a little way in: an entry of a table it indexes, or one of a list of
            // structures it checks a pointer against (func_801450E4: (pointer - 0x8014BA98) / 12 must be a case of its switch).
            int n = 0;
            while (f.addresses[n] != 0) n++;
            if (n == 0) { v = random_pointer(4); break; }
            const u32 align = 1u << (random32() % 3);
            v = f.addresses[random32() % n] + (random32() % 0x400) / align * align;
            break;
        }
        default: v = random_number_bits(); break;
        }
        return (old & ~mask) | ((v << shift) & mask);
    }

    // Adds 1 to 8 tweaks to a start already made (applying them as it goes).
    void mutate(Kept& child, const Kept& parent, recomp_context* start) {
        std::vector<u32> params;      // The parameter registers it has, by register number (floats: 32 + f12 or f14).
        for (int i = 0; f.params[i] != 0 && i < 4; i++) {
            if (f.params[i] == '.' || f.params[i] == 'd') continue;     // (a double isn't steered: "refused2", 2026-11-12)
            if (f.params[i] == 'f') { if (i < 2) params.push_back(32 + 12 + 2 * i); }
            else params.push_back(4 + i);
        }
        std::vector<u32> pool;
        for (u32 o : parent.reads) pool.push_back(memory_word(o));
        for (u32 p : params) pool.push_back(p >= 32 ? (&start->f0 + (p - 32))->u32l : (u32)(&start->r0)[p]);
        for (const std::pair<u32, u32>& c : parent.compares) {
            pool.push_back(c.first);
            pool.push_back(c.second);
        }
        const int count = 1 << (random32() % 4);
        for (int n = 0; n < count && child.tweaks.size() < MOST_TWEAKS; n++) {
            if (!parent.compares.empty() && random32() % 4 == 0 && input_to_state(child, parent, start, params)) continue;
            // What to change: a word it read (most often), a parameter, or a called function's result, of those it has.
            u32 weights[3] = {parent.reads.empty() ? 0u : 6u, params.empty() ? 0u : 2u, parent.calls == 0 ? 0u : 2u};
            u32 total = weights[0] + weights[1] + weights[2];
            if (total == 0) return;
            u32 pick = random32() % total;
            u32 what = pick < weights[0] ? 0 : pick < weights[0] + weights[1] ? 6 : 8;
            Tweak w;
            if (what < 6) {
                u32 o = parent.reads[random32() % parent.reads.size()];
                u32 old = memory_word(o);
                if (random32() % 16 == 0) {
                    // Every word read, and every word of the random memory, that holds `old`, gets the same new value.
                    u32 v = new_value(old, pool);
                    if (v == old) v = old + 1;
                    auto change = [&](u32 offset) {
                        if (memory_word(offset) != old || child.tweaks.size() >= MOST_TWEAKS) return;
                        w = {MEMORY_WORD, offset, v};
                        apply(w, start);
                        child.tweaks.push_back(w);
                    };
                    for (u32 r : parent.reads) change(r);
                    for (u32 a = ARENA; a < ARENA + ARENA_BYTES; a += 4) change(a - 0x80000000u);
                    continue;
                }
                w = {MEMORY_WORD, o, new_value(old, pool)};
                if (random32() % 16 == 0) {
                    // The same word in every entry of a table: the stride is a guess, the distance to one of the next words
                    // it read, and the new word goes there, at twice it, and so on (up to 128 entries). A loop that goes on
                    // only while each entry has a flag set (func_80138E10: 80 entries 0x13C bytes apart) gets them all at once.
                    const auto at = std::lower_bound(parent.reads.begin(), parent.reads.end(), o);
                    const size_t i = at - parent.reads.begin();
                    if (i + 1 < parent.reads.size()) {
                        const u32 stride = parent.reads[i + 1 + random32() % std::min<size_t>(8, parent.reads.size() - i - 1)] - o;
                        for (u32 k = 1; k <= 128 && stride != 0 && child.tweaks.size() < MOST_TWEAKS; k++) {
                            const Tweak more = {MEMORY_WORD, o + k * stride, w.value};
                            apply(more, start);
                            child.tweaks.push_back(more);
                        }
                    }
                }
            } else if (what < 8) {
                u32 p = params[random32() % params.size()];
                if (p >= 32) w = {FLOAT_REGISTER, p - 32, random32() % 2 ? random_float_bits() : new_value((&start->f0 + (p - 32))->u32l, pool)};
                else w = {REGISTER, p, new_value((u32)(&start->r0)[p], pool)};
            } else {
                if (random32() % 8 == 0) {
                    // Every call gets the same result (most often 0 or 1): a loop that goes on only while each call it makes
                    // says no (func_8003BBFC: eight calls in a row returning 0) is rarely passed one call at a time.
                    const u32 v = random32() % 4 == 0 ? new_value(random_word(), pool) : random32() % 2;
                    for (u32 k = 0; k < parent.calls && child.tweaks.size() < MOST_TWEAKS; k++) child.tweaks.push_back({CALL_V0, k, v});
                    continue;
                }
                u32 kind = random32() % 10;
                w = {(u8)(kind < 5 ? CALL_V0 : kind < 6 ? CALL_V1 : kind < 7 ? CALL_F0 : CALL_OUT), random32() % parent.calls, 0};
                if (w.kind == CALL_OUT) w.where = (w.where << 8) | ((random32() % 4) << 5) | (random32() % 32);
                w.value = w.kind == CALL_F0 && random32() % 2 ? random_number_bits() : new_value(random_word(), pool);
            }
            apply(w, start);
            child.tweaks.push_back(w);
        }
    }

    // Runs steered tests until every instruction has been reached and as many tests again have run since (at least 20,000), or
    // `most` tests, or the time (FUZZ_STEER_MS, 40 seconds by default). Returns how many ran.
    long run(long most) {
        const char* ms = std::getenv("FUZZ_STEER_MS");
        const ULONGLONG limit = ms ? (ULONGLONG)std::atoll(ms) : 40000, began = fuzz_clock_ms();
        int before = 0;
        for (int i = 0; i < f.instructions; i++) before += f.reached[i] ? 1 : 0;
        long n = 0, whole_at = -1;
        while (n < most) {
            // (The time is checked before every test, not every 256th: a steered test can take a slow function's loops up to
            //  the second chance's limit, and 256 of those overran lift.py's 180 seconds for func_8000CAF8.)
            if (fuzz_clock_ms() - began > limit) break;
            if ((n & 1023) == 0 || touched_pages.size() > 256) give_back_touched_pages();
            if (whole_at < 0 && all_reached()) whole_at = n;
            if (whole_at >= 0 && n >= std::max(2 * whole_at, 20000L)) break;
            steering_now = true;
            if ((n & 1023) == 0) find_rarest();
            n++;
            tests_now = n;
            Kept child;
            recomp_context start;
            const Kept* parent = nullptr;
            size_t which = 0;
            if (!kept.empty() && random32() % 16 != 0) {
                // (See choose.)
                which = choose();
                parent = &kept[which];
            }
            if (parent == nullptr) {
                // Now and then a new random start, so it doesn't only work from what it already has. (Run as it is: then its reads
                // are known, and a later test can tweak it.)
                child.base = (((uint64_t)random32() << 32) | random32()) | 1;
                const uint64_t resume = random_state;
                random_state = child.base;
                make_start(f, &start);
                random_state = resume;
            } else {
                child.base = parent->base;
                child.tweaks = parent->tweaks;
                const uint64_t resume = random_state;
                make(*parent, &start);
                random_state = resume;
                if (parent->known) mutate(child, *parent, &start);
            }
            const uint64_t resume = random_state;
            // The stand-ins put in any results it set; the original's reads are noted.
            result_tweaks = &child.tweaks;
            lift_fuzz_reads.clear();
            lift_fuzz_compares.clear();
            lift_fuzz_noting_reads = true;
            std::vector<u8> reached_now(f.instructions + 1, 0);
            Outcome e = run_both(f, start, reached_now, true);
            lift_fuzz_noting_reads = false;
            result_tweaks = nullptr;
            undo_memory();
            random_state = resume;
            if (e != COMPARED) continue;
            if (parent != nullptr && !parent->known) {
                // A start from part 1, run once more as it was: now its reads are known.
                Kept& k = kept[which];
                std::sort(lift_fuzz_reads.begin(), lift_fuzz_reads.end());
                lift_fuzz_reads.erase(std::unique(lift_fuzz_reads.begin(), lift_fuzz_reads.end()), lift_fuzz_reads.end());
                for (u32 o : lift_fuzz_reads) {
                    if (o < FRAME_LOW || o >= FRAME_HIGH) k.reads.push_back(o);
                }
                if (k.reads.size() > 2048) k.reads.resize(2048);
                k.calls = calls_by_original;
                k.known = true;
                k.compares = unique_compares(lift_fuzz_compares);
                continue;
            }
            consider(child.base, std::move(child.tweaks), reached_now, &lift_fuzz_reads, calls_by_original, &lift_fuzz_compares);
        }
        steering_now = false;
        int after = 0;
        for (int i = 0; i < f.instructions; i++) after += f.reached[i] ? 1 : 0;
        std::fprintf(stderr, "steered %s: %ld tests (last new instruction at %ld), %zu starts kept, reached %d -> %d of %d\n", f.name, n,
                     last_gain, kept.size(), before, after, f.instructions);
        return n;
    }

    // ---- Part 4 (2026-11-08, scratch/lift_steer) ----
    //
    // For a function parts 1 to 3 still didn't wholly reach: more steered tests, from the same kept starts, with what part 2
    // couldn't do. What was left unreached (scratch/lift_cover/reasons/reasons.txt) was mostly of four kinds:
    //   - a called function had to change a word of memory the caller reads again after the call: a global it set to -1 before
    //     the call (func_80136CC8), a flag in a structure (func_800F87A8), a local whose address went to the callee on the stack
    //     (func_8003A050). Part 2's stand-ins write only through pointers in a0-a3. Part 4's can write any word the original's
    //     path read after the call (CALL_WRITE), and fill pointers passed on the stack too (steer_stand_in);
    //   - several calls had to return 0 together (func_8003A050): part 1's stand-in draws are tied together (call_seed), so part
    //     4's own new starts draw each call's results independently (mixed_calls), and what part 4 changes includes each call's
    //     results by what was compared with them;
    //   - a comparison needed an exact value -- a halfword of 0x0E5A, a count of 0xE5 -- reached only by changing just the word
    //     the comparison read, the right lane of it, by the right amount. Part 2 picks among every word read; part 4 picks the
    //     few values read just before the comparison (its likely sources), and sets the one that matches a side (whole, a
    //     halfword or a byte of it, with an offset of 0, 1 or one of the function's own constants, for `addiu; beq`) so the
    //     comparison goes the other way. Comparisons that came out equal are noted too, so one can be made unequal;
    //   - a loop with a fixed count too long for the access limit (func_800119A4): some of part 4's tests where both versions ran
    //     past it run again with more room (run_both, long_retry);
    //   and for abi.py's functions (non-standard registers): the registers they read besides their parameters (gp, s4, ...) are
    //   changed too (FuzzFunction's input_registers), and a callee's result in a register other than v0 (CALL_REG).
    // As part 2: this only adds tests, compared exactly as every other test is. An instruction counts only when a compared test ran
    // it, a difference in any of them is noted, and parts 1 to 3 are unchanged (part 4 runs after them, and only when they left
    // something unreached), so whatever they proved they still prove.

    // Which way a branch should go to reach an instruction no compared test has reached: 1 its test holding (to `taken`), 0 not
    // (to `fall`), -1 neither (both reached, or not known).
    int open4(const Kept::Compare& c) const { return open4(c.op, c.taken, c.fall); }
    int open4(int op, int taken, int fall) const {
        const struct { int op, taken, fall; } c{op, taken, fall};
        if (c.op < 0 || c.taken < 0 || c.fall < 0 || c.taken >= f.instructions || c.fall >= f.instructions) return -1;
        if (!f.reached[c.taken] && f.reached[c.fall]) return 1;
        if (!f.reached[c.fall] && f.reached[c.taken]) return 0;
        return -1;
    }

    // How a float comparison came out: 1 its test held, 2 it didn't.
    template <class C> static u8 float_outcome4(const C& c) {
        float x, y;
        std::memcpy(&x, &c.a, 4);
        std::memcpy(&y, &c.b, 4);
        const int op = c.op - 16;
        return (op == 0 ? x == y : op == 2 ? x < y : x <= y) ? 1 : 2;
    }

    // A float comparison's sides for the outcome wanted (see mutate4): `from`, one side's bits, and `to`, what it becomes. A
    // number between them, a little past the other side, or the other side itself, a little being a share of its size.
    void float_aim4(const Kept::Compare& c, bool hold, u32& from, u32& to) {
        float x, y;
        std::memcpy(&x, &c.a, 4);
        std::memcpy(&y, &c.b, 4);
        static const float shares[5] = {1e-6f, 1e-3f, 0.01f, 0.1f, 0.5f};
        const bool first = random32() % 2;
        const float other = first ? y : x;
        const float little = std::max(std::fabs(other), 1.0f) * shares[random32() % 5];
        const int op = c.op - 16;
        // (Wanted, for a as `from`: a == b, a < b, a <= b, or their opposites; with b as `from` turned round.)
        float v = other;
        if (op == 0) v = hold ? other : other + little;
        else if (op == 2) v = hold ? (first ? other - little : other + little) : other;
        else v = hold ? other : (first ? other + little : other - little);
        from = first ? c.a : c.b;
        std::memcpy(&to, &v, 4);
    }

    // A float comparison that has never held, or never failed, turned: one side, as a single float or a double, found among the
    // words the parent's original read (or its latest sources) and given a value a little past the other side, or equal to it
    // (as float_aim4). A double is two words, the more significant first, as the N64 keeps it. Applied and kept in the child.
    void float_tweak4(Kept& child, const Kept& parent, const Kept::Compare& c, recomp_context* start) {
        const bool hold = (stuck4(c.site) & 1) != 0, first = random32() % 2;
        const double side = first ? c.x : c.y, other = first ? c.y : c.x;
        static const double shares[5] = {1e-6, 1e-3, 0.01, 0.1, 0.5};
        // (Half the time a few steps of the number's own precision instead -- as a float, the coarser -- for two sides that are
        //  close together, or near 0, where a share of 1 would pass the other bound too: func_80101A94 needs f22 < x < f20.)
        static const double steps[4] = {1, 16, 4096, 1048576};
        const float other_float = (float)other;
        const double step = (double)std::nextafter(other_float, std::numeric_limits<float>::infinity()) - (double)other_float;
        const double little = random32() % 2 ? std::max(std::fabs(other), 1.0) * shares[random32() % 5]
                                             : std::max(step, 1e-300) * steps[random32() % 4];
        const int op = c.op - 16;
        double v = other;
        if (op == 0) v = hold ? other : other + little;
        else if (op == 2) v = hold ? (first ? other - little : other + little) : other;
        else v = hold ? other : (first ? other + little : other - little);
        if (!(v == v) || std::isinf(v)) return;
        const float sf = (float)side;
        u32 single;
        std::memcpy(&single, &sf, 4);
        const bool exact = (double)sf == side;
        uint64_t whole;
        std::memcpy(&whole, &side, 8);
        const float vf = (float)v;
        u32 new_single;
        std::memcpy(&new_single, &vf, 4);
        uint64_t new_whole;
        std::memcpy(&new_whole, &v, 8);
        std::vector<std::pair<int, u32>> spots;     // (0: a latest source, by number; 1: a float word; 2: a double's first word)
        for (u32 i = 0; i < c.count; i++) {
            if (c.sources[i].kind == READ_EVENT && exact && c.sources[i].value == single) spots.push_back({0, i});
        }
        for (u32 o : parent.reads) {
            if (exact && memory_word(o) == single) spots.push_back({1, o});
            if (memory_word(o) == (u32)(whole >> 32) && memory_word(o + 4) == (u32)whole) spots.push_back({2, o});
        }
        if (spots.empty()) return;
        const std::pair<int, u32> at = spots[random32() % spots.size()];
        std::vector<Tweak> made;
        if (at.first == 0) {
            Tweak w{MEMORY_WORD, 0, 0};
            if (source_tweak(c.sources[at.second], 0, new_single, w)) made.push_back(w);
        } else if (at.first == 1) {
            made.push_back({MEMORY_WORD, at.second, new_single});
        } else {
            made.push_back({MEMORY_WORD, at.second, (u32)(new_whole >> 32)});
            made.push_back({MEMORY_WORD, at.second + 4, (u32)new_whole});
        }
        for (const Tweak& w : made) {
            if (child.tweaks.size() >= MOST_TWEAKS) break;
            apply(w, start);
            child.tweaks.push_back(w);
        }
    }

    // How a comparison came out, a bit each way: 1 equal, 2 not; 4 less (signed), 8 not; 16 less (unsigned), 32 not.
    static u8 outcome4(u32 a, u32 b) {
        return (a == b ? 1 : 2) | ((s32)a < (s32)b ? 4 : 8) | (a < b ? 16 : 32);
    }
    // The ways a branch has never gone in part 4's tests, as outcome4's bits, by what kind of comparison it makes (its site's low
    // 2 bits: FUZZ_COMPARE_KIND in fuzz.h); 0 if it has gone both ways.
    u8 stuck4(u32 site) const {
        const auto it = site_seen.find(site);
        const u8 seen = it == site_seen.end() ? 0 : it->second;
        const u8 kinds[4] = {1 | 2, 4 | 8, 16 | 32, 1 | 2};
        return (u8)(kinds[site & 3] & ~seen);
    }

    // What part 4 noted of the original's run just made (events4, compares4, results4), kept with a start.
    void learn4(Kept& k) {
        k.known4 = true;
        for (const Compare4& c : compares4) site_in_kept[c.site] = true;
        k.compares4.clear();
        k.after_calls.clear();
        k.results.clear();
        auto in_stack = [](u32 o) { return o >= FRAME_LOW && o < (u32)((STACK + 0x60u) - 0x80000000u); };
        // Each comparison, with the 4 values before it (reads outside its own frame, and calls' results), the latest first. For
        // each branch in the code, its first 2 and its last time (a loop's branch can run thousands of times: they would crowd
        // out the rest, and a branch run only once in a pass that got far is the one that matters), at most 256 in all.
        std::unordered_map<u32, u32> times;
        for (const Compare4& c : compares4) times[c.site]++;
        std::unordered_map<u32, u32> taken;
        for (size_t i = 0; i < compares4.size() && k.compares4.size() < 256; i++) {
            const Compare4& c = compares4[i];
            const u32 n = taken[c.site]++;
            if (n >= 2 && n + 1 != times[c.site]) continue;
            Kept::Compare kc{c.a, c.b, c.site, c.op, c.taken, c.fall, 0, {}};
            kc.x = c.x;
            kc.y = c.y;
            kc.event = c.event;
            for (size_t j = std::min<size_t>(c.event, events4.size()); j-- > 0 && kc.count < 4 && c.event - j <= 16;) {
                const Event4& e = events4[j];
                if (e.kind == READ_EVENT && e.where >= FRAME_LOW && e.where < FRAME_HIGH && e.filled == 0) continue;   // (a local or a saved register -- unless a stand-in filled it: frame_fill_of)
                kc.sources[kc.count++] = e;
            }
            // (2026-11-11) And the latest call's result within the same 16 events, when the 4 latest aren't one: a result is
            // often tested a few loads after it came back -- func_8013C164 loads 4 fields to decide whether to compare a
            // call's result less 0x2AB with 0x2AB, so the result was its 5th source back, and no test could turn it.
            bool has_result = false;
            for (u32 i = 0; i < kc.count; i++) has_result = has_result || kc.sources[i].kind == RESULT_EVENT;
            for (size_t j = std::min<size_t>(c.event, events4.size()); use_shift && !has_result && kc.count == 4 && j-- > 0 && c.event - j <= 16;) {
                if (events4[j].kind == RESULT_EVENT) { kc.sources[kc.count++] = events4[j]; break; }
            }
            k.compares4.push_back(kc);
        }
        // The words read after a call, outside the stack, once each (by call and word), up to 256.
        for (const Event4& e : events4) {
            if (k.after_calls.size() >= 256) break;
            if (e.kind != READ_EVENT || e.calls == 0 || in_stack(e.where)) continue;
            bool seen = false;
            for (const Event4& x : k.after_calls) seen = seen || (x.where == e.where && x.calls == e.calls);
            if (!seen) k.after_calls.push_back(e);
        }
        // The calls' results a caller can read: v0, v1, f0, and for abi.py's functions any scratch register the callee may change.
        const uint64_t interesting = abi_mode ? STAND_IN_SCRATCH : (1ull << 2 | 1ull << 3 | 1ull << 32);
        for (u32 call = 0; call < results4.size() && call < 64; call++) {
            const uint64_t bits = results4[call].may_write & interesting;
            for (u32 b = 0; b < 64; b++) {
                if (bits >> b & 1) k.results.push_back({call, b, results4[call].regs[b]});
            }
        }
    }

    // The registers a test's start can be changed in: the parameters (a0-a3, as part 2's), and the other registers the function
    // reads before it sets them (abi.py's functions: gp, s4, ...; FuzzFunction's input_registers).
    std::vector<u32> registers4() const {
        std::vector<u32> out;
        for (int i = 0; f.params[i] != 0 && i < 4; i++) {
            if (f.params[i] != '.' && f.params[i] != 'f' && f.params[i] != 'd') out.push_back(4 + i);
        }
        for (u32 n = 1; n < 32; n++) {
            if ((f.input_registers >> n & 1) && n != 29 && n != 31 && std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        }
        return out;
    }

    // The values v could become so that it, or one lane of it (a halfword or a byte, as a load of that size would read it), plus
    // d makes `from` -- becoming `to` (so the comparison of `from` with something comes out as if it were `to`). Appended to out.
    static void matches(u32 v, u32 from, u32 to, u32 d, std::vector<u32>& out) {
        const u32 x = from - d, y = to - d;
        if (v == x) out.push_back(y);
        const bool half = x < 0x10000 || x >= 0xFFFF8000u, byte = x < 0x100 || x >= 0xFFFFFF80u;
        if (half && (v & 0xFFFF) == (x & 0xFFFF)) out.push_back((v & 0xFFFF0000u) | (y & 0xFFFF));
        if (half && (v >> 16) == (x & 0xFFFF)) out.push_back((v & 0xFFFF) | (y << 16));
        if (byte) {
            for (int b = 0; b < 32; b += 8) {
                if (((v >> b) & 0xFF) == (x & 0xFF)) out.push_back((v & ~(0xFFu << b)) | ((y & 0xFF) << b));
            }
        }
    }

    // (2026-11-11) The values v could become if what was compared is v, or one lane of it (as a load of that size would read
    // it), plus some number the code added: that lane moved by the same amount the comparison's side has to move, to - from.
    // `matches` finds that number only when it is small or one of the function's own constants and drawn at random (1 in
    // 4 times 1 in the constants' count, times the sign: func_8013A9D4 compares a call's result less 0x100 with 25, and
    // never once got 0x105-0x108, 0x117 or 0x118 from it). This takes every source's own: wrong for a source that isn't one,
    // which then changes and only makes a different test. A lane narrower than the word only when both sides fit in it.
    static void shifts(u32 v, u32 from, u32 to, std::vector<u32>& out) {
        const u32 by = to - from;
        out.push_back(v + by);
        const bool half = (from < 0x10000 || from >= 0xFFFF8000u) && (to < 0x10000 || to >= 0xFFFF8000u);
        const bool byte = (from < 0x100 || from >= 0xFFFFFF80u) && (to < 0x100 || to >= 0xFFFFFF80u);
        if (half) {
            out.push_back((v & 0xFFFF0000u) | ((v + by) & 0xFFFF));
            out.push_back((v & 0xFFFF) | ((((v >> 16) + by) & 0xFFFF) << 16));
        }
        if (byte) {
            for (int b = 0; b < 32; b += 8) out.push_back((v & ~(0xFFu << b)) | ((((v >> b) + by) & 0xFF) << b));
        }
    }

    // The tweak that makes a source of a comparison hold `value`: a word read before any call is changed where the test starts;
    // a word read after a call, there or by that call's stand-in (CALL_WRITE: needed when the test itself wrote it before);
    // a call's result by its register.
    bool source_tweak(const Event4& s, u32 bit, u32 value, Tweak& w) {
        if (s.kind == RESULT_EVENT) {
            if (bit == 2) w = {CALL_V0, s.where, value};
            else if (bit == 3) w = {CALL_V1, s.where, value};
            else if (bit == 32) w = {CALL_F0, s.where, value};
            else if (abi_mode) w = {CALL_REG, s.where, value, bit};
            else return false;
            return true;
        }
        if (s.filled != 0) {
            // (A word of the caller's frame a stand-in filled: that stand-in fills it with this instead. 2026-11-11)
            w = {CALL_OUT, s.filled - 1, value};
            return true;
        }
        const bool unchanged = memory_word(s.where) == s.value, stack = s.where >= FRAME_LOW && s.where < (u32)((STACK + 0x60u) - 0x80000000u);
        if (s.calls == 0 || stack || (unchanged && random32() % 2)) w = {MEMORY_WORD, s.where, value};
        else w = {CALL_WRITE, s.calls - 1, value, s.where};
        return true;
    }

    // Adds 1 or 2 of part 4's tweaks to a start already made (applying them as it goes).
    void mutate4(Kept& child, const Kept& parent, recomp_context* start) {
        const std::vector<u32> registers = registers4();
        std::vector<u32> pool;
        for (const Kept::Compare& c : parent.compares4) {
            pool.push_back(c.a);
            pool.push_back(c.b);
        }
        const int count = 1 + random32() % 2;
        for (int n = 0; n < count && child.tweaks.size() < MOST_TWEAKS; n++) {
            // (Part 5, a quarter of its changes: an alias. See Part 5.)
            if (five && use_alias && random32() % 4 == 0 && alias_tweak5(child, parent, start)) continue;
            Tweak w{MEMORY_WORD, 0, 0};
            bool made = false;
            const u32 pick = random32() % 20;
            if (pick < 12 && !parent.compares4.empty()) {
                // A comparison turned the other way, through one of its sources. Three times in four, one that leads somewhere
                // no compared test has been: a branch one of whose ways goes to an instruction never reached (open4), aimed that
                // way, or failing those, a set-less-than that has never come out one way (stuck4). Otherwise any, either way.
                std::vector<const Kept::Compare*> open, stuck;
                for (const Kept::Compare& x : parent.compares4) {
                    if (open4(x) >= 0) open.push_back(&x);
                    else if ((x.op < 0 || x.op >= 16) && stuck4(x.site) != 0) stuck.push_back(&x);
                }
                // (With neither, half the time: the branch of 4 picked at random whose less-gone way the fewest compared tests
                //  have gone (Steering's hits) -- a loop that has to pass its checks a second time, in another entry, has every
                //  way reached already, but the way on through all of them only rarely.)
                // (2026-11-11: also beside set-less-thans still stuck, half the time. A set-less-than that can never come out
                //  the other way -- dead, or out of the tests' reach -- stays stuck for good, and the branches that lead on
                //  were then never aimed at: func_8013A9D4's jump table, whose entries 0 and 1 a random 0x100 or 0x101 had
                //  reached, so its bounds check had come out both ways and no branch was open.)
                std::vector<const Kept::Compare*> rare;
                const bool rare_too = use_rare_too && !stuck.empty() && random32() % 2;
                if (open.empty() && (stuck.empty() || rare_too) && random32() % 2) {
                    const Kept::Compare* best = nullptr;
                    u32 fewest = 0xFFFFFFFFu;
                    for (int k = 0; k < 4; k++) {
                        const Kept::Compare& x = parent.compares4[random32() % parent.compares4.size()];
                        if (x.op < 0 || x.op >= 16 || x.taken < 0 || x.fall < 0 || x.taken >= f.instructions || x.fall >= f.instructions) continue;
                        const u32 h = std::min(hits[x.taken], hits[x.fall]);
                        if (h < fewest) { fewest = h; best = &x; }
                    }
                    if (best != nullptr) rare.push_back(best);
                }
                const std::vector<const Kept::Compare*>& aimed = !open.empty() ? open : !rare.empty() ? rare : !stuck.empty() ? stuck : rare;
                const bool aim = !aimed.empty() && (aimed.data() == rare.data() || random32() % 4 != 0);
                const Kept::Compare* chosen = aim ? aimed[random32() % aimed.size()] : &parent.compares4[random32() % parent.compares4.size()];
                // (2026-11-11) A branch on a set-less-than's result -- `slt $v0, $a, $b; beq $v0, $zero` -- compares 0 or 1 with
                // 0, and no source holds that: what decides it is the set-less-than's own sides. So the way wanted of the branch
                // is turned into the outcome wanted of the set-less-than just before it (noted at the same place in the run, its
                // result the branch's side), and that is aimed instead. (Before, the branch was aimed with its own sides, which
                // only ever changed some source that happened to hold 0 or 1.)
                int slt_want = -1;      // (2 less, 3 not less: as `want` below, for the set-less-than)
                if (use_slt_aim && aim && chosen->op >= 0 && chosen->op < 16 && (chosen->op & 7) <= 1 && chosen->b == 0 && chosen->a <= 1 &&
                    chosen > parent.compares4.data()) {
                    const Kept::Compare& before = *(chosen - 1);
                    const u32 kind = before.site & 3;
                    const u32 result = kind == 1 ? ((s32)before.a < (s32)before.b ? 1u : 0u) : (before.a < before.b ? 1u : 0u);
                    if (before.op < 0 && (kind == 1 || kind == 2) && before.event == chosen->event && result == chosen->a) {
                        const int way = open4(*chosen) >= 0 ? open4(*chosen) : (hits[chosen->taken] < hits[chosen->fall] ? 1 : 0);
                        const bool less = ((chosen->op & 7) == 1) == (way == 1);     // (!= taken, or == not taken: the result is 1)
                        slt_want = less ? 2 : 3;
                        chosen = &before;
                    }
                }
                const Kept::Compare& c = *chosen;
                if (aim && c.op >= 16 && random32() % 2) {
                    // (A float comparison, half the time: its side found as a float or a double anywhere the test read, and
                    //  set there -- a double from a table read long before the comparison isn't among its latest sources.)
                    float_tweak4(child, parent, c, start);
                    continue;
                }
                u32 from = c.a, to = c.b;
                if (random32() % 2) std::swap(from, to);
                if (from != to && random32() % 3 == 0) to += random32() % 2 ? 1 : -1;     // (for a less-than: one past it)
                if (aim) {
                    // (Which side becomes what, for the outcome wanted: equal -- one side made the other; unequal -- left to the
                    //  fallback below, any change of a source; less -- one side made one less than the other, or the other one
                    //  more; not less -- one side made the other, or one more; and so on.)
                    int want = 0;      // (as FUZZ_BRANCH_AT's op, without the sign: the relation wanted between a and b)
                    if (c.op >= 16) {
                        // (A float comparison: never held, or never failed. A side made the value that turns it.)
                        float_aim4(c, (stuck4(c.site) & 1) != 0, from, to);
                        want = -1;
                    } else if (slt_want >= 0) {
                        want = slt_want;        // (the set-less-than a branch tests: see slt_want above)
                    } else if (c.op >= 0) {
                        static const int opposite[6] = {1, 0, 3, 2, 5, 4};
                        const int way = open4(c) >= 0 ? open4(c) : (hits[c.taken] < hits[c.fall] ? 1 : 0);   // (open, or the rarer)
                        want = way == 1 ? (c.op & 7) : opposite[c.op & 7];
                    } else {
                        const u8 missing = stuck4(c.site);
                        want = (missing & 1) ? 0 : (missing & 2) ? 1 : (missing & (4 | 16)) ? 2 : 3;
                    }
                    const bool first = random32() % 2;
                    if (want >= 0) from = first ? c.a : c.b;
                    const u32 other = first ? c.b : c.a;
                    // (a OP b with a as `from`; with b as `from` the relation turns round: a < b is b > a.)
                    static const int turned[6] = {0, 1, 5, 4, 3, 2};
                    switch (want < 0 ? -1 : first ? want : turned[want]) {
                    case -1: break;                                             // (a float comparison: done above)
                    case 0: to = other; break;                                  // equal
                    case 1: to = from; break;                                   // unequal: the fallback
                    case 2:                                                     // less
                        // (2026-11-11) Half the time, below a small bound, any value under it: `addiu $a2, $s1, -0x100;
                        //  sltiu $v0, $a2, 0x19` guards a jump table, and which entry comes next is the value itself
                        //  (func_8013A9D4: entries 5 to 8, 23 and 24). Just under the bound is only its last entries.)
                        if (use_below && other >= 2 && other <= 0x10000 && random32() % 2) to = random32() % other;
                        else to = other - 1 - random32() % 2;
                        break;
                    case 3: to = other + random32() % 2; break;                 // not less
                    case 4: to = other - random32() % 2; break;                 // not more
                    default: to = other + 1 + random32() % 2; break;            // more
                    }
                }
                // (What the code may have added before comparing: nothing, a little, or one of its own constants, either way.)
                u32 d = 0;
                const u32 how = random32() % 4;
                if (how == 1) d = (u32)((s32)(random32() % 17) - 8);
                else if (how == 2 && dictionary_count) d = dictionary[random32() % dictionary_count] * (random32() % 2 ? 1u : (u32)-1);
                // (2026-11-11: how == 3, shift, below -- d as each source's own lane makes it: see shifts.)
                const bool shift = use_shift && how == 3;
                struct Option { int source; u32 bit; u32 value; };
                std::vector<Option> options;
                std::vector<u32> values;
                for (u32 i = 0; i < c.count; i++) {
                    const Event4& s = c.sources[i];
                    if (s.kind == READ_EVENT) {
                        values.clear();
                        if (from != to) { if (shift) shifts(s.value, from, to, values); else matches(s.value, from, to, d, values); }
                        for (u32 v : values) options.push_back({(int)i, 0, v});
                    } else {
                        for (const Kept::Result& r : parent.results) {
                            if (r.call != s.where) continue;
                            values.clear();
                            if (from != to) { if (shift) shifts(r.value, from, to, values); else matches(r.value, from, to, d, values); }
                            for (u32 v : values) options.push_back({(int)i, r.bit, v});
                        }
                    }
                }
                // (And a parameter or other input register holding a side.)
                for (u32 reg : registers) {
                    values.clear();
                    if (from != to) { if (shift) shifts((u32)(&start->r0)[reg], from, to, values); else matches((u32)(&start->r0)[reg], from, to, d, values); }
                    // (Part 5: with every one of the function's own constants as what the code added, either way, not one
                    //  drawn at random -- a parameter that is compared only after it was added to and kept in the frame
                    //  (modrep_func_80020B58: its first parameter plus 10, stored at 0x144 and loaded back, against 0x15 and
                    //  0x18) has no source part 4 sees, and the one constant that works was drawn 1 time in 400.)
                    if (five && use_constant_offsets && from != to && !shift) {
                        for (int k = 0; k < dictionary_count; k++) {
                            matches((u32)(&start->r0)[reg], from, to, dictionary[k], values);
                            matches((u32)(&start->r0)[reg], from, to, (u32)-(s32)dictionary[k], values);
                        }
                    }
                    for (u32 v : values) options.push_back({-1 - (int)reg, 0, v});
                }
                // (None of those, aiming: anywhere else the parent's original read a side -- a value loaded early and kept in a
                //  register while other loads came in between (func_8011C5AC's a1, from long before its `sltiu`), as it is
                //  where the test starts, or as it was read after a call, for that call to write.)
                std::vector<Tweak> wider;
                if (aim && options.empty() && from != to) {
                    for (u32 o : parent.reads) {
                        values.clear();
                        matches(memory_word(o), from, to, d, values);
                        for (u32 v : values) wider.push_back({MEMORY_WORD, o, v});
                    }
                    for (const Event4& s : parent.after_calls) {
                        values.clear();
                        matches(s.value, from, to, d, values);
                        for (u32 v : values) wider.push_back({CALL_WRITE, s.calls - 1, v, s.where});
                    }
                }
                if (!wider.empty()) {
                    w = wider[random32() % wider.size()];
                    made = true;
                } else if (!options.empty()) {
                    const Option& o = options[random32() % options.size()];
                    if (o.source < 0) {
                        w = {REGISTER, (u32)(-1 - o.source), o.value};
                        made = true;
                    } else {
                        made = source_tweak(c.sources[o.source], o.bit, o.value, w);
                    }
                } else if (c.count != 0) {
                    // Nothing holds a side as it is (equal sides, or a value worked out of several): one of the sources changed
                    // to something else, the latest most often.
                    const u32 i = random32() % 2 ? 0 : random32() % c.count;
                    const Event4& s = c.sources[i];
                    u32 bit = 2, old = s.value;
                    if (s.kind == RESULT_EVENT) {
                        std::vector<const Kept::Result*> of_call;
                        for (const Kept::Result& r : parent.results) {
                            if (r.call == s.where) of_call.push_back(&r);
                        }
                        if (!of_call.empty()) {
                            const Kept::Result* r = of_call[random32() % of_call.size()];
                            bit = r->bit;
                            old = r->value;
                        }
                    }
                    u32 v = new_value(old, pool);
                    if (v == old) v = old + 1;
                    made = source_tweak(s, bit, v, w);
                }
            } else if (pick < 15 && !parent.after_calls.empty()) {
                // A word read after a call, changed by that call.
                const Event4& s = parent.after_calls[random32() % parent.after_calls.size()];
                u32 v = new_value(s.value, pool);
                if (v == s.value) v = s.value + 1;
                w = {CALL_WRITE, s.calls - 1, v, s.where};
                made = true;
            } else if (pick < 17 && !parent.results.empty()) {
                // A call's result: 0, 1, or a new value.
                const Kept::Result& r = parent.results[random32() % parent.results.size()];
                const Event4 s{RESULT_EVENT, r.call, 0, 0};
                const u32 v = random32() % 3 == 0 ? random32() % 2 : new_value(r.value, pool);
                made = source_tweak(s, r.bit, v, w);
            } else if (!registers.empty()) {
                // A parameter or other input register.
                const u32 reg = registers[random32() % registers.size()];
                w = {REGISTER, reg, new_value((u32)(&start->r0)[reg], pool)};
                made = true;
            }
            if (!made) continue;
            apply(w, start);
            child.tweaks.push_back(w);
        }
    }

    // Whether a word is no number as a float, or as a double's top word: its exponent all ones (NaN or infinity).
    static bool not_a_number4(u32 v) { return ((v >> 23) & 0xFF) == 0xFF || ((v >> 20) & 0x7FF) == 0x7FF; }

    // Adds tweaks giving a number to everything the last original read, was handed back or was given as a float that isn't one
    // (see run4). Whether there was anything to mend.
    bool mend4(Kept& child, const recomp_context* start) {
        const size_t had = child.tweaks.size();
        const u32 stack_high = (u32)((STACK + 0x60u) - 0x80000000u);
        for (const Event4& e : events4) {
            if (child.tweaks.size() >= MOST_TWEAKS) break;
            if (e.kind != READ_EVENT || !not_a_number4(e.value)) continue;
            const bool stack = e.where >= FRAME_LOW && e.where < stack_high;
            if (e.calls == 0 || stack) child.tweaks.push_back({MEMORY_WORD, e.where, random_number_bits()});
            else child.tweaks.push_back({CALL_WRITE, e.calls - 1, random_number_bits(), e.where});
            if (e.where >= FRAME_LOW && e.where < FRAME_HIGH) child.tweaks.pop_back();     // (its own frame: nothing to mend there)
        }
        for (u32 call = 0; call < results4.size() && call < 64; call++) {
            for (u32 b = 32; b < 52 && child.tweaks.size() < MOST_TWEAKS; b++) {
                if (!(results4[call].may_write >> b & 1) || !not_a_number4(results4[call].regs[b])) continue;
                if (b == 32) child.tweaks.push_back({CALL_F0, call, random_number_bits()});
                else if (abi_mode) child.tweaks.push_back({CALL_REG, call, random_number_bits(), b});
            }
        }
        for (u32 n : {12u, 14u}) {
            if (not_a_number4((&start->f0 + n)->u32l)) child.tweaks.push_back({FLOAT_REGISTER, n, random_number_bits()});
        }
        return child.tweaks.size() > had;
    }

    // ---- Part 5 (2026-11-12, agent "steer") ----
    //
    // For a function parts 1 to 4 still didn't wholly reach: part 4's tests again, from the same kept starts, with kinds of
    // input part 4 can't make. What was left (scratch/agents/tail, scratch/agents/steer):
    //   - a path that runs only when two of the function's own pointers meet: a store through one lands on a word read
    //     through the other (an alias). func_801422D8 writes a byte per entry through a pointer from its record's field 0x14,
    //     and only when one of those bytes lands on the top byte of the float at 0x100 of the record itself does that float
    //     pass 2^31 and the loop's other half run. func_80118C2C divides by a table's halfword it reads again after storing
    //     through its own parameter at 0xE0: zero only when that store landed on it. Random pointers never meet. So a test
    //     can take one place the parent's original stored to (or read), one it read (or stored to), and change the pointer
    //     that place came from -- a word the original read, or an input register, holding a value just below it, as a base
    //     address does -- by the distance between them, so the first lands on the second (alias_tweak5);
    //   - a loop whose count is so large that both versions run past the access limit, so the test is never compared and
    //     nothing after the loop is ever reached by one: an image whose width is 1 << a byte it reads (func_8000CAF8,
    //     func_8000CEEC), a count from the mod's tables (modrep_func_80020B58). Part 4 keeps only compared tests, so it
    //     never learns where such a count came from -- and the tests it does keep there finished only with 16 or 64 times
    //     the room, so each of its tests from them takes a tenth of a second (func_8000CEEC: 167 part-4 tests in its 40
    //     seconds). So after a test whose original ran long (past the access limit, finished or not), the comparison it
    //     made most often with one side the same every time -- a loop's bound -- is found, and whatever the original read
    //     (or was handed back by a call) before the loop that holds that bound, or the number of bits it is shifted by
    //     (1 << n, or half or twice it), is made small -- or one of the 8 latest moved by what makes it small, for a bound
    //     worked out from them; the next test is that test with it so (shrink5), up to 6 in a row. Such a test is kept as
    //     a start too when its original ran an instruction no compared test has (only as a start: what it ran counts for
    //     nothing). A loop with no memory access in it never meets the access limit, so an original that makes 64 times as
    //     many comparisons as that limit is stopped and its test set aside (count_compare); a NaN is mended up to 6 times;
    //   - a parameter compared only after the code added one of its constants to it and kept it in its frame: every one of
    //     the function's constants is tried as that addition, not one drawn at random (mutate4, use_constant_offsets).
    // As parts 2 and 4: this only adds tests, compared exactly as every other test is; an instruction counts only when a
    // compared test ran it, and a difference in any of them is noted. It runs after part 4, so parts 1 to 4 run exactly as
    // before and whatever they proved they still prove. (FUZZ_NO_PART5: without it. FUZZ_PART5_MS: its time, 40 seconds by
    // default. FUZZ_NO_ALIAS, FUZZ_NO_SHRINK, FUZZ_NO_CONSTANT_OFFSETS: without one of its additions. FUZZ_DEBUG5: what
    // shrink5 found, each time.)

    // What part 5 notes of the original's run just made (original_writes), kept with a start: where its stores landed.
    void learn5(Kept& k) {
        k.known5 = true;
        k.writes = original_writes;
        std::sort(k.writes.begin(), k.writes.end());
        k.writes.erase(std::unique(k.writes.begin(), k.writes.end()), k.writes.end());
        if (k.writes.size() > 1024) k.writes.resize(1024);
    }

    // One alias (see Part 5): `from`, a place the parent's original stored to (three times in four) or one of the words it
    // read (and which byte of it); `to`, a word it read (three times in four) or a place it stored to, and which byte. Each
    // word it read and each input register whose value is a base address of `from` -- below it or above it by less than
    // 4 KB, as a structure's fields and a table's entries are -- is a way to move `from`; one of them is given its value
    // plus the distance from `from` to `to`. Applied and kept in the child. Whether there was one.
    bool alias_tweak5(Kept& child, const Kept& parent, recomp_context* start) {
        auto read_place = [&]() { return parent.reads[random32() % parent.reads.size()] + 0x80000000u + random32() % 4; };
        auto write_place = [&]() { return parent.writes[random32() % parent.writes.size()]; };
        if (parent.reads.empty() && parent.writes.empty()) return false;
        const u32 from = !parent.writes.empty() && (parent.reads.empty() || random32() % 4 != 0) ? write_place() : read_place();
        const u32 to = !parent.reads.empty() && (parent.writes.empty() || random32() % 4 != 0) ? read_place() : write_place();
        if (from == to) return false;
        // (Never a place on the stack, the function's frame or the words above it its stack parameters come in: no pointer
        //  the game hands a function can lead there -- as own_frame_address has it -- and the converted function holds its
        //  parameters and locals in C++ variables, which an alias there would only show as the tester's difference.
        //  func_801256FC, aliased onto its 6th parameter at 0x80600054, came out "different" for it alone. 2026-11-13)
        const u32 stack_low = STACK - 0x4000u, stack_high = STACK + 0x60u;
        if ((from >= stack_low && from < stack_high) || (to >= stack_low && to < stack_high)) return false;
        auto based = [&](u32 v) { const s32 d = (s32)(from - v); return d > -0x1000 && d < 0x1000; };
        std::vector<Tweak> options;
        for (u32 o : parent.reads) {
            const u32 v = memory_word(o);
            if (based(v)) options.push_back({MEMORY_WORD, o, v + (to - from)});
        }
        for (u32 reg : registers4()) {
            const u32 v = (u32)(&start->r0)[reg];
            if (based(v)) options.push_back({REGISTER, reg, v + (to - from)});
        }
        if (options.empty() || child.tweaks.size() >= MOST_TWEAKS) return false;
        const Tweak w = options[random32() % options.size()];
        apply(w, start);
        child.tweaks.push_back(w);
        alias_to = to;
        return true;
    }

    // After a test whose original ran long or met a NaN (see Part 5): the tweaks that make a loop's bound small, from what
    // part 4 noted of the original's run (compares4, events4, results4), added to `child`. Whether there were any.
    bool shrink5(Kept& child) {
        // Each integer comparison the original made, how often, where first, and whether either side was the same every time.
        struct Site { u32 count = 0; size_t first = 0; u32 a = 0, b = 0; bool a_same = true, b_same = true; };
        std::unordered_map<u32, Site> sites;
        std::vector<u32> order;
        for (size_t i = 0; i < compares4.size(); i++) {
            const Compare4& c = compares4[i];
            if (c.op >= 16) continue;      // (a float comparison: no count)
            Site& s = sites[c.site];
            if (s.count++ == 0) {
                s.first = i;
                s.a = c.a;
                s.b = c.b;
                order.push_back(c.site);
            } else {
                s.a_same = s.a_same && c.a == s.a;
                s.b_same = s.b_same && c.b == s.b;
            }
        }
        // A loop's bound: a comparison made 8 times or more, one side changing and the other not, that side more than 16.
        struct Bound { size_t first; u32 value; };
        std::vector<Bound> bounds;
        for (u32 site : order) {
            const Site& s = sites[site];
            if (s.count < 8 || s.a_same == s.b_same) continue;
            const u32 v = s.a_same ? s.a : s.b;
            if (v > 16) bounds.push_back({s.first, v});
        }
        if (bounds.empty()) return false;
        const Bound bound = bounds[random32() % bounds.size()];
        const u32 v = bound.value;
        // What holds it, or the bits it is made from: the values the original read or was handed back before the loop's
        // first comparison (up to 256 of them, its own frame left out as part 4's sources are), each whole, each halfword
        // and each byte -- as a load of that size would read it.
        // (power: for a bit count, the 1 << n it made and the 1 << n it now makes, else 0.)
        struct Option { size_t event; u32 bit; u32 value; u32 power_was, power_now; };
        std::vector<Option> options;
        auto lanes = [&](size_t event, u32 bit, u32 word) {
            for (int width : {32, 16, 8}) {
                for (int shift = 0; shift < 32; shift += width) {
                    const u32 mask = width == 32 ? 0xFFFFFFFFu : ((1u << width) - 1) << shift;
                    const u32 x = (word & mask) >> shift;
                    u32 small = 0, was = 0, now = 0;
                    if (x == v) small = 1 + random32() % 8;       // (the bound itself)
                    else if ((x & 31) >= 5 && ((1u << (x & 31)) == v || (1u << (x & 31)) >> 1 == v || (1u << (x & 31)) << 1 == v)) {
                        small = (x & ~31u) | (1 + random32() % 3);  // (1 << n, half or twice it: n made 1 to 3)
                        was = 1u << (x & 31);
                        now = 1u << (small & 31);
                    } else continue;
                    options.push_back({event, bit, (word & ~mask) | ((small << shift) & mask), was, now});
                }
            }
        };
        // (And the 8 latest of them, each lane moved by what makes the bound small: a bound worked out from what was read --
        //  func_8000CAF8's rows, its record's halfword at 0xA less a count from its stack, -8 and so 4 billion to an unsigned
        //  compare -- is that value plus or minus something, as part 4's shifts take it.)
        const u32 small_bound = 1 + random32() % 8;
        std::vector<Option> moves;
        auto moved = [&](size_t event, u32 bit, u32 word) {
            const u32 by = small_bound - v;
            moves.push_back({event, bit, word + by, 0, 0});
            moves.push_back({event, bit, (word & 0xFFFF0000u) | ((word + by) & 0xFFFF), 0, 0});
            moves.push_back({event, bit, (word & 0xFFFF) | ((((word >> 16) + by) & 0xFFFF) << 16), 0, 0});
            for (int b = 0; b < 32; b += 8) moves.push_back({event, bit, (word & ~(0xFFu << b)) | ((((word >> b) + by) & 0xFF) << b), 0, 0});
        };
        const size_t at = std::min<size_t>(compares4[bound.first].event, events4.size());
        for (size_t j = at, seen = 0; j-- > 0 && seen < 256;) {
            const Event4& e = events4[j];
            if (e.kind == READ_EVENT) {
                if (e.where >= FRAME_LOW && e.where < FRAME_HIGH && e.filled == 0) continue;
                seen++;
                lanes(j, 0, e.value);
                if (seen <= 8) moved(j, 0, e.value);
            } else if (e.where < results4.size()) {
                seen++;
                for (u32 bit : {2u, 3u}) {
                    if (!(results4[e.where].may_write >> bit & 1)) continue;
                    lanes(j, bit, results4[e.where].regs[bit]);
                    if (seen <= 8) moved(j, bit, results4[e.where].regs[bit]);
                }
            }
        }
        static const bool debug5 = std::getenv("FUZZ_DEBUG5") != nullptr;
        if (debug5) std::fprintf(stderr, "  shrink5: %zu compares, %zu bounds, bound %08X, %zu moves, %zu options\n", compares4.size(), bounds.size(), v, moves.size(), options.size());
        // (Three times in four a lane that holds the bound or its bit count, when there is one -- half of those times the
        //  latest read before the loop, which is most often its count -- else a moved one.)
        if ((options.empty() && moves.empty()) || child.tweaks.size() >= MOST_TWEAKS) return false;
        const bool exact = !options.empty() && (moves.empty() || random32() % 4 != 0);
        const Option& o = exact ? options[random32() % 2 ? 0 : random32() % options.size()] : moves[random32() % moves.size()];
        Tweak w{MEMORY_WORD, 0, 0};
        if (!source_tweak(events4[o.event], o.bit, o.value, w)) return false;
        child.tweaks.push_back(w);
        // (A size made from a bit count is most often checked against a size kept elsewhere first -- func_8000CEEC goes on
        //  only when its record's halfword at 8 is the image's 1 << width -- so every other halfword or word read before the
        //  loop that held the old 1 << n is given the new one too, or the made-small test would stop at that check.)
        if (o.power_was != 0) {
            for (size_t j = at; j-- > 0;) {
                const Event4& e = events4[j];
                if (e.kind != READ_EVENT || j == o.event || child.tweaks.size() >= MOST_TWEAKS) continue;
                if (e.where >= FRAME_LOW && e.where < FRAME_HIGH && e.filled == 0) continue;
                u32 word = e.value;
                if (word == o.power_was) word = o.power_now;
                if ((word & 0xFFFF) == o.power_was) word = (word & 0xFFFF0000u) | o.power_now;
                if ((word >> 16) == o.power_was) word = (word & 0xFFFF) | (o.power_now << 16);
                if (word == e.value) continue;
                Tweak more{MEMORY_WORD, 0, 0};
                if (source_tweak(e, 0, word, more)) child.tweaks.push_back(more);
            }
        }
        return true;
    }

    // Part 4's tests, until every instruction has been reached and as many tests again have run since (at least 20,000), or `most`
    // tests, or the time (FUZZ_PART4_MS, 40 seconds by default). Returns how many ran. As run, with part 4's additions: a quarter
    // of its tests are new random starts whose stand-ins draw independently (mixed_calls; their children keep that), a kept start
    // part 4 hasn't run yet is run once as it is to learn what part 4 notes, and two in three changes are part 4's (mutate4),
    // the rest part 2's.
    // (Part 5 too, with `five` set: its own time, FUZZ_PART5_MS, and its additions -- see Part 5.)
    long run4(long most) {
        const char* ms = std::getenv(five ? "FUZZ_PART5_MS" : "FUZZ_PART4_MS");
        const ULONGLONG limit = ms ? (ULONGLONG)std::atoll(ms) : 40000, began = fuzz_clock_ms();
        int before = 0;
        for (int i = 0; i < f.instructions; i++) before += f.reached[i] ? 1 : 0;
        const size_t kept_before = kept.size();
        long n = 0, whole_at = -1;
        last_gain = 0;
        // (Part 5: the next test, when the last one's original ran long -- past the access limit, or past it and finished
        //  with more room -- is that test with a loop's bound made small: shrink5. Up to 6 in a row.)
        Kept pending;
        bool have_pending = false;
        size_t uncompared_kept = 0, aliases_kept = 0;
        int pending_depth = 0;
        while (n < most) {
            if (fuzz_clock_ms() - began > limit) break;
            if ((n & 1023) == 0 || touched_pages.size() > 256) give_back_touched_pages();
            if (whole_at < 0 && all_reached()) whole_at = n;
            if (whole_at >= 0 && n >= std::max(2 * whole_at, 20000L)) break;
            steering_now = true;
            if ((n & 1023) == 0) find_rarest();
            n++;
            tests_now = n;
            Kept child;
            recomp_context start;
            const Kept* parent = nullptr;
            size_t which = 0;
            const bool follow_up = five && have_pending;
            alias_to = 0;
            const int depth = follow_up ? pending_depth : 0;
            if (!follow_up && !kept.empty() && random32() % 4 != 0) {
                which = choose();
                parent = &kept[which];
            }
            // (Part 5: a start whose original's stores it hasn't noted yet is run once as it is, too.)
            const bool learning = parent != nullptr && (!parent->known4 || (five && !parent->known5));
            if (follow_up) {
                child = std::move(pending);
                have_pending = false;
                const uint64_t resume = random_state;
                make(child, &start);
                random_state = resume;
            } else if (parent == nullptr) {
                child.base = (((uint64_t)random32() << 32) | random32()) | 1;
                child.mixed = true;
                const uint64_t resume = random_state;
                random_state = child.base;
                make_start(f, &start);
                random_state = resume;
            } else {
                child.base = parent->base;
                child.tweaks = parent->tweaks;
                child.mixed = parent->mixed;
                const uint64_t resume = random_state;
                make(*parent, &start);
                random_state = resume;
                if (!learning) {
                    if (random32() % 3 == 0 && parent->known) mutate(child, *parent, &start);
                    else mutate4(child, *parent, &start);
                }
            }
            long_retry = random32() % 4 == 0;
            const uint64_t resume = random_state;
            std::vector<u8> reached_now(f.instructions + 1, 0);
            Outcome e = NOT_COMPARED;
            for (int attempt = 0;; attempt++) {
                result_tweaks = &child.tweaks;
                mixed_calls = child.mixed;
                lift_fuzz_reads.clear();
                lift_fuzz_compares.clear();
                events4.clear();
                compares4.clear();
                results4.clear();
                lift_fuzz_noting_reads = true;
                noting_writes = compares_capped = five;
                original_writes.clear();
                std::fill(reached_now.begin(), reached_now.end(), 0);
                e = run_both(f, start, reached_now, true);
                lift_fuzz_noting_reads = noting_writes = compares_capped = false;
                result_tweaks = nullptr;
                mixed_calls = false;
                undo_memory();
                // (Part 5: up to 6 times. A table its loop reads anew each pass can hold a NaN in more than two of its entries,
                //  and mend4 mends only what the run read before it stopped. 2026-11-12)
                if (e == COMPARED || learning || attempt == (five ? 6 : 2) || !original_saw_nan) break;
                // The original fed a NaN to its float arithmetic, which no game state does (NAN_CHECK in fuzz.h), so the test
                // is no test. Most often it read one, as random words and float bits sometimes are: each word it read, each
                // float a call handed back and each float parameter that isn't a number (an exponent of all ones, as a float or
                // as a double's top word) is given a number instead (mend4), and the test made again and run, up to twice. That
                // is only a different start; it is compared as any other. (Float code -- func_80022CEC's -- had most of its
                // steered tests skipped for this.)
                if (!mend4(child, &start)) break;
                const uint64_t keep = random_state;
                make(child, &start);
                random_state = keep;
            }
            long_retry = false;
            random_state = resume;
            // (And after one whose original still met a NaN when the mends ran out: a loop that reads a new table entry each
            //  pass meets one sooner or later -- modrep_func_80020B58's, 1,390 passes in, at 0x80055DBC -- and made shorter it
            //  may not. shrink5 finds nothing when no comparison was made 8 times. 2026-11-12)
            if (five && use_shrink && !learning && depth < 6 && (original_long || original_saw_nan)) {
                Kept next;
                next.base = child.base;
                next.tweaks = child.tweaks;
                next.mixed = child.mixed;
                if (shrink5(next)) {
                    pending = std::move(next);
                    have_pending = true;
                    pending_depth = depth + 1;
                }
            }
            if (e != COMPARED && learning && !events4.empty()) learn4(kept[which]);    // (what it read up to there still helps)
            if (e != COMPARED && learning && five) learn5(kept[which]);
            if (five && e != COMPARED && !learning && uncompared_kept < 512) {
                // (Part 5: a test that wasn't compared -- it ran long, or met a NaN the mends didn't reach -- but whose
                //  original ran an instruction no compared test has, is kept as a start too, to steer from: the way there
                //  is the hard part, and its children get the mends and shrink5 again. Nothing it ran counts as reached:
                //  only a compared test's does, as everywhere. modrep_func_80020B58's first parameter 11 takes it to 0x15
                //  at 0x81104BDC, whose loop then meets a NaN in its table or runs past the limit; dropped, that start had
                //  to be found again from nothing. Up to 512 of them.)
                bool beyond = false;
                for (int i = 0; i < f.instructions && !beyond; i++) beyond = reached_now[i] != 0 && !f.reached[i];
                if (beyond) {
                    const size_t had = kept.size();
                    consider(child.base, child.tweaks, reached_now, &lift_fuzz_reads, calls_by_original, &lift_fuzz_compares, true, false);
                    if (kept.size() > had) {
                        kept.back().mixed = child.mixed;
                        learn4(kept.back());
                        learn5(kept.back());
                        uncompared_kept++;
                    }
                }
            }
            if (e != COMPARED) continue;
            for (const Compare4& c : compares4) site_seen[c.site] |= c.op >= 16 ? float_outcome4(c) : outcome4(c.a, c.b);
            if (learning) {
                // A start from part 1 or 2, run once more as it was: now what part 4 notes is known (and part 2's, if not yet).
                Kept& k = kept[which];
                if (!k.known) {
                    std::sort(lift_fuzz_reads.begin(), lift_fuzz_reads.end());
                    lift_fuzz_reads.erase(std::unique(lift_fuzz_reads.begin(), lift_fuzz_reads.end()), lift_fuzz_reads.end());
                    for (u32 o : lift_fuzz_reads) {
                        if (o < FRAME_LOW || o >= FRAME_HIGH) k.reads.push_back(o);
                    }
                    if (k.reads.size() > 2048) k.reads.resize(2048);
                    k.calls = calls_by_original;
                    k.known = true;
                    k.compares = unique_compares(lift_fuzz_compares);
                }
                learn4(k);
                if (five) learn5(k);
                continue;
            }
            const size_t had = kept.size();
            const bool mixed = child.mixed;
            bool only_way = false;
            for (const Compare4& c : compares4) only_way = only_way || (open4(c.op, c.taken, c.fall) >= 0 && !site_in_kept.count(c.site));
            // (Part 5: a compared test whose alias worked -- its original's store landed where it was aimed -- is kept too, up
            //  to 512 of them, even if it reached nothing new: what the path past it needs most often depends on the value
            //  stored as well (func_801422D8's byte must be 79 to 127, from the float it then overwrites), and its children
            //  change those values with the alias still in place. Without this only the few starts part 4 kept were ever
            //  aliased, and in 2 runs of 3 none of them had such a float.)
            if (five && alias_to != 0 && aliases_kept < 512 && std::find(original_writes.begin(), original_writes.end(), alias_to) != original_writes.end()) {
                only_way = true;
                aliases_kept++;
            }
            consider(child.base, std::move(child.tweaks), reached_now, &lift_fuzz_reads, calls_by_original, &lift_fuzz_compares, only_way);
            if (kept.size() > had) {
                kept.back().mixed = mixed;
                learn4(kept.back());
                if (five) learn5(kept.back());
            }
        }
        steering_now = false;
        int after = 0;
        for (int i = 0; i < f.instructions; i++) after += f.reached[i] ? 1 : 0;
        if (std::getenv("FUZZ_DEBUG4") != nullptr) {
            // (A look inside, for working on the tester: each branch still stuck, and how many kept starts have been there.)
            for (size_t i = 0; i < kept.size(); i++) {
                int n = 0;
                for (u32 w : kept[i].reached) n += __builtin_popcount(w);
                std::fprintf(stderr, "  kept %zu: known %d known4 %d mixed %d tweaks %zu compares4 %zu after_calls %zu results %zu reached %d\n", i,
                             kept[i].known, kept[i].known4, kept[i].mixed, kept[i].tweaks.size(), kept[i].compares4.size(),
                             kept[i].after_calls.size(), kept[i].results.size(), n);
            }
            for (const auto& [site, seen] : site_seen) {
                if (stuck4(site) == 0) continue;
                size_t with = 0;
                for (const Kept& k : kept) {
                    bool has = false;
                    for (const Kept::Compare& c : k.compares4) has = has || c.site == site;
                    with += has;
                }
                std::fprintf(stderr, "  stuck: line %u kind %u seen %02X missing %02X, in %zu kept starts\n", site / 4, site & 3, seen,
                             stuck4(site), with);
            }
        }
        std::fprintf(stderr, "%s %s: %ld tests (last new instruction at %ld), %zu starts kept (%zu new), reached %d -> %d of %d\n", five ? "part 5" : "part 4", f.name,
                     n, last_gain, kept.size(), kept.size() - kept_before, before, after, f.instructions);
        return n;
    }
};

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    if (std::strcmp(argv[1], "--independent") == 0) {
        if (argc < 6) return 2;
        for (int i = 0; i < fuzz_function_count; i++) {
            if (std::strcmp(fuzz_functions[i].name, argv[2]) != 0) continue;
            lift_rdram = (uint8_t*)VirtualAlloc(nullptr, MEMORY_BYTES, MEM_RESERVE, PAGE_NOACCESS);
            if (lift_rdram == nullptr) return 3;
            if (VirtualAlloc(lift_rdram, KEPT_BYTES, MEM_COMMIT, PAGE_READWRITE) == nullptr) return 3;
            AddVectoredExceptionHandler(1, commit_on_touch);
            for (u32 a = FILLED_START; a < FILLED_START + FILLED_BYTES; a += 4) word_at(a) = random_word();
            recomp_context check;
            if (register_named(&check, argv[3]) == nullptr) return 2;
            dictionary = fuzz_functions[i].constants;
            dictionary_count = fuzz_functions[i].constant_count;
            note_device_pages(fuzz_functions[i]);
            compare_dictionary = fuzz_functions[i].compares;
            compare_count = fuzz_functions[i].compare_count;
            track_memory = fuzz_functions[i].system_calls != 0;
            under_test = &fuzz_functions[i];
            return independence(fuzz_functions[i], argv[3], std::atol(argv[4]), argv[5]);
        }
        return 2;
    }
    if (std::strcmp(argv[1], "--list") == 0) {
        for (int i = 0; i < fuzz_function_count; i++) std::printf("%s\n", fuzz_functions[i].name);
        return 0;
    }
    const FuzzFunction* found = nullptr;
    for (int i = 0; i < fuzz_function_count; i++) {
        if (std::strcmp(fuzz_functions[i].name, argv[1]) == 0) found = &fuzz_functions[i];
    }
    if (found == nullptr) return 2;
    const FuzzFunction& f = *found;
    abi_mode = f.abi != 0;      // (abi.py's functions: see fuzz_stand_in_abi)
#if FUZZ_MOD_NATIVE
    private_table = fuzz_private_table(f.name, &private_count);     // (the mod's private data: none unless lift.py gives a table)
    if (std::getenv("FUZZ_NO_PRIVATE") != nullptr) { private_table = nullptr; private_count = 0; }   // (FUZZ_NO_PRIVATE: never; final081)
    native_rules = fuzz_native_rules(f.name);      // (the user's rules of 2026-11-13: native candidates only, lift.py)
#endif
    write_cut_here = fuzz_write_cut(f.name);       // (the write cut, 2026-11-15: only the functions lift.py names)
    track_memory = f.system_calls != 0;     // (only then: the sum costs a little on every write)
    under_test = &f;
    dictionary = f.constants;
    dictionary_count = f.constant_count;
    note_device_pages(f);
    compare_dictionary = f.compares;
    compare_count = f.compare_count;
    const long tests = std::atol(argv[2]);
    // (FUZZ_SEED: another sequence of tests altogether, for lift.py's reach pass. Without it the tests are exactly as they were.)
    if (const char* seed = std::getenv("FUZZ_SEED")) { random_state ^= (uint64_t)std::atoll(seed) * 0xC2B2AE3D27D4EB4Full; if (random_state == 0) random_state = 1; }
    lift_rdram = (uint8_t*)VirtualAlloc(nullptr, MEMORY_BYTES, MEM_RESERVE, PAGE_NOACCESS);
    if (lift_rdram == nullptr) return 3;
    if (VirtualAlloc(lift_rdram, KEPT_BYTES, MEM_COMMIT, PAGE_READWRITE) == nullptr) return 3;
    AddVectoredExceptionHandler(1, commit_on_touch);
    AddVectoredExceptionHandler(1, stop_on_division_trap);
    for (u32 a = FILLED_START; a < FILLED_START + FILLED_BYTES; a += 4) word_at(a) = random_word();
    if (std::strncmp(f.name, "modrep_", 7) == 0) {
        // One of the mod's copies: the mod's sections get random contents too, and stay (see fuzz_mod_region).
        u32 mod_start = 0, mod_bytes = 0;
        fuzz_mod_region(&mod_start, &mod_bytes);
        if (mod_bytes != 0) {
            mod_fill_start = (u64)(mod_start - 0x80000000u) & ~(u64)0xFFFF;
            mod_fill_end = ((u64)(mod_start - 0x80000000u) + mod_bytes + 0xFFFF) & ~(u64)0xFFFF;
            if (VirtualAlloc(lift_rdram + mod_fill_start, mod_fill_end - mod_fill_start, MEM_COMMIT, PAGE_READWRITE) == nullptr) return 3;
            for (u32 a = mod_start & ~3u; a < mod_start + mod_bytes; a += 4) { u32 v; do { v = (random32() % 8) ? random_pointer(4) : random_word(); } while (v == 0); word_at(a) = v; }   // (not zero: a zero left as it was reads like an unfilled page)
#if FUZZ_MOD_NATIVE
            // (2026-11-12, scratch/agents/modnative1: the tester addition the user approved, "the mod's built image loaded into
            //  tester memory with its writable part randomised".) For one of the mod's own functions made native, the mod's
            //  READ-ONLY part -- its constants, tables, strings and the compiler's float pools, 0x81000000 up to its code -- is
            //  the built mod's own bytes (relocated to where the tester put the mod), not random: the native function has those
            //  constants compiled in, as the original has them in memory. Only from the random fill's own state above, so every
            //  other test is exactly as before; nothing for a function lift.py gives no image (fuzz_mod_image).
            u32 image_start = 0, image_bytes = 0;
            if (const char* path = fuzz_mod_image(f.name, &image_start, &image_bytes)) {
                FILE* file = std::fopen(path, "rb");
                if (file == nullptr) { std::fprintf(stderr, "the mod's image %s is missing\n", path); return 3; }
                std::vector<u32> words(image_bytes / 4);
                if (std::fread(words.data(), 4, words.size(), file) != words.size()) { std::fclose(file); return 3; }
                std::fclose(file);
                for (size_t i = 0; i < words.size(); i++) word_at(image_start + 4 * (u32)i) = words[i];
                mod_image_start = image_start - 0x80000000u;
                mod_image_end = mod_image_start + image_bytes;
            }
#endif
        }
    }
    {   // (The pools, from a state of their own: the tests' inputs are made exactly as they were.)
        const uint64_t kept_state = random_state;
        random_state = 0xD1B54A32D192ED03ull;
        for (u32 i = 0; i <= POOL_MASK; i++) { word_pool[i] = random_word(); float_pool[i] = random_float_bits(); }
        random_state = kept_state;
    }

    show_differences = std::getenv("FUZZ_SHOW") != nullptr;      // Prints the first difference in full.
    const ULONGLONG started = fuzz_clock_ms();
    long tests_run = 0;
    Steering steering(f);
    // Part 1: random inputs, exactly as before steering was added (the same inputs in the same order, so whatever this part proved
    // it still proves). The additions: a test that reached something no earlier test had is remembered, by the random state it
    // started from, as a starting point for part 2; and a test where only one version ran past the access limit gets the second
    // chance (run_both).
    // (t < tests + nan_set_aside: a test set aside for a NaN operand is made up for at the end, so the first `tests` starts are
    //  the same as before and as many of them are compared. 2026-11-08)
    // (And until `tests` have been COMPARED, under the same time limit, 2026-11-09: a test where both versions ran away isn't compared,
    //  and only compared part-1 tests count toward the proof. The first `tests` tests are the same ones as before; these only add more.)
    const bool fallback_allowed = std::getenv("FUZZ_NO_FALLBACK") == nullptr;
    fresh_learning = fallback_allowed;
    for (long t = 0; t < tests + nan_set_aside || compared < tests; t++) {
        if ((t & 255) == 0 && t >= 256 && fuzz_clock_ms() - started > MOST_MILLISECONDS) break;
        // (2026-11-12) None of the first FALLBACK_AFTER tests compared: the rest of the function's tests are in fallback (see
        //  there). Otherwise what was noted for it is dropped, and the function is tested exactly as it was.
        if (t == 2 * FALLBACK_AFTER && fallback && compared == 0 && std::getenv("FUZZ_NO_FALLBACK64") == nullptr) {
            fallback64 = true;      // (approved 2026-11-12: see fallback64)
            std::fprintf(stderr, "fallback64 %s: none of the first %ld tests compared; both versions get 64 times the room\n", f.name, 2 * FALLBACK_AFTER);
        }
        if (t == FALLBACK_AFTER && fresh_learning) {
            if (compared == 0) {
                fallback = true;
                std::fprintf(stderr, "fallback %s: none of the first %ld tests compared; %zu words of memory made fresh\n", f.name, FALLBACK_AFTER, fresh_words.size());
            } else {
                fresh_learning = false;
                fresh_words.clear();
            }
        }
        if ((t & 1023) == 0 || touched_pages.size() > 256) give_back_touched_pages();    // At most 16 MB of stray pages at a time.
        tests_run++;
        const uint64_t base = random_state;
        part1_fills = part1_fill_coin();
        recomp_context start;
        make_start(f, &start);
        std::vector<u8> reached_now(f.instructions + 1, 0);
        // (With the second chance too, from 2026-11-07: func_8012D0E8's original finished in 19,578 accesses and the converted one,
        //  reading its frame locals through Ref a few more times, took 20,001; func_8012EF7C the other way round. That was noted
        //  as a difference. Now the one that stopped runs again with more room and the two are compared. It changes nothing in a
        //  test where both finish or both run away, and those are all a proved function has.)
        if (run_both(f, start, reached_now, true) == COMPARED) steering.consider(base, {}, reached_now, nullptr, 0);
    }
    part1_fills = false;
    fresh_learning = false;     // (fallback: fresh_words is kept as part 1 left it, for parts 2 to 4)
    const long part1_compared = compared;       // (part 1's alone: the report's last column)

    // Part 2 (2026-11-07), only for a function part 1 didn't wholly reach: steered inputs. See Steering.
    if (!steering.all_reached() && std::getenv("FUZZ_NO_STEER") == nullptr) tests_run += steering.run(tests);

    // Part 3 (2026-11-07): boundary inputs. A quarter as many tests again (and a quarter of the time), random as part 1's but with
    // one word in four of each start being a compare's constant or one either side of it (compare_word), where a compare changes
    // its outcome: a converted compare that reads another value or another sign than the original's shows only there
    // (func_8014434C, the value from after a branch's delay slot). After parts 1 and 2, so it only adds -- instructions reached,
    // differences found -- and changes nothing they did. (FUZZ_NO_BOUNDARY: without it.)
    if (compare_count != 0 && std::getenv("FUZZ_NO_BOUNDARY") == nullptr) {
        in_part3 = true;
        const ULONGLONG part3_started = fuzz_clock_ms();
        for (long t = 0; t < tests / 4; t++) {
            if ((t & 255) == 0 && t >= 256 && fuzz_clock_ms() - part3_started > MOST_MILLISECONDS / 4) break;
            if ((t & 1023) == 0 || touched_pages.size() > 256) give_back_touched_pages();
            tests_run++;
            recomp_context start;
            boundary_start = true;
            make_start(f, &start);
            boundary_start = false;
            std::vector<u8> reached_now(f.instructions + 1, 0);
            run_both(f, start, reached_now, true);
        }
        in_part3 = false;
    }

    // Part 4 (2026-11-08), only for a function parts 1 to 3 didn't wholly reach: steered tests with what part 2 can't do. See
    // Part 4 in Steering. (FUZZ_NO_PART4: without it. FUZZ_PART4_MS: its time.)
    // Reach only: its tests add nothing to the report's tests or compared columns, which are parts 1 to 3's. What it reaches counts, as
    // part 2's does, and a difference it finds counts.
    std::fprintf(stderr, "before part 4 %s: %ld compared, %ld differences, first %s\n", f.name, compared, different, first.c_str());
#if FUZZ_MOD_NATIVE
    if (outside_set_aside) std::fprintf(stderr, "outside set aside %s: %ld (the user's rule (4))\n", f.name, outside_set_aside);
#endif
    if (!steering.all_reached() && std::getenv("FUZZ_NO_PART4") == nullptr) {
        const long compared_before_part4 = compared;
        in_part4 = true;
        steering.run4(tests * 8);   // (its time ends it first: 40 seconds)
        in_part4 = false;
        compared = compared_before_part4;
    }
    // Part 5 (2026-11-12, agent "steer"), only for a function parts 1 to 4 didn't wholly reach: part 4's tests with aliases,
    // small loop bounds and constant offsets. See Part 5 in Steering. Reach only, as part 4. (FUZZ_NO_PART5: without it. FUZZ_PART5_MS: its time.)
    if (!steering.all_reached() && std::getenv("FUZZ_NO_PART5") == nullptr && (use_alias || use_shrink || use_constant_offsets)) {
        const long compared_before_part5 = compared;
        in_part4 = true;        // (what part 4 notes and its stand-ins' additions, as there)
        steering.five = true;
        steering.run4(tests * 8);
        steering.five = false;
        in_part4 = false;
        compared = compared_before_part5;
    }

    // The same form as a proof-build report.
    std::string mask;
    for (int bit = f.instructions - 1 - ((f.instructions - 1) % 4); bit >= 0; bit -= 4) {
        int nibble = 0;
        for (int j = 0; j < 4 && bit + j < f.instructions; j++) nibble |= (f.reached[bit + j] ? 1 : 0) << j;
        mask += "0123456789abcdef"[nibble];
    }
    std::printf("%s %ld %ld %ld %s %d %s %ld\n", f.name, tests_run, compared, different, mask.c_str(), f.instructions, first.c_str(), part1_compared);
    if (nan_made_set_aside) std::fprintf(stderr, "%s: %ld tests set aside for a NaN the original made (FUZZ_NAN_MADE)\n", f.name, nan_made_set_aside);
    // (The step limit's set-aside tests, all parts', for lift.py to keep: fuzz/steps_set_aside.txt. 2026-11-14, rules084)
    if (write_cut_here)
        std::fprintf(stderr, "write cut %s: %ld tests compared at a write cut (reach only)\n", f.name, write_cut_tests);
    if (steps_set_aside_both || steps_mixed_different)
        std::fprintf(stderr, "step limit set aside %s: %ld both past the step limit, %ld mixed noted as differences\n",
                     f.name, steps_set_aside_both, steps_mixed_different);
    return 0;
}
