// ri_mips.h (phase3, 2026-11-19): a small MIPS (R4300, integer subset + the FPU's plain arithmetic) interpreter -- the C++
// twin of rig\harness\mipsemu.py, step for step.
//
// Why: the other games build their wrestlers' models, costume tables and rosters with their own code at run time; the
// importer runs that code (the source game's own functions, from the player's ROM) on an 8 MB RAM image made from the ROM,
// instead of re-writing what it does. What the code would ask the console for (a file read, a heap block) is a HOOK: a
// function of ours called in place of the game's at that address, which then returns to the caller.
//
//   Emu e(ram);  e.hooks[0x80003DD4] = [](Emu& e) { ...; return true; };   v0 = e.call(0x8000B048, {a0, a1, a2});
//
// Memory is the image's 8 MB, big-endian, index = address & 0x7FFFFF (KSEG0 / KSEG1 the same RAM). Floats are IEEE single
// and double as the console computes them (+ - * / of two singles rounded once: the same as the Python, which computes in
// double and rounds to single). A thing the Python stops on (an unknown instruction, too many steps, a break) stops here
// too, with the same message.
#pragma once
#include "ri_core.h"
#include <functional>
#include <unordered_map>

namespace ri {

struct MipsStop : Error { using Error::Error; };

class Emu {
public:
    static constexpr uint32_t SIZE = 0x800000;
    static constexpr uint32_t RET = 0x80000000u | 0x7FFFF0;    // a return address nothing executes: the call ends there

    explicit Emu(const Bytes& ram);
    explicit Emu(const uint8_t* ram, size_t n);

    uint32_t r[32] = {};
    uint32_t hi = 0, lo = 0;
    uint32_t f[32] = {};          // FPU registers: raw 32-bit words (a double is a pair: the odd register holds the high word)
    bool fcc = false;
    uint32_t pc = 0, npc = 0;
    uint64_t steps = 0;
    std::unordered_map<uint32_t, std::function<bool(Emu&)>> hooks;   // address -> handled? (then it returns to ra)

    // memory
    static uint32_t ix(uint32_t a) { return a & 0x7FFFFF; }
    uint8_t r8(uint32_t a) const { return m_[ix(a)]; }
    uint16_t r16(uint32_t a) const { return be16(&m_[ix(a)]); }
    uint32_t r32(uint32_t a) const { return be32(&m_[ix(a)]); }
    void w8(uint32_t a, uint32_t v) { m_[ix(a)] = (uint8_t)v; }
    void w16(uint32_t a, uint32_t v) { put_be16(&m_[ix(a)], v); }
    void w32(uint32_t a, uint32_t v) { put_be32(&m_[ix(a)], v); }
    void wbytes(uint32_t a, const uint8_t* p, size_t n);
    void wbytes(uint32_t a, const Bytes& b) { wbytes(a, b.data(), b.size()); }
    Bytes rbytes(uint32_t a, size_t n) const;
    uint8_t* mem() { return m_.data(); }                     // the whole 8 MB image
    const uint8_t* mem() const { return m_.data(); }

    // Runs the function at fn with the arguments in a0..a3 (and stack_args at sp + 0x10 ...) until it returns; -> v0.
    uint32_t call(uint32_t fn, const std::vector<uint32_t>& args = {}, const std::vector<uint32_t>& stack_args = {},
                  uint32_t sp = 0x807FF000, uint64_t max_steps = 5'000'000);
    void step();

private:
    std::vector<uint8_t> m_;      // SIZE + a few bytes of slack (a word read at the very end)
};

}  // namespace ri
