"""ogg -> wem (ogg2wem.py) -> the builder's decode (wem_decode_test wem) == libvorbis on the ogg (wem_decode_test ogg)
usage: python -I test_wem.py <clips dir> <count> <exe>   (also 44.1 kHz re-encodes: two block sizes -> modified packets)"""
import os, sys, subprocess, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ogg2wem
d, count, exe = sys.argv[1], int(sys.argv[2]), sys.argv[3]
enc = os.path.join(os.path.dirname(exe), "cet_all.exe")
fs = sorted(f for f in os.listdir(d) if f.endswith(".ogg")); random.Random(3).shuffle(fs)
res = {}
for k, f in enumerate(fs[:count]):
    src = os.path.join(d, f)
    kinds = [("20k", src)]
    if k % 2 == 0:
        raw = subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-i", src, "-ar", "44100", "-ac", "1", "-f", "s16le", "-"], capture_output=True, check=True).stdout
        open("r44.raw", "wb").write(raw)
        subprocess.run([enc, "r44.raw", "44100", "1", "03766f726269730d0000004c61766636302e31362e313030010000001f000000656e636f6465723d4c61766336302e33312e313032206c6962766f7262697301", "o44.ogg", "notrim"], check=True, capture_output=True)
        kinds.append(("44k", "o44.ogg"))
    for name, o in kinds:
        for be in (True, False):
            for lst in (1, 0):
                key = "%s %s list%d" % (name, "BE" if be else "LE", lst)
                try:
                    open("t.wem", "wb").write(ogg2wem.convert("books.txt", open(o, "rb").read(), be, lst))
                except SystemExit as e:
                    res.setdefault(key, [0, 0, 0])[2] += 1; continue
                a = subprocess.run([exe, "wem", "t.wem", "a.raw"], capture_output=True)
                subprocess.run([exe, "ogg", o, "b.raw"], capture_output=True, check=True)
                same = a.returncode == 0 and open("a.raw", "rb").read() == open("b.raw", "rb").read()
                res.setdefault(key, [0, 0, 0])[0 if same else 1] += 1
for k, v in sorted(res.items()): print("%-18s same %d, different %d, not convertible %d" % (k, v[0], v[1], v[2]))
