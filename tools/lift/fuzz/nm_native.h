// nm_native.h: the mod's C compiled for the PC, reaching game memory only through lift_mem.h (scratch/agents/modnative1,
// 2026-11-12).
//
// The mod's C was written for the N64, where a pointer is a 32-bit address in game memory. On the PC game memory is the
// rdram array (byte-swapped words, lift_mem.h), so the mod's pointers can't be real C++ pointers yet. native.py rewrites
// every pointer type in the mod's (preprocessed) source -- `u8 *p`, `(volatile u32*)a`, `const char* s` -- into NPtr<T>,
// which is "a C pointer into game memory": 4 bytes, an N64 address, arithmetic in elements as in C (unlike lift_mem.h's
// P<T>, which m2c's code moves in bytes). Following one reads and writes through lift_load / lift_store, so the random
// tester and the twin build see every access, and Stage 2's redirect will cover the mod as it covers converted game code.
// That also makes RD8 / RD16 / RD32 / WR*, which arcade.h writes as `*(volatile T*)(a)`, into Ref-style accesses with no
// change to the mod's source.
//
// What it deliberately refuses (a compile error, which native.py counts as a blocker): a host pointer -- a string literal,
// a host array, the address of a local or of a host global -- turning into an NPtr. On the N64 those were addresses in game
// memory another function could read; on the PC they are not, so each needs a view at its N64 address first.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include "lift_mem.h"

template <class T> struct NPtr;
// A structure in game memory, seen through its fields (native.py writes one for each of the mod's structures, after its
// definition: each field an NRef / NArr / nested view at the field's offset -- the same offsets as on the N64, since every
// pointer in the structure is a 4-byte NPtr). p->field, (*p).field and p[i].field read and write the field in game memory.
template <class T> struct NView;

// Bytes an NPtr<T> moves per element: sizeof(T), but a nested NPtr is 4 bytes (as on the N64) and void moves by bytes.
template <class T> struct NSize { static constexpr u32 value = sizeof(T); };
template <> struct NSize<void> { static constexpr u32 value = 1; };
template <class T> struct NSize<NPtr<T>> { static constexpr u32 value = 4; };

// ---- The N64's integer division (2026-11-13, scratch/agents/modnative3) ----
// The mod's compiler puts no check after a div or divu (no teq, no break), so on the N64 a division by zero goes on: the
// quotient is -1 (1 for a negative number; all ones unsigned) and the remainder the number itself, and the most negative
// number by -1 gives itself and 0. On the PC C++'s / and % stop the program there instead (emit_char's `(8u << 10) / cw` with
// cw 0). native.py n64_divisions writes each integer division whose divisor isn't a non-zero literal as nm_div(a, b),
// nm_rem(a, b) (and x /= y, x %= y as nm_div_to(x, y), nm_rem_to(x, y)), which give the N64's results in every case -- the
// same as lift_mem.h's LIFT_DIV and the tester's originals (fuzz.h FUZZ_DIV). The division is made in the type C's usual
// conversions give (int / unsigned is unsigned, as with /), so for any other divisor it is exactly C's.
template <class T> struct NRef;
template <class T> struct NVar;
template <class X> struct NValue { using type = X; };         // (the value a view stands for: NRef<T> and NVar<T> are a T)
template <class T> struct NValue<NRef<T>> { using type = T; };
template <class T> struct NValue<NVar<T>> { using type = T; };
template <class A, class B> using NQuotient = decltype(std::declval<typename NValue<A>::type>() / std::declval<typename NValue<B>::type>());
template <class A, class B> inline NQuotient<A, B> nm_div(A a, B b) {
    using C = NQuotient<A, B>;
    static_assert(std::is_integral_v<C> && sizeof(C) == 4, "nm_div: a 32-bit integer division (a 64-bit one is a library call on the N64: not handled)");
    const C x = (C)(typename NValue<A>::type)a, y = (C)(typename NValue<B>::type)b;
    if (y == 0) {
        if constexpr (std::is_signed_v<C>) return x < 0 ? 1 : -1;
        else return (C)0xFFFFFFFFu;
    }
    if constexpr (std::is_signed_v<C>) {
        if (x == (C)INT32_MIN && y == -1) return x;
    }
    return x / y;
}
template <class A, class B> inline NQuotient<A, B> nm_rem(A a, B b) {
    using C = NQuotient<A, B>;
    static_assert(std::is_integral_v<C> && sizeof(C) == 4, "nm_rem: a 32-bit integer division (a 64-bit one is a library call on the N64: not handled)");
    const C x = (C)(typename NValue<A>::type)a, y = (C)(typename NValue<B>::type)b;
    if (y == 0) return x;
    if constexpr (std::is_signed_v<C>) {
        if (x == (C)INT32_MIN && y == -1) return 0;
    }
    return x % y;
}
template <class X, class B> inline X& nm_div_to(X& x, B b) { x = (X)nm_div(x, b); return x; }
template <class X, class B> inline X& nm_rem_to(X& x, B b) { x = (X)nm_rem(x, b); return x; }

