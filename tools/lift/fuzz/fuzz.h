// fuzz.h: what the generated part of the random-input tester needs. See fuzz_main.cpp for how a test works.
#pragma once
#include <cstdint>

// (2026-11-14, fastnight) FUZZ_MOD_NATIVE: 1, the default, is the whole tester. lift.py builds it with 0 for the functions that
// have no private table, no native rules and no mod image (today every game function and every copy of the mod's: only the
// mod's own functions made native, and their controls, have them -- lift.py fuzz_variant): the code only those use -- each piece marked "#if FUZZ_MOD_NATIVE" --
// is then left out. That changes nothing such a test does: every piece of it does nothing unless lift.py gives the function a
// private table (fuzz_private_table), native rules (fuzz_native_rules) or the mod's image (fuzz_mod_image). Why: the result
// cache's key (lift.py fuzz_tester_hash) is the tester as preprocessed for the build that runs the function, so a change to
// the native-only code tests again only the functions that run it.
#ifndef FUZZ_MOD_NATIVE
#define FUZZ_MOD_NATIVE 1
#endif
#include "recomp.h"
#include "lift_mem.h"

// One converted function, and how to call it. A parameter's kind, by its number: 'b', 'h', 'w' are pointers to values of 1, 2 or 4
// bytes, 'i' an integer, 'f' a float (in f12 or f14), 'g' a float in an integer register or stack slot (a number, never NaN), and '.'
// a parameter the function ignores.
struct FuzzFunction {
    const char* name;
    void (*original)(uint8_t* rdram, recomp_context* ctx, u8* reached);
    void (*converted)(uint8_t* rdram, recomp_context* ctx);
    const char* params;
    int result;              // 0 nothing, 1 an integer in v0, 2 a float in f0, 3 a double in f0:f1 (2026-11-12, "refused2").
    u8* reached;             // Which of the original's instructions have run, one byte each (not 0: it has).
    int instructions;
    const u32* addresses;    // The fixed game addresses it names, ending in 0: their contents get randomized too.
    const u32* constants;    // The numbers in its own instructions (lift.py fuzz_constants), for random_word to draw on sometimes.
    int constant_count;
    // The port added code to its translated version (tools/lift/port_additions.py) that may test a pointer parameter against a
    // fixed game address (func_80001E08's matrix hook: the camera matrix, 0x80062BA8): a pointer parameter is then sometimes one
    // of its fixed addresses. 0 for every other function, whose random inputs stay exactly as they were.
    int port_pointers;
    // For a function that doesn't keep to the calling rules (tools/lift/abi.py), 0 for the rest: the registers whose values at the
    // end must match the original's (bit n: GPR n; bit 32 + n: float register n), and whether its tests run the stand-ins'
    // faithful way (fuzz_stand_in_abi).
    uint64_t abi_out;
    int abi;
    // What its compares compare with, each with the numbers either side (lift.py fuzz_compare_constants), for part 3's inputs.
    const u32* compares;
    int compare_count;
    // The registers other than a0-a3 the function reads before it sets them, a bit each (GPR n: bit n): for abi.py's functions
    // their inputs (s4, v1, t0, ...), and gp for one that reads it. Part 4 changes these too (fuzz_main.cpp, Steering::registers4).
    // 0 for most functions. (lift.py run_fuzz, fuzz_input_registers.)
    u32 input_registers;
    // 1 if it calls a system function (lift.py calls_system, 2026-11-07): memory is then tracked from the start of each run, for
    // fuzz_system_stand_in to note as it is at each call. 0 for every other function, whose tests stay exactly as they were.
    int system_calls;
    // 1 if it has no return instruction (lift.py never_returns, 2026-11-12): a thread's body that loops for ever, each of whose
    // tests then ends at its K-th call (fuzz_main.cpp cut_call). 0 for every other function (and left out, so 0, by the tables
    // that don't give it: the independence test's), whose tests stay exactly as they were.
    int never_returns;
};

