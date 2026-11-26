"""A small MIPS (R4300, integer subset) interpreter for running the game's own functions offline on a
RAM image (8 MB, big-endian, index = address & 0x7FFFFF). Used to unpack No Mercy Plus's movesets and
looks with No Mercy Plus's own code, and the game's with the game's.

    E = Emu(ram_bytes)
    E.call(0x80005EFC, [a0, a1, a2, a3], stack_args=[...]) -> v0
"""
import struct

M32 = 0xFFFFFFFF
def s32(v): v &= M32; return v - (1 << 32) if v & 0x80000000 else v
def sx16(v): v &= 0xFFFF; return v - 0x10000 if v & 0x8000 else v

class Stop(Exception): pass
def fcmp(fn, a, b):       # c.cond.fmt: cond bits 4 less, 2 equal, 1 unordered
    import math
    c = fn & 15; un = math.isnan(a) or math.isnan(b)
    return bool((c & 4 and not un and a < b) or (c & 2 and not un and a == b) or (c & 1 and un))

class Emu:
    RET = 0x80000000 | 0x7FFFF0     # a return address nothing executes: the call ends there
    def __init__(self, ram, size=0x800000):
        self.m = bytearray(ram[:size]) + bytearray(max(0, size - len(ram)))
        self.r = [0] * 32
        self.hi = self.lo = 0
        self.steps = 0
        self.hooks = {}            # address -> fn(emu) -> True if it handled the call (returns to ra)
        self.f = [0] * 32          # FPU registers (single precision only: raw 32-bit words)
        self.fcc = False
    # memory
    def _a(self, a): return a & 0x7FFFFF
    def r8(self, a): return self.m[self._a(a)]
    def r16(self, a): a = self._a(a); return (self.m[a] << 8) | self.m[a + 1]
    def r32(self, a): a = self._a(a); return struct.unpack_from(">I", self.m, a)[0]
    def w8(self, a, v): self.m[self._a(a)] = v & 0xFF
    def w16(self, a, v): a = self._a(a); self.m[a] = (v >> 8) & 0xFF; self.m[a + 1] = v & 0xFF
    def w32(self, a, v): struct.pack_into(">I", self.m, self._a(a), v & M32)
    def wbytes(self, a, b): a = self._a(a); self.m[a:a + len(b)] = b
    def rbytes(self, a, n): a = self._a(a); return bytes(self.m[a:a + n])

    def call(self, fn, args=(), stack_args=(), sp=0x807FF000, max_steps=5_000_000):
        r = self.r
        for i in range(32): r[i] = 0
        for i, v in enumerate(args): r[4 + i] = v & M32
        r[29] = sp
        for i, v in enumerate(stack_args): self.w32(sp + 0x10 + 4 * i, v)
        r[31] = self.RET
        self.pc = fn; self.npc = fn + 4
        self.steps = 0
        while self.pc != self.RET:
            if self.pc in self.hooks and self.hooks[self.pc](self):
                self.pc = r[31]; self.npc = self.pc + 4; continue
            self.step()
            self.steps += 1
            if self.steps > max_steps: raise Stop("too many steps at %08x" % self.pc)
        return r[2]

    def step(self):
        r = self.r
        pc = self.pc
        w = self.r32(pc)
        op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31
        sh = (w >> 6) & 31; fn = w & 63; imm = w & 0xFFFF; simm = sx16(imm)
        npc = self.npc; nnpc = npc + 4
        branch = None; likely = False
        if op == 0:
            if fn == 0: r[rd] = (r[rt] << sh) & M32
            elif fn == 2: r[rd] = r[rt] >> sh
            elif fn == 3: r[rd] = (s32(r[rt]) >> sh) & M32
            elif fn == 4: r[rd] = (r[rt] << (r[rs] & 31)) & M32
            elif fn == 6: r[rd] = r[rt] >> (r[rs] & 31)
            elif fn == 7: r[rd] = (s32(r[rt]) >> (r[rs] & 31)) & M32
            elif fn == 8: branch = r[rs]
            elif fn == 9: r[rd] = npc + 4; branch = r[rs]
            elif fn == 13: raise Stop("break at %08x" % pc)
            elif fn == 16: r[rd] = self.hi
            elif fn == 17: self.hi = r[rs]
            elif fn == 18: r[rd] = self.lo
            elif fn == 19: self.lo = r[rs]
            elif fn == 24:
                p = s32(r[rs]) * s32(r[rt]); self.lo = p & M32; self.hi = (p >> 32) & M32
            elif fn == 25:
                p = r[rs] * r[rt]; self.lo = p & M32; self.hi = (p >> 32) & M32
            elif fn == 26:
                a, b = s32(r[rs]), s32(r[rt])
                if b: q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1); self.lo = q & M32; self.hi = (a - q * b) & M32
            elif fn == 27:
                a, b = r[rs], r[rt]
                if b: self.lo = (a // b) & M32; self.hi = (a % b) & M32
            elif fn in (32, 33): r[rd] = (r[rs] + r[rt]) & M32
            elif fn in (34, 35): r[rd] = (r[rs] - r[rt]) & M32
            elif fn == 36: r[rd] = r[rs] & r[rt]
            elif fn == 37: r[rd] = r[rs] | r[rt]
            elif fn == 38: r[rd] = r[rs] ^ r[rt]
            elif fn == 39: r[rd] = ~(r[rs] | r[rt]) & M32
            elif fn == 42: r[rd] = 1 if s32(r[rs]) < s32(r[rt]) else 0
            elif fn == 43: r[rd] = 1 if r[rs] < r[rt] else 0
            else: raise Stop("special %d at %08x" % (fn, pc))
        elif op == 1:
            t = s32(r[rs]); cond = None
            if rt in (0, 2): cond = t < 0
            elif rt in (1, 3): cond = t >= 0
            elif rt in (16, 18): cond = t < 0; r[31] = npc + 4
            elif rt in (17, 19): cond = t >= 0; r[31] = npc + 4
            else: raise Stop("regimm %d at %08x" % (rt, pc))
            likely = rt in (2, 3, 18, 19)
            if cond: branch = (npc + (simm << 2)) & M32
            elif likely: self.pc = nnpc; self.npc = nnpc + 4; r[0] = 0; return
        elif op == 2: branch = (npc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        elif op == 3: r[31] = npc + 4; branch = (npc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
        elif op in (4, 5, 6, 7, 20, 21, 22, 23):
            b = op & 3
            if b == 0: cond = r[rs] == r[rt]
            elif b == 1: cond = r[rs] != r[rt]
            elif b == 2: cond = s32(r[rs]) <= 0
            else: cond = s32(r[rs]) > 0
            likely = op >= 20
            if cond: branch = (npc + (simm << 2)) & M32
            elif likely: self.pc = nnpc; self.npc = nnpc + 4; r[0] = 0; return
        elif op in (8, 9): r[rt] = (r[rs] + simm) & M32
        elif op == 10: r[rt] = 1 if s32(r[rs]) < simm else 0
        elif op == 11: r[rt] = 1 if r[rs] < (simm & M32) else 0
        elif op == 12: r[rt] = r[rs] & imm
        elif op == 13: r[rt] = r[rs] | imm
        elif op == 14: r[rt] = r[rs] ^ imm
        elif op == 15: r[rt] = (imm << 16) & M32
        elif op == 32: v = self.r8(r[rs] + simm); r[rt] = (v - 256 if v & 0x80 else v) & M32
        elif op == 33: r[rt] = sx16(self.r16(r[rs] + simm)) & M32
        elif op == 35: r[rt] = self.r32(r[rs] + simm)
        elif op == 36: r[rt] = self.r8(r[rs] + simm)
        elif op == 37: r[rt] = self.r16(r[rs] + simm)
        elif op == 40: self.w8(r[rs] + simm, r[rt])
        elif op == 41: self.w16(r[rs] + simm, r[rt])
        elif op == 43: self.w32(r[rs] + simm, r[rt])
        elif op == 34 or op == 38:      # lwl / lwr
            a = (r[rs] + simm) & M32; al = a & ~3; word = self.r32(al); k = a & 3
            if op == 34: r[rt] = ((word << (8 * k)) | (r[rt] & ((1 << (8 * k)) - 1))) & M32
            else: r[rt] = ((word >> (8 * (3 - k))) | (r[rt] & ~(M32 >> (8 * (3 - k))))) & M32
        elif op == 42 or op == 46:      # swl / swr
            a = (r[rs] + simm) & M32; k = a & 3
            if op == 42:
                for i in range(4 - k): self.w8(a + i, r[rt] >> (24 - 8 * i))
            else:
                for i in range(k + 1): self.w8(a - i, r[rt] >> (8 * i))
        elif op == 47: pass            # cache
        elif op == 49: self.f[rt] = self.r32(r[rs] + simm)                 # lwc1
        elif op == 57: self.w32(r[rs] + simm, self.f[rt])                  # swc1
        elif op == 53:                                                      # ldc1 (FR=0: a pair, odd = high word)
            a = r[rs] + simm; self.f[rt | 1] = self.r32(a); self.f[rt & ~1] = self.r32(a + 4)
        elif op == 61:                                                      # sdc1
            a = r[rs] + simm; self.w32(a, self.f[rt | 1]); self.w32(a + 4, self.f[rt & ~1])
        elif op == 17:                 # COP1 (single precision and word conversions)
            fs, fd, ft = rd, sh, rt
            F = lambda i: struct.unpack(">f", struct.pack(">I", self.f[i]))[0]
            def S(i, v): self.f[i] = struct.unpack(">I", struct.pack(">f", v))[0]
            Dg = lambda i: struct.unpack(">d", struct.pack(">II", self.f[i | 1], self.f[i & ~1]))[0]
            def Ds(i, v): hi, lo = struct.unpack(">II", struct.pack(">d", v)); self.f[i | 1] = hi; self.f[i & ~1] = lo
            if rs == 0: r[rt] = self.f[fs]                                 # mfc1
            elif rs == 4: self.f[fs] = r[rt]                               # mtc1
            elif rs == 8:                                                  # bc1f/bc1t(l)
                cond = self.fcc if rt & 1 else not self.fcc
                likely = bool(rt & 2)
                if cond: branch = (npc + (simm << 2)) & M32
                elif likely: self.pc = nnpc; self.npc = nnpc + 4; r[0] = 0; return
            elif rs == 16:
                a, b = F(fs), F(ft)
                if fn == 0: S(fd, a + b)
                elif fn == 1: S(fd, a - b)
                elif fn == 2: S(fd, a * b)
                elif fn == 3: S(fd, a / b)
                elif fn == 6: self.f[fd] = self.f[fs]
                elif fn == 33: Ds(fd, a)                                             # cvt.d.s
                elif fn == 7: S(fd, -a)
                elif fn == 5: S(fd, abs(a))
                elif fn in (13, 36): self.f[fd] = int(a) & M32                      # trunc.w / cvt.w (toward 0)
                elif fn >= 48: self.fcc = fcmp(fn, a, b)
                else: raise Stop("cop1.s %d at %08x" % (fn, pc))
            elif rs == 17:
                a, b = Dg(fs), Dg(ft)
                if fn == 0: Ds(fd, a + b)
                elif fn == 1: Ds(fd, a - b)
                elif fn == 2: Ds(fd, a * b)
                elif fn == 3: Ds(fd, a / b)
                elif fn == 6: self.f[fd & ~1], self.f[fd | 1] = self.f[fs & ~1], self.f[fs | 1]
                elif fn == 7: Ds(fd, -a)
                elif fn == 5: Ds(fd, abs(a))
                elif fn == 32: S(fd, a)                                              # cvt.s.d
                elif fn in (13, 36): self.f[fd] = int(a) & M32
                elif fn >= 48: self.fcc = fcmp(fn, a, b)
                else: raise Stop("cop1.d %d at %08x" % (fn, pc))
            elif rs == 20 and fn == 32: S(fd, float(s32(self.f[fs])))      # cvt.s.w
            elif rs == 20 and fn == 33: Ds(fd, float(s32(self.f[fs])))     # cvt.d.w
            else: raise Stop("cop1 %d/%d at %08x" % (rs, fn, pc))
        else: raise Stop("op %d at %08x (%08x)" % (op, pc, w))
        r[0] = 0
        if branch is not None:
            # the delay slot
            self.pc = npc; self.npc = branch
        else:
            self.pc = npc; self.npc = nnpc
