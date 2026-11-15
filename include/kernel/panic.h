/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_PANIC_H
#define LIBERUMS_KERNEL_PANIC_H
_Noreturn void kernel_stop(void);
_Noreturn void panic(const char *reason);
#endif
