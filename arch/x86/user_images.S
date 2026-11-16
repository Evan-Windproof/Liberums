/* SPDX-License-Identifier: GPL-3.0-or-later */
.section .rodata, "a"
.global user_a_start, user_a_end, user_b_start, user_b_end
user_a_start:
.incbin "build/user_a.bin"
user_a_end:
user_b_start:
.incbin "build/user_b.bin"
user_b_end:
.global resource_basic_start, resource_basic_end, resource_transfer_start, resource_transfer_end
.global resource_fault_start, resource_fault_end
resource_basic_start:
.incbin "build/resource_BASIC.bin"
resource_basic_end:
resource_transfer_start:
.incbin "build/resource_TRANSFER.bin"
resource_transfer_end:
resource_fault_start:
.incbin "build/resource_FAULT.bin"
resource_fault_end:
.section .note.GNU-stack, "", @progbits

.section .rodata,"a"
.global phase6_user_start, phase6_user_end
phase6_user_start:
.incbin "build/phase6-user.bin"
phase6_user_end:

.section .rodata,"a"
.global vga_user_start, vga_user_end
vga_user_start:
.incbin "build/vga-user.bin"
vga_user_end:

.section .rodata,"a"
.global interactive_user_start, interactive_user_end
interactive_user_start:
.incbin "build/interactive-user.bin"
interactive_user_end:
