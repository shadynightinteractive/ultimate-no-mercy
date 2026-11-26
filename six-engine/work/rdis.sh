#!/bin/sh
# rdis.sh ROM VADDR LEN : disassemble main-segment code straight from a ROM (hex vaddr/len)
python3 -c "
import sys
r=open('$1','rb').read(); va=int('$2',16); n=int('$3',16)
o=va-0x80000450+4176; open('/tmp/devbox-0/rdis.bin','wb').write(r[o:o+n])"
cd /tmp/devbox-0 && llvm-objcopy -I binary -O elf32-bigmips --rename-section=.data=.text,code,alloc rdis.bin rdis.o
llvm-objdump -d --triple=mips-unknown-none-elf --mcpu=mips3 --adjust-vma=0x$2 --no-show-raw-insn rdis.o | grep -E '^ *[0-9a-f]+:'
