/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32
.section .text.entry,"ax"
.global _start
.type _start,@function
_start:
    xorl %ebp,%ebp
    andl $-16,%esp          /* The kernel supplied a private, zeroed user stack. */
    call libos_start       /* We choose our own C runtime, not a kernel service. */
1:  ud2                    /* libos_start never returns. */
    jmp 1b
.section .note.GNU-stack,"",@progbits
