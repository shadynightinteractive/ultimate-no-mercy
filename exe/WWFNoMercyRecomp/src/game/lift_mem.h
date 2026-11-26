// lift_mem.h: how code converted from the original game reaches the game's memory.
//
// The game's data is still where the N64 kept it, in big-endian layout inside `rdram`, and its pointers are still 32-bit N64
// addresses, because everything not yet converted expects that. So a converted function can't use real C++ pointers yet.
// P<T> is "a pointer to a T in game memory" and Ref<T> is "the T it points at". They read like ordinary pointers:
//
//     P<s32> p = ...;   *p = 5;   s32 x = p[2];   p += 4;      // Moves 4 bytes: see the note on adding, below.
//
// Once all the code using a piece of data has been converted, that data can become a real C++ structure and these go away.
//
// Built with ARCADE_LIFT_TWIN (the proof build), a converted function runs "on the side": what it writes goes to a list, not
// to memory, and is compared with what the original did. See lift_twin.cpp.
#pragma once
#include <cstdint>
#include <cstring>
#include <type_traits>

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
// m2c's words for "this size, sign unknown".
typedef s32 M2C_UNK;
typedef s8 M2C_UNK8;
typedef s16 M2C_UNK16;
typedef s32 M2C_UNK32;
typedef s64 M2C_UNK64;

extern uint8_t* lift_rdram;   // The start of game memory; address 0x80000000 is byte 0.

#ifdef ARCADE_LIFT_TWIN
// The proof build's versions, in lift_twin.cpp: a byte is read through this function's list of writes.
u8 lift_side_read8(u32 address);
void lift_side_write8(u32 address, u8 value);
inline u8 lift_read8(u32 a) { return lift_side_read8(a); }
inline void lift_write8(u32 a, u8 v) { lift_side_write8(a, v); }
inline u16 lift_read16(u32 a) { return (u16)(lift_read8(a) << 8 | lift_read8(a + 1)); }
inline u32 lift_read32(u32 a) { return (u32)lift_read8(a) << 24 | (u32)lift_read8(a + 1) << 16 | (u32)lift_read8(a + 2) << 8 | lift_read8(a + 3); }
inline void lift_write16(u32 a, u16 v) { lift_write8(a, (u8)(v >> 8)); lift_write8(a + 1, (u8)v); }
inline void lift_write32(u32 a, u32 v) { lift_write16(a, (u16)(v >> 16)); lift_write16(a + 2, (u16)v); }
#elif defined(ARCADE_LIFT_FUZZ)
// The random-input tester's versions (tools/lift/fuzz): the same memory access as the normal build, with each write noted so it can
// be compared and undone, and each access counted, so a loop that random data has made endless can be stopped.
void lift_fuzz_note_write(uint64_t offset, u32 bytes);
void lift_fuzz_count_access();
inline u8 lift_read8(u32 a) { lift_fuzz_count_access(); return lift_rdram[(a ^ 3) - 0x80000000u]; }
inline void lift_write8(u32 a, u8 v) { lift_fuzz_note_write((a ^ 3) - 0x80000000u, 1); lift_rdram[(a ^ 3) - 0x80000000u] = v; }
inline u16 lift_read16(u32 a) { lift_fuzz_count_access(); return *(u16*)(lift_rdram + ((a ^ 2) - 0x80000000u)); }
inline void lift_write16(u32 a, u16 v) { lift_fuzz_note_write((a ^ 2) - 0x80000000u, 2); *(u16*)(lift_rdram + ((a ^ 2) - 0x80000000u)) = v; }
// (2026-11-10: a 32-bit read of a device register, 0xA4000000-0xA4FFFFFF, is an input of the test: see FuzzMem in fuzz.h.)
u32 lift_fuzz_device_read(u32 offset);
bool lift_fuzz_device_named(u32 offset);
inline u32 lift_read32(u32 a) { lift_fuzz_count_access(); if (__builtin_expect(a - 0xA4000000u < 0x01000000u, 0) && lift_fuzz_device_named(a - 0x80000000u)) return lift_fuzz_device_read(a - 0x80000000u); return *(u32*)(lift_rdram + (a - 0x80000000u)); }
inline void lift_write32(u32 a, u32 v) { lift_fuzz_note_write(a - 0x80000000u, 4); *(u32*)(lift_rdram + (a - 0x80000000u)) = v; }
#else
// Game memory holds 32-bit words in host order, so a byte's place within its word is flipped, and a halfword's too. The N64 only
// allows aligned access here (unaligned instructions aren't converted yet), and a word is written whole, as the original did.
inline u8 lift_read8(u32 a) { return lift_rdram[(a ^ 3) - 0x80000000u]; }
inline void lift_write8(u32 a, u8 v) { lift_rdram[(a ^ 3) - 0x80000000u] = v; }
inline u16 lift_read16(u32 a) { return *(u16*)(lift_rdram + ((a ^ 2) - 0x80000000u)); }
inline void lift_write16(u32 a, u16 v) { *(u16*)(lift_rdram + ((a ^ 2) - 0x80000000u)) = v; }
inline u32 lift_read32(u32 a) { return *(u32*)(lift_rdram + (a - 0x80000000u)); }
inline void lift_write32(u32 a, u32 v) { *(u32*)(lift_rdram + (a - 0x80000000u)) = v; }
#endif

