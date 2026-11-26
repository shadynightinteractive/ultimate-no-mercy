ENTRY(_start)
SECTIONS {
    . = 0x80700000;
    .text : { *(.text.start) *(EXCLUDE_FILE(*announce.o) .text*) }
    .rodata : { *(EXCLUDE_FILE(*announce.o) .rodata*) }
    .data : { *(EXCLUDE_FILE(*announce.o) .data* EXCLUDE_FILE(*announce.o) .sdata*) }
    . = 0x80790000;   /* announce.c on its own, past the six-man engine's 64 KB at 0x80780000 */
    .ann : { *announce.o(.text* .rodata* .data* .sdata*) }
    . = 0x807A0000;   /* (the six-man engine's blob is at 0x80780000) */
    .bss : { *(EXCLUDE_FILE(*announce.o) .bss* EXCLUDE_FILE(*announce.o) .sbss* EXCLUDE_FILE(*announce.o) COMMON) }
    . = 0x807BE000;
    .annbss : { *announce.o(.bss* .sbss* COMMON) }
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.mdebug*) }
}
