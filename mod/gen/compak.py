#!/usr/bin/env python3
"""The commentary pack, as files you can edit: unpack it, change what you like, pack it again.

mkcommentary.py made the pack from the sorted-out WWE '13 / Legends dialogue (its inputs weren't kept when the
project left the Linux sandbox); the pack itself has everything the exe's director reads, so it's the master now.

  python compak.py unpack <game folder or commentary.pak> <dir>
  python compak.py add <dir> <audio file> [more ...]      a new clip (any format ffmpeg reads): prints its number
  python compak.py pack <dir> <out commentary.pak>        (+ .pak.1, ... as the game wants them)

<dir> holds:
  lines.tsv   one row per line the director can play (the same clip may have several rows), tab-separated:
              clip  event  wrestler  sex  ms  weight  movekey  src  speaker  splice  text
                clip      its clip number (clips/NNNN.ogg)
                event     the director's event (bell, strike, finisher, kickout_2, name, elimination, filler, ...)
                wrestler  hex Superstar ID it's about ("v..": the one it's done to; "*" / "v*": whoever; "-": anyone)
                sex       M / F / - ;  ms: the clip's length;  weight: how likely (1, 0.5)
                movekey   the move it names (or -);  src: 0 WWE '13, 1 Legends of WrestleMania, 2 the AI voice
                speaker   J (Jim Ross) / K (Jerry Lawler) / M (either);  splice: lead-in/name join data (or -)
                text      what's said
  moves.tsv   move number -> name (the director matches move names in lines to moves)
  clips/      NNNN.ogg, mono 20 kHz Vorbis, numbered 0.. with no gaps
A row's fields are the director's own (exe\\WWFNoMercyRecomp\\src\\main\\commentary\\director.cpp reads them).
"""
import os, sys, struct, glob, hashlib, subprocess
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp

PART = 20 * 1024 * 1024
HEAD = 28
# (mkcommentary.py's: the clip's silence trimmed at both ends, mono, 20 kHz, Vorbis q0)
TRIM = "silenceremove=start_periods=1:start_threshold=-50dB,areverse,silenceremove=start_periods=1:start_threshold=-50dB,areverse"

def read_pack(path):
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

def unpack(src, d):
    rows, moves, clips, sha = read_pack(src)
    os.makedirs(os.path.join(d, "clips"), exist_ok=True)
    open(os.path.join(d, "lines.tsv"), "w", encoding="utf-8", newline="").write(rows)
    open(os.path.join(d, "moves.tsv"), "w", encoding="utf-8", newline="").write(moves)
    for i, c in enumerate(clips): open(os.path.join(d, "clips", "%04d.ogg" % i), "wb").write(c)
    print("%d lines, %d clips -> %s (pack sha1 %s)" % (rows.count("\n"), len(clips), d, sha))

def load_dir(d):
    rows = open(os.path.join(d, "lines.tsv"), encoding="utf-8", newline="").read()
    moves = open(os.path.join(d, "moves.tsv"), encoding="utf-8", newline="").read()
    rows = rows.replace("\r\n", "\n"); moves = moves.replace("\r\n", "\n")   # (if an editor wrote CRLF)
    if rows and not rows.endswith("\n"): rows += "\n"
    files = sorted(glob.glob(os.path.join(d, "clips", "*.ogg")))
    nums = [int(os.path.basename(f)[:-4]) for f in files]
    if nums != list(range(len(nums))): raise SystemExit("clips/ must be 0000.ogg .. %04d.ogg with no gaps" % (len(nums) - 1))
    for n, line in enumerate(rows.splitlines(), 1):
        f = line.split("\t")
        if len(f) != 11: raise SystemExit("lines.tsv line %d: %d fields, not 11" % (n, len(f)))
        if not (0 <= int(f[0]) < len(files)): raise SystemExit("lines.tsv line %d: no clip %s" % (n, f[0]))
    return rows, moves, [open(f, "rb").read() for f in files]

def pack(d, out):
    rows, moves, clips = load_dir(d)
    blob = build(rows, moves, clips)
    parts = [blob[i:i + PART] for i in range(0, len(blob), PART)]
    for k, p in enumerate(parts): open(out + ("" if k == 0 else ".%d" % k), "wb").write(p)
    k = len(parts)
    while os.path.exists("%s.%d" % (out, k)): os.remove("%s.%d" % (out, k)); k += 1
    print("pack", len(blob), "bytes in", len(parts), "parts; sha1", hashlib.sha1(blob).hexdigest())

def add(d, files):
    n = len(glob.glob(os.path.join(d, "clips", "*.ogg")))
    for f in files:
        out = os.path.join(d, "clips", "%04d.ogg" % n)
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", f, "-ac", "1", "-af", TRIM, "-ar", "20000",
                        "-c:a", "libvorbis", "-q:a", "0", out], check=True)
        dur = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", out],
                             capture_output=True, text=True, check=True).stdout.strip()
        print("clip %d  ms %d  <- %s   (add a row for it to lines.tsv)" % (n, int(float(dur) * 1000), f))
        n += 1

if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "unpack": unpack(a[1], a[2])
    elif len(a) == 3 and a[0] == "pack": pack(a[1], a[2])
    elif len(a) >= 3 and a[0] == "add": add(a[1], a[2:])
    else: print(__doc__)
