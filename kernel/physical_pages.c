/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/physical_pages.h>

#define PAGE_COUNT (PHYSICAL_LIMIT / PAGE_SIZE)
static uint8_t states[PAGE_COUNT];
static bool initialized;

/* Reserve every page touched by a half-open byte range, including partial pages. */
static void mark_range(uint64_t begin, uint64_t end, enum physical_page_state state)
{
    if (begin >= PHYSICAL_LIMIT || begin >= end) {
        return;
    }
    if (end > PHYSICAL_LIMIT) {
        end = PHYSICAL_LIMIT;
    }
    uint32_t first = (uint32_t)begin / PAGE_SIZE;
    uint32_t last = ((uint32_t)end + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t page = first; page < last; ++page) {
        states[page] = (uint8_t)state;
    }
}

bool physical_pages_init(const struct memory_map *map,
                         const struct physical_range *reserved, size_t count,
                         uint32_t kernel_begin, uint32_t kernel_end)
{
    if (initialized || map == NULL || map->count == 0 || map->count > MEMORY_REGION_MAX
        || (count != 0 && reserved == NULL) || kernel_begin < 0x100000
        || kernel_end > PHYSICAL_LIMIT
        || !memory_map_contains_ram(map, kernel_begin, kernel_end)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (reserved[i].begin >= reserved[i].end || reserved[i].end > PHYSICAL_LIMIT) {
            return false;
        }
    }
    for (size_t i = 0; i < map->count; ++i) {
        if (map->regions[i].begin >= map->regions[i].end) {
            return false;
        }
    }
    /* No state is changed until every input has passed validation. */
    /* Open only whole pages of type-1 RAM; holes and unaligned edges stay closed. */
    for (size_t i = 0; i < map->count; ++i) {
        const struct memory_region *r = &map->regions[i];
        if (r->type != 1 || r->begin >= PHYSICAL_LIMIT) {
            continue;
        }
        uint32_t begin = ((uint32_t)r->begin + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint32_t end = r->end > PHYSICAL_LIMIT ? PHYSICAL_LIMIT : (uint32_t)r->end;
        end &= ~(PAGE_SIZE - 1);
        mark_range(begin, end, PAGE_FREE);
    }
    for (size_t i = 0; i < map->count; ++i) {
        if (map->regions[i].type != 1) {
            mark_range(map->regions[i].begin, map->regions[i].end, PAGE_RESERVED);
        }
    }
    mark_range(0, 0x100000, PAGE_RESERVED); /* IVT/BDA, EBDA, VGA, ROM, firmware. */
    for (size_t i = 0; i < count; ++i) {
        mark_range(reserved[i].begin, reserved[i].end, PAGE_RESERVED);
    }
    mark_range(kernel_begin, kernel_end, PAGE_KERNEL); /* Includes static tables and stack. */
    initialized = true;
    return true;
}

enum physical_page_state physical_page_state(uint32_t address)
{
    if (!initialized || address >= PHYSICAL_LIMIT) {
        return PAGE_UNAVAILABLE;
    }
    return (enum physical_page_state)states[address / PAGE_SIZE];
}

bool physical_page_alloc(uint32_t *address)
{
    if (!initialized || address == NULL) {
        return false;
    }
    for (uint32_t page = 0; page < PAGE_COUNT; ++page) {
        if (states[page] == PAGE_FREE) {
            states[page] = PAGE_ALLOCATED;
            *address = page * PAGE_SIZE;
            return true;
        }
    }
    return false;
}

bool physical_page_alloc_specific(uint32_t address)
{
    if ((address & (PAGE_SIZE - 1)) != 0 || physical_page_state(address) != PAGE_FREE) {
        return false;
    }
    states[address / PAGE_SIZE] = PAGE_ALLOCATED;
    return true;
}

bool physical_page_free(uint32_t address)
{
    if ((address & (PAGE_SIZE - 1)) != 0 || physical_page_state(address) != PAGE_ALLOCATED) {
        return false;
    }
    states[address / PAGE_SIZE] = PAGE_FREE;
    return true;
}

uint32_t physical_pages_free_count(void)
{
    uint32_t count = 0;
    for (uint32_t page = 0; initialized && page < PAGE_COUNT; ++page) {
        if (states[page] == PAGE_FREE) {
            ++count;
        }
    }
    return count;
}