// In place of a function the converted one calls: notes what it was given, and hands back random results (see fuzz_main.cpp).
void fuzz_stand_in(u32 which, u32 parameter_mask, u32 stack_words, recomp_context* ctx);
#if defined(FUZZ_ARGS_STRICT) && FUZZ_ARGS_STRICT     // (the argument compare, FUZZ_ARGS_STRICT=1: see "The argument compare" in fuzz_main.cpp. Off, nothing here.)
// The next stand-in's callee, as the strict read-set checker (readcheck.py) proves it: `skip`, bit i, the argument register
// a0 a1 a2 a3 f12 f13 f14 f15 (i = 0..7) it never reads; `stack`, bit j, the stack-parameter word at $sp + 0x10 + 4j it may
// read (0xFFFFFFFF: not known -- the caller's outgoing words, fuzz_args_outgoing). lift.py writes the call before each stand-in.
void fuzz_args_next(u32 skip, u32 stack);
// The function under test's outgoing stack-parameter words (readcheck.py outgoing_words): lift.py writes it; 16 without.
u32 fuzz_args_outgoing(const char* function);
#endif
// (2026-11-12, scratch/agents/modglue) The stand-in for one of the mod's own functions (modfn_<address>), which a C patch's glue
// calls: fuzz_stand_in exactly, unless the function under test keeps the mod's private data in PC memory (a table from
// fuzz_private_table) -- then it also notes that data as it is at the call, and may change it, as a real callee may.
void fuzz_mod_stand_in(u32 which, u32 parameter_mask, u32 stack_words, recomp_context* ctx);
#if FUZZ_MOD_NATIVE     // (the mod's native functions only: see FUZZ_MOD_NATIVE above)
// One symbol of the mod's private data, for that rule (confirmed by the user 2026-11-12): see
// "The mod's private data in PC memory" in fuzz_main.cpp.
struct FuzzPrivate {
    u32 address;          // its N64 place (mod.map, as the tester puts the mod: see fuzz_mod_region)
    u32 bytes;
    const u8* widths;     // the N64 layout: each scalar's width in bytes (1, 2, 4 or 8), in order, adding up to `bytes`
    u32 width_count;
    u8* host;             // the converted side's object: the same layout, the PC's byte order
    // (2026-11-13, scratch/agents/modnative5: the user's rule (2) of 2026-11-13) 1 for a byte the mod's compiler shrank a variable
    // to (it holds 0 for the variable's initial value, 1 for its one other): the tester fills it with 0 or 1 only. 0, as every
    // table written before this has it, for everything else.
    u32 zero_or_one;
    // (2026-11-14, scratch/agents/modnative7: the PROPOSED rule NM_RULE_WRITES -- not approved; lift.py writes these only while
    // that switch is set, so every table is as before without it) For a scalar: the only values the mod's own writes can give it
    // -- value_kind 1: a list of value_count values, 2: the range values[0]..values[1]. The tester starts it at one of them
    // and a stand-in that changes it writes one of them. Null for every other symbol.
    const int64_t* values;
    u32 value_count;
    u32 value_kind;
};
const FuzzPrivate* fuzz_private_table(const char* function, int* count);
// ---- The mod's own functions made native: the user's rules of 2026-11-13 (scratch/agents/modnative5) ----
// lift.py gives each native candidate (and nothing else) its rules, fuzz_native_rules; see "The user's rules of 2026-11-13" in
// fuzz_main.cpp for what each does. A place is a GPR's number (0-31), 32 + a float register's, or 0x100 + an offset from the
// stack pointer the test starts with (a parameter past the fourth). Kinds: narrow 'b' / 'B' 8 bits zero- / sign-extended,
// 'h' / 'H' 16 bits, '1' a bool (0 or 1); fast 'i' an integer, 'w' a pointer, 'f' a float. Lists end with place -1.
struct FuzzNarrow { int where; int kind; };
struct FuzzNativeRules {
    const FuzzNarrow* narrow;   // (1) narrow parameters: extended as their C types say
    int narrow_count;
    const FuzzNarrow* fast;     // (3) the function's own parameters by register, when the mod's compiler gave it its fast convention
    int fast_count;
    int loop_budget;            // (3) each loop pass counted against a budget (lift_fuzz_loop_tick)
};
const FuzzNativeRules* fuzz_native_rules(const char* function);
// (3) The stand-in for one of the mod's fast-convention functions a native candidate calls with a float or more than four
// parameters: fuzz_mod_stand_in, after noting the registers in `extra` (GPR n: bit n, float register n: bit 32 + n) in order.
void fuzz_mod_stand_in_fast(u32 which, u32 parameter_mask, uint64_t extra, recomp_context* ctx);
// (3) One pass of a loop: the original's backward jumps (lift.py loop_ticks_original), the native code's loop conditions
// (nm_native.h NM_TICK).
void lift_fuzz_loop_tick();
// (4) The native code indexed an array past its end (nm_native.h NArr): the N64 address it reached.
void lift_fuzz_note_outside(u32 address, u32 bytes);
// (4) While an original runs in a test with the rule on: the word offsets it reads (FuzzMem below) and writes are kept.
extern bool lift_fuzz_watching_mod;
void lift_fuzz_note_mod_access(uint64_t offset);
#endif  // FUZZ_MOD_NATIVE
// ---- The step limit (2026-11-14, scratch/agents/rules084: the user's approval of 2026-11-14 for 0.84, TESTER CHANGE; nothing
//      counts from it until an independent review approves it, REVIEW_STEPLIMIT.txt. APPROVED, strict form only, by
//      reviewr084 2026-11-15) ----
// One step is one pass of a loop of the version that is running: in the original each backward jump (lift.py
// step_ticks_original), in the converted function each time a loop's body starts and each backward goto (lift.py
// step_ticks_converted). Past the limit -- 64 times the access limit, so it grows with the access limit's second chance and
// fallbacks, as the native candidates' loop budget does -- the run has run long (TooLong), exactly as past the access limit: a
// loop with no memory access in it, endless on random data, used to run until lift.py's wall limit killed the whole tester.
// Off unless FUZZ_STEP_LIMIT=1 (lift.py LIFT_STEP_LIMIT=1): steps are then never counted against anything.
extern u32 lift_fuzz_steps;
extern u32 lift_fuzz_step_limit;
[[noreturn]] void lift_fuzz_step_stop();
static inline void lift_fuzz_step() { if (++lift_fuzz_steps > lift_fuzz_step_limit) lift_fuzz_step_stop(); }
// The same, for the generated stand-ins when one of abi.py's functions is among those tested: also given the registers the callee
// reads beyond its parameters, the scratch registers it may change, and those it surely changes (masks as abi_out's). Exactly
// fuzz_stand_in unless the function under test is one of abi.py's.
void fuzz_stand_in_abi(u32 which, u32 parameter_mask, u32 stack_words, uint64_t extra_reads, uint64_t may_write, uint64_t must_write,
                       recomp_context* ctx);

