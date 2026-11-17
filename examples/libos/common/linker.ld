/* SPDX-License-Identifier: GPL-3.0-or-later */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    ro PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
    abi PT_NOTE FLAGS(4);
}
SECTIONS {
    . = 0x08000000; /* Public ELF image window, not a physical address. */
    .text : { *(.text.entry) *(.text .text.*) } :text
    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) } :ro
    .note.liberums : { KEEP(*(.note.liberums)) } :ro :abi
    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note.GNU-stack) *(.eh_frame*) }
}
