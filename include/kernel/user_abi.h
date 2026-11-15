/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_USER_ABI_H
#define LIBERUMS_USER_ABI_H
#define USER_CODE 0x08000000u
#define USER_DATA 0x08001000u
#define USER_STACK 0x08002000u
#define USER_STACK_TOP 0x08003000u
#define USER_PROBE_A 0x08400000u
#define USER_PROBE_B 0x08800000u
#include <abi/syscall.h>
/* Explicit diagnostic scenarios; no user-visible physical mapping API. */
enum user_test_mode {
    USER_NORMAL, USER_KERNEL_READ, USER_KERNEL_WRITE, USER_CROSS_READ,
    USER_CROSS_WRITE, USER_CLI, USER_IO, USER_GATE, USER_TABLE_WRITE,
    USER_PHYSICAL_READ, USER_CODE_WRITE, USER_BAD_STACK, USER_FPU
};
#endif
