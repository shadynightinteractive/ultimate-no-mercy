#!/usr/bin/env python3
"""pack_drop.py (2026-11-13, movelib2): the mod's WrestleMania 2000 / Revenge pack without the Revenge move animations.

The pack (gen/wmport.py: build\\wmport.bin, its index src\\wmport_files.h) held gen/revanims.py's animation pairs, one copy per
ported Superstar (rev_move_anims.h: 756 files, 378 pairs of which 125 differ), at fixed game file numbers. They are the
built-in move library's now (movelib_revenge.bin, served by the exe with numbers given while a match plays them), so the
pack keeps their numbers -- every other file's number stays as it is: nothing else changes -- with no bytes (size 0), as
gen/wmport.py makes it with movelib2's change (each pair's two files empty). Checks every other file is byte for byte
the old pack's.
    python pack_drop.py <mod folder>       (writes <mod>\\build\\wmport.bin and <mod>\\src\\wmport_files.h in place; the old
                                            ones kept as wmport.bin.with_rma / wmport_files.h.with_rma)"""
import os, re, shutil, sys

def main():
    mod = sys.argv[1]
    hp, bp = os.path.join(mod, "src", "wmport_files.h"), os.path.join(mod, "build", "wmport.bin")
    for p in (hp, bp):
        if not os.path.exists(p + ".with_rma"): shutil.copyfile(p, p + ".with_rma")
    h = open(hp + ".with_rma", newline="").read()
    pack = open(bp + ".with_rma", "rb").read()
    rma_h = open(os.path.join(mod, "src", "rev_move_anims.h")).read()
    first = int(re.search(r"#define RMA_FIRST 0x([0-9A-F]+)u", rma_h).group(1), 16)
    n_rma = int(re.search(r"#define RMA_FILES_N (\d+)", rma_h).group(1))
    f0 = int(re.search(r"#define WMP_FILE0 0x([0-9A-F]+)u", h).group(1), 16)
    head, rest = h.split("wmp_files[WMP_FILES_N][2] = {", 1)
    table, tail = rest.split("};", 1)
    rows = re.findall(r"    \{ 0x([0-9A-F]+), (\d+) \},", table)
    files = [pack[int(o, 16):int(o, 16) + int(n)] for o, n in rows]
    drop = set(range(first - f0, first - f0 + n_rma))
    blob = bytearray(); offs = []
    for i, b in enumerate(files):
        if i in drop: b = b""
        offs.append((len(blob), len(b))); blob += b + bytes(-len(b) % 8)     # (gen/wmport.py's own packing)
    # check: every file kept is the old one, byte for byte
    bad = sum(1 for i, (o, n) in enumerate(offs) if i not in drop and bytes(blob[o:o + n]) != files[i])
    nl = "\r\n" if "\r\n" in h else "\n"
    first_line = table.split(nl, 1)[0]
    new_table = first_line + nl + "".join("    { 0x%06X, %d },%s" % (o, n, nl) for o, n in offs)
    head = re.sub(r"#define WMP_PACK_SIZE 0x[0-9A-F]+u", "#define WMP_PACK_SIZE 0x%Xu" % len(blob), head)
    open(hp, "w", newline="").write(head + "wmp_files[WMP_FILES_N][2] = {" + new_table + "};" + tail)
    open(bp, "wb").write(blob)
    print("pack: %d files (%d of them the Revenge move animations, now empty), %d -> %d bytes (%.2f MB dropped); %d kept files different"
          % (len(files), len(drop), len(pack), len(blob), (len(pack) - len(blob)) / 1e6, bad))
    return bad

if __name__ == "__main__":
    sys.exit(1 if main() else 0)
