"""The commentary pack's writer: the same bytes mod/gen/compak.py's build() and pack() make (version 4: "NMCP", the
index, the clip table, the clips; split in 20 MB parts), copied here so the rebuild needs nothing from mod/.
2026-11-14, commentary agent."""
import os, struct, hashlib

PART = 20 * 1024 * 1024
HEAD = 28

def build(rows, moves, clips):
    index = (rows + "#moves\n" + moves).encode()
    nrows = rows.count("\n")
    toff = HEAD + len(index); toff += -toff % 4
    blob = bytearray(b"NMCP" + struct.pack("<IIIIII", 4, len(clips), nrows, HEAD, len(index), toff))
    blob += index; blob += b"\0" * (toff - len(blob))
    tab, body, base = bytearray(), bytearray(), toff + 8 * len(clips)
    for c in clips:
        tab += struct.pack("<II", base + len(body), len(c)); body += c
    return bytes(blob + tab + body)

def write_parts(blob, out):
    parts = [blob[i:i + PART] for i in range(0, len(blob), PART)]
    for k, p in enumerate(parts): open(out + ("" if k == 0 else ".%d" % k), "wb").write(p)
    k = len(parts)
    while os.path.exists("%s.%d" % (out, k)): os.remove("%s.%d" % (out, k)); k += 1
    return len(parts), hashlib.sha1(blob).hexdigest()
