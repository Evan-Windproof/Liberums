/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_PHYSICAL_PAGES_H
#define LIBERUMS_KERNEL_PHYSICAL_PAGES_H
#include <kernel/memory_map.h>

enum physical_page_state {
    PAGE_UNAVAILABLE, PAGE_RESERVED, PAGE_FREE, PAGE_KERNEL, PAGE_ALLOCATED
};

/* Boot-only initialization. Permanent reservations cannot be released by free(). */
bool physical_pages_init(const struct memory_map *map,
                         const struct physical_range *reserved, size_t count,
                         uint32_t kernel_begin, uint32_t kernel_end);
enum physical_page_state physical_page_state(uint32_t address);
bool physical_page_alloc(uint32_t *address);
bool physical_page_alloc_specific(uint32_t address);
bool physical_page_free(uint32_t address);
uint32_t physical_pages_free_count(void);
#endif
