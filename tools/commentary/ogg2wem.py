"""Test fixture maker: a standard Ogg Vorbis file -> a Wwise .wem (Vorbis, the "v62"/fmt-extra 0x30 layout WWE '13's
banks use: 2-byte packet headers, external codebooks by id, modified packets when the two block sizes differ), the inverse
of vgmstream's (ww2ogg's) rebuild. The audio packets are the same Vorbis packets, so decoding the .wem must give exactly
the Ogg file's PCM. Codebooks must exist in vgmstream's library (books.txt from dumpbooks.exe).
usage: python -I ogg2wem.py <books.txt> <in.ogg> <out.wem> [--be] [--list 0|1]"""
import struct, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "py"))
import oggpk

class BR:
    def __init__(s, b): s.b = b; s.p = 0
    def get(s, n):
        v = 0
        for i in range(n):
            byte = s.b[s.p >> 3]; v |= ((byte >> (s.p & 7)) & 1) << i; s.p += 1
        return v

class BW:
    def __init__(s): s.bits = []
    def put(s, n, v):
        for i in range(n): s.bits.append((v >> i) & 1)
    def bytes(s):
        out = bytearray((len(s.bits) + 7) // 8)
        for i, x in enumerate(s.bits): out[i >> 3] |= x << (i & 7)
        return bytes(out)

def ilog(v):
    r = 0
    while v: r += 1; v >>= 1
    return r

def quantvals(entries, dims):
    bits = ilog(entries); vals = entries >> ((bits - 1) * (dims - 1) // dims)
    while True:
        acc = 1; acc1 = 1
        for _ in range(dims): acc *= vals; acc1 *= vals + 1
        if acc <= entries and acc1 > entries: return vals
        if acc > entries: vals -= 1
        else: vals += 1

def read_codebook(r):
    start = r.p
    assert r.get(24) == 0x564342
    dims = r.get(16); entries = r.get(24)
    ordered = r.get(1)
    if ordered:
        r.get(5); cur = 0
        while cur < entries: cur += r.get(ilog(entries - cur))
    else:
        sparse = r.get(1)
        for _ in range(entries):
            if not sparse or r.get(1): r.get(5)
    lt = r.get(4)
    if lt == 1:
        r.get(32); r.get(32); vl = r.get(4) + 1; r.get(1)
        for _ in range(quantvals(entries, dims)): r.get(vl)
    elif lt != 0: raise ValueError("lookup type %d" % lt)
    return start, r.p

def bitstr(b, a, z):
    return "".join(str((b[i >> 3] >> (i & 7)) & 1) for i in range(a, z))

def load_books(path, which):
    lib = {}
    for l in open(path):
        f = l.split()
        if f[2] == "FAIL" or int(f[0]) != which: continue
        bits = int(f[2]); b = bytes.fromhex(f[3])
        lib.setdefault(bitstr(b, 0, bits), int(f[1]))
    return lib

def convert(books, ogg, be, which):
    pk = [p for p, _ in oggpk.packets(ogg)]
    ident, setup = pk[0], pk[2]
    channels = ident[11]; rate = struct.unpack_from("<I", ident, 12)[0]
    bs0, bs1 = ident[28] & 15, ident[28] >> 4
    lib = load_books(books, which)
    r = BR(setup); r.p = 7 * 8
    w = BW()
    ncb = r.get(8) + 1; w.put(8, ncb - 1)
    for _ in range(ncb):
        a, z = read_codebook(r)
        key = bitstr(setup, a, z)
        if key not in lib: raise SystemExit("a codebook not in the library (list %d)" % which)
        w.put(10, lib[key])
    # time domain (dropped by Wwise)
    nt = r.get(6) + 1
    for _ in range(nt): assert r.get(16) == 0
    # floors
    nf = r.get(6) + 1; w.put(6, nf - 1)
    for _ in range(nf):
        assert r.get(16) == 1
        parts = r.get(5); w.put(5, parts); cls = []
        for _ in range(parts): c = r.get(4); w.put(4, c); cls.append(c)
        dims = {}
        for c in range(max(cls) + 1 if cls else 0):
            d = r.get(3); w.put(3, d); dims[c] = d + 1
            sub = r.get(2); w.put(2, sub)
            if sub: w.put(8, r.get(8))
            for _ in range(1 << sub): w.put(8, r.get(8))
        w.put(2, r.get(2)); rb = r.get(4); w.put(4, rb)
        for c in cls:
            for _ in range(dims[c]): w.put(rb, r.get(rb))
    # residues
    nr = r.get(6) + 1; w.put(6, nr - 1)
    for _ in range(nr):
        t = r.get(16); w.put(2, t)
        for n in (24, 24, 24): w.put(n, r.get(n))
        ncl = r.get(6) + 1; w.put(6, ncl - 1); w.put(8, r.get(8))
        casc = []
        for _ in range(ncl):
            lo = r.get(3); w.put(3, lo); flag = r.get(1); w.put(1, flag); hi = 0
            if flag: hi = r.get(5); w.put(5, hi)
            casc.append(hi * 8 + lo)
        for c in casc:
            for k in range(8):
                if c & (1 << k): w.put(8, r.get(8))
    # mappings
    nm = r.get(6) + 1; w.put(6, nm - 1)
    for _ in range(nm):
        assert r.get(16) == 0
        sf = r.get(1); w.put(1, sf); sub = 1
        if sf: sub = r.get(4) + 1; w.put(4, sub - 1)
        sp = r.get(1); w.put(1, sp)
        if sp:
            st = r.get(8) + 1; w.put(8, st - 1)
            for _ in range(st): b = ilog(channels - 1); w.put(b, r.get(b)); w.put(b, r.get(b))
        w.put(2, r.get(2))
        if sub > 1:
            for _ in range(channels): w.put(4, r.get(4))
        for _ in range(sub): w.put(8, r.get(8)); w.put(8, r.get(8)); w.put(8, r.get(8))
    # modes
    nmo = r.get(6) + 1; w.put(6, nmo - 1); flags = []
    for _ in range(nmo):
        f = r.get(1); w.put(1, f); flags.append(f); assert r.get(16) == 0 and r.get(16) == 0; w.put(8, r.get(8))
    assert r.get(1) == 1
    wsetup = w.bytes()
    mode_bits = ilog(nmo - 1)
    modified = bs0 != bs1
    E = ">" if be else "<"
    data = struct.pack(E + "H", len(wsetup)) + wsetup
    audio_offset = len(data)
    for p in pk[3:]:
        if modified:
            # Wwise drops the packet-type bit and, for long blocks, the two window flags after the mode number
            br = BR(p); assert br.get(1) == 0
            mode = br.get(mode_bits); ww = BW(); ww.put(mode_bits, mode)
            if flags[mode]: br.get(1); br.get(1)
            while br.p < len(p) * 8: ww.put(1, br.get(1))
            q = ww.bytes()
            # (vgmstream copies packet_size - 1 whole bytes after the first: keep the length the decoder expects)
            q = q[:max(1, len(q))]
        else:
            q = p
        data += struct.pack(E + "H", len(q)) + q
    total = oggpk.packets(ogg)[-1][1]
    fmt = struct.pack(E + "HHIIHHH", 0xFFFF, channels, rate, 0, 0, 0, 0x30)
    fmt += struct.pack(E + "H", 0) + struct.pack(E + "I", 4 if channels == 1 else 3)   # (0x12 valid bits/0, 0x14 channel mask)
    extra = bytearray(0x30)
    struct.pack_into(E + "i", extra, 0x00, total)
    struct.pack_into(E + "II", extra, 0x10, 0, audio_offset)     # seek table 0 bytes, audio after the setup
    extra[0x28] = bs0; extra[0x29] = bs1                           # (vgmstream: +0x28 "blocksize_1_exp" -> ident low nibble)
    fmt += bytes(extra)
    def chunk(tag, body): return tag + struct.pack(E + "I", len(body)) + body
    body = b"WAVE" + chunk(b"fmt ", fmt) + chunk(b"data", data)
    return (b"RIFX" if be else b"RIFF") + struct.pack(E + "I", len(body)) + body

if __name__ == "__main__":
    a = sys.argv[1:]
    be = "--be" in a; which = 1
    if "--list" in a: which = int(a[a.index("--list") + 1])
    out = convert(a[0], open(a[1], "rb").read(), be, which)
    open(a[2], "wb").write(out)
