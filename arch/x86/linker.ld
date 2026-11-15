/* SPDX-License-Identifier: GPL-3.0-or-later */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* Read and execute. */
    rodata PT_LOAD FLAGS(4); /* Read only. */
    data PT_LOAD FLAGS(6);   /* Read and write. */
}

SECTIONS
{
    . = 1M;
    __kernel_start = .;
    .multiboot ALIGN(4) : { KEEP(*(.multiboot)) } :text
    .text ALIGN(16) : { *(.text.boot) *(.text .text.*) } :text
    .rodata ALIGN(4K) : { *(.rodata .rodata.*) } :rodata
    .data ALIGN(4K) : { *(.data .data.*) } :data
    .bss ALIGN(16) (NOLOAD) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    } :data
    __kernel_end = .;

    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}

ASSERT(SIZEOF(.multiboot) == 12, "Expected one Multiboot v1 header")
ASSERT((kernel_stack_top & 15) == 0, "Kernel stack must be 16-byte aligned")
