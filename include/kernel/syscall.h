/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_SYSCALL_H
#define LIBERUMS_KERNEL_SYSCALL_H
#include <arch/x86/idt.h>
void syscall_dispatch(struct exception_frame *frame);
void syscall_entry(void);
#endif
