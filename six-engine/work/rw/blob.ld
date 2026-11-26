OUTPUT_ARCH(mips)
SECTIONS {
    . = 0x80780000;
    .text : { *(.text .text.*) } =0
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    .bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
    /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) }
}
INCLUDE syms.ld
