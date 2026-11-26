"""akpk.py's check without a disc (2026-11-14): made-up packages in both byte orders, with and without the externals
table, read back. A real package from the disc is the real test (rebuild.py scan does that)."""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import akpk

def make(e, ext, files_b, files_s):
    lname = "english(us)".encode("utf-16-le" if e == "<" else "utf-16-be") + b"\0\0"
    lmap = struct.pack(e + "I", 1) + struct.pack(e + "2I", 12, 1) + lname
    lmap += b"\0" * (-len(lmap) % 4)
    def table(fs, start):
        t = struct.pack(e + "I", len(fs)); blk = start
        for fid, data in fs:
            t += struct.pack(e + "5I", fid, 16, len(data), blk, 1); blk += (len(data) + 15) // 16
        return t, blk
    # (the data area starts after the header, at a block boundary; table sizes are known before the offsets)
    nb, ns = 4 + 20 * len(files_b), 4 + 20 * len(files_s)
    hsize = 16 + len(lmap) + nb + ns + (4 + 4 if ext else 0)
    start = (8 + hsize + 15) // 16
    tb, blk = table(files_b, start); ts, _ = table(files_s, blk)
    head = b"AKPK" + struct.pack(e + "I", hsize) + struct.pack(e + "4I", 1, len(lmap), len(tb), len(ts))
    if ext: head += struct.pack(e + "I", 4)
    blob = head + lmap + tb + ts + (struct.pack(e + "I", 0) if ext else b"")
    blob += b"\0" * (start * 16 - len(blob))
    for _, d in files_b + files_s: blob += d + b"\0" * (-len(d) % 16)
    return blob

def main():
    os.makedirs(os.path.join(HERE, "tmp"), exist_ok=True)
    fb = [(3283901192, b"BKHD" + b"x" * 37)]; fs = [(1000371073, b"RIFX" + b"a" * 100), (5, b"RIFX" + b"b" * 7)]
    ok = True
    for e in "<>":
        for ext in (0, 1):
            p = os.path.join(HERE, "tmp", "t.pck"); open(p, "wb").write(make(e, ext, fb, fs))
            pk = akpk.Package(p)
            got = [(k, fid, pk.read(x)) for x in pk.files for k, fid in [(x[0], x[1])]]
            want = [("bnk", i, d) for i, d in fb] + [("wem", i, d) for i, d in fs]
            good = got == want and pk.languages == {1: "english(us)"}
            ok &= good
            print("byte order %s externals %d: %s" % (e, ext, "ok" if good else "WRONG %r" % (got,)))
            os.remove(p)
    print("ALL OK" if ok else "FAILED")

if __name__ == "__main__":
    main()
