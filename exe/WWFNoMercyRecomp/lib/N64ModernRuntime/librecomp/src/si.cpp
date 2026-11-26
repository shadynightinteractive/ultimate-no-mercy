#include <cstdint>
#include <cstdio>
#include <ultramodern/ultramodern.hpp>
#include "recomp.h"
#include "helpers.hpp"

// The controllers go straight to the PC's input now (src/main/arcade_cont.cpp). All that still comes here is the game's
// Controller Pak and microphone code, in its library until step 4: every channel answers "nothing's plugged in".
static uint8_t blocks[64];

extern "C" void __osSiDeviceBusy_recomp(uint8_t* rdram, recomp_context* ctx) {
    _return<s32>(ctx, 0);
}

extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    const s32 to_chip = (s32)ctx->r4;
    const gpr dram = ctx->r5;
    if (to_chip == 1) {
        static bool said = false;
        if (!said) { said = true; fprintf(stderr, "[si] the game asked after a pak or a microphone: none\n"); }
        for (int i = 0; i < 64; i++) blocks[i] = (uint8_t)MEM_B(i, dram);
        for (int i = 0; i < 62;) {
            const uint8_t sends = blocks[i];
            if (sends == 0xFE) break;
            if (sends == 0x00 || sends == 0xFF || (sends & 0xC0)) { i++; continue; }
            const uint8_t answers = blocks[i + 1] & 0x3F;
            if (i + 2 + sends + answers > 64) break;
            blocks[i + 1] |= 0x80;      // (no device)
            i += 2 + sends + answers;
        }
    }
    else {
        for (int i = 0; i < 64; i++) MEM_B(i, dram) = (int8_t)blocks[i];
    }
    ultramodern::send_si_message();
    _return<s32>(ctx, 0);
}
