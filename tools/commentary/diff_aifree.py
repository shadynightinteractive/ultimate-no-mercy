import sys, hashlib, struct, collections
R = sys.argv[1]; P = sys.argv[2]
blob = b"".join(open(P + s, "rb").read() for s in ("", ".1", ".2", ".3"))
ver, nd, nl, io, il, to = struct.unpack_from("<6I", blob, 4)
sha = []
for i in range(nd):
    off, ln = struct.unpack_from("<II", blob, to + 8 * i); sha.append(hashlib.sha1(blob[off:off + ln]).hexdigest())
idx = blob[io:io + il].decode("utf-8").split("#moves\n")
rows = [l.split("\t") for l in idx[0].splitlines()]
t = open(R + "/clips.tsv", encoding="utf-8").read().splitlines(); cols = t[0].split("\t")
want = set(dict(zip(cols, l.split("\t")))["sha1"] for l in t[1:])
kept = [r for r in rows if sha[int(r[0])] in want]; gone = [r for r in rows if sha[int(r[0])] not in want]
print("AI-free pack: clips", nd, "rows", len(rows), "| kept rows", len(kept), "dropped rows", len(gone), "dropped clips", len(set(r[0] for r in gone)))
print("src of dropped rows", collections.Counter(r[7] for r in gone), "src of kept rows", collections.Counter(r[7] for r in kept))
print("moves section equal to recipe:", idx[1] == open(R + "/moves.tsv", encoding="utf-8", newline="").read())
for r in gone:
    if r[7] == '0': print("\t".join(r))
