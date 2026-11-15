/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .rodata, "a"
.global user_a_start, user_a_end, user_b_start, user_b_end
user_a_start:
.incbin "build/user_a.bin"
user_a_end:
user_b_start:
.incbin "build/user_b.bin"
user_b_end:
.section .note.GNU-stack, "", @progbits
