#!/usr/bin/env python3
r"""check_reader.py (nmhandler, 2026-11-13): the shared AKI file reader on the user's No Mercy ROM against the project's earlier
extraction (data\extracted\nmfiles\f\*.bin), every file: python check_reader.py -> writes ..\work\reader_check.txt"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
os.makedirs(os.path.join(HERE, "work"), exist_ok=True)
sys.path.insert(0, HERE)
from handlers.nomercy import game as G
nm = G.NoMercy()
ex = os.path.join(G.DEV, "data", "extracted", "nmfiles", "f")
same = differ = missing = 0
first = []
for k in range(1, nm.files.count() + 1):
    p = os.path.join(ex, "%04x.bin" % k)
    if not os.path.exists(p): missing += 1; continue
    if open(p, "rb").read() == nm.files(k): same += 1
    else:
        differ += 1
        if len(first) < 10: first.append(k)
    nm.files.cache.clear()
out = "No Mercy ROM sha1 %s: %d files; %d the same as data\\extracted\\nmfiles, %d differ%s, %d not extracted there" % (
    nm.files.sha1, nm.files.count(), same, differ, (" (first: %s)" % ", ".join("0x%04X" % f for f in first)) if first else "", missing)
print(out)
open(os.path.join(HERE, "work", "reader_check.txt"), "w", newline="\n").write(out + "\n")
