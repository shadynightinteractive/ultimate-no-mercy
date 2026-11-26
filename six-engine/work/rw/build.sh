#!/bin/sh
# rw/build.sh oracle|full : the rewrites as a blob at 0x80780000 (rw/out/<layout>.bin) and its symbol list (.syms)
set -e
cd /home/devbox/work/six/rw
L=$1; mkdir -p out/$L
python3 gen_syms.py $L out/$L/syms.ld six.h stubs.S $(ls r_*.c 2>/dev/null)
FLAGS="--target=mips-unknown-none-elf -mcpu=mips2 -mabi=32 -O2 -fomit-frame-pointer -G0 -mno-abicalls -fno-pic -mno-check-zero-division -fno-builtin -nostdinc -ffreestanding -ffunction-sections -fno-jump-tables -Wall -Wextra -Wno-unused-parameter"
for f in r_*.c; do [ -e "$f" ] && clang $FLAGS -c $f -o out/$L/$(basename $f .c).o; done
clang --target=mips-unknown-none-elf -mcpu=mips2 -mabi=32 -mno-abicalls -fno-pic -c stubs.S -o out/$L/stubs.o
(cd out/$L && ld.lld -m elf32btsmip -T ../../blob.ld *.o -o blob.elf)
llvm-objcopy -O binary out/$L/blob.elf out/$L/blob.bin
llvm-nm -n out/$L/blob.elf | grep -E " [TDdRrBb] rw_" > out/$L/blob.syms
wc -c < out/$L/blob.bin; cat out/$L/blob.syms
