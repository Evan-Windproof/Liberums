/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32
.section .text.entry,"ax"
.global user_entry
user_entry:
    jmp phase6_user_main
.section .text,"ax"
.global phase6_spin, phase6_spin_loop, phase6_stale_write
phase6_spin:
    movl $0x11111111, %eax
    movl $0x22222222, %ebx
    movl $0x33333333, %ecx
    movl $0x44444444, %edx
    movl $0x55555555, %esi
    movl $0x66666666, %edi
    movl $0x77777777, %ebp
    pushl %eax
    movw $3, %ax             /* Null DS with RPL3; no DS memory operands below. */
    movw %ax, %ds
    movw $0x20, %ax          /* DPL3 data with RPL0 is legal at CPL3. */
    movw %ax, %es
    movw $0x21, %ax
    movw %ax, %fs
    movw $0x18, %ax          /* Readable DPL3 code with RPL0. */
    movw %ax, %gs
    popl %eax
    std
    stc
phase6_spin_loop:
    jmp phase6_spin_loop      /* No syscall, yield, or hidden completion condition. */
phase6_stale_write:
    movl $0xaabbccdd, 0x09000000
    movl $0xaabbccdd, 0x09001000
    jmp phase6_stale_write    /* Must #PF after forced revocation, never skip it. */
.section .note.GNU-stack,"",@progbits
