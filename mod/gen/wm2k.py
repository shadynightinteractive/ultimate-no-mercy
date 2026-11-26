"""WWF WrestleMania 2000 (USA): its files and code, for porting a Superstar of it (gen/mkjarrett.py).

Same AKI engine as No Mercy. Its files: /tmp/devbox-0/wm2k_files/XXXX.bin (unpacked from the ROM's
table at 0x11778BE, data at 0x144AA0). Its code is run from a RAM image (gen_data/wm2k_ram.bin: the
game in its attract demo) on harness/mipsemu.py:
  0x800178FC (looks 0x24 bytes, model 0x270): a costume's model, as No Mercy's func_80018A3C
  0x80004D0C (moves, slots, abilities, abilities out): a moveset unpacked, as No Mercy's func_80005EFC
Roster: record pointers 0x800411B0 [0x4C] -> { name*, u16 id, ord, ?, weight - 100, moves file,
abilities file, u8 music, video, u16 0, costume looks* [4] (0x24 bytes each) }.
Move names: move m's record (files 0x94/0x3AE/0x3AF, 20 bytes, [m / 10]) starts with its name's number
in the name tables (files 0x95-0x97: u16 offsets, then strings); No Mercy's: 0x69/0x1F0/0x1F1, 0x6A-0x6E.
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import sys, struct
sys.path.insert(0, nmp("/home/devbox/work/harness"))
from mipsemu import Emu
WMF = nmp("/tmp/devbox-0/wm2k_files/%04x.bin")
NMF = nmp("/tmp/devbox-0/nmfiles/f/%04x.bin")
RAM = open(nmp("/home/devbox/work/gen_data/wm2k_ram.bin"), "rb").read()
def wfile(i): return open(WMF % i, "rb").read()
def nfile(i): return open(NMF % i, "rb").read()
_E = Emu(RAM)
def r32(a): return _E.r32(a)
def rbytes(a, n): return _E.rbytes(a, n)
def cstr(a):
    b = rbytes(a, 48); return b[:b.index(0)].decode("latin1")

def roster():
    out = []
    for k in range(0x4C):
        rec = r32(0x800411B0 + 4 * k)
        f = struct.unpack(">HHHHHHBB", rbytes(rec + 4, 14))
        out.append(dict(k=k, name=cstr(r32(rec)), id=f[0], ord=f[1], b=f[2], weight=f[3], moves=f[4], abil=f[5],
                        music=f[6], video=f[7], cost=[r32(rec + 0x14 + 4 * i) for i in range(4)]))
    return out

def model(looks):
    """a costume's model (0x270 bytes) from its looks (0x24 bytes)"""
    E = Emu(RAM)
    E.hooks[0x8002A250] = E.hooks[0x8002A2C0] = lambda e: (e.r.__setitem__(2, 0), True)[1]   # interrupts
    E.hooks[0x80003DD4] = E.hooks[0x80003E98] = lambda e: (e.r.__setitem__(2, 0), True)[1]   # file reads
    E.wbytes(0x807A0000, bytes(0x400)); E.wbytes(0x807A1000, looks)
    E.call(0x800178FC, [0x807A1000, 0x807A0000])
    return E.rbytes(0x807A0000, 0x270)

def unpack_moves(mv, ab):
    """-> 292 slots (move, f2, f3, f4), 50 abilities"""
    E = Emu(RAM)
    E.wbytes(0x80780000, mv); E.wbytes(0x80780100, ab); E.wbytes(0x80781000, bytes(0x800)); E.wbytes(0x80782000, bytes(0x80))
    E.call(0x80004D0C, [0x80780000, 0x80781000, 0x80780100, 0x80782000])
    s = E.rbytes(0x80781000, 0x800); a = E.rbytes(0x80782000, 0x80)
    return ([struct.unpack_from(">HBBB", s, 6 * i) for i in range(292)],
            [struct.unpack_from(">h", a, 2 * i)[0] for i in range(50)])

def _names(files, get):
    out = []
    for f in files:
        d = get(f); n = struct.unpack_from(">H", d, 0)[0] // 2
        for o in struct.unpack_from(">%dH" % n, d, 0): out.append(d[o:d.index(b"\0", o)].decode("latin1"))
    return out
def _by_move(names, recfile):
    d = recfile; out = {}
    for i in range(len(d) // 20):
        k = struct.unpack_from(">H", d, 20 * i)[0]
        if k < len(names) and names[k]: out[10 * i] = names[k]
    return out
WM_NAMES = _by_move(_names((0x95, 0x96, 0x97), wfile), wfile(0x94))
NM_NAMES = _by_move(_names((0x6A, 0x6B, 0x6C, 0x6D, 0x6E), nfile), nfile(0x69))
