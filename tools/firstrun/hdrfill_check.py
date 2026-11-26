#!/usr/bin/env python3
"""hdrfill_check.py: the game's fill of the extras holes from the headers' text (C++, romimport\\ri_hdrfill.cpp) against the
build's modextra.bin, byte for byte (phase3 p3hdrfill, 2026-11-19).

  python tools/firstrun/hdrfill_check.py --ri <ri_cli.exe> --plan <out of mkplan/hdrlayout> [--modextra <modextra.bin>]
                                         [--headers mod/src] [--source mod/src]

ri_cli.exe is tools/romimport/native/build_ri.ps1's. It runs `ri_cli hdrfill` on <plan>/firstrun/hdrfill.bin with the
CURRENT generated headers (--headers; the mod's own source files, hands.c, from --source) -- the pointer words from the mod's
base image, mod_binary.holes and plan.bin beside hdrfill.bin (relclean 2026-11-19) -- and compares the modextra.bin it
writes with the build's (default <plan>/modextra.bin, as mkplan writes it). A COMPUTED hole (arcade_extras_in: the extras'
source bits, which the first-start builder works out from the player's ROMs) is given the build's own value, read from that
modextra.bin -- this checks the text's holes, not the builder's bits.

Exit 0 when every hole is the same; otherwise each hole that differs is named (a header newer than the build it is checked
against differs too: then rebuild the mod, mkplan and hdrlayout from the same headers).
"""
import argparse, hashlib, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import hdrlayout   # noqa: E402


def read_modextra(path):
    """modextra.bin -> [(start, bytes)] (and its SHA-256 checked)"""
    x = open(path, 'rb').read()
    assert x[:8] == b'UNMXTRA1', path + ': not modextra.bin'
    assert hashlib.sha256(x[44:]).digest() == x[8:40], path + ': its SHA-256 is not its payload\'s'
    n = struct.unpack_from('<I', x, 40)[0]; at = 44; out = []
    for _ in range(n):
        s, ln = struct.unpack_from('<II', x, at); at += 8
        out.append((s, x[at:at + ln])); at += ln
    assert at == len(x), path + ': bytes after its end'
    return out


def read_holes(path):
    """hdrfill.bin's holes: [(start, size, flags, header, symbol)]"""
    b = open(path, 'rb').read()
    assert b[:8] == hdrlayout.MAGIC, path + ': not hdrfill.bin'
    ver, img, n = struct.unpack_from('<III', b, 8); at = 20; out = []
    assert ver == hdrlayout.VERSION, path + ': hdrfill.bin version %d (this tool reads %d)' % (ver, hdrlayout.VERSION)
    for _ in range(n):
        s, size, sym_at, sym_size, flags = struct.unpack_from('<IIiII', b, at); at += 20
        names = []
        for _k in range(2):
            ln = b[at]; names.append(b[at + 1:at + 1 + ln].decode('ascii')); at += 1 + ln
        nr = struct.unpack_from('<I', b, at)[0]; at += 4
        for _r in range(nr):
            kind, size_, leaf0, count, off0, stride = struct.unpack_from('<B3xIIIII', b, at); at += 24      # (version 2: no PTR values)
        out.append((s, size, flags, names[0], names[1]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--ri', required=True, help='ri_cli.exe (tools/romimport/native/build_ri.ps1)')
    ap.add_argument('--plan', required=True, help='the folder mkplan.py / hdrlayout.py wrote (firstrun/hdrfill.bin in it)')
    ap.add_argument('--modextra', help='the build\'s modextra.bin (default: <plan>/modextra.bin)')
    ap.add_argument('--headers', default=os.path.join(SRC, 'mod', 'src'), help='the generated headers (current text)')
    ap.add_argument('--source', default=os.path.join(SRC, 'mod', 'src'), help='the mod\'s source files (hands.c)')
    a = ap.parse_args()
    hf = os.path.join(a.plan, 'firstrun', 'hdrfill.bin')
    want = read_modextra(a.modextra or os.path.join(a.plan, 'modextra.bin'))
    holes = read_holes(hf)
    if [h[0] for h in holes] != [s for s, _ in want]:
        print('hdrfill_check: hdrfill.bin and modextra.bin are not the same build\'s holes'); sys.exit(1)
    args = [os.path.abspath(a.ri), 'hdrfill', hf, os.path.join(a.plan, 'modextra_native.bin'), a.headers, a.source]
    for (s, size, flags, header, sym), (_, data) in zip(holes, want):
        if flags & 1: args += ['--computed', '%s=%s' % (sym, data.hex())]   # (the build's own: the builder's to compute)
    r = subprocess.run(args, capture_output=True, text=True)
    print(r.stdout.strip()); print(r.stderr.strip())
    if r.returncode: print('hdrfill_check: ri_cli failed'); sys.exit(1)
    got = read_modextra(os.path.join(a.plan, 'modextra_native.bin'))
    same = diff = 0; nb = 0
    for (s, size, flags, header, sym), (_, w), (_, g) in zip(holes, want, got):
        nb += len(w)
        if g == w: same += 1; continue
        diff += 1
        k = next((i for i in range(min(len(g), len(w))) if g[i] != w[i]), min(len(g), len(w)))
        print('DIFFERENT %s (%s) at +%d of %d bytes' % (sym, header, k, len(w)))
    whole = open(os.path.join(a.plan, 'modextra_native.bin'), 'rb').read() == open(a.modextra or os.path.join(a.plan, 'modextra.bin'), 'rb').read()
    print('hdrfill_check: %d holes (%d bytes): %d SAME, %d DIFFERENT; modextra.bin %s' % (
        len(holes), nb, same, diff, 'byte for byte the build\'s' if whole else 'DIFFERENT'))
    sys.exit(0 if whole and not diff else 1)


if __name__ == '__main__':
    main()
