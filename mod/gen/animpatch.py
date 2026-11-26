# shift bone 1's absolute positions of an AKI animation (bytes) by (dx, dy, dz) anim units; ramp: (k0, k1) keys over which the shift grows 0 -> 1
import struct
def sm_dec(v, bits):
    s = v >> (bits - 1); m = v & ((1 << (bits - 1)) - 1); return -m if s else m
def sm_enc(x, bits):
    m = min(abs(int(round(x))), (1 << (bits - 1)) - 1); return ((1 << (bits - 1)) | m) if x < 0 else m
def shift(d, dx, dy, dz, ramp=None, start=4):
    d = bytearray(d); o = start; k = 0; nkeys = 0
    # count keys first
    def walk(cb):
        o = start; k = 0
        while o + 4 <= len(d):
            for b in range(19):
                if o + 4 > len(d): return k
                w = struct.unpack_from(">I", d, o)[0]; o += 4
                if w & 0x80000000:
                    if w & 0x40000000: o += 3
                    else:
                        if b == 0: cb(k, o)
                        o += 4
            k += 1
        return k
    nkeys = walk(lambda k, o: None)
    def cb(k, o):
        t = 1.0
        if ramp:
            k0, k1 = ramp
            t = 0.0 if k < k0 else (1.0 if k >= k1 else (k - k0) / float(k1 - k0))
        w = struct.unpack_from(">I", d, o)[0]
        x, y, z = sm_dec((w >> 21) & 0x7FF, 11), sm_dec((w >> 10) & 0x7FF, 11), sm_dec(w & 0x3FF, 10)
        x += dx * t; y += dy * t; z += dz * t
        nw = (sm_enc(x, 11) << 21) | (sm_enc(y, 11) << 10) | sm_enc(z, 10)
        struct.pack_into(">I", d, o, nw)
    walk(cb)
    return bytes(d), nkeys
