"""Read the commentary pack (format: mod/gen/compak.py's, version 4) without importing anything from mod/.

2026-11-14 (commentary agent): read-only; the same layout compak.read_pack reads.
"""
import os, struct, hashlib

def read_pack(path):
    """-> (rows text, moves text, [clip bytes], sha1 of the whole pack)"""
    base = os.path.join(path, "commentary.pak") if os.path.isdir(path) else path
    blob, k = bytearray(open(base, "rb").read()), 1
    while os.path.exists("%s.%d" % (base, k)):
        blob += open("%s.%d" % (base, k), "rb").read(); k += 1
    magic, (ver, nclips, nrows, head, ilen, toff) = blob[:4], struct.unpack_from("<IIIIII", blob, 4)
    if magic != b"NMCP" or ver != 4: raise SystemExit("not a commentary pack (version 4)")
    index = blob[head:head + ilen].decode()
    clips = []
    for i in range(nclips):
        off, n = struct.unpack_from("<II", blob, toff + 8 * i); clips.append(bytes(blob[off:off + n]))
    rows, _, moves = index.partition("\n#moves\n")
    assert len(rows.split("\n")) == nrows, "row count"
    return rows + "\n", moves, clips, hashlib.sha1(blob).hexdigest()

def rows_of(text):
    """lines.tsv text -> list of field lists (11 fields each)"""
    return [l.split("\t") for l in text.splitlines() if l]
