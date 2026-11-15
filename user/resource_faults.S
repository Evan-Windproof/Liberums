/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32
.section .text, "ax"
.global resource_fault_read, resource_fault_write, resource_read_site, resource_write_site
resource_fault_read:
    movl 4(%esp), %eax
resource_read_site:
    movl (%eax), %edx
    ret
resource_fault_write:
    movl 4(%esp), %eax
resource_write_site:
    movl $0xbad, (%eax)
    ret
.section .note.GNU-stack, "", @progbits
