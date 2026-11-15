/* SPDX-License-Identifier: GPL-3.0-or-later */
.code32
.section .text, "ax"
.extern exception_dispatch

.type isr_0, @function
isr_0:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $0
    jmp isr_common

.type isr_1, @function
isr_1:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $1
    jmp isr_common

.type isr_2, @function
isr_2:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $2
    jmp isr_common

.type isr_3, @function
isr_3:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $3
    jmp isr_common

.type isr_4, @function
isr_4:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $4
    jmp isr_common

.type isr_5, @function
isr_5:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $5
    jmp isr_common

.type isr_6, @function
isr_6:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $6
    jmp isr_common

.type isr_7, @function
isr_7:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $7
    jmp isr_common

.type isr_8, @function
isr_8:
    pushl $8
    jmp isr_common

.type isr_9, @function
isr_9:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $9
    jmp isr_common

.type isr_10, @function
isr_10:
    pushl $10
    jmp isr_common

.type isr_11, @function
isr_11:
    pushl $11
    jmp isr_common

.type isr_12, @function
isr_12:
    pushl $12
    jmp isr_common

.type isr_13, @function
isr_13:
    pushl $13
    jmp isr_common

.type isr_14, @function
isr_14:
    pushl $14
    jmp isr_common

.type isr_15, @function
isr_15:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $15
    jmp isr_common

.type isr_16, @function
isr_16:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $16
    jmp isr_common

.type isr_17, @function
isr_17:
    pushl $17
    jmp isr_common

.type isr_18, @function
isr_18:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $18
    jmp isr_common

.type isr_19, @function
isr_19:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $19
    jmp isr_common

.type isr_20, @function
isr_20:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $20
    jmp isr_common

.type isr_21, @function
isr_21:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $21
    jmp isr_common

.type isr_22, @function
isr_22:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $22
    jmp isr_common

.type isr_23, @function
isr_23:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $23
    jmp isr_common

.type isr_24, @function
isr_24:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $24
    jmp isr_common

.type isr_25, @function
isr_25:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $25
    jmp isr_common

.type isr_26, @function
isr_26:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $26
    jmp isr_common

.type isr_27, @function
isr_27:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $27
    jmp isr_common

.type isr_28, @function
isr_28:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $28
    jmp isr_common

.type isr_29, @function
isr_29:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $29
    jmp isr_common

.type isr_30, @function
isr_30:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $30
    jmp isr_common

.type isr_31, @function
isr_31:
    pushl $0 /* This i686 exception has no CPU-supplied error code. */
    pushl $31
    jmp isr_common

.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    pushl $0
    pushl $128
    jmp isr_common

isr_common:
    pushal
    cld
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movl %esp, %ebx /* Pointer to the saved frame, before stack alignment. */
    andl $-16, %esp
    subl $12, %esp
    pushl %ebx
    cmpl $128, 32(%ebx)
    je syscall_call
    call exception_dispatch
syscall_call:
    call syscall_dispatch
    movl %ebx, %esp
    jmp user_iret

.section .rodata, "a"
.balign 4
.global isr_entries
isr_entries:
    .long isr_0
    .long isr_1
    .long isr_2
    .long isr_3
    .long isr_4
    .long isr_5
    .long isr_6
    .long isr_7
    .long isr_8
    .long isr_9
    .long isr_10
    .long isr_11
    .long isr_12
    .long isr_13
    .long isr_14
    .long isr_15
    .long isr_16
    .long isr_17
    .long isr_18
    .long isr_19
    .long isr_20
    .long isr_21
    .long isr_22
    .long isr_23
    .long isr_24
    .long isr_25
    .long isr_26
    .long isr_27
    .long isr_28
    .long isr_29
    .long isr_30
    .long isr_31

.section .note.GNU-stack, "", @progbits
