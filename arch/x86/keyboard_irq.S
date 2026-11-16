/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32
.section .text, "ax"
.global keyboard_irq_entry
.type keyboard_irq_entry, @function
keyboard_irq_entry:
    pushl $0
    .global keyboard_irq_capture
keyboard_irq_capture:          /* Stable audit point after the first IRQ instruction. */
    pushl $33
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
    call keyboard_dispatch       /* Interrupt gate keeps IF=0 throughout C. */
    movl %ebx, %esp           /* IRQ1 returns to either CPL; it never schedules. */
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
    .global keyboard_irq_return
keyboard_irq_return:
    iret                     /* CPU restores the matching inner/outer frame. */
.section .note.GNU-stack, "", @progbits
