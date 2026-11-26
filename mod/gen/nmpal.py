"""The ported Superstars' pack (build/wmport.bin, src/wmport_files.h) with every palette its models name made No
Mercy's size (wmport.py nm_palettes: 0x200 bytes, their own colours then unused ones) -- for a pack made before
wmport.py did it itself, without making the models again (their captures from WrestleMania 2000 / Revenge).

    python gen/nmpal.py          the pack and its index rewritten (the old ones kept as .before_nmpal)
    python gen/nmpal.py --check  only says what it would do
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import os, re, sys, pickle, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from wmport import nm_palettes, OUT_BIN, OUT_H, OUT_PKL, WMP_FILE0, NM_PAL_SIZE

def main():
    check = "--check" in sys.argv
    h = open(OUT_H).read()
    blob = open(OUT_BIN, "rb").read()
    models = pickle.load(open(OUT_PKL, "rb"))
    table = re.search(r"static const u32 wmp_files\[WMP_FILES_N\]\[2\] = \{[^\n]*\n(.*?)\n\};", h, re.S)
    ents = [(int(o, 16), int(n)) for o, n in re.findall(r"\{ 0x([0-9A-Fa-f]+), (\d+) \}", table.group(1))]
    assert len(ents) == int(re.search(r"#define WMP_FILES_N (\d+)", h).group(1))
    data = [blob[o:o + n] for o, n in ents]
    before = [len(d) for d in data]
    n = nm_palettes(data, models)
    grown = [(WMP_FILE0 + i, before[i], len(data[i])) for i in range(len(data)) if len(data[i]) != before[i]]
    print("%d models, %d files; %d palettes made %d bytes:" % (len(models), len(data), n, NM_PAL_SIZE))
    for f, a, b in grown: print("  file 0x%04X: %d -> %d" % (f, a, b))
    out = bytearray(); offs = []
    for b in data:
        offs.append((len(out), len(b))); out += b + bytes(-len(b) % 8)
    print("pack: %d -> %d bytes" % (len(blob), len(out)))
    if check: return
    for p in (OUT_BIN, OUT_H): shutil.copyfile(p, p + ".before_nmpal")
    open(OUT_BIN, "wb").write(out)
    lines = "\n".join("    { 0x%06X, %d }," % o for o in offs)
    h = h[:table.start(1)] + lines + h[table.end(1):]
    h = re.sub(r"#define WMP_PACK_SIZE 0x[0-9A-Fa-f]+u", "#define WMP_PACK_SIZE 0x%Xu" % len(out), h)
    open(OUT_H, "w").write(h)
    print("written:", OUT_BIN, OUT_H)

if __name__ == "__main__":
    main()
