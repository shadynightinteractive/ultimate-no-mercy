"""images.py (romimport, 2026-11-18): RAM images made from the ROMs, in place of the emulator rig's RAM captures.

Several generators and importers read a game's tables, code and loaded files out of an 8 MB RAM image the rig captured
(gen_data\\van_sel_ram.bin, nm_pick_ram.bin, wm2k_ram.bin, ...). Nearly all of what they read is the game's own bytes,
put in RAM by its loader: its code and data segments, its files as unpacked. A LOAD MAP says where: a list of runs
  [ram address, "rom", rom offset, length]   or   [ram address, "file", file number, offset in the file, length]
(our data: numbers only, found once by comparing a capture with the ROM -- `freeze`). `build` makes the image from the
ROM and the map; plus the map's "words": run-time values the game writes (a heap pointer ...), each one named in the map
with its reason. What a capture holds beyond the map (the game's run-time state) is NOT in the image: a step that needs
it is found by its output differing, and gets the game's code run instead (mipsemu) or a reader of its own.

    python images.py freeze <capture.bin> <game> <map.json> [--min 64]      (developer: once per capture)
    python images.py build <map.json> <out.bin>                              (the ROM-only build)
    python images.py check <map.json> <capture.bin>                          (how much of the capture the image holds)"""
import bisect, json, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from roms import Roms
from ramcover import Index, match_len

SIZE = 0x800000

def files_blob(F):
    blob = bytearray(); offs = []
    for f in range(1, F.count() + 1):
        d = F(f)
        if d is None: continue
        offs.append((len(blob), f)); blob += d + bytes(-len(d) % 16)
    return bytes(blob), offs

def freeze(capture, game, min_len=64):
    ram = open(capture, "rb").read()[:SIZE]
    R = Roms(); rom = R.rom(game); F = R.files(game)
    blob, offs = files_blob(F)
    starts = [o for o, _ in offs]
    srcs = [("rom", Index(rom)), ("file", Index(blob))]
    runs, i, n = [], 0, len(ram)
    while i < n:
        chunk = ram[i:i + 16]
        if not any(chunk): i += 16; continue
        best = None
        for name, X in srcs:
            for at in X.cands(chunk):
                L = match_len(ram, i, X.s, at)
                if name == "file":                       # (not past the file's end)
                    k = bisect.bisect_right(starts, at) - 1
                    L = min(L, len(F(offs[k][1])) - (at - offs[k][0]))
                if best is None or L > best[3]: best = (i, name, at, L)
        if best and best[3] >= min_len:
            a, name, at, L = best
            if name == "rom": runs.append([0x80000000 + a, "rom", at, L])
            else:
                k = bisect.bisect_right(starts, at) - 1
                runs.append([0x80000000 + a, "file", offs[k][1], at - offs[k][0], L])
            i += L
            if i & 15: i = (i + 15) & ~15
        else: i += 16
    return merge(dict(game=game, sha1=__import__("roms").SHA1[game], runs=runs, words=[]))

def merge(m, gap=1024):
    """a loaded segment comes out as several runs where the capture differs from the ROM inside it (the rig's hook words,
    the game's run-time data): runs from the same place (the same RAM - ROM distance) with a gap under `gap` bytes are one
    run -- the gap gets the ROM's bytes, the values the game's loader put there before it ran (not the capture's)"""
    out = []
    for r in sorted(m["runs"], key=lambda r: r[0]):
        p = out[-1] if out else None
        if (p and p[1] == r[1] and (r[1] == "rom" or p[2] == r[2]) and
                r[0] - (p[0] + p[-1]) < gap and r[0] - p[0] == r[-2] - p[-2] and r[0] >= p[0] + p[-1]):
            p[-1] = r[0] + r[-1] - p[0]
        else: out.append(list(r))
    m["runs"] = out
    return m

def build(m, R=None):
    R = R or Roms()
    rom = R.rom(m["game"]); F = R.files(m["game"])
    img = bytearray(SIZE)
    for r in m["runs"]:
        a = r[0] & 0x7FFFFF
        if r[1] == "rom": _, _, o, L = r; img[a:a + L] = rom[o:o + L]
        else: _, _, f, o, L = r; img[a:a + L] = F(f)[o:o + L]
    for w in m.get("words", []):                       # [address, value, why]
        struct.pack_into(">I", img, w[0] & 0x7FFFFF, w[1])
    return bytes(img)

def check(m, capture):
    cap = open(capture, "rb").read()[:SIZE]; img = build(m)
    same = sum(1 for i in range(0, SIZE, 4) if cap[i:i + 4] == img[i:i + 4])
    nz_missing = sum(1 for i in range(0, SIZE, 4) if cap[i:i + 4] != img[i:i + 4] and any(cap[i:i + 4]))
    return same, nz_missing

def save(m, path):
    with open(path, "w", newline="\n") as w:
        w.write('{"game": %s, "sha1": %s,\n "words": %s,\n "runs": [\n%s\n]}\n' % (json.dumps(m["game"]), json.dumps(m["sha1"]),
                json.dumps(m.get("words", [])), ",\n".join(json.dumps(r) for r in m["runs"])))

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "freeze":
        mn = int(sys.argv[sys.argv.index("--min") + 1]) if "--min" in sys.argv else 64
        m = freeze(sys.argv[2], sys.argv[3], mn); save(m, sys.argv[4])
        print("%s: %d runs, %d bytes" % (sys.argv[4], len(m["runs"]), sum(r[-1] for r in m["runs"])))
    elif cmd == "merge":
        m = merge(json.load(open(sys.argv[2]))); save(m, sys.argv[2]); print("%d runs" % len(m["runs"]))
    elif cmd == "build":
        open(sys.argv[3], "wb").write(build(json.load(open(sys.argv[2]))))
    elif cmd == "check":
        s, nz = check(json.load(open(sys.argv[2])), sys.argv[3])
        print("words equal to the capture: %d of %d; non-zero capture words not in the image: %d" % (s, SIZE // 4, nz))