// In place of a mod's hook (2026-11-12, scratch/agents/modhooks; the "hook" stand-in the user approved 2026-11-12, its decisions
// on the mod-touched functions): both versions of a function the mods hook call it at each hook place (lift.py, tools/lift/hooks.py
// lift_run_hook). It notes which place and the registers the hooks there read (`reads`, abi_out's bits), may change memory as any
// stand-in does, and changes no register -- the loader puts the context back after a hook (librecomp mod_hooks.cpp run_hook).
void fuzz_hook_stand_in(u32 which, uint64_t reads, recomp_context* ctx);

// The recording stand-in for a call into an overlay at this address (lift.py run_fuzz writes one for each address a candidate looks
// up; any other address gets fuzz_overlay_unknown_address's), or null where none are written (the independence test). fuzz_main.cpp's
// get_function, which LOOKUP_FUNC calls, hands it back.
recomp_func_t* fuzz_overlay_lookup(int32_t vram);
recomp_func_t* fuzz_overlay_unknown_address(int32_t vram);

// What a system function writes through one of its parameters: the parameter's number (0-3 a0-a3, 4 and up the stack words) and
// how many bytes from where it points. A list of them ends with parameter -1.
struct FuzzRegion { int parameter; u32 bytes; };

// In place of a system function (the runtime's osRecvMesg_recomp and the rest, lift.py stand_in_line, 2026-11-07): notes what it
// was given and the memory as it is at the call, then does what the runtime's version does to the registers -- random values in
// the result registers (result_mask: 1 v0, 2 v1, 4 f0), nothing to any other -- and writes random bytes over its regions.
void fuzz_system_stand_in(u32 which, u32 parameter_mask, u32 stack_words, u32 result_mask, const FuzzRegion* regions, recomp_context* ctx);