// The value an NPtr points at. Reading converts to T, assigning writes; & gives the NPtr back (C's &*p).
template <class T> struct NRef {
    u32 a;
    explicit NRef(u32 address) : a(address) {}
    NPtr<T> operator&() const;
    operator T() const { return lift_load<T>(a); }
    // (2026-11-13, modnative2) C's cast of an integer it reads into a pointer -- `(u16*)undo[i].at` -- reads it, then makes the
    // pointer, as `(u16*)(u32)x` would.
    template <class U, class V = T, std::enable_if_t<std::is_integral_v<V>, int> = 0> explicit operator NPtr<U>() const;
    const NRef& operator=(T v) const { lift_store<T>(a, v); return *this; }
    const NRef& operator=(const NRef& o) const { lift_store<T>(a, (T)o); return *this; }
    template <class V> const NRef& operator+=(V v) const { lift_store<T>(a, (T)((T)*this + v)); return *this; }
    template <class V> const NRef& operator-=(V v) const { lift_store<T>(a, (T)((T)*this - v)); return *this; }
    template <class V> const NRef& operator*=(V v) const { lift_store<T>(a, (T)((T)*this * v)); return *this; }
    // (the N64's division: nm_div, above. 2026-11-13)
    template <class V> const NRef& operator/=(V v) const {
        if constexpr (std::is_floating_point_v<T> || std::is_floating_point_v<V>) lift_store<T>(a, (T)((T)*this / v));
        else lift_store<T>(a, (T)nm_div((T)*this, v));
        return *this;
    }
    template <class V> const NRef& operator%=(V v) const { lift_store<T>(a, (T)nm_rem((T)*this, v)); return *this; }
    template <class V> const NRef& operator|=(V v) const { lift_store<T>(a, (T)((T)*this | v)); return *this; }
    template <class V> const NRef& operator&=(V v) const { lift_store<T>(a, (T)((T)*this & v)); return *this; }
    template <class V> const NRef& operator^=(V v) const { lift_store<T>(a, (T)((T)*this ^ v)); return *this; }
    template <class V> const NRef& operator<<=(V n) const { lift_store<T>(a, (T)((T)*this << n)); return *this; }
    template <class V> const NRef& operator>>=(V n) const { lift_store<T>(a, (T)((T)*this >> n)); return *this; }
    T operator++(int) const { T old = *this; lift_store<T>(a, (T)(old + 1)); return old; }
    T operator--(int) const { T old = *this; lift_store<T>(a, (T)(old - 1)); return old; }
    const NRef& operator++() const { lift_store<T>(a, (T)((T)*this + 1)); return *this; }
    const NRef& operator--() const { lift_store<T>(a, (T)((T)*this - 1)); return *this; }
};

// The same when the value is itself a pointer (an NPtr kept in game memory): it can be followed and indexed directly.
template <class U> struct NRef<NPtr<U>> {
    u32 a;
    explicit NRef(u32 address) : a(address) {}
    NPtr<NPtr<U>> operator&() const;
    operator NPtr<U>() const;
    const NRef& operator=(NPtr<U> v) const;
    const NRef& operator=(const NRef& o) const { lift_write32(a, lift_read32(o.a)); return *this; }
    NRef<U> operator*() const;
    NRef<U> operator[](s32 i) const;
    NView<U> operator->() const { return NView<U>(lift_read32(a)); }
    explicit operator bool() const { return lift_read32(a) != 0; }
    // (2026-11-13, modnative2) A pointer read and cast to an integer -- `(u32)model->bones`: its address.
    template <class I, std::enable_if_t<std::is_integral_v<I>, int> = 0> explicit operator I() const { return (I)lift_read32(a); }
    // (and to another pointer type, as C casts one)
    template <class V> explicit operator NPtr<V>() const { return NPtr<V>(lift_read32(a)); }
};

