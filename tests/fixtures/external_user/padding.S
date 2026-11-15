/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .text, "ax"
.global external_far_function
.type external_far_function, @function
/* Force actual execution beyond the first code page. */
.balign 4096
.space 4096, 0x90
external_far_function:
    movl $0x12345678, %eax
    ret
.section .note.GNU-stack, "", @progbits
