/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_MEMORY_MAP_H
#define LIBERUMS_KERNEL_MEMORY_MAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEMORY_REGION_MAX 128
#define PHYSICAL_LIMIT UINT32_C(0x04000000) /* Early boot supports low 64 MiB. */
#define PAGE_SIZE UINT32_C(4096)

struct memory_region {
    uint64_t begin, end; /* Half-open interval; retains firmware's 64-bit addresses. */
    uint32_t type;      /* Only type 1 is usable RAM; other types remain reserved. */
};
struct memory_map {
    struct memory_region regions[MEMORY_REGION_MAX];
    size_t count;
};
struct physical_range {
    uint32_t begin, end;
};

bool memory_map_parse(const void *bytes, size_t length, struct memory_map *map);
bool memory_map_contains_ram(const struct memory_map *map, uint64_t begin, uint64_t end);
#endif
