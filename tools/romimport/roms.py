"""roms.py (romimport, 2026-11-17): the player's ROMs, found by fingerprint -- the one place the ROM-only builders get them.

The user's rule (2026-11-17): everything is populated from the ROM files the player puts in roms\\. A ROM is known by the
SHA-1 of its bytes in big-endian (.z64) order, whatever its file name, byte order (.z64 / .v64 / .n64) or whether it sits
in a .zip. No Mercy Plus V3 is accepted either way (the user's decision D): the patched ROM itself (its fingerprint), or
No Mercy v1.0 plus the third-party "No Mercy Plus V3.xdelta" (applied in memory by the project's own VCDIFF decoder, the
result checked against the same fingerprint).

    from roms import Roms
    R = Roms()                       # roms\\ of the source tree (or $UNM_ROMS)
    R.rom("revenge")                 # bytes, or None when the player has not got it
    R.files("wm2000")(0x122C)        # an AKI game's file by number, unpacked (aki.romtable)

Nothing is written; no capture, saved state or extracted folder is read."""
import hashlib, os, struct, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(SRC, "tools", "handlers"))
from handlers.aki import modtables as MT          # read_rom: any byte order, a .zip's first ROM
from handlers.aki import romtable as RT
from handlers.aki import vcdiff

# game -> SHA-1 of the big-endian bytes (the ROMs the project's tables and the tested folders were made from)
SHA1 = {
    "nomercy":   "91cee3d096f4a76644d8b35b9aead6448909abd1",   # WWF No Mercy (USA) (Rev 1) -- the game itself
    "nomercy10": "3566d9b5723291937582453ad0453ce517cfc358",   # WWF No Mercy (USA) v1.0 -- No Mercy Plus V3's base
    "nmplus_v3": "9f67445f44db5eb464f629f6a49719753bf52abe",   # No Mercy Plus V3 (v1.0 + its xdelta), 68,208,954 bytes
    "revenge":   "e1711a2511394b9357b5f1ac8ca5cc17bd674836",   # WCW/nWo Revenge (USA)
    "wm2000":    "442d417a52ed672ca1a47e7261a5414debb1e27a",   # WWF WrestleMania 2000 (USA)
    "baddblood": "aa8efede5a93cbd943b34fc2a46fe780a1e54825",   # WWF WarZone - Badd Blood (USA) (v1.0)
    "revenge_redux": "0695b127b654a1d6b79ffe7e62fb8f2981c26d5c",   # WCW/nWo Revenge Redux 05/20/2026 (Revenge + its xdelta), 67,346,744 bytes
}
NMPLUS_PATCH = "No Mercy Plus V3.xdelta"
# WCW/nWo Revenge Redux (a third-party patch over the Revenge ROM; the user's decision C, 2026-11-18: its entrance themes are
# built from it, gen/reduxmusic.py). Accepted as the patched ROM itself (its fingerprint above), or as WCW/nWo Revenge (USA)
# plus its xdelta -- found by the xdelta's own fingerprint, alone in roms\ or inside a .zip there (the release as it is
# downloaded: the member is read in memory, never extracted, and nothing else in the archive is opened or run). The patch
# is applied by the project's own VCDIFF decoder and the result checked against the patched ROM's fingerprint.
PATCHES = {"revenge_redux": ("revenge", "9341b5224eeab62cacfb80d4323bf0afc602e722")}   # game -> (its base, the xdelta's SHA-1)
ROM_EXT = (".z64", ".n64", ".v64", ".zip")