// In place of a port function that code the port added to a translated function calls (a frame-interpolation or ray-tracing hook:
// tools/lift/port_additions.py writes one stand-in per function): notes which one and what it was given, in the same list as
// fuzz_stand_in's, and returns a result that depends only on the test and how many calls came before.
u32 fuzz_port_call(u32 which, const uint64_t* words, int count);

// The translated originals' divisions, in the tester only (lift.py run_fuzz puts these in their place). Dividing by zero gives
// what the N64 gives (the quotient -1, or 1 for a negative number, and the number itself as the remainder) where the translated
// line would stop the PC on the spot: so the original goes on to the compiler's check just after it, `bnez $q` then `break 7`,
// and stops there (fuzz_main.cpp, do_break), its break counted as reached. The game ends at either; nothing runs in between.
#define FUZZ_DIV(a, b) do { if ((int32_t)(b) == 0) { lo = S32((int32_t)(a) < 0 ? 1 : -1); hi = S32(a); } \
    else { lo = S32(S64(S32(a)) / S64(S32(b))); hi = S32(S64(S32(a)) % S64(S32(b))); } } while (0)
#define FUZZ_DIVU(a, b) do { if ((uint32_t)(b) == 0) { lo = S32(0xFFFFFFFFu); hi = S32(a); } \
    else { lo = S32(U32(a) / U32(b)); hi = S32(U32(a) % U32(b)); } } while (0)

extern FuzzFunction fuzz_functions[];
extern const int fuzz_function_count;

// The words of memory the original reads, noted while lift_fuzz_noting_reads is set (fuzz_main.cpp's Steering changes them).
extern bool lift_fuzz_noting_reads;
void lift_fuzz_note_read(uint64_t offset);
// The originals' 64-bit loads (the translator's LD, for ldc1: lift.py run_fuzz puts this in its place): the low word, then the
// high, read exactly as recomp.h's load_doubleword reads them, and noted for part 4 (only there: see fuzz_main.cpp).
uint64_t fuzz_load_doubleword(uint8_t* rdram, gpr offset, gpr reg);

// How many times an original's instruction has run in a test, counted up to 255 (lift.py writes this where each one runs). Part 2
// keeps a test that ran one a new number of times: that's how a loop shows it's getting further. Reached is still "not 0".
#define FUZZ_REACHED(count) do { if ((count) != 255) (count)++; } while (0)

// The two sides of a comparison the original makes (lift.py puts this before its branches and set-less-thans), noted while part 2
// notes reads. See input_to_state in fuzz_main.cpp.
// (And which comparison it is, for part 4 -- see Part 4 in fuzz_main.cpp: `site` is the line it is on in the generated
// originals.inc, one per comparison in the code, times 4, plus its kind (lift.py fuzz_compares): 0 equal or not, 1 a signed
// less-than, 2 an unsigned one, 3 not known. A branch's note (FUZZ_BRANCH_AT) also has its test, `op` -- 0 ==, 1 !=, 2 <, 3 >=,
// 4 <=, 5 >, + 8 when signed -- and the numbers in reached[] of the instruction it goes to when its test holds (`taken`) and
// when it doesn't (`fall`), or -1 where lift.py didn't find one.)
void lift_fuzz_note_compare(u32 a, u32 b, u32 site, int op = -1, int taken = -1, int fall = -1);
#define FUZZ_COMPARE_KIND(a, b, kind) \
    do { if (lift_fuzz_noting_reads) lift_fuzz_note_compare((u32)(a), (u32)(b), (u32)__LINE__ * 4 + (kind)); } while (0)