// A C pointer into game memory (see the top of the file).
template <class T> struct NPtr {
    u32 a;
    NPtr() : a(0) {}
    // (C lets an integer become a pointer, and the mod does: RD32(desc + 4) is *(volatile u32*)(desc + 4).)
    template <class I, std::enable_if_t<std::is_integral_v<I> || std::is_enum_v<I>, int> = 0> NPtr(I address) : a((u32)address) {}
    NPtr(std::nullptr_t) : a(0) {}
    template <class U> explicit NPtr(NPtr<U> o) : a(o.a) {}  // (another pointer type: only by a cast, as in C)
    NPtr(NPtr<void> o);                                       // (void* converts by itself, as in C)
    template <class U> NPtr(U* host) = delete;                // A host pointer is not an N64 address: see the top.
    NRef<T> operator*() const { return NRef<T>(a); }
    NRef<T> operator[](s32 i) const { return NRef<T>(a + (u32)i * NSize<T>::value); }
    NView<T> operator->() const { return NView<T>(a); }
    NPtr operator+(s32 n) const { return NPtr(a + (u32)n * NSize<T>::value); }
    NPtr operator-(s32 n) const { return NPtr(a - (u32)n * NSize<T>::value); }
    NPtr operator+(u32 n) const { return NPtr(a + n * NSize<T>::value); }
    NPtr operator-(u32 n) const { return NPtr(a - n * NSize<T>::value); }
    s32 operator-(NPtr o) const { return (s32)(a - o.a) / (s32)NSize<T>::value; }
    NPtr& operator+=(s32 n) { a += (u32)n * NSize<T>::value; return *this; }
    NPtr& operator-=(s32 n) { a -= (u32)n * NSize<T>::value; return *this; }
    NPtr& operator++() { a += NSize<T>::value; return *this; }
    NPtr& operator--() { a -= NSize<T>::value; return *this; }
    NPtr operator++(int) { NPtr o = *this; a += NSize<T>::value; return o; }
    NPtr operator--(int) { NPtr o = *this; a -= NSize<T>::value; return o; }
    explicit operator bool() const { return a != 0; }
    bool operator!() const { return a == 0; }
    template <class I, class = std::enable_if_t<std::is_integral_v<I>>> explicit operator I() const { return (I)a; }
    friend bool operator==(NPtr x, NPtr y) { return x.a == y.a; }
    friend bool operator!=(NPtr x, NPtr y) { return x.a != y.a; }
    friend bool operator<(NPtr x, NPtr y) { return x.a < y.a; }
    friend bool operator>(NPtr x, NPtr y) { return x.a > y.a; }
    friend bool operator<=(NPtr x, NPtr y) { return x.a <= y.a; }
    friend bool operator>=(NPtr x, NPtr y) { return x.a >= y.a; }
};
// void*: no element to follow; arithmetic in bytes (GNU C, as the mod's compiler allows); any pointer becomes one.
template <> struct NPtr<void> {
    u32 a;
    NPtr() : a(0) {}
    template <class I, std::enable_if_t<std::is_integral_v<I> || std::is_enum_v<I>, int> = 0> NPtr(I address) : a((u32)address) {}
    NPtr(std::nullptr_t) : a(0) {}
    template <class U> NPtr(NPtr<U> o) : a(o.a) {}
    template <class U> NPtr(U* host) = delete;
    NPtr operator+(s32 n) const { return NPtr(a + (u32)n); }
    NPtr operator-(s32 n) const { return NPtr(a - (u32)n); }
    explicit operator bool() const { return a != 0; }
    bool operator!() const { return a == 0; }
    template <class I, class = std::enable_if_t<std::is_integral_v<I>>> explicit operator I() const { return (I)a; }
    friend bool operator==(NPtr x, NPtr y) { return x.a == y.a; }
    friend bool operator!=(NPtr x, NPtr y) { return x.a != y.a; }
};
template <class T> NPtr<T>::NPtr(NPtr<void> o) : a(o.a) {}
template <class T> NPtr<T> NRef<T>::operator&() const { return NPtr<T>(a); }
template <class T> template <class U, class V, std::enable_if_t<std::is_integral_v<V>, int>> NRef<T>::operator NPtr<U>() const { return NPtr<U>((T)*this); }
template <class U> NPtr<NPtr<U>> NRef<NPtr<U>>::operator&() const { return NPtr<NPtr<U>>(a); }
template <class U> NRef<NPtr<U>>::operator NPtr<U>() const { return NPtr<U>(lift_read32(a)); }
template <class U> const NRef<NPtr<U>>& NRef<NPtr<U>>::operator=(NPtr<U> v) const { lift_write32(a, v.a); return *this; }
template <class U> NRef<U> NRef<NPtr<U>>::operator*() const { return NRef<U>(lift_read32(a)); }
template <class U> NRef<U> NRef<NPtr<U>>::operator[](s32 i) const { return NRef<U>(lift_read32(a) + (u32)i * NSize<U>::value); }
// (integer + pointer, as C allows either way round)
template <class T> inline NPtr<T> operator+(s32 n, NPtr<T> p) { return p + n; }