class Roms:
    def __init__(self, folder=None):
        self.folder = folder or os.environ.get("UNM_ROMS") or os.path.join(SRC, "roms")
        self._by_sha, self._files, self._patched = None, {}, {}
    def _scan(self):
        if self._by_sha is not None: return
        self._by_sha = {}
        for dp, dn, fn in os.walk(self.folder):
            for f in sorted(fn):
                if not f.lower().endswith(ROM_EXT): continue
                p = os.path.join(dp, f)
                try: b = MT.read_rom(p)
                except (StopIteration, zipfile.BadZipFile, OSError): continue
                self._by_sha.setdefault(hashlib.sha1(b).hexdigest(), p)
    def path(self, game):
        self._scan(); return self._by_sha.get(SHA1[game])
    def rom(self, game):
        """the game's ROM bytes (big-endian), or None"""
        p = self.path(game)
        if p: return MT.read_rom(p)
        if game == "nmplus_v3":                       # (decision D: v1.0 + the xdelta)
            base, patch = self.path("nomercy10"), self._find(NMPLUS_PATCH)
            if base and patch:
                b = vcdiff.apply(MT.read_rom(base), open(patch, "rb").read())
                if hashlib.sha1(b).hexdigest() == SHA1["nmplus_v3"]: return b
        if game in PATCHES:                           # (a base ROM + an xdelta found by its fingerprint)
            if game not in self._patched:
                base_game, patch_sha = PATCHES[game]
                base, patch, b = self.rom(base_game), self.patch(patch_sha), None
                if base is not None and patch is not None:
                    b = vcdiff.apply(base, patch)
                    if hashlib.sha1(b).hexdigest() != SHA1[game]: b = None
                self._patched[game] = b
            return self._patched[game]
        return None
    def patch(self, sha):
        """the bytes of the .xdelta in roms\\ whose SHA-1 is sha -- a file, or a member of a .zip (read in memory) -- or None"""
        for dp, dn, fn in os.walk(self.folder):
            for f in sorted(fn):
                p = os.path.join(dp, f)
                if f.lower().endswith(".xdelta"):
                    b = open(p, "rb").read()
                    if hashlib.sha1(b).hexdigest() == sha: return b
                elif f.lower().endswith(".zip"):
                    try:
                        z = zipfile.ZipFile(p)
                        for m in z.infolist():
                            if m.filename.lower().endswith(".xdelta") and m.file_size <= 1 << 28:   # (a patch, not a bomb)
                                b = z.read(m)
                                if hashlib.sha1(b).hexdigest() == sha: return b
                    except (zipfile.BadZipFile, OSError): continue
        return None
    def _find(self, name):
        for dp, dn, fn in os.walk(self.folder):
            for f in fn:
                if f.lower() == name.lower(): return os.path.join(dp, f)
        return None
    def files(self, game):
        """the game's AKI files by number (romtable.RomFiles over the ROM's bytes), or None"""
        if game not in self._files:
            b = self.rom(game)
            if b is None: self._files[game] = None
            else:
                rf = RomFiles.__new__(RomFiles); rf.init(b, game); self._files[game] = rf
        return self._files[game]

class RomFiles(RT.RomFiles):
    """aki.romtable.RomFiles over bytes already read (the table names the No Mercy Rev 1 game "nomercy")"""
    def init(self, rom, game):
        import struct
        self.rom = rom; self.sha1 = hashlib.sha1(rom).hexdigest()
        tab, n, base, _ = RT.GAMES[game]
        self.n, self.base = n, base
        self.t = struct.unpack(">%dI" % n, rom[tab:tab + 4 * n])
        self.cache = {}
    def __call__(self, i):
        """file i (1-based), unpacked; None for an entry that names no bytes (its end before its start, or past the ROM: No
        Mercy Plus has such entries, and the project's extraction wrote no file for them -- data\\nmplus\\xnm.py's rules:
        a packed entry under 4 bytes is kept as it is, a size of 0x400000 or more is not one, the unpacker stops at the
        data's end)"""
        if i not in self.cache:
            a = (self.t[i - 1] & ~1) + self.base; b = (self.t[i] & ~1) + self.base
            if b < a or b > len(self.rom): self.cache[i] = None; return None
            raw = self.rom[a:b]
            if self.t[i - 1] & 1 and len(raw) >= 4:
                dn = struct.unpack(">I", raw[:4])[0]
                raw = lzss(raw, dn) if dn < 0x400000 else raw
            self.cache[i] = raw
        return self.cache[i]

def lzss(src, n):
    """AKI's LZSS (aki.romtable.lzss), stopping cleanly where the packed data ends"""
    out = bytearray(n); ring = bytearray(0x1000); r = 0xFEE; i = 4; o = 0; flags = 0
    while o < n and i < len(src):
        flags >>= 1
        if not (flags & 0x100): flags = src[i] | 0xFF00; i += 1
        if flags & 1:
            if i >= len(src): break
            c = src[i]; i += 1; out[o] = c; o += 1; ring[r] = c; r = (r + 1) & 0xFFF
        else:
            if i + 1 >= len(src): break
            lo = src[i]; hi = src[i + 1]; i += 2
            pos = lo | ((hi & 0xF0) << 4); ln = (hi & 0xF) + 3
            for k in range(ln):
                c = ring[(pos + k) & 0xFFF]
                if o < n: out[o] = c; o += 1
                ring[r] = c; r = (r + 1) & 0xFFF
    return bytes(out)

if __name__ == "__main__":
    R = Roms()
    for g in SHA1:
        b = R.rom(g)
        how = R.path(g) and os.path.basename(R.path(g)) or ("Revenge + its xdelta" if g in PATCHES else "v1.0 + " + NMPLUS_PATCH)
        print("%-13s %s" % (g, ("%d bytes, %s" % (len(b), how)) if b else "not found"))