template <class T> struct P;
template <class T> struct Ref;

// Reading and writing one value of any size, most significant byte first, as the N64 stored it.
template <class T> inline T lift_load(u32 a);
template <class T> inline void lift_store(u32 a, T v);

template <class T> struct LiftSize { static constexpr u32 value = sizeof(T); };
template <> struct LiftSize<void> { static constexpr u32 value = 1; };          // Arithmetic on a void pointer moves by bytes.
template <class U> struct LiftSize<P<U>> { static constexpr u32 value = 4; };   // A game pointer is 4 bytes, whatever the PC's size.

// A pointer to a T in game memory.
template <class T> struct P {
    u32 a;
    P() : a(0) {}
    P(u32 address) : a(address) {}
    P(s32 address) : a((u32)address) {}
    P(s64 address) : a((u32)address) {}
    P(u64 address) : a((u32)address) {}
    template <class U> P(P<U> other) : a(other.a) {}
    template <class U> P(Ref<U> value) : a((u32)(U)value) {}
    template <class U> P(Ref<P<U>> value) : a(lift_read32(value.a)) {}

    Ref<T> operator*() const { return Ref<T>(a); }
    Ref<T> operator[](s32 i) const { return Ref<T>(a + (u32)i * LiftSize<T>::value); }
    // Adding to a pointer counts BYTES, unlike real C++. m2c writes the number straight from the N64 instruction, so p += 4
    // moves 4 bytes whatever p points at. Only p[i] counts elements.
    P operator+(s32 n) const { return P(a + (u32)n); }
    P operator-(s32 n) const { return P(a - (u32)n); }
    P& operator+=(s32 n) { a += (u32)n; return *this; }
    P& operator-=(s32 n) { a -= (u32)n; return *this; }
    s32 operator-(P other) const { return (s32)(a - other.a); }
    explicit operator bool() const { return a != 0; }
    template <class I, class = std::enable_if_t<std::is_integral_v<I>>> explicit operator I() const { return (I)a; }
    friend bool operator==(P x, P y) { return x.a == y.a; }
    friend bool operator!=(P x, P y) { return x.a != y.a; }
    friend bool operator<(P x, P y) { return x.a < y.a; }
    friend bool operator>(P x, P y) { return x.a > y.a; }
    friend bool operator<=(P x, P y) { return x.a <= y.a; }
    friend bool operator>=(P x, P y) { return x.a >= y.a; }
};

