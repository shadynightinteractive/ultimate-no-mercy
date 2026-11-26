"""readdiff.py (romimport, 2026-11-18): which RAM words a generator's emulated game code READS where the ROM-made image
differs from the rig's capture -- the run-time state a load map still lacks.

    python readdiff.py <report> <image>=<capture> [<image>=<capture> ...] -- <script.py> [args...]

Runs the script unchanged. Every harness Emu made on the way remembers the bytes its code (and the script, through
r8/r16/r32/rbytes) read before anything wrote them. At the end, per Emu, the image it was made from is told by its bytes
(the closest of the given images) and every read byte where that image and its capture differ is listed, merged into runs:
  [ram address, length, image bytes (hex, first 16), capture bytes (hex, first 16)]
A run in the report is a value the game had at run time that the step depends on."""
import json, os, runpy, sys

def main():
    rep = sys.argv[1]; k = sys.argv.index("--")
    pairs = [a.split("=", 1) for a in sys.argv[2:k]]
    script, args = sys.argv[k + 1], sys.argv[k + 1:]
    here = os.path.dirname(os.path.abspath(script))
    sys.path.insert(0, here)
    harness = None
    for d in (here, os.path.dirname(here), os.path.dirname(os.path.dirname(here))):
        if os.path.exists(os.path.join(d, "rig", "harness", "mipsemu.py")): harness = os.path.join(d, "rig", "harness")
    sys.path.insert(0, harness)
    import mipsemu
    imgs = [(open(i, "rb").read(), open(c, "rb").read(), i) for i, c in pairs]
    emus = []
    E = mipsemu.Emu
    init0 = E.__init__
    def init(self, ram, size=0x800000):
        init0(self, ram, size)
        self._init = bytes(self.m); self._rd = bytearray(len(self.m)); self._wr = bytearray(len(self.m)); emus.append(self)
    def mark_r(self, a, n):
        a = a & 0x7FFFFF
        for i in range(a, a + n):
            if not self._wr[i]: self._rd[i] = 1
    def mark_w(self, a, n):
        a = a & 0x7FFFFF; self._wr[a:a + n] = b"\1" * n
    r8, r16, r32, rb = E.r8, E.r16, E.r32, E.rbytes
    w8, w16, w32, wb = E.w8, E.w16, E.w32, E.wbytes
    E.__init__ = init
    E.r8 = lambda s, a: (mark_r(s, a, 1), r8(s, a))[1]
    E.r16 = lambda s, a: (mark_r(s, a, 2), r16(s, a))[1]
    E.r32 = lambda s, a: (mark_r(s, a, 4), r32(s, a))[1]
    E.rbytes = lambda s, a, n: (mark_r(s, a, n), rb(s, a, n))[1]
    E.w8 = lambda s, a, v: (mark_w(s, a, 1), w8(s, a, v))[1]
    E.w16 = lambda s, a, v: (mark_w(s, a, 2), w16(s, a, v))[1]
    E.w32 = lambda s, a, v: (mark_w(s, a, 4), w32(s, a, v))[1]
    E.wbytes = lambda s, a, b: (mark_w(s, a, len(b)), wb(s, a, b))[1]
    sys.argv = args
    try:
        runpy.run_path(script, run_name="__main__")
    finally:
        out = []
        for e in emus:
            # the image this Emu was made from: the most bytes equal in a sample
            best = max(imgs, key=lambda t: sum(1 for j in range(0, 0x800000, 4099) if t[0][j] == e._init[j]))
            img, cap, name = best
            runs = []
            for i in range(len(e._rd)):
                if e._rd[i] and e._init[i] == img[i] and img[i] != cap[i]:
                    if runs and i - (runs[-1][0] + runs[-1][1]) < 4: runs[-1][1] = i + 1 - runs[-1][0]
                    else: runs.append([i, 1])
            nread = sum(e._rd)
            out.append(dict(image=os.path.basename(name), bytes_read=nread,
                            runs=[[hex(0x80000000 + a), n, img[a:a + min(n, 16)].hex(), cap[a:a + min(n, 16)].hex()] for a, n in runs]))
        json.dump(out, open(rep, "w"), indent=1)

if __name__ == "__main__":
    main()
