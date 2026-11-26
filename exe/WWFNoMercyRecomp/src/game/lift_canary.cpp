// lift_canary.cpp (2026-11-13, final082, after the ARM64 proof): start-up checks that this build compiled the game's code the
// way the proofs need, said loudly in the log if not. main.cpp calls lift_canaries() once, before the game starts.
//
// This file is compiled with exactly lifted.cpp's options (CMakeLists.txt): -fwrapv, -fno-strict-aliasing, -ffp-contract=off.
// recomp_canary.c, a source of the RecompiledFuncs library, is compiled with the translated game's. Each check is a noinline
// function given volatile inputs, so the compiler can't work it out at build time and compiles it as it would a game function:
//   wrap  x + 1 > x for the largest word. The N64 (and C++ with -fwrapv) wraps round to the smallest, so it is false; without
//         -fwrapv a compiler may take it as always true, and the converted functions' overflows -- which lifted.cpp relies on
//         being the N64's -- would not be.
//   fma   a * b - c with a = b = 1 + 2^-12, c = 1 + 2^-11: 0 when the product is rounded first, as on the N64; 2^-24 if the
//         compiler fused them into one multiply-add (ARM64, the Mac, x86 with FMA, without -ffp-contract=off). Here for
//         lifted.cpp's options and in recomp_canary.c for the translated game's.
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" uint32_t recomp_canary_fma(void);

namespace {
__attribute__((noinline)) bool lift_canary_wraps(int32_t x) { return x + 1 > x; }
__attribute__((noinline)) float lift_canary_mul_sub(float a, float b, float c) { return a * b - c; }
}

extern "C" void lift_canaries() {
    volatile int32_t biggest = INT32_MAX;
    volatile float a = 1.0f + 0x1p-12f, c = 1.0f + 0x1p-11f;
    const bool wrap_ok = !lift_canary_wraps(biggest);
    const float fma = lift_canary_mul_sub(a, a, c);
    const uint32_t recomp_fma = recomp_canary_fma();
    if (wrap_ok && fma == 0.0f && recomp_fma == 0) {
        std::fprintf(stderr, "[canary] build ok: the converted code wraps on overflow (-fwrapv) and no float multiply-add is fused"
                             " (-ffp-contract=off), in lifted.cpp's code and the translated game's\n");
    } else {
        uint32_t fma_bits;
        std::memcpy(&fma_bits, &fma, sizeof fma_bits);
        std::fprintf(stderr, "[canary] ********************************************************************************\n");
        std::fprintf(stderr, "[canary] THIS BUILD IS WRONG: the game's code will not compute what the N64 computes.\n");
        if (!wrap_ok) std::fprintf(stderr, "[canary]   lifted.cpp's code doesn't wrap on overflow: build it with -fwrapv (CMakeLists.txt)\n");
        if (fma != 0.0f) std::fprintf(stderr, "[canary]   lifted.cpp's float code fuses a multiply-add (0x%08X, not 0): build it with -ffp-contract=off\n", fma_bits);
        if (recomp_fma) std::fprintf(stderr, "[canary]   the translated game's float code fuses a multiply-add (0x%08X, not 0): build RecompiledFuncs and the mod's Stage 1 with -ffp-contract=off\n", recomp_fma);
        std::fprintf(stderr, "[canary] ********************************************************************************\n");
    }
    std::fflush(stderr);
}
