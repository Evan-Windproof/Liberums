/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32
.section .bss, "aw", @nobits
.balign 4
scheduler_esp:
    .skip 4
.section .text, "ax"
.global arch_user_enter, arch_user_leave, user_iret
.type arch_user_enter, @function
arch_user_enter:
    pushal
    movl %esp, scheduler_esp
    /* No FPU state virtualization yet. TS forces user x87/MMX/SIMD access
     * to #NM rather than exposing another domain's unsaved register state. */
    movl %cr0, %eax
    orl $8, %eax
    movl %eax, %cr0
    movl 36(%esp), %ebx  /* Argument after eight saved registers and return EIP. */
    movl 40(%esp), %edx /* Segment state is separate from the stable CPU prefix. */
    movl %ebx, %esp
    movw 0(%edx), %ax
    movw %ax, %gs
    movw 4(%edx), %ax
    movw %ax, %fs
    movw 8(%edx), %ax
    movw %ax, %es
    movw 12(%edx), %ax
    movw %ax, %ds       /* DS may be null: no more DS memory operands after this. */
    jmp restore_cpu
.type arch_user_leave, @function
arch_user_leave:
    /* Discard the domain trap stack; resume the trusted scheduler call frame. */
    movl scheduler_esp, %esp
    popal
    ret
user_iret:
    /* C normalizes segment selectors and EFLAGS before this path. */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
restore_cpu:
    popal
    addl $8, %esp       /* Discard normalized vector/error, not CPU's iret frame. */
    iret               /* Pops EIP/CS/EFLAGS and, for ring 3, ESP/SS. */
.section .note.GNU-stack, "", @progbits
