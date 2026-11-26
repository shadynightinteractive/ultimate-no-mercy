// lift_call.h: how a converted function calls others, and keeps its stack frame, while most of the game is still translated.
//
// A translated function takes its parameters from the N64's registers (and the stack, after the first four) and leaves its result in
// them. So a converted caller has to put them there. lift_call does that, so the code reads almost as a normal call:
//
//     result = lift_call<s32>(ctx, ::func_800E8A14)(2, pointer);
//
// Once the function being called is converted too, the call can go straight to it, with real parameters.
#pragma once
#include <type_traits>
#include <utility>
#include "recomp.h"
#include "lift_mem.h"

// A converted function's stack frame in game memory, the same size and in the same place as the original's. Local variables whose
// address gets passed around, and parameters passed on the stack, have to be real memory that the functions it calls can see.
#ifdef ARCADE_LIFT_FUZZ
void lift_fuzz_note_keep(u32 address, int reg);
#endif
struct LiftFrame {
    recomp_context* ctx;
    u32 size;
    u32 base;
    // The saved registers it kept (keep / keep_double), put back from their slots on the way out as the original's epilogue does
    // (2026-11-11, the user's decision): if the function itself overwrote a slot -- a loop over a local array run past its end --
    // the register comes back as what's in the slot, exactly as in the original. Not ra: the converted function returns in C++.
    // Not sp: set from base and size, as the original's addiu. Without an overwrite the slot holds what was kept, so nothing changes.
    // In every build: the game runs exactly what the random tester proves.
    struct Kept { u32 offset; gpr* reg; uint64_t* pair; };
    mutable Kept kept[24];
    mutable int kept_count = 0;
    LiftFrame(recomp_context* c, u32 bytes) : ctx(c), size(bytes), base((u32)c->r29 - bytes) { c->r29 = (gpr)(int64_t)(s32)base; }
    ~LiftFrame() {
        for (int i = 0; i < kept_count; i++) {
            const Kept& k = kept[i];
            if (k.reg) *k.reg = (gpr)(int64_t)(s32)slot_word(base + k.offset);
            else *k.pair = (uint64_t)slot_word(base + k.offset) << 32 | slot_word(base + k.offset + 4);
        }
        ctx->r29 = (gpr)(int64_t)(s32)(base + size);
    }
    u32 at(u32 offset) const { return base + offset; }
    // A register saved where the original saved it (see lift.py add_context_and_frame), and noted for the way out. The register is
    // taken by reference to know which one it is (a context field); one that isn't (n out of range) is stored and not noted.
    void keep(u32 offset, const gpr& value) const {
        const long n = &value - &ctx->r0;
#ifdef ARCADE_LIFT_FUZZ
        // (the tester is told which register went to which slot: fuzz_main.cpp frame_was_overwritten, 2026-11-10)
        if (n > 0 && n < 32) lift_fuzz_note_keep(base + offset, (int)n);
#endif
        lift_write32(base + offset, (u32)value);
        if (((n >= 16 && n <= 23) || n == 30) && kept_count < 24) kept[kept_count++] = { offset, const_cast<gpr*>(&value), nullptr };
    }
    // A double register saved there (sdc1): its top word first, as the N64 stores it.
    void keep_double(u32 offset, const uint64_t& value) const {
        const long n = (const fpr*)&value - &ctx->f0;
#ifdef ARCADE_LIFT_FUZZ
        if (n >= 0 && n < 32) lift_fuzz_note_keep(base + offset, 32 + (int)n);
#endif
        lift_write32(base + offset, (u32)(value >> 32));
        lift_write32(base + offset + 4, (u32)value);
        if (n >= 20 && n < 32 && kept_count < 24) kept[kept_count++] = { offset, nullptr, const_cast<uint64_t*>(&value) };
    }
private:
    // A slot's word on the way out. In the tester straight from memory, not lift_read32: a destructor mustn't trip the tester's
    // access limit (which throws), and may run while one unwinds. Elsewhere through lift_read32, as every other access.
    static u32 slot_word(u32 a) {
#ifdef ARCADE_LIFT_FUZZ
        return *(u32*)(lift_rdram + (a - 0x80000000u));
#else
        return lift_read32(a);
#endif
    }
};

template <class T> inline T lift_plain(T value) { return value; }
template <class T> inline T lift_plain(Ref<T> value) { return (T)value; }
template <class U> inline P<U> lift_plain(Ref<P<U>> value) { return (P<U>)value; }
// (A frame word moved both as an integer and as a float, lift_mem.h Ref<LiftWord<T>>, passed on: as the type m2c declared it
//  with, which is what m2c's call took it as. 2026-11-12, agent "refused")
template <class T> inline T lift_plain(Ref<LiftWord<T>> value) { return (T)value; }

