"""Wwise file packages (.pck, magic "AKPK"): list and pull out the sound banks and streamed .wem files inside.
2026-11-14, commentary agent: a fresh reader (the sandbox's tools/akpk.py wasn't kept), written for the rebuild of the
commentary pack from the player's own WWE '13 (Wii) disc.

Layout (Wwise's File Packager, both byte orders -- the Wii's packages are big-endian):
  "AKPK", u32 header size (bytes after this field), u32 version (1),
  u32 language map size, u32 sound bank table size, u32 stream table size, [u32 externals table size: newer Wwise]
  language map: u32 count, count x (u32 name offset, u32 language id), then the names (UTF-16 or 8-bit)
  each table: u32 count, count x (u32 id, u32 block size, u32 size, u32 start block, u32 language id)
              (the externals table's id is 64-bit); a file starts at start block * block size from the package's start.

  python akpk.py list <package.pck>                       the files in it (kind, id, language, size)
  python akpk.py extract <package.pck> <out dir> [ids]    writes <lang>_<id>.wem / .bnk (all, or just these ids)
"""
import os, struct, sys

class Package:
    def __init__(self, path):
        self.path = path
        f = open(path, "rb"); h = f.read(64); f.close()
        if h[:4] != b"AKPK": raise ValueError("%s: not a Wwise file package" % path)
        # (the version word is 1: that tells the byte order)
        self.e = "<" if struct.unpack_from("<I", h, 8)[0] == 1 else ">"
        if struct.unpack_from(self.e + "I", h, 8)[0] != 1: raise ValueError("%s: unknown AKPK version" % path)
        hsize, _ver, lmap, bnk, stm = struct.unpack_from(self.e + "5I", h, 4)
        # (the header size counts from after its own field: the four sizes + the tables; newer Wwise adds a fifth size,
        #  the externals table's -- there when the four alone don't add up)
        if 16 + lmap + bnk + stm == hsize: ext, p = 0, 24
        else:
            ext = struct.unpack_from(self.e + "I", h, 24)[0]; p = 28
            if 20 + lmap + bnk + stm + ext != hsize: raise ValueError("%s: AKPK header sizes don't add up" % path)
        f = open(path, "rb"); blob = f.read(8 + hsize); f.close()
        self.languages = self._langs(blob[p:p + lmap]); p += lmap
        self.files = []        # (kind, id, language id, offset, size)
        self.files += self._table(blob[p:p + bnk], "bnk", 4); p += bnk
        self.files += self._table(blob[p:p + stm], "wem", 4); p += stm
        if ext: self.files += self._table(blob[p:p + ext], "wem", 8)

    def _langs(self, b):
        if len(b) < 4: return {}
        n = struct.unpack_from(self.e + "I", b, 0)[0]; out = {}
        for i in range(n):
            off, lid = struct.unpack_from(self.e + "2I", b, 4 + 8 * i)
            s = b[off:off + 128]
            # (UTF-16 if every other byte is 0)
            if len(s) > 1 and (s[1] == 0 or s[0] == 0):
                raw = s.split(b"\0\0")[0]
                name = (raw + b"\0").decode("utf-16-le" if self.e == "<" else "utf-16-be", "ignore").strip("\0")
            else:
                name = s.split(b"\0")[0].decode("latin-1")
            out[lid] = name
        return out

    def _table(self, b, kind, idsize):
        if len(b) < 4: return []
        n = struct.unpack_from(self.e + "I", b, 0)[0]; out = []
        fmt = self.e + ("Q4I" if idsize == 8 else "5I"); step = struct.calcsize(fmt)
        for i in range(n):
            fid, block, size, start, lang = struct.unpack_from(fmt, b, 4 + step * i)
            out.append((kind, fid, lang, start * block, size))
        return out

    def read(self, entry):
        kind, fid, lang, off, size = entry
        with open(self.path, "rb") as f:
            f.seek(off); return f.read(size)

    def by_id(self):
        return {(e[0], e[1]): e for e in self.files}

def main():
    a = sys.argv[1:]
    if len(a) >= 2 and a[0] == "list":
        pk = Package(a[1])
        print("byte order", "little" if pk.e == "<" else "big", "languages", pk.languages)
        for k, fid, lang, off, size in pk.files: print("%s\t%d\t%s\t%d\t%d" % (k, fid, pk.languages.get(lang, lang), off, size))
    elif len(a) >= 3 and a[0] == "extract":
        pk = Package(a[1]); os.makedirs(a[2], exist_ok=True)
        want = set(int(x) for x in a[3:])
        for e in pk.files:
            if want and e[1] not in want: continue
            open(os.path.join(a[2], "%d_%d.%s" % (e[2], e[1], e[0])), "wb").write(pk.read(e))
    else:
        print(__doc__)

if __name__ == "__main__":
    main()
