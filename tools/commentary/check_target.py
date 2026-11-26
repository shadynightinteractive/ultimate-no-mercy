import sys, hashlib, struct
sys.path.insert(0, '.')
from compak_build import build
R = sys.argv[1]; P = sys.argv[2]
blob = b"".join(open(P + s, "rb").read() for s in ("", ".1", ".2", ".3"))
print("source pack sha1", hashlib.sha1(blob).hexdigest())
ver, nd, nl, io, il, to = struct.unpack_from("<6I", blob, 4)
by = {}
for i in range(nd):
    off, ln = struct.unpack_from("<II", blob, to + 8 * i); c = blob[off:off + ln]; by[hashlib.sha1(c).hexdigest()] = c
t = open(R + "/clips.tsv", encoding="utf-8").read().splitlines(); cols = t[0].split("\t")
clips = [dict(zip(cols, l.split("\t"))) for l in t[1:]]
miss = [c["clip"] for c in clips if c["sha1"] not in by]
print("recipe clips", len(clips), "found in source pack", len(clips) - len(miss), "missing", miss[:10])
import rebuild
lines = open(R + "/lines.tsv", encoding="utf-8", newline="").read(); moves = rebuild.recipe_moves(R)   # (moves.tsv, else from the ROM)
out = build(lines, moves, [by[c["sha1"]] for c in clips])
print("rebuilt from it:", hashlib.sha1(out).hexdigest(), len(out))
