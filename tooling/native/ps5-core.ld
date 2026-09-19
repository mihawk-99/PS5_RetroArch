/* PS5 libretro shared cores: distinct 16 KiB code, read-only and data pages. */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
PHDRS {
    text PT_LOAD FLAGS (5);
    ro PT_LOAD FLAGS (4);
    data PT_LOAD FLAGS (6);
    dynamic PT_DYNAMIC FLAGS (6);
}
SECTIONS {
    .text 0 : ALIGN(CONSTANT(MAXPAGESIZE)) { *(.text .text.*) *(.plt .plt.*) } : text
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .rodata : { *(.rodata .rodata.*) } : ro
    .eh_frame_hdr : { KEEP(*(.eh_frame_hdr)) } : ro
    .eh_frame : { KEEP(*(.eh_frame)) } : ro
    .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) } : ro
    .note.gnu.build-id : { *(.note.gnu.build-id) } : ro
    .dynsym : { *(.dynsym) } : ro
    .gnu.hash : { *(.gnu.hash) } : ro
    .hash : { *(.hash) } : ro
    .dynstr : { *(.dynstr) } : ro
    .rela.dyn : { *(.rela.dyn) } : ro
    .rela.plt : { *(.rela.plt) } : ro
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data : { *(.data .data.*) } : data
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) } : data
    .got : { *(.got) *(.got.plt) } : data
    .preinit_array : { KEEP(*(.preinit_array)) } : data
    .init_array : { KEEP(*(.init_array .init_array.* .ctors .ctors.*)) } : data
    .fini_array : { KEEP(*(.fini_array .fini_array.* .dtors .dtors.*)) } : data
    .dynamic : { *(.dynamic) } : data : dynamic
    .bss : { *(.bss .bss.*) *(COMMON) } : data
}