// The value a pointer points at: reading it reads memory, assigning to it writes memory.
template <class T> struct Ref {
    u32 a;
    explicit Ref(u32 address) : a(address) {}
    P<T> operator&() const { return P<T>(a); }
    operator T() const { return lift_load<T>(a); }
    const Ref& operator=(T v) const { lift_store<T>(a, v); return *this; }
    const Ref& operator=(const Ref& other) const { lift_store<T>(a, (T)other); return *this; }
    const Ref& operator+=(T v) const { lift_store<T>(a, (T)((T)*this + v)); return *this; }
    const Ref& operator-=(T v) const { lift_store<T>(a, (T)((T)*this - v)); return *this; }
    const Ref& operator*=(T v) const { lift_store<T>(a, (T)((T)*this * v)); return *this; }
    const Ref& operator/=(T v) const { lift_store<T>(a, (T)((T)*this / v)); return *this; }
    // An address stored into a number (m2c's `*(s32 *)0x800435F0 = var_s1;` with var_s1 a pointer): the address's bits, cut
    // to the size, as sw / sh / sb store the register. (2026-11-07)
    template <class U>
    const Ref& operator=(P<U> v) const { static_assert(std::is_integral_v<T>, "an address stored into a float"); lift_store<T>(a, (T)v.a); return *this; }
    template <class U>
    const Ref& operator=(const Ref<P<U>>& v) const { static_assert(std::is_integral_v<T>, "an address stored into a float"); lift_store<T>(a, (T)lift_read32(v.a)); return *this; }
    const Ref& operator|=(T v) const { lift_store<T>(a, (T)((T)*this | v)); return *this; }
    const Ref& operator&=(T v) const { lift_store<T>(a, (T)((T)*this & v)); return *this; }
    const Ref& operator^=(T v) const { lift_store<T>(a, (T)((T)*this ^ v)); return *this; }
    const Ref& operator<<=(s32 n) const { lift_store<T>(a, (T)((T)*this << n)); return *this; }
    const Ref& operator>>=(s32 n) const { lift_store<T>(a, (T)((T)*this >> n)); return *this; }
    T operator++(int) const { T old = *this; lift_store<T>(a, (T)(old + 1)); return old; }
    T operator--(int) const { T old = *this; lift_store<T>(a, (T)(old - 1)); return old; }
};

// The same when the value is itself a pointer: it can be followed and moved without first being read into a variable.
template <class U> struct Ref<P<U>> {
    u32 a;
    explicit Ref(u32 address) : a(address) {}
    P<P<U>> operator&() const { return P<P<U>>(a); }
    operator P<U>() const { return P<U>(lift_read32(a)); }
    const Ref& operator=(P<U> v) const { lift_write32(a, v.a); return *this; }
    const Ref& operator=(const Ref& other) const { lift_write32(a, lift_read32(other.a)); return *this; }
    Ref<U> operator*() const { return *(P<U>)*this; }
    Ref<U> operator[](s32 i) const { return ((P<U>)*this)[i]; }
    P<U> operator+(s32 n) const { return (P<U>)*this + n; }
    P<U> operator-(s32 n) const { return (P<U>)*this - n; }
    // Moving a pointer kept in memory (a stack local, sp34 += 6): in bytes, as P's own +=. (2026-11-07)
    const Ref& operator+=(s32 n) const { lift_write32(a, lift_read32(a) + (u32)n); return *this; }
    const Ref& operator-=(s32 n) const { lift_write32(a, lift_read32(a) - (u32)n); return *this; }
    explicit operator bool() const { return lift_read32(a) != 0; }
    template <class I, class = std::enable_if_t<std::is_integral_v<I>>> explicit operator I() const { return (I)lift_read32(a); }
    friend bool operator==(Ref x, P<U> y) { return (P<U>)x == y; }
    friend bool operator!=(Ref x, P<U> y) { return (P<U>)x != y; }
};

