"""VCDIFF (xdelta3) patches applied in memory (2026-11-13, modcheck).

A mod often comes as an xdelta against the user's clean ROM (No Mercy Plus V3, Badd Blood). The checker needs the patched
ROM to diff its tables, and the rules say a ROM is never copied: so the patch is applied here, in memory, and nothing is
written. RFC 3284 with xdelta3's one addition (an Adler-32 of each target window, checked when present). Secondary
compression (xdelta3 -S djw/fgk/lzma) is not handled: such a patch stops with a plain message rather than a wrong ROM.
"""
import zlib

# the default code table (RFC 3284 section 5.6): 256 entries of (type1, size1, mode1, type2, size2, mode2)
NOOP, ADD, RUN, COPY = 0, 1, 2, 3
def _default_table():
    t = [(RUN, 0, 0, NOOP, 0, 0)]
    t += [(ADD, s, 0, NOOP, 0, 0) for s in [0] + list(range(1, 18))]
    for m in range(9):
        t.append((COPY, 0, m, NOOP, 0, 0))
        t += [(COPY, s, m, NOOP, 0, 0) for s in range(4, 19)]
    for m in range(6):
        for a in range(1, 5):
            for c in range(4, 7): t.append((ADD, a, 0, COPY, c, m))
    for m in range(6, 9):
        for a in range(1, 5): t.append((ADD, a, 0, COPY, 4, m))
    for m in range(9): t.append((COPY, 4, m, ADD, 1, 0))
    assert len(t) == 256
    return t
TABLE = _default_table()

class PatchError(Exception): pass

def _varint(b, p):
    v = 0
    while True:
        c = b[p]; p += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80: return v, p

def apply(source, patch):
    """source bytes + VCDIFF patch bytes -> target bytes"""
    b = patch
    if b[:3] != b"\xd6\xc3\xc4": raise PatchError("not a VCDIFF / xdelta3 patch")
    hi = b[4]; p = 5
    if hi & 0x01: raise PatchError("this patch uses secondary compression (xdelta3 -S): not handled -- make it with -S none")
    if hi & 0x02: raise PatchError("this patch has its own code table: not handled")
    if hi & 0x04:                                       # (xdelta3's application header: the file names it was made from)
        n, p = _varint(b, p); p += n
    out = bytearray()
    while p < len(b):
        wi = b[p]; p += 1
        seg = None
        if wi & 0x03:
            slen, p = _varint(b, p); spos, p = _varint(b, p)
            seg = source[spos:spos + slen] if wi & 0x01 else bytes(out[spos:spos + slen])
        dlen, p = _varint(b, p)                         # (the delta's length)
        tlen, p = _varint(b, p)
        di = b[p]; p += 1
        if di: raise PatchError("this patch's window is compressed (xdelta3 -S): not handled")
        alen, p = _varint(b, p); ilen, p = _varint(b, p); clen, p = _varint(b, p)
        cksum = None
        if wi & 0x04: cksum = int.from_bytes(b[p:p + 4], "big"); p += 4
        data = b[p:p + alen]; p += alen
        inst = b[p:p + ilen]; p += ilen
        addr = b[p:p + clen]; p += clen
        win = bytearray(); dp = ip = ap = 0
        slen_ = len(seg) if seg is not None else 0
        near = [0] * 4; ni = 0; same = [0] * (3 * 256)
        while ip < len(inst):
            code = inst[ip]; ip += 1
            for typ, size, mode in (TABLE[code][0:3], TABLE[code][3:6]):
                if typ == NOOP: continue
                if size == 0: size, ip = _varint(inst, ip)
                if typ == ADD:
                    win += data[dp:dp + size]; dp += size
                elif typ == RUN:
                    win += data[dp:dp + 1] * size; dp += 1
                else:
                    here = slen_ + len(win)
                    if mode == 0: a, ap = _varint(addr, ap)
                    elif mode == 1: v, ap = _varint(addr, ap); a = here - v
                    elif mode < 6: v, ap = _varint(addr, ap); a = near[mode - 2] + v
                    else: a = same[(mode - 6) * 256 + addr[ap]]; ap += 1
                    near[ni] = a; ni = (ni + 1) % 4; same[a % (3 * 256)] = a
                    if a + size <= slen_: win += seg[a:a + size]
                    else:                                # (from the window being made, maybe overlapping itself)
                        for k in range(size):
                            q = a + k
                            win.append(seg[q] if q < slen_ else win[q - slen_])
        if len(win) != tlen: raise PatchError("a window came out %d bytes, the patch says %d" % (len(win), tlen))
        if cksum is not None and zlib.adler32(bytes(win)) != cksum:
            raise PatchError("a window's Adler-32 doesn't match: the patch isn't for this ROM")
        out += win
    return bytes(out)
