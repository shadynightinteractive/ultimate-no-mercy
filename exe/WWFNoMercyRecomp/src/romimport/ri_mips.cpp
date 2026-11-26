// ri_mips.cpp (phase3, 2026-11-19): the MIPS interpreter (ri_mips.h), line for line rig\harness\mipsemu.py.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#include "ri_mips.h"
#include <cmath>

namespace ri {

Emu::Emu(const Bytes& ram) : Emu(ram.data(), ram.size()) {}
Emu::Emu(const uint8_t* ram, size_t n) : m_(SIZE + 8, 0) {
    memcpy(m_.data(), ram, n < SIZE ? n : SIZE);
}

void Emu::wbytes(uint32_t a, const uint8_t* p, size_t n) {
    size_t at = ix(a);
    if (at + n > SIZE) fail("write of %zu bytes at %08x is past the RAM image", n, a);
    memcpy(&m_[at], p, n);
}
Bytes Emu::rbytes(uint32_t a, size_t n) const {
    size_t at = ix(a);
    if (at + n > SIZE) n = at < SIZE ? SIZE - at : 0;      // (Python's slice: what there is)
    return Bytes(m_.begin() + at, m_.begin() + at + n);
}

uint32_t Emu::call(uint32_t fn, const std::vector<uint32_t>& args, const std::vector<uint32_t>& stack_args, uint32_t sp, uint64_t max_steps) {
    for (auto& x : r) x = 0;
    for (size_t i = 0; i < args.size(); i++) r[4 + i] = args[i];
    r[29] = sp;
    for (size_t i = 0; i < stack_args.size(); i++) w32(sp + 0x10 + 4 * (uint32_t)i, stack_args[i]);
    r[31] = RET;
    pc = fn; npc = fn + 4;
    steps = 0;
    while (pc != RET) {
        auto h = hooks.find(pc);
        if (h != hooks.end() && h->second(*this)) { pc = r[31]; npc = pc + 4; continue; }
        step();
        steps++;
        if (steps > max_steps) throw MipsStop(strf("too many steps at %08x", pc));
    }
    return r[2];
}

static float F(uint32_t w) { float v; memcpy(&v, &w, 4); return v; }
static uint32_t W(float v) { uint32_t w; memcpy(&w, &v, 4); return w; }
static bool fcmp(uint32_t fn, double a, double b) {          // c.cond.fmt: cond bits 4 less, 2 equal, 1 unordered
    uint32_t c = fn & 15; bool un = std::isnan(a) || std::isnan(b);
    return (c & 4 && !un && a < b) || (c & 2 && !un && a == b) || (c & 1 && un);
}
// Python's int(x) & 0xFFFFFFFF: toward zero, then the low 32 bits (a NaN or an infinity stops the Python)
static uint32_t to_w(double x, uint32_t pc) {
    if (std::isnan(x) || std::isinf(x)) throw MipsStop(strf("float to int of %f at %08x", x, pc));
    double t = std::trunc(x);
    if (std::fabs(t) < 9.2e18) return (uint32_t)(uint64_t)(int64_t)t;
    return (uint32_t)(uint64_t)(int64_t)std::fmod(t, 18446744073709551616.0);
}

void Emu::step() {
    const uint32_t w = r32(pc);
    const uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31, sh = (w >> 6) & 31, fn = w & 63;
    const uint32_t imm = w & 0xFFFF; const int32_t simm = sx16(imm);
    const uint32_t cur = pc, nn = npc + 4;
    bool has_branch = false; uint32_t branch = 0;
    auto take = [&](uint32_t t) { has_branch = true; branch = t; };
    auto skip_slot = [&]() { pc = nn; npc = nn + 4; r[0] = 0; };          // a likely branch not taken: its delay slot skipped
    auto rel = [&]() { return (uint32_t)(npc + ((uint32_t)simm << 2)); };
    switch (op) {
    case 0:
        switch (fn) {
        case 0: r[rd] = r[rt] << sh; break;
        case 2: r[rd] = r[rt] >> sh; break;
        case 3: r[rd] = (uint32_t)(s32(r[rt]) >> sh); break;
        case 4: r[rd] = r[rt] << (r[rs] & 31); break;
        case 6: r[rd] = r[rt] >> (r[rs] & 31); break;
        case 7: r[rd] = (uint32_t)(s32(r[rt]) >> (r[rs] & 31)); break;
        case 8: take(r[rs]); break;
        case 9: { uint32_t t = r[rs]; r[rd] = npc + 4; take(t); break; }
        case 13: throw MipsStop(strf("break at %08x", cur));
        case 16: r[rd] = hi; break;
        case 17: hi = r[rs]; break;
        case 18: r[rd] = lo; break;
        case 19: lo = r[rs]; break;
        case 24: { int64_t p = (int64_t)s32(r[rs]) * s32(r[rt]); lo = (uint32_t)p; hi = (uint32_t)((uint64_t)p >> 32); break; }
        case 25: { uint64_t p = (uint64_t)r[rs] * r[rt]; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); break; }
        case 26: {
            int64_t a = s32(r[rs]), b = s32(r[rt]);
            if (b) { int64_t q = a / b; lo = (uint32_t)q; hi = (uint32_t)(a - q * b); }
            break;
        }
        case 27: { uint32_t a = r[rs], b = r[rt]; if (b) { lo = a / b; hi = a % b; } break; }
        case 32: case 33: r[rd] = r[rs] + r[rt]; break;
        case 34: case 35: r[rd] = r[rs] - r[rt]; break;
        case 36: r[rd] = r[rs] & r[rt]; break;
        case 37: r[rd] = r[rs] | r[rt]; break;
        case 38: r[rd] = r[rs] ^ r[rt]; break;
        case 39: r[rd] = ~(r[rs] | r[rt]); break;
        case 42: r[rd] = s32(r[rs]) < s32(r[rt]) ? 1 : 0; break;
        case 43: r[rd] = r[rs] < r[rt] ? 1 : 0; break;
        default: throw MipsStop(strf("special %u at %08x", fn, cur));
        }
        break;
    case 1: {
        int32_t t = s32(r[rs]); bool cond;
        if (rt == 0 || rt == 2) cond = t < 0;
        else if (rt == 1 || rt == 3) cond = t >= 0;
        else if (rt == 16 || rt == 18) { cond = t < 0; r[31] = npc + 4; }
        else if (rt == 17 || rt == 19) { cond = t >= 0; r[31] = npc + 4; }
        else throw MipsStop(strf("regimm %u at %08x", rt, cur));
        bool likely = rt == 2 || rt == 3 || rt == 18 || rt == 19;
        if (cond) take(rel());
        else if (likely) { skip_slot(); return; }
        break;
    }
    case 2: take((npc & 0xF0000000u) | ((w & 0x3FFFFFF) << 2)); break;
    case 3: r[31] = npc + 4; take((npc & 0xF0000000u) | ((w & 0x3FFFFFF) << 2)); break;
    case 4: case 5: case 6: case 7: case 20: case 21: case 22: case 23: {
        uint32_t b = op & 3; bool cond;
        if (b == 0) cond = r[rs] == r[rt];
        else if (b == 1) cond = r[rs] != r[rt];
        else if (b == 2) cond = s32(r[rs]) <= 0;
        else cond = s32(r[rs]) > 0;
        bool likely = op >= 20;
        if (cond) take(rel());
        else if (likely) { skip_slot(); return; }
        break;
    }
    case 8: case 9: r[rt] = r[rs] + (uint32_t)simm; break;
    case 10: r[rt] = s32(r[rs]) < simm ? 1 : 0; break;
    case 11: r[rt] = r[rs] < (uint32_t)simm ? 1 : 0; break;
    case 12: r[rt] = r[rs] & imm; break;
    case 13: r[rt] = r[rs] | imm; break;
    case 14: r[rt] = r[rs] ^ imm; break;
    case 15: r[rt] = imm << 16; break;
    case 32: r[rt] = (uint32_t)sx8(r8(r[rs] + simm)); break;
    case 33: r[rt] = (uint32_t)sx16(r16(r[rs] + simm)); break;
    case 35: r[rt] = r32(r[rs] + simm); break;
    case 36: r[rt] = r8(r[rs] + simm); break;
    case 37: r[rt] = r16(r[rs] + simm); break;
    case 40: w8(r[rs] + simm, r[rt]); break;
    case 41: w16(r[rs] + simm, r[rt]); break;
    case 43: w32(r[rs] + simm, r[rt]); break;
    case 34: case 38: {                                              // lwl / lwr
        uint32_t a = r[rs] + simm, word = r32(a & ~3u), k = a & 3;
        if (op == 34) r[rt] = (uint32_t)(((uint64_t)word << (8 * k)) | (r[rt] & ((1u << (8 * k)) - 1)));
        else r[rt] = (word >> (8 * (3 - k))) | (r[rt] & ~(0xFFFFFFFFu >> (8 * (3 - k))));
        break;
    }
    case 42: case 46: {                                              // swl / swr
        uint32_t a = r[rs] + simm, k = a & 3;
        if (op == 42) { for (uint32_t i = 0; i < 4 - k; i++) w8(a + i, r[rt] >> (24 - 8 * i)); }
        else { for (uint32_t i = 0; i <= k; i++) w8(a - i, r[rt] >> (8 * i)); }
        break;
    }
    case 47: break;                                                  // cache
    case 49: f[rt] = r32(r[rs] + simm); break;                        // lwc1
    case 57: w32(r[rs] + simm, f[rt]); break;                         // swc1
    case 53: { uint32_t a = r[rs] + simm; f[rt | 1] = r32(a); f[rt & ~1u] = r32(a + 4); break; }     // ldc1 (FR=0)
    case 61: { uint32_t a = r[rs] + simm; w32(a, f[rt | 1]); w32(a + 4, f[rt & ~1u]); break; }      // sdc1
    case 17: {                                                       // COP1
        const uint32_t fs = rd, fd = sh, ft = rt;
        auto Dg = [&](uint32_t i) { uint64_t v = (uint64_t)f[i | 1] << 32 | f[i & ~1u]; double d; memcpy(&d, &v, 8); return d; };
        auto Ds = [&](uint32_t i, double d) { uint64_t v; memcpy(&v, &d, 8); f[i | 1] = (uint32_t)(v >> 32); f[i & ~1u] = (uint32_t)v; };
        auto S = [&](uint32_t i, double v) { f[i] = W((float)v); };
        if (rs == 0) r[rt] = f[fs];                                   // mfc1
        else if (rs == 4) f[fs] = r[rt];                              // mtc1
        else if (rs == 8) {                                           // bc1f / bc1t (likely)
            bool cond = (rt & 1) ? fcc : !fcc, likely = rt & 2;
            if (cond) take(rel());
            else if (likely) { skip_slot(); return; }
        } else if (rs == 16) {
            double a = F(f[fs]), b = F(f[ft]);
            switch (fn) {
            case 0: S(fd, a + b); break;
            case 1: S(fd, a - b); break;
            case 2: S(fd, a * b); break;
            case 3: S(fd, a / b); break;
            case 6: f[fd] = f[fs]; break;
            case 33: Ds(fd, a); break;                                // cvt.d.s
            case 7: S(fd, -a); break;
            case 5: S(fd, std::fabs(a)); break;
            case 13: case 36: f[fd] = to_w(a, cur); break;            // trunc.w / cvt.w (toward 0)
            default:
                if (fn >= 48) fcc = fcmp(fn, a, b);
                else throw MipsStop(strf("cop1.s %u at %08x", fn, cur));
            }
        } else if (rs == 17) {
            double a = Dg(fs), b = Dg(ft);
            switch (fn) {
            case 0: Ds(fd, a + b); break;
            case 1: Ds(fd, a - b); break;
            case 2: Ds(fd, a * b); break;
            case 3: Ds(fd, a / b); break;
            case 6: { uint32_t lo_ = f[fs & ~1u], hi_ = f[fs | 1]; f[fd & ~1u] = lo_; f[fd | 1] = hi_; break; }
            case 7: Ds(fd, -a); break;
            case 5: Ds(fd, std::fabs(a)); break;
            case 32: S(fd, a); break;                                 // cvt.s.d
            case 13: case 36: f[fd] = to_w(a, cur); break;
            default:
                if (fn >= 48) fcc = fcmp(fn, a, b);
                else throw MipsStop(strf("cop1.d %u at %08x", fn, cur));
            }
        } else if (rs == 20 && fn == 32) S(fd, (double)s32(f[fs]));   // cvt.s.w
        else if (rs == 20 && fn == 33) Ds(fd, (double)s32(f[fs]));    // cvt.d.w
        else throw MipsStop(strf("cop1 %u/%u at %08x", rs, fn, cur));
        break;
    }
    default: throw MipsStop(strf("op %u at %08x (%08x)", op, cur, w));
    }
    r[0] = 0;
    if (has_branch) { pc = npc; npc = branch; }       // the delay slot first
    else { pc = npc; npc = nn; }
}

}  // namespace ri
