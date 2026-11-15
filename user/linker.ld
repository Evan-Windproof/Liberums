/* SPDX-License-Identifier: GPL-3.0-or-later */
OUTPUT_FORMAT(elf32-i386)
ENTRY(user_entry)
SECTIONS {
    . = 0x08000000;
    .text : { *(.text.entry) *(.text .text.*) *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(SIZEOF(.text) <= 4096, "User image exceeds its code page")
ASSERT(SIZEOF(.data) == 0 && SIZEOF(.bss) == 0, "Use the explicit private data page")
