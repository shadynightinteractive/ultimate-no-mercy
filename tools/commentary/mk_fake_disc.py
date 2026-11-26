"""A made-up 'WWE 13 disc folder' for testing the builder end to end without the real disc: one big-endian Wwise package
(AKPK) whose streamed .wem files are the developer's pack clips repackaged as Wwise Vorbis (ogg2wem), under the recipe's
media ids. Decoding gives exactly those clips' PCM, so the builder's clips come out as re-encodes of the pack's clips
('equivalent', not 'identical' -- the real disc's lines are the originals, before the first encode).
usage: python -I mk_fake_disc.py <recipe dir> <pack> <out folder> [limit]"""
import os, sys, struct, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ogg2wem
R, P, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
lim = int(sys.argv[4]) if len(sys.argv) > 4 else 10**9
blob = b"".join(open(P + s, "rb").read() for s in ("", ".1", ".2", ".3") if os.path.exists(P + s))
ver, nd, nl, io, il, to = struct.unpack_from("<6I", blob, 4)
by = {}
for i in range(nd):
    off, ln = struct.unpack_from("<II", blob, to + 8 * i); c = blob[off:off + ln]; by[hashlib.sha1(c).hexdigest()] = c
t = open(os.path.join(R, "clips.tsv"), encoding="utf-8").read().splitlines(); cols = t[0].split("\t")
clips = [dict(zip(cols, l.split("\t"))) for l in t[1:]]
wems = {}
for c in clips:
    if c["kind"] != "w13" or len(wems) >= lim: continue
    wems[int(c["source"])] = ogg2wem.convert("books.txt", by[c["sha1"]], True, 1)
# AKPK, big-endian: header, language map (1: English(US) = id 0), empty bank table, stream table, then the files
lang_name = "english(us)".encode("utf-16-be") + b"\0\0"
lmap = struct.pack(">III", 1, 12, 0) + lang_name
lmap += b"\0" * (-len(lmap) % 4)
bnk = struct.pack(">I", 0)
ids = sorted(wems)
stm_len = 4 + 20 * len(ids)
hsize = 16 + len(lmap) + len(bnk) + stm_len
pos = (8 + hsize + 2047) & ~2047
entries = []; body = bytearray()
for i in ids:
    start_block = pos // 2048
    entries.append(struct.pack(">IIIII", i, 2048, len(wems[i]), start_block, 0))
    body += wems[i]; pos += len(wems[i]); pad = -pos % 2048; body += b"\0" * pad; pos += pad
stm = struct.pack(">I", len(ids)) + b"".join(entries)
head = b"AKPK" + struct.pack(">IIIII", hsize, 1, len(lmap), len(bnk), stm_len) + lmap + bnk + stm
head += b"\0" * (((8 + hsize + 2047) & ~2047) - len(head))
d = os.path.join(OUT, "files", "Audio", "English(US)")
os.makedirs(d, exist_ok=True)
open(os.path.join(d, "Speech.pck"), "wb").write(head + body)
os.makedirs(os.path.join(OUT, "sys"), exist_ok=True)
open(os.path.join(OUT, "sys", "boot.bin"), "wb").write(b"ZTSTW1" + bytes(26))
print("wems", len(wems), "package bytes", len(head) + len(body))