// A place in the mod's own memory: its section's place, where the loader put it, plus the offset as linked (modrep.py's helper;
// the tester's and the game's define it, as for the mod's converted copies).
u32 lift_mod_address(int section, u32 offset);

// lift_load / lift_store for a pointer kept in memory: its 32-bit address.
template <class T> struct LiftSize<NPtr<T>> { static constexpr u32 value = 4; };

// A global of the mod's that other code can see (its address is taken, or game code names it): it stays at its N64 address
// for the proof, as a view (scratch/agents/modglue's rule: only private data moves to the PC). NVar<T> for a scalar, NArr<T>
// for an array (decays to an NPtr, as an array does in C).
template <class T> struct NVar : NRef<T> {
    explicit NVar(u32 address) : NRef<T>(address) {}
    using NRef<T>::operator=;
};
// (2026-11-13, modnative5: the user's rule (4) of 2026-11-13) A global array's view knows its size -- mod.map's, in bytes
// (native.py bind_globals), 0 where it isn't known -- and an index past its end is told to the tester (nm_outside, below)
// before the access is made exactly as before: on the N64 such an access lands on the next object in memory, and when that
// is one of the mod's private objects -- whose N64 place the native side never uses -- the tester may set the test aside,
// only if the original's run reached that same place too (fuzz_main.cpp, "Outside an object"). Nothing else changes.
#if defined(ARCADE_LIFT_FUZZ)
void lift_fuzz_note_outside(u32 address, u32 bytes);
inline void nm_outside(u32 address, u32 bytes) { lift_fuzz_note_outside(address, bytes); }
#else
inline void nm_outside(u32, u32) {}
#endif
template <class T> struct NArr {
    u32 a;
    u32 bytes = 0;      // (its size on the N64, 0 if not known: only for telling the tester of an index past its end)
    explicit NArr(u32 address) : a(address) {}
    NArr(u32 address, u32 size) : a(address), bytes(size) {}
    template <class I, class = std::enable_if_t<std::is_integral_v<I>>> explicit operator I() const { return (I)a; }
    NRef<T> operator[](s32 i) const {
        if (bytes != 0 && (uint64_t)(u32)i * NSize<T>::value >= bytes) nm_outside(a + (u32)i * NSize<T>::value, NSize<T>::value);
        return NRef<T>(a + (u32)i * NSize<T>::value);
    }
    operator NPtr<T>() const { return NPtr<T>(a); }
    template <class V = T, std::enable_if_t<!std::is_void_v<V>, int> = 0> operator NPtr<void>() const { return NPtr<void>(a); }
    NPtr<T> operator+(s32 n) const { return NPtr<T>(a) + n; }
    NRef<T> operator*() const { return NRef<T>(a); }
};

// (2026-11-13, modnative2) An array whose elements are arrays -- a multi-dimensional global (`float m[4][4]`, NArr<float[4]>), a
// field that is one, or an array field of an array view: the element is itself an array view, NRef<T[N]>, so a[i][j] reaches
// the same byte as on the N64. Its size is N elements (NSize, as sizeof: 4 for each NPtr).
template <class T, size_t N> struct NSize<T[N]> { static constexpr u32 value = (u32)N * NSize<T>::value; };
template <class T, size_t N> struct NRef<T[N]> : NArr<T> {
    explicit NRef(u32 address) : NArr<T>(address) {}
    NPtr<T[N]> operator&() const { return NPtr<T[N]>(this->a); }
};

