/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/paging.h>
#include <kernel/elf_loader.h>
#include <kernel/physical_pages.h>

static bool allocate_page(struct address_space *space, uint32_t *page)
{
    if (space->count == ADDRESS_SPACE_PAGE_LIMIT || !physical_page_alloc(page)) {
        return false;
    }
    space->pages[space->count++] = *page;
    volatile uint32_t *words = (void *)(uintptr_t)*page;
    for (uint32_t i = 0; i < 1024; ++i) {
        words[i] = 0;
    }
    return true;
}

int32_t address_space_create_external(struct address_space *space, const uint8_t *image,
                                      const struct elf_plan *plan)
{
    if (space->live || space->count != 0 || plan->segment_count == 0) {
        return RES_EINVAL;
    }
    space->external = true;
    space->rollback_pages = 0;
    uint32_t directory_address, table_address;
    if (!allocate_page(space, &directory_address) || !allocate_page(space, &table_address)) {
        goto exhausted;
    }
    uint32_t *directory = (void *)(uintptr_t)directory_address;
    uint32_t *table = (void *)(uintptr_t)table_address;
    paging_copy_kernel(directory);
    directory[LIBERUMS_IMAGE_BASE >> 22] = table_address | 7;
    for (uint32_t i = 0; i < plan->segment_count; ++i) {
        const struct elf_segment *segment = &plan->segments[i];
        uint32_t begin = segment->address & ~4095u;
        uint32_t end = (segment->address + segment->memory_size + 4095) & ~4095u;
        for (uint32_t va = begin; va < end; va += PAGE_SIZE) {
            uint32_t page;
            if (!allocate_page(space, &page)) {
                goto exhausted;
            }
            table[(va >> 12) & 1023] = page | 5 | ((segment->flags & 2) ? 2 : 0);
        }
        /* Copy file bytes through supervisor physical aliases, even for R/O code.
         * Every page was zeroed, so BSS and partial-page padding remain zero. */
        for (uint32_t offset = 0; offset < segment->file_size; ++offset) {
            uint32_t va = segment->address + offset;
            uint32_t physical = (table[(va >> 12) & 1023] & ~4095u) + (va & 4095);
            *(uint8_t *)(uintptr_t)physical = image[segment->offset + offset];
        }
    }
    for (uint32_t va = LIBERUMS_STACK_BASE; va < LIBERUMS_STACK_TOP; va += PAGE_SIZE) {
        uint32_t page;
        if (!allocate_page(space, &page)) {
            goto exhausted;
        }
        table[(va >> 12) & 1023] = page | 7;
    }
    space->live = true;
    return 0;
exhausted:
    space->rollback_pages = space->count;
    address_space_destroy(space); /* Never active: no user translation can survive. */
    return RES_ENOMEM;
}
