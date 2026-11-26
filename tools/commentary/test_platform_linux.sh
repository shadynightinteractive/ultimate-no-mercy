#!/bin/bash
# Builds comm_encode_test for Linux (gcc, glibc's libm and qsort) -- in WSL or on Linux -- to compare its clips with the
# Windows build's (test_platform.py): the developer's pack was encoded on Linux.
#   bash tools/commentary/test_platform_linux.sh <out executable>
set -e
W="$(cd "$(dirname "$0")/../../exe/WWFNoMercyRecomp" && pwd)"
O=$(mktemp -d)
CF="-O2 -ffp-contract=off -I$W/third_party/libogg/include -I$W/third_party/libvorbis/include -I$W/third_party/libvorbis/lib"
for f in $W/third_party/libogg/src/bitwise.c $W/third_party/libogg/src/framing.c $W/third_party/libvorbis/lib/*.c; do
  gcc $CF -w -c $f -o $O/$(basename $f .c).o; done
g++ -std=c++17 $CF $W/src/main/commentary/packbuild/test/comm_encode_test.cpp $W/src/main/commentary/packbuild/comm_encode.cpp $O/*.o -lm -o "$1"
rm -rf $O
echo built "$1"
