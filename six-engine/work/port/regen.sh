#!/bin/sh
# The PC port from a six-man ROM, start to end:
#   port/regen.sh SIX_ROM
# 1. the port ROM (six ROM + the blob at ROM 0x1FF0000) and the blob's [[section]] in syms/dump.toml
# 2. N64Recomp + the mandatory post-regen pipeline (BUILDING.md section 4) in /home/devbox/src/NoMercySix
# 3. RecompiledFuncs, six_blob.h and six_rompatch.h into WWFNoMercyRecomp (branch six-engine)
# 4. the exe (build-mingw: NoMercyRecompiled -> "Ultimate No Mercy.exe")
set -e
SIX=$1
NMS=/home/devbox/src/NoMercySix
WWF=/home/devbox/src/WWFNoMercyRecomp
T=/tmp/devbox-0/regen; mkdir -p $T
python3 /home/devbox/work/six/port/mkport.py "$SIX" $T/port.z64 $T/rwblob.toml
# the rwblob section of dump.toml replaced by this blob's
python3 - "$NMS/syms/dump.toml" $T/rwblob.toml <<'E'
import sys, re
p, q = sys.argv[1], sys.argv[2]
s = open(p).read(); new = open(q).read().strip() + '\n'
i = s.find('[[section]]\nname = "rwblob"')
if i < 0: s = s.rstrip('\n') + '\n\n' + new
else:
    j = s.find('[[section]]', i + 10)
    s = s[:i] + new + ('\n' + s[j:] if j >= 0 else '')
open(p, 'w').write(s)
E
cp $T/port.z64 $NMS/nomercy.z64
cd $NMS
rm -rf RecompiledFuncs.prev; [ -d RecompiledFuncs ] && mv RecompiledFuncs RecompiledFuncs.prev
mkdir RecompiledFuncs
./N64Recomp nomercy.toml > $T/n64recomp.log 2>&1 || { tail -20 $T/n64recomp.log; exit 1; }
for s in fix_stumps fix_switches fix_jraddend fix_backbranches fix_selfentry readd_hand_edits audit_coresidency; do
    echo "== $s"; python3 tools/$s.py 2>&1 | tail -4
done
python3 tools/romhack/list_repaired_functions.py --recompiler ./N64Recomp 2>&1 | tail -2
# into the port
rm -rf $WWF/RecompiledFuncs; cp -r RecompiledFuncs $WWF/RecompiledFuncs
python3 - /home/devbox/work/six/rw/out/full/blob.bin $WWF/src/main/six_blob.h <<'E'
import sys, struct
b = open(sys.argv[1], 'rb').read(); b += b'\0' * (-len(b) % 4)
w = struct.unpack('>%dI' % (len(b) // 4), b)
L = ['// Generated from the six-man engine blob (rw/out/full/blob.bin): code + data at 0x80780000.', '#pragma once',
     '#include <cstdint>', 'static const uint32_t six_blob_vram = 0x80780000u;',
     'static const uint32_t six_blob_words[%d] = {' % len(w)]
for i in range(0, len(w), 8): L.append('    ' + ', '.join('0x%08X' % x for x in w[i:i + 8]) + ',')
L.append('};')
open(sys.argv[2], 'w').write('\n'.join(L) + '\n')
E
python3 /home/devbox/work/six/port/gen_rompatch.py /home/devbox/work/nomercy.z64 $T/port.z64 $WWF/src/main/six_rompatch.h
cd /home/devbox/build-mingw && cmake . > /dev/null && ninja -j2 NoMercyRecompiled 2>&1 | tail -2
ls -la "/home/devbox/build-mingw/Ultimate No Mercy.exe"
