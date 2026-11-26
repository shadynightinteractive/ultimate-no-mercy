import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# ROM patcher for the six-man engine: edits at game addresses (per section), checked against the
# original instruction, then the N64 header CRC recomputed (CIC 6102-style).
import pickle, struct

DB = nmp('/home/devbox/work/six/db.pkl')
secs, fn2sec, insns = pickle.load(open(DB, 'rb'))

def rom_off(vram, sec):
    s = secs[sec]
    return s['rom'] + (vram - s['vram'])

class Rom:
    def __init__(self, path):
        self.b = bytearray(open(path, 'rb').read())
        self.log = []
    def r32(self, vram, sec):
        o = rom_off(vram, sec); return struct.unpack('>I', self.b[o:o + 4])[0]
    def w32(self, vram, sec, val, expect=None, why=''):
        o = rom_off(vram, sec)
        cur = struct.unpack('>I', self.b[o:o + 4])[0]
        if expect is not None and cur != expect:
            raise ValueError('%s %08x: %08x != expected %08x (%s)' % (sec, vram, cur, expect, why))
        self.b[o:o + 4] = struct.pack('>I', val & 0xFFFFFFFF)
        self.log.append((sec, vram, cur, val & 0xFFFFFFFF, why))
    def raw(self, off, data):
        self.b[off:off + len(data)] = data
    def set_imm16(self, vram, sec, imm, why=''):
        cur = self.r32(vram, sec)
        self.w32(vram, sec, (cur & 0xFFFF0000) | (imm & 0xFFFF), cur, why)
    def fix_crc(self):
        # CIC-NUS-6102 checksum over 0x1000..0x101000
        b = self.b
        seed = 0xF8CA4DDC
        t1 = t2 = t3 = t4 = t5 = t6 = seed
        M = 0xFFFFFFFF
        for i in range(0x1000, 0x101000, 4):
            d = struct.unpack('>I', b[i:i + 4])[0]
            if ((t6 + d) & M) < t6: t4 = (t4 + 1) & M
            t6 = (t6 + d) & M
            t3 ^= d
            r = ((d << (d & 0x1F)) | (d >> ((32 - (d & 0x1F)) & 31))) & M if (d & 0x1F) else d
            t5 = (t5 + r) & M
            if t2 > d: t2 ^= r
            else: t2 ^= t6 ^ d
            t1 = (t1 + (t5 ^ d)) & M
        c1 = t6 ^ t4 ^ t3
        c2 = t5 ^ t2 ^ t1
        struct.pack_into('>II', b, 0x10, c1, c2)
    def save(self, path):
        self.fix_crc()
        open(path, 'wb').write(self.b)

def check_crc(path):
    r = Rom(path)
    old = struct.unpack('>II', r.b[0x10:0x18])
    r.fix_crc()
    new = struct.unpack('>II', r.b[0x10:0x18])
    return old, new

if __name__ == '__main__':
    o, n = check_crc(nmp('/home/devbox/work/nomercy.z64'))
    print('header', [hex(x) for x in o], 'computed', [hex(x) for x in n])
