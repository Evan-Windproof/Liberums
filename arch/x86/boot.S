/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32

/* Multiboot v1: page-align modules and request basic memory information. */
.set MB_MAGIC,    0x1badb002
.set MB_FLAGS,    0x00000003
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.balign 4
.long MB_MAGIC, MB_FLAGS, MB_CHECKSUM

.section .text.boot, "ax"
.global _start
.type _start, @function
_start:
    cli
    cld
    movl %eax, %esi              /* Preserve the Multiboot loader magic. */
                                /* EBX retains the information address. */
    lgdt gdt_pointer
    ljmp $0x08, $flat_segments

flat_segments:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* Establish our own zero-initialized C storage before using the stack. */
    xorl %eax, %eax
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    rep stosb

    movl $kernel_stack_top, %esp
    xorl %ebp, %ebp
    subl $8, %esp                /* 16-byte alignment immediately before call. */
    pushl %ebx                  /* cdecl second argument: information address. */
    pushl %esi                  /* cdecl first argument: loader magic. */
    call kernel_main

    /* Defensive fallback; kernel_main is declared _Noreturn. */
1:  cli
    hlt
    jmp 1b
.size _start, . - _start

.section .rodata.gdt, "a"
.balign 8
gdt:
    .quad 0x0000000000000000     /* Null descriptor. */
    .quad 0x00cf9a000000ffff     /* Ring 0, flat 32-bit executable segment. */
    .quad 0x00cf92000000ffff     /* Ring 0, flat 32-bit writable segment. */
gdt_end:
gdt_pointer:
    .word gdt_end - gdt - 1
    .long gdt

.section .bss.stack, "aw", @nobits
.balign 16
.global kernel_stack_bottom, kernel_stack_top
kernel_stack_bottom:
    .skip 16384
kernel_stack_top:

.section .note.GNU-stack, "", @progbits