// A word in the frame that the function moves both as an integer and as a float: lw and sw one time, lwc1 and swc1 another
// (m2c's "compiler-managed" local, declared once with one of the types: func_8012BF50's sp18 and modrep_func_8012CB8C's
// sp10-sp34, floats stored with swc1 and handed on with lw, or read back with lwc1). Ref<LiftWord<T>> is that word as the instructions
// see it: read as an integer it gives its 32 bits (lw), as a float the same bits as a float (lwc1), and an integer or a float
// stored into it is stored as its bits (sw, swc1). So nothing is converted between the two, where an s32 local would turn
// 1.5f into 1. T is the type m2c declared it with, which m2c's C reads it as where it says nothing else (a call's argument:
// lift_call.h lift_plain). Anything else -- arithmetic on it, a comparison -- has no single meaning and does not compile, so
// lift.py refuses the function. lift.py exact_spill_slots declares it. (2026-11-12, agent "refused")
template <class T> struct LiftWord;
template <class T> struct Ref<LiftWord<T>> {
    u32 address;        // (not `a`, as the other Refs: code that takes any Ref's address by its `a` must not compile for this)
    explicit Ref(u32 at) : address(at) {}
    P<T> operator&() const { return P<T>(address); }
    operator s32() const { return (s32)lift_read32(address); }
    operator u32() const { return lift_read32(address); }
    operator f32() const { u32 bits = lift_read32(address); f32 v; std::memcpy(&v, &bits, 4); return v; }
    const Ref& operator=(s32 v) const { lift_write32(address, (u32)v); return *this; }
    const Ref& operator=(u32 v) const { lift_write32(address, v); return *this; }
    const Ref& operator=(f32 v) const { u32 bits; std::memcpy(&bits, &v, 4); lift_write32(address, bits); return *this; }
    template <class U> const Ref& operator=(P<U> v) const { lift_write32(address, v.a); return *this; }
    template <class U> const Ref& operator=(const Ref<LiftWord<U>>& other) const { lift_write32(address, lift_read32(other.address)); return *this; }
    const Ref& operator=(const Ref& other) const { lift_write32(address, lift_read32(other.address)); return *this; }
};
// (PROPOSED 2026-11-14 by agent "reach2", part 2 -- compile-checked on func_801089E8's text only, not yet through the converter
//  or the tester.) A LiftWord beside a FLOAT in arithmetic is the float it holds. On the N64 a word meets a float in
//  arithmetic only after lwc1 moved its bits into a float register (an integer would be converted first, mtc1 + cvt.s.w, which
//  m2c writes as a cast of the integer value), so `sp80 + x` and `*p - sp80` with a float on the other side read the word's
//  bits as a single, converting nothing. func_801089E8's sp80: a float in six instructions, passed on once as a word.
//  Arithmetic on a LiftWord with no float beside it (two LiftWords, a LiftWord and an integer) has none of these and stays
//  ambiguous -- refused, as before.
template <class T> inline f32 operator+(const Ref<LiftWord<T>>& a, f32 b) { return (f32)a + b; }
template <class T> inline f32 operator+(f32 a, const Ref<LiftWord<T>>& b) { return a + (f32)b; }
template <class T> inline f32 operator-(const Ref<LiftWord<T>>& a, f32 b) { return (f32)a - b; }
template <class T> inline f32 operator-(f32 a, const Ref<LiftWord<T>>& b) { return a - (f32)b; }
template <class T> inline f32 operator*(const Ref<LiftWord<T>>& a, f32 b) { return (f32)a * b; }
template <class T> inline f32 operator*(f32 a, const Ref<LiftWord<T>>& b) { return a * (f32)b; }
template <class T> inline f32 operator/(const Ref<LiftWord<T>>& a, f32 b) { return (f32)a / b; }
template <class T> inline f32 operator/(f32 a, const Ref<LiftWord<T>>& b) { return a / (f32)b; }

// Address arithmetic m2c writes with a pointer on either side, or on both: `(arg0 * 4) + (P<s32>)0x80106464`, `temp_a2 +
// M2C_FIELD(arg1, P<P<s16>>, 4)`, a register added to a register (addu) where both held addresses. m2c's + and - on addresses are
// the N64's 32-bit add and subtract, in bytes (as P's own +), wrapping. So: the sum of the two 32-bit values, as a pointer of the
// pointer side's type (the left one's if both are); a difference with a pointer on the right is a number, as the 32-bit
// difference. P + number and Ref<P> + number keep their own operators (exact matches win over these templates). (2026-11-07)
template <class X> struct LiftAddressSide { static constexpr bool pointer = false, number = std::is_integral_v<X> && !std::is_same_v<X, bool>; };
template <class U> struct LiftAddressSide<P<U>> { static constexpr bool pointer = true, number = false; using to = U; };
template <class U> struct LiftAddressSide<Ref<P<U>>> { static constexpr bool pointer = true, number = false; using to = U; };
template <class T> struct LiftAddressSide<Ref<T>> { static constexpr bool pointer = false, number = std::is_integral_v<T>; };
template <class X> inline u32 lift_address_bits(const X& x) { return (u32)x; }   // (a number)
template <class U> inline u32 lift_address_bits(const P<U>& x) { return x.a; }
template <class U> inline u32 lift_address_bits(const Ref<P<U>>& x) { return lift_read32(x.a); }
template <class T> inline u32 lift_address_bits(const Ref<T>& x) { return (u32)(T)x; }
template <class A, class B> constexpr bool lift_mixed_address = (LiftAddressSide<A>::pointer || LiftAddressSide<B>::pointer) &&
    (LiftAddressSide<A>::pointer || LiftAddressSide<A>::number) && (LiftAddressSide<B>::pointer || LiftAddressSide<B>::number);
