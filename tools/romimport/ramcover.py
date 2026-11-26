"""ramcover.py (romimport, 2026-11-17): which parts of a captured RAM image are the ROM's bytes, and from where.

    python ramcover.py <ram.bin> <game> [--files] [--min 64] [--out cover.json]

Walks the 8 MB image in 16-byte chunks; a non-zero chunk is looked up in the game's ROM (and, with --files, in its AKI
files unpacked) and the match is then followed byte by byte as far as it goes -- so a loaded segment or file is one run.
Prints the runs (RAM address, source, offset, length) and what is left: non-zero bytes no run covers (the game's
run-time state: heap pointers, tables built at boot, ...). The runs are what a ROM-made image needs to hold the same
bytes as the capture."""
import json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from roms import Roms

def match_len(ram, i, s, at):
    """how far ram[i:] and s[at:] agree (blocks first, then bytes)"""
    L, n = 0, min(len(ram) - i, len(s) - at)
    B = 4096
    while L + B <= n and ram[i + L:i + L + B] == s[at + L:at + L + B]: L += B
    while L < n and ram[i + L] == s[at + L]: L += 1
    return L

class Index:
    """every 8-byte key at 4-byte steps of a source, sorted (numpy): candidates for a chunk in O(log n)"""
    def __init__(self, s):
        import numpy as np
        self.s = s
        w = np.frombuffer(s[:len(s) // 4 * 4], dtype=">u4").astype(np.uint64)
        keys = (w[:-1] << np.uint64(32)) | w[1:]
        self.order = np.argsort(keys, kind="stable"); self.keys = keys[self.order]
    def cands(self, chunk, cap=32):
        import numpy as np
        k = np.uint64(int.from_bytes(chunk[:8], "big"))
        lo = int(np.searchsorted(self.keys, k, "left")); hi = int(np.searchsorted(self.keys, k, "right"))
        return [int(self.order[j]) * 4 for j in range(lo, min(hi, lo + cap))]

def runs_of(ram, sources, min_len=64):
    """sources: [(name, bytes)]. -> [(ram offset, name, src offset, length)], uncovered non-zero byte count"""
    idx = [(name, Index(s)) for name, s in sources]
    out, i, n, left = [], 0, len(ram), 0
    while i < n:
        chunk = ram[i:i + 16]
        if not any(chunk): i += 16; continue
        best = None
        for name, X in idx:
            for at in X.cands(chunk):
                L = match_len(ram, i, X.s, at)
                if best is None or L > best[3]: best = (i, name, at, L)
        if best and best[3] >= min_len:
            out.append(best); i += best[3]
            if i & 15: i = (i + 15) & ~15
        else:
            left += sum(1 for b in chunk if b); i += 16
    return out, left

if __name__ == "__main__":
    ram = open(sys.argv[1], "rb").read()
    game = sys.argv[2]
    R = Roms()
    srcs = [("rom", R.rom(game))]
    if "--files" in sys.argv:
        F = R.files(game)
        blob = bytearray(); offs = []
        for f in range(1, F.count() + 1):
            offs.append((len(blob), f)); d = F(f); blob += d + bytes(-len(d) % 16)
        srcs.append(("files", bytes(blob)))
    mn = int(sys.argv[sys.argv.index("--min") + 1]) if "--min" in sys.argv else 64
    runs, left = runs_of(ram, srcs, mn)
    for a, nm, o, L in runs:
        extra = ""
        if nm == "files":
            import bisect
            k = bisect.bisect_right([x for x, _ in offs], o) - 1
            extra = " (file 0x%X +0x%X)" % (offs[k][1], o - offs[k][0])
        print("0x%08X  %-5s 0x%08X  0x%06X%s" % (0x80000000 + a, nm, o, L, extra))
    print("covered %d bytes in %d runs; non-zero bytes left %d" % (sum(r[3] for r in runs), len(runs), left))
    if "--out" in sys.argv:
        json.dump([[0x80000000 + a, nm, o, L] for a, nm, o, L in runs], open(sys.argv[sys.argv.index("--out") + 1], "w"))
