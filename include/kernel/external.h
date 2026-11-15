/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_EXTERNAL_H
#define LIBERUMS_EXTERNAL_H
#include <kernel/boot_info.h>
void external_domains_run(const struct boot_context *boot);
void elf_test_pressure_begin(bool enabled);
void elf_test_pressure_end(void);
#endif