template <class A, class B, class = std::enable_if_t<lift_mixed_address<A, B>>> inline auto operator+(const A& x, const B& y) {
    if constexpr (LiftAddressSide<A>::pointer) return P<typename LiftAddressSide<A>::to>(lift_address_bits(x) + lift_address_bits(y));
    else return P<typename LiftAddressSide<B>::to>(lift_address_bits(x) + lift_address_bits(y));
}
template <class A, class B, class = std::enable_if_t<lift_mixed_address<A, B>>> inline auto operator-(const A& x, const B& y) {
    if constexpr (LiftAddressSide<A>::pointer && LiftAddressSide<B>::number) return P<typename LiftAddressSide<A>::to>(lift_address_bits(x) - lift_address_bits(y));
    else return (s32)(lift_address_bits(x) - lift_address_bits(y));
}
// Two addresses of different kinds compared for equal or not: m2c's `var_a2 != temp_v1` with var_a2 a P<void> and temp_v1 a
// P<P<void>>, or one of them a pointer read from memory (Ref<P<...>>). beq and bne compare the two registers' 32 bits,
// whatever m2c took them to point at, and so does this. P's own == and != take two of one kind, and each side converts to
// the other's, so C++ found two equally good ones and refused to choose (func_80037C60, func_80037920, func_80037AA4,
// func_80145060 did not compile). Two of one kind still use P's own (a function that isn't a template wins over one that is);
// only an address against an address of another kind comes here. Not < and the others: an order between addresses keeps P's
// own, as before. (2026-11-12)
template <class A, class B> constexpr bool lift_two_address_kinds =
    LiftAddressSide<A>::pointer && LiftAddressSide<B>::pointer && !std::is_same_v<A, B>;
template <class A, class B, class = std::enable_if_t<lift_two_address_kinds<A, B>>> inline bool operator==(const A& x, const B& y) {
    return lift_address_bits(x) == lift_address_bits(y);
}
template <class A, class B, class = std::enable_if_t<lift_two_address_kinds<A, B>>> inline bool operator!=(const A& x, const B& y) {
    return lift_address_bits(x) != lift_address_bits(y);
}

template <class T> inline T lift_load(u32 a) {
    if constexpr (sizeof(T) == 1) {
        return (T)lift_read8(a);
    } else if constexpr (sizeof(T) == 2) {
        return (T)lift_read16(a);
    } else if constexpr (std::is_same_v<T, f32>) {
        u32 bits = lift_read32(a);
        f32 v;
        std::memcpy(&v, &bits, 4);
        return v;
    } else if constexpr (sizeof(T) == 4) {
        return (T)lift_read32(a);
    } else {
        u64 bits = (u64)lift_read32(a) << 32 | lift_read32(a + 4);
        T v;
        std::memcpy(&v, &bits, 8);
        return v;
    }
}

template <class T> inline void lift_store(u32 a, T v) {
    if constexpr (sizeof(T) == 1) {
        lift_write8(a, (u8)v);
    } else if constexpr (sizeof(T) == 2) {
        lift_write16(a, (u16)v);
    } else if constexpr (std::is_same_v<T, f32>) {
        u32 bits;
        std::memcpy(&bits, &v, 4);
        lift_write32(a, bits);
    } else if constexpr (sizeof(T) == 4) {
        lift_write32(a, (u32)v);
    } else {
        u64 bits;
        std::memcpy(&bits, &v, 8);
        lift_write32(a, (u32)(bits >> 32));
        lift_write32(a + 4, (u32)bits);
    }
}

