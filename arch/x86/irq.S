/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32
.section .text, "ax"
.global timer_irq_entry
.type timer_irq_entry, @function
timer_irq_entry:
    pushl $0
    .global timer_irq_capture
timer_irq_capture:
    pushl $32
    pushal                    /* Save EVERY GP register before using AX. */
    xorl %eax, %eax
    movw %ds, %ax
    pushl %eax
    movw %es, %ax
    pushl %eax
    movw %fs, %ax
    pushl %eax
    movw %gs, %ax
    pushl %eax
    cld
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movl %esp, %ebx
    andl $-16, %esp
    subl $12, %esp
    pushl %ebx
    call timer_dispatch       /* Interrupt gate keeps IF=0 throughout C. */
    movl %ebx, %esp           /* Only the kernel-origin path returns here. */
    popl %eax
    movw %ax, %gs
    popl %eax
    movw %ax, %fs
    popl %eax
    movw %ax, %es
    popl %eax
    movw %ax, %ds
    popal
    addl $8, %esp
    iret                     /* Ring 0 has no outer ESP/SS: do not pop them. */
.section .note.GNU-stack, "", @progbits
