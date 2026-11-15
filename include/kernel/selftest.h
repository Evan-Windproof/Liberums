/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_SELFTEST_H
#define LIBERUMS_KERNEL_SELFTEST_H
#include <kernel/boot_info.h>
/* Explicit boot diagnostics selected by a complete selftest=... command-line token. */
void selftest_run(const struct boot_context *boot);
void selftest_address_space_failure(void);
#endif
