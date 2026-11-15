/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32
.section .text, "ax"
.global user_register_test
.type user_register_test, @function
user_register_test:
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl $0x11223344, %ebx
    movl $0x22334455, %ecx
    movl $0x33445566, %edx
    movl $0x44556677, %esi
    movl $0x55667788, %edi
    movl $0x66778899, %ebp
    movl $0xffffffff, %eax
    int $0x80
    cmpl $-38, %eax
    jne register_failure
    /* Yield exercises saving the full user context, not just a returning call.
     * NT is user-settable; the kernel must clear it before its own iret. */
    pushfl
    orl $0x4000, (%esp)
    popfl
    std
    movl $0, %eax
    int $0x80
    pushfl
    popl %eax
    cld
    testl $0x4000, %eax
    jnz register_failure
    testl $0x400, %eax
    jz register_failure
    cmpl $0x11223344, %ebx
    jne register_failure
    cmpl $0x22334455, %ecx
    jne register_failure
    cmpl $0x33445566, %edx
    jne register_failure
    cmpl $0x44556677, %esi
    jne register_failure
    cmpl $0x55667788, %edi
    jne register_failure
    cmpl $0x66778899, %ebp
    jne register_failure
    xorl %eax, %eax
    jmp register_done
register_failure:
    cld
    movl $1, %eax
register_done:
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

.global user_fault, user_fault_kernel_read, user_fault_kernel_write
.global user_fault_cross_read, user_fault_cross_write, user_fault_cli, user_fault_io
.global user_fault_gate, user_fault_table_write, user_fault_physical_read
.global user_fault_code_write, user_fault_bad_stack, user_fault_fpu
user_fault:
    movl 4(%esp), %eax
    movl 8(%esp), %edx
    cmpl $1, %eax
    je user_fault_kernel_read
    cmpl $2, %eax
    je user_fault_kernel_write
    cmpl $3, %eax
    je user_fault_cross_read
    cmpl $4, %eax
    je user_fault_cross_write
    cmpl $5, %eax
    je user_fault_cli
    cmpl $6, %eax
    je user_fault_io
    cmpl $7, %eax
    je user_fault_gate
    cmpl $8, %eax
    je user_fault_table_write
    cmpl $9, %eax
    je user_fault_physical_read
    cmpl $10, %eax
    je user_fault_code_write
    cmpl $11, %eax
    je user_fault_stack_setup
    cmpl $12, %eax
    je user_fault_fpu
    ret
user_fault_kernel_read:
    movl 0x100000, %eax
    ret
user_fault_kernel_write:
    movb $0, 0x100000
    ret
user_fault_cross_read:
    movl (%edx), %eax
    ret
user_fault_cross_write:
    movb $0, (%edx)
    ret
user_fault_cli:
    cli
    ret
user_fault_io:
    outb %al, $0x80
    ret
user_fault_gate:
    int $3             /* IDT[3] is DPL 0, so software invocation must #GP. */
    ret
user_fault_table_write:
    movb $0, (%edx)
    ret
user_fault_physical_read:
    movl (%edx), %eax
    ret
user_fault_code_write:
    movb $0, 0x08000000
    ret
user_fault_stack_setup:
    movl $0, %esp      /* Hardware still switches to the trusted TSS stack. */
user_fault_bad_stack:
    ud2
user_fault_fpu:
    fninit
    ret
.section .note.GNU-stack, "", @progbits