// (2026-11-13, modnative2) A structure in game memory assigned whole (native.py struct_views' NRef operator=): from another,
// its bytes (all read first, so an overlap copies as memmove does); from a PC value, each field (an array element by element).
inline void nm_copy_bytes(u32 to, u32 from, u32 n) {
    u8 buf[4096];
    for (u32 i = 0; i < n && i < sizeof(buf); i++) buf[i] = lift_load<u8>(from + i);
    for (u32 i = 0; i < n && i < sizeof(buf); i++) lift_store<u8>(to + i, buf[i]);
}
template <class F, std::enable_if_t<!std::is_array_v<F>, int> = 0> inline void nm_assign(const NRef<F>& to, const F& v) { to = v; }
template <class F, size_t N> inline void nm_assign(const NArr<F>& to, const F (&v)[N]) {
    for (size_t i = 0; i < N; i++) nm_assign(to[(s32)i], v[i]);
}

// (2026-11-13, modnative2) `sizeof` of an expression, as the N64 has it (native.py rewrite_sizeof writes
// NSizeOf<decltype(expr)>::value): what a view stands for, not the view -- an NRef / NVar is the size of its value, an NPtr 4,
// a PC value its own size. An array view's whole size isn't known from its type (NArr has no length), so that one is refused.
template <class X> struct NSizeOf { static constexpr size_t value = sizeof(X); };
template <class T> struct NSizeOf<NRef<T>> { static constexpr size_t value = NSize<T>::value; };
template <class T> struct NSizeOf<NVar<T>> { static constexpr size_t value = NSize<T>::value; };
template <class T> struct NSizeOf<NView<T>> { static constexpr size_t value = NSize<T>::value; };
template <class T> struct NSizeOf<NArr<T>> { static_assert(sizeof(T) == 0, "sizeof of an array view: its length isn't known"); };

// A structure's field in a view (NView): a value's NRef, an array's NArr (its elements arrays again if it has more dimensions:
// NRef<T[N]>), a nested structure's own view.
template <class F> struct NFieldOf { using type = NRef<F>; };
template <class F, size_t N> struct NFieldOf<F[N]> { using type = NArr<F>; };
// (The mod's structures as the PC compiles them: the views take each field's offset from the PC's layout, which is the N64's
//  because every pointer field is a 4-byte NPtr; native.py writes NM_SELF:: before the structure's name.)
#ifndef NM_SELF
#define NM_SELF
#endif

// (2026-11-13, modnative2) The frame of the native function under way, as its entry made it (lift_call.h LiftFrame: the
// original's size, at the original's place): a local the function hands on lives there, at the original's offset (native.py
// frame_locals), so the address it hands on is the one the original hands on.
// (2026-11-13, modnative5: thread_local in the game, step E -- each N64 thread the game runs is a thread of its own on the PC,
//  and a native function may be under way in more than one; the tester has one thread.)
#if defined(ARCADE_LIFT_FUZZ)
inline u32 nm_frame_base = 0;
#else
inline thread_local u32 nm_frame_base = 0;
#endif

// The context a native mod function's calls go through while the game around it is still translated (lift_call.h): the entry
// that took the N64's registers (prove_gen.py's modrep_nat_<A>, later the generated glue) sets it, and each call the original
// makes -- to a game function, an import, or another of the mod's functions -- goes through it by name, so in a test it is
// the tester's stand-in, and in the game whatever the loader bound. (Only where recomp.h is there: the PC compile's census
// checks the C alone.)
#if __has_include("recomp.h")
#include "lift_call.h"
#if defined(ARCADE_LIFT_FUZZ)
inline recomp_context* nm_ctx = nullptr;
#else
inline thread_local recomp_context* nm_ctx = nullptr;     // (step E: per thread, as nm_frame_base)
#endif
#endif