#define FUZZ_BRANCH_AT(a, b, kind, op, taken, fall) \
    do { if (lift_fuzz_noting_reads) lift_fuzz_note_compare((u32)(a), (u32)(b), (u32)__LINE__ * 4 + (kind), (op), (taken), (fall)); } while (0)
#define FUZZ_COMPARE(a, b) FUZZ_COMPARE_KIND(a, b, 3)
// A float comparison (c.eq, c.lt, c.le), for part 4: its two values as single floats' bits (a double is rounded to one: the
// value it was converted from, most often), kind 3, and its test + 16 as `op`.
// (lift_fuzz_note_float_compare also keeps the two values as they are, a double whole.)
void lift_fuzz_note_float_compare(double a, double b, u32 site, int op);
#define FUZZ_FLOAT_COMPARE(a, b, op) \
    do { if (lift_fuzz_noting_reads) lift_fuzz_note_float_compare((a), (b), (u32)__LINE__ * 4 + 3, 16 + (op)); } while (0)

// A NaN going into the original's float arithmetic (the translator's NAN_CHECK, off in a normal build). The N64 stops on a NaN
// operand (an unimplemented-operation exception), so no game state has one, and which NaN x86 hands back depends on the order the
// compiler put an operation's operands in, which the two versions needn't share. While lift_fuzz_nan_stops is set (part 2's
// original runs, fuzz_main.cpp), such a test is no test: it's skipped, as one that ran away is. 2026-11-07: steered inputs reached
// float code with NaNs in it and showed four "differences" that were only NaN bits (func_80022178, func_80034D1C, func_8012CFA0,
// func_8012E434).
// lift_fuzz_nan_noted (2026-11-08): set by any NaN operand, stopping or not -- part 1's original runs don't stop, so the test
// runs on to the end as before, and is then set aside (run_both): 23 functions' "differences" in fuzz/report.txt of
// 2026-11-08 were the NaN bits a float store wrote (func_800111F4: x86 hands back the first operand's NaN, and clang added
// m2c's `a * b + c * d` with its operands the other way round).
extern bool lift_fuzz_nan_stops;
extern bool lift_fuzz_nan_noted;
[[noreturn]] void lift_fuzz_nan_seen();
#undef NAN_CHECK
#define NAN_CHECK(val) do { if ((val) != (val)) { lift_fuzz_nan_noted = true; if (lift_fuzz_nan_stops) lift_fuzz_nan_seen(); } } while (0)
// (2026-11-13, final082. TESTER CHANGE, approved by the user) And a NaN the original's float arithmetic MAKES from operands that
// are numbers: an invalid operation -- 0 / 0, infinity - infinity, 0 x infinity, the square root of a negative. The N64 stops
// there as well (libultra turns the invalid-operation exception on), so no game state goes on with such a NaN, and which NaN
// comes out is the CPU's, not the game's: x86 hands back a negative quiet NaN, ARM64 a positive one (the ARM64 proof's 30
// functions whose only "differences" were NaN signs). lift.py fuzz_original_text puts FUZZ_NAN_MADE after each of the
// originals' float multiplications, divisions, additions, subtractions and square roots; a NaN there is then treated exactly as
// a NaN operand is: noted, the test set aside (and stopped where a NaN operand stops it). Its operands are numbers, or the
// NAN_CHECK before it would already have caught them. FUZZ_NAN_MADE=0 in the environment turns it off, to measure what it sets
// aside; lift_fuzz_nan_made_seen says a test's original made one (counted apart, fuzz_main.cpp).
extern bool lift_fuzz_nan_made_on;
extern bool lift_fuzz_nan_made_seen;
#define FUZZ_NAN_MADE(val) do { if (lift_fuzz_nan_made_on && (val) != (val)) { lift_fuzz_nan_made_seen = true; lift_fuzz_nan_noted = true; if (lift_fuzz_nan_stops) lift_fuzz_nan_seen(); } } while (0)

