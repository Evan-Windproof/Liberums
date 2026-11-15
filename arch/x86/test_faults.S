/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Deliberate faults, reachable only via explicit selftest boot options.
 * Separate labels let GDB verify the CPU-reported EIP, not just output text. */
.code32
.section .text, "ax"
.global test_ud, test_divide, test_gp, test_pf_read, test_pf_null, test_ro_write
.global test_unmapped, fault_ud, fault_divide, fault_gp, fault_pf_read, fault_pf_null
.global fault_ro_write, fault_unmapped
test_ud:
fault_ud:
    ud2
    ret
test_divide:
    movl $0x13579bdf, %eax
    xorl %edx, %edx
    xorl %ecx, %ecx
fault_divide:
    divl %ecx
    ret
test_gp:
    movw $0x18, %ax /* Beyond the three-entry GDT. */
fault_gp:
    movw %ax, %ds
    ret
test_pf_read:
fault_pf_read:
    movl 0x04000000, %eax
    ret
test_pf_null:
fault_pf_null:
    movl 0, %eax
    ret
test_ro_write:
    movl $__kernel_start, %eax
fault_ro_write:
    movb $0, (%eax)
    ret
test_unmapped:
    movl 4(%esp), %eax
fault_unmapped:
    movl (%eax), %edx
    ret
.section .note.GNU-stack, "", @progbits
