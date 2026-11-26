import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# The PC port's input: the six-man ROM (full layout) with the rewrites' blob in the ROM's free tail (BLOB_ROM ->
# vram 0x80780000), and the [[section]] for syms/dump.toml that lists the blob's functions.
#   mkport.py SIX_ROM OUT_ROM OUT_TOML
import sys, subprocess
BLOB_ROM, VRAM = 0x1FF0000, 0x80780000
rom = bytearray(open(sys.argv[1], 'rb').read())
import os
BLOB_DIR = os.environ.get('SIX_BLOB') or nmp('/home/devbox/work/six/rw/out/full')   # (SIX_BLOB: a wider build's own blob)
B = os.path.join(BLOB_DIR, 'blob')
blob = open(B + '.bin', 'rb').read()
assert all(b in (0, 0xFF) for b in rom[BLOB_ROM:BLOB_ROM + len(blob)]), 'ROM tail not free'
rom[BLOB_ROM:BLOB_ROM + len(blob)] = blob
open(sys.argv[2], 'wb').write(rom)
# .text extent and its symbols
hdr = subprocess.run(['llvm-objdump', '-h', B + '.elf'], capture_output=True, text=True).stdout
text = [l.split() for l in hdr.splitlines() if l.split()[1:2] == ['.text']][0]
tsize, taddr = int(text[2], 16), int(text[3], 16)
assert taddr == VRAM
syms = []
for l in subprocess.run(['llvm-nm', '-n', B + '.elf'], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3 and p[1] in 'Tt' and VRAM <= int(p[0], 16) < VRAM + tsize: syms.append((int(p[0], 16), p[2]))
seen = {}; fs = []
for k, (a, nm) in enumerate(syms):
    if k + 1 < len(syms) and syms[k + 1][0] == a: continue          # (aliases: the last name)
    end = next((b for b, _ in syms[k + 1:] if b > a), VRAM + tsize)
    name = nm if nm not in seen else '%s_%08X' % (nm, a); seen[nm] = 1
    fs.append((name, a, end - a))
out = ['', '[[section]]', 'name = "rwblob"', 'rom = 0x%08X' % BLOB_ROM, 'vram = 0x%08X' % VRAM, 'size = 0x%X' % tsize, '', 'functions = [']
out += ['    { name = "%s", vram = 0x%08X, size = 0x%X },' % f for f in fs] + [']', '']
open(sys.argv[3], 'w').write('\n'.join(out))
print(len(fs), 'functions; text 0x%X; blob 0x%X bytes at ROM 0x%X' % (tsize, len(blob), BLOB_ROM))