// The originals' memory access, redefined so what they write is noted, in the same way as the converted code's.
// (The originals' memory accesses are counted here rather than through lift_fuzz_count_access: fuzz_main.cpp's counter, the same count.)
extern u32 lift_fuzz_accesses, lift_fuzz_access_limit;
[[noreturn]] void lift_fuzz_too_long();
inline __attribute__((always_inline)) void lift_fuzz_count_access_fast() {
    if (++lift_fuzz_accesses > lift_fuzz_access_limit) lift_fuzz_too_long();
}
// Device registers (2026-11-10). The N64's hardware registers sit at 0xA4000000-0xA4FFFFFF (the PI's status at 0xA4600010,
// the VI's current line at 0xA4400010, ...). Their value is the device's, not what the game last stored: a function that
// waits for one, `while (PI_STATUS & 3) {}`, relies on it changing under it. As plain memory it never changes, so in a test
// such a loop either isn't entered or never ends, and its instructions were never reached by a compared test (func_8003D250
// and four more, 28 instructions). So a 32-bit READ there is an input, like a called function's result: the n-th read of a
// run gets the n-th value of a sequence that depends only on the test (lift_fuzz_device_read, fuzz_main.cpp), the same for
// the original and the converted function. Both must then make the same reads in the same order to agree, which is what
// "the same behaviour" means for a device. Writes there are noted and compared exactly as before.
// Only for a register the function under test names itself (a `lui` of its page among its constants: lift_fuzz_device_named).
// A random pointer can land in that range too, in a function that has nothing to do with devices; there it stays plain
// memory, and every such function's tests are exactly what they were. (Tried without this, 2026-11-10: six functions
// showed "differences" that were only one version reading a random pointer's field twice where the other read it once.)
u32 lift_fuzz_device_read(u32 offset);
bool lift_fuzz_device_named(u32 offset);
static inline bool lift_fuzz_is_device(u32 offset) { return offset - 0x24000000u < 0x01000000u; }
template <class T> struct FuzzMem {
    uint8_t* rdram;
    uint64_t address;     // As the game's code computes it, sign-extended: 0xFFFFFFFF80000000 and up.
    int flip;
    // (Kept to 32 bits, as the converted code's are. Only a made-up pointer can reach past the top of the address space, and the N64
    // has no memory there, so how the two wrap round isn't a difference that means anything.)
    uint64_t offset() const { return (u32)(((u32)address ^ (u32)flip) - 0x80000000u); }
    operator T() const {
        lift_fuzz_count_access_fast();
        if (sizeof(T) == 4 && __builtin_expect(lift_fuzz_is_device((u32)offset()), 0) && lift_fuzz_device_named((u32)offset())) return (T)lift_fuzz_device_read((u32)offset());
        if (lift_fuzz_noting_reads) lift_fuzz_note_read(offset());    // (part 2 of a test notes what the original's path read)
#if FUZZ_MOD_NATIVE
        if (__builtin_expect(lift_fuzz_watching_mod, 0)) lift_fuzz_note_mod_access(offset());   // (rule (4), 2026-11-13: see fuzz_main.cpp)
#endif
        return *(T*)(rdram + offset());
    }
    const FuzzMem& operator=(T v) const {
        lift_fuzz_note_write(offset(), sizeof(T));
        *(T*)(rdram + offset()) = v;
        return *this;
    }
};
