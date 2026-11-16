/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_BOOT_INFO_H
#define LIBERUMS_KERNEL_BOOT_INFO_H
#include <kernel/memory_map.h>

#define BOOT_RANGE_MAX 32
#define BOOT_MODULE_MAX 8u
#define BOOT_STRING_MAX 128
struct boot_context {
    struct memory_map map;
    struct physical_range reserved[BOOT_RANGE_MAX];
    size_t reserved_count;
    char commandline[BOOT_STRING_MAX];
    struct physical_range modules[BOOT_MODULE_MAX];
    uint32_t module_count;
};

/* Called only before paging, with loader-supplied physical addresses. */
bool boot_info_read(uint32_t magic, uint32_t address, struct boot_context *boot);
bool boot_has_option(const char *commandline, const char *wanted);
#endif
