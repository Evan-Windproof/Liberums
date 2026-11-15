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
