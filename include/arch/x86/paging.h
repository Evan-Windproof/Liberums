/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_ARCH_X86_PAGING_H
#define LIBERUMS_ARCH_X86_PAGING_H
#include <kernel/boot_info.h>

bool paging_init(const struct boot_context *boot);
/* Early kernel only; cannot remove permanent kernel/boot mappings. */
bool paging_unmap_allocated(uint32_t address);
#endif
