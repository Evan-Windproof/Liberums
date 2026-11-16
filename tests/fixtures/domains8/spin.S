/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32
.section .text,"ax"
.global worker_spin,worker_spin_begin,worker_spin_end
.type worker_spin,@function
worker_spin:
    pushal
    movl $0x11111111,%eax
    movl $0x22222222,%ebx
    movl $0x33333333,%ecx
    movl $0x44444444,%edx
    movl $0x55555555,%esi
    movl $0x66666666,%edi
    movl $0x77777777,%ebp
worker_spin_begin:
    std
    stc
    cmpl $3,worker_command
    je worker_spin_begin
worker_spin_end:
    cld
    popal
    ret
.section .note.GNU-stack,"",@progbits