// ---- A variable the mod's compiler shrank to one byte (2026-11-13, modnative5: the user's rule (2) of 2026-11-13) ----
// LLVM shrinks an internal variable that is only ever given its initial value or ONE other value to a boolean byte: 0 holds
// the initial value, 1 the other (GlobalOpt; shrunk.py finds them and the two values in the mod's own IR). mod.map then has a
// byte where the C has an int. The native code keeps the C's type and reads and writes the byte the same way, through this
// proxy: `walls_done = 1` stores 1, reading gives `byte ? OTHER : INITIAL`. (Only the private ones: the tester's PC object is
// that byte, which the tester fills with 0 or 1 only, as no game state has anything else there.)
template <class T, long long INITIAL, long long OTHER> struct NShrunk {
    u8* byte;
    explicit NShrunk(u8* b) : byte(b) {}
    operator T() const { return *byte ? (T)OTHER : (T)INITIAL; }
    NShrunk& operator=(T v) { *byte = v == (T)INITIAL ? 0 : 1; return *this; }
    NShrunk& operator=(const NShrunk& o) { return *this = (T)o; }
    template <class V> NShrunk& operator+=(V v) { return *this = (T)((T)*this + v); }
    template <class V> NShrunk& operator-=(V v) { return *this = (T)((T)*this - v); }
    template <class V> NShrunk& operator|=(V v) { return *this = (T)((T)*this | v); }
    template <class V> NShrunk& operator&=(V v) { return *this = (T)((T)*this & v); }
    template <class V> NShrunk& operator^=(V v) { return *this = (T)((T)*this ^ v); }
    NShrunk& operator++() { return *this = (T)((T)*this + 1); }
    NShrunk& operator--() { return *this = (T)((T)*this - 1); }
    T operator++(int) { T o = *this; *this = (T)(o + 1); return o; }
    T operator--(int) { T o = *this; *this = (T)(o - 1); return o; }
};

// ---- Loops (2026-11-13, modnative5: the user's rule (3) of 2026-11-13, "a register-only-loop budget") ----
// A loop with no memory access in it never meets the tester's access limit: qa_sin's `while (a > pi) a -= 2 pi` with a random
// 1e30 never ends (a - 2 pi is a). In the test build each pass of each loop of the native code counts (native.py writes NM_TICK
// into every for / while / do condition), as each backward jump of the original does (lift.py), against a budget 64 times the
// access limit: past it the run stops as one that ran past the access limit does -- a test both versions run away in is not
// compared; never a difference by itself. In the game it is nothing.
#if defined(ARCADE_LIFT_FUZZ)
void lift_fuzz_loop_tick();
#define NM_TICK() lift_fuzz_loop_tick()
#else
#define NM_TICK() ((void)0)
#endif

// ---- An array the mod's compiler split into one variable per element (2026-11-13, modnative5) ----
// LLVM's global optimiser may replace a private array only ever indexed by constants (after unrolling) by one variable per
// element, each a symbol of its own in mod.map (`static u8 pad_coin_latch[2]` is pad_coin_latch.0 and pad_coin_latch.1, a
// word apart). The native code keeps the C's array; this binds its elements to those variables' PC objects (the tester's
// private table has each). An index past the end -- which the original, having no array, cannot even make -- is told to the
// tester and reads element 0.
// (An element the original has no variable for -- the compiler dropped it: never read, or only ever its initial 0 -- is a
//  zero word of the native side's own, nm_split_none: what is written there nothing reads, as in the original.)
inline u8* nm_split_none() { alignas(8) static u8 none[8][8] = {}; static int k = 0; return none[k++ & 7]; }
template <class T, size_t N> struct NSplit {
    u8* e[N];
    T& operator[](s32 i) const {
        if ((u32)i >= N) { nm_outside(0, 0); i = 0; }
        return *reinterpret_cast<T*>(e[i]);
    }
};

// ---- A compound literal stored whole (2026-11-13, modnative5: the user's rule (6) of 2026-11-13) ----
// `a[n++] = (MI){ ... }` on the N64 is either the fields stored one by one -- the padding between them left as it was -- or
// the whole literal copied from a constant, its padding included as zeros: the mod's compiler chooses per site
// (compound_pads.json: page_items' PG / ACT sites copy it whole, its V sites store fields). The native code does the same: at a
// site the original copies whole, the element's bytes are all zeroed first and the fields stored over them, so the padding ends
// as the original's does. (The lvalue is evaluated once, as in the assignment.)
template <class T> inline const NRef<T>& nm_assign_whole(const NRef<T>& to, const T& v) {
    for (u32 i = 0; i < NSize<T>::value; i++) lift_store<u8>(to.nm_at + i, 0);
    to = v;
    return to;
}
