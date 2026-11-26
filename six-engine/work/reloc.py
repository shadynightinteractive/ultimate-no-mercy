import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Moving data the game references by absolute address (lui hi / lo-immediate pairs), checked:
# every lui whose high half changes must only feed references that move by the same high half.
import pickle, re, collections
from luifind import find_lui, parse
refs = pickle.load(open(nmp('/home/devbox/work/six/refs.pkl'), 'rb'))
ALL = ('ovl_e', 'main_1050', 'ovl_a', 'ovl_b', 'ovl_c', 'ovl_d', 'entry')

def hilo(addr):
    lo = addr & 0xFFFF
    if lo >= 0x8000: lo -= 0x10000
    return ((addr - lo) >> 16) & 0xFFFF, lo

def base_reg(asm):
    m = re.search(r'\((\$\w+)\)', asm)
    if m: return m.group(1)
    op, args = parse(asm)
    return args[1]

_lui_users = None
def lui_users():
    """lui (section, pc) -> the references it feeds"""
    global _lui_users
    if _lui_users is None:
        _lui_users = collections.defaultdict(list)
        for r in refs:
            ea, sec, fn, pc, op, asm, kind = r
            if sec is None: continue
            try: lu, _ = find_lui(sec, fn, pc, base_reg(asm))
            except Exception: lu = None
            if lu is not None: _lui_users[(sec, lu)].append(r)
    return _lui_users

# references the scan reads as into a moved array that are not: an index biased by a large constant reaches
# other data (the scan's address is the base with no index) -- left alone
NOT_REFS = {
    ('ovl_a', 0x800DD770): 'func_800DD740: lw 0x6F58(0x800B0000 + id*4), id in 0xF0CE..0xF1CA -> the table at 0x800F3290 (ovl_a data)',
    ('main_1050', 0x80016EB4): 'func_80016D2C: 0x800768D0 + type*0xA0 + variant*0x50, type >= 1 -> the part table at 0x80076970.. (not model face parts set B)',
    ('ovl_c', 0x8012F66C): 'func_8012F518: lbu 0x30F8(0x800A0000 + i), i from 0x6700 -> 0x800A97F8.. (not model bones)',
}

class Moves:
    """A set of data moves, applied together: (lo, hi, new) ranges."""
    def __init__(self): self.ranges = []; self.skipped = []
    def add(self, lo, hi, new, why): self.ranges.append((lo, hi, new, why))
    def target(self, ea):
        for lo, hi, new, why in self.ranges:
            if lo <= ea < hi: return new + (ea - lo), why
        return None, None
    def apply(self, rom, sections=ALL, end_refs=()):
        """end_refs: addresses one past a moved array that the code uses as a loop end (moved with it)."""
        ends = dict(end_refs)              # old end address -> new end address
        users = lui_users()
        n = 0; luis = {}
        seen = set()
        for r in refs:
            ea, sec, fn, pc, op, asm, kind = r
            if sec not in sections or (sec, pc) in seen or (sec, pc) in NOT_REFS: continue
            new, why = self.target(ea)
            if new is None and kind == 'addr' and ea in ends: new, why = ends[ea], 'loop end'
            if new is None: continue
            seen.add((sec, pc))
            oh, ol = hilo(ea); nh, nl = hilo(new)
            lu, chain = find_lui(sec, fn, pc, base_reg(asm))
            if lu is None: raise ValueError('no lui for %s %s %08x %s' % (sec, fn, pc, asm))
            if nh != oh:
                if (sec, lu) in luis and luis[(sec, lu)] != nh: raise ValueError('lui %s %08x needs two highs' % (sec, lu))
                luis[(sec, lu)] = nh
            ins = rom.r32(pc, sec); imm = ins & 0xFFFF; imm = imm - 0x10000 if imm >= 0x8000 else imm
            if imm != ol:
                # (a function the database files under the wrong one of two overlays sharing an address)
                self.skipped.append((sec, fn, pc, asm)); seen.discard((sec, pc)); continue
            rom.set_imm16(pc, sec, nl, why)
            n += 1
        # every user of a changed lui must have moved with the same high half
        for (sec, lu), nh in luis.items():
            for r in users[(sec, lu)]:
                if r[1] != sec: continue
                ea = r[0]
                new, _ = self.target(ea)
                if new is None and r[6] == 'addr' and ea in ends: new = ends[ea]
                if new is None or hilo(new)[0] != nh:
                    raise ValueError('lui %s %08x (%s) also feeds %08x at %08x %s, which does not move with it' %
                                     (sec, lu, r[2], ea, r[3], r[5]))
            rom.set_imm16(lu, sec, nh, 'lui')
        return n, len(luis)
