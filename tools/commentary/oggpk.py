"""Ogg pages and packets, enough to compare two Vorbis files packet by packet and to write one back
(2026-11-14, commentary agent). Lets a rebuild keep the reference pack's exact header text (the encoder tag differs
between ffmpeg versions) while checking that the audio packets themselves are the same.
"""
import struct

def pages(b):
    """-> list of (header type, granule, serial, seqno, [segment lacing], body)"""
    out, i = [], 0
    while i < len(b):
        if b[i:i + 4] != b"OggS": raise ValueError("not an Ogg page at %d" % i)
        htype, gran, serial, seq, _crc, nseg = struct.unpack_from("<BqIIIB", b, i + 5)
        lace = list(b[i + 27:i + 27 + nseg]); n = sum(lace)
        body = b[i + 27 + nseg:i + 27 + nseg + n]
        out.append((htype, gran, serial, seq, lace, body))
        i += 27 + nseg + n
    return out

def packets(b):
    """-> list of (packet bytes, granule of the page it ends on)"""
    out, cur = [], b""
    for htype, gran, serial, seq, lace, body in pages(b):
        p = 0
        for l in lace:
            cur += body[p:p + l]; p += l
            if l < 255: out.append((cur, gran)); cur = b""
    return out

_T = []
for i in range(256):
    r = i << 24
    for _ in range(8): r = ((r << 1) ^ 0x04C11DB7) if r & 0x80000000 else r << 1
    _T.append(r & 0xFFFFFFFF)

def crc(b):
    c = 0
    for x in b: c = ((c << 8) ^ _T[((c >> 24) ^ x) & 0xFF]) & 0xFFFFFFFF
    return c

def page(htype, gran, serial, seq, lace, body):
    h = b"OggS" + struct.pack("<BBqIIIB", 0, htype, gran, serial, seq, 0, len(lace)) + bytes(lace)
    c = crc(h + body)
    return h[:22] + struct.pack("<I", c) + h[26:] + body

def replace_comment(b, comment_packet, serial=None):
    """the same Ogg file with Vorbis's comment packet (packet 2) replaced; pages re-laced as libogg would for the
    header pages (ident page alone, then comment + setup together), the audio pages untouched but renumbered"""
    pg = pages(b)
    pk = packets(b)
    ident, setup = pk[0][0], pk[2][0]
    serial = pg[0][2] if serial is None else serial
    def lacing(p):
        l = [255] * (len(p) // 255) + [len(p) % 255]
        return l
    out = [page(2, 0, serial, 0, lacing(ident), ident)]
    # (libogg flushes the comment and setup packets onto as few pages as 255 lacing values allow)
    hdr = comment_packet + setup; lace = lacing(comment_packet) + lacing(setup)
    seq = 1; chunks = []
    while lace:
        take = lace[:255]; n = sum(take)
        chunks.append((take, hdr[:n])); hdr = hdr[n:]; lace = lace[255:]
    for k, (l, body) in enumerate(chunks):
        out.append(page(1 if k and chunks[k - 1][0][-1] == 255 else 0, 0 if k < len(chunks) - 1 else 0, serial, seq, l, body))
        seq += 1
    # the audio pages: everything after the last header page of the original
    hdr_pages = 0; n_hdr = 0
    for htype, gran, s, sq, l, body in pg:
        hdr_pages += 1; n_hdr += sum(1 for x in l if x < 255)
        if n_hdr >= 3: break
    for htype, gran, s, sq, l, body in pg[hdr_pages:]:
        out.append(page(htype, gran, serial, seq, l, body)); seq += 1
    return b"".join(out)