// m2c writes an access to an unknown structure as a field: the type at an offset from a pointer.
#define M2C_FIELD(expr, type_ptr, offset) (*(type_ptr)(P<void>(expr) + (offset)))
// A double put together from two 32-bit halves, m2c's GLUE_F64(high, low): `mtc1 $s7, $f18; mtc1 $s6, $f19`, then a double
// instruction reading f18. In the N64's float registers a double's low word is in the even register and its high word in the
// odd one; each half's 32 bits are taken as they are, a float's bits included (lwc1 into one half). Until now m2c read only the
// even register and took its 32-bit value for the whole double (func_800353C0, func_80035040). (2026-11-12)
template <class X> inline u32 lift_glue_half(const X& x) {
    if constexpr (std::is_same_v<X, f32>) { u32 bits; std::memcpy(&bits, &x, 4); return bits; }
    else return lift_address_bits(x);
}
template <class T> inline u32 lift_glue_half(const Ref<T>& x) { return lift_glue_half((T)x); }
template <class H, class L> inline f64 GLUE_F64(const H& high, const L& low) {
    u64 bits = (u64)lift_glue_half(high) << 32 | lift_glue_half(low);
    f64 v;
    std::memcpy(&v, &bits, 8);
    return v;
}
// A div or divu that no compiler check guards (hand-written code: m2c's --n64-division, lift.py unchecked_divisions), with the
// N64's results where C++'s / and % would stop the PC: by zero the quotient is -1 (1 for a negative number; all ones for divu)
// and the remainder the number itself; the most negative number by -1 gives itself and 0 (the division is made in 64 bits, as
// the translated original's). The same as the random tester's originals (fuzz.h FUZZ_DIV). (2026-11-12, agent "refused")
inline s32 LIFT_DIV(s32 a, s32 b) { return b == 0 ? (a < 0 ? 1 : -1) : (s32)((s64)a / (s64)b); }
inline s32 LIFT_REM(s32 a, s32 b) { return b == 0 ? a : (s32)((s64)a % (s64)b); }
inline u32 LIFT_DIVU(u32 a, u32 b) { return b == 0 ? 0xFFFFFFFFu : a / b; }
inline u32 LIFT_REMU(u32 a, u32 b) { return b == 0 ? a : a % b; }
// A float converted to a word (trunc.w.s, trunc.w.d: m2c's --n64-conversion), with one result on every CPU. In range it is C++'s
// conversion. Out of range, for an infinity or a NaN, C++ has no defined result -- x86 gives 0x80000000 and ARM64 gives the
// nearest end of the range, 0 for a NaN -- and the N64 itself stops (an unimplemented-operation exception), so no game state
// depends on it: the helper gives 0x80000000, what the x86 build has always given. recomp.h's TRUNC_W_S / TRUNC_W_D, the
// translated originals', are the same two lines. (2026-11-13, the ARM64 proof: 121 proved functions gave other results on
// ARM64 for this alone.)
inline s32 LIFT_TRUNC_W_S(f32 x) { return (x >= -2147483648.0f && x < 2147483648.0f) ? (s32)x : (s32)0x80000000; }
inline s32 LIFT_TRUNC_W_D(f64 x) { return (x > -2147483649.0 && x < 2147483648.0) ? (s32)x : (s32)0x80000000; }
// A float compare whose branch must stay a branch (lift.py every_compilers_cpp puts it round a clamp at zero,
// `if (x < 0.0f) { x = 0.0f; }`): clang for ARM64 turns that select into fmax, which gives +0.0 for -0.0 where the compare
// keeps -0.0 (func_800FD6DC: 1,015 of 200,000 tests on ARM64). (2026-11-13, the ARM64 proof)
#define LIFT_BRANCH(c) __builtin_expect(!!(c), 0)
// The top half of a multiplication, which is how the N64's compiler divides by a constant.
inline s32 MULT_HI(s32 a, s32 b) { return (s32)(((s64)a * (s64)b) >> 32); }
inline u32 MULTU_HI(u32 a, u32 b) { return (u32)(((u64)a * (u64)b) >> 32); }

// Taking a value out of an N64 register, as a parameter of the type a converted function wants, and putting a result back.
// The registers hold 32-bit values sign-extended to 64 bits, so a result has to be stored the same way.
template <class T> inline T lift_arg(uint64_t reg) {
    if constexpr (std::is_integral_v<T>) return (T)(u32)reg;
    else return T((u32)reg);
}
template <class T> inline uint64_t lift_ret(T v) {
    if constexpr (std::is_integral_v<T>) return (uint64_t)(int64_t)(s32)v;
    else return (uint64_t)(int64_t)(s32)v.a;
}
