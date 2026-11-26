import os, struct
same = diff = 0
for f in sorted(os.listdir("elf")):
    if not f.endswith(".elf.wav"): continue
    k = f.split(".")[0]
    w = open("elf/" + f, "rb").read()
    # the RIFF's data chunk
    i = 12; data = None; fmt = None
    while i + 8 <= len(w):
        tag = w[i:i+4]; n = struct.unpack_from("<I", w, i + 4)[0]
        if tag == b"fmt ": fmt = struct.unpack_from("<HHIIHH", w, i + 8)
        if tag == b"data": data = w[i + 8:i + 8 + n]
        i += 8 + n + (n & 1)
    mine = open("elf/%s.mine.raw" % k, "rb").read()
    if data == mine: same += 1
    else: diff += 1; print("differ", k, fmt, len(data or b""), len(mine))
print("vgmstream-cli (the original build) vs the builder's decode: same %d, different %d" % (same, diff))
