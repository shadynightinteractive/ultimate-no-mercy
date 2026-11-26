"""mipsemu with taint tracking: which loads use an address computed from marked bytes (a table
looked up by a value), and which calls get marked arguments. Labels are bit masks."""
import struct
from mipsemu import Emu, Stop, s32, sx16, M32

class TaintEmu(Emu):
    def __init__(self, ram):
        super().__init__(ram)
        self.mt = {}               # memory taint: address (& 0x7FFFFF) -> mask
        self.rt = [0] * 32         # register taint
        self.ht = self.lt = 0
        self.log = []              # (pc, kind, addr, mask)
    def mark(self, a, n, mask):
        for i in range(n): self.mt[(a + i) & 0x7FFFFF] = mask
    def _mtaint(self, a, n):
        m = 0
        for i in range(n): m |= self.mt.get((a + i) & 0x7FFFFF, 0)
        return m
    def call(self, fn, args=(), **kw):
        self.rt = [0] * 32
        return super().call(fn, args, **kw)
    def step(self):
        r = self.r; t = self.rt
        pc = self.pc; w = self.r32(pc)
        op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31; fn = w & 63
        simm = sx16(w & 0xFFFF)
        # taint propagation (before the instruction changes registers)
        if op == 0:
            if fn in (0, 2, 3): t[rd] = t[rt]
            elif fn in (4, 6, 7) or fn >= 32: t[rd] = t[rs] | t[rt]
            elif fn in (24, 25, 26, 27): self.ht = self.lt = t[rs] | t[rt]
            elif fn == 16: t[rd] = self.ht
            elif fn == 18: t[rd] = self.lt
            elif fn == 9 or fn == 8:
                if t[rs]: self.log.append((pc, "jump", r[rs], t[rs]))
        elif op in (8, 9, 10, 11, 12, 13, 14): t[rt] = t[rs]
        elif op == 15: t[rt] = 0
        elif op in (32, 33, 35, 36, 37, 34, 38):
            a = (r[rs] + simm) & M32; n = {32: 1, 36: 1, 33: 2, 37: 2}.get(op, 4)
            if t[rs]: self.log.append((pc, "load", a, t[rs]))
            t[rt] = self._mtaint(a, n)
        elif op in (40, 41, 43, 42, 46):
            a = (r[rs] + simm) & M32; n = {40: 1, 41: 2}.get(op, 4)
            if t[rs]: self.log.append((pc, "store@", a, t[rs]))
            for i in range(n): self.mt[(a + i) & 0x7FFFFF] = t[rt]
        elif op == 3:
            m = t[4] | t[5] | t[6] | t[7]
            tgt = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            if m: self.log.append((pc, "call", tgt, (t[4], t[5], t[6], t[7])))
        super().step()
        t[0] = 0
        if op == 3 or (op == 0 and fn == 9): t[31] = 0
