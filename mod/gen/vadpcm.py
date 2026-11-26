"""N64 VADPCM (the audio library's ADPCM: the game's instruments) through the SDK's own tools
(/tmp/devbox-0/sdk: tabledesign, vadpcm_enc, vadpcm_dec, built from n64decomp/sm64 tools/sdk-tools).
A book is the wave table's ALADPCMBook as the game keeps it: s32 order, s32 npredictors, s16 [order * 8
* npredictors] (order 2, 4 predictors: 0x88 bytes)."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import struct, subprocess, tempfile, os
import numpy as np
SDK = nmp("/tmp/devbox-0/sdk")
if os.name == "nt":
    # The SDK's tools are Linux programs: on Windows they run through WSL (Ubuntu), the paths given as /mnt/ ones
    # (a Windows build of them from the SDK's sources fails: its audiofile library; revport/ports/PORTS.md)
    import re as _re, types as _types
    _sp = subprocess
    def _wsl(args):
        def conv(a):
            a = str(a).replace("\\", "/")
            m = _re.match(r"^([A-Za-z]):/(.*)$", a)
            return "/mnt/%s/%s" % (m.group(1).lower(), m.group(2)) if m else a
        return ["wsl.exe", "-e"] + [conv(a) for a in args]
    subprocess = _types.SimpleNamespace(check_output=lambda a, **k: _sp.check_output(_wsl(a), **k),
                                        check_call=lambda a, **k: _sp.check_call(_wsl(a), **k))

def _ext80(rate):
    """an 80-bit IEEE extended (AIFF's sample rate)"""
    import math
    e, m = 16383, float(rate)
    if m == 0: return bytes(10)
    exp = int(math.floor(math.log2(m))); mant = int(m / 2 ** exp * (1 << 63))
    return struct.pack(">HQ", e + exp, mant)
def write_aiff(path, pcm, rate):
    pcm = np.asarray(pcm, dtype=">i2")
    comm = struct.pack(">hIh", 1, len(pcm), 16) + _ext80(rate)
    ssnd = struct.pack(">II", 0, 0) + pcm.tobytes()
    body = b"AIFF" + b"COMM" + struct.pack(">I", len(comm)) + comm + b"SSND" + struct.pack(">I", len(ssnd)) + ssnd
    open(path, "wb").write(b"FORM" + struct.pack(">I", len(body)) + body)

def _chunks(b):
    assert b[:4] == b"FORM"
    o = 12; out = []
    while o + 8 <= len(b):
        cid, n = b[o:o + 4], struct.unpack_from(">I", b, o + 4)[0]
        out.append((cid, b[o + 8:o + 8 + n])); o += 8 + n + (n & 1)
    return out

def table_text(book):
    order, npred = struct.unpack_from(">ii", book, 0)
    v = struct.unpack_from(">%dh" % (order * 8 * npred), book, 8)
    rows = ["%d" % order, "%d" % npred] + [" ".join("%d" % x for x in v[i:i + 8]) for i in range(0, len(v), 8)]
    return ("\n".join(rows) + "\n").encode()

def encode(pcm, rate, loop=None, book_in=None):
    """PCM (int16 mono) -> (book 0x88, ADPCM bytes, samples). book_in: encode with this book"""
    with tempfile.TemporaryDirectory() as d:
        a = os.path.join(d, "in.aiff"); t = os.path.join(d, "t.table"); c = os.path.join(d, "o.aifc")
        pcm = np.asarray(pcm, dtype=np.int16)
        if loop:   # the loop as AIFF markers + INST (vadpcm_enc keeps a loop's ADPCM state)
            pass
        write_aiff(a, pcm, rate)
        tab = table_text(book_in) if book_in else subprocess.check_output([SDK + "/tabledesign_bin", "-s", "2", a])
        open(t, "wb").write(tab)
        subprocess.check_call([SDK + "/vadpcm_enc", "-c", t, a, c])
        b = open(c, "rb").read()
    book = data = None
    for cid, x in _chunks(b):
        if cid == b"APPL" and x[4:5] == b"\x0b" and x[5:16] == b"VADPCMCODES":
            ver, order, npred = struct.unpack_from(">hhh", x, 16)
            coefs = x[22:22 + 2 * order * 8 * npred]
            book = struct.pack(">ii", order, npred) + coefs
        if cid == b"SSND": data = x[8:]
    assert book and data is not None
    return book, data, len(pcm)

def decode(data, book, n=None):
    """ADPCM bytes + a book -> PCM int16 (n samples)"""
    order, npred = struct.unpack_from(">ii", book, 0)
    coefs = book[8:8 + 2 * order * 8 * npred]
    n = n if n is not None else len(data) // 9 * 16
    comm = struct.pack(">hIh", 1, n, 16) + _ext80(22050) + b"VAPC" + bytes([11]) + b"VADPCM ~4-1"
    appl = b"stoc" + bytes([11]) + b"VADPCMCODES" + struct.pack(">hhh", 1, order, npred) + coefs
    ssnd = struct.pack(">II", 0, 0) + bytes(data)
    def ck(i, x): return i + struct.pack(">I", len(x)) + x + (b"\0" if len(x) & 1 else b"")
    body = b"AIFC" + ck(b"FVER", struct.pack(">I", 0xA2805140)) + ck(b"COMM", comm) + ck(b"APPL", appl) + ck(b"SSND", ssnd)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.aifc"); open(p, "wb").write(b"FORM" + struct.pack(">I", len(body)) + body)
        raw = subprocess.check_output([SDK + "/vadpcm_dec", p])
    return np.frombuffer(raw, dtype=">i2")[:n].astype(np.int16)