template <class R> struct LiftCall {
    recomp_context* ctx;
    void (*function)(uint8_t*, recomp_context*);

    // One parameter, into the register or stack slot the N64's calling rules give it. I is its number, S its first argument slot
    // (put_all): the same but for a double, which fills two slots from an even one (2026-11-12, agent "refused2").
    template <size_t I, size_t S, bool ALL_FLOATS_BEFORE, class A> void put(A value) const {
        if constexpr (std::is_floating_point_v<A> && I < 2 && ALL_FLOATS_BEFORE) {
            // Floats go in f12 and f14, but only while nothing before them is an integer. A double goes in the pair, f12:f13 or
            // f14:f15: the even register's 64 bits in the context, its high word the odd register's, as the translated code
            // keeps them (2026-11-12, agent "refused2": libm's frexp and ldexp, func_800325E8 and func_800326C8).
            if constexpr (sizeof(A) == 8) (I == 0 ? ctx->f12 : ctx->f14).d = value;
            else (I == 0 ? ctx->f12 : ctx->f14).fl = value;
        } else if constexpr (std::is_floating_point_v<A> && sizeof(A) == 8) {
            // A double after an integer: its two words where two integers in its place would go, the high word first (the N64
            // is big-endian), from an even slot. (2026-11-12, agent "refused2")
            uint64_t bits;
            std::memcpy(&bits, &value, 8);
            const u32 words[2] = {(u32)(bits >> 32), (u32)bits};
            for (size_t k = 0; k < 2; k++) {
                if (S + k < 4) (&ctx->r4)[S + k] = (gpr)(int64_t)(s32)words[k];
                else lift_write32((u32)ctx->r29 + 4 * (u32)(S + k), words[k]);
            }
        } else if constexpr (std::is_floating_point_v<A>) {
            // Any other float goes where an integer in its place would, as its 32 bits: the N64's calling rules once an integer
            // came before it, or from the third parameter on (func_80146D54 passes func_8000E538 floats in its stack slots 6 to
            // 11). lift.py arg_sources reads a converted function's parameters the same way. (2026-11-12, agent "refused")
            u32 word;
            std::memcpy(&word, &value, 4);
            if constexpr (S < 4) (&ctx->r4)[S] = (gpr)(int64_t)(s32)word;
            else lift_write32((u32)ctx->r29 + 4 * S, word);
        } else {
            u32 word;
            if constexpr (std::is_class_v<A>) word = value.a;
            else { static_assert(sizeof(A) <= 4, "a 64-bit parameter this doesn't handle yet"); word = (u32)value; }
            if constexpr (S < 4) (&ctx->r4)[S] = (gpr)(int64_t)(s32)word;
            else lift_write32((u32)ctx->r29 + 4 * S, word);
        }
    }

    template <size_t... I, class... A> void put_all(std::index_sequence<I...>, A... values) const {
        constexpr bool floats[] = {std::is_floating_point_v<A>..., false};
        constexpr bool doubles[] = {(std::is_floating_point_v<A> && sizeof(A) == 8)..., false};
        constexpr auto all_floats_before = [](size_t count, const bool* f) {
            for (size_t i = 0; i < count; i++) if (!f[i]) return false;
            return true;
        };
        // (Each parameter's first slot: one each, a double two from an even one. With no double that is its number, as before.)
        constexpr auto slot = [](size_t count, const bool* d) {
            size_t s = 0;
            for (size_t i = 0; i < count; i++) s = d[i] ? (s + 1) / 2 * 2 + 2 : s + 1;
            return d[count] ? (s + 1) / 2 * 2 : s;
        };
        (put<I, slot(I, doubles), all_floats_before(I, floats)>(values), ...);
    }

    template <class... A> R operator()(A... values) const {
        put_all(std::index_sequence_for<A...>{}, lift_plain(values)...);
        function(lift_rdram, ctx);
        if constexpr (std::is_void_v<R>) return;
        else if constexpr (std::is_same_v<R, f32>) return ctx->f0.fl;
        else if constexpr (std::is_same_v<R, f64>) return ctx->f0.d;      // (a double result, the pair f0:f1: 2026-11-12, "refused2")
        else if constexpr (std::is_class_v<R>) return R((u32)ctx->r2);
        else return (R)(u32)ctx->r2;
    }
};

template <class R> inline LiftCall<R> lift_call(recomp_context* ctx, void (*function)(uint8_t*, recomp_context*)) { return {ctx, function}; }
