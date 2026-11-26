"""No Mercy's stage-set tables, all ten venues (gen/arenas_stages.py): MATCH[v] the match's set (0x800507FC[v] /
0x80050824[v] / 0x8005084C[v]: (mesh, palette) in drawing order), CLOSE[v] the entrances' close scene (index v: 0x80052280..,
data/arenaport/NM_ENTRANCE.md) -- its place, main list, extra list (+ x1_off), animated pieces (mesh, palettes by frame),
single models. (Found in data/arenaport/entrance/cap/ent_step3_ram.bin by data/arenaport/allstages/tabs.py.)

[stagelists 2026-11-19] These are the game's own tables, so they are read from the No Mercy ROM, not copied here (they were
until 2026-11-19): the game's data segment holds them (RAM 0x80050370..0x800527E0 = Rev 1 ROM 0x50F70, v1.0 ROM 0x51010 --
its pointers 0xA0 further on), the same way as the first start's builder (exe/WWFNoMercyRecomp/src/main/
arcade_firstrun_stages.cpp close_scenes: the same places and check numbers). Only the tables' addresses (as Rev 1 has them)
and the check numbers are here."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import os, struct

NM_ROM = nmp("/home/devbox/work/nomercy.z64")   # (WWF No Mercy (USA) (Rev 1))
SPAN_LO, SPAN_BYTES = 0x80050370, 0x2470         # (the tables and lists read, as Rev 1's addresses)
# (ROM offset of SPAN_LO, how much further on that ROM's pointers are, FNV-1a 64 of its SPAN_BYTES bytes: a check number)
PLACES = ((0x50F70, 0x00, 0x7DAB693F1BBBD8DB, "No Mercy (USA) Rev 1"),
          (0x51010, 0xA0, 0x562D0EDED587C460, "No Mercy (USA) v1.0"))
# the game's tables (Rev 1 addresses), by venue / scene index
MATCH_MESH, MATCH_PAL, MATCH_N = 0x800507FC, 0x80050824, 0x8005084C
SC_PLACE = 0x80050690                             # (16 bytes an index: x, y, z floats)
SC_MAIN_MESH, SC_MAIN_PAL, SC_MAIN_N = 0x80052280, 0x800522F8, 0x80052334
SC_X1_MESH, SC_X1_PAL, SC_X1_N, SC_X1_OFF = 0x80052354, 0x80052390, 0x800523CC, 0x800523EC   # (x1_off: 12 bytes an index)
SC_X2 = (0x800526C0, 0x800526FC, 0x80052738)      # (mesh list, palette lists by frame, count)
SC_X3 = (0x80052758, 0x80052790, 0x800527CC)
SC_M61_MESH, SC_M61_PAL, SC_M61_N = 0x800524A0, 0x800524DC, 0x80052518


def _fnv1a64(b):
    h = 0xCBF29CE484222325
    for x in b: h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


class _Tables:
    """reads the span by Rev 1 addresses; a pointer read is taken back to Rev 1's address"""
    def __init__(self, b, shift): self.b, self.shift = b, shift
    def _at(self, a, n):
        if not (SPAN_LO <= a and a + n <= SPAN_LO + SPAN_BYTES): raise SystemExit("arenas_stages_tab.py: 0x%X is outside the tables read" % a)
        return a - SPAN_LO
    def u16(self, a): return struct.unpack_from(">H", self.b, self._at(a, 2))[0]
    def s16(self, a): return struct.unpack_from(">h", self.b, self._at(a, 2))[0]
    def ptr(self, a):
        p = struct.unpack_from(">I", self.b, self._at(a, 4))[0]
        return p - self.shift if p else 0
    def whole(self, a):
        f = struct.unpack_from(">f", self.b, self._at(a, 4))[0]
        if f != int(f): raise SystemExit("arenas_stages_tab.py: the float at 0x%X is not a whole number" % a)
        return int(f)
    def lst(self, p, n): return [self.u16(p + 2 * k) for k in range(n)] if p else []


def read_tables(path=None):
    """(MATCH, CLOSE) from the ROM: the place whose bytes have the check number; stops if none does"""
    rom = open(path or NM_ROM, "rb").read()
    for at, shift, fnv, _ in PLACES:
        b = rom[at:at + SPAN_BYTES]
        if len(b) == SPAN_BYTES and _fnv1a64(b) == fnv: break
    else:
        raise SystemExit("arenas_stages_tab.py: %s does not hold the stage sets' tables where No Mercy (USA) Rev 1 (0x%X) or v1.0 "
                         "(0x%X) has them" % (os.path.basename(path or NM_ROM), PLACES[0][0], PLACES[1][0]))
    r = _Tables(b, shift)
    match, close = {}, {}
    for v in range(10):
        n = r.s16(MATCH_N + 2 * v)
        match[v] = list(zip(r.lst(r.ptr(MATCH_MESH + 4 * v), n), r.lst(r.ptr(MATCH_PAL + 4 * v), n)))
        d = dict(place=tuple(r.whole(SC_PLACE + 16 * v + 4 * k) for k in range(3)),
                 x1_off=tuple(r.whole(SC_X1_OFF + 12 * v + 4 * k) for k in range(3)))
        n = r.s16(SC_MAIN_N + 2 * v)
        d["main"] = list(zip(r.lst(r.ptr(SC_MAIN_MESH + 4 * v), n), r.lst(r.ptr(SC_MAIN_PAL + 4 * v), n)))
        n = r.s16(SC_X1_N + 2 * v)
        d["x1"] = list(zip(r.lst(r.ptr(SC_X1_MESH + 4 * v), n), r.lst(r.ptr(SC_X1_PAL + 4 * v), n)))
        n = r.u16(SC_M61_N + 2 * v)
        d["m61"] = list(zip(r.lst(r.ptr(SC_M61_MESH + 4 * v), n), r.lst(r.ptr(SC_M61_PAL + 4 * v), n)))
        for name, (tm, tp, tn) in (("x2", SC_X2), ("x3", SC_X3)):
            n = r.u16(tn + 2 * v)
            if not n: d[name] = None; continue
            pp = r.ptr(tp + 4 * v)
            d[name] = (r.lst(r.ptr(tm + 4 * v), 1)[0], [r.lst(r.ptr(pp + 4 * k), 1)[0] for k in range(n)])
        close[v] = d
    return match, close


MATCH, CLOSE = read_tables()
