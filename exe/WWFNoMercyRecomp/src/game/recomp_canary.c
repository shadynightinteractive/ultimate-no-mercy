// recomp_canary.c (2026-11-13, final082, after the ARM64 proof): a start-up check that the recompiled game's C was compiled the
// way its float code needs. This file is a source of the RecompiledFuncs library (CMakeLists.txt), so it gets exactly the
// options the game's own translated functions get; lift_canary.cpp calls it at start-up and says in the log if it is wrong.
//
// The check: a * b - c with a = b = 1 + 2^-12 and c their product rounded to a float (1 + 2^-11). Computed as the N64 does --
// the product rounded, then the subtraction -- it is 0. A compiler that fuses the two into one multiply-add (clang does where
// the CPU has the instruction, unless -ffp-contract=off) keeps the product exact and gives 2^-24: the game's float code would
// then give other bits than the N64 in the same way. noinline and volatile inputs, so the compiler can't work it out at build
// time and must compile the expression as it would in a game function.
#include <stdint.h>

__attribute__((noinline)) float recomp_canary_mul_sub(float a, float b, float c) {
    return a * b - c;
}

// 0 when the recompiled game's float code is unfused, as it must be; else the bits of what came back.
uint32_t recomp_canary_fma(void) {
    volatile float a = 1.0f + 0x1p-12f, c = 1.0f + 0x1p-11f;
    const float r = recomp_canary_mul_sub(a, a, c);
    union { float f; uint32_t u; } v;
    v.f = r;
    return r == 0.0f ? 0u : v.u;
}
