"""The AKI file table (2026-11-12, handlers): every AKI N64 game keeps its files the same way -- a table of u32 offsets
(bit 0: the file is LZSS-packed, a u32 unpacked size first) from a base in the ROM, files numbered from 1. Only where
the table and the base are differs by game, so each handler passes its own (GAMES below lists the ones measured).

This is the one reader for all of them; before it, data/revport/extract.py and data/extracted/wm2k_work/extract.py were
two copies of the same code with the numbers changed."""
import hashlib, struct

# game -> (table offset, file count + 1, data base, the ROM's SHA-1 these were measured on)
GAMES = {
    "revenge": (0xCE2752, 7658, 0xDAC50, None),         # WCW/nWo Revenge (USA)   (data/revport/extract.py)
    "wm2000":  (0x11778BE, 0xA120 // 4, 0x144AA0, None),  # WWF WrestleMania 2000 (USA) (data/extracted/wm2k_work/extract.py)
    # WWF No Mercy (USA) (Rev 1) (2026-11-13, nmhandler): the table the game's own file code reads (mod src/files.c files_info:
    # data base 0x1BD150), 19,458 files -- the same reader reproduces data/extracted/nmfiles byte for byte (nomercy/game.py)
    "nomercy": (0x16C31D8, 19459, 0x1BD150, None),
    # (2026-11-13, imports2) WWF WarZone - Badd Blood (USA) (v1.0), a WrestleMania 2000 mod: its table moved -- found by
    # scratch\badd_blood\find_table.py, not assumed -- the same count and data base as WrestleMania 2000's
    "baddblood": (0x1137FAA, 0xA120 // 4, 0x144AA0, None),
    # (2026-11-13, imports2) No Mercy Plus V3 (its xdelta applied to No Mercy v1.0: data\nmplus\nmplus.z64, over 64 MB): a
    # table of its own (data\nmplus\xnm.py's numbers; the same reader gives data\nmplus\f byte for byte)
    "nmplus_v3": (0x2BBC4D0, 0x7802, 0x5CD1B0, None),
}

def lzss(src, n):
    """AKI's LZSS: a 4 KB ring starting at 0xFEE, flag bytes low bit first, 12-bit position and 4-bit length + 3"""
    out = bytearray(n); ring = bytearray(0x1000); r = 0xFEE; i = 4; o = 0; flags = 0
    while o < n and i < len(src):
        flags >>= 1
        if not (flags & 0x100): flags = src[i] | 0xFF00; i += 1
        if flags & 1:
            c = src[i]; i += 1; out[o] = c; o += 1; ring[r] = c; r = (r + 1) & 0xFFF
        else:
            lo = src[i]; hi = src[i + 1]; i += 2
            pos = lo | ((hi & 0xF0) << 4); ln = (hi & 0xF) + 3
            for k in range(ln):
                c = ring[(pos + k) & 0xFFF]
                if o < n: out[o] = c; o += 1
                ring[r] = c; r = (r + 1) & 0xFFF
    return bytes(out)

class RomFiles:
    """A ROM's files by number, unpacked on first use."""
    def __init__(self, rom_path, game):
        self.rom = open(rom_path, "rb").read()
        self.sha1 = hashlib.sha1(self.rom).hexdigest()
        tab, n, base, _ = GAMES[game]
        self.n, self.base = n, base
        self.t = struct.unpack(">%dI" % n, self.rom[tab:tab + 4 * n])
        self.cache = {}
    def count(self): return self.n - 1
    def __call__(self, i):
        """file i (1-based), unpacked"""
        if i not in self.cache:
            a = (self.t[i - 1] & ~1) + self.base; b = (self.t[i] & ~1) + self.base
            raw = self.rom[a:b]
            if self.t[i - 1] & 1:
                dn = struct.unpack(">I", raw[:4])[0]
                raw = raw if dn > 0x400000 else lzss(raw, dn)      # (the extractors' rule: a size that big isn't one)
            self.cache[i] = raw
        return self.cache[i]
